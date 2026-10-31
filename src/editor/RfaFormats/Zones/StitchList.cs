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
