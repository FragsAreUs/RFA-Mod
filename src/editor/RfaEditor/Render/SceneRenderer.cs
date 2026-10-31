using System.IO;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.Media3D;
using RfaFormats.Textures;
using Vortice.D3DCompiler;
using Vortice.Direct3D;
using Vortice.Direct3D11;
using Vortice.DXGI;

namespace RfaEditor.Render;

/// <summary>
/// Draws a WPF 3D scene (a Viewport3D's models, lights and camera) with Direct3D 11 (step 1 of
/// docs\renderer-plan.md): the level view's builders still make WPF meshes and materials, and this
/// turns each mesh and texture into Direct3D buffers and textures the first time it is drawn,
/// keeping them while they are in use. Where a material names the game's texture (models, level
/// geometry), that texture is put on the card as the game stores it, full size, instead of WPF's
/// small decoded copy (step 2 begun). Things are drawn in the scene's own order, see-through ones
/// blended over what is behind them, as WPF does. Lit evenly, as WPF lit them, or with
/// <see cref="GameLighting"/> set, as the game lights the zone (step 3: its lamps, ambient and
/// exposure; see Scene.hlsl), with the sky behind it where the zone has one, and the shadows of the
/// lamps that cast them (<see cref="LampShadows"/>).
/// </summary>
sealed class SceneRenderer : IDisposable
{
    [StructLayout(LayoutKind.Sequential)]
    struct Vertex(Vector3 position, Vector3 normal, Vector2 uv)
    {
        public Vector3 Position = position;
        public Vector3 Normal = normal;
        public Vector2 Uv = uv;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct FrameConstants
    {
        public Matrix4x4 ViewProjection;
        public Vector4 CameraPosition, Ambient;
        public Vector4 Direction0, Direction1, Direction2, Direction3;
        public Vector4 Colour0, Colour1, Colour2, Colour3;
        public Vector4 Mode, AmbientAbove, AmbientBelow, Tint, FogColour, FogRange, Scales;
        public Vector4 SkyRight, SkyUp, SkyForward;  // the way the view's right edge, top edge and middle look (see Scene.hlsl, SkyPixel)
    }

    [StructLayout(LayoutKind.Sequential)]
    struct DrawConstants
    {
        public Matrix4x4 World;
        public Vector4 Colour, Emissive, Flags;
        public Vector4 DiffuseColour, Shine, Surface, Glow;  // the game material's (see GameMaterial, Scene.hlsl)
        public Vector4 Options;  // x: 1 to take the colour texture's alpha; y: cut out below this alpha (0: not cut out)
        public Vector4 Skin;     // alien skin (see GameMaterial): x 1 full, 2 simple, 0 not; y Rim_Light_Amount; z Rim_Light_Power
    }

    /// <summary>A mesh on the graphics card.</summary>
    sealed class GpuMesh(ID3D11Buffer vertices, ID3D11Buffer indices, int indexCount, bool hasNormals, Vector3 centre, float radius) : IDisposable
    {
        public ID3D11Buffer Vertices = vertices, Indices = indices;
        public int IndexCount = indexCount;
        public bool HasNormals = hasNormals;
        public Vector3 Centre = centre;  // a ball around it, to skip it for lamps that can't reach it
        public float Radius = radius;
        public long LastUsed;
        public void Dispose() { Vertices.Dispose(); Indices.Dispose(); }
    }

    /// <summary>A texture on the graphics card, with its smaller copies for far away.</summary>
    sealed class GpuTexture(ID3D11Texture2D texture, ID3D11ShaderResourceView view) : IDisposable
    {
        public ID3D11ShaderResourceView View = view;
        public long LastUsed;
        public void Dispose() { View.Dispose(); texture.Dispose(); }
    }

    /// <summary>One mesh to draw, with how it looks and where it goes.</summary>
    readonly record struct DrawItem(MeshGeometry3D Mesh, Material? Front, Material? Back, Matrix4x4 World);

    readonly GpuDevice gpu;
    readonly ID3D11VertexShader vertexShader, skyVertexShader;
    readonly ID3D11PixelShader pixelShader, skyPixelShader, shadowPixelShader;
    readonly ID3D11InputLayout inputLayout;
    readonly ID3D11Buffer frameBuffer, drawBuffer;
    readonly ID3D11SamplerState sampler, shadowSampler;
    readonly ID3D11RasterizerState twoSided, oneSided, shadowRaster;
    readonly ID3D11BlendState blend;
    readonly ID3D11DepthStencilState depthTest, depthTestOnly;  // the second tests depth without writing it (see-through things)
    readonly ID3D11DepthStencilState noDepth;  // the sky: behind everything, drawn first
    readonly ID3D11DepthStencilState shadowDepth;  // the shadow maps: ordinary depth, nearer is smaller
    readonly Dictionary<MeshGeometry3D, GpuMesh> meshes = [];
    readonly Dictionary<BitmapSource, GpuTexture> textures = [];
    readonly Dictionary<string, GpuTexture?> namedTextures = new(StringComparer.OrdinalIgnoreCase);  // the game's textures, by file name
    readonly Dictionary<BitmapSource, bool> picturesWithAlpha = [];  // whether WPF's picture kept its alpha (see-through)
    readonly LampTiles lampTiles;
    readonly LampShadows lampShadows;
    readonly System.Diagnostics.Stopwatch clock = System.Diagnostics.Stopwatch.StartNew();  // the lamps' curves play by it

