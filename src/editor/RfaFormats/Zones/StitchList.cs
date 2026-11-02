using System.Numerics;
using System.Text;

namespace RfaFormats.Zones;

/// <summary>
/// A zone's stitch list (.stchlst_pc, "STCH" version 11): where the zone's stitch pieces and
/// decals go. Stitch pieces (.cstch_pc / .gstch_pc pairs, read with <see cref="Meshes.ModelFile"/>)
/// are the fixed pieces the levels are built from: rock walls, cave ceilings, ledges, mountains,
/// roads, bridges, the Terraformer. Decals are pictures laid onto surfaces (trash, blast marks,
/// blood). Each zone's container holds its stitch list next to its .rfgzone_pc and the pieces it uses.
///
/// Layout (found by reading the files; all 119 in the game read this way):
///   0x00  "STCH", version 11, size of the texture name block, texture count
///   0x10  the texture names (the decals' textures), then up to the next multiple of 4
///   then  a 0x110-byte header of counts: +0x24 + 4 × n the number of pieces in list n, +0x6C + 4 × n
///         the number of decals after list n (the other counts are for parts not read yet)
///   then  nine lists, one per sub-zone (see <see cref="Meshes.TerrainSubzone"/>), each: the size of
///         its name block, the NUL-separated piece names (up to the next multiple of 4), 100 bytes
///         per piece, then 132 bytes per decal.
///   A piece:  +0x00 where its name starts in the list's name block, +0x04 position (3 floats),
///             +0x10 rotation (9 floats; rows: right, up, forward, as in a zone's "oP"), +0x34 flags
///             (0 or 4), +0x38 -1, +0x3C 0, +0x40 1, +0x44 the area it covers seen from above (x and
///             z from, x and z to), +0x54 run-time values
///   A decal:  +0x00 position, +0x0C rotation, +0x30 size (width, depth it reaches through along its
///             up direction, length), +0x3C and +0x40 where its colour and specular textures' names
///             start in the texture name block, +0x44 two values not known yet, +0x4C opacity,
///             +0x50 colour (3 floats), then zeros
///   After the ninth list there is more, not read yet.
/// </summary>
public sealed class StitchList
{
    public const int Version = 11;
    public const int ListCount = 9;
    const int HeaderSize = 0x110;
    const int PieceSize = 100;
    const int DecalSize = 132;

    public IReadOnlyList<StitchPiece> Pieces { get; }
    public IReadOnlyList<StitchDecal> Decals { get; }

    StitchList(List<StitchPiece> pieces, List<StitchDecal> decals)
    {
        Pieces = pieces;
        Decals = decals;
    }

