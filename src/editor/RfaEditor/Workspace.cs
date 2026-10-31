using System.IO;
using RfaFormats;

namespace RfaEditor;

/// <summary>
/// The two folders the editor works with: the game folder (read only, never written to) and the
/// mod folder, bin\, whose overrides\ folder is where edited files go. The editor itself lives in
/// bin\editor\.
///
/// Overrides work as in the loader: overrides\&lt;archive path&gt;\&lt;file&gt; replaces that archive's
/// copy of the file, and also any other archive's copy of the same size (identical copies, such as
/// each table in both table.vpp_pc and misc.vpp_pc).
/// </summary>
sealed class Workspace
{
    readonly Dictionary<string, Packfile> archives = new(StringComparer.OrdinalIgnoreCase);
    (string Key, Packfile Container)? lastContainer;
    Dictionary<string, List<string>>? overrides;  // file name -> override paths, once looked at
    List<(string Archive, string Name)>? assetLists;

    public string GameFolder { get; }
    public string ModFolder { get; }
    public string OverridesFolder => Path.Combine(ModFolder, "overrides");

    public Workspace(string gameFolder, string modFolder)
    {
        GameFolder = gameFolder;
        ModFolder = modFolder;
    }

    /// <summary>bin\: the folder above bin\editor\ when it has rfa_mods.ini, else the editor's own folder.</summary>
    public static string FindModFolder()
    {
        string editorFolder = AppContext.BaseDirectory.TrimEnd('\\');
        string? parent = Path.GetDirectoryName(editorFolder);
        return parent != null && File.Exists(Path.Combine(parent, "rfa_mods.ini")) ? parent : editorFolder;
    }

    /// <summary>
    /// The game folder, found the same way the launcher finds it: GameDir in rfa_mods.ini, or the
    /// mod folder itself when the mod was copied into the game folder. Null if neither works.
    /// </summary>
    public static string? FindGameFolder(string modFolder)
    {
        string? gameDir = IniFile.Read(Path.Combine(modFolder, "rfa_mods.ini"), "Launcher", "GameDir");
        if (RfaFormats.GameFolder.IsGameFolder(gameDir)) return gameDir!.TrimEnd('\\');
        if (RfaFormats.GameFolder.IsGameFolder(modFolder)) return modFolder;
        return null;
    }

    // ---- The game's files ----------------------------------------------------------------------

    /// <summary>An archive, by its path relative to the game folder. Opened once, then kept.</summary>
    public Packfile Archive(string archive)
    {
        if (!archives.TryGetValue(archive, out Packfile? pack))
        {
            pack = Packfile.Open(Path.Combine(GameFolder, archive));
            archives[archive] = pack;
        }
        return pack;
    }

    /// <summary>The game's own copy of a file in an archive (never the override).</summary>
    public byte[] ReadOriginal(string archive, string fileName)
    {
        Packfile pack = Archive(archive);
        return pack.Read(pack.Find(fileName) ?? throw new FileNotFoundException($"{archive} has no {fileName}"));
    }

    /// <summary>A file in an archive as the game will see it: the override if one applies, else the game's copy.</summary>
    public byte[] Read(string archive, string fileName) =>
        OverrideFor(archive, fileName) is string overridden ? File.ReadAllBytes(overridden) : ReadOriginal(archive, fileName);

    /// <summary>A .str2_pc container in an archive, as the game will see it (your rebuilt one if you
    /// have one). The last one opened is kept.</summary>
    public Packfile Container(string archive, string containerName)
    {
        string key = archive + ">" + containerName;
        if (lastContainer?.Key == key) return lastContainer.Value.Container;
        Packfile container = Packfile.FromBytes(containerName, Read(archive, containerName));
        lastContainer = (key, container);
        return container;
    }

    /// <summary>
    /// A file by name only, as the game will see it: the override if there is one, else the game's
    /// own copy from the first archive that has it. Null if no archive has it. (Used for tables,
    /// which the game looks up by name.)
    /// </summary>
    public byte[]? ReadAsGameSeesIt(string fileName)
    {
        foreach (string archive in RfaFormats.GameFolder.Archives(GameFolder))
            if (Archive(archive).Find(fileName) != null) return Read(archive, fileName);
        return null;
    }

    /// <summary>Every asset list (.asm_pc) in the game: which archive, and its name.</summary>
    public IReadOnlyList<(string Archive, string Name)> AssetLists()
    {
        assetLists ??= RfaFormats.GameFolder.Archives(GameFolder)
            .SelectMany(archive => Archive(archive).Entries.Where(e => e.Extension == "asm_pc").Select(e => (archive, e.Name)))
            .ToList();
        return assetLists;
    }

