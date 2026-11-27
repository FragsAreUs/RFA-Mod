// Guerrilla's fog of war (see fog_of_war.h), as Re-Mars-tered has it:
//   - the grid (Fow_bit_array, a bit_array of 0x40000 bits): 512 × 512 cells of 16 m, 1 hidden. fog_of_war_init (RM
//     0x850AB0, from fullscreen_map_init) clears every bit to hidden and puts the grid's corner at zone (0, 0)'s
//     smallest x and z (rfg_zonex_get_zone_bbox(0, 0): zone (column, row) spans x (column − 8) × 511 and
//     z (7 − row) × 511 to a zone further), x −4088, z 3577; a new game clears it again (fog_of_war_clear, from the main
//     menu's new game). So the grid reaches 8,192 m east and south of that corner; the zones' northmost row of metres
//     (z above 3577) is outside it, as in Guerrilla.
//   - a point's cell (fow_get_bit_index 0x83F6E0): column (x − corner x) / 16, row (corner z − z) / 16, each cut to a
//     whole number towards 0, and none (−1) unless both are 0 to 511; index row × 512 + column.
//   - uncovering (fog_of_war_update(position, radius) 0x83F800; cartographer::process calls it every frame with the
//     player's position and 100 m): over the square of ±2 × radius round the position, in 16 m steps from its corner,
//     each step's point and the point 8 m further in x and z each uncover their cell when within the radius.
//   - under the fog (fog_of_war_is_posn_obscured 0x83FAF0): a hidden cell, or no cell at all.
//   - the save (fog_of_war_save 0x83FC30 / fog_of_war_load 0x83FCC0, from ui_system_save / ui_system_load): uint16 16
//     (the cell's metres), uint16 0x400, uint32 0x40000 twice, uint32 0x8000, then the bits 8 to a byte (the first
//     the lowest), then int32 −1. The load reads the header past, every bit hidden first, then the bits.
#define RFA_MOD_NAME "guerrilla_hud"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "fog_of_war.h"
#include "world/world_api.h"
#include "rfa_game.h"
#include <d3d11.h>
#include <d3dcompiler.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace fog_of_war {
namespace {

constexpr float kRevealRadius = 100.0f;  // cartographer::process's (RM 0x87CF8E)
constexpr int kSaveHeader = 2 + 2 + 4 * 3;
constexpr int kSaveBytes = kSaveHeader + kBitBytes + 4;

std::mutex g_lock;
unsigned char g_bits[kBitBytes];
unsigned g_revision = 0;
bool g_made = false;

void HideAll() {
    memset(g_bits, 0xFF, sizeof(g_bits));
    ++g_revision;
}

void MakeOnce() {
    if (g_made) return;
    g_made = true;
    HideAll();  // fog_of_war_init: clear(true)
}

// fow_get_bit_index: the cell of a point, −1 for none.
int CellIndex(float x, float z) {
    const int column = static_cast<int>(static_cast<double>(static_cast<float>(static_cast<double>(x) - kLeft)) * 0.0625);
    if (static_cast<unsigned>(column) > 511) return -1;
    const int row = static_cast<int>(static_cast<double>(static_cast<float>(static_cast<double>(kTop) - z)) * 0.0625);
    if (static_cast<unsigned>(row) > 511) return -1;
    return row * kCells + column;
}

bool Hidden(int index) { return (g_bits[index >> 3] >> (index & 7)) & 1; }

void Uncover(int index) {
    if (index < 0 || !Hidden(index)) return;
    g_bits[index >> 3] &= static_cast<unsigned char>(~(1u << (index & 7)));
    ++g_revision;
}

float Squared(float a, float b) {
    return static_cast<float>(static_cast<double>(a) * a + static_cast<double>(b) * b);
}

}  // namespace

// fog_of_war_update(position, radius), as RM 0x83F800 steps it.
void Update(const float* position, float radius) {
    if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2])) return;  // vector::is_valid
    const float px = position[0], pz = position[2];
    const float radiusSquared = static_cast<float>(static_cast<double>(radius) * radius);
    const double reach = static_cast<double>(radius) * 2.0;
    const float left = static_cast<float>(px - reach), right = static_cast<float>(px + reach);
    const float bottom = static_cast<float>(pz - reach), top = static_cast<float>(pz + reach);
    std::lock_guard<std::mutex> hold(g_lock);
    MakeOnce();
    for (float x = left; right >= x; x = static_cast<float>(x + 16.0)) {
        const float centreX = static_cast<float>(x + 8.0);
        for (float z = bottom; top >= z; z = static_cast<float>(z + 16.0)) {
            const float centreZ = static_cast<float>(z + 8.0);
            if (radiusSquared >= Squared(px - x, pz - z)) Uncover(CellIndex(x, z));
            if (radiusSquared >= Squared(px - centreX, pz - centreZ)) Uncover(CellIndex(centreX, centreZ));
        }
    }
}

