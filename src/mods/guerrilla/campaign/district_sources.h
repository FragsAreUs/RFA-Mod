// What changes Guerrilla's EDF control and civilian morale by itself in free roam
// (district_sources.cpp): EDF buildings and propaganda destroyed, and people killed.
#pragma once
#include "campaign.h"

namespace campaign::district_sources {

void Init();         // reads edf_buildings.txt and Guerrilla's tweaks; hooks the game's human death
void Enter();        // a world starts
void Frame();        // each frame of play: the buildings
void ReloadState();  // the counted buildings read again from the campaign's save

}  // namespace campaign::district_sources
