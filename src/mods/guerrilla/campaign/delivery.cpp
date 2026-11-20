// Guerrilla's Transporter ("Guerrilla Express" inside its code: activity_delivery_data, mode 4, modal), on the framework.
//
// Each of its 18 start nodes (object_delivery_node with start_node true; campaign_objects.xml) has a parked car: the
// vehicle spawn node under it (child_vehicle_spawn_node: the world build places it, so the game parks the car there).
// Sitting in the car's driver's seat with the car within 5 m of the node starts the run (do_frame_inactive RM 0x797670):
// no prompt, fade, briefing or warp. Every other activity is killed, and the framework takes it through LOADING, READY
// and STARTING (activity_initialize 0x797170: its overrides; activity_do_frame_ready 0x76C510; activity_start 0x7AD670:
// the alert raised to the row's Alert_Level if lower, not locked; the tool tip Guerrilla_Express).
// The run (activity_do_frame_active 0x7B7D20): the clock (Time_To_Complete) waits until the car first moves at 0.5 m/s;
// out of the car the player has Time_To_Fail (10 s) to get back in; within 25 m of the end the alert value drops to 0
// once; arriving inside End_Radius (3D) at under 1.5 m/s wins: the player is put out of the car and the car stopped
// (record_victory 0x7819E0), the start node keeps completed, its best time and par beaten (the campaign's save, per
// node). Out of time (4), out of the car too long (5) and the car wrecked (6) lose (activity_lost 0x758330). The
// player's death: the run stands still while he lies dead, and as his death timer runs out it fails with no reason line
// in place of the death screen (gamestate_gp_simulate_frame → process_player_death; guerrilla_save asks, from
// Armageddon's game-over screen's start). Then Guerrilla's activity-complete screen (do_end_processing 0x76C5B0 →
// hud_ui_activity_complete_start: guerrilla_hud's GuerrillaHud_ActivityComplete) with the pro, target, best and your
// time; its answer (GuerrillaCampaign_ActivityCompleteChoose): after a win CONTINUE ends it and RETRY replays it,
// after a loss RETRY replays and EXIT ends it (0x12FB268 / 0x12FB290 / 0x12FB2B8, index 4; after EXIT a dead player
// wakes at the nearest safehouse: world::save_and_reload, guerrilla_save's GuerrillaSave_Respawn). Ending pays the rewards:
// the row's (salvage only in Guerrilla's rows) × 1 for a first win, + 1 for par first beaten (activity_guerrilla_
// express_exit 0x7B5E70). The corner panel (hud_ui_missions_get_activity_delivery_info 0x896910): TRANSPORTER, GET
// VEHICLE TO TARGETED SAFEHOUSE (GET BACK IN THE VEHICLE! while out of it), the time left (the shorter clock), the
// distance to the end. The minimap's blue End_Radius ring at the end (render_regions 0x76C780). The end's ground ring
// gp_marker_target_a while it runs.
// RETRY (activity_guerrilla_express_replay 0x7B5EE0): after a win it waits one call (Guerrilla's pending_retry, for a
// district-liberated screen; ours pauses the game, which stops the campaign's frame too); the rewards × their count;
// je_td_08 and je_td_09 rebuild their EDF garage (rfg_mover A60004AF, 9800004C: world::restore_object) when the player
// is dead and it stands damaged (not killed, not pristine); Activity_node_start_on_load = the start node; then
// world::save_and_reload with the player saved at the node's player_start (save_state_finish 0x93DE94 →
// activity_get_player_start 0x77F690): here guerrilla_save's GuerrillaSave_ReloadAt at the node's child_player_start
// (the player out of the car first: Armageddon's save with a warp is only known for a player on foot). After the
// reload, activity_force_start_generic(node, false) 0x7A8710: every other activity killed, the node's old car destroyed
// when more than 5 m from its spawn node, spawn_child_object(-1) for a fresh one, and the run is ready again as the
// player gets in.
// Not yet: the end-point map icon (6b), the GPS marker, the radio lines and music (steps 12b/12c), the countdown beeps
// (the interface's banks), the five rows' layers and scripts (chasers, roadblocks, alert triggers: steps 7, 13, 16),
// the NPC drivers kept out of the car, the weapon put back in the player's hands, the "Warp Speed" statistic, the start
// node switched off until it streams out after its car went (object_delivery_node::set_disabled), a random pick among
// several player_start children with activity_respawn (each route has one).
//
// Armageddon's: the vehicle spawn nodes (the world's list 0x804C00 / 0x804C50, vtable 0xFC0E64; +0xA8 its car's
// handle), a vehicle's seat occupant 0x603AE0 (seat 0 the driver), flags +0x4FB0 (bit 10 a wreck), hit points set by
// 0x609C40 (cdecl: the most, the handle) and 0x609680 (cdecl: the handle, the hit points), a human's vehicle +0x1160,
// vehicle_interaction_request_exit 0x656A20, vehicle_set_controls_when_abandoned 0x610820; for the retry a node's
// spawn_child_object 0x7FE5A0 (thiscall, -1 for one of its group), held back 0x801400, world::destroy_object 0x6D5450
// and world::restore_object 0x6CF800; a building's flags +0x150 (Guerrilla's +0x10C: bit 4 game_destroyed, which both
// games' is_killed read, bit 7 pristine).
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "campaign_api.h"
#include "hud/hud_api.h"
#include "save/save_api.h"
#include "activity_base.h"
#include "handle_kinds.h"
#include "liberation.h"
#include "messages.h"
#include "save.h"
#include "tables.h"
#include "world_objects.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace campaign::activities {
namespace {

// ---- Armageddon's objects

void* const kWorld = reinterpret_cast<void*>(0x1870630);
constexpr DWORD kNone = 0xFFFFFFFF;
constexpr DWORD kHandle = 0x68, kObjectType = 0x7A, kHumanVehicle = 0x1160, kVehicleFlags = 0x4FB0, kWreck = 1u << 10;
constexpr int kVehicleType = 3, kDriverSeat = 0;
auto const NodeCount = reinterpret_cast<int(__fastcall*)(void* world, void* unused)>(0x804C00);
auto const NodeAt = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int index)>(0x804C50);
constexpr DWORD kNodeVtable = 0xFC0E64, kNodeCar = 0xA8;
constexpr float kOnSpotSquared = 0.5f * 0.5f;
auto const SeatOccupant = reinterpret_cast<BYTE*(__cdecl*)(BYTE* vehicle, int seat)>(0x603AE0);
auto const SetMostHitPoints = reinterpret_cast<void(__cdecl*)(int most, DWORD vehicle)>(0x609C40);
auto const SetHitPoints = reinterpret_cast<void(__cdecl*)(DWORD vehicle, int hitPoints)>(0x609680);
auto const RequestExit = reinterpret_cast<bool(__cdecl*)(BYTE* human, BYTE* exit)>(0x656A20);
auto const StopCar = reinterpret_cast<void(__cdecl*)(BYTE* vehicle)>(0x610820);
// Effects (as car_bomb.cpp and people's props.cpp play them): the number by name, played at a place, stopped.
auto const EffectNumber = reinterpret_cast<int(__cdecl*)(const char* name)>(0x4EF370);
using EffectsPlayFn = int(__cdecl*)(int effect, const float* where, const float* turn, bool looping, DWORD host, int hostTag, DWORD source,
                                    bool onCamera, DWORD unused1, DWORD unused2);
