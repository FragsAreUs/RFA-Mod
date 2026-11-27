using System.Text;
using RfaFormats.Meshes;
using RfaFormats.Tables;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's player animations for its guns, played by Armageddon's player while he holds
/// one of Guerrilla's guns (GuerrillaWeapons builds this with the guns). Both games pick an
/// animation the same way: a character has one animation set, and its code asks the set for a state
/// (standing, walking, aiming, ...) or an action (firing, reloading, ...) in the animation group of the
/// gun held (AK for the assault rifle); a set that hasn't it asks the set it builds on
/// (Parent_Skeleton), then the group's parent group. Armageddon's player's set is vs_player, which its
/// people's sets build on too, and Guerrilla's group names are Armageddon's (Enforcer and Railgun hold
/// Armageddon's own guns as well). So Guerrilla's are a set of their own, rfg_player, built on vs_player,
/// and taken up only while one of Guerrilla's guns is held: the guerrilla_gear mod switches the player to
/// it with the game's own animation-set variants (as Armageddon plays a drunk or wounded civilian's
/// set), under the variant "confident", which nothing of Armageddon's uses. What rfg_player hasn't
/// (Armageddon's own moves: dodging, free running, the Nano Forge) stays Darius's.
///
/// For each group the guns use:
///   - Guerrilla's group as Guerrilla's game plays it: its own entries, then those of the groups it
///     builds on in Guerrilla's anim_groups.xtbl (the enforcer's are mostly the assault rifle's);
///   - the names Armageddon's player asks for (its blend tree's and Darius's set's), each from
///     Guerrilla's entry of that name or, where Guerrilla has none, from the one of Guerrilla's that
///     plays the same (<see cref="Aliases"/>: Armageddon's "fire" is Guerrilla's "stand fire", its
///     upgraded reloads are the plain reloads, ...); the cut cover system and the rest Armageddon never
///     asks for are left out;
///   - a blend tree of Guerrilla's (its walking and running blend a few animations by speed, a table
///     Armageddon can't read) is the one of its animations for the speed Armageddon's name is for:
///     Armageddon's player tree blends walking, running and sprinting itself;
///   - each animation moved from Mason's skeleton onto Darius's (<see cref="GuerrillaAnimations"/>),
///     under the name rfgpl_ + Guerrilla's, written into mods\guerrilla_gear\animations\, which the mod
///     loads once and keeps (as Armageddon keeps its player's), with Guerrilla's anim_files.xtbl entry:
///     its triggers (when a reload hands over the ammo), its hand IK and its props (the magazine moved
///     from the gun to the hand and back), what Armageddon's tables have of them; its sounds where the
///     guns' banks have them (a swing's whoosh: the caller's sound maker), the rest waiting for Guerrilla's
///     audio; its moving versions (a variant tagged Forward: the hammer swung on the move, which carries
///     Mason forward), made too though no group names them.
/// The lists the set and its animations go in (anim_set_filenames.xtbl, anim_files.xtbl) are shared with
/// the vehicles' and people's tools: this starts from the ones in mods\guerrilla_world\ and replaces only
/// its own entries, recorded in mods\guerrilla_gear\anim_entries.txt.
///
/// With it, a second set for a player on Mason's own skeleton: <see cref="MasonSetName"/>.
/// </summary>
public static class GuerrillaPlayerAnimations
{
    /// <summary>The set, the set it builds on and the variant that takes it up (Armageddon's anim_set_variants.xtbl).</summary>
    public const string SetName = "rfg_player", SetFile = "rfg_player.xtbl", BaseSet = "vs_player", Variant = "confident";

    /// <summary>What this writes in mods\guerrilla_gear\: the animations, the guns that take the set up (one name a line), its record of its own list entries.</summary>
    public const string AnimationFolder = "animations", WeaponList = "player_weapons.txt", OwnEntries = "anim_entries.txt";

    const string Prefix = "rfgpl_";

    /// <summary>
    /// Mason's set: Guerrilla's whole player set for a character on Mason's own skeleton (the player as
    /// Mason, the people session's character), its animations as Guerrilla has them (no skeleton to move
    /// them onto), named rfgm_ + Guerrilla's. Its groups are all of Guerrilla's player's, each as its game
    /// plays it and cut to the names Armageddon's player asks for, as Darius's; no set under it (Darius's
    /// animations can't play on Mason's bones). Then Armageddon's own (the user: Guerrilla's moves where
    /// Guerrilla has them, and Armageddon's additions too: dodging, free running, the Nano Forge, its guns):
    /// every group and name of Darius's set Mason's hasn't, with Darius's animation moved onto Mason's
    /// skeleton (rfgmd_ + its name, <see cref="PlayerRetarget"/>), or, where Darius's is one of Guerrilla's
    /// that Armageddon's developers moved onto him, Guerrilla's own; its entry (flags, interrupts) and its
    /// anim_files.xtbl entry (Armageddon's sounds included) Darius's. A name a group of Guerrilla's hasn't
    /// reaches Armageddon's through the game's own group parents (AK, then Rifle, then Default).
    /// </summary>
    public const string MasonSetName = "rfg_mason", MasonSetFile = "rfg_mason.xtbl";
    const string MasonPrefix = "rfgm_";

    /// <summary>Armageddon's own animations moved onto Mason's skeleton (<see cref="PlayerRetarget"/>), for the moves Guerrilla hasn't.</summary>
    const string ArmageddonPrefix = "rfgmd_";

    /// <summary>
    /// The backpacks' own set: Guerrilla's BackPacks (misc.vpp_pc backpacks.xtbl), which its Shockwave and Rhino packs'
    /// models name as their Anim_set (items_3d character_mesh): the moving parts of the pack itself, not of its wearer.
    /// Guerrilla's item::stream (RM 0xAA84A2) puts a streamed item's character instance in the state STAND, so a worn
    /// pack plays its set's "stand" (rhino_idle); nothing of Guerrilla's asks for its other states and actions. Its
    /// animations are for the packs' own skeletons (no person's), so they are only repacked (<see
    /// cref="GuerrillaAnimations.Recode"/>), named rfgbp_ + Guerrilla's, and loaded and kept by guerrilla_gear with the
    /// player's. The set is a second Skeleton_Set in the player's set file (rfg_player.xtbl, else rfg_mason.xtbl), as
    /// the game's set loader holds 90 set files at most and the people's build leaves only these two to this one.
    /// </summary>
    public const string BackpackSetName = "rfg_backpacks", GuerrillaBackpackSet = "BackPacks";
    const string BackpackSetTable = "backpacks.xtbl", BackpackPrefix = "rfgbp_";
    const string MasonModel = "Mason.cmeshx", MasonRig = "Mason.rigx";  // Guerrilla's rfg_PC's

    /// <summary>Mason's bones (Guerrilla's player's 52 and his camera) and those every one of Guerrilla's people's skeletons has.</summary>
    const int MasonBones = 53, SharedBones = 52;
    const string SetList = "anim_set_filenames.xtbl", FileList = "anim_files.xtbl";

    /// <summary>
    /// What the game holds (with guerrilla_vehicles), and the set loader's list of set files: their names on
    /// its stack (0x979C20), 32 bytes each from ebp−0xB48 up to its one local above them at ebp−4, so 90
    /// exactly. The people's build stops its own sets at 88 (GuerrillaPeople), leaving these two player sets
    /// (Darius's with Guerrilla's guns, Mason's) the last two.
    /// </summary>
    const int MaxAnimSets = 255, MaxSetFiles = 90, MaxAnimations = 8192;

    /// <summary>
    /// The (set, animation) map every set's states and actions go in: the game's own 6,344 places (it
    /// doesn't check, more spoil the memory after it), 16,384 with the guerrilla_gear mod.
    /// </summary>
    const int GameMapPlaces = 6344, MapPlaces = 16384;

    /// <summary>The animation groups Armageddon's code knows (0x1000128); a set naming another isn't read at all.</summary>
    static readonly HashSet<string> ArmageddonGroups = new(StringComparer.OrdinalIgnoreCase)
    {
        "Default", "AK", "Melee", "Pistol", "Revolver", "Rifle", "RPG", "Mortar", "Mine", "Hammer", "Remote Charge", "Turret Machinegun",
        "Turret Rocket", "Turret Railgun", "2HCarry", "Gutter", "Shotgun", "Sniper", "Arc", "Railgun", "Nano Rifle", "Rocket Launcher", "Unicorn",
        "Shield", "Grinder", "Enforcer", "Dual Wield", "Nanoforge", "Plasma Cannon", "Laser Pistol", "Plasma Launcher", "Spit", "Spray", "Boom",
        "Antenna", "Alien Shotgun", "Hand", "Quill", "Magnet Gun", "Machine Pistol", "Gravitic Imploder", "m16", "Plasma Rifle", "RPG Launcher",
    };

    /// <summary>Guerrilla's group names Armageddon knows under another.</summary>
    static readonly Dictionary<string, string> GroupNames = new(StringComparer.OrdinalIgnoreCase) { ["Nano"] = "Nano Rifle" };

