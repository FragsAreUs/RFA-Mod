using System.Numerics;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using RfaFormats.Effects;
using RfaFormats.Textures;

namespace RfaEditor;

/// <summary>
/// An emitter's or corona's look: its colour texture, and the mask a masked shader cuts it with (or none), each with
/// its flip-book tiles (across, down; 1 × 1 for a plain texture).
/// </summary>
sealed class ParticleSprite(RgbaImage image, (int Across, int Down) tiles, RgbaImage? mask, (int Across, int Down) maskTiles)
{
    public RgbaImage Image { get; } = image;
    public (int Across, int Down) Tiles { get; } = tiles;
    public RgbaImage? Mask { get; } = mask;
    public (int Across, int Down) MaskTiles { get; } = maskTiles;
    public bool MaskHasAlpha { get; } = mask != null && HasAlpha(mask);

    static bool HasAlpha(RgbaImage image)
    {
        for (int i = 3; i < image.Pixels.Length; i += 4)
            if (image.Pixels[i] < 250) return true;
        return false;
    }
}

/// <summary>What the player draws an effect with: its parts' textures, and which parts are shown.</summary>
sealed class EffectLooks
{
    public ParticleSprite?[] Emitters = [];
    public ParticleSprite?[] Coronas = [];
    public Dictionary<string, RgbaImage?> MeshTextures = new(StringComparer.OrdinalIgnoreCase);  // by texture name
    public bool[] EmittersShown = [];
    public bool[] CoronasShown = [];
    public bool[] MeshesShown = [];
    public bool[] LightsShown = [];
}

/// <summary>
/// Draws an effect into a picture, on the processor, over a dark ground with a one-metre grid:
/// <list type="bullet">
/// <item>its meshes (light rays, rings, debris), textured, by their position, rotation, scale and opacity keys;</item>
/// <item>its particles: textured squares facing the camera (or, for drops and oriented particles that use a length,
/// stretched along the way they fly); mesh particles as the effect's mesh they name; ribbons as a strip through their
/// particles in the order they were made (those sent from one node particle together: a trail behind it);</item>
/// <item>its coronas, glows facing the camera, last, on top.</item>
/// </list>
/// Everything but the coronas is drawn back to front (no depth buffer), blended by its alpha or added (glowing) by its
/// additive key; solid meshes are shaded by a light from above and lit by the effect's lights, which also light pools on
/// the ground (each marked by a small ring where it is). Particles an orientor turns lie flat in the world. Its own small renderer, because WPF's 3D can't add light
/// (only blend by alpha); bands of rows are drawn on all the processor's cores at once. The camera turns around the
/// effect's middle.
/// </summary>
sealed class EffectPlayer
{
    const float FieldOfView = 50 * MathF.PI / 180, Near = 0.05f;
    static readonly Vector3 Background = new(0.09f, 0.09f, 0.10f), GridColour = new(0.17f, 0.17f, 0.19f);
    static readonly Vector3 LightFrom = Vector3.Normalize(new Vector3(0.4f, 1, 0.3f));

    WriteableBitmap? bitmap;
    float[] colour = [];       // red, green, blue for each pixel, 0..1 (and over)
    byte[] pixels = [];        // blue, green, red, alpha for the screen
    int width, height;

    public float Yaw { get; set; } = 0.7f;
    public float Pitch { get; set; } = 0.3f;
    public float Distance { get; set; } = 6;
    public Vector3 Target { get; set; } = new(0, 1, 0);
    public float GridSize { get; set; } = 3;

    /// <summary>The picture, made again when the size changes (at most about 0.3 megapixels, for speed).</summary>
    public WriteableBitmap? Bitmap => bitmap;

    public void Resize(double viewWidth, double viewHeight)
    {
        if (viewWidth < 8 || viewHeight < 8) return;
        double scale = Math.Min(1, Math.Sqrt(300_000 / (viewWidth * viewHeight)));
        int w = Math.Max(8, (int)(viewWidth * scale)), h = Math.Max(8, (int)(viewHeight * scale));
        if (w == width && h == height && bitmap != null) return;
        width = w;
        height = h;
        bitmap = new WriteableBitmap(w, h, 96, 96, PixelFormats.Bgra32, null);
        colour = new float[w * h * 3];
        pixels = new byte[w * h * 4];
    }

    // The camera's place and its right, up and forward directions.
    readonly record struct View(Vector3 Eye, Vector3 Right, Vector3 Up, Vector3 Forward, float Focal);

