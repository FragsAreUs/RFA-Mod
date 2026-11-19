// Guerrilla's EDF reinforcements: at orange and red alert the EDF send squads in, on foot, in vehicles and in
// flyers, by reinforcement_profile.xtbl (reinforcements.cpp).
#pragma once
#include "campaign.h"

namespace campaign::reinforcements {

void Init();   // reinforcement_profile.xtbl and the setting ([GuerrillaCampaign] Reinforcements=)
void Enter();  // a world starts: no squads sent
void Frame();  // each frame of play

}  // namespace campaign::reinforcements
