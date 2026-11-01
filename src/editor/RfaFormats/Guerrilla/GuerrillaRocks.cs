using System.Numerics;
using System.Text;
using System.Text.RegularExpressions;
using RfaFormats.Meshes;
using RfaFormats.Textures;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's rocks: the stitch pieces its zones place (caps, covers and clumps of
/// rock), and where.
///
/// A Guerrilla piece (.cstch_pc + .gstch_pc) is a destruction chunk of version 55 (Armageddon's are
/// 62) with a version-6 mesh block at the c-file's 0x10: +0x10 submesh count, +0x14 submesh table,
/// +0x18 vertex count, +0x1C vertex size, format and UV channels, +0x20 vertex offset, +0x24 index
/// count, +0x28 index offset, +0x2C index size, topology and render block count; submeshes 44 bytes,
/// render blocks 20. Its vertices (32 bytes) are Armageddon's format 2 (24 bytes: position, normal,
/// tangent, UV) with two more UV channels; the first is the texture's. The indices are strips, as
/// Armageddon's topology 0. Both games' g-files: the checksum and 12 zeros, the indices from 16, the
/// vertices from the next multiple of 16, the checksum.
///
/// The piece is made over one of Armageddon's own, its boulder ter_sur_blder (dlc01_precache.vpp_pc:
/// version 62, one rock material, two submeshes, one general object), with what changes: the mesh
/// block's counts and offsets, both submeshes' boxes (the far-away copy is the piece itself again),
/// both render blocks, the general object's box, the material's colour and normal map names (written
/// over in place, so the new names must be no longer: short aliases), its tint (Guerrilla's
/// Diffuse_Color, on the same scale as Guerrilla's ground layer tints) and its detail map (a white
/// one of ours, which the boulder's shader, ir_detail_nospec, multiplying the colour by
/// lerp(1, detail, k), leaves as it is). Its collision is Guerrilla's own collision mesh for the
/// piece, written into the template's Havok 2010 collision with a tree of ours over its triangles
/// (<see cref="PieceCollision"/>); the zone's stitch list's tree reaches every piece.
/// </summary>
public static class GuerrillaRocks
{
    /// <summary>The Armageddon piece the rocks are made over, in dlc01_precache.vpp_pc.</summary>
    public const string TemplatePiece = "ter_sur_blder";

    /// <summary>A converted piece: its files, the box its mesh fills (in its own coordinates), its Guerrilla texture names (with .tga), its tint, the box its collision fills, and its triangles (three corners each, in its own coordinates).</summary>
    public sealed record Piece(byte[] Header, byte[] Data, Vector3 Min, Vector3 Max, string Diffuse, string Normal, Vector4 Tint, Vector3 CollisionMin, Vector3 CollisionMax, Vector3[] Corners);

    /// <summary>A piece a Guerrilla zone places: its name, where (world position), turned how (rows right, up, forward), the area it covers from above.</summary>
    public sealed record Placement(string Name, Vector3 Position, float[] Rotation, Vector2 AreaMin, Vector2 AreaMax);

    /// <summary>The boulder's detail map (an ice rock's), and the plain white one the rocks get in its place.</summary>
    public const string TemplateDetailTexture = "ter_ice_rock_dtlonly_di", WhiteTexture = "rfg_white";

    const int TemplateObjectBox = 0x504;   // the boulder's general object box (checked against its first submesh's)
    const uint DiffuseColor = 0x03E0B860;  // "Diffuse_Color"