    /// <summary>
    /// Reads one of the game's textures by file name (a normal, specular or glow map, or a sky), at
    /// most so many pixels across, with its alpha channel kept; null if it can't be found. Without
    /// it, game light draws no bumps, shine, glow or sky.
    /// </summary>
    public Func<string, int, BitmapSource?>? LoadTexture { get; set; }

    /// <summary>
    /// Finds one of the game's textures by file name, as stored (its texture file and its entry
    /// there), to put on the card as it is: compressed, full size, with the game's own smaller
    /// copies. Without it (or for formats the card can't take as they are), <see cref="LoadTexture"/>'s
    /// decoded pictures are used.
    /// </summary>
    public Func<string, (PegFile Peg, PegTexture Texture)?>? FindTexture { get; set; }
    ZoneLighting? gameLighting;
    long frame;

    /// <summary>The zone's lighting to light it as the game does; null to light it evenly.</summary>
    public ZoneLighting? GameLighting
    {
        get => gameLighting;
        set
        {
            if (value == gameLighting) return;
            gameLighting = value;
            shadowsLook = 0;
            if (value != null)
            {
                lampTiles.SetLamps(value.Lamps);
                lampShadows.SetLamps(value.Lamps);
            }
        }
    }

    /// <summary>
    /// How much of an environment box's ambient light reaches the light buffer. The engine scales it
    /// before the shaders get it (ir_light_ambient takes it as given); where, isn't found yet. Measured
    /// instead: the editor drawn from the running game's own camera (read through the link) next to
    /// the user's screenshot of the same view, at the first mission's start: at 1/8 the ground's
    /// colour matches within a few levels (game 53, 23, 9; editor 51, 26, 9), at the earlier guess of
    /// 1/32 it was half as bright. (The time of day's own ambient there is nearly nothing at noon.)
    /// </summary>
    public float AmbientScale { get; set; } = 1 / 8f;

    /// <summary>
    /// Where a cut-out material (see <see cref="GameMaterial"/>) is solid: its colour texture's alpha
    /// at least this. The game's shaders take it from the engine (Alpha_Threshold); its value isn't
    /// found yet, so the usual half.
    /// </summary>
    const float CutOutAlpha = 0.5f;

    /// <summary>Whether lamps that cast shadows do (game light); off, they light through everything, as the others do.</summary>
    public bool Shadows
    {
        get => shadows;
        set
        {
            if (value == shadows) return;
            shadows = value;
            shadowsLook = drawnLook = 0;
            if (gameLighting != null) lampShadows.SetLamps(gameLighting.Lamps);  // no maps until drawn again
        }
    }
    bool shadows = true;

    /// <summary>The game's settings where the camera is, as the last frame used them (for the log).</summary>
    public ZoneLighting.Settings? SettingsUsed { get; private set; }

    /// <summary>How many meshes and textures are on the card, and how many meshes the last frame drew.</summary>
    public (int Meshes, int Textures, int Drawn) Counts => (meshes.Count, textures.Count, drawn);
    int drawn;

