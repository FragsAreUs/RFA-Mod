using System.IO;
using RfaFormats;
using RfaFormats.Guerrilla;
using RfaFormats.Zones;

namespace GuerrillaBuilder;

/// <summary>
/// Checks the built Guerrilla world's constraints as the game will take them (see <see cref="GuerrillaConstraints"/>):
/// each a hinge with Armageddon's template (164 bytes; a motor's speed and gain both above 0, or both -1), its host
/// and child movers of its own zone written before it, it a child of its child and in that one's list of children;
/// and no 3 × 3 block of zones (what the game keeps loaded around the player) holding more than its constraint pool.
/// </summary>
static class ConstraintCheck
{
    /// <summary>Checks the world's constraints; logs what it finds. False if any is wrong or a block holds too many.</summary>
    public static bool Run(Setup setup, BuildLog log)
    {
        var archives = new[] { "rfg_precache.vpp_pc", "rfg_l1.vpp_pc", "rfg_l0.vpp_pc" }.Select(a => setup.NewFile(RfaFormats.Guerrilla.OverrideLayout.World, a)).ToList();
        if (!archives.All(File.Exists)) return true;
        uint constraintClass = NameHashes.ClassHash("constraint");
        uint rfgMover = NameHashes.ClassHash("rfg_mover"), generalMover = NameHashes.ClassHash("general_mover");
        uint templateHash = NameHashes.PropertyHash("template"), hostHash = NameHashes.PropertyHash("host_handle"), childHash = NameHashes.PropertyHash("child_handle");
        var perZone = new Dictionary<(int Col, int Row), int>();
        var problems = new List<string>();
        int constraints = 0, motors = 0;
        foreach (string path in archives)
        {
            var pack = Packfile.Open(path);
            foreach (var entry in pack.Entries.Where(e => e.Name.StartsWith("1h_", StringComparison.OrdinalIgnoreCase) &&
                                                          e.Name.EndsWith(".str2_pc", StringComparison.OrdinalIgnoreCase)))
            {
                var container = pack.OpenContainer(entry);
                foreach (var file in container.Entries.Where(e => e.Name.EndsWith(".rfgzone_pc", StringComparison.OrdinalIgnoreCase)))
                {
                    var zone = ZoneFile.Read(container.Read(file));
                    var index = new Dictionary<uint, int>();
                    for (int i = 0; i < zone.Objects.Count; i++) index.TryAdd(zone.Objects[i].Handle, i);
                    bool MoverBefore(uint handle, int i) =>
                        index.TryGetValue(handle, out int at) && at < i && (zone.Objects[at].ClassHash == rfgMover || zone.Objects[at].ClassHash == generalMover);
                    int here = 0;
                    for (int i = 0; i < zone.Objects.Count; i++)
                    {
                        var c = zone.Objects[i];
                        if (c.ClassHash != constraintClass) continue;
                        here++;
                        string name = $"{file.Name} constraint {c.Handle:X8}";
                        byte[]? template = c.Property(templateHash)?.Value;
                        byte[]? host = c.Property(hostHash)?.Value, child = c.Property(childHash)?.Value;
                        if (template is not { Length: GuerrillaConstraints.TemplateSize } || GuerrillaConstraints.TypeOf(template) != GuerrillaConstraints.Hinge)
                        {
                            problems.Add($"{name}: not a hinge with Armageddon's {GuerrillaConstraints.TemplateSize}-byte template");
                            continue;
                        }
                        float speed = BitConverter.ToSingle(template, GuerrillaConstraints.MotorAt), gain = BitConverter.ToSingle(template, GuerrillaConstraints.MotorAt + 4);
                        if (speed > 0 && gain > 0) motors++;
                        else if (speed != -1 || gain != -1) problems.Add($"{name}: its motor's speed {speed} and gain {gain} are neither both above 0 nor both -1");
                        if (host is not { Length: 4 } || child is not { Length: 4 })
                        {
                            problems.Add($"{name}: no host or child");
                            continue;
                        }
                        uint hostHandle = BitConverter.ToUInt32(host, 0), childHandle = BitConverter.ToUInt32(child, 0);
                        if (!MoverBefore(hostHandle, i)) problems.Add($"{name}: its host {hostHandle:X8} isn't a mover written before it in its zone");
                        if (!MoverBefore(childHandle, i)) problems.Add($"{name}: its child {childHandle:X8} isn't a mover written before it in its zone");
                        if (c.Parent != childHandle) problems.Add($"{name}: its parent {c.Parent:X8} isn't its child {childHandle:X8}");
                        else if (index.TryGetValue(childHandle, out int at))
                        {
                            bool listed = false;
                            for (uint k = zone.Objects[at].Child, steps = 0; k != ZoneObject.None && steps <= zone.Objects.Count && index.TryGetValue(k, out int n);
                                 k = zone.Objects[n].Sibling, steps++)
                                if (k == c.Handle) { listed = true; break; }
                            if (!listed) problems.Add($"{name}: not in its child {childHandle:X8}'s list of children");
                        }
                    }
                    constraints += here;
                    string bare = Path.GetFileNameWithoutExtension(file.Name);  // rfg_CC_RR
                    if (here > 0 && bare.Length >= 6 && int.TryParse(bare[^5..^3], out int col) && int.TryParse(bare[^2..], out int row))
                        perZone[(col, row)] = perZone.GetValueOrDefault((col, row)) + here;
                }
            }
        }
        int most = 0;
        foreach (var (col, row) in perZone.Keys)
        {
            int block = 0;
            for (int dc = -1; dc <= 1; dc++)
                for (int dr = -1; dr <= 1; dr++) block += perZone.GetValueOrDefault((col + dc, row + dr));
            most = Math.Max(most, block);
        }
        if (most > GuerrillaConstraints.MostConstraints)
            problems.Add($"a 3 × 3 block of zones holds {most} constraints, more than the game's {GuerrillaConstraints.MostConstraints}");
        foreach (string problem in problems) log.Line($"Constraint check: {problem}");
        log.Line(problems.Count == 0
            ? $"Constraint check: all {constraints} hinges join two movers before them in their zone ({motors} with a motor); the busiest 3 × 3 zones hold {most} of the game's {GuerrillaConstraints.MostConstraints}"
            : $"Constraint check: {problems.Count} problems in {constraints} constraints (above)");
        return problems.Count == 0;
    }
}
