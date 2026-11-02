using System.Numerics;
using System.Text;
using RfaFormats.Meshes;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's rocks: the stitch pieces its zones place (caps, covers and clumps of
/// rock), and where.
///
/// A Guerrilla piece (.cstch_pc + .gstch_pc) is a chunk file of version 55 laid out as its
/// destructible objects' (one general object, one object id, nothing breakable), so it is converted as
/// they are (see <see cref="GuerrillaChunks"/>): its mesh block (version 6 → 9; the g-file as it is),
/// its material on the nearest of Armageddon's shaders (<see cref="GuerrillaMaterials"/>), and its
/// collision, one triangle mesh under a tree, written from nothing in Armageddon's layout (see
/// <see cref="PieceCollision"/>); the zone's stitch list's tree reaches every piece. Its version-6
/// mesh block (at the c-file's 0x10: +0x10 submesh count, +0x14 submesh table, +0x18 vertex count,
/// +0x1C vertex size, format and UV channels, +0x20 vertex offset, +0x24 index count, +0x28 index
/// offset, +0x2C index size, topology and render block count; submeshes 44 bytes, render blocks 20)
/// gives the piece's box and its triangles, which the ground is laid under where a rock closes it.
/// </summary>
public static class GuerrillaRocks
{
    /// <summary>A converted piece: its files, the box its mesh fills (in its own coordinates), the textures its materials name (Guerrilla's names, without ".tga"), the box its collision fills, and its triangles (three corners each, in its own coordinates).</summary>
    public sealed record Piece(byte[] Header, byte[] Data, Vector3 Min, Vector3 Max, IReadOnlyList<string> Textures, Vector3 CollisionMin, Vector3 CollisionMax, Vector3[] Corners);

    /// <summary>A piece a Guerrilla zone places: its name, where (world position), turned how (rows right, up, forward), the area it covers from above.</summary>
    public sealed record Placement(string Name, Vector3 Position, float[] Rotation, Vector2 AreaMin, Vector2 AreaMax);

    /// <summary>A Guerrilla piece made into an Armageddon one, its textures renamed by <paramref name="textureAlias"/> (a name without ".tga" to a short one).</summary>
    public static Piece Convert(byte[] rfgHeader, byte[] rfgData, Func<string, string> textureAlias)
    {
        static uint U32(byte[] d, int a) => BitConverter.ToUInt32(d, a);
        int H = (int)U32(rfgHeader, 0x10);
        if (U32(rfgHeader, 0) != GuerrillaChunks.Signature || U32(rfgHeader, H) != 6) throw new InvalidDataException("not a Guerrilla piece (version 55, version-6 mesh block)");
        int submeshes = (int)U32(rfgHeader, H + 0x10), table = H + (int)U32(rfgHeader, H + 0x14), vertexCount = (int)U32(rfgHeader, H + 0x18);
        int vertexSize = rfgHeader[H + 0x1C], vertexOffset = (int)U32(rfgHeader, H + 0x20), indexOffset = (int)U32(rfgHeader, H + 0x28);
        int blocks = BitConverter.ToUInt16(rfgHeader, H + 0x2E);
        if (vertexSize < 12 || rfgHeader[H + 0x2C] != 2 || rfgHeader[H + 0x2D] != 0)
            throw new NotSupportedException($"{vertexSize}-byte vertices, {rfgHeader[H + 0x2C]}-byte indices, topology {rfgHeader[H + 0x2D]}");
        // Its box and triangles: every render block's strip, its vertices' positions (the first 12 bytes).
        var positions = new Vector3[vertexCount];
        for (int v = 0; v < vertexCount; v++)
        {
            int from = vertexOffset + v * vertexSize;
            positions[v] = new Vector3(BitConverter.ToSingle(rfgData, from), BitConverter.ToSingle(rfgData, from + 4), BitConverter.ToSingle(rfgData, from + 8));
        }
        var min = positions.Aggregate(new Vector3(float.MaxValue), Vector3.Min);
        var max = positions.Aggregate(new Vector3(float.MinValue), Vector3.Max);
        int blockTable = table + submeshes * 44;
        var corners = new List<Vector3>();
        for (int b = 0; b < blocks; b++)
        {
            int first = (int)U32(rfgHeader, blockTable + b * 20 + 4), count = (int)U32(rfgHeader, blockTable + b * 20 + 8);
            if (count < 3) continue;
            int[] strip = [.. Enumerable.Range(0, count).Select(i => (int)BitConverter.ToUInt16(rfgData, indexOffset + (first + i) * 2))];
            corners.AddRange(ModelFile.StripToTriangles(strip, 0, strip.Length).Select(i => positions[i]));
        }

        var (header, textures) = GuerrillaChunks.Convert(rfgHeader, textureAlias);
        var (collisionVertices, _) = PieceCollision.Read(rfgHeader);
        var collisionMin = collisionVertices.Aggregate(new Vector3(float.MaxValue), Vector3.Min);
        var collisionMax = collisionVertices.Aggregate(new Vector3(float.MinValue), Vector3.Max);
        return new Piece(header, rfgData, min, max, textures, collisionMin, collisionMax, [.. corners]);
    }

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