    // ---- Overrides -----------------------------------------------------------------------------

    /// <summary>Where the override for a file in an archive goes: overrides\ mirrors the game folder,
    /// with a folder named after each archive, e.g. overrides\build\pc\cache\table.vpp_pc\weapons.xtbl.</summary>
    public string OverridePath(string archive, string fileName) => Path.Combine(OverridesFolder, archive, fileName);

    /// <summary>
    /// The override the game will use for this archive's copy of a file, or null: one in the
    /// archive's own folder, else one for another archive whose copy is the same size.
    /// </summary>
    public string? OverrideFor(string archive, string fileName)
    {
        overrides ??= ScanOverrides();
        if (!overrides.TryGetValue(fileName, out List<string>? paths)) return null;
        string? own = paths.FirstOrDefault(p => ArchiveOf(p).Equals(archive, StringComparison.OrdinalIgnoreCase));
        if (own != null) return own;
        uint? size = Archive(archive).Find(fileName)?.Size;
        return paths.FirstOrDefault(p => TryArchiveSize(ArchiveOf(p), fileName) == size);
    }

    /// <summary>Writes an override for this archive's copy of a file.</summary>
    public string WriteOverride(string archive, string fileName, byte[] data)
    {
        string path = OverridePath(archive, fileName);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, data);
        Changed();
        return path;
    }

    /// <summary>Deletes the override the game would use for this archive's copy of a file, if any.</summary>
    public void DeleteOverride(string archive, string fileName)
    {
        if (OverrideFor(archive, fileName) is string path) File.Delete(path);
        Changed();
    }

    /// <summary>Forget what is in overrides\, so it is looked at again (after files there changed).</summary>
    public void RefreshOverrides() => Changed();

    void Changed()
    {
        overrides = null;
        lastContainer = null;
    }

    string ArchiveOf(string overridePath) => Path.GetRelativePath(OverridesFolder, Path.GetDirectoryName(overridePath)!);

    uint? TryArchiveSize(string archive, string fileName)
    {
        try
        {
            return File.Exists(Path.Combine(GameFolder, archive)) ? Archive(archive).Find(fileName)?.Size : null;
        }
        catch (InvalidDataException)
        {
            return null;
        }
    }

    Dictionary<string, List<string>> ScanOverrides()
    {
        var found = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        if (!Directory.Exists(OverridesFolder)) return found;
        foreach (string path in Directory.EnumerateFiles(OverridesFolder, "*", SearchOption.AllDirectories))
        {
            if (!Path.GetDirectoryName(path)!.EndsWith(".vpp_pc", StringComparison.OrdinalIgnoreCase)) continue;
            string name = Path.GetFileName(path);
            if (!found.TryGetValue(name, out List<string>? paths)) found[name] = paths = [];
            paths.Add(path);
        }
        return found;
    }

    public string RelativeToMod(string path) => Path.GetRelativePath(ModFolder, path);
}

/// <summary>Reads one value from an ini file (the same files Windows' GetPrivateProfileString reads).</summary>
static class IniFile
{
    public static string? Read(string path, string section, string key)
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

    /// <summary>
    /// Sets a key in an ini file, keeping everything else as it is (comments too): the key's line
    /// in its section is replaced, or added at the end of the section, or the section is added.
    /// </summary>
    public static void Write(string path, string section, string key, string value)
    {
        var lines = File.Exists(path) ? File.ReadAllLines(path).ToList() : [];
        int sectionAt = -1, endOfSection = lines.Count;
        for (int i = 0; i < lines.Count; i++)
        {
            string line = lines[i].Trim();
            if (line.StartsWith('[') && line.EndsWith(']'))
            {
                if (sectionAt >= 0)
                {
                    endOfSection = i;
                    break;
                }
                if (line[1..^1].Trim().Equals(section, StringComparison.OrdinalIgnoreCase)) sectionAt = i;
            }
            else if (sectionAt >= 0 && !line.StartsWith(';') && line.IndexOf('=') is int equals and > 0 &&
                     line[..equals].Trim().Equals(key, StringComparison.OrdinalIgnoreCase))
            {
                lines[i] = $"{key}={value}";
                File.WriteAllLines(path, lines);
                return;
            }
        }
        if (sectionAt < 0)
        {
            lines.Add($"[{section}]");
            lines.Add($"{key}={value}");
        }
        else
        {
            // After the section's last line that isn't blank.
            int at = endOfSection;
            while (at > sectionAt + 1 && lines[at - 1].Trim().Length == 0) at--;
            lines.Insert(at, $"{key}={value}");
        }
        File.WriteAllLines(path, lines);
    }
}
