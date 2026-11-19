// What guerrilla_save gives the other parts of rfa_guerrilla.dll (it was its DLL's export; it is described where it
// is made, too). While the save part isn't running (guerrilla_save=0 under [Mods]), it answers as its missing DLL
// did: no save being made, none made.
#pragma once

// For guerrilla_hud, which shows Guerrilla's saving icon while a save is made and a moment after: 1 while SAVE GAME's
// save is being made in a Guerrilla world, else 0; and through savesMade (if given) how many saves it has made since
// the game started.
int GuerrillaSave_Saving(unsigned* savesMade);
