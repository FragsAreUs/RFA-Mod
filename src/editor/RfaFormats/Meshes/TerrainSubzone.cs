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
///
/// Red Faction: Guerrilla's sub-zones ("SUBZ" version 31, terr01_07_07_4.ctmesh_pc) are read too.
/// They have no checksum, so the name block starts a word earlier (count at 0x0C, size at 0x10,
/// names from 0x14), and B lacks the leading 1; and they have the version-6 mesh
/// block: as version 9 without the detail-level count and the six floats after the vertex format
/// (submesh count at +0x10, submesh table +0x14, vertex count +0x18, vertex size and format +0x1C,
/// vertex offset +0x20, index count +0x24, index offset +0x28, index size, topology and render
/// block count +0x2C), with 44-byte submeshes (one run-time word instead of two). Vertices are
/// packed the same (format 14), but Guerrilla's zones are 511 m wide, so a grid step is 1 m.
///
/// After the ground's mesh block both games' files go on as their loaders walk them (Guerrilla's
/// terrain_load_subzone): from a multiple of 4 the stitch pieces (104 bytes each: name, skirt
/// count, skirts, stitched mesh count, stitched meshes, position, rotation, a Havok handle), then
/// for each its name (to 4), its skirts (32 bytes each: two run-time words, the skirt's submesh in
/// the stitch mesh, its position, two dead words) and its stitched mesh list (4 bytes each), to 4;
/// a word, and if it isn't 0 the stitch mesh (from 16): the skirts, ground pieces on the 1 m grid
/// around every stitch piece (the rocks, and a pseudo-piece named "_Road"; 16-byte vertices,
/// format 0: position as 3 floats, a normal); then from 4 the road decal meshes' records (40 bytes
/// each: instance count, run-time words, position), and for each, from 16, its mesh (24-byte
/// vertices, Guerrilla's format 25 and Armageddon's 36: position, normal, tangent, UV), a run-time
/// word for each of its instances, its texture name list (from 4: the list's size, the names), its
/// material map (from 16, 16 bytes) and its material (the road shader, ir_terrain_road_layer2 in
/// Armageddon: the ground's layers 0-2, the road texture and the blend map). The counts are in
/// the header's fields (the stitch pieces at +0x5C, the road decals at +0x64), each mesh's g-data
/// after the one before from a multiple of 16, and positions are metres from the record's (the
/// zone's centre). Guerrilla cuts the ground open under its skirts and road decals, and they meet
/// it exactly; with them, what is still uncovered is where the rocks close the ground themselves.
/// Guerrilla's are read into <see cref="Paving"/> and <see cref="RoadDecals"/>.
/// </summary>
public sealed class TerrainSubzone
{
    public const int Version = 42;
    public const int GuerrillaVersion = 31;

    /// <summary>The distance between two terrain samples: 255/511 m.</summary>
    public const float GridStep = 255f / 511f;

    /// <summary>The distance between two of Guerrilla's terrain samples: its zones are 511 m wide, sampled 512 times.</summary>
    public const float GuerrillaGridStep = 1f;

    public int FormatVersion { get; }    // 42, or 31 for Guerrilla's
    public int Number { get; }
    public Vector2 Origin { get; }       // x and z where the vertex grid starts
    public int PatchCount { get; }

    /// <summary>False for a sub-zone without ground (caves, mostly): it has no triangles.</summary>
    public bool HasGround => Triangles.Length > 0;
    public Vector3 BoundsMin { get; }
    public Vector3 BoundsMax { get; }

    /// <summary>
    /// Its grid step: <see cref="GridStep"/>, or 1 m in Guerrilla's and in a world the game gives
    /// 511 m zones (ours of Guerrilla's world: the same version 42, told apart by the box the
    /// ground fills).
    /// </summary>
    public float Step { get; }

    /// <summary>The width of the zone it is a ninth of: 255 m, or 511 m.</summary>
    public float ZoneWidth => Step * 511;

    public Vector3[] Positions { get; }
    public Vector3[] Normals { get; }

    /// <summary>Every triangle of the ground: three vertex numbers each.</summary>
    public int[] Triangles { get; }

    /// <summary>The triangles of each patch (its submesh), in the patches' order.</summary>
    public IReadOnlyList<int[]> PatchTriangles { get; }

    /// <summary>Guerrilla's skirts and road decals in the sub-zone (what fills the ground's cut-outs), three corners a triangle, in the world (empty for Armageddon's).</summary>
    public Vector3[] Paving { get; } = [];

    /// <summary>Guerrilla's road decal meshes in the sub-zone (empty for Armageddon's).</summary>
    public IReadOnlyList<GuerrillaRoadDecal> RoadDecals { get; } = [];

    /// <summary>The sub-zone's road decal meshes as drawn, in both games' (Armageddon lays its roads into its ground's cut-outs as Guerrilla does).</summary>
    public IReadOnlyList<RoadMesh> Roads { get; } = [];

    /// <summary>The road shader's slot for the road's own colour texture (ter_sur_road02_di in world_10_07's), the same in both games.</summary>
    const uint RoadTextureSlot = 0x97AE6CD3;

