// The level view's shader (docs\renderer-plan.md): draws the meshes the editor builds (positions,
// normals, texture coordinates), lit for every pixel in one of two ways:
//
//  - even light, as WPF lit the old view: an ambient light and up to four lights shining one way;
//  - game light (step 3): as the game's own shaders light a level (read from ir_light_point,
//    ir_light_spot_noshadows, ir_light_tube, ir_light_local_ambient, ir_light_ambient, ir_default_s,
//    ir_defaultglow_s, ir_alienskin and rl_hdr): the material's colour, bumps (normal map) and shine (specular
//    map), ambient light from above and below, the zone's lamps, glow, the exposure, the game's
//    tone curve, then the screen's sRGB; and the sky behind it all (SkyPixel). Lamps that cast
//    shadows are checked against their shadow maps (LampShadowAt; the maps drawn by ShadowPixel).
//
// Compiled when the editor starts, by Windows' own d3dcompiler_47.dll. Matrices are row-major and
// points are rows (point × World × ViewProjection), as in .NET's System.Numerics and in WPF.

cbuffer Frame : register(b0)
{
    row_major float4x4 ViewProjection;
    float4 CameraPosition;     // xyz
    float4 Ambient;            // even light: rgb from everywhere
    float4 LightDirection[4];  // even light: xyz, the way each light shines
    float4 LightColour[4];     // even light: rgb; black for a light not used
    float4 Mode;               // x: 1 for game light; y: tile size in pixels; z: tiles across; w: most lamps a tile holds
    float4 AmbientAbove;       // game light: rgb from above
    float4 AmbientBelow;       // game light: rgb from below
    float4 Tint;               // game light: rgb, the scene's tint; w: exposure
    float4 FogColour;          // game light: rgb; w: 1 when there is fog
    float4 FogRange;           // game light: x near, y far
    float4 Scales;             // game light: x the ambient light's scale (see SceneRenderer.AmbientScale)
    float4 SkyRight;           // the sky: the way the view's right edge looks, less the way its middle looks
    float4 SkyUp;              // the sky: likewise its top edge
    float4 SkyForward;         // the sky: the way the middle of the view looks
};

cbuffer Draw : register(b1)
{
    row_major float4x4 World;
    float4 Colour;         // the material's colour (white under a texture); a: how solid it is
    float4 Emissive;       // rgb: light of its own (shown however it is lit)
    float4 Flags;          // x: 1 with a texture; y: 1 when the mesh has normals; z: 1 with a normal map; w: 1 with a specular map
    float4 DiffuseColour;  // game light: the material's Diffuse_Color (multiplies its colour texture)
    float4 Shine;          // game light: rgb Specular_Color; w Specular_Power
    float4 Surface;        // game light: x Normal_Height, y Self_Illumination, z Glow_Falloff_Power, w Glow_Falloff_Inversion
    float4 Glow;           // game light: rgb Glow_Color; w: 1 with a glow map
    float4 Options;        // x: 1 to take the colour texture's alpha (see-through); 0 for solid surfaces, whose textures may keep other things there;
                           // y: cut out below this alpha, solid above it (0: not cut out)
    float4 Skin;           // game light, alien skin (ir_alienskin; see GameMaterial): x 1 full, 2 simple, 0 not alien skin; y Rim_Light_Amount; z Rim_Light_Power
};

// A lamp, in the view's space (the game's with Z turned round, as WPF's).
struct Lamp
{
    float4 PositionKind;      // xyz; w: 0 omni, 1 spot, 2 tube, 3 ambient
    float4 ForwardLength;     // xyz the way it shines (spot, ambient) or lies (tube); w: tube length
    float4 FrontPower;        // rgb facing it; w: the fade's power
    float4 BackFullTo;        // rgb facing away; w: full strength to here (metres)
    float4 SideGoneAt;        // rgb side-on; w: nothing from here on
    float4 Cone;              // spot: x the cosine where it ends, y 1 / (inner cosine - x)
};

