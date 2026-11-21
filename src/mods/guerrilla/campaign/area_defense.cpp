// Guerrilla's area defense, "HEAVY METAL" (activity mode 16, modal): hold a ring against waves of EDF vehicles or
// soldiers until enough of them are destroyed. Guerrilla (RM addresses):
//
// - A site is an object_area_defense_node (area_defense_type: the row of activity_area_defense.xtbl) and a start object,
//   an object_activity_spawn of the same row (activity_type), with a player_start under it. The row names its
//   activity.xtbl row (Base_Data: the managed layer set with the given walker, tank or turret and the spawn regions),
//   the ring's radius about the node, how many kills of which kind win (Vehicles or Humans), up to five stages (each a
//   continuous attack, entered at its Kill_Limit or Time_Limit) and the rewards.
// - The offer (do_frame_inactive 0x7A8BF0, player_is_near_area_defense_node 0xA83F20): within 5 m of a start object on
//   foot, its node not completed and its district not liberated, nothing else running, the alert below orange
//   (activity_area_defense_can_be_started 0x7A0B10): the use key starts it (activity_area_defense_start 0x7A0AC0: every
//   other activity killed, the run's layers loading).
// - Loading (activity_do_frame_loading 0x7A8BC0, do_frame_load_layers 0x7A09F0): the fade to black; once black the
//   start-up (activity_begin_start_up 0x77F980: the diversions suspended, the kills and stage zeroed), the layer's
//   objects made, the fade back in; then WAIT_PLAYER.
// - WAIT_PLAYER (0x756F30): once the player is within the radius (on the ground, x and z) of the node: RUNNING. Every
//   start object is already inside its ring, so that is the first frame.
// - RUNNING (do_frame_active_running 0x7B1610):
//   - kills (register_death 0x769FD0, while running, whoever killed): a vehicle whose most recent driver was the EDF's,
//     a human of the EDF's team;
//   - the stages (test_reinforcement_conditions 0x7B1440): the next stage's continuous attack starts (the old one ended)
//     once the kills reach its Kill_Limit or the seconds since the start its Time_Limit; the first (0 / 0) at once;
//   - out of the ring (player_in_area_defense_region 0x77FA90): a 10 s timer; at 0, still out: lost, out of range;
//   - the row's kills reached with the player alive (test_complete_conditions 0x76A0E0): won.
// - Won (record_victory 0x7B55C0): the rewards in the player's district (apply_standard_rewards 0x7B2E30: EDF control
//   down, salvage), the node completed (area_defense_completed); 2 s later the clean-up.
// - Lost (left the ring; the player died: activity_area_defense_process_player_death 0x757100): Guerrilla's results
//   screen, rfg_ui_mission_complete, with RETRY and EXIT.
// - The clean-up (do_frame_cleanup 0x7A8CD0, player_equipment_cleanup 0x7A0890) behind a fade: the continuous attack
//   ended and every NPC it made taken away, the player out of a tank or walker (warp_player_on_end 0x7A05F0), the
//   layer's walker, tank or turret destroyed; then the fade back in.
//
// Here: the offer, the start, the fades, the layer, WAIT_PLAYER and RUNNING with the kills, the stages through the
// continuous attack (continuous_attack.cpp), the 10 s out of the ring, the win's rewards and the node completed in the
// campaign's save, the clean-up; for guerrilla_hud (GuerrillaCampaign_Activity) the corner panel (HEAVY METAL, the
// subtitle while running, "kills / required", the vehicles or soldiers icon), the 10 s timer and the COMPLETE stamp
// (MissionStatus, 6 s); a loss's results screen (GuerrillaHud_ActivityComplete, mode 16: Guerrilla's
// rfg_ui_mission_complete) with RETRY (activity_area_defense_replay 0x7570E0: the world saved with the player at the
// start object's player_start and opened again, the run started at the node once it has: activity_force_start_generic
// 0x7A8710) and EXIT (activity_area_defense_exit 0x7570A0: the clean-up; a dead player wakes at the nearest safehouse);
// the death taken as the player's death timer runs out (gamestate_gp_simulate_frame's last activity). Not ported: the
// radio lines (no activity plays them yet), the input switched off during the fades, the start flare. What the layers'
// scripts do (the given vehicle's hit points, the restore lists, the traffic and reinforcements switched off) waits for
// the scripts.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "activity_base.h"
#include "hud/hud_api.h"
#include "save/save_api.h"
#include "alert_sources.h"
#include "handle_kinds.h"
#include "liberation.h"
#include "save.h"
#include "tables.h"
#include "world_objects.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <unordered_map>

