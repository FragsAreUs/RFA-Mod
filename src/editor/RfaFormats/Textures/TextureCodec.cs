namespace RfaFormats.Textures;

/// <summary>An image as plain pixels: 4 bytes per pixel in the order red, green, blue, alpha.</summary>
public sealed class RgbaImage
{
    public int Width { get; }
    public int Height { get; }
    public byte[] Pixels { get; }

    public RgbaImage(int width, int height, byte[]? pixels = null)
    {
        Width = width;
        Height = height;
        Pixels = pixels ?? new byte[width * height * 4];
        if (Pixels.Length != width * height * 4) throw new ArgumentException("pixel data doesn't match the size");
    }

    /// <summary>The image at half the width and height (at least 1), each pixel the average of the 2×2 it covers.</summary>
    public RgbaImage Half()
    {
        int width = Math.Max(1, Width / 2), height = Math.Max(1, Height / 2);
        var half = new RgbaImage(width, height);
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
                for (int c = 0; c < 4; c++)
                {
                    int x0 = Math.Min(x * 2, Width - 1), x1 = Math.Min(x * 2 + 1, Width - 1);
                    int y0 = Math.Min(y * 2, Height - 1), y1 = Math.Min(y * 2 + 1, Height - 1);
                    int sum = Pixels[(y0 * Width + x0) * 4 + c] + Pixels[(y0 * Width + x1) * 4 + c] +
                              Pixels[(y1 * Width + x0) * 4 + c] + Pixels[(y1 * Width + x1) * 4 + c];
                    half.Pixels[(y * width + x) * 4 + c] = (byte)((sum + 2) / 4);
                }
        return half;
    }
}

/// <summary>
/// Turns texture data into plain pixels and back, for every format the game uses. The DXT formats
/// work on 4×4 blocks: each block stores two colours and, per pixel, which mix of the two to use
/// (and, for DXT3/DXT5, the alpha separately).
/// </summary>
public static class TextureCodec
{
    // ---- Decoding ------------------------------------------------------------------------------

    /// <summary>The largest mip level of a texture as plain pixels.</summary>
    public static RgbaImage Decode(TextureFormat format, int width, int height, ReadOnlySpan<byte> data)
    {
        var image = new RgbaImage(width, height);
        switch (format)
        {
            case TextureFormat.Dxt1:
            case TextureFormat.Dxt3:
            case TextureFormat.Dxt5:
                int blockSize = format == TextureFormat.Dxt1 ? 8 : 16;
                int blocksWide = (width + 3) / 4, blocksHigh = (height + 3) / 4;
                Span<byte> block = stackalloc byte[64];
                for (int by = 0; by < blocksHigh; by++)
                    for (int bx = 0; bx < blocksWide; bx++)
                    {
                        ReadOnlySpan<byte> stored = data.Slice((by * blocksWide + bx) * blockSize, blockSize);
                        DecodeBlock(format, stored, block);
                        for (int py = 0; py < 4; py++)
                            for (int px = 0; px < 4; px++)
                            {
                                int x = bx * 4 + px, y = by * 4 + py;
                                if (x < width && y < height)
                                    block.Slice((py * 4 + px) * 4, 4).CopyTo(image.Pixels.AsSpan((y * width + x) * 4, 4));
                            }
                    }
                break;
            default:
                for (int i = 0; i < width * height; i++)
                    DecodePixel(format, data, i, image.Pixels.AsSpan(i * 4, 4));
                break;
        }
        return image;
    }

