using System.Text;

namespace RfaFormats.Textures;

/// <summary>
/// DDS files, the usual way to hand textures to image editors (Paint.NET, GIMP, Photoshop with a
/// plug-in, ...). A DDS file keeps the exact texture format and its mip levels, so a texture exported
/// and imported again without changes comes back identical.
///
/// Layout: "DDS ", a 124-byte header (size, flags, height, width, pitch, depth, mip count, a pixel
/// format block, capabilities), an extra 20-byte "DX10" header in newer files, then the mip levels.
/// </summary>
public sealed class DdsImage
{
    public TextureFormat Format { get; }
    public int Width { get; }
    public int Height { get; }
    public int MipLevels { get; }
    public byte[] Data { get; }  // all mip levels, largest first

    public DdsImage(TextureFormat format, int width, int height, int mipLevels, byte[] data)
    {
        Format = format;
        Width = width;
        Height = height;
        MipLevels = mipLevels;
        Data = data;
    }

    public RgbaImage ToRgba() => TextureCodec.Decode(Format, Width, Height, Data);

    // Header flags and pixel format bits from Microsoft's DDS documentation.
    const uint Caps = 0x1, HeightFlag = 0x2, WidthFlag = 0x4, Pitch = 0x8, PixelFormatFlag = 0x1000,
               MipMapCount = 0x20000, LinearSize = 0x80000;
    const uint AlphaPixels = 0x1, Alpha = 0x2, FourCc = 0x4, Rgb = 0x40;
    const uint CapsComplex = 0x8, CapsTexture = 0x1000, CapsMipMap = 0x400000;

    public byte[] ToBytes()
    {
        var output = new MemoryStream();
        var w = new BinaryWriter(output);
        w.Write(Encoding.ASCII.GetBytes("DDS "));
        bool compressed = Format.IsBlockCompressed();
        w.Write(124u);
        w.Write(Caps | HeightFlag | WidthFlag | PixelFormatFlag | (MipLevels > 1 ? MipMapCount : 0) |
                (compressed ? LinearSize : Pitch));
        w.Write((uint)Height);
        w.Write((uint)Width);
        w.Write((uint)(compressed ? Format.LevelSize(Width, Height) : Format.LevelSize(Width, 1)));
        w.Write(0u);  // depth
        w.Write((uint)MipLevels);
        for (int i = 0; i < 11; i++) w.Write(0u);

        // Pixel format block: either a four-letter code or the bit masks of each channel.
        (uint flags, string fourCc, uint bits, uint r, uint g, uint b, uint a) = Format switch
        {
            TextureFormat.Dxt1 => (FourCc, "DXT1", 0u, 0u, 0u, 0u, 0u),
            TextureFormat.Dxt3 => (FourCc, "DXT3", 0u, 0u, 0u, 0u, 0u),
            TextureFormat.Dxt5 => (FourCc, "DXT5", 0u, 0u, 0u, 0u, 0u),
            TextureFormat.Argb8888 => (Rgb | AlphaPixels, "", 32u, 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u),
            TextureFormat.Rgb888 => (Rgb, "", 24u, 0xFF0000u, 0x00FF00u, 0x0000FFu, 0u),
            TextureFormat.Rgb565 => (Rgb, "", 16u, 0xF800u, 0x07E0u, 0x001Fu, 0u),
            TextureFormat.Argb1555 => (Rgb | AlphaPixels, "", 16u, 0x7C00u, 0x03E0u, 0x001Fu, 0x8000u),
            TextureFormat.Argb4444 => (Rgb | AlphaPixels, "", 16u, 0x0F00u, 0x00F0u, 0x000Fu, 0xF000u),
            TextureFormat.A8 => (Alpha, "", 8u, 0u, 0u, 0u, 0xFFu),
            _ => throw new NotSupportedException($"texture format {(int)Format} can't be saved as DDS"),
        };
        w.Write(32u);
        w.Write(flags);
        w.Write(fourCc.Length == 4 ? BitConverter.ToUInt32(Encoding.ASCII.GetBytes(fourCc)) : 0u);
        w.Write(bits);
        w.Write(r);
        w.Write(g);
        w.Write(b);
        w.Write(a);

        w.Write(CapsTexture | (MipLevels > 1 ? CapsComplex | CapsMipMap : 0));
        for (int i = 0; i < 4; i++) w.Write(0u);  // caps 2-4, reserved
        w.Write(Data);
        return output.ToArray();
    }

