// Red Faction: Guerrilla's campaign systems, which Armageddon's engine no longer has, for the world
// ported from Guerrilla.
//
// Guerrilla's missions and activities are action scripts (.scriptx), and Armageddon still runs
// that kind of script, but it dropped about a third of Guerrilla's script words along with the
// systems behind them: the liberation of Mars district by district (EDF control, civilian morale,
// the alert level, raids, convoys, couriers, roadblocks, air strikes, guerrilla reinforcements,
// the EDF's tech level) and the activities (demolitions master, house arrest, raids, ...). This
// mod puts them back:
//
// - script_engine.cpp: how Armageddon's script engine is reached, and how the mod adds Guerrilla's
//   words (actions, events, functions) and the kinds of value Armageddon's script reader lost.
// - script_words.cpp: the words, with Guerrilla's arguments, and what each does.
// - handle_kinds.cpp: the kinds of value (alert levels, air strikes, convoys, ...) Guerrilla's
//   scripts name and Armageddon's script reader no longer knows.
// - liberation.cpp: the districts, EDF control and morale, the alert level, the tech and
//   progression levels, salvage; alert_sources.cpp what the alert level hears from the world (the
//   EDF's people seeing the player, its dead, its vehicles damaged and wrecked).
// - restricted_areas.cpp: Guerrilla's restricted areas (its EDF bases); undercover.cpp its rule that
//   the EDF take the player for a civilian while the alert is green.
// - activities.cpp (and activity_base.h): the activity framework; demolitions_master.cpp its
//   first kind.
// - tables.cpp: Guerrilla's tables, from mods\guerrilla_campaign\tables\, and world_objects.cpp
//   Guerrilla's campaign markers (activity nodes, ...), from mods\guerrilla_campaign\world\: both
//   made from the player's own copy of Guerrilla, never shipped.
// - save.cpp: what the campaign keeps that Armageddon's save has no place for.
//
// Each system keeps its state so that Guerrilla's own HUD data items (MoraleUpdate, AlertLevel,
// MissionStatus, DemolitionsInfo, ...) can be filled from it.
//
// The words are put in for every world (Armageddon's own content has no action scripts); the
// systems run only in the worlds named by [GuerrillaCampaign] Worlds= (the ported world is "rfg"),
// each frame of the game's script update (script_do_frame, 0x916AB0, which runs the scripts while a
// level is played).
//
// So far only the alert level is tried in the game. The script words and the activities have never
// run, so each waits behind a setting of its own, off unless asked for.
//
// [GuerrillaCampaign] in rfa_mods.ini: Enabled= (1; 0 leaves the game alone), Worlds= (rfg), Undercover=
// (1; 0: the EDF take the player for an enemy at any alert level, as Armageddon's do), Scripts= (0; 1
// puts Guerrilla's script words in), Activities= (0; 1 runs the activity framework), Log= (0; 1 writes
// what the alert level counts, each second it changes).
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "campaign_api.h"
#include <atomic>
#include "rfa_game.h"
#include "campaign.h"
#include "script_engine.h"
#include "script_words.h"
#include "handle_kinds.h"
#include "liberation.h"
#include "activities.h"
#include "alert_sources.h"
#include "restricted_areas.h"
#include "undercover.h"
#include "friendly_fire.h"
#include "kill_regions.h"
#include "stealth.h"
#include "district_commands.h"
#include "district_sources.h"
#include "converts.h"
#include "guerrilla_squads.h"
#include "alert_orders.h"
#include "reinforcements.h"
#include "attack_squads.h"
#include "difficulty.h"

