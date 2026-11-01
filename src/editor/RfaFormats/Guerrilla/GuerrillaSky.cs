using System.Numerics;
using System.Text;
using RfaFormats.Meshes;
using RfaFormats.Textures;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's clouds and far backdrop for the guerrilla_world mod, which draws them over
/// its sky: clouds.bin and the textures as DDS, in the mod's folder (mods\guerrilla_world\).
///
/// The cloud model is the one both games ship in skybox.vpp_pc (skybox_clouds; Armageddon's copy is
/// read, Guerrilla's is an older mesh version): an overhead dome (radius 13) and a ring round the
/// horizon (radius 16), each with two textures of two cloud layers, and a backdrop ring (radius 18,
/// one texture, vfx_sky_backdrop_01: the far horizon). The textures are Guerrilla's own (the cloud
/// layers from its skybox.vpp_pc; the backdrop, the sun's and the moons' from its misc.vpp_pc &gt;
/// always_loaded), decoded and saved as 32-bit DDS with their mip levels made, so they aren't
/// compressed twice.
///
/// clouds.bin (little-endian):
///   "GWCLOUD1"
///   vertex count, then each vertex: position (3 floats, the model's own: around the eye), normal (3),
///     texture coordinates (2; 1 = the texture's width)
///   part count, then each part: first index, index count, kind (0 overhead clouds, 1 clouds along
///     the horizon, 2 the backdrop: which shader and time of day curves), the height its clouds fade
///     in from and are full at (the up part of the look direction), its layers' strengths as the
///     model gives them (4 floats), its two textures' names (64 bytes each, NUL-padded, without .tga;
///     the backdrop has one, the second empty)
///   index count, then the indices (32-bit), triangle lists
/// </summary>
public static class GuerrillaSky
{
    static readonly string[] FromAlwaysLoaded = ["vfx_sky_backdrop_01", "vfx_sun_01", "deimos_d", "phobos_d"];

    /// <summary>Writes clouds.bin and the textures into <paramref name="output"/>, from the two games' cache folders (build\pc\cache).</summary>
    public static void Write(string armageddonCache, string guerrillaCache, string output, Action<string> log)
    {
        Packfile Inner(string archive, string container)
        {
            var outer = Packfile.Open(archive);
            return Packfile.FromBytes(container, outer.Read(outer.Find(container) ?? throw new InvalidDataException($"no {container} in {archive}")));
        }
        var ours = Inner(Path.Combine(armageddonCache, "skybox.vpp_pc"), "skybox_clouds.csmesh_pc.str2_pc");
        var theirs = Inner(Path.Combine(guerrillaCache, "skybox.vpp_pc"), "skybox_clouds.csmesh_pc.str2_pc");
        var model = ModelFile.Read("skybox_clouds.csmesh_pc", ours.Read(ours.Find("skybox_clouds.csmesh_pc")!), ours.Read(ours.Find("skybox_clouds.gsmesh_pc")!));
        var peg = PegFile.Read(theirs.Read(theirs.Find("skybox_clouds.cpeg_pc")!), theirs.Read(theirs.Find("skybox_clouds.gpeg_pc")!));
        Directory.CreateDirectory(output);

        var misc = Packfile.Open(Path.Combine(guerrillaCache, "misc.vpp_pc"));
        var always = PegFile.Read(misc.Read(misc.Find("always_loaded.cpeg_pc")!), misc.Read(misc.Find("always_loaded.gpeg_pc")!));
        foreach (var (pegFile, texture) in peg.Textures.Select(t => (peg, t))
                     .Concat(always.Textures.Where(t => FromAlwaysLoaded.Contains(Path.GetFileNameWithoutExtension(t.Name), StringComparer.OrdinalIgnoreCase)).Select(t => (always, t))))
        {
            var image = pegFile.Decode(texture);
            int mips = 1 + (int)Math.Log2(Math.Min(image.Width, image.Height));
            var dds = new DdsImage(TextureFormat.Argb8888, image.Width, image.Height, mips, TextureCodec.EncodeWithMips(TextureFormat.Argb8888, image, mips));
            File.WriteAllBytes(Path.Combine(output, Path.GetFileNameWithoutExtension(texture.Name) + ".dds"), dds.ToBytes());
        }

        // The two cloud parts (the materials whose textures are cloud layers) and the backdrop.
        var parts = new List<(int First, int Count, int Kind, float Fade, float Full, float[] Strengths, string Layer01, string Layer23)>();
        var indices = new List<int>();
        foreach (var submesh in model.Submeshes)
            foreach (var part in submesh.Parts)
            {
                var material = model.Materials[part.Material];
                string? layer01 = material.Texture("Layer01_map"), layer23 = material.Texture("Layer23_map");
                if (layer01 == null || layer23 == null)
                {
                    if (material.Texture("Diffuse_map") is not { } backdrop) continue;
                    parts.Add((indices.Count, part.Triangles.Length, 2, 0, 1, [1, 1, 1, 1], Path.GetFileNameWithoutExtension(backdrop), ""));
                    indices.AddRange(part.Triangles);
                    continue;
                }
                int kind = layer01.Contains("overhead", StringComparison.OrdinalIgnoreCase) ? 0 : 1;
                var strengths = material.Constant("Layer_strengths") ?? new Vector4(1);
                parts.Add((indices.Count, part.Triangles.Length, kind, material.Constant("Cloud_Fade_Height")?.X ?? 0, material.Constant("Cloud_Full_Height")?.X ?? 1,
                           [strengths.X, strengths.Y, strengths.Z, strengths.W], Path.GetFileNameWithoutExtension(layer01), Path.GetFileNameWithoutExtension(layer23)));
                indices.AddRange(part.Triangles);
            }
        var file = new MemoryStream();
        var w = new BinaryWriter(file);
        w.Write(Encoding.ASCII.GetBytes("GWCLOUD1"));
        w.Write(model.Positions.Length);
        for (int v = 0; v < model.Positions.Length; v++)
        {
            w.Write(model.Positions[v].X); w.Write(model.Positions[v].Y); w.Write(model.Positions[v].Z);
            w.Write(model.Normals[v].X); w.Write(model.Normals[v].Y); w.Write(model.Normals[v].Z);
            w.Write(model.TextureCoordinates[v].X); w.Write(model.TextureCoordinates[v].Y);
        }
        w.Write(parts.Count);
        foreach (var p in parts)
        {
            w.Write(p.First); w.Write(p.Count); w.Write(p.Kind); w.Write(p.Fade); w.Write(p.Full);
            foreach (float f in p.Strengths) w.Write(f);
            foreach (string name in new[] { p.Layer01, p.Layer23 })
            {
                byte[] fixedName = new byte[64];
                Encoding.ASCII.GetBytes(name).CopyTo(fixedName, 0);
                w.Write(fixedName);
            }
        }
        w.Write(indices.Count);
        foreach (int i in indices) w.Write(i);
        File.WriteAllBytes(Path.Combine(output, "clouds.bin"), file.ToArray());
        log($"Clouds and backdrop: {parts.Count} parts, {indices.Count / 3} triangles, {peg.Textures.Count + FromAlwaysLoaded.Length} textures");
    }
}