    /// <summary>
    /// A stitch list (no decals) from nothing: these pieces in their lists, and one collision tree,
    /// as the game's loader (0x6B8C09) reads them:
    ///   "STCH", 11, no texture names, the 0x110-byte header of counts (pieces in list n at +0x24 + 4n,
    ///   all else 0); each list: its name block's size (exact), the names, NUL-ended, each once, to a
    ///   multiple of 4, then 100 bytes a piece: where its name starts, position, rotation (rows right,
    ///   up, forward), flags (4 on most of the game's pieces), −1, 0 (where its words are, set at
    ///   load), 1 (how many words it has), its area seen from above (x, z from; x, z to), 16 zero
    ///   bytes (run-time);
    ///   then a word a piece (0), the tree count N = 1, then N zeros, N × −1, N zeros and N half-words
    ///   (0), zeros to a multiple of 16 in the file (the game looks for the tree there and stops on
    ///   "Trying to load invalid MOPP code" if it isn't), then the tree: "MOPP", 0x12D, its frame (from
    ///   x, y, z, x and z from <paramref name="zoneMiddle"/>, y as it is; then 2^24 / its size), the
    ///   code's size, 0, and the code padded with 0xCD to 16 bytes.
    /// With no tree (no pieces), the tree count 0 and nothing after it: the loader skips the trees then
    /// (it checks for none at 0x6B8D10), though every list in the game has at least one piece.
    /// </summary>
    /// <param name="treeCode">The Havok MOPP code (see <see cref="BoxTreeCode"/>); null for no tree.</param>
    public static byte[] Write(IReadOnlyList<StitchPieceToWrite> pieces, Vector3 zoneMiddle, Vector3 treeFrom, float treeSize, byte[]? treeCode)
    {
        var stream = new MemoryStream();
        var w = new BinaryWriter(stream);
        w.Write(Encoding.ASCII.GetBytes("STCH")); w.Write(Version); w.Write(0); w.Write(0);
        var lists = Enumerable.Range(0, ListCount).Select(n => pieces.Where(p => p.List == n).ToList()).ToArray();
        byte[] counts = new byte[HeaderSize];
        for (int list = 0; list < ListCount; list++) BitConverter.TryWriteBytes(counts.AsSpan(0x24 + list * 4), lists[list].Count);
        w.Write(counts);
        foreach (var list in lists)
        {
            var starts = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
            var names = new MemoryStream();
            foreach (var piece in list.Where(p => !starts.ContainsKey(p.Name)))
            {
                starts[piece.Name] = (int)names.Length;
                names.Write(Encoding.ASCII.GetBytes(piece.Name + "\0"));
            }
            w.Write((int)names.Length);
            w.Write(names.ToArray());
            w.Write(new byte[Align4((int)names.Length) - (int)names.Length]);
            foreach (var p in list)
            {
                if (p.Rotation.Length != 9) throw new ArgumentException($"{p.Name}'s rotation isn't 9 numbers");
                w.Write(starts[p.Name]);
                w.Write(p.Position.X); w.Write(p.Position.Y); w.Write(p.Position.Z);
                foreach (float r in p.Rotation) w.Write(r);
                w.Write(p.Flags); w.Write(-1); w.Write(0); w.Write(1);
                w.Write(p.AreaMin.X); w.Write(p.AreaMin.Y); w.Write(p.AreaMax.X); w.Write(p.AreaMax.Y);
                w.Write(new byte[16]);
            }
        }
        foreach (var _ in pieces) w.Write(0);
        if (treeCode == null)
        {
            w.Write(0);
            return stream.ToArray();
        }
        w.Write(1); w.Write(0); w.Write(-1); w.Write(0); w.Write((ushort)0);
        while (stream.Length % 16 != 0) w.Write((byte)0);
        w.Write(Encoding.ASCII.GetBytes("MOPP")); w.Write(0x12D);
        w.Write(treeFrom.X - zoneMiddle.X); w.Write(treeFrom.Y); w.Write(treeFrom.Z - zoneMiddle.Z); w.Write(16777216f / treeSize);
        int padded = (treeCode.Length + 15) & ~15;
        w.Write(padded); w.Write(0); w.Write(treeCode);
        for (int i = treeCode.Length; i < padded; i++) w.Write((byte)0xCD);
        return stream.ToArray();
    }

    /// <summary>
    /// A stitch list of these pieces whose collision tree reaches every one of them: <paramref name="boxes"/>
    /// are the pieces' collision boxes in the world, in the same order as <paramref name="pieces"/>. The
    /// tree's leaves follow the pieces as written (list by list), its frame the boxes with a metre to spare.
    /// No pieces: an empty list with no tree.
    /// </summary>
    public static byte[] Write(IReadOnlyList<StitchPieceToWrite> pieces, Vector3 zoneMiddle, IReadOnlyList<(Vector3 Min, Vector3 Max)> boxes)
    {
        if (boxes.Count != pieces.Count) throw new ArgumentException("a box for every piece");
        if (pieces.Count == 0) return Write(pieces, zoneMiddle, Vector3.Zero, 0, null);
        var order = Enumerable.Range(0, pieces.Count).OrderBy(i => pieces[i].List).ToList();  // stable: as Write lays them out
        var written = order.Select(i => boxes[i]).ToList();
        var min = new Vector3(written.Min(b => b.Min.X), written.Min(b => b.Min.Y), written.Min(b => b.Min.Z)) - Vector3.One;
        var max = new Vector3(written.Max(b => b.Max.X), written.Max(b => b.Max.Y), written.Max(b => b.Max.Z)) + Vector3.One;
        float size = MathF.Max(max.X - min.X, MathF.Max(max.Y - min.Y, max.Z - min.Z));
        return Write(pieces, zoneMiddle, min, size, TreeCode(written, min, size));
    }