namespace campaign {
namespace {

// The game's script update, each frame of play: script timers, then the scripts (cdecl, no
// arguments). Guerrilla's script_do_frame; found by its calls (mission_is_active 0x504A50,
// mission_is_ready 0x503DA0, the script timers 0x9251B0).
constexpr DWORD kScriptFrame = 0x916AB0;
constexpr size_t kScriptFramePrologue = 6;  // push ebp / mov ebp,esp / sub esp,4Ch
using ScriptFrameFn = void(__cdecl*)();
ScriptFrameFn g_scriptFrame;

std::vector<std::string> g_worlds;
int g_inOurWorld = -1;      // last frame's answer (-1 before the first)
bool g_playing = false;     // in one of our worlds with the player there: the systems run
bool g_activities = false;  // [GuerrillaCampaign] Activities=
int g_lastClock = 0;        // the game's clock at the last frame
std::atomic<bool> g_worldOpened{false};  // GuerrillaCampaign_WorldOpened: the level starts again at the next frame

// The game's frame time, in seconds (Guerrilla's Frametime): read by some 580 places, among them
// the game clock's update each frame (0x5411D2, passed to 0x51FC10).
auto& g_frameTime = *reinterpret_cast<float*>(0x11217C8);
float FrameTime() {
    const float seconds = g_frameTime;
    return seconds < 0.0f ? 0.0f : seconds > 0.25f ? 0.25f : seconds;
}

void __cdecl ScriptFrame() {
    const bool ours = InOurWorld();
    if (static_cast<int>(ours) != g_inOurWorld) {
        g_inOurWorld = ours;
        rfa::LogDetail(ours ? "Guerrilla's campaign systems run in %s" : "%s isn't one of Worlds=; Guerrilla's campaign systems rest",
                 WorldName());
    }
    // A level starts when the player is there (again): each start is a start for the systems, as
    // Guerrilla's level init is (the alert begins green).
    const bool playing = ours && Player() != nullptr;
    const int clock = GameTime();
    if (clock < g_lastClock) g_playing = false;  // the clock began again: another level
    // A world of ours opened again (guerrilla_save's GuerrillaCampaign_WorldOpened, after the save's state was given
    // back): a reload from memory restarts neither the clock nor the player, so it says so (Guerrilla's level_init).
    if (g_worldOpened.exchange(false)) g_playing = false;
    if (playing && !g_playing) {
        liberation::Enter();
        alert_sources::Enter();
        restricted_areas::Enter();
        friendly_fire::Enter();
        kill_regions::Enter();
        stealth::Enter();
        district_commands::Enter();
        district_sources::Enter();
        converts::Enter();
        guerrilla_squads::Enter();
        alert_orders::Enter();
        reinforcements::Enter();
        attack_squads::Enter();
        if (g_activities) activities::Enter();
    }
    g_playing = playing;
    const bool ticking = clock != g_lastClock;  // not paused, not loading
    g_lastClock = clock;
    if (playing && ticking) {
        const float seconds = FrameTime();
        alert_sources::Frame();
        restricted_areas::Frame();
        undercover::Frame();
        friendly_fire::Frame();
        kill_regions::Frame(g_activities && activities::AnyRunning());
        stealth::Frame();
        district_sources::Frame();
        converts::Frame();
        guerrilla_squads::Frame();
        alert_orders::Frame();
        reinforcements::Frame();
        attack_squads::Frame();
        liberation::Update(seconds);
        if (g_activities) activities::Update(seconds);
    }
    g_scriptFrame();
}

// The world object (0x1870630) finds an object by handle (thiscall, 0x6CB9E0; as the scripts' own
// callbacks do, e.g. 0x914D20).
using ObjectFromHandleFn = void*(__fastcall*)(void* world, void* unused, unsigned handle);
auto const GameObjectFromHandle = reinterpret_cast<ObjectFromHandleFn>(0x6CB9E0);
auto const g_world = reinterpret_cast<void*>(0x1870630);

// The local player: 0x8B92D0 returns this pointer (player_in_trigger_region, 0x91A320, tests its
// position, the player's +4).
auto& g_player = *reinterpret_cast<void**>(0x2271490);

}  // namespace

const char* WorldName() { return reinterpret_cast<const char*>(0x1870630 + 0x29EA); }

void* ObjectFromHandle(unsigned handle) {
    return handle == 0xFFFFFFFF ? nullptr : GameObjectFromHandle(g_world, nullptr, handle);
}

Vector PositionOf(const void* object) {
    return object ? *reinterpret_cast<const Vector*>(static_cast<const BYTE*>(object) + 4) : Vector{};
}

void* Player() { return g_player; }

int GameTime() { return *reinterpret_cast<const int*>(0x1121300); }

bool Playing() { return g_playing; }

bool InOurWorld() {
    const char* world = WorldName();
    for (const std::string& name : g_worlds)
        if (_stricmp(world, name.c_str()) == 0) return true;
    return false;
}

const std::wstring& ModFolder() {
    static const std::wstring folder = rfa::RootDir() + L"\\mods\\guerrilla_campaign";
    return folder;
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

}  // namespace campaign

void guerrilla_campaign::Init() {
    using namespace campaign;
    rfa::ReadLogSwitch(L"GuerrillaCampaign");
    if (!rfa::game::IsSupportedBuild()) {
        rfa::Log("Not the publicbeta build; nothing to do");
        return;
    }
    if (rfa::IniInt(L"GuerrillaCampaign", L"Enabled", 1) == 0) {
        rfa::LogDetail("Off (Enabled=0 in [GuerrillaCampaign])");
        return;
    }
    g_worlds = SplitList(rfa::IniString(L"GuerrillaCampaign", L"Worlds", L"rfg"));
    liberation::Init();
    difficulty::Init();
    restricted_areas::Init();
    friendly_fire::Init();  // before undercover's, whose wrap asks it
    undercover::Init();
    kill_regions::Init();
    stealth::Init();
    district_sources::Init();
    converts::Init();
    guerrilla_squads::Init();
    alert_orders::Init();
    reinforcements::Init();
    g_activities = rfa::IniInt(L"GuerrillaCampaign", L"Activities", 0) != 0;
    if (g_activities) activities::Init();
    if (rfa::IniInt(L"GuerrillaCampaign", L"Scripts", 0) != 0) {
        script::Install(words::All(), words::Count());
        handles::AddAll();
        if (script::HookScriptReader()) rfa::LogDetail("Guerrilla's script words go in before the first script");
        else rfa::Log("Could not hook the game's script reader; Guerrilla's scripts won't find the words they need");
    }
    g_scriptFrame = reinterpret_cast<ScriptFrameFn>(rfa::HookFunction(kScriptFrame, kScriptFramePrologue, reinterpret_cast<void*>(&ScriptFrame)));
    if (!g_scriptFrame) rfa::Log("Could not hook the game's script update; the campaign systems won't run");
    else rfa::LogDetail("The systems run in the worlds named by Worlds= in [GuerrillaCampaign] (script words %s, activities %s)",
                  rfa::IniInt(L"GuerrillaCampaign", L"Scripts", 0) ? "on" : "off", g_activities ? "on" : "off");
}

// For guerrilla_hud: Guerrilla's alert level (0 green, 1 yellow, 2 orange, 3 red) and, in *meter, what
// its HUD shows of it (0 to 100: liberation::AlertMeter); -1 while the systems rest.
int GuerrillaCampaign_Alert(float* meter) {
    using namespace campaign;
    if (!guerrilla_campaign::Running() || !g_playing) return -1;
    if (meter) *meter = liberation::AlertMeter();
    return liberation::AlertLevel();
}

// For guerrilla_save: one of our worlds opened again (see campaign_api.h): the level starts again at the next frame.
void GuerrillaCampaign_WorldOpened() {
    if (!guerrilla_campaign::Running()) return;
    campaign::g_worldOpened = true;
}
