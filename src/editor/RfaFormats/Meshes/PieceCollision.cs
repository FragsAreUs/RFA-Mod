using System.Numerics;
using System.Text;
using RfaFormats.Zones;

namespace RfaFormats.Meshes;

/// <summary>
/// A stitch piece's own collision (both games): a section of the piece's c-file, named in its header
/// (+0x1C where, +0x20 how long), holding "MCKH", a version (Armageddon 5, Guerrilla 2), its length
/// and 1, then a Havok packfile (Armageddon hk_2010.2.0-r1, Guerrilla Havok-5.5.0-r1) of four objects:
/// an hkpListShape of one child, an hkpMoppBvTreeShape, its hkpMoppCode, and the game's own
/// havok_mesh_shape (an extended mesh shape of one triangle part) with its vertices (three floats)
/// and triangles (three 16-bit vertex numbers) in arrays of its own. The tree's leaves are the
/// triangles (StitchList.TreeCode's language). After the section: the piece's physical material
/// ("default"), unaffected by the section's length.
///
/// Armageddon's packfile (ter_sur_blder's), in its __data__ section: the list shape at 0 (0x80 bytes:
/// its box's half-extents at +0x30, middle at +0x40), the tree shape at 0x80 (0x40), the code at 0xC0
/// (from x, y, z at +0x10, then 2^24 / its size; its bytes' array at +0x20, the bytes from 0xF0, padded
/// with zeros to 16), the mesh shape next (0x110 bytes: the counts of its part's copy at +0x34
/// triangles and +0x3C vertices, its box at +0x90 and +0xA0 (the mesh's, widened by its radius 0.01,
/// the half-extents' fourth number; the list's box the same), triangles again at +0xE4, the array of
/// parts at +0xB8, the vertices' at +0xF0 and the triangles' at +0xFC, each a pointer, a count and a
/// capacity with 0x80000000 set), its one part (0x70: triangles
/// +0x14, vertices +0x1C), the vertices, the triangles, each padded to 16. Pointers: local fixups
/// (0x18 → 0x70, 0xE0 → 0xF0, and the mesh shape's three arrays), global fixups (the list's child,
/// the tree's code and its child), and the objects' classes (virtual fixups), each table padded with
/// 0xFF to 16.
/// </summary>
public static class PieceCollision
{
    const uint Magic0 = 0x57E0E057, Magic1 = 0x10C0C010;

    /// <summary>The collision's triangles: its vertices and three vertex numbers a triangle.</summary>
    public static (Vector3[] Vertices, int[] Triangles) Read(byte[] cFile)
    {
        var (packfile, _, _) = Section(cFile);
        var p = Packfile.Parse(cFile, packfile);
        int shape = p.ObjectOf("havok_mesh_shape") ?? throw new InvalidDataException("no havok_mesh_shape in the piece's collision");
        // The mesh shape's own array pointers, in order: its parts, its vertices, its triangles.
        var arrays = p.Local.Where(f => f.From > shape && f.From < shape + 0x200).OrderBy(f => f.From).ToList();
        if (arrays.Count < 3) throw new InvalidDataException("the havok_mesh_shape's arrays weren't found");
        var (vertexFrom, vertexAt) = arrays[^2];
        var (triangleFrom, triangleAt) = arrays[^1];
        int vertexCount = BitConverter.ToInt32(cFile, p.Data + vertexFrom + 4), triangleCount = BitConverter.ToInt32(cFile, p.Data + triangleFrom + 4);
        var vertices = new Vector3[vertexCount];
        for (int i = 0; i < vertexCount; i++)
        {
            int at = p.Data + vertexAt + i * 12;
            vertices[i] = new Vector3(BitConverter.ToSingle(cFile, at), BitConverter.ToSingle(cFile, at + 4), BitConverter.ToSingle(cFile, at + 8));
        }
        var triangles = new int[triangleCount * 3];
        for (int i = 0; i < triangles.Length; i++) triangles[i] = BitConverter.ToUInt16(cFile, p.Data + triangleAt + i * 2);
        if (triangles.Any(v => v >= vertexCount)) throw new InvalidDataException("a collision triangle names a vertex past the last");
        return (vertices, triangles);
    }

