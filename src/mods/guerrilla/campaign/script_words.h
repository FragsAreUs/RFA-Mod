// Guerrilla's script words the mod provides (script_words.cpp), and Guerrilla's events as the
// mod's systems raise them.
#pragma once
#include "script_engine.h"

namespace campaign::words {

// The table of words, and how many there are.
const script::Word* All();
size_t Count();

// Guerrilla's events. Each tells every script waiting for it; the arguments are what Guerrilla's
// event carries.
void PostActivityStatusChange(unsigned node, int state);
void PostCivilianMoraleChange(float before, float after);  // raised or lowered, as it went
void PostEdfControlLowered(float before, float after);
void PostContinuousAttackReachedEnd(int squads);
void PostHouseArrestDefenderChange(unsigned node, int defenders);
void PostHumanStoppedBeingRendered(unsigned human);
void PostMissionVrailCompleted();
void PostObjectiveComplete(unsigned objective);
void PostRaidComplete(unsigned raid, unsigned winningTeam);
void PostRaidStatusChange(unsigned raid, unsigned team, int state);
void PostSatelliteReachesTarget(unsigned target);
void PostDialogueLine(unsigned voiceLine, bool finished);  // started, or finished
void PostAlertLevelRaised(int level);

}  // namespace campaign::words
