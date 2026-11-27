using System.Globalization;
using System.Xml.Linq;
using RfaFormats.Tables;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Guerrilla's walkers' blend trees, as Armageddon's. Guerrilla's walker sets name a blend tree where an
/// animation would be (walker_civilian.xtbl's "walk" state: walker_civilian_move_forward, an
/// anim_blend_trees.xtbl entry): its states are clips weighted by the walker's input (control points, a
/// weight at each input 0..1), so a walker blends from standing to walking to running as the stick is
/// pushed. Armageddon has no such table: a vehicle names one tree of its own kind (vehicles.xtbl
/// Rig/Blend_Tree, in a .blend_treex file listed in blendTree_filenames.xtbl), whose branches follow the
/// walker's state (player controlled, movement type) and whose nodes sit on an axis the walker sets (Input
/// Z forward and back, Input X right and left, Turn Speed), each naming a state of the vehicle's set.
///
/// So for each walker set: its tree-named states get the tree's main clip (the one at the lowest input it
/// is full at: a set can't name a tree), and one tree of Armageddon's kind ("rfg " + the set) is made from
/// the set's trees: for each movement (walk → forward, walk back → back, walk left/right → left/right,
/// shuffle left/right state → turn left/right) a branch whose nodes are the Guerrilla tree's clips, each at
/// the input it is full at (back and left negative; turning on Turn Speed × 1.2, Armageddon's own walker's
/// full turn), named by the set's state that plays that clip (Guerrilla's sets have one for each). The
/// walker's Walker block gets Guerrilla's walker type by its name (walker_min: small, the flamer: flame, else
/// civilian) and its acceleration from the trees' ramp speeds. Not as Guerrilla: Armageddon blends two
/// neighbouring nodes in a straight line (Guerrilla's curves are straight lines between their points too,
/// but each clip fades in over its own blend time, and its walk keeps a tenth at a standstill).
/// </summary>
public sealed class GuerrillaWalkerTrees
{
    public const string FileList = "blendTree_filenames.xtbl", TreeFile = "rfg_walkers.blend_treex";

    /// <summary>Guerrilla's walker state → Armageddon's movement types, the axis and its sign.</summary>
    static readonly (string State, string[] Movements, string Axis, float Sign)[] Movements =
    [
        ("walk", ["forward", "forward turn right", "forward turn left"], "Input Z", 1),
        ("walk back", ["back", "back turn right", "back turn left"], "Input Z", -1),
        ("walk right", ["right", "right turn right", "right turn left"], "Input X", 1),
        ("walk left", ["left", "left turn left", "left turn right"], "Input X", -1),
        ("shuffle right state", ["turn right"], "Turn Speed", 1.2f),
        ("shuffle left state", ["turn left"], "Turn Speed", -1.2f),
    ];

    readonly Dictionary<string, XtblElement> trees = new(StringComparer.OrdinalIgnoreCase);
    readonly Dictionary<string, (XElement Tree, Dictionary<string, float> Ramps)> made = new(StringComparer.OrdinalIgnoreCase);  // by Guerrilla's set
    int uid = 30000;

    public GuerrillaWalkerTrees(Packfile guerrillaTables)
    {
        if (guerrillaTables.Find("anim_blend_trees.xtbl") is not { } file) return;
        foreach (var entry in XtblFile.FromBytes(guerrillaTables.Read(file)).Entries)
            trees.TryAdd(XtblFile.EntryName(entry), entry);
    }

    static float Number(string? text) => float.TryParse(text?.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out float f) ? f : 0;

    /// <summary>A tree's states: each clip, and the input it is full at (its control point of the most weight, the lowest first).</summary>
    static List<(string Clip, float Peak)> States(XtblElement tree)
    {
        var states = new List<(string, float)>();
        foreach (var state in tree.Element("States")?.Elements ?? [])
        {
            string? clip = state.Element("Animation")?.ValueOf("Filename")?.Trim();
            if (string.IsNullOrEmpty(clip)) continue;
            var points = (state.Element("Control_points")?.Elements ?? []).Select(p => (Range: Number(p.ValueOf("Range")), Value: Number(p.ValueOf("Value")))).ToList();
            if (points.Count == 0) continue;
            float most = points.Max(p => p.Value);
            states.Add((clip, points.Where(p => p.Value >= most).Min(p => p.Range)));
        }
        return states;
    }

