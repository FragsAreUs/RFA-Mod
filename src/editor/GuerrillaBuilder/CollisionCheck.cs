using System.IO;
using System.Numerics;
using RfaFormats;
using RfaFormats.Guerrilla;
using RfaFormats.Meshes;
using RfaFormats.Zones;

namespace GuerrillaBuilder;

/// <summary>
/// Checks the built Guerrilla world's rocks as the game's physics will take them, and lists every
/// case it finds: each rock piece (.cstch_pc) has a collision mesh, and its collision tree reaches every
/// triangle; each zone's stitch list's tree reaches every rock it places, wherever it is touched. The
/// trees are walked as Havok's AABB query walks them (see <see cref="StitchList.TreeMisses"/>): a part
/// of a rock its tree hides is a part a player, a vehicle or a thrown charge goes into without touching
/// it, and a player who sinks into a rock's top that way is held inside. Also listed, to look into but
/// not failing the check: a rock whose drawn mesh reaches well past its collision.
/// </summary>
static class CollisionCheck
{
    /// <summary>How far a rock's drawn mesh may reach past its collision box before it's listed.</summary>
    const float DrawnPastCollision = 0.25f;

    /// <summary>Checks the world's rocks; logs what it finds. False if any tree hides collision or a rock has none.</summary>
    public static bool Run(Setup setup, BuildLog log)
    {
        var archives = new[] { "rfg_precache.vpp_pc", "rfg_l1.vpp_pc", "rfg_l0.vpp_pc" }.Select(a => setup.NewFile(RfaFormats.Guerrilla.OverrideLayout.World, a)).ToList();
        if (!archives.All(File.Exists))
        {
            log.Line("Collision check: no Guerrilla world built here");
            return true;
        }

        // The rock pieces' files and the zones' stitch lists, wherever the world keeps them; and the zones and the
        // destructible objects' chunk files' checksums, for the anchor data.
        var headers = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
        var datas = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
        var lists = new List<(string Container, byte[] Data)>();
        var zoneFiles = new List<(string Name, byte[] Data)>();
        var chunkSums = new Dictionary<string, uint>(StringComparer.OrdinalIgnoreCase);
        void Take(IReadOnlyList<PackfileEntry> entries, byte[][] files, string container)
        {
            for (int i = 0; i < entries.Count; i++)
            {
                string name = entries[i].Name, bare = Path.GetFileNameWithoutExtension(name);
                switch (Path.GetExtension(name).ToLowerInvariant())
                {
                    case ".cstch_pc": headers[bare] = files[i]; break;
                    case ".gstch_pc": datas[bare] = files[i]; break;
                    case ".stchlst_pc": lists.Add((container, files[i])); break;
                    case ".rfgzone_pc" when container.StartsWith("1h_", StringComparison.OrdinalIgnoreCase): zoneFiles.Add((name, files[i])); break;
                    case ".cchk_pc" when files[i].Length >= 0x28: chunkSums[name] = BitConverter.ToUInt32(files[i], 0x24); break;
                }
            }
        }
        static bool Wanted(PackfileEntry e) => Path.GetExtension(e.Name).ToLowerInvariant() is ".cstch_pc" or ".gstch_pc" or ".stchlst_pc" or ".rfgzone_pc" or ".cchk_pc";
        foreach (string path in archives)
        {
            var pack = Packfile.Open(path);
            var loose = pack.Entries.Where(Wanted).ToList();
            Take(loose, pack.ReadAll(loose), "");
            foreach (var entry in pack.Entries.Where(e => e.Name.EndsWith(".str2_pc", StringComparison.OrdinalIgnoreCase)))
            {
                var container = pack.OpenContainer(entry);
                var inside = container.Entries.Where(Wanted).ToList();
                if (inside.Count > 0) Take(inside, container.ReadAll(inside), Path.GetFileNameWithoutExtension(entry.Name));
            }
        }

        int bad = 0, doubtful = 0, triangles = 0, placed = 0;
        // Each rock's own collision and tree.
        var collision = new Dictionary<string, Vector3[]>(StringComparer.OrdinalIgnoreCase);
        foreach (var (name, header) in headers.OrderBy(h => h.Key, StringComparer.OrdinalIgnoreCase))
        {
            try
            {
                if (!PieceCollision.IsMesh(header))
                {
                    log.Line($"Collision check: rock {name} has no collision mesh: everything goes through it");
                    bad++;
                    continue;
                }
                var (vertices, tris) = PieceCollision.Read(header);
                collision[name] = vertices;
                triangles += tris.Length / 3;
                var (from, scale, code) = PieceCollision.Tree(header);
                var misses = StitchList.TreeMisses(code, from, 16777216f / scale, PieceCollision.TriangleBoxes(vertices, tris));
                if (misses.Count > 0)
                {
                    bad++;
                    log.Line($"Collision check: rock {name}: its tree hides part of {misses.Count} of its {tris.Length / 3} triangles from what touches them there, " +
                             $"up to {Depth(misses[0])} (triangle {misses[0].Leaf}, its {misses[0].Side} side)");
                }
                if (datas.TryGetValue(name, out byte[]? data))
                {
                    var drawn = ModelFile.Read(name + ".cstch_pc", header, data).Positions;
                    var (drawnMin, drawnMax) = Box(drawn);
                    var (solidMin, solidMax) = Box(vertices);
                    float past = MathF.Max(Largest(solidMin - drawnMin), Largest(drawnMax - solidMax));
                    if (past > DrawnPastCollision)
                    {
                        doubtful++;
                        log.Line($"Collision check: rock {name}: its drawn mesh reaches {past:0.00} m past its collision box (to look into: Guerrilla's own collision may be smaller by design)");
                    }
                }
            }
            catch (Exception e) when (e is InvalidDataException or NotSupportedException or ArgumentException)
            {
                log.Line($"Collision check: rock {name} can't be read: {e.Message}");
                bad++;
            }
        }

        // Each zone's tree over the rocks it places.
        int zones = 0;
        foreach (var (container, data) in lists.OrderBy(l => l.Container, StringComparer.OrdinalIgnoreCase))
        {
            string zone = container;
            try
            {
                var list = StitchList.Read(data);
                var trees = StitchList.Trees(data);
                if (list.Pieces.Count == 0) continue;
                zones++;
                placed += list.Pieces.Count;
                // 1h_cXXYY: XX the zone's column, YY 15 − its row; the tree's frame x and z from its middle.
                if (container.Length != 8 || !container.StartsWith("1h_c", StringComparison.OrdinalIgnoreCase) ||
                    !int.TryParse(container[4..6], out int col) || !int.TryParse(container[6..8], out int yy))
                    throw new InvalidDataException("a stitch list outside a zone's container");
                zone = $"rfg_{col:00}_{15 - yy:00}";
                if (trees.Count != 1) throw new InvalidDataException($"{trees.Count} collision trees over its {list.Pieces.Count} rocks, not one");
                var corner = GuerrillaWorld.Corner(col, 15 - yy);
                var (stored, scale, code) = trees[0];
                var from = stored + new Vector3(corner.X + GuerrillaWorld.ZoneWidth / 2, 0, corner.Y + GuerrillaWorld.ZoneWidth / 2);
                var unknown = list.Pieces.Where(p => !collision.ContainsKey(p.Name)).Select(p => p.Name).Distinct().ToList();
                if (unknown.Count > 0) throw new InvalidDataException($"it places rocks the world hasn't with collision: {string.Join(", ", unknown)}");
                var boxes = list.Pieces.Select(p => Box(collision[p.Name].Select(v => Vector3.Transform(v, p.Rotation) + p.Position))).ToList();
                var misses = StitchList.TreeMisses(code, from, 16777216f / scale, boxes);
                if (misses.Count > 0)
                {
                    bad++;
                    var worst = list.Pieces[misses[0].Leaf];
                    log.Line($"Collision check: zone {zone} ({container}): its tree hides part of {misses.Count} of its {list.Pieces.Count} rocks from what touches them there, " +
                             $"up to {Depth(misses[0])}: {worst.Name} at {worst.Position.X:0.0} {worst.Position.Y:0.0} {worst.Position.Z:0.0}, its {misses[0].Side} side");
                }
            }
            catch (Exception e) when (e is InvalidDataException or NotSupportedException or ArgumentException)
            {
                log.Line($"Collision check: zone {zone}'s stitch list can't be checked: {e.Message}");
                bad++;
            }
        }

        // Destructible objects whose zone checksum isn't their chunk file's: the game throws their saved anchor
        // data away, finds their anchors by contact, and shows "Zone 0x... has out of date anchor data" (the
        // builder writes such data only where Guerrilla's is stale and it has no correction: GuerrillaAnchorFixes).
        // Listed, to look into, without failing.
        uint dest = RfaFormats.Zones.NameHashes.PropertyHash("dest_checksum"), chunkName = RfaFormats.Zones.NameHashes.PropertyHash("chunk_name");
        int movers = 0;
        var stale = new List<string>();
        foreach (var (name, data) in zoneFiles)
            try
            {
                foreach (var o in RfaFormats.Zones.ZoneFile.Read(data).Objects)
                {
                    if (o.Property(dest) is not { Size: 4 } sum || o.Property(chunkName) is not { } chunk) continue;
                    movers++;
                    if (chunkSums.TryGetValue(chunk.Text, out uint own) && own != BitConverter.ToUInt32(sum.Value))
                        stale.Add($"{Path.GetFileNameWithoutExtension(name)} {o.Handle:X8} {Path.GetFileNameWithoutExtension(chunk.Text)}");
                }
            }
            catch (Exception e) when (e is InvalidDataException or ArgumentException)
            {
                log.Line($"Anchor check: {name} can't be read: {e.Message}");
            }
        log.Line(stale.Count == 0
            ? $"Anchor check: all {movers:N0} destructible objects' saved anchor data is up to date with their files"
            : $"Anchor check: {stale.Count} of {movers:N0} destructible objects' saved anchor data is out of date (the game shows a warning and anchors them by contact; to look into): {string.Join(", ", stale)}");

        log.Line(bad == 0
            ? $"Collision check: {headers.Count} rocks' own trees ({triangles:N0} triangles) and {zones} zones' trees ({placed:N0} placed rocks) reach all of them, as Havok's query walks them" +
              (doubtful > 0 ? $"; {doubtful} rocks to look into (above)" : "")
            : $"Collision check: {bad} rocks' or zones' collision trees hide collision, or can't be checked (above): build the world again" +
              (doubtful > 0 ? $"; {doubtful} rocks to look into" : ""));
        return bad == 0;
    }

    static string Depth(StitchList.TreeMiss miss) => float.IsInfinity(miss.Metres) ? "all of it (no path reaches it)" : $"{miss.Metres:0.00} m deep";

    static float Largest(Vector3 v) => MathF.Max(v.X, MathF.Max(v.Y, v.Z));

    static (Vector3 Min, Vector3 Max) Box(IEnumerable<Vector3> points)
    {
        Vector3 min = new(float.MaxValue), max = new(float.MinValue);
        foreach (var p in points) { min = Vector3.Min(min, p); max = Vector3.Max(max, p); }
        return (min, max);
    }
}
