using System.Numerics;
using System.Text;

namespace RfaFormats.Meshes;

/// <summary>One of a destructible object's collision shapes.</summary>
public abstract record ChunkShape;

/// <summary>A convex collision shape: its corners, its faces' planes (normal and distance: n · p + d is 0 on the face, below 0 inside), and the radius it is rounded by.</summary>
public sealed record ConvexShape(float Radius, Vector3[] Vertices, Vector4[] Planes) : ChunkShape;

/// <summary>Several shapes as one (a list inside the list: an object made of several hulls).</summary>
public sealed record ShapeList(IReadOnlyList<ChunkShape> Children) : ChunkShape;

/// <summary>
/// A destructible object's collision (both games' .cchk_pc files): the section the header names at
/// +0x1C (where) and +0x20 (how long): "MCKH", a version (Armageddon 5, Guerrilla 2), its length and
/// the number of shapes, then a Havok packfile: an hkpListShape whose children, in order, are the
/// shapes the file's objects and pieces name by number. Nearly all are hkpConvexVerticesShape
/// (572 of Guerrilla's 576 world files, 1,195 of Armageddon's 1,240); a few hkpBoxShape; and a child
/// can be a list of its own (Guerrilla 20 files, Armageddon 33: chairs, generators, a minecart), in
/// which the section's count is still the top list's.
///
/// A convex shape (both Havok 5.5 and 2010): the radius at +0x10, its box (half-extents +0x20,
/// middle +0x30, of the corners alone), an array of corners at +0x40 in blocks of four, each block
/// four x, four y, four z (the last corner repeated to fill one), their count at +0x4C, and an array
/// of planes (Guerrilla's at +0x50, Armageddon's at +0x58). A box (5.5): the radius at +0x10, the
/// half-extents at +0x20, round its origin.
///
/// Armageddon's packfile, as <see cref="Section"/> writes it (str_sur_edf_lightpole_a's layout):
/// hk_2010.2.0-r1, three sections (__classnames__ at 0xD0, 0x80 long; __types__ empty; __data__
/// from 0x150); the class names hkClass, hkClassMember, hkClassEnum, hkClassEnumItem, hkpListShape,
/// hkpConvexVerticesShape with their signatures; in __data__ the list (0x70 bytes: its children's
/// array at +0x18, the box round each child's box widened by its radius at +0x30 (fourth number:
/// the largest radius) and +0x40 (fourth 1), every child enabled), 16 empty bytes a child (filled
/// at load), then each child: a shape (0x70 bytes, its corners from +0x70, its planes after them),
/// or a list laid out the same way (its box round its children's, which is how its parent takes
/// it); then the pointer tables (local: arrays; global: the children; virtual: the objects'
/// classes), each padded with 0xFF to 16.
/// </summary>
public static class ChunkCollision
{
    const uint Magic0 = 0x57E0E057, Magic1 = 0x10C0C010;

    // Havok 2010.2's class names as Armageddon's packfiles list them, with each class's signature.
    static readonly (uint Signature, string Name)[] Classes =
    [
        (0x75585EF6, "hkClass"), (0x5C7EA4C2, "hkClassMember"), (0x8A3609CF, "hkClassEnum"),
        (0xCE6F8A6C, "hkClassEnumItem"), (0xA1937CBD, "hkpListShape"), (0x28726AD8, "hkpConvexVerticesShape"),
    ];

