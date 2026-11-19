// Guerrilla's walkers (ambient.cpp), run by guerrilla_people.cpp.
#pragma once

// Reads mods\guerrilla_people\ambient.txt and crews.txt and writes in the log what it found; at start-up.
void StartAmbient();
// Each frame on the game's thread (from the gameplay update): `ours` is whether the level is one of our worlds.
void AmbientFrame(bool ours);
// A walker another mod takes (a convert): given back to the game's AI at the next frame and no longer a walker.
void AmbientLeaveToGame(unsigned long person);
// The spawn resource of the zone at (x, z) (ambient.txt, "rfg_dust", ...), or null where there's none.
const char* AmbientZoneResource(float x, float z);
// The spawn resource the game is given for the one it asks for (a rotating district's version in force, step 4c).
void* AmbientResourceFor(void* resource);
