using System.Numerics;
using System.Text;
using System.Xml.Linq;
using RfaFormats.Meshes;
using RfaFormats.Tables;
using RfaFormats.Textures;
using RfaFormats.Zones;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Builds the world "rfg" from your own copy of Red Faction: Guerrilla: its open world's zones, whole,
/// each the zone of the same name and place (terr01_12_11 becomes rfg_12_11), in Guerrilla's own
/// coordinates and heights. The game gives the world Guerrilla's 511 m zones (the guerrilla_world mod
/// says so, ZoneSize=511; the game itself does for worlds named mpl_*), so its 16 × 16 grid is
/// Guerrilla's 8 km map.
///
/// For each zone: its ground sampled every 1 m from Guerrilla's sub-zones (GroundLayout.Whole), its
/// roads and paving with it, holes (under its rocks) filled smoothly, deep shafts capped and sheer drops limited over
/// all zones together (so shared edges stay equal); its nine sub-zones and the height field written
/// from that; its far-away meshes; its ground material from Guerrilla's (its four layers' textures at
/// Guerrilla's own scale and tints, its blend and colour maps, and its cliff materials); its invisible
/// barriers (see <see cref="TerrainBarriers"/>: Guerrilla's own); Guerrilla's rocks where its zone
/// places them (see <see cref="GuerrillaRocks"/>); the
/// destructible objects asked for (its rfg_movers: buildings, walls, pipes; and general_movers: crates,
/// boxes) where its zone places them, attached and linked as there (see <see cref="GuerrillaMovers"/>),
/// each chunk file converted (see <see cref="GuerrillaChunks"/>), each naming Guerrilla's level object
/// with the effects it shows (see <see cref="GuerrillaEffectTables"/>; the effects converted, see
/// <see cref="GuerrillaEffects"/>, in the containers of the zones placing them); a stitch list of the
/// rocks with a collision tree over them (empty in zones without rocks); an environment box over the
/// whole zone; Guerrilla's navigation meshes, for people and
/// vehicles (see <see cref="GuerrillaNavigation"/>; none in the scenery zones, as in Guerrilla). And
/// the time of day (<see cref="GuerrillaTimeOfDay"/>) and the clouds and backdrop for the mod (<see
/// cref="GuerrillaSky"/>).
///
/// Nothing is taken from Armageddon's levels and written over. Terrain files (see ZoneTerrain), zone
/// files (their objects, the destructible ones too, made from the game's schema), navigation files
/// (Guerrilla's own meshes: see GuerrillaNavigation), sub-zones, textures, containers, archives,
/// stitch lists, asset lists, the time of day's sets (Guerrilla's own curves) and every material (the
/// ground's, the cliffs', the roads', the rocks' and the destructible objects') are written from
/// nothing.
///
/// Out: rfg_l0.vpp_pc (the sub-zones' containers and the asset list), rfg_l1.vpp_pc (the zones'),
/// rfg_precache.vpp_pc, zonescript_rfg.vpp_pc, rfg.gtodx and a time of day for each district
/// (rfg_default.dtodx, rfg_eos.dtodx, ...), all new files for the loader's overrides
/// (build\pc\cache\); Armageddon's territories.xtbl with Guerrilla's districts added (an override in
/// build\pc\cache\table.vpp_pc\; see <see cref="GuerrillaTerritory"/>); Armageddon's texture name table
/// with room for the world's textures (an override in build\pc\cache\misc.vpp_pc\; see
/// <see cref="GuerrillaBitmaps"/>); the mod's cloud files and tables (gameplay properties, draw
/// distances, visual effects, effects and level objects, which the guerrilla_world mod hands the game);
/// and last, world.txt, the world's format (<see cref="Format"/>), which the launcher checks.
/// Guerrilla's files are only read, and what is made from them stays on your machine.
/// </summary>
public static class GuerrillaWorld
{
    public const string World = "rfg";
    public const float ZoneWidth = 511f;

    /// <summary>
    /// The world's format, written into <see cref="StampFile"/> next to the mod's tables: raise it
    /// whenever a change here means a world built before it won't work with the mods any more (the
    /// launcher, RFAModLauncher.exe, expects the same number, kGuerrillaWorldFormat, and asks before
    /// starting the game with a world of another, or with one built before worlds were stamped).
    /// 2 (v0.5.7-beta): the zones name the world's own spawn resource, rfg_Default; a world without
    /// it gives the game Armageddon's Default people, whose effects run its pools out at start.
    /// 3 (v0.5.8-beta): Guerrilla's people, guard boxes, turret parts and looping sounds; the mods
    /// size the game's tables for what the world's zones and tables now name.
    /// 4 (v0.5.11-beta).
    /// 5 (v0.6.0-beta): Guerrilla's hinges (turbines, cranes), the turbine list, transition cells,
    /// ladders, Mason's coat cloth, the kill regions and the campaign's markers, which the mods read.
    /// 6 (v0.6.2-beta): the kiosks (their list, screen effects and broadcast banks) and the building
    /// list for the minimap, which the mods read; the HUD's documents change with guerrilla_hud.
    /// 7 (v0.6.3-beta): the safehouses' respawn spots, the story people's nodes, the Marauder ambush
    /// regions, the deep pits left open, and Guerrilla's 9/3/2 district people lists.
    /// 8 (v0.6.4-beta): the action nodes and their types, the civilians' own AI properties, the walkers'
    /// blend trees served on every load, the demolitions masters' markers and kits, the vehicle cameras'
    /// mouse turn speed.
    /// 9 (v0.6.5-beta): the shape cutters, guard posts, props and NPC voices, the EDF's reinforcement
    /// places, the squads' progression weapons, and the demolitions masters' managed layers.
    /// 10 (v0.6.6-beta): sound fades in Wwise's own curve units, compressed (ADPCM) sound banks, Guerrilla's
    /// ground heights for the cars, the placed effects, wheel dust and gusts, the 475 m view distance,
    /// the carried propane tank's explosion, and the map icons.
    /// </summary>
    public const int Format = 10;
    // The effects' emitters the game holds (guerrilla_world makes its pool 6,600) and Armageddon's own files' (639 files).
    const int EmitterRoom = 6600, ArmageddonEmitters = 3457;

    /// <summary>The stamp's file name, in the mod's folder (mods\guerrilla_world\).</summary>
    public const string StampFile = "world.txt";

    /// <summary>The effects' sounds' banks: the part's name (its list file in the mod's folder) and what its banks' names start with.</summary>
    public const string EffectSoundPart = "effects", EffectSoundBankPrefix = "rfge_";

    /// <summary>The start zone: in a part of the grid no precache group names (so its area is the world's).</summary>
    public static readonly (int Col, int Row) DefaultStart = (12, 11);

    const float MaxPit = 60;    // no ground more than this below where its hollow spills
    const float MaxStep = 25f;  // nor more than this below a neighbouring sample, 1 m away: only the deep shafts' walls, not Guerrilla's own cliffs and retaining walls
    const float BoxBottom = -500, BoxTop = 1500;   // the environment boxes' heights
    const float BoxFogNear = 100000;               // where the boxes' own fog would start (past any far clip)
    // The most chunk files (c- and g-files) one container is given. The game reads a container's
    // CPU-side files through a 20 MB pool ("Pregame_split_pool"), asking a little more than their
    // sizes; its own containers hold at most 19 MB of them. Guerrilla's busiest zone, all in one
    // container, asked for 26 MB and stopped the game.
    const long ChunkBudget = 14L << 20;
    // Guerrilla's containers kept open while the world is built: more than the 20 a zone has in its
    // two layers (its own and its nine sub-zones') and the few of chunks.vpp_pc.
    const int ContainersKept = 32;
    static readonly GroundLayout Layout = GroundLayout.Whole;

    /// <summary>A zone's lowest x and z in the world (its container 1h_cXXYY: XX its column, YY 15 − its row).</summary>
    public static Vector2 Corner(int col, int row) => new((col - 8) * ZoneWidth, (7 - row) * ZoneWidth);

    /// <summary>
    /// A zone's look from its Guerrilla terrain file: its ground layers' textures (colour and normal, four
    /// layers, without ".tga") and constants, blend and colour maps, cliff materials and layer map.
    /// </summary>
    /// <summary>The most cliff materials a zone's terrain file is given: Guerrilla's zones have up to six.</summary>
    const int MaxCliffMaterials = 6;

    /// <summary>
    /// Whether a zone's terrain file gets the zone's own cliff materials (it has some, every one with its
    /// textures and side projections), else three made from its ground's layers.
    /// </summary>
    static bool OwnCliffs(Look look) =>
        look.Cliffs.Count is > 0 and <= MaxCliffMaterials &&
        Enumerable.Range(0, look.Cliffs.Count).All(k => look.CliffPairs(k).Count > 0 && look.Cliffs[k].Constants.ContainsKey(NameHashes.PropertyHash("Terrain_layer0_xform")));

    sealed record Look(string[] Textures, Vector4[] Constants, RgbaImage Blend, RgbaImage Colour,
                       List<GuerrillaTerrain.CliffMaterial> Cliffs, (byte[] Cells, List<string> Names)? LayerMap)
    {
        /// <summary>
        /// A cliff material's colour maps (…_d) with their normal maps (…_n), in its textures' order, without
        /// ".tga" (the zone's own blend and colour maps, and empty slots, left out).
        /// </summary>
        public List<(string Colour, string Normal)> CliffPairs(int material)
        {
            var names = Cliffs[material].Textures
                .Where(t => t.EndsWith(".tga", StringComparison.OrdinalIgnoreCase) && !t.StartsWith("terr01_", StringComparison.OrdinalIgnoreCase))
                .Select(t => t[..^4]).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
            return names.Where(n => n.EndsWith("_d", StringComparison.OrdinalIgnoreCase))
                .Select(d => (d, names.FirstOrDefault(n => n.Equals(d[..^2] + "_n", StringComparison.OrdinalIgnoreCase)) ?? d))
                .ToList();
        }

        /// <summary>Every ground and cliff texture the zone's terrain file names (without ".tga"), and the cliffs' dither map.</summary>
        public IEnumerable<string> TerrainTextures =>
            Textures.Take(8).Concat(Enumerable.Range(0, Cliffs.Count).SelectMany(CliffPairs).SelectMany(p => new[] { p.Colour, p.Normal }))
                .Append(GuerrillaBuiltIns.CliffDither)
                .Distinct(StringComparer.OrdinalIgnoreCase);
    }

    /// <summary>Guerrilla's zones with ground (terr01_CC_RR.cterrain_pc in its terr01_l0 and _l1 archives): column and row.</summary>
    public static List<(int Col, int Row)> ZonesIn(string guerrillaCache)
    {
        var zones = new SortedSet<(int, int)>();
        foreach (string archive in new[] { "terr01_l0.vpp_pc", "terr01_l1.vpp_pc" })
        {
            var pack = Packfile.Open(Path.Combine(guerrillaCache, archive));
            foreach (var entry in pack.Entries.Where(e => e.Name.StartsWith("1h_c") && e.Name.EndsWith(".str2_pc")))
                foreach (var file in pack.OpenContainer(entry).Entries)
                {
                    var name = Path.GetFileNameWithoutExtension(file.Name).Split('_');
                    if (file.Name.EndsWith(".cterrain_pc") && name.Length == 3 && name[0] == "terr01" && int.TryParse(name[1], out int col) && int.TryParse(name[2], out int row))
                        zones.Add((col, row));
                }
        }
        return [.. zones];
    }

