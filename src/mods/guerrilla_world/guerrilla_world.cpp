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
// 2-3 drift sideways at their own speeds (cloud_layer01_speed, cloud_layer23_speed). In front of
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
// are lit from the same direction. That light, the sky, the clouds and the backdrop are multiplied
// by Guerrilla's colour correction (color_correct_tint, ColourCorrection=), which Guerrilla applies
// to its whole picture and Armageddon to none of it.
//
// The fog: Armageddon's is each environment box's, by distance only, and it keeps the time of day's
// fog curves without drawing with them. Guerrilla's is height fog, thicker low down and far away,
// from its districts' curves. In our worlds the mod draws Guerrilla's over what the world has drawn,
// before the sky, from the depth the light pass reads (Fog=, thickness; the world builder turns the
// boxes' own fog off there).
//
// The tone mapping: Armageddon turns its picture into the final one with an exposure the environment
// box sets (3 in our zones), a curve that clips its highlights and a colour table. In our worlds the
// mod holds that exposure at 1 and draws Guerrilla's tone mapping over the result instead: its eye
// adapting to the picture's brightness, its exposure from the time of day's curves, and its curve that
// rolls the highlights off (ToneMapping=, Exposure=).
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
#define RFA_MOD_NAME "guerrilla_world"
#include "rfa_mod.h"
#include "rfa_game.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <math.h>
#include <stdarg.h>
#include <vector>

