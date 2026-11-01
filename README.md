# RFA-Mod

Mods and a level editor for the Steam version of **Red Faction: Armageddon**. RFA-Mod fixes and
improves the PC version and opens up the game's files for modding, and it never changes your game
folder: the mods run in the game's memory while it plays. It's an unofficial fan project, for
single-player.

## What you get

- **Borderless fullscreen** (`borderless`): no frame, covers your monitor, and alt-tab no longer
  minimizes the game (it still pauses, as always). The Windows key works while you play.
- **A wider field of view** (`fov`): the game's own is 45° vertical. Set yours in `rfa_mods.ini`
  or with a new **Field of View** slider in Options → Gameplay (45–170). Works on foot, in
  vehicles and in exo-suits; cutscenes keep their framing.
- **Video settings that stick** (`video_settings`): the game forgot its resolution and quality
  settings every launch. Now they're kept in `video_settings.cfg`.
- **A better developer console** (`console`): easy to read, with a dark backing, bigger text and
  a clear caret; it keeps all its output (not just the last 256 lines) for you to scroll back
  through, and it gets out of the way of the render debug panel. Console settings apply
  automatically from `console.ini`.
- **Red Faction: Guerrilla's world, a first look** (`guerrilla_world`, beta): Guerrilla's whole map
  as a new world, with its sky, clouds, day and night, and a longer view. You build it from your
  own copy of Guerrilla.
- **Guerrilla's HUD, a first piece** (`guerrilla_hud`, beta): Guerrilla's health meter and blood
  in that world, in place of Armageddon's health ring, red screen edges and compass bar.
- **An editor** (`editor\RfaEditor.exe`): browse and extract the game's files, edit tables, replace
  textures, view models and animations, and edit levels.

## What you need

- The Steam version of the game on the **publicbeta** branch (build 1932726), an internal build
  that still has the developer console. The mods that change the game's code are made for it; on
  any other build the game runs normally and they leave it alone.
- Windows 10 or 11.
- For the editor and the Guerrilla tools: the 64-bit (x64)
  [.NET 10 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/10.0).

## Install

1. In Steam, right-click the game → Properties → Game Versions & Betas, and pick **publicbeta**.
   Steam swaps in the beta's two exe files (about 10 MB); nothing else changes.
2. Download the latest zip from [Releases](https://github.com/FragsAreUs/RFA-Mod/releases) and
   unzip it, either:
   - **into a folder of your own** (for example `D:\Games\RFA mods`), then set `GameDir=` in its
     `rfa_mods.ini` to your game folder, the one with `RedFactionArmageddon_DX11.exe` (Steam, GOG,
     or anywhere else). Or start `editor\RfaEditor.exe` once and choose the folder there, which
     fills `GameDir=` in for you; or
   - **into the game folder**, and leave `GameDir=` empty. RFA-Mod only adds files there; it never
     changes the game's own.

## Play

1. With Steam running, start `RFAModLauncher.exe`.
2. The first time, go to Options → Display, choose **Windowed** and your monitor's native
   resolution (for example 1920x1080; other resolutions are stretched), then apply and keep the
   changes. That window becomes borderless fullscreen. In the game's own Fullscreen mode,
   `borderless` leaves it alone.
3. Optional: to start it from Steam, open the game's Properties → Launch Options and enter the
   launcher's path in quotes, followed by `%command%`:
   ```
   "D:\Games\RFA mods\RFAModLauncher.exe" %command%
   ```
   To play unmodded, clear the launch options or start the game normally.

## Settings

- **`rfa_mods.ini`**, next to the launcher, holds every setting, each explained in the file:
  - `[Launcher]`: where your games are (`GameDir=`, and `GuerrillaDir=` for the Guerrilla tools);
  - `[Loader]`: crash reports and overrides;
  - `[Mods]`: set a mod to `0` to turn it off;
  - one section for each mod: `[Borderless]`, `[FOV]`, `[Console]`, `[GuerrillaWorld]` and
    `[GuerrillaHud]`.
- **`console.ini`**: console settings applied for you, as `<console name>=<value>`. `[Startup]`
  runs once when the game starts, and `[Gameplay]` each time a level, save or checkpoint loads. By
  default it hides the beta's performance text.
- **`overrides\`** holds files the game uses instead of its own (the FOV slider, the Guerrilla
  world and HUD, and whatever you save from the editor). See
  [src/overrides/README.md](src/overrides/README.md).

## Try the Guerrilla world (beta)

1. Install **Red Faction: Guerrilla Steam Edition** (Re-MARS-tered's files aren't read yet). You
   need about 1 GB free where RFA-Mod is.
2. In `rfa_mods.ini`, under `[Launcher]`, set `GuerrillaDir=` to Guerrilla's folder (for example
   `D:\Games\Red Faction Guerrilla`). Or give the folder to each tool as its argument.
3. Run `editor\GuerrillaWorld.exe` (two or three minutes). It writes the world `rfg` into
   `overrides\`, with Guerrilla's ground, rocks, buildings and props (which break, and which the
   Nano Forge rebuilds), and its sky into `mods\guerrilla_world\`. `--props none` leaves out the
   buildings and props; `--zones 11-12,10-11` builds only that block of zones (columns, then rows).
4. Run `editor\GuerrillaHud.exe` for Guerrilla's HUD (a few seconds).
5. Start the game, open the console with the `` ` `` key (from the main menu is fine) and type
   `level rfg_12_11`, or any zone's name, `rfg_<column>_<row>` (such as `rfg_04_10`, a town).

Things to know:
- Run both tools again after updating RFA-Mod, and `GuerrillaHud.exe` again after changing its
  script.
- `editor\GuerrillaHud.exe --remove` takes the HUD away. Only remove it that way: one of its files
  also holds Armageddon's own HUD pictures.
- Nothing made from Guerrilla is ever shipped; it stays on your PC.
- `guerrilla_world` widens the game's physics for every world, the campaign included. Set
  `ZoneSize=0` under `[GuerrillaWorld]` if you don't use the Guerrilla world.
- To take the world away, remove the HUD first (`--remove`), then delete `rfg_*`,
  `zonescript_rfg.vpp_pc`, `rfg.gtodx`, `table.vpp_pc\territories.xtbl` and
  `misc.vpp_pc\bitmaps_pc` from `overrides\build\pc\cache\`.

## The editor

- Start `editor\RfaEditor.exe`. It only reads the game folder; what you change is saved to
  `overrides\`.
- **Explorer**: every archive and container, search, preview, Extract, and **Copy to overrides**.
- **Table editor**: double-click an `.xtbl` file to edit its entries with proper fields.
- **Textures**: view, export to PNG or DDS, and replace with an image of the same size.
- **Models**: view in 3D, with skeletons and animations, and export to OBJ.
- **Level editor**: double-click a zone to see it as the game draws it, then move, rotate, add,
  copy and delete objects, with undo.
- **With the running game**: send the game's camera to an object, or follow the game's camera
  in the editor.
- The full walkthrough is in **[docs/editor-guide.md](docs/editor-guide.md)**.

## The developer console

- Press `` ` `` / `~` (left of 1) to open or close it; it toggles when you let go of the key.
- It's laid out like Quake 3's console. What you type is the big line at the bottom, with an
  amber caret. Commands you ran show in amber, errors in red, long lines wrap, and a line printed
  many times in a row shows once with a count.
- Just above the line you type is the build stamp: the game's version, Steam build, internal name
  and the day it was built, and the RFA-Mod version you have.
- While it's shut, the last few lines it printed show at the top left for 3 seconds.
- Scroll back with the mouse wheel or Page Up / Page Down (hold Shift for one line at a time),
  Ctrl+Home for the oldest line, and End to come back to the newest. Up / Down recall earlier
  commands.
- The mouse works while it's open: drag the scroll bar on the right (or click its track to jump
  there), or hold the middle button and move up or down to scroll, faster the further you go (a
  quick middle click keeps it scrolling until your next click). Clicking the line you type moves
  the caret there.