    /// <summary>
    /// Builds the world from Guerrilla's <paramref name="zones"/> into <paramref name="overrides"/>
    /// (the loader's overrides folder: the world's files go in build\pc\cache\) and the clouds into
    /// <paramref name="modFolder"/> (mods\guerrilla_world\). <paramref name="props"/> names the chunk
    /// files (such as con_cardboard_a) whose destructible objects to place, or "all"; none if null;
    /// <paramref name="propZones"/>, if given, the zones to place them in.
    /// </summary>
    public static void Build(string guerrilla, string game, string overrides, string modFolder, IReadOnlyList<(int Col, int Row)> zones,
                             (int Col, int Row) start, Action<string> log, IReadOnlyCollection<string>? props = null,
                             IReadOnlyCollection<(int Col, int Row)>? propZones = null)
    {
        if (zones.Count == 0) throw new ArgumentException("no zones to build");
        if (!zones.Contains(start)) start = zones[0];
        string cache = Path.Combine(game, "build", "pc", "cache"), rfgCache = Path.Combine(guerrilla, "build", "pc", "cache");
        var precache = Packfile.Open(Path.Combine(cache, "dlc01_precache.vpp_pc"));
        byte[] Pre(string name) => precache.Read(precache.Find(name) ?? throw new InvalidDataException($"no {name} in dlc01_precache.vpp_pc"));

        // ---- Guerrilla's files: in a zone's own containers (both layers), else its precache. The
        // containers are opened where they lie, and only the last ContainersKept are kept open (with
        // their files unpacked): a zone's files are asked for together, zone after zone, and all of
        // Guerrilla's world's containers would take GBs.
        var rfgL0 = Packfile.Open(Path.Combine(rfgCache, "terr01_l0.vpp_pc"));
        var rfgL1 = Packfile.Open(Path.Combine(rfgCache, "terr01_l1.vpp_pc"));
        var rfgPrecache = Packfile.Open(Path.Combine(rfgCache, "terr01_precache.vpp_pc"));
        var containers = new Dictionary<string, Packfile?>();
        var opened = new Queue<string>();
        Packfile? Container(Packfile archive, string name)
        {
            string key = archive.Name + "/" + name;
            if (!containers.TryGetValue(key, out var c))
            {
                containers[key] = c = archive.Find(name + ".str2_pc") is { } e ? archive.OpenContainer(e, name) : null;
                opened.Enqueue(key);
                if (opened.Count > ContainersKept) containers.Remove(opened.Dequeue());
            }
            return c;
        }
        byte[]? RfgFile(int col, int row, string file, int minSize = 2)
        {
            var names = new List<string> { $"1h_c{col:00}{15 - row:00}" };
            for (int sz = 0; sz < 3; sz++)
                for (int sx = 0; sx < 3; sx++) names.Add($"0h_c{col * 3 + sx:00}{(15 - row) * 3 + sz:00}");
            foreach (var archive in new[] { rfgL0, rfgL1 })
                foreach (string n in names)
                    if (Container(archive, n) is { } c && c.Find(file) is { } e && e.Size >= minSize) return c.Read(e);
            return rfgPrecache.Find(file) is { } p && p.Size >= minSize ? rfgPrecache.Read(p) : null;
        }
        // A destructible object's files: beside the zone, else in Guerrilla's chunks.vpp_pc, which keeps
        // some (EDFbarricade_vehicle_a, that zones place too) in a container of their own with their textures.
        var rfgChunks = Packfile.Open(Path.Combine(rfgCache, "chunks.vpp_pc"));
        byte[]? ChunkFile(int col, int row, string chunk, string file, int minSize = 2) =>
            RfgFile(col, row, file, minSize)
            ?? (Container(rfgChunks, chunk + ".rfgchunkx") is { } c && c.Find(file) is { } e && e.Size >= minSize ? c.Read(e) : null);

        // ---- Guerrilla's rocks where its zones place them, in the list of the sub-zone each stands in.
        var rockPieces = new Dictionary<string, GuerrillaRocks.Piece>(StringComparer.OrdinalIgnoreCase);
        var rockTextures = new Dictionary<string, (string Name, byte[] Data)[]>(StringComparer.OrdinalIgnoreCase);
        var rocksIn = zones.ToDictionary(z => z, _ => new List<StitchPieceToWrite>());
        var rockPlacements = zones.ToDictionary(z => z, _ => new List<GuerrillaRocks.Placement>());
        float third = ZoneWidth / 3;
        foreach (var (col, row) in zones)
        {
            byte[] terrain = RfgFile(col, row, $"terr01_{col:00}_{row:00}.cterrain_pc")!;
            var corner = Corner(col, row);
            foreach (var p in GuerrillaRocks.Placements(terrain))
            {
                if (!rockPieces.ContainsKey(p.Name))
                {
                    byte[] h = RfgFile(col, row, p.Name + ".cstch_pc") ?? throw new InvalidDataException($"no {p.Name}.cstch_pc");
                    byte[] g = RfgFile(col, row, p.Name + ".gstch_pc", 16) ?? throw new InvalidDataException($"no {p.Name}.gstch_pc");
                    var piece = GuerrillaRocks.Convert(h, g, Alias);
                    rockPieces[p.Name] = piece;
                    foreach (string texture in piece.Textures.Where(t => !rockTextures.ContainsKey(t)))
                    {
                        if (GuerrillaBuiltIns.Files(texture, Alias(texture)) is { } builtIn)
                        {
                            rockTextures[texture] = builtIn;
                            continue;
                        }
                        byte[] th = RfgFile(col, row, texture + ".cvbm_pc") ?? throw new InvalidDataException($"no {texture}.cvbm_pc");
                        byte[] tg = RfgFile(col, row, texture + ".gvbm_pc", 1000) ?? throw new InvalidDataException($"no {texture}.gvbm_pc");
                        rockTextures[texture] = [(Alias(texture) + ".cvbm_pc", Repacked(th, tg, Alias(texture))), (Alias(texture) + ".gvbm_pc", tg)];
                    }
                }
                float x = p.Position.X - corner.X, z = p.Position.Z - corner.Y;
                int list = Math.Clamp((int)(x / third), 0, 2) + 3 * Math.Clamp((int)(z / third), 0, 2);
                rocksIn[(col, row)].Add(new StitchPieceToWrite(p.Name, list, p.Position, p.Rotation, 0, p.AreaMin, p.AreaMax));
                rockPlacements[(col, row)].Add(p);
            }
        }
        log($"Rocks: {rocksIn.Values.Sum(r => r.Count)} placed, {rockPieces.Count} pieces, {rockTextures.Count} textures");

        // ---- The ground, sampled whole from Guerrilla's sub-zones of each zone and those around it. A
        // zone's sub-zones are read for the first zone sampled around it and let go after the last (all
        // of them would take GBs); their road decals are kept for the roads below.
        int S = Layout.Samples, last = S - 1;
        var heights = new Dictionary<(int, int), float[]>();
        var holes = new Dictionary<(int, int), SquareCut[]>();
        var roadSurfaces = new Dictionary<(int, int), float[]?>();
        var cliffCovers = new Dictionary<(int, int), GuerrillaGround.CliffCover>();
        var subzoneCache = new Dictionary<(int, int, int), TerrainSubzone?>();
        var roadDecals = new Dictionary<(int, int, int), IReadOnlyList<GuerrillaRoadDecal>>();
        TerrainSubzone? Subzone(int col, int row, int n)
        {
            if (subzoneCache.TryGetValue((col, row, n), out var cached)) return cached;
            string stem = $"terr01_{col:00}_{row:00}_{n}";
            var read = RfgFile(col, row, stem + ".ctmesh_pc") is { } h && RfgFile(col, row, stem + ".gtmesh_pc", 1) is { } g ? TerrainSubzone.Read(h, g) : null;
            roadDecals[(col, row, n)] = read?.RoadDecals ?? [];
            return subzoneCache[(col, row, n)] = read;
        }
        var lastSampledBy = new Dictionary<(int, int), int>();  // a zone whose sub-zones are read: the last zone (its number) sampled around it
        for (int i = 0; i < zones.Count; i++)
            for (int dc = -1; dc <= 1; dc++)
                for (int dr = -1; dr <= 1; dr++) lastSampledBy[(zones[i].Col + dc, zones[i].Row + dr)] = i;
        for (int i = 0; i < zones.Count; i++)
        {
            var (col, row) = zones[i];
            var subzones = new List<TerrainSubzone>();
            for (int dc = -1; dc <= 1; dc++)
                for (int dr = -1; dr <= 1; dr++)
                    for (int n = 0; n < 9; n++)
                        if (Subzone(col + dc, row + dr, n) is { } s) subzones.Add(s);
            var corner = Corner(col, row);
            var over = subzones.Where(s => s.Positions.Length > 0 && s.Positions.Max(p => p.X) >= corner.X - 1 && s.Positions.Min(p => p.X) <= corner.X + ZoneWidth + 1 &&
                                           s.Positions.Max(p => p.Z) >= corner.Y - 1 && s.Positions.Min(p => p.Z) <= corner.Y + ZoneWidth + 1);
            heights[(col, row)] = GuerrillaGround.Sample(over, corner, Layout, RockCorners(col, row)) ?? throw new InvalidDataException($"no Guerrilla ground for terr01_{col:00}_{row:00}");
            (holes[(col, row)], roadSurfaces[(col, row)]) = GuerrillaGround.UnderRoads(over, corner, Layout, heights[(col, row)]);
            cliffCovers[(col, row)] = GuerrillaGround.Cliffs(over, corner, Layout);
            foreach (var ((doneCol, doneRow), lastBy) in lastSampledBy)
                if (lastBy == i)
                    for (int n = 0; n < 9; n++) subzoneCache.Remove((doneCol, doneRow, n));
        }
        log($"Ground: {zones.Count} zones sampled, {S} × {S} samples each; under the road decals {GuerrillaGround.RoadTriangles[0]:N0} triangles " +
            $"kept under them, {GuerrillaGround.RoadTriangles[1]:N0} left out (all covered, but showing through), {GuerrillaGround.RoadTriangles[2]:N0} showing through by their edges");
        // The triangles (in the world) of the rocks placed in a zone and those around it: where the
        // ground has a hole under one, the rock closes it (see GuerrillaGround.Sample).
        IEnumerable<Vector3> RockCorners(int col, int row)
        {
            for (int dc = -1; dc <= 1; dc++)
                for (int dr = -1; dr <= 1; dr++)
                    if (rockPlacements.TryGetValue((col + dc, row + dr), out var placed))
                        foreach (var p in placed)
                        {
                            var r = p.Rotation;
                            Vector3 right = new(r[0], r[1], r[2]), up = new(r[3], r[4], r[5]), forward = new(r[6], r[7], r[8]);
                            foreach (var v in rockPieces[p.Name].Corners) yield return p.Position + v.X * right + v.Y * up + v.Z * forward;
                        }
        }
        // Neighbours sample their shared edge at the same points, but where Guerrilla's ground has holes
        // each fills them from its own side: a zone's first column (lowest x) is made its west
        // neighbour's last, its first row (lowest z) its south neighbour's (row + 1) last.
        foreach (var (col, row) in zones)
            for (int k = 0; k < S; k++)
            {
                if (heights.TryGetValue((col - 1, row), out var west)) heights[(col, row)][k * S] = west[k * S + last];
                if (heights.TryGetValue((col, row + 1), out var south)) heights[(col, row)][k] = south[last * S + k];
            }
        // Where Guerrilla placed things (every object of its zones): a hollow with one of them inside it is Guerrilla's
        // own pit, left open (see LimitGround).
        var placedAt = new List<Vector3>();
        var zoneScripts = Packfile.Open(Path.Combine(rfgCache, "zonescript_terr01.vpp_pc"));
        foreach (var (col, row) in zones)
            if (zoneScripts.Find($"terr01_{col:00}_{row:00}.rfgzone_pc") is { } zoneEntry)
                foreach (var o in GuerrillaZones.Objects(zoneScripts.Read(zoneEntry)))
                    placedAt.Add(o.Value("op") is { Length: >= 12 } op
                        ? new Vector3(BitConverter.ToSingle(op, 0), BitConverter.ToSingle(op, 4), BitConverter.ToSingle(op, 8))
                        : (o.BoundsMin + o.BoundsMax) / 2);
        LimitGround(zones, heights, placedAt, log);

        // ---- Each zone's look: its Guerrilla zone's material (four layers), blend and colour maps; and
        // its invisible barriers, which take the place of the terrain template's (ruin01's arena walls).
        var looks = new Dictionary<(int, int), Look>();
        var barriers = new Dictionary<(int, int), List<ChunkShape>>();
        var navigation = new Dictionary<(int, int), NavigationFile>();
        foreach (var (col, row) in zones)
        {
            string zoneName = $"terr01_{col:00}_{row:00}";
            byte[] terrain = RfgFile(col, row, zoneName + ".cterrain_pc") ?? throw new InvalidDataException($"no {zoneName}.cterrain_pc");
            var (textures, constants) = MaterialBlocks.GuerrillaGround(terrain);
            RgbaImage Map(string stem, int minSize)
            {
                var peg = PegFile.Read(RfgFile(col, row, stem + ".cvbm_pc") ?? throw new InvalidDataException($"no {stem}.cvbm_pc"),
                                       RfgFile(col, row, stem + ".gvbm_pc", minSize) ?? throw new InvalidDataException($"no {stem}.gvbm_pc"));
                return peg.Decode(peg.Textures[0]);
            }
            looks[(col, row)] = new Look(textures, constants, Map(zoneName + "_alpha00", 1_000_000), Map(zoneName + "comb", 1000),
                                         GuerrillaTerrain.CliffMaterials(terrain), GuerrillaTerrain.LayerMap(terrain));
            barriers[(col, row)] = TerrainBarriers.Read(terrain);
            navigation[(col, row)] = GuerrillaNavigation.Convert(terrain, col, row);
        }
        log($"Invisible barriers: Guerrilla's own, {barriers.Values.Sum(b => b.Count):N0} in {barriers.Values.Count(b => b.Count > 0)} of {zones.Count} zones");
        var navigable = navigation.Values.Where(n => n.People != null || n.Vehicles != null).ToList();
        log($"Navigation: Guerrilla's own in {navigable.Count} zones ({navigable.Sum(n => n.People?.Cells.Length ?? 0):N0} cells for people, " +
            $"{navigable.Sum(n => n.Vehicles?.Cells.Length ?? 0):N0} for vehicles); none in the other {zones.Count - navigable.Count}, as in Guerrilla");
        // An undergrowth mesh made Armageddon's (see GuerrillaFoliage) under its own name, and its texture
        // under its new one; each made once.
        var undergrowthFiles = new Dictionary<string, (string Name, byte[] Data)[]>(StringComparer.OrdinalIgnoreCase);
        (string Name, byte[] Data)[] UndergrowthFiles(int col, int row, string mesh)
        {
            if (undergrowthFiles.TryGetValue(mesh, out var made)) return made;
            byte[] guerrillaMesh = RfgFile(col, row, mesh + ".cfmesh_pc") ?? throw new InvalidDataException($"no {mesh}.cfmesh_pc for {Alias(mesh)}'s undergrowth");
            var (converted, texture) = GuerrillaFoliage.Convert(guerrillaMesh, Alias);
            byte[] header = RfgFile(col, row, texture + ".cvbm_pc") ?? throw new InvalidDataException($"no {texture}.cvbm_pc for {mesh}");
            byte[] data = RfgFile(col, row, texture + ".gvbm_pc", 1000) ?? RfgFile(col, row, texture + ".gvbm_pc", 1) ?? throw new InvalidDataException($"no {texture}.gvbm_pc for {mesh}");
            return undergrowthFiles[mesh] = [(mesh + ".cfmesh_pc", converted), (Alias(texture) + ".cvbm_pc", Repacked(header, data, Alias(texture))), (Alias(texture) + ".gvbm_pc", data)];
        }

        // Guerrilla's reflection cube as a texture of its own under its new name: six faces one under
        // another, its cube flag (0x8) and every level as Guerrilla's (Armageddon keeps its cubes the same way).
        (string Name, byte[] Data)[]? guerrillaCube = null;
        (string Name, byte[] Data)[] GuerrillaCubeFiles()
        {
            if (guerrillaCube != null) return guerrillaCube;
            var rfgMiscPack = Packfile.Open(Path.Combine(rfgCache, "misc.vpp_pc"));
            byte[] Entry(string file) => rfgMiscPack.Read(rfgMiscPack.Find(file) ?? throw new InvalidDataException($"no {file} in Guerrilla's misc.vpp_pc"));
            var peg = PegFile.Read(Entry("always_loaded.cpeg_pc"), Entry("always_loaded.gpeg_pc"));
            var cube = peg.Textures.FirstOrDefault(t => Path.GetFileNameWithoutExtension(t.Name).Equals(GuerrillaMaterials.GuerrillaCube, StringComparison.OrdinalIgnoreCase))
                       ?? throw new InvalidDataException($"no {GuerrillaMaterials.GuerrillaCube} in Guerrilla's always_loaded textures");
            string alias = Alias(GuerrillaMaterials.GuerrillaCube);
            var (header, data) = peg.Single(cube, alias + ".tga", cube.MipLevels);
            return guerrillaCube = [(alias + ".cvbm_pc", header), (alias + ".gvbm_pc", data)];
        }

        // A Guerrilla texture's header made again under its new name: its size, format and flags as
        // Guerrilla's, and its mip levels, but a compressed one's only down to 4 x 4. Guerrilla sizes
        // the levels smaller than a 4-pixel block by their pixels (a 2 x 2 DXT5 level 4 bytes, not a
        // block's 16), so Armageddon would look for them in the wrong place, and past the end of a
        // file that Guerrilla's sizes fill (a 256 x 256 DXT5's 87,392 bytes, against 87,408).
        static byte[] Repacked(byte[] header, byte[] data, string name)
        {
            var t = PegFile.Read(header, data).Textures[0];
            int levels = t.MipLevels;
            if (t.Format.IsBlockCompressed())
            {
                levels = 1;
                while (levels < t.MipLevels && Math.Min(t.Width >> levels, t.Height >> levels) >= 4) levels++;
            }
            return PegFile.SingleHeader(name + ".tga", t.Width, t.Height, t.Format, levels, t.Flags);
        }
        var layerFiles = new Dictionary<string, (string Name, byte[] Data)[]>(StringComparer.OrdinalIgnoreCase);
        foreach (var ((col, row), look) in looks)
            foreach (string texture in look.TerrainTextures.Where(t => !layerFiles.ContainsKey(t)))
            {
                if (GuerrillaBuiltIns.Files(texture, Alias(texture)) is { } builtIn)
                {
                    layerFiles[texture] = builtIn;
                    continue;
                }
                byte[] header = RfgFile(col, row, texture + ".cvbm_pc") ?? throw new InvalidDataException($"no {texture}.cvbm_pc");
                byte[] data = RfgFile(col, row, texture + ".gvbm_pc", 1000) ?? throw new InvalidDataException($"no {texture}.gvbm_pc");
                layerFiles[texture] = [(Alias(texture) + ".cvbm_pc", Repacked(header, data, Alias(texture))), (Alias(texture) + ".gvbm_pc", data)];
            }

        // ---- Guerrilla's road decals, each in the sub-zone of ours it lies in (the ground left out
        // under it), and their textures under new names, in the zone's container with its ground's.
        var roadsIn = new Dictionary<(int, int), IReadOnlyList<RoadDecalToWrite>[]>();
        var roadTextures = new Dictionary<string, (string Name, byte[] Data)[]>(StringComparer.OrdinalIgnoreCase);
        var roadTexturesIn = zones.ToDictionary(z => z, _ => new HashSet<string>(StringComparer.OrdinalIgnoreCase));
        foreach (var (col, row) in zones)
        {
            var corner = Corner(col, row);
            var lists = Enumerable.Range(0, 9).Select(_ => new List<RoadDecalToWrite>()).ToArray();
            for (int n = 0; n < 9; n++)
                foreach (GuerrillaRoadDecal decal in roadDecals[(col, row, n)])
                {
                    Vector3 middle = decal.Corners.Length > 0 ? decal.Corners.Aggregate(Vector3.Add) / decal.Corners.Length : decal.Position;
                    int part = Math.Clamp((int)((middle.X - corner.X) / third), 0, 2) + 3 * Math.Clamp((int)((middle.Z - corner.Y) / third), 0, 2);
                    lists[part].Add(GuerrillaRoads.Convert(decal, RoadTexture));
                    foreach (string texture in decal.Names.Select(Path.GetFileNameWithoutExtension).OfType<string>().Where(t => !IsBlendMap(t)))
                    {
                        roadTexturesIn[(col, row)].Add(texture);
                        if (roadTextures.ContainsKey(texture)) continue;
                        if ((layerFiles.GetValueOrDefault(texture) ?? GuerrillaBuiltIns.Files(texture, Alias(texture))) is { } known)
                        {
                            roadTextures[texture] = known;
                            continue;
                        }
                        byte[] header = RfgFile(col, row, texture + ".cvbm_pc") ?? throw new InvalidDataException($"no {texture}.cvbm_pc");
                        byte[] data = RfgFile(col, row, texture + ".gvbm_pc", 1000) ?? throw new InvalidDataException($"no {texture}.gvbm_pc");
                        roadTextures[texture] = [(Alias(texture) + ".cvbm_pc", Repacked(header, data, Alias(texture))), (Alias(texture) + ".gvbm_pc", data)];
                    }
                }
            roadsIn[(col, row)] = lists;
        }
        log($"Roads: {roadsIn.Values.Sum(l => l.Sum(d => d.Count))} road decals ({string.Join(", ", GuerrillaRoads.Variants.OrderBy(v => v.Key).Select(v => $"{v.Value} {v.Key}"))}), " +
            $"{roadTextures.Count} textures; of their {GuerrillaRoads.VerticesSeen:N0} vertices {GuerrillaRoads.VerticesRaised:N0} at the road's edge made none of it as Armageddon reads it");
        // A road decal's texture's new name: the zone's blend map ours (terr01_05_05_alpha00 is
        // rfg0505_a00, as the ground's), the rest as the ground's textures.
        static bool IsBlendMap(string texture) => texture.StartsWith("terr01_", StringComparison.OrdinalIgnoreCase) && texture.EndsWith("_alpha00", StringComparison.OrdinalIgnoreCase);
        static string RoadTexture(string texture) => IsBlendMap(texture) ? $"rfg{texture[7..9]}{texture[10..12]}_a00" : Alias(texture);

        // Guerrilla's activities' managed layers (see GuerrillaActivityLayers, written at the end): their destructible
        // objects' chunk files are converted with their zone's (below), but the objects are placed by their layer.
        var activityLayers = GuerrillaActivityLayers.Sets(rfgCache);
        var layerMovers = GuerrillaActivityLayers.Movers(activityLayers);
        var layerMoversPlaced = zones.ToDictionary(z => z, _ => new List<GuerrillaZoneObject>());

        // ---- Guerrilla's destructible objects: the rfg_movers and general_movers its zones place whose
        // chunk files are asked for, each file converted, its textures under new names (as the
        // ground's and rocks').
        var propNames = new HashSet<string>(props ?? [], StringComparer.OrdinalIgnoreCase);
        bool allProps = propNames.Contains("all");
        var chunkFiles = new Dictionary<string, (byte[] Header, byte[] Data, IReadOnlyList<string> Textures)>(StringComparer.OrdinalIgnoreCase);
        var chunkTextures = new Dictionary<string, (string Name, byte[] Data)[]>(StringComparer.OrdinalIgnoreCase);
        var propsIn = zones.ToDictionary(z => z, _ => new List<GuerrillaZoneObject>());
        var guerrillaObjects = new Dictionary<(int, int), Dictionary<uint, GuerrillaZoneObject>>();
        uint rfgMover = NameHashes.ClassHash("rfg_mover"), generalMover = NameHashes.ClassHash("general_mover");
        if (propNames.Count > 0)
        {
            var rfgZones = Packfile.Open(Path.Combine(rfgCache, "zonescript_terr01.vpp_pc"));
            var missing = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
            var missingTextures = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var (col, row) in zones)
            {
                if (propZones != null && !propZones.Contains((col, row))) continue;
                if (rfgZones.Find($"terr01_{col:00}_{row:00}.rfgzone_pc") is not { } entry) continue;
                var objects = GuerrillaZones.Objects(rfgZones.Read(entry));
                guerrillaObjects[(col, row)] = objects.GroupBy(o => o.Handle).ToDictionary(g => g.Key, g => g.First());
                var layerHere = layerMovers.GetValueOrDefault((col, row)) ?? [];
                foreach (var o in objects.Concat(layerHere))
                {
                    if (o.ClassHash != generalMover && o.ClassHash != rfgMover || o.Text("chunk_name") is not { } file) continue;
                    string name = Path.GetFileNameWithoutExtension(file);
                    if (!allProps && !propNames.Contains(name)) continue;
                    if (!chunkFiles.ContainsKey(name))
                    {
                        if (ChunkFile(col, row, name, name + ".cchk_pc") is not { } c || ChunkFile(col, row, name, name + ".gchk_pc", 1) is not { } g)
                        {
                            missing.Add(name);
                            continue;
                        }
                        var (header, textures) = GuerrillaChunks.Convert(c, t => t == GuerrillaMaterials.GlassReflection ? t : Alias(t));
                        chunkFiles[name] = (header, g, textures);
                        foreach (string texture in textures.Where(t => !chunkTextures.ContainsKey(t)))
                        {
                            if (GuerrillaBuiltIns.Files(texture, Alias(texture)) is { } builtIn)
                            {
                                chunkTextures[texture] = builtIn;
                                continue;
                            }
                            // Glass's reflection cube is Armageddon's own, as it is.
                            if (texture == GuerrillaMaterials.GlassReflection)
                            {
                                chunkTextures[texture] = [(texture + ".cvbm_pc", Pre(texture + ".cvbm_pc")), (texture + ".gvbm_pc", Pre(texture + ".gvbm_pc"))];
                                continue;
                            }
                            // Guerrilla's own reflection cube (its mirror glass's), from its always-loaded textures.
                            if (texture.Equals(GuerrillaMaterials.GuerrillaCube, StringComparison.OrdinalIgnoreCase))
                            {
                                chunkTextures[texture] = GuerrillaCubeFiles();
                                continue;
                            }
                            // A texture that isn't there is drawn white.
                            if (ChunkFile(col, row, name, texture + ".cvbm_pc") is not { } th ||
                                (ChunkFile(col, row, name, texture + ".gvbm_pc", 1000) ?? ChunkFile(col, row, name, texture + ".gvbm_pc", 1)) is not { } tg)
                            {
                                missingTextures.Add(texture);
                                chunkTextures[texture] = GuerrillaBuiltIns.Files(GuerrillaMaterials.White, Alias(texture))!;
                                continue;
                            }
                            chunkTextures[texture] = [(Alias(texture) + ".cvbm_pc", Repacked(th, tg, Alias(texture))), (Alias(texture) + ".gvbm_pc", tg)];
                        }
                    }
                    if (layerHere.Contains(o)) layerMoversPlaced[(col, row)].Add(o);
                    else propsIn[(col, row)].Add(o);
                }
            }
            missing.ExceptWith(chunkFiles.Keys);
            log($"Destructible objects: {propsIn.Values.Sum(p => p.Count(o => o.ClassHash == rfgMover))} rfg_movers and {propsIn.Values.Sum(p => p.Count(o => o.ClassHash == generalMover))} general_movers placed, {chunkFiles.Count} chunk files, {chunkTextures.Count} textures" +
                (missing.Count > 0 ? $"; not found next to the zones that place them: {string.Join(", ", missing)}" : "") +
                (missingTextures.Count > 0 ? $"; textures not found, drawn white: {string.Join(", ", missingTextures)}" : ""));
        }
        // The Props and gameplay_props names Armageddon's tables have.
        var misc = Packfile.Open(Path.Combine(cache, "misc.vpp_pc"));
        HashSet<string> TableNames(string table) => new(XtblFile.FromBytes(misc.Read(misc.Find(table) ?? throw new InvalidDataException($"no {table} in misc.vpp_pc"))).Entries.Select(XtblFile.EntryName), StringComparer.OrdinalIgnoreCase);
        var levelObjects = TableNames("level_objects.xtbl");
        // Guerrilla's gameplay properties (see GuerrillaGameplay): Armageddon's table with Guerrilla's
        // settings added, for the guerrilla_world mod to hand the game; each object names its entry there.
        var rfgTables = Packfile.Open(Path.Combine(rfgCache, "misc.vpp_pc"));
        var gameplay = GuerrillaGameplay.Build(misc, rfgTables, propsIn.Values.Concat(layerMoversPlaced.Values).SelectMany(p => p).Select(o => o.Text("gameplay_props")).OfType<string>().Where(n => n.Length > 0));
        Directory.CreateDirectory(modFolder);
        File.WriteAllBytes(Path.Combine(modFolder, "gameplay_properties.xtbl"), gameplay.Text);
        log($"Gameplay properties: {gameplay.EntryOf.Count} of Guerrilla's entries named by the objects, {gameplay.Added} added to Armageddon's table with Guerrilla's settings " +
            $"(the rest are Armageddon entries with the same settings); names {gameplay.PoolBytes:N0} bytes of the mod's {GuerrillaGameplay.NamePool:N0} (mods\\guerrilla_world\\gameplay_properties.xtbl)");
        string? GameplayProps(string name) => gameplay.EntryOf.GetValueOrDefault(name);
        // The buildings' list for guerrilla_world (Guerrilla's minimap; see GuerrillaBuildingList), by these entries.
        var buildingEntries = GuerrillaBuildingList.Entries(gameplay.Text);
        var buildingLines = new List<string>();
        // Guerrilla's kiosks for guerrilla_kiosks (see GuerrillaKiosks): the movers with the kiosk flag, and its tables.
        var kiosks = new GuerrillaKiosks(rfgTables, gameplay.Text);
        // Guerrilla's draw distances (see GuerrillaLod): the LOD classes those gameplay entries name.
        var lod = GuerrillaLod.Build(misc, rfgTables, gameplay.LodOf.Values);
        File.WriteAllBytes(Path.Combine(modFolder, "lod_properties.xtbl"), lod.Text);
        log($"Draw distances: {lod.EntryOf.Count} of Guerrilla's LOD classes on the objects, {lod.Added} added to Armageddon's table (mods\\guerrilla_world\\lod_properties.xtbl)");
        string? LodProps(string name) => gameplay.LodOf.GetValueOrDefault(name) is { } c ? lod.EntryOf.GetValueOrDefault(c) : null;
        // Guerrilla's glass tints (see GuerrillaGlass), under our textures' names.
        var glass = GuerrillaGlass.Build(misc, rfgTables, Alias);
        File.WriteAllBytes(Path.Combine(modFolder, "glass.xtbl"), glass.Text);
        log($"Glass tints: {glass.Added} of Guerrilla's added to Armageddon's table under our textures' names (mods\\guerrilla_world\\glass.xtbl)");
        // Guerrilla's effects (see GuerrillaEffectTables): the level objects the objects name, their effects
        // and the visual effects those show, added to Armageddon's tables for the mod to hand the game; each
        // object names its level object there. The visual effects' files, converted (see GuerrillaEffects),
        // and their textures go with the zones whose objects show them. The effects Guerrilla's dust
        // plays round the camera (weather.xtbl's Dust_effects; the guerrilla_world mod plays them) go
        // with every zone, and so do Guerrilla's vehicles' effects and explosions (their fire, smoke,
        // exhaust and sparks: GuerrillaArsenal names these in the vehicles it makes), and Guerrilla's
        // campaign weapons' (muzzle flashes, smoke, tracers: GuerrillaWeapons names these).
        var rfgFiles = new GuerrillaTextures(rfgCache, GuerrillaTextures.EffectArchives);
        var placedProps = propsIn.Values.Concat(layerMoversPlaced.Values).SelectMany(p => p).Select(o => o.Text("Props")).OfType<string>().Where(n => n.Length > 0)
            .GroupBy(n => n, StringComparer.OrdinalIgnoreCase).ToDictionary(g => g.Key, g => g.Count(), StringComparer.OrdinalIgnoreCase);
        var dustEffects = rfgTables.Find("weather.xtbl") is { } weather
            ? XElement.Parse(Encoding.Latin1.GetString(rfgTables.Read(weather))).Descendants("Dust_effects").Elements("Element").Select(e => e.Value.Trim())
                      .Where(n => n.Length > 0).ToList()
            : [];
        var vehicleEffects = GuerrillaArsenal.VehicleEffects(rfgTables, misc);
        // The effects' sounds (an explosion's bang, a hit's knock, a breaking piece, a fire's burning): Guerrilla's
        // cues as events in banks of Armageddon's kind (rfge_<Guerrilla's bank>), which the effects name.
        var effectSounds = new GuerrillaSoundBanks(rfgCache, rfgTables, EffectSoundPart, EffectSoundBankPrefix) { Adpcm = GuerrillaSoundBanks.WorldSoundsAdpcm };
        var effectTables = GuerrillaEffectTables.Build(misc, rfgTables, placedProps, file => rfgFiles.HasEffectFile(file + ".cefct_pc"),
                                                       [.. dustEffects, .. vehicleEffects.Effects, .. GuerrillaWeapons.Effects(rfgTables),
                                                        .. GuerrillaMaterialEvents.Effects(rfgTables), .. GuerrillaFootsteps.Effects(rfgTables),
                                                        .. GuerrillaKiosks.Effects(rfgTables), .. GuerrillaActions.PropEffects(rfgTables),
                                                        .. GuerrillaWheelEffects.Effects(rfgTables), .. GuerrillaPlacedEffects.Effects(guerrillaObjects.Values),
                                                        .. GuerrillaGusts.Effects(rfgTables)],
                                                       DlcExplosions(game), [.. vehicleEffects.Explosions, .. GuerrillaWeapons.Explosions(rfgTables)],
                                                       (cue, explosion) => effectSounds.EffectSound(cue, explosion) is { } sound
                                                           ? new GuerrillaEffectTables.Sound(sound.Event, sound.HeardTo) : null,
                                                       GuerrillaFootsteps.AlsoPlays(rfgTables));
        foreach (string line in effectSounds.Write(game, overrides, modFolder)) log("Effect sounds: " + line);
        log($"Effect sounds: {effectTables.SoundsAdded} of the added effects sound, {effectTables.SoundOnly} of them without a visual" +
            (effectTables.SoundsLeft.Count > 0 ? $"; cues that couldn't be had, their effects silent: {string.Join(", ", effectTables.SoundsLeft)}" : "") +
            (effectSounds.Log.Count > 0 ? $" ({string.Join("; ", effectSounds.Log.Distinct())})" : ""));
        // The music's format test (see GuerrillaMusic): one score's ambience as Wwise ADPCM, for the mod's console command.
        foreach (string line in GuerrillaMusic.BuildFormatTest(rfgCache, rfgTables, game, overrides, modFolder)) log("Music: " + line);
        File.WriteAllBytes(Path.Combine(modFolder, "vfx.xtbl"), effectTables.Visuals);
        File.WriteAllBytes(Path.Combine(modFolder, "effects.xtbl"), effectTables.Effects);
        // What Guerrilla's pieces play as they break, fall and groan (see GuerrillaMaterialEvents): the mod's file.
        var builtEffects = XtblFile.FromBytes(effectTables.Effects).Entries.Select(XtblFile.EntryName).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var builtVisuals = XtblFile.FromBytes(effectTables.Visuals).Entries.Select(XtblFile.EntryName).ToHashSet(StringComparer.OrdinalIgnoreCase);
        log("Material events: " + GuerrillaMaterialEvents.Write(rfgTables, builtEffects, modFolder));
        // What Guerrilla's wheels kick up on each ground (see GuerrillaWheelEffects): the mod's file.
        log("Wheel effects: " + GuerrillaWheelEffects.Write(rfgTables, builtEffects, modFolder));
        // The dust's gusts on each ground (see GuerrillaGusts): the mod's file.
        log("Gusts: " + GuerrillaGusts.Write(rfgTables, builtEffects, modFolder));
        // Guerrilla's people's footsteps (see GuerrillaFootsteps): their entries in the mod's footsteps.xtbl.
        log(GuerrillaFootsteps.Write(rfgTables, misc, modFolder, builtEffects));
        File.WriteAllBytes(Path.Combine(modFolder, "level_objects.xtbl"), effectTables.LevelObjects);
        File.WriteAllBytes(Path.Combine(modFolder, "explosions.xtbl"), effectTables.Explosions);
        File.WriteAllBytes(Path.Combine(modFolder, "explosions_armageddon_cuts.xtbl"), effectTables.ExplosionsArmageddonCuts);
        var effectFiles = new Dictionary<string, List<(string Name, byte[] Data)>>(StringComparer.OrdinalIgnoreCase);
        var effectTextures = new Dictionary<string, (string Name, byte[] Data)[]>(StringComparer.OrdinalIgnoreCase);
        var unshipped = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        (string Name, byte[] Data)[] Unshipped(string texture)
        {
            // An 8 x 8 texture of no colour and no cover.
            unshipped.Add(texture);
            string alias = Alias(texture);
            return [(alias + ".cvbm_pc", PegFile.SingleHeader(alias + ".tga", 8, 8, TextureFormat.Argb8888, 1, 0)), (alias + ".gvbm_pc", new byte[8 * 8 * 4])];
        }
        foreach (string file in effectTables.Files)
        {
            string name = GuerrillaEffectTables.Added(file);
            var (c, g, textures) = GuerrillaEffects.Convert(rfgFiles.EffectFile(file + ".cefct_pc")!, rfgFiles.EffectFile(file + ".gefct_pc"), Alias, name);
            var list = new List<(string Name, byte[] Data)> { (name + ".cefct_pc", c) };
            if (g != null) list.Add((name + ".gefct_pc", g));
            foreach (string texture in textures)
            {
                // The destructible objects' own where they share one (the same file under the same name),
                // else Guerrilla's; one Guerrilla never shipped (14 of its effects' textures) draws nothing.
                if (!effectTextures.ContainsKey(texture))
                    effectTextures[texture] = chunkTextures.GetValueOrDefault(texture) ?? GuerrillaBuiltIns.Files(texture, Alias(texture)) ?? rfgFiles.Files(texture, Alias(texture))
                        ?? Unshipped(texture);
                list.AddRange(effectTextures[texture]);
            }
            effectFiles[file] = list;
        }
        log($"Explosions: {effectTables.ExplosionsAdded} of Guerrilla's added to Armageddon's table (mods\\guerrilla_world\\explosions.xtbl; the game keeps " +
            $"{GuerrillaEffectTables.ExplosionLimit}, Armageddon's and its DLCs' first)" +
            (effectTables.ExplosionsWaiting.Count > 0 ? $"; no room for {string.Join(", ", effectTables.ExplosionsWaiting)} (Armageddon's of the name play instead, where it has one)" : "") +
            (GuerrillaEffectTables.FlagsLeftOut.Count > 0 ? $"; flags Armageddon's table has no name for, left out: {string.Join(", ", GuerrillaEffectTables.FlagsLeftOut)}" : ""));
        log($"Effects: {effectTables.PropsOf.Count} of Guerrilla's level objects, {effectTables.EffectsAdded} effects and {effectTables.VisualsAdded} visual effects added to Armageddon's tables " +
            $"(mods\\guerrilla_world\\level_objects.xtbl, effects.xtbl, vfx.xtbl); {effectFiles.Count} effect files converted with {effectTextures.Count} textures, " +
            $"{effectFiles.Values.Sum(l => l.Sum(f => (long)f.Data.Length)) / 1024:N0} KB" +
            (effectTables.VisualsLeft.Count > 0 ? $"; {effectTables.VisualsLeft.Count} visual effects past the game's room of {GuerrillaEffectTables.VisualLimit} left out: {string.Join(", ", effectTables.VisualsLeft)}" : "") +
            (effectTables.ExplosionsLeft.Count > 0 ? $"; explosions neither added nor Armageddon's left out: {string.Join(", ", effectTables.ExplosionsLeft)}" : "") +
            (unshipped.Count > 0 ? $"; textures Guerrilla never shipped, drawn as nothing: {string.Join(", ", unshipped)}" : ""));
        // The effects' emitters: each emitter of each loaded effect file takes a place in a pool guerrilla_world makes
        // 6,600 (the game's 2,200); Armageddon's own files have 3,457 in all, so ours must stay within the rest.
        int ourEmitters = effectFiles.Values.Sum(l => l.Where(f => f.Name.EndsWith(".cefct_pc", StringComparison.OrdinalIgnoreCase) && f.Data.Length >= 0x34)
                                                    .Sum(f => Math.Max(0, BitConverter.ToInt32(f.Data, 0x30))));
        if (ourEmitters + ArmageddonEmitters > EmitterRoom)
            throw new InvalidDataException($"the effect files' emitters ({ourEmitters}, with Armageddon's {ArmageddonEmitters}) are more than the {EmitterRoom} "
                                           + "guerrilla_world makes room for: a load past it crashes");
        log($"Effects: the effect files' emitters {ourEmitters}, with Armageddon's {ArmageddonEmitters} {ourEmitters + ArmageddonEmitters} of the {EmitterRoom} the game holds");
        string? PropsOf(string name) => effectTables.PropsOf.GetValueOrDefault(name) ?? (levelObjects.Contains(name) ? name : null);