namespace {

// ---- The game

// The world being played: its name ("world" for the campaign, "rfg" for the ported world) is
// kept in the world object (0x1870630) at +0x29EA, as the time of day reads it to name its files.
const char* WorldName() { return reinterpret_cast<const char*>(0x1870630 + 0x29EA); }

// The camera the frame is drawn with: the camera manager (0x11B9C40) +0x50, its rows right, up and
// forward; +0xC0, its vertical field of view in degrees (as the console and FOV mods read them).
auto& g_cameraRotation = *reinterpret_cast<float(*)[9]>(0x11B9C90);
auto& g_cameraFov = *reinterpret_cast<float*>(0x11B9D00);

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

// A colour curve now: every loaded set's value at the time of day times the set's weight, added up
// (as the game does), the colour times its own brightness.
void TodColour(size_t offset, float (&out)[3]) {
    out[0] = out[1] = out[2] = 0;
    for (int set = 0; set < 4; ++set) {
        if (!g_todSets[set] || g_todWeights[set] == 0) continue;
        float value[4] = {0, 0, 0, 0};
        ColourAt(g_todSets[set] + offset, nullptr, value, g_timeOfDay);
        for (int i = 0; i < 3; ++i) out[i] += value[i] * value[3] * g_todWeights[set];
    }
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
// number. Its light is pink and the tint cyan, so only the two together give its colours. Armageddon
// doesn't colour its picture by the time of day's tint (its own sets all carry the same grey), so in
// our worlds the mod multiplies what it draws by it: the light it adds, the sky, the clouds and the
// backdrop. ColourCorrection=0 leaves it out (read again whenever rfa_mods.ini is saved).
constexpr size_t kColourCorrection = 0x47A0;
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

// Once a second: if rfa_mods.ini was saved since, read the brightness, ambient light, sun, colour
// correction, fog, exposure and clock speed again.
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
}

bool InOurWorld() {
    const char* world = WorldName();
    for (const std::string& name : g_worlds)
        if (_stricmp(world, name.c_str()) == 0) return true;
    return false;
}

// The sun's path, our own model of Guerrilla's (its orbitals file gives the sun an orbit plane
// turned 118° and fades it in from 7:30 to 19:30): it rises in the east (+x, where the sky's
// morning colours are), crosses at 13:30 at 62° up, leaning south (-z), and sets in the west.
// Toward the sun, in the world.
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

// The light that shines from one direction: tod_light_color (Guerrilla's: the sun's by day, blue
// moonlight by night) times Sun=, from the sun while it is up and from the opposite side of the sky
// (the moon, roughly) while it is down, fading out as either nears the horizon. Toward the light,
// and the light.
void DirectLight(float (&toward)[3], float (&out)[3]) {
    SunToward(toward);
    if (toward[1] < 0)
        for (float& v : toward) v = -v;
    TodColour(kSunColour, out);
    float up = (toward[1] - 0.01f) / 0.1f;  // 0 at the horizon, 1 at 6° up
    up = up < 0 ? 0 : up > 1 ? 1 : up;
    for (float& c : out) c *= g_sun * up * up * (3 - 2 * up);
}

// Replace the time of day's ambient setters: the game's own, then in our worlds the world's
// ambient light times Ambient=.
void ScaleAmbient(float (&colour)[3]) {
    if (g_ambient == 1.0f || !InOurWorld()) return;
    for (float& c : colour) c *= g_ambient;
}

void __cdecl SetAmbient(const float* colour) {
    g_setAmbient(colour);
    ScaleAmbient(g_worldAmbient);
}

void __cdecl SetBackAmbient(const float* colour) {
    g_setBackAmbient(colour);
    ScaleAmbient(g_worldBackAmbient);
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

// The radius setter, wrapped: in our worlds, held to what each layer can track.
void __fastcall SetStreamRadiusHeld(void* manager, void* unused, int layer, float radius) {
    if ((layer == kSubzoneLayer || layer == kZoneLayer) && InOurWorld() && radius > MostRadius(layer)) {
        RFA_LOG_FIRST(2, "Streaming: layer %d's radius %.0f m held to %.0f m (more could meet more cells than the game tracks)",
                      layer, radius, MostRadius(layer));
        radius = MostRadius(layer);
    }
    g_setStreamRadius(manager, unused, layer, radius);
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

int g_debugPrintsLogged = 0;

void __cdecl DebugPrint(const char* format, ...) {
    char text[1024];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format ? format : "", arguments);
    va_end(arguments);
    if (g_debugPrintsLogged < 500 && InOurWorld()) {
        char shown[1024];
        strcpy_s(shown, text);
        size_t length = strlen(shown);
        while (length > 0 && (shown[length - 1] == '\n' || shown[length - 1] == '\r')) shown[--length] = '\0';
        if (++g_debugPrintsLogged == 500) rfa::Log("Game debug print: %s (the last one logged)", shown);
        else rfa::Log("Game debug print: %s", shown);
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

// ---- Shaders

// Guerrilla's sky, as its shader works it out, over the whole screen. The vertex shader makes one
// triangle that covers the screen, at the far plane; the pixel shader turns each pixel back into
// the direction it looks in and colours it.
constexpr char kSkyShader[] = R"(
cbuffer Sky : register(b0)
{
    float4 Hemisphere[10];  // west0-3, zenith, east0-3, zenith: each colour times its brightness
    float4 Right;           // the camera's axes in the world; Right.w: tan of half the horizontal field of view
    float4 Up;              // Up.w: tan of half the vertical field of view
    float4 Forward;         // Forward.w: the far plane's depth (1, or 0 if the game's depth runs the other way)
};

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
    return float4(lerp(west, east, across), 1);
}
)";

// Guerrilla's clouds, as its cloud shader draws them. The model's points are directions from the
// eye; they go just in front of the far plane (so behind everything the world has drawn), and come
// out with their colour already times their cover (laid over the sky with "one, one minus alpha").
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
    float4 Drift;      // x: layers 0-1's sideways offset, y: layers 2-3's
};

Texture2D Layers01 : register(t0);
Texture2D Layers23 : register(t1);
SamplerState Wrapped : register(s0);

struct CloudPoint
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float3 look : TEXCOORD1;
    float4 layers : TEXCOORD2;  // xy: where layers 0-1 are read, zw: layers 2-3
};

