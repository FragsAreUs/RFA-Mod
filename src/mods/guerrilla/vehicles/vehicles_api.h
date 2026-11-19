// What guerrilla_vehicles gives the other parts of rfa_guerrilla.dll (they were its DLL's exports; each is described
// where it is made, too). While the vehicles part isn't running (guerrilla_vehicles=0 under [Mods]), each answers as
// its missing DLL did: no chase is handed over.
#pragma once

// For the people part's Marauder ambushes (crews.cpp): from the next frame on, the vehicle's driver (an NPC in seat 0)
// chases the target, the other occupants keeping their own AI. False if the vehicle or target isn't there, or
// nobody but the player drives it. Call on the game's thread.
bool GuerrillaVehicles_Chase(unsigned long vehicleHandle, unsigned long targetHandle);

// Gives a vehicle handed over with GuerrillaVehicles_Chase back to the game now.
void GuerrillaVehicles_Release(unsigned long vehicleHandle);
