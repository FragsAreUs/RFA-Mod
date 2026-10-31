using System.Numerics;
using System.Text;

namespace RfaFormats.Meshes;

/// <summary>
/// The detailed ground of one ninth of a zone: a terrain sub-zone (.ctmesh_pc + .gtmesh_pc, "SUBZ"
/// version 42). A zone's sub-zones are named after it, world_08_02_0 to world_08_02_8 (number =
/// row × 3 + column, rows from the zone's lowest z), each in its own small container.
///
/// Layout of the c-file (found by reading the files; every sub-zone in the game reads this way):
///   0x00  "SUBZ", version 42, sub-zone number, a checksum
///   0x10  a count of names, the size of the name block, then the NUL-separated names (such as
///         "ter_mag_mis14_lava"; usually none), up to the next multiple of 4. From there (B):
///   B     1, sub-zone number, origin x (float), 0, origin z: where its vertex grid starts
///   B+14  the number of patches (0: the sub-zone has no ground, and no mesh), then 3 zeros
///   B+24  bounding box: x, y, z from, x, y, z to; then its centre
///   then  the patches, 96 bytes each (one per submesh): position, rotation (9 floats), number,
///         a box around the position, a bounding sphere (centre, radius), 0. Their vertices are
///         shared with their neighbours, so they aren't needed to draw the ground.
///   then  the version-9 mesh block (see <see cref="ModelFile"/>), one submesh per patch, its
///         buffers in the g-file.
///
/// Vertices are 8 bytes (format 24): the height (int16, in 1/64 m), the grid step along x (u8),
/// the grid step along z (u8, stored with 128 added, wrapping round), and the normal (4 bytes, as
/// in models). A grid step is 255/511 m: a zone is 255 m wide and sampled 512 times across.
///
/// Red Faction: Guerrilla's sub-zones ("SUBZ" version 31, terr01_07_07_4.ctmesh_pc) are read too.
/// They have no checksum, so the name block starts a word earlier (count at 0x0C, size at 0x10,
/// names from 0x14), and B lacks the leading 1; and they have the version-6 mesh
/// block: as version 9 without the detail-level count and the six floats after the vertex format
/// (submesh count at +0x10, submesh table +0x14, vertex count +0x18, vertex size and format +0x1C,
/// vertex offset +0x20, index count +0x24, index offset +0x28, index size, topology and render
/// block count +0x2C), with 44-byte submeshes (one run-time word instead of two). Vertices are
/// packed the same (format 14), but Guerrilla's zones are 511 m wide, so a grid step is 1 m.
/// </summary>
public sealed class TerrainSubzone
{
    public const int Version = 42;
    public const int GuerrillaVersion = 31;

    /// <summary>The distance between two terrain samples: 255/511 m.</summary>
    public const float GridStep = 255f / 511f;

    /// <summary>The distance between two of Guerrilla's terrain samples: its zones are 511 m wide, sampled 512 times.</summary>
    public const float GuerrillaGridStep = 1f;

    public int FormatVersion { get; }    // 42, or 31 for Guerrilla's
    public int Number { get; }
    public Vector2 Origin { get; }       // x and z where the vertex grid starts
    public int PatchCount { get; }

    /// <summary>False for a sub-zone without ground (caves, mostly): it has no triangles.</summary>
    public bool HasGround => Triangles.Length > 0;
    public Vector3 BoundsMin { get; }
    public Vector3 BoundsMax { get; }
    public Vector3[] Positions { get; }
    public Vector3[] Normals { get; }

    /// <summary>Every triangle of the ground: three vertex numbers each.</summary>
    public int[] Triangles { get; }

    /// <summary>The triangles of each patch (its submesh), in the patches' order.</summary>
    public IReadOnlyList<int[]> PatchTriangles { get; }

