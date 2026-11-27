using System.Numerics;
using RfaFormats.Meshes;
using RfaFormats.Tables;
using RfaFormats.Textures;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's people made usable in Armageddon, from the player's own copy of Guerrilla
/// (only read): GuerrillaBuilder.exe's people, for the guerrilla_people and guerrilla_world mods. Armageddon's
/// character code is Guerrilla's with fields added: its character reader still reads Guerrilla's
/// fields, and it takes each character's bones from that character's own skeleton. So Guerrilla's
/// people keep their own meshes, skeletons and animations (nothing is moved onto another skeleton).
/// For each character asked for (Guerrilla's character.xtbl entry name):
///   - its body mesh converted (<see cref="GuerrillaCharacterMeshes"/>), its materials on Guerrilla's
///     own shaders' skinned rewrites (<see cref="GuerrillaShaders"/>), packed with its textures (each
///     one of Guerrilla's as a single-texture file under its alias, as the world's and the vehicles')
///     into a container of its own named as the character (&lt;Name&gt;.str2_pc in
///     overrides\build\pc\cache\guerrilla\people\), listed in an override of humans.vpp_pc\character_containers.asm_pc
///     (Armageddon's own list with ours added) as Armageddon lists its people (type 19). Characters
///     whose containers are the same in Guerrilla (its troopers, mercenaries and lieutenants are all
///     greene, file for file) share the first one's, as Armageddon's Shared_Container does (and
///     Guerrilla's own shared_container, its riot-shield elite's);
///   - its skeleton, Guerrilla's file as it is (the same format), a new file the guerrilla_people mod
///     has the game load by name (mods\guerrilla_people\rigs.txt);
///   - its animation set (Male, ...; with the sets it builds on), Guerrilla's own as rfg_&lt;set&gt;,
///     every animation it names as rfgp_&lt;animation&gt; (Armageddon has its own of the same names, for its
///     own skeletons: 137 bones, Guerrilla's 52 to 64; and the vehicles' rfg_ ones are moved onto
///     Armageddon's player), re-packed in Armageddon's key packing with the
///     tracks of the 52 bones every Guerrilla person's skeleton shares in the same order (the bones
///     after them are each skeleton's own: Mason's camera, the EDF's shoulder pads, coats). Guerrilla
///     kept them in its animations archive, loaded once and kept; so are they here, each a file in
///     mods\guerrilla_people\animations\ that the guerrilla_people mod loads and keeps when the game
///     loads its own archive (not in the characters' containers: every set builds on Male, and an
///     animation in two containers belongs to whichever loaded first and goes when it goes).
///     Guerrilla's blend trees (a walk blended between its uphill, level and downhill versions by the
///     ground's slope) have no reader in Armageddon: a state naming one plays its level animation for
///     now. The set lists (anim_set_filenames.xtbl, anim_files.xtbl, anim_states.xtbl, anim_actions.xtbl)
///     are shared with GuerrillaArsenal's vehicles: this starts from the ones already in
///     mods\guerrilla_world\ and replaces only its own entries;
///   - its character.xtbl entry, Guerrilla's own fields, added to Armageddon's table
///     (mods\guerrilla_world\character.xtbl, which guerrilla_world hands the game at start-up). A field
///     Armageddon's description hasn't is left out (Guerrilla's height and its variation: Armageddon's
///     reader no longer reads them); Guerrilla's Anim_Set is Armageddon's Animation_Sets list; a field
///     Armageddon requires that Guerrilla has no counterpart for gets its description's default, or,
///     where it has none, a value named here (<see cref="ArmageddonOnly"/>). Left out for now: the
///     weapons (they come with Guerrilla's guns, GuerrillaArsenal --weapons; never Armageddon's), the
///     voices (Guerrilla's are XACT personas), the props and heads not made yet (the character-mesh
///     props in its container, <see cref="PortProps"/>, and the heads with no morph, <see cref="PortHeads"/>,
///     are made: Mason's coat and face). Its ragdoll is Guerrilla's
///     (<see cref="GuerrillaRagdoll"/>: ragdoll_type, written with the table).
/// The names of the characters made are listed in mods\guerrilla_people\people.txt: in the worlds the
/// guerrilla_people mod plays, it reserves them in the level's spawn resource (where one is in force,
/// as in our worlds, the game makes only the people the resource holds).
/// </summary>
public static class GuerrillaPeople
{
    /// <summary>Guerrilla's civilians (spawn_group_human.xtbl's Default Civilian, the same as its Default Guerilla), in its order.</summary>
    public static readonly string[] Civilians =
    [
        "Bev", "Torvald", "Olga", "Dustin", "John", "Pierce", "Kat", "Celia", "Marie", "Jake", "Blair", "Selma",
        "Blair_Dust", "Chuck_Dust", "Heidi_Dust", "Isaiah_Dust", "Jackson_Dust", "Jake_Dust", "Jamaya_Dust", "Krystal_Dust", "Rachel_Dust", "Selma_Dust",
        "Isaiah", "Jackson", "Celia_Parker", "Chuck_Parker", "Dustin_Parker", "Heidi_Parker", "John_Parker", "Kat_Parker", "Kayla_Parker",
        "Numah_Dust", "Pierce_Parker", "Rachel_Parker", "Chuck", "Jamaya", "Kayla", "Krystal", "Lawrence", "Numah",
        "Blair_Oasis", "Celia_Oasis", "Dustin_Oasis", "Heidi_Oasis", "Ian_Oasis", "Isaiah_Oasis", "Jackson_Oasis", "Jake_Oasis", "Jean_Oasis",
        "John_Oasis", "Kat_Oasis", "Mila_Oasis", "Pierce_Oasis", "Selma_Oasis", "Heidi", "Ian", "Jean", "Mila", "Rachel",
        "Blair_Eos", "Celia_Eos", "Chuck_Eos", "Isaiah_Eos", "Jake_Eos", "Jamaya_Eos", "John_Eos", "Kayla_Eos", "Kat_Eos", "Krystal_Eos",
        "Numah_Eos", "Pierce_Eos", "Selma_Eos",
    ];

    /// <summary>
    /// The characters built when none are named: Guerrilla's EDF (its soldiers, and the faces, helmets and
    /// masks of Braun and May) and its player. (Next: the Marauders, the civilians and guerrillas.)
    /// </summary>
    public static readonly string[] DefaultCharacters =
    [
        "EDF_Trooper", "EDF_Trooper_Tough", "EDF_Merc", "EDF_Lieutenant", "EDF_Lieutenant_Sniper", "EDF_Lieutenant_Enforcer",
        "EDF_Elite", "EDF_Elite_Heavy", "EDF_Elite_Heavy_Tan",
        // The rest of the EDF Guerrilla puts in its world (its Default EDF and EDF_Pilot groups, its squad
        // progression): Braun and May with their faces baked and their helmets and masks; the pilot (May's model);
        // the riot-shield elite (EDF_Elite's container, Guerrilla's own sharing: after it).
        "EDF_Grunt_Braun", "EDF_Officer_May", "EDF_Lieutenant_Pilot", "EDF_Elite_Riot_Shield",
        // The Marauders (its Default Marauder group, its Marauder progression's Eddie) and its SciTech Marauder.
        "Eddie", "Slade", "Muir", "Camie",
        // Its civilians, who are its guerrillas' looks too (its Default Civilian group, in its order; each district's
        // group a part of them), and its Driver group's Kyle.
        .. Civilians,
        "Kyle",
        // Its story people at the safehouses (squads with a special_npc): its special NPCs and its other safehouse
        // VIPs (Kyle is one); guerrilla_people streams one of each in at a time, as Guerrilla's resource manager.
        "Samanya", "OldCoot", "RFCommander", "Carmen", "Jorge",
        PlayerCharacter,
    ];


    /// <summary>
    /// Guerrilla's player, Alex Mason (its character.xtbl's rfg_PC), made the player of our worlds: his own
    /// mesh and skeleton (with the tags Armageddon's player code looks for, <see cref="PlayerTags"/>), a
    /// player container, Armageddon's player fields where Guerrilla has none (from <see cref="DariusCharacter"/>),
    /// the weapons' build's animation set for him (<see cref="PlayerSet"/>), his own spine bend and arms, and
    /// the Guerrilla world's spawn resource's Player_Override. His coat (a prop in Guerrilla) comes with the props,
    /// his face (Guerrilla's base_hires_head dressed with mason_head) with the heads.
    /// </summary>
    public const string PlayerCharacter = "rfg_PC";
    /// <summary>Mason's animation set, made by the weapons' build (GuerrillaPlayerAnimations): his clips on his skeleton.</summary>
    public const string PlayerSet = "rfg_mason.xtbl";
    /// <summary>Armageddon's player (whose player fields Mason's entry takes) and its skeleton (whose tags his gets).</summary>
    const string DariusCharacter = "RF4_Cave", DariusRig = "player_v2.rig_pc";
    /// <summary>The asset list's container types: a person's, and a player's (Armageddon's player suits).</summary>
    const byte CharacterContainerType = 19, PlayerContainerType = 20;
    /// <summary>Armageddon's spine bend and limb tables, written with the player's entries added.</summary>
    public const string SpinebendTable = "spinebend.xtbl", LimbTable = "ik_limbs.xtbl";
    const string PlayerSpinebendName = "rfg_mason";
    /// <summary>The items table the props' items go into (shared with the weapons' build), and our record of them there.</summary>
    public const string ItemTable = "items_3d.xtbl", PropItemList = "prop_items.txt";

    /// <summary>The folder (under mods\guerrilla_people\) the people's animations are written to, which the guerrilla_people mod loads.</summary>
    public const string AnimationFolder = "animations";

    /// <summary>The files this writes: relative to the overrides folder, then to the mods' folders.</summary>
    public const string AssetList = @"build\pc\cache\humans.vpp_pc\character_containers.asm_pc", CharacterTable = "character.xtbl";

    /// <summary>
    /// Armageddon's AI properties (ai_properties.xtbl; a character's ai_prop names one, the default when none) and
    /// ours, <see cref="UnarmedAiProp"/>: its Default less "Auto Equip". Armageddon's NPC set-up (0x8A9260) puts
    /// the best weapon in an NPC's hands when its AI properties have Auto Equip (bit 19 of their flags, +0x58);
    /// Guerrilla never equips a new NPC (only drawing does), so a character of Guerrilla's with no Equip_Item (its
    /// civilians, who carry a gun in their inventory) names ours and is empty-handed until it draws.
    /// </summary>
    public const string AiPropertiesTable = "ai_properties.xtbl", UnarmedAiProp = "rfg_unarmed";
    const string DefaultAiProp = "Default", AutoEquipFlag = "Auto Equip";
    public const string PeopleList = "people.txt", RigList = "rigs.txt", OwnEntries = "anim_entries.txt";
    const string SetList = "anim_set_filenames.xtbl", FileList = "anim_files.xtbl", StateList = "anim_states.xtbl", ActionList = "anim_actions.xtbl";

    /// <summary>
    /// The bones every one of Guerrilla's people's skeletons has, the same and in the same order (its
    /// player's, player.rig_pc): the body and hands, its spine-bend bone and the eyes. Guerrilla plays an
    /// animation made on one person's skeleton on another's; only these bones mean the same in all.
    /// </summary>
    const int SharedBones = 52;

    /// <summary>
    /// What the people's animations are named by: Guerrilla's name after it. Armageddon has animations of
    /// many of the same names (its own people's, on its own skeleton), and GuerrillaArsenal's vehicle
    /// animations are rfg_ + Guerrilla's name where Armageddon has one (moved onto Armageddon's player:
    /// rfg_male_rfl_dwnrd_std is one of both): so not rfg_.
    /// </summary>
    const string AnimationPrefix = "rfgp_";

    /// <summary>
    /// How many animation sets the game holds with the guerrilla_vehicles mod (255; its own table has
    /// 140), and how many set files its set loader's list holds: it keeps their names on its stack, room
    /// for about 90 (0x979C20); one more would write past it.
    /// </summary>
    const int MaxAnimSets = 255, MaxSetFiles = 88;

    /// <summary>
    /// How many animations the game's animation library and its list of animation files hold with the
    /// guerrilla_vehicles mod (8,192 each; its own 4,500 and 4,224).
    /// </summary>
    const int MaxAnimations = 8192;

    /// <summary>The characters the game holds (its table of 165, 0x21B7538): one more is dropped without a word.</summary>
    const int MaxCharacters = 165;

    /// <summary>The fields of an anim_files.xtbl entry left out for now (Guerrilla's sounds are XACT cues), as the vehicles'.</summary>
    static readonly string[] LeftOutOfFiles = ["Sounds", "Voice_Lines", "OldProps"];

    /// <summary>
    /// Guerrilla's fields left out for now (see the summary), whatever Armageddon's description has:
    /// they come with ports of their own. (Heads: the heads that can be made are worn as props instead,
    /// <see cref="PortHeads"/>; Armageddon's own head code isn't used.)
    /// </summary>
    static readonly string[] NotYet = ["Personas", "Last_Equip_Item_Name", "Heads"];

    /// <summary>Guerrilla's teams' names as Armageddon's reader takes them (its own aliases: "EDF" is team 1, "Guerilla" team 0).</summary>
    static string Team(string? team) => string.IsNullOrWhiteSpace(team) ? "Civilian" : team.Trim();

