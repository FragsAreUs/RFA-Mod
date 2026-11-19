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
  through, shows the game's hidden debug output and RFA-Mod's log, takes pasted commands, and gets
  out of the way of the render debug panel. Console settings apply automatically from
  `console.ini`.
- **Red Faction: Guerrilla's world, a first look** (`guerrilla_world`, beta): Guerrilla's whole map
  as a new world, with its sky, clouds, day and night, and a longer view. You build it from your
  own copy of Guerrilla.
- **Guerrilla's HUD, the first pieces** (`guerrilla_hud`, beta): Guerrilla's health meter and blood
  in that world, in place of Armageddon's health ring, red screen edges and compass bar; its
  reticles and ammo count, in place of Armageddon's reticle and ammo ring (Armageddon's Nano Forge
  meter stays); its weapon select, in place of Armageddon's weapon selector; and its minimap (the
  map around you turning with the camera, the district's name) and its world map, which Tab opens
  and closes.
- **Guerrilla's people, the first ones** (`guerrilla_people`, beta): in the Guerrilla world you play
  Mason, in his own coat, with Guerrilla's animations and Armageddon's extra moves (dodging, the
  Nano Forge); and the EDF are there: troopers, lieutenants, elites and heavy elites, who patrol
  Guerrilla's routes, fight you with Guerrilla's guns, drop them when they die and fall as
  ragdolls. You build them from your own copy of Guerrilla too.
