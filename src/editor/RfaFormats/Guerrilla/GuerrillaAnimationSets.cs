using RfaFormats.Meshes;
using RfaFormats.Tables;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's animation sets made usable in Armageddon, for GuerrillaArsenal. A set
/// (a Skeleton_Set table, such as Guerrilla's vehicle_taxi.xtbl) names the animation each state and
/// action plays: for a vehicle, getting in, sitting, driving, getting out, diving out. Both games read
/// sets the same way while they start: the list of sets (anim_set_filenames.xtbl), every animation's
/// triggers and flags (anim_files.xtbl), then each listed set's table by its file name. For each set
/// asked for:
///   - its animations, from the vehicle's own container or Guerrilla's anims.vpp_pc, to be packed in the
///     vehicle's container as Armageddon packs its own vehicles' player animations: as Guerrilla has them,
///     in Armageddon's packing, for the people who drive in Guerrilla's world (Mason, Guerrilla's NPCs:
///     their own skeletons; retargetToDarius false), or moved from Mason's skeleton onto Darius's
///     (<see cref="PlayerRetarget"/>, given Armageddon's anims.vpp_pc to learn the two skeletons from; else
///     <see cref="GuerrillaAnimations"/>). Moved onto Darius, one Armageddon has itself on Darius (its
///     developers moved it: the same name and frame count, in its preloaded animations) is played by its
///     own name instead, with Armageddon's own entry;
///   - each animation's anim_files.xtbl entry, Guerrilla's own, added to Armageddon's table: its
///     triggers (when a door opens and is open, when the camera and the person's place hand over, the
///     footsteps) and its flags. Its sounds and voice lines (Guerrilla's are XACT cues) are left out
///     until Guerrilla's audio is ported;
///   - the set's table, Guerrilla's own, as a new file the game finds by name (it isn't in the game's
///     start-up copy of its tables), listed in anim_set_filenames.xtbl;
///   - the state and action names it uses that Armageddon's tables haven't (Guerrilla's code names some
///     itself, as "driver look back", which Armageddon's code doesn't), added to anim_states.xtbl and
///     anim_actions.xtbl, so the set reads whole.
/// The changed lists are handed to the game by the guerrilla_world mod (mods\guerrilla_world\). A name
/// Armageddon already uses (a set, an animation) gets the prefix rfg_; the others keep Guerrilla's.
/// The four lists are shared with the other tools that add sets (GuerrillaPeople's people): given
/// the mod's folder, this starts from the lists already there and replaces only its own entries, which
/// it records in mods\guerrilla_vehicles\anim_entries.txt (each tool its own such file, "set x.xtbl" and
/// "file x.animx" a line); without a record yet, its own are taken to be those neither Armageddon's nor
/// in another tool's record.
/// </summary>
public sealed class GuerrillaAnimationSets
{
    /// <summary>The tables this changes, written to the mod's folder.</summary>
    public const string SetList = "anim_set_filenames.xtbl", FileList = "anim_files.xtbl", StateList = "anim_states.xtbl", ActionList = "anim_actions.xtbl";

    /// <summary>
    /// Guerrilla's people's skeletons all start with the same 52 bones in the same order (its player's,
    /// player.rig_pc), and some add their own after them: Mason's camera (53), EDF soldiers' shoulder
    /// pads (54, 55), long coats and jackets (59, 63, 64). An animation is made for one of them, told by
    /// its bone count, and is moved onto Darius's skeleton (Armageddon's player_v2.rig_pc) from the first
    /// of Guerrilla's skeletons with that count (Mason's own for 53). An animation for any other count is
    /// for a vehicle's own skeleton (a turret, a flyer, a walker's legs), the same in Armageddon, and is
    /// only re-packed.
    /// </summary>
    const string PeopleRig = "player.rig_pc", MasonRig = "mason.rig_pc", DariusRig = "player_v2.rig_pc";

    /// <summary>
    /// How many animation sets the game holds with the guerrilla_vehicles mod: 255 (the game's own table
    /// has 140 places, MAX_ANIM_SETS, and one more stops it with "Num_anim_sets &lt; MAX_ANIM_SETS" and a
    /// crash; the mod gives it a bigger one). Armageddon fills 125 itself: the sets in the files its
    /// list names (50) and one for each object with animations of its own (anim_rig_objects.xtbl, 75:
    /// doors, machines). Sets past the room left are left out.
    /// </summary>
    public const int MaxAnimSets = 255;
    const string RigObjects = "anim_rig_objects.xtbl";

    /// <summary>The fields of an anim_files.xtbl entry left out for now (see the summary), and Guerrilla's that Armageddon's entries haven't.</summary>
    static readonly string[] LeftOutOfFiles = ["Sounds", "Voice_Lines", "OldProps"];

