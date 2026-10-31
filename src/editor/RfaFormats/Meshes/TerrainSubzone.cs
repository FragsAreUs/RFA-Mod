using System.Numerics;
using System.Text;

namespace RfaFormats.Meshes;

/// <summary>
/// The detailed ground of one ninth of a zone: a terrain sub-zone (.ctmesh_pc + .gtmesh_pc, "SUBZ"
/// version 42). A zone's sub-zones are named after it, world_08_02_0 to world_08_02_8 (number =
/// row × 3 + column, rows from the zone's lowest z), each in its own small container.
///
/// Layout of the c-file (found by reading the files; every sub-zone in the game reads this way):
///   0x00  "SUBZ", version 42, sub-zone number, a checksum
///   0x10  a count of names, the size of the name block, then the NUL-separated names (such as
///         "ter_mag_mis14_lava"; usually none), up to the next multiple of 4. From there (B):
///   B     1, sub-zone number, origin x (float), 0, origin z: where its vertex grid starts
///   B+14  the number of patches (0: the sub-zone has no ground, and no mesh), then 3 zeros
///   B+24  bounding box: x, y, z from, x, y, z to; then its centre
///   then  the patches, 96 bytes each (one per submesh): position, rotation (9 floats), number,
///         a box around the position, a bounding sphere (centre, radius), 0. Their vertices are
///         shared with their neighbours, so they aren't needed to draw the ground.
///   then  the version-9 mesh block (see <see cref="ModelFile"/>), one submesh per patch, its
///         buffers in the g-file.
///
/// Vertices are 8 bytes (format 24): the height (int16, in 1/64 m), the grid step along x (u8),
/// the grid step along z (u8, stored with 128 added, wrapping round), and the normal (4 bytes, as
/// in models). A grid step is 255/511 m: a zone is 255 m wide and sampled 512 times across.
/// </summary>
public sealed class TerrainSubzone
{
    public const int Version = 42;

    /// <summary>The distance between two terrain samples: 255/511 m.</summary>
    public const float GridStep = 255f / 511f;

    public int Number { get; }
    public Vector2 Origin { get; }       // x and z where the vertex grid starts
    public int PatchCount { get; }

    /// <summary>False for a sub-zone without ground (caves, mostly): it has no triangles.</summary>
    public bool HasGround => Triangles.Length > 0;
    public Vector3 BoundsMin { get; }
    public Vector3 BoundsMax { get; }
    public Vector3[] Positions { get; }
    public Vector3[] Normals { get; }

    /// <summary>Every triangle of the ground: three vertex numbers each.</summary>
    public int[] Triangles { get; }

    TerrainSubzone(byte[] header, byte[] data)
    {
        if (header.Length < 0x60 || Encoding.ASCII.GetString(header, 0, 4) != "SUBZ")
            throw new InvalidDataException("not a terrain sub-zone (no SUBZ)");
        int version = BitConverter.ToInt32(header, 4);
        if (version != Version) throw new NotSupportedException($"terrain sub-zone version {version}");
        Number = BitConverter.ToInt32(header, 8);
        int fields = (0x18 + BitConverter.ToInt32(header, 0x14) + 3) & ~3;  // after the name block (B)
        if (fields + 0x48 > header.Length || BitConverter.ToInt32(header, fields + 4) != Number)
            throw new InvalidDataException("the sub-zone's header isn't laid out as expected");
        Origin = new Vector2(F32(header, fields + 0x08), F32(header, fields + 0x10));
        PatchCount = BitConverter.ToInt32(header, fields + 0x14);
        BoundsMin = new Vector3(F32(header, fields + 0x24), F32(header, fields + 0x28), F32(header, fields + 0x2C));
        BoundsMax = new Vector3(F32(header, fields + 0x30), F32(header, fields + 0x34), F32(header, fields + 0x38));
        if (PatchCount == 0)
        {
            Positions = Normals = [];
            Triangles = [];
            return;
        }

        // The mesh block follows the patches; it is found by its version and matching checksums.
        int at = Enumerable.Range((fields + 0x48) / 4, (header.Length - fields - 0x48) / 4).Select(i => i * 4)
                           .FirstOrDefault(a => ModelFile.IsMeshBlock(header, a), -1);
        if (at < 0) throw new InvalidDataException("no version-9 mesh block");
        int submeshCount = (int)U32(header, at + 0x14);
        int submeshTable = at + (int)U32(header, at + 0x18);
        int vertexCount = (int)U32(header, at + 0x1C);
        int vertexSize = header[at + 0x20];
        int vertexFormat = header[at + 0x21];
        int vertexOffset = (int)U32(header, at + 0x3C);
        int indexCount = (int)U32(header, at + 0x40);
        int indexOffset = (int)U32(header, at + 0x44);
        int indexSize = header[at + 0x48];
        int topology = header[at + 0x49];
        int blockCount = BitConverter.ToUInt16(header, at + 0x4A);
        if (vertexFormat != 24 || vertexSize != 8) throw new NotSupportedException($"terrain vertex format {vertexFormat} ({vertexSize} bytes)");
        if (indexSize != 2) throw new NotSupportedException($"{indexSize}-byte terrain indices");

        Positions = new Vector3[vertexCount];
        Normals = new Vector3[vertexCount];
        for (int v = 0; v < vertexCount; v++)
        {
            int p = vertexOffset + v * vertexSize;
            int xStep = data[p + 2], zStep = (data[p + 3] + 128) & 0xFF;
            Positions[v] = new Vector3(Origin.X + xStep * GridStep, BitConverter.ToInt16(data, p) / 64f, Origin.Y + zStep * GridStep);
            Normals[v] = Vector3.Normalize(new Vector3(data[p + 4], data[p + 5], data[p + 6]) / 127.5f - Vector3.One);
        }

        var indices = new int[indexCount];
        for (int i = 0; i < indexCount; i++) indices[i] = BitConverter.ToUInt16(data, indexOffset + i * 2);
        var triangles = new List<int>();
        int blockTable = submeshTable + submeshCount * 48;
        for (int b = 0; b < blockCount; b++)
        {
            int r = blockTable + b * 20;
            int first = (int)U32(header, r + 4), count = (int)U32(header, r + 8);
            triangles.AddRange(topology == 0 ? ModelFile.StripToTriangles(indices, first, count) : ModelFile.ListTriangles(indices, first, count));
        }
        Triangles = [.. triangles];
    }

    public static TerrainSubzone Read(byte[] header, byte[] data) => new(header, data);

    /// <summary>The name of the sub-zone file pair's halves: world_08_02_6.ctmesh_pc and world_08_02_6.gtmesh_pc.</summary>
    public static string HeaderFileName(string zone, int number) => $"{zone}_{number}.ctmesh_pc";
    public static string DataFileName(string zone, int number) => $"{zone}_{number}.gtmesh_pc";

    static uint U32(byte[] bytes, int at) => BitConverter.ToUInt32(bytes, at);
    static float F32(byte[] bytes, int at) => BitConverter.ToSingle(bytes, at);
}
