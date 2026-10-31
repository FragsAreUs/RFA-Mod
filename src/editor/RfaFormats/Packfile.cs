using System.IO.Compression;
using System.Text;

namespace RfaFormats;

/// <summary>
/// A packfile: a .vpp_pc archive in the game folder, or a .str2_pc container inside one. Both use
/// format version 6:
///
///   0x000  2 KB header: magic 0x51890ACE, version 6, ..., flags at 0x14C, and at 0x154 the file
///          count, packfile size, directory size, names size, data size, compressed data size
///   0x800  directory: 24 bytes per file (name offset, 0, data offset, size, compressed size, 0)
///   then   the file names (NUL-separated), then the data; each block starts on a 2 KB boundary
///
/// The data can be stored three ways (see <see cref="PackfileFlags"/>): plain; compressed, with
/// each file its own zlib stream starting on a 2 KB boundary; or compressed and condensed, with the
/// whole data block one zlib stream. In a condensed one the files sit at their directory offsets
/// (one after another in .str2_pc containers, on 16-byte boundaries in table.vpp_pc), and the
/// stream is flushed after each file; the directory's compressed size of a file is the stream bytes
/// up to that flush. The game's zlib streams are never finished, so they are read by size, not to
/// their end.
///
/// Red Faction: Guerrilla uses version 3 of this format, with a name hash in each directory entry.
/// </summary>
public sealed class Packfile
{
    public const uint Magic = 0x51890ACE;
    public const uint Version = 6;
    const int HeaderSize = 0x800;
    const int EntrySize = 24;

    readonly Func<long, int, byte[]> readBytes;  // reads from wherever the packfile lives
    readonly byte[] header;
    readonly byte[] directory;
    readonly byte[] names;
    byte[]? condensedData;                        // the whole data block, once a condensed file was read

    public string Name { get; }
    public PackfileFlags Flags { get; }
    public long DataStart { get; }
    public uint DataSize { get; }          // uncompressed size of the data block
    public uint CompressedDataSize { get; } // 0xFFFFFFFF if not compressed
    public IReadOnlyList<PackfileEntry> Entries { get; }

    public bool IsCompressed => Flags.HasFlag(PackfileFlags.Compressed);
    public bool IsCondensed => Flags.HasFlag(PackfileFlags.Condensed);

    Packfile(string name, Func<long, int, byte[]> readBytes)
    {
        Name = name;
        this.readBytes = readBytes;

        header = readBytes(0, HeaderSize);
        if (header.Length < HeaderSize || U32(header, 0) != Magic)
            throw new InvalidDataException($"{name} is not a packfile");
        if (U32(header, 4) != Version)
            throw new InvalidDataException($"{name} is packfile version {U32(header, 4)}; only version 6 (Armageddon) is supported");

        Flags = (PackfileFlags)U32(header, 0x14C);
        int count = (int)U32(header, 0x154);
        uint directorySize = U32(header, 0x15C);
        uint namesSize = U32(header, 0x160);
        DataSize = U32(header, 0x164);
        CompressedDataSize = U32(header, 0x168);

        long namesStart = HeaderSize + Align(directorySize, 0x800);
        DataStart = namesStart + Align(namesSize, 0x800);
        directory = readBytes(HeaderSize, (int)directorySize);
        names = readBytes(namesStart, (int)namesSize);

        // Where each file's bytes are stored. Uncompressed and condensed packfiles say so in the
        // directory; for compressed ones the streams simply follow each other on 2 KB boundaries.
        var entries = new List<PackfileEntry>(count);
        long storedAt = DataStart;
        for (int i = 0; i < count; i++)
        {
            int at = i * EntrySize;
            string fileName = CString(names, (int)U32(directory, at));
            uint offset = U32(directory, at + 8);
            uint size = U32(directory, at + 12);
            uint compressedSize = U32(directory, at + 16);
            if (IsCompressed && !IsCondensed)
            {
                entries.Add(new PackfileEntry(fileName, offset, size, compressedSize, storedAt));
                storedAt += Align(compressedSize, 0x800);
            }
            else
            {
                entries.Add(new PackfileEntry(fileName, offset, size, compressedSize, DataStart + offset));
            }
        }
        Entries = entries;
    }

    /// <summary>Opens a .vpp_pc archive on disk. Only the header, directory and names are read now.</summary>
    public static Packfile Open(string path)
    {
        return new Packfile(Path.GetFileName(path), (offset, length) =>
        {
            using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            file.Position = offset;
            var bytes = new byte[length];
            file.ReadExactly(bytes);
            return bytes;
        });
    }

    /// <summary>Opens a packfile already in memory, such as a .str2_pc read out of an archive.</summary>
    public static Packfile FromBytes(string name, byte[] data)
    {
        return new Packfile(name, (offset, length) => data.AsSpan((int)offset, length).ToArray());
    }

    /// <summary>The bytes of one file, decompressed.</summary>
    public byte[] Read(PackfileEntry entry)
    {
        if (!IsCompressed)
            return readBytes(entry.StoredAt, (int)entry.Size);

        if (!IsCondensed)
            return Inflate(readBytes(entry.StoredAt, (int)entry.CompressedSize), (int)entry.Size);

        condensedData ??= Inflate(readBytes(DataStart, (int)CompressedDataSize), (int)DataSize);
        return condensedData.AsSpan((int)entry.Offset, (int)entry.Size).ToArray();
    }

    public PackfileEntry? Find(string fileName) =>
        Entries.FirstOrDefault(e => string.Equals(e.Name, fileName, StringComparison.OrdinalIgnoreCase));