    readonly Packfile guerrillaTables, gameTables, guerrillaAnims;
    readonly Packfile? gameAnims;            // Armageddon's anims.vpp_pc (its preloaded animations), if given
    readonly PlayerRetarget? retarget;       // Mason's skeleton to Darius's, learnt from it
    readonly Dictionary<string, bool> armageddonsOwn = new(StringComparer.OrdinalIgnoreCase);  // Guerrilla's x.animx → whether Armageddon has it itself
    readonly bool toDarius;                  // people's animations moved onto Darius (else kept for Guerrilla's own skeletons)
    readonly Dictionary<int, Rig> peopleRigs = [];  // Guerrilla's people's skeletons, by bone count
    readonly Rig darius;
    readonly XtblFile setList, fileList, stateList, actionList;
    readonly XtblElement fileEntries;
    readonly Dictionary<string, XtblElement> rfgFileEntries = new(StringComparer.OrdinalIgnoreCase);  // Guerrilla's anim_files.xtbl, by file name (x.animx)
    readonly HashSet<string> gameAnimations, gameSets, ourStates, ourActions;  // Armageddon's own animations (x.animx) and sets (x.xtbl); the states and actions, ours added
    readonly Dictionary<string, string> setNames = new(StringComparer.OrdinalIgnoreCase);  // Guerrilla's set → ours (or "" if it couldn't be had)
    readonly Dictionary<string, byte[]> animations = new(StringComparer.OrdinalIgnoreCase);  // our .anim_pc file → its bytes
    readonly List<(string File, byte[] Data)> setTables = [];
    readonly List<string> noRoom = [];  // Guerrilla's sets left out for want of room
    readonly List<string> ownFiles = [];  // the animations' entries this run adds (x.animx)
    readonly HashSet<string> earlierSets = new(StringComparer.OrdinalIgnoreCase);  // the set tables an earlier run of this tool wrote
    bool statesChanged, actionsChanged, othersInLists;

    /// <summary>
    /// The animations' sounds (each entry's Sounds: a frame, looping or not, and Guerrilla's cue as Foley), when
    /// given: a cue's event in our banks (GuerrillaSoundBanks: OneShot, or Loop for a looping one), or null to
    /// leave that sound out. Not given, the sounds are left out (as for the parts that don't build sounds).
    /// </summary>
    public Func<string, bool, string?>? SoundOf { get; set; }

    /// <summary>When given, called with each Guerrilla set (its name, its Skeleton_Set) before its animations are gathered (GuerrillaWalkerTrees: states naming a blend tree).</summary>
    public Action<string, XtblElement>? Trees { get; set; }

    /// <summary>This tool's record of its own entries in the shared lists, beside its skeleton list (mods\guerrilla_vehicles\).</summary>
    public const string OwnEntries = "anim_entries.txt";
    static string OwnEntriesFile(string modFolder) => Path.Combine(Path.GetDirectoryName(Path.GetFullPath(modFolder))!, "guerrilla_vehicles", OwnEntries);

