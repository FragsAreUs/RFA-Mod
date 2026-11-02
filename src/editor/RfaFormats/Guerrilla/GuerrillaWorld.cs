using System.Numerics;
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
/// each chunk file converted (see <see cref="GuerrillaChunks"/>); a stitch list of the
/// rocks with a collision tree over them (empty in zones without rocks); an environment box over the
/// whole zone; Guerrilla's navigation meshes, for people and
/// vehicles (see <see cref="GuerrillaNavigation"/>; none in the scenery zones, as in Guerrilla). And
/// the time of day (<see cref="GuerrillaTimeOfDay"/>) and the clouds and backdrop for the mod (<see
/// cref="GuerrillaSky"/>).
///
/// What still comes from Armageddon's files, written over: the rocks' template (ter_sur_blder) and the
/// roads' material (world_10_07_2's).
/// Terrain files (see ZoneTerrain), zone files (their objects, the destructible ones too, made from the
/// game's schema), navigation files (Guerrilla's own meshes: see GuerrillaNavigation), sub-zones,
/// textures, containers, archives, stitch lists and asset lists are written from nothing.
///
/// Out: rfg_l0.vpp_pc (the sub-zones' containers and the asset list), rfg_l1.vpp_pc (the zones'),
/// rfg_precache.vpp_pc, zonescript_rfg.vpp_pc, rfg.gtodx and a time of day for each district
/// (rfg_default.dtodx, rfg_eos.dtodx, ...), all new files for the loader's overrides
/// (build\pc\cache\); Armageddon's territories.xtbl with Guerrilla's districts added (an override in
/// build\pc\cache\table.vpp_pc\; see <see cref="GuerrillaTerritory"/>); Armageddon's texture name table
/// with room for the world's textures (an override in build\pc\cache\misc.vpp_pc\; see
/// <see cref="GuerrillaBitmaps"/>); and the mod's cloud files.
/// Guerrilla's files are only read, and what is made from them stays on your machine.
/// </summary>
public static class GuerrillaWorld
{
    public const string World = "rfg";
    public const float ZoneWidth = 511f;

    /// <summary>The start zone: in a part of the grid no precache group names (so its area is the world's).</summary>
    public static readonly (int Col, int Row) DefaultStart = (12, 11);

    const float MaxPit = 60;    // no ground more than this below where its hollow spills
    const float MaxStep = 25f;  // nor more than this below a neighbouring sample, 1 m away: only the deep shafts' walls, not Guerrilla's own cliffs and retaining walls
    const float BoxBottom = -500, BoxTop = 1500;   // the environment boxes' heights
    const float BoxFogNear = 100000;               // where the boxes' own fog would start (their far clip is 1,500 m)
    // The most chunk files (c- and g-files) one container is given. The game reads a container's
    // CPU-side files through a 20 MB pool ("Pregame_split_pool"), asking a little more than their
    // sizes; its own containers hold at most 19 MB of them. Guerrilla's busiest zone, all in one
    // container, asked for 26 MB and stopped the game.
    const long ChunkBudget = 14L << 20;
    static readonly GroundLayout Layout = GroundLayout.Whole;

    static Vector2 Corner(int col, int row) => new((col - 8) * ZoneWidth, (7 - row) * ZoneWidth);

    /// <summary>
    /// A zone's look from its Guerrilla terrain file: its ground layers' textures (colour and normal, four
    /// layers, without ".tga") and constants, blend and colour maps, cliff materials and layer map.
    /// </summary>
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

        /// <summary>Every ground and cliff texture the zone's terrain file names (without ".tga").</summary>
        public IEnumerable<string> TerrainTextures =>
            Textures.Take(8).Concat(Enumerable.Range(0, Cliffs.Count).SelectMany(CliffPairs).SelectMany(p => new[] { p.Colour, p.Normal }))
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
                foreach (var file in Packfile.FromBytes(entry.Name, pack.Read(entry)).Entries)
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
        var worldL0 = Packfile.Open(Path.Combine(cache, "world_l0.vpp_pc"));
        var precache = Packfile.Open(Path.Combine(cache, "dlc01_precache.vpp_pc"));
        byte[] Pre(string name) => precache.Read(precache.Find(name) ?? throw new InvalidDataException($"no {name} in dlc01_precache.vpp_pc"));
        // Armageddon's road decal material (world_10_07's, in one of its sub-zones), for Guerrilla's road decals.
        var roadContainer = Packfile.FromBytes("0h_c3224.str2_pc", worldL0.Read(worldL0.Find("0h_c3224.str2_pc") ?? throw new InvalidDataException("no 0h_c3224 in world_l0.vpp_pc")));
        var (roadMap, roadMaterial) = TerrainSubzone.RoadMaterials(roadContainer.Read(roadContainer.Find("world_10_07_2.ctmesh_pc")
                                          ?? throw new InvalidDataException("no world_10_07_2.ctmesh_pc in world_l0.vpp_pc's 0h_c3224"))).FirstOrDefault();
        if (roadMap == null || roadMaterial == null) throw new InvalidDataException("world_10_07_2 has no road decal");

