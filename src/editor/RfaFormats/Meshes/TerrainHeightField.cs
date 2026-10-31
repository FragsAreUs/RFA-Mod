namespace RfaFormats.Meshes;

/// <summary>
/// The ground's collision in a zone's terrain file (.cterrain_pc): a Havok 2010 compressed height
/// field, which the player walks on and things land on. world_08_02.cterrain_pc holds it in its
/// first Havok packfile, as an hkpListShape around an hkpTriSampledHeightFieldBvTreeShape around
/// an hkpTriSampledHeightFieldCollection around the hkpCompressedSampledHeightFieldShape:
///
///   +0x00  the size: x and z samples (512 × 512: one per terrain grid step, so 0 to 255 m from
///          the zone's corner)
///   +0x08  the height centre (float), then whether heights are projected (u8) and the type (u8)
///   +0x10  int-to-float scale (step, 1, step, 0), float-to-int scale (1/step, 1, 1/step, 0), the
///          float-to-int offset (floor-corrected), and the extents (x, height range, z, 0)
///   +0x50  the samples' array (pointer, count, capacity), whether triangles flip (u8)
///   +0x60  offset and scale: a sample's height is its u16 × scale + offset (-1000 and 1/32 in
///          world_08_02, which reach from −1000 to 1048 m)
///   from the next multiple of 16: the samples, u16 each
///
/// The list shape before it keeps a box around it all (half extents 0.01 larger than the field's,
/// with 0.01 after them, then the centre with 1), which must hold the heights too. Positions are
/// from the zone's corner.
/// </summary>
public sealed class TerrainHeightField
{
    readonly int at;        // where the size is (+0x00 above) in the file
    readonly int samplesAt;
    readonly int boxAt;     // the list shape's half extents, then its centre

    public int XSamples { get; }
    public int ZSamples { get; }
    public float Step { get; }
    public float Offset { get; }
    public float Scale { get; }

    TerrainHeightField(byte[] file, int at, int boxAt)
    {
        this.at = at;
        this.boxAt = boxAt;
        XSamples = BitConverter.ToInt32(file, at);
        ZSamples = BitConverter.ToInt32(file, at + 4);
        Step = BitConverter.ToSingle(file, at + 0x10);
        Offset = BitConverter.ToSingle(file, at + 0x60);
        Scale = BitConverter.ToSingle(file, at + 0x64);
        samplesAt = (at + 0x68 + 15) & ~15;
    }

    /// <summary>The terrain file's height field, or null if it has none (zones without ground).</summary>
    public static TerrainHeightField? Find(byte[] file)
    {
        for (int a = 0; a + 0x70 <= file.Length; a += 4)
        {
            int x = BitConverter.ToInt32(file, a), z = BitConverter.ToInt32(file, a + 4);
            if (x != z || x < 16 || x > 4096) continue;
            float step = BitConverter.ToSingle(file, a + 0x10);
            if (step <= 0 || step > 4 || BitConverter.ToSingle(file, a + 0x14) != 1 || BitConverter.ToSingle(file, a + 0x18) != step) continue;
            if (BitConverter.ToInt32(file, a + 0x54) != x * z || ((a + 0x68 + 15) & ~15) + x * z * 2 > file.Length) continue;
            // The list shape's box: half extents (half the field's, and 0.01 more), then the centre (half the field's).
            float half = BitConverter.ToSingle(file, a + 0x40) / 2;
            static bool Near(float a, float b) => MathF.Abs(a - b) < 0.001f;
            for (int b = a - 16; b >= Math.Max(0, a - 0x200); b -= 4)
                if (Near(BitConverter.ToSingle(file, b), half + 0.01f) && Near(BitConverter.ToSingle(file, b + 8), half + 0.01f) &&
                    BitConverter.ToSingle(file, b + 16) == half && BitConverter.ToSingle(file, b + 24) == half)
                    return new TerrainHeightField(file, a, b);
        }
        return null;
    }

    /// <summary>
    /// The heights, row by row along z: sample (x, z) is at <c>[z * XSamples + x]</c>, at (x, z) ×
    /// <see cref="Step"/> from the zone's corner (in world_08_02 they lie on its ground mesh, 1.2 cm
    /// apart on average; any other order is metres off).
    /// </summary>
    public float[] Heights(byte[] file)
    {
        var heights = new float[XSamples * ZSamples];
        for (int i = 0; i < heights.Length; i++) heights[i] = BitConverter.ToUInt16(file, samplesAt + i * 2) * Scale + Offset;
        return heights;
    }

    /// <summary>
    /// A copy of the terrain file with new heights (laid out as <see cref="Heights"/>), and the
    /// height field's centre and range and the list shape's box made to hold them.
    /// </summary>
    public byte[] WithHeights(byte[] file, float[] heights)
    {
        if (heights.Length != XSamples * ZSamples) throw new ArgumentException($"{heights.Length} heights for a {XSamples} × {ZSamples} field");
        byte[] result = (byte[])file.Clone();
        float low = float.MaxValue, high = float.MinValue;
        for (int i = 0; i < heights.Length; i++)
        {
            int sample = (int)MathF.Round((heights[i] - Offset) / Scale);
            if (sample is < 0 or > ushort.MaxValue) throw new ArgumentException($"a height of {heights[i]} m is out of the field's reach");
            BitConverter.TryWriteBytes(result.AsSpan(samplesAt + i * 2), (ushort)sample);
            float stored = sample * Scale + Offset;
            low = MathF.Min(low, stored);
            high = MathF.Max(high, stored);
        }
        float centre = (low + high) / 2, range = high - low;
        BitConverter.TryWriteBytes(result.AsSpan(at + 0x08), centre);
        BitConverter.TryWriteBytes(result.AsSpan(at + 0x44), range);
        BitConverter.TryWriteBytes(result.AsSpan(boxAt + 4), range / 2 + 0.01f);
        BitConverter.TryWriteBytes(result.AsSpan(boxAt + 20), centre);
        return result;
    }
}