    /// <summary>
    /// <paramref name="cFile"/> (an Armageddon piece's c-file, as <see cref="RfaFormats.Guerrilla.GuerrillaRocks.TemplatePiece"/>)
    /// with its collision made of these triangles instead: the packfile written again in its own
    /// layout, with a MOPP tree over the triangles, and the section's two lengths set.
    /// </summary>
    public static byte[] Replace(byte[] cFile, IReadOnlyList<Vector3> vertices, IReadOnlyList<int> triangles)
    {
        if (vertices.Count is 0 or > 65536 || triangles.Count == 0 || triangles.Count % 3 != 0) throw new ArgumentException("a collision mesh needs 1 to 65,536 vertices and whole triangles");
        if (triangles.Any(v => v < 0 || v >= vertices.Count)) throw new ArgumentException("a triangle names a vertex that isn't there");
        var (packfile, sectionAt, sectionLength) = Section(cFile);
        var p = Packfile.Parse(cFile, packfile);
        if (p.Version != "hk_2010.2.0-r1") throw new InvalidDataException($"the template's collision is Havok {p.Version}, not hk_2010.2.0-r1");
        int list = p.ObjectOf("hkpListShape") ?? -1, tree = p.ObjectOf("hkpMoppBvTreeShape") ?? -1, code = p.ObjectOf("hkpMoppCode") ?? -1, shape = p.ObjectOf("havok_mesh_shape") ?? -1;
        if (list != 0 || tree != 0x80 || code != 0xC0 || shape < 0xF0) throw new InvalidDataException("the template's collision isn't laid out as ter_sur_blder's");
        byte[] old = cFile.AsSpan(p.Data, p.DataLength).ToArray();

        // The box and the tree.
        var min = new Vector3(vertices.Min(v => v.X), vertices.Min(v => v.Y), vertices.Min(v => v.Z));
        var max = new Vector3(vertices.Max(v => v.X), vertices.Max(v => v.Y), vertices.Max(v => v.Z));
        const float margin = 0.05f;
        var from = min - new Vector3(margin);
        float size = MathF.Max(max.X - min.X, MathF.Max(max.Y - min.Y, max.Z - min.Z)) + 2 * margin;
        int triangleCount = triangles.Count / 3;
        var boxes = Enumerable.Range(0, triangleCount).Select(t =>
        {
            Vector3 a = vertices[triangles[3 * t]], b = vertices[triangles[3 * t + 1]], c = vertices[triangles[3 * t + 2]];
            return (Vector3.Min(a, Vector3.Min(b, c)), Vector3.Max(a, Vector3.Max(b, c)));
        }).ToList();
        byte[] mopp = StitchList.TreeCode(boxes, from, size);

        // The __data__ section again.
        var data = new List<byte>(old.AsSpan(0, 0xF0).ToArray());
        data.AddRange(mopp);
        Pad(data, 0);
        int newShape = data.Count;
        data.AddRange(old.AsSpan(shape, 0x110 + 0x70).ToArray());
        int part = newShape + 0x110, vertexAt = data.Count;
        foreach (var v in vertices) { data.AddRange(BitConverter.GetBytes(v.X)); data.AddRange(BitConverter.GetBytes(v.Y)); data.AddRange(BitConverter.GetBytes(v.Z)); }
        Pad(data, 0);
        int triangleAt = data.Count;
        foreach (int v in triangles) data.AddRange(BitConverter.GetBytes((ushort)v));
        Pad(data, 0);
        byte[] d = [.. data];
        void Int(int at, int value) => BitConverter.TryWriteBytes(d.AsSpan(at), value);
        void Uint(int at, uint value) => BitConverter.TryWriteBytes(d.AsSpan(at), value);
        // The boxes are the mesh's widened by the shape's radius (the half-extents' fourth number: 0.01).
        float radius = BitConverter.ToSingle(d, newShape + 0x9C);
        void Box(int at)
        {
            var half = (max - min) / 2 + new Vector3(radius); var middle = (max + min) / 2;
            BitConverter.TryWriteBytes(d.AsSpan(at), half.X); BitConverter.TryWriteBytes(d.AsSpan(at + 4), half.Y); BitConverter.TryWriteBytes(d.AsSpan(at + 8), half.Z);
            BitConverter.TryWriteBytes(d.AsSpan(at + 16), middle.X); BitConverter.TryWriteBytes(d.AsSpan(at + 20), middle.Y); BitConverter.TryWriteBytes(d.AsSpan(at + 24), middle.Z);
        }
        Box(list + 0x30);
        BitConverter.TryWriteBytes(d.AsSpan(code + 0x10), from.X); BitConverter.TryWriteBytes(d.AsSpan(code + 0x14), from.Y);
        BitConverter.TryWriteBytes(d.AsSpan(code + 0x18), from.Z); BitConverter.TryWriteBytes(d.AsSpan(code + 0x1C), 16777216f / size);
        Int(code + 0x24, mopp.Length); Uint(code + 0x28, 0x80000000u | (uint)mopp.Length);
        Int(newShape + 0x34, triangleCount); Int(newShape + 0x3C, vertices.Count);
        Box(newShape + 0x90);
        Int(newShape + 0xE4, triangleCount);
        Int(newShape + 0xF4, vertices.Count); Uint(newShape + 0xF8, 0x80000000u | (uint)vertices.Count);
        Int(newShape + 0x100, triangleCount); Uint(newShape + 0x104, 0x80000000u | (uint)triangleCount);
        Int(part + 0x14, triangleCount); Int(part + 0x1C, vertices.Count);

        // The pointer and class tables.
        var tables = new List<byte>();
        int localAt = d.Length;
        foreach (var (f, t) in new[] { (0x18, 0x70), (0xE0, 0xF0), (newShape + 0xB8, part), (newShape + 0xF0, vertexAt), (newShape + 0xFC, triangleAt) })
        { tables.AddRange(BitConverter.GetBytes(f)); tables.AddRange(BitConverter.GetBytes(t)); }
        Pad(tables, 0xFF);
        int globalAt = localAt + tables.Count;
        foreach (var (f, t) in new[] { (0x70, 0x80), (0x94, 0xC0), (0xB4, newShape) })
        { tables.AddRange(BitConverter.GetBytes(f)); tables.AddRange(BitConverter.GetBytes(2)); tables.AddRange(BitConverter.GetBytes(t)); }
        Pad(tables, 0xFF);
        int virtualAt = localAt + tables.Count;
        foreach (var (at, name) in new[] { (list, "hkpListShape"), (tree, "hkpMoppBvTreeShape"), (code, "hkpMoppCode"), (newShape, "havok_mesh_shape") })
        { tables.AddRange(BitConverter.GetBytes(at)); tables.AddRange(BitConverter.GetBytes(0)); tables.AddRange(BitConverter.GetBytes(p.ClassName(name))); }
        Pad(tables, 0xFF);
        int end = localAt + tables.Count;

        // The packfile: its headers and class names as they were, the __data__ section's offsets set.
        int head = p.DataStart;   // from the packfile's start to its __data__ section
        var pack = new byte[head + end];
        Array.Copy(cFile, packfile, pack, 0, head);
        d.CopyTo(pack, head);
        tables.ToArray().CopyTo(pack, head + d.Length);
        int dataHeader = 0x40 + 2 * 0x30 + 20;
        foreach (var (k, v) in new[] { (1, localAt), (2, globalAt), (3, virtualAt), (4, end), (5, end), (6, end) })
            BitConverter.TryWriteBytes(pack.AsSpan(dataHeader + 4 * k), v);

        // The c-file: up to the section, its "MCKH" header with the new length, the packfile, then the rest.
        int sectionHeader = packfile - sectionAt;
        var result = new List<byte>(cFile.AsSpan(0, packfile).ToArray());
        result.AddRange(pack);
        int newLength = sectionHeader + pack.Length;
        result.AddRange(cFile.AsSpan(sectionAt + sectionLength).ToArray());
        byte[] file = [.. result];
        BitConverter.TryWriteBytes(file.AsSpan(sectionAt + 8), newLength);
        BitConverter.TryWriteBytes(file.AsSpan(0x20), newLength);
        return file;
    }