    /// <summary>Reads a DDS file in any of the formats the game uses (including newer "DX10" files).</summary>
    public static DdsImage Read(byte[] file)
    {
        if (file.Length < 128 || Encoding.ASCII.GetString(file, 0, 4) != "DDS ")
            throw new InvalidDataException("not a DDS file");
        int height = (int)BitConverter.ToUInt32(file, 12);
        int width = (int)BitConverter.ToUInt32(file, 16);
        int mipLevels = Math.Max(1, (int)BitConverter.ToUInt32(file, 28));
        uint flags = BitConverter.ToUInt32(file, 80);
        string fourCc = Encoding.ASCII.GetString(file, 84, 4);
        uint bits = BitConverter.ToUInt32(file, 88);
        uint r = BitConverter.ToUInt32(file, 92), g = BitConverter.ToUInt32(file, 96), b = BitConverter.ToUInt32(file, 100),
             a = BitConverter.ToUInt32(file, 104);
        int dataStart = 128;
        bool swapRedBlue = false;

        TextureFormat format;
        if ((flags & FourCc) != 0 && fourCc == "DX10")
        {
            uint dxgi = BitConverter.ToUInt32(file, 128);
            dataStart = 148;
            (format, swapRedBlue) = dxgi switch
            {
                70 or 71 or 72 => (TextureFormat.Dxt1, false),
                73 or 74 or 75 => (TextureFormat.Dxt3, false),
                76 or 77 or 78 => (TextureFormat.Dxt5, false),
                87 or 88 or 90 or 91 => (TextureFormat.Argb8888, false),
                27 or 28 or 29 => (TextureFormat.Argb8888, true),  // stored red first: swapped below
                85 => (TextureFormat.Rgb565, false),
                86 => (TextureFormat.Argb1555, false),
                115 => (TextureFormat.Argb4444, false),
                65 => (TextureFormat.A8, false),
                _ => throw new NotSupportedException($"DDS format {dxgi} (DXGI) isn't one the game uses; save it as DXT1, DXT5 or 32-bit"),
            };
        }
        else if ((flags & FourCc) != 0)
        {
            format = fourCc switch
            {
                "DXT1" => TextureFormat.Dxt1,
                "DXT2" or "DXT3" => TextureFormat.Dxt3,
                "DXT4" or "DXT5" => TextureFormat.Dxt5,
                _ => throw new NotSupportedException($"DDS format {fourCc} isn't one the game uses; save it as DXT1, DXT5 or 32-bit"),
            };
        }
        else
        {
            (format, swapRedBlue) = (bits, r, g, b, a) switch
            {
                (32, 0x00FF0000, 0x0000FF00, 0x000000FF, _) => (TextureFormat.Argb8888, false),
                (32, 0x000000FF, 0x0000FF00, 0x00FF0000, _) => (TextureFormat.Argb8888, true),
                (24, 0xFF0000, 0x00FF00, 0x0000FF, _) => (TextureFormat.Rgb888, false),
                (16, 0xF800, 0x07E0, 0x001F, _) => (TextureFormat.Rgb565, false),
                (16, 0x7C00, 0x03E0, 0x001F, _) => (TextureFormat.Argb1555, false),
                (16, 0x0F00, 0x00F0, 0x000F, _) => (TextureFormat.Argb4444, false),
                (8, 0, 0, 0, 0xFF) => (TextureFormat.A8, false),
                _ => throw new NotSupportedException("this DDS pixel layout isn't one the game uses; save it as DXT1, DXT5 or 32-bit"),
            };
        }

        int size = Math.Min(format.TotalSize(width, height, mipLevels), file.Length - dataStart);
        byte[] data = file.AsSpan(dataStart, size).ToArray();
        if (swapRedBlue)
            for (int i = 0; i + 3 < data.Length; i += 4) (data[i], data[i + 2]) = (data[i + 2], data[i]);
        // Count only the mip levels that are complete.
        int complete = 0;
        for (int level = 0, used = 0; level < mipLevels; level++)
        {
            used += format.LevelSize(width >> level, height >> level);
            if (used <= data.Length) complete = level + 1;
        }
        return new DdsImage(format, width, height, Math.Max(1, complete), data);
    }
}
