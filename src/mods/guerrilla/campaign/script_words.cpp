// Guerrilla's script words the mod provides: every action, event and function of Guerrilla's that
// Armageddon lacks, keeps as an empty function, or changed, with Guerrilla's arguments (in
// Armageddon's types: see script_engine.h). Words left at nullptr aren't ported yet: a script may
// still name them (it loads), and the first use is logged.
//
// The events are Guerrilla's own tests (its Event_callback_* functions): each compares the event
// that happened with the script's arguments.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "script_words.h"
#include "activities.h"
#include "liberation.h"

namespace campaign::words {
namespace {

using namespace script;

template <typename Fn>
void* Callback(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

// ---- Events: each event's values are values[0..3] and numbers[0..1] of the mod's event.

int Matches(bool yes) { return yes ? kEventHandled : kEventIgnored; }

int __cdecl ActivityStatusChange(const EventData* e, Args args) {  // (activity node, activity state)
    if (!IsModEvent(e, EventActivityStatusChange)) return kEventIgnored;
    return Matches(AsObject(args[0]) == e->values[0] && AsHandle(args[1]) == e->values[1]);
}
// Raised past the script's value: before < x <= after. Lowered past it: after <= x < before.
int __cdecl CivilianMoraleRaised(const EventData* e, Args args) {
    if (!IsModEvent(e, EventCivilianMoraleRaised)) return kEventIgnored;
    const float x = AsNumber(args[0]);
    return Matches(e->numbers[0] < x && x <= e->numbers[1]);
}
int __cdecl CivilianMoraleLowered(const EventData* e, Args args) {
    if (!IsModEvent(e, EventCivilianMoraleLowered)) return kEventIgnored;
    const float x = AsNumber(args[0]);
    return Matches(e->numbers[1] <= x && x < e->numbers[0]);
}
int __cdecl EdfControlLowered(const EventData* e, Args args) {
    if (!IsModEvent(e, EventEdfControlLowered)) return kEventIgnored;
    const float x = AsNumber(args[0]);
    return Matches(e->numbers[1] <= x && x < e->numbers[0]);
}
int __cdecl ContinuousAttackNumSquadsReachedEnd(const EventData* e, Args args) {
    if (!IsModEvent(e, EventContinuousAttackReachedEnd)) return kEventIgnored;
    return Matches(static_cast<unsigned>(AsInt(args[0])) == e->values[0]);
}
// (node, defenders): more than that many. Guerrilla also takes the house-arrest nodes linked to the
// script's (activity_house_arrest_nodes_are_relatives); until house arrest is ported, the same node.
int __cdecl HaDefenderChange(const EventData* e, Args args) {
    if (!IsModEvent(e, EventHouseArrestDefenderChange)) return kEventIgnored;
    return Matches(AsObject(args[0]) == e->values[0] && static_cast<int>(e->values[1]) > AsInt(args[1]));
}
int __cdecl HumanStoppedBeingRendered(const EventData* e, Args args) {
    if (!IsModEvent(e, EventHumanStoppedBeingRendered)) return kEventIgnored;
    return Matches(AsObject(args[0]) == e->values[0]);
}
int __cdecl MissionVrailCompleted(const EventData* e, Args) { return Matches(IsModEvent(e, EventMissionVrailCompleted)); }
int __cdecl ObjectiveComplete(const EventData* e, Args args) {
    if (!IsModEvent(e, EventObjectiveComplete)) return kEventIgnored;
    return Matches(AsHandle(args[0]) == e->values[0]);
}
int __cdecl RaidComplete(const EventData* e, Args args) {  // (raid, winning team)
    if (!IsModEvent(e, EventRaidComplete)) return kEventIgnored;
    return Matches(AsObject(args[0]) == e->values[0] && AsHandle(args[1]) == e->values[1]);
}
// (raid, team, new state). Guerrilla also takes raid nodes linked to the script's
// (activity_raid_nodes_are_relatives); until raids are ported, the same node.
int __cdecl RaidStatusChange(const EventData* e, Args args) {
    if (!IsModEvent(e, EventRaidStatusChange)) return kEventIgnored;
    return Matches(AsObject(args[0]) == e->values[0] && AsHandle(args[1]) == e->values[1] && AsHandle(args[2]) == e->values[2]);
}
int __cdecl SatelliteReachesTarget(const EventData* e, Args args) {
    if (!IsModEvent(e, EventSatelliteReachesTarget)) return kEventIgnored;
    return Matches(AsObject(args[0]) == e->values[0]);
}
int __cdecl DialogueLineStarted(const EventData* e, Args args) {
    if (!IsModEvent(e, EventDialogueLineStarted)) return kEventIgnored;
    return Matches(AsHandle(args[0]) == e->values[0]);
}
// (level): the alert has just risen to that level or above.
int __cdecl AlertLevelRaised(const EventData* e, Args args) {
    if (!IsModEvent(e, EventAlertLevelRaised)) return kEventIgnored;
    return Matches(static_cast<int>(e->values[0]) >= AsInt(args[0]));
}
int __cdecl DialogueLineFinished(const EventData* e, Args args) {
    if (!IsModEvent(e, EventDialogueLineFinished)) return kEventIgnored;
    return Matches(AsHandle(args[0]) == e->values[0]);
}

void Post(ModEvent kind, unsigned a = 0, unsigned b = 0, unsigned c = 0) {
    EventData event;
    event.kind = kind;
    event.values[0] = a;
    event.values[1] = b;
    event.values[2] = c;
    script::Post(event);
}

void PostNumbers(ModEvent kind, float before, float after) {
    EventData event;
    event.kind = kind;
    event.numbers[0] = before;
    event.numbers[1] = after;
    script::Post(event);
}

// ---- Actions and functions (Guerrilla's Action_callback_* and Function_callback_*)

float Flag(bool yes) { return yes ? 1.0f : 0.0f; }

// The alert level: caps, levels (raised or lowered only past the current one) and the value.
void __cdecl CapAlertLevel(Args args) { liberation::SetAlertCap(AsInt(args[0]), AsInt(args[1])); }
void __cdecl RaiseAlertLevel(Args args) {
    if (AsInt(args[0]) > liberation::AlertLevel()) liberation::SetAlertLevel(AsInt(args[0]));
}
void __cdecl LowerAlertLevel(Args args) {
    if (AsInt(args[0]) < liberation::AlertLevel()) liberation::SetAlertLevel(AsInt(args[0]));
}
void __cdecl RaiseAlertValue(Args args) { liberation::SetAlertValue(liberation::AlertValue() + AsNumber(args[0])); }
void __cdecl LowerAlertValue(Args args) { liberation::SetAlertValue(liberation::AlertValue() - AsNumber(args[0])); }
void __cdecl GetAlertLevel(Variant& value, Variant&, Args) { value.number = static_cast<float>(liberation::AlertLevel()); }
void __cdecl GetAlertValue(Variant& value, Variant&, Args) { value.number = liberation::AlertValue(); }

// Morale, in the player's district.
void __cdecl SetCivilianMorale(Args args) { liberation::SetMorale(AsNumber(args[0]), -1); }
void __cdecl RaiseCivilianMorale(Args args) { liberation::ChangeMorale(AsNumber(args[0]), -1); }
void __cdecl LowerCivilianMorale(Args args) { liberation::ChangeMorale(-AsNumber(args[0]), -1); }
void __cdecl TemporaryMoraleStart(Args) { liberation::UseTemporaryMorale(true); }
void __cdecl TemporaryMoraleEnd(Args) { liberation::UseTemporaryMorale(false); }
void __cdecl TemporaryMoraleSet(Args args) { liberation::ChangeTemporaryMorale(AsNumber(args[0]), true); }
void __cdecl TemporaryMoraleIncrease(Args args) { liberation::ChangeTemporaryMorale(AsNumber(args[0]), false); }
void __cdecl TemporaryMoraleDecrease(Args args) { liberation::ChangeTemporaryMorale(-AsNumber(args[0]), false); }
void __cdecl MoraleOffsetSet(Args args) { liberation::ChangeMoraleOffset(AsNumber(args[0]), true); }
void __cdecl MoraleOffsetIncrease(Args args) { liberation::ChangeMoraleOffset(AsNumber(args[0]), false); }
void __cdecl MoraleOffsetDecrease(Args args) { liberation::ChangeMoraleOffset(-AsNumber(args[0]), false); }
// Guerrilla's own does nothing (propaganda's morale comes from destroying it, not from the script).
void __cdecl PropagandaDestroyed(Args) {}

// EDF control: Guerrilla's callback lowers it by twice the script's number.
void __cdecl LowerEdfControl(Args args) { liberation::ChangeEdfControl(-2.0f * AsNumber(args[0]), -1); }
void __cdecl LowerEdfControlAtObject(Args args) {
    const void* object = ObjectFromHandle(AsObject(args[1]));
    liberation::ChangeEdfControl(-2.0f * AsNumber(args[0]), object ? liberation::DistrictAt(PositionOf(object)) : -1);
}
void __cdecl DistrictControl(Variant& value, Variant&, Args) {
    const liberation::District* district = liberation::GetDistrict(liberation::PlayerDistrict());
    value.number = district ? district->control : 0.0f;
}
void __cdecl DistrictSpecificControl(Variant& value, Variant&, Args args) {
    const liberation::District* district = liberation::GetDistrict(liberation::DistrictNamed(AsString(args[0])));
    value.number = district ? district->control : 0.0f;
}

// Tech level (set_tech_level only raises it; change_tech_level adds, held to 0..5) and progression.
void __cdecl SetTechLevel(Args args) {
    if (AsInt(args[0]) > liberation::TechLevel()) liberation::SetTechLevel(AsInt(args[0]));
}
void __cdecl ChangeTechLevel(Args args) {
    const int level = liberation::TechLevel() + AsInt(args[0]);
    liberation::SetTechLevel(level < 0 ? 0 : level > 5 ? 5 : level);
}
void __cdecl SetEdfProgressionLevel(Args args) { liberation::SetEdfProgression(AsInt(args[0])); }
void __cdecl SetMarauderProgression(Args args) { liberation::SetMarauderProgression(AsInt(args[0])); }

// Activities.
void __cdecl ActivityIsActive(Variant& value, Variant&, Args) { value.number = Flag(activities::AnyRunning()); }
void __cdecl ActivitySetSpawn(Args args) {
    if (AsBool(args[0]))
        activities::Reinstate(activities::ByScript, false);
    else
        activities::DisableOffers(activities::ByScript);
}
void __cdecl ActivityDisableDiversions(Args args) {
    if (AsBool(args[0]))
        activities::Disable(activities::ByScript, true);
    else
        activities::Reinstate(activities::ByScript, true);
}
// (Guerrilla also starts a raid when the node is a raid node: not ported until raids are.)
void __cdecl ActivitySpawn(Args) { activities::Reinstate(activities::ByScript, false); }

// ---- The words

const Word kWords[] = {
    // ---- Activities
    {Kind::Action, "activity_disable_diversions", {{Bool}, 1}, Void, Callback(&ActivityDisableDiversions)},  // empty in Armageddon
    {Kind::Action, "activity_enable_specific", {{ObjectList}, 1}, Void, nullptr},
    {Kind::Action, "activity_set_spawn", {{Bool}, 1}, Void, Callback(&ActivitySetSpawn)},
    {Kind::Action, "activity_spawn", {{Object}, 1}, Void, Callback(&ActivitySpawn)},
    {Kind::Action, "asd_setup_bomber", {{Object, Object, Object, Object, Object}, 5}, Void, nullptr},
    {Kind::Action, "continuous_attack_add_priority_spawn", {{ObjectList}, 1}, Void, nullptr},
    {Kind::Action, "continuous_attack_change_attacker_count", {{Number}, 1}, Void, nullptr},
    {Kind::Action, "continuous_attack_end", {{}, 0}, Void, nullptr},
    {Kind::Action, "continuous_attack_remove_priority_spawn", {{ObjectList}, 1}, Void, nullptr},
    {Kind::Action, "continuous_attack_set_attacker_count", {{Number}, 1}, Void, nullptr},
    {Kind::Action, "continuous_attack_set_spawn_group", {{Number}, 1}, Void, nullptr},
    {Kind::Action, "continuous_attack_start", {{Object, Handle}, 2}, Void, nullptr},
    {Kind::Action, "continuous_support_end", {{ObjectList}, 1}, Void, nullptr},
    {Kind::Action, "continuous_support_start", {{ObjectList, Number}, 2}, Void, nullptr},
    {Kind::Action, "continuous_support_update", {{ObjectList, Object, Number}, 3}, Void, nullptr},
    {Kind::Action, "courier_escape", {{}, 0}, Void, nullptr},
    {Kind::Action, "courier_exit_and_defend", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "drive_to_location_ca", {{Object, Bool}, 2}, Void, nullptr},
    {Kind::Action, "riding_shotgun_start", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "spawn_courier", {{Object}, 1}, Void, nullptr},
    {Kind::Event, "activity_status_change", {{Object, Handle}, 2}, Void, Callback(&ActivityStatusChange)},
    {Kind::Event, "continuous_attack_num_squads_reached_end", {{Number}, 1}, Void, Callback(&ContinuousAttackNumSquadsReachedEnd)},
    {Kind::Event, "ha_defender_change", {{Object, Number}, 2}, Void, Callback(&HaDefenderChange)},
    {Kind::Function, "activity_is_active", {{}, 0}, Bool, Callback(&ActivityIsActive)},
    {Kind::Function, "get_active_courier_squad_handle", {{}, 0}, Object, nullptr},
    // ---- Alert level
    {Kind::Function, "get_alert_level", {{}, 0}, Number, Callback(&GetAlertLevel)},  // Armageddon's reads its own alert
    {Kind::Function, "get_alert_value", {{}, 0}, Number, Callback(&GetAlertValue)},  // Armageddon's reads its own alert
    {Kind::Event, "alert_level_raised", {{Number}, 1}, Void, Callback(&AlertLevelRaised)},  // Armageddon's hears its own alert
    {Kind::Action, "cap_alert_level", {{Number, Number}, 2}, Void, Callback(&CapAlertLevel)},
    {Kind::Action, "lower_alert_level", {{Number}, 1}, Void, Callback(&LowerAlertLevel)},
    {Kind::Action, "lower_alert_value", {{Number}, 1}, Void, Callback(&LowerAlertValue)},
    {Kind::Action, "raise_alert_level", {{Number}, 1}, Void, Callback(&RaiseAlertLevel)},
    {Kind::Action, "raise_alert_value", {{Number}, 1}, Void, Callback(&RaiseAlertValue)},
    // ---- Civilian morale
    {Kind::Action, "lower_civilian_morale", {{Number}, 1}, Void, Callback(&LowerCivilianMorale)},
    {Kind::Action, "morale_offset_decrease", {{Number}, 1}, Void, Callback(&MoraleOffsetDecrease)},
    {Kind::Action, "morale_offset_increase", {{Number}, 1}, Void, Callback(&MoraleOffsetIncrease)},
    {Kind::Action, "morale_offset_set", {{Number}, 1}, Void, Callback(&MoraleOffsetSet)},
    {Kind::Action, "raise_civilian_morale", {{Number}, 1}, Void, Callback(&RaiseCivilianMorale)},
    {Kind::Action, "set_civilian_morale", {{Number}, 1}, Void, Callback(&SetCivilianMorale)},
    {Kind::Action, "temporary_morale_decrease", {{Number}, 1}, Void, Callback(&TemporaryMoraleDecrease)},
    {Kind::Action, "temporary_morale_end", {{}, 0}, Void, Callback(&TemporaryMoraleEnd)},
    {Kind::Action, "temporary_morale_increase", {{Number}, 1}, Void, Callback(&TemporaryMoraleIncrease)},
    {Kind::Action, "temporary_morale_set", {{Number}, 1}, Void, Callback(&TemporaryMoraleSet)},
    {Kind::Action, "temporary_morale_start", {{}, 0}, Void, Callback(&TemporaryMoraleStart)},
    {Kind::Event, "civilian_morale_lowered", {{Number}, 1}, Void, Callback(&CivilianMoraleLowered)},
    {Kind::Event, "civilian_morale_raised", {{Number}, 1}, Void, Callback(&CivilianMoraleRaised)},
    // ---- EDF control and districts
    {Kind::Action, "lower_edf_control", {{Number}, 1}, Void, Callback(&LowerEdfControl)},
    {Kind::Action, "lower_edf_control_at_object", {{Number, Object}, 2}, Void, Callback(&LowerEdfControlAtObject)},
    {Kind::Action, "propaganda_destroyed", {{}, 0}, Void, Callback(&PropagandaDestroyed)},
    {Kind::Event, "edf_control_lowered", {{Number}, 1}, Void, Callback(&EdfControlLowered)},
    {Kind::Function, "district_control", {{}, 0}, Number, Callback(&DistrictControl)},  // empty in Armageddon
    {Kind::Function, "district_specific_control", {{String}, 1}, Number, Callback(&DistrictSpecificControl)},  // empty in Armageddon
    // ---- Tech level and progression
    {Kind::Action, "change_tech_level", {{Number}, 1}, Void, Callback(&ChangeTechLevel)},  // empty in Armageddon
    {Kind::Action, "kiosk_unlock_line", {{Handle}, 1}, Void, nullptr},
    {Kind::Action, "set_edf_progression_level", {{Number}, 1}, Void, Callback(&SetEdfProgressionLevel)},  // empty in Armageddon
    {Kind::Action, "set_marauder_progression", {{Number}, 1}, Void, Callback(&SetMarauderProgression)},  // empty in Armageddon
    {Kind::Action, "set_tech_level", {{Number}, 1}, Void, Callback(&SetTechLevel)},  // empty in Armageddon
    {Kind::Action, "upgrade_unlock_level", {{Handle, Number}, 2}, Void, nullptr},
    {Kind::Function, "player_completed_GoT", {{}, 0}, Bool, nullptr},
    {Kind::Function, "player_has_purchased_nano_forge", {{}, 0}, Bool, nullptr},
    {Kind::Function, "player_has_radiation_upgrade", {{}, 0}, Bool, nullptr},
    // ---- Raids
    {Kind::Action, "raid_register_object", {{Object, ObjectList}, 2}, Void, nullptr},
    {Kind::Action, "set_raid_regions", {{Object, Bool, Object, Object, Object}, 5}, Void, nullptr},
    {Kind::Action, "start_raid", {{Object}, 1}, Void, nullptr},
    {Kind::Event, "raid_complete", {{Object, Handle}, 2}, Void, Callback(&RaidComplete)},
    {Kind::Event, "raid_status_change", {{Object, Handle, Handle}, 3}, Void, Callback(&RaidStatusChange)},
    // ---- Convoys
    {Kind::Action, "disable_all_convoys", {{Bool}, 1}, Void, nullptr},
    {Kind::Action, "disable_convoy", {{Object, Bool}, 2}, Void, nullptr},
    {Kind::Action, "disable_convoys_except", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "spawn_convoy", {{Object, Handle}, 2}, Void, nullptr},
    {Kind::Function, "get_vehicle_from_convoy", {{Object, Number}, 2}, Object, nullptr},
    // ---- Air strikes
    {Kind::Action, "air_bomb_attack_list_add", {{Object, Number}, 2}, Void, nullptr},
    {Kind::Action, "air_bomb_attack_list_clear", {{}, 0}, Void, nullptr},
    {Kind::Action, "air_bomb_attack_list_remove", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "air_bomb_avoid_list_add", {{Object, Number}, 2}, Void, nullptr},
    {Kind::Action, "air_bomb_avoid_list_clear", {{}, 0}, Void, nullptr},
    {Kind::Action, "air_bomb_avoid_list_remove", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "air_bomb_destroy_target", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "air_bomb_hit_target", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "air_bomb_kill_player", {{}, 0}, Void, nullptr},
    {Kind::Action, "air_bomb_start_following", {{Handle, Object, Object}, 3}, Object, nullptr},
    {Kind::Action, "air_bomb_start_stationary", {{Handle, Object, Object}, 3}, Object, nullptr},
    {Kind::Action, "air_bomb_stop", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "air_bomb_stop_all", {{}, 0}, Void, nullptr},
    {Kind::Action, "air_bomb_stop_type", {{Handle}, 1}, Void, nullptr},
    {Kind::Function, "air_bomb_running", {{Handle}, 1}, Bool, nullptr},
    // ---- Roadblocks
    {Kind::Action, "spawn_roadblock", {{Object}, 1}, Void, nullptr},
    // ---- Guerrilla reinforcements and the player's squad
    {Kind::Action, "disable_guerrilla_reinforcement", {{Bool}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "guerrilla_squad_despawn", {{Object, Bool}, 2}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "player_squad_add_npc", {{Object}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "player_squad_add_squad_members", {{Object}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "player_squad_remove_all", {{}, 0}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "reinforcements_disable", {{}, 0}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "reinforcements_enable", {{}, 0}, Void, nullptr},  // empty in Armageddon
    {Kind::Function, "player_squad_size", {{}, 0}, Number, nullptr},  // empty in Armageddon
    // ---- Vehicle rails
    {Kind::Action, "drive_to_location_vrail", {{Object, Bool}, 2}, Void, nullptr},
    {Kind::Action, "start_mission_vrail", {{Handle, Object, Object}, 3}, Void, nullptr},
    {Kind::Action, "start_mission_vrail_manual", {{Handle, Object, Object, Object}, 4}, Void, nullptr},
    {Kind::Action, "start_vrail", {{Handle, Object, Object}, 3}, Void, nullptr},
    {Kind::Action, "vrail_chase_vehicle_count", {{Number}, 1}, Void, nullptr},
    {Kind::Action, "vrail_clear_spawn_region", {{ObjectList}, 1}, Void, nullptr},
    {Kind::Action, "vrail_play_next_story_section", {{}, 0}, Void, nullptr},
    {Kind::Action, "vrail_set_spawn_region", {{ObjectList}, 1}, Void, nullptr},
    {Kind::Event, "mission_vrail_completed", {{}, 0}, Void, Callback(&MissionVrailCompleted)},
    {Kind::Function, "get_vehicle_from_vrail", {{}, 0}, Object, nullptr},
    {Kind::Function, "vrail_get_patrol_squad", {{}, 0}, Object, nullptr},
    // ---- Satellite camera
    {Kind::Action, "satellite_camera_max_range", {{Number}, 1}, Void, nullptr},
    {Kind::Action, "satellite_camera_position", {{Object, Number, Number, Number, Number}, 5}, Void, nullptr},
    {Kind::Action, "satellite_camera_set_target", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "satellite_camera_show_disconnect_screen", {{}, 0}, Void, nullptr},
    {Kind::Action, "satellite_camera_start", {{}, 0}, Void, nullptr},
    {Kind::Action, "satellite_camera_stop", {{}, 0}, Void, nullptr},
    {Kind::Action, "satellite_set_weapon_source", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "satellite_temp_message", {{String, Number}, 2}, Void, nullptr},
    {Kind::Event, "satellite_reaches_target", {{Object}, 1}, Void, Callback(&SatelliteReachesTarget)},
    // ---- Public address
    {Kind::Action, "add_dialogue_line_on_pa", {{Handle, Handle, Handle, Handle, Number}, 5}, Void, nullptr},
    {Kind::Action, "play_voice_line_on_pa", {{Handle, Handle, Handle}, 3}, Void, nullptr},
    {Kind::Action, "public_address_add_speakers", {{Handle, ObjectList}, 2}, Void, nullptr},
    {Kind::Action, "public_address_override_ambient", {{Handle, Handle, Handle, Number, Number}, 5}, Void, nullptr},
    {Kind::Action, "public_address_remove_speakers", {{ObjectList}, 1}, Void, nullptr},
    {Kind::Action, "public_address_restore_ambient", {{Handle}, 1}, Void, nullptr},
    {Kind::Action, "public_address_stop_ambient", {{Handle}, 1}, Void, nullptr},
    // ---- Objectives, map and HUD
    {Kind::Action, "add_icon", {{Object, Number}, 2}, Void, nullptr},
    {Kind::Action, "add_objective", {{Handle}, 1}, Void, nullptr},
    {Kind::Action, "add_objective_icon", {{Handle, Number, Object}, -3}, Void, nullptr},
    {Kind::Action, "building_destroyed_message", {{String}, 1}, Void, nullptr},
    {Kind::Action, "complete_objective", {{Handle}, 1}, Void, nullptr},
    {Kind::Action, "complete_objective_silent", {{Handle}, 1}, Void, nullptr},
    {Kind::Action, "disable_minimap_static_zone", {{}, 0}, Void, nullptr},
    {Kind::Action, "enable_minimap_static_zone", {{Object, Number}, 2}, Void, nullptr},
    {Kind::Action, "fog_of_war_show_important_buildings", {{String}, 1}, Void, nullptr},
    {Kind::Action, "fullscreen_map_show", {{}, 0}, Void, nullptr},
    {Kind::Action, "log_objective_complete", {{}, 0}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "mission_add_ground_marker_action_node", {{Object, Number}, 2}, Void, nullptr},
    {Kind::Action, "mission_add_ground_marker_offset", {{Object, Number, Number, Number}, 4}, Void, nullptr},
    {Kind::Action, "mission_add_ground_marker_vehicle_entry", {{Object, Number}, 2}, Void, nullptr},
    {Kind::Action, "mission_hostage_icons_remove", {{}, 0}, Void, nullptr},
    {Kind::Action, "mission_hostage_icons_show", {{ObjectList}, 1}, Void, nullptr},
    {Kind::Action, "mission_remove_ground_marker", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "mover_set_visible_over_fow", {{Object, Bool}, 2}, Void, nullptr},
    {Kind::Action, "patrol_path_add_to_map", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "patrol_path_add_to_map_guerrilla", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "patrol_path_remove_from_map", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "post_message", {{String}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "remove_icon", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "remove_objective", {{Handle}, 1}, Void, nullptr},
    {Kind::Action, "remove_player_marker", {{}, 0}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "set_squad_icons", {{Object, Number, Number}, 3}, Void, nullptr},
    {Kind::Action, "show_important_buildings", {{Bool}, 1}, Void, nullptr},
    {Kind::Action, "spawn_squad_with_icons", {{Object, Number, Number}, 3}, Object, nullptr},
    {Kind::Event, "objective_complete", {{Handle}, 1}, Void, Callback(&ObjectiveComplete)},
    // ---- Music
    {Kind::Action, "music_set_limited_level", {{Handle}, 1}, Void, nullptr},
    {Kind::Action, "music_set_minimum_level", {{Handle}, 1}, Void, nullptr},
    // ---- Safehouses
    {Kind::Action, "safehouse_activity_disable", {{ObjectList}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "safehouse_activity_enable", {{ObjectList}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "safehouse_despawn_squad", {{Object, String}, 2}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "safehouse_disable", {{ObjectList}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "safehouse_disable_until_stream", {{ObjectList}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "safehouse_enable", {{ObjectList}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "safehouse_set_visible", {{Bool, ObjectList}, 2}, Void, nullptr},  // empty in Armageddon
    // ---- Dialogue and voice
    {Kind::Action, "add_dialogue_line_on_human", {{Handle, Object, Handle, Number}, 4}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "add_dialogue_line_on_persona", {{Handle, Handle, Handle, Number}, 4}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "add_dialogue_line_on_player", {{Handle, Handle, Number}, 3}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "audio_play_automobile_megaphone", {{Handle, Handle, Object}, 3}, Void, nullptr},  // Armageddon changed its arguments
    {Kind::Action, "briefing_start_voice", {{Handle, Handle}, 2}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "enable_dialogue", {{Bool}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "play_radio_message_on_persona", {{Handle, Handle}, 2}, Void, nullptr},  // Armageddon changed its arguments
    {Kind::Action, "play_unlocking_voice_line_on_human", {{Handle, Object}, 2}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "play_voice_line", {{Object, Handle}, 2}, Void, nullptr},  // Armageddon changed its arguments
    {Kind::Action, "play_voice_line_on_human", {{Handle, Object, Number}, 3}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "play_voice_line_on_persona", {{Handle, Handle, Number}, 3}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "play_voice_line_on_player", {{Handle, Number}, 2}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "player_play_voice_line", {{Handle}, 1}, Void, nullptr},  // Armageddon changed its arguments
    {Kind::Action, "stop_all_dialogue", {{}, 0}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "stop_dialogue", {{Handle, Bool}, 2}, Void, nullptr},  // empty in Armageddon
    {Kind::Event, "dialogue_line_finished", {{Handle}, 1}, Void, Callback(&DialogueLineFinished)},  // Armageddon changed its arguments
    {Kind::Event, "dialogue_line_started", {{Handle}, 1}, Void, Callback(&DialogueLineStarted)},  // Armageddon changed its arguments
    // ---- Characters, squads and orders
    {Kind::Action, "action_node_set_disabled", {{Bool, ObjectList}, 2}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "attack_buildings", {{Number, ObjectList}, 2}, Void, nullptr},
    {Kind::Action, "disable_restricted_area", {{ObjectList}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "disable_spawn_node", {{Object, Bool}, 2}, Void, nullptr},
    {Kind::Action, "disable_threaten_actions", {{Bool}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "enable_restricted_area", {{ObjectList}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "fodder_active_on_object", {{Object}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "force_npc_use_action_node", {{Object, Object}, 2}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "mission_draft_mode", {{}, 0}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "mission_lock_ped_level", {{Handle, Handle}, 2}, Void, nullptr},
    {Kind::Action, "mission_lock_veh_level", {{Handle, Handle}, 2}, Void, nullptr},
    {Kind::Action, "npc_set_ambient_vehicles_disabled", {{Bool}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "orders_attack", {{Object, Object}, 2}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "orders_defend", {{Object, Object}, 2}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "orders_fodder", {{Object, Object}, 2}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "orders_patrol", {{Object, Object}, 2}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "orders_terrorize", {{Object, Object}, 2}, Object, nullptr},  // empty in Armageddon
    {Kind::Action, "orders_terrorize_spawn", {{Object}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "release_squad_to_manager", {{Object}, 1}, Void, nullptr},
    {Kind::Action, "set_complaint_flag", {{Object}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Action, "set_npc_hitpoints", {{Number, ObjectList}, 2}, Void, nullptr},
    {Kind::Action, "spawn_jailed_npc", {{Object}, 1}, Object, nullptr},  // empty in Armageddon
    {Kind::Action, "spawn_marauder_lookout", {{Object, Object, Number}, 3}, Object, nullptr},  // empty in Armageddon
    {Kind::Action, "spawn_npc", {{Object}, 1}, Object, nullptr},  // empty in Armageddon
    {Kind::Action, "spawn_saboteur", {{Object, Object}, 2}, Object, nullptr},  // empty in Armageddon
    {Kind::Action, "squad_despawn_priority", {{Object, Handle}, 2}, Void, nullptr},
    {Kind::Action, "squad_edf_despawn_all", {{}, 0}, Void, nullptr},
    {Kind::Action, "walker_spawn_enable", {{Bool}, 1}, Void, nullptr},  // empty in Armageddon
    {Kind::Event, "human_stopped_being_rendered", {{Object}, 1}, Void, Callback(&HumanStoppedBeingRendered)},
    {Kind::Function, "player_in_restricted_area", {{Object}, 1}, Bool, nullptr},  // empty in Armageddon
};

}  // namespace

const Word* All() { return kWords; }
size_t Count() { return sizeof(kWords) / sizeof(kWords[0]); }

void PostActivityStatusChange(unsigned node, int state) { Post(EventActivityStatusChange, node, static_cast<unsigned>(state)); }
void PostCivilianMoraleChange(float before, float after) {
    if (after != before) PostNumbers(after > before ? EventCivilianMoraleRaised : EventCivilianMoraleLowered, before, after);
}
void PostEdfControlLowered(float before, float after) {
    if (after < before) PostNumbers(EventEdfControlLowered, before, after);
}
void PostContinuousAttackReachedEnd(int squads) { Post(EventContinuousAttackReachedEnd, static_cast<unsigned>(squads)); }
void PostHouseArrestDefenderChange(unsigned node, int defenders) {
    Post(EventHouseArrestDefenderChange, node, static_cast<unsigned>(defenders));
}
void PostHumanStoppedBeingRendered(unsigned human) { Post(EventHumanStoppedBeingRendered, human); }
void PostMissionVrailCompleted() { Post(EventMissionVrailCompleted); }
void PostObjectiveComplete(unsigned objective) { Post(EventObjectiveComplete, objective); }
void PostRaidComplete(unsigned raid, unsigned winningTeam) { Post(EventRaidComplete, raid, winningTeam); }
void PostRaidStatusChange(unsigned raid, unsigned team, int state) {
    Post(EventRaidStatusChange, raid, team, static_cast<unsigned>(state));
}
void PostSatelliteReachesTarget(unsigned target) { Post(EventSatelliteReachesTarget, target); }
void PostAlertLevelRaised(int level) { Post(EventAlertLevelRaised, static_cast<unsigned>(level)); }
void PostDialogueLine(unsigned voiceLine, bool finished) {
    Post(finished ? EventDialogueLineFinished : EventDialogueLineStarted, voiceLine);
}

}  // namespace campaign::words