    public SceneRenderer(GpuDevice gpu)
    {
        this.gpu = gpu;
        ID3D11Device device = gpu.Device;
        string source = ReadShader("Scene.hlsl");
        ReadOnlyMemory<byte> vertexCode = Compiler.Compile(source, "VertexMain", "Scene.hlsl", "vs_5_0", ShaderFlags.OptimizationLevel3, EffectFlags.None);
        ReadOnlyMemory<byte> pixelCode = Compiler.Compile(source, "PixelMain", "Scene.hlsl", "ps_5_0", ShaderFlags.OptimizationLevel3, EffectFlags.None);
        vertexShader = device.CreateVertexShader(vertexCode.Span);
        pixelShader = device.CreatePixelShader(pixelCode.Span);
        skyVertexShader = device.CreateVertexShader(Compiler.Compile(source, "SkyVertex", "Scene.hlsl", "vs_5_0", ShaderFlags.OptimizationLevel3, EffectFlags.None).Span);
        skyPixelShader = device.CreatePixelShader(Compiler.Compile(source, "SkyPixel", "Scene.hlsl", "ps_5_0", ShaderFlags.OptimizationLevel3, EffectFlags.None).Span);
        shadowPixelShader = device.CreatePixelShader(Compiler.Compile(source, "ShadowPixel", "Scene.hlsl", "ps_5_0", ShaderFlags.OptimizationLevel3, EffectFlags.None).Span);
        inputLayout = device.CreateInputLayout(
        [
            new InputElementDescription("POSITION", 0, Format.R32G32B32_Float, 0, 0),
            new InputElementDescription("NORMAL", 0, Format.R32G32B32_Float, 12, 0),
            new InputElementDescription("TEXCOORD", 0, Format.R32G32_Float, 24, 0),
        ], vertexCode.Span);
        frameBuffer = device.CreateBuffer((uint)Marshal.SizeOf<FrameConstants>(), BindFlags.ConstantBuffer, ResourceUsage.Dynamic, CpuAccessFlags.Write);
        drawBuffer = device.CreateBuffer((uint)Marshal.SizeOf<DrawConstants>(), BindFlags.ConstantBuffer, ResourceUsage.Dynamic, CpuAccessFlags.Write);
        sampler = device.CreateSamplerState(new SamplerDescription(Filter.Anisotropic, TextureAddressMode.Wrap, TextureAddressMode.Wrap, TextureAddressMode.Wrap) { MaxAnisotropy = 8 });
        // Shadow maps: each sample compares the pixel's depth with the map's four nearest, smoothly.
        shadowSampler = device.CreateSamplerState(new SamplerDescription(Filter.ComparisonMinMagLinearMipPoint,
            TextureAddressMode.Clamp, TextureAddressMode.Clamp, TextureAddressMode.Clamp, 0, 1, ComparisonFunction.LessEqual, 0, float.MaxValue));
        // WPF's front faces go round anticlockwise. Both states must say so: the shader turns a
        // two-sided surface's normal round on its back, and would light its front as its back.
        twoSided = device.CreateRasterizerState(new RasterizerDescription(CullMode.None, FillMode.Solid) { FrontCounterClockwise = true, DepthClipEnable = true });
        oneSided = device.CreateRasterizerState(new RasterizerDescription(CullMode.Back, FillMode.Solid) { FrontCounterClockwise = true, DepthClipEnable = true });
        // Shadow maps: both sides of everything, pushed a little away from the lamp where a surface
        // slants away from it, so lit surfaces don't shadow themselves.
        shadowRaster = device.CreateRasterizerState(new RasterizerDescription(CullMode.None, FillMode.Solid)
        {
            FrontCounterClockwise = true, DepthClipEnable = true, DepthBias = 16, SlopeScaledDepthBias = 2,
        });
        // Colours come already multiplied by how solid they are (see Scene.hlsl), as WPF expects.
        var blending = new BlendDescription(Blend.One, Blend.InverseSourceAlpha, Blend.One, Blend.InverseSourceAlpha);
        blend = device.CreateBlendState(blending);
        // Reversed depth (see RenderView): nearer is bigger.
        depthTest = device.CreateDepthStencilState(new DepthStencilDescription(true, DepthWriteMask.All, ComparisonFunction.GreaterEqual));
        depthTestOnly = device.CreateDepthStencilState(new DepthStencilDescription(true, DepthWriteMask.Zero, ComparisonFunction.GreaterEqual));
        noDepth = device.CreateDepthStencilState(new DepthStencilDescription(false, DepthWriteMask.Zero, ComparisonFunction.Always));
        shadowDepth = device.CreateDepthStencilState(new DepthStencilDescription(true, DepthWriteMask.All, ComparisonFunction.LessEqual));
        lampTiles = new LampTiles(gpu);
        lampShadows = new LampShadows(gpu);
    }

    static string ReadShader(string name)
    {
        using Stream stream = typeof(SceneRenderer).Assembly.GetManifestResourceStream($"RfaEditor.Render.Shaders.{name}")
            ?? throw new FileNotFoundException("shader not built into the editor: " + name);
        return new StreamReader(stream).ReadToEnd();
    }

    readonly List<DrawItem> items = [];
    readonly List<Light> lights = [];
    int drawnLook;  // what the last frame drew: the scene's parts and the camera, as one number
    int sceneLook;  // the scene's parts alone (for the shadow maps)
    int shadowsLook;  // what the shadow maps were drawn for: the scene's parts (0: not drawn)

    /// <summary>
    /// Gathers what a Viewport3D shows, and says whether it looks any different from the last frame
    /// drawn (a part added, removed, moved or changed; the camera moved; the view resized), so a
    /// still view isn't drawn again and again.
    /// </summary>
    public bool Changed(Viewport3D viewport, PerspectiveCamera camera, int width, int height)
    {
        items.Clear();
        lights.Clear();
        foreach (Visual3D visual in viewport.Children) Collect(visual, Matrix4x4.Identity, items, lights);
        var parts = new HashCode();
        foreach (DrawItem item in items) parts.Add(item);
        sceneLook = parts.ToHashCode();
        var look = new HashCode();
        look.Add(sceneLook);
        foreach (Light light in lights) look.Add(light);
        look.Add(camera.Position);
        look.Add(camera.LookDirection);
        look.Add(camera.UpDirection);
        look.Add(camera.FieldOfView);
        look.Add(width);
        look.Add(height);
        look.Add(gameLighting);
        look.Add(AmbientScale);
        if (gameLighting != null && lampTiles.Animated) look.Add(clock.ElapsedTicks);  // flickering lamps: every frame differs
        int now = look.ToHashCode();
        if (now == drawnLook) return false;
        drawnLook = now;
        return true;
    }

    /// <summary>Makes the next frame be drawn even if nothing changed (the picture was lost or made again).</summary>
    public void ForgetLastFrame() => drawnLook = shadowsLook = 0;

    /// <summary>How many shadow maps have been drawn (for the off-screen tests: moving about shouldn't draw many).</summary>
    public int ShadowMapsDrawn { get; private set; }

