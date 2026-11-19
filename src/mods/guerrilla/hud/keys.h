// Guerrilla's keys: the keys of the actions that Guerrilla's ported parts add to Armageddon (its world map, the
// backpacks, the map's zoom and pan, the safehouse screens' lists). They are kept in rfa_mods.ini under
// [GuerrillaKeys] and changed in the F7 window Guerrilla > Keys. Part of guerrilla_hud (keys.cpp).
//
// The other parts read the same settings: [GuerrillaKeys] <name>= a letter, a digit, Tab, Enter, Space, PageUp,
// PageDown or F1-F12 (empty: no key). A part that wants to know when one changes asks hud_api.h's
// GuerrillaHud_KeysVersion() (a number that goes up with each change made in the window), or
// GuerrillaHud_Key("<name>") for the key itself (its virtual-key code).
#pragma once

namespace keys {

enum Action {
    Map,         // opens and closes the world map
    Backpack,    // switches the worn backpack on and off (guerrilla_gear)
    MapZoomIn,
    MapZoomOut,
    MapCentre,   // centres the world map on the player
    Up,          // the world map's pan, and the safehouse screens' lists
    Down,
    Left,
    Right,
    TipContinue,  // closes a tool tip that paused the game (Guerrilla's tooltip_resume)
    MinimapZoomIn,   // Guerrilla's ZOOM IN MINIMAP (CBA_MINIMAP_ZOOM_IN)
    MinimapZoomOut,  // ZOOM OUT MINIMAP
    Count
};

void Read();                 // from rfa_mods.ini, as the mod starts
int Key(Action action);      // its virtual-key code, 0 for none
int Version();               // goes up with each change made in the window

}  // namespace keys