namespace campaign::activities {
namespace {

// One stage of a row (area_defense_reinforcement_item): the continuous attack and when it starts.
struct Stage {
    std::string attack;    // Continuous_attack: a row of activity_continuous_attack.xtbl
    int killLimit = 99;    // Kill_Limit
    int timeLimit = 9999;  // Time_Limit, seconds since the run began
};

// One row of activity_area_defense.xtbl (area_defense_info, load_area_defense_table 0x796240).
struct Row {
    std::string name;
    std::string baseData;     // Base_Data: its activity.xtbl row (its layers)
    float radius = 50.0f;     // Area_radius
    int requiredKills = 15;   // Required_kills
    bool vehicles = true;     // Kill_type: Vehicles (else Humans)
    std::vector<Stage> stages;
    Rewards rewards;          // Standard_Rewards
};

// A site: its node, its start object and where a retry puts the player, its row, and its layer's given walker, tank or
// turret (the layer's spawn node: its place and whether it is a turret's).
struct Site {
    const world::Object* node = nullptr;
    const world::Object* start = nullptr;
    const Row* row = nullptr;
    bool hasPlayerStart = false;
    float playerStart[12] = {};  // the start object's player_start (else the start object): place, then turn
    bool hasGiven = false, givenIsTurret = false;
    Vector given{};
    std::string givenName;   // its spawn_set (turret_type for a turret)
};

constexpr float kUseReach = 5.0f;          // the use prompt within 5 m of the start object (dist² < 25)
constexpr float kHereReach = 20.0f;        // the console says which one is here within 20 m (Guerrilla's start line)
constexpr int kFleeMs = 10000;             // the out-of-ring timer
constexpr int kSplashMs = 2000;            // won: the clean-up 2 s after VICTORY
constexpr float kFadeSeconds = 1.0f;
constexpr float kFadeWaitMost = 2.0f;      // never wait longer for the black
constexpr float kExitWaitMost = 3.0f;      // the player out of the given vehicle, behind the clean-up's fade
constexpr float kNodeReach2 = 1.0f;        // a spawn node of the game's within 1 m of the layer's place is that one
constexpr int kEdf = 1;                    // the EDF's team
constexpr int kKillLogEvery = 5;
constexpr int kStampMs = 6000;             // the COMPLETE stamp (hud_ui_missions_process_activity_status: 6000 ms)
constexpr float kDeadResultsSeconds = 10.0f;  // ours: the most the results wait to come up over a dead player
constexpr float kLeaveSeconds = 5.0f;      // ours: the most a retry waits for the player to get out of a vehicle
constexpr float kReopenGraceSeconds = 10.0f;  // ours: after guerrilla_save's reload, the most the retry waits for the world
constexpr float kStartOnLoadMost = 30.0f;  // ours: the most the retry's restart waits for the player in the world
// Ours: the given walker or tank is made at its spawn node as the run starts, and ls_ad_04's node is 5.3 m from the start
// object, within the use prompt's 5 m: a player standing there had it made on him (2026-11-21: 1.4 m away, it landed on
// him). Behind the start's fade, a player within this of the node is moved to the start object's player_start first.
constexpr float kGivenClear = 8.0f;

std::vector<Row> g_rows;
std::vector<Site> g_sites;
std::unordered_map<unsigned, bool> g_done;  // completed, by the node's handle and by its start object's
// A retry's restart (Guerrilla's Activity_node_start_on_load): the node to start at once the world has opened again.
unsigned g_startOnLoad = 0xFFFFFFFF;
bool g_reopened = false;
float g_startOnLoadWaited = 0.0f;

// Twelve numbers (a place and a turn) from a property, true if all are there.
bool ReadPlaceAndTurn(const std::string& text, float* to) {
    float v[12];
    if (sscanf_s(text.c_str(), "%f %f %f %f %f %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9],
                 &v[10], &v[11]) != 12)
        return false;
    memcpy(to, v, sizeof(v));
    return true;
}

const Row* FindRow(const std::string& name) {
    for (const Row& row : g_rows)
        if (_stricmp(row.name.c_str(), name.c_str()) == 0) return &row;
    return nullptr;
}

void LoadRows() {
    std::vector<tables::Element> rows;
    if (!tables::Load("activity_area_defense", rows)) return;
    for (const tables::Element& entry : rows) {
        Row row;
        row.name = entry.Text("Name");
        row.baseData = entry.Text("Base_Data");
        row.radius = entry.Number("Area_radius", 50.0f);
        row.requiredKills = entry.Int("Required_kills", 15);
        row.vehicles = _stricmp(entry.Text("Kill_type", "Vehicles").c_str(), "Humans") != 0;
        if (const tables::Element* list = entry.Child("Reinforcements"))
            for (const tables::Element& item : list->children)
                if (row.stages.size() < 5) row.stages.push_back({item.Text("Continuous_attack"), item.Int("Kill_Limit", 99), item.Int("Time_Limit", 9999)});
        if (const tables::Element* rewards = entry.Child("Standard_Rewards")) {
            row.rewards.control = rewards->Number("EDF_Control_Reduction");
            row.rewards.morale = rewards->Number("Morale_Increase");
            row.rewards.salvage = rewards->Int("Salvage_Reward");
        }
        if (!row.name.empty()) g_rows.push_back(row);
    }
    rfa::LogDetail("activity_area_defense.xtbl: %u area defenses", static_cast<unsigned>(g_rows.size()));
}

bool LoadDone(const world::Object& node) {
    return save::Int(save::ForObject("area_defense", node.handle).c_str(), "complete", node.Property("area_defense_completed") == "true") != 0;
}

void MarkDone(const Site& site, bool done) {
    g_done[site.node->handle] = done;
    if (site.start) g_done[site.start->handle] = done;
}

// The sites from the campaign's markers: each node with its row, its start object (the activity_spawn of the same row)
// and its layer's given spawn node (the activity layers' build lists them with the layer: the row's name).
void FindSites() {
    const std::vector<const world::Object*> starts = world::OfClass("object_activity_spawn");
    std::vector<const world::Object*> given = world::OfClass("object_vehicle_spawn_node");
    for (const world::Object* turret : world::OfClass("object_turret_spawn_node")) given.push_back(turret);
    int withoutStart = 0, withoutGiven = 0;
    for (const world::Object* node : world::OfClass("object_area_defense_node")) {
        Site site;
        site.node = node;
        site.row = FindRow(node->Property("area_defense_type"));
        if (!site.row) {
            rfa::Log("Area defense node %08X: no row \"%s\" in activity_area_defense.xtbl", node->handle, node->Property("area_defense_type").c_str());
            continue;
        }
        for (const world::Object* start : starts)
            if (_stricmp(start->Property("activity_type").c_str(), site.row->name.c_str()) == 0) site.start = start;
        if (site.start) {
            site.hasPlayerStart = ReadPlaceAndTurn(site.start->Property("child_player_start"), site.playerStart);
            if (!site.hasPlayerStart) ReadPlaceAndTurn(site.start->Property("op"), site.playerStart);
        }
        for (const world::Object* spawn : given)
            if (_stricmp(spawn->Property("layer").c_str(), site.row->baseData.c_str()) == 0) {
                site.hasGiven = true;
                site.givenIsTurret = spawn->objectClass == "object_turret_spawn_node";
                site.given = spawn->pos;
                site.givenName = spawn->Property(site.givenIsTurret ? "turret_type" : "spawn_set");
            }
        withoutStart += site.start ? 0 : 1;
        withoutGiven += site.hasGiven ? 0 : 1;
        g_sites.push_back(site);
        MarkDone(g_sites.back(), LoadDone(*node));
    }
    int done = 0;
    for (const Site& site : g_sites) done += g_done[site.node->handle] ? 1 : 0;
    rfa::LogDetail("Area defense: %u sites, %d done%s%s", static_cast<unsigned>(g_sites.size()), done,
             withoutStart ? " (some without a start object: build the people again)" : "",
             withoutGiven ? " (some without their layer's given vehicle: build the world again)" : "");
}

// ---- Armageddon's calls

void* const kWorld = reinterpret_cast<void*>(0x1870630);
// The fade to black and back (as demolitions master's: 0x578790 darkens, 0x578720 lightens, 0x578830 black yet).
auto const FadeOut = reinterpret_cast<void(__cdecl*)(int unused, float seconds)>(0x578790);
auto const FadeIn = reinterpret_cast<void(__cdecl*)(int a, int b, float seconds)>(0x578720);
auto const IsBlack = reinterpret_cast<bool(__cdecl*)()>(0x578830);
// The use key, as the game reads it (whatever the player bound).
auto const UseAction = reinterpret_cast<int(__cdecl*)(int player)>(0x4E1B70);
auto const ButtonHeld = reinterpret_cast<bool(__cdecl*)(int action, int player)>(0x4E4800);
auto& g_localPlayer = *reinterpret_cast<const int*>(0x11C0580);
// Out of a vehicle (vehicle_interaction_request_exit 0x656A20), and an object destroyed (world::destroy_object 0x6D5450).
auto const RequestExit = reinterpret_cast<bool(__cdecl*)(BYTE* human, BYTE* exit)>(0x656A20);
auto const DestroyObject = reinterpret_cast<bool(__fastcall*)(void* world, void* unused, BYTE* object, bool flag)>(0x6D5450);
// The console's warp (as demolitions master's): the zone at a place (0x6CB3C0 on the world: null outside the loaded
// ones), the player's own move (his virtual +0x28: place, orientation, true), his velocity zeroed (0x850990), then
// 0x7B0860(0, 1).
auto const ZoneAt = reinterpret_cast<void*(__fastcall*)(void* world, void* unused, const float* place)>(0x6CB3C0);
using MoveFn = void(__fastcall*)(BYTE* object, void* unused, const float* place, const float* orientation, bool flag);
auto const SetVelocity = reinterpret_cast<void(__cdecl*)(BYTE* human, const float* velocity)>(0x850990);
auto const AfterWarp = reinterpret_cast<void(__fastcall*)(BYTE* human, void* unused, int a, int b)>(0x7B0860);
// The console's commands (as the continuous attack's): a command registered by name (0xC7BD70), whether one runs now.
using RegisterCommandFn = void*(__fastcall*)(void* command, void* unused, const char* name, const char* description, int type, void (*handler)());
auto const RegisterCommand = reinterpret_cast<RegisterCommandFn>(0xC7BD70);
auto& g_commandRunning = *reinterpret_cast<int*>(0x28FF1C0);
BYTE g_stampCommand[32];
void StampCommand();
// The world's vehicle spawn nodes (0x804C00 how many, 0x804C50 each; an object_vehicle_spawn_node's vtable 0xFC0E64,
// its vehicle's handle at +0xA8).
auto const NodeCount = reinterpret_cast<int(__fastcall*)(void* world, void* unused)>(0x804C00);
auto const NodeAt = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int index)>(0x804C50);
constexpr DWORD kNodeVtable = 0xFC0E64, kNodeVehicle = 0xA8;
using ConsolePrintFn = void(__cdecl*)(const char* format, ...);
auto const ConsolePrint = reinterpret_cast<ConsolePrintFn>(0xC7C440);
constexpr DWORD kHandle = 0x68, kObjectType = 0x7A, kInVehicle = 0x1160, kTeam = 0x13B4;
constexpr int kHumanType = 0, kVehicleType = 3;
constexpr DWORD kNone = 0xFFFFFFFF;

bool Dead(BYTE* object) {
    using IsDeadFn = bool(__thiscall*)(void* object);
    return reinterpret_cast<IsDeadFn>((*reinterpret_cast<void***>(object))[4])(object);
}
DWORD HandleOf(const BYTE* object) { return *reinterpret_cast<const DWORD*>(object + kHandle); }
float FlatDistance(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}
float Distance2(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// The vehicle the layer's spawn node made (the game's node at the layer's place), or null.
BYTE* GivenVehicle(const Site& site) {
    if (!site.hasGiven || site.givenIsTurret) return nullptr;
    const int count = NodeCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* node = NodeAt(kWorld, nullptr, i);
        if (!node || *reinterpret_cast<const DWORD*>(node) != kNodeVtable || Distance2(PositionOf(node), site.given) > kNodeReach2) continue;
        const DWORD handle = *reinterpret_cast<const DWORD*>(node + kNodeVehicle);
        BYTE* vehicle = handle != kNone ? static_cast<BYTE*>(ObjectFromHandle(handle)) : nullptr;
        return vehicle && vehicle[kObjectType] == kVehicleType ? vehicle : nullptr;
    }
    return nullptr;
}

// Guerrilla's area_defense_active_states, with the loading's fade in front, and ours for a loss: the results screen up,
// and a retry's steps.
enum class Phase { None, Fading, WaitPlayer, Running, WaitSplash, Cleanup, Results, Retry };

class AreaDefenseActivity : public Activity {
public:
    AreaDefenseActivity() : Activity(AreaDefense, true, false) {}

