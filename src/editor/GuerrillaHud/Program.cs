// GuerrillaHud.exe [<Guerrilla folder>]
// GuerrillaHud.exe --remove          takes Guerrilla's HUD away again (all of what it wrote)
//
// Makes Red Faction: Guerrilla's HUD loadable in Armageddon, for the guerrilla_hud mod, from your
// own copy of Guerrilla (which is only read): see RfaFormats.Interface.GuerrillaHud. It lives in
// bin\editor\, next to the editor, and finds everything the way the editor does:
//   - the mod folder: the folder above it, the one with rfa_mods.ini;
//   - the game: GameDir in rfa_mods.ini, or the mod folder itself when the mod was copied into
//     the game folder;
//   - Guerrilla: the folder given, or GuerrillaDir under [Launcher] in rfa_mods.ini (the one with
//     build\pc\cache\interface.vpp_pc; the Steam Edition's is ...\Red Faction Guerrilla);
//   - our scripts: mods\guerrilla_hud\ in the mod folder.
// It writes into overrides\ in the mod folder; run it again after changing a script.
using RfaFormats;
using RfaFormats.Interface;

string here = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
string? parent = Path.GetDirectoryName(here);
string modFolder = parent != null && File.Exists(Path.Combine(parent, "rfa_mods.ini")) ? parent : here;
string ini = Path.Combine(modFolder, "rfa_mods.ini");

if (args.Length > 0 && args[0].Equals("--remove", StringComparison.OrdinalIgnoreCase))
{
    foreach (string line in GuerrillaHud.Remove(Path.Combine(modFolder, "overrides"))) Console.WriteLine(line);
    return 0;
}

string? gameDir = IniValue(ini, "Launcher", "GameDir")?.TrimEnd('\\');
string? game = GameFolder.IsGameFolder(gameDir) ? gameDir : GameFolder.IsGameFolder(modFolder) ? modFolder : null;
if (game == null)
    return Fail($"Can't find Red Faction: Armageddon: set GameDir in {ini} to the folder with {GameFolder.ExeName}.");

string? guerrilla = args.Length > 0 ? args[0] : IniValue(ini, "Launcher", "GuerrillaDir");
if (string.IsNullOrWhiteSpace(guerrilla))
    return Fail($"Which copy of Red Faction: Guerrilla? Give its folder, or set GuerrillaDir under [Launcher] in {ini}.");
guerrilla = guerrilla.Trim().Trim('"').TrimEnd('\\');
if (!File.Exists(Path.Combine(guerrilla, "build", "pc", "cache", "interface.vpp_pc")))
    return Fail($"{guerrilla} isn't a Red Faction: Guerrilla folder (it has no build\\pc\\cache\\interface.vpp_pc).");

Console.WriteLine($"Guerrilla:  {guerrilla}");
Console.WriteLine($"Armageddon: {game}");
try
{
    foreach (string line in GuerrillaHud.Build(guerrilla, game, Path.Combine(modFolder, "mods", "guerrilla_hud"),
                                               Path.Combine(modFolder, "overrides")))
        Console.WriteLine(line);
}
catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException)
{
    return Fail(error.Message);
}
Console.WriteLine($"Written into {Path.Combine(modFolder, "overrides")}.");
return 0;

static int Fail(string message)
{
    Console.Error.WriteLine(message);
    return 1;
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