void Reveal(const float* position) { Update(position, kRevealRadius); }

bool IsObscured(float x, float z) {
    const int index = CellIndex(x, z);
    if (index < 0) return true;
    std::lock_guard<std::mutex> hold(g_lock);
    MakeOnce();
    return Hidden(index);
}

unsigned Revision() {
    std::lock_guard<std::mutex> hold(g_lock);
    return g_revision;
}

// The bits and their revision, copied together (for a thread other than the game's: the map's draw).
unsigned CopyBits(unsigned char* out) {
    std::lock_guard<std::mutex> hold(g_lock);
    MakeOnce();
    memcpy(out, g_bits, sizeof(g_bits));
    return g_revision;
}

}  // namespace fog_of_war

int GuerrillaHud_FogOfWarSave(unsigned char* buffer, int room) {
    using namespace fog_of_war;
    if (!guerrilla_hud::Running()) return 0;
    if (!buffer || room < kSaveBytes) return kSaveBytes;
    const unsigned short cell = 16, second = 0x400;
    const unsigned header[3] = {0x40000, 0x40000, 0x8000};
    const int end = -1;
    memcpy(buffer, &cell, 2);
    memcpy(buffer + 2, &second, 2);
    memcpy(buffer + 4, header, sizeof(header));
    {
        std::lock_guard<std::mutex> hold(g_lock);
        MakeOnce();
        memcpy(buffer + kSaveHeader, g_bits, kBitBytes);
    }
    memcpy(buffer + kSaveHeader + kBitBytes, &end, 4);
    return kSaveBytes;
}

void GuerrillaHud_FogOfWarLoad(const unsigned char* data, int size) {
    using namespace fog_of_war;
    if (!guerrilla_hud::Running()) return;
    std::lock_guard<std::mutex> hold(g_lock);
    g_made = true;
    HideAll();
    if (!data || size <= 0) {
        rfa::LogDetail("Fog of war: every cell hidden (a fresh world, or a save without the map's fog)");
        return;
    }
    if (size < kSaveHeader + kBitBytes) {
        rfa::Log("Fog of war: the save's block is %d bytes, short of the %d it needs; every cell hidden", size,
                 kSaveHeader + kBitBytes);
        return;
    }
    memcpy(g_bits, data + kSaveHeader, kBitBytes);
    ++g_revision;
    int hidden = 0;
    for (unsigned char byte : g_bits)
        for (int bit = 0; bit < 8; ++bit) hidden += (byte >> bit) & 1;
    rfa::LogDetail("Fog of war: the save's grid back, %d of %d cells uncovered", kCells * kCells - hidden, kCells * kCells);
}