    /// <summary>One 4×4 block into 16 RGBA pixels.</summary>
    static void DecodeBlock(TextureFormat format, ReadOnlySpan<byte> stored, Span<byte> pixels)
    {
        ReadOnlySpan<byte> colourBlock = format == TextureFormat.Dxt1 ? stored : stored[8..];
        Span<byte> palette = stackalloc byte[16];
        ushort c0 = BitConverter.ToUInt16(colourBlock), c1 = BitConverter.ToUInt16(colourBlock[2..]);
        Expand565(c0, palette[0..4]);
        Expand565(c1, palette[4..8]);
        // DXT1 with the first colour not greater than the second means "three colours plus transparent".
        bool threeColours = format == TextureFormat.Dxt1 && c0 <= c1;
        for (int c = 0; c < 3; c++)
        {
            if (threeColours)
            {
                palette[8 + c] = (byte)((palette[c] + palette[4 + c]) / 2);
                palette[12 + c] = 0;
            }
            else
            {
                palette[8 + c] = (byte)((2 * palette[c] + palette[4 + c]) / 3);
                palette[12 + c] = (byte)((palette[c] + 2 * palette[4 + c]) / 3);
            }
        }
        palette[11] = 255;
        palette[15] = threeColours ? (byte)0 : (byte)255;

        uint indices = BitConverter.ToUInt32(colourBlock[4..]);
        for (int i = 0; i < 16; i++)
            palette.Slice((int)((indices >> (2 * i)) & 3) * 4, 4).CopyTo(pixels.Slice(i * 4, 4));

        if (format == TextureFormat.Dxt3)
        {
            // 4 bits of alpha per pixel.
            for (int i = 0; i < 16; i++)
            {
                int nibble = (stored[i / 2] >> (4 * (i % 2))) & 15;
                pixels[i * 4 + 3] = (byte)(nibble * 17);
            }
        }
        else if (format == TextureFormat.Dxt5)
        {
            Span<byte> alphas = stackalloc byte[8];
            AlphaPalette(stored[0], stored[1], alphas);
            ulong bits = 0;
            for (int b = 0; b < 6; b++) bits |= (ulong)stored[2 + b] << (8 * b);
            for (int i = 0; i < 16; i++)
                pixels[i * 4 + 3] = alphas[(int)((bits >> (3 * i)) & 7)];
        }
    }

    /// <summary>The 8 alpha values a DXT5 block can use, from its two stored ones.</summary>
    static void AlphaPalette(byte a0, byte a1, Span<byte> alphas)
    {
        alphas[0] = a0;
        alphas[1] = a1;
        if (a0 > a1)
        {
            for (int i = 1; i < 7; i++) alphas[1 + i] = (byte)(((7 - i) * a0 + i * a1) / 7);
        }
        else
        {
            for (int i = 1; i < 5; i++) alphas[1 + i] = (byte)(((5 - i) * a0 + i * a1) / 5);
            alphas[6] = 0;
            alphas[7] = 255;
        }
    }

    static void Expand565(ushort colour, Span<byte> rgba)
    {
        int r = (colour >> 11) & 31, g = (colour >> 5) & 63, b = colour & 31;
        rgba[0] = (byte)((r << 3) | (r >> 2));
        rgba[1] = (byte)((g << 2) | (g >> 4));
        rgba[2] = (byte)((b << 3) | (b >> 2));
        rgba[3] = 255;
    }

