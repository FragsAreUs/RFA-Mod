using System.IO;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using RfaFormats;
using RfaFormats.Meshes;
using RfaFormats.Tables;
using RfaFormats.Textures;
using RfaFormats.Zones;

namespace RfaEditor;

/// <summary>
/// Finds the game's models and textures by name, the way the game asks for them: a zone names a
/// model ("s_edf_tech_flyer.cchk_pc"), a model names its textures ("rock_di.tga"). Each is looked
/// for next to whatever asks (same container, or same archive) first, then anywhere, once the
/// index of every container is ready. Things found are kept, so each is read and decoded once.
/// </summary>
public sealed class GameAssets
{
    readonly Workspace workspace;
    readonly object assetLock = new();   // guards models, bitmaps, tables for parallel reads
    Dictionary<string, IndexedFile>? files;     // every file in an archive or container, by name
    ILookup<string, IndexedFile>? places;       // ...and every place each name is in
    Dictionary<string, TextureRef>? textures;   // every texture, by name
    readonly Dictionary<string, ModelFile?> models = new(StringComparer.OrdinalIgnoreCase);
    readonly Dictionary<string, BitmapSource?> bitmaps = new(StringComparer.OrdinalIgnoreCase);
    readonly Dictionary<string, XtblFile?> tables = new(StringComparer.OrdinalIgnoreCase);
    ZoneNames? zoneNames;
    ZoneSchema? zoneSchema;
    bool zoneSchemaRead;
    readonly Dictionary<string, IReadOnlyList<string>> choices = new(StringComparer.OrdinalIgnoreCase);  // enum choices, by their populate

    public GameAssets(Workspace workspace) => this.workspace = workspace;

    /// <summary>Whether the index of every container is ready (it is built in the background at start).</summary>
    public bool IndexReady => files != null;

    /// <summary>Raised once the index is ready, so views drawn without it can look again.</summary>
    public event Action? IndexBecameReady;

    public void SetIndex(IEnumerable<IndexedFile> allFiles, Dictionary<string, TextureRef> allTextures)
    {
        bool again = files != null;
        var byName = new Dictionary<string, IndexedFile>(StringComparer.OrdinalIgnoreCase);
        foreach (IndexedFile file in allFiles) byName.TryAdd(file.Name, file);
        files = byName;
        places = allFiles.ToLookup(f => f.Name, StringComparer.OrdinalIgnoreCase);
        textures = allTextures;
        // Misses noted before the index was ready aren't kept (see Model and Texture), so nothing to
        // forget the first time. Made again (the files changed while the editor was open: new files
        // written), what was found from the old index goes: the lists of chunks, and the misses.
        if (again)
        {
            chunkNames = null;
            chunkCatalog = null;
            chunkGroups = null;
            primitiveKinds = null;
            placements = null;
            foreach (string missed in models.Where(m => m.Value == null).Select(m => m.Key).ToList()) models.Remove(missed);
            foreach (string missed in bitmaps.Where(b => b.Value == null).Select(b => b.Key).ToList()) bitmaps.Remove(missed);
            foreach (string missed in tables.Where(t => t.Value == null).Select(t => t.Key).ToList()) tables.Remove(missed);
        }
        IndexBecameReady?.Invoke();
    }

    /// <summary>
    /// Names for the class and property hashes in zone files, from the strings in the game's exe and
    /// the names in its zone schema (which has a few the exe doesn't).
    /// </summary>
    public ZoneNames ZoneNames => zoneNames ??= new ZoneNames(
        ZoneNames.StringsIn(File.ReadAllBytes(Path.Combine(workspace.GameFolder, GameFolder.ExeName))).Concat(ZoneSchema?.Names ?? []));

    /// <summary>
    /// The game's zone schema (<see cref="RfaFormats.Zones.ZoneSchema"/>: what every class of zone
    /// object is made of), as the game has it; null if it can't be found or read.
    /// </summary>
    public ZoneSchema? ZoneSchema
    {
        get
        {
            if (zoneSchemaRead) return zoneSchema;
            zoneSchemaRead = true;
            try
            {
                if (workspace.ReadAsGameSeesIt(RfaFormats.Zones.ZoneSchema.FileName) is { } data) zoneSchema = RfaFormats.Zones.ZoneSchema.FromBytes(data);
            }
            catch (InvalidDataException) { }
            return zoneSchema;
        }
    }

    /// <summary>
    /// An enum's choices from the table its schema names (<see cref="SchemaPopulate"/>), as the game
    /// sees the table (yours, if you changed it), read once. Empty if the table can't be found.
    /// </summary>
    public IReadOnlyList<string> Choices(SchemaPopulate populate)
    {
        string key = populate.ToString();
        if (choices.TryGetValue(key, out IReadOnlyList<string>? known)) return known;
        IReadOnlyList<string> found = [];
        if (workspace.ReadAsGameSeesIt(populate.Table) is { } data) found = populate.Values(XtblFile.FromBytes(data));
        choices[key] = found;
        return found;
    }

    /// <summary>
    /// The game's script functions (statemachine_functions.xtbl and edge_conditions.xtbl, as the game sees
    /// them), for showing logic handlers' scripts; read once. Null if neither table can be found.
    /// </summary>
    public LogicFunctions? LogicFunctions
    {
        get
        {
            if (logicFunctionsRead) return logicFunctions;
            byte[]? actions = workspace.ReadAsGameSeesIt("statemachine_functions.xtbl");
            byte[]? tests = workspace.ReadAsGameSeesIt("edge_conditions.xtbl");
            logicFunctions = actions == null && tests == null ? null
                : new LogicFunctions(actions == null ? null : XtblFile.FromBytes(actions), tests == null ? null : XtblFile.FromBytes(tests));
            logicFunctionsRead = true;
            return logicFunctions;
        }
    }
    LogicFunctions? logicFunctions;
    bool logicFunctionsRead;

