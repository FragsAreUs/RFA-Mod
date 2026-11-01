# The RFA-Mod editor

`editor\RfaEditor.exe` opens Red Faction: Armageddon's files: you can browse and extract them,
edit tables, replace textures, view models and animations, and edit levels. It only ever reads
the game folder. What you change is saved to `overrides\`, which the game uses instead of its own
files the next time you start it with `RFAModLauncher.exe`. Back to the [README](../README.md).

## Contents

- [Getting started](#getting-started)
- [Browsing the game's files](#browsing-the-games-files)
- [Editing tables](#editing-tables)
- [Replacing textures](#replacing-textures)
- [Viewing models and animations](#viewing-models-and-animations)
- [Editing levels](#editing-levels)
- [Using it with the running game](#using-it-with-the-running-game)
- [Not yet](#not-yet)

## Getting started

1. Install the 64-bit (x64) [.NET 10 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/10.0).
2. Start `editor\RfaEditor.exe`. It finds the game through `GameDir=` in `rfa_mods.ini`, like the
   launcher. If that's empty and RFA-Mod isn't in the game folder, choose the game folder (the
   one with `RedFactionArmageddon_DX11.exe`) when it asks; it fills `GameDir=` in for the
   launcher too. It works with any copy of the game, not only the publicbeta branch.

The window is laid out like other game editors (Nanoforge among them):
- **Explorer** (left): every archive in the game, and search.
- **Documents** (middle, as tabs): the preview of the file you pick in the explorer, and every
  table and level you open.
- **Outliner** and **Inspector** (right): what is in the open document (a level's objects, a
  table's entries), and the details of what you picked (an object's properties, a file's details
  and buttons).
- **Log** (bottom): everything the editor did, such as each save. It's also written to
  `logs\editor.log` (the last session's is kept as `editor.prev.log`), with what went wrong if
  the editor crashes.

## Browsing the game's files

- The explorer lists every archive in the game (main game and DLC), the files in each, and the
  files inside each `.str2_pc` container, including compressed archives. Each file has an icon and
  colour for its kind (tables, textures, models, zones, scripts, sounds, ...).
- Search by file name across all of them. The files inside containers become searchable a few
  seconds after the editor starts.
- Pick a file to preview it: text files (`.xtbl`, `.lua`, ...) as text, everything else as a hex
  dump.
- **Extract** saves a file, and **Extract all** every file of an archive or container, into a
  folder.
- **Copy to overrides** puts a copy at the right place in `overrides\`, where the game uses it
  instead of its own from the next start through the launcher. Edit that copy. Files you have
  changed (overridden) get an orange `changed` tag.

## Editing tables

Double-click an `.xtbl` table (or press **Open in table editor**). The editor builds its fields
from the table's own description, the one Volition's table editor used.

- The entries are in the outliner, in the developers' folders (`_Editor` categories), with a Find
  box.
- The picked entry's fields are in the tab, in the description's order, each with an editor for
  its type:
  - text boxes that refuse numbers outside the allowed range (a red border says why);
  - drop-downs for choices and check boxes for flags;
  - a pick list for references to other tables (a weapon's `Item_3d` lists every entry in
    `items_3d.xtbl`);
  - groups, lists with Add and Remove, and a switch for "one of" groups such as a weapon's shot
    type.
- Hover over a field's name for its help text, default and allowed values. Fields an entry
  doesn't set show the default the game uses; `×` removes a value to go back to the default.
- Above the outliner:
  - **New** adds an empty entry (every field the game's default) in the folder of the entry you
    picked. Tables with Volition's templates, such as `weapons.xtbl`, also offer those.
  - **Copy** (Ctrl+D) adds a copy with every field the same, right after the original, named
    `<name>_copy`. Rename it: the game finds entries by name.
  - **Delete** (Del) first tells you which tables name the entry (as the game sees them, with your
    overrides), for example 40 characters in `character.xtbl` carry `phased_plasma_rifle`.
    Scripts and levels aren't searched.
- **Save** writes the table to `overrides\`. Next time it opens your override, or the game's own
  copy if you have none. Only what you changed differs from the original file.

## Replacing textures

Pick any texture file (`.cpeg_pc`, `.gpeg_pc`, `.cvbm_pc`, `.gvbm_pc`), in an archive or inside a
container, and you see it as a picture. If the file holds several textures, a list picks one; a
checkerboard shows behind transparent parts.

- **Export PNG** or **Export DDS**. A DDS keeps the exact format and mip levels.
- **Replace** with a PNG, JPG, BMP, TIFF or DDS of the same size. The editor encodes it in the
  texture's own format (DXT1, DXT3, DXT5, 32-bit, 565) with fresh mip levels. A DDS that already
  has the right format and mip levels is used as it is.
- Most textures are inside `.str2_pc` containers, which the game streams in whole. For those the
  editor rebuilds the container into `overrides\` and updates the game's asset lists (`.asm_pc`)
  to match.
- **Restore original** goes back to the game's own textures for that file or container, and puts
  the asset lists back. Use it rather than deleting the files: a container and its asset list
  entries have to change back together.
- Textures keep their size for now: images of another size are refused.

## Viewing models and animations

Pick any model and you see it in 3D with its textures. Drag to turn it around, scroll to zoom.
Models are:
- `.csmesh_pc` static meshes;
- `.ccmesh_pc` characters;
- `.cchk_pc` destructible objects such as buildings and props;
- `.ccar_pc` vehicles;
- `.cstch_pc` stitch pieces (the rock, cave walls, roads and other fixed pieces levels are built
  from);
- `.cfmesh_pc` foliage (the debris the game scatters over the ground);
- or their `g` files.

What you can do:
- Characters show at their most detailed level, vehicles with every part in place (wheels, doors,
  a walker's legs).
- Destructible objects show whole, each part where it goes. **Broken pieces** shows the debris
  they break into instead. Many destructible object files hold several objects (sizes of a box,
  sections of a wall); a list above the view picks which one you see.
- **Skeleton** (characters) shows the character's skeleton inside it, the character drawn
  see-through, every bone in its rest pose down to the fingers and face. Pick a skeleton file
  (`.rig_pc`) on its own to see it alone, with its bones' and tags' names.
- **Animations**: pick an animation file (`.anim_pc`) and it plays on a character whose animation
  sets (in `character.xtbl`) name it. The list above the view picks another character, or a
  skeleton alone with the right number of bones. A character's model (`.ccmesh_pc`) has a list of
  the animations its sets name.
  - Below the list: play/pause (it loops), the speed, **Move along** (let the animation move the
    character, as a walk does; off, it stays on the spot) and a slider to go to any moment.
  - **Skeleton** shows the bones moving inside the see-through character.
  - Civilians show only their head and hands for now (the game puts them together from several
    meshes).
- Textures are found by name, first in the model's own container, then anywhere in the game (once
  the editor has finished reading the containers, a few seconds after it starts).
- **Export OBJ** saves the model as OBJ + MTL with its colour textures as PNG, which Blender and
  other 3D programs open.
- You can view and export models; putting changed models back into the game comes later.

## Editing levels

The game's world is a grid of zones; each holds the objects placed in that part of a level
(buildings and props, lights, spawn points, navigation points, triggers, ...). Double-click a zone
(`.rfgzone_pc`, or a mission's `.layer_pc`), or press **Open in level editor**.

### What you see

- Every building and prop with its model and textures where the zone puts it, with all its parts
  in place.
- The items and weapons lying about (ammo, salvage, audio logs, guns: the **Pickups** chip).
- The enemies and allies at squad spawns (each squad's leader), and exos and vehicles (LAVs,
  walkers, flyers, tanks) at their spawns.
- Turrets, with base, swivel and gun head fitted together.
- Every other object as a small cube coloured by kind (lights yellow, paths blue, spawns orange,
  regions red, ...), over a grid on the ground (every 10 m).
- The zone's **level geometry**: the rock, cave walls, ledges, roads, bridges and other fixed
  pieces, the **decals** laid on them (trash, blast marks, blood, signs), and the **foliage**
  (paper, cardboard, scrap, rubble and rocks the game scatters over the ground, placed exactly as
  the game does it).
- The **terrain**, the ground of outdoor zones, painted the way the game draws it: its four
  texture layers (sand, sand ripples, rubble, ...) mixed by the zone's blend map, in colours
  matched to the game's own far-away picture of the ground. Cave zones have no ground. With
  textures off it is drawn plain.
- The zone's **mission layers**: what each mission adds (its enemies at their squad spawns,
  triggers, force fields, turrets, vehicles), with a chip and an outliner group per mission
  (`Mission_01A` for `world_08_02`). You can pick their objects and read their properties;
  **Open** opens the layer in its own tab to change them. A layer opened on its own is drawn
  with its zone's objects in the same way.
- The **zones around** the open one, where the game has any (the open world's zones are mostly
  apart; `world_08_02` has three around it): their level geometry, ground (coloured with their
  colour maps) and objects' models, so the zone's edges meet what is next to them. You can't pick them; open a zone
  to work on it. **Zones around** hides them.

The **Show** chips above the view hide or show a whole kind at once (say, everything but the
buildings), and the grid. Level geometry, terrain and foliage can be hidden but not picked or
changed yet, and clicking them doesn't pick objects behind them. They appear once the editor has
read every container. The axis widget in the corner shows which way X, Y and Z point. Hover over a
toolbar icon to see what it does; **?** lists every mouse button and key.

### Links between objects

Pick an object and its links are drawn as lines: yellow to the objects it names, blue from those
naming it, with an arrow pointing at the one named. They're also listed in the inspector's
**Links** section; click one to pick it and fly to it. Links tie a level's logic together:
navpoints' paths, roads, a patrol's first navpoint, a logic handler's triggers and the objects it
acts on, the objects an animated rig moves, lights attached to buildings. The **Links** chip draws
every link of the zone and its mission layers at once, each in the colour of the object naming
the other. Lines keep the same width on the screen near and far. You can't edit links yet.

### Looking like the game

- **Direct3D view** (the monitor icon at the right of the toolbar) draws the level on the graphics
  card instead of WPF. It shows the same things from the same camera; picking, the gizmo and
  everything else work as before.
- **Game light** (the sun icon next to it) lights the level as the game does:
  - every lamp of the zone and the zones around, the ambient light, fog and exposure of the area
    the camera is in, and the game's tone curve;
  - surfaces with their bumps, shine and glow (light fixtures, screens, light strips); caves come
    out dark, lit by their lamps;
  - lamps that flicker and sway as in the game (fire, strobes, broken lights);
  - glass and water see-through, bullet holes, hair and leaves cut out;
  - the Legion's creatures, tentacles and pods drawn with the game's alien skin look;
  - the area's sky picture behind everything outdoors;
  - soft shadows from the lamps the game gives shadows (spotlights over the wreck, the tunnels'
    floodlights), as dark as each lamp says, fading out away from the camera.

### Moving around

- Hold the right mouse button to look around, and use **WASD** while holding it to fly (Q/E down
  and up, Shift faster).
- Middle-drag slides the view, and the wheel moves forwards and back.
- Alt+drag circles the picked object, and **F** looks at it.
- **Show the whole zone** frames the zone's objects, leaving out the few far-off ones some zones
  have.

### Changing objects

- **Pick** an object by clicking it, or in the outliner (which lists objects by class, with a Find
  box). **Q** is just picking.
- **Move** (W): drag the arrows along X (red), Y (green) or Z (blue). **Rotate** (E): drag the rings
  to turn it. **Snap** moves in steps of the size you choose and turns in 15° steps. What is
  attached to the picked object (a tank on its base, a building's pieces and lights) moves and
  turns with it.
- The inspector's **Transform** shows the picked object's position (X, Y, Z in metres) and
  rotation (in degrees). Type a number, or drag a field's coloured letter left and right (Shift:
  faster; Snap applies). Below it are the object's other properties with readable names (hover
  for the game's own name and type): numbers, colours, on/off flags, and text (up to 255
  characters; longer text than before makes the object larger). Moving or turning an object moves
  its bounding box (which the game uses) with it.
- **Add** (+ in the toolbar) lists one of each kind of object the file has (a crate, a light, a
  squad spawn, a building piece, ...), with a Find box. Pick one to add a copy where the middle of
  the view points.
- **Duplicate** (Ctrl+D) copies the picked object beside it (to the view's right, clear of it). A
  copy is a whole breakable object, as the game's own are: copying a loose piece (the tank of an
  explosive tanker) copies the part it hangs from too (the tanker's base), with everything
  attached. Its model is already in the zone, so the game can show it.
- **Delete** (Del) removes the picked object with the objects attached to it (a building's lights).
  It's refused, saying why, while something else names one of them (another object's links, such
  as navpoints linked in a path, or an object in a mission layer).
- **Ctrl+Z** / **Ctrl+Y** undo and redo, one step per drag, edit, addition or deletion.

### Saving

- **Save** writes the zone to `overrides\` (through a rebuilt container and updated asset lists,
  as for textures), with the zone's new size when it grew (objects added, longer text): the game
  sets memory aside by those sizes. Many zones are in the game twice; the editor saves every copy.
- **Restore** puts the game's own zone back in every copy. Other changes in the same containers
  (such as replaced textures) stay.
- **Adding objects is experimental.** It has been tried in the game: a new game with four copied
  tankers in `world_08_03` loaded and ran with all four in place. When you test added objects,
  start the game afresh and start a new game: a save made before they were added doesn't know
  them. If you saved a zone with an older version of the editor, save it again.

## Using it with the running game

Start the game with `RFAModLauncher.exe` (the console mod links it to the editor through the
`link\` folder):

- **Show in game** (the controller icon) sends the game's camera to the picked object, in slew mode
  (the free camera), keeping the way it looks, and picks the object there, so the game's debug
  display names it.
- **Follow the game** (the link icon) moves the editor's view with the game's camera and field of
  view (the same vertical view as your FOV setting, however wide the editor's view is), and picks here what you pick there (Q in slew mode) when it's in the open zone or its
  mission layers. Handles and positions are the game's own, so the same object is at the same
  place in both.
- **Props as in the game** (the arrow-down icon): clutter (debris, barrels, crates, wreck pieces)
  falls and settles by physics when the level starts, so the game has it somewhere other than
  where the zone file puts it. While this is on, the editor asks the game twice a second where
  each of the zone's clutter props is and draws it there. Only the drawing changes: the zone
  doesn't, and the picked object's box and the gizmo stay where the zone puts it. This has only
  been tried with a stand-in for the game, not yet with the game itself.

## Not yet

- Civilians' outfits, and characters and walkers in poses other than their rest pose.
- The ground's normal maps, and effects such as fire and smoke (and the sky's moving clouds).
- Shadows of the few omni lamps that cast them.
- Editing the level geometry, terrain or foliage.
- Adding objects whose models aren't in the zone.
- Deleting objects that other objects' links name, and editing links between objects.
- Changing a zone's layers or the zones around it from the zone's tab.
- Putting changed models back into the game, and textures of another size.

What the editor still lacks next to Volition's own tools is in [editor-roadmap.md](editor-roadmap.md).
How it works inside is in [how-it-works.md](how-it-works.md#the-editor).
