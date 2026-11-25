# RFA Animation Tools (Blender add-on)

Make and change Red Faction: Armageddon's animations in Blender: bring in the game's skeletons,
characters and animations, animate, and send the result back to the game.

Blender 3.6 or later (4.x and 5.x included). Nothing else to install: the add-on reads the game's
archives itself.

## Installing

1. Zip the `rfa_animation_tools` folder (or use `rfa_animation_tools.zip` from a release).
2. Blender: **Edit > Preferences > Add-ons > Install...**, pick the zip, tick **RFA Animation Tools**.
3. In the add-on's preferences, set **Game folder** to the game's folder (the one holding
   `build\pc\cache`). **Mod folder** is where `overrides\` goes; leave it empty when RFA-Mod is
   installed into the game folder (as `RFAModLauncher.exe` does).

The tools are in the 3D view's sidebar (**N**), tab **RFA Anim**, and under **File > Import / Export**.

## Working

1. **Character from the Game**: search the game's characters (`humans.vpp_pc`). Its skeleton comes
   in as an armature (from `misc.vpp_pc`, by the mesh's name) and its meshes skinned to it. With an
   RFA armature selected, the meshes go on that one instead. **Skeleton from the Game** brings in a
   skeleton alone (weapons, rigs, monoliths...).
2. **Animation from the Game**: search the game's animations (`anims.vpp_pc` and the DLC's) and
   put one on the selected armature, as a new action (scene set to 60 fps, from frame 0). Start from
   one of the game's animations to keep what it holds that Blender has no place for (see below).
3. Animate. Pose bones' rotations and locations are the game's keys; the armature object's own
   location and rotation are the root track (the character's movement over the clip). Constraints,
   IK and drivers are fine: export takes the pose as seen on each frame.
4. **Export to Game (Override)**: writes the action as `overrides\<archive>\<name>.anim_pc`, in place
   of the game's animation of that name, which the game uses from its next start. **Restore the
   Game's Own** removes it. **Export to File...** writes an `.anim_pc` anywhere.

Export samples every frame from the scene's start to its end (or the action's range), then keeps
only the keys the others can't be worked out from within the tolerances (0.1° and 0.5 mm by
default; or a key every frame). Bones the action has no keys for are left out of the file, so the
game keeps them in their rest pose (or **All bones**).

## How the game's files map to Blender

- The game is Y up and left-handed; Blender Z up and right-handed. A game point (x, y, z) is
  Blender's (x, z, y), as in RFA-Mod's other Blender tools: nothing is mirrored.
- The game's rest pose has no rotations, so every bone points along +Y with no roll: a pose bone's
  rotation is the game's key as it is (in its parent's space), and its location the game's position
  key (added to the bone's rest offset from its parent).
- The armature keeps the skeleton's bone order (custom property `rfa_bones`): files number bones
  by it. Renaming bones is fine; adding or removing them is not.
- An imported action keeps the header's two unknown bytes, the turn and movement over the clip and
  the bones' weight curves (how strongly each bone follows the animation when it's blended over
  another: flinches weigh only the upper body) as custom properties, and gives them back on export.
  The movement over the clip is worked out again from the root track.
- Tags (where weapons and the camera attach) come in as empties on their bones.

## What it can't do yet

- Animations inside containers (`.str2_pc`, such as some vehicles' and the walkers' own) can't be
  replaced one file at a time: export those to a file.
- A new animation (a new name) needs the game's tables to use it (`anim_files.xtbl`, and the action
  or state that plays it, in `anim_actions.xtbl` / `anim_states.xtbl`). The override export only
  replaces animations the game has.
- Character materials come in as placeholders (named by their number), without textures.

## Files

- `rfa_formats.py`: the game's files in plain Python (packfiles, skeletons, animations, character
  meshes), ported from the editor's readers and writers in `src/editor/RfaFormats`. It runs without
  Blender: `python -c "import rfa_formats"`.
- `blender_io.py`: the conversions to and from Blender.
- `__init__.py`: the operators, panel and preferences.
