// Guerrilla's story people at its safehouses (story.cpp), run by guerrilla_people.cpp.
#pragma once

// Reads mods\guerrilla_world\story_nodes.txt; at start-up.
void StartStory();
// Each frame on the game's thread (from the gameplay update): `ours` is whether the level is one of our worlds.
void StoryFrame(bool ours);
// Whether a squad spawn node is a story person's that story.cpp hasn't picked to spawn now: the node spawn then makes
// no squad (story.cpp spawns the special NPC and the VIP it streams in).
bool StoryHoldsNode(const unsigned char* node);
// Guerrilla's disable_safehouse_vip: a VIP character (by name) left out of the safehouses' choice, or let back in.
void StoryDisableVip(const char* character, bool disable);