    /// <summary>A box round a piece's own box (<paramref name="min"/> to <paramref name="max"/>) placed in the world: turned by <paramref name="rotation"/> (rows right, up, forward) and moved to <paramref name="position"/>.</summary>
    public static (Vector3 Min, Vector3 Max) PlacedBox(Vector3 min, Vector3 max, IReadOnlyList<float> rotation, Vector3 position)
    {
        Vector3 right = new(rotation[0], rotation[1], rotation[2]), up = new(rotation[3], rotation[4], rotation[5]), forward = new(rotation[6], rotation[7], rotation[8]);
        Vector3 low = new(float.MaxValue), high = new(float.MinValue);
        for (int k = 0; k < 8; k++)
        {
            var corner = position + ((k & 1) == 0 ? min.X : max.X) * right + ((k & 2) == 0 ? min.Y : max.Y) * up + ((k & 4) == 0 ? min.Z : max.Z) * forward;
            low = Vector3.Min(low, corner);
            high = Vector3.Max(high, corner);
        }
        return (low, high);
    }

    /// <summary>
    /// The MOPP code of a tree whose one leaf is piece <paramref name="piece"/>, its whole frame: the
    /// frame bounded on its three axes (0x28 z, 0x27 y, 0x26 x, each from 0 to 255 of the frame), then
    /// the piece (0x30 + n, for n under 32). The code the game's one-piece lists have.
    /// </summary>
    public static byte[] BoxTreeCode(int piece = 0)
    {
        if (piece is < 0 or >= 32) throw new ArgumentOutOfRangeException(nameof(piece));
        return [0x28, 0x00, 0xFF, 0x27, 0x00, 0xFF, 0x26, 0x00, 0xFF, (byte)(0x30 + piece)];
    }

    /// <summary>
    /// The MOPP code of a tree over pieces with these boxes (in the world), within the frame from
    /// <paramref name="from"/>, <paramref name="size"/> across on every axis: piece n is leaf n (its place
    /// in the list's order, lists 0 to 8). The tree's language, as the game's 119 stitch lists read
    /// (every piece of their single-tree lists lies in the box its leaf is reached in):
    ///   0x26/0x27/0x28 lo hi: go on only if x / y / z, as a byte of the frame (256 across), is in lo..hi;
    ///   0x10-0x12 a b j: split on x / y / z: what is at most a goes on, what is at least b jumps j
    ///   bytes on (from the end of the command); 0x20-0x22 c j: the same at one cut; 0x23-0x25 a b j k:
    ///   the same with two 16-bit jumps (big-endian), j to the low side and k to the high side;
    ///   0x13-0x1C: splits on diagonal axes; 0x29-0x2B: bound checks with 24-bit values; 0x01-0x04 x y z:
    ///   zoom 2^n into the sub-box starting at those bytes; 0x09 n / 0x0A n n: add to the leaf number
    ///   (on this path); 0x70 and 4 bytes: jump to that offset in the code (big-endian; the game's long
    ///   codes are cut into 16-byte-aligned chunks padded with 0xCD); 0x30 + n (n under 32), 0x50 n:
    ///   leaf n; 0x51 n n: leaf n, 16-bit big-endian (a piece's own tree reaches triangle 357 of 386 as
    ///   51 01 65).
    /// This writes the plainest of it: halves split on the axis the pieces' middles spread most along
    /// (0x23-0x25, the low half straight after), and each piece's box (0x28, 0x27, 0x26, as the game's
    /// one-piece trees) before its leaf. Bytes are a 256th of the frame, so boxes are rounded outwards
    /// to that; no zooming. The same code serves a piece's own collision, its leaves its triangles. A
    /// tree too long for 16-bit jumps that way (Guerrilla's statues' 8,000-odd triangles) is written
    /// without the leaves' own boxes, only the splits narrowing each down: about half as long, and as
    /// right (what a tree finds is tested against the shapes themselves), only less choosy.
    /// </summary>
    public static byte[] TreeCode(IReadOnlyList<(Vector3 Min, Vector3 Max)> boxes, Vector3 from, float size)
    {
        if (boxes.Count is 0 or > 65536) throw new ArgumentException($"{boxes.Count} leaves: a tree holds 1 to 65,536");
        return TreeCode(boxes, from, size, leafBoxes: true) ?? TreeCode(boxes, from, size, leafBoxes: false)
            ?? throw new InvalidOperationException("the tree's code is too long for 16-bit jumps");
    }