// ---- The fog on the world map
//
// Guerrilla draws it in fullscreen_map_render (RM 0x876C10) after the map picture and the icons it draws under the
// fog, and before the districts and the map's interface document (its icons): fog_of_war_render(view, window)
// (0x867010) → fog_of_war_render_fog_layer (0x866C30), one quad over the map's window on screen with its own shader
// (ui_fog_of_war), in alpha-blend mode, no depth test:
//   - the mask (fog_of_war_update_mask_texture 0x850A10): 512 × 512, one texel a cell, 255 hidden, 0 uncovered,
//     row 0 north; made again as the map opens (here whenever a cell changed: the map pauses the game, so the same);
//   - the quad's mask coordinates: u = (x − grid x) / 16 / 511, v = (grid z − z) / 16 / 511 at the view's edges
//     (511, as Guerrilla divides);
//   - the shader: alpha = the mask's 3 × 3 texels round the point (1/512 apart) averaged × opacity; colour = the cloud
//     picture (ui_fsmap_fog.tga) at (uv + offset1) × 4 tinted (0.5, 0.3, 0.2) + at (uv + offset2) × 5 tinted
//     (0.22, 0.12, 0.05) (its base colour black, the draw's tint white); bilinear, wrapping;
//   - the clouds drift: each frame the offsets grow by the real frame time × (0.005, −0.007) and (0.01, 0.008);
//     the first starts at 0 once, the second at (1, 1) each time the map opens (fog_of_war_rendering_alloc 0x87A5B0);
//   - opacity 0.8 + 0.05 × sin(min(zoom × 10 × 1.57077, 1.57077)), zoom Guerrilla's 0 to 1 (0.80 zoomed all the
//     way out, 0.85 from a tenth in).
// Guerrilla has no switch for it (Fog_of_war_render, always 1, nothing sets it) and draws no fog on its minimap.
//
// Here our world map is a Vint document (rfg_ui_fullscreen.lua), so the quad is drawn where its element
// rfg_fullmap_fog comes in the document's draw order: a `map` element (depth 7: over the world picture and the zone
// pictures, under the route, the icons and the player), whose drawing (vint_element_map::render_object, 0xC6CA00,
// thiscall(params, 2 more)) only calls the interface's draw_map callback (0xC4ED90(0): Armageddon's compass).
// The interface doesn't draw as it goes: its pictures are quads put through rl_draw (begin 0xD1AF70 with the
// interface's primitive state 0x137B88C, allocate 0xD1B010, end 0xD1B040, as its draw_bitmap 0x5B5520 does), and
// the primitive renderer (the instance at [0x12F8478]; RM rl_primitive_renderer) records them and plays them back
// later on the render thread, each through its render_primitive (vtable +0x24 of its Direct3D 11 class, vtable
// 0x106E0C0: 0xC4B0E0, thiscall(type, data offset, primitive count, ?, from the recorded buffer, state), ret 18h; the
// vertices at [+0x1F8] (recorded) or [+0x1DC] + the offset). A first try drew the fog straight from the element's
// drawing: under everything the interface then played back, so not seen (2026-11-26 03:00). So:
//   - the element's drawing is replaced: for our element (its handle at +0x20, which vint_object_create gives the
//     script, 0x885FB0) it records a quad of its own the way draw_bitmap does, all four corners on one point (nothing
//     drawn whatever the blend), marked in its first corner's two unused floats (+8, +0xC of the 28-byte vertex);
//     any other map element goes on to the callback as before;
//   - render_primitive is wrapped: after the game's own (which also moves the recorded buffer along), a two-triangle
//     primitive with that mark has the fog drawn there, with the game's own Direct3D 11 device, into what the
//     interface draws on at that point, and everything changed on the device context put back.
// The script tells the mod the element and the view each frame (rfg_map_fog).
namespace fog_of_war {
namespace {

constexpr DWORD kMapElementRender = 0xC6CA00;
constexpr BYTE kMapElementRenderCode[] = {0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x6A, 0x00, 0xE8};  // push ebp ... push 0 / call
constexpr DWORD kDrawMapCallback = 0xC4ED90;  // cdecl(int): the interface's render callbacks' +0x50, if set
constexpr DWORD kElementHandle = 0x20;
constexpr DWORD kRenderPrimitive = 0xC4B0E0;
constexpr BYTE kRenderPrimitiveCode[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x2B, 0x25, 0xF1, 0x00};  // push ebp ... push 0F1252Bh
constexpr size_t kRenderPrimitivePrologue = sizeof(kRenderPrimitiveCode);
constexpr DWORD kRecordedVertices = 0x1F8;   // the primitive renderer's: the recorded buffer's vertices
constexpr DWORD kImmediateVertices = 0x1DC;  // and the immediate one's
constexpr DWORD kDrawInstance = 0x4FD5C0;    // cdecl: rl_draw
constexpr DWORD kDrawBegin = 0xD1AF70;       // thiscall(state)
constexpr DWORD kDrawAllocate = 0xD1B010;    // thiscall(triangles) -> vertices (28 bytes each), or null
constexpr DWORD kDrawEnd = 0xD1B040;         // thiscall()
constexpr DWORD kInterfaceState = 0x137B88C;  // the interface's primitive state, as draw_bitmap begins with
constexpr DWORD kMark[2] = {0x474F4652, 0x4B52414D};  // "RFOG", "MARK"
constexpr double kZoneMetres = 511.0;          // as guerrilla_hud's map: zone (column, row) from x (column − 8) × 511,
constexpr double kZonePixels = 1024.0 / 12.0;  // z (7 − row) × 511; the world map picture 1024 pixels over zones 2-13
constexpr int kFirstZone = 2;
constexpr ULONGLONG kOpenedAfterMs = 500;  // no word from the script for this long: the map was closed

// What the script says each frame, for the render thread.
struct View {
    DWORD element = 0;                // rfg_fullmap_fog's handle, 0 none
    double centreX = 0, centreY = 0;  // the world map picture's pixel in the middle of the screen
    double view = 1;                  // how many of its pixels the screen's height shows
    double zoom = 0;                  // Guerrilla's zoom, 0 to 1
    float alpha = 1;                  // the element's alpha as last recorded (the map's fade-in)
    unsigned opened = 0;              // goes up each time the map opens
};
std::mutex g_viewLock;
View g_view;
ULONGLONG g_lastWord = 0;
volatile LONG g_marksRecorded = 0;  // marked quads recorded and not yet played back (only then is the playback looked at)

// The render thread's own.
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11VertexShader* g_vertex = nullptr;
ID3D11PixelShader* g_pixel = nullptr;
ID3D11Buffer* g_constants = nullptr;
ID3D11Texture2D* g_mask = nullptr;
ID3D11ShaderResourceView* g_maskView = nullptr;
ID3D11ShaderResourceView* g_clouds = nullptr;
ID3D11SamplerState* g_sampler = nullptr;
ID3D11BlendState* g_blend = nullptr;
ID3D11DepthStencilState* g_depth = nullptr;
ID3D11RasterizerState* g_raster[2] = {};  // without and with the scissor test (as the game's own state has it)
bool g_failed = false;
unsigned g_maskRevision = ~0u;
unsigned g_seenOpened = 0;
float g_offset1[2] = {0, 0};
float g_offset2[2] = {1, 1};
LARGE_INTEGER g_lastDraw = {};
std::vector<unsigned char> g_maskBytes;

const char kFogShader[] = R"(
cbuffer Fog : register(b0) {
    float4 MaskRect;    // the quad's mask coordinates: left, top, right, bottom
    float4 Layer1;      // Fog_layer1_offset_scale: scale x, y, offset x, y
    float4 Layer2;      // Fog_layer2_offset_scale
    float4 Tint1;       // Fog_layer1_tint
    float4 Tint2;       // Fog_layer2_tint
    float4 Opacity;     // x Fog_opacity; y 1 when the target isn't sRGB (the colour is written sRGB-encoded)
};
Texture2D Mask : register(t0);      // Fog_of_war_mask
Texture2D Clouds : register(t1);    // Fog_texture (ui_fsmap_fog.tga)
SamplerState Linear : register(s0);

void FogVertex(uint id : SV_VertexID, out float4 position : SV_Position, out float2 uv : TEXCOORD0) {
    float2 corner = float2(id & 1, id >> 1);
    position = float4(corner.x * 2 - 1, 1 - corner.y * 2, 0, 1);
    uv = lerp(MaskRect.xy, MaskRect.zw, corner);
}

float4 FogPixel(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    const float texel = 0.001953;
    float hidden = 0;
    [unroll] for (int y = -1; y <= 1; y++)
        [unroll] for (int x = -1; x <= 1; x++)
            hidden += Mask.Sample(Linear, uv + float2(x, y) * texel).r;
    float alpha = hidden * Opacity.x * 0.111111;
    float3 layer1 = Clouds.Sample(Linear, (uv + Layer1.zw) * Layer1.xy).rgb;
    float3 layer2 = Clouds.Sample(Linear, (uv + Layer2.zw) * Layer2.xy).rgb;
    float3 colour = layer1 * Tint1.rgb + layer2 * Tint2.rgb;
    if (Opacity.y > 0.5)
        colour = colour <= 0.0031308 ? colour * 12.92 : 1.055 * pow(colour, 1.0 / 2.4) - 0.055;
    return float4(colour, alpha);
}
)";

struct Constants {
    float maskRect[4];
    float layer1[4];
    float layer2[4];
    float tint1[4];
    float tint2[4];
    float opacity[4];
};

ID3DBlob* Compile(pD3DCompile compile, const char* entry, const char* target) {
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    if (FAILED(compile(kFogShader, sizeof(kFogShader) - 1, "fog_of_war", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                       &code, &errors))) {
        rfa::Log("Fog of war: the shader %s didn't compile: %s", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
        code = nullptr;
    }
    if (errors) errors->Release();
    return code;
}

// Guerrilla's cloud picture (mods\guerrilla_hud\ui_fsmap_fog.dds, which GuerrillaBuilder.exe writes: 32-bit blue,
// green, red, alpha, with its mip levels), read as sRGB colour as Guerrilla's renderer reads its pictures.
ID3D11ShaderResourceView* LoadClouds() {
    const std::wstring path = rfa::RootDir() + L"\\mods\\guerrilla_hud\\ui_fsmap_fog.dds";
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) return nullptr;
    std::vector<unsigned char> dds;
    unsigned char chunk[65536];
    for (size_t got; (got = fread(chunk, 1, sizeof(chunk), file)) > 0;) dds.insert(dds.end(), chunk, chunk + got);
    fclose(file);
    auto u32 = [&](size_t at) {
        UINT value;
        memcpy(&value, dds.data() + at, 4);
        return value;
    };
    if (dds.size() < 128 || memcmp(dds.data(), "DDS ", 4) != 0) return nullptr;
    const UINT height = u32(12), width = u32(16), mips = u32(28) ? u32(28) : 1;
    if (u32(84) != 0 || u32(88) != 32 || u32(92) != 0x00FF0000) return nullptr;  // only 32-bit blue-green-red-alpha
    std::vector<D3D11_SUBRESOURCE_DATA> levels(mips);
    size_t at = 128;
    for (UINT level = 0; level < mips; ++level) {
        const UINT w = (width >> level) ? width >> level : 1, h = (height >> level) ? height >> level : 1;
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

// Everything the draw needs, made the first time (false, said once, if something can't be).
bool MakeObjects() {
    if (g_failed) return false;
    if (g_vertex) return true;
    g_device = static_cast<ID3D11Device*>(GuerrillaWorld_D3DDevice());
    if (!g_device) return false;  // not yet (or guerrilla_world isn't running): asked again next frame
    g_failed = true;
    g_device->GetImmediateContext(&g_context);
    g_clouds = LoadClouds();
    if (!g_clouds) {
        rfa::Log("Fog of war: no mods\\guerrilla_hud\\ui_fsmap_fog.dds (GuerrillaBuilder.exe build hud writes it); the world map has no fog");
        return false;
    }
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    const auto compile = compiler ? reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
    if (!compile) {
        rfa::Log("Fog of war: could not load Windows' shader compiler (d3dcompiler_47.dll); the world map has no fog");
        return false;
    }
    ID3DBlob* vertex = Compile(compile, "FogVertex", "vs_4_0");
    ID3DBlob* pixel = Compile(compile, "FogPixel", "ps_4_0");
    bool ok = vertex && pixel &&
              SUCCEEDED(g_device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &g_vertex)) &&
              SUCCEEDED(g_device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &g_pixel));
    if (vertex) vertex->Release();
    if (pixel) pixel->Release();

    D3D11_BUFFER_DESC buffer = {};
    buffer.ByteWidth = sizeof(Constants);
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(g_device->CreateBuffer(&buffer, nullptr, &g_constants));

    D3D11_TEXTURE2D_DESC mask = {};  // Guerrilla's: 512 × 512, 8 bits (RLCDT_FORMAT_A8)
    mask.Width = mask.Height = kCells;
    mask.MipLevels = mask.ArraySize = 1;
    mask.Format = DXGI_FORMAT_R8_UNORM;
    mask.SampleDesc.Count = 1;
    mask.Usage = D3D11_USAGE_DEFAULT;
    mask.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ok = ok && SUCCEEDED(g_device->CreateTexture2D(&mask, nullptr, &g_mask)) &&
         SUCCEEDED(g_device->CreateShaderResourceView(g_mask, nullptr, &g_maskView));

    D3D11_SAMPLER_DESC sampler = {};  // bilinear, wrapping (GM_FILTER's clamp mode 0)
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(g_device->CreateSamplerState(&sampler, &g_sampler));

    D3D11_BLEND_DESC blend = {};  // GM_FILTER: ALPHA_BLEND_FILTER
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(g_device->CreateBlendState(&blend, &g_blend));

    D3D11_DEPTH_STENCIL_DESC depth = {};  // no depth test or write (zbuf mode none)
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    ok = ok && SUCCEEDED(g_device->CreateDepthStencilState(&depth, &g_depth));

    D3D11_RASTERIZER_DESC raster = {};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(g_device->CreateRasterizerState(&raster, &g_raster[0]));
    raster.ScissorEnable = TRUE;
    ok = ok && SUCCEEDED(g_device->CreateRasterizerState(&raster, &g_raster[1]));
    if (!ok) {
        rfa::Log("Fog of war: could not make what it draws with; the world map has no fog");
        return false;
    }
    g_maskBytes.resize(size_t(kCells) * kCells);
    QueryPerformanceCounter(&g_lastDraw);
    g_failed = false;
    return true;
}

// The mask again from the grid (fog_of_war_update_mask_texture: 255 hidden, 0 uncovered), if a cell changed.
void UpdateMask() {
    static unsigned char bits[kBitBytes];
    const unsigned revision = CopyBits(bits);
    if (revision == g_maskRevision) return;
    g_maskRevision = revision;
    for (int index = 0; index < kCells * kCells; ++index) g_maskBytes[index] = ((bits[index >> 3] >> (index & 7)) & 1) ? 0xFF : 0x00;
    g_context->UpdateSubresource(g_mask, 0, nullptr, g_maskBytes.data(), kCells, 0);
}

// Everything on the device context the draw changes, to put back afterwards.
struct Saved {
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11InputLayout* layout = nullptr;
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11GeometryShader* geometryShader = nullptr;
    ID3D11Buffer* vertexConstants = nullptr;
    ID3D11Buffer* pixelConstants = nullptr;
    ID3D11ShaderResourceView* resources[2] = {};
    ID3D11SamplerState* sampler = nullptr;
    ID3D11BlendState* blend = nullptr;
    float blendFactor[4] = {};
    UINT sampleMask = 0;
    ID3D11DepthStencilState* depth = nullptr;
    UINT stencilRef = 0;
    ID3D11RasterizerState* raster = nullptr;