Texture2D Diffuse : register(t0);
StructuredBuffer<Lamp> Lamps : register(t1);
StructuredBuffer<uint> TileLamps : register(t2);  // per screen tile: how many, then which lamps
Texture2D NormalMap : register(t3);
Texture2D SpecularMap : register(t4);
Texture2D GlowMap : register(t5);
StructuredBuffer<float4> LampMotion : register(t6);  // per lamp: x brightness multiplier (flicker), yzw position offset (sway)
StructuredBuffer<float4> LampShadow : register(t7);  // per lamp: x its shadow map (-1: none), y the light left in its shadow, fading out from z to w metres from the camera

// A shadow map's view: the view's space to the map's (x and y from -1 to 1, z the depth from 0 to 1), as rows.
struct ShadowView
{
    float4 Row0, Row1, Row2, Row3;
    float4 Texel;  // x: a map pixel's width per metre from the lamp (cones); y: its width in metres (orthographic)
};
StructuredBuffer<ShadowView> ShadowViews : register(t8);
Texture2DArray<float> ShadowMaps : register(t9);
SamplerState Repeat : register(s0);
SamplerComparisonState ShadowCompare : register(s1);

struct VertexIn
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 Uv : TEXCOORD0;
};

struct PixelIn
{
    float4 Position : SV_Position;
    float3 World : WORLDPOS;
    float3 Normal : NORMAL;
    float2 Uv : TEXCOORD0;
};

PixelIn VertexMain(VertexIn input)
{
    PixelIn output;
    float4 world = mul(float4(input.Position, 1), World);
    output.Position = mul(world, ViewProjection);
    output.World = world.xyz;
    output.Normal = mul(float4(input.Normal, 0), World).xyz;
    output.Uv = input.Uv;
    return output;
}

float3 SrgbToLinear(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float3 LinearToSrgb(float3 c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1 / 2.4) - 0.055; }
float Luminance(float3 c) { return dot(c, float3(0.3, 0.59, 0.11)); }  // the game's weights
float Brightness(float3 c) { return dot(c, float3(0.213, 0.715, 0.072)); }  // the alien skin shader's other weights

// The game's colour for a surface by how it faces the light: facing it, the front colour; side-on,
// the side colour; facing away, the back colour; blended in between.
float3 Facing(Lamp lamp, float facing)
{
    return facing >= 0 ? lerp(lamp.SideGoneAt.rgb, lamp.FrontPower.rgb, saturate(facing))
                       : lerp(lamp.SideGoneAt.rgb, lamp.BackFullTo.rgb, saturate(-facing));
}

// One lamp's light on a point with a normal, and (in shine) how much it shines there: the
// reflection of the lamp towards the eye, as sharp as the exponent, as bright as the lamp.
float3 LampLight(Lamp lamp, float3 position, float3 normal, float3 toEye, float exponent, out float shine)
{
    shine = 0;
    int kind = (int)lamp.PositionKind.w;
    float3 source = lamp.PositionKind.xyz;
    if (kind == 2)
    {
        // A tube: from the point of it nearest this one.
        float3 axis = lamp.ForwardLength.xyz, start = source - axis * lamp.ForwardLength.w / 2;
        source = start + axis * clamp(dot(position - start, axis), 0, lamp.ForwardLength.w);
    }
    float3 toLamp = source - position;
    float distance = length(toLamp);
    float through = saturate((distance - lamp.BackFullTo.w) / max(lamp.SideGoneAt.w - lamp.BackFullTo.w, 0.001));
    if (kind == 3)  // light over an area, from one side
        return Facing(lamp, dot(normal, -lamp.ForwardLength.xyz)) * pow(1 - through, lamp.FrontPower.w);

    float3 toward = toLamp / max(distance, 0.0001);
    float facing = dot(normal, toward);
    float strength = kind == 1
        ? pow(1 - through, 3) * pow(saturate((dot(-toward, lamp.ForwardLength.xyz) - lamp.Cone.x) * lamp.Cone.y), lamp.FrontPower.w)
        : pow(1 - through, lamp.FrontPower.w);
    float3 reflected = 2 * facing * normal - toward;
    shine = pow(saturate(dot(reflected, toEye)), exponent) * Luminance(lamp.FrontPower.rgb) * saturate(facing) * strength;
    return Facing(lamp, facing) * strength;
}

