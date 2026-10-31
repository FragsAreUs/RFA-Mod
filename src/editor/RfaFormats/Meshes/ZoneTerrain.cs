using System.Numerics;
using System.Text;
using RfaFormats.Zones;

namespace RfaFormats.Meshes;

/// <summary>
/// What a zone's ground looks like, from its terrain file (.cterrain_pc, "TERR" version 42): the
/// ground material's four texture layers and the blend map that mixes them.
///
/// The material is a block laid out as in models (see <see cref="ModelMaterial"/>): size, two
/// checksums, flags (0x40), texture count (u16), constant count (u8) and room for constants (u8),
/// three run-time pointers and a 0, then 12 bytes per texture (offset of its name in the name block
/// before the material, the slot's name hash, 0), then the constants' name hashes, and from the
/// next multiple of 16 bytes their values, one float4 each. Slots and constants are named by the
/// game's terrain shader, hashed like zone properties (lower-case CRC-32): "Diffuse_Map_0" to
/// "_3", "Alpha_Map_0", "Terrain_layer0_scale_offset" to "3", "Diffuse_color_0" to "_3".
///
/// The shader (ir_terrain_height_mesh_t in misc.vpp_pc) draws the ground like this:
///   each layer's texture coordinates = (world x or z / 255 + offset) × scale, from its
///   scale_offset (scale x, scale y, offset x, offset y): 64 makes a texture repeat every 255/64 m
///   the blend map is stretched over the zone (u along x, v along z), and its colour channels mix
///   the layers one over another: start with layer 0, blend in layer 1 by blue, layer 2 by green,
///   layer 3 by red (alpha isn't used), so the weights are
///     layer 0: (1−B)(1−G)(1−R), layer 1: B(1−G)(1−R), layer 2: G(1−R), layer 3: R
///   each layer's colour is multiplied by its Diffuse_color, and the sum by the game's lighting.
/// </summary>
public sealed class ZoneTerrain
{
    public const int Version = 42;
    public const int LayerCount = 4;

    /// <summary>The blend map's texture name, e.g. "world_08_02_alpha00.tga".</summary>
    public string BlendMap { get; }

    /// <summary>The four layers, in the shader's order.</summary>
    public IReadOnlyList<TerrainLayer> Layers { get; }

    ZoneTerrain(string blendMap, IReadOnlyList<TerrainLayer> layers)
    {
        BlendMap = blendMap;
        Layers = layers;
    }

    /// <summary>The zone's ground material, or null if the terrain file has none (zones without ground).</summary>
    public static ZoneTerrain? Read(byte[] data)
    {
        if (data.Length < 0x30 || Encoding.ASCII.GetString(data, 0, 4) != "TERR")
            throw new InvalidDataException("not a terrain file (no TERR)");
        int version = BitConverter.ToInt32(data, 4);
        if (version != Version) throw new NotSupportedException($"terrain file version {version}");

        uint blendSlot = NameHashes.PropertyHash("Alpha_Map_0");
        for (int at = 0x28; at + 0x30 <= data.Length; at += 4)
        {
            if (Material.Read(data, at) is not { } material || !material.Textures.TryGetValue(blendSlot, out string? blendMap)) continue;
            var layers = new List<TerrainLayer>();
            for (int i = 0; i < LayerCount; i++)
            {
                if (!material.Textures.TryGetValue(NameHashes.PropertyHash($"Diffuse_Map_{i}"), out string? texture)) return null;
                layers.Add(new TerrainLayer(texture,
                    material.Constants.GetValueOrDefault(NameHashes.PropertyHash($"Terrain_layer{i}_scale_offset"), new Vector4(64, -64, -0.5f, -0.5f)),
                    material.Constants.GetValueOrDefault(NameHashes.PropertyHash($"Diffuse_color_{i}"), Vector4.One)));
            }
            return new ZoneTerrain(blendMap, layers);
        }
        return null;
    }

    /// <summary>A material block: its textures (slot hash → texture name) and constants (name hash → value).</summary>
    sealed record Material(Dictionary<uint, string> Textures, Dictionary<uint, Vector4> Constants)
    {
        public static Material? Read(byte[] data, int at)
        {
            uint size = BitConverter.ToUInt32(data, at);
            int textureCount = BitConverter.ToUInt16(data, at + 0x10);
            int constantCount = data[at + 0x12], constantRoom = data[at + 0x13];
            if (size < 0x40 || size > 0x4000 || at + size > data.Length || BitConverter.ToUInt32(data, at + 0x0C) != 0x40 ||
                textureCount is < 1 or > 16 || constantCount > constantRoom || BitConverter.ToUInt32(data, at + 0x20) != 0)
                return null;
            int texturesAt = at + 0x24, hashesAt = texturesAt + textureCount * 12;
            int valuesAt = at + ((hashesAt + constantCount * 4 - at + 15) & ~15);
            if (valuesAt + constantCount * 16 > at + size) return null;

            var nameOffsets = Enumerable.Range(0, textureCount).Select(t => BitConverter.ToInt32(data, texturesAt + t * 12)).ToList();
            if (NameBlockBefore(data, at, nameOffsets) is not int names) return null;
            var textures = new Dictionary<uint, string>();
            for (int t = 0; t < textureCount; t++)
                textures[BitConverter.ToUInt32(data, texturesAt + t * 12 + 4)] = CString(data, names + nameOffsets[t]);
            var constants = new Dictionary<uint, Vector4>();
            for (int c = 0; c < constantCount; c++)
            {
                int v = valuesAt + c * 16;
                constants[BitConverter.ToUInt32(data, hashesAt + c * 4)] = new Vector4(
                    BitConverter.ToSingle(data, v), BitConverter.ToSingle(data, v + 4), BitConverter.ToSingle(data, v + 8), BitConverter.ToSingle(data, v + 12));
            }
            return new Material(textures, constants);
        }

        /// <summary>
        /// The texture names a material uses are in the nearest name block before it: its size,
        /// then the NUL-separated names. Every name offset must start a ".tga" name in it.
        /// </summary>
        static int? NameBlockBefore(byte[] data, int material, List<int> offsets)
        {
            for (int p = material - 8; p >= 0x10; p -= 4)
            {
                int size = BitConverter.ToInt32(data, p);
                if (size <= 0 || size > 0x4000 || p + 4 + size > material) continue;
                int start = p + 4;
                if (offsets.All(o => o >= 0 && o < size && (o == 0 || data[start + o - 1] == 0) &&
                                     CString(data, start + o).EndsWith(".tga", StringComparison.OrdinalIgnoreCase)))
                    return start;
            }
            return null;
        }
    }

    static string CString(byte[] data, int at)
    {
        int end = Array.IndexOf(data, (byte)0, at);
        return Encoding.ASCII.GetString(data, at, (end < 0 ? data.Length : end) - at);
    }
}

/// <param name="Texture">The layer's colour texture, e.g. "ter_sur_sand_dirt_di.tga".</param>
/// <param name="ScaleOffset">Texture coordinates = (world position / 255 + (Z, W)) × (X, Y).</param>
/// <param name="Tint">Multiplies the texture's colour.</param>
public sealed record TerrainLayer(string Texture, Vector4 ScaleOffset, Vector4 Tint);
