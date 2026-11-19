// What guerrilla_kiosks gives the other parts of rfa_guerrilla.dll (they were its DLL's exports; each is described
// where it is made, too). While the kiosks part isn't running (guerrilla_kiosks=0 under [Mods]), each answers as its
// missing DLL did: nothing there (no kiosk broadcasting, no lines unlocked).
#pragma once

// The kiosk broadcasting now (Guerrilla's kiosk_get_active_kiosk: its people take it as something to look at, and
// don't mutter or say their ambient lines near it): 1 and its handle and place (x, y, z), or 0 when none is.
int GuerrillaKiosks_Active(unsigned long* handle, float* place);

// The lines an activity (its activity.xtbl name, e.g. je_ha_01) unlocks, when it is won (Guerrilla's record_victory ->
// kiosk_unlock_from_activity): how many were unlocked.
int GuerrillaKiosks_UnlockActivity(const char* activity);

// The lines a mission (its missions.xtbl name) unlocks, when it is won (Guerrilla's kiosk_unlock_from_mission).
int GuerrillaKiosks_UnlockMission(const char* mission);

// One line, by its voice line's name (the script action kiosk_unlock_line): 1 if it was unlocked.
int GuerrillaKiosks_UnlockLine(const char* line);
