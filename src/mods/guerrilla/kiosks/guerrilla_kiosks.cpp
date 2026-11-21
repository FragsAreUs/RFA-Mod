// Red Faction: Guerrilla's EDF news kiosks, which Armageddon's engine no longer has, for the world ported from
// Guerrilla.
//
// A kiosk is part of Guerrilla's public address system (kiosk.cpp): a mover whose gameplay properties have the chunk
// flag "kiosk" (the EDF tech kiosk, tech_kiosk_EDF_A, with four screens; the bars' radio, tech_radio_industrial, with
// none). Armageddon kept only the names (its chunk flag reader knows "kiosk", 0x4000; its level objects' script
// point flags know "Kiosk Large Screen" 0x20 and "Kiosk Small Screen" 0x40) and none of the code. This mod plays
// Guerrilla's code on the kiosks the world places:
// - screens.cpp: the pictures on the kiosks' screens (Guerrilla's kiosk_update_effects_and_action_nodes and
//   kiosk_start_effect_on_screen): idle, the alert picture while the alert level is yellow or above, the news while
//   a broadcast plays;
// - broadcast.cpp: the EDF's news broadcast at the kiosk (or bar radio) near the player (Guerrilla's kiosk_do_frame):
//   its music, its newsreader's lines and the pictures they show;
// - tables.cpp: Guerrilla's pa_defaults.xtbl, kiosk_screens.xtbl and kiosk_lines.xtbl and the list of the world's
//   kiosks (mods\guerrilla_kiosks\, from editor\GuerrillaBuilder.exe when it builds the world; never shipped).
//
// It runs each frame of play just after the game's scripts (Armageddon's object_logic_handler_do_frame, 0x7EE7A0, the
// step that follows script_do_frame: where Guerrilla's gamestate_gp_simulate_frame calls
// public_address_kiosk_do_frame), in the worlds named by [GuerrillaKiosks] Worlds= (the ported world is "rfg").
//
// [GuerrillaKiosks] in rfa_mods.ini: Enabled= (1; 0 leaves the game alone), Worlds= (rfg), Broadcast= (1; 0 keeps
// the screens only, no broadcast), Log= (0; 1 writes each kiosk's screens as they change).
//
// For the other mods: GuerrillaKiosks_Active (the kiosk broadcasting now: Guerrilla's kiosk_get_active_kiosk, which
// its people look at and keep quiet near), GuerrillaKiosks_UnlockActivity / _UnlockMission (Guerrilla's
// kiosk_unlock_from_activity / _from_mission, called when one is won) and GuerrillaKiosks_UnlockLine (the script
// action kiosk_unlock_line).
#define RFA_MOD_NAME "guerrilla_kiosks"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "kiosks_api.h"
#include "campaign/campaign_api.h"
#include "rfa_game.h"
#include "kiosks.h"

namespace kiosks {
namespace {

// Armageddon's object_logic_handler_do_frame (cdecl, no arguments), called each frame of play by the simulation
// frame (0x540F50, at 0x541A15) just after script_do_frame (0x916AB0).
constexpr DWORD kFrameStep = 0x7EE7A0;
constexpr size_t kFrameStepPrologue = 6;  // push ebp / mov ebp,esp / sub esp,44h
const BYTE kFrameStepOpening[kFrameStepPrologue] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x44};
using FrameStepFn = void(__cdecl*)();
FrameStepFn g_frameStep;

std::vector<std::string> g_worlds;
int g_inOurWorld = -1;     // last frame's answer (-1 before the first)
bool g_broadcast = false;  // [GuerrillaKiosks] Broadcast=

// The game's frame time, in seconds (read by some 580 places; Guerrilla's Frametime).
auto& g_frameTime = *reinterpret_cast<const float*>(0x11217C8);

void __cdecl FrameStep() {
    const bool ours = InOurWorld();
    if (static_cast<int>(ours) != g_inOurWorld) {
        g_inOurWorld = ours;
        rfa::LogDetail(ours ? "Guerrilla's kiosks run in %s" : "%s isn't one of Worlds=; Guerrilla's kiosks rest", WorldName());
        if (!ours) {
            screens::Forget();
            if (g_broadcast) broadcast::Forget();
        }
    }
    if (ours && Player()) {
        const float seconds = g_frameTime < 0.0f ? 0.0f : g_frameTime > 0.25f ? 0.25f : g_frameTime;
        if (g_broadcast) broadcast::Update(seconds);
        screens::Update(seconds);
    }
    g_frameStep();
}

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

// The world object (0x1870630) finds an object by handle (thiscall, 0x6CB9E0; null for none, or while it is
// being destroyed).
using ObjectFromHandleFn = BYTE*(__fastcall*)(void* world, void* unused, DWORD handle);
auto const GameObjectFromHandle = reinterpret_cast<ObjectFromHandleFn>(0x6CB9E0);

// The local player (0x8B92D0 returns this pointer).
auto& g_player = *reinterpret_cast<BYTE* const*>(0x2271490);

// guerrilla_campaign's alert level (GuerrillaCampaign_Alert: 0 green .. 3 red, -1 while its systems rest).
bool g_campaignLooked = false;

}  // namespace