        var schema = ZoneSchema.FromGame(game) ?? throw new InvalidDataException($"no {ZoneSchema.FileName} in the game's {ZoneSchema.ArchiveName}");

        var asm = new AsmFile();
        var asm1 = new AsmFile();
        // The marker that names the world's precache (rfg_precache.vpp_pc; see the precache below): four
        // zero bytes, as every one of the game's is.
        var layer = new List<(string Name, byte[] Data)> { (World + "_precache.precache", new byte[4]) };
        var layer1 = new List<(string Name, byte[] Data)>();
        var zonescript = new List<(string Name, byte[] Data)>();
        uint Class(string name) => NameHashes.ClassHash(name);

        // Where "level rfg_CC_RR" starts in a zone: Guerrilla's own player start there if the zone has
        // one (the one nearest the zone's middle; its world's zones have 155), its place and facing.
        var guerrillaStarts = new Dictionary<(int, int), (Vector3 Position, Matrix4x4 Rotation)>();
        {
            var rfgZoneScripts = Packfile.Open(Path.Combine(rfgCache, "zonescript_terr01.vpp_pc"));
            foreach (var (col, row) in zones)
            {
                if (rfgZoneScripts.Find($"terr01_{col:00}_{row:00}.rfgzone_pc") is not { } entry) continue;
                var corner = Corner(col, row);
                var middle = new Vector2(corner.X + ZoneWidth / 2, corner.Y + ZoneWidth / 2);
                var starts = GuerrillaZones.Objects(rfgZoneScripts.Read(entry))
                    .Where(o => o.ClassHash == Class("player_start") && o.Value("oP") is { Length: 48 })
                    .Select(o =>
                    {
                        byte[] op = o.Value("oP")!;
                        float F(int i) => BitConverter.ToSingle(op, i * 4);
                        return (Position: new Vector3(F(0), F(1), F(2)), Rotation: new Matrix4x4(F(3), F(4), F(5), 0, F(6), F(7), F(8), 0, F(9), F(10), F(11), 0, 0, 0, 0, 1));
                    })
                    .OrderBy(s => Vector2.Distance(new Vector2(s.Position.X, s.Position.Z), middle))
                    .ToList();
                if (starts.Count > 0) guerrillaStarts[(col, row)] = starts[0];
            }
            log($"Player starts: Guerrilla's own in {guerrillaStarts.Count} of {zones.Count} zones, the zone's middle in the rest");
        }