    TerrainSubzone(byte[] header, byte[] data)
    {
        if (header.Length < 0x60 || Encoding.ASCII.GetString(header, 0, 4) != "SUBZ")
            throw new InvalidDataException("not a terrain sub-zone (no SUBZ)");
        FormatVersion = BitConverter.ToInt32(header, 4);
        if (FormatVersion is not (Version or GuerrillaVersion)) throw new NotSupportedException($"terrain sub-zone version {FormatVersion}");
        bool guerrilla = FormatVersion == GuerrillaVersion;
        Number = BitConverter.ToInt32(header, 8);
        // After the name block (B). Guerrilla's starts a word earlier (no checksum) and B lacks the
        // leading 1, so its fields sit one word before where B + n would put them.
        int fields = guerrilla ? ((0x14 + BitConverter.ToInt32(header, 0x10) + 3) & ~3) - 4
                               : (0x18 + BitConverter.ToInt32(header, 0x14) + 3) & ~3;
        if (fields + 0x48 > header.Length || BitConverter.ToInt32(header, fields + 4) != Number)
            throw new InvalidDataException("the sub-zone's header isn't laid out as expected");
        Origin = new Vector2(F32(header, fields + 0x08), F32(header, fields + 0x10));
        PatchCount = BitConverter.ToInt32(header, fields + 0x14);
        BoundsMin = new Vector3(F32(header, fields + 0x24), F32(header, fields + 0x28), F32(header, fields + 0x2C));
        BoundsMax = new Vector3(F32(header, fields + 0x30), F32(header, fields + 0x34), F32(header, fields + 0x38));
        if (PatchCount == 0)
        {
            Positions = Normals = [];
            Triangles = [];
            PatchTriangles = [];
            return;
        }

        // The mesh block follows the patches; it is found by its version and matching checksums.
        int at = Enumerable.Range((fields + 0x48) / 4, (header.Length - fields - 0x48) / 4).Select(i => i * 4)
                           .FirstOrDefault(a => guerrilla ? IsGuerrillaMeshBlock(header, a) : ModelFile.IsMeshBlock(header, a), -1);
        if (at < 0) throw new InvalidDataException($"no version-{(guerrilla ? 6 : 9)} mesh block");
        int fieldsAt = guerrilla ? at - 4 : at;  // where version 9's fields would start (it has one more before them)
        int submeshCount = (int)U32(header, fieldsAt + 0x14);
        int submeshTable = at + (int)U32(header, fieldsAt + 0x18);
        int vertexCount = (int)U32(header, fieldsAt + 0x1C);
        int vertexSize = header[fieldsAt + 0x20];
        int vertexFormat = header[fieldsAt + 0x21];
        int afterFloats = guerrilla ? fieldsAt + 0x24 - 0x3C : fieldsAt;  // version 6 hasn't the six floats
        int vertexOffset = (int)U32(header, afterFloats + 0x3C);
        int indexCount = (int)U32(header, afterFloats + 0x40);
        int indexOffset = (int)U32(header, afterFloats + 0x44);
        int indexSize = header[afterFloats + 0x48];
        int topology = header[afterFloats + 0x49];
        int blockCount = BitConverter.ToUInt16(header, afterFloats + 0x4A);
        if (vertexFormat != (guerrilla ? 14 : 24) || vertexSize != 8) throw new NotSupportedException($"terrain vertex format {vertexFormat} ({vertexSize} bytes)");
        if (indexSize != 2) throw new NotSupportedException($"{indexSize}-byte terrain indices");

        float step = guerrilla ? GuerrillaGridStep : GridStep;
        Positions = new Vector3[vertexCount];
        Normals = new Vector3[vertexCount];
        for (int v = 0; v < vertexCount; v++)
        {
            int p = vertexOffset + v * vertexSize;
            int xStep = data[p + 2], zStep = (data[p + 3] + 128) & 0xFF;
            Positions[v] = new Vector3(Origin.X + xStep * step, BitConverter.ToInt16(data, p) / 64f, Origin.Y + zStep * step);
            Normals[v] = Vector3.Normalize(new Vector3(data[p + 4], data[p + 5], data[p + 6]) / 127.5f - Vector3.One);
        }

        var indices = new int[indexCount];
        for (int i = 0; i < indexCount; i++) indices[i] = BitConverter.ToUInt16(data, indexOffset + i * 2);
        // Each submesh (a patch) has the next of the render blocks, as many as its first word says.
        int submeshSize = guerrilla ? 44 : 48;
        int blockTable = submeshTable + submeshCount * submeshSize;
        var patches = new List<int[]>();
        for (int s = 0, b = 0; s < submeshCount; s++)
        {
            var triangles = new List<int>();
            for (int end = Math.Min(blockCount, b + (int)U32(header, submeshTable + s * submeshSize)); b < end; b++)
            {
                int r = blockTable + b * 20;
                int first = (int)U32(header, r + 4), count = (int)U32(header, r + 8);
                triangles.AddRange(topology == 0 ? ModelFile.StripToTriangles(indices, first, count) : ModelFile.ListTriangles(indices, first, count));
            }
            patches.Add([.. triangles]);
        }
        PatchTriangles = patches;
        Triangles = [.. patches.SelectMany(p => p)];
    }