    /// <summary>
    /// A copy of this packfile with some files replaced (by name), written the way the game's own
    /// tools wrote it: same header, same file order and names; compressed and condensed packfiles
    /// get one zlib stream flushed after each file. If every file keeps its size, every file also
    /// keeps its offset. Used for .str2_pc containers; packfiles compressed file by file (some
    /// .vpp_pc archives) can't be written.
    /// </summary>
    public byte[] Rebuild(IReadOnlyDictionary<string, byte[]> replacements)
    {
        if (IsCompressed && !IsCondensed)
            throw new NotSupportedException($"{Name} is compressed file by file, which can't be written yet");

        var lookup = new Dictionary<string, byte[]>(replacements, StringComparer.OrdinalIgnoreCase);
        var files = Entries.Select(e => lookup.TryGetValue(e.Name, out byte[]? replacement) ? replacement : Read(e)).ToList();
        int count = files.Count;

        // Where each file goes in the data block.
        var offsets = new uint[count];
        if (files.Select(f => (uint)f.Length).SequenceEqual(Entries.Select(e => e.Size)))
        {
            for (int i = 0; i < count; i++) offsets[i] = Entries[i].Offset;
        }
        else
        {
            uint alignment = Entries.Skip(1).Select((e, i) => e.Offset == Entries[i].Offset + Entries[i].Size).All(x => x) ? 1u
                           : IsCompressed ? 16u : 0x800u;
            uint at = 0;
            for (int i = 0; i < count; i++)
            {
                offsets[i] = at;
                at = (uint)Align(at + files[i].Length, alignment);
            }
        }
        uint dataSize = count == 0 ? 0 : offsets[^1] + (uint)files[^1].Length;
        var plain = new byte[dataSize];
        for (int i = 0; i < count; i++) files[i].CopyTo(plain, offsets[i]);

        // The data block as stored, and each file's stored size.
        var stored = new uint[count];
        byte[] block;
        if (IsCompressed)
        {
            var output = new MemoryStream();
            var zlib = new ZLibStream(output, CompressionLevel.SmallestSize, leaveOpen: true);
            long before = 0;
            for (int i = 0; i < count; i++)
            {
                uint end = i + 1 < count ? offsets[i + 1] : dataSize;
                zlib.Write(plain, (int)offsets[i], (int)(end - offsets[i]));
                zlib.Flush();  // a sync flush: everything so far can be decompressed
                stored[i] = (uint)(output.Length - before);
                before = output.Length;
            }
            // Taken before the stream is closed, which would add an end marker the game's own don't have.
            block = output.ToArray();
            zlib.Dispose();
        }
        else
        {
            block = plain;
            Array.Fill(stored, 0xFFFFFFFFu);
        }

        byte[] newDirectory = (byte[])directory.Clone();
        for (int i = 0; i < count; i++)
        {
            BitConverter.TryWriteBytes(newDirectory.AsSpan(i * EntrySize + 8), offsets[i]);
            BitConverter.TryWriteBytes(newDirectory.AsSpan(i * EntrySize + 12), (uint)files[i].Length);
            BitConverter.TryWriteBytes(newDirectory.AsSpan(i * EntrySize + 16), stored[i]);
        }
        byte[] newHeader = (byte[])header.Clone();
        BitConverter.TryWriteBytes(newHeader.AsSpan(0x158), (uint)(DataStart + block.Length));
        BitConverter.TryWriteBytes(newHeader.AsSpan(0x164), dataSize);
        BitConverter.TryWriteBytes(newHeader.AsSpan(0x168), IsCompressed ? (uint)block.Length : 0xFFFFFFFFu);

        var packfile = new byte[DataStart + block.Length];
        newHeader.CopyTo(packfile, 0);
        newDirectory.CopyTo(packfile, HeaderSize);
        names.CopyTo(packfile, HeaderSize + Align(directory.Length, 0x800));
        block.CopyTo(packfile, DataStart);
        return packfile;
    }

    /// <summary>
    /// Decompresses exactly <paramref name="size"/> bytes. The game's streams have no end marker,
    /// so reading stops at the expected size instead of waiting for one.
    /// </summary>
    static byte[] Inflate(byte[] compressed, int size)
    {
        var output = new byte[size];
        using var zlib = new ZLibStream(new MemoryStream(compressed), CompressionMode.Decompress);
        int done = 0;
        while (done < size)
        {
            int read = zlib.Read(output, done, size - done);
            if (read == 0)
                throw new InvalidDataException($"compressed data ended after {done} of {size} bytes");
            done += read;
        }
        return output;
    }

    static uint U32(byte[] bytes, int at) => BitConverter.ToUInt32(bytes, at);

    static long Align(long value, long alignment) => (value + alignment - 1) / alignment * alignment;

    static string CString(byte[] bytes, int at)
    {
        int end = Array.IndexOf(bytes, (byte)0, at);
        return Encoding.Latin1.GetString(bytes, at, (end < 0 ? bytes.Length : end) - at);
    }
}

[Flags]
public enum PackfileFlags : uint
{
    None = 0,
    Compressed = 1,  // file data is zlib-compressed
    Condensed = 2,   // ...as one stream for all files, instead of one per file
    // The game also sets 0x4800 on compressed packfiles; what it means is unknown.
}

/// <summary>One file inside a packfile.</summary>
/// <param name="Offset">Where the file starts in the (uncompressed) data block.</param>
/// <param name="CompressedSize">0xFFFFFFFF if the packfile isn't compressed.</param>
/// <param name="StoredAt">Where its stored bytes start in the packfile.</param>
public sealed record PackfileEntry(string Name, uint Offset, uint Size, uint CompressedSize, long StoredAt)
{
    public string Extension => Path.GetExtension(Name).TrimStart('.').ToLowerInvariant();
    public bool IsContainer => Extension == "str2_pc";
}
