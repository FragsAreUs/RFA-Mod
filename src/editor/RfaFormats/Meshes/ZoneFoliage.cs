using System.Numerics;
using System.Text;
using System.Text.RegularExpressions;

namespace RfaFormats.Meshes;

/// <summary>
/// Where a zone's foliage grows (debris clumps, rubble and rocks: the .cfmesh_pc meshes), from its
/// terrain file (.cterrain_pc). The file doesn't list the foliage one by one: it marks which 1 m
/// cubes of the zone have ground for it to grow on, and the game scatters the meshes over them
/// itself, the same way every time; <see cref="Scatter"/> does it the same way. 11 zones have
/// foliage (`world_08_02`, the first mission's, and ten others).
///
/// The terrain file's header block comes after its three name blocks (sizes at 0x10, 0x1C and
/// 0x18, from 0x28, each to a multiple of 4): +0x04 the zone's box (from x, y, z, to x, y, z;
/// see <see cref="Corner"/>),
/// +0xAC the number of foliage layers, +0xB4 the number of cells, +0xBC the number of patches.
/// The foliage section is much further on (after the blend map); it is found by its mesh names,
/// which only it has with the ".fmeshx" ending:
///   layers, 32 bytes each: model count, (run-time pointer), max density, max and min fade
///     distance, placement method (1 in every zone), random seed, cell size (4 m in every zone)
///   models, 16 bytes each, the first layer's then the next's: where its mesh name starts in the
///     name block, weight (u16), whether pieces tilt to the slope they land on (u8, 1 in every
///     zone), scale variance (u8, out of 255), two colours (bytes blue,
///     green, red, alpha) its tint is picked between
///   a name block for each layer: its mesh names ("f_cm_trash_a.fmeshx"), NUL-separated, to a
///     multiple of 4
///   cells, 12 bytes each: key, first patch (u16), patch count (u16), fade distance
///   patches, 16 bytes each: the layer (u8), 7 bytes not used (left over from memory), and a
///     64-bit mask of the 4×4×4 sub-cubes of the cell where that layer grows
///   then more, not read yet
/// A cell's key packs where it is, counted in cells from the zone's corner at height 0: x in bits
/// 22–31, height + 2048 in bits 10–21 and z in bits 0–9. A mask's bit n is the sub-cube
/// x = bits 4–5 of n, height = bits 2–3, z = bits 0–1.
/// </summary>
public sealed class ZoneFoliage
{
    public IReadOnlyList<FoliageLayer> Layers { get; }

    /// <summary>Every cell's patches: where each layer grows in it.</summary>
    public IReadOnlyList<FoliagePatch> Patches { get; }

    /// <summary>
    /// Where cells are counted from: the zone's corner (lowest x and z), at height 0. For the open
    /// world's zones (world_CC_RR) it is their square of the 255 m grid, x from (CC − 8) × 255 and
    /// z from (7 − RR) × 255; for others (the DLC's ruins) the lowest x and z of the box in the
    /// terrain file's header. The two are the same in every world zone but world_13_14, which has
    /// no ground and a small box near the world's centre instead: its foliage only lands on its
    /// surfaces (640 of 714 pieces within their sub-cube) when counted from its grid square.
    /// </summary>
    public Vector3 Corner { get; }

    readonly int patchesAt, patchCount;  // where the patch records are in the file, and how many

    ZoneFoliage(IReadOnlyList<FoliageLayer> layers, IReadOnlyList<FoliagePatch> patches, Vector3 corner, int patchesAt, int patchCount)
    {
        Layers = layers;
        Patches = patches;
        Corner = corner;
        this.patchesAt = patchesAt;
        this.patchCount = patchCount;
    }

    /// <summary>
    /// A copy of the terrain file whose foliage grows nowhere: every patch's mask cleared, so the game
    /// scatters no pieces (the layers, cells and patches stay where they are, so nothing in the file
    /// moves). A copy as it was if it has no foliage.
    /// </summary>
    public static byte[] WithoutPieces(byte[] data)
    {
        byte[] result = (byte[])data.Clone();
        if (Read(data, "") is { } foliage)
            for (int p = 0; p < foliage.patchCount; p++) Array.Clear(result, foliage.patchesAt + p * 16 + 8, 8);
        return result;
    }

