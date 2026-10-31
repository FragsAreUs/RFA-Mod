# Renderer plan: drawing levels with Direct3D 11

Status (2026-10-30): decisions 1 and 2 taken (Vortice 3.8.3, downloaded with permission; D3DImage).
Step 1 built, in a first form: see "Step 1 as built" below. Step 3 (game lighting) built in a first
form: see "Step 3 as built". Step 2 begun in stages: see "Step 2, in stages".

## Why

The editor draws in 3D with WPF's own 3D engine (`Viewport3D`). It got the editor this far, but it
can't make a level look like the game, and it costs a lot of memory:

- **Lighting.** WPF works light out only at the corners of triangles, and handles a few lights
  at a time. The game's caves are dark and lit by lamps (369 in world_08_03). Tried on 2026-10-30
  with the zone's own lamps: the big wall and floor triangles stayed dark, only small pieces near a
  lamp lit up. A game-like picture needs light worked out for every pixel.
- **Materials.** Models name normal and specular maps (`_nm`, `_sp`); WPF can only draw one colour
  texture, flat.
- **Memory and speed.** Every texture is decoded to a 32-bit picture and every mesh is kept as WPF
  objects: about 2 GB for world_05_00 with its zones around. The game's textures are stored in the
  graphics card's own compressed formats (DXT1/DXT5), which Direct3D can use as they are.
- **Picking** (clicking an object) is done by WPF on the processor, triangle by triangle.

## Goal

The level view drawn by Direct3D 11 in the same window, looking much more like the game:

- the zone's lamps lighting every pixel (spot and omni lights, with their colour, brightness, reach
  and cone, as read from `obj_light`), with dim light all round in caves and the sun outside;
- normal and specular maps, glowing parts (lamps, screens);
- textures used as stored (no decoding), each model's buffers on the card once and drawn as many
  times as it is placed;
- picking by drawing object numbers into a hidden picture and reading the pixel under the mouse;
- everything else unchanged: panels, outliner, inspector, gizmo behaviour, keys, the game link.

Later: fog, shadows, the game's effects (fire, smoke, sparks: `.cefct_pc`, not decoded yet), the
sky; characters animated on the card (skinning in the shader) for the preview.

## Choices

**Decision 1: how the editor talks to Direct3D.**

- *Vortice.Windows* (recommended): a free, MIT-licensed C# library that declares Direct3D 11, DXGI,
  Direct3D 9 and the shader compiler for .NET (packages `Vortice.Direct3D11`, `Vortice.DXGI`,
  `Vortice.Direct3D9`, `Vortice.D3DCompiler`). Widely used and maintained. Needs a download from
  NuGet (with your permission); it then ships in `bin\editor` with the editor.
- *Our own declarations*: the Direct3D interfaces we use, written out in C# by hand (the way
  `tools\research\shaders` declares the shader compiler). No download, but about 1,500 lines of
  interface declarations to write and keep right, before drawing anything.

**Decision 2: how the picture gets into the window.** Recommended: WPF's `D3DImage`. Direct3D 11
draws into a picture that WPF shows like any other, so WPF can still draw on top of it (the axis
widget, the hint, pop-ups) and the mouse handling stays as it is. It needs a small Direct3D 9
bridge, as WPF itself is built on Direct3D 9. The alternative, a child window (`HwndHost`), is
simpler, but nothing of WPF can be drawn over it, so every overlay would have to be drawn by the
renderer too.