    static byte[]? TreeCode(IReadOnlyList<(Vector3 Min, Vector3 Max)> boxes, Vector3 from, float size, bool leafBoxes)
    {
        byte Low(float v, float o) => (byte)Math.Clamp((int)MathF.Floor((v - o) * 256 / size), 0, 255);
        byte High(float v, float o) => (byte)Math.Clamp((int)MathF.Floor((v - o) * 256 / size), 0, 255);
        var lo = boxes.Select(b => new[] { Low(b.Min.X, from.X), Low(b.Min.Y, from.Y), Low(b.Min.Z, from.Z) }).ToArray();
        var hi = boxes.Select(b => new[] { High(b.Max.X, from.X), High(b.Max.Y, from.Y), High(b.Max.Z, from.Z) }).ToArray();

        var code = new List<byte>();
        bool tooLong = false;
        void Leaf(int n)
        {
            if (leafBoxes) code.AddRange([0x28, lo[n][2], hi[n][2], 0x27, lo[n][1], hi[n][1], 0x26, lo[n][0], hi[n][0]]);
            code.AddRange(n < 32 ? [(byte)(0x30 + n)] : n < 256 ? [0x50, (byte)n] : [0x51, (byte)(n >> 8), (byte)n]);
        }
        void Node(List<int> pieces)
        {
            if (pieces.Count == 1) { Leaf(pieces[0]); return; }
            int axis = Enumerable.Range(0, 3).MaxBy(a => pieces.Max(n => lo[n][a] + hi[n][a]) - pieces.Min(n => lo[n][a] + hi[n][a]));
            var sorted = pieces.OrderBy(n => lo[n][axis] + hi[n][axis]).ToList();
            var low = sorted.Take(sorted.Count / 2).ToList();
            var high = sorted.Skip(sorted.Count / 2).ToList();
            int at = code.Count;
            code.AddRange([(byte)(0x23 + axis), (byte)low.Max(n => hi[n][axis]), (byte)high.Min(n => lo[n][axis]), 0, 0, 0, 0]);
            int lowStart = code.Count;
            Node(low);
            int jump = code.Count - lowStart;
            if (jump > ushort.MaxValue) tooLong = true;
            code[at + 5] = (byte)(jump >> 8);
            code[at + 6] = (byte)jump;
            Node(high);
        }
        Node([.. Enumerable.Range(0, boxes.Count)]);
        return tooLong ? null : [.. code];
    }

    public static StitchList Read(byte[] data)
    {
        if (data.Length < 0x10 || Encoding.ASCII.GetString(data, 0, 4) != "STCH")
            throw new InvalidDataException("not a stitch list (no STCH)");
        int version = BitConverter.ToInt32(data, 4);
        if (version != Version) throw new NotSupportedException($"stitch list version {version}");

        int textureBytes = BitConverter.ToInt32(data, 8);
        int header = Align4(0x10 + textureBytes);
        int at = header + HeaderSize;
        var pieces = new List<StitchPiece>();
        var decals = new List<StitchDecal>();
        for (int list = 0; list < ListCount; list++)
        {
            int pieceCount = BitConverter.ToInt32(data, header + 0x24 + list * 4);
            int decalCount = BitConverter.ToInt32(data, header + 0x6C + list * 4);
            int nameBytes = BitConverter.ToInt32(data, at);
            int names = at + 4;
            at = Align4(names + nameBytes);
            if (at + pieceCount * PieceSize + decalCount * DecalSize > data.Length) throw new InvalidDataException("the stitch list ends early");

            for (int i = 0; i < pieceCount; i++, at += PieceSize)
            {
                int nameAt = BitConverter.ToInt32(data, at);
                if (nameAt < 0 || nameAt >= nameBytes) throw new InvalidDataException($"a stitch piece's name is outside the name block ({nameAt})");
                pieces.Add(new StitchPiece(
                    Name: CString(data, names + nameAt),
                    List: list,
                    Position: Vector(data, at + 0x04),
                    Rotation: Rotation(data, at + 0x10),
                    AreaMin: new Vector2(F32(data, at + 0x44), F32(data, at + 0x48)),
                    AreaMax: new Vector2(F32(data, at + 0x4C), F32(data, at + 0x50))));
            }

            for (int i = 0; i < decalCount; i++, at += DecalSize)
            {
                int colourAt = BitConverter.ToInt32(data, at + 0x3C), specularAt = BitConverter.ToInt32(data, at + 0x40);
                if (colourAt < 0 || colourAt >= textureBytes || specularAt < 0 || specularAt >= textureBytes)
                    throw new InvalidDataException("a decal's texture is outside the texture name block");
                decals.Add(new StitchDecal(
                    Texture: CString(data, 0x10 + colourAt),
                    SpecularTexture: CString(data, 0x10 + specularAt),
                    List: list,
                    Position: Vector(data, at),
                    Rotation: Rotation(data, at + 0x0C),
                    Size: Vector(data, at + 0x30),
                    Opacity: F32(data, at + 0x4C),
                    Colour: Vector(data, at + 0x50)));
            }
        }
        return new StitchList(pieces, decals);
    }