- **Guerrilla's vehicles** (`guerrilla_vehicles`, beta): Guerrilla's cars, trucks, buses, ATVs,
  rovers, EDF staff cars, scouts and APCs, Marauder raiders and more, which you can spawn and drive
  (W, S, Space and A/D, Guerrilla's keys). You get in and out the way Mason does in Guerrilla
  (opening the door, climbing in, quick and diving exits, moving between seats with the middle
  mouse button, getting out with E), with his own animations: in the Guerrilla world you play
  Mason. They handle, dent, burn and
  blow up as in Guerrilla, with its own fire, smoke, exhaust and explosions. You build them from
  your own copy of Guerrilla too.
- **Guerrilla's guns, the first ones** (`guerrilla_gear`, beta): the EDF's assault rifle, enforcer,
  gauss rifle and rail driver and the Peacekeeper, with Guerrilla's own models, magazines, damage,
  spread, ammo, muzzle flashes, tracers and explosions (the rail driver's scope sees through walls,
  as in Guerrilla), and Guerrilla's own player animations for them: you stand, walk, run, aim, fire
  and reload with them as Mason did, and Armageddon's own guns keep Darius's. You give yourself one from the Spawn menu. You build them from your own copy of
  Guerrilla too.
- **Guerrilla's alert level** (`guerrilla_campaign`, beta): in the Guerrilla world the EDF's alert
  rises while they see you, when you kill their people, wreck their vehicles or damage their
  buildings, and falls again once you get away. The light by the minimap shows it: green, yellow,
  orange, red.
- **In-game menus** (`rfa_loader`, drawn with [Dear ImGui](https://github.com/ocornut/imgui)):
  press F7 (or open the console) and a bar along the bottom of the screen holds RFA-Mod's menus
  (another key: `MenuKey=` under `[Loader]`). Its first
  is **Spawn** (`spawner`, in `rfa_qol`), with four windows, each with a filter: **Vehicles**, every
  vehicle the game has, Guerrilla's too, dropped in front of you even if the area hasn't loaded it;
  **Items**, every item the game can drop; **Weapons**, given to you; and **Spawned**, the vehicles
  you spawned (the oldest goes after eight), to remove one or all, and how many of the game's
  vehicle slots are free. **Limits** (`limits`, in `rfa_qol`) shows how full the engine's fixed
  tables are (vehicles, animation sets, triggers, explosions, projectiles, ...) and which a mod has raised; the
  game drops what doesn't fit without a word. **Graphics** (`graphics`, in `rfa_qol`) has sliders
  and switches for the Guerrilla world's picture (exposure, sky, sun, fog, sun shafts, sharpening,
  clock speed, tone mapping, shadows, ...): let go of a slider and the world changes within a second,
  saved in `rfa_mods.ini`. Other mods can add menus of their own (see
  [rfa_menu.h](src/common/rfa_menu.h)). The spawner is based on Venom_Intellect's vehicle_spawner
  and their notes on the game.
- **Options → Experimentals**: a new page in the game's Options menu, from the main menu or the
  pause menu, for RFA-Mod's own settings. For now it holds the Guerrilla world's switches: the
  sun's shadows and shafts, fog, blowing dust, Guerrilla's particles and tone mapping, colour
  correction, sky brightness and clock speed. Each changes as you pick it and is saved in
  `rfa_mods.ini`.
- **More memory** (the launcher): the game gets 4 GB to work in instead of 2, which the Guerrilla
  world needs. The launcher starts it from a copy of its program, kept in `large_address\`, with
  the mark Windows looks for; your game folder isn't touched.
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

## Update

1. Unzip the new version over your old one. When Windows asks about files that are already there,
   replace them all except your own `rfa_mods.ini` and `console.ini`.
2. Start `RFAModLauncher.exe`. It adds the settings the new version brings to your two files, each
   with its explanation, and keeps every value you set. Your files as they were are kept as
   `rfa_mods.ini.bak` and `console.ini.bak`, and `logs\rfa_mods.log` lists what was added.

A setting you delete from either file comes back with its default the next time the launcher
starts, so to change what one does, change its value.

Updating from v0.4.9-beta or older: borderless, FOV, video settings and the console are now one
file, `mods\rfa_qol.dll`. You can delete the old `borderless.dll`, `console.dll`, `fov.dll` and
`video_settings.dll` from `mods\`. If you leave them, they're skipped, and your settings for them
work as before.

Updating from v0.6.6-beta or older: the nine Guerrilla mods are now one file,
`mods\rfa_guerrilla.dll`. You can delete the old `guerrilla_campaign.dll`, `guerrilla_gear.dll`,
`guerrilla_hud.dll`, `guerrilla_kiosks.dll`, `guerrilla_people.dll`, `guerrilla_save.dll`,
`guerrilla_vehicles.dll`, `guerrilla_voices.dll` and `guerrilla_world.dll` from `mods\`, but keep
the `guerrilla_...` folders next to them: they hold what the Guerrilla builder made from your copy of
Guerrilla. If you leave the old files, they're skipped, and your settings for them work as before.

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
  - `[Launcher]`: where your games are (`GameDir=`, and `GuerrillaDir=` for the Guerrilla tools),
    and `LargeAddressAware=` (set it to `0` to start the game's own program, with 2 GB);
  - `[Loader]`: crash reports and overrides;
  - `[Mods]`: set a mod to `0` to turn it off. `borderless`, `fov`, `video_settings`, `console`
    and `spawner` share one file, `mods\rfa_qol.dll`, and the nine Guerrilla mods
    (`guerrilla_campaign`, `guerrilla_gear`, `guerrilla_hud`, `guerrilla_kiosks`,
    `guerrilla_people`, `guerrilla_save`, `guerrilla_vehicles`, `guerrilla_voices` and
    `guerrilla_world`) share `mods\rfa_guerrilla.dll`, but each still turns off on its own;
  - one section for each mod: `[Borderless]`, `[FOV]`, `[Console]`, `[GuerrillaWorld]`,
    `[GuerrillaHud]` and `[GuerrillaVehicles]`.
- **`console.ini`**: console settings applied for you, as `<console name>=<value>`. `[Startup]`
  runs once when the game starts, and `[Gameplay]` each time a level, save or checkpoint loads. By
  default it hides the beta's performance text.
- **`defaults\`** holds this version's own copies of both files: the launcher takes new settings
  from them. Leave them as they are; each update replaces them.
- **`overrides\`** holds files the game uses instead of its own (the FOV slider, the Experimentals
  page, the Guerrilla world and HUD, and whatever you save from the editor). See
  [src/overrides/README.md](src/overrides/README.md).

## Try the Guerrilla world (beta)

1. Install **Red Faction: Guerrilla Steam Edition** (Re-MARS-tered's files aren't read yet). You
   need about 1 GB free where RFA-Mod is.
2. Run `editor\GuerrillaBuilder.exe`. It finds Guerrilla in the Steam library Armageddon is in;
   if it doesn't, click **Browse...** beside Red Faction: Guerrilla and choose Guerrilla's folder
   (for example `D:\Games\Red Faction Guerrilla`). It keeps the folder in `rfa_mods.ini`
   (`GuerrillaDir=` under `[Launcher]`).
3. Leave everything ticked and click **Build** (a minute or more). From your copy of Guerrilla it makes:
   - the world `rfg`, with Guerrilla's ground, rocks, buildings and props (which break, and which
     the Nano Forge rebuilds) and its sky;
   - Guerrilla's HUD;
   - its vehicles, and the guns ported so far;
   - its EDF soldiers, who patrol the world;
   - Mason's coat, whose tails swing as he moves and blow in Guerrilla's wind.

   All of it goes into `overrides\` and `mods\` in your RFA-Mod folder.
4. Start the game, open the console with the `` ` `` key (from the main menu is fine) and type
   `level rfg_12_11`, or any zone's name, `rfg_<column>_<row>` (such as `rfg_04_10`, a town).
5. To drive: with the console open, click **Spawn**, then **Vehicles**, in the bar at the bottom
   of the screen; pick one (type `taxi`, `ATV`, `Bluecrab`, `Staffcar`, `Raider`, ... in the box to
   find it) and click Spawn. Close the console, walk up to its driver side and get in with E. W accelerates, S brakes
   and reverses, Space is the handbrake, A and D steer, E gets you out, the middle mouse button moves
   you to another seat.

Things to know:
- After updating RFA-Mod, run `editor\GuerrillaBuilder.exe` again: it ticks what needs building
  again, so click **Build**. Build the HUD again after changing its scripts.
- **Remove...** takes the ticked parts away again, with what's built on them: the world takes
  everything with it, the weapons take the soldiers (who carry them), the soldiers take the cloth.
  Only take the HUD away there: one of its files also holds Armageddon's own HUD pictures.
- To build only some of a part, use the builder from a command prompt, for example
  `editor\GuerrillaBuilder.exe build world --zones 11-12,10-11` (that block of zones: columns,
  then rows), `build world --props none` (no buildings or props), `build vehicles --vehicles
  Col_Taxi_1,Min_ATV_1` or `build weapons --weapons m16,rail_driver`. `editor\GuerrillaBuilder.exe
  help` lists everything. It's a window program, so start it with `start /wait` in a command
  prompt, or add `| Out-Host` in PowerShell, for the prompt to wait for it.
- Guerrilla's minimap sits at the bottom left: north turns with the camera, the arrow in the middle
  is you, and it zooms out as you drive faster. Page Up and Page Down zoom it in and out, from 40
  to 80 metres around you (`MinimapZoomIn=` / `MinimapZoomOut=` under `[GuerrillaKeys]`). Press Tab (`Map=` under `[GuerrillaKeys]`, or F7 >
  Guerrilla > Keys) for the world map, with you on it, and Tab again to close it; the game pauses
  while it's open. The minimap shows each zone's own detailed picture once it has loaded, a moment
  after you come near.
- Dots on the minimap show the people and vehicles near you, as in Guerrilla: the EDF (red) and the
  Marauders (purple) only while they're fighting, or all of them once you buy the Personnel
  Detector upgrade at a safehouse; the Red Faction (yellow) while the alert level is above green;
  every vehicle, coloured by who's in it. Guerrilla's radar sweep runs down the minimap: each dot
  is brightest just after the line passes it and fades until the line comes round again, and a ring
  grows from each person it passes. To keep the dots showing between sweeps, raise `RadarMinAlpha=`
  under `[GuerrillaHud]` (0 to 255; Guerrilla's is 0).
- Guerrilla's tool tips pop up as things first happen: when you open a weapon locker or an ammo
  crate, stand at an upgrade station, pick up salvage, get into a car, find a radio tag, or raise the
  alert. Each shows as often as in Guerrilla (most once). Some pause the game until you press Enter
  (`TipContinue=` under `[GuerrillaKeys]`). `ToolTips=0` under `[GuerrillaHud]` turns them off, and
  deleting `mods\guerrilla_hud\tool_tips_played.txt` shows them again.
- Guerrilla's reticle sits in the middle, turning red over an enemy and green over a friend, with
  the rounds in your magazine beside it and the rest in the box (the infinity sign where ammo is
  unlimited), which turn red when you should reload. Guerrilla's weapons have their own reticles
  (another while you zoom, where Guerrilla has one); Armageddon's get Guerrilla's assault rifle
  reticle. `Reticles=0` under `[GuerrillaHud]` brings Armageddon's back.
- When you change weapons, Guerrilla's weapon select opens at the bottom right: your four weapons
  on plates in a cross (key 1 is the left one, then clockwise), each with its ammo, the chosen one
  lit with its name. Each weapon shows its own picture: Guerrilla's weapons Guerrilla's,
  Armageddon's theirs. `WeaponSelect=0` brings
  Armageddon's selector back.
- The vehicles have no sounds, lights or turrets yet, and their glass shows too bright. In
  Armageddon's own levels Darius gets in with Mason's animations, so his hands may not land on the
  wheel and door. Tanks and the bulldozer aren't built yet, and walkers can't be got into yet.
  `guerrilla_vehicles` must be on: besides the keys, it makes room in the game for the vehicles
  (Armageddon holds only 40, and uses 24 itself) and their animation sets. The console's own
  `drop_car` only makes vehicles the area has loaded; the Vehicles window loads the one you pick
  first. The game makes no vehicle while you're in one. Remove ones you're done with in Spawned.
- The driving, getting-out and seat keys are under `[GuerrillaVehicles]` in `rfa_mods.ini`. A key
  you have set for one of them yourself is kept. E also gets you out of Armageddon's own vehicles
  where the game lets you (as a controller's button already does). `Dents=0` there keeps the
  vehicles from denting.
- To try Guerrilla's weapons: with the console open, click **Spawn**, then **Weapons**, type the
  weapon's name and click Give:
  - guns: `rfg_m16` (the assault rifle), `rfg_enforcer`, `autoblaster` (the Peacekeeper),
    `gauss_rifle`, `rfg_rail_driver`, `edf_pistol`, `sniper_rifle`, `rfg_shotgun`, `rfg_nano_rifle`;
  - launchers and explosives: `rfg_rpg_launcher`, `thermobaric_rockets`, `rfg_charge_placer` (the
    remote charges), `mine_placer` (proximity mines), `singularity_bomb`, `propane_tank`;
  - Guerrilla's own: `grinder`, `arc_welder`;
  - melee: `rfg_sledgehammer`, `sledge_upgrade_1` (Stonebreaker), `sledge_upgrade_2` (Shattermaster),
    `gutter`.

  They fire with Guerrilla's own shot sounds and show Guerrilla's pictures and reticles (other
  sounds and impact effects aren't there yet), and the melee button with any of them in your hands
  bashes as in Guerrilla. Throw remote charges with fire and set them all off with alt fire or reload; two
  can be out at once, as when Guerrilla starts. Swing the hammer with fire (side) or alt fire
  (overhead). With one of them in your hands
  you move as Mason did (Guerrilla's player animations); take up one of Armageddon's guns and you move
  as Darius again.
- In the Guerrilla world Mason always has his sledgehammer, as in Guerrilla: you're given it (in the
  weapon select's fourth place) when you have none, and the action button won't swap it for a gun on
  the ground; hold another gun to swap that one.
- **Guerrilla's upgrades:** press F7 and open **Guerrilla → Upgrades**. Buy with your salvage, at
  Guerrilla's prices for your difficulty (cheaper on Easy, dearer on Hard and Insane), in Guerrilla's
  order: buying the grinder opens its faster and explosive discs, the rocket launcher opens heat-seeking
  rockets, and so on. Hover an upgrade to see its levels. A gun you buy is given to you, and **Take**
  gives it again later (Guerrilla's weapon locker). Stonebreaker and Shattermaster replace Mason's
  hammer. What you buy is kept in `mods\guerrilla_gear\upgrades_bought.txt`, the same for every save.
  Guerrilla holds some upgrades back until you liberate a district or finish guerrilla activities; the
  Guerrilla world has neither yet, so the window only notes those. Many upgrades are kept as bought but
  don't do anything yet (more ammo, the remote charge count, armor and the rest); the window says which.
- **Guerrilla's backpacks:** buy the jetpack, or one of the nine backpacks Re-Mars-tered sells
  (Thrust, Fleetfoot, Stealth, Vision, Firepower, Concussion, Heal, Rhino, Tremor; 250 salvage each),
  in the same window, and pick the one you wear under **Backpacks → Wear**. With the jetpack, hold jump
  in the air to climb; it runs on fuel that refills while you don't use it (faster with Jetpack
  Recharge). Switch the others on with **B** (change it in **F7 → Guerrilla → Keys**, or `Backpack=`
  under `[GuerrillaKeys]` in `rfa_mods.ini`), each with Guerrilla's numbers: Thrust throws you up and Rhino makes you take no damage
  while on; Fleetfoot makes you run and sprint faster; Firepower makes your guns, melee and explosions
  hit harder; Concussion sets off a blast around you a second after you switch it on; Tremor shakes the
  ground around you while on; Heal heals everyone near you, enemies too. Stealth, Vision and Rhino's
  charge aren't in yet, and the packs don't show on Mason's back yet.
- The EDF carry these guns, and you can take one a soldier drops, as in Guerrilla: walk over it to
  take its ammo (if you have that gun) or to take it into a free slot, or use the action button to
  swap it for the gun in your hands. A dropped gun carries what Guerrilla gives: part of a magazine and
  a little ammo, more on Easy, less on Hard and Insane.
- In the Guerrilla world you play Mason with Armageddon's movement, except that walking and running
  turn him to face where you go, all the way round, as sprinting does; aim or shoot and he faces
  where the camera looks again. Darius walks the same way in Armageddon's own levels (with
  `guerrilla_gear` on).
- To meet the EDF: they patrol the Guerrilla world's roads and bases (try `level rfg_10_08`), or
  type `drop_human EDF_Trooper` in the console for one in front of you (also `EDF_Elite`,
  `EDF_Elite_Heavy`, `EDF_Lieutenant`, ...). Mason's Nano Forge glove gives off a cyan light, as
  Darius's does; **F7 → Graphics → Mason's Nano Forge light** turns it off (load the level again
  to see it).
- `guerrilla_gear` must be on: it makes room in the game for more weapons (Armageddon holds only 85
  inventory items, and uses up to 79 itself), for their projectiles (100, 91 of them Armageddon's) and
  for the guns' animations. If you turn it off, take
  the weapons away in the builder first.
- Nothing made from Guerrilla is ever shipped; it stays on your PC.
- `guerrilla_world` widens the game's physics for every world, the campaign included. Set
  `ZoneSize=0` under `[GuerrillaWorld]` if you don't use the Guerrilla world.
- To take the world away, tick **World** in the builder and click **Remove...**.

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
- It also shows what you'd otherwise never see: the game's own debug output and the error reports
  it flashes on the screen (marked `[game]`; blue-grey, errors red, warnings yellow), and RFA-Mod's
  log as it's written (each line starts with the mod's name, in teal). So when something doesn't
  load or a command fails, the reason is usually right there.
- Ctrl+V (or Shift+Insert) pastes. The text is cleaned up so the console can run it: curly quotes
  and dashes become plain ones, tabs become spaces, a `]` copied in front of a command is dropped,
  and anything it can't type is left out (it tells you). Paste several lines and they run one at a
  time: the first goes in the line you type, and each Enter runs it and brings up the next (Esc
  drops the rest).
- Tab completes a command's name and lists the matches with what each one does. With arguments
  already typed, Tab leaves your line alone and tells you what the command is. `help` lists every
  command, and `? <setting>` shows a setting's value.
- It keeps 10000 lines, so long lists such as `search <text>` (finds commands) can be read in
  full. Everything it shows is also in `logs\console.log`, and `print_to_file` writes every
  command to `console_commands.txt`.
- Its height, text sizes, darkness, history length, the notify lines, the build stamp and the
  extra logs are under `[Console]` in `rfa_mods.ini`.
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
  copied from `config\` only if they don't exist yet; the templates also go to `bin\defaults\`
  every time, and the launcher adds their new settings to your two files when it starts.
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
source code. Dear ImGui, which draws the in-game menus, and the editor's Direct3D libraries are
under the MIT License, which allows this. AvalonDock, which draws the editor's movable panels, is
under the Microsoft Public License, which the GPL doesn't allow by itself, so the editor carries an
added permission for it ([LICENSE-exception.txt](LICENSE-exception.txt)). Their notices are in
[docs/third-party-notices.txt](docs/third-party-notices.txt). The source code is at
<https://github.com/FragsAreUs/RFA-Mod>; a release's `version.txt` says which commit it was built from.

The mods and the editor work on your own copy of Red Faction: Armageddon, which stays untouched,
and the game's files aren't included, with three exceptions, all menu scripts in
[src/overrides/build/pc/cache/interface.vpp_pc](src/overrides/build/pc/cache/interface.vpp_pc):
`ui_options_gameplay.lua`, the game's own Options → Gameplay script with our Field of View slider
and Experimentals page added, and `ui_main_menu_pc.lua` and `ui_pause_menu_pc.lua`, the main and
pause menus with Experimentals added to their Options lists. Only the lines marked `[RFA]` in them
are ours and under the GPL; the rest is the game's. Red Faction: Armageddon
belongs to its owners; this is an unofficial fan project, not made or endorsed by them.
