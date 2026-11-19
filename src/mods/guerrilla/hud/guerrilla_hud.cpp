// Red Faction: Guerrilla's HUD in the worlds ported from Guerrilla, Armageddon's own everywhere else.
//
// Both games build their HUD from the same kind of interface documents (VINT: elements driven by a
// Lua script). Armageddon loads its HUD (the document ui_hud) once at start-up and keeps it through
// every world. This mod adds Guerrilla's HUD documents on top of it while a world named by
// [GuerrillaHud] Worlds= is played (the ported world is "rfg"), and takes them away again in any
// other world. Each document's script (in mods\guerrilla_hud\) fades out the parts of Armageddon's
// HUD it replaces while it is loaded, and back in when it is unloaded.
//
// The documents are Guerrilla's own, made loadable by GuerrillaBuilder.exe from the player's own copy
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
// on loading screens or in the menus. And the start of every level load (0x55D870) is wrapped:
// all of our documents are unloaded there, as the load frees their memory (see LoadStart).
//
// Guerrilla's map ([GuerrillaHud] Map=1): its minimap (the document rfg_ui_hud) and its world map
// (rfg_ui_fullscreen, opened and closed with [GuerrillaKeys] Map=, see keys.cpp). Guerrilla drew the map itself
// in its HUD's map elements (its "cartographer"); Armageddon kept that code only for its compass. So
// here the documents' scripts build the map from pictures, Guerrilla's own: its world map picture,
// each zone's own picture, its minimap's frame, mask and player arrow. What they need to know each
// frame, this mod tells them through Lua functions it adds to the interface's Lua (see
// RFAMod_RegisterLua): where the player is on Guerrilla's map, which way the camera looks, the
// district, and which zones' pictures are loaded. Those come as Guerrilla keeps them, each zone's
// picture on its own (here a document that only brings that texture), loaded while the player is
// near.
#define RFA_MOD_NAME "guerrilla_hud"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "hud_api.h"
#include "campaign/campaign_api.h"
#include "gear/gear_api.h"
#include "people/people_api.h"
#include "save/save_api.h"
#include "world/world_api.h"
#include "rfa_game.h"
#include "keys.h"
#include "tool_tips.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#pragma comment(lib, "user32.lib")  // the world map's key: GetAsyncKeyState, GetForegroundWindow

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
using DocumentStatusFn = int(__cdecl*)(unsigned int handle);
auto const InProject = reinterpret_cast<InProjectFn>(0xC50790);
auto const OpenDocument = reinterpret_cast<OpenDocumentFn>(0x562840);
auto const LoadDocument = reinterpret_cast<LoadDocumentFn>(0x563990);      // the handle, 0xFFFF on failure
auto const UnloadDocument = reinterpret_cast<UnloadDocumentFn>(0x5628A0);
auto const DocumentStatus = reinterpret_cast<DocumentStatusFn>(0x565DA0);  // 3 once it and its textures are in
constexpr unsigned short kNoDocument = 0xFFFF;
constexpr int kDocumentReady = 3;

// A picture of the interface's sheets, by name (vint_bms_load, as Armageddon's own map code looks up
// its pictures): its number, or -1 if no sheet has it. The sheets are read at start-up, so this says
// whether a picture exists even before the texture it is cut from is loaded.
using FindPictureFn = int(__cdecl*)(const char* name);
auto const FindPicture = reinterpret_cast<FindPictureFn>(0xC4F6F0);

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

// The local player (as Guerrilla's object: its position at +4), and the camera manager's
// orientation (0x11B9C40 +0x50, rows right, up, forward).
auto& g_player = *reinterpret_cast<BYTE**>(0x2271490);
auto& g_cameraRotation = *reinterpret_cast<float(*)[9]>(0x11B9C90);

// The interface's memory: every interface document's containers (its textures too) are loaded into
// one pool the game makes at start-up (0xA45600: 0xA44A20 allocates the block, 0x512B30 makes the
// pool "interface" of it), 10.13 MB (12 MB when a start-up flag is set). Armageddon's own documents
// fill most of it (its HUD, and 1 MB for a loading screen); with Guerrilla's map documents in it
// too, a level load failed with "Failed aligned alloc of 1036912 bytes in interface cpu pool (max
// pool size is 10.13MB (10616832) - space left is 27728)" (2026-11-05). So with the map on, the pool
// is made bigger: the three values the start-up code makes it with (the block's size, either one,
// and the pool's).
constexpr DWORD kInterfacePoolSizes[] = {0xA47355, 0xA4736A, 0xA4739F};  // mov [ebp-238h],imm / mov ...,imm / push imm
constexpr DWORD kInterfacePoolGame[] = {0xA20000, 0xC00000, 0xA20000};
constexpr DWORD kInterfacePoolOurs = 48 * 1024 * 1024;  // with every zone's picture loaded (about 13 MB of it)

void EnlargeInterfacePool() {
    for (int i = 0; i < 3; ++i)
        if (rfa::CodeValue(kInterfacePoolSizes[i]) != kInterfacePoolGame[i]) {
            rfa::Log("The interface's memory: the game's code at 0x%08lX isn't as expected (0x%08lX); it keeps its 10 MB, "
                     "too little for Guerrilla's map pictures", kInterfacePoolSizes[i], rfa::CodeValue(kInterfacePoolSizes[i]));
            return;
        }
    for (DWORD at : kInterfacePoolSizes)
        if (!rfa::PatchCode(reinterpret_cast<void*>(at), &kInterfacePoolOurs, sizeof(kInterfacePoolOurs))) {
            rfa::Log("The interface's memory: could not change the game's code at 0x%08lX", at);
            return;
        }
    rfa::Log("The interface's memory: %lu MB (the game's 10 MB is too little for Guerrilla's map pictures)",
             kInterfacePoolOurs / (1024 * 1024));
}

// ---- The interface's document limits
//
// The game holds the interface project's documents in a fixed array of 128 records (0x133A5F8:
// pointer, capacity, count, then the records, 0x3B8 bytes each, made by 0x56A7C0 from a static
// initializer), and its reader takes at most 128 (0x566B60: "Too many VINT docs found! Need to
// increase VINT_MAX_DOCUMENTS above %d"). It keeps at most 10 documents open at once: a pool of
// 0x70-byte records (0x1339CD0), made by 0x566800 in a 0x497-byte buffer of its own, through
// 0x566850(mempool, 10). Armageddon lists 115 documents; Guerrilla's HUD brings its own, its map a
// document per zone picture (as Guerrilla keeps each zone's picture in a container of its own), and
// several of those are open while the player moves about. Both limits are raised, in plain C++:
// - the array: the game's constructor runs as it is, then the array is pointed at records of our own
//   (each made by the game's record constructor, 0x56A800) and given their number (0x47CE00); the
//   reader's 128 (three values, each checked first) becomes ours;
// - the pool: its setup (0x566850) is given a buffer of ours, made a memory pool as the game makes
//   its own (0x428240, released after as the game does, 0x427F40), and our count.
// Both are made before the game's own start-up code runs (the mods load first).
constexpr int kMaxDocuments = 256;
constexpr int kMaxOpenDocuments = 64;  // 40 filled up with the world map's zone pictures too (2026-11-07)
constexpr DWORD kDocumentLimitValues[] = {0x566BC9, 0x566BD0, 0x566BEB};  // cmp [ebp-54h],imm / push imm / mov [ebp-54h],imm
constexpr DWORD kGameMaxDocuments = 0x80;
constexpr int kDocumentRecordSize = 0x3B8;
constexpr int kOpenRecordSize = 0x70;

using ThisFn = void*(__fastcall*)(void* self, void* unused);
using ArraySetFn = void(__fastcall*)(void* self, void* unused, void* records, int capacity, int count);
using PoolInitFn = void(__fastcall*)(void* self, void* unused, void* mempool, unsigned count);
using MempoolMakeFn = void*(__fastcall*)(void* self, void* unused, void* buffer, unsigned size, const char* name, int, int);
auto const DocumentRecordMake = reinterpret_cast<ThisFn>(0x56A800);
auto const ArraySet = reinterpret_cast<ArraySetFn>(0x47CE00);
auto const MempoolMake = reinterpret_cast<MempoolMakeFn>(0x428240);
auto const MempoolRelease = reinterpret_cast<ThisFn>(0x427F40);
const char* const kOpenPoolName = reinterpret_cast<const char*>(0xF7101C);  // the game's own name for it
ThisFn g_documentArrayMake;
PoolInitFn g_openPoolInit;
constexpr size_t kMempoolObjectSize = 0x58;  // the game's local one in 0x566800

void* __fastcall DocumentArrayMake(void* array, void* unused) {
    g_documentArrayMake(array, unused);
    BYTE* records = static_cast<BYTE*>(VirtualAlloc(nullptr, kMaxDocuments * kDocumentRecordSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!records) {
        rfa::Log("Interface documents: no memory for %d records; the game keeps its 128", kMaxDocuments);
        return array;
    }
    for (int i = 0; i < kMaxDocuments; ++i) DocumentRecordMake(records + i * kDocumentRecordSize, nullptr);
    ArraySet(array, nullptr, records, kMaxDocuments, 0);
    rfa::Log("Interface documents: room for %d in the project (the game's 128)", kMaxDocuments);
    return array;
}

void __fastcall OpenPoolInit(void* pool, void* unused, void* gameMempool, unsigned count) {
    const size_t size = kMaxOpenDocuments * (4 + kOpenRecordSize + 1) + 256;  // links, records, flags, alignment
    void* buffer = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buffer) {
        g_openPoolInit(pool, unused, gameMempool, count);
        rfa::Log("Interface documents: no memory for %d open ones; the game keeps its %u", kMaxOpenDocuments, count);
        return;
    }
    alignas(16) BYTE mempool[kMempoolObjectSize] = {};
    MempoolMake(mempool, nullptr, buffer, static_cast<unsigned>(size), kOpenPoolName, 1, 0);
    g_openPoolInit(pool, unused, mempool, kMaxOpenDocuments);
    MempoolRelease(mempool, nullptr);
    rfa::Log("Interface documents: %d open at once (the game's %u)", kMaxOpenDocuments, count);
}

// The interface keeps the documents it is loading, unloading and drawing in eight lists of handles
// (0x133A1A0, 0x2C bytes apart: data pointer, capacity, count, then room for 16 handles), each made
// by 0x566960 with its own 16 and no way to grow (the grow step, 0x7476F0, always refuses; an add
// that doesn't fit is dropped). One of them, 0x133A224, takes every document streamed in (load mode
// 0: the menus' and ours) until the interface works through it (0x563770); a document that doesn't
// get in reports itself not loading (0x565DA0) and never comes up. Entering Guerrilla's world puts
// 9 there at once (Guerrilla's HUD and the zone pictures near the player), and each HUD piece to
// come adds one; so, before they fill it, each list is given room of ours when it is made
// (0x47CE00 points it at the room, as the game's own constructor does at its 16).
constexpr DWORD kDocumentLists = 0x133A1A0;
constexpr int kDocumentListCount = 8;
constexpr DWORD kDocumentListStride = 0x2C;
constexpr DWORD kStreamingQueue = 0x133A224;  // the one streamed documents wait in
constexpr int kDocumentListRoom = 256;
constexpr int kGameDocumentListRoom = 16;
unsigned short g_documentListRoom[kDocumentListCount][kDocumentListRoom];
ThisFn g_documentListMake;

void* __fastcall DocumentListMake(void* list, void* unused) {
    void* made = g_documentListMake(list, unused);
    const DWORD at = reinterpret_cast<DWORD>(list);
    if (at >= kDocumentLists && at < kDocumentLists + kDocumentListCount * kDocumentListStride &&
        (at - kDocumentLists) % kDocumentListStride == 0) {
        const int index = static_cast<int>((at - kDocumentLists) / kDocumentListStride);
        ArraySet(list, nullptr, g_documentListRoom[index], kDocumentListRoom, 0);
        RFA_LOG_DETAIL_FIRST(kDocumentListCount, "Interface document list 0x%08lX: room for %d (the game's %d)", at,
                      kDocumentListRoom, kGameDocumentListRoom);
    }
    return made;
}

// How many documents wait in the streaming queue (for the log: the most there have been).
int StreamingQueueCount() { return *reinterpret_cast<const int*>(kStreamingQueue + 8); }

// The interface loads each document, and each document inside one, into one of 45 slots (0x28C3F48,
// 0x5B0 bytes each): a static array the start-up code constructs (0xF5AAD0) and destroys (0xF66610),
// which 0xC529B0 links into its free list; 0xC524B0 finds a document's slot by its handle, 0xC527C0
// a slot's number. With none free, a document asked for waits for one, without a word. In
// Guerrilla's world, Armageddon's HUD (with its 18 documents inside), Guerrilla's HUD and the zone
// pictures near the player held so many that the pause menu's pages (a dozen documents with theirs)
// never came up and couldn't be left, and a level load then waited forever (2026-11-05: the Video
// page's request still waiting in a dump of the stuck game). So the array is moved to one of ours:
// its 14 references in the game's code (each to the first slot, +0 or +4) and its count, a one-byte
// value in each of the five places that use it and in the debug display's two "%u/%u" (so the
// count stays below 128). Each is checked before anything is changed, before the start-up code
// constructs the slots.
constexpr DWORD kDocumentSlots = 0x28C3F48;
constexpr DWORD kDocumentSlotSize = 0x5B0;
constexpr BYTE kGameDocumentSlots = 45;
constexpr BYTE kDocumentSlotCount = 120;
constexpr DWORD kDocumentSlotReferences[] = {0xC5251C, 0xC52897, 0xC52A18, 0xC52A6F, 0xC52A7E, 0xC52A8C, 0xC52A9B,
                                             0xC52AAA, 0xC52ACA, 0xC52ADF, 0xC52AEE, 0xC52B07, 0xF5AAE0, 0xF66620};
constexpr DWORD kDocumentSlotCounts[] = {0xC52509, 0xC52A0A, 0xC52A54, 0xF5AAD9, 0xF66619, 0x5655F2, 0x565635};
alignas(16) BYTE g_documentSlots[kDocumentSlotCount * kDocumentSlotSize];

void MoveDocumentSlots() {
    for (DWORD at : kDocumentSlotCounts)
        if (*reinterpret_cast<const BYTE*>(at) != kGameDocumentSlots) {
            rfa::Log("Interface document slots: the game's count at 0x%08lX isn't %u; it keeps its %u", at,
                     kGameDocumentSlots, kGameDocumentSlots);
            return;
        }
    for (DWORD at : kDocumentSlotReferences) {
        const DWORD value = rfa::CodeValue(at);
        if (value < kDocumentSlots || value >= kDocumentSlots + kDocumentSlotSize) {
            rfa::Log("Interface document slots: the game's code at 0x%08lX isn't as expected (0x%08lX); it keeps its %u",
                     at, value, kGameDocumentSlots);
            return;
        }
    }
    const DWORD ours = reinterpret_cast<DWORD>(g_documentSlots);
    for (DWORD at : kDocumentSlotReferences) {
        const DWORD value = ours + (rfa::CodeValue(at) - kDocumentSlots);
        if (!rfa::PatchCode(reinterpret_cast<void*>(at), &value, sizeof(value))) {
            rfa::Log("Interface document slots: could not change the game's code at 0x%08lX", at);
            return;
        }
    }
    for (DWORD at : kDocumentSlotCounts)
        if (!rfa::PatchCode(reinterpret_cast<void*>(at), &kDocumentSlotCount, sizeof(kDocumentSlotCount))) {
            rfa::Log("Interface document slots: could not change the game's count at 0x%08lX", at);
            return;
        }
    rfa::Log("Interface document slots: %u (the game's %u), moved to 0x%08lX", kDocumentSlotCount, kGameDocumentSlots, ours);
}

// The project's reader (vint_project.cpp, 0xC50360) keeps the documents it reads from rfg.vint_proj
// in a table of its own: 128 records of 0xC8 bytes at 0x28BC7C8 (two numbers, then the document's
// file name at +8, its name at +0x48 and +0x88), their count at 0x28C3B08, and stops the game at
// the 129th ("Too many documents defined in project, increase VINT_MAX_DOCUMENTS", line 304; the
// user's first test with Guerrilla's 220, 2026-11-05). Every use of the table is in 0xC50360 to
// 0xC507E5: these values hold the address of a field of its first record.
constexpr DWORD kProjectTable = 0x28BC7C8;
constexpr DWORD kProjectRecordSize = 0xC8;
constexpr DWORD kProjectTableReferences[] = {0xC5047E, 0xC5049F, 0xC504C0, 0xC504DB, 0xC50504,
                                             0xC50524, 0xC5074E, 0xC5077E, 0xC507CF, 0xC507E2};
constexpr DWORD kProjectTableLimits[] = {0xC503B4};  // cmp [count],80h
BYTE g_projectTable[kMaxDocuments * kProjectRecordSize];

// The names in the documents' asset lists (each document's containers and the files in them) go
// into one string pool (0x133A408), 10,000 bytes, allocated and set up by 0x566A70 (the size pushed
// twice: for the allocation from the game's memory pool 0x26AEE78, and for the string pool). With
// Guerrilla's zone pictures' documents it overflowed at start-up ("Length of string pool exceeded",
// util\string_pool.cpp line 122; 2026-11-05): about 120 bytes of names each.
constexpr DWORD kAssetNameSizes[] = {0x566A96, 0x566ADC};  // push 2710h, push 2710h
constexpr DWORD kGameAssetNameBytes = 10000;
constexpr DWORD kAssetNameBytes = 64 * 1024;

// Every element of every loaded document (bitmap, text, group, tween, ...) comes from a pool of its
// type, made when the interface registers the type, with a fixed number (pushed just before the
// pool's set-up call) out of one 640 KB memory pool, "vint_perm" (made at start-up, 0xA46AC1 the
// block and 0xA46ADA the pool, 0xA0000 each). The pools hand out what they hold and then nothing:
// an element that doesn't fit is left out of its document. In Guerrilla's world, with Armageddon's
// HUD and Guerrilla's loaded, a dump showed the tweens full (820 of 820) and the bitmaps at 1,212 of
// 1,300; the Options page Video then lost its buttons' backgrounds, its script failed ("bg_x" nil)
// and the game crashed (2026-11-05). So each pool gets three times its number, and vint_perm room
// for them: the six pools are about 550 KB of its 640, three times that 1.7 MB, so 4 MB.
struct ElementPool {
    const char* type;
    DWORD sizeAt;     // the 4-byte value pushed (push imm32), or the byte (push imm8)
    bool oneByte;
    DWORD game;
    DWORD ours;
};
constexpr ElementPool kElementPools[] = {
    {"tween", 0xC6717E, false, 0x334, 3 * 0x334},          // 196 bytes each
    {"document", 0xC6EC32, false, 0xC4, 3 * 0xC4},         // 128
    {"group", 0xC6F122, false, 0x1DC, 3 * 0x1DC},          // 120
    {"text", 0xC6F822, false, 0x118, 3 * 0x118},           // 208
    {"bitmap", 0xC71AD2, false, 0x514, 3 * 0x514},         // 168
    {"animation", 0xC7258E, false, 0x16C, 3 * 0x16C},      // 88
    {"clip", 0xC6DB82, true, 0x10, 0x40},                  // one byte: stays below 128
    {"bitmap_circle", 0xC6D052, true, 0x14, 0x40},
};
constexpr DWORD kVintPermSizes[] = {0xA46AC2, 0xA46ADB};  // push 0A0000h (the block), push 0A0000h (the pool)
constexpr DWORD kGameVintPerm = 0xA0000;
constexpr DWORD kVintPermOurs = 4 * 1024 * 1024;

void RaiseElementPools() {
    for (DWORD at : kVintPermSizes)
        if (rfa::CodeValue(at) != kGameVintPerm) {
            rfa::Log("Interface elements: the game's code at 0x%08lX isn't as expected (0x%08lX); the pools keep their sizes",
                     at, rfa::CodeValue(at));
            return;
        }
    for (const ElementPool& pool : kElementPools) {
        const DWORD value = pool.oneByte ? *reinterpret_cast<const BYTE*>(pool.sizeAt) : rfa::CodeValue(pool.sizeAt);
        if (value != pool.game) {
            rfa::Log("Interface elements: the %s pool's size at 0x%08lX isn't %lu (it's %lu); the pools keep their sizes",
                     pool.type, pool.sizeAt, pool.game, value);
            return;
        }
    }
    for (DWORD at : kVintPermSizes)
        if (!rfa::PatchCode(reinterpret_cast<void*>(at), &kVintPermOurs, sizeof(kVintPermOurs))) {
            rfa::Log("Interface elements: could not change the game's code at 0x%08lX", at);
            return;
        }
    for (const ElementPool& pool : kElementPools) {
        const BYTE small = static_cast<BYTE>(pool.ours);
        const bool changed = pool.oneByte ? rfa::PatchCode(reinterpret_cast<void*>(pool.sizeAt), &small, 1)
                                          : rfa::PatchCode(reinterpret_cast<void*>(pool.sizeAt), &pool.ours, sizeof(pool.ours));
        if (!changed) {
            rfa::Log("Interface elements: could not change the %s pool's size", pool.type);
            return;
        }
    }
    rfa::Log("Interface elements: three times the game's pools (bitmaps %lu, tweens %lu, groups %lu, texts %lu, "
             "animations %lu, documents %lu), in %lu MB (the game's 640 KB)", 3 * 0x514ul, 3 * 0x334ul, 3 * 0x1DCul,
             3 * 0x118ul, 3 * 0x16Cul, 3 * 0xC4ul, kVintPermOurs / (1024 * 1024));
}

void RaiseDocumentLimits() {
    RaiseElementPools();
    MoveDocumentSlots();
    if (!rfa::MoveTable("Interface project documents", kProjectTable, kProjectRecordSize, kGameMaxDocuments,
                        kProjectTableReferences, kProjectTableLimits, g_projectTable, kMaxDocuments))
        return;  // MoveTable says why; the game keeps all its 128s
    bool namesAsExpected = true;
    for (DWORD at : kAssetNameSizes) namesAsExpected = namesAsExpected && rfa::CodeValue(at) == kGameAssetNameBytes;
    if (namesAsExpected) {
        for (DWORD at : kAssetNameSizes) rfa::PatchCode(reinterpret_cast<void*>(at), &kAssetNameBytes, sizeof(kAssetNameBytes));
        rfa::Log("Interface documents: %lu bytes for their asset lists' names (the game's %lu)", kAssetNameBytes, kGameAssetNameBytes);
    } else {
        rfa::Log("Interface documents: the game's code at 0x%08lX isn't as expected; their names keep the game's %lu bytes",
                 kAssetNameSizes[0], kGameAssetNameBytes);
    }
    for (DWORD at : kDocumentLimitValues)
        if (rfa::CodeValue(at) != kGameMaxDocuments) {
            rfa::Log("Interface documents: the game's code at 0x%08lX isn't as expected (0x%08lX); it keeps its 128", at, rfa::CodeValue(at));
            return;
        }
    g_documentArrayMake = reinterpret_cast<ThisFn>(rfa::HookFunction(0x56A7C0, 7, reinterpret_cast<void*>(&DocumentArrayMake)));
    if (!g_documentArrayMake) {
        rfa::Log("Interface documents: could not hook the game's document array; it keeps its 128");
        return;
    }
    const DWORD ours = kMaxDocuments;
    for (DWORD at : kDocumentLimitValues) rfa::PatchCode(reinterpret_cast<void*>(at), &ours, sizeof(ours));
    g_openPoolInit = reinterpret_cast<PoolInitFn>(rfa::HookFunction(0x566850, 7, reinterpret_cast<void*>(&OpenPoolInit)));
    if (!g_openPoolInit) rfa::Log("Interface documents: could not hook the open documents' pool; at most 10 open at once");
    // push ebp / mov ebp,esp / push ecx / mov [ebp-4],ecx
    g_documentListMake = reinterpret_cast<ThisFn>(rfa::HookFunction(0x566960, 7, reinterpret_cast<void*>(&DocumentListMake)));
    if (!g_documentListMake) rfa::Log("Interface documents: could not hook the document lists; each keeps its 16");
}

// Whether one of the game's own interface screens (its pause menu, ...) is up: the interface's per-frame update
// (0x58B810) sets this byte while one is, and the game frees the mouse for it (0x4E4070 reads it).
bool GameMenuUp() { return *reinterpret_cast<const BYTE*>(0x135CE04) != 0; }

// The loader's answer to "are its menus (or the developer console) up?" (RFALoader_MenusWant(0)).
bool MenusShown() {
    using WantFn = int(__cdecl*)(int);
    static WantFn want = [] {
        HMODULE loader = GetModuleHandleW(L"rfa_loader.dll");
        return loader ? reinterpret_cast<WantFn>(GetProcAddress(loader, "RFALoader_MenusWant")) : nullptr;
    }();
    return want && want(0) != 0;
}

// ---- Guerrilla's fonts by name
//
// An interface text names its font, and the interface finds the font by that name among the game's font names
// (0xD065D0, at most 7: 0xD06540(name, font) adds one, 0xD064D0 clears them); a name not among them gets font 0
// (0xD065B0), the console's ug-debug, thin and small. Armageddon's start-up of its fonts (0x5621B0: at the
// interface's start, 0x5635A0, and on a change of language, 0x5B0538) unloads every font, loads ug-debug,
// font_body and font_header (0xD061C0(name, the interface_font pool 0x26AF470, -1)) and names them "debug",
// "font_body" and "font_header". Guerrilla's documents also ask for font_numbers (the reticle's ammo, the
// shop's prices, the map's numbers) and its messages for font_latin_numbers. Guerrilla's own start-up
// (Re-Mars-tered's ui_initialize_fonts, 0x89B4D0) loads font_numbers, and names font_latin_numbers its
// font_body and font_header_latin_numbers its font_header. So after Armageddon's start-up of its fonts, the same
// here: font_numbers (Armageddon ships it, Guerrilla's file byte for byte, in interface.vpp_pc, and never loads
// it) loaded into the same pool and named, and the two latin names given Armageddon's font_body and font_header
// as they stand (0x110AB00, 0x110AAFC, per language). That makes 5 fonts of the 9 the game takes (0xA456FE) and
// 7 names of its 7. The pool (1.125 MB, 0xA4744F) is checked for room first (a mempool: its size at +0x44, what
// is used at +0x4C); without room the numbers stay in ug-debug and the log says so.
using FontLoadFn = int(__cdecl*)(const char* name, void* pool, int);
using FontNameFn = int(__cdecl*)(const char* name, int font);
using FontLookupFn = int(__cdecl*)(const char* name);
using FontStartFn = void(__cdecl*)();
auto const FontLoad = reinterpret_cast<FontLoadFn>(0xD061C0);      // the font's number, -1 if it didn't load
auto const FontName = reinterpret_cast<FontNameFn>(0xD06540);      // the name's place, -1 if the 7 are taken
auto const FontLookup = reinterpret_cast<FontLookupFn>(0xD065D0);  // the name's place, -1 if it isn't named
BYTE* const kInterfaceFontPool = reinterpret_cast<BYTE*>(0x26AF470);
auto& g_gameBodyFont = *reinterpret_cast<const int*>(0x110AB00);
auto& g_gameHeaderFont = *reinterpret_cast<const int*>(0x110AAFC);
auto& g_fontNameCount = *reinterpret_cast<const unsigned*>(0x2DD7E90);
constexpr DWORD kFontStart = 0x5621B0;
constexpr size_t kFontStartPrologue = 6;      // push ebp / mov ebp,esp / sub esp,1Ch
constexpr unsigned kNumbersFontRoom = 0x10000;  // font_numbers: its .vf3_pc 5.4 KB, its texture 43 KB, and alignment
FontStartFn g_fontStart;

unsigned FontPoolSize() { return *reinterpret_cast<const unsigned*>(kInterfaceFontPool + 0x44); }
unsigned FontPoolUsed() { return *reinterpret_cast<const unsigned*>(kInterfaceFontPool + 0x4C); }

void NameGuerrillaFonts() {
    if (FontLookup("font_numbers") < 0) {
        const unsigned before = FontPoolUsed();
        if (FontPoolSize() < before || FontPoolSize() - before < kNumbersFontRoom) {
            rfa::Log("Guerrilla's fonts: no room for font_numbers in the font pool (%u of %u bytes used); Guerrilla's "
                     "numbers stay in the console's font", before, FontPoolSize());
        } else {
            const int numbers = FontLoad("font_numbers", kInterfaceFontPool, -1);
            if (numbers < 0)
                rfa::Log("Guerrilla's fonts: font_numbers did not load; Guerrilla's numbers stay in the console's font");
            else if (FontName("font_numbers", numbers) < 0)
                rfa::Log("Guerrilla's fonts: the game's font names are full; font_numbers loaded but not named");
            else
                rfa::Log("Guerrilla's fonts: font_numbers loaded (font %d; the font pool %u to %u of %u bytes)", numbers,
                         before, FontPoolUsed(), FontPoolSize());
        }
    }
    if (FontLookup("font_latin_numbers") < 0 && FontName("font_latin_numbers", g_gameBodyFont) < 0)
        rfa::Log("Guerrilla's fonts: the game's font names are full; font_latin_numbers not named");
    if (FontLookup("font_header_latin_numbers") < 0 && FontName("font_header_latin_numbers", g_gameHeaderFont) < 0)
        rfa::Log("Guerrilla's fonts: the game's font names are full; font_header_latin_numbers not named");
}

void __cdecl FontStart() {
    g_fontStart();
    NameGuerrillaFonts();
}

void HookFonts() {
    g_fontStart = reinterpret_cast<FontStartFn>(rfa::HookFunction(kFontStart, kFontStartPrologue, reinterpret_cast<void*>(&FontStart)));
    if (!g_fontStart) {
        rfa::Log("Guerrilla's fonts: could not hook the game's start-up of its fonts; Guerrilla's numbers stay in the console's font");
        return;
    }
    if (g_fontNameCount > 0) NameGuerrillaFonts();  // the game's fonts were started before this mod
}

// ---- The mod

struct Document {
    std::string name;
    bool drawn = true;       // put on the draw list (the zone pictures' documents only bring textures)
    bool whileOpen = false;  // drawn only while the world map is open
    bool whileShop = false;  // loaded and drawn only while a safehouse's upgrade shop is open
    bool whileCabinet = false;  // loaded and drawn only while Guerrilla's weapon cabinet is open
    bool whileLiberation = false;  // loaded and drawn only while the district-liberated screen is open
    bool whileActivity = false;    // loaded and drawn only while an activity's briefing or results screen is open
    bool whileActivityComplete = false;  // loaded and drawn only while Guerrilla's activity-complete screen is open
    bool whileHandbook = false;  // loaded and drawn only while Guerrilla's handbook is open
    bool handbookPicture = false;  // one of the handbook's entries' pictures: loaded while its entry is shown
    bool zoneSet = false;    // one of the four documents of the zones' pictures (ZoneSetDocument)
    bool onTop = false;      // drawn over the others, our screens too (the tool tips' help box: KeepOnTop)
    bool loaded = false;     // the mod asked for it
    bool seen = false;       // and the game has had it open since
    bool missing = false;    // not in the game's interface project, or the game couldn't load it
    bool listed = false;     // in the draw list (only while the HUD runs)
    int closedByGame = 0;    // times the game closed it after loading it (it does when its memory runs out)
    ULONGLONG retryAt = 0;   // a zone picture the game refused (no room for another open document): when to ask again
    ULONGLONG readyAt = 0;   // when the game first had it ready (see ZoneReady), 0 while not
};
constexpr ULONGLONG kRetryRefusedMs = 2000;

// The interface's draw pass, bool(bool), cdecl: draws every document in its draw lists, every frame,
// whatever is on screen (loading screens and menus too).
constexpr DWORD kDrawPass = 0x564FF0;
constexpr size_t kDrawPassPrologue = 9;  // push ebp / mov ebp,esp / sub esp,0E8h
using DrawPassFn = bool(__cdecl*)(bool);
DrawPassFn g_drawPass;

// Our documents are drawn only while the HUD itself is updated (it is every frame in play): once
// it hasn't been for this long (loading screens, the main menu, ...), they come off the draw list.
constexpr ULONGLONG kHudGoneAfterMs = 250;
constexpr int kClosedByGameTimes = 3;
ULONGLONG g_lastHudUpdate;
ULONGLONG g_hudFrameMs = 0;                    // since the HUD's last update, at most kHudFrameMostMs
constexpr ULONGLONG kHudFrameMostMs = 100;
std::vector<Document> g_documents;
std::vector<std::string> g_worlds;
int g_inOurWorld = -1;  // last frame's answer, to log the switches (-1 before the first frame)

// ---- Guerrilla's map

// Guerrilla's world: 511 m zones, columns and rows 2 to 13, zone (column, row) from x (column − 8) ×
// 511 and z (7 − row) × 511 to a zone further (rows count southwards). Its world map picture covers
// those columns and rows over 1024 pixels, north up; each zone's own picture covers its zone over
// 512 pixels. Our world keeps Guerrilla's coordinates.
constexpr double kZoneMetres = 511.0;
constexpr double kMapPixels = 1024.0;
constexpr int kFirstZone = 2;
constexpr int kZonesAcross = 12;
constexpr double kZonePixels = kMapPixels / kZonesAcross;

// Every zone's own picture comes in one of four documents, by quarter of the world, as
// GuerrillaBuilder.exe makes them (GuerrillaHud.ZoneSetDocument: columns below 8 west, rows below 8
// north): loaded with the HUD and kept until the next level load. Not streamed zone by zone, as
// Guerrilla's were: the interface's memory for documents is a stack, and unloading one out of order
// freed the zones loaded after it while they were on screen; the next one loaded over them, and
// drawing them crashed the render thread (2026-11-07, three freezes).
std::string ZoneSetDocument(int column, int row) {
    return std::string("rfg_ui_map_zones_") + (row < 8 ? "n" : "s") + (column < 8 ? "w" : "e");
}
const char* const kZoneSets[] = {"rfg_ui_map_zones_nw", "rfg_ui_map_zones_ne", "rfg_ui_map_zones_sw", "rfg_ui_map_zones_se"};
int ZoneKey(int column, int row) { return column * 100 + row; }

struct MapView {
    bool active = false;     // in our world, with a player
    double mapX = 0, mapY = 0;  // the player on Guerrilla's world map picture, in its pixels
    double heading = 0;      // which way the camera looks: 0 north, π/2 east
    double speed = 0;        // the player's speed, m/s, smoothed
    int column = 0, row = 0; // the zone the player is in
    std::string district;    // its district's name, as Guerrilla's HUD shows it
};
MapView g_view;
bool g_mapOpen = false;      // the world map is up
std::string g_mapDocument;   // rfg_ui_fullscreen
bool g_mapKeyDown = false;
std::map<int, std::string> g_districts;          // column × 100 + row -> name (districts.txt)
std::map<int, bool> g_tilePictures;              // column × 100 + row -> the zone has its picture
double g_lastX, g_lastZ;
ULONGLONG g_lastMove;

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

std::map<std::string, std::string> g_districtShownNames;  // a district's own name (territories.xtbl) -> as the HUD shows it

// districts.txt (made by GuerrillaBuilder.exe): "CCRR=name" a zone a line, "name:own name=shown name" a district a line
// (for the guerrilla_campaign mod's districts, named by their own names), ; comments.
void ReadDistricts() {
    const std::wstring path = rfa::RootDir() + L"\\mods\\guerrilla_hud\\districts.txt";
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"r") != 0 || !file) {
        rfa::Log("No mods\\guerrilla_hud\\districts.txt (editor\\GuerrillaBuilder.exe build hud makes it): the minimap shows no district names");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "name:", 5) == 0) {
            std::string pair = line + 5;
            while (!pair.empty() && (pair.back() == '\n' || pair.back() == '\r')) pair.pop_back();
            const size_t equals = pair.find('=');
            if (equals != std::string::npos) g_districtShownNames[pair.substr(0, equals)] = pair.substr(equals + 1);
            continue;
        }
        if (line[0] == ';' || strlen(line) < 6 || line[4] != '=') continue;
        const int column = (line[0] - '0') * 10 + (line[1] - '0');
        const int row = (line[2] - '0') * 10 + (line[3] - '0');
        std::string name = line + 5;
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
        g_districts[column * 100 + row] = name;
    }
    fclose(file);
    rfa::Log("Districts of %u zones, for the minimap", static_cast<unsigned>(g_districts.size()));
}

// Guerrilla's map icons and places (map_icons.txt, made by GuerrillaBuilder.exe): its safehouses,
// missions' and activities' starts, the world map's named places, the player's own icon and the world
// map's hints, a line each: "icon|picture|x|z|caption|description|on the world map", "place||x|z|name||1",
// "player|picture|0|0|caption|description|1", "text||0|0|what|text|1".
struct MapIcon {
    std::string kind;       // icon, place, player or text
    std::string picture;    // rfg_ui_map_icon_...
    double mapX = 0, mapY = 0;  // on Guerrilla's world map picture, in its pixels
    std::string caption, description;
    bool onWorldMap = true;
    int pair = -1;          // a M.O.A.B.'s pair number (kind "bomb"), shown once its radio tags are found
    std::string rule;       // which of Guerrilla's minimap rules it follows (GuerrillaMapIcons.MinimapRule; "" for none)
    float worldX = 0, worldZ = 0;  // where it is in the world
    bool done = false;             // its activity done (the campaign's; asked once a second)
};
std::vector<MapIcon> g_mapIcons;
void FollowRadioTags();  // Guerrilla's radio tags (below)
void FollowSafehouses();  // Guerrilla's safehouses' stations (below)
bool ShopIsOpen();
void CloseShop(const char* why);
bool CabinetIsOpen();  // Guerrilla's weapon cabinet (below)
void CloseCabinet(const char* why, bool giveAmmo);
bool LiberationIsOpen();    // Guerrilla's district-liberated screen (below)
bool ActivityScreenOpen();  // an activity's briefing or results screen (below)
bool ActivityCompleteOpen();  // Guerrilla's activity-complete screen (below)
// A screen of ours that pauses the game and hides the HUD: a safehouse's (the shop or the cabinet), or one the campaign
// raises (a district liberated, an activity's briefing or results, Guerrilla's activity-complete screen).
bool StationScreenOpen() {
    return ShopIsOpen() || CabinetIsOpen() || LiberationIsOpen() || ActivityScreenOpen() || ActivityCompleteOpen();
}
bool HandbookIsOpen();  // Guerrilla's handbook (below)
bool HandbookClosing();
void FollowHandbook();
bool ObjectIsDead(BYTE* object);  // (Guerrilla's minimap blips, below)
void FollowToolTipTriggers();     // Guerrilla's tool tips this mod raises (below)
void HandbookDocumentArrived();
void HandbookPictureShown(const std::string& document);
void CloseHandbook(const char* why, bool toPauseMenu);
extern bool g_handbookBackToPauseMenu;
// One of our screens that pause the game: a safehouse's, or the handbook (also while it closes).
bool OurScreenOpen() { return StationScreenOpen() || HandbookIsOpen() || HandbookClosing(); }
void FollowMessages(ULONGLONG hudMs);  // Guerrilla's pickups, use action and saving icon (below)
void FollowCampaignMessages();         // the campaign's minor messages and tool tips (below)
void FollowDistricts();                // Guerrilla's control and morale meters' changes (below)
void OpenLiberation(const std::string& district, const std::string& key, float controlMax, int missions, const char* why);  // (below)
void FollowCampaignScreens();          // the district-liberated screen and the activities' screens (below)
void AddCampaignScreenCommands();
void StepCampaignScreens();
void CloseCampaignScreens(const char* why);
void ForgetDistricts();
void ReadHudMessages();
void ForgetPickups();