    /// <summary>The zone's foliage, or null if its terrain file has none.</summary>
    /// <param name="zone">The zone's name ("world_08_02"), which gives its corner (see <see cref="Corner"/>).</param>
    public static ZoneFoliage? Read(byte[] data, string zone)
    {
        if (data.Length < 0x30 || Encoding.ASCII.GetString(data, 0, 4) != "TERR")
            throw new InvalidDataException("not a terrain file (no TERR)");
        int version = BitConverter.ToInt32(data, 4);
        if (version != ZoneTerrain.Version) throw new NotSupportedException($"terrain file version {version}");

        int header = Align4(Align4(Align4(0x28 + I32(data, 0x10)) + I32(data, 0x1C)) + I32(data, 0x18));
        if (header + 0xC8 > data.Length) return null;
        int layerCount = I32(data, header + 0xAC), cellCount = I32(data, header + 0xB4), patchCount = I32(data, header + 0xBC);
        if (layerCount == 0) return null;
        Vector3 corner = GridCorner(zone) ?? new Vector3(F32(data, header + 0x04), 0, F32(data, header + 0x0C));

        // The first name block starts with the first ".fmeshx" name; the layers and models are right
        // before it. Their start is where the layers' model counts add up to the models in between.
        int names = FirstName(data, header + 0xC8);
        if (names < 0) throw new InvalidDataException("foliage layers but no foliage mesh names");
        int layersAt = -1;
        for (int models = 1; models <= 1024 && layersAt < 0; models++)
        {
            int at = names - models * 16 - layerCount * 32;
            if (at < header) break;
            bool layersFit = Enumerable.Range(0, layerCount).All(l => IsLayer(data, at + l * 32));
            if (layersFit && Enumerable.Range(0, layerCount).Sum(l => I32(data, at + l * 32)) == models) layersAt = at;
        }
        if (layersAt < 0) throw new InvalidDataException("the foliage layers before the mesh names don't add up");

        // Each layer has its own name block, one after another.
        var layers = new List<FoliageLayer>();
        int modelAt = layersAt + layerCount * 32;
        for (int l = 0; l < layerCount; l++)
        {
            int at = layersAt + l * 32, namesEnd = names;
            var models = new List<FoliageModel>();
            for (int m = 0; m < I32(data, at); m++, modelAt += 16)
            {
                string mesh = CString(data, names + I32(data, modelAt));
                namesEnd = Math.Max(namesEnd, names + I32(data, modelAt) + mesh.Length + 1);
                models.Add(new FoliageModel(mesh, BitConverter.ToUInt16(data, modelAt + 4), FollowsSlope: data[modelAt + 6] != 0,
                                            data[modelAt + 7] / 255f, Colour(data, modelAt + 8), Colour(data, modelAt + 12)));
            }
            layers.Add(new FoliageLayer(models, MaxDensity: F32(data, at + 8), MaxFade: F32(data, at + 0x0C), MinFade: F32(data, at + 0x10),
                                        PlacementMethod: I32(data, at + 0x14), RandomSeed: I32(data, at + 0x18), CellSize: F32(data, at + 0x1C)));
            names = Align4(namesEnd);
        }

        int cellsAt = names, patchesAt = cellsAt + cellCount * 12;
        if (patchesAt + patchCount * 16 > data.Length) throw new InvalidDataException("the foliage cells run past the end of the file");
        var patches = new List<FoliagePatch>(patchCount);
        for (int c = 0; c < cellCount; c++)
        {
            int at = cellsAt + c * 12;
            uint key = BitConverter.ToUInt32(data, at);
            int first = BitConverter.ToUInt16(data, at + 4), count = BitConverter.ToUInt16(data, at + 6);
            if (key == uint.MaxValue) continue;
            if (first + count > patchCount) throw new InvalidDataException($"foliage cell {c} names patches past the last");
            int x = (int)(key >> 22), y = (int)((key >> 10) & 0xFFF) - 2048, z = (int)(key & 0x3FF);
            for (int p = first; p < first + count; p++)
            {
                int layer = data[patchesAt + p * 16];
                if (layer >= layerCount) throw new InvalidDataException($"foliage patch {p} is for layer {layer} of {layerCount}");
                patches.Add(new FoliagePatch(x, y, z, layer, BitConverter.ToUInt64(data, patchesAt + p * 16 + 8)));
            }
        }
        return new ZoneFoliage(layers, patches, corner, patchesAt, patchCount);
    }

