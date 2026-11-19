// Guerrilla's restricted areas (restricted_areas.cpp): its EDF bases, where the EDF who see the player
// take him for an enemy, and the alert level doesn't fall.
#pragma once
#include "campaign.h"
#include <string>

namespace campaign::restricted_areas {

void Init();   // reads mods\guerrilla_campaign\restricted_areas.txt
void Enter();  // a world starts
void Frame();  // each frame of play: where the player is

// The player (or the vehicle he is in) is inside an enabled area's yellow polygon
// (ai_defense_is_human_in_restricted_area for the player).
bool PlayerInside();

// A point is inside an enabled area's yellow polygon; with forAmbientPeople, areas that let ambient
// people in (allow_ambient_peds) don't count (Guerrilla's test for its ambient NPCs).
bool Inside(float x, float z, bool forAmbientPeople);

// Where the player is, for the log: "at (x, y, z), in the EDF's restricted area 0x... (zone)", or "...,
// N m from the EDF's restricted area 0x... (zone)" (the nearest one's edge).
std::string PlayerPlace();

}  // namespace campaign::restricted_areas
