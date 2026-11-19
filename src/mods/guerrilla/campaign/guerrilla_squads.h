// Guerrilla's Red Faction squads joining the player's fights by morale (guerrilla_squads.cpp).
#pragma once
#include "campaign.h"

namespace campaign::guerrilla_squads {

void Init();   // guerrilla_reinforcement.xtbl and the setting
void Enter();  // a world starts: no squad; finds guerrilla_people's calls
void Frame();  // each frame of play

}  // namespace campaign::guerrilla_squads