    /// <summary>
    /// Armageddon's names Guerrilla's player hasn't, and Guerrilla's that play the same, in order of
    /// preference (an action may be played by a state's animation and the other way round: Armageddon's
    /// jump start is an action, Guerrilla's a state).
    /// </summary>
    /// <summary>
    /// Names a group of Guerrilla's means otherwise, checked before its own name. Guerrilla throws its thrown
    /// weapons with "throw item" (and "move throw ..."), and in those groups its "fire" is something else: in
    /// "remote charge" (the remote charges, the mines, the singularity bomb) the press of the detonator, in
    /// "2HCarry" (the propane tank) a swing with what he carries. Ours are thrown by the game's own throw (the
    /// weapons' "Thrown" shot, see <see cref="ThrowActions"/>), which asks for Guerrilla's throw names themselves;
    /// the charges go off with the second firing mode or reload (its "fire", "fire alt" and "reload" names), which
    /// take Guerrilla's detonator press, its "stand fire".
    /// </summary>
    static readonly Dictionary<string, Dictionary<string, string[]>> GroupAliases = new(StringComparer.OrdinalIgnoreCase)
    {
        ["remote charge"] = new(StringComparer.OrdinalIgnoreCase)
        {
            ["stand fire alt"] = ["stand fire"], ["walk fire alt"] = ["stand fire"], ["run fire alt"] = ["stand fire"],
            ["fine aim fire alt"] = ["stand fire"], ["fine aim fire walk alt"] = ["stand fire"],
            ["crouch fire alt"] = ["stand fire"], ["crouch walk fire alt"] = ["stand fire"],
            ["fine aim crouch fire alt"] = ["stand fire"], ["fine aim crouch fire walk alt"] = ["stand fire"],
            ["reload"] = ["stand fire"], ["walk reload"] = ["stand fire"], ["run reload"] = ["stand fire"],
            ["crouch reload"] = ["stand fire"], ["crouch walk reload"] = ["stand fire"],
            // The game's throw asks for "plant bomb" with a singularity bomb in hand; Guerrilla's Mason throws it as he
            // throws a charge (its group has no such name).
            ["plant bomb"] = ["throw item"],
            // Guerrilla's Mason never aims a charge: he holds it ready. Armageddon's player is in its aim names
            // whenever he holds a gun, and Armageddon's own "remote charge" group is its charge launcher's (its
            // charge_placer shares the group's name): without these the launcher's poses (pc_rcl_aim_*, moved onto
            // Mason with Armageddon's moves) filled them, and he held the charges like that launcher (the user's
            // Round 1 rerun, 2026-11-06: "the satchels still used the thrower").
            ["stand aim"] = ["stand ready"], ["aim turn"] = ["stand ready"], ["fine aim"] = ["stand ready"], ["fine aim turn"] = ["stand ready"],
            ["walk aim"] = ["walk ready"], ["fine aim walk"] = ["walk ready"],
            ["walk back aim"] = ["walk ready back"], ["fine aim walk back"] = ["walk ready back"],
            ["walk left aim"] = ["walk ready left"], ["fine aim walk left"] = ["walk ready left"],
            ["walk right aim"] = ["walk ready right"], ["fine aim walk right"] = ["walk ready right"],
            ["run aim"] = ["run ready"], ["run aim back"] = ["run ready back"],
            ["run aim left"] = ["run ready left"], ["run aim right"] = ["run ready right"],
            ["crouch aim"] = ["crouch ready"], ["crouch aim turn"] = ["crouch ready"], ["fine aim crouch"] = ["crouch ready"],
            ["crouch wlk aim fwd"] = ["crouch wlk rdy fwd"], ["fine aim crouch walk"] = ["crouch wlk rdy fwd"],
            ["crouch wlk aim back"] = ["crouch wlk rdy back"], ["fine aim crouch walk back"] = ["crouch wlk rdy back"],
            ["crouch wlk aim left"] = ["crouch wlk rdy left"], ["fine aim crouch walk left"] = ["crouch wlk rdy left"],
            ["crouch wlk aim right"] = ["crouch wlk rdy right"], ["fine aim crouch walk right"] = ["crouch wlk rdy right"],
        },
        ["2HCarry"] = new(StringComparer.OrdinalIgnoreCase)
        {
            // Guerrilla has one throw for what Mason carries in both hands, standing or on the move.
            ["move throw forward"] = ["throw item"], ["move throw back"] = ["throw item"],
            ["move throw left"] = ["throw item"], ["move throw right"] = ["throw item"],
        },
    };

    /// <summary>
    /// The actions the game's own throw asks for by name, kept in Armageddon's code from Guerrilla's (the player's
    /// attack 0x8BAAF0 takes a weapon whose shot is "Thrown" to 0x8A3FC0, which picks the action in 0x8A3DB0:
    /// "throw item" standing, "move throw ..." on the move, "grenade throw" for the class thrown_grenade, "plant
    /// bomb" for singularity_bomb, "place item" for a shot of the type "Place on Ground"; the thrown thing leaves
    /// the hand at the action's "impact start" trigger, 0x8A1E50). No weapon of Armageddon's own is thrown that
    /// way, so neither its player's blend tree nor Darius's set names them: without them here a thrown weapon's
    /// throw finds no animation and nothing is thrown.
    /// </summary>
    static readonly string[] ThrowActions =
        ["throw item", "place item", "plant bomb", "move throw forward", "move throw back", "move throw left", "move throw right", "grenade throw"];

    static readonly Dictionary<string, string[]> Aliases = new(StringComparer.OrdinalIgnoreCase)
    {
        ["fire"] = ["stand fire"],
        ["fine aim fire walk"] = ["fine aim fire"], ["fine aim crouch fire walk"] = ["fine aim crouch fire"],
        // Aiming: Guerrilla's aim names where its group has them, else its ready ones. Its weapons that are never
        // aimed (the hammer, the gutter, the charges, the tank, the launchers' plain stances) have only ready poses,
        // and Armageddon's player is in its aim names whenever he holds a weapon: without the ready ones those names
        // went to Armageddon's poses (Darius's hammer stance moved onto Mason, or his rifle's through the game's
        // group parents), stand-ins for Guerrilla's own (the user's rule; the Round 1 rerun, 2026-11-06).
        ["stand aim"] = ["stand ready"],
        ["walk aim"] = ["run aim", "walk ready"], ["walk back aim"] = ["run aim back", "walk ready back"],
        ["walk left aim"] = ["run aim left", "walk ready left"], ["walk right aim"] = ["run aim right", "walk ready right"],
        ["run aim"] = ["run ready"], ["run aim left"] = ["run ready left"], ["run aim right"] = ["run ready right"],
        ["crouch aim"] = ["crouch ready"],
        ["crouch wlk aim fwd"] = ["crouch wlk rdy fwd"], ["crouch wlk aim back"] = ["crouch wlk rdy back"],
        ["crouch wlk aim left"] = ["crouch wlk rdy left"], ["crouch wlk aim right"] = ["crouch wlk rdy right"],
        ["fine aim"] = ["stand aim", "stand ready"],
        ["fine aim walk"] = ["walk aim", "walk ready"], ["fine aim walk back"] = ["walk back aim", "walk ready back"],
        ["fine aim walk left"] = ["walk left aim", "walk ready left"], ["fine aim walk right"] = ["walk right aim", "walk ready right"],
        ["fine aim crouch"] = ["crouch aim", "crouch ready"],
        ["fine aim crouch walk"] = ["crouch wlk aim fwd", "crouch wlk rdy fwd"], ["fine aim crouch walk back"] = ["crouch wlk aim back", "crouch wlk rdy back"],
        ["fine aim crouch walk left"] = ["crouch wlk aim left", "crouch wlk rdy left"], ["fine aim crouch walk right"] = ["crouch wlk aim right", "crouch wlk rdy right"],
        ["fine aim crouch turn"] = ["fine aim crouch", "crouch aim", "crouch ready"],
        ["stand turn shuffle left"] = ["stand ready shuffle left", "stand shuffle left"], ["stand turn shuffle right"] = ["stand ready shuffle right", "stand shuffle right"],
        // A mounted turret: Armageddon's player aims it by height (high, medium, low, each straight, left or right),
        // Guerrilla's by turn (its Turret Machinegun group: turret left, slight left, middle, slight right, right).
        ["aim high"] = ["turret middle"], ["aim medium"] = ["turret middle"], ["aim low"] = ["turret middle"],
        ["aim high left"] = ["turret left"], ["aim medium left"] = ["turret left"], ["aim low left"] = ["turret left"],
        ["aim high right"] = ["turret right"], ["aim medium right"] = ["turret right"], ["aim low right"] = ["turret right"],
        ["walk high left"] = ["walk left"], ["walk medium left"] = ["walk left"], ["walk low left"] = ["walk left"],
        ["walk high right"] = ["walk right"], ["walk medium right"] = ["walk right"], ["walk low right"] = ["walk right"],
        ["ready turn"] = ["stand ready"], ["aim turn"] = ["stand aim", "ready turn", "stand ready"], ["crouch turn"] = ["crouch ready", "crouch"],
        ["crouch aim turn"] = ["crouch aim", "crouch ready"],
        ["fine aim turn"] = ["fine aim", "stand aim", "ready turn", "stand ready"], ["calm fall state aim"] = ["calm fall run state in air aim", "calm fall state"],
        ["jump start"] = ["run jump start", "stand jump start"], ["jump start aim"] = ["stand jump start aim forward", "stand jump start"],
        ["jump start crouch"] = ["stand jump start"], ["jump start back"] = ["run jump start"], ["jump start left"] = ["run jump start"],
        ["jump start right"] = ["run jump start"],
        ["land aim"] = ["land normal stop", "land normal"], ["land aim run"] = ["land run"], ["land aim run back"] = ["land run back", "land run"],
        ["land aim run left"] = ["land run"], ["land aim run right"] = ["land run"], ["land run left"] = ["land run"], ["land run right"] = ["land run"],
        ["land run back"] = ["land run"], ["land melee run"] = ["land run"], ["land melee stop"] = ["land normal stop"],
        ["crouch equip"] = ["equip"],
        ["reload upgraded"] = ["reload"], ["walk reload upgraded"] = ["walk reload"], ["run reload upgraded"] = ["run reload"],
        ["crouch reload upgraded"] = ["crouch reload"], ["crouch walk reload upgraded"] = ["crouch walk reload"],
        ["melee move"] = ["melee"], ["melee air"] = ["melee"], ["melee b"] = ["melee"], ["melee2"] = ["melee"], ["melee3"] = ["melee"],
        ["melee move b"] = ["melee"], ["melee move c"] = ["melee"], ["melee air b"] = ["melee"], ["melee air c"] = ["melee"],
        ["sprint back"] = ["run ready back"], ["sprint left"] = ["run ready left"], ["sprint right"] = ["run ready right"],
        ["run aim back"] = ["run ready back"], ["run ready back"] = ["run ready backpedal"],
    };

