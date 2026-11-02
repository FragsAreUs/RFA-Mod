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
    public float InverseStep { get; }
    public float ExtentX { get; }
    public float ExtentZ { get; }

    TerrainHeightField(byte[] file, int at, int boxAt)
    {
        this.at = at;
        this.boxAt = boxAt;
        XSamples = BitConverter.ToInt32(file, at);
        ZSamples = BitConverter.ToInt32(file, at + 4);
        Step = BitConverter.ToSingle(file, at + 0x10);
        Offset = BitConverter.ToSingle(file, at + 0x60);
        Scale = BitConverter.ToSingle(file, at + 0x64);
        InverseStep = BitConverter.ToSingle(file, at + 0x20);
        ExtentX = BitConverter.ToSingle(file, at + 0x40);
        ExtentZ = BitConverter.ToSingle(file, at + 0x48);
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

    /// <summary>The samples as stored (u16 each, laid out as <see cref="Heights"/>).</summary>
    public ushort[] Samples(byte[] file) => Enumerable.Range(0, XSamples * ZSamples).Select(i => BitConverter.ToUInt16(file, samplesAt + i * 2)).ToArray();

    /// <summary>
    /// The height field's "MCKH" collision section (version 5, its length, one model) written from
    /// nothing: a Havok 2010 packfile (hk_2010.2.0-r1; sections __classnames__ from 0xD0, __types__
    /// empty, __data__ from 0x1B0) of four objects, as the game's terrain files have them:
    ///   +0x000 hkpListShape (0x70): its one child's entry at +0x70; the box round it, half extents
    ///          ((x samples − 1) × step / 2 and the height range / 2, each 0.01 more; and the radius,
    ///          0x3C23D700) then the centre (with 1); every child enabled
    ///   +0x080 hkpTriSampledHeightFieldBvTreeShape (0x30): the tree type 1, its child, 1 (the
    ///          box test wanted), and 0x80000000, 0xFF (as the game's)
    ///   +0x0B0 hkpTriSampledHeightFieldCollection (0x40): collection type 2, the field, the radius 0.01
    ///   +0x0F0 hkpCompressedSampledHeightFieldShape (0x80): the samples across, the height centre,
    ///          type 1; int-to-float scale (step, 1, step, 0); float-to-int scale (its inverse, 1, the
    ///          same, 0); the floor-corrected offset (−step/128, 0, −step/128, −0); the extents (x, the
    ///          height range, z, 0); the samples' array; the triangles flipped (1); the samples' offset
    ///          and scale
    ///   +0x170 the samples (u16 each)
    /// then the pointer tables (local: the list's children, the samples; global: each object's child;
    /// virtual: the four objects' classes), each padded with 0xFF to 16. The heights' range and centre
    /// are those of the stored samples. The step, its inverse and the extents are given as stored: the
    /// game's zones have 255 m extents over 511 steps, a step of 0x3EFF7FC0 and an inverse of 511/255,
    /// not one worked out from the other (ours: 511 m, 1 and 1).
    /// </summary>
    public static byte[] Section(int xSamples, int zSamples, float step, float inverseStep, float extentX, float extentZ,
                                 float offset, float scale, IReadOnlyList<ushort> samples)
    {
        if (samples.Count != xSamples * zSamples) throw new ArgumentException($"{samples.Count} samples for a {xSamples} × {zSamples} field");
        int low = samples.Min(s => (int)s), high = samples.Max(s => (int)s);
        float lowHeight = low * scale + offset, highHeight = high * scale + offset;
        float centre = (lowHeight + highHeight) / 2, range = highHeight - lowHeight;

        // Class names: each its signature, a tab, the name; then 0xFF to 16.
        (uint Signature, string Name)[] classes =
        [
            (0x75585EF6, "hkClass"), (0x5C7EA4C2, "hkClassMember"), (0x8A3609CF, "hkClassEnum"), (0xCE6F8A6C, "hkClassEnumItem"),
            (0xA1937CBD, "hkpListShape"), (0x58E1E585, "hkpTriSampledHeightFieldBvTreeShape"),
            (0xC291DDDE, "hkpTriSampledHeightFieldCollection"), (0x97B6E143, "hkpCompressedSampledHeightFieldShape"),
        ];
        var names = new List<byte>();
        var nameAt = new Dictionary<string, int>();
        foreach (var (signature, name) in classes)
        {
            names.AddRange(BitConverter.GetBytes(signature));
            names.Add(0x09);
            nameAt[name] = names.Count;
            names.AddRange(System.Text.Encoding.ASCII.GetBytes(name));
            names.Add(0);
        }
        while (names.Count % 16 != 0) names.Add(0xFF);

        // __data__.
        var d = new byte[0x170 + samples.Count * 2];
        void I(int at, uint v) => BitConverter.TryWriteBytes(d.AsSpan(at), v);
        void F(int at, float v) => BitConverter.TryWriteBytes(d.AsSpan(at), v);
        I(0x1C, 1); I(0x20, 0x80000001);                                   // the list's one child
        F(0x30, extentX / 2 + 0.01f); F(0x34, range / 2 + 0.01f); F(0x38, extentZ / 2 + 0.01f); I(0x3C, 0x3C23D700);
        F(0x40, extentX / 2); F(0x44, centre); F(0x48, extentZ / 2); F(0x4C, 1);
        for (int b = 0x50; b < 0x70; b++) d[b] = 0xFF;                     // every child enabled
        d[0x90] = 1; d[0xA0] = 1; I(0xA8, 0x80000000); I(0xAC, 0xFF);      // the tree
        d[0xC5] = 2; F(0xD0, 0.01f); I(0xDC, 0x80000000);                  // the collection
        I(0x100, (uint)xSamples); I(0x104, (uint)zSamples); F(0x108, centre); d[0x10D] = 1;
        F(0x110, step); F(0x114, 1); F(0x118, step);
        F(0x120, inverseStep); F(0x124, 1); F(0x128, inverseStep);
        F(0x130, -step / 128); F(0x138, -step / 128); I(0x13C, 0x80000000);
        F(0x140, extentX); F(0x144, range); F(0x148, extentZ);
        I(0x154, (uint)samples.Count); I(0x158, 0x80000000u | (uint)samples.Count); d[0x15C] = 1;
        F(0x160, offset); F(0x164, scale);
        for (int i = 0; i < samples.Count; i++) BitConverter.TryWriteBytes(d.AsSpan(0x170 + i * 2), samples[i]);
        var data = new List<byte>(d);
        void Pad() { while (data.Count % 16 != 0) data.Add(0xFF); }
        void Words(params uint[] words) { foreach (uint w in words) data.AddRange(BitConverter.GetBytes(w)); }
        int local = data.Count;
        Words(0x18, 0x70, 0x150, 0x170); Pad();
        int global = data.Count;
        Words(0x70, 2, 0x80, 0x98, 2, 0xB0, 0xC8, 2, 0xF0); Pad();
        int virtualAt = data.Count;
        Words(0x00, 0, (uint)nameAt["hkpListShape"], 0x80, 0, (uint)nameAt["hkpTriSampledHeightFieldBvTreeShape"],
              0xB0, 0, (uint)nameAt["hkpTriSampledHeightFieldCollection"], 0xF0, 0, (uint)nameAt["hkpCompressedSampledHeightFieldShape"]);
        Pad();
        int end = data.Count;

        // The packfile: header, three section headers, class names, data; inside its "MCKH" section.
        const int namesAt = 0xD0;
        int dataAt = namesAt + names.Count;
        var pack = new List<byte>();
        void P(uint v) => pack.AddRange(BitConverter.GetBytes(v));
        P(0x57E0E057); P(0x10C0C010); P(0); P(8);
        pack.AddRange([4, 1, 0, 1]);
        P(3); P(2); P(0); P(0); P((uint)nameAt["hkpListShape"]);
        byte[] version = new byte[16];
        System.Text.Encoding.ASCII.GetBytes("hk_2010.2.0-r1").CopyTo(version, 0);
        version[15] = 0xFF;
        pack.AddRange(version);
        P(0); P(uint.MaxValue);
        void Section(string name, int start, params int[] offsets)
        {
            byte[] n = new byte[20];
            System.Text.Encoding.ASCII.GetBytes(name).CopyTo(n, 0);
            n[19] = 0xFF;
            pack.AddRange(n);
            P((uint)start);
            foreach (int o in offsets) P((uint)o);
        }
        Section("__classnames__", namesAt, names.Count, names.Count, names.Count, names.Count, names.Count, names.Count);
        Section("__types__", dataAt, 0, 0, 0, 0, 0, 0);
        Section("__data__", dataAt, local, global, virtualAt, end, end, end);
        pack.AddRange(names);
        pack.AddRange(data);
        return [.. "MCKH"u8, .. BitConverter.GetBytes(5), .. BitConverter.GetBytes(16 + pack.Count), .. BitConverter.GetBytes(1), .. pack];
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

    /// <summary>
    /// A copy of the terrain file whose height field has samples <paramref name="step"/> metres
    /// apart (its samples kept): the int-to-float scale (step, 1, step), the float-to-int scale
    /// (1 / step), the floor-corrected float-to-int offset (−step / 128: Armageddon's −0.0039 at
    /// 255/511 m, Guerrilla's −1/128 at its 1 m), the extents ((samples − 1) × step) and the list
    /// shape's box around them. Guerrilla's zones (511 m, 512 samples) have step 1.
    /// </summary>
    public byte[] WithStep(byte[] file, float step)
    {
        if (step <= 0) throw new ArgumentOutOfRangeException(nameof(step));
        byte[] result = (byte[])file.Clone();
        void Put(int offset, float value) => BitConverter.TryWriteBytes(result.AsSpan(offset), value);
        float extentX = (XSamples - 1) * step, extentZ = (ZSamples - 1) * step;
        Put(at + 0x10, step); Put(at + 0x18, step);
        Put(at + 0x20, 1 / step); Put(at + 0x28, 1 / step);
        Put(at + 0x30, -step / 128); Put(at + 0x38, -step / 128);
        Put(at + 0x40, extentX); Put(at + 0x48, extentZ);
        Put(boxAt, extentX / 2 + 0.01f); Put(boxAt + 8, extentZ / 2 + 0.01f);
        Put(boxAt + 16, extentX / 2); Put(boxAt + 24, extentZ / 2);
        return result;
    }
}