**Shaders** (no decision needed): our own, written in HLSL (readable source in the repository),
compiled when the editor starts by Windows' own `d3dcompiler_47.dll` (part of Windows 10 and 11,
present on this PC). The game's compiled shaders expect its engine's inputs, so they can't be
reused, but they can be read with `tools\research\shaders` to copy what they do (as was done for
the ground's blended layers).

## How it fits in

```
src\editor\RfaEditor\Render\
    GpuDevice.cs        one Direct3D 11 device for the editor; the bridge to WPF (D3DImage)
    RenderView.cs       one view: its pictures (colour, depth, object numbers), resizing, drawing
    GpuMesh.cs          vertex and index buffers made from the meshes the editor builds today
    GpuTextures.cs      textures from the game's texture files as stored, shared by name
    Lighting.cs         the zone's lamps, ambient light and sun for the shaders
    Shaders\*.hlsl      lit, see-through (foliage, decals), lines (grid, selection, gizmo), picking
```

The code that works out what to draw stays: `LevelScene`, `LevelGeometry`, `FoliageScene`,
`TerrainPainter` and the model readers. They build vertex lists already (through `MeshBuilder`);
those lists go to the card instead of into WPF meshes. `RfaFormats` (reading and writing the game's
files) doesn't change at all.

## Steps

Each step ends with something to try in the editor, and with renders from the off-screen test
(Direct3D can draw into a picture without a window, so the tests keep working).

1. **Start**: a Direct3D view in the level tab, drawing the zone's level geometry untextured;
   resizing, the screen's scaling (DPI), the overlays still on top; the off-screen render to PNG.
   A switch in the toolbar between the old view and the new one, kept until the new one does
   everything.
2. **Same as now**: terrain, models, foliage and decals with their textures, the markers, grid,
   selection box and gizmo; picking; the zones around and mission layers; following the game.
   Lit evenly, as now. Compared side by side with the old view.
3. **Lit like the game**: the zone's lamps lighting each pixel (working out which lamps reach which
   part of the screen, so hundreds stay fast); ambient light and sun per zone (where the game keeps
   them is still to find: its tables or the zone); normal and specular maps; glow. Compared with
   screenshots from the game at the same place (the game link gives the same camera and field of
   view).
4. **Lighter and faster**: textures as stored, each model once on the card however often it is
   placed, the zones around drawn more simply. Measured against today's 2.2 GB for world_05_00.
5. **Then**: the old view removed; fog, the sky; effects once `.cefct_pc` is decoded; the model
   and animation preview moved over too.

Steps 1 and 2 are the biggest (most of the level view's 3D code changes); step 3 is where it starts
to look like the game.

## Step 1 as built

The level toolbar's monitor icon switches the view to Direct3D 11 (`src\editor\RfaEditor\Render\`):

- `GpuDevice.cs`: one Direct3D 11 device on the chip of Direct3D 9's main adapter (matched by the
  adapter id; on this PC the RX 9070 XT), and the Direct3D 9 device WPF needs.
- `RenderView.cs`: the shared colour picture (shown by a D3DImage, with software fallback so the
  off-screen tests see it too), reversed float depth, reading a frame back as a picture. Each
  frame is drawn into a picture of its own and copied into the shared one while the D3DImage is
  locked, and handed over only once the card has finished (an event query): drawing straight
  into the shared picture, WPF sometimes showed half-drawn frames while moving (seen by the user
  2026-10-30: parts missing, the sky showing through the ground). Waiting for the whole frame made
  the editor stutter (the user, next): now the frame is handed over at WPF's next turn to draw,
  when the card has finished it, and only the copy is waited for (by spinning: a Windows sleep can
  take 16 ms). Flying through world_08_03 off-screen: 2–3 ms of the editor's time a frame.
- `SceneRenderer.cs` + `Shaders\Scene.hlsl`: to get there quickly, it draws what the old view's
  `Viewport3D` holds: every WPF mesh and texture is copied to the card the first time it is drawn
  and kept while in use; WPF's lights, two-sidedness, see-through parts and drawing order are
  followed. It draws only when the parts, the camera or the size changed.
- The old `Viewport3D` stays, unseen (opacity 0), for picking; the gizmo, axis widget and hints
  are WPF on top.