    static void DecodePixel(TextureFormat format, ReadOnlySpan<byte> data, int i, Span<byte> rgba)
    {
        switch (format)
        {
            case TextureFormat.Argb8888:
                rgba[0] = data[i * 4 + 2]; rgba[1] = data[i * 4 + 1]; rgba[2] = data[i * 4]; rgba[3] = data[i * 4 + 3];
                break;
            case TextureFormat.Rgb888:
                rgba[0] = data[i * 3 + 2]; rgba[1] = data[i * 3 + 1]; rgba[2] = data[i * 3]; rgba[3] = 255;
                break;
            case TextureFormat.Rgb565:
                Expand565(BitConverter.ToUInt16(data.Slice(i * 2, 2)), rgba);
                break;
            case TextureFormat.Argb1555:
            {
                int v = BitConverter.ToUInt16(data.Slice(i * 2, 2));
                rgba[0] = (byte)(((v >> 10) & 31) * 255 / 31); rgba[1] = (byte)(((v >> 5) & 31) * 255 / 31);
                rgba[2] = (byte)((v & 31) * 255 / 31); rgba[3] = (v & 0x8000) != 0 ? (byte)255 : (byte)0;
                break;
            }
            case TextureFormat.Argb4444:
            {
                int v = BitConverter.ToUInt16(data.Slice(i * 2, 2));
                rgba[0] = (byte)(((v >> 8) & 15) * 17); rgba[1] = (byte)(((v >> 4) & 15) * 17);
                rgba[2] = (byte)((v & 15) * 17); rgba[3] = (byte)(((v >> 12) & 15) * 17);
                break;
            }
            case TextureFormat.A8:
                rgba[0] = rgba[1] = rgba[2] = 255; rgba[3] = data[i];
                break;
            default:
                throw new NotSupportedException($"texture format {(int)format} is unknown");
        }
    }

    // ---- Encoding ------------------------------------------------------------------------------

    /// <summary>
    /// Encodes an image with all its mip levels, as the game stores them: the image itself, then
    /// halved again and again.
    /// </summary>
    public static byte[] EncodeWithMips(TextureFormat format, RgbaImage image, int mipLevels)
    {
        var output = new List<byte>(format.TotalSize(image.Width, image.Height, mipLevels));
        RgbaImage level = image;
        for (int i = 0; i < mipLevels; i++)
        {
            if (i > 0) level = level.Half();
            output.AddRange(Encode(format, level));
        }
        return [.. output];
    }

    /// <summary>One mip level.</summary>
    public static byte[] Encode(TextureFormat format, RgbaImage image)
    {
        int width = image.Width, height = image.Height;
        byte[] output = new byte[format.LevelSize(width, height)];
        if (!format.IsBlockCompressed())
        {
            for (int i = 0; i < width * height; i++)
                EncodePixel(format, image.Pixels.AsSpan(i * 4, 4), output, i);
            return output;
        }

        int blockSize = format == TextureFormat.Dxt1 ? 8 : 16;
        int blocksWide = (width + 3) / 4, blocksHigh = (height + 3) / 4;
        Span<byte> block = stackalloc byte[64];
        for (int by = 0; by < blocksHigh; by++)
            for (int bx = 0; bx < blocksWide; bx++)
            {
                // Pixels outside the image (when it isn't a multiple of 4) repeat the edge.
                for (int py = 0; py < 4; py++)
                    for (int px = 0; px < 4; px++)
                    {
                        int x = Math.Min(bx * 4 + px, width - 1), y = Math.Min(by * 4 + py, height - 1);
                        image.Pixels.AsSpan((y * width + x) * 4, 4).CopyTo(block.Slice((py * 4 + px) * 4, 4));
                    }
                EncodeBlock(format, block, output.AsSpan((by * blocksWide + bx) * blockSize, blockSize));
            }
        return output;
    }

    static void EncodeBlock(TextureFormat format, ReadOnlySpan<byte> pixels, Span<byte> stored)
    {
        switch (format)
        {
            case TextureFormat.Dxt1:
                bool hasTransparency = false;
                for (int i = 0; i < 16; i++) hasTransparency |= pixels[i * 4 + 3] < 128;
                EncodeColourBlock(pixels, stored, transparency: hasTransparency);
                break;
            case TextureFormat.Dxt3:
                stored[..8].Clear();
                for (int i = 0; i < 16; i++)
                    stored[i / 2] |= (byte)(((pixels[i * 4 + 3] + 8) / 17) << (4 * (i % 2)));
                EncodeColourBlock(pixels, stored[8..], transparency: false);
                break;
            case TextureFormat.Dxt5:
                EncodeAlphaBlock(pixels, stored[..8]);
                EncodeColourBlock(pixels, stored[8..], transparency: false);
                break;
        }
    }