// Where a point of Guerrilla's world (x, z metres) is on its world map picture, in its pixels.
void MapPixels(double x, double z, double& mapX, double& mapY) {
    mapX = (8.0 + x / kZoneMetres - kFirstZone) * kZonePixels;  // column 8 starts at x 0, row 8 at z 0
    mapY = (8.0 - z / kZoneMetres - kFirstZone) * kZonePixels;
}

void ReadMapIcons() {
    const std::wstring path = rfa::RootDir() + L"\\mods\\guerrilla_hud\\map_icons.txt";
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"r") != 0 || !file) {
        rfa::Log("No mods\\guerrilla_hud\\map_icons.txt (editor\\GuerrillaBuilder.exe build hud makes it): the maps show no icons");
        return;
    }
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == ';') continue;
        std::vector<std::string> fields(1);
        for (const char* c = line; *c && *c != '\n' && *c != '\r'; ++c) {
            if (*c == '|') fields.emplace_back();
            else fields.back() += *c;
        }
        if (fields.size() < 7 || (fields[0] != "icon" && fields[0] != "place" && fields[0] != "player" && fields[0] != "text" &&
                                  fields[0] != "bomb"))
            continue;
        MapIcon icon;
        icon.kind = fields[0];
        icon.picture = fields[1];
        MapPixels(atof(fields[2].c_str()), atof(fields[3].c_str()), icon.mapX, icon.mapY);
        icon.worldX = static_cast<float>(atof(fields[2].c_str()));
        icon.worldZ = static_cast<float>(atof(fields[3].c_str()));
        icon.caption = fields[4];
        icon.description = fields[5];
        icon.onWorldMap = fields[6] != "0";
        if (fields.size() >= 8 && !fields[7].empty()) icon.pair = atoi(fields[7].c_str());
        if (fields.size() >= 9) icon.rule = fields[8];
        g_mapIcons.push_back(icon);
    }
    fclose(file);
    rfa::Log("%u map icons and places, for the maps", static_cast<unsigned>(g_mapIcons.size()));
}

// Whether a zone has its own picture (in one of the zone pictures' documents), asked once.
bool HasTilePicture(int column, int row) {
    const int key = column * 100 + row;
    auto found = g_tilePictures.find(key);
    if (found != g_tilePictures.end()) return found->second;
    char name[64];
    sprintf_s(name, "rfg_terr01_%02d_%02d_map", column, row);
    const bool has = FindPicture(name) >= 0;
    g_tilePictures[key] = has;
    return has;
}

Document* FindDocument(const std::string& name) {
    for (Document& doc : g_documents)
        if (doc.name == name) return &doc;
    return nullptr;
}

// Whether a zone's picture can be shown: its document loaded and ready (its texture in) for a second.
// The game calls a document ready a moment before its texture can be drawn: a picture shown 26 ms after
// its document opened was drawn from an empty texture record (rfg_terr01_04_11_map_sheet.tga) and
// crashed the render thread (2026-11-07, 10:58: the game froze).
constexpr ULONGLONG kReadySettleMs = 1000;

// Whether a loaded document's pictures can be drawn: the document ready (its textures in) for a second.
bool DocumentSettled(Document& doc) {
    if (!doc.loaded) return false;
    BYTE* open = OpenDocument(doc.name.c_str());
    if (!open || DocumentStatus(*reinterpret_cast<unsigned short*>(open + 0x40)) != kDocumentReady) {
        doc.readyAt = 0;
        return false;
    }
    const ULONGLONG now = GetTickCount64();
    if (doc.readyAt == 0) doc.readyAt = now;
    return now - doc.readyAt >= kReadySettleMs;
}

bool ZoneReady(int column, int row) {
    const std::string set = ZoneSetDocument(column, row);
    for (Document& doc : g_documents)
        if (doc.zoneSet && doc.loaded && doc.name == set) return DocumentSettled(doc);
    return false;
}

// Where the player is on Guerrilla's map, which way the camera looks, and the zone pictures wanted.
void UpdateMapView() {
    BYTE* player = g_player;
    g_view.active = player != nullptr;
    if (!player) return;
    const float* position = reinterpret_cast<const float*>(player + 4);
    const double x = position[0], z = position[2];
    const double column = 8.0 + x / kZoneMetres, row = 8.0 - z / kZoneMetres;  // column 8 starts at x 0, row 8 at z 0
    MapPixels(x, z, g_view.mapX, g_view.mapY);
    const float* forward = &g_cameraRotation[6];
    g_view.heading = std::atan2(static_cast<double>(forward[0]), static_cast<double>(forward[2]));

    // The player's speed, smoothed over about half a second (Guerrilla's minimap zooms out with the
    // vehicle's average speed).
    const ULONGLONG now = GetTickCount64();
    if (g_lastMove != 0 && now > g_lastMove) {
        const double seconds = (now - g_lastMove) / 1000.0;
        const double moved = std::sqrt((x - g_lastX) * (x - g_lastX) + (z - g_lastZ) * (z - g_lastZ));
        const double speed = seconds > 0 && moved < 200.0 ? moved / seconds : 0.0;  // a teleport isn't speed
        const double blend = seconds / (seconds + 0.5);
        g_view.speed += (speed - g_view.speed) * blend;
    }
    g_lastX = x;
    g_lastZ = z;
    g_lastMove = now;

    g_view.column = static_cast<int>(std::floor(column));
    g_view.row = static_cast<int>(std::floor(row));
    auto district = g_districts.find(g_view.column * 100 + g_view.row);
    g_view.district = district != g_districts.end() ? district->second : "";
}

bool HeldDown(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }

bool GameFocused() {
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId();
}

// A key that has just gone down (was: its state last time).
bool Pressed(int key, bool& was) {
    const bool down = HeldDown(key);
    const bool pressed = down && !was;
    was = down;
    return pressed;
}

// ---- Guerrilla's world map: what it shows, its keys, and the game paused while it's open
//
// Guerrilla's zoom (fullscreen_map_get_zoom_info, RM 0x841220) runs from 0.1 to 0.8 and is shown as
// (zoom - 0.1) / 0.7 (fullscreen_map_get_zoom_pct, 0x841340); its map opened zoomed all the way out
// (Zoom_map_out_on_start, fsm_determine_zoom_level 0x840540, which zooms at half a unit a second).
// How much of the world a zoom shows is ours: all of it (the picture's 1024 pixels over the screen's
// height) at the least, kViewLeastMetres at the most, evenly in between on a log scale.
// Guerrilla paused its game while the map was up (a game state of its own). Armageddon's pause is
// counted (0x12F76E0): 0x520280(a, b, c, d) pauses at the first count, 0x520310(a, b) unpauses at the
// last; its console's "pause" command calls them with (1, 1, 0, 0) and (1, 1), and its UI screens
// pause through the same (0x58CC36). So the map holds one count while it's open, as a menu does.
constexpr double kZoomMin = 0.1, kZoomMax = 0.8;
constexpr double kZoomPerSecond = 0.5;    // Guerrilla's
constexpr double kZoomPerNotch = 0.05;    // a step of the mouse wheel
constexpr int kWheelNotch = 120;          // WHEEL_DELTA
constexpr double kViewMostMetres = kZonesAcross * kZoneMetres;
constexpr double kViewLeastMetres = 400.0;
constexpr double kPanScreensPerSecond = 0.6;           // panning moves this much of the screen's height a second
constexpr double kZonePicturesBelowMetres = 2000.0;    // the zones' own pictures under a view this close
constexpr size_t kFullMapTiles = 16;
constexpr double kScreenAspect = 16.0 / 9.0;           // the documents' layout, 1280 x 720
constexpr ULONGLONG kSwallowPauseMs = 500;             // after Esc closes the map, the pause menu it asked for isn't opened
auto& g_wheelTotal = *reinterpret_cast<int*>(0x2939B2C);         // the mouse wheel's running total (0xCDF220)
auto& g_pauseMenuRequest = *reinterpret_cast<BYTE*>(0x135B3F1);  // "open the pause menu" (the HUD opens it at 0x57B774)
using PauseFn = void(__cdecl*)(int, int, int, int);
using UnpauseFn = void(__cdecl*)(int, int);
auto const PauseGame = reinterpret_cast<PauseFn>(0x520280);
auto const UnpauseGame = reinterpret_cast<UnpauseFn>(0x520310);

struct FullMap {
    double centreX = 512, centreY = 512;  // the world map picture's pixel in the middle of the screen
    double zoom = kZoomMin;
    bool paused = false;                  // the map holds one of the game's pause counts
    ULONGLONG lastStep = 0;
    int lastWheel = 0;
    bool escDown = false, centreDown = false;
    ULONGLONG swallowPauseUntil = 0;
    int hudSteps = 0, scriptSteps = 0;   // for the log: which of the two ran while the game was paused
};
FullMap g_full;

double ZoomPercent() { return (g_full.zoom - kZoomMin) / (kZoomMax - kZoomMin); }
// How many metres of the world the screen's height shows.
double ViewMetres() { return kViewMostMetres * std::pow(kViewLeastMetres / kViewMostMetres, ZoomPercent()); }
double ViewPixels() { return ViewMetres() / kZoneMetres * kZonePixels; }

void OpenMap() {
    if (g_mapOpen) return;
    g_mapOpen = true;
    g_full.centreX = g_view.mapX;
    g_full.centreY = g_view.mapY;
    g_full.zoom = kZoomMin;
    g_full.lastStep = 0;
    g_full.lastWheel = g_wheelTotal;
    g_full.escDown = HeldDown(VK_ESCAPE);
    g_full.centreDown = HeldDown(keys::Key(keys::MapCentre));
    if (!g_full.paused) {
        PauseGame(1, 1, 0, 0);
        g_full.paused = true;
    }
    rfa::LogDetail("World map opened; the game paused");
}

void CloseMap(const char* why) {
    if (g_mapOpen)
        rfa::LogDetail("World map closed (%s); while it was open the HUD's update ran %d times, its script %d", why, g_full.hudSteps,
                 g_full.scriptSteps);
    g_full.hudSteps = g_full.scriptSteps = 0;
    g_mapOpen = false;
    if (g_full.paused) {
        UnpauseGame(1, 1);
        g_full.paused = false;
    }
}

// The world map's key, pressed in the game's window, the console and menus closed: opens or closes it.
// Not with Alt or the Windows key held: Alt+Tab and Win+Tab are Windows' own, and the game still has
// the focus when Tab goes down.
void FollowTheMapKey() {
    const int mapKey = keys::Key(keys::Map);  // [GuerrillaKeys] Map (keys.cpp)
    if (!mapKey || OurScreenOpen()) return;
    const bool down = HeldDown(mapKey);
    const bool windowsShortcut = HeldDown(VK_MENU) || HeldDown(VK_LWIN) || HeldDown(VK_RWIN);
    if (down && !g_mapKeyDown && !windowsShortcut && GameFocused()) {
        if (g_mapOpen) CloseMap("its key");
        else if (!MenusShown()) OpenMap();
    }
    g_mapKeyDown = down;
}

// Guerrilla's minimap zoom (cartographer::change_minimap_zoom, RM 0x84E210): while the game runs and the player has
// control, its ZOOM IN / ZOOM OUT MINIMAP keys (CBA_MINIMAP_ZOOM_IN 0x53, _OUT 0x54) take its radius in or out by 50
// metres a second (Minimap_zoom_speed 50), from 40 to 80 (Minimap_min_radius, _max_radius: +0x16EBC); the minimap
// then reaches that far plus half the vehicle's speed (determine_map_information_mini 0x872A24). Here its keys are
// [GuerrillaKeys] MinimapZoomIn / MinimapZoomOut (Guerrilla's PC defaults unknown: its code is encrypted). Its zoom
// sounds (a loop while it moves) aren't brought over yet.
constexpr double kMinimapLeastRadius = 40.0, kMinimapMostRadius = 80.0, kMinimapZoomPerSecond = 50.0;
double g_minimapRadius = kMinimapLeastRadius;
auto const GamePausedNow = reinterpret_cast<bool(__cdecl*)()>(0x520260);  // Armageddon's pause count above 0

void FollowMinimapZoom(double seconds) {
    if (g_mapOpen || OurScreenOpen() || MenusShown() || !GameFocused() || GamePausedNow()) return;
    const int in = keys::Key(keys::MinimapZoomIn), out = keys::Key(keys::MinimapZoomOut);
    const double before = g_minimapRadius;
    if (in && HeldDown(in)) g_minimapRadius -= kMinimapZoomPerSecond * seconds;
    if (out && HeldDown(out)) g_minimapRadius += kMinimapZoomPerSecond * seconds;
    g_minimapRadius = (std::max)(kMinimapLeastRadius, (std::min)(kMinimapMostRadius, g_minimapRadius));
    if (g_minimapRadius != before && (g_minimapRadius == kMinimapLeastRadius || g_minimapRadius == kMinimapMostRadius))
        RFA_LOG_DETAIL_FIRST(20, "Minimap: zoomed %s to %.0f m", g_minimapRadius == kMinimapLeastRadius ? "in" : "out", g_minimapRadius);
}

// rfg_minimap_radius() -> metres: the minimap's reach before the vehicle's speed (Guerrilla's zoom, 40 to 80).
int __cdecl LuaMinimapRadius(rfa::game::lua_State* L) {
    using namespace rfa::game;
    lua_pushnumber(L, g_minimapRadius);
    return 1;
}

// The world map's zone pictures: those of the zones under the view (nearest the middle first, at most
// kFullMapTiles: up to 4 × 3 zones are under a view of 2 km, 16 with the edges), when the view is close
// enough to want them.
std::vector<std::pair<int, int>> FullMapZones() {
    std::vector<std::pair<int, int>> zones;
    if (!g_mapOpen || ViewMetres() >= kZonePicturesBelowMetres) return zones;
    const double halfHeight = ViewPixels() / 2, halfWidth = halfHeight * kScreenAspect;
    std::vector<std::pair<double, std::pair<int, int>>> found;
    for (int c = kFirstZone; c < kFirstZone + kZonesAcross; ++c)
        for (int r = kFirstZone; r < kFirstZone + kZonesAcross; ++r) {
            const double left = (c - kFirstZone) * kZonePixels, top = (r - kFirstZone) * kZonePixels;
            if (left > g_full.centreX + halfWidth || left + kZonePixels < g_full.centreX - halfWidth ||
                top > g_full.centreY + halfHeight || top + kZonePixels < g_full.centreY - halfHeight || !HasTilePicture(c, r))
                continue;
            const double dx = left + kZonePixels / 2 - g_full.centreX, dy = top + kZonePixels / 2 - g_full.centreY;
            found.push_back({dx * dx + dy * dy, {c, r}});
        }
    std::sort(found.begin(), found.end());
    for (size_t i = 0; i < found.size() && i < kFullMapTiles; ++i) zones.push_back(found[i].second);
    return zones;
}

// Each frame while the map is open, from the HUD's update and from the map's own script (whichever
// the game runs while paused; a step moves by the time since the last one): its keys (Guerrilla's
// PAN, ZOOM, CENTER and BACK, on the keyboard and the mouse wheel) and the zone pictures it wants.
void StepFullMap(bool fromScript) {
    const ULONGLONG now = GetTickCount64();
    if (g_mapOpen) ++(fromScript ? g_full.scriptSteps : g_full.hudSteps);
    if (!g_mapOpen) {
        g_full.lastStep = 0;
        return;
    }
    double seconds = g_full.lastStep ? (now - g_full.lastStep) / 1000.0 : 0.0;
    if (seconds > 0.1) seconds = 0.1;  // a stall isn't a long press
    g_full.lastStep = now;
    const int notches = (g_wheelTotal - g_full.lastWheel) / kWheelNotch;
    g_full.lastWheel += notches * kWheelNotch;
    FollowTheMapKey();
    if (!g_mapOpen) return;
    const bool focused = GameFocused();
    if (Pressed(VK_ESCAPE, g_full.escDown) && focused) {
        g_full.swallowPauseUntil = now + kSwallowPauseMs;
        CloseMap("Esc");
        return;
    }
    if (focused) {
        const double right = (HeldDown(keys::Key(keys::Right)) || HeldDown(VK_RIGHT) ? 1.0 : 0.0) -
                             (HeldDown(keys::Key(keys::Left)) || HeldDown(VK_LEFT) ? 1.0 : 0.0);
        const double down = (HeldDown(keys::Key(keys::Down)) || HeldDown(VK_DOWN) ? 1.0 : 0.0) -
                            (HeldDown(keys::Key(keys::Up)) || HeldDown(VK_UP) ? 1.0 : 0.0);
        const double pan = kPanScreensPerSecond * ViewPixels() * seconds;
        g_full.centreX += right * pan;
        g_full.centreY += down * pan;
        const double zoomIn = (HeldDown(VK_PRIOR) || HeldDown(keys::Key(keys::MapZoomIn)) || HeldDown(VK_ADD) || HeldDown(VK_OEM_PLUS) ? 1.0 : 0.0) -
                              (HeldDown(VK_NEXT) || HeldDown(keys::Key(keys::MapZoomOut)) || HeldDown(VK_SUBTRACT) || HeldDown(VK_OEM_MINUS) ? 1.0 : 0.0);
        g_full.zoom += zoomIn * kZoomPerSecond * seconds + notches * kZoomPerNotch;
        if (Pressed(keys::Key(keys::MapCentre), g_full.centreDown)) {
            g_full.centreX = g_view.mapX;
            g_full.centreY = g_view.mapY;
        }
    }
    g_full.zoom = g_full.zoom < kZoomMin ? kZoomMin : g_full.zoom > kZoomMax ? kZoomMax : g_full.zoom;
    g_full.centreX = g_full.centreX < 0 ? 0 : g_full.centreX > kMapPixels ? kMapPixels : g_full.centreX;
    g_full.centreY = g_full.centreY < 0 ? 0 : g_full.centreY > kMapPixels ? kMapPixels : g_full.centreY;
}

// While the map is open the game is paused already, and the pause menu isn't opened over it: Esc closes
// the map (the game asks for its menu on the same key), and a request for it otherwise (borderless asks
// for it when the window loses the focus) is dropped. With the pause menu up, the game stops the HUD's
// update, which loads the map's zone pictures (2026-11-07: none loaded after an Alt+Tab). Called from
// the HUD's update, which runs before the HUD opens the menu (0x57B774).
void SwallowPauseMenuRequest() {
    if (!g_pauseMenuRequest) return;
    if (g_mapOpen || OurScreenOpen() || (GetTickCount64() < g_full.swallowPauseUntil && HeldDown(VK_ESCAPE))) {
        g_pauseMenuRequest = 0;
        RFA_LOG_DETAIL_FIRST(20, "The pause menu wasn't opened: the world map is open (or Esc just closed it)");
    }
}

// The game's own switch for its pause menu: a count (0x135B3EC) that 0x5828C0(true) raises and 0x5828C0(false)
// lowers (Armageddon's scenes use it; a level's start sets it to 0, 0x58161E). While it's above 0, neither the HUD's
// frame (0x57B730) nor the game's other way in (0x593793) opens the menu. The HUD's frame opens it on the pause key's
// press (its input action 0x74, read at 0x57B765) after the HUD's update, where the keys of the map, the shop and the
// cabinet are read: so the Esc that closed one of them opened the pause menu at once (2026-11-12, the weapon
// cabinet; the request flag above is only the scripts' and borderless's way in). Guerrilla's back key closed only
// its screen. Here the count is held one up while one of ours is open, and after one closes until the keys that
// close them are let go, and one HUD update more.
auto const BlockPauseMenu = reinterpret_cast<void(__cdecl*)(bool block)>(0x5828C0);
constexpr int kPauseMenuBlockAfterUpdates = 2;
bool g_pauseMenuBlocked = false;  // we hold one of the game's count
int g_pauseMenuBlockLeft = 0;     // HUD updates the block lasts after our screens have gone

void HoldPauseMenu(bool hold) {
    if (hold == g_pauseMenuBlocked) return;
    BlockPauseMenu(hold);
    g_pauseMenuBlocked = hold;
}

// Each HUD update, after the screens' keys have been read and before the game's frame reads the pause key.
void FollowPauseMenuBlock() {
    // The handbook closed by its back key goes back to the pause menu (as Guerrilla's went back to its menu): no block.
    if (g_handbookBackToPauseMenu) {
        g_handbookBackToPauseMenu = false;
        g_pauseMenuBlockLeft = 0;
        HoldPauseMenu(false);
        return;
    }
    const bool screenOpen = g_mapOpen || OurScreenOpen() || tool_tips::HoldsGame();
    const bool closeKeyHeld = HeldDown(VK_ESCAPE) || HeldDown(VK_TAB) || HeldDown(VK_BACK) || HeldDown(keys::Key(keys::Map));
    if (screenOpen) g_pauseMenuBlockLeft = kPauseMenuBlockAfterUpdates;
    else if (g_pauseMenuBlockLeft > 0 && !closeKeyHeld) --g_pauseMenuBlockLeft;
    HoldPauseMenu(screenOpen || g_pauseMenuBlockLeft > 0);
}

// The handbook's documents (see "Guerrilla's handbook" below).
std::string HandbookPictureWanted();  // the picked entry's picture's document, "" for none

bool AnyHandbookPictureLoaded() {
    for (const Document& doc : g_documents)
        if (doc.handbookPicture && doc.loaded) return true;
    return false;
}

bool AnyHandbookDocumentLoaded() {
    for (const Document& doc : g_documents)
        if ((doc.whileHandbook || doc.handbookPicture) && doc.loaded) return true;
    return false;
}

// A screen's document, loaded only while it's open (the handbook's, the shop's, the cabinet's; see Wanted).
bool ScreenDocument(const Document& doc) {
    return doc.whileHandbook || doc.handbookPicture || doc.whileShop || doc.whileCabinet || doc.whileLiberation || doc.whileActivity ||
           doc.whileActivityComplete;
}

bool AnyScreenDocumentLoaded() {
    for (const Document& doc : g_documents)
        if (ScreenDocument(doc) && doc.loaded) return true;
    return false;
}

bool HandbookScreenReady() {
    for (const Document& doc : g_documents)
        if (doc.whileHandbook) return doc.loaded && doc.seen;
    return false;
}

// Whether a document should be loaded now (in our world): the HUD's for as long as the level lasts (see the zone
// pictures' documents above); the handbook's only while it's open, as Guerrilla loaded its handbook as its pause
// menu opened it (13 MB of pictures loaded with the HUD ran the interface's memory out, 2026-11-12). The interface's
// memory for documents is a stack (see the zone pictures' documents): the handbook's are loaded last, on top of the
// HUD's, and go in the opposite order: its picture first (one at a time: the next waits until the last is gone),
// then the handbook itself.
//
// The shop's and the cabinet's screens (Guerrilla's were game states of their own, their documents loaded as they
// opened) are loaded only while they're open too: the shop's alone is 22 MB of the interface's memory (its
// blueprints), and every loaded document's script and elements live in the interface's own 1.5 MB heap (Vint and its
// Lua: Armageddon's Dl_mempool, see LogVintHeap), which Options > Controls ran out of in the Guerrilla world
// ("dlmalloc out of memory!", 2026-11-12).
//
// A picture's document goes only once the screen no longer shows its picture (rfg_handbook_picture_shown) and a
// moment more: the renderer may still be drawing a frame with it (a texture freed under a drawn picture crashed the
// render thread, 2026-11-07, with the zone pictures).
bool HandbookPictureKept(const Document& doc);  // (see "Guerrilla's handbook")

bool Wanted(const Document& doc) {
    if (doc.whileShop) return ShopIsOpen();
    if (doc.whileCabinet) return CabinetIsOpen();
    if (doc.whileLiberation) return LiberationIsOpen();
    if (doc.whileActivity) return ActivityScreenOpen();
    if (doc.whileActivityComplete) return ActivityCompleteOpen();
    if (doc.whileHandbook) return HandbookIsOpen() || AnyHandbookPictureLoaded();
    if (doc.handbookPicture) {
        if (doc.loaded && HandbookPictureKept(doc)) return true;
        if (!HandbookIsOpen() || !HandbookScreenReady() || doc.name != HandbookPictureWanted()) return false;
        if (doc.loaded) return true;
        return !AnyHandbookPictureLoaded();
    }
    return true;
}

// Whether a loaded document should be on the draw list now.
// While a shop or the weapon cabinet is open only its screen is drawn (Guerrilla hid its HUD for them).
bool ShouldDraw(const Document& doc, bool hudRuns) {
    const bool shop = ShopIsOpen(), cabinet = CabinetIsOpen(), handbook = HandbookIsOpen();
    const bool liberation = LiberationIsOpen(), activity = ActivityScreenOpen(), activityComplete = ActivityCompleteOpen();
    if ((shop || cabinet || handbook || liberation || activity || activityComplete) && !(shop && doc.whileShop) &&
        !(cabinet && doc.whileCabinet) && !(handbook && doc.whileHandbook) && !(liberation && doc.whileLiberation) &&
        !(activity && doc.whileActivity) && !(activityComplete && doc.whileActivityComplete) && !doc.zoneSet && !doc.handbookPicture &&
        !doc.onTop)
        return false;
    return doc.drawn && hudRuns && (!doc.whileOpen || g_mapOpen) && (!doc.whileShop || shop) && (!doc.whileCabinet || cabinet) &&
           (!doc.whileHandbook || handbook) && (!doc.whileLiberation || liberation) && (!doc.whileActivity || activity) &&
           (!doc.whileActivityComplete || activityComplete);
}

// The draw list draws in the order documents were added to it, and adds a new one last: a document added after one that
// stays on top (a screen opened while a tool tip is up) is drawn over it. So that one is taken off and added again.
void KeepOnTop() {
    for (const Document& doc : g_documents) {
        if (!doc.onTop || !doc.listed) continue;
        if (BYTE* open = OpenDocument(doc.name.c_str())) {
            const unsigned short handle = *reinterpret_cast<unsigned short*>(open + 0x40);
            RemoveFromDrawList(g_drawList, nullptr, &handle);
            AddToDrawList(g_drawList, nullptr, &handle);
        }
    }
}

void Unload(Document& doc, BYTE* open) {
    const unsigned short handle = *reinterpret_cast<unsigned short*>(open + 0x40);
    if (doc.listed) RemoveFromDrawList(g_drawList, nullptr, &handle);
    UnloadDocument(handle);
    RFA_LOG_DETAIL_FIRST(200, "Unloaded %s", doc.name.c_str());
    doc.loaded = doc.seen = doc.listed = false;
    doc.readyAt = 0;
}

// The game's state (0x53FE80, cdecl: the top of its state stack, [0x10F8BE8] into 0x1307898, -1 for
// none); 3 is the level load (registered by 0x55D6D0, see LoadStart).
using GameStateFn = int(__cdecl*)();
auto const GameState = reinterpret_cast<GameStateFn>(0x53FE80);
constexpr int kLoadingState = 3;
constexpr ULONGLONG kAfterLoadStartMs = 1000;  // the load's state may not be on the stack yet in the frame it starts
ULONGLONG g_loadStartedAt = 0;

bool LevelLoading() {
    return GameState() == kLoadingState || (g_loadStartedAt != 0 && GetTickCount64() - g_loadStartedAt < kAfterLoadStartMs);
}

// In our worlds, the documents wanted are loaded (and loaded again if the game closes one); the
// others the mod loaded are unloaded, and all of them in any other world.
void FollowTheWorld() {
    const bool ours = InOurWorld();
    if (static_cast<int>(ours) != g_inOurWorld) {
        rfa::Log(ours ? "World %s: Guerrilla's HUD" : "World %s: Armageddon's own HUD", WorldName());
        g_inOurWorld = ours;
        if (!ours) CloseMap("another world");
    }
    // Nothing is loaded or unloaded during a level load, nor the player read (see LoadStart): the HUD's
    // update can run again in the frame the load starts, and once did (2026-11-07, the reload crash at
    // 04:05:37: it loaded all 20 documents again into the memory the load was freeing).
    if (LevelLoading()) {
        RFA_LOG_DETAIL_FIRST(20, "A level load runs: Guerrilla's HUD documents wait for it to end");
        return;
    }
    if (ours) {
        UpdateMapView();
        if (g_mapOpen) StepFullMap(false);
        else FollowTheMapKey();
        FollowMinimapZoom(g_hudFrameMs / 1000.0);
        FollowRadioTags();
        FollowHandbook();
        tool_tips::Step(g_hudFrameMs / 1000.0, g_player && !ObjectIsDead(g_player));
        FollowToolTipTriggers();
        FollowCampaignMessages();
        FollowDistricts();
        AddCampaignScreenCommands();
        FollowCampaignScreens();
        StepCampaignScreens();
        if (!HandbookIsOpen() && !HandbookClosing() && !LiberationIsOpen() && !ActivityScreenOpen() && !ActivityCompleteOpen())
            FollowSafehouses();
        FollowMessages(g_hudFrameMs);
    } else {
        g_view.active = false;
    }
    for (Document& doc : g_documents) {
        const bool wanted = ours && Wanted(doc);
        if (!wanted && !doc.loaded) continue;  // most zone pictures, most of the time: nothing to ask the game
        BYTE* open = OpenDocument(doc.name.c_str());
        if (open && !doc.seen) {
            if (doc.loaded) RFA_LOG_DETAIL_FIRST(200, "The game has %s open", doc.name.c_str());
            doc.seen = true;
            if (doc.whileHandbook || doc.handbookPicture) HandbookDocumentArrived();
        }
        if (!wanted) {
            if (doc.loaded && open) Unload(doc, open);  // one still loading is unloaded once it is open
            continue;
        }
        if (open || doc.missing || (doc.loaded && !doc.seen) || GetTickCount64() < doc.retryAt) continue;
        // While a screen's documents are loaded (on top of the stack), no other is loaded above them: they couldn't go
        // first then.
        if (!ScreenDocument(doc) && AnyScreenDocumentLoaded()) continue;
        if (doc.loaded) {
            // The game closes a document whose containers don't fit its interface memory (see
            // EnlargeInterfacePool); loading it again and again wouldn't help.
            if (++doc.closedByGame > kClosedByGameTimes) {
                rfa::Log("The game closed %s %d times; leaving it closed (out of interface memory? see the game's "
                         "\"interface cpu pool\" message)", doc.name.c_str(), kClosedByGameTimes);
                doc.loaded = false;
                doc.missing = true;
                continue;
            }
            rfa::Log("The game closed %s; loading it again", doc.name.c_str());
        }
        if (!InProject(doc.name.c_str())) {
            rfa::Log("%s isn't in the game's interface project: build Guerrilla's HUD from your copy "
                     "of Guerrilla first (editor\\GuerrillaBuilder.exe build hud)", doc.name.c_str());
            doc.missing = true;
            continue;
        }
        RFA_LOG_DETAIL_FIRST(200, "Loading %s", doc.name.c_str());  // before the game's call, so a log ending here points at it
        const unsigned short handle = LoadWhilePlaying(doc.name.c_str());
        if (handle == kNoDocument) {
            // A zone's picture is asked for again a little later: the game refuses one when its room for
            // open documents is full (kMaxOpenDocuments), which frees up as other zones' pictures go.
            if (doc.zoneSet) {
                RFA_LOG_FIRST(40, "The game couldn't load %s (no room for another open document?); asking again in %llu s",
                              doc.name.c_str(), kRetryRefusedMs / 1000);
                doc.retryAt = GetTickCount64() + kRetryRefusedMs;
            } else {
                rfa::Log("The game couldn't load %s", doc.name.c_str());
                doc.missing = true;
            }
            continue;
        }
        doc.loaded = true;
        doc.seen = false;
        doc.listed = ShouldDraw(doc, true);
        if (doc.listed) AddToDrawList(g_drawList, nullptr, &handle);
        if (doc.listed && !doc.onTop) KeepOnTop();
        RFA_LOG_DETAIL_FIRST(200, "Loaded %s (handle %u)%s", doc.name.c_str(), handle, doc.listed ? ", and listed it to be drawn" : "");
    }
}

// The interface's own heap: Vint (Armageddon's interface library) and its Lua allocate from Armageddon's dlmalloc pool
// Dl_mempool, made at start-up with 1.5 MB (0xA469DD; Vint's init 0x5B3F90 gives Vint its malloc and free, 0x8FBAD0
// and 0x8FBA90, and its Lua the same through 0xB77240 and 0xD94A90; Lua's allocator 0xD95830). dlmalloc grows into it
// through 0x8FD080, which keeps how far it has grown at [0x2476378] (it never gives any back) of [0x247637C], and
// shows "dlmalloc out of memory!" when a growth doesn't fit. Logged as it grows (64 KB steps), with the world.
constexpr DWORD kVintHeapGrown = 0x2476378, kVintHeapSize = 0x247637C;
constexpr DWORD kVintHeapLogStep = 64 * 1024;       // while playing
constexpr DWORD kVintHeapMenuLogStep = 16 * 1024;   // while one of the game's menus is up (they take it in smaller steps)

// Logs the heap's use when it has grown by step since the last line (step 0: now, saying why).
void LogVintHeap(DWORD step, const char* why) {
    static DWORD logged = 0;
    const DWORD grown = *reinterpret_cast<const DWORD*>(kVintHeapGrown), size = *reinterpret_cast<const DWORD*>(kVintHeapSize);
    if (size == 0 || (step != 0 && logged != 0 && grown < logged + step)) return;
    logged = grown;
    // Nearly full: the game shows "dlmalloc out of memory!" when it can't grow, and an interface then goes missing.
    static bool warned = false;
    if (!warned && grown > size / 10 * 9) {
        warned = true;
        rfa::Log("The interface's heap (Dl_mempool) is nine tenths full: %lu KB of its %lu KB used (world %s; %s)", grown / 1024,
                 size / 1024, WorldName(), why);
    }
    RFA_LOG_DETAIL_FIRST(300, "The interface's heap (Vint and its Lua, Dl_mempool): %lu KB of its %lu KB used at the most so far (world %s; %s)",
                  grown / 1024, size / 1024, WorldName(), why);
}

// Dl_mempool's size: the game makes it 1.5 MB at start-up (0xA45600: 0xA44A20(0x180000), its memory from the
// process heap, then the mempool "dlmalloc" of 0x180000 that dlmalloc grows into). With Guerrilla's HUD documents'
// scripts and elements in it, Armageddon's Options > Controls > On Foot Controls ran it out in the Guerrilla world
// (2026-11-12: "dlmalloc out of memory!", the heap at 1080 KB after the world loaded): 3 MB (the user's choice). The two
// sizes are the values of the start-up's push 0x180000 at 0xA469DD and 0xA469F6; each is checked before it's changed.
constexpr DWORD kVintHeapSizeValues[] = {0xA469DE, 0xA469F7};
constexpr DWORD kVintHeapGame = 0x180000, kVintHeapOurs = 0x300000;

void RaiseVintHeap() {
    for (DWORD at : kVintHeapSizeValues)
        if (rfa::CodeValue(at) != kVintHeapGame) {
            rfa::Log("The interface's heap (Dl_mempool): the game's code at 0x%08lX isn't as expected (0x%08lX, not 0x%08lX); "
                     "it keeps the game's 1.5 MB", at, rfa::CodeValue(at), kVintHeapGame);
            return;
        }
    for (DWORD at : kVintHeapSizeValues)
        if (!rfa::PatchCode(reinterpret_cast<void*>(at), &kVintHeapOurs, sizeof(kVintHeapOurs))) {
            rfa::Log("The interface's heap (Dl_mempool): could not change the game's code at 0x%08lX", at);
            return;
        }
    rfa::Log("The interface's heap (Vint and its Lua, Dl_mempool): %lu KB, was 0x%08lX (%lu KB) at 0x%08lX and 0x%08lX "
             "(the game's 1.5 MB ran out in the Guerrilla world's Options > Controls)", kVintHeapOurs / 1024, kVintHeapGame,
             kVintHeapGame / 1024, kVintHeapSizeValues[0], kVintHeapSizeValues[1]);
}

void __cdecl HudUpdate(void* hud) {
    g_hudUpdate(hud);
    LogVintHeap(kVintHeapLogStep, "playing");
    const ULONGLONG now = GetTickCount64();
    g_hudFrameMs = (std::min)(now - g_lastHudUpdate, kHudFrameMostMs);  // the HUD's time: none while it's stopped
    g_lastHudUpdate = now;
    FollowTheWorld();
    SwallowPauseMenuRequest();
    FollowPauseMenuBlock();
    if (!rfa::DetailLog()) return;  // the rest is for the log, with Log=1
    // For the log: an element pool past the game's own size (each pool: +0x0C in use, +0x10 room).
    struct PoolWatch { const char* type; DWORD pool; DWORD game; DWORD logged; };
    static PoolWatch watched[] = {{"tween", 0x28EEB00, 0x334, 0}, {"document", 0x28F15CC, 0xC4, 0},
                                  {"group", 0x28F1684, 0x1DC, 0}, {"text", 0x28F18D0, 0x118, 0},
                                  {"bitmap", 0x28F19FC, 0x514, 0}, {"animation", 0x28F1B4C, 0x16C, 0}};
    for (PoolWatch& watch : watched) {
        const DWORD inUse = *reinterpret_cast<const DWORD*>(watch.pool + 0x0C);
        if (inUse > watch.game && inUse > watch.logged + 50) {
            watch.logged = inUse;
            RFA_LOG_DETAIL_FIRST(30, "Interface elements: %lu %ss in use (the game had room for %lu, now %lu)", inUse, watch.type,
                          watch.game, *reinterpret_cast<const DWORD*>(watch.pool + 0x10));
        }
    }
    // For the log: the most documents the streaming queue has held (the game's lists held 16).
    static int mostQueued = kGameDocumentListRoom / 2;
    if (const int queued = StreamingQueueCount(); queued > mostQueued) {
        mostQueued = queued;
        RFA_LOG_DETAIL_FIRST(30, "Interface: %d documents waiting in the streaming queue (the game had room for %d)", queued,
                      kGameDocumentListRoom);
    }
}

