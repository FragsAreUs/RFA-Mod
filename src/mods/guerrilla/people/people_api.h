// What guerrilla_people gives the other parts of rfa_guerrilla.dll (they were its DLL's exports; each is described
// where it is made, too). While the people part isn't running (guerrilla_people=0 under [Mods]), each answers as its
// missing DLL did: nothing there (no markers, no squad, no nav cell, and the calls that tell it something do
// nothing).
#pragma once

// ---- Markers for Guerrilla's minimap (guerrilla_people.cpp, "Markers for Guerrilla's minimap")

// One marker: a picture on a person, or on the vehicle they are in. Markers only in our worlds; the dead and the gone
// are left out.
struct GuerrillaPeopleMarker {
    unsigned long handle;  // the person
    float x, y, z;         // where to draw it: the person, or the vehicle it is in
    const char* icon;      // the picture's name in Guerrilla's minimap_icon_info.xtbl
    int placement;         // 0 plain (inside the zoom radius), 1 sticky (pinned to the rim), 2 closest sticky (only the 3 nearest), 3 a convert's pop
    unsigned long color;   // 0xAARRGGBB, or 0 for the picture's own colour
    int ageMs;             // a convert's pop: ms since it began (0-500); -1 for the others
};

// A squad's map icon (Guerrilla's map_icon: the picture's name and its placement, 0 plain, 1 sticky, 2 closest sticky),
// drawn on each living member; a null or empty picture takes it off.
void GuerrillaPeople_SetSquadMapIcon(unsigned long squad, const char* icon, int placement);

// A person joins (1 ally, 2 escort) or leaves (0) the player's squads: Guerrilla's sticky blue star on them.
void GuerrillaPeople_SetPlayerSquadMember(unsigned long person, int kind);

// A civilian converted by morale: the gold head's pop on them, queued as Guerrilla's (5 at most, 250 ms apart).
void GuerrillaPeople_Converted(unsigned long person);

// The markers to draw now, up to `capacity`; how many were written. Squads' icons and the player's squads first, then
// the converts' pops.
int GuerrillaPeople_MapMarkers(GuerrillaPeopleMarker* out, int capacity);

// ---- Squads, walkers, nav meshes and the safehouses' VIPs

// The guerrilla squad of `size` (Guerrilla's guerilla_squad_1 to _6) for a place: the world build's version of it for
// the zone's district group there (rfg_<group code>_gsq_<n>), or null (no size 5; no group, as in the wastes). The
// name stays valid.
const char* GuerrillaPeople_GuerrillaSquadAt(int size, float x, float z);

// A walker another part takes over (a civilian turned guerrilla by morale): the walkers let it go to the game's AI.
void GuerrillaPeople_LeaveToGame(unsigned long person);

// For guerrilla_campaign's reinforcements (Guerrilla's build_cell_info): an open nav cell whose centroid is within
// `within` metres of (x, y, z), on the people's mesh or (vehicles 1) the vehicles'; 1 and its centroid in out[3], or 0.
int GuerrillaPeople_OpenNavCell(float x, float y, float z, float within, int vehicles, float* out);

// For guerrilla_campaign's reinforcements (Guerrilla's pick_desired_reinforcement_group): how far (x, y, z) is from the
// nearest border edge of the loaded people's nav mesh, in metres; -1 when none is loaded.
float GuerrillaPeople_NavMeshDistance(float x, float y, float z);

// Guerrilla's disable_safehouse_vip (its scripts' action): a VIP character (by its name: Carmen, Jorge, Kyle) left out
// of the safehouses' choice (disable non-zero) or let back in. For the campaign's scripts.
void GuerrillaPeople_DisableSafehouseVip(const char* character, int disable);

// A squad sent out from its post: its leash is taken off, and with it the refusal of ways out of a guard post's box.
void GuerrillaPeople_ReleaseLeash(unsigned long squadHandle);

// ---- Squad levels (squad_levels.cpp)

// For guerrilla_campaign: a squad it made (by its handle) follows that progression set ("Reinforcement EDF" for the
// EDF's reinforcements), whatever its squad definition; null or "" takes it off.
void GuerrillaPeople_SquadSet(unsigned squad, const char* set);

// The same for every squad made until it's called again with null or "" (for a squad whose members are made before
// its maker has its handle).
void GuerrillaPeople_NextSquadsSet(const char* set);
