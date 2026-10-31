using System.Numerics;
using System.Windows.Media.Imaging;
using RfaFormats.Meshes;
using RfaFormats.Textures;

namespace RfaEditor;

/// <summary>
/// Paints the ground's texture the way the game's terrain shader does (see <see cref="ZoneTerrain"/>):
/// four texture layers, each repeated at its own scale, mixed one over another by the zone's
/// blend map. WPF 3D can't mix textures itself, so each terrain sub-zone gets one picture,
/// painted once, covering its area at <see cref="PixelsPerMetre"/>. Without the layers it paints
/// the zone's colour map instead (the game's own picture of the ground from far away).
///
/// The layers' textures hold dark, linear colours, which the game's lighting and display curve
/// lift and soften. The editor can't light them the same way, so each layer's colours are
/// multiplied (per colour channel) so that its average colour becomes scale × (average)^gamma,
/// with the gamma and scale that make the mixed layers match the zone's colour map best (for
/// world_08_02 a gamma of about 0.2). The texture's own light and dark detail stays as it is.
/// Without a colour map the usual display gamma, 1/2.2, is used.
///
/// The Direct3D view's game light lights the ground itself, so it gets a picture mixed as the
/// game's ground shader (ir_terrain_height_mesh_t) mixes it, nothing lifted: each layer's colour
/// times its Diffuse_color_N (<see cref="TerrainLayer.Tint"/>), mixed by the blend map, worked
/// out in linear light and stored as sRGB (<see cref="PaintAsInGame"/>). Lifted, the lit ground
/// came out pale grey-blue where the game's is dark red-brown.
/// </summary>
sealed class TerrainPainter
{
    /// <summary>The picture's detail. 12 pixels a metre is about 1,000 pixels for a sub-zone.</summary>
    public const int PixelsPerMetre = 12;

    const float ZoneSize = 255;

    readonly ZoneTerrain? ground;      // null: paint the colour map
    readonly RgbaImage map;             // the blend map, or the colour map
    readonly RgbaImage[] layers;        // the layers' textures, as decoded
    readonly Vector3[] gains;           // per layer: what its colours are multiplied by
    readonly Vector2 zoneCorner;
    bool asInGame;  // mix as the game does (see PaintAsInGame)

    TerrainPainter(ZoneTerrain? ground, RgbaImage map, RgbaImage[] layers, Vector3[] gains, Vector2 zoneCorner)
    {
        this.ground = ground;
        this.map = map;
        this.layers = layers;
        this.gains = gains;
        this.zoneCorner = zoneCorner;
    }

    /// <summary>The curve used, for the log: "gamma 0.32, scale 1.05".</summary>
    public string Curve { get; private init; } = "";

    /// <param name="layers">The four layers' textures, in order, as decoded.</param>
    /// <param name="zoneCorner">The zone's corner (lowest x and z): where the blend map starts.</param>
    /// <param name="colourMap">The zone's colour map, to fit the curve to; or null.</param>
    public static TerrainPainter ForLayers(ZoneTerrain ground, RgbaImage blend, RgbaImage[] layers, Vector2 zoneCorner, RgbaImage? colourMap)
    {
        (float gamma, float scale) = colourMap == null ? (1 / 2.2f, 1.25f) : FitCurve(ground, blend, layers, colourMap);
        Vector3[] gains = layers.Select((texture, i) =>
        {
            Vector3 average = Tinted(Average(texture), ground.Layers[i].Tint);  // 0-255
            Vector3 wanted = 255 * scale * ThroughCurve(average / 255, gamma);
            return Tinted(wanted / Vector3.Max(average, Vector3.One), ground.Layers[i].Tint);
        }).ToArray();
        return new TerrainPainter(ground, blend, layers, gains, zoneCorner) { Curve = $"gamma {gamma:0.00}, scale {scale:0.00}" };
    }

    /// <summary>A painter of the zone's colour map, stretched over the zone from its corner.</summary>
    public static TerrainPainter ForColourMap(RgbaImage colourMap, Vector2 zoneCorner) => new(null, colourMap, [], [], zoneCorner);

    /// <summary>
    /// The size to decode a layer's texture at: one mip level more than the pixels a repeat of it
    /// takes in the painted picture, so it is sharp but not grainy.
    /// </summary>
    public static int DetailNeeded(TerrainLayer layer)
    {
        float metres = Math.Abs(layer.ScaleOffset.X) < 1e-3f ? ZoneSize : ZoneSize / Math.Abs(layer.ScaleOffset.X);
        return (int)Math.Clamp(metres * PixelsPerMetre * 2, 8, 1024);
    }

