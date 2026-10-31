namespace RfaFormats;

/// <summary>Where things are in the game's install folder.</summary>
public static class GameFolder
{
    public const string ExeName = "RedFactionArmageddon_DX11.exe";

    public static bool IsGameFolder(string? folder) =>
        !string.IsNullOrWhiteSpace(folder) && File.Exists(Path.Combine(folder, ExeName));

    /// <summary>
    /// Every .vpp_pc archive, as a path relative to the game folder: build\pc\cache\ for the main
    /// game, build\dlc01\pc\cache\ and so on for the DLC.
    /// </summary>
    public static IEnumerable<string> Archives(string gameFolder)
    {
        string build = Path.Combine(gameFolder, "build");
        if (!Directory.Exists(build)) return [];
        return Directory.EnumerateFiles(build, "*.vpp_pc", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(gameFolder, path))
            .OrderBy(path => Path.GetDirectoryName(path) != @"build\pc\cache")  // main game first
            .ThenBy(path => path, StringComparer.OrdinalIgnoreCase);
    }
}
