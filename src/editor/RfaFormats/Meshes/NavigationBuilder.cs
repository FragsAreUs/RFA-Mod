using System.Numerics;

namespace RfaFormats.Meshes;

/// <summary>
/// Navigation made from nothing, for a new level's flat ground (see the editor's New level): a square
/// of ground cut into a grid of triangle cells, for the people's layer and the vehicles' (the same
/// mesh, the vehicles' cells flagged 0x20), each with its Havok tree. Laid out as the game's own files
/// are (see <see cref="NavigationFile"/>, <see cref="NavigationLayer"/>): cells wound as the game's
/// (x/z cross product of the edges negative), each joined to the cells across its edges (edge k runs
/// from vertex k to vertex k+1), the open edges on the zone's border its outer edges, one heap slot a
/// cell and no spare ones, one island, and the pools as Volition's tools wrote them for a layer with
/// nothing computed (<see cref="NavigationLayer.PlainPools"/>).
///
/// The tree is a MOPP block ("MOPP", 0x12D, the box's corner less 0.05, the scale, the code's size, 1,
/// then the code), as the game's: the scale 2^24 / ((the box's longest side + 0.1) × 128/127), a point
/// at floor((p − corner) × scale) and its top 8 bits compared (the code never shifts). The code is a
/// k-d tree over the cells: at each node a split on the axis the cells' middles spread most along
/// (0x23 + axis, the left half's highest and the right half's lowest value, and two 16-bit jumps:
/// left over the jump that follows, right onto it), the jump (0x07, 24 bits) over the left half, the
/// left half and the right; at each leaf the cell's box on the three axes, a value of slack each way
/// (0x26, 0x27, 0x28: lowest, highest), then the cell's number (0x30 + it below 32, else 0x50, 0x51
/// or 0x52 and 8, 16 or 24 bits). Multi-byte values big-endian. Untested in the game (the same code as
/// the Guerrilla editor's, which is too).
/// </summary>
public static class NavigationBuilder
{
    /// <summary>
    /// A navigation file for a flat square of ground: <paramref name="centre"/> (x, z) and
    /// <paramref name="half"/> its half width, at <paramref name="height"/>, in the zone whose corner is
    /// <paramref name="zoneCorner"/> (its lowest x and z) and side <paramref name="zoneWidth"/>, at
    /// <paramref name="place"/> in the world's grid (column × 16 + row: the single-zone levels' 0x88).
    /// Cells about <paramref name="step"/> m across.
    /// </summary>
    public static NavigationFile FlatSquare(Vector2 centre, float half, float height, Vector2 zoneCorner, float zoneWidth, byte place = 0x88, float step = 2) =>
        FromGround((_, _) => height, centre, half, zoneCorner, zoneWidth, place, step);

    /// <summary>
    /// A navigation file over ground of any shape: a grid over the square (<paramref name="centre"/>,
    /// <paramref name="half"/>) following <paramref name="height"/> (x, z → y), leaving out the squares
    /// steeper than <paramref name="steepest"/> degrees for people (<paramref name="steepestForVehicles"/>
    /// for vehicles: as the game's own meshes, people to about 42°, vehicles less), and those at the
    /// bottom of a drop (all four corners at the lowest height of ground that otherwise stands well
    /// above it: the 300 m drop round a new level's square). Islands are numbered by what joins.
    /// </summary>
    public static NavigationFile FromGround(Func<float, float, float> height, Vector2 centre, float half, Vector2 zoneCorner, float zoneWidth,
                                            byte place = 0x88, float step = 2, float steepest = 42, float steepestForVehicles = 30)
    {
        var (people, peopleTree) = Grid(height, centre, half, zoneCorner, zoneWidth, place, step, vehicles: false, steepest);
        var (vehicles, vehiclesTree) = Grid(height, centre, half, zoneCorner, zoneWidth, place, step, vehicles: true, steepestForVehicles);
        return new NavigationFile(0, people, peopleTree, vehicles, vehiclesTree);
    }

    const int MinPieceCells = 16;  // (with 2 m squares, 32 m² of ground)
    const int MostPieces = 250;    // (islands are numbered in a byte)

