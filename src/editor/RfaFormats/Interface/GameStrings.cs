using System.Text;
using System.Text.RegularExpressions;

namespace RfaFormats.Interface;

/// <summary>
/// A strings file (.le_strings, 182 in misc.vpp_pc): the game's text, each string under a tag's hash (CRC-32 of the tag in
/// lower case, <see cref="VintDocument.Hash"/>). The languages are files of their own: menu_us, menu_de, ... (us, de, es,
/// fr, it, jp, pl, ru); a few have no language (the personas' admiral, brief, ...). Two layouts, both starting with the
/// signature 0xA84C7F73 and a version (u16):
/// <list type="bullet">
/// <item>version 2 (179 files): the count (u32), then each string: its hash (u32), its length in bytes (u16) and the text
/// (UTF-16, NUL-terminated), the next starting on a 4-byte boundary (one entry of patch_jp breaks that; the reader then
/// takes the next right after it);</item>
/// <item>version 1 (mpanc, mpedf, mpgur): a hash table: the bucket count (u16, 32), the string count (u32), each bucket's
/// count and offset (u32 each) into a list of the strings' offsets (u32, from the file's start), and at each its hash
/// (u32) and its text (UTF-16 to a NUL).</item>
/// </list>
/// </summary>
public sealed class GameStrings
{
    public const uint Signature = 0xA84C7F73;

    /// <summary>The language suffixes, in the order shown, and their names.</summary>
    public static readonly IReadOnlyList<(string Code, string Name)> Languages =
    [
        ("us", "English"), ("de", "German"), ("es", "Spanish"), ("fr", "French"), ("it", "Italian"), ("jp", "Japanese"),
        ("pl", "Polish"), ("ru", "Russian"),
    ];

    public int Version { get; }
    public IReadOnlyList<(uint Hash, string Text)> Entries { get; }

    GameStrings(int version, IReadOnlyList<(uint, string)> entries)
    {
        Version = version;
        Entries = entries;
    }

    public static uint Hash(string tag) => VintDocument.Hash(tag.ToLowerInvariant());

    public static GameStrings Read(byte[] data)
    {
        if (data.Length < 10 || BitConverter.ToUInt32(data, 0) != Signature) throw new InvalidDataException("not a strings file (no signature)");
        int version = BitConverter.ToUInt16(data, 4);
        return version switch
        {
            2 => new GameStrings(2, ReadList(data)),
            1 => new GameStrings(1, ReadTable(data)),
            _ => throw new InvalidDataException($"strings version {version}; versions 1 and 2 are known"),
        };
    }

    static List<(uint, string)> ReadList(byte[] data)
    {
        int count = (int)BitConverter.ToUInt32(data, 6);
        var entries = new List<(uint, string)>(Math.Min(count, 100_000));
        int at = 10;
        for (int i = 0; i < count && Fits(data, at); i++)
        {
            uint hash = BitConverter.ToUInt32(data, at);
            int length = BitConverter.ToUInt16(data, at + 4);
            entries.Add((hash, Encoding.Unicode.GetString(data, at + 6, length - length % 2).TrimEnd('\0')));
            int next = (at + 6 + length + 3) & ~3;
            // Where the 4-byte step lands on something that can't be an entry, the nearest place just after the text
            // that can (patch_jp has an entry with a stray byte after it).
            if (i + 1 < count && !Fits(data, next))
                for (int k = at + 6 + length; k <= at + 6 + length + 8; k++)
                    if (Fits(data, k))
                    {
                        next = k;
                        break;
                    }
            at = next;
        }
        return entries;
    }

    // Whether an entry (hash, length, text) can start at `at`: it fits in the file, its length is whole characters, and
    // its text has no NUL before its end.
    static bool Fits(byte[] data, int at)
    {
        if (at < 0 || at + 6 > data.Length) return false;
        int length = BitConverter.ToUInt16(data, at + 4);
        if (length < 2 || length % 2 != 0 || at + 6 + length > data.Length) return false;
        for (int c = at + 6; c + 3 < at + 6 + length; c += 2)
            if (data[c] == 0 && data[c + 1] == 0) return false;
        return true;
    }