    /// <summary>
    /// A walker set's tree-named states pointed at their trees' main clips, and its tree made (when the set
    /// names any). Called on Guerrilla's set before its animations are gathered.
    /// </summary>
    public void Rewrite(string set, XtblElement skeletonSet)
    {
        var stateAnimations = Descendants(skeletonSet, "State")
            .Select(s => (Id: s.ValueOf("ID")?.Trim() ?? "", Filename: s.Element("Animation")?.Element("Filename")))
            .Where(s => s.Filename != null).ToList();
        var named = new Dictionary<string, XtblElement>(StringComparer.OrdinalIgnoreCase);  // state → its tree
        foreach (var (id, filename) in stateAnimations)
        {
            string value = filename!.Value.Trim();
            if (Path.HasExtension(value) || !trees.TryGetValue(value, out var tree)) continue;
            var states = States(tree);
            var main = states.Where(s => s.Peak > 0).OrderBy(s => s.Peak).FirstOrDefault();
            if (main.Clip == null) main = states.FirstOrDefault();
            if (main.Clip == null) continue;
            filename.Value = main.Clip;
            named[id] = tree;
        }
        if (named.Count == 0) return;
        // The clips' states (the set's own state that plays each clip).
        var tagOf = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var (id, filename) in stateAnimations)
            if (filename!.Value.Trim() is var clip && Path.HasExtension(clip) && !named.ContainsKey(id)) tagOf.TryAdd(clip, id);
        var ramps = new Dictionary<string, float>(StringComparer.OrdinalIgnoreCase);
        var branches = new List<XElement>();
        foreach (var (state, movements, axis, sign) in Movements)
        {
            if (!named.TryGetValue(state, out var tree)) continue;
            ramps[state] = Number(tree.ValueOf("Ramp_speed"));
            var nodes = new List<XElement>();
            foreach (var (clip, peak) in States(tree))
            {
                if (!tagOf.TryGetValue(clip, out var tag)) continue;  // a clip the set plays under no state of its own
                nodes.Add(new XElement("Node", new XElement("Name", tag), new XElement("Axis", axis), new XElement("uid", uid++),
                                       new XElement("Control", peak * sign == 0 ? "0" : (peak * sign).ToString("0.###", CultureInfo.InvariantCulture)),
                                       new XElement("Animation_State_Tag", tag)));
            }
            if (nodes.Count == 0) continue;
            branches.Add(new XElement("Node", new XElement("Name", state), new XElement("uid", uid++),
                                      new XElement("State_Dependent", new XElement("State_Name", "movement type"),
                                                   new XElement("State_Values", movements.Select(m => new XElement("State_Value", m)))),
                                      new XElement("Nodes", nodes)));
        }
        if (branches.Count == 0) return;  // trees of no walker's movement (a tank turret's): their states keep their clips, no tree
        string standTag = tagOf.FirstOrDefault(t => t.Value.Equals("stand", StringComparison.OrdinalIgnoreCase)).Value ?? "stand";
        var root = new XElement("Blend_Tree", new XElement("Name", TreeName(set)), new XElement("Crossfade_Speed", "5.0"),
            new XElement("States", new XElement("State", new XElement("Name", "movement type"), new XElement("Crossfade_Speed", "0.0")),
                                   new XElement("State", new XElement("Name", "crouch")), new XElement("State", new XElement("Name", "player controlled"))),
            new XElement("Nodes",
                new XElement("Node", new XElement("Name", "Unpiloted"), new XElement("uid", uid++),
                    new XElement("State_Dependent", new XElement("State_Name", "player controlled"), new XElement("State_Values", new XElement("State_Value", "uncontrolled"))),
                    new XElement("Animation_State_Tag", standTag)),
                new XElement("Node", new XElement("Name", "Piloted"), new XElement("uid", uid++),
                    new XElement("State_Dependent", new XElement("State_Name", "player controlled"),
                                 new XElement("State_Values", new XElement("State_Value", "yes"), new XElement("State_Value", "no"))),
                    new XElement("Nodes", branches))));
        made[set] = (root, ramps);
    }

    static IEnumerable<XtblElement> Descendants(XtblElement e, string name)
    {
        foreach (var c in e.Elements)
        {
            if (c.Name.Equals(name, StringComparison.OrdinalIgnoreCase)) yield return c;
            foreach (var d in Descendants(c, name)) yield return d;
        }
    }

    public static string TreeName(string set) => "rfg " + set;

    /// <summary>The tree made for a Guerrilla set, or null.</summary>
    public string? TreeFor(string? set) => set != null && made.ContainsKey(set) ? TreeName(set) : null;

    /// <summary>A walker's vehicles.xtbl entry given its tree and Guerrilla's walker settings (type by its name, acceleration from the trees' ramps).</summary>
    public void Apply(XtblElement vehicle, string vehicleName, string set)
    {
        if (!made.TryGetValue(set, out var tree)) return;
        var rig = vehicle.Element("Rig") ?? vehicle.AddElement("Rig");
        (rig.Element("Blend_Tree") ?? rig.AddElement("Blend_Tree")).Value = TreeName(set);
        var walker = vehicle.Element("Walker") ?? vehicle.AddElement("Walker");
        string type = vehicleName.StartsWith("Walker_Min", StringComparison.OrdinalIgnoreCase) ? "small"
                    : vehicleName.StartsWith("Walker_Flamer", StringComparison.OrdinalIgnoreCase) ? "flame" : "civilian";
        void Put(string field, string value) => (walker.Element(field) ?? walker.AddElement(field)).Value = value;
        Put("Walker_Type", type);
        // The offsets Armageddon's reader requires and has no default for (Guerrilla's walkers have none: its
        // code has no barrier detector or capsule of this kind); as Armageddon's own civilian walker's, none.
        foreach (string offset in new[] { "Barrier_Detector_Offset", "Collision_Capsule_Offset" })
            if (walker.Element(offset) == null)
            {
                var vector = walker.AddElement(offset);
                foreach (string axis in new[] { "X", "Y", "Z" }) vector.AddElement(axis, "0.0");
            }
        string F(float f) => f.ToString("0.###", CultureInfo.InvariantCulture);
        if (tree.Ramps.TryGetValue("walk", out float forward) && forward > 0) { Put("Movement_Acceleration_Forward", F(forward)); Put("Movement_Deceleration_Forward", F(forward)); }
        if (tree.Ramps.TryGetValue("walk back", out float back) && back > 0) Put("Movement_Acceleration_Back", F(back));
        float lateral = Math.Max(tree.Ramps.GetValueOrDefault("walk left"), tree.Ramps.GetValueOrDefault("walk right"));
        if (lateral > 0) { Put("Movement_Acceleration_Lateral", F(lateral)); Put("Movement_Deceleration_Lateral", F(lateral)); }
    }

    /// <summary>Where the loader serves the list and the tree file each time the game reads them after start-up (relative to
    /// the overrides folder): the list in place of table.vpp_pc's copy (shared with misc.vpp_pc's, the same file), the tree
    /// file as a new file found by its name (in the vehicles' folder: see OverrideLayout).</summary>
    public static readonly string ListOverride = Path.Combine("build", "pc", "cache", "table.vpp_pc", FileList);

    /// <summary>Writes the tree file and Armageddon's file list with it; a line for the log. Into the mod's folder, which
    /// guerrilla_world hands the game while it starts, and into the overrides, which the loader serves later: every
    /// level load the game empties its blend trees and reads the list again by name (0x961730: the count 0x25CD820 set
    /// to 0), and with only Armageddon's list then, Guerrilla's walkers kept trees that were gone (they didn't show, and
    /// their legs threw whatever touched them).</summary>
    public string Write(Packfile gameTables, string modFolder, string overrides)
    {
        string listPath = Path.Combine(modFolder, FileList), treePath = Path.Combine(modFolder, TreeFile);
        string listOverride = Path.Combine(overrides, ListOverride), treeOverride = OverrideLayout.NewFile(overrides, OverrideLayout.Vehicles, TreeFile);
        if (made.Count == 0)
        {
            foreach (string path in new[] { listPath, treePath, listOverride, treeOverride }) if (File.Exists(path)) File.Delete(path);
            return "Walkers: no blend trees made";
        }
        var file = new XDocument(new XElement("root", new XElement("Version", "1"), new XElement("Blend_Trees", made.Values.Select(m => m.Tree))));
        var list = XDocument.Parse(System.Text.Encoding.UTF8.GetString(gameTables.Read(gameTables.Find(FileList) ?? throw new InvalidDataException($"no {FileList} in Armageddon's misc.vpp_pc"))).TrimStart('﻿'));
        var files = list.Descendants().FirstOrDefault(e => e.Name.LocalName.Equals("files", StringComparison.OrdinalIgnoreCase)) ?? throw new InvalidDataException($"{FileList} has no <files>");
        if (!files.Elements().Any(f => f.Value.Trim().Equals(TreeFile, StringComparison.OrdinalIgnoreCase))) files.Add(new XElement("file", TreeFile));
        foreach (var (tree, fileList) in new[] { (treePath, listPath), (treeOverride, listOverride) })
        {
            Directory.CreateDirectory(Path.GetDirectoryName(tree)!);
            Directory.CreateDirectory(Path.GetDirectoryName(fileList)!);
            File.WriteAllText(tree, file.ToString());
            File.WriteAllText(fileList, list.ToString());
        }
        return $"Walkers: {made.Count} blend trees ({string.Join(", ", made.Keys.Select(TreeName))}) in mods\\guerrilla_world\\{TreeFile}, listed in {FileList}; "
               + $"the same in {treeOverride} and overrides\\{ListOverride}, for the game's reading of them at each level load";
    }
}
