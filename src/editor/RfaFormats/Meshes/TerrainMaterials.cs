using System.Numerics;
using RfaFormats.Zones;

namespace RfaFormats.Meshes;

/// <summary>
/// Armageddon's terrain materials, written from code (<see cref="TerrainFile.Material"/>) as its own
/// terrain files have them, so a zone's terrain file names nothing of another level's: the shader,
/// the material's hash, its flags, its texture slots, its constants and the rows it has room for are
/// the shaders' own; the textures and values are the caller's.
///
/// The ground's set (in this order): the ground (shader 35CA1523: four layers' colour and normal
/// maps, the blend map, the dither texture; each layer's scale and offset and tint, Specular_Power and
/// E3218939), its second pass (820F7A64: the same with the colour map in the dither's place) and the
/// colour map alone (6748866B). The cliffs: one texture projected from the side two ways (7F946638:
/// Terrain_layer0_xform and _1, Specular_Power, Diffuse_color_0), or two blended (25A64557: _xform 0
/// to 3, Specular_Power, Diffuse_color_0 and _1); both with the blend map (its alpha mixes the two),
/// the colour map and the dither texture, the one-texture kind's second texture slots left empty. The
/// far meshes: the colour map alone (6748866B again, flags 0x140).
/// </summary>
public static class TerrainMaterials
{
    // Texture slots, by their names' hashes (the last two named by what goes in them).
    const uint DiffuseMap0 = 0x0EA73D69, NormalMap0 = 0x5D63E8D5, DiffuseMap1 = 0x79A00DFF, NormalMap1 = 0x2A64D843;
    const uint DiffuseMap2 = 0xE0A95C45, NormalMap2 = 0xB36D89F9, DiffuseMap3 = 0x97AE6CD3, NormalMap3 = 0xC46AB96F;
    const uint AlphaMap0 = 0xD4061253, AlphaMap = 0x9F434FAF, DitherMap = 0x9C264F06;
    const uint GroundDitherSlot = 0x472860E0, ColourMapSlot = 0x54B89D8D;

    /// <summary>The dither texture every terrain material names (the game's own).</summary>
    public const string Dither = "dither.tga";

    /// <summary>
    /// The ground's three materials and their names block. <paramref name="layers"/>: the four layers'
    /// colour and normal maps (eight names); <paramref name="scaleOffsets"/> and <paramref name="tints"/>:
    /// each layer's Terrain_layerN_scale_offset and Diffuse_color_N; <paramref name="specularPower"/> and
    /// <paramref name="e3218939"/>: the last two constants (Guerrilla's 984BB1F3 and E3218939).
    /// </summary>
    public static (byte[] Names, List<byte[]> Materials) Ground(IReadOnlyList<string> layers, string blendMap, string colourMap,
        IReadOnlyList<Vector4> scaleOffsets, IReadOnlyList<Vector4> tints, Vector4 specularPower, Vector4 e3218939)
    {
        var (names, at) = TerrainFile.NamesBlock([.. layers, blendMap, Dither, colourMap]);
        uint[] layerSlots = [DiffuseMap0, NormalMap0, DiffuseMap1, NormalMap1, DiffuseMap2, NormalMap2, DiffuseMap3, NormalMap3];
        var textures = Enumerable.Range(0, 8).Select(k => (at[k], layerSlots[k])).Append((at[8], AlphaMap0)).ToList();
        var constants = new List<(uint, Vector4[])>();
        for (int k = 0; k < 4; k++)
        {
            constants.Add((NameHashes.PropertyHash($"Terrain_layer{k}_scale_offset"), [scaleOffsets[k]]));
            constants.Add((NameHashes.PropertyHash($"Diffuse_color_{k}"), [tints[k]]));
        }
        constants.Add((NameHashes.PropertyHash("Specular_Power"), [specularPower]));
        constants.Add((0xE3218939, [e3218939]));
        return (names,
        [
            TerrainFile.Material(0x35CA1523, 0x078AF3A9, 0x40, 20, [.. textures, (at[9], GroundDitherSlot)], constants),
            TerrainFile.Material(0x820F7A64, 0x2D58BC91, 0x40, 20, [.. textures, (at[10], ColourMapSlot)], constants),
            TerrainFile.Material(0x6748866B, 0x9D00B383, 0x40, 10, [(at[10], DiffuseMap0)], []),
        ]);
    }

