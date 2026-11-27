// What guerrilla_hud gives the other parts of rfa_guerrilla.dll (they were its DLL's exports; each is described where
// it is made, too). While the HUD part isn't running (guerrilla_hud=0 under [Mods]), each answers as its missing DLL
// did: nothing there (no key, nothing shown).
#pragma once

// Guerrilla's activity-complete screen, for guerrilla_campaign as one of the two kinds it shows ends (guerrilla_hud.cpp,
// "Guerrilla's activity-complete screen"): 1 taken (it opens at the next HUD update the game isn't paused), 0 not
// (one is up or waiting, a mode it doesn't show, or results of another size). results: guerrilla_hud's
// ActivityResults, 44 bytes (size, Guerrilla's activity mode, success, the game log's reason, seven numbers).
int GuerrillaHud_ActivityComplete(const void* results);

// The results GuerrillaHud_ActivityComplete takes (44 bytes): Guerrilla's hud_ui_activity_complete_start(mode, success),
// the game log's reason for a failure, and the numbers its kind's loader read.
struct GuerrillaHudActivityResults {
    int size;           // sizeof, 44
    int mode;           // Guerrilla's activity mode: 1 vehicle rail (Collateral Damage), 4 delivery (Transporter)
    int success;        // 1 won, 0 failed
    int reason;         // a failure's gamelog_complete_reasons (2 died, 4 out of time, ... 12 not enough damage), else 0
    float numbers[7];   // Transporter: the pro, target, best (0 for none) and your times, in ms; Collateral Damage:
                        // the goal, soldiers, structures, vehicles, score, deaths and total, in millions
};

// Whether a radio tag pair's M.O.A.B. is on the maps (guerrilla_hud.cpp, "Guerrilla's radio tags": every third tag
// found shows its own pair's), for guerrilla_campaign's car bomb.
bool GuerrillaHud_BombShown(int pair);

// Guerrilla's keys (keys.cpp, [GuerrillaKeys]): a number that goes up with each change made in the F7 window.
int GuerrillaHud_KeysVersion();

// The virtual-key code of one of Guerrilla's keys by its setting's name ("Backpack", "Map", ...); 0 for none or a
// name not among them.
int GuerrillaHud_Key(const char* name);

// Raises one of Guerrilla's tool tips by its name (tool_tips.cpp; see tool_tips.h): false if it wasn't queued.
bool GuerrillaHud_ToolTip(const char* name, int delayMs, const char* icon);

// Whether one of Guerrilla's tool tips has been shown at least once (ui_tool_tips_tip_has_been_played).
bool GuerrillaHud_ToolTipPlayed(const char* name);

// Guerrilla's nav beacon (guerrilla_hud.cpp, "The nav beacon"; cartographer::add_player_marker RM 0x87DFB0,
// remove_player_marker 0x87E410, has_player_marker 0x84D000): one point on the minimap and the world map, taken away
// once the player is within 20 m of it. attached: set for an activity's icon (the GPS key's), so it goes when that icon
// does (cartographer::render 0x882E41).
void GuerrillaHud_AddPlayerMarker(float x, float y, float z, int attached);
void GuerrillaHud_RemovePlayerMarker();
int GuerrillaHud_HasPlayerMarker();
// The nearest safehouse to (x, z), flat (safehouse_find_nearest_safehouse RM 0xA6B570: visible and enabled ones, from
// map_icons.txt): 1 and where in out[3], or 0 when none is known.
int GuerrillaHud_NearestSafehouse(float x, float z, float* out);
