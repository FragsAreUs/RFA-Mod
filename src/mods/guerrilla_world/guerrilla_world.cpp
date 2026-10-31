// What the world ported from Red Faction: Guerrilla needs that Armageddon doesn't do itself. So
// far: Guerrilla's sky and clouds.
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
#define RFA_MOD_NAME "guerrilla_world"
#include "rfa_mod.h"
#include "rfa_game.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <math.h>
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

// The scene renderer's sky step: a method (the renderer in ECX).
constexpr DWORD kRenderSkybox = 0xD528E0;
constexpr size_t kRenderSkyboxPrologue = 6;  // push ebp / mov ebp,esp / sub esp,3Ch
using RenderSkyboxFn = void(__fastcall*)(void* renderer, void* unused);
RenderSkyboxFn g_renderSkybox;

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

// Once a second: if rfa_mods.ini was saved since, read the brightness again.
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
}

bool InOurWorld() {
    const char* world = WorldName();
    for (const std::string& name : g_worlds)
        if (_stricmp(world, name.c_str()) == 0) return true;
    return false;
}

std::wstring DataFolder() { return rfa::RootDir() + L"\\mods\\guerrilla_world\\"; }

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

// ---- Drawing

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

// Compiles the sky's shaders and makes the states it draws with, once the game has its device.
bool MakeSkyObjects() {
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    g_compile = compiler ? reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
    if (!g_compile) {
        rfa::Log("Could not load Windows' shader compiler (d3dcompiler_47.dll); no sky");
        return false;
    }
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
    for (int c = 0; c < 10; ++c) {
        float colour[3];
        TodColour(kSkyCurves[c], colour);
        for (int i = 0; i < 3; ++i) hemisphere[c][i] = colour[i] * g_brightness;
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
    float length = sqrtf(g_lightDirection[0] * g_lightDirection[0] + g_lightDirection[1] * g_lightDirection[1] + g_lightDirection[2] * g_lightDirection[2]);
    for (int i = 0; i < 3; ++i) constants.light[i] = length > 0 ? g_lightDirection[i] / length : (i == 1 ? 1.0f : 0.0f);
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
    for (const CloudPart& part : g_cloudParts) {
        float colour[3];
        if (part.kind == 2) {
            TodColour(kMountainFront, colour);
            for (int i = 0; i < 3; ++i) constants.front[i] = colour[i] * g_brightness;
            TodColour(kMountainBack, colour);
            for (int i = 0; i < 3; ++i) constants.rear[i] = colour[i] * g_brightness;
            TodColour(kMountainFog, colour);  // mixed into the picture before it is lit
            for (int i = 0; i < 3; ++i) constants.strengths[i] = colour[i];
            constants.strengths[3] = TodNumber(kMountainFogDensity);
            if (g_tellColours)
                rfa::Log("Backdrop: lit side %.3f %.3f %.3f, far side %.3f %.3f %.3f, haze %.3f %.3f %.3f at %.2f", constants.front[0],
                         constants.front[1], constants.front[2], constants.rear[0], constants.rear[1], constants.rear[2],
                         constants.strengths[0], constants.strengths[1], constants.strengths[2], constants.strengths[3]);
        } else {
            TodColour(kCloudFront[part.kind], colour);
            for (int i = 0; i < 3; ++i) constants.front[i] = colour[i] * g_brightness;
            TodColour(kCloudRear[part.kind], colour);
            for (int i = 0; i < 3; ++i) constants.rear[i] = colour[i] * g_brightness;
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

// Everything on the device context the sky and clouds change, to put back afterwards.
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
    ID3D11Buffer* pixelConstants = nullptr;
    ID3D11ShaderResourceView* pixelResources[2] = {};
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
        g_context->PSGetConstantBuffers(0, 1, &pixelConstants);
        g_context->PSGetShaderResources(0, 2, pixelResources);
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
        g_context->PSSetConstantBuffers(0, 1, &pixelConstants);
        g_context->PSSetShaderResources(0, 2, pixelResources);
        g_context->PSSetSamplers(0, 1, &pixelSampler);
        g_context->OMSetBlendState(blend, blendFactor, sampleMask);
        g_context->RSSetState(raster);
        for (IUnknown* held : {static_cast<IUnknown*>(layout), static_cast<IUnknown*>(vertexBuffer), static_cast<IUnknown*>(indexBuffer),
                               static_cast<IUnknown*>(vertexShader), static_cast<IUnknown*>(pixelShader), static_cast<IUnknown*>(geometryShader),
                               static_cast<IUnknown*>(vertexConstants), static_cast<IUnknown*>(pixelConstants),
                               static_cast<IUnknown*>(pixelResources[0]), static_cast<IUnknown*>(pixelResources[1]),
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

// Replaces the scene renderer's sky step: the game's own, then Guerrilla's sky in our worlds.
void __fastcall RenderSkybox(void* renderer, void* unused) {
    g_renderSkybox(renderer, unused);
    RFA_LOG_FIRST(1, "Sky: the game's sky step runs (world \"%s\")", WorldName());
    if (!g_skyOn || g_drawingFailed || !g_context || !InOurWorld()) return;
    CheckSettings();
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
    ReadBrightness();
    if (!g_skyOn) {
        rfa::Log("Guerrilla's sky is off (Sky=0)");
        return;
    }
    if (!rfa::HookImport(GetModuleHandleW(nullptr), "d3d11.dll", "D3D11CreateDevice", &CreateDevice, &g_createDevice)) {
        rfa::Log("Could not watch for the game's Direct3D device; no sky");
        return;
    }
    g_renderSkybox = reinterpret_cast<RenderSkyboxFn>(
        rfa::HookFunction(kRenderSkybox, kRenderSkyboxPrologue, reinterpret_cast<void*>(&RenderSkybox)));
    rfa::Log(g_renderSkybox ? (g_cloudsOn ? "Guerrilla's sky and clouds are drawn in the worlds named by Worlds= in [GuerrillaWorld]"
                                          : "Guerrilla's sky is drawn (no clouds) in the worlds named by Worlds= in [GuerrillaWorld]")
                            : "Could not hook the game's sky step; no sky");
}
