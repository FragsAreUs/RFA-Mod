// What Guerrilla's alert level hears from the world (alert_sources.cpp): the EDF's people who see
// the player, the EDF's dead, and its vehicles damaged and wrecked.
#pragma once
#include "campaign.h"

namespace campaign::alert_sources {

void Enter();  // a world starts: nobody counted yet
void Frame();  // each frame of play: the EDF's deaths and its vehicles' damage near the player go to the alert

// How many of the EDF's people see the player, have a target and are near enough to engage it
// (Guerrilla's get_hostile_target_count), and whether any does (has_los_to_enemy_player).
int EdfSeeingPlayer();

// What a report's value is multiplied by: the alert multiplier of the vehicle the player is in, or
// of the weapon in the player's hands (alert_value_apply_multiplier).
float PlayerMultiplier();

// The player is in a tank or a walker: there the alert never falls.
bool PlayerInTankOrWalker();

// Whether a person's AI holds a belief of that kind about someone (Guerrilla's ai_belief_types, the
// same numbers in Armageddon: 0 an enemy, 5 the first contact, someone in sight now).
bool Believes(const void* human, int kind, unsigned about);
// How many of the EDF's people hold the player for an enemy.
int EdfHoldingPlayerForEnemy();

}  // namespace campaign::alert_sources
