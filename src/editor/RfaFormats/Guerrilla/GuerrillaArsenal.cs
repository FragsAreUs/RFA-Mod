using RfaFormats.Tables;
using RfaFormats.Textures;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's vehicles made usable in Armageddon, from the player's own copy of Guerrilla
/// (only read): GuerrillaBuilder.exe's vehicles, for the guerrilla_world mod. For each vehicle asked for:
///   - its model converted (<see cref="GuerrillaVehicles"/>) and packed with its textures (each one of
///     Guerrilla's, from the vehicle's own texture file, as a single-texture file under its alias, its
///     compressed mip levels down to 4 × 4) into a container of its own, named as Guerrilla's
///     (&lt;mesh&gt;.ccar_pc.str2_pc) in overrides\build\pc\cache\guerrilla\vehicles\, where the game finds a new file by name;
///   - an entry for that container added to Armageddon's vehicle asset list (an override of
///     vehicles.vpp_pc\vehicle_containers.asm_pc, the player's own with ours added), as Armageddon lists
///     its own vehicles (type 27, flags 0x80; the model a primitive of type 11, each texture of type 1);
///   - its vehicles.xtbl entry, Guerrilla's own, added to Armageddon's table, which the guerrilla_world
///     mod hands the game while it starts (mods\guerrilla_world\vehicles.xtbl). The two tables describe a
///     vehicle with nearly the same fields; a field Armageddon's table hasn't is left out (Guerrilla's
///     vehicle family, its sounds' wave bank, ...) unless Armageddon's reader still reads it (the
///     handling fields in <see cref="ReadAnyway"/>), and so is a value Armageddon has no choice for; a
///     field Armageddon requires that Guerrilla's hasn't (its electric hit points, memory slot, the
///     cameras' vertical limits, ...) gets the default Armageddon's description gives it. Its effects
///     (engine fire and smoke, the wreck's fire, exhaust, sparks, its explosion) are Guerrilla's own, as
///     the world builder adds them ("rfg_" names: see <see cref="VehicleEffects"/>). For now also left
///     out: the sounds (Guerrilla's are XACT cues, Armageddon plays Wwise events), the lights (Armageddon
///     lights a vehicle through its own Lights map), and the turrets;
///   - its vehicle_interactions.xtbl entry (where the player and passengers get in, sit, move between
///     seats and get out, and how: opening the door, climbing in, quick and diving exits, crawling out of
///     an upturned car), Guerrilla's own, added to Armageddon's table
///     (mods\guerrilla_world\vehicle_interactions.xtbl, handed over the same way). Both games name the
///     same steps, actions and animation states ("vehicle enter primary 1" ends in "driving forward",
///     which is what puts the player behind the wheel); Guerrilla's Door is Armageddon's Door_1. The
///     doors' sound cues are left out until Guerrilla's audio is ported;
///   - the animation set the interaction plays (Vehicle_Taxi, ...), Guerrilla's own, with its
///     animations as Guerrilla has them (re-encoded for Armageddon; Mason is the player in Guerrilla's
///     world, and its people use the same skeletons) and packed in the vehicle's container
///     (<see cref="GuerrillaAnimationSets"/>). Without the animations a transition that names a door
///     stops the player at it ("Vehicle interaction anim missing open 'DOOR_L' anim or trigger"): the
///     door opens on the "door start" and "door end" triggers of the person's animation.
/// Guerrilla's teams are Armageddon's where it has them (Guerilla the Red Faction).
/// </summary>
public static class GuerrillaArsenal
{
    /// <summary>The vehicles built when none are named: every one that converts.</summary>
    public static readonly string[] DefaultVehicles = ["all"];

    /// <summary>Guerrilla's teams as Armageddon's (human_team_names.xtbl).</summary>
    static readonly Dictionary<string, string> Teams = new(StringComparer.OrdinalIgnoreCase)
    {
        ["Guerilla"] = "Red Faction", ["Guerrilla"] = "Red Faction", ["Civilian"] = "Civilian", ["Marauder"] = "Marauder",
    };

    /// <summary>
    /// The fields the two games measure differently (each table's description says how), and what a
    /// value of Guerrilla's is multiplied by for Armageddon: the vehicle's price, Guerrilla's in millions,
    /// Armageddon's in whole units (most of its vehicles 20,000; Guerrilla's cars 0.02); a flyer's turn
    /// and tilt rates, Guerrilla's in revolutions a second (and a second squared), Armageddon's in
    /// degrees (its default turn 90, Guerrilla's 0.25). Armageddon's are whole numbers: a decimal stops
    /// its table reader ("Could not parse '.25' into an integer").
    /// </summary>
    static readonly Dictionary<string, double> UnitScales = new(StringComparer.OrdinalIgnoreCase)
    {
        ["Value"] = 1_000_000,
        ["FlyerParams/FlyerMaxTurnAngvel"] = 360, ["FlyerParams/FlyerMaxTurnAngaccl"] = 360,
        ["FlyerParams/FlyerMaxTiltAngvel"] = 360, ["FlyerParams/FlyerMaxTiltAngaccl"] = 360,
    };

    /// <summary>The fields of Guerrilla's entry left out for now (see the summary), whatever Armageddon's table has.</summary>
    static readonly string[] NotYet = ["Foley"];  // Guerrilla's Foley: replaced by the sounds' own (GuerrillaVehicleSounds)

    /// <summary>
    /// The fields Armageddon's table description leaves out that its vehicle reader still reads, as
    /// Guerrilla's does (0x628340 and its parts are Guerrilla's vehicle_parse_vehicle with fields added:
    /// each of these names is pushed there, and read into the vehicle's settings): kept although the
    /// description lacks them. Without them the game takes vehicle_defaults.xtbl's: Guerrilla's cars
    /// turn with the default yaw torque (1, the taxi's 1.4), the APCs wait half a second before reversing,
    /// the mining rovers push with 4.75 times their force assist, AI drivers lose their own torque, spin
    /// damping and top speed. Armageddon's own entries carry undescribed fields too, so its table reader
    /// takes them. Paths as <see cref="Prune"/> builds them (a list's items under the list's name).
    /// </summary>
    static readonly HashSet<string> ReadAnyway = new(StringComparer.OrdinalIgnoreCase)
    {
        "Yaw_Torque_Scalar", "Collision_Mass_Scalar", "Vehicle_Damage_Scale", "Alert_Level_Multiplier", "Autobrake_Speed",
        "AIEnforcedMaxSpeed", "Engine/AITorque", "Engine/Force_assist", "Axles/Wheel_Spring_Pow",
        "Transmission/Forward_To_Reverse_Delay", "Transmission/Reverse_To_Forward_Delay", "Angular_Damping/Normal_Spin_Damping_AI",
        "Effects/Bomb_Explosion",
        // A choice Armageddon's description lost but its reader still takes (as path=value): Guerrilla's
        // artillery tanks' type (the exe's name table at 0x10FCB78 still parses it, type 4: a tank).
        "Special_Vehicle_Type=Artillery Tank",
        // A mount nobody mans (Guerrilla's no-turret APCs' roof plate, TurretPlug): its seat "none", which
        // Armageddon's seat look-up (0x603FC0) reads as no seat (-1) as Guerrilla's does; without it the
        // field the reader requires is missing and it stops.
        "Turrets/Seat=none",
    };

    /// <summary>
    /// The fields of a vehicle's Effects that name an explosion (explosions.xtbl); the others Armageddon's
    /// reader reads (<see cref="EffectFields"/>) name an effect (effects.xtbl).
    /// </summary>
    static readonly HashSet<string> ExplosionFields = new(StringComparer.OrdinalIgnoreCase) { "Explosion", "Bomb_Explosion", "Electrical_Explosion" };

    /// <summary>
    /// The Effects fields Armageddon's vehicle reader reads (0x62D2C0): those its description has
    /// (engine fire and smoke, the wreck's smoke, a broken-off part's fire, the explosion, exhaust, sparks
    /// on a hit and a scrape, a part coming off, Armageddon's electric damage and hit markers), and
    /// Bomb_Explosion (<see cref="ReadAnyway"/>). Guerrilla's light fields (Head_Light_Primary ...) aren't
    /// among them: Armageddon lights its vehicles through its Lights map.
    /// </summary>
    static IReadOnlyList<string> EffectFields(FieldDescription vehicleDescription) =>
    [
        .. (vehicleDescription.Children.FirstOrDefault(f => f.Name.Equals("Effects", StringComparison.OrdinalIgnoreCase))?.Children ?? [])
               .Where(f => f.Type == FieldType.Reference).Select(f => f.Name),
        .. ReadAnyway.Where(p => p.StartsWith("Effects/", StringComparison.OrdinalIgnoreCase)).Select(p => p["Effects/".Length..]),
    ];

