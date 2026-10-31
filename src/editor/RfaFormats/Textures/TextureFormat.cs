namespace RfaFormats.Textures;

/// <summary>Pixel formats of the game's textures; the numbers are the game's own.</summary>
/// <remarks>
/// Used in the game: Dxt1 (18,680 textures), Dxt5 (1,612), Argb8888 (292), Dxt3 (47), Rgb565 (23).
/// The DXT formats ("block compression") store each 4×4 block of pixels in 8 or 16 bytes.
/// </remarks>
public enum TextureFormat : ushort
{
    Dxt1 = 400,      // 8 bytes per 4×4 block: colour, optionally with on/off transparency
    Dxt3 = 401,      // 16 bytes per block: colour plus 4-bit alpha
    Dxt5 = 402,      // 16 bytes per block: colour plus smooth alpha
    Rgb565 = 403,    // 2 bytes per pixel
    Argb1555 = 404,  // 2 bytes per pixel
    Argb4444 = 405,  // 2 bytes per pixel
    Rgb888 = 406,    // 3 bytes per pixel, stored blue, green, red
    Argb8888 = 407,  // 4 bytes per pixel, stored blue, green, red, alpha
    A8 = 410,        // 1 byte per pixel, alpha only
}

public static class TextureFormats
{
    public static bool IsKnown(this TextureFormat format) => Enum.IsDefined(format);

    public static bool IsBlockCompressed(this TextureFormat format) =>
        format is TextureFormat.Dxt1 or TextureFormat.Dxt3 or TextureFormat.Dxt5;

    public static string Describe(this TextureFormat format) => format switch
    {
        TextureFormat.Dxt1 => "DXT1 (BC1)",
        TextureFormat.Dxt3 => "DXT3 (BC2)",
        TextureFormat.Dxt5 => "DXT5 (BC3)",
        TextureFormat.Rgb565 => "16-bit RGB 565",
        TextureFormat.Argb1555 => "16-bit ARGB 1555",
        TextureFormat.Argb4444 => "16-bit ARGB 4444",
        TextureFormat.Rgb888 => "24-bit RGB",
        TextureFormat.Argb8888 => "32-bit ARGB",
        TextureFormat.A8 => "8-bit alpha",
        _ => $"unknown format {(int)format}",
    };

    /// <summary>Bytes for one mip level of the given size.</summary>
    public static int LevelSize(this TextureFormat format, int width, int height)
    {
        width = Math.Max(1, width);
        height = Math.Max(1, height);
        int blocks = (width + 3) / 4 * ((height + 3) / 4);
        return format switch
        {
            TextureFormat.Dxt1 => blocks * 8,
            TextureFormat.Dxt3 or TextureFormat.Dxt5 => blocks * 16,
            TextureFormat.Rgb565 or TextureFormat.Argb1555 or TextureFormat.Argb4444 => width * height * 2,
            TextureFormat.Rgb888 => width * height * 3,
            TextureFormat.Argb8888 => width * height * 4,
            TextureFormat.A8 => width * height,
            _ => throw new NotSupportedException($"texture format {(int)format} is unknown"),
        };
    }

    /// <summary>Bytes for all mip levels: the full size, then each level half the size of the one before.</summary>
    public static int TotalSize(this TextureFormat format, int width, int height, int mipLevels)
    {
        int total = 0;
        for (int level = 0; level < mipLevels; level++)
            total += format.LevelSize(width >> level, height >> level);
        return total;
    }
}