    /// <summary>
    /// Draws the shadow maps of the lamps near the camera that cast shadows (game light only), before
    /// the frame (see <see cref="RenderView.Draw"/>): all of them when the scene changed, else only
    /// those of lamps the camera came near. Each map is the depth of what <see cref="Changed"/> gathered, seen
    /// from the lamp; only the level's own things (textured, or with the game's material) cast
    /// shadows, not the editor's plain-coloured markers, and see-through ones only where their
    /// picture is solid.
    /// </summary>
    public void DrawShadows(ID3D11DeviceContext context, PerspectiveCamera camera)
    {
        if (gameLighting == null || !shadows) return;
        var at = new Vector3((float)camera.Position.X, (float)camera.Position.Y, (float)camera.Position.Z);
        bool picked = lampShadows.Choose(at, out IReadOnlyList<LampShadows.Map> added);
        IReadOnlyList<LampShadows.Map> drawing = shadowsLook != sceneLook ? lampShadows.Chosen : added;
        shadowsLook = sceneLook;
        if (drawing.Count == 0)
        {
            if (picked) lampShadows.Upload(context);  // some lamps' maps were let go
            return;
        }

        context.PSSetShaderResource(9, null!);  // the maps are drawn into now
        context.IASetInputLayout(inputLayout);
        context.IASetPrimitiveTopology(PrimitiveTopology.TriangleList);
        context.VSSetShader(vertexShader);
        context.VSSetConstantBuffer(0, frameBuffer);
        context.VSSetConstantBuffer(1, drawBuffer);
        context.PSSetConstantBuffer(1, drawBuffer);
        context.PSSetSampler(0, sampler);
        context.RSSetState(shadowRaster);
        context.RSSetViewport(0, 0, LampShadows.MapSize, LampShadows.MapSize, 0, 1);
        context.OMSetDepthStencilState(shadowDepth, 0);
        foreach (LampShadows.Map map in drawing)
        {
            ShadowMapsDrawn++;
            ID3D11DepthStencilView target = lampShadows.Target(map.Slot);
            context.OMSetRenderTargets(ReadOnlySpan<ID3D11RenderTargetView>.Empty, target);
            context.ClearDepthStencilView(target, DepthStencilClearFlags.Depth, 1, 0);
            Upload(context, frameBuffer, new FrameConstants { ViewProjection = map.ViewProjection });
            foreach (DrawItem item in items)
            {
                Material? material = item.Front ?? item.Back;
                (Vector4 colour, _, BitmapSource? picture) = Look(material);
                GameMaterial? carried = GameMaterial.Of(material);
                bool seeThrough = carried?.SeeThrough == true || carried?.CutOut == true || colour.W < 1 || (picture != null && KeepsAlpha(picture));
                if ((picture == null && carried == null) || Mesh(item.Mesh) is not GpuMesh mesh) continue;
                // Out of the lamp's reach: its ball, moved and scaled as the item is.
                Vector3 centre = Vector3.Transform(mesh.Centre, item.World);
                float scale = MathF.Max(new Vector3(item.World.M11, item.World.M12, item.World.M13).Length(),
                              MathF.Max(new Vector3(item.World.M21, item.World.M22, item.World.M23).Length(),
                                        new Vector3(item.World.M31, item.World.M32, item.World.M33).Length()));
                if (Vector3.Distance(centre, map.Position) > map.Reach + mesh.Radius * scale) continue;
                GpuTexture? texture = seeThrough ? ColourTexture(picture, carried, true).Texture : null;
                Upload(context, drawBuffer, new DrawConstants
                {
                    World = item.World,
                    Colour = colour,
                    Flags = new Vector4(texture != null ? 1 : 0, 0, 0, 0),
                    Options = new Vector4(1, 0, 0, 0),
                });
                context.PSSetShader(seeThrough ? shadowPixelShader : null!);
                context.PSSetShaderResource(0, texture?.View!);
                context.IASetVertexBuffer(0, mesh.Vertices, (uint)Marshal.SizeOf<Vertex>(), 0);
                context.IASetIndexBuffer(mesh.Indices, Format.R32_UInt, 0);
                context.DrawIndexed((uint)mesh.IndexCount, 0, 0);
            }
        }
        lampShadows.Upload(context);
    }

