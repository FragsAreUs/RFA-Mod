using RfaFormats.Textures;

namespace RfaFormats.Interface;

/// <summary>
/// The cloud picture of Guerrilla's fog of war, for the guerrilla_hud mod's world map: <see cref="FileName"/>, next to
/// our scripts, made from the player's own copy of Guerrilla (never shipped).
///
/// Guerrilla's world map covers the parts of the world you haven't been near with two layers of one cloud picture,
/// ui_fsmap_fog.tga (its world map document's textures, interface.vpp_pc &gt; rfg_ui_fullscreen.vint_doc2.str2_pc &gt;
/// interface-fsmap), drawn by its own shader (ui_fog_of_war) through a mask of the cells you have uncovered. Vint can't
/// draw that, so guerrilla_hud draws it itself with Direct3D 11 and reads the picture from this file: 32-bit
/// blue-green-red-alpha, with Guerrilla's own mip levels (one).
/// </summary>
public static class GuerrillaFogOfWar
{
    public const string FileName = "ui_fsmap_fog.dds";
    const string Container = "rfg_ui_fullscreen.vint_doc2.str2_pc";
    const string Peg = "interface-fsmap";
    const string Picture = "ui_fsmap_fog";

    /// <summary>Writes <see cref="FileName"/> into <paramref name="scriptsFolder"/>; returns what was done, a line.</summary>
    public static List<string> Build(string guerrillaCache, string scriptsFolder)
    {
        var archive = Packfile.Open(Path.Combine(guerrillaCache, "interface.vpp_pc"));
        if (archive.Find(Container) is not { } entry)
            return [$"Fog of war: Guerrilla's interface.vpp_pc has no {Container}; the world map has no fog"];
        Packfile container = Packfile.FromBytes(entry.Name, archive.Read(entry));
        if (container.Find(Peg + ".cpeg_pc") is not { } header || container.Find(Peg + ".gpeg_pc") is not { } data)
            return [$"Fog of war: {Container} has no {Peg}; the world map has no fog"];
        var peg = PegFile.Read(container.Read(header), container.Read(data));
        var texture = peg.Textures.FirstOrDefault(t => Path.GetFileNameWithoutExtension(t.Name).Equals(Picture, StringComparison.OrdinalIgnoreCase));
        if (texture == null) return [$"Fog of war: {Peg} has no {Picture}; the world map has no fog"];
        var image = peg.Decode(texture);
        int mips = Math.Clamp(texture.MipLevels, 1, 1 + (int)Math.Log2(Math.Min(image.Width, image.Height)));
        var dds = new DdsImage(TextureFormat.Argb8888, image.Width, image.Height, mips, TextureCodec.EncodeWithMips(TextureFormat.Argb8888, image, mips));
        Directory.CreateDirectory(scriptsFolder);
        File.WriteAllBytes(Path.Combine(scriptsFolder, FileName), dds.ToBytes());
        return [$"Fog of war: {FileName}, Guerrilla's {texture.Name} ({texture.Description})"];
    }
}
