// Red Faction: Guerrilla's HUD in the worlds ported from Guerrilla, Armageddon's own everywhere else.
//
// Both games build their HUD from the same kind of interface documents (VINT: elements driven by a
// Lua script). Armageddon loads its HUD (the document ui_hud) once at start-up and keeps it through
// every world. This mod adds Guerrilla's HUD documents on top of it while a world named by
// [GuerrillaHud] Worlds= is played (the ported world is "rfg"), and takes them away again in any
// other world. Each document's script (in mods\guerrilla_hud\) fades out the parts of Armageddon's
// HUD it replaces while it is loaded, and back in when it is unloaded.
//
// The documents are Guerrilla's own, made loadable by GuerrillaHud.exe from the player's own copy
// of Guerrilla (never shipped): see RfaFormats\Interface\GuerrillaHud.cs. Without them the mod only
// says so in the log.
//
// When: the HUD's data update (0x57BC30), which the HUD's per-frame process (0x57B520) calls every
// frame while the HUD is up, is wrapped. After the game's own update the mod looks at the world's
// name (kept in the world object, 0x1870630, at +0x29EA, as the time of day reads it to name its
// files) and loads or unloads the documents the way the game loads its menus while playing, putting
// them in a draw list of the interface as the console's vint_doc_load does (a document that is in
// none is loaded but never drawn). The interface's draw pass (0x564FF0) is wrapped too: our
// documents stay on the draw list only while the HUD is being updated, so nothing of them is left
// on loading screens or in the menus.
#define RFA_MOD_NAME "guerrilla_hud"
#include "rfa_mod.h"
#include "rfa_game.h"
#include <string>
#include <vector>

namespace {

// ---- The game

// The world being played: "world" for the campaign, "rfg" for the ported world.
const char* WorldName() { return reinterpret_cast<const char*>(0x1870630 + 0x29EA); }

// The HUD's data update: fills the data items its scripts read (health_hud, reticule_hud, ...).
constexpr DWORD kHudUpdate = 0x57BC30;
constexpr size_t kHudUpdatePrologue = 6;  // push ebp / mov ebp,esp / mov eax,[ebp+8]
using HudUpdateFn = void(__cdecl*)(void* hud);
HudUpdateFn g_hudUpdate;

// The interface's documents. The project (rfg.vint_proj) lists every document the game can load;
// a loaded ("open") document has its handle at +0x40 of its record.
using InProjectFn = void*(__cdecl*)(const char* name);
using OpenDocumentFn = BYTE*(__cdecl*)(const char* name);
using LoadDocumentFn = unsigned short(__cdecl*)(const char* name, int, float, int, int, int, int);
using UnloadDocumentFn = void(__cdecl*)(unsigned int handle);
auto const InProject = reinterpret_cast<InProjectFn>(0xC50790);
auto const OpenDocument = reinterpret_cast<OpenDocumentFn>(0x562840);
auto const LoadDocument = reinterpret_cast<LoadDocumentFn>(0x563990);      // the handle, 0xFFFF on failure
auto const UnloadDocument = reinterpret_cast<UnloadDocumentFn>(0x5628A0);
constexpr unsigned short kNoDocument = 0xFFFF;

// A loaded document is drawn only while its handle is in one of the interface's draw lists, which
// its draw pass (0x564FF0) goes through every frame. The console's vint_doc_load puts what it loads
// in this one (0x561E30), and vint_doc_unload takes it out again before unloading (0x561EC0); the
// game's own screens are drawn through their own lists. Methods of the list (in ECX), each given
// the handle by pointer: add (only if it isn't there yet), remove.
auto const g_drawList = reinterpret_cast<void*>(0x133A2D4);
using DrawListFn = int(__fastcall*)(void* list, void* unused, const unsigned short* handle);
auto const AddToDrawList = reinterpret_cast<DrawListFn>(0x566120);
auto const RemoveFromDrawList = reinterpret_cast<DrawListFn>(0x5661C0);

// Loads a document as the menus (0x58D050) and the console's vint_doc_load (0x561E30) do while
// playing: mode 0 streams its containers in. Not as rf4_hud_post_boot_init (0x581490) loads
// ui_hud at start-up: its mode 2 reads the document there and then (0xC52540), which is only
// meant for start-up; asked for while playing, the game ended (2026-10-31).
unsigned short LoadWhilePlaying(const char* name) { return LoadDocument(name, 0, 0.0f, 0, 0, 0x80, 0); }

// ---- The mod

struct Document {
    std::string name;
    bool loaded = false;   // the mod asked for it
    bool seen = false;     // and the game has had it open since
    bool missing = false;  // not in the game's interface project, or the game couldn't load it
    bool listed = false;   // in the draw list (only while the HUD runs)
};

// The interface's draw pass, bool(bool), cdecl: draws every document in its draw lists, every frame,
// whatever is on screen (loading screens and menus too).
constexpr DWORD kDrawPass = 0x564FF0;
constexpr size_t kDrawPassPrologue = 9;  // push ebp / mov ebp,esp / sub esp,0E8h
using DrawPassFn = bool(__cdecl*)(bool);
DrawPassFn g_drawPass;

// Our documents are drawn only while the HUD itself is updated (it is every frame in play): once
// it hasn't been for this long (loading screens, the main menu, ...), they come off the draw list.
constexpr ULONGLONG kHudGoneAfterMs = 250;
ULONGLONG g_lastHudUpdate;
std::vector<Document> g_documents;
std::vector<std::string> g_worlds;
int g_inOurWorld = -1;  // last frame's answer, to log the switches (-1 before the first frame)

std::vector<std::string> SplitList(const std::wstring& text) {
    std::vector<std::string> names;
    std::string current;
    for (wchar_t c : text) {
        if (c == L',' || c == L' ' || c == L'\t') {
            if (!current.empty()) names.push_back(current);
            current.clear();
        } else {
            current += static_cast<char>(c);
        }
    }
    if (!current.empty()) names.push_back(current);
    return names;
}

bool InOurWorld() {
    const char* world = WorldName();
    for (const std::string& name : g_worlds)
        if (_stricmp(world, name.c_str()) == 0) return true;
    return false;
}

// In our worlds, every document is loaded (and loaded again if the game closes it); elsewhere, the
// ones the mod loaded are unloaded.
void FollowTheWorld() {
    const bool ours = InOurWorld();
    if (static_cast<int>(ours) != g_inOurWorld) {
        rfa::Log(ours ? "World %s: Guerrilla's HUD" : "World %s: Armageddon's own HUD", WorldName());
        g_inOurWorld = ours;
    }
    for (Document& doc : g_documents) {
        BYTE* open = OpenDocument(doc.name.c_str());
        if (open && !doc.seen) {
            if (doc.loaded) rfa::Log("The game has %s open", doc.name.c_str());
            doc.seen = true;
        }
        if (!ours) {
            if (doc.loaded && open) {
                const unsigned short handle = *reinterpret_cast<unsigned short*>(open + 0x40);
                if (doc.listed) RemoveFromDrawList(g_drawList, nullptr, &handle);
                UnloadDocument(handle);
                rfa::Log("Unloaded %s", doc.name.c_str());
            }
            doc.loaded = doc.seen = doc.listed = false;
            continue;
        }
        if (open || doc.missing || (doc.loaded && !doc.seen)) continue;
        if (doc.loaded) rfa::Log("The game closed %s; loading it again", doc.name.c_str());
        if (!InProject(doc.name.c_str())) {
            rfa::Log("%s isn't in the game's interface project: make Guerrilla's HUD with editor\\GuerrillaHud.exe "
                     "(from your copy of Guerrilla) first", doc.name.c_str());
            doc.missing = true;
            continue;
        }
        rfa::Log("Loading %s", doc.name.c_str());  // before the game's call, so a log ending here points at it
        const unsigned short handle = LoadWhilePlaying(doc.name.c_str());
        if (handle == kNoDocument) {
            rfa::Log("The game couldn't load %s", doc.name.c_str());
            doc.missing = true;
            continue;
        }
        AddToDrawList(g_drawList, nullptr, &handle);
        rfa::Log("Loaded %s (handle %u), and listed it to be drawn", doc.name.c_str(), handle);
        doc.loaded = doc.listed = true;
        doc.seen = false;
    }
}

void __cdecl HudUpdate(void* hud) {
    g_hudUpdate(hud);
    g_lastHudUpdate = GetTickCount64();
    FollowTheWorld();
}

// Before each draw pass: our loaded documents on the draw list while the HUD runs, off it while it
// doesn't.
bool __cdecl DrawPass(bool flag) {
    const bool hudRuns = GetTickCount64() - g_lastHudUpdate <= kHudGoneAfterMs;
    for (Document& doc : g_documents) {
        if (!doc.loaded || doc.listed == hudRuns) continue;
        BYTE* open = OpenDocument(doc.name.c_str());
        if (!open) continue;
        const unsigned short handle = *reinterpret_cast<unsigned short*>(open + 0x40);
        if (hudRuns) AddToDrawList(g_drawList, nullptr, &handle);
        else RemoveFromDrawList(g_drawList, nullptr, &handle);
        doc.listed = hudRuns;
        RFA_LOG_FIRST(20, hudRuns ? "%s shown again: the HUD runs" : "%s hidden: the HUD isn't running", doc.name.c_str());
    }
    return g_drawPass(flag);
}

}  // namespace

