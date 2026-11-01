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
    /// </summary>
    /// <param name="treeCode">The Havok MOPP code; see <see cref="BoxTreeCode"/>.</param>
    public static byte[] Write(IReadOnlyList<StitchPieceToWrite> pieces, Vector3 zoneMiddle, Vector3 treeFrom, float treeSize, byte[] treeCode)
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
    /// The MOPP code of a tree whose one leaf is piece <paramref name="piece"/>, its whole frame: the
    /// frame bounded on its three axes (0x28 z, 0x27 y, 0x26 x, each from 0 to 255 of the frame), then
    /// the piece (0x30 + n, for n under 32). The code the game's one-piece lists have.
    /// </summary>
    public static byte[] BoxTreeCode(int piece = 0)
    {
        if (piece is < 0 or >= 32) throw new ArgumentOutOfRangeException(nameof(piece));
        return [0x28, 0x00, 0xFF, 0x27, 0x00, 0xFF, 0x26, 0x00, 0xFF, (byte)(0x30 + piece)];
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