    /// <summary>A record of a tool's own entries: its sets' tables and its animations' entries; null if there is none.</summary>
    static (HashSet<string> Sets, HashSet<string> Files)? ReadRecord(string path)
    {
        if (!File.Exists(path)) return null;
        var sets = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var files = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var parts in File.ReadAllLines(path).Select(l => l.Trim().Split(' ', 2)).Where(p => p.Length == 2))
            (parts[0] == "set" ? sets : parts[0] == "file" ? files : null)?.Add(parts[1].Trim());
        return (sets, files);
    }

    /// <summary>The other tools' records (mods\*\anim_entries.txt but this tool's own).</summary>
    static (HashSet<string> Sets, HashSet<string> Files) OthersRecords(string modFolder)
    {
        var sets = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var files = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        string mods = Path.GetDirectoryName(Path.GetFullPath(modFolder))!, own = Path.GetFullPath(OwnEntriesFile(modFolder));
        if (!Directory.Exists(mods)) return (sets, files);
        foreach (string folder in Directory.GetDirectories(mods))
        {
            string path = Path.Combine(folder, OwnEntries);
            if (Path.GetFullPath(path).Equals(own, StringComparison.OrdinalIgnoreCase) || ReadRecord(path) is not { } record) continue;
            sets.UnionWith(record.Sets);
            files.UnionWith(record.Files);
        }
        return (sets, files);
    }

    /// <summary>How many sets Armageddon fills itself, and how many of ours fit.</summary>
    public int GameSets { get; }
    public int Room => MaxAnimSets - GameSets;

    /// <summary>Guerrilla's sets left out because the game holds no more (see <see cref="MaxAnimSets"/>).</summary>
    public IReadOnlyList<string> LeftOutForRoom => noRoom;

    /// <summary>
    /// The sets' port, with Armageddon's lists, or, given <paramref name="modFolder"/> (mods\guerrilla_world\),
    /// the lists already written there with this tool's earlier entries taken out (see the summary).
    /// </summary>
    public GuerrillaAnimationSets(Packfile guerrillaTables, Packfile gameTables, Packfile guerrillaAnims, string? modFolder = null, Packfile? gameAnims = null,
                                  bool retargetToDarius = true)
    {
        (this.guerrillaTables, this.gameTables, this.guerrillaAnims, this.gameAnims) = (guerrillaTables, gameTables, guerrillaAnims, gameAnims);
        toDarius = retargetToDarius;
        var people = Rig.Read(Read(guerrillaTables, PeopleRig));
        var mason = Rig.Read(Read(guerrillaTables, MasonRig));
        peopleRigs[mason.Bones.Count] = mason;
        foreach (var entry in guerrillaTables.Entries.Where(e => e.Name.EndsWith(".rig_pc", StringComparison.OrdinalIgnoreCase)))
        {
            Rig rig;
            try { rig = Rig.Read(guerrillaTables.Read(entry)); }
            catch (Exception e) when (e is InvalidDataException or ArgumentException or IndexOutOfRangeException) { continue; }
            if (rig.Bones.Count >= people.Bones.Count && !peopleRigs.ContainsKey(rig.Bones.Count) &&
                Enumerable.Range(0, people.Bones.Count).All(i => rig.Bones[i].Name.Equals(people.Bones[i].Name, StringComparison.OrdinalIgnoreCase) &&
                                                                 rig.Bones[i].Parent == people.Bones[i].Parent))
                peopleRigs[rig.Bones.Count] = rig;
        }
        darius = Rig.Read(Read(gameTables, DariusRig));
        if (gameAnims != null && retargetToDarius)
            try { retarget = PlayerRetarget.Fit(guerrillaTables, guerrillaAnims, gameTables, gameAnims); }
            catch (InvalidDataException) { retarget = null; }
        setList = XtblFile.FromBytes(Read(gameTables, SetList));
        fileList = XtblFile.FromBytes(Read(gameTables, FileList));
        stateList = XtblFile.FromBytes(Read(gameTables, StateList));
        actionList = XtblFile.FromBytes(Read(gameTables, ActionList));
        fileEntries = fileList.Table?.Element("Files")?.Element("Anim_files") ?? throw new InvalidDataException($"Armageddon's {FileList} has no Files/Anim_files");
        gameAnimations = new(fileEntries.Elements.Select(AnimationName).OfType<string>(), StringComparer.OrdinalIgnoreCase);
        gameSets = new(setList.Entries.Select(e => e.ValueOf("Name")?.Trim()).OfType<string>(), StringComparer.OrdinalIgnoreCase);
        ourStates = new(stateList.Entries.Select(XtblFile.EntryName), StringComparer.OrdinalIgnoreCase);
        ourActions = new(actionList.Entries.Select(XtblFile.EntryName), StringComparer.OrdinalIgnoreCase);
        // Armageddon's own sets: each listed file's (a file this can't find counts as one), and its rig objects'.
        int ownSets = 0;
        foreach (string name in setList.Entries.Select(e => e.ValueOf("Name")?.Trim()).OfType<string>())
            ownSets += gameTables.Find(name) is { } file
                ? Math.Max(1, XtblFile.FromBytes(gameTables.Read(file)).Entries.Count(e => e.Name.Equals("Skeleton_Set", StringComparison.OrdinalIgnoreCase)))
                : 1;
        if (gameTables.Find(RigObjects) is { } rigObjects)
            ownSets += XtblFile.FromBytes(gameTables.Read(rigObjects)).Entries.Count(e => e.Name.Equals("Anim_Rig_Object", StringComparison.OrdinalIgnoreCase));
        GameSets = ownSets;

        // The lists already written (Armageddon's with ours and other tools'), without this tool's own.
        if (modFolder != null)
        {
            var earlier = ReadRecord(OwnEntriesFile(modFolder));
            var others = OthersRecords(modFolder);
            bool Ours(string name, HashSet<string> armageddons, HashSet<string> recorded, HashSet<string> othersOwn) =>
                earlier != null ? recorded.Contains(name) : !armageddons.Contains(name) && !othersOwn.Contains(name);
            XtblFile? Existing(string name) => File.Exists(Path.Combine(modFolder, name)) ? XtblFile.FromBytes(File.ReadAllBytes(Path.Combine(modFolder, name))) : null;
            if (Existing(SetList) is { } sets)
            {
                setList = sets;
                foreach (var e in setList.Entries.ToList())
                {
                    string name = e.ValueOf("Name")?.Trim() ?? "";
                    if (Ours(name, gameSets, earlier?.Sets ?? [], others.Sets)) { earlierSets.Add(name); e.Remove(); }
                    else if (!gameSets.Contains(name)) { GameSets++; othersInLists = true; }
                }
            }
            if (Existing(FileList) is { } files)
            {
                fileList = files;
                fileEntries = fileList.Table?.Element("Files")?.Element("Anim_files") ?? throw new InvalidDataException($"the {FileList} in {modFolder} has no Files/Anim_files");
                foreach (var e in fileEntries.Elements.ToList())
                {
                    string name = AnimationName(e) ?? "";
                    if (Ours(name, gameAnimations, earlier?.Files ?? [], others.Files)) e.Remove();
                    else if (!gameAnimations.Contains(name)) othersInLists = true;
                }
            }
            // The state and action names: every one kept (another tool's sets may name them too).
            if (Existing(StateList) is { } states) { stateList = states; statesChanged = true; }
            if (Existing(ActionList) is { } actions) { actionList = actions; actionsChanged = true; }
            ourStates = new(stateList.Entries.Select(XtblFile.EntryName), StringComparer.OrdinalIgnoreCase);
            ourActions = new(actionList.Entries.Select(XtblFile.EntryName), StringComparer.OrdinalIgnoreCase);
        }

        var rfgFiles = XtblFile.FromBytes(Read(guerrillaTables, FileList)).Table?.Element("Files")?.Element("Anim_files");
        foreach (var e in rfgFiles?.Elements ?? [])
            if (AnimationName(e) is { } name) rfgFileEntries.TryAdd(name, e);
    }

    static byte[] Read(Packfile pack, string name) =>
        pack.Read(pack.Find(name) ?? throw new InvalidDataException($"no {name} in {pack.Name}"));

    /// <summary>An anim_files.xtbl entry's animation (x.animx).</summary>
    static string? AnimationName(XtblElement entry) => entry.Element("Animation")?.ValueOf("Filename")?.Trim();

    /// <summary>
    /// The weapon groups Armageddon's code knows (its list at 0x1000128): its set loader (0x97C3A0) loads no set
    /// naming another ("We should have found the group in the group names array"). Its anim_groups.xtbl
    /// differs a little (it has no Grinder), so the code's list is the one that counts.
    /// </summary>
    static readonly HashSet<string> ArmageddonGroups = new(StringComparer.OrdinalIgnoreCase)
    {
        "Default", "AK", "Melee", "Pistol", "Revolver", "Rifle", "RPG", "Mortar", "Mine", "Hammer", "Remote Charge", "Turret Machinegun",
        "Turret Rocket", "Turret Railgun", "2HCarry", "Gutter", "Shotgun", "Sniper", "Arc", "Railgun", "Nano Rifle", "Rocket Launcher", "Unicorn",
        "Shield", "Grinder", "Enforcer", "Dual Wield", "Nanoforge", "Plasma Cannon", "Laser Pistol", "Plasma Launcher", "Spit", "Spray", "Boom",
        "Antenna", "Alien Shotgun", "Hand", "Quill", "Magnet Gun", "Machine Pistol", "Gravitic Imploder", "m16", "Plasma Rifle", "RPG Launcher",
    };

    /// <summary>Guerrilla's groups under Armageddon's name for the same weapon.</summary>
    static readonly Dictionary<string, string> GroupNames = new(StringComparer.OrdinalIgnoreCase) { ["Nano"] = "Nano Rifle" };

    /// <summary>
    /// A set's groups as Armageddon's code knows them: Guerrilla's Nano as Nano Rifle; a group it has no name for
    /// (Guerrilla's Pipe, which its civilians' set has with nothing in it) left out, said in the log. For every
    /// converter of Guerrilla's sets (the vehicles', the people's): call it on the Skeleton_Set before writing it.
    /// </summary>
    internal static void FitGroups(string set, XtblElement skeletonSet, List<string> log)
    {
        if (skeletonSet.Element("Groups") is not { } groups) return;
        var names = groups.Elements.Select(g => g.ValueOf("group")?.Trim() ?? "").ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (var group in groups.Elements.ToList())
        {
            if (group.Element("group") is not { } field) continue;
            string name = field.Value.Trim();
            if (ArmageddonGroups.Contains(name)) continue;
            if (GroupNames.TryGetValue(name, out var ours) && !names.Contains(ours))
            {
                field.Value = ours;
                names.Add(ours);
                log.Add($"animation set {set}: its group {name} is Armageddon's {ours}");
                continue;
            }
            int held = (group.Element("States")?.Elements.Count() ?? 0) + (group.Element("Actions")?.Elements.Count() ?? 0);
            group.Remove();
            log.Add(held == 0 ? $"animation set {set}: left out its empty group {name} (Armageddon has no such weapon group)"
                              : $"animation set {set}: left out its group {name}'s {held} states and actions (Armageddon has no such weapon group)");
        }
    }

    /// <summary>
    /// Guerrilla's set <paramref name="set"/> (its Skeleton_Set name, as an interaction's Anim_Set names
    /// it) for Armageddon: its name there (null if it can't be had), and its animations to pack in the
    /// vehicle's container, looked for first in <paramref name="container"/> (Guerrilla's vehicle
    /// container), then in Guerrilla's anims.vpp_pc. Each set and animation is converted once; a set
    /// asked for again gives its animations again (each vehicle's container has its own copies).
    /// </summary>
    public (string? Set, IReadOnlyList<(string Name, byte[] Data)> Animations) Port(string set, Packfile? container, List<string> log)
    {
        if (setNames.TryGetValue(set, out var known) && known.Length == 0) return (null, []);
        if (known == null && setTables.Count >= Room)
        {
            setNames[set] = "";
            noRoom.Add(set);
            log.Add($"animation set {set}: left out, the game holds {MaxAnimSets} animation sets and Armageddon's own and ours already fill them");
            return (null, []);
        }
        string rfgFile = set + ".xtbl";
        var rfgSet = guerrillaTables.Find(rfgFile);
        if (rfgSet == null) { setNames[set] = ""; log.Add($"animation set {set}: Guerrilla's misc.vpp_pc has no {rfgFile}"); return (null, []); }
        var table = XtblFile.FromBytes(guerrillaTables.Read(rfgSet));
        var skeletonSet = table.Entries.FirstOrDefault(e => e.Name.Equals("Skeleton_Set", StringComparison.OrdinalIgnoreCase));
        if (skeletonSet == null) { setNames[set] = ""; log.Add($"animation set {set}: {rfgFile} has no Skeleton_Set"); return (null, []); }
        Trees?.Invoke(set, skeletonSet);

        // Its animations, each moved onto Darius's skeleton (all of them, or the set is left out).
        var files = Descendants(skeletonSet, "Animation").Select(a => a.ValueOf("Filename")?.Trim()).OfType<string>()
                        .Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        // Their variants: animations the game plays in an animation's place by a tag (anim_files Variants: a car's
        // driver's "Reverse", looking back as it reverses, and "Coat"), named in the animation's entry, not the set's.
        // Made too, under our names, and the entries name ours (Round 3: the Kitsune's driver's Reverse variant still
        // named scout_dr_lookarnd_bck, a name Armageddon's table has without the animation, and the driver T-posed
        // when reversing). One Guerrilla's archives haven't is left out of its entry, the set kept.
        var variantFiles = files.SelectMany(f => rfgFileEntries.TryGetValue(f, out var e) ? VariantFiles(e) : [])
                                .Where(v => !files.Contains(v, StringComparer.OrdinalIgnoreCase)).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        var ours = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);  // Guerrilla's x.animx → ours
        var made = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);  // our .anim_pc files new in this set
        var missing = new List<string>();
        var missingVariants = new List<string>();
        foreach (string file in files.Concat(variantFiles))
        {
            bool variant = variantFiles.Contains(file, StringComparer.OrdinalIgnoreCase);
            var lacking = variant ? missingVariants : missing;
            if (toDarius && ArmageddonsOwn(file, container)) { ours[file] = file; continue; }
            string name = gameAnimations.Contains(file) ? "rfg_" + file : file;
            string pc = Path.ChangeExtension(name, ".anim_pc"), rfgPc = Path.ChangeExtension(file, ".anim_pc");
            if (animations.ContainsKey(pc) || made.ContainsKey(pc)) { ours[file] = name; continue; }
            Packfile? source = container?.Find(rfgPc) != null ? container : guerrillaAnims.Find(rfgPc) != null ? guerrillaAnims : null;
            if (source == null) { lacking.Add(file); continue; }
            byte[] bytes = source.Read(source.Find(rfgPc)!);
            if (bytes.Length < 0x30) { lacking.Add($"{file} (too short to be an animation)"); continue; }
            ours[file] = name;
            bool additive = rfgFileEntries.TryGetValue(file, out var rfgEntry) &&
                            (rfgEntry.Element("Flags")?.Elements.Any(f => f.Value.Trim().Equals("Additive", StringComparison.OrdinalIgnoreCase)) ?? false);
            try
            {
                made[pc] = !peopleRigs.TryGetValue(bytes[0xB], out var skeleton) || !toDarius ? GuerrillaAnimations.Recode(bytes)
                         : retarget != null ? retarget.ToDarius(bytes, skeleton, additive)
                         : additive ? GuerrillaAnimations.RetargetAdditive(bytes, skeleton, darius)
                         : GuerrillaAnimations.Retarget(bytes, skeleton, darius);
            }
            catch (InvalidDataException e) { ours.Remove(file); lacking.Add($"{file} ({e.Message})"); }
        }
        if (missing.Count > 0)
        {
            setNames[set] = "";
            log.Add($"animation set {set}: left out, its animations can't all be had: {string.Join(", ", missing)}");
            return (null, []);
        }
        if (missingVariants.Count > 0) log.Add($"animation set {set}: variants Guerrilla's archives haven't, left out of their entries: {string.Join(", ", missingVariants)}");
        foreach (var (file, name) in ours)
        {
            string pc = Path.ChangeExtension(name, ".anim_pc");
            if (!made.TryGetValue(pc, out var bytes)) continue;
            animations[pc] = bytes;
            AddFileEntry(file, name, log, ours);
        }

        // Its table, with our names, and the state and action names Armageddon hasn't.
        string ourSet = gameSets.Contains(rfgFile) || gameTables.Find(rfgFile) != null ? "rfg_" + set : set;
        string ourFile = ourSet + ".xtbl";
        if (!setNames.ContainsKey(set))
        {
            if (skeletonSet.Element("Name") is { } nameField) nameField.Value = ourSet;
            if (skeletonSet.Element("Variant_Type") is { } variant)
            {
                log.Add($"animation set {set}: left out its Variant_Type {variant.Value.Trim()} (Armageddon's sets have none)");
                variant.Remove();
            }
            FitGroups(set, skeletonSet, log);
            foreach (var animation in Descendants(skeletonSet, "Animation"))
                if (animation.Element("Filename") is { } filename && ours.TryGetValue(filename.Value.Trim(), out var renamed))
                    filename.Value = renamed;
            var added = new List<string>();
            foreach (var id in Descendants(skeletonSet, "State").Select(s => s.ValueOf("ID")?.Trim()).OfType<string>())
                if (ourStates.Add(id)) { stateList.NewEntry(id, ["Entries", id[..1].ToLowerInvariant()]); statesChanged = true; added.Add($"state \"{id}\""); }
            foreach (var id in Descendants(skeletonSet, "Action").Select(s => s.ValueOf("ID")?.Trim()).OfType<string>())
                if (ourActions.Add(id)) { actionList.NewEntry(id, ["Entries", id[..1].ToLowerInvariant()]); actionsChanged = true; added.Add($"action \"{id}\""); }
            setTables.Add((ourFile, table.ToBytes()));
            var listed = guerrillaTables.Find(SetList) is { } list
                ? XtblFile.FromBytes(guerrillaTables.Read(list)).Entries.FirstOrDefault(e => string.Equals(e.ValueOf("Name")?.Trim(), rfgFile, StringComparison.OrdinalIgnoreCase))
                : null;
            var listing = listed?.Copy() ?? new XtblElement("Anim_Set_Filename");
            if (listing.Element("Name") is { } listedName) listedName.Value = ourFile;
            else listing.AddElement("Name", ourFile);
            setList.Table!.AddElement(listing);
            setNames[set] = ourSet;
            log.Add($"animation set {set}" + (ourSet != set ? $" (as {ourSet})" : "") + $": {files.Count} animations" +
                    (added.Count > 0 ? $"; names Armageddon's tables lacked, added: {string.Join(", ", added)}" : ""));
        }
        return (ourSet, ours.Where(o => !armageddonsOwn.GetValueOrDefault(o.Key)).Select(o => Path.ChangeExtension(o.Value, ".anim_pc"))
                            .Select(pc => (pc, animations[pc])).ToList());
    }

    /// <summary>
    /// Whether Armageddon has Guerrilla's animation itself, on Darius: its developers moved many of
    /// Guerrilla's player animations over under Guerrilla's names; one of the same name and frame count in
    /// Armageddon's preloaded animations (anims.vpp_pc) for Darius's skeleton is theirs, and is played by
    /// its name (no copy of ours, no entry of ours: Armageddon's own).
    /// </summary>
    bool ArmageddonsOwn(string file, Packfile? container)
    {
        if (armageddonsOwn.TryGetValue(file, out var known)) return known;
        bool same = false;
        string pc = Path.ChangeExtension(file, ".anim_pc");
        if (gameAnims?.Find(pc) is { } theirs && gameAnimations.Contains(file))
        {
            Packfile? source = container?.Find(pc) != null ? container : guerrillaAnims.Find(pc) != null ? guerrillaAnims : null;
            try
            {
                var armageddons = Animation.Read(gameAnims.Read(theirs));
                same = source != null && armageddons.SkeletonBones == darius.Bones.Count &&
                       armageddons.Frames == Animation.ReadGuerrilla(source.Read(source.Find(pc)!)).Frames;
            }
            catch (Exception e) when (e is InvalidDataException or ArgumentException or IndexOutOfRangeException) { }
        }
        return armageddonsOwn[file] = same;
    }

    /// <summary>Guerrilla's anim_files.xtbl entry for an animation, under our name, added to Armageddon's (or a plain one if Guerrilla's table hasn't it).</summary>
    /// <summary>The animations an anim_files entry's Variants name (each Variant's File).</summary>
    static IEnumerable<string> VariantFiles(XtblElement entry) =>
        (entry.Element("Variants")?.Elements ?? []).Where(v => v.Name.Equals("Variant", StringComparison.OrdinalIgnoreCase))
                                                   .Select(v => v.ValueOf("File")?.Trim()).OfType<string>().Where(f => f.Length > 0);

    /// <param name="names">Guerrilla's animations' names to ours (its variants named by them; one not in it is left out).</param>
    void AddFileEntry(string rfgFile, string ourFile, List<string> log, IReadOnlyDictionary<string, string> names)
    {
        var entry = rfgFileEntries.TryGetValue(rfgFile, out var rfg) ? rfg.Copy() : new XtblElement("Anim_file");
        var animation = entry.Element("Animation") ?? entry.AddElement("Animation");
        if (animation.Element("Filename") is { } filename) filename.Value = ourFile;
        else animation.AddElement("Filename", ourFile);
        if (entry.Element("Variants") is { } variants)
            foreach (var variant in variants.Elements.Where(v => v.Name.Equals("Variant", StringComparison.OrdinalIgnoreCase)).ToList())
            {
                if (variant.Element("File") is not { } file || file.Value.Trim().Length == 0) continue;
                if (names.TryGetValue(file.Value.Trim(), out var ourVariant)) file.Value = ourVariant;
                else variant.Remove();
            }
        // In the vehicle's container, loaded with it (as Armageddon's own vehicles' animations).
        if (animation.Element("Preload") is { } preload) preload.Value = "False";
        else animation.AddElement("Preload", "False");
        foreach (var part in entry.Elements.ToList())
        {
            if (SoundOf != null && part.Name.Equals("Sounds", StringComparison.OrdinalIgnoreCase) && part.HasElements)
            {
                foreach (var sound in part.Elements.ToList())
                {
                    string? cue = sound.ValueOf("Foley")?.Trim();
                    bool looping = sound.ValueOf("Looping")?.Trim().Equals("True", StringComparison.OrdinalIgnoreCase) == true;
                    if (!string.IsNullOrEmpty(cue) && SoundOf(cue, looping) is { } eventName) sound.Element("Foley")!.Value = eventName;
                    else sound.Remove();
                }
                if (!part.Elements.Any()) part.Remove();
                continue;
            }
            bool leftOut = LeftOutOfFiles.Contains(part.Name, StringComparer.OrdinalIgnoreCase);
            if (leftOut && part.HasElements) log.Add($"animation {rfgFile}: its {part.Name} left out until Guerrilla's audio is ported");
            if (leftOut || (!part.HasElements && part.Value.Trim().Length == 0)) part.Remove();
        }
        fileEntries.AddElement(entry);
        ownFiles.Add(ourFile);
    }

    /// <summary>
    /// Writes the sets' tables (new files, in <paramref name="part"/>'s folder of overrides\build\pc\cache: see OverrideLayout), the
    /// lists they change (in <paramref name="modFolder"/>, for guerrilla_world to hand the game; a list
    /// with nothing but Armageddon's in it is deleted there, so the game keeps its own) and this tool's
    /// record of its own entries in them.
    /// </summary>
    public void Write(string overrides, string part, string modFolder)
    {
        AnimTriggerPools.Check(fileList);  // refuses (throws) before anything is written
        Directory.CreateDirectory(modFolder);
        // The set tables an earlier run of this tool wrote that this one doesn't (another tool's are kept).
        foreach (string name in earlierSets)
            if (!setTables.Any(t => t.File.Equals(name, StringComparison.OrdinalIgnoreCase)))
                OverrideLayout.Delete(overrides, name);
        foreach (var (file, data) in setTables) File.WriteAllBytes(OverrideLayout.NewFile(overrides, part, file), data);
        void Put(string name, XtblFile table, bool changed)
        {
            string path = Path.Combine(modFolder, name);
            if (changed) File.WriteAllBytes(path, table.ToBytes());
            else if (File.Exists(path)) File.Delete(path);
        }
        Put(SetList, setList, setTables.Count > 0 || othersInLists);
        Put(FileList, fileList, animations.Count > 0 || othersInLists);
        Put(StateList, stateList, statesChanged);
        Put(ActionList, actionList, actionsChanged);
        string record = OwnEntriesFile(modFolder);
        Directory.CreateDirectory(Path.GetDirectoryName(record)!);
        File.WriteAllLines(record, setTables.Select(t => $"set {t.File}").Concat(ownFiles.Select(f => $"file {f}")));
    }

    /// <summary>
    /// Takes away what <see cref="Write"/> wrote: this tool's sets' tables and its entries in the lists (by
    /// its record; without one, the entries neither Armageddon's nor in another tool's record), each list
    /// kept while another tool's entries are in it, and the record.
    /// </summary>
    public static IEnumerable<string> Remove(string game, string overrides, string modFolder)
    {
        var gameTables = Packfile.Open(Path.Combine(game, "build", "pc", "cache", "misc.vpp_pc"));
        var record = ReadRecord(OwnEntriesFile(modFolder));
        var others = OthersRecords(modFolder);
        var theirSets = new HashSet<string>(XtblFile.FromBytes(Read(gameTables, SetList)).Entries.Select(e => e.ValueOf("Name")?.Trim()).OfType<string>(),
                                            StringComparer.OrdinalIgnoreCase);
        var theirFiles = new HashSet<string>((XtblFile.FromBytes(Read(gameTables, FileList)).Table?.Element("Files")?.Element("Anim_files")?.Elements ?? [])
                                             .Select(AnimationName).OfType<string>(), StringComparer.OrdinalIgnoreCase);
        bool Ours(string name, HashSet<string> armageddons, HashSet<string> recorded, HashSet<string> othersOwn) =>
            record != null ? recorded.Contains(name) : !armageddons.Contains(name) && !othersOwn.Contains(name);
        bool anyOthers = others.Sets.Count > 0 || others.Files.Count > 0;

        string setPath = Path.Combine(modFolder, SetList);
        if (File.Exists(setPath))
        {
            var sets = XtblFile.FromBytes(File.ReadAllBytes(setPath));
            foreach (var e in sets.Entries.ToList())
            {
                string name = e.ValueOf("Name")?.Trim() ?? "";
                if (!Ours(name, theirSets, record?.Sets ?? [], others.Sets)) continue;
                e.Remove();
                foreach (string file in OverrideLayout.Delete(overrides, name)) yield return $"Deleted {file}";
            }
            if (sets.Entries.Any(e => !theirSets.Contains(e.ValueOf("Name")?.Trim() ?? ""))) { File.WriteAllBytes(setPath, sets.ToBytes()); yield return $"{setPath}: our sets taken out, other tools' kept"; }
            else { File.Delete(setPath); yield return $"Deleted {setPath}"; }
        }
        string filePath = Path.Combine(modFolder, FileList);
        if (File.Exists(filePath))
        {
            var files = XtblFile.FromBytes(File.ReadAllBytes(filePath));
            var entries = files.Table?.Element("Files")?.Element("Anim_files");
            foreach (var e in entries?.Elements.ToList() ?? [])
                if (Ours(AnimationName(e) ?? "", theirFiles, record?.Files ?? [], others.Files)) e.Remove();
            if (entries?.Elements.Any(e => !theirFiles.Contains(AnimationName(e) ?? "")) == true) { File.WriteAllBytes(filePath, files.ToBytes()); yield return $"{filePath}: our animations taken out, other tools' kept"; }
            else { File.Delete(filePath); yield return $"Deleted {filePath}"; }
        }
        // The state and action names: kept while another tool's sets may name them.
        foreach (string name in new[] { StateList, ActionList })
        {
            string path = Path.Combine(modFolder, name);
            if (File.Exists(path) && !anyOthers) { File.Delete(path); yield return $"Deleted {path}"; }
        }
        string own = OwnEntriesFile(modFolder);
        if (File.Exists(own)) { File.Delete(own); yield return $"Deleted {own}"; }
    }

    /// <summary>Every element of a name inside another, at any depth, in document order.</summary>
    static IEnumerable<XtblElement> Descendants(XtblElement element, string name)
    {
        foreach (var child in element.Elements)
        {
            if (child.Name.Equals(name, StringComparison.OrdinalIgnoreCase)) yield return child;
            foreach (var inner in Descendants(child, name)) yield return inner;
        }
    }
}
