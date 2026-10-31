using System.Text;

namespace RfaFormats;

/// <summary>
/// An asset list (.asm_pc): for each .str2_pc container the game streams, how big it is and what
/// is inside, so the game can set memory aside before reading it. Version 5, the same as Red
/// Faction: Guerrilla:
///
///   signature 0xBEEFFEED (u32), version 5 (u16), container count (u16), then per container:
///     name length (u16) + name (without .str2_pc), type (u8), flags (u16), primitive count (u16),
///     data offset (u32: where the container's data block starts), size count (u32),
///     compressed size (u32: the container's compressed data block), sizes (i32 each), then per
///     primitive: name length (u16) + name, type (u8), allocator (u8), flags (u8),
///     split index (u8), header size (i32: the file's size), data size (i32: its g-file's size)
///
/// A container rebuilt with different contents needs its entry here to match, or the game sets
/// aside the wrong amount.
/// </summary>
public sealed class AsmFile
{
    public const uint Signature = 0xBEEFFEED;
    public const ushort Version = 5;

    public List<AsmContainer> Containers { get; } = [];

    public static AsmFile Read(byte[] data)
    {
        var r = new BinaryReader(new MemoryStream(data));
        if (r.ReadUInt32() != Signature) throw new InvalidDataException("not an asset list (.asm_pc)");
        ushort version = r.ReadUInt16();
        if (version != Version) throw new InvalidDataException($"asset list version {version}; only version 5 is known");
        var file = new AsmFile();
        int count = r.ReadUInt16();
        for (int i = 0; i < count; i++)
        {
            var container = new AsmContainer
            {
                Name = ReadName(r),
                Type = r.ReadByte(),
                Flags = r.ReadUInt16(),
            };
            int primitives = r.ReadUInt16();
            container.DataOffset = r.ReadUInt32();
            int sizes = (int)r.ReadUInt32();
            container.CompressedSize = r.ReadUInt32();
            for (int s = 0; s < sizes; s++) container.Sizes.Add(r.ReadInt32());
            for (int p = 0; p < primitives; p++)
                container.Primitives.Add(new AsmPrimitive
                {
                    Name = ReadName(r),
                    Type = r.ReadByte(),
                    Allocator = r.ReadByte(),
                    Flags = r.ReadByte(),
                    SplitIndex = r.ReadByte(),
                    HeaderSize = r.ReadInt32(),
                    DataSize = r.ReadInt32(),
                });
            file.Containers.Add(container);
        }
        if (r.BaseStream.Position != data.Length)
            throw new InvalidDataException("the asset list has data after its last container");
        return file;
    }

    public byte[] ToBytes()
    {
        var output = new MemoryStream();
        var w = new BinaryWriter(output);
        w.Write(Signature);
        w.Write(Version);
        w.Write((ushort)Containers.Count);
        foreach (AsmContainer container in Containers)
        {
            WriteName(w, container.Name);
            w.Write(container.Type);
            w.Write(container.Flags);
            w.Write((ushort)container.Primitives.Count);
            w.Write(container.DataOffset);
            w.Write((uint)container.Sizes.Count);
            w.Write(container.CompressedSize);
            foreach (int size in container.Sizes) w.Write(size);
            foreach (AsmPrimitive primitive in container.Primitives)
            {
                WriteName(w, primitive.Name);
                w.Write(primitive.Type);
                w.Write(primitive.Allocator);
                w.Write(primitive.Flags);
                w.Write(primitive.SplitIndex);
                w.Write(primitive.HeaderSize);
                w.Write(primitive.DataSize);
            }
        }
        return output.ToArray();
    }

    static string ReadName(BinaryReader r) => Encoding.Latin1.GetString(r.ReadBytes(r.ReadUInt16()));

    static void WriteName(BinaryWriter w, string name)
    {
        w.Write((ushort)name.Length);
        w.Write(Encoding.Latin1.GetBytes(name));
    }
}

public sealed class AsmContainer
{
    public string Name { get; set; } = "";       // the .str2_pc file name without its extension
    public byte Type { get; set; }
    public ushort Flags { get; set; }
    public uint DataOffset { get; set; }         // where the container's data block starts
    public uint CompressedSize { get; set; }     // size of the container's (compressed) data block
    public List<int> Sizes { get; } = [];
    public List<AsmPrimitive> Primitives { get; } = [];

    public string FileName => Name + ".str2_pc";
}

public sealed class AsmPrimitive
{
    public string Name { get; set; } = "";  // the file in the container, e.g. rock.cpeg_pc
    public byte Type { get; set; }
    public byte Allocator { get; set; }
    public byte Flags { get; set; }
    public byte SplitIndex { get; set; }
    public int HeaderSize { get; set; }     // size of that file
    public int DataSize { get; set; }       // size of the file that goes with it (rock.gpeg_pc)
}