    View Camera()
    {
        Vector3 eye = Target + Distance * new Vector3(MathF.Cos(Pitch) * MathF.Sin(Yaw), MathF.Sin(Pitch), MathF.Cos(Pitch) * MathF.Cos(Yaw));
        Vector3 forward = Vector3.Normalize(Target - eye);
        Vector3 right = Vector3.Normalize(Vector3.Cross(forward, Vector3.UnitY));
        Vector3 up = Vector3.Cross(right, forward);
        return new View(eye, right, up, forward, height / 2f / MathF.Tan(FieldOfView / 2));
    }

    // ---- What is drawn ----------------------------------------------------------------------------------

    // A particle or corona ready to draw: where it is on screen (its middle and its two half-axes), the pixels it covers,
    // its colour and how it blends, and the frame of its texture (and mask).
    sealed record Quad(
        Vector2 Centre, Vector2 Along, Vector2 Across, int Left, int Right, int Top, int Bottom, Vector4 Tint, float Saturation,
        float Additive, float Brightness, ParticleSprite? Sprite, (float Left, float Top, float Width, float Height) Frame,
        (float Left, float Top, float Width, float Height) MaskFrame);

    // A triangle of a mesh, a ribbon or a flat-lying particle: its corners on screen (x, y, and 1 / depth), their texture
    // places and colours, its texture, and how it blends: solid (shaded and lit), or by alpha and the additive key as a
    // particle.
    sealed record Triangle(
        Vector3 A, Vector3 B, Vector3 C, Vector2 UvA, Vector2 UvB, Vector2 UvC, Vector4 TintA, Vector4 TintB, Vector4 TintC,
        RgbaImage? Texture, bool Solid, Vector3 Shade, float Additive, float Brightness, int Left, int Right, int Top, int Bottom);

    // A light as it is now: where, its colour times its strength, and the light itself (for its reach).
    readonly record struct LitBy(Vector3 Place, Vector3 Colour, LightSettings Light);