        // ---- Guerrilla's files: in a zone's own containers (both layers), else its precache.
        var rfgL0 = Packfile.Open(Path.Combine(rfgCache, "terr01_l0.vpp_pc"));
        var rfgL1 = Packfile.Open(Path.Combine(rfgCache, "terr01_l1.vpp_pc"));
        var rfgPrecache = Packfile.Open(Path.Combine(rfgCache, "terr01_precache.vpp_pc"));
        var containers = new Dictionary<string, Packfile?>();
        Packfile? Container(Packfile archive, string name)
        {
            string key = archive.Name + "/" + name;
            if (!containers.TryGetValue(key, out var c))
                containers[key] = c = archive.Find(name + ".str2_pc") is { } e ? Packfile.FromBytes(name, archive.Read(e)) : null;
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
        var whiteFiles = GuerrillaRocks.WhiteTextureFiles();
        byte[] rockHeader = Pre(GuerrillaRocks.TemplatePiece + ".cstch_pc"), rockData = Pre(GuerrillaRocks.TemplatePiece + ".gstch_pc");
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
                    var piece = GuerrillaRocks.Convert(rockHeader, rockData, h, g, Alias);
                    rockPieces[p.Name] = piece;
                    foreach (string texture in new[] { piece.Diffuse, piece.Normal }.Select(Path.GetFileNameWithoutExtension).OfType<string>().Where(t => !rockTextures.ContainsKey(t)))
                    {
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

        // ---- The ground, sampled whole from Guerrilla's sub-zones of each zone and those around it.
        int S = Layout.Samples, last = S - 1;
        var heights = new Dictionary<(int, int), float[]>();
        var holes = new Dictionary<(int, int), SquareCut[]>();
        var subzoneCache = new Dictionary<(int, int, int), TerrainSubzone?>();
        TerrainSubzone? Subzone(int col, int row, int n)
        {
            if (subzoneCache.TryGetValue((col, row, n), out var cached)) return cached;
            string stem = $"terr01_{col:00}_{row:00}_{n}";
            var read = RfgFile(col, row, stem + ".ctmesh_pc") is { } h && RfgFile(col, row, stem + ".gtmesh_pc", 1) is { } g ? TerrainSubzone.Read(h, g) : null;
            return subzoneCache[(col, row, n)] = read;
        }
        foreach (var (col, row) in zones)
        {
            var subzones = new List<TerrainSubzone>();
            for (int dc = -1; dc <= 1; dc++)
                for (int dr = -1; dr <= 1; dr++)
                    for (int n = 0; n < 9; n++)
                        if (Subzone(col + dc, row + dr, n) is { } s) subzones.Add(s);
            var corner = Corner(col, row);
            var over = subzones.Where(s => s.Positions.Length > 0 && s.Positions.Max(p => p.X) >= corner.X - 1 && s.Positions.Min(p => p.X) <= corner.X + ZoneWidth + 1 &&
                                           s.Positions.Max(p => p.Z) >= corner.Y - 1 && s.Positions.Min(p => p.Z) <= corner.Y + ZoneWidth + 1);
            heights[(col, row)] = GuerrillaGround.Sample(over, corner, Layout, RockCorners(col, row)) ?? throw new InvalidDataException($"no Guerrilla ground for terr01_{col:00}_{row:00}");
            holes[(col, row)] = GuerrillaGround.RoadHoles(over, corner, Layout);
        }
        log($"Ground: {zones.Count} zones sampled, {S} × {S} samples each");
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
        LimitGround(zones, heights, log);

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
                foreach (GuerrillaRoadDecal decal in Subzone(col, row, n)?.RoadDecals ?? [])
                {
                    Vector3 middle = decal.Corners.Length > 0 ? decal.Corners.Aggregate(Vector3.Add) / decal.Corners.Length : decal.Position;
                    int part = Math.Clamp((int)((middle.X - corner.X) / third), 0, 2) + 3 * Math.Clamp((int)((middle.Z - corner.Y) / third), 0, 2);
                    lists[part].Add(GuerrillaRoads.Convert(decal, roadMap, roadMaterial, RoadTexture));
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
        log($"Roads: {roadsIn.Values.Sum(l => l.Sum(d => d.Count))} road decals, {roadTextures.Count} textures");
        // A road decal's texture's new name: the zone's blend map ours (terr01_05_05_alpha00 is
        // rfg0505_a00, as the ground's), the rest as the ground's textures.
        static bool IsBlendMap(string texture) => texture.StartsWith("terr01_", StringComparison.OrdinalIgnoreCase) && texture.EndsWith("_alpha00", StringComparison.OrdinalIgnoreCase);
        static string RoadTexture(string texture) => IsBlendMap(texture) ? $"rfg{texture[7..9]}{texture[10..12]}_a00" : Alias(texture);

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
                foreach (var o in objects)
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
                    propsIn[(col, row)].Add(o);
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
        var gameplayProperties = TableNames("gameplay_properties.xtbl");
        // Guerrilla's gameplay_props names that Armageddon's table lacks (121, on about 2,000 objects;
        // nearly all cast shadows) get Armageddon's Default entry with the same shadows and anchoring as
        // Guerrilla's entry: Default, Default_shadow, Default_Prevent_Anchor or Default_Shadow_and_Anchor.
        var rfgTables = Packfile.Open(Path.Combine(rfgCache, "misc.vpp_pc"));
        var guerrillaGameplay = XtblFile.FromBytes(rfgTables.Read(rfgTables.Find("gameplay_properties.xtbl") ?? throw new InvalidDataException("no gameplay_properties.xtbl in Guerrilla's misc.vpp_pc")))
            .Entries.GroupBy(XtblFile.EntryName, StringComparer.OrdinalIgnoreCase).ToDictionary(g => g.Key, g => g.First(), StringComparer.OrdinalIgnoreCase);
        string? GameplayProps(string name)
        {
            if (gameplayProperties.Contains(name)) return name;
            if (!guerrillaGameplay.TryGetValue(name, out var entry)) return null;
            var flags = (entry.Element("chunk_flags")?.ElementsNamed("Flag").Select(f => f.Value.Trim()) ?? []).ToHashSet(StringComparer.OrdinalIgnoreCase);
            bool shadows = flags.Contains("casts_shadows"), anchor = flags.Contains("prevent_anchor");
            string closest = shadows && anchor ? "Default_Shadow_and_Anchor" : shadows ? "Default_shadow" : anchor ? "Default_Prevent_Anchor" : "Default";
            return gameplayProperties.Contains(closest) ? closest : null;
        }

        var schema = ZoneSchema.FromGame(game) ?? throw new InvalidDataException($"no {ZoneSchema.FileName} in the game's {ZoneSchema.ArchiveName}");

        var asm = new AsmFile();
        var asm1 = new AsmFile();
        // The marker that names the world's precache (rfg_precache.vpp_pc; see the precache below).
        var layer = new List<(string Name, byte[] Data)> { (World + "_precache.precache", worldL0.Read(worldL0.Find("world_precache.precache")!)) };
        var pending = new List<PendingContainer>();
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

        foreach (var (col, row) in zones)
        {
            string zone = $"{World}_{col:00}_{row:00}";
            bool isStart = (col, row) == start;
            var look = looks[(col, row)];
            var h = heights[(col, row)];
            var corner = Corner(col, row);
            var zoneMiddle = new Vector3(corner.X + ZoneWidth / 2, 0, corner.Y + ZoneWidth / 2);
            uint prefix = (uint)(col * 16 + row) << 24;

            // The ground. The blend map's alpha is its cliff weight too (red: the cliff shader mixes its
            // two textures by alpha, which the ground's shader doesn't read).
            var blend = new RgbaImage(look.Blend.Width, look.Blend.Height);
            Array.Copy(look.Blend.Pixels, blend.Pixels, blend.Pixels.Length);
            for (int p = 0; p < blend.Pixels.Length; p += 4) blend.Pixels[p + 3] = blend.Pixels[p];
            var cliff = new byte[S * S];
            for (int z = 0; z < S; z++)
                for (int x = 0; x < S; x++)
                    cliff[z * S + x] = blend.Pixels[(Math.Min(z, blend.Height - 1) * blend.Width + Math.Min(x, blend.Width - 1)) * 4];
            var subzones = GuerrillaGround.Subzones(h, corner, null, cliff, Layout, holes[(col, row)], roadsIn[(col, row)]);
            string blendName = $"rfg{col:00}{row:00}_a00", colourName = $"rfg{col:00}{row:00}cmb";
            var (terrainFile, terrainData) = ZoneTerrain(h, corner, look, barriers[(col, row)], zone, blendName, colourName);
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
            var marker = zoneFile.AddNew(Class("obj_zone"), prefix | 0x000020, schema.Class("obj_zone")!.NewObject(zoneMiddle, DefaultChoice),
                                         zoneMiddle - new Vector3(0.5f), zoneMiddle + new Vector3(0.5f));
            marker.Property("terrain_file_name")!.SetText(zone);
            ZoneEnvironment(zoneFile, schema, prefix | 0x030008, zoneMiddle);
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
                var start = file.AddNew(Class("player_start"), prefix | 0x000004, schema.Class("player_start")!.NewObject(startAt),
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
                                                               levelObjects.Contains, GameplayProps,
                                                               chunk => BitConverter.ToUInt32(chunkFiles[chunk].Header, 0x24), taken);
                foreach (var mover in movers) zoneFile.Insert(zoneFile.Objects.Count, mover);
            }
            byte[] zoneBytes = GuerrillaMovers.WithLinks(zoneFile.ToBytes(), links);
            // The persistent zone: the player start again (as the game's own levels keep one there), and
            // no district or mission (0, as the game's persistent zones).
            var persistent = ZoneFile.Create(0, 0);
            PlayerStart(persistent);
            zonescript.Add((zone + ".rfgzone_pc", zoneBytes));
            zonescript.Add(("p_" + zone + ".rfgzone_pc", persistent.ToBytes()));

            // The stitch list: Guerrilla's rocks, with a collision tree reaching all of them, each by the
            // box its own collision fills where it is placed; none in a zone without rocks (Guerrilla's
            // scenery zones and some others), whose list is empty, with no tree, which the game's loader
            // takes (Armageddon's own levels put a floor piece in such zones instead).
            var rocks = rocksIn[(col, row)];
            var boxes = rocks.Select(r => StitchList.PlacedBox(rockPieces[r.Name].CollisionMin, rockPieces[r.Name].CollisionMax, r.Rotation, r.Position)).ToList();
            byte[] stitch = StitchList.Write(rocks, zoneMiddle, boxes);

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

            // Its containers and asset list entries.
            var files = new List<(string Name, byte[] Data)>();
            foreach (string texture in look.TerrainTextures) files.AddRange(layerFiles[texture]);
            foreach (string texture in roadTexturesIn[(col, row)]) files.AddRange(roadTextures[texture]);
            foreach (string name in rocks.Select(r => r.Name).Distinct(StringComparer.OrdinalIgnoreCase))
            {
                var piece = rockPieces[name];
                files.Add((name + ".cstch_pc", piece.Header));
                files.Add((name + ".gstch_pc", piece.Data));
                foreach (string texture in new[] { piece.Diffuse, piece.Normal }.Select(Path.GetFileNameWithoutExtension).OfType<string>())
                    files.AddRange(rockTextures[texture]);
            }
            if (rocks.Count > 0) files.AddRange(whiteFiles);
            AddChunks(files, zoneChunks);
            files = files.DistinctBy(f => f.Name, StringComparer.OrdinalIgnoreCase).ToList();
            files.AddRange(new (string Name, byte[] Data)[]
            {
                (blendName + ".cvbm_pc", PegFile.SingleHeader(blendName + ".tga", blend.Width, blend.Height, TextureFormat.Argb8888, 1, PegFile.DataFlag)), (blendName + ".gvbm_pc", blendData),
                (colourName + ".cvbm_pc", PegFile.SingleHeader(colourName + ".tga", look.Colour.Width, look.Colour.Height, TextureFormat.Dxt1, colourLevels, 0)), (colourName + ".gvbm_pc", colourData),
                (zone + ".cterrain_pc", terrainFile), (zone + ".gterrain_pc", terrainData),
                (zone + ".navdata_pc", navigation[(col, row)].ToBytes()), (zone + ".stchlst_pc", stitch), (zone + ".rfgzone_pc", zoneBytes),
            });
            pending.Add(new PendingContainer($"1h_c{col:00}{15 - row:00}", ZoneLayer: true, files));
            for (int n = 0; n < 9; n++)
            {
                var partFiles = new List<(string Name, byte[] Data)> { ($"{zone}_{n}.ctmesh_pc", subzones[n].Header), ($"{zone}_{n}.gtmesh_pc", subzones[n].Data) };
                AddChunks(partFiles, chunksIn[n]);
                pending.Add(new PendingContainer($"0h_c{col * 3 + n % 3:00}{(15 - row) * 3 + n / 3:00}", ZoneLayer: false,
                                                 partFiles.DistinctBy(f => f.Name, StringComparer.OrdinalIgnoreCase).ToList()));
            }
            log($"{zone}{(isStart ? " (start)" : "")}: ground {h.Min():0} to {h.Max():0} m, {rocks.Count} rocks, {propsIn[(col, row)].Count} destructible objects ({links.Length / ZoneFile.DestructionLinkSize} links), {files.Sum(f => (long)f.Data.Length) / 1024:N0} KB of files in the zone's container");
        }

        // ---- The precache: every file more than one container holds (the ground's layers, roads,
        // rocks, chunk files and their textures) is kept once, loose, in the world's precache archive
        // (rfg_precache.vpp_pc), and each cell's precache group in the asset list (0p_cXXYY for a
        // sub-zone, 1p_cXXYY for a zone: type 17, flags 0x200, no data, a primitive for each header
        // file) names those it needs: the game loads each of them once, while any cell that names it
        // is loaded. So Guerrilla keeps its shared files (terr01_precache.vpp_pc and a precache group
        // for every cell) and Armageddon its areas' (surface_precache.vpp_pc, 0p_ and 1p_ groups in
        // world_l0.asm_pc). The guerrilla_world mod has the game mount it (its area for our worlds).
        var holders = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        var lengths = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        var differing = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var c in pending)
            foreach (var (name, data) in c.Files)
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
        foreach (var unit in holders.Keys.Where(n => holders[n] > 1 && !differing.Contains(n))
                                         .GroupBy(UnitOf, StringComparer.OrdinalIgnoreCase)
                                         .OrderByDescending(u => (long)(u.Max(n => holders[n]) - 1) * u.Sum(n => (long)lengths[n])))
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
        long before = 0;
        foreach (var c in pending)
        {
            before += c.Files.Sum(f => (long)f.Data.Length);
            var kept = c.Files.Where(f => !Shared(f.Name)).ToList();
            var fromPrecache = c.Files.Where(f => Shared(f.Name)).ToList();
            foreach (var (name, data) in fromPrecache) precacheFiles.TryAdd(name, data);
            byte[] bytes = Packfile.CreateContainer(kept);
            var own = ContainerEntry(c.Name, bytes, kept);
            var precacheGroup = new AsmContainer { Name = c.Name[..1] + "p" + c.Name[2..], Type = 17, Flags = 0x200 };
            precacheGroup.Primitives.AddRange(fromPrecache.Where(f => IsHeader(f.Name)).Select(f => PrimitiveOf(f.Name, fromPrecache)));
            if (precacheGroup.Primitives.Count > 0) asm.Containers.Add(precacheGroup);  // all in layer 0's list, as Guerrilla's
            if (c.ZoneLayer)
            {
                asm1.Containers.Add(own);
                layer1.Add((c.Name + ".str2_pc", bytes));
                // The zone's group: everything the zone needs, its container's and its precache group's.
                var group = new AsmContainer { Name = "1_" + c.Name[3..], Type = 17, Flags = 0x2300 };
                group.Primitives.AddRange(own.Primitives.Concat(precacheGroup.Primitives).Select(p => new AsmPrimitive
                {
                    Name = p.Name, Type = p.Type, Allocator = p.Allocator, Flags = p.Flags, SplitIndex = p.SplitIndex, HeaderSize = p.HeaderSize, DataSize = p.DataSize,
                }));
                asm.Containers.Add(group);  // the groups stay in layer 0's list, as Guerrilla's
            }
            else
            {
                asm.Containers.Add(own);
                layer.Add((c.Name + ".str2_pc", bytes));
            }
        }
        long sharedBytes = precacheFiles.Values.Sum(d => (long)d.Length), after = pending.Sum(c => c.Files.Where(f => !Shared(f.Name)).Sum(f => (long)f.Data.Length));
        log($"Precache: {precacheFiles.Count} files ({sharedBytes / 1048576:N0} MB) held by more than one container, kept once (its directory " +
            $"{directory / 1024:N0} KB of the {PrecacheDirectoryBudget / 1024:N0} KB it may take; {left} more such files stay in their containers); " +
            $"the containers' files {before / 1048576:N0} MB → {after / 1048576:N0} MB");
        foreach (var (name, piece) in rockPieces)
        {
            var pieceFiles = new List<(string Name, byte[] Data)> { (name + ".cstch_pc", piece.Header), (name + ".gstch_pc", piece.Data) };
            foreach (var texture in new[] { piece.Normal, piece.Diffuse }.Select(t => rockTextures[Path.GetFileNameWithoutExtension(t)])) pieceFiles.AddRange(texture);
            pieceFiles.AddRange(whiteFiles);
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
        layer.Add((World + "_l0.asm_pc", asm.ToBytes()));
        layer1.Add((World + "_l1.asm_pc", asm1.ToBytes()));

        string output = Path.Combine(overrides, "build", "pc", "cache");
        Directory.CreateDirectory(output);
        // Written straight into their files: with Guerrilla's buildings the layers pass 2 GB. The zone
        // files are stored compressed, as the game's own zonescript_world.vpp_pc is: the game reads the
        // archive's stored bytes through a 20 MB pool, and with Guerrilla's objects they come to 26 MB.
        Packfile.Write(Path.Combine(output, World + "_l0.vpp_pc"), layer);
        Packfile.Write(Path.Combine(output, World + "_precache.vpp_pc"), precacheFiles.Select(f => (f.Key, f.Value)).ToList());
        Packfile.Write(Path.Combine(output, World + "_l1.vpp_pc"), layer1);
        Packfile.Write(Path.Combine(output, "zonescript_" + World + ".vpp_pc"), zonescript, compressed: true);
        // Guerrilla's districts (the territory, an override of Armageddon's table) and a time of day for each.
        var rfgMisc = Packfile.Open(Path.Combine(rfgCache, "misc.vpp_pc"));
        var (territories, districts) = GuerrillaTerritory.Build(Packfile.Open(Path.Combine(cache, "table.vpp_pc")), rfgMisc, World);
        string tables = Path.Combine(output, "table.vpp_pc");
        Directory.CreateDirectory(tables);
        File.WriteAllBytes(Path.Combine(tables, "territories.xtbl"), territories);
        var (orbitals, sets) = GuerrillaTimeOfDay.Build(Packfile.Open(Path.Combine(cache, "misc.vpp_pc")), rfgMisc, districts);
        File.WriteAllBytes(Path.Combine(output, World + ".gtodx"), orbitals);
        foreach (var (name, set) in sets) File.WriteAllBytes(Path.Combine(output, $"{World}_{name.Replace(' ', '_').ToLowerInvariant()}.dtodx"), set);
        log($"Districts: {string.Join(", ", districts.Select(d => $"{d.Guerrilla} ({d.Name})"))}, each with Guerrilla's time of day ({World}_<name>.dtodx), " +
            $"in the territory {World} (table.vpp_pc\\territories.xtbl)");
        // Room for the world's textures in the game's texture name table (see GuerrillaBitmaps): a slot
        // for every texture file the world has, and some to spare (the HUD's, other mods').
        var textureFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var container in pending)
            foreach (var file in container.Files.Where(f => f.Name.EndsWith(".cvbm_pc", StringComparison.OrdinalIgnoreCase))) textureFiles.Add(file.Name);
        foreach (string name in precacheFiles.Keys.Where(n => n.EndsWith(".cvbm_pc", StringComparison.OrdinalIgnoreCase))) textureFiles.Add(name);
        int extraSlots = (textureFiles.Count + SpareTextureSlots + 499) / 500 * 500;
        var gameMisc = Packfile.Open(Path.Combine(cache, "misc.vpp_pc"));
        byte[] bitmaps = gameMisc.Read(gameMisc.Find("bitmaps_pc") ?? throw new InvalidDataException("no bitmaps_pc in misc.vpp_pc"));
        string miscFolder = Path.Combine(output, "misc.vpp_pc");
        Directory.CreateDirectory(miscFolder);
        File.WriteAllBytes(Path.Combine(miscFolder, "bitmaps_pc"), GuerrillaBitmaps.WithRoom(bitmaps, extraSlots));
        log($"Textures: {textureFiles.Count:N0} texture files; the game's texture name table gets {extraSlots:N0} more slots " +
            $"({GuerrillaBitmaps.Capacity(bitmaps):N0} → {GuerrillaBitmaps.Capacity(bitmaps) + extraSlots:N0}; misc.vpp_pc\\bitmaps_pc)");
        log($"World {World}: {zones.Count} zones, start {World}_{start.Col:00}_{start.Row:00}; written into {output}");
        GuerrillaSky.Write(cache, rfgCache, modFolder, log);
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
    /// </summary>
    static void LimitGround(IReadOnlyList<(int Col, int Row)> zones, Dictionary<(int, int), float[]> heights, Action<string> log)
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
        int plugged = 0, raised = 0;
        for (int k = 0; k < ground.Length; k++)
            if (present[k] && ground[k] < filled[k] - MaxPit) { ground[k] = filled[k] - MaxPit; plugged++; }
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
        log($"Ground: {plugged:N0} samples raised to within {MaxPit} m of their hollow's spill, {raised:N0} so no drop is over {MaxStep} m a sample");
    }