CloudPoint CloudVertex(float3 position : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD)
{
    float3 look = normalize(position);
    float depth = dot(look, Forward.xyz);
    CloudPoint p;
    p.position = float4(dot(look, Right.xyz) / Right.w, dot(look, Up.xyz) / Up.w, depth * lerp(0.000001, 0.999999, Forward.w), depth);
    p.normal = normal;
    p.look = look;
    p.layers = float4(uv.x + Drift.x, uv.y, uv.x + Drift.y, uv.y);
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
    float rim = min(exp(-covered * Shape.w), 1);
    float towardsSun = pow(saturate(dot(normalize(p.look), Light.xyz)), 30);
    lit += Front.rgb * Shape.z * rim * towardsSun;
    float alpha = covered * covered;
    return min(float4(light * lit * alpha, alpha), 32);
}

// The backdrop, as Guerrilla's backdrop shader (rfg-skybox-matte_s) draws it. With it the buffer's
// Front and Rear are the mountains' colour on the side facing the light and on the far side, and
// Strengths is the mountain haze: its colour (rgb) and how thick it is (a).
float4 BackdropPixel(CloudPoint p) : SV_Target
{
    float4 picture = Layers01.Sample(Wrapped, p.layers.xy);
    float3 mountain = lerp(Rear.rgb, Front.rgb, dot(normalize(p.normal), Light.xyz) * 0.5 + 0.5);
    float3 colour = lerp(picture.rgb, Strengths.rgb, Strengths.a) * mountain;
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
constexpr char kLightShader[] = R"(
cbuffer Light : register(b0)
{
    float4 FromAbove;   // the sky's light on a surface facing straight up
    float4 FromBelow;   // and facing straight down
    float4 SunColour;   // the sun's light on a surface facing it (0 while it is down)
    float4 SunToward;   // toward the sun, in the camera's space
    float4 UpToward;    // straight up, in the camera's space
    float4 TargetSize;  // the light buffer's width and height, and the tangents of half the field of view across and up
};

Texture2D Normals : register(t0);
SamplerState Nearest : register(s0);

float4 LightVertex(uint id : SV_VertexID) : SV_Position
{
    float2 corner = float2((id << 1) & 2, id & 2);
    return float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 LightPixel(float4 position : SV_Position) : SV_Target
{
    float2 at = position.xy / TargetSize.xy;
    float4 stored = Normals.Sample(Nearest, at);
    float3 n = stored.xyz;
    if (n.z >= 0.501961) n.z -= 0.5;
    n = n * 2 - 1;
    float length2 = dot(n, n);
    n = length2 > 0.000001 ? n * rsqrt(length2) : UpToward.xyz;
    float3 sky = lerp(FromBelow.rgb, FromAbove.rgb, dot(n, UpToward.xyz) * 0.5 + 0.5);
    float facing = dot(n, SunToward.xyz);
    float3 sun = SunColour.rgb * saturate(facing);

    float sharpness = (stored.w >= 0.501961 ? stored.w - 0.501961 : stored.w) * 14.166667;
    sharpness = sharpness * sharpness * 5.12;
    float3 eye = normalize(float3((at * float2(2, -2) + float2(-1, 1)) * TargetSize.zw, 1));  // from the camera to the surface
    float3 reflection = 2 * facing * n - SunToward.xyz;
    float toEye = max(saturate(dot(reflection, -eye)), 0.000001);
    float shine = saturate(facing) * pow(toEye, sharpness) * dot(SunColour.rgb, float3(0.3, 0.59, 0.11));
    return float4(sky + sun, shine);
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

// Guerrilla's tone mapping (its rl_hdr passes 11-13, 12 and 21), drawn over Armageddon's final picture
// from the scene it was made from (held at exposure 1, so Guerrilla's own values). AdaptPixel, into a
// 1 x 1 target: the picture's brightness as Guerrilla measures it, 4.5 × each sample's luminance
// (Rec. 709; its scene buffer keeps the picture ÷ 8 and its measure takes 36 ×), each held to
// luminance_min-max, averaged over a grid, and the eye's brightness eased toward it, by
// 1 − 2^(−0.8744 × the frame's time × iris_rate). ToneMapPixel: the exposure, lum_range ÷ max(1 +
// lum_offset × (base + amount × (adapted − base)), 0.1) (eye_adaption_base, _amount), times Exposure=;
// the picture 1 − e^(−exposure × (scene + bloom × bloom_amount)), encoded for the screen (sRGB:
// Guerrilla's display gamma, 2.4).
constexpr char kToneMappingShader[] = R"(
cbuffer ToneMapping : register(b0)
{
    float4 Adapt;     // x how far the eye moves toward the measure this frame (1: all the way), y z the range each sample is held to
    float4 Exposure;  // x lum_range, y lum_offset, z eye_adaption_base, w eye_adaption_amount
    float4 Picture;   // x Exposure=, y bloom_amount, z w the picture's width and height
};

Texture2D Scene : register(t0);
Texture2D Bloom : register(t1);
Texture2D Adapted : register(t2);  // 1 x 1: the brightness the eye has adapted to
SamplerState Smooth : register(s0);

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
            total += clamp(4.5 * dot(colour, float3(0.2125, 0.7154, 0.0721)), Adapt.y, Adapt.z);
        }
    float measured = total / 576;
    float before = Adapted.Load(int3(0, 0, 0)).r;
    return float4(Adapt.x >= 1 ? measured : before + (measured - before) * Adapt.x, 0, 0, 1);
}

float4 TonePixel(float4 position : SV_Position) : SV_Target
{
    float3 colour = max(Scene.Load(int3(position.xy, 0)).rgb, 0);
    float3 bloom = max(Bloom.Sample(Smooth, position.xy / Picture.zw).rgb, 0) * Picture.y;
    float adapted = Adapted.Load(int3(0, 0, 0)).r;
    float exposure = Exposure.x / max(1 + Exposure.y * (Exposure.z + Exposure.w * (adapted - Exposure.z)), 0.1) * Picture.x;
    float3 picture = saturate(1 - exp(-exposure * (colour + bloom)));
    float3 encoded = picture <= 0.0031308 ? picture * 12.92 : 1.055 * pow(picture, 1 / 2.4) - 0.055;
    return float4(encoded, 1);
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
};

// A cloud part from clouds.bin (see the world builder, rfgsky): which indices, overhead (0) or along
// the horizon (1), where its clouds fade in, and its two textures.
struct CloudPart {
    UINT first = 0, count = 0;
    int kind = 0;
    float fade = 0, full = 1;
    ID3D11ShaderResourceView* layers[2] = {};
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
// with its mip levels.
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
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
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
    ColourCorrection(tint);
    for (int c = 0; c < 10; ++c) {
        float colour[3];
        TodColour(kSkyCurves[c], colour);
        for (int i = 0; i < 3; ++i) hemisphere[c][i] = colour[i] * SkyBrightness() * tint[i];
        hemisphere[c][3] = 1;
    }
}

// The clouds' drift: layers 0-1 and 2-3 move sideways at their speeds (texture widths per
// 240 seconds at speed 1).
void Drift() {
    const ULONGLONG now = GetTickCount64();
    const float seconds = g_lastDrift ? (now - g_lastDrift) / 1000.0f : 0.0f;
    g_lastDrift = now;
    if (seconds > 1) return;  // paused, loading
    g_drift[0] = fmodf(g_drift[0] + TodNumber(kLayer01Speed) * seconds / 240.0f, 1.0f);
    g_drift[1] = fmodf(g_drift[1] + TodNumber(kLayer23Speed) * seconds / 240.0f, 1.0f);
}

void DrawClouds(const SkyConstants& sky) {
    if (g_cloudParts.empty()) return;
    Drift();
    CloudConstants constants = {};
    memcpy(constants.right, sky.right, sizeof(constants.right));
    memcpy(constants.up, sky.up, sizeof(constants.up));
    memcpy(constants.forward, sky.forward, sizeof(constants.forward));
    if (g_lightOn) {
        float toward[3], unused[3];  // lit by our sun (or moon)
        DirectLight(toward, unused);
        for (int i = 0; i < 3; ++i) constants.light[i] = toward[i];
    } else {
        float length = sqrtf(g_lightDirection[0] * g_lightDirection[0] + g_lightDirection[1] * g_lightDirection[1] + g_lightDirection[2] * g_lightDirection[2]);
        for (int i = 0; i < 3; ++i) constants.light[i] = length > 0 ? g_lightDirection[i] / length : (i == 1 ? 1.0f : 0.0f);
    }
    constants.shape[2] = TodNumber(kBacklightStrength);
    constants.shape[3] = TodNumber(kBacklightPower);
    constants.drift[0] = g_drift[0];
    constants.drift[1] = g_drift[1];

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
    ColourCorrection(tint);
    for (const CloudPart& part : g_cloudParts) {
        float colour[3];
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
    ID3D11Buffer* vertexConstants = nullptr;
    ID3D11Buffer* pixelConstants[2] = {};
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
        g_context->VSGetConstantBuffers(0, 1, &vertexConstants);
        g_context->PSGetConstantBuffers(0, 2, pixelConstants);
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
        g_context->VSSetConstantBuffers(0, 1, &vertexConstants);
        g_context->PSSetConstantBuffers(0, 2, pixelConstants);
        g_context->PSSetShaderResources(0, 3, pixelResources);
        g_context->PSSetSamplers(0, 1, &pixelSampler);
        g_context->OMSetBlendState(blend, blendFactor, sampleMask);
        g_context->RSSetState(raster);
        for (IUnknown* held : {static_cast<IUnknown*>(layout), static_cast<IUnknown*>(vertexBuffer), static_cast<IUnknown*>(indexBuffer),
                               static_cast<IUnknown*>(vertexShader), static_cast<IUnknown*>(pixelShader), static_cast<IUnknown*>(geometryShader),
                               static_cast<IUnknown*>(vertexConstants), static_cast<IUnknown*>(pixelConstants[0]), static_cast<IUnknown*>(pixelConstants[1]),
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

// Adds the sky's light and the sun into the light buffer the light pass has just filled (its lamps'
// light), over the whole buffer, and puts the device context back as it was.
void DrawLight(ID3D11RenderTargetView* buffer, ID3D11ShaderResourceView* normals) {
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
    ColourCorrection(tint);
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
    g_context->OMSetRenderTargets(1, &buffer, nullptr);
    g_context->RSSetViewports(1, &viewport);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->IASetInputLayout(nullptr);
    g_context->VSSetShader(g_lightVertex, nullptr, 0);
    g_context->PSSetShader(g_lightPixel, nullptr, 0);
    g_context->GSSetShader(nullptr, nullptr, 0);
    g_context->PSSetConstantBuffers(0, 1, &g_lightConstants);
    g_context->PSSetShaderResources(0, 1, &normals);
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

// For the fog, drawn later in the frame (at the sky step): the G-buffer's depth (t14) and the game's
// own constants (vc4, whose IR_Similarity_Data turns depth into distance), noted during the light pass.
bool g_noteFog = false;
ID3D11ShaderResourceView* g_fogDepth = nullptr;
ID3D11Buffer* g_gameConstants = nullptr;

void DropFogInputs() {
    if (g_fogDepth) g_fogDepth->Release();
    if (g_gameConstants) g_gameConstants->Release();
    g_fogDepth = nullptr;
    g_gameConstants = nullptr;
}

// Draws Guerrilla's fog over what the world has drawn into the game's current target, and puts the
// device context back as it was.
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
    D3D11_VIEWPORT viewport = {};
    UINT viewports = 1;
    g_context->RSGetViewports(&viewports, &viewport);
    D3D11_BUFFER_DESC gameConstantsDesc = {};
    g_gameConstants->GetDesc(&gameConstantsDesc);

    FogConstants constants = {};
    float colour[3], tint[3];
    TodColour(kFogColour, colour);
    ColourCorrection(tint);
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
    if (!targets[0] || viewport.Width <= 0 || viewport.Height <= 0 || gameConstantsDesc.ByteWidth < 13 * 16) {
        RFA_LOG_FIRST(1, "Fog: nothing to draw into, or the game's constants are not the ones expected (%u bytes); no fog",
                      gameConstantsDesc.ByteWidth);
    } else if (SUCCEEDED(g_context->Map(g_fogConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &constants, sizeof(constants));
        g_context->Unmap(g_fogConstants, 0);
        SavedState saved;
        saved.Save();
        g_context->OMSetRenderTargets(1, &targets[0], nullptr);  // the depth is read, so it can't stay bound for testing
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->IASetInputLayout(nullptr);
        g_context->VSSetShader(g_fogVertex, nullptr, 0);
        g_context->PSSetShader(g_fogPixel, nullptr, 0);
        g_context->GSSetShader(nullptr, nullptr, 0);
        ID3D11Buffer* buffers[2] = {g_fogConstants, g_gameConstants};
        g_context->PSSetConstantBuffers(0, 2, buffers);
        g_context->PSSetShaderResources(0, 1, &g_fogDepth);
        g_context->PSSetSamplers(0, 1, &g_fogSampler);
        const float noFactor[4] = {1, 1, 1, 1};
        g_context->OMSetBlendState(g_fogBlend, noFactor, 0xFFFFFFFF);
        g_context->OMSetDepthStencilState(g_fogDepthTest, 0);
        g_context->RSSetState(g_fogRaster);
        g_context->Draw(3, 0);
        saved.Restore();
        g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, depthView);
        g_context->OMSetDepthStencilState(gameDepth, stencilRef);
    }
    for (ID3D11RenderTargetView* target : targets)
        if (target) target->Release();
    if (depthView) depthView->Release();
    if (gameDepth) gameDepth->Release();
}

// Guerrilla's tone mapping, drawn over Armageddon's final picture (its target, 8-bit) from the scene it
// was made from (16-bit float, at exposure 1) and its bloom: the eye's brightness eased toward this
// frame's measure, then the picture. Puts the device context back as it was.
int g_eyeNow = 0;           // which of the two textures holds the eye's brightness
bool g_eyeFresh = true;     // start the eye at the measure (the first frame, or after a gap)
LARGE_INTEGER g_toneLast = {};
ULONGLONG g_lastEyeLog = 0;
bool g_eyeReadPending = false;

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
    constants.picture[1] = TodNumber(kBloomAmount);
    constants.picture[2] = static_cast<float>(width);
    constants.picture[3] = static_cast<float>(height);
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

    // The picture, over Armageddon's.
    const D3D11_VIEWPORT whole = {0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    g_context->OMSetRenderTargets(1, &target, nullptr);
    g_context->RSSetViewports(1, &whole);
    ID3D11ShaderResourceView* pictureInputs[3] = {scene, bloom, g_eyeView[next]};
    g_context->PSSetShaderResources(0, 3, pictureInputs);
    g_context->PSSetShader(g_tonePixel, nullptr, 0);
    g_context->OMSetBlendState(g_toneBlend, noFactor, 0xFFFFFFFF);
    g_context->Draw(3, 0);

    ID3D11ShaderResourceView* none[3] = {};
    g_context->PSSetShaderResources(0, 3, none);
    g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, gameTargets, gameDepthView);
    g_context->RSSetViewports(viewportCount, gameViewports);
    g_context->OMSetDepthStencilState(gameDepth, stencilRef);
    saved.Restore();
    for (ID3D11RenderTargetView* gameTarget : gameTargets)
        if (gameTarget) gameTarget->Release();
    if (gameDepthView) gameDepthView->Release();
    if (gameDepth) gameDepth->Release();
    g_eyeNow = next;

    // Every 10 seconds, the eye's brightness read back (a frame or two later, never waiting) for the log.
    if (g_eyeReadPending) {
        D3D11_MAPPED_SUBRESOURCE eye;
        if (SUCCEEDED(g_context->Map(g_eyeReadback, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &eye))) {
            const float adapted = *static_cast<const float*>(eye.pData);
            g_context->Unmap(g_eyeReadback, 0);
            g_eyeReadPending = false;
            float lit = 1 + constants.exposure[1] * (constants.exposure[2] + constants.exposure[3] * (adapted - constants.exposure[2]));
            if (lit < 0.1f) lit = 0.1f;
            static int told = 0;
            if (told++ < 60)
                rfa::Log("Tone mapping at %.2f h: the eye's brightness %.3f, exposure %.3f (range %.3g, offset %.3g, base %.3g, amount %.3g, "
                         "Exposure=%.3g)", g_timeOfDay, adapted, constants.exposure[0] / lit * g_exposure, constants.exposure[0],
                         constants.exposure[1], constants.exposure[2], constants.exposure[3], g_exposure);
        }
    } else if (GetTickCount64() - g_lastEyeLog >= 10000) {
        g_lastEyeLog = GetTickCount64();
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
}

// During the light pass in our worlds, the first time its check runs (all is bound by then), the
// mod notes the light buffer it draws into and the G-buffer's normals (and, for the fog, its depth and
// the game's constants); once the pass is done it adds its light there.
bool g_inLightPass = false;
ID3D11RenderTargetView* g_lightBuffer = nullptr;
ID3D11ShaderResourceView* g_lightNormals = nullptr;

bool __cdecl LightCheck() {
    if (g_inLightPass && !g_lightBuffer) {
        g_context->OMGetRenderTargets(1, &g_lightBuffer, nullptr);
        g_context->PSGetShaderResources(13, 1, &g_lightNormals);
    }
    if (g_noteFog && !g_fogDepth) {
        g_context->PSGetShaderResources(14, 1, &g_fogDepth);
        g_context->PSGetConstantBuffers(4, 1, &g_gameConstants);
    }
    return g_lightCheck();
}

void __fastcall LightPass(void* renderer, void* unused) {
    const bool ours = g_context && InOurWorld();
    g_inLightPass = ours && g_lightOn && !g_lightFailed;
    DropFogInputs();
    g_noteFog = ours && g_fog > 0 && !g_fogFailed;
    g_lightPass(renderer, unused);
    g_noteFog = false;
    if (g_inLightPass) {
        g_inLightPass = false;
        if (!g_lightVertex && !MakeLightObjects()) {
            g_lightFailed = true;
        } else if (g_lightBuffer && g_lightNormals) {
            RFA_LOG_FIRST(1, "Light: lighting world \"%s\" with the sky's light and the sun", WorldName());
            DrawLight(g_lightBuffer, g_lightNormals);
        } else {
            RFA_LOG_FIRST(1, "Light: a light pass showed no light buffer or normals (this is logged once); no light added to it");
        }
    }
    if (g_lightBuffer) g_lightBuffer->Release();
    if (g_lightNormals) g_lightNormals->Release();
    g_lightBuffer = nullptr;
    g_lightNormals = nullptr;
}

// Replaces the scene renderer's sky step: the game's own, then, in our worlds, the clock,
// Guerrilla's fog over what the world has drawn, and Guerrilla's sky behind it.
void __fastcall RenderSkybox(void* renderer, void* unused) {
    g_renderSkybox(renderer, unused);
    RFA_LOG_FIRST(1, "Sky: the game's sky step runs (world \"%s\")", WorldName());
    const bool ours = InOurWorld();
    if (ours) {
        CheckSettings();
        TellAmbient();
    }
    RunClock(ours);
    RunStreaming(ours);
    if (ours && g_fog > 0 && !g_fogFailed && g_context) {
        if (!g_fogDepth || !g_gameConstants) {
            RFA_LOG_FIRST(1, "Fog: the light pass showed no depth or constants (this is logged once); no fog");
        } else if (!g_fogVertex && !MakeFogObjects()) {
            g_fogFailed = true;
        } else {
            RFA_LOG_FIRST(1, "Fog: drawing Guerrilla's fog in world \"%s\"", WorldName());
            DrawFog();
        }
    }
    DropFogInputs();
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
    rfa::Log("Composite: the last step (0x%X) drew into %s", kFinalComposite[which], text);
    ID3D11ShaderResourceView* views[8] = {};
    g_context->PSGetShaderResources(0, 8, views);
    for (int slot = 0; slot < 8; ++slot) {
        if (!views[slot]) continue;
        DescribeView(views[slot], text, sizeof(text));
        rfa::Log("Composite: t%d left bound: %s", slot, text);
        views[slot]->Release();
    }
}

void __fastcall FinalComposite0(void* compositor, void* unused, int flag) {
    g_finalComposite[0](compositor, unused, flag);
    if (!InOurWorld()) return;
    TellComposite(0);
    ToneMapAfterComposite();
}

void __fastcall FinalComposite1(void* compositor, void* unused, int flag) {
    g_finalComposite[1](compositor, unused, flag);
    if (!InOurWorld()) return;
    TellComposite(1);
    ToneMapAfterComposite();
}

// ---- The game's device

using CreateDeviceFn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
                                        ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
CreateDeviceFn g_createDevice;

// Notes the device the game makes (the last one, if it makes several), and keeps it alive.
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

}  // namespace

extern "C" __declspec(dllexport) void __cdecl RFAMod_Init() {
    if (!rfa::game::IsSupportedBuild()) {
        rfa::Log("Not the publicbeta build; nothing to do");
        return;
    }
    g_skyOn = rfa::IniInt(L"GuerrillaWorld", L"Sky", 1) != 0;
    g_cloudsOn = rfa::IniInt(L"GuerrillaWorld", L"Clouds", 1) != 0;
    g_worlds = SplitList(rfa::IniString(L"GuerrillaWorld", L"Worlds", L"rfg"));
    g_lightOn = rfa::IniInt(L"GuerrillaWorld", L"Light", 1) != 0;
    ReadBrightness();
    ReadClockSpeed();
    ReadZoneSize();
    ReadStreamRadius();
    ReadAmbient();
    ReadSun();
    ReadColourCorrection();
    ReadFog();
    ReadToneMapping();
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
    g_setAmbient = reinterpret_cast<SetAmbientFn>(rfa::HookFunction(kSetAmbient, kSetAmbientPrologue, reinterpret_cast<void*>(&SetAmbient)));
    g_setBackAmbient = reinterpret_cast<SetAmbientFn>(rfa::HookFunction(kSetBackAmbient, kSetAmbientPrologue, reinterpret_cast<void*>(&SetBackAmbient)));
    if (!g_setAmbient || !g_setBackAmbient) rfa::Log("Could not hook the time of day's ambient light; Ambient= does nothing");
    g_reportError = reinterpret_cast<ReportErrorFn>(rfa::HookFunction(kReportError, kReportErrorPrologue, reinterpret_cast<void*>(&ReportError)));
    if (!g_reportError) rfa::Log("Could not hook the game's error reports; the invalid mission hash lines stay");
    g_debugPrint = reinterpret_cast<DebugPrintFn>(rfa::HookFunction(kDebugPrint, kDebugPrintPrologue, reinterpret_cast<void*>(&DebugPrint)));
    if (!g_debugPrint) rfa::Log("Could not hook the game's debug print; what it says while loading our worlds isn't logged");
    g_precacheArea = reinterpret_cast<PrecacheAreaFn>(rfa::HookFunction(kPrecacheArea, kPrecacheAreaPrologue, reinterpret_cast<void*>(&PrecacheArea)));
    if (!g_precacheArea) rfa::Log("Could not hook the game's precache areas; our worlds' shared files won't be found");
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
    }
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