    /// <summary>Draws the effect as it is now.</summary>
    public void Draw(EffectSimulation simulation, IReadOnlyList<EffectMesh> meshes, IReadOnlyList<CoronaSettings> coronas,
                     IReadOnlyList<LightSettings> lights, EffectLooks looks)
    {
        if (bitmap == null) return;
        View view = Camera();
        float time = simulation.Time;

        // The lights shown, as they are now. Their intensity (1 to 100 in the files) is eased into 0..1, so a weak lamp
        // still shows and a blinding flash doesn't swamp everything.
        var lit = new List<LitBy>();
        for (int l = 0; l < lights.Count; l++)
        {
            if (l < looks.LightsShown.Length && !looks.LightsShown[l]) continue;
            float strength = 1 - MathF.Exp(-Math.Max(0, lights[l].Intensity.Number(time)) * 0.25f);
            Vector3 c = lights[l].Colour.Vector(time) * strength;
            if (c.LengthSquared() > 1e-6f) lit.Add(new LitBy(lights[l].Position.Vector(time), c, lights[l]));
        }

        // The background, and the ground (Y = 0) lit where the lights reach it.
        Parallel.For(0, height, y => FillBackground(view, y, lit, time));
        DrawGrid(view);

        var shapes = new List<(float Depth, Quad? Quad, Triangle? Triangle)>();

        // The meshes, but those mesh particles use (they're drawn where the particles are).
        var particleMeshes = simulation.Emitters.Where(e => e.ParticleType == 10 && e.MeshName != null).Select(e => e.MeshName!)
                                                .ToHashSet(StringComparer.OrdinalIgnoreCase);
        for (int m = 0; m < meshes.Count; m++)
        {
            EffectMesh mesh = meshes[m];
            if (m < looks.MeshesShown.Length && !looks.MeshesShown[m] || particleMeshes.Contains(mesh.Name)) continue;
            float opacity = Math.Clamp(mesh.Opacity.Number(time), 0, 1);
            if (opacity <= 0.002f) continue;
            Matrix4x4 place = Matrix4x4.CreateScale(mesh.Scale.Vector(time)) * Matrix4x4.CreateFromQuaternion(mesh.Rotation.Rotation(time)) *
                              Matrix4x4.CreateTranslation(mesh.Position.Vector(time));
            AddMesh(shapes, view, mesh, place, new Vector4(1, 1, 1, opacity), mesh.Additive, looks, lit, time);
        }

        // The particles, back to front with the meshes.
        foreach (Particle p in simulation.Particles)
        {
            if (p.Emitter < looks.EmittersShown.Length && !looks.EmittersShown[p.Emitter]) continue;
            EmitterSettings e = simulation.Emitters[p.Emitter];
            if (e.ParticleType is 2 or 6) continue;  // ribbons below; node particles aren't drawn
            float depth = Vector3.Dot(p.Position - view.Eye, view.Forward);
            if (depth <= Near) continue;
            if (e.ParticleType == 10)
            {
                if (meshes.FirstOrDefault(x => x.Name.Equals(e.MeshName, StringComparison.OrdinalIgnoreCase)) is not { } mesh) continue;
                float life = p.Life;
                // The shape turns about a slanted axis by the particle's angle; its size scales it.
                Matrix4x4 place = Matrix4x4.CreateScale(Math.Max(0.01f, p.Size.At(life))) *
                                  Matrix4x4.CreateFromAxisAngle(Vector3.Normalize(new Vector3(1, 0.6f, 0.3f)), p.Angle) *
                                  Matrix4x4.CreateTranslation(p.Position);
                AddMesh(shapes, view, mesh, place, Vector4.One, false, looks, lit, time);
                continue;
            }
            // An orientor's particle lies flat in the world, turned the orientor's way.
            if (simulation.OrientorOf(p.Emitter) is { } facing)
            {
                AddFlatParticle(shapes, view, depth, p, e, facing, p.Emitter < looks.Emitters.Length ? looks.Emitters[p.Emitter] : null);
                continue;
            }
            if (Prepare(view, depth, p, e, p.Emitter < looks.Emitters.Length ? looks.Emitters[p.Emitter] : null) is { } quad)
                shapes.Add((depth, quad, null));
        }

        // The ribbons: a strip through each one's particles in the order they were made, one strip for each node.
        for (int r = 0; r < simulation.Emitters.Count; r++)
        {
            EmitterSettings e = simulation.Emitters[r];
            if (e.ParticleType != 2 || r < looks.EmittersShown.Length && !looks.EmittersShown[r]) continue;
            ParticleSprite? sprite = r < looks.Emitters.Length ? looks.Emitters[r] : null;
            foreach (var strip in simulation.Particles.Where(p => p.Emitter == r).GroupBy(p => p.Node))
            {
                var points = strip.OrderBy(p => p.Id).ToList();
                for (int k = 0; k + 1 < points.Count; k++) AddRibbonPiece(shapes, view, e, sprite, points[k], points[k + 1]);
            }
        }

        shapes.Sort((a, b) => b.Depth.CompareTo(a.Depth));

        // The coronas, last: glows on top of everything.
        for (int c = 0; c < coronas.Count; c++)
        {
            if (c < looks.CoronasShown.Length && !looks.CoronasShown[c]) continue;
            if (PrepareCorona(view, coronas[c], time, c < looks.Coronas.Length ? looks.Coronas[c] : null) is { } quad) shapes.Add((0, quad, null));
        }

        // Bands of rows drawn at the same time, each with every shape in order: a pixel is only ever touched by its own
        // band, so the blending is the same as drawing them one by one.
        int bands = Math.Clamp(Environment.ProcessorCount * 2, 1, 32), rows = (height + bands - 1) / bands;
        Parallel.For(0, bands, band =>
        {
            int top = band * rows, bottom = Math.Min(height - 1, top + rows - 1);
            foreach (var shape in shapes)
            {
                if (shape.Quad != null) Fill(shape.Quad, top, bottom);
                else if (shape.Triangle != null) Fill(shape.Triangle, top, bottom);
            }
        });

        foreach (LitBy light in lit) DrawLightMark(view, light);

        for (int i = 0, o = 0; i < colour.Length; i += 3, o += 4)
        {
            pixels[o] = ToByte(colour[i + 2]);
            pixels[o + 1] = ToByte(colour[i + 1]);
            pixels[o + 2] = ToByte(colour[i]);
            pixels[o + 3] = 255;
        }
        bitmap.WritePixels(new Int32Rect(0, 0, width, height), pixels, width * 4, 0);
    }

    static byte ToByte(float value) => (byte)Math.Clamp((int)(value * 255 + 0.5f), 0, 255);

    // One row of the background: dark, and where the row's pixels see the ground (Y = 0) in reach of a light, that
    // light's colour on it, brighter where it falls from above.
    void FillBackground(View view, int y, List<LitBy> lit, float time)
    {
        for (int x = 0; x < width; x++)
        {
            Vector3 c = Background;
            if (lit.Count > 0)
            {
                Vector3 ray = view.Forward + view.Right * ((x + 0.5f - width / 2f) / view.Focal) - view.Up * ((y + 0.5f - height / 2f) / view.Focal);
                if (ray.Y < -1e-4f)
                {
                    Vector3 ground = view.Eye + ray * (-view.Eye.Y / ray.Y);
                    foreach (LitBy light in lit)
                    {
                        Vector3 toLight = light.Place - ground;
                        // Brighter where it falls from above, but a lamp at ground level still lights the floor round it.
                        float facing = toLight.LengthSquared() > 1e-8f ? 0.35f + 0.65f * Math.Max(0, Vector3.Normalize(toLight).Y) : 1;
                        c += light.Colour * (0.6f * facing * light.Light.Reach(time, ground));
                    }
                }
            }
            int at = (y * width + x) * 3;
            colour[at] = c.X;
            colour[at + 1] = c.Y;
            colour[at + 2] = c.Z;
        }
    }

