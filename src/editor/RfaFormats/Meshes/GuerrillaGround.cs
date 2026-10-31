using System.Numerics;

namespace RfaFormats.Meshes;

/// <summary>
/// Red Faction: Guerrilla's ground in place of an Armageddon zone's: its terrain sub-zones (what is
/// drawn) and the height field in its terrain file (what is walked on). Guerrilla's zones are 511 m
/// squares sampled every 1 m, Armageddon's 255 m squares sampled every 255/511 m, so one of
/// Guerrilla's steps becomes two of Armageddon's (the land 0.2% smaller) and a 256 m square of
/// Guerrilla's world fills an Armageddon zone.
///
/// The ground is sampled from Guerrilla's sub-zones onto its 1 m grid (the top surface: their
/// skirts hang below the edges), and made again as a regular grid of those samples: each of the
/// zone's nine sub-zones takes its third of the square both ways (neighbours share their edge
/// samples, so there are no cracks), cut into 5 × 5 patches, each patch a strip row by row. The
/// height field takes the same samples, halfway ones in between.
/// </summary>
public static class GuerrillaGround
{
    /// <summary>Samples across the square: 257 at 1 m (0 to 256 m), the last of them past the zone's 511th step.</summary>
    public const int Samples = 257;

    const int PatchesAcross = 5;

    /// <summary>
    /// Guerrilla's ground over a 256 m square from <paramref name="corner"/> (x, z in its world),
    /// from its sub-zones there: the top surface's height every 1 m, <c>[z * Samples + x]</c>.
    /// Points no sub-zone covers take the nearest covered one's height. Null if none is covered.
    /// </summary>
    public static float[]? Sample(IEnumerable<TerrainSubzone> subzones, Vector2 corner)
    {
        var heights = new float[Samples * Samples];
        Array.Fill(heights, float.NaN);
        foreach (TerrainSubzone s in subzones.Where(s => s.HasGround))
            for (int t = 0; t < s.Triangles.Length; t += 3)
            {
                Vector3 a = s.Positions[s.Triangles[t]], b = s.Positions[s.Triangles[t + 1]], c = s.Positions[s.Triangles[t + 2]];
                float area = (b.X - a.X) * (c.Z - a.Z) - (c.X - a.X) * (b.Z - a.Z);
                if (MathF.Abs(area) < 1e-9f) continue;
                int x1 = Math.Max(0, (int)MathF.Floor(MathF.Min(a.X, MathF.Min(b.X, c.X)) - corner.X)), x2 = Math.Min(Samples - 1, (int)MathF.Ceiling(MathF.Max(a.X, MathF.Max(b.X, c.X)) - corner.X));
                int z1 = Math.Max(0, (int)MathF.Floor(MathF.Min(a.Z, MathF.Min(b.Z, c.Z)) - corner.Y)), z2 = Math.Min(Samples - 1, (int)MathF.Ceiling(MathF.Max(a.Z, MathF.Max(b.Z, c.Z)) - corner.Y));
                for (int z = z1; z <= z2; z++)
                    for (int x = x1; x <= x2; x++)
                    {
                        float px = corner.X + x, pz = corner.Y + z;
                        float wa = ((b.X - px) * (c.Z - pz) - (c.X - px) * (b.Z - pz)) / area;
                        float wb = ((c.X - px) * (a.Z - pz) - (a.X - px) * (c.Z - pz)) / area;
                        float wc = 1 - wa - wb;
                        if (wa < -1e-4f || wb < -1e-4f || wc < -1e-4f) continue;
                        float h = wa * a.Y + wb * b.Y + wc * c.Y;
                        int i = z * Samples + x;
                        if (float.IsNaN(heights[i]) || h > heights[i]) heights[i] = h;
                    }
            }
        if (heights.All(float.IsNaN)) return null;
        // Holes: the nearest covered sample's height, spreading out a step at a time.
        while (heights.Any(float.IsNaN))
        {
            float[] before = (float[])heights.Clone();
            for (int z = 0; z < Samples; z++)
                for (int x = 0; x < Samples; x++)
                {
                    if (!float.IsNaN(before[z * Samples + x])) continue;
                    foreach (var (dx, dz) in new[] { (1, 0), (-1, 0), (0, 1), (0, -1) })
                        if (x + dx is >= 0 and < Samples && z + dz is >= 0 and < Samples && !float.IsNaN(before[(z + dz) * Samples + x + dx]))
                        {
                            heights[z * Samples + x] = before[(z + dz) * Samples + x + dx];
                            break;
                        }
                }
        }
        return heights;
    }