    /// <summary>
    /// The colour half of a block: the two stored colours are the pixels furthest apart along the
    /// direction the block's colours vary most (found from their spread), and each pixel then gets
    /// the nearest of the four mixes. With <paramref name="transparency"/>, pixels with alpha below
    /// 128 become fully transparent and the rest use three mixes.
    /// </summary>
    static void EncodeColourBlock(ReadOnlySpan<byte> pixels, Span<byte> stored, bool transparency)
    {
        // Average colour and how the colours spread around it (covariance), of the visible pixels.
        Span<float> mean = stackalloc float[3];
        int counted = 0;
        for (int i = 0; i < 16; i++)
        {
            if (transparency && pixels[i * 4 + 3] < 128) continue;
            for (int c = 0; c < 3; c++) mean[c] += pixels[i * 4 + c];
            counted++;
        }
        if (counted == 0)
        {
            // All transparent: colours don't matter, every index is 3 ("transparent" in 3-colour mode).
            BitConverter.TryWriteBytes(stored, (ushort)0);
            BitConverter.TryWriteBytes(stored[2..], (ushort)0xFFFF);
            BitConverter.TryWriteBytes(stored[4..], 0xFFFFFFFFu);
            return;
        }
        for (int c = 0; c < 3; c++) mean[c] /= counted;

        Span<float> spread = stackalloc float[9];
        for (int i = 0; i < 16; i++)
        {
            if (transparency && pixels[i * 4 + 3] < 128) continue;
            for (int a = 0; a < 3; a++)
                for (int b = 0; b < 3; b++)
                    spread[a * 3 + b] += (pixels[i * 4 + a] - mean[a]) * (pixels[i * 4 + b] - mean[b]);
        }

        // The main direction: repeatedly multiplying a vector by the spread matrix turns it towards it.
        Span<float> axis = [1f, 1f, 1f];
        for (int step = 0; step < 8; step++)
        {
            float x = spread[0] * axis[0] + spread[1] * axis[1] + spread[2] * axis[2];
            float y = spread[3] * axis[0] + spread[4] * axis[1] + spread[5] * axis[2];
            float z = spread[6] * axis[0] + spread[7] * axis[1] + spread[8] * axis[2];
            float length = MathF.Max(MathF.Max(MathF.Abs(x), MathF.Abs(y)), MathF.Abs(z));
            if (length < 1e-6f) break;
            axis[0] = x / length; axis[1] = y / length; axis[2] = z / length;
        }

        // The two pixels furthest apart along that direction become the stored colours.
        int lowest = -1, highest = -1;
        float lowestAt = float.MaxValue, highestAt = float.MinValue;
        for (int i = 0; i < 16; i++)
        {
            if (transparency && pixels[i * 4 + 3] < 128) continue;
            float at = pixels[i * 4] * axis[0] + pixels[i * 4 + 1] * axis[1] + pixels[i * 4 + 2] * axis[2];
            if (at < lowestAt) { lowestAt = at; lowest = i; }
            if (at > highestAt) { highestAt = at; highest = i; }
        }
        ushort c0 = To565(pixels.Slice(highest * 4, 3)), c1 = To565(pixels.Slice(lowest * 4, 3));

        // Four mixes need the first stored colour to be the greater number, three mixes the smaller.
        if (transparency ? c0 > c1 : c0 < c1) (c0, c1) = (c1, c0);
        Span<byte> palette = stackalloc byte[16];
        Expand565(c0, palette[0..4]);
        Expand565(c1, palette[4..8]);
        int choices = transparency || c0 == c1 ? 3 : 4;
        for (int c = 0; c < 3; c++)
        {
            if (choices == 3) palette[8 + c] = (byte)((palette[c] + palette[4 + c]) / 2);
            else
            {
                palette[8 + c] = (byte)((2 * palette[c] + palette[4 + c]) / 3);
                palette[12 + c] = (byte)((palette[c] + 2 * palette[4 + c]) / 3);
            }
        }

        uint indices = 0;
        for (int i = 0; i < 16; i++)
        {
            int best = 0;
            if (transparency && pixels[i * 4 + 3] < 128) best = 3;
            else
            {
                int bestDistance = int.MaxValue;
                for (int choice = 0; choice < choices; choice++)
                {
                    int distance = 0;
                    for (int c = 0; c < 3; c++)
                    {
                        int d = pixels[i * 4 + c] - palette[choice * 4 + c];
                        distance += d * d;
                    }
                    if (distance < bestDistance) { bestDistance = distance; best = choice; }
                }
            }
            indices |= (uint)best << (2 * i);
        }
        BitConverter.TryWriteBytes(stored, c0);
        BitConverter.TryWriteBytes(stored[2..], c1);
        BitConverter.TryWriteBytes(stored[4..], indices);
    }