Checked on world_08_03 against the old view from the same three cameras: the same picture (after
telling Direct3D that WPF's front faces go anticlockwise, which two-sided parts need). 553 meshes
and 138 textures go to the card in 0.3 s; a frame then takes about 1 ms.

Still from the old view, to replace in step 2: the meshes are built as WPF meshes first (memory),
textures are decoded pictures, picking is WPF's. Seen on the way: bullet-hole decals show as white
squares with holes (both views); the game probably darkens with them rather than painting them on.

## Step 3 as built

The level toolbar's sun icon ("game light") lights the Direct3D view as the game does (the game's
own shaders read for it; see `docs\game-notes.md`, "Lighting"):

- `Level\ZoneLighting.cs` reads the lamps and environment boxes of the zone and the zones around
  (boxes reach over zone edges), and says which box's settings hold at the camera.
- `Render\LampTiles.cs` puts the lamps on the card, and each frame works out which lamps can
  reach each 32-pixel square of the screen (63 at most, nearest first), so a pixel adds up only
  those: 922 lamps for world_08_03 and its neighbours, a frame still a few milliseconds.
- `Shaders\Scene.hlsl` lights each pixel as the game's light shaders do (front, side and back
  colours; the fades; spot cones; tubes; ambient lamps), adds the box's ambient from above and
  below, fog, the tint and exposure, then the game's tone curve and sRGB.

Checked against the user's screenshot of the first mission's tunnel: warm, dark, with the fire
lamps lighting the wreck. One factor isn't read from the game but measured against it: the
environment box's ambient is used at 1/8 (`SceneRenderer.AmbientScale`; first set by eye at 1/32,
then measured 2026-10-30 from the running game's own camera next to a screenshot of the same view;
at face value the caves come out nearly white).

Then surfaces (2026-10-30): each WPF material the builders make carries the model's own
(`Render\GameMaterial.cs`, an attached property: its normal, specular and glow map names and its
constants), and in game light the renderer reads those maps by name the first time a surface
needs them (`SceneRenderer.LoadTexture`). The shader bends the surface by the normal map along
tangents worked out from screen derivatives (the meshes carry none), adds each lamp's shine as the
game's shaders do, multiplies by `Diffuse_Color`, and adds glow. Checked on world_08_03: light
fixtures and the pipe's light strips glow, scratched metal shines where lamps reach it, and many
surfaces are as dark as their `Diffuse_Color` makes them in the game. Loading the maps takes about
2 s the first time game light is switched on in a zone.

Then decals, flicker and the sky (2026-10-30). Materials the game draws see-through are drawn
after the solid ones (the bullet holes over the first mission's walls had shown as white
squares). (At first that was any material with a `Main_Opacity`; since 2026-10-30 it is the
material's flags, see "Alien skin and the material flags" below.) Lamps with curves (`RfaFormats\Zones\LightCurve.cs`) flicker and sway: each
frame `LampTiles` works out every such lamp's brightness and offset from the editor's clock and
the shader applies them, and the view keeps drawing while any lamp moves. The sky: where the
environment names a sky effect, its picture is read from the effect file's texture names
(`ZoneLighting.SkyPicture`) and drawn first, over the whole view, as a dome around the camera
(`SkyPixel` in `Scene.hlsl`). Checked off-screen on world_08_03: the first mission's day sky
behind the Terraformer, the fire lamp's light changing from frame to frame, no white squares.

Then shadows (2026-10-30), as the game's `ir_light_spot` draws them (see `docs\game-notes.md`,
"Shadows"): `Render\LampShadows.cs` keeps a 1024 × 1024 depth map for each of up to 16 lamps that
cast shadows and whose shadows can show from the camera (nearest first), drawn before the frame
(`SceneRenderer.DrawShadows`) only when the scene changes or the camera comes near other such
lamps (lamps that stay near keep their maps; only those of lamps newly near are drawn: 2–4 maps
over a 60 m flight). Only the level's own things cast shadows (textured, or with the game's material), not
the editor's plain-coloured markers; see-through ones only where their picture is solid. The
shader tests each such lamp's pixels against its map with nine smoothed samples, as the game
does. Checked off-screen on world_08_03 by drawing each view with and without shadows and by
saving the maps: the fire spotlight by the wreck sees the ground, bushes and a rock, which shade
the ground; the lamps' own fittings are kept out by drawing nothing nearer than 0.25 m.

