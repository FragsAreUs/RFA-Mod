using System.IO;
using System.Numerics;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using RfaFormats;
using RfaFormats.Levels;
using RfaFormats.Meshes;
using RfaFormats.Textures;
using RfaFormats.Zones;

namespace RfaEditor;

/// <summary>
/// Makes a brand-new level for one of the game's modes (Ruin, Infestation) and lists it in that mode's
/// map list. See the project's rfa-new-level.md. Nothing of another level's is in it:
///
///   - Its zone and persistent zone are made empty and given new objects, from the game's zone schema
///     with the editor's settings: the zone object (its terrain, its spawn resources), the player's
///     starts (in the persistent zone too, as the game's levels have them), the spectator's intro
///     camera, the mode's own (Ruin: an exo spawn; Infestation: the objective and four enemy spawns
///     round the edge), and one environment box round the zone with the chosen sky. All on the new
///     ground, round its middle.
///   - Its ground is made from nothing (a flat square of the chosen size in the middle of the zone,
///     the height field, sub-zones, far-away ground, a blank blend map and a plain colour map; no
///     foliage, roads, walls or occluders), or there is none (for a cave or interior built from pieces).
///     The terrain files' materials (which ground textures the shader draws, and names for its blend
///     and colour maps) are the only part taken from the game: from the level whose ground textures you
///     chose, as there is no other way to describe them. The ground textures go with the level.
///   - Its stitch list is empty; its navigation new (Ruin none; Infestation a flat mesh over the new
///     ground for its enemies, with its Havok tree; with no ground, none).
///   - Its asset list keeps only the groups the game loads a level of the mode by (the first of the
///     mode's levels has them: its zone group and cells, ns_base, ns_precache), listing only the new
///     level's files, its ground textures and its sky's.
///   - Its map list row, its strings, its pictures and its spawn resources are its own (the row laid out
///     as the mode's rows are; the spawn resources start as what the mode's first level can spawn, to
///     be changed with Spawn resources). With no picture given, a title card with its name is made.
///
/// The level: build\pc\cache\&lt;name&gt;.vpp_pc, a new archive in overrides\ (the editor's own: it opens
/// and saves it as any level, its edits kept in edits\), its files in its ns_base container (as
/// Infestation's levels keep theirs; its zone group, 1_c0807, names its zone and the game finds it
/// there). The game reads its tables and strings at start-up.
/// </summary>
sealed class NewLevel
{
    public enum Mode { Ruin, Infestation }

    const string Cache = @"build\pc\cache\";
    const string Tables = Cache + "table.vpp_pc", Misc = Cache + "misc.vpp_pc", Interface = Cache + "interface.vpp_pc";
    public const string DefaultSky = "env_sur_day_sky";

    // Where a single-zone level is in the world: every one of the game's (Ruin's, Infestation's) has its
    // zone object at (127.5, 0, -127.5), its zone the square from (0, -255) to (255, 0), place 0x88.
    static readonly Vector2 ZoneCorner = new(0, -255);
    const float ZoneWidth = 255;
    const byte ZonePlace = 0x88;

    public static (string Table, string Row, string Key) TableOf(Mode mode) => mode == Mode.Ruin
        ? ("wrecking_crew_maps.xtbl", "wc_map_list", "file_name")
        : ("mp_levels.xtbl", "mp_level_list", "Filename");

    /// <summary>The levels of a mode (their file names), from its table as the game sees it.</summary>
    public static List<string> LevelsOf(Workspace workspace, Mode mode)
    {
        var (table, row, key) = TableOf(mode);
        try { return LevelFiles.Keys(Encoding.Latin1.GetString(workspace.Read(Tables, table)), row, key); }
        catch (Exception ex) when (ex is IOException or InvalidDataException) { return []; }
    }

    /// <summary>The game's levels (of both modes) that have ground, whose ground textures a new level can have.</summary>
    public static List<string> LevelsWithGround(Workspace workspace, GameAssets assets) =>
        LevelsOf(workspace, Mode.Ruin).Concat(LevelsOf(workspace, Mode.Infestation))
            .Where(l => File.Exists(Path.Combine(workspace.GameFolder, Cache + l + ".vpp_pc")))
            .Where(l => assets.ReadAnywhere(l + ".cterrain_pc") is { Length: > 64 * 1024 })  // (a level with none has a small one)
            .Distinct(StringComparer.OrdinalIgnoreCase).ToList();

    readonly Workspace workspace;
    readonly GameAssets assets;
    readonly List<string> report = [];

    public NewLevel(Workspace workspace, GameAssets assets)
    {
        this.workspace = workspace;
        this.assets = assets;
    }

    /// <summary>A level of the game's as it is: its archive's loose files, its asset list, and the files of its own (its name in theirs).</summary>
    sealed record GameLevel(string Name, Dictionary<string, byte[]> Loose, AsmFile Asm, Dictionary<string, byte[]> Own, bool Compressed)
    {
        public bool IsOwn(string file) => file.Contains(Name, StringComparison.OrdinalIgnoreCase);
        public string? Precache => Loose.Keys.FirstOrDefault(f => f.EndsWith(".precache", StringComparison.OrdinalIgnoreCase));
    }

    GameLevel ReadLevel(string level)
    {
        string archive = Cache + level + ".vpp_pc";
        Packfile source = workspace.Archive(archive);
        var loose = source.Entries.ToDictionary(e => e.Name, e => source.Read(e), StringComparer.OrdinalIgnoreCase);
        if (!loose.TryGetValue(level + ".asm_pc", out byte[]? asmBytes)) throw new InvalidDataException($"{archive} has no {level}.asm_pc");
        AsmFile asm = AsmFile.Read(asmBytes);
        bool Own(string file) => file.Contains(level, StringComparison.OrdinalIgnoreCase);
        var own = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
        if (loose.TryGetValue("ns_base.str2_pc", out byte[]? nsBytes))
        {
            Packfile nsBase = Packfile.FromBytes("ns_base.str2_pc", nsBytes);
            foreach (PackfileEntry e in nsBase.Entries.Where(x => Own(x.Name))) own[e.Name] = nsBase.Read(e);
        }
        foreach (string header in asm.Containers.SelectMany(c => c.Primitives).Select(p => p.Name).Where(Own).Distinct(StringComparer.OrdinalIgnoreCase))
        {
            if (!own.ContainsKey(header) && (loose.GetValueOrDefault(header) ?? assets.ReadAnywhere(header)) is { } h) own[header] = h;
            if (DataOf(header) is string dataName && !own.ContainsKey(dataName) && assets.ReadAnywhere(dataName) is { } d) own[dataName] = d;
        }
        return new GameLevel(level, loose, asm, own, source.IsCompressed);
    }

