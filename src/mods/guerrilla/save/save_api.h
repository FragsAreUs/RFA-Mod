// What guerrilla_save gives the other parts of rfa_guerrilla.dll (it was its DLL's export; it is described where it
// is made, too). While the save part isn't running (guerrilla_save=0 under [Mods]), it answers as its missing DLL
// did: no save being made, none made.
#pragma once

// For guerrilla_hud, which shows Guerrilla's saving icon while a save is made and a moment after: 1 while SAVE GAME's
// save is being made in a Guerrilla world, else 0; and through savesMade (if given) how many saves it has made since
// the game started.
int GuerrillaSave_Saving(unsigned* savesMade);

// For guerrilla_campaign's activities (a Transporter's RETRY: Guerrilla's activity replay, world::save_and_reload with
// the player saved at the activity's player start): the world saved as it is with the player put at position (3 floats)
// facing orientation (9: right, up, forward), then opened again from that save, as LOAD CHECKPOINT opens one (the
// campaign gets the save's state back as from any save). After a death the save is a respawn's (the weapons he had
// alive go with it). Made over the next gameplay steps. False when it can't be asked: the part isn't running, not in a
// Guerrilla world, or a respawn or another reload is already waiting.
bool GuerrillaSave_ReloadAt(const float* position, const float* orientation);

// 1 from GuerrillaSave_ReloadAt's asking until the world opens again from its save (or the reload is given up: its
// line in the log says why), else 0.
int GuerrillaSave_Reloading();

// For guerrilla_campaign, after an activity's EXIT with the player dead (Guerrilla's world::save_and_reload then: he
// wakes at the nearest safehouse): the respawn CONTINUE gives, without the game-over screen. Nowhere to put him: the
// game-over screen instead. True when the reload was asked.
bool GuerrillaSave_Respawn();

// For guerrilla_campaign, when its results screen over a dead player can't come up: Armageddon's game-over screen now.
// (As his death timer runs out, the game-over screen's start asks GuerrillaCampaign_TakePlayerDeath first.)
void GuerrillaSave_GameOver();