## Step 2, in stages (begun 2026-10-30)

Step 1 already draws everything the old view did (terrain, models, foliage, decals, markers,
zones around, mission layers, following the game), from the WPF scene. What is left of step 2
(with step 4) is to stop going through WPF, a stage at a time, each tried in the editor:

1. **Textures as stored** (built 2026-10-30): where a material names the game's texture (models
   and level geometry: `GameMaterial.DiffuseMap`), the Direct3D view puts that texture on the
   card as the game stores it (`SceneRenderer.FindTexture`): DXT1/3/5 as BC1/2/3 and 32-bit
   colour as they are, full size, with the game's own smaller copies; other formats decoded.
   Normal, specular and glow maps and the sky likewise. WPF's pictures were decoded at most 256
   pixels across. The game's textures keep alpha even where it isn't see-through, so it is
   taken only for see-through surfaces (`Options.x` in `Scene.hlsl`). Checked off-screen: signs
   and rock sharper, bushes still cut out, the first frame no slower.

   **Alien skin and the material flags** (2026-10-30, after the user saw monsters turn red in
   game light and the tentacles break): the full-size textures' alpha made the solid materials
   that take a `Main_Opacity` (tentacles, creepers' legs, cloth, walkers) vanish, and the alien
   skin's glow covered them (flat blue tentacles and pods, red creatures). Now see-through
   follows the material's flags (`ModelMaterial.Flags`: blended in the see-through pass, cut out
   in the solid pass by `Options.y`, everything else solid), and alien skin is drawn as its
   shader does (`Skin` in `Scene.hlsl`; see docs\game-notes.md, "Alien skin"), its glow from the
   glow map's average colour since the editor doesn't read the second texture coordinates.
   Checked off-screen: mp_inf01's tentacles whole and textured, a creeper with its legs and a
   few red spots of glow, the world_00_02 pods dark and textured, grunts their own olive with red
   streaks; world_08_03 unchanged but for the bushes, now cut out crisply at half alpha.
2. **Picking on the card**: each object's number drawn into a hidden picture, read under the
   mouse, instead of WPF's hit testing (which needs every mesh as WPF objects).
3. **Meshes straight to the card**: the builders (`LevelScene`, `LevelGeometry`, `FoliageScene`,
   terrain) give vertex lists to the renderer instead of WPF meshes; each model once on the card
   however often it is placed; the hidden WPF view and its decoded pictures dropped. Measured
   against today's 2.2 GB for world_05_00.
4. **The old view removed**, with the model and animation preview moved over.

Not yet: shadows of the 18 omni lamps that cast them, of swaying lamps as they sway, the
game's exact fade-out (a guess: over a quarter of shadow_distance) and its size for
"orthographic" shadows (here the cone's width at its reach); reflections, detail and second
texture layers, the auto-exposure, the sky's clouds, lightning and mountains (the rest of the
effect); the sky dome's shape is a guess (which way its picture faces was matched against the
user's screenshots); which way the normal map's up-down bend points is taken from the texture's v
direction, not checked against the game. The ground in game light is painted as the game mixes
it (layers × their `Diffuse_color_N`, unbrightened): it had come out pale grey-blue.

## Risks

- **Two graphics chips** (this PC has the RX 9070 XT and the Ryzen's built-in one): the renderer
  must use the chip WPF draws with, or the picture can't be shared. Pick the adapter of the
  monitor the window is on.
- **Lost devices**: locking the PC, remote desktop or a driver reset make Direct3D 9 pictures go
  away; `D3DImage` says when, and the view rebuilds its pictures.
- **Memory on the card**: big zones with the zones around must fit; textures as stored are about
  a quarter to an eighth of today's decoded pictures.
- Nothing about the game or its files changes: this is only how the editor draws.