// Before each draw pass: our loaded documents on the draw list while the HUD runs (the world map
// only while it is open), off it otherwise.
bool __cdecl DrawPass(bool flag) {
    // The interface's heap while the game's own menus are up (the HUD's update doesn't run then): as one opens, and
    // in 16 KB steps while they're up (the screens they open take it: Options > Controls > On Foot Controls ran it out).
    static bool menuWasUp = false;
    const bool menuUp = GameMenuUp();
    if (menuUp && !menuWasUp) LogVintHeap(0, "a menu opened");
    else if (menuUp) LogVintHeap(kVintHeapMenuLogStep, "in a menu");
    menuWasUp = menuUp;
    // While the world map or a safehouse's screen is open the game is paused, which may stop the HUD's update: still
    // ours, but not while one of the game's own screens is up over it (its pause menu, which it opens itself when
    // the window loses the focus: 2026-11-11, the weapon cabinet drawn over it).
    const bool screenUp = (g_mapOpen || StationScreenOpen()) && !GameMenuUp();
    const bool hudRuns = screenUp || GetTickCount64() - g_lastHudUpdate <= kHudGoneAfterMs;
    for (Document& doc : g_documents) {
        const bool draw = ShouldDraw(doc, hudRuns);
        if (!doc.loaded || doc.listed == draw) continue;
        BYTE* open = OpenDocument(doc.name.c_str());
        if (!open) continue;
        const unsigned short handle = *reinterpret_cast<unsigned short*>(open + 0x40);
        if (draw) AddToDrawList(g_drawList, nullptr, &handle);
        else RemoveFromDrawList(g_drawList, nullptr, &handle);
        doc.listed = draw;
        if (draw && !doc.onTop) KeepOnTop();
        RFA_LOG_DETAIL_FIRST(40, draw ? "%s shown" : "%s hidden", doc.name.c_str());
    }
    return g_drawPass(flag);
}

// Every level load starts in the game's loading state (state 3, registered by 0x55D6D0): a reload
// (death, a restart, the console's level), another world, or the main menu. Its start, 0x55D870
// (called through the state's table), prints "LOAD SCREEN: Start time" and closes the screens
// that are up (0x565C80). The load frees the interface memory our documents' containers are in,
// but leaves the documents themselves open. After one reload, the game was told to unload the map
// pictures the player had left behind; it released pictures whose memory was already used for
// other pictures, and crashed (2026-11-06, rfg_ui_map_07_11). So the mod unloads all of its
// documents as each load starts, while their memory is still theirs, as the game does with its own
// screens there. The ones still wanted load again once the HUD runs.
constexpr DWORD kLoadStart = 0x55D870;
constexpr size_t kLoadStartPrologue = 6;  // push ebp / mov ebp,esp / sub esp,8
using LoadStartFn = void(__cdecl*)();
LoadStartFn g_loadStart;

void __cdecl LoadStart() {
    g_loadStartedAt = GetTickCount64();
    CloseMap("a level load");  // and the game unpaused
    CloseShop("a level load");
    CloseCabinet("a level load", false);
    CloseHandbook("a level load", false);
    g_pauseMenuBlockLeft = 0;
    HoldPauseMenu(false);  // before the level's start sets the game's count to 0
    ForgetPickups();  // what the new level gives at its start isn't picked up
    tool_tips::LevelShutdown();  // the tips waiting gone, the game unpaused
    ForgetDistricts();           // the new level's districts noted, not shown as changes
    CloseCampaignScreens("a level load");
    int unloaded = 0, forgotten = 0;
    for (Document& doc : g_documents) {
        doc.closedByGame = 0;
        if (!doc.loaded) continue;
        if (BYTE* open = OpenDocument(doc.name.c_str())) {
            Unload(doc, open);
            ++unloaded;
        } else {
            doc.loaded = doc.seen = doc.listed = false;  // still waiting: the load empties the game's queue
            doc.readyAt = 0;
            ++forgotten;
        }
    }
    if (unloaded || forgotten)
        RFA_LOG_DETAIL_FIRST(40, "A load starts: unloaded %d of Guerrilla's HUD documents (and forgot %d still waiting)", unloaded, forgotten);
    g_loadStart();
}

// ---- Guerrilla's radio tags ("Blast from the past")
//
// Guerrilla hid 36 radio tags about its world (object_bftp_node with tag_node 1), each paired with a
// M.O.A.B. (the node with the same pair_number and tag_node 0). Its blast_from_the_past_process_car_bombs
// (RM 0xAA4C60) finds a tag when the player is on foot within 1.5 m of it (distance squared under 2.25,
// [0x1311020]); bftp_touch_tag (0xA8DB50) then counts it, and every third tag found
// (blast_from_the_past_tags_collected() % 3 == 0) shows that tag's pair's M.O.A.B. on the maps
// (spawn_bomb_from_tag: the bomb node's add_icon with MAP_ELEMENT_CARBOMB) and says so
// (HM_BFTP_SPAWN_BOMB); otherwise it says how many more tags are needed (HM_BFTP_TAG_FOUND for one,
// HM_BFTP_TAGS_FOUND), each a HUD message for 4 s; here Armageddon's own message line (0x581820, which
// shows one for 5 s). Not yet: the bomb itself (the item bftp_bomb, which a car drives up to, to start
// Guerrilla's car bomb activity), the handbook's entry, the tool tips, the sound, the stats.
// The tags found are kept in mods\guerrilla_hud\radio_tags_found.txt, one list for every save.
struct RadioTag {
    bool tag = false;  // a radio tag, else a M.O.A.B.
    int pair = 0;
    float x = 0, y = 0, z = 0;
};
std::vector<RadioTag> g_radioTags;
std::map<std::string, std::string> g_radioTagTexts;  // Guerrilla's messages: tag_found, tags_found, spawn_bomb
std::set<int> g_tagsFound;                           // the pairs whose radio tag was found
std::vector<int> g_tagsFoundOrder;                   // and in the order found
std::set<int> g_bombsShown;                          // the pairs whose M.O.A.B. is on the maps
constexpr float kTagReachSquared = 2.25f;
constexpr DWORD kHumanVehicle = 0x1160;              // the vehicle a human is in (as guerrilla_vehicles reads it)
using AddHudMessageFn = void(__cdecl*)(const char* text);
auto const AddHudMessage = reinterpret_cast<AddHudMessageFn>(0x581820);
// One of Guerrilla's message lines (its major line, or its minor line; see "Guerrilla's messages" below).
void ShowMessage(const std::string& text, bool major, float seconds = -1.0f);

std::wstring RadioTagsFoundPath() { return rfa::RootDir() + L"\\mods\\guerrilla_hud\\radio_tags_found.txt"; }

// Which M.O.A.B.s the tags found show: Guerrilla's rule, every third tag shows its own pair's.
void ShowBombsOfTagsFound() {
    g_bombsShown.clear();
    for (size_t i = 0; i < g_tagsFoundOrder.size(); ++i)
        if ((i + 1) % 3 == 0) g_bombsShown.insert(g_tagsFoundOrder[i]);
}

// radio_tags.txt (made by GuerrillaBuilder.exe) and the list of tags found.
void ReadRadioTags() {
    FILE* file = nullptr;
    if (_wfopen_s(&file, (rfa::RootDir() + L"\\mods\\guerrilla_hud\\radio_tags.txt").c_str(), L"r") != 0 || !file) {
        rfa::Log("No mods\\guerrilla_hud\\radio_tags.txt (editor\\GuerrillaBuilder.exe build hud makes it): no radio tags");
        return;
    }
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == ';') continue;
        std::vector<std::string> fields(1);
        for (const char* c = line; *c && *c != '\n' && *c != '\r'; ++c) {
            if (*c == '|') fields.emplace_back();
            else fields.back() += *c;
        }
        if (fields[0] == "text" && fields.size() >= 3) {
            g_radioTagTexts[fields[1]] = fields[2];
        } else if ((fields[0] == "tag" || fields[0] == "bomb") && fields.size() >= 5) {
            RadioTag node;
            node.tag = fields[0] == "tag";
            node.pair = atoi(fields[1].c_str());
            node.x = static_cast<float>(atof(fields[2].c_str()));
            node.y = static_cast<float>(atof(fields[3].c_str()));
            node.z = static_cast<float>(atof(fields[4].c_str()));
            g_radioTags.push_back(node);
        }
    }
    fclose(file);
    if (_wfopen_s(&file, RadioTagsFoundPath().c_str(), L"r") == 0 && file) {
        while (fgets(line, sizeof(line), file)) {
            if (line[0] == ';' || line[0] < '0' || line[0] > '9') continue;
            const int pair = atoi(line);
            if (g_tagsFound.insert(pair).second) g_tagsFoundOrder.push_back(pair);
        }
        fclose(file);
    }
    ShowBombsOfTagsFound();
    rfa::Log("%u radio tags and M.O.A.B.s; %u tags found so far, %u M.O.A.B.s shown", static_cast<unsigned>(g_radioTags.size()),
             static_cast<unsigned>(g_tagsFound.size()), static_cast<unsigned>(g_bombsShown.size()));
}

void SaveRadioTagsFound() {
    FILE* file = nullptr;
    if (_wfopen_s(&file, RadioTagsFoundPath().c_str(), L"w") != 0 || !file) {
        rfa::Log("Couldn't write mods\\guerrilla_hud\\radio_tags_found.txt");
        return;
    }
    fputs("; Guerrilla's radio tags you have found (their pair numbers, in the order found), kept by guerrilla_hud.\n"
          "; Every third shows its M.O.A.B. on the maps. Delete this file to start the hunt again.\n", file);
    for (int pair : g_tagsFoundOrder) fprintf(file, "%d\n", pair);
    fclose(file);
}

// Guerrilla's message with the number of tags still needed put in for its %ls.
std::string RadioTagMessage(const char* what, int number) {
    std::string text = g_radioTagTexts.count(what) ? g_radioTagTexts[what] : what;
    const size_t at = text.find("%ls");
    if (at != std::string::npos) text.replace(at, 3, std::to_string(number));
    return text;
}

// Each HUD update in our world: a radio tag the player walks up to on foot is found (bftp_touch_tag).
void FollowRadioTags() {
    BYTE* player = g_player;
    if (!player || g_radioTags.empty()) return;
    const DWORD vehicle = *reinterpret_cast<const DWORD*>(player + kHumanVehicle);
    if (vehicle != 0xFFFFFFFF && vehicle != 0) return;
    const float* position = reinterpret_cast<const float*>(player + 4);
    for (const RadioTag& node : g_radioTags) {
        if (!node.tag || g_tagsFound.count(node.pair)) continue;
        const float dx = node.x - position[0], dy = node.y - position[1], dz = node.z - position[2];
        if (dx * dx + dy * dy + dz * dz >= kTagReachSquared) continue;
        g_tagsFound.insert(node.pair);
        g_tagsFoundOrder.push_back(node.pair);
        SaveRadioTagsFound();
        const int left = 3 - static_cast<int>(g_tagsFoundOrder.size() % 3);
        std::string message;
        if (left == 3) {
            g_bombsShown.insert(node.pair);
            message = RadioTagMessage("spawn_bomb", 0);
            tool_tips::Add("MOAB_Located");  // bftp_touch_tag (RM 0xA8DC6F): a M.O.A.B.'s three tags found
        } else {
            tool_tips::Add("Blast_from_the_Past");  // bftp_touch_tag (0xA8DBE7)
            message = left == 1 ? RadioTagMessage("tag_found", 1) : RadioTagMessage("tags_found", left);
        }
        ShowMessage(message, true);  // bftp_touch_tag: a primary message (hud_message_add)
        rfa::Log("Radio tag %d found (%u of 36): %s", node.pair, static_cast<unsigned>(g_tagsFoundOrder.size()), message.c_str());
    }
}

// ---- Guerrilla's safehouses: the upgrade shop
//
// Each of Guerrilla's safehouses has an upgrade shop (its object_upgrade_node, at the safehouse's own spot) and a
// weapon cabinet (an object_action_node "Player Open Weapon Cabinet"), which Armageddon never makes: safehouses.txt
// (made by GuerrillaBuilder.exe) has where they are. Walking up to a shop on foot shows Guerrilla's use prompt
// (rfg_ui_messages), and the game's own use key (the action 0x4E1B70 gives for what the player is doing, held as
// 0x4E4800 reads it, so whatever the player bound) opens Guerrilla's upgrade screen (rfg_ui_upgrades). Guerrilla ran
// it as a game state of its own (hud_ui_upgrades_process, RM 0x87FD20) with the game paused; here it takes one of
// the game's pause counts, as the world map does. The upgrades and buying are guerrilla_gear's (its F7 window's):
// the list through its GuerrillaGear_Upgrade (gear_api.h), a purchase at once (GuerrillaGear_BuyNow, on this thread).
// Keys while it's open: W / S, the arrows or the mouse wheel pick, the use key, Enter or Space buy, Esc or Tab
// close. Not yet: the weapon cabinet (its screen, rfg_ui_safehouse_weapons), Guerrilla's purchase message box.
// Whether guerrilla_gear's upgrades are there (it runs): the shops stay shut without them.
bool GearReady() {
    const bool running = guerrilla_gear::Running();
    if (!running) RFA_LOG_FIRST(1, "Safehouses: guerrilla_gear is off: the shops stay shut");
    return running;
}

struct Station {
    bool shop = false;  // an upgrade shop, else a weapon cabinet
    float x = 0, y = 0, z = 0;
};
std::vector<Station> g_stations;
std::map<std::string, std::string> g_shopTexts;  // the upgrade screen's words: title, price, credits, select, back, locked, use
constexpr float kStationReachSquared = 2.5f * 2.5f;
using UseActionFn = int(__cdecl*)(int player);
using ButtonHeldFn = bool(__cdecl*)(int action, int player);
auto const UseAction = reinterpret_cast<UseActionFn>(0x4E1B70);
auto const ButtonHeld = reinterpret_cast<ButtonHeldFn>(0x4E4800);
auto& g_localPlayer = *reinterpret_cast<int*>(0x11C0580);
auto const GetSalvage = reinterpret_cast<bool(__cdecl*)(int* out)>(0x54C2B0);

struct Shop {
    bool open = false;
    bool paused = false;
    int nearStation = -1;   // the station the player could use now
    bool useDown = true;    // the use key's last state (true at first: a key held then isn't a press)
    std::vector<GuerrillaGearUpgrade> list;
    int highlighted = 0;    // in list
    int top = 0;            // the list's first row shown
    int lastWheel = 0;
    bool upDown = false, wDown = false, downDown = false, sDown = false, buyDown = false, closeDown = false;
    std::string status;     // the last purchase's outcome, shown for a while
    ULONGLONG statusUntil = 0;
};
Shop g_shop;
constexpr int kShopRows = 4;  // Guerrilla's screen has four rows (MAX_BUTTONS)
constexpr ULONGLONG kShopStatusMs = 3000;

// safehouses.txt (made by GuerrillaBuilder.exe): "shop|x|y|z", "cabinet|x|y|z", "text|what|text".
void ReadSafehouses() {
    FILE* file = nullptr;
    if (_wfopen_s(&file, (rfa::RootDir() + L"\\mods\\guerrilla_hud\\safehouses.txt").c_str(), L"r") != 0 || !file) {
        rfa::Log("No mods\\guerrilla_hud\\safehouses.txt (editor\\GuerrillaBuilder.exe build hud makes it): no safehouse shops");
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == ';') continue;
        std::vector<std::string> fields(1);
        for (const char* c = line; *c && *c != '\n' && *c != '\r'; ++c) {
            if (*c == '|') fields.emplace_back();
            else fields.back() += *c;
        }
        if (fields[0] == "text" && fields.size() >= 3) {
            g_shopTexts[fields[1]] = fields[2];
        } else if ((fields[0] == "shop" || fields[0] == "cabinet") && fields.size() >= 4) {
            Station station;
            station.shop = fields[0] == "shop";
            station.x = static_cast<float>(atof(fields[1].c_str()));
            station.y = static_cast<float>(atof(fields[2].c_str()));
            station.z = static_cast<float>(atof(fields[3].c_str()));
            g_stations.push_back(station);
        }
    }
    fclose(file);
    rfa::Log("%u safehouse stations", static_cast<unsigned>(g_stations.size()));
}

void RefreshShopList() {
    g_shop.list.clear();
    const int count = GuerrillaGear_UpgradeCount();
    for (int i = 0; i < count; ++i) {
        GuerrillaGearUpgrade upgrade{};
        upgrade.size = sizeof(upgrade);
        if (GuerrillaGear_Upgrade(i, &upgrade)) g_shop.list.push_back(upgrade);
    }
    if (g_shop.highlighted >= static_cast<int>(g_shop.list.size())) g_shop.highlighted = static_cast<int>(g_shop.list.size()) - 1;
    if (g_shop.highlighted < 0) g_shop.highlighted = 0;
}

void OpenShop() {
    g_shop.open = true;
    g_shop.highlighted = g_shop.top = 0;
    g_shop.lastWheel = g_wheelTotal;
    g_shop.upDown = g_shop.wDown = g_shop.downDown = g_shop.sDown = g_shop.buyDown = g_shop.closeDown = true;  // keys held as it opens aren't presses
    g_shop.status.clear();
    RefreshShopList();
    if (!g_shop.paused) {
        PauseGame(1, 1, 0, 0);
        g_shop.paused = true;
    }
    rfa::LogDetail("Upgrade shop opened (%u upgrades); the game paused", static_cast<unsigned>(g_shop.list.size()));
}

void CloseShop(const char* why) {
    if (g_shop.open) rfa::LogDetail("Upgrade shop closed (%s)", why);
    g_shop.open = false;
    g_shop.useDown = true;
    if (g_shop.paused) {
        UnpauseGame(1, 1);
        g_shop.paused = false;
    }
}

// The shop's keys, each HUD update while it's open.
void StepShop() {
    if (!GameFocused()) return;
    const int notches = (g_wheelTotal - g_shop.lastWheel) / kWheelNotch;
    g_shop.lastWheel += notches * kWheelNotch;
    int move = -notches;
    if (Pressed(VK_UP, g_shop.upDown)) move -= 1;
    if (Pressed(keys::Key(keys::Up), g_shop.wDown)) move -= 1;
    if (Pressed(VK_DOWN, g_shop.downDown)) move += 1;
    if (Pressed(keys::Key(keys::Down), g_shop.sDown)) move += 1;
    const int count = static_cast<int>(g_shop.list.size());
    if (count > 0 && move != 0) {
        g_shop.highlighted = (g_shop.highlighted + move) % count;
        if (g_shop.highlighted < 0) g_shop.highlighted += count;
    }
    if (g_shop.highlighted < g_shop.top) g_shop.top = g_shop.highlighted;
    if (g_shop.highlighted >= g_shop.top + kShopRows) g_shop.top = g_shop.highlighted - kShopRows + 1;
    const bool use = ButtonHeld(UseAction(g_localPlayer), g_localPlayer);
    const bool buy = use || HeldDown(VK_RETURN) || HeldDown(VK_SPACE);
    const bool buyPressed = buy && !g_shop.buyDown;
    g_shop.buyDown = buy;
    if (buyPressed && count > 0 && g_player) {
        const GuerrillaGearUpgrade& chosen = g_shop.list[g_shop.highlighted];
        if (chosen.available) {
            GuerrillaGear_BuyNow(g_player, g_shop.highlighted);
            char status[256] = {};
            GuerrillaGear_Status(status, sizeof(status));
            g_shop.status = status;
            g_shop.statusUntil = GetTickCount64() + kShopStatusMs;
            RefreshShopList();
        }
    }
    const bool close = HeldDown(VK_ESCAPE) || HeldDown(VK_TAB) || HeldDown(VK_BACK);
    if (close && !g_shop.closeDown) {
        g_full.swallowPauseUntil = GetTickCount64() + kSwallowPauseMs;
        CloseShop("its key");
    }
    g_shop.closeDown = close;
}

void OpenCabinet(bool crate);  // Guerrilla's weapon cabinet (below)
void StepCabinet();
void FollowCabinet();

// Each HUD update in our world: the station the player is at (on foot), and the use key there: an upgrade shop
// opens Guerrilla's upgrade screen, a weapon locker its weapon cabinet.
void FollowSafehouses() {
    FollowCabinet();
    if (g_shop.open) {
        StepShop();
        return;
    }
    if (CabinetIsOpen()) {
        StepCabinet();
        return;
    }
    g_shop.nearStation = -1;
    BYTE* player = g_player;
    if (!player || g_stations.empty()) return;
    const DWORD vehicle = *reinterpret_cast<const DWORD*>(player + kHumanVehicle);
    const float* position = reinterpret_cast<const float*>(player + 4);
    if (vehicle == 0xFFFFFFFF || vehicle == 0)
        for (size_t i = 0; i < g_stations.size(); ++i) {
            const Station& station = g_stations[i];
            const float dx = station.x - position[0], dy = station.y - position[1], dz = station.z - position[2];
            if (dx * dx + dy * dy + dz * dz < kStationReachSquared) g_shop.nearStation = static_cast<int>(i);
        }
    // Guerrilla's use prompt at an upgrade station raised its "upgrades" tool tip, 100 ms on (hud_ui_process_use_action_info).
    if (g_shop.nearStation >= 0 && g_stations[g_shop.nearStation].shop) tool_tips::Add("upgrades", 100);
    const bool use = ButtonHeld(UseAction(g_localPlayer), g_localPlayer);
    if (use && !g_shop.useDown && g_shop.nearStation >= 0 && !g_mapOpen && !MenusShown() && GameFocused()) {
        if (!g_stations[g_shop.nearStation].shop) OpenCabinet(false);
        else if (GearReady()) OpenShop();
    }
    g_shop.useDown = use;
}

bool ShopIsOpen() { return g_shop.open; }

// ---- Guerrilla's use prompt at its ammo boxes
//
// Armageddon kept Guerrilla's ammo box action: near an item flagged is_ammo_box (Guerrilla's ammo_box, which the
// world places), the use key is the player's "use ammo box" action (type 9, as Guerrilla's
// GENERAL_ACTION_USE_AMMO_BOX), and it refills the player's ammo. The search that finds it (0x8B29F0; Guerrilla's
// player_process_usable_items, RM 0xADDF30) fills in the action's handle, type and function but no text: Guerrilla's
// actions had none (its prompt is a picture, a hand, beside the key's), and Armageddon, which added a text to each
// action for its own prompt, never used this one. So its prompt showed whatever was last in that memory:
// "[E] {Symbol not found!}". Here that action's text is emptied (Armageddon's prompt stays away for an empty one),
// and the script shows Guerrilla's own prompt (rfg_ui_messages, use_info) while that action is the player's.
struct UseActionRecord {  // the game's (Guerrilla's t_usable_object, with Armageddon's text after it)
    DWORD handle;
    int type;
    int icon;
    float dot;
    float distanceSquared;
    void* function;
    wchar_t text[0x100];
};
constexpr DWORD kUsableItems = 0x8B29F0;
constexpr size_t kUsableItemsPrologue = 9;  // push ebp / mov ebp,esp / sub esp,29Ch
constexpr DWORD kUsableItemsCode = 0x029CEC81;  // that sub's first four bytes, at kUsableItems + 3
constexpr int kActionUseAmmoBox = 9;
constexpr DWORD kPlayerUseAction = 0x1540;  // the player's use action now (a UseActionRecord)
using UsableItemsFn = bool(__cdecl*)(BYTE* player, UseActionRecord* action);
UsableItemsFn g_usableItems = nullptr;
using UseActionFunction = bool(__cdecl*)(BYTE* player, bool use);
UseActionFunction g_ammoCrateUse = nullptr;  // the game's own use of an ammo box (0x8B9C80), which ours calls
bool __cdecl UseAmmoCrate(BYTE* player, bool use);  // Guerrilla's weapon cabinet (below)

bool __cdecl UsableItems(BYTE* player, UseActionRecord* action) {
    const bool found = g_usableItems(player, action);
    if (found && action && action->type == kActionUseAmmoBox) {
        action->text[0] = L'\0';
        // Guerrilla's ammo crate opens its weapon cabinet: the action's function is ours (UseAmmoCrate, below).
        if (action->function && action->function != reinterpret_cast<void*>(&UseAmmoCrate)) {
            g_ammoCrateUse = reinterpret_cast<UseActionFunction>(action->function);
            action->function = reinterpret_cast<void*>(&UseAmmoCrate);
        }
    }
    return found;
}

void HookUsableItems() {
    if (rfa::CodeValue(kUsableItems + 3) != kUsableItemsCode) {
        rfa::Log("Ammo boxes: the game's code at 0x%08lX isn't as expected; its use prompt stays as it is", kUsableItems);
        return;
    }
    g_usableItems = reinterpret_cast<UsableItemsFn>(rfa::HookFunction(kUsableItems, kUsableItemsPrologue, reinterpret_cast<void*>(&UsableItems)));
    if (!g_usableItems) rfa::Log("Ammo boxes: could not hook the game's search for usable items; its use prompt stays as it is");
}

// ---- Guerrilla's weapon cabinet: a safehouse's weapon locker, and every ammo crate
//
// Guerrilla's weapon cabinet (hud_ui_weapon_cabinet_*, its game state GS_INGAME_WEAPON_CABINET, its document
// rfg_ui_safehouse_weapons) opens two ways: at a safehouse's locker (its object_action_node "Player Open Weapon
// Cabinet": player::do_frame_action_node RM 0xAC25AA, show(false)) and at any ammo crate (player_use_ammo_box,
// player::do_frame_open_ammo_box RM 0xACD6A6, show(true), but during a Demolitions Master activity, which refills
// at once). Its rules, as kept here:
//   - The list (Guerrilla's Weapon_list: weapon and whether bought, in the order found; hud_ui_weapon_cabinet_save
//     keeps it in the save): every weapon the player carries as it opens (add_inventory_items RM 0x87CBC0), picks
//     up (pick_up_cb), buys (player_meta_data::purchase_upgrade: a "Weapon" upgrade's weapon) or a mission gives,
//     unlocked; and each weapon a "Weapon" upgrade not bought yet but on sale sells, locked ("PURCHASE AT UPGRADE
//     TABLE"; hud_ui_upgrades_update_list RM 0x87C950). Never the sledgehammers (class melee_sledgehammer) or a
//     non-inventory weapon. The singularity bomb is passed over unless carried.
//   - The screen: seven of the list at a time around the one picked, its name, the ammo it will have, its upgrades
//     bought, its team's logo, "n / count"; the four slots below with their weapons and ammo. The list runs top to
//     bottom, the next weapon below the one picked. Keys here: W / S, the arrows or the mouse wheel pick (S, down
//     and the wheel down the next); 1, 2 and 3 put the weapon picked into that slot (Guerrilla's
//     process_input RM 0x85A300: a weapon already in another of the three swaps places with the slot's; the
//     fourth slot is Mason's hammer and never changes); Esc, Tab or Backspace close it (Guerrilla's back).
//   - Its HUD is hidden while it's open (hud_ui_unload_items on enter): here ours and Armageddon's (the script).
//   - Ammo, as it closes (exit RM 0x85AFA0, weapon_cabinet_get_ammo_box_ammo RM 0x85A170), for each gun carried
//     that uses ammo: at a locker, full (Max_Rounds, or Max_Rounds_Upgrade with its "Weapon" upgrade's value 2
//     bought) and "FULL AMMO" said; at a crate, what it had as the cabinet opened (none for a gun taken from it)
//     plus Ammo_Box_Restock × the district's morale's share (Guerrilla's table, by tenths) × the difficulty's
//     "player ammo crates", up to full. A "never in cabinet" gun keeps its ammo; melee has none.
//   - An ammo crate opens once (player::open_ammo_box RM 0xA846E0): opening it uses it up until its zone loads it
//     again (see UseUpAmmoCrate). A locker opens as often as wanted, full each time.
// The game is paused while it's open (as the shop), and only its screen is drawn. Here the list is kept in
// mods\guerrilla_hud\cabinet_weapons.txt, one for every save (as the upgrades bought are), and the weapons carried
// are looked at every second (pickups, purchases). Armageddon's inventory: the record in a slot (0x7A8290), the
// game's own delete of a record (0x85C6A0, as its swap 0x8BEB00 calls it) and add into a slot (0x85B260). Not here
// yet: Mason's walk to the crate and its opening animation (the crate's lid, Weap_RM), the cabinet's sounds, the
// singularity bomb's warning, the morale system (the district's morale at the start is used).
struct CabinetWeapon {          // cabinet.txt's "weapon" lines (Guerrilla's weapons.xtbl)
    std::string guerrilla;      // Guerrilla's name
    std::string ours;           // the built weapon's (player_weapons.txt: rfg_<name> or <name>), "" if not built
    std::string shown;          // its name, as Guerrilla shows it
    std::string picture;        // its icon (rfg_ui_hud_weapon_icon_...)
    int team = -1;              // whose logo: 0 Guerrilla, 1 EDF, 2 Ultor, 3 Marauder
    bool sledgehammer = false, nonInventory = false, neverInCabinet = false, melee = false, singularity = false;
    int maxRounds = 0, maxRoundsUpgraded = 0, restock = 0;
    const BYTE* entry = nullptr;  // our weapon entry, found at first use
    bool looked = false;
};
struct CabinetUpgrade {         // upgrades.txt's (guerrilla_gear's) upgrades of a weapon: "Weapon" or "Weapon Upgrade"
    std::string name, type, weapon;  // weapon: ours
    std::vector<int> values;
    std::vector<std::string> levelNames, levelTexts;
};
struct CabinetItem {
    int weapon = -1;            // in g_cabinetWeapons
    bool unlocked = false;
};
struct Cabinet {
    bool open = false, paused = false;
    bool crate = false;         // opened at an ammo crate, else at a safehouse's locker
    bool pending = false;       // an ammo crate's use asked for it: opens at the next HUD update
    DWORD crateHandle = 0xFFFFFFFF;  // that crate
    std::vector<CabinetItem> list;
    int current = 0;            // the one picked, in list
    int skipped = -1;           // the singularity bomb's place in list, passed over while not carried
    std::map<int, int> roundsAtOpen;  // by weapon: the rounds carried as it opened
    int version = 0;            // changed with anything the screen shows
    int scroll = 0;             // the last move: +1 on, -1 back (the list's slide)
    int shine = 0;              // the slot just filled, 1-4 (its plate's shine)
    bool useDown = true, closeDown = true, upDown = false, wDown = false, downDown = false, sDown = false;
    bool slotDown[3] = {true, true, true};
    int lastWheel = 0;
    ULONGLONG nextLook = 0;
    ULONGLONG lastStep = 0;     // its keys' last step (see StepCabinet)
};
std::vector<CabinetWeapon> g_cabinetWeapons;
std::vector<CabinetUpgrade> g_cabinetUpgrades;
std::map<std::string, std::string> g_cabinetTexts;  // crate, locker, full_ammo, buy, back, charges
std::map<std::string, int> g_districtMorale;         // a district's shown name -> its morale at the start (0-100)
float g_crateDifficulty[4] = {1.0f, 1.0f, 1.0f, 1.0f};
Cabinet g_cabinet;
// Guerrilla's ammo crates' share of the restock by the district's morale, a step a tenth (RM 0x1662710).
constexpr float kCrateMoraleShare[11] = {0.2f, 0.3f, 0.35f, 0.4f, 0.45f, 0.5f, 0.55f, 0.6f, 0.7f, 0.8f, 1.0f};
constexpr int kCabinetSlots = 3;          // the slots the cabinet fills; the fourth is Mason's hammer
constexpr int kInventorySlots = 4;
constexpr ULONGLONG kCabinetLookMs = 1000;
constexpr DWORD kGameDifficulty = 0x10F7E98;  // 0 easy, 1 medium, 2 hard, 3 insane
constexpr DWORD kWeaponTable = 0x18F8048, kWeaponTableCount = 0x18F804C, kWeaponEntrySize = 0xE0;
constexpr DWORD kEntryInventoryItem = 0x18;   // a weapon entry -> its inventory item
constexpr DWORD kRecordWeapon = 0x18;         // an inventory record -> its weapon's handle
constexpr DWORD kWeaponEntry = 0x188;         // a weapon -> its weapon entry
// A weapon's firing modes (0x138 apart from +0x1C8; the first one's fields here): its magazine +0x2E0 (the game's getter
// 0x730EC0, which its HUD reads, and setter 0x7310F0, which the reload 0x7334B0 writes it with), and +0x2FC the rounds
// a gun lying on the ground carries besides (a dead NPC's reserve, 0x731A10 from the gun drop 0x8A2F52).
constexpr DWORD kWeaponFiringMode = 0x1C8, kWeaponMagazine = 0x2E0, kModeMagazineSize = 0x13E;
void* const kCabinetWorld = reinterpret_cast<void*>(0x1870630);
auto const RecordInSlot = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human, int slot)>(0x7A8290);
auto const DeleteRecord = reinterpret_cast<void(__cdecl*)(BYTE* human, BYTE* record, bool, bool, bool)>(0x85C6A0);
auto const GiveIntoSlot = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human, const void* inventoryItem, int rounds, int slot, bool, bool, bool)>(0x85B260);
auto const ObjectFromHandle = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, DWORD handle)>(0x436250);
auto const FiringModeInfo = reinterpret_cast<BYTE*(__fastcall*)(BYTE* slot, void* unused)>(0x42A1C0);
auto const HumansAmmo = reinterpret_cast<void*(__fastcall*)(BYTE* human, void* unused)>(0x84E8F0);
auto const ReserveRounds = reinterpret_cast<int(__fastcall*)(void* ammo, void* unused, BYTE* weapon, int slot)>(0x70EF40);
auto const AddRounds = reinterpret_cast<int(__fastcall*)(void* ammo, void* unused, BYTE* weapon, int slot, int rounds)>(0x70F4F0);
auto const TakeRounds = reinterpret_cast<int(__fastcall*)(void* ammo, void* unused, BYTE* weapon, int slot, int rounds)>(0x70F650);
// The game's setter of a firing mode's magazine (thiscall: the weapon, the rounds, the mode): it keeps it within the
// magazine's size and tells the owner's ammo store (0x70F840, 0x70F790), as the reload does.
auto const SetMagazine = reinterpret_cast<void(__fastcall*)(BYTE* weapon, void* unused, int rounds, int mode)>(0x7310F0);
// The rounds a gun carries (thiscall: the weapon, the mode, with its magazine): +0x2FC, and its magazine when it has one
// of its own (its mode info's +0x14C, Shared_Magazine_Index, -1): what a gun on the ground gives.
auto const RoundsCarried = reinterpret_cast<int(__fastcall*)(BYTE* weapon, void* unused, int mode, bool withMagazine)>(0x731B00);
// The record in a hand (0: the main one) and the one on its way there (thiscall: the human, the hand).
auto const RecordInHand = reinterpret_cast<BYTE*(__fastcall*)(BYTE* human, void* unused, int hand)>(0x84E840);
auto const RecordOnItsWay = reinterpret_cast<BYTE*(__fastcall*)(BYTE* human, void* unused, int hand)>(0x84E860);
// An item from its handle, or null for another kind of object (thiscall on the world).
auto const ItemFromHandle = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, DWORD handle)>(0x4431B0);
auto const RemoveMaterialEffect = reinterpret_cast<void(__cdecl*)(DWORD effect)>(0x6F5720);
constexpr DWORD kItemSpecialFlags = 0xC9;      // an item's special flags (as Guerrilla's: bit 0 is_ammo_box, bit 1 use_enabled)
constexpr BYTE kItemIsAmmoBox = 0x01, kItemUseEnabled = 0x02;
constexpr DWORD kItemEffect = 0x124;           // its material effect (an ammo crate's glow), -1 for none
constexpr DWORD kItemOpenedFlags = 0x130;      // its first bit cleared by the game's own open (0x8B6F22)
constexpr DWORD kNoEffect = 0xFFFFFFFF;

std::vector<std::string> SplitFields(const std::string& line, char separator) {
    std::vector<std::string> fields(1);
    for (char c : line) {
        if (c == separator) fields.emplace_back();
        else if (c != '\r' && c != '\n') fields.back() += c;
    }
    return fields;
}

std::wstring CabinetListPath() { return rfa::RootDir() + L"\\mods\\guerrilla_hud\\cabinet_weapons.txt"; }

int CabinetWeaponNamed(const std::string& guerrilla) {
    for (size_t i = 0; i < g_cabinetWeapons.size(); ++i)
        if (_stricmp(g_cabinetWeapons[i].guerrilla.c_str(), guerrilla.c_str()) == 0) return static_cast<int>(i);
    return -1;
}

int CabinetWeaponOurs(const std::string& ours) {
    for (size_t i = 0; i < g_cabinetWeapons.size(); ++i)
        if (!g_cabinetWeapons[i].ours.empty() && _stricmp(g_cabinetWeapons[i].ours.c_str(), ours.c_str()) == 0) return static_cast<int>(i);
    return -1;
}

void SaveCabinetList() {
    FILE* file = nullptr;
    if (_wfopen_s(&file, CabinetListPath().c_str(), L"w") != 0 || !file) return;
    fputs("; Guerrilla's weapon cabinet: the weapons found or bought, in order (guerrilla_hud; one list for every save)\n", file);
    for (const CabinetItem& item : g_cabinet.list)
        fprintf(file, "%s\t%d\n", g_cabinetWeapons[item.weapon].guerrilla.c_str(), item.unlocked ? 1 : 0);
    fclose(file);
}