    /// <summary>The cells in the pieces (joined across their edges) of at least <paramref name="minCells"/> cells, the
    /// <paramref name="most"/> biggest; the biggest piece alone if none is that big. The vertices are those they use.</summary>
    static (List<ushort[]> Corners, Vector3[] Vertices) KeepBigPieces(List<ushort[]> corners, List<Vector3> vertices, int minCells, int most)
    {
        var byEdge = new Dictionary<(int, int), int>();
        var piece = new int[corners.Count];
        var parent = Enumerable.Range(0, corners.Count).ToArray();
        int Find(int x)
        {
            while (parent[x] != x) x = parent[x] = parent[parent[x]];
            return x;
        }
        for (int cell = 0; cell < corners.Count; cell++)
            for (int k = 0; k < 3; k++)
            {
                int p = corners[cell][k], q = corners[cell][(k + 1) % 3];
                var key = (Math.Min(p, q), Math.Max(p, q));
                if (byEdge.Remove(key, out int other)) parent[Find(cell)] = Find(other);
                else byEdge[key] = cell;
            }
        for (int cell = 0; cell < corners.Count; cell++) piece[cell] = Find(cell);
        var sizes = piece.GroupBy(p => p).Select(g => (Piece: g.Key, Cells: g.Count())).OrderByDescending(g => g.Cells).ToList();
        var keep = sizes.Where(g => g.Cells >= minCells).Take(most).Select(g => g.Piece).ToHashSet();
        if (keep.Count == 0) keep.Add(sizes[0].Piece);

        var remap = new Dictionary<ushort, ushort>();
        var usedVertices = new List<Vector3>();
        var keptCorners = new List<ushort[]>();
        for (int cell = 0; cell < corners.Count; cell++)
        {
            if (!keep.Contains(piece[cell])) continue;
            keptCorners.Add(corners[cell].Select(v =>
            {
                if (!remap.TryGetValue(v, out ushort at))
                {
                    remap[v] = at = (ushort)usedVertices.Count;
                    usedVertices.Add(vertices[v]);
                }
                return at;
            }).ToArray());
        }
        return (keptCorners, usedVertices.ToArray());
    }