    /// <summary>
    /// The fields Armageddon requires that Guerrilla has nothing for, and its description gives no
    /// default: what kind of model the game takes it for, and its audio switch (Guerrilla's sounds are
    /// another system; until they are ported, the switch of Armageddon's people of the same side), by
    /// Guerrilla's team (and for team Guerilla whether the set is a civilian's).
    /// </summary>
    static (string ModelType, string Audio) ArmageddonOnly(string team, string set) => team.ToLowerInvariant() switch
    {
        "edf" => ("special", "CRUSADER"),
        "marauder" => ("marauder", "MARAUDER"),
        _ when set.StartsWith("civilian", StringComparison.OrdinalIgnoreCase) => ("civilian", "CIVILIAN"),
        _ => ("red faction", "RED_FACTION"),
    };

    /// <summary>
    /// Builds the characters named (Guerrilla's character.xtbl names) into <paramref name="overrides"/>,
    /// <paramref name="worldFolder"/> (mods\guerrilla_world\: the tables) and <paramref name="peopleFolder"/>
    /// (mods\guerrilla_people\: the lists the mod reads).
    /// </summary>
    public static IEnumerable<string> Build(string guerrilla, string game, string overrides, string worldFolder, string peopleFolder, IReadOnlyList<string> characters)
    {
        string guerrillaCache = Path.Combine(guerrilla, "build", "pc", "cache"), gameCache = Path.Combine(game, "build", "pc", "cache");
        var guerrillaTables = Packfile.Open(Path.Combine(guerrillaCache, "misc.vpp_pc"));
        var gameTables = Packfile.Open(Path.Combine(gameCache, "misc.vpp_pc"));
        var guerrillaHumans = Packfile.Open(Path.Combine(guerrillaCache, "humans.vpp_pc"));
        var guerrillaAnims = Packfile.Open(Path.Combine(guerrillaCache, "anims.vpp_pc"));
        var gameHumans = Packfile.Open(Path.Combine(gameCache, "humans.vpp_pc"));
        XtblFile ReadTable(Packfile pack, string name) => XtblFile.FromBytes(pack.Read(pack.Find(name) ?? throw new InvalidDataException($"no {name} in {pack.Name}")));
        // A table we share: the one already written (ours, or another tool's with Armageddon's), else Armageddon's own.
        XtblFile Shared(string name) => File.Exists(Path.Combine(worldFolder, name)) ? XtblFile.FromBytes(File.ReadAllBytes(Path.Combine(worldFolder, name))) : ReadTable(gameTables, name);

        var rfgCharacters = ReadTable(guerrillaTables, CharacterTable);
        var gameCharacters = ReadTable(gameTables, CharacterTable);
        var description = gameCharacters.Description ?? throw new InvalidDataException("Armageddon's character.xtbl has no description");
        var ourCharacters = Shared(CharacterTable);
        var gameCharacterNames = new HashSet<string>(gameCharacters.Entries.Select(XtblFile.EntryName), StringComparer.OrdinalIgnoreCase);
        var sets = new SetPort(guerrillaTables, gameTables, guerrillaAnims, Shared(SetList), Shared(FileList), Shared(StateList), Shared(ActionList),
                               ReadOwn(peopleFolder));
        var assetList = File.Exists(Path.Combine(overrides, AssetList))
            ? AsmFile.Read(File.ReadAllBytes(Path.Combine(overrides, AssetList)))
            : AsmFile.Read(gameHumans.Read(gameHumans.Find("character_containers.asm_pc") ?? throw new InvalidDataException("no character_containers.asm_pc in humans.vpp_pc")));
        var guerrillaPegs = new Lazy<List<PegFile>>(() => guerrillaTables.Entries.Where(e => e.Name.EndsWith(".cpeg_pc", StringComparison.OrdinalIgnoreCase))
            .Select(e => guerrillaTables.Find(PegFile.DataFileName(e.Name)!) is { } data ? PegFile.Read(guerrillaTables.Read(e), guerrillaTables.Read(data)) : null)
            .OfType<PegFile>().ToList());

        // Ours from an earlier run go: their entries, containers and skeletons are made again.
        var oldPeople = File.Exists(Path.Combine(peopleFolder, PeopleList)) ? File.ReadAllLines(Path.Combine(peopleFolder, PeopleList)).Select(l => l.Trim()).Where(l => l.Length > 0).ToList() : [];
        foreach (var old in ourCharacters.Entries.Where(e => oldPeople.Contains(XtblFile.EntryName(e), StringComparer.OrdinalIgnoreCase)).ToList())
            ourCharacters.RemoveEntry(old);
        assetList.Containers.RemoveAll(c => oldPeople.Contains(c.Name, StringComparer.OrdinalIgnoreCase));

        var entries = new Dictionary<string, XtblElement>(StringComparer.OrdinalIgnoreCase);
        foreach (var e in rfgCharacters.Entries) entries.TryAdd(XtblFile.EntryName(e), e);
        var containers = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
        var containerOf = new Dictionary<string, string>(StringComparer.Ordinal);  // a Guerrilla container's contents → the character made with it
        // The props: Guerrilla's items, Armageddon's item names and description, ours made, and each container's props.
        var rfgItems = new Dictionary<string, XtblElement>(StringComparer.OrdinalIgnoreCase);
        foreach (var e in ReadTable(guerrillaTables, ItemTable).Entries) rfgItems.TryAdd(XtblFile.EntryName(e), e);
        var gameItemTable = ReadTable(gameTables, ItemTable);
        var gameItemNames = new HashSet<string>(gameItemTable.Entries.Select(XtblFile.EntryName), StringComparer.OrdinalIgnoreCase);
        var itemDescription = gameItemTable.Description ?? throw new InvalidDataException($"Armageddon's {ItemTable} has no description");
        var propItems = new Dictionary<string, XtblElement>(StringComparer.OrdinalIgnoreCase);
        var propsOf = new Dictionary<string, HashSet<string>>(StringComparer.OrdinalIgnoreCase);  // a container's owner → the props made into it
        var headsOf = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);    // and the heads (their items)
        var guerrillaItems = new Lazy<Packfile>(() => Packfile.Open(Path.Combine(guerrillaCache, "items.vpp_pc")));
        // The props' and heads' textures: misc's (always_loaded: the eyes, eyelashes), and items.vpp_pc's own
        // (items_containers: helmets, masks, backpacks), after the character's container's.
        var propPegs = new Lazy<List<PegFile>>(() =>
        [
            .. guerrillaPegs.Value,
            .. guerrillaItems.Value.Entries.Where(e => e.Name.EndsWith(".cpeg_pc", StringComparison.OrdinalIgnoreCase))
                .Select(e => guerrillaItems.Value.Find(PegFile.DataFileName(e.Name)!) is { } data ? PegFile.Read(guerrillaItems.Value.Read(e), guerrillaItems.Value.Read(data)) : null)
                .OfType<PegFile>(),
        ]);
        var ownerOf = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);  // a character made → whose container it has
        var rigs = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
        var made = new List<string>();
        // Guerrilla's inventory items ported so far (GuerrillaWeapons): Guerrilla's name → ours.
        var gameItems = new HashSet<string>(ReadTable(gameTables, "items_inventory.xtbl").Entries.Select(XtblFile.EntryName), StringComparer.OrdinalIgnoreCase);
        string itemsFile = Path.Combine(worldFolder, "items_inventory.xtbl");
        var portedItems = File.Exists(itemsFile)
            ? XtblFile.FromBytes(File.ReadAllBytes(itemsFile)).Entries.Select(XtblFile.EntryName).Where(n => !gameItems.Contains(n)).ToHashSet(StringComparer.OrdinalIgnoreCase)
            : [];
        string? Item(string guerrilla) => portedItems.Contains(guerrilla) ? guerrilla : portedItems.Contains("rfg_" + guerrilla) ? "rfg_" + guerrilla : null;

        foreach (string name in characters)
        {
            if (!entries.TryGetValue(name, out var entry)) { yield return $"{name}: Guerrilla's character.xtbl has no such character"; continue; }
            if (gameCharacterNames.Contains(name)) { yield return $"{name}: Armageddon has a character of that name already; left out"; continue; }
            if (gameHumans.Find(name + ".str2_pc") != null) { yield return $"{name}: Armageddon has a container of that name already; left out"; continue; }
            if (ourCharacters.Entries.Count() >= MaxCharacters) { yield return $"{name}: left out, the game holds {MaxCharacters} characters and the table is full"; continue; }
            string? model = entry.Element("Model")?.ValueOf("Filename")?.Trim();
            string? rigFile = entry.Element("Rig")?.ValueOf("Filename")?.Trim();
            string? set = entry.ValueOf("Anim_Set")?.Trim();
            if (string.IsNullOrEmpty(model) || string.IsNullOrEmpty(rigFile) || string.IsNullOrEmpty(set)) { yield return $"{name}: no model, skeleton or animation set named; left out"; continue; }
            string mesh = Path.GetFileNameWithoutExtension(model);
            string rig = Path.ChangeExtension(Path.GetFileName(rigFile), ".rig_pc").ToLowerInvariant();

            // Its skeleton: Guerrilla's file (Armageddon has none of the same name).
            if (gameTables.Find(rig) != null) { yield return $"{name}: Armageddon has a skeleton {rig} of its own; left out (its bones would be Armageddon's)"; continue; }
            if (guerrillaTables.Find(rig) is not { } rfgRig) { yield return $"{name}: Guerrilla has no skeleton {rig}; left out"; continue; }
            byte[] rigBytes = guerrillaTables.Read(rfgRig);
            var skeleton = Rig.Read(rigBytes);
            if (skeleton.Bones.Count < SharedBones) { yield return $"{name}: its skeleton has {skeleton.Bones.Count} bones, not a person's ({SharedBones} or more); left out"; continue; }
            bool isPlayer = name.Equals(PlayerCharacter, StringComparison.OrdinalIgnoreCase);
            if (isPlayer)
            {
                // The tags Armageddon's player code looks for on its player (see PlayerTags).
                if (gameTables.Find(DariusRig) is not { } dariusRig) { yield return $"{name}: Armageddon has no {DariusRig} to take its player's tags from; left out"; continue; }
                var tagLog = new List<string>();
                rigBytes = PlayerTags(rigBytes, skeleton, Rig.Read(gameTables.Read(dariusRig)), tagLog);
                // Darius's camera-lookat bone, which the player camera turns toward in Armageddon's moves
                // (the gear session's PlayerRetarget keeps its track in the moves it moves onto Mason).
                rigBytes = PlayerRetarget.WithCameraLookat(rigBytes, gameTables.Read(dariusRig));
                skeleton = Rig.Read(rigBytes);
                foreach (string line in tagLog) yield return $"{name}: {line}";
            }

            // Its container: one Guerrilla shares (its shared_container), one the same as a character's
            // made before it, or its own: its model and textures from its container in Guerrilla's humans.vpp_pc.
            string? sharedWith = entry.ValueOf("shared_container")?.Trim() is { Length: > 0 } guerrillas ? guerrillas : null;
            if (sharedWith != null)
            {
                if (!ownerOf.TryGetValue(sharedWith, out var owner))
                { yield return $"{name}: shares {sharedWith}'s container in Guerrilla, which isn't made (name {sharedWith} before it); left out"; continue; }
                sharedWith = owner;
            }
            (string Name, byte[] Data, List<(string Name, byte[] Data)> Files, string Line)? own = null;
            string? sameAs = null, ownKey = null;
            if (sharedWith == null)
            {
                var source = guerrillaHumans.Find(name + ".str2_pc");
                if (source == null) { yield return $"{name}: Guerrilla's humans.vpp_pc has no {name}.str2_pc"; continue; }
                var pack = guerrillaHumans.OpenContainer(source);
                string key = ContentKey(pack);
                if (containerOf.TryGetValue(key, out var same)) sharedWith = sameAs = same;
                else
                {
                    var cEntry = pack.Find(mesh + ".ccmesh_pc");
                    var gEntry = pack.Find(mesh + ".gcmesh_pc");
                    if (cEntry == null || gEntry == null) { yield return $"{name}: its container has no {mesh}.ccmesh_pc and .gcmesh_pc"; continue; }
                    byte[] cFile = pack.Read(cEntry), gFile = pack.Read(gEntry);
                    if (GuerrillaCharacterMeshes.NotYet(cFile) is { } why) { yield return $"{name}: its model {mesh}: {why}; left out"; continue; }
                    var notSkinned = new List<string>();
                    var (header, textures) = GuerrillaCharacterMeshes.Convert(cFile, GuerrillaVehicles.Alias, notSkinned);
                    if (notSkinned.Count > 0)
                        yield return $"{name}: its model {mesh}'s materials the skinned shaders can't draw yet (drawn by Armageddon's, wrongly): {string.Join("; ", notSkinned)}";
                    var files = new List<(string Name, byte[] Data)> { (mesh + ".ccmesh_pc", header), (mesh + ".gcmesh_pc", gFile) };
                    var pegs = pack.Entries.Where(e => e.Name.EndsWith(".cpeg_pc", StringComparison.OrdinalIgnoreCase))
                                   .Select(e => PegFile.Read(pack.Read(e), pack.Read(pack.Find(PegFile.DataFileName(e.Name)!)!))).ToList();
                    var missing = new List<string>();
                    foreach (string texture in textures)
                    {
                        string alias = GuerrillaVehicles.Alias(texture);
                        var texFiles = GuerrillaBuiltIns.Files(texture, alias) ?? Single(pegs, texture, alias) ?? Single(guerrillaPegs.Value, texture, alias);
                        if (texFiles == null) missing.Add(texture);
                        else files.AddRange(texFiles);
                    }
                    // Its props: Guerrilla's character-mesh props whose files are in its container (Mason's coat, ...).
                    var propLog = new List<string>();
                    propsOf[name] = PortProps(entry, pack, pegs, propPegs, guerrillaTables, gameTables, guerrillaItems, rfgItems, gameItemNames,
                                              itemDescription, files, rigs, propItems, propLog);
                    // Its heads, as props too (Mason's face).
                    headsOf[name] = PortHeads(entry, pack, pegs, propPegs, guerrillaTables, gameTables, guerrillaItems, rfgItems, gameItemNames,
                                              itemDescription, files, rigs, propItems, propLog);
                    foreach (string line in propLog) yield return $"{name}: {line}";
                    byte[] container = Packfile.CreateContainer(files);
                    own = (name, container, files,
                           $"model {mesh} ({cFile.Length:N0} → {header.Length:N0} bytes, {skeleton.Bones.Count} bones), {textures.Count - missing.Count} textures" +
                           (missing.Count > 0 ? $" ({missing.Count} not found: {string.Join(", ", missing)})" : "") +
                           $"; container {container.Length / 1024.0 / 1024:0.0} MB");
                    ownKey = key;
                }
            }

            // Its animation set (and the ones it builds on), with every animation. The player's is the
            // weapons' build's rfg_mason (Guerrilla's player clips on his skeleton, and Armageddon's player
            // moves carried onto it), which must be listed already.
            PortedSet? ported;
            if (isPlayer)
            {
                if (!sets.Listed(PlayerSet)) { yield return $"{name}: the animation set {PlayerSet} isn't in the animation lists (build the weapons first: it comes with them); left out"; continue; }
                ported = new PortedSet(Path.GetFileNameWithoutExtension(PlayerSet), PlayerSet, []);
            }
            else
            {
                var setLog = new List<string>();
                ported = sets.Port(Path.GetFileNameWithoutExtension(set), setLog);
                foreach (string line in setLog) yield return $"{name}: {line}";
                if (ported == null) { yield return $"{name}: its animation set {set} couldn't be ported; left out"; continue; }
            }

            if (own is { } c)
            {
                containers[c.Name + ".str2_pc"] = c.Data;
                assetList.Containers.RemoveAll(a => a.Name.Equals(c.Name, StringComparison.OrdinalIgnoreCase));
                assetList.Containers.Add(Entry(c.Name, c.Data, c.Files, isPlayer ? PlayerContainerType : CharacterContainerType));
            }
            rigs[rig] = rigBytes;
            yield return $"{name}: " + (own is { } o ? o.Line : sameAs != null ? $"the same container as {sameAs} in Guerrilla, file for file: shares it" : $"shares {sharedWith}'s container (Guerrilla's own sharing)") +
                         $", skeleton {rig} ({skeleton.Bones.Count} bones), animation set {ported.Set}" + (isPlayer ? " (the weapons' build's)" : $" ({ported.Animations.Count} animations)");

            // Its table entry.
            var log = new List<string>();
            var ours = CharacterEntry(entry, description, ported.SetFile, rig, Item, sharedWith, log,
                                      isPlayer ? gameCharacters.Entries.FirstOrDefault(e => XtblFile.EntryName(e).Equals(DariusCharacter, StringComparison.OrdinalIgnoreCase)) : null,
                                      propsOf.GetValueOrDefault(sharedWith ?? name), headsOf.GetValueOrDefault(sharedWith ?? name));
            // Its footsteps: Guerrilla's entry, which the world's build adds under our name for it (GuerrillaFootsteps).
            if (ours.Element("FootstepsControl") is { } footsteps && footsteps.Value.Trim().Length > 0)
                footsteps.Value = GuerrillaFootsteps.NameFor(footsteps.Value.Trim(), worldFolder);
            ourCharacters.Table!.AddElement(ours);
            foreach (string line in log) yield return $"{name}: {line}";
            made.Add(name);
            ownerOf[name] = sharedWith ?? name;
            if (ownKey != null) containerOf[ownKey] = name;
        }

        // Guerrilla's squads, with the people made so far (and from earlier runs: every character of ours in the table).
        var ourPeople = new HashSet<string>(ourCharacters.Entries.Select(XtblFile.EntryName).Where(n => !gameCharacterNames.Contains(n) && !n.Equals(PlayerCharacter, StringComparison.OrdinalIgnoreCase)),
                                            StringComparer.OrdinalIgnoreCase);
        bool playerMade = made.Contains(PlayerCharacter, StringComparer.OrdinalIgnoreCase);
        // Each one's container: its own, or the one it shares (Shared_Container), for the spawn resource.
        var containerNameOf = ourCharacters.Entries.Where(e => ourPeople.Contains(XtblFile.EntryName(e)))
            .ToDictionary(XtblFile.EntryName, e => e.ValueOf("Shared_Container")?.Trim() is { Length: > 0 } shared ? shared : XtblFile.EntryName(e), StringComparer.OrdinalIgnoreCase);
        foreach (string line in GuerrillaSquads.Build(guerrillaTables, gameTables, worldFolder, ourPeople, containerNameOf,
                                                                     c => entries.TryGetValue(c, out var e) ? e.ValueOf("Team")?.Trim() : null, playerMade ? PlayerCharacter : null)) yield return line;
        // The vehicles' crews (GuerrillaDistricts.CrewLines): who can crew a vehicle where, for the vehicles' code.
        {
            string? TeamOf(string c) => entries.TryGetValue(c, out var e) ? e.ValueOf("Team")?.Trim() : null;
            var crewResources = GuerrillaDistricts.Resources(GuerrillaDistricts.Groups(guerrillaTables),
                                                             GuerrillaDistricts.DistrictGroups(GuerrillaDistricts.AmbientGroups(guerrillaTables).Values),
                                                             ourPeople, containerNameOf, TeamOf, out var crewKept);
            var crewLines = GuerrillaDistricts.CrewLines(crewResources, TeamOf, crewKept);
            Directory.CreateDirectory(peopleFolder);
            File.WriteAllLines(Path.Combine(peopleFolder, GuerrillaDistricts.CrewList), crewLines);
            yield return $"Crews: {crewLines.Count - 1} lines in {GuerrillaDistricts.CrewList} (each resource's people by team, for the vehicles' crews)";
            // Where Guerrilla's people walk (GuerrillaAmbient): each zone's walkers by day and night and their resource.
            yield return GuerrillaAmbient.Write(guerrillaCache, guerrillaTables, peopleFolder);
            yield return GuerrillaActions.Write(guerrillaCache, guerrillaTables, peopleFolder);
        }
        // The player's spine bend and arms (Armageddon's tables name Darius's bones), added to what is written there
        // already (the vehicles build adds its walkers' legs to the limb table), ours from an earlier run replaced.
        {
            var spinebend = SharedTableWithoutOurs(worldFolder, gameTables, SpinebendTable);
            var limbs = SharedTableWithoutOurs(worldFolder, gameTables, LimbTable);
            if (playerMade)
            {
                spinebend = PlayerSpinebend(spinebend);
                limbs = PlayerLimbs(limbs);
            }
            Directory.CreateDirectory(worldFolder);
            foreach (var (table, written) in new[] { (SpinebendTable, spinebend), (LimbTable, limbs) })
                if (playerMade || File.Exists(Path.Combine(worldFolder, table))) File.WriteAllBytes(Path.Combine(worldFolder, table), written.ToBytes());
            if (playerMade)
                yield return $"Player: {PlayerCharacter} (Mason), the Guerrilla world's player; his spine bend ({PlayerSpinebendName}) in {SpinebendTable}, his arms in {LimbTable}";
        }
        // The props' items, added to the items table already written (the weapons' build's, or Armageddon's
        // own), ours from an earlier run taken out first (mods\guerrilla_people\prop_items.txt).
        {
            string itemsPath = Path.Combine(worldFolder, ItemTable), recordPath = Path.Combine(peopleFolder, PropItemList);
            var itemsTable = File.Exists(itemsPath) ? XtblFile.FromBytes(File.ReadAllBytes(itemsPath)) : ReadTable(gameTables, ItemTable);
            var earlier = File.Exists(recordPath) ? File.ReadAllLines(recordPath).Select(l => l.Trim()).Where(l => l.Length > 0).ToHashSet(StringComparer.OrdinalIgnoreCase) : [];
            foreach (var old in itemsTable.Entries.Where(e => earlier.Contains(XtblFile.EntryName(e))).ToList()) itemsTable.RemoveEntry(old);
            foreach (var item in propItems.Values) itemsTable.Table!.AddElement(item);
            if (propItems.Count > 0 || earlier.Count > 0)
            {
                Directory.CreateDirectory(worldFolder);
                Directory.CreateDirectory(peopleFolder);
                File.WriteAllBytes(itemsPath, itemsTable.ToBytes());
                File.WriteAllLines(recordPath, propItems.Keys.Order(StringComparer.OrdinalIgnoreCase));
            }
            if (propItems.Count > 0) yield return $"Props: {propItems.Count} of Guerrilla's items added to {ItemTable} ({string.Join(", ", propItems.Keys)})";
        }

        foreach (var (file, bytes) in containers) File.WriteAllBytes(OverrideLayout.NewFile(overrides, OverrideLayout.People, file), bytes);
        // Containers of an earlier run's characters with none of their own now.
        foreach (string old in oldPeople.Where(o => !containers.ContainsKey(o + ".str2_pc")))
            OverrideLayout.Delete(overrides, old + ".str2_pc");
        string listFile = Path.Combine(overrides, AssetList);
        Directory.CreateDirectory(Path.GetDirectoryName(listFile)!);
        File.WriteAllBytes(listFile, assetList.ToBytes());
        Directory.CreateDirectory(worldFolder);
        File.WriteAllBytes(Path.Combine(worldFolder, CharacterTable), ourCharacters.ToBytes());
        // Our AI properties with Armageddon's (guerrilla_world hands the table over).
        {
            var aiProps = ReadTable(gameTables, AiPropertiesTable);
            var defaultProps = aiProps.Entries.FirstOrDefault(e => XtblFile.EntryName(e).Equals(DefaultAiProp, StringComparison.OrdinalIgnoreCase))
                ?? throw new InvalidDataException($"Armageddon's {AiPropertiesTable} has no {DefaultAiProp} entry");
            var unarmed = aiProps.CopyEntry(defaultProps, UnarmedAiProp);
            foreach (var flag in (unarmed.Element("ai_flags")?.Elements ?? []).Where(f => f.Value.Trim().Equals(AutoEquipFlag, StringComparison.OrdinalIgnoreCase)).ToList())
                flag.Remove();
            File.WriteAllBytes(Path.Combine(worldFolder, AiPropertiesTable), aiProps.ToBytes());
            yield return $"AI properties: {UnarmedAiProp} added to {AiPropertiesTable} (Armageddon's {DefaultAiProp} less {AutoEquipFlag}), named by " +
                         $"{ourCharacters.Entries.Count(e => e.ValueOf("ai_prop")?.Trim() == UnarmedAiProp)} of our characters (those Guerrilla gives no Equip_Item)";
        }
        // Guerrilla's human ragdoll, which the people's entries name (guerrilla_world hands it over).
        File.WriteAllBytes(Path.Combine(worldFolder, GuerrillaRagdoll.FileName), GuerrillaRagdoll.Table());
        foreach (string line in sets.Write(overrides, worldFolder, peopleFolder)) yield return line;
        foreach (var (file, bytes) in rigs) File.WriteAllBytes(OverrideLayout.NewFile(overrides, OverrideLayout.People, file), bytes);
        Directory.CreateDirectory(peopleFolder);
        File.WriteAllLines(Path.Combine(peopleFolder, RigList), rigs.Keys.Order(StringComparer.OrdinalIgnoreCase));
        File.WriteAllLines(Path.Combine(peopleFolder, PeopleList), made);
        // Guerrilla's material shaders' skinned rewrites, which the people's materials name.
        var shaders = GuerrillaShaders.WriteSkinned(gameCache, overrides);
        yield return $"Shaders: {shaders.Count} skinned effect files of Guerrilla's material shaders";
        yield return $"Characters: {ourCharacters.Entries.Count()} of the {MaxCharacters} the game holds";
        yield return made.Count == 0 ? "No characters built." : $"People: {string.Join(", ", made)}";
    }

    /// <summary>Takes away everything <see cref="Build"/> wrote.</summary>
    public static IEnumerable<string> Remove(string game, string overrides, string worldFolder, string peopleFolder)
    {
        var gameTables = Packfile.Open(Path.Combine(game, "build", "pc", "cache", "misc.vpp_pc"));
        var own = ReadOwn(peopleFolder);
        // Our entries out of the shared animation lists (each kept if another tool's are still in it).
        foreach (var (table, kind) in new[] { (SetList, "set"), (FileList, "file") })
        {
            string path = Path.Combine(worldFolder, table);
            if (!File.Exists(path)) continue;
            var x = XtblFile.FromBytes(File.ReadAllBytes(path));
            int before = RemoveOwn(x, table, own);
            if (before > 0) { File.WriteAllBytes(path, x.ToBytes()); yield return $"{table}: {before} of ours taken out"; }
        }
        foreach (string set in own.Where(o => o.Kind == "set").Select(o => o.Name))
            foreach (string file in OverrideLayout.Delete(overrides, set)) yield return $"Deleted {file}";
        string animationFolder = Path.Combine(peopleFolder, AnimationFolder);
        if (Directory.Exists(animationFolder))
        {
            int count = 0;
            foreach (string file in Directory.EnumerateFiles(animationFolder, "*.anim_pc").ToList()) { File.Delete(file); count++; }
            if (!Directory.EnumerateFileSystemEntries(animationFolder).Any()) Directory.Delete(animationFolder);
            yield return $"Deleted {count} animations from {animationFolder}";
        }
        foreach (string list in new[] { PeopleList, RigList })
        {
            string path = Path.Combine(peopleFolder, list);
            if (!File.Exists(path)) continue;
            foreach (string line in File.ReadAllLines(path).Select(l => l.Trim()).Where(l => l.Length > 0))
                foreach (string file in OverrideLayout.Delete(overrides, list == PeopleList ? line + ".str2_pc" : line)) yield return $"Deleted {file}";
            File.Delete(path);
            yield return $"Deleted {path}";
        }
        // Our props' items out of the shared items table (the weapons' stay).
        string itemsPath = Path.Combine(worldFolder, ItemTable), recordPath = Path.Combine(peopleFolder, PropItemList);
        if (File.Exists(itemsPath) && File.Exists(recordPath))
        {
            var ourItems = File.ReadAllLines(recordPath).Select(l => l.Trim()).Where(l => l.Length > 0).ToHashSet(StringComparer.OrdinalIgnoreCase);
            var itemsTable = XtblFile.FromBytes(File.ReadAllBytes(itemsPath));
            int removed = 0;
            foreach (var old in itemsTable.Entries.Where(e => ourItems.Contains(XtblFile.EntryName(e))).ToList()) { itemsTable.RemoveEntry(old); removed++; }
            File.WriteAllBytes(itemsPath, itemsTable.ToBytes());
            File.Delete(recordPath);
            yield return $"{ItemTable}: {removed} of our props' items taken out";
        }
        foreach (string path in new[] { Path.Combine(overrides, AssetList), Path.Combine(worldFolder, CharacterTable), Path.Combine(worldFolder, GuerrillaRagdoll.FileName),
                                        Path.Combine(worldFolder, AiPropertiesTable),
                                        Path.Combine(peopleFolder, OwnEntries), Path.Combine(peopleFolder, GuerrillaDistricts.CrewList),
                                        Path.Combine(peopleFolder, GuerrillaAmbient.File), Path.Combine(peopleFolder, GuerrillaActions.File) })
            if (File.Exists(path)) { File.Delete(path); yield return $"Deleted {path}"; }
        // The spine bend and limb tables: our rows out; the file goes only if no other build's rows are left in it.
        foreach (string table in new[] { SpinebendTable, LimbTable })
        {
            string path = Path.Combine(worldFolder, table);
            if (!File.Exists(path)) continue;
            var left = SharedTableWithoutOurs(worldFolder, gameTables, table);
            var armageddons = XtblFile.FromBytes(ArmageddonsTable(gameTables, table)).Entries.Select(XtblFile.EntryName).ToHashSet(StringComparer.OrdinalIgnoreCase);
            if (left.Entries.All(e => armageddons.Contains(XtblFile.EntryName(e)))) { File.Delete(path); yield return $"Deleted {path}"; }
            else { File.WriteAllBytes(path, left.ToBytes()); yield return $"{table}: Mason's rows taken out (another build's are kept)"; }
        }
        foreach (string line in GuerrillaSquads.Remove(worldFolder)) yield return line;
    }

    /// <summary>
    /// Guerrilla's character entry for Armageddon (see the summary): its fields as they are, its animation
    /// set as ours, its skeleton by the name it is loaded under; the fields left out for now
    /// (<see cref="NotYet"/>) and those Armageddon's description hasn't taken away, the required ones
    /// Armageddon has a default for filled, the others named here.
    /// </summary>
    static XtblElement CharacterEntry(XtblElement rfg, FieldDescription description, string setFile, string rig, Func<string, string?> item,
                                      string? sharedContainer, List<string> log, XtblElement? player = null, IReadOnlySet<string>? props = null,
                                      IReadOnlyList<string>? heads = null)
    {
        var copy = rfg.Copy();
        // Its heads made into its container (PortHeads), of those its Heads name: worn as a prop, one picked at random as Guerrilla does.
        var ownHeads = (heads ?? []).Where(h => rfg.Element("Heads")?.Elements.Any(e => (HeadPrefix + e.ValueOf("Name")?.Trim()).Equals(h, StringComparison.OrdinalIgnoreCase)) == true).ToList();
        // Its props: only the items made into its container (a listed item whose mesh isn't there stops the game);
        // a prop with none of its items left goes.
        if (copy.Element("Props") is { } propList)
        {
            var unmade = new List<string>();
            void Keep(XtblElement? list)
            {
                foreach (var held in list?.Elements.ToList() ?? [])
                {
                    if (props == null || !props.Contains(held.ValueOf("Name")?.Trim() ?? "")) { unmade.Add(held.ValueOf("Name")?.Trim() ?? "?"); held.Remove(); continue; }
                    Keep(held.Element("Sub_Items"));
                    if (held.Element("Sub_Items") is { } subs && !subs.Elements.Any()) subs.Remove();
                }
            }
            var retyped = new List<string>();
            foreach (var prop in propList.Elements.ToList())
            {
                Keep(prop.Element("Items"));
                if (prop.Element("Items")?.Elements.Any() != true) { prop.Remove(); continue; }
                // Armageddon passes over a prop of type Back in a Wrecking Crew match, which our worlds are (0x511930):
                // Guerrilla's are worn, so they take another type (a type only names the prop for lookups).
                if (prop.Element("Type") is { } type && type.Value.Trim().Equals(SkippedPropType, StringComparison.OrdinalIgnoreCase))
                {
                    type.Value = WornPropType;
                    retyped.Add(prop.ValueOf("Prop_Name")?.Trim() ?? "?");
                }
            }
            if (!propList.Elements.Any()) propList.Remove();
            if (unmade.Count > 0) log.Add($"props not made yet, left out of its entry: {string.Join(", ", unmade.Distinct())}");
            if (retyped.Count > 0) log.Add($"props of type {SkippedPropType} (skipped in our worlds) as {WornPropType}: {string.Join(", ", retyped)}");
        }
        if (ownHeads.Count > 0)
        {
            var headProp = (copy.Element("Props") ?? copy.AddElement("Props")).AddElement("Prop");
            headProp.AddElement("Prop_Name", "Head");
            headProp.AddElement("Chance", "100");
            headProp.AddElement("Type", HeadPropType);
            var headItems = headProp.AddElement("Items");
            for (int k = 0; k < ownHeads.Count; k++)
            {
                var held = headItems.AddElement("Item");
                held.AddElement("Name", ownHeads[k]);
                held.AddElement("Chance", (100 / ownHeads.Count + (k == 0 ? 100 % ownHeads.Count : 0)).ToString());
            }
            log.Add($"its head{(ownHeads.Count > 1 ? "s" : "")} worn as a {HeadPropType} prop: {string.Join(", ", ownHeads)}");
        }
        // Its container: its own (named as it is), or another character's (Armageddon's Shared_Container).
        copy.Element("Shared_Container")?.Remove();
        // Its inventory: Guerrilla's items that have been ported, under their names here (never Armageddon's own).
        var inventory = new XtblElement("Inventory");
        var notPorted = new List<string>();
        foreach (var held in rfg.Element("Inventory")?.Elements ?? [])
        {
            string name = held.ValueOf("Item_Name")?.Trim() ?? "";
            if (item(name) is not { } ours) { notPorted.Add(name); continue; }
            var copied = held.Copy();
            copied.Element("Item_Name")!.Value = ours;
            inventory.AddElement(copied);
        }
        string? equip = rfg.ValueOf("Equip_Item_Name")?.Trim() is { Length: > 0 } e ? item(e) : null;
        copy.Element("Inventory")?.Remove();
        copy.Element("Equip_Item_Name")?.Remove();
        if (notPorted.Count > 0) log.Add($"its items not ported yet (with Guerrilla's weapons): {string.Join(", ", notPorted)}");
        string team = Team(copy.ValueOf("Team"));
        string rfgSet = copy.ValueOf("Anim_Set")?.Trim() ?? "";
        var leftOut = new List<string>();
        foreach (string field in NotYet)
            if (copy.Element(field) is { } part)
            {
                // (Its Heads go whichever way: the ones made are worn as props, above.)
                bool worn = field == "Heads" && ownHeads.Count > 0 && ownHeads.Count == part.Elements.Count();
                if ((part.HasElements || part.Value.Trim().Length > 0) && !worn) leftOut.Add(field);
                part.Remove();
            }
        if (copy.Element("Height") is { } height && copy.ValueOf("Height")?.Trim() is { } h && h != "1" && h != "1.0")
            log.Add($"its height {h} (× Guerrilla's variation {copy.ValueOf("Variation")?.Trim() ?? "0"}): Armageddon's reader no longer reads it; it stands at its model's own size until that is ported");
        var dropped = new List<string>();
        Prune(copy, description.Children, "", dropped);
        Set(copy, "Team", team);
        if (copy.Element("Rig") is { } rigElement)
            if (rigElement.Element("Filename") is { } filename) filename.Value = Path.ChangeExtension(rig, ".rigx");
        var sets = copy.AddElement("Animation_Sets").AddElement("Animation_Set");
        sets.AddElement("Animation_Set", setFile);
        sets.AddElement("Chance", "1");
        if (sharedContainer != null) copy.AddElement("Shared_Container", sharedContainer);
        copy.AddElement("Personas");
        copy.AddElement(inventory);
        if (equip != null) copy.AddElement("Equip_Item_Name", equip);
        // No equip item in Guerrilla's entry: empty-handed until it draws (see UnarmedAiProp).
        if (rfg.ValueOf("Equip_Item_Name")?.Trim() is not { Length: > 0 }) Set(copy, "ai_prop", UnarmedAiProp);
        // Unarmed while none of its weapons is ported (never one of Armageddon's); no spine bend of
        // Armageddon's (its table names Darius's bones) until Guerrilla's is. Its ragdoll Guerrilla's
        // (GuerrillaRagdoll): without one, Armageddon hides a dead body at once.
        var flags = copy.Element("Flags") ?? copy.AddElement("Flags");
        foreach (var never in flags.Elements.Where(f => f.Value.Trim().Equals("never ragdoll", StringComparison.OrdinalIgnoreCase)).ToList()) never.Remove();
        copy.Element("ragdoll_type")?.Remove();
        copy.AddElement("ragdoll_type").AddElement("Filename", GuerrillaRagdoll.FileName);
        // (The player: armed by the game's own inventory, his own spine bend, and Armageddon's player's flags.)
        string[] ourFlags = player != null ? [.. player.Element("Flags")?.Elements.Select(f => f.Value.Trim()) ?? []]
                          : equip == null ? ["unarmed", "no spinebend"] : ["no spinebend"];
        foreach (string flag in ourFlags)
            if (!flags.Elements.Any(f => f.Value.Trim().Equals(flag, StringComparison.OrdinalIgnoreCase))) flags.AddElement("Flag", flag);
        if (equip != null) log.Add($"armed with Guerrilla's own {equip} (GuerrillaArsenal's)");
        var (modelType, audio) = ArmageddonOnly(team, rfgSet);
        Set(copy, "ModelType", modelType);
        Set(copy, "Audio_Character_Type", audio);
        Set(copy, "Controller_Type", "Human NPC");
        Set(copy, "Effect_material_type", "Flesh");
        var fromPlayer = new List<string>();
        if (player != null)
        {
            // Armageddon's player's fields where Guerrilla's entry has none (the controller, container, blend
            // tree, indicators, the Nano Forge's light, ...): Guerrilla's own values stay where it has them.
            foreach (var field in player.Elements.Where(f => !NotFromPlayer.Contains(f.Name, StringComparer.OrdinalIgnoreCase) && rfg.Element(f.Name) == null).ToList())
            {
                copy.Element(field.Name)?.Remove();
                copy.AddElement(field.Copy());
                fromPlayer.Add(field.Name);
            }
            Set(copy, "Spinebend", PlayerSpinebendName);
            (modelType, audio) = (copy.ValueOf("ModelType")?.Trim() ?? modelType, copy.ValueOf("Audio_Character_Type")?.Trim() ?? audio);
        }
        var added = new List<string>();
        var unfilled = new List<string>();
        FillRequired(copy, description.Children, "", added, unfilled);
        if (leftOut.Count > 0) log.Add($"left out for now (each comes with its own port): {string.Join(", ", leftOut)}");
        if (dropped.Count > 0) log.Add($"left out of its entry (Armageddon's table hasn't them): {string.Join(", ", dropped)}");
        if (player != null) log.Add($"the player: Armageddon's player's fields where Guerrilla has none ({XtblFile.EntryName(player)}'s): {string.Join(", ", fromPlayer)}; flags {string.Join(", ", ourFlags)}; spine bend {PlayerSpinebendName}");
        else log.Add($"Armageddon's fields with no Guerrilla counterpart: model type {modelType}, audio {audio} (until Guerrilla's sounds are ported), controller Human NPC, effect material Flesh");
        if (added.Count > 0) log.Add($"Armageddon's defaults added for the fields it requires: {string.Join(", ", added.Distinct())}");
        if (unfilled.Count > 0) log.Add($"required by Armageddon, with no default to give (the game will stop on them): {string.Join(", ", unfilled)}");
        return copy;
    }

    /// <summary>
    /// Armageddon's player's fields Mason's entry never takes: who it is and what it looks like (his own
    /// model, skeleton, set, ragdoll and spine bend), its inventory (the game's own player's), and Darius's bone
    /// levels of detail (his bones' names).
    /// </summary>
    static readonly string[] NotFromPlayer =
    [
        "Name", "Model", "Rig", "Team", "Animation_Sets", "Shared_Container", "ragdoll_type", "Spinebend", "Bone_LOD", "Flags",
        "Inventory", "Equip_Item_Name", "Personas", "_Editor",
    ];

    // ---- Props

    /// <summary>
    /// A character's props as Armageddon's: Guerrilla's items its Props name (with their Sub_Items, an item worn on
    /// another, as Braun's mask on his helmet), of either kind Armageddon's prop code wears:
    ///   - character meshes with their own skeleton (items_3d character_mesh: mesh and rig; inherit_bone_transforms:
    ///     Armageddon's prop code, Guerrilla's kept, copies the wearer's bones onto the prop's skeleton by name):
    ///     Mason's coat, the Marauders' masks and bags, Braun's backpack; converted by <see cref="GuerrillaCharacterMeshes"/>
    ///     (its bone numbers are its own skeleton's), the skeleton Guerrilla's file as it is (loaded through rigs.txt);
    ///   - static meshes (items_3d Mesh): helmets, masks, hats; converted by <see cref="GuerrillaStaticMeshes"/>
    ///     (version 4 → 6, their tags as they are: the prop hangs on the wearer's Character_Prop by its own
    ///     Item_Prop tag).
    /// Each one's mesh comes from the character's own container, or from the item's own in Guerrilla's items.vpp_pc
    /// (its streaming category there "General", "Head mesh": loaded on its own), and is packed with its textures into
    /// the container being made (<paramref name="files"/>), as Armageddon's own people carry theirs: so every item is
    /// "Character prop mesh only". Its item, Guerrilla's entry fitted to Armageddon's table, goes into
    /// <paramref name="items"/>. Returns the items made; the rest wait (logged).
    /// </summary>
    static HashSet<string> PortProps(XtblElement character, Packfile pack, List<PegFile> pegs, Lazy<List<PegFile>> allPegs, Packfile guerrillaTables,
                                     Packfile gameTables, Lazy<Packfile> guerrillaItems, IReadOnlyDictionary<string, XtblElement> rfgItems,
                                     IReadOnlySet<string> gameItemNames, FieldDescription itemDescription, List<(string Name, byte[] Data)> files,
                                     Dictionary<string, byte[]> rigs, Dictionary<string, XtblElement> items, List<string> log)
    {
        var made = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var waiting = new List<string>();
        // Every item a prop names, and the items worn on them. A prop never worn (Chance 0: the multiplayer
        // backpacks) is made only from the character's own container (Mason's armour upgrades).
        var held = new List<string>();
        var ownOnly = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        void Collect(IEnumerable<XtblElement> list, string sub, bool worn)
        {
            foreach (var item in list)
            {
                if (item.ValueOf("Name")?.Trim() is { Length: > 0 } name)
                {
                    if (!held.Contains(name, StringComparer.OrdinalIgnoreCase)) { held.Add(name); if (!worn) ownOnly.Add(name); }
                    else if (worn) ownOnly.Remove(name);
                }
                Collect(item.Element(sub)?.Elements ?? [], sub, worn);
            }
        }
        foreach (var prop in character.Element("Props")?.Elements ?? [])
            Collect(prop.Element("Items")?.Elements ?? [], "Sub_Items", (prop.ValueOf("Chance")?.Trim() ?? "100") is not ("0" or "0.0"));

        // A file of the item's: in the character's container, else in the item's own in items.vpp_pc.
        (Packfile Pack, PackfileEntry C, PackfileEntry G)? Find(string item, string mesh, string cExt, string gExt)
        {
            if (pack.Find(mesh + cExt) is { } c && pack.Find(mesh + gExt) is { } g) return (pack, c, g);
            if (ownOnly.Contains(item)) return null;
            foreach (string container in new[] { item, mesh })
                if (guerrillaItems.Value.Find(container + ".str2_pc") is { } source)
                {
                    var own = guerrillaItems.Value.OpenContainer(source);
                    if (own.Find(mesh + cExt) is { } oc && own.Find(mesh + gExt) is { } og) return (own, oc, og);
                }
            return null;
        }

        string NotFound(string item, string mesh) => ownOnly.Contains(item)
            ? $"{item} (never worn here, Chance 0, and its mesh {mesh} isn't in the character's container)"
            : $"{item} (its mesh {mesh} is neither in the character's container nor in an item's of its own)";

        foreach (string name in held)
        {
            if (!rfgItems.TryGetValue(name, out var rfg)) { waiting.Add($"{name} (Guerrilla's {ItemTable} hasn't it)"); continue; }
            if (gameItemNames.Contains(name)) { waiting.Add($"{name} (Armageddon has an item of that name)"); continue; }
            var characterMesh = rfg.Element("character_mesh");
            string? meshFile = characterMesh?.Element("character_mesh")?.ValueOf("Filename")?.Trim(), rigFile = characterMesh?.Element("rig")?.ValueOf("Filename")?.Trim();
            string? staticFile = rfg.Element("Mesh")?.ValueOf("Filename")?.Trim();
            IReadOnlyList<string> textures;
            string line;
            if (!string.IsNullOrEmpty(meshFile) && !string.IsNullOrEmpty(rigFile))
            {
                string mesh = Path.GetFileNameWithoutExtension(meshFile).ToLowerInvariant(), rig = Path.ChangeExtension(Path.GetFileName(rigFile), ".rig_pc").ToLowerInvariant();
                if (Find(name, mesh, ".ccmesh_pc", ".gcmesh_pc") is not { } found) { waiting.Add(NotFound(name, mesh)); continue; }
                if (gameTables.Find(rig) != null) { waiting.Add($"{name} (Armageddon has a skeleton {rig} of its own)"); continue; }
                if (guerrillaTables.Find(rig) is not { } rigEntry) { waiting.Add($"{name} (Guerrilla has no {rig})"); continue; }
                byte[] cFile = found.Pack.Read(found.C);
                if (GuerrillaCharacterMeshes.NotYet(cFile) is { } why) { waiting.Add($"{name} ({why})"); continue; }
                var notSkinned = new List<string>();
                (byte[] header, textures) = GuerrillaCharacterMeshes.Convert(cFile, GuerrillaVehicles.Alias, notSkinned);
                if (!files.Any(f => f.Name.Equals(mesh + ".ccmesh_pc", StringComparison.OrdinalIgnoreCase)))
                {
                    files.Add((mesh + ".ccmesh_pc", header));
                    files.Add((mesh + ".gcmesh_pc", found.Pack.Read(found.G)));
                }
                rigs[rig] = guerrillaTables.Read(rigEntry);
                line = $"prop {name}: its mesh {mesh} ({cFile.Length:N0} → {header.Length:N0} bytes{(found.Pack == pack ? "" : ", from its own container in items.vpp_pc")}), " +
                       $"its own skeleton {rig}" + (notSkinned.Count > 0 ? $"; materials the skinned shaders can't draw yet: {string.Join("; ", notSkinned)}" : "");
            }
            else if (!string.IsNullOrEmpty(staticFile))
            {
                string mesh = Path.GetFileNameWithoutExtension(staticFile).ToLowerInvariant();
                if (Find(name, mesh, ".csmesh_pc", ".gsmesh_pc") is not { } found) { waiting.Add(NotFound(name, mesh)); continue; }
                byte[] cFile = found.Pack.Read(found.C);
                if (GuerrillaStaticMeshes.NotYet(cFile) is { } why) { waiting.Add($"{name} ({why})"); continue; }
                (byte[] header, textures) = GuerrillaStaticMeshes.Convert(cFile, GuerrillaVehicles.Alias);
                if (!files.Any(f => f.Name.Equals(mesh + ".csmesh_pc", StringComparison.OrdinalIgnoreCase)))
                {
                    files.Add((mesh + ".csmesh_pc", header));
                    files.Add((mesh + ".gsmesh_pc", found.Pack.Read(found.G)));
                }
                line = $"prop {name}: its static mesh {mesh} ({cFile.Length:N0} → {header.Length:N0} bytes{(found.Pack == pack ? "" : ", from its own container in items.vpp_pc")})";
            }
            else { waiting.Add($"{name} (neither a character mesh nor a static mesh)"); continue; }

            var missing = new List<string>();
            foreach (string texture in textures)
            {
                string alias = GuerrillaVehicles.Alias(texture);
                if (files.Any(f => f.Name.Equals(alias + ".cvbm_pc", StringComparison.OrdinalIgnoreCase))) continue;  // packed already
                var texFiles = GuerrillaBuiltIns.Files(texture, alias) ?? Single(pegs, texture, alias) ?? Single(allPegs.Value, texture, alias);
                if (texFiles == null) missing.Add(texture);
                else files.AddRange(texFiles);
            }
            items[name] = PropItem(rfg, itemDescription);
            if (GuerrillaCloth.Worn(name)) items[name].Element("Item_Flags")!.AddElement("Flag", "cloth_sim");  // its cloth (the cloth part, GuerrillaCloth)
            made.Add(name);
            log.Add(line + $", {textures.Count} textures" + (missing.Count > 0 ? $" ({missing.Count} not found: {string.Join(", ", missing)})" : ""));
        }
        if (waiting.Count > 0) log.Add($"props not made yet: {string.Join(", ", waiting.Distinct())}");
        return made;
    }

    /// <summary>The prefix of a head's item and mesh (rfg_ + the face's name: rfg_mason_head).</summary>
    const string HeadPrefix = "rfg_";

    /// <summary>The prop type a head is worn as (one of Armageddon's prop_system_type.xtbl: Guerrilla's Head isn't in either game's).</summary>
    const string HeadPropType = "head_prop";

    /// <summary>The prop type Armageddon leaves off in a Wrecking Crew match (our worlds), and the one such props take instead.</summary>
    const string SkippedPropType = "Back", WornPropType = "accessory_misc";

    /// <summary>
    /// A character's heads (its Heads: Guerrilla draws each as its base head, an items_3d character mesh, dressed
    /// with the face's own materials, &lt;Name&gt;.mat_pc in the character's container, on the face's skeleton,
    /// &lt;Name&gt;.rig_pc, else the base head's), made as props Armageddon's prop code wears (<see cref="HeadPropType"/>).
    /// Every head here is on its base head's skeleton: the game holds at most 290 skeletons, and a face's own (32 of
    /// them) is only the base head's bones placed for that face. That is enough because the game draws a prop's bones
    /// with its wearer's finished skin matrices (inherit_bone_transforms: 0xD8A2B0 matches the bones by name, 0xD80850
    /// copies the wearer's into the prop's), so the prop's own skeleton's places never count: the head's vertices stay
    /// in the face's own pose, which is its wearer's (every character's skeleton has its face's head and eye bones in
    /// the same places). Moving them onto the base head's places (2026-11-06 to 11-12) put the eyes 1.5 to 3.5 cm in
    /// front of the face, or behind it, by how far each face's bones are from the base head's.
    /// Armageddon kept Guerrilla's head code, but it needs the base heads as preloaded items and drops a face's
    /// shape (its morph); a prop needs neither. Each one's mesh (the base head's, from Guerrilla's items.vpp_pc)
    /// is converted dressed with the face's materials and packed as rfg_&lt;Name&gt; with its textures into the
    /// container being made (<paramref name="files"/>), its skeleton is the base head's, Guerrilla's file as it is
    /// (loaded through rigs.txt), and its item, rfg_&lt;Name&gt; (<see cref="HeadItem"/>), goes into <paramref name="items"/>. A face
    /// shaped by a morph (&lt;Name&gt;.morph_pc: Braun's, May's, the civilians') has it baked into the base head's
    /// vertices (<see cref="GuerrillaFaces"/>). Guerrilla's low-detail head for 20 m and more (Lod_Head) isn't used:
    /// the head's own detail levels do. Returns the items made, in the Heads' order.
    /// </summary>
    static List<string> PortHeads(XtblElement character, Packfile pack, List<PegFile> pegs, Lazy<List<PegFile>> allPegs, Packfile guerrillaTables,
                                  Packfile gameTables, Lazy<Packfile> guerrillaItems, IReadOnlyDictionary<string, XtblElement> rfgItems,
                                  IReadOnlySet<string> gameItemNames, FieldDescription itemDescription, List<(string Name, byte[] Data)> files,
                                  Dictionary<string, byte[]> rigs, Dictionary<string, XtblElement> items, List<string> log)
    {
        var made = new List<string>();
        var waiting = new List<string>();
        foreach (var head in character.Element("Heads")?.Elements ?? [])
        {
            string face = head.ValueOf("Name")?.Trim() ?? "", baseHead = head.ValueOf("Base_Head")?.Trim() ?? "";
            if (face.Length == 0 || baseHead.Length == 0) continue;
            string item = HeadPrefix + face;
            if (made.Contains(item, StringComparer.OrdinalIgnoreCase)) continue;
            if (gameItemNames.Contains(item)) { waiting.Add($"{face} (Armageddon has an item {item})"); continue; }
            if (!rfgItems.TryGetValue(baseHead, out var baseItem)) { waiting.Add($"{face} (Guerrilla's {ItemTable} hasn't its base head {baseHead})"); continue; }
            string? meshFile = baseItem.Element("character_mesh")?.Element("character_mesh")?.ValueOf("Filename")?.Trim();
            if (string.IsNullOrEmpty(meshFile)) { waiting.Add($"{face} (its base head {baseHead} isn't a character mesh)"); continue; }
            string mesh = Path.GetFileNameWithoutExtension(meshFile);
            if (guerrillaItems.Value.Find(baseHead + ".str2_pc") is not { } headSource) { waiting.Add($"{face} (Guerrilla's items.vpp_pc has no {baseHead}.str2_pc)"); continue; }
            var headPack = guerrillaItems.Value.OpenContainer(headSource);
            // The face's shape: its morph of the base head, baked in (GuerrillaFaces), if it has one.
            var morphEntry = pack.Find(face + ".morph_pc");
            var morphPack = morphEntry != null ? pack : headPack;
            morphEntry ??= headPack.Find(face + ".morph_pc");
            var cEntry = headPack.Find(mesh + ".ccmesh_pc");
            var gEntry = headPack.Find(mesh + ".gcmesh_pc");
            if (cEntry == null || gEntry == null) { waiting.Add($"{face} ({baseHead}.str2_pc has no {mesh}.ccmesh_pc and .gcmesh_pc)"); continue; }
            if (pack.Find(face + ".mat_pc") is not { } materialEntry) { waiting.Add($"{face} (its materials, {face}.mat_pc, aren't in the character's container)"); continue; }
            // Every face on its base head's skeleton (the game holds 290 at most); its vertices stay in its own pose (see above).
            string rig = baseHead.ToLowerInvariant() + ".rig_pc", faceRig = face.ToLowerInvariant() + ".rig_pc";
            if (gameTables.Find(rig) != null) { waiting.Add($"{face} (Armageddon has a skeleton {rig} of its own)"); continue; }
            if (guerrillaTables.Find(rig) is not { } rigEntry) { waiting.Add($"{face} (Guerrilla has no {rig})"); continue; }
            byte[] cFile = headPack.Read(cEntry);
            if (GuerrillaCharacterMeshes.NotYet(cFile) is { } why) { waiting.Add($"{face} ({why})"); continue; }
            byte[] gFile = headPack.Read(gEntry);
            string shape = "no morph: the base head's own shape";
            if (morphEntry != null)
            {
                try { gFile = GuerrillaFaces.Bake(cFile, gFile, morphPack.Read(morphEntry), out string baked); shape = $"its morph baked in: {baked}"; }
                catch (Exception e) when (e is InvalidDataException or NotSupportedException) { waiting.Add($"{face} (its morph couldn't be baked: {e.Message})"); continue; }
            }
            string skeleton = $"the base head's skeleton {rig}";
            if (guerrillaTables.Find(faceRig) is { } faceRigEntry && !faceRig.Equals(rig, StringComparison.OrdinalIgnoreCase))
            {
                // The face's own skeleton must be the base head's bones (by name and parent), so the wearer's match them.
                var own = Rig.Read(guerrillaTables.Read(faceRigEntry));
                var shared = Rig.Read(guerrillaTables.Read(rigEntry));
                if (own.Bones.Count != shared.Bones.Count ||
                    !own.Bones.Zip(shared.Bones).All(p => p.First.Name.Equals(p.Second.Name, StringComparison.OrdinalIgnoreCase) && p.First.Parent == p.Second.Parent))
                { waiting.Add($"{face} (its skeleton {faceRig} isn't {rig}'s bones)"); continue; }
                skeleton += $" (the face's own, {faceRig}, has the same bones; the head keeps its own pose, its wearer's)";
            }
            var notSkinned = new List<string>();
            var (header, textures) = GuerrillaCharacterMeshes.Convert(cFile, GuerrillaVehicles.Alias, notSkinned, pack.Read(materialEntry));
            files.Add((item + ".ccmesh_pc", header));
            files.Add((item + ".gcmesh_pc", gFile));
            var missing = new List<string>();
            foreach (string texture in textures)
            {
                string alias = GuerrillaVehicles.Alias(texture);
                if (files.Any(f => f.Name.Equals(alias + ".cvbm_pc", StringComparison.OrdinalIgnoreCase))) continue;  // packed already
                var texFiles = GuerrillaBuiltIns.Files(texture, alias) ?? Single(pegs, texture, alias) ?? Single(allPegs.Value, texture, alias);
                if (texFiles == null) missing.Add(texture);
                else files.AddRange(texFiles);
            }
            rigs[rig] = guerrillaTables.Read(rigEntry);
            items[item] = HeadItem(baseItem, item, rig, itemDescription);
            made.Add(item);
            log.Add($"head {face}: {item}, the base head {baseHead} ({cFile.Length:N0} → {header.Length:N0} bytes; {shape}) dressed with {face}.mat_pc, {textures.Count} textures" +
                    (missing.Count > 0 ? $" ({missing.Count} not found: {string.Join(", ", missing)})" : "") + $", {skeleton}; worn as a {HeadPropType} prop" +
                    (notSkinned.Count > 0 ? $"; materials the skinned shaders can't draw yet: {string.Join("; ", notSkinned)}" : ""));
        }
        if (waiting.Count > 0) log.Add($"heads not made yet: {string.Join(", ", waiting.Distinct())}");
        return made;
    }

    /// <summary>
    /// A head's item: its base head's items_3d entry under the head's own name, its mesh ours (rfg_&lt;Name&gt;, in the
    /// wearer's container: "Character prop mesh only"), its base head's skeleton, and inherit_bone_transforms (the
    /// head's bones, spine2 to the eyes, follow the wearer's of the same names), fitted as a prop's.
    /// </summary>
    static XtblElement HeadItem(XtblElement baseItem, string item, string rig, FieldDescription description)
    {
        var head = baseItem.Copy();
        head.Element("Name")!.Value = item;
        var characterMesh = head.Element("character_mesh")!;
        characterMesh.Element("character_mesh")!.Element("Filename")!.Value = item + ".cmeshx";
        (characterMesh.Element("rig") ?? characterMesh.AddElement("rig")).Element("Filename")?.Remove();
        characterMesh.Element("rig")!.AddElement("Filename", Path.ChangeExtension(rig, ".rigx"));
        head.Element("streaming_category")?.Remove();
        head.AddElement("streaming_category", "Character prop mesh only");
        var flags = head.Element("Item_Flags") ?? head.AddElement("Item_Flags");
        if (!flags.Elements.Any(f => f.Value.Trim().Equals("inherit_bone_transforms", StringComparison.OrdinalIgnoreCase))) flags.AddElement("Flag", "inherit_bone_transforms");
        return PropItem(head, description);
    }

    /// <summary>
    /// A prop's item: Guerrilla's items_3d entry fitted to Armageddon's description (the fields it hasn't left out,
    /// the ones it requires filled), with Armageddon's cast_shadows (its own props cast shadows; Guerrilla has no such
    /// flag). An item with cloth of its own (GuerrillaCloth.Worn, the cloth part) gets Armageddon's cloth_sim flag where it is made.
    /// </summary>
    static XtblElement PropItem(XtblElement rfg, FieldDescription description)
    {
        var item = rfg.Copy();
        // Its mesh is packed in the wearer's container (PortProps), whatever Guerrilla streamed it with.
        item.Element("streaming_category")?.Remove();
        item.AddElement("streaming_category", "Character prop mesh only");
        var dropped = new List<string>();
        GuerrillaTableFit.Prune(item, description.Children, "", dropped);
        var flags = item.Element("Item_Flags") ?? item.AddElement("Item_Flags");
        if (!flags.Elements.Any(f => f.Value.Trim().Equals("cast_shadows", StringComparison.OrdinalIgnoreCase))) flags.AddElement("Flag", "cast_shadows");
        item.Element("_Editor")?.Remove();
        item.AddElement("_Editor").AddElement("Category", "Character Props:Guerrilla");
        GuerrillaTableFit.FillRequired(item, description.Children, "", [], []);
        return item;
    }

    // ---- The player (Mason)

    /// <summary>
    /// Mason's skeleton with the tags Armageddon's player code looks for by name that his hasn't (it skips a
    /// name it can't find, and the feature with it): muzzle_flash0, where the Nano Forge launches from and its
    /// abilities' effects and its light sit, and l-nanoforge, both on Darius's left forearm; l-spotlight, the
    /// player's light, on Darius's chest. Tags come after the bones, so his animations' bone numbers stay as
    /// they are. Each is placed as on Darius: the forearm's by the forearm (Darius's is bent about 40° forward
    /// at rest, Mason's hangs straight, so the offset and the turn are carried over by the turn between the two
    /// forearms, and scaled by their lengths), the chest's from the chest bone, scaled by the two heights.
    /// (camera-lookat, which the camera reads as a bone, is added as a bone by PlayerRetarget.WithCameraLookat:
    /// missing, the entry holds -1 and the camera goes where bone -1 points.)
    /// </summary>
    static byte[] PlayerTags(byte[] rig, Rig mason, Rig darius, List<string> log)
    {
        int Bone(Rig r, string name) => r.Bones.ToList().FindIndex(b => b.Name.Equals(name, StringComparison.OrdinalIgnoreCase));
        RigTag? Tag(string name) => darius.Tags.FirstOrDefault(t => t.Name.Equals(name, StringComparison.OrdinalIgnoreCase));
        var added = new List<(string Name, int Bone, Matrix4x4 Rotation, Vector3 Position)>();
        bool Has(string name) => mason.Tags.Any(t => t.Name.Equals(name, StringComparison.OrdinalIgnoreCase));

        // The forearm's: Darius's l-forearm → L-Hand, Mason's l-foretwist → l-hand.
        int dForearm = Bone(darius, "l-forearm"), dHand = Bone(darius, "L-Hand"), mForearm = Bone(mason, "l-foretwist"), mHand = Bone(mason, "l-hand");
        if (dForearm >= 0 && dHand >= 0 && mForearm >= 0 && mHand >= 0)
        {
            Vector3 dAxis = darius.Bones[dHand].Position - darius.Bones[dForearm].Position, mAxis = mason.Bones[mHand].Position - mason.Bones[mForearm].Position;
            var turn = Between(Vector3.Normalize(dAxis), Vector3.Normalize(mAxis));
            float scale = mAxis.Length() / dAxis.Length();
            foreach (string name in new[] { "muzzle_flash0", "l-nanoforge" })
                if (Tag(name) is { } t && t.Bone == dForearm && !Has(name))
                    added.Add((name, mForearm, Turned(t.Rotation, turn), Vector3.Transform(t.Position, turn) * scale));
        }
        // The chest's: Darius's l-spotlight bone (on Spine2) and its tag's offset, from Mason's spine2.
        int dChest = Bone(darius, "Spine2"), mChest = Bone(mason, "spine2"), dHead = Bone(darius, "Head"), mHead = Bone(mason, "head");
        if (Tag("l-spotlight") is { } spot && dChest >= 0 && mChest >= 0 && dHead >= 0 && mHead >= 0 && !Has("l-spotlight"))
        {
            float height = mason.Bones[mHead].Position.Y / darius.Bones[dHead].Position.Y;
            var at = darius.Bones[spot.Bone].Position + spot.Position - darius.Bones[dChest].Position;
            added.Add(("l-spotlight", mChest, spot.Rotation, at * height));
        }
        foreach (var t in added) log.Add($"tag {t.Name} added on {mason.Bones[t.Bone].Name} at ({t.Position.X:0.000}, {t.Position.Y:0.000}, {t.Position.Z:0.000}) from it");
        return added.Count == 0 ? rig : WithTags(rig, added);
    }

    /// <summary>The shortest turn taking direction <paramref name="from"/> to <paramref name="to"/> (both of length 1).</summary>
    static Quaternion Between(Vector3 from, Vector3 to)
    {
        var axis = Vector3.Cross(from, to);
        float dot = Math.Clamp(Vector3.Dot(from, to), -1, 1);
        if (axis.LengthSquared() < 1e-12f) return dot > 0 ? Quaternion.Identity : Quaternion.CreateFromAxisAngle(Vector3.UnitY, MathF.PI);
        return Quaternion.CreateFromAxisAngle(Vector3.Normalize(axis), MathF.Acos(dot));
    }

    /// <summary>A tag's rotation (its rows right, up, forward) turned by <paramref name="turn"/>.</summary>
    static Matrix4x4 Turned(Matrix4x4 rotation, Quaternion turn)
    {
        Vector3 Row(float x, float y, float z) => Vector3.Transform(new Vector3(x, y, z), turn);
        var r = Row(rotation.M11, rotation.M12, rotation.M13);
        var u = Row(rotation.M21, rotation.M22, rotation.M23);
        var f = Row(rotation.M31, rotation.M32, rotation.M33);
        return new Matrix4x4(r.X, r.Y, r.Z, 0, u.X, u.Y, u.Z, 0, f.X, f.Y, f.Z, 0, 0, 0, 0, 1);
    }

    /// <summary>
    /// A skeleton file (see <see cref="Rig"/>) with tags added after its own: each 60 bytes (where its name
    /// starts in the names, its rotation's rows, its position from its bone, its bone, -1), their names after
    /// the file's own, the tag count (+0x30) raised. The bones and their names stay where they are.
    /// </summary>
    static byte[] WithTags(byte[] rig, IReadOnlyList<(string Name, int Bone, Matrix4x4 Rotation, Vector3 Position)> tags)
    {
        int boneCount = BitConverter.ToInt32(rig, 0x24), tagCount = BitConverter.ToInt32(rig, 0x30);
        int tagsEnd = 0x40 + 4 * boneCount + 36 * boneCount + 60 * tagCount;
        byte[] names = rig[tagsEnd..];
        var newNames = new List<byte>(names);
        using var output = new MemoryStream();
        using var w = new BinaryWriter(output);
        w.Write(rig, 0, tagsEnd);
        foreach (var (name, bone, rotation, position) in tags)
        {
            w.Write(newNames.Count);
            foreach (float v in new[] { rotation.M11, rotation.M12, rotation.M13, rotation.M21, rotation.M22, rotation.M23, rotation.M31, rotation.M32, rotation.M33 }) w.Write(v);
            w.Write(position.X); w.Write(position.Y); w.Write(position.Z);
            w.Write(bone);
            w.Write(-1);
            newNames.AddRange(System.Text.Encoding.Latin1.GetBytes(name));
            newNames.Add(0);
        }
        w.Write(newNames.ToArray());
        byte[] result = output.ToArray();
        BitConverter.GetBytes(tagCount + tags.Count).CopyTo(result, 0x30);
        return result;
    }

    /// <summary>
    /// The people build's rows in the spine bend and limb tables (Mason's), the only ones it writes or takes out there:
    /// the vehicles build adds its walkers' legs to the same limb table (mods\guerrilla_world\ik_limbs.xtbl).
    /// </summary>
    static readonly Dictionary<string, string[]> OurRows = new(StringComparer.OrdinalIgnoreCase)
    {
        [SpinebendTable] = [PlayerSpinebendName], [LimbTable] = ["rfg human left hand", "rfg human right hand"],
    };

    /// <summary>One of Armageddon's tables (misc.vpp_pc), as its file's bytes.</summary>
    static byte[] ArmageddonsTable(Packfile gameTables, string table) =>
        gameTables.Read(gameTables.Find(table) ?? throw new InvalidDataException($"no {table} in Armageddon's misc.vpp_pc"));

    /// <summary>A table of mods\guerrilla_world\ another build also adds to: the one written there (else Armageddon's), our rows taken out.</summary>
    static XtblFile SharedTableWithoutOurs(string worldFolder, Packfile gameTables, string table)
    {
        string path = Path.Combine(worldFolder, table);
        var written = XtblFile.FromBytes(File.Exists(path) ? File.ReadAllBytes(path) : ArmageddonsTable(gameTables, table));
        foreach (var row in written.Entries.Where(e => OurRows[table].Contains(XtblFile.EntryName(e), StringComparer.OrdinalIgnoreCase)).ToList())
            written.RemoveEntry(row);
        return written;
    }

    /// <summary>
    /// Armageddon's spine bend table with Mason's entry added: its player's (the same turns, limits and
    /// blends) on his bones: his upper arms are l-/r-upperarmtwist1 (Darius's l-/r-upperarm), his eyes l-/r-eye
    /// (Darius's face-l-/r-eye). Guerrilla has no such table (its spine bend is its own code's).
    /// </summary>
    static XtblFile PlayerSpinebend(XtblFile table)
    {
        var player = table.Entries.FirstOrDefault(e => XtblFile.EntryName(e).Equals("player", StringComparison.OrdinalIgnoreCase))
                     ?? throw new InvalidDataException($"Armageddon's {SpinebendTable} has no \"player\" entry");
        var mason = player.Copy();
        mason.Element("Name")!.Value = PlayerSpinebendName;
        var renamed = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["r-upperarm"] = "r-upperarmtwist1", ["l-upperarm"] = "l-upperarmtwist1", ["face-r-eye"] = "r-eye", ["face-l-eye"] = "l-eye",
        };
        foreach (var bone in mason.Element("Bones")?.Elements ?? [])
            if (bone.Element("name") is { } n && renamed.TryGetValue(n.Value.Trim(), out var his)) n.Value = his;
        table.Table!.AddElement(mason);
        return table;
    }

    /// <summary>
    /// Armageddon's limb table with Mason's arms added (for "ik hands to weapon"): its human hands' limbs on
    /// his bones, upper arm l-/r-upperarmtwist1, forearm l-/r-foretwist (Darius's l-/r-upperarm, l-/r-forearm),
    /// the hand tags as they are (his are on his hand props). They come after Armageddon's, which the game tries
    /// first for a skeleton that has their bones (Darius's); his legs already have Armageddon's bone names.
    /// </summary>
    static XtblFile PlayerLimbs(XtblFile table)
    {
        foreach (string side in new[] { "left", "right" })
        {
            string s = side[..1];
            var human = table.Entries.FirstOrDefault(e => XtblFile.EntryName(e).Equals($"human {side} hand", StringComparison.OrdinalIgnoreCase))
                        ?? throw new InvalidDataException($"Armageddon's {LimbTable} has no \"human {side} hand\"");
            var mason = human.Copy();
            mason.Element("Name")!.Value = $"rfg human {side} hand";
            mason.Element("Anchor")!.Value = $"{s}-upperarmtwist1";
            mason.Element("Joint")!.Value = $"{s}-foretwist";
            table.Table!.AddElement(mason);
        }
        return table;
    }

    /// <summary>What a container of Guerrilla's holds (its files' names and bytes), to tell which characters' are the same.</summary>
    static string ContentKey(Packfile pack)
    {
        using var hash = System.Security.Cryptography.IncrementalHash.CreateHash(System.Security.Cryptography.HashAlgorithmName.SHA256);
        foreach (var e in pack.Entries.OrderBy(e => e.Name, StringComparer.OrdinalIgnoreCase))
        {
            hash.AppendData(System.Text.Encoding.UTF8.GetBytes(e.Name.ToLowerInvariant() + "\0"));
            hash.AppendData(pack.Read(e));
        }
        return System.Convert.ToHexString(hash.GetHashAndReset());
    }

    /// <summary>A character container's asset list entry, as Armageddon lists its people's: the model and character-mesh props (type 7), static props (type 6), each texture (type 1), each animation (type 16, no data file).</summary>
    static AsmContainer Entry(string name, byte[] container, IReadOnlyList<(string Name, byte[] Data)> files, byte containerType = CharacterContainerType)
    {
        var c = Packfile.FromBytes(name, container);
        var entry = new AsmContainer { Name = name, Type = containerType, Flags = 0x80, DataOffset = (uint)c.DataStart, CompressedSize = c.CompressedDataSize };
        entry.Sizes.AddRange(files.Select(f => f.Data.Length));
        foreach (var (file, data) in files)
        {
            string ext = Path.GetExtension(file).ToLowerInvariant();
            byte type = ext switch { ".ccmesh_pc" => 7, ".csmesh_pc" => 6, ".cvbm_pc" or ".cpeg_pc" => 1, ".anim_pc" => 16, _ => 0 };
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

    /// <summary>A texture of the texture files given as Armageddon's single-texture files under <paramref name="name"/>, or null if none has it.</summary>
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

    // ---- The animation sets

    /// <summary>What this tool added to the shared animation lists (mods\guerrilla_people\anim_entries.txt): "set x.xtbl" and "file x.animx" lines.</summary>
    readonly record struct Own(string Kind, string Name);

    static List<Own> ReadOwn(string peopleFolder)
    {
        string path = Path.Combine(peopleFolder, OwnEntries);
        if (!File.Exists(path)) return [];
        return [.. File.ReadAllLines(path).Select(l => l.Trim().Split(' ', 2)).Where(p => p.Length == 2).Select(p => new Own(p[0], p[1].Trim()))];
    }

    /// <summary>Takes this tool's own entries (as <paramref name="own"/> names them) out of a shared list; how many.</summary>
    static int RemoveOwn(XtblFile table, string name, IReadOnlyList<Own> own)
    {
        int removed = 0;
        if (name == SetList)
        {
            var sets = new HashSet<string>(own.Where(o => o.Kind == "set").Select(o => o.Name), StringComparer.OrdinalIgnoreCase);
            foreach (var e in table.Entries.Where(e => sets.Contains(e.ValueOf("Name")?.Trim() ?? "")).ToList()) { e.Remove(); removed++; }
        }
        else if (name == FileList)
        {
            var files = new HashSet<string>(own.Where(o => o.Kind == "file").Select(o => o.Name), StringComparer.OrdinalIgnoreCase);
            var list = table.Table?.Element("Files")?.Element("Anim_files");
            foreach (var e in list?.Elements.Where(e => files.Contains(e.Element("Animation")?.ValueOf("Filename")?.Trim() ?? "")).ToList() ?? []) { e.Remove(); removed++; }
        }
        return removed;
    }

    /// <summary>A set ported: its table's file name, its Skeleton_Set name, and the animations to pack with a character that plays it.</summary>
    sealed record PortedSet(string Set, string SetFile, IReadOnlyList<(string Name, byte[] Data)> Animations);

    /// <summary>
    /// Guerrilla's people's animation sets for Armageddon (see the summary). Each set is ported once, with
    /// the set it builds on (Parent_Skeleton); a character's container packs the animations of its set and
    /// of every set under it.
    /// </summary>
    sealed class SetPort
    {
        readonly Packfile guerrillaTables, gameTables, guerrillaAnims;
        readonly XtblFile setList, fileList, stateList, actionList;
        readonly XtblElement fileEntries;
        readonly Dictionary<string, XtblElement> rfgFileEntries = new(StringComparer.OrdinalIgnoreCase);  // Guerrilla's anim_files.xtbl by x.animx
        readonly Dictionary<string, XtblElement> blendTrees = new(StringComparer.OrdinalIgnoreCase);      // Guerrilla's anim_blend_trees.xtbl by name
        readonly Dictionary<string, XtblElement> rfgListing = new(StringComparer.OrdinalIgnoreCase);      // Guerrilla's anim_set_filenames.xtbl by file
        readonly HashSet<string> ourStates, ourActions;
        readonly Dictionary<string, PortedSet?> ported = new(StringComparer.OrdinalIgnoreCase);
        readonly Dictionary<string, byte[]> animations = new(StringComparer.OrdinalIgnoreCase);         // our .anim_pc → its bytes
        readonly List<(string File, byte[] Data)> setTables = [];
        readonly List<Own> own = [];
        readonly int otherSets, otherSetFiles;
        bool statesChanged, actionsChanged;

        public SetPort(Packfile guerrillaTables, Packfile gameTables, Packfile guerrillaAnims, XtblFile setList, XtblFile fileList, XtblFile stateList,
                       XtblFile actionList, IReadOnlyList<Own> earlier)
        {
            (this.guerrillaTables, this.gameTables, this.guerrillaAnims) = (guerrillaTables, gameTables, guerrillaAnims);
            (this.setList, this.fileList, this.stateList, this.actionList) = (setList, fileList, stateList, actionList);
            RemoveOwn(setList, SetList, earlier);
            RemoveOwn(fileList, FileList, earlier);
            fileEntries = fileList.Table?.Element("Files")?.Element("Anim_files") ?? throw new InvalidDataException($"the {FileList} has no Files/Anim_files");
            ourStates = new(stateList.Entries.Select(XtblFile.EntryName), StringComparer.OrdinalIgnoreCase);
            ourActions = new(actionList.Entries.Select(XtblFile.EntryName), StringComparer.OrdinalIgnoreCase);
            foreach (var e in Read(guerrillaTables, FileList).Table?.Element("Files")?.Element("Anim_files")?.Elements ?? [])
                if (e.Element("Animation")?.ValueOf("Filename")?.Trim() is { } file) rfgFileEntries.TryAdd(file, e);
            foreach (var e in Read(guerrillaTables, "anim_blend_trees.xtbl").Entries)
                blendTrees.TryAdd(XtblFile.EntryName(e), e);
            foreach (var e in Read(guerrillaTables, SetList).Entries)
                if (e.ValueOf("Name")?.Trim() is { } file) rfgListing.TryAdd(file, e);
            // The sets already in the game: Armageddon's own (each listed file's, and one an animated
            // object) and the vehicles' (listed by GuerrillaArsenal).
            int sets = 0;
            foreach (string file in setList.Entries.Select(e => e.ValueOf("Name")?.Trim()).OfType<string>())
                sets += gameTables.Find(file) is { } f ? Math.Max(1, XtblFile.FromBytes(gameTables.Read(f)).Entries.Count(e => e.Name.Equals("Skeleton_Set", StringComparison.OrdinalIgnoreCase))) : 1;
            if (gameTables.Find("anim_rig_objects.xtbl") is { } objects)
                sets += XtblFile.FromBytes(gameTables.Read(objects)).Entries.Count(e => e.Name.Equals("Anim_Rig_Object", StringComparison.OrdinalIgnoreCase));
            otherSets = sets;
            otherSetFiles = setList.Entries.Count();
        }

        static XtblFile Read(Packfile pack, string name) => XtblFile.FromBytes(pack.Read(pack.Find(name) ?? throw new InvalidDataException($"no {name} in {pack.Name}")));

        /// <summary>Whether the animation lists already list a set file (another tool's, such as the player's rfg_mason).</summary>
        public bool Listed(string setFile) =>
            setList.Entries.Any(e => string.Equals(e.ValueOf("Name")?.Trim(), setFile, StringComparison.OrdinalIgnoreCase));

        /// <summary>Guerrilla's set <paramref name="set"/> (its file's name without .xtbl) and the sets it builds on, ported; null if it can't be.</summary>
        public PortedSet? Port(string set, List<string> log)
        {
            if (ported.TryGetValue(set, out var done)) return done;
            ported[set] = null;  // (a set building on itself)
            string rfgFile = set + ".xtbl";
            var file = guerrillaTables.Find(rfgFile);
            if (file == null) { log.Add($"animation set {set}: Guerrilla's misc.vpp_pc has no {rfgFile}"); return null; }
            var table = XtblFile.FromBytes(guerrillaTables.Read(file));
            var skeletonSet = table.Entries.FirstOrDefault(e => e.Name.Equals("Skeleton_Set", StringComparison.OrdinalIgnoreCase));
            if (skeletonSet == null) { log.Add($"animation set {set}: {rfgFile} has no Skeleton_Set"); return null; }
            if (otherSets + setTables.Count + 1 > MaxAnimSets || otherSetFiles + setTables.Count + 1 > MaxSetFiles)
            {
                log.Add($"animation set {set}: left out, the game has no room for another (sets {otherSets + setTables.Count} of {MaxAnimSets}, set files {otherSetFiles + setTables.Count} of about {MaxSetFiles + 2})");
                return null;
            }

            // The set it builds on first: its animations go with this one's.
            var packed = new List<(string Name, byte[] Data)>();
            string? parent = skeletonSet.ValueOf("Parent_Skeleton")?.Trim();
            if (!string.IsNullOrEmpty(parent))
            {
                var under = Port(parent, log);
                if (under == null) { log.Add($"animation set {set}: the set it builds on, {parent}, couldn't be ported"); return null; }
                skeletonSet.Element("Parent_Skeleton")!.Value = under.Set;
                packed.AddRange(under.Animations);
            }
            // Its groups as Armageddon's code knows them (Guerrilla's Nano as Nano Rifle, its civilians' empty Pipe left out).
            GuerrillaAnimationSets.FitGroups(set, skeletonSet, log);

            // Each animation a state or action names: Guerrilla's file, or a blend tree's level one.
            var trees = new List<string>();
            var missing = new List<string>();
            // Animations this can't read yet: the faces' (*_fcl_*: after the bones' tracks they hold more,
            // the face's morph targets' weights, which come with the heads).
            var unreadable = new List<string>();
            var states = Descendants(skeletonSet, "State").Concat(Descendants(skeletonSet, "Action")).ToList();
            foreach (var animation in Descendants(skeletonSet, "Animation").ToList())
            {
                var filename = animation.Element("Filename");
                string? named = filename?.Value.Trim();
                if (filename == null || string.IsNullOrEmpty(named)) continue;
                string clip = named;
                if (!named.EndsWith(".animx", StringComparison.OrdinalIgnoreCase))
                {
                    if (!blendTrees.TryGetValue(named, out var tree) || LevelAnimation(tree) is not { } level) { missing.Add(named); filename.Value = ""; continue; }
                    trees.Add(named);
                    clip = level;
                }
                string ours = AnimationPrefix + clip;
                if (!animations.ContainsKey(Path.ChangeExtension(ours, ".anim_pc")))
                {
                    string pc = Path.ChangeExtension(clip, ".anim_pc");
                    var entry = guerrillaAnims.Find(pc);
                    if (entry == null) { missing.Add(clip); filename.Value = ""; continue; }
                    try { animations[Path.ChangeExtension(ours, ".anim_pc")] = Recode(guerrillaAnims.Read(entry)); }
                    catch (Exception e) when (e is InvalidDataException or ArgumentException or IndexOutOfRangeException) { unreadable.Add(clip); filename.Value = ""; continue; }
                    AddFileEntry(clip, ours);
                }
                filename.Value = ours;
                if (!packed.Any(p => p.Name.Equals(Path.ChangeExtension(ours, ".anim_pc"), StringComparison.OrdinalIgnoreCase)))
                    packed.Add((Path.ChangeExtension(ours, ".anim_pc"), animations[Path.ChangeExtension(ours, ".anim_pc")]));
            }
            // A state or action whose animation Guerrilla's files haven't (Guerrilla's own game had none to
            // play), or that this can't read yet, is left out.
            int dropped = 0;
            foreach (var state in states)
                if (state.Element("Animation")?.ValueOf("Filename")?.Trim() is "") { state.Remove(); dropped++; }

            string ourSet = "rfg_" + (skeletonSet.ValueOf("Name")?.Trim() ?? set);
            string ourFile = "rfg_" + rfgFile.ToLowerInvariant();
            if (skeletonSet.Element("Name") is { } nameField) nameField.Value = ourSet;
            var addedNames = new List<string>();
            foreach (var id in Descendants(skeletonSet, "State").Select(s => s.ValueOf("ID")?.Trim()).OfType<string>())
                if (ourStates.Add(id)) { stateList.NewEntry(id, ["Entries", id[..1].ToLowerInvariant()]); statesChanged = true; addedNames.Add($"state \"{id}\""); }
            foreach (var id in Descendants(skeletonSet, "Action").Select(s => s.ValueOf("ID")?.Trim()).OfType<string>())
                if (ourActions.Add(id)) { actionList.NewEntry(id, ["Entries", id[..1].ToLowerInvariant()]); actionsChanged = true; addedNames.Add($"action \"{id}\""); }
            setTables.Add((ourFile, table.ToBytes()));
            var listing = rfgListing.TryGetValue(rfgFile, out var listed) ? listed.Copy() : new XtblElement("Anim_Set_Filename");
            if (listing.Element("Name") is { } listedName) listedName.Value = ourFile;
            else listing.AddElement("Name", ourFile);
            setList.Table!.AddElement(listing);
            own.Add(new("set", ourFile));
            log.Add($"animation set {set} as {ourSet}: {packed.Count} animations with the sets it builds on" +
                    (trees.Count > 0 ? $"; {trees.Distinct().Count()} blend trees (by the ground's slope) play their level animation for now" : "") +
                    (dropped > 0 ? $"; {dropped} states and actions left out for now" : "") +
                    (missing.Count > 0 ? $"; their animations in none of Guerrilla's files: {string.Join(", ", missing.Distinct())}" : "") +
                    (unreadable.Count > 0 ? $"; their animations not read yet (the faces' morph weights after the bones' tracks; with the heads): {string.Join(", ", unreadable.Distinct())}" : "") +
                    (addedNames.Count > 0 ? $"; {addedNames.Count} state and action names Armageddon's tables lacked added" : ""));
            return ported[set] = new PortedSet(ourSet, ourFile, packed);
        }

        /// <summary>A blend tree's animation for level ground: the one whose weight is 1 at its control's 0, else its middle one.</summary>
        static string? LevelAnimation(XtblElement tree)
        {
            var states = tree.Element("States")?.Elements.ToList() ?? [];
            foreach (var state in states)
                foreach (var point in state.Element("Control_points")?.Elements ?? [])
                    if (double.TryParse(point.ValueOf("Range"), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double range) && range == 0 &&
                        double.TryParse(point.ValueOf("Value"), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double value) && value >= 0.999)
                        return state.Element("Animation")?.ValueOf("Filename")?.Trim();
            return states.Count > 0 ? states[states.Count / 2].Element("Animation")?.ValueOf("Filename")?.Trim() : null;
        }

        /// <summary>Guerrilla's anim_files.xtbl entry for an animation (its triggers: footsteps, ...; its flags), under our name, in the character's container.</summary>
        void AddFileEntry(string rfgFile, string ourFile)
        {
            if (fileEntries.Elements.Any(e => string.Equals(e.Element("Animation")?.ValueOf("Filename")?.Trim(), ourFile, StringComparison.OrdinalIgnoreCase)))
                throw new InvalidDataException($"{FileList} already has an animation {ourFile} that isn't ours (another tool's): the names would clash");
            var entry = rfgFileEntries.TryGetValue(rfgFile, out var rfg) ? rfg.Copy() : new XtblElement("Anim_file");
            var animation = entry.Element("Animation") ?? entry.AddElement("Animation");
            if (animation.Element("Filename") is { } filename) filename.Value = ourFile;
            else animation.AddElement("Filename", ourFile);
            if (animation.Element("Preload") is { } preload) preload.Value = "False";
            else animation.AddElement("Preload", "False");
            foreach (var part in entry.Elements.ToList())
                if (LeftOutOfFiles.Contains(part.Name, StringComparer.OrdinalIgnoreCase) || (!part.HasElements && part.Value.Trim().Length == 0)) part.Remove();
            fileEntries.AddElement(entry);
            own.Add(new("file", ourFile));
        }

        /// <summary>
        /// One of Guerrilla's people's animations in Armageddon's key packing, with the tracks of the
        /// bones every person's skeleton shares (<see cref="SharedBones"/>), for a skeleton of at most that
        /// many: the rest of a person's bones (their own) keep their skeleton's rest pose under their parents.
        /// </summary>
        static byte[] Recode(byte[] animation)
        {
            var source = Animation.ReadGuerrilla(animation);
            var tracks = source.Tracks.Where(t => t.Bone < SharedBones).ToList();
            var root = new AnimationTrack(-1, source.Root.Rotations, source.Root.Positions, source.Root.Scales, source.Root.Weights);
            return AnimationWriter.Write(animation.AsSpan(0, 0x30), source.Frames, Math.Min(source.SkeletonBones, SharedBones), tracks, root);
        }

        /// <summary>
        /// Writes the sets' tables (overrides\build\pc\cache\guerrilla\people\), the animations (mods\guerrilla_people\animations\,
        /// which the guerrilla_people mod loads; an earlier run's go), the shared lists (mods\guerrilla_world\), and
        /// what is ours in them (mods\guerrilla_people\).
        /// </summary>
        public IEnumerable<string> Write(string overrides, string worldFolder, string peopleFolder)
        {
            yield return AnimTriggerPools.Check(fileList);  // refuses (throws) before anything is written
            Directory.CreateDirectory(worldFolder);
            Directory.CreateDirectory(peopleFolder);
            foreach (var (file, data) in setTables) File.WriteAllBytes(OverrideLayout.NewFile(overrides, OverrideLayout.People, file), data);
            string folder = Path.Combine(peopleFolder, AnimationFolder);
            Directory.CreateDirectory(folder);
            foreach (string old in Directory.EnumerateFiles(folder, "*.anim_pc").ToList()) File.Delete(old);
            foreach (var (file, data) in animations) File.WriteAllBytes(Path.Combine(folder, file), data);
            File.WriteAllBytes(Path.Combine(worldFolder, SetList), setList.ToBytes());
            File.WriteAllBytes(Path.Combine(worldFolder, FileList), fileList.ToBytes());
            if (statesChanged || File.Exists(Path.Combine(worldFolder, StateList))) File.WriteAllBytes(Path.Combine(worldFolder, StateList), stateList.ToBytes());
            if (actionsChanged || File.Exists(Path.Combine(worldFolder, ActionList))) File.WriteAllBytes(Path.Combine(worldFolder, ActionList), actionList.ToBytes());
            File.WriteAllLines(Path.Combine(peopleFolder, OwnEntries), own.Select(o => $"{o.Kind} {o.Name}"));
            yield return $"Animation sets: {otherSets + setTables.Count} of the {MaxAnimSets} the game holds (ours {setTables.Count}); set files {otherSetFiles + setTables.Count}; " +
                         $"animations {animations.Count} of ours ({animations.Values.Sum(a => (long)a.Length) / 1024.0 / 1024:0.0} MB in {folder}), " +
                         $"the list's {fileEntries.Elements.Count()} of the {MaxAnimations} the game holds with guerrilla_vehicles";
            if (fileEntries.Elements.Count() > MaxAnimations)
                yield return $"WARNING: the list of animations has {fileEntries.Elements.Count()}, more than the {MaxAnimations} the game holds: the ones past it won't play";
        }
    }

    // ---- Table entries fitted to Armageddon's description (as GuerrillaArsenal's)

    /// <summary>Leaves out of an entry every field Armageddon's description hasn't, and every Selection value it has no choice for.</summary>
    static void Prune(XtblElement element, IReadOnlyList<FieldDescription> fields, string path, List<string> dropped)
    {
        foreach (var child in element.Elements.ToList())
        {
            if (child.Name.Equals("_Editor", StringComparison.OrdinalIgnoreCase)) continue;
            var field = fields.FirstOrDefault(f => f.Name.Equals(child.Name, StringComparison.OrdinalIgnoreCase));
            if (field == null) { child.Remove(); dropped.Add(path + child.Name); continue; }
            switch (field.Type)
            {
                case FieldType.Element:
                    Prune(child, field.Children, path + child.Name + "/", dropped);
                    break;
                case FieldType.List or FieldType.Grid when field.Item is { Type: FieldType.Element } item:
                    foreach (var one in child.Elements.Where(e => e.Name.Equals(field.ItemElementName, StringComparison.OrdinalIgnoreCase)).ToList())
                        Prune(one, item.Children, path + child.Name + "/", dropped);
                    break;
                case FieldType.Selection when field.Choices.Count > 0 && !child.HasElements &&
                                              !field.Choices.Contains(child.Value.Trim(), StringComparer.OrdinalIgnoreCase):
                    dropped.Add($"{path}{child.Name} {child.Value.Trim()}");
                    child.Remove();
                    break;
                case FieldType.Flags when field.Flags.Count > 0:
                    foreach (var flag in child.Elements.Where(f => !field.Flags.Contains(f.Value.Trim(), StringComparer.OrdinalIgnoreCase)).ToList())
                    {
                        dropped.Add($"{path}{child.Name} {flag.Value.Trim()}");
                        flag.Remove();
                    }
                    break;
            }
        }
    }

    /// <summary>Adds every field Armageddon's description requires that an entry hasn't, with its default (a group or vector with its own).</summary>
    static void FillRequired(XtblElement element, IReadOnlyList<FieldDescription> fields, string path, List<string> added, List<string> unfilled)
    {
        foreach (var field in fields)
        {
            var child = element.Element(field.Name);
            if (child == null && field.Required)
            {
                if (field.HoldsFields) child = element.AddElement(field.Name);
                else if (field.Default != null) element.AddElement(field.Name, field.Default);
                else { unfilled.Add(path + field.Name); continue; }
                added.Add(path + field.Name);
            }
            if (child == null) continue;
            if (field.HoldsFields) FillRequired(child, field.Children, path + field.Name + "/", added, unfilled);
            else if (field.Type is FieldType.List or FieldType.Grid && field.Item is { Type: FieldType.Element } item)
                foreach (var one in child.Elements.Where(e => e.Name.Equals(field.ItemElementName, StringComparison.OrdinalIgnoreCase)))
                    FillRequired(one, item.Children, path + field.Name + "/", added, unfilled);
        }
    }

    static void Set(XtblElement entry, string field, string value)
    {
        if (entry.Element(field) is { } existing) existing.Value = value;
        else entry.AddElement(field, value);
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
