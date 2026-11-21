using System.Globalization;
using RfaFormats.Tables;
using RfaFormats.Zones;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Guerrilla's tables for the guerrilla_campaign mod, which runs Guerrilla's campaign systems (the
/// alert level and the EDF's answer to it, EDF control, morale, ...) from Guerrilla's own numbers.
/// Each is copied as it is from the player's own copy of Guerrilla (its misc.vpp_pc) into
/// mods\guerrilla_campaign\tables\, where the mod reads it by name; never shipped.
/// </summary>
public static class GuerrillaCampaign
{
    /// <summary>The folder in mods\guerrilla_campaign\ the tables go into.</summary>
    public const string TablesFolder = "tables";
    /// <summary>Each of Guerrilla's vehicles and its team, a line each (name, a tab, the team).</summary>
    public const string VehicleTeams = "vehicle_teams.txt";
    /// <summary>Guerrilla's restricted areas (the EDF's bases), a line each: see <see cref="RestrictedAreas"/>.</summary>
    public const string RestrictedAreasFile = "restricted_areas.txt";
    /// <summary>Guerrilla's kill regions (its pits and minefields), a line each: see <see cref="KillRegions"/>.</summary>
    public const string KillRegionsFile = "kill_regions.txt";
    const int MostPolygonPoints = 64;  // restricted_area_polygon_data: vector2[64] each

    /// <summary>
    /// The tables the mod reads: the alert level's bands and decay, what each kind of damage and death
    /// adds to it, the difficulty's multipliers, the EDF's and the guerrillas' reinforcements, the
    /// districts, the missions (which district each belongs to) and the activities.
    /// </summary>
    public static readonly string[] Tables =
    [
        "alert_level.xtbl", "alert_value.xtbl", "difficulty.xtbl", "reinforcement_profile.xtbl", "guerrilla_reinforcement.xtbl",
        "territories.xtbl", "missions.xtbl", "activity.xtbl", "activity_demolitions_master.xtbl",
        // The other kinds of activity, and where they spawn by themselves.
        "activity_spawn.xtbl", "activity_timed_delivery.xtbl", "activity_house_arrest.xtbl", "activity_area_defense.xtbl",
        "activity_riding_shotgun.xtbl", "activity_courier_chase.xtbl", "activity_continuous_attack.xtbl", "raid_data.xtbl",
        "activity_tanks_a_lot.xtbl", "activity_mayhem.xtbl",
        // Guerrilla's tweak values: what kills and propaganda do to morale.
        "tweak_table.xtbl",
    ];

    /// <summary>The buildings that change a district by being destroyed: see <see cref="EdfBuildings"/>.</summary>
    public const string EdfBuildingsFile = "edf_buildings.txt";

    /// <summary>The buildings with a value of their own, for mayhem's score: see <see cref="BuildingValues"/>.</summary>
    public const string BuildingValuesFile = "building_values.txt";

    /// <summary>
    /// Guerrilla's effects the campaign's activities play themselves, built with the world's effects (GuerrillaWorld, as
    /// rfg_ effects): Transporter's ground ring at its end (gp_marker_target_a, objective_highlight_ground_marker_add,
    /// Re-Mars-tered 0xB2E1E0).
    /// </summary>
    public static readonly string[] CampaignEffects = ["gp_marker_target_a"];

    /// <summary>The campaign's markers, in mods\guerrilla_campaign\: see <see cref="CampaignObjects"/>.</summary>
    public const string CampaignObjectsFile = @"world\campaign_objects.xml";

    /// <summary>
    /// The classes of Guerrilla's campaign markers: activity, mission and safehouse nodes Armageddon has no code
    /// for, so the world converter leaves them out and the mod reads them from <see cref="CampaignObjectsFile"/>.
    /// </summary>
    static readonly string[] CampaignClasses =
    [
        "object_demolitions_master_node", "object_house_arrest_node", "object_raid_node", "object_riding_shotgun_node",
        "object_delivery_node", "object_area_defense_node", "object_courier_end_point", "object_convoy_end_point",
        "object_activity_spawn", "object_roadblock_node", "object_mission_start_node", "object_safehouse", "object_upgrade_node",
        "object_bftp_node",
    ];

    public static IEnumerable<string> Build(string guerrilla, string campaignFolder)
    {
        var guerrillaTables = Packfile.Open(Path.Combine(guerrilla, "build", "pc", "cache", "misc.vpp_pc"));
        string folder = Path.Combine(campaignFolder, TablesFolder);
        Directory.CreateDirectory(folder);
        var missing = new List<string>();
        int copied = 0;
        foreach (string table in Tables)
        {
            if (guerrillaTables.Find(table) is not { } entry) { missing.Add(table); continue; }
            File.WriteAllBytes(Path.Combine(folder, table), guerrillaTables.Read(entry));
            copied++;
        }
        yield return $"Campaign tables: {copied} of Guerrilla's tables copied into {folder} (the alert level, reinforcements, districts, activities)";
        if (missing.Count > 0) yield return $"Campaign tables: Guerrilla's misc.vpp_pc has no {string.Join(", ", missing)}; the mod keeps Guerrilla's usual values for those";

        // Whose each vehicle is (Guerrilla's vehicles.xtbl, Default_Team: EDF, Guerilla, Civilian, Marauder): the alert
        // rises when the EDF's are wrecked. The vehicles' own table can't say: Armageddon's has no EDF to name.
        if (guerrillaTables.Find("vehicles.xtbl") is { } vehicles)
        {
            var lines = new List<string>();
            foreach (var entry in XtblFile.FromBytes(guerrillaTables.Read(vehicles)).Entries)
                if (entry.ValueOf("Default_Team")?.Trim() is { Length: > 0 } team)
                    lines.Add($"{XtblFile.EntryName(entry)}\t{team}");
            File.WriteAllLines(Path.Combine(campaignFolder, VehicleTeams), lines);
            yield return $"Campaign tables: {lines.Count} vehicles' teams ({lines.Count(l => l.EndsWith("\tEDF", StringComparison.OrdinalIgnoreCase))} the EDF's) in {VehicleTeams}";
        }

        yield return RestrictedAreas(Path.Combine(guerrilla, "build", "pc", "cache"), campaignFolder);
        yield return KillRegions(Path.Combine(guerrilla, "build", "pc", "cache"), campaignFolder);
        yield return CampaignObjects(Path.Combine(guerrilla, "build", "pc", "cache"), guerrillaTables, campaignFolder);
        yield return EdfBuildings(Path.Combine(guerrilla, "build", "pc", "cache"), guerrillaTables, campaignFolder);
        yield return BuildingValues(Path.Combine(guerrilla, "build", "pc", "cache"), guerrillaTables, campaignFolder);
    }

    /// <summary>
    /// The buildings with a value of their own, for the guerrilla_campaign mod's mayhem (its cost of damage,
    /// activity_register_damage_get_cost, Re-Mars-tered 0x7705C0: a building whose gameplay_properties row has a value,
    /// in millions of credits, costs that, else its size and importance decide): each of Guerrilla's rfg_movers whose
    /// row's value isn't -1 (25 rows have one: the EDF's headquarters, power cores, armories, ... and a few others).
    /// Armageddon's gameplay_properties has no such field, and its reader can merge rows that differ only by it, so
    /// the value goes by building. A line each: value, Guerrilla's handle, its place (op), the value. A building in a
    /// zone and again in its p_ file is written once.
    /// </summary>
    static string BuildingValues(string guerrillaCache, Packfile guerrillaMisc, string campaignFolder)
    {
        if (guerrillaMisc.Find("gameplay_properties.xtbl") is not { } propertiesEntry) return "Building values: Guerrilla's misc.vpp_pc has no gameplay_properties.xtbl; none written";
        var values = new Dictionary<string, float>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in XtblFile.FromBytes(guerrillaMisc.Read(propertiesEntry)).Entries)
            if (float.TryParse(entry.ValueOf("value")?.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out float value) && value != -1f)
                values.TryAdd(XtblFile.EntryName(entry), value);
        var zones = Packfile.Open(Path.Combine(guerrillaCache, "zonescript_terr01.vpp_pc"));
        uint moverClass = NameHashes.ClassHash("rfg_mover");
        var lines = new List<string> { "# The buildings with a value of their own (Guerrilla's gameplay_properties value, millions of credits), for the guerrilla_campaign mod's mayhem (written by editor\\GuerrillaBuilder.exe with the people)" };
        var written = new HashSet<uint>();
        static string F(float v) => v.ToString("0.###", CultureInfo.InvariantCulture);
        foreach (var entry in zones.Entries.Where(e => e.Name.EndsWith(".rfgzone_pc", StringComparison.OrdinalIgnoreCase))
                                           .OrderBy(e => e.Name.StartsWith("p_", StringComparison.OrdinalIgnoreCase)).ThenBy(e => e.Name, StringComparer.OrdinalIgnoreCase))
            foreach (var o in GuerrillaZones.Objects(zones.Read(entry)).Where(o => o.ClassHash == moverClass))
            {
                if (o.Text("gameplay_props")?.Trim() is not { Length: > 0 } name || !values.TryGetValue(name, out float value)) continue;
                if (o.Value("op") is not { Length: >= 12 } op || !written.Add(o.Handle)) continue;
                lines.Add($"value {o.Handle:X8} {F(BitConverter.ToSingle(op, 0))} {F(BitConverter.ToSingle(op, 4))} {F(BitConverter.ToSingle(op, 8))} {F(value)}");
            }
        Directory.CreateDirectory(campaignFolder);
        File.WriteAllLines(Path.Combine(campaignFolder, BuildingValuesFile), lines);
        return $"Building values: {written.Count} buildings of {values.Count} gameplay properties with a value, in {BuildingValuesFile}";
    }

    /// <summary>
    /// The buildings whose destruction changes a district, for the guerrilla_campaign mod (district_sources.cpp):
    /// each of Guerrilla's rfg_movers whose gameplay_properties row is the EDF's and gives EDF control, and every
    /// one with the chunk flag propaganda (its billboards). Armageddon's gameplay_properties reader keeps neither
    /// the control column nor the team for this, so the numbers come from Guerrilla's own row. A line each:
    /// building, Guerrilla's handle, its place (op), its points of control, its importance (0 none .. 3 high),
    /// propaganda (0/1). A building in a zone and again in its p_ file is written once.
    /// </summary>
    static string EdfBuildings(string guerrillaCache, Packfile guerrillaMisc, string campaignFolder)
    {
        if (guerrillaMisc.Find("gameplay_properties.xtbl") is not { } propertiesEntry) return "EDF buildings: Guerrilla's misc.vpp_pc has no gameplay_properties.xtbl; none written";
        var rows = new Dictionary<string, (bool Edf, int Importance, float Control, bool Propaganda)>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in XtblFile.FromBytes(guerrillaMisc.Read(propertiesEntry)).Entries)
        {
            bool edf = string.Equals(entry.ValueOf("team")?.Trim(), "EDF", StringComparison.OrdinalIgnoreCase);
            int importance = entry.ValueOf("importance")?.Trim().ToUpperInvariant() switch { "LOW" => 1, "MEDIUM" => 2, "HIGH" => 3, _ => 0 };
            float control = float.TryParse(entry.ValueOf("control")?.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out float c) ? c : 0f;
            bool propaganda = entry.Element("chunk_flags")?.ElementsNamed("Flag").Any(f => string.Equals(f.Value.Trim(), "propaganda", StringComparison.OrdinalIgnoreCase)) == true;
            rows.TryAdd(XtblFile.EntryName(entry), (edf, importance, control, propaganda));
        }
        var zones = Packfile.Open(Path.Combine(guerrillaCache, "zonescript_terr01.vpp_pc"));
        uint moverClass = NameHashes.ClassHash("rfg_mover");
        var lines = new List<string> { "# The buildings that change a district by being destroyed, for the guerrilla_campaign mod (written by editor\\GuerrillaBuilder.exe with the people)" };
        var written = new HashSet<uint>();
        int edfBuildings = 0, billboards = 0;
        float points = 0f;
        static string F(float v) => v.ToString("0.###", CultureInfo.InvariantCulture);
        foreach (var entry in zones.Entries.Where(e => e.Name.EndsWith(".rfgzone_pc", StringComparison.OrdinalIgnoreCase))
                                           .OrderBy(e => e.Name.StartsWith("p_", StringComparison.OrdinalIgnoreCase)).ThenBy(e => e.Name, StringComparer.OrdinalIgnoreCase))
            foreach (var o in GuerrillaZones.Objects(zones.Read(entry)).Where(o => o.ClassHash == moverClass))
            {
                if (o.Text("gameplay_props")?.Trim() is not { Length: > 0 } name || !rows.TryGetValue(name, out var row)) continue;
                float control = row.Edf ? ControlPoints(row.Importance, row.Control) : 0f;
                if (control <= 0f && !row.Propaganda) continue;
                if (!written.Add(o.Handle)) continue;
                if (o.Value("op") is not { Length: >= 12 } op) continue;
                lines.Add($"building {o.Handle:X8} {F(BitConverter.ToSingle(op, 0))} {F(BitConverter.ToSingle(op, 4))} {F(BitConverter.ToSingle(op, 8))} {F(control)} {row.Importance} {(row.Propaganda ? 1 : 0)}");
                if (control > 0f) { edfBuildings++; points += control; }
                if (row.Propaganda) billboards++;
            }
        Directory.CreateDirectory(campaignFolder);
        File.WriteAllLines(Path.Combine(campaignFolder, EdfBuildingsFile), lines);
        return $"EDF buildings: {edfBuildings} give control ({F(points)} points in all), {billboards} are propaganda, in {EdfBuildingsFile}";
    }

    /// <summary>
    /// A building's points of EDF control, as object_mover::set_control_value (Re-Mars-tered 0xA13AF0) makes them from
    /// its gameplay_properties row's importance and control value: low 0, 0.5, 1, 2, 3 give 0, 1, 1, 2, 3 (anything
    /// else 1); medium 0, 1, 2, 3 give 0, 10, 12, 15 (anything else 10); high 1 gives 80, anything else itself.
    /// </summary>
    static float ControlPoints(int importance, float control) => importance switch
    {
        1 => control == 0f ? 0f : control == 0.5f || control == 1f ? 1f : control == 2f ? 2f : control == 3f ? 3f : 1f,
        2 => control == 0f ? 0f : control == 1f ? 10f : control == 2f ? 12f : control == 3f ? 15f : 10f,
        3 => control == 1f ? 80f : control,
        _ => 0f,
    };

    /// <summary>
    /// Guerrilla's campaign markers (<see cref="CampaignClasses"/>), every one in its zones and p_ zones, written for
    /// the guerrilla_campaign mod (its world_objects.cpp) as
    /// <c>&lt;objects&gt;&lt;object&gt;&lt;class/&gt;&lt;handle/&gt;&lt;parent/&gt;&lt;zone/&gt;&lt;pos/&gt;&lt;property&gt;&lt;name/&gt;&lt;value/&gt;&lt;/property&gt;...&lt;/object&gt;...&lt;/objects&gt;</c>:
    /// all child elements (the mod's reader keeps no attributes). The handle and parent are Guerrilla's (hex);
    /// buildings keep Guerrilla's handles in our world (but where the world converter had to give one another),
    /// so a demolitions master node's parent names its target, and parent_pos says where that is, to find it by its
    /// place otherwise; parent_center is the parent's bounding-box centre, and a demolitions master node's area_center is its
    /// layer's "Demo Master" dummy (the play area's middle, where there is one). The zone is ours (rfg_CC_RR); the place is the object's op position. Each property is named and written by
    /// Guerrilla's own zone schema (rfg_zonex_format.txt): text, enums and flags as text, a bool as true or false,
    /// whole and fractional numbers, handles in hex, vectors, matrices, boxes and lists as numbers split by spaces.
    /// A marker that is in a zone and again in that zone's p_ file (the same handle) is written once.
    /// </summary>
    static string CampaignObjects(string guerrillaCache, Packfile guerrillaMisc, string campaignFolder)
    {
        if (guerrillaMisc.Find(ZoneSchema.FileName) is not { } schemaEntry) return $"Campaign markers: Guerrilla's misc.vpp_pc has no {ZoneSchema.FileName}; none written";
        var schema = ZoneSchema.FromBytes(guerrillaMisc.Read(schemaEntry));
        var zones = Packfile.Open(Path.Combine(guerrillaCache, "zonescript_terr01.vpp_pc"));
        var wanted = CampaignClasses.ToDictionary(NameHashes.ClassHash, c => c);
        var written = new HashSet<uint>();
        var counts = new SortedDictionary<string, int>();
        int unnamed = 0;
        var xml = new System.Text.StringBuilder("<objects>\n");
        static string F(float v) => v.ToString("0.####", CultureInfo.InvariantCulture);
        static string Escape(string text) => text.Replace("&", "&amp;").Replace("<", "&lt;").Replace(">", "&gt;");
        static string Floats(byte[] v) => string.Join(' ', Enumerable.Range(0, v.Length / 4).Select(i => F(BitConverter.ToSingle(v, i * 4))));
        static string Words(byte[] v) => string.Join(' ', Enumerable.Range(0, v.Length / 4).Select(i => BitConverter.ToUInt32(v, i * 4).ToString("X8")));
        static string Value(SchemaType type, byte[] v) => type switch
        {
            SchemaType.Bool => v.Length > 0 && v[0] != 0 ? "true" : "false",
            SchemaType.Int when v.Length >= 4 => BitConverter.ToInt32(v, 0).ToString(CultureInfo.InvariantCulture),
            SchemaType.Float when v.Length >= 4 => F(BitConverter.ToSingle(v, 0)),
            SchemaType.Handle when v.Length >= 4 => BitConverter.ToUInt32(v, 0).ToString("X8"),
            SchemaType.Vector or SchemaType.Matrix or SchemaType.Box or SchemaType.Color => Floats(v),
            SchemaType.List => Words(v),
            _ => System.Text.Encoding.Latin1.GetString(v).TrimEnd('\0').Trim(),
        };
        // The zones before their p_ files, so a marker in both is named by its zone.
        var zoneEntries = zones.Entries.Where(e => e.Name.EndsWith(".rfgzone_pc", StringComparison.OrdinalIgnoreCase))
                                       .OrderBy(e => e.Name.StartsWith("p_", StringComparison.OrdinalIgnoreCase)).ThenBy(e => e.Name, StringComparer.OrdinalIgnoreCase).ToList();
        static System.Numerics.Vector3 PlaceOf(GuerrillaZoneObject o) =>
            o.Value("op") is { Length: >= 12 } op ? new(BitConverter.ToSingle(op, 0), BitConverter.ToSingle(op, 4), BitConverter.ToSingle(op, 8)) : (o.BoundsMin + o.BoundsMax) / 2;
        // Where each object is, for the markers' parents: the world converter gives a building another handle when
        // ours already has Guerrilla's (je_dm_05's smokestack, 96030008), so the mod also finds a parent by its place.
        var placeOf = new Dictionary<uint, System.Numerics.Vector3>();
        // Each object's bounding-box centre: the middle of a demolitions master's play area, unless its layer moves it.
        var centreOf = new Dictionary<uint, System.Numerics.Vector3>();
        var sizeOf = new Dictionary<uint, System.Numerics.Vector3>();  // and its size: the target's mark on the minimap
        var objectsOf = new Dictionary<string, List<GuerrillaZoneObject>>();
        foreach (var entry in zoneEntries)
        {
            var objects = GuerrillaZones.Objects(zones.Read(entry));
            objectsOf[entry.Name] = objects;
            foreach (var o in objects)
            {
                placeOf.TryAdd(o.Handle, PlaceOf(o));
                centreOf.TryAdd(o.Handle, (o.BoundsMin + o.BoundsMax) / 2);
                sizeOf.TryAdd(o.Handle, o.BoundsMax - o.BoundsMin);
            }
        }
        // A demolitions master layer's "Demo Master" dummy (activities.vpp_pc, je_dm_XX.str2_pc): the play area's
        // middle instead of the building's (activity_demolitions_master::activity_do_frame_active, LOADING_LAYERS).
        var demoMasterCentre = new Dictionary<string, System.Numerics.Vector3>(StringComparer.OrdinalIgnoreCase);
        uint dummyClass = NameHashes.ClassHash("object_dummy");
        if (File.Exists(Path.Combine(guerrillaCache, "activities.vpp_pc")))
        {
            var activities = Packfile.Open(Path.Combine(guerrillaCache, "activities.vpp_pc"));
            foreach (var entry in activities.Entries.Where(e => e.Name.StartsWith("je_dm_", StringComparison.OrdinalIgnoreCase)))
            {
                var inner = activities.OpenContainer(entry);
                foreach (var layer in inner.Entries.Where(e => e.Name.EndsWith(".layer_pc", StringComparison.OrdinalIgnoreCase)))
                    foreach (var o in GuerrillaZones.LayerObjects(inner.Read(layer)))
                        if (o.ClassHash == dummyClass && string.Equals(o.Text("dummy_type")?.Trim(), "Demo Master", StringComparison.OrdinalIgnoreCase))
                            demoMasterCentre.TryAdd(Path.GetFileNameWithoutExtension(entry.Name), PlaceOf(o));
            }
        }
        // Each object's children by its parent's handle, for a Transporter start node's spawn node and player start.
        uint vehicleSpawnClass = NameHashes.ClassHash("object_vehicle_spawn_node"), playerStartClass = NameHashes.ClassHash("player_start");
        var childrenOf = new Dictionary<uint, List<GuerrillaZoneObject>>();
        foreach (var objects in objectsOf.Values)
            foreach (var o in objects.Where(o => o.Parent != 0xFFFFFFFF && (o.ClassHash == vehicleSpawnClass || o.ClassHash == playerStartClass)))
            {
                if (!childrenOf.TryGetValue(o.Parent, out var list)) childrenOf[o.Parent] = list = [];
                if (!list.Any(c => c.Handle == o.Handle)) list.Add(o);
            }
        foreach (var entry in zoneEntries)
        {
            string guerrillaZone = Path.GetFileNameWithoutExtension(entry.Name);
            string zone = "rfg_" + guerrillaZone[(guerrillaZone.LastIndexOf("terr01_", StringComparison.OrdinalIgnoreCase) + "terr01_".Length)..];
            foreach (var o in objectsOf[entry.Name])
            {
                if (!wanted.TryGetValue(o.ClassHash, out string? className) || !written.Add(o.Handle)) continue;
                counts[className] = counts.GetValueOrDefault(className) + 1;
                var at = PlaceOf(o);
                xml.Append($"<object><class>{className}</class><handle>{o.Handle:X8}</handle><parent>{o.Parent:X8}</parent><zone>{zone}</zone>")
                   .Append($"<pos>{F(at.X)} {F(at.Y)} {F(at.Z)}</pos>");
                if (o.Parent != 0xFFFFFFFF && placeOf.TryGetValue(o.Parent, out var parentAt)) xml.Append($"<parent_pos>{F(parentAt.X)} {F(parentAt.Y)} {F(parentAt.Z)}</parent_pos>");
                if (o.Parent != 0xFFFFFFFF && centreOf.TryGetValue(o.Parent, out var parentCentre)) xml.Append($"<parent_center>{F(parentCentre.X)} {F(parentCentre.Y)} {F(parentCentre.Z)}</parent_center>");
                if (o.Parent != 0xFFFFFFFF && sizeOf.TryGetValue(o.Parent, out var parentSize)) xml.Append($"<parent_size>{F(parentSize.X)} {F(parentSize.Y)} {F(parentSize.Z)}</parent_size>");
                if (className == "object_demolitions_master_node" && o.Text("demolitions_master_type")?.Trim() is { Length: > 0 } level &&
                    demoMasterCentre.TryGetValue(level, out var areaCentre))
                    xml.Append($"<area_center>{F(areaCentre.X)} {F(areaCentre.Y)} {F(areaCentre.Z)}</area_center>");
                xml.Append('\n');
                foreach (var (hash, (_, value)) in o.Properties)
                {
                    // op: the place and turn (pos and orient, written together), 12 numbers.
                    bool placeAndTurn = hash == NameHashes.PropertyHash("op");
                    var property = placeAndTurn ? null : schema.Property(o.ClassHash, hash);
                    string name = placeAndTurn ? "op" : property?.Name ?? "";
                    if (name.Length == 0) { unnamed++; continue; }
                    string text = placeAndTurn ? Floats(value) : Value(property!.Type, value);
                    xml.Append($"  <property><name>{Escape(name)}</name><value>{Escape(text)}</value></property>\n");
                }
                // A Transporter start node's two children (Guerrilla's zones; neither is a campaign marker): its car's
                // vehicle spawn node (child_vehicle_spawn_node: its handle, place and spawn_set; the world build places it,
                // so the game parks the car there) and the player_start a retry puts the player at (child_player_start:
                // its op, place and turn). An activity's start object (object_activity_spawn: an area defense's) has a
                // player_start too, where a retry starts the player.
                if ((className == "object_delivery_node" || className == "object_activity_spawn") && childrenOf.TryGetValue(o.Handle, out var children))
                    foreach (var child in children)
                    {
                        if (child.ClassHash == vehicleSpawnClass)
                        {
                            var place = PlaceOf(child);
                            xml.Append($"  <property><name>child_vehicle_spawn_node</name><value>{child.Handle:X8} {F(place.X)} {F(place.Y)} {F(place.Z)} " +
                                       $"{Escape(child.Text("spawn_set")?.Trim() ?? "")}</value></property>\n");
                        }
                        else if (child.ClassHash == playerStartClass && child.Value("op") is { Length: >= 48 } childOp)
                            xml.Append($"  <property><name>child_player_start</name><value>{Floats(childOp)}</value></property>\n");
                    }
                xml.Append("</object>\n");
            }
        }
        // Guerrilla's spawn regions (object_spawn_region, OT 23): where a continuous attack's squads appear
        // (activity_continuous_attack_data::find_local_spawn_region RM 0x77FED0 takes any in the world, 50-350 m from the
        // player; its only check is the distance). Those of the world's zones, and those of each activity's layers
        // (activities.vpp_pc: an area defense's, a Transporter's), which are in the world only while that activity's
        // layers are: each with its layer (the activity's name, "-" for the world's). Only their place matters.
        uint spawnRegionClass = NameHashes.ClassHash("object_spawn_region");
        int worldRegions = 0, layerRegions = 0;
        void WriteRegion(GuerrillaZoneObject o, string zone, string layer)
        {
            if (!written.Add(o.Handle)) return;
            var at = PlaceOf(o);
            xml.Append($"<object><class>object_spawn_region</class><handle>{o.Handle:X8}</handle><parent>{o.Parent:X8}</parent><zone>{zone}</zone>")
               .Append($"<pos>{F(at.X)} {F(at.Y)} {F(at.Z)}</pos>\n")
               .Append($"  <property><name>layer</name><value>{Escape(layer)}</value></property>\n</object>\n");
        }
        foreach (var entry in zoneEntries)
        {
            string guerrillaZone = Path.GetFileNameWithoutExtension(entry.Name);
            string zone = "rfg_" + guerrillaZone[(guerrillaZone.LastIndexOf("terr01_", StringComparison.OrdinalIgnoreCase) + "terr01_".Length)..];
            foreach (var o in objectsOf[entry.Name].Where(o => o.ClassHash == spawnRegionClass))
            {
                WriteRegion(o, zone, "-");
                worldRegions++;
            }
        }
        // An activity layer's vehicle and turret spawn nodes (an area defense's given walker, tank or turret): the layer's
        // own (the activity layers' build makes them, keeping Guerrilla's handles), listed here with their layer so the
        // activity knows its given vehicle: its handle, place and spawn_set (turret_type for a turret).
        uint turretSpawnClass = NameHashes.ClassHash("object_turret_spawn_node");
        int layerSpawnNodes = 0;
        void WriteLayerSpawnNode(GuerrillaZoneObject o, string layer, bool turret)
        {
            if (!written.Add(o.Handle)) return;
            var at = PlaceOf(o);
            string kind = turret ? "object_turret_spawn_node" : "object_vehicle_spawn_node";
            xml.Append($"<object><class>{kind}</class><handle>{o.Handle:X8}</handle><parent>{o.Parent:X8}</parent><zone>-</zone>")
               .Append($"<pos>{F(at.X)} {F(at.Y)} {F(at.Z)}</pos>\n")
               .Append($"  <property><name>layer</name><value>{Escape(layer)}</value></property>\n")
               .Append($"  <property><name>{(turret ? "turret_type" : "spawn_set")}</name><value>{Escape(o.Text(turret ? "turret_type" : "spawn_set")?.Trim() ?? "")}</value></property>\n")
               .Append("</object>\n");
            layerSpawnNodes++;
        }
        if (File.Exists(Path.Combine(guerrillaCache, "activities.vpp_pc")))
        {
            var activities = Packfile.Open(Path.Combine(guerrillaCache, "activities.vpp_pc"));
            foreach (var entry in activities.Entries.Where(e => e.Name.EndsWith(".str2_pc", StringComparison.OrdinalIgnoreCase)))
            {
                string activity = Path.GetFileNameWithoutExtension(entry.Name);
                var inner = activities.OpenContainer(entry);
                foreach (var layer in inner.Entries.Where(e => e.Name.EndsWith(".layer_pc", StringComparison.OrdinalIgnoreCase)))
                    foreach (var o in GuerrillaZones.LayerObjects(inner.Read(layer)))
                    {
                        if (o.ClassHash == spawnRegionClass)
                        {
                            WriteRegion(o, "-", activity);
                            layerRegions++;
                        }
                        else if (GuerrillaActivityLayers.IsAreaDefense(activity) && (o.ClassHash == vehicleSpawnClass || o.ClassHash == turretSpawnClass))
                            WriteLayerSpawnNode(o, activity, o.ClassHash == turretSpawnClass);
                    }
            }
        }
        xml.Append("</objects>\n");
        string file = Path.Combine(campaignFolder, CampaignObjectsFile);
        Directory.CreateDirectory(Path.GetDirectoryName(file)!);
        File.WriteAllText(file, xml.ToString(), new System.Text.UTF8Encoding(false));
        return $"Campaign markers: {written.Count} in {CampaignObjectsFile} ({string.Join(", ", counts.Select(c => $"{c.Value} {c.Key}"))}; " +
               $"{demoMasterCentre.Count} demolitions master areas centred by their layer; spawn regions: {worldRegions} in the world's zones, " +
               $"{layerRegions} in activities' layers; {layerSpawnNodes} area defenses' given vehicle and turret spawn nodes)" +
               (unnamed > 0 ? $"; {unnamed} properties the schema doesn't name left out" : "");
    }

    /// <summary>
    /// Guerrilla's kill regions: the trigger_regions whose region_type is "kill human" (25 in its zones: 21 at
    /// the bottoms of pits and chasms, region_kill_type cliff, and 4 minefields, mine). Armageddon's own
    /// trigger_region never reads region_type, so the mod does what Guerrilla's trigger_region_do_frame does
    /// with them. A line each: region, its handle, its zone, cliff or mine, box or sphere, enabled (0/1),
    /// not_in_mission (0/1), not_in_activity (0/1), then its place and turn (op: position, right, up,
    /// forward; 12 numbers), its box around that place (bb: least, then most corner; 6 numbers) and its
    /// radius (outer_radius, for a sphere). A region that is in a zone and again in that zone's p_ file (the
    /// same handle) is written once.
    /// </summary>
    static string KillRegions(string guerrillaCache, string campaignFolder)
    {
        var zones = Packfile.Open(Path.Combine(guerrillaCache, "zonescript_terr01.vpp_pc"));
        uint regionClass = NameHashes.ClassHash("trigger_region");
        var lines = new List<string> { "# Guerrilla's kill regions, for the guerrilla_campaign mod (written by editor\\GuerrillaBuilder.exe with the people)" };
        var written = new HashSet<uint>();
        int cliffs = 0, mines = 0;
        var problems = new List<string>();
        static string F(float v) => v.ToString("0.####", CultureInfo.InvariantCulture);
        static float[]? Floats(GuerrillaZoneObject o, string name)
        {
            if (o.Value(name) is not { Length: > 0 } v || v.Length % 4 != 0) return null;
            var f = new float[v.Length / 4];
            for (int i = 0; i < f.Length; i++) f[i] = BitConverter.ToSingle(v, i * 4);
            return f;
        }
        // The zones before their p_ files, so a region in both is named by its zone.
        foreach (var entry in zones.Entries.Where(e => e.Name.EndsWith(".rfgzone_pc", StringComparison.OrdinalIgnoreCase))
                                           .OrderBy(e => e.Name.StartsWith("p_", StringComparison.OrdinalIgnoreCase)).ThenBy(e => e.Name, StringComparer.OrdinalIgnoreCase))
        {
            string zone = Path.GetFileNameWithoutExtension(entry.Name);
            foreach (var region in GuerrillaZones.Objects(zones.Read(entry)).Where(o => o.ClassHash == regionClass))
            {
                if (!string.Equals(region.Text("region_type")?.Trim(), "kill human", StringComparison.OrdinalIgnoreCase)) continue;
                if (!written.Add(region.Handle)) continue;
                bool mine = string.Equals(region.Text("region_kill_type")?.Trim(), "mine", StringComparison.OrdinalIgnoreCase);
                bool sphere = string.Equals(region.Text("trigger_shape")?.Trim(), "sphere", StringComparison.OrdinalIgnoreCase);
                bool enabled = region.Value("enabled") is not { Length: > 0 } on || on[0] != 0;  // the schema's default: true
                string flags = region.Text("trigger_flags") ?? "";
                bool notInMission = flags.Contains("not_in_mission", StringComparison.OrdinalIgnoreCase);
                bool notInActivity = flags.Contains("not_in_activity", StringComparison.OrdinalIgnoreCase);
                float[]? op = Floats(region, "op"), bb = Floats(region, "bb"), radius = Floats(region, "outer_radius");
                if (op is not { Length: >= 12 } || bb is not { Length: >= 6 })
                {
                    problems.Add($"{region.Handle:X8} in {zone} has no place or box");
                    continue;
                }
                lines.Add($"region {region.Handle:X8} {zone} {(mine ? "mine" : "cliff")} {(sphere ? "sphere" : "box")} {(enabled ? 1 : 0)} {(notInMission ? 1 : 0)} {(notInActivity ? 1 : 0)} " +
                          $"{string.Join(' ', op.Take(12).Select(F))} {string.Join(' ', bb.Take(6).Select(F))} {F(radius is { Length: >= 1 } r ? r[0] : 0f)}");
                if (mine) mines++;
                else cliffs++;
            }
        }
        Directory.CreateDirectory(campaignFolder);
        File.WriteAllLines(Path.Combine(campaignFolder, KillRegionsFile), lines);
        return $"Kill regions: {cliffs + mines} in {KillRegionsFile} ({cliffs} pits, {mines} minefields)" +
               (problems.Count > 0 ? $"; left out: {string.Join("; ", problems)}" : "");
    }

    /// <summary>
    /// Guerrilla's restricted areas (object_restricted_area, 40 in its zones: its EDF bases), which Armageddon
    /// has no class for, so the world converter leaves them out. Each is a "yellow" polygon (inside it the
    /// EDF who see the player take him for an enemy, and the alert level doesn't fall) and a wider "warning"
    /// polygon (may be empty), in world coordinates (x, z), as object_restricted_area::serialize reads them
    /// (yellow_polygon, warning_polygon: vector2 each, at most 64), with its disabled and allow_ambient_peds
    /// switches. A line each: area, its handle, its zone, disabled (0/1), allow_ambient_peds (0/1), the yellow
    /// polygon's number of points and its points (x z ...), the warning polygon's the same way.
    /// </summary>
    static string RestrictedAreas(string guerrillaCache, string campaignFolder)
    {
        var zones = Packfile.Open(Path.Combine(guerrillaCache, "zonescript_terr01.vpp_pc"));
        uint areaClass = NameHashes.ClassHash("object_restricted_area");
        var lines = new List<string> { "# Guerrilla's restricted areas, for the guerrilla_campaign mod (written by editor\\GuerrillaBuilder.exe with the people)" };
        int areas = 0, empty = 0;
        static string F(float v) => v.ToString("0.###", CultureInfo.InvariantCulture);
        static string Polygon(byte[]? value)
        {
            int count = Math.Min((value?.Length ?? 0) / 8, MostPolygonPoints);
            var parts = new List<string> { count.ToString(CultureInfo.InvariantCulture) };
            for (int i = 0; i < count; i++) parts.Add($"{F(BitConverter.ToSingle(value!, i * 8))} {F(BitConverter.ToSingle(value!, i * 8 + 4))}");
            return string.Join(" ", parts);
        }
        static string Switch(byte[]? value) => value is { Length: > 0 } && value.Any(b => b != 0) ? "1" : "0";
        foreach (var entry in zones.Entries.Where(e => e.Name.EndsWith(".rfgzone_pc", StringComparison.OrdinalIgnoreCase)).OrderBy(e => e.Name, StringComparer.OrdinalIgnoreCase))
            foreach (var area in GuerrillaZones.Objects(zones.Read(entry)).Where(o => o.ClassHash == areaClass))
            {
                var yellow = area.Value("yellow_polygon");
                if ((yellow?.Length ?? 0) < 24) { empty++; continue; }  // fewer than three points: no area
                lines.Add($"area {area.Handle:X8} {Path.GetFileNameWithoutExtension(entry.Name)} {Switch(area.Value("disabled"))} {Switch(area.Value("allow_ambient_peds"))} " +
                          $"{Polygon(yellow)} {Polygon(area.Value("warning_polygon"))}");
                areas++;
            }
        Directory.CreateDirectory(campaignFolder);
        File.WriteAllLines(Path.Combine(campaignFolder, RestrictedAreasFile), lines);
        return $"Restricted areas: {areas} in {RestrictedAreasFile}" + (empty > 0 ? $" ({empty} without a yellow polygon left out)" : "");
    }

    public static IEnumerable<string> Remove(string campaignFolder)
    {
        foreach (string written in new[] { VehicleTeams, RestrictedAreasFile, KillRegionsFile, CampaignObjectsFile, EdfBuildingsFile, BuildingValuesFile })
        {
            string file = Path.Combine(campaignFolder, written);
            if (!File.Exists(file)) continue;
            File.Delete(file);
            yield return $"Deleted {file}";
        }
        string worldFolder = Path.Combine(campaignFolder, Path.GetDirectoryName(CampaignObjectsFile)!);
        if (Directory.Exists(worldFolder) && !Directory.EnumerateFileSystemEntries(worldFolder).Any()) Directory.Delete(worldFolder);
        string folder = Path.Combine(campaignFolder, TablesFolder);
        if (!Directory.Exists(folder)) yield break;
        foreach (string table in Tables)
        {
            string file = Path.Combine(folder, table);
            if (!File.Exists(file)) continue;
            File.Delete(file);
            yield return $"Deleted {file}";
        }
        if (!Directory.EnumerateFileSystemEntries(folder).Any()) Directory.Delete(folder);
    }
}
