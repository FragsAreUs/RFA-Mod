using System.IO;
using System.Text;
using RfaFormats.Levels;
using RfaFormats.Tables;
using RfaFormats.Zones;

namespace RfaEditor;

// What the level's spawn nodes spawn, kept in its spawn resources (spawn_resource.xtbl: the entry its row names,
// resource_reservation; see SpawnResource): a level can only spawn what its entry holds, so a node placed for a
// squad, vehicle or exo the entry hasn't spawned nothing. Whenever a change is kept (a node added, duplicated or
// pasted, its squad, vehicle set or exo changed, undo and redo), what the zone's spawn nodes need is worked out
// from the game's tables and whatever the entry lacks is added to it:
//   squad spawns: the squad's leader and members (squads.xtbl member_info: characters, civilians among them), and
//     with squad_vehicle its vehicle set's vehicles (vehicle_type: spawn_group_vehicle.xtbl), with squad_exo its exo;
//   vehicle spawns: their set's vehicles (spawn_set: spawn_group_vehicle.xtbl vehicle_type, vehicles.xtbl names);
//   exo spawns: the exo (exo_type: exo.xtbl, the entry's exo_list);
//   animated rigs with a body of their own (a monolith, the queen: anim_rig_objects.xtbl's Props > Prop > Stream_Human,
//     a character the game streams as the rig's model; monolith_small / medium / large).
// What the editor added this way, and only that, is taken out again once no spawn node needs it (a node's squad
// changed, the node deleted): the editor keeps a record of what it added to which entry (spawn_resources_added.txt
// next to it). What an entry had to begin with, and what you put in it with Spawn resources, stays (saving a list there
// makes all of it yours: MarkSpawnResourcesChosen).
// An entry other levels share is first split off into the level's own (its row then names that), as Spawn
// resources' "Give this level its own" does, so no other level changes. A resource streams at most 16 characters
// (GuerrillaSquads.HumanSlots: the game's 17 stream slots less the player): past that a character is left out, and
// said. The game reads its tables at start-up, so the entry applies from its next start; Live has the running game
// reserve what a node it makes needs at once (editor_reserve, see LiveReserveLines).
public partial class LevelEditor
{
    const string SpawnTablesArchive = @"build\pc\cache\table.vpp_pc";
    const int MostResourceCharacters = 16;
    static readonly string[] DlcTablePrefixes = ["", "dlc01_", "dlc02_", "dlc03_", "dlc04_"];
    static readonly (string Table, string Row, string Key)[] LevelTables =
    [
        ("wrecking_crew_maps.xtbl", "wc_map_list", "file_name"),
        ("mp_levels.xtbl", "mp_level_list", "Filename"),
        ("missions.xtbl", "mission", "Name"),
    ];

    /// <summary>What spawn nodes need: characters, vehicles (vehicles.xtbl names) and exos (exo.xtbl names).</summary>
    sealed class SpawnNeeds
    {
        public readonly List<string> Characters = [], Vehicles = [], Exos = [];
        public bool Any => Characters.Count + Vehicles.Count + Exos.Count > 0;

        public void Add(List<string> list, string? name)
        {
            if (name?.Trim() is { Length: > 0 } n && !n.Equals("None", StringComparison.OrdinalIgnoreCase) && !list.Contains(n, StringComparer.OrdinalIgnoreCase)) list.Add(n);
        }
    }

    bool spawnLevelUnknownSaid;
    static readonly string AddedRecordPath = Path.Combine(AppContext.BaseDirectory, "spawn_resources_added.txt");