    TerrainSubzone(byte[] header, byte[] data)
    {
        if (header.Length < 0x60 || Encoding.ASCII.GetString(header, 0, 4) != "SUBZ")
            throw new InvalidDataException("not a terrain sub-zone (no SUBZ)");
        FormatVersion = BitConverter.ToInt32(header, 4);
        if (FormatVersion is not (Version or GuerrillaVersion)) throw new NotSupportedException($"terrain sub-zone version {FormatVersion}");
        bool guerrilla = FormatVersion == GuerrillaVersion;
        Number = BitConverter.ToInt32(header, 8);
        // After the name block (B). Guerrilla's starts a word earlier (no checksum) and B lacks the
        // leading 1, so its fields sit one word before where B + n would put them.
        int fields = guerrilla ? ((0x14 + BitConverter.ToInt32(header, 0x10) + 3) & ~3) - 4
                               : (0x18 + BitConverter.ToInt32(header, 0x14) + 3) & ~3;
        if (fields + 0x48 > header.Length || BitConverter.ToInt32(header, fields + 4) != Number)
            throw new InvalidDataException("the sub-zone's header isn't laid out as expected");
        Origin = new Vector2(F32(header, fields + 0x08), F32(header, fields + 0x10));
        PatchCount = BitConverter.ToInt32(header, fields + 0x14);
        BoundsMin = new Vector3(F32(header, fields + 0x24), F32(header, fields + 0x28), F32(header, fields + 0x2C));
        BoundsMax = new Vector3(F32(header, fields + 0x30), F32(header, fields + 0x34), F32(header, fields + 0x38));
        Step = guerrilla ? GuerrillaGridStep : GridStep;
        if (PatchCount == 0)
        {
            Positions = Normals = [];
            Triangles = [];
            PatchTriangles = [];
            return;
        }

        // The mesh block follows the patches; it is found by its version and matching checksums.
        int at = Enumerable.Range((fields + 0x48) / 4, (header.Length - fields - 0x48) / 4).Select(i => i * 4)
                           .FirstOrDefault(a => guerrilla ? IsGuerrillaMeshBlock(header, a) : ModelFile.IsMeshBlock(header, a), -1);
        if (at < 0) throw new InvalidDataException($"no version-{(guerrilla ? 6 : 9)} mesh block");
        int fieldsAt = guerrilla ? at - 4 : at;  // where version 9's fields would start (it has one more before them)
        int submeshCount = (int)U32(header, fieldsAt + 0x14);
        int submeshTable = at + (int)U32(header, fieldsAt + 0x18);
        int vertexCount = (int)U32(header, fieldsAt + 0x1C);
        int vertexSize = header[fieldsAt + 0x20];
        int vertexFormat = header[fieldsAt + 0x21];
        int afterFloats = guerrilla ? fieldsAt + 0x24 - 0x3C : fieldsAt;  // version 6 hasn't the six floats
        int vertexOffset = (int)U32(header, afterFloats + 0x3C);
        int indexCount = (int)U32(header, afterFloats + 0x40);
        int indexOffset = (int)U32(header, afterFloats + 0x44);
        int indexSize = header[afterFloats + 0x48];
        int topology = header[afterFloats + 0x49];
        int blockCount = BitConverter.ToUInt16(header, afterFloats + 0x4A);
        if (vertexFormat != (guerrilla ? 14 : 24) || vertexSize != 8) throw new NotSupportedException($"terrain vertex format {vertexFormat} ({vertexSize} bytes)");
        if (indexSize != 2) throw new NotSupportedException($"{indexSize}-byte terrain indices");

        if (!guerrilla && vertexCount > 0)
        {
            // The box's width over the grid steps it spans says which step it was written with.
            int fewest = int.MaxValue, most = int.MinValue;
            for (int v = 0; v < vertexCount; v++)
            {
                int xStep = data[vertexOffset + v * vertexSize + 2];
                fewest = Math.Min(fewest, xStep);
                most = Math.Max(most, xStep);
            }
            float each = most > fewest ? (BoundsMax.X - BoundsMin.X) / (most - fewest) : GridStep;
            if (MathF.Abs(each - GuerrillaGridStep) < MathF.Abs(each - GridStep)) Step = GuerrillaGridStep;
        }
        float step = Step;
        Positions = new Vector3[vertexCount];
        Normals = new Vector3[vertexCount];
        for (int v = 0; v < vertexCount; v++)
        {
            int p = vertexOffset + v * vertexSize;
            int xStep = data[p + 2], zStep = (data[p + 3] + 128) & 0xFF;
            Positions[v] = new Vector3(Origin.X + xStep * step, BitConverter.ToInt16(data, p) / 64f, Origin.Y + zStep * step);
            Normals[v] = Vector3.Normalize(new Vector3(data[p + 4], data[p + 5], data[p + 6]) / 127.5f - Vector3.One);
        }

        var indices = new int[indexCount];
        for (int i = 0; i < indexCount; i++) indices[i] = BitConverter.ToUInt16(data, indexOffset + i * 2);
        // Each submesh (a patch) has the next of the render blocks, as many as its first word says.
        int submeshSize = guerrilla ? 44 : 48;
        int blockTable = submeshTable + submeshCount * submeshSize;
        var patches = new List<int[]>();
        for (int s = 0, b = 0; s < submeshCount; s++)
        {
            var triangles = new List<int>();
            for (int end = Math.Min(blockCount, b + (int)U32(header, submeshTable + s * submeshSize)); b < end; b++)
            {
                int r = blockTable + b * 20;
                int first = (int)U32(header, r + 4), count = (int)U32(header, r + 8);
                triangles.AddRange(topology == 0 ? ModelFile.StripToTriangles(indices, first, count) : ModelFile.ListTriangles(indices, first, count));
            }
            patches.Add([.. triangles]);
        }
        PatchTriangles = patches;
        Triangles = [.. patches.SelectMany(p => p)];
        Roads = ReadRoads(header, data, fields, at, guerrilla);
        if (guerrilla) (Paving, RoadDecals) = GuerrillaFill(header, data, fields, at);
    }