    /// <summary>The shapes of a destructible object's collision (Havok 5.5 or 2010), in the list's order; boxes as eight-cornered shapes.</summary>
    public static List<ChunkShape> Read(byte[] cFile)
    {
        int section = BitConverter.ToInt32(cFile, 0x1C), at = section + 16;
        if (section <= 0 || at + 0x100 > cFile.Length || Encoding.ASCII.GetString(cFile, section, 4) != "MCKH" ||
            BitConverter.ToUInt32(cFile, at) != Magic0 || BitConverter.ToUInt32(cFile, at + 4) != Magic1)
            throw new InvalidDataException("no Havok collision where the header says");
        int I(int a) => BitConverter.ToInt32(cFile, a);
        int Header(int s) => at + 0x40 + s * 0x30 + 20;
        int names = at + I(Header(0)), data = at + I(Header(2));
        int local = I(Header(2) + 4), global = I(Header(2) + 8), virt = I(Header(2) + 12), exports = I(Header(2) + 16);
        var classOf = new Dictionary<int, string>();
        for (int q = virt; q + 12 <= exports; q += 12)
        {
            if (I(data + q) == -1) continue;
            int n = names + I(data + q + 8);
            classOf[I(data + q)] = Encoding.ASCII.GetString(cFile, n, Array.IndexOf(cFile, (byte)0, n) - n);
        }
        var locals = new List<(int From, int To)>();
        for (int q = local; q + 8 <= global; q += 8) if (I(data + q) != -1) locals.Add((I(data + q), I(data + q + 4)));
        var globals = new Dictionary<int, int>();
        for (int q = global; q + 12 <= virt; q += 12) if (I(data + q) != -1) globals[I(data + q)] = I(data + q + 8);

        // Each list's children; the top list is the one no list has as a child.
        var childrenOf = new Dictionary<int, List<int>>();
        foreach (int list in classOf.Where(kv => kv.Value == "hkpListShape").Select(kv => kv.Key))
        {
            int count = I(data + list + 0x1C);
            int childArray = count == 0 ? 0 : locals.Single(f => f.From == list + 0x18).To;
            childrenOf[list] = Enumerable.Range(0, count).Select(i => globals[childArray + 16 * i]).ToList();
        }
        int top = childrenOf.Keys.Single(l => !childrenOf.Values.Any(c => c.Contains(l)));

        Vector3 V(int a) => new(BitConverter.ToSingle(cFile, a), BitConverter.ToSingle(cFile, a + 4), BitConverter.ToSingle(cFile, a + 8));
        ChunkShape Shape(int shape)
        {
            float radius = BitConverter.ToSingle(cFile, data + shape + 0x10);
            switch (classOf[shape])
            {
                case "hkpListShape":
                    return new ShapeList(childrenOf[shape].Select(Shape).ToList());
                case "hkpConvexVerticesShape":
                {
                    var arrays = locals.Where(f => f.From > shape && f.From < shape + 0x70).OrderBy(f => f.From).ToList();
                    if (arrays.Count != 2) throw new InvalidDataException($"the convex shape at 0x{shape:X} hasn't two arrays");
                    int count = I(data + shape + 0x4C), planeCount = I(data + arrays[1].From + 4);
                    var vertices = new Vector3[count];
                    for (int v = 0; v < count; v++)
                    {
                        int block = data + arrays[0].To + (v / 4) * 48, lane = (v % 4) * 4;
                        vertices[v] = new Vector3(BitConverter.ToSingle(cFile, block + lane), BitConverter.ToSingle(cFile, block + 16 + lane), BitConverter.ToSingle(cFile, block + 32 + lane));
                    }
                    var planes = new Vector4[planeCount];
                    for (int p = 0; p < planeCount; p++)
                    {
                        int a = data + arrays[1].To + p * 16;
                        planes[p] = new Vector4(V(a), BitConverter.ToSingle(cFile, a + 12));
                    }
                    return new ConvexShape(radius, vertices, planes);
                }
                case "hkpBoxShape":
                {
                    var h = V(data + shape + 0x20);
                    var corners = Enumerable.Range(0, 8).Select(k => new Vector3((k & 1) == 0 ? -h.X : h.X, (k & 2) == 0 ? -h.Y : h.Y, (k & 4) == 0 ? -h.Z : h.Z)).ToArray();
                    Vector4[] faces = [new(1, 0, 0, -h.X), new(-1, 0, 0, -h.X), new(0, 1, 0, -h.Y), new(0, -1, 0, -h.Y), new(0, 0, 1, -h.Z), new(0, 0, -1, -h.Z)];
                    return new ConvexShape(radius, corners, faces);
                }
                default: throw new NotSupportedException($"the collision shape at 0x{shape:X} is a {classOf[shape]}");
            }
        }
        return childrenOf[top].Select(Shape).ToList();
    }

