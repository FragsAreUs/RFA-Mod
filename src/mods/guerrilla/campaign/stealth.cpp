// Guerrilla's stealth rule, which Armageddon dropped.
//
// In Guerrilla every piece of AI asks one function whom a human is fighting: human_combat_get_target
// (0xA2F920 in Re-Mars-tered, 143 callers: aiming, firing, moving, choosing cover, ...). It returns the
// human behind the fighter's combat target handle, except in single player (not Wrecking Crew, no DLC):
// when that target is the local player and he is fully stealthed (human_is_player_in_stealth 0xA2F8F0:
// his backpack's multi_object_backpack::is_stealth_active), an EDF (team 1) or Marauder (team 3) fighter
// gets no target at all. The fighter keeps its handle and its beliefs; it just has nobody to fight while
// the stealth lasts, and has him again the moment it ends.
//
// Armageddon kept the getter (0x880B70, cdecl: the human; the human behind its +0xF8C, through the world's
// human-by-handle 0x436120; 157 callers) without the stealth test. Here it is wrapped and the test put back
// in our worlds. The stealth state is guerrilla_gear's (GuerrillaGear_PlayerStealthed: 1 while the Stealth
// backpack has the player fully stealthed).
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "gear/gear_api.h"
#include "stealth.h"
#include <string.h>

namespace campaign::stealth {
namespace {

constexpr DWORD kCombatTarget = 0x880B70;
constexpr size_t kCombatTargetPrologue = 6;  // push ebp / mov ebp,esp / mov eax,dword ptr [ebp+8]
const BYTE kCombatTargetStart[kCombatTargetPrologue] = {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08};
using CombatTargetFn = BYTE*(__cdecl*)(BYTE* human);
CombatTargetFn g_combatTarget = nullptr;

constexpr DWORD kTeam = 0x13B4;
constexpr int kEdf = 1, kMarauders = 3;  // the teams that lose the stealthed player (both games' numbers)

bool g_wasStealthed = false;
unsigned g_skipped = 0;             // targets withheld since the stealth began

bool PlayerStealthed() { return GuerrillaGear_PlayerStealthed() != 0; }

BYTE* __cdecl CombatTarget(BYTE* human) {
    BYTE* target = g_combatTarget(human);
    if (!target || !human || target != Player() || !Playing()) return target;
    const int team = *reinterpret_cast<const int*>(human + kTeam);
    if (team != kEdf && team != kMarauders) return target;
    if (!PlayerStealthed()) return target;
    ++g_skipped;
    return nullptr;
}

}  // namespace

void Init() {
    if (memcmp(reinterpret_cast<const void*>(kCombatTarget), kCombatTargetStart, kCombatTargetPrologue) != 0) {
        rfa::Log("Stealth: the game's combat target function isn't as expected; the EDF will target a stealthed player");
        return;
    }
    g_combatTarget = reinterpret_cast<CombatTargetFn>(rfa::HookFunction(kCombatTarget, kCombatTargetPrologue, reinterpret_cast<void*>(&CombatTarget)));
    if (!g_combatTarget) rfa::Log("Stealth: could not hook the game's combat target function; the EDF will target a stealthed player");
}

void Enter() {
    static bool told = false;  // once, as the lookup was made once
    if (!g_combatTarget || told) return;
    told = true;
    rfa::Log(guerrilla_gear::Running() ? "Stealth: the EDF and the Marauders have no target in the player while the Stealth backpack has him fully stealthed"
                                       : "Stealth: guerrilla_gear is off; stealth doesn't hide the player from the EDF");
}

void Frame() {
    if (!g_combatTarget || !guerrilla_gear::Running()) return;
    const bool stealthed = PlayerStealthed();
    if (stealthed == g_wasStealthed) return;
    g_wasStealthed = stealthed;
    if (stealthed) {
        g_skipped = 0;
        rfa::Log("Stealth: the player is fully stealthed; the EDF and the Marauders lose him as their target");
    } else {
        rfa::Log("Stealth: the player is seen again (%u times the EDF or Marauders were given no target while he was stealthed)", g_skipped);
    }
}

}  // namespace campaign::stealth