    // A small ring in a light's colour where it is, so a light whose pool is off the ground can be found.
    void DrawLightMark(View view, LitBy light)
    {
        var at = Project(view, light.Place);
        if (at.Depth <= Near) return;
        Vector3 c = Vector3.Normalize(light.Colour + new Vector3(1e-3f)) * 1.2f;
        for (int k = 0; k < 48; k++)
        {
            float angle = k / 48f * MathF.PI * 2;
            int x = (int)(at.X + MathF.Cos(angle) * 6), y = (int)(at.Y + MathF.Sin(angle) * 6);
            if (x < 0 || y < 0 || x >= width || y >= height) continue;
            int i = (y * width + x) * 3;
            colour[i] = c.X;
            colour[i + 1] = c.Y;
            colour[i + 2] = c.Z;
        }
    }

    (float X, float Y, float Depth) Project(View view, Vector3 point)
    {
        Vector3 d = point - view.Eye;
        float depth = Vector3.Dot(d, view.Forward);
        if (depth <= Near) return (0, 0, depth);
        return (width / 2f + Vector3.Dot(d, view.Right) * view.Focal / depth, height / 2f - Vector3.Dot(d, view.Up) * view.Focal / depth, depth);
    }

    // A grid on the ground (Y = 0), a line every metre, to show the effect's size.
    void DrawGrid(View view)
    {
        int n = (int)Math.Clamp(MathF.Ceiling(GridSize), 1, 50);
        for (int i = -n; i <= n; i++)
        {
            DrawLine(view, new Vector3(i, 0, -n), new Vector3(i, 0, n));
            DrawLine(view, new Vector3(-n, 0, i), new Vector3(n, 0, i));
        }
    }

    void DrawLine(View view, Vector3 from, Vector3 to)
    {
        // In short pieces, so a line passing behind the camera is cut rather than flipped.
        const int Pieces = 40;
        for (int k = 0; k < Pieces; k++)
        {
            var a = Project(view, Vector3.Lerp(from, to, k / (float)Pieces));
            var b = Project(view, Vector3.Lerp(from, to, (k + 1) / (float)Pieces));
            if (a.Depth <= Near || b.Depth <= Near) continue;
            int steps = (int)Math.Max(Math.Abs(b.X - a.X), Math.Abs(b.Y - a.Y)) + 1;
            for (int s = 0; s <= steps; s++)
            {
                int x = (int)(a.X + (b.X - a.X) * s / steps), y = (int)(a.Y + (b.Y - a.Y) * s / steps);
                if (x < 0 || y < 0 || x >= width || y >= height) continue;
                int at = (y * width + x) * 3;
                colour[at] = GridColour.X;
                colour[at + 1] = GridColour.Y;
                colour[at + 2] = GridColour.Z;
            }
        }
    }

    // ---- Particles and coronas (squares) ---------------------------------------------------------------

    Quad? Prepare(View view, float depth, Particle p, EmitterSettings e, ParticleSprite? sprite)
    {
        float life = p.Life;
        Vector4 tint = e.ColourAt(life);
        if (tint.W <= 0.002f) return null;
        float size = p.Size.At(life);
        if (size <= 0) return null;
        var centre = Project(view, p.Position);
        float scale = view.Focal / depth;

        // The square's two half-axes on screen: turned by the particle's angle, or along its flight when stretched.
        Vector2 along, across;
        bool stretched = e.UseLength && e.ParticleType is 7 or 8 && p.Velocity.LengthSquared() > 1e-6f;
        if (stretched)
        {
            var ahead = Project(view, p.Position + Vector3.Normalize(p.Velocity) * 0.1f);
            Vector2 way = new(ahead.X - centre.X, ahead.Y - centre.Y);
            way = way.LengthSquared() > 1e-6f ? Vector2.Normalize(way) : Vector2.UnitY;
            along = way * (Math.Max(size, p.Length.At(life)) / 2 * scale);
            across = new Vector2(-way.Y, way.X) * (size / 2 * scale);
        }
        else
        {
            float half = size / 2 * scale, c = MathF.Cos(p.Angle), s = MathF.Sin(p.Angle);
            along = new Vector2(c, s) * half;
            across = new Vector2(-s, c) * half;
        }
        float saturation = e.UseSaturation ? Math.Clamp(e.Saturation.At(life), 0, 1) : 1;
        // The additive key: 0 blends by alpha, 1 adds (glows); above 1 it adds that many times as bright.
        float additiveKey = e.UseAdditive ? Math.Max(0, e.Additive.At(life)) : 0;
        var frame = sprite == null ? (0f, 0f, 1f, 1f) : Tile(sprite.Tiles, p.Frame(sprite.Tiles.Across * sprite.Tiles.Down));
        var maskFrame = sprite == null ? (0f, 0f, 1f, 1f) : Tile(sprite.MaskTiles, p.Frame(sprite.MaskTiles.Across * sprite.MaskTiles.Down));
        return MakeQuad(new Vector2(centre.X, centre.Y), along, across, tint, saturation, Math.Min(1, additiveKey), Math.Max(1, additiveKey),
                        sprite, frame, maskFrame);
    }

