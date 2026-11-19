// Guerrilla's action nodes for the people under them (actions.cpp, piece 3a), run by guerrilla_people.cpp.
#pragma once

// Reads mods\guerrilla_people\action_nodes.txt; at start-up.
void StartActions();
// Each frame on the game's thread (from the gameplay update): `ours` is whether the level is one of our worlds.
void ActionsFrame(bool ours);

// The walkers (ambient.cpp, 3b): whether a walker is at an action node now (actions.cpp drives it), and start one at
// the nearest free node within 30 m whose type allows ambient people (true when one was found); stop lets one go
// (ambient.cpp gives the walker back or takes it away).
bool ActionsWalkerBusy(unsigned long human);
bool ActionsWalkerStart(unsigned long human, const float* at);
void ActionsWalkerStop(unsigned long human);