    /// <summary>Every place a file name is: archives, and containers in them. Empty until the index is ready.</summary>
    public IEnumerable<(string Archive, string? Container)> PlacesOf(string name) =>
        places == null ? [] : places[name].Select(f => (f.Archive, f.Container));

    /// <summary>Every file name in the game that matches (each once). Empty until the index is ready.</summary>
    public IEnumerable<string> NamesWhere(Func<string, bool> match) => files == null ? [] : files.Keys.Where(match);

    // ---- Any chunk (the level editor's Add list) ------------------------------------------------

    List<string>? chunkNames;
    Dictionary<string, AsmContainer>? chunkGroups;       // "<chunk>" -> its group (<chunk>.rfgchunkx) in an asset list
    Dictionary<string, AsmPrimitive>? primitiveKinds;    // a file's primitive as some asset list gives it (its type, allocator, ...)
    Task<Dictionary<string, ChunkPlacement>>? placements;
    readonly Dictionary<uint, List<ObjectExample>> examples = [];  // filled with the placements (see ObjectExamples)
    readonly List<LogicRecipe> gameRecipes = [];                    // and every different state machine of the game's scripts
    readonly Dictionary<string, SortedSet<string>> scriptTexts = [];  // and the texts they pass, by "function|parameter number"

    /// <summary>
    /// The classes whose objects only Volition's tools could make (their data isn't described: a logic handler's
    /// script, a road's or spline's points, a rig's bones), by name: added as copies of the game's own.
    /// </summary>
    public static readonly string[] ExampleClasses = ["object_logic_handler", "object_path_road", "object_spline", "object_anim_rig", "constraint"];

    /// <summary>
    /// Objects of the game's levels of <see cref="ExampleClasses"/>, by class: up to 60 different ones a class (a rig
    /// by its rig_name, a handler by the text in its script, else one a zone), to add copies of. Found with
    /// <see cref="ChunkPlacements"/> (each zone read once).
    /// </summary>
    public async Task<IReadOnlyDictionary<uint, List<ObjectExample>>> ObjectExamples()
    {
        await ChunkPlacements();
        lock (examples) return examples.ToDictionary(e => e.Key, e => e.Value.ToList());
    }

    /// <summary>Every chunk (model of a building or prop: a .cchk_pc) in the game's files and ours, by name without extension. Empty until the index is ready.</summary>
    public IReadOnlyList<string> ChunkNames()
    {
        if (chunkNames != null) return chunkNames;
        if (files == null) return [];
        return chunkNames = files.Keys.Where(n => n.EndsWith(".cchk_pc", StringComparison.OrdinalIgnoreCase))
                                     .Select(n => n[..^".cchk_pc".Length]).Distinct(StringComparer.OrdinalIgnoreCase)
                                     .OrderBy(n => n, StringComparer.OrdinalIgnoreCase).ToList();
    }

    List<ChunkEntry>? chunkCatalog;

    /// <summary>Every chunk sorted for the Add list's browser: its type, category and setting (from its name) and the packs that have it. Empty until the index is ready.</summary>
    public IReadOnlyList<ChunkEntry> ChunkCatalog()
    {
        if (chunkCatalog != null) return chunkCatalog;
        if (files == null || places == null) return [];
        var entries = new List<ChunkEntry>();
        void Add(string name, string extension, bool stitch)
        {
            (string type, string category, string setting) = RfaEditor.ChunkCatalog.Classify(name);
            if (stitch) type = RfaEditor.ChunkCatalog.LevelGeometry;
            var where = places[name + extension]
                .Select(f => (Pack: RfaEditor.ChunkCatalog.PackName(f.Archive), Container: f.Container == null ? null : RfaEditor.ChunkCatalog.ContainerName(f.Container)))
                .Distinct().ToList();
            entries.Add(new ChunkEntry(name, type, category, setting, where, stitch));
        }
        foreach (string name in ChunkNames()) Add(name, ".cchk_pc", stitch: false);
        // The stitch pieces (level geometry) too, as a type of their own.
        foreach (string name in files.Keys.Where(n => n.EndsWith(".cstch_pc", StringComparison.OrdinalIgnoreCase)).Select(n => n[..^".cstch_pc".Length])
                                     .Distinct(StringComparer.OrdinalIgnoreCase).OrderBy(n => n, StringComparer.OrdinalIgnoreCase))
            Add(name, ".cstch_pc", stitch: true);
        return chunkCatalog = entries;
    }

    // Files the editor made (terrain pieces, textures given a zone's layer's name length), found by
    // name before the game's own: until saved into a level's containers they exist only here.
    readonly Dictionary<string, byte[]> generated = new(StringComparer.OrdinalIgnoreCase);
    readonly Dictionary<string, string> generatedLike = new(StringComparer.OrdinalIgnoreCase);     // a made header file → the game's file whose asset-list kind it takes
    readonly Dictionary<string, string> generatedTextures = new(StringComparer.OrdinalIgnoreCase); // a made texture's name → its header file

    /// <summary>Adds a file the editor made (see above); <paramref name="like"/>: the game's file whose kind (for the asset lists) it has.</summary>
    public void AddGenerated(string name, byte[] bytes, string? like = null)
    {
        lock (assetLock)
        {
            generated[name] = bytes;
            if (like != null) generatedLike[name] = like;
            models.Remove(name);
            if (name.EndsWith(".cvbm_pc", StringComparison.OrdinalIgnoreCase) || name.EndsWith(".cpeg_pc", StringComparison.OrdinalIgnoreCase))
                foreach (string texture in PegFile.TextureNames(bytes)) generatedTextures[texture] = name;
        }
    }