    Quad? MakeQuad(Vector2 centre, Vector2 along, Vector2 across, Vector4 tint, float saturation, float additive, float brightness,
                   ParticleSprite? sprite, (float, float, float, float) frame, (float, float, float, float) maskFrame)
    {
        float reach = MathF.Abs(along.X) + MathF.Abs(across.X), reachY = MathF.Abs(along.Y) + MathF.Abs(across.Y);
        if (reach < 0.3f && reachY < 0.3f) return null;  // smaller than a pixel
        int x0 = Math.Max(0, (int)(centre.X - reach)), x1 = Math.Min(width - 1, (int)(centre.X + reach) + 1);
        int y0 = Math.Max(0, (int)(centre.Y - reachY)), y1 = Math.Min(height - 1, (int)(centre.Y + reachY) + 1);
        if (x0 > x1 || y0 > y1) return null;
        return new Quad(centre, along, across, x0, x1, y0, y1, tint, saturation, additive, brightness, sprite, frame, maskFrame);
    }

    // A corona: a glow at its place, upright on screen, its width a part of its height, added to what is behind it.
    Quad? PrepareCorona(View view, CoronaSettings corona, float time, ParticleSprite? sprite)
    {
        Vector3 place = corona.Position.Vector(time);
        var centre = Project(view, place);
        if (centre.Depth <= Near) return null;
        var (halfHeight, halfWidth, glow, saturation) = corona.At(time, Vector3.Distance(place, view.Eye));
        if (glow.W <= 0.002f || halfHeight <= 0) return null;
        float scale = view.Focal / centre.Depth;
        return MakeQuad(new Vector2(centre.X, centre.Y), new Vector2(0, halfHeight * scale), new Vector2(halfWidth * scale, 0), glow, saturation,
                        1, 1, sprite, (0, 0, 1, 1), (0, 0, 1, 1));
    }

    // Draws the rows of a square from `top` to `bottom` (one band of the picture).
    void Fill(Quad q, int top, int bottom)
    {
        int y0 = Math.Max(q.Top, top), y1 = Math.Min(q.Bottom, bottom);
        if (y0 > y1) return;
        float alongLength2 = q.Along.LengthSquared(), acrossLength2 = q.Across.LengthSquared();
        ParticleSprite? sprite = q.Sprite;
        for (int y = y0; y <= y1; y++)
        {
            for (int x = q.Left; x <= q.Right; x++)
            {
                float dx = x + 0.5f - q.Centre.X, dy = y + 0.5f - q.Centre.Y;
                float u = (dx * q.Along.X + dy * q.Along.Y) / alongLength2, v = (dx * q.Across.X + dy * q.Across.Y) / acrossLength2;
                if (u < -1 || u > 1 || v < -1 || v > 1) continue;
                float tu = (v + 1) / 2, tv = (u + 1) / 2;  // the texture's up along the particle's length
                Vector4 texel = sprite == null ? SoftDot(u, v)
                              : Sample(sprite.Image, q.Frame.Left + tu * q.Frame.Width, q.Frame.Top + tv * q.Frame.Height);
                float alpha = q.Tint.W * texel.W;
                if (alpha <= 0.002f) continue;
                if (sprite?.Mask is { } mask)
                {
                    Vector4 m = Sample(mask, q.MaskFrame.Left + tu * q.MaskFrame.Width, q.MaskFrame.Top + tv * q.MaskFrame.Height);
                    alpha *= sprite.MaskHasAlpha ? m.W : (m.X + m.Y + m.Z) / 3;
                    if (alpha <= 0.002f) continue;
                }
                Blend(y * width + x, new Vector3(q.Tint.X * texel.X, q.Tint.Y * texel.Y, q.Tint.Z * texel.Z), alpha, q.Saturation, q.Additive, q.Brightness);
            }
        }
    }

