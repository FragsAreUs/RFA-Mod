using System.Numerics;

namespace RfaFormats.Meshes;

/// <summary>
/// How a zone made from Guerrilla's ground is sampled. Guerrilla's zones are 511 m squares sampled
/// every 1 m. <see cref="Quarter"/>: Armageddon's own zones, 255 m squares sampled every 255/511 m,
/// so one of Guerrilla's steps becomes two of Armageddon's (the land 0.2% smaller) and a 255.5 m
/// square of Guerrilla's world fills a zone: 257 samples, every 1 m from 0 to 255 m, then the far edge
/// at 255.5 m (the zone's 511th step), so that zones made from neighbouring squares share their
/// edge samples. <see cref="Whole"/>: a whole Guerrilla zone, for a world the game gives 511 m zones
/// (as it does those named mpl_*): 512 samples 1 m apart, one grid step each, as Guerrilla's own.
/// </summary>
public sealed class GroundLayout
{
    /// <summary>Samples across the square.</summary>
    public int Samples { get; }

    /// <summary>The zone's grid steps from one sample to the next.</summary>
    public int StepsPerSample { get; }

    /// <summary>A grid step in the game's metres.</summary>
    public float GridStep { get; }

    readonly bool halfLast;  // the last sample half a step after the one before

    GroundLayout(int samples, int stepsPerSample, float gridStep, bool halfLast)
    {
        Samples = samples;
        StepsPerSample = stepsPerSample;
        GridStep = gridStep;
        this.halfLast = halfLast;
    }

    public static readonly GroundLayout Quarter = new(257, 2, TerrainSubzone.GridStep, true);
    public static readonly GroundLayout Whole = new(512, 1, TerrainSubzone.GuerrillaGridStep, false);

    /// <summary>Where sample <paramref name="n"/> is, in Guerrilla's metres from the square's corner.</summary>
    public float SampleAt(int n) => halfLast ? Math.Min(n, Samples - 1.5f) : n;

    /// <summary>Which of the zone's grid steps sample <paramref name="n"/> is on.</summary>
    public int StepOf(int n) => halfLast ? Math.Min(StepsPerSample * n, StepsPerSample * Samples - 3) : StepsPerSample * n;
}

/// <summary>
/// Red Faction: Guerrilla's ground in place of an Armageddon zone's: its terrain sub-zones (what is
/// drawn) and the height field in its terrain file (what is walked on), sampled as a
/// <see cref="GroundLayout"/> says (<see cref="GroundLayout.Quarter"/> where none is given).
///
/// The ground is sampled from Guerrilla's sub-zones every 1 m (the top surface: their skirts hang
/// below the edges). It is made again as a grid of those samples: each of the zone's nine sub-zones
/// takes its third of the square both ways (neighbours share their edge samples, so there are no
/// cracks), cut into 5 × 5 patches, each patch a strip row by row. The height field takes the same
/// samples, the drawn ground's height in between.
/// </summary>
public static class GuerrillaGround
{
    /// <summary>Samples across a quarter-layout square (see <see cref="GroundLayout.Quarter"/>).</summary>
    public const int Samples = 257;

    /// <summary>Where sample <paramref name="n"/> of a quarter-layout square is, in Guerrilla's metres from its corner.</summary>
    public static float SampleAt(int n) => GroundLayout.Quarter.SampleAt(n);

    const int PatchesAcross = 5;

