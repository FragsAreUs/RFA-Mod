# Overrides

Files here replace files inside the game's archives (`.vpp_pc`) while the game runs. The
archives themselves are never changed.

This folder mirrors the game folder. Each archive gets a folder with the archive's own name, at
the same path it has in the game folder, and replacements go inside it with the original file
name:

```
overrides\build\pc\cache\interface.vpp_pc\ui_options_gameplay.lua
          └─ <game>\build\pc\cache\interface.vpp_pc ─┘ └─ file inside it ─┘
```

At startup the loader checks every file here: the archive must exist in the game folder and must
contain a file with that name (or, for new files, see below). Each file is then logged as `Override: ...`, or as skipped with the
reason, in `logs\rfa_mods.log`. Files named `README*` are ignored.

Any file directly inside an archive can be overridden: tables (`.xtbl`), Lua scripts, textures,
whole `.str2_pc` containers and so on. Lua scripts are swapped when the game compiles them; every
other file is swapped when the game looks it up by name, and the game then reads the replacement
like a loose file on disk. The log says `Using override ...` the first time the game reads one.
Turn overrides off with `Overrides=0` under `[Loader]` in `rfa_mods.ini`.

An override replaces the copy of the file in *its* archive. Some names are in several archives
with different contents (a container in each multiplayer map, say), and the game uses the one in
the archive it has loaded; the loader tells them apart by size. Archives with identical copies
share one override: every table is in both `table.vpp_pc` and `misc.vpp_pc`, so an override in
either folder replaces both.

**New files** go at their path in the game folder, outside any archive's folder, where the game
folder has no file: `overrides\build\pc\cache\rfg01.vpp_pc` is a new archive (a new map's). When
the game looks for a file and finds it nowhere, the loader gives it the new file at that path, or
of that name. The game mounts a level's archive this way too. Each is logged as `New file: ...`,
and `Using new file ...` when the game first asks for it. A file the game folder already has can't
be replaced whole: put replacements in its archive's folder as above.

**A new Ruin map** is listed with an entry in an override of `wrecking_crew_maps.xtbl` (in
`table.vpp_pc`), with its `file_name` the name of its archive (`rfg01` for `rfg01.vpp_pc`). The
game reads at most five maps from the table, and gives each place its own scoreboard, round times
and score multiplier, so a new map takes the place of one of the game's maps: put it where that
map's entry was. The loader makes the game start the map the entry names (the game itself starts
the map built in for that place), and logs `Ruin map ... is ...` for it, and any map left out past
the fifth.

Files *inside* a `.str2_pc` container can't be overridden one by one: the game loads the whole
container at once. The editor replaces textures in containers by rebuilding the container into
overrides, and updates the container's entries in the asset lists (`.asm_pc`), which it overrides
too. **Undo those with the editor's Restore buttons** (Restore original for textures, Restore in the
level editor), not by deleting files: a container and its asset list entries have to change back
together.

## Making an override

The editor (`bin\editor\RfaEditor.exe`) does it for you: find the file, press **Copy to
overrides**, then edit the copy it puts in `bin\overrides\`.

Overrides start as a copy of the game's file. In the overrides kept in this project, our changes
are marked with `[RFA]` comments, and everything else is the game's original. To compare with the
original, extract it with the editor.

`src\overrides` holds the overrides that are part of this project; `build.cmd` copies it to
`bin\overrides`, replacing files of the same name there. Your own overrides can live in
`bin\overrides` only.

## What is here

| Override | What it changes |
|---|---|
| `build\pc\cache\interface.vpp_pc\ui_options_gameplay.lua` | Options → Gameplay: adds the Field of View slider (45–170), saved to `rfa_mods.ini` through `rfa_get_setting` / `rfa_set_setting` |