extern "C" __declspec(dllexport) void __cdecl RFAMod_Init() {
    if (!rfa::game::IsSupportedBuild()) {
        rfa::Log("Not the publicbeta build; nothing to do");
        return;
    }
    g_worlds = SplitList(rfa::IniString(L"GuerrillaHud", L"Worlds", L"rfg"));
    for (const std::string& name : SplitList(rfa::IniString(L"GuerrillaHud", L"Documents", L"rfg_ui_hud_health")))
        g_documents.push_back({name});
    if (g_worlds.empty() || g_documents.empty()) {
        rfa::Log("No worlds or no documents under [GuerrillaHud]; nothing to do");
        return;
    }
    g_hudUpdate = reinterpret_cast<HudUpdateFn>(rfa::HookFunction(kHudUpdate, kHudUpdatePrologue, reinterpret_cast<void*>(&HudUpdate)));
    if (!g_hudUpdate) {
        rfa::Log("Could not hook the HUD's update; Guerrilla's HUD stays off");
        return;
    }
    g_drawPass = reinterpret_cast<DrawPassFn>(rfa::HookFunction(kDrawPass, kDrawPassPrologue, reinterpret_cast<void*>(&DrawPass)));
    if (!g_drawPass) rfa::Log("Could not hook the interface's draw pass; Guerrilla's HUD may stay up on loading screens and menus");
    std::string documents, worlds;
    for (const Document& doc : g_documents) documents += (documents.empty() ? "" : ", ") + doc.name;
    for (const std::string& world : g_worlds) worlds += (worlds.empty() ? "" : ", ") + world;
    rfa::Log("Guerrilla's HUD (%s) in the worlds %s", documents.c_str(), worlds.c_str());
}