    /// <summary>
    /// Guerrilla's ground over the square from <paramref name="corner"/> (x, z in its world), from
    /// its sub-zones there: the top surface's height at each sample, <c>[z * Samples + x]</c>, its
    /// roads and paving included. Points no sub-zone covers are where Guerrilla's rocks close the
    /// ground themselves (caps on pillar tops, covers on slopes); they are filled smoothly from the
    /// covered ones around them, and then, where one of <paramref name="closers"/> (the rocks'
    /// triangles in the world, three corners each) lies just above that fill (its first surface
    /// there no more than <see cref="ClosedReach"/> up), raised to <see cref="ClosedBelow"/> under
    /// it, so the ground stays inside the rock with no gap under its rim. A rock surface higher up
    /// spans the hole (an arch, a tall spire's flank) rather than closing it, and is left alone.
    /// Null if nothing is covered.
    /// </summary>
    public static float[]? Sample(IEnumerable<TerrainSubzone> subzones, Vector2 corner, GroundLayout? layout = null, IEnumerable<Vector3>? closers = null)
    {
        layout ??= GroundLayout.Quarter;
        int samples = layout.Samples;
        var heights = Cover(subzones, corner, layout);
        if (heights.All(float.IsNaN)) return null;
        bool[] hole = heights.Select(float.IsNaN).ToArray();
        Fill(heights, hole, samples);
        if (closers == null) return heights;
        // The first rock surface at or above each filled sample's fill, if near enough.
        var under = new float[heights.Length];
        Array.Fill(under, float.NaN);
        using var corners = closers.GetEnumerator();
        while (corners.MoveNext())
        {
            var a = corners.Current;
            if (!corners.MoveNext()) break;
            var b = corners.Current;
            if (!corners.MoveNext()) break;
            Visit(a, b, corners.Current, corner, layout, (i, h) =>
            {
                if (hole[i] && h >= heights[i] - ClosedBelow && h <= heights[i] + ClosedReach && (float.IsNaN(under[i]) || h < under[i])) under[i] = h;
            });
        }
        bool closed = false;
        for (int i = 0; i < heights.Length; i++)
            if (!float.IsNaN(under[i]))
            {
                heights[i] = MathF.Max(heights[i], under[i] - ClosedBelow);
                hole[i] = false;
                closed = true;
            }
        // The rest of each hole smoothed again around what the rocks closed.
        if (closed) Smooth(heights, hole, samples);
        return heights;
    }

    /// <summary>The samples marked as holes filled: first the nearest covered sample's height, spreading out a step at a time; then smoothed.</summary>
    static void Fill(float[] heights, bool[] hole, int samples)
    {
        while (heights.Any(float.IsNaN))
        {
            float[] before = (float[])heights.Clone();
            for (int z = 0; z < samples; z++)
                for (int x = 0; x < samples; x++)
                {
                    if (!float.IsNaN(before[z * samples + x])) continue;
                    foreach (var (dx, dz) in new[] { (1, 0), (-1, 0), (0, 1), (0, -1) })
                        if (x + dx >= 0 && x + dx < samples && z + dz >= 0 && z + dz < samples && !float.IsNaN(before[(z + dz) * samples + x + dx]))
                        {
                            heights[z * samples + x] = before[(z + dz) * samples + x + dx];
                            break;
                        }
                }
        }
        Smooth(heights, hole, samples);
    }

    /// <summary>
    /// Each hole sample made the mean of its neighbours, over and over, so a hole spans its sides
    /// smoothly (across a slope) instead of stepping between them where the spreading from each
    /// side met.
    /// </summary>
    static void Smooth(float[] heights, bool[] hole, int samples)
    {
        int[] filled = Enumerable.Range(0, hole.Length).Where(i => hole[i]).ToArray();
        for (int pass = 0; pass < HoleSmoothing; pass++)
            foreach (int i in filled)
            {
                int x = i % samples, z = i / samples;
                float sum = 0;
                int count = 0;
                if (x > 0) { sum += heights[i - 1]; count++; }
                if (x < samples - 1) { sum += heights[i + 1]; count++; }
                if (z > 0) { sum += heights[i - samples]; count++; }
                if (z < samples - 1) { sum += heights[i + samples]; count++; }
                heights[i] = sum / count;
            }
    }

    /// <summary>How many times holes are smoothed (enough to settle across a hole a few dozen metres wide).</summary>
    const int HoleSmoothing = 400;

    /// <summary>How far below a rock's surface the ground goes where the rock closes a hole (m).</summary>
    public const float ClosedBelow = 0.5f;

    /// <summary>How far above a hole's smooth fill a rock's surface may be and still close the hole (m); the floating rocks' rims were 1–4.5 m up.</summary>
    public const float ClosedReach = 6f;

    /// <summary>Which samples of the square (as <see cref="Sample"/>'s) no sub-zone covers.</summary>
    public static bool[] Uncovered(IEnumerable<TerrainSubzone> subzones, Vector2 corner, GroundLayout? layout = null) =>
        Cover(subzones, corner, layout).Select(float.IsNaN).ToArray();

    /// <summary>The top surface's height at each sample (the ground, and Guerrilla's roads and paving), NaN where no sub-zone covers it.</summary>
    public static float[] Cover(IEnumerable<TerrainSubzone> subzones, Vector2 corner, GroundLayout? layout = null)
    {
        layout ??= GroundLayout.Quarter;
        int samples = layout.Samples;
        var heights = new float[samples * samples];
        Array.Fill(heights, float.NaN);
        foreach (TerrainSubzone s in subzones.Where(s => s.HasGround))
        {
            for (int t = 0; t < s.Triangles.Length; t += 3) Lay(heights, s.Positions[s.Triangles[t]], s.Positions[s.Triangles[t + 1]], s.Positions[s.Triangles[t + 2]], corner, layout);
            for (int t = 0; t + 2 < s.Paving.Length; t += 3) Lay(heights, s.Paving[t], s.Paving[t + 1], s.Paving[t + 2], corner, layout);
        }
        return heights;
    }