    /// <summary>The road decals' meshes (see <see cref="Sections"/>), in the world, each with its road texture's name.</summary>
    static IReadOnlyList<RoadMesh> ReadRoads(byte[] header, byte[] data, int fields, int groundAt, bool guerrilla)
    {
        var roads = new List<RoadMesh>();
        foreach (RoadDecalSections d in Sections(header, fields, groundAt, guerrilla).Decals)
        {
            var origin = new Vector3(F32(header, d.Record + 0x1C), F32(header, d.Record + 0x20), F32(header, d.Record + 0x24));
            var (positions, textureCoordinates, triangles) = ReadMesh(header, data, d.Mesh, d.Data, origin, guerrilla);
            // The material's textures (12 bytes each: where the name starts in the list, the slot's hash, an index) from +0x20 in Guerrilla's, +0x24 in Armageddon's.
            int textures = BitConverter.ToUInt16(header, d.Material + 0x10), first = d.Material + (guerrilla ? 0x20 : 0x24);
            string? roadTexture = null;
            for (int t = 0; t < textures; t++)
                if (U32(header, first + t * 12 + 4) == RoadTextureSlot)
                {
                    int start = d.Names + 4 + (int)U32(header, first + t * 12);
                    roadTexture = Encoding.ASCII.GetString(header, start, Array.IndexOf(header, (byte)0, start) - start);
                }
            roads.Add(new RoadMesh(positions, textureCoordinates, triangles, roadTexture));
        }
        return roads;
    }

    /// <summary>Where a road decal's parts are in a sub-zone's c-file: its record, mesh block, name list (its size first), material map and material; where the material ends; where its g-data starts.</summary>
    internal sealed record RoadDecalSections(int Record, int Mesh, int Names, int Map, int Material, int End, int Data);

    /// <summary>
    /// The sections after the ground's mesh block (at <paramref name="groundAt"/>), walked as the
    /// game's loader walks them (see the class's notes): the stitch mesh (-1 if none) and where its
    /// g-data starts, and the road decals' parts. <paramref name="fields"/> is where version 42's
    /// fields start (Guerrilla's a word before its own).
    /// </summary>
    internal static (int StitchMesh, int StitchMeshData, List<RoadDecalSections> Decals) Sections(byte[] header, int fields, int groundAt, bool guerrilla)
    {
        static int Align(int value, int to) => (value + to - 1) / to * to;
        int I(int o) => BitConverter.ToInt32(header, o);
        int stitchPieces = I(fields + 0x5C), roadDecals = I(fields + 0x64);
        int dataAt = Align(I(groundAt + 0x0C), 16);
        int at = Align(groundAt + I(groundAt + 8), 4);
        int pieces = at;
        at += stitchPieces * 104;
        for (int k = 0; k < stitchPieces; k++)
        {
            int nameEnd = Array.IndexOf(header, (byte)0, at);
            at = Align(nameEnd + 1, 4);
            at += I(pieces + k * 104 + 4) * 32;   // its skirts
            at += I(pieces + k * 104 + 0x0C) * 4;  // its stitched meshes
            at = Align(at, 4);
        }
        int stitchMesh = -1, stitchMeshData = -1;
        if (I(at) != 0)
        {
            stitchMesh = Align(at + 4, 16);
            stitchMeshData = dataAt;
            dataAt = Align(dataAt + I(stitchMesh + 0x0C), 16);
            at = stitchMesh + I(stitchMesh + 8);
        }
        else at += 4;
        at = Align(at, 4);
        int records = at;
        at += roadDecals * 40;
        var decals = new List<RoadDecalSections>();
        for (int k = 0; k < roadDecals; k++)
        {
            int mesh = Align(at, 16);
            // After the mesh, a run-time slot for each of its instances (the record's first word), then from 4 the name list's size and names.
            int names = Align(mesh + I(mesh + 8) + I(records + k * 40) * 4, 4);
            int map = Align(names + 4 + I(names), 16), material = map + 16;
            at = material + I(material);
            decals.Add(new RoadDecalSections(records + k * 40, mesh, names, map, material, at, dataAt));
            dataAt = Align(dataAt + I(mesh + 0x0C), 16);
        }
        if (at > header.Length) throw new InvalidDataException("the sub-zone's stitch and road sections run past its end");
        return (stitchMesh, stitchMeshData, decals);
    }

    /// <summary>
    /// An Armageddon sub-zone's road decals' material maps (16 bytes) and material blocks, in order;
    /// none if it has no road. (world_10_07_2's are the road shader's, ir_terrain_road_layer2.)
    /// </summary>
    public static IReadOnlyList<(byte[] Map, byte[] Material)> RoadMaterials(byte[] header)
    {
        if (BitConverter.ToInt32(header, 4) != Version) throw new NotSupportedException("not an Armageddon terrain sub-zone");
        int fields = (0x18 + BitConverter.ToInt32(header, 0x14) + 3) & ~3;
        int ground = Enumerable.Range((fields + 0x48) / 4, (header.Length - fields - 0x48) / 4).Select(i => i * 4)
                               .FirstOrDefault(a => ModelFile.IsMeshBlock(header, a), -1);
        if (ground < 0) return [];
        return Sections(header, fields, ground, guerrilla: false).Decals.Select(d => (header[d.Map..d.Material], header[d.Material..d.End])).ToList();
    }

