// rfa_guerrilla.dll: RFA-Mod's mods for Red Faction: Guerrilla's world and campaign, in one DLL, so mods\ holds fewer
// files. Its parts, each in a folder of its own here, and the section of rfa_mods.ini that holds its settings:
//   guerrilla_campaign  campaign\  the campaign's systems Armageddon no longer has (alert, liberation, ...)  [GuerrillaCampaign]
//   guerrilla_gear      gear\      the player's gear: weapons, the hammer, backpacks, upgrades              [GuerrillaGear]
//   guerrilla_hud       hud\       Guerrilla's HUD in its world, and the keys of its gear                  [GuerrillaHud]
//   guerrilla_kiosks    kiosks\    the EDF's news kiosks                                                    [GuerrillaKiosks]
//   guerrilla_people    people\    what Guerrilla's people need from Armageddon's engine                   [GuerrillaPeople]
//   guerrilla_save      save\      saving in Guerrilla's world                                              [GuerrillaSave]
//   guerrilla_vehicles  vehicles\  what Guerrilla's vehicles need from Armageddon's engine                 [GuerrillaVehicles]
//   guerrilla_voices    voices\    Guerrilla's people's voices                                              [GuerrillaVoices]
//   guerrilla_world     world\     what Guerrilla's world needs: sky, light, sounds, props, destruction, ... [GuerrillaWorld]
// Each works as it did when it was a DLL of its own: its settings section in rfa_mods.ini, its own switch under
// [Mods] (guerrilla_campaign=, ...; 0 = off, on when missing), its own name in the log and its data folder in mods\.
// They start in the order they loaded in as separate DLLs (by name), each in a crash guard of its own.
//
// The old DLLs (guerrilla_campaign.dll, ...) may still be in mods\ when an update was copied over an older
// install; the loader skips them while this DLL is there.
#define RFA_MOD_NAME "rfa_guerrilla"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"

namespace {

using InitFn = void (*)();
using MenuFn = void (*)(const RFAMenu*);

// Each part: its [Mods] key, its start, its menus (or none), and whether it is running.
struct Part {
    const wchar_t* name;
    InitFn init;
    MenuFn menu;
    bool running;
    bool menuCrashed;
};

Part g_parts[] = {
    {L"guerrilla_campaign", guerrilla_campaign::Init, nullptr, false, false},
    {L"guerrilla_gear", guerrilla_gear::Init, guerrilla_gear::Menu, false, false},
    {L"guerrilla_hud", guerrilla_hud::Init, guerrilla_hud::Menu, false, false},
    {L"guerrilla_kiosks", guerrilla_kiosks::Init, nullptr, false, false},
    {L"guerrilla_people", guerrilla_people::Init, nullptr, false, false},
    {L"guerrilla_save", guerrilla_save::Init, nullptr, false, false},
    {L"guerrilla_vehicles", guerrilla_vehicles::Init, nullptr, false, false},
    {L"guerrilla_voices", guerrilla_voices::Init, nullptr, false, false},
    {L"guerrilla_world", guerrilla_world::Init, nullptr, false, false},
};
enum PartIndex { kCampaign, kGear, kHud, kKiosks, kPeople, kSave, kVehicles, kVoices, kWorld, kParts };
static_assert(sizeof(g_parts) / sizeof(g_parts[0]) == kParts, "one entry for each part");

// Catches a crash in one part's start, so it ends up in the log and the other parts still start.
bool CallInit(InitFn init) {
    __try {
        init();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Starts a part unless its switch under [Mods] is 0. It counts as running while it starts, so it can call its own
// functions then, and the parts before it in the list are already running.
void StartPart(Part& part) {
    if (!rfa::IniInt(L"Mods", part.name, 1)) {
        rfa::LogDetail("Skipping %ls (disabled in [Mods])", part.name);
        return;
    }
    part.running = true;
    if (!CallInit(part.init)) {
        part.running = false;
        rfa::Log("%ls crashed while starting; it is off, and the other parts take it as missing", part.name);
    }
}

// Catches a crash in one part's menus, as the loader does for each DLL's.
bool CallMenu(MenuFn menu, const RFAMenu* info) {
    __try {
        menu(info);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

bool guerrilla_campaign::Running() { return g_parts[kCampaign].running; }
bool guerrilla_gear::Running() { return g_parts[kGear].running; }
bool guerrilla_hud::Running() { return g_parts[kHud].running; }
bool guerrilla_kiosks::Running() { return g_parts[kKiosks].running; }
bool guerrilla_people::Running() { return g_parts[kPeople].running; }
bool guerrilla_save::Running() { return g_parts[kSave].running; }
bool guerrilla_vehicles::Running() { return g_parts[kVehicles].running; }
bool guerrilla_voices::Running() { return g_parts[kVoices].running; }
bool guerrilla_world::Running() { return g_parts[kWorld].running; }

extern "C" __declspec(dllexport) void __cdecl RFAMod_Init() {
    int running = 0;
    std::wstring names;  // the parts running, for the start-up line
    for (Part& part : g_parts) {
        StartPart(part);
        if (!part.running) continue;
        ++running;
        names += (names.empty() ? L"" : L", ") + std::wstring(part.name);
    }
    rfa::LogInfo("%d of %d parts running: %ls", running, static_cast<int>(kParts), names.c_str());
}

// The in-game menus (rfa_menu.h): guerrilla_gear's upgrades window, then guerrilla_hud's keys window. A part whose
// menus crashed is left out from then on, and the rest wait for the next frame, after the loader has put ImGui
// right again.
extern "C" __declspec(dllexport) void __cdecl RFAMod_Menu(const RFAMenu* menu) {
    for (Part& part : g_parts) {
        if (!part.menu || !part.running || part.menuCrashed) continue;
        if (!CallMenu(part.menu, menu)) {
            part.menuCrashed = true;
            rfa::Log("%ls's menus crashed; they are off", part.name);
            return;
        }
    }
}

// The game's menu Lua: guerrilla_hud's functions (its maps, handbook and screens), then guerrilla_save's.
extern "C" __declspec(dllexport) void __cdecl RFAMod_RegisterLua(rfa::game::lua_State* L) {
    if (g_parts[kHud].running) guerrilla_hud::RegisterLua(L);
    if (g_parts[kSave].running) guerrilla_save::RegisterLua(L);
}