    static void Pad(List<byte> bytes, byte with) { while (bytes.Count % 16 != 0) bytes.Add(with); }

    /// <summary>Where the collision's packfile starts, and its section (with the MCKH header): where, how long.</summary>
    static (int Packfile, int SectionAt, int SectionLength) Section(byte[] cFile)
    {
        int at = BitConverter.ToInt32(cFile, 0x1C), length = BitConverter.ToInt32(cFile, 0x20);
        if (at <= 0 || at + 16 > cFile.Length || Encoding.ASCII.GetString(cFile, at, 4) != "MCKH" || BitConverter.ToInt32(cFile, at + 8) != length || at + length > cFile.Length)
            throw new InvalidDataException("the piece's collision section (MCKH) isn't where its header says");
        int packfile = at + 16;
        if (BitConverter.ToUInt32(cFile, packfile) != Magic0 || BitConverter.ToUInt32(cFile, packfile + 4) != Magic1) throw new InvalidDataException("no Havok packfile in the piece's collision section");
        return (packfile, at, length);
    }

    /// <summary>A Havok packfile's parts this needs: its __data__ section, objects and local pointers.</summary>
    sealed class Packfile
    {
        public string Version = "";
        public int DataStart;     // from the packfile's start
        public int Data;          // in the file
        public int DataLength;
        public List<(int From, int To)> Local = [];
        public List<(int At, string Class)> Objects = [];
        readonly Dictionary<string, int> classOffsets = [];

