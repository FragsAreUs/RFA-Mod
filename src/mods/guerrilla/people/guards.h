// Guerrilla's guard posts (guards.cpp), run by guerrilla_people.cpp.
#pragma once

// Reads mods\guerrilla_world\guard_posts.txt; at start-up.
void StartGuards();
// A squad node spawned its squad (the node-spawn hook, in our worlds): true when the node's parent box is one of
// Guerrilla's guard posts, whose members guards.cpp then puts at the post's guard nodes.
bool GuardsPostSpawned(unsigned long node, unsigned long box, unsigned long squad);
// Each frame on the game's thread: `ours` is whether the level is one of our worlds.
void GuardsFrame(bool ours);
// The human's path query for an NPC: -1 when it isn't a guard at a post; else whether the goal may be asked for (a guard
// goes only to its post's guard nodes, as Guerrilla's).
int GuardsGoalAllowed(unsigned long human, const float* goal);
// A squad sent out from its post (GuerrillaPeople_ReleaseLeash): its members leave their guard nodes for good.
void GuardsReleaseSquad(unsigned long squad);
