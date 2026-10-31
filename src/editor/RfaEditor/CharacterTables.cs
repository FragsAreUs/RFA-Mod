using System.IO;
using RfaFormats.Meshes;
using RfaFormats.Tables;

namespace RfaEditor;

/// <summary>
/// Which characters play which animations, from the game's tables. character.xtbl (and each DLC's
/// dlc0N_character.xtbl) gives every character its model (x.cmeshx, the file x.ccmesh_pc), its
/// skeleton (y.rigx, the file y.rig_pc) and its animation sets (Animation_Sets: male.xtbl,
/// npc_table.xtbl, ...). Each animation set is a table whose states and actions name the
/// animations they play (Filename: "z.animx" or just "z", the file z.anim_pc).
/// anim_set_filenames.xtbl gives some sets a default model and skeleton of their own (weapons,
/// vehicles, monsters); those count as characters here too, named after their set.
/// Read when first asked, and again until the asset index is ready (tables are found through it).
/// </summary>
sealed class CharacterTables(GameAssets assets)
{
    /// <summary>A character: its name, model file, skeleton file and animation set tables.</summary>
    public sealed record Character(string Name, string Model, string Rig, IReadOnlyList<string> Sets);

    static readonly string[] CharacterTableNames = ["character.xtbl", "dlc01_character.xtbl", "dlc02_character.xtbl", "dlc03_character.xtbl", "dlc04_character.xtbl"];

    List<Character>? characters;
    readonly Dictionary<string, HashSet<string>> setAnimations = new(StringComparer.OrdinalIgnoreCase);
    readonly Dictionary<string, int?> boneCounts = new(StringComparer.OrdinalIgnoreCase);

    public IReadOnlyList<Character> Characters
    {
        get
        {
            if (characters != null) return characters;
            var found = new List<Character>();
            foreach (string table in CharacterTableNames)
                foreach (XtblElement entry in assets.Table(table)?.Entries ?? [])
                {
                    string? model = entry.Element("Model")?.ValueOf("Filename")?.Trim();
                    string? rig = entry.Element("Rig")?.ValueOf("Filename")?.Trim();
                    if (string.IsNullOrEmpty(model) || string.IsNullOrEmpty(rig)) continue;
                    // Animation_Sets holds Animation_Set entries, each with the table's name in an
                    // Animation_Set of its own (or, in a few, directly).
                    var sets = (entry.Element("Animation_Sets") is { } list ? Descendants(list) : [])
                        .Where(e => e.Name == "Animation_Set" && !e.HasElements)
                        .Select(e => e.Value.Trim()).Where(s => s.Length > 0).ToList();
                    found.Add(new Character(XtblFile.EntryName(entry), FileOf(model, ".ccmesh_pc"), FileOf(rig, ".rig_pc"), sets));
                }
            foreach (XtblElement set in assets.Table("anim_set_filenames.xtbl")?.Entries ?? [])
            {
                string? name = set.ValueOf("Name")?.Trim(), model = set.ValueOf("Default_model")?.Trim(), rig = set.ValueOf("Default_rig")?.Trim();
                if (string.IsNullOrEmpty(name) || string.IsNullOrEmpty(model) || string.IsNullOrEmpty(rig)) continue;
                found.Add(new Character(Path.GetFileNameWithoutExtension(name), FileOf(model, ".ccmesh_pc"), FileOf(rig, ".rig_pc"), [name]));
            }
            if (assets.IndexReady) characters = found;
            return found;
        }
    }

    /// <summary>The animations (file names, x.anim_pc) an animation set table names.</summary>
    public IReadOnlySet<string> AnimationsIn(string set)
    {
        if (setAnimations.TryGetValue(set, out HashSet<string>? known)) return known;
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        if (assets.Table(set) is XtblFile table)
            foreach (XtblElement filename in Descendants(table.Document).Where(e => e.Name == "Filename"))
                if (filename.Value.Trim() is { Length: > 0 } value)
                    names.Add(FileOf(value, ".anim_pc"));
        if (assets.IndexReady || names.Count > 0) setAnimations[set] = names;
        return names;
    }

    /// <summary>The characters whose animation sets name an animation file.</summary>
    public IEnumerable<Character> UsersOf(string animationFile) =>
        Characters.Where(c => c.Sets.Any(set => AnimationsIn(set).Contains(animationFile)));

    /// <summary>Every animation file a character's sets name, by name.</summary>
    public IReadOnlyList<string> AnimationsOf(Character character) =>
        character.Sets.SelectMany(AnimationsIn).Distinct(StringComparer.OrdinalIgnoreCase).Order(StringComparer.OrdinalIgnoreCase).ToList();

    /// <summary>The characters with a model file (the first is the one the game's own table lists first).</summary>
    public IEnumerable<Character> WithModel(string modelFile) =>
        Characters.Where(c => c.Model.Equals(modelFile, StringComparison.OrdinalIgnoreCase));

    /// <summary>How many bones a skeleton file has (read once), or null if it can't be read.</summary>
    public int? BoneCount(string rigFile, string nearArchive, string? nearContainer)
    {
        if (boneCounts.TryGetValue(rigFile, out int? known)) return known;
        int? count = null;
        if (assets.ReadFile(rigFile, nearArchive, nearContainer) is { } file)
            try { count = Rig.Read(file.Data).Bones.Count; }
            catch (InvalidDataException) { }
        if (count != null || assets.IndexReady) boneCounts[rigFile] = count;
        return count;
    }

    /// <summary>Every skeleton file in the game (by name), once the asset index is ready.</summary>
    public IEnumerable<string> AllRigs => assets.NamesWhere(n => n.EndsWith(".rig_pc", StringComparison.OrdinalIgnoreCase));

    /// <summary>"x.cmeshx", "x.animx" or "x" as the file the game loads: x and the extension given, in lower case.</summary>
    static string FileOf(string tableName, string extension)
    {
        string name = tableName.Trim();
        string stem = Path.GetExtension(name) is ".cmeshx" or ".rigx" or ".animx" ? Path.GetFileNameWithoutExtension(name) : name;
        return (stem + extension).ToLowerInvariant();
    }

    static IEnumerable<XtblElement> Descendants(XtblElement element)
    {
        foreach (XtblElement child in element.Elements)
        {
            yield return child;
            foreach (XtblElement below in Descendants(child)) yield return below;
        }
    }
}