    /// <summary>
    /// The ground over an area as the game's shader mixes it, for game light (see above). The
    /// colour map, where there are no layers, is the same either way.
    /// </summary>
    public BitmapSource PaintAsInGame(Vector2 from, Vector2 to)
    {
        if (ground == null) return Paint(from, to);
        var painter = new TerrainPainter(ground, map, layers, ground.Layers.Select(l => new Vector3(l.Tint.X, l.Tint.Y, l.Tint.Z)).ToArray(), zoneCorner)
        {
            asInGame = true,
        };
        return painter.Paint(from, to);
    }

    /// <summary>The ground over an area (x and z from, x and z to), as a picture: rows along z.</summary>
    public BitmapSource Paint(Vector2 from, Vector2 to)
    {
        int width = Math.Clamp((int)MathF.Ceiling((to.X - from.X) * PixelsPerMetre), 1, 2048);
        int height = Math.Clamp((int)MathF.Ceiling((to.Y - from.Y) * PixelsPerMetre), 1, 2048);
        var pixels = new byte[width * height * 4];
        Parallel.For(0, height, row =>
        {
            float z = from.Y + (row + 0.5f) / height * (to.Y - from.Y);
            for (int column = 0; column < width; column++)
            {
                float x = from.X + (column + 0.5f) / width * (to.X - from.X);
                Vector3 colour = ColourAt(x, z);
                int p = (row * width + column) * 4;
                pixels[p] = (byte)Math.Clamp(colour.X, 0, 255);
                pixels[p + 1] = (byte)Math.Clamp(colour.Y, 0, 255);
                pixels[p + 2] = (byte)Math.Clamp(colour.Z, 0, 255);
                pixels[p + 3] = 255;
            }
        });
        return Bitmaps.FromRgba(new RgbaImage(width, height, pixels), alpha: false);
    }

    /// <summary>The ground's colour at a point of the world (0-255 per channel).</summary>
    Vector3 ColourAt(float x, float z)
    {
        // The blend map (or colour map) covers the zone from its corner.
        Vector4 here = Sample(map, (x - zoneCorner.X) / ZoneSize * map.Width - 0.5f, (z - zoneCorner.Y) / ZoneSize * map.Height - 0.5f, wrap: false);
        if (ground == null) return new Vector3(here.X, here.Y, here.Z);

        Span<float> weights = stackalloc float[ZoneTerrain.LayerCount];
        Weights(here.X / 255, here.Y / 255, here.Z / 255, weights);
        Vector3 colour = Vector3.Zero;
        for (int i = 0; i < ZoneTerrain.LayerCount; i++)
        {
            if (weights[i] <= 0) continue;
            Vector4 so = ground.Layers[i].ScaleOffset;  // texture coordinates = (world / 255 + offset) × scale
            float u = (x / ZoneSize + so.Z) * so.X, v = (z / ZoneSize + so.W) * so.Y;
            RgbaImage texture = layers[i];
            Vector4 texel = Sample(texture, Fraction(u) * texture.Width - 0.5f, Fraction(v) * texture.Height - 0.5f, wrap: true);
            Vector3 texture3 = new(texel.X, texel.Y, texel.Z);
            colour += weights[i] * (asInGame ? ToLinear(texture3 / 255) : texture3) * gains[i];
        }
        return asInGame ? ToSrgb(colour) * 255 : colour;
    }

    // The screen's sRGB curve, both ways (as the Direct3D view's shader takes pictures).
    static Vector3 ToLinear(Vector3 c) => new(ToLinear(c.X), ToLinear(c.Y), ToLinear(c.Z));
    static float ToLinear(float c) => c <= 0.04045f ? c / 12.92f : MathF.Pow((c + 0.055f) / 1.055f, 2.4f);
    static Vector3 ToSrgb(Vector3 c) => new(ToSrgb(c.X), ToSrgb(c.Y), ToSrgb(c.Z));
    static float ToSrgb(float c) => c <= 0.0031308f ? c * 12.92f : 1.055f * MathF.Pow(Math.Clamp(c, 0, 1), 1 / 2.4f) - 0.055f;

    /// <summary>The layers' weights from the blend map's red, green and blue: they lay layers 3, 2 and 1 over layer 0.</summary>
    static void Weights(float r, float g, float b, Span<float> weights)
    {
        weights[0] = (1 - b) * (1 - g) * (1 - r);
        weights[1] = b * (1 - g) * (1 - r);
        weights[2] = g * (1 - r);
        weights[3] = r;
    }