auto const EffectsPlay = reinterpret_cast<EffectsPlayFn>(0x4EB460);
auto const EffectObject = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int handle)>(0x4D4360);
auto const StopEffect = reinterpret_cast<void(__fastcall*)(BYTE* effect, void* unused, int style)>(0x4F7AA0);
// The retry's (as guerrilla_vehicles' safehouses.cpp and demolitions_master.cpp call them).
auto const SpawnChild = reinterpret_cast<DWORD(__fastcall*)(BYTE* node, void* unused, DWORD vehicle)>(0x7FE5A0);
auto const NodeHeldBack = reinterpret_cast<bool(__fastcall*)(BYTE* node, void* unused)>(0x801400);
auto const DestroyObject = reinterpret_cast<bool(__fastcall*)(void* world, void* unused, BYTE* object, bool flag)>(0x6D5450);
auto const RestoreObject = reinterpret_cast<bool(__fastcall*)(void* world, void* unused, DWORD handle, bool flag)>(0x6CF800);
constexpr DWORD kObjectSubtype = 0x7B, kMoverFlags = 0x150, kPristine = 1u << 7;
constexpr int kMoverType = 2, kRfgMover = 1;

DWORD HandleOf(const BYTE* object) { return *reinterpret_cast<const DWORD*>(object + kHandle); }
float Squared(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}
float FlatSquared(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}
bool IsDead(BYTE* object) {
    using IsDeadFn = bool(__thiscall*)(void* object);
    return reinterpret_cast<IsDeadFn>((*reinterpret_cast<void***>(object))[4])(object);
}
BYTE* VehicleByHandle(DWORD handle) {
    BYTE* object = handle != kNone ? static_cast<BYTE*>(ObjectFromHandle(handle)) : nullptr;
    return object && object[kObjectType] == kVehicleType ? object : nullptr;
}

// ---- Guerrilla's numbers (activity_start / do_frame_active's constants)

constexpr float kStartReach = 5.0f;        // the car within 5 m of its start node (3D)
constexpr float kReadySquared = 25.0f;     // vehicle_is_ready_to_start: within 5 m of its spawn node (xz)
constexpr float kMovingSpeed = 0.5f;       // the clock starts once the car moves this fast (m/s)
constexpr float kStoppedSpeed = 1.5f;      // arrived: under this (m/s)
constexpr float kAlertDropReach = 25.0f;   // the alert value drops to 0 once within this of the end
constexpr int kTipMs = 2750;               // ui_tool_tips_add_tip(Guerrilla_Express, 2750)
constexpr int kDeliveryMode = 4;           // activity_modes DELIVERY (the activity-complete screen's mode)
constexpr float kNearSquared = 30.0f * 30.0f;  // the routes looked at: their start within 30 m of the player (ours)
// The end's ground ring (objective_highlight_ground_marker_add RM 0xB2E1E0): Guerrilla's gp_marker_target_a (the world
// build's rfg_ copy) at the end node, 0.02 m up, drawn by its render callback (0xB2DD00) with a transform scaled to
// End_Radius × 0.65 and faded by distance; here played with that scale in its orientation (I: that Armageddon's effects
// take the scale so). Guerrilla shows it while the end node is streamed in (object_delivery_node::stream); here while
// the player is within the zones' streaming reach of it.
const char* const kRingEffect = "rfg_gp_marker_target_a";
constexpr float kRingScale = 0.65f;
constexpr float kRingUp = 0.02f;
constexpr float kRingShowSquared = 340.0f * 340.0f, kRingHideSquared = 400.0f * 400.0f;
// The retry (activity_guerrilla_express_replay, activity_force_start_generic).
constexpr float kOldCarReach = 5.0f;          // force_start: the old car destroyed beyond this from its spawn node (3D)
constexpr float kLeaveCarSeconds = 5.0f;      // ours: the most the retry waits for the player to get out of the car
constexpr float kStartOnLoadSeconds = 30.0f;  // ours: the most it waits for the start's node after the reload
constexpr float kDeadResultsSeconds = 10.0f;  // ours: the most the results wait to come up over a dead player
constexpr float kReopenGraceSeconds = 10.0f;  // ours: after guerrilla_save's reload, the most the retry waits for the world's opening
struct Garage {
    const char* route;
    DWORD mover;
};
const Garage kGarages[] = {{"je_td_08", 0xA60004AF}, {"je_td_09", 0x9800004C}};  // TRGT_EDF_CONVOY_STATION, TRGT_EDF_GARAGE

// ---- activity_timed_delivery.xtbl

struct Definition {
    std::string name, subtitle, baseName;
    float speed = -1.0f;            // Vehicle_Overrides/Vehicle_Speed (no row has one)
    int mostHitPoints = -1;         // Vehicle_Overrides/Vehicle_Hit_Points
    float health = 1.0f;            // Vehicle_Overrides/Vehicle_Health
    float endRadius = 5.0f;         // End_Radius (the code's 5 when missing)
    int timeToComplete = 0;         // ms
    int timeToFail = 0;             // ms out of the car
    int parTime = 0;                // ms
    int alertLevel = -1;            // Alert_Level (-1 none: green, no raise)
    Rewards rewards;
};
std::vector<Definition> g_definitions;

int AlertFrom(const std::string& name) {
    static const char* const kNames[] = {"green", "yellow", "orange", "red"};
    for (int i = 0; i < 4; ++i)
        if (_stricmp(name.c_str(), kNames[i]) == 0) return i;
    return -1;
}