    public static TerrainSubzone Read(byte[] header, byte[] data) => new(header, data);

    /// <summary>
    /// Writes a sub-zone (version 42) with new ground, keeping all that isn't the ground from an
    /// Armageddon sub-zone with ground (normally the one it replaces): its header up to the
    /// patches (number, checksum, names, and the area the game fills in when loading it), the
    /// checksum that ties its c-file to its g-file, the render blocks' material word and what
    /// follows the mesh block and the vertices. The layout, checked on all 261 of the game's
    /// sub-zones with ground:
    ///   the patches from the template's first, 96 bytes each: the box's middle, no rotation (the
    ///     identity), the patch's number, the box around that middle, a bounding sphere (centre
    ///     0, radius), 0; then zeros up to a multiple of 16
    ///   the mesh block: 0x60 bytes of header (version 9, checksum, block size, g-file size, 1
    ///     detail level, the submesh count, the submesh table at 0x60, the vertex count, vertex
    ///     size 8 and format 24, the template's six floats, the vertex and index offsets, the
    ///     index count, 2-byte indices, strips, the render block count, zeros); a submesh per
    ///     patch (48 bytes: its render block count, pivot 0, the patch's box, two run-time words);
    ///     a render block per patch (20 bytes: material, first index, index count, lowest and
    ///     highest vertex); 8 zero bytes; the checksum again
    ///   the g-file: the checksum and 12 zero bytes, the indices (a strip per patch), zeros up to
    ///     a multiple of 16, the vertices (8 bytes each, as read), then the template's last 4 bytes.
    /// Each patch is one strip (see <see cref="Strip"/> for making one from triangles).
    /// </summary>
    /// <param name="origin">Where the vertex grid starts (x, z).</param>
    /// <param name="patches">Each patch's triangle strip, as the game's: every other triangle turned round, repeated indices joining pieces.</param>
    public static (byte[] Header, byte[] Data) Write(byte[] templateHeader, byte[] templateData, Vector2 origin,
                                                     IReadOnlyList<GroundVertex> vertices, IReadOnlyList<int[]> patches)
    {
        if (vertices.Count is 0 or > 65536) throw new ArgumentException($"{vertices.Count} vertices: a sub-zone holds 1 to 65,536");
        if (patches.Count == 0 || patches.Any(p => p.Length < 3)) throw new ArgumentException("every patch needs a strip");
        var template = new TerrainSubzone(templateHeader, templateData);
        if (template.FormatVersion != Version || template.PatchCount == 0) throw new ArgumentException("the template must be an Armageddon sub-zone with ground");
        int fields = (0x18 + BitConverter.ToInt32(templateHeader, 0x14) + 3) & ~3;
        int block = Enumerable.Range((fields + 0x48) / 4, (templateHeader.Length - fields - 0x48) / 4).Select(i => i * 4)
                              .First(a => ModelFile.IsMeshBlock(templateHeader, a));
        int firstPatch = Enumerable.Range((fields + 0x48) / 4, (block - fields - 0x48) / 4).Select(i => i * 4)
                                   .First(a => U32(templateHeader, a + 48) == 0 && F32(templateHeader, a + 12) == 1 && F32(templateHeader, a + 28) == 1 && F32(templateHeader, a + 44) == 1);
        uint checksum = U32(templateHeader, block + 4);
        int templateBlockEnd = block + (int)U32(templateHeader, block + 8);
        byte[] afterBlock = templateHeader[templateBlockEnd..];
        uint material = U32(templateHeader, block + 0x60 + (int)U32(templateHeader, block + 0x14) * 48);
        int templateVertexEnd = (int)U32(templateHeader, block + 0x3C) + (int)U32(templateHeader, block + 0x1C) * 8;
        byte[] dataTail = templateData.Length >= templateVertexEnd + 4 ? templateData[templateVertexEnd..(templateVertexEnd + 4)] : new byte[4];

        Vector3 Position(GroundVertex v) => new(origin.X + v.XStep * GridStep, v.Height, origin.Y + v.ZStep * GridStep);

        // Each patch: its box, its strip.
        var indices = new List<ushort>();
        var boxes = new List<(Vector3 Min, Vector3 Max)>();
        var blocks = new List<(int First, int Count, int Lowest, int Highest)>();
        foreach (int[] strip in patches)
        {
            Vector3 min = new(float.MaxValue), max = new(float.MinValue);
            foreach (int v in strip)
            {
                min = Vector3.Min(min, Position(vertices[v]));
                max = Vector3.Max(max, Position(vertices[v]));
            }
            boxes.Add((min, max));
            blocks.Add((indices.Count, strip.Length, strip.Min(), strip.Max()));
            foreach (int v in strip) indices.Add((ushort)v);
        }

        // The g-file.
        var data = new MemoryStream();
        var g = new BinaryWriter(data);
        g.Write(checksum); g.Write(0); g.Write(0); g.Write(0);
        foreach (ushort i in indices) g.Write(i);
        while (data.Length % 16 != 0) g.Write((byte)0);
        int vertexOffset = (int)data.Length;
        foreach (GroundVertex v in vertices)
        {
            if (v.XStep is < 0 or > 255 || v.ZStep is < 0 or > 255) throw new ArgumentException($"a vertex at grid step {v.XStep}, {v.ZStep}: steps go from 0 to 255");
            g.Write((short)Math.Clamp(MathF.Round(v.Height * 64), short.MinValue, short.MaxValue));
            g.Write((byte)v.XStep);
            g.Write((byte)((v.ZStep - 128) & 0xFF));
            Vector3 n = Vector3.Normalize(v.Normal);
            g.Write(NormalByte(n.X)); g.Write(NormalByte(n.Y)); g.Write(NormalByte(n.Z)); g.Write((byte)0x80);  // the 4th: 0x80 or 0xFF in the game's, unused by its shaders
        }
        g.Write(dataTail);
        byte[] dataBytes = data.ToArray();

        // The mesh block.
        var meshBlock = new MemoryStream();
        var m = new BinaryWriter(meshBlock);
        int blockSize = 0x60 + patches.Count * 48 + patches.Count * 20 + 8 + 4;
        m.Write(9); m.Write(checksum); m.Write(blockSize); m.Write(dataBytes.Length);
        m.Write(1); m.Write(patches.Count); m.Write(0x60); m.Write(vertices.Count);
        m.Write((byte)8); m.Write((byte)24); m.Write((byte)0); m.Write((byte)0);
        m.Write(templateHeader, block + 0x24, 0x18);  // the six floats
        m.Write(vertexOffset); m.Write(indices.Count); m.Write(16);
        m.Write((byte)2); m.Write((byte)0); m.Write((ushort)patches.Count);
        m.Write(new byte[0x60 - 0x4C]);
        foreach (var (min, max) in boxes)
        {
            Vector3 half = (max - min) / 2;
            m.Write(1); m.Write(0f); m.Write(0f); m.Write(0f);
            m.Write(-half.X); m.Write(-half.Y); m.Write(-half.Z); m.Write(half.X); m.Write(half.Y); m.Write(half.Z);
            m.Write(0); m.Write(0);
        }
        foreach (var (first, count, lowest, highest) in blocks)
        {
            m.Write(material); m.Write(first); m.Write(count); m.Write(lowest); m.Write(highest);
        }
        m.Write(0); m.Write(0); m.Write(checksum);

        // The c-file: the template's header with the new place, patch count and box; the patches; the block.
        var output = new MemoryStream();
        var h = new BinaryWriter(output);
        byte[] prefix = templateHeader[..firstPatch];
        Vector3 allMin = boxes.Select(b => b.Min).Aggregate(Vector3.Min), allMax = boxes.Select(b => b.Max).Aggregate(Vector3.Max);
        BitConverter.TryWriteBytes(prefix.AsSpan(fields + 0x08), origin.X);
        BitConverter.TryWriteBytes(prefix.AsSpan(fields + 0x10), origin.Y);
        BitConverter.TryWriteBytes(prefix.AsSpan(fields + 0x14), patches.Count);
        float[] box = [allMin.X, allMin.Y, allMin.Z, allMax.X, allMax.Y, allMax.Z, (allMin.X + allMax.X) / 2, (allMin.Y + allMax.Y) / 2, (allMin.Z + allMax.Z) / 2];
        for (int i = 0; i < box.Length; i++) BitConverter.TryWriteBytes(prefix.AsSpan(fields + 0x24 + i * 4), box[i]);
        h.Write(prefix);
        for (int p = 0; p < patches.Count; p++)
        {
            var (min, max) = boxes[p];
            Vector3 middle = (min + max) / 2, half = (max - min) / 2;
            h.Write(middle.X); h.Write(middle.Y); h.Write(middle.Z);
            h.Write(1f); h.Write(0f); h.Write(0f); h.Write(0f); h.Write(1f); h.Write(0f); h.Write(0f); h.Write(0f); h.Write(1f);
            h.Write(p);
            h.Write(-half.X); h.Write(-half.Y); h.Write(-half.Z); h.Write(half.X); h.Write(half.Y); h.Write(half.Z);
            h.Write(0f); h.Write(0f); h.Write(0f); h.Write(half.Length());
            h.Write(0);
        }
        while (output.Length % 16 != 0) h.Write((byte)0);
        h.Write(meshBlock.ToArray());
        h.Write(afterBlock);
        return (output.ToArray(), dataBytes);
    }