bool g_log = false;

const char* WorldName() { return reinterpret_cast<const char*>(kWorld + 0x29EA); }

bool InOurWorld() {
    const char* world = WorldName();
    for (const std::string& name : g_worlds)
        if (_stricmp(world, name.c_str()) == 0) return true;
    return false;
}

BYTE* ObjectFromHandle(DWORD handle) {
    return handle == 0xFFFFFFFF ? nullptr : GameObjectFromHandle(reinterpret_cast<void*>(kWorld), nullptr, handle);
}

BYTE* Player() { return g_player; }

int AlertLevel() {
    if (!g_campaignLooked) {
        g_campaignLooked = true;
        rfa::LogDetail(guerrilla_campaign::Running() ? "The alert level comes from guerrilla_campaign (the alert picture at yellow and above)"
                                               : "guerrilla_campaign isn't running: the alert level stays green for the kiosks");
    }
    const int level = GuerrillaCampaign_Alert(nullptr);
    return level < 0 ? 0 : level;
}

const std::wstring& ModFolder() {
    static const std::wstring folder = rfa::RootDir() + L"\\mods\\guerrilla_kiosks";
    return folder;
}

}  // namespace kiosks

void guerrilla_kiosks::Init() {
    using namespace kiosks;
    if (!rfa::game::IsSupportedBuild()) {
        rfa::Log("Not the publicbeta build; nothing to do");
        return;
    }
    if (rfa::IniInt(L"GuerrillaKiosks", L"Enabled", 1) == 0) {
        rfa::LogDetail("Off (Enabled=0 in [GuerrillaKiosks])");
        return;
    }
    g_worlds = SplitList(rfa::IniString(L"GuerrillaKiosks", L"Worlds", L"rfg"));
    g_log = rfa::ReadLogSwitch(L"GuerrillaKiosks");
    if (!tables::Load()) return;  // says why
    g_broadcast = rfa::IniInt(L"GuerrillaKiosks", L"Broadcast", 1) != 0;
    if (g_broadcast) broadcast::Init();
    else rfa::LogDetail("Broadcast=0 in [GuerrillaKiosks]: the screens only, no broadcast");
    if (memcmp(reinterpret_cast<const void*>(kFrameStep), kFrameStepOpening, sizeof(kFrameStepOpening)) == 0)
        g_frameStep = reinterpret_cast<FrameStepFn>(rfa::HookFunction(kFrameStep, kFrameStepPrologue, reinterpret_cast<void*>(&FrameStep)));
    if (!g_frameStep) {
        rfa::Log("Could not hook the game's frame step after its scripts (its code isn't as expected); the kiosks stay dark");
        return;
    }
    rfa::LogDetail("Guerrilla's kiosk code runs in the worlds named by Worlds= in [GuerrillaKiosks]");
}

// ---- For the other mods

// The kiosk broadcasting now (Guerrilla's kiosk_get_active_kiosk: its people take it as something to look at, and
// don't mutter or say their ambient lines near it): 1 and its handle and place, or 0 when none is.
int GuerrillaKiosks_Active(DWORD* handle, float* place) {
    return guerrilla_kiosks::Running() && kiosks::broadcast::Active(handle, place) ? 1 : 0;
}

// The lines an activity (its activity.xtbl name, e.g. je_ha_01) unlocks, when it is won (Guerrilla's record_victory →
// kiosk_unlock_from_activity): how many were unlocked.
int GuerrillaKiosks_UnlockActivity(const char* activity) {
    return guerrilla_kiosks::Running() ? kiosks::broadcast::UnlockActivity(activity) : 0;
}

// The lines a mission (its missions.xtbl name) unlocks, when it is won (Guerrilla's kiosk_unlock_from_mission).
int GuerrillaKiosks_UnlockMission(const char* mission) {
    return guerrilla_kiosks::Running() ? kiosks::broadcast::UnlockMission(mission) : 0;
}

// One line, by its voice line's name (the script action kiosk_unlock_line): 1 if it was unlocked.
int GuerrillaKiosks_UnlockLine(const char* line) {
    return guerrilla_kiosks::Running() ? kiosks::broadcast::UnlockLine(line) : 0;
}
