using System.Numerics;
using System.Text;
using RfaFormats.Zones;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Reading and editing the material blocks inside model and terrain files, for making Armageddon's
/// files from Red Faction: Guerrilla's.
///
/// An Armageddon material block (in terrain files and stitch pieces): its size, the shader's and the
/// material's name hashes, 0x40, the texture count (u16), the constant count and room (u8s), run-time
/// words and a 0 at +0x20, then 12 bytes a texture from +0x24 (where its name starts, its slot hash,
/// its index), the constants' name hashes, and from the next multiple of 16 their values (a float4
/// each). Guerrilla's are the same but for +0x0C (0) and the textures starting at +0x20.
/// </summary>
public static class MaterialBlocks
{
    /// <summary>Sets a constant's value in every Armageddon material block of the file that has it; returns how many did.</summary>
    public static int SetConstant(byte[] file, uint hash, Vector4 value)
    {
        int count = 0;
        foreach (var (hashesAt, valuesAt, constants) in ArmageddonMaterials(file))
            for (int c = 0; c < constants; c++)
                if (BitConverter.ToUInt32(file, hashesAt + c * 4) == hash)
                {
                    Write(file, valuesAt + c * 16, value);
                    count++;
                }
        return count;
    }

    /// <summary>A constant of the first Guerrilla material block in the file that has it (a stitch piece's, say), or null.</summary>
    public static Vector4? GuerrillaConstant(byte[] file, uint hash)
    {
        foreach (var (at, textures, constants) in GuerrillaMaterials(file))
        {
            int hashesAt = at + 0x20 + textures * 12, valuesAt = at + ((hashesAt + constants * 4 - at + 15) & ~15);
            for (int c = 0; c < constants; c++)
                if (BitConverter.ToUInt32(file, hashesAt + c * 4) == hash) return Read(file, valuesAt + c * 16);
        }
        return null;
    }

    /// <summary>
    /// The ground material in a Guerrilla terrain file: 9 textures (four layers' colour and normal maps,
    /// then the blend map) and 10 constants (each layer's scale and offset, then its tint; then two
    /// more): its texture names (without ".tga") and constants.
    /// </summary>
    public static (string[] Textures, Vector4[] Constants) GuerrillaGround(byte[] terrain)
    {
        int material = -1;
        for (int at = 0x10; at + 0x40 < terrain.Length && material < 0; at += 4)
            if (BitConverter.ToUInt32(terrain, at) == 0x200 && BitConverter.ToUInt16(terrain, at + 0x10) == 9 && terrain[at + 0x12] == 10) material = at;
        if (material < 0) throw new InvalidDataException("no ground material in the Guerrilla terrain file");
        int count = BitConverter.ToUInt16(terrain, material + 0x10), constantCount = terrain[material + 0x12];
        var offsets = Enumerable.Range(0, count).Select(k => BitConverter.ToInt32(terrain, material + 0x20 + k * 12)).ToList();
        int names = NameBlockBefore(terrain, material, offsets) ?? throw new InvalidDataException("no texture names for the Guerrilla ground material");
        string[] textures = offsets.Select(o => CString(terrain, names + o)[..^4]).ToArray();
        int hashesAt = material + 0x20 + count * 12, valuesAt = material + ((hashesAt + constantCount * 4 - material + 15) & ~15);
        var constants = Enumerable.Range(0, constantCount).Select(c => Read(terrain, valuesAt + c * 16)).ToArray();
        return (textures, constants);
    }

    /// <summary>
    /// Writes a texture name over every whole copy of another (preceded by a NUL or the file's start,
    /// followed by a NUL) in the file, no longer than it, the rest NULs: nothing moves. Returns how many.
    /// </summary>
    public static int ReplaceName(byte[] file, string oldName, string newName)
    {
        if (newName.Length > oldName.Length) throw new ArgumentException($"{newName} is longer than {oldName}");
        byte[] oldBytes = Encoding.ASCII.GetBytes(oldName + "\0"), newBytes = Encoding.ASCII.GetBytes(newName);
        int count = 0;
        for (int at = file.AsSpan().IndexOf(oldBytes); at >= 0;)
        {
            if (at == 0 || file[at - 1] == 0)
            {
                Array.Clear(file, at, oldBytes.Length);
                newBytes.CopyTo(file, at);
                count++;
            }
            int next = file.AsSpan(at + 1).IndexOf(oldBytes);
            at = next < 0 ? -1 : at + 1 + next;
        }
        return count;
    }