    void Save() {
        g_context->IAGetPrimitiveTopology(&topology);
        g_context->IAGetInputLayout(&layout);
        g_context->VSGetShader(&vertexShader, nullptr, nullptr);
        g_context->PSGetShader(&pixelShader, nullptr, nullptr);
        g_context->GSGetShader(&geometryShader, nullptr, nullptr);
        g_context->VSGetConstantBuffers(0, 1, &vertexConstants);
        g_context->PSGetConstantBuffers(0, 1, &pixelConstants);
        g_context->PSGetShaderResources(0, 2, resources);
        g_context->PSGetSamplers(0, 1, &sampler);
        g_context->OMGetBlendState(&blend, blendFactor, &sampleMask);
        g_context->OMGetDepthStencilState(&depth, &stencilRef);
        g_context->RSGetState(&raster);
    }

    void Restore() {
        g_context->IASetPrimitiveTopology(topology);
        g_context->IASetInputLayout(layout);
        g_context->VSSetShader(vertexShader, nullptr, 0);
        g_context->PSSetShader(pixelShader, nullptr, 0);
        g_context->GSSetShader(geometryShader, nullptr, 0);
        g_context->VSSetConstantBuffers(0, 1, &vertexConstants);
        g_context->PSSetConstantBuffers(0, 1, &pixelConstants);
        g_context->PSSetShaderResources(0, 2, resources);
        g_context->PSSetSamplers(0, 1, &sampler);
        g_context->OMSetBlendState(blend, blendFactor, sampleMask);
        g_context->OMSetDepthStencilState(depth, stencilRef);
        g_context->RSSetState(raster);
        for (IUnknown* held : {static_cast<IUnknown*>(layout), static_cast<IUnknown*>(vertexShader), static_cast<IUnknown*>(pixelShader),
                               static_cast<IUnknown*>(geometryShader), static_cast<IUnknown*>(vertexConstants),
                               static_cast<IUnknown*>(pixelConstants), static_cast<IUnknown*>(resources[0]),
                               static_cast<IUnknown*>(resources[1]), static_cast<IUnknown*>(sampler), static_cast<IUnknown*>(blend),
                               static_cast<IUnknown*>(depth), static_cast<IUnknown*>(raster)})
            if (held) held->Release();
    }
};

bool IsSrgb(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
           format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
}

// A point of the world map picture (its pixels) as Guerrilla's mask coordinates.
void MaskCoordinates(double mapX, double mapY, float& u, float& v) {
    const double x = (mapX / kZonePixels + kFirstZone - 8.0) * kZoneMetres;  // guerrilla_hud's MapPixels, backwards
    const double z = (8.0 - kFirstZone - mapY / kZonePixels) * kZoneMetres;
    u = static_cast<float>((x - kLeft) * 0.0625 / 511.0);
    v = static_cast<float>((kTop - z) * 0.0625 / 511.0);
}

// fog_of_war_render_fog_layer, over the whole of what the interface draws on (our world map fills the screen).
void DrawFog(const View& view) {
    if (!MakeObjects()) return;
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    double seconds = static_cast<double>(now.QuadPart - g_lastDraw.QuadPart) / static_cast<double>(frequency.QuadPart);
    g_lastDraw = now;
    if (view.opened != g_seenOpened) {  // the map opened: fog_of_war_rendering_alloc
        g_seenOpened = view.opened;
        g_offset2[0] = g_offset2[1] = 1.0f;
        seconds = 0;
    }
    seconds = seconds < 0 ? 0 : seconds > 0.25 ? 0.25 : seconds;
    g_offset1[0] = static_cast<float>(g_offset1[0] + seconds * 0.005);
    g_offset1[1] = static_cast<float>(g_offset1[1] + seconds * -0.007);
    g_offset2[0] = static_cast<float>(g_offset2[0] + seconds * 0.01);
    g_offset2[1] = static_cast<float>(g_offset2[1] + seconds * 0.008);

    ID3D11RenderTargetView* target = nullptr;
    g_context->OMGetRenderTargets(1, &target, nullptr);
    if (!target) return;
    D3D11_RENDER_TARGET_VIEW_DESC targetDesc = {};
    target->GetDesc(&targetDesc);
    target->Release();
    D3D11_VIEWPORT viewport = {};
    UINT viewports = 1;
    g_context->RSGetViewports(&viewports, &viewport);
    if (viewports == 0 || viewport.Width <= 0 || viewport.Height <= 0) return;

    const float alpha = view.alpha;
    static bool told = false;
    if (!told) {
        told = true;
        rfa::LogDetail("Fog of war: drawing on the world map as its marked quad plays back, a %u-format target, %.0f x %.0f at (%.0f, %.0f); "
                       "the element's alpha %g",
                       static_cast<unsigned>(targetDesc.Format), viewport.Width, viewport.Height, viewport.TopLeftX, viewport.TopLeftY, alpha);
    }
    UpdateMask();
    // The screen's edges on the world map picture: its height shows view pixels, centred on (centreX, centreY).
    const double perPixel = view.view / viewport.Height;
    const double left = view.centreX - viewport.Width * 0.5 * perPixel, right = view.centreX + viewport.Width * 0.5 * perPixel;
    const double top = view.centreY - viewport.Height * 0.5 * perPixel, bottom = view.centreY + viewport.Height * 0.5 * perPixel;
    Constants c = {};
    MaskCoordinates(left, top, c.maskRect[0], c.maskRect[1]);
    MaskCoordinates(right, bottom, c.maskRect[2], c.maskRect[3]);
    c.layer1[0] = c.layer1[1] = 4.0f;  // Fow_layer1_scale
    c.layer1[2] = g_offset1[0];
    c.layer1[3] = g_offset1[1];
    c.layer2[0] = c.layer2[1] = 5.0f;  // Fow_layer2_scale
    c.layer2[2] = g_offset2[0];
    c.layer2[3] = g_offset2[1];
    c.tint1[0] = 0.5f, c.tint1[1] = 0.3f, c.tint1[2] = 0.2f;     // Fow_layer1_tint
    c.tint2[0] = 0.22f, c.tint2[1] = 0.12f, c.tint2[2] = 0.05f;  // Fow_layer2_tint
    const double angle = std::fmin(view.zoom * 10.0 * 1.57077, 1.57077);
    c.opacity[0] = static_cast<float>((0.8 + 0.05 * std::sin(angle)) * alpha);
    c.opacity[1] = IsSrgb(targetDesc.Format) ? 0.0f : 1.0f;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(g_context->Map(g_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    memcpy(mapped.pData, &c, sizeof(c));
    g_context->Unmap(g_constants, 0);

    Saved saved;
    saved.Save();
    D3D11_RASTERIZER_DESC gameRaster = {};
    if (saved.raster) saved.raster->GetDesc(&gameRaster);  // keep the interface's clip (scissor) if it has one on
    ID3D11ShaderResourceView* resources[2] = {g_maskView, g_clouds};
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    g_context->IASetInputLayout(nullptr);
    g_context->VSSetShader(g_vertex, nullptr, 0);
    g_context->PSSetShader(g_pixel, nullptr, 0);
    g_context->GSSetShader(nullptr, nullptr, 0);
    g_context->VSSetConstantBuffers(0, 1, &g_constants);
    g_context->PSSetConstantBuffers(0, 1, &g_constants);
    g_context->PSSetShaderResources(0, 2, resources);
    g_context->PSSetSamplers(0, 1, &g_sampler);
    const float factor[4] = {1, 1, 1, 1};
    g_context->OMSetBlendState(g_blend, factor, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(g_depth, 0);
    g_context->RSSetState(g_raster[gameRaster.ScissorEnable ? 1 : 0]);
    g_context->Draw(4, 0);
    saved.Restore();
}

// vint_element_map::render_object (0xC6CA00), replaced: our fog element records its marked quad (see above), any other
// goes on to the interface's draw_map callback as the game's own did.
void __fastcall MapElementRender(BYTE* element, void* /*unused*/, const float* params, int /*b*/, int /*c*/) {
    bool ours = false;
    if (element) {
        std::lock_guard<std::mutex> hold(g_viewLock);
        ours = g_view.element != 0 && *reinterpret_cast<const DWORD*>(element + kElementHandle) == g_view.element;
        if (ours) {
            // The element's alpha (the map's fade-in), as the interface hands it on (vint_render_params +0x50, as
            // Guerrilla's lays it out; an element at 0 isn't drawn at all, so anything else means another layout: 1).
            const float alpha = params ? params[0x50 / 4] : 1.0f;
            g_view.alpha = alpha > 0.0f && alpha <= 1.0f ? alpha : 1.0f;
        }
    }
    if (!ours) {
        reinterpret_cast<void(__cdecl*)(int)>(kDrawMapCallback)(0);
        return;
    }
    void* draw = reinterpret_cast<void*(__cdecl*)()>(kDrawInstance)();
    if (!draw) return;
    reinterpret_cast<void(__fastcall*)(void*, void*, void*)>(kDrawBegin)(draw, nullptr, reinterpret_cast<void*>(kInterfaceState));
    DWORD* vertices = reinterpret_cast<DWORD*(__fastcall*)(void*, void*, int)>(kDrawAllocate)(draw, nullptr, 2);
    if (vertices) {
        memset(vertices, 0, 4 * 28);  // four corners at (0, 0), no colour
        vertices[2] = kMark[0];
        vertices[3] = kMark[1];
        InterlockedIncrement(&g_marksRecorded);
    }
    reinterpret_cast<void(__fastcall*)(void*, void*)>(kDrawEnd)(draw, nullptr);
}

// render_primitive (0xC4B0E0), wrapped: the game's own, then the fog where our marked quad plays back.
using RenderPrimitiveFn = void(__fastcall*)(BYTE* renderer, void* unused, int type, int offset, int primitives, int d, int recorded,
                                            DWORD state);
RenderPrimitiveFn g_renderPrimitive = nullptr;

void __fastcall RenderPrimitive(BYTE* renderer, void* unused, int type, int offset, int primitives, int d, int recorded, DWORD state) {
    bool marked = false;
    if (primitives == 2 && g_marksRecorded > 0 && renderer) {
        const BYTE* buffer = *reinterpret_cast<BYTE* const*>(renderer + ((recorded & 0xFF) ? kRecordedVertices : kImmediateVertices));
        if (buffer && offset >= 0) {
            const DWORD* first = reinterpret_cast<const DWORD*>(buffer + offset);
            marked = first[2] == kMark[0] && first[3] == kMark[1];
        }
    }
    g_renderPrimitive(renderer, unused, type, offset, primitives, d, recorded, state);
    if (!marked) return;
    InterlockedDecrement(&g_marksRecorded);
    View view;
    {
        std::lock_guard<std::mutex> hold(g_viewLock);
        view = g_view;
    }
    DrawFog(view);
}

bool g_hooked = false;

void HookMapElement() {
    if (g_hooked) return;
    g_hooked = true;
    if (memcmp(reinterpret_cast<const void*>(kMapElementRender), kMapElementRenderCode, sizeof(kMapElementRenderCode)) != 0 ||
        memcmp(reinterpret_cast<const void*>(kRenderPrimitive), kRenderPrimitiveCode, sizeof(kRenderPrimitiveCode)) != 0) {
        rfa::Log("Fog of war: the map element's drawing (0x%X) or the primitive renderer's (0x%X) isn't the code expected; the world map has no fog",
                 kMapElementRender, kRenderPrimitive);
        return;
    }
    g_renderPrimitive = reinterpret_cast<RenderPrimitiveFn>(
        rfa::HookFunction(kRenderPrimitive, kRenderPrimitivePrologue, reinterpret_cast<const void*>(&RenderPrimitive)));
    if (!g_renderPrimitive) {
        rfa::Log("Fog of war: could not wrap the primitive renderer's drawing; the world map has no fog");
        return;
    }
    if (!rfa::ReplaceFunction(kMapElementRender, reinterpret_cast<const void*>(&MapElementRender)))
        rfa::Log("Fog of war: could not replace the map element's drawing; the world map has no fog");
}

// rfg_map_fog(element, centre_x, centre_y, view, zoom_pct): from rfg_ui_fullscreen.lua each frame the map is up, its
//   fog element (a `map` element, by handle) and the map's view as rfg_map_full gives it.
int __cdecl LuaMapFog(rfa::game::lua_State* L) {
    using namespace rfa::game;
    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> hold(g_viewLock);
    if (now - g_lastWord > kOpenedAfterMs) ++g_view.opened;
    g_lastWord = now;
    g_view.element = static_cast<DWORD>(lua_tonumber(L, 1));
    g_view.centreX = lua_tonumber(L, 2);
    g_view.centreY = lua_tonumber(L, 3);
    const double view = lua_tonumber(L, 4);
    g_view.view = view > 0 ? view : 1;
    g_view.zoom = lua_tonumber(L, 5);
    return 0;
}

}  // namespace

void Register(rfa::game::lua_State* L) {
    HookMapElement();
    rfa::game::lua_register(L, "rfg_map_fog", LuaMapFog);
}

}  // namespace fog_of_war