    /// <summary>Draws what <see cref="Changed"/> gathered, as the camera sees it.</summary>
    public void Draw(ID3D11DeviceContext context, PerspectiveCamera camera, int width, int height)
    {
        frame++;

        // The frame: camera, and lights from the scene (WPF's ambient and one-way lights).
        (Matrix4x4 view, Matrix4x4 projection, float scale, float aspect, float near) = Camera(camera, width, height);
        var constants = new FrameConstants
        {
            ViewProjection = view * projection,
            CameraPosition = new Vector4((float)camera.Position.X, (float)camera.Position.Y, (float)camera.Position.Z, 1),
        };
        var directions = new Vector4[4];
        var colours = new Vector4[4];
        int used = 0;
        foreach (Light light in lights)
        {
            if (light is AmbientLight ambient) constants.Ambient += ColourOf(ambient.Color);
            else if (light is DirectionalLight directional && used < 4)
            {
                Vector3D d = directional.Direction;
                directions[used] = new Vector4(Vector3.Normalize(new Vector3((float)d.X, (float)d.Y, (float)d.Z)), 0);
                colours[used++] = ColourOf(directional.Color);
            }
        }
        (constants.Direction0, constants.Direction1, constants.Direction2, constants.Direction3) = (directions[0], directions[1], directions[2], directions[3]);
        (constants.Colour0, constants.Colour1, constants.Colour2, constants.Colour3) = (colours[0], colours[1], colours[2], colours[3]);

        // The game's light: the settings where the camera is, and which lamps reach which tile.
        SettingsUsed = null;
        GpuTexture? sky = null;
        if (gameLighting != null)
        {
            var at = new Vector3((float)camera.Position.X, (float)camera.Position.Y, -(float)camera.Position.Z);  // the game's Z
            ZoneLighting.Settings settings = gameLighting.At(at);
            SettingsUsed = settings;
            lampTiles.Update(context, view, scale, aspect, near, width, height);
            lampTiles.UpdateMotion(context, (float)clock.Elapsed.TotalSeconds);
            constants.Mode = new Vector4(1, LampTiles.TileSize, lampTiles.TilesAcross, LampTiles.PerTile);
            constants.AmbientAbove = new Vector4(settings.AmbientAbove, 0);
            constants.AmbientBelow = new Vector4(settings.AmbientBelow, 0);
            constants.Tint = new Vector4(settings.Tint, settings.Exposure);
            constants.FogColour = new Vector4(settings.FogColour, settings.Fog ? 1 : 0);
            constants.FogRange = new Vector4(settings.FogNear, settings.FogFar, 0, 0);
            constants.Scales = new Vector4(AmbientScale, 1, 0, 0);
            // The sky: which way each corner of the view looks, for SkyPixel (big pictures: they fill the view).
            sky = NamedTexture(settings.Sky, 1024);
            if (sky != null)
            {
                Matrix4x4.Invert(view, out Matrix4x4 turn);  // its rows: the camera's right, up and backward
                constants.SkyRight = new Vector4(turn.M11, turn.M12, turn.M13, 0) * (aspect / scale);
                constants.SkyUp = new Vector4(turn.M21, turn.M22, turn.M23, 0) / scale;
                constants.SkyForward = -new Vector4(turn.M31, turn.M32, turn.M33, 0);
            }
            context.PSSetShaderResource(1, lampTiles.LampView!);
            context.PSSetShaderResource(2, lampTiles.TileView!);
            context.PSSetShaderResource(6, lampTiles.MotionView!);
            context.PSSetShaderResource(7, lampShadows.ShadowView!);
            context.PSSetShaderResource(8, lampShadows.ViewsView);
            context.PSSetShaderResource(9, lampShadows.MapView);
            context.PSSetSampler(1, shadowSampler);
        }
        Upload(context, frameBuffer, constants);

        context.IASetPrimitiveTopology(PrimitiveTopology.TriangleList);
        context.VSSetConstantBuffer(0, frameBuffer);
        context.VSSetConstantBuffer(1, drawBuffer);
        context.PSSetConstantBuffer(0, frameBuffer);
        context.PSSetConstantBuffer(1, drawBuffer);
        context.PSSetSampler(0, sampler);
        context.OMSetBlendState(blend);
        if (sky != null)
        {
            // One triangle over the whole view, behind everything else.
            context.IASetInputLayout(null);
            context.VSSetShader(skyVertexShader);
            context.PSSetShader(skyPixelShader);
            context.OMSetDepthStencilState(noDepth, 0);
            context.RSSetState(twoSided);
            context.PSSetShaderResource(0, sky.View);
            context.Draw(3, 0);
        }
        context.IASetInputLayout(inputLayout);
        context.VSSetShader(vertexShader);
        context.PSSetShader(pixelShader);
        context.OMSetDepthStencilState(depthTest, 0);

        drawn = 0;
        // Solid things first, then see-through ones over them without hiding what is behind them.
        bool SeeThrough(DrawItem item) =>
            GameMaterial.Of(item.Front ?? item.Back)?.SeeThrough == true || (item.Front ?? item.Back) is DiffuseMaterial { Brush.Opacity: < 1 };
        foreach (DrawItem item in items.Where(i => !SeeThrough(i)).Concat(items.Where(SeeThrough)))
        {
            if (Mesh(item.Mesh) is not GpuMesh mesh) continue;
            context.OMSetDepthStencilState(SeeThrough(item) ? depthTestOnly : depthTest, 0);
            (Vector4 colour, Vector4 emissive, BitmapSource? picture) = Look(item.Front ?? item.Back);
            GameMaterial? carried = GameMaterial.Of(item.Front ?? item.Back);
            // The game material's maps, in game light only (they are read when first needed).
            GameMaterial? game = gameLighting != null ? carried : null;
            if (game?.Picture != null) picture = game.Picture.Value;  // its own picture for game light (the ground)
            // Cut out (hair, capes, bullet holes): solid where its alpha reaches the game's threshold.
            bool cutOut = carried?.CutOut == true && !SeeThrough(item);
            (GpuTexture? texture, bool alpha) = ColourTexture(picture, game?.Picture == null ? carried : null, SeeThrough(item) || cutOut);
            GpuTexture? normals = NamedTexture(game?.NormalMap), shine = NamedTexture(game?.SpecularMap), glow = NamedTexture(game?.GlowMap);
            AlienSkin? skin = game?.Skin;
            Upload(context, drawBuffer, new DrawConstants
            {
                World = item.World,
                Colour = colour,
                Emissive = emissive,
                Flags = new Vector4(texture != null ? 1 : 0, mesh.HasNormals ? 1 : 0, normals != null ? 1 : 0, shine != null ? 1 : 0),
                DiffuseColour = game?.DiffuseColour ?? Vector4.One,
                Shine = game != null ? new Vector4(game.SpecularColour.X, game.SpecularColour.Y, game.SpecularColour.Z, game.SpecularPower) : Vector4.Zero,
                Surface = game != null ? new Vector4(game.NormalHeight, game.SelfIllumination, game.GlowFalloffPower, game.GlowFalloffInversion) : Vector4.Zero,
                Glow = game != null && glow != null ? new Vector4(game.GlowColour.X, game.GlowColour.Y, game.GlowColour.Z, 1) : Vector4.Zero,
                Options = new Vector4(alpha ? 1 : 0, cutOut && texture != null ? CutOutAlpha : 0, 0, 0),
                Skin = skin != null ? new Vector4(skin.Full ? 1 : 2, skin.RimLightAmount, skin.RimLightPower, 0) : Vector4.Zero,
            });
            context.PSSetShaderResource(0, texture?.View!);
            context.PSSetShaderResource(3, normals?.View!);
            context.PSSetShaderResource(4, shine?.View!);
            context.PSSetShaderResource(5, glow?.View!);
            context.RSSetState(item.Back != null ? twoSided : oneSided);
            context.IASetVertexBuffer(0, mesh.Vertices, (uint)Marshal.SizeOf<Vertex>(), 0);
            context.IASetIndexBuffer(mesh.Indices, Format.R32_UInt, 0);
            context.DrawIndexed((uint)mesh.IndexCount, 0, 0);
            drawn++;
        }
        ForgetUnused();
    }