    /// <summary>Files (header files, each with its data file) as a package to bring into a level's containers, with their primitives for the asset lists; null, with why, if one can't be read.</summary>
    public ChunkPackage? FilesPackage(string name, IEnumerable<string> headers, out string? why)
    {
        ReadAssetLists();
        return Package(name, headers.ToList(), null, out why);
    }

    /// <summary>Every ground texture the game's zones (and the Guerrilla world's) use as a layer, with how many zones use it; read once, in the background.</summary>
    public Task<List<(string Texture, int Zones)>> GroundTextures() => groundTextures ??= Task.Run(() =>
    {
        var counts = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        foreach (string name in NamesWhere(n => n.EndsWith(".cterrain_pc", StringComparison.OrdinalIgnoreCase)).ToList())
        {
            try
            {
                if (ReadAnywhere(name) is { } bytes && RfaFormats.Meshes.ZoneTerrain.Read(bytes) is { } ground)
                    foreach (string texture in ground.Layers.Select(l => l.Texture).Distinct(StringComparer.OrdinalIgnoreCase))
                        counts[texture] = counts.GetValueOrDefault(texture) + 1;
            }
            catch (Exception ex) when (ex is InvalidDataException or NotSupportedException or ArgumentException) { }
        }
        return counts.OrderBy(kv => kv.Key, StringComparer.OrdinalIgnoreCase).Select(kv => (kv.Key, kv.Value)).ToList();
    });
    Task<List<(string Texture, int Zones)>>? groundTextures;

    /// <summary>Whether a file is one the editor made (see <see cref="AddGenerated"/>).</summary>
    public bool IsGenerated(string name) => generated.ContainsKey(name);

    /// <summary>A file by name from wherever the index says it is, as the game sees it; null if none has it.</summary>
    public byte[]? ReadAnywhere(string name)
    {
        if (generated.TryGetValue(name, out byte[]? made)) return made;
        if (files == null || !files.TryGetValue(name, out IndexedFile? found)) return null;
        Packfile pack = Pack(found.Archive, found.Container);
        return pack.Find(name) is { } entry ? pack.Read(entry) : null;
    }

    void ReadAssetLists()
    {
        if (chunkGroups != null) return;
        var groups = new Dictionary<string, AsmContainer>(StringComparer.OrdinalIgnoreCase);
        var kinds = new Dictionary<string, AsmPrimitive>(StringComparer.OrdinalIgnoreCase);
        foreach ((string archive, string list) in workspace.AssetLists())
        {
            AsmFile asm;
            try { asm = AsmFile.Read(workspace.Read(archive, list)); }
            catch (InvalidDataException) { continue; }
            foreach (AsmContainer c in asm.Containers)
            {
                if (c.Sizes.Count == 0 && c.Name.EndsWith(".rfgchunkx", StringComparison.OrdinalIgnoreCase))
                    groups.TryAdd(c.Name[..^".rfgchunkx".Length], c);
                foreach (AsmPrimitive primitive in c.Primitives) kinds.TryAdd(primitive.Name, primitive);
            }
        }
        chunkGroups = groups;
        primitiveKinds = kinds;
    }

    /// <summary>Whether an asset list of this archive, or of the archives of its world beside it (rfg_l0 and rfg_l1), has the chunk's own group: the world finds the chunk without it being brought in.</summary>
    public bool WorldHasChunk(string chunk, string archive)
    {
        string folder = Path.GetDirectoryName(archive) ?? "", world = WorldOf(archive);
        foreach ((string other, string list) in workspace.AssetLists())
        {
            if (!(Path.GetDirectoryName(other) ?? "").Equals(folder, StringComparison.OrdinalIgnoreCase) ||
                !WorldOf(other).Equals(world, StringComparison.OrdinalIgnoreCase)) continue;
            try
            {
                if (AsmFile.Read(workspace.Read(other, list)).Containers.Any(c => c.Name.Equals(chunk + ".rfgchunkx", StringComparison.OrdinalIgnoreCase)))
                    return true;
            }
            catch (InvalidDataException) { }
        }
        return false;
    }

    /// <summary>An archive's world: its name without _l0, _l1 or _precache (rfg_l1.vpp_pc: rfg).</summary>
    static string WorldOf(string archive)
    {
        string name = Path.GetFileNameWithoutExtension(archive);
        var match = System.Text.RegularExpressions.Regex.Match(name, @"^(.*?)(_l\d+|_precache)$", System.Text.RegularExpressions.RegexOptions.IgnoreCase);
        return match.Success ? match.Groups[1].Value : name;
    }

    /// <summary>
    /// What a zone needs to show a chunk from anywhere in the game: its files (the model, its g-file,
    /// and its textures' files), a primitive for each header file and the chunk's own group for the
    /// asset lists. The files are those the chunk's own group in an asset list names (as the game
    /// streams it), else the model and the texture files holding the textures its materials name.
    /// Null, with why, if they can't all be found.
    /// </summary>
    public ChunkPackage? PackageOf(string chunk, out string? why)
    {
        why = null;
        ReadAssetLists();
        var headers = new List<string>();
        AsmContainer? own = chunkGroups!.GetValueOrDefault(chunk);
        if (own != null && own.Primitives.Count > 0)
            headers.AddRange(own.Primitives.Select(p => p.Name));
        else
        {
            string modelName = chunk + ".cchk_pc";
            byte[]? header = ReadAnywhere(modelName), data = ReadAnywhere(ModelFile.DataFileName(modelName));
            if (header == null || data == null)
            {
                why = $"{modelName} or its g-file isn't in the game's files";
                return null;
            }
            headers.Add(modelName);
            try
            {
                ModelFile model = ModelFile.Read(modelName, header, data);
                foreach (string texture in model.Materials.SelectMany(m => m.Textures.Select(t => t.Texture)).Distinct(StringComparer.OrdinalIgnoreCase))
                    if (textures != null && textures.TryGetValue(texture, out TextureRef? at) && !headers.Contains(at.PegHeader, StringComparer.OrdinalIgnoreCase))
                        headers.Add(at.PegHeader);
            }
            catch (Exception ex) when (ex is InvalidDataException or NotSupportedException)
            {
                why = $"{modelName} couldn't be read ({ex.Message})";
                return null;
            }
        }

        return Package(chunk, headers, own, out why);
    }