    // activity_area_defense_start: every other activity killed, the run's layers loading.
    bool StartAt(const Site& site, const char* how) {
        if (!site.row) return false;
        for (int mode = 1; mode < kModes; ++mode)
            if (Activity* other = Get(static_cast<Mode>(mode)); other && other != this && other->GetState() != State::Wait) other->KillNow();
        const BaseData* data = FindBaseData(site.row->baseData);
        const int district = liberation::DistrictAt(site.node->pos);
        if (!StartSpawn(site.node->handle, data, district >= 0 ? district : liberation::PlayerDistrict())) {
            rfa::Log("Area defense %s: the framework didn't start it (state %d, cooldown %.0f s)", site.row->name.c_str(), static_cast<int>(GetState()), CooldownLeft());
            return false;
        }
        site_ = &site;
        phase_ = Phase::None;
        rfa::LogDetail("Area defense %s: started (%s) at node %08X: %d %s to destroy within %.0f m, %u stages; %s%s", site.row->name.c_str(), how, site.node->handle,
                 site.row->requiredKills, site.row->vehicles ? "EDF vehicles" : "EDF soldiers", site.row->radius, static_cast<unsigned>(site.row->stages.size()),
                 layersRequested_ ? "its layers loading" : "ITS LAYER SET ISN'T IN THE WORLD (no given vehicle; build the world again)",
                 site.hasGiven ? (std::string(", given ") + site.givenName).c_str() : "");
        return true;
    }