- It keeps 10000 lines, so long lists such as `search <text>` (finds commands) can be read in
  full. Everything it prints is also in `logs\console.log`, and `print_to_file` writes every
  command to `console_commands.txt`.
- Its height, text sizes, darkness, history length, the notify lines and the build stamp are
  under `[Console]` in `rfa_mods.ini`.
- The render debug panel (`` ` `` + X) takes the mouse while the console is open. While the panel
  is up, the console moves to the bottom of the screen, below the panel, so you can click its
  options and still type commands.
- `script <file>` runs a text file of commands, and `cam_fov <degrees>` sets the camera FOV.
- `slew_mode` (or `` ` `` + S) frees the camera. Q then picks what it looks at and shows its
  details (class, handle, model, position), the same handles the level editor shows.
- It's single-player only: some commands refuse to run in multiplayer, and VAC still applies
  online.

## When something goes wrong

- `logs\rfa_mods.log` has everything the launcher, loader and mods did (the run before is kept
  as `rfa_mods.prev.log`).
- After a crash, the log ends with a `CRASH:` block, and `logs\crash-*.dmp` is written next to it.
  The editor's log is `logs\editor.log`.
- Please attach those, and the release's `version.txt`, to an
  [issue](https://github.com/FragsAreUs/RFA-Mod/issues).

## Build from source

- You need Visual Studio with the C++ desktop workload. The editor also needs the .NET 10 SDK; the
  build skips the editor without it.
- Run `build.cmd`. Everything goes to `bin\`. Your `bin\rfa_mods.ini` and `bin\console.ini` are
  copied from `config\` only if they don't exist yet.
- To make a zip to share (with the settings templates, never your own files):
  ```
  powershell -ExecutionPolicy Bypass -File tools\package.ps1
  ```
  - `-Build` runs `build.cmd` first, and `-NoEditor` leaves the editor out.
  - `-Version v0.3.0-beta` makes a release zip with a `version.txt`.
  - It refuses to package anything that names your PC.
- Releases: pushing a tag that starts with `v` makes GitHub build, package and publish that
  commit, with the notes from `docs\releases\<tag>.md`. A tag with a `-` makes a pre-release.

## For developers

- **[docs/how-it-works.md](docs/how-it-works.md)** has the inside story:
  - how the mod gets into the game, and the project layout;
  - the loader's overrides and Lua bridge;
  - each mod's hooks and game addresses;
  - the editor's internals;
  - writing a mod of your own;
  - building, packaging and releasing;
  - checking the game folder is untouched.
- [docs/editor-roadmap.md](docs/editor-roadmap.md) is what the editor still lacks, and
  [docs/renderer-plan.md](docs/renderer-plan.md) is the plan for drawing levels like the game.

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
