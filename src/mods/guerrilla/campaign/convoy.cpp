// Guerrilla's convoy activity (activity mode 13, modal, can always start: activity_convoy_data, RM 0x22451E0), on the
// convoys guerrilla_vehicles drives (its convoys.cpp, Guerrilla's object_convoy). Guerrilla (RM addresses):
//
// - The rows: convoys.xtbl (convoy_definitions_init 0xA8E900): its members (a squad, a vehicle group, a role: Target,
//   Defender, Break and Attack), its activity.xtbl row (base_activity_data), difficulty 1-9, Destroy or Capture, its
//   speeds, its hours (tod_info) and its rewards (Standard_Rewards). Each object_convoy_end_point (convoy_type: its row,
//   home_district: where the rewards go) sends that convoy.
// - The pick (collect_spawn_objects 0x780DA0), for the natural spawner (natural_spawning.cpp): while the campaign runs,
//   none once every end point is completed; else the activity level is 1, or the difficulty + 1 of the last end point
//   (in the world's order) that is completed or in a liberated district; the end points not completed nor disabled,
//   of that difficulty, whose hours include the game's (convoy_allowed_to_spawn_tod 0x757670) and more than 150 m from
//   the player. (After the campaign: any of them, collect_convoy_objects_random 0x7808C0; its end isn't ported.) The
//   spawner skips an end point that sent one in the last 10 minutes.
// - activity_initialize (0x796EA0): the modal start (READY), the radio's male or female answers (50 %), spawn_convoy
//   (0x780FA0): the convoy set off from the end point, its START line; set_ready_flags (0x780A90).
// - READY, while it drives (do_frame_ready 0x780C50): its members followed (update_squad_status 0x76B8C0,
//   update_squad_vehicles 0x76B960); it starts (begin_start_up 0x780A50: the evade speed) once it is attacked
//   (convoy_was_attacked 0x76B630: its own flag, or one of its crews fighting the player) with the ESCAPING line, or the
//   capture target lost its driver or left the line (update_target_vehicle_driver 0x780B30, capture_target_left_convoy
//   0x757930: every role break and attack first), or the player got into it (update_player_in_target_vehicle
//   0x76BA10: the CAPTURED lines); half way along its route, the DISTANCE_50 line. At the route's end
//   (convoy_at_destination 0x7AD460) the FAIL line and the run is killed: no loss. At orange or red the framework
//   withdraws the offer.
// - activity_start (0x7B59A0): the running flags (set_active_flags_capture / _destroy), the break-and-attack squads let go
//   (release_attack_squads 0x7B1BB0), the modal start (the row's alert caps), the tool tip Convoy (2 s).
// - RUNNING (do_frame_active 0x7B1B40): every 1-2 s the break-and-attack squads sent at the player (update_squad_orders
//   0x7AD2C0); then
//   - Capture (do_frame_active_capture 0x796DA0): the target more than 350 m from the player: lost, ESCAPED (its FAIL line
//     if any squad lives); its crew may get out once it is 30 % damaged or stopped over 2 s (process_target_driver_flags
//     0x76A6A0); destroyed: lost, VEHICLE_DESTROYED (the DESTROYED lines if the player lives); the player in it inside a
//     safehouse: delivered (target_vehicle_delivered 0x76BC00: the activity level up, the DELIVERED line, the player
//     out, the vehicle locked, the win at once).
//   - Destroy (do_frame_active_destroy 0x7A0E00): a member's vehicle more than 350 m from the player: lost, ESCAPED; every
//     member's vehicle destroyed (empty ones and escorts too): 1 s later the win, 2 s after it the DESTROYED lines and
//     the end (destruction_convoy_destroyed 0x796F10).
// - The win (record_victory 0x7B5A40): the tool tip Guerrilla_Action_Complete (4 s), the end point completed (saved),
//   apply_standard_rewards (0x7B2E30) in the end point's home district: EDF control −35 (destroy) or −40 (capture),
//   morale +10.
// - activity_end (0x796D10) and activity_kill (0x796D40): the framework's end, and the convoy object destroyed (its crews
//   given back to the game).
//
// - The HUD: render_icons (0x781090, while READY, STARTING or RUNNING): a capture convoy's target
//   (MAP_ELEMENT_CONVOY_CAPTURE_ICON, ui_map_icon_convoy_cap) while the player isn't in it, else every live member of a
//   destroy convoy (MAP_ELEMENT_CONVOY_ICON, ui_map_icon_convoy), sticky, grey (Completed_icon_color 130, 130, 130) once
//   the end point is completed, else white (Default_map_item_color), on the world map too (both rows VisibleOnFSMap);
//   in the target, the nav beacon once on the nearest safehouse (safehouse_find_nearest_safehouse 0xA6B570, flag 0x10).
//   The route on the maps (cartographer::render_convoy_routes 0x870940) unless the player drives the target. The corner
//   panel (hud_ui_missions_get_activity_convoy_info 0x896790, LUA_ACTIVITY_CONVOY 3) while RUNNING: ACT_CONVOY,
//   ACT_CONVOY_CAPTURE / _CAPTURE_RETURN (in the target) / ACT_CONVOY_DESTROY, the vehicles left of all ("%02d/%02d"),
//   the distance (capture: to the target, 0 in it; destroy: the nearest live member) and the target's health
//   ("%.0f%%", capture). The target highlighted while RUNNING and the player isn't in it (do_frame_active_capture:
//   objective_highlight_add(vehicle, HIGHLIGHT_PURPOSE_NEUTRAL, default style)). The win and a destroyed target take the
//   nav beacon away (remove_player_marker).
//
// Here: all of the above but Guerrilla's register_player_hit (a shot of the player's landing within twice a member
// vehicle's radius also starts it: Armageddon's human_combat_process_hits isn't found yet), the radio lines (logged
// until the campaign's voices: step 12c), and the reticle's and camera's aim at an empty member vehicle of a destroy
// convoy (activity_convoy_vehicle_destroy 0x757BC0, for hud_ui_weapons_determine_object_targeted and
// camera_view_data::focus_on_target: Armageddon's are not found yet). The highlight is Armageddon's objective highlight
// (the same system), whose purposes are its own (waypoint, attack_*, destroy_*, ...): Guerrilla's NEUTRAL is its
// "waypoint" (0). Guerrilla's activity_convoy_data's reset clears its despawning list before its clean-up reads it, so
// the parked crews never raise the alert: nothing to port there.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "activity_base.h"
#include "vehicles/vehicles_api.h"
#include "hud/hud_api.h"
#include "campaign_api.h"
#include "liberation.h"
#include "messages.h"
#include "save.h"
#include "tables.h"
#include "world_objects.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