    void Reset() override {
        Activity::Reset();
        site_ = nullptr;
        phase_ = Phase::None;
        level_ = -1;
        vehicleKills_ = humanKills_ = 0;
        fleeEndsAt_ = -1;
        fadedOut_ = cleanedUp_ = false;
        won_ = false;
        awaiting_ = false;
        deadWaited_ = retryWaited_ = 0.0f;
        exitAsked_ = false;
    }

    // A new level (a retry's reload among them): the run goes with it; a retry's restart is set up from now on.
    void LevelInit() override {
        static bool commandAdded = false;
        if (!commandAdded) {
            commandAdded = true;
            RegisterCommand(g_stampCommand, nullptr, "rfg_ad_stamp", "Shows area defense's corner panel and COMPLETE stamp for 6 s (to see them; nothing else changes)",
                            3, &StampCommand);
        }
        Activity::LevelInit();
        suspended_ = false;
        if (g_startOnLoad != kNone) {
            g_reopened = true;
            g_startOnLoadWaited = 0.0f;
        }
    }

    // Told of every death (activity_register_death); counted only while running (register_death 0x769FD0).
    bool Listens() const override { return phase_ == Phase::Running; }
    void RegisterDeath(void* victim, void* /*killer*/) override {
        if (phase_ != Phase::Running || !victim || !site_) return;
        const BYTE* object = static_cast<const BYTE*>(victim);
        const int type = static_cast<signed char>(object[kObjectType]);
        if (type == kVehicleType && alert_sources::VehicleTeam(object) == kEdf) ++vehicleKills_;
        else if (type == kHumanType && *reinterpret_cast<const int*>(object + kTeam) == kEdf) ++humanKills_;
        else return;
        const bool counts = site_->row->vehicles == (type == kVehicleType);
        if (counts && Kills() % kKillLogEvery == 0)
            rfa::LogDetail("Area defense %s: %d / %d %s", site_->row->name.c_str(), Kills(), site_->row->requiredKills, site_->row->vehicles ? "vehicles" : "soldiers");
    }

    // The use prompt at a start object (Guerrilla's icon 1, the default hand), 0 none.
    int UsePrompt() const { return offer_ ? 1 : 0; }

    // The running run's ring (render_regions 0x76A020: green while this mode is the current one).
    bool Ring(Vector& center, float& radius) const {
        if (!site_ || !IsActive() || phase_ == Phase::None || phase_ == Phase::Fading) return false;
        center = site_->node->pos;
        radius = site_->row->radius;
        return true;
    }

    // As the player dies: nothing ends yet (the out-of-ring timer stops). The run stands still while he lies dead
    // until his death timer runs out: TakePlayerDeath.
    void OnPlayerDeath() {
        if (phase_ != Phase::Running && phase_ != Phase::WaitPlayer) return;
        fleeEndsAt_ = -1;
        rfa::LogDetail("Area defense %s: the player died (%d / %d); the run waits for his death timer", site_->row->name.c_str(), Kills(),
                 site_->row->requiredKills);
    }

    // His death timer ran out (activity_area_defense_process_player_death 0x757100): lost, the results screen in place
    // of the death screen. True when it took the death; false (the game-over screen then) when no run is on, or the
    // results screen can't come up.
    bool TakePlayerDeath() {
        if (!site_ || !IsActive() || (phase_ != Phase::Running && phase_ != Phase::WaitPlayer)) return false;
        deadWaited_ = 0.0f;
        Lose(PlayerDied);
        return awaiting_;
    }

