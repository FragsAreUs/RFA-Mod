using System.Text;
using System.Text.RegularExpressions;

namespace RfaFormats.Audio;

/// <summary>
/// One of Armageddon's conversations (.ctdg_pc, 363 of them in sounds.vpp_pc): who says which voice line, in what order.
/// audiolib reads it (0xB7FA70) and plays it line by line as voice lines (0xB98590), with nothing else from Wwise.
/// <para>Layout, little-endian:</para>
/// <list type="table">
/// <item><term>+0x00</term><description>"DCAV"</description></item>
/// <item><term>+0x04</term><description>version, 1</description></item>
/// <item><term>+0x08</term><description>its name, 32 bytes, NUL-padded (the source file's, often cut to 31 characters: "e3_new_start.ctd")</description></item>
/// <item><term>+0x28</term><description>the number of lines; the game takes 1 to 30</description></item>
/// <item><term>+0x2C</term><description>the lines, 12 bytes each: the persona's id, the line's id (both Wwise's hash of the name,
/// <see cref="RfaAudio.Id"/>), and a signed pause in milliseconds (0-1000 mostly, in steps of 250; negative: the line starts
/// that long before the one before it ends)</description></item>
/// </list>
/// </summary>
public sealed class ConversationFile
{
    public const uint Magic = 0x56414344;  // "DCAV"
    public const int MaxLines = 30;        // the game refuses more (and none)

    public sealed record Line(uint Persona, uint LineId, int PauseMs);

    public string Name { get; }
    public IReadOnlyList<Line> Lines { get; }

    ConversationFile(string name, IReadOnlyList<Line> lines)
    {
        Name = name;
        Lines = lines;
    }

    /// <summary>The people who speak, in the order they first do (the game gathers them so, up to 30).</summary>
    public IReadOnlyList<uint> Personas => Lines.Select(l => l.Persona).Distinct().ToList();

    /// <summary>Whether the game will play it: it refuses a conversation with no lines or more than 30.</summary>
    public bool GameTakesIt => Lines.Count is > 0 and <= MaxLines;

    public static ConversationFile Read(byte[] data)
    {
        if (data.Length < 0x2C || BitConverter.ToUInt32(data, 0) != Magic)
            throw new InvalidDataException("not a conversation (no DCAV at its start)");
        uint version = BitConverter.ToUInt32(data, 4);
        if (version != 1)
            throw new InvalidDataException($"conversation version {version}; the game reads only version 1");
        int nameLength = Array.IndexOf(data, (byte)0, 8, 32) is int end and >= 0 ? end - 8 : 32;
        string name = Encoding.Latin1.GetString(data, 8, nameLength);
        uint count = BitConverter.ToUInt32(data, 0x28);
        if (0x2C + 12L * count > data.Length)
            throw new InvalidDataException($"it says {count} lines, but only {(data.Length - 0x2C) / 12} fit in the file");
        var lines = new List<Line>((int)count);
        for (int i = 0; i < count; i++)
        {
            int at = 0x2C + 12 * i;
            lines.Add(new Line(BitConverter.ToUInt32(data, at), BitConverter.ToUInt32(data, at + 4), BitConverter.ToInt32(data, at + 8)));
        }
        return new ConversationFile(name, lines);
    }
}

/// <summary>
/// The names of Armageddon's voice personas and lines, from its own tables: audio_personas.xtbl (a persona's Name and
/// wwise_id) and audio_line_tags.xtbl (a line's Name, wwise_id and the developers' folder, such as "MISSIONS-MISSION 13").
/// About 55 of the 1,032 lines conversations use are in neither table; they're shown by their ids.
/// </summary>
public sealed class VoiceNames
{
    public sealed record Entry(string Name, string? Folder);

    readonly Dictionary<uint, Entry> personas = [];
    readonly Dictionary<uint, Entry> lines = [];

    /// <param name="personasTable">audio_personas.xtbl's text, or null.</param>
    /// <param name="lineTagsTable">audio_line_tags.xtbl's text, or null.</param>
    /// <param name="moreNames">Other names a persona may have, such as the voice banks' (darius.bnk_pc: "darius").</param>
    public VoiceNames(string? personasTable, string? lineTagsTable, IEnumerable<string>? moreNames = null)
    {
        if (personasTable != null) Add(personas, personasTable);
        if (lineTagsTable != null) Add(lines, lineTagsTable);
        foreach (string name in moreNames ?? []) personas.TryAdd(RfaAudio.Id(name), new Entry(name, null));
    }

    public Entry? Persona(uint id) => personas.GetValueOrDefault(id);
    public Entry? Line(uint id) => lines.GetValueOrDefault(id);

    public string PersonaName(uint id) => Persona(id)?.Name ?? id.ToString("X8");
    public string LineName(uint id) => Line(id)?.Name ?? id.ToString("X8");

    // Each entry's <Name>, <wwise_id> (the hash of the name; the hash is taken when it's missing) and _Editor <Category>.
    static void Add(Dictionary<uint, Entry> into, string table)
    {
        foreach (Match row in Row().Matches(table))
        {
            string name = row.Groups["name"].Value.Trim();
            if (name.Length == 0) continue;
            uint id = uint.TryParse(row.Groups["id"].Value, out uint given) ? given : RfaAudio.Id(name);
            string? folder = row.Groups["folder"].Success ? row.Groups["folder"].Value.Trim() : null;
            into.TryAdd(id, new Entry(name, folder));
        }
    }

    static Regex Row() => row ??= new Regex(
        @"<Name>(?<name>[^<]*)</Name>\s*(?:<wwise_id>(?<id>\d+)</wwise_id>)?\s*(?:<_Editor>\s*<Category>(?<folder>[^<]*)</Category>)?",
        RegexOptions.IgnoreCase | RegexOptions.Compiled);
    static Regex? row;
}