    /// <summary>
    /// What a zone's level needs to show a stitch piece (level geometry) from anywhere in the game: its
    /// model and g-file, the texture files holding the textures its materials name, and a primitive for
    /// each header file for the asset lists (no group of its own: stitch pieces are named by the
    /// stitch list). Null, with why, if they can't all be found.
    /// </summary>
    public ChunkPackage? StitchPackageOf(string piece, out string? why)
    {
        why = null;
        ReadAssetLists();
        string modelName = piece + ".cstch_pc";
        byte[]? header = ReadAnywhere(modelName), data = ReadAnywhere(ModelFile.DataFileName(modelName));
        if (header == null || data == null)
        {
            why = $"{modelName} or its g-file isn't in the game's files";
            return null;
        }
        var headers = new List<string> { modelName };
        try
        {
            ModelFile model = ModelFile.Read(modelName, header, data);
            foreach (string texture in model.Materials.SelectMany(m => m.Textures.Select(t => t.Texture)).Distinct(StringComparer.OrdinalIgnoreCase))
                if (textures != null && textures.TryGetValue(texture, out TextureRef? at) && !headers.Contains(at.PegHeader, StringComparer.OrdinalIgnoreCase))
                    headers.Add(at.PegHeader);
        }
        catch (Exception ex) when (ex is InvalidDataException or NotSupportedException)
        {
            why = $"{modelName} couldn't be read ({ex.Message})";
            return null;
        }
        return Package(piece, headers, null, out why);
    }

    RfaFormats.Guerrilla.GuerrillaEffectTables.EffectLookup? effectLookup;

    /// <summary>
    /// The effect tables as the game reads them (the guerrilla_world mod's, Armageddon's with
    /// Guerrilla's added, where the mod has them; else Armageddon's own), for following an object's
    /// level object to its effects' files. Null if they can't be read.
    /// </summary>
    RfaFormats.Guerrilla.GuerrillaEffectTables.EffectLookup? EffectLookup()
    {
        if (effectLookup != null) return effectLookup;
        string? Text(string table)
        {
            string mods = Path.Combine(workspace.ModFolder, "mods", "guerrilla_world", table);
            byte[]? bytes = File.Exists(mods) ? File.ReadAllBytes(mods) : ReadAnywhere(table);
            return bytes == null ? null : System.Text.Encoding.Latin1.GetString(bytes);
        }
        if (Text("level_objects.xtbl") is not string objects || Text("effects.xtbl") is not string effects ||
            Text("vfx.xtbl") is not string visuals || Text("explosions.xtbl") is not string explosions) return null;
        try { return effectLookup = new(objects, effects, visuals, explosions); }
        catch (InvalidDataException) { return null; }
    }

    /// <summary>
    /// The files the effects of these level objects (objects' Props) need in a level, those
    /// <paramref name="present"/> says the level hasn't: each effect's .cefct_pc and .gefct_pc, and
    /// the textures it names that are files of their own (a converted Guerrilla effect's .cvbm_pc and
    /// .gvbm_pc; Armageddon's own effects' textures are all in effects.vpp_pc, loaded everywhere). As a
    /// package to bring into a level's containers; null if there is nothing to bring.
    /// </summary>
    public ChunkPackage? EffectPackageFor(IEnumerable<string> levelObjects, Func<string, bool> present, out List<string> effects)
    {
        effects = [];
        ReadAssetLists();
        if (files == null || EffectLookup() is not { } lookup) return null;
        var headers = new List<string>();
        foreach (string file in levelObjects.Distinct(StringComparer.OrdinalIgnoreCase).SelectMany(lookup.FilesOf).Distinct(StringComparer.OrdinalIgnoreCase))
        {
            string header = file + ".cefct_pc";
            if (present(header) || ReadAnywhere(header) is not byte[] bytes) continue;
            effects.Add(file);
            headers.Add(header);
            foreach (string texture in EffectTextures(bytes))
            {
                string textureHeader = Path.GetFileNameWithoutExtension(texture) + ".cvbm_pc";
                if (files.ContainsKey(textureHeader) && !present(textureHeader) && !headers.Contains(textureHeader, StringComparer.OrdinalIgnoreCase))
                    headers.Add(textureHeader);
            }
        }
        return headers.Count == 0 ? null : Package("effects", headers, null, out _);
    }

    /// <summary>
    /// The animations these animated rigs (object_anim_rig's rig_name: anim_rig_objects.xtbl entries) play, those
    /// <paramref name="present"/> says the level hasn't: every state's and action's file (x.animx, kept as x.anim_pc).
    /// A level only has them where its world's containers do (Infestation's precache has the monoliths' and tentacles';
    /// a Ruin level none), and a rig without them doesn't move. As a package to bring into a level's containers; null if
    /// there is nothing to bring. <paramref name="missing"/> names the rigs it was for.
    /// </summary>
    public ChunkPackage? RigAnimationPackageFor(IEnumerable<string> rigs, Func<string, bool> present, out List<string> missing)
    {
        missing = [];
        ReadAssetLists();
        if (files == null || Table("anim_rig_objects.xtbl") is not { } table) return null;
        var headers = new List<string>();
        foreach (string rig in rigs.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            if (table.Entries.FirstOrDefault(e => XtblFile.EntryName(e).Equals(rig, StringComparison.OrdinalIgnoreCase)) is not { } entry) continue;
            bool any = false;
            foreach (string clip in RigFiles(entry).Where(f => f.EndsWith(".animx", StringComparison.OrdinalIgnoreCase)))
            {
                string header = Path.ChangeExtension(clip, ".anim_pc");
                if (present(header) || headers.Contains(header, StringComparer.OrdinalIgnoreCase) || !files.ContainsKey(header)) continue;
                headers.Add(header);
                any = true;
            }
            if (any) missing.Add(rig);
        }
        return headers.Count == 0 ? null : Package("rig animations", headers, null, out _);
    }

