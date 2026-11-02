using System.Numerics;

namespace RfaFormats.Meshes;

/// <summary>
/// A zone's navigation file (.navdata_pc): the meshes its people walk and its vehicles drive on, as
/// Red Faction: Guerrilla kept them in its terrain files (terrain_pf_data, bb_pfg_layer) and
/// Armageddon in a file of their own. Little-endian; offsets in the header and the block are from the
/// file's start, offsets in a layer from the layer's start:
///   header (0x34 bytes): the file's size, 2, a stamp (Volition's tools wrote the time, which the game
///     doesn't read), 0, 0, the block's offset (0x34), then 0 × 7
///   the block (44 bytes): 1, its size (from its start to the end of the vehicles' tree), the people's
///     layer's offset and size, its tree's offset and size, the vehicles' layer and tree likewise, 0.
///     A layer of size 0 is none: the zone has no navigation for it (every zone needs the file and the
///     block, or the game won't finish loading it)
///   each layer, followed by its tree; then the vehicles' layer, at a multiple of 4
///   each tree: a Havok MOPP block over the layer's cells ("MOPP", 0x12D, the box's corner less 0.05,
///     the scale, the code's size, 1, the code), whose keys are the cells' numbers
/// A layer ("FRPF"; see <see cref="NavigationLayer"/>): +0x00 the header of Guerrilla's bb_pfg_layer
/// (signature, version, vertices, cells, the cell heap, outer edges, box), +0x3C twelve words about the
/// pools after it (<see cref="NavigationLayer.PoolWords"/>), +0x6C the vertices, the cells (36 bytes
/// each, Guerrilla's 32 and four more; then spare slots, zeros, for cells the game adds), the heap (4
/// bytes a cell slot, zeros), the outer edges (8 bytes) and the pools. Checked against every file in
/// the game (119, the multiplayer and DLC maps' too): each reads and writes back byte for byte.
/// </summary>
public sealed record NavigationFile(uint Stamp, NavigationLayer? People, byte[]? PeopleTree, NavigationLayer? Vehicles, byte[]? VehiclesTree)
{
    const int HeaderSize = 0x34, BlockSize = 44;

    /// <summary>A file with no navigation (the zone's people and vehicles have none).</summary>
    public static NavigationFile None { get; } = new(0, null, null, null, null);

    public static NavigationFile Read(byte[] file)
    {
        uint U(int at) => BitConverter.ToUInt32(file, at);
        if (U(0) != file.Length || U(4) != 2 || U(0x14) != HeaderSize)
            throw new InvalidDataException("not a navigation file (its size, 2 and 0x34 first)");
        int block = HeaderSize;
        if (U(block) != 1) throw new InvalidDataException("the navigation block isn't version 1");
        (NavigationLayer?, byte[]?) Layer(int at)
        {
            int offset = (int)U(at), size = (int)U(at + 4), tree = (int)U(at + 8), treeSize = (int)U(at + 12);
            if (size == 0) return (null, null);
            return (NavigationLayer.Read(file[offset..(offset + size)]), file[tree..(tree + treeSize)]);
        }
        var (people, peopleTree) = Layer(block + 8);
        var (vehicles, vehiclesTree) = Layer(block + 24);
        return new NavigationFile(U(8), people, peopleTree, vehicles, vehiclesTree);
    }

    public byte[] ToBytes()
    {
        var output = new MemoryStream();
        var w = new BinaryWriter(output);
        w.Write(new byte[HeaderSize + BlockSize]);
        var places = new int[8];
        void Pad() { while (output.Length % 4 != 0) w.Write((byte)0); }
        void Part(int index, byte[]? bytes)
        {
            if (bytes == null) return;
            places[index] = (int)output.Length;
            places[index + 1] = bytes.Length;
            w.Write(bytes);
        }
        Part(0, People?.ToBytes());
        Part(2, PeopleTree);
        if (Vehicles != null) Pad();
        Part(4, Vehicles?.ToBytes());
        Part(6, VehiclesTree);
        int end = (int)output.Length;
        Pad();
        byte[] file = output.ToArray();
        void Put(int at, int value) => BitConverter.TryWriteBytes(file.AsSpan(at), value);
        Put(0, file.Length);
        Put(4, 2);
        BitConverter.TryWriteBytes(file.AsSpan(8), Stamp);
        Put(0x14, HeaderSize);
        Put(HeaderSize, 1);
        Put(HeaderSize + 4, end - HeaderSize);
        for (int k = 0; k < 8; k++) Put(HeaderSize + 8 + k * 4, places[k]);
        return file;
    }
}

