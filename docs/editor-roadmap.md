# Editor roadmap: what Volition's own tools did that ours doesn't yet

Red Faction: Armageddon runs on Geo-Mod 2.0, the engine made for Red Faction: Guerrilla. Levels
were built in **CLOE** ("Chunk LayOut Editor"), never released: zones made of destructible
"chunks" (pre-broken buildings and props) and fixed stitch pieces, placed on terrain. Its traces
are still in the game: the debug display of slew mode (`slew_mode`, Q to pick) shows each object's
"CLOE pos" (its position from the middle of its zone), and the console has `warp_to_cloe`. The
other tools covered models, animation, effects, sound, menus and AI. This list is what the editor
in `src\editor` still lacks, grouped by those tools, with what each needs.

## Done so far

Archives and containers, tables (editing fields; adding, copying and deleting entries), textures (same-size replacing), every kind of
model (viewing, exporting), zones and mission layers (moving, turning, adding copies, deleting
with their attachments and destruction links, editing properties and longer text), level
geometry, terrain, foliage and decals (viewing), links between objects (drawn as lines, listed in
the inspector), the zones around, the link with the running game
(show in game, follow the game's camera), CLOE positions in the inspector, skeletons, animations
(playing them on the characters that use them, or on a skeleton; characters' meshes move with
their bones).

## Level design (what CLOE did)

| Missing | What it needs |
|---|---|
| Placing any chunk or building, not only copies of the zone's own | adding the model's files (and textures) to the zone's container and asset list |
| Moving and adding level geometry (stitch pieces) | writing stitch lists (`.stchlst_pc`, read already) |
| Editing links and logic between objects: triggers, logic handlers, patrols, navpoint paths, squads (shown as lines and listed already) | writing the list properties (`obj_links`, `logic_handler_buffer`, ...): their layouts beyond the handles; what a logic handler's buffer says |
| How buildings break: pieces, links and their strengths; tuning them | the destroyables in chunk files (read) and their links (read); their strengths' meaning |
| Lights (placing, colours, flicker curves) | writing `.lightmult_pc`, `.lightpos_pc`, `.lightrot_pc` (read, and played in game light, already; the lighting itself is drawn). A zone's lamps (`obj_light`, 369 in world_08_03) are known: `type_enum` (spot, omni), `Color` (red, green, blue, brightness 1–3), `atten_start` and `atten_range` (metres), `hotspot_size` and `hotspot_falloff_size` (the spot's cone), facing along their forward direction |
| The AI's navigation mesh | `.navdata_pc` (119 files) |
| Terrain and foliage editing | writing `.ctmesh_pc` / `.cterrain_pc` (read already) |
| Effects in levels: fire, smoke, the sky's clouds (the sky's picture is drawn already) | `.cefct_pc` / `.gefct_pc` (2,654 + 962 files) |

## Animation

| Missing | What it needs |
|---|---|
| Blending animations (upper body over lower, flinches) | the weight curves (read) and the game's blend trees (`.blend_treex`) |
| Which palette a mesh part uses, from the game itself | where a mesh with several bone palettes says it (the editor picks by fit now) |
| Weapons in the hands | placing the weapon's model at the `item1`/`item2` bones |
| Civilians whole | their head, body and outfit meshes put together (only the first is shown) |
| Face morphs, cloth | `.cmorph_pc`, `.sim_pc` |
| Editing or making animations | writing `.anim_pc` (the reading is the game's, so writing can mirror it) |

## Other assets

| Missing | What it needs |
|---|---|
| Textures of a new size or format | rebuilding the texture file's header |
| Translated text (all languages) | `.le_strings` (190 files) |
| Sound banks: listening, replacing | `.bnk_pc`, `.mbnk_pc`, `.vad_pc` (Wwise banks) |
| Menus and HUD, fonts | `.vint_doc`, `.vf3_pc` |
| Lua scripts with highlighting and checking | the scripts are plain text already |
| Civilians' outfits | following their outfit meshes |

## The editor itself

| Missing | What it needs |
|---|---|
| Opening big zones without freezing (4–8 s now) | reading models and textures in the background (thread-safe file caches) |
| Less memory in the biggest zones (about 2 GB) | lighter zones around (fewer, simpler models far away) |
| Seeing a level lit as in the game (dark caves, pools of lamp light, fire) | a renderer that lights every pixel (Direct3D 11 in the window; planned in `docs\renderer-plan.md`). Tried with WPF's own lights (2026-10-30): it works light out only at triangle corners and handles a few lights, so the zone's lamps lit almost nothing on the big wall and floor triangles |