    /// <summary>
    /// Makes the level and its row; returns what was done (a line each), or throws InvalidDataException
    /// saying why it couldn't be. <paramref name="name"/>: the level's file name (lower case letters,
    /// digits and _); <paramref name="groundTextures"/>: the game's level whose ground textures its
    /// ground has (any with ground, of either mode); <paramref name="picture"/>: a picture file for its
    /// preview, or null for a title card.
    /// </summary>
    public List<string> Make(Mode mode, string groundTextures, string name, string displayName, string? picture, bool replace = false, bool noGround = false,
                             float size = 255, string sky = DefaultSky)
    {
        string newArchive = Cache + name + ".vpp_pc";
        if (File.Exists(Path.Combine(workspace.GameFolder, newArchive))) throw new InvalidDataException($"the game already has a level {name}");
        if (File.Exists(Path.Combine(workspace.OverridesFolder, newArchive)) && !(replace && workspace.RemoveNewArchive(newArchive)))
            throw new InvalidDataException($"there is already a level {name} in overrides\\ (delete overrides\\{newArchive} to make it again, or choose another name)");
        if (assets.ZoneSchema is not { } schema) throw new InvalidDataException("the game's zone schema (rfg_zonex_format.txt) couldn't be read");

        // The groups the game loads a level of this mode by: the first of the mode's levels has them.
        string pattern = LevelsOf(workspace, mode).FirstOrDefault(l => File.Exists(Path.Combine(workspace.GameFolder, Cache + l + ".vpp_pc")))
                         ?? throw new InvalidDataException($"the game has none of {mode}'s levels to see how its levels are loaded");
        GameLevel basis = ReadLevel(pattern);

        var files = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
        var kinds = new Dictionary<string, AsmPrimitive>(StringComparer.OrdinalIgnoreCase);
        var groundTextureFiles = new List<string>();
        Vector2 centre = ZoneCorner + new Vector2(ZoneWidth / 2);
        float half = ZoneWidth / 2, height = 0;
        bool copyTextures = false;
        if (noGround)
        {
            report.Add("no ground (" + NoGround(name, files) + "): build the floor, walls and ceiling from stitch pieces, terrain pieces and chunks");
        }
        else
        {
            GameLevel ground = groundTextures.Equals(pattern, StringComparison.OrdinalIgnoreCase) ? basis : ReadLevel(groundTextures);
            string tag = LevelFiles.TextureTag(name, ground.Name.Length);
            // Its terrain files: the terrain file, the far-away ground, the sub-zones, the blend and colour maps.
            foreach (var (file, data) in ground.Own)
            {
                string ext = Path.GetExtension(file).ToLowerInvariant();
                if (ext is not (".cterrain_pc" or ".gterrain_pc" or ".ctmesh_pc" or ".gtmesh_pc" or ".cvbm_pc" or ".gvbm_pc")) continue;
                bool map = ext.EndsWith("vbm_pc");
                byte[] bytes = ext is ".cterrain_pc" or ".ctmesh_pc" or ".cvbm_pc" ? LevelFiles.ReplaceSameLength(data, ground.Name, tag) : data;
                files[ReplaceIgnoringCase(file, ground.Name, map ? tag : name)] = bytes;
            }
            if (!files.ContainsKey(name + ".cterrain_pc")) throw new InvalidDataException($"{ground.Name}'s terrain file wasn't found in the game's files");
            (centre, half, height) = NewGround(name, files, size);
            // The ground textures its materials name, with the level (copied into it if the mode's levels don't load them).
            copyTextures = !string.Equals(ground.Precache, basis.Precache, StringComparison.OrdinalIgnoreCase);
            var groundKinds = ground.Asm.Containers.SelectMany(c => c.Primitives).GroupBy(p => p.Name, StringComparer.OrdinalIgnoreCase)
                                    .ToDictionary(g => g.Key, g => g.First(), StringComparer.OrdinalIgnoreCase);
            foreach (string texture in GroundTexturesOf(files, tag))
            {
                if (assets.ReadAnywhere(texture) is not { } header) continue;
                groundTextureFiles.Add(texture);
                AsmPrimitive like = groundKinds.GetValueOrDefault(texture) ?? Kind(texture);
                byte[]? data = DataOf(texture) is { } dataName ? assets.ReadAnywhere(dataName) : null;
                kinds[texture] = new AsmPrimitive
                {
                    Name = texture, Type = like.Type, Allocator = like.Allocator, Flags = like.Flags, SplitIndex = like.SplitIndex,
                    HeaderSize = header.Length, DataSize = data?.Length ?? 0,
                };
                if (!copyTextures) continue;
                files[texture] = header;
                if (data != null) files[DataOf(texture)!] = data;
            }
            report.Add($"new ground: a {half * 2:0} m square at {height:0.#} m in the middle of the zone, made from nothing (its height field, sub-zones at every grid step, " +
                       $"far-away ground, a blank blend map and a plain colour map; no foliage, roads, walls or occluders); its ground textures {ground.Name}'s " +
                       $"({groundTextureFiles.Count}{(copyTextures ? ", copied into the level" : "")})");
        }

        // An empty stitch list; navigation.
        files[name + ".stchlst_pc"] = StitchList.Write([], new Vector3(centre.X, height, centre.Y), []);
        if (!noGround)
        {
            files[name + ".navdata_pc"] = NavigationBuilder.FlatSquare(centre, half - 1, height, ZoneCorner, ZoneWidth, ZonePlace).ToBytes();
            report.Add("navigation: a new mesh over the ground, for people and vehicles (squads you add find their way; edit it with the NavMesh tool, N)");
        }
        else
        {
            files[name + ".navdata_pc"] = NavigationFile.None.ToBytes();
            report.Add("navigation: none (no ground to make it over; once you've built a floor, the NavMesh tool, N, can make one)");
        }

        // The sky: its effect file and the textures it draws go with the level (the game loads them
        // with it). The effect must be one the game has, or it shows none.
        if (assets.ReadAnywhere(sky + ".cefct_pc") is null)
        {
            report.Add($"the sky effect '{sky}.cefct_pc' isn't in the game's files: the level's sky is {DefaultSky} instead");
            sky = DefaultSky;
        }
        AddSky(sky, files, kinds);

        // Its row, strings, spawn resources and picture (before its zone, which names its spawn resources).
        string resources = AddToMode(mode, pattern, name, displayName, picture);

        // Its zones, new.
        var (zone, persistent) = NewZones(schema, mode, name, resources, centre, half, height, sky);

        // ---- The asset list: the mode's groups, listing only the level's files.
        AsmFile asm = basis.Asm;
        int dropped = asm.Containers.RemoveAll(c => c.Type == 14);
        AsmContainer? Holding(string file) => asm.Containers.FirstOrDefault(c => c.Type == 17 && !c.Name.StartsWith("ns_", StringComparison.OrdinalIgnoreCase) && c.Primitives.Any(p => p.Name.Equals(file, StringComparison.OrdinalIgnoreCase)));
        AsmContainer zoneGroup = Holding(pattern + ".rfgzone_pc") ?? Holding(pattern + ".cterrain_pc")
                                 ?? asm.Containers.FirstOrDefault(c => c.Type == 17 && c.Name.StartsWith("1_", StringComparison.OrdinalIgnoreCase))
                                 ?? throw new InvalidDataException($"{pattern}'s asset list has no zone group");
        var cells = new Dictionary<int, AsmContainer>();
        for (int k = 0; k < 64; k++)
            if (Holding($"{pattern}_{k}.ctmesh_pc") is { } cell) cells[k] = cell;
        // ns_precache: the files the game loads with the level (its sky's, its ground textures'), in a
        // container of their own (the game's levels have one).
        AsmContainer precache = asm.Containers.FirstOrDefault(c => c.Name.Equals("ns_precache", StringComparison.OrdinalIgnoreCase))
                              ?? new AsmContainer { Name = "ns_precache", Type = 17, Flags = 0x80 };
        if (!asm.Containers.Contains(precache)) asm.Containers.Add(precache);
        foreach (AsmContainer c in asm.Containers.Where(c => !IsNsBase(c))) c.Primitives.Clear();

        AsmPrimitive Entry(string file)
        {
            AsmPrimitive like = kinds.GetValueOrDefault(file) ?? Kind(file);
            return new AsmPrimitive
            {
                Name = file, Type = like.Type, Allocator = like.Allocator, Flags = like.Flags, SplitIndex = like.SplitIndex,
                HeaderSize = files.TryGetValue(file, out byte[]? h) ? h.Length : like.HeaderSize,
                DataSize = DataOf(file) is string dn && files.TryGetValue(dn, out byte[]? d) ? d.Length : like.DataSize,
            };
        }
        foreach (string file in files.Keys.Where(f => !IsDataFile(f)).Concat(groundTextureFiles).Distinct(StringComparer.OrdinalIgnoreCase).Order())
        {
            Match subzone = Regex.Match(file, "^" + Regex.Escape(name) + @"_(\d+)\.ctmesh_pc$", RegexOptions.IgnoreCase);
            AsmContainer into = subzone.Success && cells.TryGetValue(int.Parse(subzone.Groups[1].Value), out AsmContainer? cell) ? cell : zoneGroup;
            into.Primitives.Add(Entry(file));
            bool precached = groundTextureFiles.Contains(file, StringComparer.OrdinalIgnoreCase) || skyFiles.Contains(file);
            if (precached) precache.Primitives.Add(Entry(file));
        }

        // ns_base: every file of the level's (but its persistent zone, loose beside it).
        var baseFiles = files.Select(f => (f.Key, f.Value)).ToList();
        byte[] baseBytes = Packfile.CreateContainer(baseFiles);
        var container = Packfile.FromBytes("ns_base.str2_pc", baseBytes);
        AsmContainer baseEntry = asm.Containers.FirstOrDefault(IsNsBase) ?? new AsmContainer { Name = "ns_base", Type = 17, Flags = 0x80 };
        if (!asm.Containers.Contains(baseEntry)) asm.Containers.Insert(0, baseEntry);
        baseEntry.DataOffset = (uint)container.DataStart;
        baseEntry.CompressedSize = container.CompressedDataSize;
        baseEntry.Sizes.Clear();
        baseEntry.Sizes.AddRange(container.Entries.Select(e => (int)e.Size));
        baseEntry.Primitives.Clear();
        foreach (var (file, _) in baseFiles.Where(f => !IsDataFile(f.Key))) baseEntry.Primitives.Add(Entry(file));

        // ns_precache: its own container, as the game reads it (the sky's effect and textures in it,
        // so the sky is there when the game loads the level). Its files go in as the game keeps them:
        // each header and the data file going with it.
        var precacheFiles = new List<(string Name, byte[] Data)>();
        foreach (AsmPrimitive p in precache.Primitives)
        {
            if (!files.TryGetValue(p.Name, out byte[]? header)) continue;
            precacheFiles.Add((p.Name, header));
            if (DataOf(p.Name) is { } dataName && files.TryGetValue(dataName, out byte[]? data)) precacheFiles.Add((dataName, data));
        }
        byte[] precacheBytes = Packfile.CreateContainer(precacheFiles);
        var precachePack = Packfile.FromBytes("ns_precache.str2_pc", precacheBytes);
        precache.DataOffset = (uint)precachePack.DataStart;
        precache.CompressedSize = precachePack.CompressedDataSize;
        precache.Sizes.Clear();
        precache.Sizes.AddRange(precachePack.Entries.Select(e => (int)e.Size));

        // ---- The archive: its asset list, its precache marker, ns_base, ns_precache, its zones.
        var archiveFiles = new List<(string Name, byte[] Data)> { (name + ".asm_pc", asm.ToBytes()) };
        foreach (var (file, data) in basis.Loose.Where(f => f.Key.EndsWith(".precache", StringComparison.OrdinalIgnoreCase))) archiveFiles.Add((file, data));
        archiveFiles.Add(("ns_base.str2_pc", baseBytes));
        archiveFiles.Add(("ns_precache.str2_pc", precacheBytes));
        archiveFiles.Add((name + ".rfgzone_pc", zone));
        archiveFiles.Add(("p_" + name + ".rfgzone_pc", persistent));
        string path = Path.Combine(workspace.OverridesFolder, newArchive);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        Packfile.Write(path, archiveFiles, basis.Compressed);
        report.Add($"{newArchive} written ({new FileInfo(path).Length / 1024:N0} KB): {baseFiles.Count} files in its ns_base container; its asset list " +
                   $"has the groups {mode} loads a level by, with only its own files ({dropped} model groups of {pattern}'s left out)");
        workspace.RefreshOverrides();
        return report;
    }

