using System.IO;
using System.Text;
using RfaFormats.Guerrilla;

namespace GuerrillaBuilder;

/// <summary>
/// What each part wrote, in mods\guerrilla_world\built\&lt;part&gt;.txt: every file under overrides\ and mods\ its last
/// build made or changed, one path a line (relative to the mod folder). The builder sees it by looking at those
/// folders before and after the part runs, so nothing that writes a file has to report it. The lists show what a
/// part needs in place (the builder's check finds any file they name that isn't there) and let Remove take away
/// the part's new files that its own Remove left behind.
/// </summary>
sealed class FileLists
{
    const string FolderName = "built";
    readonly Setup setup;

    public FileLists(Setup setup) => this.setup = setup;

    string Folder => Path.Combine(setup.WorldFolder, FolderName);
    string ListFile(Part part) => Path.Combine(Folder, Parts.Word(part) + ".txt");

    /// <summary>Every file under overrides\ and mods\ with its size and time, but the builder's own records and the editor's live files.</summary>
    public Dictionary<string, (long Size, DateTime Time)> Look()
    {
        var files = new Dictionary<string, (long, DateTime)>(StringComparer.OrdinalIgnoreCase);
        foreach (string root in new[] { setup.Overrides, Path.Combine(setup.ModFolder, "mods") })
        {
            if (!Directory.Exists(root)) continue;
            foreach (var file in new DirectoryInfo(root).EnumerateFiles("*", SearchOption.AllDirectories))
            {
                string relative = Path.GetRelativePath(setup.ModFolder, file.FullName);
                if (!Counts(relative)) continue;
                files[relative] = (file.Length, file.LastWriteTimeUtc);
            }
        }
        return files;
    }