// cabinet.txt (GuerrillaBuilder.exe), our built weapons (guerrilla_gear's player_weapons.txt), the weapons'
// upgrades (guerrilla_gear's upgrades.txt) and the list kept so far.
void ReadCabinet() {
    const std::wstring folder = rfa::RootDir() + L"\\mods\\";
    std::set<std::string> built;
    FILE* file = nullptr;
    char line[1024];
    if (_wfopen_s(&file, (folder + L"guerrilla_gear\\player_weapons.txt").c_str(), L"r") == 0 && file) {
        while (fgets(line, sizeof(line), file)) {
            std::string name = SplitFields(line, '\t')[0];
            if (!name.empty() && name[0] != ';') built.insert(name);
        }
        fclose(file);
    }
    if (_wfopen_s(&file, (folder + L"guerrilla_hud\\cabinet.txt").c_str(), L"r") != 0 || !file) {
        rfa::Log("No mods\\guerrilla_hud\\cabinet.txt (editor\\GuerrillaBuilder.exe build hud makes it): no weapon cabinet");
        return;
    }
    auto number = [](const std::string& text) { return atoi(text.c_str()); };
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == ';') continue;
        const std::vector<std::string> f = SplitFields(line, '|');
        if (f[0] == "weapon" && f.size() >= 11) {
            CabinetWeapon w;
            w.guerrilla = f[1];
            w.shown = f[2];
            w.picture = f[3];
            w.team = number(f[4]);
            w.sledgehammer = _stricmp(f[5].c_str(), "melee_sledgehammer") == 0;
            w.singularity = _stricmp(f[5].c_str(), "singularity_bomb") == 0;
            w.melee = _stricmp(f[6].c_str(), "melee") == 0;
            w.nonInventory = f[7].find("non_inventory") != std::string::npos;
            w.neverInCabinet = f[7].find("never_in_cabinet") != std::string::npos;
            w.maxRounds = number(f[8]);
            w.maxRoundsUpgraded = number(f[9]);
            w.restock = number(f[10]);
            // Our name for it: Guerrilla's, or rfg_ + it where Armageddon has one of that name (the weapons build's).
            if (built.count("rfg_" + w.guerrilla)) w.ours = "rfg_" + w.guerrilla;
            else if (built.count(w.guerrilla)) w.ours = w.guerrilla;
            g_cabinetWeapons.push_back(w);
        } else if (f[0] == "crates" && f.size() >= 5) {
            for (int i = 0; i < 4; ++i) g_crateDifficulty[i] = static_cast<float>(atof(f[i + 1].c_str()));
        } else if (f[0] == "morale" && f.size() >= 3) {
            g_districtMorale[f[1]] = number(f[2]);
        } else if (f[0] == "text" && f.size() >= 3) {
            g_cabinetTexts[f[1]] = f[2];
        }
    }
    fclose(file);
    if (_wfopen_s(&file, (folder + L"guerrilla_gear\\upgrades.txt").c_str(), L"r") == 0 && file) {
        while (fgets(line, sizeof(line), file)) {
            const std::vector<std::string> f = SplitFields(line, '\t');
            if (f[0] == "upgrade" && f.size() >= 5 && (f[3] == "Weapon" || f[3] == "Weapon Upgrade") && !f[4].empty() && f[4] != "-") {
                CabinetUpgrade u;
                u.name = f[1];
                u.type = f[3];
                u.weapon = f[4];
                g_cabinetUpgrades.push_back(u);
            } else if (f[0] == "upgrade") {
                g_cabinetUpgrades.push_back({});  // not a weapon's: its levels are skipped
            } else if (f[0] == "level" && f.size() >= 5 && !g_cabinetUpgrades.empty()) {
                CabinetUpgrade& u = g_cabinetUpgrades.back();
                u.values.push_back(number(f[2]));
                u.levelNames.push_back(f[3]);
                u.levelTexts.push_back(f[4]);
            }
        }
        fclose(file);
        g_cabinetUpgrades.erase(std::remove_if(g_cabinetUpgrades.begin(), g_cabinetUpgrades.end(),
                                               [](const CabinetUpgrade& u) { return u.name.empty(); }), g_cabinetUpgrades.end());
    }
    if (_wfopen_s(&file, CabinetListPath().c_str(), L"r") == 0 && file) {
        while (fgets(line, sizeof(line), file)) {
            if (line[0] == ';') continue;
            const std::vector<std::string> f = SplitFields(line, '\t');
            const int weapon = CabinetWeaponNamed(f[0]);
            if (weapon >= 0 && f.size() >= 2) g_cabinet.list.push_back({weapon, f[1] == "1"});
        }
        fclose(file);
    }
    rfa::Log("Weapon cabinet: %u of Guerrilla's weapons (%u built), %u weapon upgrades, %u weapons in the cabinet so far",
             static_cast<unsigned>(g_cabinetWeapons.size()),
             static_cast<unsigned>(std::count_if(g_cabinetWeapons.begin(), g_cabinetWeapons.end(), [](const CabinetWeapon& w) { return !w.ours.empty(); })),
             static_cast<unsigned>(g_cabinetUpgrades.size()), static_cast<unsigned>(g_cabinet.list.size()));
}

const BYTE* CabinetEntry(CabinetWeapon& w) {
    if (w.looked) return w.entry;
    w.looked = true;
    BYTE* entries = *reinterpret_cast<BYTE* const*>(kWeaponTable);
    const int count = *reinterpret_cast<const int*>(kWeaponTableCount);
    for (int i = 0; entries && !w.ours.empty() && i < count; ++i) {
        const char* name = *reinterpret_cast<const char* const*>(entries + i * kWeaponEntrySize);
        if (name && _stricmp(name, w.ours.c_str()) == 0) w.entry = entries + i * kWeaponEntrySize;
    }
    return w.entry;
}

// The weapon entry of an inventory record (and its weapon), or null.
const BYTE* RecordEntry(BYTE* record, BYTE** weapon = nullptr) {
    BYTE* object = record ? ObjectFromHandle(kCabinetWorld, nullptr, *reinterpret_cast<const DWORD*>(record + kRecordWeapon)) : nullptr;
    if (weapon) *weapon = object;
    return object ? *reinterpret_cast<const BYTE* const*>(object + kWeaponEntry) : nullptr;
}

// The cabinet's weapon of a weapon entry, or -1.
int CabinetWeaponOfEntry(const BYTE* entry) {
    if (!entry) return -1;
    for (size_t i = 0; i < g_cabinetWeapons.size(); ++i)
        if (CabinetEntry(g_cabinetWeapons[i]) == entry) return static_cast<int>(i);
    return -1;
}

bool Listable(const CabinetWeapon& w) { return !w.sledgehammer && !w.nonInventory && !w.ours.empty(); }

// Guerrilla's unlock_weapon then add_weapon (unlocked), or add_unique (locked): a weapon is listed once.
void CabinetAdd(int weapon, bool unlocked) {
    if (weapon < 0 || !Listable(g_cabinetWeapons[weapon])) return;
    for (CabinetItem& item : g_cabinet.list)
        if (item.weapon == weapon) {
            if (unlocked && !item.unlocked) {
                item.unlocked = true;
                ++g_cabinet.version;
                SaveCabinetList();
                rfa::Log("Weapon cabinet: %s bought", g_cabinetWeapons[weapon].guerrilla.c_str());
            }
            return;
        }
    g_cabinet.list.push_back({weapon, unlocked});
    ++g_cabinet.version;
    SaveCabinetList();
    rfa::Log("Weapon cabinet: %s added (%s)", g_cabinetWeapons[weapon].guerrilla.c_str(), unlocked ? "found" : "on sale, not bought");
}

// The level bought of an upgrade (guerrilla_gear's), and whether its next can be bought now.
int UpgradeBought(const std::string& name, bool* available = nullptr) {
    if (available) *available = false;
    if (!GearReady()) return 0;
    const int count = GuerrillaGear_UpgradeCount();
    for (int i = 0; i < count; ++i) {
        GuerrillaGearUpgrade upgrade{};
        upgrade.size = sizeof(upgrade);
        if (GuerrillaGear_Upgrade(i, &upgrade) && name == upgrade.name) {
            if (available) *available = upgrade.available != 0;
            return upgrade.bought;
        }
    }
    return 0;
}

// The weapons carried (Guerrilla's add_inventory_items, and its pick-up event), the "Weapon" upgrades bought
// (purchase_upgrade) and, with onSale, those on sale (hud_ui_upgrades_update_list, locked).
void CabinetLook(BYTE* player, bool onSale) {
    for (int slot = 0; slot < kInventorySlots; ++slot)
        CabinetAdd(CabinetWeaponOfEntry(RecordEntry(RecordInSlot(player, slot))), true);
    for (const CabinetUpgrade& u : g_cabinetUpgrades) {
        if (u.type != "Weapon") continue;
        bool available = false;
        const int bought = UpgradeBought(u.name, &available);
        if (bought >= 1) CabinetAdd(CabinetWeaponOurs(u.weapon), true);
        else if (onSale && available) CabinetAdd(CabinetWeaponOurs(u.weapon), false);
    }
}

// Guerrilla's upgrade_weapon_has_extra_ammo: the weapon's "Weapon" upgrade at a value of 2 or more.
bool HasExtraAmmo(const CabinetWeapon& w) {
    for (const CabinetUpgrade& u : g_cabinetUpgrades)
        if (u.type == "Weapon" && _stricmp(u.weapon.c_str(), w.ours.c_str()) == 0) {
            const int bought = UpgradeBought(u.name);
            return bought >= 0 && bought < static_cast<int>(u.values.size()) && u.values[bought] >= 2;
        }
    return false;
}

// The rounds a weapon will have as the cabinet closes (weapon_cabinet_get_ammo_box_ammo); -1 for none (melee).
int CabinetRounds(int weapon) {
    const CabinetWeapon& w = g_cabinetWeapons[weapon];
    const auto had = g_cabinet.roundsAtOpen.find(weapon);
    const int before = had != g_cabinet.roundsAtOpen.end() ? had->second : 0;
    if (w.neverInCabinet) return before;
    if (w.melee) return -1;
    const int most = HasExtraAmmo(w) ? w.maxRoundsUpgraded : w.maxRounds;
    if (!g_cabinet.crate) return most;
    int morale = 0;
    const auto district = g_districtMorale.find(g_view.district);
    if (district != g_districtMorale.end()) morale = district->second;
    const int step = (std::min)(10, (std::max)(0, morale / 10));
    const int level = *reinterpret_cast<const int*>(kGameDifficulty);
    const float difficulty = g_crateDifficulty[level >= 0 && level < 4 ? level : 1];  // Custom (4) as Medium (the campaign's rule)
    const int restock = static_cast<int>(static_cast<float>(w.restock) * kCrateMoraleShare[step] * difficulty);
    return (std::min)(most, before + restock);
}

// Whether a weapon is the one in the player's hands (the record in his main hand, 0x84E840, as guerrilla_gear reads it).
bool InHands(BYTE* player, const BYTE* weapon) {
    BYTE* held = nullptr;
    RecordEntry(RecordInHand(player, nullptr, 0), &held);
    return held && held == weapon;
}

// A weapon's rounds: the player's reserve of its ammo (the game's own ammo store), and its magazine while it's in his
// hands. Armageddon empties a gun's magazine into the reserve as it's put away and takes the same number back out as
// it's drawn, so a gun put away has all its rounds in the reserve (2026-11-12: each gun put away showed its magazine as
// "picked up"; m16 with 18 in its magazine put away "+18", drawn again: 18 in the magazine, the total the same).
int RoundsOf(BYTE* player, BYTE* weapon) {
    void* ammo = HumansAmmo(player, nullptr);
    const int reserve = ammo ? ReserveRounds(ammo, nullptr, weapon, 0) : 0;
    return reserve + (InHands(player, weapon) ? *reinterpret_cast<const int*>(weapon + kWeaponMagazine) : 0);
}

// Gives a weapon that many rounds in all: its magazine as full as they fill it (through the game's setter), the rest
// in the reserve; all of them in the reserve for a gun put away, which takes its magazine's out of it as it's drawn.
// The reserve is read after the magazine is set, as the setter tells the store.
void SetRounds(BYTE* player, BYTE* weapon, int total) {
    BYTE* mode = FiringModeInfo(weapon + kWeaponFiringMode, nullptr);
    const int size = mode ? *reinterpret_cast<const WORD*>(mode + kModeMagazineSize) : 0;
    const int magazine = (std::max)(0, (std::min)(total, size));
    const bool inHands = InHands(player, weapon);
    SetMagazine(weapon, nullptr, magazine, 0);
    void* ammo = HumansAmmo(player, nullptr);
    if (!ammo) return;
    const int now = ReserveRounds(ammo, nullptr, weapon, 0), wanted = (std::max)(0, inHands ? total - magazine : total);
    if (now > wanted) TakeRounds(ammo, nullptr, weapon, 0, now - wanted);
    else if (now < wanted) AddRounds(ammo, nullptr, weapon, 0, wanted - now);
}

// Guerrilla's get_weapon(offset, true): the list's place that many on from the one picked, round the end,
// passing over the singularity bomb while it isn't carried.
int CabinetPlace(int from, int by) {
    const int count = static_cast<int>(g_cabinet.list.size());
    if (count == 0) return -1;
    int place = from;
    while (by != 0) {
        place = by > 0 ? (place + 1) % count : (place + count - 1) % count;
        if (place == g_cabinet.skipped && count > 1) continue;
        by += by > 0 ? -1 : 1;
    }
    return place;
}

bool CabinetIsOpen() { return g_cabinet.open; }

// Guerrilla's ammo crate opens once: its player::open_ammo_box (RM 0xA846E0) clears the crate's use_enabled (which
// ammo_box_is_usable_by_player, RM 0xA10280, asks for unless the debug Ammo_box_unlimited_use is set), takes its glow
// off (its material effect) and its map icon away; the crate is usable again only when it's made again
// (item::serialize RM 0xAA7FFE: use_enabled = is_ammo_box), as its zone loads. Armageddon kept that whole: its own
// open (0x8B6CF0, from the ammo box's use 0x8B9C80) clears the same flag (+0xC9's second bit, which its usable check
// 0x8B29B0 reads), removes the effect (+0x124) and clears +0x130's first bit (its map icon's removal, 0xEFA6F0, does
// nothing). The cabinet opens instead of the game's open (UseAmmoCrate), so that part is done here.
// (2026-11-12: without it a crate gave its restock again at each opening.)
void UseUpAmmoCrate(DWORD handle) {
    BYTE* crate = handle != 0xFFFFFFFF ? ItemFromHandle(kCabinetWorld, nullptr, handle) : nullptr;
    if (!crate || !(crate[kItemSpecialFlags] & kItemIsAmmoBox)) {
        rfa::LogDetail("Weapon cabinet: the ammo crate it opened at is gone; nothing used up");
        return;
    }
    crate[kItemSpecialFlags] &= static_cast<BYTE>(~kItemUseEnabled);
    const DWORD effect = *reinterpret_cast<const DWORD*>(crate + kItemEffect);
    if (effect != kNoEffect) RemoveMaterialEffect(effect);
    *reinterpret_cast<DWORD*>(crate + kItemOpenedFlags) &= ~1u;
}

void OpenCabinet(bool crate) {
    BYTE* player = g_player;
    if (!player || g_cabinet.open || g_cabinetWeapons.empty()) return;
    CabinetLook(player, true);
    if (g_cabinet.list.empty()) {
        rfa::LogDetail("Weapon cabinet: nothing in it yet; it stays shut");
        return;
    }
    g_cabinet.roundsAtOpen.clear();
    bool singularityCarried = false;
    for (int slot = 0; slot < kInventorySlots; ++slot) {
        BYTE* weapon = nullptr;
        const int w = CabinetWeaponOfEntry(RecordEntry(RecordInSlot(player, slot), &weapon));
        if (w < 0 || !weapon) continue;
        g_cabinet.roundsAtOpen[w] = RoundsOf(player, weapon);
        if (g_cabinetWeapons[w].singularity) singularityCarried = true;
    }
    g_cabinet.skipped = -1;
    for (size_t i = 0; !singularityCarried && i < g_cabinet.list.size(); ++i)
        if (g_cabinetWeapons[g_cabinet.list[i].weapon].singularity) g_cabinet.skipped = static_cast<int>(i);
    g_cabinet.crate = crate;
    if (crate) UseUpAmmoCrate(g_cabinet.crateHandle);
    g_cabinet.current = 0;
    if (g_cabinet.current == g_cabinet.skipped) g_cabinet.current = CabinetPlace(0, 1);
    g_cabinet.open = true;
    tool_tips::Add(crate ? "Ammo_Crate_Weapons" : "Locker");  // hud_ui_weapon_cabinet_process (RM 0x85B33C)
    g_cabinet.scroll = g_cabinet.shine = 0;
    ++g_cabinet.version;
    g_cabinet.useDown = g_cabinet.closeDown = true;  // keys held as it opens aren't presses
    g_cabinet.upDown = g_cabinet.wDown = g_cabinet.downDown = g_cabinet.sDown = true;
    for (bool& down : g_cabinet.slotDown) down = true;
    g_cabinet.lastWheel = g_wheelTotal;
    if (!g_cabinet.paused) {
        PauseGame(1, 1, 0, 0);
        g_cabinet.paused = true;
    }
    rfa::LogDetail("Weapon cabinet opened at %s (%u weapons); the game paused", crate ? "an ammo crate (now used up)" : "a safehouse's locker",
             static_cast<unsigned>(g_cabinet.list.size()));
}

void CloseCabinet(const char* why, bool giveAmmo) {
    if (!g_cabinet.open) return;
    BYTE* player = g_player;
    if (giveAmmo && player) {
        int guns = 0;
        for (int slot = 0; slot < kInventorySlots; ++slot) {
            BYTE* weapon = nullptr;
            const int w = CabinetWeaponOfEntry(RecordEntry(RecordInSlot(player, slot), &weapon));
            if (w < 0 || !weapon) continue;
            const int rounds = CabinetRounds(w);
            if (rounds < 0) continue;
            SetRounds(player, weapon, rounds);
            ++guns;
        }
        // hud_ui_weapon_cabinet_exit: a secondary message (ui_add_secondary_message).
        if (!g_cabinet.crate && g_cabinetTexts.count("full_ammo")) ShowMessage(g_cabinetTexts["full_ammo"], false);
        rfa::LogDetail("Weapon cabinet: ammo for %d guns (%s)", guns, g_cabinet.crate ? "the crate's restock" : "full, at a locker");
        tool_tips::Add(g_cabinet.crate ? "Ammo_Boxes" : "Locker_Ammo");  // hud_ui_weapon_cabinet_exit (0x85B1D9, 0x85B20C)
    }
    tool_tips::Remove(g_cabinet.crate ? "Ammo_Crate_Weapons" : "Locker");
    g_cabinet.open = false;
    g_shop.useDown = true;  // the use key still held doesn't open it again
    if (g_cabinet.paused) {
        UnpauseGame(1, 1);
        g_cabinet.paused = false;
    }
    rfa::LogDetail("Weapon cabinet closed (%s)", why);
}

// Guerrilla's process_input for a slot key: the weapon picked into slot 0-2 of the three, swapping places with
// the slot's weapon when it's in another of them already.
void CabinetPutInSlot(int slot) {
    BYTE* player = g_player;
    if (!player || g_cabinet.list.empty()) return;
    const CabinetItem item = g_cabinet.list[g_cabinet.current];
    CabinetWeapon& chosen = g_cabinetWeapons[item.weapon];
    const BYTE* entry = CabinetEntry(chosen);
    if (!item.unlocked || !entry) {
        rfa::LogDetail("Weapon cabinet: %s isn't bought yet", chosen.guerrilla.c_str());
        return;
    }
    int from = -1;
    for (int s = 0; s < kCabinetSlots; ++s)
        if (RecordEntry(RecordInSlot(player, s)) == entry) from = s;
    if (from == slot) return;
    BYTE* old = RecordInSlot(player, slot);
    const BYTE* oldEntry = RecordEntry(old);
    const int oldWeapon = CabinetWeaponOfEntry(oldEntry);
    if (oldWeapon >= 0 && g_cabinetWeapons[oldWeapon].sledgehammer) {
        rfa::LogDetail("Weapon cabinet: slot %d holds Mason's hammer, which stays", slot + 1);
        return;
    }
    const void* oldItem = oldEntry ? *reinterpret_cast<const void* const*>(oldEntry + kEntryInventoryItem) : nullptr;
    if (old) DeleteRecord(player, old, false, false, false);
    if (from >= 0) {
        if (BYTE* moved = RecordInSlot(player, from)) DeleteRecord(player, moved, false, false, false);
        if (oldItem) GiveIntoSlot(player, oldItem, 0, from, false, false, false);
    }
    const void* chosenItem = *reinterpret_cast<const void* const*>(entry + kEntryInventoryItem);
    const BYTE* given = GiveIntoSlot(player, chosenItem, 0, slot, false, false, false);
    g_cabinet.shine = slot + 1;
    ++g_cabinet.version;
    rfa::LogDetail("Weapon cabinet: %s into slot %d%s%s", chosen.guerrilla.c_str(), slot + 1, from >= 0 ? " (swapped with the slot's)" : "",
             given ? "" : "; the game refused it");
}

void CabinetMove(int by) {
    if (g_cabinet.list.empty() || by == 0) return;
    g_cabinet.current = CabinetPlace(g_cabinet.current, by);
    g_cabinet.scroll = by > 0 ? 1 : -1;
    g_cabinet.shine = 0;
    ++g_cabinet.version;
}

// The cabinet's keys, each HUD update while it's open. After a pause in its steps (the game's own pause menu was
// up, which stops the HUD's update, or the window lost the focus) a key still held isn't a press: the Esc that
// closed the pause menu doesn't close the cabinet too. Guerrilla's keys to leave are its back's (Esc here, and Tab
// and Backspace); the use key does nothing on its screen.
void StepCabinet() {
    const ULONGLONG now = GetTickCount64();
    const bool resumed = now - g_cabinet.lastStep > kHudGoneAfterMs;
    g_cabinet.lastStep = now;
    if (!GameFocused() || resumed) {
        g_cabinet.closeDown = g_cabinet.upDown = g_cabinet.wDown = g_cabinet.downDown = g_cabinet.sDown = true;
        for (bool& down : g_cabinet.slotDown) down = true;
        g_cabinet.lastWheel = g_wheelTotal;
        return;
    }
    const int notches = (g_wheelTotal - g_cabinet.lastWheel) / kWheelNotch;
    g_cabinet.lastWheel += notches * kWheelNotch;
    int move = -notches;
    // The list runs top to bottom (its groups 0-6, the one picked in the middle): down and the wheel down the next.
    if (Pressed(VK_UP, g_cabinet.upDown)) move -= 1;
    if (Pressed(keys::Key(keys::Up), g_cabinet.wDown)) move -= 1;
    if (Pressed(VK_DOWN, g_cabinet.downDown)) move += 1;
    if (Pressed(keys::Key(keys::Down), g_cabinet.sDown)) move += 1;
    if (move != 0) CabinetMove(move);
    for (int slot = 0; slot < kCabinetSlots; ++slot)
        if (Pressed('1' + slot, g_cabinet.slotDown[slot])) CabinetPutInSlot(slot);
    const bool close = HeldDown(VK_ESCAPE) || HeldDown(VK_TAB) || HeldDown(VK_BACK);
    if (close && !g_cabinet.closeDown) {
        g_full.swallowPauseUntil = GetTickCount64() + kSwallowPauseMs;
        CloseCabinet("its key", true);
    }
    g_cabinet.closeDown = close;
}

// Each HUD update in our world: the weapons carried and bought looked at every second, and an ammo crate's use.
void FollowCabinet() {
    if (g_cabinet.pending) {
        g_cabinet.pending = false;
        if (!g_shop.open && !g_mapOpen) OpenCabinet(true);
    }
    const ULONGLONG now = GetTickCount64();
    if (g_cabinet.open || now < g_cabinet.nextLook || !g_player) return;
    g_cabinet.nextLook = now + kCabinetLookMs;
    CabinetLook(g_player, false);
}

// An ammo crate's use (its action's function, see UsableItems): asked first whether it can be used (use false),
// then used. The game's own (Armageddon's refill at once) says whether it can be; using it opens the cabinet.
bool __cdecl UseAmmoCrate(BYTE* player, bool use) {
    if (!g_ammoCrateUse) return false;
    if (!g_ammoCrateUse(player, false)) return false;
    if (!use) return true;
    if (g_cabinetWeapons.empty() || player != g_player) return g_ammoCrateUse(player, true);  // no cabinet: the game's refill
    g_cabinet.pending = true;
    g_cabinet.crateHandle = reinterpret_cast<const UseActionRecord*>(player + kPlayerUseAction)->handle;
    return true;
}

// rfg_cabinet() -> open, version, scroll, shine
//   The weapon cabinet: whether it's open; a number that changes with anything it shows; the last move (1 on,
//   -1 back, 0 none: the list's slide); the slot just filled (1-4, 0 none: its plate's shine).
int __cdecl LuaCabinet(rfa::game::lua_State* L) {
    using namespace rfa::game;
    lua_pushboolean(L, g_cabinet.open);
    lua_pushnumber(L, g_cabinet.version);
    lua_pushnumber(L, g_cabinet.scroll);
    lua_pushnumber(L, g_cabinet.shine);
    return 4;
}

// rfg_cabinet_screen() -> title, back, counter, counter's title
//   Its title (an ammo crate's or a locker's), the word by the back key, "n / count", and the word over that (the
//   document names it by Guerrilla's tag MENU_WEAPON, which Armageddon's strings haven't).
int __cdecl LuaCabinetScreen(rfa::game::lua_State* L) {
    using namespace rfa::game;
    const std::string title = g_cabinetTexts[g_cabinet.crate ? "crate" : "locker"];
    const int count = static_cast<int>(g_cabinet.list.size()) - (g_cabinet.skipped >= 0 ? 1 : 0);
    const int place = g_cabinet.current + 1 - (g_cabinet.skipped >= 0 && g_cabinet.current > g_cabinet.skipped ? 1 : 0);
    char counter[32];
    sprintf_s(counter, "%d / %d", place, count);
    lua_pushstring(L, title.c_str());
    lua_pushstring(L, g_cabinetTexts["back"].c_str());
    lua_pushstring(L, counter);
    lua_pushstring(L, g_cabinetTexts["weapon"].c_str());
    return 4;
}

// rfg_cabinet_info() -> name, ammo, upgrades, team, bought
//   The weapon picked: its name; the ammo it will have (at a crate after Guerrilla's logo; "PURCHASE AT UPGRADE
//   TABLE" if not bought; "" for none); its upgrades bought, by name; its team (-1 none, 0-3); whether bought.
int __cdecl LuaCabinetInfo(rfa::game::lua_State* L) {
    using namespace rfa::game;
    if (g_cabinet.list.empty()) return 0;
    const CabinetItem& item = g_cabinet.list[g_cabinet.current];
    const CabinetWeapon& w = g_cabinetWeapons[item.weapon];
    std::string ammo;
    if (!item.unlocked) {
        ammo = g_cabinetTexts["buy"];
    } else if (CabinetRounds(item.weapon) >= 0) {
        const int rounds = CabinetRounds(item.weapon);
        char text[160];
        if (g_cabinet.crate) sprintf_s(text, "[format][scale:0.5][image:rfg_ui_hud_guerilla_logo_white][/format] %d", rounds);
        else sprintf_s(text, "%d", rounds);
        ammo = text;
    }
    // Its upgrades bought (hud_ui_weapon_cabinet_process_weapon_info_data): each of its upgrades' level bought,
    // by name (a remote charge count by its description), a "Weapon" upgrade only from its second level.
    std::string upgrades;
    for (const CabinetUpgrade& u : g_cabinetUpgrades) {
        if (_stricmp(u.weapon.c_str(), w.ours.c_str()) != 0) continue;
        const int bought = UpgradeBought(u.name);
        if (bought < 1 || bought >= static_cast<int>(u.levelNames.size())) continue;
        if (u.type == "Weapon" && u.values[bought] < 2) continue;
        std::string name = u.levelNames[bought];
        if (name == g_cabinetTexts["charges"]) name = u.levelTexts[bought];
        if (name.empty()) continue;
        if (!upgrades.empty()) upgrades += ", ";
        upgrades += name;
    }
    lua_pushstring(L, w.shown.c_str());
    lua_pushstring(L, ammo.c_str());
    lua_pushstring(L, upgrades.c_str());
    lua_pushnumber(L, w.team);
    lua_pushboolean(L, item.unlocked);
    return 5;
}

// rfg_cabinet_icon(offset) -> picture, bought
//   The list's weapon that many on from the one picked (-3 to 3, Guerrilla's seven): its picture, whether bought.
int __cdecl LuaCabinetIcon(rfa::game::lua_State* L) {
    using namespace rfa::game;
    const int place = CabinetPlace(g_cabinet.current, static_cast<int>(lua_tonumber(L, 1)));
    if (place < 0) return 0;
    const CabinetItem& item = g_cabinet.list[place];
    lua_pushstring(L, g_cabinetWeapons[item.weapon].picture.c_str());
    lua_pushboolean(L, item.unlocked);
    return 2;
}

// rfg_cabinet_slot(slot) -> picture, ammo
//   The player's slot 1-4: its weapon's picture and the rounds it will have (-1 for none); nothing for an empty
//   slot or a weapon not Guerrilla's.
int __cdecl LuaCabinetSlot(rfa::game::lua_State* L) {
    using namespace rfa::game;
    const int slot = static_cast<int>(lua_tonumber(L, 1)) - 1;
    if (!g_player || slot < 0 || slot >= kInventorySlots) return 0;
    const int w = CabinetWeaponOfEntry(RecordEntry(RecordInSlot(g_player, slot)));
    if (w < 0) return 0;
    lua_pushstring(L, g_cabinetWeapons[w].picture.c_str());
    lua_pushnumber(L, CabinetRounds(w));
    return 2;
}

// ---- Guerrilla's handbook: its pause menu's HANDBOOK
//
// Guerrilla's handbook (hud_ui_handbook_*, its document rfg_ui_handbook; RM 0x8691E0 its frame) lists the entries of
// guerrilla_handbook.xtbl (handbook.txt, GuerrillaBuilder.exe's), a page for each category, only the pages with an
// entry open (hud_ui_handbook_count_viewable_pages) and only the open entries on them. An entry is open from the
// start (Unlocked) or once the game opens it (hud_ui_handbook_unlock_entry): here, by what this mod keeps already:
//   - a weapon in the weapon cabinet's list (found, bought or on sale) opens the entry of its Guerrilla name
//     (hud_ui_weapon_cabinet_add_weapon RM 0x877200: unlock_entry(crc of the weapon's name));
//   - a radio tag found opens the radio tags' page's ("BLAST FROM THE PAST") entry of its number
//     (bftp_touch_tag RM 0xA8DBB2: hud_ui_handbook_unlock_bftp_entry(the tag's number)).
// Not yet: the activities' starts (activity_base::activity_initialize opens theirs), the tool tips, the cutscenes;
// the videos (gameplay, cutscenes) and the radio tags' recordings and portraits.
// An entry not read yet is marked new (its page too) until it's shown (handbook_entry_set_viewed): the entries read
// are kept in mods\guerrilla_hud\handbook_read.txt, one list for every save (as the cabinet's list).
// It opens from Armageddon's pause menu (rfg_handbook_open: the game stays paused, held by this mod, as the pause
// menu closes), and its back key (Esc, Tab or Backspace) closes it and opens the pause menu again, as Guerrilla's
// went back to its menu. Up / down pick an entry (W / S, the arrows, the mouse wheel), left / right the page (A / D,
// the arrows): [GuerrillaKeys] Up, Down, Left, Right.
struct HandbookPage {
    std::string tag, shown, kind;
};
struct HandbookEntry {
    std::string name, title, text, picture, video, icon, voice;
    std::string pictureDocument;  // the document its picture is in (loaded while it's shown)
    int page = 0;           // in g_handbookPages
    bool openAtStart = false;
};
struct Handbook {
    bool open = false, paused = false, pending = false;
    bool closing = false;   // closed by its back key: the pause menu opens once its documents are gone
    int page = 0;           // the page shown, in g_handbookPages
    int row = 0;            // the entry picked, among the page's open ones
    int top = 0;            // the first row shown
    int version = 0;        // changed with anything shown
    bool upDown = true, downDown = true, leftDown = true, rightDown = true, closeDown = true;
    int lastWheel = 0;
    ULONGLONG lastStep = 0;
};
std::vector<HandbookPage> g_handbookPages;
std::vector<HandbookEntry> g_handbookEntries;
std::map<std::string, std::string> g_handbookTexts;  // title, new, back, select
std::set<std::string> g_handbookRead;
Handbook g_handbook;
constexpr int kHandbookRows = 16;   // Guerrilla's MAX_ROWS
constexpr int kHandbookPagesShown = 8;  // its MAX_PAGES (its page tabs)

std::wstring HandbookReadPath() { return rfa::RootDir() + L"\\mods\\guerrilla_hud\\handbook_read.txt"; }

// A text field of handbook.txt: "\n" (the two characters) a new line.
std::string HandbookText(const std::string& field) {
    std::string text;
    for (size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 1 < field.size() && (field[i + 1] == 'n' || field[i + 1] == 'N')) {
            text += '\n';
            ++i;
        } else {
            text += field[i];
        }
    }
    return text;
}

void ReadHandbook() {
    FILE* file = nullptr;
    char line[8192];
    if (_wfopen_s(&file, (rfa::RootDir() + L"\\mods\\guerrilla_hud\\handbook.txt").c_str(), L"r") != 0 || !file) {
        rfa::Log("No mods\\guerrilla_hud\\handbook.txt (editor\\GuerrillaBuilder.exe build hud makes it): no handbook");
        return;
    }
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == ';') continue;
        const std::vector<std::string> f = SplitFields(line, '|');
        if (f[0] == "page" && f.size() >= 5) {
            g_handbookPages.push_back({f[2], f[3], f[4]});
        } else if (f[0] == "entry" && f.size() >= 10) {
            HandbookEntry e;
            e.name = f[1];
            e.page = atoi(f[2].c_str()) - 1;
            e.title = HandbookText(f[3]);
            e.text = HandbookText(f[4]);
            e.picture = f[5];
            e.video = f[6];
            e.icon = f[7];
            e.openAtStart = f[8] == "1";
            if (f.size() >= 10) e.voice = f[9];
            if (f.size() >= 11) e.pictureDocument = f[10];
            if (e.page >= 0 && e.page < static_cast<int>(g_handbookPages.size())) g_handbookEntries.push_back(e);
        } else if (f[0] == "text" && f.size() >= 3) {
            g_handbookTexts[f[1]] = HandbookText(f[2]);
        }
    }
    fclose(file);
    if (_wfopen_s(&file, HandbookReadPath().c_str(), L"r") == 0 && file) {
        while (fgets(line, sizeof(line), file)) {
            const std::string name = SplitFields(line, '\t')[0];
            if (!name.empty() && name[0] != ';') g_handbookRead.insert(name);
        }
        fclose(file);
    }
    rfa::Log("Handbook: %u entries on %u pages, %u read so far", static_cast<unsigned>(g_handbookEntries.size()),
             static_cast<unsigned>(g_handbookPages.size()), static_cast<unsigned>(g_handbookRead.size()));
}

void SaveHandbookRead() {
    FILE* file = nullptr;
    if (_wfopen_s(&file, HandbookReadPath().c_str(), L"w") != 0 || !file) return;
    fputs("; Guerrilla's handbook: the entries read (guerrilla_hud; one list for every save)\n", file);
    for (const std::string& name : g_handbookRead) fprintf(file, "%s\n", name.c_str());
    fclose(file);
}

bool HandbookEntryOpen(const HandbookEntry& e) {
    if (e.openAtStart) return true;
    const std::string& kind = g_handbookPages[e.page].kind;
    if (kind == "bftp") {
        const int number = atoi(e.name.c_str());
        return number > 0 && g_tagsFound.count(number) != 0;
    }
    for (const CabinetItem& item : g_cabinet.list)
        if (_stricmp(g_cabinetWeapons[item.weapon].guerrilla.c_str(), e.name.c_str()) == 0) return true;
    return false;
}

bool HandbookEntryNew(const HandbookEntry& e) { return HandbookEntryOpen(e) && g_handbookRead.count(e.name) == 0; }

// The open entries of a page, in the table's order.
std::vector<int> HandbookRows(int page) {
    std::vector<int> rows;
    for (size_t i = 0; i < g_handbookEntries.size(); ++i)
        if (g_handbookEntries[i].page == page && HandbookEntryOpen(g_handbookEntries[i])) rows.push_back(static_cast<int>(i));
    return rows;
}

// The pages shown: those with an open entry.
std::vector<int> HandbookPagesShown() {
    std::vector<int> pages;
    for (int page = 0; page < static_cast<int>(g_handbookPages.size()); ++page)
        if (!HandbookRows(page).empty() && static_cast<int>(pages.size()) < kHandbookPagesShown) pages.push_back(page);
    return pages;
}

bool HandbookIsOpen() { return g_handbook.open; }

// The entry picked, marked read (handbook_entry_set_viewed).
void HandbookMarkRead() {
    const std::vector<int> rows = HandbookRows(g_handbook.page);
    if (g_handbook.row < 0 || g_handbook.row >= static_cast<int>(rows.size())) return;
    if (g_handbookRead.insert(g_handbookEntries[rows[g_handbook.row]].name).second) SaveHandbookRead();
}

void HandbookPick(int page, int row) {
    const std::vector<int> rows = HandbookRows(page);
    g_handbook.page = page;
    g_handbook.row = rows.empty() ? 0 : (std::max)(0, (std::min)(row, static_cast<int>(rows.size()) - 1));
    if (g_handbook.row < g_handbook.top) g_handbook.top = g_handbook.row;
    if (g_handbook.row >= g_handbook.top + kHandbookRows) g_handbook.top = g_handbook.row - kHandbookRows + 1;
    g_handbook.top = (std::max)(0, (std::min)(g_handbook.top, (std::max)(0, static_cast<int>(rows.size()) - kHandbookRows)));
    ++g_handbook.version;
}

// rfg_handbook_open (the pause menu's HANDBOOK): the game paused now, held by this mod, and the handbook opened at the
// next HUD update (the pause menu closes first).
void RequestHandbook() {
    if (g_handbook.open || g_handbook.pending || g_handbookEntries.empty()) return;
    g_handbook.pending = true;
    if (!g_handbook.paused) {
        PauseGame(1, 1, 0, 0);
        g_handbook.paused = true;
    }
}