    /// <summary>Armageddon material blocks: where each one's constant hashes and values are, and how many constants.</summary>
    static IEnumerable<(int HashesAt, int ValuesAt, int Constants)> ArmageddonMaterials(byte[] d)
    {
        for (int at = 0x28; at + 0x30 <= d.Length; at += 4)
        {
            uint size = BitConverter.ToUInt32(d, at);
            int textureCount = BitConverter.ToUInt16(d, at + 0x10), constantCount = d[at + 0x12], room = d[at + 0x13];
            if (size < 0x40 || size > 0x4000 || at + size > d.Length || BitConverter.ToUInt32(d, at + 0x0C) != 0x40 ||
                textureCount is < 1 or > 16 || constantCount > room || BitConverter.ToUInt32(d, at + 0x20) != 0) continue;
            int hashesAt = at + 0x24 + textureCount * 12, valuesAt = at + ((hashesAt + constantCount * 4 - at + 15) & ~15);
            if (valuesAt + constantCount * 16 > at + size) continue;
            yield return (hashesAt, valuesAt, constantCount);
        }
    }

    /// <summary>Guerrilla material blocks: where each starts, its texture count and constant count (its textures numbered 0, 1, ... in order).</summary>
    static IEnumerable<(int At, int Textures, int Constants)> GuerrillaMaterials(byte[] d)
    {
        for (int at = 0x10; at + 0x40 <= d.Length; at += 4)
        {
            int size = (int)BitConverter.ToUInt32(d, at), textures = BitConverter.ToUInt16(d, at + 0x10), constants = d[at + 0x12], room = d[at + 0x13];
            if (size < 0x40 || size > 0x4000 || at + size > d.Length || textures is < 1 or > 16 || constants == 0 || constants > room) continue;
            if (!Enumerable.Range(0, textures).All(k => BitConverter.ToUInt32(d, at + 0x28 + k * 12) == k)) continue;
            int hashesAt = at + 0x20 + textures * 12, valuesAt = at + ((hashesAt + constants * 4 - at + 15) & ~15);
            if (valuesAt + constants * 16 > at + size) continue;
            yield return (at, textures, constants);
        }
    }

    /// <summary>Where a material's texture names are: the nearest name block before it in which every offset starts a .tga name.</summary>
    static int? NameBlockBefore(byte[] d, int material, List<int> offsets)
    {
        for (int p = material - 8; p >= 0x10; p -= 4)
        {
            int size = BitConverter.ToInt32(d, p);
            if (size <= 0 || size > 0x8000 || p + 4 + size > material) continue;
            int start = p + 4;
            if (offsets.All(o => o >= 0 && o < size && (o == 0 || d[start + o - 1] == 0) && CString(d, start + o).EndsWith(".tga", StringComparison.OrdinalIgnoreCase)))
                return start;
        }
        return null;
    }

    internal static string CString(byte[] d, int at)
    {
        int end = Array.IndexOf(d, (byte)0, at);
        return Encoding.ASCII.GetString(d, at, (end < 0 ? d.Length : end) - at);
    }

    static Vector4 Read(byte[] d, int at) =>
        new(BitConverter.ToSingle(d, at), BitConverter.ToSingle(d, at + 4), BitConverter.ToSingle(d, at + 8), BitConverter.ToSingle(d, at + 12));

    static void Write(byte[] d, int at, Vector4 v)
    {
        BitConverter.TryWriteBytes(d.AsSpan(at), v.X); BitConverter.TryWriteBytes(d.AsSpan(at + 4), v.Y);
        BitConverter.TryWriteBytes(d.AsSpan(at + 8), v.Z); BitConverter.TryWriteBytes(d.AsSpan(at + 12), v.W);
    }
}