    /// <summary>What the editor added to each entry, by entry: "character:x", "vehicle:x", "exo:x". Read afresh each time.</summary>
    internal static Dictionary<string, HashSet<string>> ReadAddedRecord()
    {
        var record = new Dictionary<string, HashSet<string>>(StringComparer.OrdinalIgnoreCase);
        try
        {
            if (!File.Exists(AddedRecordPath)) return record;
            foreach (string line in File.ReadAllLines(AddedRecordPath))
            {
                string[] parts = line.Split('\t');
                if (parts.Length < 2 || line.StartsWith('#')) continue;
                if (!record.TryGetValue(parts[0], out var set)) record[parts[0]] = set = new(StringComparer.OrdinalIgnoreCase);
                if (parts[1].Length > 0) set.Add(parts[1]);
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
        return record;
    }

    /// <summary>A list saved in Spawn resources is the user's own, all of it: none of it is taken out for the nodes' sake.</summary>
    internal static void MarkSpawnResourcesChosen(string entryName)
    {
        var record = ReadAddedRecord();
        record[entryName] = new(StringComparer.OrdinalIgnoreCase);
        WriteAddedRecord(record);
    }

    internal static void WriteAddedRecord(Dictionary<string, HashSet<string>> record)
    {
        try
        {
            var lines = new List<string> { "# What the level editor added to each level's spawn resources (spawn_resource.xtbl), to take out again once no spawn node needs it." };
            foreach (var (entryName, set) in record.OrderBy(r => r.Key, StringComparer.OrdinalIgnoreCase))
            {
                lines.Add(entryName + "\t");  // (the entry is known, even with nothing of the editor's in it)
                lines.AddRange(set.Order(StringComparer.OrdinalIgnoreCase).Select(item => entryName + "\t" + item));
            }
            File.WriteAllLines(AddedRecordPath, lines);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            EditorLog.Write("The spawn resources' record couldn't be written: " + ex.Message);
        }
    }
    string? spawnNeedsChecked;  // the needs last checked against the entry: the same again needs no look
    readonly HashSet<string> spawnFullSaid = new(StringComparer.OrdinalIgnoreCase);

    static bool IsSpawnNodeClass(string cls) =>
        cls.Equals("object_squad_spawn_node", StringComparison.OrdinalIgnoreCase) ||
        cls.Equals("object_vehicle_spawn_node", StringComparison.OrdinalIgnoreCase) ||
        cls.Equals("object_exo_spawn_node", StringComparison.OrdinalIgnoreCase) ||
        cls.Equals("object_anim_rig", StringComparison.OrdinalIgnoreCase);

    /// <summary>A table's entry by name, the DLCs' tables searched too (as <see cref="ObjectModels"/>); null if none.</summary>
    XtblElement? SpawnTableEntry(string tableName, string entryName)
    {
        if (context.Assets.Table(tableName) is not { } table) return null;
        return DlcTablePrefixes.Select(prefix => prefix.Length == 0 ? table : context.Assets.Table(prefix + tableName)).OfType<XtblFile>()
            .SelectMany(t => t.Entries).FirstOrDefault(e => XtblFile.EntryName(e).Equals(entryName, StringComparison.OrdinalIgnoreCase));
    }

    static string? SpawnText(ZoneObject obj, string property) =>
        obj.Property(property) is { Type: ZoneProperty.TextType } p && p.Text.Trim().Length > 0 ? p.Text.Trim() : null;

    static bool SpawnFlag(ZoneObject obj, string property) => obj.Property(property) is { Size: >= 1 } p && p.Value[0] != 0;

    /// <summary>A vehicle set's vehicles (spawn_group_vehicle.xtbl's vehicle_type), into the needs.</summary>
    void AddVehicleSet(SpawnNeeds needs, string? set)
    {
        if (set == null || set.Equals("None", StringComparison.OrdinalIgnoreCase)) return;
        if (SpawnTableEntry("spawn_group_vehicle.xtbl", set) is not { } entry) return;
        foreach (XtblElement list in entry.ElementsNamed("vehicle_list"))
            foreach (XtblElement vehicle in list.ElementsNamed("vehicle_type")) needs.Add(needs.Vehicles, vehicle.Value);
    }

    /// <summary>What one object spawns (nothing for anything but a squad, vehicle or exo spawn node).</summary>
    void AddSpawnNeeds(SpawnNeeds needs, ZoneObject obj)
    {
        switch (names.Class(obj.ClassHash).ToLowerInvariant())
        {
            case "object_squad_spawn_node":
                if (SpawnText(obj, "squad_def") is string squad && SpawnTableEntry("squads.xtbl", squad) is { } entry)
                {
                    needs.Add(needs.Characters, entry.Element("squad_leader")?.ValueOf("member_info"));
                    foreach (XtblElement list in entry.ElementsNamed("squad_member_list"))
                        foreach (XtblElement member in list.ElementsNamed("squad_member")) needs.Add(needs.Characters, member.ValueOf("member_info"));
                }
                if (SpawnFlag(obj, "squad_vehicle")) AddVehicleSet(needs, SpawnText(obj, "vehicle_type"));
                if (SpawnFlag(obj, "squad_exo")) needs.Add(needs.Exos, SpawnText(obj, "exo_type"));
                break;
            case "object_vehicle_spawn_node":
                AddVehicleSet(needs, SpawnText(obj, "spawn_set"));
                break;
            case "object_exo_spawn_node":
                needs.Add(needs.Exos, SpawnText(obj, "exo_type"));
                break;
            case "object_anim_rig":
                if (SpawnText(obj, "rig_name") is string rig && SpawnTableEntry("anim_rig_objects.xtbl", rig) is { } rigEntry)
                    foreach (XtblElement props in rigEntry.ElementsNamed("Props"))
                        foreach (XtblElement prop in props.ElementsNamed("Prop")) needs.Add(needs.Characters, prop.ValueOf("Stream_Human"));
                break;
        }
    }

    /// <summary>
    /// The lines that have the running game hold what a node it is about to make spawns (editor_reserve, before its
    /// editor_create): its characters, its vehicles, and each exo as the character it is made of (exo.xtbl's
    /// Character: the game holds an exo as its character, 0x761D70).
    /// </summary>
    IEnumerable<string> LiveReserveLines(ZoneObject obj)
    {
        if (!IsSpawnNodeClass(names.Class(obj.ClassHash))) yield break;
        var needs = new SpawnNeeds();
        AddSpawnNeeds(needs, obj);
        foreach (string exo in needs.Exos)
            needs.Add(needs.Characters, SpawnTableEntry("exo.xtbl", exo)?.ValueOf("Character"));
        foreach (string character in needs.Characters) yield return "editor_reserve character " + character;
        foreach (string vehicle in needs.Vehicles) yield return "editor_reserve vehicle " + vehicle;
    }

    /// <summary>The level's row naming its spawn resources: its table, row and key tags, its key, and the entry; null if none.</summary>
    (string Table, string Row, string KeyTag, string Key, string Entry)? SpawnLevelRow()
    {
        static string Bare(string name) => Path.GetFileName(name).Split('.')[0];
        var keys = new List<string>();
        void Key(string? name)
        {
            if (name == null) return;
            string bare = Bare(name);
            if (bare.Length > 0 && !keys.Contains(bare, StringComparer.OrdinalIgnoreCase)) keys.Add(bare);
            if (bare.StartsWith("p_", StringComparison.OrdinalIgnoreCase)) Key(bare[2..]);
        }
        Key(zoneName);
        Key(container);
        Key(archive);
        foreach (var (tableName, row, keyTag) in LevelTables)
        {
            string table;
            try { table = Encoding.Latin1.GetString(context.Workspace.Read(SpawnTablesArchive, tableName)); }
            catch (Exception ex) when (ex is IOException or InvalidDataException) { continue; }
            foreach (string key in keys)
                if (LevelFiles.RowValue(table, row, keyTag, key, "resource_reservation") is { Length: > 0 } entry)
                    return (tableName, row, keyTag, key, entry);
        }
        return null;
    }

    /// <summary>
    /// After a change is kept: whatever the zone's spawn nodes need that the level's spawn resources lack is added to
    /// them (see the top). Quiet when nothing is missing.
    /// </summary>
    void KeepSpawnResourcesStocked()
    {
        var needs = new SpawnNeeds();
        try
        {
            foreach (ZoneObject obj in zone.Objects)
                if (IsSpawnNodeClass(names.Class(obj.ClassHash))) AddSpawnNeeds(needs, obj);
        }
        catch (InvalidOperationException) { return; }  // (the objects changed under us: next time)
        string signature = string.Join("|", new[] { needs.Characters, needs.Vehicles, needs.Exos }
            .Select(list => string.Join(",", list.Order(StringComparer.OrdinalIgnoreCase))));
        if (signature == spawnNeedsChecked) return;
        spawnNeedsChecked = signature;
        if (SpawnLevelRow() is not { } level)
        {
            if (needs.Any && !spawnLevelUnknownSaid)
            {
                spawnLevelUnknownSaid = true;
                context.Log($"Spawn resources: no Ruin, multiplayer or mission row names {zoneName}'s spawn resources, so what its spawn nodes need isn't added to them " +
                            "(the game may not spawn a squad, vehicle or exo it hasn't).");
            }
            return;
        }
        try
        {
            string resources = Encoding.Latin1.GetString(context.Workspace.Read(SpawnTablesArchive, "spawn_resource.xtbl"));
            SpawnResource entry = SpawnResource.Read(resources, level.Entry) ?? new SpawnResource { Name = level.Entry };
            var added = new List<string>();
            var removed = new List<string>();
            var left = new List<string>();
            var record = ReadAddedRecord();
            if (!record.TryGetValue(level.Entry, out var ours)) record[level.Entry] = ours = new(StringComparer.OrdinalIgnoreCase);
            // The editor's own that no spawn node needs now: out first, so their room counts.
            void Unneeded(List<string> have, List<string> want, string what)
            {
                foreach (string name in have.ToList())
                {
                    if (!ours.Contains(what + ":" + name) || want.Contains(name, StringComparer.OrdinalIgnoreCase)) continue;
                    have.RemoveAll(n => n.Equals(name, StringComparison.OrdinalIgnoreCase));
                    ours.Remove(what + ":" + name);
                    removed.Add($"{name} ({what})");
                }
            }
            Unneeded(entry.Characters, needs.Characters, "character");
            Unneeded(entry.Vehicles, needs.Vehicles, "vehicle");
            Unneeded(entry.Exos, needs.Exos, "exo");
            void Missing(List<string> have, List<string> want, string what, int most = int.MaxValue)
            {
                foreach (string name in want)
                {
                    if (have.Contains(name, StringComparer.OrdinalIgnoreCase)) continue;
                    if (have.Count >= most)
                    {
                        if (spawnFullSaid.Add(name)) left.Add(name);
                        continue;
                    }
                    have.Add(name);
                    ours.Add(what + ":" + name);
                    added.Add($"{name} ({what})");
                }
            }
            Missing(entry.Characters, needs.Characters, "character", MostResourceCharacters);
            Missing(entry.Vehicles, needs.Vehicles, "vehicle");
            Missing(entry.Exos, needs.Exos, "exo");
            if (left.Count > 0)
                context.Log($"Spawn resources: {level.Entry} already lists {MostResourceCharacters} characters, as many as the game streams; left out: {string.Join(", ", left)}. " +
                            "Take some out with Spawn resources, or use squads of fewer kinds of people.");
            if (added.Count == 0 && removed.Count == 0 && SpawnResourceFiles.GuerrillaLacks(context.Workspace, entry.Name))
            {
                // The Guerrilla world's copy of the table, which the game reads in place of the override, was made before
                // this level's entry was: the entry goes into it as it is.
                SpawnResourceFiles.Change(context.Workspace, table => SpawnResource.Read(table, entry.Name) == null ? entry.WriteTo(table) : null);
                context.Log($"Spawn resources: {entry.Name} copied into mods\\guerrilla_world\\spawn_resource.xtbl, which the game reads in place of the table with the Guerrilla world installed " +
                            "(the level had Armageddon's Default without it). Restart the game for it to apply.");
            }
            if (added.Count == 0 && removed.Count == 0)
            {
                WriteAddedRecord(record);  // (an entry seen for the first time)
                return;
            }

            // An entry other levels name too is split off into this level's own first.
            int sharing = 0;
            foreach (var (tableName, row, keyTag) in LevelTables)
            {
                try
                {
                    string table = Encoding.Latin1.GetString(context.Workspace.Read(SpawnTablesArchive, tableName));
                    sharing += LevelFiles.Keys(table, row, keyTag).Count(k => LevelFiles.RowValue(table, row, keyTag, k, "resource_reservation") is string e &&
                                                                              e.Equals(level.Entry, StringComparison.OrdinalIgnoreCase));
                }
                catch (Exception ex) when (ex is IOException or InvalidDataException) { }
            }
            string from = level.Entry, guerrillaToo = "";
            if (sharing > 1)
            {
                var taken = SpawnResource.Names(resources).ToHashSet(StringComparer.OrdinalIgnoreCase);
                string own = level.Key;
                for (int n = 2; taken.Contains(own); n++) own = $"{level.Key}_{n}";
                entry.Name = own;
                record[own] = ours;  // (what the editor added is the level's own entry's now; the shared one is left as it was)
                record.Remove(from);
                string table = Encoding.Latin1.GetString(context.Workspace.Read(SpawnTablesArchive, level.Table));
                string changed = LevelFiles.WithRowValue(table, level.Row, level.KeyTag, level.Key, "resource_reservation", own)
                                 ?? throw new InvalidDataException($"{level.Table}'s row for {level.Key} couldn't be changed");
                guerrillaToo = SpawnResourceFiles.Write(context.Workspace, entry, from);
                context.Workspace.WriteOverride(SpawnTablesArchive, level.Table, Encoding.Latin1.GetBytes(changed));
                context.Log($"Spawn resources: {level.Key} shared {from} with {sharing - 1} other level(s); it has its own now, \"{own}\" ({level.Table} names it).");
            }
            else guerrillaToo = SpawnResourceFiles.Write(context.Workspace, entry);
            WriteAddedRecord(record);
            if (added.Count > 0)
                context.Log($"Spawn resources: added to {entry.Name}{guerrillaToo}, for the level's spawn nodes: {string.Join(", ", added)}.");
            if (removed.Count > 0)
                context.Log($"Spawn resources: taken out of {entry.Name}, as no spawn node needs them now: {string.Join(", ", removed)}.");
            context.Log("  The game reads its tables at start-up: restart it for a normal load to have them" + (added.Count > 0 ? " (Live spawns new ones now)." : "."));
            context.OverridesChanged();
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException or UnauthorizedAccessException)
        {
            spawnNeedsChecked = null;  // (tried again at the next change)
            context.Log("Spawn resources: couldn't be kept up to date with the spawn nodes: " + ex.Message);
        }
    }
}
