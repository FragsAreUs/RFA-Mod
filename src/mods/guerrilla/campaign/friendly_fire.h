// Guerrilla's friendly fire (ai_perception::player_friendly_fire_info, friendly_fire.cpp): a guerrilla who sees
// the player kill one of his friends, or is hurt by him, turns on him for a while.
#pragma once
#include <windows.h>

namespace campaign::friendly_fire {

void Init();   // [GuerrillaCampaign] FriendlyFire=; Guerrilla's numbers from tweak_table.xtbl
void Enter();  // a level starts: every record goes (Guerrilla's perceptions are made again)
void Frame();  // each frame of play: new NPCs' records, the hits, the battle cries, the records that run out

// Guerrilla's get_effective_disposition clause, for undercover.cpp's wrap of the game's human-to-human
// disposition: the NPC's disposition to the other (already worked out) as Guerrilla's perception takes it.
int Effective(const BYTE* npc, const BYTE* other, int disposition);
bool On();

// The player's kills (district_sources.cpp's wrap of the human death; main thread) and the game's damage to an NPC
// who lives on (GuerrillaCampaign_HumanDamaged; it may come from the physics jobs, so it is queued for Frame).
void HumanKilled(BYTE* victim, BYTE* attacker);
void HumanDamaged(BYTE* victim, BYTE* attacker, float done, int kind);

}  // namespace campaign::friendly_fire
