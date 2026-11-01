# How RFA-Mod works

This page is for people working on RFA-Mod's code: how the mod gets into the game, what each mod
changes inside it, and how the editor is put together. To install and use the mod, see the
[README](../README.md).

Every game address here is in the Steam **publicbeta** exe, `RedFactionArmageddon_DX11.exe` build
1932726 (July 2017). It is an internal "Release" build that still has the developer console. On
any other build the mods that touch game code check `rfa::game::IsSupportedBuild()`, log that it
isn't the publicbeta build, and leave the game alone.

## Contents

- [Getting into the game](#getting-into-the-game)
- [Project layout](#project-layout)
- [The loader](#the-loader)
- [The mods](#the-mods)
- [The editor](#the-editor)
- [Writing a mod](#writing-a-mod)
- [Building, packaging and releasing](#building-packaging-and-releasing)
- [Checking the game files are untouched](#checking-the-game-files-are-untouched)

## Getting into the game

1. `RFAModLauncher.exe` starts `RedFactionArmageddon_DX11.exe -packfiles` suspended, holds it at
   its very first instruction and loads `rfa_loader.dll` into it. Keep `-packfiles`: the
   official launcher (`rf4_launcher.exe`) passes it, and without it the engine expects loose dev
   files instead of the shipped `.vpp_pc` archives, and its first frames fail with
   `DXGI_ERROR_DEVICE_REMOVED`.
2. The loader loads every DLL in `mods\` (alphabetically) and calls its `RFAMod_Init` export.
3. The game is released. Mods hook Windows functions the game imports, and send some of the
   game's own functions to plain C++ replacements in the mod DLLs.

The game folder is `GameDir=` from `rfa_mods.ini`, or the launcher's own folder when the mod was
copied into the game folder, or (with Steam's launch options `"<launcher>" %command%`) the folder
of the exe Steam passes on the command line. The launcher sets `SteamAppId` / `SteamGameId` (55110)
when they aren't set, since without an app id the game asks Steam to restart it, which would drop
the mods. It never looks anything up in Steam or the registry. With `WaitForExit=1` it waits for
the game to exit, which keeps Steam's playtime and overlay right.

Nothing is tied to one PC: the launcher, loader, mods and editor find everything from where their
own files are and from `GameDir=`. `rfa_mods.ini` and `console.ini` in `bin\` are your own
settings; the templates others start from are in `config\`.

## Project layout

```
src\launcher\             RFAModLauncher.exe: starts the game and injects the loader
src\loader\               rfa_loader.dll: loads mods, crash reports, overrides, Lua bridge
src\mods\<name>\          one folder per mod DLL (borderless, console, fov, guerrilla_hud,
                          guerrilla_world, video_settings), and a mod's own scripts
src\common\               rfa_mod.h (logging, ini, hooking helpers), rfa_game.h (build check, Lua API)
src\overrides\            replacements for files inside the game's archives, same layout as the game
src\editor\RfaFormats\    C# library that reads and writes the game's file formats (no UI)
src\editor\RfaEditor\     the editor (C#, WPF desktop app); built into bin\editor\
src\editor\GuerrillaHud\  GuerrillaHud.exe: makes Guerrilla's HUD for guerrilla_hud; built into bin\editor\
src\editor\GuerrillaWorld\ GuerrillaWorld.exe: builds the world rfg for guerrilla_world; built into bin\editor\
config\rfa_mods.ini       the settings template (copied to bin\ on the first build)
config\console.ini        console settings template (copied to bin\ when missing)
docs\editor-guide.md      how to use the editor
docs\how-it-works.md      this page
docs\editor-roadmap.md    what the editor still lacks, by Volition's own tools (CLOE, ...)
docs\renderer-plan.md     the plan for drawing levels with Direct3D 11, to look like the game
docs\releases\            each release's notes (the GitHub release text)
tools\game-integrity.ps1  proves the game folder is untouched
tools\package.ps1         makes a clean copy to share (dist\), without your own settings
reference\build-*\        each Steam build's file list, for tools\game-integrity.ps1
bin\                      the built mod (what you install); build\ holds compiler leftovers
dist\                     made by tools\package.ps1: the mod and editor as others get them
```

## The loader

Settings are in `rfa_mods.ini`. Everything the launcher, loader and mods do is written to
`logs\rfa_mods.log` (the launcher keeps the previous run as `rfa_mods.prev.log`). A mod is skipped
when its name is set to `0` under `[Mods]`.

**Crash reports** (`CrashReports=1` under `[Loader]`): if the game crashes, `logs\rfa_mods.log`
ends with a `CRASH:` block: the exception, the module+offset it happened in, the registers, and
likely callers found on the stack. A minidump (`logs\crash-<date>-<time>.dmp`) is written next to
it for deeper analysis.

**Overrides** (`Overrides=1`): `overrides\` mirrors the game folder, with a folder for each
archive, so `overrides\build\pc\cache\interface.vpp_pc\ui_options_gameplay.lua` replaces that
script from `<game>\build\pc\cache\interface.vpp_pc`. The archives themselves are untouched.
- At startup the loader checks that every override names a real file in a real archive and logs
  each one (or why it was skipped).
- Any file directly inside an archive can be overridden: Lua scripts are swapped when the game
  compiles them (`luaL_loadbuffer`, `0xD95730`), every other file when the game looks it up by
  name (`FindFile`, `0xC90960`), which then reads the replacement as a loose file.
- An override replaces only its own archive's copy (recognised by size; identical copies in other
  archives share it).
- Files inside `.str2_pc` containers can't be overridden one by one; the editor rebuilds the
  container instead.
- Inside an override, our changes are marked `[RFA]`; the rest is the game's original.
- See [src/overrides/README.md](../src/overrides/README.md) for the details.

**New files**: a file in `overrides\` outside any archive's folder, where the game folder has no
file (a new map's archive, `overrides\build\pc\cache\<map>.vpp_pc`), is handed to the game when it
looks for that path or name and finds nothing. The game mounts a level's archive the same way: it
finds the archive (`0x6CD1A0`, through `0xC92010`), then opens it with `cf_open` (`0xC91470` →
`0xC914E0`), both through `FindFile`; the mount itself (`0xCB5280` → `0xCB53C0`) reads the archive
from the file it was given.

**Ruin maps**: a new Ruin map is listed in an override of `wrecking_crew_maps.xtbl`. The game
itself starts Ruin maps by their place in that table (it reads at most five), whatever their
`file_name` says, so the loader puts each map's `file_name` back after the game reads the table
(`0x50E690`).

**LogLookups=1** under `[Loader]` logs every lookup of a level's files (zones, layers, archives,
containers, terrain), to see what a level loads. It also logs the Ruin map list when a Ruin match
begins (`0x50F400`).

**Lua bridge**: menu scripts can call `rfa_get_setting(section, key, default)`,
`rfa_set_setting(section, key, value)` and `rfa_log(text)`. After a change the loader calls
`RFAMod_SettingChanged(section, key)` in every mod that exports it.

## The mods

### borderless

When the game is set to Windowed, its window loses its frame and covers the whole monitor. While
the game is in its own exclusive fullscreen mode, the mod leaves it alone. Everything goes through
the game's import table (`USER32.dll`: `CreateWindowExW`, `SetWindowLong*`, `SetWindowPos`,
`ShowWindow`, `ClipCursor`, `SetCursorPos`, `GetForegroundWindow`, `IsIconic`, `GetAsyncKeyState`,
`RegisterRawInputDevices`, `SetWindowsHookExA`, ...; and `XInputGetState`). DXGI/D3D COM vtables are
deliberately not patched: the Steam overlay patches them too, and when the game rebuilt its swap
chain the two hooks ended up calling each other until the stack overflowed.

With `IgnoreFocusLoss=1` the game is never told it was alt-tabbed away from: focus messages are
hidden from it, its own low-level keyboard hook doesn't see Alt+Tab and friends, and the mod frees
the cursor (no clipping, no recentring) and mutes keyboard and controller input instead. It also
asks the game to open its pause menu, as if you had pressed the pause key (the request flag
`0x135B3F1`, whose own setter is `0x582760`; the HUD opens the menu on its next update unless a
cutscene or another menu is up, `0x57B774`). So switching away always pauses the game, as it does
when the game sees the switch itself.

The game normally blocks the Windows key system-wide while it runs; with `AllowWindowsKey=1` it
works in the game too. With `Enabled=0` every hook stays installed but only logs.

### fov

Every frame the active camera mode hands its camera to the game's "finalise the camera FOV"
function (`0x4D32D0`), which copies the camera's FOV (45 degrees vertical unless something like
aiming changes it) into the camera manager as the FOV the frame is drawn with. On 4:3 displays it
multiplies by 1.125 first (the console's `Camera_43_fov_modifier`). The mod replaces that function
with a few lines of C++ that multiply by FOV / 45 instead. Cutscene cameras don't use it, so
cutscenes keep their intended framing.

The vehicle and exo-suit camera mode (`0x4C5370`) doesn't use that function: it copies its
camera's FOV into the camera manager itself. With `Vehicles=1` the mod wraps that camera mode and
scales the FOV right after it runs. Both cap the result at 170: the engine has no limit of its
own, and at 180 or more the picture turns inside out.

**The FOV slider in Options → Gameplay** is an override of the game's own script,
[ui_options_gameplay.lua](../src/overrides/build/pc/cache/interface.vpp_pc/ui_options_gameplay.lua)
(every line we added or changed is marked `[RFA]`). It reads and saves `[FOV] VerticalFov=`
through the Lua bridge's `rfa_get_setting` / `rfa_set_setting`, and the fov mod picks up the change
through `RFAMod_SettingChanged`. The game's slider code assumes every slider starts at 0, so the
override maps mouse clicks and drags itself.

### video_settings

The game saves its video options (resolution, Windowed, quality settings) through its own
`ReadSetting`/`WriteSetting` functions (`0xCC6090`/`0xCC60C0`; their only callers are the video
settings load, `0x5A7E40`, and save, `0x5A7C50`), which store them under `HKEY_LOCAL_MACHINE`.
Windows refuses that without administrator rights, so every save failed and the game started at
1280x720 fullscreen each time. Both Steam builds have this bug. The mod replaces those two
functions with versions that use `video_settings.cfg` in the mod folder, a plain text file. The
game's registry code never runs for these settings, and nothing in the mod calls the registry.

### console

- *Mouse wheel*: while the console is open the wheel scrolls its output, 3 lines per notch, and
  stops once the oldest line is at the top. The wheel doesn't reach the game then, so it no longer
  switches weapons behind the console. The mod wraps the game's mouse message handler (`0xCDF220`)
  and moves the console's own scroll position (`0x28FBA40`), the one Page Up / Page Down use. The
  console keeps its last 256 output lines in a ring buffer.
- *`console.ini`*: `[Startup]` is applied once when the game has finished starting (right after
  the game's own `gamestart.txt`); `[Gameplay]` every time gameplay starts after loading a level,
  save or checkpoint (right after the game's own per-level scripts, `0x540840`). The mod looks each
  name up in the game's command list to see what it is: on/off settings take `1` / `0` (sent as
  the console's `on` / `off`), number settings take the number, commands take their argument (`1`
  / `0` also mean on / off) or nothing to just run them. Because values say what a setting should
  be, applying them again after every load is safe. Each one is logged, and unknown names are
  skipped with a message. The template is `config\console.ini`, copied only when `bin\console.ini`
  doesn't exist, so your edits are kept.
- *The level editor's link*: every frame (from the game's frame update, `0x544CB0`) the mod puts
  what the game shows into a small block of shared memory the editor reads while it follows the
  game (the object picked in the game, the camera's position, direction and field of view). While
  the folder `link\` exists (the level editor makes it), it also runs the console commands the
  editor puts in `link\to_game.txt`, checked ten times a second.

### guerrilla_world

What a world ported from Red Faction: Guerrilla needs that Armageddon doesn't do itself (see
`[GuerrillaWorld]` in the ini). Everything below happens only in the worlds named by `Worlds=`.

- **The sky**: a dome coloured by the time of day's nine sky colours (four bands up from the
  horizon on the west side, four on the east, and the zenith), worked out the way Guerrilla's sky
  shader does it. Armageddon reads those colours into every time-of-day set but never draws such a
  dome for a world; its scene renderer's sky step (`0xD528E0`) only draws the unlockables viewer's
  background. The mod wraps that step and draws the sky after it: one triangle over the whole
  screen at the far plane, behind whatever the world has drawn, with the game's own Direct3D device
  (noted when the game makes it) and its own small shader, compiled when first needed. Everything
  it changes on the device is put back. It draws nothing in Armageddon's own worlds.
  `Brightness=` sets how bright the sky is; the mod reads it again whenever `rfa_mods.ini` is
  saved, so it can be tuned while playing.
- **The clouds** (`Clouds=`) follow Guerrilla's cloud shader: Guerrilla's cloud model (an overhead
  dome and a ring along the horizon), two textures of two cloud layers each, the layers'
  strengths, light colours, rim light and drift speeds from the time of day. In front of them, a
  ring of far mountains: Guerrilla's backdrop, tinted by the time of day's mountain and haze
  colours as its backdrop shader does. The model and textures (the backdrop's too) are files in
  `mods\guerrilla_world\`, made from your own copy of Guerrilla when the ported world is built
  (they are never shipped); without them the sky is drawn alone.
- **The clock**: Armageddon still has Guerrilla's game clock, and the time of day still follows
  it, but the game sets its speed to 0 at start-up, so the day never moves. The mod runs it at
  `ClockSpeed=` game seconds a real second (48 by default: a day in half an hour, also read again
  when `rfa_mods.ini` is saved) and gives the clock its old speed back in any other world.
- **The view distance**: the game keeps a world's zones loaded only within a radius of the player,
  and each zone's far-away ground comes with it; Armageddon's radius covers little more than the
  zones next to you (its levels are walled canyons), so in an open world the ground stops not far
  off. The game streams zones and their high-detail sub-zones on two grids, each tracking only
  about 35 containers at once. The mod wraps the game's radius setter (`0x6B3100`) and widens the zones' radius to `StreamRadius=`
  metres (500 by default and at most, 5 × 5 zones; also read again when `rfa_mods.ini` is saved)
  and puts the game's own back elsewhere.
- **The light**: Armageddon lights surfaces only with lamps (it draws them into a light buffer
  that each surface is then multiplied by); the time of day's ambient light never reaches the
  ground, so a ported world without lamps is dark. The mod adds light into that buffer right after
  the game's light pass (`Light=`; the pass is `0xEF8BB0`): the sky's light, the time of day's
  ambient from above and below blended by which way each surface faces (times `Ambient=`), and the
  sun's light along its path over the day, by night the moon's, from the time of day's light
  colour (times `Sun=`). Both are read again when `rfa_mods.ini` is saved. The clouds are lit from
  the same direction.
- **The zone size**: the ported world keeps Guerrilla's 511 m zones (`ZoneSize=`), so the game's
  16 × 16 grid of zones is Guerrilla's whole 8 km map. The game already knows that size (it gives
  it to worlds whose names start `mpl_`, left over from Guerrilla's multiplayer maps); the mod
  gives it to the worlds in `Worlds=` (wrapping `0x6C0D60`).
- **The physics world**: Armageddon's physics world reaches only 2295 m from the middle (nine of
  its own 255 m zones; the constants `0xFB5DC0` and `0xFB5DC4`), and anything beyond it drops out
  of the physics, so at start-up the mod widens it to nine of ours (4599 m). That is for every
  world, since the game makes it only once.

The world itself is built by `editor\GuerrillaWorld.exe` (code in `src\editor\GuerrillaWorld\` and
`src\editor\RfaFormats\Guerrilla\`), from the player's own copy of Guerrilla. It writes the world
`rfg` into `overrides\build\pc\cache\` and the sky's files into `mods\guerrilla_world\`.

### guerrilla_hud

Red Faction: Guerrilla's HUD in the worlds ported from Guerrilla (`[GuerrillaHud] Worlds=`),
Armageddon's own everywhere else. Both games build their HUD from the same kind of interface
documents (elements driven by a Lua script).

- Armageddon loads its HUD once at start-up and keeps it through every world. The mod wraps the
  HUD's data update (`0x57BC30`) and, when a listed world is entered, loads Guerrilla's documents
  (`Documents=`) the way the game loads its own (`0x563990`), and unloads them when another world
  is.
- Guerrilla's documents and pictures load in Armageddon as they are, but their scripts read game
  data Armageddon doesn't have, so each document gets a script of our own (`mods\guerrilla_hud\`,
  from `src\mods\guerrilla_hud\`) that drives Guerrilla's elements from Armageddon's data
  (`health_hud`) and fades out the parts of Armageddon's HUD it replaces while it is loaded.
- The mod puts each document in the interface's draw list after loading it, as the console's
  `vint_doc_load` does (a document in no draw list is loaded but never drawn), and keeps it there
  only while the HUD itself runs (the interface's draw pass, `0x564FF0`, is wrapped too), so
  nothing of it stays on loading screens or in the menus.

`editor\GuerrillaHud.exe` (code in `src\editor\GuerrillaHud\` and
`src\editor\RfaFormats\Interface\`) makes the documents from the player's own copy of Guerrilla:
- It renames Guerrilla's pictures (`rfg_...`, since both games use some of the same names for
  different pictures), gathers the textures each document needs into one texture file, packs each
  document with its script into containers as Armageddon packs its own, and adds them to the
  game's lists of documents and containers (`interface.vpp_pc\vint_doc_containers.asm_pc` and
  `misc.vpp_pc\rfg.vint_proj`), all in `overrides\`.
- Guerrilla's picture sheets (where each picture sits on its texture) are added to a copy of the
  game's sheet table under a new name, `guerrilla_hud_sheets.xtbl`, which the interface project
  names in place of the game's. The game reads the tables it needs at start-up from
  `table.vpp_pc` held in memory, past the overrides, so its own `bitmap_sheets.xtbl` can't be
  overridden, but a table under a new name is looked up by name.
- A picture that is a whole texture (the blood) gets a sheet of its own there too, holding just
  that picture, since the game finds pictures reliably only through sheets.
- `--remove` takes all of it away again. Since that sheet table now holds the pictures of
  Armageddon's own HUD too, it must never be deleted alone.

## The editor

The code is in `src\editor\`: `RfaFormats` reads and writes the formats (no UI, reusable by other
tools) and `RfaEditor` is the editor:
- `MainWindow`: the layout and explorer.
- `Documents\`: the tabs (file preview, table editor, level editor).
- `Level\`: the level view's camera, gizmo, scene, grid, kinds of objects, inspector parts, level
  geometry and the ground's painter.
- `Render\`: the Direct3D 11 view.
- `Theme.xaml`: the dark look of every control (with `Theme.cs` for code).
- `Icons.cs`: the icons (characters of the Segoe MDL2 Assets font that comes with Windows) and
  the kinds of files.
- `NumberField.cs`: the draggable number box.

The editor's builds name no folder of the PC they were built on: `src\editor\Directory.Build.props`
writes `/_/` in place of the project's folder in every path the compiler stores (source files and
`.pdb`), and `tools\package.ps1` checks for that. The editor is built 64-bit; a 32-bit one runs
out of memory on big levels.

**Tables**: the game's tables describe their own fields (the `<TableDescription>` Volition's own
table editor used), and the table editor is built from that description rather than from
anything hard-coded. Saving writes back only what changed; the rest is written back byte for
byte, in the game's own formatting.

**Containers**: most textures and zones are inside `.str2_pc` containers, which the game streams
in whole. To change one, the editor rebuilds the container into `overrides\`, compressed the way
Volition's tools did, and updates the container's entries in the asset lists (`.asm_pc`) that
tell the game how big it is, including the group entries (such as `1_c0812`) that the game sets
memory aside by. Many zones are in the game twice, identically (in a `zonescript_*.vpp_pc` archive
and in their level's container); the game may read either, so the editor saves every copy.

**Saving zones**: the game's object table (the zone's relation table) is built again on saving, as
the game's tools do. Deleting an object also removes its destruction links to the buildings next
to it (the game's table of which pieces hold onto which, renumbered as the game's own files number
it). A copied object is a whole breakable object, with everything attached and the destruction
links holding the pieces together, each with a handle of its own.

The add-objects freeze (found in the game on 2026-10-31): with objects added to `world_08_03` the
game froze, stuck in its breakable objects' destruction links. The zone had grown, but a group
entry of the asset list still gave its old size, so the end of the zone ran into other memory.
Fixed (saving again mends a list saved before): a new game with four copied tankers in
`world_08_03` then loaded and ran, with all four copies in place.

**Speed**: the level view draws objects in batches (every object of one kind with one texture, in
one 64 m square of the map, joined into one mesh), the level geometry and the zones around joined
per texture, and each model's parts joined per texture. A zone is a few hundred pieces for WPF to
draw each frame instead of over ten thousand (`world_08_02`: 523, was 10,201). A click still picks
the one object under the mouse (found by the triangle hit), and an object that moves is taken out
of its batch and drawn on its own. The log says how long each part of a zone took to load.

**Level geometry and foliage**: for the first mission's `world_08_02`, the stitch list places 865
pieces including the Terraformer, its platform and the ring walls. Foliage (in 11 zones) is scattered exactly
as the game does it and set down on what is below; pieces with no surface in their 1 m cube are left
out, as the game does: 336 of 495 are drawn in `world_08_02`.

**Direct3D view**: draws the level with Direct3D 11 on the graphics card instead of WPF, the first
step of [renderer-plan.md](renderer-plan.md). It shows the same things from the same camera, worked out for
every pixel, and only draws again when something changed (a frame takes the editor 2–3 ms). It uses the Vortice
libraries (MIT; see [third-party-notices.txt](third-party-notices.txt)).

**Game light** uses the game's own formulas, read from its shaders: lamps (spot, omni, tube and
ambient, with their colours, fades and cones), the environment box's ambient light, fog and
exposure, the game's tone curve, and materials (normal, specular and glow maps, colour multiplier,
alpha and cut-outs, the Legion's alien skin shader). The game gives shadows to 561 lamps; the
editor casts them as the game does.

**Handles**: handles and positions in the editor are the game's own, so a building picked in the
game (`0x820300F9`, `str_sur_edf_qm_01` at 90.4, -4.7, 1306.6) is the same object at the same place
in the editor.

What the editor still lacks, measured against Volition's own tools, is in
[editor-roadmap.md](editor-roadmap.md).

## Writing a mod

Create `src\mods\<name>\<name>.cpp`, define `RFA_MOD_NAME`, include `rfa_mod.h`, and export:

```cpp
extern "C" __declspec(dllexport) void __cdecl RFAMod_Init() { /* install hooks */ }
```

- `rfa_mod.h` provides `rfa::Log`, `rfa::IniInt`/`IniString`, `rfa::HookImport` (import table),
  `rfa::ReplaceFunction` (send a game function to a C++ function with the same signature) and
  `rfa::HookFunction` (the same, keeping a pointer to call the original).
- `rfa_game.h` has `rfa::game::IsSupportedBuild()` (check it before touching game code) and the
  game's Lua API.
- Write hooks in plain C++: match the game function's calling convention (`__cdecl`, or
  `__fastcall` for `this`-in-`ecx` methods without stack arguments) instead of writing assembly.
- Two `HookFunction`s on one address don't chain, so check no other mod hooks it first.
- Export `RFAMod_SettingChanged(section, key)` to hear about settings changed from the game's
  menus.
- Add a `cl` line for it to `build.cmd`. Disable a mod with `<name>=0` under `[Mods]`.

## Building, packaging and releasing

`build.cmd` needs Visual Studio with the C++ desktop workload, and the .NET 10 SDK for the editor
(skipped if it's missing). It builds the launcher, loader and mods 32-bit (the game is a 32-bit
process) into `bin\`, copies `src\overrides\` into `bin\overrides\`, and copies
`config\rfa_mods.ini` and `config\console.ini` to `bin\` only if there are none there yet. It
builds the editor and the two Guerrilla tools 64-bit into `bin\editor\`. The build turns off the
.NET SDK's usage reporting and its first-run web certificate.

`tools\package.ps1` makes the copy to give others, in `dist\RFA-Mod\` and as
`dist\RFA-Mod-<date>.zip`:
- It holds the launcher, the loader, the mods (and their own scripts, from `src\mods\`; never
  files made from your copy of Guerrilla), the mod's own overrides (from `src\overrides\`, not the
  files you edited), the editor (without its `.pdb` debugging files), the README with the documents
  it links to (named one by one: `docs\` also holds private notes) and the licence, with the
  settings templates from `config\` (so `GameDir=` is empty).
- Your own `rfa_mods.ini`, `console.ini`, `video_settings.cfg`, `logs\`, `link\` and edited
  overrides stay out.
- Every file it packs is checked for anything naming your PC (your user folder or name, this
  project's folder, your game folder), and nothing is packaged if one does.
- `-Build` runs `build.cmd` first; `-NoEditor` leaves the editor out; `-Version v0.1.0-beta` makes
  a release: the zip is named `RFA-Mod-v0.1.0-beta.zip` and its `version.txt` says which commit it
  was built from.

**Releases** on GitHub are built by GitHub itself (`.github\workflows\release.yml`): pushing a tag
that starts with `v` builds that commit on a clean Windows machine with `build.cmd`, packs it with
`tools\package.ps1 -Version <tag>` and publishes the zip as a release, a pre-release when the tag
has a `-` in it (`v0.1.0-beta`). Its text comes from `docs\releases\<tag>.md`. To release:

```
git tag -a v0.3.0-beta -m "RFA-Mod v0.3.0-beta"
git push origin v0.3.0-beta
```

## Checking the game files are untouched

```
powershell -ExecutionPolicy Bypass -File tools\game-integrity.ps1 -Mode Verify
```

This compares every file in the game folder with the SHA-256 snapshot of the publicbeta install,
`reference\build-1932726-publicbeta\game_baseline.csv`. `reference\build-1291145\game_baseline.csv`
is the default branch, taken before the first modded launch. Files the beta writes itself
(`console_commands.txt`, `rfg_last_level_pc.txt`, `Load_Successful.txt`) are listed as `GAME`, not
counted as changes.
