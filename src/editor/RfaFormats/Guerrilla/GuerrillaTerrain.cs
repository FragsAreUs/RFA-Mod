using System.Numerics;
using System.Text;
using RfaFormats.Meshes;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Parts of Red Faction: Guerrilla's zone terrain files (.cterrain_pc, TERR version 31) that the
/// world builder ports. The file is laid out as Armageddon's (see <see cref="TerrainFile"/>) up to its
/// barriers, with a 36-byte header (no "has ground" word), a 1,064-byte terrain_data whose fields sit
/// 4 bytes lower than Armageddon's (the sub-zones at +0x48, the layer map's bytes at +0x60 and
/// materials at +0x68, the height field's handle at +0x98, the cliff materials at +0x9C), Guerrilla's
/// material blocks (textures from +0x20, not +0x24) and, after the cliff materials, its pathfinding
/// (which Armageddon's version 42 keeps elsewhere).
///
/// Its cliff ("sidemap") materials: most zones have three, for the three kinds of cliff the sub-zones
/// draw: texture A alone, A blended into B, B alone (03_12: bad_cliff_wall_01, both, bad_rock_black_01),
/// each with its own projections (Terrain_layer0_xform, ... : four rows each) and tints
/// (Diffuse_color_0, _1). Its layer map: each 1 m cell's ground effect material (2 bits), and their
/// names (03_12: Sand, Stone, Gravel), which Armageddon's effect material table still has.
/// </summary>
public static class GuerrillaTerrain
{
    /// <summary>A cliff material: its texture names (with ".tga", in its textures' order) and its constants by name hash, each its value rows.</summary>
    public sealed record CliffMaterial(IReadOnlyList<string> Textures, IReadOnlyDictionary<uint, Vector4[]> Constants);

    /// <summary>The zone's cliff materials, in order (none if it has none).</summary>
    public static List<CliffMaterial> CliffMaterials(byte[] terrain)
    {
        int I(int at) => BitConverter.ToInt32(terrain, at);
        int data = DataAt(terrain);
        int p = SkipMaterialSet(terrain, data + 0x428);
        if (I(data + 0x98) != -1)
        {
            if (Encoding.ASCII.GetString(terrain, p, 4) != "MCKH") throw new InvalidDataException("no height field where the Guerrilla terrain file should have it");
            p += I(p + 8);
        }
        p = To(p, 4) + I(data + 0x48) * 4;
        int count = I(data + 0x9C);
        var result = new List<CliffMaterial>();
        if (count <= 0) return result;
        p += 8 + count * 8;
        for (int k = 0; k < count; k++)
        {
            p = To(p, 4);
            int names = p + 4;
            p = names + I(p);
            p = To(p, 16);
            int material = p, size = I(p);
            p += size == 0 ? 4 : size;
            if (size == 0) continue;
            int textures = BitConverter.ToUInt16(terrain, material + 0x10), constants = terrain[material + 0x12];
            var textureNames = Enumerable.Range(0, textures).Select(t => CString(terrain, names + I(material + 0x20 + t * 12))).ToList();
            int hashesAt = material + 0x20 + textures * 12, valuesAt = material + To(hashesAt + constants * 4 - material, 16);
            var values = new Dictionary<uint, Vector4[]>();
            for (int c = 0; c < constants; c++)
            {
                uint hash = BitConverter.ToUInt32(terrain, hashesAt + c * 4);
                if (hash == uint.MaxValue) continue;
                int rows = 1;
                while (c + rows < constants && BitConverter.ToUInt32(terrain, hashesAt + (c + rows) * 4) == uint.MaxValue) rows++;
                values[hash] = Enumerable.Range(0, rows).Select(r => Read(terrain, valuesAt + (c + r) * 16)).ToArray();
            }
            result.Add(new CliffMaterial(textureNames, values));
        }
        return result;
    }

    /// <summary>The zone's layer map: its cells (2 bits each, as the file keeps them) and its materials' names; null if it has none.</summary>
    public static (byte[] Cells, List<string> Names)? LayerMap(byte[] terrain)
    {
        int I(int at) => BitConverter.ToInt32(terrain, at);
        int data = DataAt(terrain), size = I(data + 0x60), count = I(data + 0x68);
        if (size <= 0 || count <= 0) return null;
        int p = To(TerrainBarriers.End(terrain), 4);
        byte[] cells = terrain[p..(p + size)];
        p += size + count * 4;
        var names = new List<string>();
        for (int k = 0; k < count; k++)
        {
            names.Add(CString(terrain, p));
            p += names[^1].Length + 1;
        }
        return (cells, names);
    }

    /// <summary>Where terrain_data starts: after the 36-byte header, the texture names, the stitch names and placements and the foliage mesh names, each to 4.</summary>
    static int DataAt(byte[] terrain)
    {
        uint U(int at) => BitConverter.ToUInt32(terrain, at);
        if (U(0) != TerrainFile.Signature || U(4) != 31) throw new InvalidDataException("not a Guerrilla terrain file (TERR version 31)");
        int p = To(0x24 + (int)U(12), 4);
        p = To(p + (int)U(24) + (int)U(32) * 20, 4);
        return To(p + (int)U(20), 4);
    }

    /// <summary>Past a material set (names block, words, map, slots, materials), as <see cref="TerrainFile"/> reads the ground's.</summary>
    static int SkipMaterialSet(byte[] f, int p)
    {
        int I(int at) => BitConverter.ToInt32(f, at);
        p = To(p, 4);
        p += 4 + I(p);
        p = To(p, 4);
        int words = I(p);
        p = To(p + 4, 16);
        int n = I(p + 4);
        p += 8 + n * 4 + words * 4;
        p = To(p, 16) + n * 4;
        for (int k = 0; k < n; k++)
        {
            p = To(p, 16);
            int size = I(p);
            p += size == 0 ? 4 : size;
        }
        return p;
    }

    static string CString(byte[] d, int at) => Encoding.ASCII.GetString(d, at, Array.IndexOf(d, (byte)0, at) - at);
    static Vector4 Read(byte[] d, int at) => new(BitConverter.ToSingle(d, at), BitConverter.ToSingle(d, at + 4), BitConverter.ToSingle(d, at + 8), BitConverter.ToSingle(d, at + 12));
    static int To(int p, int multiple) => (p + multiple - 1) / multiple * multiple;
}