    /// <summary>
    /// The camera's view and projection: the same view WPF's camera gives, with "reversed" depth
    /// (1 at the near plane, towards 0 far away; see RenderView) and no far plane. Also the
    /// projection's top-to-bottom scale, the view's width / height and the near plane, for the lamps' tiles.
    /// </summary>
    static (Matrix4x4 View, Matrix4x4 Projection, float Scale, float Aspect, float Near) Camera(PerspectiveCamera camera, int width, int height)
    {
        Point3D p = camera.Position;
        Vector3D look = camera.LookDirection, up = camera.UpDirection;
        var position = new Vector3((float)p.X, (float)p.Y, (float)p.Z);
        Matrix4x4 view = Matrix4x4.CreateLookAt(position, position + new Vector3((float)look.X, (float)look.Y, (float)look.Z),
                                                new Vector3((float)up.X, (float)up.Y, (float)up.Z));
        // WPF's field of view is side to side; the projection wants top to bottom.
        double aspect = (double)width / height;
        double vertical = 2 * Math.Atan(Math.Tan(camera.FieldOfView * Math.PI / 360) / aspect);
        float scale = (float)(1 / Math.Tan(vertical / 2)), near = (float)Math.Max(0.01, camera.NearPlaneDistance);
        var projection = new Matrix4x4(
            scale / (float)aspect, 0, 0, 0,
            0, scale, 0, 0,
            0, 0, 0, -1,
            0, 0, near, 0);
        return (view, projection, scale, (float)aspect, near);
    }

    /// <summary>Gathers the meshes (and lights) under a visual, with their placement in the scene.</summary>
    static void Collect(Visual3D visual, Matrix4x4 parent, List<DrawItem> items, List<Light> lights)
    {
        if (visual is not ModelVisual3D model) return;
        Matrix4x4 placement = Combine(model.Transform, parent);
        if (model.Content != null) Collect(model.Content, placement, items, lights);
        foreach (Visual3D child in model.Children) Collect(child, placement, items, lights);
    }

    static void Collect(Model3D model, Matrix4x4 parent, List<DrawItem> items, List<Light> lights)
    {
        Matrix4x4 placement = Combine(model.Transform, parent);
        switch (model)
        {
            case Model3DGroup group:
                foreach (Model3D child in group.Children) Collect(child, placement, items, lights);
                break;
            case GeometryModel3D { Geometry: MeshGeometry3D mesh } geometry:
                items.Add(new DrawItem(mesh, geometry.Material, geometry.BackMaterial, placement));
                break;
            case Light light:
                lights.Add(light);
                break;
        }
    }

    static Matrix4x4 Combine(Transform3D? transform, Matrix4x4 parent)
    {
        if (transform == null || transform.Value.IsIdentity) return parent;
        Matrix3D m = transform.Value;
        var own = new Matrix4x4((float)m.M11, (float)m.M12, (float)m.M13, (float)m.M14,
                                (float)m.M21, (float)m.M22, (float)m.M23, (float)m.M24,
                                (float)m.M31, (float)m.M32, (float)m.M33, (float)m.M34,
                                (float)m.OffsetX, (float)m.OffsetY, (float)m.OffsetZ, (float)m.M44);
        return own * parent;
    }

    /// <summary>A material's colour (with how solid it is), glow and picture, from WPF's diffuse and emissive materials.</summary>
    static (Vector4 Colour, Vector4 Emissive, BitmapSource? Picture) Look(Material? material)
    {
        Vector4 colour = Vector4.One, emissive = Vector4.Zero;
        BitmapSource? picture = null;
        foreach (Material part in material is MaterialGroup group ? group.Children : material != null ? [material] : [])
        {
            if (part is DiffuseMaterial diffuse)
            {
                Vector4 tint = ColourOf(diffuse.Color);
                switch (diffuse.Brush)
                {
                    case SolidColorBrush solid:
                        colour = ColourOf(solid.Color) * tint;
                        colour.W *= (float)solid.Opacity;
                        break;
                    case ImageBrush { ImageSource: BitmapSource image } brush:
                        picture = image;
                        colour = tint;
                        colour.W *= (float)brush.Opacity;
                        break;
                }
            }
            else if (part is EmissiveMaterial { Brush: SolidColorBrush glow })
                emissive = ColourOf(glow.Color);
        }
        return (colour, emissive, picture);
    }

