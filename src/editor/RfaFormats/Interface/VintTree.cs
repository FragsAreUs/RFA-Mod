using System.Text;

namespace RfaFormats.Interface;

/// <summary>
/// An interface document (.vint_doc) as a tree that can be changed in shape: elements added, copied and deleted,
/// properties added and removed, and written out anew (<see cref="ToBytes"/>), every offset worked out again. The game's
/// 115 documents all lay a node out the same way, which this writes back byte for byte:
/// <list type="bullet">
/// <item>a node: name, type (string indices, u32 each), child count (u16), a byte (0 in all 3,821), the offset of its
/// base properties (u32), its variant count (u8) and per variant a resolution (string index) and the offset of its
/// properties (u32 each); then each variant's property list, then the base list, then its children;</item>
/// <item>a property list: per property a value type (u8; 0 ends the list), a name hash (u32) and the value (types 1-4
/// four bytes, 5 one, 6 twelve, 7 eight; 4 is a string index);</item>
/// <item>the animations (in 80 documents) come after the elements and are nodes of the same kind (an animation and its
/// tweens), with the same absolute offsets.</item>
/// </list>
/// The header (0x1E bytes), the dependencies and the metadata are kept as they are (their string indices too), and the
/// string table is written last, with any strings added at its end. See <see cref="VintDocument"/> for the header.
/// </summary>
public sealed class VintTree
{
    readonly byte[] head;          // the header, the dependencies and the metadata, as read
    public List<string> Strings { get; }
    public List<VintRawNode> Roots { get; }
    public List<VintRawNode> Animations { get; }

    VintTree(byte[] head, List<string> strings, List<VintRawNode> roots, List<VintRawNode> animations)
    {
        this.head = head;
        Strings = strings;
        Roots = roots;
        Animations = animations;
    }

    public static VintTree Read(byte[] data)
    {
        if (!VintDocument.IsBinary(data) || data.Length < 0x1E) throw new InvalidDataException("not a binary interface document (.vint_doc)");
        if (BitConverter.ToUInt16(data, 8) != VintDocument.Version) throw new InvalidDataException("only interface documents of version 1 are known");
        int metadataCount = (int)BitConverter.ToUInt32(data, 0x0E), dependencyCount = (int)BitConverter.ToUInt32(data, 0x12);
        int stringsAt = (int)BitConverter.ToUInt32(data, 0x16);
        int rootCount = BitConverter.ToUInt16(data, 0x1A), animationCount = BitConverter.ToUInt16(data, 0x1C);
        int count = (int)BitConverter.ToUInt32(data, stringsAt);
        int textAt = stringsAt + 8 + 4 * count;
        var strings = new List<string>(count);
        for (int i = 0; i < count; i++)
        {
            int start = textAt + (int)BitConverter.ToUInt32(data, stringsAt + 8 + 4 * i);
            strings.Add(Encoding.Latin1.GetString(data, start, Array.IndexOf(data, (byte)0, start) - start));
        }
        int at = 0x1E + 5 * dependencyCount + 8 * metadataCount;
        byte[] head = data[..at];

        List<VintRawProperty> Properties(int p)
        {
            var list = new List<VintRawProperty>();
            while (true)
            {
                byte type = data[p++];
                if (type == 0) return list;
                int size = VintRawProperty.SizeOf(type);
                list.Add(new VintRawProperty(type, BitConverter.ToUInt32(data, p), data[(p + 4)..(p + 4 + size)]));
                p += 4 + size;
            }
        }
        int ListEnd(int p)
        {
            while (true)
            {
                byte type = data[p++];
                if (type == 0) return p;
                p += 4 + VintRawProperty.SizeOf(type);
            }
        }
        VintRawNode Node()
        {
            var node = new VintRawNode
            {
                Name = (int)BitConverter.ToUInt32(data, at),
                Type = (int)BitConverter.ToUInt32(data, at + 4),
                Flag = data[at + 10],
            };
            int children = BitConverter.ToUInt16(data, at + 8);
            int baseAt = (int)BitConverter.ToUInt32(data, at + 11);
            int variants = data[at + 15];
            for (int v = 0; v < variants; v++)
                node.Variants.Add(((int)BitConverter.ToUInt32(data, at + 16 + 8 * v), Properties((int)BitConverter.ToUInt32(data, at + 20 + 8 * v))));
            node.Base.AddRange(Properties(baseAt));
            at = ListEnd(baseAt);  // its children follow its base list
            for (int c = 0; c < children; c++) node.Children.Add(Node());
            return node;
        }
        var roots = new List<VintRawNode>(rootCount);
        for (int i = 0; i < rootCount; i++) roots.Add(Node());
        var animations = new List<VintRawNode>(animationCount);
        for (int i = 0; i < animationCount; i++) animations.Add(Node());
        if (at != stringsAt) throw new InvalidDataException($"the document's nodes end at 0x{at:X}, not at its string table (0x{stringsAt:X})");
        return new VintTree(head, strings, roots, animations);
    }

