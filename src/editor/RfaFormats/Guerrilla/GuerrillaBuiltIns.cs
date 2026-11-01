using RfaFormats.Textures;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Textures Red Faction: Guerrilla's engine has built in rather than as files (neither game ships
/// them). Its zones' ground materials name them for the layers they leave unused: "misc-white" for
/// the colour and "flat-normalmap" for the normal (in 11 of its 102 zones, e.g. terr01_02_11). The
/// world builder makes them from nothing, under the names it gives them.
/// </summary>
public static class GuerrillaBuiltIns
{
    /// <summary>A built-in texture's files (.cvbm_pc, .gvbm_pc) under <paramref name="name"/>, or null if Guerrilla has no such built-in.</summary>
    public static (string Name, byte[] Data)[]? Files(string texture, string name) => texture.ToLowerInvariant() switch
    {
        "misc-white" => Flat(name, [255, 255, 255, 255], 0),
        // Guerrilla's normal maps hold the normal's x and y in red and green (128 is 0) and blue 0:
        // their means are 127-130, 127-128, 0.
        "flat-normalmap" => Flat(name, [128, 128, 0, 255], PegFile.DataFlag),
        _ => null,
    };

    /// <summary>A texture of one colour (red, green, blue, alpha): 64 × 64, DXT1 with its levels down to 4 × 4.</summary>
    internal static (string Name, byte[] Data)[] Flat(string name, byte[] colour, ushort flags)
    {
        const int size = 64, levels = 5;
        var image = new RgbaImage(size, size);
        for (int i = 0; i < image.Pixels.Length; i++) image.Pixels[i] = colour[i % 4];
        return [(name + ".cvbm_pc", PegFile.SingleHeader(name + ".tga", size, size, TextureFormat.Dxt1, levels, flags)),
                (name + ".gvbm_pc", TextureCodec.EncodeWithMips(TextureFormat.Dxt1, image, levels))];
    }
}