    /// <summary>Every file an entry names (its Filename fields, at any depth).</summary>
    static IEnumerable<string> RigFiles(XtblElement element)
    {
        foreach (XtblElement child in element.Elements)
        {
            if (child.Name.Equals("Filename", StringComparison.OrdinalIgnoreCase) && !child.HasElements && child.Value.Trim().Length > 0) yield return child.Value.Trim();
            foreach (string inside in RigFiles(child)) yield return inside;
        }
    }

    /// <summary>The textures a version-36 effect file names (its bitmaps: a count at +0x1C, a list of their names' offsets at +0x20).</summary>
    static IEnumerable<string> EffectTextures(byte[] effect)
    {
        if (effect.Length < 0x24) yield break;
        int count = BitConverter.ToInt32(effect, 0x1C), list = BitConverter.ToInt32(effect, 0x20);
        for (int i = 0; i < count && list + i * 4 + 4 <= effect.Length; i++)
        {
            int at = BitConverter.ToInt32(effect, list + i * 4);
            if (at <= 0 || at >= effect.Length) continue;
            int end = Array.IndexOf(effect, (byte)0, at);
            if (end > at) yield return System.Text.Encoding.Latin1.GetString(effect, at, end - at);
        }
    }

    /// <summary>The files, primitives and own group (see <see cref="PackageOf"/>) of a chunk or stitch piece, from its header files.</summary>
    ChunkPackage? Package(string chunk, List<string> headers, AsmContainer? own, out string? why)
    {
        why = null;
        var found = new List<(string Name, byte[] Data)>();
        var primitives = new List<AsmPrimitive>();
        foreach (string header in headers)
        {
            if (ReadAnywhere(header) is not byte[] bytes)
            {
                why = $"{header} isn't in the game's files";
                return null;
            }
            found.Add((header, bytes));
            string dataName = DataFileOf(header);
            byte[]? data = dataName.Length > 0 ? ReadAnywhere(dataName) : null;
            if (data != null) found.Add((dataName, data));
            AsmPrimitive? kind = primitiveKinds!.GetValueOrDefault(header) ??
                                 (generatedLike.TryGetValue(header, out string? like) ? primitiveKinds!.GetValueOrDefault(like) : null);
            (byte type, byte allocator, byte flags, byte split) = kind != null ? (kind.Type, kind.Allocator, kind.Flags, kind.SplitIndex)
                : Path.GetExtension(header).ToLowerInvariant() switch
                {
                    ".cchk_pc" or ".cstch_pc" => ((byte)2, (byte)1, (byte)6, (byte)0),
                    ".cefct_pc" => ((byte)13, (byte)1, (byte)6, (byte)0),
                    ".anim_pc" => ((byte)16, (byte)1, (byte)2, (byte)0),
                    ".cpeg_pc" or ".cvbm_pc" => ((byte)1, (byte)1, (byte)6, (byte)1),
                    _ => ((byte)0, (byte)0, (byte)0, (byte)0),
                };
            if (type == 0 && kind == null)
            {
                why = $"the kind of {header} for the asset lists isn't known";
                return null;
            }
            primitives.Add(new AsmPrimitive { Name = header, Type = type, Allocator = allocator, Flags = flags, SplitIndex = split,
                                              HeaderSize = bytes.Length, DataSize = data?.Length ?? 0 });
        }
        var group = new AsmContainer { Name = chunk + ".rfgchunkx", Type = own?.Type ?? 14, Flags = own?.Flags ?? 0x300 };
        group.Primitives.AddRange(primitives.Select(p => new AsmPrimitive
        {
            Name = p.Name, Type = p.Type, Allocator = p.Allocator, Flags = p.Flags, SplitIndex = p.SplitIndex, HeaderSize = p.HeaderSize, DataSize = p.DataSize,
        }));
        return new ChunkPackage(chunk, found, primitives, group);
    }

    static string DataFileOf(string file)
    {
        string extension = Path.GetExtension(file);
        return extension.Length > 2 && extension[1] == 'c' ? Path.ChangeExtension(file, "g" + extension[2..]) : "";
    }