    /// <summary>Armageddon's collision section for these shapes: its "MCKH" header (version 5) and the Havok 2010 packfile.</summary>
    public static byte[] Section(IReadOnlyList<ChunkShape> shapes)
    {
        if (shapes.Count == 0) throw new ArgumentException("a collision needs at least one shape");

        // Class names.
        var names = new List<byte>();
        var nameAt = new Dictionary<string, int>();
        foreach (var (signature, name) in Classes)
        {
            names.AddRange(BitConverter.GetBytes(signature));
            names.Add(0x09);
            nameAt[name] = names.Count;
            names.AddRange(Encoding.ASCII.GetBytes(name));
            names.Add(0);
        }
        Pad(names, 0xFF);

        // __data__: the list, its children's entries, each child (a list with its own after it).
        var d = new List<byte>();
        var locals = new List<(int From, int To)>();
        var globals = new List<(int From, int To)>();
        var objects = new List<(int At, string Class)>();
        void PutI(int a, int v) { var b = BitConverter.GetBytes(v); for (int k = 0; k < 4; k++) d[a + k] = b[k]; }
        void PutF(int a, float v) { var b = BitConverter.GetBytes(v); for (int k = 0; k < 4; k++) d[a + k] = b[k]; }

        // Each writer returns the box its parent takes the shape by, and the largest radius in it.
        (Vector3 Min, Vector3 Max, float Radius) List(IReadOnlyList<ChunkShape> children)
        {
            if (children.Count == 0) throw new ArgumentException("a list needs at least one shape");
            int at = d.Count;
            objects.Add((at, "hkpListShape"));
            d.AddRange(new byte[0x70 + 16 * children.Count]);
            PutI(at + 0x1C, children.Count); PutI(at + 0x20, unchecked((int)0x80000000u) | children.Count);
            for (int k = at + 0x50; k < at + 0x70; k++) d[k] = 0xFF;
            locals.Add((at + 0x18, at + 0x70));
            Vector3 min = new(float.MaxValue), max = new(float.MinValue);
            float radius = 0;
            for (int i = 0; i < children.Count; i++)
            {
                globals.Add((at + 0x70 + 16 * i, d.Count));
                var (childMin, childMax, childRadius) = children[i] switch
                {
                    ConvexShape s => Convex(s),
                    ShapeList l => List(l.Children),
                    _ => throw new NotSupportedException(children[i].GetType().Name),
                };
                min = Vector3.Min(min, childMin); max = Vector3.Max(max, childMax); radius = Math.Max(radius, childRadius);
            }
            var half = (max - min) / 2; var middle = (max + min) / 2;
            PutF(at + 0x30, half.X); PutF(at + 0x34, half.Y); PutF(at + 0x38, half.Z); PutF(at + 0x3C, radius);
            PutF(at + 0x40, middle.X); PutF(at + 0x44, middle.Y); PutF(at + 0x48, middle.Z); PutF(at + 0x4C, 1);
            return (min, max, radius);
        }

        (Vector3 Min, Vector3 Max, float Radius) Convex(ConvexShape s)
        {
            if (s.Vertices.Length == 0 || s.Planes.Length == 0) throw new ArgumentException("a shape needs corners and planes");
            int at = d.Count, blocks = (s.Vertices.Length + 3) / 4;
            objects.Add((at, "hkpConvexVerticesShape"));
            d.AddRange(new byte[0x70]);
            var min = s.Vertices.Aggregate(new Vector3(float.MaxValue), Vector3.Min);
            var max = s.Vertices.Aggregate(new Vector3(float.MinValue), Vector3.Max);
            PutF(at + 0x10, s.Radius);
            var half = (max - min) / 2; var middle = (max + min) / 2;
            PutF(at + 0x20, half.X); PutF(at + 0x24, half.Y); PutF(at + 0x28, half.Z);
            PutF(at + 0x30, middle.X); PutF(at + 0x34, middle.Y); PutF(at + 0x38, middle.Z);
            PutI(at + 0x44, blocks); PutI(at + 0x48, unchecked((int)0x80000000u) | blocks);
            PutI(at + 0x4C, s.Vertices.Length);
            PutI(at + 0x5C, s.Planes.Length); PutI(at + 0x60, unchecked((int)0x80000000u) | s.Planes.Length);
            locals.Add((at + 0x40, d.Count));
            for (int b = 0; b < blocks; b++)
                for (int axis = 0; axis < 3; axis++)
                    for (int lane = 0; lane < 4; lane++)
                    {
                        var v = s.Vertices[Math.Min(b * 4 + lane, s.Vertices.Length - 1)];
                        d.AddRange(BitConverter.GetBytes(axis == 0 ? v.X : axis == 1 ? v.Y : v.Z));
                    }
            locals.Add((at + 0x58, d.Count));
            foreach (var p in s.Planes) { d.AddRange(BitConverter.GetBytes(p.X)); d.AddRange(BitConverter.GetBytes(p.Y)); d.AddRange(BitConverter.GetBytes(p.Z)); d.AddRange(BitConverter.GetBytes(p.W)); }
            return (min - new Vector3(s.Radius), max + new Vector3(s.Radius), s.Radius);
        }

        List(shapes);
        Pad(d, 0);
        int dataLength = d.Count;
        foreach (var (f, t) in locals) { d.AddRange(BitConverter.GetBytes(f)); d.AddRange(BitConverter.GetBytes(t)); }
        Pad(d, 0xFF);
        int globalAt = d.Count;
        foreach (var (f, t) in globals) { d.AddRange(BitConverter.GetBytes(f)); d.AddRange(BitConverter.GetBytes(2)); d.AddRange(BitConverter.GetBytes(t)); }
        Pad(d, 0xFF);
        int virtualAt = d.Count;
        foreach (var (at, name) in objects) { d.AddRange(BitConverter.GetBytes(at)); d.AddRange(BitConverter.GetBytes(0)); d.AddRange(BitConverter.GetBytes(nameAt[name])); }
        Pad(d, 0xFF);
        int end = d.Count;

        // The packfile: header, three section headers, class names, data.
        const int namesAt = 0xD0;
        int dataAt = namesAt + names.Count;
        var pack = new List<byte>();
        pack.AddRange(BitConverter.GetBytes(Magic0)); pack.AddRange(BitConverter.GetBytes(Magic1));
        pack.AddRange(BitConverter.GetBytes(0)); pack.AddRange(BitConverter.GetBytes(8));          // user tag, file version
        pack.AddRange([4, 1, 0, 1]);                                                                // layout: pointers 4 bytes, little-endian, reuse padding, empty base class
        pack.AddRange(BitConverter.GetBytes(3)); pack.AddRange(BitConverter.GetBytes(2)); pack.AddRange(BitConverter.GetBytes(0));  // sections, contents section and offset
        pack.AddRange(BitConverter.GetBytes(0)); pack.AddRange(BitConverter.GetBytes(nameAt["hkpListShape"]));                     // contents class name
        byte[] version = new byte[16];
        Encoding.ASCII.GetBytes("hk_2010.2.0-r1").CopyTo(version, 0);
        version[15] = 0xFF;
        pack.AddRange(version);
        pack.AddRange(BitConverter.GetBytes(0)); pack.AddRange(BitConverter.GetBytes(-1));
        void SectionHeader(string name, int start, int length, int localAt, int globalOffset, int virtualOffset, int endOffset)
        {
            byte[] n = new byte[20];
            Encoding.ASCII.GetBytes(name).CopyTo(n, 0);
            n[19] = 0xFF;
            pack.AddRange(n);
            foreach (int v in new[] { start, localAt, globalOffset, virtualOffset, endOffset, endOffset, endOffset }) pack.AddRange(BitConverter.GetBytes(v));
            _ = length;
        }
        SectionHeader("__classnames__", namesAt, names.Count, names.Count, names.Count, names.Count, names.Count);
        SectionHeader("__types__", dataAt, 0, 0, 0, 0, 0);
        SectionHeader("__data__", dataAt, dataLength, dataLength, globalAt, virtualAt, end);
        pack.AddRange(names);
        pack.AddRange(d);

        var result = new List<byte>();
        result.AddRange(Encoding.ASCII.GetBytes("MCKH"));
        result.AddRange(BitConverter.GetBytes(5));
        result.AddRange(BitConverter.GetBytes(16 + pack.Count));
        result.AddRange(BitConverter.GetBytes(shapes.Count));
        result.AddRange(pack);
        return [.. result];
    }

    static void Pad(List<byte> bytes, byte with) { while (bytes.Count % 16 != 0) bytes.Add(with); }
}