    /// <summary>A triangle laid on the samples it covers: each keeps the highest surface over it.</summary>
    static void Lay(float[] heights, Vector3 a, Vector3 b, Vector3 c, Vector2 corner, GroundLayout layout) =>
        Visit(a, b, c, corner, layout, (i, h) => { if (float.IsNaN(heights[i]) || h > heights[i]) heights[i] = h; });

    /// <summary>Each sample a triangle covers, with the triangle's height there.</summary>
    static void Visit(Vector3 a, Vector3 b, Vector3 c, Vector2 corner, GroundLayout layout, Action<int, float> visit)
    {
        int samples = layout.Samples;
        float area = (b.X - a.X) * (c.Z - a.Z) - (c.X - a.X) * (b.Z - a.Z);
        if (MathF.Abs(area) < 1e-9f) return;
        int x1 = Math.Max(0, (int)MathF.Floor(MathF.Min(a.X, MathF.Min(b.X, c.X)) - corner.X)), x2 = Math.Min(samples - 1, (int)MathF.Ceiling(MathF.Max(a.X, MathF.Max(b.X, c.X)) - corner.X));
        int z1 = Math.Max(0, (int)MathF.Floor(MathF.Min(a.Z, MathF.Min(b.Z, c.Z)) - corner.Y)), z2 = Math.Min(samples - 1, (int)MathF.Ceiling(MathF.Max(a.Z, MathF.Max(b.Z, c.Z)) - corner.Y));
        for (int z = z1; z <= z2; z++)
            for (int x = x1; x <= x2; x++)
            {
                float px = corner.X + layout.SampleAt(x), pz = corner.Y + layout.SampleAt(z);
                float wa = ((b.X - px) * (c.Z - pz) - (c.X - px) * (b.Z - pz)) / area;
                float wb = ((c.X - px) * (a.Z - pz) - (a.X - px) * (c.Z - pz)) / area;
                float wc = 1 - wa - wb;
                if (wa < -1e-4f || wb < -1e-4f || wc < -1e-4f) continue;
                visit(z * samples + x, wa * a.Y + wb * b.Y + wc * c.Y);
            }
    }

    /// <summary>
    /// The height at a point of the square (in Guerrilla's metres from its corner), on the
    /// triangles the sub-zones make: each square of samples is cut from its corner at (x, z + 1)
    /// to the one at (x + 1, z), so the height field lies exactly on the drawn ground.
    /// </summary>
    public static float HeightAt(float[] heights, float x, float z, GroundLayout? layout = null)
    {
        layout ??= GroundLayout.Quarter;
        int samples = layout.Samples;
        x = Math.Clamp(x, 0, layout.SampleAt(samples - 1));
        z = Math.Clamp(z, 0, layout.SampleAt(samples - 1));
        int x0 = Math.Min((int)x, samples - 2), z0 = Math.Min((int)z, samples - 2);
        // (In the quarter layout the last square is half a metre across.)
        float fx = (x - x0) / (layout.SampleAt(x0 + 1) - x0), fz = (z - z0) / (layout.SampleAt(z0 + 1) - z0);
        float h00 = heights[z0 * samples + x0], h10 = heights[z0 * samples + x0 + 1], h01 = heights[(z0 + 1) * samples + x0], h11 = heights[(z0 + 1) * samples + x0 + 1];
        return fx + fz <= 1
            ? h00 + fx * (h10 - h00) + fz * (h01 - h00)
            : h11 + (1 - fx) * (h01 - h11) + (1 - fz) * (h10 - h11);
    }

    /// <summary>
    /// The zone's nine sub-zones made from the samples (see <see cref="Sample"/>), each written over
    /// the zone's own (<paramref name="templates"/>, by number, which must have ground; see
    /// <see cref="TerrainSubzone.Write"/>), or from nothing if there are none, for the zone whose square starts at
    /// <paramref name="zoneCorner"/> (x, z in Armageddon's world).
    /// </summary>
    public static IReadOnlyList<(byte[] Header, byte[] Data)> Subzones(float[] heights, Vector2 zoneCorner, IReadOnlyList<(byte[] Header, byte[] Data)> templates) =>
        Subzones(heights, zoneCorner, templates, null);

