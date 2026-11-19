// Guerrilla's difficulty in our worlds: its levels and its combat values in place of Armageddon's own.
#pragma once

namespace campaign::difficulty {

// The difficulty level as Guerrilla's tables index it: 0 Easy, 1 Medium, 2 Hard, 3 Insane, from Armageddon's
// own setting (the same numbers). Armageddon's Custom (4), or anything out of range, counts as Medium.
int Level();

// Loads Guerrilla's difficulty.xtbl and wraps Armageddon's difficulty lookup (see difficulty.cpp).
void Init();

}  // namespace campaign::difficulty
