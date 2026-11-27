using System.Globalization;
using System.Text;
using RfaFormats.Tables;
using RfaFormats.Zones;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Guerrilla's activities' managed layers (the objects an activity places while it runs and takes away after it: the
/// demolitions masters' barrels, propane tanks, ammo boxes, walkers' and a turret's spawn nodes and their play areas'
/// dummies), as Armageddon's managed layers, which its own code loads, makes and unloads (only its missions use them;
/// the guerrilla_campaign mod asks for an activity's set by name, Armageddon's 0x4FF5C0).
///
/// Guerrilla's: each activity.xtbl row (base_activity) with a managed_layer_info lists its layer files (layer_file_0
/// .. layer_file_15, such as terr01_09_09_137: the zone's column and row, and the layer, 128 and up); its container is
/// the activity's name (je_dm_15.str2_pc in activities.vpp_pc: type 6, flags 0x80, a primitive of type 3 a layer),
/// each layer ZONE version 36 with a 24-byte header and the objects straight after it. Armageddon's are the same but
/// for the layer's version: ZONE 46, a 40-byte header, flags 4 (no relation table), as its missions' layers in
/// missions.vpp_pc (world_04_04_128.layer_pc in Quest_2.str2_pc), listed in missions.vpp_pc's Layer_Container.asm_pc
/// and named by rf4_layer_definition.xtbl (a managed_layer_info row: Name, the container's and the set's, and its
/// managed_layer_list of managed_layer_name).
///
/// Written here, for Guerrilla's demolitions masters (je_dm_NN, 13 with a layer) and area defenses (je_ad_01/02, ls_ad_01-07:
/// each a given walker, tank or turret's spawn node and its spawn regions, which the campaign mod reads from its markers):
///   - each layer converted by the world's own converters (<see cref="GuerrillaPickups"/> for the propane tanks and
///     ammo boxes, <see cref="GuerrillaMovers"/> for the barrels, <see cref="GuerrillaVehiclePlaces"/> for the walkers'
///     spawn nodes) and two of its own (object_dummy: its dummy_type, the same list in both games; and
///     object_turret_spawn_node), keeping Guerrilla's handles (the layer in their third byte, which Armageddon's
///     member_of_layer 0x4FF230 checks) and Guerrilla's names, under our world's name (rfg_09_09_137.layer_pc);
///   - each set's container (je_dm_15.str2_pc), packed as Armageddon's mission containers are, loose in
///     overrides\build\pc\cache\ (the loader hands the game a new file there by its name);
///   - an override of missions.vpp_pc's Layer_Container.asm_pc: Armageddon's entries byte for byte, then ours;
///   - mods\guerrilla_world\rf4_layer_definition.xtbl: Armageddon's rows as they are, then a row a set, named as
///     Guerrilla's activity (je_dm_15), for guerrilla_world to hand the game with its built tables.
/// The barrels' chunk files must be loaded where the layer is made: the world build puts the chunks of a layer's
/// movers in its zone's own container, which loads with the whole zone (see <see cref="Movers"/>).
/// </summary>
public static class GuerrillaActivityLayers
{
    /// <summary>Armageddon's table of managed layer sets, which mods\guerrilla_world\ gets with ours added.</summary>
    public const string LayerTable = "rf4_layer_definition.xtbl";
    /// <summary>The archive with Armageddon's own managed layers' containers and their asset list.</summary>
    public const string MissionArchive = "missions.vpp_pc";
    /// <summary>Armageddon's managed layers' asset list in <see cref="MissionArchive"/> (0x4FFD10 names it).</summary>
    public const string AssetList = "Layer_Container.asm_pc";

    /// <summary>The activities whose layers are ported so far: Guerrilla's demolitions masters (je_dm_) and area defenses (je_ad_, ls_ad_).</summary>
    static readonly string[] ActivityPrefixes = ["je_dm_", "je_ad_", "ls_ad_"];
    static readonly string[] AreaDefensePrefixes = ["je_ad_", "ls_ad_"];

    /// <summary>Whether an activity (activity.xtbl row, its layer set's name) is one of Guerrilla's area defenses ("Heavy Metal").</summary>
    public static bool IsAreaDefense(string activity) => AreaDefensePrefixes.Any(p => activity.StartsWith(p, StringComparison.OrdinalIgnoreCase));
    const string ActivityTable = "activity.xtbl", ActivityArchive = "activities.vpp_pc";
    /// <summary>The most layer files an activity row lists (Guerrilla's layer_file_0 .. layer_file_15; Armageddon's list holds 16).</summary>
    const int MostLayers = 16;

    /// <summary>A layer container's asset list entry: type 6, flags 0x80; a layer file's primitive: type 3 (both games).</summary>
    public const byte ContainerType = 6, LayerPrimitiveType = 3;
    public const ushort ContainerFlags = 0x80;
    /// <summary>The most objects world::serialize_layer (0x6D5D30) makes from one layer (its handle list's room, 0x200).</summary>
    public const int MostObjectsInLayer = 512;
    /// <summary>The memory of the one "Large Layer" slot a set streams into (its descriptor at 0xA478CF: 200 KB of CPU memory).</summary>
    public const int SlotBytes = 0x32000;
    /// <summary>The sets the game's pool holds (0x12D9230, made 128) and the room for their names (the string pool at 0x12D91C8, 6 KB).</summary>
    public const int MostSets = 128, NameRoom = 0x1800;

    const uint None = 0xFFFFFFFF;
    static readonly uint Dummy = NameHashes.ClassHash("object_dummy"), TurretNode = NameHashes.ClassHash("object_turret_spawn_node"),
                         VehicleNode = NameHashes.ClassHash("object_vehicle_spawn_node"), Item = NameHashes.ClassHash("item"),
                         Weapon = NameHashes.ClassHash("weapon"), RfgMover = NameHashes.ClassHash("rfg_mover"),
                         SpawnRegion = NameHashes.ClassHash("object_spawn_region"),
                         GeneralMover = NameHashes.ClassHash("general_mover");

    /// <summary>The order Volition's tools wrote each class's properties in (Armageddon's own levels: all 1,686 dummies, all 29 turret nodes).</summary>
    static readonly string[] DummyOrder = ["dummy_type", "op"];
    static readonly string[] TurretOrder = ["turret_type", "op", "spawn_prob", "autoturret", "rotation_limit"];

    /// <summary>One of Guerrilla's layer files: its name (terr01_09_09_137), its zone and layer number, and its objects.</summary>
    public sealed record Layer(string GuerrillaName, int Col, int Row, int Number, IReadOnlyList<GuerrillaZoneObject> Objects)
    {
        /// <summary>Our name for it, as Armageddon names its missions' layers after their world (world_04_04_128): rfg_09_09_137.</summary>
        public string Name => $"{GuerrillaWorld.World}_{Col:00}_{Row:00}_{Number}";
    }

    /// <summary>An activity's managed layer set: the activity's name (its container's too) and its layers.</summary>
    public sealed record LayerSet(string Name, IReadOnlyList<Layer> Layers);

    /// <summary>The asset list override's place in overrides\.</summary>
    public static string AssetListOverride(string overrides) => Path.Combine(overrides, "build", "pc", "cache", MissionArchive, AssetList);

    /// <summary>A set's container's place in overrides\: where it is, else where the world part writes it (see OverrideLayout).</summary>
    public static string ContainerFile(string overrides, string set) =>
        OverrideLayout.Find(overrides, set + ".str2_pc") ?? Path.Combine(OverrideLayout.Folder(overrides, OverrideLayout.World), set + ".str2_pc");

    /// <summary>Guerrilla's demolitions masters' and area defenses' layer sets, from its activity.xtbl (misc.vpp_pc) and activities.vpp_pc.</summary>
    public static List<LayerSet> Sets(string guerrillaCache)
    {
        var misc = Packfile.Open(Path.Combine(guerrillaCache, "misc.vpp_pc"));
        var table = XtblFile.FromBytes(misc.Read(misc.Find(ActivityTable) ?? throw new InvalidDataException($"Guerrilla's misc.vpp_pc has no {ActivityTable}")));
        var activities = Packfile.Open(Path.Combine(guerrillaCache, ActivityArchive));
        var sets = new List<LayerSet>();
        foreach (var row in table.Entries)
        {
            string name = XtblFile.EntryName(row);
            if (!ActivityPrefixes.Any(p => name.StartsWith(p, StringComparison.OrdinalIgnoreCase)) || row.Element("managed_layer_info") is not { } info) continue;
            // In the order of their tags (layer_file_0 first), as Guerrilla's managed_layer_set::load reads them.
            var files = Enumerable.Range(0, MostLayers).Select(n => info.ValueOf($"layer_file_{n}")?.Trim()).OfType<string>().Where(f => f.Length > 0).ToList();
            if (files.Count == 0) continue;
            var container = activities.OpenContainer(activities.Find(name + ".str2_pc") ?? throw new InvalidDataException($"Guerrilla's {ActivityArchive} has no {name}.str2_pc"), name);
            var layers = new List<Layer>();
            foreach (string file in files)
            {
                var (col, zoneRow, number) = Place(file) ?? throw new InvalidDataException($"{name}'s layer file {file} isn't named <world>_<column>_<row>_<layer>");
                var entry = container.Find(file + ".layer_pc") ?? throw new InvalidDataException($"Guerrilla's {name}.str2_pc has no {file}.layer_pc");
                layers.Add(new Layer(file, col, zoneRow, number, GuerrillaZones.LayerObjects(container.Read(entry))));
            }
            sets.Add(new LayerSet(name, layers));
        }
        return sets;
    }

    /// <summary>
    /// The destructible objects (rfg_movers, general_movers) of the sets' layers, by the zone their layer is in: the world
    /// build converts their chunk files with the zones' and puts them in that zone's own container, which loads with the
    /// whole zone, so they are there whenever the layer is made (a mover whose chunk isn't loaded isn't made).
    /// </summary>
    public static Dictionary<(int Col, int Row), List<GuerrillaZoneObject>> Movers(IEnumerable<LayerSet> sets)
    {
        var movers = new Dictionary<(int Col, int Row), List<GuerrillaZoneObject>>();
        foreach (var layer in sets.SelectMany(s => s.Layers))
            foreach (var o in layer.Objects.Where(o => (o.ClassHash == RfgMover || o.ClassHash == GeneralMover) && o.Text("chunk_name") != null))
            {
                if (!movers.TryGetValue((layer.Col, layer.Row), out var list)) movers[(layer.Col, layer.Row)] = list = [];
                list.Add(o);
            }
        return movers;
    }

    /// <summary>
    /// Writes the sets' containers and the asset list override into <paramref name="overrides"/> and the layer table
    /// into <paramref name="worldModFolder"/> (mods\guerrilla_world\). The movers' names are the world's (its tables'
    /// entries for Guerrilla's Props, gameplay_props and LOD classes); <paramref name="chunkChecksum"/> gives a chunk
    /// file's destruction checksum, or null where the world build didn't convert it (a mover of it is left out). The
    /// pickups' names are the weapons build's (<see cref="GuerrillaWeapons.PickupNames"/>). Returns what to log.
    /// </summary>
    public static IEnumerable<string> Write(IReadOnlyList<LayerSet> sets, string game, string overrides, string worldModFolder, ZoneSchema schema,
        Func<string, string?> propsOf, Func<string, string?> gameplayPropsOf, Func<string, string?> lodPropsOf, Func<string, uint?> chunkChecksum,
        IReadOnlyDictionary<string, string> pickupItems, IReadOnlyDictionary<string, string> pickupWeapons)
    {
        string cache = Path.Combine(game, "build", "pc", "cache");
        var missions = Packfile.Open(Path.Combine(cache, MissionArchive));
        byte[] gameAssets = missions.Read(missions.Find(AssetList) ?? throw new InvalidDataException($"Armageddon's {MissionArchive} has no {AssetList}"));
        var assets = AsmFile.Read(gameAssets);
        if (!assets.ToBytes().AsSpan().SequenceEqual(gameAssets))
            throw new InvalidDataException($"Armageddon's {AssetList} doesn't read back byte for byte: its entries couldn't be kept as they are");
        var gameLayer = assets.Containers.FirstOrDefault(c => c.Type == ContainerType && c.Primitives.Count > 0)
                        ?? throw new InvalidDataException($"Armageddon's {AssetList} has no layer container to pack ours as");
        // Packed as Armageddon's own mission layer containers (its header and flags: compressed, one stream).
        var template = missions.OpenContainer(missions.Find(gameLayer.FileName) ?? throw new InvalidDataException($"Armageddon's {MissionArchive} has no {gameLayer.FileName}"), gameLayer.Name);
        var layerPrimitive = gameLayer.Primitives[0];
        var table = GameLayerTable(game);
        var taken = new HashSet<string>(table.Entries.Select(XtblFile.EntryName).Concat(assets.Containers.Select(c => c.Name)), StringComparer.OrdinalIgnoreCase);
        var turretNames = GameTableNames(game, "turrets.xtbl");

        string output = OverrideLayout.Folder(overrides, OverrideLayout.World);
        var leftOut = new SortedDictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        var classCounts = new SortedDictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        var lines = new List<string>();
        foreach (var set in sets)
        {
            if (!taken.Add(set.Name)) throw new InvalidDataException($"{set.Name}: Armageddon has a managed layer set or container of that name already");
            var files = new List<(string Name, byte[] Data)>();
            var described = new List<string>();
            foreach (var layer in set.Layers)
            {
                var (bytes, counts) = Convert(layer, schema, propsOf, gameplayPropsOf, lodPropsOf, chunkChecksum, pickupItems, pickupWeapons, turretNames, leftOut);
                files.Add((layer.Name + ".layer_pc", bytes));
                foreach (var (kind, n) in counts) classCounts[kind] = classCounts.GetValueOrDefault(kind) + n;
                described.Add($"{layer.Name} ({counts.Values.Sum()} objects: {string.Join(", ", counts.Select(c => $"{c.Value} {c.Key}"))})");
            }
            byte[] container = Packfile.CreateContainer(template, files);
            File.WriteAllBytes(OverrideLayout.NewFile(overrides, OverrideLayout.World, set.Name + ".str2_pc"), container);
            var packed = Packfile.FromBytes(set.Name, container);
            var entry = new AsmContainer { Name = set.Name, Type = gameLayer.Type, Flags = gameLayer.Flags, DataOffset = (uint)packed.DataStart, CompressedSize = packed.CompressedDataSize };
            entry.Sizes.AddRange(files.Select(f => f.Data.Length));
            entry.Primitives.AddRange(files.Select(f => new AsmPrimitive
            {
                Name = f.Name, Type = layerPrimitive.Type, Allocator = layerPrimitive.Allocator, Flags = layerPrimitive.Flags, SplitIndex = layerPrimitive.SplitIndex,
                HeaderSize = f.Data.Length, DataSize = 0,
            }));
            assets.Containers.Add(entry);
            // The set's row, as Armageddon's rows are written: its name, its layers, its folder.
            var row = table.Table!.AddElement("managed_layer_info");
            row.AddElement("Name", set.Name);
            var list = row.AddElement("managed_layer_list");
            foreach (var layer in set.Layers) list.AddElement("managed_layer_name", layer.Name);
            row.AddElement("_Editor").AddElement("Category", "Entries");
            lines.Add($"{set.Name}: {set.Name}.str2_pc, {string.Join("; ", described)}");
        }
        string assetFile = AssetListOverride(overrides);
        Directory.CreateDirectory(Path.GetDirectoryName(assetFile)!);
        File.WriteAllBytes(assetFile, assets.ToBytes());
        Directory.CreateDirectory(worldModFolder);
        File.WriteAllBytes(Path.Combine(worldModFolder, LayerTable), table.ToBytes());
        yield return $"{sets.Count} of Guerrilla's demolitions masters' and area defenses' layer sets as Armageddon's managed layers ({sets.Sum(s => s.Layers.Count)} layers, " +
                     $"{classCounts.Values.Sum()} objects: {string.Join(", ", classCounts.Select(c => $"{c.Value} {c.Key}"))}): their containers in {output}, " +
                     $"{MissionArchive}\\{AssetList} overridden ({assets.Containers.Count - sets.Count} of Armageddon's entries + {sets.Count}), " +
                     $"mods\\guerrilla_world\\{LayerTable} with a row a set" +
                     (leftOut.Count > 0 ? $"; left out: {string.Join(", ", leftOut.Select(l => $"{l.Value} {l.Key}"))}" : "");
        foreach (string line in lines) yield return line;
    }

    /// <summary>One layer as Armageddon's layer file, and how many objects of each class it has.</summary>
    static (byte[] Bytes, SortedDictionary<string, int> Counts) Convert(Layer layer, ZoneSchema schema, Func<string, string?> propsOf, Func<string, string?> gameplayPropsOf,
        Func<string, string?> lodPropsOf, Func<string, uint?> chunkChecksum, IReadOnlyDictionary<string, string> pickupItems, IReadOnlyDictionary<string, string> pickupWeapons,
        HashSet<string> armageddonTurrets, IDictionary<string, int> leftOut)
    {
        var all = layer.Objects.GroupBy(o => o.Handle).ToDictionary(g => g.Key, g => g.First());
        if (all.Count != layer.Objects.Count) throw new InvalidDataException($"{layer.GuerrillaName}: two of its objects have the same handle");
        var taken = new HashSet<uint>();  // the layer's own: its objects keep Guerrilla's handles
        var made = new List<ZoneObject>();
        void LeftOut(string what, int count = 1) => leftOut[what] = (leftOut.TryGetValue(what, out int n) ? n : 0) + count;

        // The barrels (and any destructible object): their chunk files are the world build's.
        var movers = layer.Objects.Where(o => o.ClassHash == RfgMover || o.ClassHash == GeneralMover).ToList();
        var placedMovers = new List<GuerrillaZoneObject>();
        foreach (var o in movers)
        {
            string chunk = Path.GetFileNameWithoutExtension(o.Text("chunk_name") ?? "");
            if (chunk.Length > 0 && chunkChecksum(chunk) != null) placedMovers.Add(o);
            else LeftOut($"{ClassName(schema, o.ClassHash)} {chunk} (its chunk not built with the world)");
        }
        byte[] links = [];
        if (placedMovers.Count > 0)
        {
            (var moverObjects, links) = GuerrillaMovers.Convert(placedMovers, all, schema, propsOf, gameplayPropsOf, lodPropsOf, chunk => chunkChecksum(chunk)!.Value, taken);
            made.AddRange(moverObjects);
        }
        // The propane tanks and ammo boxes.
        var pickupsLeft = new SortedDictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        made.AddRange(GuerrillaPickups.Convert(all, schema, taken, pickupItems, pickupWeapons, pickupsLeft));
        foreach (var (type, n) in pickupsLeft) LeftOut($"pickup {type} (not one the weapons build makes)", n);
        // The walkers' spawn nodes (their groups rfg_<Guerrilla's>, the vehicles build's). (A layer has no roads, which
        // are all the zone's square and the problems are for.)
        var corner = GuerrillaWorld.Corner(layer.Col, layer.Row);
        var (_, nodes) = GuerrillaVehiclePlaces.Convert(all, schema, taken, [], (corner.X, corner.Y), GuerrillaWorld.ZoneWidth, []);
        made.AddRange(nodes);
        // The dummies and the turret's spawn node; any other class has no converter.
        foreach (var o in layer.Objects)
        {
            if (o.ClassHash == Dummy)
            {
                if (ConvertDummy(o, schema, taken) is { } dummy) made.Add(dummy);
                else LeftOut($"object_dummy {o.Text("dummy_type")} (not a dummy_type of Armageddon's)");
            }
            else if (o.ClassHash == TurretNode) made.Add(ConvertTurretNode(o, schema, taken, armageddonTurrets));
            // An area defense's spawn regions (where its continuous attacks' squads appear) aren't made as the game's objects:
            // the guerrilla_campaign mod reads them from its campaign markers (spawn regions with their layer) and uses
            // those of the sets that are loaded.
            else if (o.ClassHash == SpawnRegion) continue;
            else if (o.ClassHash != RfgMover && o.ClassHash != GeneralMover && o.ClassHash != Item && o.ClassHash != Weapon && o.ClassHash != VehicleNode)
                LeftOut($"{ClassName(schema, o.ClassHash)} (no converter)");
        }
        foreach (var o in made)
            if (!all.ContainsKey(o.Handle)) throw new InvalidDataException($"{layer.GuerrillaName}: an object was given the handle {o.Handle:X8}, not Guerrilla's");

        // In Guerrilla's order, each parent before the objects attached to it.
        var byHandle = made.ToDictionary(o => o.Handle);
        var file = ZoneFile.Create(0, 0, relationTable: false);
        var written = new HashSet<uint>();
        void Write(uint handle)
        {
            if (!byHandle.TryGetValue(handle, out var o) || !written.Add(handle)) return;
            if (o.Parent != None) Write(o.Parent);
            file.Insert(file.Objects.Count, o);
        }
        foreach (var o in layer.Objects) Write(o.Handle);
        if (file.Objects.Count > MostObjectsInLayer)
            throw new InvalidDataException($"{layer.GuerrillaName} has {file.Objects.Count} objects; the game makes at most {MostObjectsInLayer} from a layer");
        byte[] bytes = file.ToBytes();
        if (links.Length > 0) bytes = GuerrillaMovers.WithLinks(bytes, links);

        var counts = new SortedDictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        foreach (var o in file.Objects)
        {
            string kind = ClassName(schema, o.ClassHash);
            counts[kind] = counts.GetValueOrDefault(kind) + 1;
        }
        return (bytes, counts);
    }

    /// <summary>A dummy (a place the activity's code finds by its type: Demo Master, the play area's middle): its dummy_type and place, or null if Armageddon's list hasn't the type.</summary>
    static ZoneObject? ConvertDummy(GuerrillaZoneObject o, ZoneSchema schema, ISet<uint> taken)
    {
        string type = o.Text("dummy_type")?.Trim() ?? "";
        var property = schema.Class("object_dummy")?.Property("dummy_type") ?? throw new InvalidDataException("the schema's object_dummy has no dummy_type");
        if (type.Length > 0 && !property.Choices.Contains(type, StringComparer.OrdinalIgnoreCase)) return null;
        if (!taken.Add(o.Handle)) throw new InvalidDataException($"the handle {o.Handle:X8} is another object's");
        return Make(o, DummyOrder.Select(name => name switch
        {
            "dummy_type" when type.Length > 0 => new NewProperty(ZoneProperty.TextType, NameHashes.PropertyHash(name), Text(type)),
            _ when o.Properties.ContainsKey(NameHashes.PropertyHash(name)) => Own(o, name),
            _ => Default(schema, "object_dummy", name),
        }));
    }

    /// <summary>
    /// A turret's spawn node: its turret (Guerrilla's name, or "rfg_" + it where Armageddon's turrets.xtbl has one of
    /// the name, as the vehicles build names Guerrilla's turrets), place, chance and how far it turns; Armageddon's
    /// autoturret at the schema's default (off). Guerrilla's night chance isn't read by Armageddon's.
    /// </summary>
    static ZoneObject ConvertTurretNode(GuerrillaZoneObject o, ZoneSchema schema, ISet<uint> taken, HashSet<string> armageddonTurrets)
    {
        string turret = o.Text("turret_type")?.Trim() ?? "";
        string ours = armageddonTurrets.Contains(turret) ? "rfg_" + turret : turret;
        if (!taken.Add(o.Handle)) throw new InvalidDataException($"the handle {o.Handle:X8} is another object's");
        return Make(o, TurretOrder.Select(name => name switch
        {
            "turret_type" when ours.Length > 0 => new NewProperty(ZoneProperty.TextType, NameHashes.PropertyHash(name), Text(ours)),
            _ when o.Properties.ContainsKey(NameHashes.PropertyHash(name)) => Own(o, name),
            _ => Default(schema, "object_turret_spawn_node", name),
        }));
    }

    static ZoneObject Make(GuerrillaZoneObject o, IEnumerable<NewProperty> properties)
    {
        var obj = ZoneObject.Create(o.ClassHash, o.Handle, o.Number, o.BoundsMin, o.BoundsMax, [.. properties], 0);
        obj.Parent = obj.Sibling = obj.Child = None;
        return obj;
    }

    static NewProperty Own(GuerrillaZoneObject o, string name)
    {
        var (type, raw) = o.Properties[NameHashes.PropertyHash(name)];
        return new NewProperty(type, NameHashes.PropertyHash(name), raw);
    }

    static NewProperty Default(ZoneSchema schema, string className, string name)
    {
        var property = schema.Class(className)?.Property(name) ?? throw new InvalidDataException($"the schema's {className} has no {name}");
        return new NewProperty(property.ZoneType, NameHashes.PropertyHash(property.ZoneName),
                               property.DefaultValue() ?? throw new InvalidDataException($"the schema has no default for {className}'s {name}"));
    }

    static byte[] Text(string text) => [.. Encoding.Latin1.GetBytes(text), 0];

    /// <summary>A class's name for the log (the schema names the movers' classes by their shared part, obj_chunk_ref).</summary>
    static string ClassName(ZoneSchema schema, uint classHash) =>
        classHash == Item ? "item" : classHash == Weapon ? "weapon" : classHash == GeneralMover ? "general_mover" : classHash == RfgMover ? "rfg_mover"
        : schema.Class(classHash)?.Name ?? $"class {classHash:X8}";

    /// <summary>A layer file's zone and layer from its name: the last three numbers (terr01_09_09_137: 9, 9, 137), as Armageddon's 0x6BFCD0 reads them.</summary>
    static (int Col, int Row, int Number)? Place(string name)
    {
        string[] parts = name.Split('_');
        if (parts.Length < 4) return null;
        bool Number(string s, out int n) => int.TryParse(s, NumberStyles.None, CultureInfo.InvariantCulture, out n);
        if (!Number(parts[^3], out int col) || !Number(parts[^2], out int row) || !Number(parts[^1], out int layer)) return null;
        if (col is < 0 or > 15 || row is < 0 or > 15 || layer is < 0x80 or > 0xFE) return null;  // a managed layer's byte: 0x80 and up, not 0xFF
        return (col, row, layer);
    }

    /// <summary>Armageddon's own layer table (table.vpp_pc's copy, which its reader asks first; else misc.vpp_pc's).</summary>
    static XtblFile GameLayerTable(string game)
    {
        string cache = Path.Combine(game, "build", "pc", "cache");
        foreach (string archive in new[] { "table.vpp_pc", "misc.vpp_pc" })
        {
            string path = Path.Combine(cache, archive);
            if (!File.Exists(path)) continue;
            var pack = Packfile.Open(path);
            if (pack.Find(LayerTable) is { } entry) return XtblFile.FromBytes(pack.Read(entry));
        }
        throw new InvalidDataException($"Armageddon's table.vpp_pc and misc.vpp_pc have no {LayerTable}");
    }

    /// <summary>The names of a table's entries: the world's copy (mods\guerrilla_world\) if <paramref name="worldModFolder"/> has one, else Armageddon's.</summary>
    static HashSet<string> GameTableNames(string game, string table, string? worldModFolder = null)
    {
        XtblFile file;
        if (worldModFolder != null && File.Exists(Path.Combine(worldModFolder, table))) file = XtblFile.FromBytes(File.ReadAllBytes(Path.Combine(worldModFolder, table)));
        else
        {
            var misc = Packfile.Open(Path.Combine(game, "build", "pc", "cache", "misc.vpp_pc"));
            file = XtblFile.FromBytes(misc.Read(misc.Find(table) ?? throw new InvalidDataException($"Armageddon's misc.vpp_pc has no {table}")));
        }
        return new HashSet<string>(file.Entries.Select(XtblFile.EntryName), StringComparer.OrdinalIgnoreCase);
    }

    /// <summary>Takes away what <see cref="Write"/> wrote: the containers its asset list override names, the override, and the layer table.</summary>
    public static IEnumerable<string> Remove(string game, string overrides, string worldModFolder)
    {
        string assetFile = AssetListOverride(overrides);
        if (File.Exists(assetFile))
        {
            var own = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var missions = Packfile.Open(Path.Combine(game, "build", "pc", "cache", MissionArchive));
            if (missions.Find(AssetList) is { } entry) own.UnionWith(AsmFile.Read(missions.Read(entry)).Containers.Select(c => c.Name));
            foreach (var container in AsmFile.Read(File.ReadAllBytes(assetFile)).Containers.Where(c => !own.Contains(c.Name)))
                foreach (string file in OverrideLayout.Delete(overrides, container.Name + ".str2_pc")) yield return $"Deleted {file}";
            File.Delete(assetFile);
            yield return $"Deleted {assetFile}";
            string folder = Path.GetDirectoryName(assetFile)!;
            if (!Directory.EnumerateFileSystemEntries(folder).Any()) Directory.Delete(folder);
        }
        string table = Path.Combine(worldModFolder, LayerTable);
        if (File.Exists(table))
        {
            File.Delete(table);
            yield return $"Deleted {table}";
        }
    }

    /// <summary>
    /// Checks what <see cref="Write"/> wrote as the game will take it: the asset list override starts with Armageddon's
    /// own entries byte for byte; the layer table keeps Armageddon's rows; each of our rows names a container the asset
    /// list lists (type 6, flags 0x80, a type 3 primitive a layer, sizes as the container's) and that is on disk with
    /// those layers; each layer is ZONE 46 with flags 4 and no relation table, within the objects the game makes from a
    /// layer, its handles in its zone and layer (the third byte, which member_of_layer checks) and no handle used twice;
    /// each container within the Large Layer slot; the sets within the game's pool and their names within its room.
    /// Notes (not problems): the tables' rows the objects name that the game won't have (a pickup's weapon or item, a
    /// spawn node's vehicle group or turret: the object isn't made). Nothing is checked when the world built none.
    /// </summary>
    public static List<(bool Problem, string Text)> Check(string game, string overrides, string worldModFolder)
    {
        var found = new List<(bool Problem, string Text)>();
        void Problem(string text) => found.Add((true, text));
        void Note(string text) => found.Add((false, text));
        string tableFile = Path.Combine(worldModFolder, LayerTable), assetFile = AssetListOverride(overrides);
        bool hasTable = File.Exists(tableFile), hasAssets = File.Exists(assetFile);
        if (!hasTable && !hasAssets) return found;
        if (!hasTable || !hasAssets)
        {
            Problem($"{(hasTable ? $"mods\\guerrilla_world\\{LayerTable}" : $"{MissionArchive}\\{AssetList}")} is there without " +
                    $"{(hasTable ? $"{MissionArchive}\\{AssetList}" : $"mods\\guerrilla_world\\{LayerTable}")}: build the world again");
            return found;
        }
        string cache = Path.Combine(game, "build", "pc", "cache");
        var missions = Packfile.Open(Path.Combine(cache, MissionArchive));
        byte[] gameAssets = missions.Read(missions.Find(AssetList) ?? throw new InvalidDataException($"Armageddon's {MissionArchive} has no {AssetList}"));
        byte[] ourAssets = File.ReadAllBytes(assetFile);
        var asm = AsmFile.Read(ourAssets);
        int gameCount = AsmFile.Read(gameAssets).Containers.Count;
        // The header (signature, version, count: 8 bytes) differs by the count; every byte after it of Armageddon's comes first.
        if (ourAssets.Length < gameAssets.Length || !ourAssets.AsSpan(8, gameAssets.Length - 8).SequenceEqual(gameAssets.AsSpan(8)))
            Problem($"{MissionArchive}\\{AssetList} doesn't start with Armageddon's {gameCount} entries byte for byte");

        var gameTable = GameLayerTable(game);
        var table = XtblFile.FromBytes(File.ReadAllBytes(tableFile));
        static string RowText(XtblElement row) =>
            $"{XtblFile.EntryName(row)}|{string.Join(",", row.Element("managed_layer_list")?.ElementsNamed("managed_layer_name").Select(e => e.Value.Trim()) ?? [])}|{row.ValueOf("anim_group_name")?.Trim()}";
        var gameRows = gameTable.Entries.ToDictionary(XtblFile.EntryName, RowText, StringComparer.OrdinalIgnoreCase);
        var ourRows = table.Entries.GroupBy(XtblFile.EntryName, StringComparer.OrdinalIgnoreCase).ToDictionary(g => g.Key, g => g.ToList(), StringComparer.OrdinalIgnoreCase);
        foreach (var (name, text) in gameRows)
            if (!ourRows.TryGetValue(name, out var rows) || rows.Count != 1 || RowText(rows[0]) != text)
                Problem($"{LayerTable}: Armageddon's row {name} isn't there as Armageddon has it");
        foreach (var (name, rows) in ourRows.Where(r => r.Value.Count > 1)) Problem($"{LayerTable}: {rows.Count} rows named {name} (the game finds a set by its name)");

        var containers = asm.Containers.GroupBy(c => c.Name, StringComparer.OrdinalIgnoreCase).ToDictionary(g => g.Key, g => g.First(), StringComparer.OrdinalIgnoreCase);
        var ourSets = table.Entries.Where(r => !gameRows.ContainsKey(XtblFile.EntryName(r))).ToList();
        foreach (var extra in asm.Containers.Skip(gameCount).Where(c => !ourSets.Any(r => XtblFile.EntryName(r).Equals(c.Name, StringComparison.OrdinalIgnoreCase))))
            Problem($"{AssetList} lists {extra.Name}, which no row of {LayerTable} names");

        var handles = new Dictionary<uint, string>();
        var named = new List<(string Where, string Class, string Property, string Value)>();
        int layers = 0, objects = 0;
        foreach (var row in ourSets)
        {
            string set = XtblFile.EntryName(row);
            var layerNames = row.Element("managed_layer_list")?.ElementsNamed("managed_layer_name").Select(e => e.Value.Trim()).ToList() ?? [];
            if (layerNames.Count is 0 or > MostLayers) { Problem($"{set}: {layerNames.Count} layers (the game takes 1 to {MostLayers})"); continue; }
            if (!containers.TryGetValue(set, out var entry)) { Problem($"{set}: {AssetList} doesn't list its container"); continue; }
            if (entry.Type != ContainerType || entry.Flags != ContainerFlags)
                Problem($"{set}: its container's entry is type {entry.Type}, flags 0x{entry.Flags:X} (Armageddon's layers' are type {ContainerType}, flags 0x{ContainerFlags:X})");
            string path = ContainerFile(overrides, set);
            if (!File.Exists(path)) { Problem($"{set}: {set}.str2_pc isn't in overrides\\build\\pc\\cache\\guerrilla\\world\\"); continue; }
            var container = Packfile.FromBytes(set, File.ReadAllBytes(path));
            if (container.DataStart != entry.DataOffset || container.CompressedDataSize != entry.CompressedSize)
                Problem($"{set}: {AssetList}'s entry doesn't match its container (data at {entry.DataOffset}, {entry.CompressedSize} bytes; the container's at {container.DataStart}, {container.CompressedDataSize})");
            if (container.DataSize > SlotBytes) Problem($"{set}: its layers take {container.DataSize:N0} bytes, more than the {SlotBytes:N0} of the slot it streams into");
            var wanted = layerNames.Select(l => l + ".layer_pc").ToList();
            if (!wanted.SequenceEqual(container.Entries.Select(e => e.Name), StringComparer.OrdinalIgnoreCase) ||
                !wanted.SequenceEqual(entry.Primitives.Select(p => p.Name), StringComparer.OrdinalIgnoreCase))
            {
                Problem($"{set}: its row's layers ({string.Join(", ", wanted)}), its container's files ({string.Join(", ", container.Entries.Select(e => e.Name))}) " +
                        $"and its entry's ({string.Join(", ", entry.Primitives.Select(p => p.Name))}) aren't the same");
                continue;
            }
            for (int i = 0; i < wanted.Count; i++)
            {
                string layerName = layerNames[i];
                var file = container.Entries[i];
                var primitive = entry.Primitives[i];
                if (primitive.Type != LayerPrimitiveType || primitive.HeaderSize != file.Size || primitive.DataSize != 0 || i >= entry.Sizes.Count || entry.Sizes[i] != file.Size)
                    Problem($"{set}: {file.Name}'s entry (type {primitive.Type}, {primitive.HeaderSize} bytes) isn't a type {LayerPrimitiveType} layer of its {file.Size} bytes");
                if (Place(layerName) is not { } layerPlace) { Problem($"{set}: {layerName} isn't named <world>_<column>_<row>_<layer 128-254>"); continue; }
                var (col, zoneRow, number) = layerPlace;
                byte[] bytes = container.Read(file);
                ZoneFile zone;
                try { zone = ZoneFile.Read(bytes); }
                catch (InvalidDataException e) { Problem($"{set}: {file.Name} isn't a layer the game reads ({e.Message})"); continue; }
                if (zone.HasRelationTable || BitConverter.ToUInt32(bytes, 0x24) != 4) Problem($"{set}: {file.Name} has a relation table or flags other than 4 (a layer's)");
                if (zone.Objects.Count > MostObjectsInLayer) Problem($"{set}: {file.Name} has {zone.Objects.Count} objects; the game makes at most {MostObjectsInLayer} from a layer");
                layers++;
                objects += zone.Objects.Count;
                uint place = (uint)(col << 4 | zoneRow);
                foreach (var o in zone.Objects)
                {
                    if (o.Handle >> 24 != place || (o.Handle >> 16 & 0xFF) != number)
                        Problem($"{set}: {file.Name}'s object {o.Handle:X8} isn't in its zone and layer ({place:X2}, {number:X2}): the game wouldn't take it away with the set");
                    if (!handles.TryAdd(o.Handle, set)) Problem($"{set}: the handle {o.Handle:X8} is {handles[o.Handle]}'s too");
                    foreach (var (cls, property) in new[] { (Weapon, "weapon_type"), (Item, "item_type"), (VehicleNode, "spawn_set"), (TurretNode, "turret_type") })
                        if (o.ClassHash == cls && o.Property(property) is { } p)
                            named.Add((set, cls == Weapon ? "weapon" : cls == Item ? "item" : cls == VehicleNode ? "vehicle spawn node" : "turret spawn node", property, p.Text.TrimEnd('\0')));
                }
            }
        }
        // The sets the game keeps (Armageddon's with a container, and ours) and their names, if all of them took room.
        int sets = gameRows.Keys.Count(containers.ContainsKey) + ourSets.Count;
        int nameBytes = table.Entries.Sum(r => XtblFile.EntryName(r).Length + 1 +
                                               (r.Element("managed_layer_list")?.ElementsNamed("managed_layer_name").Sum(e => e.Value.Trim().Length + 1) ?? 0));
        if (sets > MostSets) Problem($"{sets} managed layer sets: the game's pool holds {MostSets}");
        if (nameBytes > NameRoom) Problem($"the sets' and layers' names take {nameBytes:N0} bytes: the game's room for them is {NameRoom:N0}");
        // What the objects name, against the tables the game will have (the world's own where it has them).
        var tables = new Dictionary<string, HashSet<string>>
        {
            ["weapon_type"] = GameTableNames(game, "weapons.xtbl", worldModFolder),
            ["item_type"] = GameTableNames(game, "items_3d.xtbl", worldModFolder),
            ["spawn_set"] = GameTableNames(game, "spawn_group_vehicle.xtbl", worldModFolder),
            ["turret_type"] = GameTableNames(game, "turrets.xtbl", worldModFolder),
        };
        foreach (var group in named.Where(n => !tables[n.Property].Contains(n.Value)).GroupBy(n => (n.Class, n.Property, n.Value)))
            Note($"{string.Join(", ", group.Select(g => g.Where).Distinct())}: {group.Count()} {group.Key.Class}(s) name {group.Key.Value}, which the game's " +
                 $"{(group.Key.Property == "weapon_type" ? "weapons" : group.Key.Property == "item_type" ? "items_3d" : group.Key.Property == "spawn_set" ? "spawn_group_vehicle" : "turrets")}.xtbl " +
                 "won't have: not made until it does");
        if (!found.Any(f => f.Problem))
            found.Insert(0, (false, $"{ourSets.Count} sets of Guerrilla's ({layers} layers, {objects} objects), each listed in {MissionArchive}\\{AssetList} after Armageddon's " +
                                    $"{gameCount} entries and in overrides\\build\\pc\\cache\\; {sets} of the {MostSets} sets the game holds, their names " +
                                    $"{nameBytes:N0} of {NameRoom:N0} bytes"));
        return found;
    }
}
