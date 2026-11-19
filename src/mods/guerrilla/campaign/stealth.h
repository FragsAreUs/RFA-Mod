// Guerrilla's stealth rule (stealth.cpp): the EDF and the Marauders have no target in the player while
// he is fully stealthed (the Stealth backpack, guerrilla_gear).
#pragma once
#include "campaign.h"

namespace campaign::stealth {

void Init();   // hooks the game's combat target getter
void Enter();  // a world starts: finds guerrilla_gear's stealth state
void Frame();  // each frame of play: the log when stealth starts and ends

}  // namespace campaign::stealth
