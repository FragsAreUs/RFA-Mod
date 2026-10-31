using System.Numerics;
using System.Runtime.InteropServices;
using Vortice.Direct3D11;

namespace RfaEditor.Render;

/// <summary>
/// A zone's lamps on the graphics card, and for each 32-pixel square of the screen ("tile"), which
/// of them can reach it. Each pixel then only adds up the lamps of its tile (at most
/// <see cref="PerTile"/>, the nearest the camera first), not all of a zone's hundreds. Which lamps
/// reach a tile is worked out on the processor each frame, from each lamp's reach as a ball,
/// as seen on the screen. Positions here are in the view's space (the game's, Z turned round).
/// </summary>
sealed class LampTiles : IDisposable
{
    public const int TileSize = 32, PerTile = 63;

    [StructLayout(LayoutKind.Sequential)]
    struct GpuLamp
    {
        public Vector4 PositionKind, ForwardLength, FrontPower, BackFullTo, SideGoneAt, Cone;
    }

    readonly GpuDevice gpu;
    ID3D11Buffer? lampBuffer, tileBuffer, motionBuffer;
    IReadOnlyList<ZoneLighting.Lamp> lamps = [];
    Vector4[] motion = [];  // per lamp: brightness multiplier, position offset (view space)
    int tileCapacity;
    List<(Vector3 Centre, float Reach)> balls = [];  // each lamp's reach, to find the tiles it lights

    public ID3D11ShaderResourceView? LampView { get; private set; }
    public ID3D11ShaderResourceView? MotionView { get; private set; }
    /// <summary>Whether any lamp flickers or sways (so the view has to be drawn again and again).</summary>
    public bool Animated { get; private set; }
    public ID3D11ShaderResourceView? TileView { get; private set; }
    public int TilesAcross { get; private set; }
    public int LampCount => balls.Count;

    public LampTiles(GpuDevice gpu) => this.gpu = gpu;

    /// <summary>Puts a zone's lamps on the card (in the view's space).</summary>
    public void SetLamps(IReadOnlyList<ZoneLighting.Lamp> lamps)
    {
        LampView?.Dispose();
        lampBuffer?.Dispose();
        MotionView?.Dispose();
        motionBuffer?.Dispose();
        this.lamps = lamps;
        Animated = lamps.Any(l => l.Moves);
        motion = Enumerable.Repeat(new Vector4(1, 0, 0, 0), Math.Max(1, lamps.Count)).ToArray();
        motionBuffer = gpu.Device.CreateBuffer(motion, BindFlags.ShaderResource, ResourceUsage.Default, CpuAccessFlags.None,
                                               ResourceOptionFlags.BufferStructured, 0, (uint)Marshal.SizeOf<Vector4>());
        MotionView = gpu.Device.CreateShaderResourceView(motionBuffer);
        var gpuLamps = new GpuLamp[Math.Max(1, lamps.Count)];  // a buffer can't be empty
        balls = [];
        for (int i = 0; i < lamps.Count; i++)
        {
            ZoneLighting.Lamp lamp = lamps[i];
            Vector3 position = ToView(lamp.Position), forward = Vector3.Normalize(ToView(lamp.Forward));
            float inner = MathF.Cos(lamp.Hotspot / 2 * MathF.PI / 180), outer = MathF.Cos((lamp.Hotspot + lamp.Falloff) / 2 * MathF.PI / 180);
            gpuLamps[i] = new GpuLamp
            {
                PositionKind = new Vector4(position, (float)lamp.Kind),
                ForwardLength = new Vector4(forward, lamp.TubeLength),
                FrontPower = new Vector4(lamp.Front, lamp.Power),
                BackFullTo = new Vector4(lamp.Back, lamp.FullTo),
                SideGoneAt = new Vector4((lamp.Front + lamp.Back) / 2, lamp.GoneAt),
                Cone = new Vector4(outer, 1 / Math.Max(inner - outer, 0.0001f), 0, 0),
            };
            balls.Add((position, lamp.GoneAt + (lamp.Kind == ZoneLighting.LampKind.Tube ? lamp.TubeLength / 2 : 0)));
        }
        lampBuffer = gpu.Device.CreateBuffer(gpuLamps, BindFlags.ShaderResource, ResourceUsage.Default, CpuAccessFlags.None,
                                             ResourceOptionFlags.BufferStructured, 0, (uint)Marshal.SizeOf<GpuLamp>());
        LampView = gpu.Device.CreateShaderResourceView(lampBuffer);
    }

