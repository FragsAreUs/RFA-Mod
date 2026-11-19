// What the parts of the guerrilla_campaign mod share.
#pragma once
#include <string>
#include <vector>

namespace campaign {

// The world being played ("world" for Armageddon's campaign, "rfg" for the ported world), and
// whether it is one of the worlds named by [GuerrillaCampaign] Worlds=.
const char* WorldName();
bool InOurWorld();

// The mod's own folder, mods\guerrilla_campaign\ next to this DLL: Guerrilla's tables (made from
// the player's copy of Guerrilla, never shipped) and the campaign's save.
const std::wstring& ModFolder();

// A list setting split at commas and spaces.
std::vector<std::string> SplitList(const std::wstring& text);

// The game's objects. An object's handle names it in scripts and saves; the object itself may not
// be loaded (null).
struct Vector {
    float x, y, z;
};
void* ObjectFromHandle(unsigned handle);
Vector PositionOf(const void* object);  // where the object is (its +4, as in Guerrilla's object)
void* Player();                          // the local player, or null

// The game's clock, in milliseconds: it stops while the game is paused.
int GameTime();

// One of our worlds is being played, with the player in it: the systems run.
bool Playing();

}  // namespace campaign
