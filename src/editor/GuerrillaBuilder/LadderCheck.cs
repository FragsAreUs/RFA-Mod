using System.IO;
using RfaFormats;
using RfaFormats.Guerrilla;
using RfaFormats.Zones;

namespace GuerrillaBuilder;

/// <summary>
/// Checks the built Guerrilla world's ladders as the game will take them (see <see cref="GuerrillaLadders"/>): each
/// one's parent is an rfg_mover of its own zone, written before it (the game ties an object to its parent only among
/// those made before it, and switches off a ladder whose parent isn't a mover), and the ladder is in that building's
/// list of children; and no 3 × 3 block of zones (what the game keeps loaded around the player) holds more ladders
/// than its ladder pool.
/// </summary>
static class LadderCheck
{
    /// <summary>Checks the world's ladders; logs what it finds. False if any ladder hangs on something else or a block holds too many.</summary>
    public static bool Run(Setup setup, BuildLog log)
    {
        var archives = new[] { "rfg_precache.vpp_pc", "rfg_l1.vpp_pc", "rfg_l0.vpp_pc" }.Select(a => setup.NewFile(RfaFormats.Guerrilla.OverrideLayout.World, a)).ToList();
        if (!archives.All(File.Exists))
        {
            log.Line("Ladder check: no Guerrilla world built here");
            return true;
        }
        uint ladderClass = NameHashes.ClassHash("ladder"), moverClass = NameHashes.ClassHash("rfg_mover");
        var perZone = new Dictionary<(int Col, int Row), int>();
        var problems = new List<string>();
        int ladders = 0;
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
                    int here = 0;
                    for (int i = 0; i < zone.Objects.Count; i++)
                    {
                        var ladder = zone.Objects[i];
                        if (ladder.ClassHash != ladderClass) continue;
                        here++;
                        string name = $"{file.Name} ladder {ladder.Handle:X8}";
                        if (!index.TryGetValue(ladder.Parent, out int at) || zone.Objects[at].ClassHash != moverClass)
                        {
                            problems.Add($"{name}: its parent {ladder.Parent:X8} isn't an rfg_mover of its zone (the game switches it off)");
                            continue;
                        }
                        if (at > i) problems.Add($"{name}: written before its building {ladder.Parent:X8} (the game wouldn't find its parent)");
                        bool listed = false;
                        for (uint c = zone.Objects[at].Child, steps = 0; c != ZoneObject.None && steps <= zone.Objects.Count && index.TryGetValue(c, out int k);
                             c = zone.Objects[k].Sibling, steps++)
                            if (c == ladder.Handle) { listed = true; break; }
                        if (!listed) problems.Add($"{name}: not in its building {ladder.Parent:X8}'s list of children");
                    }
                    ladders += here;
                    // rfg_CC_RR.rfgzone_pc
                    string bare = Path.GetFileNameWithoutExtension(file.Name);
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
        if (most > GuerrillaLadders.MostLadders)
            problems.Add($"a 3 × 3 block of zones holds {most} ladders, more than the game's {GuerrillaLadders.MostLadders}");
        foreach (string problem in problems) log.Line($"Ladder check: {problem}");
        log.Line(problems.Count == 0
            ? $"Ladder check: all {ladders} ladders hang on their building (an rfg_mover before them in their zone); the busiest 3 × 3 zones hold {most} of the game's {GuerrillaLadders.MostLadders}"
            : $"Ladder check: {problems.Count} problems in {ladders} ladders (above)");
        return problems.Count == 0;
    }
}
