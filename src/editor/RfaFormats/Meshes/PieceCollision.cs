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
/// Armageddon's (<see cref="Section(IReadOnlyList{Vector3}, IReadOnlyList{int})"/> writes it from
/// nothing; 930 of the game's 933 stitch pieces are laid out so, every byte the same but the boxes,
/// counts, tree and mesh, and two words below), in its __data__ section:
///   the list shape at 0 (0x80 bytes): its child array at +0x18 (one, capacity 0x80000001), its box
///     (the mesh's half-extents at +0x30, 0; the middle at +0x40, 1), 32 bytes of 0xFF at +0x50
///     (every child enabled), its child's entry at +0x70 (the tree, then 12 zeros)
///   the tree shape at 0x80 (0x40: zeros but its code at +0x14 and its child at +0x34, set at load;
///     +0x08 a user word, 0 in 804 of them)
///   the code at 0xC0 (0x30: from x, y, z at +0x10, then 2^24 / its size; its bytes' array at +0x20,
///     their count, 0x80000000 | the count, 1), the bytes from 0xF0, padded with zeros to 16
///   the mesh shape (0x110): 0x100 at +0x14; a copy of its part at +0x20; its box at +0x90 (the
///     mesh's half-extents widened by its radius 0.01, the radius; the middle, 0); 12 at +0xB4; its
///     parts' array at +0xB8 (one, 0x80000001), two empty arrays (+0xC4, +0xD0), 6 at +0xDC, the
///     triangle count at +0xE4, the radius at +0xE8, its vertices' array at +0xF0 and its triangles'
///     at +0xFC (each a count and 0x80000000 | the count after it)
///   its one part (0x70, a triangle part): 0x100, 0, 0x10000, 0, 0, the triangle count, 0, the vertex
///     count, 0, 12 (a vertex's bytes), the triangle offset (+0x28: leftover memory from Volition's
///     tools, a different word in nearly every file; 0 here), 0x20006 (6 bytes a triangle, 16-bit
///     numbers), zeros, then a transform's scale (1 at +0x5C and four 1s from +0x60)
///   the vertices and the triangles, each padded to 16
/// Its fixups: local (the list's children, the code's bytes, the shape's parts, vertices and
/// triangles), global (the list's child, the tree's code and child) and the objects' classes.
/// </summary>
public static class PieceCollision
{
    const uint Magic0 = 0x57E0E057, Magic1 = 0x10C0C010;