void LoadDefinitions() {
    std::vector<tables::Element> rows;
    if (!tables::Load("activity_timed_delivery", rows)) return;
    for (const tables::Element& row : rows) {
        Definition d;
        d.name = row.Text("Name");
        d.subtitle = row.Text("Subtitle");
        d.baseName = row.Text("base_activity_data");
        d.endRadius = row.Number("End_Radius", 5.0f);
        d.timeToComplete = static_cast<int>(row.Number("Time_To_Complete", 0.0f) * 1000.0f);
        d.timeToFail = static_cast<int>(row.Number("Time_To_Fail", 0.0f) * 1000.0f);
        d.parTime = row.Int("Par_Time", 0) * 1000;
        d.alertLevel = AlertFrom(row.Text("Alert_Level"));
        if (const tables::Element* overrides = row.Child("Vehicle_Overrides")) {
            d.speed = overrides->Number("Vehicle_Speed", -1.0f);
            d.mostHitPoints = overrides->Int("Vehicle_Hit_Points", -1);
            d.health = overrides->Number("Vehicle_Health", 1.0f);
        }
        if (const tables::Element* rewards = row.Child("Standard_Rewards")) {
            d.rewards.control = rewards->Number("EDF_Control_Reduction");
            d.rewards.morale = rewards->Number("Morale_Increase");
            d.rewards.salvage = rewards->Int("Salvage_Reward");
        }
        g_definitions.push_back(d);
    }
    rfa::Log("activity_timed_delivery.xtbl: %u deliveries", static_cast<unsigned>(g_definitions.size()));
}

const Definition* DefinitionNamed(const std::string& name) {
    for (const Definition& d : g_definitions)
        if (_stricmp(d.name.c_str(), name.c_str()) == 0) return &d;
    return nullptr;
}

// ---- The nodes

struct Route {
    const world::Object* start = nullptr;
    const world::Object* end = nullptr;
    const Definition* definition = nullptr;
    Vector spawnNode{};   // its car's vehicle spawn node (child_vehicle_spawn_node)
    bool hasSpawnNode = false;
    // Where a retry puts the player (activity_get_player_start): its player_start child's place and turn
    // (child_player_start), else the node's own (op).
    float playerStart[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
    bool hasPlayerStart = false;
};
std::vector<Route> g_routes;

// Guerrilla's Activity_node_start_on_load: the start node a retry's reload sets up again. Kept over the reload (the
// level's start leaves it); g_reopened once the world opened again.
unsigned g_startOnLoad = 0xFFFFFFFF;
DWORD g_retryOldCar = 0xFFFFFFFF;  // the run's car, for the start's set-up after the reload (Guerrilla's old vehicle)
bool g_reopened = false;
float g_startOnLoadWaited = 0.0f;

// Twelve numbers (a place and a turn) from a property, true if all are there.
bool ReadPlaceAndTurn(const std::string& text, float* to) {
    float v[12];
    const int read = sscanf_s(text.c_str(), "%f %f %f %f %f %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6],
                              &v[7], &v[8], &v[9], &v[10], &v[11]);
    if (read != 12) return false;
    memcpy(to, v, sizeof(v));
    return true;
}

struct Progress {
    bool completed = false, parBeaten = false;
    int best = 0;  // ms, 0 none
};
std::unordered_map<unsigned, Progress> g_progress;  // by the start node's handle

Progress LoadProgress(unsigned node) {
    const std::string section = save::ForObject("delivery", node);
    Progress p;
    p.completed = save::Int(section.c_str(), "completed", 0) != 0;
    p.parBeaten = save::Int(section.c_str(), "par_beaten", 0) != 0;
    p.best = save::Int(section.c_str(), "best_time", 0);
    return p;
}

void SaveProgress(unsigned node, const Progress& p) {
    g_progress[node] = p;
    const std::string section = save::ForObject("delivery", node);
    save::SetInt(section.c_str(), "completed", p.completed ? 1 : 0);
    save::SetInt(section.c_str(), "par_beaten", p.parBeaten ? 1 : 0);
    save::SetInt(section.c_str(), "best_time", p.best);
}

void LoadRoutes() {
    world::Load();
    const std::vector<const world::Object*> nodes = world::OfClass("object_delivery_node");
    for (const world::Object* start : nodes) {
        if (start->Property("start_node") != "true") continue;
        Route route;
        route.start = start;
        const std::string pair = start->Property("pair_number");
        for (const world::Object* end : nodes)
            if (end->Property("start_node") != "true" && end->Property("pair_number") == pair) route.end = end;
        route.definition = DefinitionNamed(start->Property("delivery_type"));
        const std::string spawn = start->Property("child_vehicle_spawn_node");
        unsigned handle = 0;
        route.hasSpawnNode = sscanf_s(spawn.c_str(), "%x %f %f %f", &handle, &route.spawnNode.x, &route.spawnNode.y, &route.spawnNode.z) == 4;
        route.hasPlayerStart = ReadPlaceAndTurn(start->Property("child_player_start"), route.playerStart);
        if (!route.hasPlayerStart && !ReadPlaceAndTurn(start->Property("op"), route.playerStart)) {
            route.playerStart[0] = start->pos.x;
            route.playerStart[1] = start->pos.y;
            route.playerStart[2] = start->pos.z;
        }
        if (!route.hasPlayerStart)
            rfa::Log("Transporter: the start node %08X (%s) has no player start (build people again); a retry puts the player at the node",
                     start->handle, start->Property("delivery_type").c_str());
        if (!route.end || !route.definition || !route.hasSpawnNode) {
            rfa::Log("Transporter: the start node %08X (%s) left out:%s%s%s", start->handle, start->Property("delivery_type").c_str(),
                     route.end ? "" : " no end node", route.definition ? "" : " no table row", route.hasSpawnNode ? "" : " no car's spawn node (build people again)");
            continue;
        }
        g_routes.push_back(route);
        g_progress[start->handle] = LoadProgress(start->handle);
    }
    int done = 0;
    for (const auto& entry : g_progress) done += entry.second.completed ? 1 : 0;
    rfa::Log("Transporter: %u routes, %d done", static_cast<unsigned>(g_routes.size()), done);
}

// A route's vehicle spawn node (the game's node at that place), or null while its zone isn't loaded.
BYTE* SpawnNodeOf(const Route& route) {
    const int count = NodeCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* node = NodeAt(kWorld, nullptr, i);
        if (node && *reinterpret_cast<const DWORD*>(node) == kNodeVtable && Squared(PositionOf(node), route.spawnNode) <= kOnSpotSquared) return node;
    }
    return nullptr;
}