void OpenHandbook() {
    g_handbook.pending = false;
    const std::vector<int> pages = HandbookPagesShown();
    const Document* screen = FindDocument("rfg_ui_handbook");
    if (pages.empty() || !InOurWorld() || !screen || screen->missing) {
        if (g_handbook.paused) {
            UnpauseGame(1, 1);
            g_handbook.paused = false;
        }
        rfa::LogDetail("Handbook: nothing open in it; it stays shut");
        return;
    }
    g_handbook.open = true;
    g_handbook.top = 0;
    HandbookPick(pages[0], 0);
    HandbookMarkRead();
    g_handbook.upDown = g_handbook.downDown = g_handbook.leftDown = g_handbook.rightDown = g_handbook.closeDown = true;
    g_handbook.lastWheel = g_wheelTotal;
    if (!g_handbook.paused) {
        PauseGame(1, 1, 0, 0);
        g_handbook.paused = true;
    }
    rfa::LogDetail("Handbook opened (%u pages, %u entries open); the game paused", static_cast<unsigned>(pages.size()),
             static_cast<unsigned>(std::count_if(g_handbookEntries.begin(), g_handbookEntries.end(), HandbookEntryOpen)));
}

bool g_handbookBackToPauseMenu = false;  // closed by its back key: the pause menu opens again (FollowPauseMenuBlock)

// Closed by its back key, it goes back to the pause menu once its documents are gone (FollowHandbook), the game kept
// paused and the pause menu kept from opening till then: the pause menu's documents mustn't be loaded on top of the
// handbook's while those are still to go (the interface's memory is a stack, see Wanted).
void CloseHandbook(const char* why, bool toPauseMenu) {
    const bool wasOpen = g_handbook.open || g_handbook.pending;
    g_handbook.open = g_handbook.pending = false;
    HandbookPictureShown("");
    g_handbook.closing = wasOpen && toPauseMenu;
    if (g_handbook.paused && !g_handbook.closing) {
        UnpauseGame(1, 1);
        g_handbook.paused = false;
    }
    if (!wasOpen) return;
    rfa::LogDetail("Handbook closed (%s)", why);
}

bool HandbookClosing() { return g_handbook.closing; }

// Its keys, each HUD update while it's open (see StepCabinet for the pause in its steps).
void StepHandbook() {
    const ULONGLONG now = GetTickCount64();
    const bool resumed = now - g_handbook.lastStep > kHudGoneAfterMs;
    g_handbook.lastStep = now;
    if (!GameFocused() || resumed) {
        g_handbook.upDown = g_handbook.downDown = g_handbook.leftDown = g_handbook.rightDown = g_handbook.closeDown = true;
        g_handbook.lastWheel = g_wheelTotal;
        return;
    }
    const int notches = (g_wheelTotal - g_handbook.lastWheel) / kWheelNotch;
    g_handbook.lastWheel += notches * kWheelNotch;
    // A key and its arrow count as one: pressed as either goes down while neither was.
    auto pressed = [](int arrow, keys::Action action, bool& was) {
        const bool down = HeldDown(arrow) || HeldDown(keys::Key(action));
        const bool press = down && !was;
        was = down;
        return press;
    };
    int move = -notches;
    if (pressed(VK_UP, keys::Up, g_handbook.upDown)) move -= 1;
    if (pressed(VK_DOWN, keys::Down, g_handbook.downDown)) move += 1;
    int turn = 0;
    if (pressed(VK_LEFT, keys::Left, g_handbook.leftDown)) turn -= 1;
    if (pressed(VK_RIGHT, keys::Right, g_handbook.rightDown)) turn += 1;
    if (turn != 0) {
        const std::vector<int> pages = HandbookPagesShown();
        const auto at = std::find(pages.begin(), pages.end(), g_handbook.page);
        const int place = at == pages.end() ? 0 : static_cast<int>(at - pages.begin());
        const int count = static_cast<int>(pages.size());
        if (count > 0) {
            g_handbook.top = 0;
            HandbookPick(pages[((place + turn) % count + count) % count], 0);
            HandbookMarkRead();
        }
    } else if (move != 0) {
        const int count = static_cast<int>(HandbookRows(g_handbook.page).size());
        if (count > 0) {
            HandbookPick(g_handbook.page, ((g_handbook.row + move) % count + count) % count);
            HandbookMarkRead();
        }
    }
    const bool close = HeldDown(VK_ESCAPE) || HeldDown(VK_TAB) || HeldDown(VK_BACK);
    if (close && !g_handbook.closeDown) CloseHandbook("its key", true);
    g_handbook.closeDown = close;
}

// The pause menu's documents (it opens the handbook): they were loaded after ours, and go as it closes; the
// handbook's are loaded once they're gone, so that theirs stay on top (see Wanted).
constexpr const char* kPauseMenuDocuments[] = {"ui_pause_menu", "ui_background_ingame"};
constexpr ULONGLONG kHandbookWaitMostMs = 5000;
ULONGLONG g_handbookAskedAt = 0;

bool PauseMenuDocumentsOpen() {
    for (const char* name : kPauseMenuDocuments)
        if (OpenDocument(name)) return true;
    return false;
}

// A picture sheet tied to its texture again. Armageddon ties each sheet of its sheet tables to a texture once, as it
// reads them at start-up (0xC4F2E0: for each sheet, 0xCAFD90 finds the texture by its name "<sheet>.tga", or makes it
// an empty slot, and keeps the slot at the sheet's +4; sheets at 0x28BBD68, 12 bytes each, the count at 0x28BC698).
// The zones' pictures load once and fill those slots; a handbook picture's document is loaded and unloaded as its
// entry is picked and left, and a texture loaded again needn't land in the slot the sheet kept: its picture then came
// out a small grey box, the same for every entry (2026-11-12, at image_holder's corner). So once a picture's document
// has settled, its sheet is pointed at the slot its texture has now: found by name (0xCAF200, which makes nothing new;
// -1 if not there). Logged, with what it was before.
struct SheetEntry {
    const char* name;  // the sheet's texture's name ("<sheet>.tga")
    int bitmap;        // its slot, -1 if none
    int rest;          // not used here
};
static_assert(sizeof(SheetEntry) == 12, "Armageddon's sheet entries are 12 bytes");
auto* const kSheets = reinterpret_cast<SheetEntry*>(0x28BBD68);
auto& g_sheetCount = *reinterpret_cast<unsigned*>(0x28BC698);
auto const FindBitmapByName = reinterpret_cast<int(__cdecl*)(const char* name)>(0xCAF200);

// Whether a sheet entry's name is this sheet's, with or without ".tga".
bool SheetNamed(const char* name, const std::string& sheet) {
    if (!name || _strnicmp(name, sheet.c_str(), sheet.size()) != 0) return false;
    return name[sheet.size()] == '\0' || _stricmp(name + sheet.size(), ".tga") == 0;
}

void TieSheetAgain(const std::string& sheet) {
    static int logged = 0;
    for (unsigned i = 0; i < g_sheetCount; ++i) {
        SheetEntry& entry = kSheets[i];
        if (!SheetNamed(entry.name, sheet)) continue;
        const int now = FindBitmapByName(entry.name);
        if (logged < 40) {
            ++logged;
            rfa::Log("Handbook picture %s: its sheet had texture slot %d, the texture is at %d now%s", sheet.c_str(), entry.bitmap, now,
                     now == -1 ? " (not found: left as it was)" : now == entry.bitmap ? "" : " (tied again)");
        }
        if (now != -1) entry.bitmap = now;
        return;
    }
    if (logged < 40) {
        ++logged;
        rfa::Log("Handbook picture %s: no such sheet among the game's %u", sheet.c_str(), g_sheetCount);
    }
}

// Each HUD update in our world.
void FollowHandbook() {
    if (g_handbook.closing && !AnyHandbookDocumentLoaded()) {
        g_handbook.closing = false;
        if (g_handbook.paused) {
            UnpauseGame(1, 1);
            g_handbook.paused = false;
        }
        g_handbookBackToPauseMenu = true;
        g_pauseMenuRequest = 1;  // the HUD's frame opens the pause menu (0x57B774)
    }
    if (g_handbook.pending) {
        if (g_handbookAskedAt == 0) g_handbookAskedAt = GetTickCount64();
        if (!PauseMenuDocumentsOpen()) {
            g_handbookAskedAt = 0;
            OpenHandbook();
        } else if (GetTickCount64() - g_handbookAskedAt > kHandbookWaitMostMs) {
            g_handbookAskedAt = 0;
            rfa::Log("Handbook: the pause menu's documents are still open after %llu s; it stays shut", kHandbookWaitMostMs / 1000);
            CloseHandbook("the pause menu stayed", false);
        }
    }
    if (g_handbook.open) {
        StepHandbook();
        // The picked entry's picture, once its document has settled: the screen asks again and shows it.
        static std::string lastWanted, settled;
        const std::string wanted = HandbookPictureWanted();
        if (wanted != lastWanted) {
            lastWanted = wanted;
            settled.clear();
        }
        if (!wanted.empty() && settled != wanted) {
            Document* doc = FindDocument(wanted);
            if (doc && DocumentSettled(*doc)) {
                settled = wanted;
                for (const HandbookEntry& e : g_handbookEntries)
                    if (e.pictureDocument == wanted) {
                        TieSheetAgain(e.picture + "_sheet");
                        break;
                    }
                ++g_handbook.version;
            }
        }
    }
}

// The picked entry's picture's document, "" for none (see Wanted).
std::string HandbookPictureWanted() {
    const std::vector<int> rows = HandbookRows(g_handbook.page);
    if (!g_handbook.open || g_handbook.row < 0 || g_handbook.row >= static_cast<int>(rows.size())) return "";
    return g_handbookEntries[rows[g_handbook.row]].pictureDocument;
}

// One of its documents has arrived: what the screen shows changes.
void HandbookDocumentArrived() { ++g_handbook.version; }

// An entry's picture can be shown once its document has settled (as the zone pictures': see DocumentSettled).
bool HandbookPictureReady(const HandbookEntry& e) {
    if (e.pictureDocument.empty()) return false;
    Document* doc = FindDocument(e.pictureDocument);
    return doc && DocumentSettled(*doc);
}

// The picture the screen shows now (its script says, rfg_handbook_picture_shown), and the one it showed last, released
// at g_handbookPictureReleasedAt: its document stays that much longer (see Wanted).
std::string g_handbookShownDocument, g_handbookReleasedDocument;
ULONGLONG g_handbookPictureReleasedAt = 0;
constexpr ULONGLONG kHandbookPictureLingerMs = 300;

std::string HandbookPictureDocumentOf(const std::string& picture) {
    if (picture.empty()) return "";
    for (const HandbookEntry& e : g_handbookEntries)
        if (e.picture == picture) return e.pictureDocument;
    return "";
}

void HandbookPictureShown(const std::string& document) {
    if (document == g_handbookShownDocument) return;
    g_handbookReleasedDocument = g_handbookShownDocument;
    g_handbookPictureReleasedAt = GetTickCount64();
    g_handbookShownDocument = document;
}

bool HandbookPictureKept(const Document& doc) {
    if (doc.name == g_handbookShownDocument) return true;
    return doc.name == g_handbookReleasedDocument && GetTickCount64() - g_handbookPictureReleasedAt < kHandbookPictureLingerMs;
}

// rfg_handbook_available() -> available, label
//   For Armageddon's pause menu: whether its HANDBOOK shows (in our world, with entries), and its word.
int __cdecl LuaHandbookAvailable(rfa::game::lua_State* L) {
    using namespace rfa::game;
    const bool available = InOurWorld() && !g_handbookEntries.empty();
    lua_pushboolean(L, available);
    const auto title = g_handbookTexts.find("title");
    lua_pushstring(L, title != g_handbookTexts.end() ? title->second.c_str() : "HANDBOOK");
    return 2;
}

// rfg_handbook_open() -> opened: the pause menu's HANDBOOK (the pause menu closes itself after).
int __cdecl LuaHandbookOpen(rfa::game::lua_State* L) {
    RequestHandbook();
    rfa::game::lua_pushboolean(L, g_handbook.pending);
    return 1;
}

// rfg_handbook() -> open, version: whether it's open, and a number that changes with anything it shows.
int __cdecl LuaHandbook(rfa::game::lua_State* L) {
    using namespace rfa::game;
    lua_pushboolean(L, g_handbook.open);
    lua_pushnumber(L, g_handbook.version);
    return 2;
}

// rfg_handbook_screen() -> title, page name, page number (1-), pages, show scroll, scroll percent, back word, new word
//   and rfg_handbook_page_new(i) -> whether the i-th page shown (1-) has an entry not read yet.
int __cdecl LuaHandbookScreen(rfa::game::lua_State* L) {
    using namespace rfa::game;
    const std::vector<int> pages = HandbookPagesShown();
    const auto at = std::find(pages.begin(), pages.end(), g_handbook.page);
    const int rows = static_cast<int>(HandbookRows(g_handbook.page).size());
    lua_pushstring(L, g_handbookTexts["title"].c_str());
    lua_pushstring(L, g_handbook.page < static_cast<int>(g_handbookPages.size()) ? g_handbookPages[g_handbook.page].shown.c_str() : "");
    lua_pushnumber(L, at == pages.end() ? 1 : static_cast<double>(at - pages.begin() + 1));
    lua_pushnumber(L, static_cast<double>(pages.size()));
    lua_pushboolean(L, rows > kHandbookRows);
    lua_pushnumber(L, rows > kHandbookRows ? static_cast<double>(g_handbook.top) / (rows - kHandbookRows) : 0.0);
    lua_pushstring(L, g_handbookTexts["back"].c_str());
    lua_pushstring(L, g_handbookTexts["new"].c_str());
    return 8;
}

int __cdecl LuaHandbookPageNew(rfa::game::lua_State* L) {
    using namespace rfa::game;
    const int i = static_cast<int>(lua_tonumber(L, 1)) - 1;
    const std::vector<int> pages = HandbookPagesShown();
    bool isNew = false;
    if (i >= 0 && i < static_cast<int>(pages.size()))
        for (int entry : HandbookRows(pages[i])) isNew = isNew || HandbookEntryNew(g_handbookEntries[entry]);
    lua_pushboolean(L, isNew);
    return 1;
}

// rfg_handbook_rows() -> count, picked: the rows shown (at most 16) and which of them is picked (1-).
// rfg_handbook_row(i) -> caption, new: the i-th row shown.
int __cdecl LuaHandbookRows(rfa::game::lua_State* L) {
    using namespace rfa::game;
    const int rows = static_cast<int>(HandbookRows(g_handbook.page).size());
    lua_pushnumber(L, (std::min)(kHandbookRows, rows - g_handbook.top));
    lua_pushnumber(L, g_handbook.row - g_handbook.top + 1);
    return 2;
}

int __cdecl LuaHandbookRow(rfa::game::lua_State* L) {
    using namespace rfa::game;
    const std::vector<int> rows = HandbookRows(g_handbook.page);
    const int i = g_handbook.top + static_cast<int>(lua_tonumber(L, 1)) - 1;
    if (i < 0 || i >= static_cast<int>(rows.size())) return 0;
    const HandbookEntry& e = g_handbookEntries[rows[i]];
    lua_pushstring(L, e.title.c_str());
    lua_pushboolean(L, HandbookEntryNew(e));
    return 2;
}

// rfg_handbook_entry() -> kind, title, text, picture, icon, video, name, counter: the entry picked, and its page's kind
// (gameplay, actions, combat, vehicles, background, bftp, cutscene); "" for none (the picture until its document has
// settled). A radio tag's entry is named by its number; the radio tags' page's counter is the tags found of all
// (hud_ui_handbook_process_page: "%d/%d" of the entries opened, [0x2B9DCE0], and the page's entries less its first).
int __cdecl LuaHandbookEntry(rfa::game::lua_State* L) {
    using namespace rfa::game;
    const std::vector<int> rows = HandbookRows(g_handbook.page);
    if (g_handbook.row < 0 || g_handbook.row >= static_cast<int>(rows.size())) return 0;
    const HandbookEntry& e = g_handbookEntries[rows[g_handbook.row]];
    lua_pushstring(L, g_handbookPages[e.page].kind.c_str());
    lua_pushstring(L, e.title.c_str());
    lua_pushstring(L, e.text.c_str());
    lua_pushstring(L, HandbookPictureReady(e) ? e.picture.c_str() : "");  // its document loads while it's shown
    lua_pushstring(L, e.icon.c_str());
    lua_pushstring(L, e.video.c_str());
    lua_pushstring(L, e.name.c_str());
    std::string counter;
    if (g_handbookPages[e.page].kind == "bftp") {
        int tags = 0;
        for (const HandbookEntry& other : g_handbookEntries)
            if (other.page == e.page && atoi(other.name.c_str()) > 0) ++tags;
        char text[32];
        sprintf_s(text, "%d/%d", static_cast<int>(g_tagsFound.size()), tags);
        counter = text;
    }
    lua_pushstring(L, counter.c_str());
    return 8;
}

// rfg_handbook_picture_shown(picture): the screen shows that picture now ("" for none): its document stays (Wanted).
int __cdecl LuaHandbookPictureShown(rfa::game::lua_State* L) {
    using namespace rfa::game;
    const char* picture = lua_type(L, 1) == 4 ? lua_tolstring(L, 1, nullptr) : nullptr;
    HandbookPictureShown(HandbookPictureDocumentOf(picture ? picture : ""));
    return 0;
}

// rfg_handbook_clock() -> seconds: a clock for the screen's own movements (the documents' tweens stand still while
// the game is paused, as it is while the handbook is open).
int __cdecl LuaHandbookClock(rfa::game::lua_State* L) {
    rfa::game::lua_pushnumber(L, static_cast<double>(GetTickCount64() % 100000000ULL) / 1000.0);
    return 1;
}

// ---- Guerrilla's messages: its message lines, pickups, use prompt, weapon swap and saving icon
//
// The document rfg_ui_messages (Guerrilla's, run by its own script and ours, rfg_ui_messages.lua) shows them; Guerrilla's
// game code fed it, which Armageddon hasn't, so here is what our script needs (RM: ui_process_primary_messages 0x8E8890,
// ui_process_secondary_messages 0x8E8E50, hud_ui_add_item_pickup_message 0x895700, hud_ui_process_use_action_info
// 0x8E9410, hud_render_pickup_message 0x8550C0 with hud_ui_set_swap_data 0x8B1EA0, ui_process_save_data 0x8B96F0):
//   - message lines: the major line in the middle (Guerrilla's primary messages) and the minor line at the top left
//     (its secondary ones), each 3 s unless given. Ours go to the script through rfg_message_next while the document
//     is up (Armageddon's message line otherwise); Armageddon's own lines (its messages_hud) the script takes itself.
//   - pickups: Guerrilla lists what is picked up beside the reticle, at most three rows at once: salvage (its total,
//     with "+N" over it from 3 up), a weapon picked up, rounds picked up ("+ N"), each with its picture; the same single
//     thing picked up again while its row shows adds to that row. Here they're seen as they happen: the salvage and
//     the carried weapons' rounds looked at each HUD update (not while a safehouse's screen or the world map is up,
//     nor just after: what those give isn't picked up, as Guerrilla's cabinet gave its ammo without rows).
//   - the use prompt: Guerrilla's use action (t_usable_object, which Armageddon kept with its own text after it, at
//     the player's +0x1540) names its prompt's picture (use_object_icon: 1 a hand, 2 a hydrogen tank, 3 a ladder, 4 a
//     vehicle, 5 a mission's start, 10 the upgrades; 0 a weapon to swap). Its prompt shows for any of 1-10 on foot;
//     the safehouses' stations are ours (see above), and the start of one of Guerrilla's activities is the
//     guerrilla_campaign mod's (GuerrillaCampaign_UsePrompt, when it has one). Guerrilla's prompts replace
//     Armageddon's ("[E] Enter" at a car, 2026-11-12): its own shows only for an action they have no picture for.
//   - the weapon swap: icon 0 at a weapon on the ground (Armageddon's search, 0x8B29F0, gives it when no slot is free;
//     the weapon to drop is 0x8BE980's): the held weapon's picture and rounds, an arrow, the other's.
//   - the saving icon: while the guerrilla_save mod makes a save (GuerrillaSave_Saving) and 3 s of play after.
struct GuerrillaMessage {
    bool major = false;
    std::string text;
    float seconds = -1.0f;      // -1: Guerrilla's 3 s
};
std::deque<GuerrillaMessage> g_messages;
constexpr size_t kMessagesKept = 16;

struct Pickup {
    std::string picture;
    int amount = 0;             // rounds, or salvage, picked up (0: a weapon without rounds)
    int total = 0;              // salvage: the total after
    bool salvage = false;
};
struct Pickups {
    std::vector<Pickup> rows;   // the rows shown now
    int version = 0;            // changed with each new set of rows
    bool merged = false;        // the last change added to the one row shown
    ULONGLONG shownAt = 0;
    bool known = false;         // what was carried is known (below)
    ULONGLONG quietUntil = 0;   // until then what changes isn't picked up (a level's start, a screen just closed)
    const BYTE* entries[kInventorySlots] = {};
    int rounds[kInventorySlots] = {};
    int salvage = -1;
    const BYTE* inHand = nullptr;      // the records in his hand and on their way there, as last seen
    const BYTE* onItsWay = nullptr;
    ULONGLONG handsChangedAt = 0;
};
Pickups g_pickups;
constexpr int kPickupRows = 3;
// Rounds that come while a weapon is drawn or put away (the game moves its magazine to and from the reserve then)
// aren't picked up: not within this long of a change in his hands.
constexpr ULONGLONG kPickupHandsQuietMs = 750;
constexpr ULONGLONG kPickupMergeMs = 2500;      // a row takes this long to fade (its animation, ammo_anim)
constexpr ULONGLONG kPickupQuietAfterLoadMs = 3000;
constexpr ULONGLONG kPickupQuietAfterScreenMs = 500;
const char* const kSalvagePicture = "rfg_ui_hud_icon_salvage";

constexpr int kUseIconSwap = 0, kUseIconDefault = 1, kUseIconUpgrade = 10, kUseIconLast = 10;
constexpr DWORD kObjectSubtype = 0x7B;          // an object's subtype (0x423530); 9 a weapon
constexpr int kWeaponSubtype = 9;
auto const SwapRecord = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human)>(0x8BE980);  // the record a swap drops

struct UsePrompt {
    int useType = 0;            // Guerrilla's use_object_icon, 0 none
    bool swap = false;
    std::string currentIcon, nextIcon;
    int currentAmmo = -1, nextAmmo = -1;  // -1: none shown
    bool armageddonOwn = false; // an action Guerrilla's prompts have no picture for: Armageddon's own prompt shows it
};

constexpr ULONGLONG kSavingAfterMs = 3000;
ULONGLONG g_savingLeftMs = 0;   // HUD time the saving icon still shows
unsigned g_savesMade = 0;
bool g_savesKnown = false;

bool MessagesDocumentUp() {
    const Document* doc = FindDocument("rfg_ui_messages");
    return doc && doc->loaded && doc->seen && InOurWorld();
}

void ShowMessage(const std::string& text, bool major, float seconds) {
    if (text.empty()) return;
    if (!MessagesDocumentUp()) {
        AddHudMessage(text.c_str());
        return;
    }
    if (g_messages.size() >= kMessagesKept) g_messages.pop_front();
    g_messages.push_back({major, text, seconds});
}

// ---- The campaign's messages: Guerrilla's minor messages and tool tips that guerrilla_campaign raises
//
// The campaign queues them (GuerrillaCampaign_NextMessage(&tag, &icon, &kind, &ms), 1 while there's one): kind 0 a
// minor message (hud_message_add_minor: Guerrilla's string tag), kind 2 a minor message whose text takes a number (the
// number in ms: HM_GUERRILLAS_ASSIST "[IMAGE:...] %ls GUERRILLAS JOINED"), kind 1 a tool tip by its name, ms the
// number Guerrilla's ui_tool_tips_add_tip was given (its wait before the tip shows). The texts are Guerrilla's
// (hud_messages.txt, made by GuerrillaBuilder.exe: text|tag|text, its pictures named rfg_...); a number goes in place
// of the text's first %ls, %s, %d or %i, as Guerrilla's HUD formatted it. A message's icon (the skull, the EDF's and
// the propaganda's diversion icons) is for Guerrilla's control and morale meters, not shown yet (logged). Taken each
// HUD update in our worlds, at most kCampaignMessagesAFrame.
constexpr int kCampaignMessageMinor = 0, kCampaignMessageTip = 1, kCampaignMessageCount = 2;
constexpr int kCampaignMessagesAFrame = 8;
std::map<std::string, std::string> g_hudMessageTexts;  // Guerrilla's tag -> its text

// Guerrilla's strings by their tag's hash (hud_messages.txt's "hash|XXXXXXXX|text" lines: every string of its hud,
// activity, menu, scripts and mission strings). A strings file keeps only the hash of each tag, Guerrilla's checksum
// (CRC-32 of the tag in lower case, no final xor; VintDocument.Hash in the builder), so a tag the campaign gives or one
// of Guerrilla's scripts sets as a text is looked up by it: Armageddon's string tables don't have Guerrilla's.
std::map<unsigned, std::string> g_guerrillaStrings;

unsigned TagHash(const char* tag) {
    unsigned crc = 0;
    for (const char* c = tag; *c; ++c) {
        crc ^= static_cast<unsigned char>(tolower(static_cast<unsigned char>(*c)));
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0u);
    }
    return crc;
}

// Guerrilla's text for its string tag, or "" (none given, or not one of its strings).
std::string GuerrillaText(const char* tag) {
    if (!tag || !*tag) return "";
    if (const auto byTag = g_hudMessageTexts.find(tag); byTag != g_hudMessageTexts.end()) return byTag->second;
    const auto found = g_guerrillaStrings.find(TagHash(tag));
    return found != g_guerrillaStrings.end() ? found->second : "";
}

// A text as the builder wrote it: "\n" for a new line.
std::string Unescaped(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n') {
            out += '\n';
            ++i;
        } else {
            out += text[i];
        }
    }
    return out;
}

void ReadHudMessages() {
    FILE* file = nullptr;
    if (_wfopen_s(&file, (rfa::RootDir() + L"\\mods\\guerrilla_hud\\hud_messages.txt").c_str(), L"r") != 0 || !file) {
        rfa::Log("No mods\\guerrilla_hud\\hud_messages.txt (editor\\GuerrillaBuilder.exe build hud makes it): the campaign's "
                 "messages show only their tool tips");
        return;
    }
    char line[4096];  // Guerrilla's longest strings (its menus' descriptions) are a few hundred characters
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == ';') continue;
        std::vector<std::string> fields(1);
        for (const char* c = line; *c && *c != '\n' && *c != '\r'; ++c) {
            if (*c == '|') fields.emplace_back();
            else fields.back() += *c;
        }
        if (fields[0] == "text" && fields.size() >= 3) g_hudMessageTexts[fields[1]] = Unescaped(fields[2]);
        else if (fields[0] == "hash" && fields.size() >= 3)
            g_guerrillaStrings[static_cast<unsigned>(strtoul(fields[1].c_str(), nullptr, 16))] = Unescaped(fields[2]);
    }
    fclose(file);
    rfa::Log("The campaign's messages: %zu texts; Guerrilla's strings: %zu", g_hudMessageTexts.size(), g_guerrillaStrings.size());
}

// Guerrilla's text with the number in place of its first %ls, %s, %d or %i (as Guerrilla's HUD formatted it).
std::string WithNumber(const std::string& text, int number) {
    for (const char* mark : {"%ls", "%s", "%d", "%i"}) {
        const size_t at = text.find(mark);
        if (at != std::string::npos) return text.substr(0, at) + std::to_string(number) + text.substr(at + strlen(mark));
    }
    return text;
}

void FollowCampaignMessages() {
    if (!guerrilla_campaign::Running()) return;
    for (int taken = 0; taken < kCampaignMessagesAFrame; ++taken) {
        const char* tag = nullptr;
        const char* icon = nullptr;
        int kind = 0, ms = 0;
        if (!GuerrillaCampaign_NextMessage(&tag, &icon, &kind, &ms)) return;
        if (!tag) continue;
        if (kind == kCampaignMessageTip) {
            const bool queued = tool_tips::Add(tag, ms);
            RFA_LOG_DETAIL_FIRST(40, "The campaign's messages: tool tip %s (wait %d ms)%s", tag, ms, queued ? "" : ", not queued");
            continue;
        }
        const std::string text = GuerrillaText(tag);
        if (text.empty()) {
            RFA_LOG_FIRST(20, "The campaign's messages: no text for %s (not among Guerrilla's strings in hud_messages.txt); not shown", tag);
            continue;
        }
        const std::string shown = kind == kCampaignMessageCount ? WithNumber(text, ms) : text;
        ShowMessage(shown, false);
        RFA_LOG_DETAIL_FIRST(40, "The campaign's messages: %s%s%s: \"%s\"%s%s", tag, kind == kCampaignMessageCount ? " with " : "",
                      kind == kCampaignMessageCount ? std::to_string(ms).c_str() : "", shown.c_str(),
                      icon ? ", icon " : "", icon ? icon : "");
    }
}

void ForgetPickups() {
    g_pickups.known = false;
    g_pickups.quietUntil = 0;
}

void AddPickups(std::vector<Pickup> found, ULONGLONG now) {
    if (found.empty()) return;
    if (found.size() > static_cast<size_t>(kPickupRows)) found.resize(kPickupRows);
    // hud_ui_add_item_pickup_message: one thing picked up again while its row shows adds to it.
    if (found.size() == 1 && g_pickups.rows.size() == 1 && now - g_pickups.shownAt < kPickupMergeMs &&
        found[0].picture == g_pickups.rows[0].picture) {
        g_pickups.rows[0].amount += found[0].amount;
        g_pickups.rows[0].total = found[0].total;
        g_pickups.merged = true;
    } else {
        g_pickups.rows = found;
        g_pickups.merged = false;
    }
    g_pickups.shownAt = now;
    ++g_pickups.version;
    for (const Pickup& p : found) RFA_LOG_DETAIL_FIRST(60, "Picked up: %s %+d%s", p.picture.c_str(), p.amount, p.salvage ? " (salvage)" : "");
}

// Each HUD update in our world: what was picked up since the last (salvage, weapons, rounds).
void FollowPickups(ULONGLONG now) {
    BYTE* player = g_player;
    if (!player || g_shop.open || CabinetIsOpen() || HandbookIsOpen() || g_mapOpen) {
        g_pickups.known = false;
        g_pickups.quietUntil = now + kPickupQuietAfterScreenMs;
        return;
    }
    const BYTE* entries[kInventorySlots] = {};
    int rounds[kInventorySlots] = {};
    for (int slot = 0; slot < kInventorySlots; ++slot) {
        BYTE* weapon = nullptr;
        entries[slot] = RecordEntry(RecordInSlot(player, slot), &weapon);
        rounds[slot] = weapon ? RoundsOf(player, weapon) : 0;
    }
    int salvage = -1;
    if (!GetSalvage(&salvage)) salvage = -1;
    const BYTE* inHand = RecordInHand(player, nullptr, 0);
    const BYTE* onItsWay = RecordOnItsWay(player, nullptr, 0);
    if (inHand != g_pickups.inHand || onItsWay != g_pickups.onItsWay) g_pickups.handsChangedAt = now;
    g_pickups.inHand = inHand;
    g_pickups.onItsWay = onItsWay;
    const bool handsSettled = now - g_pickups.handsChangedAt >= kPickupHandsQuietMs;
    if (!g_pickups.known && g_pickups.quietUntil == 0) g_pickups.quietUntil = now + kPickupQuietAfterLoadMs;
    std::vector<Pickup> found;
    if (g_pickups.known && now >= g_pickups.quietUntil) {
        if (salvage >= 0 && g_pickups.salvage >= 0 && salvage > g_pickups.salvage) {
            found.push_back({kSalvagePicture, salvage - g_pickups.salvage, salvage, true});
            tool_tips::Add("Salvage", 500);  // player_meta_data::add_salvage (RM 0xA11AB6), 500 ms on
        }
        for (int slot = 0; slot < kInventorySlots; ++slot) {
            const int w = CabinetWeaponOfEntry(entries[slot]);
            if (w < 0 || g_cabinetWeapons[w].picture.empty() || g_cabinetWeapons[w].sledgehammer) continue;
            int before = -1;  // where it was carried (a weapon moved to another slot isn't new)
            for (int s = 0; s < kInventorySlots; ++s)
                if (g_pickups.entries[s] == entries[slot]) before = s;
            if (before < 0) found.push_back({g_cabinetWeapons[w].picture, rounds[slot], 0, false});
            else if (rounds[slot] > g_pickups.rounds[before] && handsSettled)
                found.push_back({g_cabinetWeapons[w].picture, rounds[slot] - g_pickups.rounds[before], 0, false});
        }
    }
    for (int slot = 0; slot < kInventorySlots; ++slot) {
        g_pickups.entries[slot] = entries[slot];
        g_pickups.rounds[slot] = rounds[slot];
    }
    g_pickups.salvage = salvage;
    g_pickups.known = true;
    AddPickups(found, now);
}

// The guerrilla_campaign mod's use prompt at the start of one of Guerrilla's activities (Guerrilla's use_object_icon,
// 0 none), when it has one.
int CampaignUsePrompt() { return GuerrillaCampaign_UsePrompt(); }

bool OnFoot(const BYTE* player) {
    const DWORD vehicle = *reinterpret_cast<const DWORD*>(player + kHumanVehicle);
    return vehicle == 0xFFFFFFFF || vehicle == 0;
}

// What Guerrilla's prompts show now (see above).
UsePrompt CurrentUsePrompt() {
    UsePrompt prompt;
    BYTE* player = g_player;
    if (!player || !InOurWorld() || g_shop.open || CabinetIsOpen() || HandbookIsOpen() || g_mapOpen || !OnFoot(player)) return prompt;
    if (g_shop.nearStation >= 0) {
        prompt.useType = g_stations[g_shop.nearStation].shop ? kUseIconUpgrade : kUseIconDefault;
        return prompt;
    }
    const int activity = CampaignUsePrompt();
    if (activity >= 1 && activity <= kUseIconLast) {
        prompt.useType = activity;
        return prompt;
    }
    if (!g_usableItems) return prompt;
    const auto* action = reinterpret_cast<const UseActionRecord*>(player + kPlayerUseAction);
    if (!action->function) return prompt;
    if (action->icon >= 1 && action->icon <= kUseIconLast) {
        prompt.useType = action->icon;
        return prompt;
    }
    // A weapon on the ground, and no slot free: Guerrilla's swap, for its weapons. Anything else (a weapon of
    // Armageddon's, an icon past Guerrilla's) keeps Armageddon's own prompt, which shows the action's text.
    BYTE* ground = action->icon == kUseIconSwap ? ObjectFromHandle(kCabinetWorld, nullptr, action->handle) : nullptr;
    const int next = ground && static_cast<signed char>(ground[kObjectSubtype]) == kWeaponSubtype
                         ? CabinetWeaponOfEntry(*reinterpret_cast<const BYTE* const*>(ground + kWeaponEntry)) : -1;
    if (next < 0 || g_cabinetWeapons[next].picture.empty()) {
        prompt.armageddonOwn = action->text[0] != L'\0';
        return prompt;
    }
    prompt.swap = true;
    prompt.nextIcon = g_cabinetWeapons[next].picture;
    if (!g_cabinetWeapons[next].melee) prompt.nextAmmo = RoundsCarried(ground, nullptr, 0, true);
    BYTE* held = nullptr;
    const int current = CabinetWeaponOfEntry(RecordEntry(SwapRecord(player), &held));
    if (current >= 0 && held) {
        prompt.currentIcon = g_cabinetWeapons[current].picture;
        if (!g_cabinetWeapons[current].melee) prompt.currentAmmo = RoundsOf(player, held);
    }
    return prompt;
}

// For finding out which use actions Armageddon gives in our world: each new type and icon, once (the first 40).
void LogUseAction() {
    static std::set<long long> seen;
    BYTE* player = g_player;
    if (!player || !g_usableItems || seen.size() >= 40) return;
    const auto* action = reinterpret_cast<const UseActionRecord*>(player + kPlayerUseAction);
    if (!action->function) return;
    const long long key = (static_cast<long long>(action->type) << 32) | static_cast<unsigned>(action->icon);
    if (!seen.insert(key).second) return;
    BYTE* object = ObjectFromHandle(kCabinetWorld, nullptr, action->handle);
    char text[96] = {};
    WideCharToMultiByte(CP_UTF8, 0, action->text, -1, text, sizeof(text) - 1, nullptr, nullptr);
    rfa::LogDetail("Use action seen: type %d, icon %d, object type %d subtype %d, text \"%s\"", action->type, action->icon,
             object ? static_cast<signed char>(object[0x7A]) : -1, object ? static_cast<signed char>(object[kObjectSubtype]) : -1, text);
}

// The saving icon: while guerrilla_save makes a save, and 3 s of the HUD's time after it.
void FollowSaving(ULONGLONG hudMs) {
    g_savingLeftMs = g_savingLeftMs > hudMs ? g_savingLeftMs - hudMs : 0;
    if (!guerrilla_save::Running()) return;
    unsigned made = 0;
    const bool now = GuerrillaSave_Saving(&made) != 0;
    if (g_savesKnown && (now || made != g_savesMade)) g_savingLeftMs = kSavingAfterMs;
    g_savesMade = made;
    g_savesKnown = true;
}

void FollowMessages(ULONGLONG hudMs) {
    const ULONGLONG now = GetTickCount64();
    FollowPickups(now);
    LogUseAction();
    FollowSaving(hudMs);
}

// rfg_message_next() -> major, text, seconds: the next of our messages for Guerrilla's lines (its major line or its
//   minor line; seconds -1 for Guerrilla's 3), taken off the list; nothing when there's none.
int __cdecl LuaMessageNext(rfa::game::lua_State* L) {
    using namespace rfa::game;
    if (g_messages.empty()) return 0;
    const GuerrillaMessage message = g_messages.front();
    g_messages.pop_front();
    lua_pushboolean(L, message.major);
    lua_pushstring(L, message.text.c_str());
    lua_pushnumber(L, message.seconds);
    return 3;
}

// rfg_pickups() -> version, count, merged: the rows picked up now (version changes with each new set; merged: the
//   last change added to the one row shown). rfg_pickup(i) -> picture, amount, total, salvage: its i-th row.
int __cdecl LuaPickups(rfa::game::lua_State* L) {
    using namespace rfa::game;
    lua_pushnumber(L, g_pickups.version);
    lua_pushnumber(L, static_cast<double>(g_pickups.rows.size()));
    lua_pushboolean(L, g_pickups.merged);
    return 3;
}