    /// <summary>
    /// The height at a point of the square (in Guerrilla's metres from its corner), on the
    /// triangles the sub-zones make: each square of samples is cut from its corner at (x, z + 1)
    /// to the one at (x + 1, z), so the height field lies exactly on the drawn ground.
    /// </summary>
    public static float HeightAt(float[] heights, float x, float z)
    {
        x = Math.Clamp(x, 0, Samples - 1);
        z = Math.Clamp(z, 0, Samples - 1);
        int x0 = Math.Min((int)x, Samples - 2), z0 = Math.Min((int)z, Samples - 2);
        float fx = x - x0, fz = z - z0;
        float h00 = heights[z0 * Samples + x0], h10 = heights[z0 * Samples + x0 + 1], h01 = heights[(z0 + 1) * Samples + x0], h11 = heights[(z0 + 1) * Samples + x0 + 1];
        return fx + fz <= 1
            ? h00 + fx * (h10 - h00) + fz * (h01 - h00)
            : h11 + (1 - fx) * (h01 - h11) + (1 - fz) * (h10 - h11);
    }

    /// <summary>
    /// The zone's nine sub-zones made from the samples (see <see cref="Sample"/>), each written over
    /// the zone's own (<paramref name="templates"/>, by number, which must have ground; see
    /// <see cref="TerrainSubzone.Write"/>), for the zone whose 255 m square starts at
    /// <paramref name="zoneCorner"/> (x, z in Armageddon's world).
    /// </summary>
    public static IReadOnlyList<(byte[] Header, byte[] Data)> Subzones(float[] heights, Vector2 zoneCorner, IReadOnlyList<(byte[] Header, byte[] Data)> templates)
    {
        if (templates.Count != 9) throw new ArgumentException("a zone has nine sub-zones");
        float step = TerrainSubzone.GridStep;
        var written = new List<(byte[], byte[])>();
        for (int n = 0; n < 9; n++)
        {
            // This third of the square both ways, in samples (85 or 86 squares across), sharing its edge samples with its neighbours.
            int x0 = n % 3 * (Samples - 1) / 3, x1 = (n % 3 + 1) * (Samples - 1) / 3;
            int z0 = n / 3 * (Samples - 1) / 3, z1 = (n / 3 + 1) * (Samples - 1) / 3;
            int width = x1 - x0 + 1;
            var vertices = new List<GroundVertex>();
            for (int z = z0; z <= z1; z++)
                for (int x = x0; x <= x1; x++)
                {
                    // The slope from the neighbouring samples, 2 Armageddon steps apart.
                    float left = heights[z * Samples + Math.Max(0, x - 1)], right = heights[z * Samples + Math.Min(Samples - 1, x + 1)];
                    float down = heights[Math.Max(0, z - 1) * Samples + x], up = heights[Math.Min(Samples - 1, z + 1) * Samples + x];
                    float spanX = (Math.Min(Samples - 1, x + 1) - Math.Max(0, x - 1)) * 2 * step, spanZ = (Math.Min(Samples - 1, z + 1) - Math.Max(0, z - 1)) * 2 * step;
                    var normal = Vector3.Normalize(new Vector3(-(right - left) / spanX, 1, -(up - down) / spanZ));
                    vertices.Add(new GroundVertex((x - x0) * 2, (z - z0) * 2, heights[z * Samples + x], normal));
                }
            // 5 × 5 patches, each a strip along x, a row of squares at a time (rows joined by repeated indices).
            var patches = new List<int[]>();
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
                    patches.Add([.. strip]);
                }
            var origin = new Vector2(zoneCorner.X + x0 * 2 * step, zoneCorner.Y + z0 * 2 * step);
            written.Add(TerrainSubzone.Write(templates[n].Header, templates[n].Data, origin, vertices, patches));
        }
        return written;
    }

    /// <summary>
    /// The zone's terrain file with its height field made from the samples (one per Armageddon
    /// step: Guerrilla's at the even ones, halfway between them at the odd ones), and the zone's
    /// box in its header widened to hold them.
    /// </summary>
    public static byte[] TerrainFile(byte[] terrainFile, float[] heights)
    {
        var field = TerrainHeightField.Find(terrainFile) ?? throw new InvalidDataException("the terrain file has no height field");
        var fieldHeights = new float[field.XSamples * field.ZSamples];
        for (int z = 0; z < field.ZSamples; z++)
            for (int x = 0; x < field.XSamples; x++)
                fieldHeights[z * field.XSamples + x] = HeightAt(heights, x / 2f, z / 2f);
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
