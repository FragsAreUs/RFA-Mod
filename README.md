# Red Faction: Armageddon mods

Mods for the Steam version of Red Faction: Armageddon (DX10/11 exe) that never modify the game's
install folder. Everything happens in the game's memory after it starts.

The mods are written for the Steam **publicbeta** branch (build 1932726, July 2017). It is an
internal "Release" build that still has the developer console, which makes modding much easier.
On any other build the mods that touch game code log that and leave the game alone.

## How it works

1. `bin\RFAModLauncher.exe` starts `RedFactionArmageddon_DX11.exe -packfiles` paused, holds it at
   its very first instruction and loads `bin\rfa_loader.dll` into it.
2. The loader loads every DLL in `bin\mods\` (alphabetically) and calls its `RFAMod_Init` export.
3. The game is released. Mods hook Windows functions the game imports, and send some of the
   game's own functions to plain C++ replacements in the mod DLLs.

Settings are in `bin\rfa_mods.ini`. Everything the launcher, loader and mods do is written to
`bin\logs\rfa_mods.log` (the previous run is kept as `rfa_mods.prev.log`).

## Project layout

```
src\launcher\            RFAModLauncher.exe: starts the game and injects the loader
src\loader\              rfa_loader.dll: loads mods, crash reports, overrides, Lua bridge
src\mods\<name>\         one folder per mod DLL (borderless, console, fov, video_settings)
src\common\              rfa_mod.h (logging, ini, hooking helpers), rfa_game.h (build check, Lua API)
src\overrides\           replacements for files inside the game's archives, same layout as the game
src\editor\RfaFormats\   C# library that reads the game's file formats (archives so far)
src\editor\RfaEditor\    the editor (C#, WPF desktop app); built into bin\editor\
config\rfa_mods.ini      the settings template (copied to bin\ on the first build)
config\console.ini       console settings template (copied to bin\ when missing)
docs\editor-roadmap.md   what the editor still lacks, by Volition's own tools (CLOE, ...)
docs\renderer-plan.md    the plan for drawing levels with Direct3D 11, to look like the game
tools\game-integrity.ps1 proves the game folder is untouched
tools\package.ps1        makes a clean copy to share (dist\), without your own settings
reference\build-*\       each Steam build's file list, for tools\game-integrity.ps1
bin\                     the built mod (what you install); build\ holds compiler leftovers
dist\                    made by tools\package.ps1: the mod and editor as others get them
```

## Installing

The contents of `bin\` (`RFAModLauncher.exe`, `rfa_loader.dll`, `rfa_mods.ini`, `console.ini`,
`mods\`, `overrides\`) are the whole mod; `bin\editor\` is the editor, which isn't needed to play
(it makes `bin\link\`, where it and the running game talk).
Pick one:

- **Keep it in a folder of its own** (game files stay 100% untouched): set `GameDir=` in
  `rfa_mods.ini` to the folder of your copy of the game, the one with
  `RedFactionArmageddon_DX11.exe`, wherever it is installed (a Steam library, GOG, ...). Or start
  the editor once and choose that folder: it fills `GameDir=` in for the launcher too.
- **Copy it into the game folder yourself**: leave `GameDir=` empty; the launcher and the editor
  use the folder they are in. The game's own files are still never modified, only added to.

Nothing is tied to one PC: the launcher, loader, mods and editor find everything from where their
own files are and from `GameDir=`. The launcher never looks anything up in Steam or the registry.
`rfa_mods.ini` and `console.ini` in `bin\` are your own settings (with your `GameDir=`); the
templates others start from are in `config\`.

## Playing

**Once:** in Steam, right-click the game, then Properties, then Game Versions & Betas, and pick
**publicbeta**. Steam swaps in the beta's two exe files (about 10 MB); nothing else changes. The
mods that change the game's code are written for that build; with any other build (Steam's
default branch, or a copy from another store) the game starts normally and those mods leave it
alone, saying so in `logs\rfa_mods.log`. The editor works with any copy: it only reads the game's
data files.

Run `RFAModLauncher.exe` (with Steam running, for a Steam copy).

**First time:** in the game, go to Options → Display, choose **Windowed** and your monitor's native
resolution (e.g. 1920x1080), apply and keep the changes. The mod turns that window into borderless
fullscreen. While the game is set to Fullscreen, the mod stays out of the way and the game runs
in its normal exclusive fullscreen. Optionally, to start it from Steam, right-click
the game, then Properties, then Launch Options, and enter the launcher's path on your PC, in
quotes, followed by `%command%`. For example, if you put the mod in `D:\Games\RFA mods`:

```
"D:\Games\RFA mods\RFAModLauncher.exe" %command%
```

To play unmodded again, clear the launch options or start the game normally.

## Mods

**borderless**: when the game is set to Windowed, its window loses its frame and covers the whole
monitor. Alt-tab pauses the game, as it always does, but no longer minimizes it or switches its
display mode; the cursor is freed and keyboard/controller input is ignored while you're in
another app. The game normally blocks the Windows key
system-wide (even in other apps); in borderless it works again (`AllowWindowsKey`). Pick your
monitor's native resolution in the game's video options, since other resolutions are stretched.
See `[Borderless]` in the ini.

**fov**: sets the camera's field of view (`[FOV] VerticalFov=`, 30–170; the game's own is 45
vertical, about 73 horizontal on 16:9). Works on foot, and with `Vehicles=1` in vehicles and
exo-suits too. Cutscenes keep their intended framing. The game's "finalise the camera FOV"
function (`0x4D32D0`) is replaced by a few lines of C++ that multiply by FOV / 45, and the
vehicle/exo-suit camera mode (`0x4C5370`) is wrapped so its FOV is scaled right after it runs.
Both cap the result at 170: the engine has no limit of its own, and at 180 or more the picture
turns inside out.

**video_settings**: makes the game's video options (resolution, Windowed, quality settings)
stick between launches. The game saves them through its own `ReadSetting`/`WriteSetting`
functions (`0xCC6090`/`0xCC60C0`), which store them under `HKEY_LOCAL_MACHINE`. Windows refuses that
without administrator rights, so every save failed and the game started at 1280x720 fullscreen
each time. This mod replaces those two functions with versions that use `video_settings.cfg`, a
plain text file you can read or edit. The game's registry code never runs for these settings.

**console**: improves the developer console (see below).
- *Mouse wheel*: while the console is open the wheel scrolls its output, 3 lines per notch, and
  stops once the oldest line is at the top. The wheel doesn't reach the game then, so it no longer
  switches weapons behind the console. It wraps the game's mouse message handler (`0xCDF220`) and
  moves the console's own scroll position (`0x28FBA40`), the one Page Up / Page Down use.
- *`console.ini`*: console settings it applies for you, one `<console name>=<value>` per line.
  `[Startup]` is applied once when the game has finished starting (right after the game's own
  `gamestart.txt`); `[Gameplay]` every time gameplay starts after loading a level, save or
  checkpoint (right after the game's own per-level scripts, `0x540840`). The mod looks each name
  up in the game's command list to see what it is: on/off settings take `1` / `0` (sent as the
  console's `on` / `off`), number settings take the number, commands take their argument (`1` /
  `0` also mean on / off) or nothing to just run them. Because values say what a setting should
  be, applying them again after every load is safe. Each one is logged, and unknown names are
  skipped with a message. The template is `config\console.ini`, copied only when `bin\console.ini`
  doesn't exist, so your edits are kept. By default it hides the beta's performance text.
- *The level editor's link*: every frame (from the game's frame update, `0x544CB0`) the mod puts
  what the game shows into a small block of shared memory the editor reads while it follows the
  game (the object picked in the game, the camera's position, direction and field of view). While
  the folder `bin\link` exists (the level editor makes it), it also runs the console commands the
  editor puts in `bin\link\to_game.txt`, checked ten times a second.

**FOV slider in the game's menu**: Options → Gameplay gains a **Field of View** slider (45–170,
the same kind of slider as the audio volumes). The view changes while you move it, so from the
pause menu you can see the result behind the menu; Confirm keeps it and Cancel puts the old one
back. It's saved to `rfa_mods.ini`. This uses two loader features any mod can build on:
- *Overrides*: `overrides\` mirrors the game folder, with a folder for each archive, so
  `overrides\build\pc\cache\interface.vpp_pc\ui_options_gameplay.lua` replaces that script from
  `<game>\build\pc\cache\interface.vpp_pc`. The archives themselves are untouched. At startup the
  loader checks that every override names a real file in a real archive and logs each one (or why
  it was skipped). Any file directly inside an archive can be overridden: Lua scripts are swapped
  when the game compiles them (`luaL_loadbuffer`, `0xD95730`), every other file when the game
  looks it up by name (`FindFile`, `0xC90960`), which then reads the replacement as a loose file.
  An override replaces only its own archive's copy (recognised by size; identical copies in other
  archives share it). Files inside `.str2_pc` containers can't be overridden one by one; the
  editor rebuilds the container instead. Inside an override, our
  changes are marked `[RFA]`; the rest is the game's original. See `src\overrides\README.md`. The
  game's slider code assumes every slider starts at 0, so the override maps mouse clicks and drags
  itself.
- *Lua bridge*: menu scripts can call `rfa_get_setting(section, key, default)`,
  `rfa_set_setting(section, key, value)` and `rfa_log(text)`. After a change the loader calls
  `RFAMod_SettingChanged(section, key)` in every mod that exports it.

## Editor

`bin\editor\RfaEditor.exe` is an editor for the game's files (needs the .NET 10 Desktop Runtime).
It finds the game through `GameDir` in `rfa_mods.ini`, like the launcher, and only ever reads the
game folder. One dark window, laid out like other game editors (Nanoforge among them):

- **Explorer** (left): every archive in the game, and search. Each file has an icon and colour
  for its kind (tables, textures, models, zones, scripts, sounds, ...).
- **Documents** (middle, as tabs): the preview of the file picked in the explorer, and every table
  and level you open.
- **Outliner** and **Inspector** (right): what is in the chosen document (a level's objects, a
  table's entries), and the details of what is picked (an object's properties, a file's details
  and buttons).
- **Log** (bottom): everything the editor did, such as each save.

The explorer:

- Every archive in the game (main game and DLC), the files in each, and the files inside each
  `.str2_pc` container, including compressed archives.
- Search by file name across all of them (the files inside containers become searchable a few
  seconds after it starts).
- A preview: text files (`.xtbl`, `.lua`, ...) as text, everything else as a hex dump.
- **Extract** a file, or **Extract all** files of an archive or container into a folder.
- **Copy to overrides**: puts a copy at the right place in `bin\overrides\`, where the game uses
  it instead of its own from the next start through the launcher. Edit that copy. Files you have
  changed (overridden) are marked with an orange `changed` tag.

**Table editor**: double-click an `.xtbl` table (or **Open in table editor**). The game's tables
describe their own fields (the `<TableDescription>` Volition's own table editor used), and the
editor is built from that description rather than from anything hard-coded:

- Entries in the outliner, in the developers' folders (`_Editor` categories), with a Find box.
- The selected entry's fields in the tab, in the description's order, each with an editor for
  its type: text boxes that refuse numbers outside the allowed range (a red border says why),
  drop-downs for choices, check boxes for flags, a pick list of entry names for references to
  other tables (e.g. a weapon's `Item_3d` lists every entry in `items_3d.xtbl`), groups, lists
  with Add and Remove, and a switch for "one of" groups such as a weapon's shot type.
- Hovering over a field's name shows its help text, default and allowed values. Fields an entry
  doesn't set show the default the game uses; `×` removes a value to go back to the default.
- Above the outliner: **New** (an empty entry, every field the game's default, in the folder of
  the entry picked; tables with Volition's templates, such as `weapons.xtbl`, also offer those),
  **Copy** (Ctrl+D: every field the same, right after the original, named `<name>_copy`; rename
  it, as the game finds entries by name) and **Delete** (Del). Delete first says which tables
  name the entry (as the game sees them, with your overrides), e.g. 40 characters in
  `character.xtbl` carry `phased_plasma_rifle`. Scripts and levels aren't searched.
- **Save** writes the table to `bin\overrides\`. It opens your override next time,
  or the game's own copy if you have none. Only what you changed differs from the original file:
  the rest is written back byte for byte, in the game's own formatting.

**Textures**: select any texture file (`.cpeg_pc`, `.gpeg_pc`, `.cvbm_pc`, `.gvbm_pc`), in an
archive or inside a container, and it is shown as a picture (with a choice when the file holds
several textures, and a checkerboard behind transparent parts).

- **Export PNG** or **Export DDS**. A DDS keeps the exact format and mip levels.
- **Replace** with a PNG, JPG, BMP, TIFF or DDS of the same size. The editor encodes it in the
  texture's own format (DXT1, DXT3, DXT5, 32-bit, 565) with fresh mip levels. A DDS that already
  has the right format and mip levels is used as it is.
- Most textures are inside `.str2_pc` containers, which the game streams in whole. For those the
  editor rebuilds the container into `overrides\`, compressed the way Volition's tools did, and
  updates the container's entries in the asset lists (`.asm_pc`) that tell the game how big it is.
- **Restore original** goes back to the game's own textures for that file or container, and puts
  the asset lists back. Use it rather than deleting the files.
- Textures keep their size for now: images of another size are refused.

**Models**: select any model (`.csmesh_pc` static meshes, `.ccmesh_pc` characters, `.cchk_pc`
destructible objects such as buildings and props, `.ccar_pc` vehicles, `.cstch_pc` stitch pieces
(the rock, cave walls, roads and other fixed pieces levels are built from), or their `g` files) and
it is shown in 3D with its textures. Drag to turn it around, scroll to zoom.

- Characters are shown at their most detailed level, vehicles with every part in place (wheels,
  doors, a walker's legs). Destructible objects are shown whole, each
  part where it goes, and **Broken pieces** shows the debris they break into instead. Many
  destructible object files hold several objects (sizes of a box, sections of a wall); a list
  above the view picks which one is shown.
- **Skeleton** (characters): shows the character's skeleton (the rig `character.xtbl` names for it)
  inside it, the character drawn see-through: every bone in its rest pose, down to the fingers
  and face. A skeleton file (`.rig_pc`) picked on its own is shown by itself, with its bones' and
  tags' names.
- **Animations**: an animation file (`.anim_pc`) plays on a character whose animation sets (in
  `character.xtbl`) name it, the character's mesh moving with its bones; the list above the view
  picks another character, or a skeleton alone with the right number of bones. A character's
  model (`.ccmesh_pc`) has a list of the animations its sets name, to play on it. Below the list:
  play/pause (it loops), the speed, **Move along** (let the animation move the character, as a
  walk does; off, it stays on the spot), and a slider to go to any moment. **Skeleton** shows the
  bones moving inside the see-through character. Civilians show only their head and hands for
  now (the game puts them together from several meshes).
- Textures are found by name, first in the model's own container, then anywhere in the game (once
  the editor has finished reading the containers, a few seconds after it starts).
- **Export OBJ** saves the model as OBJ + MTL with its colour textures as PNG, which Blender and
  other 3D programs open.
- Models can be viewed and exported; putting changed models back into the game is a later step.
  Foliage meshes (`.cfmesh_pc`: the debris the game scatters over the ground) are shown too.

**Level editor**: double-click a zone (`.rfgzone_pc`, or a mission's `.layer_pc`), or press **Open
in level editor**. The game's world is a grid of zones; each holds the objects placed in that part
of a level (buildings and props, lights, spawn points, navigation points, triggers, ...).

- The zone in 3D: every building and prop drawn with its model and textures where the zone puts it
  (each the very object the zone names in its model file, all its parts in place), the items and
  weapons lying about (ammo, salvage, audio logs, guns: the **Pickups** chip) with theirs, the
  enemies and allies at squad spawns (each squad's leader), exos and vehicles (LAVs, walkers,
  flyers, tanks) at their spawns, turrets (base, swivel and gun head fitted together), and
  every other object as a small cube coloured by kind (lights yellow, paths blue, spawns orange,
  regions red, ...), over a grid on the ground (every 10 m). The **Show** chips above the view hide
  or show a whole kind at once (say, everything but the buildings), and the grid. The axis widget
  in the corner shows which way X, Y and Z point. The toolbar's icons say what they do when
  hovered; **?** lists every mouse button and key.
- **Links between objects**: the picked object's links are drawn as lines, yellow to the objects it
  names and blue from those naming it, with an arrow pointing at the one named, and listed in the
  inspector's **Links** section (click one to pick it and fly to it). They are what ties a level's
  logic together: navpoints' paths, roads, a patrol's first navpoint, a logic handler's triggers
  and the objects it acts on, the objects an animated rig moves, lights attached to buildings. The
  **Links** chip draws every link of the zone and its mission layers at once, each in the colour of
  the object naming the other. Lines keep the same width on the screen near and far. They can't be
  edited yet.
- **Direct3D view** (the monitor icon at the right of the toolbar): draws the level with Direct3D 11
  on the graphics card instead of WPF, the first step of `docs\renderer-plan.md`. It shows the same
  things from the same camera, lit the same way for now but worked out for every pixel, and only
  draws again when something changed (a frame takes the editor 2–3 ms). Picking, the gizmo and
  everything else work as before. It uses the Vortice libraries (MIT; see `docs\third-party-notices.txt`).
- **Game light** (the sun icon next to it): lights the level as the game does, with the game's own
  formulas (read from its shaders): every lamp of the zone and the zones around (spot, omni, tube
  and ambient lamps, with their colours, fades and cones), the ambient light, fog and exposure of
  the environment box the camera is in, and the game's tone curve. Surfaces use their materials as
  the game does: bumps (normal maps), shine (specular maps), glow (light fixtures, screens, light
  strips) and their colour multiplier. Caves come out dark, lit by their lamps. Lamps flicker and
  sway as in the game (fire, strobes, broken lights), surfaces are see-through or cut out only where
  the game's material says so (glass and water; the bullet holes laid over walls, hair, leaves), the
  Legion's creatures, tentacles and pods are drawn as the game's alien skin shader draws them, and outdoors the area's sky is drawn behind everything
  (the game's own sky picture). Lamps the game gives shadows (spotlights over the wreck, the
  tunnels' floodlights; 561 in the game) cast them as the game does, soft-edged, as dark as each
  lamp says, fading out away from the camera. Not yet: the sky's moving clouds, shadows of the
  few omni lamps that cast them.
- The zone's **level geometry** is drawn around its objects: the rock, cave walls, ledges, roads,
  bridges and other fixed pieces its stitch list places (for the first mission's `world_08_02`,
  865 pieces including the Terraformer, its platform and the ring walls), the **decals** laid on
  them (trash, blast marks, blood, signs), the **foliage** (paper, cardboard, scrap, rubble and
  rocks the game scatters over the ground in 11 zones, scattered here exactly as the game does it
  and set down on what is below; pieces with no surface in their 1 m cube are left out, as the
  game does: 336 of 495 are drawn in `world_08_02`), and the **terrain**: the ground of outdoor
  zones, painted the way the game draws it: its four texture layers (sand, sand ripples, rubble,
  ...) mixed by the zone's blend map, in colours matched to the game's own far-away picture of
  the ground (cave zones have no ground). With textures off it is drawn plain. Each can be hidden with
  its chip, but not picked or changed yet; clicking them doesn't pick objects behind them. They
  are found once the editor has read every container.
- A zone's **mission layers** are drawn with it: what each mission adds (its enemies at their squad
  spawns, triggers, force fields, turrets, vehicles), with a chip and an outliner group per
  mission (`Mission_01A` for `world_08_02`). Their objects can be picked and their properties
  read, and **Open** opens the layer in its own tab to change them. A layer opened on its own is
  drawn with its zone's objects in the same way.
- The **zones around** the open one are drawn too, where the game has any (the open world's zones
  are mostly apart; `world_08_02` has three around it): their level geometry, ground (coloured with
  their colour maps) and objects' models, so the zone's edges meet what is next to them. They
  can't be picked; open a zone to work on it. **Zones around** hides them.
- **Speed**: the view draws objects in batches (every object of one kind with one texture, in one
  64 m square of the map, joined into one mesh), the level geometry and the zones around joined per
  texture, and each model's parts joined per texture: a zone is a few hundred pieces for WPF to
  draw each frame instead of over ten thousand (`world_08_02`: 523, was 10,201). A click still
  picks the one object under the mouse (found by the triangle hit), and an object that moves is
  taken out of its batch and drawn on its own. The log says how long each part of a zone took to
  load.
- **Show the whole zone** frames the zone's objects, leaving out the few far-off ones some zones
  have.
- **The running game** (started with RFAModLauncher, so the console mod links it to the editor
  through `bin\link`): **Show in game** (the controller icon) sends the game's camera to the picked
  object, in slew mode (the free camera), keeping the way it looks, and picks the object there, so
  the game's debug display names it. **Follow the game** (the link icon) moves this view with the
  game's camera, with the game's field of view (the same vertical view as your FOV setting, however
  wide the editor's view is), and picks here what you pick there (Q in slew mode), when it is in
  the open zone or its mission layers. Handles and positions are the game's own: a building picked in the game
  (`0x820300F9`, `str_sur_edf_qm_01` at 90.4, -4.7, 1306.6) is the same object at the same place in
  the editor. **Props as in the game** (the arrow-down icon): clutter (debris, barrels, crates,
  wreck pieces) falls and settles by physics when the level starts, so the game has it elsewhere
  than the zone file puts it; while this is on, the editor asks the game twice a second where each
  of the zone's clutter props is and draws it there. Only the drawing changes: the zone isn't, and
  the picked object's box and the gizmo stay where the zone puts it. Tried offline with a stand-in
  for the game; not yet with the game itself.
- The outliner lists the objects by class, each with its kind's icon, with a Find box.
- Objects are moved and turned with the mouse, as in other 3D editors: pick an object (click it,
  or pick it in the outliner) and drag the arrows of the **Move** tool (W) along X (red), Y (green)
  or Z (blue), or the rings of the **Rotate** tool (E) to turn it around them. **Q** is just
  picking. **Snap** moves in steps of the chosen size and turns in 15° steps. The handles are drawn
  on top of everything and stay the same size on screen.
- **Add** (+ in the toolbar) lists one of each kind of object the file has (a crate, a light, a
  squad spawn, a building piece, ...), with a Find box; picking one adds a copy where the middle of
  the view points. **Duplicate** (**Ctrl+D**) copies the picked object in the same place, to be
  dragged away. A copy is a new object with a handle of its own, attached to nothing; its model is
  already in the zone, so the game can show it. **Delete** (**Del**) removes the picked object
  with the objects attached to it (a building's lights) and its destruction links to the buildings
  next to it (the game's table of which pieces hold onto which, renumbered as the game's own
  files number it). It is refused, saying what, while something else names one of them (another
  object's links, such as navpoints linked in a path, or an object in a mission layer). The
  game's object table (the zone's relation table) is built again on saving, as the game's tools
  do. Not tried in the game yet.
- Every change can be undone and redone (**Ctrl+Z** / **Ctrl+Y**), one step per drag, edit,
  addition or deletion.
- Moving around: hold the right mouse button to look around, and use **WASD** (Q/E down/up, Shift
  faster) to fly while holding it; middle-drag slides the view, the wheel moves forwards and back,
  Alt+drag circles the picked object, **F** looks at it.
- The inspector shows the picked object's **Transform**: its position (X, Y, Z in metres) and
  rotation (around X, Y and Z, in degrees). Type a number, or drag a field's coloured letter left
  and right to change it (Shift: faster; Snap applies). Below it are the object's other
  properties, with readable names (hover for the game's own name and type): numbers, colours,
  on/off flags, and text (up to 255 characters; longer than before makes the object larger).
  Moving or turning an object moves its bounding box (which the game uses) with it.
- **Save** (to overrides): a zone inside a container is saved through a rebuilt container and updated
  asset lists, as for textures. Many zones are in the game twice, identically (in a
  `zonescript_*.vpp_pc` archive and in their level's container); the game may read either, so the
  editor saves every copy. **Restore** puts the game's own zone back in every copy; other
  changes in the same containers (such as replaced textures) stay.
- Not yet: civilians' outfits,
  characters and walkers in poses other than their rest pose, the ground's normal maps, effects such as fire and smoke (of the sky, only its picture), editing
  the level geometry, terrain or foliage, adding objects whose models aren't in the zone,
  deleting objects other objects' links name, editing links between objects, and changing a
  zone's layers or the zones around it from the zone's tab.

The code is in `src\editor\`: `RfaFormats` reads and writes the formats (no UI, reusable by other
tools) and `RfaEditor` is the editor: `MainWindow` the layout and explorer, `Documents\` the tabs
(file preview, table editor, level editor), `Level\` the level view's camera, gizmo, scene, grid,
kinds of objects, inspector parts, level geometry and the ground's painter, `Theme.xaml` the dark look of every control (with
`Theme.cs` for code), `Icons.cs` the icons (characters of the Segoe MDL2 Assets font that comes
with Windows) and the kinds of files, `NumberField.cs` the draggable number box.

## Developer console

The publicbeta build has the game's developer console (about 1,500 commands and settings). Press
the `` ` `` / `~` key (left of 1) to open or close it; it toggles when the key is released. Scroll
with the mouse wheel (console mod) or Page Up / Page Down; End jumps back to the newest output.
Up / Down recall earlier commands. Useful
commands: `search <text>` finds commands, `print_to_file` writes them all to
`console_commands.txt`, `script <file>` runs a text file of commands, `cam_fov <degrees>` sets the
camera FOV. To apply settings automatically, put them in `bin\console.ini` (console mod above).
`slew_mode` (or `` ` `` + S) frees the camera to fly anywhere; Q then picks what the camera looks
at and shows its details (class, handle, model, position), the same handles the level editor shows.
Single-player only: some commands refuse to run in multiplayer, and VAC still applies online. The beta writes a few files into its own folder (`console_commands.txt`,
`rfg_last_level_pc.txt`, `Load_Successful.txt`); the integrity check lists those as `GAME`.

## Building

Needs Visual Studio with the C++ desktop workload, and the .NET 10 SDK for the editor (skipped
if it's missing). Run `build.cmd`. Output goes to `bin\`; `config\rfa_mods.ini` is copied to
`bin\` only if there is no ini there yet. The build turns off the .NET SDK's usage reporting and
its first-run web certificate. The editor's builds name no folder of the PC they were built on
(`src\editor\Directory.Build.props` writes `/_/` in place of the project's folder).

## Sharing

`tools\package.ps1` makes the copy to give others, in `dist\RFA-Mod\` and as
`dist\RFA-Mod-<date>.zip`:

```
powershell -ExecutionPolicy Bypass -File tools\package.ps1
```

It holds the launcher, the loader, the mods, the mod's own overrides (from `src\overrides\`, not
the files you edited), the editor (without its `.pdb` debugging files), this README and the
licence, with the settings templates from `config\` (so `GameDir=` is empty). Your own `rfa_mods.ini`,
`console.ini`, `video_settings.cfg`, `logs\`, `link\` and edited overrides stay out. Every file it
packs is checked for anything naming your PC (your user folder or name, this project's folder,
your game folder), and nothing is packaged if one does. `-Build` runs `build.cmd` first;
`-NoEditor` leaves the editor out; `-Version v0.1.0-beta` makes a release: the zip is named
`RFA-Mod-v0.1.0-beta.zip` and its `version.txt` says which commit it was built from.

**Releases** on GitHub are built by GitHub itself (`.github\workflows\release.yml`): pushing a tag
that starts with `v` builds that commit on a clean Windows machine with `build.cmd`, packs it with
`tools\package.ps1 -Version <tag>` and publishes the zip as a release, a pre-release when the tag
has a `-` in it (`v0.1.0-beta`). Its text comes from `docs\releases\<tag>.md`. To release:

```
git tag -a v0.2.0-beta -m "RFA-Mod v0.2.0-beta"
git push origin v0.2.0-beta
``` Others unzip it into a folder of their own (and set
`GameDir=`, or let the editor ask) or into their game folder. The editor needs the
[.NET 10 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/10.0); the mod itself
needs nothing else.

## Writing a mod

Create `src\mods\<name>\<name>.cpp`, define `RFA_MOD_NAME`, include `rfa_mod.h`, and export:

```cpp
extern "C" __declspec(dllexport) void __cdecl RFAMod_Init() { /* install hooks */ }
```

`rfa_mod.h` provides `rfa::Log`, `rfa::IniInt`/`IniString`, `rfa::HookImport` (import table),
`rfa::ReplaceFunction` (send a game function to a C++ function with the same signature) and
`rfa::HookFunction` (the same, keeping a pointer to call the original). `rfa_game.h` has
`rfa::game::IsSupportedBuild()` (check it before touching game code) and the game's Lua API.
Write hooks in plain C++: match the game function's calling convention (`__cdecl`, or
`__fastcall` for `this`-in-`ecx` methods without stack arguments) instead of writing assembly.
Add a `cl` line for it to `build.cmd`. Disable a mod with `<name>=0` under `[Mods]`.

## When something crashes

`logs\rfa_mods.log` ends with a `CRASH:` block: the exception, the module+offset it happened in,
the registers, and likely callers found on the stack. A minidump (`logs\crash-*.dmp`) is written
next to it for deeper analysis. Turn this off with `CrashReports=0` under `[Loader]`.

## Checking the game files are untouched

```
powershell -ExecutionPolicy Bypass -File tools\game-integrity.ps1 -Mode Verify
```

This compares every file in the game folder with the SHA-256 snapshot of the publicbeta install,
`reference\build-1932726-publicbeta\game_baseline.csv`. `reference\build-1291145\game_baseline.csv`
is the default branch, taken before the first modded launch. Files the beta writes itself are
listed as `GAME`, not counted as changes.

## License

Copyright (C) 2026 FragsAreUs.

This project's own code and notes are free software under the GNU General Public License,
version 3 or (at your option) any later version ([LICENSE](LICENSE)). Anyone may use, change and
share them, but whatever they pass on, changed or not, must stay under the same licence with its
source code. The editor's Direct3D libraries are under the MIT License, which allows this; their
notices are in [docs/third-party-notices.txt](docs/third-party-notices.txt). The source code is at
<https://github.com/FragsAreUs/RFA-Mod>; a release's `version.txt` says which commit it was built from.

The mods and the editor work on your own copy of Red Faction: Armageddon, which stays untouched,
and the game's files aren't included, with one exception:
[ui_options_gameplay.lua](src/overrides/build/pc/cache/interface.vpp_pc/ui_options_gameplay.lua),
the game's own Options → Gameplay script with our Field of View slider added. Only the lines
marked `[RFA]` in it are ours and under the GPL; the rest is the game's. Red Faction: Armageddon
belongs to its owners; this is an unofficial fan project, not made or endorsed by them.
