namespace RfaFormats.Zones;

/// <summary>
/// A zone's relation table: a hash table from each object's handle to where the object is in the
/// zone's objects block, which the game uses to find objects by handle. 87,368 bytes, room for
/// 7,280 objects (read from the game's code: insert at 0x6C0FE0, find at 0x6C1160, the hash at
/// 0xCA9A30):
///
///   +0       a number, 0x0112DDC8 in every zone (kept as it is)
///   +4       the first free entry (u16)
///   +6       7,280 buckets (u16): the first entry whose handle hashes to it, 0xFFFF for none
///   +14566   7,280 links (u16): an entry's next in its bucket, or a free entry's next free one
///   +29126   2 bytes of padding
///   +29128   7,280 keys (u32): each entry's handle
///   +58248   7,280 values (u32): where its object starts, from the start of the objects block
///
/// The zones' tables hold the objects in order: object n is entry n, put at the front of its
/// bucket's list; the free entries follow, each linked to the next.
/// </summary>
public static class ZoneRelationTable
{
    public const int Size = 87368;
    public const int Entries = 7280;
    const ushort Nothing = 0xFFFF;
    const int FreeAt = 4, BucketsAt = 6, LinksAt = BucketsAt + 2 * Entries, KeysAt = LinksAt + 2 * Entries + 2, ValuesAt = KeysAt + 4 * Entries;

    /// <summary>
    /// The table for objects with these handles and sizes, in this order, as the game's tools build
    /// it. Free entries' keys and values are left at 0 (the game's own tables sometimes have old
    /// numbers there, which nothing reads).
    /// </summary>
    /// <param name="first">The table's first 4 bytes, kept from the zone as read.</param>
    public static byte[] Build(IReadOnlyList<(uint Handle, int Size)> objects, uint first)
    {
        if (objects.Count > Entries) throw new InvalidOperationException($"a zone can hold at most {Entries:N0} objects");
        var table = new byte[Size];
        BitConverter.TryWriteBytes(table.AsSpan(0), first);
        for (int bucket = 0; bucket < Entries; bucket++) SetU16(table, BucketsAt + 2 * bucket, Nothing);
        int offset = 0;
        for (int entry = 0; entry < objects.Count; entry++)
        {
            (uint handle, int size) = objects[entry];
            int bucket = Bucket(handle);
            SetU16(table, LinksAt + 2 * entry, BitConverter.ToUInt16(table, BucketsAt + 2 * bucket));
            SetU16(table, BucketsAt + 2 * bucket, (ushort)entry);
            BitConverter.TryWriteBytes(table.AsSpan(KeysAt + 4 * entry), handle);
            BitConverter.TryWriteBytes(table.AsSpan(ValuesAt + 4 * entry), offset);
            offset += size;
        }
        // The free entries, each linked to the next.
        SetU16(table, FreeAt, objects.Count < Entries ? (ushort)objects.Count : Nothing);
        for (int entry = objects.Count; entry < Entries; entry++)
            SetU16(table, LinksAt + 2 * entry, entry + 1 < Entries ? (ushort)(entry + 1) : Nothing);
        return table;
    }

    /// <summary>
    /// The bucket a handle goes in (0xCA9A30): Thomas Wang's 32-bit integer hash with signed
    /// shifts (0xCA9A50), and its remainder after dividing by 7,280, signed as in C.
    /// </summary>
    public static int Bucket(uint handle)
    {
        int k = (int)handle;
        k = ~(k << 15) + k;
        k ^= k >> 10;
        k += k << 3;
        k ^= k >> 6;
        k = ~(k << 11) + k;
        k ^= k >> 16;
        return (ushort)(k % Entries);
    }

    /// <summary>Whether two tables say the same, leaving out the padding and free entries' keys and values (nothing reads those).</summary>
    public static bool SameEntries(byte[] a, byte[] b, int objectCount)
    {
        if (a.Length != Size || b.Length != Size) return false;
        if (!a.AsSpan(0, KeysAt - 2).SequenceEqual(b.AsSpan(0, KeysAt - 2))) return false;
        return a.AsSpan(KeysAt, 4 * objectCount).SequenceEqual(b.AsSpan(KeysAt, 4 * objectCount)) &&
               a.AsSpan(ValuesAt, 4 * objectCount).SequenceEqual(b.AsSpan(ValuesAt, 4 * objectCount));
    }

    static void SetU16(byte[] table, int at, int value) => BitConverter.TryWriteBytes(table.AsSpan(at), (ushort)value);
}