    // A colour laid over a pixel: kept by (1 − alpha × (1 − additive)), the colour added by alpha × brightness.
    void Blend(int pixel, Vector3 c, float alpha, float saturation, float additive, float brightness)
    {
        if (saturation < 1)
        {
            float grey = c.X * 0.3f + c.Y * 0.59f + c.Z * 0.11f;
            c = new Vector3(grey) + (c - new Vector3(grey)) * saturation;
        }
        int at = pixel * 3;
        float keep = 1 - alpha * (1 - additive), add = alpha * brightness;
        colour[at] = colour[at] * keep + c.X * add;
        colour[at + 1] = colour[at + 1] * keep + c.Y * add;
        colour[at + 2] = colour[at + 2] * keep + c.Z * add;
    }

    // ---- Meshes and ribbons (triangles) -----------------------------------------------------------------

    // A mesh placed by `place`: each triangle in front of the camera, textured by its material's colour texture. Solid
    // unless it adds (its type) or its material is see-through (blended or cut out) or its tint is.
    void AddMesh(List<(float, Quad?, Triangle?)> shapes, View view, EffectMesh mesh, Matrix4x4 place, Vector4 tint, bool additive, EffectLooks looks,
                 List<LitBy> lit, float time)
    {
        if (tint.W <= 0.002f) return;
        var world = new Vector3[mesh.Positions.Length];
        var screen = new (float X, float Y, float Depth)[mesh.Positions.Length];
        for (int v = 0; v < world.Length; v++)
        {
            world[v] = Vector3.Transform(mesh.Positions[v], place);
            screen[v] = Project(view, world[v]);
        }
        foreach (var (material, triangles) in mesh.Parts)
        {
            string? textureName = mesh.TextureOf(material);
            RgbaImage? texture = textureName != null && looks.MeshTextures.TryGetValue(textureName, out var t) ? t : null;
            var look = material >= 0 && material < mesh.Materials.Count ? mesh.Materials[material] : null;
            bool seeThrough = look != null && (look.Blended || look.CutOut);
            // A see-through material that glows (the effects' glow masks: light rays, rings) adds light; its
            // Main_Opacity and Diffuse_Color, when it has them, scale it.
            bool glows = seeThrough && look!.Constant("Self_Illumination") != null;
            Vector4 partTint = tint;
            if (look?.Constant("Diffuse_Color") is { } diffuse) partTint *= new Vector4(diffuse.X, diffuse.Y, diffuse.Z, 1);
            if (seeThrough && look!.Constant("Main_Opacity") is { } opacity) partTint.W *= Math.Clamp(opacity.X, 0, 1);
            bool adds = additive || glows;
            bool solid = !adds && !seeThrough && tint.W >= 0.99f;
            for (int i = 0; i + 2 < triangles.Length; i += 3)
            {
                int a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
                var (pa, pb, pc) = (screen[a], screen[b], screen[c]);
                if (pa.Depth <= Near || pb.Depth <= Near || pc.Depth <= Near) continue;
                Vector3 normal = Vector3.Cross(world[b] - world[a], world[c] - world[a]);
                Vector3 n = normal.LengthSquared() > 1e-12f ? Vector3.Normalize(normal) : Vector3.UnitY;
                Vector3 shade = new(0.4f + 0.6f * MathF.Abs(Vector3.Dot(n, LightFrom)));
                if (solid && lit.Count > 0)
                {
                    // The effect's own lights on it too, as on the ground (either face: the meshes are thin).
                    Vector3 middle = (world[a] + world[b] + world[c]) / 3;
                    foreach (LitBy light in lit)
                    {
                        Vector3 toLight = light.Place - middle;
                        float facing = toLight.LengthSquared() > 1e-8f ? MathF.Abs(Vector3.Dot(n, Vector3.Normalize(toLight))) : 1;
                        shade += light.Colour * (facing * light.Light.Reach(time, middle));
                    }
                }
                if (MakeTriangle(pa, pb, pc, mesh.TextureCoordinates[a], mesh.TextureCoordinates[b], mesh.TextureCoordinates[c], partTint, partTint, partTint,
                                 texture, solid, shade, adds ? 1 : 0, 1) is { } triangle)
                    shapes.Add(((pa.Depth + pb.Depth + pc.Depth) / 3, null, triangle));
            }
        }
    }

