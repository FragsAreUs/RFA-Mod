using System.Text;

namespace RfaFormats.Zones;

/// <summary>
/// Zone files store class and property names as hashes. Both hashes are Volition's (the same as in
/// Red Faction: Guerrilla), and both ignore case:
///   classes ("rfg_mover", "obj_light", ...): rotate the hash left by 6 bits, then XOR the next character
///   properties ("chunk_name", "oP", ...): CRC-32 (the zlib table) started at 0, with no final inversion
/// The names themselves are in the game's exe, so the editor reads them from there and hashes them
/// all, instead of keeping a list.
/// </summary>
public static class NameHashes
{
    static readonly uint[] CrcTable = Enumerable.Range(0, 256).Select(n =>
    {
        uint c = (uint)n;
        for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
        return c;
    }).ToArray();

    public static uint ClassHash(string name)
    {
        uint hash = 0;
        foreach (char c in name.ToLowerInvariant()) hash = ((hash << 6) | (hash >> 26)) ^ (byte)c;
        return hash;
    }

    public static uint PropertyHash(string name)
    {
        uint hash = 0;
        foreach (char c in name.ToLowerInvariant()) hash = CrcTable[(hash ^ (byte)c) & 0xFF] ^ (hash >> 8);
        return hash;
    }
}

/// <summary>Names for the hashes in zone files, found by hashing every name-like string given.</summary>
public sealed class ZoneNames
{
    readonly Dictionary<uint, string> classes = [];
    readonly Dictionary<uint, string> properties = [];

    /// <summary>
    /// Names that aren't strings in the exe, found by hashing guesses: object_squad_spawn_node is
    /// class 0x1290612D (4,856 objects), the squad spawns, whose squad_def names a squads.xtbl entry.
    /// </summary>
    static readonly string[] KnownNames = ["object_squad_spawn_node"];

    public static ZoneNames Empty { get; } = new([]);

    public ZoneNames(IEnumerable<string> candidates)
    {
        foreach (string name in candidates.Concat(KnownNames))
        {
            classes.TryAdd(NameHashes.ClassHash(name), name);
            properties.TryAdd(NameHashes.PropertyHash(name), name);
        }
    }

    /// <summary>Every name-like string in a file (letters, digits, '_' and spaces, NUL-terminated), e.g. the game's exe.</summary>
    public static IEnumerable<string> StringsIn(byte[] file)
    {
        var current = new StringBuilder();
        foreach (byte b in file)
        {
            bool nameCharacter = b is >= (byte)'a' and <= (byte)'z' or >= (byte)'A' and <= (byte)'Z' or >= (byte)'0' and <= (byte)'9' or (byte)'_' or (byte)' ';
            if (nameCharacter && current.Length < 64)
            {
                current.Append((char)b);
                continue;
            }
            if (b == 0 && current.Length >= 2) yield return current.ToString();
            current.Clear();
        }
    }

    public string Class(uint hash) => classes.TryGetValue(hash, out string? name) ? name : $"class {hash:X8}";
    public string Property(uint hash) => properties.TryGetValue(hash, out string? name) ? name : $"property {hash:X8}";
}