namespace campaign::activities {
namespace {

constexpr unsigned kNone = 0xFFFFFFFF;
constexpr int kMostMembers = 12;           // MemberSquads (squad_array: 12)
constexpr float kMinStart = 150.0f;        // the end point more than this from the player (22500 m²)
constexpr float kEscape = 350.0f;          // a member this far from the player is lost (122500 m²)
constexpr int kNodeRestMs = 600000;        // an end point that sent one waits 10 minutes
constexpr int kMostLevel = 16;             // MAX_CONVOY_DIFFICULTY_LEVEL
// The map icons' colours (0xAARRGGBB): Default_map_item_color (white, RM 0x16621A0), Completed_icon_color (grey
// 130, 130, 130, 0x16621A4).
constexpr unsigned long kIconColour = 0xFFFFFFFF, kCompletedIconColour = 0xFF828282;
constexpr int kStickyRimOnce = 4;          // the HUD's placement: sticky, only the first pushed to the rim drawn (0x8713AA)
const char* const kConvoyPicture = "ui_map_icon_convoy";         // MAP_ELEMENT_CONVOY_ICON's IconName
const char* const kCapturePicture = "ui_map_icon_convoy_cap";    // MAP_ELEMENT_CONVOY_CAPTURE_ICON's
// Armageddon's objective highlights, the same system as Guerrilla's: objective_highlight_add (0x914C30: an object's
// handle, a purpose, a style; nothing if it has one already) and objective_highlight_remove (0x914D20).
using HighlightAddFn = void(__cdecl*)(unsigned handle, int purpose, int style);
auto const HighlightAdd = reinterpret_cast<HighlightAddFn>(0x914C30);
using HighlightRemoveFn = void(__cdecl*)(unsigned handle);
auto const HighlightRemove = reinterpret_cast<HighlightRemoveFn>(0x914D20);
constexpr int kHighlightWaypoint = 0;      // Armageddon's purpose "waypoint", for Guerrilla's HIGHLIGHT_PURPOSE_NEUTRAL
constexpr int kHighlightDefaultStyle = -1; // HIGHLIGHT_STYLE_DEFAULT: the purpose's own

// One convoys.xtbl row.
struct Definition {
    std::string name;
    bool capture = false;
    bool mission = false;  // Type Mission: a mission's convoy (assert and kill in Guerrilla's start)
    int difficulty = 1;
    int startHour = 0, endHour = 0;
    Rewards rewards;
    BaseData baseData;  // its activity.xtbl row (base_activity_data); its layers left out: each holds only the route, which
                        // convoys.txt already has (built from the same files), so no layer set is loaded for a convoy
    bool hasBaseData = false;
};
std::vector<Definition> g_definitions;

// An object_convoy_end_point, in campaign_objects.xml's order (the world's).
struct EndPoint {
    const world::Object* node = nullptr;
    const Definition* def = nullptr;
    int homeDistrict = -1;
    bool completed = false;
    int restsUntil = -1;  // the game's clock: its 10 minutes after it sent one
};
std::vector<EndPoint> g_ends;
bool g_loaded = false, g_haveTable = false;

const Definition* FindDefinition(const std::string& name) {
    for (const Definition& def : g_definitions)
        if (_stricmp(def.name.c_str(), name.c_str()) == 0) return &def;
    return nullptr;
}

EndPoint* FindEnd(unsigned handle) {
    for (EndPoint& end : g_ends)
        if (end.node->handle == handle) return &end;
    return nullptr;
}

std::string SaveSection(unsigned handle) { return save::ForObject("convoy", handle); }

void Load() {
    if (g_loaded) return;
    g_loaded = true;
    std::vector<tables::Element> rows;
    if (!tables::Load("convoys", rows)) {
        rfa::Log("Convoys: no convoys.xtbl in mods\\guerrilla_campaign\\tables (build the people again with GuerrillaBuilder.exe); guerrilla_vehicles sends its own convoys, not the activity's");
        return;
    }
    for (const tables::Element& row : rows) {
        Definition def;
        def.name = row.Text("Name");
        const std::string type = row.Text("Type");
        def.capture = _stricmp(type.c_str(), "Capture") == 0;
        def.mission = _stricmp(type.c_str(), "Mission") == 0;
        def.difficulty = row.Int("difficulty", 1);
        if (const tables::Element* tod = row.Child("tod_info")) {
            def.startHour = tod->Int("spawn_start_time", 0);
            def.endHour = tod->Int("spawn_end_time", 0);
        }
        if (const tables::Element* rewards = row.Child("Standard_Rewards")) {
            def.rewards.control = rewards->Number("EDF_Control_Reduction", 0.0f);
            def.rewards.morale = rewards->Number("Morale_Increase", 0.0f);
            def.rewards.salvage = rewards->Int("Salvage_Reward", 0);
        }
        if (const BaseData* data = FindBaseData(row.Text("base_activity_data"))) {
            def.baseData = *data;
            def.baseData.layers.clear();
            def.hasBaseData = true;
        }
        g_definitions.push_back(def);
    }
    g_haveTable = !g_definitions.empty();
    int withoutRow = 0;
    for (const world::Object* node : world::OfClass("object_convoy_end_point")) {
        EndPoint end;
        end.node = node;
        end.def = FindDefinition(node->Property("convoy_type"));
        end.homeDistrict = liberation::DistrictNamed(node->Property("home_district").c_str());
        if (!end.def || !end.def->hasBaseData) ++withoutRow;
        g_ends.push_back(end);
    }
    rfa::LogDetail("Convoys: %u convoys.xtbl rows, %u end points%s", static_cast<unsigned>(g_definitions.size()), static_cast<unsigned>(g_ends.size()),
                   withoutRow ? " (some without their convoys.xtbl or activity.xtbl row)" : "");
}

void ReadProgress() {
    for (EndPoint& end : g_ends) end.completed = save::Int(SaveSection(end.node->handle).c_str(), "complete", 0) != 0;
}

bool Liberated(int district) {
    const liberation::District* d = liberation::GetDistrict(district);
    return d && d->liberated;
}

// convoy_allowed_to_spawn_tod: the same start and end, any hour; else from the start up to the end (across midnight).
bool HoursAllow(const Definition& def) {
    const int hour = *reinterpret_cast<const BYTE*>(0x12F76D4);  // the game's clock's hour (guerrilla_vehicles' GameHour)
    if (def.startHour == def.endHour) return true;
    if (def.endHour > def.startHour) return def.startHour <= hour && hour < def.endHour;
    return hour >= def.startHour || hour < def.endHour;
}

float Distance(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// The convoy's radio lines (play_radio_line 0x7576D0: each once a run), logged until the campaign's voices.
enum Line { kStartLine, kCapturedLine, kCapturedResponse, kDestroyedLine, kDestroyedResponse, kDeliveredLine, kEscapingLine, kFailLine, kDistance50Line, kLines };
const char* const kLineTags[kLines] = {"Start", "Captured_Vehicle", "Captured_Vehicle_Res", "Destroyed_Vehicle", "Destroyed_Vehicle_Res",
                                       "Delivered_Vehicle", "Escaping_Vehicle", "Fail", "Distance_50"};

// The console's commands (as the other kinds'): one registered by name (0xC7BD70); rfg_cv_start's handler, below.
using RegisterCommandFn = void*(__fastcall*)(void* command, void* unused, const char* name, const char* description, int type, void (*handler)());
auto const RegisterCommand = reinterpret_cast<RegisterCommandFn>(0xC7BD70);
BYTE g_startCommand[32];
void StartCommand();

class ConvoyActivity : public Activity {
public:
    ConvoyActivity() : Activity(Convoy, true, true) {}

    bool CollectSpawnObjects(const Vector& player, std::vector<const world::Object*>& nodes) const override {
        if (!g_haveTable) return false;
        bool allDone = true;
        for (const EndPoint& end : g_ends) allDone = allDone && end.completed;
        // (The campaign's end, Guerrilla's 0x222966F, isn't ported: the campaign always runs.)
        if (allDone) return false;
        int level = 1;
        for (const EndPoint& end : g_ends)
            if (end.def && end.homeDistrict >= 0 && (end.completed || Liberated(end.homeDistrict))) level = end.def->difficulty + 1;
        for (const EndPoint& end : g_ends) {
            // (An end point's own disabled and suspended flags come from the missions' disable_convoy words: none yet.)
            if (!end.def || end.completed || end.def->difficulty != level || !HoursAllow(*end.def)) continue;
            if (Distance(end.node->pos, player) <= kMinStart) continue;
            nodes.push_back(end.node);
        }
        return !nodes.empty();
    }

    const BaseData* NodeBaseData(const world::Object& node) const override {
        const EndPoint* end = FindEnd(node.handle);
        return end && end->def && end->def->hasBaseData ? &end->def->baseData : nullptr;
    }

    bool NodeResting(const world::Object& node) const override {
        const EndPoint* end = FindEnd(node.handle);
        return end && end->restsUntil >= 0 && GameTime() < end->restsUntil;
    }

    void NaturallySpawned(const world::Object& node) override {
        if (EndPoint* end = FindEnd(node.handle)) end->restsUntil = GameTime() + kNodeRestMs;
    }

    // For the console: a run from an end point now, whatever its difficulty, hours or distance.
    bool StartAt(EndPoint& end, const char* how) {
        if (!end.def || !end.def->hasBaseData) return false;
        int district = liberation::DistrictAt(end.node->pos);
        if (district < 0) district = liberation::PlayerDistrict();
        if (!StartSpawn(end.node->handle, &end.def->baseData, district)) return false;
        rfa::LogDetail("Convoy %s: started (%s) at its end point %08X", end.def->name.c_str(), how, end.node->handle);
        return true;
    }

    void Reset() override {
        Activity::Reset();
        ResetConvoy();
    }

    // render_icons (0x781090).
    int MapIcons(GuerrillaCampaignIcon* icons, int most) const override {
        if (!end_ || !end_->def || most <= 0) return 0;
        int count = 0;
        auto add = [&](const Member& member, const char* picture) {
            if (count >= most) return;
            GuerrillaCampaignIcon& icon = icons[count++];
            icon.x = member.x;
            icon.y = member.y;
            icon.z = member.z;
            icon.picture = picture;
            icon.placement = kStickyRimOnce;
            icon.color = end_->completed ? kCompletedIconColour : kIconColour;
            icon.onWorldMap = 1;
        };
        if (Capture()) {
            if (const Member* target = Target(); target && !PlayerInTarget()) add(*target, kCapturePicture);
        } else {
            for (int i = 0; i < convoy_.members && i < kMostMembers; ++i)
                if (!members_[i].vehicleKilled) add(members_[i], kConvoyPicture);
        }
        return count;
    }

    // get_icon_image_name (0x757B50): get_minimap_bitmap_icon_name(15 capture, else 14).
    const char* IconImageName() const override {
        if (!end_ || !end_->def) return nullptr;
        return Capture() ? kCapturePicture : kConvoyPicture;
    }

    // The icon render_icons gives for the end point the convoy came from (its spawn object): the first one.
    bool SpawnedIcon(unsigned node, Vector& at) const override {
        if (node != node_) return false;
        GuerrillaCampaignIcon icon{};
        if (MapIcons(&icon, 1) != 1) return false;
        at = Vector{icon.x, icon.y, icon.z};
        return true;
    }

    // hud_ui_missions_get_activity_convoy_info (0x896790): the corner panel while RUNNING.
    bool Fill(Screen& screen) const {
        if (!end_ || !end_->def || state_ != State::Running) return false;
        const bool capture = Capture(), inTarget = PlayerInTarget();
        screen.mode = Convoy;
        screen.screen = 2;
        screen.title = "ACT_CONVOY";
        screen.objective = !capture ? "ACT_CONVOY_DESTROY" : inTarget ? "ACT_CONVOY_CAPTURE_RETURN" : "ACT_CONVOY_CAPTURE";
        screen.status = capture ? 0 : 1;  // its activity_info: 0 capture, 1 destroy
        // activity_convoy_get_num_current_vehicles (0x76BE90) of get_num_spawn_vehicles: a member counts while its vehicle
        // lives (a point, not made yet, counts too).
        int left = 0;
        const int all = convoy_.members;
        for (int i = 0; i < all && i < kMostMembers; ++i)
            if (!members_[i].vehicleKilled) ++left;
        char text[32];
        sprintf_s(text, left >= 100 || all >= 100 ? "%03d/%03d" : "%02d/%02d", left, all);
        screen.count = text;
        // get_target_distance (0x781280): capture, to the target (0 while in it); destroy, the nearest live member.
        float distance = 0.0f;
        if (capture) {
            if (const Member* target = Target(); target && !inTarget) distance = FromPlayer(*target);
        } else {
            bool any = false;
            for (int i = 0; i < all && i < kMostMembers; ++i) {
                if (members_[i].vehicleKilled) continue;
                const float d = FromPlayer(members_[i]);
                if (!any || d < distance) distance = d;
                any = true;
            }
        }
        sprintf_s(text, "%.0f", distance);
        screen.distance = text;
        // activity_convoy_get_nearest_target_health (0x76BE00): (1 - damage) × 100 of the nearest target, capture only.
        if (capture) {
            const Member* nearest = nullptr;
            for (int i = 0; i < all && i < kMostMembers; ++i)
                if (target_[i] && !members_[i].vehicleKilled && (!nearest || FromPlayer(members_[i]) < FromPlayer(*nearest))) nearest = &members_[i];
            sprintf_s(text, "%.0f%%", nearest ? (1.0f - nearest->damage) * 100.0f : 0.0f);
            screen.strength = text;
        }
        return true;
    }

    // render_convoy_routes: not while the player drives the target (activity_convoy_player_in_vehicle 0x757C40).
    bool RouteShown() const { return end_ && WasInitialized() && !PlayerInTarget(); }

    // level_init (0x76A600): no run, the activity level 1; each end point's 10 minutes from the level's start (as the
    // vehicles' own convoys wait: [GuerrillaVehicles] ConvoyWaitAfterLoad=, Guerrilla's last_used set as it loads).
    // A run or offer cut off by the reload (the player's death) leaves no convoy behind: the convoy object went with
    // the old level, so guerrilla_vehicles lets its convoy go and the row may be sent again.
    void LevelInit() override {
        GuerrillaVehicles_ConvoyForget();
        static bool commandAdded = false;
        if (!commandAdded) {
            commandAdded = true;
            RegisterCommand(g_startCommand, nullptr, "rfg_cv_start", "Starts one of Guerrilla's convoys from its end point now (je_cc_01, je_cd_02, ...)", 3,
                            &StartCommand);
        }
        Activity::LevelInit();
        Load();
        ReadProgress();
        level_ = 1;
        const int wait = (std::max)(0, rfa::IniInt(L"GuerrillaVehicles", L"ConvoyWaitAfterLoad", 600)) * 1000;
        for (EndPoint& end : g_ends) end.restsUntil = GameTime() + wait;
    }

protected:
    bool Initialize() override {
        EndPoint* end = FindEnd(node_);
        if (!end || !end->def) return false;
        if (!Activity::Initialize()) return false;
        ResetConvoy();
        end_ = end;
        female_ = rand() % 100 <= 50;
        if (end->def->mission || !GuerrillaVehicles_ConvoySpawn(end->def->name.c_str())) {
            rfa::Log("Convoy %s: guerrilla_vehicles didn't send it", end->def->name.c_str());
            return false;
        }
        Play(kStartLine);
        // Which members are its targets (the roles may all turn break and attack later).
        GuerrillaVehiclesConvoyState state{};
        state.size = sizeof(state);
        GuerrillaVehiclesConvoyMember members[kMostMembers]{};
        if (GuerrillaVehicles_ConvoyStatus(&state, members, kMostMembers))
            for (int i = 0; i < state.members && i < kMostMembers; ++i) target_[i] = members[i].role == 0;
        rfa::LogDetail("Convoy %s: offered (%s, difficulty %d, %d members)", end->def->name.c_str(), end->def->capture ? "capture" : "destroy",
                       end->def->difficulty, state.members);
        return true;
    }

    void FrameReady(float) override {
        if (!Follow()) {
            rfa::LogDetail("Convoy %s: its convoy is gone; the offer ends", Name());
            Kill();
            return;
        }
        FollowHud();
        if (AtDestination()) return;
        const Member* target = Target();
        if (Capture() && target && (target->driverless || target->leftConvoy)) {
            GuerrillaVehicles_ConvoyAllAttack();
            BeginStartUp();
            return;
        }
        if (Capture() && PlayerInTarget()) {
            PlayCapturedLines();
            BeginStartUp();
            return;
        }
        bool attacked = convoy_.attacked != 0;
        for (int i = 0; i < convoy_.members; ++i) attacked = attacked || members_[i].noticedPlayer;
        if (attacked) {
            Play(kEscapingLine);
            BeginStartUp();
            return;
        }
        if (convoy_.routePoints > 0 && convoy_.nextPoint * 2 >= convoy_.routePoints) Play(kDistance50Line);
    }

    bool BeginStartUp() override {
        if (!Activity::BeginStartUp()) return false;
        GuerrillaVehicles_ConvoyAttacked();
        rfa::LogDetail("Convoy %s: attacked; the run starts", Name());
        return true;
    }

    bool Start() override {
        if (!Activity::Start()) return false;
        GuerrillaVehicles_ConvoyTargetMayExit(false);
        GuerrillaVehicles_ConvoyReleaseAttackers();
        ordersAt_ = GameTime() + 1000 + rand() % 1001;
        messages::Tip("Convoy", 2000);
        return true;
    }

    void FrameActive(float) override {
        if (!Follow()) {
            rfa::LogDetail("Convoy %s: its convoy is gone; the run ends", Name());
            End();
            return;
        }
        FollowHud();
        const int now = GameTime();
        if (now >= ordersAt_) {
            GuerrillaVehicles_ConvoyReleaseAttackers();
            ordersAt_ = now + 1000 + rand() % 1001;
        }
        if (Capture()) FrameCapture(now);
        else FrameDestroy(now);
    }

    void RecordVictory(const Rewards& rewards) override {
        if (victoryRecorded_) return;
        messages::Tip("Guerrilla_Action_Complete", 4000);
        GuerrillaHud_RemovePlayerMarker();  // remove_player_marker
        if (end_) {
            end_->completed = true;
            save::SetInt(SaveSection(end_->node->handle).c_str(), "complete", 1);
        }
        // apply_standard_rewards in the end point's home district (or none).
        const int district = district_;
        district_ = end_ ? end_->homeDistrict : -1;
        Activity::RecordVictory(rewards);
        district_ = district;
        rfa::LogDetail("Convoy %s: won (EDF control -%.0f, morale +%.0f in %s)", Name(), rewards.control, rewards.morale,
                       end_ && liberation::GetDistrict(end_->homeDistrict) ? liberation::GetDistrict(end_->homeDistrict)->name.c_str() : "no district");
    }

    void End() override {
        if (state_ == State::Wait || state_ == State::Shutdown) return;
        if (highlighted_ != kNone) HighlightRemove(highlighted_);
        highlighted_ = kNone;
        Activity::End();
        GuerrillaVehicles_ConvoyRemove();
        ResetConvoy();
    }

private:
    using Member = GuerrillaVehiclesConvoyMember;

    void ResetConvoy() {
        end_ = nullptr;
        level_ = level_ < 1 ? 1 : level_;
        female_ = false;
        played_ = 0;
        capturedLines_ = false;
        winAt_ = doneAt_ = -1;
        stoppedSince_ = -1;
        ordersAt_ = -1;
        for (bool& t : target_) t = false;
        highlighted_ = kNone;  // (a level's start: its highlights went with the old level)
        safehouseMarked_ = false;
        convoy_ = GuerrillaVehiclesConvoyState{};
        for (Member& m : members_) m = Member{};
    }

    const char* Name() const { return end_ && end_->def ? end_->def->name.c_str() : "?"; }
    bool Capture() const { return end_ && end_->def && end_->def->capture; }

    // The convoy now (update_squad_status, update_squad_vehicles); false once it's gone.
    bool Follow() {
        convoy_ = GuerrillaVehiclesConvoyState{};
        convoy_.size = sizeof(convoy_);
        return GuerrillaVehicles_ConvoyStatus(&convoy_, members_, kMostMembers);
    }

    // At the route's end: the FAIL line, the run killed (no loss).
    bool AtDestination() {
        if (!convoy_.atDestination) return false;
        Play(kFailLine);
        rfa::LogDetail("Convoy %s: reached its destination; the run is over (no loss)", Name());
        Kill();
        return true;
    }

    // The capture target (get_target_squad: the first member that was one), or null.
    const Member* Target() const {
        for (int i = 0; i < convoy_.members && i < kMostMembers; ++i)
            if (target_[i]) return &members_[i];
        return nullptr;
    }

    bool PlayerInTarget() const {
        const Member* target = Target();
        void* player = Player();
        if (!target || target->vehicle == kNone || !player) return false;
        return *reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(player) + 0x1160) == target->vehicle;  // the human's vehicle
    }

    bool AnySquadAlive() const {
        for (int i = 0; i < convoy_.members; ++i)
            if (!members_[i].squadKilled) return true;
        return false;
    }

    float FromPlayer(const Member& member) const {
        void* player = Player();
        return player ? Distance(Vector{member.x, member.y, member.z}, PositionOf(player)) : 0.0f;
    }

    void PlayCapturedLines() {
        if (capturedLines_) return;
        capturedLines_ = true;
        Play(kCapturedLine);
        Play(kCapturedResponse);
    }

    void Lose(const char* why) {
        rfa::LogDetail("Convoy %s: lost (%s)", Name(), why);
        RecordLoss();
        End();
    }

    void FrameCapture(int now) {
        const Member* target = Target();
        if (target && (target->driverless || target->leftConvoy)) GuerrillaVehicles_ConvoyAllAttack();
        if (PlayerInTarget()) PlayCapturedLines();
        const bool destroyed = !target || target->vehicleKilled || (target->vehicle == kNone && target->squadKilled);
        if (target && !destroyed && FromPlayer(*target) > kEscape) {
            if (AnySquadAlive()) Play(kFailLine);
            Lose("the target got more than 350 m away: ESCAPED");
            return;
        }
        // process_target_driver_flags: its crew may get out once it is 30 % damaged or has stood still over 2 s (stopped:
        // Guerrilla's speed is 0 exactly; a car at rest here still jitters, so below 0.1 m/s).
        if (target && !destroyed) {
            if (target->speed > 0.1f) stoppedSince_ = -1;
            else if (stoppedSince_ < 0) stoppedSince_ = now;
            GuerrillaVehicles_ConvoyTargetMayExit(target->damage >= 0.3f || (stoppedSince_ >= 0 && now - stoppedSince_ > 2000));
        }
        if (destroyed) {
            if (Player()) {
                Play(kDestroyedLine);
                Play(kDestroyedResponse);
            }
            if (GuerrillaHud_HasPlayerMarker()) GuerrillaHud_RemovePlayerMarker();
            Lose("the target was destroyed: VEHICLE_DESTROYED");
            return;
        }
        if (PlayerInTarget() && GuerrillaVehicles_PlayerInSafehouse()) {
            // target_vehicle_delivered: the tech reward (Tech_Reward: no row has one), the activity level up, the player
            // out, the vehicle locked and stopped, the win, the end.
            level_ = (std::min)(level_ + 1, kMostLevel);
            Play(kDeliveredLine);
            GuerrillaVehicles_ConvoyDelivered(target->vehicle);
            RecordVictory(end_->def->rewards);
            End();
        }
    }

    void FrameDestroy(int now) {
        if (doneAt_ >= 0) {  // destruction_convoy_destroyed: the win 1 s on, then 2 s for the lines and the end
            if (winAt_ >= 0 && now >= winAt_) {
                winAt_ = -1;
                RecordVictory(end_->def->rewards);
            }
            if (now >= doneAt_) {
                Play(kDestroyedLine);
                Play(kDestroyedResponse);
                End();
            }
            return;
        }
        int alive = 0;
        for (int i = 0; i < convoy_.members; ++i) {
            const Member& member = members_[i];
            if (member.vehicleKilled) continue;
            ++alive;
            if (FromPlayer(member) > kEscape) {
                if (AnySquadAlive()) Play(kFailLine);
                Lose("a vehicle of it got more than 350 m away: ESCAPED");
                return;
            }
        }
        if (alive == 0) {
            winAt_ = now + 1000;
            doneAt_ = now + 3000;
            rfa::LogDetail("Convoy %s: every vehicle destroyed", Name());
        }
    }

    // Each frame while offered or running: the target's highlight (do_frame_active_capture's objective_highlight_add,
    // RUNNING, the player not in it) and, in the target, the nav beacon once on the nearest safehouse (render_icons).
    void FollowHud() {
        const Member* target = Capture() && state_ == State::Running ? Target() : nullptr;
        const bool inTarget = PlayerInTarget();
        const unsigned want = target && !inTarget && target->vehicle != kNone && !target->vehicleKilled ? target->vehicle : kNone;
        if (want != highlighted_) {
            if (highlighted_ != kNone) HighlightRemove(highlighted_);
            if (want != kNone) HighlightAdd(want, kHighlightWaypoint, kHighlightDefaultStyle);
            highlighted_ = want;
        }
        if (Capture() && inTarget && !safehouseMarked_) {
            safehouseMarked_ = true;
            const Vector player = PositionOf(Player());
            float at[3] = {};
            if (GuerrillaHud_NearestSafehouse(player.x, player.z, at)) {
                GuerrillaHud_AddPlayerMarker(at[0], at[1], at[2], 0);
                rfa::LogDetail("Convoy %s: in the target; the nav beacon on the nearest safehouse (%.0f, %.0f)", Name(), at[0], at[2]);
            } else {
                rfa::Log("Convoy %s: in the target, but no safehouse is known for the nav beacon (map_icons.txt: build the HUD)", Name());
            }
        }
    }

    void Play(Line line) {
        if (played_ & (1u << line)) return;
        played_ |= 1u << line;
        rfa::LogDetail("Convoy %s: radio line %s%s (not played: the campaign's voices come later)", Name(), kLineTags[line],
                       line == kStartLine || line == kCapturedResponse || line == kDestroyedResponse || line == kEscapingLine || line == kFailLine ||
                               line == kDistance50Line || line == kDeliveredLine
                           ? (female_ ? "_F" : "_M")
                           : "");
    }

    EndPoint* end_ = nullptr;
    int level_ = 1;          // activity_level (0x2245640)
    bool female_ = false;    // responses_are_female
    unsigned played_ = 0;    // radio_line_flags
    bool capturedLines_ = false;
    int winAt_ = -1, doneAt_ = -1;  // victory_timer, and the end after it
    int stoppedSince_ = -1;
    int ordersAt_ = -1;      // update_squad_orders' next
    unsigned highlighted_ = kNone;  // the vehicle highlighted (objective_highlight_add), kNone none
    bool safehouseMarked_ = false;  // the nav beacon put on the nearest safehouse (flag 0x10)
    bool target_[kMostMembers] = {};
    GuerrillaVehiclesConvoyState convoy_{};
    Member members_[kMostMembers]{};
};

ConvoyActivity* g_convoy = nullptr;

using ParseArgumentsFn = void(__cdecl*)(int kinds, int flags);
auto const ParseArguments = reinterpret_cast<ParseArgumentsFn>(0xC7D570);
auto& g_commandRunning = *reinterpret_cast<int*>(0x28FF1C0);
auto const g_commandWord = reinterpret_cast<const char*>(0x28FF0B8);
using ConsolePrintFn = void(__cdecl*)(const char* format, ...);
auto const ConsolePrint = reinterpret_cast<ConsolePrintFn>(0xC7C440);
// rfg_cv_start <row>: a convoy from its end point now (je_cc_01, je_cd_02, ...).
void StartCommand() {
    if (!g_commandRunning || !g_convoy) return;
    ParseArguments(2, 0);
    const std::string name = g_commandWord;
    for (EndPoint& end : g_ends)
        if (end.def && _stricmp(end.def->name.c_str(), name.c_str()) == 0) {
            // A test word: the activities' cooldown (Guerrilla's rule for its offers) is skipped when nothing else
            // stops the start.
            if (CooldownLeft() > 0.0f && !AnyRunning() && RestrictedGroupAllows(Convoy) && liberation::AlertLevel() < 3) {
                rfa::Log("rfg_cv_start: the activities' cooldown (%.0f s more) skipped for %s", CooldownLeft(), end.def->name.c_str());
                ConsolePrint("rfg_cv_start: the activities' cooldown (%.0f s more) skipped\n", CooldownLeft());
                ClearCooldown();
            }
            if (g_convoy->StartAt(end, "the console")) {
                ConsolePrint("rfg_cv_start: %s sets off\n", end.def->name.c_str());
                return;
            }
            char why[160];
            if (AnyRunning()) sprintf_s(why, "an activity is running");
            else if (!RestrictedGroupAllows(Convoy)) sprintf_s(why, "a convoy, courier or raid is offered or running");
            else if (CooldownLeft() > 0.0f) sprintf_s(why, "the activities' cooldown, %.0f s more", CooldownLeft());
            else if (liberation::AlertLevel() >= 3) sprintf_s(why, "the alert is red");
            else sprintf_s(why, "see logs\\rfa_mods.log");
            rfa::Log("rfg_cv_start: couldn't start %s: %s", name.c_str(), why);
            ConsolePrint("rfg_cv_start: couldn't start %s: %s\n", name.c_str(), why);
            return;
        }
    rfa::Log("rfg_cv_start: no convoy end point sends \"%s\" (je_cc_01, je_cc_02, je_cc_03, je_cc_05, je_cd_01..05)", name.c_str());
    ConsolePrint("rfg_cv_start: no convoy end point sends \"%s\"\n", name.c_str());
}

}  // namespace

Activity* MakeConvoy() {
    static ConvoyActivity activity;
    g_convoy = &activity;
    return &activity;
}

bool ConvoysRun() {
    if (!g_convoy) return false;
    Load();
    return g_haveTable;
}

bool ConvoyNodeDone(unsigned node) {
    const EndPoint* end = FindEnd(node);
    return end && end->completed;
}

void ConvoyReloadProgress() { ReadProgress(); }
bool ConvoyScreen(Screen& screen) { return g_convoy && g_convoy->Fill(screen); }
int ConvoyRoute(float* xz, int most) { return g_convoy && g_convoy->RouteShown() ? GuerrillaVehicles_ConvoyRoute(xz, most) : 0; }

}  // namespace campaign::activities

// For guerrilla_vehicles' convoys (campaign_api.h).
bool GuerrillaCampaign_RunsConvoys() {
    using namespace campaign;
    return guerrilla_campaign::Running() && InOurWorld() && activities::ConvoysRun();
}