    /// <summary>
    /// A string in readable letters. The game's fonts don't follow Unicode past plain ASCII: each language's text names
    /// its letters by the codes its font has them at (font_general, font_polish, font_cyrillic, font_japanese in
    /// interface.vpp_pc), so a German ü is stored as 0xB9. Western languages (us, de, es, fr, it) share one table: from
    /// 0x80 a non-breaking space, ¡, ..., ®, ..., ¿, then the accented letters in Unicode's order with a few left out
    /// (Ã, Õ, Ð, ×, Ø, Ý, Þ, ÷, ø, ...), then œ, —, ’ “ ” … (pinned on the words of all their strings files);
    /// Polish has its own (Ą to ż from 0xB8, … at 0x114); Russian's Cyrillic letters are stored 0x345 below Unicode
    /// (Ё at 0xBC to ё at 0x10C). Codes not pinned stay as they are; Japanese (its font's own glyph numbers) too.
    /// </summary>
    public static string Readable(string text, string? language)
    {
        Dictionary<char, char>? table = language switch
        {
            "us" or "de" or "es" or "fr" or "it" => Western,
            "pl" => Polish,
            "ru" => Russian,
            _ => null,
        };
        if (table == null) return text;
        var readable = new StringBuilder(text.Length);
        foreach (char c in text)
        {
            if (language == "ru" && c >= 0xBC && c <= 0x10C) readable.Append((char)(c + 0x345));
            else if (c >= 0x80 && table.TryGetValue(c, out char letter)) readable.Append(letter);
            else readable.Append(c);
        }
        return readable.ToString();
    }

    /// <summary>A readable string back in the game's codes (for drawing it with the game's fonts): the reverse of <see cref="Readable"/>.</summary>
    public static string GameCodes(string text, string? language)
    {
        Dictionary<char, char>? table = language switch
        {
            "us" or "de" or "es" or "fr" or "it" => Western,
            "pl" => Polish,
            "ru" => Russian,
            _ => null,
        };
        if (table == null) return text;
        var back = new Dictionary<char, char>();
        foreach (var (code, letter) in table) back.TryAdd(letter, code);
        var codes = new StringBuilder(text.Length);
        foreach (char c in text)
        {
            if (language == "ru" && c >= 0xBC + 0x345 && c <= 0x10C + 0x345) codes.Append((char)(c - 0x345));
            else codes.Append(back.TryGetValue(c, out char code) ? code : c);
        }
        return codes.ToString();
    }

    static readonly Dictionary<char, char> Western = new()
    {
        ['\x80'] = '\u00A0', ['\x81'] = '¡', ['\x86'] = '®', ['\x8A'] = '¿',
        ['\x8B'] = 'À', ['\x8C'] = 'Á', ['\x8D'] = 'Â', ['\x8E'] = 'Ä', ['\x90'] = 'Ç', ['\x91'] = 'È', ['\x92'] = 'É', ['\x93'] = 'Ê',
        ['\x94'] = 'Ë', ['\x95'] = 'Ì', ['\x96'] = 'Í', ['\x97'] = 'Î', ['\x98'] = 'Ï', ['\x99'] = 'Ñ', ['\x9A'] = 'Ò', ['\x9B'] = 'Ó',
        ['\x9C'] = 'Ô', ['\x9D'] = 'Ö', ['\x9E'] = 'Ù', ['\x9F'] = 'Ú', ['\xA0'] = 'Û', ['\xA1'] = 'Ü', ['\xA2'] = 'ß',
        ['\xA3'] = 'à', ['\xA4'] = 'á', ['\xA5'] = 'â', ['\xA6'] = 'ä', ['\xA8'] = 'ç', ['\xA9'] = 'è', ['\xAA'] = 'é', ['\xAB'] = 'ê',
        ['\xAC'] = 'ë', ['\xAD'] = 'ì', ['\xAE'] = 'í', ['\xAF'] = 'î', ['\xB0'] = 'ï', ['\xB1'] = 'ñ', ['\xB2'] = 'ò', ['\xB3'] = 'ó',
        ['\xB4'] = 'ô', ['\xB5'] = 'ö', ['\xB6'] = 'ù', ['\xB7'] = 'ú', ['\xB8'] = 'û', ['\xB9'] = 'ü', ['\xBB'] = 'œ',
        ['\xBE'] = '—', ['\xC0'] = '’', ['\xC2'] = '“', ['\xC3'] = '”', ['\xC5'] = '…',
    };

