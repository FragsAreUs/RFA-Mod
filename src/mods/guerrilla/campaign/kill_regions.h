// Guerrilla's kill regions (kill_regions.cpp): the bottoms of its pits and chasms, and its minefields,
// which kill the people who come into them.
#pragma once
#include "campaign.h"

namespace campaign::kill_regions {

void Init();                         // reads mods\guerrilla_campaign\kill_regions.txt
void Enter();                        // a world starts
void Frame(bool activityRunning);    // each frame of play (activityRunning: one of Guerrilla's activities is on)

}  // namespace campaign::kill_regions