    // The results screen's answer (check_for_user_input_complete: the first hint after a failure is RETRY, the second
    // EXIT). EXIT: the clean-up (activity_area_defense_exit); with the player dead, he wakes at the nearest safehouse
    // (world::save_and_reload: guerrilla_save's respawn).
    void Choose(int choice) {
        if (!awaiting_) return;
        awaiting_ = false;
        BYTE* player = static_cast<BYTE*>(Player());
        if (choice == 1) {
            phase_ = Phase::Retry;
            retryWaited_ = 0.0f;
            exitAsked_ = false;
            rfa::LogDetail("Area defense %s: RETRY chosen", site_->row->name.c_str());
            return;
        }
        if (player && Dead(player)) {
            rfa::LogDetail("Area defense %s: EXIT with the player dead: he wakes at the nearest safehouse (Guerrilla's save and reload)", site_->row->name.c_str());
            End();
            if (!GuerrillaSave_Respawn()) rfa::Log("Area defense: guerrilla_save couldn't respawn him (its lines say why)");
            return;
        }
        rfa::LogDetail("Area defense %s: EXIT chosen: the clean-up", site_->row->name.c_str());
        StartCleanup();
    }

    // For guerrilla_hud: the corner panel while it runs (MissionInfo, hud_ui_missions_get_activity_area_defense_info
    // 0x896B50: HEAVY METAL, the subtitle while running, "kills / required", the vehicles (0) or soldiers (1) icon), the
    // out-of-ring timer (HudTimerInfo, "m:ss"), and a won run's COMPLETE stamp for 6 s (MissionStatus status 1,
    // MISS_COMPLETE_MALE, the seconds left), which outlasts the run's end.
    void Fill(Screen& screen) const {
        screen = {};
        const int now = GameTime();
        const bool stamping = stampEndsAt_ > now && stampEndsAt_ - now <= kStampMs;
        const bool running = site_ && IsActive() && (phase_ == Phase::WaitPlayer || phase_ == Phase::Running || phase_ == Phase::WaitSplash ||
                                                      phase_ == Phase::Cleanup);
        if (!running && !stamping) return;
        screen.mode = AreaDefense;
        screen.screen = 2;
        screen.title = "ACT_AREA_DEFENSE_NAME";
        if (running) {
            const Row& row = *site_->row;
            screen.objective = phase_ == Phase::Running ? (row.vehicles ? "ACT_AREA_DEFENSE_VEHICLE_SUBTITLE" : "ACT_AREA_DEFENSE_HUMANS_SUBTITLE") : "";
            screen.status = row.vehicles ? 0 : 1;
            char counter[32];
            sprintf_s(counter, "%i / %i", Kills(), row.requiredKills);
            screen.distance = counter;
            if (phase_ == Phase::Running && fleeEndsAt_ >= 0) {
                const int left = (std::max)(0, (fleeEndsAt_ - now + 999) / 1000);  // script_timer_hud_message_display: ceil seconds
                char timer[16];
                sprintf_s(timer, "%d:%02d", left / 60, left % 60);
                screen.timer = timer;
                screen.outOfRange = true;
            }
            screen.center = site_->node->pos;
            screen.range = row.radius;
        } else {
            screen.status = stampVehicles_ ? 0 : 1;
            screen.distance = stampCounter_;
        }
        if (stamping) {
            screen.success = true;
            screen.result = "MISS_COMPLETE_MALE";
            screen.elapsedMs = stampEndsAt_ - now;  // the stamp's time left
        }
    }

    // The console's rfg_ad_stamp: a won run's corner panel and stamp, for 6 s, with no run (to see them).
    void TestStamp() {
        stampEndsAt_ = GameTime() + kStampMs;
        stampVehicles_ = false;
        stampCounter_ = "60 / 60";
        rfa::LogDetail("Area defense: the console's rfg_ad_stamp: the COMPLETE stamp for %d s", kStampMs / 1000);
    }

    // A retry's restart (activity_force_start_generic once the world opened again from the retry's save): started at
    // the node, once guerrilla_save's reload is through and the player is in the world.
    void ForceStartOnLoad(float seconds) {
        g_startOnLoadWaited += seconds;
        if (!g_reopened) {
            if (GuerrillaSave_Reloading()) return;
            if (g_startOnLoadWaited < kReopenGraceSeconds) return;
            rfa::Log("Area defense: the retry's reload didn't come within %.0f s (guerrilla_save's lines say why); not started again", kReopenGraceSeconds);
            g_startOnLoad = kNone;
            return;
        }
        BYTE* player = static_cast<BYTE*>(Player());
        const Site* site = nullptr;
        for (const Site& candidate : g_sites)
            if (candidate.node->handle == g_startOnLoad) site = &candidate;
        if (!site) {
            g_startOnLoad = kNone;
            return;
        }
        if (!player || Dead(player) || GetState() != State::Wait || !CanInitialize()) {
            if (g_startOnLoadWaited < kStartOnLoadMost) return;
            rfa::Log("Area defense %s: the retry couldn't start again within %.0f s (state %d)", site->row->name.c_str(), kStartOnLoadMost,
                     static_cast<int>(GetState()));
            g_startOnLoad = kNone;
            return;
        }
        g_startOnLoad = kNone;
        StartAt(*site, "a retry");
    }

protected:
    void FrameInactive(float seconds) override {
        if (g_startOnLoad != kNone) {
            ForceStartOnLoad(seconds);
            return;
        }
        // The offer: the nearest start object within 5 m, on foot, its node not done and its district not liberated.
        offer_ = nullptr;
        BYTE* player = static_cast<BYTE*>(Player());
        if (!player || Dead(player)) return;
        const Vector here = PositionOf(player);
        const bool canStart = !AnyRunning() && CanInitialize() && liberation::AlertLevel() < handles::AlertOrange &&
                              *reinterpret_cast<const DWORD*>(player + kInVehicle) == kNone;
        float nearest = kUseReach * kUseReach;
        for (const Site& site : g_sites) {
            if (!site.start || g_done[site.node->handle]) continue;
            const int district = liberation::DistrictAt(site.node->pos);
            if (const liberation::District* d = liberation::GetDistrict(district); d && d->liberated) continue;
            const float distance2 = Distance2(site.start->pos, here);
            if (distance2 < kHereReach * kHereReach && nearSite_ != site.node->handle) {
                nearSite_ = site.node->handle;
                ConsolePrint("Area defense here: %s (%d %s, %.0f m ring)\n", site.row->name.c_str(), site.row->requiredKills,
                             site.row->vehicles ? "vehicles" : "soldiers", site.row->radius);
            }
            if (canStart && distance2 < nearest) {
                nearest = distance2;
                offer_ = &site;
            }
        }
        const bool use = ButtonHeld(UseAction(g_localPlayer), g_localPlayer);
        if (use && !useDown_ && offer_) {
            const Site* site = offer_;
            offer_ = nullptr;
            StartAt(*site, "the use key at its start");
        }
        useDown_ = use;
    }