    // A piece of a ribbon between two of its particles: a band facing the camera, as wide as each end's size, its
    // texture across it and along it by the particles' lives.
    // A particle an orientor turns: a square lying in the world, its face along the orientor's Z (the ground, for the
    // water's ripples), turned in that plane by the particle's angle, its texture's frame as on a facing particle.
    void AddFlatParticle(List<(float, Quad?, Triangle?)> shapes, View view, float depth, Particle p, EmitterSettings e, Quaternion facing, ParticleSprite? sprite)
    {
        float life = p.Life;
        Vector4 tint = e.ColourAt(life);
        float half = p.Size.At(life) / 2;
        if (tint.W <= 0.002f || half <= 0) return;
        Quaternion turn = facing * Quaternion.CreateFromAxisAngle(Vector3.UnitZ, p.Angle);
        Vector3 across = Vector3.Transform(Vector3.UnitX, turn) * half, along = Vector3.Transform(Vector3.UnitY, turn) * half;
        var corners = new[] { p.Position - across - along, p.Position + across - along, p.Position + across + along, p.Position - across + along }
            .Select(c => Project(view, c)).ToArray();
        if (corners.Any(c => c.Depth <= Near)) return;
        var frame = sprite == null ? (0f, 0f, 1f, 1f) : Tile(sprite.Tiles, p.Frame(sprite.Tiles.Across * sprite.Tiles.Down));
        Vector2 Uv(float u, float v) => new(frame.Item1 + u * frame.Item3, frame.Item2 + v * frame.Item4);
        float additiveKey = e.UseAdditive ? Math.Max(0, e.Additive.At(life)) : 0;
        float additive = Math.Min(1, additiveKey), brightness = Math.Max(1, additiveKey);
        RgbaImage? texture = sprite?.Image;
        if (MakeTriangle(corners[0], corners[1], corners[2], Uv(0, 0), Uv(1, 0), Uv(1, 1), tint, tint, tint, texture, false, Vector3.One, additive, brightness) is { } first)
            shapes.Add((depth, null, first));
        if (MakeTriangle(corners[0], corners[2], corners[3], Uv(0, 0), Uv(1, 1), Uv(0, 1), tint, tint, tint, texture, false, Vector3.One, additive, brightness) is { } second)
            shapes.Add((depth, null, second));
    }

    void AddRibbonPiece(List<(float, Quad?, Triangle?)> shapes, View view, EmitterSettings e, ParticleSprite? sprite, Particle from, Particle to)
    {
        var a = Project(view, from.Position);
        var b = Project(view, to.Position);
        if (a.Depth <= Near || b.Depth <= Near) return;
        Vector2 way = new(b.X - a.X, b.Y - a.Y);
        if (way.LengthSquared() < 1e-6f) return;
        way = Vector2.Normalize(way);
        Vector2 side = new(-way.Y, way.X);
        float lifeA = from.Life, lifeB = to.Life;
        float halfA = Math.Max(0, from.Size.At(lifeA)) / 2 * view.Focal / a.Depth, halfB = Math.Max(0, to.Size.At(lifeB)) / 2 * view.Focal / b.Depth;
        Vector4 tintA = e.ColourAt(lifeA), tintB = e.ColourAt(lifeB);
        float additiveKey = e.UseAdditive ? Math.Max(0, e.Additive.At((lifeA + lifeB) / 2)) : 0;
        var a1 = (a.X + side.X * halfA, a.Y + side.Y * halfA, a.Depth);
        var a2 = (a.X - side.X * halfA, a.Y - side.Y * halfA, a.Depth);
        var b1 = (b.X + side.X * halfB, b.Y + side.Y * halfB, b.Depth);
        var b2 = (b.X - side.X * halfB, b.Y - side.Y * halfB, b.Depth);
        RgbaImage? texture = sprite?.Image;
        float depth = (a.Depth + b.Depth) / 2;
        if (MakeTriangle(a1, a2, b1, new Vector2(0, lifeA), new Vector2(1, lifeA), new Vector2(0, lifeB), tintA, tintA, tintB, texture, false, Vector3.One,
                         Math.Min(1, additiveKey), Math.Max(1, additiveKey)) is { } first)
            shapes.Add((depth, null, first));
        if (MakeTriangle(a2, b2, b1, new Vector2(1, lifeA), new Vector2(1, lifeB), new Vector2(0, lifeB), tintA, tintB, tintB, texture, false, Vector3.One,
                         Math.Min(1, additiveKey), Math.Max(1, additiveKey)) is { } second)
            shapes.Add((depth, null, second));
    }

    Triangle? MakeTriangle((float X, float Y, float Depth) a, (float X, float Y, float Depth) b, (float X, float Y, float Depth) c,
                           Vector2 uvA, Vector2 uvB, Vector2 uvC, Vector4 tintA, Vector4 tintB, Vector4 tintC, RgbaImage? texture,
                           bool solid, Vector3 shade, float additive, float brightness)
    {
        int x0 = Math.Max(0, (int)MathF.Floor(Math.Min(a.X, Math.Min(b.X, c.X)))), x1 = Math.Min(width - 1, (int)MathF.Ceiling(Math.Max(a.X, Math.Max(b.X, c.X))));
        int y0 = Math.Max(0, (int)MathF.Floor(Math.Min(a.Y, Math.Min(b.Y, c.Y)))), y1 = Math.Min(height - 1, (int)MathF.Ceiling(Math.Max(a.Y, Math.Max(b.Y, c.Y))));
        if (x0 > x1 || y0 > y1) return null;
        return new Triangle(new Vector3(a.X, a.Y, 1 / a.Depth), new Vector3(b.X, b.Y, 1 / b.Depth), new Vector3(c.X, c.Y, 1 / c.Depth), uvA, uvB, uvC,
                            tintA, tintB, tintC, texture, solid, shade, additive, brightness, x0, x1, y0, y1);
    }