// How much of a lamp's light reaches a point past what stands in its way (1: all of it), as the
// game's ir_light_spot works it out: the point's place in the lamp's shadow map, nine smoothed
// samples around it, at least the light the shadow leaves, fading out away from the camera. The
// point is first moved off its surface by about a map pixel, so a lit surface doesn't shadow itself.
float LampShadowAt(float4 shadow, float3 position, float3 normal, float3 lampPosition)
{
    ShadowView view = ShadowViews[(uint)shadow.x];
    float pixel = view.Texel.x * distance(position, lampPosition) + view.Texel.y;
    float3 p = position + normal * pixel * 1.5;
    float4 inMap = p.x * view.Row0 + p.y * view.Row1 + p.z * view.Row2 + view.Row3;
    if (inMap.w <= 0) return 1;
    inMap.xyz /= inMap.w;
    float2 uv = float2(0.5 + 0.5 * inMap.x, 0.5 - 0.5 * inMap.y);
    if (any(uv < 0) || any(uv > 1) || inMap.z > 1) return 1;  // outside the map: outside the cone, which lights nothing
    float lit = 0;
    [unroll] for (int y = -1; y <= 1; y++)
        [unroll] for (int x = -1; x <= 1; x++)
            lit += ShadowMaps.SampleCmpLevelZero(ShadowCompare, float3(uv, shadow.x), inMap.z, int2(x, y));
    float near = saturate((shadow.w - distance(position, CameraPosition.xyz)) / max(shadow.w - shadow.z, 0.001));
    return max(1 - (1 - lit / 9) * near, shadow.y);
}

// The normal map's bend (decoded as the game's ir_default_s does), turned into the view's space
// along the surface's texture directions, worked out from how position and texture coordinates
// change across the screen (the meshes carry no tangents).
float3 Bumped(float3 normal, float3 position, float2 uv)
{
    float4 t = NormalMap.Sample(Repeat, uv);
    float x = (2 * t.a - 1) * (2 * t.r - 1), y = 2 * t.g - 1;
    float3 bend = normalize(float3(x, y, sqrt(max(0, 1 - x * x - y * y)) - 1) * Surface.x + float3(0, 0, 1));

    float3 dp1 = ddx(position), dp2 = ddy(position);
    float2 duv1 = ddx(uv), duv2 = ddy(uv);
    float3 across2 = cross(dp2, normal), across1 = cross(normal, dp1);
    float3 tangent = across2 * duv1.x + across1 * duv2.x;   // where u grows
    float3 bitangent = across2 * duv1.y + across1 * duv2.y; // where v grows
    float scale = rsqrt(max(max(dot(tangent, tangent), dot(bitangent, bitangent)), 1e-20));
    return normalize(bend.x * tangent * scale + bend.y * bitangent * scale + bend.z * normal);
}