    static (NavigationLayer Layer, byte[] Tree) Grid(Func<float, float, float> height, Vector2 centre, float half, Vector2 zoneCorner, float zoneWidth, byte place,
                                                     float step, bool vehicles, float steepest)
    {
        // 2 m squares (4 m before): the game switches off the cells a building or a parked vehicle stands on,
        // and with big cells that took the ground at their doors too (the player walked round and round a
        // vehicle to get in; doorway navpoints found no cell; the mesh fell into pieces). 127 squares a side
        // is 32,258 cells, just under the cap below, and 16,384 vertices.
        int n = Math.Clamp((int)MathF.Ceiling(2 * half / step), 1, 127);
        float s = 2 * half / n, x0 = centre.X - half, z0 = centre.Y - half;
        var grid = new Vector3[(n + 1) * (n + 1)];
        for (int j = 0; j <= n; j++)
            for (int i = 0; i <= n; i++) grid[j * (n + 1) + i] = new Vector3(x0 + i * s, height(x0 + i * s, z0 + j * s), z0 + j * s);
        float lowest = grid.Min(v => v.Y), highestGround = grid.Max(v => v.Y);
        bool drop = highestGround - lowest > 50;
        float slopeLimit = MathF.Tan(steepest * MathF.PI / 180);

        // Two cells a square, wound as the game's: (v1 − v0) × (v2 − v0) across x and z negative; squares
        // too steep, or at the bottom of a drop, left out. Only the vertices the cells use are kept.
        var used = new Dictionary<int, ushort>();
        var kept = new List<Vector3>();
        ushort V(int i, int j)
        {
            int k = j * (n + 1) + i;
            if (!used.TryGetValue(k, out ushort at))
            {
                if (kept.Count >= ushort.MaxValue) throw new InvalidOperationException("too many navigation vertices");
                used[k] = at = (ushort)kept.Count;
                kept.Add(grid[k]);
            }
            return at;
        }
        var corners = new List<ushort[]>();
        for (int j = 0; j < n; j++)
            for (int i = 0; i < n; i++)
            {
                Vector3 pa = grid[j * (n + 1) + i], pb = grid[j * (n + 1) + i + 1], pc = grid[(j + 1) * (n + 1) + i + 1], pd = grid[(j + 1) * (n + 1) + i];
                float rise = MathF.Max(MathF.Max(MathF.Abs(pa.Y - pb.Y), MathF.Abs(pd.Y - pc.Y)), MathF.Max(MathF.Abs(pa.Y - pd.Y), MathF.Abs(pb.Y - pc.Y)));
                if (rise > s * slopeLimit) continue;
                if (drop && MathF.Max(MathF.Max(pa.Y, pb.Y), MathF.Max(pc.Y, pd.Y)) <= lowest + 0.5f) continue;
                if (corners.Count + 2 > 0x7FFF) break;
                ushort a = V(i, j), b = V(i + 1, j), c = V(i + 1, j + 1), d = V(i, j + 1);
                corners.Add([a, c, b]);
                corners.Add([a, d, c]);
            }
        if (corners.Count == 0) throw new InvalidOperationException("there is no ground flat enough to walk on");
        // Bits of ground walkable on their own (a few squares between steep ones) are left out: each piece is
        // an island, a cell keeps its island's number in a byte, and the AI can't reach them anyway.
        Vector3[] vertices;
        (corners, vertices) = KeepBigPieces(corners, kept, MinPieceCells, MostPieces);

        // Each cell joined to the one across each of its edges.
        var across = new Dictionary<(int, int), (int Cell, int Edge)>();
        var adjacent = corners.Select(_ => new[] { -1, -1, -1 }).ToArray();
        for (int cell = 0; cell < corners.Count; cell++)
            for (int k = 0; k < 3; k++)
            {
                int p = corners[cell][k], q = corners[cell][(k + 1) % 3];
                var key = (Math.Min(p, q), Math.Max(p, q));
                if (across.Remove(key, out var other))
                {
                    adjacent[cell][k] = other.Cell;
                    adjacent[other.Cell][other.Edge] = cell;
                }
                else across[key] = (cell, k);
            }

        // The open edges on the zone's border are its outer edges (where the next zone's mesh would join).
        const float Near = 0.01f;
        bool OnBorder(Vector3 p, Vector3 q) =>
            (MathF.Abs(p.X - zoneCorner.X) < Near && MathF.Abs(q.X - zoneCorner.X) < Near) ||
            (MathF.Abs(p.X - zoneCorner.X - zoneWidth) < Near && MathF.Abs(q.X - zoneCorner.X - zoneWidth) < Near) ||
            (MathF.Abs(p.Z - zoneCorner.Y) < Near && MathF.Abs(q.Z - zoneCorner.Y) < Near) ||
            (MathF.Abs(p.Z - zoneCorner.Y - zoneWidth) < Near && MathF.Abs(q.Z - zoneCorner.Y - zoneWidth) < Near);
        var outer = across.Values.Where(e => OnBorder(vertices[corners[e.Cell][e.Edge]], vertices[corners[e.Cell][(e.Edge + 1) % 3]]))
                                  .OrderBy(e => e.Cell).ThenBy(e => e.Edge)
                                  .Select(e => new NavigationEdge((ushort)e.Cell, (byte)e.Edge, 0, uint.MaxValue)).ToArray();

        byte flags = vehicles ? (byte)0x20 : (byte)0;
        var cells = corners.Select((v, i) => new NavigationCell(adjacent[i], 0, 0, v, 0xFFFF, 0, place, flags, 0, place)).ToArray();
        byte[] islands = NavigationLayer.SwitchedOnIslands(cells);
        cells = cells.Select((c, i) => c with { Island = islands[i] }).ToArray();
        var (words, pools) = NavigationLayer.PlainPools(islands.Max() + 1, place, people: !vehicles);
        float yLow = vertices.Length > 0 ? vertices.Min(v => v.Y) : 0, yHigh = vertices.Length > 0 ? vertices.Max(v => v.Y) : 0;
        var min = new Vector3(x0, yLow, z0);
        var max = new Vector3(x0 + 2 * half, yHigh, z0 + 2 * half);
        var layer = new NavigationLayer(NavigationLayer.ArmageddonVersion, vertices, cells, 0, outer, min, max, words, pools);

        var boxes = corners.Select(v =>
        {
            Vector3 a = vertices[v[0]], b = vertices[v[1]], c = vertices[v[2]];
            return (Min: Vector3.Min(a, Vector3.Min(b, c)), Max: Vector3.Max(a, Vector3.Max(b, c)));
        }).ToArray();
        return (layer, Mopp(boxes, min, max));
    }

