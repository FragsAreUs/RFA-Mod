using System.Text;

namespace RfaFormats.Textures;

/// <summary>
/// A texture file, which comes as a pair: a small header file (.cpeg_pc, or .cvbm_pc for single
/// images) listing the textures, and a data file (.gpeg_pc / .gvbm_pc) with their pixels. The
/// game keeps the first in normal memory and loads the second straight into video memory.
///
/// Header file (format "GEKV" version 10, the same as Red Faction: Guerrilla):
///   0x00  "GEKV", version 10 (u16), platform 0 (u16), size of this file, size of the data file,
///         bitmap count (u16), 0 (u16), texture count (u16), alignment (u16, always 16)
///   0x18  48 bytes per texture: data offset, width, height, format, source width, animation tile
///         width and height, frame count (1), flags, name offset, source height, fps (1), mip
///         levels, data size, then 16 bytes the game fills in at run time
///   then  the texture names, NUL-terminated, in order
/// Data file: each texture's mip levels, largest first, one after another; textures start on
/// 16-byte boundaries.
/// </summary>
public sealed class PegFile
{
    public const uint Magic = 0x564B4547;  // "GEKV"
    public const ushort Version = 10;
    const int HeaderSize = 0x18;
    const int EntrySize = 48;

    readonly byte[] header;
    readonly byte[] data;

    public IReadOnlyList<PegTexture> Textures { get; }

    PegFile(byte[] header, byte[] data, List<PegTexture> textures)
    {
        this.header = header;
        this.data = data;
        Textures = textures;
    }

    public static PegFile Read(byte[] header, byte[] data)
    {
        if (header.Length < HeaderSize || BitConverter.ToUInt32(header, 0) != Magic)
            throw new InvalidDataException("not a texture header file (GEKV)");
        ushort version = BitConverter.ToUInt16(header, 4);
        if (version != Version)
            throw new InvalidDataException($"texture format version {version}; only version 10 is known");
        int count = BitConverter.ToUInt16(header, 0x10);

        int nameAt = HeaderSize + count * EntrySize;
        var textures = new List<PegTexture>(count);
        for (int i = 0; i < count; i++)
        {
            int at = HeaderSize + i * EntrySize;
            int nameEnd = Array.IndexOf(header, (byte)0, nameAt);
            if (nameEnd < 0) nameEnd = header.Length;
            string name = Encoding.Latin1.GetString(header, nameAt, nameEnd - nameAt);
            nameAt = nameEnd + 1;
            textures.Add(new PegTexture(
                Index: i,
                Name: name,
                Width: BitConverter.ToUInt16(header, at + 4),
                Height: BitConverter.ToUInt16(header, at + 6),
                Format: (TextureFormat)BitConverter.ToUInt16(header, at + 8),
                Flags: BitConverter.ToUInt16(header, at + 18),
                MipLevels: header[at + 27],
                DataOffset: (int)BitConverter.ToUInt32(header, at),
                DataSize: (int)BitConverter.ToUInt32(header, at + 28)));
        }
        return new PegFile(header, data, textures);
    }

    /// <summary>
    /// A texture's flags: 0x200 on maps that hold data rather than colours (normal maps, the
    /// ground's blend maps: ruin01_alpha00, ter_sur_blder_nm), 0 on colour maps (ruin01comb, *_di).
    /// </summary>
    public const ushort DataFlag = 0x200;

    /// <summary>
    /// A header file for one texture (a .cvbm_pc) from nothing, as the game's own single-image ones
    /// are (checked byte for byte against ruin01_alpha00, ruin01comb, ter_sur_blder_nm and
    /// ter_ice_rock_dtlonly_di): "GEKV", version 10, platform 0, its own size, the data file's size
    /// (the texture's data up to a multiple of 16), 1 bitmap, 0, 1 texture, alignment 16; the
    /// texture at data offset 0: its width and height, format, source width 0, animation tile 1 ×
    /// 1, 1 frame, its flags, name offset 0, source height 0, 1 frame a second, its mip levels and
    /// data size, 16 zero bytes the game fills in; then its name.
    /// </summary>
    /// <param name="name">The name the game knows it by, with ".tga" ("rfg_white.tga").</param>
    public static byte[] SingleHeader(string name, int width, int height, TextureFormat format, int mipLevels, ushort flags)
    {
        int dataSize = 0;
        for (int level = 0; level < mipLevels; level++) dataSize += format.LevelSize(Math.Max(1, width >> level), Math.Max(1, height >> level));
        byte[] nameBytes = Encoding.Latin1.GetBytes(name + "\0");
        var stream = new MemoryStream();
        var w = new BinaryWriter(stream);
        w.Write(Magic); w.Write(Version); w.Write((ushort)0);
        w.Write(HeaderSize + EntrySize + nameBytes.Length); w.Write((dataSize + 15) & ~15);
        w.Write((ushort)1); w.Write((ushort)0); w.Write((ushort)1); w.Write((ushort)16);
        w.Write(0); w.Write((ushort)width); w.Write((ushort)height); w.Write((ushort)format);
        w.Write((ushort)0); w.Write((ushort)1); w.Write((ushort)1); w.Write((ushort)1); w.Write(flags);
        w.Write(0); w.Write((ushort)0); w.Write((byte)1); w.Write((byte)mipLevels); w.Write(dataSize);
        w.Write(new byte[16]);
        w.Write(nameBytes);
        return stream.ToArray();
    }

