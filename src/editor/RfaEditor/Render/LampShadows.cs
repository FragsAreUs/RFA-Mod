using System.Numerics;
using System.Runtime.InteropServices;
using Vortice.Direct3D;
using Vortice.Direct3D11;
using Vortice.DXGI;

namespace RfaEditor.Render;

/// <summary>
/// Shadows for the lamps that cast them, as the game draws them (its ir_light_spot shader; see
/// docs\game-notes.md, "Lighting"). 561 of the game's 38,240 lamps cast shadows (casts_shadows),
/// 543 of them spotlights; the 18 omni lamps that do are left without here. Each gets a shadow
/// map: a depth picture of the scene drawn from the lamp along its cone (or straight along it, for
/// shadow_projection "orthographic"). A pixel the lamp lights is in shadow where the map has
/// something nearer the lamp. As in the game, nine smoothed samples around the pixel's place in the
/// map soften the edge, shadow_density says how dark the shadow is (1 − density of the light
/// stays), and the shadows fade out from shadow_distance metres away from the camera.
///
/// Maps are kept for up to <see cref="MaxMaps"/> lamps at a time: those whose shadows can show
/// from where the camera is, nearest first. Positions here are in the view's space (the game's, Z
/// turned round).
/// </summary>
sealed class LampShadows : IDisposable
{
    public const int MaxMaps = 16, MapSize = 1024;

    /// <summary>
    /// Nearer the lamp than this isn't drawn into its map: the lamp's own fitting, around it, would
    /// otherwise shadow everything it lights.
    /// </summary>
    const float Near = 0.25f;

    /// <summary>How far past shadow_distance a shadow takes to fade out (a guess: the game's fade isn't read yet).</summary>
    const float FadeLength = 0.25f;

    /// <summary>One lamp's map: which lamp, its place in the map texture, and how the scene is turned into the map.</summary>
    public readonly record struct Map(int Lamp, int Slot, Matrix4x4 ViewProjection, Vector3 Position, float Reach);

    [StructLayout(LayoutKind.Sequential)]
    struct GpuShadowView
    {
        public Vector4 Row0, Row1, Row2, Row3;  // the view's space to the map's: x and y from -1 to 1, z the depth from 0 to 1
        public Vector4 Texel;  // x: a map pixel's width per metre from the lamp (cones); y: its width in metres (orthographic)
    }

    readonly GpuDevice gpu;
    readonly ID3D11Texture2D maps;
    readonly ID3D11DepthStencilView[] targets = new ID3D11DepthStencilView[MaxMaps];
    readonly ID3D11Buffer viewBuffer;
    readonly GpuShadowView[] views = new GpuShadowView[MaxMaps];
    ID3D11Buffer? shadowBuffer;
    Vector4[] shadowOf = [];  // per lamp: its map (-1: none), the light left in its shadow, fading out from z to w metres from the camera
    IReadOnlyList<ZoneLighting.Lamp> lamps = [];
    readonly List<int> casting = [];  // the lamps that cast shadows

    public IReadOnlyList<Map> Chosen { get; private set; } = [];
    public ID3D11ShaderResourceView MapView { get; }
    public ID3D11ShaderResourceView ViewsView { get; }
    public ID3D11ShaderResourceView? ShadowView { get; private set; }

    public LampShadows(GpuDevice gpu)
    {
        this.gpu = gpu;
        maps = gpu.Device.CreateTexture2D(new Texture2DDescription(Format.R32_Typeless, MapSize, MapSize, MaxMaps, 1,
            BindFlags.DepthStencil | BindFlags.ShaderResource));
        for (int slot = 0; slot < MaxMaps; slot++)
            targets[slot] = gpu.Device.CreateDepthStencilView(maps,
                new DepthStencilViewDescription(maps, DepthStencilViewDimension.Texture2DArray, Format.D32_Float, 0, (uint)slot, 1));
        MapView = gpu.Device.CreateShaderResourceView(maps,
            new ShaderResourceViewDescription(maps, ShaderResourceViewDimension.Texture2DArray, Format.R32_Float, 0, 1, 0, MaxMaps));
        viewBuffer = gpu.Device.CreateBuffer(views, BindFlags.ShaderResource, ResourceUsage.Default, CpuAccessFlags.None,
                                             ResourceOptionFlags.BufferStructured, 0, (uint)Marshal.SizeOf<GpuShadowView>());
        ViewsView = gpu.Device.CreateShaderResourceView(viewBuffer);
        SetLamps([]);
    }

    /// <summary>The zone's lamps (in the same order as <see cref="LampTiles"/> has them).</summary>
    public void SetLamps(IReadOnlyList<ZoneLighting.Lamp> lamps)
    {
        this.lamps = lamps;
        casting.Clear();
        for (int i = 0; i < lamps.Count; i++)
            if (lamps[i].CastsShadows && lamps[i].Kind == ZoneLighting.LampKind.Spot) casting.Add(i);
        Chosen = [];
        ShadowView?.Dispose();
        shadowBuffer?.Dispose();
        shadowOf = Enumerable.Repeat(new Vector4(-1, 1, 0, 0), Math.Max(1, lamps.Count)).ToArray();
        shadowBuffer = gpu.Device.CreateBuffer(shadowOf, BindFlags.ShaderResource, ResourceUsage.Default, CpuAccessFlags.None,
                                               ResourceOptionFlags.BufferStructured, 0, (uint)Marshal.SizeOf<Vector4>());
        ShadowView = gpu.Device.CreateShaderResourceView(shadowBuffer);
    }