    // READY (do_frame_load_layers): the fade to black, then the start-up behind it.
    void FrameReady(float seconds) override {
        if (phase_ == Phase::None) {
            phase_ = Phase::Fading;
            fadeTime_ = 0.0f;
            FadeOut(0, kFadeSeconds);
            fadedOut_ = true;
            return;
        }
        fadeTime_ += seconds;
        if (!IsBlack() && fadeTime_ < kFadeWaitMost) return;
        ClearGivenSpot();
        if (!BeginStartUp()) End();
    }

    // Ours (see kGivenClear): behind the fade, a player standing on the given vehicle's spot moved to the start object's
    // player_start (Guerrilla's start for the site, where a retry puts him), so it isn't made on him.
    void ClearGivenSpot() {
        BYTE* player = static_cast<BYTE*>(Player());
        if (!player || !site_ || !site_->hasGiven || !site_->start) return;
        const float away = FlatDistance(PositionOf(player), site_->given);
        if (away >= kGivenClear) return;
        const Vector to{site_->playerStart[0], site_->playerStart[1], site_->playerStart[2]};
        if (FlatDistance(to, site_->given) < away || !ZoneAt(kWorld, nullptr, &to.x)) {
            rfa::LogDetail("Area defense %s: the player is %.1f m from the given vehicle's spot; its player_start isn't farther or loaded, so he stays",
                     site_->row->name.c_str(), away);
            return;
        }
        const MoveFn move = reinterpret_cast<MoveFn>((*reinterpret_cast<void***>(player))[0x28 / 4]);
        move(player, nullptr, &to.x, site_->playerStart + 3, true);
        const float still[3] = {0, 0, 0};
        SetVelocity(player, still);
        AfterWarp(player, nullptr, 0, 1);
        rfa::LogDetail("Area defense %s: the player was %.1f m from the given vehicle's spot; moved to the start's player_start (%.1f m from it)",
                 site_->row->name.c_str(), away, FlatDistance(to, site_->given));
    }

    bool Start() override {
        if (!Activity::Start()) return false;  // the layer's objects made (its given vehicle or turret)
        // activity_begin_start_up: the diversions suspended (all the free ones but the continuous attack), the counts zeroed.
        SuspendDiversions(true);
        vehicleKills_ = humanKills_ = 0;
        level_ = -1;
        fleeEndsAt_ = -1;
        phase_ = Phase::WaitPlayer;
        if (fadedOut_) {
            FadeIn(1, 0, kFadeSeconds);
            fadedOut_ = false;
        }
        ConsolePrint("Area defense %s: destroy %d %s; stay within %.0f m\n", site_->row->name.c_str(), site_->row->requiredKills,
                     site_->row->vehicles ? "EDF vehicles" : "EDF soldiers", site_->row->radius);
        return true;
    }

    void FrameActive(float seconds) override {
        if (!site_) return;
        const int now = GameTime();
        BYTE* player = static_cast<BYTE*>(Player());
        switch (phase_) {
        case Phase::WaitPlayer:
            if (player && !Dead(player) && FlatDistance(PositionOf(player), site_->node->pos) <= site_->row->radius) {
                phase_ = Phase::Running;
                runStart_ = now;
                rfa::LogDetail("Area defense %s: running (the player inside the %.0f m ring)", site_->row->name.c_str(), site_->row->radius);
                StepStages(now, player);
            }
            break;
        case Phase::Running:
            if (!player || Dead(player)) break;
            if (FlatDistance(PositionOf(player), site_->node->pos) > site_->row->radius) {
                if (fleeEndsAt_ < 0) {
                    fleeEndsAt_ = now + kFleeMs;
                    rfa::LogDetail("Area defense %s: out of the ring: 10 s to come back", site_->row->name.c_str());
                    ConsolePrint("Area defense: 10 seconds to return to the area\n");
                } else if (now >= fleeEndsAt_) {
                    return Lose(OutOfRange);
                }
            } else if (fleeEndsAt_ >= 0) {
                fleeEndsAt_ = -1;
                rfa::LogDetail("Area defense %s: back in the ring", site_->row->name.c_str());
            }
            StepStages(now, player);
            if (Kills() >= site_->row->requiredKills) return Win();
            break;
        case Phase::WaitSplash:
            if (now >= splashEndsAt_) StartCleanup();
            break;
        case Phase::Results:
            FollowDeadResults(seconds);
            break;
        case Phase::Retry:
            StepRetry(seconds);
            break;
        case Phase::Cleanup:
            fadeTime_ += seconds;
            if (!IsBlack() && fadeTime_ < kFadeWaitMost) break;
            CleanUp(seconds);
            break;
        default:
            break;
        }
    }

    void End() override {
        SuspendDiversions(false);
        ContinuousAttackEnd("area defense ended");
        if (fadedOut_) {
            FadeIn(1, 0, kFadeSeconds);
            fadedOut_ = false;
        }
        phase_ = Phase::None;
        Activity::End();
    }

private:
    int Kills() const { return site_ && site_->row->vehicles ? vehicleKills_ : humanKills_; }