    /// <summary>
    /// Where the game's levels (and the Guerrilla world) place each chunk: one object naming it, in a
    /// zone file, for a copy with its properties (and the pieces joined to it). Found in the
    /// background the first time it's asked for (every zone file is read), with an editor-side
    /// copy of the workspace, so nothing here is touched from another thread.
    /// </summary>
    public Task<Dictionary<string, ChunkPlacement>> ChunkPlacements()
    {
        if (placements != null) return placements;
        if (files == null) return Task.FromResult(new Dictionary<string, ChunkPlacement>(StringComparer.OrdinalIgnoreCase));
        // Each zone and layer once, from an archive directly where it can be (a container is read whole).
        var zones = places!.Where(g => g.Key.EndsWith(".rfgzone_pc", StringComparison.OrdinalIgnoreCase) || g.Key.EndsWith(".layer_pc", StringComparison.OrdinalIgnoreCase))
                           .Where(g => !g.Key.StartsWith("p_", StringComparison.OrdinalIgnoreCase))
                           .Select(g => g.OrderBy(f => f.Container == null ? 0 : 1).First())
                           .OrderBy(f => f.Archive, StringComparer.OrdinalIgnoreCase).ThenBy(f => f.Container ?? "", StringComparer.OrdinalIgnoreCase)
                           .ToList();
        var own = new Workspace(workspace.GameFolder, workspace.ModFolder);
        ZoneNames zoneNames = ZoneNames;                 // (read here, on the editor's thread)
        LogicFunctions? functions = LogicFunctions;
        uint handlerClass = NameHashes.ClassHash("object_logic_handler"), scriptProperty = NameHashes.PropertyHash("logic_handler_buffer");
        var recipeKeys = new HashSet<string>(StringComparer.Ordinal);
        return placements = Task.Run(() =>
        {
            var found = new Dictionary<string, ChunkPlacement>(StringComparer.OrdinalIgnoreCase);
            uint chunkName = NameHashes.PropertyHash("chunk_name");
            var exampleClasses = ExampleClasses.ToDictionary(NameHashes.ClassHash, c => c);
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (IndexedFile file in zones)
            {
                try
                {
                    Packfile pack = file.Container == null ? own.Archive(file.Archive) : own.Container(file.Archive, file.Container);
                    if (pack.Find(file.Name) is not { } entry) continue;
                    ZoneFile zoneFile = ZoneFile.Read(pack.Read(entry));
                    Dictionary<uint, uint>? classOf = null;
                    foreach (ZoneObject obj in zoneFile.Objects)
                    {
                        if (obj.ClassHash == handlerClass && obj.Property(scriptProperty) is { } held && LogicScript.Read(held.Value) is { } script)
                        {
                            classOf ??= zoneFile.Objects.GroupBy(o => o.Handle).ToDictionary(g => g.Key, g => g.First().ClassHash);
                            NoteScript(script, file, classOf, zoneNames, functions, recipeKeys);
                        }
                        if (obj.Property(chunkName) is { Type: ZoneProperty.TextType } name)
                            found.TryAdd(Path.GetFileNameWithoutExtension(name.Text), new ChunkPlacement(file.Archive, file.Container, file.Name, obj.Handle));
                        if (!exampleClasses.TryGetValue(obj.ClassHash, out string? className)) continue;
                        string label = ExampleLabel(obj) is { Length: > 0 } l ? l : Path.GetFileNameWithoutExtension(file.Name);
                        lock (examples)
                        {
                            if (!examples.TryGetValue(obj.ClassHash, out List<ObjectExample>? list)) examples[obj.ClassHash] = list = [];
                            if (list.Count < 60 && seen.Add(className + "|" + label))
                                list.Add(new ObjectExample(className, label, new ChunkPlacement(file.Archive, file.Container, file.Name, obj.Handle)));
                        }
                    }
                }
                catch (Exception) { }  // (a file that can't be read gives no places)
            }
            return found;
        });
    }

    /// <summary>
    /// Every state machine of the game's scripts, each different one once (by what it does, whatever objects it
    /// names), as recipes to add to a script (see <see cref="LogicRecipe.FromGame"/>). Found with
    /// <see cref="ChunkPlacements"/>.
    /// </summary>
    public async Task<IReadOnlyList<LogicRecipe>> GameRecipes()
    {
        await ChunkPlacements();
        lock (gameRecipes) return gameRecipes.OrderBy(r => r.Source, StringComparer.OrdinalIgnoreCase).ThenBy(r => r.Name, StringComparer.OrdinalIgnoreCase).ToList();
    }

    /// <summary>
    /// Texts for a function's text parameter: the names in the table it names (an explosion's in explosions.xtbl, an
    /// objective's in mission_objectives.xtbl, ...), then those the game's scripts pass it (once they've been read,
    /// see <see cref="ChunkPlacements"/>).
    /// </summary>
    public IReadOnlyList<string> ScriptTextChoices(string function, int parameter, string parameterName)
    {
        var choices = new List<string>();
        if (TableForText(function, parameterName) is { } tableName && Table(tableName) is { } table)
            choices.AddRange(table.Entries.Select(XtblFile.EntryName).Where(n => !string.IsNullOrWhiteSpace(n)).Distinct(StringComparer.OrdinalIgnoreCase));
        lock (scriptTexts)
            if (scriptTexts.TryGetValue($"{function}|{parameter}", out SortedSet<string>? used))
                choices.AddRange(used.Where(u => !choices.Contains(u, StringComparer.OrdinalIgnoreCase)));
        return choices;
    }

    /// <summary>The table whose entries' names a text parameter takes, by its name (and function); null if none.</summary>
    static string? TableForText(string function, string parameterName)
    {
        string name = parameterName.Trim().ToLowerInvariant();
        if (function.Contains("explosion", StringComparison.OrdinalIgnoreCase) && name is "name" or "explosion name" or "explosion") return "explosions.xtbl";
        return name switch
        {
            "explosion name" => "explosions.xtbl",
            "effect" or "effect name" => "effects.xtbl",
            "objective" or "objective name" or "objective_name" => "mission_objectives.xtbl",
            "persona" or "voice persona" => "audio_personas.xtbl",
            "weapon" or "weapon name" or "weapon type" or "weapon_info" => "weapons.xtbl",
            "item_type" => "items_3d.xtbl",
            "orders type" => "orders_types.xtbl",
            "mission" or "mission name" or "mission_name" => "missions.xtbl",
            "projectile_info" => "projectiles.xtbl",
            "shake style" => "camera_shake.xtbl",
            "achievement" => "achievements.xtbl",
            "audio log name" => "audio_logs.xtbl",
            "anim action" or "action_name" or "action name" or "rig_action" or "human_action" => "anim_actions.xtbl",
            "anim_state_name" or "anim state" => "anim_states.xtbl",
            _ => null,
        };
    }