    /// <summary>
    /// A cliff material and its names block: <paramref name="second"/> null for one texture (projected by
    /// xforms 0 and 1, tinted by Diffuse_color_0), else two blended (the second by xforms 2 and 3 and
    /// Diffuse_color_1). <paramref name="xforms"/>: four rows each; <paramref name="tints"/>: one or two.
    /// </summary>
    public static (byte[] Names, byte[] Material) Cliff((string Colour, string Normal) first, (string Colour, string Normal)? second,
        string blendMap, string colourMap, IReadOnlyList<Vector4[]> xforms, IReadOnlyList<Vector4> tints, Vector4 specularPower)
    {
        var (names, at) = second is { } s
            ? TerrainFile.NamesBlock([first.Colour, first.Normal, s.Colour, s.Normal, blendMap, colourMap, Dither])
            : TerrainFile.NamesBlock([first.Colour, first.Normal, "", blendMap, colourMap, Dither]);
        int empty = 2;  // the one-texture kind's second texture: the empty name
        var textures = second is null
            ? new List<(int, uint)> { (at[0], DiffuseMap0), (at[1], NormalMap0), (at[empty], DiffuseMap1), (at[empty], NormalMap1), (at[3], AlphaMap), (at[4], ColourMapSlot), (at[5], DitherMap) }
            : [(at[0], DiffuseMap0), (at[1], NormalMap0), (at[2], DiffuseMap1), (at[3], NormalMap1), (at[4], AlphaMap), (at[5], ColourMapSlot), (at[6], DitherMap)];
        int projections = second is null ? 2 : 4;
        var constants = Enumerable.Range(0, projections).Select(k => (NameHashes.PropertyHash($"Terrain_layer{k}_xform"), xforms[k])).ToList();
        constants.Add((NameHashes.PropertyHash("Specular_Power"), [specularPower]));
        for (int k = 0; k < (second is null ? 1 : 2); k++) constants.Add((NameHashes.PropertyHash($"Diffuse_color_{k}"), [tints[k]]));
        return second is null
            ? (names, TerrainFile.Material(0x7F946638, 0x67CEC620, 0x40, 20, textures, constants))
            : (names, TerrainFile.Material(0x25A64557, 0xF71192C5, 0x40, 27, textures, constants));
    }

    /// <summary>
    /// A cliff texture's two side projections (xforms 0 and 1, or 2 and 3), as the game's are: the
    /// texture repeating <paramref name="perMetre"/> times a metre, centred on the zone's middle
    /// (<paramref name="halfWidth"/> from its corner).
    /// </summary>
    public static Vector4[][] SideProjections(float perMetre, float halfWidth)
    {
        float s = perMetre, c = s * halfWidth;
        return
        [
            [new(s, 0, 0, -c), new(0, 0, -1, 0), new(0, -s, 0, c), new(0, 0, 0, 1)],
            [new(0, 0, -s, -c), new(-1, 0, 0, 0), new(0, -s, 0, c), new(0, 0, 0, 1)],
        ];
    }

    /// <summary>The far meshes' material and its names block: the colour map alone.</summary>
    public static (byte[] Names, byte[] Material) Far(string colourMap)
    {
        var (names, at) = TerrainFile.NamesBlock([colourMap]);
        return (names, TerrainFile.Material(0x6748866B, 0x7D45CBD6, 0x140, 10, [(at[0], DiffuseMap0)], []));
    }
}
