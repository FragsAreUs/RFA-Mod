// Guerrilla's difficulty in our worlds: its combat values in place of Armageddon's own.
//
// Both games keep a table of difficulty items, each with a value per level, read through one lookup:
//   - Guerrilla: difficulty_get_value (RM 0x772E10) over Difficulty_values, float[27][5].
//   - Armageddon: 0x4FE3D0, cdecl (item, level), over 0x12D8F70: 26 items × 5 levels.
//     - Level 5 means the current one. An item past 25 gives 1.0.
//     - Every item gives 1.0 while the byte 0x12D9178 is set.
//     - In multiplayer (the byte 0x2685118) the value is × a multiplayer table's (0xA342E0).
// The current level is 0x10F7E98, in both games' numbers: 0 Easy, 1 Medium, 2 Hard, 3 Insane, 4 Custom.
//
// Armageddon's code asks for its items by number. In our worlds the lookup is wrapped: each of Armageddon's items
// that is one of Guerrilla's gives Guerrilla's value, from the player's copy of difficulty.xtbl.
//
//   Armageddon item              ← Guerrilla item (who reads it in Guerrilla)
//    0 ENEMY_DAMAGE_TAKEN         ← 0 ENEMY_DAMAGE_TAKEN (human_damage)
//    1 ENEMY_VEHICLE_DAMAGE       ← 1 ENEMY_VEHICLE_DAMAGE (vehicle_apply_damage)
//    2 ENEMY_WEAPON_SPREAD        ← 2 ENEMY_WEAPON_SPREAD (weapon::fire)
//    3 ENEMY_AIM_DRIFT            ← 3 ENEMY_AIM_DRIFT (npc::weapon_drift_process)
//    4 ENEMY_BURST_SIZE           ← 4 ENEMY_BURST_SIZE (weapon_firing_pattern::get_burst_size)
//    5 ENEMY_PHYSICS_COLLISIONS   ← 16 ENV_PHYSICS_COLLISIONS (human_collide_mover_damage, RM 0xAD08A0: every human)
//    6 PLAYER_DAMAGE_TAKEN        ← 7 PLAYER_DAMAGE_TAKEN (human_damage)
//    7 PLAYER_REGEN_DELAY         ← 8 PLAYER_REGEN_DELAY (human_health_damage_register)
//    8 PLAYER_REGEN_RATE          ← 9 PLAYER_REGEN_RATE (human_restore_health)
//   11 PLAYER_VEHICLE_DAMAGE      ← 12 PLAYER_VEHICLE_DAMAGE (vehicle_apply_damage)
//   12 PLAYER_VEHICLE_MASS        ← 13 PLAYER_VEHICLE_MASS (never read in Guerrilla; 1 at every level)
//   13 PLAYER_PHYSICS_COLLISIONS  ← 16 ENV_PHYSICS_COLLISIONS
//   19 ENV_EXPLOSION_DAMAGE       ← 15 ENV_EXPLOSION_DAMAGE (human_combat_process_explosion_hit RM 0xADBB30: only a
//                                    human whose sub type is OT_SUB_HUMAN_PLAYER, outside multiplayer)
//   20 PLAYER_FALLING_DAMAGE      ← 17 PLAYER_FALLING_DAMAGE (human_move_fall_do_landing_damage RM 0xAD4D90: the
//                                    local player only)
//
// Armageddon's item numbers, from the code that asks for them:
//   - 6 in the human damage code (0x85E0C0); 1 and 11 in the vehicle's (0x6077A0);
//   - 2 in weapon firing (0x734840); 4 in the burst (0x71EB10);
//   - 8 in health restore (0x857E10), after the player test 0x7B2660;
//   - 5 and 13 either side of the player test 0x7B2600 in one collision function (0x874050);
//   - 19 after the same test in an explosion hit (0x87E520); 20 in the fall (0x893D20).
// Their names are in that order in the exe.
//
// Left as Armageddon's:
//   - 9 PLAYER_AMMO_PICKUP and 10 PLAYER_AMMO_CRATES: guerrilla_gear and guerrilla_hud apply Guerrilla's to its guns
//     and ammo boxes themselves.
//   - Its own systems' items: hero delay and invulnerability, fine aim, pods, salvage, multiplayer, the Nano Forge,
//     the monolith.
// Guerrilla's PLAYER_DEATH_BUFFER (14) has no Armageddon item. Its alert, upgrade, morale and control items are read
// by liberation.cpp and guerrilla_gear themselves. ENEMY_REINFORCEMENTS (5) and the per-district CONTROL_* (23-26)
// are read by no code in Guerrilla.
//
// Levels: Guerrilla's table has Easy, Medium, Hard and Insane. Its Custom level starts as a copy of the level
// the player was on (switch_to_custom RM 0x772D80, a debug path). Armageddon's Custom (4) has no Guerrilla column,
// so it reads as Medium, here and for the campaign's own items (Level()).
//
// [GuerrillaCampaign] GuerrillaDifficulty=0 keeps Armageddon's values everywhere (on unless set).
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "campaign.h"
#include "difficulty.h"
#include "tables.h"
#include <string.h>