// The car parked at a route's start: its vehicle spawn node's car, or null.
BYTE* ParkedCar(const Route& route) {
    BYTE* node = SpawnNodeOf(route);
    return node ? VehicleByHandle(*reinterpret_cast<const DWORD*>(node + kNodeCar)) : nullptr;
}

const Route* RouteOf(unsigned startNode) {
    for (const Route& route : g_routes)
        if (route.start->handle == startNode) return &route;
    return nullptr;
}

std::string MinutesSeconds(int ms) {
    if (ms < 0) ms = 0;
    const int seconds = ms / 1000;
    char text[16];
    sprintf_s(text, "%02d:%02d", seconds / 60, seconds % 60);
    return text;
}

// ---- The activity

class DeliveryActivity : public Activity {
public:
    DeliveryActivity() : Activity(Delivery, true, false) {}

    void LevelInit() override {
        Activity::LevelInit();
        ringEffect_ = -1;  // gone with the old level (not stopped: its handle is stale)
        ResetRun();
        if (g_startOnLoad != kNone) {  // a retry's reload: its start is set up again once the node is there
            g_reopened = true;
            g_startOnLoadWaited = 0.0f;
        }
    }

    // The corner panel while it runs (and the minimap's ring).
    bool Fill(Screen& screen) const {
        if (!route_ || state_ != State::Running || ended_) return false;
        screen.mode = Delivery;
        screen.screen = 2;
        screen.title = "ACT_GEXPRESS_NAME";
        const std::string subtitle = route_->definition->subtitle;
        screen.objective = outOfCar_ ? "ACT_GEXPRESS_GET_IN" : !subtitle.empty() ? subtitle : "MISS_G_EXPRESS_INFO";
        const int left = outOfCar_ && failLeft_ < timeLeft_ ? failLeft_ : timeLeft_;
        screen.timer = MinutesSeconds(left);
        char distance[32];
        const float shown = distanceToEnd_ > 0.0f ? distanceToEnd_ : 0.0f;
        sprintf_s(distance, shown < 10.0f ? "%.2f" : "%.0f", shown);
        screen.distance = distance;
        screen.center = route_->end->pos;
        screen.range = route_->definition->endRadius;
        return true;
    }

    // The activity-complete screen's answer: 1 the first hint, 2 the second. RETRY replays the run (the steps in
    // StepRetry, from the next frame on); CONTINUE and EXIT end it.
    void Choose(int choice) {
        if (!awaiting_) return;
        awaiting_ = false;
        const bool retry = (won_ && choice == 2) || (!won_ && choice == 1);
        if (!retry) {
            Exit();
            // check_for_user_input_complete (RM 0x846032): after the exit, a dead player's world is saved and opened
            // again (world::save_and_reload: he wakes at the nearest safehouse); guerrilla_save's respawn here.
            BYTE* player = static_cast<BYTE*>(Player());
            if (player && IsDead(player)) {
                rfa::Log("Transporter: EXIT with the player dead: he wakes at the nearest safehouse (Guerrilla's save and reload)");
                if (!GuerrillaSave_Respawn()) rfa::Log("Transporter: guerrilla_save couldn't respawn him (its lines say why)");
            }
            return;
        }
        retrying_ = true;
        retryDeferred_ = won_;  // pending_retry: after a win, one call later
        exitAsked_ = false;
        retryWaited_ = 0.0f;
        rfa::Log("Transporter %s: RETRY chosen (after a %s)", route_->definition->name.c_str(), won_ ? "win" : "loss");
    }

    // As the player dies: nothing ends yet. Guerrilla's run stands still while he lies dead (do_frame_active returns,
    // re-arming its post-death timer each frame so that it never runs out) until his death timer runs out:
    // TakePlayerDeath.
    void PlayerDied() {
        if (IsActive() && !ended_) rfa::Log("Transporter %s: the player died; the run waits for his death timer", route_->definition->name.c_str());
    }

    // His death timer ran out (gamestate_gp_simulate_frame → activity_guerrilla_express_process_player_death RM 0x76C8E0
    // = do_end_processing(true)): failed, with no reason line and no loss recorded (activity_lost isn't called), and the
    // results screen in place of the death screen. True when it took the death; false (the game-over screen then) when
    // no run is on, or the results screen can't come up (the run ends).
    bool TakePlayerDeath() {
        if (!route_ || state_ != State::Running || ended_ || !IsActive()) return false;
        won_ = false;
        rewardCount_ = 0;
        deadWaited_ = 0.0f;
        rfa::Log("Transporter %s: failed: the player died (his death timer ran out; the closest it came: %.1f m from the end ring)",
                 route_->definition->name.c_str(), closest_ > 1e8f ? -1.0f : closest_);
        ShowResults(false, Failed);
        return awaiting_;
    }

protected:
    // do_frame_inactive: the player in a parked car's driver's seat, the car at its start node.
    void FrameInactive(float seconds) override {
        if (g_startOnLoad != kNone) {
            ForceStartOnLoad(seconds);
            return;
        }
        BYTE* player = static_cast<BYTE*>(Player());
        if (!player) return;
        const DWORD inVehicle = *reinterpret_cast<const DWORD*>(player + kHumanVehicle);
        if (inVehicle == kNone) {
            blockedCar_ = kNone;
            return;
        }
        const Route* best = nullptr;
        BYTE* bestCar = nullptr;
        float bestSquared = 0.0f;
        for (const Route& route : g_routes) {
            // (Only the routes whose start is near: the car must be within 5 m of it with the player at its wheel.)
            if (FlatSquared(PositionOf(player), route.start->pos) > kNearSquared) continue;
            if (route.definition->baseName.size() && FindBaseData(route.definition->baseName) && FindBaseData(route.definition->baseName)->disabled) continue;
            BYTE* car = ParkedCar(route);
            if (!car || IsDead(car) || FlatSquared(PositionOf(car), route.spawnNode) > kReadySquared) continue;
            const float away = FlatSquared(PositionOf(player), route.start->pos);
            if (!best || away < bestSquared) {
                best = &route;
                bestCar = car;
                bestSquared = away;
            }
        }
        if (!best || HandleOf(bestCar) != inVehicle || SeatOccupant(bestCar, kDriverSeat) != player) return;
        if (Squared(PositionOf(bestCar), best->start->pos) >= kStartReach * kStartReach) return;
        // The start's rule is met: whether the run may start now (why not, once for each car he gets into).
        const std::string why = WhyNoStart();
        if (!why.empty()) {
            if (blockedCar_ != inVehicle) {
                blockedCar_ = inVehicle;
                rfa::Log("Transporter %s: the player is at the wheel of its car %08X, but the run doesn't start: %s", best->definition->name.c_str(),
                         inVehicle, why.c_str());
            }
            return;
        }
        blockedCar_ = kNone;
        // activity_kill_all_except(DELIVERY): every other activity not waiting.
        for (int mode = 1; mode < kModes; ++mode)
            if (Activity* other = Get(static_cast<Mode>(mode)); other && other != this && other->GetState() != State::Wait) other->KillNow();
        const BaseData* data = FindBaseData(best->definition->baseName);
        const int district = liberation::DistrictAt(best->start->pos);
        if (!StartSpawn(best->start->handle, data, district >= 0 ? district : liberation::PlayerDistrict())) {
            RFA_LOG_FIRST(20, "Transporter %s: the player is at the wheel of its car %08X, but the framework didn't start the run (state %d)",
                          best->definition->name.c_str(), inVehicle, static_cast<int>(GetState()));
            return;
        }
        route_ = best;
        car_ = HandleOf(bestCar);
        rfa::Log("Transporter %s: started in the car %08X", best->definition->name.c_str(), car_);
    }