    /// <summary>The index of a string, added to the table if it isn't there.</summary>
    public int StringIndex(string text)
    {
        int index = Strings.IndexOf(text);
        if (index >= 0) return index;
        Strings.Add(text);
        return Strings.Count - 1;
    }

    public string Text(int index) => index >= 0 && index < Strings.Count ? Strings[index] : "";

    /// <summary>The metadata's value for a key (lua_script_file: the document's script), or null.</summary>
    public string? MetadataValue(string key)
    {
        int metadataCount = (int)BitConverter.ToUInt32(head, 0x0E), dependencyCount = (int)BitConverter.ToUInt32(head, 0x12);
        int at = 0x1E + 5 * dependencyCount;
        for (int i = 0; i < metadataCount; i++, at += 8)
            if (Text((int)BitConverter.ToUInt32(head, at)).Equals(key, StringComparison.OrdinalIgnoreCase)) return Text((int)BitConverter.ToUInt32(head, at + 4));
        return null;
    }

    /// <summary>The texture files the document needs (its dependencies of type 0).</summary>
    public List<string> TextureFiles()
    {
        int dependencyCount = (int)BitConverter.ToUInt32(head, 0x12);
        var files = new List<string>();
        for (int i = 0, at = 0x1E; i < dependencyCount; i++, at += 5)
            if (head[at] == 0) files.Add(Text((int)BitConverter.ToUInt32(head, at + 1)));
        return files;
    }

    /// <summary>The document as it is now: every node and property list written anew, then the string table.</summary>
    public byte[] ToBytes()
    {
        var output = new MemoryStream();
        output.Write(head);
        void Write(VintRawNode node)
        {
            long headerAt = output.Position;
            output.Write(BitConverter.GetBytes((uint)node.Name));
            output.Write(BitConverter.GetBytes((uint)node.Type));
            output.Write(BitConverter.GetBytes((ushort)node.Children.Count));
            output.WriteByte(node.Flag);
            output.Write(BitConverter.GetBytes(0u));  // the base list's offset, filled in below
            output.WriteByte((byte)node.Variants.Count);
            foreach (var (resolution, _) in node.Variants)
            {
                output.Write(BitConverter.GetBytes((uint)resolution));
                output.Write(BitConverter.GetBytes(0u));
            }
            var offsets = new List<uint>();
            foreach (var (_, properties) in node.Variants)
            {
                offsets.Add((uint)output.Position);
                WriteList(properties);
            }
            uint baseAt = (uint)output.Position;
            WriteList(node.Base);
            long end = output.Position;
            output.Position = headerAt + 11;
            output.Write(BitConverter.GetBytes(baseAt));
            for (int v = 0; v < offsets.Count; v++)
            {
                output.Position = headerAt + 20 + 8 * v;
                output.Write(BitConverter.GetBytes(offsets[v]));
            }
            output.Position = end;
            foreach (VintRawNode child in node.Children) Write(child);
        }
        void WriteList(List<VintRawProperty> properties)
        {
            foreach (VintRawProperty property in properties)
            {
                output.WriteByte(property.Type);
                output.Write(BitConverter.GetBytes(property.Hash));
                output.Write(property.Value);
            }
            output.WriteByte(0);
        }
        foreach (VintRawNode root in Roots) Write(root);
        foreach (VintRawNode animation in Animations) Write(animation);
        uint stringsAt = (uint)output.Position;

        var text = new MemoryStream();
        var offsets = new List<uint>();
        foreach (string s in Strings)
        {
            offsets.Add((uint)text.Length);
            text.Write(Encoding.Latin1.GetBytes(s));
            text.WriteByte(0);
        }
        output.Write(BitConverter.GetBytes((uint)Strings.Count));
        output.Write(BitConverter.GetBytes((uint)text.Length));
        foreach (uint offset in offsets) output.Write(BitConverter.GetBytes(offset));
        text.WriteTo(output);

        byte[] bytes = output.ToArray();
        BitConverter.TryWriteBytes(bytes.AsSpan(0x16), stringsAt);
        BitConverter.TryWriteBytes(bytes.AsSpan(0x1A), (ushort)Roots.Count);
        BitConverter.TryWriteBytes(bytes.AsSpan(0x1C), (ushort)Animations.Count);
        return bytes;
    }
}