        // ---- The files the zones share (the ground's layers, roads, rocks, chunk files and their
        // textures, effects), placed in every zone's containers first: the precache (below) is chosen
        // from them, so each zone's containers are made and packed as soon as its own files are (see
        // Pack), not all the zones' files held to the end (GBs).
        void AddChunks(List<(string Name, byte[] Data)> to, IEnumerable<string> names)
        {
            foreach (string name in names)
            {
                var chunk = chunkFiles[name];
                to.Add((name + ".cchk_pc", chunk.Header));
                to.Add((name + ".gchk_pc", chunk.Data));
                foreach (string texture in chunk.Textures) to.AddRange(chunkTextures[texture]);
            }
        }
        var sharedIn = new Dictionary<(int, int), (List<(string Name, byte[] Data)> Zone, List<(string Name, byte[] Data)>[] Parts)>();
        foreach (var (col, row) in zones)
        {
            string zone = $"{World}_{col:00}_{row:00}";
            var corner = Corner(col, row);

            // The chunk files go where Armageddon keeps them, in the container of the sub-zone each object
            // stands in (its third of the zone each way), each container's share kept within ChunkBudget;
            // what doesn't fit goes into the zone's container, which loads with the whole zone, and if
            // that is full too, into the nearest of the zone's other sub-zones with room. A general_mover
            // that others are attached to (Guerrilla's substations, its reactor core) goes in the zone's
            // container first: one whose chunk isn't loaded yet isn't made, and the objects attached to
            // it, made when the zone streams in, lose it ("Invalidating parent handle ...").
            var chunksIn = Enumerable.Range(0, 9).Select(_ => new List<string>()).ToArray();
            var zoneChunks = new List<string>();
            {
                var placedHere = propsIn[(col, row)];
                var parentHandles = placedHere.Select(p => p.Parent).ToHashSet();
                var parentChunks = placedHere.Where(p => p.ClassHash == generalMover && parentHandles.Contains(p.Handle))
                    .Select(p => Path.GetFileNameWithoutExtension(p.Text("chunk_name")!)).ToHashSet(StringComparer.OrdinalIgnoreCase);
                var wanted = Enumerable.Range(0, 9).Select(_ => new HashSet<string>(StringComparer.OrdinalIgnoreCase)).ToArray();
                foreach (var p in placedHere)
                {
                    byte[] placement = p.Value("oP")!;
                    float x = BitConverter.ToSingle(placement, 0) - corner.X, z = BitConverter.ToSingle(placement, 8) - corner.Y;
                    string chunk = Path.GetFileNameWithoutExtension(p.Text("chunk_name")!);
                    if (!parentChunks.Contains(chunk)) wanted[Math.Clamp((int)(x / third), 0, 2) + 3 * Math.Clamp((int)(z / third), 0, 2)].Add(chunk);
                }
                long Cost(string name) => chunkFiles[name].Header.Length + (long)chunkFiles[name].Data.Length;
                long zoneCost = 0;
                foreach (string name in parentChunks)
                {
                    zoneChunks.Add(name);
                    zoneCost += Cost(name);
                }
                // An activity layer's movers' chunks (GuerrillaActivityLayers) go in the zone's container too: the layer is
                // made while its activity runs, wherever in the zone the player is, so they must load with the whole zone.
                foreach (string name in layerMoversPlaced[(col, row)].Select(p => Path.GetFileNameWithoutExtension(p.Text("chunk_name")!)).Distinct(StringComparer.OrdinalIgnoreCase))
                    if (!zoneChunks.Contains(name, StringComparer.OrdinalIgnoreCase))
                    {
                        zoneChunks.Add(name);
                        zoneCost += Cost(name);
                    }
                var partCost = new long[9];
                foreach (int n in Enumerable.Range(0, 9).OrderByDescending(n => wanted[n].Sum(Cost)))
                    foreach (string name in wanted[n].OrderByDescending(Cost))
                    {
                        if (zoneChunks.Contains(name, StringComparer.OrdinalIgnoreCase)) continue;
                        int to = n;
                        if (partCost[n] + Cost(name) > ChunkBudget)
                        {
                            if (zoneCost + Cost(name) <= ChunkBudget)
                            {
                                zoneChunks.Add(name);
                                zoneCost += Cost(name);
                                continue;
                            }
                            int nearest = Enumerable.Range(0, 9).Where(m => m != n && partCost[m] + Cost(name) <= ChunkBudget)
                                .OrderBy(m => Math.Abs(m % 3 - n % 3) + Math.Abs(m / 3 - n / 3)).ThenBy(m => partCost[m]).DefaultIfEmpty(n).First();
                            if (nearest != n) log($"  {zone}: {name} (for sub-zone {n}) put in sub-zone {nearest}, the nearest with room");
                            to = nearest;
                        }
                        if (chunksIn[to].Contains(name, StringComparer.OrdinalIgnoreCase)) continue;
                        chunksIn[to].Add(name);
                        partCost[to] += Cost(name);
                    }
                for (int n = 0; n < 9; n++)
                    if (partCost[n] > ChunkBudget)
                        log($"  {zone}: sub-zone {n} holds {partCost[n] / (1 << 20):0} MB of chunk files, more than the {ChunkBudget >> 20} MB a container is given; it may not load");
            }

            // The zone's container: its ground's layers, roads, rocks, chunk files and the effects its
            // objects show (with the zone, as Guerrilla's own world's are with its level); each
            // sub-zone's: its chunk files.
            var files = new List<(string Name, byte[] Data)>();
            foreach (string texture in looks[(col, row)].TerrainTextures) files.AddRange(layerFiles[texture]);
            foreach (string texture in roadTexturesIn[(col, row)]) files.AddRange(roadTextures[texture]);
            foreach (string name in rocksIn[(col, row)].Select(r => r.Name).Distinct(StringComparer.OrdinalIgnoreCase))
            {
                var piece = rockPieces[name];
                files.Add((name + ".cstch_pc", piece.Header));
                files.Add((name + ".gstch_pc", piece.Data));
                foreach (string texture in piece.Textures) files.AddRange(rockTextures[texture]);
            }
            AddChunks(files, zoneChunks);
            // The meshes its undergrowth scatters (under Guerrilla's names, as its terrain file names
            // them) and their textures: in more than one zone each, so kept once, in the precache.
            if (RfgFile(col, row, $"terr01_{col:00}_{row:00}.cterrain_pc") is { } guerrillaTerrainFile &&
                GuerrillaTerrain.Undergrowth(guerrillaTerrainFile) is { } undergrowth)
                foreach (string mesh in GuerrillaTerrain.UndergrowthMeshes(undergrowth.Section, undergrowth.Layers))
                    files.AddRange(UndergrowthFiles(col, row, mesh));
            foreach (string file in propsIn[(col, row)].Concat(layerMoversPlaced[(col, row)]).Select(o => o.Text("Props")).OfType<string>()
                         .SelectMany(p => effectTables.FilesOf.GetValueOrDefault(p) ?? []).Concat(effectTables.WorldFiles).Distinct(StringComparer.OrdinalIgnoreCase))
                files.AddRange(effectFiles[file]);
            var parts = chunksIn.Select(names =>
            {
                var partFiles = new List<(string Name, byte[] Data)>();
                AddChunks(partFiles, names);
                return partFiles.DistinctBy(f => f.Name, StringComparer.OrdinalIgnoreCase).ToList();
            }).ToArray();
            sharedIn[(col, row)] = (files.DistinctBy(f => f.Name, StringComparer.OrdinalIgnoreCase).ToList(), parts);
        }