    // Why a run can't start now ("" when it can): Guerrilla's do_frame_inactive asks activity_can_initialize (the
    // activity switched off, another modal one current, the cooldown after one ended; RED stops all but Guerrilla
    // Express).
    std::string WhyNoStart() const {
        char text[200] = "";
        if (Disabled())
            sprintf_s(text, "it is switched off (reasons 0x%X: 1 an activity, 2 a mission, 4 a script, 8 the console, ...)", disabled_);
        else if (GetState() != State::Wait && GetState() != State::Loading)
            sprintf_s(text, "its last run is still ending (state %d)", static_cast<int>(GetState()));
        else if (CurrentMode() != ModeNone)
            sprintf_s(text, "another activity is current (mode %d)", static_cast<int>(CurrentMode()));
        else if (CooldownLeft() > 0.0f)
            sprintf_s(text, "the activities' cooldown, %.0f s more (Guerrilla's: 180 s and more after a run ends)", CooldownLeft());
        else if (!CanInitialize())
            sprintf_s(text, "the framework won't offer it now");
        return text;
    }

    // activity_initialize 0x797170: the car's overrides.
    bool Initialize() override {
        if (!route_ || !Activity::Initialize()) return false;
        const Definition& d = *route_->definition;
        if (d.mostHitPoints != -1) {
            const int most = d.mostHitPoints > 0 ? d.mostHitPoints : 1;
            SetMostHitPoints(most, car_);
            SetHitPoints(car_, static_cast<int>(static_cast<float>(most) * d.health));
            rfa::Log("Transporter %s: the car's hit points %d (x %.2f)", d.name.c_str(), most, d.health);
        }
        if (d.speed != -1.0f) rfa::Log("Transporter %s: a speed override (%.1f) isn't ported (no row of Guerrilla's has one)", d.name.c_str(), d.speed);
        return true;
    }

    // activity_do_frame_ready: the car still there, on to STARTING.
    void FrameReady(float) override {
        BYTE* car = VehicleByHandle(car_);
        if (!Player() || !car || IsDead(car)) {
            Kill();
            return;
        }
        if (!BeginStartUp()) Kill();
    }

    // activity_start 0x7AD670.
    bool Start() override {
        BYTE* player = static_cast<BYTE*>(Player());
        if (!route_ || !player || *reinterpret_cast<const DWORD*>(player + kHumanVehicle) != car_ || !Activity::Start()) return false;
        const Definition& d = *route_->definition;
        if (d.alertLevel >= 0 && liberation::AlertLevel() < d.alertLevel) liberation::SetAlertLevel(d.alertLevel);
        timeLeft_ = d.timeToComplete;
        failLeft_ = d.timeToFail;
        outOfCar_ = timerStarted_ = alertDropped_ = ended_ = awaiting_ = won_ = false;
        rewardCount_ = 0;
        elapsed_ = 0;
        closest_ = 1e9f;
        closestSpeed_ = 0.0f;
        nearLog_ = 0.0f;
        lastCarPos_ = PositionOf(VehicleByHandle(car_));
        distanceToEnd_ = sqrtf(Squared(lastCarPos_, route_->end->pos)) - d.endRadius;
        messages::Tip("Guerrilla_Express", kTipMs);
        return true;
    }

