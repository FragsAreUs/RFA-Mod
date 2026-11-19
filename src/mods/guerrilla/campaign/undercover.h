// Guerrilla's undercover rule (undercover.cpp): while the alert level is green, the EDF take the player
// for a civilian until he gives himself away.
#pragma once

namespace campaign::undercover {

void Init();   // [GuerrillaCampaign] Undercover=; hooks the game's human-to-human disposition
void Frame();  // each frame of play: a line in the log when the EDF start or stop holding the player for an enemy at green

}  // namespace campaign::undercover
