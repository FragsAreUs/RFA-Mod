using System.Text;

namespace RfaFormats.Interface;

/// <summary>
/// An interface document (.vint_doc, binary): a tree of elements (groups, bitmaps, texts, ...) with
/// their properties, the animations that play on them, and the script that drives them. Both Red
/// Faction: Armageddon and Red Faction: Guerrilla use this format, at the same version; the game
/// reads it at 0xC53010 (a file not starting with the signature is read as XML instead).
///
///   0x00  signature 0x3027 (u32), 0 (u32), version 1 (u16), a float, metadata count (u32),
///         dependency count (u32), string table offset (u32), root element count (u16),
///         animation count (u16)
///   0x1E  dependencies: type (u8; 0 = a texture file the document needs), name (string)
///         metadata: key, value (strings); lua_script_file is the document's script
///         elements, depth first: name, type (strings), child count (u16), u8, offset of its base
///         properties (u32), variant count (u8), per variant a resolution name (string) and the
///         offset of its properties (u32); properties are value type (u8, 0 ends the list), name
///         hash (u32, <see cref="Hash"/>), value (types 1-3 and 4, a string, are 4 bytes, 5 is one
///         byte, 6 three floats, 7 two floats)
///         then the animations
///   string table: count (u32), size of the text (u32), count offsets (u32 each), the text
///         (NUL-terminated strings)
///
/// Every reference to a string is its index in the table (a u32), and every offset is from the
/// start of the file. The table comes last, so strings can be added and references pointed at them
/// without moving anything else. Unknown property names are skipped by their value type; an
/// unknown element type stops the game reading the document.
/// </summary>
public sealed class VintDocument
{
    public const uint Signature = 0x3027;
    public const ushort Version = 1;
    public const byte StringValue = 4;

    readonly byte[] body;        // everything before the string table, with references changed in place
    readonly List<string> strings;

    public IReadOnlyList<string> Strings => strings;
    public IReadOnlyList<VintReference> Dependencies { get; }
    public IReadOnlyList<(VintReference Key, VintReference Value)> Metadata { get; }
    public IReadOnlyList<VintElement> Roots { get; }

    VintDocument(byte[] body, List<string> strings, List<VintReference> dependencies,
                 List<(VintReference, VintReference)> metadata, List<VintElement> roots)
    {
        this.body = body;
        this.strings = strings;
        Dependencies = dependencies;
        Metadata = metadata;
        Roots = roots;
    }

    public static bool IsBinary(byte[] data) => data.Length >= 4 && BitConverter.ToUInt32(data, 0) == Signature;

    public static VintDocument Read(byte[] data)
    {
        if (!IsBinary(data) || data.Length < 0x1E) throw new InvalidDataException("not a binary interface document (.vint_doc)");
        ushort version = BitConverter.ToUInt16(data, 8);
        if (version != Version) throw new InvalidDataException($"interface document version {version}; only version 1 is known");
        int metadataCount = (int)BitConverter.ToUInt32(data, 0x0E);
        int dependencyCount = (int)BitConverter.ToUInt32(data, 0x12);
        int stringsAt = (int)BitConverter.ToUInt32(data, 0x16);
        int rootCount = BitConverter.ToUInt16(data, 0x1A);

        int count = (int)BitConverter.ToUInt32(data, stringsAt);
        int textAt = stringsAt + 8 + 4 * count;
        var strings = new List<string>(count);
        for (int i = 0; i < count; i++)
        {
            int start = textAt + (int)BitConverter.ToUInt32(data, stringsAt + 8 + 4 * i);
            int end = Array.IndexOf(data, (byte)0, start);
            strings.Add(Encoding.Latin1.GetString(data, start, end - start));
        }

        VintReference Reference(int at) => new(at, (int)BitConverter.ToUInt32(data, at));
        int p = 0x1E;
        var dependencies = new List<VintReference>();
        for (int i = 0; i < dependencyCount; i++, p += 5)
            dependencies.Add(Reference(p + 1) with { Kind = data[p] });
        var metadata = new List<(VintReference, VintReference)>();
        for (int i = 0; i < metadataCount; i++, p += 8)
            metadata.Add((Reference(p), Reference(p + 4)));

        // Reads a property list into `properties`; returns where it ends.
        int ReadProperties(int at, string? resolution, List<VintProperty> properties)
        {
            while (true)
            {
                byte type = data[at++];
                if (type == 0) return at;
                uint hash = BitConverter.ToUInt32(data, at);
                at += 4;
                properties.Add(new VintProperty(type, hash, at, resolution,
                                                type == StringValue ? (int)BitConverter.ToUInt32(data, at) : -1));
                at += type switch
                {
                    1 or 2 or 3 or 4 => 4,
                    5 => 1,
                    6 => 12,
                    7 => 8,
                    _ => throw new InvalidDataException($"unknown property value type {type} at 0x{at - 5:X}"),
                };
            }
        }
        VintElement Element()
        {
            string name = strings[(int)BitConverter.ToUInt32(data, p)];
            string type = strings[(int)BitConverter.ToUInt32(data, p + 4)];
            int childCount = BitConverter.ToUInt16(data, p + 8);
            int baseAt = (int)BitConverter.ToUInt32(data, p + 11);
            int variantCount = data[p + 15];
            p += 16;
            var properties = new List<VintProperty>();
            for (int v = 0; v < variantCount; v++, p += 8)
                ReadProperties((int)BitConverter.ToUInt32(data, p + 4), strings[(int)BitConverter.ToUInt32(data, p)], properties);
            p = ReadProperties(baseAt, null, properties);  // the base properties come last; the children follow them
            var children = new List<VintElement>(childCount);
            for (int c = 0; c < childCount; c++) children.Add(Element());
            return new VintElement(name, type, properties, children);
        }
        var roots = new List<VintElement>(rootCount);
        for (int i = 0; i < rootCount; i++) roots.Add(Element());

        return new VintDocument(data.AsSpan(0, stringsAt).ToArray(), strings, dependencies, metadata, roots);
    }