    static Vector4 ColourOf(Color c) => new(c.R / 255f, c.G / 255f, c.B / 255f, c.A / 255f);

    /// <summary>A mesh's buffers on the card, made the first time it is drawn.</summary>
    GpuMesh? Mesh(MeshGeometry3D mesh)
    {
        if (meshes.TryGetValue(mesh, out GpuMesh? known))
        {
            known.LastUsed = frame;
            return known;
        }
        int count = mesh.Positions.Count;
        if (count == 0) return null;
        bool hasNormals = mesh.Normals.Count == count, hasUv = mesh.TextureCoordinates.Count == count;
        var vertices = new Vertex[count];
        for (int i = 0; i < count; i++)
        {
            Point3D p = mesh.Positions[i];
            Vector3D n = hasNormals ? mesh.Normals[i] : default;
            System.Windows.Point uv = hasUv ? mesh.TextureCoordinates[i] : default;
            vertices[i] = new Vertex(new Vector3((float)p.X, (float)p.Y, (float)p.Z), new Vector3((float)n.X, (float)n.Y, (float)n.Z),
                                     new Vector2((float)uv.X, (float)uv.Y));
        }
        // Without a triangle list, WPF takes the points three by three.
        uint[] indices = mesh.TriangleIndices.Count > 0 ? mesh.TriangleIndices.Select(i => (uint)i).ToArray()
                                                        : Enumerable.Range(0, count - count % 3).Select(i => (uint)i).ToArray();
        if (indices.Length == 0) return null;
        Vector3 low = vertices.Select(v => v.Position).Aggregate(Vector3.Min), high = vertices.Select(v => v.Position).Aggregate(Vector3.Max);
        var made = new GpuMesh(gpu.Device.CreateBuffer(vertices, BindFlags.VertexBuffer), gpu.Device.CreateBuffer(indices, BindFlags.IndexBuffer),
                               indices.Length, hasNormals, (low + high) / 2, Vector3.Distance(low, high) / 2) { LastUsed = frame };
        meshes[mesh] = made;
        return made;
    }

    /// <summary>
    /// One of the game's textures by file name, on the card: as stored if it can be found that way,
    /// else decoded (at most so many pixels across). Read once; null if it can't be found.
    /// </summary>
    GpuTexture? NamedTexture(string? name, int largest = 256)
    {
        if (name == null) return null;
        if (!namedTextures.TryGetValue(name, out GpuTexture? known))
        {
            known = FindTexture?.Invoke(name) is var (peg, texture) ? Upload(peg, texture) : null;
            if (known == null && LoadTexture?.Invoke(name, largest) is BitmapSource picture) known = Upload(picture);
            namedTextures[name] = known;
        }
        return known;
    }

    /// <summary>
    /// A surface's colour texture: the game's own, as stored, where its material names it and WPF
    /// shows a picture for it (textures on); else WPF's picture. And whether to take its alpha:
    /// WPF's pictures keep alpha only where it means see-through; the game's textures keep it
    /// always, so it is taken only for see-through surfaces (and those whose WPF picture kept it).
    /// </summary>
    (GpuTexture? Texture, bool Alpha) ColourTexture(BitmapSource? picture, GameMaterial? material, bool seeThrough)
    {
        if (picture == null) return (null, true);
        if (NamedTexture(material?.DiffuseMap) is GpuTexture stored) return (stored, seeThrough || KeepsAlpha(picture));
        return (Texture(picture), true);
    }

    /// <summary>Whether a WPF picture has see-through pixels (it was made with its alpha kept).</summary>
    bool KeepsAlpha(BitmapSource picture)
    {
        if (picturesWithAlpha.TryGetValue(picture, out bool known)) return known;
        BitmapSource bgra = picture.Format == PixelFormats.Bgra32 ? picture : new FormatConvertedBitmap(picture, PixelFormats.Bgra32, null, 0);
        var pixels = new byte[bgra.PixelWidth * bgra.PixelHeight * 4];
        bgra.CopyPixels(pixels, bgra.PixelWidth * 4, 0);
        bool any = false;
        for (int i = 3; i < pixels.Length && !any; i += 4) any = pixels[i] < 255;
        return picturesWithAlpha[picture] = any;
    }