    // activity_do_frame_active 0x7B7D20.
    void FrameActive(float seconds) override {
        if (ended_) {  // the results screen is up: its answer ends the run, or replays it
            if (retrying_) StepRetry(seconds);
            else if (awaiting_) FollowDeadResults(seconds);
            return;
        }
        BYTE* player = static_cast<BYTE*>(Player());
        if (player && IsDead(player)) return;  // do_frame_active: the run stands still while he lies dead (see PlayerDied)
        BYTE* car = VehicleByHandle(car_);
        if (!player || !car) {
            rfa::Log("Transporter: the car is gone; the run stops");
            Kill();
            return;
        }
        if ((*reinterpret_cast<const DWORD*>(car + kVehicleFlags) & kWreck) || IsDead(car)) {
            Lose(VehicleDestroyed, "the car was wrecked");
            return;
        }
        const Definition& d = *route_->definition;
        const Vector at = PositionOf(car);
        const float speed = seconds > 0.0f ? sqrtf(Squared(at, lastCarPos_)) / seconds : 0.0f;
        lastCarPos_ = at;
        const int ms = static_cast<int>(seconds * 1000.0f);
        // The clock waits until the car first moves.
        if (!timerStarted_) {
            if (speed >= kMovingSpeed) timerStarted_ = true;
            else timeLeft_ = d.timeToComplete;
        }
        if (timerStarted_) {
            timeLeft_ -= ms;
            elapsed_ += ms;
        }
        // Out of the car (any seat counts as in it).
        if (*reinterpret_cast<const DWORD*>(player + kHumanVehicle) == car_) {
            outOfCar_ = false;
            failLeft_ = d.timeToFail;
        } else {
            outOfCar_ = true;
            failLeft_ -= ms;
            if (failLeft_ <= 0) {
                Lose(VehicleAbandoned, "out of the car too long");
                return;
            }
        }
        if (timerStarted_ && timeLeft_ <= 0) {
            Lose(OutOfTime, "out of time");
            return;
        }
        distanceToEnd_ = sqrtf(Squared(at, route_->end->pos)) - d.endRadius;
        if (distanceToEnd_ < closest_) {
            closest_ = distanceToEnd_;
            closestSpeed_ = speed;
        }
        if (distanceToEnd_ <= 0.0f && speed < kStoppedSpeed) {
            Win(player, car);
            return;
        }
        // The log, near the end: why it doesn't count yet (once a second inside the ring, every 2 s within 25 m).
        nearLog_ -= seconds;
        if (distanceToEnd_ < kAlertDropReach && nearLog_ <= 0.0f) {
            nearLog_ = distanceToEnd_ <= 0.0f ? 1.0f : 2.0f;
            if (distanceToEnd_ <= 0.0f)
                rfa::Log("Transporter %s: inside the end ring (%.1f m from the end, the ring %.1f m) but moving %.1f m/s: it counts under %.1f m/s",
                         d.name.c_str(), distanceToEnd_ + d.endRadius, d.endRadius, speed, kStoppedSpeed);
            else
                rfa::Log("Transporter %s: %.1f m from the end ring (%.1f m from the end), %.1f m/s", d.name.c_str(), distanceToEnd_,
                         distanceToEnd_ + d.endRadius, speed);
        }
        if (distanceToEnd_ < kAlertDropReach && !alertDropped_) {
            alertDropped_ = true;
            liberation::SetAlertValue(0.0f);
        }
        // The end's ground ring while the player is near enough for its zone.
        const float playerAway = Squared(PositionOf(player), route_->end->pos);
        if (ringEffect_ == -1 && playerAway < kRingShowSquared) ShowRing();
        else if (ringEffect_ >= 0 && playerAway > kRingHideSquared) HideRing();
    }

    void End() override {
        Activity::End();
        ResetRun();
    }

private:
    // record_victory 0x7819E0: out of the car, the car stopped, the node's progress, then the screen.
    void Win(BYTE* player, BYTE* car) {
        BYTE exit[4] = {};
        RequestExit(player, exit);
        StopCar(car);
        const unsigned node = route_->start->handle;
        Progress p = g_progress[node];
        const bool firstWin = !p.completed;
        p.completed = true;
        p.best = p.best == 0 || elapsed_ < p.best ? elapsed_ : p.best;
        const bool parNow = !p.parBeaten && p.best < route_->definition->parTime;
        if (parNow) p.parBeaten = true;
        SaveProgress(node, p);
        rewardCount_ = (firstWin ? 1 : 0) + (parNow ? 1 : 0);
        Activity::RecordVictory({});  // the framework's (interface VICTORY, kiosk lines); the rewards come at the end
        won_ = true;
        rfa::Log("Transporter %s: delivered in %.2f s (best %.2f s, par %d s)%s%s", route_->definition->name.c_str(), elapsed_ / 1000.0f,
                 p.best / 1000.0f, route_->definition->parTime / 1000, firstWin ? ", the first time" : "", parNow ? ", par beaten" : "");
        ShowResults(true, Success);
    }

    void Lose(EndReason reason, const char* why) {
        RecordLoss();
        won_ = false;
        rewardCount_ = 0;
        rfa::Log("Transporter %s: failed: %s (the closest it came: %.1f m from the end ring, moving %.1f m/s then)", route_->definition->name.c_str(), why,
                 closest_ > 1e8f ? -1.0f : closest_, closestSpeed_);
        ShowResults(false, reason);
    }

    void ShowRing() {
        static bool notBuiltSaid = false;
        const int number = EffectNumber(kRingEffect);
        if (number < 0) {
            if (!notBuiltSaid) {
                notBuiltSaid = true;
                rfa::Log("Transporter: the effect %s isn't built (editor\\GuerrillaBuilder.exe build world makes it); no ground ring", kRingEffect);
            }
            ringEffect_ = -2;  // not tried again this run
            return;
        }
        const float scale = route_->definition->endRadius * kRingScale;
        const float turn[9] = {scale, 0, 0, 0, scale, 0, 0, 0, scale};
        const Vector at{route_->end->pos.x, route_->end->pos.y + kRingUp, route_->end->pos.z};
        ringEffect_ = EffectsPlay(number, &at.x, turn, true, kNone, -1, kNone, false, 0, 0);
        rfa::Log("Transporter %s: the end's ground ring %s (%.1f m ring, scale %.2f)", route_->definition->name.c_str(),
                 ringEffect_ >= 0 ? "shown" : "couldn't be played", route_->definition->endRadius, scale);
        if (ringEffect_ < 0) ringEffect_ = -2;
    }

    void HideRing() {
        if (ringEffect_ >= 0)
            if (BYTE* effect = EffectObject(kWorld, nullptr, ringEffect_)) StopEffect(effect, nullptr, 1);
        ringEffect_ = -1;
    }

    // hud_ui_activity_complete_start(DELIVERY, success): the four times (guerrilla_express_load_info, in ms).
    void ShowResults(bool success, EndReason reason) {
        ended_ = true;
        HideRing();  // do_end_processing: the end marker taken away
        const Definition& d = *route_->definition;
        GuerrillaHudActivityResults results{};
        results.size = sizeof(results);
        results.mode = kDeliveryMode;
        results.success = success ? 1 : 0;
        results.reason = success ? 0 : static_cast<int>(reason);
        const Progress& p = g_progress[route_->start->handle];
        results.numbers[0] = static_cast<float>(d.parTime);
        results.numbers[1] = static_cast<float>(d.timeToComplete);
        results.numbers[2] = static_cast<float>(p.completed ? p.best : 0);
        results.numbers[3] = static_cast<float>(elapsed_);
        awaiting_ = GuerrillaHud_ActivityComplete(&results) != 0;
        if (!awaiting_) {
            rfa::Log("Transporter: no activity-complete screen; the run ends");
            Exit();
        }
    }

    // The rewards × their count (apply_standard_rewards in the player's district), once.
    void PayRewards() {
        if (route_ && rewardCount_ > 0) {
            const Rewards& r = route_->definition->rewards;
            if (r.control != 0.0f) liberation::ChangeEdfControl(-r.control * rewardCount_, -1);
            if (r.morale != 0.0f) liberation::ChangeMorale(r.morale * rewardCount_, -1);
            if (r.salvage != 0) liberation::AddSalvage(r.salvage * rewardCount_);
            rfa::Log("Transporter: rewards x %d (salvage %d)", rewardCount_, r.salvage * rewardCount_);
        }
        rewardCount_ = 0;
    }