    /// <summary>The name hash of a property: CRC-32 (reflected, polynomial 0xEDB88320) started at 0, with no final inversion.</summary>
    public static uint Hash(string name)
    {
        uint crc = 0;
        foreach (byte b in Encoding.Latin1.GetBytes(name))
        {
            crc ^= b;
            for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xEDB88320u : 0);
        }
        return crc;
    }

    /// <summary>Every element, depth first.</summary>
    public IEnumerable<VintElement> Elements()
    {
        var stack = new Stack<VintElement>(Roots.Reverse());
        while (stack.Count > 0)
        {
            VintElement element = stack.Pop();
            yield return element;
            for (int i = element.Children.Count - 1; i >= 0; i--) stack.Push(element.Children[i]);
        }
    }

    public string Text(VintReference reference) => strings[CurrentIndex(reference)];

    /// <summary>Points a reference (a dependency, a metadata value, a string property's value) at this text, added to the table if it isn't there.</summary>
    public void Point(VintReference reference, string text)
    {
        int index = strings.IndexOf(text);
        if (index < 0)
        {
            index = strings.Count;
            strings.Add(text);
        }
        BitConverter.TryWriteBytes(body.AsSpan(reference.At), (uint)index);
    }

    int CurrentIndex(VintReference reference) => (int)BitConverter.ToUInt32(body, reference.At);

    /// <summary>The document as it is now: the body with its references, then the string table.</summary>
    public byte[] ToBytes()
    {
        var text = new MemoryStream();
        var offsets = new List<uint>();
        foreach (string s in strings)
        {
            offsets.Add((uint)text.Length);
            text.Write(Encoding.Latin1.GetBytes(s));
            text.WriteByte(0);
        }
        var output = new MemoryStream();
        output.Write(body);
        output.Write(BitConverter.GetBytes((uint)strings.Count));
        output.Write(BitConverter.GetBytes((uint)text.Length));
        foreach (uint offset in offsets) output.Write(BitConverter.GetBytes(offset));
        text.WriteTo(output);
        return output.ToArray();
    }
}

/// <summary>A string reference in the document: where its index is stored (<paramref name="At"/>) and the index it had when read.</summary>
public sealed record VintReference(int At, int Index)
{
    public byte Kind { get; init; }  // for a dependency: 0 = a texture file
}

/// <param name="ValueAt">Where the value is stored in the file.</param>
/// <param name="Resolution">The resolution this value is for, or null for the base value.</param>
/// <param name="StringIndex">For a string value, the index it had when read; -1 otherwise.</param>
public sealed record VintProperty(byte ValueType, uint NameHash, int ValueAt, string? Resolution, int StringIndex)
{
    public VintReference AsReference => new(ValueAt, StringIndex);
}

public sealed record VintElement(string Name, string Type, IReadOnlyList<VintProperty> Properties, IReadOnlyList<VintElement> Children);
