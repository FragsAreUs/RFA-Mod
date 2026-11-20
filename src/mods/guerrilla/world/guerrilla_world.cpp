// What the world ported from Red Faction: Guerrilla needs that Armageddon doesn't do itself. So
// far: Guerrilla's sky and clouds, a running clock, and the light of the sky and the sun.
//
// Guerrilla's sky is a dome coloured by the time of day: nine colour curves in each time-of-day
// set (.dtodx), four bands from the horizon up on the west side (west0-west3), four on the east
// (east0-east3) and the zenith. Its shader (rfg-skybox_s, which Armageddon still ships in
// misc.vpp_pc) picks the band from how high a point of the dome is (the mean of its angle above the
// horizon and its height, 0 at the horizon to 4 straight up), blends the two bands either side,
// and blends the west colour with the east one by how far round from west to east the point is.
//
// Its clouds (rfg-skybox-clouds_s, in Guerrilla's misc.vpp_pc) are a model round the eye, an
// overhead dome and a ring along the horizon (skybox_clouds, which both games ship), each with two
// textures holding two cloud layers each: green and alpha the layers' cover, red and blue their
// light. Each layer's cover is scaled by its strength from the time of day (overhead_layer0-3 or
// horizon_layer0-3_strength) and faded in with height; the layers are laid one over another; the
// clouds are lit by blending a front and a rear light colour (clouds_front/rear_light_color, or the
// clouds_horizon_ ones) by which way they face the time of day's light, with a rim of light where
// they are thin and between the eye and the sun (cloud_backlight_strength, _power); layers 0-1 and
// 2-3 move at their own speeds (cloud_layer01_speed, cloud_layer23_speed): along the horizon they
// slide round it, overhead (rfg-skybox-clouds-2_s) they turn about the top of the sky, tiled 4 and
// 4.5 times. In front of
// them a ring of far mountains, the backdrop (rfg-skybox-matte_s, vfx_sky_backdrop_01): its picture
// mixed with a haze colour (horizon_mountain_fog_color, _density), lit by blending the mountains'
// colour on the side facing the light and on the far side (horizon_mountain_color, _back).
//
// Armageddon reads all those curves into every time-of-day set but never draws a dome for a world:
// its scene renderer's sky step (rl_scene_renderer::render_skybox, 0xD528E0) only draws the
// unlockables viewer's background. This mod wraps that step: after the game's own, in the worlds
// named by [GuerrillaWorld] Worlds= (the ported world is "rfg"), it draws Guerrilla's sky behind
// everything, as one triangle over the whole screen at the far plane, each pixel coloured the way
// Guerrilla's shader colours its dome (kSkyShader), then the clouds over it the way Guerrilla's
// cloud shader draws them (kCloudShader), from the time of day's colours this frame times
// [GuerrillaWorld] Brightness= (read again whenever rfa_mods.ini is saved). Where the world has
// drawn something nearer, the depth test keeps it. The cloud model and textures are files in
// mods\guerrilla_world\ (clouds.bin and the textures as .dds), made from the player's own copy of
// Guerrilla; without them the sky is drawn alone.
//
// The mod draws with the game's own Direct3D 11 device, which it notes when the game makes it
// (the game imports D3D11CreateDevice), and puts back everything it changes on the device context.
//
// The clock: Armageddon still has Guerrilla's game clock, and its time of day still follows it
// every frame, but the game sets the clock's speed to 0 at start-up, so the day never moves. In our
// worlds the mod runs the clock at [GuerrillaWorld] ClockSpeed= (game seconds a real second), and
// puts the game's own speed back when a world that isn't ours is played.
//
// The view distance: the game streams a world's zones (each with its far-away ground) only within a
// radius of the player, small in the campaign (its levels are walled canyons). In our worlds, whose
// zones stream in a layer of their own, the mod widens it to [GuerrillaWorld] StreamRadius= metres
// (500 by default: the 3 × 3 of our 511 m zones around the player, as Guerrilla's own 510 m; at
// most just under two zones, 5 × 5), and puts the game's own back elsewhere.
//
// The zone size: our worlds' zones are [GuerrillaWorld] ZoneSize= metres wide (511, Guerrilla's),
// so 16 × 16 of them is Guerrilla's whole map; the mod widens the game's physics world at start-up
// to hold it (for every world: the game makes it once).
//
// The light: Armageddon lights surfaces only with lamps (its levels place lamps for the sun and the
// sky); the time of day's ambient light doesn't reach them. In our worlds the mod adds light itself,
// into the light buffer the lamps light (after the game's light pass): the sky's light, the time of
// day's ambient light from above and from below blended by which way a surface faces, times
// [GuerrillaWorld] Ambient=; and the light of the sun (or by night, the moon) along its path over
// the day, tod_light_color times Sun= (both read again whenever rfa_mods.ini is saved), with its
// shine on the surfaces that have specular maps, as Armageddon's lamps give theirs. The clouds
// are lit from the same direction. Guerrilla's colour correction (color_correct_tint and
// _saturation, ColourCorrection=), which Guerrilla applies to its whole picture and Armageddon to none
// of it, is applied with Guerrilla's tone mapping to the whole scene in its last pass, as Guerrilla's
// (Armageddon's own lamps, glows and effects with it), and without it to what the mod draws itself.
//
// The fog: Armageddon's is each environment box's, by distance only, and it keeps the time of day's
// fog curves without drawing with them. Guerrilla's is height fog, thicker low down and far away,
// from its districts' curves. In our worlds the mod draws Guerrilla's over what the world has drawn,
// before the sky, from the depth the light pass reads (Fog=, thickness; the world builder turns the
// boxes' own fog off there), and over the sky, the clouds and the backdrop as Guerrilla's sky shaders
// fog them, with the clouds' ambient light as Guerrilla adds it.
//
// The tone mapping: Armageddon turns its picture into the final one with an exposure the environment
// box sets (3 in our zones), a curve that clips its highlights and a colour table. In our worlds the
// mod holds that exposure at 1 and draws Guerrilla's tone mapping over the result instead: its eye
// adapting to the picture's brightness, its exposure from the time of day's curves, its bloom (the
// glow round bright things, from the picture scaled by the eye, in three stages blurred and laid
// together, as Guerrilla's compositor makes it; Bloom=), and its curve that rolls the highlights off
// (ToneMapping=, Exposure=).
//
// The sun and moons: Armageddon draws none in our worlds. The mod draws Guerrilla's: every orbital of
// the world's global time of day (the sun, Phobos and Deimos; each its own path, texture, size, colour
// and opacity over the day) as a small bright picture behind the clouds; the sun's corona (its rays,
// glow, halo and lens flares over the picture, fading as the sun is hidden or sets); the night sky's
// stars (from Guerrilla's star catalogue) and meteors; and with the tone mapping, Guerrilla's sun
// shafts, the glow streaming from the sky round the sun past whatever stands in front of it
// (SunShafts=).
//
// And it leaves out the game's "invalid mission info hash" report for our worlds' zones, which
// carry no mission (one line a zone at every load).
//
// The precache: a world keeps the files several of its containers use once, loose, in a precache
// archive, and each streaming cell's precache group in the asset list names those it needs. Which
// precache a level mounts comes from the area of its start zone's grid place, from a table filled
// once at start-up from precache.xtbl (bio, ice, magma, natural, surface; elsewhere "world"). That
// table knows only grid places, so our worlds would get the campaign's empty world_precache: for
// them the mod answers the area as the world's own name, so a world rfg mounts rfg_precache.vpp_pc.
//
// The gameplay properties: a destructible object names an entry of gameplay_properties.xtbl (its
// flags, such as shadows, a map icon or smoke when destroyed, and its importance, team and salvage).
// Armageddon's table lacks many of Guerrilla's entries and has the rest with its own settings, so the
// world builder writes Armageddon's table with Guerrilla's settings added
// (mods\guerrilla_world\gameplay_properties.xtbl). The game reads that table once, at start-up, from
// its copy of table.vpp_pc in memory, where the loader's overrides don't reach; the mod hands it the
// builder's table there, and gives the table's names more room than the game's 24 KB, which
// Armageddon's own nearly fill. Without that file the game keeps its own table. The draw distances
// (lod_properties.xtbl: the LOD class an object names, Guerrilla's switch-off distances added) are
// handed over the same way, and so are the effects: level_objects.xtbl (what a destructible object
// shows when it breaks or is hit), effects.xtbl and vfx.xtbl (the visual effects and their files),
// and explosions.xtbl (what an object dies in), each Armageddon's with Guerrilla's entries the world
// uses added under "rfg_" names; and vehicles.xtbl and vehicle_interactions.xtbl, Armageddon's with
// Guerrilla's vehicles that GuerrillaBuilder.exe made added under their own names; and glass.xtbl
// (the colour a pane's shatter is tinted by its texture), Armageddon's with Guerrilla's entries added
// under our worlds' texture names.
//
// Broken glass: Armageddon makes no bullet marks on glass at all, so in our worlds the mod makes
// Guerrilla's (its bullet hole on both faces of a pane, and mostly a crack effect and two or three
// cracks running out from it), on the game's own decals and settings (BulletDecal); and it shatters
// panes with Guerrilla's settings, all the small shards round the hit kept and thrown and spun as
// hard (GlassUpdate).
//
// The undergrowth: Guerrilla's grass and dust rocks, which its zones' terrain files scatter over 8 m
// cells and Armageddon's undergrowth code still loads, scatters and draws. Its random scatter puts
// every piece at height 0 (it takes the height before looking it up), so the mod replaces it with the
// same scatter in Guerrilla's order, the pieces on the ground (RandomlyPlace).
//
// Buildings breaking: Armageddon still runs Guerrilla's whole support system (anchors to the ground,
// links between pieces and between buildings, stress), with some of its own numbers in it. In our
// worlds the mod gives it Guerrilla's: how strongly a piece standing on the ground holds to it
// (GuerrillaAnchors=), the materials whose strength Armageddon changed and the load a building's
// weight puts on its links (GuerrillaMaterials=); and it logs what breaks (DestructionLog=). See
// "Guerrilla's buildings" below.
#define RFA_MOD_NAME "guerrilla_world"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "world_api.h"
#include "campaign/campaign_api.h"
#include "gear/gear_api.h"
#include "rfa_game.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <intrin.h>
#include <math.h>
#include <tlhelp32.h>
#include <stdarg.h>
#include <algorithm>
#include <unordered_map>
#include <vector>
#pragma comment(lib, "user32.lib")  // the long frames' watchdog: GetForegroundWindow, GetWindowThreadProcessId

namespace {

// ---- The game

// The world being played: its name ("world" for the campaign, "rfg" for the ported world) is
// kept in the world object (0x1870630) at +0x29EA, as the time of day reads it to name its files.
const char* WorldName() { return reinterpret_cast<const char*>(0x1870630 + 0x29EA); }

// The camera the frame is drawn with: the camera manager (0x11B9C40) +0x50, its rows right, up and
// forward; +0xC0, its vertical field of view in degrees (as the console and FOV mods read them).
auto& g_cameraRotation = *reinterpret_cast<float(*)[9]>(0x11B9C90);
auto& g_cameraFov = *reinterpret_cast<float*>(0x11B9D00);
// Where it is (+0x2C).
auto& g_cameraPosition = *reinterpret_cast<float(*)[3]>(0x11B9C6C);

// The time of day's light direction this frame: the time of day's update (0x52D6E0) hands the
// renderer a block of particle lighting each frame (0x1973924, by 0x7686E0): an ambient colour, a
// light colour, then this direction.
auto& g_lightDirection = *reinterpret_cast<float(*)[3]>(0x1973944);

// The time of day: up to four loaded sets (d, c, b, a) with their weights, and the time the curves
// are read at, in hours (as in the console mod). A colour curve's value is red, green, blue and a
// brightness; the game's own functions work a curve out (methods of the curve, in ECX).
auto& g_timeOfDay = *reinterpret_cast<float*>(0x10F8898);
auto& g_todSets = *reinterpret_cast<BYTE* (*)[4]>(0x12FD6B4);
auto& g_todWeights = *reinterpret_cast<float(*)[4]>(0x12FD6C4);
using ColourAtFn = float*(__fastcall*)(void* curve, void* unused, float* out, float time);
using NumberAtFn = float(__fastcall*)(void* curve, void* unused, float time);
auto const ColourAt = reinterpret_cast<ColourAtFn>(0x531B90);
auto const NumberAt = reinterpret_cast<NumberAtFn>(0x531CB0);

// The game clock (0x12F76D0: date, hour, minute, second, then this): its speed, in game seconds a
// real second (Game_clock.m_time_scale, the console's game_clock_scale_factor). Set to 0 only at
// start-up (0x51F910); setting the time (0x520410, set_time_of_day) keeps it.
auto& g_gameClockSpeed = *reinterpret_cast<float*>(0x12F76D8);

// Streaming: the stream manager (its pointer at 0x1872BFC) keeps each layer's containers loaded
// within a box of a radius around the player (stream_follows_player). The layers are two grids
// over the world (set up at 0x6B56F0): layer 0 the sub-zones, 48 × 48 cells a third of a zone wide
// (radius 170 m), layer 1 the zones, 16 × 16 (radius 255 m): the console's stream_radius and
// stream_radius_terrain. 0x6B3100 sets a layer's radius (the box; the next update streams to it),
// 0x6B3180 gives it. A layer can track only about 35 containers (its update's lists hold 35; past
// that it drops cells and the game crashes), so the zones' radius stays at most two zones (5 × 5);
// the campaign's 255 m keeps 3 × 3, so its ground stops two zones away.
auto& g_streamManager = *reinterpret_cast<void**>(0x1872BFC);
using SetStreamRadiusFn = void(__fastcall*)(void* manager, void* unused, int layer, float radius);
using StreamRadiusFn = float(__fastcall*)(void* manager, void* unused, int layer);
auto const SetStreamRadius = reinterpret_cast<SetStreamRadiusFn>(0x6B3100);
auto const StreamRadiusOf = reinterpret_cast<StreamRadiusFn>(0x6B3180);
// The setter, wrapped: in our worlds no radius may reach a box that could meet more cells than a
// layer tracks (the game sets them itself, from the zone size, at times a mode changes).
constexpr DWORD kSetStreamRadius = 0x6B3100;
constexpr size_t kSetStreamRadiusPrologue = 6;  // push ebp / mov ebp,esp / sub esp,20h
SetStreamRadiusFn g_setStreamRadius;

// Where streaming centres: each frame the world's step (0x6C84B0) writes the camera's position
// (0x11B9C6C; the player's with the console's stream_follows_player) to the world at +0x25D0, and
// 0x6DB730 hands it to the stream manager's update, 0x6B28B0 (its only caller; thiscall: the
// manager, the position, a flag). Each layer's box is that position ± its radius, with no give: a
// cell that leaves the box is unloaded that frame, one that enters it loaded. Near a cell's line the
// camera swinging round the player (turning to aim, a hammer swing, an explosion's knock, a vehicle's
// camera) moves the box's edge back and forth over it: a row of sub-zones 340 m off (an 850 m strip of
// buildings, made again when it comes back) or a column of zones 500 m off unloads and loads again
// within seconds (in the run of 2026-11-12, 96 of 533 sub-zone loads were one loaded again within
// 15 s). Guerrilla streams the same way, but its camera draws nothing past 475 m, so its edges are
// out of sight; ours drew to 1500 m (now to FarClip=, Guerrilla's 475 unless a player asks for more:
// see ApplyEnvSettings). In our worlds the update is handed a held position instead,
// moved to the camera only once it has gone StreamSteady= metres from it across the ground (its height
// always the camera's), so a swing of the camera no longer moves the box.
constexpr DWORD kStreamUpdate = 0x6B28B0;
constexpr size_t kStreamUpdatePrologue = 9;  // push ebp / mov ebp,esp / sub esp,6A4h
using StreamUpdateFn = void(__fastcall*)(void* manager, void* unused, const float* position, int flag);
StreamUpdateFn g_streamUpdate;
float g_streamHeld[3] = {};   // the position streaming is centred on in our worlds
bool g_streamHeldSet = false;

// The zone size: 0x6C0D60(name, &size) gives a world's zone width from its name (a world's or a
// zone's): 511 m, Guerrilla's, for names starting "mpl_" (left over from Guerrilla's multiplayer
// maps), 255 m for any other. The streaming grids' cells, the zones' places and the terrain's grid
// step all come from it, while the grid stays 16 × 16 zones: with Guerrilla's size it is
// Guerrilla's own 8 km map. Our worlds get [GuerrillaWorld] ZoneSize=.
constexpr DWORD kZoneSize = 0x6C0D60;
constexpr size_t kZoneSizePrologue = 6;  // push ebp / mov ebp,esp / sub esp,40h
using ZoneSizeFn = void(__cdecl*)(const char* name, float* size);
ZoneSizeFn g_zoneSize;

// The physics world: Havok's broadphase, made once at start-up (0x772C30, "havok_system_init"), is
// a box 2295 m out from the middle across the ground (nine 255 m zones: the 16 × 16 grid and a zone
// to spare) and 1000 m up and down; the game's out-of-the-world bounds (0x1975E2C, 0x1975E38) are
// the same box. A body outside it is dropped: a zone's ground ("Tried to delete a zone (%s) which
// has dimensions outside the broadphase", and the player falls through) or a character ("fell out
// of the world"). The 2295 m comes from two constants only that code reads (0xFB5DC0, and
// 0xFB5DC4 for -2295), so with bigger zones the mod sets them, before the game starts, to nine of
// our zones. It is one box for every world, the campaign's too.
constexpr DWORD kWorldReach = 0xFB5DC0, kWorldReachBack = 0xFB5DC4;
constexpr float kGameWorldReach = 2295.0f;
constexpr float kWorldReachZones = 9.0f;

// The time of day's ambient light: each frame the time of day's update (0x52D6E0) hands the world
// its ambient light from above (0x768520) and from below (0x768570), each a colour by pointer,
// already times its brightness (held to ldr_min..ldr_max, so at most the colour itself); the world
// keeps them at its start (0x1870630, 0x187063C), and its update gives them to the renderer's scene
// (V_ambient_render, V_back_ambient_render). Only shaders drawn straight to the screen use them:
// the ground gets no light but the light buffer's (below). The environment boxes' own ambient
// colours only reach effects.
constexpr DWORD kSetAmbient = 0x768520, kSetBackAmbient = 0x768570;
constexpr size_t kSetAmbientPrologue = 6;  // push ebp / mov ebp,esp / sub esp,0Ch
using SetAmbientFn = void(__cdecl*)(const float* colour);
SetAmbientFn g_setAmbient, g_setBackAmbient;
auto& g_worldAmbient = *reinterpret_cast<float(*)[3]>(0x1870630);
auto& g_worldBackAmbient = *reinterpret_cast<float(*)[3]>(0x187063C);

// Where a set keeps its curves (found in its reader, 0x558EE0). The sky's: west0-west3,
// east0-east3, zenith, in the order Guerrilla's shader takes them (the west bands then the zenith,
// the east bands then the zenith again).
constexpr size_t kSkyCurves[10] = {0x0, 0x340, 0x680, 0x9C0, 0x1A00, 0xD00, 0x1040, 0x1380, 0x16C0, 0x1A00};
// The clouds': for the overhead clouds [0] and the ones along the horizon [1].
constexpr size_t kCloudFront[2] = {0x4AE0, 0x5160};  // clouds_front_light_color, clouds_horizon_front_light_color
constexpr size_t kCloudRear[2] = {0x4E20, 0x54A0};   // clouds_rear_light_color, clouds_horizon_rear_light_color
constexpr size_t kCloudLayers[2][4] = {{0x5BB0, 0x5CA4, 0x5D98, 0x5E8C},   // overhead_layer0-3_strength
                                       {0x57E0, 0x58D4, 0x59C8, 0x5ABC}};  // horizon_layer0-3_strength
constexpr size_t kBacklightPower = 0x6A34, kBacklightStrength = 0x6B28;
constexpr size_t kLayer01Speed = 0x6C1C, kLayer23Speed = 0x6D10;
// The backdrop's: horizon_mountain_color (the side facing the light), _back, _fog_color, _fog_density.
constexpr size_t kMountainFront = 0x5F80, kMountainBack = 0x62C0, kMountainFog = 0x6600, kMountainFogDensity = 0x6940;

// The sun's light: tod_light_color.
constexpr size_t kSunColour = 0x1D40;

// The game's error reports (0x545D40: level, format, ...) hand their arguments on to 0x545BD0
// (level, 1, format, the arguments). Loading a zone whose header names no known mission reports
// "%s has an invalid mission info hash of 0x%08X" (this format, from 0x6B5CE0 and 0x501C30): the
// ported world's zones carry Ruin's "none" (0x01BEEBE5), 64 lines on the screen at every load; a
// real mission's name would make the level command start that mission (0x501C30 sets the current
// mission, 0x55F600). So the mod drops that one report for zones of our worlds.
constexpr DWORD kReportError = 0x545BD0;
constexpr size_t kReportErrorPrologue = 9;  // push ebp / mov ebp,esp / sub esp,12Ch
constexpr DWORD kMissionHashFormat = 0xF82734;
using ReportErrorFn = void(__cdecl*)(int level, int flag, const char* format, va_list arguments);
ReportErrorFn g_reportError;

// The game's debug print (0xCA9F10: a format and its arguments, written out with
// OutputDebugString). What it says while loading (an archive it couldn't mount, ...) reaches
// nowhere a player sees, so in our worlds the mod writes it to the log as well (the first 500).
constexpr DWORD kDebugPrint = 0xCA9F10;
constexpr size_t kDebugPrintPrologue = 6;  // push ebp / mov ebp,esp / sub esp,8
using DebugPrintFn = void(__cdecl*)(const char* format, ...);
DebugPrintFn g_debugPrint;

// The area a level's start zone is in, for its precache (0x6B4710: column, row and the grid's size
// in, the area's 64-byte name out). Its one caller, the level's streaming set-up (0x6B43ED, after
// the world's name is known), then looks for the one "<area>_precache.precache" marker among the
// world's archives and mounts build\pc\cache\<area>_precache.vpp_pc.
constexpr DWORD kPrecacheArea = 0x6B4710;
constexpr size_t kPrecacheAreaPrologue = 6;  // push ebp / mov ebp,esp / sub esp,10h
using PrecacheAreaFn = const char*(__cdecl*)(int column, int row, int gridSize);
PrecacheAreaFn g_precacheArea;

// While the game starts, it reads tables from its copy of table.vpp_pc in memory: the table reader
// (0xC8E760) asks the copy for a table by its file name (0xC8FCD0, a method of the copy: true and the
// table's bytes, or false, and the reader opens the file by name instead).
constexpr DWORD kTableCacheFind = 0xC8FCD0;
constexpr size_t kTableCacheFindPrologue = 6;  // push ebp / mov ebp,esp / sub esp,8
struct CachedTable {
    const void* data;
    DWORD size;
};
using TableCacheFindFn = bool(__fastcall*)(void* cache, void* unused, const char* name, CachedTable* out);
TableCacheFindFn g_tableCacheFind;

// A pool of names (a method of the pool: the buffer, its size, the pool's name). The gameplay
// properties' is "Gameplay Property Names", 0x6000 bytes, set up just before the table is read
// (0x7A6E50); a name takes its length and 2 or 3 bytes more, and a full pool stops the game.
constexpr DWORD kNamePoolInit = 0xC97AF0;
constexpr size_t kNamePoolInitPrologue = 6;  // push ebp / mov ebp,esp / sub esp,8
using NamePoolInitFn = void(__fastcall*)(void* pool, void* unused, void* buffer, DWORD size, const char* name);
NamePoolInitFn g_namePoolInit;

// The tables the world builder writes for the mod to hand the game: the gameplay properties, the draw
// distances (LOD classes), and the effects (visual effects, effects, explosions, level objects), each
// read at start-up (the visual effects before the effects, which name them, and those before the
// level objects, which name the effects; the explosions name effects too, and level objects name
// explosions), and the glass tints. The vehicles and their interactions (where and how people get in; Armageddon's with
// Guerrilla's added) are written by GuerrillaBuilder.exe, and so are the animation lists their
// animation sets need: the sets (anim_set_filenames.xtbl, read first; each set's own table is a new
// file the game then finds by name), every animation's triggers and flags (anim_files.xtbl), and the
// state and action names (anim_states.xtbl, anim_actions.xtbl), each Armageddon's with Guerrilla's added.
struct BuiltTable {
    const char* name;         // its file name, in mods\guerrilla_world\ and as the game asks for it
    const char* what;         // for the log
    std::vector<BYTE> bytes;  // the builder's table, with a 0 after it; empty if there is none
};
BuiltTable g_builtTables[] = {
    {"gameplay_properties.xtbl", "Gameplay properties", {}},
    {"lod_properties.xtbl", "Draw distances", {}},
    {"vfx.xtbl", "Visual effects", {}},
    {"effects.xtbl", "Effects", {}},
    {"explosions.xtbl", "Explosions", {}},
    {"level_objects.xtbl", "Level objects", {}},
    {"vehicles.xtbl", "Vehicles", {}},
    {"vehicle_interactions.xtbl", "Vehicle interactions", {}},
    // Guerrilla's vehicle spawn groups ("rfg_<its name>", the vehicles built), which the world's parked
    // vehicles' spots name; Armageddon's own rows kept.
    {"spawn_group_vehicle.xtbl", "Vehicle spawn groups", {}},
    // Guerrilla's vehicles' turrets (the turrets their mounts name; Armageddon's with Guerrilla's added).
    {"turrets.xtbl", "Turrets", {}},
    // Guerrilla's vehicles' light types (each a row: its kind, Guerrilla's glow effects, its breaking), which
    // the vehicles' Lights maps name; Armageddon's own rows kept. Read once while the game starts.
    {"vehicle_lights.xtbl", "Vehicle lights", {}},
    // Guerrilla's walkers' blend trees (one file of Armageddon's kind, GuerrillaBuilder.exe's), and Armageddon's
    // list of tree files with it; read while the game starts (guerrilla_vehicles makes room for the trees).
    // Guerrilla's walkers' footsteps (their own effects; Armageddon's rows kept).
    {"footsteps.xtbl", "Footsteps", {}},
    {"blendTree_filenames.xtbl", "Blend tree files", {}},
    {"rfg_walkers.blend_treex", "Guerrilla's walkers' blend trees", {}},
    {"anim_set_filenames.xtbl", "Animation sets", {}},
    {"anim_files.xtbl", "Animation files", {}},
    {"anim_states.xtbl", "Animation states", {}},
    {"anim_actions.xtbl", "Animation actions", {}},
    {"glass.xtbl", "Glass tints", {}},
    // Guerrilla's weapons (GuerrillaBuilder.exe's weapons; each Armageddon's with Guerrilla's added): the
    // weapons, their items (models in the hand and on the ground; the inventory items name them), their
    // inventory items, ammo types (Guerrilla's ammo is each weapon's own), and the NPCs' firing patterns
    // and aim drift where Guerrilla's differ from Armageddon's of the same name. The game reads the
    // inventory items and items before the weapons, which name them.
    {"weapons.xtbl", "Weapons", {}},
    {"items_3d.xtbl", "Items", {}},
    {"items_inventory.xtbl", "Inventory items", {}},
    {"ammo.xtbl", "Ammo types", {}},
    {"weapon_firing_patterns.xtbl", "NPC firing patterns", {}},
    {"aim_drift.xtbl", "NPC aim drift", {}},
    {"camera_shake.xtbl", "Camera shakes", {}},  // Guerrilla's weapons' where they differ from Armageddon's of the name
    {"projectiles.xtbl", "Projectiles", {}},     // Guerrilla's thrown and launched weapons'; Armageddon holds 100, its own 91
    {"melee.xtbl", "Melee moves", {}},           // Guerrilla's (the hammer's swings, ...), each rfg_; Armageddon holds 32, its own 22
    // Guerrilla's melee groups and bullet groups (what a melee hit or a shot shows and sounds like on each material:
    // the hammer's, the gutter's, a gun's butt; a bullet's, the rail driver's), each rfg_, which its weapons name;
    // Armageddon's own kept. The game makes room for as many as each table has.
    {"rfg_melee_groups.xtbl", "Melee groups", {}},
    {"rfg_bullet_groups.xtbl", "Bullet groups", {}},
    // Guerrilla's people (GuerrillaBuilder.exe's people; Armageddon's characters with Guerrilla's added). Their
    // animation sets go into the four animation lists above, which it shares with the vehicles.
    {"character.xtbl", "Characters", {}},
    // Armageddon's AI properties with rfg_unarmed (its Default less Auto Equip), which Guerrilla's characters with no
    // equip item name: Guerrilla never puts a gun in a new NPC's hands (GuerrillaBuilder.exe's people).
    {"ai_properties.xtbl", "AI properties", {}},
    // Guerrilla's squads (their members as Armageddon names them) and their weapons (loadouts), the
    // squads its zones' patrols and spawn nodes put down (GuerrillaBuilder.exe).
    {"squads.xtbl", "Squads", {}},
    {"squad_loadout.xtbl", "Squad loadouts", {}},
    // The Guerrilla world's spawn resource (rfg_Default: the people its zones stream in), which every
    // zone of it names, in place of Armageddon's Default (its aliens and soldiers).
    {"spawn_resource.xtbl", "Spawn resources", {}},
    // Guerrilla's human ragdoll (its numbers from its code, in Armageddon's ragdoll table's layout), which
    // the people's character entries name; the game reads every ragdoll table a character names at start-up.
    {"ragdoll_rfg_human.xtbl", "Guerrilla's ragdoll", {}},
    // Guerrilla's player's spine bend and arms (Armageddon's tables name Darius's bones; Mason's entries added).
    {"spinebend.xtbl", "Spine bends", {}},
    {"ik_limbs.xtbl", "Limbs", {}},
    // Guerrilla's sounds' banks (GuerrillaBuilder.exe; banks of Armageddon's kind, "rfg_<Guerrilla's
    // bank>", new files in overrides\): Armageddon's patch rows with ours. The game registers each bank
    // listed (its media opened by name, the bank loaded at boot), and posts their events by name.
    {"audio_banks_patch.xtbl", "Sound banks", {}},
    // Armageddon's managed layer sets with Guerrilla's added (GuerrillaBuilder.exe: the demolitions master's layers),
    // read once at start-up (0x4FF360 from 0x4FE6D0), so only through this copy of table.vpp_pc, not a loose file.
    {"rf4_layer_definition.xtbl", "Managed layer sets", {}},
};
BuiltTable& g_gameplayTable = g_builtTables[0];
constexpr char kGameplayNames[] = "Gameplay Property Names";
constexpr DWORD kGameplayNameRoom = 0x10000;  // the world builder's GuerrillaGameplay.NamePool
std::vector<char> g_gameplayNames;

// The scene renderer's sky step: a method (the renderer in ECX).
constexpr DWORD kRenderSkybox = 0xD528E0;
constexpr size_t kRenderSkyboxPrologue = 6;  // push ebp / mov ebp,esp / sub esp,3Ch
using RenderSkyboxFn = void(__fastcall*)(void* renderer, void* unused);
RenderSkyboxFn g_renderSkybox;

// Armageddon lights with "inferred lighting": surfaces first draw their normals (the G-buffer),
// then every lamp adds its light into a light buffer, then each surface draws again as its colour
// times the light buffer (the terrain's ir_terrain_height_mesh_t). Nothing else lights it: the
// global ambient pass (ir_light_ambient) is set up but never drawn. The light pass is the scene
// renderer's method 12 (rl_pc_scene_renderer, 0xEF8BB0, "IR Light Pass"): it binds the light buffer
// as the render target and the G-buffer as textures (slot 6 = t13 the normals, 7 = t14 the depth,
// 8 = t9; table 0x111A348), clears it, and draws each lamp (0xEF67D0).
constexpr DWORD kLightPass = 0xEF8BB0;
constexpr size_t kLightPassPrologue = 5;  // push ebp / mov ebp,esp / push -1
using LightPassFn = void(__fastcall*)(void* renderer, void* unused);
LightPassFn g_lightPass;

// The exposure: Armageddon's tone mapping (rl_hdr) adapts its exposure toward Desired_brightness ÷
// the picture's brightness, held between Exposure_min and _max; its tone-mapping step (0xD22C60, the
// renderer in ECX, wrapped by the console mod) uploads them from the renderer's +0xAD8, +0xAE0 and
// +0xAE4, which the environment boxes set (ruin01's holds it at 3). The step's one caller (0xC45130,
// the compositor's tone-mapping constants, the same renderer in ECX) is wrapped here instead.
constexpr DWORD kToneMappingConstants = 0xC45130;
constexpr size_t kToneMappingConstantsPrologue = 6;  // push ebp / mov ebp,esp / sub esp,3Ch
using ToneMappingConstantsFn = void(__fastcall*)(void* renderer, void* unused);
ToneMappingConstantsFn g_toneMappingConstants;

// The compositor's last step, one of two (0xC46C10, 0xC471C0; the frame runs one, a method with one
// flag): the picture tone-mapped into its final target (8-bit; from the scene, 16-bit float, at t0, the
// bloom at t5, and with 0xC46C10 its colour table at t6). After it, in our worlds, the mod draws
// Guerrilla's tone mapping over its picture.
constexpr DWORD kFinalComposite[2] = {0xC46C10, 0xC471C0};
constexpr size_t kFinalCompositePrologue = 5;  // push ebp / mov ebp,esp / push -1
using FinalCompositeFn = void(__fastcall*)(void* compositor, void* unused, int flag);
FinalCompositeFn g_finalComposite[2];

// A small check the light pass makes once all that is bound: in each lamp's drawing and after the
// last lamp (0xD574A0: is a lighting debug view on). Where the mod notes the light buffer and normals.
constexpr DWORD kLightCheck = 0xD574A0;
constexpr size_t kLightCheckPrologue = 11;  // push ebp / mov ebp,esp / push ecx / cmp dword ptr [0x306B570],0Ch
using LightCheckFn = bool(__cdecl*)();
LightCheckFn g_lightCheck;

// A colour curve now, as Guerrilla's game_time_of_day_render_update blends one over the loaded sets
// (the districts round the camera): red, green, blue and the brightness each added up by the sets'
// weights, then the colour times the brightness. (Adding up each set's colour times its own
// brightness, as before, came out wrong near district borders: down to a third of the sun there.)
void TodColour(size_t offset, float (&out)[3]) {
    float sum[4] = {0, 0, 0, 0};
    for (int set = 0; set < 4; ++set) {
        if (!g_todSets[set] || g_todWeights[set] == 0) continue;
        float value[4] = {0, 0, 0, 0};
        ColourAt(g_todSets[set] + offset, nullptr, value, g_timeOfDay);
        for (int i = 0; i < 4; ++i) sum[i] += value[i] * g_todWeights[set];
    }
    for (int i = 0; i < 3; ++i) out[i] = sum[i] * sum[3];
}

// A number curve now, the same way.
float TodNumber(size_t offset) {
    float total = 0;
    for (int set = 0; set < 4; ++set)
        if (g_todSets[set] && g_todWeights[set] != 0) total += NumberAt(g_todSets[set] + offset, nullptr, g_timeOfDay) * g_todWeights[set];
    return total;
}

// ---- Settings

bool g_skyOn = true;
bool g_cloudsOn = true;
std::vector<std::string> g_worlds;

// How bright the sky and clouds are: their time-of-day colours (Guerrilla's own, which Guerrilla's
// renderer shows brighter than Armageddon's does) times this. Read again whenever rfa_mods.ini is
// saved, so it can be tuned while playing.
constexpr float kDefaultBrightness = 5.0f;
float g_brightness = kDefaultBrightness;
FILETIME g_iniWritten = {};
ULONGLONG g_lastIniCheck = 0;
bool g_tellColours = true;  // log the colours at the next draw

void ReadBrightness() {
    const float brightness = static_cast<float>(_wtof(rfa::IniString(L"GuerrillaWorld", L"Brightness", L"5").c_str()));
    g_brightness = brightness > 0 ? brightness : kDefaultBrightness;
}

// How fast the game clock runs in our worlds, in game seconds a real second (48: a day in half an
// hour); 0 leaves the clock alone. Also read again whenever rfa_mods.ini is saved.
constexpr float kDefaultClockSpeed = 48.0f;
float g_clockSpeed = kDefaultClockSpeed;
bool g_clockOurs = false;      // the clock runs at our speed
float g_clockSpeedBefore = 0;  // the game's own speed, put back on leaving

void ReadClockSpeed() {
    const float speed = static_cast<float>(_wtof(rfa::IniString(L"GuerrillaWorld", L"ClockSpeed", L"48").c_str()));
    g_clockSpeed = speed >= 0 ? speed : kDefaultClockSpeed;
}

// How wide our worlds' zones are, in metres: the ported world's are Guerrilla's, 511; 0 leaves the
// game's own (255). Read once: a world's files are built for one or the other.
float g_zoneWidth = 511.0f;

void ReadZoneSize() {
    const float width = static_cast<float>(_wtof(rfa::IniString(L"GuerrillaWorld", L"ZoneSize", L"511").c_str()));
    g_zoneWidth = width >= 0 ? width : 511.0f;
}

float OurZoneWidth() { return g_zoneWidth > 0 ? g_zoneWidth : 255.0f; }

// How far around the player our worlds' zones stay loaded, in metres (the zones' layer, 1); 0
// leaves the game's own. Also read again whenever rfa_mods.ini is saved.
constexpr int kSubzoneLayer = 0, kZoneLayer = 1;
constexpr float kDefaultStreamRadius = 500.0f;
float g_streamRadius = kDefaultStreamRadius;
bool g_streamOurs = false;        // the zones' layer streams at our radius
float g_streamRadiusBefore = 0;   // the game's own radius, put back on leaving
void* g_streamLayers = nullptr;   // the layers we set it in (a level load makes new ones)

void ReadStreamRadius() {
    const float radius = static_cast<float>(_wtof(rfa::IniString(L"GuerrillaWorld", L"StreamRadius", L"500").c_str()));
    g_streamRadius = radius >= 0 ? radius : kDefaultStreamRadius;
}

// How far the camera may move, in metres across the ground, before what is loaded around it moves too
// (StreamSteady=; see kStreamUpdate); 0 moves it every frame, as both games do. Also read again
// whenever rfa_mods.ini is saved.
constexpr float kDefaultStreamSteady = 25.0f;
float g_streamSteady = kDefaultStreamSteady;

void ReadStreamSteady() {
    const float steady = static_cast<float>(_wtof(rfa::IniString(L"GuerrillaWorld", L"StreamSteady", L"25").c_str()));
    g_streamSteady = steady >= 0 ? steady : kDefaultStreamSteady;
}

// How far the camera draws in our worlds, in metres (FarClip=; see ApplyEnvSettings; also read again whenever
// rfa_mods.ini is saved). Guerrilla's camera draws to 475 m (camera_system_init's far clip), inside the 510 m its
// zones stay loaded around it, so zones and buildings load out of sight in its haze. Farther shows more, but also
// the world's edge and buildings appearing as they load. Held to 475-1500 (Guerrilla's night sky is drawn 474 m
// out; past 1,500 m nothing is loaded); 0 = the zones' own far clip, from their environment boxes.
constexpr float kGuerrillaFarClip = 475.0f, kMostFarClip = 1500.0f;
float g_farClip = kGuerrillaFarClip;

void ReadFarClip() {
    const float farClip = static_cast<float>(_wtof(rfa::IniString(L"GuerrillaWorld", L"FarClip", L"475").c_str()));
    g_farClip = farClip <= 0 ? 0 : std::clamp(farClip, kGuerrillaFarClip, kMostFarClip);
}

// Whether our worlds draw their ground as far as Guerrilla does (GroundDetail=; see RunGroundDetail).
// Also read again whenever rfa_mods.ini is saved.
bool g_groundDetail = true;
void ReadGroundDetail() { g_groundDetail = rfa::IniInt(L"GuerrillaWorld", L"GroundDetail", 1) != 0; }

// The most a layer's radius may be in our worlds: under two of its cells (sub-zones a third of a
// zone, zones a zone), so its box meets at most 5 × 5 cells, 25 of the 35 a layer tracks (a box
// exactly four cells wide can meet six a side; Guerrilla's size makes the sub-zones' own radius,
// 511 × 0.6667, just that).
float MostRadius(int layer) {
    const float cell = layer == kSubzoneLayer ? OurZoneWidth() / 3 : OurZoneWidth();
    return 2 * cell - 1;
}

// How bright the sky's light is in our worlds: the time of day's ambient light times this. Also
// read again whenever rfa_mods.ini is saved.
float g_ambient = 1.0f;
bool g_tellAmbient = true;  // log the ambient light at the next frame in our world

void ReadAmbient() {
    const float ambient = static_cast<float>(_wtof(rfa::IniString(L"GuerrillaWorld", L"Ambient", L"1").c_str()));
    g_ambient = ambient >= 0 ? ambient : 1.0f;
}

// Whether the mod lights our worlds itself (the sky's light and the sun, in the light buffer), and
// how bright the sun is: tod_light_color times this (read again whenever rfa_mods.ini is saved).
bool g_lightOn = true;
float g_sun = 1.0f;

void ReadSun() {
    const float sun = static_cast<float>(_wtof(rfa::IniString(L"GuerrillaWorld", L"Sun", L"1").c_str()));
    g_sun = sun >= 0 ? sun : 1.0f;
}

// Guerrilla's colour correction, the time of day's color_correct_tint: Guerrilla multiplies its whole
// picture by it, divided by its brightest channel (TOD_tint_primary_color), and ignores its fourth
// number; before that, its saturation (color_correct_saturation) about its luminance. Its light is
// pink and the tint cyan, so only the two together give its colours. Guerrilla does both in its last
// pass (rl_hdr 21), on the scene alone, before its bloom and particles go over it. Armageddon doesn't
// colour its picture by the time of day's tint (its own sets all carry the same grey), so in our
// worlds the mod does it in its own last pass with Guerrilla's tone mapping (TonePixel), and without
// that, multiplies what it draws itself by it (PartTint). ColourCorrection=0 leaves it out (read
// again whenever rfa_mods.ini is saved).
constexpr size_t kColourCorrection = 0x47A0, kColourSaturation = 0x46AC;
bool g_colourCorrection = true;

void ReadColourCorrection() {
    g_colourCorrection = _wtoi(rfa::IniString(L"GuerrillaWorld", L"ColourCorrection", L"1").c_str()) != 0;
}

// The colour correction now: every loaded set's tint at the time of day times the set's weight,
// added up, divided by its brightest channel; white when it is off.
void ColourCorrection(float (&out)[3]) {
    out[0] = out[1] = out[2] = 1;
    if (!g_colourCorrection) return;
    float tint[3] = {0, 0, 0};
    for (int set = 0; set < 4; ++set) {
        if (!g_todSets[set] || g_todWeights[set] == 0) continue;
        float value[4] = {0, 0, 0, 0};
        ColourAt(g_todSets[set] + kColourCorrection, nullptr, value, g_timeOfDay);
        for (int i = 0; i < 3; ++i) tint[i] += value[i] * g_todWeights[set];
    }
    float brightest = tint[0];
    if (tint[1] > brightest) brightest = tint[1];
    if (tint[2] > brightest) brightest = tint[2];
    if (brightest <= 0) return;
    for (int i = 0; i < 3; ++i) out[i] = tint[i] / brightest;
}

// Guerrilla's fog, from the time of day's curves (in our sets as Guerrilla's): its colour, its
// density, the density's fall-off with height (atmosphere scale), the fog's ground, an offset into
// the distance, and the range that ground is held to around the camera. How thick it is: the density
// times Fog= (0: none; read again whenever rfa_mods.ini is saved).
constexpr size_t kFogColour = 0x2700, kFogGround = 0x30C0, kFogAtmosphereScale = 0x31B4, kFogDensity = 0x32A8,
                 kFogDensityOffset = 0x339C, kFogCameraMin = 0x6E04, kFogCameraMax = 0x6EF8;
float g_fog = 1.0f;
bool g_tellFog = true;  // log the fog at its next draw

// Guerrilla fogs its sky too: the dome, the clouds and the backdrop, each as if it were a surface
// stretched across to Guerrilla's far clip (1,000 m at full detail, game_render_set_lod_level) and up
// to the time of day's fog scale, raised by its fog offset (fog_skybox_, fog_clouds_ and fog_matte_
// offset and scale; Guerrilla's defaults 100 and 100, the backdrop's offset 10). Guerrilla's world
// has its fog follow the camera (fog_camera_follow), which takes the camera's height off the offsets.
constexpr size_t kSkyFogOffset = 0x6FEC, kSkyFogScale = 0x70E0, kCloudsFogOffset = 0x71D4, kCloudsFogScale = 0x72C8,
                 kMatteFogOffset = 0x73BC, kMatteFogScale = 0x74B0;
constexpr float kSkyFogFar = 1000;

void ReadFog() {
    const float fog = static_cast<float>(_wtof(rfa::IniString(L"GuerrillaWorld", L"Fog", L"1").c_str()));
    g_fog = fog >= 0 ? fog : 1.0f;
}

// Guerrilla's tone mapping in our worlds (ToneMapping=1): Armageddon's exposure is held at 1, so the
// picture holds Guerrilla's own values, and the mod turns it into the final picture as Guerrilla does
// (see DrawToneMapping), from the time of day's curves (in our sets as Guerrilla's). Exposure=
// multiplies Guerrilla's exposure (1 = Guerrilla's). ToneMapping=0 leaves Armageddon's own (the
// environment box's exposure, its curve and colour table). Both read again whenever rfa_mods.ini is saved.
constexpr size_t kLumRange = 0x44C4, kLumOffset = 0x45B8, kEyeAdaptionBase = 0x3B3C, kEyeAdaptionAmount = 0x3C30, kIrisRate = 0x376C,
                 kLuminanceMin = 0x3954, kLuminanceMax = 0x3860, kBloomAmount = 0x40F4;
bool g_toneMapping = true;
bool g_toneMappingFailed = false;
float g_exposure = 1;

void ReadToneMapping() {
    g_toneMapping = rfa::IniInt(L"GuerrillaWorld", L"ToneMapping", 1) != 0;
    const float exposure = static_cast<float>(_wtof(rfa::IniString(L"GuerrillaWorld", L"Exposure", L"1").c_str()));
    g_exposure = exposure > 0 ? exposure : 1;
}

bool GuerrillaToneMapping() { return g_toneMapping && !g_toneMappingFailed; }

// The colour correction what the mod draws itself (its light, the sky, clouds, sun, backdrop, fog and
// the colours our shaders light by) takes: none with Guerrilla's tone mapping, whose last pass
// corrects the whole picture at once (Armageddon's lamps, glows, effects and people with it), as
// Guerrilla's does; otherwise the colour correction, as there is no other place for it.
void PartTint(float (&out)[3]) {
    if (GuerrillaToneMapping()) {
        out[0] = out[1] = out[2] = 1;
        return;
    }
    ColourCorrection(out);
}

// Guerrilla's particle layer with its tone mapping (Particles=; read again whenever rfa_mods.ini is
// saved): see ParticlePhase. 0 = particles drawn into the scene as Armageddon draws them.
bool g_particles = true;

void ReadParticles() { g_particles = rfa::IniInt(L"GuerrillaWorld", L"Particles", 1) != 0; }

// Guerrilla's fog on particles (ParticleFog=; read again whenever rfa_mods.ini is saved): see
// "Guerrilla's fog on particles". 0: none, as Armageddon draws them in our worlds.
bool g_particleFog = true;

void ReadParticleFog() { g_particleFog = rfa::IniInt(L"GuerrillaWorld", L"ParticleFog", 1) != 0; }

// Guerrilla's dust round the camera (Dust=; read again whenever rfa_mods.ini is saved): see DustUpdate.
// 0: none.
bool g_dust = true;

void ReadDust() { g_dust = rfa::IniInt(L"GuerrillaWorld", L"Dust", 1) != 0; }

// Guerrilla's sun shafts, drawn with its tone mapping: how strong, times Guerrilla's own (SunShafts=;
// 0: none; read again whenever rfa_mods.ini is saved).
float g_sunShafts = 1.0f;
bool g_shaftsFailed = false;

void ReadSunShafts() {
    const float shafts = static_cast<float>(_wtof(rfa::IniString(L"GuerrillaWorld", L"SunShafts", L"1").c_str()));
    g_sunShafts = shafts >= 0 ? shafts : 1.0f;
}

// How strong the sun's shine on surfaces is (Shine=; 1 as drawn so far, 0 none; read again whenever
// rfa_mods.ini is saved): the highlight the mod's light writes for every surface, which each material
// then takes times its specular map and colour (see kLightShader).
float g_shine = 1.0f;

void ReadShine() {
    const float shine = static_cast<float>(_wtof(rfa::IniString(L"GuerrillaWorld", L"Shine", L"1").c_str()));
    g_shine = shine >= 0 ? shine : 1.0f;
}

// How strong the glow round bright things is, with Guerrilla's tone mapping (Bloom=; times the time of
// day's bloom_amount; 1 as drawn so far, 0 none; read again whenever rfa_mods.ini is saved). The glow
// itself is Armageddon's bright pass's (see DrawToneMapping).
float g_bloom = 1.0f;

void ReadBloom() {
    const float bloom = static_cast<float>(_wtof(rfa::IniString(L"GuerrillaWorld", L"Bloom", L"1").c_str()));
    g_bloom = bloom >= 0 ? bloom : 1.0f;
}

bool SunShaftsOn() { return GuerrillaToneMapping() && g_sunShafts > 0 && !g_shaftsFailed; }

// How strong the sun's corona is (SunCorona=; times Guerrilla's own; 0: none; read again whenever
// rfa_mods.ini is saved): see "The sun's corona".
float g_sunCorona = 1.0f;

void ReadSunCorona() {
    const float corona = static_cast<float>(_wtof(rfa::IniString(L"GuerrillaWorld", L"SunCorona", L"1").c_str()));
    g_sunCorona = corona >= 0 ? corona : 1.0f;
}

// Sharpening of the final picture in our worlds (Sharpen=, 0-100; 0: none; read again whenever
// rfa_mods.ini is saved): see "Sharpening".
float g_sharpen = 0.4f;
bool g_sharpenFailed = false;

void ReadSharpen() {
    const int amount = rfa::IniInt(L"GuerrillaWorld", L"Sharpen", 40);
    g_sharpen = (amount < 0 ? 0 : amount > 100 ? 100 : amount) / 100.0f;
}

// The sun's shadows (SunShadows=; read again whenever rfa_mods.ini is saved): the sun's light drawn
// through the shadow map of the sun's lamp (see "The sun's shadows"). 0: it lights through everything.
bool g_sunShadows = true;

void ReadSunShadows() { g_sunShadows = rfa::IniInt(L"GuerrillaWorld", L"SunShadows", 1) != 0; }

// How bright the sky, clouds and backdrop are drawn: their time-of-day colours times Brightness=; with
// Guerrilla's tone mapping the picture holds Guerrilla's own values, so there the default 5 is
// Guerrilla's own level (× 1).
float SkyBrightness() { return GuerrillaToneMapping() ? g_brightness / kDefaultBrightness : g_brightness; }

// The fog's numbers now, as Guerrilla works them out each frame (game_render_setup_frame): x the
// density's fall-off with height k (the atmosphere scale, never quite 0); y the density at the
// camera's height, in powers of two (× 1/ln 2), e^(−k × the camera's height over the fog's ground)
// times the density, held to 0–1000; w the offset. Guerrilla's world (its terr01.gtodx,
// fog_camera_follow) keeps the fog's ground at the camera's height plus fog_ground, held between
// fog_camera_min and _max, so the camera's own height drops out.
void FogNumbers(float (&out)[4]) {
    const float k = TodNumber(kFogAtmosphereScale) + 0.00001f;
    float ground = TodNumber(kFogGround);
    const float lowest = TodNumber(kFogCameraMin), highest = TodNumber(kFogCameraMax);
    if (ground > highest) ground = highest;
    if (ground < lowest) ground = lowest;
    float density = TodNumber(kFogDensity) * g_fog * 1.442695f * expf(k * ground);
    if (density < 0) density = 0;
    if (density > 1000) density = 1000;
    out[0] = k;
    out[1] = density;
    out[2] = 0;
    out[3] = TodNumber(kFogDensityOffset);
}

void BuildingsSettingsChanged();  // (below, with Guerrilla's buildings)

// Once a second: if rfa_mods.ini was saved since, read the brightness, ambient light, sun, colour
// correction, fog, exposure, sun shafts, clock speed and the buildings' switches again.
void CheckSettings() {
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastIniCheck < 1000) return;
    g_lastIniCheck = now;
    WIN32_FILE_ATTRIBUTE_DATA ini;
    if (!GetFileAttributesExW(rfa::IniPath().c_str(), GetFileExInfoStandard, &ini)) return;
    if (CompareFileTime(&ini.ftLastWriteTime, &g_iniWritten) == 0) return;
    const bool first = g_iniWritten.dwLowDateTime == 0 && g_iniWritten.dwHighDateTime == 0;
    g_iniWritten = ini.ftLastWriteTime;
    if (first) return;
    const float before = g_brightness;
    ReadBrightness();
    if (g_brightness != before) {
        rfa::Log("Sky: brightness %.3g (was %.3g)", g_brightness, before);
        g_tellColours = true;
    }
    const float ambientBefore = g_ambient;
    ReadAmbient();
    if (g_ambient != ambientBefore) {
        rfa::Log("Ground: ambient light times %.3g (was %.3g)", g_ambient, ambientBefore);
        g_tellAmbient = true;
    }
    const float sunBefore = g_sun;
    ReadSun();
    if (g_sun != sunBefore) {
        rfa::Log("Ground: sunlight times %.3g (was %.3g)", g_sun, sunBefore);
        g_tellAmbient = true;
    }
    const bool correctionBefore = g_colourCorrection;
    ReadColourCorrection();
    if (g_colourCorrection != correctionBefore) {
        rfa::Log("Colour correction (Guerrilla's tint) %s", g_colourCorrection ? "on" : "off");
        g_tellAmbient = g_tellColours = g_tellFog = true;
    }
    const float fogBefore = g_fog;
    ReadFog();
    if (g_fog != fogBefore) {
        rfa::Log("Fog: thickness times %.3g (was %.3g)", g_fog, fogBefore);
        g_tellFog = true;
    }
    const float exposureBefore = g_exposure;
    const bool toneMappingBefore = g_toneMapping;
    ReadToneMapping();
    if (g_toneMapping != toneMappingBefore) {
        rfa::Log("Tone mapping: %s", g_toneMapping ? "Guerrilla's" : "Armageddon's own");
        g_tellColours = true;
    }
    if (g_exposure != exposureBefore) rfa::Log("Tone mapping: exposure times %.3g (was %.3g)", g_exposure, exposureBefore);
    const float shaftsBefore = g_sunShafts;
    ReadSunShafts();
    ReadSharpen();
    if (g_sunShafts != shaftsBefore) rfa::Log("Sun shafts: times %.3g (was %.3g)", g_sunShafts, shaftsBefore);
    const bool shadowsBefore = g_sunShadows;
    ReadSunShadows();
    if (g_sunShadows != shadowsBefore) rfa::Log("Sun's shadows: %s", g_sunShadows ? "on" : "off");
    const bool particlesBefore = g_particles;
    ReadParticles();
    if (g_particles != particlesBefore) rfa::Log("Particles: %s", g_particles ? "in a layer of their own, as Guerrilla's" : "drawn into the scene, as Armageddon's");
    const bool particleFogBefore = g_particleFog;
    ReadParticleFog();
    if (g_particleFog != particleFogBefore) rfa::Log("Particles: %s", g_particleFog ? "fogged as Guerrilla's" : "not fogged");
    const bool dustBefore = g_dust;
    ReadDust();
    if (g_dust != dustBefore) rfa::Log("Dust: %s", g_dust ? "on" : "off");
    const float clockBefore = g_clockSpeed;
    ReadClockSpeed();
    if (g_clockSpeed != clockBefore && g_clockOurs) {
        g_gameClockSpeed = g_clockSpeed > 0 ? g_clockSpeed : g_clockSpeedBefore;
        g_clockOurs = g_clockSpeed > 0;
        rfa::Log("Clock: speed %.3g (was %.3g)", g_gameClockSpeed, clockBefore);
    }
    const float radiusBefore = g_streamRadius;
    ReadStreamRadius();
    if (g_streamRadius != radiusBefore) rfa::Log("Streaming: radius %.0f m (was %.0f; 0 = the game's own)", g_streamRadius, radiusBefore);
    const float steadyBefore = g_streamSteady;
    ReadStreamSteady();
    if (g_streamSteady != steadyBefore) rfa::Log("Streaming: follows the camera once it has gone %.0f m (was %.0f; 0 = every frame)", g_streamSteady, steadyBefore);
    const float farClipBefore = g_farClip;
    ReadFarClip();
    if (g_farClip != farClipBefore) rfa::Log("View distance: %.0f m (was %.0f; 0 = the zones' own)", g_farClip, farClipBefore);
    ReadGroundDetail();
    const float shineBefore = g_shine;
    ReadShine();
    if (g_shine != shineBefore) rfa::Log("Light: the sun's shine times %.3g (was %.3g)", g_shine, shineBefore);
    const float bloomBefore = g_bloom;
    ReadBloom();
    if (g_bloom != bloomBefore) rfa::Log("Tone mapping: bloom times %.3g (was %.3g)", g_bloom, bloomBefore);
    const float coronaBefore = g_sunCorona;
    ReadSunCorona();
    if (g_sunCorona != coronaBefore) rfa::Log("Corona: times %.3g (was %.3g)", g_sunCorona, coronaBefore);
    BuildingsSettingsChanged();
}

bool InOurWorld() {
    const char* world = WorldName();
    for (const std::string& name : g_worlds)
        if (_stricmp(world, name.c_str()) == 0) return true;
    return false;
}

// The sun's path, as Guerrilla's time of day works it out (tod_shared_calculate_orbital_position with
// the sun orbital of its terr01.gtodx: a circle turned 118° about x and 180° about y, 4.5 hours on,
// which comes to this): it rises in the east (+x, where the sky's morning colours are) at 7:30,
// crosses at 13:30 at 62° up, leaning south (-z), and sets in the west at 19:30. Toward the sun, in
// the world.
void SunToward(float (&out)[3]) {
    const float pi = 3.14159265f;
    const float turn = 2 * pi * (g_timeOfDay - 13.5f) / 24.0f;  // 0 when highest, ±90° at rising and setting
    const float lean = 28.0f * pi / 180.0f;                     // the orbit plane's tilt from straight up
    out[0] = -sinf(turn);
    out[1] = cosf(turn) * cosf(lean);
    out[2] = -cosf(turn) * sinf(lean);
}

bool SunUp() {
    float toward[3];
    SunToward(toward);
    return toward[1] >= 0;
}

// Where Guerrilla's light comes from, which isn't the sun's disc: its time of day lights the world
// (and, with a lower floor, its clouds) from tod_shared_calcaulate_light_direction_at_time, which
// Armageddon keeps too (0x55A850) with the same constants: an arc that rises in the east at 7:30,
// crosses south at 13:30 and sets in the west at 19:30, at night mirrored back through the north,
// never lower than `leastDegrees` above the horizon (Tod_light_min_angle_to_horizon 40; the clouds 15),
// turned about y by TOD_rot_around_y (0 in Guerrilla's world). Toward the light, in the world.
void LightToward(float hours, float leastDegrees, float (&out)[3]) {
    const float pi = 3.14159265f;
    float t = hours - 7.5f;
    if (t < 0) t += 24;
    const float a = pi * (t / 12 - 1);
    float y = fabsf(sinf(a));
    const float least = sinf(leastDegrees * pi / 180);
    if (y < least) y = least;
    if (y > 1) y = 1;
    const float v[3] = {-cosf(a), y, 0.53f * sinf(a)};
    const float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int i = 0; i < 3; ++i) out[i] = v[i] / length;
}

constexpr float kLightLeastDegrees = 40, kCloudLightLeastDegrees = 15;

// Re-Mars-tered's balance of the light, which the 2009 game doesn't have (none of these numbers are
// in its exe); the rfg world is lit as Re-Mars-tered lights Guerrilla's. Its renderer takes the time of
// day's ambient light from above × 0.65 (rl_renderer::set_ambient_color), the one from below × 0.29
// (set_back_ambient_color), and the light of its base pass, the sun's or the moon's, × 0.8
// (rl_renderer::set_light, as begin_base_pass calls it); lamps keep theirs.
constexpr float kRemasteredAmbient = 0.65f, kRemasteredBackAmbient = 0.29f, kRemasteredSunLight = 0.8f;

// The light that shines from one direction: tod_light_color (Guerrilla's: the sun's by day, blue
// moonlight by night) times Re-Mars-tered's 0.8 and Sun=, from Guerrilla's light direction
// (LightToward: never under 40°, the moon's half of the day through the north), at full strength all
// day as Guerrilla's (its colour curves dim it at dusk themselves). Toward the light, and the light.
void DirectLight(float (&toward)[3], float (&out)[3]) {
    LightToward(g_timeOfDay, kLightLeastDegrees, toward);
    TodColour(kSunColour, out);
    for (float& c : out) c *= kRemasteredSunLight * g_sun;
}

// ---- The sun's shadows
// Armageddon can't draw a light that shines from one direction over the whole map (all its light
// shaders are lamps'), but its light pass renders a lamp's shadow map, and it keeps a lamp for a sun's
// shadow: the follow player light (lights.xtbl's FollowPlayerLight: a spot lamp with an orthographic
// shadow map only, to 30 m), which an environment box's follow_player_light switches on for a level.
// Each frame the game makes it if need be and puts it over the camera (Follow_player_light_offset_*,
// 20 m up), unturned, shining straight down; the lightning lamps of its outdoor maps (use_sun_shadows)
// borrow its shadow map. In our worlds the mod switches it on (choosing its template as such a box
// would), puts it along the sun instead, aimed back at the camera, and has it drawn (not its map only)
// with next to no light of its own; when the light pass has drawn it (its shadow map just rendered and
// still bound), the mod draws the sun's light through that map (DrawLamp, below), and the sky's light
// after the pass as before.
// The follow light's template (an index into lights.xtbl's lights, -1 for none: the environment's
// choice, cleared at each level's start) and its object's handle.
auto& g_followLightTemplate = *reinterpret_cast<int*>(0x1102DE4);
auto& g_followLightHandle = *reinterpret_cast<DWORD*>(0x1101C10);
// lights.xtbl's light of a name: its index, or -1.
using FindLightTemplateFn = int(__cdecl*)(const char* name);
auto const FindLightTemplate = reinterpret_cast<FindLightTemplateFn>(0x7E82F0);
// Each frame, the player's update of its lights: the follow light put where it goes (0x7E8A80) while
// a template is chosen, else dimmed to nothing.
constexpr DWORD kFollowLights = 0x8B8C30;
constexpr size_t kFollowLightsPrologue = 6;  // push ebp / mov ebp,esp / sub esp,44h
using FollowLightsFn = void(__cdecl*)(void* player);
FollowLightsFn g_followLights;
// Makes the follow light if need be (from the template) and puts it at a place, turned so (a world
// matrix's rows right, up and forward; a spot lamp shines down its up).
struct Place { float at[3]; };
struct Turn { float rows[9]; };
constexpr DWORD kPlaceFollowLight = 0x7E8A80;
constexpr size_t kPlaceFollowLightPrologue = 6;  // push ebp / mov ebp,esp / sub esp,10h
using PlaceFollowLightFn = void(__cdecl*)(Place place, Turn turn, int lightTemplate);
PlaceFollowLightFn g_placeFollowLight;
// Finds a lamp object by its handle (the world, 0x1870630, in ECX), or null. A lamp object: its colour
// at +0x10C (its light's colour is that times its intensity, +0x128), its light (rl_light) at +0x194.
// The light's flags at +0x15C: 1 it casts shadows, 2 it only renders its shadow map (0x7E5170 sets it).
using FindLampFn = BYTE*(__fastcall*)(void* world, void* unused, DWORD handle);
auto const FindLamp = reinterpret_cast<FindLampFn>(0x61C100);
constexpr size_t kLampColour = 0x10C, kLampLight = 0x194;
using SetShadowMapOnlyFn = void(__fastcall*)(void* light, void* unused, bool only);
auto const SetShadowMapOnly = reinterpret_cast<SetShadowMapOnlyFn>(0x7E5170);
// The light's colour (+0xF0: red, green, blue and a fourth, 1).
using SetLightColourFn = void(__fastcall*)(void* light, void* unused, const float* colour);
auto const SetLightColour = reinterpret_cast<SetLightColourFn>(0x52F050);
// lights.xtbl's lights as the game keeps them (0x2A0 bytes each, from 0x1CD07FC): a lamp made from one
// copies its reach (atten_start +0x50, atten_range +0x54, metres), its cone (hotspot_size +0x5C,
// hotspot_falloff_size +0x60, degrees) and its shadow distance (+0x70), and its light's shadow setup
// is fixed then: an orthographic shadow map (4096 × 4096) as wide as the reach × tan(hotspot +
// falloff) and as deep as the reach from the lamp, its shadows fading out over the last fifth of
// the shadow distance from the camera (the template's 80 m, 50° and 30 m made a map 95 m across and
// 80 m deep). So while the game makes the follow light in our worlds the mod gives its template the
// sun's, and puts the template's own back straight after.
auto& g_lightTemplates = *reinterpret_cast<BYTE**>(0x1CD07FC);
constexpr size_t kLightTemplateSize = 0x2A0;
constexpr size_t kTemplateNumbers[5] = {0x50, 0x54, 0x5C, 0x60, 0x70};  // reach start, reach range, hotspot, falloff, shadow distance
// The sun's: the lamp 150 m along the sun with a reach of 400 m (so its map holds what stands
// between the sun and the camera and beyond), a cone of 27° (a map about 204 m across, 5 cm a
// texel), the shadows to 100 m round the camera, fading from 80 m: as far as Guerrilla's
// (build_shadow_params_base_clb: shadow_max_dist 100, shadow_fade_start 0.8).
constexpr float kSunLampDistance = 150.0f;
constexpr float kSunTemplateNumbers[5] = {400.0f, 10.0f, 21.0f, 6.0f, 100.0f};

BYTE* g_sunLight = nullptr;          // the follow light's light while it gives the sun its shadows
bool g_followLightOurs = false;      // its template chosen by the mod
BYTE* g_followLightSeen = nullptr;   // the follow light last logged

void __cdecl FollowLights(void* player) {
    if (!InOurWorld()) {
        g_followLightOurs = false;  // (the game clears the template at each level's start)
        g_sunLight = nullptr;
    } else if (g_sunShadows) {
        if (g_followLightTemplate < 0) {
            g_followLightTemplate = FindLightTemplate("FollowPlayerLight");
            g_followLightOurs = g_followLightTemplate >= 0;
            if (g_followLightOurs)
                RFA_LOG_FIRST(1, "Sun's shadows: the follow player light switched on in world \"%s\" (lights.xtbl's light %d)", WorldName(), g_followLightTemplate);
            else
                RFA_LOG_FIRST(1, "Sun's shadows: lights.xtbl has no FollowPlayerLight; the sun lights without shadows");
        }
    } else if (g_followLightOurs) {
        g_followLightTemplate = -1;  // dimmed to nothing by the game, as where no level asks for it
        g_followLightOurs = false;
        g_sunLight = nullptr;
    }
    // The template the sun's while the game may make the follow light from it, then its own again.
    float* numbers[5] = {};
    float own[5] = {};
    const bool sunTemplate = g_followLightOurs && g_followLightTemplate >= 0 && g_lightTemplates;
    if (sunTemplate)
        for (int i = 0; i < 5; ++i) {
            numbers[i] = reinterpret_cast<float*>(g_lightTemplates + g_followLightTemplate * kLightTemplateSize + kTemplateNumbers[i]);
            own[i] = *numbers[i];
            *numbers[i] = kSunTemplateNumbers[i];
        }
    g_followLights(player);
    if (sunTemplate)
        for (int i = 0; i < 5; ++i) *numbers[i] = own[i];
}

void __cdecl PlaceFollowLight(Place place, Turn turn, int lightTemplate) {
    const bool sun = g_followLightOurs && g_sunShadows && InOurWorld();
    if (sun) {
        // Its up along the light (the sun's, by night the moon's), so it shines back at the camera.
        float up[3], unused[3];
        DirectLight(up, unused);
        const float east[3] = {1, 0, 0}, north[3] = {0, 0, 1};
        const float* along = fabsf(up[2]) > 0.9f ? east : north;
        float right[3] = {up[1] * along[2] - up[2] * along[1], up[2] * along[0] - up[0] * along[2], up[0] * along[1] - up[1] * along[0]};
        const float length = sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
        for (float& v : right) v /= length;
        const float forward[3] = {right[1] * up[2] - right[2] * up[1], right[2] * up[0] - right[0] * up[2], right[0] * up[1] - right[1] * up[0]};
        for (int i = 0; i < 3; ++i) {
            turn.rows[i] = right[i];
            turn.rows[3 + i] = up[i];
            turn.rows[6 + i] = forward[i];
            place.at[i] = g_cameraPosition[i] + up[i] * kSunLampDistance;
        }
    }
    g_placeFollowLight(place, turn, lightTemplate);
    g_sunLight = nullptr;
    if (!sun) return;
    BYTE* lamp = FindLamp(reinterpret_cast<void*>(0x1870630), nullptr, g_followLightHandle);
    BYTE* light = lamp ? *reinterpret_cast<BYTE**>(lamp + kLampLight) : nullptr;
    if (!light) return;
    float* colour = reinterpret_cast<float*>(lamp + kLampColour);
    if (lamp != g_followLightSeen) {
        g_followLightSeen = lamp;
        rfa::Log("Sun's shadows: the follow player light %08X (object %p, light %p) along the sun", g_followLightHandle, lamp, light);
    }
    // Next to no light of its own: the object's colour, and its light's (that times the intensity,
    // set when the light is made).
    for (int i = 0; i < 3; ++i) colour[i] = 0.001f;
    const float faint[4] = {0.001f, 0.001f, 0.001f, 1.0f};
    SetLightColour(light, nullptr, faint);
    SetShadowMapOnly(light, nullptr, false);
    g_sunLight = light;
}

// Replace the time of day's ambient setters: the game's own, then in our worlds the world's
// ambient light times Re-Mars-tered's balance (kRemasteredAmbient, kRemasteredBackAmbient) and Ambient=.
void ScaleAmbient(float (&colour)[3], float remastered) {
    if (!InOurWorld()) return;
    for (float& c : colour) c *= remastered * g_ambient;
}

void __cdecl SetAmbient(const float* colour) {
    g_setAmbient(colour);
    ScaleAmbient(g_worldAmbient, kRemasteredAmbient);
}

void __cdecl SetBackAmbient(const float* colour) {
    g_setBackAmbient(colour);
    ScaleAmbient(g_worldBackAmbient, kRemasteredBackAmbient);
}

void TellAmbient() {
    if (!g_tellAmbient) return;
    g_tellAmbient = false;
    float toward[3], sun[3], tint[3];
    DirectLight(toward, sun);
    ColourCorrection(tint);
    rfa::Log("Ground at %.2f h: sky light from above %.3f %.3f %.3f, from below %.3f %.3f %.3f (Ambient=%.3g); "
             "%s toward %.2f %.2f %.2f, light %.3f %.3f %.3f (Sun=%.3g); colour correction %.3f %.3f %.3f",
             g_timeOfDay, g_worldAmbient[0], g_worldAmbient[1], g_worldAmbient[2], g_worldBackAmbient[0], g_worldBackAmbient[1],
             g_worldBackAmbient[2], g_ambient, SunUp() ? "sun" : "moon", toward[0], toward[1], toward[2], sun[0], sun[1], sun[2], g_sun,
             tint[0], tint[1], tint[2]);
}

// Each frame: in our worlds the game clock runs at ClockSpeed; in any other, at the speed it had.
void RunClock(bool ours) {
    if (ours && g_clockSpeed > 0 && !g_clockOurs) {
        g_clockSpeedBefore = g_gameClockSpeed;
        g_gameClockSpeed = g_clockSpeed;
        g_clockOurs = true;
        rfa::Log("Clock: running at %.3g game seconds a second in world \"%s\", a day in %.3g minutes (was %.3g)",
                 g_clockSpeed, WorldName(), 24 * 60 / g_clockSpeed, g_clockSpeedBefore);
    } else if (!ours && g_clockOurs) {
        g_gameClockSpeed = g_clockSpeedBefore;
        g_clockOurs = false;
        rfa::Log("Clock: back to the game's speed %.3g in world \"%s\"", g_clockSpeedBefore, WorldName());
    }
}

// ---- Armageddon's achievements and stats: off in our worlds (the user, 2026-11-19: "Yes, block them")
//
// Armageddon's stats (gamewide\stats.cpp) and its Steam achievements test nothing but that the profile owns its saves
// (0x557770), so play in our worlds counted as its campaign: kills, shots, salvage, time, and achievements such as
// SMP - Total Salvage, SP - Salvage Piles, SP - Flyer Bash Kills and the Nano ones could be won there. In our worlds
// both are off; everywhere else they run as the game's own.
// - Counting: every stat change goes through 0x553DC0 (cdecl: the stat's number, the value's address; a stat +1 is
//   0x554630, which only calls it; so do 0x554660, the "most" stats' 0x554690 and 0x554750, the salvage 0x8C9E70 and the
//   pods 0x8CEC80). A count also checks the stat's achievements (0x554330 → 0x554F80, Steam's award). Not held: the
//   setters 0x5549A0 and 0x554AC0, which only the mission system, the save ("last mission saved", "completion percent",
//   with a mission) and the profile's load call.
// - Awarding by name: 0x554F30 (cdecl bool: the name, two flags; the only caller of 0x555060): the missions' code, the
//   upgrades ("SMP - Upgrade Ring", "SMP - All Upgrades"), the scripts' sf_achievement_unlock, Infestation. In our worlds
//   it refuses (false, as for a name it doesn't know).
// - What reads the stats: the statistics screens, the menus' mission state (missions completed, game completed, last
//   mission saved), Ruin's map unlocks (its high scores), Infestation (waves, totals, hidden challenges), the telemetry
//   of a mission done and an upgrade bought (salvage total collected, rf4_gds_log_*). None of them is in our worlds'
//   way. The salvage the player spends is another number: 0x8C9E70 adds a pickup to the stats, then to its holder's +0x10
//   (held under the most at 0x227D9CC), which this leaves alone.
constexpr DWORD kStatAdd = 0x553DC0;
constexpr size_t kStatAddPrologue = 6;            // push ebp / mov ebp,esp / sub esp,34h
constexpr DWORD kAwardAchievement = 0x554F30;
constexpr size_t kAwardAchievementPrologue = 7;  // push ebp / mov ebp,esp / push ecx / mov eax,[ebp+8]
using StatAddFn = void(__cdecl*)(int stat, const void* value);
using AwardAchievementFn = bool(__cdecl*)(const char* name, bool b1, bool b2);
StatAddFn g_statAdd = nullptr;
AwardAchievementFn g_awardAchievement = nullptr;
bool g_statsOffSaid = false;  // the log line said for this stay in one of our worlds

void __cdecl StatAdd(int stat, const void* value) {
    if (InOurWorld()) return;
    g_statAdd(stat, value);
}

bool __cdecl AwardAchievement(const char* name, bool b1, bool b2) {
    if (InOurWorld()) {
        RFA_LOG_DETAIL_FIRST(8, "Achievements and stats: \"%s\" not awarded in world \"%s\"", name ? name : "?", WorldName());
        return false;
    }
    return g_awardAchievement(name, b1, b2);
}

// At the sky step, as the clock: one line on entering one of our worlds, one on leaving it.
void RunStatsBlock(bool ours) {
    if (!g_statAdd && !g_awardAchievement) return;
    if (ours && !g_statsOffSaid) {
        g_statsOffSaid = true;
        rfa::Log("Achievements and stats: Armageddon's off in world \"%s\"", WorldName());
    } else if (!ours && g_statsOffSaid) {
        g_statsOffSaid = false;
        rfa::Log("Achievements and stats: Armageddon's on again in world \"%s\"", WorldName());
    }
}

void InstallStatsBlock() {
    const BYTE addOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x34};
    const BYTE awardOpening[] = {0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08};
    if (memcmp(reinterpret_cast<const void*>(kStatAdd), addOpening, sizeof(addOpening)) == 0)
        g_statAdd = reinterpret_cast<StatAddFn>(rfa::HookFunction(kStatAdd, kStatAddPrologue, reinterpret_cast<void*>(&StatAdd)));
    if (memcmp(reinterpret_cast<const void*>(kAwardAchievement), awardOpening, sizeof(awardOpening)) == 0)
        g_awardAchievement = reinterpret_cast<AwardAchievementFn>(
            rfa::HookFunction(kAwardAchievement, kAwardAchievementPrologue, reinterpret_cast<void*>(&AwardAchievement)));
    if (!g_statAdd)
        rfa::Log("Achievements and stats: the game's stat counting isn't as expected; Armageddon's stats (and the achievements "
                 "they win) still count in our worlds");
    if (!g_awardAchievement)
        rfa::Log("Achievements and stats: the game's award by name isn't as expected; Armageddon's achievements awarded by name "
                 "can still be won in our worlds");
}

// The radius setter, wrapped: in our worlds, held to what each layer can track.
void __fastcall SetStreamRadiusHeld(void* manager, void* unused, int layer, float radius) {
    if ((layer == kSubzoneLayer || layer == kZoneLayer) && InOurWorld() && radius > MostRadius(layer)) {
        RFA_LOG_FIRST(2, "Streaming: layer %d's radius %.0f m held to %.0f m (more could meet more cells than the game tracks)",
                      layer, radius, MostRadius(layer));
        radius = MostRadius(layer);
    }
    g_setStreamRadius(manager, unused, layer, radius);
}

// What holds streaming up (a diagnosis, in our worlds). In the run of 2026-11-12 13:11 no zone or sub-zone
// loaded for 19 s while driving (the ground drawn from its colour map alone, the pause menu 11 s late behind
// the same queue), with no allocation failure logged. Two things can stop it without a word:
// - The stream I/O paused: 0x913180 ("pause stream_os procesing") and 0x9131D0 (unpause), called only by the
//   video player (its per-video step 0x8E1380 pauses while a playing video's buffer is low and resumes above
//   85%; video_player_process_all 0x8E0F60 resumes only in a frame with no video playing, 0x8E10A5), the
//   blocking load (0x908FC0) and the level load (0xA497E0). That window had video_player_process_all at
//   138 ms in one frame.
// - The level heap (level_memmgr 0x26B0320, 246 MB) too full for the next container: its allocator asks
//   can_alloc first (0x90B740) and the request waits. Its largest free block is 0xCB8470 (thiscall, under the
//   heap's own lock; the game prints it at 0x98FD0F).
// So each pause is timed with its caller, each video opened (bink_open 0x8DE840, cdecl: its second argument
// the file's name) is named, and every 10 s the heap's largest free block is logged with them.
constexpr DWORD kPauseStreamIo = 0x913180, kResumeStreamIo = 0x9131D0;
constexpr size_t kStreamIoPrologue = 5;  // push ebp / mov ebp,esp / push 0
constexpr DWORD kBinkOpen = 0x8DE840;
constexpr size_t kBinkOpenPrologue = 9;  // push ebp / mov ebp,esp / sub esp,0A0h
using StreamIoFn = void(__cdecl*)();
using BinkOpenFn = void*(__cdecl*)(void* first, const char* name, DWORD flags, DWORD fourth);
using LargestFreeFn = DWORD(__fastcall*)(void* heap, void* unused);
StreamIoFn g_pauseStreamIo, g_resumeStreamIo;
BinkOpenFn g_binkOpen;
auto const LargestFree = reinterpret_cast<LargestFreeFn>(0xCB8470);
void* const kLevelHeap = reinterpret_cast<void*>(0x26B0320);
DWORD g_streamPausedAt = 0;         // GetTickCount at the pause now in force (0 none)
void* g_streamPausedBy = nullptr;   // its caller
LONG g_streamPauses = 0, g_videosOpened = 0;
DWORD g_streamPausedMs = 0, g_streamPausedLongest = 0;
void* g_streamPausedLongestBy = nullptr;
DWORD g_streamWatchAt = 0;

void __cdecl PauseStreamIo() {
    if (InOurWorld() && !g_streamPausedAt) {
        g_streamPausedAt = GetTickCount() | 1;
        g_streamPausedBy = _ReturnAddress();
        InterlockedIncrement(&g_streamPauses);
    }
    g_pauseStreamIo();
}

void __cdecl ResumeStreamIo() {
    if (g_streamPausedAt) {
        const DWORD ms = GetTickCount() - g_streamPausedAt;
        g_streamPausedMs += ms;
        if (ms > g_streamPausedLongest) {
            g_streamPausedLongest = ms;
            g_streamPausedLongestBy = g_streamPausedBy;
        }
        if (ms >= 1000)
            RFA_LOG_FIRST(100, "Streaming: the stream I/O was paused %.1f s (by the code at %p; resumed by %p)", ms / 1000.0,
                          g_streamPausedBy, _ReturnAddress());
        g_streamPausedAt = 0;
    }
    g_resumeStreamIo();
}

void* __cdecl BinkOpen(void* first, const char* name, DWORD flags, DWORD fourth) {
    if (InOurWorld()) {
        InterlockedIncrement(&g_videosOpened);
        RFA_LOG_DETAIL_FIRST(100, "Streaming: a video opened: %.120s (flags %08lX)", name ? name : "(no name)", flags);
    }
    return g_binkOpen(first, name, flags, fourth);
}

// Every 10 s in our worlds: the level heap's largest free block, and what paused the stream I/O meanwhile.
void StreamWatchUpdate(bool ours) {
    if (!rfa::DetailLog() || !ours || !g_pauseStreamIo) return;  // a report for the log (Log=1)
    const DWORD now = GetTickCount();
    if (now - g_streamWatchAt < 10000) return;
    g_streamWatchAt = now;
    const DWORD largest = LargestFree(kLevelHeap, nullptr);
    const DWORD pausedNow = g_streamPausedAt ? now - g_streamPausedAt : 0;
    rfa::LogDetail("Streaming (10 s): the level heap's largest free block %.1f MB; the stream I/O paused %ld times for %.1f s in all (the longest "
             "%.1f s, by %p)%s; %ld videos opened",
             largest / 1048576.0, g_streamPauses, g_streamPausedMs / 1000.0, g_streamPausedLongest / 1000.0, g_streamPausedLongestBy,
             pausedNow ? " and paused now" : "", g_videosOpened);
    if (pausedNow) rfa::LogDetail("Streaming: the stream I/O has been paused %.1f s, by the code at %p", pausedNow / 1000.0, g_streamPausedBy);
    g_streamPauses = g_videosOpened = 0;
    g_streamPausedMs = g_streamPausedLongest = 0;
    g_streamPausedLongestBy = nullptr;
}

// The stream update, wrapped: in our worlds centred on a held position that follows the camera only
// once it has gone StreamSteady= metres from it (see kStreamUpdate).
void __fastcall StreamUpdateSteady(void* manager, void* unused, const float* position, int flag) {
    if (!position || g_streamSteady <= 0 || !InOurWorld()) {
        g_streamHeldSet = false;
        g_streamUpdate(manager, unused, position, flag);
        return;
    }
    const float dx = position[0] - g_streamHeld[0], dz = position[2] - g_streamHeld[2];
    if (!g_streamHeldSet || dx * dx + dz * dz > g_streamSteady * g_streamSteady) {
        if (!g_streamHeldSet)
            RFA_LOG_FIRST(4, "Streaming: what is loaded follows the camera once it has gone %.0f m in world \"%s\" (StreamSteady=)",
                          g_streamSteady, WorldName());
        g_streamHeld[0] = position[0];
        g_streamHeld[2] = position[2];
        g_streamHeldSet = true;
    }
    g_streamHeld[1] = position[1];
    g_streamUpdate(manager, unused, g_streamHeld, flag);
}

// ---- Children of destroyed parents, when a zone is made again ----------------------------------
// A zone made again from a save, or after it streamed out, keeps a list of the objects that went
// out untouched. The world's serialize_zone (0x6D5F90) makes those objects again from their zone
// file stubs, and makes the others from their saved state. Its sibling 0x6CFDA0 makes the listed
// ones only. 0x6CFBD0 fills a set with the listed handles, and 0x6DA720 asks whether a handle is
// in it. A stub carries the object's original parent (stub +0x24, object +0x6C), and the add
// (0x6D4880) clears a parent it can't find with "Invalidating parent handle ... because the parent
// does not exist". A parent destroyed before the save is in neither the list nor the saved state.
// Its untouched child is made from its stub with that parent: a crate by the Dust safehouse after
// its neighbour was blown up. Guerrilla's create_object_from_stub (RM 0x93E410) clears the link the
// same way, so the child stands alone. Here such a child is made unattached and logged, when its
// parent is in its own zone (the handles' top byte) and in neither the list nor the world. Any
// other missing parent still reaches the game's check and its warning.
constexpr DWORD kSerializeZone = 0x6D5F90;     // thiscall: the zone's record, its file, a flag; ret 0Ch
constexpr DWORD kRemakeListed = 0x6CFDA0;      // thiscall: the zone's record, its file; ret 8
constexpr DWORD kFillListedSet = 0x6CFBD0;     // thiscall: the set, handles out, the zone, a flag; ret 10h
constexpr DWORD kAddZoneObject = 0x6D4880;     // thiscall: the object, a flag; ret 8
constexpr DWORD kZoneRecordListed = 0x68;      // the zone record's count of listed objects
constexpr DWORD kObjectHandle = 0x68;
constexpr DWORD kObjectParent = 0x6C;
using SerializeZoneFn = DWORD(__fastcall*)(void* world, void* unused, BYTE* record, void* file, int flag);
using RemakeListedFn = DWORD(__fastcall*)(void* world, void* unused, BYTE* record, void* file);
using FillListedSetFn = DWORD(__fastcall*)(void* world, void* unused, void* set, void* handles, int zone, int flag);
using AddZoneObjectFn = DWORD(__fastcall*)(void* world, void* unused, BYTE* object, int flag);
using InListedSetFn = bool(__fastcall*)(void* set, void* unused, DWORD handle);
auto const InListedSet = reinterpret_cast<InListedSetFn>(0x6DA720);
using ObjectInWorldFn = BYTE*(__fastcall*)(void* world, void* unused, DWORD handle);
auto const ObjectInWorld = reinterpret_cast<ObjectInWorldFn>(0x6CB9E0);  // the add's own lookup
SerializeZoneFn g_serializeZone;
RemakeListedFn g_remakeListed;
FillListedSetFn g_fillListedSet;
AddZoneObjectFn g_addZoneObject;
thread_local bool t_remaking = false;      // in a zone being made again with a list
thread_local void* t_listedSet = nullptr;  // that list's set, once filled

DWORD __fastcall SerializeZoneRemaking(void* world, void* unused, BYTE* record, void* file, int flag) {
    const bool wasRemaking = t_remaking;
    void* const wasSet = t_listedSet;
    t_remaking = record && *reinterpret_cast<int*>(record + kZoneRecordListed) > 0;
    t_listedSet = nullptr;
    const DWORD made = g_serializeZone(world, unused, record, file, flag);
    t_remaking = wasRemaking;
    t_listedSet = wasSet;
    return made;
}

DWORD __fastcall RemakeListedRemaking(void* world, void* unused, BYTE* record, void* file) {
    const bool wasRemaking = t_remaking;
    void* const wasSet = t_listedSet;
    t_remaking = true;
    t_listedSet = nullptr;
    const DWORD made = g_remakeListed(world, unused, record, file);
    t_remaking = wasRemaking;
    t_listedSet = wasSet;
    return made;
}

DWORD __fastcall FillListedSetNoted(void* world, void* unused, void* set, void* handles, int zone, int flag) {
    const DWORD count = g_fillListedSet(world, unused, set, handles, zone, flag);
    if (t_remaking) t_listedSet = set;
    return count;
}

DWORD __fastcall AddZoneObjectChecked(void* world, void* unused, BYTE* object, int flag) {
    if (t_remaking && t_listedSet && object) {
        const DWORD handle = *reinterpret_cast<DWORD*>(object + kObjectHandle);
        const DWORD parent = *reinterpret_cast<DWORD*>(object + kObjectParent);
        if (parent != 0xFFFFFFFF && parent >> 24 == handle >> 24 && InOurWorld() &&
            !ObjectInWorld(world, nullptr, parent) && !InListedSet(t_listedSet, nullptr, parent)) {
            RFA_LOG_DETAIL_FIRST(200, "Zones made again: object %08X is made unattached; its parent %08X, in its zone, was in neither the zone's "
                               "list nor its saved state (destroyed before the save)", handle, parent);
            *reinterpret_cast<DWORD*>(object + kObjectParent) = 0xFFFFFFFF;
        }
    }
    return g_addZoneObject(world, unused, object, flag);
}

// Hooks the four, each only if its first bytes are the ones read here.
void InstallRemadeChildren() {
    const BYTE serializeOpening[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xB8, 0x01, 0x00, 0x00};
    const BYTE remakeOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x38};
    const BYTE fillOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x24};
    const BYTE addOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14};
    if (memcmp(reinterpret_cast<const void*>(kSerializeZone), serializeOpening, sizeof(serializeOpening)) != 0 ||
        memcmp(reinterpret_cast<const void*>(kRemakeListed), remakeOpening, sizeof(remakeOpening)) != 0 ||
        memcmp(reinterpret_cast<const void*>(kFillListedSet), fillOpening, sizeof(fillOpening)) != 0 ||
        memcmp(reinterpret_cast<const void*>(kAddZoneObject), addOpening, sizeof(addOpening)) != 0) {
        rfa::Log("Zones made again: the game's zone loading isn't the code expected; children of destroyed parents keep the game's warning");
        return;
    }
    g_addZoneObject = reinterpret_cast<AddZoneObjectFn>(rfa::HookFunction(kAddZoneObject, sizeof(addOpening), reinterpret_cast<void*>(&AddZoneObjectChecked)));
    g_fillListedSet = reinterpret_cast<FillListedSetFn>(rfa::HookFunction(kFillListedSet, sizeof(fillOpening), reinterpret_cast<void*>(&FillListedSetNoted)));
    g_remakeListed = reinterpret_cast<RemakeListedFn>(rfa::HookFunction(kRemakeListed, sizeof(remakeOpening), reinterpret_cast<void*>(&RemakeListedRemaking)));
    g_serializeZone = reinterpret_cast<SerializeZoneFn>(rfa::HookFunction(kSerializeZone, sizeof(serializeOpening), reinterpret_cast<void*>(&SerializeZoneRemaking)));
    if (!g_addZoneObject || !g_fillListedSet || !g_remakeListed || !g_serializeZone)
        rfa::Log("Zones made again: could not hook all of the game's zone loading; children of destroyed parents may keep the game's warning");
}

// Each frame: in our worlds the sub-zones' layer keeps the game's radius but no more than it can
// track, and the zones' layer streams at StreamRadius (set again if the game put its own back); in
// any other world the zones' radius it had, if its layers are still the ones we changed.
void RunStreaming(bool ours) {
    void* manager = g_streamManager;
    if (!manager || !*static_cast<void**>(manager)) return;  // no manager, or its layers not made yet
    void* layers = *static_cast<void**>(manager);
    if (ours && StreamRadiusOf(manager, nullptr, kSubzoneLayer) > MostRadius(kSubzoneLayer)) {
        SetStreamRadius(manager, nullptr, kSubzoneLayer, MostRadius(kSubzoneLayer));
        rfa::Log("Streaming: sub-zones of world \"%s\" kept loaded %.0f m around the player (layer %d)", WorldName(),
                 StreamRadiusOf(manager, nullptr, kSubzoneLayer), kSubzoneLayer);
    }
    const float now = StreamRadiusOf(manager, nullptr, kZoneLayer);
    const float wanted = g_streamRadius < MostRadius(kZoneLayer) ? g_streamRadius : MostRadius(kZoneLayer);
    if (ours && g_streamRadius > 0) {
        if (now == wanted) return;
        if (!g_streamOurs || layers != g_streamLayers) g_streamRadiusBefore = now;
        g_streamOurs = true;
        g_streamLayers = layers;
        SetStreamRadius(manager, nullptr, kZoneLayer, wanted);
        rfa::Log("Streaming: zones of world \"%s\" (%.0f m) kept loaded %.0f m around the player (layer %d; the game's radius %.0f m)",
                 WorldName(), OurZoneWidth(), wanted, kZoneLayer, g_streamRadiusBefore);
    } else if (g_streamOurs) {
        g_streamOurs = false;
        if (layers == g_streamLayers) {
            SetStreamRadius(manager, nullptr, kZoneLayer, g_streamRadiusBefore);
            rfa::Log("Streaming: back to the game's radius %.0f m in world \"%s\"", g_streamRadiusBefore, WorldName());
        }
    }
}

// The ground's detail distances, the console floats Rl_terrain_fade_start and _end: Armageddon draws
// a sub-zone's ground with all its layers to 125 m, fades it to the colour map by 150 m and draws none
// past that (0xF039A0: its far ground, 4.3 m cells laid up to 18 m under the real ground, shows from
// there), so buildings and rocks 150 m off stood over ground that sank away. Guerrilla draws every
// loaded sub-zone's ground, its layers to 250 m and the colour map beyond, and its far ground only past
// 320 m (rl_terrain_renderer::prerender; the camera's high-detail far clip). Armageddon's fade end is
// also its cut-off and its far ground's start, so in our worlds (GroundDetail=1) the two get 250 and
// 320, and the game's own come back elsewhere (unless changed in the console meanwhile).
float& g_terrainFadeStart = *reinterpret_cast<float*>(0x1130BD0);
float& g_terrainFadeEnd = *reinterpret_cast<float*>(0x1130BD4);
constexpr float kGuerrillaFadeStart = 250.0f, kGuerrillaFadeEnd = 320.0f;
bool g_groundDetailOurs = false;
float g_fadeStartBefore = 125.0f, g_fadeEndBefore = 150.0f;

// Each frame: Guerrilla's distances on entering one of our worlds, the game's back on leaving it.
void RunGroundDetail(bool ours) {
    if (ours && g_groundDetail && !g_groundDetailOurs) {
        g_fadeStartBefore = g_terrainFadeStart;
        g_fadeEndBefore = g_terrainFadeEnd;
        g_terrainFadeStart = kGuerrillaFadeStart;
        g_terrainFadeEnd = kGuerrillaFadeEnd;
        g_groundDetailOurs = true;
        rfa::Log("Ground: detailed to %.0f m, far ground from %.0f m, as Guerrilla's (the game's %.0f and %.0f m; GroundDetail=)",
                 kGuerrillaFadeStart, kGuerrillaFadeEnd, g_fadeStartBefore, g_fadeEndBefore);
    } else if ((!ours || !g_groundDetail) && g_groundDetailOurs) {
        g_groundDetailOurs = false;
        if (g_terrainFadeStart == kGuerrillaFadeStart && g_terrainFadeEnd == kGuerrillaFadeEnd) {
            g_terrainFadeStart = g_fadeStartBefore;
            g_terrainFadeEnd = g_fadeEndBefore;
            rfa::Log("Ground: back to the game's detail distances, %.0f and %.0f m", g_fadeStartBefore, g_fadeEndBefore);
        }
    }
}

// Whether a world's or zone's name is one of ours: the world's name, or it, "_" and more.
bool NamedOurs(const char* name) {
    for (const std::string& world : g_worlds) {
        const size_t n = world.size();
        if (_strnicmp(name, world.c_str(), n) == 0 && (name[n] == 0 || name[n] == '_')) return true;
    }
    return false;
}

// Overwrites one of the game's constants (its read-only data).
bool SetGameConstant(DWORD address, float value) {
    auto constant = reinterpret_cast<float*>(address);
    DWORD old;
    if (!VirtualProtect(constant, sizeof(float), PAGE_READWRITE, &old)) return false;
    *constant = value;
    VirtualProtect(constant, sizeof(float), old, &old);
    return true;
}

// The physics world widened for our zones (see kWorldReach), if they need more than the game's.
void WidenWorld() {
    const float reach = kWorldReachZones * g_zoneWidth;
    if (reach <= kGameWorldReach) return;
    const float& now = *reinterpret_cast<const float*>(kWorldReach);
    const float& nowBack = *reinterpret_cast<const float*>(kWorldReachBack);
    if (now != kGameWorldReach || nowBack != -kGameWorldReach) {
        rfa::Log("The physics world's size isn't the expected %.0f m (%.0f, %.0f); left alone, so zones beyond it won't work",
                 kGameWorldReach, now, nowBack);
        return;
    }
    if (SetGameConstant(kWorldReach, reach) && SetGameConstant(kWorldReachBack, -reach))
        rfa::Log("The physics world reaches %.0f m from the middle (the game's %.0f m; nine %.0f m zones)", reach, kGameWorldReach, g_zoneWidth);
    else
        rfa::Log("Could not widen the physics world; zones beyond %.0f m from the middle won't work", kGameWorldReach);
}

// The zone size, wrapped: the game's, then ZoneSize= for our worlds.
void __cdecl ZoneSize(const char* name, float* size) {
    g_zoneSize(name, size);
    if (g_zoneWidth > 0 && name && size && NamedOurs(name) && *size != g_zoneWidth) {
        RFA_LOG_FIRST(1, "Zones: \"%s\" gets %.0f m zones (the game's %.0f m; ZoneSize=)", name, g_zoneWidth, *size);
        *size = g_zoneWidth;
    }
}

std::wstring DataFolder() { return rfa::RootDir() + L"\\mods\\guerrilla_world\\"; }

// Whether a zone's name (<world>_XX_YY) is one of our worlds'.
bool OurZone(const char* zone) {
    for (const std::string& name : g_worlds)
        if (_strnicmp(zone, name.c_str(), name.size()) == 0 && zone[name.size()] == '_') return true;
    return false;
}

// Replaces the game's error reports: all go through, but the invalid mission hash of our zones.
int g_missionHashReports = 0;
int g_reportsLogged = 0;  // the game's other reports in our worlds, also written to the log (the first 200)

void __cdecl ReportError(int level, int flag, const char* format, va_list arguments) {
    if (reinterpret_cast<DWORD>(format) == kMissionHashFormat) {
        va_list peek;
        va_copy(peek, arguments);
        const char* zone = va_arg(peek, const char*);
        va_end(peek);
        if (zone && OurZone(zone)) {
            if (g_missionHashReports++ == 0)
                rfa::Log("Leaving out the game's \"invalid mission info hash\" reports for our worlds' zones (first: %s)", zone);
            return;
        }
    }
    // What the game reports in our worlds (a file it couldn't load, a pool that ran out, ...) only
    // flashes on the screen; the log keeps it.
    if (format && g_reportsLogged < 200 && InOurWorld()) {
        char text[512];
        va_list copy;
        va_copy(copy, arguments);
        vsnprintf(text, sizeof(text), format, copy);
        va_end(copy);
        size_t length = strlen(text);
        while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) text[--length] = '\0';
        if (++g_reportsLogged == 200) rfa::Log("Game report (level %d): %s (the last one logged)", level, text);
        else rfa::Log("Game report (level %d): %s", level, text);
    }
    g_reportError(level, flag, format, arguments);
}

// The renderer's warnings: when one of its pools runs out it sets a bit in a word (the renderer,
// [0x11BA7F0], +0x60, then +4) and draws a "RENDER_LIB WARNINGS" box with a line a bit, a message for
// each but two (0x40 and 0x400 show as an empty line). The box only shows on the screen; each bit is
// named in the log the first time it is seen in our worlds. 0x400 has its name here from the one place
// that sets it: the batched pass's list of materials (0x1000 of them, the check at 0xD72108) is full.
constexpr DWORD kRenderer = 0x11BA7F0;
constexpr size_t kRendererMetrics = 0x60, kMetricsWarnings = 0x04;
struct RenderWarning { DWORD bit; const char* text; };
constexpr RenderWarning kRenderWarnings[] = {
    {0x1, "out of dynamic indices"}, {0x2, "out of instance buffer space"}, {0x4, "out of instance blocks"},
    {0x8, "out of dynamic render groups"}, {0x10, "out of decals"}, {0x20, "out of decal blocks"},
    {0x80, "out of buffer space on the hardware"}, {0x100, "out of morph weight memory"},
    {0x200, "out of unbatched renderable trackers"}, {0x400, "out of batched materials"}, {0x800, "out of render calls"},
    {0x1000, "out of visibility list entries"},
    {0x2000, "out of GPU visibility queue entries"}, {0x4000, "out of GPU occlusion queries"}, {0x8000, "out of Edge geometry jobs"},
    {0x10000, "out of light volume buffer space"}, {0x20000, "out of batched visibility entries"}, {0x40000, "dynamic vertices stall"},
    {0x80000, "dynamic clone vertices stall"}, {0x100000, "dynamic indices stall"}, {0x200000, "dynamic instance vertices stall"},
};
DWORD g_renderWarningsLogged = 0;

// The list behind the warning 0x200, what the batched pass draws one by one (see "Room for what the game
// draws one by one"): how many the last frame asked for (0x316B0B4, kept as the pass is finished, before
// its count is cut to the list's size, so it goes on past it) and the list's size, the value in the game's
// check as it adds one (0xD7205E: 384, or what the mod made it). The count is one frame's, so the most
// seen in our worlds is kept, and logged from 256 up each time it has grown by a quarter.
constexpr DWORD kUnbatchedWarning = 0x200;
constexpr DWORD kUnbatchedAsked = 0x316B0B4;
constexpr DWORD kUnbatchedAddValue = 0xD7205E;  // the add's cmp [ebp-4],180h (81 7D FC, then it)
DWORD g_unbatchedMost = 0, g_unbatchedMostLogged = 0;

void NoteRenderWarnings() {
    if (!InOurWorld()) return;
    const DWORD asked = *reinterpret_cast<const DWORD*>(kUnbatchedAsked);
    if (asked > g_unbatchedMost) g_unbatchedMost = asked;
    if (g_unbatchedMost >= 256 && g_unbatchedMost >= g_unbatchedMostLogged + g_unbatchedMostLogged / 4) {
        g_unbatchedMostLogged = g_unbatchedMost;
        rfa::Log("Unbatched renderables: %lu asked for in one frame, the most so far (the list holds %lu)", g_unbatchedMost,
                 rfa::CodeValue(kUnbatchedAddValue));
    }
    const BYTE* renderer = *reinterpret_cast<BYTE* const*>(kRenderer);
    const BYTE* metrics = renderer ? *reinterpret_cast<const BYTE* const*>(renderer + kRendererMetrics) : nullptr;
    const DWORD warnings = metrics ? *reinterpret_cast<const DWORD*>(metrics + kMetricsWarnings) : 0;
    const DWORD fresh = warnings & ~g_renderWarningsLogged;
    if (!fresh) return;
    g_renderWarningsLogged |= fresh;
    for (DWORD bit = 1; bit; bit <<= 1) {
        if (!(fresh & bit)) continue;
        const char* text = nullptr;
        for (const RenderWarning& warning : kRenderWarnings)
            if (warning.bit == bit) text = warning.text;
        if (bit == kUnbatchedWarning && text)
            rfa::Log("Render warning (0x%X): %s (the most asked for in one frame so far %lu, the list holds %lu)", bit, text, g_unbatchedMost,
                     rfa::CodeValue(kUnbatchedAddValue));
        else if (text) rfa::Log("Render warning (0x%X): %s", bit, text);
        else rfa::Log("Render warning (0x%X): one the game shows with no message", bit);
    }
}

// ---- Transition navpoints

// A building's navpoints come with its destructible object, and the game makes them as it streams the
// object in (navpoint's stream, 0x7F08A0, thiscall, one bool, ret 4). A transition navpoint (type 11, the
// byte at +0xF4; one joining the building's points to the ground's navigation) then looks for the nav
// cell under its position (+0x04) with the cell lookup (0x5B9AA0, cdecl, a query whose +0 points at the
// position; it returns the cell, or none), skipping the cells switched off (flags 0x08 and 0x10).
// Guerrilla switches off the ground's cells round many buildings until a mission switches them on, so
// in our worlds some of these points stand on no cell the lookup takes, and the game reports
// "Invalid transition navpoint ... can not find nav cell". Guerrilla's own navpoint stream does the same
// lookup and then, finding none, makes a navpoint at the nearest point of an open cell and links the
// two. Here the transition point is given that nearest open cell itself (the lookup asked again round
// it, ring by ring), which joins it to the ground's navigation in the same place; the game then keeps
// it in that cell as it does any other.
constexpr DWORD kNavpointStream = 0x7F08A0;
constexpr size_t kNavpointStreamPrologue = 9;  // push ebp / mov ebp,esp / sub esp,0A8h
using NavpointStreamFn = bool(__fastcall*)(BYTE* navpoint, void* unused, bool flag);
NavpointStreamFn g_navpointStream;
constexpr DWORD kFindNavCell = 0x5B9AA0;
constexpr size_t kFindNavCellPrologue = 9;  // push ebp / mov ebp,esp / sub esp,474h
using FindNavCellFn = void*(__cdecl*)(BYTE* query);
FindNavCellFn g_findNavCell;
constexpr size_t kNavpointPosition = 0x04, kNavpointHandle = 0x68, kNavpointType = 0xF4;
constexpr char kTransitionNavpoint = 11;
constexpr size_t kNavQuerySize = 0x30;   // the caller's query on its stack, with what the lookup writes back
constexpr float kNavRingStep = 0.5f;     // the rings round the point, every half metre ...
constexpr int kNavRings = 20;            // ... out to 10 m
constexpr int kNavRingDirections = 16;
thread_local BYTE* g_streamingNavpoint = nullptr;  // the navpoint this thread is streaming in, while its stream runs
int g_navpointsJoined = 0;

bool __fastcall NavpointStream(BYTE* navpoint, void* unused, bool flag) {
    BYTE* outer = g_streamingNavpoint;
    g_streamingNavpoint = navpoint;
    const bool result = g_navpointStream(navpoint, unused, flag);
    g_streamingNavpoint = outer;
    return result;
}

void* __cdecl FindNavCell(BYTE* query) {
    void* cell = g_findNavCell(query);
    BYTE* navpoint = g_streamingNavpoint;
    const float* position = navpoint ? reinterpret_cast<const float*>(navpoint + kNavpointPosition) : nullptr;
    if (cell || !navpoint || *reinterpret_cast<const char*>(navpoint + kNavpointType) != kTransitionNavpoint ||
        *reinterpret_cast<const float* const*>(query) != position || !InOurWorld())
        return cell;
    BYTE ask[kNavQuerySize];
    memcpy(ask, query, sizeof(ask));
    float around[3] = {position[0], position[1], position[2]};
    *reinterpret_cast<float**>(ask) = around;
    const DWORD handle = *reinterpret_cast<const DWORD*>(navpoint + kNavpointHandle);
    for (int ring = 1; ring <= kNavRings; ++ring) {
        const float distance = ring * kNavRingStep;
        for (int direction = 0; direction < kNavRingDirections; ++direction) {
            const float angle = direction * 6.2831853f / kNavRingDirections;
            around[0] = position[0] + distance * cosf(angle);
            around[2] = position[2] + distance * sinf(angle);
            cell = g_findNavCell(ask);
            if (!cell) continue;
            if (g_navpointsJoined++ < 50)
                rfa::Log("Pathfinding: transition navpoint %08X at (%.1f, %.1f, %.1f) stands on no open nav cell; joined to the nearest "
                         "open one, %.1f m away, as Guerrilla links such a point to the nearest open place",
                         handle, position[0], position[1], position[2], distance);
            return cell;
        }
    }
    rfa::Log("Pathfinding: transition navpoint %08X at (%.1f, %.1f, %.1f) has no open nav cell within %.0f m",
             handle, position[0], position[1], position[2], kNavRings * kNavRingStep);
    return nullptr;
}

int g_debugPrintsLogged = 0;

void __cdecl DebugPrint(const char* format, ...) {
    char text[1024];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format ? format : "", arguments);
    va_end(arguments);
    if (rfa::DetailLog() && g_debugPrintsLogged < 500 && InOurWorld()) {
        char shown[1024];
        strcpy_s(shown, text);
        size_t length = strlen(shown);
        while (length > 0 && (shown[length - 1] == '\n' || shown[length - 1] == '\r')) shown[--length] = '\0';
        if (++g_debugPrintsLogged == 500) rfa::LogDetail("Game debug print: %s (the last one logged)", shown);
        else rfa::LogDetail("Game debug print: %s", shown);
    }
    g_debugPrint("%s", text);
}

// Replaces the area lookup: in our worlds the area is the world itself, so its own precache is mounted
// (if the world has one: a world built before the precache keeps the game's choice, as before).
const char* __cdecl PrecacheArea(int column, int row, int gridSize) {
    const char* area = g_precacheArea(column, row, gridSize);
    if (!InOurWorld()) return area;
    wchar_t precache[MAX_PATH];
    swprintf_s(precache, L"%s\\overrides\\build\\pc\\cache\\%hs_precache.vpp_pc", rfa::RootDir().c_str(), WorldName());
    if (GetFileAttributesW(precache) == INVALID_FILE_ATTRIBUTES) {
        rfa::Log("Precache: world \"%s\" has no %s_precache.vpp_pc (built before it had one); the game's \"%s\" is used", WorldName(), WorldName(), area);
        return area;
    }
    rfa::Log("Precache: world \"%s\" mounts %s_precache.vpp_pc (the game's area there is \"%s\")", WorldName(), WorldName(), area);
    return WorldName();
}

// The game holds 140 animation sets and fills 125 itself; guerrilla_vehicles makes room for more (its
// own table of them, the size in the loaders' checks changed). The check's size, in the first loader
// (cmp dword ptr [2632170h],8Ch at 0x97C1CC): still 140 means there's no room, and Guerrilla's
// vehicles' sets, more than the 15 left, would stop the game at start-up, so their list isn't handed
// over then.
constexpr DWORD kAnimSetLimit = 0x97C1D2;
constexpr DWORD kGameMaxAnimSets = 140;

// Answers the start-up table copy's lookup of a table the world builder wrote (by its file name,
// whatever folder it is asked with) with the builder's; every other table is the game's.
bool __fastcall TableCacheFind(void* cache, void* unused, const char* name, CachedTable* out) {
    if (name && out) {
        const char* file = name;
        for (const char* c = name; *c; ++c)
            if (*c == '\\' || *c == '/') file = c + 1;
        for (const BuiltTable& table : g_builtTables) {
            if (table.bytes.empty() || _stricmp(file, table.name) != 0) continue;
            if (_stricmp(table.name, "anim_set_filenames.xtbl") == 0 && *reinterpret_cast<const DWORD*>(kAnimSetLimit) == kGameMaxAnimSets) {
                RFA_LOG_FIRST(1, "%s: the game has no room for Guerrilla's vehicles' sets (the guerrilla_vehicles mod makes it); it keeps its own", table.what);
                break;
            }
            out->data = table.bytes.data();
            out->size = static_cast<DWORD>(table.bytes.size() - 1);
            rfa::Log("%s: the game reads mods\\guerrilla_world\\%s (%lu bytes)", table.what, table.name, out->size);
            return true;
        }
    }
    return g_tableCacheFind(cache, unused, name, out);
}

// The effect files, as the game streams them in: the streaming system's load handler for its VFX
// files (.cefct_pc with .gefct_pc; 0x4EAD30, registered at 0x5295D8), cdecl (the file's name, -, its
// data, -, its data file), true once the effect is in the game's map of loaded effects (0x11E9EF8; its
// lookup 0x4EB430 by the file's name), where playing an effect looks for its visual's file (the
// table's Filename, with .cefct_pc for its extension). Wrapped to write to the log, for the effects
// the world builder adds (rfg_), whether each loaded, and how many the game loads in all.
constexpr DWORD kEffectFileLoad = 0x4EAD30;
constexpr size_t kEffectFileLoadPrologue = 6;  // push ebp / mov ebp,esp / sub esp,8
using EffectFileLoadFn = bool(__cdecl*)(const char* name, void* unused1, void* data, void* unused2, void* dataFile);
EffectFileLoadFn g_effectFileLoad;
using LoadedEffectFn = void*(__cdecl*)(const char* name);
auto const LoadedEffect = reinterpret_cast<LoadedEffectFn>(0x4EB430);
int g_effectFilesLoaded = 0, g_ourEffectFilesLogged = 0;
std::vector<std::string> g_ourEffectFiles;  // the rfg_ effect files the game has loaded (see GuerrillaParticleLight)

// The effects' emitters' pool (0x26BF650: each emitter of each loaded effect takes a place, see "Room for the
// effects' emitters"): how many places it holds (a word at +0xA) and how many are taken (a byte each at +0x18,
// 0xFF while free).
constexpr DWORD kEmitterPool = 0x26BF650;
int EmitterPoolUse(int& holds) {
    holds = *reinterpret_cast<const WORD*>(kEmitterPool + 0xA);
    const BYTE* flags = *reinterpret_cast<BYTE* const*>(kEmitterPool + 0x18);
    int used = 0;
    for (int i = 0; flags && i < holds; ++i)
        if (flags[i] != 0xFF) ++used;
    return used;
}

bool __cdecl EffectFileLoad(const char* name, void* unused1, void* data, void* unused2, void* dataFile) {
    // Each file named before it loads, with the emitters' pool's use, so a load that never returns is named.
    int holds = 0;
    const int used = EmitterPoolUse(holds);
    ++g_effectFilesLoaded;
    rfa::LogDetail("Effects: loading %d: %s (emitters %d of %d)", g_effectFilesLoaded, name ? name : "?", used, holds);
    const bool loaded = g_effectFileLoad(name, unused1, data, unused2, dataFile);
    if (loaded && name && _strnicmp(name, "rfg_", 4) == 0 && strstr(name, ".cefct_pc") &&
        std::find(g_ourEffectFiles.begin(), g_ourEffectFiles.end(), name) == g_ourEffectFiles.end())
        g_ourEffectFiles.push_back(name);
    if (name && (!loaded || (_strnicmp(name, "rfg_", 4) == 0 && strstr(name, ".cefct_pc") && !LoadedEffect(name)))) {
        ++g_ourEffectFilesLogged;
        rfa::Log("Effects: %s: %s, %s in its map of loaded effects", name, loaded ? "loaded" : "NOT LOADED", LoadedEffect(name) ? "now" : "NOT");
    }
    return loaded;
}

// Playing an effect: the worker (0x4EB4B0; cdecl, ten arguments: the effect's entry in effects.xtbl,
// where, and eight more), which returns the effect object's handle or -1. Before making the object it
// may leave the visual out: with <LOD> (+0x9C) it drops it past the spawning distance (+0xA4) or, with
// View (+0x9D), out of the camera's view (0x768400(where, radius +0x84)); without a visual or a sound
// (the effect's +0x48) it gives up; and it gives up while a count it reads (0xD15220) is under 20.
// Wrapped to write to the log, for visuals the world builder adds (rfg_), each of those and the result
// (each visual's first try, and its first success after failing; up to 60 lines).
constexpr DWORD kPlayEffect = 0x4EB4B0;
constexpr size_t kPlayEffectPrologue = 6;  // push ebp / mov ebp,esp / sub esp,3Ch
using PlayEffectFn = int(__cdecl*)(BYTE* effect, const float* where, DWORD a3, DWORD a4, DWORD a5, DWORD a6, DWORD a7, DWORD a8,
                                   DWORD a9, DWORD a10);
PlayEffectFn g_playEffect;
using VisualFn = BYTE*(__cdecl*)(int index);
auto const Visual = reinterpret_cast<VisualFn>(0x4EF3F0);
using InViewFn = bool(__cdecl*)(const float* where, float radius);
auto const InView = reinterpret_cast<InViewFn>(0x768400);
using PlayCountFn = int(__cdecl*)();
auto const PlayCount = reinterpret_cast<PlayCountFn>(0xD15220);
int g_ourPlaysLogged = 0;
std::vector<std::string> g_visualsFailing, g_visualsPlayed;

// The materials' effects' memory pool (see "Room for the materials' effects"): its size and the bytes taken.
constexpr DWORD kMaterialEffectPool = 0x155E148;
constexpr DWORD kMemoryPoolSize = 0x44, kMemoryPoolTaken = 0x4C;

// Whether the Guerrilla world's pieces play Guerrilla's own event effects (see "Guerrilla's material events").
bool g_materialEventsOn = false;

void MusicTestFrame();  // (see "The music's format test")

int __cdecl PlayEffect(BYTE* effect, const float* where, DWORD a3, DWORD a4, DWORD a5, DWORD a6, DWORD a7, DWORD a8, DWORD a9, DWORD a10) {
    // The tenth argument is Armageddon's destruction sound for the event (its own event, its material switches,
    // the event's size), which the effect object plays beside the effect. In the Guerrilla world the effect is
    // Guerrilla's and has Guerrilla's sound in it, so Armageddon's is left out there.
    if (g_materialEventsOn && a10 && InOurWorld()) {
        RFA_LOG_FIRST(1, "Material events: Armageddon's destruction sound is left out in the Guerrilla world (Guerrilla's effects have their own)");
        a10 = 0;
    }
    // That pool is filled while the game starts; by the first effect played its use is final.
    RFA_LOG_FIRST(1, "Material effects: %lu of %lu bytes in use", *reinterpret_cast<const DWORD*>(kMaterialEffectPool + kMemoryPoolTaken),
                  *reinterpret_cast<const DWORD*>(kMaterialEffectPool + kMemoryPoolSize));
    MusicTestFrame();
    const int handle = g_playEffect(effect, where, a3, a4, a5, a6, a7, a8, a9, a10);
    BYTE* visual = effect ? Visual(*reinterpret_cast<int*>(effect + 0x44)) : nullptr;
    const char* name = visual ? reinterpret_cast<const char*>(visual) : "";
    if (_strnicmp(name, "rfg_", 4) != 0 || g_ourPlaysLogged >= 60 || !where) return handle;
    auto seen = [name](const std::vector<std::string>& names) { return std::find(names.begin(), names.end(), name) != names.end(); };
    if (seen(g_visualsPlayed) || (handle == -1 && seen(g_visualsFailing))) return handle;
    (handle == -1 ? g_visualsFailing : g_visualsPlayed).push_back(name);
    {
        ++g_ourPlaysLogged;
        const float radius = *reinterpret_cast<float*>(visual + 0x84), spawning = *reinterpret_cast<float*>(visual + 0xA4);
        float away = 0;
        for (int i = 0; i < 3; ++i) away += (where[i] - g_cameraPosition[i]) * (where[i] - g_cameraPosition[i]);
        rfa::LogDetail("Effects: play %s at %.1f %.1f %.1f (%.1f m from the camera): handle %d; LOD %d, spawning %.0f m, view %d (in view: %d), "
                 "radius %.1f, flags 0x%X, category %d, file %s (%s), sound %d, count %d",
                 name, where[0], where[1], where[2], sqrtf(away), handle, visual[0x9C], spawning, visual[0x9D], InView(where, radius) ? 1 : 0,
                 radius, *reinterpret_cast<DWORD*>(visual + 0x88), *reinterpret_cast<int*>(visual + 0x94),
                 reinterpret_cast<const char*>(visual + 0x41), LoadedEffect(reinterpret_cast<const char*>(visual + 0x41)) ? "loaded" : "NOT loaded",
                 *reinterpret_cast<int*>(effect + 0x48), PlayCount());
    }
    return handle;
}

// ---- What becomes of the effects' sounds (a look, not a change)
//
// The user, 2026-11-10: a hammer hit's sound is "a small snippet or sometimes the full", and sounds in general
// "still need work"; other vehicles' engines and NPCs' rifle loops end by themselves in a fight. The game doesn't
// stop a sound that plays once when its effect ends (0x4F5B10: only a loop's, or with kill_sound_when_done), and
// the hits' cues have no limit of their own, so what ends them isn't known. Two things are written to the log:
//
// Wwise's own messages. The game hands Wwise a function for its errors (AK::Monitor::SetLocalOutput at 0xB97D62,
// level 3, the function 0xB97750: the error's number, its text in 16-bit letters, its level), which copies the
// text and drops it. Wrapped, they go to the log ("Wwise: ...": the first few of each text, then its count): a
// stream that can't start for want of memory, a source or a voice starving, a file not found. (The game gives all its streams 2.25 MB in
// pieces of 26 KB, 0xB9783F / 0xB97848: 88 pieces. A piece is two seconds of Armageddon's Vorbis and 0.15-0.4 s
// of Guerrilla's sound as we keep it, uncompressed.)
//
// How long an effect's sound played. The effect object's sound (0x4F8430, thiscall, 1 = the effect starts) posts
// the event (+0x1A4) and keeps the sound's handle (+0x1A8); 0xB84D20 gives a handle's game object, 0 once the
// game knows the sound ended (Wwise's end-of-event). For the sounds of the visuals the world builder added that
// play once: "Effect sounds: <visual>'s sound (event N) ended M ms after it started; ..." (the first 200), with
// how far from the camera it started, how far its effect moved meanwhile (the game moves the sound with it) and
// what Wwise said of that sound's game object. A hammer hit's waves are 0.6-1.3 s long. (The first run, 2026-11-11
// 00:16: four hits in five lasted their wave's length; one in five ended after 60-280 ms, with nothing else
// starting then.) The ends are looked for once a frame, from the effects' frame update: 0xB84D20 finds a
// handle's game object by walking every sound object in use and its events (0xB814B0, some 4 µs a time), and
// asked on every effect's sound update (some 200 a frame) for every watched sound it took 15 ms of a frame
// (2026-11-12).
constexpr DWORD kWwiseOutput = 0xB97750;
constexpr size_t kWwiseOutputPrologue = 9;  // push ebp / mov ebp,esp / sub esp,80Ch
// (Wwise's own declaration, from the exe's export of SetLocalOutput: the error, its text, its level, the playing
// sound's number and its game object.)
using WwiseOutputFn = void(__cdecl*)(int code, const wchar_t* message, int level, DWORD playing, DWORD gameObject);
WwiseOutputFn g_wwiseOutput;

constexpr DWORD kEffectSound = 0x4F8430;
constexpr size_t kEffectSoundPrologue = 6;  // push ebp / mov ebp,esp / sub esp,40h
constexpr DWORD kEffectPosition = 0x4, kEffectSoundEvent = 0x1A4, kEffectSoundHandle = 0x1A8, kEffectObjectVisual = 0x134;
using EffectSoundFn = void(__fastcall*)(BYTE* effect, void* unused, DWORD start);
EffectSoundFn g_effectSound;
auto const SoundGameObject = reinterpret_cast<DWORD(__cdecl*)(DWORD handle)>(0xB84D20);  // 0 once the sound has ended
auto const EventIsLoop = reinterpret_cast<bool(__cdecl*)(DWORD event)>(0xB82FD0);       // whether it has a stop event

// A sound being watched: one an added effect plays once. Its handle, event and Wwise game object; when it
// started, how far from the camera, where its effect was then and how far the effect has moved since (the game
// moves the sound with its effect); and what Wwise said of it, if anything.
struct WatchedSound {
    DWORD handle, event, gameObject, started;
    BYTE* effect;
    float at[3], fromCamera, moved;
    char visual[48], wwise[96];
    DWORD wwiseAfter;
};
WatchedSound g_watchedSounds[32];
constexpr int kSoundEndsLogged = 200;
int g_soundEndsLogged = 0;

// Each of Wwise's messages by its text: how often it has come.
struct WwiseMessage {
    char text[96];
    int count;
};
WwiseMessage g_wwiseMessages[24];
constexpr int kWwiseEachLogged = 5;

// Wwise's message: for a watched sound, kept with it (its end's line says it); else the first few of each text,
// then its count at 100 and 1,000, so one that comes all the time (a far footstep's voice ended as too quiet to
// hear: Armageddon's own setting, 300 in 13 s of the first run) can't hide the others.
// Wwise's "Source starvation" (a playing stream had no data when the mixer asked for it).
constexpr int kSourceStarvation = 28;
void LogStreamMemoryAtStarvation();

void __cdecl WwiseOutput(int code, const wchar_t* message, int level, DWORD playing, DWORD gameObject) {
    if (code == kSourceStarvation) LogStreamMemoryAtStarvation();
    char text[96] = "";
    for (size_t i = 0; message && message[i] && i + 1 < sizeof(text); ++i) {
        text[i] = static_cast<char>(message[i] & 0xFF);
        text[i + 1] = 0;
    }
    bool watched = false;
    for (WatchedSound& sound : g_watchedSounds) {
        if (!sound.handle || !gameObject || sound.gameObject != gameObject) continue;
        strncpy_s(sound.wwise, text, _TRUNCATE);
        sound.wwiseAfter = GetTickCount() - sound.started;
        watched = true;
    }
    if (!watched)
        for (WwiseMessage& known : g_wwiseMessages) {
            if (known.count && strcmp(known.text, text) != 0) continue;
            if (!known.count) strncpy_s(known.text, text, _TRUNCATE);
            ++known.count;
            if (known.count <= kWwiseEachLogged)
                rfa::Log("Wwise: %s (code %d, level %d, playing %lu, game object %lu)", text, code, level, playing, gameObject);
            else if (known.count == 100 || known.count == 1000)
                rfa::Log("Wwise: %s: %d times so far", text, known.count);
            break;
        }
    g_wwiseOutput(code, message, level, playing, gameObject);
}

// Each watched sound that has ended since the last look: how long it played, and what is known of it.
void LogEndedSounds() {
    if (!g_effectSound) return;  // not watched (Log=0)
    const DWORD now = GetTickCount();
    for (WatchedSound& watched : g_watchedSounds) {
        if (!watched.handle || SoundGameObject(watched.handle) != 0) continue;
        // The first 200, and after them (300 more) only those that ended at once: what a sound engine that has
        // stopped playing would give every one of them.
        const bool atOnce = now - watched.started < 150;
        if (g_soundEndsLogged < kSoundEndsLogged || (atOnce && g_soundEndsLogged < kSoundEndsLogged + 300)) {
            ++g_soundEndsLogged;
            char wwise[160] = "Wwise said nothing of it";
            if (watched.wwise[0]) snprintf(wwise, sizeof(wwise), "Wwise after %lu ms: %s", watched.wwiseAfter, watched.wwise);
            // (The effect lets go of the handle when it ends itself; the effects are in a pool, so its place can be read.)
            const bool held = *reinterpret_cast<const DWORD*>(watched.effect + kEffectSoundHandle) == watched.handle;
            rfa::LogDetail("Effect sounds: %s's sound (event %lu) ended %lu ms after it started; %.1f m from the camera at its start, its effect moved %.1f m "
                     "and %s; %s",
                     watched.visual, watched.event, now - watched.started, watched.fromCamera, watched.moved,
                     held ? "still held the sound" : "had ended or let go of it", wwise);
        }
        watched.handle = 0;
    }
}

// Wwise's memory pools, every ten seconds (the first 90 times). It has no count of the sounds playing to read (its
// limits end sounds without a message, and the exe exports nothing for it); its pools' use is the only gauge the
// exe offers (AK::MemoryMgr::GetMaxPools, GetPoolName, GetPoolStats: bytes reserved, bytes used, ...): what every
// playing sound takes of the sound engine's pools, and whether any is near full.
auto const WwiseMaxPools = reinterpret_cast<long(__cdecl*)()>(0xE3CD40);
auto const WwisePoolName = reinterpret_cast<const wchar_t*(__cdecl*)(long pool)>(0xE3CCC0);
auto const WwisePoolStats = reinterpret_cast<int(__cdecl*)(long pool, DWORD* stats)>(0xE3D000);
// The audio thread's own time in ms (AK::Monitor::GetTimeStamp: a value its mixing loop writes, 0xE664C4, for
// every buffer it makes). It going on by ten seconds between two lines says the thread still runs; standing
// still, that it has stopped. (The run of 2026-11-11 01:20: all sound gone from 01:25:15 on, no voice or stream in
// any pool after, the game still posting and Wwise saying nothing: whether its thread ran wasn't known.)
auto const WwiseAudioTime = reinterpret_cast<long(__cdecl*)()>(0xE5AEA0);

void LogWwiseMemory() {
    static DWORD last = 0;
    static int logged = 0;
    const DWORD now = GetTickCount();
    if (logged >= 90 || now - last < 10000) return;
    last = now;
    ++logged;
    char line[1200] = "";
    size_t at = 0;
    for (long pool = 0, pools = WwiseMaxPools(); pool < pools && pool < 64 && at < sizeof(line) - 100; ++pool) {
        DWORD stats[16] = {};  // reserved, used, the largest free block, allocations, frees, the most used
        if (WwisePoolStats(pool, stats) != 1 || stats[0] == 0) continue;
        char name[40] = "";
        const wchar_t* wide = WwisePoolName(pool);
        for (size_t i = 0; wide && wide[i] && i + 1 < sizeof(name); ++i) {
            name[i] = static_cast<char>(wide[i] & 0xFF);
            name[i + 1] = 0;
        }
        at += snprintf(line + at, sizeof(line) - at, "%s%s %lu of %lu KB", at ? "; " : "", name[0] ? name : "a pool", stats[1] / 1024, stats[0] / 1024);
        // The pool everything the banks hold and every playing sound's own record come from: its largest free piece
        // too (a request larger than that fails though the pool isn't full), and how often it has been asked.
        if (strcmp(name, "Default") == 0)
            at += snprintf(line + at, sizeof(line) - at, " (the most ever used %lu KB, largest free piece %lu KB, %lu requests, %lu given back)",
                           stats[5] / 1024, stats[2] / 1024, stats[3], stats[4]);
        // The streams' two pools: the most each has held (the samples are ten seconds apart).
        else if (strncmp(name, "Stream", 6) == 0)
            at += snprintf(line + at, sizeof(line) - at, " (the most ever used %lu KB)", stats[5] / 1024);
    }
    // The game's own audio memory beside Wwise's: its pool "audio" (0x26AF858; cut up at start-up into the banks'
    // slots and, with all that is left, the cache of stream pieces), and that cache's counts (the object 0x2745E98:
    // how often a piece a stream asked for was in it, and how often it had to come from the disk).
    const BYTE* audioPool = reinterpret_cast<const BYTE*>(0x26AF858);
    const DWORD* audioCache = reinterpret_cast<const DWORD*>(0x2745E98);
    rfa::LogDetail("Wwise memory: %s; the audio thread's time %ld ms; the game's audio pool %lu of %lu KB; its stream cache %lu hits, %lu misses", line,
             WwiseAudioTime(), *reinterpret_cast<const DWORD*>(audioPool + 0x4C) / 1024, *reinterpret_cast<const DWORD*>(audioPool + 0x44) / 1024,
             audioCache[3], audioCache[4]);
}

// The streams' memory at the instant Wwise says a source starved: its Stream I/O pool (the pieces of 26 KB every
// stream reads into: with all of them in hand a stream waits for one) and its Stream Manager pool (the streams'
// own records). Full there, the starvation is the memory's; with room there, the reading didn't keep up. At most
// four lines a second, 60 in all.
void LogStreamMemoryAtStarvation() {
    static DWORD last = 0;
    static int logged = 0;
    const DWORD now = GetTickCount();
    if (logged >= 60 || now - last < 250) return;
    last = now;
    ++logged;
    char line[400] = "";
    size_t at = 0;
    for (long pool = 0, pools = WwiseMaxPools(); pool < pools && pool < 64 && at < sizeof(line) - 120; ++pool) {
        DWORD stats[16] = {};
        if (WwisePoolStats(pool, stats) != 1 || stats[0] == 0) continue;
        const wchar_t* wide = WwisePoolName(pool);
        char name[40] = "";
        for (size_t i = 0; wide && wide[i] && i + 1 < sizeof(name); ++i) {
            name[i] = static_cast<char>(wide[i] & 0xFF);
            name[i + 1] = 0;
        }
        if (strncmp(name, "Stream", 6) != 0) continue;
        at += snprintf(line + at, sizeof(line) - at, "%s%s %lu of %lu KB (the most ever %lu KB, %lu requests, %lu given back)", at ? "; " : "", name,
                       stats[1] / 1024, stats[0] / 1024, stats[5] / 1024, stats[3], stats[4]);
    }
    const DWORD* audioCache = reinterpret_cast<const DWORD*>(0x2745E98);
    rfa::Log("Wwise: a source starved with %s; the game's stream cache %lu hits, %lu misses", line, audioCache[3], audioCache[4]);
}

void __fastcall EffectSound(BYTE* effect, void* unused, DWORD start) {
    g_effectSound(effect, unused, start);
    LogWwiseMemory();
    if (!effect) return;
    const DWORD handle = *reinterpret_cast<const DWORD*>(effect + kEffectSoundHandle), event = *reinterpret_cast<const DWORD*>(effect + kEffectSoundEvent);
    const float* at = reinterpret_cast<const float*>(effect + kEffectPosition);
    // A watched sound's effect, on a later frame: how far it has moved from where the sound started.
    for (WatchedSound& watched : g_watchedSounds) {
        if (!watched.handle || watched.effect != effect || watched.handle != handle) continue;
        float moved = 0;
        for (int i = 0; i < 3; ++i) moved += (at[i] - watched.at[i]) * (at[i] - watched.at[i]);
        moved = sqrtf(moved);
        if (moved > watched.moved) watched.moved = moved;
        return;
    }
    if ((start & 0xFF) == 0 || g_soundEndsLogged >= kSoundEndsLogged + 300) return;
    if (!handle || !event || EventIsLoop(event)) return;
    BYTE* visual = Visual(*reinterpret_cast<int*>(effect + kEffectObjectVisual));
    const char* name = visual ? reinterpret_cast<const char*>(visual) : "an effect without a visual";
    if (visual && _strnicmp(name, "rfg_", 4) != 0) return;
    for (WatchedSound& watched : g_watchedSounds) {
        if (watched.handle) continue;
        watched.event = event;
        watched.gameObject = SoundGameObject(handle);
        watched.started = GetTickCount();
        watched.effect = effect;
        float away = 0;
        for (int i = 0; i < 3; ++i) {
            watched.at[i] = at[i];
            away += (at[i] - g_cameraPosition[i]) * (at[i] - g_cameraPosition[i]);
        }
        watched.fromCamera = sqrtf(away);
        watched.moved = 0;
        watched.wwise[0] = 0;
        watched.wwiseAfter = 0;
        strncpy_s(watched.visual, name, _TRUNCATE);
        watched.handle = handle;  // last: Wwise's messages come on another thread
        return;
    }
}

// Making an object in the world (0x6D44E0; the world in ECX, then its type, its zone and one more):
// none when its type's pool has none free (0x6D4650). Wrapped to write any failure to the log.
constexpr DWORD kMakeObject = 0x6D44E0;
constexpr size_t kMakeObjectPrologue = 6;  // push ebp / mov ebp,esp / sub esp,10h
using MakeObjectFn = void*(__fastcall*)(void* world, void* unused, void* type, void* zone, DWORD a3);
MakeObjectFn g_makeObject;
int g_objectFailuresLogged = 0;

void* __fastcall MakeObject(void* world, void* unused, void* type, void* zone, DWORD a3) {
    void* object = g_makeObject(world, unused, type, zone, a3);
    if (!object && g_objectFailuresLogged < 40) {
        ++g_objectFailuresLogged;
        rfa::Log("Objects: the game could not make an object (type %p, zone %p)", type, zone);
    }
    return object;
}

// Gives the gameplay properties' name pool kGameplayNameRoom bytes of the mod's own when the mod hands
// the game its table (the game's own buffer is left unused); every other pool is the game's.
void __fastcall NamePoolInit(void* pool, void* unused, void* buffer, DWORD size, const char* name) {
    if (!g_gameplayTable.bytes.empty() && name && strcmp(name, kGameplayNames) == 0) {
        g_gameplayNames.assign(kGameplayNameRoom, 0);
        rfa::Log("Gameplay properties: their names get %lu bytes (the game's %lu)", kGameplayNameRoom, size);
        g_namePoolInit(pool, unused, g_gameplayNames.data(), kGameplayNameRoom, name);
        return;
    }
    g_namePoolInit(pool, unused, buffer, size, name);
}

// ---- Shaders

// Guerrilla's sky, as its shader works it out, over the whole screen. The vertex shader makes one
// triangle that covers the screen, at the far plane; the pixel shader turns each pixel back into
// the direction it looks in and colours it, and lays Guerrilla's fog over it as its dome's shaders do
// (rfg-skybox_s): the dome (rfg_skybox, 150 m across, from 23.2 m below its middle to 75 m above)
// stretched across to the far clip and up to the fog scale (its height from the bottom times the scale
// ÷ its half-height, less that half-height), raised by the fog offset, and fogged there as the world's
// surfaces are (Guerrilla's height fog, as kFogShader); the sky's colour then mixed toward the fog's.
constexpr char kSkyShader[] = R"(
cbuffer Sky : register(b0)
{
    float4 Hemisphere[10];  // west0-3, zenith, east0-3, zenith: each colour times its brightness
    float4 Right;           // the camera's axes in the world; Right.w: tan of half the horizontal field of view
    float4 Up;              // Up.w: tan of half the vertical field of view
    float4 Forward;         // Forward.w: the far plane's depth (1, or 0 if the game's depth runs the other way)
    float4 FogColour;       // the fog's colour (times the colour correction and the sky's brightness)
    float4 FogNumbers;      // as the world's fog: x the fall-off with height, y the density at the camera's height, w the offset
    float4 DomeFog;         // x the far clip, y the fog scale, z the fog offset less the camera's height
};

// Guerrilla's own dome (skybox.vpp_pc): a sphere of radius 10 in 16 rings 7.5° apart, from 16.2°
// below the horizon to the top, so half as high as from its lowest point to its top: 6.39495.
// (Armageddon's copy of the dome is 75 across; the fog follows the dome's own numbers.)
static const float kDomeRadius = 10, kDomeHalfHeight = 6.39495;

// Guerrilla's height fog at a point (from the camera): 1 − min(2^(−(term × distance × density − offset)), 1).
float HeightFog(float3 at)
{
    float kh = at.y * FogNumbers.x;
    float term = abs(at.y) > 0.01 ? (1 - exp2(kh * -1.442695)) / kh : 1;
    return 1 - min(exp2(-(term * length(at) * FogNumbers.y - FogNumbers.w)), 1);
}

struct Pixel
{
    float4 position : SV_Position;
    float2 screen : TEXCOORD0;  // -1 to 1 across the screen, up is +1
};

Pixel SkyVertex(uint id : SV_VertexID)
{
    // (-1, -1), (3, -1), (-1, 3): one triangle over the whole screen.
    float2 screen = float2((id << 1) & 2, id & 2) * 2 - 1;
    Pixel p;
    p.position = float4(screen, Forward.w, 1);
    p.screen = screen;
    return p;
}

float4 SkyPixel(Pixel p) : SV_Target
{
    float3 look = normalize(Forward.xyz + Right.xyz * (p.screen.x * Right.w) + Up.xyz * (p.screen.y * Up.w));
    // How far round from west (-x) to east (+x), 0 to 1.
    float across = acos(clamp(-look.x, -1, 1)) / 3.14159265;
    // How high: the band, 0 at the horizon to 4 straight up (below the horizon, the horizon's).
    float height = saturate(look.y);
    float band = 2 * (asin(height) / 1.57079633 + height);
    int below = (int)min(floor(band), 3);
    float between = band - below;
    float3 west = lerp(Hemisphere[below].rgb, Hemisphere[below + 1].rgb, between);
    float3 east = lerp(Hemisphere[below + 5].rgb, Hemisphere[below + 6].rgb, between);
    float3 dome = look * kDomeRadius;
    float3 fogAt = float3(dome.x / kDomeRadius * DomeFog.x,
                          (dome.y + kDomeHalfHeight) * DomeFog.y / kDomeHalfHeight - kDomeHalfHeight + DomeFog.z,
                          dome.z / kDomeRadius * DomeFog.x);
    return float4(lerp(lerp(west, east, across), FogColour.rgb, HeightFog(fogAt)), 1);
}
)";

// Guerrilla's clouds, as its cloud shader draws them. The model's points are directions from the
// eye; they go just in front of the far plane (so behind everything the world has drawn), and come
// out with their colour already times their cover (laid over the sky with "one, one minus alpha").
// Fogged as Guerrilla's cloud and backdrop shaders fog them, each point of the model: its part
// stretched across to the far clip (its point across times the far clip ÷ the part's half-width) and
// up to the fog scale ((its height + the part's half-height) × the scale ÷ that half-height), raised
// by the fog offset, and fogged there as the world's surfaces are.
constexpr char kCloudShader[] = R"(
cbuffer Clouds : register(b0)
{
    float4 Right;      // as the sky's
    float4 Up;
    float4 Forward;    // Forward.w: 1 if the far plane's depth is 1, 0 if it is 0
    float4 Light;      // the time of day's light direction
    float4 Front;      // the clouds' front and rear light colours (times their brightness)
    float4 Rear;
    float4 Strengths;  // the four layers' strengths
    float4 Shape;      // x: the height they fade in from, y: the height they are full at, z: rim light strength, w: rim light power
    float4 Drift;      // x: layers 0-1's offset, y: layers 2-3's; z: 1 for the overhead clouds (turned, not slid)
    float4 Ambient;    // added to the clouds' light: 0, as Guerrilla's sky ambient
    float4 FogColour;  // the fog's colour
    float4 FogNumbers; // as the world's fog: x the fall-off with height, y the density at the camera's height, w the offset
    float4 FogScale;   // x the far clip ÷ the part's half-width, y the fog scale ÷ its half-height, z the far clip ÷ its half-depth, w its half-height
    float4 FogRaise;   // x the fog offset less the camera's height
};

float HeightFog(float3 at)
{
    float kh = at.y * FogNumbers.x;
    float term = abs(at.y) > 0.01 ? (1 - exp2(kh * -1.442695)) / kh : 1;
    return 1 - min(exp2(-(term * length(at) * FogNumbers.y - FogNumbers.w)), 1);
}

Texture2D Layers01 : register(t0);
Texture2D Layers23 : register(t1);
SamplerState Wrapped : register(s0);

struct CloudPoint
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float3 look : TEXCOORD1;
    float4 layers : TEXCOORD2;  // xy: where layers 0-1 are read, zw: layers 2-3
    float fog : TEXCOORD3;
};

CloudPoint CloudVertex(float3 position : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD)
{
    float3 look = normalize(position);
    float depth = dot(look, Forward.xyz);
    CloudPoint p;
    p.position = float4(dot(look, Right.xyz) / Right.w, dot(look, Up.xyz) / Up.w, depth * lerp(0.000001, 0.999999, Forward.w), depth);
    p.normal = normal;
    p.look = look;
    if (Drift.z > 0.5)
    {
        // The overhead clouds, as Guerrilla's rfg-skybox-clouds-2 lays them: each pair of layers turned
        // about the top of the dome by its offset (half a turn an offset of 1) and tiled 4 and 4.5 times.
        float2 c = float2(uv.x - 0.5, uv.y + 0.5);
        float s01, c01, s23, c23;
        sincos(-3.14159 * Drift.x, s01, c01);
        sincos(-3.14159 * Drift.y, s23, c23);
        p.layers.xy = (float2(c.x * c01 - c.y * s01, c.y * c01 + c.x * s01) + 0.5) * 4.0;
        p.layers.zw = (float2(c.x * c23 - c.y * s23, c.y * c23 + c.x * s23) + 0.5) * 4.5;
    }
    else
    {
        // The ones along the horizon (rfg-skybox-clouds): slid round it.
        p.layers = float4(uv.x + Drift.x, uv.y, uv.x + Drift.y, uv.y);
    }
    p.fog = HeightFog(float3(position.x * FogScale.x, (position.y + FogScale.w) * FogScale.y + FogRaise.x, position.z * FogScale.z));
    return p;
}

float4 CloudPixel(CloudPoint p) : SV_Target
{
    float4 layers01 = Layers01.Sample(Wrapped, p.layers.xy);
    float4 layers23 = Layers23.Sample(Wrapped, p.layers.zw);
    float fade = saturate((clamp(p.look.y, -1, 1) - Shape.x) / (Shape.y - Shape.x));
    // Each layer's cover (green and alpha), and how much of what is behind it shows through.
    float4 cover = float4(layers01.g, layers01.a, layers23.g, layers23.a) * Strengths;
    float4 through = 1 - cover * fade;
    float covered = 1 - through.x * through.y * through.z * through.w;
    // Their light (red and blue), each layer over the ones before.
    float light = layers01.r * through.y + cover.y * fade * layers01.b;
    light = through.z * light + cover.z * fade * layers23.r;
    light = saturate(through.w * light + cover.w * fade * layers23.b);
    // Front or rear light by which way the cloud faces the light; a rim where it is thin, towards the sun.
    float3 lit = lerp(Front.rgb, Rear.rgb, dot(normalize(p.normal), Light.xyz) * 0.5 + 0.5);
    float rim = min(exp2(-covered * Shape.w), 1);  // base 2, as Guerrilla's shader's exp
    float towardsSun = pow(saturate(dot(normalize(p.look), Light.xyz)), 30);
    lit += Front.rgb * Shape.z * rim * towardsSun;
    float alpha = covered * covered;
    float3 colour = lerp(light * lit + Ambient.rgb, FogColour.rgb, p.fog);
    return min(float4(colour * alpha, alpha), 32);
}

// The backdrop, as Guerrilla's backdrop shader (rfg-skybox-matte_s) draws it. With it the buffer's
// Front and Rear are the mountains' colour on the side facing the light and on the far side, and
// Strengths is the mountain haze: its colour (rgb) and how thick it is (a).
float4 BackdropPixel(CloudPoint p) : SV_Target
{
    float4 picture = Layers01.Sample(Wrapped, p.layers.xy);
    float3 mountain = lerp(Rear.rgb, Front.rgb, dot(normalize(p.normal), Light.xyz) * 0.5 + 0.5);
    float3 colour = lerp(lerp(picture.rgb, Strengths.rgb, Strengths.a) * mountain, FogColour.rgb, p.fog);
    return min(float4(colour * picture.a, picture.a), 32);
}
)";

// The sky's light and the sun, added into the game's light buffer over the whole screen. Each pixel
// reads its surface's normal from the G-buffer (in the camera's space, as the game's ambient light
// shader decodes it: 0-1 to -1-1, the top half of z a flag) and gets the sky's light from above to
// below by how much it faces up (as ir_light_ambient blends them), plus the sun's light by how much
// it faces the sun. And the sun's shine, in the buffer's alpha as the game's lamps write theirs
// (ir_light_point): the sun reflected off the surface, how squarely toward the eye to the power of
// the surface's sharpness (the G-buffer normals' alpha: the square root of its Specular_Power / 50,
// the top half a flag; the power × 5.12), times how much it faces the sun and the sun's brightness.
// A surface's own shader takes that times its specular map and Specular_Color; the ground's ignores it.
// Drawn in two parts when the sun has its shadows (see "The sun's shadows"): the sun's light, through
// the sun's lamp's shadow map, as the light pass draws that lamp, then the sky's light after the pass.
// The shadow is the game's own test (ir_light_spot): the pixel's place in the camera's space (from its
// depth, as the lamps work it out: the ray through the lamp's inverse projection, times the distance
// ahead from IR_Similarity_Data), moved out a little along its normal, taken into the shadow map by
// Shadow_map_projTM, compared with the map over three texels round it, weighed as the game does; it
// fades out away from the camera and leaves at least Shadow_map_fade_params.x.
constexpr char kLightShader[] = R"(
cbuffer Light : register(b0)
{
    float4 FromAbove;   // the sky's light on a surface facing the light (Guerrilla's V_ambient_render)
    float4 FromBelow;   // and facing away from it (V_back_ambient_render)
    float4 SunColour;   // the sun's light on a surface facing it (0 while it is down)
    float4 SunToward;   // toward the sun, in the camera's space
    float4 UpToward;    // straight up, in the camera's space
    float4 TargetSize;  // the light buffer's width and height, and the tangents of half the field of view across and up
    float4 Parts;       // x 1: the sky's light; y 1: the sun's; z 1: the sun's through the lamp's shadow map; w the shine's strength (Shine=)
};

cbuffer Game : register(b1)
{
    float4 GameUnused[12];
    float4 SimilarityData;  // zw: the camera's distance ahead = 1 / (z × depth + w)
};

cbuffer Lamp : register(b2)  // the sun's lamp's, as the light pass drew it (ir_light_spot's vc0)
{
    float4 LampPosition;
    float4 InverseProjection[4];
    float4 SpotDirection;
    float4 ShadowRows[4];  // Shadow_map_projTM: x, y, depth and w in the map from the camera's space
    float4 ShadowFade;     // x the least light left, y and z the fade by distance ahead, w a texel's size
};

Texture2D Normals : register(t0);
Texture2D ShadowMap : register(t1);
Texture2D Depth : register(t2);
SamplerState Nearest : register(s0);

// facing: how squarely the surface faces the sun (its normal · toward the sun); away: which way
// to move the point looked up off the surface (its normal, or toward the sun where the normal faces
// away). The normals are in the camera's space as the positions worked out here are (the game's lamps
// light with both).
float SunShadow(float2 at, float3 away, float facing)
{
    float depth = Depth.SampleLevel(Nearest, at, 0).x;
    float ahead = 1 / (SimilarityData.z * depth + SimilarityData.w);
    float2 corner = at * float2(2, -2) + float2(-1, 1);
    float4 near = InverseProjection[0] * corner.x + InverseProjection[1] * corner.y + InverseProjection[3];
    float3 ray = near.xyz / near.w;
    float4 surface = float4(ray / ray.z * ahead, 1);
    // So that a surface doesn't shadow itself through the map's wide texels (a texel's width in metres
    // from the map's width, its depth per metre from the map's depth), the point looked up is moved
    // out along the surface's normal, more the more it slants from the sun, and its depth held back
    // by a texel or two. Moving out instead of holding the depth far back keeps thin overhangs (a
    // roof's edge just above a wall) casting their shadow on what is under them.
    float texel = ShadowFade.w / length(ShadowRows[0].xyz);
    float slant = sqrt(saturate(1 - facing * facing));
    float4 lookup = float4(surface.xyz + away * texel * (1 + 2 * slant), 1);
    float4 inMap = float4(dot(ShadowRows[0], lookup), dot(ShadowRows[1], lookup), dot(ShadowRows[2], lookup), dot(ShadowRows[3], lookup));
    float3 mapped = inMap.xyz / inMap.w;
    if (any(mapped.xy < 0) || any(mapped.xy > 1)) return 1;
    float slope = min(slant / max(facing, 0.05), 1);
    float bias = (1 + slope) * texel * length(ShadowRows[2].xyz);
    // The test over a square three texels across centred on the point, each texel counted by how much
    // of it the square covers (the game's own lamps weigh theirs the same way): the edge slides
    // smoothly from texel to texel instead of stepping.
    uint width, height;
    ShadowMap.GetDimensions(width, height);
    float2 start = mapped.xy * float2(width, height) - 1.5;
    float2 first = floor(start);
    float2 part = start - first;
    float4 across = float4(1 - part.x, 1, 1, part.x);
    float4 down = float4(1 - part.y, 1, 1, part.y);
    float lit = 0;
    [unroll] for (int y = 0; y < 4; ++y)
        [unroll] for (int x = 0; x < 4; ++x)
        {
            int2 cell = clamp(int2(first) + int2(x, y), 0, int2(width, height) - 1);
            float stored = ShadowMap.Load(int3(cell, 0)).x;
            lit += (stored < mapped.z - bias ? 0 : 1) * across[x] * down[y];
        }
    float fade = saturate((ShadowFade.y - surface.z) * ShadowFade.z);
    return max(1 - (1 - lit / 9) * fade, ShadowFade.x);
}

float4 LightVertex(uint id : SV_VertexID) : SV_Position
{
    float2 corner = float2((id << 1) & 2, id & 2);
    return float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
}

// Two targets: the light buffer, and the mod's own light for Guerrilla's material shaders (the same
// light, before the game's ambient occlusion, and in alpha the sun's shadow: see SurfaceInputs).
struct LightOut
{
    float4 light : SV_Target0;
    float4 ours : SV_Target1;
};

LightOut LightPixel(float4 position : SV_Position)
{
    float2 at = position.xy / TargetSize.xy;
    float4 stored = Normals.Sample(Nearest, at);
    float3 n = stored.xyz;
    if (n.z >= 0.501961) n.z -= 0.5;
    n = n * 2 - 1;
    float length2 = dot(n, n);
    n = length2 > 0.000001 ? n * rsqrt(length2) : UpToward.xyz;
    // As Re-Mars-tered's surface shaders light with the sky (their indirect light): by how much the
    // surface faces its light (n · L / 2 + 1/2), not the sky above, from V_back_ambient_render facing
    // away to the light's own colour × 0.019 facing it, blended toward V_ambient_render by up to ½ on a
    // surface turned sideways (facing ¼ to ¾); the occlusion comes after, from the game's ambient
    // occlusion, which the light buffer gets once this is drawn.
    float facingLight = saturate(dot(n, SunToward.xyz) * 0.5 + 0.5);
    float side = 2 * abs(2 * facingLight - 1);
    float3 sky = lerp(lerp(FromBelow.rgb, SunColour.rgb * 0.019, facingLight), FromAbove.rgb, max(1 - side * side, 0) * 0.5);
    float facing = dot(n, SunToward.xyz);
    float3 sun = SunColour.rgb * saturate(facing);

    float sharpness = (stored.w >= 0.501961 ? stored.w - 0.501961 : stored.w) * 14.166667;
    sharpness = sharpness * sharpness * 5.12;
    float3 eye = normalize(float3((at * float2(2, -2) + float2(-1, 1)) * TargetSize.zw, 1));  // from the camera to the surface
    float3 reflection = 2 * facing * n - SunToward.xyz;
    float toEye = max(saturate(dot(reflection, -eye)), 0.000001);
    float shine = saturate(facing) * pow(toEye, sharpness) * dot(SunColour.rgb, float3(0.3, 0.59, 0.11));
    // Worked out facing away from the sun too: Guerrilla's metal is lit there (its lobe), shadowed. There
    // the point looked up is moved toward the sun, not along the normal: a normal facing away (the
    // ground's, under a cliff fading over it on a steep wall) would move it into the rock, and the
    // surface shadowed itself (straight-edged dark triangles).
    float shadow = Parts.z > 0 ? SunShadow(at, facing > 0 ? n : SunToward.xyz, max(facing, 0.05)) : 1;
    LightOut o;
    o.light = float4(sky * Parts.x + sun * Parts.y * shadow, shine * Parts.y * shadow * Parts.w);
    o.ours = float4(o.light.rgb, Parts.y * shadow);
    return o;
}
)";

// Guerrilla's fog over what the world has drawn: for each pixel its surface's distance and height
// from the camera (the G-buffer's depth, turned into a distance with the numbers the game's lamps use,
// IR_Similarity_Data in its constants vc4), and the fog there as Guerrilla's shaders work it out per
// vertex (rfg-nodecal_s and the rest): term = (1 − e^(−k·dy)) / (k·dy), 1 when nearly level; fog =
// 1 − min(2^(−(term × distance × density − offset)), 1). Blended over the picture by that amount, as
// Guerrilla's surfaces mix in Fog_color. Pixels with nothing drawn (depth at the far plane) are the sky's.
constexpr char kFogShader[] = R"(
cbuffer Fog : register(b0)
{
    float4 FogColour;   // the fog's colour (times the colour correction)
    float4 FogNumbers;  // x the fall-off with height, y the density at the camera's height (powers of two a metre), w the offset
    float4 UpToward;    // straight up, in the camera's space; w the depth buffer's value at the far plane
    float4 TargetSize;  // the picture's width and height, and the tangents of half the field of view across and up
};

cbuffer Game : register(b1)
{
    float4 Unused[12];
    float4 SimilarityData;  // zw: the camera's distance ahead = 1 / (z × depth + w)
};

Texture2D Depth : register(t0);
SamplerState Nearest : register(s0);

float4 FogVertex(uint id : SV_VertexID) : SV_Position
{
    float2 corner = float2((id << 1) & 2, id & 2);
    return float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 FogPixel(float4 position : SV_Position) : SV_Target
{
    float2 at = position.xy / TargetSize.xy;
    float depth = Depth.Sample(Nearest, at).x;
    if (abs(depth - UpToward.w) < 0.0000001) discard;
    float ahead = 1 / (SimilarityData.z * depth + SimilarityData.w);
    float3 offset = float3((at * float2(2, -2) + float2(-1, 1)) * TargetSize.zw, 1) * ahead;  // the surface, from the camera
    float dy = dot(offset, UpToward.xyz);
    float kh = dy * FogNumbers.x;
    float term = abs(dy) > 0.01 ? (1 - exp2(kh * -1.442695)) / kh : 1;
    float fog = 1 - min(exp2(-(term * length(offset) * FogNumbers.y - FogNumbers.w)), 1);
    return float4(FogColour.rgb, saturate(fog));
}
)";

// Guerrilla's tone mapping (its rl_hdr passes 11-13, 12 and 19), drawn over Armageddon's final picture
// from the scene it was made from (held at exposure 1, so Guerrilla's own values). Guerrilla's scene
// holds the picture ÷ 8 (its lights, sky and fog colours as the time of day gives them), and every
// pass that reads it takes it × 8 (pass 19: `mul r3, r0, 8`; the bright pass 24 the same); ours holds
// the same values (the same lights, sky and fog), so these passes take it × 8 too (kSceneScale).
// AdaptPixel, into a 1 x 1 target: the picture's brightness as Guerrilla measures it, 36 × each
// sample's luminance (Rec. 709, on the scene as kept), each held to luminance_min-max, averaged over a
// grid, and the eye's brightness eased toward it, by 1 − 2^(−0.8744 × the frame's time × iris_rate).
// ToneMapPixel: the exposure, lum_range ÷ max(1 + lum_offset × (base + amount × (adapted − base)),
// 0.1) (eye_adaption_base, _amount), times Exposure=; the picture 1 − e^(−exposure × (8 × scene +
// (bloom + 8 × sun shafts) × bloom_amount)), encoded for the screen (sRGB: Guerrilla's display gamma,
// 2.4). Guerrilla draws its sun shafts into the texture its bloom is then laid into, so they go with
// the bloom; our bloom is made × 8 already (see kBloomShader), our shafts from the scene as kept.
constexpr char kToneMappingShader[] = R"(
cbuffer ToneMapping : register(b0)
{
    float4 Adapt;     // x how far the eye moves toward the measure this frame (1: all the way), y z the range each sample is held to
    float4 Exposure;  // x lum_range, y lum_offset, z eye_adaption_base, w eye_adaption_amount
    float4 Picture;   // x Exposure=, y bloom_amount, z w the picture's width and height
    float4 Shafts;    // x how strong the sun shafts are (SunShafts=; 0 when none were drawn)
    float4 Tint;      // xyz the colour correction (Guerrilla's Tint_color), w its saturation (Tint_saturation.x)
};

Texture2D Scene : register(t0);
Texture2D Bloom : register(t1);
Texture2D Adapted : register(t2);  // 1 x 1: the brightness the eye has adapted to
Texture2D SunShafts : register(t3);
Texture2D Particles : register(t4);  // Guerrilla's particle layer: their colour, and in alpha how much they cover (none: zeros)
SamplerState Smooth : register(s0);

static const float kSceneScale = 8;  // Guerrilla's scene holds the picture ÷ 8

float4 ToneVertex(uint id : SV_VertexID) : SV_Position
{
    float2 corner = float2((id << 1) & 2, id & 2);
    return float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 AdaptPixel(float4 position : SV_Position) : SV_Target
{
    float total = 0;
    for (int y = 0; y < 18; ++y)
        for (int x = 0; x < 32; ++x)
        {
            int2 at = int2((x + 0.5) * Picture.z / 32, (y + 0.5) * Picture.w / 18);
            float3 colour = max(Scene.Load(int3(at, 0)).rgb, 0);
            total += clamp(4.5 * kSceneScale * dot(colour, float3(0.2125, 0.7154, 0.0721)), Adapt.y, Adapt.z);
        }
    float measured = total / 576;
    float before = Adapted.Load(int3(0, 0, 0)).r;
    return float4(Adapt.x >= 1 ? measured : before + (measured - before) * Adapt.x, 0, 0, 1);
}

float4 TonePixel(float4 position : SV_Position) : SV_Target
{
    float3 colour = max(Scene.Load(int3(position.xy, 0)).rgb, 0) * kSceneScale;
    float3 bloom = max(Bloom.Sample(Smooth, position.xy / Picture.zw).rgb, 0) * Picture.y;
    float3 shafts = max(SunShafts.Sample(Smooth, position.xy / Picture.zw).rgb, 0) * kSceneScale * Picture.y * Shafts.x;
    float adapted = Adapted.Load(int3(0, 0, 0)).r;
    float exposure = Exposure.x / max(1 + Exposure.y * (Exposure.z + Exposure.w * (adapted - Exposure.z)), 0.1) * Picture.x;
    float4 particles = Particles.Load(int3(position.xy, 0));
    float3 laid = min(max(particles.rgb, 0), 0.996);
    float behind = saturate(1 - particles.a);
    // The colour correction, on the scene alone as Guerrilla's last pass does it: its saturation about
    // its luminance, then its tint; the bloom, the sun shafts and the particles go over it as they are.
    float luminance = dot(colour, float3(0.3, 0.59, 0.11));
    colour = (luminance + Tint.w * (colour - luminance)) * Tint.rgb;
    float3 picture = saturate(1 - (1 - laid) * exp(-exposure * (colour * behind + bloom + shafts)));
    float3 encoded = picture <= 0.0031308 ? picture * 12.92 : 1.055 * pow(picture, 1 / 2.4) - 0.055;
    return float4(encoded, 1);
}
)";

// Guerrilla's bloom, as its rl_hdr_compositor makes it (render_bloom_3; the 2009 shaders rl_downsample,
// rl_particle_brightpass, rl_hdr 24 and 22, rl_gaussian_blur), in true values: Guerrilla's bright
// pass takes its scene (the picture ÷ 8, as ours) × 8 and stores its bloom × 0.125 for the last pass
// to take × 8 again; ours takes the scene × 8 (kSceneScale) and keeps the bloom as it is.
// BrightPixel, at a quarter of the picture's size: the scene boxed 4 × 4 times bloom_exposure ÷ the
// eye (eye_adaption_base + _amount × (the adapted brightness − base) + 0.001); of that, as much as the
// curve gives (its brightness L = 0.3 × (r + g + b); below bloom_theta, slope_A × L; above, slope_B ×
// (L − theta) + theta × slope_A), times what the particles leave showing, plus the particles' own glow
// (each 0.85 over, ÷ (that + 0.3) × 3), never below 0. BoxPixel: half the size again, 4 × 4 samples
// a picture-texel apart (as Guerrilla's, whatever the size). BlurPixel: 9 taps along one axis
// (a 17-tap Gaussian with each pair folded into one filtered tap). CombinePixel: 2 × the first stage +
// 1.15 × the second + 0.45 × the third.
constexpr char kBloomShader[] = R"(
cbuffer Bloom : register(b0)
{
    float4 Bright;   // x bloom_exposure, y eye_adaption_base, z eye_adaption_amount
    float4 Curve;    // bloom_theta, bloom_slope_A, bloom_slope_B, theta × slope_A
    float4 Sizes;    // xy 1 ÷ the target's size, zw 1 ÷ the picture's
    float4 Taps[9];  // xy where (in the texture), w how much
};

Texture2D Source : register(t0);
Texture2D Adapted : register(t1);    // 1 x 1: the brightness the eye has adapted to
Texture2D Particles : register(t2);  // their colour, and in alpha how much they cover (none: zeros)
Texture2D Stage1 : register(t3);
Texture2D Stage2 : register(t4);
SamplerState Smooth : register(s0);

float4 BrightPixel(float4 position : SV_Position) : SV_Target
{
    int2 corner = int2(position.xy) * 4;
    float3 scene = 0, glow = 0;
    float shows = 0;
    [unroll] for (int y = 0; y < 4; ++y)
        [unroll] for (int x = 0; x < 4; ++x)
        {
            scene += Source.Load(int3(corner + int2(x, y), 0)).rgb;
            float4 p = Particles.Load(int3(corner + int2(x, y), 0));
            float3 over = max(p.rgb - 0.85, 0);
            glow += over / (over + 0.3) * 3;
            shows += 1 - p.a;
        }
    scene /= 16;
    glow /= 16;
    shows /= 16;
    float adapted = Adapted.Load(int3(0, 0, 0)).r;
    float3 c = scene * 8 * (Bright.x / (Bright.y + Bright.z * (adapted - Bright.y) + 0.001));  // (the scene × 8, as the tone mapping's kSceneScale)
    float L = dot(c, 0.3);
    float k = L < Curve.x ? Curve.y * L : Curve.z * abs(Curve.x - L) + Curve.w;
    c = c * k / (L + 0.001);
    return float4(min(max(c * shows + glow, 0), 65000), 1);  // (65000: a picture's inf or NaN never spreads)
}

float4 BoxPixel(float4 position : SV_Position) : SV_Target
{
    float2 uv = position.xy * Sizes.xy;
    float4 sum = 0;
    [unroll] for (int j = 0; j < 4; ++j)
        [unroll] for (int i = 0; i < 4; ++i)
            sum += Source.SampleLevel(Smooth, uv + float2(i - 1.5, j - 1.5) * Sizes.zw, 0);
    return sum / 16;
}

float4 BlurPixel(float4 position : SV_Position) : SV_Target
{
    float2 uv = position.xy * Sizes.xy;
    float4 sum = 0;
    [unroll] for (int i = 0; i < 9; ++i) sum += Source.SampleLevel(Smooth, uv + Taps[i].xy, 0) * Taps[i].w;
    return sum;
}

float4 CombinePixel(float4 position : SV_Position) : SV_Target
{
    float2 uv = position.xy * Sizes.xy;
    return Source.SampleLevel(Smooth, uv, 0) * 2 + Stage1.SampleLevel(Smooth, uv, 0) * 1.15 + Stage2.SampleLevel(Smooth, uv, 0) * 0.45;
}
)";

// The sun and moons, as Guerrilla's sky draws its orbitals (rl_skybox_renderable_instance::
// render_orbital_object): a square facing the camera, centred one unit from the eye toward the orbital
// and as many units across as twice its scale, behind everything the world has drawn; its texture
// times its colour and opacity (no more than 32), added to what is there by its alpha (or blended by
// it, for an orbital that asks: alpha_blend).
constexpr char kSunShader[] = R"(
cbuffer Sun : register(b0)
{
    float4 Right;    // as the sky's
    float4 Up;
    float4 Forward;
    float4 Centre;   // toward the orbital (xyz), and its half-size one unit away (w)
    float4 Colour;   // its colour times its brightness (rgb), and its opacity (a)
};

Texture2D Disc : register(t0);
SamplerState Clamped : register(s0);

struct SunPoint
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

SunPoint SunVertex(uint id : SV_VertexID)
{
    // A strip of two triangles: (-1, -1), (1, -1), (-1, 1), (1, 1).
    float2 corner = float2(id & 1, id >> 1) * 2 - 1;
    float3 look = Centre.xyz + (Right.xyz * corner.x + Up.xyz * corner.y) * Centre.w;
    float depth = dot(look, Forward.xyz);
    SunPoint p;
    p.position = float4(dot(look, Right.xyz) / Right.w, dot(look, Up.xyz) / Up.w, depth * lerp(0.000001, 0.999999, Forward.w), depth);
    p.uv = corner * float2(0.5, -0.5) + 0.5;
    return p;
}

// As Guerrilla's rfg-orbital: the texture times the orbital's colour and opacity, no more than 32.
float4 SunPixel(SunPoint p) : SV_Target
{
    return min(Disc.Sample(Clamped, p.uv) * Colour, 32);
}
)";

// Guerrilla's sun shafts (its rl_hdr passes 27 and 15 with their vertex shaders 10 and 4; the steps
// between as Re-Mars-tered's rl_hdr_compositor::render_sun_shafts takes them), each at half the
// picture's size. SourcePixel: the sky round the sun, the picture averaged over 4 × 4 samples where
// nothing was drawn (the depth at the far plane), times a round fall-off the sun's size across
// (Guerrilla's sun mask: a texture its compositor makes at start, rl_hdr_compositor::initialize, 64 ×
// 64, 1 at the middle falling in a straight line to 0 at its edge, laid round the sun). Guerrilla's
// pass also multiplies the picture by a mask texture; in Re-Mars-tered that is a picture of its own
// (the colour buffer of m_pSsaoBlurTarget[0], borrowed) that render_scene_part2 clears to black and
// then draws the skybox, and the landmarks, into before the scene: the sky alone, black wherever
// anything else is, so the same pixels as ours. Whether it holds the sky's colour or only some of its
// channels (the sky's pixel shader writes its colour, alpha 1) isn't known; ours counts the sky's
// pixels whole. BlurPixel: a
// 5 × 5 Gaussian. RadialPixel: 16 samples on the way from the pixel toward the sun, the nearer the
// pixel the more each counts (base − k × step ÷ 16), all times a fall-off with the distance from the
// sun across the screen, e^(curve base × (min(distance, radius) ÷ radius)^exponent) + shift (never
// below 0), and the strength ÷ 16.
constexpr char kShaftShader[] = R"(
cbuffer Shafts : register(b0)
{
    float4 Sun;       // xy the sun's place on the picture (0 to 1, down is +y), z the picture's width ÷ height, w the depth at the far plane
    float4 Texel;     // xy one texel of the half-size targets, zw one texel of the picture
    float4 Strength;  // x the strength, y the base, z the step; w the sun's size in half-size texels
    float4 Curve;     // x the radius, y the curve's base, z its exponent, w its shift
    float4 Blur;      // the Gaussian's weights 0, 1 and 2 texels off (x, y, z)
};

Texture2D Source : register(t0);
Texture2D Depth : register(t1);
SamplerState Smooth : register(s0);

static const float kTowardSun[16] = {1, 0.84375, 0.779029, 0.729367, 0.6875, 0.650614, 0.617267, 0.586601,
                                     0.558058, 0.53125, 0.505894, 0.481777, 0.458734, 0.436633, 0.415366, 0.394846};

float4 ShaftVertex(uint id : SV_VertexID) : SV_Position
{
    float2 corner = float2((id << 1) & 2, id & 2);
    return float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
}

// (Its alpha, which nothing draws with, is the share of its 16 taps that were sky: the log's "Sun shafts at" line reads it.)
float4 SourcePixel(float4 position : SV_Position) : SV_Target
{
    float2 at = position.xy * Texel.xy;
    float3 sky = 0;
    float seen = 0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
        {
            float2 tap = at + (float2(x, y) - 1.5) * Texel.zw;
            float depth = Depth.Load(int3(tap / Texel.zw, 0)).x;
            if (abs(depth - Sun.w) < 0.0000001)
            {
                sky += max(Source.SampleLevel(Smooth, tap, 0).rgb, 0);
                seen += 1;
            }
        }
    float fromSun = length(position.xy - Sun.xy / Texel.xy) / Strength.w;  // in the sun's sizes
    return float4(sky / 16 * saturate(1 - 2 * fromSun), seen / 16);
}

float4 BlurPixel(float4 position : SV_Position) : SV_Target
{
    float2 at = position.xy * Texel.xy;
    float weights[3] = {Blur.x, Blur.y, Blur.z};
    float4 sum = 0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
            sum += Source.SampleLevel(Smooth, at + float2(x, y) * Texel.xy, 0) * weights[abs(x)] * weights[abs(y)];
    return sum;
}

float4 RadialPixel(float4 position : SV_Position) : SV_Target
{
    float2 at = position.xy * Texel.xy;
    float2 toSun = Sun.xy - at;
    float distance = length(float2(toSun.x * Sun.z, toSun.y));
    float falloff = max(exp(Curve.y * pow(min(distance, Curve.x) / Curve.x, Curve.z)) + Curve.w, 0);
    float3 sum = 0;
    for (int k = 0; k < 16; ++k)
        sum += Source.SampleLevel(Smooth, lerp(Sun.xy, at, kTowardSun[k]), 0).rgb * (Strength.y - k * Strength.z / 16);
    return float4(sum * falloff * Strength.x / 16, 1);
}
)";

// ---- Drawing

struct LightConstants {
    float fromAbove[4];
    float fromBelow[4];
    float sunColour[4];
    float sunToward[4];
    float upToward[4];
    float targetSize[4];
    float parts[4];
};

struct FogConstants {
    float fogColour[4];
    float fogNumbers[4];
    float upToward[4];
    float targetSize[4];
};

struct SkyConstants {
    float hemisphere[10][4];
    float right[4];
    float up[4];
    float forward[4];
    float fogColour[4];
    float fogNumbers[4];
    float domeFog[4];
};

struct CloudConstants {
    float right[4];
    float up[4];
    float forward[4];
    float light[4];
    float front[4];
    float rear[4];
    float strengths[4];
    float shape[4];
    float drift[4];
    float ambient[4];
    float fogColour[4];
    float fogNumbers[4];
    float fogScale[4];
    float fogRaise[4];
};

// A cloud part from clouds.bin (see the world builder, rfgsky): which indices, overhead (0), along
// the horizon (1) or the backdrop (2), where its clouds fade in, its two textures, and half its size
// across, up and deep (from its points; Guerrilla's fog takes its part's box).
struct CloudPart {
    UINT first = 0, count = 0;
    int kind = 0;
    float fade = 0, full = 1;
    ID3D11ShaderResourceView* layers[2] = {};
    float half[3] = {1, 1, 1};
};

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11VertexShader* g_skyVertex = nullptr;
ID3D11PixelShader* g_skyPixel = nullptr;
ID3D11Buffer* g_skyConstants = nullptr;
ID3D11DepthStencilState* g_skyDepth[2] = {};  // the far plane at 1 (depth test less-or-equal), at 0 (greater-or-equal)
ID3D11BlendState* g_skyBlend = nullptr;
ID3D11RasterizerState* g_skyRaster = nullptr;
bool g_drawingFailed = false;

std::vector<CloudPart> g_cloudParts;  // empty: no clouds
ID3D11VertexShader* g_cloudVertex = nullptr;
ID3D11PixelShader* g_cloudPixel = nullptr;
ID3D11PixelShader* g_backdropPixel = nullptr;
ID3D11InputLayout* g_cloudLayout = nullptr;
ID3D11Buffer* g_cloudVertices = nullptr;
ID3D11Buffer* g_cloudIndices = nullptr;
ID3D11Buffer* g_cloudConstants = nullptr;
ID3D11SamplerState* g_cloudSampler = nullptr;
ID3D11BlendState* g_cloudBlend = nullptr;
ID3D11RasterizerState* g_cloudRaster = nullptr;
float g_drift[2] = {0, 0};
ULONGLONG g_lastDrift = 0;

pD3DCompile g_compile = nullptr;

ID3DBlob* Compile(const char* source, size_t size, const char* entry, const char* target) {
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    if (FAILED(g_compile(source, size, "guerrilla_world", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors))) {
        rfa::Log("The shader %s didn't compile: %s", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
        code = nullptr;
    }
    if (errors) errors->Release();
    return code;
}

// Windows' shader compiler, loaded the first time a shader is needed.
bool LoadCompiler() {
    if (g_compile) return true;
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    g_compile = compiler ? reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
    if (!g_compile) rfa::Log("Could not load Windows' shader compiler (d3dcompiler_47.dll); no sky or light");
    return g_compile != nullptr;
}

// Compiles the sky's shaders and makes the states it draws with, once the game has its device.
bool MakeSkyObjects() {
    if (!LoadCompiler()) return false;
    ID3DBlob* vertex = Compile(kSkyShader, sizeof(kSkyShader) - 1, "SkyVertex", "vs_4_0");
    ID3DBlob* pixel = Compile(kSkyShader, sizeof(kSkyShader) - 1, "SkyPixel", "ps_4_0");
    bool ok = vertex && pixel &&
              SUCCEEDED(g_device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &g_skyVertex)) &&
              SUCCEEDED(g_device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &g_skyPixel));
    if (vertex) vertex->Release();
    if (pixel) pixel->Release();

    D3D11_BUFFER_DESC buffer = {};
    buffer.ByteWidth = sizeof(SkyConstants);
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(g_device->CreateBuffer(&buffer, nullptr, &g_skyConstants));

    // Behind everything: tested against the depth buffer, never written to it.
    D3D11_DEPTH_STENCIL_DESC depth = {};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    ok = ok && SUCCEEDED(g_device->CreateDepthStencilState(&depth, &g_skyDepth[0]));
    depth.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    ok = ok && SUCCEEDED(g_device->CreateDepthStencilState(&depth, &g_skyDepth[1]));

    D3D11_BLEND_DESC blend = {};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(g_device->CreateBlendState(&blend, &g_skyBlend));

    D3D11_RASTERIZER_DESC raster = {};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = FALSE;  // the far plane itself must not be clipped away
    ok = ok && SUCCEEDED(g_device->CreateRasterizerState(&raster, &g_skyRaster));
    if (!ok) rfa::Log("Could not make the sky's shaders or states; no sky");
    return ok;
}

// The light pass's shaders and states.
ID3D11VertexShader* g_lightVertex = nullptr;
ID3D11PixelShader* g_lightPixel = nullptr;
ID3D11Buffer* g_lightConstants = nullptr;
ID3D11BlendState* g_lightBlend = nullptr;
ID3D11DepthStencilState* g_lightDepth = nullptr;
ID3D11RasterizerState* g_lightRaster = nullptr;
ID3D11SamplerState* g_lightSampler = nullptr;
bool g_lightFailed = false;

bool MakeLightObjects() {
    if (!LoadCompiler()) return false;
    ID3DBlob* vertex = Compile(kLightShader, sizeof(kLightShader) - 1, "LightVertex", "vs_4_0");
    ID3DBlob* pixel = Compile(kLightShader, sizeof(kLightShader) - 1, "LightPixel", "ps_4_0");
    bool ok = vertex && pixel &&
              SUCCEEDED(g_device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &g_lightVertex)) &&
              SUCCEEDED(g_device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &g_lightPixel));
    if (vertex) vertex->Release();
    if (pixel) pixel->Release();

    D3D11_BUFFER_DESC buffer = {};
    buffer.ByteWidth = sizeof(LightConstants);
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(g_device->CreateBuffer(&buffer, nullptr, &g_lightConstants));

    // Added to what the lamps put there.
    D3D11_BLEND_DESC blend = {};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(g_device->CreateBlendState(&blend, &g_lightBlend));

    D3D11_DEPTH_STENCIL_DESC depth = {};  // no depth test: every pixel
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    ok = ok && SUCCEEDED(g_device->CreateDepthStencilState(&depth, &g_lightDepth));

    D3D11_RASTERIZER_DESC raster = {};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(g_device->CreateRasterizerState(&raster, &g_lightRaster));

    D3D11_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(g_device->CreateSamplerState(&sampler, &g_lightSampler));
    if (!ok) rfa::Log("Could not make the light pass's shaders or states; the ground keeps the game's lamps only");
    return ok;
}

// The fog's shaders and states.
ID3D11VertexShader* g_fogVertex = nullptr;
ID3D11PixelShader* g_fogPixel = nullptr;
ID3D11Buffer* g_fogConstants = nullptr;
ID3D11BlendState* g_fogBlend = nullptr;
ID3D11DepthStencilState* g_fogDepthTest = nullptr;
ID3D11RasterizerState* g_fogRaster = nullptr;
ID3D11SamplerState* g_fogSampler = nullptr;
bool g_fogFailed = false;

bool MakeFogObjects() {
    if (!LoadCompiler()) return false;
    ID3DBlob* vertex = Compile(kFogShader, sizeof(kFogShader) - 1, "FogVertex", "vs_4_0");
    ID3DBlob* pixel = Compile(kFogShader, sizeof(kFogShader) - 1, "FogPixel", "ps_4_0");
    bool ok = vertex && pixel &&
              SUCCEEDED(g_device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &g_fogVertex)) &&
              SUCCEEDED(g_device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &g_fogPixel));
    if (vertex) vertex->Release();
    if (pixel) pixel->Release();

    D3D11_BUFFER_DESC buffer = {};
    buffer.ByteWidth = sizeof(FogConstants);
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(g_device->CreateBuffer(&buffer, nullptr, &g_fogConstants));

    // The picture becomes the fog's colour by the fog's amount; its alpha is left as it is.
    D3D11_BLEND_DESC blend = {};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    ok = ok && SUCCEEDED(g_device->CreateBlendState(&blend, &g_fogBlend));

    D3D11_DEPTH_STENCIL_DESC depth = {};  // no depth test: the depth is read as a texture
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    ok = ok && SUCCEEDED(g_device->CreateDepthStencilState(&depth, &g_fogDepthTest));

    D3D11_RASTERIZER_DESC raster = {};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(g_device->CreateRasterizerState(&raster, &g_fogRaster));

    D3D11_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(g_device->CreateSamplerState(&sampler, &g_fogSampler));
    if (!ok) rfa::Log("Could not make the fog's shaders or states; no fog");
    return ok;
}

// Guerrilla's tone mapping: its shaders, the eye's brightness (two 1 x 1 textures, last frame's and
// this frame's, swapped each frame), a copy of it to read back for the log, and its states.
struct ToneConstants {
    float adapt[4];
    float exposure[4];
    float picture[4];
    float shafts[4];
    float tint[4];   // the colour correction and its saturation
};

ID3D11VertexShader* g_toneVertex = nullptr;
ID3D11PixelShader* g_adaptPixel = nullptr;
ID3D11PixelShader* g_tonePixel = nullptr;
ID3D11Buffer* g_toneConstants = nullptr;
ID3D11Texture2D* g_eye[2] = {};
ID3D11RenderTargetView* g_eyeTarget[2] = {};
ID3D11ShaderResourceView* g_eyeView[2] = {};
ID3D11Texture2D* g_eyeReadback = nullptr;
ID3D11BlendState* g_eyeBlend = nullptr;   // written as it is
ID3D11BlendState* g_toneBlend = nullptr;  // the picture's red, green and blue
ID3D11SamplerState* g_toneSampler = nullptr;
ID3D11DepthStencilState* g_toneDepth = nullptr;
ID3D11RasterizerState* g_toneRaster = nullptr;

bool MakeToneMappingObjects() {
    if (!LoadCompiler()) return false;
    ID3DBlob* vertex = Compile(kToneMappingShader, sizeof(kToneMappingShader) - 1, "ToneVertex", "vs_4_0");
    ID3DBlob* adapt = Compile(kToneMappingShader, sizeof(kToneMappingShader) - 1, "AdaptPixel", "ps_4_0");
    ID3DBlob* tone = Compile(kToneMappingShader, sizeof(kToneMappingShader) - 1, "TonePixel", "ps_4_0");
    bool ok = vertex && adapt && tone &&
              SUCCEEDED(g_device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &g_toneVertex)) &&
              SUCCEEDED(g_device->CreatePixelShader(adapt->GetBufferPointer(), adapt->GetBufferSize(), nullptr, &g_adaptPixel)) &&
              SUCCEEDED(g_device->CreatePixelShader(tone->GetBufferPointer(), tone->GetBufferSize(), nullptr, &g_tonePixel));
    for (ID3DBlob* blob : {vertex, adapt, tone})
        if (blob) blob->Release();

    D3D11_BUFFER_DESC buffer = {};
    buffer.ByteWidth = sizeof(ToneConstants);
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(g_device->CreateBuffer(&buffer, nullptr, &g_toneConstants));

    D3D11_TEXTURE2D_DESC eye = {};
    eye.Width = eye.Height = 1;
    eye.MipLevels = eye.ArraySize = 1;
    eye.Format = DXGI_FORMAT_R32_FLOAT;
    eye.SampleDesc.Count = 1;
    eye.Usage = D3D11_USAGE_DEFAULT;
    eye.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    const float zero = 0;
    const D3D11_SUBRESOURCE_DATA start = {&zero, sizeof(zero), 0};
    for (int i = 0; i < 2 && ok; ++i)
        ok = SUCCEEDED(g_device->CreateTexture2D(&eye, &start, &g_eye[i])) &&
             SUCCEEDED(g_device->CreateRenderTargetView(g_eye[i], nullptr, &g_eyeTarget[i])) &&
             SUCCEEDED(g_device->CreateShaderResourceView(g_eye[i], nullptr, &g_eyeView[i]));
    eye.Usage = D3D11_USAGE_STAGING;
    eye.BindFlags = 0;
    eye.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ok = ok && SUCCEEDED(g_device->CreateTexture2D(&eye, nullptr, &g_eyeReadback));

    D3D11_BLEND_DESC blend = {};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(g_device->CreateBlendState(&blend, &g_eyeBlend));
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    ok = ok && SUCCEEDED(g_device->CreateBlendState(&blend, &g_toneBlend));

    D3D11_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(g_device->CreateSamplerState(&sampler, &g_toneSampler));

    D3D11_DEPTH_STENCIL_DESC depth = {};  // no depth test
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    ok = ok && SUCCEEDED(g_device->CreateDepthStencilState(&depth, &g_toneDepth));

    D3D11_RASTERIZER_DESC raster = {};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(g_device->CreateRasterizerState(&raster, &g_toneRaster));
    if (!ok) rfa::Log("Could not make the tone mapping's shaders or states; Armageddon's own tone mapping stays");
    return ok;
}

bool ReadWholeFile(const std::wstring& path, std::vector<BYTE>& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart < (64 << 20);
    if (ok) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
    }
    CloseHandle(file);
    return ok;
}

template <typename T>
T ReadAt(const std::vector<BYTE>& bytes, size_t at) {
    T value;
    memcpy(&value, bytes.data() + at, sizeof(T));
    return value;
}

// A texture from a .dds file: 32-bit (blue, green, red, alpha, as the world builder writes them),
// with its mip levels, read as sRGB colour: Guerrilla's renderer reads every texture whose header
// doesn't say it holds data (0x200: its normal maps) that way (rl_render_lib::load_texture), its
// sky's, sun's and moons' included.
ID3D11ShaderResourceView* LoadTexture(const std::wstring& path) {
    std::vector<BYTE> dds;
    if (!ReadWholeFile(path, dds) || dds.size() < 128 || memcmp(dds.data(), "DDS ", 4) != 0) return nullptr;
    const UINT height = ReadAt<UINT>(dds, 12), width = ReadAt<UINT>(dds, 16);
    const UINT mips = ReadAt<UINT>(dds, 28) ? ReadAt<UINT>(dds, 28) : 1;
    const UINT bits = ReadAt<UINT>(dds, 88), red = ReadAt<UINT>(dds, 92);
    if (ReadAt<UINT>(dds, 84) != 0 || bits != 32 || red != 0x00FF0000) return nullptr;  // only 32-bit blue-green-red-alpha
    std::vector<D3D11_SUBRESOURCE_DATA> levels(mips);
    size_t at = 128;
    for (UINT level = 0; level < mips; ++level) {
        const UINT w = width >> level ? width >> level : 1, h = height >> level ? height >> level : 1;
        if (at + size_t(w) * h * 4 > dds.size()) return nullptr;
        levels[level].pSysMem = dds.data() + at;
        levels[level].SysMemPitch = w * 4;
        at += size_t(w) * h * 4;
    }
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = mips;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    if (SUCCEEDED(g_device->CreateTexture2D(&desc, levels.data(), &texture))) {
        g_device->CreateShaderResourceView(texture, nullptr, &view);
        texture->Release();
    }
    return view;
}

// The sun and moons as Guerrilla's sky draws them: the orbitals of the world's global time of day
// (<world>.gtodx, which the world builder writes from Guerrilla's terr01.gtodx and the game finds among
// the overrides; Guerrilla's are the Sun, Phobos, Deimos and a faint second Phobos), read once a world.
// Each its path (a circle turned plane_rot_x° about x, then plane_rot_y° about y, distance_off_plane
// off its middle, orbit_time_offset hours on), its texture (map_name, in mods\guerrilla_world\ as
// .dds), whether it blends rather than adds (alpha_blend), and three curves over the day: scale (its
// half-size, in hundredths of its distance), tint (red, green, blue and a brightness) and opacity. A
// curve's keys are half-hour slots, with a straight line between them round the clock, as Guerrilla's
// time of day fills its slots (and its game_time_of_day_get_orbital_params reads them: the colour times
// its brightness, the opacity apart, the scale ÷ 100); a curve with no keys holds what Guerrilla's
// tod_orbital_object::init gives it (white at brightness 1, scale 10, opacity 1).
struct CurveKey {
    float slot = 0;
    float value[4] = {};
};

struct Orbital {
    std::string name, map;
    float turnX = 0, turnY = 0, offPlane = 0, hoursOn = 0;  // degrees, degrees, -1 to 1, hours
    bool blends = false;
    std::vector<CurveKey> scale, tint, opacity;
    ID3D11ShaderResourceView* texture = nullptr;  // loaded when first drawn
    bool textureTried = false;
};

constexpr size_t kMostOrbitals = 15;  // Guerrilla keeps no more (farray<tod_orbital_object, 15>)
std::string g_orbitalsWorld;          // the world they were read for
std::vector<Orbital> g_orbitals;

// The text between <tag> and </tag>, looked for from `from` and ending before `limit`.
bool Between(const std::string& text, const char* tag, size_t from, size_t limit, size_t& start, size_t& end) {
    const std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
    start = text.find(open, from);
    if (start == std::string::npos || start >= limit) return false;
    start += open.size();
    end = text.find(close, start);
    return end != std::string::npos && end <= limit;
}

std::vector<CurveKey> ReadKeys(const std::string& text, const char* tag, size_t from, size_t limit) {
    std::vector<CurveKey> keys;
    size_t start, end;
    if (!Between(text, tag, from, limit, start, end)) return keys;
    size_t at = start, keyStart, keyEnd;
    while (Between(text, "key", at, end, keyStart, keyEnd)) {
        CurveKey key;
        size_t s, e;
        if (Between(text, "time", keyStart, keyEnd, s, e)) key.slot = static_cast<float>(atof(text.substr(s, e - s).c_str()));
        if (Between(text, "value", keyStart, keyEnd, s, e))
            sscanf_s(text.substr(s, e - s).c_str(), "%f %f %f %f", &key.value[0], &key.value[1], &key.value[2], &key.value[3]);
        keys.push_back(key);
        at = keyEnd;
    }
    std::sort(keys.begin(), keys.end(), [](const CurveKey& a, const CurveKey& b) { return a.slot < b.slot; });
    return keys;
}

// A curve at a time (in hours): its first `count` numbers, `otherwise` when it has no keys.
void CurveAt(const std::vector<CurveKey>& keys, float hours, int count, float otherwise, float* out) {
    for (int i = 0; i < count; ++i) out[i] = otherwise;
    if (keys.empty()) return;
    float slot = fmodf(hours * 2, 48.0f);
    if (slot < 0) slot += 48;
    size_t next = 0;  // the first key after now; the one before it, round the clock
    while (next < keys.size() && keys[next].slot <= slot) ++next;
    const CurveKey& after = keys[next % keys.size()];
    const CurveKey& before = keys[(next + keys.size() - 1) % keys.size()];
    float from = before.slot, to = after.slot;
    if (from > slot) from -= 48;
    if (to <= slot) to += 48;
    const float t = to > from ? (slot - from) / (to - from) : 0;
    for (int i = 0; i < count; ++i) out[i] = before.value[i] + (after.value[i] - before.value[i]) * t;
}

void DropOrbitals() {
    for (Orbital& orbital : g_orbitals)
        if (orbital.texture) orbital.texture->Release();
    g_orbitals.clear();
}

float NumberBetween(const std::string& text, const char* tag, size_t from, size_t limit) {
    size_t s, e;
    return Between(text, tag, from, limit, s, e) ? static_cast<float>(atof(text.substr(s, e - s).c_str())) : 0.0f;
}

void ReadOrbitals() {
    const char* world = WorldName();
    if (g_orbitalsWorld == world) return;
    g_orbitalsWorld = world;
    DropOrbitals();
    const std::wstring name(world, world + strlen(world));
    std::vector<BYTE> bytes;
    if (!ReadWholeFile(rfa::RootDir() + L"\\overrides\\build\\pc\\cache\\" + name + L".gtodx", bytes)) {
        rfa::Log("Sun and moons: no overrides\\build\\pc\\cache\\%s.gtodx (the world builder writes it); none drawn", world);
        return;
    }
    const std::string text(bytes.begin(), bytes.end());
    size_t at = 0, start, end;
    while (g_orbitals.size() < kMostOrbitals && Between(text, "orbital", at, text.size(), start, end)) {
        Orbital orbital;
        size_t s, e;
        if (Between(text, "name", start, end, s, e)) orbital.name = text.substr(s, e - s);
        if (Between(text, "map_name", start, end, s, e)) orbital.map = text.substr(s, e - s);
        if (Between(text, "alpha_blend", start, end, s, e)) orbital.blends = text.compare(s, e - s, "true") == 0;
        orbital.turnX = NumberBetween(text, "plane_rot_x", start, end);
        orbital.turnY = NumberBetween(text, "plane_rot_y", start, end);
        orbital.offPlane = NumberBetween(text, "distance_off_plane", start, end);
        orbital.hoursOn = NumberBetween(text, "orbit_time_offset", start, end);
        orbital.scale = ReadKeys(text, "scale", start, end);
        orbital.tint = ReadKeys(text, "tint", start, end);
        orbital.opacity = ReadKeys(text, "opacity", start, end);
        rfa::Log("Sun and moons: %s from %s.gtodx: texture %s, turned %.2f° and %.2f°, %.2f h on%s; %zu scale, %zu tint and %zu opacity keys",
                 orbital.name.c_str(), world, orbital.map.c_str(), orbital.turnX, orbital.turnY, orbital.hoursOn, orbital.blends ? ", blended" : "",
                 orbital.scale.size(), orbital.tint.size(), orbital.opacity.size());
        g_orbitals.push_back(std::move(orbital));
        at = end;
    }
    if (g_orbitals.empty()) rfa::Log("Sun and moons: %s.gtodx has no orbitals; none drawn", world);
}

// Toward an orbital at a time (in hours), in the world, as Guerrilla's
// tod_shared_calculate_orbital_position works it out: a circle of radius sin(acos(d)), d off its
// middle, at ((hours + its offset) mod 24) ÷ 24 of a turn, turned about x, then about y (for the sun's
// numbers this is SunToward's path).
void OrbitalToward(const Orbital& orbital, float hours, float (&out)[3]) {
    const float pi = 3.14159265f;
    float t = hours + orbital.hoursOn;
    if (t < 0) t = 24 - t;  // (as Guerrilla's)
    const float a = fmodf(t, 24.0f) / 24.0f * 2 * pi;
    const float r = sinf(acosf(orbital.offPlane));
    float v[3] = {r * cosf(a), orbital.offPlane, r * sinf(a)};
    const float x = orbital.turnX * pi / 180, y = orbital.turnY * pi / 180;
    const float aboutX[3] = {v[0], v[1] * cosf(x) - v[2] * sinf(x), v[2] * cosf(x) + v[1] * sinf(x)};
    out[0] = aboutX[0] * cosf(y) + aboutX[2] * sinf(y);
    out[1] = aboutX[1];
    out[2] = aboutX[2] * cosf(y) - aboutX[0] * sinf(y);
}

struct SunConstants {
    float right[4];
    float up[4];
    float forward[4];
    float centre[4];
    float colour[4];
};

ID3D11VertexShader* g_sunVertex = nullptr;
ID3D11PixelShader* g_sunPixel = nullptr;
ID3D11Buffer* g_sunConstants = nullptr;
ID3D11BlendState* g_sunBlend = nullptr;    // added by its alpha
ID3D11BlendState* g_orbitalBlend = nullptr;  // blended by its alpha (alpha_blend)
ID3D11SamplerState* g_sunSampler = nullptr;
bool g_sunFailed = false;

// The orbitals' shaders and states (made once).
bool MakeSunObjects() {
    if (g_sunVertex) return true;
    {
        ID3DBlob* vertex = Compile(kSunShader, sizeof(kSunShader) - 1, "SunVertex", "vs_4_0");
        ID3DBlob* pixel = Compile(kSunShader, sizeof(kSunShader) - 1, "SunPixel", "ps_4_0");
        bool ok = vertex && pixel &&
                  SUCCEEDED(g_device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &g_sunVertex)) &&
                  SUCCEEDED(g_device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &g_sunPixel));
        for (ID3DBlob* blob : {vertex, pixel})
            if (blob) blob->Release();
        D3D11_BUFFER_DESC buffer = {};
        buffer.ByteWidth = sizeof(SunConstants);
        buffer.Usage = D3D11_USAGE_DYNAMIC;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ok = ok && SUCCEEDED(g_device->CreateBuffer(&buffer, nullptr, &g_sunConstants));
        D3D11_BLEND_DESC blend = {};  // on the sky's red, green and blue: added by the alpha, or blended by it
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
        blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        ok = ok && SUCCEEDED(g_device->CreateBlendState(&blend, &g_sunBlend));
        blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        ok = ok && SUCCEEDED(g_device->CreateBlendState(&blend, &g_orbitalBlend));
        D3D11_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        ok = ok && SUCCEEDED(g_device->CreateSamplerState(&sampler, &g_sunSampler));
        if (!ok) {
            rfa::Log("Could not make the sun's and moons' shaders or states; none drawn");
            return false;
        }
    }
    return true;
}

// An orbital's texture (mods\guerrilla_world\<map_name>.dds, which the world builder writes), loaded
// the first time it is drawn; without it, that orbital isn't drawn (logged once).
ID3D11ShaderResourceView* OrbitalTexture(Orbital& orbital) {
    if (!orbital.textureTried) {
        orbital.textureTried = true;
        const std::wstring map(orbital.map.begin(), orbital.map.end());
        orbital.texture = orbital.map.empty() ? nullptr : LoadTexture(DataFolder() + map + L".dds");
        if (!orbital.texture)
            rfa::Log("Sun and moons: no mods\\guerrilla_world\\%s.dds (the world builder writes it); %s not drawn", orbital.map.c_str(),
                     orbital.name.c_str());
    }
    return orbital.texture;
}

// Guerrilla's sun shaft settings (Re-Mars-tered's Rl_sun_shafts_*, their starting values): the sun's
// size in half-size texels, the strength (taken × 0.15625, and by the renderer's fade colour, 1 but in
// a screen fade), base and step, the fall-off's radius and curve, and the
// Gaussian's rho and multiplier.
constexpr float kShaftSunSize = 300, kShaftScale = 1, kShaftBaseLum = 1, kShaftLumStep = 1, kShaftRadius = 5, kShaftCurveBase = -4,
                kShaftCurveExponent = 1.6f, kShaftCurveShift = -0.02f, kShaftBlurRho = 0.75f, kShaftBlurMultiplier = 1;

struct ShaftConstants {
    float sun[4];
    float texel[4];
    float strength[4];
    float curve[4];
    float blur[4];
};

ID3D11VertexShader* g_shaftVertex = nullptr;
ID3D11PixelShader* g_shaftSource = nullptr;
ID3D11PixelShader* g_shaftBlur = nullptr;
ID3D11PixelShader* g_shaftRadial = nullptr;
ID3D11Buffer* g_shaftConstants = nullptr;
ID3D11Texture2D* g_shaft[2] = {};  // half the picture's size, 16-bit float: drawn into in turn
ID3D11RenderTargetView* g_shaftTarget[2] = {};
ID3D11ShaderResourceView* g_shaftView[2] = {};
UINT g_shaftWidth = 0, g_shaftHeight = 0;
// What the log's "Sun shafts at" line reads back (with Log=1, every 10 s; see WatchShafts): a texel of the
// blurred sky round the sun at the sun's place, and one of the shafts at the picture's middle.
ID3D11Texture2D* g_shaftReadback = nullptr;  // 2 × 1 texels, the half-size targets' format, for reading
bool g_shaftReadPending = false;
ULONGLONG g_lastShaftLog = 0;
struct ShaftLook {
    float hours;      // the time of day
    float sunHeight;  // degrees above the horizon (negative below it)
    float at[2];      // the sun's place on the picture (0 to 1 across and down)
    bool skyRead;     // whether that place is on the picture, so the sky round the sun was read
    bool depthKept;   // whether the shafts read the depth kept at the sky step (else the game's own at the frame's end)
};
ShaftLook g_shaftLook = {};
ID3D11ShaderResourceView* g_shaftDepth = nullptr;  // the G-buffer's depth, kept from the light pass for the shafts and the corona
// The depth the shafts read, at the tone mapping at the frame's end: a copy of the G-buffer's depth made at the sky step
// (see KeepShaftDepth), when the fog and the corona read it too. Read at the frame's end itself, it gave no pixel at the
// far plane: the sky round the sun came out black every time (the user's 2026-11-19 run).
ID3D11Texture2D* g_shaftDepthCopy = nullptr;
ID3D11ShaderResourceView* g_shaftDepthCopyView = nullptr;
bool g_shaftDepthKept = false;        // this frame's copy is made
bool g_shaftDepthCopyFailed = false;  // it can't be made: the shafts read the game's own depth
bool CoronaWanted();  // (the sun's corona, below)
float g_farDepth = 1;                              // the depth buffer's value at the far plane (from the sky step)

bool MakeShaftObjects() {
    if (!LoadCompiler()) return false;
    ID3DBlob* vertex = Compile(kShaftShader, sizeof(kShaftShader) - 1, "ShaftVertex", "vs_4_0");
    ID3DBlob* source = Compile(kShaftShader, sizeof(kShaftShader) - 1, "SourcePixel", "ps_4_0");
    ID3DBlob* blur = Compile(kShaftShader, sizeof(kShaftShader) - 1, "BlurPixel", "ps_4_0");
    ID3DBlob* radial = Compile(kShaftShader, sizeof(kShaftShader) - 1, "RadialPixel", "ps_4_0");
    bool ok = vertex && source && blur && radial &&
              SUCCEEDED(g_device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &g_shaftVertex)) &&
              SUCCEEDED(g_device->CreatePixelShader(source->GetBufferPointer(), source->GetBufferSize(), nullptr, &g_shaftSource)) &&
              SUCCEEDED(g_device->CreatePixelShader(blur->GetBufferPointer(), blur->GetBufferSize(), nullptr, &g_shaftBlur)) &&
              SUCCEEDED(g_device->CreatePixelShader(radial->GetBufferPointer(), radial->GetBufferSize(), nullptr, &g_shaftRadial));
    for (ID3DBlob* blob : {vertex, source, blur, radial})
        if (blob) blob->Release();
    D3D11_BUFFER_DESC buffer = {};
    buffer.ByteWidth = sizeof(ShaftConstants);
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(g_device->CreateBuffer(&buffer, nullptr, &g_shaftConstants));
    if (!ok) rfa::Log("Could not make the sun shafts' shaders; no sun shafts");
    return ok;
}

// The two half-size targets, made again when the picture's size changes.
bool ShaftTargets(UINT width, UINT height) {
    if (g_shaft[0] && g_shaftWidth == width && g_shaftHeight == height) return true;
    for (int i = 0; i < 2; ++i) {
        if (g_shaftView[i]) g_shaftView[i]->Release();
        if (g_shaftTarget[i]) g_shaftTarget[i]->Release();
        if (g_shaft[i]) g_shaft[i]->Release();
        g_shaftView[i] = nullptr;
        g_shaftTarget[i] = nullptr;
        g_shaft[i] = nullptr;
    }
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    bool ok = true;
    for (int i = 0; i < 2 && ok; ++i)
        ok = SUCCEEDED(g_device->CreateTexture2D(&desc, nullptr, &g_shaft[i])) &&
             SUCCEEDED(g_device->CreateRenderTargetView(g_shaft[i], nullptr, &g_shaftTarget[i])) &&
             SUCCEEDED(g_device->CreateShaderResourceView(g_shaft[i], nullptr, &g_shaftView[i]));
    g_shaftWidth = ok ? width : 0;
    g_shaftHeight = ok ? height : 0;
    if (ok) rfa::Log("Sun shafts: drawn at half the picture's size, %u x %u", width, height);
    return ok;
}

// Loads the cloud model and textures (mods\guerrilla_world\) and makes what the clouds draw with.
// Without them, no clouds (logged).
void MakeCloudObjects() {
    const std::wstring folder = DataFolder();
    std::vector<BYTE> file;
    if (!ReadWholeFile(folder + L"clouds.bin", file) || file.size() < 12 || memcmp(file.data(), "GWCLOUD1", 8) != 0) {
        rfa::Log("Clouds: no mods\\guerrilla_world\\clouds.bin (made by the world builder from Guerrilla's files); the sky is drawn without clouds");
        return;
    }
    // Vertices: position, normal, texture coordinates (8 floats).
    size_t at = 8;
    const UINT vertexCount = ReadAt<UINT>(file, at);
    at += 4;
    const size_t vertexBytes = size_t(vertexCount) * 32;
    if (at + vertexBytes + 4 > file.size()) { rfa::Log("Clouds: clouds.bin is cut short"); return; }
    const BYTE* vertices = file.data() + at;
    at += vertexBytes;
    const UINT partCount = ReadAt<UINT>(file, at);
    at += 4;
    std::vector<CloudPart> parts;
    for (UINT i = 0; i < partCount; ++i) {
        if (at + 4 * 9 + 128 > file.size()) { rfa::Log("Clouds: clouds.bin is cut short"); return; }
        CloudPart part;
        part.first = ReadAt<UINT>(file, at);
        part.count = ReadAt<UINT>(file, at + 4);
        const int kind = ReadAt<int>(file, at + 8);
        part.kind = kind >= 0 && kind <= 2 ? kind : 1;
        part.fade = ReadAt<float>(file, at + 12);
        part.full = ReadAt<float>(file, at + 16);
        at += 20 + 16;  // and the model's own layer strengths, which the time of day's replace
        for (int t = 0; t < 2; ++t) {
            char name[65] = {};
            memcpy(name, file.data() + at, 64);
            at += 64;
            if (!name[0]) continue;  // the backdrop's second
            std::wstring path = folder;
            for (const char* c = name; *c; ++c) path += static_cast<wchar_t>(*c);
            part.layers[t] = LoadTexture(path + L".dds");
            if (!part.layers[t]) rfa::Log("Clouds: could not load %s.dds", name);
        }
        if (part.layers[0] && (part.layers[1] || part.kind == 2)) parts.push_back(part);
    }
    if (at + 4 > file.size()) { rfa::Log("Clouds: clouds.bin is cut short"); return; }
    const UINT indexCount = ReadAt<UINT>(file, at);
    at += 4;
    if (at + size_t(indexCount) * 4 > file.size() || parts.empty()) { rfa::Log("Clouds: clouds.bin has no usable parts"); return; }
    // Each part's box (for its fog), from the points its triangles use.
    for (CloudPart& part : parts) {
        float low[3] = {1e30f, 1e30f, 1e30f}, high[3] = {-1e30f, -1e30f, -1e30f};
        for (UINT i = part.first; i < part.first + part.count && i < indexCount; ++i) {
            const UINT index = ReadAt<UINT>(file, at + size_t(i) * 4);
            if (index >= vertexCount) continue;
            const float* point = reinterpret_cast<const float*>(vertices + size_t(index) * 32);
            for (int a = 0; a < 3; ++a) {
                if (point[a] < low[a]) low[a] = point[a];
                if (point[a] > high[a]) high[a] = point[a];
            }
        }
        for (int a = 0; a < 3; ++a) part.half[a] = high[a] > low[a] ? (high[a] - low[a]) / 2 : 1;
    }

    ID3DBlob* vertex = Compile(kCloudShader, sizeof(kCloudShader) - 1, "CloudVertex", "vs_4_0");
    ID3DBlob* pixel = Compile(kCloudShader, sizeof(kCloudShader) - 1, "CloudPixel", "ps_4_0");
    ID3DBlob* backdrop = Compile(kCloudShader, sizeof(kCloudShader) - 1, "BackdropPixel", "ps_4_0");
    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    bool ok = vertex && pixel && backdrop &&
              SUCCEEDED(g_device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &g_cloudVertex)) &&
              SUCCEEDED(g_device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &g_cloudPixel)) &&
              SUCCEEDED(g_device->CreatePixelShader(backdrop->GetBufferPointer(), backdrop->GetBufferSize(), nullptr, &g_backdropPixel)) &&
              SUCCEEDED(g_device->CreateInputLayout(layout, 3, vertex->GetBufferPointer(), vertex->GetBufferSize(), &g_cloudLayout));
    if (vertex) vertex->Release();
    if (pixel) pixel->Release();
    if (backdrop) backdrop->Release();

    D3D11_BUFFER_DESC buffer = {};
    D3D11_SUBRESOURCE_DATA data = {};
    buffer.Usage = D3D11_USAGE_IMMUTABLE;
    buffer.ByteWidth = static_cast<UINT>(vertexBytes);
    buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    data.pSysMem = vertices;
    ok = ok && SUCCEEDED(g_device->CreateBuffer(&buffer, &data, &g_cloudVertices));
    buffer.ByteWidth = indexCount * 4;
    buffer.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = file.data() + at;
    ok = ok && SUCCEEDED(g_device->CreateBuffer(&buffer, &data, &g_cloudIndices));
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.ByteWidth = sizeof(CloudConstants);
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(g_device->CreateBuffer(&buffer, nullptr, &g_cloudConstants));

    D3D11_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(g_device->CreateSamplerState(&sampler, &g_cloudSampler));

    // Laid over the sky: the colour comes already times its cover.
    D3D11_BLEND_DESC blend = {};
    auto& over = blend.RenderTarget[0];
    over.BlendEnable = TRUE;
    over.SrcBlend = over.SrcBlendAlpha = D3D11_BLEND_ONE;
    over.DestBlend = over.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    over.BlendOp = over.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    over.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(g_device->CreateBlendState(&blend, &g_cloudBlend));

    // Clipped as usual (so what is behind the eye is cut away), just in front of the far plane.
    D3D11_RASTERIZER_DESC raster = {};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(g_device->CreateRasterizerState(&raster, &g_cloudRaster));
    if (!ok) {
        rfa::Log("Clouds: could not make their shaders, buffers or states; the sky is drawn without clouds");
        for (CloudPart& part : parts)
            for (auto* layer : part.layers) if (layer) layer->Release();
        return;
    }
    g_cloudParts = parts;
    rfa::Log("Clouds: %u parts, %u triangles, from mods\\guerrilla_world", static_cast<unsigned>(parts.size()), indexCount / 3);
}

void SkyColours(float (&hemisphere)[10][4]) {
    float tint[3];
    PartTint(tint);
    for (int c = 0; c < 10; ++c) {
        float colour[3];
        TodColour(kSkyCurves[c], colour);
        for (int i = 0; i < 3; ++i) hemisphere[c][i] = colour[i] * SkyBrightness() * tint[i];
        hemisphere[c][3] = 1;
    }
}

// The clouds' drift, as Guerrilla's game_time_of_day_do_frame moves them: layers 0-1's and 2-3's
// offsets grow by their speeds times the real hours gone times Cloud_scroll_separate_clock_scale_factor
// (150: a texture width in 24 seconds at speed 1), taken back by 10 when they reach 10.
constexpr float kCloudClockScale = 150.0f;
void Drift() {
    const ULONGLONG now = GetTickCount64();
    const float seconds = g_lastDrift ? (now - g_lastDrift) / 1000.0f : 0.0f;
    g_lastDrift = now;
    if (seconds > 1) return;  // paused, loading
    const float hours = seconds / 3600.0f * kCloudClockScale;
    g_drift[0] += TodNumber(kLayer01Speed) * hours;
    g_drift[1] += TodNumber(kLayer23Speed) * hours;
    for (float& drift : g_drift)
        if (drift >= 10) drift -= 10;
}

void DrawClouds(const SkyConstants& sky) {
    if (g_cloudParts.empty()) return;
    Drift();
    CloudConstants constants = {};
    memcpy(constants.right, sky.right, sizeof(constants.right));
    memcpy(constants.up, sky.up, sizeof(constants.up));
    memcpy(constants.forward, sky.forward, sizeof(constants.forward));
    if (g_lightOn) {
        float toward[3];  // lit as Guerrilla's clouds are (game_time_of_day_get_cloud_lighting: its light direction, at least 15° up)
        LightToward(g_timeOfDay, kCloudLightLeastDegrees, toward);
        for (int i = 0; i < 3; ++i) constants.light[i] = toward[i];
    } else {
        float length = sqrtf(g_lightDirection[0] * g_lightDirection[0] + g_lightDirection[1] * g_lightDirection[1] + g_lightDirection[2] * g_lightDirection[2]);
        for (int i = 0; i < 3; ++i) constants.light[i] = length > 0 ? g_lightDirection[i] / length : (i == 1 ? 1.0f : 0.0f);
    }
    constants.shape[2] = TodNumber(kBacklightStrength);
    constants.shape[3] = TodNumber(kBacklightPower);
    constants.drift[0] = g_drift[0];
    constants.drift[1] = g_drift[1];
    // No ambient light: Guerrilla draws its sky with the renderer's ambient set to RL_skybox_ambient
    // (rl_skybox_renderable_instance::render), which nothing ever sets, so its clouds' ambient term is
    // black (with the world's added, ours came out up to 100 times too bright: grey-blue sheets where
    // Guerrilla's are dark streaks). The fog's colour and numbers as the sky's (its own scale and offset each part).
    for (float& a : constants.ambient) a = 0;
    memcpy(constants.fogColour, sky.fogColour, sizeof(constants.fogColour));
    memcpy(constants.fogNumbers, sky.fogNumbers, sizeof(constants.fogNumbers));

    UINT stride = 32, offset = 0;
    g_context->IASetInputLayout(g_cloudLayout);
    g_context->IASetVertexBuffers(0, 1, &g_cloudVertices, &stride, &offset);
    g_context->IASetIndexBuffer(g_cloudIndices, DXGI_FORMAT_R32_UINT, 0);
    g_context->VSSetShader(g_cloudVertex, nullptr, 0);
    g_context->VSSetConstantBuffers(0, 1, &g_cloudConstants);
    g_context->PSSetConstantBuffers(0, 1, &g_cloudConstants);
    g_context->PSSetSamplers(0, 1, &g_cloudSampler);
    const float noFactor[4] = {1, 1, 1, 1};
    g_context->OMSetBlendState(g_cloudBlend, noFactor, 0xFFFFFFFF);
    g_context->RSSetState(g_cloudRaster);
    // In the model's order: the overhead clouds, the ones along the horizon, then the backdrop in front of them.
    float tint[3];
    PartTint(tint);
    for (const CloudPart& part : g_cloudParts) {
        float colour[3];
        // Each part's fog by its own curves, as Guerrilla's object_skybox_update_render_all gives them:
        // the overhead clouds the sky's (fog_skybox_), the ones along the horizon the clouds'
        // (fog_clouds_), the backdrop the matte's (fog_matte_).
        const size_t kFogScales[3] = {kSkyFogScale, kCloudsFogScale, kMatteFogScale};
        const size_t kFogOffsets[3] = {kSkyFogOffset, kCloudsFogOffset, kMatteFogOffset};
        const float fogScale = TodNumber(kFogScales[part.kind]);
        const float fogOffset = TodNumber(kFogOffsets[part.kind]);
        constants.drift[2] = part.kind == 0 ? 1.0f : 0.0f;
        constants.fogScale[0] = kSkyFogFar / part.half[0];
        constants.fogScale[1] = fogScale / part.half[1];
        constants.fogScale[2] = kSkyFogFar / part.half[2];
        constants.fogScale[3] = part.half[1];
        constants.fogRaise[0] = fogOffset - g_cameraPosition[1];
        if (part.kind == 2) {
            TodColour(kMountainFront, colour);
            for (int i = 0; i < 3; ++i) constants.front[i] = colour[i] * SkyBrightness() * tint[i];
            TodColour(kMountainBack, colour);
            for (int i = 0; i < 3; ++i) constants.rear[i] = colour[i] * SkyBrightness() * tint[i];
            TodColour(kMountainFog, colour);  // mixed into the picture before it is lit
            for (int i = 0; i < 3; ++i) constants.strengths[i] = colour[i];
            constants.strengths[3] = TodNumber(kMountainFogDensity);
            if (g_tellColours)
                rfa::Log("Backdrop: lit side %.3f %.3f %.3f, far side %.3f %.3f %.3f, haze %.3f %.3f %.3f at %.2f", constants.front[0],
                         constants.front[1], constants.front[2], constants.rear[0], constants.rear[1], constants.rear[2],
                         constants.strengths[0], constants.strengths[1], constants.strengths[2], constants.strengths[3]);
        } else {
            TodColour(kCloudFront[part.kind], colour);
            for (int i = 0; i < 3; ++i) constants.front[i] = colour[i] * SkyBrightness() * tint[i];
            TodColour(kCloudRear[part.kind], colour);
            for (int i = 0; i < 3; ++i) constants.rear[i] = colour[i] * SkyBrightness() * tint[i];
            for (int i = 0; i < 4; ++i) constants.strengths[i] = TodNumber(kCloudLayers[part.kind][i]);
            constants.shape[0] = part.fade;
            constants.shape[1] = part.full > part.fade ? part.full : part.fade + 0.01f;
            if (g_tellColours)
                rfa::Log("Clouds (%s): front %.3f %.3f %.3f, rear %.3f %.3f %.3f, layers %.2f %.2f %.2f %.2f, rim %.2f ^%.2f, light from %.2f %.2f %.2f",
                         part.kind == 0 ? "overhead" : "horizon", constants.front[0], constants.front[1], constants.front[2], constants.rear[0],
                         constants.rear[1], constants.rear[2], constants.strengths[0], constants.strengths[1], constants.strengths[2],
                         constants.strengths[3], constants.shape[2], constants.shape[3], constants.light[0], constants.light[1], constants.light[2]);
        }
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(g_context->Map(g_cloudConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) continue;
        memcpy(mapped.pData, &constants, sizeof(constants));
        g_context->Unmap(g_cloudConstants, 0);
        g_context->PSSetShader(part.kind == 2 ? g_backdropPixel : g_cloudPixel, nullptr, 0);
        g_context->PSSetShaderResources(0, 2, part.layers);
        g_context->DrawIndexed(part.count, part.first, 0);
    }
}

// The sun and moons over the sky, before the clouds, in the order the time of day lists them (as
// Guerrilla's render_orbital_objects): each toward where its path has it now, its scale, colour and
// opacity from its curves now, the colour (times its brightness and Guerrilla's light scale, 1) times
// the colour correction and the sky's brightness.
void DrawOrbitals(const SkyConstants& sky) {
    ReadOrbitals();
    if (g_orbitals.empty() || g_sunFailed) return;
    if (!MakeSunObjects()) {
        g_sunFailed = true;
        return;
    }
    float correction[3];
    PartTint(correction);
    bool drawing = false;
    for (Orbital& orbital : g_orbitals) {
        ID3D11ShaderResourceView* texture = OrbitalTexture(orbital);
        if (!texture) continue;
        float scale, opacity, tint[4], toward[3];
        CurveAt(orbital.scale, g_timeOfDay, 1, 10, &scale);
        CurveAt(orbital.opacity, g_timeOfDay, 1, 1, &opacity);
        CurveAt(orbital.tint, g_timeOfDay, 4, 1, tint);
        if (opacity <= 0 || scale <= 0) continue;
        OrbitalToward(orbital, g_timeOfDay, toward);
        SunConstants constants = {};
        memcpy(constants.right, sky.right, sizeof(constants.right));
        memcpy(constants.up, sky.up, sizeof(constants.up));
        memcpy(constants.forward, sky.forward, sizeof(constants.forward));
        for (int i = 0; i < 3; ++i) {
            constants.centre[i] = toward[i];
            constants.colour[i] = tint[i] * tint[3] * correction[i] * SkyBrightness();
        }
        constants.colour[3] = opacity;
        constants.centre[3] = scale / 100;
        if (g_tellColours)
            rfa::Log("%s at %.2f h: toward %.2f %.2f %.2f, %.4f across, colour %.3f %.3f %.3f, opacity %.3f", orbital.name.c_str(), g_timeOfDay,
                     toward[0], toward[1], toward[2], 2 * constants.centre[3], constants.colour[0], constants.colour[1], constants.colour[2], opacity);
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(g_context->Map(g_sunConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) break;
        memcpy(mapped.pData, &constants, sizeof(constants));
        g_context->Unmap(g_sunConstants, 0);
        if (!drawing) {
            drawing = true;
            g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            g_context->IASetInputLayout(nullptr);
            g_context->VSSetShader(g_sunVertex, nullptr, 0);
            g_context->PSSetShader(g_sunPixel, nullptr, 0);
            g_context->VSSetConstantBuffers(0, 1, &g_sunConstants);
            g_context->PSSetConstantBuffers(0, 1, &g_sunConstants);
            g_context->PSSetSamplers(0, 1, &g_sunSampler);
        }
        g_context->PSSetShaderResources(0, 1, &texture);
        const float noFactor[4] = {1, 1, 1, 1};
        g_context->OMSetBlendState(orbital.blends ? g_orbitalBlend : g_sunBlend, noFactor, 0xFFFFFFFF);
        g_context->Draw(4, 0);
    }
    if (drawing) g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

// ---- The night sky's stars and meteors
// Guerrilla's (rl_star_field, rl_meteor_shower), drawn after the sun and moons, before the clouds, as
// its skybox does. Both fade in and out with the hour as its object_skybox_update_render_all sets
// their strength: none from 07:00 to 17:00, rising to full by 22:00, full until 03:00, gone by 07:00.
float Seconds();
float NightSkyStrength(float hours) {
    if (hours > 22) return 1;
    if (hours > 17) return (hours - 17) / 5;
    if (hours < 3) return 1;
    if (hours < 7) return (7 - hours) / 4;
    return 0;
}

// How far away Guerrilla draws them: its camera's far clip (475 m, camera_system_init); the stars one
// metre nearer (the sky's Draw_distance).
constexpr float kNightSkyFar = 475.0f;

// The stars: Guerrilla's star catalogue (5,456 real stars, brightest first: right ascension and
// declination in radians, and a brightness), which the world builder copies from Guerrilla's exe into
// mods\guerrilla_world\stars.bin. Of them Guerrilla makes 1,000 small squares
// (rl_star_field::generate_billboard_stars): going down the catalogue, each star tilted 15° (its
// declination less π/12) and kept if then more than 4° above the horizon, its brightness a quarter of
// the catalogue's but no less than 0.0015, and a twinkle phase from the C runtime's rand() (from its
// first seed, as the stars are made before the game seeds it: rand() % 1001 × 0.001, one for each
// star looked at, kept or not); each number stored as a 16-bit whole number of 4096ths.
// (Re-Mars-tered draws only the first 555 of the 1,000, a slip in its geometry's vertex count; the
// stars it leaves out are all at the faintest brightness. Guerrilla also has a second kind, single
// points for the catalogue's fainter stars, which it never draws.)
constexpr int kStarCount = 1000;
constexpr char kStarFileMagic[8] = {'G', 'W', 'S', 'T', 'A', 'R', 'S', '1'};

// As Guerrilla's rfg-skybox-stars (its billboard technique): each star a square 0.9 m across facing
// the camera, kNightSkyFar − 1 away along its direction (heading h, pitch p: sin h cos p, −sin p,
// cos p cos h), at the far plane; its colour the sun's texture (vfx_sun_01) without its alpha, its
// alpha its brightness × the strength × a twinkle × 12, added to the sky by that alpha. The twinkle:
// 1 − 0.2 sin(4π r), only while sin(6π r) ≤ 0.5, r the star's phase plus the seconds gone.
constexpr char kStarShader[] = R"(
cbuffer Stars : register(b0)
{
    float4 Right;    // as the sky's
    float4 Up;
    float4 Forward;
    float4 Clock;    // x the seconds gone, y the strength, z how far away the stars are
    float4 Colour;   // the colour correction times the sky's brightness
};

cbuffer Catalogue : register(b1)
{
    float4 Star[1000];  // pitch, heading, brightness, twinkle phase
};

Texture2D Picture : register(t0);
SamplerState Clamped : register(s0);

struct StarPoint
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float alpha : TEXCOORD1;
};

static const float2 kCorners[6] = {float2(0, 0), float2(1, 0), float2(0, 1), float2(0, 1), float2(1, 0), float2(1, 1)};

StarPoint StarVertex(uint id : SV_VertexID)
{
    float4 star = Star[id / 6];
    float2 uv = kCorners[id % 6];
    float2 off = (uv - 0.5) * 0.9;
    float3 toward = float3(sin(star.y) * cos(star.x), -sin(star.x), cos(star.x) * cos(star.y));
    float3 look = toward * Clock.z + Right.xyz * off.x + Up.xyz * off.y;
    float depth = dot(look, Forward.xyz);
    StarPoint p;
    p.position = float4(dot(look, Right.xyz) / Right.w, dot(look, Up.xyz) / Up.w, depth * lerp(0.000001, 0.999999, Forward.w), depth);
    p.uv = uv;
    float r = star.w + Clock.x;
    float twinkle = 1 - 0.2 * sin(12.56636 * r) * (sin(18.849541 * r) <= 0.5 ? 1 : 0);
    p.alpha = star.z * Clock.y * twinkle * 12;
    return p;
}

float4 StarPixel(StarPoint p) : SV_Target
{
    return float4(Picture.Sample(Clamped, p.uv).rgb * Colour.rgb, p.alpha);
}
)";

// The meteors, as Guerrilla's rl_meteor_shower: up to 16 at once, a new one every 2 to 12 seconds
// while the strength isn't 0. Each starts 200 m up, a quarter of kNightSkyFar out from the camera in a
// random direction, and flies level toward a point kNightSkyFar out on the far side (straight across,
// give or take 30°) at 100 to 200 m/s for 2.5 to 5 seconds. Each is drawn as a line 20 m long ahead of
// where it is, at the far plane, its alpha from 0 at the tail to its head's: its fade (rising over the
// first half of its life, falling over the second) × (a flicker, max(0, ½(sin 23π t + sin 13π t)), and
// 0.2 × the strength); its colour white, added by that alpha.
constexpr int kMostMeteors = 16;
constexpr float kMeteorDelay = 2, kMeteorDelayRandom = 10, kMeteorLifespan = 2.5f, kMeteorLifespanRandom = 2.5f, kMeteorSpeed = 100,
                kMeteorSpeedRandom = 100, kMeteorHeight = 200, kMeteorLength = 20;

constexpr char kMeteorShader[] = R"(
cbuffer Meteors : register(b0)
{
    float4 Right;    // as the sky's
    float4 Up;
    float4 Forward;
    float4 Clock;    // y the strength
    float4 Colour;   // the colour correction times the sky's brightness
    float4 Meteor[32];  // two each: where it started, from the camera (xyz), and its age in seconds (w); its velocity (xyz) and lifespan (w)
};

struct MeteorPoint
{
    float4 position : SV_Position;
    float alpha : TEXCOORD0;
};

MeteorPoint MeteorVertex(uint id : SV_VertexID)
{
    float4 start = Meteor[(id >> 1) * 2], motion = Meteor[(id >> 1) * 2 + 1];
    float end = id & 1;
    float age = start.w;
    float3 look = start.xyz + motion.xyz * age + normalize(motion.xyz) * end * 20;
    float depth = dot(look, Forward.xyz);
    MeteorPoint p;
    p.position = float4(dot(look, Right.xyz) / Right.w, dot(look, Up.xyz) / Up.w, depth * lerp(0.000001, 0.999999, Forward.w), depth);
    float life = saturate(age / motion.w);
    float flicker = max(0, 0.5 * (sin(72.256569 * age) + sin(40.840672 * age)));
    float fade = life < 0.5 ? 2 * life : 2 - 2 * life;
    p.alpha = end * fade * (saturate(flicker) + 0.2 * Clock.y);
    return p;
}

float4 MeteorPixel(MeteorPoint p) : SV_Target
{
    return float4(Colour.rgb, p.alpha);
}
)";

struct NightSkyConstants {
    float right[4];
    float up[4];
    float forward[4];
    float clock[4];
    float colour[4];
};

struct MeteorConstants {
    NightSkyConstants sky;
    float meteor[kMostMeteors * 2][4];
};

ID3D11VertexShader* g_starVertex = nullptr;
ID3D11PixelShader* g_starPixel = nullptr;
ID3D11VertexShader* g_meteorVertex = nullptr;
ID3D11PixelShader* g_meteorPixel = nullptr;
ID3D11Buffer* g_starConstants = nullptr;
ID3D11Buffer* g_starCatalogue = nullptr;  // the 1,000 stars, made once
ID3D11Buffer* g_meteorConstants = nullptr;
ID3D11ShaderResourceView* g_starPicture = nullptr;
int g_starsDrawn = 0;
bool g_nightSkyTried = false, g_nightSkyFailed = false;

// The MSVC C runtime's rand(), from its first seed (1), as Guerrilla's stars were made with it.
struct RuntimeRand {
    unsigned state = 1;
    int Next() {
        state = state * 214013u + 2531011u;
        return (state >> 16) & 0x7FFF;
    }
};

// A value as Guerrilla's star vertices hold it: a 16-bit whole number of 4096ths, cut toward 0.
float InFourThousandNinetySixths(float value) { return static_cast<float>(static_cast<short>(static_cast<int>(value * 4096.0))) / 4096.0f; }

// The 1,000 stars from mods\guerrilla_world\stars.bin, as Guerrilla makes them (see kStarCount); how
// many, or 0 without the file (logged).
int MakeStars(float (&stars)[kStarCount][4]) {
    std::vector<BYTE> bytes;
    if (!ReadWholeFile(DataFolder() + L"stars.bin", bytes) || bytes.size() < 12 || memcmp(bytes.data(), kStarFileMagic, 8) != 0) {
        rfa::Log("Stars: no mods\\guerrilla_world\\stars.bin (the world builder writes it from Guerrilla's exe); no stars");
        return 0;
    }
    int count = 0;
    memcpy(&count, bytes.data() + 8, 4);
    if (count < 0 || bytes.size() < 12 + static_cast<size_t>(count) * 12) {
        rfa::Log("Stars: mods\\guerrilla_world\\stars.bin is cut short; no stars");
        return 0;
    }
    const float tilt = 0.2617993950843811f;            // π/12, as Guerrilla's float
    const double lowest = -0.069813170079773182;        // −4°
    RuntimeRand rand;
    int made = 0;
    for (int i = 0; i < count && made < kStarCount; ++i) {
        float entry[3];  // right ascension, declination, brightness
        memcpy(entry, bytes.data() + 12 + i * 12, sizeof(entry));
        const float pitch = entry[1] - tilt;
        float brightness = entry[2] * 0.25f;
        if (brightness < 0.0015f) brightness = 0.0015f;
        const float phase = static_cast<float>(rand.Next() % 1001) * 0.001f;
        if (!(pitch < lowest)) continue;
        stars[made][0] = InFourThousandNinetySixths(pitch);
        stars[made][1] = InFourThousandNinetySixths(entry[0]);
        stars[made][2] = InFourThousandNinetySixths(brightness);
        stars[made][3] = InFourThousandNinetySixths(phase);
        ++made;
    }
    rfa::Log("Stars: %d of Guerrilla's star catalogue's %d stars, as its star field makes them", made, count);
    return made;
}

ID3D11Buffer* MakeConstantBuffer(UINT size, const void* data) {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = (size + 15) & ~15u;
    desc.Usage = data ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = data ? 0 : D3D11_CPU_ACCESS_WRITE;
    D3D11_SUBRESOURCE_DATA initial = {data, 0, 0};
    ID3D11Buffer* buffer = nullptr;
    return SUCCEEDED(g_device->CreateBuffer(&desc, data ? &initial : nullptr, &buffer)) ? buffer : nullptr;
}

// The stars' and meteors' shaders, buffers and the stars themselves (made once); false if they can't
// be drawn at all.
bool MakeNightSkyObjects() {
    if (g_nightSkyTried) return !g_nightSkyFailed;
    g_nightSkyTried = true;
    g_nightSkyFailed = true;
    if (!MakeSunObjects()) return false;  // (its blend state and sampler)
    ID3DBlob* blobs[4] = {Compile(kStarShader, sizeof(kStarShader) - 1, "StarVertex", "vs_4_0"),
                          Compile(kStarShader, sizeof(kStarShader) - 1, "StarPixel", "ps_4_0"),
                          Compile(kMeteorShader, sizeof(kMeteorShader) - 1, "MeteorVertex", "vs_4_0"),
                          Compile(kMeteorShader, sizeof(kMeteorShader) - 1, "MeteorPixel", "ps_4_0")};
    bool ok = blobs[0] && blobs[1] && blobs[2] && blobs[3] &&
              SUCCEEDED(g_device->CreateVertexShader(blobs[0]->GetBufferPointer(), blobs[0]->GetBufferSize(), nullptr, &g_starVertex)) &&
              SUCCEEDED(g_device->CreatePixelShader(blobs[1]->GetBufferPointer(), blobs[1]->GetBufferSize(), nullptr, &g_starPixel)) &&
              SUCCEEDED(g_device->CreateVertexShader(blobs[2]->GetBufferPointer(), blobs[2]->GetBufferSize(), nullptr, &g_meteorVertex)) &&
              SUCCEEDED(g_device->CreatePixelShader(blobs[3]->GetBufferPointer(), blobs[3]->GetBufferSize(), nullptr, &g_meteorPixel));
    for (ID3DBlob* blob : blobs)
        if (blob) blob->Release();
    ok = ok && (g_starConstants = MakeConstantBuffer(sizeof(NightSkyConstants), nullptr)) && (g_meteorConstants = MakeConstantBuffer(sizeof(MeteorConstants), nullptr));
    if (!ok) {
        rfa::Log("Could not make the stars' and meteors' shaders or buffers; none drawn");
        return false;
    }
    static float stars[kStarCount][4] = {};
    g_starsDrawn = MakeStars(stars);
    if (g_starsDrawn > 0) {
        g_starCatalogue = MakeConstantBuffer(sizeof(stars), stars);
        g_starPicture = LoadTexture(DataFolder() + L"vfx_sun_01.dds");
        if (!g_starCatalogue || !g_starPicture) {
            rfa::Log("Stars: %s; no stars", g_starPicture ? "could not make their buffer" : "no mods\\guerrilla_world\\vfx_sun_01.dds (the world builder writes it)");
            g_starsDrawn = 0;
        }
    }
    g_nightSkyFailed = false;
    return true;
}

void FillNightSkyConstants(const SkyConstants& sky, float strength, NightSkyConstants& out) {
    memcpy(out.right, sky.right, sizeof(out.right));
    memcpy(out.up, sky.up, sizeof(out.up));
    memcpy(out.forward, sky.forward, sizeof(out.forward));
    out.clock[0] = Seconds();
    out.clock[1] = strength;
    out.clock[2] = kNightSkyFar - 1;
    float correction[3];
    PartTint(correction);
    for (int i = 0; i < 3; ++i) out.colour[i] = correction[i] * SkyBrightness();
    out.colour[3] = 1;
}

void DrawStars(const SkyConstants& sky, float strength) {
    if (g_starsDrawn <= 0) return;
    NightSkyConstants constants = {};
    FillNightSkyConstants(sky, strength, constants);
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(g_context->Map(g_starConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    memcpy(mapped.pData, &constants, sizeof(constants));
    g_context->Unmap(g_starConstants, 0);
    ID3D11Buffer* buffers[2] = {g_starConstants, g_starCatalogue};
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->IASetInputLayout(nullptr);
    g_context->VSSetShader(g_starVertex, nullptr, 0);
    g_context->PSSetShader(g_starPixel, nullptr, 0);
    g_context->VSSetConstantBuffers(0, 2, buffers);
    g_context->PSSetConstantBuffers(0, 1, &g_starConstants);
    g_context->PSSetShaderResources(0, 1, &g_starPicture);
    g_context->PSSetSamplers(0, 1, &g_sunSampler);
    const float noFactor[4] = {1, 1, 1, 1};
    g_context->OMSetBlendState(g_sunBlend, noFactor, 0xFFFFFFFF);
    g_context->Draw(g_starsDrawn * 6, 0);
}

// The meteors as Guerrilla's rl_meteor_shower::update moves them (only while the strength isn't 0):
// those past their lifespan freed; its emission clock run on by the time since it last ran (no more
// than half a second), a meteor made each time it passes the next delay (if one is free), and the
// clock taken back by that delay.
struct Meteor {
    float at[3], velocity[3];  // from the camera, metres; metres a second
    float born = 0, lifespan = -1;  // seconds; less than 0 while free
};
Meteor g_meteors[kMostMeteors];
float g_meteorsUpdated = 0, g_meteorEmission = 0, g_nextMeteor = kMeteorDelay;
unsigned g_meteorRandom = 0x9E3779B9u;

float MeteorRandom() {  // 0 to 1 (Guerrilla's fl_rand; any even spread does)
    g_meteorRandom ^= g_meteorRandom << 13;
    g_meteorRandom ^= g_meteorRandom >> 17;
    g_meteorRandom ^= g_meteorRandom << 5;
    return (g_meteorRandom >> 8) / 16777216.0f;
}

void UpdateMeteors(float now) {
    float elapsed = now - g_meteorsUpdated;
    if (elapsed >= 0.5f) elapsed = 0.5f;
    g_meteorsUpdated = now;
    for (Meteor& meteor : g_meteors)
        if (meteor.lifespan > 0 && now - meteor.born > meteor.lifespan) meteor.lifespan = -1;
    g_meteorEmission += elapsed;
    const float pi = 3.14159265f;
    while (g_meteorEmission > g_nextMeteor) {
        for (Meteor& meteor : g_meteors) {
            if (meteor.lifespan > 0) continue;
            const float from = 2 * pi * MeteorRandom();
            const float to = from + pi + (2 * MeteorRandom() - 1) * pi / 6;
            const float speed = kMeteorSpeed + MeteorRandom() * kMeteorSpeedRandom;
            meteor.at[0] = cosf(from) * kNightSkyFar * 0.25f;
            meteor.at[1] = kMeteorHeight;
            meteor.at[2] = sinf(from) * kNightSkyFar * 0.25f;
            const float way[3] = {cosf(to) * kNightSkyFar - meteor.at[0], 0, sinf(to) * kNightSkyFar - meteor.at[2]};
            const float length = sqrtf(way[0] * way[0] + way[2] * way[2]);
            for (int i = 0; i < 3; ++i) meteor.velocity[i] = length > 0 ? way[i] / length * speed : 0;
            meteor.born = now;
            meteor.lifespan = kMeteorLifespan + MeteorRandom() * kMeteorLifespanRandom;
            break;
        }
        g_meteorEmission -= g_nextMeteor;
        g_nextMeteor = kMeteorDelay + MeteorRandom() * kMeteorDelayRandom;
    }
}

void DrawMeteors(const SkyConstants& sky, float strength) {
    const float now = Seconds();
    UpdateMeteors(now);
    MeteorConstants constants = {};
    FillNightSkyConstants(sky, strength, constants.sky);
    int live = 0;
    for (const Meteor& meteor : g_meteors) {
        if (meteor.lifespan <= 0) continue;
        float* start = constants.meteor[live * 2];
        float* motion = constants.meteor[live * 2 + 1];
        for (int i = 0; i < 3; ++i) {
            start[i] = meteor.at[i];
            motion[i] = meteor.velocity[i];
        }
        start[3] = now - meteor.born;
        motion[3] = meteor.lifespan;
        ++live;
    }
    if (live == 0) return;
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(g_context->Map(g_meteorConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    memcpy(mapped.pData, &constants, sizeof(constants));
    g_context->Unmap(g_meteorConstants, 0);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    g_context->IASetInputLayout(nullptr);
    g_context->VSSetShader(g_meteorVertex, nullptr, 0);
    g_context->PSSetShader(g_meteorPixel, nullptr, 0);
    g_context->VSSetConstantBuffers(0, 1, &g_meteorConstants);
    g_context->PSSetConstantBuffers(0, 1, &g_meteorConstants);
    const float noFactor[4] = {1, 1, 1, 1};
    g_context->OMSetBlendState(g_sunBlend, noFactor, 0xFFFFFFFF);
    g_context->Draw(live * 2, 0);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

// The stars, then the meteors, while the night sky's strength isn't 0.
void DrawNightSky(const SkyConstants& sky) {
    const float strength = NightSkyStrength(g_timeOfDay);
    if (strength <= 0 || !MakeNightSkyObjects()) return;
    static bool told = false;
    if (!told) {
        told = true;
        rfa::Log("Night sky: drawn from %.2f h, strength %.2f: %d stars and up to %d meteors", g_timeOfDay, strength, g_starsDrawn, kMostMeteors);
    }
    DrawStars(sky, strength);
    DrawMeteors(sky, strength);
}

const char* FormatName(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "16-bit float (HDR)";
    case DXGI_FORMAT_R11G11B10_FLOAT: return "11/10-bit float (HDR)";
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "8-bit";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "8-bit sRGB";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "10-bit";
    default: return "other";
    }
}

// Everything on the device context the sky, clouds, fog and tone mapping change, to put back afterwards.
struct SavedState {
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* vertexBuffer = nullptr;
    UINT vertexStride = 0, vertexOffset = 0;
    ID3D11Buffer* indexBuffer = nullptr;
    DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
    UINT indexOffset = 0;
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11GeometryShader* geometryShader = nullptr;
    ID3D11Buffer* vertexConstants[2] = {};
    ID3D11Buffer* pixelConstants[3] = {};
    ID3D11ShaderResourceView* pixelResources[3] = {};
    ID3D11SamplerState* pixelSampler = nullptr;
    ID3D11BlendState* blend = nullptr;
    float blendFactor[4] = {};
    UINT sampleMask = 0;
    ID3D11RasterizerState* raster = nullptr;

    void Save() {
        g_context->IAGetPrimitiveTopology(&topology);
        g_context->IAGetInputLayout(&layout);
        g_context->IAGetVertexBuffers(0, 1, &vertexBuffer, &vertexStride, &vertexOffset);
        g_context->IAGetIndexBuffer(&indexBuffer, &indexFormat, &indexOffset);
        g_context->VSGetShader(&vertexShader, nullptr, nullptr);
        g_context->PSGetShader(&pixelShader, nullptr, nullptr);
        g_context->GSGetShader(&geometryShader, nullptr, nullptr);
        g_context->VSGetConstantBuffers(0, 2, vertexConstants);
        g_context->PSGetConstantBuffers(0, 3, pixelConstants);
        g_context->PSGetShaderResources(0, 3, pixelResources);
        g_context->PSGetSamplers(0, 1, &pixelSampler);
        g_context->OMGetBlendState(&blend, blendFactor, &sampleMask);
        g_context->RSGetState(&raster);
    }

    void Restore() {
        g_context->IASetPrimitiveTopology(topology);
        g_context->IASetInputLayout(layout);
        g_context->IASetVertexBuffers(0, 1, &vertexBuffer, &vertexStride, &vertexOffset);
        g_context->IASetIndexBuffer(indexBuffer, indexFormat, indexOffset);
        g_context->VSSetShader(vertexShader, nullptr, 0);
        g_context->PSSetShader(pixelShader, nullptr, 0);
        g_context->GSSetShader(geometryShader, nullptr, 0);
        g_context->VSSetConstantBuffers(0, 2, vertexConstants);
        g_context->PSSetConstantBuffers(0, 3, pixelConstants);
        g_context->PSSetShaderResources(0, 3, pixelResources);
        g_context->PSSetSamplers(0, 1, &pixelSampler);
        g_context->OMSetBlendState(blend, blendFactor, sampleMask);
        g_context->RSSetState(raster);
        for (IUnknown* held : {static_cast<IUnknown*>(layout), static_cast<IUnknown*>(vertexBuffer), static_cast<IUnknown*>(indexBuffer),
                               static_cast<IUnknown*>(vertexShader), static_cast<IUnknown*>(pixelShader), static_cast<IUnknown*>(geometryShader),
                               static_cast<IUnknown*>(vertexConstants[0]), static_cast<IUnknown*>(vertexConstants[1]), static_cast<IUnknown*>(pixelConstants[0]), static_cast<IUnknown*>(pixelConstants[1]),
                               static_cast<IUnknown*>(pixelConstants[2]),
                               static_cast<IUnknown*>(pixelResources[0]), static_cast<IUnknown*>(pixelResources[1]), static_cast<IUnknown*>(pixelResources[2]),
                               static_cast<IUnknown*>(pixelSampler), static_cast<IUnknown*>(blend), static_cast<IUnknown*>(raster)})
            if (held) held->Release();
    }
};

// Draws the sky and the clouds on whatever the game is drawing to, behind what it has drawn, and
// puts the device context back as it was.
void DrawSky() {
    ID3D11RenderTargetView* target = nullptr;
    ID3D11DepthStencilView* depthView = nullptr;
    g_context->OMGetRenderTargets(1, &target, &depthView);
    ID3D11DepthStencilState* gameDepth = nullptr;
    UINT stencilRef = 0;
    g_context->OMGetDepthStencilState(&gameDepth, &stencilRef);
    D3D11_DEPTH_STENCIL_DESC gameDepthDesc = {};
    gameDepthDesc.DepthFunc = D3D11_COMPARISON_LESS;  // what no state means
    if (gameDepth) gameDepth->GetDesc(&gameDepthDesc);
    const bool reversed = gameDepthDesc.DepthFunc == D3D11_COMPARISON_GREATER || gameDepthDesc.DepthFunc == D3D11_COMPARISON_GREATER_EQUAL;
    g_farDepth = reversed ? 0.0f : 1.0f;
    D3D11_VIEWPORT viewport = {};
    UINT viewports = 1;
    g_context->RSGetViewports(&viewports, &viewport);

    static bool told = false;
    if (!told) {
        told = true;
        D3D11_RENDER_TARGET_VIEW_DESC targetDesc = {};
        if (target) target->GetDesc(&targetDesc);
        rfa::Log("Sky: drawing into a %s target, %.0f x %.0f, %s; the game's depth test %d (%s)",
                 target ? FormatName(targetDesc.Format) : "missing", viewport.Width, viewport.Height,
                 depthView ? "with a depth buffer" : "WITHOUT a depth buffer", static_cast<int>(gameDepthDesc.DepthFunc),
                 reversed ? "far is 0" : "far is 1");
    }

    // Without a depth buffer the sky would cover what has been drawn: leave it.
    if (target && depthView && viewport.Width > 0 && viewport.Height > 0) {
        SkyConstants constants = {};
        SkyColours(constants.hemisphere);
        if (g_tellColours)
            rfa::Log("Sky at %.2f h, brightness %.3g: west horizon %.3f %.3f %.3f, east horizon %.3f %.3f %.3f, zenith %.3f %.3f %.3f",
                     g_timeOfDay, g_brightness, constants.hemisphere[0][0], constants.hemisphere[0][1], constants.hemisphere[0][2],
                     constants.hemisphere[5][0], constants.hemisphere[5][1], constants.hemisphere[5][2], constants.hemisphere[4][0],
                     constants.hemisphere[4][1], constants.hemisphere[4][2]);
        const float tanHalfVertical = tanf(g_cameraFov * 3.14159265f / 360.0f);
        for (int i = 0; i < 3; ++i) {
            constants.right[i] = g_cameraRotation[i];
            constants.up[i] = g_cameraRotation[3 + i];
            constants.forward[i] = g_cameraRotation[6 + i];
        }
        constants.right[3] = tanHalfVertical * viewport.Width / viewport.Height;
        constants.up[3] = tanHalfVertical;
        constants.forward[3] = reversed ? 0.0f : 1.0f;
        // Guerrilla's fog over the sky: the world's fog colour and numbers, the dome's scale and offset.
        float fogColour[3], correction[3];
        TodColour(kFogColour, fogColour);
        PartTint(correction);
        for (int i = 0; i < 3; ++i) constants.fogColour[i] = fogColour[i] * correction[i] * SkyBrightness();
        FogNumbers(constants.fogNumbers);
        constants.domeFog[0] = kSkyFogFar;
        constants.domeFog[1] = TodNumber(kSkyFogScale);
        constants.domeFog[2] = TodNumber(kSkyFogOffset) - g_cameraPosition[1];
        if (g_tellColours)
            rfa::Log("Sky's fog at %.2f h: dome scale %.0f offset %.0f, clouds %.0f %.0f, backdrop %.0f %.0f (camera at %.0f m); fog colour "
                     "%.3f %.3f %.3f, fall-off %.4f, density %.5f, offset %.3f",
                     g_timeOfDay, constants.domeFog[1], TodNumber(kSkyFogOffset), TodNumber(kCloudsFogScale), TodNumber(kCloudsFogOffset),
                     TodNumber(kMatteFogScale), TodNumber(kMatteFogOffset), g_cameraPosition[1], constants.fogColour[0], constants.fogColour[1],
                     constants.fogColour[2], constants.fogNumbers[0], constants.fogNumbers[1], constants.fogNumbers[3]);
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(g_context->Map(g_skyConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, &constants, sizeof(constants));
            g_context->Unmap(g_skyConstants, 0);
            SavedState saved;
            saved.Save();

            g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_context->IASetInputLayout(nullptr);
            g_context->VSSetShader(g_skyVertex, nullptr, 0);
            g_context->PSSetShader(g_skyPixel, nullptr, 0);
            g_context->GSSetShader(nullptr, nullptr, 0);
            g_context->VSSetConstantBuffers(0, 1, &g_skyConstants);
            g_context->PSSetConstantBuffers(0, 1, &g_skyConstants);
            const float noFactor[4] = {1, 1, 1, 1};
            g_context->OMSetBlendState(g_skyBlend, noFactor, 0xFFFFFFFF);
            g_context->OMSetDepthStencilState(g_skyDepth[reversed ? 1 : 0], 0);
            g_context->RSSetState(g_skyRaster);
            g_context->Draw(3, 0);
            DrawOrbitals(constants);
            DrawNightSky(constants);
            if (g_cloudsOn) DrawClouds(constants);

            saved.Restore();
        }
        g_tellColours = false;
    }
    g_context->OMSetDepthStencilState(gameDepth, stencilRef);
    if (gameDepth) gameDepth->Release();
    if (target) target->Release();
    if (depthView) depthView->Release();
}

// What the sun's light is drawn through when it has its shadows: the sun's lamp's shadow map, the
// G-buffer's depth, the game's constants (vc4) and the lamp's (vc0), as the light pass drew the lamp.
struct SunShadowInputs {
    ID3D11ShaderResourceView* map;
    ID3D11ShaderResourceView* depth;
    ID3D11Buffer* game;
    ID3D11Buffer* lamp;
};

// What Guerrilla's material shaders (the world builder's gw_* effect files, see LoadShaders) are given
// in the material pass, in registers the engine never binds (it binds textures t0-t15 and constant
// buffers b0-b7 only): the mod's own light as it added it to the light buffer, before the game's
// ambient occlusion, with the sun's shadow in alpha (t16); the light buffer as it was just before the
// occlusion (t17); and the frame's sun, sky and time (b8). From those a shader has the occlusion (the
// light buffer then and now), the lamps' light (the light buffer less the mod's) and the shadow, and
// lights its surface as Guerrilla's shader does. Made by the light pass in our worlds only; without
// them (b8 unbound) the shaders light by the light buffer, as Armageddon's do.
struct SurfaceConstants {
    float sunToward[4];  // toward the sun or moon, in the world; w 1: given
    float sunColour[4];  // Guerrilla's V_light_color (× the colour correction)
    float above[4];      // V_ambient_render: the sky's light facing the light
    float below[4];      // V_back_ambient_render: facing away from it
    float time[4];       // x seconds; w the lamps' night level (Guerrilla's Time.w)
    float sunTowardView[4];  // the sun's toward, in the camera's space (the terrain lights by the G-buffer's normals, in it)
    float view[4];       // the picture's width and height; the tangents of half the field of view across and up
    float tint[4];       // the colour correction the light above is already multiplied by (Guerrilla's Tint_color)
    float packFuel[4];   // the worn backpack's gauge (Guerrilla's mp_fuels: fuel, fuel to go on, 1 while on), for its
                         // rfg-mp_fuel_gauge rewrite (gw_fuelgauge0c); guerrilla_gear's GuerrillaGear_PackGauge, 0s without
};


ID3D11Texture2D* g_ourLightTexture = nullptr;
ID3D11RenderTargetView* g_ourLightTarget = nullptr;
ID3D11ShaderResourceView* g_ourLightView = nullptr;
ID3D11Texture2D* g_beforeTexture = nullptr;
ID3D11ShaderResourceView* g_beforeView = nullptr;
ID3D11Buffer* g_surfaceConstants = nullptr;
ID3D11ShaderResourceView* g_surfaceNormals = nullptr;  // the G-buffer's normals, for the terrain's material pass (t18)
bool g_ourLightCleared = false;  // this light pass's
bool g_beforeCopied = false;     // this light pass's
bool g_surfaceInputsMade = false;  // by this frame's light pass, for its material pass
bool g_surfaceInputsFailed = false;

void DropSurfaceTargets() {
    for (IUnknown** held : {reinterpret_cast<IUnknown**>(&g_ourLightView), reinterpret_cast<IUnknown**>(&g_ourLightTarget),
                            reinterpret_cast<IUnknown**>(&g_ourLightTexture), reinterpret_cast<IUnknown**>(&g_beforeView),
                            reinterpret_cast<IUnknown**>(&g_beforeTexture)})
        if (*held) {
            (*held)->Release();
            *held = nullptr;
        }
}

// The two textures, as the light buffer is (its size and format), made again when it changes.
bool MakeSurfaceTargets(const D3D11_TEXTURE2D_DESC& light) {
    if (g_surfaceInputsFailed) return false;
    if (g_ourLightTexture) {
        D3D11_TEXTURE2D_DESC have = {};
        g_ourLightTexture->GetDesc(&have);
        if (have.Width == light.Width && have.Height == light.Height && have.Format == light.Format) return true;
        DropSurfaceTargets();
    }
    D3D11_TEXTURE2D_DESC desc = light;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    bool ok = SUCCEEDED(g_device->CreateTexture2D(&desc, nullptr, &g_ourLightTexture)) &&
              SUCCEEDED(g_device->CreateRenderTargetView(g_ourLightTexture, nullptr, &g_ourLightTarget)) &&
              SUCCEEDED(g_device->CreateShaderResourceView(g_ourLightTexture, nullptr, &g_ourLightView));
    // The copy must match the light buffer exactly for CopyResource: its own description, read only.
    D3D11_TEXTURE2D_DESC copy = light;
    copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    copy.Usage = D3D11_USAGE_DEFAULT;
    copy.CPUAccessFlags = 0;
    copy.MiscFlags = 0;
    ok = ok && SUCCEEDED(g_device->CreateTexture2D(&copy, nullptr, &g_beforeTexture)) &&
         SUCCEEDED(g_device->CreateShaderResourceView(g_beforeTexture, nullptr, &g_beforeView));
    if (ok && !g_surfaceConstants) {
        D3D11_BUFFER_DESC buffer = {};
        buffer.ByteWidth = sizeof(SurfaceConstants);
        buffer.Usage = D3D11_USAGE_DYNAMIC;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ok = SUCCEEDED(g_device->CreateBuffer(&buffer, nullptr, &g_surfaceConstants));
    }
    if (!ok) {
        rfa::Log("Couldn't make what Guerrilla's material shaders are lit with; they light by the light buffer");
        DropSurfaceTargets();
        g_surfaceInputsFailed = true;
        return false;
    }
    rfa::Log("Guerrilla's material shaders: lit by the mod's own light (%u x %u)", light.Width, light.Height);
    return true;
}

// The light buffer as it is now, kept for the material pass (just before the game's occlusion).
void CopyLightBefore(ID3D11RenderTargetView* buffer) {
    if (!g_beforeTexture || g_beforeCopied) return;
    ID3D11Resource* resource = nullptr;
    buffer->GetResource(&resource);
    if (!resource) return;
    g_context->CopyResource(g_beforeTexture, resource);
    resource->Release();
    g_beforeCopied = true;
}

// Guerrilla's lamps' night level (its TOD_light_on_off: 7.5, 8.5, 17.5, 18.5): 1 at night, falling to
// 0 from 7:30 to 8:30, 0 by day, back to 1 from 17:30 to 18:30.
float NightLevel(float hours) {
    if (hours < 7.5f) return 1;
    if (hours < 8.5f) return 8.5f - hours;
    if (hours < 17.5f) return 0;
    if (hours < 18.5f) return hours - 17.5f;
    return 1;
}

float Seconds() {
    static LARGE_INTEGER frequency = {}, start = {};
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
        start = now;
    }
    return static_cast<float>(static_cast<double>(now.QuadPart - start.QuadPart) / static_cast<double>(frequency.QuadPart));
}

// The frame's constants, once the light pass has drawn the mod's light into both targets.
bool FillSurfaceConstants() {
    if (!g_surfaceConstants) return false;
    SurfaceConstants constants = {};
    float toward[3], sun[3], tint[3];
    DirectLight(toward, sun);
    PartTint(tint);
    for (int i = 0; i < 3; ++i) {
        constants.sunToward[i] = toward[i];
        constants.sunColour[i] = sun[i] * tint[i];
        constants.above[i] = g_worldAmbient[i] * tint[i];
        constants.below[i] = g_worldBackAmbient[i] * tint[i];
        constants.tint[i] = tint[i];
    }
    constants.tint[3] = 1;
    constants.sunToward[3] = 1;
    constants.time[0] = Seconds();
    constants.time[3] = NightLevel(g_timeOfDay);
    GuerrillaGear_PackGauge(constants.packFuel);  // guerrilla_gear's worn backpack's gauge (0s without it)
    // The camera's space as DrawLight's: x along its right, y its up, z its forward.
    const float* axes[3] = {&g_cameraRotation[0], &g_cameraRotation[3], &g_cameraRotation[6]};
    for (int i = 0; i < 3; ++i) constants.sunTowardView[i] = toward[0] * axes[i][0] + toward[1] * axes[i][1] + toward[2] * axes[i][2];
    D3D11_TEXTURE2D_DESC size = {};
    if (g_ourLightTexture) g_ourLightTexture->GetDesc(&size);
    constants.view[0] = static_cast<float>(size.Width);
    constants.view[1] = static_cast<float>(size.Height);
    constants.view[3] = tanf(g_cameraFov * 3.14159265f / 360.0f);
    constants.view[2] = size.Height ? constants.view[3] * size.Width / size.Height : constants.view[3];
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(g_context->Map(g_surfaceConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
    memcpy(mapped.pData, &constants, sizeof(constants));
    g_context->Unmap(g_surfaceConstants, 0);
    return true;
}

// Adds the sky's light and the sun into the light buffer the light pass fills (its lamps' light), over
// the whole buffer, and puts the device context back as it was: either or both, the sun through its
// lamp's shadow map when given. The same light goes into the mod's own target for Guerrilla's
// material shaders (SurfaceConstants), cleared before the first of a pass.
void DrawLight(ID3D11RenderTargetView* buffer, ID3D11ShaderResourceView* normals, bool withSky, bool withSun, const SunShadowInputs* shadow) {
    ID3D11Resource* resource = nullptr;
    buffer->GetResource(&resource);
    ID3D11Texture2D* texture = nullptr;
    D3D11_TEXTURE2D_DESC desc = {};
    if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)))) {
        texture->GetDesc(&desc);
        texture->Release();
    }
    if (resource) resource->Release();
    if (desc.Width == 0 || desc.Height == 0) return;
    if (MakeSurfaceTargets(desc) && !g_ourLightCleared) {
        const float none[4] = {0, 0, 0, 0};
        g_context->ClearRenderTargetView(g_ourLightTarget, none);
        g_ourLightCleared = true;
    }
    static bool told = false;
    if (!told) {
        told = true;
        D3D11_SHADER_RESOURCE_VIEW_DESC normalsDesc = {};
        normals->GetDesc(&normalsDesc);
        rfa::Log("Light: adding the sky's light and the sun to the game's light buffer (%s, format %d, %u x %u); normals format %d",
                 FormatName(desc.Format), static_cast<int>(desc.Format), desc.Width, desc.Height, static_cast<int>(normalsDesc.Format));
    }

    LightConstants constants = {};
    float toward[3], sun[3], tint[3];
    DirectLight(toward, sun);
    PartTint(tint);
    for (int i = 0; i < 3; ++i) {
        constants.fromAbove[i] = g_worldAmbient[i] * tint[i];
        constants.fromBelow[i] = g_worldBackAmbient[i] * tint[i];
        constants.sunColour[i] = sun[i] * tint[i];
    }
    // The camera's space: x along its right, y its up, z its forward (its rows in the world).
    const float* axes[3] = {&g_cameraRotation[0], &g_cameraRotation[3], &g_cameraRotation[6]};
    for (int i = 0; i < 3; ++i) {
        constants.sunToward[i] = toward[0] * axes[i][0] + toward[1] * axes[i][1] + toward[2] * axes[i][2];
        constants.upToward[i] = axes[i][1];
    }
    constants.targetSize[0] = static_cast<float>(desc.Width);
    constants.targetSize[1] = static_cast<float>(desc.Height);
    constants.targetSize[3] = tanf(g_cameraFov * 3.14159265f / 360.0f);
    constants.targetSize[2] = constants.targetSize[3] * desc.Width / desc.Height;
    constants.parts[0] = withSky ? 1.0f : 0.0f;
    constants.parts[1] = withSun ? 1.0f : 0.0f;
    constants.parts[2] = shadow ? 1.0f : 0.0f;
    constants.parts[3] = g_shine;
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(g_context->Map(g_lightConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    memcpy(mapped.pData, &constants, sizeof(constants));
    g_context->Unmap(g_lightConstants, 0);

    SavedState saved;
    saved.Save();
    ID3D11RenderTargetView* gameTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* gameDepthView = nullptr;
    g_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, gameTargets, &gameDepthView);
    ID3D11DepthStencilState* gameDepth = nullptr;
    UINT stencilRef = 0;
    g_context->OMGetDepthStencilState(&gameDepth, &stencilRef);
    D3D11_VIEWPORT gameViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    g_context->RSGetViewports(&viewportCount, gameViewports);

    const D3D11_VIEWPORT viewport = {0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0, 1};
    ID3D11RenderTargetView* targets[2] = {buffer, g_ourLightTarget};
    g_context->OMSetRenderTargets(g_ourLightTarget ? 2 : 1, targets, nullptr);
    g_context->RSSetViewports(1, &viewport);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->IASetInputLayout(nullptr);
    g_context->VSSetShader(g_lightVertex, nullptr, 0);
    g_context->PSSetShader(g_lightPixel, nullptr, 0);
    g_context->GSSetShader(nullptr, nullptr, 0);
    g_context->PSSetConstantBuffers(0, 1, &g_lightConstants);
    g_context->PSSetShaderResources(0, 1, &normals);
    if (shadow) {
        ID3D11Buffer* buffers[2] = {shadow->game, shadow->lamp};
        g_context->PSSetConstantBuffers(1, 2, buffers);
        ID3D11ShaderResourceView* resources[2] = {shadow->map, shadow->depth};
        g_context->PSSetShaderResources(1, 2, resources);
    }
    g_context->PSSetSamplers(0, 1, &g_lightSampler);
    const float noFactor[4] = {1, 1, 1, 1};
    g_context->OMSetBlendState(g_lightBlend, noFactor, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(g_lightDepth, 0);
    g_context->RSSetState(g_lightRaster);
    g_context->Draw(3, 0);

    g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, gameTargets, gameDepthView);
    g_context->RSSetViewports(viewportCount, gameViewports);
    g_context->OMSetDepthStencilState(gameDepth, stencilRef);
    saved.Restore();
    for (ID3D11RenderTargetView* target : gameTargets)
        if (target) target->Release();
    if (gameDepthView) gameDepthView->Release();
    if (gameDepth) gameDepth->Release();
}

// For the fog, drawn later in the frame (as the particles begin): the G-buffer's depth (t14) and the
// game's own constants (vc4, whose IR_Similarity_Data turns depth into distance), noted during the
// light pass. The game keeps changing that buffer after the light pass, so the fog reads a copy of it
// as the light pass had it (g_fogGameCopy), as the mod's light does live.
bool g_noteFog = false;
ID3D11ShaderResourceView* g_fogDepth = nullptr;
ID3D11Buffer* g_gameConstants = nullptr;
ID3D11Buffer* g_fogGameCopy = nullptr;
// The fog waits from the sky step for the particles (see ParticlePhase): Armageddon's sky step draws
// into a layer of its own (8× multisampled, black before it), laid over the scene only where nothing
// was drawn, so fog drawn there was lost on everything but the sky; the particles draw into the lit
// scene itself, once the sky is laid in.
bool g_fogPending = false;
bool g_fogAtParticles = false;  // the particle phase is hooked (else the fog is drawn at the sky step)
bool TextureOf(ID3D11View* view, D3D11_TEXTURE2D_DESC& desc);
bool g_fogGameCopied = false;  // this frame's copy is made
// During the light pass: the game's constants copied for the fog, from the light pass's first check
// and again (as the mod's light reads them) right after the sun's lamp is drawn.
void CopyGameConstants(ID3D11Buffer* from) {
    if (!from || !g_device) return;
    D3D11_BUFFER_DESC desc;
    from->GetDesc(&desc);
    if (g_fogGameCopy) {
        D3D11_BUFFER_DESC have;
        g_fogGameCopy->GetDesc(&have);
        if (have.ByteWidth != desc.ByteWidth) {
            g_fogGameCopy->Release();
            g_fogGameCopy = nullptr;
        }
    }
    if (!g_fogGameCopy) {
        D3D11_BUFFER_DESC copy = {};
        copy.ByteWidth = desc.ByteWidth;
        copy.Usage = D3D11_USAGE_DEFAULT;
        copy.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(g_device->CreateBuffer(&copy, nullptr, &g_fogGameCopy))) {
            RFA_LOG_FIRST(1, "Fog: could not copy the game's constants (%u bytes); the fog reads them at the sky step", desc.ByteWidth);
            g_fogGameCopy = nullptr;
            return;
        }
    }
    g_context->CopyResource(g_fogGameCopy, from);
    g_fogGameCopied = true;
}

void DropFogInputs() {
    g_fogGameCopied = false;
    if (g_fogDepth) g_fogDepth->Release();
    if (g_gameConstants) g_gameConstants->Release();
    g_fogDepth = nullptr;
    g_gameConstants = nullptr;
}

// Draws Guerrilla's fog over what the world has drawn into the game's current target (the lit scene, as
// the particles begin), and puts the device context back as it was.
void DrawFog() {
    ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* depthView = nullptr;
    g_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, &depthView);
    ID3D11DepthStencilState* gameDepth = nullptr;
    UINT stencilRef = 0;
    g_context->OMGetDepthStencilState(&gameDepth, &stencilRef);
    D3D11_DEPTH_STENCIL_DESC gameDepthDesc = {};
    gameDepthDesc.DepthFunc = D3D11_COMPARISON_LESS;  // what no state means
    if (gameDepth) gameDepth->GetDesc(&gameDepthDesc);
    const bool reversed = gameDepthDesc.DepthFunc == D3D11_COMPARISON_GREATER || gameDepthDesc.DepthFunc == D3D11_COMPARISON_GREATER_EQUAL;
    D3D11_VIEWPORT gameViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    g_context->RSGetViewports(&viewportCount, gameViewports);
    D3D11_VIEWPORT viewport = viewportCount > 0 ? gameViewports[0] : D3D11_VIEWPORT{};
    ID3D11RenderTargetView* into = targets[0];
    D3D11_TEXTURE2D_DESC scene = {};
    if (into && TextureOf(into, scene) && scene.SampleDesc.Count == 1) {
        viewport = {0, 0, static_cast<float>(scene.Width), static_cast<float>(scene.Height), 0, 1};
        RFA_LOG_FIRST(1, "Fog: drawn into the lit scene (%u x %u, format %d) as the particles begin", scene.Width, scene.Height, static_cast<int>(scene.Format));
    } else {
        RFA_LOG_FIRST(1, "Fog: the picture as the particles begin is format %d with %u samples, not the lit scene expected",
                      static_cast<int>(scene.Format), scene.SampleDesc.Count);
    }
    D3D11_BUFFER_DESC gameConstantsDesc = {};
    g_gameConstants->GetDesc(&gameConstantsDesc);

    FogConstants constants = {};
    float colour[3], tint[3];
    TodColour(kFogColour, colour);
    PartTint(tint);
    for (int i = 0; i < 3; ++i) constants.fogColour[i] = colour[i] * tint[i];
    FogNumbers(constants.fogNumbers);
    // The camera's space: x along its right, y its up, z its forward (its rows in the world).
    const float* axes[3] = {&g_cameraRotation[0], &g_cameraRotation[3], &g_cameraRotation[6]};
    for (int i = 0; i < 3; ++i) constants.upToward[i] = axes[i][1];
    constants.upToward[3] = reversed ? 0.0f : 1.0f;
    constants.targetSize[0] = viewport.Width;
    constants.targetSize[1] = viewport.Height;
    constants.targetSize[3] = tanf(g_cameraFov * 3.14159265f / 360.0f);
    constants.targetSize[2] = viewport.Height > 0 ? constants.targetSize[3] * viewport.Width / viewport.Height : 0;
    if (g_tellFog) {
        g_tellFog = false;
        rfa::Log("Fog at %.2f h: colour %.4f %.4f %.4f, fall-off %.4f, density at the camera %.5f, offset %.3f (Fog=%.3g)",
                 g_timeOfDay, constants.fogColour[0], constants.fogColour[1], constants.fogColour[2], constants.fogNumbers[0],
                 constants.fogNumbers[1], constants.fogNumbers[3], g_fog);
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (!into || viewport.Width <= 0 || viewport.Height <= 0 || gameConstantsDesc.ByteWidth < 13 * 16) {
        RFA_LOG_FIRST(1, "Fog: nothing to draw into, or the game's constants are not the ones expected (%u bytes); no fog",
                      gameConstantsDesc.ByteWidth);
    } else if (SUCCEEDED(g_context->Map(g_fogConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &constants, sizeof(constants));
        g_context->Unmap(g_fogConstants, 0);
        SavedState saved;
        saved.Save();
        g_context->OMSetRenderTargets(1, &into, nullptr);  // the depth is read, so it can't stay bound for testing
        g_context->RSSetViewports(1, &viewport);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->IASetInputLayout(nullptr);
        g_context->VSSetShader(g_fogVertex, nullptr, 0);
        g_context->PSSetShader(g_fogPixel, nullptr, 0);
        g_context->GSSetShader(nullptr, nullptr, 0);
        ID3D11Buffer* buffers[2] = {g_fogConstants, g_fogGameCopied ? g_fogGameCopy : g_gameConstants};
        g_context->PSSetConstantBuffers(0, 2, buffers);
        g_context->PSSetShaderResources(0, 1, &g_fogDepth);
        g_context->PSSetSamplers(0, 1, &g_fogSampler);
        const float noFactor[4] = {1, 1, 1, 1};
        g_context->OMSetBlendState(g_fogBlend, noFactor, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_fogDepthTest, 0);
        g_context->RSSetState(g_fogRaster);
        g_context->Draw(3, 0);
        g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, depthView);
        g_context->RSSetViewports(viewportCount, gameViewports);
        saved.Restore();
        g_context->OMSetDepthStencilState(gameDepth, stencilRef);
    }
    for (ID3D11RenderTargetView* target : targets)
        if (target) target->Release();
    if (depthView) depthView->Release();
    if (gameDepth) gameDepth->Release();
}

// Where the sun is on the picture (0 to 1 across, and down; past those when it is off the picture): false
// while it is behind the camera. That is Re-Mars-tered's own rule for its sun shafts: its scene render
// (rl_scene_renderer::render_scene_part2) draws them only while the camera's forward and the way to the sun
// make a positive dot product, with Rl_sun_shafts_enabled; it never asks whether the sun is above the
// horizon, so neither does this (below it, the sky the shafts are made from is ground anyway). Its place for
// them is the sun's own way each frame (the orbital's effect, object_effect::update_render, hands it to
// game_render_set_sun_shafts_position; game_render_setup_frame adds the camera's place), with no height test
// there either. Their tint's last factor is the renderer's fade colour (rl_state_manager::m_fade_color), 1
// but in a screen fade, so ours is 1.
bool SunOnPicture(UINT width, UINT height, float (&at)[2]) {
    float toward[3];
    SunToward(toward);
    if (height == 0) return false;
    const float* axes[3] = {&g_cameraRotation[0], &g_cameraRotation[3], &g_cameraRotation[6]};  // right, up, forward
    float along[3];
    for (int a = 0; a < 3; ++a) along[a] = toward[0] * axes[a][0] + toward[1] * axes[a][1] + toward[2] * axes[a][2];
    if (along[2] <= 0) return false;
    const float tanUp = tanf(g_cameraFov * 3.14159265f / 360.0f), tanAcross = tanUp * width / height;
    at[0] = 0.5f + 0.5f * along[0] / (along[2] * tanAcross);
    at[1] = 0.5f - 0.5f * along[1] / (along[2] * tanUp);
    return true;
}

// Guerrilla's sun shafts for this frame (see kShaftShader), from the scene and the G-buffer's depth
// (as kept at the sky step, see KeepShaftDepth), into the first half-size target: the view to read them from, or none. Draws with the tone mapping's
// sampler, depth and rasterizer states (set by the caller, who puts the device context back).
ID3D11ShaderResourceView* DrawShafts(ID3D11ShaderResourceView* scene, UINT width, UINT height, const float (&sun)[2]) {
    if (!g_shaftVertex && !MakeShaftObjects()) {
        g_shaftsFailed = true;
        return nullptr;
    }
    const UINT halfWidth = (width + 1) / 2, halfHeight = (height + 1) / 2;
    if (!ShaftTargets(halfWidth, halfHeight)) {
        g_shaftsFailed = true;
        return nullptr;
    }
    ShaftConstants constants = {};
    constants.sun[0] = sun[0];
    constants.sun[1] = sun[1];
    constants.sun[2] = static_cast<float>(width) / height;
    constants.sun[3] = g_farDepth;
    constants.texel[0] = 1.0f / halfWidth;
    constants.texel[1] = 1.0f / halfHeight;
    constants.texel[2] = 1.0f / width;
    constants.texel[3] = 1.0f / height;
    constants.strength[0] = kShaftScale * 0.15625f;
    constants.strength[1] = kShaftBaseLum;
    constants.strength[2] = kShaftLumStep;
    constants.strength[3] = kShaftSunSize;
    constants.curve[0] = kShaftRadius;
    constants.curve[1] = kShaftCurveBase;
    constants.curve[2] = kShaftCurveExponent;
    constants.curve[3] = kShaftCurveShift;
    // The Gaussian, each way: e^(−x² ÷ 2rho²) for 0, 1 and 2 texels off, the five adding to 1, with the
    // multiplier shared between the two ways.
    float weights[3], total = 0;
    for (int i = 0; i < 3; ++i) {
        weights[i] = expf(-static_cast<float>(i * i) / (2 * kShaftBlurRho * kShaftBlurRho));
        total += i ? 2 * weights[i] : weights[i];
    }
    for (int i = 0; i < 3; ++i) constants.blur[i] = weights[i] / total * sqrtf(kShaftBlurMultiplier);
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(g_context->Map(g_shaftConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return nullptr;
    memcpy(mapped.pData, &constants, sizeof(constants));
    g_context->Unmap(g_shaftConstants, 0);
    RFA_LOG_FIRST(1, "Sun shafts: Guerrilla's, toward the sun in world \"%s\" (SunShafts=%.3g)", WorldName(), g_sunShafts);

    const float noFactor[4] = {1, 1, 1, 1};
    const D3D11_VIEWPORT half = {0, 0, static_cast<float>(halfWidth), static_cast<float>(halfHeight), 0, 1};
    g_context->RSSetViewports(1, &half);
    g_context->VSSetShader(g_shaftVertex, nullptr, 0);
    g_context->PSSetConstantBuffers(0, 1, &g_shaftConstants);
    g_context->OMSetBlendState(g_eyeBlend, noFactor, 0xFFFFFFFF);
    ID3D11ShaderResourceView* none[2] = {};
    struct Pass {
        ID3D11PixelShader* shader;
        ID3D11RenderTargetView* target;
        ID3D11ShaderResourceView* inputs[2];
    };
    ID3D11ShaderResourceView* const depth = g_shaftDepthKept ? g_shaftDepthCopyView : g_shaftDepth;
    const Pass passes[3] = {{g_shaftSource, g_shaftTarget[0], {scene, depth}},
                            {g_shaftBlur, g_shaftTarget[1], {g_shaftView[0], nullptr}},
                            {g_shaftRadial, g_shaftTarget[0], {g_shaftView[1], nullptr}}};
    for (const Pass& pass : passes) {
        g_context->PSSetShaderResources(0, 2, none);
        g_context->OMSetRenderTargets(1, &pass.target, nullptr);
        g_context->PSSetShaderResources(0, 2, pass.inputs);
        g_context->PSSetShader(pass.shader, nullptr, 0);
        g_context->Draw(3, 0);
    }
    g_context->PSSetShaderResources(0, 2, none);
    g_context->OMSetRenderTargets(0, nullptr, nullptr);
    return g_shaftView[0];
}

// A 16-bit float, as the half-size targets hold them, as a float.
float FromHalf(uint16_t half) {
    const int exponent = (half >> 10) & 31, fraction = half & 1023;
    float value;
    if (exponent == 0) value = ldexpf(static_cast<float>(fraction), -24);  // the smallest, without the leading 1
    else if (exponent == 31) value = fraction ? NAN : INFINITY;
    else value = ldexpf(static_cast<float>(fraction | 1024), exponent - 25);
    return (half & 0x8000) ? -value : value;
}

// The log's "Sun shafts at" line, with Log=1, every 10 seconds: the hour, how high the sun is, where it is on the
// picture, and what the shafts are made of there, to set beside Re-Mars-tered at the same hour, place and facing.
// When shafts were drawn, a texel of the blurred sky round the sun is copied from the sun's place (when that is on
// the picture) and one of the shafts from the picture's middle, and read a frame or two later without waiting;
// with the sun behind the camera (none drawn, as in Re-Mars-tered) the line says so at once. The values are the
// scene's own (before the tone mapping's × 8, the eye and the bloom).
void WatchShafts(ID3D11ShaderResourceView* drawn, const float (&sunAt)[2]) {
    if (g_shaftReadPending) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(g_context->Map(g_shaftReadback, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) return;
        const uint16_t* texels = static_cast<const uint16_t*>(mapped.pData);
        float sky[3], shafts[3];
        for (int i = 0; i < 3; ++i) {
            sky[i] = FromHalf(texels[i]);
            shafts[i] = FromHalf(texels[4 + i]);
        }
        const float skyShare = FromHalf(texels[3]);  // the source pass's alpha, blurred: how much of the taps there were sky
        g_context->Unmap(g_shaftReadback, 0);
        g_shaftReadPending = false;
        const ShaftLook& look = g_shaftLook;
        static int told = 0;
        if (told++ >= 60) return;
        if (look.skyRead)
            rfa::LogDetail("Sun shafts at %.2f h: the sun %.0f degrees up, on the picture at %.2f %.2f; the sky round it %.3f %.3f %.3f "
                           "(%.0f%% of it sky, by the depth %s), the shafts at the picture's middle %.4f %.4f %.4f (the scene's values)",
                           look.hours, look.sunHeight, look.at[0], look.at[1], sky[0], sky[1], sky[2], skyShare * 100,
                           look.depthKept ? "kept at the sky step" : "at the frame's end", shafts[0], shafts[1], shafts[2]);
        else
            rfa::LogDetail("Sun shafts at %.2f h: the sun %.0f degrees up, off the picture at %.2f %.2f (none of the sky round it on the "
                           "picture); the shafts at the picture's middle %.4f %.4f %.4f (the scene's values)",
                           look.hours, look.sunHeight, look.at[0], look.at[1], shafts[0], shafts[1], shafts[2]);
        return;
    }
    if (GetTickCount64() - g_lastShaftLog < 10000) return;
    g_lastShaftLog = GetTickCount64();
    float toward[3];
    SunToward(toward);
    const float height = asinf(toward[1] < -1 ? -1 : toward[1] > 1 ? 1 : toward[1]) * 180.0f / 3.14159265f;
    if (!drawn) {
        static int toldNone = 0;
        if (toldNone++ < 30)
            rfa::LogDetail("Sun shafts at %.2f h: none, the sun (%.0f degrees up) is behind the camera; Re-Mars-tered draws none then either",
                           g_timeOfDay, height);
        return;
    }
    if (!g_shaftReadback) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = 2;
        desc.Height = 1;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &g_shaftReadback))) return;
    }
    g_shaftLook = {g_timeOfDay, height, {sunAt[0], sunAt[1]}, false, g_shaftDepthKept};
    const int x = static_cast<int>(floorf(sunAt[0] * g_shaftWidth)), y = static_cast<int>(floorf(sunAt[1] * g_shaftHeight));
    if (x >= 0 && y >= 0 && x < static_cast<int>(g_shaftWidth) && y < static_cast<int>(g_shaftHeight)) {
        const D3D11_BOX sun = {static_cast<UINT>(x), static_cast<UINT>(y), 0, static_cast<UINT>(x) + 1, static_cast<UINT>(y) + 1, 1};
        g_context->CopySubresourceRegion(g_shaftReadback, 0, 0, 0, 0, g_shaft[1], 0, &sun);
        g_shaftLook.skyRead = true;
    }
    const D3D11_BOX middle = {g_shaftWidth / 2, g_shaftHeight / 2, 0, g_shaftWidth / 2 + 1, g_shaftHeight / 2 + 1, 1};
    g_context->CopySubresourceRegion(g_shaftReadback, 0, 1, 0, 0, g_shaft[0], 0, &middle);
    g_shaftReadPending = true;
}

// Guerrilla's tone mapping, drawn over Armageddon's final picture (its target, 8-bit) from the scene it
// was made from (16-bit float, at exposure 1) and its bloom: the eye's brightness eased toward this
// frame's measure, the sun shafts, then the picture. Puts the device context back as it was.
int g_eyeNow = 0;           // which of the two textures holds the eye's brightness
bool g_eyeFresh = true;     // start the eye at the measure (the first frame, or after a gap)
LARGE_INTEGER g_toneLast = {};
ULONGLONG g_lastEyeLog = 0;
bool g_eyeReadPending = false;
float g_eyeBrightness = -1;  // as last read back (< 0: not yet)

// Guerrilla's particle layer (see ParticlePhase): this frame's particles, once the phase has drawn them.
ID3D11ShaderResourceView* g_particleView = nullptr;
bool g_particlesReady = false;

// Guerrilla's bloom (see kBloomShader), in place of Armageddon's (its stages a sixth, twelfth and
// 24th of the picture, equal parts, clamped, from a picture not scaled by the eye): three stages of
// half-float pictures, a quarter of the picture's size and then half that twice (Guerrilla makes
// two more it never uses), each in two (blurred one way into the other and back), remade when the
// picture's size changes. Its curves from the time of day (bloom_exposure, bloom_theta, _slope_A,
// _slope_B; bloom_amount in TonePixel). Re-Mars-tered always draws with fixed values instead
// (RL_hdr_override: exposure 0.6, theta 1.2, slopes 0 and 0.08, amount 2.5); the Steam Edition's
// can't be read (its code is encrypted), but Armageddon's copy of the switch is off unless set from
// the console ("return control to the environment"), and the districts' curves are tuned one by one.
constexpr size_t kBloomExposure = 0x3678, kBloomTheta = 0x41E8, kBloomSlopeA = 0x42DC, kBloomSlopeB = 0x43D0;

struct BloomConstants {
    float bright[4];
    float curve[4];
    float sizes[4];
    float taps[9][4];
};

struct BloomStage {
    ID3D11Texture2D* texture[2] = {};
    ID3D11RenderTargetView* target[2] = {};
    ID3D11ShaderResourceView* view[2] = {};
    UINT width = 0, height = 0;
};

ID3D11PixelShader* g_bloomBright = nullptr;
ID3D11PixelShader* g_bloomBox = nullptr;
ID3D11PixelShader* g_bloomBlur = nullptr;
ID3D11PixelShader* g_bloomCombine = nullptr;
ID3D11Buffer* g_bloomConstants = nullptr;
BloomStage g_bloomStages[3];
UINT g_bloomForWidth = 0, g_bloomForHeight = 0;  // the picture's size the stages were made for
bool g_bloomFailed = false;                     // then Armageddon's bloom stays

bool MakeBloomShaders() {
    ID3DBlob* blobs[4] = {Compile(kBloomShader, sizeof(kBloomShader) - 1, "BrightPixel", "ps_4_0"),
                          Compile(kBloomShader, sizeof(kBloomShader) - 1, "BoxPixel", "ps_4_0"),
                          Compile(kBloomShader, sizeof(kBloomShader) - 1, "BlurPixel", "ps_4_0"),
                          Compile(kBloomShader, sizeof(kBloomShader) - 1, "CombinePixel", "ps_4_0")};
    ID3D11PixelShader** shaders[4] = {&g_bloomBright, &g_bloomBox, &g_bloomBlur, &g_bloomCombine};
    bool ok = true;
    for (int i = 0; i < 4; ++i)
        ok = ok && blobs[i] && SUCCEEDED(g_device->CreatePixelShader(blobs[i]->GetBufferPointer(), blobs[i]->GetBufferSize(), nullptr, shaders[i]));
    for (ID3DBlob* blob : blobs)
        if (blob) blob->Release();
    D3D11_BUFFER_DESC buffer = {};
    buffer.ByteWidth = sizeof(BloomConstants);
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return ok && SUCCEEDED(g_device->CreateBuffer(&buffer, nullptr, &g_bloomConstants));
}

void DropBloomStages() {
    for (BloomStage& stage : g_bloomStages)
        for (int i = 0; i < 2; ++i) {
            if (stage.view[i]) stage.view[i]->Release();
            if (stage.target[i]) stage.target[i]->Release();
            if (stage.texture[i]) stage.texture[i]->Release();
            stage.view[i] = nullptr;
            stage.target[i] = nullptr;
            stage.texture[i] = nullptr;
        }
    g_bloomForWidth = g_bloomForHeight = 0;
}

// The stages for a picture this size: a quarter of it (Guerrilla's w >> 2), then each half the one
// before, rounded (Guerrilla's (int)(size × 0.5 + 0.5)).
bool MakeBloomStages(UINT width, UINT height) {
    DropBloomStages();
    UINT w = width >> 2, h = height >> 2;
    for (int s = 0; s < 3; ++s) {
        if (s > 0) {
            w = static_cast<UINT>(w * 0.5f + 0.5f);
            h = static_cast<UINT>(h * 0.5f + 0.5f);
        }
        if (w == 0 || h == 0) return false;
        BloomStage& stage = g_bloomStages[s];
        stage.width = w;
        stage.height = h;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        for (int i = 0; i < 2; ++i)
            if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &stage.texture[i])) ||
                FAILED(g_device->CreateRenderTargetView(stage.texture[i], nullptr, &stage.target[i])) ||
                FAILED(g_device->CreateShaderResourceView(stage.texture[i], nullptr, &stage.view[i])))
                return false;
    }
    g_bloomForWidth = width;
    g_bloomForHeight = height;
    rfa::Log("Bloom: Guerrilla's, in stages of %u x %u, %u x %u and %u x %u", g_bloomStages[0].width, g_bloomStages[0].height,
             g_bloomStages[1].width, g_bloomStages[1].height, g_bloomStages[2].width, g_bloomStages[2].height);
    return true;
}

// A blur's 9 taps as Guerrilla lays them (rl_gaussian5x5_blur_kernel::init, then init_bilinear): 17
// weights e^(−x² ÷ 2σ²) for x from −8 to 8, made to add up to 1; the pairs (−8, −7) … (−2, −1) each
// folded into one tap between them (where a filtered sample weighs the two as they are), the middle
// kept, the other side the same; along one axis, a texel the stage's width or height.
void BloomTaps(float sigma, float texel, bool vertical, float (&taps)[9][4]) {
    float weights[17], sum = 0;
    for (int i = 0; i < 17; ++i) {
        const float x = static_cast<float>(i - 8);
        weights[i] = expf(-x * x / (2 * sigma * sigma));
        sum += weights[i];
    }
    float where[9], much[9];
    for (int k = 0; k < 4; ++k) {
        const float a = weights[2 * k] / sum, b = weights[2 * k + 1] / sum;
        much[k] = a + b;
        where[k] = ((2 * k - 8) * a + (2 * k - 7) * b) / (a + b);
    }
    where[4] = 0;
    much[4] = weights[8] / sum;
    for (int k = 0; k < 4; ++k) {
        where[5 + k] = -where[3 - k];
        much[5 + k] = much[3 - k];
    }
    for (int k = 0; k < 9; ++k) {
        taps[k][0] = vertical ? 0 : where[k] * texel;
        taps[k][1] = vertical ? where[k] * texel : 0;
        taps[k][2] = 0;
        taps[k][3] = much[k];
    }
}

// One of the bloom's passes: into a target of this size, from these pictures (t0-t4), with this shader.
bool BloomPass(ID3D11RenderTargetView* target, UINT width, UINT height, ID3D11ShaderResourceView* const (&from)[5], ID3D11PixelShader* shader,
               const BloomConstants& constants) {
    ID3D11ShaderResourceView* none[5] = {};
    g_context->PSSetShaderResources(0, 5, none);
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(g_context->Map(g_bloomConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
    memcpy(mapped.pData, &constants, sizeof(constants));
    g_context->Unmap(g_bloomConstants, 0);
    const D3D11_VIEWPORT viewport = {0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    g_context->OMSetRenderTargets(1, &target, nullptr);
    g_context->RSSetViewports(1, &viewport);
    g_context->PSSetShaderResources(0, 5, from);
    g_context->PSSetShader(shader, nullptr, 0);
    g_context->Draw(3, 0);
    return true;
}

// Blurs a stage in place: along its width into its second picture, then along its height back.
bool BlurStage(BloomStage& stage, float sigma, BloomConstants& constants) {
    constants.sizes[0] = 1.0f / stage.width;
    constants.sizes[1] = 1.0f / stage.height;
    BloomTaps(sigma, 1.0f / stage.width, false, constants.taps);
    ID3D11ShaderResourceView* across[5] = {stage.view[0]};
    if (!BloomPass(stage.target[1], stage.width, stage.height, across, g_bloomBlur, constants)) return false;
    BloomTaps(sigma, 1.0f / stage.height, true, constants.taps);
    ID3D11ShaderResourceView* down[5] = {stage.view[1]};
    return BloomPass(stage.target[0], stage.width, stage.height, down, g_bloomBlur, constants);
}

// Guerrilla's bloom of this frame's scene, after the eye has been measured: the picture to lay into
// the tone mapping (null: none made, Armageddon's stays). The caller has the tone mapping's vertex
// shader, sampler, states and constant buffer bound, and puts the constant buffer back after.
ID3D11ShaderResourceView* DrawGuerrillaBloom(ID3D11ShaderResourceView* scene, ID3D11ShaderResourceView* eye, ID3D11ShaderResourceView* particles,
                                             UINT width, UINT height) {
    if (g_bloomFailed) return nullptr;
    if (!g_bloomBright && !MakeBloomShaders()) {
        g_bloomFailed = true;
        rfa::Log("Bloom: could not make Guerrilla's bloom's shaders; Armageddon's stays");
        return nullptr;
    }
    if ((g_bloomForWidth != width || g_bloomForHeight != height) && !MakeBloomStages(width, height)) {
        DropBloomStages();
        g_bloomFailed = true;
        rfa::Log("Bloom: could not make Guerrilla's bloom's pictures for %u x %u; Armageddon's stays", width, height);
        return nullptr;
    }
    g_context->PSSetConstantBuffers(0, 1, &g_bloomConstants);
    BloomConstants constants = {};
    constants.bright[0] = TodNumber(kBloomExposure);
    constants.bright[1] = TodNumber(kEyeAdaptionBase);
    constants.bright[2] = TodNumber(kEyeAdaptionAmount);
    constants.curve[0] = TodNumber(kBloomTheta);
    constants.curve[1] = TodNumber(kBloomSlopeA);
    constants.curve[2] = TodNumber(kBloomSlopeB);
    constants.curve[3] = constants.curve[0] * constants.curve[1];
    constants.sizes[2] = 1.0f / width;
    constants.sizes[3] = 1.0f / height;
    BloomStage& first = g_bloomStages[0];
    BloomStage& second = g_bloomStages[1];
    BloomStage& third = g_bloomStages[2];
    // The bright pass, then the first stage blurred (σ 2); each further stage boxed down from the one
    // before and blurred (σ 3); then the three together, into the first stage's second picture.
    ID3D11ShaderResourceView* bright[5] = {scene, eye, particles};
    constants.sizes[0] = 1.0f / first.width;
    constants.sizes[1] = 1.0f / first.height;
    bool ok = BloomPass(first.target[0], first.width, first.height, bright, g_bloomBright, constants) && BlurStage(first, 2, constants);
    BloomStage* stages[3] = {&first, &second, &third};
    for (int s = 1; s < 3 && ok; ++s) {
        constants.sizes[0] = 1.0f / stages[s]->width;
        constants.sizes[1] = 1.0f / stages[s]->height;
        ID3D11ShaderResourceView* box[5] = {stages[s - 1]->view[0]};
        ok = BloomPass(stages[s]->target[0], stages[s]->width, stages[s]->height, box, g_bloomBox, constants) && BlurStage(*stages[s], 3, constants);
    }
    constants.sizes[0] = 1.0f / first.width;
    constants.sizes[1] = 1.0f / first.height;
    ID3D11ShaderResourceView* together[5] = {first.view[0], nullptr, nullptr, second.view[0], third.view[0]};
    ok = ok && BloomPass(first.target[1], first.width, first.height, together, g_bloomCombine, constants);
    ID3D11ShaderResourceView* none[5] = {};
    g_context->PSSetShaderResources(0, 5, none);
    return ok ? first.view[1] : nullptr;
}

void DrawToneMapping(ID3D11RenderTargetView* target, ID3D11ShaderResourceView* scene, ID3D11ShaderResourceView* bloom, UINT width, UINT height) {
    // The frame's time, for the eye (a new start after a gap of more than a second).
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    float seconds = g_toneLast.QuadPart ? static_cast<float>(now.QuadPart - g_toneLast.QuadPart) / frequency.QuadPart : 0;
    g_toneLast = now;
    if (seconds > 1) g_eyeFresh = true;
    if (seconds > 0.25f) seconds = 0.25f;

    ToneConstants constants = {};
    constants.adapt[0] = g_eyeFresh ? 1.0f : 1 - exp2f(-0.87439f * seconds * TodNumber(kIrisRate));
    constants.adapt[1] = TodNumber(kLuminanceMin);
    constants.adapt[2] = TodNumber(kLuminanceMax);
    if (constants.adapt[2] < constants.adapt[1]) constants.adapt[2] = constants.adapt[1];
    constants.exposure[0] = TodNumber(kLumRange);
    constants.exposure[1] = TodNumber(kLumOffset);
    constants.exposure[2] = TodNumber(kEyeAdaptionBase);
    constants.exposure[3] = TodNumber(kEyeAdaptionAmount);
    constants.picture[0] = g_exposure;
    constants.picture[1] = TodNumber(kBloomAmount) * g_bloom;
    constants.picture[2] = static_cast<float>(width);
    constants.picture[3] = static_cast<float>(height);
    float sunAt[2] = {};
    const bool shafts = SunShaftsOn() && g_shaftDepth && SunOnPicture(width, height, sunAt);
    constants.shafts[0] = shafts ? g_sunShafts : 0.0f;
    float tint[3];
    ColourCorrection(tint);
    for (int i = 0; i < 3; ++i) constants.tint[i] = tint[i];
    constants.tint[3] = g_colourCorrection ? TodNumber(kColourSaturation) : 1.0f;
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(g_context->Map(g_toneConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    memcpy(mapped.pData, &constants, sizeof(constants));
    g_context->Unmap(g_toneConstants, 0);
    g_eyeFresh = false;
    const int next = 1 - g_eyeNow;

    SavedState saved;
    saved.Save();
    ID3D11RenderTargetView* gameTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* gameDepthView = nullptr;
    g_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, gameTargets, &gameDepthView);
    ID3D11DepthStencilState* gameDepth = nullptr;
    UINT stencilRef = 0;
    g_context->OMGetDepthStencilState(&gameDepth, &stencilRef);
    D3D11_VIEWPORT gameViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    g_context->RSGetViewports(&viewportCount, gameViewports);

    const float noFactor[4] = {1, 1, 1, 1};
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->IASetInputLayout(nullptr);
    g_context->VSSetShader(g_toneVertex, nullptr, 0);
    g_context->GSSetShader(nullptr, nullptr, 0);
    g_context->PSSetConstantBuffers(0, 1, &g_toneConstants);
    g_context->PSSetSamplers(0, 1, &g_toneSampler);
    g_context->OMSetDepthStencilState(g_toneDepth, 0);
    g_context->RSSetState(g_toneRaster);

    // The eye: this frame's measure, and last frame's brightness eased toward it.
    const D3D11_VIEWPORT one = {0, 0, 1, 1, 0, 1};
    g_context->OMSetRenderTargets(1, &g_eyeTarget[next], nullptr);
    g_context->RSSetViewports(1, &one);
    ID3D11ShaderResourceView* eyeInputs[3] = {scene, nullptr, g_eyeView[g_eyeNow]};
    g_context->PSSetShaderResources(0, 3, eyeInputs);
    g_context->PSSetShader(g_adaptPixel, nullptr, 0);
    g_context->OMSetBlendState(g_eyeBlend, noFactor, 0xFFFFFFFF);
    g_context->Draw(3, 0);

    // Guerrilla's bloom of this scene, as its own compositor makes it once the eye is measured, in
    // place of Armageddon's (which stays if ours can't be made).
    if (ID3D11ShaderResourceView* ours = DrawGuerrillaBloom(scene, g_eyeView[next], g_particlesReady ? g_particleView : nullptr, width, height))
        bloom = ours;
    g_context->PSSetConstantBuffers(0, 1, &g_toneConstants);

    // The sun shafts (with none drawn the shader reads nothing there).
    ID3D11ShaderResourceView* sunShafts = nullptr;
    if (shafts) {
        ID3D11ShaderResourceView* eyeNone[3] = {};
        g_context->PSSetShaderResources(0, 3, eyeNone);
        sunShafts = DrawShafts(scene, width, height, sunAt);
        g_context->VSSetShader(g_toneVertex, nullptr, 0);
        g_context->PSSetConstantBuffers(0, 1, &g_toneConstants);
    }
    if (rfa::DetailLog() && SunShaftsOn() && g_shaftDepth) WatchShafts(sunShafts, sunAt);

    // The picture, over Armageddon's.
    const D3D11_VIEWPORT whole = {0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    g_context->OMSetRenderTargets(1, &target, nullptr);
    g_context->RSSetViewports(1, &whole);
    ID3D11ShaderResourceView* pictureInputs[5] = {scene, bloom, g_eyeView[next], sunShafts, g_particlesReady ? g_particleView : nullptr};
    g_context->PSSetShaderResources(0, 5, pictureInputs);
    g_particlesReady = false;
    g_context->PSSetShader(g_tonePixel, nullptr, 0);
    g_context->OMSetBlendState(g_toneBlend, noFactor, 0xFFFFFFFF);
    g_context->Draw(3, 0);

    ID3D11ShaderResourceView* none[5] = {};
    g_context->PSSetShaderResources(0, 5, none);
    g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, gameTargets, gameDepthView);
    g_context->RSSetViewports(viewportCount, gameViewports);
    g_context->OMSetDepthStencilState(gameDepth, stencilRef);
    saved.Restore();
    for (ID3D11RenderTargetView* gameTarget : gameTargets)
        if (gameTarget) gameTarget->Release();
    if (gameDepthView) gameDepthView->Release();
    if (gameDepth) gameDepth->Release();
    g_eyeNow = next;

    // The eye's brightness read back (a frame or two later, never waiting), for the particles' fog
    // colour (see ParticleFogColour), and every 10 seconds into the log.
    if (g_eyeReadPending) {
        D3D11_MAPPED_SUBRESOURCE eye;
        if (SUCCEEDED(g_context->Map(g_eyeReadback, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &eye))) {
            const float adapted = *static_cast<const float*>(eye.pData);
            g_context->Unmap(g_eyeReadback, 0);
            g_eyeReadPending = false;
            g_eyeBrightness = adapted;
            if (GetTickCount64() - g_lastEyeLog >= 10000) {
                g_lastEyeLog = GetTickCount64();
                float lit = 1 + constants.exposure[1] * (constants.exposure[2] + constants.exposure[3] * (adapted - constants.exposure[2]));
                if (lit < 0.1f) lit = 0.1f;
                static int told = 0;
                if (told++ < 60)
                    rfa::LogDetail("Tone mapping at %.2f h: the eye's brightness %.3f, exposure %.3f (range %.3g, offset %.3g, base %.3g, amount %.3g, "
                             "Exposure=%.3g)", g_timeOfDay, adapted, constants.exposure[0] / lit * g_exposure, constants.exposure[0],
                             constants.exposure[1], constants.exposure[2], constants.exposure[3], g_exposure);
            }
        }
    }
    if (!g_eyeReadPending) {
        g_context->CopyResource(g_eyeReadback, g_eye[g_eyeNow]);
        g_eyeReadPending = true;
    }
}

// The texture behind a view (false if none or not a 2D texture).
bool TextureOf(ID3D11View* view, D3D11_TEXTURE2D_DESC& desc) {
    desc = {};
    if (!view) return false;
    ID3D11Resource* resource = nullptr;
    view->GetResource(&resource);
    ID3D11Texture2D* texture = nullptr;
    const bool ok = resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)));
    if (texture) {
        texture->GetDesc(&desc);
        texture->Release();
    }
    if (resource) resource->Release();
    return ok;
}

// At the sky step: the G-buffer's depth copied for the sun shafts, which are drawn at the tone mapping at the frame's end
// (the copy made again when the depth's size or format changes). Once it can't be made, the shafts read the game's own.
void KeepShaftDepth() {
    if (!g_shaftDepth || !g_device || !g_context || g_shaftDepthCopyFailed) return;
    ID3D11Resource* resource = nullptr;
    g_shaftDepth->GetResource(&resource);
    ID3D11Texture2D* depth = nullptr;
    if (resource) {
        resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&depth));
        resource->Release();
    }
    if (!depth) return;
    D3D11_TEXTURE2D_DESC desc;
    depth->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC have = {};
    if (g_shaftDepthCopy) g_shaftDepthCopy->GetDesc(&have);
    if (!g_shaftDepthCopy || have.Width != desc.Width || have.Height != desc.Height || have.Format != desc.Format ||
        have.MipLevels != desc.MipLevels || have.ArraySize != desc.ArraySize || have.SampleDesc.Count != desc.SampleDesc.Count) {
        if (g_shaftDepthCopyView) g_shaftDepthCopyView->Release();
        if (g_shaftDepthCopy) g_shaftDepthCopy->Release();
        g_shaftDepthCopyView = nullptr;
        g_shaftDepthCopy = nullptr;
        D3D11_TEXTURE2D_DESC copy = desc;
        copy.Usage = D3D11_USAGE_DEFAULT;
        copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        copy.CPUAccessFlags = 0;
        copy.MiscFlags = 0;
        D3D11_SHADER_RESOURCE_VIEW_DESC view;
        g_shaftDepth->GetDesc(&view);
        const bool made = SUCCEEDED(g_device->CreateTexture2D(&copy, nullptr, &g_shaftDepthCopy)) &&
                          SUCCEEDED(g_device->CreateShaderResourceView(g_shaftDepthCopy, &view, &g_shaftDepthCopyView));
        if (!made) {
            if (g_shaftDepthCopyView) g_shaftDepthCopyView->Release();
            if (g_shaftDepthCopy) g_shaftDepthCopy->Release();
            g_shaftDepthCopyView = nullptr;
            g_shaftDepthCopy = nullptr;
            g_shaftDepthCopyFailed = true;
            depth->Release();
            rfa::Log("Sun shafts: could not keep a copy of the depth (%u x %u, format %d); they read the game's own at the frame's end",
                     desc.Width, desc.Height, static_cast<int>(desc.Format));
            return;
        }
        rfa::Log("Sun shafts: the depth kept from the sky step for them (%u x %u, format %d)", desc.Width, desc.Height,
                 static_cast<int>(desc.Format));
    }
    g_context->CopyResource(g_shaftDepthCopy, depth);
    depth->Release();
    g_shaftDepthKept = true;
}

// Right after Armageddon's last composite step in our worlds: its target and the scene (t0) and bloom
// (t5) it drew from, still bound, then Guerrilla's tone mapping over its picture.
void ToneMapAfterComposite() {
    if (!GuerrillaToneMapping() || !g_context || !g_device) return;
    ID3D11RenderTargetView* target = nullptr;
    g_context->OMGetRenderTargets(1, &target, nullptr);
    ID3D11ShaderResourceView* scene = nullptr;
    ID3D11ShaderResourceView* bloom = nullptr;
    g_context->PSGetShaderResources(0, 1, &scene);
    g_context->PSGetShaderResources(5, 1, &bloom);
    D3D11_TEXTURE2D_DESC targetDesc, sceneDesc, bloomDesc;
    if (TextureOf(target, targetDesc) && TextureOf(scene, sceneDesc) && TextureOf(bloom, bloomDesc) &&
        sceneDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT && sceneDesc.Width == targetDesc.Width && sceneDesc.Height == targetDesc.Height) {
        if (!g_toneVertex && !MakeToneMappingObjects()) {
            g_toneMappingFailed = true;
        } else {
            RFA_LOG_FIRST(1, "Tone mapping: Guerrilla's, over the final picture in world \"%s\" (%u x %u)", WorldName(), targetDesc.Width,
                          targetDesc.Height);
            DrawToneMapping(target, scene, bloom, targetDesc.Width, targetDesc.Height);
        }
    } else {
        RFA_LOG_FIRST(1, "Tone mapping: the last composite step left no picture, scene and bloom of the kinds expected; Armageddon's own stays");
    }
    for (ID3D11View* view : {static_cast<ID3D11View*>(target), static_cast<ID3D11View*>(scene), static_cast<ID3D11View*>(bloom)})
        if (view) view->Release();
    if (g_shaftDepth) g_shaftDepth->Release();
    g_shaftDepth = nullptr;
    g_shaftDepthKept = false;
}

// During the light pass in our worlds, the first time its check runs (all is bound by then), the
// mod notes the light buffer it draws into and the G-buffer's normals (and, for the fog, its depth and
// the game's constants). It adds the sky's light (and the sun's, if not drawn through its lamp) on the
// check the pass makes after its lamps and right before it applies the ambient occlusion to the light
// buffer (the call at 0xEF9136, back to 0xEF913B; the apply 0xC2FF50 at 0xEF9142), so that, as in
// Guerrilla (whose surfaces weigh the sky's light by the occlusion), corners, crevices and insides are
// darker; or after the pass, if that check never came.
bool g_inLightPass = false;
bool g_sunDrawn = false;       // the sun's light drawn in this light pass (through its shadow map, or with the sky's)
bool g_skyLightDrawn = false;  // the sky's light drawn into this light pass's buffer
ID3D11RenderTargetView* g_lightBuffer = nullptr;
ID3D11ShaderResourceView* g_lightNormals = nullptr;
constexpr DWORD kCheckBeforeOcclusion = 0xEF913B;

bool __cdecl LightCheck() {
    const DWORD returnTo = reinterpret_cast<DWORD>(_ReturnAddress());
    if (g_inLightPass && !g_lightBuffer) {
        g_context->OMGetRenderTargets(1, &g_lightBuffer, nullptr);
        g_context->PSGetShaderResources(13, 1, &g_lightNormals);
    }
    if (g_noteFog && !g_fogDepth) {
        g_context->PSGetShaderResources(14, 1, &g_fogDepth);
        g_context->PSGetConstantBuffers(4, 1, &g_gameConstants);
        CopyGameConstants(g_gameConstants);
    }
    if (g_inLightPass && !g_skyLightDrawn && returnTo == kCheckBeforeOcclusion && g_lightBuffer && g_lightNormals) {
        RFA_LOG_FIRST(1, "Light: the sky's light drawn before the ambient occlusion in world \"%s\"", WorldName());
        DrawLight(g_lightBuffer, g_lightNormals, true, !g_sunDrawn, nullptr);
        g_skyLightDrawn = true;
        g_sunDrawn = true;
        CopyLightBefore(g_lightBuffer);  // the lamps' light and ours, before the occlusion
    }
    return g_lightCheck();
}

// The light pass draws each lamp with 0xEF67D0 (the scene renderer in ECX; the lamp's entry in the
// frame's list of lamps, its light at +0x10, and the entry whose shadow map was just rendered into the
// shared map): the shadowed spot lamp's shader
// (ir_light_spot) then has the shadow map at t0 and the lamp's constants (Shadow_map_projTM, ...) at
// b0, the G-buffer's normals and depth at t13 and t14, the game's constants at b4. After the sun's lamp
// is drawn, while they are all still bound, the mod draws the sun's light through that shadow map.
constexpr DWORD kDrawLamp = 0xEF67D0;
constexpr size_t kDrawLampPrologue = 6;  // push ebx / mov ebx,esp / sub esp,8
using DrawLampFn = void(__fastcall*)(void* renderer, void* unused, BYTE* entry, BYTE* shadowEntry);
DrawLampFn g_drawLamp;
constexpr size_t kEntryLight = 0x10;

// Once: what the sun's lamp was drawn with (its shadow map, and its constants read back from the card).
void TellSunLamp(ID3D11ShaderResourceView* map, ID3D11Buffer* lamp) {
    ID3D11Resource* resource = nullptr;
    map->GetResource(&resource);
    ID3D11Texture2D* texture = nullptr;
    D3D11_TEXTURE2D_DESC desc = {};
    if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)))) {
        texture->GetDesc(&desc);
        texture->Release();
    }
    if (resource) resource->Release();
    D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
    map->GetDesc(&view);
    rfa::Log("Sun's shadows: the sun's lamp's shadow map %u x %u, format %d (viewed as %d)", desc.Width, desc.Height,
             static_cast<int>(desc.Format), static_cast<int>(view.Format));
    D3D11_BUFFER_DESC bufferDesc = {};
    lamp->GetDesc(&bufferDesc);
    D3D11_BUFFER_DESC stagingDesc = {};
    stagingDesc.ByteWidth = bufferDesc.ByteWidth;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* staging = nullptr;
    if (FAILED(g_device->CreateBuffer(&stagingDesc, nullptr, &staging))) return;
    g_context->CopyResource(staging, lamp);
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        const float* f = static_cast<const float*>(mapped.pData);
        const char* names[11] = {"light position", "inverse projection", "", "", "", "spot direction",
                                 "shadow map projection", "", "", "", "shadow fade"};
        for (UINT row = 0; row < 11 && row * 16 + 16 <= bufferDesc.ByteWidth; ++row)
            rfa::LogDetail("Sun's shadows: the lamp's constants [%u] %g %g %g %g %s", row, f[row * 4], f[row * 4 + 1], f[row * 4 + 2], f[row * 4 + 3], names[row]);
        g_context->Unmap(staging, 0);
    }
    staging->Release();
}

// Draws the sun's light through the sun's lamp's shadow map with what the game has bound for the lamp
// (its map at t0 and constants at b0, the G-buffer's normals and depth at t13 and t14, the game's
// constants at b4, the light buffer as the target); false, and the sun left to the pass's end, when
// any of them isn't bound (logged once, with which).
bool DrawSunThroughLamp() {
    ID3D11RenderTargetView* buffer = nullptr;
    g_context->OMGetRenderTargets(1, &buffer, nullptr);
    ID3D11ShaderResourceView* map = nullptr;
    ID3D11ShaderResourceView* normals = nullptr;
    ID3D11ShaderResourceView* depth = nullptr;
    g_context->PSGetShaderResources(0, 1, &map);
    g_context->PSGetShaderResources(13, 1, &normals);
    g_context->PSGetShaderResources(14, 1, &depth);
    ID3D11Buffer* lamp = nullptr;
    ID3D11Buffer* game = nullptr;
    g_context->PSGetConstantBuffers(0, 1, &lamp);
    g_context->PSGetConstantBuffers(4, 1, &game);
    const bool all = buffer && map && normals && depth && lamp && game;
    // For the fog: the depth and the game's constants as the lamp was lit with them (the light pass's
    // first check may come while other textures and values are still bound).
    if (g_noteFog && depth && game) {
        if (g_fogDepth != depth) {
            RFA_LOG_FIRST(1, "Fog: the light pass's first check had another texture at t14 than the sun's lamp; the fog takes the lamp's");
            if (g_fogDepth) g_fogDepth->Release();
            g_fogDepth = depth;
            g_fogDepth->AddRef();
        }
        CopyGameConstants(game);
    }
    if (all) {
        static bool told = false;
        if (!told) {
            told = true;
            TellSunLamp(map, lamp);
            rfa::Log("Sun's shadows: the sun's light drawn through the follow player light's shadow map in world \"%s\"", WorldName());
        }
        const SunShadowInputs shadow = {map, depth, game, lamp};
        DrawLight(buffer, normals, false, true, &shadow);
        g_sunDrawn = true;
    } else {
        RFA_LOG_FIRST(1, "Sun's shadows: the follow player light drawn without everything bound (the light buffer %d, its shadow map %d, "
                         "normals %d, depth %d, its constants %d, the game's %d; this is logged once); the sun lights without shadows",
                      buffer != nullptr, map != nullptr, normals != nullptr, depth != nullptr, lamp != nullptr, game != nullptr);
    }
    for (IUnknown* held : {static_cast<IUnknown*>(buffer), static_cast<IUnknown*>(map), static_cast<IUnknown*>(normals),
                           static_cast<IUnknown*>(depth), static_cast<IUnknown*>(lamp), static_cast<IUnknown*>(game)})
        if (held) held->Release();
    return all;
}

// Sets a texture slot of the renderer's draw state (0xC2F490: the state in ECX, the slot, the texture,
// or null to clear it). The lamp's drawing clears its shadow map's slot (0xEF73ED) right after it has
// drawn the lamp (0xEF72CE), the shared map being drawn into next for the next lamp. While the sun's
// lamp is drawn, the mod draws the sun's light just before the first clearing, everything still bound.
constexpr DWORD kSetTexture = 0xC2F490;
constexpr size_t kSetTexturePrologue = 7;  // push ebp / mov ebp,esp / push ecx / mov [ebp-4],ecx
using SetTextureFn = void(__fastcall*)(void* state, void* unused, int slot, void* texture);
SetTextureFn g_setTexture;
bool g_drawingSunLamp = false;  // the sun's lamp being drawn, its light not yet drawn through its map

void __fastcall SetTexture(void* state, void* unused, int slot, void* texture) {
    if (g_drawingSunLamp && !texture) {
        g_drawingSunLamp = false;
        DrawSunThroughLamp();
    }
    g_setTexture(state, unused, slot, texture);
}

void __fastcall DrawLamp(void* renderer, void* unused, BYTE* entry, BYTE* shadowEntry) {
    const bool sunLamp = g_inLightPass && entry && g_sunLight && *reinterpret_cast<BYTE**>(entry + kEntryLight) == g_sunLight &&
                         !g_sunDrawn && g_lightVertex && g_setTexture;
    g_drawingSunLamp = sunLamp;
    g_drawLamp(renderer, unused, entry, shadowEntry);
    if (g_drawingSunLamp) {  // the lamp drawn without clearing a texture: try once more after it
        g_drawingSunLamp = false;
        DrawSunThroughLamp();
    }
}

void __fastcall LightPass(void* renderer, void* unused) {
    NoteRenderWarnings();
    const bool ours = g_context && InOurWorld();
    g_inLightPass = ours && g_lightOn && !g_lightFailed;
    if (g_inLightPass && !g_lightVertex && !MakeLightObjects()) {
        g_lightFailed = true;
        g_inLightPass = false;
    }
    g_sunDrawn = false;
    g_skyLightDrawn = false;
    g_ourLightCleared = false;
    g_beforeCopied = false;
    g_surfaceInputsMade = false;
    DropFogInputs();
    if (g_shaftDepth) g_shaftDepth->Release();
    g_shaftDepth = nullptr;
    g_shaftDepthKept = false;
    g_noteFog = ours && ((g_fog > 0 && !g_fogFailed) || SunShaftsOn() || CoronaWanted());
    g_lightPass(renderer, unused);
    g_noteFog = false;
    if (g_inLightPass) {
        g_inLightPass = false;
        if (g_skyLightDrawn) {
            // drawn before the occlusion (LightCheck)
        } else if (g_lightBuffer && g_lightNormals) {
            RFA_LOG_FIRST(1, "Light: lighting world \"%s\" with the sky's light and the sun after the pass (no ambient occlusion on it)", WorldName());
            DrawLight(g_lightBuffer, g_lightNormals, true, !g_sunDrawn, nullptr);
            CopyLightBefore(g_lightBuffer);  // (no occlusion: the same)
        } else {
            RFA_LOG_FIRST(1, "Light: a light pass showed no light buffer or normals (this is logged once); no light added to it");
        }
        g_surfaceInputsMade = g_ourLightCleared && g_beforeCopied && FillSurfaceConstants();
        if (g_surfaceNormals) g_surfaceNormals->Release();
        g_surfaceNormals = g_surfaceInputsMade ? g_lightNormals : nullptr;
        if (g_surfaceNormals) g_surfaceNormals->AddRef();
    }
    if (g_lightBuffer) g_lightBuffer->Release();
    if (g_lightNormals) g_lightNormals->Release();
    g_lightBuffer = nullptr;
    g_lightNormals = nullptr;
}

// The scene renderer's material pass (0xEF9290, right after the light pass: each surface drawn again
// with its colours, lit by the light buffer). In our worlds, while it runs, Guerrilla's material
// shaders are given what the light pass made for them (SurfaceConstants), and the G-buffer's normals
// (t18, for the terrain's), unbound after it, as the next light pass draws into those textures. The
// constants (b8) stay bound after it: the next frame's depth and G-buffer passes of Guerrilla's
// cut-outs read their time from them, so a scrolling cut-out's holes move with its colours (no shader
// of the game's reads b8). A frame whose light pass made nothing for them unbinds b8 first.
constexpr DWORD kMaterialPass = 0xEF9290;
constexpr size_t kMaterialPassPrologue = 5;  // push ebp / mov ebp,esp / push -1
using MaterialPassFn = void(__fastcall*)(void* renderer, void* unused);
MaterialPassFn g_materialPass;

void __fastcall MaterialPass(void* renderer, void* unused) {
    const bool give = g_surfaceInputsMade && g_context && g_ourLightView && g_beforeView && g_surfaceConstants;
    if (give) {
        RFA_LOG_FIRST(1, "Guerrilla's material shaders: given the mod's light in world \"%s\"", WorldName());
        ID3D11ShaderResourceView* views[3] = {g_ourLightView, g_beforeView, g_surfaceNormals};
        g_context->PSSetShaderResources(16, 3, views);
        g_context->PSSetConstantBuffers(8, 1, &g_surfaceConstants);
    } else if (g_context) {
        ID3D11Buffer* noBuffer = nullptr;
        g_context->PSSetConstantBuffers(8, 1, &noBuffer);
    }
    g_materialPass(renderer, unused);
    if (give) {
        ID3D11ShaderResourceView* none[3] = {nullptr, nullptr, nullptr};
        g_context->PSSetShaderResources(16, 3, none);
    }
    if (g_surfaceNormals) g_surfaceNormals->Release();
    g_surfaceNormals = nullptr;
    g_surfaceInputsMade = false;
}

// Replaces the scene renderer's sky step: the game's own, then, in our worlds, the clock, Guerrilla's
// fog made ready (drawn over what the world has drawn as the particles begin) and Guerrilla's sky behind it.
void __fastcall RenderSkybox(void* renderer, void* unused) {
    g_renderSkybox(renderer, unused);
    RFA_LOG_FIRST(1, "Sky: the game's sky step runs (world \"%s\")", WorldName());
    const bool ours = InOurWorld();
    if (ours) {
        CheckSettings();
        TellAmbient();
    }
    RunClock(ours);
    RunStatsBlock(ours);
    RunStreaming(ours);
    StreamWatchUpdate(ours);
    RunGroundDetail(ours);
    if (ours && g_fog > 0 && !g_fogFailed && g_context) {
        if (!g_fogDepth || !g_gameConstants) {
            RFA_LOG_FIRST(1, "Fog: the light pass showed no depth or constants (this is logged once); no fog");
        } else if (!g_fogVertex && !MakeFogObjects()) {
            g_fogFailed = true;
        } else {
            if (g_fogAtParticles) {
                g_fogPending = true;  // drawn as the particles begin (see ParticlePhase)
            } else {
                RFA_LOG_FIRST(1, "Fog: drawing Guerrilla's fog in world \"%s\" at the sky step (the particle phase isn't hooked)", WorldName());
                DrawFog();
            }
        }
    }
    // The depth stays for the sun's corona, drawn with the particles, and the sun shafts, drawn with
    // the tone mapping at the end of the frame.
    if (ours && (SunShaftsOn() || CoronaWanted()) && g_fogDepth && !g_shaftDepth) {
        g_shaftDepth = g_fogDepth;
        g_shaftDepth->AddRef();
    }
    if (ours && SunShaftsOn() && g_shaftDepth && !g_shaftDepthKept) KeepShaftDepth();
    if (!g_fogPending) DropFogInputs();
    if (!ours || !g_skyOn || g_drawingFailed || !g_context) return;
    if (!g_skyVertex) {
        if (!MakeSkyObjects()) {
            g_drawingFailed = true;
            return;
        }
        if (g_cloudsOn) MakeCloudObjects();
    }
    RFA_LOG_FIRST(1, "Sky: drawing Guerrilla's sky in world \"%s\"", WorldName());
    DrawSky();
}

// Replaces the compositor's tone-mapping constants: in our worlds with Guerrilla's tone mapping, the
// exposure range the game uploads is held at 1 for the call (so the picture holds Guerrilla's own
// values), and the renderer's own put back after.
void __fastcall ToneMappingConstants(void* renderer, void* unused) {
    float* exposure = reinterpret_cast<float*>(static_cast<BYTE*>(renderer) + 0xAD8);  // desired brightness, -, min, max
    const bool ours = InOurWorld();
    if (ours)
        RFA_LOG_FIRST(1, "Exposure: the game's own in world \"%s\": desired brightness %.3g, held between %.3g and %.3g", WorldName(),
                      exposure[0], exposure[2], exposure[3]);
    if (!ours || !GuerrillaToneMapping()) {
        g_toneMappingConstants(renderer, unused);
        return;
    }
    const float before[2] = {exposure[2], exposure[3]};
    exposure[2] = exposure[3] = 1;
    g_toneMappingConstants(renderer, unused);
    exposure[2] = before[0];
    exposure[3] = before[1];
}

// A view's texture, for the log: its format, size and mip levels.
void DescribeView(ID3D11View* view, char* out, size_t size) {
    ID3D11Resource* resource = nullptr;
    view->GetResource(&resource);
    ID3D11Texture2D* texture = nullptr;
    D3D11_TEXTURE2D_DESC desc = {};
    if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)))) {
        texture->GetDesc(&desc);
        texture->Release();
    }
    if (resource) resource->Release();
    sprintf_s(out, size, "%s (format %d), %u x %u, %u mip levels", FormatName(desc.Format), static_cast<int>(desc.Format), desc.Width,
              desc.Height, desc.MipLevels);
}

// Logs, once, what the compositor's last step drew into and what it left bound to draw from (the
// picture at t0, the bloom at t5, ...), to place Guerrilla's own tone mapping there.
void TellComposite(int which) {
    static bool told = false;
    if (told || !g_context) return;
    told = true;
    char text[160];
    ID3D11RenderTargetView* target = nullptr;
    g_context->OMGetRenderTargets(1, &target, nullptr);
    if (target) {
        DescribeView(target, text, sizeof(text));
        target->Release();
    } else {
        sprintf_s(text, "none");
    }
    rfa::LogDetail("Composite: the last step (0x%X) drew into %s", kFinalComposite[which], text);
    ID3D11ShaderResourceView* views[8] = {};
    g_context->PSGetShaderResources(0, 8, views);
    for (int slot = 0; slot < 8; ++slot) {
        if (!views[slot]) continue;
        DescribeView(views[slot], text, sizeof(text));
        rfa::LogDetail("Composite: t%d left bound: %s", slot, text);
        views[slot]->Release();
    }
}

// ---- Sharpening

// Contrast-adaptive sharpening (AMD's FidelityFX CAS, as its paper describes it), over the final
// picture in our worlds, right after the last composite step (and Guerrilla's tone mapping), before
// the HUD and menus are drawn over it, so their text isn't sharpened. Each pixel is pushed away from
// the average of the four next to it, by less where the nine around it already span much of the
// range (an edge, which would ring) and by more where they are close (soft detail): the amount is
// sqrt(min(darkest, 2 - brightest) / brightest), each of those over the cross of five plus the
// corners, times -1 / lerp(8, 5, Sharpen). The picture is copied first, as the pass draws over it.
const char kSharpenShader[] = R"(
Texture2D Picture : register(t0);
cbuffer Sharpen : register(b0) { float4 Params; };  // x: the negative peak weight

float4 SharpenVertex(uint id : SV_VertexID) : SV_Position {
    float2 at = float2((id << 1) & 2, id & 2);
    return float4(at * float2(2, -2) + float2(-1, 1), 0, 1);
}

float3 At(int2 p, int2 size) { return Picture.Load(int3(clamp(p, int2(0, 0), size - 1), 0)).rgb; }

float4 SharpenPixel(float4 position : SV_Position) : SV_Target {
    uint width, height;
    Picture.GetDimensions(width, height);
    int2 size = int2(width, height), p = int2(position.xy);
    float3 a = At(p + int2(-1, -1), size), b = At(p + int2(0, -1), size), c = At(p + int2(1, -1), size);
    float3 d = At(p + int2(-1, 0), size), e = At(p, size), f = At(p + int2(1, 0), size);
    float3 g = At(p + int2(-1, 1), size), h = At(p + int2(0, 1), size), i = At(p + int2(1, 1), size);
    float3 low = min(min(min(d, e), min(f, b)), h);
    low += min(low, min(min(a, c), min(g, i)));
    float3 high = max(max(max(d, e), max(f, b)), h);
    high += max(high, max(max(a, c), max(g, i)));
    float3 amount = sqrt(saturate(min(low, 2.0 - high) / max(high, 1e-5)));
    float3 weight = amount * Params.x;
    return float4(saturate(((b + d + f + h) * weight + e) / (1.0 + 4.0 * weight)), 1);
}
)";

ID3D11VertexShader* g_sharpenVertex = nullptr;
ID3D11PixelShader* g_sharpenPixel = nullptr;
ID3D11Buffer* g_sharpenConstants = nullptr;
ID3D11BlendState* g_sharpenBlend = nullptr;
ID3D11Texture2D* g_sharpenCopy = nullptr;  // the picture, copied to be read from
ID3D11ShaderResourceView* g_sharpenView = nullptr;
D3D11_TEXTURE2D_DESC g_sharpenCopyDesc = {};

bool MakeSharpenObjects() {
    if (!LoadCompiler()) return false;
    ID3DBlob* vertex = Compile(kSharpenShader, sizeof(kSharpenShader) - 1, "SharpenVertex", "vs_4_0");
    ID3DBlob* pixel = Compile(kSharpenShader, sizeof(kSharpenShader) - 1, "SharpenPixel", "ps_4_0");
    bool ok = vertex && pixel &&
              SUCCEEDED(g_device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &g_sharpenVertex)) &&
              SUCCEEDED(g_device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &g_sharpenPixel));
    for (ID3DBlob* blob : {vertex, pixel})
        if (blob) blob->Release();
    D3D11_BUFFER_DESC buffer = {};
    buffer.ByteWidth = 16;
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(g_device->CreateBuffer(&buffer, nullptr, &g_sharpenConstants));
    D3D11_BLEND_DESC blend = {};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    ok = ok && SUCCEEDED(g_device->CreateBlendState(&blend, &g_sharpenBlend));
    if (!ok) rfa::Log("Could not make the sharpening's shaders or states; no sharpening");
    return ok;
}

// A copy of `picture` to read from, made again if its size or format changes.
bool CopyForSharpening(ID3D11Texture2D* picture, const D3D11_TEXTURE2D_DESC& desc) {
    if (!g_sharpenCopy || g_sharpenCopyDesc.Width != desc.Width || g_sharpenCopyDesc.Height != desc.Height ||
        g_sharpenCopyDesc.Format != desc.Format) {
        if (g_sharpenView) g_sharpenView->Release();
        if (g_sharpenCopy) g_sharpenCopy->Release();
        g_sharpenView = nullptr;
        g_sharpenCopy = nullptr;
        D3D11_TEXTURE2D_DESC copy = desc;
        copy.MipLevels = copy.ArraySize = 1;
        copy.Usage = D3D11_USAGE_DEFAULT;
        copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        copy.CPUAccessFlags = copy.MiscFlags = 0;
        if (FAILED(g_device->CreateTexture2D(&copy, nullptr, &g_sharpenCopy)) ||
            FAILED(g_device->CreateShaderResourceView(g_sharpenCopy, nullptr, &g_sharpenView)))
            return false;
        g_sharpenCopyDesc = copy;
        rfa::Log("Sharpening: the final picture %u x %u (format %d), Sharpen=%d", desc.Width, desc.Height, static_cast<int>(desc.Format),
                 static_cast<int>(g_sharpen * 100 + 0.5f));
    }
    g_context->CopyResource(g_sharpenCopy, picture);
    return true;
}

void SharpenAfterComposite() {
    if (g_sharpen <= 0 || g_sharpenFailed || !g_context || !g_device) return;
    ID3D11RenderTargetView* target = nullptr;
    g_context->OMGetRenderTargets(1, &target, nullptr);
    if (!target) return;
    ID3D11Resource* resource = nullptr;
    target->GetResource(&resource);
    ID3D11Texture2D* picture = nullptr;
    D3D11_TEXTURE2D_DESC desc = {};
    if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&picture)))) picture->GetDesc(&desc);
    if (picture && desc.SampleDesc.Count == 1 && desc.ArraySize == 1 && desc.MipLevels == 1) {
        if (!g_sharpenVertex && !MakeSharpenObjects()) {
            g_sharpenFailed = true;
        } else if (CopyForSharpening(picture, desc)) {
            const float peak = -1.0f / (8.0f + (5.0f - 8.0f) * g_sharpen);
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(g_context->Map(g_sharpenConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                const float params[4] = {peak, 0, 0, 0};
                memcpy(mapped.pData, params, sizeof(params));
                g_context->Unmap(g_sharpenConstants, 0);
                SavedState saved;
                saved.Save();
                ID3D11DepthStencilState* gameDepth = nullptr;
                UINT stencilRef = 0;
                g_context->OMGetDepthStencilState(&gameDepth, &stencilRef);
                D3D11_VIEWPORT gameViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
                UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
                g_context->RSGetViewports(&viewportCount, gameViewports);
                const float noFactor[4] = {1, 1, 1, 1};
                const D3D11_VIEWPORT whole = {0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0, 1};
                g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                g_context->IASetInputLayout(nullptr);
                g_context->VSSetShader(g_sharpenVertex, nullptr, 0);
                g_context->GSSetShader(nullptr, nullptr, 0);
                g_context->PSSetShader(g_sharpenPixel, nullptr, 0);
                g_context->PSSetConstantBuffers(0, 1, &g_sharpenConstants);
                g_context->PSSetShaderResources(0, 1, &g_sharpenView);
                if (g_toneDepth) g_context->OMSetDepthStencilState(g_toneDepth, 0);
                if (g_toneRaster) g_context->RSSetState(g_toneRaster);
                g_context->OMSetBlendState(g_sharpenBlend, noFactor, 0xFFFFFFFF);
                g_context->OMSetRenderTargets(1, &target, nullptr);
                g_context->RSSetViewports(1, &whole);
                g_context->Draw(3, 0);
                ID3D11ShaderResourceView* none = nullptr;
                g_context->PSSetShaderResources(0, 1, &none);
                g_context->RSSetViewports(viewportCount, gameViewports);
                g_context->OMSetDepthStencilState(gameDepth, stencilRef);
                saved.Restore();
                if (gameDepth) gameDepth->Release();
            }
        }
    }
    if (picture) picture->Release();
    if (resource) resource->Release();
    target->Release();
}

void __fastcall FinalComposite0(void* compositor, void* unused, int flag) {
    g_finalComposite[0](compositor, unused, flag);
    if (!InOurWorld()) return;
    NoteRenderWarnings();
    TellComposite(0);
    ToneMapAfterComposite();
    SharpenAfterComposite();
}

void __fastcall FinalComposite1(void* compositor, void* unused, int flag) {
    g_finalComposite[1](compositor, unused, flag);
    if (!InOurWorld()) return;
    NoteRenderWarnings();
    TellComposite(1);
    ToneMapAfterComposite();
    SharpenAfterComposite();
}

// ---- The game's device

using CreateDeviceFn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
                                        ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
CreateDeviceFn g_createDevice;

// Notes the device the game makes (the last one, if it makes several), and keeps it alive.
// ---- The sun's corona
// Guerrilla's sun has a corona: its time of day gives the Sun orbital an effect (env_sun_corona_a),
// played at each level's start and kept one metre from the camera toward the sun; its five coronas
// (the sun's rays, a wide glow, a halo and two lens flares) are pictures laid over the screen round
// the sun, fading as it is hidden or sets. Armageddon reads an orbital's effect but never plays it,
// so the mod draws them as Guerrilla's corona shader (rl_corona) does, from corona.bin (each corona's
// numbers and texture, which the world builder writes from Guerrilla's effect): at the end of the
// particle phase, into the particle layer (Guerrilla draws coronas with its particles; without the
// layer, into the scene as the game's particles are), each one's colour added (its additive key is 1).
//
// How much of each is seen, as Guerrilla measures it with two occlusion queries a frame: of a square
// facing the camera round the sun, as wide as the corona's visibility radius one metre away, the share
// of its pixels on the screen where nothing was drawn (the depth still at the far plane), eased over
// the frames (while between 5% and 99%: by 0.6 × the gap × the gap each frame; else taken at once).
// The mod samples the light pass's depth over the square (8 × 8 places) into a small texture, reading
// last frame's back on the graphics card for the easing.
//
// Each corona, as rl_corona's vertex and pixel shaders work it out (the corona one metre away):
// - shown: the visibility × its fade as the sun sets (its height ÷ 0.02, 0 to 1), less the share of the
//   square off the screen, eased (smoothstep); its opacity × shown^(opacity visibility decay), its size
//   × shown^(radius visibility decay);
// - its size and colour between their start and end values by the square root of how far it is
//   between its distances (here 1 m of 0-100: a tenth of the way);
// - a square on the screen round the sun, its size in screen units (the width × its width scale; the
//   height × the screen's width ÷ height), turned by its rotation offset and 2π(½ + ½ × its rotation
//   scale × how far the sun is to the side), and moved along that turn by its position offset;
// - its brightness 1 + intensity scale × (1 − smoothstep(how far the sun is to the side))^decay;
// - its texture's colour, its saturation applied (from its grey 0.3/0.59/0.11), times its colour and
//   Guerrilla's particle tint (1.6, as its particles are drawn with), the result premultiplied by its
//   alpha and held to 0-1.
// (Not drawn: Guerrilla's view fade, off in all five; its fog, nothing one metre away; coronas in the
// world rather than on the screen, which the sun's has none of.)
constexpr char kCoronaShader[] = R"(
cbuffer Corona : register(b0)
{
    float4 Right;        // the camera's right (xyz), the tan of half its field of view across (w)
    float4 Up;           // its up, the tan of half its field of view up
    float4 Forward;      // its forward
    float4 Sun;          // toward the sun (xyz), its fade as it sets (w)
    float4 Screen;       // x the picture's width ÷ height, y this corona's place in the visibility texture
    float4 ColourStart;
    float4 ColourEnd;
    float4 Radius;       // its size at the start and end, and the distances it goes between
    float4 Shade;        // the distances its colour goes between, its opacity's and size's visibility decays
    float4 Shape;        // its width scale, visibility radius, position offset and saturation
    float4 Turn;         // its rotation scale, rotation offset (degrees), intensity scale and intensity decay
    float4 Tint;         // Guerrilla's particle tint (rgb times SunCorona=)
    float4 Radii[4];     // every corona's visibility radius, four a vector (the visibility pass)
    float4 Depth;        // the depth's width and height (xy), its value at the far plane (z)
};

Texture2D DepthMap : register(t0);  // SeenPixel: the light pass's depth
Texture2D Before : register(t1);    // SeenPixel: last frame's visibility
Texture2D Picture : register(t2);   // CoronaPixel: the corona's texture
Texture2D Seen : register(t3);      // CoronaVertex: this frame's visibility
SamplerState Smooth : register(s0);

float2 OnScreen(float3 look)
{
    float depth = dot(look, Forward.xyz);
    return float2(dot(look, Right.xyz) / Right.w, dot(look, Up.xyz) / Up.w) / depth;
}

float4 SeenVertex(uint id : SV_VertexID) : SV_Position
{
    float2 corner = float2((id << 1) & 2, id & 2);
    return float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 SeenPixel(float4 position : SV_Position) : SV_Target
{
    uint i = (uint)position.x;
    float half = 0.5 * Radii[i / 4][i % 4];
    float seen = 0, all = 0;
    if (dot(Sun.xyz, Forward.xyz) > 0)
    {
        float2 low = OnScreen(Sun.xyz - (Right.xyz + Up.xyz) * half), high = OnScreen(Sun.xyz + (Right.xyz + Up.xyz) * half);
        float2 a = clamp(min(low, high), -1, 1), b = clamp(max(low, high), -1, 1);
        if (b.x > a.x && b.y > a.y)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                {
                    float2 at = lerp(a, b, (float2(x, y) + 0.5) / 8);
                    int2 pixel = min(int2((at * float2(0.5, -0.5) + 0.5) * Depth.xy), int2(Depth.xy) - 1);
                    all += 1;
                    seen += abs(DepthMap.Load(int3(pixel, 0)).x - Depth.z) < 0.0000001 ? 1 : 0;
                }
    }
    float now = all > 0 ? seen / all : 0;
    float before = Before.Load(int3(i, 0, 0)).x;
    return now >= 0.05 && now <= 0.99 ? before + 0.6 * abs(before - now) * (now - before) : now;
}

struct CoronaPoint
{
    float4 position : SV_Position;
    float4 colour : COLOR0;
    float2 uv : TEXCOORD0;
    float saturation : TEXCOORD1;
};

CoronaPoint CoronaVertex(uint id : SV_VertexID)
{
    float2 uv = float2(id & 1, id >> 1);
    float3 at = Sun.xyz;
    float visibility = Seen.Load(int3((int)Screen.y, 0, 0)).x * Sun.w;
    float half = 0.5 * Shape.y;
    float2 topLeft = OnScreen(at - Right.xyz * half + Up.xyz * half);
    float2 topRight = OnScreen(at + Right.xyz * half + Up.xyz * half);
    float2 bottomRight = OnScreen(at + Right.xyz * half - Up.xyz * half);
    float area = abs(topRight.x - topLeft.x) * abs(bottomRight.y - topRight.y);
    float kept = abs(clamp(topRight.x, -1, 1) - clamp(topLeft.x, -1, 1)) * abs(clamp(bottomRight.y, -1, 1) - clamp(topRight.y, -1, 1));
    float shown = visibility == 0 ? 0 : saturate((visibility * area - (area - kept)) / area);
    shown = shown * shown * (3 - 2 * shown);
    float opacity = shown > 0 ? pow(shown, Shade.z) : 0;
    float size = shown > 0 ? pow(shown, Shade.w) : 0;

    float distance = length(at);
    float3 toEye = -at / distance;
    float radius = (sqrt(max(min(distance, Radius.w) - Radius.z, 0) / (Radius.w - Radius.z)) * (Radius.y - Radius.x) + Radius.x) * size;
    float4 colour = lerp(ColourStart, ColourEnd, sqrt(max(min(distance, Shade.y) - Shade.x, 0) / (Shade.y - Shade.x)));
    float across = dot(toEye, Right.xyz);
    float width = radius * Shape.x;
    float angle = Turn.y * 0.017453 + 6.283185 * (0.5 + 0.5 * across * Turn.x);
    float s, c;
    sincos(angle, s, c);
    float2 d = uv - 0.5;
    float2 offset = float2(s * d.y * radius + c * d.x * width + c * Shape.z, -c * d.y * radius + s * d.x * width + s * Shape.z);

    CoronaPoint p;
    p.position = float4(OnScreen(at) + float2(offset.x, offset.y * Screen.x), 0, 1);
    float side = min(abs(across), 1);
    float intensity = pow(max(1 - side * side * (3 - 2 * side), 0.000001), Turn.w) * Turn.z + 1;
    p.colour = float4(colour.rgb * intensity, colour.a * opacity);
    p.uv = uv;
    p.saturation = Shape.w;
    return p;
}

float4 CoronaPixel(CoronaPoint p) : SV_Target
{
    float4 picture = Picture.Sample(Smooth, p.uv);
    float grey = dot(picture.rgb, float3(0.3, 0.59, 0.11));
    float4 c = float4(max(grey + p.saturation * (picture.rgb - grey), 0), picture.a) * p.colour * Tint;
    return float4(saturate(c.a * c.rgb), 0);
}
)";

// Guerrilla's tint on everything its particle renderer draws (render_scene_part2, into Tint_color).
constexpr float kParticleTint = 1.6f;
constexpr int kMostCoronas = 16;
constexpr float kCoronaDistance = 1;  // how far from the camera toward the sun Guerrilla keeps it, metres
constexpr char kCoronaFileMagic[8] = {'G', 'W', 'C', 'O', 'R', 'O', 'N', '1'};

struct Corona {
    std::string orbital, effect, texture;
    int type = 0;
    bool viewFade[3] = {};
    float rotationOffset = 0, colourStart[4] = {}, colourEnd[4] = {};
    float numbers[18] = {};  // as corona.bin: radius start, end, distance start, end; colour distance start, end,
                             // opacity's and size's visibility decay; width scale, visibility radius, position
                             // offset, saturation; rotation scale, intensity scale, intensity decay, opacity
                             // decay; view fade scale and decay
    ID3D11ShaderResourceView* picture = nullptr;
};

struct CoronaConstants {
    float right[4], up[4], forward[4], sun[4], screen[4];
    float colourStart[4], colourEnd[4], radius[4], shade[4], shape[4], turn[4], tint[4];
    float radii[4][4];
    float depth[4];
};

std::string g_coronaWorld;
std::vector<Corona> g_coronas;
ID3D11VertexShader* g_seenVertex = nullptr;
ID3D11PixelShader* g_seenPixel = nullptr;
ID3D11VertexShader* g_coronaVertex = nullptr;
ID3D11PixelShader* g_coronaPixel = nullptr;
ID3D11Buffer* g_coronaConstants = nullptr;
ID3D11BlendState* g_coronaBlend = nullptr;
ID3D11RasterizerState* g_coronaRaster = nullptr;
ID3D11SamplerState* g_coronaSampler = nullptr;
ID3D11Texture2D* g_coronaSeen[2] = {};  // the visibility, one texel a corona: drawn into in turn
ID3D11RenderTargetView* g_coronaSeenTarget[2] = {};
ID3D11ShaderResourceView* g_coronaSeenView[2] = {};
int g_coronaSeenNext = 0;
bool g_coronaFailed = false;
ID3D11RenderTargetView* g_coronaTarget = nullptr;  // what the particle phase draws into, from its start

bool CoronaWanted() { return !g_coronaFailed && g_sunCorona > 0; }

void DropCoronas() {
    for (Corona& corona : g_coronas)
        if (corona.picture) corona.picture->Release();
    g_coronas.clear();
}

// The world's coronas from mods\guerrilla_world\corona.bin, and their textures (once a world).
void ReadCoronas() {
    const char* world = WorldName();
    if (g_coronaWorld == world) return;
    g_coronaWorld = world;
    DropCoronas();
    std::vector<BYTE> bytes;
    if (!ReadWholeFile(DataFolder() + L"corona.bin", bytes) || bytes.size() < 12 || memcmp(bytes.data(), kCoronaFileMagic, 8) != 0) {
        rfa::Log("Corona: no mods\\guerrilla_world\\corona.bin (the world builder writes it); the sun has none");
        g_coronaFailed = true;
        return;
    }
    int count = 0;
    memcpy(&count, bytes.data() + 8, 4);
    constexpr size_t kEach = 32 + 32 + 64 + 4 + 4 + 4 + 16 + 16 + 18 * 4;
    if (count < 0 || bytes.size() < 12 + static_cast<size_t>(count) * kEach) {
        rfa::Log("Corona: mods\\guerrilla_world\\corona.bin is cut short; the sun has none");
        g_coronaFailed = true;
        return;
    }
    const BYTE* at = bytes.data() + 12;
    auto text = [&](size_t size) {
        std::string s(reinterpret_cast<const char*>(at), strnlen(reinterpret_cast<const char*>(at), size));
        at += size;
        return s;
    };
    auto floats = [&](float* out, int n) {
        memcpy(out, at, n * sizeof(float));
        at += n * sizeof(float);
    };
    for (int i = 0; i < count && static_cast<int>(g_coronas.size()) < kMostCoronas; ++i) {
        Corona corona;
        corona.orbital = text(32);
        corona.effect = text(32);
        corona.texture = text(64);
        memcpy(&corona.type, at, 4);
        at += 4;
        for (int k = 0; k < 3; ++k) corona.viewFade[k] = at[k] != 0;
        at += 4;
        floats(&corona.rotationOffset, 1);
        floats(corona.colourStart, 4);
        floats(corona.colourEnd, 4);
        floats(corona.numbers, 18);
        const std::wstring texture(corona.texture.begin(), corona.texture.end());
        corona.picture = corona.texture.empty() ? nullptr : LoadTexture(DataFolder() + texture + L".dds");
        if (!corona.picture) rfa::Log("Corona: no mods\\guerrilla_world\\%s.dds; that corona isn't drawn", corona.texture.c_str());
        if (corona.type != 0) rfa::Log("Corona: %s's %s (%s) is a corona in the world, which the mod doesn't draw", corona.orbital.c_str(), corona.effect.c_str(), corona.texture.c_str());
        if (corona.viewFade[0] || corona.viewFade[1] || corona.viewFade[2])
            rfa::Log("Corona: %s's %s (%s) has a view fade, which the mod leaves out", corona.orbital.c_str(), corona.effect.c_str(), corona.texture.c_str());
        g_coronas.push_back(std::move(corona));
    }
    rfa::Log("Corona: %zu coronas from corona.bin (%s's %s)", g_coronas.size(), g_coronas.empty() ? "-" : g_coronas[0].orbital.c_str(),
             g_coronas.empty() ? "-" : g_coronas[0].effect.c_str());
}

bool MakeCoronaObjects() {
    if (g_coronaVertex) return true;
    if (!LoadCompiler()) return false;
    ID3DBlob* blobs[4] = {Compile(kCoronaShader, sizeof(kCoronaShader) - 1, "SeenVertex", "vs_4_0"),
                          Compile(kCoronaShader, sizeof(kCoronaShader) - 1, "SeenPixel", "ps_4_0"),
                          Compile(kCoronaShader, sizeof(kCoronaShader) - 1, "CoronaVertex", "vs_4_0"),
                          Compile(kCoronaShader, sizeof(kCoronaShader) - 1, "CoronaPixel", "ps_4_0")};
    bool ok = blobs[0] && blobs[1] && blobs[2] && blobs[3] &&
              SUCCEEDED(g_device->CreateVertexShader(blobs[0]->GetBufferPointer(), blobs[0]->GetBufferSize(), nullptr, &g_seenVertex)) &&
              SUCCEEDED(g_device->CreatePixelShader(blobs[1]->GetBufferPointer(), blobs[1]->GetBufferSize(), nullptr, &g_seenPixel)) &&
              SUCCEEDED(g_device->CreateVertexShader(blobs[2]->GetBufferPointer(), blobs[2]->GetBufferSize(), nullptr, &g_coronaVertex)) &&
              SUCCEEDED(g_device->CreatePixelShader(blobs[3]->GetBufferPointer(), blobs[3]->GetBufferSize(), nullptr, &g_coronaPixel));
    for (ID3DBlob* blob : blobs)
        if (blob) blob->Release();
    D3D11_BUFFER_DESC buffer = {};
    buffer.ByteWidth = sizeof(CoronaConstants);
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(g_device->CreateBuffer(&buffer, nullptr, &g_coronaConstants));
    D3D11_BLEND_DESC blend = {};  // premultiplied, as the particles are drawn
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(g_device->CreateBlendState(&blend, &g_coronaBlend));
    D3D11_RASTERIZER_DESC raster = {};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(g_device->CreateRasterizerState(&raster, &g_coronaRaster));
    D3D11_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(g_device->CreateSamplerState(&sampler, &g_coronaSampler));
    D3D11_TEXTURE2D_DESC seen = {};
    seen.Width = kMostCoronas;
    seen.Height = 1;
    seen.MipLevels = seen.ArraySize = 1;
    seen.Format = DXGI_FORMAT_R16_FLOAT;
    seen.SampleDesc.Count = 1;
    seen.Usage = D3D11_USAGE_DEFAULT;
    seen.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    for (int i = 0; i < 2 && ok; ++i)
        ok = SUCCEEDED(g_device->CreateTexture2D(&seen, nullptr, &g_coronaSeen[i])) &&
             SUCCEEDED(g_device->CreateRenderTargetView(g_coronaSeen[i], nullptr, &g_coronaSeenTarget[i])) &&
             SUCCEEDED(g_device->CreateShaderResourceView(g_coronaSeen[i], nullptr, &g_coronaSeenView[i]));
    if (ok) {
        const float none[4] = {0, 0, 0, 0};
        for (ID3D11RenderTargetView* target : g_coronaSeenTarget) g_context->ClearRenderTargetView(target, none);
    } else {
        rfa::Log("Corona: could not make its shaders or textures; the sun has none");
    }
    return ok;
}

// Draws the sun's coronas into what the particle phase draws into (see above), and puts the device
// context back as it was.
void DrawSunCorona() {
    ID3D11RenderTargetView* target = g_coronaTarget;
    g_coronaTarget = nullptr;
    if (!target) return;
    D3D11_TEXTURE2D_DESC targetDesc, depthDesc;
    const Orbital* sun = nullptr;
    float toward[3] = {}, fade = 0;
    if (!g_coronaFailed) {
        ReadOrbitals();
        ReadCoronas();
        if (!g_coronas.empty())
            for (const Orbital& orbital : g_orbitals)
                if (orbital.name == g_coronas[0].orbital) sun = &orbital;
        if (!g_coronas.empty() && !sun) RFA_LOG_FIRST(1, "Corona: the time of day has no orbital named %s; no corona", g_coronas[0].orbital.c_str());
        if (sun) OrbitalToward(*sun, g_timeOfDay, toward);
        fade = toward[1] >= 0.02f ? 1 : (toward[1] > 0 ? toward[1] / 0.02f : 0);
    }
    bool ready = sun && fade > 0 && g_shaftDepth && TextureOf(target, targetDesc) && TextureOf(g_shaftDepth, depthDesc) && targetDesc.Height > 0;
    if (ready && !MakeCoronaObjects()) {
        g_coronaFailed = true;
        ready = false;
    }
    if (!ready) {
        target->Release();
        return;
    }
    RFA_LOG_FIRST(1, "Corona: drawing %s's %zu coronas in world \"%s\"", sun->name.c_str(), g_coronas.size(), WorldName());

    CoronaConstants constants = {};
    const float width = static_cast<float>(targetDesc.Width), height = static_cast<float>(targetDesc.Height);
    const float tanUp = tanf(g_cameraFov * 3.14159265f / 360.0f);
    for (int i = 0; i < 3; ++i) {
        constants.right[i] = g_cameraRotation[i];
        constants.up[i] = g_cameraRotation[3 + i];
        constants.forward[i] = g_cameraRotation[6 + i];
        constants.sun[i] = toward[i] * kCoronaDistance;
        constants.tint[i] = kParticleTint * g_sunCorona;
    }
    constants.right[3] = tanUp * width / height;
    constants.up[3] = tanUp;
    constants.sun[3] = fade;
    constants.screen[0] = width / height;
    constants.tint[3] = 1;
    for (size_t i = 0; i < g_coronas.size(); ++i) constants.radii[i / 4][i % 4] = g_coronas[i].numbers[9];
    constants.depth[0] = static_cast<float>(depthDesc.Width);
    constants.depth[1] = static_cast<float>(depthDesc.Height);
    constants.depth[2] = g_farDepth;

    SavedState saved;
    saved.Save();
    ID3D11RenderTargetView* gameTarget = nullptr;
    ID3D11DepthStencilView* gameDepth = nullptr;
    g_context->OMGetRenderTargets(1, &gameTarget, &gameDepth);
    D3D11_VIEWPORT gameViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    g_context->RSGetViewports(&viewportCount, gameViewports);
    ID3D11ShaderResourceView* gameVertexResource = nullptr;
    g_context->VSGetShaderResources(3, 1, &gameVertexResource);
    ID3D11DepthStencilState* gameDepthState = nullptr;
    UINT stencilRef = 0;
    g_context->OMGetDepthStencilState(&gameDepthState, &stencilRef);

    auto upload = [&]() {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(g_context->Map(g_coronaConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
        memcpy(mapped.pData, &constants, sizeof(constants));
        g_context->Unmap(g_coronaConstants, 0);
        return true;
    };
    const float noFactor[4] = {1, 1, 1, 1};
    const int next = g_coronaSeenNext, before = 1 - next;
    g_coronaSeenNext = before;
    if (upload()) {
        // How much of each is seen: one texel a corona, from the depth and last frame's.
        ID3D11ShaderResourceView* none[2] = {};
        g_context->VSSetShaderResources(3, 1, none);
        g_context->OMSetRenderTargets(1, &g_coronaSeenTarget[next], nullptr);
        D3D11_VIEWPORT seenViewport = {0, 0, static_cast<float>(g_coronas.size()), 1, 0, 1};
        g_context->RSSetViewports(1, &seenViewport);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->IASetInputLayout(nullptr);
        g_context->VSSetShader(g_seenVertex, nullptr, 0);
        g_context->PSSetShader(g_seenPixel, nullptr, 0);
        g_context->GSSetShader(nullptr, nullptr, 0);
        g_context->VSSetConstantBuffers(0, 1, &g_coronaConstants);
        g_context->PSSetConstantBuffers(0, 1, &g_coronaConstants);
        ID3D11ShaderResourceView* seenInputs[2] = {g_shaftDepth, g_coronaSeenView[before]};
        g_context->PSSetShaderResources(0, 2, seenInputs);
        g_context->OMSetBlendState(nullptr, noFactor, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(nullptr, 0);
        g_context->RSSetState(g_coronaRaster);
        g_context->Draw(3, 0);
        g_context->PSSetShaderResources(0, 2, none);

        // The coronas, over the picture.
        g_context->OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT viewport = {0, 0, width, height, 0, 1};
        g_context->RSSetViewports(1, &viewport);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        g_context->VSSetShader(g_coronaVertex, nullptr, 0);
        g_context->PSSetShader(g_coronaPixel, nullptr, 0);
        g_context->VSSetShaderResources(3, 1, &g_coronaSeenView[next]);
        g_context->PSSetSamplers(0, 1, &g_coronaSampler);
        g_context->OMSetBlendState(g_coronaBlend, noFactor, 0xFFFFFFFF);
        for (size_t i = 0; i < g_coronas.size(); ++i) {
            const Corona& corona = g_coronas[i];
            if (!corona.picture || corona.type != 0) continue;
            const float* n = corona.numbers;
            memcpy(constants.colourStart, corona.colourStart, sizeof(constants.colourStart));
            memcpy(constants.colourEnd, corona.colourEnd, sizeof(constants.colourEnd));
            const float radius[4] = {n[0], n[1], n[2], n[3]}, shade[4] = {n[4], n[5], n[6], n[7]}, shape[4] = {n[8], n[9], n[10], n[11]},
                        turn[4] = {n[12], corona.rotationOffset, n[13], n[14]};
            memcpy(constants.radius, radius, sizeof(radius));
            memcpy(constants.shade, shade, sizeof(shade));
            memcpy(constants.shape, shape, sizeof(shape));
            memcpy(constants.turn, turn, sizeof(turn));
            constants.screen[1] = static_cast<float>(i);
            if (!upload()) break;
            g_context->PSSetShaderResources(2, 1, &corona.picture);
            g_context->Draw(4, 0);
        }
    }

    g_context->VSSetShaderResources(3, 1, &gameVertexResource);
    g_context->OMSetRenderTargets(1, &gameTarget, gameDepth);
    g_context->RSSetViewports(viewportCount, gameViewports);
    g_context->OMSetDepthStencilState(gameDepthState, stencilRef);
    saved.Restore();
    for (IUnknown* held : {static_cast<IUnknown*>(gameTarget), static_cast<IUnknown*>(gameDepth), static_cast<IUnknown*>(gameVertexResource),
                           static_cast<IUnknown*>(gameDepthState), static_cast<IUnknown*>(target)})
        if (held) held->Release();
}

// ---- Guerrilla's particle layer

// Guerrilla draws its particles into a buffer of their own (premultiplied colour, and in alpha how
// much they cover) and lays it over the picture only in its final tone mapping (rl_hdr pass 21):
// picture = 1 − (1 − particles) · e^(−exposure · (scene · (1 − cover) + bloom)). So particles keep
// the colour they are drawn in, dim only what is behind them, and never feed the bloom or the eye.
// Armageddon draws them into the lit scene before its bloom, where Guerrilla's colours (made for
// Guerrilla's way) burn out: an EDF screen's glow added to its lit screen turns white and blooms.
//
// Armageddon draws them all in one phase (0xC35FC0, thiscall, one argument, ret 4): the half-res
// ones (0xC35DD0, laid into the scene at its end), the full-resolution ones (0xC35040), then a last
// step (0xC35CF0, none) for the heat distortion, which reads the scene. All of them draw into the
// scene (half floats) with premultiplied blending (colour one / one − source alpha, alpha the same,
// all four written), so a scene cleared to nothing ends the phase holding exactly Guerrilla's
// particle buffer. In our worlds with Guerrilla's tone mapping (Particles=1): at the phase's start
// the scene is copied aside and cleared; at the last step's start (or the phase's end) what it then
// holds is copied into our layer and the scene put back, so the distortion, the bloom and the eye see
// the scene without particles; the tone mapping lays the layer over it. Otherwise the game draws its
// particles its own way.
constexpr DWORD kParticlePhase = 0xC35FC0, kParticleLast = 0xC35CF0;
constexpr size_t kParticlePhasePrologue = 5;  // push ebp / mov ebp,esp / push −1 (both)
using ParticlePhaseFn = void(__fastcall*)(void* renderer, void* unused, int value);
using ParticleLastFn = void(__fastcall*)(void* renderer, void* unused);
ParticlePhaseFn g_particlePhase;
ParticleLastFn g_particleLast;
ID3D11Texture2D* g_particleSceneCopy = nullptr;  // the scene as the phase found it
ID3D11Texture2D* g_particleTaken = nullptr;      // what the phase drew, as drawn (multisampled if the scene is)
ID3D11Texture2D* g_particleResolved = nullptr;   // ... and resolved, when it is
ID3D11Texture2D* g_particleScene = nullptr;      // the scene being drawn into, while the phase runs
D3D11_TEXTURE2D_DESC g_particleDesc = {};
bool g_particleLayerFailed = false;

bool ParticlesWanted() {
    return g_particles && !g_particleLayerFailed && GuerrillaToneMapping() && g_toneVertex && g_context && g_device && InOurWorld();
}

// The layer's textures for a scene of this description (made again when it changes).
bool MakeParticleTextures(const D3D11_TEXTURE2D_DESC& scene) {
    if (g_particleSceneCopy && scene.Width == g_particleDesc.Width && scene.Height == g_particleDesc.Height &&
        scene.Format == g_particleDesc.Format && scene.SampleDesc.Count == g_particleDesc.SampleDesc.Count &&
        scene.SampleDesc.Quality == g_particleDesc.SampleDesc.Quality && scene.MipLevels == g_particleDesc.MipLevels)
        return true;
    for (ID3D11Texture2D** texture : {&g_particleSceneCopy, &g_particleTaken, &g_particleResolved}) {
        if (*texture) (*texture)->Release();
        *texture = nullptr;
    }
    if (g_particleView) g_particleView->Release();
    g_particleView = nullptr;
    g_particleDesc = scene;
    D3D11_TEXTURE2D_DESC copy = scene;
    copy.Usage = D3D11_USAGE_DEFAULT;
    copy.BindFlags = 0;
    copy.CPUAccessFlags = 0;
    copy.MiscFlags = 0;
    const bool multisampled = scene.SampleDesc.Count > 1;
    D3D11_TEXTURE2D_DESC taken = copy;
    if (!multisampled) taken.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_TEXTURE2D_DESC resolved = copy;
    resolved.SampleDesc = {1, 0};
    resolved.MipLevels = 1;
    resolved.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bool ok = SUCCEEDED(g_device->CreateTexture2D(&copy, nullptr, &g_particleSceneCopy)) &&
              SUCCEEDED(g_device->CreateTexture2D(&taken, nullptr, &g_particleTaken)) &&
              (!multisampled || SUCCEEDED(g_device->CreateTexture2D(&resolved, nullptr, &g_particleResolved)));
    ok = ok && SUCCEEDED(g_device->CreateShaderResourceView(multisampled ? g_particleResolved : g_particleTaken, nullptr, &g_particleView));
    if (!ok) {
        g_particleLayerFailed = true;
        rfa::Log("Particles: could not make the particle layer's textures; the game draws its particles its own way");
        return false;
    }
    rfa::Log("Particles: drawn into a layer of their own and laid over the picture as Guerrilla does (%u x %u, format %d, %u samples)",
             scene.Width, scene.Height, static_cast<int>(scene.Format), scene.SampleDesc.Count);
    return true;
}

// At the phase's start: the scene being drawn into, copied aside and cleared.
void SetSceneAside() {
    if (g_particleScene || !ParticlesWanted()) return;
    ID3D11RenderTargetView* target = nullptr;
    g_context->OMGetRenderTargets(1, &target, nullptr);
    if (!target) return;
    D3D11_TEXTURE2D_DESC desc;
    ID3D11Resource* resource = nullptr;
    target->GetResource(&resource);
    ID3D11Texture2D* scene = nullptr;
    if (resource && TextureOf(target, desc) && desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT &&
        SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&scene))) && MakeParticleTextures(desc)) {
        if (g_particlesReady) RFA_LOG_FIRST(3, "Particles: last frame's layer was never laid in (the tone mapping didn't draw)");
        g_context->CopyResource(g_particleSceneCopy, scene);
        const float nothing[4] = {0, 0, 0, 0};
        g_context->ClearRenderTargetView(target, nothing);
        g_particleScene = scene;  // (the reference kept until the scene is put back)
        scene = nullptr;
    } else {
        RFA_LOG_FIRST(1, "Particles: the scene the particles draw into isn't a half-float target; the game draws them its own way there");
    }
    if (scene) scene->Release();
    if (resource) resource->Release();
    target->Release();
}

// Once the particles are drawn: what the scene holds is the layer, and the scene is put back.
void TakeParticles() {
    if (!g_particleScene) return;
    g_context->CopyResource(g_particleTaken, g_particleScene);
    if (g_particleResolved) g_context->ResolveSubresource(g_particleResolved, 0, g_particleTaken, 0, g_particleDesc.Format);
    g_context->CopyResource(g_particleScene, g_particleSceneCopy);
    g_particleScene->Release();
    g_particleScene = nullptr;
    g_particlesReady = true;
}

// At the phase's start in our worlds: what it draws into, for the sun's corona at its end.
void NoteCoronaTarget() {
    if (g_coronaTarget) g_coronaTarget->Release();
    g_coronaTarget = nullptr;
    if (InOurWorld() && CoronaWanted() && g_context) g_context->OMGetRenderTargets(1, &g_coronaTarget, nullptr);
}

void NoteParticleFog();  // (see "Guerrilla's fog on particles")

void __fastcall ParticlePhase(void* renderer, void* unused, int value) {
    if (g_fogPending) {
        g_fogPending = false;
        RFA_LOG_FIRST(1, "Fog: drawing Guerrilla's fog in world \"%s\"", WorldName());
        DrawFog();
        DropFogInputs();
    }
    NoteParticleFog();
    NoteCoronaTarget();
    SetSceneAside();
    g_particlePhase(renderer, unused, value);
    DrawSunCorona();  // (if the last step didn't)
    TakeParticles();
}

void __fastcall ParticleLast(void* renderer, void* unused) {
    DrawSunCorona();
    TakeParticles();
    g_particleLast(renderer, unused);
}

// ---- Guerrilla's fog on particles

// Guerrilla fogs every particle as its surfaces (its rl_particle_standard_* vertex shaders, the same
// in 2009 and Re-Mars-tered): its height fog at each corner, f = 1 − min(2^(−(term × distance ×
// density − offset)), 1), term = (1 − e^(−k·dy)) ÷ (k·dy) (1 when nearly level), with the time of
// day's numbers as for its surfaces (see FogNumbers). Its pixel shaders then take the particle's
// colour and alpha toward (F, 0) by f, and its tint (1.6, see kParticleTint) over that: so far off a
// particle turns the haze's colour as it fades out, and lost in the fog it is gone. F is the colour a
// surface lost in the fog shows on the screen (calculate_particle_fog_color, drawn once a frame
// before the particles: 1 − e^(−8 × exposure × the fog colour, saturated and tinted as the last pass
// does)), since its particles go over the tone-mapped picture.
//
// Fog at one point seen from the camera, as Guerrilla's particles' vertex shaders work it out.
float FogAt(const float (&numbers)[4], const float* at) {
    const float d[3] = {at[0] - g_cameraPosition[0], at[1] - g_cameraPosition[1], at[2] - g_cameraPosition[2]};
    const float distance = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const float kh = d[1] * numbers[0];
    const float term = fabsf(d[1]) > 0.01f ? (1 - exp2f(kh * -1.442695f)) / kh : 1;
    float fog = 1 - exp2f(-(term * distance * numbers[1] - numbers[3]));
    if (fog < 0) fog = 0;
    if (fog > 1) fog = 1;
    return fog;
}

// The colour particles are fogged toward (in Armageddon's Fog_color, which its particle pixel shaders
// mix toward by their fog), times Guerrilla's particle tint, which multiplies it in Guerrilla's
// shaders and which the world builder bakes into our particles' own colours (Armageddon's renderer has
// none). In Guerrilla's particle layer (ParticlesWanted) it is F, as the tone mapping draws a surface
// lost in the fog (TonePixel: its saturation and tint, then 1 − e^(−8 × exposure × that), the exposure
// from the eye's brightness a frame or two back); drawn into the scene instead, the fog's own colour,
// as the fog over the world.
void ParticleFogColour(float (&out)[4]) {
    float colour[3], tint[3];
    TodColour(kFogColour, colour);
    PartTint(tint);
    for (int i = 0; i < 3; ++i) colour[i] *= tint[i];
    if (ParticlesWanted()) {
        float correction[3];
        ColourCorrection(correction);
        const float saturation = g_colourCorrection ? TodNumber(kColourSaturation) : 1.0f;
        const float base = TodNumber(kEyeAdaptionBase);
        const float adapted = g_eyeBrightness >= 0 ? g_eyeBrightness : base;
        float lit = 1 + TodNumber(kLumOffset) * (base + TodNumber(kEyeAdaptionAmount) * (adapted - base));
        if (lit < 0.1f) lit = 0.1f;
        const float exposure = 8 * TodNumber(kLumRange) / lit * g_exposure;  // (× 8: the scene's scale, see kToneMappingShader)
        const float luminance = colour[0] * 0.3f + colour[1] * 0.59f + colour[2] * 0.11f;
        for (int i = 0; i < 3; ++i) {
            float seen = (luminance + saturation * (colour[i] - luminance)) * correction[i];
            if (seen < 0) seen = 0;
            colour[i] = 1 - expf(-exposure * seen);
        }
    }
    for (int i = 0; i < 3; ++i) out[i] = colour[i] * kParticleTint;
    out[3] = 0;
}

// Armageddon fogs its particles by their distance alone (each vertex Fog_dist.x × sat((d − y) ÷ (z −
// y))^w, its pixel shaders mixing the colour toward Fog_color by that, never fading them out), from
// its environment boxes, whose fog our worlds hold at 100,000 m: none. So in our worlds each effect is
// drawn with Guerrilla's fog. The particle renderer's draw of one effect (0xC25030, thiscall, the
// effect's instance; it draws with the renderer's +0x2E4 set, and otherwise only sorts the instance
// into its step's list) is wrapped: Guerrilla's fog f is worked out at the effect's particles (the
// mean over up to 32 of them), and the effect drawn with Fog_dist (f, −1, 0, 1), which gives every
// vertex f, Fog_color as ParticleFogColour, and its opacity (the instance's +0xE8, which the draw
// hands the vertex shader as Effect_opacity.x: it multiplies the particles' alpha and so, as they are
// premultiplied, their colour) times 1 − f; then all are put back (the getters 0xC35900, 0xC35960;
// the draw goes out inside, and a value left set would reach the next). Heat haze (the distortion
// step, the renderer's +0x2A1 set through it) is left as it is. Guerrilla works f out at each corner;
// once an effect, the far ones (all about one distance off) are as Guerrilla's, while a big one near
// by (a smoke column) is fogged evenly through.
//
// The instance (rl_renderable_particles_instance): +0xE0 its renderable (+0x04 flags: 8 the particles
// simulated on the CPU, 1 their positions the emitter's own), +0x50 the CPU's particles (records of
// 0x80 bytes, the position first), +0x64 the GPU's (parametric, 0x40 bytes, where each set off from
// first; the shader moves it on), each a pointer and at +8 a count; +0x4C the GPU particles' constants
// (+0xD4 above 0: their positions the emitter's own); +0x7C the emitter's position and +0x88 its turn
// (3 × 3, rows) for those; +0xE8 the opacity. Positions are the world's.
//
// Guerrilla fogs its coronas (lamps' glows) too: its corona vertex shader works out the same fog and
// its pixel shader takes the colour down by 1 − f, as Armageddon's rl_corona does from Fog_dist. The
// corona's draw (0xC33820, thiscall: the corona, the pass) is wrapped as the particles' is, f at the
// corona (+0x10, its position in the world), in the pass that draws it (0; passes 1 and 2 ask the GPU
// how much of it shows).
constexpr DWORD kParticleDraw = 0xC25030, kCoronaDraw = 0xC33820;
constexpr size_t kParticleDrawPrologue = 6;  // push ebp / mov ebp,esp / sub esp,70h
constexpr size_t kCoronaDrawPrologue = 6;    // push ebx / mov ebx,esp / sub esp,8
constexpr size_t kRendererDrawing = 0x2E4, kRendererDistortion = 0x2A1;
constexpr size_t kInstanceGpuConstants = 0x4C, kInstanceCpuParticles = 0x50, kInstanceGpuParticles = 0x64, kInstanceEmitterAt = 0x7C,
                 kInstanceEmitterTurn = 0x88, kInstanceRenderable = 0xE0, kInstanceOpacity = 0xE8;
constexpr size_t kRenderableFlags = 0x04, kGpuConstantsLocal = 0xD4, kCoronaAt = 0x10;
constexpr DWORD kRenderableOnCpu = 8, kRenderableLocal = 1;
using ParticleDrawFn = void(__fastcall*)(void* renderer, void* unused, BYTE* instance);
using CoronaDrawFn = void(__fastcall*)(void* renderer, void* unused, BYTE* corona, int pass);
ParticleDrawFn g_particleDraw;
CoronaDrawFn g_coronaDraw;
// The renderer's fog (on the render context; the object they are called on goes unused).
auto const SetFogDistance = reinterpret_cast<void(__fastcall*)(void* renderer, void* unused, float x, float y, float z, float w)>(0xC35880);
auto const GetFogDistance = reinterpret_cast<void(__fastcall*)(void* renderer, void* unused, float* x, float* y, float* z, float* w)>(0xC35900);
auto const SetFogColour = reinterpret_cast<void(__fastcall*)(void* renderer, void* unused, const float* colour)>(0xC35510);
auto const GetFogColour = reinterpret_cast<void(__fastcall*)(void* renderer, void* unused, float* colour)>(0xC35960);

// This frame's fog for particles, set as the particle phase starts.
bool g_particleFogNow = false;
float g_particleFogNumbers[4] = {};
float g_particleFogColour[4] = {};

void NoteParticleFog() {
    g_particleFogNow = g_particleFog && InOurWorld();
    if (!g_particleFogNow) return;
    FogNumbers(g_particleFogNumbers);
    ParticleFogColour(g_particleFogColour);
}

// Guerrilla's fog at an effect's particles: the mean over up to 32 of them, spread through its list
// (0 with none). `distance`: how far off the first of them is (for the log).
float EffectFog(BYTE* instance, float& distance) {
    distance = 0;
    const BYTE* renderable = *reinterpret_cast<BYTE* const*>(instance + kInstanceRenderable);
    if (!renderable) return 0;
    const DWORD flags = *reinterpret_cast<const DWORD*>(renderable + kRenderableFlags);
    const bool onCpu = (flags & kRenderableOnCpu) != 0;
    const BYTE* list = instance + (onCpu ? kInstanceCpuParticles : kInstanceGpuParticles);
    const BYTE* records = *reinterpret_cast<const BYTE* const*>(list);
    const int count = *reinterpret_cast<const int*>(list + 8);
    if (!records || count <= 0) return 0;
    const size_t size = onCpu ? 0x80 : 0x40;
    bool local = (flags & kRenderableLocal) != 0;
    if (!onCpu) {
        const BYTE* constants = *reinterpret_cast<const BYTE* const*>(instance + kInstanceGpuConstants);
        local = constants && *reinterpret_cast<const float*>(constants + kGpuConstantsLocal) > 0;
    }
    const float* emitter = reinterpret_cast<const float*>(instance + kInstanceEmitterAt);
    const float* turn = reinterpret_cast<const float*>(instance + kInstanceEmitterTurn);
    const int samples = count < 32 ? count : 32;
    float total = 0;
    for (int s = 0; s < samples; ++s) {
        const float* p = reinterpret_cast<const float*>(records + static_cast<size_t>(s) * count / samples * size);
        float at[3];
        for (int i = 0; i < 3; ++i) at[i] = local ? emitter[i] + p[0] * turn[i] + p[1] * turn[3 + i] + p[2] * turn[6 + i] : p[i];
        if (s == 0) {
            const float d[3] = {at[0] - g_cameraPosition[0], at[1] - g_cameraPosition[1], at[2] - g_cameraPosition[2]};
            distance = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        }
        total += FogAt(g_particleFogNumbers, at);
    }
    return total / samples;
}

// Draws with Fog_dist giving every vertex `fog` and Fog_color as given (null: left), then puts both back.
template <typename Draw>
void DrawFogged(float fog, const float* colour, Draw draw) {
    void* renderer = *reinterpret_cast<void**>(kRenderer);
    float before[4] = {}, colourBefore[4] = {};
    GetFogDistance(renderer, nullptr, &before[0], &before[1], &before[2], &before[3]);
    if (colour) GetFogColour(renderer, nullptr, colourBefore);
    SetFogDistance(renderer, nullptr, fog, -1, 0, 1);
    if (colour) SetFogColour(renderer, nullptr, colour);
    draw();
    SetFogDistance(renderer, nullptr, before[0], before[1], before[2], before[3]);
    if (colour) SetFogColour(renderer, nullptr, colourBefore);
}

void __fastcall ParticleDraw(void* renderer, void* unused, BYTE* instance) {
    const BYTE* drawing = static_cast<const BYTE*>(renderer);
    float distance = 0;
    const float fog = g_particleFogNow && instance && drawing[kRendererDrawing] != 0 && drawing[kRendererDistortion] == 0
                          ? EffectFog(instance, distance) : 0.0f;
    if (fog < 0.0005f) {
        g_particleDraw(renderer, unused, instance);
        return;
    }
    RFA_LOG_DETAIL_FIRST(3, "Particles: an effect %.0f m off fogged %.2f as Guerrilla's (toward %.3f %.3f %.3f)", distance, fog,
                  g_particleFogColour[0], g_particleFogColour[1], g_particleFogColour[2]);
    float& opacity = *reinterpret_cast<float*>(instance + kInstanceOpacity);
    const float opacityBefore = opacity;
    opacity = opacityBefore * (1 - fog);
    DrawFogged(fog, g_particleFogColour, [&] { g_particleDraw(renderer, unused, instance); });
    opacity = opacityBefore;
}

void __fastcall CoronaDraw(void* renderer, void* unused, BYTE* corona, int pass) {
    const float fog = g_particleFogNow && corona && pass == 0 ? FogAt(g_particleFogNumbers, reinterpret_cast<const float*>(corona + kCoronaAt)) : 0.0f;
    if (fog < 0.0005f) {
        g_coronaDraw(renderer, unused, corona, pass);
        return;
    }
    const float* at = reinterpret_cast<const float*>(corona + kCoronaAt);
    RFA_LOG_DETAIL_FIRST(2, "Particles: a corona %.0f m off fogged %.2f as Guerrilla's",
                  sqrtf((at[0] - g_cameraPosition[0]) * (at[0] - g_cameraPosition[0]) + (at[1] - g_cameraPosition[1]) * (at[1] - g_cameraPosition[1]) +
                        (at[2] - g_cameraPosition[2]) * (at[2] - g_cameraPosition[2])), fog);
    DrawFogged(fog, nullptr, [&] { g_coronaDraw(renderer, unused, corona, pass); });
}

// ---- Armageddon's mission start in our worlds

// Armageddon starts (or starts again) its current mission from F9, one of its developer keys (the key
// handler 0x4D5580, scan code 0x43, in play), and from the console's mission_start with no name
// (0x504E50): both call 0x4FD750 (cdecl, none), which builds a `mission_start "<name>"` and runs it.
// Our worlds have none of Armageddon's missions; there the start trips one of the game's own checks
// (0xC99A60, "Concurrency bug, another thread using this object pool", from 0x4283FF), whose
// breakpoint makes the game write its dump and quit (and its exit then crashes in a static
// destructor, on a lock already deleted). So in our worlds it does nothing but say so.
constexpr DWORD kStartMission = 0x4FD750;
constexpr size_t kStartMissionPrologue = 9;  // push ebp / mov ebp,esp / sub esp,344h
using StartMissionFn = void(__cdecl*)();
StartMissionFn g_startMission;
auto const ConsolePrint = reinterpret_cast<void(__cdecl*)(const char* text, int flags)>(0xC7C310);

void __cdecl StartMission() {
    if (!InOurWorld()) {
        g_startMission();
        return;
    }
    rfa::Log("Missions: F9 or mission_start would start one of Armageddon's missions, which world \"%s\" hasn't (left alone)", WorldName());
    ConsolePrint("Guerrilla's world has none of Armageddon's missions to start (F9, mission_start).\n", 0);
}

// ---- Guerrilla's wind

// Armageddon blows its wind from its environment boxes: each frame the world update (0x768DF0, after
// the effects update) blends the boxes' env_settings at the camera (0x51BD30) and applies them
// (0x51D960, cdecl: the blend, the scene, where), and their weather part (+0x150 on; +0x154 a value of
// its own, 0x86D140; +0x158 and +0x15C the wind's speed and direction, 0x6FB650; +0x160 and +0x164 the
// weather camera effects, 0x6E45B0) is on even with no box about, with the defaults' wind of 0 mph from
// 90°. So in our worlds the wind died a frame after the level started, and with it the dust. Guerrilla
// has no such boxes: its weather wanders the wind by itself (weather_wind_update, every frame). Every
// 20 s (WIND_RANDOM_CHANGE_INTERVAL) it takes the player's zone's wind_min_speed and wind_max_speed and
// a new speed at most 40 mph either way from the present one, within them; over the first 10 s of
// those 20 (WIND_RAMPUP_TIME) the speed it heads for moves from the old to the new, and the wind's speed
// and direction follow what they head for at 30 a second (Wind_transition_rate). It starts at the
// middle of the zone's limits (75 and 85 outside every zone) from 90° (weather_wind_start), as
// Armageddon's own start does (0x6FB390: also when the `wind` command turns it on; the wind blows while
// 0x18E15A8 is set). In our worlds the mod leaves the boxes' weather out and blows Guerrilla's wind
// before the game's effects update, which turns it into the effects' wind.
constexpr DWORD kApplyEnvSettings = 0x51D960;
constexpr size_t kApplyEnvSettingsPrologue = 9;  // push ebp / mov ebp,esp / sub esp,0F8h
constexpr size_t kEnvWeatherOn = 0x150;
using ApplyEnvSettingsFn = void(__cdecl*)(BYTE* settings, void* scene, const float* where);
ApplyEnvSettingsFn g_applyEnvSettings;
constexpr DWORD kWindStart = 0x6FB390;
constexpr size_t kWindStartPrologue = 6;  // push ebp / mov ebp,esp / sub esp,8
using WindStartFn = bool(__cdecl*)();
WindStartFn g_windStart;

// The weather's wind (0x6FB620: its speed in mph and its direction in degrees; set by 0x6FB650, whose
// third value goes unused), whether it blows, and the zone's limits (0x10FF838 and 0x10FF83C, the
// latter also Guerrilla's Wind_max_speed). The local player (0x8B92D0; where it is at +0x04) and the
// zone at a point (0x6CB3C0, the world in ECX; wind_min_speed and wind_max_speed at +0xC8 and +0xCC).
using WindFn = void(__cdecl*)(float* speed, float* direction);
auto const Wind = reinterpret_cast<WindFn>(0x6FB620);
using SetWindFn = void(__cdecl*)(float speed, float direction, float unused);
auto const SetWind = reinterpret_cast<SetWindFn>(0x6FB650);
auto& g_windBlows = *reinterpret_cast<bool*>(0x18E15A8);
auto& g_windLowSpeed = *reinterpret_cast<float*>(0x10FF838);
auto& g_windTopSpeed = *reinterpret_cast<float*>(0x10FF83C);
using LocalPlayerFn = BYTE*(__cdecl*)();
auto const LocalPlayer = reinterpret_cast<LocalPlayerFn>(0x8B92D0);
using ZoneAtFn = BYTE*(__fastcall*)(void* world, void* unused, const float* where);
auto const ZoneAt = reinterpret_cast<ZoneAtFn>(0x6CB3C0);
constexpr size_t kPlayerPlace = 0x04, kZoneWindLow = 0xC8, kZoneWindTop = 0xCC;
void* const kWorld = reinterpret_cast<void*>(0x1870630);

constexpr float kWindChangeEvery = 20, kWindRampTime = 10, kWindMostChange = 40, kWindFollowRate = 30;
constexpr float kWindStartDirection = 90;
struct GuerrillaWind {
    bool started = false;
    float speed = 0, direction = 0;      // the wind's
    float toSpeed = 0, toDirection = 0;  // what they head for
    float rampFrom = 0, rampTo = 0;      // this change's old and new speed
    float changeLeft = -1;               // seconds until the next change; below 0: one now
};
GuerrillaWind g_wind;
unsigned g_windRandom = 0x2545F491u;

// A random number from low to high (a xorshift of the mod's own).
float WindRandom(float low, float high) {
    g_windRandom ^= g_windRandom << 13;
    g_windRandom ^= g_windRandom >> 17;
    g_windRandom ^= g_windRandom << 5;
    return low + (high - low) * static_cast<float>(g_windRandom & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

// The player's zone's wind limits, if the player is in one.
bool ZoneWindLimits(float& low, float& top) {
    const BYTE* player = LocalPlayer();
    const BYTE* zone = player ? ZoneAt(kWorld, nullptr, reinterpret_cast<const float*>(player + kPlayerPlace)) : nullptr;
    if (!zone) return false;
    low = *reinterpret_cast<const float*>(zone + kZoneWindLow);
    top = *reinterpret_cast<const float*>(zone + kZoneWindTop);
    return true;
}

// The same blend carries the boxes' view: while +0xCC (affects_visibility) is set, the apply hands +0xD0
// (cull_distance) and +0xD4 (far_clip) to the scene (0x4ECEC0, 0x4ECEE0), and the far clip becomes the camera's
// far plane (the depth's IR_Similarity_Data). Both are 475 m by the zone schema's defaults, Guerrilla's camera's
// far clip; our world's boxes once asked for 1,500 m. In our worlds the far clip is FarClip= instead (Guerrilla's
// 475 by default; see ReadFarClip); the cull distance stays the boxes'.
constexpr size_t kEnvAffectsVisibility = 0xCC, kEnvFarClip = 0xD4;

void __cdecl ApplyEnvSettings(BYTE* settings, void* scene, const float* where) {
    // (The blend is the caller's own copy, made each frame.)
    if (settings && InOurWorld()) {
        *reinterpret_cast<int*>(settings + kEnvWeatherOn) = 0;
        if (g_farClip > 0 && *reinterpret_cast<int*>(settings + kEnvAffectsVisibility) != 0) {
            float& farClip = *reinterpret_cast<float*>(settings + kEnvFarClip);
            static float told = -1;
            if (told != g_farClip) {
                told = g_farClip;
                rfa::Log("View distance: the camera draws to %.0f m in world \"%s\" (FarClip=; Guerrilla's 475; the zones' boxes say %.0f m)",
                         g_farClip, WorldName(), farClip);
            }
            farClip = g_farClip;
        }
    }
    g_applyEnvSettings(settings, scene, where);
}

bool __cdecl WindStart() {
    g_wind.started = false;  // (started again as Guerrilla's at the next update in our worlds)
    return g_windStart();
}

void GuerrillaWindUpdate(float seconds) {
    if (!g_windBlows) {
        g_wind = GuerrillaWind{};
        return;
    }
    if (!g_wind.started) {
        float low = 75, top = 85;
        ZoneWindLimits(low, top);
        g_windLowSpeed = low;
        g_windTopSpeed = top;
        g_wind = GuerrillaWind{};
        g_wind.started = true;
        g_wind.speed = g_wind.toSpeed = g_wind.rampFrom = g_wind.rampTo = (low + top) / 2;
        g_wind.direction = g_wind.toDirection = kWindStartDirection;
        rfa::Log("Wind: Guerrilla's in world \"%s\": %.0f mph from %.0f degrees, wandering within %.0f-%.0f mph",
                 WorldName(), g_wind.speed, g_wind.direction, low, top);
    }
    if (g_wind.changeLeft < 0) {
        // A new speed, at most 40 mph either way, within the zone's limits (outside every zone, the
        // limits it had).
        g_wind.changeLeft = kWindChangeEvery;
        float low = g_windLowSpeed, top = g_windTopSpeed;
        ZoneWindLimits(low, top);
        if (low > top) std::swap(low, top);
        g_windLowSpeed = low;
        g_windTopSpeed = top;
        const float up = std::clamp(top - g_wind.speed, 0.0f, kWindMostChange);
        const float down = std::clamp(low - g_wind.speed, -kWindMostChange, 0.0f);
        g_wind.rampFrom = g_wind.speed;
        g_wind.rampTo = g_wind.speed + WindRandom(down, up);
        RFA_LOG_DETAIL_FIRST(6, "Wind: heading from %.0f to %.0f mph over %.0f s (the zone's %.0f-%.0f)", g_wind.rampFrom, g_wind.rampTo,
                      kWindRampTime, low, top);
    } else {
        g_wind.changeLeft -= seconds;
        if (g_wind.changeLeft > kWindChangeEvery - kWindRampTime) {
            const float t = (kWindChangeEvery - g_wind.changeLeft) / kWindRampTime;
            g_wind.toSpeed = fabsf(g_wind.rampFrom + t * (g_wind.rampTo - g_wind.rampFrom));
        } else if (g_wind.changeLeft <= 0) {
            g_wind.changeLeft = -1;
        }
    }
    const float follow = std::min(kWindFollowRate * seconds, 1.0f);
    g_wind.speed += (g_wind.toSpeed - g_wind.speed) * follow;
    g_wind.direction += (g_wind.toDirection - g_wind.direction) * follow;
    SetWind(g_wind.speed, g_wind.direction, -1);
}

// ---- Guerrilla's dust

// Guerrilla blows dust round the camera with a system of its own (dust_update, every frame), which
// Armageddon has dropped (its dust_system_init is empty) though it keeps all it rests on: the weather's
// wind, the effects' wind and expression variables, and the effect player. Each frame:
//   - how much of the sky round the camera is blocked (gameaudio_update_listener_los): one 50 m ray a
//     frame (two in Armageddon, see SkyBlocked), in turn along 26 directions, each blocked if it hits
//     the world within 20 m; the share blocked, each direction weighed (up 2, up and out 1, level 0.5,
//     down 0.1);
//   - the effects' variable wind_los: the wind's speed in m/s times the share of the sky left open
//     (so none indoors), set for every effect (the camera effect's expressions fade its haze, its dust
//     and then its grit in as wind_los grows from 0 to 45; see OpacityScales);
//   - the camera effect (weather.xtbl's first Dust_effects, env_dust_camera_a; the world builder adds
//     it as rfg_env_dust_camera_a) kept playing at the camera, turned as the effects' wind blows and
//     moved upwind by up to a quarter of its radius (Dust_max_offset) as the wind nears its top speed.
// In our worlds the mod does the same after the game's own effects update (0x4F8E10, cdecl, the
// frame's seconds; it turns the weather's wind into the effects' wind and ages the effects), with
// Guerrilla's wind (see GuerrillaWindUpdate). Not done yet: Guerrilla's gusts of dust on the ground
// round the player (each ground material's own) and the fading of the dust right by buildings.
constexpr DWORD kEffectsUpdate = 0x4F8E10;
constexpr size_t kEffectsUpdatePrologue = 6;  // push ebp / mov ebp,esp / sub esp,5Ch
using EffectsUpdateFn = void(__cdecl*)(float seconds);
EffectsUpdateFn g_effectsUpdate;

// effects.xtbl's entries by name (0x4EF370: the entry's number, or −1) and by number (0x4EF320: the
// entry; +0x44 its visual's number); playing one (0x4EB460: the entry's number, where, the turn, and
// seven more, given as Armageddon's own camera effects give them), which returns the effect object's
// handle or −1. An object by its handle (the world, 0x1870630, in ECX; 0x4D4360), or null: an effect
// object's place at +0x04, its turn (rows right, up, forward) at +0x10, flags at +0x57 (8: moved, to be
// drawn there; as Guerrilla's set_render_flags(4)). As Armageddon's camera effects do once one plays,
// two of its flags cleared (0x7B20A0(false), 0x4F8DA0(false)); stopping it 0x4F7AA0(1), as Guerrilla's
// dust does.
using EffectNumberFn = int(__cdecl*)(const char* name);
auto const EffectNumber = reinterpret_cast<EffectNumberFn>(0x4EF370);
using EffectEntryFn = BYTE*(__cdecl*)(int number);
auto const EffectEntry = reinterpret_cast<EffectEntryFn>(0x4EF320);
using EffectsPlayFn = int(__cdecl*)(int effect, const float* where, const float* turn, bool a4, int a5, int a6, int a7, bool a8, DWORD a9, DWORD a10);
auto const EffectsPlay = reinterpret_cast<EffectsPlayFn>(0x4EB460);
const float* const kIdentityTurn = reinterpret_cast<const float*>(0x290CB30);
using ObjectFromHandleFn = BYTE*(__fastcall*)(void* world, void* unused, int handle);
auto const ObjectFromHandle = reinterpret_cast<ObjectFromHandleFn>(0x4D4360);
using ObjectFlagFn = void(__fastcall*)(BYTE* object, void* unused, bool on);
auto const ClearObjectFlagA = reinterpret_cast<ObjectFlagFn>(0x7B20A0);
auto const ClearObjectFlagB = reinterpret_cast<ObjectFlagFn>(0x4F8DA0);
using StopEffectFn = void(__fastcall*)(BYTE* object, void* unused, int style);
auto const StopEffect = reinterpret_cast<StopEffectFn>(0x4F7AA0);
constexpr size_t kObjectPlace = 0x04, kObjectTurn = 0x10, kObjectFlags = 0x57;

// The effects' wind filter ([0x26BF284]: its turn at +0x50, which the game's effects update sets from
// the wind's direction; Guerrilla's at +0x54).
BYTE* const* const kWindFilter = reinterpret_cast<BYTE* const*>(0x26BF284);
constexpr size_t kWindFilterTurn = 0x50;

// The effects' expression variables (0x124FAEC: each a pointer to its record, its name's hash at +0x0C;
// number 4 is wind_los, as in Guerrilla) and setting one for every effect (0xB63140: the name's hash and
// the value; Guerrilla's vfx_expressions_set_global_variable).
BYTE* const* const kExpressionVariables = reinterpret_cast<BYTE* const*>(0x124FAEC);
constexpr int kWindLos = 4;
using SetEffectVariableFn = void(__cdecl*)(DWORD nameHash, float value);
auto const SetEffectVariable = reinterpret_cast<SetEffectVariableFn>(0xB63140);

// A ray through the world (0x7993C0, cdecl: the ray, its hits; true if it hit), as Guerrilla's
// havok_raycast with one more word: from, to, its radius, +0x1C −1 (an object it passes through, it
// seems), +0x20 and +0x24 0, the collision filter +0x28 (0x1975F00: layer 28, Guerrilla's
// COLLIDE_RAY_WORLD_ONLY), which hits +0x2C (0: the nearest), +0x30 0; 0x424AD0 sets those defaults
// (the game's own filter, layer 26). Its hits come back in a Havok array of Guerrilla's hit_info (0x38
// bytes: where it hit, ..., +0x24 how far along the ray), here one of the mod's own the game may fill
// but not free (its capacity's top bit set).
struct Ray {
    float from[3], to[3];
    float radius;
    int passThrough;
    DWORD unknown20, unknown24, filter, nearest, unknown30;
};
static_assert(sizeof(Ray) == 0x34, "the game's ray input");
struct RayHit {
    float at[3], middle[3], normal[3];
    float along;
    DWORD rest[4];
};
static_assert(sizeof(RayHit) == 0x38, "the game's ray hit");
struct RayHits {
    RayHit* hits;
    int count;
    DWORD capacityAndFlags;
};
using RayInitFn = void(__fastcall*)(Ray* ray, void* unused);
auto const RayInit = reinterpret_cast<RayInitFn>(0x424AD0);
using CastRayFn = bool(__cdecl*)(const Ray* ray, RayHits* hits);
auto const CastRay = reinterpret_cast<CastRayFn>(0x7993C0);
auto& g_worldOnlyRays = *reinterpret_cast<DWORD*>(0x1975F00);
auto& g_buildingRays = *reinterpret_cast<DWORD*>(0x1975EF0);

// Guerrilla's 26 directions round the listener, in its order, and their weights.
struct SkyDirection { float x, y, z, weight; };
constexpr SkyDirection kSkyDirections[26] = {
    {0, 1, 0, 2},           {0, -1, 0, 0.1f},          {-1, 0.707f, 0, 1},         {1, -0.353f, 0, 0.1f},
    {0, 0, 1, 0.5f},        {0, 0, -1, 0.5f},          {1, 0.707f, 0, 1},          {-1, -0.353f, 0, 0.1f},
    {0, 0.707f, -1, 1},     {0, -0.353f, 1, 0.1f},     {-1, 0, 0, 0.5f},           {1, 0, 0, 0.5f},
    {0, -0.353f, -1, 0.1f}, {0, 0.707f, 1, 1},         {-0.707f, 0.707f, 0.707f, 1}, {0.707f, -0.353f, -0.707f, 0.1f},
    {0.707f, 0.707f, 0.707f, 1}, {-0.707f, -0.353f, -0.707f, 0.1f}, {-0.707f, 0, 0.707f, 0.5f}, {0.707f, 0, -0.707f, 0.5f},
    {-0.707f, 0.707f, -0.707f, 1}, {0.707f, -0.353f, 0.707f, 0.1f}, {0.707f, 0.707f, -0.707f, 1}, {-0.707f, -0.353f, 0.707f, 0.1f},
    {0.707f, 0, 0.707f, 0.5f},   {-0.707f, 0, -0.707f, 0.5f},
};
constexpr float kSkyRayLength = 50, kSkyBlockedWithin = 20;
constexpr float kMphToMetresPerSecond = 0.44704f;
constexpr float kDustMaxOffset = 0.25f;  // weather.xtbl's Dust_max_offset, 25 (%)
constexpr const char* kDustEffect = "rfg_env_dust_camera_a";

bool g_skyBlocked[26] = {};
int g_skyNext = 0;
int g_dustHandle = -1;
int g_dustEffect = -2;  // −2: not looked up yet; −1: the world has none
bool g_dustLogged = false;
float g_windLosNow = 0;  // wind_los as last set

// How far a ray from the camera along one of the directions first hits, in metres, or −1.
float CameraRayHit(const SkyDirection& direction, DWORD filter) {
    Ray ray;
    RayInit(&ray, nullptr);
    for (int i = 0; i < 3; ++i) ray.from[i] = g_cameraPosition[i];
    ray.to[0] = g_cameraPosition[0] + direction.x * kSkyRayLength;
    ray.to[1] = g_cameraPosition[1] + direction.y * kSkyRayLength;
    ray.to[2] = g_cameraPosition[2] + direction.z * kSkyRayLength;
    ray.radius = 0.1f;
    ray.filter = filter;
    ray.nearest = 0;
    RayHit hits[4] = {};
    RayHits found = {hits, 0, 0x80000000u | 4};
    if (!CastRay(&ray, &found) || found.count <= 0 || !found.hits) return -1;
    return found.hits[0].along * kSkyRayLength;
}

// One more of the 26 rays; the share of the sky blocked round the camera. Guerrilla's one ray of the
// world (its terrain and buildings) is two in Armageddon: [0x1975F00] (layer 0x1C) sees the terrain
// but goes through the buildings, which [0x1975EF0] (layer 0x31, Guerrilla's
// COLLIDE_RAY_RFG_DESTROYABLE by its place in the list) sees; the camera's roof at 5.5-7 m was hit by
// the latter and missed by the former.
float SkyBlocked() {
    const SkyDirection& direction = kSkyDirections[g_skyNext];
    const float ground = CameraRayHit(direction, g_worldOnlyRays);
    const float building = ground >= 0 && ground < kSkyBlockedWithin ? ground : CameraRayHit(direction, g_buildingRays);
    g_skyBlocked[g_skyNext] = building >= 0 && building < kSkyBlockedWithin;
    g_skyNext = (g_skyNext + 1) % 26;
    float blocked = 0, all = 0;
    for (int i = 0; i < 26; ++i) {
        all += kSkyDirections[i].weight;
        if (g_skyBlocked[i]) blocked += kSkyDirections[i].weight;
    }
    return blocked / all;
}

void StopDust() {
    if (g_dustHandle == -1) return;
    if (BYTE* effect = ObjectFromHandle(kWorld, nullptr, g_dustHandle)) StopEffect(effect, nullptr, 1);
    g_dustHandle = -1;
}

void DustUpdate() {
    if (!InOurWorld()) {
        g_dustHandle = -1;  // (gone with the world's objects)
        g_dustLogged = false;
        return;
    }
    if (!g_dust) {
        StopDust();
        return;
    }
    float speed = 0, direction = 0;
    Wind(&speed, &direction);
    const float open = 1 - SkyBlocked();
    const float windLos = (open > 0 ? open : 0) * speed * kMphToMetresPerSecond;
    if (const BYTE* variable = kExpressionVariables[kWindLos]) SetEffectVariable(*reinterpret_cast<const DWORD*>(variable + 0x0C), windLos);
    g_windLosNow = windLos;

    if (g_dustEffect == -2) {
        g_dustEffect = EffectNumber(kDustEffect);
        if (g_dustEffect < 0) rfa::Log("Dust: the game has no effect %s (build the world again with this version's editor\\GuerrillaBuilder.exe); no dust", kDustEffect);
    }
    if (g_dustEffect < 0) return;
    BYTE* effect = g_dustHandle != -1 ? ObjectFromHandle(kWorld, nullptr, g_dustHandle) : nullptr;
    if (!effect) {
        g_dustHandle = EffectsPlay(g_dustEffect, g_cameraPosition, kIdentityTurn, true, -1, -1, -1, false, 0, 0);
        effect = g_dustHandle != -1 ? ObjectFromHandle(kWorld, nullptr, g_dustHandle) : nullptr;
        if (!effect) {
            RFA_LOG_FIRST(3, "Dust: %s didn't play (handle %d)", kDustEffect, g_dustHandle);
            g_dustHandle = -1;
            return;
        }
        effect[kObjectFlags] |= 8;
        ClearObjectFlagA(effect, nullptr, false);
        ClearObjectFlagB(effect, nullptr, false);
        if (!g_dustLogged) {
            g_dustLogged = true;
            rfa::Log("Dust: %s plays round the camera; the wind %.0f mph (top %.0f) from %.0f degrees, %.0f %% of the sky open, wind_los %.1f",
                     kDustEffect, speed, g_windTopSpeed, direction, open * 100, windLos);
        }
    }
    // At the camera, turned as the effects' wind blows, upwind by up to a quarter of its radius.
    float* place = reinterpret_cast<float*>(effect + kObjectPlace);
    float* turn = reinterpret_cast<float*>(effect + kObjectTurn);
    const BYTE* filter = *kWindFilter;
    if (filter) memcpy(turn, filter + kWindFilterTurn, 9 * sizeof(float));
    const BYTE* entry = EffectEntry(g_dustEffect);
    const BYTE* visual = entry ? Visual(*reinterpret_cast<const int*>(entry + 0x44)) : nullptr;
    const float radius = visual ? *reinterpret_cast<const float*>(visual + 0x84) : 0;
    const float upwind = g_windTopSpeed > 0 ? radius * kDustMaxOffset * speed / g_windTopSpeed : 0;
    for (int i = 0; i < 3; ++i) place[i] = g_cameraPosition[i] - upwind * turn[6 + i];
    effect[kObjectFlags] |= 8;
}

// ---- Guerrilla's particle light

// Guerrilla lights a particle with the time of day's particle_ambient_color times its emitter's own
// ambient colour (rl_particle_renderer::render: Ambient_color = the scene's particle ambient × the
// material's). Armageddon takes the emitter's ambient colour as the light itself: its artists cut
// Guerrilla's 0.4-0.6 greys to about 0.01 when they ported the effects both games have, and with the
// world's ambient light at 0 Guerrilla's dust haze (ambient 1) still drew white. So in our worlds the
// mod gives each emitter of Guerrilla's loaded effects (the rfg_ files the world builder adds) that
// product each frame: the time of day's particle_ambient_color (its sets' curves at +0x2A40, blended
// as the game blends them; the block the game hands the renderer, 0x1973924, reads 0 in our worlds)
// times Guerrilla's own ambient, kept from when the effect was first seen; nothing while no set is
// loaded. A loaded effect (the game's map by file name, LoadedEffect) keeps its data at +0x0C: the file
// as loaded, its offsets made pointers (signature +0, version 36 +4, the emitters' count +0x30 and
// heads +0x34, 0x40 bytes each, the material at +0x1C; the material's ambient colour at +0x44).
constexpr size_t kParticleAmbient = 0x2A40;  // particle_ambient_color
constexpr DWORD kEffectSignature = 0x57423137, kEffectVersion = 36;
constexpr size_t kEffectData = 0x0C, kEffectEmitterCount = 0x30, kEffectEmitters = 0x34, kEmitterHeadSize = 0x40,
                 kEmitterMaterial = 0x1C, kMaterialAmbient = 0x44;
struct Colour3 { float r, g, b; };
// An emitter as converted: its ambient colour, and its four colour keys' opacity (block +0x54, +0x64,
// +0x78, +0x8C: start, mid A, mid B, end, each red, green, blue, opacity; taken as particles are made).
struct EmitterOwn {
    Colour3 ambient;
    float opacity[4];
};
constexpr size_t kEmitterBlock = 0x18;
constexpr size_t kBlockColours[4] = {0x54, 0x64, 0x78, 0x8C};
std::vector<std::pair<std::string, std::vector<EmitterOwn>>> g_guerrillaAmbients;  // by file, emitter by emitter
bool g_particleLightLogged = false;

// Guerrilla's emitters' opacity scale (vfx_particle_system_emitter::m_opacity_scale): its effects'
// clerp_opacity_scale expressions set it from a variable (wind_los for each haze layer of the camera
// dust, each from its own threshold), and as it draws each frame its renderer takes the emitter's
// colour keys' opacity times it (vfx_effect::update_render), so the particles fade at once. Armageddon
// keeps the function (0xB63C30, writing a scale of its own) but not the multiplying (none of its own
// effects use it): with the haze at full strength indoors, our rooms filled with dust. So the mod works
// out each emitter's scale from those of its expressions on wind_los (the one variable the mod sets for
// every effect alike; the others, such as a vehicle's speed, are each object's own) as Guerrilla's
// clerp does, and gives its colour keys their opacity times it. A loaded effect's expressions: +0x08
// their count, +0x0C (a pointer by then) 0x14 bytes each: the function's name's hash +0x00, the emitter
// +0x08, the parameters' count +0x0C and (a pointer) +0x10, 0x0C bytes each: whether a variable +0x00,
// its name's hash +0x04, else a pointer to the value +0x08 (the variable, the range it's taken from and
// the one it's taken to).
constexpr DWORD kClerpOpacityScale = 0x4B730832, kWindLosHash = 0x1F3D5446;
constexpr size_t kEffectExpressionCount = 0x08, kEffectExpressions = 0x0C, kExpressionSize = 0x14, kParameterSize = 0x0C;

// A pointer the game's loader made of a file's offset, or the offset still.
template <typename T>
const T* InEffect(const BYTE* data, const void* field) {
    const BYTE* at = *reinterpret_cast<const BYTE* const*>(field);
    if (at && at < data) at = data + reinterpret_cast<uintptr_t>(at);
    return reinterpret_cast<const T*>(at);
}

// Guerrilla's clerp: the value from low to high (clamped) taken to from to to.
float Clerp(float value, float low, float high, float from, float to) {
    if (high <= low) return value < high ? from : to;
    return from + (to - from) * std::clamp((value - low) / (high - low), 0.0f, 1.0f);
}

// Each emitter's opacity scale in a loaded effect, 1 where none is set.
void OpacityScales(const BYTE* data, int emitters, std::vector<float>& scales) {
    scales.assign(emitters, 1.0f);
    const int count = *reinterpret_cast<const int*>(data + kEffectExpressionCount);
    const BYTE* expressions = count > 0 && count <= 256 ? InEffect<BYTE>(data, data + kEffectExpressions) : nullptr;
    for (int i = 0; expressions && i < count; ++i) {
        const BYTE* expression = expressions + i * kExpressionSize;
        const int emitter = *reinterpret_cast<const int*>(expression + 0x08);
        const int parameterCount = *reinterpret_cast<const int*>(expression + 0x0C);
        if (*reinterpret_cast<const DWORD*>(expression) != kClerpOpacityScale || emitter < 0 || emitter >= emitters || parameterCount < 5) continue;
        const BYTE* parameters = InEffect<BYTE>(data, expression + 0x10);
        if (!parameters || !parameters[0] || *reinterpret_cast<const DWORD*>(parameters + 4) != kWindLosHash) continue;
        float value[4];
        bool constants = true;
        for (int k = 0; k < 4; ++k) {
            const BYTE* parameter = parameters + (k + 1) * kParameterSize;
            const float* number = parameter[0] ? nullptr : InEffect<float>(data, parameter + 8);
            if (!number) constants = false;
            else value[k] = *number;
        }
        if (constants) scales[emitter] = Clerp(g_windLosNow, value[0], value[1], value[2], value[3]);
    }
}

void GuerrillaParticleLight() {
    float light[3];
    TodColour(kParticleAmbient, light);
    const bool lighting = light[0] != 0 || light[1] != 0 || light[2] != 0;
    int lit = 0;
    std::vector<float> scales;
    for (const std::string& file : g_ourEffectFiles) {
        const BYTE* effect = static_cast<const BYTE*>(LoadedEffect(file.c_str()));
        BYTE* data = effect ? *reinterpret_cast<BYTE* const*>(effect + kEffectData) : nullptr;
        if (!data) continue;
        if (*reinterpret_cast<const DWORD*>(data) != kEffectSignature || *reinterpret_cast<const DWORD*>(data + 4) != kEffectVersion) {
            RFA_LOG_FIRST(3, "Particles: %s isn't laid out as expected in memory; its light left as it is", file.c_str());
            continue;
        }
        const int count = *reinterpret_cast<const int*>(data + kEffectEmitterCount);
        BYTE* heads = *reinterpret_cast<BYTE* const*>(data + kEffectEmitters);
        if (count <= 0 || count > 256 || !heads) continue;
        auto kept = std::find_if(g_guerrillaAmbients.begin(), g_guerrillaAmbients.end(), [&](const auto& entry) { return entry.first == file; });
        if (kept == g_guerrillaAmbients.end()) {
            // First seen: as converted from Guerrilla's file.
            std::vector<EmitterOwn> own(count, EmitterOwn{});
            for (int i = 0; i < count; ++i) {
                const BYTE* head = heads + i * kEmitterHeadSize;
                if (const BYTE* material = *reinterpret_cast<BYTE* const*>(head + kEmitterMaterial))
                    own[i].ambient = *reinterpret_cast<const Colour3*>(material + kMaterialAmbient);
                if (const BYTE* block = *reinterpret_cast<BYTE* const*>(head + kEmitterBlock))
                    for (int k = 0; k < 4; ++k) own[i].opacity[k] = *reinterpret_cast<const float*>(block + kBlockColours[k] + 12);
            }
            g_guerrillaAmbients.emplace_back(file, std::move(own));
            kept = g_guerrillaAmbients.end() - 1;
        }
        OpacityScales(data, count, scales);
        for (int i = 0; i < count && i < static_cast<int>(kept->second.size()); ++i) {
            BYTE* head = heads + i * kEmitterHeadSize;
            const EmitterOwn& own = kept->second[i];
            BYTE* material = *reinterpret_cast<BYTE* const*>(head + kEmitterMaterial);
            if (material && lighting) {
                auto* ambient = reinterpret_cast<Colour3*>(material + kMaterialAmbient);
                *ambient = Colour3{light[0] * own.ambient.r, light[1] * own.ambient.g, light[2] * own.ambient.b};
                ++lit;
            }
            if (BYTE* block = *reinterpret_cast<BYTE* const*>(head + kEmitterBlock))
                for (int k = 0; k < 4; ++k) *reinterpret_cast<float*>(block + kBlockColours[k] + 12) = own.opacity[k] * scales[i];
        }
    }
    if (lit && !g_particleLightLogged) {
        g_particleLightLogged = true;
        rfa::Log("Particles: Guerrilla's particle light: %d emitters lit by the time of day's particle ambient (%.3f %.3f %.3f) times their own",
                 lit, light[0], light[1], light[2]);
    }
}

// ---- Guerrilla's gusts
//
// Guerrilla's dust (dust_update, RM 0x6EBBF0) also plays gusts on the ground ahead of the player: every 0.2 s, while
// the particles in use are under half of all it has (particles_parametric_max less the manager's free ones,
// vfx_particle_system_manager::get_num_particles_parametric_free; Dust_gust_parametric_threshold 0.5), it picks a
// place: at even odds near (6 m ahead along the player's forward, then up to 30 m either way across x and z) or far
// (70 m ahead, up to 50 m). There it asks for the ground (get_terrain_effect_material, 0x6E1900): the terrain's height,
// and at four points 4 m off diagonally, given up if any is more than 0.5 m off the first (a slope), the gust put at
// the highest; then the effect material of the zone there (the zone's get_effect_material, its virtual +0x60 with
// -1, the place, 0). Of that material's gusts of that range (rfg_effect_materials.xtbl's Gust_Effects), the first
// whose chance beats a roll plays, else the likeliest; with no turn, tinted with the material's tint if it has one.
// Armageddon has the same particle manager (its pointer at 0x26BF27C; parametric particles at most 0x1111300, the
// free ones counted at +0x74), the same height query (0x7748E0), the zone at a place (0x6CB3C0) and its effect
// material (Armageddon's object virtual +0x68, as the glass code's), effects_play (0x4EB460) and an effect's tint
// (0x4F8370), but no gusts. The world builder writes Guerrilla's gust lists to mods\guerrilla_world\gusts.txt
// (GuerrillaGusts) and adds their effects; in our worlds the mod plays them with the dust (Dust=), as Guerrilla's.
// (Guerrilla also takes a gust away at once if its effect object isn't a streamed one, which with the ground loaded
// there it is; not done here.)
constexpr float kGustEvery = 0.2f, kGustParticleShare = 0.5f, kGustNearChance = 0.5f;
constexpr float kGustNearAhead = 6, kGustNearSpread = 30, kGustFarAhead = 70, kGustFarSpread = 50;
constexpr float kGustSlopeRange = 4, kGustSlopeMost = 0.5f;
BYTE* const* const kParticleManager = reinterpret_cast<BYTE* const*>(0x26BF27C);
const int* const kParametricParticlesMost = reinterpret_cast<const int*>(0x1111300);
constexpr size_t kParametricParticlesFree = 0x74;
constexpr size_t kObjectForward = 0x28;          // an object's turn, rows from +0x10: its forward axis
constexpr size_t kZoneMaterialSlot = 0x68 / 4;   // an object's method: its effect material at a piece and place
constexpr size_t kMaterialTintColour = 0x24, kMaterialTintIsOn = 0x34;  // an effect material's tint (4 floats) and whether it has one
using GustGroundFn = bool(__cdecl*)(float* height, float x, float z, float* normal, int flags);
auto const GustGround = reinterpret_cast<GustGroundFn>(0x7748E0);
using GustZoneMaterialFn = BYTE(__fastcall*)(void* zone, void* unused, int piece, const float* at, int zero);
using GustRandomFn = float(__cdecl*)(float low, float high);
auto const GustRandom = reinterpret_cast<GustRandomFn>(0xC8BC00);
using SetEffectTintFn = void(__fastcall*)(BYTE* effect, void* unused, const float* colour);
auto const SetEffectTint = reinterpret_cast<SetEffectTintFn>(0x4F8370);
const char* EffectMaterialName(DWORD number);  // (below, with Guerrilla's material events)
BYTE* EffectMaterialRecord(DWORD number);

struct GustEffect {
    std::string name;
    int number = -2;  // its number in effects.xtbl, once asked (-1: the game hasn't it)
    float chance;     // 0 to 1
    bool close;       // near the player (Guerrilla's Near), else far off
    float fadeIn;
};
struct GustMaterial {
    std::string material;
    std::vector<GustEffect> gusts;
};
std::vector<GustMaterial> g_gustMaterials;
float g_gustClock = 0;
bool g_gustsLoaded = false;

void LoadGusts() {
    g_gustsLoaded = true;
    std::vector<BYTE> file;
    if (!ReadWholeFile(DataFolder() + L"gusts.txt", file) || file.empty()) {
        rfa::Log("Gusts: no gusts.txt from the world builder; no gusts");
        return;
    }
    const std::string text(file.begin(), file.end());
    size_t count = 0;
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        std::vector<std::string> words;
        for (size_t from = 0; from <= line.size();) {
            size_t tab = line.find('\t', from);
            if (tab == std::string::npos) tab = line.size();
            words.push_back(line.substr(from, tab - from));
            from = tab + 1;
        }
        if (words.size() == 2 && words[0] == "material") g_gustMaterials.push_back({words[1], {}});
        else if (words.size() == 5 && words[0] == "gust" && !g_gustMaterials.empty()) {
            GustEffect gust;
            gust.name = words[1];
            gust.chance = static_cast<float>(atof(words[2].c_str()));
            gust.close = words[3] == "1";
            gust.fadeIn = static_cast<float>(atof(words[4].c_str()));
            g_gustMaterials.back().gusts.push_back(gust);
            ++count;
        }
    }
    rfa::Log("Gusts: %zu of Guerrilla's on %zu materials (gusts.txt); played ahead of the player in our worlds", count, g_gustMaterials.size());
}

// The ground at a place as Guerrilla's gusts take it: false on a slope or where no ground is loaded; else the place
// raised to the highest of its five heights, and the zone's effect material there.
bool GustGroundAt(float (&place)[3], BYTE& material) {
    float first = place[1], normal[3];
    if (!GustGround(&first, place[0], place[2], normal, 0)) return false;
    static const float kCorners[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    float highest = first;
    for (const auto& corner : kCorners) {
        float height = place[1];
        if (!GustGround(&height, place[0] + corner[0] * kGustSlopeRange, place[2] + corner[1] * kGustSlopeRange, normal, 0)) return false;
        if (fabsf(height - first) > kGustSlopeMost) return false;
        if (height > highest) highest = height;
    }
    place[1] = highest;
    BYTE* zone = ZoneAt(kWorld, nullptr, place);
    if (!zone) return false;
    auto const zoneMaterial = reinterpret_cast<GustZoneMaterialFn>((*reinterpret_cast<void***>(zone))[kZoneMaterialSlot]);
    material = zoneMaterial(zone, nullptr, -1, place, 0);
    return true;
}

// The gust to play from a material's list for this range (Guerrilla's: the first whose chance beats a roll, else the
// likeliest), or null.
GustEffect* PickGust(GustMaterial& material, bool close) {
    for (GustEffect& gust : material.gusts)
        if (gust.close == close && gust.chance >= GustRandom(0, 1)) return &gust;
    GustEffect* likeliest = nullptr;
    for (GustEffect& gust : material.gusts)
        if (gust.close == close && (!likeliest || gust.chance >= likeliest->chance)) likeliest = &gust;
    return likeliest;
}

// Each frame in our worlds, with the dust: Guerrilla's gusts.
void GustUpdate(float seconds) {
    if (!InOurWorld() || !g_dust) {
        g_gustClock = 0;
        return;
    }
    if (!g_gustsLoaded) LoadGusts();
    if (g_gustMaterials.empty()) return;
    g_gustClock += seconds;
    if (g_gustClock < kGustEvery) return;
    g_gustClock = 0;
    // The particles in use, as a share of all of them.
    const BYTE* manager = *kParticleManager;
    const int most = *kParametricParticlesMost;
    if (!manager || most <= 0) return;
    const int used = most - *reinterpret_cast<const int*>(manager + kParametricParticlesFree);
    if (static_cast<float>(used) / static_cast<float>(most) >= kGustParticleShare) return;
    // A place ahead of the player.
    const BYTE* player = LocalPlayer();
    if (!player) return;
    const bool close = GustRandom(0, 1) <= kGustNearChance;
    const float ahead = close ? kGustNearAhead : kGustFarAhead, spread = close ? kGustNearSpread : kGustFarSpread;
    const float* at = reinterpret_cast<const float*>(player + kPlayerPlace);
    const float* forward = reinterpret_cast<const float*>(player + kObjectForward);
    float place[3] = {at[0] + forward[0] * ahead + GustRandom(-spread, spread), at[1] + forward[1] * ahead,
                      at[2] + forward[2] * ahead + GustRandom(-spread, spread)};
    BYTE material = 0xFF;
    if (!GustGroundAt(place, material) || material == 0xFF) return;
    const char* name = EffectMaterialName(material);
    if (!name) return;
    auto found = std::find_if(g_gustMaterials.begin(), g_gustMaterials.end(), [&](const GustMaterial& m) { return _stricmp(m.material.c_str(), name) == 0; });
    if (found == g_gustMaterials.end()) return;
    GustEffect* gust = PickGust(*found, close);
    if (!gust) return;
    if (gust->number == -2) gust->number = EffectNumber(gust->name.c_str());
    if (gust->number < 0) return;
    const int handle = EffectsPlay(gust->number, place, kIdentityTurn, false, -1, -1, -1, false, 0, 0);
    BYTE* effect = handle != -1 ? ObjectFromHandle(kWorld, nullptr, handle) : nullptr;
    if (!effect) return;
    const BYTE* record = EffectMaterialRecord(material);
    if (record && record[kMaterialTintIsOn]) SetEffectTint(effect, nullptr, reinterpret_cast<const float*>(record + kMaterialTintColour));
    RFA_LOG_DETAIL_FIRST(30, "Gusts: %s on %s at %.0f %.0f %.0f (%s; particles in use %d of %d)", gust->name.c_str(), name, place[0], place[1], place[2],
                         close ? "near" : "far", used, most);
}

void GlassUpdate();  // (below, with Guerrilla's broken glass)
void BuildingsUpdate();  // (below, with Guerrilla's buildings)
void TurbinesUpdate(float seconds);  // (below, with Guerrilla's wind turbines)

void __cdecl EffectsUpdate(float seconds) {
    // The watched effect sounds' ends: once a frame (each look at a sound is a walk through all the game's sound
    // objects; see "How long an effect's sound played").
    LogEndedSounds();
    if (InOurWorld()) GuerrillaWindUpdate(seconds);
    else g_wind.started = false;
    GlassUpdate();
    BuildingsUpdate();
    TurbinesUpdate(seconds);
    g_effectsUpdate(seconds);
    DustUpdate();
    GustUpdate(seconds);
    if (InOurWorld()) GuerrillaParticleLight();
}

// ---- Road log (RoadLog=1, a diagnosis; off by default)

// Our roads' edges show one even colour where they should hand over to the ground beside them.
// Armageddon's ground and road vertex shaders both place the ground's layers and its blend map by
// two constants the engine sets in their per-object buffer (cb2): Terrain_uv_params (register 6)
// and Terrain_subzone_offset (23), from the world position (objTM, registers 15-17, plus
// Render_offset, 18). The road's own texture doesn't use them, so if the engine doesn't set them
// for a road, only its edges go wrong. With RoadLog=1 the mod notes the game's terrain vertex
// shaders as the game makes them (by the constants they name: a road's names Edge_fade__use_spec_map),
// keeps a copy of what the game writes into each constant buffer (Map/Unmap and UpdateSubresource),
// and at each draw with a terrain shader logs that buffer's registers when their values are new, on
// the game's main context and any deferred one it makes. Every few seconds in our worlds it also
// logs how many draws of each kind it saw, so a log without values still says why.
bool g_roadLog = false;
using CreateVertexShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device* device, const void* code, SIZE_T size,
                                                         ID3D11ClassLinkage* linkage, ID3D11VertexShader** shader);
using CreateDeferredContextFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device* device, UINT flags, ID3D11DeviceContext** context);
using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext* context, UINT count, UINT first, INT base);
using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext* context, UINT count, UINT instances, UINT first,
                                                        INT base, UINT firstInstance);
using MapFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type,
                                          UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped);
using UnmapFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource);
using UpdateSubresourceFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource,
                                                     const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch);
// Slots in the methods' tables: ID3D11Device after IUnknown's 3 (CreateBuffer is 3); ID3D11DeviceContext
// after IUnknown's 3 and ID3D11DeviceChild's 4 (VSSetConstantBuffers is 7).
constexpr int kCreateVertexShaderSlot = 12, kCreateDeferredContextSlot = 27;
constexpr int kDrawIndexedSlot = 12, kMapSlot = 14, kUnmapSlot = 15, kDrawIndexedInstancedSlot = 20, kUpdateSubresourceSlot = 48;
CreateVertexShaderFn g_createVertexShader = nullptr;
CreateDeferredContextFn g_createDeferredContext = nullptr;
// The context methods as the game's main context has them; a deferred context's table is another,
// with its own originals.
struct ContextMethods {
    void** table;
    DrawIndexedFn drawIndexed;
    DrawIndexedInstancedFn drawIndexedInstanced;
    MapFn map;
    UnmapFn unmap;
    UpdateSubresourceFn updateSubresource;
};
std::vector<ContextMethods> g_contextMethods;
ContextMethods* MethodsOf(ID3D11DeviceContext* context) {
    void** table = *reinterpret_cast<void***>(context);
    for (ContextMethods& methods : g_contextMethods)
        if (methods.table == table) return &methods;
    return nullptr;
}

constexpr UINT kTerrainRegisters = 24;
struct Constants {
    ID3D11Resource* buffer;
    float values[kTerrainRegisters * 4];
};
struct Mapped {
    ID3D11DeviceContext* context;
    ID3D11Resource* buffer;
    void* data;
};
SRWLOCK g_roadLogLock = SRWLOCK_INIT;
std::vector<ID3D11VertexShader*> g_roadShaders, g_groundShaders;
std::vector<Constants> g_constants;  // the last values written into each constant buffer big enough
std::unordered_map<ID3D11Resource*, size_t> g_constantIndex;  // where each buffer's are in g_constants
std::vector<Mapped> g_mapped;        // buffers mapped for writing, not yet unmapped
struct LoggedTerrain {
    bool road;
    float values[7];  // Terrain_uv_params, Terrain_subzone_offset
};
std::vector<LoggedTerrain> g_roadLogged;
int g_roadLogLines = 0, g_roadLogSummaries = 0;
constexpr int kRoadLogLines = 120, kRoadLogSummaries = 16;
long g_vertexShadersMade = 0, g_deferredContexts = 0, g_drawsIndexed = 0, g_drawsInstanced = 0, g_roadDraws = 0, g_groundDraws = 0,
     g_terrainWithoutValues = 0, g_constantWrites = 0, g_drawRewraps = 0;
ULONGLONG g_roadLogSummaryAt = 0;
ULONGLONG g_inWorldSince = 0;              // when the log first saw our world
constexpr ULONGLONG kRoadLogTime = 60000;  // how long it logs there, in ms

bool Names(const void* code, SIZE_T size, const char* name) {
    const char* start = static_cast<const char*>(code);
    const size_t length = strlen(name);
    return std::search(start, start + size, name, name + length) != start + size;
}

// A constant buffer of at least the terrain's 24 registers, or null.
ID3D11Buffer* BigConstantBuffer(ID3D11Resource* resource) {
    if (!resource) return nullptr;
    D3D11_RESOURCE_DIMENSION dimension;
    resource->GetType(&dimension);
    if (dimension != D3D11_RESOURCE_DIMENSION_BUFFER) return nullptr;
    auto* buffer = static_cast<ID3D11Buffer*>(resource);
    D3D11_BUFFER_DESC desc;
    buffer->GetDesc(&desc);
    return (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) && desc.ByteWidth >= kTerrainRegisters * 16 ? buffer : nullptr;
}

bool RoadLogging();
void SummariseRoadLog();
void KeepDrawSlots(ID3D11DeviceContext* context);

// Keeps what the game wrote into a constant buffer, only while the log is still writing in our
// worlds: reading back a buffer the game maps for writing is slow.
void KeepConstants(ID3D11Resource* buffer, const void* data) {
    if (!RoadLogging()) return;
    AcquireSRWLockExclusive(&g_roadLogLock);
    ++g_constantWrites;
    auto found = g_constantIndex.find(buffer);
    Constants* kept;
    if (found != g_constantIndex.end()) {
        kept = &g_constants[found->second];
    } else if (g_constants.size() < g_constants.capacity()) {
        g_constantIndex[buffer] = g_constants.size();
        g_constants.push_back({buffer, {}});
        kept = &g_constants.back();
    } else {
        kept = nullptr;
    }
    if (kept) memcpy(kept->values, data, sizeof(kept->values));
    ReleaseSRWLockExclusive(&g_roadLogLock);
}

HRESULT STDMETHODCALLTYPE MapNoted(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type, UINT flags,
                                   D3D11_MAPPED_SUBRESOURCE* mapped) {
    RFA_LOG_DETAIL_FIRST(1, "Road log: the first Map through the log's wrapper (in our world: %d)", InOurWorld());
    SummariseRoadLog();
    KeepDrawSlots(context);
    const ContextMethods* methods = MethodsOf(context);
    const HRESULT result = methods->map(context, resource, subresource, type, flags, mapped);
    if (SUCCEEDED(result) && mapped && mapped->pData && (type == D3D11_MAP_WRITE_DISCARD || type == D3D11_MAP_WRITE_NO_OVERWRITE) &&
        RoadLogging() && BigConstantBuffer(resource)) {
        AcquireSRWLockExclusive(&g_roadLogLock);
        g_mapped.push_back({context, resource, mapped->pData});
        ReleaseSRWLockExclusive(&g_roadLogLock);
    }
    return result;
}

void STDMETHODCALLTYPE UnmapNoted(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource) {
    void* data = nullptr;
    AcquireSRWLockExclusive(&g_roadLogLock);
    for (size_t i = 0; i < g_mapped.size(); ++i)
        if (g_mapped[i].context == context && g_mapped[i].buffer == resource) {
            data = g_mapped[i].data;
            g_mapped.erase(g_mapped.begin() + i);
            break;
        }
    ReleaseSRWLockExclusive(&g_roadLogLock);
    if (data) KeepConstants(resource, data);  // still mapped: read before the game's Unmap
    MethodsOf(context)->unmap(context, resource, subresource);
}

void STDMETHODCALLTYPE UpdateSubresourceNoted(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, const D3D11_BOX* box,
                                              const void* data, UINT rowPitch, UINT depthPitch) {
    if (data && !box && BigConstantBuffer(resource)) KeepConstants(resource, data);
    MethodsOf(context)->updateSubresource(context, resource, subresource, box, data, rowPitch, depthPitch);
}

HRESULT STDMETHODCALLTYPE CreateVertexShaderNoted(ID3D11Device* device, const void* code, SIZE_T size, ID3D11ClassLinkage* linkage,
                                                  ID3D11VertexShader** shader) {
    const HRESULT result = g_createVertexShader(device, code, size, linkage, shader);
    RFA_LOG_DETAIL_FIRST(1, "Road log: the first CreateVertexShader through the log's wrapper");
    if (SUCCEEDED(result) && shader && *shader && code) {
        AcquireSRWLockExclusive(&g_roadLogLock);
        ++g_vertexShadersMade;
        if (Names(code, size, "Terrain_uv_params")) {
            if (Names(code, size, "Edge_fade__use_spec_map")) g_roadShaders.push_back(*shader);
            else g_groundShaders.push_back(*shader);
        }
        ReleaseSRWLockExclusive(&g_roadLogLock);
    }
    return result;
}

void STDMETHODCALLTYPE DrawIndexedLogged(ID3D11DeviceContext* context, UINT count, UINT first, INT base);
void STDMETHODCALLTYPE DrawIndexedInstancedLogged(ID3D11DeviceContext* context, UINT count, UINT instances, UINT first, INT base,
                                                  UINT firstInstance);
void** g_mainContextTable = nullptr;

// The file name of the module `address` is in ("?" if none).
const char* ModuleOf(const void* address, char (&name)[MAX_PATH]) {
    HMODULE module = nullptr;
    strcpy_s(name, "?");
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(address), &module))
        GetModuleFileNameA(module, name, MAX_PATH);
    const char* slash = strrchr(name, '\\');
    return slash ? slash + 1 : name;
}

// Every 5 s (from any wrapper, in any world): what the log has seen, whether we're in our world, and
// whether the main context's draw slots still hold the log's wrappers.
void SummariseRoadLog() {
    const ULONGLONG now = GetTickCount64();
    if (g_roadLogSummaries >= kRoadLogSummaries || now - g_roadLogSummaryAt < 5000) return;
    g_roadLogSummaryAt = now;
    ++g_roadLogSummaries;
    const bool drawOurs = g_mainContextTable && g_mainContextTable[kDrawIndexedSlot] == reinterpret_cast<void*>(&DrawIndexedLogged);
    const bool instancedOurs =
        g_mainContextTable && g_mainContextTable[kDrawIndexedInstancedSlot] == reinterpret_cast<void*>(&DrawIndexedInstancedLogged);
    char module[MAX_PATH];
    rfa::LogDetail("Road log: so far %ld vertex shaders made (%u road, %u ground), %ld deferred contexts; draws: %ld indexed, %ld instanced; "
             "with a terrain shader: %ld road, %ld ground (%ld without values kept); constant buffer writes kept: %ld; world \"%s\" "
             "(ours: %d); the draw slots still ours: %d %d (now in %s), wrapped again %ld times",
             g_vertexShadersMade, static_cast<unsigned>(g_roadShaders.size()), static_cast<unsigned>(g_groundShaders.size()),
             g_deferredContexts, g_drawsIndexed, g_drawsInstanced, g_roadDraws, g_groundDraws, g_terrainWithoutValues, g_constantWrites,
             WorldName(), InOurWorld(), drawOurs, instancedOurs,
             g_mainContextTable ? ModuleOf(g_mainContextTable[kDrawIndexedSlot], module) : "?", g_drawRewraps);
}

void LogTerrainDraw(ID3D11DeviceContext* context, bool instanced) {
    instanced ? ++g_drawsInstanced : ++g_drawsIndexed;
    ID3D11VertexShader* shader = nullptr;
    context->VSGetShader(&shader, nullptr, nullptr);
    if (!shader) return;
    AcquireSRWLockShared(&g_roadLogLock);
    const bool road = std::find(g_roadShaders.begin(), g_roadShaders.end(), shader) != g_roadShaders.end();
    const bool ground = !road && std::find(g_groundShaders.begin(), g_groundShaders.end(), shader) != g_groundShaders.end();
    ReleaseSRWLockShared(&g_roadLogLock);
    shader->Release();
    if (!road && !ground) return;
    road ? ++g_roadDraws : ++g_groundDraws;
    ID3D11Buffer* bound = nullptr;
    context->VSGetConstantBuffers(2, 1, &bound);
    if (!bound) return;
    float c[kTerrainRegisters * 4];
    bool found = false;
    AcquireSRWLockShared(&g_roadLogLock);
    auto index = g_constantIndex.find(bound);
    if (index != g_constantIndex.end()) {
        memcpy(c, g_constants[index->second].values, sizeof(c));
        found = true;
    }
    ReleaseSRWLockShared(&g_roadLogLock);
    bound->Release();
    if (!found) {
        ++g_terrainWithoutValues;
        return;
    }
    LoggedTerrain seen = {road, {c[24], c[25], c[26], c[27], c[92], c[93], c[94]}};
    AcquireSRWLockExclusive(&g_roadLogLock);
    bool known = false;
    for (const LoggedTerrain& logged : g_roadLogged)
        if (logged.road == seen.road && memcmp(logged.values, seen.values, sizeof(seen.values)) == 0) known = true;
    if (!known) g_roadLogged.push_back(seen);
    ReleaseSRWLockExclusive(&g_roadLogLock);
    if (known || g_roadLogLines >= kRoadLogLines) return;
    ++g_roadLogLines;
    rfa::LogDetail("Road log: %s%s  uv_params (%.6g, %.6g, %.6g, %.6g)  subzone_offset (%.6g, %.6g, %.6g)  objTM at (%.6g, %.6g, %.6g)  "
             "render_offset (%.6g, %.6g, %.6g)  position_scale (%.6g, %.6g, %.6g)",
             road ? "ROAD  " : "ground", instanced ? " (instanced)" : "", c[24], c[25], c[26], c[27], c[92], c[93], c[94], c[63], c[67],
             c[71], c[72], c[73], c[74], c[28], c[29], c[30]);
}

// While the log writes: in our worlds, for its first minute there, until it has its lines.
bool RoadLogging() {
    if (g_roadLogLines >= kRoadLogLines || !InOurWorld()) return false;
    const ULONGLONG now = GetTickCount64();
    if (!g_inWorldSince) g_inWorldSince = now;
    return now - g_inWorldSince < kRoadLogTime;
}

void STDMETHODCALLTYPE DrawIndexedLogged(ID3D11DeviceContext* context, UINT count, UINT first, INT base) {
    RFA_LOG_DETAIL_FIRST(1, "Road log: the first DrawIndexed through the log's wrapper (in our world: %d)", InOurWorld());
    SummariseRoadLog();
    if (RoadLogging()) LogTerrainDraw(context, false);
    MethodsOf(context)->drawIndexed(context, count, first, base);
}

void STDMETHODCALLTYPE DrawIndexedInstancedLogged(ID3D11DeviceContext* context, UINT count, UINT instances, UINT first, INT base,
                                                  UINT firstInstance) {
    RFA_LOG_DETAIL_FIRST(1, "Road log: the first DrawIndexedInstanced through the log's wrapper (in our world: %d)", InOurWorld());
    SummariseRoadLog();
    if (RoadLogging()) LogTerrainDraw(context, true);
    MethodsOf(context)->drawIndexedInstanced(context, count, instances, first, base, firstInstance);
}

// Puts `replacement` in slot `slot` of a table of methods, keeping the old one.
template <typename Fn>
bool WrapSlot(void** table, int slot, Fn replacement, Fn* original) {
    *original = reinterpret_cast<Fn>(table[slot]);
    void* value = reinterpret_cast<void*>(replacement);
    return rfa::PatchCode(&table[slot], &value, sizeof(value));
}

// Direct3D's runtime puts its own draw methods back in a context's table now and then (it swaps
// between versions of them as the context's state changes; the first road logs lost theirs within
// seconds, while Map kept its wrapper). So while the log writes, each Map checks the two draw slots
// and, where the runtime has put one back, takes that as the original and wraps it again.
void KeepDrawSlots(ID3D11DeviceContext* context) {
    if (!RoadLogging()) return;
    ContextMethods* methods = MethodsOf(context);
    if (!methods) return;
    void** table = methods->table;
    if (table[kDrawIndexedSlot] != reinterpret_cast<void*>(&DrawIndexedLogged)) {
        DrawIndexedFn now = reinterpret_cast<DrawIndexedFn>(table[kDrawIndexedSlot]);
        methods->drawIndexed = now;
        WrapSlot(table, kDrawIndexedSlot, &DrawIndexedLogged, &now);
        ++g_drawRewraps;
    }
    if (table[kDrawIndexedInstancedSlot] != reinterpret_cast<void*>(&DrawIndexedInstancedLogged)) {
        DrawIndexedInstancedFn now = reinterpret_cast<DrawIndexedInstancedFn>(table[kDrawIndexedInstancedSlot]);
        methods->drawIndexedInstanced = now;
        WrapSlot(table, kDrawIndexedInstancedSlot, &DrawIndexedInstancedLogged, &now);
        ++g_drawRewraps;
    }
}

// Wraps a context's methods, once for each table of them (the main context's, a deferred one's).
bool WrapContext(ID3D11DeviceContext* context) {
    void** table = *reinterpret_cast<void***>(context);
    if (MethodsOf(context)) return true;
    if (g_contextMethods.size() == g_contextMethods.capacity()) return false;  // no room left without moving the list
    ContextMethods methods = {table, nullptr, nullptr, nullptr, nullptr, nullptr};
    methods.drawIndexed = reinterpret_cast<DrawIndexedFn>(table[kDrawIndexedSlot]);
    methods.drawIndexedInstanced = reinterpret_cast<DrawIndexedInstancedFn>(table[kDrawIndexedInstancedSlot]);
    methods.map = reinterpret_cast<MapFn>(table[kMapSlot]);
    methods.unmap = reinterpret_cast<UnmapFn>(table[kUnmapSlot]);
    methods.updateSubresource = reinterpret_cast<UpdateSubresourceFn>(table[kUpdateSubresourceSlot]);
    g_contextMethods.push_back(methods);  // before the table changes, so a call already finds its originals
    if (!g_mainContextTable) g_mainContextTable = table;
    char drawModule[MAX_PATH], mapModule[MAX_PATH];
    rfa::LogDetail("Road log: a context's methods at %p: DrawIndexed in %s, Map in %s", static_cast<void*>(table),
             ModuleOf(reinterpret_cast<void*>(methods.drawIndexed), drawModule), ModuleOf(reinterpret_cast<void*>(methods.map), mapModule));
    DrawIndexedFn drawIndexed;
    DrawIndexedInstancedFn drawIndexedInstanced;
    MapFn map;
    UnmapFn unmap;
    UpdateSubresourceFn updateSubresource;
    return WrapSlot(table, kMapSlot, &MapNoted, &map) && WrapSlot(table, kUnmapSlot, &UnmapNoted, &unmap) &&
           WrapSlot(table, kUpdateSubresourceSlot, &UpdateSubresourceNoted, &updateSubresource) &&
           WrapSlot(table, kDrawIndexedSlot, &DrawIndexedLogged, &drawIndexed) &&
           WrapSlot(table, kDrawIndexedInstancedSlot, &DrawIndexedInstancedLogged, &drawIndexedInstanced);
}

HRESULT STDMETHODCALLTYPE CreateDeferredContextNoted(ID3D11Device* device, UINT flags, ID3D11DeviceContext** context) {
    const HRESULT result = g_createDeferredContext(device, flags, context);
    if (SUCCEEDED(result) && context && *context) {
        ++g_deferredContexts;
        if (!WrapContext(*context)) rfa::Log("Road log: could not wrap a deferred context's methods");
    }
    return result;
}

void StartRoadLog(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (g_createVertexShader) return;
    g_contextMethods.reserve(8);  // never moved while the game calls through it
    g_constants.reserve(4096);
    void** deviceTable = *reinterpret_cast<void***>(device);
    if (WrapSlot(deviceTable, kCreateVertexShaderSlot, &CreateVertexShaderNoted, &g_createVertexShader) &&
        WrapSlot(deviceTable, kCreateDeferredContextSlot, &CreateDeferredContextNoted, &g_createDeferredContext) && WrapContext(context))
        rfa::Log("Road log: on (RoadLog=1); the terrain's constants are logged in our worlds");
    else
        rfa::Log("Road log: could not wrap the device's methods; nothing logged");
}

HRESULT WINAPI CreateDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver, HMODULE software, UINT flags,
                            const D3D_FEATURE_LEVEL* levels, UINT levelCount, UINT sdk, ID3D11Device** device,
                            D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context) {
    const HRESULT result = g_createDevice(adapter, driver, software, flags, levels, levelCount, sdk, device, level, context);
    if (SUCCEEDED(result) && device && *device && context && *context) {
        if (g_context) rfa::Log("Sky: the game made another Direct3D device; the sky uses the newest");
        (*device)->AddRef();
        (*context)->AddRef();
        g_device = *device;
        g_context = *context;
        if (g_roadLog) StartRoadLog(*device, *context);
    }
    return result;
}

std::vector<std::string> SplitList(const std::wstring& text) {
    std::vector<std::string> names;
    std::string current;
    for (wchar_t c : text) {
        if (c == L',' || c == L' ') {
            if (!current.empty()) names.push_back(current);
            current.clear();
        } else {
            current += static_cast<char>(c);
        }
    }
    if (!current.empty()) names.push_back(current);
    return names;
}

// ---- Room for more explosions

// The game keeps every explosion explosions.xtbl (and each DLC's) describes in one table of 132
// (0x144 bytes each from 0x1888D88, how many it has at 0x1893498, just after it); its reader
// (0x6E9C70) drops each one past the 132nd (cmp [1893498h],84h). Armageddon's own and its DLCs' come
// to 130, so only 2 of the 14 Guerrilla's props blow up with fitted. The game's code reaches the
// table by its address in seven instructions (the reader and the look-ups by name and number, one of
// them a field at +0x20) and its size in that one, so the mod moves it to a table of 256 of its own,
// as guerrilla_vehicles does its tables, after checking each value. The world builder fills to the
// same 256 (GuerrillaEffectTables.ExplosionLimit).
constexpr DWORD kExplosions = 0x1888D88;
constexpr DWORD kExplosionSize = 0x144;
constexpr DWORD kGameMaxExplosions = 132;
constexpr DWORD kExplosionReferences[] = {0x6E9D14, 0x6EAE5F, 0x6EAE7A, 0x6EAECF, 0x6EAEE2, 0x6EAF3F, 0x6EAF78, 0x6EAF98};
constexpr DWORD kExplosionLimits[] = {0x6E9D00};
constexpr DWORD kMaxExplosions = 256;
alignas(16) BYTE g_explosions[kMaxExplosions * kExplosionSize];  // the new table, filled by the game

// ---- Room for more particle emitters

// The renderer keeps one "renderable particles" entry for each emitter of every effect loaded, in a
// pool of 1,900 (Guerrilla's: 2,048) that its setup (0x7676B0) sizes with `push 76Ch` at 0x7677AA
// (stored at 0x306EE8C by 0x767AF0; the pool's memory is made from it afterwards). Armageddon's own
// preloaded effects take 1,175, and the effects our worlds stream in with their zones passed the rest:
// an emitter that finds the pool full is set up without one (0xB6FBAB) and never draws while its effect
// stays loaded (flames, smoke or sparks missing without a word; the game's debug overlay reports
// "Maximum ...'s exceeded, max is 1900"). Worse, the template's load then uses the emitter's missing
// one unchecked (0xB6FE60) and the game crashes: at rfg_10_08 once the effect templates were no longer
// held to 350 (2026-11-06). The mod gives it room for 16,384 (0x18 bytes each; 4,096 until then).
constexpr DWORD kParticlePoolSize = 0x7677AB;   // the push's value
constexpr DWORD kParticlePoolSet = 0x306EE8C;  // where the setup stores it
constexpr DWORD kGameParticlePool = 1900, kParticlePool = 16384;  // 4096 filled at rfg_10_08 with the templates at 1024 (2026-11-06)

void MoreParticleRoom() {
    if (rfa::CodeValue(kParticlePoolSize) != kGameParticlePool) {
        rfa::Log("Particles: the renderer's setup isn't as expected (0x%08lX); the game keeps its %lu emitters", rfa::CodeValue(kParticlePoolSize),
                 kGameParticlePool);
        return;
    }
    if (*reinterpret_cast<const DWORD*>(kParticlePoolSet) != 0) {
        rfa::Log("Particles: the renderer was set up already; the game keeps its %lu emitters", kGameParticlePool);
        return;
    }
    const DWORD room = kParticlePool;
    if (rfa::PatchCode(reinterpret_cast<void*>(kParticlePoolSize), &room, sizeof(room)))
        rfa::Log("Particles: room for %lu emitters (the game's %lu)", kParticlePool, kGameParticlePool);
}

// ---- Room for more effect templates

// Each effect file the game loads (.cefct_pc: its own, and the Guerrilla world's, vehicles' and
// weapons') becomes a template from one pool, "vfx_effect_template": 350 of 0x1C bytes. The pool
// object is at 0x26BF43C, set up by the start-up constructor 0xF58190 (0xB6BF70: +8 its name, +0xC
// the name's hash, +0x10 the size 0x1C, +0x14 its init 0xB6C0A0, +0x1C the count from push 15Eh, so
// the count is 0x26BF458). The game makes its memory pools in one go (0xD2E740, from 0xD2E5C4, after
// the mods load): it adds up count × size of every registered pool (the list at 0x2E7A6AC), allocates
// one block of that size (0xA509F0), and calls each pool's init (+0x14) with an allocator over it
// (0x2E7A710); the template pool's init allocates count × 0x1C from it and lists them all free. When
// the pool is empty, the template's allocation (0xB6C120) gives none and the effect-file loader
// (0xB5AC10) calls into it at 0xB5AC28: the game crashes. Armageddon's own preloaded effects take about
// 200 and the Guerrilla world's 150 more; a zone's characters loading theirs filled it (a crash at
// rfg_10_08 after 300-odd effect files, 2026-11-06). The mod wraps the pools' set-up and raises the
// count before it adds up the sizes, so the block and the pool are both made for 1024 (a value in the
// game's data, not its code; raising it any later would make the pool bigger than its share of the block).
//
// The emitters of the effects those templates hold come from pools of their own, made the same way: a
// template's load gives each emitter a "renderable particles" object (0xD5EDC0 from 0x306EE70, the pool
// the renderer sizes itself, see "Room for more particle emitters") and, with none left, sets the
// emitter up without one (0xB6FBAB) and then uses it unchecked (0xB6FE60, from 0xB6FF60): a crash at
// rfg_10_08 once the templates were no longer held to 350 (2026-11-06; 0x47CE1B writing 0x44). So the
// effect family's other pools are raised with the templates, threefold, each checked by its name (+8)
// and the game's count before it is changed: the emitter and filter instances, the mesh and renderable
// filter instances and each kind of particle filter, and the renderer's particle instances and groups.
constexpr DWORD kMemoryPoolsSetUp = 0xD2E740;
constexpr size_t kMemoryPoolsSetUpPrologue = 6;  // push ebp / mov ebp,esp / sub esp,1Ch
constexpr DWORD kPoolName = 0x08, kPoolCount = 0x1C;  // a pool object's name and count
struct PoolRoom {
    DWORD pool;  // the pool object (its start-up constructor's `mov ecx,...`)
    const char* name;
    DWORD game, ours;
};
const PoolRoom kPoolRooms[] = {
    {0x26BF43C, "vfx_effect_template", 350, 1024},                         // 0x1C bytes each
    {0x26BF3D8, "vfx_particle_system_emitter_instance", 1600, 4800},       // 0x194
    {0x26BF470, "vfx_renderable_filter_instance", 200, 600},               // 0xC0
    {0x26BF4A4, "vfx_particle_system_filter_instance", 500, 1500},         // 0xCC
    {0x26BF4D8, "vfx_mesh_instance", 512, 1536},                           // 0x2C
    {0x26BF50C, "vfx_particle_system_filter_wind", 50, 150},               // 0x7C
    {0x26BF540, "vfx_particle_system_filter_gravity", 5, 15},              // 0x54
    {0x26BF574, "vfx_particle_system_filter_magnet", 200, 600},            // 0x5C
    {0x26BF5A8, "vfx_particle_system_filter_turbulence", 100, 300},        // 0x5C
    {0x26BF5DC, "vfx_particle_system_filter_vortex", 10, 30},              // 0x54
    {0x26BF610, "vfx_particle_system_filter_orientor", 100, 300},          // 0x4C
    {0x2DD7FD0, "rl_renderable_particles_instance", 1600, 4800},           // 0xF4
    {0x3073684, "rl_renderable_particles_instance_group", 512, 1536},      // 0x140
};
using MemoryPoolsSetUpFn = void(__cdecl*)();
MemoryPoolsSetUpFn g_memoryPoolsSetUp;

void __cdecl MemoryPoolsSetUp() {
    for (const PoolRoom& room : kPoolRooms) {
        const char* name = *reinterpret_cast<const char* const*>(room.pool + kPoolName);
        DWORD& count = *reinterpret_cast<DWORD*>(room.pool + kPoolCount);
        if (!name || strcmp(name, room.name) != 0)
            rfa::Log("Effect pools: the pool at 0x%08lX isn't %s; left as it is", room.pool, room.name);
        else if (count != room.game)
            rfa::Log("Effect pools: %s holds %lu, not the game's %lu; left as it is", room.name, count, room.game);
        else {
            count = room.ours;
            rfa::Log("Effect pools: %s holds %lu (the game's %lu)", room.name, room.ours, room.game);
        }
    }
    g_memoryPoolsSetUp();
}

void MoreEffectTemplateRoom() {
    g_memoryPoolsSetUp = reinterpret_cast<MemoryPoolsSetUpFn>(
        rfa::HookFunction(kMemoryPoolsSetUp, kMemoryPoolsSetUpPrologue, reinterpret_cast<void*>(&MemoryPoolsSetUp)));
    if (!g_memoryPoolsSetUp) rfa::Log("Effect pools: could not wrap the memory pools' set-up; the game keeps its own sizes (more effects can crash it)");
}

// ---- Room for the effects' emitters
//
// Each emitter of each effect file the game loads takes a place (80 bytes) from a fixed pool at 0x26BF650
// (0xB76040, in the emitter's set-up 0xB6FAF0, the emitter's +0x14); with none left the emitter keeps none, and
// the template's set-up then uses it anyway (0xB6FE60 → 0xB75FE0 on null: the write to 0x44 at 0x47CE1B, the
// loading crashes of 2026-11-06 00:28 and 21:45). Its start-up constructor (0xB76720, from 0xF58370) makes it for
// 2,200 (`push 898h`) in a buffer inside the object (+0x30, `push 2DA7Dh`). Armageddon's 639 effect files have
// 3,457 emitters in all, the world builder's 188 about 1,300. The user, 2026-11-06: OK'd: the mod runs the
// constructor itself (as for its other fixed pools): the pool's base, a memory pool over the mod's buffer, the
// pool's set-up for 6,600 (3 times the game's, as the other effect pools), the memory pool dropped; the
// constructor's own values checked first. The effect files' log shows its use as each loads.
constexpr DWORD kEmitterPoolConstructor = 0xB76720;
constexpr size_t kEmitterPoolConstructorPrologue = 6;  // push ebp / mov ebp,esp / sub esp,64h
constexpr DWORD kGameEmitters = 2200, kEmitters = 6600;
constexpr DWORD kEmitterPlace = 0x50 + 4 + 1;          // an emitter's object, its link, its flag
constexpr DWORD kGameEmitterMemory = 0x2DA7D;
// The constructor's own values: its buffer's size (push 2DA7Dh), the buffer's place in the object (add eax,30h)
// and its count (push 898h).
constexpr DWORD kEmitterMemoryValue = 0xB76745, kEmitterBufferValue = 0xB7674E, kEmitterCountValue = 0xB76759;
alignas(16) BYTE g_emitterPoolMemory[kEmitters * kEmitterPlace + 64];

auto const EmitterPoolBase = reinterpret_cast<void(__fastcall*)(BYTE* pool, void* unused)>(0x71C5D0);
auto const EmitterMempoolOver = reinterpret_cast<void(__fastcall*)(void* mempool, void* unused, void* memory, DWORD size, const void* name,
                                                                    DWORD first, DWORD second)>(0x428240);
auto const EmitterMempoolDone = reinterpret_cast<void(__fastcall*)(void* mempool, void* unused)>(0x427F40);
auto const EmitterPoolInit = reinterpret_cast<void(__fastcall*)(BYTE* pool, void* unused, void* mempool, DWORD holds)>(0xB76790);
const void* const kEmitterPoolName = reinterpret_cast<const void*>(0xF7101C);  // "object pool", the name the game gives it

BYTE* __fastcall EmitterPoolConstructor(BYTE* pool, void* unused) {
    EmitterPoolBase(pool, nullptr);
    alignas(16) BYTE mempool[0x60];  // the game's takes 0x5C bytes of its stack
    EmitterMempoolOver(mempool, nullptr, g_emitterPoolMemory, sizeof(g_emitterPoolMemory), kEmitterPoolName, 1, 0);
    EmitterPoolInit(pool, nullptr, mempool, kEmitters);
    EmitterMempoolDone(mempool, nullptr);
    return pool;
}

void MoreEmitterRoom() {
    if (*reinterpret_cast<const WORD*>(kEmitterPool + 0xA) != 0) {
        rfa::Log("Effect emitters: the game has already built its pool; it keeps its %lu", kGameEmitters);
        return;
    }
    if (rfa::CodeValue(kEmitterMemoryValue) != kGameEmitterMemory || rfa::CodeValue(kEmitterCountValue) != kGameEmitters ||
        *reinterpret_cast<const BYTE*>(kEmitterBufferValue) != 0x30) {
        rfa::Log("Effect emitters: the pool's constructor (0x%08lX) isn't as expected; the game keeps its %lu", kEmitterPoolConstructor, kGameEmitters);
        return;
    }
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x64};
    if (memcmp(reinterpret_cast<const void*>(kEmitterPoolConstructor), opening, sizeof(opening)) != 0 ||
        !rfa::HookFunction(kEmitterPoolConstructor, kEmitterPoolConstructorPrologue, reinterpret_cast<void*>(&EmitterPoolConstructor))) {
        rfa::Log("Effect emitters: could not change the game's code; it keeps its %lu", kGameEmitters);
        return;
    }
    rfa::Log("Effect emitters: the game holds %lu (its own %lu)", kEmitters, kGameEmitters);
}

// ---- Guerrilla's material events
//
// What a breaking piece plays. The game asks one function for it (0x684090, cdecl: the event's kind, its size 0-2,
// the kind of piece 0-4, the piece's effect material and the other one's (their numbers; 0xFF none), and two
// more), which answers with an effect's number from the materials' lists (rfg_effect_materials.xtbl's
// event_effects) or -1; its callers (0x665DA0, 0x672760) then play that effect with Armageddon's destruction
// sound beside it. The kinds (the game's names at 0x10FDBA4): 0 load_balance, 1 stress, 2 detach, 3 groan,
// 4 shatter, 5 collapse, 6 melee, 7 bullet, 8 impact, 9 shift, 10 roll, 11 slide; the pieces: Solid, Sheet, Pole,
// Fence, Catwalk. A material's name is the start of its entry (0x9C bytes each from the pointer at 0x155E108,
// how many at 0x155E114).
//
// Guerrilla's table has the same materials and the same sizes' thresholds (all 44 the same), with its own
// effects, most of them a DES_ sound alone. They can't go into Armageddon's table (read once, for every level),
// so the world builder writes them to mods\guerrilla_world\material_events.txt (GuerrillaMaterialEvents) and the
// mod answers from that while the Guerrilla world is loaded: for a material Guerrilla has, Guerrilla's row for the
// event, the kind of piece and the other material, else its row without another material, else the other
// material's own row, else nothing (as Guerrilla). Kinds 6 and 7 and materials Guerrilla hasn't are the game's.
constexpr DWORD kMaterialEffect = 0x684090;
constexpr size_t kMaterialEffectPrologue = 6;  // push ebp / mov ebp,esp / sub esp,14h
using MaterialEffectFn = int(__cdecl*)(int kind, int size, int type, DWORD material, DWORD other, DWORD a6, DWORD a7);
MaterialEffectFn g_materialEffect;
constexpr DWORD kEffectMaterials = 0x155E108, kEffectMaterialCount = 0x155E114, kEffectMaterialSize = 0x9C;
constexpr int kMeleeKind = 6, kBulletKind = 7, kEventKinds = 12, kEventSizes = 3;
const char* const kEventKindNames[kEventKinds] = {"load_balance", "stress", "detach", "groan", "shatter", "collapse",
                                                   "melee", "bullet", "impact", "shift", "roll", "slide"};

struct MaterialEvent {
    int kind, type;
    std::string other;                 // the other material, or empty: against anything
    std::string names[kEventSizes];    // the effect for each size (empty: none)
    int numbers[kEventSizes];          // their numbers in effects.xtbl, once asked for
    bool numbered;
};
struct MaterialEvents {
    std::string material;
    std::vector<MaterialEvent> events;
};
std::vector<MaterialEvents> g_materialEvents;

const char* EffectMaterialName(DWORD number) {
    const BYTE* materials = *reinterpret_cast<BYTE* const*>(kEffectMaterials);
    const DWORD count = *reinterpret_cast<const DWORD*>(kEffectMaterialCount);
    return materials && number < count ? reinterpret_cast<const char*>(materials + number * kEffectMaterialSize) : nullptr;
}

// An effect material's record by its number, or null.
BYTE* EffectMaterialRecord(DWORD number) {
    BYTE* materials = *reinterpret_cast<BYTE* const*>(kEffectMaterials);
    const DWORD count = *reinterpret_cast<const DWORD*>(kEffectMaterialCount);
    return materials && number < count ? materials + number * kEffectMaterialSize : nullptr;
}

MaterialEvents* GuerrillaMaterial(const char* name) {
    if (!name) return nullptr;
    for (MaterialEvents& m : g_materialEvents)
        if (_stricmp(m.material.c_str(), name) == 0) return &m;
    return nullptr;
}

// A material's effect for an event of this kind, piece and size against the other material (null: none), or -1.
int GuerrillaEventEffect(MaterialEvents& material, int kind, int type, int size, const char* other) {
    for (int pass = 0; pass < 2; ++pass) {
        const char* against = pass == 0 ? other : "";
        if (!against) continue;
        if (pass == 0 && !*against) continue;
        for (MaterialEvent& e : material.events) {
            if (e.kind != kind || e.type != type || _stricmp(e.other.c_str(), against) != 0) continue;
            if (!e.numbered) {
                for (int i = 0; i < kEventSizes; ++i) e.numbers[i] = e.names[i].empty() ? -1 : EffectNumber(e.names[i].c_str());
                e.numbered = true;
            }
            return e.numbers[size];
        }
    }
    return -1;
}

void CountBreakEvent(int kind);  // (below, with Guerrilla's buildings)

int __cdecl MaterialEffect(int kind, int size, int type, DWORD material, DWORD other, DWORD a6, DWORD a7) {
    if (InOurWorld()) CountBreakEvent(kind);
    if (kind < 0 || kind >= kEventKinds || kind == kMeleeKind || kind == kBulletKind || size < 0 || size >= kEventSizes || !InOurWorld())
        return g_materialEffect(kind, size, type, material, other, a6, a7);
    const char* name = EffectMaterialName(material & 0xFF);
    const char* otherName = (other & 0xFF) == 0xFF ? nullptr : EffectMaterialName(other & 0xFF);
    MaterialEvents* ours = GuerrillaMaterial(name);
    MaterialEvents* theirs = GuerrillaMaterial(otherName);
    if (!ours && !theirs) return g_materialEffect(kind, size, type, material, other, a6, a7);
    int effect = ours ? GuerrillaEventEffect(*ours, kind, type, size, otherName) : -1;
    if (effect == -1 && theirs) effect = GuerrillaEventEffect(*theirs, kind, type, size, name);
    RFA_LOG_DETAIL_FIRST(80, "Material events: %s, size %d, piece %d, %s on %s: effect %d", kEventKindNames[kind], size, type, name ? name : "?",
                  otherName ? otherName : "nothing", effect);
    return effect;
}

void LoadMaterialEvents() {
    std::vector<BYTE> file;
    if (!ReadWholeFile(DataFolder() + L"material_events.txt", file) || file.empty()) {
        rfa::Log("Material events: no material_events.txt from the world builder; the pieces play Armageddon's own");
        return;
    }
    size_t rows = 0;
    const std::string text(file.begin(), file.end());
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        std::vector<std::string> words;
        for (size_t from = 0; from <= line.size();) {
            size_t tab = line.find('\t', from);
            if (tab == std::string::npos) tab = line.size();
            words.push_back(line.substr(from, tab - from));
            from = tab + 1;
        }
        if (words.size() == 2 && words[0] == "material") g_materialEvents.push_back({words[1], {}});
        else if (words.size() == 7 && words[0] == "event" && !g_materialEvents.empty()) {
            MaterialEvent e{};
            e.kind = atoi(words[1].c_str());
            e.type = atoi(words[2].c_str());
            if (words[3] != "-") e.other = words[3];
            for (int i = 0; i < kEventSizes; ++i)
                if (words[4 + i] != "-") e.names[i] = words[4 + i];
            g_materialEvents.back().events.push_back(e);
            ++rows;
        }
    }
    if (g_materialEvents.empty()) {
        rfa::Log("Material events: material_events.txt names no material; the pieces play Armageddon's own");
        return;
    }
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14};
    if (memcmp(reinterpret_cast<const void*>(kMaterialEffect), opening, sizeof(opening)) == 0)
        g_materialEffect = reinterpret_cast<MaterialEffectFn>(rfa::HookFunction(kMaterialEffect, kMaterialEffectPrologue, reinterpret_cast<void*>(&MaterialEffect)));
    if (!g_materialEffect) {
        g_materialEvents.clear();
        rfa::Log("Material events: could not answer for the game's event effects (its code isn't as expected); the pieces play Armageddon's own");
        return;
    }
    g_materialEventsOn = true;
    rfa::Log("Material events: %zu of Guerrilla's materials, %zu rows; in the Guerrilla world the pieces play Guerrilla's effects and sounds",
             g_materialEvents.size(), rows);
}

// ---- Guerrilla's wheel effects
//
// Each frame, for each car, the game's wheel code (0x5F51A0, cdecl: the car) plays at every wheel that touches the
// ground (the zone's terrain, not an object) the ground's effect material's dust: its +0x78 front and +0x7C rear
// effect's numbers (-1: none, and then the wheel table's front and rear), tyre tracks while +0x74 is set, tinted
// with +0x24 (red, green, blue, alpha, 0 to 1) while +0x34 is set; braking, peeling and skidding play the wheel
// table's effects. That table (vehicle_wheel_surface_effects.xtbl's one row, read at 0x630B70) is kept as five
// effect numbers at 0x10FC52C: front, rear, skid, peel, brake. Guerrilla's wheel code is the same
// (automobile_effects_handle_wheel_effects), and both games' tables name the same effects, but Armageddon's
// files for them are loaded only with its own levels, so in our worlds the wheels kicked up nothing, and its
// tints are its own (its Terrain a pale grey, Guerrilla's a sandy orange).
//
// The world builder adds Guerrilla's wheel effects to the world's tables (their files in every zone) and writes
// Guerrilla's values to mods\guerrilla_world\wheel_effects.txt (GuerrillaWheelEffects): the wheel table's five
// effects, then each of Guerrilla's materials with its tracks, front and rear effects and tint. Both of the
// game's tables are read once for every level, so in our worlds the mod wraps the wheel code: for each call it
// gives the materials and the wheel table Guerrilla's values, runs the game's code, and puts Armageddon's back,
// so nothing else, and none of Armageddon's levels, sees them.
constexpr DWORD kWheelEffects = 0x5F51A0;
constexpr size_t kWheelEffectsPrologue = 9;  // push ebp / mov ebp,esp / sub esp,534h
using WheelEffectsFn = void(__cdecl*)(void* car);
WheelEffectsFn g_wheelEffects;
constexpr DWORD kWheelTableEffects = 0x10FC52C;  // front, rear, skid, peel, brake: five effects' numbers
constexpr int kWheelTableFields = 5;
const char* const kWheelTableFieldNames[kWheelTableFields] = {"front", "rear", "skid", "peel", "brake"};
constexpr size_t kMaterialTint = 0x24, kMaterialTintOn = 0x34, kMaterialTracks = 0x74, kMaterialFront = 0x78, kMaterialRear = 0x7C;

struct WheelMaterial {
    std::string name;
    bool tracks = false;
    std::string front, rear;  // effects' names in the world's tables (empty: none)
    bool tinted = false;
    float tint[3] = {};       // 0 to 1
    int number = -1;          // the game's effect material of this name (-1: it has none)
    int frontNumber = -1, rearNumber = -1;
};
std::string g_wheelTableNames[kWheelTableFields];
int g_wheelTableNumbers[kWheelTableFields];
std::vector<WheelMaterial> g_wheelMaterials;
bool g_wheelNumbered = false;

// What a material had before the wheel code ran with Guerrilla's values, to put back.
struct WheelMaterialBefore {
    float tint[3];
    BYTE tintOn, tracks;
    int front, rear;
};

// The materials' and effects' numbers, the first time the wheel code runs in our world (the tables are read by then).
void NumberWheelEffects() {
    g_wheelNumbered = true;
    for (int i = 0; i < kWheelTableFields; ++i)
        g_wheelTableNumbers[i] = g_wheelTableNames[i].empty() ? -1 : EffectNumber(g_wheelTableNames[i].c_str());
    const DWORD count = *reinterpret_cast<const DWORD*>(kEffectMaterialCount);
    size_t found = 0, dusty = 0;
    std::string missing;
    for (WheelMaterial& m : g_wheelMaterials) {
        m.number = -1;
        for (DWORD n = 0; n < count && m.number < 0; ++n)
            if (const char* name = EffectMaterialName(n); name && _stricmp(name, m.name.c_str()) == 0) m.number = static_cast<int>(n);
        if (m.number < 0) {
            missing += missing.empty() ? m.name : ", " + m.name;
            continue;
        }
        ++found;
        m.frontNumber = m.front.empty() ? -1 : EffectNumber(m.front.c_str());
        m.rearNumber = m.rear.empty() ? -1 : EffectNumber(m.rear.c_str());
        if (m.frontNumber >= 0 || m.rearNumber >= 0) ++dusty;
    }
    rfa::Log("Wheel effects: %zu of Guerrilla's %zu materials found among the game's (%zu with dust of their own)%s%s; the wheel table's "
             "front %d, rear %d, skid %d, peel %d, brake %d", found, g_wheelMaterials.size(), dusty, missing.empty() ? "" : "; not found: ",
             missing.c_str(), g_wheelTableNumbers[0], g_wheelTableNumbers[1], g_wheelTableNumbers[2], g_wheelTableNumbers[3], g_wheelTableNumbers[4]);
}

void __cdecl WheelEffects(void* car) {
    BYTE* materials = *reinterpret_cast<BYTE**>(kEffectMaterials);
    if (!InOurWorld() || !materials) {
        g_wheelEffects(car);
        return;
    }
    if (!g_wheelNumbered) NumberWheelEffects();
    RFA_LOG_FIRST(4, "Wheel effects: the wheels play Guerrilla's dust, braking and skids in world \"%s\"", WorldName());
    int* table = reinterpret_cast<int*>(kWheelTableEffects);
    int tableBefore[kWheelTableFields];
    for (int i = 0; i < kWheelTableFields; ++i) {
        tableBefore[i] = table[i];
        table[i] = g_wheelTableNumbers[i];
    }
    static std::vector<WheelMaterialBefore> before;
    before.resize(g_wheelMaterials.size());
    for (size_t i = 0; i < g_wheelMaterials.size(); ++i) {
        const WheelMaterial& m = g_wheelMaterials[i];
        if (m.number < 0) continue;
        BYTE* at = materials + m.number * kEffectMaterialSize;
        WheelMaterialBefore& was = before[i];
        memcpy(was.tint, at + kMaterialTint, sizeof(was.tint));
        was.tintOn = at[kMaterialTintOn];
        was.tracks = at[kMaterialTracks];
        memcpy(&was.front, at + kMaterialFront, sizeof(int));
        memcpy(&was.rear, at + kMaterialRear, sizeof(int));
        if (m.tinted) memcpy(at + kMaterialTint, m.tint, sizeof(m.tint));
        at[kMaterialTintOn] = m.tinted ? 1 : 0;
        at[kMaterialTracks] = m.tracks ? 1 : 0;
        memcpy(at + kMaterialFront, &m.frontNumber, sizeof(int));
        memcpy(at + kMaterialRear, &m.rearNumber, sizeof(int));
    }
    g_wheelEffects(car);
    for (size_t i = 0; i < g_wheelMaterials.size(); ++i) {
        const WheelMaterial& m = g_wheelMaterials[i];
        if (m.number < 0) continue;
        BYTE* at = materials + m.number * kEffectMaterialSize;
        const WheelMaterialBefore& was = before[i];
        memcpy(at + kMaterialTint, was.tint, sizeof(was.tint));
        at[kMaterialTintOn] = was.tintOn;
        at[kMaterialTracks] = was.tracks;
        memcpy(at + kMaterialFront, &was.front, sizeof(int));
        memcpy(at + kMaterialRear, &was.rear, sizeof(int));
    }
    for (int i = 0; i < kWheelTableFields; ++i) table[i] = tableBefore[i];
}

void LoadWheelEffects() {
    std::vector<BYTE> file;
    if (!ReadWholeFile(DataFolder() + L"wheel_effects.txt", file) || file.empty()) {
        rfa::Log("Wheel effects: no wheel_effects.txt from the world builder; the wheels play Armageddon's own (nothing shows in our worlds)");
        return;
    }
    const std::string text(file.begin(), file.end());
    bool haveWheel = false;
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        std::vector<std::string> words;
        for (size_t from = 0; from <= line.size();) {
            size_t tab = line.find('\t', from);
            if (tab == std::string::npos) tab = line.size();
            words.push_back(line.substr(from, tab - from));
            from = tab + 1;
        }
        auto effect = [](const std::string& word) { return word == "-" ? std::string() : word; };
        if (words.size() == 1 + kWheelTableFields && words[0] == "wheel") {
            for (int i = 0; i < kWheelTableFields; ++i) g_wheelTableNames[i] = effect(words[1 + i]);
            haveWheel = true;
        } else if (words.size() == 8 && words[0] == "material") {
            WheelMaterial m;
            m.name = words[1];
            m.tracks = words[2] == "1";
            m.front = effect(words[3]);
            m.rear = effect(words[4]);
            m.tinted = words[5] != "-";
            for (int i = 0; i < 3 && m.tinted; ++i) m.tint[i] = static_cast<float>(atoi(words[5 + i].c_str())) / 255.0f;
            g_wheelMaterials.push_back(m);
        }
    }
    if (!haveWheel || g_wheelMaterials.empty()) {
        g_wheelMaterials.clear();
        rfa::Log("Wheel effects: wheel_effects.txt has no wheel line or no material; the wheels play Armageddon's own");
        return;
    }
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x34, 0x05, 0x00, 0x00};
    if (memcmp(reinterpret_cast<const void*>(kWheelEffects), opening, sizeof(opening)) == 0)
        g_wheelEffects = reinterpret_cast<WheelEffectsFn>(rfa::HookFunction(kWheelEffects, kWheelEffectsPrologue, reinterpret_cast<void*>(&WheelEffects)));
    if (!g_wheelEffects) {
        g_wheelMaterials.clear();
        rfa::Log("Wheel effects: could not wrap the game's wheel code (it isn't as expected); the wheels play Armageddon's own");
        return;
    }
    std::string table;
    for (int i = 0; i < kWheelTableFields; ++i)
        table += std::string(i ? ", " : "") + kWheelTableFieldNames[i] + " " + (g_wheelTableNames[i].empty() ? "none" : g_wheelTableNames[i]);
    rfa::Log("Wheel effects: Guerrilla's for %zu materials and the wheel table (%s); in our worlds the wheels play them", g_wheelMaterials.size(),
             table.c_str());
}

// ---- Room for the materials' effects
//
// What plays on each material is kept in one memory pool, the "Effect candidate event pool" (0x155E148; its size
// +0x44, the bytes taken +0x4C), laid over a buffer of 64 KB (0x154E108) by a start-up constructor (0xF25A90:
// push 10000h, push 154E108h; nothing else names the buffer). The game takes from it, while it starts and never
// gives back: the effect materials (rfg_effect_materials.xtbl, 0x681550: 0x9C bytes each, and their events'
// effects), the materials (rfg_materials.xtbl), each melee group's and bullet group's name, and for each material
// a group names a list sized by the number of groups (0x682590: two lists of 20 bytes a melee group; 0x682790: 20
// and 4 bytes a bullet group); for each level object with a Melee_impact or Bullet_impact a list of 4 bytes a
// group (0x7AF1A0, 0x7AF2B0); footsteps.xtbl (0x8862A0) and audio_destruction_event_sizes.xtbl (0x534940). A
// request that doesn't fit stops the game ("Failed aligned alloc of 144 bytes in Effect candidate event pool",
// the user's start of 2026-11-10 22:3x: Guerrilla's five melee groups and four bullet groups, added to
// Armageddon's eleven and seven, made every material's lists longer by about 11 KB in all, in a pool that was
// at least 85% full). The mod gives the constructor a buffer of its own of 256 KB (the user's OK asked through
// the main session), both values checked first, all or nothing; the weapons builder counts what the groups take
// (GuerrillaWeapons.MaterialEffectRoom), and the first effect played logs the pool's use.
constexpr DWORD kMaterialEffectSizeValue = 0xF25A9D;    // push 10000h
constexpr DWORD kMaterialEffectMemoryValue = 0xF25AA2;  // push 154E108h
constexpr DWORD kGameMaterialEffectRoom = 0x10000, kGameMaterialEffectMemory = 0x154E108;
constexpr DWORD kMaterialEffectRoom = 0x40000;          // the weapons builder's GuerrillaWeapons.MaterialEffectRoom
alignas(16) BYTE g_materialEffectMemory[kMaterialEffectRoom];

void MoreMaterialEffectRoom() {
    if (*reinterpret_cast<const DWORD*>(kMaterialEffectPool + kMemoryPoolSize) != 0) {
        rfa::Log("Material effects: the game has already made its pool; it keeps its %lu bytes", kGameMaterialEffectRoom);
        return;
    }
    const BYTE push = 0x68;
    if (*reinterpret_cast<const BYTE*>(kMaterialEffectSizeValue - 1) != push || rfa::CodeValue(kMaterialEffectSizeValue) != kGameMaterialEffectRoom ||
        *reinterpret_cast<const BYTE*>(kMaterialEffectMemoryValue - 1) != push || rfa::CodeValue(kMaterialEffectMemoryValue) != kGameMaterialEffectMemory) {
        rfa::Log("Material effects: the pool's constructor (0x00F25A90) isn't as expected; the game keeps its %lu bytes", kGameMaterialEffectRoom);
        return;
    }
    const DWORD size = kMaterialEffectRoom, memory = reinterpret_cast<DWORD>(&g_materialEffectMemory[0]);
    if (!rfa::PatchCode(reinterpret_cast<void*>(kMaterialEffectSizeValue), &size, sizeof(size))) {
        rfa::Log("Material effects: could not change the game's code; it keeps its %lu bytes", kGameMaterialEffectRoom);
        return;
    }
    if (!rfa::PatchCode(reinterpret_cast<void*>(kMaterialEffectMemoryValue), &memory, sizeof(memory))) {
        rfa::PatchCode(reinterpret_cast<void*>(kMaterialEffectSizeValue), &kGameMaterialEffectRoom, sizeof(kGameMaterialEffectRoom));
        rfa::Log("Material effects: could not change the game's code; it keeps its %lu bytes", kGameMaterialEffectRoom);
        return;
    }
    rfa::Log("Material effects: the pool holds %lu bytes (the game's %lu)", kMaterialEffectRoom, kGameMaterialEffectRoom);
}

// ---- Room for what the game draws one by one
//
// Each frame the game builds its batched pass anew (the constructor 0xD71B20, from 0xEFC7BA) and goes through what
// is visible (0xD51F70). A renderable with +0x108 set or the flag 0x20 (its flags at +0x40) is queued at once, and
// one that can go into batches (the flag 0x80) does; any other (people, vehicles' parts, the ground's cover, the
// terrain's skirts, ...) has its render groups queued and is noted in a list of pointers, one each, to be drawn
// by itself (0xD72040, from 0xD52785): the pass's count (+0x10) goes up by one and, while it is no more than
// 0x180 (384), the pointer is written into the list (+0x14); past that the render warning 0x200 "out of unbatched
// renderable trackers" is set and the thing isn't drawn that frame. As the pass is finished (0xD71E90) the count is
// kept at 0x316B0B4 (what the frame asked for, past the size too) and then cut to 0x180; the drawing's and the
// decals' loops run to the count and the sort is in place, so nothing else knows the size.
// The list is 0x180 pointers of the exe's own memory (0x30AAAB0), right between the pass's decals (0x30A8AB0) and
// its batched instances (0x30AB0B0), so it can't grow where it is, and only the constructor names it. The Guerrilla
// world asks for more than 384 (the warning in the run of 2026-11-11 02:41). The user's OK was asked through the
// main session: 4,096, the size Guerrilla Re-Mars-tered has for the same list. No more than 10,000 could ever be
// asked for, the visible list's own size ([0x1123650]).
// The mod gives the pass a list of its own and changes five values, each checked first with its instruction's
// bytes, all or nothing, and only before the game has made its renderer (the pass is built every frame, so there
// is nothing to copy): the list's address in the constructor, then the size in the add's check, in the finish's
// test and the value it cuts to, and in a stats function nothing calls. In that order each step is safe alone:
// with the size raised over the game's own list the adds would run on into the batched instances, and a count cut
// to more than the adds may write would draw pointers no one wrote. F7 Limits shows the last frame's count; the
// log has the most asked for (NoteRenderWarnings).
constexpr DWORD kGameUnbatchedRenderables = 0x180, kUnbatchedRenderables = 4096;
constexpr DWORD kGameUnbatchedList = 0x30AAAB0;
constexpr DWORD kUnbatchedListValue = 0xD71B7C;       // the constructor's mov [ecx+14h],30AAAB0h (C7 41 14, then it)
constexpr DWORD kUnbatchedClampTestValue = 0xD71ECE;  // the finish's cmp [edx+10h],180h (81 7A 10, then it)
constexpr DWORD kUnbatchedClampValue = 0xD71EE2;      // the finish's mov [ebp-38h],180h (C7 45 C8, then it)
constexpr DWORD kUnbatchedStatsValue = 0xD72293;      // the stats' mov [edx],180h (C7 02, then it)
alignas(16) void* g_unbatchedList[kUnbatchedRenderables];  // the pass's list, filled by the game each frame

void MoreUnbatchedRenderableRoom() {
    if (*reinterpret_cast<void* const*>(kRenderer) != nullptr) {
        rfa::Log("Unbatched renderables: the game has already made its renderer; it keeps its %lu a frame", kGameUnbatchedRenderables);
        return;
    }
    struct Value {
        DWORD at;            // the value's place in the game's code
        BYTE opening[3];     // its instruction's bytes before it
        size_t openingSize;  // how many of them
        DWORD game, ours;
    };
    const Value values[] = {
        {kUnbatchedListValue, {0xC7, 0x41, 0x14}, 3, kGameUnbatchedList, reinterpret_cast<DWORD>(&g_unbatchedList[0])},
        {kUnbatchedAddValue, {0x81, 0x7D, 0xFC}, 3, kGameUnbatchedRenderables, kUnbatchedRenderables},
        {kUnbatchedClampTestValue, {0x81, 0x7A, 0x10}, 3, kGameUnbatchedRenderables, kUnbatchedRenderables},
        {kUnbatchedClampValue, {0xC7, 0x45, 0xC8}, 3, kGameUnbatchedRenderables, kUnbatchedRenderables},
        {kUnbatchedStatsValue, {0xC7, 0x02}, 2, kGameUnbatchedRenderables, kUnbatchedRenderables},
    };
    for (const Value& value : values) {
        if (memcmp(reinterpret_cast<const void*>(value.at - value.openingSize), value.opening, value.openingSize) != 0 ||
            rfa::CodeValue(value.at) != value.game) {
            rfa::Log("Unbatched renderables: the game's code at 0x%08lX isn't as expected; it keeps its %lu a frame", value.at, kGameUnbatchedRenderables);
            return;
        }
    }
    for (size_t i = 0; i < std::size(values); ++i) {
        if (rfa::PatchCode(reinterpret_cast<void*>(values[i].at), &values[i].ours, sizeof(values[i].ours))) continue;
        // Not changed: the ones before it go back as they were, the last changed first.
        bool back = true;
        for (size_t k = i; k-- > 0;)
            back = rfa::PatchCode(reinterpret_cast<void*>(values[k].at), &values[k].game, sizeof(values[k].game)) && back;
        if (back)
            rfa::Log("Unbatched renderables: could not change the game's code at 0x%08lX; it keeps its %lu a frame", values[i].at, kGameUnbatchedRenderables);
        else
            rfa::Log("Unbatched renderables: could not change the game's code at 0x%08lX, nor put back what was changed before it", values[i].at);
        return;
    }
    rfa::Log("Unbatched renderables: %lu a frame (the game's %lu), in the mod's own list", kUnbatchedRenderables, kGameUnbatchedRenderables);
}

// ---- Room in the sound engine's own memory
//
// Wwise keeps what its banks describe (every sound, container, event and curve) and a record for every sound
// playing or virtual in one pool of its own, "Default", whose size the game hands it when it starts: 0x2C8000 bytes
// (2.78 MB; 0x536626 `mov dword ptr [ebp-54h],2C8000h` in the audio set-up's values, copied to 0x27454E0 and
// given to Wwise as its default pool's size at 0xB978D1). Wwise takes its pools' memory from the process itself
// (its memory hooks are the game's 0xB975A0, the C runtime's aligned allocation, and 0xB975E0, VirtualAlloc), not
// from the game's audio pool (0x26AF858, 0xF80000 bytes, of which 15,701,808 are still free once Wwise has
// started), so the audio cache, made of what that pool has left, keeps its size. With Guerrilla's 31 banks'
// objects in it the pool stood at 2,680-2,720 of 2,844 KB all through the run of 2026-11-11 01:20 (95%), which
// ended with all sound gone. The user's OK for the sound limits was asked through the main session. The mod made
// the size 6 MB (the value checked first, and that the audio hasn't started), then 20 MB (2026-11-15, the user's
// "Yes, raise it"): Guerrilla's people's voices are one bank loaded at boot, 5.4 MB of events, since Armageddon's
// bank list holds 300 banks at most (see GuerrillaSoundBanks.GameBankLimit). 17.2 MB more of the process's memory
// than the game's. The log's "Wwise memory:" lines and F7 Limits show its use.
constexpr DWORD kSoundEngineMemoryValue = 0x536629;  // the value of the mov at 0x536626 (C7 45 AC, then it)
constexpr DWORD kGameSoundEngineMemory = 0x2C8000, kSoundEngineMemory = 0x1400000;
constexpr DWORD kSoundEngineMemoryInUse = 0x27454E0;  // where the audio set-up keeps the size once it has run

void MoreSoundEngineRoom() {
    if (*reinterpret_cast<const DWORD*>(kSoundEngineMemoryInUse) != 0) {
        rfa::Log("Sound engine memory: the game's audio has already started; it keeps its %lu bytes", kGameSoundEngineMemory);
        return;
    }
    const BYTE opening[] = {0xC7, 0x45, 0xAC};
    if (memcmp(reinterpret_cast<const void*>(kSoundEngineMemoryValue - sizeof(opening)), opening, sizeof(opening)) != 0 ||
        rfa::CodeValue(kSoundEngineMemoryValue) != kGameSoundEngineMemory) {
        rfa::Log("Sound engine memory: the game's code (0x00536626) isn't as expected; it keeps its %lu bytes", kGameSoundEngineMemory);
        return;
    }
    if (!rfa::PatchCode(reinterpret_cast<void*>(kSoundEngineMemoryValue), &kSoundEngineMemory, sizeof(kSoundEngineMemory))) {
        rfa::Log("Sound engine memory: could not change the game's code; it keeps its %lu bytes", kGameSoundEngineMemory);
        return;
    }
    rfa::Log("Sound engine memory: %lu bytes (the game's %lu)", kSoundEngineMemory, kGameSoundEngineMemory);
}

// ---- Room for the sound engine's streams
//
// Wwise reads every streamed sound through one pool of fixed pieces, "Stream I/O": the game gives it 0x240000 bytes
// (2.25 MB; 0x53661F `mov dword ptr [ebp-58h],240000h` in the audio set-up's values, copied to 0x27454DC and
// handed over as the stream device's memory at 0xB9783F) in pieces of 0x6800 bytes (0xB97848): 88 pieces for all
// streams at once. That was sized for Armageddon's Vorbis, where a piece is two seconds of sound. Guerrilla's
// sounds are all streams of uncompressed sound (a mono one-shot plays a piece in 0.42 s, a stereo engine loop in
// 0.15 s), and the top bus lets 256 play. In the run of 2026-11-11 02:41 the pool read full (2,288 of 2,288 KB),
// and a building's collapse gave Wwise's "Source starvation" for twenty destruction sounds and an engine.
// Each open stream also has a record in a second pool, "Stream Manager": 0x10000 bytes (0xB97801
// `mov dword ptr [ebp-7Ch],10000h`, the stream manager's own settings, just before it is made). 18 of its 60 KB
// were in use with 81 pieces out, so the pieces can't be raised alone: 315 pieces would need about 70 KB, and a
// full record pool is "Not enough memory to start stream".
// The user's OK for the sound limits was asked through the main session ("you have the okay to raise the limits
// more"); both numbers were told to them. The mod made the pieces' memory 8 MB (315 pieces) and the records'
// 256 KB, both or neither (each value and its opcode checked first, and that the audio hasn't started): about
// 6 MB more of the process's memory, none of the game's audio pool (Wwise's pools are the process's own, see "Room
// in the sound engine's own memory"). The log's "Wwise memory:" lines and F7 Limits show both pools' use.
// 2026-11-19 (the user, through the main session: "raise the audio limits"): 16 MB, 630 pieces, for Wwise reading
// each stream a second ahead where it read 380 ms (see "How far ahead the sound engine reads"); at 380 ms the most
// ever used was 5 MB of the 8. 630 pieces need about 140 KB of records, so the records' 256 KB stay.
constexpr DWORD kStreamMemoryValue = 0x536622;   // the value of the mov at 0x53661F (C7 45 A8, then it)
constexpr DWORD kGameStreamMemory = 0x240000, kStreamMemory = 0x1000000;
constexpr DWORD kStreamMemoryInUse = 0x27454DC;  // where the audio set-up keeps the size once it has run
constexpr DWORD kStreamRecordsValue = 0xB97804;  // the value of the mov at 0xB97801 (C7 45 84, then it)
constexpr DWORD kGameStreamRecords = 0x10000, kStreamRecords = 0x40000;

void MoreStreamRoom() {
    if (*reinterpret_cast<const DWORD*>(kStreamMemoryInUse) != 0) {
        rfa::Log("Stream memory: the game's audio has already started; it keeps its %lu bytes for the streams' pieces and %lu for their records",
                 kGameStreamMemory, kGameStreamRecords);
        return;
    }
    const BYTE memoryOpening[] = {0xC7, 0x45, 0xA8}, recordsOpening[] = {0xC7, 0x45, 0x84};
    if (memcmp(reinterpret_cast<const void*>(kStreamMemoryValue - sizeof(memoryOpening)), memoryOpening, sizeof(memoryOpening)) != 0 ||
        rfa::CodeValue(kStreamMemoryValue) != kGameStreamMemory ||
        memcmp(reinterpret_cast<const void*>(kStreamRecordsValue - sizeof(recordsOpening)), recordsOpening, sizeof(recordsOpening)) != 0 ||
        rfa::CodeValue(kStreamRecordsValue) != kGameStreamRecords) {
        rfa::Log("Stream memory: the game's code (0x0053661F, 0x00B97801) isn't as expected; it keeps its %lu bytes for the streams' pieces and %lu for their records",
                 kGameStreamMemory, kGameStreamRecords);
        return;
    }
    // Both or neither: more pieces without more records would only move the shortage.
    if (!rfa::PatchCode(reinterpret_cast<void*>(kStreamRecordsValue), &kStreamRecords, sizeof(kStreamRecords))) {
        rfa::Log("Stream memory: could not change the game's code; it keeps its %lu bytes for the streams' pieces and %lu for their records",
                 kGameStreamMemory, kGameStreamRecords);
        return;
    }
    if (!rfa::PatchCode(reinterpret_cast<void*>(kStreamMemoryValue), &kStreamMemory, sizeof(kStreamMemory))) {
        const bool back = rfa::PatchCode(reinterpret_cast<void*>(kStreamRecordsValue), &kGameStreamRecords, sizeof(kGameStreamRecords));
        rfa::Log("Stream memory: could not change the game's code; it keeps its %lu bytes for the streams' pieces, and for their records %lu",
                 kGameStreamMemory, back ? kGameStreamRecords : kStreamRecords);
        return;
    }
    rfa::Log("Stream memory: %lu bytes for the streams' pieces (the game's %lu: %lu pieces where it had %lu) and %lu for their records (the game's %lu)",
             kStreamMemory, kGameStreamMemory, kStreamMemory / 0x6800, kGameStreamMemory / 0x6800, kStreamRecords, kGameStreamRecords);
}

// ---- How far ahead the sound engine reads its streams
//
// Wwise keeps each playing stream a set time ahead of what it plays (its stream device's "target buffer length").
// The game takes Wwise's default for it: 380 ms (the float at 0x10AE6C8, read only by AK::StreamMgr::
// GetDefaultDeviceSettings 0xECAB60, which only the game's audio set-up calls, at 0xB97830; the game then sets the
// device's memory, pieces, scheduler and 18 requests at once, not this). That was enough for Armageddon's Vorbis.
// Every one of Guerrilla's sounds is a stream of uncompressed sound read through the game's own reader (one thread,
// 0xB9C310, 24 requests at a time, through its cache of pieces), and the runs of 2026-11-19 had Wwise's "Source
// starvation" 120 times (the player's engine's layers, other vehicles', destruction hits), with the streams' pools
// nearly empty and the reader asked for only a few pieces a second: the pieces came late, not too few. A second
// ahead rides out a late piece nearly three times as long. The user, through the main session: "raise the audio
// limits". The value is changed only while it is Wwise's 380 and the audio hasn't started (0x27454DC still 0).
constexpr DWORD kStreamLookAheadValue = 0x10AE6C8;
constexpr float kGameStreamLookAheadMs = 380.0f, kStreamLookAheadMs = 1000.0f;

void MoreStreamLookAhead() {
    if (*reinterpret_cast<const DWORD*>(kStreamMemoryInUse) != 0) {
        rfa::Log("Stream look-ahead: the game's audio has already started; it keeps Wwise's %.0f ms", kGameStreamLookAheadMs);
        return;
    }
    if (*reinterpret_cast<const float*>(kStreamLookAheadValue) != kGameStreamLookAheadMs) {
        rfa::Log("Stream look-ahead: Wwise's value (0x010AE6C8) isn't the %.0f ms expected; left as it is", kGameStreamLookAheadMs);
        return;
    }
    if (!rfa::PatchCode(reinterpret_cast<void*>(kStreamLookAheadValue), &kStreamLookAheadMs, sizeof(kStreamLookAheadMs))) {
        rfa::Log("Stream look-ahead: could not change Wwise's value; it keeps %.0f ms", kGameStreamLookAheadMs);
        return;
    }
    rfa::Log("Stream look-ahead: Wwise reads each stream %.0f ms ahead (its default %.0f ms)", kStreamLookAheadMs, kGameStreamLookAheadMs);
}

// ---- Room in the game's audio pool, and so in its cache of stream pieces
//
// The game's pool "audio" (0x26AF858; size +0x44, used +0x4C) is made at start-up in 0xA45600 from the start-up
// allocator 0xA44A20 (the process's own memory, aligned) with the size in [ebp-1F8h]: 0xF80000 bytes (15.5 MB), set
// by one of two identical movs a flag picks (0xA46B8A and 0xA46B96: mov dword ptr [ebp-1F8h], then the value). The
// banks' slots are cut from it, and all that is left becomes the cache of stream pieces in front of the game's
// reader (0xB9B870: half of it as blocks of 0x6810 bytes, the rest as blocks of 0x2810, their count worked out from
// the room, so a bigger pool is a bigger cache). It was full in every run (15,856 of 15,872 KB: about 10 MB of
// cache) and a third of the pieces asked for had to come from the disk. An engine of Guerrilla's is 2-3 MB of loops
// read again on every turn of the loop, so 10 MB held four vehicles' engines. The user, through the main session:
// "raise the audio limits": 32 MB (a cache of about 26 MB), both values or neither, before the pool is made.
constexpr DWORD kAudioPool = 0x26AF858;
constexpr DWORD kAudioPoolValues[] = {0xA46B90, 0xA46B9C};  // the values of the two movs at 0xA46B8A and 0xA46B96
constexpr DWORD kGameAudioPool = 0xF80000, kAudioPoolSize = 0x2000000;

void MoreAudioPool() {
    if (*reinterpret_cast<const DWORD*>(kAudioPool + 0x44) != 0) {
        rfa::Log("Audio pool: already made; it keeps its %lu bytes", *reinterpret_cast<const DWORD*>(kAudioPool + 0x44));
        return;
    }
    const BYTE opening[] = {0xC7, 0x85, 0x08, 0xFE, 0xFF, 0xFF};  // mov dword ptr [ebp-1F8h], value
    for (DWORD value : kAudioPoolValues)
        if (memcmp(reinterpret_cast<const void*>(value - sizeof(opening)), opening, sizeof(opening)) != 0 || rfa::CodeValue(value) != kGameAudioPool) {
            rfa::Log("Audio pool: the game's code (0x%08lX) isn't as expected; it keeps its %lu bytes", value - static_cast<DWORD>(sizeof(opening)),
                     kGameAudioPool);
            return;
        }
    if (!rfa::PatchCode(reinterpret_cast<void*>(kAudioPoolValues[0]), &kAudioPoolSize, sizeof(kAudioPoolSize))) {
        rfa::Log("Audio pool: could not change the game's code; it keeps its %lu bytes", kGameAudioPool);
        return;
    }
    if (!rfa::PatchCode(reinterpret_cast<void*>(kAudioPoolValues[1]), &kAudioPoolSize, sizeof(kAudioPoolSize))) {
        const bool back = rfa::PatchCode(reinterpret_cast<void*>(kAudioPoolValues[0]), &kGameAudioPool, sizeof(kGameAudioPool));
        rfa::Log("Audio pool: could not change the game's code; it keeps its %lu bytes%s", kGameAudioPool,
                 back ? "" : " (one of its two values could not be put back)");
        return;
    }
    rfa::Log("Audio pool: %lu bytes (the game's %lu); what the banks' slots leave of it is the cache of stream pieces", kAudioPoolSize, kGameAudioPool);
}

// ---- A sound's distance, given to Wwise whenever it has changed
//
// Every sound of Guerrilla's fades with the game parameter game_distance (0x85C3E987), which Armageddon's audiolib
// sets on each sound object (see WwiseBankWriter.GameDistance). Its update of an object (0xB81D00, every 33 ms)
// measures the nearest of the object's places from the listener into +0x0C and sends it only if 0xB86900 says it
// has changed: by more than 0.05 m from +0x10, which the update then sets to +0x0C every time, sent or not. So the
// change it looks at is one tick's: under 1.5 m/s between the listener and the sound (walking away from a kiosk,
// a car rolling up), nothing is sent, and Wwise keeps the distance last sent however far the two drift apart.
// Armageddon's own sounds hardly use the parameter (Wwise's own attenuation places them); for ours it is the whole
// fade. The user, 2026-11-19: "not respecting the distance, for instance the kiosk".
// Guerrilla works out a sound's volume from its distance at every update. The mod replaces 0xB86900 (a thiscall
// on the object, called only from that update) so the distance is compared with the one last sent for that
// object: sent once it differs by more than the game's 0.05 m, the first time the mod sees the object (or after
// 100 ms without an update: the game reuses its sound objects' records), and again every 500 ms in any case, so
// a value Wwise dropped with a game object it forgot is put back soon. A line every 10 s (with Log=1) says how
// many were sent, and how many the game's own rule would have sent.
constexpr DWORD kDistanceChanged = 0xB86900;
constexpr float kDistanceStep = 0.05f;  // the game's own
constexpr DWORD kDistanceGapMs = 100, kDistanceResendMs = 500;

struct SentDistance {
    float distance;
    DWORD sentAt, seenAt;
};
std::unordered_map<const BYTE*, SentDistance> g_sentDistances;
SRWLOCK g_sentDistancesLock = SRWLOCK_INIT;
DWORD g_distancesSince = 0;
long g_distanceUpdates = 0, g_distancesSent = 0, g_distancesByGameRule = 0;
int g_distanceLines = 0;

bool __fastcall DistanceToSend(const BYTE* object, void* /*unused*/) {
    const float distance = *reinterpret_cast<const float*>(object + 0x0C);
    const float lastUpdate = *reinterpret_cast<const float*>(object + 0x10);
    const DWORD now = GetTickCount();
    AcquireSRWLockExclusive(&g_sentDistancesLock);
    auto [entry, first] = g_sentDistances.try_emplace(object, SentDistance{distance, now, now});
    SentDistance& sent = entry->second;
    const bool send = first || now - sent.seenAt > kDistanceGapMs || fabsf(distance - sent.distance) > kDistanceStep ||
                      now - sent.sentAt >= kDistanceResendMs;
    sent.seenAt = now;
    if (send) {
        sent.distance = distance;
        sent.sentAt = now;
    }
    ++g_distanceUpdates;
    if (send) ++g_distancesSent;
    if (fabsf(distance - lastUpdate) > kDistanceStep) ++g_distancesByGameRule;
    if (g_distancesSince == 0) g_distancesSince = now;
    if (now - g_distancesSince >= 10000 && g_distanceLines < 30) {
        ++g_distanceLines;
        rfa::LogDetail("Sound distances (10 s): %ld updates of %u sound objects, %ld distances sent to Wwise (the game's own rule: %ld)", g_distanceUpdates,
                       static_cast<unsigned>(g_sentDistances.size()), g_distancesSent, g_distancesByGameRule);
        g_distancesSince = now;
        g_distanceUpdates = g_distancesSent = g_distancesByGameRule = 0;
    }
    ReleaseSRWLockExclusive(&g_sentDistancesLock);
    return send;
}

void InstallSoundDistances() {
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D, 0xF8};  // push ebp / mov ebp,esp / sub esp,0Ch / mov [ebp-8],ecx
    if (memcmp(reinterpret_cast<const void*>(kDistanceChanged), opening, sizeof(opening)) != 0) {
        rfa::Log("Sound distances: the game's code (0x00B86900) isn't as expected; a sound's distance goes to Wwise by the game's own rule");
        return;
    }
    if (!rfa::ReplaceFunction(kDistanceChanged, reinterpret_cast<const void*>(&DistanceToSend))) {
        rfa::Log("Sound distances: could not change the game's code; a sound's distance goes to Wwise by the game's own rule");
        return;
    }
    rfa::Log("Sound distances: each sound's distance goes to Wwise once it differs by more than %.2f m from the one last sent (the game: from the one "
             "33 ms before), and every %lu ms in any case",
             kDistanceStep, kDistanceResendMs);
}

// ---- The music's format test (console command rfg_music_test)
//
// Guerrilla's music is to go into Wwise's own ADPCM (a quarter of uncompressed sound: 590 MB for the campaign's
// music where our other banks' format would be 2.3 GB). Armageddon's sound engine has that codec built in (its
// source factory 0xEB0470 makes the ADPCM sources for codec 2 as it makes PCM's for codec 1; the decoder 0xEC0DE0),
// but none of the game's own banks use it, so one listen is to settle it before the music is built. The world
// builder makes one score's ambience clips as a bank of ADPCM media and lists them in
// mods\guerrilla_world\music_test.txt (GuerrillaMusic.BuildFormatTest: "format" and its words, then a line a clip:
// "clip", its event, its stop event, seconds, rate, channels, bank; tabs between).
// The command, typed in the game's console in any level: each time it stops the clip before and plays the next of
// the list, in 2D on the game's music bus, on a sound object of its own (0xB83000 makes one at a place, as the game
// does for its own lone sounds; 0xB846E0 plays an event on it and gives a handle; 0xB84D20 says whether a handle
// still plays). It writes to the log what it started and whether the game took it ("STARTED" / "NOT STARTED"),
// and two seconds later whether the clip is still playing: so silence with "STARTED" and "still playing" is a
// fault of the sound itself, and "NOT STARTED" or "has ended already" one of the bank or the file (Wwise's own
// word for it is then in a "Wwise:" line). A command is registered as the campaign mod's is (0xC7BD70: the object,
// name, description, type 3: a handler without arguments; 0x28FF1C0 is set while it is run and not asked for help).
struct MusicTestClip {
    std::string event, stopEvent, bank;
    float seconds;
    int rate, channels;
};
std::vector<MusicTestClip> g_musicTestClips;
std::string g_musicTestFormat = "an unnamed format";
DWORD g_musicTestObject = 0, g_musicTestHandle = 0, g_musicTestStarted = 0;
int g_musicTestNext = 0, g_musicTestPlaying = -1;
bool g_musicTestChecked = true;
BYTE g_musicTestCommand[32];

auto const MakeSoundObject = reinterpret_cast<DWORD(__cdecl*)(const float* place, int zero)>(0xB83000);
auto const PlaySoundEvent = reinterpret_cast<int(__cdecl*)(DWORD soundObject, DWORD eventId, DWORD* handle, int unused, int delayMs)>(0xB846E0);

// Wwise's id of a name: FNV-1 (32 bits) of its lower-case letters.
DWORD WwiseNameId(const char* name) {
    DWORD h = 2166136261u;
    for (; *name; ++name) h = (h * 16777619u) ^ static_cast<BYTE>(*name >= 'A' && *name <= 'Z' ? *name + 32 : *name);
    return h;
}

void LoadMusicTest() {
    std::vector<BYTE> file;
    if (!ReadWholeFile(DataFolder() + L"music_test.txt", file)) return;
    const std::string text(file.begin(), file.end());
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        std::vector<std::string> words;
        for (size_t from = 0; from <= line.size();) {
            size_t tab = line.find('\t', from);
            if (tab == std::string::npos) tab = line.size();
            words.push_back(line.substr(from, tab - from));
            from = tab + 1;
        }
        if (words.size() == 2 && words[0] == "format") g_musicTestFormat = words[1];
        else if (words.size() == 7 && words[0] == "clip")
            g_musicTestClips.push_back({words[1], words[2], words[6], static_cast<float>(atof(words[3].c_str())), atoi(words[4].c_str()), atoi(words[5].c_str())});
    }
}

bool MusicTestPlaying() { return g_musicTestHandle && SoundGameObject(g_musicTestHandle) != 0; }

void MusicTestCommand() {
    if (!*reinterpret_cast<const int*>(0x28FF1C0)) return;  // asked for its help text, not run
    if (g_musicTestClips.empty()) LoadMusicTest();
    if (g_musicTestClips.empty()) {
        rfa::Log("Music test: mods\\guerrilla_world\\music_test.txt names no clip (build the world part again); nothing played");
        return;
    }
    // The clip before: how it went, and its end.
    if (g_musicTestPlaying >= 0) {
        const MusicTestClip& before = g_musicTestClips[g_musicTestPlaying];
        const bool playing = MusicTestPlaying();
        rfa::Log("Music test: clip %d (%.1f s long) %s %.1f s after its start", g_musicTestPlaying + 1, before.seconds,
                 playing ? "was still playing" : "had ended", (GetTickCount() - g_musicTestStarted) / 1000.0f);
        if (playing) PlaySoundEvent(g_musicTestObject, WwiseNameId(before.stopEvent.c_str()), nullptr, 0, 0);
    }
    if (!g_musicTestObject) {
        const float nowhere[3] = {0.0f, 0.0f, 0.0f};
        g_musicTestObject = MakeSoundObject(nowhere, 0);
    }
    const MusicTestClip& clip = g_musicTestClips[g_musicTestNext];
    DWORD handle = 0;
    const int result = g_musicTestObject ? PlaySoundEvent(g_musicTestObject, WwiseNameId(clip.event.c_str()), &handle, 0, 0) : -1;
    rfa::Log("Music test: clip %d of %u, event %s (bank %s; %s; %d Hz, %d channels, %.1f s): %s (sound object 0x%08lX, handle %lu, result %d)",
             g_musicTestNext + 1, static_cast<unsigned>(g_musicTestClips.size()), clip.event.c_str(), clip.bank.c_str(), g_musicTestFormat.c_str(), clip.rate,
             clip.channels, clip.seconds,
             handle ? "STARTED" : "NOT STARTED (the event or its bank isn't loaded, or the game gave no sound object)", g_musicTestObject, handle, result);
    g_musicTestHandle = handle;
    g_musicTestStarted = GetTickCount();
    g_musicTestPlaying = g_musicTestNext;
    g_musicTestChecked = handle == 0;
    g_musicTestNext = (g_musicTestNext + 1) % static_cast<int>(g_musicTestClips.size());
}

// Each time an effect starts: the command registered (once, the game running by then), and two seconds after a
// clip's start a line on whether it still plays.
void MusicTestFrame() {
    static bool registered;
    if (!registered) {
        registered = true;
        using RegisterCommandFn = void*(__fastcall*)(void* command, void* unused, const char* name, const char* description, int type, void (*handler)());
        reinterpret_cast<RegisterCommandFn>(0xC7BD70)(g_musicTestCommand, nullptr, "rfg_music_test",
                                                       "Plays the next clip of the music format test (Guerrilla's ambience as Wwise ADPCM)", 3, &MusicTestCommand);
        rfa::Log("Music test: console command rfg_music_test added");
    }
    if (g_musicTestChecked || GetTickCount() - g_musicTestStarted < 2000) return;
    g_musicTestChecked = true;
    rfa::Log("Music test: two seconds on, clip %d %s", g_musicTestPlaying + 1,
             MusicTestPlaying() ? "is still playing: Wwise took the file and is decoding it"
                                : "has ended already: it did not play (Wwise's reason, if it gave one, is in a \"Wwise:\" line)");
}

// ---- How many sounds the game plays at once
//
// Armageddon's top bus over its effects, vehicles, weapons and destruction (0x1001382D, in init.bnk_pc) lets 128
// sounds play at once, silent and virtual ones counted, and ends the oldest of the lowest priority for one more,
// without a word from Wwise. Guerrilla had no such cap and its world filled this one (other vehicles' engines and
// held guns ended in a fight, hits were cut short: the run of 2026-11-11 00:43). The user: "you have the okay to
// raise the limits more". The Guerrilla builder writes an override of the player's own init.bnk_pc with that one
// number made 256 (GuerrillaSoundBanks.WriteBusLimit; the game's code isn't touched); this says in the log what
// the override holds (a bus: an object of type 8, its limit 25 bytes after its id). The same override lets the
// effects' bus for one-shots (0x2785E1AB) play 64 where Armageddon's let 15 (2026-11-19: a car through a building
// had 1,833 destruction impacts in ten seconds, and the one-shots past 15 never sounded).
constexpr DWORD kTopSoundBus = 0x1001382D, kEffectsSoundBus = 0x2785E1AB;
constexpr DWORD kGameSoundsAtOnce = 128, kGameEffectSoundsAtOnce = 15;

void LogSoundLimit() {
    std::vector<BYTE> bank;
    if (!ReadWholeFile(rfa::RootDir() + L"\\overrides\\build\\pc\\cache\\soundboot.vpp_pc\\init.bnk_pc", bank)) {
        rfa::Log("Sounds at once: the game's own %lu (there is no override of its init.bnk_pc)", kGameSoundsAtOnce);
        return;
    }
    WORD top = 0, effects = 0;
    for (size_t at = 0; at + 8 <= bank.size();) {
        DWORD size = 0;
        memcpy(&size, &bank[at + 4], sizeof(size));
        if (size > bank.size() || at + 8 + size > bank.size()) break;
        if (memcmp(&bank[at], "HIRC", 4) == 0 && size >= 4) {
            DWORD count = 0;
            memcpy(&count, &bank[at + 8], sizeof(count));
            size_t object = at + 12;
            for (DWORD k = 0; k < count && object + 12 <= at + 8 + size; ++k) {
                DWORD type = 0, length = 0, id = 0;
                memcpy(&type, &bank[object], sizeof(type));
                memcpy(&length, &bank[object + 4], sizeof(length));
                memcpy(&id, &bank[object + 8], sizeof(id));
                if (length < 4 || object + 8 + length > bank.size()) break;
                if (type == 8 && (id == kTopSoundBus || id == kEffectsSoundBus) && length >= 27)
                    memcpy(id == kTopSoundBus ? &top : &effects, &bank[object + 8 + 25], sizeof(WORD));
                object += 8 + length;
            }
        }
        at += 8 + size;
    }
    if (top == 0) {
        rfa::Log("Sounds at once: the override of init.bnk_pc has no bus 0x%08lX; the limit isn't known", kTopSoundBus);
        return;
    }
    rfa::Log("Sounds at once: %u (the game's own %lu), the top bus's limit, and %u of the effects' one-shots (the game's own %lu), in the override of init.bnk_pc",
             top, kGameSoundsAtOnce, effects, kGameEffectSoundsAtOnce);
}

// ---- Room for more visual effects

// The game keeps every visual effect vfx.xtbl describes (and a DLC level's, while it's loaded) in a
// table of 950 (0xB4 bytes each from 0x121FDE8, how many at 0x1200BF4); its entry reader (0x4EE340)
// leaves out each one past it (cmp [1200BF4h],3B6h). Armageddon's own are 904, so only 46 of the 87
// visuals Guerrilla's props show fitted. Seven functions reach the table by its address (the base
// table's reader and a DLC level's, the entry readers, the look-ups by name and number), 13 values,
// and the start-up constructor (0xF1C8B0) builds its entries with 0x4EF6A0 (+0x94 = -1). The mod
// moves it to a table of 2048, builds the new entries with that constructor itself (in case the
// game's start-up constructor already ran), and the world builder fills to the same
// (GuerrillaEffectTables.VisualLimit). The effects (effects.xtbl) are in a table of their own, found
// by name through a lookup of their own: see "Room for more effects".
constexpr DWORD kVisuals = 0x121FDE8;
constexpr DWORD kVisualSize = 0xB4;
constexpr DWORD kGameMaxVisuals = 950;
constexpr DWORD kVisualCount = 0x1200BF4;
constexpr DWORD kVisualReferences[] = {0x4EA941, 0x4EAA96, 0x4EAB97, 0x4EAF3F, 0x4EB0AD, 0x4EDFDA, 0x4EE03A,
                                       0x4EE18E, 0x4EE2D7, 0x4EE423, 0x4EF412, 0x4EF482, 0xF1C8C3};
constexpr DWORD kVisualLimits[] = {0x4EE40A, 0xF1C8B9};  // the entry reader's check, the constructor's push 3B6h
constexpr DWORD kMaxVisuals = 2048;
alignas(16) BYTE g_visuals[kMaxVisuals * kVisualSize];  // the new table, filled by the game
auto const BuildVisual = reinterpret_cast<void*(__fastcall*)(void* visual, void* unused)>(0x4EF6A0);

void MoveVisuals() {
    if (*reinterpret_cast<const int*>(kVisualCount) != 0) {
        rfa::Log("Visual effects: the game has already read some; it keeps its %lu", kGameMaxVisuals);
        return;
    }
    for (DWORD i = 0; i < kMaxVisuals; ++i) BuildVisual(g_visuals + i * kVisualSize, nullptr);
    rfa::MoveTable("Visual effects", kVisuals, kVisualSize, kGameMaxVisuals, kVisualReferences, kVisualLimits, g_visuals, kMaxVisuals);
}

// ---- Room for more effects

// The game keeps every effect effects.xtbl describes (and a DLC level's, while it's loaded) in a
// table of 1225 (0x68 bytes each from 0x1200C40, how many at 0x1200BEC; the entry reader 0x4EE950
// stops at cmp [1200BECh],4C9h). Armageddon's own are 1112 and the world's objects add about 100, so
// Guerrilla's vehicles' fire, smoke, exhaust and explosions (and later its lights and weapons) don't
// fit. Seven places in the game's code hold the table's address (the reader, the DLC level's unload,
// the look-ups by name and number, an effect host reading an entry's +0x44, the start-up constructor
// 0xF1C890, which builds the entries with 0x4EF790), and two its size: the mod moves it to a table of
// 4096, as the visual effects.
//
// The game finds an effect by its name through a lookup of its own at 0x124BBE0 (the effect entry's
// name → the entry), and a loaded effect file through another of the same kind at 0x11E9EF8 (the
// file's name, copied → its template): a hash table with 1225 places built into each object (a 16-bit
// free list head at +4, 1225 buckets at +6, the chains at +0x998, the names at +0x132C, the values at
// +0x2650, the file map's copied names at +0x3974), whose methods reach those by displacement. Only
// those two objects use the class (its two constructors are called only by the start-up code for
// them, 0xF1C6B0 and 0xF1C8F0), and nothing else reaches its arrays. So the mod replaces the class's
// methods with its own, which keep each object's names in a lookup of 4096 here: the same kind of
// hash (h × 33 ^ the lower-case letter), names compared as the game compares them (the same pointer,
// or the same letters in any case), a name already there refused (the first one stays), a removal
// taking the first match, a look-up that misses leaving the answer as it was. The game's start-up
// constructors still run, and their clearing (0x4EDB50) is ours. The methods are __thiscall with the
// callee popping its arguments: __fastcall here, with EDX unused.
constexpr DWORD kEffects = 0x1200C40;
constexpr DWORD kEffectSize = 0x68;
constexpr DWORD kGameMaxEffects = 1225;
constexpr DWORD kEffectCount = 0x1200BEC;
constexpr DWORD kEffectReferences[] = {0x4EEA67, 0x4EF304, 0x4EF33F, 0x4EF3B8, 0x4EF3D7, 0x8EA4FF, 0xF1C8A0};
constexpr DWORD kEffectLimits[] = {0x4EEA51, 0xF1C899};  // the entry reader's check, the constructor's push 4C9h
constexpr DWORD kMaxEffects = 4096;
alignas(16) BYTE g_effects[kMaxEffects * kEffectSize];  // the new table, filled by the game
auto const BuildEffect = reinterpret_cast<void*(__fastcall*)(void* effect, void* unused)>(0x4EF790);

constexpr DWORD kEffectNames = 0x124BBE0;  // effects.xtbl names → effect entries (the names are the entries')
constexpr DWORD kEffectFiles = 0x11E9EF8;  // loaded effect files → their templates (the names copied)

// The class's methods (each one's arguments after `this`; what it gives back).
constexpr DWORD kLookupContains = 0x4ED1D0;      // (name) → bool
constexpr DWORD kLookupRemove = 0x4ED220;        // (name) → its place (unused)
constexpr DWORD kLookupFind = 0x4ED320;          // (name, value*) → bool
constexpr DWORD kLookupInsertCopy = 0x4ED380;    // (name, const value*) → bool: the name copied (the file map)
constexpr DWORD kLookupInsert = 0x4EF4B0;        // (name, const value*) → bool: the name kept as given
constexpr DWORD kLookupClear = 0x4EDB50;         // ()
// The ones only the table's own (replaced) methods, or its never-called virtual slots, reach: given
// stand-ins that say so, rather than left working on the game's arrays, which these no longer fill.
constexpr DWORD kLookupInsertKey = 0x4ED8D0;     // (name) → bool, a virtual slot
constexpr DWORD kLookupSave = 0x4ED900;          // (stream) → bool, a virtual slot
constexpr DWORD kLookupSaveInner = 0x4ED9C0;     // (stream) → bool

constexpr int kLookupPlaces = 4096, kLookupBuckets = 4099, kNameLength = 64;
constexpr WORD kNone = 0xFFFF;
struct NameLookup {
    bool ready;
    WORD free;                         // the first free place, kNone when full
    WORD buckets[kLookupBuckets];      // each bucket's first place
    WORD next[kLookupPlaces];          // the next place in its bucket (or the next free one)
    const char* names[kLookupPlaces];
    void* values[kLookupPlaces];
    char copied[kLookupPlaces][kNameLength];  // the file map's names
};
NameLookup g_effectNames, g_effectFiles;

void ClearLookup(NameLookup& t) {
    t.free = 0;
    for (WORD& b : t.buckets) b = kNone;
    for (int i = 0; i < kLookupPlaces; ++i) t.next[i] = static_cast<WORD>(i + 1 < kLookupPlaces ? i + 1 : kNone);
    t.ready = true;
}

NameLookup* LookupOf(const void* object) {
    NameLookup* t = reinterpret_cast<DWORD>(object) == kEffectNames ? &g_effectNames
                  : reinterpret_cast<DWORD>(object) == kEffectFiles ? &g_effectFiles : nullptr;
    if (!t) {
        RFA_LOG_FIRST(8, "Effects: a name lookup the mod doesn't know (0x%08lX) used the effects' methods; it finds nothing",
                      reinterpret_cast<DWORD>(object));
        return nullptr;
    }
    if (!t->ready) ClearLookup(*t);
    return t;
}

int Bucket(const char* name) {
    unsigned h = 0;
    for (const char* c = name; *c; ++c) h = h * 33 ^ static_cast<unsigned char>(tolower(static_cast<unsigned char>(*c)));
    return static_cast<int>(h % kLookupBuckets);
}

bool SameName(const char* a, const char* b) { return a == b || _stricmp(a, b) == 0; }

// The place holding `name`, or kNone.
WORD PlaceOf(const NameLookup& t, const char* name) {
    for (WORD at = t.buckets[Bucket(name)]; at != kNone; at = t.next[at])
        if (SameName(t.names[at], name)) return at;
    return kNone;
}

// A free place with `name` and `value` in it, or kNone if the name is there already or none is free.
WORD Add(NameLookup& t, const char* name, void* value) {
    if (!name || PlaceOf(t, name) != kNone) return kNone;
    const WORD at = t.free;
    if (at == kNone) {
        RFA_LOG_FIRST(4, "Effects: a name lookup is full (%d); %s was left out", kLookupPlaces, name);
        return kNone;
    }
    t.free = t.next[at];
    const int b = Bucket(name);
    t.next[at] = t.buckets[b];
    t.buckets[b] = at;
    t.names[at] = name;
    t.values[at] = value;
    return at;
}

bool __fastcall LookupContains(void* object, void*, const char* name) {
    const NameLookup* t = LookupOf(object);
    return t && name && PlaceOf(*t, name) != kNone;
}

WORD __fastcall LookupRemove(void* object, void*, const char* name) {
    NameLookup* t = LookupOf(object);
    if (!t || !name) return kNone;
    const int b = Bucket(name);
    for (WORD* link = &t->buckets[b]; *link != kNone; link = &t->next[*link]) {
        const WORD at = *link;
        if (!SameName(t->names[at], name)) continue;
        *link = t->next[at];
        t->next[at] = t->free;
        t->free = at;
        return at;
    }
    return kNone;
}

bool __fastcall LookupFind(void* object, void*, const char* name, void** value) {
    const NameLookup* t = LookupOf(object);
    if (!t || !name) return false;
    const WORD at = PlaceOf(*t, name);
    if (at == kNone) return false;
    *value = t->values[at];
    return true;
}

bool __fastcall LookupInsert(void* object, void*, const char* name, void* const* value) {
    NameLookup* t = LookupOf(object);
    return t && value && Add(*t, name, *value) != kNone;
}

bool __fastcall LookupInsertCopy(void* object, void*, const char* name, void* const* value) {
    NameLookup* t = LookupOf(object);
    if (!t || !value) return false;
    const WORD at = Add(*t, name, *value);
    if (at == kNone) return false;
    strncpy_s(t->copied[at], name, _TRUNCATE);  // as the game's, at most 63 letters
    t->names[at] = t->copied[at];
    return true;
}

void __fastcall LookupClear(void* object, void*) {
    if (NameLookup* t = LookupOf(object)) ClearLookup(*t);
}

bool __fastcall LookupUnused(void* object, void*, void*) {
    RFA_LOG_FIRST(4, "Effects: a name lookup method the game wasn't seen to use was called (0x%08lX); it does nothing",
                  reinterpret_cast<DWORD>(object));
    return false;
}

void MoreEffectRoom() {
    if (*reinterpret_cast<const int*>(kEffectCount) != 0) {
        rfa::Log("Effects: the game has already read some; it keeps its %lu", kGameMaxEffects);
        return;
    }
    // The lookups first: without room for the names, more entries would be read but never found.
    const struct { DWORD address; const void* ours; } methods[] = {
        {kLookupContains, reinterpret_cast<const void*>(&LookupContains)}, {kLookupRemove, reinterpret_cast<const void*>(&LookupRemove)},
        {kLookupFind, reinterpret_cast<const void*>(&LookupFind)}, {kLookupInsertCopy, reinterpret_cast<const void*>(&LookupInsertCopy)},
        {kLookupInsert, reinterpret_cast<const void*>(&LookupInsert)}, {kLookupClear, reinterpret_cast<const void*>(&LookupClear)},
        {kLookupInsertKey, reinterpret_cast<const void*>(&LookupUnused)}, {kLookupSave, reinterpret_cast<const void*>(&LookupUnused)},
        {kLookupSaveInner, reinterpret_cast<const void*>(&LookupUnused)},
    };
    // Each method starts as the game's do (push ebp / mov ebp,esp): checked before any is changed.
    for (const auto& m : methods) {
        const BYTE* code = reinterpret_cast<const BYTE*>(m.address);
        if (code[0] != 0x55 || code[1] != 0x8B || code[2] != 0xEC) {
            rfa::Log("Effects: the game's name lookup at 0x%08lX isn't as expected; the game keeps its %lu effects", m.address, kGameMaxEffects);
            return;
        }
    }
    for (const auto& m : methods) {
        if (!rfa::ReplaceFunction(m.address, m.ours)) {
            rfa::Log("Effects: could not replace the game's name lookup at 0x%08lX (some already are: effects may not be found)", m.address);
            return;
        }
    }
    rfa::Log("Effects: the two name lookups (effects, effect files) hold %d names each (the game's 1225)", kLookupPlaces);
    for (DWORD i = 0; i < kMaxEffects; ++i) BuildEffect(g_effects + i * kEffectSize, nullptr);
    rfa::MoveTable("Effects", kEffects, kEffectSize, kGameMaxEffects, kEffectReferences, kEffectLimits, g_effects, kMaxEffects);
}

// Guerrilla's material shaders. The world builder rewrites them for Armageddon's renderer as effect
// files of the game's own kind (overrides\build\pc\cache\gw_*.fxo_pc_dx11, one name a family of
// Guerrilla's shaders: _s for still meshes, _bs instanced ones, _c skinned ones), and names them in
// the world's materials. The game only loads the effect files inside its archives, all at start-up
// (rl_shader_library::load_shaders, 0xD4CD30, and again for the console's reload_shaders), so once it
// has loaded its own this loads ours as it does each of its: a shader object from its pool
// (0xC3D730), the object's load (the 5th function in its table: it opens the file by name, which the
// loader hands out from overrides\), then the library registers it under the file's name (0xD4D3D0).
// A material whose shader isn't loaded is drawn red (the game's rl_debug_highlight), so a missing
// file shows.
constexpr DWORD kLoadShaders = 0xD4CD30;
constexpr size_t kLoadShadersPrologue = 9;  // push ebp / mov ebp,esp / sub esp,11Ch
using LoadShadersFn = bool(__fastcall*)(void* library, void* unused);
LoadShadersFn g_loadShaders;
auto const NewShader = reinterpret_cast<void*(__cdecl*)()>(0xC3D730);
auto const RegisterShader = reinterpret_cast<bool(__fastcall*)(void* library, void* unused, const char* file, void* shader)>(0xD4D3D0);
using ShaderLoadFn = bool(__fastcall*)(void* shader, void* unused, const char* file);  // the shader's table +0x10
using ShaderReleaseFn = void(__fastcall*)(void* shader, void* unused);                  // its table +0x08

// Our effect files' names (gw_*.fxo_pc_dx11 in overrides\build\pc\cache\), instanced and skinned
// variants first, as the game's own archive lists them.
std::vector<std::string> OurShaderFiles() {
    std::vector<std::string> files;
    WIN32_FIND_DATAW found;
    const std::wstring pattern = rfa::RootDir() + L"\\overrides\\build\\pc\\cache\\gw_*.fxo_pc_dx11";
    HANDLE search = FindFirstFileW(pattern.c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return files;
    do {
        std::string name;  // the builder names them in plain ASCII
        for (const wchar_t* c = found.cFileName; *c; ++c) name += static_cast<char>(*c);
        files.push_back(name);
    } while (FindNextFileW(search, &found));
    FindClose(search);
    std::sort(files.begin(), files.end());
    return files;
}

bool __fastcall LoadShaders(void* library, void* unused) {
    const bool loaded = g_loadShaders(library, unused);  // the game's own
    int count = 0;
    for (const std::string& file : OurShaderFiles()) {
        void* shader = NewShader();
        if (!shader) {
            rfa::Log("No room for more shaders: Guerrilla's from %s on aren't loaded (their materials draw red)", file.c_str());
            break;
        }
        void** functions = *static_cast<void***>(shader);
        if (!reinterpret_cast<ShaderLoadFn>(functions[4])(shader, nullptr, file.c_str())) {
            rfa::Log("Couldn't load the shader %s (its materials draw red)", file.c_str());
            reinterpret_cast<ShaderReleaseFn>(functions[2])(shader, nullptr);
            continue;
        }
        if (!RegisterShader(library, nullptr, file.c_str(), shader)) {
            rfa::Log("Couldn't add the shader %s (its name taken, or the shader library full)", file.c_str());
            continue;
        }
        ++count;
    }
    if (count > 0) rfa::Log("Guerrilla's material shaders: %d files loaded", count);
    return loaded;
}


// ---- Guerrilla's undergrowth (the grass and dust rocks our worlds' terrain files carry, which the
// game's own undergrowth code still loads, scatters over each zone's cells and draws). Its random
// scatter (randomly_place_objects, 0x6C4850) works out each piece's height from its cell's middle
// before it has looked the ground's height up, so every piece it places lands at height 0 (its
// even scatter and Guerrilla's own random scatter, Re-Mars-tered's 0x92B5E0, don't). So it is
// replaced by the same function, in Guerrilla's order, on the game's own helpers and its rand (so
// the pieces fall where Guerrilla's do): for each piece, a model by the layer's weights, a place in
// the cell, the ground's height and normal there, its turn (along the slope if the model says so)
// and size, a tint between its model's two colours; kept where the cell's 1 m mask says the layer
// grows, into the model's active cell (made when it has none yet, room allowing).
constexpr DWORD kRandomlyPlace = 0x6C4850;
constexpr size_t kRandomlyPlacePrologue = 9;  // push ebp / mov ebp,esp / sub esp,0x164

struct GrowthVector { float x, y, z; };
struct GrowthRows { GrowthVector row[3]; };  // a piece's turn and size (rows)

// A layer's model (16 bytes): its mesh's name, its weight, whether it turns to the slope (bit 0),
// how much its size may vary (of 255), the two colours its tint is picked between (blue, green,
// red, alpha bytes).
struct GrowthModel {
    const char* name;
    uint16_t weight;
    uint8_t flags;
    uint8_t scaleNoise;
    uint8_t colourA[4];
    uint8_t colourB[4];
};
struct GrowthLayer { int models; GrowthModel* model; };

// An active cell of one model's pieces, as the game keeps it.
struct GrowthCell {
    uint32_t unused0;
    int count;
    uint8_t* elements;  // 32 bytes a piece
    int model;
    uint32_t unused10;
    GrowthVector middle;
};

// A piece as the game's element constructor (0x6A5420) and packer (0x6A5150) take it.
struct GrowthElement {
    GrowthVector at;     // from the cell's middle
    GrowthRows rows;
    float tint[4];
    float sway;
    uint8_t more[64];    // whatever else the constructor sets
};

using PickModelFn = int(__cdecl*)(GrowthLayer** layer, int totalWeight);                      // 0x6C4EA0
using GrowthRandFn = int(__cdecl*)();                                                          // 0xDA9376, the C runtime's rand
using GroundAtFn = bool(__cdecl*)(float* height, float x, float z, GrowthVector* normal, int flags);  // 0x7748E0: false where no ground is loaded
using OrientFn = void(__fastcall*)(GrowthRows* rows, void* unused, const GrowthVector* up, const GrowthVector* right);  // 0xC9DE80
using TurnFn = void(__fastcall*)(GrowthRows* rows, void* unused, float angle);                // 0xC9E780
using PoolRoomFn = bool(__fastcall*)(void* pool, void* unused);                               // 0x664C80
using MempoolRoomFn = bool(__fastcall*)(void* mempool, void* unused, int size, int flags);    // 0xCC6410
using NewCellFn = GrowthCell*(__fastcall*)(void* list, void* unused, int flags);              // 0x6C63D0
using MempoolTakeFn = uint8_t*(__fastcall*)(void* mempool, void* unused, int size, int a, int b, int c);  // 0xCC6570
using ElementInitFn = void(__fastcall*)(GrowthElement* element, void* unused);                // 0x6A5420
using ElementPutFn = void(__fastcall*)(uint8_t* slot, void* unused, const GrowthElement* element);  // 0x6A5150

const auto PickModel = reinterpret_cast<PickModelFn>(0x6C4EA0);
const auto GrowthRand = reinterpret_cast<GrowthRandFn>(0xDA9376);
const auto GroundAt = reinterpret_cast<GroundAtFn>(0x7748E0);
const auto Orient = reinterpret_cast<OrientFn>(0xC9DE80);
const auto Turn = reinterpret_cast<TurnFn>(0xC9E780);
const auto CellPoolHasRoom = reinterpret_cast<PoolRoomFn>(0x664C80);
const auto MempoolHasRoom = reinterpret_cast<MempoolRoomFn>(0xCC6410);
const auto NewCell = reinterpret_cast<NewCellFn>(0x6C63D0);
const auto MempoolTake = reinterpret_cast<MempoolTakeFn>(0xCC6570);
const auto ElementInit = reinterpret_cast<ElementInitFn>(0x6A5420);
const auto ElementPut = reinterpret_cast<ElementPutFn>(0x6A5150);
using FindFoliageMeshFn = void*(__cdecl*)(const char* name);                                  // 0x972B50, by its ".fmeshx" name
const auto FindFoliageMesh = reinterpret_cast<FindFoliageMeshFn>(0x972B50);
int g_growthCallsLogged = 0;  // the first scatters are logged, to see where the pieces go
int g_growthCalls = 0;        // and every 500th, with the totals so far
long long g_growthKept = 0, g_growthTried = 0, g_growthNoGround = 0, g_growthUnderRock = 0;
void* const kCellPool = reinterpret_cast<void*>(0x1859790);
void* const kCellList = reinterpret_cast<void*>(0x18597AC);
void* const kGrowthMempool = reinterpret_cast<void*>(0x26AFD68);
const auto kIdentityRows = reinterpret_cast<const GrowthRows*>(0x290CB30);
const auto kRightVector = reinterpret_cast<const GrowthVector*>(0x290772C);

// A random number from 0 to 999 over `over` (as the game's: rand() % 1000 / 1000.0).
double GrowthChance(double over = 1000.0) { return (GrowthRand() % 1000) / over; }

// The ground under a point, as Guerrilla's scatter takes it (havok_terrain_height without its ray: the zone's
// height field, whatever stands on it). The game's query (0x7748E0) casts a ray from *height + 1 down to
// -1000 with the terrain-only ray (0x1975F00, layer 0x1C: Guerrilla's COLLIDE_RAY_TERRAIN_ONLY, one layer up in
// Armageddon, as all its ray layers are), which never stops on a building but does on a rock (the stitch
// lists' bodies). So it is asked from the top (998: the world ends at 1000), then again from just under each
// surface it stops on, and the lowest is kept: the ground, under an arch or a mound as much as in the open;
// under a rock sunk into the ground, that rock's underside, where the piece is hidden inside the rock as
// Guerrilla's is. False where no ground is loaded. `over`: how many surfaces were found above the lowest.
bool GroundUnder(float x, float z, float& height, GrowthVector& normal, int& over) {
    float from = 998.0f;
    bool found = false;
    over = 0;
    for (int surfaces = 0; surfaces < 8; ++surfaces) {
        float at = from;
        GrowthVector facing{};
        if (!GroundAt(&at, x, z, &facing, 0)) break;
        if (found) ++over;
        height = at;
        normal = facing;
        found = true;
        from = at - 1.05f;  // the next ray from 5 cm under this surface
    }
    return found;
}

// cells: the active cell of each of the layer's models (null until it has one); count: how many
// pieces to try; zone: the undergrowth zone (+4 its cell size); layer: the layer; record: the cell's
// record of the layer (+2 its 64-bit mask of 1 m squares); low, high: the cell's corners (x, z);
// middle: its middle.
void __cdecl RandomlyPlace(GrowthCell** cells, int count, uint8_t* zone, GrowthLayer** layer, const uint8_t* record,
                           const float* low, const float* high, const GrowthVector* middle) {
    RFA_LOG_FIRST(1, "Undergrowth: pieces scattered on the ground (the game's random scatter replaced)");
    const bool logThis = g_growthCallsLogged < 12;
    int kept = 0, firstModel = -1, withoutGround = 0, underRock = 0;
    GrowthVector firstAt{};
    float firstHeight = 0;
    int totalWeight = 0;
    for (int m = 0; m < (*layer)->models; ++m) totalWeight += (*layer)->model[m].weight;
    const float across = high[0] - low[0];
    const float cellSize = *reinterpret_cast<const float*>(zone + 4);
    for (int k = 0; k < count; ++k) {
        const int m = PickModel(layer, totalWeight);
        const GrowthModel& model = (*layer)->model[m];
        // Its place, the ground's height and normal there.
        GrowthVector offset{};
        offset.x = static_cast<float>((GrowthChance() - 0.5) * across);
        offset.z = static_cast<float>((GrowthChance() - 0.5) * across);
        GrowthVector at{offset.x + middle->x, 0, offset.z + middle->z};
        // The ground's height there (see GroundUnder; asked from 0, as the game's own scatter asks through
        // 0x6C4820, the ray began at 1 m and found no ground above it, and no piece was ever kept). Where no
        // ground is loaded yet (a cell made while a level loads) the piece is left out; its cell is made again
        // with the rest when the player comes back.
        float height = 0;
        GrowthVector normal{};
        int over = 0;
        if (!GroundUnder(at.x, at.z, height, normal, over)) { ++withoutGround; continue; }
        if (over > 0) ++underRock;
        at.y = height;
        offset.y = at.y - middle->y;
        // Its turn: along the slope if its model says so (the rows the game's orient gives, the second
        // and third swapped, the new third turned round), then about its up by 0-359 degrees.
        GrowthRows rows = *kIdentityRows;
        if (model.flags & 1) {
            GrowthRows slope = *kIdentityRows;
            Orient(&slope, nullptr, &normal, kRightVector);
            rows.row[0] = slope.row[0];
            rows.row[1] = slope.row[2];
            rows.row[2] = {-slope.row[1].x, -slope.row[1].y, -slope.row[1].z};
        }
        Turn(&rows, nullptr, static_cast<float>((GrowthRand() % 360) / 180.0 * 3.1415927410125732));
        // Its size and tint.
        const float scale = static_cast<float>(1.0 - (GrowthChance(500.0) - 1.0) * (model.scaleNoise / 255.0));
        const float mix = static_cast<float>(GrowthChance());
        float tint[4];
        for (int c = 0; c < 4; ++c) {
            static const int kByte[4] = {2, 1, 0, 3};  // red, green, blue, alpha from the blue, green, red, alpha bytes
            const float a = model.colourA[kByte[c]] / 255.0f, b = model.colourB[kByte[c]] / 255.0f;
            tint[c] = (b - a) * mix + a;
        }
        for (GrowthVector& row : rows.row) row = {row.x * scale, row.y * scale, row.z * scale};
        // Kept only where the layer grows: the cell's 1 m square there (8 across).
        const int ix = static_cast<int>(floorf(static_cast<float>((at.x - low[0]) / cellSize * 8.0)));
        const int iz = static_cast<int>(floorf(static_cast<float>((at.z - low[1]) / cellSize * 8.0)));
        const int square = ix + iz * 8;
        if (square < 0 || square >= 64 || !(record[2 + square / 8] & (1 << (square % 8)))) continue;
        const float sway = static_cast<float>(GrowthChance() * 7.0);
        // The model's active cell, made the first time, room allowing.
        if (!cells[m]) {
            if (!CellPoolHasRoom(kCellPool, nullptr) || !MempoolHasRoom(kGrowthMempool, nullptr, count * 32, 0)) continue;
            GrowthCell* cell = NewCell(kCellList, nullptr, 0);
            if (!cell) continue;
            cells[m] = cell;
            cell->model = m;
            cell->count = 0;
            cell->elements = MempoolTake(kGrowthMempool, nullptr, count * 32, 0, 0, 0);
            cell->unused0 = 0;
            cell->middle = *middle;
        }
        GrowthCell* cell = cells[m];
        uint8_t* slot = cell->elements + cell->count * 32;
        cell->count++;
        alignas(16) GrowthElement element{};
        ElementInit(&element, nullptr);
        element.at = offset;
        element.rows = rows;
        for (int c = 0; c < 4; ++c) element.tint[c] = tint[c];
        element.sway = sway;
        ElementPut(slot, nullptr, &element);
        if (kept++ == 0) { firstModel = m; firstAt = at; firstHeight = height; }
    }
    g_growthKept += kept;
    g_growthTried += count;
    g_growthNoGround += withoutGround;
    g_growthUnderRock += underRock;
    if (++g_growthCalls % 500 == 0)
        rfa::LogDetail("Undergrowth scatter: %d cells scattered so far, %lld of %lld pieces kept (%lld where no ground was loaded yet; %lld under a rock, "
                 "put on the ground below it)", g_growthCalls, g_growthKept, g_growthTried, g_growthNoGround, g_growthUnderRock);
    if (logThis) {
        ++g_growthCallsLogged;
        const GrowthModel* model = firstModel >= 0 ? &(*layer)->model[firstModel] : nullptr;
        rfa::LogDetail("Undergrowth scatter: %d of %d pieces kept in the cell at (%.1f, %.1f, %.1f) (%d where no ground is loaded yet, %d under a rock), cell size %.3f; "
                 "first %s at (%.1f, %.1f, %.1f), ground %.2f, its mesh %s",
                 kept, count, middle->x, middle->y, middle->z, withoutGround, underRock, cellSize, model && model->name ? model->name : "-", firstAt.x, firstAt.y, firstAt.z,
                 firstHeight, model && model->name ? (FindFoliageMesh(model->name) ? "found" : "NOT found") : "-");
    }
}

// ---- Guerrilla's broken glass (bullet holes and cracks on its panes, its shatter)

// A bullet's mark (object_decal_create_bullet_impact_decal, 0x4F23B0, cdecl): the hit object, the
// shooter, the bullet's kind (its first byte), the weapon's decal size, where and on what surface
// facing (normal) it hit, the hit's damage, and the shot (+0x40 the hit piece, +0x44 its part, +0x69
// the effect material, 0xFF for the object's own). Armageddon's makes no mark at all on glass (it
// returns at once for the effect material Glass); Guerrilla's makes the bullet's hole on both faces
// of the pane and, mostly, a crack effect and two or three cracks running out from it, on both faces
// too, all with the decals and settings Armageddon still loads (Glass_radial_decal_prob,
// Glass_min/max_radial_decals; decal_bullet_glass_crack_1-3; des_glass_crack_md). So in our worlds
// glass gets Guerrilla's (GuerrillaGlassDecals), on the game's own helpers; every other mark is the
// game's.
constexpr DWORD kBulletDecal = 0x4F23B0;
constexpr size_t kBulletDecalPrologue = 9;  // push ebp / mov ebp,esp / sub esp,0DCh

struct GameVector { float x, y, z; };

using BulletDecalFn = void(__cdecl*)(void* hit, void* shooter, const uint8_t* bulletKind, float size, const GameVector* at,
                                     const GameVector* normal, float damage, const uint8_t* shot);
BulletDecalFn g_bulletDecal;

constexpr size_t kShotPiece = 0x40, kShotPart = 0x44, kShotMaterial = 0x69;
constexpr uint8_t kOwnMaterial = 0xFF;
constexpr size_t kMaterialAtSlot = 0x68 / 4;  // the object's method: its effect material at a piece and place

// A decal's settings (rl_decal_parameters: the game's own make up to 0xA8 bytes): where (+0x04), its
// rows (right +0x10, up +0x1C, forward +0x28, the way it faces), whether it is kept (+0x34; bullet
// marks never are), its width and length (+0x84, +0x88).
constexpr size_t kDecalSize = 0xC0;
constexpr size_t kDecalAt = 0x04, kDecalRows = 0x10, kDecalRight = 0x10, kDecalForward = 0x28, kDecalKept = 0x34, kDecalWidth = 0x84,
                 kDecalLength = 0x88;

// The game's switches and settings for marks (its tweak table's): decals on (R_enable_decals), how far
// from the camera a bullet's mark is made (the player's: Bullet_hole_decal_max_dist_player, 50 m; anyone
// else's: Bullet_hole_decal_max_dist, 15 m), how much a hole's size varies (Bullet_hole_decal_size_variation).
const auto& kDecalsOn = *reinterpret_cast<const bool*>(0x1100890);
const auto& kPlayerMarkReach = *reinterpret_cast<const float*>(0x124F95C);
const auto& kOtherMarkReach = *reinterpret_cast<const float*>(0x124F960);
const auto& kHoleSizeVariation = *reinterpret_cast<const float*>(0x124F958);
// What the game's glass decal set-up (0x4F37A0) finds: the effect material Glass, the three crack
// decals, the crack effect; and the crack settings (Glass_radial_decal_prob, out of 100, and
// Glass_min/max_radial_decals).
const auto& kGlassMaterial = *reinterpret_cast<const uint8_t*>(0x124F974);
const auto& kCrackDecals = *reinterpret_cast<const int(*)[3]>(0x124F968);
const auto& kCrackEffect = *reinterpret_cast<const uint32_t*>(0x10F7C84);
const auto& kCrackChance = *reinterpret_cast<const int*>(0x10F7C90);
const auto& kFewestCracks = *reinterpret_cast<const int*>(0x10F7C88);
const auto& kMostCracks = *reinterpret_cast<const int*>(0x10F7C8C);
constexpr uint32_t kNoEffect = 0xFFFFFFFF;
constexpr float kFullTurn = 6.2831855f;

// What the hit is: its type (+0x7A; 2 a building or other mover, 3 a vehicle, as Guerrilla's
// object_type), and for a mover (rfg_mover), its flags (+0x150; 8 invulnerable, as the console's
// building_invulnerability clears) and its pieces: a piece (40 bytes) +0x23 how many links it has
// left; a link, how much damage breaks it (rfg_link yield_limit).
constexpr size_t kObjectType = 0x7A;
constexpr int8_t kVehicleType = 3;
constexpr size_t kMoverFlags = 0x150;
constexpr uint32_t kMoverInvulnerable = 8;
constexpr size_t kPieceLinkCount = 0x23;

using MaterialAtFn = uint8_t(__fastcall*)(void* object, void* unused, int piece, const GameVector* at, int zero);
using EffectMaterialFn = void*(__cdecl*)(uint32_t material);                                       // 0x683AC0
using BulletDecalOfFn = int(__fastcall*)(void* effectMaterial, void* unused, uint32_t bulletKind);  // 0x6842E0, -1 for none
using DecalInitFn = void(__fastcall*)(uint8_t* decal, void* unused);                               // 0x4F2210
using DecalInfoFn = void(__cdecl*)(uint8_t* decal, int info);                                      // 0x9721C0 (nothing for -1)
using FaceAlongFn = void(__fastcall*)(float* rows, void* unused, const GameVector* forward);        // 0xC9DA10, matrix::make
using TurnAboutForwardFn = void(__fastcall*)(float* rows, void* unused, float angle);              // 0xC9E800
using RandomBetweenFn = float(__cdecl*)(float low, float high);                                    // 0xC8BC00
using RandomWholeFn = int(__cdecl*)(int low, int high);                                            // 0xC8BBE0, both ends included
using ChanceFn = int(__cdecl*)(int percent);                                                       // 0x42D7B0
using ApplyDecalFn = uint32_t(__cdecl*)(void* object, const uint8_t* decal, float zero, int piece, int part, int none);  // 0x4F0F80
using IsPlayerFn = bool(__cdecl*)(void* object);                                                   // 0x7B2660
using MoverOfFn = uint8_t*(__fastcall*)(void* world, void* unused, void* object);                  // 0x4243C0, null if not one
using ShapeOfFn = uint8_t*(__fastcall*)(uint8_t* mover, void* unused);                             // 0x42A1F0
using IsShatterableFn = bool(__fastcall*)(uint8_t* mover, void* unused, int piece);                // 0x68ADB0
using PieceOfFn = uint8_t*(__fastcall*)(uint8_t* shape, void* unused, int piece);                  // 0x4A3400
using LinkOfFn = const int*(__fastcall*)(uint8_t* shape, void* unused, const uint8_t* piece, int link);  // 0x663620
using PlayEffectAtFn = uint32_t(__cdecl*)(uint32_t effect, const GameVector* at, const float* rows, int a, int b, int c, int d, int e,
                                          int f, int g);  // effects_play, 0x4EB460

const auto EffectMaterial = reinterpret_cast<EffectMaterialFn>(0x683AC0);
const auto BulletDecalOf = reinterpret_cast<BulletDecalOfFn>(0x6842E0);
const auto DecalInit = reinterpret_cast<DecalInitFn>(0x4F2210);
const auto DecalInfo = reinterpret_cast<DecalInfoFn>(0x9721C0);
const auto FaceAlong = reinterpret_cast<FaceAlongFn>(0xC9DA10);
const auto TurnAboutForward = reinterpret_cast<TurnAboutForwardFn>(0xC9E800);
const auto RandomBetween = reinterpret_cast<RandomBetweenFn>(0xC8BC00);
const auto RandomWhole = reinterpret_cast<RandomWholeFn>(0xC8BBE0);
const auto Chance = reinterpret_cast<ChanceFn>(0x42D7B0);
const auto ApplyDecal = reinterpret_cast<ApplyDecalFn>(0x4F0F80);
const auto IsPlayer = reinterpret_cast<IsPlayerFn>(0x7B2660);
const auto MoverOf = reinterpret_cast<MoverOfFn>(0x4243C0);
const auto ShapeOf = reinterpret_cast<ShapeOfFn>(0x42A1F0);
const auto IsShatterable = reinterpret_cast<IsShatterableFn>(0x68ADB0);
const auto PieceOf = reinterpret_cast<PieceOfFn>(0x4A3400);
const auto LinkOf = reinterpret_cast<LinkOfFn>(0x663620);
const auto PlayEffectAt = reinterpret_cast<PlayEffectAtFn>(0x4EB460);

int g_glassMarksLogged = 0;
constexpr int kGlassMarksLogged = 12;

// Says (for the first few glass hits) what became of one: its marks, or why it got none or fewer.
void GlassLog(const char* what, int piece, float damage) {
    if (g_glassMarksLogged >= kGlassMarksLogged) return;
    ++g_glassMarksLogged;
    rfa::LogDetail("Glass: a shot on piece %d (damage %.0f): %s", piece, damage, what);
}

float& DecalFloat(uint8_t* decal, size_t at) { return *reinterpret_cast<float*>(decal + at); }

// Puts a decal on the hit piece of the object (as the game puts every mark: only on that piece).
void PutDecal(void* hit, const uint8_t* decal, int piece, int part) { ApplyDecal(hit, decal, 0.0f, piece, part, -1); }

// Turns a decal round to face the other way (its forward row turned round), for the pane's far face.
void TurnRound(uint8_t* decal) {
    for (size_t c = 0; c < 3; ++c) DecalFloat(decal, kDecalForward + c * 4) = -DecalFloat(decal, kDecalForward + c * 4);
}

// Guerrilla's marks on glass (its object_decal_create_bullet_impact_decal's glass part), in its order.
void GuerrillaGlassMarks(void* hit, void* shooter, uint8_t bulletKind, float size, const GameVector& at, const GameVector& normal, float damage,
                         int piece, int part, uint8_t material) {
    if (!kDecalsOn) return GlassLog("none, decals are off", piece, damage);
    // Near enough to the camera: anyone's shots, the player's farther off (Armageddon makes only the
    // player's marks; Guerrilla, everyone's).
    const float reach = IsPlayer(shooter) ? kPlayerMarkReach : kOtherMarkReach;
    const float dx = g_cameraPosition[0] - at.x, dy = g_cameraPosition[1] - at.y, dz = g_cameraPosition[2] - at.z;
    if (dx * dx + dy * dy + dz * dz > reach * reach) return GlassLog("none, too far from the camera", piece, damage);
    // A pane that shatters is gone at once (its piece deleted), so nothing is put on it. (Guerrilla puts
    // the front hole there and no more, unseen; leaving it out spares the game a decal on a piece it is
    // deleting.)
    uint8_t* mover = MoverOf(kWorld, nullptr, hit);
    if (mover && piece >= 0 && IsShatterable(mover, nullptr, piece)) return GlassLog("none, the pane shatters", piece, damage);
    // The hole, as the game makes every bullet's: its effect material's decal for this kind of bullet,
    // at the hit, facing along the surface's normal, turned at random, sized by the weapon and varied.
    void* effectMaterial = EffectMaterial(material);
    if (!effectMaterial) return GlassLog("none, no effect material", piece, damage);
    const int hole = BulletDecalOf(effectMaterial, nullptr, bulletKind);
    if (hole == -1) return GlassLog("none, no hole decal for this bullet", piece, damage);
    alignas(16) uint8_t decal[kDecalSize] = {};
    DecalInit(decal, nullptr);
    DecalInfo(decal, hole);
    decal[kDecalKept] = 0;
    *reinterpret_cast<GameVector*>(decal + kDecalAt) = at;
    float* rows = reinterpret_cast<float*>(decal + kDecalRows);
    FaceAlong(rows, nullptr, &normal);
    TurnAboutForward(rows, nullptr, RandomBetween(0.0f, kFullTurn));
    const float scale = size * RandomBetween(1.0f - kHoleSizeVariation, 1.0f + kHoleSizeVariation);
    DecalFloat(decal, kDecalWidth) *= scale;
    DecalFloat(decal, kDecalLength) *= scale;
    PutDecal(hit, decal, piece, part);
    // The rest only on a pane the hit leaves standing: not a vehicle's, not an invulnerable building's,
    // and not one whose links this hit's damage breaks.
    if (*(reinterpret_cast<const int8_t*>(hit) + kObjectType) == kVehicleType) return GlassLog("the hole, on a vehicle", piece, damage);
    if (mover) {
        if (*reinterpret_cast<const uint32_t*>(mover + kMoverFlags) & kMoverInvulnerable)
            return GlassLog("the hole, on an invulnerable building", piece, damage);
        uint8_t* shape = piece >= 0 ? ShapeOf(mover, nullptr) : nullptr;
        if (const uint8_t* own = shape ? PieceOf(shape, nullptr, piece) : nullptr)
            for (int l = 0; l < own[kPieceLinkCount]; ++l)
                if (const int* breaksAt = LinkOf(shape, nullptr, own, l); breaksAt && *breaksAt <= static_cast<int>(damage)) {
                    char what[96];
                    sprintf_s(what, "the hole, as the hit breaks the pane's link %d (it holds to %d)", l, *breaksAt);
                    return GlassLog(what, piece, damage);
                }
    }
    // The hole on the far face.
    TurnRound(decal);
    PutDecal(hit, decal, piece, part);
    // Mostly, the crack effect, and two or three cracks running out from the hole in turn round it,
    // each from the hole outwards (its middle half its width from the hit), on both faces.
    if (!Chance(kCrackChance)) return GlassLog("the hole on both faces, no cracks this time", piece, damage);
    if (kCrackEffect != kNoEffect) {
        alignas(16) float facing[9] = {};
        FaceAlong(facing, nullptr, &normal);
        PlayEffectAt(kCrackEffect, &at, facing, 0, -1, -1, -1, 0, 1, 0);  // as the game plays its shatter's
    }
    const int cracks = RandomWhole(kFewestCracks, kMostCracks);
    if (cracks <= 0) return;
    const float slice = kFullTurn / static_cast<float>(cracks);
    for (int i = 0; i < cracks; ++i) {
        alignas(16) uint8_t crack[kDecalSize] = {};
        DecalInit(crack, nullptr);
        const int info = kCrackDecals[RandomWhole(0, 2)];
        if (info == -1) continue;
        DecalInfo(crack, info);
        crack[kDecalKept] = 0;
        float* crackRows = reinterpret_cast<float*>(crack + kDecalRows);
        FaceAlong(crackRows, nullptr, &normal);
        TurnAboutForward(crackRows, nullptr, RandomBetween(static_cast<float>(i) * slice, static_cast<float>(i + 1) * slice));
        const float half = DecalFloat(crack, kDecalWidth) * 0.5f;
        const float* right = reinterpret_cast<const float*>(crack + kDecalRight);
        *reinterpret_cast<GameVector*>(crack + kDecalAt) = {at.x - right[0] * half, at.y - right[1] * half, at.z - right[2] * half};
        PutDecal(hit, crack, piece, part);
        TurnRound(crack);
        PutDecal(hit, crack, piece, part);
    }
    char what[64];
    sprintf_s(what, "the hole and %d cracks, on both faces", cracks);
    GlassLog(what, piece, damage);
}

void __cdecl BulletDecal(void* hit, void* shooter, const uint8_t* bulletKind, float size, const GameVector* at, const GameVector* normal,
                         float damage, const uint8_t* shot) {
    if (hit && bulletKind && at && normal && InOurWorld()) {
        // The effect material hit, as the game finds it: the shot's, or the object's own there.
        const int piece = shot ? *reinterpret_cast<const int*>(shot + kShotPiece) : -1;
        const int part = shot ? *reinterpret_cast<const int*>(shot + kShotPart) : -1;
        uint8_t material = shot ? shot[kShotMaterial] : kOwnMaterial;
        if (material == kOwnMaterial) {
            const auto materialAt = reinterpret_cast<MaterialAtFn>((*reinterpret_cast<void***>(hit))[kMaterialAtSlot]);
            material = materialAt(hit, nullptr, piece, at, 0);
        }
        if (material == kGlassMaterial) {
            GuerrillaGlassMarks(hit, shooter, *bulletKind, size, *at, *normal, damage, piece, part, material);
            return;
        }
    }
    g_bulletDecal(hit, shooter, bulletKind, size, at, normal, damage, shot);
}

// Guerrilla's shatter: the same pattern of shards as Armageddon's (create_shatter_effect), but
// Armageddon leaves out the small shards round the hit (Glass_lod_enable: its inner bands, and half of
// the rest when the shard pool is half full; Guerrilla has no such setting) and throws and spins its
// shards far more gently. In our worlds the game's settings are Guerrilla's (Re-Mars-tered's values),
// and Armageddon's are put back elsewhere. They are read as each pane breaks and its shards move.
auto& g_glassLod = *reinterpret_cast<uint8_t*>(0x10FF422);
struct GlassSetting {
    const char* name;
    DWORD address;
    float guerrilla;
};
constexpr GlassSetting kGlassSettings[] = {
    {"Glass_impact_force_max", 0x10FF400, 100.0f},  // Armageddon's 15
    {"Glass_rotation_min", 0x10FF404, 1.0f},        // 3
    {"Glass_rotation_max", 0x10FF408, 100.0f},      // 10
    {"Glass_rotational_damping", 0x10FF414, 1.0f},  // 3
    {"Glass_rotation_scale", 0x10FF418, 100.0f},    // 10
};
float g_glassSettingsBefore[std::size(kGlassSettings)];
uint8_t g_glassLodBefore;
bool g_glassOurs;

void GlassUpdate() {
    const bool ours = InOurWorld();
    if (ours == g_glassOurs) return;
    g_glassOurs = ours;
    for (size_t i = 0; i < std::size(kGlassSettings); ++i) {
        float& setting = *reinterpret_cast<float*>(kGlassSettings[i].address);
        if (ours) {
            g_glassSettingsBefore[i] = setting;
            setting = kGlassSettings[i].guerrilla;
        } else {
            setting = g_glassSettingsBefore[i];
        }
    }
    if (ours) {
        g_glassLodBefore = g_glassLod;
        g_glassLod = 0;
    } else {
        g_glassLod = g_glassLodBefore;
    }
    rfa::Log("Glass: %s shatter (impact force %.0f, rotation %.0f-%.0f, damping %.0f, scale %.0f, fewer shards %s)", ours ? "Guerrilla's" : "the game's own",
             *reinterpret_cast<const float*>(kGlassSettings[0].address), *reinterpret_cast<const float*>(kGlassSettings[1].address),
             *reinterpret_cast<const float*>(kGlassSettings[2].address), *reinterpret_cast<const float*>(kGlassSettings[3].address),
             *reinterpret_cast<const float*>(kGlassSettings[4].address), g_glassLod ? "on" : "off");
}

// ---- Guerrilla's buildings: how they hold to the ground, their materials and load, and what breaks
//
// Armageddon's destruction is Guerrilla's code, function for function: a building's pieces are joined by
// links, the pieces standing on the ground are anchored to it when the building loads, a piece cut off from
// every anchor falls, and a stress pass breaks links whose load (the weight on them times 9.8 times the
// stress control's Force_Scalar) is more than they hold. Three of its numbers are Armageddon's own:
//
// - How strongly a piece holds to the ground (rfg_destroyable_shape::create_world_anchor, 0x698F30, thiscall:
//   the shape; the mover, the piece's state; it stores the strength in the state's first word through
//   0x679E40, only for an anchored piece, the state's +0x18 bit 0). It is a damage buffer: a hit on an
//   anchored piece wears the anchor down before the piece's links take damage. Guerrilla's
//   (Re-Mars-tered 0x72FBF0) is the piece's footprint, its box's x by z (the piece's base: box from +0 to
//   +0x0C), times 256, truncated, times its material's link strength (record +0x40, LinkStrength × 1000)
//   shifted 8 left, the 64-bit product shifted 16 right; an indestructible piece (base +0x3F bit 0x20)
//   0x7FFFFFFF. Armageddon's takes the area as the piece's mass over its material's density times the box's
//   height, which comes to under 1/256 m² for every piece of both games: so every anchor's strength is
//   its floor of 1, and an anchored foundation or beam gives way at the first hit. With GuerrillaAnchors=1
//   the mod wraps the function and, after the game's, stores Guerrilla's strength (at least 1, the game's
//   own floor) for the same pieces the game set one for.
// - The materials (rfg_materials.xtbl; records of 0x5C bytes from the pointer at 0x155E10C, how many at
//   0x155E110, the name at +0, Density +0x2C, LinkStrength × 1000 +0x40, StressMultiplier +0x44). Both games'
//   tables are the same but for two: Steel-SuperStrong's links Armageddon made 5 times weaker (800, not
//   4000), with a StressMultiplier of 5 (a field Guerrilla hasn't: it multiplies a link group's yield in the
//   stress pass) to make up for it there; and Stone-Indestructo (LinkStrength 8388, Density 5000; Guerrilla's
//   2000 and 500).
// - The stress controls (rfg_stress_controls.xtbl; records of 0x34 bytes from 0x17EE5B0, how many at
//   0x17EE69C, the name at +0, Force_Scalar +0x2C): Default's Force_Scalar is 0.15 in Armageddon, 0.12 in
//   Guerrilla, so its buildings' weight is a quarter heavier.
// With GuerrillaMaterials=1 the mod puts Guerrilla's values in those records while one of our worlds is
// loaded, and the game's back when another is: each value is checked first, and one that isn't the game's is
// left alone. The game works a building's link strengths out as it loads (0x697120, thiscall: the shape),
// so the values are set there too, before the world's first building, as well as each frame.
//
// And a log (DestructionLog=1): every 10 s in our worlds, how many of each of the destruction events the
// game looked up an effect for (the material events: load_balance, stress, detach, groan, shatter,
// collapse, ...), how many pieces were anchored and with what strength, how many of the game's links
// between touching buildings are in use (rfg_mover_create_dynamic_link, 0x684A90: a pool of 0x28-byte
// links at 0x155E9A8, how many taken at 0x15685E8, the limit at 0x684CAE: 1,000; a building that touches
// another when the pool is full gets no link to it), and how long the frames and the game's physics took.
// The physics: the main loop times its Havok step (0x774110, "havok_simulate") in microseconds into
// 0x130860C (0x5417A9-0x5417BE). The game's debris balancer (rfgl, 0x67FDE0) reads it: its load is that
// time over 10 ms (Guerrilla's over 26.25 ms, Re-Mars-tered 0x715470) plus the debris made lately, and
// while the load is up it takes debris away, each with the load_balance event. So many load_balance
// events mean the physics step ran over 10 ms.
//
// The three switches are read again whenever rfa_mods.ini is saved (CheckSettings). The stress pass reads
// the materials and load as it goes, so those change at once; anchors and link strengths are worked out as
// buildings load, so `level` again loads every building with the new ones.
constexpr DWORD kCreateWorldAnchor = 0x698F30;
constexpr size_t kCreateWorldAnchorPrologue = 6;  // push ebp / mov ebp,esp / sub esp,30h
constexpr DWORD kResetLinkStrengths = 0x697120;
constexpr size_t kResetLinkStrengthsPrologue = 6;  // push ebp / mov ebp,esp / sub esp,14h
constexpr DWORD kPieceBase = 0x663570;             // thiscall (the shape; the piece's state): its base
constexpr DWORD kPieceOwnedBy = 0x665960;          // thiscall (the shape; the piece's state): whether it's the shape's
constexpr DWORD kMaterialRecord = 0x683A90;        // cdecl (its number): its record, the default's for none
using CreateWorldAnchorFn = void(__fastcall*)(void* shape, void* unused, void* mover, BYTE* state);
using ResetLinkStrengthsFn = void(__fastcall*)(void* shape, void* unused);
using PieceBaseFn = const BYTE*(__fastcall*)(void* shape, void* unused, BYTE* state);
using PieceOwnedByFn = bool(__fastcall*)(void* shape, void* unused, BYTE* state);
using MaterialRecordFn = const BYTE*(__cdecl*)(int number);
CreateWorldAnchorFn g_createWorldAnchor;
ResetLinkStrengthsFn g_resetLinkStrengths;
constexpr DWORD kMaterials = 0x155E10C, kMaterialCount = 0x155E110, kMaterialSize = 0x5C;
constexpr DWORD kStressControls = 0x17EE5B0, kStressControlCount = 0x17EE69C, kStressControlSize = 0x34;
constexpr DWORD kDynamicLinksTaken = 0x15685E8, kDynamicLinkLimitAt = 0x684CAE, kDynamicLinkCheckAt = 0x684CAA;

// A value of the game's that Guerrilla has otherwise: the record (by its name), the field, both games' values.
struct GuerrillaValue {
    bool stress;          // a stress control's, else a material's
    const char* record;
    const char* field;
    DWORD offset;
    bool whole;           // a whole number, else a float
    double game, guerrilla;
    bool set;             // put in by the mod (so put back on leaving)
};
GuerrillaValue g_guerrillaValues[] = {
    {false, "Steel-SuperStrong", "LinkStrength x 1000", 0x40, true, 800000, 4000000, false},
    {false, "Steel-SuperStrong", "StressMultiplier", 0x44, false, 5.0, 1.0, false},
    {false, "Stone-Indestructo", "LinkStrength x 1000", 0x40, true, 8388000, 2000000, false},
    {false, "Stone-Indestructo", "Density", 0x2C, false, 5000.0, 500.0, false},
    {true, "Default", "Force_Scalar", 0x2C, false, 0.15, 0.12, false},
};

bool g_anchorsSetting = true, g_materialsSetting = true, g_destructionLog = false;
void ReadPropLog();  // PropLog= (below, with the props' log)
bool g_buildingsWorld;     // our world, as last seen
bool g_anchorsOurs;        // Guerrilla's anchors now
bool g_materialsOurs;      // Guerrilla's values put in
CRITICAL_SECTION g_buildingsLock;
bool g_buildingsReady;

// What happened since the last line of the log.
LONG g_breakEvents[kEventKinds], g_otherBreakEvents;
LONG g_anchorsMade, g_anchorsNone, g_anchorsWrapped;
long long g_anchorLeast = -1, g_anchorMost;
int g_dynamicLinksMost;
DWORD g_lastBuildingsLog;

// The frames and the game's physics step, since the last line.
constexpr DWORD kPhysicsMicroseconds = 0x130860C;
constexpr double kSlowFrameMs = 1000.0 / 30, kPhysicsBudgetMs = 10.0;
LARGE_INTEGER g_frameAt;
int g_frames, g_slowFrames, g_overBudget;
double g_frameMs, g_frameWorst, g_physicsMs, g_physicsWorst;

// Whether the game's own window is the one in front (a frame with it behind isn't a hitch: the game pauses itself
// while it isn't in front, and its main loop waits).
bool GameInFront() {
    DWORD process = 0;
    const HWND front = GetForegroundWindow();
    return front && GetWindowThreadProcessId(front, &process) && process == GetCurrentProcessId();
}

// Long frames: what the main thread was doing (in our worlds, always: a stall is a problem to know of). A watchdog
// thread looks every 50 ms; all a frame does for it is MarkFrame (a tick and a count), and it does nothing more while
// frames are short. Once a frame (from one effects update to the next) has run 250 ms, then at 500 ms, 1 s and 2 s,
// it stops the main thread for an instant, copies its registers and its stack, and lets it go. When the frame ends it
// logs one line for it (from 1 s; shorter ones only with Log=1, which also takes the looks before 1 s): its length
// and where the main thread was at the last look, with the call chain read from
// its frame pointers (the game keeps them: push ebp / mov ebp,esp throughout); the game's own addresses as
// themselves, others as module+offset. Not before 15 s of play in our world (the world's load and its first seconds
// are long frames by nature), nor for a frame with the game's window behind another (the game pauses itself then):
// those are left out of the frame times too (the Destruction and Sections lines say how many).
volatile LONG g_frameNumber;            // frames timed (the watchdog's way of telling one frame from the next)
volatile LONG g_frameStartTick;         // GetTickCount at the frame's start
volatile LONG g_frameInBackground;      // this frame had the game's window behind another at some point
volatile LONG g_simFrameInBackground;   // the same for the sim frame (the Sections line's)
volatile LONG g_lastFrameEnded = -1;    // the number of the frame that last ended, and its length in ms
volatile LONG g_lastFrameMs;
volatile LONG g_playSinceTick;          // GetTickCount when frames began in our world (0 not yet)
volatile DWORD g_loopThread;            // the thread that runs the frames (the effects update's)
bool g_framesMarked;                    // MarkFrame has run since frames began in our world
int g_backgroundFrames;                 // frames left out of the frame times since the last line
HANDLE g_watchdog;

// A frame still running at the last look (5 s) is stuck: its line is written then, without waiting for the frame to
// end, so a freeze the game never comes back from still names where the main thread hangs. Windows stops treating a
// window that hasn't answered for 5 s as the one in front (it puts up its "not responding" copy), so after 3 s into a
// frame the window being behind no longer stops the looks.
constexpr size_t kStackCopy = 64 * 1024;  // at most this much of the main thread's stack copied at each look
constexpr DWORD kLookAtMs[] = {250, 500, 1000, 2000, 5000};
constexpr DWORD kStuckMs = 5000;
constexpr DWORD kFrontCheckedForMs = 3000;
constexpr DWORD kPlayBeforeLooking = 15000;
constexpr DWORD kStallMs = 1000;  // a frame this long is always logged; looks before it only with Log=1
constexpr int kChainLength = 20;

// Copies memory that may not be readable; false if it wasn't.
bool CopyIfReadable(void* to, const void* from, size_t size) {
    __try {
        memcpy(to, from, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// How much of a stopped thread's stack, from esp, can be copied (at most `most` bytes): the rest of the committed,
// readable stretch it is in, asked of Windows. Copying past the stack's end faulted, and the fault went through the
// loader's handler (which logs, and asks for the module) while the thread looked at was still stopped: if that thread
// held a lock the handler needed, both waited (the user's 18:53 run, 2026-11-15, a 5.6 s frame).
size_t StackBytes(DWORD esp, size_t most) {
    MEMORY_BASIC_INFORMATION info;
    const void* at = reinterpret_cast<const void*>(static_cast<uintptr_t>(esp));
    if (!VirtualQuery(at, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    const size_t left = static_cast<const BYTE*>(info.BaseAddress) + info.RegionSize - static_cast<const BYTE*>(at);
    return left < most ? left & ~static_cast<size_t>(3) : most;
}

// An address as the log shows it: the game's own as itself, else its module's name and offset (or the address).
void DescribeCodeAddress(DWORD address, char* out, size_t size) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(address)), &module) || !module ||
        module == GetModuleHandleW(nullptr)) {
        snprintf(out, size, "%08lX", address);
        return;
    }
    char path[MAX_PATH] = {};
    GetModuleFileNameA(module, path, MAX_PATH);
    const char* name = strrchr(path, '\\');
    snprintf(out, size, "%s+%lX", name ? name + 1 : path, address - static_cast<DWORD>(reinterpret_cast<uintptr_t>(module)));
}

// One look at the main thread: where it was and its call chain (return addresses, the nearest first).
struct ThreadLook {
    DWORD eip;
    DWORD chain[kChainLength];
    int chainLength;
};

// The call chain from a copy of the stack (starting at esp): each frame's saved ebp and return address, followed
// while they stay inside the copy and go up the stack.
int FollowFrames(const DWORD* stack, DWORD esp, size_t copied, DWORD ebp, DWORD* chain) {
    int length = 0;
    for (DWORD frame = ebp; length < kChainLength;) {
        if (frame < esp || frame - esp + 8 > copied || (frame - esp) % 4) break;
        const DWORD* at = stack + (frame - esp) / 4;
        chain[length++] = at[1];
        if (at[0] <= frame) break;
        frame = at[0];
    }
    return length;
}

// A look as the log shows it: where the main thread was, and its call chain.
std::string DescribeLook(const ThreadLook& look) {
    char part[160];
    DescribeCodeAddress(look.eip, part, sizeof(part));
    std::string text = std::string(part) + ", called from ";
    for (int i = 0; i < look.chainLength; ++i) {
        DescribeCodeAddress(look.chain[i], part, sizeof(part));
        text += i ? ", " : "";
        text += part;
    }
    if (!look.chainLength) text += "(no frames read)";
    return text;
}

// A stuck frame whose main thread is waiting (in ntdll or KERNELBASE: a wait, a sleep) shows what it waits for only
// through the others: one line for each of the game's other threads, with where it started (its start address names
// its kind: the job threads, streaming, audio, render) and where it is now. Each is stopped only while its registers
// and stack are copied, as the main thread's; the list of threads is taken before any is stopped, and nothing is
// looked up or logged until it runs again. Not while a level loads (the loading state sleeps by design: the "level
// loaded" byte 0x133183B is 0). At most kThreadDumps stuck frames a run.
constexpr DWORD kLevelLoaded = 0x133183B;
constexpr int kThreadDumps = 3;
constexpr int kMostThreads = 128;
using NtQueryInformationThreadFn = LONG(WINAPI*)(HANDLE thread, int what, void* out, ULONG size, ULONG* written);

bool InWait(DWORD eip) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(eip)), &module) || !module)
        return false;
    return module == GetModuleHandleW(L"ntdll.dll") || module == GetModuleHandleW(L"KERNELBASE.dll");
}

void LogOtherThreads(DWORD mainThread, DWORD* stack, size_t stackSize) {
    DWORD ids[kMostThreads];
    int count = 0;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    const DWORD process = GetCurrentProcessId(), self = GetCurrentThreadId();
    for (BOOL more = Thread32First(snapshot, &entry); more && count < kMostThreads; more = Thread32Next(snapshot, &entry))
        if (entry.th32OwnerProcessID == process && entry.th32ThreadID != mainThread && entry.th32ThreadID != self)
            ids[count++] = entry.th32ThreadID;
    CloseHandle(snapshot);
    const auto query = reinterpret_cast<NtQueryInformationThreadFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
    rfa::Log("Stuck frame: the main thread waits; the game's other %d threads:", count);
    for (int i = 0; i < count; ++i) {
        const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, ids[i]);
        if (!thread) continue;
        DWORD start = 0;
        if (query) query(thread, 9, &start, sizeof(start), nullptr);  // ThreadQuerySetWin32StartAddress
        CONTEXT context = {};
        context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        size_t copied = 0;
        bool got = false;
        if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
            got = GetThreadContext(thread, &context) != 0;
            if (got) {
                copied = StackBytes(context.Esp, stackSize);
                if (!CopyIfReadable(stack, reinterpret_cast<const void*>(static_cast<uintptr_t>(context.Esp)), copied)) copied = 0;
            }
            ResumeThread(thread);
        }
        CloseHandle(thread);
        if (!got) continue;
        ThreadLook look = {};
        look.eip = context.Eip;
        look.chainLength = copied >= 256 ? FollowFrames(stack, context.Esp, copied, context.Ebp, look.chain) : 0;
        char started[160];
        DescribeCodeAddress(start, started, sizeof(started));
        rfa::Log("Stuck frame: thread %lu (started at %s) is at %s", ids[i], started, DescribeLook(look).c_str());
    }
}

DWORD WINAPI Watchdog(void*) {
    HANDLE thread = nullptr;
    DWORD threadId = 0;
    LONG watchedFrame = -1;
    int looks = 0;
    bool behind = false, haveLook = false;
    ThreadLook last = {};
    static DWORD stack[kStackCopy / 4];
    for (;;) {
        Sleep(50);
        const LONG frame = g_frameNumber;
        if (frame != watchedFrame) {
            // The frame watched has ended: one line for it, if it was looked at.
            if (haveLook && !behind && g_lastFrameEnded == watchedFrame) {
                if (g_lastFrameMs >= static_cast<LONG>(kStallMs))
                    RFA_LOG_FIRST(50, "Long frame: %ld ms; at the last look (%lu ms in) the main thread was at %s", g_lastFrameMs,
                                  kLookAtMs[looks - 1], DescribeLook(last).c_str());
                else
                    RFA_LOG_DETAIL_FIRST(50, "Long frame: %ld ms; at the last look (%lu ms in) the main thread was at %s", g_lastFrameMs,
                                         kLookAtMs[looks - 1], DescribeLook(last).c_str());
            }
            watchedFrame = frame;
            looks = 0;
            behind = haveLook = false;
        }
        if (!g_buildingsWorld || !g_loopThread || !g_playSinceTick) continue;
        const DWORD now = GetTickCount();
        if (now - static_cast<DWORD>(g_playSinceTick) < kPlayBeforeLooking) continue;
        const LONG elapsed = static_cast<LONG>(now - static_cast<DWORD>(g_frameStartTick));
        if (behind) continue;
        const bool lookDue = looks < static_cast<int>(sizeof(kLookAtMs) / sizeof(kLookAtMs[0])) && elapsed >= static_cast<LONG>(kLookAtMs[looks]);
        // Whether the game's window is behind another: asked only when a look is due, or each time round while the
        // frame times are kept (the Destruction line's), so a short frame costs nothing here either.
        if ((lookDue || g_destructionLog) && elapsed < static_cast<LONG>(kFrontCheckedForMs) && !GameInFront()) {
            behind = true;
            InterlockedExchange(&g_frameInBackground, 1);
            InterlockedExchange(&g_simFrameInBackground, 1);
            continue;
        }
        if (!lookDue) continue;
        if (kLookAtMs[looks] < kStallMs && !rfa::DetailLog()) {  // under a second: only for the log (Log=1)
            ++looks;
            continue;
        }
        if (threadId != g_loopThread) {
            if (thread) CloseHandle(thread);
            threadId = g_loopThread;
            thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, threadId);
        }
        if (!thread) continue;
        // Stopped only while its registers and stack are copied: nothing here takes a lock the game could hold.
        CONTEXT context = {};
        context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        size_t copied = 0;
        if (SuspendThread(thread) == static_cast<DWORD>(-1)) continue;
        const bool gotContext = GetThreadContext(thread, &context) != 0 && g_frameNumber == frame;
        if (gotContext) {
            copied = StackBytes(context.Esp, sizeof(stack));
            if (!CopyIfReadable(stack, reinterpret_cast<const void*>(static_cast<uintptr_t>(context.Esp)), copied)) copied = 0;
        }
        ResumeThread(thread);
        ++looks;
        if (!gotContext) continue;
        last.eip = context.Eip;
        last.chainLength = copied >= 256 ? FollowFrames(stack, context.Esp, copied, context.Ebp, last.chain) : 0;
        haveLook = true;
        if (kLookAtMs[looks - 1] == kStuckMs) {
            static int stuckLines, threadDumps;
            if (stuckLines >= 10) continue;
            ++stuckLines;
            const bool loading = *reinterpret_cast<const BYTE*>(kLevelLoaded) == 0;
            rfa::Log("Stuck frame: still running after %ld ms (not ended yet%s); the main thread is at %s", elapsed,
                     loading ? ", a level loading" : "", DescribeLook(last).c_str());
            if (!loading && threadDumps < kThreadDumps && InWait(last.eip)) {
                ++threadDumps;
                LogOtherThreads(threadId, stack, sizeof(stack));
            }
        }
    }
}

// Each frame in our worlds (from BuildingsUpdate), always: all the long frames' watchdog needs, a tick and a count.
void MarkFrame() {
    const DWORD tick = GetTickCount();
    g_loopThread = GetCurrentThreadId();
    if (!g_framesMarked) {
        g_framesMarked = true;
        InterlockedExchange(&g_playSinceTick, static_cast<LONG>(tick | 1));  // frames begin in our world
    } else {
        InterlockedExchange(&g_lastFrameMs, static_cast<LONG>(tick - static_cast<DWORD>(g_frameStartTick)));
    }
    InterlockedExchange(&g_lastFrameEnded, g_frameNumber);
    InterlockedExchange(&g_frameStartTick, static_cast<LONG>(tick));
    InterlockedIncrement(&g_frameNumber);
}

// Each frame (from BuildingsUpdate) with DestructionLog=1: this frame's time and the game's last physics step's.
void TimeFrame() {
    LARGE_INTEGER now, rate;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&rate);
    const bool front = GameInFront();
    const bool background = InterlockedExchange(&g_frameInBackground, front ? 0 : 1) != 0 || !front;
    if (g_frameAt.QuadPart != 0 && background) {
        ++g_backgroundFrames;  // the game behind another window: not a frame of play
    } else if (g_frameAt.QuadPart != 0) {
        const double ms = static_cast<double>(now.QuadPart - g_frameAt.QuadPart) * 1000.0 / static_cast<double>(rate.QuadPart);
        // A second or more is a load or a pause, not a frame.
        if (ms < 1000.0) {
            ++g_frames;
            g_frameMs += ms;
            if (ms > g_frameWorst) g_frameWorst = ms;
            if (ms > kSlowFrameMs) ++g_slowFrames;
            const int physics = *reinterpret_cast<const int*>(kPhysicsMicroseconds);
            const double step = physics > 0 && physics < 10000000 ? physics / 1000.0 : 0.0;
            g_physicsMs += step;
            if (step > g_physicsWorst) g_physicsWorst = step;
            if (step > kPhysicsBudgetMs) ++g_overBudget;
        }
    }
    g_frameAt = now;
}

// The game's own sections, timed (DestructionLog=1; a diagnostic, for the frames lost while buildings come down).
// The game names the parts of its frame itself, for its profiler: a section begins with 0xCA1AE0 (thiscall: the
// section, an object on the caller's stack; its name, then three more; ret 0x10) and ends with 0xCA1DF0
// (thiscall: the section). The main loop's outermost is "sim frame" (0x540F66, the name at 0xF8AAA4); inside it
// come "ai_system_do_frame", "rfg_do_frame_pre" (the stress pass), "gameplay_do_frame", "events & holes",
// "camera doframe" and the rest, and inside those the destruction code's own ("rfg_mover::create_subpieces",
// "rfg_mover::update_render_subpieces", ...). The game's profiler records nothing unless it is switched on
// (0xCA1AE0 returns at once otherwise), so the mod times them itself, on the thread that runs "sim frame": each
// section's time with what it calls, and its own (less the sections inside it). Every 10 s in our worlds in
// which a sim frame took longer than 1/30 s, a line gives the sim frame's times and the ten sections that took
// most time of their own.
constexpr DWORD kSectionBegin = 0xCA1AE0, kSectionEnd = 0xCA1DF0;
constexpr size_t kSectionBeginPrologue = 6, kSectionEndPrologue = 6;  // push ebp / mov ebp,esp / sub esp,30h (28h)
constexpr DWORD kSimFrameName = 0xF8AAA4;                             // "sim frame"
using SectionBeginFn = void(__fastcall*)(void* section, void* unused, const char* name, int a, int b, int c);
using SectionEndFn = void(__fastcall*)(void* section, void* unused);
SectionBeginFn g_sectionBegin;
SectionEndFn g_sectionEnd;

struct SectionTimes {
    const char* key;          // the game's name for it (the pointer it passes)
    char name[48];
    long long own, all;       // in performance-counter ticks, this window
    long long thisFrame, mostInAFrame;
    long long ownThisFrame;   // this frame's own time (taken back out if the frame turns out to have been in the background)
    int calls, callsThisFrame;
};
constexpr int kSectionSlots = 2048;  // a power of 2
constexpr int kMostOpenSections = 96;
SectionTimes g_sectionTimes[kSectionSlots];
int g_sectionsKept, g_sectionsDropped;
struct OpenSection {
    void* section;
    SectionTimes* times;
    long long start, inside;  // when it began; the time of the sections in it so far
};
OpenSection g_openSections[kMostOpenSections];
int g_openCount;
DWORD g_mainThread;
volatile bool g_sectionsWanted;      // set each frame by BuildingsUpdate: in our worlds, with DestructionLog=1
bool g_sectionsOn;                   // what that was at the start of this sim frame
int g_simFrames, g_slowSimFrames, g_backgroundSimFrames;
bool g_simFrameSlow;                 // the sim frame just ended was slow
long long g_sectionCalls, g_tickRate;
DWORD g_lastSectionsLog;

SectionTimes* SectionTimesFor(const char* key) {
    size_t at = (reinterpret_cast<uintptr_t>(key) >> 2) & (kSectionSlots - 1);
    for (int probe = 0; probe < kSectionSlots; ++probe, at = (at + 1) & (kSectionSlots - 1)) {
        SectionTimes& s = g_sectionTimes[at];
        if (s.key == key) return &s;
        if (s.key) continue;
        if (g_sectionsKept >= kSectionSlots * 3 / 4) {
            ++g_sectionsDropped;
            return nullptr;
        }
        s.key = key;
        int i = 0;
        for (; i < static_cast<int>(sizeof(s.name)) - 1 && key[i]; ++i) s.name[i] = key[i];
        s.name[i] = 0;
        ++g_sectionsKept;
        return &s;
    }
    return nullptr;
}

double TicksMs(long long ticks) { return static_cast<double>(ticks) * 1000.0 / static_cast<double>(g_tickRate); }

void ClearSectionTimes() {
    memset(g_sectionTimes, 0, sizeof(g_sectionTimes));
    g_sectionsKept = g_sectionsDropped = 0;
    g_simFrames = 1;  // the frame begun now
    g_slowSimFrames = g_backgroundSimFrames = 0;
    g_sectionCalls = 0;
    g_lastSectionsLog = GetTickCount();
}

// At each sim frame's start, on the main thread: the last frame's times rolled up, and every 10 s a line if a sim
// frame was slow.
void SectionsFrame() {
    // A frame with the game's window behind another (the game pauses itself and its loop waits) isn't a frame of
    // play: its times are taken back out and it is counted apart.
    const bool front = GameInFront();
    const bool background = InterlockedExchange(&g_simFrameInBackground, front ? 0 : 1) != 0 || !front;
    if (background) {
        ++g_backgroundSimFrames;
        if (g_simFrameSlow) --g_slowSimFrames;
    } else {
        ++g_simFrames;
    }
    g_simFrameSlow = false;
    for (SectionTimes& s : g_sectionTimes)
        if (s.key && s.thisFrame) {
            if (background) {
                s.all -= s.thisFrame;
                s.own -= s.ownThisFrame;
                s.calls -= s.callsThisFrame;
            } else if (s.thisFrame > s.mostInAFrame) {
                s.mostInAFrame = s.thisFrame;
            }
            s.thisFrame = s.ownThisFrame = 0;
            s.callsThisFrame = 0;
        }
    const DWORD now = GetTickCount();
    if (now - g_lastSectionsLog < 10000) return;
    g_lastSectionsLog = now;
    if (g_slowSimFrames > 0 && g_simFrames > 1) {
        const double frames = g_simFrames - 1;  // the frame begun now isn't in
        const SectionTimes* sim = nullptr;
        std::vector<const SectionTimes*> ranked;
        for (const SectionTimes& s : g_sectionTimes)
            if (s.key && s.calls > 0) {
                ranked.push_back(&s);
                if (s.key == reinterpret_cast<const char*>(kSimFrameName)) sim = &s;
            }
        const size_t shown = std::min<size_t>(ranked.size(), 10);
        std::partial_sort(ranked.begin(), ranked.begin() + shown, ranked.end(),
                          [](const SectionTimes* a, const SectionTimes* b) { return a->own > b->own; });
        std::string line = "Sections (10 s, the game's own, on its main thread): ";
        char part[256];
        if (sim) {
            snprintf(part, sizeof(part), "%d sim frames, %.1f ms on average (the slowest %.1f ms, %d slower than 30 a second); the most time of their own: ",
                     g_simFrames - 1, TicksMs(sim->all) / frames, TicksMs(sim->mostInAFrame), g_slowSimFrames);
            line += part;
        }
        for (size_t i = 0; i < shown; ++i) {
            const SectionTimes& s = *ranked[i];
            snprintf(part, sizeof(part), "%s%s %.2f ms a frame (with what it calls %.2f, the most in a frame %.1f; %.1f times a frame)",
                     i ? ", " : "", s.name, TicksMs(s.own) / frames, TicksMs(s.all) / frames, TicksMs(s.mostInAFrame), s.calls / frames);
            line += part;
        }
        snprintf(part, sizeof(part), "; %.0f sections timed a frame, %d names", static_cast<double>(g_sectionCalls) / frames, g_sectionsKept);
        line += part;
        if (g_backgroundSimFrames > 0) {
            snprintf(part, sizeof(part), "; %d frames with the game's window behind another left out", g_backgroundSimFrames);
            line += part;
        }
        if (g_sectionsDropped > 0) {
            snprintf(part, sizeof(part), " (%d more past the table's room, not timed)", g_sectionsDropped);
            line += part;
        }
        rfa::LogDetail("%s", line.c_str());
    }
    ClearSectionTimes();
}

void __fastcall SectionBegin(void* section, void* unused, const char* name, int a, int b, int c) {
    g_sectionBegin(section, unused, name, a, b, c);
    if (!name) return;
    const DWORD thread = GetCurrentThreadId();
    if (name == reinterpret_cast<const char*>(kSimFrameName)) {
        // The outermost section: the timer switched on or off here, between frames. What is still open from
        // before (a frame cut short) is dropped, and the last frame rolled up.
        const bool wasOn = g_sectionsOn;
        g_sectionsOn = g_sectionsWanted;
        g_mainThread = thread;
        g_openCount = 0;
        if (g_sectionsOn && !wasOn) ClearSectionTimes();
        else if (g_sectionsOn) SectionsFrame();
    }
    if (!g_sectionsOn || thread != g_mainThread || g_openCount >= kMostOpenSections) return;
    SectionTimes* times = SectionTimesFor(name);
    if (!times) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    g_openSections[g_openCount++] = {section, times, now.QuadPart, 0};
    ++g_sectionCalls;
}

void __fastcall SectionEnd(void* section, void* unused) {
    if (g_sectionsOn && g_openCount > 0 && GetCurrentThreadId() == g_mainThread) {
        int i = g_openCount - 1;
        while (i >= 0 && g_openSections[i].section != section) --i;
        if (i >= 0) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            const OpenSection& open = g_openSections[i];
            const long long all = now.QuadPart - open.start;
            SectionTimes& s = *open.times;
            s.all += all;
            s.own += all - open.inside;
            s.thisFrame += all;
            s.ownThisFrame += all - open.inside;
            ++s.calls;
            ++s.callsThisFrame;
            if (s.key == reinterpret_cast<const char*>(kSimFrameName) && TicksMs(all) > kSlowFrameMs) {
                ++g_slowSimFrames;
                g_simFrameSlow = true;
            }
            g_openCount = i;  // (any opened in it and never closed are dropped with it)
            if (i > 0) g_openSections[i - 1].inside += all;
        }
    }
    g_sectionEnd(section, unused);
}

// Where a value lives: the record named, in the materials or the stress controls; null if none is.
BYTE* GuerrillaValueAt(const GuerrillaValue& v) {
    const DWORD count = *reinterpret_cast<const DWORD*>(v.stress ? kStressControlCount : kMaterialCount);
    BYTE* records = v.stress ? reinterpret_cast<BYTE*>(kStressControls) : *reinterpret_cast<BYTE**>(kMaterials);
    const DWORD size = v.stress ? kStressControlSize : kMaterialSize;
    if (!records || count > 255) return nullptr;
    for (DWORD i = 0; i < count; ++i) {
        BYTE* record = records + i * size;
        if (strncmp(reinterpret_cast<const char*>(record), v.record, 0x20) == 0) return record + v.offset;
    }
    return nullptr;
}

double ReadValue(const GuerrillaValue& v, const BYTE* at) {
    return v.whole ? static_cast<double>(*reinterpret_cast<const int*>(at)) : static_cast<double>(*reinterpret_cast<const float*>(at));
}

void WriteValue(const GuerrillaValue& v, BYTE* at, double value) {
    if (v.whole) *reinterpret_cast<int*>(at) = static_cast<int>(value);
    else *reinterpret_cast<float*>(at) = static_cast<float>(value);
}

bool SameValue(const GuerrillaValue& v, double a, double b) {
    return v.whole ? static_cast<int>(a) == static_cast<int>(b) : static_cast<float>(a) == static_cast<float>(b);
}

// Guerrilla's values in, or the game's back, as `ours` says; each value checked first.
void SetGuerrillaValues(bool ours, const char* when) {
    int changed = 0;
    for (GuerrillaValue& v : g_guerrillaValues) {
        BYTE* at = GuerrillaValueAt(v);
        const double from = ours ? v.game : v.guerrilla, to = ours ? v.guerrilla : v.game;
        if (!ours && !v.set) continue;
        if (!at) {
            rfa::Log("Buildings: the game has no %s %s \"%s\"; it stays as it is", v.stress ? "stress control" : "material", v.record, v.field);
            v.set = false;
            continue;
        }
        const double now = ReadValue(v, at);
        if (!SameValue(v, now, from)) {
            rfa::Log("Buildings: %s's %s is %.10g, not the %s %.10g; left as it is", v.record, v.field, now, ours ? "game's" : "mod's", from);
            v.set = false;
            continue;
        }
        WriteValue(v, at, to);
        v.set = ours;
        ++changed;
        rfa::LogDetail("Buildings: %s's %s %.10g (%s; the game's own %.10g)", v.record, v.field, to, ours ? "Guerrilla's" : "the game's own back", v.game);
    }
    rfa::Log("Buildings: %s (%d values, %s)", ours ? "Guerrilla's materials and load in this world" : "the game's own materials and load again", changed, when);
}

// On entering or leaving our worlds (whenever the game is about to work something out from these values,
// and each frame): the settings read again on entering, and the values set to match.
void BuildingsSync(const char* when) {
    if (!g_buildingsReady) return;
    const bool world = InOurWorld();
    if (world == g_buildingsWorld) return;
    EnterCriticalSection(&g_buildingsLock);
    if (world != g_buildingsWorld) {
        g_buildingsWorld = world;
        if (world) {
            g_anchorsSetting = rfa::IniInt(L"GuerrillaWorld", L"GuerrillaAnchors", 1) != 0;
            g_materialsSetting = rfa::IniInt(L"GuerrillaWorld", L"GuerrillaMaterials", 1) != 0;
            g_destructionLog = rfa::ReadLogSwitch(L"GuerrillaWorld", L"DestructionLog");
            ReadPropLog();
        }
        const bool anchors = world && g_anchorsSetting && g_createWorldAnchor;
        if (anchors != g_anchorsOurs) {
            g_anchorsOurs = anchors;
            rfa::Log(anchors ? "Buildings: pieces on the ground hold to it as Guerrilla's (GuerrillaAnchors=1; %s)"
                             : "Buildings: pieces on the ground hold to it as the game's own (%s)", when);
        }
        const bool materials = world && g_materialsSetting;
        if (materials != g_materialsOurs) {
            g_materialsOurs = materials;
            SetGuerrillaValues(materials, when);
        }
    }
    LeaveCriticalSection(&g_buildingsLock);
}

void __fastcall ResetLinkStrengths(void* shape, void* unused) {
    BuildingsSync("before a building's links were worked out");
    g_resetLinkStrengths(shape, unused);
}

// ---- Cars hitting buildings and props, measured (DestructionLog=1; the user's 22:28 run, 2026-11-18: "when hitting
// walls that are supposed to be breakable I hit them like a brick wall going full speed") ------------------------------
// Both games turn a contact into destruction the same way, as far as read: the contact step (Armageddon 0x670E60,
// Guerrilla rfg_dyn_add_pre_collide_contact RM 0x72DDB0) has the hit's energy worked out (Armageddon 0x671AD0,
// Guerrilla rfg_dyn_compute_point_collision_energy RM 0x71C470: half of each side's mass times its speed squared, times
// the other side's energy scale (a vehicle's Energy_Scale, 1 by default), times 100 when a vehicle is in it), then
// records an impact whose energy becomes the damage to the piece hit (× the level object's Damage_Scalar, 1 by default,
// in Armageddon's object-collision step 0x67C560). The constants of both games' destruction code are the same (200000
// for a dent, 15 for stress, ...). What is left to see is what our cars' hits come to: whether they reach the energy
// step at all, and with how much energy. A car-against-mover contact near the player is counted at the contact step; its
// energy step writes one line (energy, both masses and speeds), at most one each 100 ms and 40 a run.
// Armageddon's energy record (0x671680 makes it): +0 one object, +8 the other, +0x14 the energy, +0x18 and +0x1C the
// masses, +0x20 and +0x2C the velocities, +0x38 flags.
constexpr DWORD kContactStep = 0x670E60, kEnergyStep = 0x671AD0;
constexpr int kCarHitLines = 40;
constexpr float kCarHitNearPlayer = 40.0f;
using ContactStepFn = bool(__cdecl*)(void* event, BYTE* a, BYTE* b);
using EnergyStepFn = bool(__cdecl*)(BYTE* info, void* event);
using ObjectTypeFn = int(__fastcall*)(const BYTE* object, void* unused);
auto const ObjectTypeOf = reinterpret_cast<ObjectTypeFn>(0x422AD0);  // 0 a person, 2 a building or other mover, 3 a vehicle
ContactStepFn g_contactStep;
EnergyStepFn g_energyStep;
volatile LONG g_carHitLines, g_carContacts, g_carContactsTaken;
volatile LONG g_impactLines;  // the impact handlers' lines have a budget of their own
DWORD g_lastCarHitLine, g_lastCarContactsLine;

// The movers a car hit in the last 3 s (their handles), so the later steps' lines can be matched to them: a recorded
// impact keeps the other object's handle at +0xA0 (0x66F390's key), and the per-piece damage step is handed a position
// that isn't the world's, so neither a vehicle check nor a distance from the player finds them.
struct CarHitMover { DWORD handle, at; };
CarHitMover g_carHitMovers[8];
LONG g_carHitMoverNext;

void NoteCarHitMover(DWORD handle) {
    for (CarHitMover& m : g_carHitMovers)
        if (m.handle == handle) {
            m.at = GetTickCount();
            return;
        }
    g_carHitMovers[(InterlockedIncrement(&g_carHitMoverNext) - 1) & 7] = {handle, GetTickCount()};
}

bool HitByCarLately(DWORD handle) {
    const DWORD now = GetTickCount();
    for (const CarHitMover& m : g_carHitMovers)
        if (m.handle == handle && now - m.at < 3000) return true;
    return false;
}

// A vehicle and a mover, near the player: which is which (null when not that pair).
bool CarAndMover(BYTE* a, BYTE* b, BYTE*& car, BYTE*& mover) {
    car = mover = nullptr;
    if (!a || !b) return false;
    const int ta = ObjectTypeOf(a, nullptr), tb = ObjectTypeOf(b, nullptr);
    if (ta == 3 && tb == 2) car = a, mover = b;
    else if (tb == 3 && ta == 2) car = b, mover = a;
    else return false;
    const BYTE* player = LocalPlayer();
    if (!player) return false;
    const float* at = reinterpret_cast<const float*>(car + 4);
    const float* me = reinterpret_cast<const float*>(player + 4);
    const float dx = at[0] - me[0], dz = at[2] - me[2];
    return dx * dx + dz * dz < kCarHitNearPlayer * kCarHitNearPlayer;
}

bool __cdecl ContactStepCounted(void* event, BYTE* a, BYTE* b) {
    BYTE *car, *mover;
    const bool counted = g_destructionLog && g_carHitLines < kCarHitLines && CarAndMover(a, b, car, mover);
    const bool taken = g_contactStep(event, a, b);
    if (counted) {
        InterlockedIncrement(&g_carContacts);
        if (taken) InterlockedIncrement(&g_carContactsTaken);
        const DWORD now = GetTickCount();
        if (now - g_lastCarContactsLine >= 1000 && InterlockedIncrement(&g_carHitLines) <= kCarHitLines) {
            g_lastCarContactsLine = now;
            rfa::LogDetail("Car hits: %ld contacts between a vehicle and a mover near the player in the last second, %ld of them made an impact",
                     InterlockedExchange(&g_carContacts, 0), InterlockedExchange(&g_carContactsTaken, 0));
        }
    }
    return taken;
}

bool __cdecl EnergyStepLogged(BYTE* info, void* event) {
    const bool result = g_energyStep(info, event);
    if (!g_destructionLog || !info || g_carHitLines >= kCarHitLines) return result;
    BYTE *car, *mover;
    if (!CarAndMover(*reinterpret_cast<BYTE**>(info), *reinterpret_cast<BYTE**>(info + 8), car, mover)) return result;
    if (*reinterpret_cast<const float*>(info + 0x14) > 100.0f) NoteCarHitMover(*reinterpret_cast<const DWORD*>(mover + kObjectHandle));
    const DWORD now = GetTickCount();
    if (*reinterpret_cast<const float*>(info + 0x14) < 1000.0f) return result;  // scrapes: no line
    if (now - g_lastCarHitLine < 100 || InterlockedIncrement(&g_carHitLines) > kCarHitLines) return result;
    g_lastCarHitLine = now;
    const float* va = reinterpret_cast<const float*>(info + 0x20);
    const float* vb = reinterpret_cast<const float*>(info + 0x2C);
    const BYTE* first = *reinterpret_cast<BYTE**>(info);
    rfa::LogDetail("Car hits: vehicle %08lX on mover %08lX: energy %.0f (the impact threshold for a dent is 200000), masses %.0f and %.0f kg, speeds "
             "%.1f and %.1f m/s (the %s first), flags 0x%X, %s",
             *reinterpret_cast<const DWORD*>(car + kObjectHandle), *reinterpret_cast<const DWORD*>(mover + kObjectHandle),
             *reinterpret_cast<const float*>(info + 0x14), *reinterpret_cast<const float*>(info + 0x18), *reinterpret_cast<const float*>(info + 0x1C),
             sqrtf(va[0] * va[0] + va[1] * va[1] + va[2] * va[2]), sqrtf(vb[0] * vb[0] + vb[1] * vb[1] + vb[2] * vb[2]),
             first == car ? "vehicle" : "mover", *reinterpret_cast<const WORD*>(info + 0x38), result ? "taken" : "not taken");
    return result;
}

// Past "taken": the impact is recorded and handled in the destruction frame (the dispatcher 0x672380 by its kind:
// 3 → 0x67C440, simple; 4 → 0x67CBE0, complex; Guerrilla's rfg_dyn_apply_object_simple / _complex, RM 0x73A1F0 /
// 0x73A2E0), which hand it to the object-collision step 0x67C560 (cdecl: the mover, the piece, the position, the
// direction, the energy, the kind, flags; Guerrilla's rfg_dyn_apply_single_object_collision). The user's 23:30 run
// (2026-11-19) had cars' impacts taken (energy 231443) and no piece damaged. A handler's impact whose other object (the
// event's +0xBC handle) is a vehicle near the player gets one line: whether it reached the object-collision step, and if
// the simple handler stopped before it, at which of its checks: no rfg_mover for its +0x4C handle (0x424350), the mover
// being removed (+0x54 bit 6), "player damage only" and the player over 20 m away (0x68A5D0, as Guerrilla's
// rfg_mover::player_damage_only), the mover's +0xF0 bit 0x8000, or its +0xDC record's +4 bit 0x100 (the last two are
// Armageddon's own; Guerrilla's handler checks neither).
constexpr DWORD kImpactSimple = 0x67C440, kImpactComplex = 0x67CBE0, kObjectCollision = 0x67C560;
using ImpactHandlerFn = void(__cdecl*)(BYTE* event);
using ObjectCollisionFn = void*(__cdecl*)(BYTE* mover, int piece, const float* position, const float* direction, DWORD energy, int kind,
                                           DWORD flags);
using MoverFromHandleFn = BYTE*(__fastcall*)(void* world, void* unused, DWORD handle);
using MoverCheckFn = bool(__fastcall*)(BYTE* mover, void* unused);
auto const MoverFromHandle = reinterpret_cast<MoverFromHandleFn>(0x424350);
auto const PlayerDamageOnlyAndFar = reinterpret_cast<MoverCheckFn>(0x68A5D0);
ImpactHandlerFn g_impactSimple, g_impactComplex;
ObjectCollisionFn g_objectCollision;
thread_local int t_collisionsReached = 0;
thread_local DWORD t_collisionEnergy = 0;
thread_local int t_collisionPiece = -1;
thread_local int t_pieceDamageCalls = 0;    // the per-piece damage step (0x6797B0, PieceDamaged) on this thread
thread_local int t_collisionPieceDamages = 0;  // how many of them the last object-collision step made
DWORD g_lastImpactLine;

// The event's other object, if it is a vehicle near the player.
bool ImpactByCarNearPlayer(BYTE* event) {
    if (HitByCarLately(*reinterpret_cast<const DWORD*>(event + 0x4C))) return true;
    const DWORD other = *reinterpret_cast<const DWORD*>(event + 0xA0);  // the other object (0x66F390's key +8)
    if (other == 0xFFFFFFFF) return false;
    BYTE* car = ObjectInWorld(reinterpret_cast<void*>(kWorld), nullptr, other);
    if (!car || ObjectTypeOf(car, nullptr) != 3) return false;
    const BYTE* player = LocalPlayer();
    if (!player) return false;
    const float* at = reinterpret_cast<const float*>(car + 4);
    const float* me = reinterpret_cast<const float*>(player + 4);
    const float dx = at[0] - me[0], dz = at[2] - me[2];
    return dx * dx + dz * dz < kCarHitNearPlayer * kCarHitNearPlayer;
}

void* __cdecl ObjectCollisionCounted(BYTE* mover, int piece, const float* position, const float* direction, DWORD energy, int kind, DWORD flags) {
    ++t_collisionsReached;
    t_collisionEnergy = energy;
    t_collisionPiece = piece;
    const int before = t_pieceDamageCalls;
    void* const result = g_objectCollision(mover, piece, position, direction, energy, kind, flags);
    t_collisionPieceDamages = t_pieceDamageCalls - before;
    return result;
}

// Which of the simple handler's checks would stop this impact, read before the handler runs (it can change them).
const char* SimpleImpactStop(BYTE* event) {
    BYTE* mover = MoverFromHandle(reinterpret_cast<void*>(kWorld), nullptr, *reinterpret_cast<const DWORD*>(event + 0x4C));
    if (!mover) return "no rfg_mover for its handle";
    if (mover[0x54] & 0x40) return "the mover is being removed (+0x54 bit 6)";
    if (PlayerDamageOnlyAndFar(mover, nullptr)) return "\"player damage only\" and the player over 20 m away";
    if (*reinterpret_cast<const DWORD*>(mover + 0xF0) & 0x8000) return "the mover's +0xF0 bit 0x8000 (Armageddon's own check)";
    const BYTE* record = *reinterpret_cast<BYTE* const*>(mover + 0xDC);
    if (record && (*reinterpret_cast<const DWORD*>(record + 4) & 0x100)) return "the mover's +0xDC record's +4 bit 0x100 (Armageddon's own check)";
    return "none of its first checks (something later in the handler)";
}

void LogImpact(BYTE* event, bool simple, const char* stop) {
    const DWORD now = GetTickCount();
    if (now - g_lastImpactLine < 200 || InterlockedIncrement(&g_impactLines) > kCarHitLines) return;
    g_lastImpactLine = now;
    const DWORD handle = *reinterpret_cast<const DWORD*>(event + 0x4C);
    if (t_collisionsReached)
        rfa::LogDetail("Car hits: the %s impact on mover %08lX (piece %d) reached the object-collision step with energy %lu and piece %d; the "
                       "per-piece damage step ran %d times in it",
                       simple ? "simple" : "complex", handle, *reinterpret_cast<const int*>(event + 0x48), t_collisionEnergy, t_collisionPiece,
                       t_collisionPieceDamages);
    else
        rfa::LogDetail("Car hits: the %s impact on mover %08lX (piece %d) stopped before the object-collision step: %s", simple ? "simple" : "complex",
                       handle, *reinterpret_cast<const int*>(event + 0x48), stop);
}

void __cdecl ImpactSimpleLogged(BYTE* event) {
    const bool watched = g_destructionLog && event && g_impactLines < kCarHitLines && ImpactByCarNearPlayer(event);
    const char* stop = watched ? SimpleImpactStop(event) : nullptr;
    t_collisionsReached = 0;
    g_impactSimple(event);
    if (watched) LogImpact(event, true, stop);
}

void __cdecl ImpactComplexLogged(BYTE* event) {
    const bool watched = g_destructionLog && event && g_impactLines < kCarHitLines && ImpactByCarNearPlayer(event);
    t_collisionsReached = 0;
    g_impactComplex(event);
    if (watched) LogImpact(event, false, "a check of the complex handler's (not looked into)");
}

// A hit on an anchored piece, logged (a diagnosis, for road signs that stop a car the first time): the game's damage
// to one piece, 0x6797B0 (cdecl: the mover, the piece's state, the damage, the hit's position, its kind, two flags (the
// second: break it outright), two more; Guerrilla's rfg_dyn_apply_damage_to_subpiece, RM 0x732A30). Like Guerrilla's,
// it takes the damage off the piece's anchor strength (state +0, while state +0x18 bit 0 says it's anchored; 0x679E60)
// and frees the piece once that reaches 0. One line per hit on a piece still anchored, within 12 m of the player, not
// one that breaks it outright: the mover, the damage against the strength, the strength left, and the hit's kind as
// passed (if it is Guerrilla's order, 3 and 4 are something touching it, DET_OBJECT_SIMPLE and _COMPLEX; not checked).
// At most 40 a run (DestructionLog=1).
constexpr DWORD kPieceDamage = 0x6797B0;
constexpr size_t kPieceDamagePrologue = 9;  // push ebp / mov ebp,esp / sub esp,0B4h
constexpr int kPieceHitLines = 40;
constexpr float kPieceHitNearPlayer = 12.0f;
using PieceDamageFn = DWORD(__cdecl*)(BYTE* mover, BYTE* state, DWORD damage, const float* position, int kind, int flag, int outright,
                                      void* other, void* more);
PieceDamageFn g_pieceDamage;
volatile LONG g_pieceHitLines;

// A piece's links as the game holds them (for the car-hit lines, 2026-11-19): the piece's link numbers (state +4, u16;
// how many at state +0x23), each link's run-time yield left (shape +0x28: an int per link number; 0x663620) and its
// record (the base shape, shape +0x3C, its +0x10: 16 bytes per link, yield_max +0, area +4, flags +0xC; 0x663600, as
// Guerrilla's rfg_link_base). The per-piece damage step takes an impact's share off each link's yield and breaks the link
// at 0 (0x699790, 0x699620). Read through CopyIfReadable, so a wrong guess can't fault.
constexpr int kLinksShown = 6;
struct LinkSnapshot {
    int count = 0;
    int left[kLinksShown] = {}, most[kLinksShown] = {};
    float area[kLinksShown] = {};
};

void SnapLinks(BYTE* mover, BYTE* state, LinkSnapshot& snap) {
    snap.count = 0;
    BYTE* shape = ShapeOf(mover, nullptr);
    BYTE count = 0;
    BYTE* numbers = nullptr;
    BYTE* yields = nullptr;
    BYTE* base = nullptr;
    BYTE* records = nullptr;
    if (!shape || !CopyIfReadable(&count, state + 0x23, 1) || !CopyIfReadable(&numbers, state + 4, 4) || !CopyIfReadable(&yields, shape + 0x28, 4) ||
        !CopyIfReadable(&base, shape + 0x3C, 4) || !base || !CopyIfReadable(&records, base + 0x10, 4) || !numbers || !yields || !records)
        return;
    for (int i = 0; i < count && snap.count < kLinksShown; ++i) {
        WORD number = 0;
        if (!CopyIfReadable(&number, numbers + i * 2, 2)) break;
        int left = 0, most = 0;
        float area = 0;
        if (!CopyIfReadable(&left, yields + number * 4, 4) || !CopyIfReadable(&most, records + number * 16, 4) ||
            !CopyIfReadable(&area, records + number * 16 + 4, 4))
            break;
        snap.left[snap.count] = left;
        snap.most[snap.count] = most;
        snap.area[snap.count] = area;
        ++snap.count;
    }
}

std::string DescribeLinks(const LinkSnapshot& before, const LinkSnapshot& after) {
    std::string text;
    char part[96];
    for (int i = 0; i < before.count; ++i) {
        snprintf(part, sizeof(part), "%s%d -> %d of %d (%.2f m2)", i ? ", " : "", before.left[i], i < after.count ? after.left[i] : -1, before.most[i],
                 before.area[i]);
        text += part;
    }
    return text.empty() ? "none read" : text;
}

DWORD __cdecl PieceDamaged(BYTE* mover, BYTE* state, DWORD damage, const float* position, int kind, int flag, int outright, void* other,
                           void* more) {
    ++t_pieceDamageCalls;
    if (!g_destructionLog || g_pieceHitLines >= kPieceHitLines || (outright & 0xFF) || !state || !mover || !position)
        return g_pieceDamage(mover, state, damage, position, kind, flag, outright, other, more);
    // Pieces not anchored to the ground are logged too (2026-11-18): a wall's pieces hang on links to others.
    const bool wasAnchored = (*reinterpret_cast<const DWORD*>(state + 0x18) & 1) != 0;
    const int before = wasAnchored ? *reinterpret_cast<const int*>(state) : 0;
    LinkSnapshot linksBefore, linksAfter;
    const bool carHitBefore = HitByCarLately(*reinterpret_cast<const DWORD*>(mover + kObjectHandle));
    if (carHitBefore) SnapLinks(mover, state, linksBefore);
    const DWORD result = g_pieceDamage(mover, state, damage, position, kind, flag, outright, other, more);
    if (carHitBefore) SnapLinks(mover, state, linksAfter);
    const BYTE* player = LocalPlayer();
    if (!player || !InOurWorld()) return result;
    // Near the player by the mover's own position (the hit position handed in isn't the world's), or a mover a car hit.
    const float* at = reinterpret_cast<const float*>(player + 4);
    const float* moverAt = reinterpret_cast<const float*>(mover + 4);
    const float dx = moverAt[0] - at[0], dy = moverAt[1] - at[1], dz = moverAt[2] - at[2];
    const float distance = sqrtf(dx * dx + dy * dy + dz * dz);
    const bool carHit = HitByCarLately(*reinterpret_cast<const DWORD*>(mover + kObjectHandle));
    if ((distance > kPieceHitNearPlayer && !carHit) || InterlockedIncrement(&g_pieceHitLines) > kPieceHitLines) return result;
    const bool anchored = (*reinterpret_cast<const DWORD*>(state + 0x18) & 1) != 0;
    const int after = anchored ? *reinterpret_cast<const int*>(state) : 0;
    const bool loose = (*reinterpret_cast<const DWORD*>(state + 0x18) & 0x100) != 0;  // freed (0x69A720 sets it)
    if (wasAnchored)
        rfa::LogDetail("Piece hit: mover %08lX, kind %d, damage %lu against an anchor strength of %d: %s (%d left); at (%.1f, %.1f, %.1f), %.1f m "
                 "from the player",
                 *reinterpret_cast<const DWORD*>(mover + kObjectHandle), kind, damage, before, !anchored || after <= 0 ? "it gives way" : "it holds",
                 after, position[0], position[1], position[2], distance);
    else
        rfa::LogDetail("Piece hit: mover %08lX, kind %d, damage %lu on a piece held by links, not anchored: %s; at (%.1f, %.1f, %.1f), %.1f m from the "
                 "player",
                 *reinterpret_cast<const DWORD*>(mover + kObjectHandle), kind, damage, loose ? "it came loose" : "still held", position[0],
                 position[1], position[2], distance);
    if (carHitBefore)
        rfa::LogDetail("Piece hit: its links (yield left before -> after the hit, of the most, and the link's area): %s",
                       DescribeLinks(linksBefore, linksAfter).c_str());
    return result;
}

// ---- Props near the player, logged (PropLog=1; a diagnosis for the user's 2026-11-15 test: a barrel gone when
// hammered, barrels ending upside down when bumped) ------------------------------------------------------------------
// A hit: the game's damage to a prop (general_mover::apply_damage, 0x8CEC80, vtable 0xFCE88C +0xA4; thiscall, ten
// arguments: the damage, its kind, two vectors, the attacker, ...; Guerrilla's RM 0xA8ACF0). One line with its hit points
// before and after (the level object's, at +0xB4 → +0x1C) and whether it is killed (vtable +0x10), and 0.3 s later one
// with how fast it moves and how far it went, or that it is gone. A tip: every frame a slice of the world's props
// (the world's list for their type, +0x68 + type × 12: the objects, room, count) is looked at; one within 4 m of the
// player is remembered with which way its top faces; one line when its top has turned more than 120° from that.
// Each line has its mass and balance point as Havok holds them: the body (0x778440 on its handle +0x4C) holds its
// motion state (hkMotionState: the transform, whose translation is the object's position, then the swept transform:
// centre of mass 0 and 1, rotations 0 and 1, centre of mass local; the motion's inertia and inverse mass come 0x80 after
// the translation, its linear velocity 0x90 after). The translation's place is found once, by the object's position.
constexpr DWORD kPropDamage = 0x8CEC80;
constexpr size_t kPropDamagePrologue = 6;  // push ebp / mov ebp,esp / sub esp,74h
constexpr DWORD kPropVtable = 0xFCE88C;
constexpr DWORD kObjectPos = 0x04, kObjectOrient = 0x10, kObjectBody = 0x4C, kObjectTypeNumber = 0x7A, kObjectProps = 0xB4;
constexpr DWORD kPropsHitpoints = 0x1C;
constexpr DWORD kWorldTypeLists = 0x68;
constexpr int kPropLines = 60, kPropsPerFrame = 256, kTrackedProps = 64;
constexpr float kPropHitNear = 15.0f, kPropTipNear = 4.0f;
using PropDamageFn = DWORD(__fastcall*)(BYTE* prop, void* unused, float damage, int kind, const float* a, const float* b, BYTE* attacker,
                                         DWORD handle, float scale, int x, int y, int z);
using IsKilledFn = bool(__fastcall*)(BYTE* object, void* unused);
auto const PropBody = reinterpret_cast<BYTE*(__cdecl*)(DWORD handle)>(0x778440);
PropDamageFn g_propDamage;
bool g_propLog = false;
volatile LONG g_propLines;
int g_propTranslation = -1;  // where in a body the motion state's translation is (found once)
int g_propType = -1;         // the props' type number in the world's lists (found once)
int g_propScan = 0;
struct TrackedProp { DWORD handle; float up[3]; DWORD seen; bool told; };
TrackedProp g_tracked[kTrackedProps];
struct PropFollow { DWORD handle; DWORD due; float from[3]; };
PropFollow g_follows[8];

bool PropLineLeft() { return g_propLines < kPropLines; }

// A value of the game's, read with care: false when the memory went away, as CopyIfReadable catches. The prop log reads
// the world's object lists and the objects in them while the game may be freeing them (a zone streaming out). Asking
// Windows first (VirtualQuery, as this log once did) wasn't enough: the 19:02 run faulted reading an object's vtable
// just after VirtualQuery had called it readable, with the search running every frame, and the game crashed in its heap
// 0.4 s later.
template <typename T> bool ReadGame(const void* at, T& value) { return CopyIfReadable(&value, at, sizeof(T)); }

// Its body's mass, local balance point and speed, as text; "no body" or "body layout not found" otherwise.
std::string PropBodyText(BYTE* prop) {
    char text[200];
    DWORD bodyHandle = 0;
    float at[3];
    if (!ReadGame(prop + kObjectBody, bodyHandle) || !ReadGame(prop + kObjectPos, at)) return "gone";
    BYTE* body = PropBody(bodyHandle);
    if (!body) return "no body";
    if (g_propTranslation < 0) {
        for (int o = 0x40; o < 0x300; o += 16) {
            float v[3];
            if (!CopyIfReadable(v, body + o, sizeof(v))) break;
            if (fabsf(v[0] - at[0]) < 0.01f && fabsf(v[1] - at[1]) < 0.01f && fabsf(v[2] - at[2]) < 0.01f &&
                (fabsf(at[0]) + fabsf(at[1]) + fabsf(at[2])) > 1.0f) {
                g_propTranslation = o;
                rfa::LogDetail("Props: a body's motion state found (its translation at +0x%X)", o);
                break;
            }
        }
        if (g_propTranslation < 0) return "body layout not found";
    }
    float local[4], inertia[4], velocity[4];
    if (!CopyIfReadable(local, body + g_propTranslation + 0x50, sizeof(local)) ||
        !CopyIfReadable(inertia, body + g_propTranslation + 0x80, sizeof(inertia)) ||
        !CopyIfReadable(velocity, body + g_propTranslation + 0x90, sizeof(velocity)))
        return "body unreadable";
    const float speed = sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
    snprintf(text, sizeof(text), "mass %.1f kg, balance point (%.2f, %.2f, %.2f) from its origin, inverse inertia (%.4f, %.4f, %.4f), moving %.1f m/s",
             inertia[3] > 0 ? 1.0f / inertia[3] : 0.0f, local[0], local[1], local[2], inertia[0], inertia[1], inertia[2], speed);
    return text;
}

// Which way its top faces (its orientation's second row); false when it can't be read.
bool PropUp(const BYTE* prop, float (&up)[3]) { return ReadGame(prop + kObjectOrient + 3 * sizeof(float), up); }

// Guerrilla's melee hit on a loose prop. Both games' melee (Guerrilla's object_mover::process_melee_hit RM 0xA139C0,
// Armageddon's 0x8DA940, vtable +0x78) apply the move's structure figure as damage of kind 5, then push the body with the
// move's ImpactForce (the same in both: the per-set figure, times the attacker's upgrades). The prop's apply_damage (both:
// the figure × 200 ÷ the body's mass off its hit points; taken away only when a flagged vehicle hits it, or at 0 hit points
// if its physical material is crushable) takes the figure as it comes. Our melee table carries Guerrilla's structure figures
// × 100, as Guerrilla's buildings take them (its rfg_mover::apply_damage multiplies a melee hit by 100 itself, Armageddon's
// doesn't; see the world builder's melee moves), so a loose prop took 100 times Guerrilla's: the hammer's 150,000 for 1,500
// took the blue barrel's 50,000 hit points at one blow (1,500 takes 3,000 of them a blow, as in Guerrilla). The user saw
// hammered barrels go (2026-11-19); what took them once their hit points were gone isn't known. In our worlds a melee hit on
// a loose prop is Guerrilla's figure again.
constexpr int kMeleeDamageKind = 5;
constexpr float kGuerrillaMeleeBuildingScale = 100.0f;

DWORD __fastcall PropDamaged(BYTE* prop, void* unused, float damage, int kind, const float* a, const float* b, BYTE* attacker, DWORD handle,
                             float scale, int x, int y, int z) {
    if (kind == kMeleeDamageKind && InOurWorld()) {
        RFA_LOG_FIRST(1, "Props: a melee hit on a loose prop takes Guerrilla's figure, the table's divided by %.0f (%.0f for %.0f)",
                      kGuerrillaMeleeBuildingScale, damage / kGuerrillaMeleeBuildingScale, damage);
        damage /= kGuerrillaMeleeBuildingScale;
    }
    // Its hit points before the hit: its death goes to the campaign's activities (Guerrilla's general_mover::
    // apply_damage, RM 0xA8B032, calls activity_register_death when its hit points go from above 0 to 0 or less).
    const BYTE* propsBefore = prop ? *reinterpret_cast<BYTE* const*>(prop + kObjectProps) : nullptr;
    const int hitPointsBefore = propsBefore ? *reinterpret_cast<const int*>(propsBefore + kPropsHitpoints) : 0;
    if (!g_propLog || !prop || !PropLineLeft()) {
        const DWORD result = g_propDamage(prop, unused, damage, kind, a, b, attacker, handle, scale, x, y, z);
        GuerrillaCampaign_PropDamaged(prop, attacker, hitPointsBefore);
        return result;
    }
    const BYTE* player = LocalPlayer();
    const float* at = reinterpret_cast<const float*>(prop + kObjectPos);
    const float* me = player ? reinterpret_cast<const float*>(player + kObjectPos) : nullptr;
    const float dx = me ? at[0] - me[0] : 0, dz = me ? at[2] - me[2] : 0;
    const bool close = me && dx * dx + dz * dz < kPropHitNear * kPropHitNear;
    const BYTE* props = *reinterpret_cast<BYTE* const*>(prop + kObjectProps);
    const int before = close && props ? *reinterpret_cast<const int*>(props + kPropsHitpoints) : 0;
    const DWORD result = g_propDamage(prop, unused, damage, kind, a, b, attacker, handle, scale, x, y, z);
    GuerrillaCampaign_PropDamaged(prop, attacker, hitPointsBefore);
    if (!close || !InOurWorld()) return result;
    InterlockedIncrement(&g_propLines);
    const DWORD self = *reinterpret_cast<const DWORD*>(prop + kObjectHandle);
    const int after = props ? *reinterpret_cast<const int*>(props + kPropsHitpoints) : 0;
    const bool killed = reinterpret_cast<IsKilledFn>((*reinterpret_cast<void***>(prop))[0x10 / 4])(prop, nullptr);
    rfa::LogDetail("Props: %08lX hit (damage %.0f, kind %d, by %s): hit points %d -> %d%s; %s; at (%.1f, %.1f, %.1f)", self, damage, kind,
             attacker == player ? "the player" : (attacker ? "someone" : "nothing"), before, after, killed ? ", killed" : "",
             PropBodyText(prop).c_str(), at[0], at[1], at[2]);
    for (PropFollow& f : g_follows)
        if (!f.handle) {
            f = {self, GetTickCount() + 300, {at[0], at[1], at[2]}};
            break;
        }
    return result;
}

// When one of our worlds loads: PropLog= (0, off, unless set).
void ReadPropLog() {
    g_propLog = rfa::ReadLogSwitch(L"GuerrillaWorld", L"PropLog") && g_propDamage;
    if (g_propLog) rfa::Log("Props: hits and tips of the props near the player are logged (PropLog=1, at most %d lines)", kPropLines);
}

// Each frame with PropLog=1 in our worlds: the hits' follow-ups, then a slice of the props for tips.
void PropLogUpdate() {
    if (!g_propLog || !PropLineLeft()) return;
    const BYTE* player = LocalPlayer();
    if (!player) return;
    const DWORD now = GetTickCount();
    for (PropFollow& f : g_follows) {
        if (!f.handle || static_cast<LONG>(now - f.due) < 0) continue;
        BYTE* prop = ObjectInWorld(reinterpret_cast<void*>(kWorld), nullptr, f.handle);
        if (!prop) rfa::LogDetail("Props: %08lX 0.3 s after its hit: gone from the world", f.handle);
        else if (float at[3]; ReadGame(prop + kObjectPos, at)) {
            const float dx = at[0] - f.from[0], dy = at[1] - f.from[1], dz = at[2] - f.from[2];
            rfa::LogDetail("Props: %08lX 0.3 s after its hit: moved %.1f m (up %.1f); %s", f.handle, sqrtf(dx * dx + dy * dy + dz * dz), dy,
                     PropBodyText(prop).c_str());
        }
        InterlockedIncrement(&g_propLines);
        f.handle = 0;
    }
    const BYTE* world = reinterpret_cast<const BYTE*>(kWorld);
    // Which list is the props' is found once, with the level loaded: not every list holds objects (the 18:22 and
    // 18:51 runs faulted reading one's entries as objects as the world loaded), so every entry and object is read with
    // care (ReadGame), and the object found must say it is of that type (+0x7A).
    if (g_propType == -1) {  // -2: searched and not found, not searched again
        if (*reinterpret_cast<const BYTE*>(kLevelLoaded) == 0) return;
        for (int t = 0; t < 64 && g_propType < 0; ++t) {
            const BYTE* entry = world + kWorldTypeLists + t * 12;
            BYTE* const* list = nullptr;
            int room = 0, count = 0;
            if (!ReadGame(entry, list) || !ReadGame(entry + 4, room) || !ReadGame(entry + 8, count)) continue;
            if (!list || count <= 0 || count > room || room > 65536) continue;
            for (int i = 0; i < count && i < 64; ++i) {
                BYTE* object = nullptr;
                DWORD vtable = 0;
                BYTE type = 0;
                if (!ReadGame(list + i, object) || !object || !ReadGame(object, vtable) || vtable != kPropVtable) continue;
                if (!ReadGame(object + kObjectTypeNumber, type) || type != t) continue;
                g_propType = t;
                rfa::LogDetail("Props: the world's props are its type %d objects (%d now)", t, count);
                break;
            }
        }
        if (g_propType < 0) {
            g_propType = -2;  // not found: no tips are looked for, and the search isn't made again
            rfa::Log("Props: the world's list of props wasn't found; only hits are logged");
        }
    }
    if (g_propType < 0) return;
    const BYTE* entry = world + kWorldTypeLists + g_propType * 12;
    BYTE* const* list = nullptr;
    int count = 0;
    float me[3];
    if (!ReadGame(entry, list) || !ReadGame(entry + 8, count) || !list || count <= 0 || !ReadGame(player + kObjectPos, me)) return;
    for (int n = 0; n < kPropsPerFrame && n < count; ++n) {
        g_propScan = (g_propScan + 1) % count;
        BYTE* prop = nullptr;
        DWORD vtable = 0, handle = 0;
        float at[3], up[3];
        if (!ReadGame(list + g_propScan, prop) || !prop || !ReadGame(prop, vtable) || vtable != kPropVtable) continue;
        if (!ReadGame(prop + kObjectPos, at) || !ReadGame(prop + kObjectHandle, handle) || !PropUp(prop, up)) continue;
        const float dx = at[0] - me[0], dz = at[2] - me[2];
        TrackedProp* slot = nullptr;
        TrackedProp* spare = nullptr;
        for (TrackedProp& t : g_tracked) {
            if (t.handle == handle) slot = &t;
            else if (!spare && (!t.handle || now - t.seen > 20000)) spare = &t;
        }
        if (!slot) {
            if (dx * dx + dz * dz > kPropTipNear * kPropTipNear || !spare) continue;
            *spare = {handle, {up[0], up[1], up[2]}, now, false};
            continue;
        }
        slot->seen = now;
        const float turned = up[0] * slot->up[0] + up[1] * slot->up[1] + up[2] * slot->up[2];
        if (slot->told || turned > -0.5f) continue;  // turned less than 120 degrees
        slot->told = true;
        InterlockedIncrement(&g_propLines);
        rfa::LogDetail("Props: %08lX turned over (its top now faces (%.2f, %.2f, %.2f), was (%.2f, %.2f, %.2f)); %s; at (%.1f, %.1f, %.1f)", handle,
                 up[0], up[1], up[2], slot->up[0], slot->up[1], slot->up[2], PropBodyText(prop).c_str(), at[0], at[1], at[2]);
        if (!PropLineLeft()) return;
    }
}

void __fastcall CreateWorldAnchor(void* shape, void* unused, void* mover, BYTE* state) {
    BuildingsSync("before a piece was anchored");
    g_createWorldAnchor(shape, unused, mover, state);
    if (!g_anchorsOurs || !state) return;
    // The pieces the game gave a strength: its own, anchored, not indestructible.
    if (!(*reinterpret_cast<const DWORD*>(state + 0x18) & 1)) return;
    if (!reinterpret_cast<PieceOwnedByFn>(kPieceOwnedBy)(shape, nullptr, state)) return;
    const BYTE* base = reinterpret_cast<PieceBaseFn>(kPieceBase)(shape, nullptr, state);
    if (!base || (base[0x3F] & 0x20)) return;
    // Guerrilla's: the footprint, times 256, times the material's link strength.
    const float* box = reinterpret_cast<const float*>(base);  // lowest x, y, z, then highest
    const float footprint = (box[3] - box[0]) * (box[5] - box[2]);
    const int area = static_cast<int>(static_cast<double>(footprint) * 256.0);
    const BYTE* material = reinterpret_cast<MaterialRecordFn>(kMaterialRecord)(base[0x3C]);
    if (!material) return;
    const int strength = static_cast<int>(static_cast<unsigned>(*reinterpret_cast<const int*>(material + 0x40)) << 8);
    // Guerrilla keeps the 32 bits of it as they come (G 0x72FD2E-0x72FD37: no floor, no clamp): 0 for a piece
    // whose footprint is under 1/256 m² (a pole, an upright panel), negative past 2^31 (a footprint of some
    // 500 m² of super-strong steel). Either gives way at the first hit there, as the game's floor of 1 does.
    const int guerrilla = static_cast<int>(static_cast<unsigned>((static_cast<long long>(area) * strength) >> 16));
    if (guerrilla == 0) InterlockedIncrement(&g_anchorsNone);
    else if (guerrilla < 0) InterlockedIncrement(&g_anchorsWrapped);
    const int held = guerrilla < 1 ? 1 : guerrilla;
    *reinterpret_cast<int*>(state) = held;
    InterlockedIncrement(&g_anchorsMade);
    if (g_anchorLeast < 0 || held < g_anchorLeast) g_anchorLeast = held;
    if (held > g_anchorMost) g_anchorMost = held;
}

// What the game anchors and links by contact (DestructionLog=1). As a building streams in, it trusts its zone's
// saved anchors and links (world_anchors, dynamic_links) when the zone's dest_checksum is its file's; else (or
// with none saved) it finds them itself: Havok's getPenetrations (create_anchors 0x8D8C10) hands each piece
// touching something to subpiece_collector::set_as_anchor (0x689310, thiscall: the collector; the mover, its
// piece (short), the other mover or none, its piece (u16); ret 0x10), which anchors a piece touching the world
// (0x698F30) and links one touching another building (0x684A90: a run-time link, 0x28 bytes from 0x155E9A8, the
// next free one's number at 0x155E1D8 + 2 × the count taken, 0x15685E8: +0/+2 the two pieces, +4/+6 their
// depths from the ground, +8/+0x0C the two objects' handles, +0x14 the contact area × 1024). In our worlds every
// building's zone data is trusted, so this logs only what the game had to work out itself.
constexpr DWORD kSetAsAnchor = 0x689310;
constexpr size_t kSetAsAnchorPrologue = 6;  // push ebp / mov ebp,esp / sub esp,18h
constexpr DWORD kRunTimeLinks = 0x155E9A8, kRunTimeLinkFree = 0x155E1D8, kRunTimeLinkSize = 0x28;
constexpr DWORD kMoverShape = 0x42A1F0, kPieceState = 0x4A3400;  // thiscall (the mover): its shape; (the shape; a piece): its state
using SetAsAnchorFn = void(__fastcall*)(void* collector, void* unused, BYTE* mover, int piece, BYTE* other, int otherPiece);
using MoverShapeFn = void*(__fastcall*)(BYTE* mover, void* unused);
using PieceStateFn = const BYTE*(__fastcall*)(void* shape, void* unused, int piece);
SetAsAnchorFn g_setAsAnchor;
int g_contactLines;

void __fastcall SetAsAnchor(void* collector, void* unused, BYTE* mover, int piece, BYTE* other, int otherPiece) {
    if (!g_destructionLog || !mover || !InOurWorld() || g_contactLines >= 4000) {
        g_setAsAnchor(collector, unused, mover, piece, other, otherPiece);
        return;
    }
    const bool pool = rfa::CodeValue(kDynamicLinkCheckAt) == kDynamicLinksTaken;
    const int takenBefore = pool ? *reinterpret_cast<const int*>(kDynamicLinksTaken) : 0;
    const DWORD limit = rfa::CodeValue(kDynamicLinkLimitAt);
    const WORD slot = pool && takenBefore >= 0 && static_cast<DWORD>(takenBefore) < limit
                          ? *reinterpret_cast<const WORD*>(kRunTimeLinkFree + takenBefore * 2) : 0xFFFF;
    g_setAsAnchor(collector, unused, mover, piece, other, otherPiece);
    const DWORD handle = *reinterpret_cast<const DWORD*>(mover + 0x68);
    const short mine = static_cast<short>(piece);
    ++g_contactLines;
    if (!other) {
        void* shape = reinterpret_cast<MoverShapeFn>(kMoverShape)(mover, nullptr);
        const BYTE* state = shape ? reinterpret_cast<PieceStateFn>(kPieceState)(shape, nullptr, mine) : nullptr;
        rfa::LogDetail("Contact: %08lX piece %d touches the world: %s", handle, mine,
                 state && (*reinterpret_cast<const DWORD*>(state + 0x18) & 1) ? "anchored" : "not anchored");
        return;
    }
    const DWORD otherHandle = *reinterpret_cast<const DWORD*>(other + 0x68);
    const int takenAfter = pool ? *reinterpret_cast<const int*>(kDynamicLinksTaken) : 0;
    if (pool && takenAfter == takenBefore + 1 && slot != 0xFFFF) {
        const BYTE* link = reinterpret_cast<const BYTE*>(kRunTimeLinks + slot * kRunTimeLinkSize);
        rfa::LogDetail("Contact: %08lX piece %d touches %08lX piece %u: linked (link pieces %u and %u, depths %u and %u, area %d = %.3f m2)",
                 handle, mine, otherHandle, static_cast<unsigned>(static_cast<WORD>(otherPiece)), *reinterpret_cast<const WORD*>(link),
                 *reinterpret_cast<const WORD*>(link + 2), *reinterpret_cast<const WORD*>(link + 4), *reinterpret_cast<const WORD*>(link + 6),
                 *reinterpret_cast<const int*>(link + 0x14), *reinterpret_cast<const int*>(link + 0x14) / 1024.0);
    } else {
        rfa::LogDetail("Contact: %08lX piece %d touches %08lX piece %u: no link made", handle, mine, otherHandle,
                 static_cast<unsigned>(static_cast<WORD>(otherPiece)));
    }
}

void CountBreakEvent(int kind) {
    if (kind >= 0 && kind < kEventKinds) InterlockedIncrement(&g_breakEvents[kind]);
    else InterlockedIncrement(&g_otherBreakEvents);
}

void BuildingsUpdate() {
    BuildingsSync("each frame");
    g_sectionsWanted = g_buildingsWorld && g_destructionLog && g_sectionBegin && g_sectionEnd;
    if (!g_buildingsWorld) {
        g_frameAt.QuadPart = 0;
        g_framesMarked = false;
        return;
    }
    MarkFrame();
    if (g_destructionLog) TimeFrame();
    PropLogUpdate();
    const bool pool = rfa::CodeValue(kDynamicLinkCheckAt) == kDynamicLinksTaken;
    const int links = pool ? *reinterpret_cast<const int*>(kDynamicLinksTaken) : -1;
    if (links > g_dynamicLinksMost) g_dynamicLinksMost = links;
    const DWORD now = GetTickCount();
    if (now - g_lastBuildingsLog < 10000) return;
    g_lastBuildingsLog = now;
    LONG events = g_otherBreakEvents;
    for (LONG n : g_breakEvents) events += n;
    if (!g_destructionLog) return;
    std::string line = "Destruction (10 s): ";
    char part[256];
    for (int k = 0; k < kEventKinds; ++k)
        if (g_breakEvents[k] > 0) {
            snprintf(part, sizeof(part), "%s %ld, ", kEventKindNames[k], g_breakEvents[k]);
            line += part;
        }
    if (g_otherBreakEvents > 0) {
        snprintf(part, sizeof(part), "other %ld, ", g_otherBreakEvents);
        line += part;
    }
    if (events == 0) line += "no events, ";
    if (g_anchorsMade > 0) {
        snprintf(part, sizeof(part), "%ld pieces anchored to the ground, Guerrilla's strength %lld to %lld (the game's own: 1), ", g_anchorsMade,
                 g_anchorLeast, g_anchorMost);
        line += part;
        if (g_anchorsNone > 0) {
            snprintf(part, sizeof(part), "%ld of them 0 in Guerrilla (a footprint under 1/256 m2; held at 1, the game's floor), ", g_anchorsNone);
            line += part;
        }
        if (g_anchorsWrapped > 0) {
            snprintf(part, sizeof(part), "%ld past 2^31 (negative in Guerrilla; held at 1, the game's floor), ", g_anchorsWrapped);
            line += part;
        }
    }
    if (pool) snprintf(part, sizeof(part), "links between buildings %d of %lu in use (the most %d)", links, rfa::CodeValue(kDynamicLinkLimitAt), g_dynamicLinksMost);
    else snprintf(part, sizeof(part), "the links between buildings can't be counted (the game's code isn't as expected)");
    line += part;
    if (g_frames > 0) {
        snprintf(part, sizeof(part), "; %d frames, %.1f ms on average (the slowest %.1f ms, %d slower than 30 a second); "
                 "the game's physics %.1f ms a frame on average (the most %.1f ms, %d frames over its debris balancer's 10 ms)",
                 g_frames, g_frameMs / g_frames, g_frameWorst, g_slowFrames, g_physicsMs / g_frames, g_physicsWorst, g_overBudget);
        line += part;
    }
    if (g_backgroundFrames > 0) {
        snprintf(part, sizeof(part), "; %d frames with the game's window behind another left out", g_backgroundFrames);
        line += part;
    }
    rfa::LogDetail("%s", line.c_str());
    for (LONG& n : g_breakEvents) n = 0;
    g_otherBreakEvents = g_anchorsMade = g_anchorsNone = g_anchorsWrapped = 0;
    g_anchorLeast = -1;
    g_anchorMost = 0;
    g_frames = g_slowFrames = g_overBudget = g_backgroundFrames = 0;
    g_frameMs = g_frameWorst = g_physicsMs = g_physicsWorst = 0;
}

// rfa_mods.ini saved (CheckSettings): the three switches read again, and in one of our worlds put in force at
// once for what the game works out from then on (see the top of this section).
void BuildingsSettingsChanged() {
    if (!g_buildingsReady) return;
    EnterCriticalSection(&g_buildingsLock);
    const bool anchors = rfa::IniInt(L"GuerrillaWorld", L"GuerrillaAnchors", 1) != 0;
    const bool materials = rfa::IniInt(L"GuerrillaWorld", L"GuerrillaMaterials", 1) != 0;
    const bool log = rfa::ReadLogSwitch(L"GuerrillaWorld", L"DestructionLog");
    if (anchors != g_anchorsSetting || materials != g_materialsSetting || log != g_destructionLog) {
        g_anchorsSetting = anchors;
        g_materialsSetting = materials;
        g_destructionLog = log;
        rfa::Log("Buildings: rfa_mods.ini saved: GuerrillaAnchors=%d, GuerrillaMaterials=%d, DestructionLog=%d", anchors ? 1 : 0, materials ? 1 : 0, log ? 1 : 0);
        if (g_buildingsWorld) {
            const bool anchorsOurs = anchors && g_createWorldAnchor;
            if (anchorsOurs != g_anchorsOurs) {
                g_anchorsOurs = anchorsOurs;
                rfa::Log(anchorsOurs ? "Buildings: pieces anchored from now on hold to the ground as Guerrilla's (`level` again anchors every building so)"
                                     : "Buildings: pieces anchored from now on hold to the ground as the game's own (`level` again anchors every building so)");
            }
            if (materials != g_materialsOurs) {
                g_materialsOurs = materials;
                SetGuerrillaValues(materials, "rfa_mods.ini saved; `level` again works every building's links out with them");
            }
        }
    }
    LeaveCriticalSection(&g_buildingsLock);
}

void InstallBuildings() {
    InitializeCriticalSection(&g_buildingsLock);
    const BYTE anchorOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x30};
    const BYTE resetOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14};
    if (memcmp(reinterpret_cast<const void*>(kCreateWorldAnchor), anchorOpening, sizeof(anchorOpening)) == 0)
        g_createWorldAnchor = reinterpret_cast<CreateWorldAnchorFn>(
            rfa::HookFunction(kCreateWorldAnchor, kCreateWorldAnchorPrologue, reinterpret_cast<void*>(&CreateWorldAnchor)));
    if (!g_createWorldAnchor) rfa::Log("Buildings: could not wrap the game's anchoring (its code isn't as expected); pieces hold to the ground as the game's own");
    {
        const BYTE damageOpening[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xB4, 0x00, 0x00, 0x00};
        if (memcmp(reinterpret_cast<const void*>(kPieceDamage), damageOpening, sizeof(damageOpening)) == 0)
            g_pieceDamage = reinterpret_cast<PieceDamageFn>(
                rfa::HookFunction(kPieceDamage, kPieceDamagePrologue, reinterpret_cast<void*>(&PieceDamaged)));
        if (!g_pieceDamage) rfa::Log("Buildings: could not wrap the game's damage to a piece (its code isn't as expected); hits on anchored pieces aren't logged");
        const BYTE contactOpening[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xF4, 0x00, 0x00, 0x00};
        const BYTE energyOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x44};
        if (memcmp(reinterpret_cast<const void*>(kContactStep), contactOpening, sizeof(contactOpening)) == 0 &&
            memcmp(reinterpret_cast<const void*>(kEnergyStep), energyOpening, sizeof(energyOpening)) == 0) {
            g_contactStep = reinterpret_cast<ContactStepFn>(rfa::HookFunction(kContactStep, sizeof(contactOpening), reinterpret_cast<void*>(&ContactStepCounted)));
            g_energyStep = reinterpret_cast<EnergyStepFn>(rfa::HookFunction(kEnergyStep, sizeof(energyOpening), reinterpret_cast<void*>(&EnergyStepLogged)));
        }
        const BYTE simpleOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x40};
        const BYTE complexOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x74};
        const BYTE collisionOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x3C};
        if (memcmp(reinterpret_cast<const void*>(kImpactSimple), simpleOpening, sizeof(simpleOpening)) == 0 &&
            memcmp(reinterpret_cast<const void*>(kImpactComplex), complexOpening, sizeof(complexOpening)) == 0 &&
            memcmp(reinterpret_cast<const void*>(kObjectCollision), collisionOpening, sizeof(collisionOpening)) == 0) {
            g_objectCollision = reinterpret_cast<ObjectCollisionFn>(
                rfa::HookFunction(kObjectCollision, sizeof(collisionOpening), reinterpret_cast<void*>(&ObjectCollisionCounted)));
            if (g_objectCollision) {  // the handlers' lines need its count
                g_impactSimple = reinterpret_cast<ImpactHandlerFn>(rfa::HookFunction(kImpactSimple, sizeof(simpleOpening), reinterpret_cast<void*>(&ImpactSimpleLogged)));
                g_impactComplex = reinterpret_cast<ImpactHandlerFn>(rfa::HookFunction(kImpactComplex, sizeof(complexOpening), reinterpret_cast<void*>(&ImpactComplexLogged)));
            }
        }
        if (!g_contactStep || !g_energyStep) rfa::Log("Car hits: could not wrap the game's contact or energy step; they aren't measured");
        const BYTE propOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x74};
        if (memcmp(reinterpret_cast<const void*>(kPropDamage), propOpening, sizeof(propOpening)) == 0 &&
            *reinterpret_cast<const DWORD*>(kPropVtable + 0xA4) == kPropDamage)
            g_propDamage = reinterpret_cast<PropDamageFn>(rfa::HookFunction(kPropDamage, kPropDamagePrologue, reinterpret_cast<void*>(&PropDamaged)));
        if (!g_propDamage)
            rfa::Log("Props: could not wrap the game's damage to a prop (its code isn't as expected); a melee hit on one takes the table's "
                     "building figure, and PropLog= does nothing");
    }
    if (memcmp(reinterpret_cast<const void*>(kResetLinkStrengths), resetOpening, sizeof(resetOpening)) == 0)
        g_resetLinkStrengths = reinterpret_cast<ResetLinkStrengthsFn>(
            rfa::HookFunction(kResetLinkStrengths, kResetLinkStrengthsPrologue, reinterpret_cast<void*>(&ResetLinkStrengths)));
    if (!g_resetLinkStrengths) rfa::Log("Buildings: could not watch the links' strengths being worked out; Guerrilla's materials are set each frame only");
    if (rfa::CodeValue(kDynamicLinkCheckAt) != kDynamicLinksTaken) rfa::Log("Buildings: the links between buildings can't be counted (the game's code isn't as expected)");
    const BYTE collectorOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18};
    if (memcmp(reinterpret_cast<const void*>(kSetAsAnchor), collectorOpening, sizeof(collectorOpening)) == 0)
        g_setAsAnchor = reinterpret_cast<SetAsAnchorFn>(rfa::HookFunction(kSetAsAnchor, kSetAsAnchorPrologue, reinterpret_cast<void*>(&SetAsAnchor)));
    if (!g_setAsAnchor) rfa::Log("Buildings: could not watch the game's anchoring by contact (its code isn't as expected); the log won't list it");
    LARGE_INTEGER rate;
    QueryPerformanceFrequency(&rate);
    g_tickRate = rate.QuadPart > 0 ? rate.QuadPart : 1;
    if (!g_watchdog) {
        g_watchdog = CreateThread(nullptr, 0, &Watchdog, nullptr, 0, nullptr);
        if (g_watchdog) SetThreadPriority(g_watchdog, THREAD_PRIORITY_ABOVE_NORMAL);  // to look while the game is busy
        else rfa::Log("Buildings: could not start the long frames' watchdog; the log won't say what a long frame was doing");
    }
    const BYTE sectionBeginOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x30};
    const BYTE sectionEndOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28};
    // Timed only for the Sections line (Log=1 and DestructionLog=1): their hooks run thousands of times a frame.
    if (rfa::DetailLog() && memcmp(reinterpret_cast<const void*>(kSectionBegin), sectionBeginOpening, sizeof(sectionBeginOpening)) == 0 &&
        memcmp(reinterpret_cast<const void*>(kSectionEnd), sectionEndOpening, sizeof(sectionEndOpening)) == 0 &&
        strcmp(reinterpret_cast<const char*>(kSimFrameName), "sim frame") == 0) {
        g_sectionEnd = reinterpret_cast<SectionEndFn>(rfa::HookFunction(kSectionEnd, kSectionEndPrologue, reinterpret_cast<void*>(&SectionEnd)));
        if (g_sectionEnd)
            g_sectionBegin = reinterpret_cast<SectionBeginFn>(rfa::HookFunction(kSectionBegin, kSectionBeginPrologue, reinterpret_cast<void*>(&SectionBegin)));
    }
    if (rfa::DetailLog() && (!g_sectionBegin || !g_sectionEnd))
        rfa::Log("Buildings: could not time the game's sections (its code isn't as expected); the log won't list them");
    g_buildingsReady = true;
}

// ---- Guerrilla's wind turbines
//
// A turbine is three movers from one chunk: its tower (an rfg_mover), its blades (an rfg_mover, held to the tower
// by a hinge that a motor turns: the world builder places them, GuerrillaConstraints) and its motor (a
// general_mover, the nacelle, whose death plays its electric death effect and the transformer explosion).
// Guerrilla gives them code of their own (custom_object_set_callbacks, RM 0xAD9670, by level object name):
// - The tower damaged (windmill_base_damaged_callback, 0xA52520; damage above 0): its motor is watched each frame
//   (windmill_motor_do_frame_callback, 0xA2C7D0), and once it moves at 1 m/s or more (it falls with the tower),
//   it is given 1,000 damage of the level-object kind and dies.
// - The blades damaged (windmill_blade_damaged_callback, 0xA522D0): a 3 s timer starts (again at each hit), and
//   each frame (0xA51C70) 1.25 s on the motor is given the same 1,000 damage (once), and at 3 s the blades'
//   constraints are destroyed: the hinge goes and the blades fall off.
// - The blades turned to a random angle as they load (windmill_blade_stream_in_callback): done by the world builder,
//   once, in the zone.
// Armageddon has the same callbacks per mover, set by its own custom_object_set_callbacks (0x7A4650; cdecl: the
// mover, a flag): by the kind its level object's name has in its own table (the name: the level object
// properties at [mover+0xB4], whose first field's first field is the name, as at 0x7A46E3). It has no turbines,
// so ours get its plain mover callbacks. Its slots are Guerrilla's one place later: +0xBC stream-in, +0xC0
// damaged, +0xC4 destroyed. A callback is cdecl: the event (its type at +0, the object's handle at +4; damage is
// type 0, with the share of damage at +8, Guerrilla's building_damaged_event) and a notify record, and returns an
// event_result (Armageddon's mover callback 0x7A4DF0: 2 when it acted, 0 when not).
// So, in our worlds, every mover's damaged and destroyed slots get ours, which first call the callback Armageddon gave
// it (kept by its handle, +0x68), then play Guerrilla's turbine code on a turbine's tower and blades (known by
// handle from the builder's list) and keep each building's state for the minimap (below). The timers and the motor watch run in the effects' frame update. The objects come from
// the world builder's list (mods\guerrilla_world\turbines.txt: zone, tower, blades, motor, hinge), by handle.
// Damage is the game's own (apply_damage, virtual +0xA4 in Armageddon, Guerrilla's +0x88 with three more
// arguments: Armageddon's own calls pass a scale of 1 and zeros, 0x7A21C1), so the motor dies as any other.
constexpr DWORD kSetMoverCallbacks = 0x7A4650;
constexpr size_t kSetMoverCallbacksPrologue = 6;  // push ebp / mov ebp,esp / sub esp,20h
constexpr size_t kMoverLevelObject = 0xB4, kMoverDamagedCallback = 0xC0, kMoverDestroyedCallback = 0xC4, kMoverHandle = 0x68;
constexpr size_t kMoverStreamedInCallback = 0xBC;  // (Guerrilla's on_stream_in_callback, one place earlier)
constexpr int kStreamedInEvent = 0x23;             // Armageddon's stream-in event (its mover callback 0x7A4A20; Guerrilla's 0x1E)
// A mover with this flag (its flags at +0xF0) gets no callbacks in either game: Armageddon's setup zeroes them and
// stops (0x7A46CE), as Guerrilla's does (mover_flags & 4, 0xAD9678). Seen on blades' pieces (their level object's
// name, made in play), so Guerrilla's turbine code leaves them alone too.
constexpr size_t kMoverCallbackFlags = 0xF0;
constexpr DWORD kMoverNoCallbacks = 4;
constexpr size_t kApplyDamageSlot = 0xA4 / 4;
constexpr int kDamagedEvent = 0, kDestroyedEvent = 2;  // the event types (Armageddon's mover callbacks 0x7A4DF0, 0x7A50E0)
constexpr int kLevelObjectDamage = 4;          // damaged_by_types DBT_LEVEL_OBJECT, as Guerrilla's turbine code gives
constexpr float kTurbineMotorDamage = 1000.0f;  // Guerrilla's
constexpr float kMotorMovingSpeed = 1.0f;       // m/s: Guerrilla's motor watch (its body's speed squared ≥ 1)
constexpr float kBladesMotorAt = 1.25f, kBladesFallAt = 3.0f;  // seconds after the blades' last hit (Guerrilla's 3 s timer, at 1.75 s left)

struct MoverEvent {
    int type;      // 0 damaged
    DWORD handle;  // the mover's
    float share;   // damage's share (damaged)
};
using MoverCallbackFn = int(__cdecl*)(const MoverEvent* event, void* notify);
using SetMoverCallbacksFn = bool(__cdecl*)(BYTE* mover, bool flag);
SetMoverCallbacksFn g_setMoverCallbacks;
using GameObjectFromHandleFn = BYTE*(__fastcall*)(void* world, void* unused, DWORD handle);
auto const GameObjectFromHandle = reinterpret_cast<GameObjectFromHandleFn>(0x6CB9E0);
using DestroyObjectFn = bool(__fastcall*)(void* world, void* unused, BYTE* object, bool now);
auto const DestroyGameObject = reinterpret_cast<DestroyObjectFn>(0x6D5450);
using ApplyDamageFn = bool(__fastcall*)(BYTE* object, void* unused, float amount, int kind, const float* position, const float* direction,
                                        const BYTE* attacker, DWORD weapon, float scale, int unknown, bool first, bool second);

struct Turbine {
    DWORD tower, blades, motor, hinge;
};
std::vector<Turbine> g_turbines;                              // from turbines.txt
// The damaged and destroyed callbacks the game gave each mover we wrap, by its handle: ours call them first.
struct MoverCallbacksBefore {
    MoverCallbackFn damaged, destroyed;
    MoverCallbackFn streamedIn;  // (only Guerrilla's custom objects' are wrapped)
};
std::unordered_map<DWORD, MoverCallbacksBefore> g_moverCallbacks;
struct MotorWatch {
    DWORD motor;
    float at[3];
    bool seen;
};
std::vector<MotorWatch> g_motorsWatched;  // motors whose tower was damaged (Guerrilla's motor watch)
struct BladesTimer {
    size_t turbine;
    float seconds;    // since the blades' last hit
    bool motorDone;   // the motor given its damage (Guerrilla zeroes the blades' hit points for it)
};
std::vector<BladesTimer> g_bladesTimers;
// Motors already given their damage: once is enough (the tower keeps taking damage as it falls, and Guerrilla's
// code would give a dead motor the same again). Forgotten when the turbine's tower is made again.
std::vector<DWORD> g_motorsKilled;

int TurbineOf(DWORD handle, bool blades) {
    for (size_t i = 0; i < g_turbines.size(); ++i)
        if ((blades ? g_turbines[i].blades : g_turbines[i].tower) == handle) return static_cast<int>(i);
    return -1;
}

// The motor given Guerrilla's 1,000 damage (it dies as any other: its death effects play).
bool MotorKilled(DWORD handle) { return std::find(g_motorsKilled.begin(), g_motorsKilled.end(), handle) != g_motorsKilled.end(); }

void KillTurbineMotor(DWORD handle, const char* why) {
    BYTE* motor = handle != 0xFFFFFFFF && !MotorKilled(handle) ? GameObjectFromHandle(kWorld, nullptr, handle) : nullptr;
    if (!motor) return;
    g_motorsKilled.push_back(handle);
    static const float kNoDirection[3] = {0, 0, 0};
    float at[3];
    memcpy(at, motor + 4, sizeof(at));
    auto const applyDamage = reinterpret_cast<ApplyDamageFn>((*reinterpret_cast<void** const*>(motor))[kApplyDamageSlot]);
    applyDamage(motor, nullptr, kTurbineMotorDamage, kLevelObjectDamage, at, kNoDirection, nullptr, 0xFFFFFFFF, 1.0f, 0, false, false);
    RFA_LOG_DETAIL_FIRST(40, "Wind turbines: motor %08lX given %.0f damage (%s)", handle, kTurbineMotorDamage, why);
}

// Guerrilla's turbine code on a damaged event (windmill_base_damaged_callback, windmill_blade_damaged_callback).
void TurbineDamaged(const MoverEvent& event) {
    if (const int i = TurbineOf(event.handle, false); i >= 0) {
        const DWORD motor = g_turbines[i].motor;
        if (event.share != 0 && motor != 0xFFFFFFFF && !MotorKilled(motor) &&
            std::none_of(g_motorsWatched.begin(), g_motorsWatched.end(), [&](const MotorWatch& w) { return w.motor == motor; }))
            g_motorsWatched.push_back({motor, {}, false});
    } else if (const int b = TurbineOf(event.handle, true); b >= 0) {
        auto timer = std::find_if(g_bladesTimers.begin(), g_bladesTimers.end(), [&](const BladesTimer& t) { return t.turbine == static_cast<size_t>(b); });
        if (timer == g_bladesTimers.end()) g_bladesTimers.push_back({static_cast<size_t>(b), 0.0f, false});
        else timer->seconds = 0.0f;  // each hit starts the 3 s again, as Guerrilla's
    }
}

void BuildingDamaged(const MoverEvent& event);    // (below, with the buildings for the minimap)
void BuildingDestroyed(const MoverEvent& event);
void ForgetBuildingStates();

void CustomObjectDamaged(const MoverEvent& event);  // (below, Guerrilla's custom objects)

// Each mover's damaged and destroyed callbacks in our worlds: the game's own first, then what we keep of it.
int __cdecl MoverDamaged(const MoverEvent* event, void* notify) {
    const auto found = event ? g_moverCallbacks.find(event->handle) : g_moverCallbacks.end();
    const int result = found != g_moverCallbacks.end() && found->second.damaged ? found->second.damaged(event, notify) : 0;
    if (!event || event->type != kDamagedEvent || !InOurWorld()) return result;
    BuildingDamaged(*event);
    TurbineDamaged(*event);
    CustomObjectDamaged(*event);
    return result;
}

// ---- Guerrilla's custom objects: effects that stop, objects that die when moved
//
// Guerrilla's custom object setup (custom_object_set_callbacks, RM 0xAD9670) gives some movers code of their own by
// their level object's name (each mover otherwise: its custom effects started when it streams in, unless it is
// killed, custom_effects_start_all, and stopped when it is destroyed). Armageddon starts and stops them the same
// way and has the same stop (0x7A5B80, cdecl: the mover; it stops the effects whose handles are at [mover+0xB4]+4
// on), but none of Guerrilla's kinds. In our worlds these are Guerrilla's, by the same names (as Guerrilla's level
// objects or ours, "rfg_" before them):
// - Smokestacks (smokestack_a, 0102smokestack_a, 0105smokestack_a): an rfg_mover stack smokes only while it has
//   taken no damage. Streamed in already damaged (its percent destroyed above 0) it doesn't start
//   (stop_effect_on_destroyable_stream_in_callback); its first damage stops it
//   (stop_effect_on_destroyable_damaged_callback). A general_mover stack keeps its smoke.
// - Vents and antennas (ind_vent_wall01, res_vent_roof_A, res_vent_wall_A, EDF_antenna): the same damage rule, and a
//   watch (stop_effect_on_move_do_frame_callback): once its body moves at all (or it is no longer a streamed
//   object), its effects (the vents' heat, the antennas' lights) stop and the watch ends.
// - PA speakers and portable comms (PA_speaker, edf_portable_comm_a): a watch (kill_on_move_do_frame_callback): once
//   its body moves at all, the object is given its level object's own hit points as damage of the level-object
//   kind (it dies, with its death effects) and the watch ends.
// - Fire barrels (tank_fire01_a): streamed in tipped over (its up axis less than 0.9 up) its fire doesn't start
//   (fire_barrel_stream_in_callback); else a watch (fire_barrel_do_frame_callback): while it is a streamed object,
//   once it leans past that (knocked over) its fire goes out 3 s later.
// - Power cores (EDFPower_Core): their effects follow their damage, effect_percent (variable 10 in both games): as
//   they stream in, 1 - their hit points left over their level object's (power_core_stream_in_callback); on each
//   damaged event, the event's share of damage (power_core_damaged_callback).
// - Mission computers (miss_comp_adv_A): of their three effects only the first and third show while no one uses
//   them (computer_stream_in_callback); destroyed, all three are taken away at once (computer_destroyed_callback).
//   Not yet: someone using one at an action node swaps the third for the second and back (computer_use_action_callback,
//   on Guerrilla's use event 0x33): Armageddon's mover use slot (+0xC8) is a stub and its use event isn't known.
// Not yet: the excavator (0101machine_big_mining_a_blade, _engine): its blade's human collision (damage, ragdoll), its
// motor's watch and the engine's death stopping the motor.
// The stream-in rules run after the game's own stream-in callback (wrapped, +0xBC, event 0x23), as Guerrilla's run in
// its stream-in callbacks.
// The watches run in the effects' frame update (TurbinesUpdate). Guerrilla's look at the object's body's speed as
// Armageddon has it too (havok_body_get_linear_velocity, 0x776740 here: moving is any speed at all) and at whether it
// is still a streamed object (obj_flags.streamed, +0x55 bit 0x10 in Guerrilla: once it isn't, a vent's effects stop
// and a barrel's watch ends). Armageddon's place for that flag isn't known (its object flags aren't all laid out as
// Guerrilla's), so here an object counts as streamed while it lasts, as one placed in a zone stays.
constexpr DWORD kStopCustomEffects = 0x7A5B80;
using StopCustomEffectsFn = void(__cdecl*)(BYTE* mover);
auto const StopCustomEffects = reinterpret_cast<StopCustomEffectsFn>(kStopCustomEffects);
bool g_stopOnMoveOn = false;  // the game's stop is as expected (checked at start-up)
enum class CustomKind { None, Smokestack, StopOnMove, KillOnMove, FireBarrel, PowerCore, Computer };
struct CustomName {
    const char* name;
    CustomKind kind;
};
const CustomName kCustomNames[] = {
    {"smokestack_a", CustomKind::Smokestack}, {"0102smokestack_a", CustomKind::Smokestack}, {"0105smokestack_a", CustomKind::Smokestack},
    {"ind_vent_wall01", CustomKind::StopOnMove}, {"res_vent_roof_A", CustomKind::StopOnMove}, {"res_vent_wall_A", CustomKind::StopOnMove},
    {"EDF_antenna", CustomKind::StopOnMove},     {"PA_speaker", CustomKind::KillOnMove},       {"edf_portable_comm_a", CustomKind::KillOnMove},
    {"tank_fire01_a", CustomKind::FireBarrel},   {"EDFPower_Core", CustomKind::PowerCore},     {"miss_comp_adv_A", CustomKind::Computer},
};
constexpr size_t kCustomObjectBody = 0x4C;     // an object's physics body's handle (-1: none)
constexpr size_t kMoverEffectHandles = 0x04;   // in a mover's record ([mover+0xB4]): its 3 custom effects' handles
constexpr int kMoverEffectCount = 3;
constexpr size_t kMoverHitpointsLeft = 0x1C;   // in that record: its hit points left (a whole number; Guerrilla's +0x24)
constexpr int kEffectPercentVariable = 10;     // effect_percent, in both games' vfx_expression_variables.xtbl
using SetEffectPercentFn = void(__fastcall*)(BYTE* effect, void* unused, int variable, float value);  // object_effect::set_expression_variable
auto const SetEffectPercent = reinterpret_cast<SetEffectPercentFn>(0x4F8250);
using BodyVelocityFn = void(__cdecl*)(DWORD body, float* velocity);  // 0x776740, Guerrilla's havok_body_get_linear_velocity
auto const BodyVelocity = reinterpret_cast<BodyVelocityFn>(0x776740);
constexpr float kFireBarrelUpright = 0.9f;     // Guerrilla's: its up axis's height at least this
constexpr float kFireBarrelOutAfter = 3.0f;    // seconds (Guerrilla's 3,000 ms timer)
constexpr size_t kObjectUpHeight = 0x20;       // an object's turn, rows from +0x10: its up axis's height
constexpr size_t kRfgMoverDestroyed = 0x13C;   // an rfg_mover's percent destroyed (0 to 1)
constexpr size_t kObjectKind = 0x7A, kObjectSubkind = 0x7B;  // an object's type (2 a mover) and subtype (1 an rfg_mover)
constexpr size_t kLevelObjectHitpoints = 0x44;  // a level object's Hitpoints (a whole number; Guerrilla's +0x4C)
struct CustomWatch {
    DWORD handle;
    CustomKind kind;
    float at[3];
    bool seen;
    float burning;  // a fire barrel's time leaning (below 0: not leaning)
};
std::vector<CustomWatch> g_customWatches;

// A mover's level object (its properties at [mover+0xB4], whose first field is the level object, whose first field
// is the name), or null.
const BYTE* MoverLevelObject(const BYTE* mover) {
    const BYTE* custom = *reinterpret_cast<BYTE* const*>(mover + kMoverLevelObject);
    return custom ? *reinterpret_cast<BYTE* const*>(custom) : nullptr;
}

const char* MoverLevelObjectName(const BYTE* mover) {
    const BYTE* levelObject = MoverLevelObject(mover);
    return levelObject ? *reinterpret_cast<const char* const*>(levelObject) : nullptr;
}

CustomKind CustomKindOf(const char* name) {
    if (!name) return CustomKind::None;
    if (_strnicmp(name, "rfg_", 4) == 0) name += 4;
    for (const CustomName& custom : kCustomNames)
        if (_stricmp(name, custom.name) == 0) return custom.kind;
    return CustomKind::None;
}

bool IsRfgMover(const BYTE* object) { return object[kObjectKind] == 2 && object[kObjectSubkind] == 1; }

const char* KindName(CustomKind kind) {
    switch (kind) {
        case CustomKind::Smokestack: return "smokestack";
        case CustomKind::StopOnMove: return "vent or antenna";
        case CustomKind::KillOnMove: return "PA speaker or comm";
        case CustomKind::FireBarrel: return "fire barrel";
        case CustomKind::PowerCore: return "power core";
        case CustomKind::Computer: return "computer";
        default: return "?";
    }
}

void StopObjectEffects(BYTE* mover, DWORD handle, CustomKind kind, const char* why) {
    StopCustomEffects(mover);
    const char* name = MoverLevelObjectName(mover);
    RFA_LOG_DETAIL_FIRST(60, "Effects: %s %08lX (%s): its effects stopped, %s, as Guerrilla's", name ? name : "?", handle, KindName(kind), why);
}

// A PA speaker or comm that moved: its own hit points as level-object damage, as Guerrilla's kill_on_move does.
void KillMovedObject(BYTE* mover, DWORD handle) {
    const BYTE* levelObject = MoverLevelObject(mover);
    const int hitpoints = levelObject ? *reinterpret_cast<const int*>(levelObject + kLevelObjectHitpoints) : 0;
    if (hitpoints <= 0 || hitpoints > 10000000) {
        RFA_LOG_FIRST(4, "Effects: %08lX moved, but its level object's hit points read %d; left alive", handle, hitpoints);
        return;
    }
    static const float kNoDirection[3] = {0, 0, 0};
    float at[3];
    memcpy(at, mover + 4, sizeof(at));
    auto const applyDamage = reinterpret_cast<ApplyDamageFn>((*reinterpret_cast<void** const*>(mover))[kApplyDamageSlot]);
    applyDamage(mover, nullptr, static_cast<float>(hitpoints), kLevelObjectDamage, at, kNoDirection, nullptr, 0xFFFFFFFF, 1.0f, 0, false, false);
    const char* name = MoverLevelObjectName(mover);
    RFA_LOG_DETAIL_FIRST(60, "Effects: %s %08lX moved: given its %d hit points of damage, as Guerrilla's kill on move", name ? name : "?", handle,
                         hitpoints);
}

// A mover's custom effects' handles: three at [mover+0xB4]+4 (Guerrilla's m_custom_effects, at +8 of its record).
DWORD* MoverEffectHandles(BYTE* mover) {
    BYTE* custom = *reinterpret_cast<BYTE**>(mover + kMoverLevelObject);
    return custom ? reinterpret_cast<DWORD*>(custom + kMoverEffectHandles) : nullptr;
}

// A power core's effects told how damaged it is (power_core_set_effect_variables: effect_percent, variable 10 in
// both games, on each of its custom effects).
void SetPowerCoreEffects(BYTE* mover, DWORD handle, float percent, const char* why) {
    DWORD* handles = MoverEffectHandles(mover);
    if (!handles) return;
    for (int i = 0; i < kMoverEffectCount; ++i)
        if (handles[i] != 0xFFFFFFFF)
            if (BYTE* effect = ObjectFromHandle(kWorld, nullptr, handles[i])) SetEffectPercent(effect, nullptr, kEffectPercentVariable, percent);
    const char* name = MoverLevelObjectName(mover);
    RFA_LOG_DETAIL_FIRST(40, "Effects: %s %08lX (power core): its effects' effect_percent %.2f, %s, as Guerrilla's", name ? name : "?", handle, percent, why);
}

// A computer's effect taken away at once (Guerrilla's computer code destroys them, not fades them).
void DestroyMoverEffect(BYTE* mover, int slot) {
    DWORD* handles = MoverEffectHandles(mover);
    if (!handles || handles[slot] == 0xFFFFFFFF) return;
    if (BYTE* effect = ObjectFromHandle(kWorld, nullptr, handles[slot])) DestroyGameObject(kWorld, nullptr, effect, false);
    handles[slot] = 0xFFFFFFFF;
}

// On a mover's damaged event in our worlds: a smokestack, vent or antenna that is an rfg_mover has its effects
// stopped at its first damage (stop_effect_on_destroyable_damaged_callback); a power core's effects take the damage's
// share (power_core_damaged_callback).
void CustomObjectDamaged(const MoverEvent& event) {
    auto watch = std::find_if(g_customWatches.begin(), g_customWatches.end(), [&](const CustomWatch& w) { return w.handle == event.handle; });
    if (watch == g_customWatches.end()) return;
    BYTE* mover = GameObjectFromHandle(kWorld, nullptr, event.handle);
    if (!mover) return;
    if (watch->kind == CustomKind::PowerCore) {
        SetPowerCoreEffects(mover, event.handle, event.share, "damaged");
        return;
    }
    if ((watch->kind != CustomKind::Smokestack && watch->kind != CustomKind::StopOnMove) || !IsRfgMover(mover)) return;
    StopObjectEffects(mover, event.handle, watch->kind, "at its first damage");
    g_customWatches.erase(watch);
}

// On a mover's destroyed event in our worlds, before the game's own: a computer's effects are taken away at once
// (computer_destroyed_callback), where the game's would fade them.
void CustomObjectDestroyed(const MoverEvent& event) {
    auto watch = std::find_if(g_customWatches.begin(), g_customWatches.end(), [&](const CustomWatch& w) { return w.handle == event.handle; });
    if (watch == g_customWatches.end() || watch->kind != CustomKind::Computer) return;
    if (BYTE* mover = GameObjectFromHandle(kWorld, nullptr, event.handle)) {
        for (int i = 0; i < kMoverEffectCount; ++i) DestroyMoverEffect(mover, i);
        RFA_LOG_DETAIL_FIRST(20, "Effects: computer %08lX destroyed: its effects taken away, as Guerrilla's", event.handle);
    }
    g_customWatches.erase(watch);
}

// On a mover's stream-in event in our worlds, after the game's own (which starts its custom effects, as Guerrilla's
// custom_effects_start_all): Guerrilla's stream-in callbacks for its kinds, and their watches begin.
void CustomObjectStreamedIn(DWORD handle) {
    auto watch = std::find_if(g_customWatches.begin(), g_customWatches.end(), [&](const CustomWatch& w) { return w.handle == handle; });
    if (watch == g_customWatches.end()) return;
    BYTE* mover = GameObjectFromHandle(kWorld, nullptr, handle);
    if (!mover) return;
    watch->seen = true;
    watch->burning = -1;
    const float upHeight = *reinterpret_cast<const float*>(mover + kObjectUpHeight);
    bool done = false;
    switch (watch->kind) {
        case CustomKind::Smokestack:
            // stop_effect_on_destroyable_stream_in_callback: an rfg_mover stack already damaged doesn't smoke. (Its first
            // damage is watched from its damaged event; a general_mover stack keeps its smoke.)
            if (IsRfgMover(mover) && *reinterpret_cast<const float*>(mover + kRfgMoverDestroyed) > 0)
                StopObjectEffects(mover, handle, watch->kind, "streamed in damaged");
            done = !IsRfgMover(mover) || *reinterpret_cast<const float*>(mover + kRfgMoverDestroyed) > 0;
            break;
        case CustomKind::FireBarrel:
            // fire_barrel_stream_in_callback: tipped over, no fire (and no watch).
            if (upHeight < kFireBarrelUpright) {
                StopObjectEffects(mover, handle, watch->kind, "streamed in tipped over");
                done = true;
            }
            break;
        case CustomKind::PowerCore: {
            // power_core_stream_in_callback: 1 - its hit points left over its level object's, held to 0-1. (Its watch
            // stays, for its damaged events.)
            const BYTE* custom = *reinterpret_cast<BYTE* const*>(mover + kMoverLevelObject);
            const BYTE* levelObject = MoverLevelObject(mover);
            const int most = levelObject ? *reinterpret_cast<const int*>(levelObject + kLevelObjectHitpoints) : 0;
            if (custom && most > 0) {
                const float left = static_cast<float>(*reinterpret_cast<const int*>(custom + kMoverHitpointsLeft)) / static_cast<float>(most);
                SetPowerCoreEffects(mover, handle, std::clamp(1 - left, 0.0f, 1.0f), "streamed in");
            }
            break;
        }
        case CustomKind::Computer:
            // computer_stream_in_callback starts its first and third effects only: its second (the screen of someone
            // using it) waits for a user. (Its watch stays, for its destroyed event.)
            DestroyMoverEffect(mover, 1);
            RFA_LOG_DETAIL_FIRST(20, "Effects: computer %08lX streamed in: its second effect taken away (no one using it), as Guerrilla's", handle);
            break;
        default:
            break;
    }
    if (done) g_customWatches.erase(watch);
}

// The movers' stream-in callbacks in our worlds, for Guerrilla's custom objects: the game's own first, then theirs.
int __cdecl MoverStreamedIn(const MoverEvent* event, void* notify) {
    const auto found = event ? g_moverCallbacks.find(event->handle) : g_moverCallbacks.end();
    const int result = found != g_moverCallbacks.end() && found->second.streamedIn ? found->second.streamedIn(event, notify) : 0;
    if (event && event->type == kStreamedInEvent && InOurWorld()) CustomObjectStreamedIn(event->handle);
    return result;
}

// Each frame in our worlds: Guerrilla's watches.
void WatchCustomObjects(float seconds) {
    for (size_t k = 0; k < g_customWatches.size();) {
        CustomWatch& watch = g_customWatches[k];
        BYTE* mover = GameObjectFromHandle(kWorld, nullptr, watch.handle);
        if (!mover) {
            g_customWatches.erase(g_customWatches.begin() + k);
            continue;
        }
        const float upHeight = *reinterpret_cast<const float*>(mover + kObjectUpHeight);
        bool done = false;
        if (!watch.seen) {  // (watched from its stream-in, as Guerrilla's watches start: CustomObjectStreamedIn)
            ++k;
            continue;
        }
        {
            // Its body's speed, as Guerrilla's watches read it (havok_body_get_linear_velocity; "moving" is any speed
            // at all, vector::is_null false), or none without a body.
            const bool wantsSpeed = watch.kind == CustomKind::StopOnMove || watch.kind == CustomKind::KillOnMove;
            const DWORD body = wantsSpeed ? *reinterpret_cast<const DWORD*>(mover + kCustomObjectBody) : 0xFFFFFFFF;
            float velocity[3] = {};
            if (body != 0xFFFFFFFF) BodyVelocity(body, velocity);
            const bool moving = body != 0xFFFFFFFF && (velocity[0] != 0 || velocity[1] != 0 || velocity[2] != 0);
            switch (watch.kind) {
                case CustomKind::StopOnMove:
                    // (Guerrilla's: with no body, its effects stop at once.)
                    if (body == 0xFFFFFFFF || moving) {
                        StopObjectEffects(mover, watch.handle, watch.kind, body == 0xFFFFFFFF ? "it has no body" : "its body moved");
                        done = true;
                    }
                    break;
                case CustomKind::KillOnMove:
                    // (Guerrilla's: with no body, the watch ends and it lives.)
                    if (moving) KillMovedObject(mover, watch.handle);
                    done = body == 0xFFFFFFFF || moving;
                    break;
                case CustomKind::FireBarrel:
                    // (Guerrilla ends this watch once the barrel isn't a streamed object; knocked over it still is,
                    // so its fire goes out 3 s after it leans. Here the watch ends with the barrel.)
                    if (watch.burning >= 0 || upHeight < kFireBarrelUpright) {
                        if (watch.burning < 0) watch.burning = 0;
                        watch.burning += seconds;
                        if (watch.burning >= kFireBarrelOutAfter) {
                            StopObjectEffects(mover, watch.handle, watch.kind, "3 s after it leaned over");
                            done = true;
                        }
                    }
                    break;
                default:
                    break;
            }
        }
        if (done) g_customWatches.erase(g_customWatches.begin() + k);
        else ++k;
    }
}

int __cdecl MoverDestroyed(const MoverEvent* event, void* notify) {
    if (event && event->type == kDestroyedEvent && InOurWorld()) CustomObjectDestroyed(*event);
    const auto found = event ? g_moverCallbacks.find(event->handle) : g_moverCallbacks.end();
    const int result = found != g_moverCallbacks.end() && found->second.destroyed ? found->second.destroyed(event, notify) : 0;
    if (event && event->type == kDestroyedEvent && InOurWorld()) BuildingDestroyed(*event);
    return result;
}

// Each mover given its callbacks: in our worlds, its damaged and destroyed ones wrapped by ours (kept by its handle),
// for Guerrilla's turbine code and the buildings' state on the minimap. A mover the game gave none keeps none, as in
// both games.
bool __cdecl SetMoverCallbacks(BYTE* mover, bool flag) {
    const bool result = g_setMoverCallbacks(mover, flag);
    if (!mover || !InOurWorld()) return result;
    if (*reinterpret_cast<const DWORD*>(mover + kMoverCallbackFlags) & kMoverNoCallbacks) return result;  // none in either game
    // Guerrilla's custom objects (see WatchCustomObjects): watched from their setup, as Guerrilla's setup does.
    if (const CustomKind kind = g_stopOnMoveOn ? CustomKindOf(MoverLevelObjectName(mover)) : CustomKind::None; kind != CustomKind::None) {
        const DWORD watched = *reinterpret_cast<const DWORD*>(mover + kMoverHandle);
        if (std::none_of(g_customWatches.begin(), g_customWatches.end(), [&](const CustomWatch& w) { return w.handle == watched; }))
            g_customWatches.push_back({watched, kind, {}, false, -1.0f});
        // Its stream-in callback wrapped, for Guerrilla's stream-in callbacks after the game's own (see MoverStreamedIn).
        auto& streamedIn = *reinterpret_cast<MoverCallbackFn*>(mover + kMoverStreamedInCallback);
        if (streamedIn != &MoverStreamedIn) g_moverCallbacks[watched].streamedIn = streamedIn;
        if (streamedIn) streamedIn = &MoverStreamedIn;
        else CustomObjectStreamedIn(watched);  // (no stream-in callback of the game's: its watch begins now)
        RFA_LOG_FIRST(1, "Effects: Guerrilla's smokestacks, vents, antennas, PA speakers, comms, fire barrels, power cores and computers act as "
                         "Guerrilla's in our worlds");
    }
    auto& damaged = *reinterpret_cast<MoverCallbackFn*>(mover + kMoverDamagedCallback);
    auto& destroyed = *reinterpret_cast<MoverCallbackFn*>(mover + kMoverDestroyedCallback);
    if (!damaged && !destroyed) return result;
    const DWORD handle = *reinterpret_cast<const DWORD*>(mover + kMoverHandle);
    MoverCallbacksBefore& before = g_moverCallbacks[handle];
    if (damaged != &MoverDamaged) before.damaged = damaged;
    if (destroyed != &MoverDestroyed) before.destroyed = destroyed;
    if (damaged) damaged = &MoverDamaged;
    if (destroyed) destroyed = &MoverDestroyed;
    // A turbine made again (its zone loaded again): its motor may be given its damage again.
    if (const int i = TurbineOf(handle, false); i >= 0)
        g_motorsKilled.erase(std::remove(g_motorsKilled.begin(), g_motorsKilled.end(), g_turbines[i].motor), g_motorsKilled.end());
    RFA_LOG_FIRST(1, "Buildings: their damaged and destroyed callbacks watched in our worlds (Guerrilla's turbine code, the minimap's buildings)");
    return result;
}

// Each frame in our worlds: the motors watched, and the blades' timers (none kept in any other world).
void TurbinesUpdate(float seconds) {
    if (!InOurWorld()) {
        g_motorsWatched.clear();
        g_bladesTimers.clear();
        g_motorsKilled.clear();
        g_customWatches.clear();
        ForgetBuildingStates();
        return;
    }
    if (seconds <= 0) return;
    WatchCustomObjects(seconds);
    for (size_t k = 0; k < g_motorsWatched.size();) {
        MotorWatch& watch = g_motorsWatched[k];
        BYTE* motor = GameObjectFromHandle(kWorld, nullptr, watch.motor);
        if (!motor) {
            g_motorsWatched.erase(g_motorsWatched.begin() + k);
            continue;
        }
        const float* at = reinterpret_cast<const float*>(motor + 4);
        const float dx = at[0] - watch.at[0], dy = at[1] - watch.at[1], dz = at[2] - watch.at[2];
        if (watch.seen && dx * dx + dy * dy + dz * dz >= kMotorMovingSpeed * kMotorMovingSpeed * seconds * seconds) {
            KillTurbineMotor(watch.motor, "it fell with its damaged tower");
            g_motorsWatched.erase(g_motorsWatched.begin() + k);
            continue;
        }
        memcpy(watch.at, at, sizeof(watch.at));
        watch.seen = true;
        ++k;
    }
    for (size_t k = 0; k < g_bladesTimers.size();) {
        BladesTimer& timer = g_bladesTimers[k];
        const Turbine& turbine = g_turbines[timer.turbine];
        timer.seconds += seconds;
        if (timer.seconds >= kBladesMotorAt && !timer.motorDone) {
            timer.motorDone = true;
            KillTurbineMotor(turbine.motor, "its blades were hit");
        }
        if (timer.seconds >= kBladesFallAt) {
            if (BYTE* hinge = GameObjectFromHandle(kWorld, nullptr, turbine.hinge)) {
                DestroyGameObject(kWorld, nullptr, hinge, false);
                RFA_LOG_DETAIL_FIRST(40, "Wind turbines: blades %08lX come off (their hinge %08lX taken away)", turbine.blades, turbine.hinge);
            }
            g_bladesTimers.erase(g_bladesTimers.begin() + k);
            continue;
        }
        ++k;
    }
}

void LoadBuildingList();  // (below, with the buildings for the minimap)

void InstallTurbines() {
    FILE* file = nullptr;
    if (_wfopen_s(&file, (rfa::RootDir() + L"\\mods\\guerrilla_world\\turbines.txt").c_str(), L"r") == 0 && file) {
        char line[256], zone[64];
        while (fgets(line, sizeof(line), file)) {
            unsigned long tower, blades, motor, hinge;
            if (sscanf_s(line, "%63s %lx %lx %lx %lx", zone, static_cast<unsigned>(sizeof(zone)), &tower, &blades, &motor, &hinge) == 5)
                g_turbines.push_back({tower, blades, motor, hinge});
        }
        fclose(file);
    }
    LoadBuildingList();
    const BYTE stopOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08};
    g_stopOnMoveOn = memcmp(reinterpret_cast<const void*>(kStopCustomEffects), stopOpening, sizeof(stopOpening)) == 0;
    if (!g_stopOnMoveOn)
        rfa::Log("Effects: the game's stop for an object's effects isn't as expected; Guerrilla's smokestacks, vents, antennas, PA speakers, comms "
                 "and fire barrels act as Armageddon's");
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20};
    if (memcmp(reinterpret_cast<const void*>(kSetMoverCallbacks), opening, sizeof(opening)) == 0)
        g_setMoverCallbacks = reinterpret_cast<SetMoverCallbacksFn>(
            rfa::HookFunction(kSetMoverCallbacks, kSetMoverCallbacksPrologue, reinterpret_cast<void*>(&SetMoverCallbacks)));
    if (!g_setMoverCallbacks) {
        g_turbines.clear();
        rfa::Log("Wind turbines: could not hook the movers' callbacks (the game's code isn't as expected); Guerrilla's turbine code and the "
                 "buildings' damage on the minimap are off");
        return;
    }
    if (g_turbines.empty()) rfa::Log("Wind turbines: no mods\\guerrilla_world\\turbines.txt (build the world); Guerrilla's turbine code is off");
    else
        rfa::Log("Wind turbines: %u read from mods\\guerrilla_world\\turbines.txt; Guerrilla's turbine code plays on them in our worlds",
                 static_cast<unsigned>(g_turbines.size()));
}

// ---- Guerrilla's props' balance point
//
// A loose prop (a general_mover made from one of its chunk file's general objects: barrels, crates, chairs) gets its
// body's mass properties from that general object's box, which both games' files keep in the file's own scene. Guerrilla's
// blue barrel (tank_empty01_a) stands on that scene's floor: its box 0.00 to 0.98 m up, its own origin (the record's place)
// at 0.49 m, its middle. Guerrilla's havok_body_calc_mass_props for a box (RM 0x80E3D0, called by general_mover::stream)
// gives a solid box's inertia and the centre of mass at the body's own origin: zero. Armageddon's (0x776A20, cdecl: from,
// to, mass, the centre of mass and the inertia out) gives the same inertia but the centre of mass at the box's middle, and
// its general mover's body (0x8CCEB0, vtable 0xFCE88C +0x18: thiscall, one byte, ret 4) keeps it for a general object (a
// broken-off piece's is worked out again after). Armageddon's own props sit at their scene's origin, so the box's middle
// is theirs; Guerrilla's are off by their record's place: the blue barrel's balance point came 0.49 m above its middle, at
// its lid, the fire barrel's (tank_fire01_a) and the explosive one's (tank_Explosive01_A) just above theirs, so a knock
// turned each over onto its lid, where it stayed (the user's 2026-11-19 run). In our worlds a general mover's body has
// Guerrilla's: the centre of mass at its origin, the rest as the game works it out.
constexpr DWORD kGeneralMoverBody = 0x8CCEB0;
constexpr size_t kGeneralMoverBodyPrologue = 9;  // push ebp / mov ebp,esp / sub esp,1F8h
constexpr DWORD kGeneralMoverBodySlot = 0x18;    // in the general movers' vtable (kPropVtable)
constexpr DWORD kBoxMassProperties = 0x776A20;
constexpr size_t kBoxMassPropertiesPrologue = 6;  // push ebp / mov ebp,esp / sub esp,34h
using GeneralMoverBodyFn = bool(__fastcall*)(BYTE* mover, void* unused, bool flag);
using BoxMassPropertiesFn = void(__cdecl*)(const float* from, const float* to, float mass, float* centre, float* inertia);
GeneralMoverBodyFn g_generalMoverBody;
BoxMassPropertiesFn g_boxMassProperties;
thread_local BYTE* t_bodyOf = nullptr;  // the general mover in our worlds whose body is being made
volatile LONG g_balanceLines = 0;

bool __fastcall GeneralMoverBody(BYTE* mover, void* unused, bool flag) {
    BYTE* const before = t_bodyOf;
    t_bodyOf = InOurWorld() ? mover : nullptr;
    const bool made = g_generalMoverBody(mover, unused, flag);
    t_bodyOf = before;
    return made;
}

// Its level object's name, read with care (the body is made as it streams in) into `name`, or false.
bool PropName(const BYTE* mover, char (&name)[64]) {
    const BYTE* custom = nullptr;
    const BYTE* levelObject = nullptr;
    const char* text = nullptr;
    if (!ReadGame(mover + kMoverLevelObject, custom) || !custom || !ReadGame(custom, levelObject) || !levelObject ||
        !ReadGame(levelObject, text) || !text)
        return false;
    for (size_t i = 0; i < sizeof(name); ++i)
        if (!ReadGame(text + i, name[i]) || name[i] == '\0') {
            name[i] = '\0';
            return i > 0;
        }
    name[sizeof(name) - 1] = '\0';
    return true;
}

void __cdecl BoxMassProperties(const float* from, const float* to, float mass, float* centre, float* inertia) {
    g_boxMassProperties(from, to, mass, centre, inertia);
    const BYTE* const mover = t_bodyOf;
    if (!mover || !centre) return;
    if (InterlockedIncrement(&g_balanceLines) <= 8) {
        char name[64];
        rfa::LogDetail("Props: %s (%.0f kg): its balance point at its origin, as Guerrilla's (Armageddon's: its box's middle, %.2f %.2f %.2f m off)",
                       PropName(mover, name) ? name : "a prop", mass, centre[0], centre[1], centre[2]);
    }
    centre[0] = centre[1] = centre[2] = 0;
}

void InstallPropBalance() {
    const BYTE bodyOpening[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xF8, 0x01, 0x00, 0x00};
    const BYTE massOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x34};
    if (memcmp(reinterpret_cast<const void*>(kGeneralMoverBody), bodyOpening, sizeof(bodyOpening)) == 0 &&
        memcmp(reinterpret_cast<const void*>(kBoxMassProperties), massOpening, sizeof(massOpening)) == 0 &&
        *reinterpret_cast<const DWORD*>(kPropVtable + kGeneralMoverBodySlot) == kGeneralMoverBody) {
        g_boxMassProperties = reinterpret_cast<BoxMassPropertiesFn>(
            rfa::HookFunction(kBoxMassProperties, kBoxMassPropertiesPrologue, reinterpret_cast<void*>(&BoxMassProperties)));
        if (g_boxMassProperties)
            g_generalMoverBody = reinterpret_cast<GeneralMoverBodyFn>(
                rfa::HookFunction(kGeneralMoverBody, kGeneralMoverBodyPrologue, reinterpret_cast<void*>(&GeneralMoverBody)));
    }
    if (g_generalMoverBody) rfa::Log("Props: in our worlds a loose prop's balance point is its origin, as Guerrilla's (Armageddon's: its box's middle)");
    else rfa::Log("Props: could not wrap the making of a prop's body (the game's code isn't as expected); props balance as Armageddon's");
}

// ---- The buildings, for Guerrilla's minimap (for guerrilla_hud)
//
// Guerrilla's minimap (cartographer) draws the buildings near it: once a second it collects every mover in a 250 m
// box round the map's middle (collect_structures, RM 0x872E80). It draws each as its chunk's top-down picture
// ("<chunk>_thumb", render_masked_structures 0x86E43A), sized by the mover's box (get_bbox) and turned with it,
// coloured by its team (get_structure_color 0x862850). On the EDF's buildings of medium or high importance it puts
// IMPORTANCE_MEDIUM/HIGH icons (get_structure_importance_icon 0x862D30). Next to those that took damage in the last
// 3 s it puts their health, 1 - min(1, damage / game_destroyed_pct) (render_structures_post 0x86FF50, 0x87D436).
// The damage is the figure each building's damaged event carries: Guerrilla posts it once a frame for each mover
// damaged that frame (object_mover_post_damaged_destroyed_events 0xA5F0D0: type 0, the handle, the mover's
// damage), and its destroyed event (type 2) once.
// The world builder lists every mover with what doesn't change in play (GuerrillaBuildingList:
// mods\guerrilla_world\buildings.txt). The game gives the rest: where it is and how it is turned (the object at +4
// and +0x10), its box (get_bbox, virtual +8: the object's own +0x7C and +0x88), and through the movers' callbacks
// (above) its damage and when, and whether it was destroyed. GuerrillaWorld_Buildings hands the HUD the loaded ones
// near a point.
struct ListedBuilding {
    DWORD handle, parent;
    char chunk[64];
    int importance, team;
    float destroyedAt;
    float x, z;
};
std::vector<ListedBuilding> g_buildingList;  // from buildings.txt
struct BuildingState {
    DWORD damagedAt;  // GetTickCount of its last damaged event (0 none)
    float damage;     // that event's figure
    bool destroyed;
};
std::unordered_map<DWORD, BuildingState> g_buildingStates;  // by handle, in our worlds
SRWLOCK g_buildingStatesLock = SRWLOCK_INIT;                 // the events come on the game's thread; the HUD may ask from its own

void LoadBuildingList() {
    FILE* file = nullptr;
    if (_wfopen_s(&file, (rfa::RootDir() + L"\\mods\\guerrilla_world\\buildings.txt").c_str(), L"r") != 0 || !file) {
        rfa::Log("Buildings: no mods\\guerrilla_world\\buildings.txt (build the world); the minimap gets no buildings");
        return;
    }
    char line[512], zone[64];
    while (fgets(line, sizeof(line), file)) {
        ListedBuilding b = {};
        unsigned long handle, parent;
        float y;
        if (sscanf_s(line, "%63s %lx %lx %63s %d %d %f %f %f %f", zone, static_cast<unsigned>(sizeof(zone)), &handle, &parent, b.chunk,
                     static_cast<unsigned>(sizeof(b.chunk)), &b.importance, &b.team, &b.destroyedAt, &b.x, &y, &b.z) == 10) {
            b.handle = handle;
            b.parent = parent;
            g_buildingList.push_back(b);
        }
    }
    fclose(file);
    rfa::Log("Buildings: %u read from mods\\guerrilla_world\\buildings.txt, for Guerrilla's minimap", static_cast<unsigned>(g_buildingList.size()));
}

void BuildingDamaged(const MoverEvent& event) {
    AcquireSRWLockExclusive(&g_buildingStatesLock);
    BuildingState& state = g_buildingStates[event.handle];
    state.damagedAt = GetTickCount() | 1;
    state.damage = event.share;
    ReleaseSRWLockExclusive(&g_buildingStatesLock);
}

void BuildingDestroyed(const MoverEvent& event) {
    AcquireSRWLockExclusive(&g_buildingStatesLock);
    g_buildingStates[event.handle].destroyed = true;
    ReleaseSRWLockExclusive(&g_buildingStatesLock);
}

void ForgetBuildingStates() {
    AcquireSRWLockExclusive(&g_buildingStatesLock);
    if (!g_buildingStates.empty()) g_buildingStates.clear();
    ReleaseSRWLockExclusive(&g_buildingStatesLock);
}

// ---- Cutting buildings where something charges through them (for other mods)
//
// Guerrilla's Rhino backpack charge (multi_object_backpack::process, RM 0x9F358B), on each step it hits a mover,
// makes a shape cutter there: shape_cutter_props(the hit's place plus 0.475 × the charge's direction, the player's
// turn, radius 0.95, the explosion mp_rhino_cutter, the player's handle as source and owner), then
// world::create_object_internal. A shape cutter is an object the game makes and processes once
// (shape_cutter_do_frame): it cuts the buildings' pieces inside its radius away and, with an explosion, pushes and
// damages what's there as that explosion does.
// Armageddon has the same object, made the same way. Its console command shape_cutter_test (0x694340) makes one
// with 0x6944E0 (shape_cutter_props: thiscall, the props block; the place, the turn, the radius (outer_radius), the
// explosion (exp_info), an explosion direction or none (exp_dir), the source and the owner; ret 1Ch; it also sets
// cause_explosions), 0x695F40 (thiscall on the world: the props and the zone, -1; returns the object) and 0x694580
// (the props block freed). Guerrilla's exp_info is the explosion's byte id; Armageddon's is the explosion_info
// itself (the cutter keeps it at +0xA0 and reads it as one), found by name with 0x6EAE30 (cdecl, Guerrilla's
// explosion_lookup).
//
// GuerrillaWorld_CutBuildings (world_api.h) is for the parts that make such things (guerrilla_gear's Rhino charge). Call
// it on the game's own thread, as the charge does, once per step it touches a building.
constexpr DWORD kShapeCutterProps = 0x6944E0, kMakeShapeCutter = 0x695F40, kFreeObjectProps = 0x694580, kExplosionLookup = 0x6EAE30;
using ShapeCutterPropsFn = void*(__fastcall*)(void* props, void* unused, const float* position, const float* turn, float radius, const void* explosion,
                                             const float* explosionDirection, DWORD source, DWORD owner);
using MakeShapeCutterFn = BYTE*(__fastcall*)(void* world, void* unused, void* props, int zone);
using FreeObjectPropsFn = void(__fastcall*)(void* props, void* unused);
using ExplosionLookupFn = const void*(__cdecl*)(const char* name);

}  // namespace

// Cuts the buildings' pieces within `radius` metres of `position` away, as Guerrilla's shape cutter does.
// - position: x, y, z.
// - turn: the cutter's 3 × 3 rotation, rows right, up, forward (the game's matrix), as Guerrilla passes the
//   charging player's.
// - radius: metres (Guerrilla's charge: 0.95).
// - explosion: an explosion's name in the explosion table, to push and damage what's there as it does
//   (Guerrilla's charge: "mp_rhino_cutter"), or null for a cut alone.
// - source: who made it, by handle (the player), also kept as its owner; 0xFFFFFFFF for none.
// Returns true if the cutter was made.
bool GuerrillaWorld_CutBuildings(const float* position, const float* turn, float radius, const char* explosion, DWORD source) {
    if (!position || !turn || !(radius > 0) || !guerrilla_world::Running()) return false;
    static std::string lastName;
    static const void* lastExplosion = nullptr;
    const void* info = nullptr;
    if (explosion && *explosion) {
        if (lastName != explosion) {
            lastName = explosion;
            lastExplosion = reinterpret_cast<ExplosionLookupFn>(kExplosionLookup)(explosion);
            if (!lastExplosion) RFA_LOG_FIRST(10, "Cutting buildings: no explosion %s in the game's table; cutting without one", explosion);
        }
        info = lastExplosion;
    }
    alignas(16) BYTE props[64] = {};  // an obj_prop_block (12 bytes in both games), with room to spare
    reinterpret_cast<ShapeCutterPropsFn>(kShapeCutterProps)(props, nullptr, position, turn, radius, info, nullptr, source, source);
    BYTE* cutter = reinterpret_cast<MakeShapeCutterFn>(kMakeShapeCutter)(kWorld, nullptr, props, -1);
    reinterpret_cast<FreeObjectPropsFn>(kFreeObjectProps)(props, nullptr);
    RFA_LOG_DETAIL_FIRST(10, "Cutting buildings: a cutter of %.2f m at %.1f %.1f %.1f (explosion %s)%s", radius, position[0], position[1], position[2],
                  info ? explosion : "none", cutter ? "" : ": the game made none");
    return cutter != nullptr;
}

// (A building's struct, GuerrillaWorldBuilding, is in world_api.h.)

// The loaded buildings whose listed place is within a square of half-width `halfSize` metres round (x, z) (Guerrilla
// collects a 250 m box once a second), at most `most` of them into `out`. Returns how many it wrote. In a world
// that isn't ours, none. Call it on the game's thread.
int GuerrillaWorld_Buildings(float x, float z, float halfSize, GuerrillaWorldBuilding* out, int most) {
    if (!out || most <= 0 || !guerrilla_world::Running() || !InOurWorld()) return 0;
    using GetBoxFn = void(__fastcall*)(BYTE* object, void* unused, float* boundsMin, float* boundsMax);
    const DWORD now = GetTickCount();
    int count = 0;
    AcquireSRWLockShared(&g_buildingStatesLock);
    for (const ListedBuilding& listed : g_buildingList) {
        if (count >= most) break;
        if (fabsf(listed.x - x) > halfSize || fabsf(listed.z - z) > halfSize) continue;
        BYTE* mover = GameObjectFromHandle(kWorld, nullptr, listed.handle);
        if (!mover) continue;
        GuerrillaWorldBuilding& b = out[count++];
        b.handle = listed.handle;
        b.parent = listed.parent;
        memcpy(b.chunk, listed.chunk, sizeof(b.chunk));
        b.importance = listed.importance;
        b.team = listed.team;
        b.destroyedAt = listed.destroyedAt;
        memcpy(b.position, mover + 4, sizeof(b.position));
        memcpy(b.turn, mover + 0x10, sizeof(b.turn));
        reinterpret_cast<GetBoxFn>((*reinterpret_cast<void** const*>(mover))[2])(mover, nullptr, b.boundsMin, b.boundsMax);
        const auto state = g_buildingStates.find(listed.handle);
        const bool known = state != g_buildingStates.end();
        b.damage = known ? state->second.damage : 0.0f;
        b.msSinceDamaged = known && state->second.damagedAt ? static_cast<int>(now - state->second.damagedAt) : -1;
        b.destroyed = known && state->second.destroyed ? 1 : 0;
    }
    ReleaseSRWLockShared(&g_buildingStatesLock);
    return count;
}

namespace {

// ---- Guerrilla's ladders
//
// A ladder is an invisible volume on a building that the player and people climb (the world builder places
// Guerrilla's, each on its building: GuerrillaLadders); Armageddon's ladder code is Guerrilla's. Two things in it
// are Armageddon's own, and in our worlds they're Guerrilla's:
//   - What a damaged building does to its ladder. Both games run a callback on a building's damage event (event 0,
//     the building's handle at +4). Guerrilla's (ladder_building_damaged_callback, RM 0xA36B10): for an rfg_mover
//     whose percent destroyed is 0 or past 2.5 %, its first ladder child is switched on if the building is whole
//     (0 %) and fixed (move type RFGM_FIXED), else off; between the two nothing changes. Armageddon's (0x7AB1D0):
//     every ladder child of a mover off once it's past 0.1 % or not fixed. So a few bullet holes, which leave
//     Guerrilla's ladder climbable, took Armageddon's away. (Armageddon switches a building's ladders back on when
//     the building comes back, 0x7AB2C0, as Guerrilla's world reset does; left as it is.)
//   - The slide's sounds. Sliding down, Guerrilla plays MOV_FOLEY_LADDER_LOOP_01 (ladder_start_slide_sound) and,
//     landing, stops it and plays MOV_FOLEY_LADDER_RELEASE_01; Armageddon's same code (0x7AAD50, 0x7AA706) posts its
//     own events, game_audio.xtbl's Slide_Down_Ladder and Slide_Down_Ladder_Land, whose ids the game keeps at
//     0x13033E0 and 0x13033E4. Each slide's start (the landing always follows one) points both at the weapons
//     build's events of Guerrilla's cues in our worlds, at Armageddon's elsewhere. (Guerrilla plays the player's
//     release in 2D; ours plays at him in 3D, which sounds the same.)
constexpr DWORD kLadderBuildingDamaged = 0x7AB1D0;  // cdecl (event) -> its result: the game's callback on a building's damage
constexpr size_t kLadderBuildingDamagedPrologue = 6;  // push ebp / mov ebp,esp / sub esp,14h
constexpr DWORD kLadderSlideSound = 0x7AAD50;  // cdecl (human): the slide's loop started
constexpr size_t kLadderSlideSoundPrologue = 6;  // push ebp / mov ebp,esp / mov eax,[ebp+8]
constexpr DWORD kSlideLoopEvent = 0x13033E0, kSlideLandEvent = 0x13033E4;  // the two events' Wwise ids
constexpr DWORD kLadderGameWorld = 0x1870630;
constexpr DWORD kLadderMoverFromHandle = 0x424350;  // thiscall (world, handle): the mover of that handle, or null
constexpr DWORD kLadderObjType = 0x7A, kLadderObjSubtype = 0x7B;  // bytes of an object: its type and subtype
constexpr DWORD kLadderFirstChild = 0x34, kLadderNextSibling = 0x38;    // an object's children: a ring through each one's sibling
constexpr DWORD kLadderObjFlags = 0x54;                        // a ladder is switched on while bit 5 of this byte is set
constexpr DWORD kVtableSetEnabled = 0x8C;                   // a ladder's set_enabled(bool), thiscall
constexpr DWORD kDamagePct = 0x13C, kMoveType = 0x19D;      // an rfg_mover's percent destroyed (0-1), its move type (byte)
constexpr int kTypeMover = 2, kSubtypeRfgMover = 1, kTypeLadder = 0xE, kMoveFixed = 0;
constexpr float kLadderDamageLimit = 0.025f;  // Guerrilla's 2.5 % (RM 0x1694500)

// Wwise's id of a name: FNV-1 (32 bits) of its lower-case letters (as the game's GetIDFromString).
constexpr DWORD LadderSoundId(const char* name) {
    DWORD h = 2166136261u;
    for (; *name; ++name) h = (h * 16777619u) ^ static_cast<BYTE>(*name >= 'A' && *name <= 'Z' ? *name + 32 : *name);
    return h;
}
constexpr DWORD kGuerrillaSlideLoop = LadderSoundId("RFG_MOV_FOLEY_LADDER_LOOP_01");        // the weapons build's (GuerrillaLadders.SlideCues)
constexpr DWORD kGuerrillaSlideRelease = LadderSoundId("RFG_MOV_FOLEY_LADDER_RELEASE_01");

using LadderBuildingDamagedFn = int(__cdecl*)(const BYTE* event);
using LadderSlideSoundFn = void(__cdecl*)(BYTE* human);
using MoverFromHandleFn = BYTE*(__fastcall*)(void* world, void* unused, DWORD handle);
using SetLadderEnabledFn = bool(__fastcall*)(BYTE* ladder, void* unused, bool enabled);
LadderBuildingDamagedFn g_ladderBuildingDamaged = nullptr;
LadderSlideSoundFn g_ladderSlideSound = nullptr;
bool g_armageddonSlideKept = false;
DWORD g_armageddonSlideLoop = 0, g_armageddonSlideLand = 0;
bool g_guerrillaSlideLogged = false;

bool LadderOn(const BYTE* ladder) { return (ladder[kLadderObjFlags] >> 5 & 1) != 0; }

int __cdecl LadderBuildingDamaged(const BYTE* event) {
    if (!InOurWorld()) return g_ladderBuildingDamaged(event);
    // Guerrilla's ladder_building_damaged_callback.
    const int kind = *reinterpret_cast<const int*>(event);
    const DWORD handle = *reinterpret_cast<const DWORD*>(event + 4);
    if (kind != 0 || handle == 0xFFFFFFFF) return 0;
    BYTE* building = reinterpret_cast<MoverFromHandleFn>(kLadderMoverFromHandle)(reinterpret_cast<void*>(kLadderGameWorld), nullptr, handle);
    if (!building || building[kLadderObjType] != kTypeMover || building[kLadderObjSubtype] != kSubtypeRfgMover) return 0;
    const float destroyed = *reinterpret_cast<const float*>(building + kDamagePct);
    const bool whole = destroyed == 0.0f;
    if (!whole && !(destroyed > kLadderDamageLimit)) return 0;
    // Its first ladder child.
    BYTE* const first = *reinterpret_cast<BYTE**>(building + kLadderFirstChild);
    BYTE* ladder = nullptr;
    for (BYTE* child = first; child; ) {
        if (child[kLadderObjType] == kTypeLadder) {
            ladder = child;
            break;
        }
        child = *reinterpret_cast<BYTE**>(child + kLadderNextSibling);
        if (child == first) break;
    }
    if (!ladder) return 2;
    const bool on = whole && static_cast<signed char>(building[kMoveType]) == kMoveFixed;
    const bool was = LadderOn(ladder);
    auto setEnabled = reinterpret_cast<SetLadderEnabledFn>((*reinterpret_cast<void* const* const*>(ladder))[kVtableSetEnabled / 4]);
    setEnabled(ladder, nullptr, on);
    if (LadderOn(ladder) != was)
        rfa::LogDetail("Ladders: building %08lX %.1f%% destroyed, %s: its ladder %08lX switched %s", handle, destroyed * 100.0f,
                 static_cast<signed char>(building[kMoveType]) == kMoveFixed ? "fixed" : "moving", *reinterpret_cast<const DWORD*>(ladder + 0x68),
                 LadderOn(ladder) ? "on" : "off");
    return 2;
}

void __cdecl LadderSlideSound(BYTE* human) {
    DWORD& loop = *reinterpret_cast<DWORD*>(kSlideLoopEvent);
    DWORD& land = *reinterpret_cast<DWORD*>(kSlideLandEvent);
    if (!g_armageddonSlideKept) {  // the game's own, read from game_audio.xtbl while it started; nothing changes them but this
        g_armageddonSlideLoop = loop;
        g_armageddonSlideLand = land;
        g_armageddonSlideKept = true;
    }
    const bool ours = InOurWorld();
    loop = ours ? kGuerrillaSlideLoop : g_armageddonSlideLoop;
    land = ours ? kGuerrillaSlideRelease : g_armageddonSlideLand;
    if (ours && !g_guerrillaSlideLogged) {
        g_guerrillaSlideLogged = true;
        rfa::Log("Ladders: a slide down a ladder plays Guerrilla's RFG_MOV_FOLEY_LADDER_LOOP_01 and, landing, _RELEASE_01 (in place of the game's "
                 "events %08lX and %08lX)", g_armageddonSlideLoop, g_armageddonSlideLand);
    }
    g_ladderSlideSound(human);
}

void InstallLadders() {
    const BYTE damagedOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14};
    const BYTE slideOpening[] = {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08};
    if (memcmp(reinterpret_cast<const void*>(kLadderBuildingDamaged), damagedOpening, sizeof(damagedOpening)) == 0)
        g_ladderBuildingDamaged = reinterpret_cast<LadderBuildingDamagedFn>(
            rfa::HookFunction(kLadderBuildingDamaged, kLadderBuildingDamagedPrologue, reinterpret_cast<void*>(&LadderBuildingDamaged)));
    if (memcmp(reinterpret_cast<const void*>(kLadderSlideSound), slideOpening, sizeof(slideOpening)) == 0)
        g_ladderSlideSound = reinterpret_cast<LadderSlideSoundFn>(
            rfa::HookFunction(kLadderSlideSound, kLadderSlideSoundPrologue, reinterpret_cast<void*>(&LadderSlideSound)));
    if (g_ladderBuildingDamaged && g_ladderSlideSound)
        rfa::Log("Ladders: in our worlds a damaged building's ladder goes by Guerrilla's rule (off past 2.5%% or moving), and a slide down one sounds as Guerrilla's");
    else
        rfa::Log("Ladders: the game's code isn't as expected (%s%s); Armageddon's own there", g_ladderBuildingDamaged ? "" : "the damaged building's callback ",
                 g_ladderSlideSound ? "" : "the slide's sound");
}

}  // namespace

void guerrilla_world::Init() {
    if (rfa::ReadLogSwitch(L"GuerrillaWorld")) rfa::Log("Log=1: routine lines and the diagnoses (\"[guerrilla_world:log]\") are on");
    if (!rfa::game::IsSupportedBuild()) {
        rfa::Log("Not the publicbeta build; nothing to do");
        return;
    }
    rfa::MoveTable("Explosions", kExplosions, kExplosionSize, kGameMaxExplosions, kExplosionReferences, kExplosionLimits, g_explosions,
                   kMaxExplosions);
    MoveVisuals();
    MoreEffectRoom();
    MoreParticleRoom();
    MoreEffectTemplateRoom();
    MoreEmitterRoom();
    MoreMaterialEffectRoom();
    LoadMaterialEvents();
    LoadWheelEffects();
    InstallBuildings();
    InstallTurbines();
    InstallPropBalance();
    InstallStatsBlock();
    InstallLadders();
    MoreSoundEngineRoom();
    MoreStreamRoom();
    MoreStreamLookAhead();
    MoreAudioPool();
    InstallSoundDistances();
    LogSoundLimit();
    MoreUnbatchedRenderableRoom();
    g_loadShaders =reinterpret_cast<LoadShadersFn>(rfa::HookFunction(kLoadShaders, kLoadShadersPrologue, reinterpret_cast<void*>(&LoadShaders)));
    if (!g_loadShaders) rfa::Log("Could not add Guerrilla's material shaders to the game's; materials using them draw red");
    // Guerrilla's undergrowth: its random scatter with the pieces on the ground (see RandomlyPlace).
    if (!rfa::HookFunction(kRandomlyPlace, kRandomlyPlacePrologue, reinterpret_cast<void*>(&RandomlyPlace)))
        rfa::Log("Could not replace the undergrowth's random scatter; its pieces lie at height 0");
    // Guerrilla's bullet holes and cracks on glass (see BulletDecal); its shatter is set each frame
    // with the effects (GlassUpdate).
    g_bulletDecal = reinterpret_cast<BulletDecalFn>(rfa::HookFunction(kBulletDecal, kBulletDecalPrologue, reinterpret_cast<void*>(&BulletDecal)));
    if (!g_bulletDecal) rfa::Log("Could not hook the game's bullet marks; glass gets none, as in Armageddon");
    g_roadLog = rfa::ReadLogSwitch(L"GuerrillaWorld", L"RoadLog");
    g_skyOn = rfa::IniInt(L"GuerrillaWorld", L"Sky", 1) != 0;
    g_cloudsOn = rfa::IniInt(L"GuerrillaWorld", L"Clouds", 1) != 0;
    g_worlds = SplitList(rfa::IniString(L"GuerrillaWorld", L"Worlds", L"rfg"));
    g_lightOn = rfa::IniInt(L"GuerrillaWorld", L"Light", 1) != 0;
    ReadBrightness();
    ReadClockSpeed();
    ReadZoneSize();
    ReadStreamRadius();
    ReadStreamSteady();
    ReadFarClip();
    ReadGroundDetail();
    ReadShine();
    ReadBloom();
    ReadAmbient();
    ReadSun();
    ReadColourCorrection();
    ReadFog();
    ReadToneMapping();
    ReadSunShafts();
    ReadSunCorona();
    ReadSharpen();
    ReadSunShadows();
    ReadParticles();
    ReadParticleFog();
    ReadDust();
    // Our worlds' zone size (and a physics world wide enough for them), and the streaming radii held
    // to what the game can track.
    if (g_zoneWidth > 0) {
        g_zoneSize = reinterpret_cast<ZoneSizeFn>(rfa::HookFunction(kZoneSize, kZoneSizePrologue, reinterpret_cast<void*>(&ZoneSize)));
        if (g_zoneSize) {
            rfa::Log("Those worlds' zones are %.0f m wide (ZoneSize=)", g_zoneWidth);
            WidenWorld();
        } else {
            rfa::Log("Could not hook the game's zone size; ZoneSize= does nothing (and a world built for it won't load right)");
        }
    }
    g_setStreamRadius = reinterpret_cast<SetStreamRadiusFn>(
        rfa::HookFunction(kSetStreamRadius, kSetStreamRadiusPrologue, reinterpret_cast<void*>(&SetStreamRadiusHeld)));
    if (!g_setStreamRadius) rfa::Log("Could not hook the game's streaming radius; the game's own radii aren't held in those worlds");
    g_streamUpdate = reinterpret_cast<StreamUpdateFn>(
        rfa::HookFunction(kStreamUpdate, kStreamUpdatePrologue, reinterpret_cast<void*>(&StreamUpdateSteady)));
    if (!g_streamUpdate) rfa::Log("Could not hook the game's stream update; StreamSteady= does nothing");
    InstallRemadeChildren();
    {
        const BYTE ioOpening[] = {0x55, 0x8B, 0xEC, 0x6A, 0x00};
        const BYTE binkOpening[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00};
        if (memcmp(reinterpret_cast<const void*>(kPauseStreamIo), ioOpening, sizeof(ioOpening)) == 0 &&
            memcmp(reinterpret_cast<const void*>(kResumeStreamIo), ioOpening, sizeof(ioOpening)) == 0) {
            g_resumeStreamIo = reinterpret_cast<StreamIoFn>(rfa::HookFunction(kResumeStreamIo, kStreamIoPrologue, reinterpret_cast<void*>(&ResumeStreamIo)));
            if (g_resumeStreamIo)
                g_pauseStreamIo = reinterpret_cast<StreamIoFn>(rfa::HookFunction(kPauseStreamIo, kStreamIoPrologue, reinterpret_cast<void*>(&PauseStreamIo)));
        }
        if (memcmp(reinterpret_cast<const void*>(kBinkOpen), binkOpening, sizeof(binkOpening)) == 0)
            g_binkOpen = reinterpret_cast<BinkOpenFn>(rfa::HookFunction(kBinkOpen, kBinkOpenPrologue, reinterpret_cast<void*>(&BinkOpen)));
        if (!g_pauseStreamIo || !g_binkOpen) rfa::Log("Streaming: could not watch the stream I/O's pauses or the videos (the game's code isn't as expected)");
    }
    g_setAmbient = reinterpret_cast<SetAmbientFn>(rfa::HookFunction(kSetAmbient, kSetAmbientPrologue, reinterpret_cast<void*>(&SetAmbient)));
    g_setBackAmbient = reinterpret_cast<SetAmbientFn>(rfa::HookFunction(kSetBackAmbient, kSetAmbientPrologue, reinterpret_cast<void*>(&SetBackAmbient)));
    if (!g_setAmbient || !g_setBackAmbient) rfa::Log("Could not hook the time of day's ambient light; Ambient= does nothing");
    g_reportError = reinterpret_cast<ReportErrorFn>(rfa::HookFunction(kReportError, kReportErrorPrologue, reinterpret_cast<void*>(&ReportError)));
    if (!g_reportError) rfa::Log("Could not hook the game's error reports; the invalid mission hash lines stay");
    g_debugPrint = reinterpret_cast<DebugPrintFn>(rfa::HookFunction(kDebugPrint, kDebugPrintPrologue, reinterpret_cast<void*>(&DebugPrint)));
    if (!g_debugPrint) rfa::Log("Could not hook the game's debug print; what it says while loading our worlds isn't logged");
    g_precacheArea = reinterpret_cast<PrecacheAreaFn>(rfa::HookFunction(kPrecacheArea, kPrecacheAreaPrologue, reinterpret_cast<void*>(&PrecacheArea)));
    if (!g_precacheArea) rfa::Log("Could not hook the game's precache areas; our worlds' shared files won't be found");
    g_navpointStream = reinterpret_cast<NavpointStreamFn>(
        rfa::HookFunction(kNavpointStream, kNavpointStreamPrologue, reinterpret_cast<void*>(&NavpointStream)));
    g_findNavCell = reinterpret_cast<FindNavCellFn>(rfa::HookFunction(kFindNavCell, kFindNavCellPrologue, reinterpret_cast<void*>(&FindNavCell)));
    if (!g_navpointStream || !g_findNavCell)
        rfa::Log("Could not hook the navpoints' streaming or the nav cell lookup; buildings' transition points on switched-off cells stay unjoined");
    // Guerrilla's gameplay properties, draw distances and effects, if the world builder has written them (the
    // game reads the tables while it starts, before any world).
    bool anyTable = false;
    // The explosions: the builder writes them twice, cutting buildings by Guerrilla's own radii (explosions.xtbl)
    // and by Armageddon's explosions of the same names (explosions_armageddon_cuts.xtbl); GuerrillaExplosions=0
    // hands the game the second.
    const bool guerrillaExplosions = rfa::IniInt(L"GuerrillaWorld", L"GuerrillaExplosions", 1) != 0;
    for (BuiltTable& table : g_builtTables) {
        std::wstring file = DataFolder();
        const bool armageddonCuts = !guerrillaExplosions && strcmp(table.name, "explosions.xtbl") == 0 &&
                                    GetFileAttributesW((DataFolder() + L"explosions_armageddon_cuts.xtbl").c_str()) != INVALID_FILE_ATTRIBUTES;
        if (armageddonCuts) file += L"explosions_armageddon_cuts.xtbl";
        else for (const char* c = table.name; *c; ++c) file += static_cast<wchar_t>(*c);
        if (strcmp(table.name, "explosions.xtbl") == 0)
            rfa::Log(armageddonCuts ? "Explosions: they cut buildings by Armageddon's radii for the same names (GuerrillaExplosions=0)"
                                    : "Explosions: they cut buildings by Guerrilla's own radii, if the world was built with them (GuerrillaExplosions=1)");
        if (ReadWholeFile(file, table.bytes) && !table.bytes.empty()) {
            table.bytes.push_back(0);
            anyTable = true;
        } else {
            table.bytes.clear();
            rfa::Log("%s: no mods\\guerrilla_world\\%s (made by the Guerrilla tools); the game keeps its own", table.what, table.name);
        }
    }
    if (anyTable) {
        g_namePoolInit = reinterpret_cast<NamePoolInitFn>(rfa::HookFunction(kNamePoolInit, kNamePoolInitPrologue, reinterpret_cast<void*>(&NamePoolInit)));
        g_tableCacheFind = reinterpret_cast<TableCacheFindFn>(
            rfa::HookFunction(kTableCacheFind, kTableCacheFindPrologue, reinterpret_cast<void*>(&TableCacheFind)));
        if (!g_namePoolInit || !g_tableCacheFind) {
            rfa::Log("Could not hook the game's table reading; the game keeps its own tables (gameplay properties, draw distances, effects)");
            for (BuiltTable& table : g_builtTables) table.bytes.clear();  // (a hook that did go in only passes through)
        }
        g_effectFileLoad = reinterpret_cast<EffectFileLoadFn>(
            rfa::HookFunction(kEffectFileLoad, kEffectFileLoadPrologue, reinterpret_cast<void*>(&EffectFileLoad)));
        if (!g_effectFileLoad) rfa::Log("Could not watch the game's effect files loading (the log won't say which load)");
        g_playEffect = reinterpret_cast<PlayEffectFn>(rfa::HookFunction(kPlayEffect, kPlayEffectPrologue, reinterpret_cast<void*>(&PlayEffect)));
        // What becomes of the effects' sounds: Wwise's own messages, and how long each sound played (see there).
        g_wwiseOutput = reinterpret_cast<WwiseOutputFn>(rfa::HookFunction(kWwiseOutput, kWwiseOutputPrologue, reinterpret_cast<void*>(&WwiseOutput)));
        // The sounds' lengths, and Wwise's memory every 10 s, only for the log (Log=1): the watch runs on every effect's sound update.
        if (rfa::DetailLog())
            g_effectSound = reinterpret_cast<EffectSoundFn>(rfa::HookFunction(kEffectSound, kEffectSoundPrologue, reinterpret_cast<void*>(&EffectSound)));
        if (!g_wwiseOutput || (rfa::DetailLog() && !g_effectSound))
            rfa::Log("Could not watch the effects' sounds (Wwise's messages %s, the sounds' lengths %s)", g_wwiseOutput ? "watched" : "not",
                     g_effectSound ? "watched" : "not");
        g_makeObject = reinterpret_cast<MakeObjectFn>(rfa::HookFunction(kMakeObject, kMakeObjectPrologue, reinterpret_cast<void*>(&MakeObject)));
        if (!g_playEffect || !g_makeObject) rfa::Log("Could not watch the game's effects playing (the log won't say why one doesn't)");
    }
    if (!g_skyOn && !g_lightOn && g_fog <= 0 && g_clockSpeed <= 0) {
        rfa::Log("Guerrilla's sky, light, fog and clock are off (Sky=0, Light=0, Fog=0, ClockSpeed=0)");
        return;
    }
    if ((g_skyOn || g_lightOn || g_fog > 0) &&
        !rfa::HookImport(GetModuleHandleW(nullptr), "d3d11.dll", "D3D11CreateDevice", &CreateDevice, &g_createDevice)) {
        rfa::Log("Could not watch for the game's Direct3D device; no sky, light or fog");
        g_skyOn = g_lightOn = false;
        g_fogFailed = true;
    }
    // The light pass, for the light and for the depth the fog reads (Fog= can be raised from 0 while
    // playing, so it is wrapped either way; it only passes through when neither is wanted).
    if (g_lightOn || !g_fogFailed) {
        g_lightPass = reinterpret_cast<LightPassFn>(rfa::HookFunction(kLightPass, kLightPassPrologue, reinterpret_cast<void*>(&LightPass)));
        g_lightCheck = reinterpret_cast<LightCheckFn>(rfa::HookFunction(kLightCheck, kLightCheckPrologue, reinterpret_cast<void*>(&LightCheck)));
        if (!g_lightPass || !g_lightCheck) {
            rfa::Log("Could not hook the game's light pass; no light added and no fog");
            g_lightOn = false;  // (a hook that did go in only passes through)
            g_fogFailed = true;
        }
        // For the sun's shadows: the follow player light switched on and put along the sun, and each
        // lamp's drawing (hooks that didn't go in leave the sun unshadowed, and only pass through).
        if (g_lightOn) {
            g_materialPass = reinterpret_cast<MaterialPassFn>(
                rfa::HookFunction(kMaterialPass, kMaterialPassPrologue, reinterpret_cast<void*>(&MaterialPass)));
            if (!g_materialPass) rfa::Log("Could not hook the game's material pass; Guerrilla's material shaders light by the light buffer");
            g_followLights = reinterpret_cast<FollowLightsFn>(
                rfa::HookFunction(kFollowLights, kFollowLightsPrologue, reinterpret_cast<void*>(&FollowLights)));
            g_placeFollowLight = reinterpret_cast<PlaceFollowLightFn>(
                rfa::HookFunction(kPlaceFollowLight, kPlaceFollowLightPrologue, reinterpret_cast<void*>(&PlaceFollowLight)));
            g_drawLamp = reinterpret_cast<DrawLampFn>(rfa::HookFunction(kDrawLamp, kDrawLampPrologue, reinterpret_cast<void*>(&DrawLamp)));
            g_setTexture = reinterpret_cast<SetTextureFn>(rfa::HookFunction(kSetTexture, kSetTexturePrologue, reinterpret_cast<void*>(&SetTexture)));
            if (!g_followLights || !g_placeFollowLight || !g_drawLamp || !g_setTexture)
                rfa::Log("Could not hook the follow player light or the light pass's lamps; the sun lights without shadows");
        }
    }
    // Guerrilla's particle layer (with its tone mapping).
    g_particlePhase = reinterpret_cast<ParticlePhaseFn>(rfa::HookFunction(kParticlePhase, kParticlePhasePrologue, reinterpret_cast<void*>(&ParticlePhase)));
    g_particleLast = reinterpret_cast<ParticleLastFn>(rfa::HookFunction(kParticleLast, kParticlePhasePrologue, reinterpret_cast<void*>(&ParticleLast)));
    g_fogAtParticles = g_particlePhase != nullptr;
    if (!g_particlePhase || !g_particleLast) {
        rfa::Log("Could not hook the particle phase; the game draws its particles its own way");
        g_particleLayerFailed = true;  // (a hook that did go in only passes through)
    }
    // Guerrilla's fog on particles and coronas.
    g_particleDraw = reinterpret_cast<ParticleDrawFn>(rfa::HookFunction(kParticleDraw, kParticleDrawPrologue, reinterpret_cast<void*>(&ParticleDraw)));
    g_coronaDraw = reinterpret_cast<CoronaDrawFn>(rfa::HookFunction(kCoronaDraw, kCoronaDrawPrologue, reinterpret_cast<void*>(&CoronaDraw)));
    if (!g_particleDraw || !g_coronaDraw) rfa::Log("Could not hook the particles' draw; they aren't fogged");
    // Armageddon's mission start (F9, mission_start), left alone in our worlds.
    g_startMission = reinterpret_cast<StartMissionFn>(rfa::HookFunction(kStartMission, kStartMissionPrologue, reinterpret_cast<void*>(&StartMission)));
    if (!g_startMission) rfa::Log("Could not hook the mission start; F9 in our worlds still quits the game");
    // Guerrilla's dust, after the game's effects update each frame.
    g_effectsUpdate = reinterpret_cast<EffectsUpdateFn>(rfa::HookFunction(kEffectsUpdate, kEffectsUpdatePrologue, reinterpret_cast<void*>(&EffectsUpdate)));
    if (!g_effectsUpdate) rfa::Log("Could not hook the game's effects update; no dust, and Armageddon's wind");
    // Guerrilla's wind, in place of the environment boxes' weather.
    g_applyEnvSettings = reinterpret_cast<ApplyEnvSettingsFn>(
        rfa::HookFunction(kApplyEnvSettings, kApplyEnvSettingsPrologue, reinterpret_cast<void*>(&ApplyEnvSettings)));
    g_windStart = reinterpret_cast<WindStartFn>(rfa::HookFunction(kWindStart, kWindStartPrologue, reinterpret_cast<void*>(&WindStart)));
    if (!g_applyEnvSettings || !g_windStart) rfa::Log("Could not hook the environment boxes' weather or the wind's start; Armageddon's wind");
    g_toneMappingConstants = reinterpret_cast<ToneMappingConstantsFn>(
        rfa::HookFunction(kToneMappingConstants, kToneMappingConstantsPrologue, reinterpret_cast<void*>(&ToneMappingConstants)));
    g_finalComposite[0] = reinterpret_cast<FinalCompositeFn>(
        rfa::HookFunction(kFinalComposite[0], kFinalCompositePrologue, reinterpret_cast<void*>(&FinalComposite0)));
    g_finalComposite[1] = reinterpret_cast<FinalCompositeFn>(
        rfa::HookFunction(kFinalComposite[1], kFinalCompositePrologue, reinterpret_cast<void*>(&FinalComposite1)));
    if (!g_toneMappingConstants || !g_finalComposite[0] || !g_finalComposite[1]) {
        rfa::Log("Could not hook the game's tone mapping; Armageddon's own stays in those worlds");
        g_toneMappingFailed = true;  // (the hooks that did go in only pass through then)
    }
    g_renderSkybox = reinterpret_cast<RenderSkyboxFn>(
        rfa::HookFunction(kRenderSkybox, kRenderSkyboxPrologue, reinterpret_cast<void*>(&RenderSkybox)));
    if (!g_renderSkybox) {
        rfa::Log("Could not hook the game's sky step; no sky, clock or setting changes while playing");
        return;
    }
    if (g_skyOn)
        rfa::Log(g_cloudsOn ? "Guerrilla's sky and clouds are drawn in the worlds named by Worlds= in [GuerrillaWorld]"
                            : "Guerrilla's sky is drawn (no clouds) in the worlds named by Worlds= in [GuerrillaWorld]");
    if (g_lightOn) rfa::Log("The sky's light and the sun light those worlds (Light=; Ambient=%.3g, Sun=%.3g)", g_ambient, g_sun);
    if (!g_fogFailed) rfa::Log("Guerrilla's fog is drawn in those worlds (Fog=%.3g)", g_fog);
    if (!g_toneMappingFailed)
        rfa::Log(g_toneMapping ? "Guerrilla's tone mapping draws those worlds' picture (ToneMapping=1, Exposure=%.3g)"
                               : "Armageddon's own tone mapping in those worlds (ToneMapping=0)", g_exposure);
    if (g_clockSpeed > 0) rfa::Log("The game clock runs at %.3g game seconds a second in those worlds (ClockSpeed=)", g_clockSpeed);
    if (g_streamRadius > 0) rfa::Log("Those worlds' zones stay loaded %.0f m around the player (StreamRadius=)", g_streamRadius);
}