    // Draws the rows of a triangle from `top` to `bottom`: each pixel inside it, its texture place worked out with
    // the depth (so textures don't swim), its colour blended between the corners'.
    void Fill(Triangle t, int top, int bottom)
    {
        int y0 = Math.Max(t.Top, top), y1 = Math.Min(t.Bottom, bottom);
        if (y0 > y1) return;
        float area = (t.B.X - t.A.X) * (t.C.Y - t.A.Y) - (t.B.Y - t.A.Y) * (t.C.X - t.A.X);
        if (MathF.Abs(area) < 1e-6f) return;
        float inverse = 1 / area;
        for (int y = y0; y <= y1; y++)
        {
            float py = y + 0.5f;
            for (int x = t.Left; x <= t.Right; x++)
            {
                float px = x + 0.5f;
                float wa = ((t.B.X - px) * (t.C.Y - py) - (t.B.Y - py) * (t.C.X - px)) * inverse;
                float wb = ((t.C.X - px) * (t.A.Y - py) - (t.C.Y - py) * (t.A.X - px)) * inverse;
                float wc = 1 - wa - wb;
                if (wa < 0 || wb < 0 || wc < 0) continue;
                float z = wa * t.A.Z + wb * t.B.Z + wc * t.C.Z;
                Vector2 uv = (t.UvA * (wa * t.A.Z) + t.UvB * (wb * t.B.Z) + t.UvC * (wc * t.C.Z)) / z;
                Vector4 tint = t.TintA * wa + t.TintB * wb + t.TintC * wc;
                Vector4 texel = t.Texture == null ? Vector4.One : SampleWrapped(t.Texture, uv.X, uv.Y);
                Vector3 c = new(tint.X * texel.X, tint.Y * texel.Y, tint.Z * texel.Z);
                int pixel = y * width + x;
                if (t.Solid)
                {
                    int at = pixel * 3;
                    colour[at] = c.X * t.Shade.X;
                    colour[at + 1] = c.Y * t.Shade.Y;
                    colour[at + 2] = c.Z * t.Shade.Z;
                    continue;
                }
                float alpha = tint.W * texel.W;
                if (alpha > 0.002f) Blend(pixel, c, alpha, 1, t.Additive, t.Brightness);
            }
        }
    }

    // ---- Textures -------------------------------------------------------------------------------------------

    // Where frame number `index` of a grid of tiles is, as parts of the texture (left to right, then top to bottom).
    static (float Left, float Top, float Width, float Height) Tile((int Across, int Down) tiles, int index)
    {
        float w = 1f / tiles.Across, h = 1f / tiles.Down;
        return (index % tiles.Across * w, index / tiles.Across % tiles.Down * h, w, h);
    }

    // A round, soft-edged dot, for an emitter whose texture isn't found.
    static Vector4 SoftDot(float u, float v)
    {
        float r = MathF.Sqrt(u * u + v * v);
        return new Vector4(1, 1, 1, Math.Clamp(1 - r, 0, 1));
    }

    // A mesh's texture repeats: its places past 0..1 wrap round.
    static Vector4 SampleWrapped(RgbaImage image, float u, float v) => Sample(image, u - MathF.Floor(u), v - MathF.Floor(v));

    // The texture at (u, v), 0..1 across, blended between its four nearest pixels.
    static Vector4 Sample(RgbaImage image, float u, float v)
    {
        float fx = Math.Clamp(u * image.Width - 0.5f, 0, image.Width - 1), fy = Math.Clamp(v * image.Height - 0.5f, 0, image.Height - 1);
        int x0 = (int)fx, y0 = (int)fy, x1 = Math.Min(x0 + 1, image.Width - 1), y1 = Math.Min(y0 + 1, image.Height - 1);
        float wx = fx - x0, wy = fy - y0;
        Vector4 top = Vector4.Lerp(Pixel(image, x0, y0), Pixel(image, x1, y0), wx);
        Vector4 bottom = Vector4.Lerp(Pixel(image, x0, y1), Pixel(image, x1, y1), wx);
        return Vector4.Lerp(top, bottom, wy);
    }

    static Vector4 Pixel(RgbaImage image, int x, int y)
    {
        int at = (y * image.Width + x) * 4;
        byte[] p = image.Pixels;
        return new Vector4(p[at] / 255f, p[at + 1] / 255f, p[at + 2] / 255f, p[at + 3] / 255f);
    }
}