    /// <summary>
    /// A Guerrilla piece made into an Armageddon one over the template (ter_sur_blder's files), its
    /// textures renamed by <paramref name="textureAlias"/> (a name without ".tga" to a short one).
    /// </summary>
    public static Piece Convert(byte[] templateHeader, byte[] templateData, byte[] rfgHeader, byte[] rfgData, Func<string, string> textureAlias)
    {
        static uint U32(byte[] d, int a) => BitConverter.ToUInt32(d, a);
        int H = (int)U32(rfgHeader, 0x10);
        if (U32(rfgHeader, 0) != 0xB0CEEFA5 || U32(rfgHeader, H) != 6) throw new InvalidDataException("not a Guerrilla piece (version 55, version-6 mesh block)");
        int submeshes = (int)U32(rfgHeader, H + 0x10), table = H + (int)U32(rfgHeader, H + 0x14), vertexCount = (int)U32(rfgHeader, H + 0x18);
        int vertexSize = rfgHeader[H + 0x1C], vertexOffset = (int)U32(rfgHeader, H + 0x20), indexOffset = (int)U32(rfgHeader, H + 0x28);
        int blocks = BitConverter.ToUInt16(rfgHeader, H + 0x2E);
        if (rfgHeader[H + 0x1D] != 2 || vertexSize < 24 || rfgHeader[H + 0x2C] != 2 || rfgHeader[H + 0x2D] != 0)
            throw new NotSupportedException($"vertex format {rfgHeader[H + 0x1D]} ({vertexSize} bytes), {rfgHeader[H + 0x2C]}-byte indices, topology {rfgHeader[H + 0x2D]}");
        int blockTable = table + submeshes * 44;
        // Every render block's strip, joined into one by repeated indices (each block's first triangle
        // kept at an even place, so it faces as before).
        var strip = new List<ushort>();
        for (int b = 0; b < blocks; b++)
        {
            int first = (int)U32(rfgHeader, blockTable + b * 20 + 4), count = (int)U32(rfgHeader, blockTable + b * 20 + 8);
            ushort At(int i) => BitConverter.ToUInt16(rfgData, indexOffset + (first + i) * 2);
            if (count < 3) continue;
            if (strip.Count > 0)
            {
                strip.Add(strip[^1]);
                strip.Add(At(0));
                if (strip.Count % 2 == 1) strip.Add(At(0));
            }
            for (int i = 0; i < count; i++) strip.Add(At(i));
        }
        var vertices = new byte[vertexCount * 24];
        var positions = new Vector3[vertexCount];
        Vector3 min = new(float.MaxValue), max = new(float.MinValue);
        for (int v = 0; v < vertexCount; v++)
        {
            int from = vertexOffset + v * vertexSize;
            Buffer.BlockCopy(rfgData, from, vertices, v * 24, 24);  // position, normal, tangent, the first UV channel
            var p = positions[v] = new Vector3(BitConverter.ToSingle(rfgData, from), BitConverter.ToSingle(rfgData, from + 4), BitConverter.ToSingle(rfgData, from + 8));
            min = Vector3.Min(min, p);
            max = Vector3.Max(max, p);
        }
        var corners = ModelFile.StripToTriangles([.. strip.Select(i => (int)i)], 0, strip.Count).Select(i => positions[i]).ToArray();

        // Its textures: the colour and normal maps (names ending _d and _n).
        var names = Regex.Matches(Encoding.Latin1.GetString(rfgHeader), @"[A-Za-z0-9_]+\.tga").Select(m => m.Value).ToList();
        string diffuse = names.FirstOrDefault(n => n.EndsWith("_d.tga", StringComparison.OrdinalIgnoreCase)) ?? throw new InvalidDataException("the Guerrilla piece has no colour map");
        string normal = names.FirstOrDefault(n => n.EndsWith("_n.tga", StringComparison.OrdinalIgnoreCase)) ?? throw new InvalidDataException("the Guerrilla piece has no normal map");

        // The g-file.
        var data = new MemoryStream();
        var g = new BinaryWriter(data);
        uint checksum = U32(templateData, 0);
        g.Write(checksum); g.Write(0); g.Write(0); g.Write(0);
        foreach (ushort i in strip) g.Write(i);
        while (data.Length % 16 != 0) g.Write((byte)0);
        int newVertexOffset = (int)data.Length;
        g.Write(vertices);
        g.Write(checksum);
        byte[] dataBytes = data.ToArray();

        // The c-file: the template's, its mesh block, object box and material changed.
        byte[] header = (byte[])templateHeader.Clone();
        int M = (int)U32(header, 0x10);
        int templateSubmeshes = (int)U32(header, M + 0x14), templateTable = M + (int)U32(header, M + 0x18);
        int templateBlocks = BitConverter.ToUInt16(header, M + 0x4A);
        if (templateSubmeshes != 2 || templateBlocks != 2 || header[M + 0x20] != 24 || header[M + 0x21] != 2)
            throw new InvalidDataException("the template isn't a two-submesh, format-2 piece");
        for (int k = 0; k < 6; k++)
            if (MathF.Abs(BitConverter.ToSingle(header, TemplateObjectBox + k * 4) - BitConverter.ToSingle(header, templateTable + 16 + k * 4)) > 0.01f)
                throw new InvalidDataException("the template's object box isn't where it was");
        void Put(int at, int value) => BitConverter.TryWriteBytes(header.AsSpan(at), value);
        void PutF(int at, float value) => BitConverter.TryWriteBytes(header.AsSpan(at), value);
        Put(M + 0x0C, dataBytes.Length);
        Put(M + 0x1C, vertexCount);
        Put(M + 0x3C, newVertexOffset);
        Put(M + 0x40, strip.Count);
        Put(M + 0x44, 16);
        float[] box = [min.X, min.Y, min.Z, max.X, max.Y, max.Z];
        for (int s = 0; s < 2; s++)
        {
            int sm = templateTable + s * 48;
            PutF(sm + 4, 0); PutF(sm + 8, 0); PutF(sm + 12, 0);
            for (int k = 0; k < 6; k++) PutF(sm + 16 + k * 4, box[k]);
            int rb = templateTable + 2 * 48 + s * 20;  // its material word kept
            Put(rb + 4, 0); Put(rb + 8, strip.Count); Put(rb + 12, 0); Put(rb + 16, vertexCount - 1);
        }
        for (int k = 0; k < 6; k++) PutF(TemplateObjectBox + k * 4, box[k]);
        Rename(header, TemplatePiece + "_di.tga", textureAlias(Path.GetFileNameWithoutExtension(diffuse)) + ".tga");
        Rename(header, TemplatePiece + "_nm.tga", textureAlias(Path.GetFileNameWithoutExtension(normal)) + ".tga");
        Rename(header, TemplateDetailTexture + ".tga", WhiteTexture + ".tga");
        var tint = MaterialBlocks.GuerrillaConstant(rfgHeader, DiffuseColor) ?? throw new InvalidDataException("the Guerrilla piece has no Diffuse_Color");
        if (MaterialBlocks.SetConstant(header, DiffuseColor, tint) != 1) throw new InvalidDataException("the template's Diffuse_Color wasn't found");

        // Its collision: Guerrilla's own collision mesh, in the template's Havok 2010 layout.
        var (collisionVertices, collisionTriangles) = PieceCollision.Read(rfgHeader);
        header = PieceCollision.Replace(header, collisionVertices, collisionTriangles);
        var collisionMin = collisionVertices.Aggregate(new Vector3(float.MaxValue), Vector3.Min);
        var collisionMax = collisionVertices.Aggregate(new Vector3(float.MinValue), Vector3.Max);
        return new Piece(header, dataBytes, min, max, diffuse, normal, tint, collisionMin, collisionMax, corners);
    }

