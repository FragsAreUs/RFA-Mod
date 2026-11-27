using System.IO;
using RfaFormats;
using RfaFormats.Guerrilla;

namespace GuerrillaBuilder;

/// <summary>
/// Where everything is. The builder lives in editor\, next to the editor, and finds things the way
/// the editor does:
///   - the mod folder: the folder above it, the one with rfa_mods.ini;
///   - Armageddon: GameDir under [Launcher] in rfa_mods.ini, or the mod folder itself when the mod
///     was copied into the game folder;
///   - Guerrilla: GuerrillaDir under [Launcher] in rfa_mods.ini, or a folder given (the Steam
///     Edition's, ...\Red Faction Guerrilla, the one with build\pc\cache\terr01_l0.vpp_pc).
/// What it makes goes into the mod folder only (overrides\ and mods\); neither game's folder is
/// ever changed.
/// </summary>
sealed class Setup
{
    public string ModFolder { get; }
    public string Ini => Path.Combine(ModFolder, "rfa_mods.ini");
    public string Overrides => Path.Combine(ModFolder, "overrides");
    /// <summary>The cache folder in overrides\: the game's archives' folders, and a folder for each part's new files (see OverrideLayout).</summary>
    public string Cache => OverrideLayout.Cache(Overrides);
    /// <summary>A part's new file: where it is (its part's folder, or the cache's top from an older build), else where it would be.</summary>
    public string NewFile(string part, string name) => OverrideLayout.Find(Overrides, name) ?? Path.Combine(OverrideLayout.Folder(Overrides, part), name);
    /// <summary>A new file of any part, where it is; null if it is nowhere.</summary>
    public string? FindNewFile(string name) => OverrideLayout.Find(Overrides, name);
    public string WorldFolder => Path.Combine(ModFolder, "mods", "guerrilla_world");
    public string HudFolder => Path.Combine(ModFolder, "mods", "guerrilla_hud");
    public string PeopleFolder => Path.Combine(ModFolder, "mods", "guerrilla_people");
    public string CampaignFolder => Path.Combine(ModFolder, "mods", "guerrilla_campaign");
    public string LogFile => Path.Combine(ModFolder, "logs", "guerrilla_builder.log");

    /// <summary>Armageddon's folder, or null if it can't be found.</summary>
    public string? Game { get; private set; }
    /// <summary>Guerrilla's folder as given or set (it may not be one: see <see cref="GuerrillaProblem"/>).</summary>
    public string? Guerrilla { get; private set; }

    /// <summary>
    /// Which RFA-Mod this is: the first line of version.txt, which a release has ("RFA-Mod v0.5.7-beta,
    /// built from commit ..."); null for a build of your own, which has none.
    /// </summary>
    public string? Version { get; }

    Setup(string modFolder)
    {
        ModFolder = modFolder;
        string versionFile = Path.Combine(modFolder, "version.txt");
        Version = File.Exists(versionFile) ? File.ReadLines(versionFile).FirstOrDefault()?.Trim() is { Length: > 0 } v ? v : null : null;
        string? gameDir = Clean(IniFile.Read(Ini, "Launcher", "GameDir"));
        Game = GameFolder.IsGameFolder(gameDir) ? gameDir : GameFolder.IsGameFolder(modFolder) ? modFolder : null;
        Guerrilla = Clean(IniFile.Read(Ini, "Launcher", "GuerrillaDir"));
    }

    /// <summary>The set-up for the copy of RFA-Mod this program is in.</summary>
    public static Setup Find()
    {
        string here = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
        string? parent = Path.GetDirectoryName(here);
        return new Setup(parent != null && File.Exists(Path.Combine(parent, "rfa_mods.ini")) ? parent : here);
    }

    /// <summary>Uses another Guerrilla folder for this run (rfa_mods.ini is left as it is).</summary>
    public void UseGuerrilla(string folder) => Guerrilla = Clean(folder);

    /// <summary>Uses another Guerrilla folder and keeps it as GuerrillaDir in rfa_mods.ini.</summary>
    public void KeepGuerrilla(string folder)
    {
        Guerrilla = Clean(folder);
        if (File.Exists(Ini) && Guerrilla != null) IniFile.Write(Ini, "Launcher", "GuerrillaDir", Guerrilla);
    }

    /// <summary>Uses another Armageddon folder and keeps it as GameDir in rfa_mods.ini; false if it isn't one.</summary>
    public bool KeepGame(string folder)
    {
        string? game = Clean(folder);
        if (!GameFolder.IsGameFolder(game)) return false;
        Game = game;
        if (File.Exists(Ini)) IniFile.Write(Ini, "Launcher", "GameDir", game!);
        return true;
    }

    /// <summary>Why the builder can't read Armageddon, or null if it can.</summary>
    public string? GameProblem => Game == null
        ? $"Can't find Red Faction: Armageddon. Choose its folder (the one with {GameFolder.ExeName}), or set GameDir under [Launcher] in {Ini}."
        : null;

    /// <summary>Why the builder can't read Guerrilla, or null if it can.</summary>
    public string? GuerrillaProblem
    {
        get
        {
            if (string.IsNullOrWhiteSpace(Guerrilla))
                return $"Which copy of Red Faction: Guerrilla? Choose its folder, or set GuerrillaDir under [Launcher] in {Ini}.";
            if (File.Exists(Path.Combine(Guerrilla, "build", "pc", "cache", "terr01_l0.vpp_pc"))) return null;
            return File.Exists(Path.Combine(Guerrilla, "data", "terr01_l0.vpp_pc"))
                ? $"{Guerrilla} is Red Faction: Guerrilla Re-MARS-tered, which this doesn't read yet (its files are newer versions). Use the original Steam Edition, Red Faction: Guerrilla."
                : $"{Guerrilla} isn't a Red Faction: Guerrilla folder (it has no build\\pc\\cache\\terr01_l0.vpp_pc).";
        }
    }

    /// <summary>
    /// Guerrilla's Steam Edition in the Steam library Armageddon is in (beside it, in
    /// steamapps\common\), or in another library that library's libraryfolders.vdf names; null if
    /// it isn't there. Only files are read.
    /// </summary>
    public string? FindGuerrilla()
    {
        string? common = Game != null ? Path.GetDirectoryName(Game) : null;
        if (common == null) return null;
        var candidates = new List<string> { Path.Combine(common, SteamFolderName) };
        string? steamapps = Path.GetDirectoryName(common);
        string libraries = steamapps != null ? Path.Combine(steamapps, "libraryfolders.vdf") : "";
        if (File.Exists(libraries))
            foreach (System.Text.RegularExpressions.Match m in System.Text.RegularExpressions.Regex.Matches(File.ReadAllText(libraries), "\"path\"\\s+\"([^\"]+)\""))
                candidates.Add(Path.Combine(m.Groups[1].Value.Replace(@"\\", @"\"), "steamapps", "common", SteamFolderName));
        return candidates.FirstOrDefault(c => File.Exists(Path.Combine(c, "build", "pc", "cache", "terr01_l0.vpp_pc")));
    }

    /// <summary>Guerrilla's Steam Edition's folder in a Steam library's steamapps\common\.</summary>
    const string SteamFolderName = "Red Faction Guerrilla";

    static string? Clean(string? folder) =>
        string.IsNullOrWhiteSpace(folder) ? null : folder.Trim().Trim('"').TrimEnd('\\', '/');
}