    /// <summary>A Guerrilla texture's name in our world (its name's hash, so every alias is as short and the names in our files never grow).</summary>
    static string Alias(string texture) => "rfg_" + NameHashes.PropertyHash(texture).ToString("x8");

    /// <summary>
    /// A zone's terrain file, written from its parts (see <see cref="TerrainFile"/>): terrain_data field by field;
    /// every material written from code (<see cref="TerrainMaterials"/>): the ground's with Guerrilla's
    /// layer textures, scales, offsets and tints, the three cliff kinds from Guerrilla's own cliff
    /// materials (textures, projections, tints; a zone with none: the ground's layers 0 and 3), the far
    /// meshes' with the zone's colour map; Guerrilla's barriers and layer map; no foliage (Guerrilla's is
    /// still to port); the texture list of what the materials name; the height field's Havok packfile
    /// (<see cref="TerrainHeightField.Section"/>) and the far meshes and their buffer (<see cref="FarGround"/>)
    /// from the zone's heights. Nothing of another level's terrain file is used. Returns the terrain
    /// file and its .gterrain_pc.
    /// </summary>
    static (byte[] Terrain, byte[] Data) ZoneTerrain(float[] heights, Vector2 corner, Look look, IReadOnlyList<ChunkShape> barriers,
                                                     string zone, string blendName, string colourName)
    {
        var t = new TerrainFile { HasGround = true };
        string Tga(string name) => name + ".tga";

        // The ground's collision: a height field of 512 × 512 samples a metre apart (the zone's 511 m),
        // each height as a 16-bit sample of 1/32 m from -1,000 m (as the game's own).
        const int FieldSamples = 512;
        const float SampleOffset = -1000, SampleScale = 1 / 32f;
        float[] field = GuerrillaGround.FieldHeights(heights, FieldSamples, Layout);
        var samples = field.Select(h => (ushort)Math.Clamp(MathF.Round((h - SampleOffset) / SampleScale), 0, ushort.MaxValue)).ToArray();
        float fieldStep = ZoneWidth / (FieldSamples - 1);
        t.HeightField = TerrainHeightField.Section(FieldSamples, FieldSamples, fieldStep, 1 / fieldStep, ZoneWidth, ZoneWidth, SampleOffset, SampleScale, samples);
        float low = samples.Min() * SampleScale + SampleOffset, high = samples.Max() * SampleScale + SampleOffset;
        t.SubzoneSlots = new byte[9 * 4];

        // The far-away meshes, from the heights as stored.
        var (farRecords, farBlocks, farData) = FarGround.Build(samples.Select(s => s * SampleScale + SampleOffset).ToArray(), FieldSamples, fieldStep, corner, ZoneWidth);
        t.FarRecords = farRecords;
        t.FarBlocks.AddRange(farBlocks);

        // The ground: Guerrilla's four layers, their scales, offsets and tints, and its last two constants.
        var (groundNames, groundMaterials) = TerrainMaterials.Ground(look.Textures.Take(8).Select(n => Tga(Alias(n))).ToList(), Tga(blendName), Tga(colourName),
            [look.Constants[0], look.Constants[2], look.Constants[4], look.Constants[6]], [look.Constants[1], look.Constants[3], look.Constants[5], look.Constants[7]],
            look.Constants[8], look.Constants[9]);

        // The cliffs, as the game's three kinds: texture A alone, A blended into B, B alone. From Guerrilla's
        // first three cliff materials, which are those kinds in most zones (fewer: A from its first, B from
        // its last; none: the ground's layers 0 and 3, as the game's terrain repeats them, and their tints).
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
        var cliffs = new List<(byte[] Names, byte[] Material)>
        {
            TerrainMaterials.Cliff(Named(sideA.Pair), null, Tga(blendName), Tga(colourName), sideA.Xforms, [sideA.Tint], sideA.Specular),
            TerrainMaterials.Cliff(Named(sideA.Pair), Named(sideB.Pair), Tga(blendName), Tga(colourName), [.. sideA.Xforms, .. sideB.Xforms], [sideA.Tint, sideB.Tint], sideA.Specular),
            TerrainMaterials.Cliff(Named(sideB.Pair), null, Tga(blendName), Tga(colourName), sideB.Xforms, [sideB.Tint], sideB.Specular),
        };
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
        var effectNames = TerrainFile.LayerMapPart.PaddedNames(guerrillaNames,
            ["Terrain", "Dirt", "Ice", "Steel", "Rubber", "Concrete", "Composite", "Unobtanium", "Electronics"]);
        t.LayerMap = new TerrainFile.LayerMapPart(cells, new byte[effectNames.Count * 4], effectNames, new byte[effectNames.Count * 4]);
        t.Occluders = [];

        // terrain_data, field by field (the box's heights the height field's).
        var data = new byte[TerrainFile.DataSize];
        void Word(int at, int value) => BitConverter.TryWriteBytes(data.AsSpan(at), value);
        void Float(int at, float value) => BitConverter.TryWriteBytes(data.AsSpan(at), value);
        Word(0x00, 1);
        Float(0x04, corner.X); Float(0x08, low); Float(0x0C, corner.Y);
        Float(0x10, corner.X + ZoneWidth); Float(0x14, high); Float(0x18, corner.Y + ZoneWidth);
        Word(0x1C, FieldSamples); Word(0x20, FieldSamples);
        Word(0x24, 0);                                  // occluders
        Word(0x4C, t.SubzoneSlots.Length / 4);          // sub-zones
        Word(0x58, 512); Word(0x5C, 512); Word(0x60, 2); Word(0x64, cells.Length); Word(0x6C, effectNames.Count);
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
        return (t.Write(), farData);

    }

    /// <summary>
    /// A zone's environment box, made from the game's schema (object_env_settings): every setting at the
    /// schema's default, which affects nothing until its affects_ flag is set, then what the world needs
    /// (the game's own world zones set these groups in their boxes too):
    ///   the box: the whole zone, every height (BoxBottom to BoxTop, half a metre past its edges)
    ///   visibility: the far clip 1,500 m (the open world's view; the default is 475) and the rocks'
    ///     detail kept to 200 m (the default 125)
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
        Number("far_clip", 1500);
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

    /// <summary>Whether a file is one the asset list names as a primitive (a header file; its data file goes with it).</summary>
    static bool IsHeader(string name)
    {
        string e = Path.GetExtension(name).ToLowerInvariant();
        return e is ".rfgzone_pc" or ".navdata_pc" or ".stchlst_pc" || e.StartsWith(".c");
    }

    /// <summary>A container of the world before its shared files go to the precache: its name (0h_cXXYY or 1h_cXXYY), whether it is in the zones' layer, and its files.</summary>
    sealed record PendingContainer(string Name, bool ZoneLayer, List<(string Name, byte[] Data)> Files);

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