/// <summary>
/// One of a zone's navigation layers (the people's or the vehicles'): a mesh of triangular cells over
/// the vertices, each cell joined to the cells across its edges, and the outer edges where it meets
/// the next zone's. Pieces of it joined to nothing else are islands, numbered by their lowest cell;
/// each cell keeps its island's number, and the pools keep one record an island.
/// </summary>
/// <param name="Version">0x00040001 in all of Armageddon's (Guerrilla's: 0x00020000).</param>
/// <param name="SpareCells">Zeroed cell slots after the cells (and as many more heap slots), room for cells the game adds.</param>
/// <param name="PoolWords">
/// The twelve words at +0x3C, with the four offsets (words 1, 3, 5 and 10; 0xFFFFFFFF none) from the
/// pools' start: pool A's count and offset (52-byte records), the islands' count and offset (48 bytes
/// each), pool C's count and offset (20 bytes), its free records and a word of heads, pool D's count
/// and free records (12 bytes), its offset, and a last word. See <see cref="PlainPools"/>.
/// </param>
/// <param name="Pools">The pools' bytes, from the outer edges' end to the layer's.</param>
public sealed record NavigationLayer(uint Version, Vector3[] Vertices, NavigationCell[] Cells, int SpareCells, NavigationEdge[] OuterEdges,
                                     Vector3 BoxMin, Vector3 BoxMax, uint[] PoolWords, byte[] Pools)
{
    public const uint Signature = 0x46505246;  // "FRPF"
    public const uint ArmageddonVersion = 0x00040001;
    public const int CellSize = 36;
    const int HeaderSize = 0x6C;
    static readonly int[] PoolOffsetWords = [1, 3, 5, 10];

    public static NavigationLayer Read(byte[] d)
    {
        uint U(int at) => BitConverter.ToUInt32(d, at);
        if (U(0) != Signature) throw new InvalidDataException("not a navigation layer (FRPF)");
        int vertexCount = (int)U(8), vertices = (int)U(0xC), cellCount = (int)U(0x10), cells = (int)U(0x14), heap = (int)U(0x18),
            edgeCount = (int)U(0x1C), edges = (int)U(0x20);
        if (vertices != HeaderSize || cells != vertices + vertexCount * 12 || (heap - cells) % CellSize != 0)
            throw new InvalidDataException("the navigation layer's vertices and cells aren't where they should be");
        int slots = (heap - cells) / CellSize;
        if (edges != heap + slots * 4) throw new InvalidDataException("the navigation layer's heap isn't one slot a cell");
        Vector3 V(int at) => new(BitConverter.ToSingle(d, at), BitConverter.ToSingle(d, at + 4), BitConverter.ToSingle(d, at + 8));
        var cellList = new NavigationCell[cellCount];
        for (int i = 0; i < cellCount; i++) cellList[i] = NavigationCell.Read(d, cells + i * CellSize, cells);
        int pools = edges + edgeCount * 8;
        uint[] words = new uint[12];
        for (int k = 0; k < 12; k++) words[k] = U(0x3C + k * 4);
        foreach (int k in PoolOffsetWords)
            if (words[k] != uint.MaxValue) words[k] -= (uint)pools;
        return new NavigationLayer(U(4), [.. Enumerable.Range(0, vertexCount).Select(i => V(vertices + i * 12))], cellList, slots - cellCount,
            [.. Enumerable.Range(0, edgeCount).Select(i => NavigationEdge.Read(d, edges + i * 8))], V(0x24), V(0x30), words, d[pools..]);
    }

    public byte[] ToBytes()
    {
        int cells = HeaderSize + Vertices.Length * 12, slots = Cells.Length + SpareCells, heap = cells + slots * CellSize,
            edges = heap + slots * 4, pools = edges + OuterEdges.Length * 8;
        byte[] d = new byte[pools + Pools.Length];
        void Put(int at, uint value) => BitConverter.TryWriteBytes(d.AsSpan(at), value);
        void PutVector(int at, Vector3 v) { BitConverter.TryWriteBytes(d.AsSpan(at), v.X); BitConverter.TryWriteBytes(d.AsSpan(at + 4), v.Y); BitConverter.TryWriteBytes(d.AsSpan(at + 8), v.Z); }
        Put(0, Signature);
        Put(4, Version);
        Put(8, (uint)Vertices.Length);
        Put(0xC, HeaderSize);
        Put(0x10, (uint)Cells.Length);
        Put(0x14, (uint)cells);
        Put(0x18, (uint)heap);
        Put(0x1C, (uint)OuterEdges.Length);
        Put(0x20, (uint)edges);
        PutVector(0x24, BoxMin);
        PutVector(0x30, BoxMax);
        for (int k = 0; k < 12; k++)
            Put(0x3C + k * 4, PoolOffsetWords.Contains(k) && PoolWords[k] != uint.MaxValue ? PoolWords[k] + (uint)pools : PoolWords[k]);
        for (int i = 0; i < Vertices.Length; i++) PutVector(HeaderSize + i * 12, Vertices[i]);
        for (int i = 0; i < Cells.Length; i++) Cells[i].Write(d.AsSpan(cells + i * CellSize, CellSize), cells);
        for (int i = 0; i < OuterEdges.Length; i++) OuterEdges[i].Write(d.AsSpan(edges + i * 8, 8));
        Pools.CopyTo(d, pools);
        return d;
    }

    /// <summary>
    /// The pools as Volition's tools wrote them for a layer with nothing computed in them (39 of the
    /// campaign world's 77 zones): for the people's layer pool A's one empty record (nine words of
    /// 0xFFFFFFFF, then 5 and zeros), a record an island (its number, the zone's place as column ×
    /// 16 + row, zeros, then 0xFFFF and 0), pool C's 20 free records in a ring (0xFFFFFFFF, 0,
    /// 0xFFFFFFFF, 0xFFFFFFFF, the previous and next record's numbers) and pool D's 40 (0xFFFFFFFF,
    /// 0x0000FFFF, the previous and next); for the vehicles' layer the islands' records alone.
    /// </summary>
    public static (uint[] Words, byte[] Pools) PlainPools(int islands, byte place, bool people)
    {
        const int a = 52, island = 48, c = 20, cCount = 20, dCount = 40;
        var output = new MemoryStream();
        var w = new BinaryWriter(output);
        if (people)
        {
            for (int k = 0; k < 9; k++) w.Write(uint.MaxValue);
            w.Write(5u);
            w.Write(new byte[a - 40]);
        }
        for (int k = 0; k < islands; k++)
        {
            w.Write((byte)k);
            w.Write(place);
            w.Write(new byte[island - 6]);
            w.Write(ushort.MaxValue);
            w.Write((ushort)0);
        }
        if (!people) return ([0, uint.MaxValue, (uint)islands, 0, 0, uint.MaxValue, 0, uint.MaxValue, 0, 0, uint.MaxValue, 0x0000FFFF], output.ToArray());
        for (int k = 0; k < cCount; k++)
        {
            w.Write(uint.MaxValue);
            w.Write(0u);
            w.Write(uint.MaxValue);
            w.Write(uint.MaxValue);
            w.Write((ushort)((k + cCount - 1) % cCount));
            w.Write((ushort)((k + 1) % cCount));
        }
        for (int k = 0; k < dCount; k++)
        {
            w.Write(uint.MaxValue);
            w.Write(0x0000FFFFu);
            w.Write((ushort)((k + dCount - 1) % dCount));
            w.Write((ushort)((k + 1) % dCount));
        }
        uint islandsAt = a, cAt = (uint)(a + islands * island), dAt = (uint)(cAt + cCount * c);
        return ([1, 0, (uint)islands, islandsAt, cCount, cAt, cCount, 0xFFFF0000, dCount, dCount, dAt, 0], output.ToArray());
    }

    /// <summary>The cells' islands: each piece of cells joined across their edges, numbered by its lowest cell.</summary>
    public static byte[] Islands(IReadOnlyList<NavigationCell> cells)
    {
        var island = new int[cells.Count];
        Array.Fill(island, -1);
        int next = 0;
        var stack = new Stack<int>();
        for (int start = 0; start < cells.Count; start++)
        {
            if (island[start] >= 0) continue;
            island[start] = next;
            stack.Push(start);
            while (stack.Count > 0)
                foreach (int a in cells[stack.Pop()].Adjacent)
                    if (a >= 0 && island[a] < 0)
                    {
                        island[a] = next;
                        stack.Push(a);
                    }
            next++;
        }
        if (next > 256) throw new InvalidDataException($"a navigation layer has {next} islands; a cell keeps its island's number in a byte");
        return [.. island.Select(i => (byte)i)];
    }

    /// <summary>
    /// The people's layer's islands as the game checks them when a level loads ("Isolated
    /// sub-islands found! This will break pathfinding!"): within an island, the cells no mission has
    /// switched off (flag 0x08 clear) must be one piece, joined across their edges. Each piece of
    /// switched-on cells is an island; a switched-off cell joins the piece nearest it across the mesh
    /// (cells none reaches make islands of their own); numbered by their lowest cell. Where switched-off
    /// cells split nothing (every people's layer in the game) the same as <see cref="Islands"/>. The
    /// game doesn't check the vehicles' layer, and in some of its own a switched-off road splits an
    /// island, so that keeps <see cref="Islands"/>.
    /// </summary>
    public static byte[] SwitchedOnIslands(IReadOnlyList<NavigationCell> cells)
    {
        const byte Off = 0x08;
        var piece = new int[cells.Count];
        Array.Fill(piece, -1);
        int pieces = 0;
        var queue = new Queue<int>();
        // Each piece of switched-on cells.
        for (int start = 0; start < cells.Count; start++)
        {
            if (piece[start] >= 0 || (cells[start].Flags & Off) != 0) continue;
            piece[start] = pieces;
            var stack = new Stack<int>([start]);
            while (stack.Count > 0)
                foreach (int a in cells[stack.Pop()].Adjacent)
                    if (a >= 0 && piece[a] < 0 && (cells[a].Flags & Off) == 0)
                    {
                        piece[a] = pieces;
                        stack.Push(a);
                    }
            pieces++;
        }
        // Switched-off cells join the nearest piece, spreading from all of them at once.
        for (int i = 0; i < cells.Count; i++)
            if (piece[i] >= 0) queue.Enqueue(i);
        while (queue.Count > 0)
        {
            int c = queue.Dequeue();
            foreach (int a in cells[c].Adjacent)
                if (a >= 0 && piece[a] < 0)
                {
                    piece[a] = piece[c];
                    queue.Enqueue(a);
                }
        }
        // Switched-off cells no switched-on cell reaches: pieces of their own.
        for (int start = 0; start < cells.Count; start++)
        {
            if (piece[start] >= 0) continue;
            piece[start] = pieces;
            queue.Enqueue(start);
            while (queue.Count > 0)
                foreach (int a in cells[queue.Dequeue()].Adjacent)
                    if (a >= 0 && piece[a] < 0)
                    {
                        piece[a] = pieces;
                        queue.Enqueue(a);
                    }
            pieces++;
        }
        // Numbered by their lowest cell.
        var number = new Dictionary<int, int>();
        foreach (int p in piece) number.TryAdd(p, number.Count);
        if (number.Count > 256) throw new InvalidDataException($"a navigation layer has {number.Count} islands; a cell keeps its island's number in a byte");
        return [.. piece.Select(p => (byte)number[p])];
    }
}

