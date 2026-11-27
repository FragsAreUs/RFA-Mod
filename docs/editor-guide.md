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

The window:
- **The title bar**: the menus of the tab you are working in, where its file is (in the middle),
  and the **Overrides folder** button.
- **What you open** (the middle, as tabs): the preview of the file you pick in the explorer, and
  every table and zone you open.
- **Explorer** (left): every archive in the game, and search.
- **Scene** (the outliner) and **Inspector** (right): what is in the tab you are working in (a zone's objects,
  a table's entries), and the details of what you picked (an object's properties, a file's details
  and buttons).
- **Assets** (bottom): what you can add to the zone you are working in, to pick and drag into
  its view (see [The Assets panel](#the-assets-panel)).
- **Log** (bottom, beside Assets): everything the editor did, such as each save. It's also written to
  `logs\editor.log` (the last session's is kept as `editor.prev.log`), with what went wrong if the
  editor crashes, and, if the window stops answering for 10 seconds or more, what the editor was
  doing then (and when it answered again). Send that file with a report of a crash or a hang.
- **The status line** (the foot): what the editor last did; the tab's own status (whose zone it is
  and where saving puts it); which copy of RFA-Mod this editor belongs to (its folder, shortened:
  hover for the whole path) and where the game is; how much memory the editor uses. Start the game
  with `RFAModLauncher.exe` from that same folder to see what you saved.

On Windows 11 the title bar and the status line show your desktop's colours faintly through them.

You can put the panels where you like:
- Drag a panel by its title, or a tab by its tab. Drop it on one of the arrows that appear to dock
  it to that side, or on the middle one to make it a tab of the panel under it. Drop it anywhere
  else and it floats as a window of its own.
- The pin in a panel's title tucks it away at the side of the window until you point at it.
- Drag a zone's tab to a side of the other tabs to see two zones next to each other. The panels
  show the tab you last clicked in.
- Where you leave the panels is remembered (in `editor\layout\main.xml`). Delete that file while
  the editor is closed to put them back where they started.

## Browsing the game's files

- The explorer lists every archive in the game (main game and DLC), the files in each, and the
  files inside each `.str2_pc` container, including compressed archives. Each file has an icon and
  colour for its kind (tables, textures, models, zones, scripts, sounds, ...).
- New archives your mods add in `overrides\` are listed too, marked "new, in overrides", such as
  the Guerrilla world's `rfg_l0.vpp_pc`, `rfg_l1.vpp_pc` and `zonescript_rfg.vpp_pc` once
  `GuerrillaBuilder.exe` has built them. You can open their zones in the level editor; what you save
  to them goes to `edits\`, so the archives stay as they were built.
- **New files** (in each folder that has some, such as `build\pc\cache\guerrilla\people`): the files in `overrides\`
  that aren't in any archive. The Guerrilla builder puts most of the port there, a folder for each of
  its parts under `build\pc\cache\guerrilla\`: the characters in `people` (`mason.rig_pc`,
  `EDF_Elite.str2_pc`, ...), vehicles in `vehicles` (`*.ccar_pc.str2_pc`), weapons in `weapons`
  (`rfg_enforcer.str2_pc`), sounds in `sounds`, the HUD's documents and scripts in `hud`, the world's
  archives in `world`. The game gets one when it looks
  for that name and none of its archives has it; a new file whose name an archive has is marked
  "unused", as the game always takes the archive's.
- **mods**: each mod's data folder (`mods\guerrilla_world`, `mods\guerrilla_gear\animations`,
  ...): the tables, animations, pictures and scripts the Guerrilla mods read and hand to the game.
- Containers, models, rigs and animations among them open and preview like the game's own. If
  you save a change to one of these files (a table, a texture in a container), it is changed in
  place, as the game reads it there, and gets the `changed` tag; its copy from before is kept, so
  you can restore it. Building the Guerrilla world again writes the file anew, without your change.
- Search by file name across all of them; the search says where each is ("new file", "the mod's
  data"). The files inside containers become searchable a few seconds after the editor starts. If
  the Guerrilla builder adds or removes files while the editor is open, the explorer and the search
  are made again when you come back to the editor's window.
- Pick a file to preview it: text files (`.xtbl`, `.lua`, ...) as text; textures, models, sound
  banks, conversations, effects, strings, menus and HUD screens in their own views (below); anything else as a hex dump.
- Pick one of the Guerrilla world's sound banks (a `.bnk_pc` the Guerrilla builder wrote) to hear
  it: its events are listed, and **Play** and **Stop** post them as the game would. Drag the sound
  around you on the pad to hear its distance and side, set its game parameters (an engine's RPM,
  ...) and switches, and see the voices playing. **Guerrilla's original** plays the sound it was made from, out of your
  copy of Red Faction: Guerrilla, to compare. Armageddon's own banks list their events but don't
  play yet.
- Pick a conversation (a `.ctdg_pc` in `sounds.vpp_pc`, such as `e3_new_start.ctdg_pc`) to see
  who speaks and what they say, in order:
  - **Who speaks** lists the people in it (the game's voice personas: `darius`, `kara`, ...), how
    many lines each has, and their voice bank in `voices.vpp_pc`.
  - **Lines** lists each line with its speaker, the line's name and folder (from the game's
    `audio_personas.xtbl` and `audio_line_tags.xtbl`), and the pause before it. A negative pause,
    such as "250 ms early", starts the line before the one before it ends, so the two overlap.
  - Pick a line to see what plays it. **Play line** (or double-click it, or Enter) plays it.
    **Play conversation** plays every line in order from the picked one, with the file's pauses
    and overlaps, as the game would. Each speaker stands at their own place in front of you, so
    you can tell them apart. **Stop** stops it.
  - If the game won't play a conversation, the top line says why: it has no lines or more than
    30, or one of its lines isn't in its speaker's voice bank (the game checks every line before
    it starts one).
- Pick an interface document (a `.vint_doc` in `interface.vpp_pc`, inside its container: `ui_hud`
  is the in-game HUD, `ui_pause_menu` the pause menu) to see the screen it draws:
  - **The screen** is drawn from the document's elements as the game lays them out: pictures
    cut from the game's sheets, text in the game's English words, sub-documents (`vdo_*`) drawn
    inside the elements that show them. Click a picture or a text to pick it.
  - **Elements** lists its tree. Pick one to see its properties, with a dashed box round it on the
    screen. Untick an element's box to leave it out of the picture.
  - **Add** puts a new element inside the picked one (or at the top when none is picked): a group,
    picture, text, clip, sub-document, circle picture or map, with the values its kind needs. **Copy**
    copies the picked element and everything in it, right after it, as `<name>_copy`. **Delete**
    deletes it; if the script names it, you're asked first, as the script may fail without it. For example, untick
    `low_health` in `ui_hud`: the HUD script only fades that red overlay in when you're hurt, but
    the document has it showing.
  - **Properties** shows the picked element's name and values: anchor (its place), offset, scale,
    rotation, tint, alpha, image, text and so on. Hover over a name for what it does. Type a new
    value and press Enter: the screen is drawn again at once. **×** removes a value (the game then
    uses its default), and the list at the bottom adds one the element hasn't got. Renaming an
    element doesn't change the script, which finds elements by name. **Undo changes** forgets what
    you haven't saved.
  - **Save** writes your changes into the document where the game reads it. The document's
    container is rebuilt in `overrides\`, and the asset list that holds the containers' sizes is
    updated with it. Changes to a sub-document's elements (a `vdo_*` shown inside) are saved into
    that sub-document. **Restore original** goes back to the game's own copy of the document.
  - **Script** shows the document's Lua script, coloured. Every element name the script uses is
    underlined: click one to pick that element. Pick an element and the lines that name it are
    marked, and listed under its properties ("line 120", ...) to jump to.
  - **Animations** (the tab beside the script) lists the document's animations. Pick one and press
    **Play** to see it move over the screen, looping as it loops in the game; drag the slider to see
    any moment, and **Stop** goes back to the document's start. Its tweens (each moves one property
    of one element: alpha, anchor, scale, rotation, tint, ...) are listed with their start, length,
    values and easing. Pick one to change any of these, or delete it. **Add tween** adds one that
    fades the picked element out; **New animation** and **Delete animation** add and remove whole
    animations. Animation names in the script are underlined too: click one to show it. Renaming an
    element moves its tweens with it; deleting one that tweens move asks first. The scripts play
    animations when the game needs them, so what you see is each one alone.
  - **Show hidden** also draws the elements that start hidden, faded; the scripts show them as
    they need them. **Outlines** draws a thin box round every picture and text. **Game fonts**
    draws text with the game's own fonts, letter by letter from their font sheets; untick it for a
    plain stand-in font.
  - The resolution list lays it out for the game's other resolution (960x720) where the document
    has values for it.
  - It updates live. While a document shows, saving its document, its container or its script
    in your mod folder (`overrides\`, `mods\`) draws it again from the new files. So you can keep
    the editor open beside your text editor while you work on a HUD script or document.
  - **Reload in game** tells the running game (started through `RFAModLauncher.exe`) to load the
    document again from your files, so you see your saved changes in the game too. Save first: the
    game sees only what is saved.
  - What you see is close to the game, not exact. Masks and multiply-blended elements aren't drawn, glowing (additive) ones are drawn
    plainly, and anything a script moves, fills in or animates is shown where the document
    starts it.
- Pick a strings file (a `.le_strings` in `misc.vpp_pc`, such as `menu_us.le_strings`) to read the
  game's text:
  - Every string is listed by its tag (`MENU_ACCELERATE`, ...), the name the game's tables, menus and
    scripts use for it, with the text beside it.
  - A file that comes in several languages (`menu_us`, `menu_de`, ... `menu_ru`) shows all of them
    side by side, one column each, whichever of them you picked.
  - Type in the search box to keep only the strings whose tag or text, in any language, has it.
  - Pick a string to read it whole, in every language, below.
  - Accented, Polish and Russian letters show as letters. The game stores them under its fonts'
    own codes, which the editor turns back into letters; a few rare symbols, and Japanese, are
    shown as stored.
- Pick an effect (a `.cefct_pc`, or the `.gefct_pc` beside it, in `effects.vpp_pc` and the
  levels' containers) to see it play and what it's made of:
  - It plays at the top, over a grid whose lines are a metre apart. Drag to turn around it and use
    the wheel to come closer. **Pause** (or Space) stops it, **Restart** starts it again from
    nothing, and the speed list slows it down to a quarter. **Loop** plays it again and again, as
    an effect that keeps going (a fire); turn it off to see a one-off effect (an explosion) once,
    its particles left to die.
  - The top line says how long it lasts, how big it is, what it holds, and which entries of
    `effects.xtbl` use it, with their sounds. Those entries' names are what weapons, objects and
    scripts call the effect.
  - **Textures** shows each texture it uses as a picture. Hover over one to see which parts use it.
  - **Parts** lists its emitters (what particles they make, from what shape, how many, how long
    they live), lights (point or spot, and how far they reach), coronas (glows), meshes and filters
    (forces that push its particles). Pick one to see more: an emitter's colours at birth and at
    the end, how it's drawn, its shader and textures; which emitters a filter pushes. Untick a
    part's **Shown** box to play the effect without it.
  - The player draws particles, ribbons and trails (a strip through the particles behind a flying
    spark), coronas (glows, drawn on top), and the effect's meshes: light rays, rings, falling
    debris and shell casings.
  - Its lights light the ground under it and its solid meshes; a small ring in the light's colour
    marks where each light is. A spotlight shows its cone on the ground.
  - Its filters push its particles while it plays: a magnet pulls them in, a vortex swirls them,
    turbulence shakes them, and an orientor lays them flat (the rings a bullet makes in water).
  - What plays is close to the game, not exact: the editor works it out from the file's settings
    itself, and a mesh can show through one in front of it. Wind filters aren't played yet.
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
(`.rfgzone_pc`, or a mission's `.layer_pc`), or press **Open in level editor**. It opens as a tab.

### The zone's tab

- **The menus** (File, Edit, View, Tools, Game, Help), in the window's title bar while the zone is
  the tab you work in, hold every command, with its key beside it.
- **The toolbar** over the view, left to right: the tools (Select, Move, Rotate, Sculpt); Snap, the
  **snap step** and Magnets; **+**, which adds an object; what is drawn (Models, Textures, the
  Direct3D view, Game light); look at the picked object and show the whole zone; the running game's
  three (show in game, follow the game, props as in the game). On its right: **undo**, **redo**
  and **Save**.
- In the view's bottom right corner, brought up to date every frame: **fps** and **frame** (how often
  the window draws itself, at most as often as your screen shows a new picture), and with the
  Direct3D view on, **cpu** and **gpu** (what the last frame of the level took the processor and the
  graphics card, by the card's own clock) and **drawn** (how many frames of the level the card drew
  in the last second: the level is drawn again only when something changes, so it is 0 while
  nothing does).
- **The status line** at the window's foot: whose zone it is and where saving puts it (hover over it
  for all of it).
- **Scene** (the panel on the right): the zone as a tree, with an eye on every row (see below).
- **Assets** (the panel at the bottom): everything you can add, in folders, with a preview; drag
  one into the view to put it there (see below).
- Hover over any button to see what it does. **Help > Mouse and keys** lists every mouse button
  and key of the view.

### What you see

- Every building and prop with its model and textures where the zone puts it, with all its parts
  in place.
- The items and weapons lying about (ammo, salvage, audio logs, guns: the **Pickups** kind).
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
  textures off it is drawn plain. Roads the game lays into the ground (as in `world_10_07`, and
  all over the Guerrilla world) are drawn with their road texture; in the game their edges blend
  into the ground's layers, which the editor doesn't show yet.
- The zone's **mission layers**: what each mission adds (its enemies at their squad spawns,
  triggers, force fields, turrets, vehicles), with a row of their own in the Scene panel per mission
  (`Mission_01A` for `world_08_02`). You can pick their objects and read their properties;
  **Open** opens the layer in its own tab to change them. A layer opened on its own is drawn
  with its zone's objects in the same way.
- The **zones around** the open one, where the game has any (the open world's zones are mostly
  apart; `world_08_02` has three around it): their level geometry, ground (coloured with their
  colour maps) and objects' models, so the zone's edges meet what is next to them. You can't pick them; open a zone
  to work on it. **Zones around** hides them.

### The Scene panel

The **Scene** panel shows the zone as a tree:
- **the zone**: its objects by kind (models, lights, spawns, regions, ...), each kind by class, and
  under each class its objects;
- **its mission layers**, each with its objects by class;
- **the level's parts**: the zones around, the terrain, foliage, decals, level geometry, the links
  and the grid.

Every row has an **eye** at its right end: click it to hide what the row stands for, and again to
show it. That hides a whole kind (in the mission layers too), a whole class, one object, a layer
or a part of the level. What is hidden is drawn dim in the list. Hiding is only how the view draws:
the zone isn't changed, and you can't click what is hidden. Click a row to pick its object, or pick
an object in the view to find its row. **+** adds an object (as in the toolbar), and the **Find**
box narrows the tree to the objects whose name or class has what you type.

Terrain and foliage can be hidden but not picked or changed yet, and clicking them doesn't pick
objects behind them. They appear once the editor has read every container. The axis widget in the
view's corner shows which way X, Y and Z point.

### Guerrilla's gameplay

In a zone of the Guerrilla world (`rfg_CC_RR`), the editor also shows the places the Guerrilla
port's gameplay uses, which the Guerrilla builder writes for the mods rather than into the zones:
the campaign's markers (activity spawns, raid, delivery and BFTP nodes, mission starts, house
arrests, demolitions masters, upgrade nodes, safehouses, ...), kill regions (pits and minefields),
restricted areas (the EDF's bases), Marauder ambush regions, the buildings that change a district,
story people, the squads' and walkers' action nodes, kiosks, respawn spots, safehouse parking and
stations, and convoy starts.
- In the view: places are coloured posts as tall as a person, regions see-through boxes, and
  restricted areas see-through walls around the base.
- In the Scene panel: the group **Guerrilla's gameplay**, by kind, each kind with an eye. Pick
  one to see where it is and all it says in the Inspector, with a box around it in the view.
- They are read only for now: the Guerrilla builder writes those files anew on every build.

### The Assets panel

The **Assets** panel (at the bottom, beside the Log) holds everything you can add to the zone you
are working in, in folders on its left:
- **This file**: one of each kind of object the zone has (by class and name), by kind. Adding one
  adds a copy, with what is joined to it.
- **Chunks**: every building and prop in the game's files (the Guerrilla world's too, once it is
  built), whatever level it comes from: **by type** (props, structures, ...; then by category),
  **by setting** (common, city, EDF, ...) and **by pack** (the archive that has it).
- **Level geometry**: every stitch piece (rock walls, cliffs, ledges, roads, ...), by category,
  setting and pack.
- **New objects**: every class in the game's schema that can be made from it alone (lights,
  spawns, triggers, effects, items, navpoints, ...), by kind. Under **From the game's levels** are
  the classes only Volition's tools could make (logic handlers, roads, splines, animated rigs):
  every different one the game's levels have, added as a copy with the rest of its set.
- **Everything**, at the top, lists all of them at once.

Pick a folder to list what is in it and in the folders under it. The **Find** box narrows the list
to what has the words you type in its name, category or setting ("crate", "containers"). Pick
something to see it on the right: its model in 3D (drag to turn it, right-drag to slide it, the
wheel to come closer), or its kind's icon if it has no model. Then:
- **double-click** it (or press **Enter**) to add it where the middle of the view points, or
- **drag** it into the 3D view and let go where you want it: it goes on whatever is under the
  mouse there (the ground, a building, level geometry), or 15 m ahead if nothing is.

Adding works as the Add list's does (see [Changing objects](#changing-objects)): a chunk is copied
from a level that places it, or made as a new mover, and its model comes with it when you save;
the new object is picked, and each addition is one step to undo. Chunks and level geometry show
once the editor has read the game's files, a few seconds after it starts.

### Links between objects

Pick an object and its links are drawn as lines: yellow to the objects it names, blue from those
naming it, with an arrow pointing at the one named. They're also listed in the inspector's
**Links** section; click one to pick it and fly to it. Links tie a level's logic together:
navpoints' paths, roads, a patrol's first navpoint, a logic handler's triggers and the objects it
acts on, the objects an animated rig moves, lights attached to buildings. The **Links** row's eye in the Scene panel draws
every link of the zone and its mission layers at once, each in the colour of the object naming
the other. Lines keep the same width on the screen near and far. You can't edit links yet.

### Looking like the game

- **Direct3D view** (the monitor in the toolbar, or View > Direct3D view; on when a
  zone opens) draws the level on the graphics card. Turn it off to draw with WPF instead; if your graphics card can't
  draw it, the log says why and WPF draws the level. It shows the same things from the same camera;
  picking, the gizmo and everything else work the same either way.
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

### What the game doesn't draw

The light-bulb button in the toolbar (on to start with) shows, in the editor only, what the game
keeps invisible: each **spot light's cone** (yellow: as far as it reaches, as wide as its hotspot and
falloff), each **spectator camera's view** (blue: the MP markers of type *Spectator camera* and
*Spectator intro camera*), the **invisible walls** (red: from each wall point to the next, 40 m
high, and 20 m down where the wall extends downward) and the **invisible collision boxes**
(orange). They follow the objects as you move them, from this zone's files and those shown with it.
Nothing of them is saved.

### With an Xbox controller

Any XInput controller (Xbox 360, One or Series; plugged in or wireless) works while the level
editor's tab shows and its window is in front. The log lists the controls when one connects. A
reticle in the middle of the view shows what the buttons act on, with its name below: white on
nothing, blue on an object, grey on another file's object (which can't be moved from this one),
orange and closed in while holding one.

- **Left stick** moves, **right stick** looks around, **LB** / **RB** fly down and up, **LT** goes
  faster (the harder you press), and the **right stick pressed** zooms in while held.
- **A** grabs what the reticle is on, in one press, and **A** again lets go of it (one step to
  undo). While held it stays where it is from the view as you move and look (Snap applies).
- **Hold RT** while holding an object to work on it instead of moving the view: the **left stick**
  pushes it away or pulls it nearer, and the **right stick** turns it (left / right about its
  up axis, up / down about the view's side to side; in 15° steps with Snap on).
- **B** while holding puts it back where you grabbed it. **D-pad left / right** turn the held object
  (or the one the reticle is on) 15°.
- **X** on an object duplicates it and puts the copy in your hand. **X** on nothing opens the
  **Add** list: the **D-pad** or **left stick** scroll it, **LB** / **RB** a page at a time, **A** goes
  into a folder of chunks or adds the choice in your hand, **B** goes back up (or closes it). (Finding a chunk by name still needs the keyboard.)
- **Y** deletes the object the reticle is on (or the one held).
- **B** (holding nothing) switches to the terrain sculptor, and back.
- **D-pad up** turns Snap on and off, the **left stick pressed** turns Magnets on and off, **D-pad
  down** looks at the object the reticle is on.
- **View** (Back) undoes, **Menu** (Start) saves.
- In the terrain sculptor: **RT** sculpts where the middle of the view is (as hard as you press),
  **LT** does the opposite (lowers instead of raising), **A** changes the brush (raise, lower, smooth,
  flatten), **D-pad left / right** its size, **up / down** its strength, the **left stick pressed**
  goes faster, **B** goes back to moving objects.

### Sculpting the ground

**Sculpt** (T, the brush in the toolbar, or Tools > Sculpt the ground) shapes the zone's ground. An
orange ring shows the brush on the ground, and a small **Sculpt** box in the view's top left corner
has the brush's choices while you sculpt.

- Hold the left mouse button and move it to sculpt. **Raise** and **Lower** push the ground up and
  down (Shift does the opposite), **Smooth** evens it out, and **Flatten** levels it at the height
  where the stroke began. Each stroke is one step to undo.
- **[** and **]**, or the Sculpt box's Radius, change the brush's size; Strength is how fast it
  works, and Brush is what a stroke does.
- Strokes carry on across zones: the sculptor holds the ground of the zones around this one too,
  so a stroke near an edge shapes both sides of it the same (Smooth evens out over the edge), and
  you can sculpt the ground all round the zone.
- Saving writes, for every zone you changed, its height field (`<zone>.cterrain_pc`, which the
  game's collision and physics use), its **far-away ground** (the low-detail meshes the game draws
  in the distance, kept in the terrain file and `<zone>.gterrain_pc`: each far vertex moves by as
  much as the ground under it did, so it keeps its shape and how far it lies under the ground) and
  the sub-zones you changed (`<zone>_N.ctmesh_pc` and its `.gtmesh_pc`, the ground you see), in
  every copy of them. No file changes size.
- Ground edits are always overrides, never changes to the game's files or the Guerrilla world's
  archives: the containers that hold them are rebuilt into `overrides\` (the Guerrilla world's into
  `edits\`), and a file sculpted back to its own shape stops being overridden. **Restore** puts this
  zone's ground back too, with its objects (open a zone around it and Restore there to put its
  ground back).
- What stands on the ground moves with it:
  - **Objects**: those on the ground (whose bottom is from 3 m in it to 2 m over it, so not a lamp
    on a pole) keep their height over it, with what is attached to them (a building's pieces). This
    zone's move as you sculpt, in the same undo step; its other files (its persistent zone, its
    mission layers) and the zones around's are moved when you save, by as much as the ground moved
    since the last save.
  - **Roads**: their decal meshes in the sub-zones move by as much as the ground under them, when
    you save.
  - **Foliage**: the game grows it in the 1 m cubes of ground its terrain file marks, setting each
    piece on the surface in its cube; the cubes near the ground move up or down with it when you
    save. They stay in their 4 m cell, or move to the cell above or below where the zone has one; a
    cube with nowhere to go grows nothing (the log says how many).
  - Level geometry standing on the ground moves with it as objects do (it is saved in the stitch
    list). Not moved: the zones around's level geometry, and the ground pieces (skirts) round it.
- Where a sub-zone's mesh is coarser than the height field, small strokes show in the collision more
  than on screen.

### Changing objects

- **Pick** an object by clicking it (or with a controller: see above), or in the Scene panel (which lists objects by kind and class, with a Find
  box). **Q** is just picking.
- **Move** (W): drag the arrows along X (red), Y (green) or Z (blue). **Rotate** (E): drag the rings
  to turn it. **Snap** moves in steps of the size you choose and turns in 15° steps. What is
  attached to the picked object (a tank on its base, a building's pieces and lights) moves and
  turns with it.
- **Magnets** (M, the plug in the toolbar, or Tools > Magnets; on to start with): a moved object
  jumps to touch the models near it, corner to corner, edge to edge or face to face, so chunks line
  up without gaps or overlaps. The points are the corners, the middles of the edges and the
  middles of the faces of each model's box, turned with it; they hold within a reach that grows
  with the object's size (a pink diamond marks where). Dragging an arrow, they only move it along
  the arrow. Magnets come before Snap: where nothing is near, Snap applies.
- The inspector's **Transform** shows the picked object's position (X, Y, Z in metres) and
  rotation (in degrees). Type a number, or drag a field's coloured letter left and right (Shift:
  faster; Snap applies). Moving or turning an object moves its bounding box (which the game uses)
  with it.
- Below it are the object's **properties**, as the game's own zone schema describes them (the file
  Volition's level editor used, which the game still has): each with its name and an editor that
  fits it. Hover over a name for the game's own name, what it holds, its default and its choices.
  - A list of **choices**: the schema's own, and the entries of the table it names (a squad
    spawn's squads come from `squads.xtbl`, an effect's from `effects.xtbl`; tables you changed
    are read as you changed them). Type to find one; only a choice is taken.
  - A **tick box per flag** (a light's player, characters, vehicles, environment), and one for
    on/off values.
  - **Numbers**, whole numbers and **handles** of other objects (8 hex digits, or `none`).
  - **X, Y, Z** for vectors, the lowest and highest corners for boxes (a trigger region's), and a
    **colour** swatch with its red, green, blue and alpha (above 1 is brighter than white).
  - **Text**, up to 255 characters (longer text than before makes the object larger).
  - Properties the game's files leave out at their default (most lights have no light flags) show
    as *not set*: tick a flag, or press **Set**, to add one.
  - As in Volition's editor, some properties are hidden and some are read-only (with a lock): its
    tools set them, such as a building's model or a light's curve files. Tick **Show hidden and
    read-only properties** to see and change them anyway.
  - Properties Volition's tools added that the schema doesn't describe are shown at the end, with
    their names from the game, and every one can be changed: a navpoint's data as its type, outer
    radius and speed limit; links and a rig's children as handles in hex (add or take out as many
    as you like); numbers as numbers; any other packed data (a logic handler's script, a road's
    points) as its bytes in hex, with the text in it on hover.
- **Add** (+ in the toolbar, or Edit > Add) lists one of each kind of object the file has (a crate, a light, a
  squad spawn, a building piece, ...), with a Find box. Pick one to add a copy where the middle of
  the view points. Below them are **new objects** of every class in the game's schema that can be
  made from it alone (lights, spawns, triggers, effects, items, ...): a new one gets every
  property at the schema's default, as Volition's tools would have written it, where the middle
  of the view points. A new navpoint gets its packed data from the schema's defaults (a path
  node); link it by adding handles to its obj_links. Below them, **From the game's levels** lists
  every different logic handler, road, spline and animated rig in the game's levels: picking one
  adds a copy of it with the objects it hangs from and those hanging from it (a trigger with its
  handler, a monolith's rigs), its handles new and the handles they name of each other changed to
  the copies'. Buildings are added from the chunk browser. A few classes, marked
  *unused*, are in none of the game's levels, so the game may do nothing with them. The
  [Assets panel](#the-assets-panel) offers the same things in folders, with a preview, to drag
  into the view.
- **Any chunk from the game**: the Add list's **Browse the game's chunks** section sorts every
  building and prop in the game's files (the Guerrilla world's too, once it is built), whatever level
  it comes from, four ways:
  - **By type**: props, structures, terrain pieces, physics pieces, ...
  - **By category**: containers, lights, trash and debris, industrial, furniture, signs, walls,
    highways, tunnels, ...
  - **By setting**: common, city, surface, EDF, Marauder, ice, colonist, multiplayer, ...
  - **By pack**: the archive that has it (and then the part of that archive), so you can take
    everything one level uses.

  These come from the chunks' names (`s_cm_cont_crate_med_a`: a prop, common, a container); "Other"
  is where a name doesn't say. Pick a way, then a group, to go into it; inside a group you can
  narrow it down by the other ways, and its chunks are listed below, under small headings. The bar
  above the list shows where you are: **◂ Back** (or Backspace in an empty Find box, or **B** on a
  controller) goes up a level, and the **✕** on a group takes it away. The Find box narrows what
  is shown, and at the top level typing at least two letters finds a chunk by name anywhere.
  Pick a chunk to add it where the middle of the view points:
- If one of the game's levels places it, that object is copied, with its properties and the pieces
    joined to it (a building with its loose parts, and the destruction links between them).
  - Otherwise a new mover is made for it: an `rfg_mover` if it breaks into pieces, else a
    `general_mover`, with the schema's defaults.
  - Its model and textures go into the zone's container when you save (into every level's container
    that has a copy of the zone; for a level that keeps its zone loose in its archive, into the
    archive's level container), with entries in the asset lists (the zone's own group, and the chunk's own group, `<name>.rfgchunkx`, which is how the game
    finds a model a zone names), unless the zone's world has the chunk already. The log says what was
    brought in. The first time, the editor looks through every level for where the chunk is placed,
    which takes a little while.
- **Level geometry** (the stitch pieces the zone's stitch list places: rock walls, cliffs, cave
  ceilings, ledges, roads, bridges) can be edited like objects: each piece is one, of the kind
  *Level geometry* (its eye in the Scene panel hides them; it lists them under `level_geometry`). Pick one
  and move it, turn it, type its numbers, carry it with the controller, snap it with magnets,
  duplicate or delete it. **Add** lists every stitch piece in the game too, as the type *Level
  geometry* in the chunk browser (by category, setting and pack, or by name): pick one to place it
  where the middle of the view points. Saving writes them into the zone's stitch list (every copy),
  with its decals as they were and a collision tree made again to reach every piece; pieces from
  elsewhere (another level's, or the Guerrilla world's: their shaders load everywhere) have their
  model and textures brought into the list's container, or for a level whose list is loose in its
  archive (the Ruin maps), into the zone's level container. They are never saved in the zone file
  itself. Sculpting moves the pieces standing on the ground with it.
- **Duplicate** (Ctrl+D) copies the picked object beside it (to the view's right, clear of it). A
  copy is a whole breakable object, as the game's own are: copying a loose piece (the tank of an
  explosive tanker) copies the part it hangs from too (the tanker's base), with everything
  attached. Its model is already in the zone, so the game can show it.
- **Delete** (Del) removes the picked object with the objects attached to it (a building's lights).
  It's refused, saying why, while something else names one of them (another object's links, such
  as navpoints linked in a path, or an object in a mission layer).
- **Effects follow objects into any level.** What an object shows when it breaks, is hit or burns,
  and the explosion it dies in, come from its level object (its `Props`: `level_objects.xtbl`, its
  effects in `effects.xtbl`, their visuals in `vfx.xtbl`, its explosion in `explosions.xtbl`, as the
  game reads them, the `guerrilla_world` mod's with Guerrilla's added). Most visuals load with a
  level, from files its archives hold, so a prop moved to another level (or a Guerrilla building in
  one of Armageddon's) broke without them. On saving, the effect files the objects added to the zone
  need and the level hasn't (each `.cefct_pc` and `.gefct_pc`, and a converted Guerrilla effect's
  textures) are brought into its containers with their models. Effects loaded at start-up, and
  Armageddon's effect textures, are everywhere already.
- **Delete all** (Edit > Delete all, which says how many of each) deletes every chunk (building and prop), every
  piece of level geometry, both, or every object of the picked one's class, with what is attached
  to them, in one step to undo. Those something else names are kept, as Delete keeps them, and the
  log says how many.
- **Ctrl+Z** / **Ctrl+Y** undo and redo, one step per drag, edit, addition or deletion.

### Saving

- **Save** writes the zone to `overrides\` (through a rebuilt container and updated asset lists,
  as for textures), with the zone's new size when it grew (objects added, longer text): the game
  sets memory aside by those sizes. Many zones are in the game twice; the editor saves every copy.
- **The Guerrilla world** (`rfg_CC_RR` zones, in `zonescript_rfg.vpp_pc` and `rfg_l1.vpp_pc` under
  `overrides\build\pc\cache\guerrilla\world\`) is ours, not the game's: its archives are new files in `overrides\`,
  so its changes go to `edits\` instead (beside `overrides\`, laid out the same way:
  `edits\build\pc\cache\guerrilla\world\rfg_l1.vpp_pc\1h_c0312.str2_pc`), which the loader uses as it does
  overrides. The archives stay as the Guerrilla builder made them. If you build the world again, a
  change made to a file the build changed is left out (the loader logs it): make it again in the
  editor. (This needs the loader from this version; changes an older editor made to those archives
  themselves are still put back by Restore, from `editor_originals\`.)
- **Restore** puts the game's own zone back in every copy. Other changes in the same containers
  (such as replaced textures) stay.
- **Adding objects is experimental.** It has been tried in the game: a new game with four copied
  tankers in `world_08_03` loaded and ran with all four in place. New objects made from the
  schema haven't been tried in the game yet. When you test added objects, start the game afresh
  and start a new game: a save made before they were added doesn't know them. If you saved a zone
  with an older version of the editor, save it again.

## Using it with the running game

Start the game with `RFAModLauncher.exe` (the console mod links it to the editor through the
`link\` folder):

These three are in the **Game** menu, and at the right end of the toolbar.

- **Show in game** (the controller) sends the game's camera to the picked object, in slew mode
  (the free camera), keeping the way it looks, and picks the object there, so the game's debug
  display names it.
- **Follow the game** (the link) moves the editor's view with the game's camera and field of
  view (the same vertical view as your FOV setting, however wide the editor's view is), and picks here what you pick there (Q in slew mode) when it's in the open zone or its
  mission layers. Handles and positions are the game's own, so the same object is at the same
  place in both.
- **Props as in the game** (the arrow down): clutter (debris, barrels, crates, wreck pieces)
  falls and settles by physics when the level starts, so the game has it somewhere other than
  where the zone file puts it. While this is on, the editor asks the game twice a second where
  each of the zone's clutter props is and draws it there. Only the drawing changes: the zone
  doesn't, and the picked object's box and the gizmo stay where the zone puts it. This has only
  been tried with a stand-in for the game, not yet with the game itself.

### Panels, and the game docked in the editor

Every panel (Explorer, Outliner, Inspector, Log, Game) can be dragged by its title to dock it
elsewhere or float it as a window of its own; documents are the tabs in the middle.

The **Game** tab holds the running game's window, docked inside the editor (the **Game** button,
top right, brings it forward):

- **Start the game** runs `RFAModLauncher.exe` (Steam has to be running). With **Dock
  automatically** on, the game is docked as soon as its window is up. **Dock** takes a game that
  is already running.
- The game has to be in **Windowed** mode (Options > Display; with the borderless mod that is
  borderless fullscreen). Its exclusive Fullscreen mode can't be docked.
- **Click the game to play.** The first click only gives it the mouse and keys. **Shift+F1**,
  **Back to the editor** or Alt+Tab gives them back. The game keeps running while you work in the
  editor (it doesn't pause), and doesn't see your keys or mouse.
- Drag the Game tab to a side of the documents to see a level and the game at once.
- The game fills the panel, whatever its size or shape: it draws at its own resolution and is
  stretched to the space it has.
- **Undock** or closing the editor gives the game its own window back, as it was.

### MCP servers

If you use Claude with this mod, Claude starts two helper programs (MCP servers):
`live_analysis_mcp.exe`, for the running game, and `editor\RfaEditorMcp.exe`, for the game's files.
The **MCP servers** button in the top bar lists them and closes them. They also close when you
close the editor, so none is left running. To use their tools again in a Claude session that was
open, reconnect them in that session's `/mcp`.

## Not yet

- Civilians' outfits, and characters and walkers in poses other than their rest pose.
- The ground's normal maps, and effects such as fire and smoke (and the sky's moving clouds).
- Shadows of the few omni lamps that cast them.
- Editing the level geometry, terrain or foliage.
- Making new roads, splines, logic handlers and animation rigs from nothing (add a copy of one of
  the game's instead).
- Deleting objects that other objects' links name, and editing links between objects.
- Changing a zone's layers or the zones around it from the zone's tab.
- Putting changed models back into the game, and textures of another size.

What the editor still lacks next to Volition's own tools is in [editor-roadmap.md](editor-roadmap.md).
How it works inside is in [how-it-works.md](how-it-works.md#the-editor).
