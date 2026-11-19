// Guerrilla's squads' weapons by level (squad_levels.cpp), run by guerrilla_people.cpp.
#pragma once

// Reads mods\guerrilla_world\squad_levels.txt and hooks the squad's member list; at start-up.
void StartSquadLevels();
// Each frame on the game's thread: `ours` is whether the level is one of our worlds.
void SquadLevelsFrame(bool ours);