    /// <summary>A MOPP block over these boxes (each found by its number): see the class.</summary>
    public static byte[] Mopp(IReadOnlyList<(Vector3 Min, Vector3 Max)> boxes, Vector3 min, Vector3 max)
    {
        Vector3 offset = min - new Vector3(0.05f);
        Vector3 size = max - min;
        float longest = MathF.Max(size.X, MathF.Max(size.Y, size.Z));
        float scale = 16777216f / ((longest + 0.1f) * 128f / 127f);
        int Q(float value, float from) => Math.Clamp((int)MathF.Floor((value - from) * scale), 0, 0xFFFFFF) >> 16;
        int[] Lo(Vector3 v) => [Q(v.X, offset.X), Q(v.Y, offset.Y), Q(v.Z, offset.Z)];
        var lows = boxes.Select(b => Lo(b.Min)).ToArray();
        var highs = boxes.Select(b => Lo(b.Max)).ToArray();

        var code = new List<byte>();
        Build(Enumerable.Range(0, boxes.Count).ToList(), code, lows, highs);

        var output = new MemoryStream();
        var w = new BinaryWriter(output);
        w.Write(0x50504F4Du);  // "MOPP"
        w.Write(0x12Du);
        w.Write(offset.X);
        w.Write(offset.Y);
        w.Write(offset.Z);
        w.Write(scale);
        w.Write(code.Count);
        w.Write(1u);
        w.Write(code.ToArray());
        return output.ToArray();
    }

    static void Build(List<int> these, List<byte> code, int[][] lows, int[][] highs)
    {
        if (these.Count == 1)
        {
            int cell = these[0];
            for (int axis = 0; axis < 3; axis++)
            {
                code.Add((byte)(0x26 + axis));
                code.Add((byte)Math.Max(0, lows[cell][axis] - 1));
                code.Add((byte)Math.Min(255, highs[cell][axis] + 1));
            }
            if (cell < 32) code.Add((byte)(0x30 + cell));
            else if (cell < 0x100) { code.Add(0x50); code.Add((byte)cell); }
            else if (cell < 0x10000) { code.Add(0x51); code.Add((byte)(cell >> 8)); code.Add((byte)cell); }
            else { code.Add(0x52); code.Add((byte)(cell >> 16)); code.Add((byte)(cell >> 8)); code.Add((byte)cell); }
            return;
        }
        // Split along the axis the middles spread most along, half each side.
        int best = 0;
        float spread = -1;
        for (int axis = 0; axis < 3; axis++)
        {
            int lo = these.Min(c => lows[c][axis] + highs[c][axis]), hi = these.Max(c => lows[c][axis] + highs[c][axis]);
            if (hi - lo > spread) { spread = hi - lo; best = axis; }
        }
        int ax = best;
        var sorted = these.OrderBy(c => lows[c][ax] + highs[c][ax]).ThenBy(c => c).ToList();
        var left = sorted.Take(sorted.Count / 2).ToList();
        var right = sorted.Skip(sorted.Count / 2).ToList();
        int leftHighest = Math.Min(255, left.Max(c => highs[c][ax]) + 1), rightLowest = Math.Max(0, right.Min(c => lows[c][ax]) - 1);

        var leftCode = new List<byte>();
        Build(left, leftCode, lows, highs);
        if (leftCode.Count > 0xFFFFFF) throw new InvalidOperationException("a navigation tree's half is too big to jump over");
        // left if the box's lowest ≤ leftHighest (on to pc + 7 + 4: past the jump); right if its highest ≥ rightLowest (pc + 7 + 0: the jump).
        code.AddRange([(byte)(0x23 + ax), (byte)leftHighest, (byte)rightLowest, 0x00, 0x04, 0x00, 0x00]);
        code.AddRange([0x07, (byte)(leftCode.Count >> 16), (byte)(leftCode.Count >> 8), (byte)leftCode.Count]);
        code.AddRange(leftCode);
        Build(right, code, lows, highs);
    }
}