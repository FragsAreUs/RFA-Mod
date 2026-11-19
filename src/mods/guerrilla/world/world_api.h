// What guerrilla_world gives the other parts of rfa_guerrilla.dll (they were its DLL's exports; each is described
// where it is made, too). While the world part isn't running (guerrilla_world=0 under [Mods]), each answers as its
// missing DLL did: nothing cut, no buildings.
#pragma once

// Cuts the buildings' pieces within `radius` metres of `position` away, as Guerrilla's shape cutter does (for the
// parts that make such things: guerrilla_gear's Rhino charge). Call it on the game's own thread, once per step it
// touches a building.
// - position: x, y, z.
// - turn: the cutter's 3 x 3 rotation, rows right, up, forward (the game's matrix), as Guerrilla passes the charging
//   player's.
// - radius: metres (Guerrilla's charge: 0.95).
// - explosion: an explosion's name in the explosion table, to push and damage what's there as it does (Guerrilla's
//   charge: "mp_rhino_cutter"), or null for a cut alone.
// - source: who made it, by handle (the player), also kept as its owner; 0xFFFFFFFF for none.
// Returns true if the cutter was made.
bool GuerrillaWorld_CutBuildings(const float* position, const float* turn, float radius, const char* explosion,
                                 unsigned long source);

// A building near the minimap, for Guerrilla's minimap (guerrilla_world.cpp, "The buildings, for Guerrilla's minimap").
struct GuerrillaWorldBuilding {
    unsigned long handle;  // its mover
    unsigned long parent;  // the mover it is a piece of, 0xFFFFFFFF none (Guerrilla's icons leave pieces out)
    char chunk[64];        // its chunk's name: Guerrilla's minimap draws the picture "<chunk>_thumb"
    int importance;        // Guerrilla's object_importance_level: -1 unknown, 0 none, 1 low, 2 medium, 3 high
    int team;              // Guerrilla's human_teams: -1 none, 0 guerrilla, 1 EDF, 2 civilian (neutral), 3 marauder
    float destroyedAt;     // its game_destroyed_pct: its health is 1 - min(1, damage / destroyedAt)
    float position[3];     // where it is
    float turn[9];         // its rotation, rows right, up, forward
    float boundsMin[3];    // its box in the world (the game's get_bbox), which Guerrilla sizes the picture by
    float boundsMax[3];
    float damage;          // its damage, as its last damaged event gave it (0 none yet)
    int msSinceDamaged;    // milliseconds since that event, -1 never (Guerrilla: a 5 s flash, the health label for 3 s)
    int destroyed;         // 1 once its destroyed event came
};

// The loaded buildings whose listed place is within a square of half-width `halfSize` metres round (x, z) (Guerrilla
// collects a 250 m box once a second), at most `most` of them into `out`. Returns how many it wrote. In a world that
// isn't ours, none. Call it on the game's thread.
int GuerrillaWorld_Buildings(float x, float z, float halfSize, GuerrillaWorldBuilding* out, int most);