    /// <summary>Guerrilla's skirts and road decals after the ground's mesh block (at <paramref name="groundAt"/>): all their triangles' corners in the world, and the road decals.</summary>
    static (Vector3[] Corners, IReadOnlyList<GuerrillaRoadDecal> Decals) GuerrillaFill(byte[] header, byte[] data, int fields, int groundAt)
    {
        // The skirts' positions are from the zone's centre (their records say so), the road decals' from their records'.
        const float zoneWidth = 511;
        Vector3 middle = (BoundsMinOf(header, fields) + BoundsMaxOf(header, fields)) / 2;
        var centre = new Vector3(MathF.Floor(middle.X / zoneWidth) * zoneWidth + zoneWidth / 2, 0, MathF.Floor(middle.Z / zoneWidth) * zoneWidth + zoneWidth / 2);
        var (stitchMesh, stitchMeshData, sections) = Sections(header, fields, groundAt, guerrilla: true);
        var corners = new List<Vector3>();
        if (stitchMesh >= 0) corners.AddRange(MeshCorners(header, data, stitchMesh, stitchMeshData, centre));
        var decals = new List<GuerrillaRoadDecal>();
        foreach (var d in sections)
        {
            var position = new Vector3(F32(header, d.Record + 0x1C), F32(header, d.Record + 0x20), F32(header, d.Record + 0x24));
            var triangles = MeshCorners(header, data, d.Mesh, d.Data, position);
            corners.AddRange(triangles);
            int gSize = (int)U32(header, d.Mesh + 0x0C);
            // The texture names (the list's size, then the names one after another) and the material's slots and constants.
            var names = new List<string>();
            for (int n = d.Names + 4, end = d.Names + 4 + (int)U32(header, d.Names); n < end;)
            {
                int zero = Array.IndexOf(header, (byte)0, n);
                if (zero > n) names.Add(Encoding.ASCII.GetString(header, n, zero - n));
                n = zero + 1;
            }
            int textureCount = BitConverter.ToUInt16(header, d.Material + 0x10), constantCount = header[d.Material + 0x12];
            string NameAt(int offset)
            {
                int start = d.Names + 4 + offset;
                return Encoding.ASCII.GetString(header, start, Array.IndexOf(header, (byte)0, start) - start);
            }
            var slots = Enumerable.Range(0, textureCount).Select(t => (U32(header, d.Material + 0x20 + t * 12 + 4), NameAt((int)U32(header, d.Material + 0x20 + t * 12)))).ToList();
            int hashes = d.Material + 0x20 + textureCount * 12, values = d.Material + ((0x20 + textureCount * 12 + constantCount * 4 + 15) & ~15);
            var constants = new Dictionary<uint, Vector4>();
            for (int c = 0; c < constantCount; c++)
                constants[U32(header, hashes + c * 4)] = new Vector4(F32(header, values + c * 16), F32(header, values + c * 16 + 4), F32(header, values + c * 16 + 8), F32(header, values + c * 16 + 12));
            decals.Add(new GuerrillaRoadDecal(header[d.Record..(d.Record + 40)], position, header[d.Mesh..(d.Mesh + (int)U32(header, d.Mesh + 8))],
                                              data[d.Data..(d.Data + gSize)], names, slots, constants, triangles));
        }
        return ([.. corners], decals);
    }

    static Vector3 BoundsMinOf(byte[] header, int fields) => new(F32(header, fields + 0x24), F32(header, fields + 0x28), F32(header, fields + 0x2C));
    static Vector3 BoundsMaxOf(byte[] header, int fields) => new(F32(header, fields + 0x30), F32(header, fields + 0x34), F32(header, fields + 0x38));

    /// <summary>A version-6 mesh block's triangles' corners, its positions from <paramref name="origin"/>; its g-data from <paramref name="dataAt"/>.</summary>
    static Vector3[] MeshCorners(byte[] header, byte[] data, int at, int dataAt, Vector3 origin)
    {
        var (positions, _, triangles) = ReadMesh(header, data, at, dataAt, origin, version6: true);
        return [.. triangles.Select(v => positions[v])];
    }

    /// <summary>
    /// A version-6 or version-9 mesh block's vertices (positions, 3 floats first in each, from
    /// <paramref name="origin"/>; the first texture coordinates, 2 × 16 bits with 1024 = 1, the
    /// last 4 bytes if it has any) and its triangles (three vertex numbers each); its g-data from
    /// <paramref name="dataAt"/>.
    /// </summary>
    static (Vector3[] Positions, Vector2[] TextureCoordinates, int[] Triangles) ReadMesh(byte[] header, byte[] data, int at, int dataAt, Vector3 origin, bool version6)
    {
        int fieldsAt = version6 ? at - 4 : at;              // where version 9's fields would start (it has one more before them)
        int afterFloats = version6 ? fieldsAt - 0x18 : fieldsAt;  // version 6 hasn't the six floats
        int submeshes = (int)U32(header, fieldsAt + 0x14), table = at + (int)U32(header, fieldsAt + 0x18), vertexCount = (int)U32(header, fieldsAt + 0x1C);
        int vertexSize = header[fieldsAt + 0x20], uvChannels = header[fieldsAt + 0x22];
        int vertexOffset = dataAt + (int)U32(header, afterFloats + 0x3C), indexCount = (int)U32(header, afterFloats + 0x40), indexOffset = dataAt + (int)U32(header, afterFloats + 0x44);
        if (header[afterFloats + 0x48] != 2) throw new NotSupportedException($"{header[afterFloats + 0x48]}-byte indices in a terrain mesh");
        var positions = new Vector3[vertexCount];
        var textureCoordinates = new Vector2[uvChannels > 0 && vertexSize >= 16 ? vertexCount : 0];
        for (int v = 0; v < vertexCount; v++)
        {
            int p = vertexOffset + v * vertexSize;
            positions[v] = origin + new Vector3(F32(data, p), F32(data, p + 4), F32(data, p + 8));
            if (textureCoordinates.Length > 0)
            {
                int uv = p + vertexSize - 4 * uvChannels;
                textureCoordinates[v] = new Vector2(BitConverter.ToInt16(data, uv), BitConverter.ToInt16(data, uv + 2)) / 1024f;
            }
        }
        var indices = new int[indexCount];
        for (int i = 0; i < indexCount; i++) indices[i] = BitConverter.ToUInt16(data, indexOffset + i * 2);
        // Each submesh has the next of the render blocks (20 bytes: a word, first index, count, ...), as many as its first word says.
        int submeshSize = version6 ? 44 : 48, blockTable = table + submeshes * submeshSize;
        var triangles = new List<int>();
        for (int s = 0, b = 0; s < submeshes; s++)
            for (int end = b + (int)U32(header, table + s * submeshSize); b < end; b++)
                triangles.AddRange(ModelFile.StripToTriangles(indices, (int)U32(header, blockTable + b * 20 + 4), (int)U32(header, blockTable + b * 20 + 8)));
        return (positions, textureCoordinates, [.. triangles]);
    }