int __cdecl LuaPickup(rfa::game::lua_State* L) {
    using namespace rfa::game;
    const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;
    if (index < 0 || index >= static_cast<int>(g_pickups.rows.size())) return 0;
    const Pickup& row = g_pickups.rows[index];
    lua_pushstring(L, row.picture.c_str());
    lua_pushnumber(L, row.amount);
    lua_pushnumber(L, row.total);
    lua_pushboolean(L, row.salvage);
    return 4;
}

// rfg_use_prompt() -> use_type, swap, current_icon, next_icon, current_ammo, next_ammo, armageddon_own
//   Guerrilla's use prompt (its picture, Guerrilla's use_object_icon 1-10; 0 none) or weapon swap now (the held
//   weapon's picture and rounds, the one on the ground's; "" and -1 where there's none); armageddon_own: the player's
//   use action is one Guerrilla's prompts have no picture for, which Armageddon's own prompt shows (else it's hidden:
//   Guerrilla's replace it, and Guerrilla showed none while a pickup row was up).
int __cdecl LuaUsePrompt(rfa::game::lua_State* L) {
    using namespace rfa::game;
    const UsePrompt prompt = CurrentUsePrompt();
    lua_pushnumber(L, prompt.useType);
    lua_pushboolean(L, prompt.swap);
    lua_pushstring(L, prompt.currentIcon.c_str());
    lua_pushstring(L, prompt.nextIcon.c_str());
    lua_pushnumber(L, prompt.currentAmmo);
    lua_pushnumber(L, prompt.nextAmmo);
    lua_pushboolean(L, prompt.armageddonOwn);
    return 7;
}

// rfg_saving() -> whether Guerrilla's saving icon shows now.
int __cdecl LuaSaving(rfa::game::lua_State* L) {
    rfa::game::lua_pushboolean(L, g_savingLeftMs > 0);
    return 1;
}

// ---- What the documents' scripts ask (Lua, on the game's thread)

using rfa::game::lua_State;

// rfg_station_prompt() -> near, kind
//   Whether the player is at one of Guerrilla's safehouse stations now (on foot, within reach): its kind,
//   "shop" (an upgrade shop) or "cabinet" (a weapon locker). For Guerrilla's use prompt.
int __cdecl LuaStationPrompt(lua_State* L) {
    using namespace rfa::game;
    const bool atStation = !g_shop.open && !CabinetIsOpen() && g_shop.nearStation >= 0;
    lua_pushboolean(L, atStation);
    lua_pushstring(L, atStation && g_stations[g_shop.nearStation].shop ? "shop" : "cabinet");
    return 2;
}

// rfg_ammo_box_prompt() -> near
//   Whether the player's use action now is one of Guerrilla's ammo boxes (see "Guerrilla's use prompt at its
//   ammo boxes" above). For Guerrilla's use prompt.
int __cdecl LuaAmmoBoxPrompt(lua_State* L) {
    using namespace rfa::game;
    bool atBox = false;
    if (g_usableItems && g_player && InOurWorld() && !g_shop.open && !CabinetIsOpen()) {
        const auto* action = reinterpret_cast<const UseActionRecord*>(g_player + kPlayerUseAction);
        atBox = action->type == kActionUseAmmoBox && action->function != nullptr;
    }
    lua_pushboolean(L, atBox);
    return 1;
}

// rfg_shop() -> open, count, highlighted, top, salvage, status
//   The upgrade shop: whether it's open; how many upgrades it lists; the one picked and the first row
//   shown (1 to the count); the player's salvage; the last purchase's outcome ("" after a while).
int __cdecl LuaShop(lua_State* L) {
    using namespace rfa::game;
    int salvage = 0;
    if (!GetSalvage(&salvage)) salvage = 0;
    lua_pushboolean(L, g_shop.open);
    lua_pushnumber(L, static_cast<double>(g_shop.list.size()));
    lua_pushnumber(L, g_shop.highlighted + 1);
    lua_pushnumber(L, g_shop.top + 1);
    lua_pushnumber(L, salvage);
    lua_pushstring(L, GetTickCount64() < g_shop.statusUntil ? g_shop.status.c_str() : "");
    return 6;
}

// rfg_shop_item(i) -> name, shown, type, level_name, level_text, bought, levels, cost, available
//   The i-th upgrade the shop lists (guerrilla_gear's): its name, as shown, its kind; its next level's
//   name and description ("" when all are bought); levels bought and in all; the next one's cost; whether it
//   can be bought now. Nothing for an i out of range.
int __cdecl LuaShopItem(lua_State* L) {
    using namespace rfa::game;
    const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;
    if (index < 0 || index >= static_cast<int>(g_shop.list.size())) return 0;
    const GuerrillaGearUpgrade& u = g_shop.list[index];
    lua_pushstring(L, u.name);
    lua_pushstring(L, u.shown);
    lua_pushstring(L, u.type);
    lua_pushstring(L, u.levelName);
    lua_pushstring(L, u.levelText);
    lua_pushnumber(L, u.bought);
    lua_pushnumber(L, u.levels);
    lua_pushnumber(L, u.cost);
    lua_pushboolean(L, u.available != 0);
    return 9;
}

// rfg_shop_text(what) -> Guerrilla's word for it on the upgrade screen (safehouses.txt): title, price,
//   credits, select, back, locked, use; "" if unknown.
int __cdecl LuaShopText(lua_State* L) {
    using namespace rfa::game;
    const char* what = lua_tolstring(L, 1, nullptr);
    auto found = what ? g_shopTexts.find(what) : g_shopTexts.end();
    lua_pushstring(L, found != g_shopTexts.end() ? found->second.c_str() : "");
    return 1;
}

// rfg_map_view() -> active, map_x, map_y, heading, speed, column, row, district, map_open
//   active: in Guerrilla's world, with a player; map_x, map_y: the player on Guerrilla's world map
//   picture, in its pixels (0-1024); heading: which way the camera looks (radians, 0 north, π/2
//   east); speed: the player's (m/s, smoothed); column, row: the zone the player is in; district:
//   its name ("" if unknown); map_open: whether the world map is open.
int __cdecl LuaMapView(lua_State* L) {
    using namespace rfa::game;
    lua_pushboolean(L, g_view.active);
    lua_pushnumber(L, g_view.mapX);
    lua_pushnumber(L, g_view.mapY);
    lua_pushnumber(L, g_view.heading);
    lua_pushnumber(L, g_view.speed);
    lua_pushnumber(L, g_view.column);
    lua_pushnumber(L, g_view.row);
    lua_pushstring(L, g_view.district.c_str());
    lua_pushboolean(L, g_mapOpen);
    return 9;
}

// rfg_map_tiles() -> count, column, row, column, row, ...
//   The zones around the player whose own pictures are loaded (rfg_terr01_<column>_<row>_map).
int __cdecl LuaMapTiles(lua_State* L) {
    using namespace rfa::game;
    if (!g_view.active) {
        lua_pushnumber(L, 0);
        return 1;
    }
    int zones[9][2];
    int count = 0;
    for (int dc = -1; dc <= 1; ++dc)
        for (int dr = -1; dr <= 1; ++dr) {
            const int c = g_view.column + dc, r = g_view.row + dr;
            if (c < kFirstZone || r < kFirstZone || c >= kFirstZone + kZonesAcross || r >= kFirstZone + kZonesAcross) continue;
            if (!HasTilePicture(c, r) || !ZoneReady(c, r)) continue;
            zones[count][0] = c;
            zones[count][1] = r;
            ++count;
        }
    lua_pushnumber(L, count);
    for (int i = 0; i < count; ++i) {
        lua_pushnumber(L, zones[i][0]);
        lua_pushnumber(L, zones[i][1]);
    }
    return 1 + 2 * count;
}

// rfg_map_icon_count() -> how many icons and places map_icons.txt has.
int __cdecl LuaMapIconCount(lua_State* L) {
    rfa::game::lua_pushnumber(L, static_cast<double>(g_mapIcons.size()));
    return 1;
}

// rfg_map_icon(i) -> kind, picture, map_x, map_y, caption, description, on_world_map, minimap_rule
//   The i-th line (1 to the count) of map_icons.txt: kind "icon" (its picture, rfg_ui_map_icon_...),
//   "place" (a named place, its name in caption), "player" (the player's icon: its picture and texts)
//   or "text" (a hint of the world map: what it is in caption, its text in description); where it is
//   on Guerrilla's world map picture, in its pixels (as rfg_map_view's map_x, map_y); what the world
//   map's highlight box says of it; whether the world map shows it (the minimap shows every icon).
//   Nothing for an i out of range.
int __cdecl LuaMapIcon(lua_State* L) {
    using namespace rfa::game;
    const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;
    if (index < 0 || index >= static_cast<int>(g_mapIcons.size())) return 0;
    const MapIcon& icon = g_mapIcons[index];
    lua_pushstring(L, icon.kind.c_str());
    lua_pushstring(L, icon.picture.c_str());
    lua_pushnumber(L, icon.mapX);
    lua_pushnumber(L, icon.mapY);
    lua_pushstring(L, icon.caption.c_str());
    lua_pushstring(L, icon.description.c_str());
    lua_pushboolean(L, icon.onWorldMap);
    lua_pushstring(L, icon.rule.c_str());
    return 8;
}

// rfg_map_radio_tags(reach) -> count, and rfg_map_radio_tag(i) -> map_x, map_y
//   Guerrilla's radio tags the minimap shows (cartographer::collect_bftp_nodes, RM 0x86D450): those not found yet
//   within `reach` metres of the player (its zoom radius), on Guerrilla's world map picture in its pixels.
std::vector<std::pair<double, double>> g_tagsInReach;

int __cdecl LuaMapRadioTags(lua_State* L) {
    using namespace rfa::game;
    g_tagsInReach.clear();
    BYTE* player = g_player;
    const double reach = lua_tonumber(L, 1);
    if (player && InOurWorld()) {
        const float* at = reinterpret_cast<const float*>(player + 4);
        for (const RadioTag& node : g_radioTags) {
            if (!node.tag || g_tagsFound.count(node.pair)) continue;
            const double dx = node.x - at[0], dz = node.z - at[2];
            if (dx * dx + dz * dz > reach * reach) continue;
            double mapX = 0, mapY = 0;
            MapPixels(node.x, node.z, mapX, mapY);
            g_tagsInReach.push_back({mapX, mapY});
        }
    }
    lua_pushnumber(L, static_cast<double>(g_tagsInReach.size()));
    return 1;
}

int __cdecl LuaMapRadioTag(lua_State* L) {
    using namespace rfa::game;
    const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;
    if (index < 0 || index >= static_cast<int>(g_tagsInReach.size())) return 0;
    lua_pushnumber(L, g_tagsInReach[index].first);
    lua_pushnumber(L, g_tagsInReach[index].second);
    return 2;
}

// ---- Guerrilla's minimap: what the other parts give it
//
// Through their headers (campaign_api.h, world_api.h, gear_api.h, people_api.h); a part that is off gives nothing.
//   guerrilla_campaign: GuerrillaCampaign_Running (an activity running: Guerrilla hides the activities' starts, and the
//     important buildings aren't pinned to the rim), GuerrillaCampaign_ActivityDoneAt (an activity's start done: its icon
//     grey, Completed_icon_color);
//   guerrilla_world: GuerrillaWorld_Buildings (the buildings in a 250 m box, gathered once a second as Guerrilla's
//     collect_structures 0x872E80: the important EDF ones' icons, phigh / pmedium, and their health "NN%" for 3 s after
//     damage, render_bldg_icons 0x86F4D0, render_structures_post 0x86FF50);
//   guerrilla_gear: GuerrillaGear_PlacedExplosives (remote charges, mines, singularity bombs inside the radius:
//     collect_projectiles 0x86D000, render_projectiles 0x85FCC0);
//   guerrilla_people: GuerrillaPeople_MapMarkers (squads' icons, the player's allies' stars, morale's converts).
// (Their structs are in world_api.h, gear_api.h and people_api.h.)

// Whether an activity runs, and whether a mission does (the campaign's; neither without it).
void CampaignRunning(bool& activity, bool& mission) {
    int mode = 0, missionOn = 0;
    activity = GuerrillaCampaign_Running(&mode, &missionOn) != 0;
    mission = missionOn != 0;
}

// The activities' starts done, asked of the campaign once a second.
void FollowActivitiesDone() {
    static ULONGLONG askedAt = 0;
    const ULONGLONG now = GetTickCount64();
    if (!guerrilla_campaign::Running() || (askedAt != 0 && now - askedAt < 1000)) return;
    askedAt = now;
    for (MapIcon& icon : g_mapIcons)
        if (!icon.rule.empty() && icon.rule != "safehouse" && icon.rule != "mission") icon.done = GuerrillaCampaign_ActivityDoneAt(icon.worldX, icon.worldZ) != 0;
}

// The important EDF buildings in Guerrilla's 250 m box, gathered once a second.
struct MinimapBuilding {
    double mapX = 0, mapY = 0;
    int importance = 0;       // 2 medium, 3 high
    int health = 100;         // per cent
    int msSinceDamaged = -1;  // at the gathering (-1 never)
};
std::vector<MinimapBuilding> g_minimapBuildings;
ULONGLONG g_buildingsGatheredAt = 0;
constexpr float kBuildingsHalfBox = 125.0f;  // Guerrilla's 250 m box

void GatherBuildings() {
    const ULONGLONG now = GetTickCount64();
    if (!guerrilla_world::Running() || (g_buildingsGatheredAt != 0 && now - g_buildingsGatheredAt < 1000)) return;
    g_buildingsGatheredAt = now;
    g_minimapBuildings.clear();
    BYTE* player = g_player;
    if (!player) return;
    const float* at = reinterpret_cast<const float*>(player + 4);
    static std::vector<GuerrillaWorldBuilding> found(256);
    const int count = (std::min)(GuerrillaWorld_Buildings(at[0], at[2], kBuildingsHalfBox, found.data(), static_cast<int>(found.size())),
                                 static_cast<int>(found.size()));
    for (int i = 0; i < count; ++i) {
        const GuerrillaWorldBuilding& b = found[i];
        // get_structure_importance_icon (RM 0x862D30): a living EDF building of medium or high importance, not a piece.
        if (b.destroyed || b.team != 1 || b.importance < 2 || (b.parent != 0 && b.parent != 0xFFFFFFFF)) continue;
        MinimapBuilding m;
        MapPixels(b.position[0], b.position[2], m.mapX, m.mapY);
        m.importance = b.importance;
        const double broken = b.destroyedAt > 0 ? (std::min)(1.0, static_cast<double>(b.damage) / b.destroyedAt) : 0.0;
        m.health = static_cast<int>(100.0 * (1.0 - broken) + 0.5);
        m.msSinceDamaged = b.msSinceDamaged;
        g_minimapBuildings.push_back(m);
    }
}

// rfg_activity_running() -> activity, mission: whether an activity runs now, and a mission (the campaign's; false
//   without it).
int __cdecl LuaActivityRunning(lua_State* L) {
    using namespace rfa::game;
    bool activity = false, mission = false;
    CampaignRunning(activity, mission);
    lua_pushboolean(L, activity);
    lua_pushboolean(L, mission);
    return 2;
}

// rfg_map_icon_done(i) -> whether the i-th line of map_icons.txt is an activity's start that is done (its icon grey).
int __cdecl LuaMapIconDone(lua_State* L) {
    using namespace rfa::game;
    FollowActivitiesDone();
    const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;
    lua_pushboolean(L, index >= 0 && index < static_cast<int>(g_mapIcons.size()) && g_mapIcons[index].done);
    return 1;
}

// rfg_map_buildings() -> count, and rfg_map_building(i) -> map_x, map_y, importance (2 medium, 3 high), health (%),
//   ms since it was last damaged (-1 never): the important EDF buildings around the player (guerrilla_world's).
int __cdecl LuaMapBuildings(lua_State* L) {
    using namespace rfa::game;
    GatherBuildings();
    lua_pushnumber(L, static_cast<double>(g_minimapBuildings.size()));
    return 1;
}

int __cdecl LuaMapBuilding(lua_State* L) {
    using namespace rfa::game;
    const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;
    if (index < 0 || index >= static_cast<int>(g_minimapBuildings.size())) return 0;
    const MinimapBuilding& b = g_minimapBuildings[index];
    lua_pushnumber(L, b.mapX);
    lua_pushnumber(L, b.mapY);
    lua_pushnumber(L, b.importance);
    lua_pushnumber(L, b.health);
    lua_pushnumber(L, b.msSinceDamaged < 0 ? -1.0 : static_cast<double>(b.msSinceDamaged + (GetTickCount64() - g_buildingsGatheredAt)));
    return 5;
}

// rfg_charges() -> placed, loose: the player's remote charges out, and how many of them aren't stuck to anything
//   (Guerrilla's projectile_count_charges_proxy_mines RM 0xA719C0, as hud_ui_weapons_process_weapon_fire_info 0x8B31AE
//   used it: WeaponFire's charges, and its status = the charges - the player's stuck projectiles; those blink).
int __cdecl LuaCharges(lua_State* L) {
    using namespace rfa::game;
    int placed = 0, stuck = 0;
    if (guerrilla_gear::Running() && InOurWorld()) {
        static GuerrillaGearExplosive found[64];
        const int count = (std::min)(GuerrillaGear_PlacedExplosives(found, 64), 64);
        for (int i = 0; i < count; ++i) {
            if (!found[i].players) continue;
            if (found[i].kind == 0) ++placed;
            if (found[i].stuck) ++stuck;
        }
    }
    lua_pushnumber(L, placed);
    lua_pushnumber(L, placed > stuck ? placed - stuck : 0);
    return 2;
}

// rfg_charges_most() -> how many remote charges the player can have out: Guerrilla's upgrade_get_max_charges (RM
//   0x775F70): the remote_charge_count upgrade's value at the level bought (upgrades.txt: 2 to start, up to 11).
int __cdecl LuaChargesMost(lua_State* L) {
    using namespace rfa::game;
    int most = 0;
    for (const CabinetUpgrade& u : g_cabinetUpgrades)
        if (u.name == "remote_charge_count" && !u.values.empty()) {
            int bought = UpgradeBought(u.name);
            if (bought < 0) bought = 0;
            if (bought >= static_cast<int>(u.values.size())) bought = static_cast<int>(u.values.size()) - 1;
            most = u.values[bought];
        }
    lua_pushnumber(L, most);
    return 1;
}

// rfg_map_explosives(reach) -> count, and rfg_map_explosive(i) -> map_x, map_y, kind (0 charge, 1 mine, 2 singularity
//   bomb), stuck: the placed explosives within `reach` metres of the player (guerrilla_gear's; everyone's, as Guerrilla).
struct MinimapExplosive { double mapX, mapY; int kind; bool stuck; };
std::vector<MinimapExplosive> g_minimapExplosives;

int __cdecl LuaMapExplosives(lua_State* L) {
    using namespace rfa::game;
    g_minimapExplosives.clear();
    BYTE* player = g_player;
    const double reach = lua_tonumber(L, 1);
    if (guerrilla_gear::Running() && player && InOurWorld()) {
        static GuerrillaGearExplosive found[64];
        const int count = (std::min)(GuerrillaGear_PlacedExplosives(found, 64), 64);
        const float* at = reinterpret_cast<const float*>(player + 4);
        for (int i = 0; i < count; ++i) {
            const double dx = found[i].x - at[0], dz = found[i].z - at[2];
            if (dx * dx + dz * dz > reach * reach) continue;
            MinimapExplosive e{};
            MapPixels(found[i].x, found[i].z, e.mapX, e.mapY);
            e.kind = found[i].kind;
            e.stuck = found[i].stuck != 0;
            g_minimapExplosives.push_back(e);
        }
    }
    lua_pushnumber(L, static_cast<double>(g_minimapExplosives.size()));
    return 1;
}

int __cdecl LuaMapExplosive(lua_State* L) {
    using namespace rfa::game;
    const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;
    if (index < 0 || index >= static_cast<int>(g_minimapExplosives.size())) return 0;
    const MinimapExplosive& e = g_minimapExplosives[index];
    lua_pushnumber(L, e.mapX);
    lua_pushnumber(L, e.mapY);
    lua_pushnumber(L, e.kind);
    lua_pushboolean(L, e.stuck);
    return 4;
}

// rfg_map_markers() -> count, and rfg_map_marker(i) -> map_x, map_y, picture (rfg_...), placement (0 plain, 1 sticky,
//   2 closest sticky, 3 a convert's pop), r, g, b (-1 for the picture's own colour), age ms (-1 for none): the people's
//   map markers (guerrilla_people's).
struct MinimapMarker { double mapX, mapY; std::string picture; int placement; int r, g, b; int ageMs; };
std::vector<MinimapMarker> g_minimapMarkers;

int __cdecl LuaMapMarkers(lua_State* L) {
    using namespace rfa::game;
    g_minimapMarkers.clear();
    if (guerrilla_people::Running() && InOurWorld()) {
        static GuerrillaPeopleMarker found[64];
        const int count = (std::min)(GuerrillaPeople_MapMarkers(found, 64), 64);
        for (int i = 0; i < count; ++i) {
            if (!found[i].icon || !found[i].icon[0]) continue;
            MinimapMarker m{};
            MapPixels(found[i].x, found[i].z, m.mapX, m.mapY);
            m.picture = std::string("rfg_") + found[i].icon;
            m.placement = found[i].placement;
            const DWORD c = found[i].color;
            m.r = c ? static_cast<int>((c >> 16) & 0xFF) : -1;
            m.g = c ? static_cast<int>((c >> 8) & 0xFF) : -1;
            m.b = c ? static_cast<int>(c & 0xFF) : -1;
            m.ageMs = found[i].ageMs;
            g_minimapMarkers.push_back(m);
        }
    }
    lua_pushnumber(L, static_cast<double>(g_minimapMarkers.size()));
    return 1;
}

int __cdecl LuaMapMarker(lua_State* L) {
    using namespace rfa::game;
    const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;
    if (index < 0 || index >= static_cast<int>(g_minimapMarkers.size())) return 0;
    const MinimapMarker& m = g_minimapMarkers[index];
    lua_pushnumber(L, m.mapX);
    lua_pushnumber(L, m.mapY);
    lua_pushstring(L, m.picture.c_str());
    lua_pushnumber(L, m.placement);
    lua_pushnumber(L, m.r);
    lua_pushnumber(L, m.g);
    lua_pushnumber(L, m.b);
    lua_pushnumber(L, m.ageMs);
    return 8;
}

// ---- Guerrilla's control and morale meters, from the campaign's districts
//
// Guerrilla's HUD script shows its meters on its MoraleUpdate data item (both_morale_update), which its game code filled
// (hud_ui_process_morale_info RM 0x894AA0) from what its campaign gave as control and morale changed:
//   - hud_ui_change_control (0x8B2060; from gfm_change_edf_control 0x7B03F3): the district's control as shown
//     (gfm_get_edf_control_display_value 0x7AB620: whole points rounded down, 1 under 1 point, 0 at none), its percent
//     of the most (gfm_get_edf_control_percent), the change in whole points and in percent; the district's mission
//     counts (missions_get_hud_info 0x79E320: in the district, done, locked; one just done, ones just unlocked);
//   - hud_ui_change_morale (0x8B1FC0; from gfm_change_district_morale 0x7AB46C): morale's percent now and before (the
//     first change's "before" kept while one waits to show), the change in points. A district's morale shows only if
//     it is the one shown last or 5 s have gone since that was (0x165769C, timestamp 0x1388).
//   The script waits for one meter's animation to end before the other's starts (both stand in the same place).
// The guerrilla_campaign mod keeps the districts (GuerrillaCampaign_District(index, &info): control, morale, their most
// and before, when each last changed); each HUD update in our worlds they're compared with the last seen, and a change
// is handed to our script (rfg_district_change) as MoraleUpdate's values. Guerrilla's missions aren't ported: their
// counts are 0 (no mission slots on the control meter). Not while a level loads; the first look after one only notes
// where they stand.

constexpr int kMostDistricts = 16;
constexpr ULONGLONG kMoraleOtherDistrictMs = 5000;

struct DistrictSeen {
    int controlAt = -1, moraleAt = -1;
    bool liberated = false;
};

struct MeterUpdate {
    bool control = false, morale = false;
    int current = 0, delta = 0;                     // control as shown, its change in whole points
    float currentPct = 0.0f, deltaPct = 0.0f;       // 0..1
    float moralePct = 0.0f, moraleLastPct = 0.0f;   // 0..1
    float moraleChange = 0.0f;                      // points
    std::string district;
};

std::vector<DistrictSeen> g_districtsSeen;
bool g_districtsKnown = false;
MeterUpdate g_meterUpdate;
int g_moraleShownDistrict = -1;
ULONGLONG g_moraleShownAt = 0;

// gfm_get_edf_control_display_value: whole points rounded down; under 1 point but some, 1.
int ControlShown(float control) {
    if (control >= 1.0f) return static_cast<int>(floorf(control));
    return control > 0.0f ? 1 : 0;
}

void FollowDistricts() {
    if (!guerrilla_campaign::Running()) return;
    const bool known = g_districtsKnown;
    const ULONGLONG now = GetTickCount64();
    for (int i = 0; i < kMostDistricts; ++i) {
        GuerrillaCampaignDistrict info{};
        if (!GuerrillaCampaign_District(i, &info)) break;
        if (static_cast<int>(g_districtsSeen.size()) <= i) g_districtsSeen.resize(i + 1);
        DistrictSeen& seen = g_districtsSeen[i];
        std::string name = info.name ? info.name : "";
        if (const auto shown = g_districtShownNames.find(name); shown != g_districtShownNames.end()) name = shown->second;
        if (known && info.controlChangedAt >= 0 && info.controlChangedAt != seen.controlAt && info.controlMax > 0.0f) {
            MeterUpdate& u = g_meterUpdate;
            u.control = true;
            u.current = ControlShown(info.control);
            u.delta = static_cast<int>(info.control - info.controlBefore);
            u.currentPct = info.control / info.controlMax;
            u.deltaPct = (info.control - info.controlBefore) / info.controlMax;
            u.district = name;
            RFA_LOG_DETAIL_FIRST(40, "Guerrilla's meters: %s's control %d (%+d, %.0f%%)", name.c_str(), u.current, u.delta, u.currentPct * 100.0f);
        }
        if (known && info.moraleChangedAt >= 0 && info.moraleChangedAt != seen.moraleAt && info.moraleMax > 0.0f) {
            const bool sameDistrict = i == g_moraleShownDistrict;
            const bool waited = g_moraleShownAt == 0 || now - g_moraleShownAt >= kMoraleOtherDistrictMs;
            if (sameDistrict || waited) {
                if (waited) {
                    g_moraleShownDistrict = i;
                    g_moraleShownAt = now;
                }
                MeterUpdate& u = g_meterUpdate;
                if (!u.morale) u.moraleLastPct = info.moraleBefore / info.moraleMax;  // the first change's "before"
                u.morale = true;
                u.moralePct = info.morale / info.moraleMax;
                u.moraleChange = info.morale - info.moraleBefore;
                u.district = name;
                RFA_LOG_DETAIL_FIRST(40, "Guerrilla's meters: %s's morale %.0f%% (%+.0f)", name.c_str(), u.moralePct * 100.0f, u.moraleChange);
            } else {
                RFA_LOG_DETAIL_FIRST(20, "Guerrilla's meters: %s's morale change not shown (another district's shown under 5 s ago)", name.c_str());
            }
        }
        seen.controlAt = info.controlChangedAt;
        seen.moraleAt = info.moraleChangedAt;
        // district::liberate_district's screen (hud_ui_district_liberation_show, LUT_LIBERATION) as it's liberated.
        if (known && info.liberated && !seen.liberated) OpenLiberation(name, info.name ? info.name : "", info.controlMax, 0, "the campaign liberated it");
        seen.liberated = info.liberated != 0;
    }
    g_districtsKnown = true;
}

// A level load: the next look notes where the districts stand, showing nothing.
void ForgetDistricts() {
    g_districtsKnown = false;
    g_meterUpdate = MeterUpdate{};
}

// rfg_district_change() -> control_changed, control, delta, control_pct, delta_pct, morale_changed, morale_pct,
//   last_morale_pct, morale_change, district: a change for Guerrilla's meters (MoraleUpdate's values, percents 0..1),
//   taken; nothing when there's none.
int __cdecl LuaDistrictChange(lua_State* L) {
    using namespace rfa::game;
    if (!g_meterUpdate.control && !g_meterUpdate.morale) return 0;
    const MeterUpdate u = g_meterUpdate;
    g_meterUpdate = MeterUpdate{};
    lua_pushboolean(L, u.control);
    lua_pushnumber(L, u.current);
    lua_pushnumber(L, u.delta);
    lua_pushnumber(L, u.currentPct);
    lua_pushnumber(L, u.deltaPct);
    lua_pushboolean(L, u.morale);
    lua_pushnumber(L, u.moralePct);
    lua_pushnumber(L, u.moraleLastPct);
    lua_pushnumber(L, u.moraleChange);
    lua_pushstring(L, u.district.c_str());
    return 10;
}

// ---- Guerrilla's screens the campaign raises: a district liberated, and an activity's briefing and results
//
// The district-liberated screen (Guerrilla's own game state: hud_ui_district_liberation_show RM 0x856F80 with
// LUT_LIBERATION, from upgrade_unlock_from_liberation 0x790E20 as district::liberate_district liberates one; _enter
// 0x846FB0 blurs the screen, loads rfg_ui_district_liberated and puts the HUD's items away; _process_input 0x846910):
// its document's script runs it on four data items (DistrictLiberated: the district's name and MENU_LIBERATED;
// DistrictControl: shown, the control's number, the district's mission count; DistrictUpgrade: an upgrade unlocked;
// DistrictHints: the button and MENU_CONTINUE). Its steps, each on the continue button once the last step's animation
// has ended (its script calls the game's rfg_dist_lib_tween_end, which clears the game's "busy", 0x1660EEB):
//   0 the header (the name and LIBERATED); 1 the control's tally: the meter's bar empties (meter_fill_scale_twn_1) and
//   the number follows it from the most to 0 (_process_control_data 0x856AA0: (the bar's scale / (its start - end) + 1)
//   x the most); 2 the upgrades the liberation unlocked (up to 3: each level of an upgrade the district unlocks), one
//   a press; then it closes (the document unloaded, the HUD's items back).
// Here: opened when the campaign's district turns liberated (FollowDistricts), or by the console's rfg_liberated (to
// see it); the game paused while it's open, as Guerrilla's state did; the continue keys Enter, Space and the left
// button (Guerrilla's PC key picture for Enter, key_178, in its hint). Its unlocks: guerrilla_gear's
// GuerrillaGear_LiberationUnlocks for the district, asked as the screen opens (the console's test shows what its
// district would unlock). An animation's end not heard of in kScreenStepMostMs: presses are taken anyway, so a
// missing end can't hold the game.
//
// An activity's screens: GuerrillaCampaign_Activity(&ActivityInfo) gives what shows (1 the briefing, waiting for
// Accept or Cancel; 2 running: the corner panel; 3 the results, waiting for Close) with Guerrilla's string tags;
// GuerrillaCampaign_ActivityChoose(1 accept / 0 cancel; any value closes the results) answers. The briefing and the
// results are Guerrilla's rfg_ui_demolitions (DemolitionsInfo; its process_demolitions_data RM 0x844D50: the labels
// ACT_DM_RESULTS_GOAL_TIME / _YOU_TIME / _PAR_TIME / _BEST_TIME / _SALVAGE, times "%.02d:%.02d", salvage "+%i", the
// hints MENU_CANCEL and MENU_ACCEPT at the briefing, MENU_CLOSE at the results), shown here with the game paused; the
// keys only once its script lets them (its rfg_enable_button_input, as its animation ends): Enter accepts or closes,
// Esc (or Backspace) cancels, and Space or Esc close the results too. Once asked, the campaign waits for the answer:
// a missing document (or one that never lets the keys in, after kScreenStepMostMs) answers Accept / Close itself. The
// corner panel is rfg_ui_hud_missions' (its script, fed by ours).
//
// The campaign's map rings (GuerrillaCampaign_MapRings: an activity's play area, green, or red while the 5 s to
// return count): Guerrilla's decals (add_decal 0x873F80: ui_map_icon_circ_fill_outline scaled to the radius), drawn on
// the minimap by rfg_ui_hud.
// (Their structs, GuerrillaCampaignActivity and GuerrillaCampaignMapRing, are in campaign_api.h.)

constexpr ULONGLONG kScreenStepMostMs = 10000;
constexpr int kActivityBriefing = 1, kActivityRunning = 2, kActivityResults = 3;
constexpr int kMostMapRings = 8;
const char* const kEnterKeyPicture = "rfg_key_178";   // Guerrilla's PC key pictures (ui_bms_pc_buttons): Enter
const char* const kEscKeyPicture = "rfg_key_124";     // and Esc

struct LiberationScreen {
    bool open = false, paused = false;
    std::string district;      // as shown
    float controlMax = 0.0f;
    int missions = 0;
    int step = 0;              // 0 the header, 1 the control's tally, 2 the unlocks
    bool busy = true;          // a step's animation plays: presses wait for its end
    ULONGLONG busySince = 0;
    int unlocks = 0, unlock = 0;
    std::string unlockName[3], unlockTitle[3];  // the upgrades unlocked: upgrades.xtbl's name, the level's name
    unsigned version = 0;
    bool enterDown = true, spaceDown = true, clickDown = true;
    ULONGLONG lastStep = 0;
};
LiberationScreen g_liberation;

struct PendingLiberation {
    bool wanted = false;
    std::string district, key;  // as shown; Guerrilla's name
    float controlMax = 0.0f;
} g_liberationPending;      // the console's: opened at the next HUD update

struct ActivityScreen {
    bool asked = false;        // the campaign has a screen up (we asked: it waits for our answer)
    GuerrillaCampaignActivity info{};
    std::string title, subtitle, body, objective, timer, result, failure, weapon[4];
    int screen = 0;            // what the campaign shows: 0 none, 1 briefing, 2 running, 3 results
    bool open = false, paused = false;  // our briefing or results screen
    int answered = 0;          // the screen we answered (the campaign may still give it until its next frame)
    bool input = false;        // its script lets the keys in
    ULONGLONG openedAt = 0;
    unsigned version = 0;
    bool enterDown = true, escDown = true, spaceDown = true;
    ULONGLONG lastStep = 0;
};
ActivityScreen g_activity;
std::vector<GuerrillaCampaignMapRing> g_mapRings;

bool LiberationIsOpen() { return g_liberation.open; }
bool ActivityScreenOpen() { return g_activity.open; }

void CloseOtherScreens(const char* why) {
    if (g_mapOpen) CloseMap(why);
    CloseShop(why);
    CloseCabinet(why, false);
}

void OpenLiberation(const std::string& district, const std::string& key, float controlMax, int missions, const char* why) {
    LiberationScreen& s = g_liberation;
    if (s.open) return;
    CloseOtherScreens("the district-liberated screen");
    const unsigned version = s.version;
    s = LiberationScreen{};
    s.version = version + 1;
    s.open = true;
    s.district = district;
    s.controlMax = controlMax;
    s.missions = missions;
    // What the liberation unlocked (upgrade_unlock_from_liberation's list, three at most), from guerrilla_gear.
    GuerrillaGearUnlock list[3] = {};
    for (GuerrillaGearUnlock& one : list) one.size = sizeof(GuerrillaGearUnlock);
    s.unlocks = GuerrillaGear_LiberationUnlocks(key.c_str(), list, 3);
    if (s.unlocks < 0 || s.unlocks > 3) s.unlocks = 0;
    for (int i = 0; i < s.unlocks; ++i) {
        s.unlockName[i] = list[i].name;
        s.unlockTitle[i] = list[i].levelName;
    }
    s.busySince = GetTickCount64();
    PauseGame(1, 1, 0, 0);
    s.paused = true;
    rfa::Log("District-liberated screen: %s (%s); the game paused", district.c_str(), why);
}

void CloseLiberation(const char* why) {
    LiberationScreen& s = g_liberation;
    if (!s.open) return;
    s.open = false;
    if (s.paused) {
        UnpauseGame(1, 1);
        s.paused = false;
    }
    g_shop.useDown = true;
    rfa::LogDetail("District-liberated screen closed (%s)", why);
}

// _process_input: the next step (LUT_LIBERATION: the header, the control's tally, each unlock), or the end.
void LiberationNext() {
    LiberationScreen& s = g_liberation;
    if (s.step == 0) {
        s.step = 1;
    } else if (s.step == 1) {
        if (s.unlocks == 0) {
            CloseLiberation("its last step");
            return;
        }
        s.step = 2;
        s.unlock = 0;
    } else if (++s.unlock >= s.unlocks) {
        CloseLiberation("its last unlock");
        return;
    }
    s.busy = true;
    s.busySince = GetTickCount64();
    ++s.version;
    RFA_LOG_DETAIL_FIRST(20, "District-liberated screen: step %d", s.step);
}

void StepLiberation() {
    LiberationScreen& s = g_liberation;
    const ULONGLONG now = GetTickCount64();
    const bool resumed = now - s.lastStep > kHudGoneAfterMs;
    s.lastStep = now;
    if (!GameFocused() || resumed) {
        s.enterDown = s.spaceDown = s.clickDown = true;
        return;
    }
    if (s.busy && now - s.busySince > kScreenStepMostMs) {
        s.busy = false;
        rfa::Log("District-liberated screen: step %d's animation never told its end; the keys are taken anyway", s.step);
    }
    const bool enter = Pressed(VK_RETURN, s.enterDown);
    const bool space = Pressed(VK_SPACE, s.spaceDown);
    const bool click = Pressed(VK_LBUTTON, s.clickDown);
    if ((enter || space || click) && !s.busy) LiberationNext();
}

// The campaign's ActivityInfo kept, its texts copied (valid only until the next call).
void KeepActivity(const GuerrillaCampaignActivity& info) {
    ActivityScreen& s = g_activity;
    s.info = info;
    s.title = info.title ? info.title : "";
    s.subtitle = info.subtitle ? info.subtitle : "";
    s.body = info.body ? info.body : "";
    s.objective = info.objective ? info.objective : "";
    s.timer = info.timer ? info.timer : "";
    s.result = info.result ? info.result : "";
    s.failure = info.failure ? info.failure : "";
    for (int i = 0; i < 4; ++i) s.weapon[i] = info.weapon[i] ? info.weapon[i] : "";
}

