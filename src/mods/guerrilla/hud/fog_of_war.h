// Guerrilla's fog of war on its world map (fog_of_war.cpp): which parts of the world you have been near. Part of
// guerrilla_hud. Guerrilla keeps a grid of 512 × 512 cells of 16 m, every one hidden at a new game, and uncovers the
// cells within 100 m of you every frame; its world map covers the hidden ones with drifting clouds. The grid goes
// with each save of the rfg world (guerrilla_save adds it to the save, as Guerrilla's ui_system_save did).
#pragma once

namespace rfa::game { struct lua_State; }

namespace fog_of_war {

// As the interface's Lua is set up: rfg_map_fog() for the world map's script, and the map element's drawing
// replaced so that the fog is drawn where the script's fog element comes (see fog_of_war.cpp).
void Register(rfa::game::lua_State* L);

// Each HUD update in our world, with the player's position (x, y, z): the cells near him uncovered
// (Guerrilla's cartographer::process: fog_of_war_update(player position, 100)).
void Reveal(const float* position);

// Whether a point of the world (x, z metres) is under the fog: true on a hidden cell and outside the grid, false on an
// uncovered one (Guerrilla's fog_of_war_is_posn_obscured).
bool IsObscured(float x, float z);

// A number that goes up whenever a cell changes (for the map's mask: rebuilt only then).
unsigned Revision();

// The grid's bits copied into out (kBitBytes, 32,768), 8 cells to a byte, cell (column, row) at bit
// (row × 512 + column) % 8 of byte (row × 512 + column) / 8, 1 hidden; returns their Revision. Any thread.
constexpr int kBitBytes = 512 * 512 / 8;
unsigned CopyBits(unsigned char* out);

// Where the grid lies: column 0 starts at x = kLeft, row 0 at z = kTop (rows count southwards), 16 m a cell.
constexpr float kLeft = -4088.0f;
constexpr float kTop = 3577.0f;
constexpr float kCellMetres = 16.0f;
constexpr int kCells = 512;

}  // namespace fog_of_war

// For guerrilla_save, when it saves one of our worlds: the grid as Guerrilla's fog_of_war_save wrote it into its save
// game (a header, the 32 KB of bits, an end mark) into buffer (room bytes). Returns the bytes written, or the bytes
// needed when buffer is null or too small; 0 while the HUD part isn't running.
int GuerrillaHud_FogOfWarSave(unsigned char* buffer, int room);

// For guerrilla_save, when one of our worlds opens: the grid back from a save's bytes (as fog_of_war_load read them),
// or every cell hidden for a world started fresh or a save that has none (null, 0), as Guerrilla's new game cleared it.
void GuerrillaHud_FogOfWarLoad(const unsigned char* data, int size);