    /// <summary>
    /// One of the game's textures on the card as stored: DXT1, DXT3 and DXT5 (BC1 to BC3) and 32-bit
    /// colour as they are, with each of the game's smaller copies; other formats (and compressed
    /// ones not a whole number of 4-pixel blocks across) decoded first.
    /// </summary>
    GpuTexture Upload(PegFile peg, PegTexture texture)
    {
        Format? format = texture.Format switch
        {
            TextureFormat.Dxt1 => Format.BC1_UNorm,
            TextureFormat.Dxt3 => Format.BC2_UNorm,
            TextureFormat.Dxt5 => Format.BC3_UNorm,
            TextureFormat.Argb8888 => Format.B8G8R8A8_UNorm,
            _ => null,
        };
        bool blocks = texture.Format.IsBlockCompressed();
        if (format == null || (blocks && (texture.Width % 4 != 0 || texture.Height % 4 != 0)))
            return Upload(Bitmaps.FromRgba(peg.Decode(texture), alpha: true));

        byte[] data = peg.TextureData(texture);
        var levels = new List<(int Offset, int Pitch)>();
        int at = 0;
        for (int level = 0; level < Math.Max(1, texture.MipLevels); level++)
        {
            int width = Math.Max(1, texture.Width >> level), height = Math.Max(1, texture.Height >> level);
            int size = texture.Format.LevelSize(width, height);
            if (at + size > data.Length) break;
            levels.Add((at, blocks ? Math.Max(1, (width + 3) / 4) * (texture.Format == TextureFormat.Dxt1 ? 8 : 16) : width * 4));
            at += size;
            if (width == 1 && height == 1) break;
        }
        if (levels.Count == 0) return Upload(Bitmaps.FromRgba(peg.Decode(texture), alpha: true));
        GCHandle pinned = GCHandle.Alloc(data, GCHandleType.Pinned);
        try
        {
            SubresourceData[] initial = levels.Select(l => new SubresourceData(pinned.AddrOfPinnedObject() + l.Offset, (uint)l.Pitch)).ToArray();
            var description = new Texture2DDescription(format.Value, (uint)texture.Width, (uint)texture.Height, 1, (uint)levels.Count,
                                                       BindFlags.ShaderResource, ResourceUsage.Immutable);
            ID3D11Texture2D made = gpu.Device.CreateTexture2D(description, initial);
            return new GpuTexture(made, gpu.Device.CreateShaderResourceView(made)) { LastUsed = frame };
        }
        finally { pinned.Free(); }
    }

    /// <summary>A picture as a texture on the card, with smaller copies made for far away.</summary>
    GpuTexture Texture(BitmapSource picture)
    {
        if (textures.TryGetValue(picture, out GpuTexture? known))
        {
            known.LastUsed = frame;
            return known;
        }
        GpuTexture made = Upload(picture);
        textures[picture] = made;
        return made;
    }

    GpuTexture Upload(BitmapSource picture)
    {
        BitmapSource bgra = picture.Format == PixelFormats.Bgra32 ? picture : new FormatConvertedBitmap(picture, PixelFormats.Bgra32, null, 0);
        int width = bgra.PixelWidth, height = bgra.PixelHeight;
        var pixels = new byte[width * height * 4];
        bgra.CopyPixels(pixels, width * 4, 0);
        var description = new Texture2DDescription(Format.B8G8R8A8_UNorm, (uint)width, (uint)height, 1, 0,
            BindFlags.ShaderResource | BindFlags.RenderTarget, ResourceUsage.Default, CpuAccessFlags.None, 1, 0, ResourceOptionFlags.GenerateMips);
        ID3D11Texture2D texture = gpu.Device.CreateTexture2D(description);
        gpu.Context.UpdateSubresource(pixels, texture, 0, (uint)(width * 4), 0);
        ID3D11ShaderResourceView view = gpu.Device.CreateShaderResourceView(texture);
        gpu.Context.GenerateMips(view);
        return new GpuTexture(texture, view) { LastUsed = frame };
    }

    /// <summary>Lets go of meshes and textures not drawn for a while (the scene was rebuilt, or the view moved on).</summary>
    void ForgetUnused()
    {
        if (frame % 60 != 0) return;
        foreach (var (key, mesh) in meshes.Where(m => frame - m.Value.LastUsed > 120).ToList())
        {
            mesh.Dispose();
            meshes.Remove(key);
        }
        foreach (var (key, texture) in textures.Where(t => frame - t.Value.LastUsed > 120).ToList())
        {
            texture.Dispose();
            textures.Remove(key);
        }
    }

    static void Upload<T>(ID3D11DeviceContext context, ID3D11Buffer buffer, T value) where T : unmanaged
    {
        Vortice.Direct3D11.MappedSubresource mapped = context.Map(buffer, MapMode.WriteDiscard);
        Marshal.StructureToPtr(value, mapped.DataPointer, false);
        context.Unmap(buffer, 0);
    }

    public void Dispose()
    {
        lampTiles.Dispose();
        lampShadows.Dispose();
        foreach (GpuMesh mesh in meshes.Values) mesh.Dispose();
        foreach (GpuTexture texture in textures.Values) texture.Dispose();
        foreach (GpuTexture? texture in namedTextures.Values) texture?.Dispose();
        meshes.Clear();
        textures.Clear();
        namedTextures.Clear();
        vertexShader.Dispose();
        pixelShader.Dispose();
        skyVertexShader.Dispose();
        skyPixelShader.Dispose();
        shadowPixelShader.Dispose();
        shadowSampler.Dispose();
        shadowRaster.Dispose();
        shadowDepth.Dispose();
        inputLayout.Dispose();
        frameBuffer.Dispose();
        drawBuffer.Dispose();
        sampler.Dispose();
        twoSided.Dispose();
        oneSided.Dispose();
        blend.Dispose();
        depthTest.Dispose();
        depthTestOnly.Dispose();
        noDepth.Dispose();
    }
}
