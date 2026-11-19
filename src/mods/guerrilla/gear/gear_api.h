// What guerrilla_gear gives the other parts of rfa_guerrilla.dll (they were its DLL's exports; each is described
// where it is made, too). While the gear part isn't running (guerrilla_gear=0 under [Mods]), each answers as its
// missing DLL did: nothing there (no pack, no explosives, no upgrades).
#pragma once

// ---- Backpacks (backpacks.cpp)

// For guerrilla_hud's backpack icon and fuel meter: the worn pack's Unique_ID (0 the jetpack, 1 Thrust ... 9 Tremor;
// -1 none), and, where asked, its fuel (0 to 1), whether it is on (the jetpack: pushing) and its HUD icon
// (mp_backpacks.xtbl icon_name, e.g. ui_hud_backpack_icon_thrust; null for none). Read on any thread: the values are
// the game thread's last.
int GuerrillaGear_Backpack(float* fuel, int* on, const char** icon);

// For guerrilla_world's backpacks' fuel gauge shader (gw_fuelgauge0c): Guerrilla's mp_fuels as
// multi_object_backpack::update_render (RM 0x97D210) sets it on the pack: its fuel; the fuel it needs to go on; 1
// while it is on; 0. All 0 with none worn. Returns 1 with one worn.
int GuerrillaGear_PackGauge(float* out);

// For guerrilla_campaign's EDF sight: whether the player is wholly stealthed by the Stealth pack
// (multi_object_backpack::is_fully_stealthed): Stealth on, faded in, no re-stealth waiting after an attack. 1 or 0.
int GuerrillaGear_PlayerStealthed();

// ---- The placed explosives (guerrilla_gear.cpp), for guerrilla_hud's minimap and the demolitions master

struct GuerrillaGearExplosive {
    float x, y, z;  // where it is
    int kind;       // 0 a remote charge, 1 a mine, 2 a singularity bomb
    int stuck;      // 1 when stuck to something (its parent or a constraint)
    int players;    // 1 when the player threw it (its owner is the player)
};

// Fills up to `most` records (none when `out` is null) and returns how many there are, which may be more. Call it on
// the game's thread: it reads the world's list as it is.
int GuerrillaGear_PlacedExplosives(GuerrillaGearExplosive* out, int most);

// ---- Upgrades (upgrades.cpp), for guerrilla_hud's safehouse shop and district-liberated screen

// One upgrade, as the shop lists it. It starts with its size, which the caller sets.
struct GuerrillaGearUpgrade {
    int size;               // sizeof(GuerrillaGearUpgrade), set by the caller
    char name[64];          // upgrades.txt's name (Guerrilla's upgrades.xtbl Name)
    char shown[96];         // its name as Guerrilla shows it
    char type[32];          // Weapon, Weapon Upgrade, General, Backpack
    char levelName[96];     // the next level's name ("" when all are bought)
    char levelText[256];    // and its description
    int bought;             // levels bought (0: none past the first)
    int levels;             // levels in all (the first is what you start with)
    int cost;               // the next level's cost, at this difficulty (0 when all are bought)
    int available;          // 1: the next level can be bought now (its chain met), 0: not yet or all bought
};

// How many upgrades there are.
int GuerrillaGear_UpgradeCount();

// Upgrade `index` into *out: false for no such upgrade (or out's size isn't this struct's).
bool GuerrillaGear_Upgrade(int index, GuerrillaGearUpgrade* out);

// One upgrade a district's liberation unlocked. It starts with its size, which the caller sets.
struct GuerrillaGearUnlock {
    int size;             // sizeof(GuerrillaGearUnlock), set by the caller
    char name[64];        // upgrades.txt's name (Guerrilla's upgrades.xtbl Name: its pictures' key)
    char levelName[96];   // the level unlocked, as Guerrilla shows it
};

// What a district's liberation unlocked, as Guerrilla's upgrade_unlock_from_liberation (RM 0x790E20) lists it (three
// at most): how many were written into out (`most` at most).
int GuerrillaGear_LiberationUnlocks(const char* district, GuerrillaGearUnlock* out, int most);

// Buys the next level of an upgrade, on the game's thread at the player's next step; GuerrillaGear_Status says how it
// went.
void GuerrillaGear_Buy(int index);

// Buys it now: only on the game's thread (the shop calls it from the HUD's update, which runs while the game is
// paused, when the player's step doesn't). GuerrillaGear_Status says how it went.
void GuerrillaGear_BuyNow(unsigned char* player, int index);

// The level bought of the upgrade of that name (upgrades.txt's, or one of RFA-Mod's own): 0 for none, or for no such
// upgrade. From any thread; guerrilla_vehicles asks for "rfa_turret_link" while the player drives with a gun.
int GuerrillaGear_UpgradeLevel(const char* name);

// How the last purchase went, as a line of text into out ("" before any).
void GuerrillaGear_Status(char* out, int size);