    /// <summary>The names of the textures in a header file, without needing its data file.</summary>
    public static IEnumerable<string> TextureNames(byte[] header)
    {
        if (header.Length < HeaderSize || BitConverter.ToUInt32(header, 0) != Magic) yield break;
        int count = BitConverter.ToUInt16(header, 0x10);
        int at = HeaderSize + count * EntrySize;
        for (int i = 0; i < count && at < header.Length; i++)
        {
            int end = Array.IndexOf(header, (byte)0, at);
            if (end < 0) end = header.Length;
            yield return Encoding.Latin1.GetString(header, at, end - at);
            at = end + 1;
        }
    }

    /// <summary>The data file name that goes with a header file: x.cpeg_pc → x.gpeg_pc, x.cvbm_pc → x.gvbm_pc.</summary>
    public static string? DataFileName(string headerFileName)
    {
        string extension = Path.GetExtension(headerFileName).ToLowerInvariant();
        return extension is ".cpeg_pc" or ".cvbm_pc" ? Path.ChangeExtension(headerFileName, "g" + extension[2..]) : null;
    }

    /// <summary>The header file name that goes with a data file: x.gpeg_pc → x.cpeg_pc.</summary>
    public static string? HeaderFileName(string dataFileName)
    {
        string extension = Path.GetExtension(dataFileName).ToLowerInvariant();
        return extension is ".gpeg_pc" or ".gvbm_pc" ? Path.ChangeExtension(dataFileName, "c" + extension[2..]) : null;
    }

    public byte[] HeaderBytes => (byte[])header.Clone();
    public byte[] DataBytes => (byte[])data.Clone();

    /// <summary>All mip levels of one texture, as stored.</summary>
    public byte[] TextureData(PegTexture texture) => data.AsSpan(texture.DataOffset, texture.DataSize).ToArray();

    /// <summary>
    /// As much of one texture's mip levels as the data file holds: all of them, unless the file is
    /// shorter than its header says.
    /// </summary>
    public byte[] TextureDataThere(PegTexture texture)
    {
        int from = Math.Min(texture.DataOffset, data.Length);
        return data.AsSpan(from, Math.Min(texture.DataSize, data.Length - from)).ToArray();
    }

    /// <summary>
    /// The first mip level no bigger than <paramref name="maxSize"/> on either side (or the
    /// smallest there is) as plain pixels: for previews, where full size isn't needed.
    /// </summary>
    public RgbaImage Decode(PegTexture texture, int maxSize = int.MaxValue)
    {
        int level = 0, offset = texture.DataOffset;
        while (level + 1 < texture.MipLevels && Math.Max(texture.Width >> level, texture.Height >> level) > maxSize)
        {
            offset += texture.Format.LevelSize(texture.Width >> level, texture.Height >> level);
            level++;
        }
        int width = Math.Max(1, texture.Width >> level), height = Math.Max(1, texture.Height >> level);
        return TextureCodec.Decode(texture.Format, width, height, data.AsSpan(offset, texture.Format.LevelSize(width, height)));
    }

    /// <summary>
    /// A copy of the data file with one texture's pixels replaced. The new data must be the same
    /// size (same width, height, format and mip levels), so the header file and everything that
    /// records file sizes stay valid.
    /// </summary>
    public byte[] WithTextureData(PegTexture texture, byte[] newData)
    {
        if (newData.Length != texture.DataSize)
            throw new ArgumentException($"{texture.Name} needs {texture.DataSize:N0} bytes of data, not {newData.Length:N0}");
        byte[] copy = DataBytes;
        newData.CopyTo(copy, texture.DataOffset);
        return copy;
    }
}

/// <param name="DataOffset">Where its mip levels start in the data file.</param>
/// <param name="DataSize">All its mip levels together, in bytes.</param>
public sealed record PegTexture(int Index, string Name, int Width, int Height, TextureFormat Format, ushort Flags,
                                int MipLevels, int DataOffset, int DataSize)
{
    public string Description => $"{Width} × {Height}, {Format.Describe()}, {MipLevels} mip level{(MipLevels == 1 ? "" : "s")}";
}