    /// <summary>Not the builder's own records (builder.ini, these lists), nor the editor's live files (overrides\live\).</summary>
    bool Counts(string relative)
    {
        string records = Path.GetRelativePath(setup.ModFolder, setup.WorldFolder) + Path.DirectorySeparatorChar;
        if (relative.StartsWith(records + FolderName + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) return false;
        if (relative.Equals(records + "builder.ini", StringComparison.OrdinalIgnoreCase)) return false;
        return !relative.StartsWith(Path.Combine("overrides", "live") + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>
    /// Saves a part's list: the files made or changed since <paramref name="before"/>; with <paramref name="again"/> (the
    /// part built a second time in one build: the vehicles and weapons, for the turrets) the first time's files too.
    /// </summary>
    public int Save(Part part, Dictionary<string, (long Size, DateTime Time)> before, bool again)
    {
        var written = Look().Where(f => !before.TryGetValue(f.Key, out var was) || was != f.Value)
                            .Select(f => f.Key).Order(StringComparer.OrdinalIgnoreCase).ToList();
        var kept = again ? Of(part).Where(f => File.Exists(Path.Combine(setup.ModFolder, f))) : Enumerable.Empty<string>();
        var all = written.Union(kept, StringComparer.OrdinalIgnoreCase).Order(StringComparer.OrdinalIgnoreCase).ToList();
        Directory.CreateDirectory(Folder);
        var text = new StringBuilder();
        text.Append($"; The files the {Parts.Name(part)} part of editor\\GuerrillaBuilder.exe wrote, relative to the mod folder.\r\n");
        text.Append("; Written by the builder; its check looks for each of them.\r\n");
        foreach (string file in all) text.Append(file).Append("\r\n");
        File.WriteAllText(ListFile(part), text.ToString());
        return all.Count;
    }

    /// <summary>A part's list (empty if it has none: not built, or built before the lists).</summary>
    public List<string> Of(Part part)
    {
        string path = ListFile(part);
        if (!File.Exists(path)) return [];
        return [.. File.ReadLines(path).Select(l => l.Trim()).Where(l => l.Length > 0 && !l.StartsWith(';'))];
    }

    /// <summary>
    /// After a part's Remove: deletes the new files its list names (those in a part folder of overrides\build\pc\cache)
    /// that are still there and that no other part's list names, then its list. Lines for the log.
    /// </summary>
    public IEnumerable<string> RemoveLeft(Part part)
    {
        string guerrilla = Path.Combine(OverrideLayout.Cache(setup.Overrides), "guerrilla") + Path.DirectorySeparatorChar;
        var others = Parts.All.Where(p => p != part).SelectMany(Of).ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (string file in Of(part))
        {
            string path = Path.Combine(setup.ModFolder, file);
            if (!path.StartsWith(guerrilla, StringComparison.OrdinalIgnoreCase) || others.Contains(file) || !File.Exists(path)) continue;
            File.Delete(path);
            yield return $"Deleted {path}";
        }
        if (File.Exists(ListFile(part))) File.Delete(ListFile(part));
        foreach (string folder in OverrideLayout.Folders.Select(f => OverrideLayout.Folder(setup.Overrides, f)).Append(guerrilla.TrimEnd(Path.DirectorySeparatorChar)))
            if (Directory.Exists(folder) && !Directory.EnumerateFileSystemEntries(folder).Any()) Directory.Delete(folder);
    }

    /// <summary>
    /// The check: every file a part's list names is there, and no two new files in overrides\ share a name (the
    /// loader gives the game one of them by its name, whichever it saw first). False if either fails.
    /// </summary>
    public bool Check(BuildLog log)
    {
        bool good = true;
        int listed = 0;
        foreach (Part part in Parts.All)
        {
            var files = Of(part);
            listed += files.Count;
            var missing = files.Where(f => !File.Exists(Path.Combine(setup.ModFolder, f))).ToList();
            if (missing.Count == 0) continue;
            good = false;
            log.Line($"Layout check: {missing.Count} of the files {Parts.Noun(part)} wrote aren't there (build {Parts.Noun(part)} again): " +
                     string.Join(", ", missing.Take(10)) + (missing.Count > 10 ? ", ..." : ""));
        }
        var byName = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        if (Directory.Exists(setup.Overrides))
            foreach (string path in Directory.EnumerateFiles(setup.Overrides, "*", SearchOption.AllDirectories))
            {
                string relative = Path.GetRelativePath(setup.Overrides, path);
                bool inArchiveFolder = Path.GetDirectoryName(relative)!.Split(Path.DirectorySeparatorChar)
                                           .Any(f => f.EndsWith(".vpp_pc", StringComparison.OrdinalIgnoreCase));
                if (inArchiveFolder || Path.GetFileName(path).StartsWith("README", StringComparison.OrdinalIgnoreCase)) continue;
                string name = Path.GetFileName(path);
                if (!byName.TryGetValue(name, out var places)) byName[name] = places = [];
                places.Add(relative);
            }
        var twice = byName.Where(n => n.Value.Count > 1).ToList();
        foreach (var (name, places) in twice.Take(10))
            log.Line($"Layout check: {name} is in overrides\\ {places.Count} times ({string.Join(", ", places)}); the game gets only one of them");
        if (twice.Count > 0) good = false;
        int oldLayout = Directory.Exists(setup.Cache)
            ? Directory.EnumerateFiles(setup.Cache).Count(f => OverrideLayout.Find(setup.Overrides, Path.GetFileName(f)) == f && IsBuilders(Path.GetFileName(f)))
            : 0;
        log.Line($"Layout check: {listed} files in the parts' lists" + (good ? ", all there; no new file's name twice" : "") +
                 (oldLayout > 0 ? $"; {oldLayout} files from a build before the parts' folders still at the top of the cache (build the parts again to move them)" : ""));
        return good;
    }

    /// <summary>
    /// A new file at the top of the cache that the builder made before the parts' folders: not a level the editor made
    /// (&lt;level&gt;.vpp_pc, which stays there) nor RFA-Mod's own (rfa_*, which the editor moves to rfa\).
    /// </summary>
    static bool IsBuilders(string name)
    {
        if (name.StartsWith("rfa_", StringComparison.OrdinalIgnoreCase)) return false;
        if (!name.EndsWith(".vpp_pc", StringComparison.OrdinalIgnoreCase)) return true;
        string world = GuerrillaWorld.World;
        return name.StartsWith(world + "_", StringComparison.OrdinalIgnoreCase) || name.StartsWith("zonescript_" + world, StringComparison.OrdinalIgnoreCase);
    }
}