    /// <summary>A script of the game's (read while looking through the levels): its state machines as recipes, its texts.</summary>
    void NoteScript(LogicScript script, IndexedFile file, Dictionary<uint, uint> classOf, ZoneNames zoneNames, LogicFunctions? functions, HashSet<string> keys)
    {
        foreach (LogicCall call in script.Calls())
            for (int i = 0; i < call.Arguments.Count; i++)
                if (call.Arguments[i] is { Type: LogicValueType.String } text && text.Text.Text.Length > 0)
                    lock (scriptTexts)
                    {
                        if (!scriptTexts.TryGetValue($"{call.Function}|{i}", out SortedSet<string>? set))
                            scriptTexts[$"{call.Function}|{i}"] = set = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
                        set.Add(text.Text.Text);
                    }
        var describe = new LogicText(script, null, null);
        string where = file.Container != null
            ? $"{Path.GetFileNameWithoutExtension(file.Container)} / {Path.GetFileNameWithoutExtension(file.Name)}"
            : Path.GetFileNameWithoutExtension(file.Name);
        foreach (LogicObject machine in script.Objects)
        {
            // The same state machine naming other objects (a copy for each building, each squad) is one recipe.
            string key = System.Text.RegularExpressions.Regex.Replace(describe.Describe(machine), @"\b[0-9A-F]{8}\b", "#");
            lock (gameRecipes)
            {
                if (!keys.Add(key)) continue;
                gameRecipes.Add(LogicRecipe.FromGame(script, machine, where, functions,
                    h => classOf.TryGetValue(h, out uint c) ? zoneNames.Class(c) : null));
            }
        }
    }

    /// <summary>What tells an example object from others of its class: a rig's name, the first words in a handler's script.</summary>
    static string ExampleLabel(ZoneObject obj)
    {
        if (obj.Property("rig_name") is { Type: ZoneProperty.TextType } rig) return rig.Text;
        if (obj.Property("template") is { Type: ZoneProperty.TextType } template) return template.Text;
        if (obj.Property("logic_handler_buffer") is { } script)
            return string.Join(" ", System.Text.RegularExpressions.Regex.Matches(System.Text.Encoding.Latin1.GetString(script.Value), "[A-Za-z_][A-Za-z0-9_ ]{3,}")
                                        .Select(m => m.Value.Trim()).Take(2));
        return "";
    }

    /// <summary>A model by its c-file name, or null if it can't be found or read.</summary>
    public ModelFile? Model(string name, string nearArchive, string? nearContainer)
    {
        lock (assetLock)
        {
            if (models.TryGetValue(name, out ModelFile? known) && known != null) return known;
            ModelFile? model = null;
            try
            {
                if (generated.TryGetValue(name, out byte[]? madeHeader) && generated.TryGetValue(ModelFile.DataFileName(name), out byte[]? madeData))
                    model = ModelFile.Read(name, madeHeader, madeData);
                else if (Find(name, nearArchive, nearContainer) is (Packfile pack, PackfileEntry header) &&
                    pack.Find(ModelFile.DataFileName(name)) is PackfileEntry data)
                    model = ModelFile.Read(name, pack.Read(header), pack.Read(data));
            }
            catch (InvalidDataException) { }
            catch (NotSupportedException) { }
            if (model != null || IndexReady) models[name] = model;  // a miss is only final once the index is ready
            return model;
        }
    }

    /// <summary>
    /// A texture by name as a picture at most <paramref name="maxSize"/> pixels wide, or null. With
    /// <paramref name="alpha"/> its alpha channel is kept as see-through (for decals); otherwise it
    /// is shown opaque, as some textures use alpha for other things.
    /// </summary>
    public BitmapSource? Texture(string name, string nearArchive, string? nearContainer, int maxSize, bool alpha = false)
    {
        lock (assetLock)
        {
            string key = $"{name}@{maxSize}{(alpha ? "+alpha" : "")}";
            if (bitmaps.TryGetValue(key, out BitmapSource? known) && known != null) return known;
            BitmapSource? bitmap = FindTexture(name, nearArchive, nearContainer) is var (peg, texture)
                ? Bitmaps.FromRgba(peg.Decode(texture, maxSize), alpha)
                : null;
            if (bitmap != null || IndexReady) bitmaps[key] = bitmap;
            return bitmap;
        }
    }

    /// <summary>A game table (.xtbl) by name, read once; null if it can't be found (or the index isn't ready yet).</summary>
    public XtblFile? Table(string name)
    {
        if (tables.TryGetValue(name, out XtblFile? known)) return known;
        XtblFile? table = null;
        if (files != null && files.TryGetValue(name, out IndexedFile? found) && Pack(found.Archive, found.Container) is var pack && pack.Find(name) is { } entry)
            table = XtblFile.FromBytes(pack.Read(entry));
        if (table != null || IndexReady) tables[name] = table;
        return table;
    }

    /// <summary>The name of the texture file (its header, .cpeg_pc or .cvbm_pc) holding a texture, as <see cref="FindTexture"/> finds it; null if none.</summary>
    public string? TextureFileName(string name, string nearArchive, string? nearContainer)
    {
        if (generatedTextures.TryGetValue(name, out string? madeHeader)) return madeHeader;
        if (nearContainer != null)
        {
            Packfile pack = workspace.Container(nearArchive, nearContainer);
            foreach (PackfileEntry entry in pack.Entries.Where(e => e.Extension is "cpeg_pc" or "cvbm_pc"))
                if (PegFile.TextureNames(pack.Read(entry)).Contains(name, StringComparer.OrdinalIgnoreCase))
                    return entry.Name;
        }
        return textures != null && textures.TryGetValue(name, out TextureRef? at) ? at.PegHeader : null;
    }