float4 PixelMain(PixelIn input, bool front : SV_IsFrontFace) : SV_Target
{
    float3 normal;
    if (Flags.y > 0)
    {
        normal = normalize(input.Normal);
        if (!front) normal = -normal;  // the back of a two-sided surface faces the other way
    }
    else
    {
        // No normals given (WPF made its own): the triangle's own facing, turned towards the camera.
        normal = normalize(cross(ddx(input.World), ddy(input.World)));
        if (dot(normal, CameraPosition.xyz - input.World) < 0) normal = -normal;
    }

    float4 colour = Colour;
    if (Flags.x > 0)
    {
        float4 picture = Diffuse.Sample(Repeat, input.Uv);
        colour.rgb *= picture.rgb;
        if (Options.x > 0) colour.a *= picture.a;
    }
    if (Options.y > 0)
    {
        // Cut out (hair, capes, bullet holes): missing below the threshold, solid above it.
        if (colour.a < Options.y) discard;
        colour.a = 1;
    }
    if (colour.a < 0.004) discard;  // nothing to see: and it mustn't hide what is behind it

    float3 shown;
    if (Mode.x > 0)
    {
        // Game light. Up in the view's space is +Y, as in the game.
        float3 toEye = normalize(CameraPosition.xyz - input.World);
        float3 surface = Flags.z > 0 ? Bumped(normal, input.World, input.Uv) : normal;
        float3 albedo = SrgbToLinear(colour.rgb);
        if (Skin.x > 0)
            albedo = lerp(albedo, Brightness(albedo) * DiffuseColour.rgb, Skin.y);  // alien skin: towards Diffuse_Color, by Rim_Light_Amount
        else
            albedo *= DiffuseColour.rgb;
        float3 light = lerp(AmbientBelow.rgb, AmbientAbove.rgb, 0.5 + 0.5 * surface.y) * Scales.x;
        float exponent = min(Shine.w, 50) * 5.12, shine = 0;
        uint tileSize = (uint)Mode.y, perTile = (uint)Mode.w;
        uint tile = (uint)(input.Position.y / tileSize) * (uint)Mode.z + (uint)(input.Position.x / tileSize);
        uint first = tile * (perTile + 1), count = TileLamps[first];
        for (uint i = 0; i < count; i++)
        {
            uint index = TileLamps[first + 1 + i];
            Lamp lamp = Lamps[index];
            float4 motion = LampMotion[index], shadow = LampShadow[index];
            float strength = motion.x;
            if (shadow.x >= 0) strength *= LampShadowAt(shadow, input.World, normal, lamp.PositionKind.xyz);
            lamp.PositionKind.xyz += motion.yzw;
            float lampShine;
            light += LampLight(lamp, input.World, surface, toEye, exponent, lampShine) * strength;
            shine += lampShine * strength;
        }
        float3 scene = albedo * light;
        float4 specular = Flags.w > 0 ? SpecularMap.Sample(Repeat, input.Uv) : 0;
        if (Flags.w > 0)
        {
            // Shine, coloured as the light falling there (ir_default_s), by the specular map and colour
            // (alien skin: by the map's green only).
            float3 shineColour = light * shine / (Luminance(light) + 0.00001);
            scene += shineColour * (Skin.x > 0 ? specular.ggg : specular.rgb) * Shine.rgb;
        }
        if (Skin.x > 0 && Glow.w > 0)
        {
            // Alien skin's glow (ir_alienskin): only where the specular map's alpha is, fading in
            // strong light, and (the full shader) towards the edges. The game scrolls the glow map
            // along the mesh's second texture coordinates, which the editor hasn't: its average colour.
            float dark = pow(1 - saturate(Brightness(light)), Skin.x == 1 ? Skin.z : Surface.z);
            float edges = Skin.x == 1 ? pow(max(dot(normal, toEye), 0), Surface.z) : 1;
            float3 glowColour = SrgbToLinear(GlowMap.SampleLevel(Repeat, input.Uv, 16).rgb);
            scene += glowColour * Glow.rgb * Surface.y * specular.a * specular.a * edges * dark;
        }
        if (FogColour.w > 0)
        {
            float far = saturate((distance(input.World, CameraPosition.xyz) - FogRange.x) / max(FogRange.y - FogRange.x, 0.001));
            scene = lerp(scene, FogColour.rgb, far);
        }
        scene = scene * Tint.rgb * Tint.w + SrgbToLinear(Emissive.rgb);
        if (Glow.w > 0 && Skin.x == 0)
        {
            // Glow (ir_defaultglow_s): added after the tint and exposure, faded towards the edges
            // (or the middle) by how squarely the surface faces the eye.
            float facingEye = max(dot(normal, toEye), 0);
            float falloff = pow(abs(1 - Surface.w - facingEye), Surface.z);
            scene += SrgbToLinear(GlowMap.Sample(Repeat, input.Uv).rgb) * Glow.rgb * Surface.y * falloff;
        }
        // rl_hdr's tone curve: scaled so 1.49 is white, then 1.5x - 0.5x³, a soft roll-off into white.
        float3 x = saturate(scene * 0.671141);
        shown = LinearToSrgb(1.5 * x - 0.5 * x * x * x);
    }
    else
    {
        float3 light = Ambient.rgb;
        [unroll] for (int i = 0; i < 4; i++)
            light += LightColour[i].rgb * saturate(dot(normal, -LightDirection[i].xyz));
        shown = saturate(colour.rgb * light + Emissive.rgb);
    }

    // WPF's picture takes colours already multiplied by how solid they are.
    return float4(shown * colour.a, colour.a);
}