    /// <summary>
    /// Picks the lamps to keep maps for, seen from a camera (in the view's space): those whose
    /// shadows can show there (the camera within their fade distance and reach), nearest first.
    /// Lamps picked before keep their maps; <paramref name="added"/> are the ones whose maps have to
    /// be drawn. Says whether the pick changed at all.
    /// </summary>
    public bool Choose(Vector3 camera, out IReadOnlyList<Map> added)
    {
        HashSet<int> picked = casting
            .Select(i => (Index: i, Distance: Vector3.Distance(ToView(lamps[i].Position), camera)))
            .Where(l => l.Distance <= FadeEnd(lamps[l.Index]) + lamps[l.Index].GoneAt)
            .OrderBy(l => l.Distance).Take(MaxMaps).Select(l => l.Index).ToHashSet();
        added = [];
        if (picked.SetEquals(Chosen.Select(m => m.Lamp))) return false;
        List<Map> kept = Chosen.Where(m => picked.Contains(m.Lamp)).ToList();
        var free = new Queue<int>(Enumerable.Range(0, MaxMaps).Except(kept.Select(m => m.Slot)));
        added = picked.Where(lamp => !kept.Any(m => m.Lamp == lamp)).Select(lamp => MapFor(lamp, free.Dequeue())).ToList();
        Chosen = [.. kept, .. added];
        return true;
    }

    static float FadeEnd(ZoneLighting.Lamp lamp) => lamp.ShadowDistance * (1 + FadeLength);

    /// <summary>A lamp's map: looking from the lamp along its cone, wide enough for the whole cone.</summary>
    Map MapFor(int index, int slot)
    {
        ZoneLighting.Lamp lamp = lamps[index];
        Vector3 position = ToView(lamp.Position), forward = Vector3.Normalize(ToView(lamp.Forward));
        Vector3 up = MathF.Abs(forward.Y) > 0.99f ? Vector3.UnitZ : Vector3.UnitY;
        Matrix4x4 view = Matrix4x4.CreateLookAt(position, position + forward, up);
        float far = Math.Max(lamp.GoneAt, Near * 2);
        float halfAngle = Math.Clamp((lamp.Hotspot + lamp.Falloff) / 2 + 2, 1, 80) * MathF.PI / 180;
        Matrix4x4 projection;
        Vector4 texel;
        if (lamp.ShadowOrthographic)
        {
            float half = Math.Max(1, far * MathF.Tan(halfAngle));
            projection = Matrix4x4.CreateOrthographic(half * 2, half * 2, Near, far);
            texel = new Vector4(0, half * 2 / MapSize, 0, 0);
        }
        else
        {
            projection = Matrix4x4.CreatePerspectiveFieldOfView(halfAngle * 2, 1, Near, far);
            texel = new Vector4(2 * MathF.Tan(halfAngle) / MapSize, 0, 0, 0);
        }
        Matrix4x4 toMap = view * projection;
        views[slot] = new GpuShadowView
        {
            Row0 = new Vector4(toMap.M11, toMap.M12, toMap.M13, toMap.M14),
            Row1 = new Vector4(toMap.M21, toMap.M22, toMap.M23, toMap.M24),
            Row2 = new Vector4(toMap.M31, toMap.M32, toMap.M33, toMap.M34),
            Row3 = new Vector4(toMap.M41, toMap.M42, toMap.M43, toMap.M44),
            Texel = texel,
        };
        return new Map(index, slot, toMap, position, far);
    }

    /// <summary>Where a map is drawn (one layer of the maps' texture).</summary>
    public ID3D11DepthStencilView Target(int slot) => targets[slot];

    /// <summary>Tells the shader which lamps have maps, once they are drawn.</summary>
    public void Upload(ID3D11DeviceContext context)
    {
        Array.Fill(shadowOf, new Vector4(-1, 1, 0, 0));
        foreach (Map map in Chosen)
        {
            ZoneLighting.Lamp lamp = lamps[map.Lamp];
            shadowOf[map.Lamp] = new Vector4(map.Slot, Math.Clamp(1 - lamp.ShadowDensity, 0, 1), lamp.ShadowDistance, FadeEnd(lamp));
        }
        context.UpdateSubresource(shadowOf, shadowBuffer!);
        context.UpdateSubresource(views, viewBuffer);
    }

    static Vector3 ToView(Vector3 game) => new(game.X, game.Y, -game.Z);

    public void Dispose()
    {
        ShadowView?.Dispose();
        shadowBuffer?.Dispose();
        ViewsView.Dispose();
        viewBuffer.Dispose();
        MapView.Dispose();
        foreach (ID3D11DepthStencilView target in targets) target.Dispose();
        maps.Dispose();
    }
}
