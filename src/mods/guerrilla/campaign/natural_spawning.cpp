// Guerrilla's natural activity spawner: the guerrilla actions the game offers by itself, near the player, without a
// node to walk up to (Guerrilla's code, RM addresses):
//
// - activity_system_do_frame (0x7ACBB0): once its timer (0x1651950) is up, do_activity_spawn_frame (0x7A83F0), then the
//   timer set again to 4 to 6 s.
// - do_activity_spawn_frame: nothing while there's no player. In a safehouse: nothing for 30 to 60 s (0x165195C).
//   Nothing either until 90 to 120 s after a modal run ended (0x1651958, set by activity_base_modal::activity_end_common
//   0x769950), or 60 to 90 s after a mission (0x1651954: none is ported yet). It also needs:
//   - the alert below orange;
//   - the player outside the restricted areas;
//   - no activity running (activity_none 0x7699C0, special_activity_starting 0x7552A0: an offensive raid or house arrest
//     loading, offered or starting);
//   - none shutting down (any_activity_shutting_down 0x755240), starting (other_activities_starting 0x755160) or
//     loading (other_activities_loading 0x755110).
//   (A flag at 0x2229595 is tested too; nothing in Guerrilla ever sets it.)
// - The kinds it may start (get_spawn_set_from_activity 0x795B50 and get_spawn_set_from_props 0x7A80B0): the modal ones
//   that can always start (convoy, courier) and that could initialize now and aren't active; and the player's
//   district's activity_spawn.xtbl entries (activity_spawn_level_init_post 0x785BD0) not completed, whose control_level
//   is at least the district's EDF control (as a fraction of its most), each kind once.
// - One picked at random, taken out of the list, until one starts: it must be able to initialize and not be active;
//   its nodes near the player (get_local_spawn_object 0x77EE10: its collect_spawn_objects, then
//   select_activity_spawn_object 0x7693B0: the nearest, or a node of the same class attached to it); a convoy end
//   point used in the last 10 minutes is passed over; then the node's activity.xtbl row (get_base_activity_data
//   0x769320), skipped if disabled or if the node's district (else the player's) has more EDF control than the row's
//   control_limit; and start_activity_spawn(node, district). Started: the hint's 5 s (Activity_naturally_spawned_timer
//   0x1651948), a convoy end point's 10 minutes. Not started: the kind is killed (activity_kill).
// - activity_spawn.xtbl's entries are marked completed by a modal win of a kind that can't always start (the defensive
//   raids: with them, step 11). Its one Delivery entry never starts: delivery's collect_spawn_objects (0x758140) gives
//   nothing.
//
// The hint 5 s after a natural spawn (activity_base::activity_do_frame_ready 0x7697D0: the tool tips New_Activity,
// Guerrilla_Action_GPS, else HUD_GPS_HINT for 4 s) and the GPS key's beacon at the last spawned node come with the HUD
// (step 8a-2); until then the log says when the hint is due.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "activity_base.h"
#include "vehicles/vehicles_api.h"
#include "hud/hud_api.h"
#include "difficulty.h"
#include "messages.h"
#include "handle_kinds.h"
#include "liberation.h"
#include "restricted_areas.h"
#include "tables.h"
#include "world_objects.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>