    /// <summary>
    /// As <see cref="Subzones(float[], Vector2, IReadOnlyList{ValueTuple{byte[], byte[]}})"/>, and if
    /// <paramref name="cliff"/> is given (the blend map's cliff weight, 0–255, at each sample, laid
    /// out as the heights), each patch's cliffs, triangles steeper than <see cref="CliffSlope"/>
    /// with a mean cliff weight from <see cref="CliffFrom"/>, are drawn with textures projected from
    /// the side, as the game's own sub-zones draw theirs: cliff kind 4, or 5 from
    /// <see cref="CliffHighFrom"/>; the rest of the patch as ground seen from above. (Kind 3, the
    /// ground's first layer alone from the side, showed slopes darker than the mixed layers around
    /// them, so moderate slopes stay ground.)
    /// </summary>
    public static IReadOnlyList<(byte[] Header, byte[] Data)> Subzones(float[] heights, Vector2 zoneCorner, IReadOnlyList<(byte[] Header, byte[] Data)>? templates, byte[]? cliff,
                                                                     GroundLayout? layout = null)
    {
        if (templates != null && templates.Count != 9) throw new ArgumentException("a zone has nine sub-zones");
        layout ??= GroundLayout.Quarter;
        int samples = layout.Samples;
        float step = layout.GridStep;
        var written = new List<(byte[], byte[])>();
        for (int n = 0; n < 9; n++)
        {
            // This third of the square both ways, in samples, sharing its edge samples with its neighbours.
            int x0 = n % 3 * (samples - 1) / 3, x1 = (n % 3 + 1) * (samples - 1) / 3;
            int z0 = n / 3 * (samples - 1) / 3, z1 = (n / 3 + 1) * (samples - 1) / 3;
            int width = x1 - x0 + 1;
            var vertices = new List<GroundVertex>();
            for (int z = z0; z <= z1; z++)
                for (int x = x0; x <= x1; x++)
                {
                    // The slope from the neighbouring samples.
                    int xl = Math.Max(0, x - 1), xr = Math.Min(samples - 1, x + 1), zd = Math.Max(0, z - 1), zu = Math.Min(samples - 1, z + 1);
                    float left = heights[z * samples + xl], right = heights[z * samples + xr];
                    float down = heights[zd * samples + x], up = heights[zu * samples + x];
                    float spanX = (layout.StepOf(xr) - layout.StepOf(xl)) * step, spanZ = (layout.StepOf(zu) - layout.StepOf(zd)) * step;
                    var normal = Vector3.Normalize(new Vector3(-(right - left) / spanX, 1, -(up - down) / spanZ));
                    vertices.Add(new GroundVertex(layout.StepOf(x) - layout.StepOf(x0), layout.StepOf(z) - layout.StepOf(z0), heights[z * samples + x], normal));
                }
            // 5 × 5 patches, each a strip along x, a row of squares at a time (rows joined by repeated indices).
            var patches = new List<IReadOnlyList<TerrainSubzone.PatchBlock>>();
            int quadsX = x1 - x0, quadsZ = z1 - z0;
            for (int pz = 0; pz < PatchesAcross; pz++)
                for (int px = 0; px < PatchesAcross; px++)
                {
                    int qx0 = px * quadsX / PatchesAcross, qx1 = (px + 1) * quadsX / PatchesAcross;
                    int qz0 = pz * quadsZ / PatchesAcross, qz1 = (pz + 1) * quadsZ / PatchesAcross;
                    var strip = new List<int>();
                    for (int qz = qz0; qz < qz1; qz++)
                    {
                        if (strip.Count > 0)
                        {
                            strip.Add(strip[^1]);
                            strip.Add(qz * width + qx0);
                        }
                        // a: this row's sample, b: the next row's (higher z): triangles (a0, b0, a1), (a1, b0, b1), ... face up.
                        for (int qx = qx0; qx <= qx1; qx++)
                        {
                            strip.Add(qz * width + qx);
                            strip.Add((qz + 1) * width + qx);
                        }
                    }
                    patches.Add(cliff == null ? [new TerrainSubzone.PatchBlock(TerrainSubzone.GroundKind, [.. strip])]
                                              : CliffBlocks(vertices, width, qx0, qx1, qz0, qz1, x0, z0, cliff, layout));
                }
            var origin = new Vector2(zoneCorner.X + layout.StepOf(x0) * step, zoneCorner.Y + layout.StepOf(z0) * step);
            written.Add(templates != null ? TerrainSubzone.Write(templates[n].Header, templates[n].Data, origin, vertices, patches, step)
                                          : TerrainSubzone.Write(n, origin, vertices, patches, step));
        }
        return written;
    }