    static int Align4(int n) => (n + 3) & ~3;

    static float F32(byte[] data, int at) => BitConverter.ToSingle(data, at);

    static Vector3 Vector(byte[] data, int at) => new(F32(data, at), F32(data, at + 4), F32(data, at + 8));

    static Matrix4x4 Rotation(byte[] data, int at) => new(
        F32(data, at), F32(data, at + 4), F32(data, at + 8), 0,
        F32(data, at + 12), F32(data, at + 16), F32(data, at + 20), 0,
        F32(data, at + 24), F32(data, at + 28), F32(data, at + 32), 0,
        0, 0, 0, 1);

    static string CString(byte[] data, int at)
    {
        int end = Array.IndexOf(data, (byte)0, at);
        return Encoding.ASCII.GetString(data, at, (end < 0 ? data.Length : end) - at);
    }
}

/// <param name="Name">The piece's model, without extension: "ter_sur_dome" is ter_sur_dome.cstch_pc.</param>
/// <param name="List">Which of the stitch list's nine lists (sub-zones) it is in, 0 to 8.</param>
/// <param name="Rotation">Rows: right, up and forward directions.</param>
/// <param name="AreaMin">The area it covers seen from above: x and z.</param>
public sealed record StitchPiece(string Name, int List, Vector3 Position, Matrix4x4 Rotation, Vector2 AreaMin, Vector2 AreaMax);

/// <param name="Texture">Its colour texture, e.g. "prp_dec_trash_di.tga".</param>
/// <param name="Rotation">Rows: right, up and forward; the decal lies across right and forward, and reaches along up.</param>
/// <param name="Size">Width (along right), depth (along up) and length (along forward), in metres.</param>
public sealed record StitchDecal(string Texture, string SpecularTexture, int List, Vector3 Position, Matrix4x4 Rotation,
                                 Vector3 Size, float Opacity, Vector3 Colour);

/// <summary>A piece for <see cref="StitchList.Write"/>.</summary>
/// <param name="Name">The piece's model, without extension.</param>
/// <param name="List">Its list (the sub-zone it stands in: column + 3 × row of the zone's thirds, from its lowest x and z), 0 to 8.</param>
/// <param name="Position">Where, in the world.</param>
/// <param name="Rotation">Rows right, up and forward, 9 numbers.</param>
/// <param name="Flags">4 on most of the game's pieces, 0 on the rest (the same piece is placed both ways; meaning not known).</param>
/// <param name="AreaMin">The area it covers seen from above: x and z.</param>
public sealed record StitchPieceToWrite(string Name, int List, Vector3 Position, float[] Rotation, int Flags, Vector2 AreaMin, Vector2 AreaMax)
{
    /// <summary>No turn.</summary>
    public static readonly float[] Unturned = [1, 0, 0, 0, 1, 0, 0, 0, 1];
}