    /// <summary>
    /// Every piece of foliage, scattered as the game does it (read from its code, at 0x6A75F0 and
    /// 0x6A78C0 in the exe), patch by patch:
    ///   how many: the share of the cell's 64 sub-cubes marked × the layer's max density × 4,
    ///   rounded to the nearest whole number
    ///   the C library's random numbers (rand), seeded with the cell's x, height and z and the
    ///   layer's seed (the low byte of each, in that order from the lowest byte), then for each piece:
    ///     a marked sub-cube: the (rand % marked)-th marked one, counting from bit 0
    ///     a model, by weight: rand % total weight, less each model's weight until below 0
    ///     x and z within the sub-cube: rand % 1000 / 1000 of its size each, at the top of it
    ///     turned around the up axis by rand % 360 degrees
    ///     scaled by 1 − variance × (rand % 1000 / 500 − 1)
    ///     tinted between the model's two colours by rand % 1000 / 1000
    ///     and a sway phase for the wind, rand % 1000 / 1000 × 7 (not used here)
    /// The game then looks straight down from each piece, through its sub-cube (and a millimetre
    /// either side), for a surface: it moves the piece onto the first one it finds, tilted to the
    /// slope there if its model says so, keeping its turn; pieces that find none are left out
    /// (0x6A7430). That needs the level's surfaces, so it is left to whoever draws them. The game
    /// also stops scattering a cell if it would make too many pieces (not done here).
    /// </summary>
    public IEnumerable<FoliageInstance> Scatter()
    {
        foreach (FoliagePatch patch in Patches)
        {
            FoliageLayer layer = Layers[patch.Layer];
            int marked = BitOperations.PopCount(patch.Mask);
            if (marked == 0 || layer.PlacementMethod != 1) continue;  // placement method 0 (a grid) isn't used by any zone
            float share = (float)(marked / 64.0);
            int count = RoundToNearest((float)(share * (double)layer.MaxDensity * 4.0));
            float subSize = layer.CellSize / 4;
            Vector3 cell = Corner + new Vector3(patch.X, patch.Y, patch.Z) * layer.CellSize;
            int totalWeight = layer.Models.Sum(m => m.Weight);

            var random = new CRandom((uint)((patch.X & 0xFF) | (patch.Y & 0xFF) << 8 | (patch.Z & 0xFF) << 16 | (layer.RandomSeed & 0xFF) << 24));
            for (int i = 0; i < count; i++)
            {
                int bit = NthMarked(patch.Mask, random.Next() % marked);
                Vector3 subCube = cell + new Vector3((bit >> 4) & 3, (bit >> 2) & 3, bit & 3) * subSize;
                int model = PickModel(layer, totalWeight, random);
                float x = (float)(random.Next() % 1000 / 1000.0 * subSize);
                float z = (float)(random.Next() % 1000 / 1000.0 * subSize);
                float yaw = (float)(random.Next() % 360 / 180.0 * Math.PI);
                float variation = (float)(random.Next() % 1000 / 500.0 - 1.0);
                float tint = (float)(random.Next() % 1000 / 1000.0);
                random.Next();  // the wind's sway phase

                FoliageModel? used = model < layer.Models.Count ? layer.Models[model] : null;
                yield return new FoliageInstance(
                    Layer: patch.Layer,
                    Model: used,
                    Position: subCube + new Vector3(x, subSize, z),
                    Drop: subSize,
                    Yaw: yaw,
                    Scale: used == null ? 1 : 1 - used.ScaleVariance * variation,
                    Tint: used == null ? Vector4.One : Vector4.Lerp(used.ColourA, used.ColourB, tint));
            }
        }
    }

    /// <summary>The corner of an open-world zone's 255 m square, from its name (world_CC_RR), or null for other zones.</summary>
    public static Vector3? GridCorner(string zone) =>
        Regex.Match(zone, @"^world_(\d\d)_(\d\d)$") is { Success: true } m
            ? new Vector3((int.Parse(m.Groups[1].Value) - 8) * 255, 0, (7 - int.Parse(m.Groups[2].Value)) * 255)
            : null;

    /// <summary>A model by weight (0x6A7FD0): model 0 if the weights add up to 0, without using a random number.</summary>
    static int PickModel(FoliageLayer layer, int totalWeight, CRandom random)
    {
        if (totalWeight == 0) return 0;
        int left = random.Next() % totalWeight;
        for (int m = 0; m < layer.Models.Count; m++)
        {
            left -= layer.Models[m].Weight;
            if (left < 0) return m;
        }
        return 0;
    }

    /// <summary>The bit number of the n-th set bit (from 0), counting from bit 0.</summary>
    static int NthMarked(ulong mask, int n)
    {
        for (int bit = 0; bit < 64; bit++)
            if ((mask >> bit & 1) != 0 && n-- == 0) return bit;
        return -1;
    }