void OpenActivityScreen(int screen) {
    ActivityScreen& s = g_activity;
    CloseOtherScreens("an activity's screen");
    s.open = true;
    s.input = false;
    s.openedAt = GetTickCount64();
    s.enterDown = s.escDown = s.spaceDown = true;
    ++s.version;
    if (!s.paused) {
        PauseGame(1, 1, 0, 0);
        s.paused = true;
    }
    rfa::Log("Activity screen: the %s of %s; the game paused", screen == kActivityBriefing ? "briefing" : "results", s.title.c_str());
}

void CloseActivityScreen(const char* why) {
    ActivityScreen& s = g_activity;
    if (!s.open) return;
    s.open = false;
    if (s.paused) {
        UnpauseGame(1, 1);
        s.paused = false;
    }
    g_shop.useDown = true;
    rfa::LogDetail("Activity screen closed (%s)", why);
}

void AnswerActivity(int accept, const char* why) {
    ActivityScreen& s = g_activity;
    GuerrillaCampaign_ActivityChoose(accept);
    s.answered = s.screen;
    rfa::LogDetail("Activity screen: %s (%s)", s.screen == kActivityBriefing ? (accept ? "Accept" : "Cancel") : "Close", why);
    CloseActivityScreen(why);
}

void StepActivityScreen() {
    ActivityScreen& s = g_activity;
    const ULONGLONG now = GetTickCount64();
    const bool resumed = now - s.lastStep > kHudGoneAfterMs;
    s.lastStep = now;
    if (!GameFocused() || resumed) {
        s.enterDown = s.escDown = s.spaceDown = true;
        return;
    }
    if (!s.input && now - s.openedAt > kScreenStepMostMs) {
        s.input = true;
        rfa::Log("Activity screen: its script never let the keys in; they're taken anyway");
    }
    const bool enter = Pressed(VK_RETURN, s.enterDown);
    const bool esc = Pressed(VK_ESCAPE, s.escDown) || HeldDown(VK_BACK);
    const bool space = Pressed(VK_SPACE, s.spaceDown);
    if (!s.input) return;
    if (s.screen == kActivityBriefing) {
        if (enter) AnswerActivity(1, "Enter");
        else if (esc) {
            g_full.swallowPauseUntil = GetTickCount64() + kSwallowPauseMs;
            AnswerActivity(0, "Esc");
        }
    } else if (enter || esc || space) {
        if (esc) g_full.swallowPauseUntil = GetTickCount64() + kSwallowPauseMs;
        AnswerActivity(1, enter ? "Enter" : esc ? "Esc" : "Space");
    }
}

// ---- Guerrilla's activity-complete screen: a Transporter's or a Collateral Damage's results
//
// Guerrilla's activities raised it as they ended: hud_ui_activity_complete_start RM 0x846670(mode, success), from
// delivery's do_end_processing (Guerrilla Express, shown as Transporter), vehicle rail's determine_reward_and_end (Riding
// Shotgun, shown as Collateral Damage), and the raids', house arrest's and area defense's ends (never demolitions
// master, convoys or couriers). The activity's mode gave the screen's (AC) number (VEHICLE_RAIL 1 -> 5, DELIVERY 4 -> 4;
// RAID_DEFEND 8 -> 1, RAID_ATTACK 9 -> 9, HOUSE_ARREST 10 -> 2, AREA_DEFENSE 16 -> 8), and a failure the tag of the game
// log's reason (gamelog_complete_reasons 2 MENU_YOU_DIED, 4 ACTIVITY_FAIL_OUT_OF_TIME, 5 _AWAY_FROM_VEH, 6 _VEH_DESTROYED,
// 7 _VEH_ESCAPED, 8 _LEFT_AREA, 9 _OUT_OF_AMMO, 10 _HOSTAGES_DEAD, 11 _BUILDING_DESTROYED, 12 _DAMAGE; none for the
// others). Its game state came up at the next frame the game wasn't paused (_do_frame 0x846620), loaded the number's
// document (cUI_activity_complete_doc 0x1660F60: rfg_ui_activity_complete for 4 and 5 alone; the raids, house arrest and
// area defense show Guerrilla's mission-complete screen, rfg_ui_mission_complete, not ported), hid the HUD, and once its
// document was in read the activity's numbers (_process 0x856530): guerrilla_express_load_info 0x846230 from
// activity_guerrilla_express_get_score_results 0x76C900 (the pro and target times, the best, 0 for none, and yours, in
// ms), riding_shotgun_load_info 0x846110 from activity_riding_shotgun_get_score_results 0x75AC10 (the goal, the
// soldiers', structures' and vehicles' damage, the score: their sum to the hundredth, the colonists' deaths: the penalty,
// and the total, in millions). Then each frame its data items: ActivityComplete (process_activity_information 0x845B80:
// the number, complete, the title (ACT_GEXPRESS_NAME, ACT_SHOTGUN_NAME), the failure's tag, and two hints: the first
// MENU_CONTINUE, or MENU_RETRY after a failure (both kinds have a replay then, 0x12FB268); the second offered only after a
// failure (MENU_EXIT) or a Transporter won (MENU_RETRY: its replay on a success, 0x12FB290); their key pictures only once
// its script lets the buttons in, rfg_enable_button_input 0x1660DC5; hide_overall only for Collateral Damage in some of
// its languages, not English) and ActivityTallys (process_activity_tallys 0x856000: seven texts, the goal, then each of
// its three tallies' numbers in turn; Transporter's "%i:%.02i.%.02i" (minutes, seconds, hundredths) from the second,
// Collateral Damage's "%.2f"). Its script starts each tally as its block fades in (rfg_start_activity_complete(1 to 3):
// _start_tally 0x846880), whose numbers then count from 0 to theirs in 1.5 s while the buttons are out; once they're in,
// every number is shown as it is. Then the first hint's key (Btn_menu_select, or a click on the hint) or the second's
// closed it (check_for_user_input_complete 0x845F00): the first replays a failed activity, or ends it (Collateral
// Damage's after a fade to black); the second replays a Transporter won, or ends a failed one; then a dead player's
// world loads its last save again, and a success shows the upgrades it unlocked.
// Here: asked for by GuerrillaHud_ActivityComplete (the end of the file: what guerrilla_campaign calls as one of these
// two kinds ends, once one is ported), or by the console's rfg_activity_complete (to see it), and opened at the next HUD
// update the game isn't paused nor another of our screens up; the game paused while it's open, as Guerrilla's state
// did; the hints' keys Enter, and Esc (or Backspace) for the second (Guerrilla's PC key pictures key_178, key_124 in
// them). The player's choice goes to guerrilla_campaign's GuerrillaCampaign_ActivityCompleteChoose(choice), if it has
// it: 1 the first hint (a failure: replay it; else end it), 2 the second (a Transporter won: replay it; a failure: end
// it); the campaign does the rest (the fade, the reload, the upgrades). Not here: the blur, the music's fade, a click
// on a hint, and its sounds (HUD_OBJECTIVE_HEADING, the tally's money loop: the interface's sound banks are full).

// What an activity gives the screen as it ends (GuerrillaHud_ActivityComplete): Guerrilla's
// hud_ui_activity_complete_start(mode, success), the game log's reason for a failure, and the numbers its kind's loader
// read.
struct ActivityResults {
    int size;           // sizeof(ActivityResults), 44
    int mode;           // Guerrilla's activity mode: 1 vehicle rail (Collateral Damage), 4 delivery (Transporter)
    int success;        // 1 won, 0 failed
    int reason;         // a failure's gamelog_complete_reasons (2 died, 4 out of time, ... 12 not enough damage), else 0
    float numbers[7];   // Transporter: the pro, target, best (0 for none) and your times, in ms; Collateral Damage:
                        // the goal, soldiers, structures, vehicles, score, deaths and total, in millions
};
static_assert(sizeof(ActivityResults) == 44, "GuerrillaHud_ActivityComplete's results are 44 bytes");

constexpr int kAcTransporter = 4, kAcCollateralDamage = 5;  // the screen's numbers of the two kinds it shows
constexpr int kAcNoChoice = 0, kAcFirstHint = 1, kAcSecondHint = 2;
constexpr float kAcTallySeconds = 1.5f;                     // a tally's count up (process_activity_tallys)
const char* const kActivityCompleteDocument = "rfg_ui_activity_complete";

struct ActivityCompleteTally {  // one of process_activity_tallys' three (0x2B9DB3C, 0x28 bytes each)
    int count = 0;              // its numbers, at most 4
    float target[4] = {};
    float progress = -1.0f;     // the seconds it has counted, -1 before its script starts it
};

struct ActivityCompleteScreen {
    bool pending = false;       // asked for, not open yet
    ActivityResults results{};
    bool test = false;          // the console's: no one is told the choice
    std::string why;
    bool open = false, paused = false;
    int ac = 0;                 // the screen's number: 4 Transporter, 5 Collateral Damage
    bool success = false;
    const char* title = "";     // Guerrilla's tags
    const char* failure = nullptr;
    bool goalShown = false;     // Collateral Damage's goal (the first number, not counted)
    float goal = 0.0f;
    ActivityCompleteTally tallies[3];
    bool input = false;         // its script lets the keys in (rfg_enable_button_input)
    ULONGLONG openedAt = 0, lastStep = 0;
    unsigned version = 0;       // changes as it opens and as the keys come in (its hints show then)
    bool enterDown = true, escDown = true;
};
ActivityCompleteScreen g_activityComplete;

bool ActivityCompleteOpen() { return g_activityComplete.open; }

// The screen's number for Guerrilla's activity mode (hud_ui_activity_complete_start's table), 0 for one it doesn't show
// (the raids, house arrest and area defense: Guerrilla's mission-complete screen, not ported).
int ActivityCompleteNumber(int mode) {
    if (mode == 1) return kAcCollateralDamage;  // ACTIVITY_MODE_VEHICLE_RAIL
    if (mode == 4) return kAcTransporter;       // ACTIVITY_MODE_DELIVERY
    return 0;
}

// A failure's tag for the game log's reason (hud_ui_activity_complete_start's table), null for none.
const char* ActivityFailureTag(int reason) {
    static const char* const tags[] = {"MENU_YOU_DIED", nullptr, "ACTIVITY_FAIL_OUT_OF_TIME", "ACTIVITY_FAIL_AWAY_FROM_VEH",
                                       "ACTIVITY_FAIL_VEH_DESTROYED", "ACTIVITY_FAIL_VEH_ESCAPED", "ACTIVITY_FAIL_LEFT_AREA",
                                       "ACTIVITY_FAIL_OUT_OF_AMMO", "ACTIVITY_FAIL_HOSTAGES_DEAD", "ACTIVITY_FAIL_BUILDING_DESTROYED",
                                       "ACTIVITY_FAIL_DAMAGE"};
    return reason >= 2 && reason <= 12 ? tags[reason - 2] : nullptr;
}

// The second hint: offered after a failure (exit) and after a Transporter won (its replay on a success: retry).
bool ActivityCompleteRetriesWon(const ActivityCompleteScreen& s) { return s.success && s.ac == kAcTransporter; }
bool ActivityCompleteSecondHint(const ActivityCompleteScreen& s) { return !s.success || ActivityCompleteRetriesWon(s); }

// Asks for the screen: false while one is up or waiting, or for a mode it doesn't show (hud_ui_activity_complete_start's
// answer).
bool AskActivityComplete(const ActivityResults& results, bool test, const char* why) {
    ActivityCompleteScreen& s = g_activityComplete;
    if (s.pending || s.open) {
        rfa::Log("Activity-complete screen: asked for again while one is up (%s); not shown", why);
        return false;
    }
    if (ActivityCompleteNumber(results.mode) == 0) {
        rfa::Log("Activity-complete screen: Guerrilla's activity mode %d isn't one it shows (%s)", results.mode, why);
        return false;
    }
    s.pending = true;
    s.results = results;
    s.test = test;
    s.why = why;
    return true;
}

// Guerrilla's _enter and its kind's loader: the title, the failure, the goal and the tallies.
void OpenActivityComplete() {
    ActivityCompleteScreen& s = g_activityComplete;
    const ActivityResults& r = s.results;
    s.pending = false;
    CloseOtherScreens("the activity-complete screen");
    s.open = true;
    s.ac = ActivityCompleteNumber(r.mode);
    s.success = r.success != 0;
    s.failure = s.success ? nullptr : ActivityFailureTag(r.reason);
    for (ActivityCompleteTally& tally : s.tallies) tally = ActivityCompleteTally{};
    if (s.ac == kAcTransporter) {
        // guerrilla_express_load_info: no goal; the pro, target and best times; yours.
        s.title = "ACT_GEXPRESS_NAME";
        s.goalShown = false;
        s.goal = 0.0f;
        s.tallies[0].count = 3;
        for (int i = 0; i < 3; ++i) s.tallies[0].target[i] = r.numbers[i];
        s.tallies[1].count = 1;
        s.tallies[1].target[0] = r.numbers[3];
    } else {
        // riding_shotgun_load_info: the goal; the soldiers', structures' and vehicles' damage and the score; the deaths;
        // the total.
        s.title = "ACT_SHOTGUN_NAME";
        s.goalShown = true;
        s.goal = r.numbers[0];
        s.tallies[0].count = 4;
        for (int i = 0; i < 4; ++i) s.tallies[0].target[i] = r.numbers[1 + i];
        s.tallies[1].count = 1;
        s.tallies[1].target[0] = r.numbers[5];
        s.tallies[2].count = 1;
        s.tallies[2].target[0] = r.numbers[6];
    }
    s.input = false;
    s.openedAt = GetTickCount64();
    s.enterDown = s.escDown = true;
    ++s.version;
    if (!s.paused) {
        PauseGame(1, 1, 0, 0);
        s.paused = true;
    }
    rfa::Log("Activity-complete screen: %s %s%s%s (%s); the game paused", GuerrillaText(s.title).c_str(), s.success ? "complete" : "failed",
             s.failure ? ", " : "", s.failure ? s.failure : "", s.why.c_str());
}

// Closed: the choice (a hint, or none) to the campaign, unless it's the console's.
void CloseActivityComplete(int choice, const char* why) {
    ActivityCompleteScreen& s = g_activityComplete;
    if (!s.open) return;
    s.open = false;
    if (s.paused) {
        UnpauseGame(1, 1);
        s.paused = false;
    }
    g_shop.useDown = true;
    rfa::LogDetail("Activity-complete screen closed: choice %d (%s)", choice, why);
    // The choice is for guerrilla_campaign's GuerrillaCampaign_ActivityCompleteChoose(choice), which the campaign hasn't
    // yet: once campaign_api.h has it, call it here when choice != kAcNoChoice and the screen isn't the console's test.
}

// Each HUD update while it's open: the tallies counting, then its keys, once its script lets them in.
void StepActivityComplete() {
    ActivityCompleteScreen& s = g_activityComplete;
    if (!s.input)
        for (ActivityCompleteTally& tally : s.tallies)
            if (tally.progress >= 0.0f)
                tally.progress = (std::min)(tally.progress + static_cast<float>(g_hudFrameMs) / 1000.0f, kAcTallySeconds);
    const Document* doc = FindDocument(kActivityCompleteDocument);
    if (!doc || doc->missing) {
        CloseActivityComplete(kAcFirstHint, "no rfg_ui_activity_complete document");
        return;
    }
    const ULONGLONG now = GetTickCount64();
    const bool resumed = now - s.lastStep > kHudGoneAfterMs;
    s.lastStep = now;
    if (!GameFocused() || resumed) {
        s.enterDown = s.escDown = true;
        return;
    }
    if (!s.input && now - s.openedAt > kScreenStepMostMs) {
        s.input = true;
        ++s.version;
        rfa::Log("Activity-complete screen: its script never let the keys in; they're taken anyway");
    }
    const bool enter = Pressed(VK_RETURN, s.enterDown);
    const bool esc = Pressed(VK_ESCAPE, s.escDown) || HeldDown(VK_BACK);
    if (!s.input) return;
    if (enter) {
        CloseActivityComplete(kAcFirstHint, "Enter");
    } else if (esc && ActivityCompleteSecondHint(s)) {
        g_full.swallowPauseUntil = now + kSwallowPauseMs;
        CloseActivityComplete(kAcSecondHint, "Esc");
    }
}

// Each HUD update in our worlds: the campaign's activity and rings asked for, and the console's liberated screen.
void FollowCampaignScreens() {
    if (g_liberationPending.wanted) {
        g_liberationPending.wanted = false;
        OpenLiberation(g_liberationPending.district, g_liberationPending.key, g_liberationPending.controlMax, 0, "the console's rfg_liberated");
    }
    // The activity-complete screen asked for comes up once the game isn't paused (Guerrilla's _do_frame), with none of
    // our screens nor the loader's menus or the console up.
    if (g_activityComplete.pending && !g_mapOpen && !OurScreenOpen() && !MenusShown() && !GamePausedNow()) OpenActivityComplete();
    ActivityScreen& s = g_activity;
    // Only with the campaign: once asked, it waits for our answer.
    if (guerrilla_campaign::Running()) {
        GuerrillaCampaignActivity info{};
        const int screen = GuerrillaCampaign_Activity(&info) ? info.screen : 0;
        if (screen != 0) KeepActivity(info);
        if (screen != s.screen) {
            RFA_LOG_DETAIL_FIRST(40, "Activity screen: the campaign shows %d (mode %d, %s)", screen, info.mode, info.title ? info.title : "-");
            s.answered = 0;
            ++s.version;
        }
        s.screen = screen;
        const bool asks = (screen == kActivityBriefing || screen == kActivityResults) && s.answered != screen;
        const Document* doc = FindDocument("rfg_ui_demolitions");
        if (asks && (!doc || doc->missing)) {
            AnswerActivity(1, "no rfg_ui_demolitions document");
        } else if (asks && !s.open) {
            OpenActivityScreen(screen);
        } else if (!asks && s.open) {
            CloseActivityScreen("the campaign's screen ended");
        }
    }
    g_mapRings.clear();
    if (guerrilla_campaign::Running()) {
        GuerrillaCampaignMapRing found[kMostMapRings]{};
        const int count = GuerrillaCampaign_MapRings(found, kMostMapRings);
        for (int i = 0; i < count && i < kMostMapRings; ++i) g_mapRings.push_back(found[i]);
    }
}

// Each HUD update while one is open: its keys.
void StepCampaignScreens() {
    if (g_liberation.open) StepLiberation();
    if (g_activity.open) StepActivityScreen();
    if (g_activityComplete.open) StepActivityComplete();
}

// A level load: all closed (an activity's screen answered as Guerrilla's would close: accept, close; the
// activity-complete screen with no choice: the campaign's activity goes with the level).
void CloseCampaignScreens(const char* why) {
    CloseLiberation(why);
    CloseActivityScreen(why);
    CloseActivityComplete(kAcNoChoice, why);
    g_activityComplete.pending = false;
    g_activity.screen = 0;
    g_activity.answered = 0;
    g_liberationPending.wanted = false;
    g_mapRings.clear();
}

// The console's rfg_liberated: the district-liberated screen of the player's district (Dust outside them), to see it;
// nothing changes. Registered as the campaign's commands are (the game's 0xC7BD70; type 3 runs the handler).
using RegisterCommandFn = void*(__fastcall*)(void* command, void* unused, const char* name, const char* description, int type, void (*handler)());
auto const RegisterCommand = reinterpret_cast<RegisterCommandFn>(0xC7BD70);
auto& g_consoleCommandRunning = *reinterpret_cast<int*>(0x28FF1C0);
using ConsolePrintFn = void(__cdecl*)(const char* format, ...);
auto const ConsolePrint = reinterpret_cast<ConsolePrintFn>(0xC7C440);
BYTE g_liberatedCommand[32];

void LiberatedCommand() {
    if (!g_consoleCommandRunning) return;
    GuerrillaCampaignDistrict info{};
    std::string name = "DUST", key = "Dust";
    float most = 380.0f;
    if ((GuerrillaCampaign_District(-1, &info) || GuerrillaCampaign_District(1, &info)) && info.name) {
        name = key = info.name;
        if (const auto shown = g_districtShownNames.find(name); shown != g_districtShownNames.end()) name = shown->second;
        most = info.controlMax;
    }
    g_liberationPending.wanted = true;
    g_liberationPending.district = name;
    g_liberationPending.key = key;
    g_liberationPending.controlMax = most;
    ConsolePrint("Guerrilla's district-liberated screen for %s (close the console; Enter, Space or a click goes on)\n", name.c_str());
}

// The console's rfg_activity_complete: Guerrilla's activity-complete screen with sample numbers, to see it; nothing
// changes, and no one is told the choice. rfg_activity_complete transporter|collateral [won|failed] [reason]: a
// Transporter's (Guerrilla Express; "express" too) or a Collateral Damage's (Riding Shotgun; "shotgun" too) results, won
// (the default) or failed, for a reason (died, time, left_vehicle, vehicle_destroyed, escaped, left_area, ammo, hostages,
// building, damage; by default Transporter's time, Collateral Damage's damage). The numbers are Guerrilla's own test
// numbers (its loaders' debug branch, never on in its game), Transporter's read as seconds (its times are in ms).
// Each call of 0xC7D570(kinds, 0) reads the command's next word into 0x28FF0B8 (as the campaign's commands read theirs).
using ParseArgumentsFn = void(__cdecl*)(int kinds, int flags);
auto const ParseArguments = reinterpret_cast<ParseArgumentsFn>(0xC7D570);
auto const g_commandWord = reinterpret_cast<const char*>(0x28FF0B8);
constexpr int kArgumentNone = 1, kArgumentWord = 2;
BYTE g_activityCompleteCommand[32];

struct FailureReasonWord {
    const char* word;
    int reason;  // gamelog_complete_reasons
};
const FailureReasonWord kFailureReasonWords[] = {{"died", 2},       {"time", 4},     {"left_vehicle", 5}, {"vehicle_destroyed", 6},
                                                 {"escaped", 7},    {"left_area", 8}, {"ammo", 9},         {"hostages", 10},
                                                 {"building", 11}, {"damage", 12}};

std::string NextCommandWord() {
    ParseArguments(kArgumentWord | kArgumentNone, 0);
    return g_commandWord;
}

void ActivityCompleteCommand() {
    if (!g_consoleCommandRunning) return;
    const std::string kind = NextCommandWord(), outcome = NextCommandWord(), reasonWord = NextCommandWord();
    const char* const usage = "rfg_activity_complete transporter|collateral [won|failed] [died|time|left_vehicle|vehicle_destroyed|"
                              "escaped|left_area|ammo|hostages|building|damage]\n";
    ActivityResults results{};
    results.size = sizeof(ActivityResults);
    const bool transporter = _stricmp(kind.c_str(), "transporter") == 0 || _stricmp(kind.c_str(), "express") == 0;
    const bool collateral = _stricmp(kind.c_str(), "collateral") == 0 || _stricmp(kind.c_str(), "shotgun") == 0;
    const bool failed = _stricmp(outcome.c_str(), "failed") == 0 || _stricmp(outcome.c_str(), "fail") == 0;
    if ((!transporter && !collateral) || (!failed && !outcome.empty() && _stricmp(outcome.c_str(), "won") != 0 &&
                                          _stricmp(outcome.c_str(), "success") != 0)) {
        ConsolePrint(usage);
        return;
    }
    if (!FindDocument(kActivityCompleteDocument)) {
        ConsolePrint("Guerrilla's activity-complete screen needs the HUD's map ([GuerrillaHud] Map=1)\n");
        return;
    }
    results.success = failed ? 0 : 1;
    if (transporter) {
        // guerrilla_express_load_info's test numbers (30, 100, 25.23, 46.75), as seconds: pro, target, best, yours.
        results.mode = 4;
        const float numbers[] = {30000.0f, 100000.0f, 25230.0f, 46750.0f};
        for (int i = 0; i < 4; ++i) results.numbers[i] = numbers[i];
        results.reason = 4;  // ACTIVITY_FAIL_OUT_OF_TIME
    } else {
        // riding_shotgun_load_info's test numbers: goal, soldiers, structures, vehicles, score, deaths, total.
        results.mode = 1;
        const float numbers[] = {15.5f, 1.25f, 10.8f, 3.53f, 14.0f, 0.85f, 16.7f};
        for (int i = 0; i < 7; ++i) results.numbers[i] = numbers[i];
        results.reason = 12;  // ACTIVITY_FAIL_DAMAGE
    }
    if (!reasonWord.empty()) {
        results.reason = -1;
        for (const FailureReasonWord& one : kFailureReasonWords)
            if (_stricmp(reasonWord.c_str(), one.word) == 0) results.reason = one.reason;
        if (results.reason < 0) {
            ConsolePrint(usage);
            return;
        }
    }
    if (!failed) results.reason = 0;
    if (!AskActivityComplete(results, true, "the console's rfg_activity_complete")) {
        ConsolePrint("Guerrilla's activity-complete screen is up already\n");
        return;
    }
    ConsolePrint("Guerrilla's activity-complete screen: %s %s (close the console; once it has counted up, Enter goes on%s)\n",
                 transporter ? "Transporter" : "Collateral Damage", failed ? "failed" : "won",
                 failed || transporter ? ", Esc too" : "");
}

void AddCampaignScreenCommands() {
    static bool added = false;
    if (added) return;
    added = true;
    RegisterCommand(g_liberatedCommand, nullptr, "rfg_liberated", "Shows Guerrilla's district-liberated screen for your district (to see it; nothing changes)", 3,
                    &LiberatedCommand);
    RegisterCommand(g_activityCompleteCommand, nullptr, "rfg_activity_complete",
                    "Shows Guerrilla's activity-complete screen with sample numbers (to see it; nothing changes): rfg_activity_complete "
                    "transporter|collateral [won|failed] [reason]",
                    3, &ActivityCompleteCommand);
}

// rfg_liberation() -> open, version, step, busy, district, label, control_most, missions, hint_key, hint_text,
//                     upgrade, upgrade_title, upgrade_label, upgrade_info
//   The district-liberated screen: whether it's open; a number that changes with each step; its step (0 the header,
//   1 the control's tally, 2 the unlocks); whether its animation still plays; the district's name; LIBERATED;
//   the control's most (the tally starts there); the district's missions; the continue key's picture and CONTINUE;
//   in step 2 the upgrade shown (upgrades.xtbl's name, the key of its pictures), the level's name, the tag
//   MENU_UNLOCKED and Guerrilla's TOOLTIP_UNLOCK_UPGRADES_DESC (hud_ui_district_liberation_process_unlock_data RM
//   0x846DF0 outside the DLC); nil while no unlock is shown.
int __cdecl LuaLiberation(lua_State* L) {
    using namespace rfa::game;
    const LiberationScreen& s = g_liberation;
    lua_pushboolean(L, s.open);
    lua_pushnumber(L, s.version);
    lua_pushnumber(L, s.step);
    lua_pushboolean(L, s.busy);
    lua_pushstring(L, s.district.c_str());
    lua_pushstring(L, GuerrillaText("MENU_LIBERATED").c_str());
    lua_pushnumber(L, s.controlMax);
    lua_pushnumber(L, s.missions);
    lua_pushstring(L, kEnterKeyPicture);
    lua_pushstring(L, GuerrillaText("MENU_CONTINUE").c_str());
    if (s.open && s.step == 2 && s.unlock >= 0 && s.unlock < s.unlocks) {
        lua_pushstring(L, s.unlockName[s.unlock].c_str());
        lua_pushstring(L, s.unlockTitle[s.unlock].c_str());
        lua_pushstring(L, "MENU_UNLOCKED");
        lua_pushstring(L, GuerrillaText("TOOLTIP_UNLOCK_UPGRADES_DESC").c_str());
    } else {
        for (int i = 0; i < 4; ++i) lua_pushnil(L);
    }
    return 14;
}

// rfg_liberation_step_done(): the step's animation has ended (Guerrilla's rfg_dist_lib_tween_end).
int __cdecl LuaLiberationStepDone(lua_State*) {
    g_liberation.busy = false;
    return 0;
}

// "mm:ss" (Guerrilla's "%.02d:%.02d") of whole seconds; "" for none (below 0).
std::string MinutesSeconds(int seconds) {
    if (seconds < 0) return "";
    char text[16];
    sprintf_s(text, "%02d:%02d", seconds / 60, seconds % 60);
    return text;
}

// A weapon's picture: its icon by its name in Guerrilla's weapons (cabinet.txt), or a picture's name as it is (a
// layer prop's: renamed rfg_...); "" for none.
std::string WeaponPicture(const std::string& name) {
    if (name.empty()) return "";
    for (const CabinetWeapon& w : g_cabinetWeapons)
        if (_stricmp(w.guerrilla.c_str(), name.c_str()) == 0) return w.picture;
    if (_strnicmp(name.c_str(), "rfg_", 4) == 0) return name;
    if (_strnicmp(name.c_str(), "ui_", 3) == 0) {
        std::string picture = "rfg_" + name;
        for (char& c : picture) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        return picture;
    }
    RFA_LOG_FIRST(10, "Activity screen: no picture for the weapon %s", name.c_str());
    return "";
}

// rfg_activity() -> screen, version, mode, title, subtitle, body, objective, timer, result, failure, limit, par, best,
//   elapsed, status, success, pro_time, out_of_range, salvage
//   The campaign's activity (screen 0 none, 1 the briefing, 2 running, 3 the results), its texts as Guerrilla's (from
//   its tags), its times "mm:ss" ("" for none: limit 0, no best yet); status 0, 1 unstable, 2 destroyed.
//   rfg_activity_weapon(i) -> picture, rounds: its i-th weapon (1-4), nothing past the last.
int __cdecl LuaActivity(lua_State* L) {
    using namespace rfa::game;
    const ActivityScreen& s = g_activity;
    const GuerrillaCampaignActivity& i = s.info;
    lua_pushnumber(L, s.screen);
    lua_pushnumber(L, s.version);
    lua_pushnumber(L, i.mode);
    lua_pushstring(L, GuerrillaText(s.title.c_str()).c_str());
    lua_pushstring(L, GuerrillaText(s.subtitle.c_str()).c_str());
    lua_pushstring(L, GuerrillaText(s.body.c_str()).c_str());
    lua_pushstring(L, GuerrillaText(s.objective.c_str()).c_str());
    lua_pushstring(L, s.timer.c_str());
    lua_pushstring(L, GuerrillaText(s.result.c_str()).c_str());
    lua_pushstring(L, GuerrillaText(s.failure.c_str()).c_str());
    lua_pushstring(L, MinutesSeconds(i.timeLimit > 0 ? i.timeLimit : -1).c_str());
    lua_pushstring(L, MinutesSeconds(i.parTime > 0 ? i.parTime : -1).c_str());
    lua_pushstring(L, MinutesSeconds(i.bestMs > 0 ? (i.bestMs + 999) / 1000 : -1).c_str());
    lua_pushstring(L, MinutesSeconds(i.elapsedMs >= 0 ? (i.elapsedMs + 999) / 1000 : -1).c_str());
    lua_pushnumber(L, i.status);
    lua_pushboolean(L, i.success != 0);
    lua_pushboolean(L, i.proTime != 0);
    lua_pushboolean(L, i.outOfRange != 0);
    lua_pushnumber(L, i.salvage);
    return 19;
}

int __cdecl LuaActivityWeapon(lua_State* L) {
    using namespace rfa::game;
    const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;
    if (index < 0 || index >= 4 || g_activity.weapon[index].empty()) return 0;
    const std::string picture = WeaponPicture(g_activity.weapon[index]);
    if (picture.empty()) return 0;
    lua_pushstring(L, picture.c_str());
    lua_pushnumber(L, g_activity.info.ammo[index]);
    return 2;
}

// rfg_screen_input(on): Guerrilla's rfg_enable_button_input, from the screen's script (off while its animation
//   plays): the activity-complete screen's while it's open (its hints come up with the keys), else the activity
//   screen's.
int __cdecl LuaScreenInput(lua_State* L) {
    using namespace rfa::game;
    const bool on = lua_toboolean(L, 1) != 0;
    if (g_activityComplete.open) {
        if (g_activityComplete.input != on) ++g_activityComplete.version;
        g_activityComplete.input = on;
        return 0;
    }
    g_activity.input = on;
    return 0;
}

// rfg_activity_complete() -> open, version, mode, complete, title, failure, hint_1_image, hint_1_text, hint_2_image,
//   hint_2_text, hide_overall
//   Guerrilla's activity-complete screen: whether it's open; a number that changes as it opens and as the keys come
//   in; then ActivityComplete's values as process_activity_information gave them: the screen's number (4 Transporter,
//   5 Collateral Damage), complete, Guerrilla's tags for the title, the failure (nil for none) and the hints' texts, the
//   hints' PC key pictures (nil until the keys are let in; the second's nil when it isn't offered), hide_overall.
int __cdecl LuaActivityComplete(lua_State* L) {
    using namespace rfa::game;
    const ActivityCompleteScreen& s = g_activityComplete;
    lua_pushboolean(L, s.open);
    lua_pushnumber(L, s.version);
    lua_pushnumber(L, s.ac);
    lua_pushboolean(L, s.success);
    lua_pushstring(L, s.title);
    if (s.failure) lua_pushstring(L, s.failure);
    else lua_pushnil(L);
    if (s.input) lua_pushstring(L, kEnterKeyPicture);
    else lua_pushnil(L);
    lua_pushstring(L, s.success ? "MENU_CONTINUE" : "MENU_RETRY");
    if (s.input && ActivityCompleteSecondHint(s)) lua_pushstring(L, kEscKeyPicture);
    else lua_pushnil(L);
    lua_pushstring(L, ActivityCompleteRetriesWon(s) ? "MENU_RETRY" : "MENU_EXIT");
    lua_pushboolean(L, false);  // Collateral Damage's only in some of Guerrilla's languages, not English
    return 11;
}

// A Transporter's time (ms) as process_activity_tallys wrote it: "%i:%.02i.%.02i", the minutes, seconds and hundredths
// (rounded, fl_round).
std::string TransporterTime(float ms) {
    const int seconds = static_cast<int>(ms / 1000.0f);
    const int hundredths = static_cast<int>(static_cast<float>(static_cast<int>(ms) % 1000) / 10.0f + 0.5f);
    char text[32];
    sprintf_s(text, "%i:%.02i.%.02i", seconds / 60, seconds % 60, hundredths);
    return text;
}

// rfg_activity_complete_tallys() -> goal, soldiers, structures, vehicles, score, deaths, total
//   ActivityTallys as process_activity_tallys wrote it: Collateral Damage's goal, then each tally's numbers in turn
//   (Transporter's times from the second), counted up: a tally its script started counts from 0 to its numbers in
//   1.5 s while the keys are still out; once they're in, every number is shown as it is. "" for none.
int __cdecl LuaActivityCompleteTallys(lua_State* L) {
    using namespace rfa::game;
    const ActivityCompleteScreen& s = g_activityComplete;
    std::string texts[7];
    char text[32];
    int at = 0;
    if (s.goalShown) {
        sprintf_s(text, "%.2f", s.goal);
        texts[at++] = text;
    } else if (s.ac == kAcTransporter) {
        at = 1;
    }
    for (const ActivityCompleteTally& tally : s.tallies)
        for (int i = 0; i < tally.count && at < 7; ++i) {
            float shown = 0.0f;
            if (s.input) shown = tally.target[i];
            else if (tally.progress >= 0.0f) shown = tally.target[i] * tally.progress / kAcTallySeconds;
            if (s.ac == kAcTransporter) {
                texts[at++] = TransporterTime(shown);
            } else {
                sprintf_s(text, "%.2f", shown);
                texts[at++] = text;
            }
        }
    for (const std::string& one : texts) lua_pushstring(L, one.c_str());
    return 7;
}

// rfg_activity_complete_tally(n): Guerrilla's rfg_start_activity_complete, from the screen's script as each block fades
//   in: tally n (1 to 3) starts counting up (_start_tally RM 0x846880; 0, its mission-complete screen's, starts nothing).
int __cdecl LuaActivityCompleteTally(lua_State* L) {
    using namespace rfa::game;
    const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;
    if (g_activityComplete.open && index >= 0 && index < 3) g_activityComplete.tallies[index].progress = 0.0f;
    return 0;
}

// rfg_guerrilla_text(tag) -> Guerrilla's text for one of its string tags, or nothing (not a tag of its strings).
int __cdecl LuaGuerrillaText(lua_State* L) {
    using namespace rfa::game;
    if (lua_type(L, 1) != 4) return 0;  // a string (lua_tolstring would turn a number into one in place)
    const char* tag = lua_tolstring(L, 1, nullptr);
    if (!tag || !*tag || strchr(tag, ' ')) return 0;
    const std::string text = GuerrillaText(tag);
    if (text.empty()) return 0;
    lua_pushstring(L, text.c_str());
    return 1;
}

// rfg_map_rings() -> how many of the campaign's rings show; rfg_map_ring(i) -> map_x, map_y, radius (world map
//   pixels), r, g, b, a (0-255), inverted.
int __cdecl LuaMapRings(lua_State* L) {
    using namespace rfa::game;
    lua_pushnumber(L, static_cast<double>(g_mapRings.size()));
    return 1;
}

int __cdecl LuaMapRing(lua_State* L) {
    using namespace rfa::game;
    const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;
    if (index < 0 || index >= static_cast<int>(g_mapRings.size())) return 0;
    const GuerrillaCampaignMapRing& ring = g_mapRings[index];
    double mapX = 0, mapY = 0;
    MapPixels(ring.x, ring.z, mapX, mapY);
    lua_pushnumber(L, mapX);
    lua_pushnumber(L, mapY);
    lua_pushnumber(L, ring.radius / kZoneMetres * kZonePixels);
    lua_pushnumber(L, ring.r);
    lua_pushnumber(L, ring.g);
    lua_pushnumber(L, ring.b);
    lua_pushnumber(L, ring.a);
    lua_pushboolean(L, ring.inverted != 0);
    return 8;
}

// rfg_map_icon_shown(i) -> whether the i-th line of map_icons.txt is shown now: a M.O.A.B. ("bomb") once
//   its radio tags are found, everything else always.
int __cdecl LuaMapIconShown(lua_State* L) {
    using namespace rfa::game;
    const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;
    bool shown = index >= 0 && index < static_cast<int>(g_mapIcons.size());
    if (shown && g_mapIcons[index].kind == "bomb") shown = g_bombsShown.count(g_mapIcons[index].pair) != 0;
    lua_pushboolean(L, shown);
    return 1;
}