    // activity_guerrilla_express_exit 0x7B5E70: the rewards × their count, then the end.
    void Exit() {
        PayRewards();
        End();
    }

    // Ours: the results screen asked for over a dead player that doesn't come up. The campaign's frame runs only while
    // the game isn't paused, and the open screen pauses it, so this counts only while it isn't up. After 10 s of play,
    // the game-over screen instead, so that he isn't left dead with nothing to choose (guerrilla_hud's lines say why
    // its screen didn't open).
    void FollowDeadResults(float seconds) {
        BYTE* player = static_cast<BYTE*>(Player());
        if (!player || !IsDead(player)) return;
        deadWaited_ += seconds;
        if (deadWaited_ < kDeadResultsSeconds) return;
        rfa::Log("Transporter %s: the results screen didn't come up in %.0f s with the player dead; the run ends, and Armageddon's "
                 "game-over screen comes instead", route_->definition->name.c_str(), kDeadResultsSeconds);
        awaiting_ = false;
        End();
        GuerrillaSave_GameOver();
    }

    // RETRY's steps, each frame once the screen closed: after a win one call later (pending_retry; a district-liberated
    // screen pauses the game, and this frame with it), then the player out of the car (ours), then the replay.
    void StepRetry(float seconds) {
        if (retryDeferred_) {
            retryDeferred_ = false;
            return;
        }
        BYTE* player = static_cast<BYTE*>(Player());
        if (player && !IsDead(player) && *reinterpret_cast<const DWORD*>(player + kHumanVehicle) != kNone) {
            if (!exitAsked_) {
                BYTE exit[4] = {};
                RequestExit(player, exit);
                exitAsked_ = true;
            }
            retryWaited_ += seconds;
            if (retryWaited_ < kLeaveCarSeconds) return;
            rfa::Log("Transporter %s: RETRY: the player is still in a vehicle after %.0f s; saved as he is", route_->definition->name.c_str(),
                     kLeaveCarSeconds);
        }
        retrying_ = false;
        Replay(player);
    }

    // activity_guerrilla_express_replay 0x7B5EE0: the rewards, je_td_08 / 09's garage, the start node kept for the
    // reload, the world saved with the player at the start and opened again (guerrilla_save). The run ends here; the
    // reload sets it up again (ForceStartOnLoad).
    void Replay(BYTE* player) {
        const Route& route = *route_;
        const char* name = route.definition->name.c_str();
        PayRewards();
        const bool dead = player && IsDead(player);
        for (const Garage& garage : kGarages) {
            if (_stricmp(name, garage.route) != 0) continue;
            BYTE* mover = static_cast<BYTE*>(ObjectFromHandle(garage.mover));
            if (!mover || mover[kObjectType] != kMoverType || mover[kObjectSubtype] != kRfgMover) {
                rfa::Log("Transporter %s: RETRY: its EDF garage %08X isn't loaded; not rebuilt", name, garage.mover);
            } else if (dead && !IsDead(mover) && !(*reinterpret_cast<const DWORD*>(mover + kMoverFlags) & kPristine)) {
                const bool rebuilt = RestoreObject(kWorld, nullptr, garage.mover, false);
                rfa::Log("Transporter %s: RETRY after a death: its damaged EDF garage %08X %s", name, garage.mover,
                         rebuilt ? "rebuilt (world::restore_object)" : "not rebuilt (the game said no)");
            }
        }
        if (!GuerrillaSave_ReloadAt(route.playerStart, route.playerStart + 3)) {
            rfa::Log("Transporter %s: RETRY: the save and reload couldn't be asked (guerrilla_save off, not in its world, or a respawn or "
                     "reload already waiting); the run ends as EXIT", name);
            End();
            return;
        }
        g_startOnLoad = route.start->handle;
        g_retryOldCar = car_;
        g_reopened = false;
        g_startOnLoadWaited = 0.0f;
        rfa::Log("Transporter %s: RETRY: the world saved with the player at its start (%.1f, %.1f, %.1f%s)%s and opened again; the run's car %08X "
                 "is seen to after it", name, route.playerStart[0], route.playerStart[1], route.playerStart[2],
                 route.hasPlayerStart ? ", its player_start" : ", the node", dead ? " after his death (a respawn's save: his weapons kept)" : "",
                 car_);
        End();
        // Guerrilla's reload starts its level with no cooldown (level_init 0x797120); End() just started one, and the
        // cooldown isn't in the save: cleared now, whatever comes of the reload (21:27:47: "the activities' cooldown, 132 s
        // more" after a retry whose set-up never came).
        ClearCooldown();
    }

