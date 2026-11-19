// Guerrilla's Marauder ambushes (ambush.cpp), run by guerrilla_people.cpp.
#pragma once

// Reads mods\guerrilla_world\ambush_regions.txt and hooks the squad node's orders; at start-up.
void StartAmbushes();
// Each frame on the game's thread (from the gameplay update): `ours` is whether the level is one of our worlds.
void AmbushFrame(bool ours);
// Whether a squad spawn node belongs to an ambush region and isn't the one being fired now: the node spawn then makes
// no squad (the region spawns its nodes when it fires).
bool AmbushHoldsNode(const unsigned char* node);