    /// <summary>
    /// The fields of an anim_files.xtbl entry left out for now, as the vehicles' and people's: Mason's voice
    /// lines (his voice isn't ported), the old props, and the sounds when no sound maker is given (Guerrilla's
    /// are XACT cues; with one, each becomes an event of our banks or goes).
    /// </summary>
    static readonly string[] LeftOutOfFiles = ["Sounds", "Voice_Lines", "OldProps"];

    /// <summary>One of our guns that takes the set up: its weapons.xtbl name (ours) and Guerrilla's animation group.</summary>
    public readonly record struct PlayerWeapon(string Name, string Group);

    /// <summary>A jump, fall or landing state or action (their names in Armageddon's player set).</summary>
    static bool Airborne(string id) =>
        id.StartsWith("jump start", StringComparison.OrdinalIgnoreCase) || id.StartsWith("calm fall", StringComparison.OrdinalIgnoreCase) ||
        id.StartsWith("land ", StringComparison.OrdinalIgnoreCase);

    /// <summary>
    /// Builds rfg_player for the groups <paramref name="weapons"/> use: the set's table (overrides\build\pc\cache\guerrilla\weapons\),
    /// the animations and the gun list (<paramref name="gearFolder"/>, mods\guerrilla_gear\), the shared lists
    /// (<paramref name="worldFolder"/>, mods\guerrilla_world\). <paramref name="soundOf"/>, when given, makes an
    /// animation's sound (Guerrilla's cue, looping or not) an event of our banks, or gives null to leave it out.
    /// Returns what was done, a line each.
    /// </summary>
    public static IEnumerable<string> Build(string guerrilla, string game, string overrides, string worldFolder, string gearFolder, IReadOnlyList<PlayerWeapon> weapons,
                                            Func<string, bool, string?>? soundOf = null)
    {
        var log = new List<string>();
        Make(guerrilla, game, overrides, worldFolder, gearFolder, weapons, soundOf, log);
        return log;
    }

    static void Make(string guerrilla, string game, string overrides, string worldFolder, string gearFolder, IReadOnlyList<PlayerWeapon> weapons,
                     Func<string, bool, string?>? soundOf, List<string> log)
    {
        string guerrillaCache = Path.Combine(guerrilla, "build", "pc", "cache"), gameCache = Path.Combine(game, "build", "pc", "cache");
        var guerrillaTables = Packfile.Open(Path.Combine(guerrillaCache, "misc.vpp_pc"));
        var guerrillaAnims = Packfile.Open(Path.Combine(guerrillaCache, "anims.vpp_pc"));
        var gameTables = Packfile.Open(Path.Combine(gameCache, "misc.vpp_pc"));
        XtblFile Table(Packfile pack, string name) => XtblFile.FromBytes(pack.Read(pack.Find(name) ?? throw new InvalidDataException($"no {name} in {pack.Name}")));
        XtblFile Shared(string name) => File.Exists(Path.Combine(worldFolder, name)) ? XtblFile.FromBytes(File.ReadAllBytes(Path.Combine(worldFolder, name))) : Table(gameTables, name);

        // The shared lists without this tool's entries of an earlier run.
        var setList = Shared(SetList);
        var fileList = Shared(FileList);
        var fileEntries = fileList.Table?.Element("Files")?.Element("Anim_files") ?? throw new InvalidDataException($"the {FileList} has no Files/Anim_files");
        var earlier = ReadOwn(gearFolder);
        foreach (var e in setList.Entries.Where(e => earlier.Sets.Contains(e.ValueOf("Name")?.Trim() ?? "")).ToList()) e.Remove();
        foreach (var e in fileEntries.Elements.Where(e => earlier.Files.Contains(AnimationName(e) ?? "")).ToList()) e.Remove();

        // Guerrilla's player set, its groups' parents, its blend trees and its animations' entries.
        var rfgSet = Table(guerrillaTables, "player.xtbl").Entries.FirstOrDefault(e => e.Name.Equals("Skeleton_Set", StringComparison.OrdinalIgnoreCase))
                     ?? throw new InvalidDataException("Guerrilla's player.xtbl has no Skeleton_Set");
        var rfgGroups = new Dictionary<string, XtblElement>(StringComparer.OrdinalIgnoreCase);
        foreach (var g in rfgSet.Element("Groups")?.Elements ?? [])
            if (g.ValueOf("group")?.Trim() is { Length: > 0 } name) rfgGroups.TryAdd(name, g);
        var rfgParents = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var e in Table(guerrillaTables, "anim_groups.xtbl").Entries)
            if (e.ValueOf("Parent")?.Trim() is { Length: > 0 } parent) rfgParents[XtblFile.EntryName(e)] = parent;
        var trees = new Dictionary<string, XtblElement>(StringComparer.OrdinalIgnoreCase);
        foreach (var e in Table(guerrillaTables, "anim_blend_trees.xtbl").Entries) trees.TryAdd(XtblFile.EntryName(e), e);
        var rfgFiles = new Dictionary<string, XtblElement>(StringComparer.OrdinalIgnoreCase);
        foreach (var e in Table(guerrillaTables, FileList).Table?.Element("Files")?.Element("Anim_files")?.Elements ?? [])
            if (AnimationName(e) is { } file) rfgFiles.TryAdd(file, e);

        // What Armageddon's player asks for: its blend tree's names and Darius's set's.
        var (askedStates, askedActions) = Asked(gameTables, Table(gameTables, BaseSet + ".xtbl"));
        // And what the player's melee moves play (Guerrilla's melee.xtbl, each move's AttackAnim for Player.xtbl): the
        // game plays a move's action by its name from the swinging weapon's group, so a group cut to the names above
        // left out the Gutter's combo swings ("melee slow", "melee fast", "melee fast slow", ...; the user, 2026-11-13:
        // Mason "cant actually use it as there is no attack animations for it"). Only names the action list knows
        // (the shared anim_actions.xtbl, else Armageddon's own) are kept.
        var knownActions = Shared("anim_actions.xtbl").Entries.Select(e => e.ValueOf("Name")?.Trim()).OfType<string>()
                                                         .ToHashSet(StringComparer.OrdinalIgnoreCase);
        var meleeActions = new List<string>();
        foreach (var move in Table(guerrillaTables, "melee.xtbl").Entries)
            foreach (var animSet in Descendants(move, "AnimSet"))
                if (string.Equals(animSet.ValueOf("AnimSetName")?.Trim(), "Player.xtbl", StringComparison.OrdinalIgnoreCase) &&
                    animSet.ValueOf("AttackAnim")?.Trim() is { Length: > 0 } attack &&
                    !askedActions.Contains(attack, StringComparer.OrdinalIgnoreCase) && !meleeActions.Contains(attack, StringComparer.OrdinalIgnoreCase))
                {
                    if (knownActions.Contains(attack)) meleeActions.Add(attack);
                    else log.Add($"player animations: melee action \"{attack}\" isn't in the action list; its moves play nothing for the player");
                }
        askedActions.AddRange(meleeActions);
        if (meleeActions.Count > 0) log.Add($"player animations: {meleeActions.Count} actions the player's melee moves play, kept in each group that has them: {string.Join(", ", meleeActions)}");

        // What Armageddon's tables have of an animation's entry.
        HashSet<string> Names(string table, string field = "Name") =>
            Table(gameTables, table).Entries.Select(e => e.ValueOf(field)?.Trim()).OfType<string>().ToHashSet(StringComparer.OrdinalIgnoreCase);
        var known = new KnownParts(Names("anim_triggers.xtbl"), Names("anim_ik_location.xtbl"), Names("anim_files_flags.xtbl", "XML_Name"), Names("prop_system_type.xtbl"));

        // The skeletons: Guerrilla's people's by bone count (Mason's for 53), Darius's.
        var peopleRigs = PeopleRigs(guerrillaTables);
        var darius = Rig.Read(gameTables.Read(gameTables.Find("player_v2.rig_pc") ?? throw new InvalidDataException("no player_v2.rig_pc in Armageddon's misc.vpp_pc")));

        // The groups. Darius's set: each group our guns use, once, in Armageddon's name. Mason's: every
        // group of Guerrilla's player.
        var dariusGroups = new List<(string Rfg, string Ours)>();
        var weaponsKept = new List<string>();
        foreach (var weapon in weapons)
        {
            string rfg = weapon.Group;
            string ours = GroupNames.TryGetValue(rfg, out var renamed) ? renamed : rfg;
            if (!rfgGroups.ContainsKey(rfg) && !rfgParents.ContainsKey(rfg)) { log.Add($"player animations: {weapon.Name}'s group {rfg} isn't one of Guerrilla's player's; it plays Darius's"); continue; }
            if (!ArmageddonGroups.Contains(ours)) { log.Add($"player animations: {weapon.Name}'s group {rfg} isn't one Armageddon knows; it plays Darius's"); continue; }
            if (!dariusGroups.Any(g => g.Ours.Equals(ours, StringComparison.OrdinalIgnoreCase))) dariusGroups.Add((rfg, ours));
            weaponsKept.Add(weapon.Name);
        }
        var masonGroups = new List<(string Rfg, string Ours)>();
        foreach (string rfg in rfgGroups.Keys)
        {
            string ours = GroupNames.TryGetValue(rfg, out var renamed) ? renamed : rfg;
            if (!ArmageddonGroups.Contains(ours)) { log.Add($"Mason's animations: Guerrilla's group {rfg} isn't one Armageddon knows; left out"); continue; }
            if (!masonGroups.Any(g => g.Ours.Equals(ours, StringComparison.OrdinalIgnoreCase))) masonGroups.Add((rfg, ours));
        }