    /// <summary>The collision's triangles: its vertices and three vertex numbers a triangle.</summary>
    public static (Vector3[] Vertices, int[] Triangles) Read(byte[] cFile)
    {
        var (packfile, _, _) = SectionOf(cFile);
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

    // Havok 2010.2's class names as Armageddon's stitch pieces list them, with each class's signature.
    static readonly (uint Signature, string Name)[] Classes =
    [
        (0x75585EF6, "hkClass"), (0x5C7EA4C2, "hkClassMember"), (0x8A3609CF, "hkClassEnum"), (0xCE6F8A6C, "hkClassEnumItem"),
        (0xA1937CBD, "hkpListShape"), (0x90B29D39, "hkpMoppBvTreeShape"), (0x924C2661, "hkpMoppCode"), (0xB22A5053, "havok_mesh_shape"),
    ];

    const float Radius = 0.01f;

    /// <summary>Whether a model's collision (either game's) is a stitch piece's: one triangle mesh under a tree, not a destructible object's shapes.</summary>
    public static bool IsMesh(byte[] cFile)
    {
        int at = BitConverter.ToInt32(cFile, 0x1C);
        if (at <= 0 || at + 16 > cFile.Length || Encoding.ASCII.GetString(cFile, at, 4) != "MCKH") return false;
        return Packfile.Parse(cFile, at + 16).ObjectOf("havok_mesh_shape") != null;
    }

    /// <summary>A piece's tree: where its frame starts, its scale (2^24 / its size) and its code (as Armageddon's files keep it).</summary>
    public static (Vector3 From, float Scale, byte[] Code) Tree(byte[] cFile)
    {
        var (packfile, _, _) = SectionOf(cFile);
        var p = Packfile.Parse(cFile, packfile);
        int codeObject = p.ObjectOf("hkpMoppCode") ?? throw new InvalidDataException("no hkpMoppCode in the piece's collision");
        int code = p.Data + codeObject, bytesAt = p.Local.Single(f => f.From == codeObject + 0x20).To;
        int length = BitConverter.ToInt32(cFile, code + 0x24);
        return (new Vector3(BitConverter.ToSingle(cFile, code + 0x10), BitConverter.ToSingle(cFile, code + 0x14), BitConverter.ToSingle(cFile, code + 0x18)),
                BitConverter.ToSingle(cFile, code + 0x1C), cFile[(p.Data + bytesAt)..(p.Data + bytesAt + length)]);
    }

    /// <summary>Armageddon's collision section for a piece of these triangles: written from nothing, with a tree of ours over them.</summary>
    public static byte[] Section(IReadOnlyList<Vector3> vertices, IReadOnlyList<int> triangles)
    {
        Check(vertices, triangles);
        var min = new Vector3(vertices.Min(v => v.X), vertices.Min(v => v.Y), vertices.Min(v => v.Z));
        var max = new Vector3(vertices.Max(v => v.X), vertices.Max(v => v.Y), vertices.Max(v => v.Z));
        const float margin = 0.05f;
        var from = min - new Vector3(margin);
        float size = MathF.Max(max.X - min.X, MathF.Max(max.Y - min.Y, max.Z - min.Z)) + 2 * margin;
        var boxes = Enumerable.Range(0, triangles.Count / 3).Select(t =>
        {
            Vector3 a = vertices[triangles[3 * t]], b = vertices[triangles[3 * t + 1]], c = vertices[triangles[3 * t + 2]];
            return (Vector3.Min(a, Vector3.Min(b, c)), Vector3.Max(a, Vector3.Max(b, c)));
        }).ToList();
        return Section(vertices, triangles, from, 16777216f / size, StitchList.TreeCode(boxes, from, size));
    }

    /// <summary>As <see cref="Section(IReadOnlyList{Vector3}, IReadOnlyList{int})"/> with the tree given: its frame's start, scale (2^24 / its size) and code.</summary>
    public static byte[] Section(IReadOnlyList<Vector3> vertices, IReadOnlyList<int> triangles, Vector3 treeFrom, float treeScale, byte[] treeCode)
    {
        Check(vertices, triangles);
        int triangleCount = triangles.Count / 3;
        var min = new Vector3(vertices.Min(v => v.X), vertices.Min(v => v.Y), vertices.Min(v => v.Z));
        var max = new Vector3(vertices.Max(v => v.X), vertices.Max(v => v.Y), vertices.Max(v => v.Z));
        var half = (max - min) / 2 + new Vector3(Radius);
        var middle = (max + min) / 2;

        var d = new List<byte>();
        void Room(int bytes) => d.AddRange(new byte[bytes]);
        void I(int at, int value) { var b = BitConverter.GetBytes(value); for (int k = 0; k < 4; k++) d[at + k] = b[k]; }
        void U(int at, uint value) => I(at, unchecked((int)value));
        void F(int at, float value) { var b = BitConverter.GetBytes(value); for (int k = 0; k < 4; k++) d[at + k] = b[k]; }
        void V(int at, Vector3 v, float w) { F(at, v.X); F(at + 4, v.Y); F(at + 8, v.Z); F(at + 12, w); }

        // The list shape (one child) and its child's entry.
        const int list = 0, tree = 0x80, code = 0xC0, codeBytes = 0xF0;
        Room(0x80);
        I(list + 0x1C, 1); U(list + 0x20, 0x80000001);
        V(list + 0x30, half, 0); V(list + 0x40, middle, 1);
        for (int k = list + 0x50; k < list + 0x70; k++) d[k] = 0xFF;
        // The tree shape (its pointers set at load).
        Room(0x40);
        // The code.
        Room(0x30);
        V(code + 0x10, treeFrom, treeScale);
        I(code + 0x24, treeCode.Length); U(code + 0x28, 0x80000000u | (uint)treeCode.Length); I(code + 0x2C, 1);
        d.AddRange(treeCode);
        Havok2010Packfile.Pad(d, 0);
        // The mesh shape, with a copy of its part.
        int shape = d.Count, part = shape + 0x110;
        Room(0x110 + 0x70);
        I(shape + 0x14, 0x100);
        foreach (int at in new[] { shape + 0x20, part })
        {
            I(at + 0x00, 0x100); I(at + 0x08, 0x10000);
            I(at + 0x14, triangleCount); I(at + 0x1C, vertices.Count);
            I(at + 0x24, 12); I(at + 0x2C, 0x20006);
            for (int k = 0x5C; k < 0x70; k += 4) F(at + k, 1);
        }
        V(shape + 0x90, half, Radius); V(shape + 0xA0, middle, 0);
        I(shape + 0xB4, 12);
        I(shape + 0xBC, 1); U(shape + 0xC0, 0x80000001);
        U(shape + 0xCC, 0x80000000); U(shape + 0xD8, 0x80000000);
        I(shape + 0xDC, 6);
        I(shape + 0xE4, triangleCount); F(shape + 0xE8, Radius);
        I(shape + 0xF4, vertices.Count); U(shape + 0xF8, 0x80000000u | (uint)vertices.Count);
        I(shape + 0x100, triangleCount); U(shape + 0x104, 0x80000000u | (uint)triangleCount);
        // The vertices and triangles.
        int vertexAt = d.Count;
        foreach (var v in vertices) { d.AddRange(BitConverter.GetBytes(v.X)); d.AddRange(BitConverter.GetBytes(v.Y)); d.AddRange(BitConverter.GetBytes(v.Z)); }
        Havok2010Packfile.Pad(d, 0);
        int triangleAt = d.Count;
        foreach (int v in triangles) d.AddRange(BitConverter.GetBytes((ushort)v));

        byte[] packfile = Havok2010Packfile.Write(Classes, "hkpListShape", d,
            [(list + 0x18, list + 0x70), (code + 0x20, codeBytes), (shape + 0xB8, part), (shape + 0xF0, vertexAt), (shape + 0xFC, triangleAt)],
            [(list + 0x70, tree), (tree + 0x14, code), (tree + 0x34, shape)],
            [(list, "hkpListShape"), (tree, "hkpMoppBvTreeShape"), (code, "hkpMoppCode"), (shape, "havok_mesh_shape")]);
        return Havok2010Packfile.Section(packfile, 1);
    }

    static void Check(IReadOnlyList<Vector3> vertices, IReadOnlyList<int> triangles)
    {
        if (vertices.Count is 0 or > 65536 || triangles.Count == 0 || triangles.Count % 3 != 0) throw new ArgumentException("a collision mesh needs 1 to 65,536 vertices and whole triangles");
        if (triangles.Any(v => v < 0 || v >= vertices.Count)) throw new ArgumentException("a triangle names a vertex that isn't there");
    }


    /// <summary>Where the collision's packfile starts, and its section (with the MCKH header): where, how long.</summary>
    static (int Packfile, int SectionAt, int SectionLength) SectionOf(byte[] cFile)
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