    /// <summary>The texture file holding a texture, and the texture in it.</summary>
    public (PegFile, PegTexture)? FindTexture(string name, string nearArchive, string? nearContainer)
    {
        if (generatedTextures.TryGetValue(name, out string? madeHeader) && generated.TryGetValue(madeHeader, out byte[]? h) &&
            PegFile.DataFileName(madeHeader) is { } madeData && generated.TryGetValue(madeData, out byte[]? d))
        {
            PegFile made = PegFile.Read(h, d);
            if (made.Textures.FirstOrDefault(t => t.Name.Equals(name, StringComparison.OrdinalIgnoreCase)) is { } texture) return (made, texture);
        }
        if (nearContainer != null && FindTextureIn(workspace.Container(nearArchive, nearContainer), name) is { } local) return local;
        if (textures != null && textures.TryGetValue(name, out TextureRef? at))
            return ReadTexture(Pack(at.Archive, at.Container), at.PegHeader, name);
        return null;
    }

    /// <summary>
    /// Any file by name, as the game sees it, and where it was found: in the container given, else
    /// in the archive given, else anywhere (once the index is ready). Null if it can't be found.
    /// </summary>
    public (byte[] Data, string Archive, string? Container)? ReadFile(string name, string nearArchive, string? nearContainer)
    {
        if (generated.TryGetValue(name, out byte[]? made)) return (made, nearArchive, nearContainer);
        if (nearContainer != null && workspace.Container(nearArchive, nearContainer) is var container && container.Find(name) is { } inContainer)
            return (container.Read(inContainer), nearArchive, nearContainer);
        // (A file loose in an archive as the game sees it: its override, or edit, if it has one.)
        if (workspace.Archive(nearArchive).Find(name) != null)
            return (workspace.Read(nearArchive, name), nearArchive, null);
        if (files != null && files.TryGetValue(name, out IndexedFile? found) && Pack(found.Archive, found.Container).Find(name) is { } entry)
            return (found.Container == null ? workspace.Read(found.Archive, name) : Pack(found.Archive, found.Container).Read(entry), found.Archive, found.Container);
        return null;
    }

    /// <summary>A file by name: in the container given, else in the archive given, else anywhere (index).</summary>
    (Packfile, PackfileEntry)? Find(string name, string nearArchive, string? nearContainer)
    {
        if (nearContainer != null && workspace.Container(nearArchive, nearContainer) is var container && container.Find(name) is { } inContainer)
            return (container, inContainer);
        if (workspace.Archive(nearArchive).Find(name) is { } inArchive)
            return (workspace.Archive(nearArchive), inArchive);
        if (files != null && files.TryGetValue(name, out IndexedFile? found))
        {
            Packfile pack = Pack(found.Archive, found.Container);
            if (pack.Find(name) is { } entry) return (pack, entry);
        }
        return null;
    }

    /// <summary>An archive, or a container in it, as the game sees it.</summary>
    Packfile Pack(string archive, string? container) => container == null ? workspace.Archive(archive) : workspace.Container(archive, container);

    static (PegFile, PegTexture)? FindTextureIn(Packfile pack, string name)
    {
        foreach (PackfileEntry entry in pack.Entries.Where(e => e.Extension is "cpeg_pc" or "cvbm_pc"))
            if (PegFile.TextureNames(pack.Read(entry)).Contains(name, StringComparer.OrdinalIgnoreCase))
                return ReadTexture(pack, entry.Name, name);
        return null;
    }

    /// <summary>One texture from a given texture file pair in a packfile.</summary>
    static (PegFile, PegTexture)? ReadTexture(Packfile pack, string headerName, string name)
    {
        if (pack.Find(headerName) is not { } header || pack.Find(PegFile.DataFileName(headerName)!) is not { } data) return null;
        PegFile peg = PegFile.Read(pack.Read(header), pack.Read(data));
        PegTexture? texture = peg.Textures.FirstOrDefault(t => t.Name.Equals(name, StringComparison.OrdinalIgnoreCase));
        return texture == null ? null : (peg, texture);
    }
}

static class Bitmaps
{
    /// <summary>A picture for the screen. Without <paramref name="alpha"/> every pixel is shown opaque.</summary>
    public static BitmapSource FromRgba(RgbaImage image, bool alpha)
    {
        var bgra = new byte[image.Pixels.Length];
        for (int i = 0; i < bgra.Length; i += 4)
        {
            bgra[i] = image.Pixels[i + 2];
            bgra[i + 1] = image.Pixels[i + 1];
            bgra[i + 2] = image.Pixels[i];
            bgra[i + 3] = alpha ? image.Pixels[i + 3] : (byte)255;
        }
        BitmapSource bitmap = BitmapSource.Create(image.Width, image.Height, 96, 96, PixelFormats.Bgra32, null, bgra, image.Width * 4);
        bitmap.Freeze();
        return bitmap;
    }
}

/// <summary>A chunk's files and asset list entries, to bring it into a zone (see <see cref="GameAssets.PackageOf"/>).</summary>
public sealed record ChunkPackage(string Chunk, List<(string Name, byte[] Data)> Files, List<AsmPrimitive> Primitives, AsmContainer Group);

/// <summary>An object placing a chunk: in which zone file (where it is) and its handle.</summary>
public sealed record ChunkPlacement(string Archive, string? Container, string Zone, uint Handle);

/// <summary>An object of one of the game's levels to add a copy of (see <see cref="GameAssets.ObjectExamples"/>).</summary>
public sealed record ObjectExample(string Class, string Label, ChunkPlacement Place);