    static bool IsNsBase(AsmContainer c) => c.Name.Equals("ns_base", StringComparison.OrdinalIgnoreCase);

    static string ReplaceIgnoringCase(string text, string from, string to) =>
        Regex.Replace(text, Regex.Escape(from), to.Replace("$", "$$"), RegexOptions.IgnoreCase);

    /// <summary>The data file (g-file) going with a header file, by the game's naming: .cXXX → .gXXX; null for one without.</summary>
    static string? DataOf(string header)
    {
        string ext = Path.GetExtension(header);
        return ext.Length > 2 && (ext[1] is 'c' or 'C') ? Path.ChangeExtension(header, ".g" + ext[2..]) : null;
    }

    static bool IsDataFile(string file)
    {
        string ext = Path.GetExtension(file);
        return ext.Length > 2 && (ext[1] is 'g' or 'G') && !ext.Equals(".gtodx", StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>A file's kind in an asset list (type, allocator, flags, split), as the game's levels list such files.</summary>
    static AsmPrimitive Kind(string file)
    {
        (int type, int allocator, int flags, int split) = Path.GetExtension(file).ToLowerInvariant() switch
        {
            ".rfgzone_pc" => (3, 0, 2, 0),
            ".cterrain_pc" => (4, 0, 6, 0),
            ".navdata_pc" => (5, 0, 2, 0),
            ".ctmesh_pc" => (21, 0, 6, 0),
            ".stchlst_pc" => (23, 0, 2, 0),
            ".cefct_pc" => (13, 1, 6, 0),
            ".cvbm_pc" => (1, 1, 6, 1),
            ".cpeg_pc" => (1, 1, 6, 1),
            _ => (0, 0, 2, 0),
        };
        return new AsmPrimitive { Name = file, Type = (byte)type, Allocator = (byte)allocator, Flags = (byte)flags, SplitIndex = (byte)split };
    }

    /// <summary>The ground textures the terrain files' materials name (their .tga names, as the game's textures: .cvbm_pc), but the level's own maps.</summary>
    static List<string> GroundTexturesOf(Dictionary<string, byte[]> files, string tag) =>
        files.Where(f => f.Key.EndsWith(".cterrain_pc", StringComparison.OrdinalIgnoreCase) || f.Key.EndsWith(".ctmesh_pc", StringComparison.OrdinalIgnoreCase))
             .SelectMany(f => Regex.Matches(Encoding.Latin1.GetString(f.Value), @"(?<![A-Za-z0-9_])[A-Za-z0-9_]+\.tga", RegexOptions.IgnoreCase).Select(m => m.Value))
             .Where(t => !t.StartsWith(tag, StringComparison.OrdinalIgnoreCase))
             .Select(t => Path.ChangeExtension(t, ".cvbm_pc")).Distinct(StringComparer.OrdinalIgnoreCase).Order().ToList();

    // ---- The zones ---------------------------------------------------------------------------------

    /// <summary>
    /// The level's zone and persistent zone, made empty and given new objects (see the class), from the
    /// zone schema with the editor's settings. Handles 0x88000001 on (the zone's place, as the game's levels').
    /// </summary>
    (byte[] Zone, byte[] Persistent) NewZones(ZoneSchema schema, Mode mode, string name, string resources, Vector2 centre, float half, float y, string sky)
    {
        ZoneFile zone = ZoneFile.Create(NameHashes.ClassHash("Default"), NameHashes.ClassHash("none"));
        ZoneFile persistent = ZoneFile.Create(0, 0);
        uint next = 0x88000001;
        int made = 0;
        var middle = new Vector3(centre.X, y, centre.Y);
        float reach = MathF.Max(4, MathF.Min(half - 6, 24));

        ZoneObject Add(string className, Vector3 at, params (string Name, object Value)[] values) => AddWithin(className, at, null, values);
        ZoneObject AddWithin(string className, Vector3 at, Vector3? extent, params (string Name, object Value)[] values)
        {
            SchemaClass type = schema.Class(className) ?? throw new InvalidDataException($"the zone schema has no {className}");
            var (min, max) = extent is { } e ? (at - e, at + e) : type.NewObjectBounds(at);
            ZoneObject o = zone.AddNew(NameHashes.ClassHash(type.ZoneClassNames[0]), next++, type.NewObject(at, assets.Choices), min, max);
            foreach (var (property, value) in values) Set(o, property, value);
            made++;
            return o;
        }
        void Face(ZoneObject o, Vector3 towards)
        {
            Vector3 forward = towards - o.Placement.Position;
            if (forward.LengthSquared() < 1e-6f) return;
            forward = Vector3.Normalize(forward);
            Vector3 right = Vector3.Cross(Vector3.UnitY, forward);
            right = right.LengthSquared() < 1e-6f ? Vector3.UnitX : Vector3.Normalize(right);
            Vector3 up = Vector3.Cross(forward, right);
            o.TurnTo(new Matrix4x4(right.X, right.Y, right.Z, 0, up.X, up.Y, up.Z, 0, forward.X, forward.Y, forward.Z, 0, 0, 0, 0, 1));
        }
        void Persist(ZoneObject o)
        {
            o.Flags |= 1;  // (the game's player starts: in both zones, persistent)
            persistent.Insert(persistent.Objects.Count, ZoneFile.Copy(o, o.Handle));
        }

        // The zone object: which terrain, which spawn resources.
        Add("obj_zone", new Vector3(centre.X, 0, centre.Y), ("terrain_file_name", name), ("spawn_resource_data", resources), ("district", "Default"),
            ("ambient_spawn", "None"), ("wind_min_speed", 50f), ("wind_max_speed", 80f));

        // The player's starts.
        Vector3 start = middle + new Vector3(0, 0, -reach);
        if (mode == Mode.Ruin)
        {
            ZoneObject player = Add("player_start", start, ("mp_team", "Neutral"), ("respawn", true), ("initial_spawn", false), ("mission_info", "none"));
            Face(player, middle);
            Persist(player);
            Face(Add("object_exo_spawn_node", start + new Vector3(6, 0, 0), ("exo_type", "rf_exo"), ("spawn_prob", 1f)), middle);
        }
        else
        {
            for (int i = 0; i < 4; i++)
            {
                ZoneObject player = Add("player_start", start + new Vector3((i - 1.5f) * 3, 0, 0), ("mp_team", "Guerilla"), ("respawn", true), ("initial_spawn", true),
                                        ("mission_info", "none"));
                Face(player, middle);
                Persist(player);
            }
            Add("multi_object_marker", middle, ("marker_type", "Infestation objective"), ("mp_team", "Neutral"));
            // Its enemies come from the edge.
            float edge = MathF.Max(4, half - 8);
            foreach (Vector3 at in new[] { new Vector3(-edge, 0, -edge), new Vector3(edge, 0, -edge), new Vector3(edge, 0, edge), new Vector3(-edge, 0, edge) })
                Face(Add("object_squad_spawn_node", middle + at, ("squad_def", "Creeper"), ("default_orders", "Immediate Attack"), ("spawn_prob", 0f)), middle);
        }
        // The spectator's intro camera: behind and above the start, looking at the middle.
        Face(Add("multi_object_marker", start + new Vector3(0, 8, -10), ("marker_type", "Spectator intro camera"), ("mp_team", "Neutral")), middle);

        // One environment box round the zone, with the sky.
        Vector3 box = new(ZoneWidth / 2 + 1, 600, ZoneWidth / 2 + 1);
        AddWithin("object_env_settings", new Vector3(centre.X, y, centre.Y), box, ("cam_effect_1", sky), ("skybox_enable", true), ("far_clip", 1500f),
            ("cull_distance", 475f), ("stitch_lod_distance", 200f), ("area_shape", "box"), ("bb", new[] { -box.X, -box.Y, -box.Z, box.X, box.Y, box.Z }));
        report.Add($"its zone made new: {made} objects ({(mode == Mode.Ruin ? "a player start, an exo spawn" : "four player starts, the objective, four enemy spawns")}, " +
                   $"the intro camera, the zone object, an environment box with the sky {sky}); its persistent zone its player starts");
        return (zone.ToBytes(), persistent.ToBytes());
    }

    /// <summary>Sets a property of a new object (adding it if the object hasn't it): text, a number, true or false, or numbers.</summary>
    static void Set(ZoneObject o, string property, object value)
    {
        byte[] bytes = value switch
        {
            string text => [.. Encoding.Latin1.GetBytes(text), 0],
            float number => BitConverter.GetBytes(number),
            bool flag => [(byte)(flag ? 1 : 0)],
            float[] numbers => [.. numbers.SelectMany(BitConverter.GetBytes)],
            _ => throw new ArgumentException($"{property}: a value of {value.GetType().Name}"),
        };
        int type = value is string ? ZoneProperty.TextType : ZoneProperty.ValueType;
        if (o.Property(property) is { } have) have.SetValue(bytes);
        else o.AddProperty(type, NameHashes.PropertyHash(property), bytes);
    }

    // ---- The ground ----------------------------------------------------------------------------------

    /// <summary>
    /// The level's ground made from nothing (its terrain file keeps only its materials): a
    /// <paramref name="size"/> m square of flat ground in the middle of the zone, as the height field
    /// (every sample; outside the square 300 m down, so the level ends in a drop), the sub-zones (a vertex
    /// at every grid step, nothing drawn outside the square), the far-away ground, a blend map all of its
    /// first layer and a plain grey colour map (no tint); no foliage, barriers, occluders, roads or
    /// undergrowth. Returns the square's centre, half width and height.
    /// </summary>
    (Vector2 Centre, float Half, float Height) NewGround(string name, Dictionary<string, byte[]> files, float size)
    {
        string terrainName = name + ".cterrain_pc";
        byte[] oldTerrain = files[terrainName];
        TerrainFile t = TerrainFile.Read(oldTerrain);
        TerrainHeightField field = TerrainHeightField.Find(oldTerrain) ?? throw new InvalidDataException("its terrain file has no height field to go by");
        int n = field.XSamples, m = field.ZSamples;
        float step = field.Step, width = field.ExtentX;
        var corner = new Vector2(BitConverter.ToSingle(t.Data, 0x04), BitConverter.ToSingle(t.Data, 0x0C));
        float half = Math.Clamp(size, 16, width) / 2;
        var centre = corner + new Vector2(width / 2);
        bool Inside(int i, int j) => MathF.Abs(corner.X + i * step - centre.X) <= half + step * 0.01f && MathF.Abs(corner.Y + j * step - centre.Y) <= half + step * 0.01f;

        // The height: 0 if the height field reaches it with 300 m below to drop to, else as near as it can.
        float lowest = field.Offset, highest = field.Offset + field.Scale * ushort.MaxValue;
        float level = Math.Clamp(0, Math.Min(lowest + 300, highest), highest);
        ushort Sample(float h) => (ushort)Math.Clamp(MathF.Round((h - field.Offset) / field.Scale), 0, ushort.MaxValue);
        ushort ground = Sample(level), drop = Sample(level - 300);
        var samples = new ushort[n * m];
        var heights = new float[n * m];
        for (int j = 0; j < m; j++)
            for (int i = 0; i < n; i++)
            {
                samples[j * n + i] = Inside(i, j) ? ground : drop;
                heights[j * n + i] = samples[j * n + i] * field.Scale + field.Offset;
            }
        float top = ground * field.Scale + field.Offset, bottom = drop * field.Scale + field.Offset;
        t.HeightField = TerrainHeightField.Section(n, m, step, field.InverseStep, field.ExtentX, field.ExtentZ, field.Offset, field.Scale, samples);
        var (records, blocks, farData) = RfaFormats.Guerrilla.FarGround.Build(heights, n, step, corner, width);
        t.FarRecords = records;
        t.FarBlocks.Clear();
        t.FarBlocks.AddRange(blocks);
        (t.BarrierRecords, t.BarrierSection) = TerrainBarriers.Parts([]);
        t.Foliage = null;
        t.FoliageBuffer = null;
        t.FoliageMeshNames = [];
        t.FoliageMeshCount = 0;
        t.Undergrowth = null;
        t.Occluders = [];
        t.StitchNames = [];
        t.StitchPlacements = [];
        if (t.LayerMap is { } layerMap) t.LayerMap = layerMap with { Cells = new byte[layerMap.Cells.Length] };
        void Word(int at, int value) => BitConverter.TryWriteBytes(t.Data.AsSpan(at), value);
        void Float(int at, float value) => BitConverter.TryWriteBytes(t.Data.AsSpan(at), value);
        Float(0x08, bottom); Float(0x14, top);
        Word(0x24, 0); Word(0x78, 0); Word(0x84, 0); Word(0x94, 0);
        Word(0xAC, 0); Word(0xB4, 0); Word(0xBC, 0); Word(0xC4, 0);
        files[terrainName] = t.Write();
        files[name + ".gterrain_pc"] = farData;

        // The sub-zones: each its part of the grid (from where the old one started), from nothing.
        var parts = files.Keys.Where(f => f.StartsWith(name + "_", StringComparison.OrdinalIgnoreCase) && f.EndsWith(".ctmesh_pc", StringComparison.OrdinalIgnoreCase)).ToList();
        var starts = new List<(string File, int Number, int X0, int Z0, uint Material)>();
        foreach (string file in parts)
        {
            if (!files.TryGetValue(Path.ChangeExtension(file, ".gtmesh_pc"), out byte[]? data)) continue;
            var old = TerrainSubzone.Read(files[file], data);
            int number = int.TryParse(file[(name.Length + 1)..^".ctmesh_pc".Length], out int k) ? k : starts.Count;
            starts.Add((file, number, (int)MathF.Round((old.Origin.X - corner.X) / step), (int)MathF.Round((old.Origin.Y - corner.Y) / step), TerrainSubzone.GroundMaterialOf(files[file])));
        }
        // A regular grid of them, numbered across then up (as the game's: sub-zone k is column k % 3, row k / 3,
        // x and z growing; the game's own overlap and start where their ground does, which isn't a grid).
        int columns = Math.Max(1, (int)MathF.Round(MathF.Sqrt(starts.Count))), rows = Math.Max(1, (starts.Count + columns - 1) / columns);
        foreach (var (file, number, _, _, material) in starts)
        {
            int column = number % columns, row = Math.Min(rows - 1, number / columns);
            int x0 = column * (n - 1) / columns, x1 = (column + 1) * (n - 1) / columns, z0 = row * (m - 1) / rows, z1 = (row + 1) * (m - 1) / rows;
            var (header, data) = TerrainSubzone.Grid(number, new Vector2(corner.X + x0 * step, corner.Y + z0 * step), x1 - x0, z1 - z0,
                (x, z) => Inside(x0 + x, z0 + z) && Inside(x0 + x + 1, z0 + z) && Inside(x0 + x, z0 + z + 1) && Inside(x0 + x + 1, z0 + z + 1),
                (x, z) => heights[(z0 + z) * n + x0 + x], (_, _) => Vector3.UnitY, material, step);
            files[file] = header;
            files[Path.ChangeExtension(file, ".gtmesh_pc")] = data;
        }

        // The blend map all of the first layer; the colour map plain grey (no tint).
        foreach (string map in files.Keys.Where(f => f.EndsWith(".cvbm_pc", StringComparison.OrdinalIgnoreCase)).ToList())
        {
            string dataName = Path.ChangeExtension(map, ".gvbm_pc");
            if (!files.TryGetValue(dataName, out byte[]? mapData)) continue;
            try
            {
                PegFile peg = PegFile.Read(files[map], mapData);
                PegTexture texture = peg.Textures[0];
                var image = new RgbaImage(texture.Width, texture.Height);
                byte grey = map.Contains("_alpha", StringComparison.OrdinalIgnoreCase) ? (byte)0 : (byte)128;
                for (int p = 0; p < image.Pixels.Length; p += 4) { image.Pixels[p] = grey; image.Pixels[p + 1] = grey; image.Pixels[p + 2] = grey; image.Pixels[p + 3] = 255; }
                files[dataName] = peg.WithTextureData(texture, TextureCodec.EncodeWithMips(texture.Format, image, texture.MipLevels));
            }
            catch (Exception ex) when (ex is InvalidDataException or NotSupportedException or ArgumentException)
            {
                files.Remove(map);
                files.Remove(dataName);
                report.Add($"{map} couldn't be made ({ex.Message}); the level has none");
            }
        }
        return (centre, half, top);
    }

    /// <summary>The game's skies (effects.xtbl's effects drawing one: env_..._sky...).</summary>
    public static List<string> Skies(Workspace workspace)
    {
        try
        {
            string table = Encoding.Latin1.GetString(workspace.Read(Tables, "effects.xtbl"));
            return Regex.Matches(table, @"<Name>(env_[A-Za-z0-9_]*sky[A-Za-z0-9_]*)</Name>").Select(m => m.Groups[1].Value)
                        .Where(s => !s.Contains("lightning") && !s.Contains("bomb")).Distinct(StringComparer.OrdinalIgnoreCase).Order().ToList();
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException) { return [DefaultSky]; }
    }

    /// <summary>The sky's files (its effect file and the textures it draws), which go in the level's ns_base container: the game loads them with the level.</summary>
    void AddSky(string sky, Dictionary<string, byte[]> files, Dictionary<string, AsmPrimitive> kinds)
    {
        var added = new List<string>();
        void Add(string header)
        {
            if (files.ContainsKey(header) || assets.ReadAnywhere(header) is not { } h) return;
            files[header] = h;
            if (DataOf(header) is { } dataName && assets.ReadAnywhere(dataName) is { } d) files[dataName] = d;
            kinds[header] = Kind(header);
            skyFiles.Add(header);
            added.Add(header);
        }

        // The effect, and the textures it draws (the game loads them with the level).
        Add(sky + ".cefct_pc");
        if (files.GetValueOrDefault(sky + ".cefct_pc") is { } effectFile)
            foreach (string texture in Regex.Matches(Encoding.Latin1.GetString(effectFile), @"(?<![A-Za-z0-9_])[A-Za-z0-9_]+\.tga").Select(m => m.Value).Distinct(StringComparer.OrdinalIgnoreCase))
                Add(Path.ChangeExtension(texture, ".cvbm_pc"));
        report.Add($"its sky: {sky}" + (added.Count > 0 ? $", its files with the level ({string.Join(", ", added)})" : " (its files weren't found: the game shows none)"));
    }

    readonly HashSet<string> skyFiles = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Levels with no ground, whose terrain files (a terrain file with no height field, empty sub-zones: no names in them) a level with none is given.</summary>
    static readonly string[] GroundlessLevels = ["mp_inf01", "mp_inf02", "mp_inf03", "mp_inf04", "mp_inf05", "mp_inf06", "mp_inf07", "mp_inf08"];

    /// <summary>The level's terrain file and sub-zones with no ground in them, under its name. Returns whose they were.</summary>
    string NoGround(string name, Dictionary<string, byte[]> files)
    {
        foreach (string from in GroundlessLevels)
        {
            byte[]? header = assets.ReadAnywhere(from + ".cterrain_pc"), data = assets.ReadAnywhere(from + ".gterrain_pc");
            if (header == null || data == null || header.Length > 64 * 1024) continue;  // (a level with ground: its terrain file is far bigger)
            var subzones = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
            for (int k = 0; k < 64; k++)
            {
                byte[]? c = assets.ReadAnywhere($"{from}_{k}.ctmesh_pc"), g = assets.ReadAnywhere($"{from}_{k}.gtmesh_pc");
                if (c == null || g == null) break;
                subzones[$"{name}_{k}.ctmesh_pc"] = c;
                subzones[$"{name}_{k}.gtmesh_pc"] = g;
            }
            files[name + ".cterrain_pc"] = header;
            files[name + ".gterrain_pc"] = data;
            foreach (var (file, bytes) in subzones) files[file] = bytes;
            return $"empty terrain files as {from}'s, with {subzones.Count / 2} empty sub-zones";
        }
        throw new InvalidDataException("no level with no ground was found to see what empty terrain files are like (Infestation's mp_inf01 to mp_inf08)");
    }

    /// <summary>A level the editor made: its file name, the mode whose list has it (null: none, only its files are left), whether its archive is there.</summary>
    public sealed record MadeLevel(string Name, Mode? Mode, bool Archive)
    {
        public override string ToString() =>
            $"{Name}  ({(Mode is { } m ? m.ToString() : "in no map list")}{(Archive ? "" : Mode == null ? ", its edits left" : ", its archive gone")})";
    }

    /// <summary>
    /// The levels the editor has made: those in a mode's list with no archive of the game's, and any
    /// left in no list (a level's archive in overrides\, one archive holding its zone and asset list
    /// under its own name, or only its edits, in edits\).
    /// </summary>
    public static List<MadeLevel> Made(Workspace workspace)
    {
        var made = new List<MadeLevel>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        bool Game(string level) => File.Exists(Path.Combine(workspace.GameFolder, Cache + level + ".vpp_pc"));
        bool Ours(string level) => File.Exists(Path.Combine(workspace.OverridesFolder, Cache + level + ".vpp_pc"));
        foreach (Mode mode in new[] { Mode.Ruin, Mode.Infestation })
            foreach (string level in LevelsOf(workspace, mode))
                if (!Game(level) && seen.Add(level)) made.Add(new MadeLevel(level, mode, Ours(level)));
        // In no list: archives of ours that are a level (its zone and asset list under its name).
        string cache = Path.Combine(workspace.OverridesFolder, Cache);
        if (Directory.Exists(cache))
            foreach (string path in Directory.EnumerateFiles(cache, "*.vpp_pc"))
            {
                string level = Path.GetFileNameWithoutExtension(path);
                if (Game(level) || seen.Contains(level)) continue;
                try
                {
                    Packfile pack = Packfile.FromBytes(Path.GetFileName(path), File.ReadAllBytes(path));
                    if (pack.Find(level + ".rfgzone_pc") == null || pack.Find(level + ".asm_pc") == null) continue;
                }
                catch (Exception ex) when (ex is IOException or InvalidDataException or UnauthorizedAccessException) { continue; }
                seen.Add(level);
                made.Add(new MadeLevel(level, null, true));
            }
        // Only its edits left.
        string edits = Path.Combine(workspace.EditsFolder, Cache);
        if (Directory.Exists(edits))
            foreach (string folder in Directory.EnumerateDirectories(edits, "*.vpp_pc"))
            {
                string level = Path.GetFileNameWithoutExtension(folder);
                if (Game(level) || Ours(level) || seen.Contains(level)) continue;
                if (!File.Exists(Path.Combine(folder, level + ".rfgzone_pc")) && !File.Exists(Path.Combine(folder, level + ".asm_pc"))) continue;
                seen.Add(level);
                made.Add(new MadeLevel(level, null, false));
            }
        return made;
    }

    /// <summary>
    /// Deletes a level the editor made, all of it: its rows in both modes' lists (and Infestation's
    /// pre-game list), its own spawn resources, its name in every language's strings, its pictures
    /// (their containers and their entries in ui_images.asm_pc), and its archive with its edits and
    /// the copies kept of them. Returns what was done.
    /// </summary>
    public List<string> Remove(string name)
    {
        string archive = Cache + name + ".vpp_pc";
        if (File.Exists(Path.Combine(workspace.GameFolder, archive))) throw new InvalidDataException($"{name} is one of the game's own levels");
        var images = new List<string>();
        var resources = new List<string>();
        foreach (Mode mode in new[] { Mode.Ruin, Mode.Infestation })
        {
            var (tableName, row, key) = TableOf(mode);
            string table = Encoding.Latin1.GetString(workspace.Read(Tables, tableName));
            if (LevelFiles.RowValue(table, row, key, name, "resource_reservation") is { Length: > 0 } r) resources.Add(r);
            images.AddRange(new[] { "image_name", "level_image_name", "Load_image" }
                            .Select(t => LevelFiles.RowValue(table, row, key, name, t)).OfType<string>().Where(i => i.Length > 0));
            if (LevelFiles.WithoutRow(table, row, key, name) is { } without)
            {
                workspace.WriteOverride(Tables, tableName, Encoding.Latin1.GetBytes(without));
                report.Add($"{tableName}: its row taken out");
            }
        }
        string pregame = Encoding.Latin1.GetString(workspace.Read(Tables, "rf4_mp_pregame.xtbl"));
        if (LevelFiles.WithoutRow(pregame, "pregame", "Name", name) is { } p)
        {
            workspace.WriteOverride(Tables, "rf4_mp_pregame.xtbl", Encoding.Latin1.GetBytes(p));
            report.Add("rf4_mp_pregame.xtbl: its row taken out");
        }
        // Its own spawn resources (as New level named them: as the level), only if no other level names them.
        string spawn = SpawnResourceFiles.Read(workspace);
        if (resources.Append(name).Any(r => r.Equals(name, StringComparison.OrdinalIgnoreCase)) && !OthersUse(name) &&
            LevelFiles.WithoutRow(spawn, "spawn_resource_entry", "Name", name) != null)
        {
            SpawnResourceFiles.Change(workspace, table => LevelFiles.WithoutRow(table, "spawn_resource_entry", "Name", name));
            report.Add($"spawn_resource.xtbl: its spawn resources \"{name}\" taken out");
        }
        // Its name in every language.
        string upper = name.ToUpperInvariant();
        string[] tags = ["RUIN_MAP_" + upper, "RUIN_MAP_" + upper + "_UC", "MP_" + upper + "_NAME", "MP_" + upper + "_NAME_UC"];
        int languages = 0;
        foreach (PackfileEntry entry in workspace.Archive(Misc).Entries.Where(e => e.Name.StartsWith("multiplayer_", StringComparison.OrdinalIgnoreCase) && e.Name.EndsWith(".le_strings", StringComparison.OrdinalIgnoreCase)))
            if (LevelFiles.WithoutStrings(workspace.Read(Misc, entry.Name), tags) is { } fewer)
            {
                workspace.WriteOverride(Misc, entry.Name, fewer);
                languages++;
            }
        if (languages > 0) report.Add($"its name taken out of {languages} languages' multiplayer strings");
        // Its pictures: those of its own (a container New level wrote beside interface.vpp_pc), also
        // by the names New level gives them, should its row be gone already.
        images.AddRange(["ui_ruin_" + name, "ui_mp_lobby_" + name, "ui_mp_load_" + name]);
        var own = images.Distinct(StringComparer.OrdinalIgnoreCase).Where(i => File.Exists(Path.Combine(workspace.OverridesFolder, Cache, i + ".str2_pc"))).ToList();
        AsmFile list = AsmFile.Read(workspace.Read(Interface, "ui_images.asm_pc"));
        int listed = list.Containers.RemoveAll(c => own.Contains(c.Name, StringComparer.OrdinalIgnoreCase) ||
                                                    images.Skip(images.Count - 3).Contains(c.Name, StringComparer.OrdinalIgnoreCase));
        if (listed > 0) workspace.WriteOverride(Interface, "ui_images.asm_pc", list.ToBytes());
        foreach (string image in own) File.Delete(Path.Combine(workspace.OverridesFolder, Cache, image + ".str2_pc"));
        if (own.Count > 0 || listed > 0) report.Add($"its pictures taken out ({string.Join(", ", own)})");
        // Its archive, its edits and the copies kept.
        if (workspace.RemoveNewArchive(archive)) report.Add($"{archive}, its edits and the copies kept of them deleted");
        workspace.RefreshOverrides();
        return report;
    }

    /// <summary>Whether a level other than <paramref name="name"/> names the spawn resources <paramref name="name"/>.</summary>
    bool OthersUse(string name)
    {
        foreach (var (table, row, key) in new[] { TableOf(Mode.Ruin), TableOf(Mode.Infestation), ("missions.xtbl", "mission", "Name") })
        {
            string text;
            try { text = Encoding.Latin1.GetString(workspace.Read(Tables, table)); }
            catch (Exception ex) when (ex is IOException or InvalidDataException) { continue; }
            foreach (System.Text.RegularExpressions.Match m in System.Text.RegularExpressions.Regex.Matches(text, $@"<{row}>.*?</{row}>", System.Text.RegularExpressions.RegexOptions.Singleline))
            {
                string k = System.Text.RegularExpressions.Regex.Match(m.Value, $@"<{key}>([^<]*)</{key}>").Groups[1].Value.Trim();
                string r = System.Text.RegularExpressions.Regex.Match(m.Value, @"<resource_reservation>([^<]*)</resource_reservation>").Groups[1].Value.Trim();
                if (!k.Equals(name, StringComparison.OrdinalIgnoreCase) && r.Equals(name, StringComparison.OrdinalIgnoreCase)) return true;
            }
        }
        return false;
    }

    // ---- The mode's list -------------------------------------------------------------------------

    /// <summary>
    /// The level's row in its mode's map list (laid out as <paramref name="template"/>'s, every name,
    /// string, picture and its spawn resources its own), its strings and pictures (<paramref name="picture"/>,
    /// or a title card with its name). Returns the name of its spawn resources (spawn_resource.xtbl).
    /// </summary>
    string AddToMode(Mode mode, string template, string name, string displayName, string? picture)
    {
        string resourcesName = "Default";
        Func<int, int, RgbaImage> pixels = picture != null ? (w, h) => LoadPicture(picture, w, h) : (w, h) => TitleCard(displayName, mode, w, h);
        string from = picture != null ? Path.GetFileName(picture) : "a title card with its name";
        var (tableName, row, key) = TableOf(mode);
        string table = Encoding.Latin1.GetString(workspace.Read(Tables, tableName));
        string upper = name.ToUpperInvariant();
        var strings = new List<(string Tag, string Text)>();
        var change = new Dictionary<string, string>();
        // Its own spawn resources (spawn_resource.xtbl: what it can spawn), as the template's to start
        // with, so changing them (Spawn resources...) changes no other level's.
        if (LevelFiles.RowValue(table, row, key, template, "resource_reservation") is { Length: > 0 } templateResources)
        {
            string resources = Encoding.Latin1.GetString(workspace.Read(Tables, "spawn_resource.xtbl"));
            resourcesName = templateResources;
            if (SpawnResource.Read(resources, templateResources) is { } own)
            {
                own.Name = name;
                SpawnResourceFiles.Write(workspace, own, templateResources);
                change["resource_reservation"] = name;
                resourcesName = name;
                report.Add($"spawn_resource.xtbl: its own spawn resources \"{name}\", to start with what {mode} levels spawn ({templateResources}'s); change them with Spawn resources");
            }
        }
        if (mode == Mode.Ruin)
        {
            string id = "RUIN_MAP_" + upper;
            strings.Add((id, displayName));
            strings.Add((id + "_UC", displayName.ToUpperInvariant()));
            change["Name"] = displayName;
            change["file_name"] = name;
            change["display_name"] = id;
            change["display_name_uppercase"] = id + "_UC";
            change["is_locked"] = "False";
            change["unlocks_map"] = "";
            if (TryPicture(LevelFiles.RowValue(table, row, key, template, "image_name"), "ui_ruin_" + name, pixels, from) is { } image) change["image_name"] = image;
        }
        else
        {
            string id = "MP_" + upper + "_NAME";
            strings.Add((id, displayName));
            strings.Add((id + "_UC", displayName.ToUpperInvariant()));
            change["Name"] = name;
            change["Filename"] = name;
            change["Display_name"] = id;
            change["Display_name_uppercase"] = id + "_UC";
            if (TryPicture(LevelFiles.RowValue(table, row, key, template, "level_image_name"), "ui_mp_lobby_" + name, pixels, from) is { } lobby) change["level_image_name"] = lobby;
            if (TryPicture(LevelFiles.RowValue(table, row, key, template, "Load_image"), "ui_mp_load_" + name, pixels, from) is { } load) change["Load_image"] = load;
        }
        string made = LevelFiles.WithRowCopied(table, row, key, template, name, change) ?? throw new InvalidDataException($"{tableName} has no row for {template}");
        workspace.WriteOverride(Tables, tableName, Encoding.Latin1.GetBytes(made));
        report.Add($"{tableName}: its row (\"{displayName}\"), unlocked, laid out as the mode's rows are");
        if (mode == Mode.Infestation)
        {
            string pregame = Encoding.Latin1.GetString(workspace.Read(Tables, "rf4_mp_pregame.xtbl"));
            if (LevelFiles.WithRowCopied(pregame, "pregame", "Name", template, name, new Dictionary<string, string> { ["Name"] = name }) is { } p)
            {
                workspace.WriteOverride(Tables, "rf4_mp_pregame.xtbl", Encoding.Latin1.GetBytes(p));
                report.Add("rf4_mp_pregame.xtbl: its pre-game row");
            }
        }
        // The strings, in every language's multiplayer strings.
        int languages = 0;
        foreach (PackfileEntry entry in workspace.Archive(Misc).Entries.Where(e => e.Name.StartsWith("multiplayer_", StringComparison.OrdinalIgnoreCase) && e.Name.EndsWith(".le_strings", StringComparison.OrdinalIgnoreCase)))
        {
            workspace.WriteOverride(Misc, entry.Name, LevelFiles.WithStrings(workspace.Read(Misc, entry.Name), strings));
            languages++;
        }
        report.Add($"its name in {languages} languages' multiplayer strings ({string.Join(", ", strings.Select(s => s.Tag))})");
        return resourcesName;
    }

    /// <summary>The picture made (its name), or null (and why, in the report) if it couldn't be: the level is made without it.</summary>
    string? TryPicture(string? templateImage, string image, Func<int, int, RgbaImage> pixels, string from)
    {
        try { return MakePicture(templateImage, image, pixels, from); }
        catch (Exception ex) when (ex is InvalidDataException or IOException or ArgumentException or NotSupportedException
                                       or InvalidOperationException or FormatException or System.Runtime.InteropServices.COMException)
        {
            report.Add($"its picture {image} couldn't be made ({ex.Message}); the menu shows another level's ({templateImage ?? "none"})");
            return null;
        }
    }

    /// <summary>
    /// A preview picture, new: <paramref name="pixels"/> at the size and in the format the menu's
    /// pictures of this kind have (<paramref name="templateImage"/>'s, in interface.vpp_pc), as a new
    /// image container beside interface.vpp_pc (&lt;image&gt;.str2_pc), listed in ui_images.asm_pc as
    /// such pictures are. Returns its name.
    /// </summary>
    string MakePicture(string? templateImage, string image, Func<int, int, RgbaImage> pixels, string from)
    {
        if (templateImage == null) throw new InvalidDataException("the template's row names no picture");
        Packfile ui = workspace.Archive(Interface);
        if (ui.Find(templateImage + ".str2_pc") is not { } templateEntry) throw new InvalidDataException($"interface.vpp_pc has no {templateImage}.str2_pc");
        Packfile templateContainer = Packfile.FromBytes(templateEntry.Name, ui.Read(templateEntry));
        byte[] header = templateContainer.Read(templateContainer.Find(templateImage + ".cvbm_pc") ?? throw new InvalidDataException($"{templateImage} has no texture"));
        byte[] data = templateContainer.Read(templateContainer.Find(templateImage + ".gvbm_pc") ?? throw new InvalidDataException($"{templateImage} has no texture data"));
        PegFile peg = PegFile.Read(header, data);
        PegTexture texture = peg.Textures[0];
        RgbaImage made0 = pixels(texture.Width, texture.Height);
        (byte[] newHeader, byte[] newData) = peg.Single(texture, image + ".tga", texture.MipLevels);
        PegFile made = PegFile.Read(newHeader, newData);
        newData = made.WithTextureData(made.Textures[0], TextureCodec.EncodeWithMips(texture.Format, made0, texture.MipLevels));
        var containerFiles = new List<(string Name, byte[] Data)> { (image + ".cvbm_pc", newHeader), (image + ".gvbm_pc", newData) };
        byte[] bytes = Packfile.CreateContainer(containerFiles);
        string containerPath = Path.Combine(workspace.OverridesFolder, Cache, image + ".str2_pc");
        Directory.CreateDirectory(Path.GetDirectoryName(containerPath)!);
        File.WriteAllBytes(containerPath, bytes);
        // Listed as the template's is.
        AsmFile list = AsmFile.Read(workspace.Read(Interface, "ui_images.asm_pc"));
        AsmContainer like = list.Containers.First(c => c.Name.Equals(templateImage, StringComparison.OrdinalIgnoreCase));
        list.Containers.RemoveAll(c => c.Name.Equals(image, StringComparison.OrdinalIgnoreCase));
        var c = Packfile.FromBytes(image + ".str2_pc", bytes);
        var entry = new AsmContainer { Name = image, Type = like.Type, Flags = like.Flags, DataOffset = (uint)c.DataStart, CompressedSize = c.CompressedDataSize };
        entry.Sizes.AddRange(c.Entries.Select(e => (int)e.Size));
        AsmPrimitive p = like.Primitives[0];
        entry.Primitives.Add(new AsmPrimitive
        {
            Name = image + ".cvbm_pc", Type = p.Type, Allocator = p.Allocator, Flags = p.Flags, SplitIndex = p.SplitIndex,
            HeaderSize = newHeader.Length, DataSize = newData.Length,
        });
        list.Containers.Add(entry);
        workspace.WriteOverride(Interface, "ui_images.asm_pc", list.ToBytes());
        report.Add($"its picture: {image} ({texture.Width} × {texture.Height}, {from})");
        return image;
    }

    /// <summary>
    /// A title card for a level with no picture of its own: its name, large, over a dark ground in the
    /// mode's colour (Ruin rust, Infestation green), with the mode's name under it.
    /// </summary>
    static RgbaImage TitleCard(string displayName, Mode mode, int width, int height)
    {
        var visual = new DrawingVisual();
        using (DrawingContext dc = visual.RenderOpen())
        {
            Color top = mode == Mode.Ruin ? Color.FromRgb(0x5A, 0x24, 0x12) : Color.FromRgb(0x1C, 0x40, 0x22);
            dc.DrawRectangle(new LinearGradientBrush(top, Color.FromRgb(0x10, 0x10, 0x12), 90), null, new System.Windows.Rect(0, 0, width, height));
            var face = new Typeface(new FontFamily("Segoe UI"), System.Windows.FontStyles.Normal, System.Windows.FontWeights.Bold, System.Windows.FontStretches.Normal);
            double size = height * 0.2;
            FormattedText Text(string text, double em, Brush brush) => new(text, System.Globalization.CultureInfo.CurrentUICulture, System.Windows.FlowDirection.LeftToRight,
                                                                          face, em, brush, 1.0) { MaxTextWidth = width * 0.9, TextAlignment = System.Windows.TextAlignment.Center };
            FormattedText title = Text(displayName.ToUpperInvariant(), size, Brushes.White);
            while (title.Height > height * 0.6 && size > 8) title = Text(displayName.ToUpperInvariant(), size *= 0.85, Brushes.White);
            FormattedText under = Text(mode == Mode.Ruin ? "RUIN" : "INFESTATION", Math.Max(8, height * 0.08), new SolidColorBrush(Color.FromArgb(0xC0, 0xFF, 0xFF, 0xFF)));
            double y = (height - title.Height - under.Height) / 2;
            dc.DrawText(title, new System.Windows.Point(width * 0.05, y));
            dc.DrawText(under, new System.Windows.Point(width * 0.05, y + title.Height));
        }
        var bitmap = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32);
        bitmap.Render(visual);
        var bgra = new byte[width * height * 4];
        bitmap.CopyPixels(bgra, width * 4, 0);
        var image = new RgbaImage(width, height);
        for (int p = 0; p < bgra.Length; p += 4)
        {
            image.Pixels[p] = bgra[p + 2];
            image.Pixels[p + 1] = bgra[p + 1];
            image.Pixels[p + 2] = bgra[p];
            image.Pixels[p + 3] = 255;
        }
        return image;
    }

    /// <summary>A picture file (PNG, JPG, BMP...) scaled to fill <paramref name="width"/> × <paramref name="height"/>, its middle kept.</summary>
    static RgbaImage LoadPicture(string path, int width, int height)
    {
        BitmapDecoder decoder = BitmapDecoder.Create(new Uri(path), BitmapCreateOptions.PreservePixelFormat, BitmapCacheOption.OnLoad);
        BitmapSource frame = decoder.Frames[0];
        double scale = Math.Max(width / (double)frame.PixelWidth, height / (double)frame.PixelHeight);
        BitmapSource scaled = new TransformedBitmap(frame, new ScaleTransform(scale, scale));
        int x = Math.Max(0, (scaled.PixelWidth - width) / 2), y = Math.Max(0, (scaled.PixelHeight - height) / 2);
        BitmapSource cut = new CroppedBitmap(scaled, new System.Windows.Int32Rect(x, y, Math.Min(width, scaled.PixelWidth - x), Math.Min(height, scaled.PixelHeight - y)));
        var bgra = new FormatConvertedBitmap(cut, PixelFormats.Bgra32, null, 0);
        var image = new RgbaImage(width, height);
        var row = new byte[bgra.PixelWidth * 4];
        for (int line = 0; line < Math.Min(height, bgra.PixelHeight); line++)
        {
            bgra.CopyPixels(new System.Windows.Int32Rect(0, line, bgra.PixelWidth, 1), row, row.Length, 0);
            for (int i = 0; i < Math.Min(width, bgra.PixelWidth); i++)
            {
                int o = (line * width + i) * 4;
                image.Pixels[o] = row[i * 4 + 2];
                image.Pixels[o + 1] = row[i * 4 + 1];
                image.Pixels[o + 2] = row[i * 4];
                image.Pixels[o + 3] = row[i * 4 + 3];
            }
        }
        return image;
    }
}
