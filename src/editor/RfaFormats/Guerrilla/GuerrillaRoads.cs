using System.Numerics;
using System.Text;
using RfaFormats.Meshes;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's road decals (the road meshes its sub-zones lay into the ground's
/// cut-outs; see <see cref="TerrainSubzone"/>) made into Armageddon's, for our sub-zones of its world:
///   the record (40 bytes: instance count, run-time words, and the position its vertices are from,
///     the zone's centre) as it is: Armageddon's is the same
///   the mesh block from version 6 to 9 (<see cref="MeshBlockConversion"/>), its vertex format 25
///     made 36, the same 24 bytes (position, normal, tangent, texture coordinates); its g-data as it is
///   the texture name list with each name renamed (the ground's layers and the road's textures
///     under their new names, the zone's blend map ours), each name followed by two zeros and padded
///     to an even length, as both games' are
///   the material map and material Armageddon's own road decal's (ir_terrain_road_layer2, from one
///     of its sub-zones), which has the same nine texture slots as Guerrilla's road shader and, by
///     name hash, the same constants but one renamed and the zone's centre (26CE49CD), which
///     Armageddon's hasn't: each slot pointed at its texture's name and each constant given
///     Guerrilla's value.
/// </summary>
public static class GuerrillaRoads
{
    const byte GuerrillaVertexFormat = 25, VertexFormat = 36;

    /// <summary>Armageddon's road shader's constant name hashes that Guerrilla's names otherwise (the value (1, 1, 1, 50) in both).</summary>
    static readonly Dictionary<uint, uint> GuerrillaNameOf = new() { [0x08CC10EF] = 0x984BB1F3 };

    /// <summary>
    /// A road decal as Armageddon's sub-zones hold it, with <paramref name="map"/> and
    /// <paramref name="material"/> one of Armageddon's road decals' (see
    /// <see cref="TerrainSubzone.RoadMaterials"/>) and <paramref name="rename"/> giving each texture's
    /// new name (without ".tga").
    /// </summary>
    public static RoadDecalToWrite Convert(GuerrillaRoadDecal decal, byte[] map, byte[] material, Func<string, string> rename)
    {
        byte[] mesh = MeshBlockConversion.FromVersion6(decal.MeshBlock, 0);
        if (mesh[0x21] != GuerrillaVertexFormat) throw new InvalidDataException($"a road decal with vertex format {mesh[0x21]}, not {GuerrillaVertexFormat}");
        mesh[0x21] = VertexFormat;

        // The names, and where each starts (by Guerrilla's name).
        var names = new MemoryStream();
        var nameAt = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        foreach (string name in decal.Names.Where(n => !nameAt.ContainsKey(n)))
        {
            nameAt[name] = (int)names.Length;
            names.Write(Encoding.ASCII.GetBytes(rename(Path.GetFileNameWithoutExtension(name)) + ".tga"));
            names.Write([0, 0]);
            if (names.Length % 2 != 0) names.WriteByte(0);
        }
        byte[] list = [.. BitConverter.GetBytes((int)names.Length), .. names.ToArray()];

        // The material: its textures from +0x24 (12 bytes each: where the name starts, the slot's hash,
        // an index), the constants' hashes, and from the next multiple of 16 their values.
        byte[] m = (byte[])material.Clone();
        int textures = BitConverter.ToUInt16(m, 0x10), constants = m[0x12];
        for (int t = 0; t < textures; t++)
        {
            int slot = 0x24 + t * 12;
            uint hash = BitConverter.ToUInt32(m, slot + 4);
            string texture = decal.Slots.FirstOrDefault(s => s.Slot == hash).Texture
                             ?? throw new InvalidDataException($"Guerrilla's road decal has no texture for slot {hash:X8}");
            BitConverter.TryWriteBytes(m.AsSpan(slot), nameAt[texture]);
        }
        int hashes = 0x24 + textures * 12, values = (hashes + constants * 4 + 15) & ~15;
        for (int c = 0; c < constants; c++)
        {
            uint hash = BitConverter.ToUInt32(m, hashes + c * 4);
            if (!decal.Constants.TryGetValue(GuerrillaNameOf.GetValueOrDefault(hash, hash), out Vector4 value)) continue;
            BitConverter.TryWriteBytes(m.AsSpan(values + c * 16), value.X);
            BitConverter.TryWriteBytes(m.AsSpan(values + c * 16 + 4), value.Y);
            BitConverter.TryWriteBytes(m.AsSpan(values + c * 16 + 8), value.Z);
            BitConverter.TryWriteBytes(m.AsSpan(values + c * 16 + 12), value.W);
        }

        return new RoadDecalToWrite(decal.Record, mesh, BitConverter.ToInt32(decal.Record, 0), list, map, m, decal.MeshData);
    }
}
