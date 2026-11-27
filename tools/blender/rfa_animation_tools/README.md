# RFA Animation Tools (Blender add-on)

Make and change Red Faction: Armageddon's animations in Blender: bring in the game's skeletons,
characters and animations, animate, and send the result back to the game.

Blender 3.6 or later (4.x and 5.x included). Nothing else to install: the add-on reads the game's
archives itself.

## Installing

1. Zip the `rfa_animation_tools` folder (or use `rfa_animation_tools.zip` from a release).
2. Blender: **Edit > Preferences > Add-ons > Install...**, pick the zip, tick **RFA Animation Tools**.
3. In the add-on's preferences, set **Game folder** to the game's folder (the one holding
   `build\pc\cache`). **Mod folder** is where RFA-Mod is installed (the folder holding
   `rfa_mods.ini` and `mods\`); leave it empty when RFA-Mod is installed into the game folder (as
   `RFAModLauncher.exe` does).

The tools are in the 3D view's sidebar (**N**), tab **RFA Anim**, and under **File > Import / Export**.

## Working

1. **Character from the Game**: search the game's characters (`humans.vpp_pc`). Its skeleton comes
   in as an armature (from `misc.vpp_pc`, by the mesh's name) and its meshes skinned to it, with
   their materials' textures (colour, normal and specular maps, from the character's own texture
   files and those the game always has loaded for characters), packed into the .blend. With an RFA
   armature selected, the meshes go on that one instead. **Skeleton from the Game** brings in a
   skeleton alone (weapons, rigs, monoliths...).
2. **Animation from the Game**: search all the game's animations (`anims.vpp_pc`, `missions.vpp_pc`
   and the DLC's, and those inside containers: the walkers' and vehicles', a level's monsters') and
   put one on the selected armature, as a new action (scene set to 60 fps, from frame 0). Start from
   one of the game's animations to keep what it holds that Blender has no place for (see below).
3. Animate. Pose bones' rotations and locations are the game's keys; the armature object's own
   location and rotation are the root track (the character's movement over the clip). Constraints,
   IK and drivers are fine: export takes the pose as seen on each frame.
4. **Export to Game**: writes the action as `mods\animations\<name>.anim_pc`. RFA-Mod loads every file
   there at the game's start (`[Mods] animations=1`, on by default): in place of the game's
   animation of that name, wherever the game keeps it (an archive or a container), or as a new
   animation. **Restore the Game's Own** removes it. **Export to File...** writes an `.anim_pc`
   anywhere.

   A new name plays where the game is told to play it: an anim set's state or action naming
   `<name>.animx` (the Skeleton_Set tables, in the editor's table editor). The export's window says
   whether the name is the game's or new.

Export samples every frame from the scene's start to its end (or the action's range), then keeps
only the keys the others can't be worked out from within the tolerances (0.1° and 0.5 mm by
default; or a key every frame). Bones the action has no keys for are left out of the file, so the
game keeps them in their rest pose (or **All bones**).

## Vehicles

The **RFA Vehicle** panel (same sidebar tab) builds new vehicles.

1. **New Vehicle**: pick a kind (any of the game's vehicles: the LAV, the ambulance, the tank,
   the combug, the flyers, the bomber, the walkers...) and a name. A kit comes in: one object a
   working part, named as the game names it (`body`, `wheel_fl1`, `steering_joint_fr1`, `door_l`,
   `turret`, `barrel_main`, ...), parented as the parts hang from each other; the lights' meshes;
   the collision (wire hulls, in the "collision" collection); the points (driver and passenger
   seats, engine, gas tank, turrets, cameras: empties in the "points" collection). It comes in laid
   out as the game draws it: the suspension pistons between their two points on the wheel and the
   chassis, the collision stand-ins (`veh_proxy`, `wheelhulls`, ...) and the tank's single tread link
   hidden, and what the game hangs on it from its tables in a "turrets and props" collection: each
   turret's own models (the LAV's gun head, the combug's swivel and head) on the point the vehicle
   names for it, the gun on the turret's head, and props such as the ATV's tassel. Those move with
   the part they sit on (the LAV's gun turns with its turret) and stay out of the vehicle you export
   (the game puts them on from its tables).
2. Model the vehicle: replace each part's mesh with your own (edit it, or parent your own meshes
   under the part's object: everything under a part, down to the next part, is that part), move
   the parts where your model needs them (a wheel where your wheel is), move the seats and points.
   Materials are ordinary Blender ones: the Principled BSDF's **Base Color**, **Normal** (through a
   Normal Map node), **Specular** and **Emission Color** images become the game's colour, normal,
   specular and glow maps (any size: scaled to powers of two, up to 2048). The kind's own materials
   and textures can be kept as they are.
3. Collision: each wire hull is one convex shape round its vertices, for the part it's parented to
   (the body's for the chassis). Reshape them, delete them, or select one of your meshes and press
   **Collision Hull from Mesh**. Keep hulls simple (up to a few dozen corners each).
4. **Export Vehicle to Game**: writes the model, its textures and the kind's effects and animations
   into `overrides\build\pc\cache\<name>.ccar_pc.str2_pc`, adds it to the vehicles' asset list
   and adds a `vehicles.xtbl` entry: the kind's (handling, seats, entering, camera, sounds), under
   your name and display name (also in the Guerrilla world's table, when it's built). From the
   game's next start the spawner lists it. **Remove Vehicle from Game** takes it all out again.

**New Vehicle from Nothing** starts without a game vehicle's parts: a box for a body, 4, 6 or 8
wheels and the seats (driver, passengers) and points (engine, gas tank, exhaust) as empties. Model
over them; **Make Part** (the row of buttons in the panel) turns any selected mesh into a front wheel
(the first axle, which steers), a rear wheel, a separate piece, or back into the body. Everything
else under the body is the body. Collision: wire hulls under a part (or under the vehicle for the
chassis); a part without one gets a hull round its mesh. Materials of your own use your colours and
images only (a plain colour where there's no image). It drives with the Marauder combug's settings
(its handling table entry, under your name), which the table editor can change; no lights, doors
or turrets.

What the kind decides: what each part does (a wheel steers, a door opens on its hinge, a turret
turns), the handling and the seats' animations, a walker's skeleton and animations (moving a
walker's legs elsewhere than the kind's skeleton has them doesn't move the skeleton). Edit the
table entry in the editor's table editor to change handling, hit points or the display name.

## Getting in, driving, getting out (the player's vehicle animations)

The "RFA Vehicle Animation" panel animates the player with any of the game's vehicles:

1. **Animate Getting In**, pick the vehicle (search by its vehicles.xtbl name, such as RF_LAV_1). You get
   the vehicle's kit, the places people stand and sit as arrows (green: the way the person faces;
   moved and turned freely, and exported where you leave them), the player character, and three clips:
   **Get In** (from where they stand to the seat), **Drive** (sitting at the wheel; it loops) and
   **Get Out** (from the seat to where they get out). Each starts from the vehicle's own animation, or,
   for the many vehicles that have none (they use the "Dummy" interaction: the player pops in), from
   the LAV's set that the game never plays (pc_enter_lav, pc_drive_lav, pc_exit_lav).
2. The **Get In / Drive / Get Out** buttons switch clips: the player stands at the clip's first place,
   and the timeline shows the clip's frames and its triggers. Animate as usual: bones, and the
   armature object itself for moving through space (its keys are the root track, from the first place).
   **Start Clip From...** starts the clip shown again from any of the game's animations (search
   "enter", "exit", "exo", "lddr"...), with that animation's triggers.
3. **Add Trigger Here** marks the frame for the game: a door opening (door start, door end) and closing,
   the camera handing over, footsteps. Triggers are timeline markers; the clip keeps its own.
4. **End at the Next Place** keys the player so the clip's last frame is at the seat (or where they get
   out); **Move the Next Place to the End** moves the seat there instead.
5. **Export Vehicle Animations to Game**: the three animations (mods\animations\rfa_<vehicle>_enter...),
   the vehicle's own anim set (overrides\build\pc\cache\rfa_<vehicle>.xtbl), and its entries in the
   tables the game reads while it starts: anim_set_filenames.xtbl, anim_files.xtbl (triggers, flags),
   vehicle_interactions.xtbl (RFA_<vehicle>: the places and steps) and vehicles.xtbl (the vehicle names
   it). From the game's next start. **Remove** puts the vehicle back as it was. A clip with door
   triggers opens the vehicle's door (or hatch) nearest where the player gets in or out, unless
   the vehicle's steps already name one.

The start-up tables go to mods\tables\, which RFA-Mod hands the game (rfa_qol's tables part,
`[Mods] tables=1`); where the Guerrilla world's tools have written that table (mods\guerrilla_world\),
the entries go into that copy instead, as guerrilla_world hands it over. Rebuilding the Guerrilla
world's vehicles writes those tables anew: export again afterwards. The game holds 140 anim sets and
fills 125 itself (guerrilla_vehicles makes room for 255): each animated vehicle takes one.

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

## Good to know

- The list of the game's animations is made the first time it's opened (a few seconds: every
  archive's asset lists are read) and kept in a cache file in the system's temporary folder, read
  again only for archives that changed. **Read the Game's Files Again** makes it anew.
- Materials look as the game's shaders colour them (decompiled from `ir_carwalker`, `ir_cardefault`,
  `ir_carglass`: the colour map x `Diffuse_Color`, brown dust laid over where the mask's green x
  `Dust_Amount` says, shine from the specular map x `Specular_Color` (none under dust),
  `Specular_Power` as roughness, the DirectX normal map's green flipped, glow x `Glow_Color` x
  `Self_Illumination`, glass as see-through as it is dark). The game's own lighting is the only
  difference left. The constants are labelled nodes: changing them changes the exported vehicle's
  material. **Game Look for Materials** makes the selected objects' materials again (for things
  brought in by an earlier version). Normal maps you plug straight into a Normal Map node are taken
  as Blender's (OpenGL) and flipped for the game on export.
- Textures are the game's own pixels (DXT), saved as DDS files in the temporary folder and packed
  into the .blend. A material is named after its colour texture; importing again reuses a material
  of that name as it is, so changes to it are kept. Materials the game draws blended or cut out
  take the colour texture's alpha.
- A few of the game's animations are stored the way Red Faction: Guerrilla stored them (the heavy
  walker's `plyr_wklr_dr_enter`): they're read either way and exported the game's own way.
- `mods\animations` names are the file names: up to 53 characters, as the game's library takes them.

## Files

- `rfa_formats.py`: the game's files in plain Python (packfiles, asset lists, skeletons, animations,
  character meshes and their materials, texture files), ported from the editor's readers and writers in `src/editor/RfaFormats`. It runs without
  Blender: `python -c "import rfa_formats"`.
- `rfa_vehicle.py`: vehicle models (`.ccar_pc` + `.gcar_pc`, version 30: parts, tags, lights,
  meshes, materials, collision as Havok 2010 convex shapes), containers and asset lists; reads and
  writes every one of the game's 17 vehicles back byte for byte. Runs without Blender.
- `rfa_textures.py`: DXT1 / DXT5 encoding (numpy) and texture files (`.cpeg_pc` + `.gpeg_pc`).
- `blender_io.py`: the conversions to and from Blender.
- `blender_vehicle.py`: the vehicle kits and their export.
- `blender_vehicle_anim.py`: the player's vehicle animations, their places and the tables that play them.
- `__init__.py`: the operators, panel and preferences.
