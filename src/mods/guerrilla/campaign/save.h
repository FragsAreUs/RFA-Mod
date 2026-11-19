// The campaign's own save: what Guerrilla keeps in its save game that Armageddon's has no place
// for (districts, alert level, each activity node's progress, ...). One file,
// mods\guerrilla_campaign\campaign_save.ini, sections and keys of the systems' choosing.
#pragma once
#include <string>
#include <vector>

namespace campaign::save {

int Int(const char* section, const char* key, int fallback = 0);
float Number(const char* section, const char* key, float fallback = 0.0f);
void SetInt(const char* section, const char* key, int value);
void SetNumber(const char* section, const char* key, float value);

// A section name for an object, by its handle ("node 7A030003").
std::string ForObject(const char* kind, unsigned handle);

// Every key of a section.
std::vector<std::string> Keys(const char* section);

// The whole save as bytes (what a game save carries: GuerrillaCampaign_SaveState), and the save
// replaced by such bytes (or emptied, for a fresh campaign, with none).
std::string Bytes();
void Replace(const unsigned char* data, int size);

}  // namespace campaign::save