        public int? ObjectOf(string name) => Objects.Where(o => o.Class == name).Select(o => (int?)o.At).FirstOrDefault();
        public int ClassName(string name) => classOffsets.TryGetValue(name, out int at) ? at : throw new InvalidDataException($"no class {name} in the packfile");

        public static Packfile Parse(byte[] f, int at)
        {
            int I(int a) => BitConverter.ToInt32(f, a);
            var p = new Packfile { Version = Encoding.ASCII.GetString(f, at + 0x28, 16).Split('\0')[0] };
            int sections = I(at + 0x14);
            if (sections != 3) throw new InvalidDataException($"a packfile of {sections} sections, not 3");
            int Header(int s) => at + 0x40 + s * 0x30 + 20;
            int names = at + I(Header(0)), namesLength = I(Header(0) + 4);
            for (int q = 0; q + 5 < namesLength && f[names + q] != 0xFF;)
            {
                q += 5;
                int stop = Array.IndexOf(f, (byte)0, names + q);
                p.classOffsets[Encoding.ASCII.GetString(f, names + q, stop - names - q)] = q;
                q = stop - names + 1;
            }
            p.DataStart = I(Header(2));
            p.Data = at + p.DataStart;
            int local = I(Header(2) + 4), global = I(Header(2) + 8), virt = I(Header(2) + 12), exports = I(Header(2) + 16);
            p.DataLength = local;
            for (int q = local; q + 8 <= global; q += 8)
                if (I(p.Data + q) != -1) p.Local.Add((I(p.Data + q), I(p.Data + q + 4)));
            var byOffset = p.classOffsets.ToDictionary(kv => kv.Value, kv => kv.Key);
            for (int q = virt; q + 12 <= exports; q += 12)
                if (I(p.Data + q) != -1) p.Objects.Add((I(p.Data + q), byOffset.GetValueOrDefault(I(p.Data + q + 8), "?")));
            return p;
        }
    }
}