    /// <summary>A triangle can be a cliff if its normal's up part is under this (about 53° from flat).</summary>
    public const float CliffSlope = 0.6f;

    /// <summary>Mean cliff weights from which a steep triangle is a cliff (kind 4), and kind 5.</summary>
    public const int CliffFrom = 128, CliffHighFrom = 224;

    /// <summary>
    /// One patch (squares qx0–qx1, qz0–qz1 of a sub-zone whose first sample is x0, z0; its
    /// vertices <paramref name="width"/> to a row) as render blocks: the ground's triangles, then
    /// each cliff kind's, every square cut as the strips cut it (from its corner at (x, z + 1) to
    /// the one at (x + 1, z)).
    /// </summary>
    static IReadOnlyList<TerrainSubzone.PatchBlock> CliffBlocks(List<GroundVertex> vertices, int width, int qx0, int qx1, int qz0, int qz1,
                                                                 int x0, int z0, byte[] cliff, GroundLayout layout)
    {
        var kinds = new Dictionary<uint, List<int>>();
        float step = layout.GridStep;
        Vector3 At(int v) => new(vertices[v].XStep * step, vertices[v].Height, vertices[v].ZStep * step);
        int Weight(int v) => cliff[(z0 + v / width) * layout.Samples + x0 + v % width];
        void Add(int a, int b, int c)
        {
            Vector3 n = Vector3.Cross(At(b) - At(a), At(c) - At(a));
            uint kind = TerrainSubzone.GroundKind;
            if (n.Length() > 1e-6f && MathF.Abs(n.Y) / n.Length() < CliffSlope)
            {
                int mean = (Weight(a) + Weight(b) + Weight(c)) / 3;
                if (mean >= CliffFrom) kind = mean < CliffHighFrom ? TerrainSubzone.CliffKindMixed : TerrainSubzone.CliffKindHigh;
            }
            if (!kinds.TryGetValue(kind, out var list)) kinds[kind] = list = [];
            list.Add(a); list.Add(b); list.Add(c);
        }
        for (int qz = qz0; qz < qz1; qz++)
            for (int qx = qx0; qx < qx1; qx++)
            {
                int a0 = qz * width + qx, a1 = a0 + 1, b0 = a0 + width, b1 = b0 + 1;
                Add(a0, b0, a1);
                Add(a1, b0, b1);
            }
        return kinds.OrderBy(k => k.Key).Select(k => new TerrainSubzone.PatchBlock(k.Key, TerrainSubzone.Strip(k.Value))).ToList();
    }

    /// <summary>
    /// The zone's terrain file with its height field made from the samples (one per grid step: in
    /// the quarter layout Guerrilla's at the even steps and the last, the drawn ground's height
    /// between them at the odd ones), its samples a grid step apart (1 m in the whole layout, as
    /// Guerrilla's), and the zone's box in its header widened to hold the heights.
    /// </summary>
    public static byte[] TerrainFile(byte[] terrainFile, float[] heights, GroundLayout? layout = null)
    {
        layout ??= GroundLayout.Quarter;
        var field = TerrainHeightField.Find(terrainFile) ?? throw new InvalidDataException("the terrain file has no height field");
        if (MathF.Abs(field.Step - layout.GridStep) > 1e-4f)
        {
            terrainFile = field.WithStep(terrainFile, layout.GridStep);
            field = TerrainHeightField.Find(terrainFile)!;
        }
        var fieldHeights = new float[field.XSamples * field.ZSamples];
        for (int z = 0; z < field.ZSamples; z++)
            for (int x = 0; x < field.XSamples; x++)
                fieldHeights[z * field.XSamples + x] = HeightAt(heights, (float)x / layout.StepsPerSample, (float)z / layout.StepsPerSample, layout);
        byte[] result = field.WithHeights(terrainFile, fieldHeights);
        // The header block's box (after the three name blocks): from x, y, z, to x, y, z.
        static int Align4(int value) => (value + 3) & ~3;
        int header = Align4(Align4(Align4(0x28 + BitConverter.ToInt32(result, 0x10)) + BitConverter.ToInt32(result, 0x1C)) + BitConverter.ToInt32(result, 0x18));
        float low = MathF.Min(BitConverter.ToSingle(result, header + 0x08), fieldHeights.Min());
        float high = MathF.Max(BitConverter.ToSingle(result, header + 0x14), fieldHeights.Max());
        BitConverter.TryWriteBytes(result.AsSpan(header + 0x08), low);
        BitConverter.TryWriteBytes(result.AsSpan(header + 0x14), high);
        return result;
    }
}
