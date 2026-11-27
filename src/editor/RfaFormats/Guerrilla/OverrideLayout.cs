namespace RfaFormats.Guerrilla;

/// <summary>
/// Where the Guerrilla builder's new files go in overrides\build\pc\cache. The loader gives the game a new file
/// wherever it is under overrides\ (it finds it by its name), so each part of the port keeps its files in a folder
/// of its own:
///
///   overrides\build\pc\cache\
///     interface.vpp_pc\ misc.vpp_pc\ ...  replacements of the game's own files (a folder per archive)
///     guerrilla\world\                    the world's archives (rfg_l0, rfg_l1, rfg_precache, zonescript_rfg),
///                                         its time of day and sun and moons, its activities' containers
///     guerrilla\shaders\                  Guerrilla's material shaders (gw_*.fxo_pc_dx11)
///     guerrilla\sounds\                   every sound bank (.bnk_pc and its _media.bnk_pc)
///     guerrilla\hud\                      the HUD's and the world map's documents and pictures
///     guerrilla\vehicles\                 the vehicles' containers, tables and rigs
///     guerrilla\weapons\                  the guns' and turrets' containers and rigs, the player's animations
///     guerrilla\people\                   the people's containers, tables, rigs and animations
///     guerrilla\cloth\                    the cloth's containers
///     rfa\                                RFA-Mod's own new files (the main menu's UI pack, which the editor makes)
///     &lt;level&gt;.vpp_pc                       levels made with the editor's New level stay at the top
///
/// A name is in one place only, as when every new file was at the top of the cache (builds made before this
/// layout): writing a file deletes a copy of its name anywhere else (the game would get whichever the loader saw
/// first), and finding or deleting one looks everywhere. A part's folder only decides where a file is written.
/// </summary>
public static class OverrideLayout
{
    public const string World = "world", Shaders = "shaders", Sounds = "sounds", Hud = "hud",
                        Vehicles = "vehicles", Weapons = "weapons", People = "people", Cloth = "cloth";

    /// <summary>Every part folder, for reading a file another part wrote.</summary>
    public static readonly string[] Folders = [World, Shaders, Sounds, Hud, Vehicles, Weapons, People, Cloth];

    /// <summary>overrides\build\pc\cache: the game's archives' folders (their replacements) and the new files.</summary>
    public static string Cache(string overrides) => Path.Combine(overrides, "build", "pc", "cache");

    /// <summary>A part's folder of new files: overrides\build\pc\cache\guerrilla\&lt;part&gt;.</summary>
    public static string Folder(string overrides, string part) => Path.Combine(Cache(overrides), "guerrilla", part);

    /// <summary>RFA-Mod's own new files: overrides\build\pc\cache\rfa.</summary>
    public static string RfaFolder(string overrides) => Path.Combine(Cache(overrides), "rfa");

    /// <summary>The path to write one of RFA-Mod's own new files at (rfa\, made if it isn't there); the copy an older build left at the top of the cache is deleted.</summary>
    public static string NewRfaFile(string overrides, string name)
    {
        string old = Path.Combine(Cache(overrides), name);
        if (File.Exists(old)) File.Delete(old);
        Directory.CreateDirectory(RfaFolder(overrides));
        return Path.Combine(RfaFolder(overrides), name);
    }

    /// <summary>
    /// The path to write a part's new file at: in the part's folder (made if it isn't there). A file of the name
    /// anywhere else (another part's folder, or the top of the cache from an older build) is deleted: a name is in
    /// one place only, as the loader gives the game one file of each name.
    /// </summary>
    public static string NewFile(string overrides, string part, string name)
    {
        string folder = Folder(overrides, part), path = Path.Combine(folder, name);
        foreach (string other in Places(overrides, name))
            if (!other.Equals(path, StringComparison.OrdinalIgnoreCase) && File.Exists(other)) File.Delete(other);
        Directory.CreateDirectory(folder);
        return path;
    }

    /// <summary>
    /// The path to write one of the world's new archives at (rfg_l0.vpp_pc...), as <see cref="NewFile"/>. The
    /// editor's changes to the archive (editsuild\pc\cache\&lt;archive&gt;\, which the loader finds by the archive's
    /// place in overrides\) move with it.
    /// </summary>
    public static string NewArchive(string overrides, string part, string name)
    {
        string path = NewFile(overrides, part, name);
        string edits = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(overrides))!, "edits");
        string oldEdits = Path.Combine(Cache(edits), name), newEdits = Path.Combine(Folder(edits, part), name);
        if (Directory.Exists(oldEdits) && !Directory.Exists(newEdits))
        {
            Directory.CreateDirectory(Path.GetDirectoryName(newEdits)!);
            Directory.Move(oldEdits, newEdits);
        }
        return path;
    }

    /// <summary>
    /// Where a new file of this name is now: in a part folder, or at the top of the cache (an older build); null
    /// if it is nowhere. For reading what a part wrote (its own last build's, or another part's).
    /// </summary>
    public static string? Find(string overrides, string name) => Places(overrides, name).FirstOrDefault(File.Exists);

    /// <summary>The new files of a pattern (gw_*.fxo_pc_dx11) in every part folder and at the top of the cache.</summary>
    public static IEnumerable<string> FindAll(string overrides, string pattern)
    {
        foreach (string folder in Folders.Select(f => Folder(overrides, f)).Append(Cache(overrides)))
            if (Directory.Exists(folder))
                foreach (string path in Directory.EnumerateFiles(folder, pattern)) yield return path;
    }

    /// <summary>Deletes a new file wherever it is (a part folder, or the top of the cache); the paths deleted.</summary>
    public static List<string> Delete(string overrides, string name)
    {
        var deleted = new List<string>();
        foreach (string path in Places(overrides, name).Where(File.Exists))
        {
            File.Delete(path);
            deleted.Add(path);
            DeleteIfEmpty(Path.GetDirectoryName(path)!);
        }
        return deleted;
    }

    /// <summary>Every place a new file of this name may be: each part folder, then the top of the cache. None for a name that isn't a file's.</summary>
    static IEnumerable<string> Places(string overrides, string name)
    {
        if (name.Length == 0 || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0) yield break;
        foreach (string part in Folders) yield return Path.Combine(Folder(overrides, part), name);
        yield return Path.Combine(Cache(overrides), name);
    }

    /// <summary>Takes away a part folder left empty (and guerrilla\ with it, if that is empty too); never the cache itself.</summary>
    static void DeleteIfEmpty(string folder)
    {
        string guerrilla = Path.GetDirectoryName(folder)!;
        if (!Path.GetFileName(guerrilla).Equals("guerrilla", StringComparison.OrdinalIgnoreCase)) return;
        foreach (string path in new[] { folder, guerrilla })
            if (Directory.Exists(path) && !Directory.EnumerateFileSystemEntries(path).Any()) Directory.Delete(path);
    }
}