    /// <summary>The alpha half of a DXT5 block: the highest and lowest alpha, and per pixel the nearest of 8 steps between them.</summary>
    static void EncodeAlphaBlock(ReadOnlySpan<byte> pixels, Span<byte> stored)
    {
        byte high = 0, low = 255;
        for (int i = 0; i < 16; i++)
        {
            high = Math.Max(high, pixels[i * 4 + 3]);
            low = Math.Min(low, pixels[i * 4 + 3]);
        }
        stored[0] = high;
        stored[1] = low;
        Span<byte> alphas = stackalloc byte[8];
        AlphaPalette(high, low, alphas);
        ulong bits = 0;
        for (int i = 0; i < 16; i++)
        {
            int best = 0, bestDistance = int.MaxValue;
            for (int choice = 0; choice < 8; choice++)
            {
                int distance = Math.Abs(pixels[i * 4 + 3] - alphas[choice]);
                if (distance < bestDistance) { bestDistance = distance; best = choice; }
            }
            bits |= (ulong)best << (3 * i);
        }
        for (int b = 0; b < 6; b++) stored[2 + b] = (byte)(bits >> (8 * b));
    }

    static ushort To565(ReadOnlySpan<byte> rgb) =>
        (ushort)(((rgb[0] * 31 + 127) / 255 << 11) | ((rgb[1] * 63 + 127) / 255 << 5) | ((rgb[2] * 31 + 127) / 255));

    static void EncodePixel(TextureFormat format, ReadOnlySpan<byte> rgba, byte[] output, int i)
    {
        switch (format)
        {
            case TextureFormat.Argb8888:
                output[i * 4] = rgba[2]; output[i * 4 + 1] = rgba[1]; output[i * 4 + 2] = rgba[0]; output[i * 4 + 3] = rgba[3];
                break;
            case TextureFormat.Rgb888:
                output[i * 3] = rgba[2]; output[i * 3 + 1] = rgba[1]; output[i * 3 + 2] = rgba[0];
                break;
            case TextureFormat.Rgb565:
                BitConverter.TryWriteBytes(output.AsSpan(i * 2), To565(rgba));
                break;
            case TextureFormat.Argb1555:
                BitConverter.TryWriteBytes(output.AsSpan(i * 2), (ushort)((rgba[3] >= 128 ? 0x8000 : 0) |
                    ((rgba[0] * 31 + 127) / 255 << 10) | ((rgba[1] * 31 + 127) / 255 << 5) | ((rgba[2] * 31 + 127) / 255)));
                break;
            case TextureFormat.Argb4444:
                BitConverter.TryWriteBytes(output.AsSpan(i * 2), (ushort)(((rgba[3] + 8) / 17 << 12) |
                    ((rgba[0] + 8) / 17 << 8) | ((rgba[1] + 8) / 17 << 4) | ((rgba[2] + 8) / 17)));
                break;
            case TextureFormat.A8:
                output[i] = rgba[3];
                break;
            default:
                throw new NotSupportedException($"texture format {(int)format} is unknown");
        }
    }
}
