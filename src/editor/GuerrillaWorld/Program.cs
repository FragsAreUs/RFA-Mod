// GuerrillaWorld.exe [<Guerrilla folder>] [--zones all | <columns>,<rows>] [--props <chunk files>]
//
// Builds the world "rfg", Red Faction: Guerrilla's open world, from your own copy of Guerrilla (which
// is only read): see RfaFormats.Guerrilla.GuerrillaWorld. What it makes stays on your machine. It
// lives in bin\editor\, next to the editor, and finds everything the way the editor does:
//   - the mod folder: the folder above it, the one with rfa_mods.ini;
//   - the game: GameDir in rfa_mods.ini, or the mod folder itself when the mod was copied into the
//     game folder;
//   - Guerrilla: the folder given, or GuerrillaDir under [Launcher] in rfa_mods.ini (the one
//     with build\pc\cache\terr01_l0.vpp_pc; the Steam Edition's is
//     ...\Red Faction Guerrilla; Re-MARS-tered's newer files aren't read yet).
// It writes the world into overrides\build\pc\cache\ in the mod folder and the clouds into
// mods\guerrilla_world\. --zones picks which of Guerrilla's zones to build: all of them (the
// default), or a block of columns and rows, such as 11-12,10-11. --props places Guerrilla's props (its
// general movers) made from the chunk files named, such as con_cardboard_a,con_crate02_a; none by
// default while they are being tried. In the game: level rfg_12_11 in the developer console.
using RfaFormats;
using RfaFormats.Guerrilla;

string here = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
string? parent = Path.GetDirectoryName(here);
string modFolder = parent != null && File.Exists(Path.Combine(parent, "rfa_mods.ini")) ? parent : here;
string ini = Path.Combine(modFolder, "rfa_mods.ini");

string? guerrillaArgument = null, zonesArgument = null;
var props = new List<string>();
for (int i = 0; i < args.Length; i++)
{
    if (args[i] == "--zones" && i + 1 < args.Length) zonesArgument = args[++i];
    else if (args[i] == "--props" && i + 1 < args.Length) props.AddRange(args[++i].Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries));
    else if (args[i].StartsWith("--")) return Fail($"Unknown option {args[i]}. Use: GuerrillaWorld.exe [<Guerrilla folder>] [--zones all | <columns>,<rows>] [--props <chunk files>]");
    else guerrillaArgument = args[i];
}

string? gameDir = IniValue(ini, "Launcher", "GameDir")?.TrimEnd('\\');
string? game = GameFolder.IsGameFolder(gameDir) ? gameDir : GameFolder.IsGameFolder(modFolder) ? modFolder : null;
if (game == null)
    return Fail($"Can't find Red Faction: Armageddon: set GameDir in {ini} to the folder with {GameFolder.ExeName}.");

string? guerrilla = guerrillaArgument ?? IniValue(ini, "Launcher", "GuerrillaDir");
if (string.IsNullOrWhiteSpace(guerrilla))
    return Fail($"Which copy of Red Faction: Guerrilla? Give its folder, or set GuerrillaDir under [Launcher] in {ini}.");
guerrilla = guerrilla.Trim().Trim('"').TrimEnd('\\');
string guerrillaCache = Path.Combine(guerrilla, "build", "pc", "cache");
if (!File.Exists(Path.Combine(guerrillaCache, "terr01_l0.vpp_pc")))
    return Fail(File.Exists(Path.Combine(guerrilla, "data", "terr01_l0.vpp_pc"))
        ? $"{guerrilla} is Red Faction: Guerrilla Re-MARS-tered, which this doesn't read yet (its files are newer versions). Use the original Steam Edition, Red Faction: Guerrilla."
        : $"{guerrilla} isn't a Red Faction: Guerrilla folder (it has no build\\pc\\cache\\terr01_l0.vpp_pc).");

Console.WriteLine($"Guerrilla:  {guerrilla}");
Console.WriteLine($"Armageddon: {game}");
try
{
    var zones = GuerrillaWorld.ZonesIn(guerrillaCache);
    if (zonesArgument != null && !zonesArgument.Equals("all", StringComparison.OrdinalIgnoreCase))
    {
        var block = ParseBlock(zonesArgument);
        if (block == null) return Fail($"--zones {zonesArgument}: give columns and rows as from-to,from-to (such as 11-12,10-11), or all.");
        var (cols, rows) = block.Value;
        zones = zones.Where(z => z.Col >= cols.From && z.Col <= cols.To && z.Row >= rows.From && z.Row <= rows.To).ToList();
        if (zones.Count == 0) return Fail($"Guerrilla has no zones in columns {cols.From}-{cols.To}, rows {rows.From}-{rows.To}.");
    }
    Console.WriteLine($"Building {zones.Count} of Guerrilla's zones (this takes a while for many)...");
    GuerrillaWorld.Build(guerrilla, game, Path.Combine(modFolder, "overrides"), Path.Combine(modFolder, "mods", "guerrilla_world"), zones,
                         GuerrillaWorld.DefaultStart, Console.WriteLine, props);
}
catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException or NotSupportedException)
{
    return Fail(error.Message);
}
Console.WriteLine("Done. In the game, open the developer console and type: level rfg_12_11");
return 0;

static int Fail(string message)
{
    Console.Error.WriteLine(message);
    return 1;
}

// "11-12,10-11": columns 11 to 12, rows 10 to 11 (a single number for one).
static ((int From, int To) Cols, (int From, int To) Rows)? ParseBlock(string text)
{
    string[] parts = text.Split(',');
    if (parts.Length != 2) return null;
    static (int, int)? Range(string s)
    {
        string[] ends = s.Trim().Split('-');
        if (ends.Length is < 1 or > 2 || !int.TryParse(ends[0], out int from)) return null;
        int to = from;
        if (ends.Length == 2 && !int.TryParse(ends[1], out to)) return null;
        return (Math.Min(from, to), Math.Max(from, to));
    }
    return Range(parts[0]) is { } cols && Range(parts[1]) is { } rows ? (cols, rows) : null;
}

// A key's value in an ini file, as the editor reads it: [section], key=value, ; comments.
static string? IniValue(string path, string section, string key)
{
    if (!File.Exists(path)) return null;
    string current = "";
    foreach (string raw in File.ReadLines(path))
    {
        string line = raw.Trim();
        if (line.StartsWith('[') && line.EndsWith(']'))
            current = line[1..^1].Trim();
        else if (current.Equals(section, StringComparison.OrdinalIgnoreCase) && !line.StartsWith(';'))
        {
            int equals = line.IndexOf('=');
            if (equals > 0 && line[..equals].Trim().Equals(key, StringComparison.OrdinalIgnoreCase))
                return line[(equals + 1)..].Trim();
        }
    }
    return null;
}
