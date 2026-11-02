namespace RfaFormats.Meshes;

/// <summary>
/// A zone's invisible barriers, in its terrain file (.cterrain_pc, both games): collision shapes
/// nothing draws, which keep the player out of places (Guerrilla's 03_12 has 19, Armageddon's arena
/// ruin01 28, its world zones up to 146; many zones none). Placed from the zone's middle in x and z
/// (Guerrilla's zones: ((col − 8) × 511 + 255.5, (7 − row) × 511 + 255.5)), y as it is.
///
/// After the ground's collision (the height field's "MCKH" section) and the cliff materials come an
/// empty "MCKH" header (version, length 16, count 0), then one 8-byte record a barrier (its child's
/// number in the list, then −1: a run-time handle), padded with zeros to 16 bytes, then the barriers'
/// own "MCKH" section: version (Armageddon 5, Guerrilla 2), its length with the 16-byte header, the
/// number of barriers, then a Havok packfile (an hkpListShape of them; Guerrilla's are boxes, as
/// hkpConvexTransformShape around hkpBoxShape, and hulls). A zone with none has no records and the
/// section's header alone (length 16, count 0), as Armageddon's world_07_11. The count is also in
/// the file's terrain_data, the block that starts with the zone's box (+0x90).
/// </summary>
public static class TerrainBarriers
{
    const uint Magic0 = 0x57E0E057, Magic1 = 0x10C0C010;

    /// <summary>The barriers of a terrain file (either game), as convex shapes from the zone's middle; empty if it has none.</summary>
    public static List<ChunkShape> Read(byte[] terrain)
    {
        var (_, count, section, _) = Find(terrain);
        return count == 0 ? [] : ChunkCollision.ReadPackfile(terrain, section + 16);
    }

    /// <summary>Where a terrain file's barrier section ends (either game): the layer map follows, from the next multiple of 4.</summary>
    public static int End(byte[] terrain) => Find(terrain).End;

    /// <summary>The barrier records and section for <see cref="TerrainFile.BarrierRecords"/> and <see cref="TerrainFile.BarrierSection"/>: a record a barrier, and their shapes (or an empty section).</summary>
    public static (byte[] Records, byte[] Section) Parts(IReadOnlyList<ChunkShape> barriers)
    {
        var records = new List<byte>();
        for (int i = 0; i < barriers.Count; i++) { records.AddRange(BitConverter.GetBytes(i)); records.AddRange(BitConverter.GetBytes(-1)); }
        return ([.. records], barriers.Count > 0 ? ChunkCollision.Section(barriers) : TerrainFile.EmptySection);
    }

    /// <summary>
    /// Where the barriers are: their first record, how many, their section's header, and where the
    /// section ends. The first empty "MCKH" header followed by records counting up and a section with
    /// that many.
    /// </summary>
    static (int Records, int Count, int Section, int End) Find(byte[] terrain)
    {
        int I(int a) => BitConverter.ToInt32(terrain, a);
        bool Header(int a) => a >= 0 && a + 16 <= terrain.Length && terrain[a] == 'M' && terrain[a + 1] == 'C' && terrain[a + 2] == 'K' && terrain[a + 3] == 'H';
        for (int empty = 0; empty + 32 <= terrain.Length; empty += 4)
        {
            if (!Header(empty) || I(empty + 8) != 16 || I(empty + 12) != 0) continue;
            int records = empty + 16;
            // The section after the records: count them up to the next header.
            int n = 0;
            while (records + n * 8 + 8 <= terrain.Length && I(records + n * 8) == n && I(records + n * 8 + 4) == -1) n++;
            int section = records + (n * 8 + 15) / 16 * 16;
            if (!Header(section) || I(section + 12) != n) continue;
            int length = I(section + 8);
            if (n == 0 ? length != 16 : length <= 16 || section + length > terrain.Length || BitConverter.ToUInt32(terrain, section + 16) != Magic0 || BitConverter.ToUInt32(terrain, section + 20) != Magic1)
                continue;
            return (records, n, section, section + length);
        }
        throw new InvalidDataException("no invisible barrier section found in the terrain file");
    }

    /// <summary>Where terrain_data starts in an Armageddon terrain file (its box, then the rest): after the header and its three name blocks.</summary>
    static int DataAt(byte[] terrain)
    {
        int at = 0x28;
        foreach (int sizeAt in new[] { 0x10, 0x1C, 0x18 }) at += (BitConverter.ToInt32(terrain, sizeAt) + 3) & ~3;
        return at + 4;
    }
}