    public static TerrainSubzone Read(byte[] header, byte[] data) => new(header, data);

    /// <summary>
    /// Writes a sub-zone (version 42) with new ground, keeping all that isn't the ground from an
    /// Armageddon sub-zone with ground (normally the one it replaces): its header up to the
    /// patches (number, checksum, names, and the area the game fills in when loading it), the
    /// checksum that ties its c-file to its g-file, the render blocks' material word and what
    /// follows the mesh block and the vertices. The layout, checked on all 261 of the game's
    /// sub-zones with ground:
    ///   the patches from the template's first, 96 bytes each: the box's middle, no rotation (the
    ///     identity), the patch's number, the box around that middle, a bounding sphere (centre
    ///     0, radius), 0; then zeros up to a multiple of 16
    ///   the mesh block: 0x60 bytes of header (version 9, checksum, block size, g-file size, 1
    ///     detail level, the submesh count, the submesh table at 0x60, the vertex count, vertex
    ///     size 8 and format 24, the template's six floats, the vertex and index offsets, the
    ///     index count, 2-byte indices, strips, the render block count, zeros); a submesh per
    ///     patch (48 bytes: its render block count, pivot 0, the patch's box, two run-time words);
    ///     a render block per patch (20 bytes: material, first index, index count, lowest and
    ///     highest vertex); 8 zero bytes; the checksum again
    ///   the g-file: the checksum and 12 zero bytes, the indices (a strip per patch), zeros up to
    ///     a multiple of 16, the vertices (8 bytes each, as read), then the template's last 4 bytes.
    /// Each patch is one strip (see <see cref="Strip"/> for making one from triangles).
    /// </summary>
    /// <param name="origin">Where the vertex grid starts (x, z).</param>
    /// <param name="patches">Each patch's triangle strip, as the game's: every other triangle turned round, repeated indices joining pieces.</param>
    public static (byte[] Header, byte[] Data) Write(byte[] templateHeader, byte[] templateData, Vector2 origin,
                                                     IReadOnlyList<GroundVertex> vertices, IReadOnlyList<int[]> patches) =>
        Write(templateHeader, templateData, origin, vertices, patches.Select(p => (IReadOnlyList<PatchBlock>)[new PatchBlock(GroundKind, p)]).ToList());

    /// <summary>The kind of render block that draws ground seen from above (the template's material word).</summary>
    public const uint GroundKind = 0;

    /// <summary>
    /// Kinds of render block that draw cliffs, their textures projected from the side (the terrain
    /// file's cliff material, ir_terrain_sidemap_t and _single_t): the game's sub-zones sort a patch's
    /// steep triangles into these by the blend map's cliff weight (red), 3 the least, 5 the most.
    /// </summary>
    public const uint CliffKindLow = 3, CliffKindMixed = 4, CliffKindHigh = 5;

    /// <summary>One render block of a patch: its kind (<see cref="GroundKind"/> or a cliff kind) and its strip.</summary>
    public sealed record PatchBlock(uint Kind, int[] Strip);

    /// <summary>
    /// As <see cref="Write(byte[], byte[], Vector2, IReadOnlyList{GroundVertex}, IReadOnlyList{int[]})"/>,
    /// with each patch drawn by one or more render blocks (as the game's own sub-zones: ground, then
    /// cliff kinds 3, 4 and 5 for its steep triangles). A ground block takes the template's material
    /// word; a cliff block's word is its kind.
    /// </summary>
    public static (byte[] Header, byte[] Data) Write(byte[] templateHeader, byte[] templateData, Vector2 origin,
                                                     IReadOnlyList<GroundVertex> vertices, IReadOnlyList<IReadOnlyList<PatchBlock>> patches,
                                                     float gridStep = GridStep)
    {
        if (vertices.Count is 0 or > 65536) throw new ArgumentException($"{vertices.Count} vertices: a sub-zone holds 1 to 65,536");
        if (patches.Count == 0 || patches.Any(p => p.Count == 0 || p.Any(b => b.Strip.Length < 3))) throw new ArgumentException("every patch needs a strip");
        var template = new TerrainSubzone(templateHeader, templateData);
        if (template.FormatVersion != Version || template.PatchCount == 0) throw new ArgumentException("the template must be an Armageddon sub-zone with ground");
        int fields = (0x18 + BitConverter.ToInt32(templateHeader, 0x14) + 3) & ~3;
        int block = Enumerable.Range((fields + 0x48) / 4, (templateHeader.Length - fields - 0x48) / 4).Select(i => i * 4)
                              .First(a => ModelFile.IsMeshBlock(templateHeader, a));
        int firstPatch = Enumerable.Range((fields + 0x48) / 4, (block - fields - 0x48) / 4).Select(i => i * 4)
                                   .First(a => U32(templateHeader, a + 48) == 0 && F32(templateHeader, a + 12) == 1 && F32(templateHeader, a + 28) == 1 && F32(templateHeader, a + 44) == 1);
        uint checksum = U32(templateHeader, block + 4);
        int templateBlockEnd = block + (int)U32(templateHeader, block + 8);
        byte[] afterBlock = templateHeader[templateBlockEnd..];
        uint material = U32(templateHeader, block + 0x60 + (int)U32(templateHeader, block + 0x14) * 48);
        int templateVertexEnd = (int)U32(templateHeader, block + 0x3C) + (int)U32(templateHeader, block + 0x1C) * 8;
        byte[] dataTail = templateData.Length >= templateVertexEnd + 4 ? templateData[templateVertexEnd..(templateVertexEnd + 4)] : new byte[4];
        return Write(templateHeader[..firstPatch], fields, checksum, material, templateHeader[(block + 0x24)..(block + 0x3C)], afterBlock, dataTail,
                     origin, vertices, patches, gridStep);
    }