    static void Rename(byte[] header, string oldName, string newName)
    {
        if (MaterialBlocks.ReplaceName(header, oldName, newName) == 0) throw new InvalidDataException($"no {oldName} in the template piece");
    }

    /// <summary>The white detail map's files, made from nothing: 64 × 64, white all over, DXT1 with its levels down to 4 × 4, a colour map.</summary>
    public static (string Name, byte[] Data)[] WhiteTextureFiles() => GuerrillaBuiltIns.Flat(WhiteTexture, [255, 255, 255, 255], 0);

    /// <summary>
    /// The stitch pieces a Guerrilla zone's terrain file (TERR version 31) places. After the texture
    /// names (from 0x24, 0x0C bytes) come the piece names (0x18 bytes, from the next multiple of 4:
    /// 6 zones' texture names end between, e.g. terr01_05_05's at 0x192), then 20 bytes a placement
    /// (0x20 of them): the area it covers seen from above (x and z from and to) and where its name starts.
    /// Later in the file, 56 bytes a placement in the same order: position, rotation (9 floats), −1,
    /// where its name starts; found by their name offsets matching the first list's.
    /// </summary>
    public static List<Placement> Placements(byte[] terrain)
    {
        static float F(byte[] d, int a) => BitConverter.ToSingle(d, a);
        if (Encoding.ASCII.GetString(terrain, 0, 4) != "TERR" || BitConverter.ToInt32(terrain, 4) != 31) throw new InvalidDataException("not a Guerrilla terrain file (TERR version 31)");
        int textureSize = BitConverter.ToInt32(terrain, 0x0C), nameSize = BitConverter.ToInt32(terrain, 0x18), count = BitConverter.ToInt32(terrain, 0x20);
        int names = (0x24 + textureSize + 3) & ~3, areas = names + nameSize;
        var result = new List<Placement>();
        if (count == 0) return result;
        int[] nameAt = Enumerable.Range(0, count).Select(k => BitConverter.ToInt32(terrain, areas + k * 20 + 16)).ToArray();
        bool RecordAt(int p, int k) => BitConverter.ToInt32(terrain, p + 48) == -1 && BitConverter.ToInt32(terrain, p + 52) == nameAt[k] &&
                                       Enumerable.Range(0, 3).All(r => MathF.Abs(new Vector3(F(terrain, p + 12 + r * 12), F(terrain, p + 16 + r * 12), F(terrain, p + 20 + r * 12)).Length() - 1) < 0.01f);
        int start = -1;
        for (int p = areas + count * 20; p + count * 56 <= terrain.Length && start < 0; p += 2)
            if (RecordAt(p, 0) && Enumerable.Range(1, count - 1).All(k => RecordAt(p + k * 56, k))) start = p;
        if (start < 0) throw new InvalidDataException($"the {count} placements' positions weren't found");
        for (int k = 0; k < count; k++)
        {
            int p = start + k * 56, a = areas + k * 20;
            result.Add(new Placement(MaterialBlocks.CString(terrain, names + nameAt[k]),
                new Vector3(F(terrain, p), F(terrain, p + 4), F(terrain, p + 8)), Enumerable.Range(0, 9).Select(r => F(terrain, p + 12 + r * 4)).ToArray(),
                new Vector2(F(terrain, a), F(terrain, a + 4)), new Vector2(F(terrain, a + 8), F(terrain, a + 12))));
        }
        return result;
    }
}
