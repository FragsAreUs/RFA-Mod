// Guerrilla's civilians joining the fight by morale (converts.cpp).
#pragma once
#include "campaign.h"

namespace campaign::converts {

void Init();   // the setting
void Enter();  // a world starts: no converts; finds guerrilla_people's calls
void Frame();  // each frame of play
// A human died (district_sources' death hook): when the EDF killed a civilian, those around may rise.
void HumanKilled(BYTE* victim, BYTE* killer);
// A person who leaves as Guerrilla's active despawn: walks away from the player, then taken out of the world
// once out of view 20 m away, or beyond 120 m (the converts, and guerrilla squads sent home).
void SendAway(DWORD person);

}  // namespace campaign::converts