        // Armageddon's animations, and the converter between the two skeletons learnt from them (PlayerRetarget).
        var gameAnims = Packfile.Open(Path.Combine(gameCache, "anims.vpp_pc"));
        var gameFileEntries = new Dictionary<string, XtblElement>(StringComparer.OrdinalIgnoreCase);
        foreach (var e in Table(gameTables, FileList).Table?.Element("Files")?.Element("Anim_files")?.Elements ?? [])
            if (AnimationName(e) is { } file) gameFileEntries.TryAdd(file, e);
        PlayerRetarget? retarget = null;
        try
        {
            retarget = PlayerRetarget.Fit(guerrillaTables, guerrillaAnims, gameTables, gameAnims);
            log.Add($"player animations: moved between Mason's skeleton and Darius's as learnt from {retarget.Pairs} of Guerrilla's player animations Armageddon's developers moved onto Darius");
        }
        catch (InvalidDataException e) { log.Add($"player animations: no pairs to learn the skeletons from ({e.Message}); Darius's are moved bone for bone, Armageddon's moves left out of Mason's"); }
        // Guerrilla's animations Armageddon has itself on Darius (its developers' versions, preloaded): used by their names.
        var armageddonOwn = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        var animations = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);    // our .anim_pc → its bytes
        var dariusFiles = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);   // Guerrilla's x.animx → ours, moved onto Darius
        var masonFiles = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);    // Guerrilla's x.animx → ours, for Mason's own skeleton
        var unreadable = new List<string>();
        // One of Guerrilla's animations for one of the sets: on Darius, Armageddon's own version where it has one
        // (by its name), else moved onto his skeleton; or repacked for Mason's.
        string? Clip(string rfgFile, bool onDarius)
        {
            var made = onDarius ? dariusFiles : masonFiles;
            if (made.TryGetValue(rfgFile, out var done)) return done;
            if (unreadable.Any(u => u.StartsWith(rfgFile + " ", StringComparison.OrdinalIgnoreCase))) return null;
            if (onDarius && gameFileEntries.ContainsKey(rfgFile) && gameAnims.Find(Path.ChangeExtension(rfgFile, ".anim_pc")) is { } own)
            {
                try
                {
                    if (Animation.Read(gameAnims.Read(own)).SkeletonBones == darius.Bones.Count)
                    {
                        armageddonOwn.Add(rfgFile);
                        return made[rfgFile] = rfgFile;
                    }
                }
                catch (Exception e) when (e is InvalidDataException or ArgumentException or IndexOutOfRangeException) { }
            }
            var entry = guerrillaAnims.Find(Path.ChangeExtension(rfgFile, ".anim_pc"));
            if (entry == null) { unreadable.Add($"{rfgFile} (in none of Guerrilla's files)"); return null; }
            byte[] bytes = guerrillaAnims.Read(entry);
            bool additive = rfgFiles.TryGetValue(rfgFile, out var rfgEntry) &&
                            (rfgEntry.Element("Flags")?.Elements.Any(f => f.Value.Trim().Equals("Additive", StringComparison.OrdinalIgnoreCase)) ?? false);
            string ours = (onDarius ? Prefix : MasonPrefix) + rfgFile.ToLowerInvariant();
            try
            {
                if (bytes.Length < 0x30 || !peopleRigs.TryGetValue(bytes[0xB], out var skeleton))
                { unreadable.Add($"{rfgFile} (not for a person's skeleton)"); return null; }
                animations[Path.ChangeExtension(ours, ".anim_pc")] = !onDarius ? ForMason(bytes)
                    : retarget != null ? retarget.ToDarius(bytes, skeleton, additive)
                    : additive ? GuerrillaAnimations.RetargetAdditive(bytes, skeleton, darius) : GuerrillaAnimations.Retarget(bytes, skeleton, darius);
            }
            catch (Exception e) when (e is InvalidDataException or ArgumentException or IndexOutOfRangeException) { unreadable.Add($"{rfgFile} ({e.Message})"); return null; }
            return made[rfgFile] = ours;
        }

        // A set's table: each group as Guerrilla's game plays it (its own entries, then its parents'),
        // cut to the names Armageddon's player asks for, each Guerrilla's entry of that name or one that
        // plays the same. Returns how many states and actions it has.
        int MakeSet(XtblElement skeletonSet, List<(string Rfg, string Ours)> groups, bool onDarius, string who, string rest)
        {
            var setGroups = skeletonSet.AddElement("Groups");
            int places = 0;
            foreach (var (rfg, ours) in groups)
            {
                var states = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                var actions = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                var chain = new List<string>();
                for (string? g = rfg; g != null && !chain.Contains(g, StringComparer.OrdinalIgnoreCase); g = rfgParents.GetValueOrDefault(g))
                {
                    chain.Add(g);
                    if (!rfgGroups.TryGetValue(g, out var group)) continue;
                    foreach (var s in group.Element("States")?.Elements ?? [])
                        if (s.ValueOf("ID")?.Trim() is { Length: > 0 } id && s.Element("Animation")?.ValueOf("Filename")?.Trim() is { Length: > 0 } f) states.TryAdd(id, f);
                    foreach (var a in group.Element("Actions")?.Elements ?? [])
                        if (a.ValueOf("ID")?.Trim() is { Length: > 0 } id && a.Element("Animation")?.ValueOf("Filename")?.Trim() is { Length: > 0 } f) actions.TryAdd(id, f);
                }
                var outGroup = setGroups.AddElement("Group");
                outGroup.AddElement("group", ours);
                var outStates = outGroup.AddElement("States");
                var outActions = outGroup.AddElement("Actions");
                int direct = 0, aliased = 0, missingClip = 0;
                void Add(XtblElement list, string kind, string id, bool isState)
                {
                    // Darius jumps as Armageddon has him (his own clips, the base set's): Guerrilla's carry a hop of
                    // their own in the root track, which kept him a hand's height off the ground.
                    if (onDarius && Airborne(id)) return;
                    string? source = null;
                    bool alias = false;
                    var (mine, other) = isState ? (states, actions) : (actions, states);
                    string? f = null;
                    if (GroupAliases.TryGetValue(rfg, out var meant) && meant.TryGetValue(id, out var instead))
                        foreach (string alt in instead)
                            if (mine.TryGetValue(alt, out f) || other.TryGetValue(alt, out f)) { source = f; alias = true; break; }
                    if (source == null && mine.TryGetValue(id, out f)) source = f;
                    if (source == null)
                        foreach (string alt in Aliases.GetValueOrDefault(id) ?? [])
                            if (mine.TryGetValue(alt, out f) || other.TryGetValue(alt, out f)) { source = f; alias = true; break; }
                    if (source == null) return;
                    string? file = source.EndsWith(".animx", StringComparison.OrdinalIgnoreCase) ? source
                                 : trees.TryGetValue(source, out var tree) ? BandClip(tree, id) : null;
                    if (file == null || Clip(file, onDarius) is not { } clip) { missingClip++; return; }
                    var entry = list.AddElement(kind);
                    entry.AddElement("ID", id);
                    entry.AddElement("Animation").AddElement("Filename", clip);
                    if (alias) aliased++; else direct++;
                }
                foreach (string id in askedStates) Add(outStates, "State", id, true);
                foreach (string id in askedActions) Add(outActions, "Action", id, false);
                int count = outStates.Elements.Count() + outActions.Elements.Count();
                places += count;
                log.Add($"{who}: group {ours} (Guerrilla's {string.Join(" > ", chain)}): {count} of the {askedStates.Count + askedActions.Count} names Armageddon's player asks for, " +
                        $"{direct} Guerrilla's own, {aliased} from one that plays the same; {rest}" +
                        (missingClip > 0 ? $"; {missingClip} left out, their animations unreadable" : ""));
            }
            skeletonSet.AddElement("_Editor").AddElement("Category", "Entries");
            return places;
        }

        // Darius's: built on his set, taken up by the variant (see the summary).
        var dariusSet = new XtblFile("<root><Table></Table></root>");
        var dariusSkeletonSet = dariusSet.Table!.AddElement("Skeleton_Set");
        dariusSkeletonSet.AddElement("Name", SetName);
        dariusSkeletonSet.AddElement("Parent_Skeleton", BaseSet);
        dariusSkeletonSet.AddElement("Variant_Type", Variant);
        int dariusPlaces = MakeSet(dariusSkeletonSet, dariusGroups, true, "player animations", "the rest stay Darius's");
        // Mason's: a set of its own (Darius's can't play on his bones), a character's whole set.
        var masonSet = new XtblFile("<root><Table></Table></root>");
        var masonSkeletonSet = masonSet.Table!.AddElement("Skeleton_Set");
        masonSkeletonSet.AddElement("Name", MasonSetName);
        int masonPlaces = MakeSet(masonSkeletonSet, masonGroups, false, "Mason's animations", "the rest from Armageddon's (below)");

        // The backpacks' own set (see BackpackSetName): Guerrilla's as it is, each state and action Armageddon's
        // tables know, its animations repacked.
        var backpackFiles = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);  // Guerrilla's x.animx → ours
        var backpackSkeletonSet = new XtblElement("Skeleton_Set");
        int backpackPlaces = 0;
        if (guerrillaTables.Find(BackpackSetTable) is null)
            log.Add($"backpacks' set: Guerrilla's {BackpackSetTable} isn't in its misc.vpp_pc; the Shockwave and Rhino packs hold still");
        else if (Table(guerrillaTables, BackpackSetTable).Entries.FirstOrDefault(e => e.Name.Equals("Skeleton_Set", StringComparison.OrdinalIgnoreCase) &&
                     string.Equals(e.ValueOf("Name")?.Trim(), GuerrillaBackpackSet, StringComparison.OrdinalIgnoreCase)) is not { } rfgBackpacks)
            log.Add($"backpacks' set: Guerrilla's {BackpackSetTable} has no set {GuerrillaBackpackSet}; the Shockwave and Rhino packs hold still");
        else
        {
            var gameStates = Names("anim_states.xtbl");
            var gameActions = Names("anim_actions.xtbl");
            var unknown = new List<string>();
            backpackSkeletonSet.AddElement("Name", BackpackSetName);
            var outGroups = backpackSkeletonSet.AddElement("Groups");
            foreach (var group in rfgBackpacks.Element("Groups")?.Elements ?? [])
            {
                string groupName = group.ValueOf("group")?.Trim() ?? "";
                if (!ArmageddonGroups.Contains(groupName)) { unknown.Add($"group {groupName}"); continue; }
                var outGroup = outGroups.AddElement("Group");
                outGroup.AddElement("group", groupName);
                foreach (var (list, kind, gameNamed) in new[] { ("States", "State", gameStates), ("Actions", "Action", gameActions) })
                {
                    var outList = outGroup.AddElement(list);
                    foreach (var item in group.Element(list)?.Elements ?? [])
                    {
                        if (item.ValueOf("ID")?.Trim() is not { Length: > 0 } id || item.Element("Animation")?.ValueOf("Filename")?.Trim() is not { Length: > 0 } rfgFile) continue;
                        if (!gameNamed.Contains(id)) { unknown.Add($"{kind.ToLowerInvariant()} \"{id}\""); continue; }
                        if (!backpackFiles.TryGetValue(rfgFile, out var ours))
                        {
                            if (guerrillaAnims.Find(Path.ChangeExtension(rfgFile, ".anim_pc")) is not { } clip) { unreadable.Add($"{rfgFile} (in none of Guerrilla's files)"); continue; }
                            ours = BackpackPrefix + rfgFile.ToLowerInvariant();
                            try { animations[Path.ChangeExtension(ours, ".anim_pc")] = GuerrillaAnimations.Recode(guerrillaAnims.Read(clip)); }
                            catch (Exception e) when (e is InvalidDataException or ArgumentException or IndexOutOfRangeException) { unreadable.Add($"{rfgFile} ({e.Message})"); continue; }
                            backpackFiles[rfgFile] = ours;
                        }
                        var entry = outList.AddElement(kind);
                        entry.AddElement("ID", id);
                        entry.AddElement("Animation").AddElement("Filename", ours);
                        backpackPlaces++;
                    }
                }
            }
            backpackSkeletonSet.AddElement("_Editor").AddElement("Category", "Entries");
            log.Add($"backpacks' set: {BackpackSetName} (Guerrilla's {GuerrillaBackpackSet}): {backpackPlaces} states and actions, {backpackFiles.Count} animations repacked" +
                    (unknown.Count > 0 ? $"; left out, Armageddon's tables haven't them: {string.Join(", ", unknown)}" : ""));
        }

        // Armageddon's own moves for Mason (see MasonSetName).
        var armageddonFiles = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);  // Armageddon's x.animx → ours, on Mason
        // One of Darius's animations for Mason: Guerrilla's own if it is one of Guerrilla's, else moved.
        string? FromDarius(string file)
        {
            if (guerrillaAnims.Find(Path.ChangeExtension(file, ".anim_pc")) != null) return Clip(file, false);
            if (armageddonFiles.TryGetValue(file, out var done)) return done;
            if (retarget == null || unreadable.Any(u => u.StartsWith(file + " ", StringComparison.OrdinalIgnoreCase))) return null;
            var entry = gameAnims.Find(Path.ChangeExtension(file, ".anim_pc"));
            if (entry == null) { unreadable.Add($"{file} (in none of Armageddon's preloaded animations)"); return null; }
            bool additive = gameFileEntries.TryGetValue(file, out var gameEntry) &&
                            (gameEntry.Element("Flags")?.Elements.Any(f => f.Value.Trim().Equals("Additive", StringComparison.OrdinalIgnoreCase)) ?? false);
            string ours = ArmageddonPrefix + file.ToLowerInvariant();
            try { animations[Path.ChangeExtension(ours, ".anim_pc")] = retarget.ToMason(gameAnims.Read(entry), additive); }
            catch (Exception e) when (e is InvalidDataException or ArgumentException or IndexOutOfRangeException) { unreadable.Add($"{file} ({e.Message})"); return null; }
            return armageddonFiles[file] = ours;
        }
        if (retarget != null)
        {
            var masonSetGroups = masonSkeletonSet.Element("Groups")!;
            var byName = masonSetGroups.Elements.ToDictionary(g => g.ValueOf("group")?.Trim() ?? "", StringComparer.OrdinalIgnoreCase);
            int added = 0, newGroups = 0, missing = 0;
            foreach (var group in Table(gameTables, BaseSet + ".xtbl").Entries.SelectMany(e => Descendants(e, "Group")))
            {
                string name = group.ValueOf("group")?.Trim() ?? "";
                if (name.Length == 0) continue;
                if (!byName.TryGetValue(name, out var ours))
                {
                    ours = masonSetGroups.AddElement("Group");
                    ours.AddElement("group", name);
                    ours.AddElement("States");
                    ours.AddElement("Actions");
                    byName[name] = ours;
                    newGroups++;
                }
                foreach (var (kind, list) in new[] { ("State", "States"), ("Action", "Actions") })
                {
                    var into = ours.Element(list) ?? ours.AddElement(list);
                    var have = into.Elements.Select(x => x.ValueOf("ID")?.Trim() ?? "").ToHashSet(StringComparer.OrdinalIgnoreCase);
                    foreach (var item in group.Element(list)?.ElementsNamed(kind) ?? [])
                    {
                        string id = item.ValueOf("ID")?.Trim() ?? "";
                        string file = item.Element("Animation")?.ValueOf("Filename")?.Trim() ?? "";
                        if (id.Length == 0 || file.Length == 0 || !have.Add(id)) continue;
                        if (FromDarius(file) is not { } clip) { missing++; continue; }
                        var copy = item.Copy();
                        copy.Element("Animation")!.Element("Filename")!.Value = clip;
                        into.AddElement(copy);
                        added++;
                    }
                }
            }
            masonPlaces += added;
            log.Add($"Mason's animations: Armageddon's own moves added: {added} states and actions (of Darius's {newGroups} groups Guerrilla's player hasn't, and the names Guerrilla's groups lack), " +
                    $"{armageddonFiles.Count} of Darius's animations moved onto Mason" + (missing > 0 ? $"; {missing} left out, their animations unreadable" : ""));
        }
        // Jumping is Armageddon's (the user: Armageddon's movement). Its controller lifts the player itself,
        // and its jump clips keep the character on the ground (pc_rfl_jmprun's root track: 4 cm forward,
        // no rise); Guerrilla's carry their own hop in the root track (plyr_rfl_jmprun: up 0.24 m and back
        // down by frame 60, 0.49 m forward), which the game plays as the character's own movement, so Mason
        // hopped a hand's height and no more. Every jump, fall and landing name in Mason's groups takes
        // Darius's clip for it (moved onto Mason), from Darius's group of that name or the game's parent of
        // it (anim_groups.xtbl: AK, then Rifle); where that chain has none (Guerrilla's RPG, Rocket Launcher,
        // Grinder, Arc and 2HCarry groups), from his Rifle group, as Armageddon's own launchers (Plasma
        // Launcher, Gravitic Imploder, ...) take theirs; where Darius has none at all, Guerrilla's stays.
        if (retarget != null)
        {
            var gameParents = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var e in Table(gameTables, "anim_groups.xtbl").Entries)
                if (e.ValueOf("Parent")?.Trim() is { Length: > 0 } parent) gameParents[XtblFile.EntryName(e)] = parent;
            var dariusByGroup = new Dictionary<string, Dictionary<string, string>>(StringComparer.OrdinalIgnoreCase);
            foreach (var group in Table(gameTables, BaseSet + ".xtbl").Entries.SelectMany(e => Descendants(e, "Group")))
            {
                string groupName = group.ValueOf("group")?.Trim() ?? "";
                if (!dariusByGroup.TryGetValue(groupName, out var names)) dariusByGroup[groupName] = names = new(StringComparer.OrdinalIgnoreCase);
                foreach (var item in (group.Element("States")?.Elements ?? []).Concat(group.Element("Actions")?.Elements ?? []))
                    if (item.ValueOf("ID")?.Trim() is { Length: > 0 } id && item.Element("Animation")?.ValueOf("Filename")?.Trim() is { Length: > 0 } file)
                        names.TryAdd(id, file);
            }
            int jumps = 0, kept = 0, fromRifle = 0;
            foreach (var group in masonSkeletonSet.Element("Groups")!.Elements)
            {
                string name = group.ValueOf("group")?.Trim() ?? "";
                foreach (var item in (group.Element("States")?.Elements ?? []).Concat(group.Element("Actions")?.Elements ?? []))
                {
                    string id = item.ValueOf("ID")?.Trim() ?? "";
                    if (!Airborne(id) || item.Element("Animation")?.Element("Filename") is not { } filename) continue;
                    string? dariusFile = null;
                    var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                    for (string? g = name; g != null && seen.Add(g) && dariusFile == null; g = gameParents.GetValueOrDefault(g))
                        if (dariusByGroup.TryGetValue(g, out var names)) names.TryGetValue(id, out dariusFile);
                    if (dariusFile == null && dariusByGroup.TryGetValue("Rifle", out var rifle) && rifle.TryGetValue(id, out dariusFile)) fromRifle++;
                    if (dariusFile != null && FromDarius(dariusFile) is { } clip) { filename.Value = clip; jumps++; }
                    else kept++;
                }
            }
            log.Add($"Mason's animations: jumping as Armageddon's: {jumps} jump, fall and landing entries take Darius's clip" +
                    (fromRifle > 0 ? $" ({fromRifle} from his Rifle group, as Armageddon's launchers)" : "") +
                    (kept > 0 ? $", {kept} keep Guerrilla's (Darius has none for them)" : ""));
        }
        // Empty-handed: Guerrilla's Mason always carries his sledgehammer, so no set has him walk with
        // nothing in his hands (Darius's Default group hasn't either: he always has his). The game plays
        // Default for a player with no weapon, and with only its idles he stands still or T-poses. While
        // Guerrilla's hammer isn't built, every name his hammer group has and Default lacks is the hammer
        // group's, Guerrilla's own Mason clips: with nothing in his hands he moves as he does with the
        // hammer. Built, it is in his hands as in Guerrilla (the guerrilla_gear mod gives it and keeps it),
        // and Default is Guerrilla's own.
        if (weapons.Any(w => w.Group.Equals("Hammer", StringComparison.OrdinalIgnoreCase)))
            log.Add("Mason's animations: empty-handed (Default) is Guerrilla's own (his sledgehammer is built: he always carries it)");
        else
        {
            var groups = masonSkeletonSet.Element("Groups")!.Elements.ToDictionary(g => g.ValueOf("group")?.Trim() ?? "", StringComparer.OrdinalIgnoreCase);
            if (groups.TryGetValue("Default", out var unarmed) && groups.TryGetValue("Hammer", out var hammer))
            {
                int fromHammer = 0;
                foreach (var (kind, list) in new[] { ("State", "States"), ("Action", "Actions") })
                {
                    var into = unarmed.Element(list) ?? unarmed.AddElement(list);
                    var have = into.Elements.Select(x => x.ValueOf("ID")?.Trim() ?? "").ToHashSet(StringComparer.OrdinalIgnoreCase);
                    foreach (var item in hammer.Element(list)?.ElementsNamed(kind) ?? [])
                        if (have.Add(item.ValueOf("ID")?.Trim() ?? "")) { into.AddElement(item.Copy()); fromHammer++; }
                }
                masonPlaces += fromHammer;
                log.Add($"Mason's animations: empty-handed (Default) takes {fromHammer} states and actions from his hammer group");
            }
        }
        // The moving versions: Guerrilla's entry for a swing plays another animation in its place when the player
        // swings on the move (a variant tagged Forward: the hammer's plyr_hmr_mele plays plyr_hmr_melemv, which
        // carries him forward; the gutter's, the guns' bashes alike), and Armageddon plays variants the same way
        // (its own dual-wield swing, pc_dw_melee, has pc_dw_melee_mv). No group names those animations, so they
        // are made here, for the set whose animation names them. The other tags stay out: Air (a swing in a jump,
        // where Armageddon's movement is kept) and the rest.
        int movingVersions = 0;
        foreach (var (made, onDarius) in new[] { (masonFiles, false), (dariusFiles, true) })
            for (bool more = true; more;)
            {
                more = false;
                foreach (string rfgFile in made.Keys.ToList())
                {
                    if (onDarius && armageddonOwn.Contains(rfgFile)) continue;  // Armageddon's own, its own entry
                    foreach (var variant in rfgFiles.GetValueOrDefault(rfgFile)?.Element("Variants")?.ElementsNamed("Variant") ?? [])
                        if (variant.ValueOf("Anim_Tag")?.Trim().Equals("Forward", StringComparison.OrdinalIgnoreCase) == true &&
                            variant.ValueOf("File")?.Trim() is { Length: > 0 } file && !made.ContainsKey(file) && Clip(file, onDarius) != null)
                        { movingVersions++; more = true; }
                }
            }
        if (movingVersions > 0) log.Add($"player animations: {movingVersions} moving versions (Guerrilla's variants tagged Forward: swings on the move) made");
        if (unreadable.Count > 0) log.Add($"player animations: animations left out: {string.Join(", ", unreadable.Distinct())}");

        // Each animation's entry, Guerrilla's, with what Armageddon's tables have of it.
        var gameNames = new HashSet<string>(fileEntries.Elements.Select(AnimationName).OfType<string>(), StringComparer.OrdinalIgnoreCase);
        var leftOut = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        var ownFiles = new List<string>();
        foreach (var made in new[] { dariusFiles, masonFiles, backpackFiles })
            foreach (var (rfgFile, ours) in made)
            {
                if (made == dariusFiles && armageddonOwn.Contains(rfgFile)) continue;  // Armageddon's own, its own entry
                if (gameNames.Contains(ours)) throw new InvalidDataException($"{FileList} already has an animation {ours} that isn't ours (another tool's): the names would clash");
                fileEntries.AddElement(FileEntry(rfgFiles.GetValueOrDefault(rfgFile), ours, made, known, leftOut, soundOf));
                ownFiles.Add(ours);
            }
        // Armageddon's own moved onto Mason: Armageddon's own entry (its triggers, IKs, props, sounds), under our name.
        foreach (var (gameFile, ours) in armageddonFiles)
        {
            if (gameNames.Contains(ours)) throw new InvalidDataException($"{FileList} already has an animation {ours} that isn't ours (another tool's): the names would clash");
            var entry = gameFileEntries.TryGetValue(gameFile, out var game0) ? game0.Copy() : new XtblElement("Anim_file");
            var animation = entry.Element("Animation") ?? entry.AddElement("Animation");
            if (animation.Element("Filename") is { } filename) filename.Value = ours;
            else animation.AddElement("Filename", ours);
            if (animation.Element("Preload") is { } preload) preload.Value = "False";
            else animation.AddElement("Preload", "False");
            fileEntries.AddElement(entry);
            ownFiles.Add(ours);
        }
        if (leftOut.Count > 0) log.Add($"player animations: left out of their entries (Armageddon hasn't them, or until Guerrilla's audio is ported): {string.Join(", ", leftOut)}");

        // Room for the sets.
        bool hasDarius = dariusGroups.Count > 0 && dariusPlaces > 0, hasMason = masonGroups.Count > 0 && masonPlaces > 0;
        int otherSetFiles = setList.Entries.Count();
        int otherSets = 0;
        foreach (string file in setList.Entries.Select(e => e.ValueOf("Name")?.Trim()).OfType<string>())
            otherSets += gameTables.Find(file) is { } f ? Math.Max(1, XtblFile.FromBytes(gameTables.Read(f)).Entries.Count(e => e.Name.Equals("Skeleton_Set", StringComparison.OrdinalIgnoreCase))) : 1;
        if (gameTables.Find("anim_rig_objects.xtbl") is { } objects)
            otherSets += XtblFile.FromBytes(gameTables.Read(objects)).Entries.Count(e => e.Name.Equals("Anim_Rig_Object", StringComparison.OrdinalIgnoreCase));
        int adding = (hasDarius ? 1 : 0) + (hasMason ? 1 : 0);
        if (adding > 0 && (otherSets + adding > MaxAnimSets || otherSetFiles + adding > MaxSetFiles))
        {
            log.Add($"player animations: left out, the game has no room for {adding} more sets (sets {otherSets} of {MaxAnimSets}, set files {otherSetFiles} of {MaxSetFiles})");
            hasDarius = hasMason = false;
        }
        // The backpacks' set rides in one of those files: a set more, no file more.
        bool hasBackpacks = backpackPlaces > 0 && (hasDarius || hasMason);
        if (hasBackpacks && otherSets + adding + 1 > MaxAnimSets)
        {
            log.Add($"backpacks' set: left out, the game has no room for another set (sets {otherSets + adding} of {MaxAnimSets}); the Shockwave and Rhino packs hold still");
            hasBackpacks = false;
        }
        else if (backpackPlaces > 0 && !hasBackpacks) log.Add("backpacks' set: left out with the player's sets (it rides in their file); the Shockwave and Rhino packs hold still");
        if (hasBackpacks) (hasDarius ? dariusSet : masonSet).Table!.AddElement(backpackSkeletonSet);
        // A set left out takes its animations with it.
        foreach (var (has, made) in new[] { (hasDarius, dariusFiles), (hasMason, masonFiles), (hasMason, armageddonFiles), (hasBackpacks, backpackFiles) })
        {
            if (has) continue;
            foreach (string ours in made.Values.Where(o => !armageddonOwn.Contains(o)))
            {
                fileEntries.Elements.FirstOrDefault(x => string.Equals(AnimationName(x), ours, StringComparison.OrdinalIgnoreCase))?.Remove();
                ownFiles.Remove(ours);
                animations.Remove(Path.ChangeExtension(ours, ".anim_pc"));
            }
        }
        if (!hasDarius) weaponsKept.Clear();
        log.Add(AnimTriggerPools.Check(fileList));  // refuses (throws) before anything is written

        // Armageddon's moves on Mason keep Darius's camera-lookat track for a bone of that name after
        // Mason's own (PlayerRetarget.WithCameraLookat, which the people's build applies to the skeleton it
        // writes); a skeleton written before that gets it here, so those animations never name a bone it hasn't.
        string? masonRigFile = OverrideLayout.Find(overrides, "mason.rig_pc");  // the people part's
        if (hasMason && masonRigFile != null && gameTables.Find("player_v2.rig_pc") is { } dariusRigEntry)
        {
            byte[] installed = File.ReadAllBytes(masonRigFile);
            byte[] withLookat = PlayerRetarget.WithCameraLookat(installed, gameTables.Read(dariusRigEntry));
            if (withLookat.Length != installed.Length)
            {
                File.WriteAllBytes(masonRigFile, withLookat);
                log.Add($"Mason's skeleton: {PlayerRetarget.CameraLookat} bone added (mason.rig_pc)");
            }
        }

        Directory.CreateDirectory(worldFolder);
        Directory.CreateDirectory(gearFolder);
        string folder = Path.Combine(gearFolder, AnimationFolder);
        Directory.CreateDirectory(folder);
        foreach (string old in Directory.EnumerateFiles(folder, "*.anim_pc").ToList()) File.Delete(old);
        foreach (var (has, file, table, listedAs) in new[] { (hasDarius, SetFile, dariusSet, "darius"), (hasMason, MasonSetFile, masonSet, "mason") })
        {
            if (!has) { OverrideLayout.Delete(overrides, file); continue; }
            string path = OverrideLayout.NewFile(overrides, OverrideLayout.Weapons, file);
            // Darius's set is listed as his own (his model and skeleton), Mason's as Guerrilla lists its player's.
            var listing = listedAs == "darius"
                ? setList.Entries.FirstOrDefault(e => string.Equals(e.ValueOf("Name")?.Trim(), BaseSet + ".xtbl", StringComparison.OrdinalIgnoreCase))?.Copy()
                : Table(guerrillaTables, SetList).Entries.FirstOrDefault(e => string.Equals(e.ValueOf("Name")?.Trim(), "player.xtbl", StringComparison.OrdinalIgnoreCase))?.Copy();
            listing ??= new XtblElement("Anim_Set_Filename");
            if (listing.Element("Name") is { } listedName) listedName.Value = file;
            else listing.AddElement("Name", file);
            // Guerrilla's lists its player's set with Player_.cmeshx and Player.rigx; Armageddon has a
            // player.rig_pc of its own, so Mason's names his own model and skeleton (Guerrilla's rfg_PC's).
            if (listedAs == "mason")
                foreach (var (field, value) in new[] { ("Default_model", MasonModel), ("Default_rig", MasonRig) })
                    if (listing.Element(field) is { } part) part.Value = value;
                    else listing.AddElement(field, value);
            setList.Table!.AddElement(listing);
            File.WriteAllBytes(path, table.ToBytes());
        }
        foreach (var (file, data) in animations) File.WriteAllBytes(Path.Combine(folder, file), data);
        File.WriteAllBytes(Path.Combine(worldFolder, SetList), setList.ToBytes());
        File.WriteAllBytes(Path.Combine(worldFolder, FileList), fileList.ToBytes());
        var ownSets = new List<string>();
        if (hasDarius) ownSets.Add(SetFile);
        if (hasMason) ownSets.Add(MasonSetFile);
        File.WriteAllLines(Path.Combine(gearFolder, OwnEntries), ownSets.Select(s => $"set {s}").Concat(ownFiles.Select(f => $"file {f}")));
        File.WriteAllLines(Path.Combine(gearFolder, WeaponList), weaponsKept);
        if (!hasDarius && !hasMason) { log.Add("player animations: none made; the guns play Darius's"); return; }

        int total = MapUse(setList, overrides, gameTables);
        double Megabytes(string prefix) => animations.Where(a => a.Key.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)).Sum(a => (long)a.Value.Length) / 1024.0 / 1024;
        if (hasDarius)
            log.Add($"player animations: set {SetName} ({dariusPlaces} states and actions, {dariusFiles.Count} animations: {armageddonOwn.Count} Armageddon's own versions by name, " +
                    $"{dariusFiles.Count - armageddonOwn.Count} moved onto Darius, {Megabytes(Prefix):0.0} MB), taken up by {string.Join(", ", weaponsKept)}");
        if (hasMason)
            log.Add($"Mason's animations: set {MasonSetName} ({masonSkeletonSet.Element("Groups")!.Elements.Count()} groups, {masonPlaces} states and actions, " +
                    $"{masonFiles.Count} of Guerrilla's animations ({Megabytes(MasonPrefix):0.0} MB) and {armageddonFiles.Count} of Armageddon's ({Megabytes(ArmageddonPrefix):0.0} MB)), " +
                    $"for a character on Mason's skeleton to name in its Animation_Sets ({MasonSetFile})");
        log.Add($"player animations: in {folder}; the list of animations {fileEntries.Elements.Count()} of the {MaxAnimations} the game holds with guerrilla_vehicles; " +
                $"set files {otherSetFiles + adding}, sets {otherSets + adding + (hasBackpacks ? 1 : 0)} of {MaxAnimSets}");
        log.Add($"player animations: every set's states and actions: {total} (the game holds {GameMapPlaces}, {MapPlaces} with the guerrilla_gear mod)");
        if (fileEntries.Elements.Count() > MaxAnimations) log.Add($"WARNING: the list of animations has {fileEntries.Elements.Count()}, more than the {MaxAnimations} the game holds: the ones past it won't play");
        if (total > MapPlaces) log.Add($"WARNING: more states and actions than the {MapPlaces} places guerrilla_gear gives them: the game's memory would be spoilt");
    }

    /// <summary>
    /// One of Guerrilla's people's animations for Mason's own skeleton: Guerrilla's keys as they are, in
    /// Armageddon's packing. One made for Mason (53 bones) or Guerrilla's player (52, Mason without his
    /// camera bone) keeps its bones; one for another person's skeleton (the EDF's 54 and 55: the flinches)
    /// keeps the 52 every person's has, the rest of Mason's keeping his rest pose.
    /// </summary>
    static byte[] ForMason(byte[] animation)
    {
        var source = Animation.ReadGuerrilla(animation);
        int keep = source.SkeletonBones == MasonBones ? MasonBones : Math.Min(source.SkeletonBones, SharedBones);
        var tracks = source.Tracks.Where(t => t.Bone < keep).ToList();
        var root = new AnimationTrack(-1, source.Root.Rotations, source.Root.Positions, source.Root.Scales, source.Root.Weights);
        return AnimationWriter.Write(animation.AsSpan(0, 0x30), source.Frames, keep, tracks, root);
    }

    /// <summary>The names Armageddon's player asks for: its blend tree's (new_pc.blend_treex, the tree "player"), every one Darius's set has, states and actions, and its throw's (<see cref="ThrowActions"/>).</summary>
    static (List<string> States, List<string> Actions) Asked(Packfile gameTables, XtblFile dariusSet)
    {
        var states = new List<string>();
        var actions = new List<string>();
        void Add(List<string> list, string? id)
        {
            if (!string.IsNullOrWhiteSpace(id) && !list.Contains(id.Trim(), StringComparer.OrdinalIgnoreCase)) list.Add(id.Trim());
        }
        if (gameTables.Find("new_pc.blend_treex") is { } treeFile)
        {
            var document = XtblXml.Parse(Encoding.Latin1.GetString(gameTables.Read(treeFile)));
            var player = Descendants(document, "Blend_Tree").FirstOrDefault(t => string.Equals(t.ValueOf("Name")?.Trim(), "player", StringComparison.OrdinalIgnoreCase));
            foreach (var tag in player == null ? [] : Descendants(player, "Animation_State_Tag")) Add(states, tag.Value);
            foreach (var tag in player == null ? [] : Descendants(player, "Animation_Action_Tag")) Add(actions, tag.Value);
        }
        foreach (var skeletonSet in dariusSet.Entries)
        {
            foreach (var s in Descendants(skeletonSet, "State")) Add(states, s.ValueOf("ID"));
            foreach (var a in Descendants(skeletonSet, "Action")) Add(actions, a.ValueOf("ID"));
        }
        foreach (string thrown in ThrowActions) Add(actions, thrown);
        return (states, actions);
    }

    /// <summary>
    /// Which of a Guerrilla blend tree's animations plays for one of Armageddon's names: its walking one
    /// for a walk, its sprint for a sprint, else its running one. Guerrilla's player trees blend by
    /// speed, slowest first (standing, walking, jogging, running, sprinting: plyr_rfl_std, _wlk, _jog,
    /// _run, _spnt), Armageddon's player tree blends those bands itself.
    /// </summary>
    static string? BandClip(XtblElement tree, string asked)
    {
        var clips = new List<string>();
        foreach (var state in tree.Element("States")?.Elements ?? [])
            if ((state.Element("Animation") ?? state.Element("Animations"))?.ValueOf("Filename")?.Trim() is { Length: > 0 } f && !clips.Contains(f, StringComparer.OrdinalIgnoreCase)) clips.Add(f);
        var moving = clips.Where(c => !c.Contains("_std", StringComparison.OrdinalIgnoreCase)).ToList();
        if (moving.Count == 0) return clips.FirstOrDefault();
        string? Having(string part) => moving.FirstOrDefault(c => c.Contains(part, StringComparison.OrdinalIgnoreCase));
        string name = asked.ToLowerInvariant();
        if (name.Contains("sprint")) return Having("spnt") ?? Having("_run") ?? moving[^1];
        if (name.StartsWith("walk") || name.Contains(" walk") || name.Contains("wlk")) return Having("wlk") ?? moving[0];
        return Having("_run") ?? moving.LastOrDefault(c => !c.Contains("spnt", StringComparison.OrdinalIgnoreCase)) ?? moving[^1];
    }

    /// <summary>The names Armageddon's tables give a trigger, an IK's place, a flag (as written in an entry) and a prop's type.</summary>
    sealed record KnownParts(HashSet<string> Triggers, HashSet<string> IkLocations, HashSet<string> Flags, HashSet<string> PropTypes);

    /// <summary>Guerrilla's anim_files.xtbl entry for an animation (or a plain one), under our name, with only what Armageddon's tables have.</summary>
    static XtblElement FileEntry(XtblElement? rfg, string ours, IReadOnlyDictionary<string, string> ourFiles, KnownParts known, ISet<string> leftOut,
                                 Func<string, bool, string?>? soundOf)
    {
        var entry = rfg?.Copy() ?? new XtblElement("Anim_file");
        var animation = entry.Element("Animation") ?? entry.AddElement("Animation");
        if (animation.Element("Filename") is { } filename) filename.Value = ours;
        else animation.AddElement("Filename", ours);
        // Loaded and kept by the guerrilla_gear mod, not from Armageddon's animations archive.
        if (animation.Element("Preload") is { } preload) preload.Value = "False";
        else animation.AddElement("Preload", "False");
        void Keep(string list, string item, Func<XtblElement, bool> isKnown, Func<XtblElement, string> what)
        {
            foreach (var part in entry.Element(list)?.ElementsNamed(item).ToList() ?? [])
                if (!isKnown(part)) { leftOut.Add(what(part)); part.Remove(); }
        }
        Keep("Triggers", "Trigger", t => known.Triggers.Contains(t.ValueOf("Name")?.Trim() ?? ""), t => $"trigger \"{t.ValueOf("Name")?.Trim()}\"");
        Keep("IKs", "IK", ik => known.IkLocations.Contains(ik.ValueOf("Location")?.Trim() ?? ""), ik => $"IK at \"{ik.ValueOf("Location")?.Trim()}\"");
        Keep("Flags", "Flag", f => known.Flags.Contains(f.Value.Trim()), f => $"flag {f.Value.Trim()}");
        Keep("Props", "Prop", p => known.PropTypes.Contains(p.ValueOf("Type")?.Trim() ?? ""), p => $"prop type \"{p.ValueOf("Type")?.Trim()}\"");
        // A variant plays another animation in this one's place: ours if it was made, else it goes.
        foreach (var variant in entry.Element("Variants")?.ElementsNamed("Variant").ToList() ?? [])
            if (variant.Element("File") is { } file && ourFiles.TryGetValue(file.Value.Trim(), out var made)) file.Value = made;
            else { leftOut.Add("variants played by animations not ported"); variant.Remove(); }
        if (entry.Element("Variants") is { } variants && !variants.ElementsNamed("Variant").Any()) variants.Remove();
        foreach (var part in entry.Elements.ToList())
        {
            // Its sounds, given a sound maker: each Foley cue as our banks' event (frame and looping as Guerrilla's); one not had goes.
            if (soundOf != null && part.Name.Equals("Sounds", StringComparison.OrdinalIgnoreCase) && part.HasElements)
            {
                foreach (var sound in part.Elements.ToList())
                {
                    string? cue = sound.ValueOf("Foley")?.Trim();
                    bool looping = sound.ValueOf("Looping")?.Trim().Equals("True", StringComparison.OrdinalIgnoreCase) == true;
                    if (!string.IsNullOrEmpty(cue) && soundOf(cue, looping) is { } eventName) sound.Element("Foley")!.Value = eventName;
                    else { leftOut.Add("sounds not built yet (the sounds lines name them)"); sound.Remove(); }
                }
                if (!part.Elements.Any()) part.Remove();
                continue;
            }
            if (LeftOutOfFiles.Contains(part.Name, StringComparer.OrdinalIgnoreCase))
            {
                if (part.HasElements) leftOut.Add(part.Name.Replace('_', ' ').ToLowerInvariant());
                part.Remove();
            }
            else if (!part.HasElements && part.Value.Trim().Length == 0) part.Remove();
        }
        return entry;
    }

    /// <summary>How many states and actions every listed set has (each a place in the game's (set, animation) map).</summary>
    static int MapUse(XtblFile setList, string overrides, Packfile gameTables)
    {
        int total = 0;
        foreach (string file in setList.Entries.Select(e => e.ValueOf("Name")?.Trim()).OfType<string>())
        {
            XtblFile? table = OverrideLayout.Find(overrides, file) is { } ours ? XtblFile.FromBytes(File.ReadAllBytes(ours))
                            : gameTables.Find(file) is { } entry ? XtblFile.FromBytes(gameTables.Read(entry)) : null;
            foreach (var skeletonSet in table?.Entries.Where(e => e.Name.Equals("Skeleton_Set", StringComparison.OrdinalIgnoreCase)) ?? [])
                total += Descendants(skeletonSet, "State").Count() + Descendants(skeletonSet, "Action").Count();
        }
        return total;
    }

    /// <summary>
    /// Guerrilla's people's skeletons by bone count: they all start with its player's 52 bones
    /// (player.rig_pc), Mason's adds his camera (53), others their own after them (as GuerrillaAnimationSets').
    /// </summary>
    static Dictionary<int, Rig> PeopleRigs(Packfile guerrillaTables)
    {
        Rig Read(string name) => Rig.Read(guerrillaTables.Read(guerrillaTables.Find(name) ?? throw new InvalidDataException($"no {name} in Guerrilla's misc.vpp_pc")));
        var people = Read("player.rig_pc");
        var mason = Read("mason.rig_pc");
        var rigs = new Dictionary<int, Rig> { [people.Bones.Count] = people, [mason.Bones.Count] = mason };
        foreach (var entry in guerrillaTables.Entries.Where(e => e.Name.EndsWith(".rig_pc", StringComparison.OrdinalIgnoreCase)))
        {
            Rig rig;
            try { rig = Rig.Read(guerrillaTables.Read(entry)); }
            catch (Exception e) when (e is InvalidDataException or ArgumentException or IndexOutOfRangeException) { continue; }
            if (rig.Bones.Count >= people.Bones.Count && !rigs.ContainsKey(rig.Bones.Count) &&
                Enumerable.Range(0, people.Bones.Count).All(i => rig.Bones[i].Name.Equals(people.Bones[i].Name, StringComparison.OrdinalIgnoreCase) &&
                                                                 rig.Bones[i].Parent == people.Bones[i].Parent))
                rigs[rig.Bones.Count] = rig;
        }
        return rigs;
    }

    /// <summary>This tool's record of its own entries in the shared lists.</summary>
    static (HashSet<string> Sets, HashSet<string> Files) ReadOwn(string gearFolder)
    {
        var sets = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var files = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        string path = Path.Combine(gearFolder, OwnEntries);
        if (File.Exists(path))
            foreach (var parts in File.ReadAllLines(path).Select(l => l.Trim().Split(' ', 2)).Where(p => p.Length == 2))
                (parts[0] == "set" ? sets : parts[0] == "file" ? files : null)?.Add(parts[1].Trim());
        return (sets, files);
    }

    /// <summary>
    /// Takes away what <see cref="Build"/> wrote: the set's table, the animations, the gun list, this
    /// tool's entries in the shared lists (other tools' are kept) and its record.
    /// </summary>
    public static IEnumerable<string> Remove(string overrides, string worldFolder, string gearFolder)
    {
        var own = ReadOwn(gearFolder);
        foreach (string set in own.Sets.Concat([SetFile, MasonSetFile]).Distinct(StringComparer.OrdinalIgnoreCase))
            foreach (string setPath in OverrideLayout.Delete(overrides, set)) yield return $"Deleted {setPath}";
        string setListPath = Path.Combine(worldFolder, SetList);
        if (File.Exists(setListPath) && own.Sets.Count > 0)
        {
            var list = XtblFile.FromBytes(File.ReadAllBytes(setListPath));
            foreach (var e in list.Entries.Where(e => own.Sets.Contains(e.ValueOf("Name")?.Trim() ?? "")).ToList()) e.Remove();
            File.WriteAllBytes(setListPath, list.ToBytes());
            yield return $"{setListPath}: the player's sets taken out";
        }
        string fileListPath = Path.Combine(worldFolder, FileList);
        if (File.Exists(fileListPath) && own.Files.Count > 0)
        {
            var list = XtblFile.FromBytes(File.ReadAllBytes(fileListPath));
            var entries = list.Table?.Element("Files")?.Element("Anim_files");
            foreach (var e in entries?.Elements.Where(e => own.Files.Contains(AnimationName(e) ?? "")).ToList() ?? []) e.Remove();
            File.WriteAllBytes(fileListPath, list.ToBytes());
            yield return $"{fileListPath}: the player's animations taken out";
        }
        string folder = Path.Combine(gearFolder, AnimationFolder);
        if (Directory.Exists(folder))
        {
            int count = 0;
            foreach (string file in Directory.EnumerateFiles(folder, "*.anim_pc").ToList()) { File.Delete(file); count++; }
            yield return $"Deleted {count} animations from {folder}";
        }
        foreach (string name in new[] { WeaponList, OwnEntries })
        {
            string path = Path.Combine(gearFolder, name);
            if (File.Exists(path)) { File.Delete(path); yield return $"Deleted {path}"; }
        }
    }

    /// <summary>An anim_files.xtbl entry's animation (x.animx).</summary>
    static string? AnimationName(XtblElement entry) => entry.Element("Animation")?.ValueOf("Filename")?.Trim();

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
