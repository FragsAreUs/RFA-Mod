using System.Text;

namespace RfaFormats.Meshes;

/// <summary>
/// Armageddon's Havok 2010 packfiles (hk_2010.2.0-r1) as its models' collision sections hold them,
/// written from nothing: the header (magic, user tag 0, file version 8, layout 4/1/0/1, three
/// sections, the contents in __data__ at 0 of the class named), the three section headers
/// (__classnames__ from 0xD0, __types__ empty, __data__ after the class names: where each starts,
/// then its local, global and virtual fixup tables' and its end's offsets), the class names (each its
/// signature, 0x09, the name, a NUL), and __data__ (the objects, padded with zeros to 16, then the
/// local fixups (from, to), the global fixups (from, 2, to) and the virtual fixups (an object, 0, its
/// class's name), each table padded with 0xFF to 16). Each collision section is "MCKH", 5, its length
/// and its shape count, then the packfile.
/// </summary>
static class Havok2010Packfile
{
    public const uint Magic0 = 0x57E0E057, Magic1 = 0x10C0C010;
    const int NamesAt = 0xD0;

    /// <summary>A packfile of these objects (their data at their offsets in <paramref name="data"/>), its contents the object of class <paramref name="contents"/> at 0.</summary>
    public static byte[] Write(IReadOnlyList<(uint Signature, string Name)> classes, string contents, IReadOnlyList<byte> data,
                               IEnumerable<(int From, int To)> locals, IEnumerable<(int From, int To)> globals, IEnumerable<(int At, string Class)> objects)
    {
        // Class names.
        var names = new List<byte>();
        var nameAt = new Dictionary<string, int>();
        foreach (var (signature, name) in classes)
        {
            names.AddRange(BitConverter.GetBytes(signature));
            names.Add(0x09);
            nameAt[name] = names.Count;
            names.AddRange(Encoding.ASCII.GetBytes(name));
            names.Add(0);
        }
        Pad(names, 0xFF);

        // __data__ and its fixup tables.
        var d = new List<byte>(data);
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
        int dataAt = NamesAt + names.Count;
        var pack = new List<byte>();
        pack.AddRange(BitConverter.GetBytes(Magic0)); pack.AddRange(BitConverter.GetBytes(Magic1));
        pack.AddRange(BitConverter.GetBytes(0)); pack.AddRange(BitConverter.GetBytes(8));          // user tag, file version
        pack.AddRange([4, 1, 0, 1]);                                                                // layout: pointers 4 bytes, little-endian, reuse padding, empty base class
        pack.AddRange(BitConverter.GetBytes(3)); pack.AddRange(BitConverter.GetBytes(2)); pack.AddRange(BitConverter.GetBytes(0));  // sections, contents section and offset
        pack.AddRange(BitConverter.GetBytes(0)); pack.AddRange(BitConverter.GetBytes(nameAt[contents]));                           // contents class name
        byte[] version = new byte[16];
        Encoding.ASCII.GetBytes("hk_2010.2.0-r1").CopyTo(version, 0);
        version[15] = 0xFF;
        pack.AddRange(version);
        pack.AddRange(BitConverter.GetBytes(0)); pack.AddRange(BitConverter.GetBytes(-1));
        void SectionHeader(string name, int start, int localAt, int globalOffset, int virtualOffset, int endOffset)
        {
            byte[] n = new byte[20];
            Encoding.ASCII.GetBytes(name).CopyTo(n, 0);
            n[19] = 0xFF;
            pack.AddRange(n);
            foreach (int v in new[] { start, localAt, globalOffset, virtualOffset, endOffset, endOffset, endOffset }) pack.AddRange(BitConverter.GetBytes(v));
        }
        SectionHeader("__classnames__", NamesAt, names.Count, names.Count, names.Count, names.Count);
        SectionHeader("__types__", dataAt, 0, 0, 0, 0);
        SectionHeader("__data__", dataAt, dataLength, globalAt, virtualAt, end);
        pack.AddRange(names);
        pack.AddRange(d);
        return [.. pack];
    }

    /// <summary>A model's collision section: "MCKH", 5, its length, the shape count, the packfile.</summary>
    public static byte[] Section(byte[] packfile, int shapes)
    {
        var result = new List<byte>();
        result.AddRange(Encoding.ASCII.GetBytes("MCKH"));
        result.AddRange(BitConverter.GetBytes(5));
        result.AddRange(BitConverter.GetBytes(16 + packfile.Length));
        result.AddRange(BitConverter.GetBytes(shapes));
        result.AddRange(packfile);
        return [.. result];
    }

    public static void Pad(List<byte> bytes, byte with) { while (bytes.Count % 16 != 0) bytes.Add(with); }
}