    // test_reinforcement_conditions: each stage whose kills or seconds are reached starts its continuous attack in turn
    // (the old one ended); the first stage (0 / 0) at once.
    void StepStages(int now, BYTE* player) {
        const int kills = Kills();
        const int seconds = (now - runStart_) / 1000;
        while (level_ + 1 < static_cast<int>(site_->row->stages.size())) {
            const Stage& next = site_->row->stages[static_cast<size_t>(level_ + 1)];
            if (kills < next.killLimit && seconds < next.timeLimit) break;
            ++level_;
            if (level_ > 0) ContinuousAttackEnd("area defense: the next stage");
            const bool started = ContinuousAttackStart(HandleOf(player), next.attack.c_str(), site_->row->name.c_str());
            rfa::LogDetail("Area defense %s: stage %d of %u (%d kills, %d s): %s%s", site_->row->name.c_str(), level_ + 1,
                     static_cast<unsigned>(site_->row->stages.size()), kills, seconds, next.attack.c_str(), started ? "" : " DIDN'T START");
        }
    }

    // start_cleanup 0x756DE0: the fade out; the clean-up once black.
    void StartCleanup() {
        phase_ = Phase::Cleanup;
        fadeTime_ = 0.0f;
        exitTime_ = 0.0f;
        cleanedUp_ = false;
        FadeOut(0, kFadeSeconds);
        fadedOut_ = true;
    }

    // record_victory: the rewards in the player's district, the node completed; the clean-up 2 s later.
    void Win() {
        won_ = true;
        phase_ = Phase::WaitSplash;
        splashEndsAt_ = GameTime() + kSplashMs;
        stampEndsAt_ = GameTime() + kStampMs;
        stampVehicles_ = site_->row->vehicles;
        char counter[32];
        sprintf_s(counter, "%i / %i", Kills(), site_->row->requiredKills);
        stampCounter_ = counter;
        const int district = liberation::PlayerDistrict();
        if (district >= 0) district_ = district;
        RecordVictory(site_->row->rewards);
        MarkDone(*site_, true);
        save::SetInt(save::ForObject("area_defense", site_->node->handle).c_str(), "complete", 1);
        rfa::LogDetail("Area defense %s: won (%d %s): EDF control -%.0f, salvage %d; the node completed", site_->row->name.c_str(), Kills(),
                 site_->row->vehicles ? "vehicles" : "soldiers", site_->row->rewards.control, site_->row->rewards.salvage);
        ConsolePrint("Area defense %s: complete\n", site_->row->name.c_str());
    }

    // Lost (process_completion_condition: left the area, the player died): the loss recorded, the results screen
    // (hud_ui_activity_complete_start(16, false), the game log's reason); with none, the clean-up at once.
    void Lose(EndReason reason) {
        won_ = false;
        fleeEndsAt_ = -1;
        RecordLoss();
        const char* why = reason == OutOfRange ? "left the area" : reason == PlayerDied ? "the player died" : "failed";
        rfa::LogDetail("Area defense %s: lost (%s) at %d / %d", site_->row->name.c_str(), why, Kills(), site_->row->requiredKills);
        ConsolePrint("Area defense %s: failed (%s)\n", site_->row->name.c_str(), why);
        GuerrillaHudActivityResults results{};
        results.size = sizeof(results);
        results.mode = AreaDefense;
        results.success = 0;
        results.reason = static_cast<int>(reason);
        phase_ = Phase::Results;
        awaiting_ = GuerrillaHud_ActivityComplete(&results) != 0;
        if (!awaiting_) {
            rfa::LogDetail("Area defense %s: no results screen; the clean-up", site_->row->name.c_str());
            if (reason == PlayerDied) {
                phase_ = Phase::None;
                End();
            } else {
                StartCleanup();
            }
        }
    }

    // Ours: the results screen asked for over a dead player that doesn't come up: after 10 s of play the game-over
    // screen instead, so that he isn't left dead with nothing to choose (guerrilla_hud's lines say why).
    void FollowDeadResults(float seconds) {
        BYTE* player = static_cast<BYTE*>(Player());
        if (!awaiting_ || !player || !Dead(player)) return;
        deadWaited_ += seconds;
        if (deadWaited_ < kDeadResultsSeconds) return;
        rfa::Log("Area defense %s: the results screen didn't come up in %.0f s with the player dead; Armageddon's game-over screen instead",
                 site_->row->name.c_str(), kDeadResultsSeconds);
        awaiting_ = false;
        End();
        GuerrillaSave_GameOver();
    }

    // RETRY's steps: the player out of a vehicle (ours, as the Transporter's), then the replay.
    void StepRetry(float seconds) {
        BYTE* player = static_cast<BYTE*>(Player());
        if (player && !Dead(player) && *reinterpret_cast<const DWORD*>(player + kInVehicle) != kNone) {
            if (!exitAsked_) {
                BYTE exit[4] = {};
                RequestExit(player, exit);
                exitAsked_ = true;
            }
            retryWaited_ += seconds;
            if (retryWaited_ < kLeaveSeconds) return;
            rfa::LogDetail("Area defense %s: RETRY: the player is still in a vehicle after %.0f s; saved as he is", site_->row->name.c_str(), kLeaveSeconds);
        }
        Replay(player);
    }