// rfg_map_close(): closes the world map (its script's own close).
int __cdecl LuaMapClose(lua_State*) {
    CloseMap("its script");
    return 0;
}

// rfg_map_full() -> open, centre_x, centre_y, view, zoom_pct, district
//   The world map's view: open; the world map picture's pixel in the middle of the screen; how many of
//   its pixels the screen's height shows; Guerrilla's zoom (0 to 1); the district's name under the
//   middle ("" outside them). Steps the map's keys first (see StepFullMap).
int __cdecl LuaMapFull(lua_State* L) {
    using namespace rfa::game;
    StepFullMap(true);
    const int column = static_cast<int>(std::floor(g_full.centreX / kZonePixels)) + kFirstZone;
    const int row = static_cast<int>(std::floor(g_full.centreY / kZonePixels)) + kFirstZone;
    auto district = g_districts.find(ZoneKey(column, row));
    lua_pushboolean(L, g_mapOpen);
    lua_pushnumber(L, g_full.centreX);
    lua_pushnumber(L, g_full.centreY);
    lua_pushnumber(L, ViewPixels());
    lua_pushnumber(L, ZoomPercent());
    lua_pushstring(L, district != g_districts.end() ? district->second.c_str() : "");
    return 6;
}

// rfg_map_full_tiles() -> count; rfg_map_full_tile(i) -> column, row
//   The zones under the world map's view whose own pictures are loaded (rfg_terr01_<column>_<row>_map),
//   when it is zoomed in far enough to want them: how many (at most kFullMapTiles), then each (1 to the
//   count; a C function has room for 20 values only, so not all at once).
std::vector<std::pair<int, int>> g_fullMapTiles;

int __cdecl LuaMapFullTiles(lua_State* L) {
    using namespace rfa::game;
    g_fullMapTiles.clear();
    for (const auto& zone : FullMapZones())
        if (ZoneReady(zone.first, zone.second)) g_fullMapTiles.push_back(zone);
    lua_pushnumber(L, static_cast<double>(g_fullMapTiles.size()));
    return 1;
}

int __cdecl LuaMapFullTile(lua_State* L) {
    using namespace rfa::game;
    const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;
    if (index < 0 || index >= static_cast<int>(g_fullMapTiles.size())) return 0;
    lua_pushnumber(L, g_fullMapTiles[index].first);
    lua_pushnumber(L, g_fullMapTiles[index].second);
    return 2;
}

// ---- Guerrilla's data items
//
// Guerrilla's own HUD scripts (run as they are, from the player's copy: see GuerrillaHud.cs) read
// the data items Guerrilla's game filled (WeaponFire, ...), which Armageddon doesn't have. These
// functions let our scripts make such an item and fill it from Armageddon's own items, so that
// Guerrilla's script reads it as it read Guerrilla's: the engine's data items (the same as Armageddon's
// health_hud and the rest). An item is made once (0xC58910: its handle, name and the type of each
// field; Armageddon's own items are made at start-up by 0x57A240) and kept; a field is set with a
// value (a 16-byte variant, made by the game's constructor for its type) on the item the handle
// names (0xC581A0), and the engine tells the item's subscribers when one changes (0xC576E0 compares
// first), as Armageddon's HUD update (0x57DE80) sets health_hud.
using MakeItemFn = void*(__cdecl*)(DWORD* handle, const char* name, const int* types, int count, const void* defaults, int defaultsCount);
using FindItemFn = void*(__cdecl*)(DWORD* handle);
using SetFieldFn = void(__fastcall*)(void* item, void* unused, int index, const void* value);
using StringValueFn = void*(__fastcall*)(void* value, void* unused, const char* text);
using IntValueFn = void*(__fastcall*)(void* value, void* unused, int number);
using FloatValueFn = void*(__fastcall*)(void* value, void* unused, float number);
using ReleaseValueFn = void(__fastcall*)(void* value, void* unused);
auto const MakeItem = reinterpret_cast<MakeItemFn>(0xC58910);
auto const FindItem = reinterpret_cast<FindItemFn>(0xC581A0);
auto const SetField = reinterpret_cast<SetFieldFn>(0xC576E0);
auto const StringValue = reinterpret_cast<StringValueFn>(0x55C6E0);
auto const BoolValue = reinterpret_cast<IntValueFn>(0x55C740);
auto const FloatValue = reinterpret_cast<FloatValueFn>(0x55C710);
auto const IntValue = reinterpret_cast<IntValueFn>(0x5709D0);
auto const ReleaseValue = reinterpret_cast<ReleaseValueFn>(0xC54460);
constexpr int kMaxItemFields = 0x24;  // 0xC576E0 takes no field past this
constexpr size_t kValueSize = 16;

struct GuerrillaItem {
    std::string name;   // kept: the engine may keep the name's address
    std::string types;  // a letter a field: b(oolean), i(nteger), f(loat), s(tring)
    DWORD handle = 0;
};
std::map<std::string, GuerrillaItem> g_items;  // never taken away: the engine keeps its items

// The engine's code for a field's type, as Armageddon's own items give them (health_hud's: 4 a
// string, 5 a boolean, 3 a float, 1 an integer).
int FieldType(char letter) {
    switch (letter) {
    case 'b': return 5;
    case 'f': return 3;
    case 's': return 4;
    default: return 1;
    }
}

// rfg_dataitem_make(name, types): makes the data item `name` (once), its fields' types a letter each.
int __cdecl LuaDataItemMake(lua_State* L) {
    using namespace rfa::game;
    const char* name = lua_tolstring(L, 1, nullptr);
    const char* types = lua_tolstring(L, 2, nullptr);
    if (!name || !types) return 0;
    if (g_items.count(name)) return 0;
    GuerrillaItem& item = g_items[name];
    item.name = name;
    item.types = types;
    if (item.types.size() > kMaxItemFields) item.types.resize(kMaxItemFields);
    std::vector<int> codes;
    for (char letter : item.types) codes.push_back(FieldType(letter));
    // The engine holds 128 items (0x28D4B90, 0x26C bytes each; 0xC581A0 refuses a handle past them).
    if (!MakeItem(&item.handle, item.name.c_str(), codes.data(), static_cast<int>(codes.size()), nullptr, 0))
        rfa::Log("Could not make Guerrilla's data item %s: the engine's 128 data items may all be in use", name);
    else
        rfa::LogDetail("Made Guerrilla's data item %s (%u fields): handle 0x%08lX", name, static_cast<unsigned>(codes.size()), item.handle);
    return 0;
}

// rfg_dataitem_set(name, values...): sets the item's fields, in order, each as its type.
int __cdecl LuaDataItemSet(lua_State* L) {
    using namespace rfa::game;
    const char* name = lua_tolstring(L, 1, nullptr);
    auto found = name ? g_items.find(name) : g_items.end();
    if (found == g_items.end()) return 0;
    GuerrillaItem& item = found->second;
    void* object = FindItem(&item.handle);
    if (!object) return 0;
    const int given = lua_gettop(L) - 1;
    for (int i = 0; i < static_cast<int>(item.types.size()) && i < given; ++i) {
        alignas(8) BYTE value[kValueSize] = {};
        const int at = i + 2;
        switch (item.types[i]) {
        case 'b': BoolValue(value, nullptr, lua_toboolean(L, at)); break;
        case 'f': FloatValue(value, nullptr, static_cast<float>(lua_tonumber(L, at))); break;
        case 's': {
            const char* text = lua_tolstring(L, at, nullptr);
            StringValue(value, nullptr, text ? text : "");
            break;
        }
        default: IntValue(value, nullptr, static_cast<int>(lua_tonumber(L, at))); break;
        }
        SetField(object, nullptr, i, value);  // the engine copies the value (a string's text too)
        ReleaseValue(value, nullptr);
    }
    return 0;
}

// rfg_string_find(text, part) -> where part starts and ends in text (from 1), or nil: Lua's
// string.find for a plain part, for Guerrilla's scripts (the game's Lua has no string library).
int __cdecl LuaStringFind(lua_State* L) {
    using namespace rfa::game;
    const char* text = lua_tolstring(L, 1, nullptr);
    const char* part = lua_tolstring(L, 2, nullptr);
    const char* at = text && part ? strstr(text, part) : nullptr;
    if (!at) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, static_cast<double>(at - text + 1));
    lua_pushnumber(L, static_cast<double>(at - text + strlen(part)));
    return 2;
}

// The game's window: the process's visible top-level window with a client area.
BOOL CALLBACK FindGameWindow(HWND window, LPARAM found) {
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
    *reinterpret_cast<HWND*>(found) = window;
    return FALSE;
}

// rfg_get_screen_dim() -> width, height: the game window's size in pixels, as Guerrilla's gave it.
int __cdecl LuaScreenDim(lua_State* L) {
    using namespace rfa::game;
    HWND window = nullptr;
    EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&window));
    RECT client = {};
    if (window) GetClientRect(window, &client);
    lua_pushnumber(L, client.right - client.left);
    lua_pushnumber(L, client.bottom - client.top);
    return 2;
}

// ---- Guerrilla's minimap blips: people and vehicles
//
// Guerrilla's cartographer collects the people and vehicles within its minimap's reach every frame and draws
// each as a blip (collect_humans RM 0x86D140, collect_vehicles 0x86D590, render_humans 0x86D710,
// render_vehicles 0x86DED0). Its rules, as kept here:
//   Reach: the minimap's zoom radius (its zoom_radius_squared: the frame's edge, 40 m, more in a fast
//     vehicle), not out to the frame's corners.
//   People: never the player, the dead, the hidden, or anyone in a vehicle (the vehicle is the blip). Then
//   by how the player's side takes theirs (human_teams_get_disposition):
//     hostile: once the player owns the Personnel Detector (get_upgrade_value(0x11) > 0: "locate enemy EDF
//       on your minimap"), else only those human::render_on_minimap passes: in combat (is_in_combat), or
//       flagged always_show_on_minimap, or with minimap_flags; red, the Marauders purple;
//     allied (the Red Faction): never the player's squad; those flagged always_show_on_minimap or
//       activity_related; the rest only while the alert level is above green; yellow;
//     friendly (civilians): only a house arrest's hostages (activity_ha_hostage), and only with the
//       Personnel Detector;
//     unfriendly and the rest: never (Guerrilla's debug switch only).
//   Vehicles: every one but the player's own and wrecks, coloured by who is in it (its driver, else another
//     occupant): yellow the Red Faction's, red a hostile's (purple the Marauders'), grey the others and the
//     empty ones.
// The colours are Guerrilla's (get_human_color RM 0x862400, get_vehicle_color 0x862680); the script has them.
// "In combat" is Guerrilla's npc::is_in_combat (its AI knows of an enemy); here the person's combat target
// (0x880B70). Nothing of ours sets Guerrilla's flags yet (always_show_on_minimap, activity_related,
// minimap_flags, hostages, hidden), and the player has no squad (Armageddon's player_squad_* are empty): so
// the parts of the rule that need them are left out until a mod sets them. Its radar sweep (a line moving down the
// minimap, a ring growing on each person it passes, the blips fading behind it) is the script's (rfg_ui_hud.lua),
// by rfg_map_sweep_clock below.
void* const kWorldLists = reinterpret_cast<void*>(0x1870630);
using ListCountFn = int(__thiscall*)(void* world);
using ListAtFn = BYTE*(__thiscall*)(void* world, int index);
auto const HumanCount = reinterpret_cast<ListCountFn>(0x43A860);
auto const HumanAt = reinterpret_cast<ListAtFn>(0x43A8B0);
auto const VehicleCount = reinterpret_cast<ListCountFn>(0x477090);
auto const VehicleAt = reinterpret_cast<ListAtFn>(0x4770E0);
auto const HumanDisposition = reinterpret_cast<int(__cdecl*)(BYTE* human, BYTE* other)>(0x8A04D0);
auto const TeamDisposition = reinterpret_cast<int(__cdecl*)(int team, int other)>(0x8A0460);
auto const CombatTarget = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human)>(0x880B70);
auto const Driving = reinterpret_cast<bool(__cdecl*)(BYTE* human)>(0x8695D0);
constexpr DWORD kObjectOrientation = 0x10, kObjectHandle = 0x68, kHumanTeam = 0x13B4, kVehicleFlags = 0x4FB0;
constexpr DWORD kVehicleWreck = 1u << 10;  // Guerrilla's vehicle_flags m_corpse
constexpr DWORD kNoHandle = 0xFFFFFFFF;
constexpr int kTeamGuerrillas = 0, kTeamMarauders = 3;  // both games' numbers
constexpr int kDispositionHostile = 0, kDispositionUnfriendly = 1, kDispositionFriendly = 3, kDispositionAllied = 4;
constexpr int kBlipGrey = 0, kBlipYellow = 1, kBlipRed = 2, kBlipPurple = 3;  // the script's BLIP_COLOURS
constexpr size_t kMostBlips = 40;                                            // the script's BLIPS

struct Blip {
    bool vehicle = false;
    double mapX = 0, mapY = 0;  // on Guerrilla's world map picture, in its pixels
    double facing = 0;          // a vehicle's (radians, 0 north, π/2 east)
    int colour = kBlipGrey;
    double distanceSquared = 0;
};
std::vector<Blip> g_blips;

// Dead, as the game's objects answer it (the fifth virtual function, in both games).
bool ObjectIsDead(BYTE* object) {
    using IsDeadFn = bool(__thiscall*)(void* object);
    void** functions = *reinterpret_cast<void***>(object);
    return reinterpret_cast<IsDeadFn>(functions[4])(object);
}

// Guerrilla's alert level from the guerrilla_campaign mod: 0 green to 3 red; -1 while nothing runs it.
int AlertLevelNow() {
    float meter = 0.0f;
    return GuerrillaCampaign_Alert(&meter);
}

// Whether the player has bought Guerrilla's Personnel Detector (guerrilla_gear's upgrade "personnel"); asked
// once a second.
bool OwnsPersonnelDetector() {
    static ULONGLONG askedAt = 0;
    static bool owned = false;
    static int logged = -1;  // what the log said last: -1 nothing yet, 0 not owned, 1 owned, 2 guerrilla_gear not there
    const ULONGLONG now = GetTickCount64();
    if (askedAt != 0 && now - askedAt < 1000) return owned;
    askedAt = now;
    owned = false;
    bool found = false;
    if (GearReady()) {
        const int count = GuerrillaGear_UpgradeCount();
        for (int i = 0; i < count; ++i) {
            GuerrillaGearUpgrade upgrade{};
            upgrade.size = sizeof(upgrade);
            if (GuerrillaGear_Upgrade(i, &upgrade) && strcmp(upgrade.name, "personnel") == 0) {
                owned = upgrade.bought > 0;
                found = true;
                break;
            }
        }
    }
    const int state = !GearReady() ? 2 : owned ? 1 : 0;
    if (state != logged) {
        logged = state;
        rfa::Log("Minimap: the Personnel Detector is %s", state == 2 ? "unknown (guerrilla_gear's upgrades aren't there)"
                                                       : owned ? "owned: every hostile in reach shows"
                                                       : found ? "not bought: hostiles show only while in combat"
                                                               : "not among guerrilla_gear's upgrades");
    }
    return owned;
}

// The people and vehicles the minimap shows now, within reachMetres of the player (see above).
void CollectBlips(double reachMetres) {
    g_blips.clear();
    BYTE* player = g_player;
    if (!player || !InOurWorld()) return;
    const float* centre = reinterpret_cast<const float*>(player + 4);
    const double reachSquared = reachMetres * reachMetres;
    const int playerTeam = *reinterpret_cast<const int*>(player + kHumanTeam);
    const DWORD ownVehicle = *reinterpret_cast<const DWORD*>(player + kHumanVehicle);
    const bool detector = OwnsPersonnelDetector();
    const bool alerted = AlertLevelNow() > 0;

    auto add = [&](const BYTE* object, bool vehicle, int colour) {
        const float* position = reinterpret_cast<const float*>(object + 4);
        const double dx = position[0] - centre[0], dz = position[2] - centre[2];
        Blip blip;
        blip.distanceSquared = dx * dx + dz * dz;
        if (blip.distanceSquared > reachSquared) return;
        blip.vehicle = vehicle;
        blip.colour = colour;
        MapPixels(position[0], position[2], blip.mapX, blip.mapY);
        if (vehicle) {
            const float* forward = reinterpret_cast<const float*>(object + kObjectOrientation + 24);
            blip.facing = std::atan2(static_cast<double>(forward[0]), static_cast<double>(forward[2]));
        }
        g_blips.push_back(blip);
    };

    std::map<DWORD, int> teamIn;  // a vehicle's handle -> the team of who is in it
    std::set<DWORD> driven;       // the vehicles whose driver gave that team
    const int humans = HumanCount(kWorldLists);
    for (int i = 0; i < humans; ++i) {
        BYTE* human = HumanAt(kWorldLists, i);
        if (!human || human == player || ObjectIsDead(human)) continue;
        const int team = *reinterpret_cast<const int*>(human + kHumanTeam);
        const DWORD vehicle = *reinterpret_cast<const DWORD*>(human + kHumanVehicle);
        if (vehicle != kNoHandle && vehicle != 0) {
            if (Driving(human)) {
                teamIn[vehicle] = team;
                driven.insert(vehicle);
            } else if (!driven.count(vehicle)) {
                teamIn.emplace(vehicle, team);
            }
            continue;
        }
        const int disposition = HumanDisposition(player, human);
        if (disposition == kDispositionHostile) {
            if (detector || CombatTarget(human)) add(human, false, team == kTeamMarauders ? kBlipPurple : kBlipRed);
        } else if (disposition == kDispositionAllied) {
            if (alerted) add(human, false, kBlipYellow);
        }
    }

    const int vehicles = VehicleCount(kWorldLists);
    for (int i = 0; i < vehicles; ++i) {
        BYTE* vehicle = VehicleAt(kWorldLists, i);
        if (!vehicle || ObjectIsDead(vehicle)) continue;
        if (*reinterpret_cast<const DWORD*>(vehicle + kVehicleFlags) & kVehicleWreck) continue;
        const DWORD handle = *reinterpret_cast<const DWORD*>(vehicle + kObjectHandle);
        if (handle == ownVehicle) continue;
        int colour = kBlipGrey;
        const auto inIt = teamIn.find(handle);
        if (inIt != teamIn.end()) {
            const int team = inIt->second;
            const int disposition = TeamDisposition(playerTeam, team);
            if (disposition >= kDispositionFriendly) {
                if (team == kTeamGuerrillas) colour = kBlipYellow;
            } else if (disposition <= kDispositionUnfriendly) {
                colour = team == kTeamMarauders ? kBlipPurple : kBlipRed;
            }
        }
        add(vehicle, true, colour);
    }

    // More than the script can show: the nearest stay.
    if (g_blips.size() > kMostBlips) {
        std::sort(g_blips.begin(), g_blips.end(), [](const Blip& a, const Blip& b) { return a.distanceSquared < b.distanceSquared; });
        g_blips.resize(kMostBlips);
    }
}

// ---- Guerrilla's tool tips this mod raises (their rules: tool_tips.cpp)
//
// Where Guerrilla raised them (RM), and where this mod does:
//   - the weapon cabinet (hud_ui_weapon_cabinet_process 0x85B33C, _exit 0x85B1D9 / 0x85B20C): "Locker" or
//     "Ammo_Crate_Weapons" as it opens, taken away as it closes, then "Locker_Ammo" (a locker) or "Ammo_Boxes" (a
//     crate): OpenCabinet, CloseCabinet;
//   - an upgrade station's use prompt (hud_ui_process_use_action_info 0x8E9495): "upgrades", 100 ms on: the stations;
//   - salvage picked up (player_meta_data::add_salvage 0xA11AB6): "Salvage", 500 ms on: the pickups;
//   - a radio tag found (bftp_touch_tag 0xA8DBE7, 0xA8DC6F): "Blast_from_the_Past", or "MOAB_Located" with a M.O.A.B.'s
//     third: FollowRadioTags (Guerrilla's "Blast", from its collectable pickup, isn't raised: which pickup it was is
//     to check);
//   - getting into a vehicle, once seated (set_enter_complete 0xBB548A): "Driving", for a car or a tank (not a flyer or a
//     walker: a walker has its own, not yet; Guerrilla's "EDF_Turret" for a gunner's seat and "Mine_Pooper" for a mine
//     layer aren't told apart yet): here;
//   - the alert level changing to anything above green (change_alert_level 0x7A43CA; Guerrilla not in a safehouse or a
//     mission, not checked yet): "Yellow_Alert": here.
// The rest are raised by Guerrilla's mission and activity scripts (tool_tip_add), its campaign's systems (activities
// started and won, control and morale) and its gear: other mods, through GuerrillaHud_ToolTip.
constexpr BYTE kSubtypeAutomobile = 5;  // an object's subtype (+0x7B): 5 a car or tank, 6 a flyer, 8 a walker

void FollowToolTipTriggers() {
    BYTE* player = g_player;
    static DWORD enteredVehicle = kNoHandle;  // got into, not seated yet
    static DWORD lastVehicle = kNoHandle;
    const DWORD vehicle = player ? *reinterpret_cast<const DWORD*>(player + kHumanVehicle) : kNoHandle;
    if (vehicle != lastVehicle) {
        lastVehicle = vehicle;
        enteredVehicle = vehicle != 0 ? vehicle : kNoHandle;
    }
    if (enteredVehicle != kNoHandle && player && Driving(player)) {
        BYTE* object = ObjectFromHandle(kCabinetWorld, nullptr, enteredVehicle);
        if (object && object[kObjectSubtype] == kSubtypeAutomobile) tool_tips::Add("Driving");
        enteredVehicle = kNoHandle;
    }
    static int lastAlert = -1;
    const int alert = AlertLevelNow();
    if (alert != lastAlert) {
        if (lastAlert >= 0 && alert > 0) tool_tips::Add("Yellow_Alert");
        lastAlert = alert;
    }
}

// rfg_map_blips(reach) -> count
//   Collects the people and vehicles Guerrilla's minimap shows within `reach` metres of the player (its
//   rules: see "Guerrilla's minimap blips" above), for rfg_map_blip.
int __cdecl LuaMapBlips(lua_State* L) {
    using namespace rfa::game;
    CollectBlips(lua_tonumber(L, 1));
    lua_pushnumber(L, static_cast<double>(g_blips.size()));
    return 1;
}

// rfg_map_blip(i) -> vehicle, map_x, map_y, colour, facing
//   The i-th of them (from 1): whether it's a vehicle (else a person); where it is on Guerrilla's world map
//   picture, in its pixels; its colour (0 grey, 1 yellow, 2 red, 3 purple); a vehicle's facing (radians, 0
//   north, π/2 east). Nothing for an i out of range.
int __cdecl LuaMapBlip(lua_State* L) {
    using namespace rfa::game;
    const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;
    if (index < 0 || index >= static_cast<int>(g_blips.size())) return 0;
    const Blip& blip = g_blips[index];
    lua_pushboolean(L, blip.vehicle);
    lua_pushnumber(L, blip.mapX);
    lua_pushnumber(L, blip.mapY);
    lua_pushnumber(L, blip.colour);
    lua_pushnumber(L, blip.facing);
    return 5;
}

// rfg_map_sweep_clock() -> seconds: the clock Guerrilla's minimap radar sweep moves by. Guerrilla's
// render_scan_line (RM 0x863A60) moves its line by the real frame time (Frametime_real), and not while the game
// is paused (game_is_paused): so this is real time that stands still while Armageddon's game is paused
// (0x520260, its pause count above 0). The HUD's script runs while the game is paused, so it can't tell itself.
auto const GameIsPaused = reinterpret_cast<bool(__cdecl*)()>(0x520260);

int __cdecl LuaMapSweepClock(lua_State* L) {
    using namespace rfa::game;
    static ULONGLONG last = 0;
    static double seconds = 0.0;
    const ULONGLONG now = GetTickCount64();
    if (last != 0 && !GameIsPaused()) seconds += static_cast<double>(now - last) / 1000.0;
    last = now;
    lua_pushnumber(L, seconds);
    return 1;
}

// rfg_key_down(code) -> whether that key (a virtual-key code) is held now, and the game's window has the focus: for
//   the reticle's script, which tells a reload's start by Armageddon's reload key (R) as nothing in its HUD's data says.
int __cdecl LuaKeyDown(lua_State* L) {
    using namespace rfa::game;
    const int code = static_cast<int>(lua_tonumber(L, 1));
    lua_pushboolean(L, code > 0 && code < 256 && GameFocused() && (GetAsyncKeyState(code) & 0x8000) != 0);
    return 1;
}

// rfg_sin_cos(angle) -> sine, cosine (radians): the game's Lua has no math library, and the sweep needs where each
// blip is on the turned minimap.
int __cdecl LuaSinCos(lua_State* L) {
    using namespace rfa::game;
    const double angle = lua_tonumber(L, 1);
    lua_pushnumber(L, std::sin(angle));
    lua_pushnumber(L, std::cos(angle));
    return 2;
}

// rfg_alert() -> whether Guerrilla's alert level is running (the guerrilla_campaign mod runs it in this
// world), and what Guerrilla's HUD shows of it: 0 to 25 green, to 50 yellow, to 75 orange, above red
// (its AlertLevel data item's alert_level_value).
int __cdecl LuaAlert(lua_State* L) {
    using namespace rfa::game;
    float meter = 0.0f;
    const int level = GuerrillaCampaign_Alert(&meter);
    lua_pushboolean(L, level >= 0);
    lua_pushnumber(L, meter);
    return 2;
}

}  // namespace

void guerrilla_hud::RegisterLua(rfa::game::lua_State* L) {
    rfa::game::lua_register(L, "rfg_map_view", LuaMapView);
    rfa::game::lua_register(L, "rfg_map_tiles", LuaMapTiles);
    rfa::game::lua_register(L, "rfg_map_close", LuaMapClose);
    rfa::game::lua_register(L, "rfg_map_icon_count", LuaMapIconCount);
    rfa::game::lua_register(L, "rfg_map_icon", LuaMapIcon);
    rfa::game::lua_register(L, "rfg_map_icon_shown", LuaMapIconShown);
    rfa::game::lua_register(L, "rfg_map_full", LuaMapFull);
    rfa::game::lua_register(L, "rfg_map_full_tiles", LuaMapFullTiles);
    rfa::game::lua_register(L, "rfg_map_full_tile", LuaMapFullTile);
    rfa::game::lua_register(L, "rfg_station_prompt", LuaStationPrompt);
    rfa::game::lua_register(L, "rfg_ammo_box_prompt", LuaAmmoBoxPrompt);
    rfa::game::lua_register(L, "rfg_use_prompt", LuaUsePrompt);
    rfa::game::lua_register(L, "rfg_message_next", LuaMessageNext);
    rfa::game::lua_register(L, "rfg_pickups", LuaPickups);
    rfa::game::lua_register(L, "rfg_pickup", LuaPickup);
    rfa::game::lua_register(L, "rfg_saving", LuaSaving);
    rfa::game::lua_register(L, "rfg_cabinet", LuaCabinet);
    rfa::game::lua_register(L, "rfg_handbook_available", LuaHandbookAvailable);
    rfa::game::lua_register(L, "rfg_handbook_open", LuaHandbookOpen);
    rfa::game::lua_register(L, "rfg_handbook", LuaHandbook);
    tool_tips::Register(L);
    rfa::game::lua_register(L, "rfg_handbook_screen", LuaHandbookScreen);
    rfa::game::lua_register(L, "rfg_handbook_page_new", LuaHandbookPageNew);
    rfa::game::lua_register(L, "rfg_handbook_rows", LuaHandbookRows);
    rfa::game::lua_register(L, "rfg_handbook_row", LuaHandbookRow);
    rfa::game::lua_register(L, "rfg_handbook_entry", LuaHandbookEntry);
    rfa::game::lua_register(L, "rfg_handbook_picture_shown", LuaHandbookPictureShown);
    rfa::game::lua_register(L, "rfg_handbook_clock", LuaHandbookClock);
    rfa::game::lua_register(L, "rfg_cabinet_screen", LuaCabinetScreen);
    rfa::game::lua_register(L, "rfg_cabinet_info", LuaCabinetInfo);
    rfa::game::lua_register(L, "rfg_cabinet_icon", LuaCabinetIcon);
    rfa::game::lua_register(L, "rfg_cabinet_slot", LuaCabinetSlot);
    rfa::game::lua_register(L, "rfg_shop", LuaShop);
    rfa::game::lua_register(L, "rfg_shop_item", LuaShopItem);
    rfa::game::lua_register(L, "rfg_shop_text", LuaShopText);
    rfa::game::lua_register(L, "rfg_dataitem_make", LuaDataItemMake);
    rfa::game::lua_register(L, "rfg_dataitem_set", LuaDataItemSet);
    rfa::game::lua_register(L, "rfg_string_find", LuaStringFind);
    rfa::game::lua_register(L, "rfg_get_screen_dim", LuaScreenDim);
    rfa::game::lua_register(L, "rfg_alert", LuaAlert);
    rfa::game::lua_register(L, "rfg_map_blips", LuaMapBlips);
    rfa::game::lua_register(L, "rfg_map_blip", LuaMapBlip);
    rfa::game::lua_register(L, "rfg_map_sweep_clock", LuaMapSweepClock);
    rfa::game::lua_register(L, "rfg_sin_cos", LuaSinCos);
    rfa::game::lua_register(L, "rfg_key_down", LuaKeyDown);
    rfa::game::lua_register(L, "rfg_minimap_radius", LuaMinimapRadius);
    rfa::game::lua_register(L, "rfg_map_radio_tags", LuaMapRadioTags);
    rfa::game::lua_register(L, "rfg_map_radio_tag", LuaMapRadioTag);
    rfa::game::lua_register(L, "rfg_activity_running", LuaActivityRunning);
    rfa::game::lua_register(L, "rfg_map_icon_done", LuaMapIconDone);
    rfa::game::lua_register(L, "rfg_map_buildings", LuaMapBuildings);
    rfa::game::lua_register(L, "rfg_map_building", LuaMapBuilding);
    rfa::game::lua_register(L, "rfg_map_explosives", LuaMapExplosives);
    rfa::game::lua_register(L, "rfg_map_explosive", LuaMapExplosive);
    rfa::game::lua_register(L, "rfg_charges", LuaCharges);
    rfa::game::lua_register(L, "rfg_charges_most", LuaChargesMost);
    rfa::game::lua_register(L, "rfg_map_markers", LuaMapMarkers);
    rfa::game::lua_register(L, "rfg_map_marker", LuaMapMarker);
    rfa::game::lua_register(L, "rfg_district_change", LuaDistrictChange);
    rfa::game::lua_register(L, "rfg_liberation", LuaLiberation);
    rfa::game::lua_register(L, "rfg_liberation_step_done", LuaLiberationStepDone);
    rfa::game::lua_register(L, "rfg_activity", LuaActivity);
    rfa::game::lua_register(L, "rfg_activity_weapon", LuaActivityWeapon);
    rfa::game::lua_register(L, "rfg_screen_input", LuaScreenInput);
    rfa::game::lua_register(L, "rfg_activity_complete", LuaActivityComplete);
    rfa::game::lua_register(L, "rfg_activity_complete_tallys", LuaActivityCompleteTallys);
    rfa::game::lua_register(L, "rfg_activity_complete_tally", LuaActivityCompleteTally);
    rfa::game::lua_register(L, "rfg_guerrilla_text", LuaGuerrillaText);
    rfa::game::lua_register(L, "rfg_map_rings", LuaMapRings);
    rfa::game::lua_register(L, "rfg_map_ring", LuaMapRing);
}

// Guerrilla's activity-complete screen, for guerrilla_campaign as one of the two kinds it shows ends (Guerrilla's
// activities called hud_ui_activity_complete_start then; see "Guerrilla's activity-complete screen"):
//   GuerrillaHud_ActivityComplete(&results) -> 1 taken (it opens at the next HUD update the game isn't paused), 0 not
//   (one is up or waiting, a mode it doesn't show, or results of another size); results: guerrilla_hud's
//   ActivityResults, 44 bytes (size, Guerrilla's activity mode, success, the game log's reason, seven numbers).
// The player's choice comes back through the campaign's own GuerrillaCampaign_ActivityCompleteChoose(choice), if it
// has one: 1 the first hint, 2 the second.
int GuerrillaHud_ActivityComplete(const void* results) {
    if (!results || !guerrilla_hud::Running()) return 0;
    const int size = *static_cast<const int*>(results);
    if (size != static_cast<int>(sizeof(ActivityResults))) {
        rfa::Log("GuerrillaHud_ActivityComplete: results of %d bytes, not %u; not shown", size, static_cast<unsigned>(sizeof(ActivityResults)));
        return 0;
    }
    ActivityResults copy{};
    memcpy(&copy, results, sizeof(copy));
    return AskActivityComplete(copy, false, "the campaign") ? 1 : 0;
}

void guerrilla_hud::Init() {
    // Log= also has Guerrilla's HUD scripts write what they find (rfg_ui_*.lua read it themselves).
    if (rfa::ReadLogSwitch(L"GuerrillaHud")) rfa::Log("Log=1: routine lines (\"[guerrilla_hud:log]\") are on");
    if (!rfa::game::IsSupportedBuild()) {
        rfa::Log("Not the publicbeta build; nothing to do");
        return;
    }
    RaiseDocumentLimits();  // The builder adds the HUD's documents to the interface project whatever the settings
    RaiseVintHeap();
    g_worlds = SplitList(rfa::IniString(L"GuerrillaHud", L"Worlds", L"rfg"));
    for (const std::string& name : SplitList(rfa::IniString(L"GuerrillaHud", L"Documents", L"rfg_ui_hud_health")))
        g_documents.push_back({name});
    if (rfa::IniInt(L"GuerrillaHud", L"Reticles", 1) && !FindDocument("rfg_ui_hud_reticles"))
        g_documents.push_back({"rfg_ui_hud_reticles"});
    if (rfa::IniInt(L"GuerrillaHud", L"WeaponSelect", 1) && !FindDocument("rfg_ui_hud_weapon_sel"))
        g_documents.push_back({"rfg_ui_hud_weapon_sel"});
    if (rfa::IniInt(L"GuerrillaHud", L"Map", 1)) {
        if (!FindDocument("rfg_ui_hud")) g_documents.push_back({"rfg_ui_hud"});
        g_mapDocument = "rfg_ui_fullscreen";
        Document map{g_mapDocument};
        map.whileOpen = true;  // loaded with the HUD, so it opens at once; drawn only while open
        g_documents.push_back(map);
        // Guerrilla's prompts (its use prompt at the safehouses' stations) and its upgrade screen, loaded with
        // the HUD; the screen drawn only while a shop is open.
        if (!FindDocument("rfg_ui_messages")) g_documents.push_back({"rfg_ui_messages"});
        // Guerrilla's help box (its tool tips), loaded with the HUD, drawn over the rest, our screens too (a tip the
        // weapon cabinet raises shows over it).
        Document help{"rfg_ui_hud_help"};
        help.onTop = true;
        g_documents.push_back(help);
        Document shop{"rfg_ui_upgrades"};
        shop.whileShop = true;
        g_documents.push_back(shop);
        Document cabinet{"rfg_ui_safehouse_weapons"};  // Guerrilla's weapon cabinet, drawn only while it's open
        cabinet.whileCabinet = true;
        g_documents.push_back(cabinet);
        // The campaign's: an activity's corner panel (with the HUD), the district-liberated screen, an activity's
        // briefing and results, and Guerrilla's activity-complete screen (only while they're open).
        if (!FindDocument("rfg_ui_hud_missions")) g_documents.push_back({"rfg_ui_hud_missions"});
        Document liberation{"rfg_ui_district_liberated"};
        liberation.whileLiberation = true;
        g_documents.push_back(liberation);
        Document demolitions{"rfg_ui_demolitions"};
        demolitions.whileActivity = true;
        g_documents.push_back(demolitions);
        Document activityComplete{kActivityCompleteDocument};  // Guerrilla's activity-complete screen
        activityComplete.whileActivityComplete = true;
        g_documents.push_back(activityComplete);
        Document handbook{"rfg_ui_handbook"};  // Guerrilla's handbook, loaded only while it's open (see Wanted)
        handbook.whileHandbook = true;
        g_documents.push_back(handbook);
        for (const char* set : kZoneSets) {
            // Listed to be drawn like the others (they draw nothing: one empty group). Unlisted and
            // without a script, the game closed such documents as soon as they had loaded (2026-11-05).
            Document tiles{set};
            tiles.zoneSet = true;
            g_documents.push_back(tiles);
        }
        keys::Read();  // the map's keys among Guerrilla's ([GuerrillaKeys], F7 Guerrilla > Keys)
        ReadDistricts();
        ReadMapIcons();
        ReadRadioTags();
        ReadSafehouses();
        ReadCabinet();
        ReadHandbook();
        tool_tips::Read();
        ReadHudMessages();
        // The handbook's entries' pictures' documents, listed before the handbook's own: each pass over the list
        // unloads a picture before the handbook (the opposite of the order they load in; see Wanted).
        std::set<std::string> pictureDocuments;
        auto handbookAt = std::find_if(g_documents.begin(), g_documents.end(), [](const Document& d) { return d.whileHandbook; });
        size_t at = static_cast<size_t>(handbookAt - g_documents.begin());
        for (const HandbookEntry& e : g_handbookEntries)
            if (!e.pictureDocument.empty() && pictureDocuments.insert(e.pictureDocument).second) {
                Document picture{e.pictureDocument};
                picture.handbookPicture = true;
                g_documents.insert(g_documents.begin() + static_cast<std::ptrdiff_t>(at++), picture);
            }
        EnlargeInterfacePool();
    }
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
    g_loadStart = reinterpret_cast<LoadStartFn>(rfa::HookFunction(kLoadStart, kLoadStartPrologue, reinterpret_cast<void*>(&LoadStart)));
    if (!g_loadStart) rfa::Log("Could not hook the start of level loads; a reload may crash when the HUD unloads a map picture after it");
    HookUsableItems();
    HookFonts();
    std::string documents, worlds;
    for (const Document& doc : g_documents) documents += (documents.empty() ? "" : ", ") + doc.name;
    for (const std::string& world : g_worlds) worlds += (worlds.empty() ? "" : ", ") + world;
    rfa::Log("Guerrilla's HUD (%s) in the worlds %s%s", documents.c_str(), worlds.c_str(),
             g_mapDocument.empty() ? "" : "; the world map's key is [GuerrillaKeys] Map= (F7 Guerrilla > Keys)");
}