    /// <summary>
    /// A Guerrilla vehicle entry's effects as Guerrilla plays them: each field of <paramref name="fields"/>
    /// its Effects names, else what Guerrilla's vehicle_defaults.xtbl names (its reader takes the
    /// defaults' for a field an entry leaves out, 0xB6BE50; Armageddon's defaults name the same effects,
    /// but Armageddon's effects of those names are its own: its own fire, sparks, a purple car explosion,
    /// and exhaust it loads only with its own levels).
    /// </summary>
    static Dictionary<string, string> EffectsOf(XtblElement entry, XtblElement? defaults, IReadOnlyList<string> fields)
    {
        var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (string field in fields)
        {
            string? value = entry.Element("Effects")?.ValueOf(field)?.Trim();
            if (string.IsNullOrEmpty(value)) value = defaults?.Element("Effects")?.ValueOf(field)?.Trim();
            if (!string.IsNullOrEmpty(value)) result[field] = value;  // "null": none (an effect of no such name)
        }
        return result;
    }

    /// <summary>
    /// The effects and explosions (Guerrilla's names) Guerrilla's vehicles play, every one of its entries
    /// (see <see cref="EffectsOf"/>): the world builder adds them to the effect tables as effects played
    /// everywhere in the world (<see cref="GuerrillaEffectTables"/>), and this tool names the added ones
    /// ("rfg_" + Guerrilla's name) in the vehicles' entries.
    /// </summary>
    public static (IReadOnlyList<string> Effects, IReadOnlyList<string> Explosions) VehicleEffects(Packfile guerrillaTables, Packfile gameTables)
    {
        XtblFile ReadTable(Packfile pack, string name) => XtblFile.FromBytes(pack.Read(pack.Find(name) ?? throw new InvalidDataException($"no {name} in {pack.Name}")));
        var fields = EffectFields(ReadTable(gameTables, "vehicles.xtbl").Description ?? throw new InvalidDataException("Armageddon's vehicles.xtbl has no description"));
        var defaults = ReadTable(guerrillaTables, "vehicle_defaults.xtbl").Entries.FirstOrDefault();
        var effects = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        var explosions = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in ReadTable(guerrillaTables, "vehicles.xtbl").Entries)
            foreach (var (field, value) in EffectsOf(entry, defaults, fields))
                if (!value.Equals("null", StringComparison.OrdinalIgnoreCase)) (ExplosionFields.Contains(field) ? explosions : effects).Add(value);
        foreach (string light in GuerrillaVehicleLights.Effects(guerrillaTables)) effects.Add(light);  // the lights' (see GuerrillaVehicleLights)
        foreach (string step in GuerrillaVehicleFootsteps.Effects(guerrillaTables)) effects.Add(step);  // the walkers' footsteps'
        return ([.. effects], [.. explosions]);
    }

    /// <summary>The files this writes (relative to the overrides folder, then to the mod's folder), for --remove.</summary>
    public const string AssetList = @"build\pc\cache\vehicles.vpp_pc\vehicle_containers.asm_pc", Table = "vehicles.xtbl",
                        InteractionTable = "vehicle_interactions.xtbl", SpawnGroupTable = "spawn_group_vehicle.xtbl";

    /// <summary>
    /// The vehicles' own skeletons Armageddon hasn't, one file name a line, in the guerrilla_vehicles mod's
    /// folder (mods\guerrilla_vehicles\rigs.txt, beside mods\guerrilla_world\). The game loads every
    /// skeleton its archives list while it starts, and a vehicle whose skeleton isn't loaded stops it
    /// (its table entry names it; a null where the skeleton should be): a new file isn't in any
    /// archive's list, so the mod has the game load these by name after its own.
    /// </summary>
    public const string RigList = "rigs.txt";
    static string RigListFile(string modFolder) => Path.Combine(Path.GetDirectoryName(Path.GetFullPath(modFolder))!, "guerrilla_vehicles", RigList);

    /// <summary>
    /// Builds the vehicles named (Guerrilla's vehicles.xtbl entry names; "all" for every one that converts)
    /// into <paramref name="overrides"/> and <paramref name="modFolder"/> (mods\guerrilla_world\).
    /// </summary>
    public static IEnumerable<string> Build(string guerrilla, string game, string overrides, string modFolder, IReadOnlyList<string> vehicles)
    {
        string guerrillaCache = Path.Combine(guerrilla, "build", "pc", "cache"), gameCache = Path.Combine(game, "build", "pc", "cache");
        var guerrillaTables = Packfile.Open(Path.Combine(guerrillaCache, "misc.vpp_pc"));
        // Guerrilla's default steering torque, vehicle_defaults.xtbl's <Defaults> (see where each car's entry is copied).
        string? guerrillaSteeringTorque = guerrillaTables.Find("vehicle_defaults.xtbl") is { } defaultsFile &&
                                          System.Text.RegularExpressions.Regex.Match(System.Text.Encoding.UTF8.GetString(guerrillaTables.Read(defaultsFile)),
                                              @"<Extra_Steering_Torque>\s*([^<\s]+)\s*</Extra_Steering_Torque>") is { Success: true } torque
                                              ? torque.Groups[1].Value : null;
        var gameTables = Packfile.Open(Path.Combine(gameCache, "misc.vpp_pc"));
        XtblFile ReadTable(Packfile pack, string name) => XtblFile.FromBytes(pack.Read(pack.Find(name) ?? throw new InvalidDataException($"no {name} in {pack.Name}")));
        var rfgVehicles = ReadTable(guerrillaTables, "vehicles.xtbl");
        var ourVehicles = ReadTable(gameTables, "vehicles.xtbl");
        var description = ourVehicles.Description ?? throw new InvalidDataException("Armageddon's vehicles.xtbl has no description");
        var groups = new HashSet<string>(ReadTable(gameTables, "vehicle_groups.xtbl").ReferenceTargets("Vehicle_Group.Name"), StringComparer.OrdinalIgnoreCase);
        var effectFields = EffectFields(description);
        var rfgDefaults = ReadTable(guerrillaTables, "vehicle_defaults.xtbl").Entries.FirstOrDefault();
        // The effects and explosions the world builder added for the vehicles (GuerrillaWorld writes
        // them into the same mod folder; see VehicleEffects).
        HashSet<string> NamesIn(string table) => File.Exists(Path.Combine(modFolder, table))
            ? new(XtblFile.FromBytes(File.ReadAllBytes(Path.Combine(modFolder, table))).Entries.Select(XtblFile.EntryName), StringComparer.OrdinalIgnoreCase)
            : new(StringComparer.OrdinalIgnoreCase);
        var worldEffects = NamesIn("effects.xtbl");
        var worldExplosions = NamesIn("explosions.xtbl");
        var notInWorld = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        var rfgInteractions = ReadTable(guerrillaTables, InteractionTable);
        var ourInteractions = ReadTable(gameTables, InteractionTable);
        var interactionDescription = ourInteractions.Description ?? throw new InvalidDataException("Armageddon's vehicle_interactions.xtbl has no description");
        var takenInteractions = new HashSet<string>(ourInteractions.Entries.Select(XtblFile.EntryName), StringComparer.OrdinalIgnoreCase);
        // The animation lists are shared (GuerrillaPeople adds the people's sets): from the ones already written.
        // The people who get in are Guerrilla's own in its world (Mason the player, its NPCs, all on its
        // people's skeletons), so the vehicles' animations stay Guerrilla's: re-encoded, not moved onto
        // Darius. A Guerrilla vehicle in Armageddon's own levels plays them on Darius (a Darius version later).
        var sets = new GuerrillaAnimationSets(guerrillaTables, gameTables, Packfile.Open(Path.Combine(guerrillaCache, "anims.vpp_pc")), modFolder,
                                              retargetToDarius: false);
        var guerrillaArchive = Packfile.Open(Path.Combine(guerrillaCache, "vehicles_r.vpp_pc"));
        var gameArchive = Packfile.Open(Path.Combine(gameCache, "vehicles.vpp_pc"));
        var assetList = AsmFile.Read(gameArchive.Read(gameArchive.Find("vehicle_containers.asm_pc") ?? throw new InvalidDataException("no vehicle_containers.asm_pc in vehicles.vpp_pc")));

        // Guerrilla's entries, the first of each name (its table repeats a few names).
        var entries = new Dictionary<string, XtblElement>(StringComparer.OrdinalIgnoreCase);
        foreach (var e in rfgVehicles.Entries) entries.TryAdd(XtblFile.EntryName(e), e);
        var wanted = vehicles.Count == 1 && vehicles[0].Equals("all", StringComparison.OrdinalIgnoreCase) ? entries.Keys.ToList() : vehicles.ToList();
        var taken = new HashSet<string>(ourVehicles.Entries.Select(XtblFile.EntryName), StringComparer.OrdinalIgnoreCase);
        var takenNames = new HashSet<string>(ourVehicles.Entries.Select(e => e.ValueOf("Display_Name") ?? ""), StringComparer.OrdinalIgnoreCase);
        var containers = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);  // file name → container
        var built = new List<string>();
        var builtNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var modelLights = new Dictionary<string, IReadOnlyList<string>>(StringComparer.OrdinalIgnoreCase);  // a model's light types (GuerrillaVehicles.LightTypes)
        var rigs = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);  // Guerrilla's vehicle skeletons Armageddon hasn't: file → bytes

        // Their sounds, Guerrilla's own, in banks of Armageddon's kind (see GuerrillaVehicleSounds): the
        // engines, the Foley cues, and (below) the doors'. A failure leaves the vehicles without them.
        var soundFoley = new Dictionary<string, Dictionary<string, string>>(StringComparer.OrdinalIgnoreCase);
        var soundLog = new List<string>();
        GuerrillaSoundBanks? soundBanks = null;
        try
        {
            soundBanks = new GuerrillaSoundBanks(guerrillaCache, guerrillaTables, GuerrillaVehicleSounds.Part, GuerrillaVehicleSounds.BankPrefix) { Adpcm = GuerrillaSoundBanks.WorldSoundsAdpcm };
            soundFoley = GuerrillaVehicleSounds.Build(soundBanks, guerrillaTables,
                                                      wanted.Where(entries.ContainsKey).Select(v => entries[v]).Distinct(), soundLog);
        }
        catch (Exception error) when (error is IOException or InvalidDataException or NotSupportedException or ArgumentException or IndexOutOfRangeException)
        {
            soundLog.Add($"not built ({error.Message}); the vehicles have no sounds");
            soundBanks = null;
        }
        foreach (string line in soundLog) yield return "Sounds: " + line;
        string? DoorSound(string cue) => soundBanks?.OneShot(cue);
        var turrets = new GuerrillaTurrets(guerrillaTables, gameTables, modFolder, soundBanks);
        // The interactions' animations' sounds (getting in and out, doors), Guerrilla's cues as our events.
        if (soundBanks != null) sets.SoundOf = (cue, looping) => looping ? soundBanks.Loop(cue) : soundBanks.OneShot(cue);
        // The walkers' blend trees (see GuerrillaWalkerTrees): their sets' tree-named states, and the trees.
        var walkerTrees = new GuerrillaWalkerTrees(guerrillaTables);
        var footstepNames = new List<string>();  // Guerrilla's footsteps entries the vehicles name
        var walkerRigs = new List<string>();      // the walkers' rigs (Guerrilla's file names), for their legs' limb rows
        var gunnerVehicles = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);  // vehicles with a gunner's seat (a turret on a passenger seat)
        var flyerModels = new List<string>();    // the flyers' lines of flyers.txt (Guerrilla's flight model's values)
        sets.Trees = walkerTrees.Rewrite;

        foreach (string name in wanted)
        {
            if (!entries.TryGetValue(name, out var entry)) { yield return $"{name}: Guerrilla's vehicles.xtbl has no such vehicle"; continue; }
            if (taken.Contains(name)) { yield return $"{name}: Armageddon has a vehicle of that name already; left out"; continue; }
            string? meshFile = entry.Element("Mesh_Name")?.ValueOf("Filename");
            if (string.IsNullOrEmpty(meshFile)) { yield return $"{name}: no model named"; continue; }
            string mesh = Path.GetFileNameWithoutExtension(meshFile);
            string containerName = mesh + ".ccar_pc";
            if (gameArchive.Find(containerName + ".str2_pc") != null) { yield return $"{name}: Armageddon has a model named {mesh} already; left out"; continue; }

            // Its own skeleton, if it has one (walkers, flyers, turret trucks): Armageddon's if it has one of
            // that name, else Guerrilla's, as a new file the guerrilla_vehicles mod loads (see RigList).
            string? rigFile = entry.Element("Rig")?.Element("Rig")?.ValueOf("Filename")?.Trim();
            string? rig = string.IsNullOrEmpty(rigFile) ? null : Path.ChangeExtension(Path.GetFileName(rigFile), ".rig_pc").ToLowerInvariant();
            if (rig != null && gameTables.Find(rig) == null && !rigs.ContainsKey(rig))
            {
                if (guerrillaTables.Find(rig) is not { } rfgRig)
                {
                    yield return $"{name}: its skeleton {rig} is in neither game; left out (the game stops on a vehicle whose skeleton isn't loaded)";
                    continue;
                }
                rigs[rig] = guerrillaTables.Read(rfgRig);
            }

            // Its interaction (where and how people get in), Guerrilla's, and the animation set it plays.
            string? interactionName = entry.ValueOf("Vehicle_Interaction");
            var rfgInteraction = interactionName == null ? null
                : rfgInteractions.Entries.FirstOrDefault(e => XtblFile.EntryName(e).Equals(interactionName, StringComparison.OrdinalIgnoreCase));
            string? rfgSet = rfgInteraction?.ValueOf("Anim_Set")?.Trim();
            string? ourSet = null;
            var setLog = new List<string>();

            // The model, its textures, and the set's animations (each vehicle's container has its own).
            if (containers.ContainsKey(containerName + ".str2_pc"))
            {
                if (!string.IsNullOrEmpty(rfgSet)) (ourSet, _) = sets.Port(rfgSet, null, setLog);
            }
            else
            {
                var source = guerrillaArchive.Find(containerName + ".str2_pc");
                if (source == null) { yield return $"{name}: Guerrilla's vehicles_r.vpp_pc has no {containerName}.str2_pc"; continue; }
                var pack = guerrillaArchive.OpenContainer(source);
                byte[] Read(string file) => pack.Read(pack.Find(file) ?? throw new InvalidDataException($"{source.Name} has no {file}"));
                byte[] cFile = Read(mesh + ".ccar_pc"), gFile = Read(mesh + ".gcar_pc");
                if (GuerrillaVehicles.NotYet(cFile) is { } why) { yield return $"{name}: {why}; left out"; continue; }
                // A tank (Guerrilla's Tank or Artillery Tank type) has its drive wheels hidden, as Guerrilla's code does.
                bool tank = entry.ValueOf("Special_Vehicle_Type")?.Trim() is { } kind &&
                            (kind.Equals("Tank", StringComparison.OrdinalIgnoreCase) || kind.Equals("Artillery Tank", StringComparison.OrdinalIgnoreCase));
                var (header, textures) = GuerrillaVehicles.Convert(cFile, t => t == GuerrillaMaterials.GlassReflection ? t : GuerrillaVehicles.Alias(t), tank);
                modelLights[mesh] = GuerrillaVehicles.LightTypes(cFile);
                var files = new List<(string Name, byte[] Data)> { (mesh + ".ccar_pc", header), (mesh + ".gcar_pc", gFile) };
                var pegs = pack.Entries.Where(e => e.Name.EndsWith(".cpeg_pc", StringComparison.OrdinalIgnoreCase))
                               .Select(e => PegFile.Read(pack.Read(e), Read(PegFile.DataFileName(e.Name)!))).ToList();
                var missing = new List<string>();
                foreach (string texture in textures)
                {
                    var made = texture == GuerrillaMaterials.GlassReflection ? GlassReflection(gameCache)
                             : GuerrillaBuiltIns.Files(texture, GuerrillaVehicles.Alias(texture)) ?? Single(pegs, texture, GuerrillaVehicles.Alias(texture));
                    if (made == null) missing.Add(texture);
                    else files.AddRange(made);
                }
                int animationCount = 0;
                if (!string.IsNullOrEmpty(rfgSet))
                {
                    (ourSet, var animations) = sets.Port(rfgSet, pack, setLog);
                    files.AddRange(animations);
                    animationCount = animations.Count;
                }
                byte[] container = Packfile.CreateContainer(files);
                containers[containerName + ".str2_pc"] = container;
                assetList.Containers.RemoveAll(c => c.Name.Equals(containerName, StringComparison.OrdinalIgnoreCase));
                assetList.Containers.Add(Entry(containerName, container, files));
                yield return $"{name}: model {mesh} ({cFile.Length:N0} → {header.Length:N0} bytes), {textures.Count - missing.Count} textures" +
                             (missing.Count > 0 ? $" ({missing.Count} not found: {string.Join(", ", missing)})" : "") +
                             (animationCount > 0 ? $", {animationCount} animations" : "");
            }
            foreach (string line in setLog) yield return $"{name}: {line}";

            // The table entry.
            var copy = entry.Copy();
            var dropped = new List<string>();
            foreach (string field in NotYet)
                if (copy.Element(field) is { } part) { part.Remove(); dropped.Add(field); }
            // Its cameras' turning speed with the mouse: Guerrilla has one (Y_Axis_Rotation, its y_axis_rotation_speed),
            // Armageddon a second for the mouse (Y_Axis_Rotation_mouse, 1.0 when a camera has none: the reader 0x62E56D),
            // so with the mouse Guerrilla's vehicles turned five times as fast as Guerrilla's 0.2 (the user: "hard to aim /
            // looking around while driving"). Guerrilla's own value goes in the mouse's too.
            if (copy.Element("Cameras") is { } cameras)
                foreach (var camera in cameras.Elements)
                    if (camera.ValueOf("Y_Axis_Rotation") is { } speed && camera.Element("Y_Axis_Rotation_mouse") == null)
                        camera.AddElement("Y_Axis_Rotation_mouse", speed.Trim());
            // Its steering torque: both games build Havok's extra yaw torque for a car as Extra_Steering_Torque × Mass ÷ its
            // steering angle (Guerrilla automobile_create_havok_vehicle_data RM 0xB9B4A8, Armageddon 0x5EEB50), and none of
            // Guerrilla's cars names the field: they all take vehicle_defaults.xtbl's -0.5. Armageddon built ours with 0
            // (Min_LightPickup_1, the drive log 2026-11-18: "extra 0.00", where Guerrilla's gives -1,719): no push into the
            // turn, a wide circle (the user: "as if you are on a boat"). So a car of Guerrilla's that doesn't name it gets
            // Guerrilla's default written in.
            if (copy.Element("Steering") != null && copy.Element("Extra_Steering_Torque") == null && guerrillaSteeringTorque != null)
                copy.AddElement("Extra_Steering_Torque", guerrillaSteeringTorque);
            // Its turrets, Guerrilla's (see GuerrillaTurrets): a mount whose turret can't be had yet is left out.
            if (copy.Element("Turrets") is { } mounts)
            {
                foreach (var mount in mounts.Elements.ToList())
                {
                    string? turret = mount.ValueOf("AttachedTurret")?.Trim();
                    if (string.IsNullOrEmpty(turret)) continue;
                    if (turrets.Ported(turret, out string? why) is { } portedTurret)
                    {
                        mount.Element("AttachedTurret")!.Value = portedTurret;
                        if (mount.ValueOf("Seat")?.Trim() is { } seat && seat.StartsWith("passenger", StringComparison.OrdinalIgnoreCase)) gunnerVehicles.Add(name);
                    }
                    else { mount.Remove(); dropped.Add($"turret {turret} ({why})"); }
                }
                if (!mounts.Elements.Any()) mounts.Remove();
            }
            // Its effects, Guerrilla's (see EffectsOf), under the names the world builder added them by. An
            // effect the world's tables haven't shows nothing (the game's look-up gives none, 0x4EF370); an
            // explosion they haven't stays Guerrilla's name, Armageddon's explosion of that name, until the
            // world is built again (a vehicle's explosion is never left without one).
            var effects = copy.Element("Effects") ?? copy.AddElement("Effects");
            foreach (var (field, value) in EffectsOf(entry, rfgDefaults, effectFields))
            {
                if (value.Equals("null", StringComparison.OrdinalIgnoreCase)) { Set(effects, field, value); continue; }
                string ported = GuerrillaEffectTables.Added(value);
                bool isExplosion = ExplosionFields.Contains(field), there = (isExplosion ? worldExplosions : worldEffects).Contains(ported);
                if (!there) notInWorld.Add(ported);
                Set(effects, field, there || !isExplosion ? ported : value);
            }
            // Its own animation set (walkers', flyers', turret trucks' Rig/Anim_set: the interaction's set, by the name
            // it was ported under, not Armageddon's set of Guerrilla's name), and a walker's blend tree.
            // Its footsteps (walkers'): Guerrilla's entry, under our name for it (see GuerrillaVehicleFootsteps).
            if (copy.Element("Rig")?.Element("Footsteps") is { } footsteps && footsteps.Value.Trim().Length > 0)
            {
                footstepNames.Add(footsteps.Value.Trim());
                footsteps.Value = GuerrillaVehicleFootsteps.OurName(footsteps.Value.Trim(), gameTables);
            }
            if (ourSet != null && copy.Element("Rig")?.Element("Anim_set") is { } rigSet && rigSet.Value.Trim().Equals(rfgSet, StringComparison.OrdinalIgnoreCase))
            {
                rigSet.Value = ourSet;
                if (copy.ValueOf("VehicleClass")?.Trim().Equals("Walker", StringComparison.OrdinalIgnoreCase) == true)
                {
                    walkerTrees.Apply(copy, name, rfgSet!);
                    if (copy.Element("Rig")?.Element("Rig")?.ValueOf("Filename")?.Trim() is { Length: > 0 } walkerRig) walkerRigs.Add(walkerRig);
                }
            }
            // Its lights: each light type its model has, linked to our row for it (see GuerrillaVehicleLights).
            copy.Element("Lights")?.Remove();
            if (modelLights.TryGetValue(mesh, out var lightTypes) && lightTypes.Count > 0)
            {
                var map = copy.AddElement("Lights").AddElement("LightsMap");
                foreach (string type in lightTypes)
                {
                    var link = map.AddElement("LightMap");
                    link.AddElement("LightName", type);
                    link.AddElement("VehLightType", GuerrillaVehicleLights.RowFor(type));
                }
            }
            Prune(copy, description.Children, "", dropped, ReadAnyway);
            var rounded = new List<string>();
            FitValues(copy, description.Children, "", rounded);
            if (copy.ValueOf("VehicleClass")?.Trim().Equals("Flyer", StringComparison.OrdinalIgnoreCase) == true)
            {
                FlyerLimits(copy, rfgDefaults);
                flyerModels.Add(FlyerModel(name, entry, rfgDefaults));
            }
            // The skeleton by the name it is loaded under (in lower case: the game's own names are).
            if (rig != null && copy.Element("Rig")?.Element("Rig")?.Element("Filename") is { } rigName)
                rigName.Value = Path.ChangeExtension(rig, ".rigx");

            // Its interaction: Guerrilla's, converted, unless Armageddon has one of that name.
            if (rfgInteraction != null && ourSet != null && !takenInteractions.Contains(interactionName!))
            {
                var interaction = ConvertInteraction(rfgInteraction, interactionDescription, ourSet, DoorSound, out var left);
                ourInteractions.Table!.AddElement(interaction);
                takenInteractions.Add(interactionName!);
                yield return $"{name}: interaction {interactionName} ({interaction.Element("Waypoints")?.Elements.Count() ?? 0} waypoints, " +
                             $"{interaction.Element("Waypoint_transitions")?.Elements.Count() ?? 0} transitions, animation set {ourSet})" +
                             (left.Count > 0 ? $"; its doors' sounds that couldn't be had, left out: {string.Join(", ", left.Distinct())}" : "");
            }
            if (rfgInteraction != null && takenInteractions.Contains(interactionName!)) Set(copy, "Vehicle_Interaction", interactionName!);
            else
            {
                // No interaction: the vehicle can't be got into (as Guerrilla's own without one, and two of
                // Armageddon's). Not Armageddon's Dummy: with it the player gets in and can't drive or get out.
                copy.Element("Vehicle_Interaction")?.Remove();
                if (interactionName != null)
                    yield return rfgInteraction == null
                        ? $"{name}: Guerrilla has no interaction {interactionName}; it can't be got into"
                        : $"{name}: its interaction's animations couldn't be had; it can't be got into yet";
            }
            string? team = copy.ValueOf("Default_Team");
            if (team != null) Set(copy, "Default_Team", Teams.TryGetValue(team, out var ours) ? ours : "Civilian");
            if (copy.ValueOf("Group") is { } group && !groups.Contains(group)) { copy.Element("Group")!.Remove(); dropped.Add($"Group {group}"); }
            string display = copy.ValueOf("Display_Name") ?? name;
            if (takenNames.Contains(display)) Set(copy, "Display_Name", display = "rfg " + display);
            // Its sounds, where built: Armageddon's Foley fields naming the banks' events.
            if (soundFoley.TryGetValue(name, out var events))
            {
                var foleyGroup = copy.AddElement("Foley");
                foreach (var (field, eventName) in events) foleyGroup.AddElement(field, eventName);
            }
            var added = new List<string>();
            var unfilled = new List<string>();
            FillRequired(copy, description.Children, "", added, unfilled);
            ourVehicles.Table!.AddElement(copy);
            taken.Add(name);
            takenNames.Add(display);
            built.Add($"{name} (\"{display}\")");
            builtNames.Add(name);
            if (dropped.Count > 0) yield return $"{name}: left out of its table entry: {string.Join(", ", dropped)}";
            if (rounded.Count > 0) yield return $"{name}: decimals where Armageddon reads whole numbers, rounded: {string.Join(", ", rounded)}";
            if (added.Count > 0) yield return $"{name}: Armageddon's defaults added for the fields it requires: {string.Join(", ", added.Distinct())}";
            if (unfilled.Count > 0) yield return $"{name}: required by Armageddon, with no default to give (the game will stop on them): {string.Join(", ", unfilled)}";
        }

        foreach (var (file, bytes) in containers) File.WriteAllBytes(OverrideLayout.NewFile(overrides, OverrideLayout.Vehicles, file), bytes);
        string listFile = Path.Combine(overrides, AssetList);
        Directory.CreateDirectory(Path.GetDirectoryName(listFile)!);
        File.WriteAllBytes(listFile, assetList.ToBytes());
        Directory.CreateDirectory(modFolder);
        File.WriteAllBytes(Path.Combine(modFolder, Table), ourVehicles.ToBytes());
        int interactionNames = InteractionNameBytes(ourInteractions), interactionBlock = InteractionBlockBytes(ourInteractions);
        if (interactionNames > MostInteractionNames)
            throw new InvalidDataException($"{InteractionTable}'s names need {interactionNames} bytes of the game's {MostInteractionNames} (guerrilla_vehicles' " +
                                           "\"Vehicle interaction info strings\"): a vehicle whose interaction's name doesn't fit stops the game");
        if (interactionBlock >= MostInteractionBlock)
            throw new InvalidDataException($"{InteractionTable}'s records, waypoints and transitions need {interactionBlock} bytes of the game's {MostInteractionBlock} " +
                                           "(guerrilla_vehicles'): the game would stop reading the table there, and a vehicle whose interaction it never read stops the game");
        File.WriteAllBytes(Path.Combine(modFolder, InteractionTable), ourInteractions.ToBytes());
        yield return $"Vehicle interactions: {ourInteractions.Entries.Count()} in {InteractionTable}; their records, waypoints and transitions " +
                     $"{interactionBlock} of {MostInteractionBlock} bytes, their names {interactionNames} of {MostInteractionNames} bytes";
        // Guerrilla's vehicle spawn groups (the groups its parked vehicles' spots and traffic pick from; the
        // world builder places the spots, see GuerrillaVehiclePlaces), each as GroupPrefix + its name with the
        // vehicles built here, after Armageddon's own; a group none of whose vehicles were built is left out.
        var ourGroups = ReadTable(gameTables, SpawnGroupTable);
        var emptyGroups = new List<string>();
        var unbuilt = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        int groupsWritten = 0;
        var writtenGroups = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);  // Guerrilla's name → the vehicles built in it
        foreach (var rfgGroup in ReadTable(guerrillaTables, SpawnGroupTable).Entries)
        {
            var group = rfgGroup.Copy();
            Set(group, "Name", GuerrillaVehiclePlaces.GroupPrefix + XtblFile.EntryName(rfgGroup));
            foreach (var vehicle in group.Element("vehicle_list")?.Elements.ToList() ?? [])
                if (!builtNames.Contains(vehicle.Value.Trim())) { unbuilt.Add(vehicle.Value.Trim()); vehicle.Remove(); }
            if (group.Element("vehicle_list")?.Elements.Any() != true) { emptyGroups.Add(XtblFile.EntryName(rfgGroup)); continue; }
            ourGroups.Table!.AddElement(group);
            groupsWritten++;
            writtenGroups[XtblFile.EntryName(rfgGroup)] = group.Element("vehicle_list")!.Elements.Select(v => v.Value.Trim()).ToList();
        }
        if (ourGroups.Entries.Count() > MostSpawnGroups)
            throw new InvalidDataException($"{SpawnGroupTable} has {ourGroups.Entries.Count()} groups, more than the {MostSpawnGroups} guerrilla_vehicles makes room for: "
                                           + "the game stops while it starts (\"NULL != veh_group_info\")");
        File.WriteAllBytes(Path.Combine(modFolder, SpawnGroupTable), ourGroups.ToBytes());
        turrets.Write(modFolder);
        yield return GuerrillaVehicleLights.Write(guerrillaTables, gameTables, modFolder, worldEffects);
        yield return walkerTrees.Write(gameTables, modFolder, overrides);
        yield return GuerrillaWalkerLimbs.Write(guerrillaTables, gameTables, modFolder, walkerRigs);
        yield return GuerrillaVehicleFootsteps.Write(guerrillaTables, gameTables, modFolder, Path.GetDirectoryName(RigListFile(modFolder))!, footstepNames, worldEffects);
        // Guerrilla's traffic by zone, for the guerrilla_vehicles mod's traffic (see GuerrillaTraffic).
        string trafficLine;
        try { trafficLine = GuerrillaTraffic.Write(guerrillaCache, guerrillaTables, writtenGroups, gunnerVehicles, Path.GetDirectoryName(RigListFile(modFolder))!); }
        catch (Exception error) when (error is IOException or InvalidDataException) { trafficLine = $"Traffic: not written ({error.Message})"; }
        yield return trafficLine;
        // Guerrilla's safehouses' parked vehicles' spots and areas, for the guerrilla_vehicles mod (see GuerrillaSafehouseVehicles).
        string safehouseLine;
        try { safehouseLine = GuerrillaSafehouseVehicles.Write(guerrillaCache, Path.GetDirectoryName(RigListFile(modFolder))!); }
        catch (Exception error) when (error is IOException or InvalidDataException) { safehouseLine = $"Safehouses: not written ({error.Message})"; }
        yield return safehouseLine;
        // Guerrilla's flight model's values for each flyer, for the guerrilla_vehicles mod's flyers (Armageddon's reader keeps some only as
        // whole degrees, and the player's limits above overwrite two): name, then FlyerMaxUpThrust, TurnAngvel, TurnAngaccl (revolutions),
        // ThrustOffsetX, ThrustOffsetZ (radians, as Guerrilla's loader makes them), TiltAngvel, TiltAngaccl, SpinBankScalar,
        // ThrustTiltScalar, BankTiltScalar, DefaultLookatYScalar.
        File.WriteAllLines(Path.Combine(Path.GetDirectoryName(RigListFile(modFolder))!, FlyerFile),
                           new[] { @"# Guerrilla's flight model's values for its flyers (written by editor\GuerrillaBuilder.exe with the vehicles)" }.Concat(flyerModels));
        yield return $@"Flyers: {flyerModels.Count} with Guerrilla's flight model's values in mods\guerrilla_vehicles\{FlyerFile}";
        // Guerrilla's convoys (their definitions, end points and routes), for the guerrilla_vehicles mod's convoys (see GuerrillaConvoys).
        string convoyLine;
        try { convoyLine = GuerrillaConvoys.Write(guerrillaCache, guerrillaTables, writtenGroups, Path.GetDirectoryName(RigListFile(modFolder))!); }
        catch (Exception error) when (error is IOException or InvalidDataException) { convoyLine = $"Convoys: not written ({error.Message})"; }
        yield return convoyLine;
        yield return $"Turrets: {turrets.Added} of Guerrilla's in {GuerrillaTurrets.Table}" +
                     (turrets.LeftOut.Any() ? $"; left out (their vehicles' mounts too): {string.Join(", ", turrets.LeftOut)}" : "");
        yield return $"Turrets: for the weapons part ({GuerrillaTurrets.ItemList}): {turrets.ItemsSummary}";
        if (soundBanks != null)
        {
            List<string> written;
            try { written = soundBanks.Write(game, overrides, modFolder); }
            catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException) { written = [$"not written ({error.Message})"]; }
            foreach (string line in written.Concat(soundBanks.Log.Distinct())) yield return "Sounds: " + line;
        }
        yield return $"Spawn groups: {groupsWritten} of Guerrilla's in {SpawnGroupTable} (as {GuerrillaVehiclePlaces.GroupPrefix}<name>)" +
                     (unbuilt.Count > 0 ? $"; their vehicles not built, left out of them: {string.Join(", ", unbuilt)}" : "") +
                     (emptyGroups.Count > 0 ? $"; groups left out (none of their vehicles built): {string.Join(", ", emptyGroups)}" : "");
        sets.Write(overrides, OverrideLayout.Vehicles, modFolder);
        foreach (var (file, bytes) in rigs) File.WriteAllBytes(OverrideLayout.NewFile(overrides, OverrideLayout.Vehicles, file), bytes);
        string rigList = RigListFile(modFolder);
        Directory.CreateDirectory(Path.GetDirectoryName(rigList)!);
        File.WriteAllLines(rigList, rigs.Keys.Order(StringComparer.OrdinalIgnoreCase));
        if (notInWorld.Count > 0)
            yield return $"Effects: the world's tables (mods\\guerrilla_world\\effects.xtbl, explosions.xtbl) haven't these of Guerrilla's vehicle effects yet: " +
                         $"{string.Join(", ", notInWorld)}. Build the world again, then the vehicles (editor\\GuerrillaBuilder.exe): until then the vehicles show none of " +
                         "those effects, and explode as Armageddon's explosions of those names.";
        if (rigs.Count > 0) yield return $"Skeletons: {rigs.Count} of Guerrilla's vehicles' own, for the guerrilla_vehicles mod to load: {string.Join(", ", rigs.Keys)}";
        yield return $"Animation sets: Armageddon's own and the other Guerrilla tools' fill {sets.GameSets} of the {GuerrillaAnimationSets.MaxAnimSets} the game holds; " +
                     $"{sets.Room} were left for Guerrilla's" +
                     (sets.LeftOutForRoom.Count > 0 ? $", so these vehicles' sets were left out (they can't be got into): {string.Join(", ", sets.LeftOutForRoom)}" : "");
        yield return built.Count == 0 ? "No vehicles built." : $"Vehicles: {string.Join(", ", built)}";
    }

    /// <summary>
    /// Guerrilla's interaction entry for Armageddon (see the summary): its waypoints, transitions and
    /// steps as they are, each transition's door (Guerrilla's Door) as Armageddon's first (Door_1), the
    /// animation set as ported (<paramref name="animSet"/>), then pruned and filled as the vehicle entries
    /// are. The doors' sounds (Guerrilla's XACT cues) are named by the events <paramref name="soundOf"/> gives (in
    /// our banks); one it gives none for is left out, and <paramref name="leftOut"/> names it.
    /// </summary>
    static XtblElement ConvertInteraction(XtblElement rfg, FieldDescription description, string animSet, Func<string, string?> soundOf, out List<string> leftOut)
    {
        var copy = rfg.Copy();
        leftOut = [];
        // Guerrilla's "Moving Exit" (once, the mini hauler's quick door exit; its every other one is "Exit Moving"): Armageddon's
        // waypoints know "Exit Moving" by name and nothing by the other.
        foreach (var flag in (copy.Element("Waypoints")?.Elements ?? []).SelectMany(w => w.Element("Node_Type")?.Elements ?? []))
            if (flag.Value.Trim().Equals("Moving Exit", StringComparison.OrdinalIgnoreCase)) flag.Value = "Exit Moving";
        foreach (var transition in copy.Element("Waypoint_transitions")?.Elements ?? [])
        {
            if (transition.Element("Door") is { } door)
            {
                transition.InsertAfter(door, new XtblElement("Door_1") { Value = door.Value });
                door.Remove();
            }
            foreach (string field in new[] { "Door_open", "Door_close" })
                if (transition.Element(field) is { } sound && sound.Value.Trim().Length > 0)
                {
                    if (soundOf(sound.Value.Trim()) is { } eventName) sound.Value = eventName;
                    else
                    {
                        leftOut.Add(sound.Value.Trim());
                        sound.Remove();
                    }
                }
        }
        Set(copy, "Anim_Set", animSet);
        Prune(copy, description.Children, "", []);
        FitValues(copy, description.Children, "", []);
        FillRequired(copy, description.Children, "", [], []);
        return copy;
    }

    /// <summary>Takes away everything <see cref="Build"/> wrote: the vehicles' containers (the ones the asset list names that Armageddon hasn't), the asset list, the tables and the animation sets.</summary>
    public static IEnumerable<string> Remove(string game, string overrides, string modFolder)
    {
        foreach (string line in GuerrillaAnimationSets.Remove(game, overrides, modFolder)) yield return line;
        foreach (string line in GuerrillaSoundBanks.Remove(game, overrides, modFolder, GuerrillaVehicleSounds.Part)) yield return line;
        foreach (string line in GuerrillaWalkerLimbs.Remove(Packfile.Open(Path.Combine(game, "build", "pc", "cache", "misc.vpp_pc")), modFolder)) yield return line;
        string rigList = RigListFile(modFolder);
        if (File.Exists(rigList))
        {
            foreach (string rig in File.ReadAllLines(rigList).Select(l => l.Trim()))
                foreach (string file in OverrideLayout.Delete(overrides, rig)) yield return $"Deleted {file}";
            File.Delete(rigList);
            yield return $"Deleted {rigList}";
        }
        foreach (string list in new[] { GuerrillaTraffic.File, GuerrillaConvoys.File, FlyerFile })
        {
            string file = Path.Combine(Path.GetDirectoryName(rigList)!, list);
            if (File.Exists(file)) { File.Delete(file); yield return $"Deleted {file}"; }
        }
        // (footsteps.xtbl: only the vehicles write it so far, so it goes whole, with this part's list of its rows.)
        foreach (string file in new[] { Path.Combine(Path.GetDirectoryName(rigList)!, GuerrillaVehicleFootsteps.OwnRows), Path.Combine(modFolder, GuerrillaVehicleFootsteps.Table) })
            if (File.Exists(file)) { File.Delete(file); yield return $"Deleted {file}"; }
        string listFile = Path.Combine(overrides, AssetList);
        if (File.Exists(listFile))
        {
            var gameArchive = Packfile.Open(Path.Combine(game, "build", "pc", "cache", "vehicles.vpp_pc"));
            foreach (var c in AsmFile.Read(File.ReadAllBytes(listFile)).Containers.Where(c => gameArchive.Find(c.FileName) == null))
                foreach (string file in OverrideLayout.Delete(overrides, c.FileName)) yield return $"Deleted {file}";
            File.Delete(listFile);
            yield return $"Deleted {listFile}";
        }
        foreach (string name in new[] { Table, InteractionTable, SpawnGroupTable, GuerrillaTurrets.Table, GuerrillaTurrets.ItemList,
                                        GuerrillaVehicleLights.Table, GuerrillaWalkerTrees.FileList, GuerrillaWalkerTrees.TreeFile })
        {
            string table = Path.Combine(modFolder, name);
            if (File.Exists(table)) { File.Delete(table); yield return $"Deleted {table}"; }
        }
        string treeList = Path.Combine(overrides, GuerrillaWalkerTrees.ListOverride);
        if (File.Exists(treeList)) { File.Delete(treeList); yield return $"Deleted {treeList}"; }
        foreach (string file in OverrideLayout.Delete(overrides, GuerrillaWalkerTrees.TreeFile)) yield return $"Deleted {file}";
    }

    /// <summary>
    /// Leaves out of an entry (or a group in it) every field Armageddon's description hasn't (but those
    /// in <paramref name="keep"/>, by path), and every Selection value it has no choice for;
    /// <paramref name="dropped"/> names them.
    /// </summary>
    /// <summary>
    /// A flyer's flight limits in Armageddon's terms, from Guerrilla's. Guerrilla's player never flew (its
    /// flyer_steer is empty): its flyers flew for its AI, by a thrust model (flyer::flight_compute_physics:
    /// the acceleration left once the thrust holds the weight, g·√((1 + u·UpThrust)² − 1) for urgency u,
    /// in every direction; braking at the same; tilting and turning at its rates). Armageddon's player
    /// controller (0x61DA50) flies by speeds and accelerations instead, which Guerrilla's entries lack (0:
    /// its velocities would come out NaN). So: each axis's acceleration and deceleration Guerrilla's at full
    /// thrust (u = 1, m/s²), its top speed Guerrilla's fastest air-traffic speed, 50 m/s (Armageddon's in
    /// mph), the minimum scalars 1 (the limits as they are at any speed); the turn and tilt rates and
    /// accelerations Guerrilla's (in degrees, the ×360 above), each axis's deceleration and mouse
    /// acceleration as its acceleration; but Armageddon applies the angular acceleration per frame, not per
    /// second, so those are Guerrilla's per second over 60 frames; the bank angle the tilt Guerrilla's
    /// sideways acceleration gives (atan(BankTiltScalar · a / g)), banking at its tilt rate.
    /// </summary>
    const string FlyerFile = "flyers.txt";

    /// <summary>The room for vehicle_interactions.xtbl's names: guerrilla_vehicles' "Vehicle interaction info strings" (the game's 8 KB).</summary>
    const int MostInteractionNames = 0x8000;

    /// <summary>The vehicle spawn groups the game holds with guerrilla_vehicles (its own 32; Guerrilla's 136).</summary>
    const int MostSpawnGroups = 136;

    /// <summary>The room for vehicle_interactions.xtbl's records, waypoints and transitions: guerrilla_vehicles' block (the game's 64 KB).</summary>
    const int MostInteractionBlock = 0x40000;

    /// <summary>
    /// The bytes vehicle_interactions.xtbl takes in the game's block as its reader (0x65F360) lays it out: 32 a record
    /// (one an entry, and three more), 36 a waypoint, 48 a transition, 12 a step of a transition (its Interactions);
    /// it stops reading once they reach the block's size.
    /// </summary>
    static int InteractionBlockBytes(XtblFile interactions)
    {
        int bytes = (interactions.Entries.Count() + 3) * 32;
        foreach (var entry in interactions.Entries)
        {
            bytes += (entry.Element("Waypoints")?.Elements.Count() ?? 0) * 36;
            foreach (var transition in entry.Element("Waypoint_transitions")?.Elements ?? [])
                bytes += 48 + (transition.Element("Interactions")?.Elements.Count() ?? 0) * 12;
        }
        return bytes;
    }

    /// <summary>
    /// The bytes vehicle_interactions.xtbl's names take in the game's string pool, as its reader (0x65F360) stores them:
    /// each interaction's Name, each waypoint's Name and Parent_Component, each transition's Door_N; each name once
    /// (the pool's find-or-add, 0xC97DB0, compares without case), in its length + 1 rounded up to even, + 2 when that
    /// is even already.
    /// </summary>
    static int InteractionNameBytes(XtblFile interactions)
    {
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in interactions.Entries)
        {
            if (entry.ValueOf("Name") is { } name) names.Add(name.Trim());
            foreach (var waypoint in entry.Element("Waypoints")?.Elements ?? [])
                foreach (string field in new[] { "Name", "Parent_Component" })
                    if (waypoint.ValueOf(field) is { } text) names.Add(text.Trim());
            foreach (var transition in entry.Element("Waypoint_transitions")?.Elements ?? [])
                foreach (var door in transition.Elements.Where(e => System.Text.RegularExpressions.Regex.IsMatch(e.Name, @"^Door_\d+$", System.Text.RegularExpressions.RegexOptions.IgnoreCase)))
                    names.Add(door.Value.Trim());
        }
        return names.Sum(n => { int stored = System.Text.Encoding.Latin1.GetByteCount(n) + 1; return stored + (stored % 2 == 1 ? 1 : 2); });
    }

    /// <summary>A flyer's line of flyers.txt: Guerrilla's own FlyerParams (its entry's, else the defaults'), as its loader keeps them.</summary>
    static string FlyerModel(string name, XtblElement guerrillaEntry, XtblElement? rfgDefaults)
    {
        var flyer = guerrillaEntry.Element("FlyerParams");
        var defaults = rfgDefaults?.Element("FlyerParams");
        string Value(string field, double toRadians = 1)
        {
            string? text = flyer?.ValueOf(field) ?? defaults?.ValueOf(field);
            double v = double.TryParse(text, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double parsed) ? parsed : 0;
            return (v * toRadians).ToString("0.######", System.Globalization.CultureInfo.InvariantCulture);
        }
        return string.Join(" ", name, Value("FlyerMaxUpThrust"), Value("FlyerMaxTurnAngvel"), Value("FlyerMaxTurnAngaccl"),
                           Value("FlyerMaxThrustOffsetX", Math.PI / 180), Value("FlyerMaxThrustOffsetZ", Math.PI / 180), Value("FlyerMaxTiltAngvel"),
                           Value("FlyerMaxTiltAngaccl"), Value("FlyerSpinBankScalar"), Value("FlyerThrustTiltScalar"), Value("FlyerBankTiltScalar"),
                           Value("FlyerDefaultLookatYScalar"));
    }

    static void FlyerLimits(XtblElement vehicle, XtblElement? rfgDefaults)
    {
        var flyer = vehicle.Element("FlyerParams") ?? vehicle.AddElement("FlyerParams");
        var defaults = rfgDefaults?.Element("FlyerParams");
        double Get(string field, double fallback) =>
            double.TryParse(flyer.ValueOf(field) ?? defaults?.ValueOf(field), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double v) ? v : fallback;
        void Put(string field, double value, bool whole = false) =>
            (flyer.Element(field) ?? flyer.AddElement(field)).Value = whole ? ((int)Math.Round(value)).ToString(System.Globalization.CultureInfo.InvariantCulture)
                                                                            : value.ToString("0.###", System.Globalization.CultureInfo.InvariantCulture);
        // A turn or tilt rate in degrees: the entry's (converted ×360 already), else Guerrilla's default's (revolutions).
        double Degrees(string field, double revolutions) =>
            double.TryParse(flyer.ValueOf(field), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double v) ? v
            : 360 * (double.TryParse(defaults?.ValueOf(field), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double d) ? d : revolutions);
        const double G = 9.8, TopSpeed = 50, Mph = 0.44704, FramesPerSecond = 60;
        double upThrust = Get("FlyerMaxUpThrust", 0.8);
        double acceleration = G * Math.Sqrt(Math.Pow(1 + upThrust, 2) - 1);
        foreach (string axis in new[] { "Forward", "Lateral", "Vertical" })
        {
            Put($"FlyerMax{axis}Vel", TopSpeed / Mph);
            Put($"FlyerMax{axis}Accel", acceleration);
            Put($"FlyerMax{axis}Decel", acceleration);
        }
        Put("FlyerMinLinearAccelScalar", 1);
        foreach (string axis in new[] { "Turn", "Tilt" })
        {
            // (degrees a second, after the ×360 of Guerrilla's revolutions; Guerrilla's defaults likewise)
            double rate = Degrees($"FlyerMax{axis}Angvel", 0.25), perFrame = Math.Max(1, Degrees($"FlyerMax{axis}Angaccl", 0.25) / FramesPerSecond);
            Put($"FlyerMax{axis}Angaccl", perFrame, whole: true);
            Put($"FlyerMax{axis}Angdecl", perFrame, whole: true);
            Put($"FlyerMax{axis}AngacclMouse", perFrame, whole: true);
            if (axis == "Tilt") Put("FlyerMaxBankAccel", Math.Min(rate, 90), whole: true);  // Armageddon's most (its description's); the AAX's 108 is over it
        }
        Put("FlyerMinAngularAccelScalar", 1);
        Put("FlyerMaxBankAngle", Math.Atan(Get("FlyerBankTiltScalar", 0.5) * acceleration / G) * 180 / Math.PI, whole: true);
    }

    /// <summary>An entry of another of Guerrilla's tables made fit for Armageddon's as the vehicles' are: pruned, its values fitted, its required fields filled.</summary>
    internal static void FitEntry(XtblElement entry, FieldDescription description)
    {
        Prune(entry, description.Children, "", []);
        FitValues(entry, description.Children, "", []);
        FillRequired(entry, description.Children, "", [], []);
    }

    static void Prune(XtblElement element, IReadOnlyList<FieldDescription> fields, string path, List<string> dropped, ISet<string>? keep = null)
    {
        foreach (var child in element.Elements.ToList())
        {
            if (child.Name.Equals("_Editor", StringComparison.OrdinalIgnoreCase)) continue;
            var field = fields.FirstOrDefault(f => f.Name.Equals(child.Name, StringComparison.OrdinalIgnoreCase));
            if (field == null)
            {
                if (keep?.Contains(path + child.Name) == true) continue;
                child.Remove();
                dropped.Add(path + child.Name);
                continue;
            }
            switch (field.Type)
            {
                case FieldType.Element:
                    Prune(child, field.Children, path + child.Name + "/", dropped, keep);
                    break;
                case FieldType.List or FieldType.Grid when field.Item is { Type: FieldType.Element } item:
                    foreach (var one in child.Elements.Where(e => e.Name.Equals(field.ItemElementName, StringComparison.OrdinalIgnoreCase)).ToList())
                        Prune(one, item.Children, path + child.Name + "/", dropped, keep);
                    break;
                case FieldType.Selection when field.Choices.Count > 0 && !child.HasElements &&
                                              !field.Choices.Contains(child.Value.Trim(), StringComparer.OrdinalIgnoreCase) &&
                                              keep?.Contains($"{path}{child.Name}={child.Value.Trim()}") != true:
                    dropped.Add($"{path}{child.Name} {child.Value.Trim()}");
                    child.Remove();
                    break;
            }
        }
    }

    /// <summary>
    /// Writes an entry's numbers (in it, its groups and its lists' items) as Armageddon reads them: a
    /// field measured differently (<see cref="UnitScales"/>) converted; then a decimal in a field
    /// Armageddon's description makes a whole number (Int), which would stop its table reader, rounded,
    /// and named in <paramref name="rounded"/> (none are known; a new one wants its unit looked at).
    /// </summary>
    static void FitValues(XtblElement element, IReadOnlyList<FieldDescription> fields, string path, List<string> rounded)
    {
        foreach (var child in element.Elements)
        {
            var field = fields.FirstOrDefault(f => f.Name.Equals(child.Name, StringComparison.OrdinalIgnoreCase));
            if (field == null) continue;
            string at = path + field.Name;
            if (field.Type == FieldType.Element) { FitValues(child, field.Children, at + "/", rounded); continue; }
            if (field.Type is FieldType.List or FieldType.Grid && field.Item is { Type: FieldType.Element } item)
            {
                foreach (var one in child.Elements.Where(e => e.Name.Equals(field.ItemElementName, StringComparison.OrdinalIgnoreCase)))
                    FitValues(one, item.Children, at + "/", rounded);
                continue;
            }
            if (child.HasElements || field.Type is not (FieldType.Int or FieldType.Float)) continue;
            if (!double.TryParse(child.Value.Trim(), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double value)) continue;
            if (UnitScales.TryGetValue(at, out double scale)) value *= scale;
            else if (field.Type != FieldType.Int || value == Math.Floor(value)) continue;
            if (field.Type == FieldType.Int)
            {
                double whole = Math.Round(value);
                if (!UnitScales.ContainsKey(at) && whole != value) rounded.Add($"{at} {child.Value.Trim()}");
                child.Value = whole.ToString("0", System.Globalization.CultureInfo.InvariantCulture);
            }
            else child.Value = value.ToString("0.######", System.Globalization.CultureInfo.InvariantCulture);
        }
    }

    /// <summary>
    /// Adds to an entry (or a group in it, or each item of a list in it) every field Armageddon's
    /// description marks as required that it hasn't, with the description's default: the game's table
    /// reader stops on a missing one ("Could not find required attribute Vehicle:Neutral_Reticule").
    /// A missing group is added with its own required fields. <paramref name="unfilled"/> names the
    /// required fields with no default.
    /// </summary>
    static void FillRequired(XtblElement element, IReadOnlyList<FieldDescription> fields, string path, List<string> added, List<string> unfilled)
    {
        foreach (var field in fields)
        {
            var child = element.Element(field.Name);
            if (child == null && field.Required)
            {
                if (field.Type == FieldType.Element) child = element.AddElement(field.Name);
                else if (field.Default != null) element.AddElement(field.Name, field.Default);
                else { unfilled.Add(path + field.Name); continue; }
                added.Add(path + field.Name);
            }
            if (child == null) continue;
            if (field.Type == FieldType.Element) FillRequired(child, field.Children, path + field.Name + "/", added, unfilled);
            else if (field.Type is FieldType.List or FieldType.Grid && field.Item is { Type: FieldType.Element } item)
                foreach (var one in child.Elements.Where(e => e.Name.Equals(field.ItemElementName, StringComparison.OrdinalIgnoreCase)))
                    FillRequired(one, item.Children, path + field.Name + "/", added, unfilled);
        }
    }

    /// <summary>Sets a field of an entry, adding it if it isn't there.</summary>
    static void Set(XtblElement entry, string field, string value)
    {
        if (entry.Element(field) is { } existing) existing.Value = value;
        else entry.AddElement(field, value);
    }

    /// <summary>The reflection cube glass is given (<see cref="GuerrillaMaterials"/>): Armageddon's own, from its dlc01_precache.vpp_pc, as it is.</summary>
    static (string Name, byte[] Data)[] GlassReflection(string gameCache)
    {
        var precache = Packfile.Open(Path.Combine(gameCache, "dlc01_precache.vpp_pc"));
        byte[] Pre(string name) => precache.Read(precache.Find(name) ?? throw new InvalidDataException($"no {name} in dlc01_precache.vpp_pc"));
        string cube = GuerrillaMaterials.GlassReflection;
        return [(cube + ".cvbm_pc", Pre(cube + ".cvbm_pc")), (cube + ".gvbm_pc", Pre(cube + ".gvbm_pc"))];
    }

    /// <summary>A texture of the vehicle's own texture files as Armageddon's single-texture files under <paramref name="name"/>, or null if none has it.</summary>
    static (string Name, byte[] Data)[]? Single(IReadOnlyList<PegFile> pegs, string texture, string name)
    {
        foreach (var peg in pegs)
        {
            var t = peg.Textures.FirstOrDefault(x => string.Equals(Path.GetFileNameWithoutExtension(x.Name), texture, StringComparison.OrdinalIgnoreCase));
            if (t == null) continue;
            int levels = t.MipLevels;
            if (t.Format.IsBlockCompressed())
            {
                levels = 1;
                while (levels < t.MipLevels && Math.Min(t.Width >> levels, t.Height >> levels) >= 4) levels++;
            }
            var (header, data) = peg.Single(t, name + ".tga", levels);
            return [(name + ".cvbm_pc", header), (name + ".gvbm_pc", data)];
        }
        return null;
    }

    /// <summary>
    /// A vehicle container's asset list entry, as Armageddon's own: the model (type 11) and each texture
    /// (type 1), with their data files' sizes; each animation (type 16, flags 0, no data file).
    /// </summary>
    static AsmContainer Entry(string name, byte[] container, IReadOnlyList<(string Name, byte[] Data)> files)
    {
        var c = Packfile.FromBytes(name, container);
        var entry = new AsmContainer { Name = name, Type = 27, Flags = 0x80, DataOffset = (uint)c.DataStart, CompressedSize = c.CompressedDataSize };
        entry.Sizes.AddRange(files.Select(f => f.Data.Length));
        foreach (var (file, data) in files)
        {
            string ext = Path.GetExtension(file).ToLowerInvariant();
            byte type = ext switch { ".ccar_pc" => 11, ".cvbm_pc" or ".cpeg_pc" => 1, ".anim_pc" => 16, _ => 0 };
            if (type == 0) continue;
            if (type == 16)
            {
                entry.Primitives.Add(new AsmPrimitive { Name = file, Type = type, Allocator = 0, Flags = 0, SplitIndex = 0, HeaderSize = data.Length, DataSize = 0 });
                continue;
            }
            string dataName = Path.ChangeExtension(file, ".g" + ext[2..]);
            int dataSize = files.FirstOrDefault(f => f.Name.Equals(dataName, StringComparison.OrdinalIgnoreCase)).Data?.Length ?? 0;
            entry.Primitives.Add(new AsmPrimitive { Name = file, Type = type, Allocator = 0, Flags = 4, SplitIndex = 0, HeaderSize = data.Length, DataSize = dataSize });
        }
        return entry;
    }
}