/// <summary>A node of a document (an element, or an animation or tween): its name and type as string indices, its
/// base properties, its per-resolution variants, and its children.</summary>
public sealed class VintRawNode
{
    public int Name { get; set; }
    public int Type { get; set; }
    public byte Flag { get; set; }
    public List<VintRawProperty> Base { get; } = [];
    public List<(int Resolution, List<VintRawProperty> Properties)> Variants { get; } = [];
    public List<VintRawNode> Children { get; } = [];

    /// <summary>A copy of it and everything under it (the same strings).</summary>
    public VintRawNode Copy()
    {
        var copy = new VintRawNode { Name = Name, Type = Type, Flag = Flag };
        copy.Base.AddRange(Base.Select(p => p.Copy()));
        foreach (var (resolution, properties) in Variants) copy.Variants.Add((resolution, [.. properties.Select(p => p.Copy())]));
        foreach (VintRawNode child in Children) copy.Children.Add(child.Copy());
        return copy;
    }
}

/// <summary>A property as stored: its value type, its name's hash and its value's bytes.</summary>
public sealed class VintRawProperty(byte type, uint hash, byte[] value)
{
    public byte Type { get; } = type;
    public uint Hash { get; } = hash;
    public byte[] Value { get; set; } = value;

    public static int SizeOf(byte type) => type switch
    {
        1 or 2 or 3 or 4 => 4,
        5 => 1,
        6 => 12,
        7 => 8,
        _ => throw new InvalidDataException($"unknown property value type {type}"),
    };

    public VintRawProperty Copy() => new(Type, Hash, (byte[])Value.Clone());

    /// <summary>Its value as numbers (null for a string: its index is <see cref="StringIndex"/>).</summary>
    public float[]? Numbers => Type switch
    {
        1 => [BitConverter.ToInt32(Value, 0)],
        2 => [BitConverter.ToUInt32(Value, 0)],
        3 => [BitConverter.ToSingle(Value, 0)],
        5 => [Value[0]],
        6 => [BitConverter.ToSingle(Value, 0), BitConverter.ToSingle(Value, 4), BitConverter.ToSingle(Value, 8)],
        7 => [BitConverter.ToSingle(Value, 0), BitConverter.ToSingle(Value, 4)],
        _ => null,
    };

    public int StringIndex => Type == VintDocument.StringValue ? (int)BitConverter.ToUInt32(Value, 0) : -1;

    /// <summary>The value bytes for numbers of a type.</summary>
    public static byte[] Encode(byte type, IReadOnlyList<float> numbers)
    {
        var bytes = new byte[SizeOf(type)];
        switch (type)
        {
            case 1: BitConverter.TryWriteBytes(bytes, (int)MathF.Round(numbers[0])); break;
            case 2: BitConverter.TryWriteBytes(bytes, (uint)Math.Max(0, MathF.Round(numbers[0]))); break;
            case 3: BitConverter.TryWriteBytes(bytes, numbers[0]); break;
            case 5: bytes[0] = (byte)Math.Clamp(MathF.Round(numbers[0]), 0, 255); break;
            case 6: for (int i = 0; i < 3; i++) BitConverter.TryWriteBytes(bytes.AsSpan(4 * i), numbers[i]); break;
            case 7: for (int i = 0; i < 2; i++) BitConverter.TryWriteBytes(bytes.AsSpan(4 * i), numbers[i]); break;
            case 4: throw new ArgumentException("a string's value is its index", nameof(type));
        }
        return bytes;
    }
}