    // activity_area_defense_replay 0x7570E0: the node kept for the reload (Activity_node_start_on_load), the world saved
    // with the player at the start object's player_start and opened again (guerrilla_save). The run ends here; the
    // reload starts it again (ForceStartOnLoad).
    void Replay(BYTE* player) {
        const Site& site = *site_;
        const bool dead = player && Dead(player);
        if (!GuerrillaSave_ReloadAt(site.playerStart, site.playerStart + 3)) {
            rfa::Log("Area defense %s: RETRY: the save and reload couldn't be asked (guerrilla_save off, not in its world, or a respawn or reload "
                     "already waiting); the run ends as EXIT", site.row->name.c_str());
            if (dead) {
                End();
                GuerrillaSave_Respawn();
            } else {
                StartCleanup();
            }
            return;
        }
        g_startOnLoad = site.node->handle;
        g_reopened = false;
        g_startOnLoadWaited = 0.0f;
        rfa::LogDetail("Area defense %s: RETRY: the world saved with the player at its start (%.1f, %.1f, %.1f%s)%s and opened again", site.row->name.c_str(),
                 site.playerStart[0], site.playerStart[1], site.playerStart[2], site.hasPlayerStart ? ", its player_start" : ", the start object",
                 dead ? " after his death" : "");
        End();
        ClearCooldown();  // Guerrilla's reload starts its level with no cooldown (level_init); ours isn't in the save
    }

    // do_frame_cleanup / player_equipment_cleanup, behind the fade: the attack ended and its NPCs taken away, the player
    // out of the given walker or tank, it destroyed; then the end and the fade back in.
    void CleanUp(float seconds) {
        if (!cleanedUp_) {
            cleanedUp_ = true;
            ContinuousAttackClear("area defense's clean-up");
            if (site_->givenIsTurret)
                rfa::LogDetail("Area defense %s: its turret isn't taken from the player yet (the layer's release takes it away)", site_->row->name.c_str());
        }
        BYTE* player = static_cast<BYTE*>(Player());
        BYTE* given = GivenVehicle(*site_);
        if (player && given && *reinterpret_cast<const DWORD*>(player + kInVehicle) == HandleOf(given)) {
            // warp_player_on_end: out of a tank or walker (vehicle_interaction_request_exit).
            if (exitTime_ == 0.0f) {
                BYTE exit[4] = {};
                RequestExit(player, exit);
            }
            exitTime_ += seconds;
            if (exitTime_ < kExitWaitMost) return;
            rfa::LogDetail("Area defense %s: the player is still in its vehicle after %.0f s; it stays", site_->row->name.c_str(), kExitWaitMost);
        } else if (given) {
            DestroyObject(kWorld, nullptr, given, false);
        }
        rfa::LogDetail("Area defense %s: cleaned up (%s)", site_->row->name.c_str(), won_ ? "won" : "lost");
        End();
    }

    // activity_suspend_diversions / reinstate (the free activities but the continuous attack, which the stages use).
    void SuspendDiversions(bool suspend) {
        if (suspended_ == suspend) return;
        suspended_ = suspend;
        for (int mode = 1; mode < kModes; ++mode) {
            Activity* other = Get(static_cast<Mode>(mode));
            if (!other || other->Modal() || other->GetMode() == ContinuousAttack || other->GetMode() == MissionVrail) continue;
            if (suspend) other->Disable(ByActivity);
            else other->Enable(ByActivity);
        }
    }

    const Site* site_ = nullptr;
    const Site* offer_ = nullptr;
    Phase phase_ = Phase::None;
    int level_ = -1;             // the stage running (-1 none yet)
    int vehicleKills_ = 0, humanKills_ = 0;
    int runStart_ = 0;           // the game's clock when RUNNING began
    int fleeEndsAt_ = -1;        // the out-of-ring timer's end (-1: inside)
    int splashEndsAt_ = 0;
    float fadeTime_ = 0.0f, exitTime_ = 0.0f, deadWaited_ = 0.0f, retryWaited_ = 0.0f;
    bool fadedOut_ = false, cleanedUp_ = false, won_ = false, suspended_ = false, awaiting_ = false, exitAsked_ = false;
    // A won run's COMPLETE stamp, which outlasts the run (not reset with it).
    int stampEndsAt_ = -1;
    bool stampVehicles_ = true;
    std::string stampCounter_;
    bool useDown_ = true;        // a key held when the offer comes isn't a press
    unsigned nearSite_ = 0xFFFFFFFF;
};

AreaDefenseActivity* g_areaDefense = nullptr;

}  // namespace

Activity* MakeAreaDefense() {
    LoadRows();
    world::Load();
    FindSites();
    g_areaDefense = new AreaDefenseActivity();
    return g_areaDefense;
}

int AreaDefenseUsePrompt() { return g_areaDefense ? g_areaDefense->UsePrompt() : 0; }

bool AreaDefenseRing(Vector& center, float& radius) { return g_areaDefense && g_areaDefense->Ring(center, radius); }

bool AreaDefenseNodeDone(unsigned handle) {
    const auto found = g_done.find(handle);
    return found != g_done.end() && found->second;
}

void AreaDefenseReloadProgress() {
    for (const Site& site : g_sites) MarkDone(site, LoadDone(*site.node));
}

void AreaDefensePlayerDied() {
    if (g_areaDefense) g_areaDefense->OnPlayerDeath();
}

bool AreaDefenseScreen(Screen& screen) {
    if (!g_areaDefense) return false;
    g_areaDefense->Fill(screen);
    return screen.mode != ModeNone;
}

void AreaDefenseChoose(int choice) {
    if (g_areaDefense) g_areaDefense->Choose(choice);
}

bool AreaDefenseTakePlayerDeath() { return g_areaDefense && g_areaDefense->TakePlayerDeath(); }

namespace {
void StampCommand() {
    if (!g_commandRunning) return;
    if (g_areaDefense) g_areaDefense->TestStamp();
    ConsolePrint("Area defense: the corner panel and COMPLETE stamp for 6 s (close the console)\n");
}
}  // namespace

void AreaDefenseReopened(const char* how) {
    if (g_startOnLoad == kNone || g_reopened) return;
    g_reopened = true;
    g_startOnLoadWaited = 0.0f;
    rfa::LogDetail("Area defense: the retry's reload is in (%s); the run starts again at its node", how);
}

}  // namespace campaign::activities