    static Vector3 ToView(Vector3 game) => new(game.X, game.Y, -game.Z);

    /// <summary>The flickering and swaying lamps' brightness and offset at a time (in seconds), for the shader.</summary>
    public void UpdateMotion(ID3D11DeviceContext context, float seconds)
    {
        if (!Animated || motionBuffer == null) return;
        for (int i = 0; i < lamps.Count; i++)
        {
            if (!lamps[i].Moves) continue;
            (float brightness, Vector3 offset) = lamps[i].At(seconds);
            motion[i] = new Vector4(brightness, offset.X, offset.Y, -offset.Z);
        }
        context.UpdateSubresource(motion, motionBuffer);
    }

    /// <summary>
    /// Works out each tile's lamps for a camera: <paramref name="view"/> takes the view's space to
    /// the camera's, <paramref name="scale"/> is the projection's top-to-bottom scale (1 / tan of half
    /// the field of view), <paramref name="aspect"/> width / height.
    /// </summary>
    public void Update(ID3D11DeviceContext context, Matrix4x4 view, float scale, float aspect, float near, int width, int height)
    {
        TilesAcross = (width + TileSize - 1) / TileSize;
        int tilesDown = (height + TileSize - 1) / TileSize, tiles = TilesAcross * tilesDown;
        var lists = new uint[tiles * (PerTile + 1)];

        // Nearest lamps first, so a crowded tile keeps the ones that matter most.
        var order = Enumerable.Range(0, balls.Count)
            .Select(i => (Index: i, Centre: Vector3.Transform(balls[i].Centre, view), balls[i].Reach))
            .OrderBy(b => b.Centre.LengthSquared());
        foreach (var (index, centre, reach) in order)
        {
            float depth = -centre.Z;  // in front of the camera is -Z
            if (depth + reach < near) continue;  // all of it behind the camera
            int x0 = 0, x1 = TilesAcross - 1, y0 = 0, y1 = tilesDown - 1;
            if (depth - reach > near)
            {
                // The ball's square on the screen, made a little too big to be safe (its nearest depth).
                float nearest = depth - reach;
                float cx = (centre.X / depth * scale / aspect + 1) / 2 * width, cy = (1 - centre.Y / depth * scale) / 2 * height;
                float rx = reach / nearest * scale / aspect / 2 * width, ry = reach / nearest * scale / 2 * height;
                x0 = (int)Math.Floor((cx - rx) / TileSize);
                x1 = (int)Math.Floor((cx + rx) / TileSize);
                y0 = (int)Math.Floor((cy - ry) / TileSize);
                y1 = (int)Math.Floor((cy + ry) / TileSize);
                if (x1 < 0 || y1 < 0 || x0 >= TilesAcross || y0 >= tilesDown) continue;  // off the screen
                x0 = Math.Max(x0, 0); y0 = Math.Max(y0, 0);
                x1 = Math.Min(x1, TilesAcross - 1); y1 = Math.Min(y1, tilesDown - 1);
            }
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++)
                {
                    int at = (y * TilesAcross + x) * (PerTile + 1);
                    if (lists[at] < PerTile) lists[at + 1 + lists[at]++] = (uint)index;
                }
        }

        if (tileBuffer == null || lists.Length != tileCapacity)  // the view's size changed
        {
            TileView?.Dispose();
            tileBuffer?.Dispose();
            tileCapacity = lists.Length;
            tileBuffer = gpu.Device.CreateBuffer(lists, BindFlags.ShaderResource, ResourceUsage.Default, CpuAccessFlags.None,
                                                 ResourceOptionFlags.BufferStructured, 0, sizeof(uint));
            TileView = gpu.Device.CreateShaderResourceView(tileBuffer);
        }
        else context.UpdateSubresource(lists, tileBuffer);
    }

    public void Dispose()
    {
        LampView?.Dispose();
        lampBuffer?.Dispose();
        MotionView?.Dispose();
        motionBuffer?.Dispose();
        TileView?.Dispose();
        tileBuffer?.Dispose();
    }
}