    /// <summary>The word at 0x0C of a sub-zone's header: the same, 0x00E798D0, in every one of the game's.</summary>
    const uint HeaderWord = 0x00E798D0;

    /// <summary>The bytes between the header's fields and the first patch: zeros in the files (filled in when the game loads it).</summary>
    const int LoadArea = 0x40C;

    /// <summary>
    /// Writes a sub-zone (version 42) from nothing but its ground, as the game's own are laid out
    /// (checked on ruin01's and world_08_02's): "SUBZ", 42, its number, the header word
    /// (0x00E798D0, the same in all), no names; its fields (1, number, origin, patch count, box and
    /// centre) and 0x40C zero bytes the game fills in; the patches and mesh block as
    /// <see cref="Write(byte[], byte[], Vector2, IReadOnlyList{GroundVertex}, IReadOnlyList{IReadOnlyList{PatchBlock}}, float)"/>
    /// writes them, with the mesh block's six floats 1, 1, 1, 0, 0, 0 (as in every one), the ground
    /// blocks' material word 0 (the game's hold a leftover pointer's high half, the low half 0, as
    /// the ground kind), and 4 zero bytes after the block; a checksum of our own tying the c-file
    /// to the g-file (each of the game's has its own; nothing else names it), which also ends the
    /// g-file.
    /// </summary>
    /// <param name="number">The sub-zone's number in its zone, 0 to 8 (row × 3 + column, rows from the lowest z).</param>
    /// <param name="roads">Road decal meshes to lay after the ground (see the class's notes), or none.</param>
    public static (byte[] Header, byte[] Data) Write(int number, Vector2 origin, IReadOnlyList<GroundVertex> vertices,
                                                     IReadOnlyList<IReadOnlyList<PatchBlock>> patches, float gridStep = GridStep,
                                                     IReadOnlyList<RoadDecalToWrite>? roads = null)
    {
        const int fields = 0x18;
        byte[] prefix = new byte[fields + 0x48 + LoadArea];
        Encoding.ASCII.GetBytes("SUBZ").CopyTo(prefix, 0);
        BitConverter.TryWriteBytes(prefix.AsSpan(4), Version);
        BitConverter.TryWriteBytes(prefix.AsSpan(8), number);
        BitConverter.TryWriteBytes(prefix.AsSpan(0x0C), HeaderWord);
        BitConverter.TryWriteBytes(prefix.AsSpan(fields), 1);
        BitConverter.TryWriteBytes(prefix.AsSpan(fields + 4), number);
        BitConverter.TryWriteBytes(prefix.AsSpan(fields + 0x64), roads?.Count ?? 0);
        // The checksum: a hash of the ground (any value ties the halves together, as long as both carry it).
        uint checksum = 2166136261;
        void Mix(int value) { foreach (byte b in BitConverter.GetBytes(value)) checksum = (checksum ^ b) * 16777619; }
        Mix(number); Mix(BitConverter.SingleToInt32Bits(origin.X)); Mix(BitConverter.SingleToInt32Bits(origin.Y));
        foreach (GroundVertex v in vertices) { Mix(v.XStep); Mix(v.ZStep); Mix(BitConverter.SingleToInt32Bits(v.Height)); }
        foreach (var patch in patches) foreach (PatchBlock b in patch) { Mix((int)b.Kind); foreach (int i in b.Strip) Mix(i); }
        if (checksum == 0) checksum = 1;
        byte[] sixFloats = [.. new[] { 1f, 1f, 1f, 0f, 0f, 0f }.SelectMany(BitConverter.GetBytes)];
        return Write(prefix, fields, checksum, GroundKind, sixFloats, new byte[4], BitConverter.GetBytes(checksum), origin, vertices, patches, gridStep, roads);
    }

