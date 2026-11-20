// What guerrilla_vehicles gives the other parts of rfa_guerrilla.dll (they were its DLL's exports; each is described
// where it is made, too). While the vehicles part isn't running (guerrilla_vehicles=0 under [Mods]), each answers as
// its missing DLL did: no chase is handed over.
#pragma once

// For the people part's Marauder ambushes and the campaign's attacks (crews.cpp): from the next frame on, the
// vehicle's driver (an NPC in seat 0) chases the target, the other occupants keeping their own AI. False if the
// vehicle or target isn't there, the vehicle isn't a car (a flyer or walker keeps its own AI), or nobody but the
// player drives it. Call on the game's thread.
bool GuerrillaVehicles_Chase(unsigned long vehicleHandle, unsigned long targetHandle);

// The same, the chase giving up beyond `giveUpMeters` (flat) from the target, in place of Chase's 250 m: for the
// campaign's attacks, whose squads Guerrilla keeps within 325 m of their target. False also for giveUpMeters <= 0.
bool GuerrillaVehicles_ChaseWithin(unsigned long vehicleHandle, unsigned long targetHandle, float giveUpMeters);

// True while a vehicle handed over with GuerrillaVehicles_Chase(Within) still chases (a chase ends by itself when
// the target is gone or past its give-up distance, the crew gets out to fight, or nobody can drive). Hand it over
// again to restart.
bool GuerrillaVehicles_Chasing(unsigned long vehicleHandle);

// Gives a vehicle handed over with GuerrillaVehicles_Chase back to the game now.
void GuerrillaVehicles_Release(unsigned long vehicleHandle);
