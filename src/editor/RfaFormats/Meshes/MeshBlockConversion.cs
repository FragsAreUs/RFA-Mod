namespace RfaFormats.Meshes;

/// <summary>
/// Red Faction: Guerrilla's mesh blocks (version 6, in its destructible objects and stitch pieces)
/// written as Armageddon's (version 9). The vertex and index buffers (in the g-file) stay as they are:
/// both games use vertex format 2 (24, 28 or 32 bytes with 1 to 3 texture coordinate sets) and 2- or
/// 4-byte strip indices (every Guerrilla world chunk's combination is also one of Armageddon's).
///
///   version 6                                  version 9
///   +0x00 version, checksum, block size, g size  the same
///   +0x10 submeshes, table offset, vertices      +0x10 detail levels (1), submeshes, table offset, vertices
///   +0x1C vertex size, format, UV sets, 0        +0x20 the same, then six floats 1 1 1 0 0 0
///   +0x20 vertex offset, index count, offset     +0x3C the same
///   +0x2C index size, topology, render blocks    +0x48 the same; zeros to the table at +0x60
///   table at +0x30, 44 bytes a submesh:          48 bytes a submesh: the same, and 4 more run-time
///     render block count, pivot, box, 4 run-time   bytes
///   20 bytes a render block                      the same
///   the checksum                                 8 zero bytes, the checksum
/// </summary>
public static class MeshBlockConversion
{
    /// <summary>The version-9 block for the version-6 block starting at <paramref name="at"/> in <paramref name="file"/>.</summary>
    public static byte[] FromVersion6(byte[] file, int at)
    {
        int I(int o) => BitConverter.ToInt32(file, at + o);
        if (I(0) != 6) throw new InvalidDataException($"mesh block version {I(0)}, not 6");
        int submeshes = I(0x10), table = I(0x14), blocks = BitConverter.ToUInt16(file, at + 0x2E);
        // The terrain's road decal and skirt meshes give 0 render blocks here and as many a submesh as
        // its first word says (Armageddon's own keep the 0): count them from the submeshes.
        if (blocks == 0) blocks = Enumerable.Range(0, submeshes).Sum(s => I(table + s * 44));
        uint checksum = BitConverter.ToUInt32(file, at + 4);
        int end6 = table + submeshes * 44 + blocks * 20;
        if (BitConverter.ToUInt32(file, at + end6) != checksum) throw new InvalidDataException("the version-6 block doesn't end with its checksum where its counts say");

        const int table9 = 0x60;
        int size = table9 + submeshes * 48 + blocks * 20 + 8 + 4;
        var b = new byte[size];
        void Put(int o, int v) => BitConverter.TryWriteBytes(b.AsSpan(o), v);
        Put(0x00, 9);
        BitConverter.TryWriteBytes(b.AsSpan(0x04), checksum);
        Put(0x08, size);
        Put(0x0C, I(0x0C));                       // the g-file's size
        Put(0x10, 1);                             // detail levels
        Put(0x14, submeshes);
        Put(0x18, table9);
        Put(0x1C, I(0x18));                       // vertices
        Array.Copy(file, at + 0x1C, b, 0x20, 4);  // vertex size, format, UV sets, 0
        foreach (var (o, v) in new[] { (0x24, 1f), (0x28, 1f), (0x2C, 1f) }) BitConverter.TryWriteBytes(b.AsSpan(o), v);
        Put(0x3C, I(0x20));                       // vertex offset (in the g-file)
        Put(0x40, I(0x24));                       // index count
        Put(0x44, I(0x28));                       // index offset
        Array.Copy(file, at + 0x2C, b, 0x48, 4);  // index size, topology, render blocks
        for (int s = 0; s < submeshes; s++)
            Array.Copy(file, at + table + s * 44, b, table9 + s * 48, 44);
        Array.Copy(file, at + table + submeshes * 44, b, table9 + submeshes * 48, blocks * 20);
        BitConverter.TryWriteBytes(b.AsSpan(size - 4), checksum);
        return b;
    }
}