    /// <summary>The shared part: <paramref name="prefix"/> is the header up to the first patch, its fields at <paramref name="fields"/>.</summary>
    static (byte[] Header, byte[] Data) Write(byte[] prefix, int fields, uint checksum, uint material, byte[] sixFloats, byte[] afterBlock, byte[] dataTail,
                                              Vector2 origin, IReadOnlyList<GroundVertex> vertices, IReadOnlyList<IReadOnlyList<PatchBlock>> patches, float gridStep,
                                              IReadOnlyList<RoadDecalToWrite>? roads = null)
    {
        if (vertices.Count is 0 or > 65536) throw new ArgumentException($"{vertices.Count} vertices: a sub-zone holds 1 to 65,536");
        if (patches.Count == 0 || patches.Any(p => p.Count == 0 || p.Any(b => b.Strip.Length < 3))) throw new ArgumentException("every patch needs a strip");
        prefix = (byte[])prefix.Clone();

        // (A grid step is the zone's width / 511: GridStep in Armageddon's 255 m zones, 1 m in a world the game gives 511 m zones.)
        Vector3 Position(GroundVertex v) => new(origin.X + v.XStep * gridStep, v.Height, origin.Y + v.ZStep * gridStep);

        // Each patch: its box, its blocks' strips.
        var indices = new List<ushort>();
        var boxes = new List<(Vector3 Min, Vector3 Max)>();
        var blocks = new List<(uint Material, int First, int Count, int Lowest, int Highest)>();
        foreach (var patch in patches)
        {
            Vector3 min = new(float.MaxValue), max = new(float.MinValue);
            foreach (PatchBlock b in patch)
            {
                foreach (int v in b.Strip)
                {
                    min = Vector3.Min(min, Position(vertices[v]));
                    max = Vector3.Max(max, Position(vertices[v]));
                }
                blocks.Add((b.Kind == GroundKind ? material : b.Kind, indices.Count, b.Strip.Length, b.Strip.Min(), b.Strip.Max()));
                foreach (int v in b.Strip) indices.Add((ushort)v);
            }
            boxes.Add((min, max));
        }

        // The g-file.
        var data = new MemoryStream();
        var g = new BinaryWriter(data);
        g.Write(checksum); g.Write(0); g.Write(0); g.Write(0);
        foreach (ushort i in indices) g.Write(i);
        while (data.Length % 16 != 0) g.Write((byte)0);
        int vertexOffset = (int)data.Length;
        foreach (GroundVertex v in vertices)
        {
            if (v.XStep is < 0 or > 255 || v.ZStep is < 0 or > 255) throw new ArgumentException($"a vertex at grid step {v.XStep}, {v.ZStep}: steps go from 0 to 255");
            g.Write((short)Math.Clamp(MathF.Round(v.Height * 64), short.MinValue, short.MaxValue));
            g.Write((byte)v.XStep);
            g.Write((byte)((v.ZStep - 128) & 0xFF));
            Vector3 n = Vector3.Normalize(v.Normal);
            // The 4th byte (0x80 or 0xFF in the game's): how much of a cliff shows. The cliff shaders
            // (ir_terrain_sidemap_t, _single_t) take it as w × 2 − 1 and drop each pixel where that is
            // under a dither pattern, so 0x80 shows none of a cliff (it drew black) and 0xFF all of
            // it; the ground's shader doesn't read it.
            g.Write(NormalByte(n.X)); g.Write(NormalByte(n.Y)); g.Write(NormalByte(n.Z)); g.Write((byte)0xFF);
        }
        g.Write(dataTail);
        byte[] dataBytes = data.ToArray();

        // The mesh block.
        var meshBlock = new MemoryStream();
        var m = new BinaryWriter(meshBlock);
        int blockSize = 0x60 + patches.Count * 48 + blocks.Count * 20 + 8 + 4;
        m.Write(9); m.Write(checksum); m.Write(blockSize); m.Write(dataBytes.Length);
        m.Write(1); m.Write(patches.Count); m.Write(0x60); m.Write(vertices.Count);
        m.Write((byte)8); m.Write((byte)24); m.Write((byte)0); m.Write((byte)0);
        m.Write(sixFloats);
        m.Write(vertexOffset); m.Write(indices.Count); m.Write(16);
        m.Write((byte)2); m.Write((byte)0); m.Write((ushort)blocks.Count);
        m.Write(new byte[0x60 - 0x4C]);
        for (int p = 0; p < patches.Count; p++)
        {
            var (min, max) = boxes[p];
            Vector3 half = (max - min) / 2;
            m.Write(patches[p].Count); m.Write(0f); m.Write(0f); m.Write(0f);
            m.Write(-half.X); m.Write(-half.Y); m.Write(-half.Z); m.Write(half.X); m.Write(half.Y); m.Write(half.Z);
            m.Write(0); m.Write(0);
        }
        foreach (var (word, first, count, lowest, highest) in blocks)
        {
            m.Write(word); m.Write(first); m.Write(count); m.Write(lowest); m.Write(highest);
        }
        m.Write(0); m.Write(0); m.Write(checksum);

        // The c-file: the header with the new place, patch count and box; the patches; the block.
        var output = new MemoryStream();
        var h = new BinaryWriter(output);
        Vector3 allMin = boxes.Select(b => b.Min).Aggregate(Vector3.Min), allMax = boxes.Select(b => b.Max).Aggregate(Vector3.Max);
        BitConverter.TryWriteBytes(prefix.AsSpan(fields + 0x08), origin.X);
        BitConverter.TryWriteBytes(prefix.AsSpan(fields + 0x10), origin.Y);
        BitConverter.TryWriteBytes(prefix.AsSpan(fields + 0x14), patches.Count);
        float[] box = [allMin.X, allMin.Y, allMin.Z, allMax.X, allMax.Y, allMax.Z, (allMin.X + allMax.X) / 2, (allMin.Y + allMax.Y) / 2, (allMin.Z + allMax.Z) / 2];
        for (int i = 0; i < box.Length; i++) BitConverter.TryWriteBytes(prefix.AsSpan(fields + 0x24 + i * 4), box[i]);
        h.Write(prefix);
        for (int p = 0; p < patches.Count; p++)
        {
            var (min, max) = boxes[p];
            Vector3 middle = (min + max) / 2, half = (max - min) / 2;
            h.Write(middle.X); h.Write(middle.Y); h.Write(middle.Z);
            h.Write(1f); h.Write(0f); h.Write(0f); h.Write(0f); h.Write(1f); h.Write(0f); h.Write(0f); h.Write(0f); h.Write(1f);
            h.Write(p);
            h.Write(-half.X); h.Write(-half.Y); h.Write(-half.Z); h.Write(half.X); h.Write(half.Y); h.Write(half.Z);
            h.Write(0f); h.Write(0f); h.Write(0f); h.Write(half.Length());
            h.Write(0);
        }
        while (output.Length % 16 != 0) h.Write((byte)0);
        h.Write(meshBlock.ToArray());
        h.Write(afterBlock);
        if (roads == null || roads.Count == 0) return (output.ToArray(), dataBytes);

        // The road decals (after no stitch pieces and no stitch mesh: the 0 word in afterBlock): their
        // records from 4, then each one's mesh from 16, a run-time slot for each instance, from 4 its
        // name list, from 16 its material map and material; each mesh's g-data after the ground's, from 16.
        void Align(int to) { while (output.Length % to != 0) h.Write((byte)0); }
        Align(4);
        foreach (RoadDecalToWrite road in roads) h.Write(road.Record);
        var roadData = new MemoryStream();
        roadData.Write(dataBytes);
        foreach (RoadDecalToWrite road in roads)
        {
            Align(16);
            h.Write(road.Mesh);
            h.Write(new byte[road.Instances * 4]);
            Align(4);
            h.Write(road.Names);
            Align(16);
            h.Write(road.Map);
            h.Write(road.Material);
            while (roadData.Length % 16 != 0) roadData.WriteByte(0);
            roadData.Write(road.Data);
        }
        return (output.ToArray(), roadData.ToArray());
    }