    static byte NormalByte(float value) => (byte)Math.Clamp(MathF.Round((value + 1) * 127.5f), 0, 255);

    /// <summary>
    /// A strip made of triangles (three vertex numbers each, wound as <see cref="PatchTriangles"/>
    /// reads them): each joined to the last by repeated indices, which make no triangles, and
    /// starting at an even place so it keeps its winding.
    /// </summary>
    public static int[] Strip(IReadOnlyList<int> triangles)
    {
        var strip = new List<int>(triangles.Count * 2);
        for (int t = 0; t + 2 < triangles.Count; t += 3)
        {
            if (strip.Count > 0)
            {
                strip.Add(strip[^1]);
                strip.Add(triangles[t]);
                if (strip.Count % 2 == 1) strip.Add(triangles[t]);
            }
            strip.Add(triangles[t]);
            strip.Add(triangles[t + 1]);
            strip.Add(triangles[t + 2]);
        }
        return [.. strip];
    }

    /// <summary>Guerrilla's version-6 mesh block: version, checksum, block size, g-file size, and the checksum again at its end.</summary>
    static bool IsGuerrillaMeshBlock(byte[] header, int at)
    {
        if (at < 0 || at + 0x30 > header.Length || U32(header, at) != 6) return false;
        uint size = U32(header, at + 8);
        return size >= 0x30 && at + size <= header.Length && U32(header, at + 4) != 0 && U32(header, at + (int)size - 4) == U32(header, at + 4);
    }

    /// <summary>The name of the sub-zone file pair's halves: world_08_02_6.ctmesh_pc and world_08_02_6.gtmesh_pc.</summary>
    public static string HeaderFileName(string zone, int number) => $"{zone}_{number}.ctmesh_pc";
    public static string DataFileName(string zone, int number) => $"{zone}_{number}.gtmesh_pc";

    static uint U32(byte[] bytes, int at) => BitConverter.ToUInt32(bytes, at);
    static float F32(byte[] bytes, int at) => BitConverter.ToSingle(bytes, at);
}

/// <summary>A vertex of ground to write (see <see cref="TerrainSubzone.Write"/>): its place on the sub-zone's grid (0-255 steps from its origin), its height in metres, and its normal.</summary>
public readonly record struct GroundVertex(int XStep, int ZStep, float Height, Vector3 Normal);