    // activity_force_start_generic(node, false) 0x7A8710 for a delivery node, once the world opened again from the
    // retry's save: every other activity killed (activity_kill_all_except(DELIVERY)), the node's old car destroyed when
    // more than 5 m from its spawn node, then spawn_child_object(-1) (a fresh one; none while a live one is there).
    // Guerrilla's activity_delivery_start_activity then only says whether it may start: the run starts as the player
    // gets in, as any. Ours: waits until the node is loaded and spawning, up to 30 s of play.
    void ForceStartOnLoad(float seconds) {
        // The world opened again: the campaign's state handed back from the save (DeliveryReloadProgress) or a level's
        // start (LevelInit). A reload from memory restarts neither the game clock nor the player, so the level's start
        // may not come (2026-11-19 20:54:14: the run's frame came 12 ms before the save's state, and gave up). While
        // guerrilla_save's reload is under way it waits; after it, 10 s of play more at most.
        if (!g_reopened) {
            if (GuerrillaSave_Reloading()) return;
            g_startOnLoadWaited += seconds;
            if (g_startOnLoadWaited < kReopenGraceSeconds) return;
            rfa::Log("Transporter: the retry's reload didn't come within %.0f s (guerrilla_save's lines say why); nothing set up again",
                     kReopenGraceSeconds);
            g_startOnLoad = kNone;
            return;
        }
        const Route* route = RouteOf(g_startOnLoad);
        if (!route) {
            g_startOnLoad = kNone;
            return;
        }
        const char* name = route->definition->name.c_str();
        BYTE* node = SpawnNodeOf(*route);
        if (!node || !Player() || NodeHeldBack(node, nullptr)) {
            g_startOnLoadWaited += seconds;
            if (g_startOnLoadWaited < kStartOnLoadSeconds) return;
            rfa::Log("Transporter %s: RETRY: its car's spawn node %s after %.0f s; the run isn't set up again", name,
                     !node ? "isn't loaded" : !Player() ? "is there but the player isn't" : "is held back by the game", kStartOnLoadSeconds);
            g_startOnLoad = kNone;
            return;
        }
        g_startOnLoad = kNone;
        for (int mode = 1; mode < kModes; ++mode)
            if (Activity* other = Get(static_cast<Mode>(mode)); other && other != this && other->GetState() != State::Wait) other->KillNow();
        // level_init (0x797120): a reload starts with no cooldown (this run's end before the save started one).
        ClearCooldown();
        // The old car: the node's own (its +0xA8), else the run's, kept over the reload (g_retryOldCar): a reload from the
        // save leaves the car in the world but not linked to its node any more (2026-11-19 21:13, "no old car" twice).
        // Guerrilla's rule: destroyed when more than 5 m from the node, else kept as the node's. One the node no longer
        // owns can't be kept so: the start's rule asks for the node's car, so it is destroyed too, and the node makes its
        // own.
        BYTE* nodeCar = VehicleByHandle(*reinterpret_cast<const DWORD*>(node + kNodeCar));
        BYTE* runCar = g_retryOldCar != kNone ? VehicleByHandle(g_retryOldCar) : nullptr;
        BYTE* old = nodeCar ? nodeCar : runCar;
        const DWORD oldHandle = old ? HandleOf(old) : kNone;
        const float away = old ? sqrtf(Squared(PositionOf(old), PositionOf(node))) : 0.0f;
        const bool orphan = old && !nodeCar;
        bool destroyed = false;
        if (old && (away > kOldCarReach || orphan)) destroyed = DestroyObject(kWorld, nullptr, old, false);
        const DWORD made = SpawnChild(node, nullptr, kNone);
        char oldText[200];
        if (!old && g_retryOldCar != kNone)
            sprintf_s(oldText, "the run's car %08X isn't in the reloaded world", g_retryOldCar);
        else if (!old)
            sprintf_s(oldText, "no old car");
        else if (away > kOldCarReach || orphan)
            sprintf_s(oldText, "the old car %08X (%.0f m from the node%s) %s", oldHandle, away, orphan ? ", no longer the node's" : "",
                      destroyed ? "destroyed" : "not destroyed (the game said no)");
        else
            sprintf_s(oldText, "the old car %08X kept (%.1f m from the node)", oldHandle, away);
        g_retryOldCar = kNone;
        rfa::Log("Transporter %s: RETRY: set up again at its start: %s; %s", name, oldText,
                 made != kNone && made != oldHandle && VehicleByHandle(made) ? "a fresh car made" :
                 made != kNone && made == oldHandle ? "the node keeps its car" : "the node's car asked for (it comes once its model streams in)");
    }

    void ResetRun() {
        HideRing();
        route_ = nullptr;
        car_ = kNone;
        timeLeft_ = failLeft_ = elapsed_ = 0;
        outOfCar_ = timerStarted_ = alertDropped_ = ended_ = awaiting_ = won_ = false;
        retrying_ = retryDeferred_ = exitAsked_ = false;
        retryWaited_ = deadWaited_ = 0.0f;
        rewardCount_ = 0;
        distanceToEnd_ = 0.0f;
    }

    const Route* route_ = nullptr;
    DWORD car_ = kNone;
    int timeLeft_ = 0, failLeft_ = 0, elapsed_ = 0;  // ms
    bool outOfCar_ = false, timerStarted_ = false, alertDropped_ = false;
    bool ended_ = false, awaiting_ = false, won_ = false;
    bool retrying_ = false, retryDeferred_ = false, exitAsked_ = false;  // RETRY chosen: its steps (StepRetry)
    float retryWaited_ = 0.0f;                                           // seconds waited for the player to leave the car
    float deadWaited_ = 0.0f;                                            // seconds the results waited over a dead player
    DWORD blockedCar_ = kNone;                                           // the car he sits in whose run couldn't start (logged once)
    int rewardCount_ = 0;
    float distanceToEnd_ = 0.0f;
    Vector lastCarPos_{};
    float closest_ = 1e9f, closestSpeed_ = 0.0f;  // for the log: the nearest the car came to the end ring, and its speed then
    float nearLog_ = 0.0f;                        // seconds to the next "near the end" line
    int ringEffect_ = -1;                         // the end's ground ring (an effect handle; -1 none, -2 couldn't be played)
};

DeliveryActivity* g_delivery = nullptr;

}  // namespace

Activity* MakeDelivery() {
    LoadDefinitions();
    LoadRoutes();
    static DeliveryActivity delivery;
    g_delivery = &delivery;
    return &delivery;
}

bool DeliveryScreen(Screen& screen) { return g_delivery && g_delivery->Fill(screen); }
void DeliveryChoose(int choice) {
    if (g_delivery) g_delivery->Choose(choice);
}
void DeliveryPlayerDied() {
    if (g_delivery) g_delivery->PlayerDied();
}
bool DeliveryTakePlayerDeath() { return g_delivery && g_delivery->TakePlayerDeath(); }
bool DeliveryNodeDone(unsigned node) {
    const auto found = g_progress.find(node);
    return found != g_progress.end() && found->second.completed;
}
void DeliveryReloadProgress() {
    for (const Route& route : g_routes) g_progress[route.start->handle] = LoadProgress(route.start->handle);
    DeliveryReopened("the save's state given back");  // a save with the campaign's state: the world opened again
}

// The world opened again, by either sign: guerrilla_save's reload running again (GuerrillaCampaign_ReloadedAtSpot), or
// a save's state handed back. With a retry waiting, its start is set up from the next frame on (ForceStartOnLoad).
void DeliveryReopened(const char* how) {
    if (g_startOnLoad == kNone || g_reopened) return;
    g_reopened = true;
    g_startOnLoadWaited = 0.0f;
    rfa::Log("Transporter: the retry's reload is in (%s); its start is set up as soon as its node is there", how);
}

}  // namespace campaign::activities

// The activity-complete screen's answer (guerrilla_hud's CloseActivityComplete): 1 its first hint, 2 its second.
void GuerrillaCampaign_ActivityCompleteChoose(int choice) {
    if (guerrilla_campaign::Running()) campaign::activities::DeliveryChoose(choice);
}