    /// <summary>
    /// The gamma and scale with which the mixed layers come closest to the colour map: for each
    /// gamma tried, each layer's average colour through the curve is mixed at every blend map
    /// pixel, the best scale for it found by least squares, and the gamma with the smallest
    /// difference kept.
    /// </summary>
    static (float Gamma, float Scale) FitCurve(ZoneTerrain ground, RgbaImage blend, RgbaImage[] layers, RgbaImage colourMap)
    {
        (float Gamma, float Scale, double Error) best = (1 / 2.2f, 1, double.MaxValue);
        Span<float> weights = stackalloc float[ZoneTerrain.LayerCount];
        for (float gamma = 0.2f; gamma <= 1.001f; gamma += 0.02f)
        {
            Vector3[] averages = layers.Select((texture, i) => ThroughCurve(Tinted(Average(texture), ground.Layers[i].Tint) / 255, gamma)).ToArray();
            var mixed = new Vector3[blend.Width * blend.Height];
            var wanted = new Vector3[mixed.Length];
            double mixedTimesWanted = 0, mixedSquared = 0;
            for (int y = 0; y < blend.Height; y++)
                for (int x = 0; x < blend.Width; x++)
                {
                    int i = y * blend.Width + x, p = i * 4;
                    Weights(blend.Pixels[p] / 255f, blend.Pixels[p + 1] / 255f, blend.Pixels[p + 2] / 255f, weights);
                    for (int l = 0; l < ZoneTerrain.LayerCount; l++) mixed[i] += weights[l] * averages[l];
                    int c = ((y * colourMap.Height / blend.Height) * colourMap.Width + x * colourMap.Width / blend.Width) * 4;
                    wanted[i] = new Vector3(colourMap.Pixels[c], colourMap.Pixels[c + 1], colourMap.Pixels[c + 2]) / 255;
                    mixedTimesWanted += Vector3.Dot(mixed[i], wanted[i]);
                    mixedSquared += Vector3.Dot(mixed[i], mixed[i]);
                }
            float scale = (float)(mixedTimesWanted / Math.Max(mixedSquared, 1e-9));
            double error = 0;
            for (int i = 0; i < mixed.Length; i++) error += (mixed[i] * scale - wanted[i]).LengthSquared();
            if (error < best.Error) best = (gamma, scale, error);
        }
        return (best.Gamma, best.Scale);
    }

    static Vector3 ThroughCurve(Vector3 colour, float gamma) =>
        new(MathF.Pow(colour.X, gamma), MathF.Pow(colour.Y, gamma), MathF.Pow(colour.Z, gamma));

    static Vector3 Tinted(Vector3 colour, Vector4 tint) => colour * new Vector3(tint.X, tint.Y, tint.Z);

    static Vector3 Average(RgbaImage image)
    {
        Vector3 sum = Vector3.Zero;
        for (int p = 0; p < image.Pixels.Length; p += 4) sum += new Vector3(image.Pixels[p], image.Pixels[p + 1], image.Pixels[p + 2]);
        return sum / Math.Max(1, image.Pixels.Length / 4);
    }

    /// <summary>A picture's colour between pixels (bilinear), either repeating or stopping at its edges.</summary>
    static Vector4 Sample(RgbaImage image, float px, float py, bool wrap)
    {
        int x0 = (int)MathF.Floor(px), y0 = (int)MathF.Floor(py);
        float fx = px - x0, fy = py - y0;
        Vector4 Texel(int x, int y)
        {
            x = wrap ? ((x % image.Width) + image.Width) % image.Width : Math.Clamp(x, 0, image.Width - 1);
            y = wrap ? ((y % image.Height) + image.Height) % image.Height : Math.Clamp(y, 0, image.Height - 1);
            int p = (y * image.Width + x) * 4;
            return new Vector4(image.Pixels[p], image.Pixels[p + 1], image.Pixels[p + 2], image.Pixels[p + 3]);
        }
        Vector4 top = Vector4.Lerp(Texel(x0, y0), Texel(x0 + 1, y0), fx);
        Vector4 bottom = Vector4.Lerp(Texel(x0, y0 + 1), Texel(x0 + 1, y0 + 1), fx);
        return Vector4.Lerp(top, bottom, fy);
    }

    static float Fraction(float value) => value - MathF.Floor(value);
}