// The sky (game light, where the zone has one; see ZoneLighting): the game's sky effects draw a
// dome textured with a picture of the whole sky as seen from below, straight up in the middle and
// the horizon round the edge. Drawn first, over the whole view, as the camera sees it from the
// middle of the dome; below the horizon, the horizon's colour. The dome's own shape isn't known
// yet (it is in the effect file): here each degree up from the horizon takes the same step
// towards the middle of the picture (a picture taken as if looked down on, squeezed at the edge,
// smears into streaks low in the sky). Which way it faces was matched against the user's
// screenshots of the first mission: from the start, looking east at the Terraformer, the sun (low
// in the picture) is ahead, dark clouds on its left and a yellow glow on its right; looking west,
// brown haze. So the picture's bottom is the game's +X (east) and its right +Z (north): the
// picture as seen from above, turned a quarter.
// The shadow maps: only depth is drawn (no pixel shader for solid things); see-through things
// (grilles, leaves) leave holes where their picture is see-through.
void ShadowPixel(PixelIn input)
{
    float alpha = Colour.a * (Flags.x > 0 ? Diffuse.Sample(Repeat, input.Uv).a : 1);
    if (alpha < 0.5) discard;
}

struct SkyIn
{
    float4 Position : SV_Position;
    float2 Screen : TEXCOORD0;  // -1 to 1 from left to right, bottom to top
};

SkyIn SkyVertex(uint id : SV_VertexID)
{
    // One triangle, big enough to cover the view.
    float2 corner = float2(id == 1 ? 3 : -1, id == 2 ? 3 : -1);
    SkyIn output;
    output.Position = float4(corner, 0, 1);
    output.Screen = corner;
    return output;
}

float4 SkyPixel(SkyIn input) : SV_Target
{
    float3 look = normalize(SkyForward.xyz + input.Screen.x * SkyRight.xyz + input.Screen.y * SkyUp.xyz);
    float2 flat = float2(look.x, -look.z);  // the game's x and z (the view's z is turned round)
    float fromTop = acos(saturate(look.y)) / 1.5707963;  // 0 straight up, 1 at the horizon and below
    float2 across = flat / max(length(flat), 0.00001) * fromTop;
    float2 uv = 0.5 + 0.5 * across.yx * 0.99;  // the picture's circle nearly fills it
    float3 sky = Diffuse.Sample(Repeat, uv).rgb;
    // Below the horizon, the picture's edge would smear into streaks: a blurred copy of it instead.
    if (look.y < 0) sky = lerp(sky, Diffuse.SampleLevel(Repeat, uv, 5).rgb, saturate(-look.y * 10));
    return float4(sky, 1);
}