    /// <summary>Rounds halves away from zero, as the game's own rounding does.</summary>
    static int RoundToNearest(float value) => (int)(value >= 0 ? value + 0.5 : value - 0.5);

    /// <summary>Whether 32 bytes look like a layer: some models, a placement method of 0 or 1, a cell size.</summary>
    static bool IsLayer(byte[] data, int at) =>
        I32(data, at) is > 0 and < 256 && I32(data, at + 0x14) is 0 or 1 && F32(data, at + 0x1C) is > 0.5f and < 64;

    /// <summary>The first ".fmeshx" name after a place, from the start of its name.</summary>
    static int FirstName(byte[] data, int from)
    {
        int at = data.AsSpan(from).IndexOf(".fmeshx\0"u8);
        if (at < 0) return -1;
        int start = from + at;
        while (start > from && data[start - 1] != 0) start--;
        return start;
    }

    static Vector4 Colour(byte[] data, int at) => new Vector4(data[at + 2], data[at + 1], data[at], data[at + 3]) / 255f;

    static int Align4(int value) => (value + 3) & ~3;
    static int I32(byte[] data, int at) => BitConverter.ToInt32(data, at);
    static float F32(byte[] data, int at) => BitConverter.ToSingle(data, at);

    static string CString(byte[] data, int at)
    {
        int end = Array.IndexOf(data, (byte)0, at);
        return Encoding.ASCII.GetString(data, at, (end < 0 ? data.Length : end) - at);
    }

    /// <summary>The C library's rand(), which the game uses: the same numbers from the same seed.</summary>
    sealed class CRandom(uint seed)
    {
        uint state = seed;

        public int Next()
        {
            state = state * 214013 + 2531011;
            return (int)(state >> 16 & 0x7FFF);
        }
    }
}

/// <summary>A kind of foliage and how densely it grows.</summary>
/// <param name="MaxDensity">Pieces per 16 sub-cubes (a quarter of a cell) where every sub-cube is marked.</param>
/// <param name="MaxFade">How far away it fades out completely (metres).</param>
/// <param name="MinFade">How far away it starts to fade.</param>
/// <param name="PlacementMethod">1: scattered over the marked sub-cubes (every zone); 0: a grid (unused).</param>
/// <param name="CellSize">The size of a cell (4 m in every zone); its sub-cubes are a quarter of it.</param>
public sealed record FoliageLayer(IReadOnlyList<FoliageModel> Models, float MaxDensity, float MaxFade, float MinFade,
                                  int PlacementMethod, int RandomSeed, float CellSize);

/// <param name="Mesh">The mesh's name, e.g. "f_cm_trash_a.fmeshx" (the file is f_cm_trash_a.cfmesh_pc).</param>
/// <param name="Weight">How often it is picked, against the layer's other models.</param>
/// <param name="FollowsSlope">Whether its pieces tilt to the slope of what they land on.</param>
/// <param name="ScaleVariance">How much bigger or smaller a piece can be, from 0 to 1.</param>
/// <param name="ColourA">One end of its tint (red, green, blue, alpha from 0 to 1).</param>
/// <param name="ColourB">The other end.</param>
public sealed record FoliageModel(string Mesh, int Weight, bool FollowsSlope, float ScaleVariance, Vector4 ColourA, Vector4 ColourB)
{
    /// <summary>The mesh's file: f_cm_trash_a.fmeshx is f_cm_trash_a.cfmesh_pc.</summary>
    public string FileName => Path.ChangeExtension(Mesh, ".cfmesh_pc");
}

/// <summary>Where one layer grows in one cell: the cell (counted from the zone's corner) and its marked sub-cubes.</summary>
public readonly record struct FoliagePatch(int X, int Y, int Z, int Layer, ulong Mask);

/// <summary>One piece of foliage.</summary>
/// <param name="Model">Its model, or null if the layer has none.</param>
/// <param name="Position">Where the game looks down for a surface from: the top of its sub-cube.</param>
/// <param name="Drop">How far down it looks (the sub-cube's height).</param>
/// <param name="Yaw">Turn around the up axis, in radians.</param>
/// <param name="Tint">Multiplies its colour (red, green, blue, alpha).</param>
public readonly record struct FoliageInstance(int Layer, FoliageModel? Model, Vector3 Position, float Drop, float Yaw, float Scale, Vector4 Tint);