    // Russian's own, below its Cyrillic letters (those are a shift, in Readable).
    static readonly Dictionary<char, char> Russian = new() { ['\x84'] = '®' };

    static readonly Dictionary<char, char> Polish = new()
    {
        ['\x84'] = '®', ['\x99'] = 'Ó', ['\xB1'] = 'ó',
        ['\xB8'] = 'Ą', ['\xB9'] = 'ą', ['\xBA'] = 'Ć', ['\xBB'] = 'ć', ['\xBC'] = 'Ę', ['\xBD'] = 'ę', ['\xBE'] = 'Ł', ['\xBF'] = 'ł',
        ['\xC0'] = 'Ń', ['\xC1'] = 'ń', ['\xC4'] = 'Ś', ['\xC5'] = 'ś', ['\xC6'] = 'Ź', ['\xC7'] = 'ź', ['\xC8'] = 'Ż', ['\xC9'] = 'ż',
        ['\u0114'] = '…',
    };

    static List<(uint, string)> ReadTable(byte[] data)
    {
        int buckets = BitConverter.ToUInt16(data, 6), count = (int)BitConverter.ToUInt32(data, 8);
        int offsets = 12 + buckets * 8;
        var entries = new List<(uint, string)>(Math.Min(count, 100_000));
        for (int i = 0; i < count && offsets + 4 * i + 4 <= data.Length; i++)
        {
            int at = (int)BitConverter.ToUInt32(data, offsets + 4 * i);
            if (at < 0 || at + 6 > data.Length) continue;
            int end = at + 4;
            while (end + 1 < data.Length && (data[end] != 0 || data[end + 1] != 0)) end += 2;
            entries.Add((BitConverter.ToUInt32(data, at), Encoding.Unicode.GetString(data, at + 4, end - at - 4)));
        }
        return entries;
    }

    /// <summary>A strings file's name without its language and extension, and its language ("us", ...), or null if it has none.</summary>
    public static (string Base, string? Language) Split(string fileName)
    {
        string name = fileName.EndsWith(".le_strings", StringComparison.OrdinalIgnoreCase) ? fileName[..^".le_strings".Length] : fileName;
        int underscore = name.LastIndexOf('_');
        if (underscore > 0 && Languages.Any(l => l.Code.Equals(name[(underscore + 1)..], StringComparison.OrdinalIgnoreCase)))
            return (name[..underscore], name[(underscore + 1)..].ToLowerInvariant());
        return (name, null);
    }

    /// <summary>
    /// The tags' names, found by hashing every word of the given tables (the game's .xtbl files in misc.vpp_pc name all
    /// but one of the 2,237 tags its English strings use): hash → tag, as the tables write it.
    /// </summary>
    public static Dictionary<uint, string> TagNames(IEnumerable<byte[]> tables)
    {
        var names = new Dictionary<uint, string>();
        foreach (byte[] table in tables)
            foreach (Match word in TagWord().Matches(Encoding.Latin1.GetString(table)))
                names.TryAdd(Hash(word.Value), word.Value);
        return names;
    }

    static Regex TagWord() => tagWord ??= new Regex(@"[A-Za-z][A-Za-z0-9_]{2,}", RegexOptions.Compiled);
    static Regex? tagWord;
}
