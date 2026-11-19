// The kinds of game handle Guerrilla's scripts name that Armageddon's script reader dropped with the
// systems behind them. Guerrilla's reader (script_node_parse) turns each into a value by its system's
// lookup: alm_resolve_alert_level, raid_common_resolve_state, activity_state_from_name,
// game_music_get_index_from_name, npc_get_despawn_type (names compared in any case, from tables in
// its exe), air_bomb_definition_get_index_from_name, convoy_definition_get_index_from_name,
// public_address_find_group, upgrades_get_handle, vrail_data_get_type and
// activity_cont_attack_get_index (entries of its tables). The mod does the same with Guerrilla's
// names, and for the table kinds keeps each name it meets, so that a system can find the entry by
// name once its table is read.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "handle_kinds.h"
#include "script_engine.h"
#include <string.h>
#include <string>
#include <vector>

namespace campaign::handles {
namespace {

unsigned FromNames(const char* name, const char* const* names, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (_stricmp(name, names[i]) == 0) return static_cast<unsigned>(i);
    return script::kNoHandle;
}

unsigned AlertLevelFrom(const char* name) {
    static const char* const kNames[] = {"green", "yellow", "orange", "red"};
    return FromNames(name, kNames, 4);
}
unsigned RaidStateFrom(const char* name) {
    // (Guerrilla's table has no name for "any".)
    static const char* const kNames[] = {"Wait", "Withdraw", "Standard", "Attack"};
    return FromNames(name, kNames, 4);
}
unsigned ActivityStateFrom(const char* name) {
    static const char* const kNames[] = {"Wait", "Load", "Ready", "Starting", "Running", "Shutdown"};
    return FromNames(name, kNames, 6);
}
unsigned MusicThresholdFrom(const char* name) {
    static const char* const kNames[] = {"_SILENCE", "_AMBIENCE", "_COMBAT_LIGHT_LULL", "_COMBAT_LIGHT", "_COMBAT_HEAVY_LULL", "_COMBAT_HEAVY"};
    return FromNames(name, kNames, 6);
}
unsigned DespawnTypeFrom(const char* name) {
    static const char* const kNames[] = {"Low", "Medium", "High", "Withdraw", "Retreat"};
    return FromNames(name, kNames, 5);
}

std::vector<std::string> g_names[static_cast<int>(Named::kCount)];

unsigned Intern(Named kind, const char* name) {
    std::vector<std::string>& names = g_names[static_cast<int>(kind)];
    for (size_t i = 0; i < names.size(); ++i)
        if (_stricmp(names[i].c_str(), name) == 0) return static_cast<unsigned>(i);
    names.push_back(name);
    return static_cast<unsigned>(names.size() - 1);
}
unsigned AirBombFrom(const char* name) { return Intern(Named::AirBomb, name); }
unsigned ConvoyFrom(const char* name) { return Intern(Named::Convoy, name); }
unsigned PaGroupFrom(const char* name) { return Intern(Named::PaGroup, name); }
unsigned UpgradeFrom(const char* name) { return Intern(Named::Upgrade, name); }
unsigned VehicleRailFrom(const char* name) { return Intern(Named::VehicleRail, name); }
unsigned ContinuousAttackFrom(const char* name) { return Intern(Named::ContinuousAttack, name); }

}  // namespace

const char* NameOf(Named kind, unsigned value) {
    const std::vector<std::string>& names = g_names[static_cast<int>(kind)];
    return value < names.size() ? names[value].c_str() : nullptr;
}

void AddAll() {
    script::AddHandleKind("alert_level", &AlertLevelFrom);
    script::AddHandleKind("raid_state", &RaidStateFrom);
    script::AddHandleKind("activity_state", &ActivityStateFrom);
    script::AddHandleKind("music_threshold", &MusicThresholdFrom);
    script::AddHandleKind("despawn_type", &DespawnTypeFrom);
    script::AddHandleKind("air_bomb", &AirBombFrom);
    script::AddHandleKind("convoy", &ConvoyFrom);
    script::AddHandleKind("pa_group", &PaGroupFrom);
    script::AddHandleKind("upgrades", &UpgradeFrom);
    script::AddHandleKind("vrail_type", &VehicleRailFrom);
    // Guerrilla's own spelling.
    script::AddHandleKind("cont_atttack_type", &ContinuousAttackFrom);
}

}  // namespace campaign::handles