namespace campaign::difficulty {
namespace {

constexpr DWORD kLookup = 0x4FE3D0;
constexpr size_t kLookupPrologue = 6;  // push ebp / mov ebp,esp / sub esp,8
const BYTE kLookupStart[kLookupPrologue] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08};
using LookupFn = float(__cdecl*)(int item, int level);
LookupFn g_lookup = nullptr;

const auto& g_level = *reinterpret_cast<const int*>(0x10F7E98);
const auto& g_allOnes = *reinterpret_cast<const BYTE*>(0x12D9178);
const auto& g_multiplayer = *reinterpret_cast<const BYTE*>(0x2685118);

constexpr int kArmageddonItems = 26;
constexpr int kCurrentLevel = 5;

struct Mapped {
    int armageddon;
    const char* guerrilla;  // the item's name in Guerrilla's difficulty.xtbl
};
const Mapped kMapped[] = {
    {0, "MENU_ENEMY_DAMAGE_TAKEN"},      {1, "MENU_ENEMY_VEHICLE_DAMAGE"},    {2, "MENU_ENEMY_WEAPON_SPREAD"},
    {3, "MENU_ENEMY_AIM_DRIFT"},         {4, "MENU_ENEMY_BURST_SIZE"},        {5, "MENU_ENV_PHYSICS_COLLISIONS"},
    {6, "MENU_PLAYER_DAMAGE_TAKEN"},     {7, "MENU_PLAYER_REGEN_DELAY"},      {8, "MENU_PLAYER_REGEN_RATE"},
    {11, "MENU_PLAYER_VEHICLE_DAMAGE"},  {12, "MENU_PLAYER_VEHICLE_MASS"},    {13, "MENU_ENV_PHYSICS_COLLISIONS"},
    {19, "MENU_ENV_EXPLOSION_DAMAGE"},   {20, "MENU_PLAYER_FALLING_DAMAGE"},
};

// Guerrilla's values by Armageddon's item: Easy, Medium, Hard, Insane; `known` false where none is taken.
struct Values {
    bool known = false;
    float level[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};
Values g_values[kArmageddonItems];

int ToGuerrillaLevel(int level) { return level < 0 || level > 3 ? 1 : level; }

float __cdecl Lookup(int item, int level) {
    if (item < 0 || item >= kArmageddonItems || !g_values[item].known || g_allOnes || g_multiplayer || !InOurWorld())
        return g_lookup(item, level);
    return g_values[item].level[ToGuerrillaLevel(level == kCurrentLevel ? g_level : level)];
}

// The difficulty item of that name, wherever the table keeps it (its items are lists inside its entry).
const tables::Element* Item(const tables::Element& in, const char* name) {
    if (_stricmp(in.Text("Name").c_str(), name) == 0 && in.Child("Easy")) return &in;
    for (const tables::Element& child : in.children)
        if (const tables::Element* found = Item(child, name)) return found;
    return nullptr;
}

int LoadValues() {
    std::vector<tables::Element> rows;
    if (!tables::Load("difficulty", rows)) return 0;
    int taken = 0;
    for (const Mapped& mapped : kMapped) {
        const tables::Element* item = nullptr;
        for (const tables::Element& row : rows)
            if ((item = Item(row, mapped.guerrilla)) != nullptr) break;
        if (!item) {
            rfa::Log("Difficulty: Guerrilla's difficulty.xtbl has no %s; Armageddon's item %d keeps its own values", mapped.guerrilla,
                     mapped.armageddon);
            continue;
        }
        Values& values = g_values[mapped.armageddon];
        const char* const levels[] = {"Easy", "Medium", "Hard", "Insane"};
        for (int level = 0; level < 4; ++level) values.level[level] = item->Number(levels[level], 1.0f);
        values.known = true;
        ++taken;
    }
    return taken;
}

}  // namespace

int Level() { return ToGuerrillaLevel(g_level); }

void Init() {
    if (rfa::IniInt(L"GuerrillaCampaign", L"GuerrillaDifficulty", 1) == 0) {
        rfa::Log("Difficulty: Armageddon's own combat values in every world (GuerrillaDifficulty=0)");
        return;
    }
    const int taken = LoadValues();
    if (taken == 0) return;
    if (memcmp(reinterpret_cast<const void*>(kLookup), kLookupStart, kLookupPrologue) != 0) {
        rfa::Log("Difficulty: the game's difficulty lookup (0x%08lX) isn't as expected; Armageddon's combat values stay", kLookup);
        return;
    }
    g_lookup = reinterpret_cast<LookupFn>(rfa::HookFunction(kLookup, kLookupPrologue, reinterpret_cast<void*>(&Lookup)));
    if (!g_lookup) {
        rfa::Log("Difficulty: could not hook the game's difficulty lookup; Armageddon's combat values stay");
        return;
    }
    rfa::Log("Difficulty: in our worlds %d of Armageddon's difficulty items take Guerrilla's values (enemy damage, vehicle damage, "
             "spread, aim drift, bursts, collisions, the player's damage, regen delay and rate, vehicle damage and mass, explosions, "
             "falls); Custom reads as Medium",
             taken);
}

}  // namespace campaign::difficulty