    static byte NormalByte(float value) => (byte)Math.Clamp(MathF.Round((value + 1) * 127.5f), 0, 255);

    /// <summary>
    /// A strip made of triangles (three vertex numbers each, wound as <see cref="PatchTriangles"/>
    /// reads them): each joined to the last by repeated indices, which make no triangles, and
    /// starting at an even place so it keeps its winding.
    /// </summary>
    public static int[] Strip(IReadOnlyList<int> triangles)
    {
        var strip = new List<int>(triangles.Count * 2);
        for (int t = 0; t + 2 < triangles.Count; t += 3)
        {
            if (strip.Count > 0)
            {
                strip.Add(strip[^1]);
                strip.Add(triangles[t]);
                if (strip.Count % 2 == 1) strip.Add(triangles[t]);
            }
            strip.Add(triangles[t]);
            strip.Add(triangles[t + 1]);
            strip.Add(triangles[t + 2]);
        }
        return [.. strip];
    }

    /// <summary>Guerrilla's version-6 mesh block: version, checksum, block size, g-file size, and the checksum again at its end.</summary>
    static bool IsGuerrillaMeshBlock(byte[] header, int at)
    {
        if (at < 0 || at + 0x30 > header.Length || U32(header, at) != 6) return false;
        uint size = U32(header, at + 8);
        return size >= 0x30 && at + size <= header.Length && U32(header, at + 4) != 0 && U32(header, at + (int)size - 4) == U32(header, at + 4);
    }

    /// <summary>The name of the sub-zone file pair's halves: world_08_02_6.ctmesh_pc and world_08_02_6.gtmesh_pc.</summary>
    public static string HeaderFileName(string zone, int number) => $"{zone}_{number}.ctmesh_pc";
    public static string DataFileName(string zone, int number) => $"{zone}_{number}.gtmesh_pc";

    static uint U32(byte[] bytes, int at) => BitConverter.ToUInt32(bytes, at);
    static float F32(byte[] bytes, int at) => BitConverter.ToSingle(bytes, at);
}

/// <summary>A vertex of ground to write (see <see cref="TerrainSubzone.Write"/>): its place on the sub-zone's grid (0-255 steps from its origin), its height in metres, and its normal.</summary>
public readonly record struct GroundVertex(int XStep, int ZStep, float Height, Vector3 Normal);

/// <summary>
/// One of Guerrilla's road decal meshes: its 40-byte record, its position (the zone's centre, its
/// vertices' origin), its version-6 mesh block and g-data, its texture name list, its material's
/// slots (slot hash, texture) and constants (name hash, value), and its triangles in the world.
/// </summary>
public sealed record GuerrillaRoadDecal(byte[] Record, Vector3 Position, byte[] MeshBlock, byte[] MeshData, IReadOnlyList<string> Names,
                                        IReadOnlyList<(uint Slot, string Texture)> Slots, IReadOnlyDictionary<uint, Vector4> Constants, Vector3[] Corners);

/// <summary>
/// A road decal mesh of a sub-zone as drawn: its vertices in the world, their texture coordinates
/// (across the road, and along it in repeats of its texture), its triangles (three vertex numbers
/// each, wound as <see cref="TerrainSubzone.PatchTriangles"/>), and its road texture (the road
/// shader's own, such as ter_sur_road02_di.tga), or null.
/// </summary>
public sealed record RoadMesh(Vector3[] Positions, Vector2[] TextureCoordinates, int[] Triangles, string? RoadTexture);

/// <summary>
/// A road decal mesh as Armageddon's sub-zones hold it: its 40-byte record (instance count first,
/// then run-time words and its position), its version-9 mesh block, how many instances it has (a
/// run-time slot each after the mesh), its texture name list (the list's size, then the names),
/// its 16-byte material map and its material block; and its mesh's g-data.
/// </summary>
public sealed record RoadDecalToWrite(byte[] Record, byte[] Mesh, int Instances, byte[] Names, byte[] Map, byte[] Material, byte[] Data);