namespace campaign::activities {
namespace {

// Guerrilla's Activity_names (RM 0x16512A0), the names activity_spawn.xtbl gives the kinds (activity_get_mode_from_name
// 0x756250).
const char* const kNames[kModes] = {"",          "Vehicle_Rail",       "Mission_VRail", "Tanks_a_Lot", "Delivery", "Killing_Spree",
                                    "Courier",   "Mayhem",             "Raid Defend",   "Raid Attack", "House_Arrest",
                                    "Demolitions_Master", "Assassination", "Convoy",    "Cont_Attack", "Car_bomb", "Area_Defense"};

Mode ModeNamed(const std::string& name) {
    for (int mode = 1; mode < kModes; ++mode)
        if (_stricmp(name.c_str(), kNames[mode]) == 0) return static_cast<Mode>(mode);
    return ModeNone;
}

// One of a district's activity_spawn.xtbl entries (activity_spawn_properties).
struct SpawnEntry {
    Mode mode = ModeNone;
    float controlLevel = 1.0f;  // offered while the district's EDF control (a fraction of its most) is at most this
    bool completed = false;
};
std::vector<std::vector<SpawnEntry>> g_entries;  // by district index
bool g_tableRead = false;

constexpr unsigned kNone = 0xFFFFFFFF;
int g_nextPass = -1;        // 0x1651950: the next pass (the game's clock, ms)
int g_modalEndWait = -1;    // 0x1651958: 90-120 s after a modal run ended
int g_safehouseWait = -1;   // 0x165195C: 30-60 s after a pass found the player in a safehouse
int g_hintAt = -1;          // Activity_naturally_spawned_timer 0x1651948
unsigned g_lastNode = kNone;  // Last_activity_spawned
Mode g_lastMode = ModeNone;

int Between(int low, int high) { return low + rand() % (high - low + 1); }
// A Guerrilla timestamp: one never set counts as run out.
bool RunOut(int until) { return until < 0 || GameTime() >= until; }

void ReadTable() {
    g_tableRead = true;
    std::vector<tables::Element> rows;
    if (!tables::Load("activity_spawn", rows)) return;
    g_entries.assign(liberation::DistrictCount(), {});
    int entries = 0, unknown = 0;
    for (const tables::Element& row : rows) {
        const int district = liberation::DistrictNamed(row.Text("Name").c_str());
        if (district < 0 || district >= static_cast<int>(g_entries.size())) {
            rfa::Log("activity_spawn.xtbl: no district \"%s\"", row.Text("Name").c_str());
            continue;
        }
        if (const tables::Element* list = row.Child("activity_list"))
            for (const tables::Element* info : list->Children("activity_info")) {
                SpawnEntry entry;
                entry.mode = ModeNamed(info->Text("activity_name"));
                entry.controlLevel = info->Number("control_level", 1.0f);
                if (entry.mode == ModeNone) ++unknown;
                g_entries[district].push_back(entry);
                ++entries;
            }
    }
    rfa::LogDetail("activity_spawn.xtbl: %d natural spawns in %u districts%s", entries, static_cast<unsigned>(rows.size()),
                   unknown ? " (some name no kind of activity)" : "");
}

// Whether any activity is in a state (other_activities_loading: 1, other_activities_starting: 3,
// any_activity_shutting_down: 5).
bool AnyIn(State state) {
    for (int mode = 1; mode < kModes; ++mode)
        if (const Activity* activity = Get(static_cast<Mode>(mode)); activity && activity->GetState() == state) return true;
    return false;
}

// special_activity_starting: an offensive raid or a house arrest loading, offered or starting.
bool SpecialStarting() {
    for (Mode mode : {RaidAttack, HouseArrest})
        if (const Activity* activity = Get(mode); activity && activity->GetState() >= State::Loading && activity->GetState() <= State::Starting)
            return true;
    return false;
}

// The district's EDF control as a fraction of its most (gfm_get_edf_control_percent).
float ControlFraction(int district) {
    const liberation::District* d = liberation::GetDistrict(district);
    return d && d->controlMax > 0.0f ? d->control / d->controlMax : 0.0f;
}

float Distance2(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// select_activity_spawn_object: the node nearest the player (the first of equals); then, unless the campaign is over
// or the difficulty is Hard or more, the first node attached to it of the same class, if any; else its parent, if of
// the same class. (The campaign's end isn't ported: never over yet.)
const world::Object* SelectNode(const std::vector<const world::Object*>& nodes, const Vector& player) {
    const world::Object* nearest = nullptr;
    float best = 3.4e38f;
    for (const world::Object* node : nodes)
        if (const float d = Distance2(node->pos, player); d < best) {
            best = d;
            nearest = node;
        }
    if (!nearest) return nullptr;
    const bool campaignOver = false;
    if (!campaignOver && difficulty::Level() < 2) {
        for (const world::Object* child : world::OfClass(nearest->objectClass.c_str()))
            if (child->parent == nearest->handle) return child;
        return nearest;
    }
    if (const world::Object* parent = world::Find(nearest->parent); parent && parent->objectClass == nearest->objectClass) return parent;
    return nearest;
}

// One pass (do_activity_spawn_frame): empty when a run was started (what started in *started), else what stopped it.
std::string Pass(std::string* started) {
    if (!g_tableRead) ReadTable();
    void* player = Player();
    if (!player) return "no player";
    const int now = GameTime();
    // A mission running (0x165150C != -1: its 60-90 s after), mission_prevent_activity_spawn: no missions yet.
    if (GuerrillaVehicles_PlayerInSafehouse()) {
        g_safehouseWait = now + Between(30000, 60000);
        return "you're in a safehouse (none for 30-60 s)";
    }
    if (!RunOut(g_modalEndWait)) return "an activity ended less than 90-120 s ago";
    if (!RunOut(g_safehouseWait)) return "you were in a safehouse less than 30-60 s ago";
    if (liberation::AlertLevel() >= handles::AlertOrange) return "the alert is orange or red";
    if (restricted_areas::PlayerInside()) return "you're in a restricted area";
    if (!NoneActive()) return "an activity is running";
    if (SpecialStarting()) return "an offensive raid or house arrest is starting";
    if (AnyIn(State::Shutdown)) return "an activity is shutting down";
    if (AnyIn(State::Starting)) return "an activity is starting";
    if (AnyIn(State::Loading)) return "an activity is loading";

    const Vector at = PositionOf(player);
    const int playerDistrict = liberation::PlayerDistrict();
    std::vector<Mode> set;
    for (int mode = 1; mode < kModes; ++mode)
        if (const Activity* activity = Get(static_cast<Mode>(mode)))
            if (activity->Modal() && activity->CanAlwaysStart() && !activity->IsActive() && activity->CanInitialize()) set.push_back(activity->GetMode());
    if (playerDistrict >= 0 && playerDistrict < static_cast<int>(g_entries.size()))
        for (const SpawnEntry& entry : g_entries[playerDistrict])
            if (entry.mode != ModeNone && !entry.completed && entry.controlLevel >= ControlFraction(playerDistrict) &&
                std::find(set.begin(), set.end(), entry.mode) == set.end())
                set.push_back(entry.mode);
    if (set.empty()) return "no kind can start now (each is offered, running, disabled or waiting for the cooldown)";

    std::string why;
    while (!set.empty()) {
        const size_t pick = rand() % set.size();
        const Mode mode = set[pick];
        set.erase(set.begin() + pick);
        Activity* activity = Get(mode);  // (an activity_spawn.xtbl kind not ported yet has none)
        if (!activity || !activity->CanInitialize() || activity->IsActive()) {
            why += std::string(kNames[mode]) + (activity ? ": can't initialize; " : ": not ported yet; ");
            continue;
        }
        std::vector<const world::Object*> nodes;
        const world::Object* node = activity->CollectSpawnObjects(at, nodes) ? SelectNode(nodes, at) : nullptr;
        if (!node) {
            why += std::string(kNames[mode]) + ": no node near you; ";
            continue;
        }
        if (activity->NodeResting(*node)) {  // a convoy end point that sent one in the last 10 minutes
            why += std::string(kNames[mode]) + ": its nearest node waits (10 minutes from the level's start or its last one); ";
            continue;
        }
        const BaseData* data = activity->NodeBaseData(*node);
        if (!data || data->disabled) {
            why += std::string(kNames[mode]) + " " + node->Property("convoy_type", node->Property("courier_type").c_str()) + ": its activity.xtbl row is missing or disabled; ";
            continue;
        }
        int district = liberation::DistrictAt(node->pos);
        if (district < 0) district = playerDistrict;
        if (ControlFraction(district) > data->controlLimit) {
            why += data->name + ": its district's EDF control is above its control_limit; ";
            continue;
        }
        if (!activity->StartSpawn(node->handle, data, district)) {
            activity->KillNow();
            why += data->name + ": didn't start; ";
            continue;
        }
        g_hintAt = now + 5000;
        activity->NaturallySpawned(*node);
        if (started) {
            char text[160];
            sprintf_s(text, "%s %s at node %08X, %.0f m from you", kNames[mode], data->name.c_str(), node->handle, sqrtf(Distance2(node->pos, at)));
            *started = text;
        }
        return "";
    }
    return why;
}

using ConsolePrintFn = void(__cdecl*)(const char* format, ...);
auto const ConsolePrint = reinterpret_cast<ConsolePrintFn>(0xC7C440);
auto& g_commandRunning = *reinterpret_cast<int*>(0x28FF1C0);

}  // namespace

void SpawnLevelInit() {
    g_nextPass = -1;
    g_modalEndWait = g_safehouseWait = g_hintAt = -1;
    g_lastNode = kNone;
    g_lastMode = ModeNone;
}

void SpawnModalEnded() { g_modalEndWait = GameTime() + Between(90000, 120000); }

unsigned LastSpawnedNode(Mode* mode) {
    if (mode) *mode = g_lastMode;
    return g_lastNode;
}

void SetLastSpawned(unsigned node, Mode mode) {
    g_lastNode = node;
    g_lastMode = mode;
}

void SpawnFrame() {
    if (!RunOut(g_nextPass)) return;
    std::string started;
    const std::string why = Pass(&started);
    if (why.empty()) rfa::LogDetail("Activities: natural spawn: %s", started.c_str());
    else RFA_LOG_DETAIL_FIRST(20, "Activities: natural spawn: none (%s)", why.c_str());
    g_nextPass = GameTime() + Between(4000, 6000);
}

void SpawnCommand() {
    if (!g_commandRunning) return;
    std::string started;
    const std::string why = Pass(&started);
    if (why.empty()) {
        rfa::Log("rfg_act_spawn: started %s", started.c_str());
        ConsolePrint("rfg_act_spawn: started %s\n", started.c_str());
    } else {
        rfa::Log("rfg_act_spawn: none: %s", why.c_str());
        ConsolePrint("rfg_act_spawn: none: %s\n", why.c_str());
    }
}

void OfferHintFrame() {
    if (g_hintAt < 0 || GameTime() < g_hintAt) return;
    g_hintAt = -1;
    // activity_base::activity_do_frame_ready (RM 0x7697D0): the New_Activity tip until it has played once, then the
    // Guerrilla_Action_GPS tip, then the HUD message HUD_GPS_HINT ("[BUTTON:GPS_MARKER] TO GET PATH TO [image:rfg_%ls]")
    // with the kind's picture, 4 s. (Guerrilla_Action_Final follows once the campaign is over: its end isn't ported. The
    // hint's message isn't taken away as the run starts, activity_kill_gps_message: ours has no handle; it lasts 4 s.)
    if (!GuerrillaHud_ToolTipPlayed("New_Activity")) {
        messages::Tip("New_Activity", 0);
        rfa::LogDetail("Activities: the offer's hint: the New_Activity tip");
        return;
    }
    if (!GuerrillaHud_ToolTipPlayed("Guerrilla_Action_GPS")) {
        messages::Tip("Guerrilla_Action_GPS", 0);
        rfa::LogDetail("Activities: the offer's hint: the Guerrilla_Action_GPS tip");
        return;
    }
    const Activity* offered = g_lastMode != ModeNone ? Get(g_lastMode) : nullptr;
    const char* picture = offered ? offered->IconImageName() : nullptr;
    if (!picture) return;
    messages::PrimaryPicture("HUD_GPS_HINT", picture, 4000);
    RFA_LOG_DETAIL_FIRST(20, "Activities: the offer's hint: HUD_GPS_HINT with %s", picture);
}

}  // namespace campaign::activities
