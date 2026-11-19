// The kinds of game handle Guerrilla's scripts name that Armageddon's script reader no longer knows
// (handle_kinds.cpp), and their values.
#pragma once

namespace campaign::handles {

// Guerrilla's enums, with Guerrilla's values (its names, compared in any case, in the comments).
enum AlertLevel : int { AlertGreen = 0, AlertYellow, AlertOrange, AlertRed, kAlertLevels };  // green yellow orange red
enum RaidState : int { RaidWait = 0, RaidWithdraw, RaidStandard, RaidAttack, RaidAny };     // Wait Withdraw Standard Attack (Any)
enum ActivityState : int {                                                                  // Wait Load Ready Starting Running Shutdown
    ActivityWait = 0,
    ActivityLoading,
    ActivityReady,
    ActivityStarting,
    ActivityRunning,
    ActivityShutdown,
};
enum MusicThreshold : int {  // _SILENCE _AMBIENCE _COMBAT_LIGHT_LULL _COMBAT_LIGHT _COMBAT_HEAVY_LULL _COMBAT_HEAVY
    MusicSilence = 0,
    MusicAmbience,
    MusicCombatLightLull,
    MusicCombatLight,
    MusicCombatHeavyLull,
    MusicCombatHeavy,
};
enum DespawnType : int { DespawnLow = 0, DespawnMedium, DespawnHigh, DespawnWithdraw, DespawnRetreat };  // Low Medium High Withdraw Retreat

// Kinds whose values are entries of a table or names of a system (an air strike in
// air_bombing.xtbl, a convoy, a public-address group, ...): the value is the name's place in the
// mod's list of names of that kind, so the system can find its entry by name.
enum class Named { AirBomb, Convoy, PaGroup, Upgrade, VehicleRail, ContinuousAttack, kCount };
const char* NameOf(Named kind, unsigned value);  // null if no such value

// Adds every kind to the script reader.
void AddAll();

}  // namespace campaign::handles