/// <summary>
/// A navigation cell (36 bytes): the cells across its three edges (-1 none; the file keeps the layer
/// offset of their records), two words the game fills (a navpoint, a search's number), its three
/// vertices, a hull reference (0xFFFF), a heap index, its zone (row × 16 + column), its flags (0x08
/// off until a mission turns it on, with 0x10 a stronger off, both tested where the game finds a cell
/// to stand in; 0x20 the vehicles' layer; 0x04 the game sets on cells it makes impassable), and
/// Armageddon's four more bytes: its island, its zone as column × 16 + row, and 0xFF, 0.
/// </summary>
public sealed record NavigationCell(int[] Adjacent, uint Navpoint, uint HeapSession, ushort[] Vertices, ushort Hull, ushort HeapData,
                                    byte Zone, byte Flags, byte Island, byte Place, byte Extra0 = 0xFF, byte Extra1 = 0)
{
    /// <summary>A cell from its record at <paramref name="at"/>, its neighbours found among the records from <paramref name="cellsAt"/>, each <paramref name="size"/> bytes.</summary>
    internal static NavigationCell Read(byte[] d, int at, int cellsAt, int size = NavigationLayer.CellSize)
    {
        int Adjacent(int k)
        {
            int offset = BitConverter.ToInt32(d, at + k * 4);
            if (offset == -1) return -1;
            if ((offset - cellsAt) % size != 0) throw new InvalidDataException("a navigation cell's neighbour isn't a cell's record");
            return (offset - cellsAt) / size;
        }
        bool armageddon = size == NavigationLayer.CellSize;
        return new NavigationCell([Adjacent(0), Adjacent(1), Adjacent(2)], BitConverter.ToUInt32(d, at + 12), BitConverter.ToUInt32(d, at + 16),
            [BitConverter.ToUInt16(d, at + 20), BitConverter.ToUInt16(d, at + 22), BitConverter.ToUInt16(d, at + 24)],
            BitConverter.ToUInt16(d, at + 26), BitConverter.ToUInt16(d, at + 28), d[at + 30], d[at + 31],
            armageddon ? d[at + 32] : (byte)0, armageddon ? d[at + 33] : (byte)0, armageddon ? d[at + 34] : (byte)0xFF, armageddon ? d[at + 35] : (byte)0);
    }

    internal void Write(Span<byte> r, int cellsAt)
    {
        for (int k = 0; k < 3; k++)
            BitConverter.TryWriteBytes(r[(k * 4)..], Adjacent[k] < 0 ? -1 : cellsAt + Adjacent[k] * NavigationLayer.CellSize);
        BitConverter.TryWriteBytes(r[12..], Navpoint);
        BitConverter.TryWriteBytes(r[16..], HeapSession);
        for (int k = 0; k < 3; k++) BitConverter.TryWriteBytes(r[(20 + k * 2)..], Vertices[k]);
        BitConverter.TryWriteBytes(r[26..], Hull);
        BitConverter.TryWriteBytes(r[28..], HeapData);
        r[30] = Zone;
        r[31] = Flags;
        r[32] = Island;
        r[33] = Place;
        r[34] = Extra0;
        r[35] = Extra1;
    }
}

/// <summary>An outer edge (8 bytes): a cell, which of its edges, a byte, and the next zone it meets (0xFFFFFFFF; the game fills it).</summary>
public sealed record NavigationEdge(ushort Cell, byte Edge, byte Pad, uint Zone)
{
    internal static NavigationEdge Read(byte[] d, int at) => new(BitConverter.ToUInt16(d, at), d[at + 2], d[at + 3], BitConverter.ToUInt32(d, at + 4));

    internal void Write(Span<byte> r)
    {
        BitConverter.TryWriteBytes(r, Cell);
        r[2] = Edge;
        r[3] = Pad;
        BitConverter.TryWriteBytes(r[4..], Zone);
    }
}