        // ---- The precache: every file more than one container holds (the ground's layers, roads,
        // rocks, chunk files and their textures) is kept once, loose, in the world's precache archive
        // (rfg_precache.vpp_pc), and each cell's precache group in the asset list (0p_cXXYY for a
        // sub-zone, 1p_cXXYY for a zone: type 17, flags 0x200, no data, a primitive for each header
        // file) names those it needs: the game loads each of them once, while any cell that names it
        // is loaded. So Guerrilla keeps its shared files (terr01_precache.vpp_pc and a precache group
        // for every cell) and Armageddon its areas' (surface_precache.vpp_pc, 0p_ and 1p_ groups in
        // world_l0.asm_pc). The guerrilla_world mod has the game mount it (its area for our worlds).
        // (The zones' own files, their ground's, terrain and zone files, are each in one container, so
        // only the shared ones are counted.)
        var holders = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        var lengths = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        var differing = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var (zoneFiles, parts) in sharedIn.Values)
            foreach (var (name, data) in parts.Prepend(zoneFiles).SelectMany(files => files))
            {
                holders[name] = holders.GetValueOrDefault(name) + 1;
                if (lengths.TryGetValue(name, out int length) && length != data.Length) differing.Add(name);
                lengths[name] = data.Length;
            }
        // Which of them: the game reads the directories of a level's archives into one pool of
        // 225,280 bytes ("temp packfile"; 0xCB5B40 asks it for an archive's directory and names and
        // 16 bytes, else stops with "Out of space in temp packfile"), and our first and second layers
        // and zone archive take about 52 KB of it. So the precache's directory (24 bytes a file) and
        // names are kept within PrecacheDirectoryBudget: the files that save the most memory go first
        // (their size times the containers past the first that would hold them), each with its data
        // file; the rest stay in their containers.
        static string UnitOf(string name)
        {
            string ext = Path.GetExtension(name);
            return ext.Length > 2 && (ext[1] is 'g' or 'G') ? Path.ChangeExtension(name, ".c" + ext[2..]) : name;
        }
        var inPrecache = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        int directory = 16, left = 0;
        // (The undergrowth's meshes first, small as they are: the game keeps the foliage meshes loaded in
        // a table of 64, which a copy in every zone's container would crowd.)
        foreach (var unit in holders.Keys.Where(n => holders[n] > 1 && !differing.Contains(n))
                                         .GroupBy(UnitOf, StringComparer.OrdinalIgnoreCase)
                                         .OrderByDescending(u => u.Key.EndsWith(".cfmesh_pc", StringComparison.OrdinalIgnoreCase))
                                         .ThenByDescending(u => (long)(u.Max(n => holders[n]) - 1) * u.Sum(n => (long)lengths[n])))
        {
            int cost = unit.Sum(n => 24 + n.Length + 1);
            if (directory + cost > PrecacheDirectoryBudget)
            {
                left += unit.Count();
                continue;
            }
            directory += cost;
            inPrecache.UnionWith(unit);
        }
        bool Shared(string name) => inPrecache.Contains(name);
        var precacheFiles = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
        var textureFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase);  // every texture file in the world's containers
        long before = 0, after = 0;
        // A container of the world (0h_cXXYY for a sub-zone, 1h_cXXYY for a zone, which goes in the
        // zones' layer) with its files, packed: those the precache holds left out, and its asset list
        // entries made.
        void Pack(string name, bool zoneLayer, IReadOnlyList<(string Name, byte[] Data)> files)
        {
            var kept = files.Where(f => !Shared(f.Name)).ToList();
            var fromPrecache = files.Where(f => Shared(f.Name)).ToList();
            before += files.Sum(f => (long)f.Data.Length);
            after += kept.Sum(f => (long)f.Data.Length);
            textureFiles.UnionWith(files.Select(f => f.Name).Where(n => n.EndsWith(".cvbm_pc", StringComparison.OrdinalIgnoreCase)));
            foreach (var (file, data) in fromPrecache) precacheFiles.TryAdd(file, data);
            byte[] bytes = Packfile.CreateContainer(kept);
            var own = ContainerEntry(name, bytes, kept);
            var precacheGroup = new AsmContainer { Name = name[..1] + "p" + name[2..], Type = 17, Flags = 0x200 };
            precacheGroup.Primitives.AddRange(fromPrecache.Where(f => IsHeader(f.Name)).Select(f => PrimitiveOf(f.Name, fromPrecache)));
            if (precacheGroup.Primitives.Count > 0) asm.Containers.Add(precacheGroup);  // all in layer 0's list, as Guerrilla's
            if (zoneLayer)
            {
                asm1.Containers.Add(own);
                layer1.Add((name + ".str2_pc", bytes));
                // The zone's group: everything the zone needs, its container's and its precache group's.
                var group = new AsmContainer { Name = "1_" + name[3..], Type = 17, Flags = 0x2300 };
                group.Primitives.AddRange(own.Primitives.Concat(precacheGroup.Primitives).Select(p => new AsmPrimitive
                {
                    Name = p.Name, Type = p.Type, Allocator = p.Allocator, Flags = p.Flags, SplitIndex = p.SplitIndex, HeaderSize = p.HeaderSize, DataSize = p.DataSize,
                }));
                asm.Containers.Add(group);  // the groups stay in layer 0's list, as Guerrilla's
            }
            else
            {
                asm.Containers.Add(own);
                layer.Add((name + ".str2_pc", bytes));
            }
        }

        // Guerrilla's people (see GuerrillaPopulation, GuerrillaDistricts): the patrols whose squad GuerrillaPeople
        // puts in the squad tables, with their squad spawn nodes and route navpoints, kept within the game's 72
        // patrols (Guerrilla's; guerrilla_people gives it them) for every 3 × 3 block of zones; and the squads standing where Guerrilla puts them (at its
        // buildings, bounding boxes and action nodes), kept within MostPeople people a block with the patrols'. The
        // squads are worked out from Guerrilla's own tables for the people it makes (its default ones, and any its
        // last run made), so the world needn't wait for it: a squad it doesn't write after all (its weapons not
        // ported) the game drops, with a warning. A zone's civilian and guerrilla squads are its human group's
        // versions, and the zone names its group's spawn resource (its district's people; rfg_Default where it has none).
        var peopleSquads = GuerrillaSquads.Expected(rfgTables, misc, GuerrillaSquads.PlannedCharacters(modFolder));
        var ambientGroups = GuerrillaDistricts.AmbientGroups(rfgTables);
        var districtGroups = GuerrillaDistricts.DistrictGroups(ambientGroups.Values).ToHashSet(StringComparer.OrdinalIgnoreCase);
        uint zoneMarkerClass = NameHashes.ClassHash("obj_zone");
        string? GroupOf((int, int) zone) =>
            guerrillaObjects.TryGetValue(zone, out var objects) && objects.Values.FirstOrDefault(o => o.ClassHash == zoneMarkerClass) is { } marker &&
            GuerrillaDistricts.ZoneGroup(marker, ambientGroups) is { } group && districtGroups.Contains(group) ? group : null;
        Func<string, string> SquadsOf((int, int) zone) { string? group = GroupOf(zone); return squad => GuerrillaDistricts.SquadFor(squad, group); }
        // The zones' spawn resources (GuerrillaSquads), written here holding only the player where GuerrillaPeople
        // hasn't run yet (a zone naming a resource the table hasn't would take Armageddon's "Default", which
        // streams its aliens and soldiers in).
        bool resourceWritten = GuerrillaSquads.EnsureSpawnResources(rfgTables, misc, modFolder);
        log($"People: the zones' spawn resources are {string.Join(", ", GuerrillaSquads.SpawnResources(rfgTables))} (rfg_Default where a zone has no human group)" +
            (resourceWritten ? " (written with the player only; building the people adds theirs)" : ""));
        var patrolsIn = GuerrillaPopulation.Cap(guerrillaObjects.ToDictionary(z => z.Key, z => GuerrillaPopulation.Patrols(z.Value, peopleSquads, SquadsOf(z.Key))), out int patrolsLeftOut);
        log(peopleSquads.Count == 0 ? "People: none of Guerrilla's squads can be had with the people built so far, so no patrols placed"
            : $"People: {patrolsIn.Values.Sum(p => p.Count)} of Guerrilla's patrols placed" +
              (patrolsLeftOut > 0 ? $"; {patrolsLeftOut} left out to keep {GuerrillaPopulation.MostPatrols} patrols at most within each 3 x 3 block of zones" : ""));
        // The standing squads, kept within MostPeople people a 3 × 3 block with the patrols' (the squads' sizes, Guerrilla's).
        var squadSizes = XtblFile.FromBytes(rfgTables.Read(rfgTables.Find(GuerrillaSquads.SquadTable)!)).Entries
            .ToDictionary(XtblFile.EntryName, e => int.TryParse(e.ValueOf("member_count"), out int n) ? n : 1, StringComparer.OrdinalIgnoreCase);
        // A squad's size, Guerrilla's: a district version (rfg_du_csq_2) is its Guerrilla squad's (civilian_squad_2).
        int Members(string squad) => squadSizes.TryGetValue(GuerrillaDistricts.GuerrillaSquadOf(squad), out int n) ? n : 1;
        uint patrolClass = NameHashes.ClassHash("object_patrol"), squadNodeClass = NameHashes.ClassHash("object_squad_spawn_node");
        var patrolPeople = patrolsIn.ToDictionary(p => p.Key, p => p.Value.Sum(handle =>
            guerrillaObjects[p.Key].Values.FirstOrDefault(o => o.ClassHash == squadNodeClass && o.Parent == handle) is { } node ? Members(node.Text("squad_def") ?? "") : 0));
        var standingAll = guerrillaObjects.ToDictionary(z => z.Key, z => GuerrillaPopulation.StandingSquads(z.Value, peopleSquads, SquadsOf(z.Key)));
        var standingIn = GuerrillaPopulation.CapPeople(standingAll, patrolPeople, Members, out int standingLeftOut,
                                                       (z, handle) => GuerrillaPopulation.StandingRank(guerrillaObjects[z], handle));
        int RankCount(Dictionary<(int, int), List<(uint Handle, string Squad)>> lists, int rank) =>
            lists.Sum(z => z.Value.Count(s => GuerrillaPopulation.StandingRank(guerrillaObjects[z.Key], s.Handle) == rank));
        log($"People: the cap keeps squads under action nodes first ({RankCount(standingIn, 2)} of {RankCount(standingAll, 2)}), then guard posts' " +
            $"({RankCount(standingIn, 1)} of {RankCount(standingAll, 1)}), then the rest ({RankCount(standingIn, 0)} of {RankCount(standingAll, 0)})");
        log($"People: {standingIn.Values.Sum(s => s.Count)} of Guerrilla's squads standing where it puts them ({standingIn.Values.Sum(s => s.Sum(x => Members(x.Squad)))} people)" +
            (standingLeftOut > 0 ? $"; {standingLeftOut} left out to keep {GuerrillaPopulation.MostPeople} people at most within each 3 x 3 block of zones" : "") +
            $"; squads: {string.Join(", ", peopleSquads.Order())}");
        // Guerrilla's Marauder ambush regions: their squad nodes on foot are placed (not counted in the cap: they spawn only
        // when their region fires) and the regions written for guerrilla_people (ambush.cpp).
        var ambushIn = guerrillaObjects.ToDictionary(z => z.Key, z => GuerrillaPopulation.AmbushRegions(z.Value, peopleSquads, SquadsOf(z.Key)));
        // Guerrilla's story people at the safehouses: their special NPCs' and VIPs' squad nodes, placed outside the cap
        // (guerrilla_people holds them and spawns one special NPC and one VIP a safehouse, as Guerrilla's resource
        // manager streams them) and written to story_nodes.txt with their characters (the squad's special_npc).
        var storyIn = guerrillaObjects.ToDictionary(z => z.Key, z => GuerrillaPopulation.StoryNodes(z.Value, peopleSquads, SquadsOf(z.Key)));
        var storyCharacter = XtblFile.FromBytes(rfgTables.Read(rfgTables.Find(GuerrillaSquads.SquadTable)!)).Entries
            .Where(e => e.ValueOf("special_npc")?.Trim() is { Length: > 0 })
            .ToDictionary(XtblFile.EntryName, e => e.ValueOf("special_npc")!.Trim(), StringComparer.OrdinalIgnoreCase);
        // The EDF's squad nodes placed, for guerrilla_campaign's reinforcements (Guerrilla's build_node_info, RM 0x787320:
        // any loaded squad node whose squad's team is allied to the EDF is a place an EDF reinforcement squad on foot may
        // be made, 80 to 160 m from the player).
        var edfSquads = XtblFile.FromBytes(rfgTables.Read(rfgTables.Find(GuerrillaSquads.SquadTable)!)).Entries
            .Where(e => string.Equals(e.ValueOf("team_name")?.Trim(), "EDF", StringComparison.OrdinalIgnoreCase))
            .Select(XtblFile.EntryName).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var reinforcementLines = new List<string>
        {
            "; The EDF's squad nodes as placed, for guerrilla_campaign's reinforcements (places on foot), written by the world build.",
            "; node <the squad node's handle as placed> <place x y z>",
        };
        var guardLines = new List<string>
        {
            "; Guerrilla's guard posts for guerrilla_people's guards.cpp (its guard post AI), written by the world build.",
            "; guard <the post's bounding box as placed> <the guard node's place x y z> <its facing x z> <its scan angle left, right (degrees)> <its group_id>",
        };
        var storyLines = new List<string>
        {
            "; Guerrilla's story people at the safehouses for guerrilla_people's story.cpp, written by the world build.",
            "; <special|vip> <the squad node's handle as placed> <its squad> <its character> <place x y z> <its group: the top of its parents in Guerrilla's zone> <zone col row>",
        };
        int storyNodesPlaced = 0;
        var ambushLines = new List<string>
        {
            "; Guerrilla's Marauder ambush regions (marauder_ambush_region) for guerrilla_people's ambush.cpp, written by the world build.",
            "; region <handle> <place x y z> <axes: right, up, forward rows> <box least x y z> <box most x y z> <enabled> <day chance> <night chance> <least> <most>",
            "; node <region> <the squad node's handle as placed> <its squad>",
        };
        int ambushNodesPlaced = 0, ambushRiding = 0, ambushRegionsWritten = 0;
        // The squads the zones name, for the people build (GuerrillaSquads.PlacedList): it writes only the district
        // versions placed, as the game keeps few squads.
        {
            var placedSquads = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var (zone, handles) in patrolsIn)
                foreach (uint handle in handles)
                    if (guerrillaObjects[zone].Values.FirstOrDefault(o => o.ClassHash == squadNodeClass && o.Parent == handle) is { } node)
                        placedSquads.Add(SquadsOf(zone)(node.Text("squad_def") ?? ""));
            foreach (var (_, list) in standingIn)
                foreach (var (_, squad) in list) placedSquads.Add(squad);
            foreach (var (_, regions) in ambushIn)
                foreach (var (_, onFoot, riding) in regions)
                    foreach (var (_, squad) in onFoot.Concat(riding)) placedSquads.Add(squad);
            foreach (var (_, nodes) in storyIn)
                foreach (var node in nodes) placedSquads.Add(node.Squad);
            Directory.CreateDirectory(modFolder);
            File.WriteAllLines(Path.Combine(modFolder, GuerrillaSquads.PlacedList), placedSquads);
            log($"People: the zones name {placedSquads.Count} squads ({placedSquads.Count(s => s.StartsWith("rfg_", StringComparison.OrdinalIgnoreCase))} district versions)");
        }

        int pedestriansPlaced = 0;  // Guerrilla's pedestrian navpoints, for the ambient people (counted as the zones are written)
        int guardPostsPlaced = 0;   // Guerrilla's guard posts' boxes, with their squads' nodes under them (GuerrillaPopulation.GuardBoxOf)
        int nodalPlaced = 0, nodalOnMovers = 0, nodalLeftOut = 0, guardAnchorsMoved = 0;  // Guerrilla's node network for people (GuerrillaPopulation.Nodal)
        int nodalOnOwnCell = 0, nodalOnNearestCell = 0, nodalWithoutCell = 0, ladderPairs = 0, laddersAlone = 0, ladderTopsAsNodes = 0;  // its transitions' cells, its ladders
        int pedestriansOnNearestCell = 0, pedestriansWithoutCell = 0;  // pedestrians with no switched-on cell under them
        float pedestriansFarthestCell = 0;
        float nodalFarthestCell = 0;
        var transitionCells = new List<string>();  // mods\guerrilla_world\transition_cells.txt, for guerrilla_people
        uint guardBoxClass = NameHashes.ClassHash("object_bounding_box");
        var vehiclePlaceProblems = new List<string>();
        var roadsMoved = new List<string>();  // roads whose own position was moved into their zone's square
        int laddersPlaced = 0, laddersLeftOut = 0, laddersInZone = 0;  // Guerrilla's ladders, each on its building (GuerrillaLadders)
        int placedEffects = 0, placedEffectsLeftOut = 0;  // Guerrilla's placed effects (GuerrillaPlacedEffects)
        int hingesPlaced = 0, hingesLeftOut = 0, hingesWithMotors = 0;  // Guerrilla's hinges, on the objects they join (GuerrillaConstraints)
        int cuttersPlaced = 0, cuttersLeftOut = 0, cuttersInZone = 0;  // Guerrilla's shape cutters, its pre-made damage (GuerrillaShapeCutters)
        var turbineLines = new List<string>();  // its wind turbines' objects, for guerrilla_world (GuerrillaConstraints.TurbineList)
        int roadsPlaced = 0, spotsPlaced = 0;
        // Guerrilla's pickups (its ammo boxes, weapon lockers, propane tanks, singularity bombs; see GuerrillaPickups),
        // under the names the weapons build gives them.
        var (pickupItems, pickupWeapons) = GuerrillaWeapons.PickupNames(game);
        var pickupsLeft = new SortedDictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        int pickupsPlaced = 0;
        foreach (var (col, row) in zones)
        {
            string zone = $"{World}_{col:00}_{row:00}";
            bool isStart = (col, row) == start;
            var look = looks[(col, row)];
            var h = heights[(col, row)];
            var corner = Corner(col, row);
            var zoneMiddle = new Vector3(corner.X + ZoneWidth / 2, 0, corner.Y + ZoneWidth / 2);
            uint prefix = (uint)(col * 16 + row) << 24;

            // The ground, its cliffs where Guerrilla's sub-zones draw theirs. The blend map as Guerrilla's
            // (ARGB): its alpha is how the two-texture cliffs mix their textures (the ground's shader
            // doesn't read it); close to its red, the cliff weight, but painted on its own (terr01_12_11's
            // differs from red by more than 8 in 3% of texels).
            var blend = new RgbaImage(look.Blend.Width, look.Blend.Height);
            Array.Copy(look.Blend.Pixels, blend.Pixels, blend.Pixels.Length);
            // Drawn under the road decals (see GuerrillaGround.UnderRoads); walked on as sampled.
            var subzones = GuerrillaGround.Subzones(GuerrillaGround.BelowRoads(h, roadSurfaces[(col, row)]), corner, null, cliffCovers[(col, row)], Layout,
                                                    holes[(col, row)], roadsIn[(col, row)], OwnCliffs(look) ? look.Cliffs.Count : 3);
            string blendName = $"rfg{col:00}{row:00}_a00", colourName = $"rfg{col:00}{row:00}cmb";
            string guerrillaZone = $"terr01_{col:00}_{row:00}";
            byte[] guerrillaTerrain = RfgFile(col, row, guerrillaZone + ".cterrain_pc") ?? throw new InvalidDataException($"no {guerrillaZone}.cterrain_pc");
            var (terrainFile, terrainData, farLowered, roadSamples) = ZoneTerrain(h, corner, look, barriers[(col, row)], zone, blendName, colourName, guerrillaTerrain,
                RfgFile(col, row, guerrillaZone + ".gterrain_pc", 1) ?? throw new InvalidDataException($"no {guerrillaZone}.gterrain_pc"), roadSurfaces[(col, row)]);
            // The blend map uncompressed with one level, data not colour; the colour map DXT1 with its
            // levels down to 4 × 4: as the game's own ground maps are kept.
            byte[] blendData = TextureCodec.Encode(TextureFormat.Argb8888, blend);
            int colourLevels = (int)Math.Log2(Math.Min(look.Colour.Width, look.Colour.Height)) - 1;
            byte[] colourData = TextureCodec.EncodeWithMips(TextureFormat.Dxt1, look.Colour, colourLevels);

            // Objects, made from the game's schema (each class's properties at their defaults, as Volition's
            // tools wrote them), as each of Armageddon's world zones has them: the zone's marker, an
            // environment box over the whole zone (every height; see ZoneEnvironment) and a player start
            // (so "level rfg_CC_RR" starts in any zone: Guerrilla's own where the zone has one, else the
            // zone's middle on the ground, which can be inside a rock or a building).
            var zoneFile = ZoneFile.Create(Class("Default"), Class("none"));
            // (Their handles clear of Guerrilla's roads' in the zone, which keep theirs: their junctions name them;
            // the other Guerrilla objects placed take another handle if theirs is taken.)
            uint roadClass = Class("object_path_road");
            var roadHandles = guerrillaObjects.TryGetValue((col, row), out var guerrillaHere)
                ? guerrillaHere.Values.Where(o => o.ClassHash == roadClass).Select(o => o.Handle).ToHashSet() : [];
            uint OwnHandle(uint handle)
            {
                while (roadHandles.Contains(handle)) handle = (handle & 0xFFFF0000) | ((handle + 1) & 0xFFFF);
                return handle;
            }
            var marker = zoneFile.AddNew(Class("obj_zone"), OwnHandle(prefix | 0x000020), schema.Class("obj_zone")!.NewObject(zoneMiddle, DefaultChoice),
                                         zoneMiddle - new Vector3(0.5f), zoneMiddle + new Vector3(0.5f));
            marker.Property("terrain_file_name")!.SetText(zone);
            marker.Property("spawn_resource_data")!.SetText(GuerrillaDistricts.ResourceOf(GroupOf((col, row))));
            ZoneEnvironment(zoneFile, schema, OwnHandle(prefix | 0x030008), zoneMiddle);
            Vector3 startAt = new(zoneMiddle.X, GuerrillaGround.HeightAt(h, ZoneWidth / 2, ZoneWidth / 2, Layout) + 1, zoneMiddle.Z);
            Matrix4x4? startTurn = null;
            if (guerrillaStarts.TryGetValue((col, row), out var guerrillaStart))
            {
                // (Never below the ground: lifted onto it if it were.)
                float ground = GuerrillaGround.HeightAt(h, guerrillaStart.Position.X - corner.X, guerrillaStart.Position.Z - corner.Y, Layout);
                startAt = guerrillaStart.Position with { Y = Math.Max(guerrillaStart.Position.Y, ground + 0.5f) };
                startTurn = guerrillaStart.Rotation;
            }
            ZoneObject PlayerStart(ZoneFile file)
            {
                var start = file.AddNew(Class("player_start"), OwnHandle(prefix | 0x000004), schema.Class("player_start")!.NewObject(startAt),
                                        startAt - new Vector3(0.5f), startAt + new Vector3(0.5f));
                if (startTurn is { } turn) start.TurnTo(turn);
                return start;
            }
            PlayerStart(zoneFile);
            // The destructible objects, attached and linked as in Guerrilla's zone.
            byte[] links = [];
            if (propsIn[(col, row)].Count > 0)
            {
                var taken = new HashSet<uint>(zoneFile.Objects.Select(o => o.Handle));
                (var movers, links) = GuerrillaMovers.Convert(propsIn[(col, row)], guerrillaObjects[(col, row)], schema,
                                                               PropsOf, GameplayProps, LodProps,
                                                               chunk => BitConverter.ToUInt32(chunkFiles[chunk].Header, 0x24), taken);
                foreach (var mover in movers) zoneFile.Insert(zoneFile.Objects.Count, mover);
                // Objects whose saved anchor data Guerrilla left stale (the game shows "out of date anchor data" for
                // those without a correction of ours: GuerrillaAnchorFixes).
                var stale = GuerrillaMovers.StaleAnchorData(propsIn[(col, row)], chunk => BitConverter.ToUInt32(chunkFiles[chunk].Header, 0x24));
                if (stale.Count > 0)
                    log($"{zone}: {stale.Count(s => s.Fix != null)} objects' stale anchor data replaced by what the game works out (" +
                        string.Join(", ", stale.Where(s => s.Fix != null).Select(s => $"{s.Object.Handle:X8}")) + ")" +
                        (stale.Any(s => s.Fix == null) ? $"; still stale, the game warns: " + string.Join(", ", stale.Where(s => s.Fix == null)
                            .Select(s => $"{s.Object.Handle:X8} {Path.GetFileNameWithoutExtension(s.Object.Text("chunk_name") ?? "")}")) : ""));
            }
            // Guerrilla's ladders, each on its building placed above (see GuerrillaLadders).
            if (guerrillaObjects.TryGetValue((col, row), out var rfgLadderZone))
            {
                var taken = new HashSet<uint>(zoneFile.Objects.Select(o => o.Handle));
                var ladders = GuerrillaLadders.Convert(rfgLadderZone, zoneFile.Objects, taken, out int laddersLeft);
                foreach (var ladder in ladders) zoneFile.Insert(zoneFile.Objects.Count, ladder);
                laddersPlaced += ladders.Count;
                laddersLeftOut += laddersLeft;
                laddersInZone = Math.Max(laddersInZone, ladders.Count);
                // Guerrilla's placed effects (the EDF's force fields, the Mohole's smoke and ash; see GuerrillaPlacedEffects).
                var effectsHere = GuerrillaPlacedEffects.Convert(rfgLadderZone, taken, builtEffects, builtVisuals, out int effectsLeft);
                foreach (var effect in effectsHere) zoneFile.Insert(zoneFile.Objects.Count, effect);
                placedEffects += effectsHere.Count;
                placedEffectsLeftOut += effectsLeft;
                // Guerrilla's hinges (the turbines' blades with their motors, the cranes' parts), each on the objects
                // it joins placed above (see GuerrillaConstraints).
                var turbinesHere = new List<GuerrillaConstraints.Turbine>();
                var hinges = GuerrillaConstraints.Convert(rfgLadderZone, zoneFile.Objects, taken, out int hingesLeft, out int hingeMotors, turbinesHere);
                foreach (var hinge in hinges) zoneFile.Insert(zoneFile.Objects.Count, hinge);
                turbineLines.AddRange(turbinesHere.Select(t => $"{zone} {t.Tower:X8} {t.Blades:X8} {t.Motor:X8} {t.Hinge:X8}"));
                hingesPlaced += hinges.Count;
                hingesLeftOut += hingesLeft;
                hingesWithMotors += hingeMotors;
                // Guerrilla's shape cutters (its pre-made damage: the Mohole's broken pipes), each on the building it
                // cuts placed above (see GuerrillaShapeCutters).
                var cutters = GuerrillaShapeCutters.Convert(rfgLadderZone, zoneFile.Objects, taken, out int cuttersLeft);
                foreach (var cutter in cutters) zoneFile.Insert(zoneFile.Objects.Count, cutter);
                cuttersPlaced += cutters.Count;
                cuttersLeftOut += cuttersLeft;
                cuttersInZone = Math.Max(cuttersInZone, cutters.Count);
            }
            // Guerrilla's patrols with their squads and route navpoints, its standing squads, and its pedestrian network.
            var patrolsHere = patrolsIn.GetValueOrDefault((col, row)) ?? [];
            var ambushHere = ambushIn.GetValueOrDefault((col, row)) ?? [];
            var storyHere = storyIn.GetValueOrDefault((col, row)) ?? [];
            var standingHere = (standingIn.GetValueOrDefault((col, row)) ?? []).Concat(ambushHere.SelectMany(r => r.OnFoot.Concat(r.Riding)))
                .Concat(storyHere.Select(n => (n.Handle, n.Squad))).ToList();
            int pedestriansHere = guerrillaObjects.TryGetValue((col, row), out var rfgHere) ? GuerrillaPopulation.Pedestrians(rfgHere).Count : 0;
            pedestriansPlaced += pedestriansHere;
            int nodalHere = rfgHere != null ? GuerrillaPopulation.Nodal(rfgHere).Count : 0;  // its node network (stairs, paths on walls, doors)
            if (patrolsHere.Count > 0 || standingHere.Count > 0 || pedestriansHere > 0 || nodalHere > 0)
            {
                var taken = new HashSet<uint>(zoneFile.Objects.Select(o => o.Handle));
                // The movers placed above, by Guerrilla's handle (the same object: its number too), for the navpoints on them.
                uint nodalMoverClass = NameHashes.ClassHash("rfg_mover"), nodalGeneralMoverClass = NameHashes.ClassHash("general_mover");
                var placedMovers = zoneFile.Objects.Where(o => o.ClassHash == nodalMoverClass || o.ClassHash == nodalGeneralMoverClass)
                                                   .GroupBy(o => o.Handle).ToDictionary(g => g.Key, g => g.First());
                var rfgZone = guerrillaObjects[(col, row)];
                ZoneObject? PlacedMover(uint handle) =>
                    placedMovers.TryGetValue(handle, out var m) && rfgZone.TryGetValue(handle, out var g) && g.Number == m.Number ? m : null;
                // The people's mesh of this zone and the zones round it, for the cells the transitions stand on.
                var peopleLayers = Enumerable.Range(-1, 3).SelectMany(dc => Enumerable.Range(-1, 3).Select(dr => (col + dc, row + dr)))
                    .Where(z => navigation.TryGetValue(z, out var n) && n.People != null).Select(z => (z, navigation[z].People!)).ToList();
                var people = GuerrillaPopulation.Convert(rfgZone, patrolsHere, standingHere, SquadsOf((col, row)), schema, taken, PlacedMover,
                                                         GuerrillaPopulation.CellFinder(peopleLayers),
                                                         GuerrillaPopulation.CellFinder(peopleLayers, float.PositiveInfinity), out var nodalCount);
                nodalOnOwnCell += nodalCount.OnOwnCell;
                nodalOnNearestCell += nodalCount.OnNearestCell;
                nodalWithoutCell += nodalCount.WithoutCell;
                pedestriansOnNearestCell += nodalCount.PedestriansOnNearestCell;
                pedestriansWithoutCell += nodalCount.PedestriansWithoutCell;
                pedestriansFarthestCell = MathF.Max(pedestriansFarthestCell, nodalCount.FarthestPedestrianCell);
                nodalFarthestCell = MathF.Max(nodalFarthestCell, nodalCount.FarthestCell);
                ladderPairs += nodalCount.LadderPairs;
                laddersAlone += nodalCount.LaddersAlone;
                ladderTopsAsNodes += nodalCount.LadderTopsAsLadderNodes;
                transitionCells.AddRange(nodalCount.Cells.Select(c => $"{c.Navpoint:X8} {c.Cell:X16}"));
                foreach (var (region, onFoot, riding) in ambushHere)
                {
                    if (GuerrillaPopulation.AmbushRegionLine(region) is not { } regionLine) continue;
                    ambushLines.Add(regionLine);
                    ambushRegionsWritten++;
                    foreach (var (handle, squad) in onFoot)
                        if (nodalCount.Handles.TryGetValue(handle, out uint placed))
                        {
                            ambushLines.Add($"node {region.Handle:X8} {placed:X8} {squad}");
                            ambushNodesPlaced++;
                        }
                    foreach (var (handle, squad) in riding)  // seated in their vehicle by the game's squad spawn (squad_vehicle, vehicle_type)
                        if (nodalCount.Handles.TryGetValue(handle, out uint placed))
                        {
                            ambushLines.Add($"node {region.Handle:X8} {placed:X8} {squad} riding");
                            ambushRiding++;
                        }
                }
                guardLines.AddRange(GuerrillaPopulation.GuardPostLines(rfgZone, nodalCount.Handles));
                foreach (var (handle, squad) in standingIn.GetValueOrDefault((col, row)) ?? [])
                    if (edfSquads.Contains(squad) && nodalCount.Handles.TryGetValue(handle, out uint placedEdf) && rfgZone.TryGetValue(handle, out var edfNode) &&
                        edfNode.Value("op") is { Length: >= 12 } edfAt)
                        reinforcementLines.Add(string.Create(System.Globalization.CultureInfo.InvariantCulture,
                            $"node {placedEdf:X8} {BitConverter.ToSingle(edfAt, 0):0.##} {BitConverter.ToSingle(edfAt, 4):0.##} {BitConverter.ToSingle(edfAt, 8):0.##}"));
                var storyCulture = System.Globalization.CultureInfo.InvariantCulture;
                foreach (var node in storyHere)
                    if (nodalCount.Handles.TryGetValue(node.Handle, out uint placedStory) &&
                        storyCharacter.TryGetValue(GuerrillaDistricts.GuerrillaSquadOf(node.Squad), out var character))
                    {
                        storyLines.Add(string.Create(storyCulture, $"{node.Kind} {placedStory:X8} {node.Squad} {character} {node.At.X:R} {node.At.Y:R} {node.At.Z:R} {node.Group:X8} {col} {row}"));
                        storyNodesPlaced++;
                    }
                foreach (var person in people)
                {
                    zoneFile.Insert(zoneFile.Objects.Count, person);
                    if (person.ClassHash == guardBoxClass) guardPostsPlaced++;
                }
                nodalPlaced += nodalCount.Placed;
                nodalOnMovers += nodalCount.OnMovers;
                nodalLeftOut += nodalCount.LeftOut;
                guardAnchorsMoved += nodalCount.GuardAnchors;
            }
            // Guerrilla's roads (traffic's paths) and its parked vehicles' spots (see GuerrillaVehiclePlaces).
            List<ZoneObject> roadsHere = [];
            if (guerrillaObjects.TryGetValue((col, row), out var rfgZoneObjects))
            {
                var taken = new HashSet<uint>(zoneFile.Objects.Select(o => o.Handle));
                var (roadObjects, spots) = GuerrillaVehiclePlaces.Convert(rfgZoneObjects, schema, taken, vehiclePlaceProblems, (corner.X, corner.Y), ZoneWidth,
                                                                          roadsMoved);
                foreach (var placed in roadObjects.Concat(spots)) zoneFile.Insert(zoneFile.Objects.Count, placed);
                roadsHere = roadObjects;
                roadsPlaced += roadObjects.Count;
                spotsPlaced += spots.Count;
                var pickups = GuerrillaPickups.Convert(rfgZoneObjects, schema, taken, pickupItems, pickupWeapons, pickupsLeft);
                foreach (var pickup in pickups) zoneFile.Insert(zoneFile.Objects.Count, pickup);
                pickupsPlaced += pickups.Count;
            }
            buildingLines.AddRange(GuerrillaBuildingList.Lines(zone, zoneFile.Objects, buildingEntries));
            kiosks.Add(zone, zoneFile.Objects);
            byte[] zoneBytes = GuerrillaMovers.WithLinks(zoneFile.ToBytes(), links);
            // The persistent zone: the player start again (as the game's own levels keep one there), and
            // no district or mission (0, as the game's persistent zones); the zone's roads again, as Guerrilla's.
            var persistent = ZoneFile.Create(0, 0);
            PlayerStart(persistent);
            foreach (var road in GuerrillaVehiclePlaces.Copies(roadsHere)) persistent.Insert(persistent.Objects.Count, road);
            zonescript.Add((zone + ".rfgzone_pc", zoneBytes));
            zonescript.Add(("p_" + zone + ".rfgzone_pc", persistent.ToBytes()));

            // The stitch list: Guerrilla's rocks, with a collision tree reaching all of them, each by the
            // box its own collision fills where it is placed; none in a zone without rocks (Guerrilla's
            // scenery zones and some others), whose list is empty, with no tree, which the game's loader
            // takes (Armageddon's own levels put a floor piece in such zones instead).
            var rocks = rocksIn[(col, row)];
            var boxes = rocks.Select(r => StitchList.PlacedBox(rockPieces[r.Name].CollisionMin, rockPieces[r.Name].CollisionMax, r.Rotation, r.Position)).ToList();
            byte[] stitch = StitchList.Write(rocks, zoneMiddle, boxes);

            // Its containers (packed now: see Pack) and asset list entries: the shared files placed for
            // it (above), with its own.
            var (sharedFiles, sharedParts) = sharedIn[(col, row)];
            var files = new List<(string Name, byte[] Data)>(sharedFiles)
            {
                (blendName + ".cvbm_pc", PegFile.SingleHeader(blendName + ".tga", blend.Width, blend.Height, TextureFormat.Argb8888, 1, PegFile.DataFlag)), (blendName + ".gvbm_pc", blendData),
                (colourName + ".cvbm_pc", PegFile.SingleHeader(colourName + ".tga", look.Colour.Width, look.Colour.Height, TextureFormat.Dxt1, colourLevels, 0)), (colourName + ".gvbm_pc", colourData),
                (zone + ".cterrain_pc", terrainFile), (zone + ".gterrain_pc", terrainData),
                (zone + ".navdata_pc", navigation[(col, row)].ToBytes()), (zone + ".stchlst_pc", stitch), (zone + ".rfgzone_pc", zoneBytes),
            };
            Pack($"1h_c{col:00}{15 - row:00}", zoneLayer: true, files);
            for (int n = 0; n < 9; n++)
                Pack($"0h_c{col * 3 + n % 3:00}{(15 - row) * 3 + n / 3:00}", zoneLayer: false,
                     [($"{zone}_{n}.ctmesh_pc", subzones[n].Header), ($"{zone}_{n}.gtmesh_pc", subzones[n].Data), .. sharedParts[n]]);
            log($"{zone}{(isStart ? " (start)" : "")}: ground {h.Min():0} to {h.Max():0} m, {rocks.Count} rocks, {propsIn[(col, row)].Count} destructible objects ({links.Length / ZoneFile.DestructionLinkSize} links), {files.Sum(f => (long)f.Data.Length) / 1024:N0} KB of files in the zone's container, " +
                $"{(OwnCliffs(look) ? $"its {look.Cliffs.Count} cliff materials" : "3 cliff materials from its ground")}, Guerrilla's far meshes ({farLowered} vertices lowered under our ground), " +
                $"{roadSamples:N0} road samples walked on at Guerrilla's own heights");
            // What was made for this zone alone isn't needed again.
            heights.Remove((col, row));
            looks.Remove((col, row));
            navigation.Remove((col, row));
        }
        log($"People: {pedestriansPlaced} of Guerrilla's pedestrian navpoints placed (its pedestrian network, for the ambient people); " +
            $"{guardPostsPlaced} guard posts' boxes with their squads' nodes under them, {guardLines.Count(l => l.StartsWith("guard "))} of their guard nodes " +
            "written to guard_posts.txt (guerrilla_people's guard post AI)");
        log($"People: {nodalPlaced} of Guerrilla's node network's navpoints placed (paths on walls, transitions, stairs, ladders, doors: the ways between " +
            $"the nav mesh's pieces), {nodalOnMovers} of them on the building or other mover they belong to, {nodalLeftOut} left out (their mover isn't " +
            $"placed); {guardAnchorsMoved} guard posts' boxes anchored on a guard node (the leash's way back)");
        log($"People: transitions' cells (navpoint_data version 4, as Armageddon's): {nodalOnOwnCell} on the switched-on cell under them, " +
            $"{nodalOnNearestCell} on the nearest one (as Guerrilla's stream-in does; the farthest {nodalFarthestCell:F1} m), {nodalWithoutCell} with none " +
            $"within 10 m; ladders: {ladderPairs} with their two ends linked, {ladderTopsAsNodes} ladder tops on walls written as ladder nodes, " +
            $"{laddersAlone} ladder navpoints linked to no other ladder or ladder transition (the game reports those on a repair)");
        log($"People: pedestrians' cells: {pedestriansOnNearestCell} with no switched-on cell over or under them (on buildings' floors and walkways) given the " +
            $"nearest (navpoint_data version 4; the farthest {pedestriansFarthestCell:F1} m), {pedestriansWithoutCell} with none within 10 m (the game reports those)");
        // The same cells by navpoint, for guerrilla_people to give them where the game's reading came before their zone's mesh.
        File.WriteAllLines(Path.Combine(modFolder, "transition_cells.txt"), transitionCells);
        File.WriteAllLines(Path.Combine(modFolder, "ambush_regions.txt"), ambushLines);
        File.WriteAllLines(Path.Combine(modFolder, "story_nodes.txt"), storyLines);
        File.WriteAllLines(Path.Combine(modFolder, "guard_posts.txt"), guardLines);
        File.WriteAllLines(Path.Combine(modFolder, "reinforcement_nodes.txt"), reinforcementLines);
        log($"People: {reinforcementLines.Count(l => l.StartsWith("node "))} of the EDF's squad nodes placed written to reinforcement_nodes.txt (places for its reinforcements on foot)");
        log($"People: {storyNodesPlaced} of Guerrilla's story people's squad nodes at the safehouses placed and written to story_nodes.txt " +
            $"({storyLines.Count(l => l.StartsWith("special "))} special NPCs', {storyLines.Count(l => l.StartsWith("vip "))} VIPs'; held for guerrilla_people's story.cpp)");
        log($"People: {ambushRegionsWritten} of Guerrilla's Marauder ambush regions written to ambush_regions.txt, {ambushNodesPlaced} of their squad nodes on foot " +
            $"placed (held until their region fires: guerrilla_people's ambush.cpp), and {ambushRiding} riding ones (seated in their vehicles)");
        log($"People: {transitionCells.Count} transitions' and pedestrians' cells written to transition_cells.txt (guerrilla_people gives each its cell once its zone's mesh is in)");
        log($"Vehicles' places: {roadsPlaced} of Guerrilla's roads (each in its zone and its persistent zone), {spotsPlaced} parked vehicles' spots " +
            $"(their groups {GuerrillaVehiclePlaces.GroupPrefix}<Guerrilla's name>, which building the vehicles writes)" +
            (roadsMoved.Count > 0 ? $"; {roadsMoved.Count} roads' own positions moved into their zone (their points as they are): {string.Join(", ", roadsMoved)}" : "") +
            (vehiclePlaceProblems.Count > 0 ? $"; left out: {string.Join("; ", vehiclePlaceProblems)}" : ""));
        log($"Pickups: {pickupsPlaced} of Guerrilla's placed (ammo boxes, weapon lockers, propane tanks, singularity bombs: the weapons build " +
            "makes their items and weapons)" + (pickupsLeft.Count > 0 ? $"; not yet: {string.Join(", ", pickupsLeft.Select(p => $"{p.Key} {p.Value}"))}" : ""));
        log($"Ladders: {laddersPlaced} of Guerrilla's placed, each on its building (the most in one zone {laddersInZone}; the game holds " +
            $"{GuerrillaLadders.MostLadders} at once)" + (laddersLeftOut > 0 ? $"; {laddersLeftOut} left out, their building not placed" : ""));
        log($"Placed effects: {placedEffects} of Guerrilla's placed (its object_effects: the EDF's force fields, the Mohole's smoke and ash)" +
            (placedEffectsLeftOut > 0 ? $"; {placedEffectsLeftOut} left out, their effect not in the world's tables" : ""));
        log($"Shape cutters: {cuttersPlaced} of Guerrilla's placed (its pre-made damage, cut as each zone streams in), each on the building it cuts " +
            $"(the most in one zone {cuttersInZone}; the game holds {GuerrillaShapeCutters.MostShapeCutters} at once)" +
            (cuttersLeftOut > 0 ? $"; {cuttersLeftOut} left out, their building not placed" : ""));
        log($"Hinges: {hingesPlaced} of Guerrilla's placed on the objects they join, {hingesWithMotors} turned by a motor (the wind turbines' blades, the " +
            $"mining machine's)" + (hingesLeftOut > 0 ? $"; {hingesLeftOut} left out, an object they join not placed" : ""));
        File.WriteAllLines(Path.Combine(modFolder, GuerrillaConstraints.TurbineList), turbineLines);
        File.WriteAllLines(Path.Combine(modFolder, GuerrillaBuildingList.FileName), buildingLines);
        log($"Buildings: {buildingLines.Count} movers written to {GuerrillaBuildingList.FileName} (chunk, importance, team, destroyed share and place, " +
            $"for Guerrilla's minimap); {buildingLines.Count(l => l.Split(' ')[4] is "2" or "3")} of medium or high importance");
        foreach (string line in kiosks.Write(modFolder, rfgCache, game, overrides)) log("Kiosks: " + line);
        foreach (string line in GuerrillaVoiceBanks.Write(rfgTables, rfgCache, game, overrides, modFolder)) log("Voices: " + line);
        log($"Wind turbines: {turbineLines.Count} written to {GuerrillaConstraints.TurbineList}, their blades each turned to its own angle " +
            $"(guerrilla_world plays Guerrilla's turbine code on them)" +
            (turbineLines.Count(l => l.Split(' ')[3] == "FFFFFFFF") is int noMotor and > 0 ? $"; {noMotor} without their motor placed" : ""));
        log($"Precache: {precacheFiles.Count} files ({precacheFiles.Values.Sum(d => (long)d.Length) / 1048576:N0} MB) held by more than one container, kept once (its directory " +
            $"{directory / 1024:N0} KB of the {PrecacheDirectoryBudget / 1024:N0} KB it may take; {left} more such files stay in their containers); " +
            $"the containers' files {before / 1048576:N0} MB → {after / 1048576:N0} MB");
        foreach (var (name, piece) in rockPieces)
        {
            var pieceFiles = new List<(string Name, byte[] Data)> { (name + ".cstch_pc", piece.Header), (name + ".gstch_pc", piece.Data) };
            foreach (string texture in piece.Textures) pieceFiles.AddRange(rockTextures[texture]);
            var group = new AsmContainer { Name = name + ".rfgstitchx", Type = 14, Flags = 0x300 };
            group.Primitives.AddRange(pieceFiles.Where(f => f.Name.EndsWith(".cstch_pc") || f.Name.EndsWith(".cvbm_pc")).Select(f => PrimitiveOf(f.Name, pieceFiles)));
            asm.Containers.Add(group);
        }
        foreach (var (name, chunk) in chunkFiles)
        {
            var chunkGroupFiles = new List<(string Name, byte[] Data)> { (name + ".cchk_pc", chunk.Header), (name + ".gchk_pc", chunk.Data) };
            foreach (string texture in chunk.Textures) chunkGroupFiles.AddRange(chunkTextures[texture]);
            chunkGroupFiles = chunkGroupFiles.DistinctBy(f => f.Name, StringComparer.OrdinalIgnoreCase).ToList();
            var group = new AsmContainer { Name = name + ".rfgchunkx", Type = 14, Flags = 0x300 };
            group.Primitives.AddRange(chunkGroupFiles.Where(f => f.Name.EndsWith(".cchk_pc") || f.Name.EndsWith(".cvbm_pc")).Select(f => PrimitiveOf(f.Name, chunkGroupFiles)));
            asm.Containers.Add(group);
        }
        // Each effect file a group of its own named after it (type 2, as Armageddon's effects): the game
        // makes an effect object only when the streaming system knows a group by the visual's file name
        // (rfg_<name>.cefct_pc), which the object then holds while it plays; its files are the ones the
        // zones and the precache hold. Listed where Armageddon lists its own (below, effects.vpp_pc's
        // asset list, read once and kept), not in the world's: the world's list is read again at every
        // level load, and with effect groups in it a second load broke the game's container records.
        var effectGroups = new List<AsmContainer>();
        foreach (var files in effectFiles.Values)
        {
            var groupFiles = files.DistinctBy(f => f.Name, StringComparer.OrdinalIgnoreCase).ToList();
            var group = new AsmContainer { Name = groupFiles[0].Name, Type = 2, Flags = 0x300 };
            group.Primitives.AddRange(groupFiles.Where(f => f.Name.EndsWith(".cefct_pc") || f.Name.EndsWith(".cvbm_pc")).Select(f => PrimitiveOf(f.Name, groupFiles)));
            foreach (var primitive in group.Primitives) primitive.Allocator = 0;  // as Armageddon's effect groups
            effectGroups.Add(group);
        }
        layer.Add((World + "_l0.asm_pc", asm.ToBytes()));
        layer1.Add((World + "_l1.asm_pc", asm1.ToBytes()));

        string output = Path.Combine(overrides, "build", "pc", "cache");
        Directory.CreateDirectory(output);
        // Guerrilla's material shaders rewritten for Armageddon's renderer, which the chunks' and rocks'
        // materials name (the guerrilla_world mod has the game load them).
        var shaderFiles = GuerrillaShaders.Write(cache, output);
        log($"Shaders: {shaderFiles.Count} effect files of Guerrilla's material shaders; {GuerrillaMaterials.GuerrillaShaderMaterials:N0} materials drawn with them");
        // Written straight into their files: with Guerrilla's buildings the layers pass 2 GB. The zone
        // files are stored compressed, as the game's own zonescript_world.vpp_pc is: the game reads the
        // archive's stored bytes through a 20 MB pool, and with Guerrilla's objects they come to 26 MB.
        Packfile.Write(Path.Combine(output, World + "_l0.vpp_pc"), layer);
        Packfile.Write(Path.Combine(output, World + "_precache.vpp_pc"), precacheFiles.Select(f => (f.Key, f.Value)).ToList());
        Packfile.Write(Path.Combine(output, World + "_l1.vpp_pc"), layer1);
        Packfile.Write(Path.Combine(output, "zonescript_" + World + ".vpp_pc"), zonescript, compressed: true);
        // Guerrilla's activities' managed layers (the demolitions masters' barrels, propane tanks, ammo boxes, walkers and
        // turret) as Armageddon's: see GuerrillaActivityLayers.
        foreach (string line in GuerrillaActivityLayers.Write(activityLayers, game, overrides, modFolder, schema, PropsOf, GameplayProps, LodProps,
                     chunk => chunkFiles.TryGetValue(chunk, out var built) ? (uint?)BitConverter.ToUInt32(built.Header, 0x24) : null, pickupItems, pickupWeapons))
            log("Activity layers: " + line);
        // Guerrilla's districts (the territory, an override of Armageddon's table) and a time of day for each.
        var rfgMisc = Packfile.Open(Path.Combine(rfgCache, "misc.vpp_pc"));
        var (territories, districts) = GuerrillaTerritory.Build(Packfile.Open(Path.Combine(cache, "table.vpp_pc")), rfgMisc, World);
        string tables = Path.Combine(output, "table.vpp_pc");
        Directory.CreateDirectory(tables);
        File.WriteAllBytes(Path.Combine(tables, "territories.xtbl"), territories);
        var (orbitals, sets) = GuerrillaTimeOfDay.Build(rfgMisc, districts);
        File.WriteAllBytes(Path.Combine(output, World + ".gtodx"), orbitals);
        foreach (var (name, set) in sets) File.WriteAllBytes(Path.Combine(output, $"{World}_{name.Replace(' ', '_').ToLowerInvariant()}.dtodx"), set);
        log($"Districts: {string.Join(", ", districts.Select(d => $"{d.Guerrilla} ({d.Name})"))}, each with Guerrilla's time of day ({World}_<name>.dtodx), " +
            $"in the territory {World} (table.vpp_pc\\territories.xtbl)");
        // Room for the world's textures in the game's texture name table (see GuerrillaBitmaps): a slot
        // for every texture file the world has (textureFiles, from its containers), and some to spare
        // (the HUD's, other mods').
        int extraSlots = (textureFiles.Count + SpareTextureSlots + 499) / 500 * 500;
        var gameMisc = Packfile.Open(Path.Combine(cache, "misc.vpp_pc"));
        byte[] bitmaps = gameMisc.Read(gameMisc.Find("bitmaps_pc") ?? throw new InvalidDataException("no bitmaps_pc in misc.vpp_pc"));
        string miscFolder = Path.Combine(output, "misc.vpp_pc");
        Directory.CreateDirectory(miscFolder);
        File.WriteAllBytes(Path.Combine(miscFolder, "bitmaps_pc"), GuerrillaBitmaps.WithRoom(bitmaps, extraSlots));
        // The effects' groups (above), added to Armageddon's effects asset list, as an override of it.
        var gameEffects = Packfile.Open(Path.Combine(cache, "effects.vpp_pc"));
        var effectsList = AsmFile.Read(gameEffects.Read(gameEffects.Find("vfx_containers.asm_pc") ?? throw new InvalidDataException("no vfx_containers.asm_pc in effects.vpp_pc")));
        effectsList.Containers.AddRange(effectGroups);
        string effectsFolder = Path.Combine(output, "effects.vpp_pc");
        Directory.CreateDirectory(effectsFolder);
        File.WriteAllBytes(Path.Combine(effectsFolder, "vfx_containers.asm_pc"), effectsList.ToBytes());
        log($"Effects: {effectGroups.Count} effect groups added to Armageddon's effects asset list ({effectsList.Containers.Count} groups; effects.vpp_pc\\vfx_containers.asm_pc)");
        log($"Textures: {textureFiles.Count:N0} texture files; the game's texture name table gets {extraSlots:N0} more slots " +
            $"({GuerrillaBitmaps.Capacity(bitmaps):N0} → {GuerrillaBitmaps.Capacity(bitmaps) + extraSlots:N0}; misc.vpp_pc\\bitmaps_pc)");
        log($"World {World}: {zones.Count} zones, start {World}_{start.Col:00}_{start.Row:00}; written into {output}");
        GuerrillaSky.Write(cache, rfgCache, modFolder, log);
        GuerrillaSky.WriteStars(guerrilla, modFolder, log);
        GuerrillaSky.WriteCorona(rfgCache, modFolder, log);
        // Last, once everything is written: which format this world is, for the launcher to check.
        File.WriteAllText(Path.Combine(modFolder, StampFile),
                          $"RFA-Mod Guerrilla world, format {Format}\r\nBuilt {DateTime.Now:yyyy-MM-dd HH:mm}, {zones.Count} zones\r\n");
    }

    /// <summary>
    /// Deep shafts and sheer walls, over all zones' samples together (so shared edges stay equal):
    /// Guerrilla's ground has round openings (where it puts other pieces) whose walls go down hundreds
    /// of metres. No point is left more than MaxPit below where its hollow would spill (a priority
    /// flood from the edge of the zones built), then none more than MaxStep below a neighbouring
    /// sample (the low side raised): seen from inside or below, the shafts' walls look like flat
    /// sheets hanging in the sky. MaxStep is kept above Guerrilla's own vertical faces (cliffs and
    /// the retaining walls beside its roads, up to about 24 m a metre): a lower one pushes ramps of
    /// ground out from them, into the buildings and onto the roads at their feet.
    ///
    /// But a hollow Guerrilla put things in, below where the cap would put the ground (<paramref name="placedAt"/>:
    /// every object of its zones), is its own pit, meant to be seen and gone into, and keeps Guerrilla's ground: the
    /// Mohole in the Badlands (193 m deep, its pipes, towers, buildings and effects going down into it) and the deep
    /// spots of its basin with buildings in them, and the pits Guerrilla's kill regions (trigger_region) wait at the
    /// bottom of. The 43 others (empty shafts up to 477 m deep) are capped as before.
    /// </summary>
    static void LimitGround(IReadOnlyList<(int Col, int Row)> zones, Dictionary<(int, int), float[]> heights, IEnumerable<Vector3> placedAt,
                            Action<string> log)
    {
        int S = Layout.Samples, last = S - 1;
        int colMin = zones.Min(z => z.Col), colMax = zones.Max(z => z.Col), rowMin = zones.Min(z => z.Row), rowMax = zones.Max(z => z.Row);
        int acrossX = (colMax - colMin + 1) * last + 1, acrossZ = (rowMax - rowMin + 1) * last + 1;
        var ground = new float[acrossX * acrossZ];
        var present = new bool[ground.Length];
        foreach (var (col, row) in zones)
        {
            int i = col - colMin, j = rowMax - row;  // j along z (a higher row is further south)
            for (int z = 0; z <= last; z++)
                for (int x = 0; x <= last; x++)
                {
                    int k = (j * last + z) * acrossX + i * last + x;
                    ground[k] = heights[(col, row)][z * S + x];
                    present[k] = true;
                }
        }
        var filled = (float[])ground.Clone();
        var done = new bool[ground.Length];
        var queue = new PriorityQueue<int, float>();
        for (int k = 0; k < ground.Length; k++)
        {
            if (!present[k]) continue;
            int cx = k % acrossX, cz = k / acrossX;
            if (cx == 0 || cz == 0 || cx == acrossX - 1 || cz == acrossZ - 1 || !present[k - 1] || !present[k + 1] || !present[k - acrossX] || !present[k + acrossX])
            {
                done[k] = true;
                queue.Enqueue(k, filled[k]);
            }
        }
        while (queue.TryDequeue(out int cell, out float level))
        {
            int cx = cell % acrossX, cz = cell / acrossX;
            foreach (var (dx, dz) in new[] { (1, 0), (-1, 0), (0, 1), (0, -1) })
            {
                int nx = cx + dx, nz = cz + dz;
                if (nx < 0 || nz < 0 || nx >= acrossX || nz >= acrossZ) continue;
                int next = nz * acrossX + nx;
                if (done[next] || !present[next]) continue;
                done[next] = true;
                filled[next] = Math.Max(ground[next], level);
                queue.Enqueue(next, filled[next]);
            }
        }
        // The hollows deeper than the cap, each a connected set of samples, and those Guerrilla put something in below
        // the cap.
        bool Deep(int k) => present[k] && ground[k] < filled[k] - MaxPit;
        var hollowOf = new Dictionary<int, int>();
        int hollows = 0;
        var stack = new Stack<int>();
        for (int k = 0; k < ground.Length; k++)
        {
            if (!Deep(k) || hollowOf.ContainsKey(k)) continue;
            hollowOf[k] = ++hollows;
            stack.Push(k);
            while (stack.TryPop(out int cell))
            {
                int cx = cell % acrossX, cz = cell / acrossX;
                foreach (var (dx, dz) in new[] { (1, 0), (-1, 0), (0, 1), (0, -1) })
                {
                    int nx = cx + dx, nz = cz + dz;
                    if (nx < 0 || nz < 0 || nx >= acrossX || nz >= acrossZ) continue;
                    int next = nz * acrossX + nx;
                    if (!Deep(next) || hollowOf.ContainsKey(next)) continue;
                    hollowOf[next] = hollows;
                    stack.Push(next);
                }
            }
        }
        var open = new HashSet<int>();
        float originX = (colMin - 8) * ZoneWidth, originZ = (7 - rowMax) * ZoneWidth;  // the world place of sample (0, 0)
        foreach (var at in placedAt)
        {
            float spacing = Layout.StepsPerSample * Layout.GridStep;  // metres from one sample to the next
            int cx = (int)MathF.Round((at.X - originX) / spacing), cz = (int)MathF.Round((at.Z - originZ) / spacing);
            if (cx < 0 || cz < 0 || cx >= acrossX || cz >= acrossZ) continue;
            int k = cz * acrossX + cx;
            if (hollowOf.TryGetValue(k, out int hollow) && at.Y < filled[k] - MaxPit) open.Add(hollow);
        }
        int plugged = 0, raised = 0, kept = 0;
        for (int k = 0; k < ground.Length; k++)
        {
            if (!Deep(k)) continue;
            if (open.Contains(hollowOf[k])) { kept++; continue; }
            ground[k] = filled[k] - MaxPit;
            plugged++;
        }
        for (bool changed = true; changed;)
        {
            changed = false;
            for (int pass = 0; pass < 2; pass++)
                for (int n = 0; n < ground.Length; n++)
                {
                    int k = pass == 0 ? n : ground.Length - 1 - n;
                    if (!present[k]) continue;
                    int cx = k % acrossX, cz = k / acrossX;
                    float need = ground[k];
                    if (cx > 0 && present[k - 1]) need = Math.Max(need, ground[k - 1] - MaxStep);
                    if (cx < acrossX - 1 && present[k + 1]) need = Math.Max(need, ground[k + 1] - MaxStep);
                    if (cz > 0 && present[k - acrossX]) need = Math.Max(need, ground[k - acrossX] - MaxStep);
                    if (cz < acrossZ - 1 && present[k + acrossX]) need = Math.Max(need, ground[k + acrossX] - MaxStep);
                    if (need <= ground[k] + 0.001f) continue;
                    ground[k] = need;
                    raised++;
                    changed = true;
                }
        }
        foreach (var (col, row) in zones)
        {
            int i = col - colMin, j = rowMax - row;
            for (int z = 0; z <= last; z++)
                for (int x = 0; x <= last; x++)
                    heights[(col, row)][z * S + x] = ground[(j * last + z) * acrossX + i * last + x];
        }
        log($"Ground: {hollows - open.Count} hollows deeper than {MaxPit} m below their spill capped there ({plugged:N0} samples raised), {open.Count} left " +
            $"open as Guerrilla's own pits with its objects in them ({kept:N0} samples); {raised:N0} raised so no drop is over {MaxStep} m a sample");
    }

    /// <summary>A Guerrilla texture's name in our world (its name's hash, so every alias is as short and the names in our files never grow).</summary>
    static string Alias(string texture) => "rfg_" + NameHashes.PropertyHash(texture).ToString("x8");

    /// <summary>
    /// A zone's terrain file, written from its parts (see <see cref="TerrainFile"/>): terrain_data field by field;
    /// every material written from code (<see cref="TerrainMaterials"/>): the ground's with Guerrilla's
    /// layer textures, scales, offsets and tints, Guerrilla's own cliff materials (textures,
    /// projections, tints; a zone with none: three made from the ground's layers 0 and 3), the far
    /// meshes' with the zone's colour map; Guerrilla's barriers and layer map; no foliage (Guerrilla's is
    /// still to port); the texture list of what the materials name; the height field's Havok packfile
    /// (<see cref="TerrainHeightField.Section"/>) from the zone's heights, and Guerrilla's own far
    /// meshes and their buffer (from <paramref name="guerrillaTerrain"/> and <paramref name="guerrillaData"/>,
    /// its terrain file and .gterrain_pc; see <see cref="FarGround.FromGuerrilla"/>), kept under the
    /// heights. Nothing of another level's terrain file is used. Returns the terrain file, its
    /// .gterrain_pc and how many far vertices were lowered.
    /// </summary>
    static (byte[] Terrain, byte[] Data, int FarLowered, int RoadSamples) ZoneTerrain(float[] heights, Vector2 corner, Look look, IReadOnlyList<ChunkShape> barriers,
                                                                     string zone, string blendName, string colourName, byte[] guerrillaTerrain, byte[] guerrillaData,
                                                                     float[]? roadSurface)
    {
        var t = new TerrainFile { HasGround = true };
        string Tga(string name) => name + ".tga";

        // The ground's collision: a height field of 512 × 512 samples a metre apart (the zone's 511 m),
        // as Guerrilla's own: each height a 16-bit sample of 1/64 m (Guerrilla's step, which keeps its
        // heights exactly; from -512 m, so -512 to +512 m, the world spans about -120 to 150 m), and
        // each square cut from (x + 1, z) to (x, z + 1) as Guerrilla's collision cuts them (its field
        // doesn't flip) and our drawn ground cuts them away from the roads.
        const int FieldSamples = 512;
        const float SampleOffset = -512, SampleScale = 1 / 64f;
        float[] field = GuerrillaGround.FieldHeights(heights, FieldSamples, Layout);
        // Under Guerrilla's road decals, its own collision's heights (its height field, on the same grid):
        // our samples take the decals' surface there, which lies a few cm off what Guerrilla's cars drive
        // on (up to 28 cm). A zone without a height field (the scenery around the map) keeps the decals'.
        int roadSamples = 0;
        if (roadSurface?.Length == field.Length && GuerrillaTerrain.HeightField(guerrillaTerrain) is { Samples: FieldSamples, Step: 1 } own)
            for (int i = 0; i < field.Length; i++)
                if (!float.IsNaN(roadSurface[i]))
                {
                    field[i] = own.Heights[i];
                    roadSamples++;
                }
        var samples = field.Select(h => (ushort)Math.Clamp(MathF.Round((h - SampleOffset) / SampleScale), 0, ushort.MaxValue)).ToArray();
        float fieldStep = ZoneWidth / (FieldSamples - 1);
        t.HeightField = TerrainHeightField.Section(FieldSamples, FieldSamples, fieldStep, 1 / fieldStep, ZoneWidth, ZoneWidth, SampleOffset, SampleScale, samples,
                                                   flipped: false);
        float low = samples.Min() * SampleScale + SampleOffset, high = samples.Max() * SampleScale + SampleOffset;
        t.SubzoneSlots = new byte[9 * 4];

        // The far-away meshes: Guerrilla's own (its terrain file's and .gterrain_pc's), kept under the heights as stored.
        var (farRecords, farBlocks, farData, farLowered) = FarGround.FromGuerrilla(guerrillaTerrain, guerrillaData,
            samples.Select(s => s * SampleScale + SampleOffset).ToArray(), FieldSamples, fieldStep, corner);
        t.FarRecords = farRecords;
        t.FarBlocks.AddRange(farBlocks);

        // The ground: Guerrilla's four layers, their scales, offsets and tints, and its last two constants.
        var (groundNames, groundMaterials) = TerrainMaterials.Ground(look.Textures.Take(8).Select(n => Tga(Alias(n))).ToList(), Tga(blendName), Tga(colourName),
            [look.Constants[0], look.Constants[2], look.Constants[4], look.Constants[6]], [look.Constants[1], look.Constants[3], look.Constants[5], look.Constants[7]],
            look.Constants[8], look.Constants[9]);

        // The cliffs: Guerrilla's own cliff materials, each as it is (its one or two textures, their side
        // projections and tints), in its order, as many as it has (up to six): its sub-zones' cliff blocks
        // name them by number (render block material 3 + n), and ours keep its numbers. A zone with none
        // usable gets the game's three kinds made from the ground's layers 0 and 3 (texture A alone, A
        // blended into B, B alone), as the game's terrain repeats them, and their tints.
        var specular = new Vector4(1, 1, 1, 1);
        uint xform0 = NameHashes.PropertyHash("Terrain_layer0_xform"), xform1 = NameHashes.PropertyHash("Terrain_layer1_xform");
        uint xform2 = NameHashes.PropertyHash("Terrain_layer2_xform"), xform3 = NameHashes.PropertyHash("Terrain_layer3_xform");
        uint tint0 = NameHashes.PropertyHash("Diffuse_color_0"), tint1 = NameHashes.PropertyHash("Diffuse_color_1");
        ((string Colour, string Normal) Pair, Vector4[][] Xforms, Vector4 Tint, Vector4 Specular) Side(int material, bool last)
        {
            var g = look.Cliffs[material];
            var pairs = look.CliffPairs(material);
            var pair = last ? pairs[^1] : pairs[0];
            bool second = last && pairs.Count > 1;
            Vector4[]? Rows(uint hash) => g.Constants.TryGetValue(hash, out var rows) ? rows : null;
            var xforms = second && Rows(xform2) is { } a && Rows(xform3) is { } b ? new[] { a, b } : new[] { Rows(xform0)!, Rows(xform1)! };
            var tint = (second ? Rows(tint1) : null)?[0] ?? Rows(tint0)?[0] ?? Vector4.One;
            return (pair, xforms, tint, Rows(0x984BB1F3)?[0] ?? specular);  // Guerrilla's 984BB1F3 holds what Specular_Power does
        }
        var withPairs = Enumerable.Range(0, look.Cliffs.Count).Where(k => look.CliffPairs(k).Count > 0 && look.Cliffs[k].Constants.ContainsKey(xform0)).Take(3).ToList();
        ((string Colour, string Normal) Pair, Vector4[][] Xforms, Vector4 Tint, Vector4 Specular) sideA = withPairs.Count > 0 ? Side(withPairs[0], last: false)
            : ((look.Textures[0], look.Textures[1]), TerrainMaterials.SideProjections(MathF.Abs(look.Constants[0].X) / ZoneWidth, ZoneWidth / 2), look.Constants[1], specular);
        ((string Colour, string Normal) Pair, Vector4[][] Xforms, Vector4 Tint, Vector4 Specular) sideB = withPairs.Count > 0 ? Side(withPairs[^1], last: true)
            : ((look.Textures[6], look.Textures[7]), TerrainMaterials.SideProjections(MathF.Abs(look.Constants[6].X) / ZoneWidth, ZoneWidth / 2), look.Constants[7], specular);
        (string, string) Named((string Colour, string Normal) pair) => (Tga(Alias(pair.Colour)), Tga(Alias(pair.Normal)));
        string cliffDither = Tga(Alias(GuerrillaBuiltIns.CliffDither));  // (see there: the ring's normals smooth)
        var cliffs = new List<(byte[] Names, byte[] Material)>();
        if (OwnCliffs(look))
            for (int k = 0; k < look.Cliffs.Count; k++)
            {
                // Its first texture by xforms 0 and 1 and Diffuse_color_0; a second by xforms 2 and 3 and Diffuse_color_1.
                var first = Side(k, last: false);
                if (look.CliffPairs(k).Count > 1)
                {
                    var second = Side(k, last: true);
                    cliffs.Add(TerrainMaterials.Cliff(Named(first.Pair), Named(second.Pair), Tga(blendName), Tga(colourName),
                                                      [.. first.Xforms, .. second.Xforms], [first.Tint, second.Tint], first.Specular, cliffDither));
                }
                else
                    cliffs.Add(TerrainMaterials.Cliff(Named(first.Pair), null, Tga(blendName), Tga(colourName), first.Xforms, [first.Tint], first.Specular, cliffDither));
            }
        else
        {
            cliffs.Add(TerrainMaterials.Cliff(Named(sideA.Pair), null, Tga(blendName), Tga(colourName), sideA.Xforms, [sideA.Tint], sideA.Specular, cliffDither));
            cliffs.Add(TerrainMaterials.Cliff(Named(sideA.Pair), Named(sideB.Pair), Tga(blendName), Tga(colourName), [.. sideA.Xforms, .. sideB.Xforms], [sideA.Tint, sideB.Tint], sideA.Specular, cliffDither));
            cliffs.Add(TerrainMaterials.Cliff(Named(sideB.Pair), null, Tga(blendName), Tga(colourName), sideB.Xforms, [sideB.Tint], sideB.Specular, cliffDither));
        }
        t.Cliffs.Clear();
        t.Cliffs.AddRange(cliffs);
        t.CliffHeader = new byte[8 + cliffs.Count * 8];
        t.Ground = new TerrainFile.MaterialSet(groundNames, new int[cliffs.Count], TerrainFile.MaterialMap(groundMaterials.Count), new byte[groundMaterials.Count * 4], groundMaterials);

        // The far meshes: the zone's colour map.
        var (farNames, farMaterial) = TerrainMaterials.Far(Tga(colourName));
        t.FarMaterial = new TerrainFile.MaterialSet(farNames, null, TerrainFile.MaterialMap(1), [], [farMaterial]);

        // No foliage; Guerrilla's barriers and layer map; nothing in the run-time slots.
        t.FoliageMeshNames = [];
        t.FoliageMeshCount = 0;
        t.Foliage = null;
        t.FoliageBuffer = null;
        (t.BarrierRecords, t.BarrierSection) = TerrainBarriers.Parts(barriers);
        t.StitchCollisionCount = 0;
        t.StitchCollision = TerrainFile.EmptySection;
        var (cells, guerrillaNames) = look.LayerMap ?? (new byte[512 * 512 / 4], new List<string> { "Terrain" });
        if (cells.Length != 512 * 512 / 4) throw new InvalidDataException($"{zone}'s layer map is {cells.Length} bytes, not 512 × 512 cells of 2 bits");
        // Guerrilla's names, then (so the far meshes after them start at a multiple of 16) effect materials
        // Armageddon's rfg_effect_materials.xtbl has, which no cell uses.
        // Guerrilla's undergrowth (its grass and dust rocks), as its file keeps it: Armageddon's loader
        // and scatter still take it (the mod puts the scattered pieces on the ground).
        var undergrowth = GuerrillaTerrain.Undergrowth(guerrillaTerrain);
        t.Undergrowth = undergrowth?.Section;
        var effectNames = TerrainFile.LayerMapPart.PaddedNames(guerrillaNames,
            ["Terrain", "Dirt", "Ice", "Steel", "Rubber", "Concrete", "Composite", "Unobtanium", "Electronics"], t.Undergrowth?.Length ?? 0);
        t.LayerMap = new TerrainFile.LayerMapPart(cells, new byte[effectNames.Count * 4], effectNames, new byte[effectNames.Count * 4]);
        t.Occluders = GuerrillaTerrain.Occluders(guerrillaTerrain);

        // terrain_data, field by field (the box's heights the height field's).
        var data = new byte[TerrainFile.DataSize];
        void Word(int at, int value) => BitConverter.TryWriteBytes(data.AsSpan(at), value);
        void Float(int at, float value) => BitConverter.TryWriteBytes(data.AsSpan(at), value);
        Word(0x00, 1);
        Float(0x04, corner.X); Float(0x08, low); Float(0x0C, corner.Y);
        Float(0x10, corner.X + ZoneWidth); Float(0x14, high); Float(0x18, corner.Y + ZoneWidth);
        Word(0x1C, FieldSamples); Word(0x20, FieldSamples);
        Word(0x24, t.Occluders.Length / 24);            // occluders (Guerrilla's)
        Word(0x4C, t.SubzoneSlots.Length / 4);          // sub-zones
        Word(0x58, 512); Word(0x5C, 512); Word(0x60, 2); Word(0x64, cells.Length); Word(0x6C, effectNames.Count);
        Word(0x78, undergrowth?.Layers ?? 0); Word(0x84, undergrowth?.Records ?? 0);  // Guerrilla's undergrowth
        Word(0x94, barriers.Count);
        Word(0x9C, 0);                                  // a height field
        Word(0xA0, cliffs.Count);
        t.Data = data;

        // The header: the textures the materials name (the dither texture is the game's own), and an id of our own.
        var listed = new[] { groundNames }.Concat(cliffs.Select(c => c.Names))
            .SelectMany(b => System.Text.Encoding.ASCII.GetString(b, 4, b.Length - 4).Split('\0', StringSplitOptions.RemoveEmptyEntries))
            .Where(n => !n.Equals("dither.tga", StringComparison.OrdinalIgnoreCase)).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        t.TextureNames = TerrainFile.NameList(listed).Bytes;
        t.TextureNameCount = listed.Count;
        t.Id = (NameHashes.PropertyHash(zone) & 0xFFFF0000) | 0x20;
        return (t.Write(), farData, farLowered, roadSamples);

    }

    /// <summary>
    /// A zone's environment box, made from the game's schema (object_env_settings): every setting at the
    /// schema's default, which affects nothing until its affects_ flag is set, then what the world needs
    /// (the game's own world zones set these groups in their boxes too):
    ///   the box: the whole zone, every height (BoxBottom to BoxTop, half a metre past its edges)
    ///   visibility: the far clip 475 m (Guerrilla's camera's, also the schema's default: zones and buildings
    ///     load out of sight; the guerrilla_world mod draws farther with its FarClip=) and the rocks' detail
    ///     kept to 200 m (the default 125)
    ///   fog: on, starting far past the far clip (Armageddon's fog is the box's, by distance; the
    ///     guerrilla_world mod draws Guerrilla's height fog instead)
    ///   HDR: on, bloom 0.2 over a height of 1 (the defaults 0.5 and 3): the bloom the world's picture was
    ///     matched to Guerrilla's with (Exposure=1, 2026-11-01); its exposure the defaults, which
    ///     Guerrilla's tone mapping (the mod) holds at 1 anyway
    ///   the sky: the skybox on, in which pass the mod draws Guerrilla's sky; no camera effect
    /// Colour, particles, wind, sound and depth of field stay off: Guerrilla's colour correction, district
    /// tints and the rest are the mod's and the time of day's.
    /// </summary>
    static void ZoneEnvironment(ZoneFile zone, ZoneSchema schema, uint handle, Vector3 middle)
    {
        float half = ZoneWidth / 2 + 0.5f;
        var box = zone.AddNew(NameHashes.ClassHash("object_env_settings"), handle, schema.Class("object_env_settings")!.NewObject(middle),
                              middle + new Vector3(-half, BoxBottom, -half), middle + new Vector3(half, BoxTop, half));
        void Number(string name, float value) => box.Property(name)!.SetValue(BitConverter.GetBytes(value));
        void Flag(string name, bool on) => box.Property(name)!.SetValue([on ? (byte)1 : (byte)0]);
        box.Property("bb")!.SetValue([.. new[] { -half, BoxBottom, -half, half, BoxTop, half }.SelectMany(BitConverter.GetBytes)]);
        Flag("affects_visibility", true);
        Number("far_clip", 475);
        Number("stitch_lod_distance", 200);
        Flag("affects_fog", true);
        Number("fog_near", BoxFogNear);
        Number("fog_far", BoxFogNear + 1);
        Flag("affects_hdr", true);
        Number("bloom_amount", 0.2f);
        Number("bloom_height", 1);
        Flag("skybox_enable", true);
    }

    /// <summary>The first choice for an enum the schema gives no default (the zone marker's district and spawn resources: "Default", which the game's tables have).</summary>
    static IReadOnlyList<string> DefaultChoice(SchemaPopulate table) => ["Default"];

    /// <summary>An asset list entry for a container of ours: its data block's place and size, each file's size, and a primitive for each header file.</summary>
    static AsmContainer ContainerEntry(string name, byte[] containerBytes, IReadOnlyList<(string Name, byte[] Data)> files)
    {
        var c = Packfile.FromBytes(name, containerBytes);
        var entry = new AsmContainer { Name = name, Type = 17, Flags = 0x480, DataOffset = (uint)c.DataStart, CompressedSize = c.CompressedDataSize };
        entry.Sizes.AddRange(files.Select(f => f.Data.Length));
        entry.Primitives.AddRange(files.Where(f => IsHeader(f.Name)).Select(f => PrimitiveOf(f.Name, files)));
        return entry;
    }

    /// <summary>The most the precache's directory and file names may take (bytes): see the precache in <see cref="Build"/>.</summary>
    const int PrecacheDirectoryBudget = 150_000;

    /// <summary>
    /// Texture name slots added past the world's own texture files (see <see cref="GuerrillaBitmaps"/>):
    /// for Guerrilla's HUD and other mods. Each slot costs the game about 36 bytes of its start-up pool.
    /// </summary>
    const int SpareTextureSlots = 1_000;

    /// <summary>
    /// How many explosions the game's DLCs add after its own (each DLC's dlcNN_explosions.xtbl, in an
    /// archive of its build\dlcNN\pc\cache folder; the player's copy has what it has), which take room
    /// from the explosions the world can add (see <see cref="GuerrillaEffectTables.ExplosionLimit"/>).
    /// </summary>
    static int DlcExplosions(string game)
    {
        int count = 0;
        string build = Path.Combine(game, "build");
        if (!Directory.Exists(build)) return 0;
        foreach (string dlc in Directory.GetDirectories(build, "dlc*"))
        {
            string cache = Path.Combine(dlc, "pc", "cache");
            if (!Directory.Exists(cache)) continue;
            foreach (string archive in Directory.GetFiles(cache, "*.vpp_pc"))
            {
                var pack = Packfile.Open(archive);
                foreach (var entry in pack.Entries.Where(e => e.Name.StartsWith("dlc", StringComparison.OrdinalIgnoreCase) &&
                                                              e.Name.EndsWith("_explosions.xtbl", StringComparison.OrdinalIgnoreCase)))
                    count += XtblFile.FromBytes(pack.Read(entry)).Entries.Count();
            }
        }
        return count;
    }

    /// <summary>Whether a file is one the asset list names as a primitive (a header file; its data file goes with it).</summary>
    static bool IsHeader(string name)
    {
        string e = Path.GetExtension(name).ToLowerInvariant();
        return e is ".rfgzone_pc" or ".navdata_pc" or ".stchlst_pc" || e.StartsWith(".c");
    }

    /// <summary>A file's asset list primitive: its kind's type, allocator, flags and split index (as the game's), and its and its data file's sizes.</summary>
    static AsmPrimitive PrimitiveOf(string name, IReadOnlyList<(string Name, byte[] Data)> files)
    {
        string ext = Path.GetExtension(name).ToLowerInvariant();
        (byte type, byte allocator, byte flags, byte split) = ext switch
        {
            ".cvbm_pc" => ((byte)1, (byte)1, (byte)6, (byte)1),
            ".cstch_pc" => ((byte)2, (byte)1, (byte)6, (byte)1),
            ".cchk_pc" => ((byte)2, (byte)1, (byte)6, (byte)0),
            ".rfgzone_pc" => ((byte)3, (byte)0, (byte)2, (byte)0),
            ".cterrain_pc" => ((byte)4, (byte)0, (byte)6, (byte)0),
            ".navdata_pc" => ((byte)5, (byte)0, (byte)2, (byte)0),
            ".cfmesh_pc" => ((byte)8, (byte)1, (byte)2, (byte)0),
            ".cefct_pc" => ((byte)13, (byte)1, (byte)6, (byte)0),
            ".ctmesh_pc" => ((byte)21, (byte)0, (byte)6, (byte)0),
            ".stchlst_pc" => ((byte)23, (byte)0, (byte)2, (byte)0),
            _ => throw new InvalidDataException($"no asset list primitive type known for {name}"),
        };
        string dataName = ext.StartsWith(".c") && ext != ".cfmesh_pc" ? Path.ChangeExtension(name, ".g" + ext[2..]) : "";
        var data = files.FirstOrDefault(f => f.Name.Equals(dataName, StringComparison.OrdinalIgnoreCase));
        return new AsmPrimitive { Name = name, Type = type, Allocator = allocator, Flags = flags, SplitIndex = split,
                                  HeaderSize = files.First(f => f.Name == name).Data.Length, DataSize = data.Data?.Length ?? 0 };
    }
}
