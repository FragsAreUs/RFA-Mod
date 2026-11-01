namespace RfaFormats.Guerrilla;

/// <summary>
/// Room for a ported world's textures in Armageddon's texture name table (misc.vpp_pc's bitmaps_pc).
/// The game names every texture it loads by a slot in that table: its own textures' names come with
/// the table (5,605), and a texture whose name isn't there takes one of the spare slots (200), which
/// are never given back while the game runs. Once they're gone, every new texture is drawn as the
/// game's stand-in (a grid), and the rest white: what the second level loaded in the Guerrilla world
/// looked like, since its thousands of texture names filled the spare slots in the first.
///
/// The table, as the game reads it: version (6) and the slot count; a string table (its sizes, a
/// 32-byte name, a word, a half-word and a byte, the names, padding to 4, two words, the bucket
/// count, a word a bucket, then each bucket's entries, 8 bytes each); padding to 16; then the slots,
/// 12 bytes each (an empty one starts with -1); then 4 bytes. The game makes room for the spare
/// slots' names from the slot count, so a table with more empty slots at the end gives more room.
/// </summary>
public static class GuerrillaBitmaps
{
    /// <summary>The table with <paramref name="extra"/> more empty slots.</summary>
    public static byte[] WithRoom(byte[] table, int extra)
    {
        int U32(int at) => BitConverter.ToInt32(table, at);
        if (table.Length < 64 || U32(0) != 6) throw new InvalidDataException("bitmaps_pc isn't a version 6 texture name table");
        int capacity = U32(4);
        int at = 8;
        int strings = U32(at);
        at += 8 + 0x20 + 4 + 2 + 1 + strings;
        at = (at + 3) & ~3;
        at += 8;
        int buckets = U32(at);
        at += 4;
        int entries = 0;
        for (int b = 0; b < buckets; b++) entries += U32(at + b * 4);
        at += buckets * 4 + entries * 8;
        int slots = (at + 15) & ~15;
        int end = slots + capacity * 12;
        if (capacity <= 0 || end > table.Length) throw new InvalidDataException("bitmaps_pc's slots run past its end");
        int last = end - 12;
        if (U32(last) != -1) throw new InvalidDataException("bitmaps_pc's last slot isn't empty");

        var output = new byte[table.Length + extra * 12];
        Array.Copy(table, output, end);
        BitConverter.TryWriteBytes(output.AsSpan(4), capacity + extra);
        for (int k = 0; k < extra; k++) Array.Copy(table, last, output, end + k * 12, 12);
        Array.Copy(table, end, output, end + extra * 12, table.Length - end);
        return output;
    }

    /// <summary>The slot count of a table.</summary>
    public static int Capacity(byte[] table) => BitConverter.ToInt32(table, 4);
}
