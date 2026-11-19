// Guerrilla's undercover rule, which Armageddon dropped.
//
// In Guerrilla the player has an undercover team, the civilians' (human +0xC9C, set by
// human::init_from_h_info), and an NPC's perception takes him for that team rather than his own while
// treat_as_undercover holds (ai_perception::get_effective_disposition 0x665AF0 → treat_as_undercover
// 0x6657B0, Re-Mars-tered):
// - not for anyone with the EDF once the alert level is yellow or more;
// - not for an NPC that already takes him for an enemy (an enemy belief about him);
// - and only where it makes him less hostile (the NPC's disposition to civilians is above its
//   disposition to his own team): the EDF's people (hostile to his side, unfriendly to civilians).
// So at green the EDF leave the player alone, as they leave civilians, until he gives himself away:
// - he attacks one of them: the one struck takes him for an enemy (in Armageddon too: its damage
//   reaction, 0x470EB0 → 0x471180 → 0x46DB30, makes a struck NPC take an unfriendly attacker of a
//   side it is hostile to for an enemy);
// - one of them sees him in a restricted area, an EDF base (record_los_result_unfriendly →
//   go_hostile_on_unfriendly_target; restricted_areas.cpp);
// - the alert level reaches yellow (EDF buildings damaged, its people killed or wrecked vehicles seen,
//   or those who took him for an enemy seeing him: alert_sources.cpp).
// Not ported: an EDF soldier beating down a player carrying an obvious weapon within 3 m
// (start_beatdown; its AI is gone from Armageddon), and the rule's clause for a vehicle driven at an NPC.
//
// Armageddon kept the NPC's side of it (its get_effective_disposition, 0x467B60, still has
// Guerrilla's friendly-fire part) but not the undercover part, and its notice of other people asks the
// teams' disposition straight (0x468B20 → 0x8A04D0). So here the game's human-to-human disposition
// (0x8A04D0, cdecl: the human, the other; the first's disposition toward the second: 0 hostile .. 4
// allied) is wrapped in our worlds: when the other is the player and the first takes his side for
// hostile, it answers as Guerrilla's effective disposition does. The NPC's own code does the rest: an
// NPC that takes him for an unfriendly ignores him, one that takes him for hostile notices him when it
// sees him (its view and line of sight) and fights. The player's own view of the EDF (the reticle,
// guerrilla_hud) is the other way round and stays hostile.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "undercover.h"
#include "alert_sources.h"
#include "campaign.h"
#include "handle_kinds.h"
#include "liberation.h"
#include "restricted_areas.h"
#include <string.h>
#include <string>

namespace campaign::undercover {
namespace {

constexpr DWORD kDisposition = 0x8A04D0;
constexpr size_t kDispositionPrologue = 7;  // push ebp / mov ebp,esp / cmp dword ptr [ebp+8],0
const BYTE kDispositionStart[kDispositionPrologue] = {0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x08, 0x00};
using DispositionFn = int(__cdecl*)(BYTE* human, BYTE* other);
DispositionFn g_disposition = nullptr;

auto const TeamDisposition = reinterpret_cast<int(__cdecl*)(int team, int other)>(0x8A0460);
constexpr DWORD kTeam = 0x13B4, kHandle = 0x68;
constexpr int kHostile = 0, kFriendly = 3;
constexpr int kEdf = 1, kCivilians = 2;  // the teams (both games' numbers); the civilians are the player's undercover team
constexpr int kEnemyBelief = 0;          // AI_BELIEF_ENEMY_EXISTS

int g_lastLook = 0;     // when Frame last counted, on the game's clock
int g_holding = -1;     // how many of the EDF held the player for an enemy at green, at the last count
bool g_wasGreen = true;

int __cdecl Disposition(BYTE* human, BYTE* other) {
    const int disposition = g_disposition(human, other);
    if (disposition != kHostile || !human || !other || human == other || other != Player() || !Playing()) return disposition;
    const int team = *reinterpret_cast<const int*>(human + kTeam);
    const int undercover = TeamDisposition(team, kCivilians);
    if (undercover <= disposition) return disposition;  // he isn't less hostile as a civilian (the Marauders)
    const bool withTheEdf = TeamDisposition(team, kEdf) >= kFriendly;
    if (withTheEdf && liberation::AlertLevel() >= handles::AlertYellow) return disposition;
    if (withTheEdf && restricted_areas::PlayerInside()) return disposition;  // seen in an EDF base: an enemy
    if (alert_sources::Believes(human, kEnemyBelief, *reinterpret_cast<const DWORD*>(other + kHandle))) return disposition;
    return undercover;
}

}  // namespace

void Init() {
    if (rfa::IniInt(L"GuerrillaCampaign", L"Undercover", 1) == 0) {
        rfa::Log("Undercover: off (Undercover=0 in [GuerrillaCampaign]): the EDF take the player for an enemy at any alert level");
        return;
    }
    if (memcmp(reinterpret_cast<const void*>(kDisposition), kDispositionStart, kDispositionPrologue) != 0) {
        rfa::Log("Undercover: the game's disposition function isn't as expected; Guerrilla's undercover rule is left out");
        return;
    }
    g_disposition = reinterpret_cast<DispositionFn>(rfa::HookFunction(kDisposition, kDispositionPrologue, reinterpret_cast<void*>(&Disposition)));
    rfa::Log(g_disposition ? "Undercover: at green the EDF take the player for a civilian until he attacks them, is seen in one of their bases or the alert rises"
                           : "Undercover: could not hook the game's disposition function; the rule is left out");
}

void Frame() {
    if (!g_disposition) return;
    const int now = GameTime();
    if (now >= g_lastLook && now - g_lastLook < 500) return;
    g_lastLook = now;
    const bool green = liberation::AlertLevel() == handles::AlertGreen;
    if (green != g_wasGreen) {
        g_wasGreen = green;
        g_holding = -1;
        const std::string place = restricted_areas::PlayerPlace();
        if (green) rfa::Log("Undercover: green again; the EDF who don't hold the player for an enemy take him for a civilian (the player %s)", place.c_str());
        else rfa::Log("Undercover: the alert rose; all of the EDF take the player for an enemy (the player %s)", place.c_str());
    }
    if (!green) return;
    const int holding = alert_sources::EdfHoldingPlayerForEnemy();
    if ((holding > 0) != (g_holding > 0) && g_holding >= 0) {
        const std::string place = restricted_areas::PlayerPlace();
        if (holding > 0) rfa::Log("Undercover: %d of the EDF hold the player for an enemy (attacked, or seen in one of their bases); the player %s", holding, place.c_str());
        else rfa::Log("Undercover: none of the EDF holds the player for an enemy now; the player %s", place.c_str());
    }
    g_holding = holding;
}

}  // namespace campaign::undercover
