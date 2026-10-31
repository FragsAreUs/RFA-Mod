using System.Numerics;
using System.Text;
using RfaFormats.Zones;

namespace RfaFormats.Meshes;

/// <summary>
/// A 3D model: a static mesh (.csmesh_pc), a character (.ccmesh_pc), a destructible object such as
/// a building or prop (.cchk_pc), a vehicle (.ccar_pc), a stitch piece (.cstch_pc: the rocks,
/// cave walls, roads and other fixed pieces a level is built from, placed by its stitch list) or
/// a foliage mesh (.cfmesh_pc: debris clumps the terrain scatters, see <see cref="ZoneFoliage"/>).
/// Like textures, each comes as a pair: the c-file with the description and materials, and the
/// g-file with the vertex and index buffers, which the game loads into video memory. Foliage
/// meshes are the exception: they have no g-file, as their buffers are inside the mesh block, at
/// offsets from its start (format 30, triangle lists).
///
/// All kinds hold the same mesh block (version 9), at an offset the c-file's header gives:
///   +0x00 version 9, checksum, size of this block, size of the g-file
///   +0x10 detail levels, submesh count, submesh table offset (from the block start), vertex count
///   +0x20 vertex size (u8), vertex format (u8), UV channels (u8), 0 (u8), then 6 floats (1,1,1, 0,0,0)
///   +0x3C vertex buffer offset (in the g-file), index count, index buffer offset (in the g-file)
///   +0x48 index size (u8, 2, or 4 in a few big objects), topology (u8, 0 = triangle strips), render block count (u16)
///   +0x4C characters: the size of their bone palettes together, +0x54 how many palettes, and from
///         +0x5C the palettes, one byte per entry: the skeleton's bone for each of the mesh's own bone
///         numbers (the soldier's one: 1, 2, ... 22, 24, ... skipping the root and the bones items are
///         held by); then 2 bytes per palette, its size and where it starts (the officer's: 64 from
///         0, 21 from 64)
///   submeshes: 48 bytes each: render block count, pivot (3 floats), bounding box (6 floats), 8 run-time bytes
///   render blocks: 20 bytes each: material (u16) and 2 unused bytes, first index, index count, lowest and highest vertex
///   the block ends with the checksum again
/// Submeshes are the detail levels of characters (one each), the body and parts of vehicles
/// (placed by the vehicle's part table, see <see cref="VehicleParts"/>), or for
/// destructible objects and stitch pieces the meshes their objects are built from: which ones an
/// object uses, and where each goes, is in the file's destruction data (see <see cref="ChunkObjects"/>).
///
/// Vertices start with the position (3 floats) and the normal (4 bytes: x, y, z from 0-255 meaning
/// -1 to 1), then a tangent (4 bytes) for most formats, for characters (vertex format 3) the weights
/// of up to 4 bones (4 bytes at +20, adding up to about 255) and their numbers in the mesh's bone
/// palette (4 bytes at +24), and last the UV channels (2 × 16-bit, 1024 = 1.0). The texture's UVs are
/// the first channel. (Checked on all 73 character meshes whose skeleton has their name: through the
/// palette, vertices sit on or near the bones that move them; see <see cref="SkinBones"/>.)
/// </summary>
public sealed class ModelFile
{
    public const int MeshVersion = 9;

    public string Name { get; }
    public ModelKind Kind { get; }
    public int VertexFormat { get; }
    public int VertexSize { get; }
    public int UvChannels { get; }
    public int DetailLevels { get; }
    public IReadOnlyList<Submesh> Submeshes { get; }
    public IReadOnlyList<ModelMaterial> Materials { get; }

    /// <summary>For destructible objects and stitch pieces, the objects in the file; else, or if they couldn't be read, null.</summary>
    public ChunkObjects? Objects { get; }

    /// <summary>For vehicles, where each part goes; else, or if they couldn't be read, null.</summary>
    public VehicleParts? Parts { get; }

    /// <summary>
    /// For static meshes, their tags: named points where something attaches (every static mesh in
    /// the game has one; a turret's base has "turret_swivel", where its middle part goes). Empty
    /// for other kinds.
    /// </summary>
    public IReadOnlyList<MeshTag> Tags { get; } = [];

    /// <summary>Every vertex position, normal and texture coordinate (first UV channel).</summary>
    public Vector3[] Positions { get; }
    public Vector3[] Normals { get; }
    public Vector2[] TextureCoordinates { get; }

    /// <summary>
    /// For characters, the bones that move each vertex: 4 per vertex (vertex v's are at 4v to 4v + 3),
    /// as the mesh's own bone numbers (entries in a bone palette: see <see cref="SkinBones"/>), with
    /// how much each moves it in <see cref="SkinWeights"/> (0 to 1, adding up to 1). Null for other kinds.
    /// </summary>
    public int[]? SkinNumbers { get; }
    public float[]? SkinWeights { get; }

    /// <summary>For characters, the bone palettes: for each of the mesh's bone numbers, the skeleton's bone. Most have one.</summary>
    public IReadOnlyList<int[]> BonePalettes { get; } = [];

    ModelFile(string name, ModelKind kind, byte[] header, byte[] data)
    {
        Name = name;
        Kind = kind;
        int at = FindMeshBlock(kind, header);
        if (kind == ModelKind.Foliage) data = header[at..];  // its buffers are in the mesh block
        uint checksum = U32(header, at + 4);
        int blockSize = (int)U32(header, at + 8);
        DetailLevels = Math.Max(1, (int)U32(header, at + 0x10));
        int submeshCount = (int)U32(header, at + 0x14);
        int submeshTable = at + (int)U32(header, at + 0x18);
        int vertexCount = (int)U32(header, at + 0x1C);
        VertexSize = header[at + 0x20];
        VertexFormat = header[at + 0x21];
        UvChannels = header[at + 0x22];
        int vertexOffset = (int)U32(header, at + 0x3C);
        int indexCount = (int)U32(header, at + 0x40);
        int indexOffset = (int)U32(header, at + 0x44);
        int indexSize = header[at + 0x48];
        int topology = header[at + 0x49];
        int blockCount = BitConverter.ToUInt16(header, at + 0x4A);
        if (indexSize is not (2 or 4)) throw new NotSupportedException($"{name}: {indexSize}-byte indices");
        if (U32(header, at + blockSize - 4) != checksum) throw new InvalidDataException($"{name}: the mesh block's checksums differ");

        // Vertices
        Positions = new Vector3[vertexCount];
        Normals = new Vector3[vertexCount];
        TextureCoordinates = new Vector2[vertexCount];
        int uvAt = VertexSize - 4 * UvChannels;
        for (int v = 0; v < vertexCount; v++)
        {
            int p = vertexOffset + v * VertexSize;
            Positions[v] = new Vector3(F32(data, p), F32(data, p + 4), F32(data, p + 8));
            Normals[v] = Vector3.Normalize(new Vector3(data[p + 12], data[p + 13], data[p + 14]) / 127.5f - Vector3.One);
            if (UvChannels > 0)
                TextureCoordinates[v] = new Vector2(BitConverter.ToInt16(data, p + uvAt), BitConverter.ToInt16(data, p + uvAt + 2)) / 1024f;
        }

        // Characters: which bones move each vertex (by the mesh's own numbers), and the palettes
        // that turn those into the skeleton's.
        if (kind == ModelKind.Character && VertexFormat == 3 && VertexSize >= 28)
        {
            int paletteSize = (int)U32(header, at + 0x4C), paletteCount = (int)U32(header, at + 0x54);
            int palettesAt = at + 0x5C, listAt = palettesAt + paletteSize;
            var palettes = new List<int[]>();
            for (int i = 0; i < paletteCount && listAt + 2 * i + 1 < header.Length; i++)
            {
                int size = header[listAt + 2 * i], first = header[listAt + 2 * i + 1];
                if (first + size <= paletteSize) palettes.Add(header[(palettesAt + first)..(palettesAt + first + size)].Select(b => (int)b).ToArray());
            }
            if (palettes.Sum(p => p.Length) != paletteSize)  // not as expected: all of it as one palette
                palettes = [header[palettesAt..listAt].Select(b => (int)b).ToArray()];
            BonePalettes = palettes;
            SkinNumbers = new int[vertexCount * 4];
            SkinWeights = new float[vertexCount * 4];
            for (int v = 0; v < vertexCount; v++)
            {
                int p = vertexOffset + v * VertexSize;
                float total = data[p + 20] + data[p + 21] + data[p + 22] + data[p + 23];
                for (int i = 0; i < 4; i++)
                {
                    SkinNumbers[v * 4 + i] = data[p + 24 + i];
                    SkinWeights[v * 4 + i] = total > 0 ? data[p + 20 + i] / total : (i == 0 ? 1 : 0);
                }
            }
        }

        // Indices, then the render blocks and the submeshes they belong to
        var indices = new int[indexCount];
        for (int i = 0; i < indexCount; i++)
            indices[i] = indexSize == 2 ? BitConverter.ToUInt16(data, indexOffset + i * 2) : BitConverter.ToInt32(data, indexOffset + i * 4);
        int blockTable = submeshTable + submeshCount * 48;
        var submeshes = new List<Submesh>(submeshCount);
        int nextBlock = 0;
        for (int s = 0; s < submeshCount; s++)
        {
            int e = submeshTable + s * 48;
            int blocks = (int)U32(header, e);
            var parts = new List<MeshPart>(blocks);
            for (int b = 0; b < blocks && nextBlock < blockCount; b++, nextBlock++)
            {
                int r = blockTable + nextBlock * 20;
                int material = BitConverter.ToUInt16(header, r);  // the other 2 bytes aren't part of it
                int first = (int)U32(header, r + 4), count = (int)U32(header, r + 8);
                parts.Add(new MeshPart(material, topology == 0 ? StripToTriangles(indices, first, count) : ListTriangles(indices, first, count)));
            }
            submeshes.Add(new Submesh(
                Position: new Vector3(F32(header, e + 4), F32(header, e + 8), F32(header, e + 12)),
                BoundsMin: new Vector3(F32(header, e + 16), F32(header, e + 20), F32(header, e + 24)),
                BoundsMax: new Vector3(F32(header, e + 28), F32(header, e + 32), F32(header, e + 36)),
                Parts: parts));
        }
        Submeshes = submeshes;

        // Static meshes and characters list their materials in the header (0x10: offset of a list of
        // material offsets, 0x14: how many); the other kinds are searched for after the mesh block.
        IReadOnlyList<int>? listed = null;
        if (kind is ModelKind.Static or ModelKind.Character)
        {
            int list = (int)U32(header, 0x10), count = (int)U32(header, 0x14);
            if (count < 1024 && list + count * 4 <= header.Length)
                listed = Enumerable.Range(0, count).Select(i => (int)U32(header, list + i * 4)).ToList();
        }
        // Destructible objects and stitch pieces (as in Guerrilla) have, from the next multiple of 16
        // after the mesh block, the size of their texture name list and the names; from the next
        // multiple of 16 after them, a run-time value and the number of materials, which follow.
        int? names = null, materialCount = null;
        int from = at + blockSize;
        if (kind is ModelKind.Destructible or ModelKind.Stitch)
        {
            int list = (at + blockSize + 15) & ~15;
            int size = list + 4 <= header.Length ? (int)U32(header, list) : -1;
            int counts = (list + 4 + size + 15) & ~15;
            if (size >= 0 && counts + 8 <= header.Length && U32(header, counts + 4) < 1024)
                (names, materialCount, from) = (list + 4, (int)U32(header, counts + 4), counts + 8);
        }
        Materials = ModelMaterial.ReadAll(header, from, listed, names, materialCount);

        if (kind is ModelKind.Destructible or ModelKind.Stitch)
        {
            try { Objects = ChunkObjects.Read(header, submeshCount); }
            catch (InvalidDataException) { }  // drawn as before: the first submesh
        }
        if (kind == ModelKind.Static) Tags = MeshTag.ReadAll(header);
        if (kind == ModelKind.Vehicle)
        {
            try { Parts = VehicleParts.Read(header, submeshCount); }
            catch (Exception ex) when (ex is InvalidDataException or ArgumentOutOfRangeException) { }  // drawn as stored
        }
    }

    /// <summary>
    /// For characters, the bones that move each vertex as bone numbers in a skeleton (4 per vertex,
    /// as <see cref="SkinNumbers"/>), through the bone palettes. A mesh with several palettes doesn't
    /// say (that we've found) which part uses which, so each part (render block) gets the one that puts
    /// its vertices nearest the bones moving them most, in the rest pose: on the characters checked,
    /// one fits clearly (about 0.1 m away, against 0.8 m for the others). Null for other kinds.
    /// </summary>
    public int[]? SkinBones(Rig rig)
    {
        if (SkinNumbers == null || SkinWeights == null || BonePalettes.Count == 0) return null;
        int BoneOf(int[] palette, int number) => number < palette.Length && palette[number] < rig.Bones.Count ? palette[number] : 0;
        int Heaviest(int v) => SkinNumbers[v * 4 + Enumerable.Range(0, 4).MaxBy(i => SkinWeights[v * 4 + i])];

        var bones = new int[SkinNumbers.Length];
        var done = new bool[Positions.Length];
        foreach (MeshPart part in Submeshes.SelectMany(s => s.Parts))
        {
            int[] vertices = part.Triangles.Distinct().ToArray();
            int[] chosen = BonePalettes[0];
            double nearest = double.MaxValue;
            foreach (int[] palette in BonePalettes)
            {
                if (vertices.Any(v => Enumerable.Range(0, 4).Any(i => SkinWeights[v * 4 + i] > 0 && SkinNumbers[v * 4 + i] >= palette.Length))) continue;
                double distance = vertices.Sum(v => Vector3.Distance(Positions[v], rig.Bones[BoneOf(palette, Heaviest(v))].Position));
                if (distance < nearest) (chosen, nearest) = (palette, distance);
            }
            foreach (int v in vertices)
            {
                for (int i = 0; i < 4; i++) bones[v * 4 + i] = BoneOf(chosen, SkinNumbers[v * 4 + i]);
                done[v] = true;
            }
        }
        for (int v = 0; v < Positions.Length; v++)
            if (!done[v])
                for (int i = 0; i < 4; i++) bones[v * 4 + i] = BoneOf(BonePalettes[0], SkinNumbers[v * 4 + i]);
        return bones;
    }

    /// <param name="data">The g-file; for foliage meshes, which have none, the c-file again (see <see cref="DataFileName"/>).</param>
    public static ModelFile Read(string name, byte[] header, byte[] data) =>
        new(name, KindOf(name) ?? throw new NotSupportedException($"{name} isn't a model file"), header, data);

    /// <summary>The kind of model a file name is (either half of the pair), or null.</summary>
    public static ModelKind? KindOf(string fileName) => Path.GetExtension(fileName).ToLowerInvariant() switch
    {
        ".csmesh_pc" or ".gsmesh_pc" => ModelKind.Static,
        ".ccmesh_pc" or ".gcmesh_pc" => ModelKind.Character,
        ".cchk_pc" or ".gchk_pc" => ModelKind.Destructible,
        ".ccar_pc" or ".gcar_pc" => ModelKind.Vehicle,
        ".cstch_pc" or ".gstch_pc" => ModelKind.Stitch,
        ".cfmesh_pc" => ModelKind.Foliage,
        _ => null,
    };

    /// <summary>The c-file of a pair, from either half: x.gsmesh_pc → x.csmesh_pc.</summary>
    public static string HeaderFileName(string fileName)
    {
        string extension = Path.GetExtension(fileName);
        return extension.Length > 1 && char.ToLowerInvariant(extension[1]) == 'g' ? Path.ChangeExtension(fileName, "c" + extension[2..]) : fileName;
    }

    /// <summary>
    /// The g-file of a pair, from either half: x.csmesh_pc → x.gsmesh_pc. Foliage meshes have no
    /// g-file, so for them it is the c-file itself.
    /// </summary>
    public static string DataFileName(string fileName) => KindOf(fileName) == ModelKind.Foliage
        ? fileName
        : Path.ChangeExtension(fileName, "g" + Path.GetExtension(HeaderFileName(fileName))[2..]);

    /// <summary>
    /// The model as seen in the game, each submesh where it goes: for a destructible object, the
    /// object a zone names by <paramref name="uid"/> (or the file's first), whole; for a stitch
    /// piece its first submesh (the second is a less detailed copy, for far away); for a vehicle
    /// its body and every part in its rest pose (see <see cref="VehicleParts"/>); the most
    /// detailed level of a character; every submesh of the rest.
    /// </summary>
    public IReadOnlyList<PlacedSubmesh> Intact(uint? uid = null)
    {
        if (Kind is ModelKind.Destructible or ModelKind.Stitch)
        {
            if (ObjectFor(uid) is { Whole.Count: > 0 } chosen) return Place(chosen.Whole);
            return Submeshes.Count == 0 ? [] : [new PlacedSubmesh(Submeshes[0], Matrix4x4.Identity)];
        }
        if (Kind == ModelKind.Vehicle && Parts != null && Submeshes.Count > 0)
            return Parts.Parts.Where(p => p.Submesh > 0 && p.Seen)
                              .Select(p => new PlacedSubmesh(Submeshes[p.Submesh], p.Transform))
                              .Prepend(new PlacedSubmesh(Submeshes[0], Matrix4x4.Identity))
                              .ToList();
        return Submeshes.Take(Math.Max(1, Submeshes.Count / DetailLevels)).Select(s => new PlacedSubmesh(s, Matrix4x4.Identity)).ToList();
    }

    /// <summary>
    /// The model as the game draws it from far away: for a stitch piece its less detailed copy (its
    /// second submesh, in the same place with about half the triangles; 827 of the game's 933
    /// pieces have one); everything else as <see cref="Intact"/>.
    /// </summary>
    public IReadOnlyList<PlacedSubmesh> FarAway(uint? uid = null) =>
        Kind == ModelKind.Stitch && Submeshes.Count > 1 ? [new PlacedSubmesh(Submeshes[1], Matrix4x4.Identity)] : Intact(uid);

    /// <summary>The pieces a destructible object (named by <paramref name="uid"/>, or the file's first) breaks into, each where it starts.</summary>
    public IReadOnlyList<PlacedSubmesh> Broken(uint? uid = null) =>
        Kind == ModelKind.Destructible && ObjectFor(uid) is { } chosen ? Place(chosen.Pieces) : [];

    /// <summary>The object with a uid, or the file's first one (also when the uid isn't in the file).</summary>
    public ChunkObject? ObjectFor(uint? uid) =>
        Objects == null ? null : (uid is uint id ? Objects.WithUid(id) : null) ?? Objects.Objects.FirstOrDefault();

    List<PlacedSubmesh> Place(IEnumerable<ChunkPart> parts) => parts.Select(p => new PlacedSubmesh(Submeshes[p.Submesh], p.Transform)).ToList();

    /// <summary>
    /// Where the mesh block starts: each kind's header says so (static meshes, characters and foliage
    /// at 0x08, destructible objects and stitch pieces at 0x10, vehicles at 0x24). Checked by its
    /// version and checksums.
    /// </summary>
    static int FindMeshBlock(ModelKind kind, byte[] header)
    {
        int pointer = kind switch { ModelKind.Destructible or ModelKind.Stitch => 0x10, ModelKind.Vehicle => 0x24, _ => 0x08 };
        int at = (int)U32(header, pointer);
        if (IsMeshBlock(header, at)) return at;
        throw new InvalidDataException($"no version-{MeshVersion} mesh block where the header says (0x{at:X})");
    }

    internal static bool IsMeshBlock(byte[] header, int at)
    {
        if (at < 0 || at + 0x50 > header.Length || U32(header, at) != MeshVersion) return false;
        uint size = U32(header, at + 8);
        return size >= 0x50 && at + size <= header.Length && U32(header, at + (int)size - 4) == U32(header, at + 4);
    }

    /// <summary>
    /// Triangles from a triangle strip: each index makes a triangle with the two before it, every
    /// other one turned around so all face the same way. Repeated indices join separate strips and
    /// make no triangle.
    /// </summary>
    internal static int[] StripToTriangles(int[] indices, int first, int count)
    {
        var triangles = new List<int>(Math.Max(0, count - 2) * 3);
        for (int i = first; i + 2 < first + count; i++)
        {
            int a = indices[i], b = indices[i + 1], c = indices[i + 2];
            if (a == b || b == c || a == c) continue;
            if ((i - first) % 2 == 0) triangles.AddRange([a, b, c]);
            else triangles.AddRange([b, a, c]);
        }
        return [.. triangles];
    }

    internal static int[] ListTriangles(int[] indices, int first, int count) => indices.Skip(first).Take(count / 3 * 3).ToArray();

    static uint U32(byte[] bytes, int at) => BitConverter.ToUInt32(bytes, at);
    static float F32(byte[] bytes, int at) => BitConverter.ToSingle(bytes, at);
}

public enum ModelKind { Static, Character, Destructible, Vehicle, Stitch, Foliage }

/// <param name="Position">The submesh's pivot.</param>
/// <param name="BoundsMin">Bounding box around the pivot.</param>
public sealed record Submesh(Vector3 Position, Vector3 BoundsMin, Vector3 BoundsMax, IReadOnlyList<MeshPart> Parts);

/// <summary>A submesh and where it goes in the model (points as rows: p × Transform).</summary>
public readonly record struct PlacedSubmesh(Submesh Submesh, Matrix4x4 Transform);

/// <summary>
/// A tag of a static mesh: a named point (and direction) where something attaches. The c-file's
/// header gives where the tag table is (0x24); there, the number of tags, then 56 bytes per tag
/// (Guerrilla's are 52): a number not known yet, the name's hash (lower-case CRC-32, as zone
/// property names: "turret_swivel" is 0xF5BDC1F4), rotation (9 floats, rows right, up, forward)
/// and position.
/// </summary>
/// <param name="Placement">Where the tag is in the mesh (points as rows: p × Placement).</param>
public sealed record MeshTag(uint NameHash, Matrix4x4 Placement)
{
    internal static IReadOnlyList<MeshTag> ReadAll(byte[] header)
    {
        if (header.Length < 0x28) return [];
        int at = BitConverter.ToInt32(header, 0x24);
        if (at <= 0 || at + 4 > header.Length) return [];
        int count = BitConverter.ToInt32(header, at);
        if (count < 0 || count > 256 || at + 4 + count * 56 > header.Length) return [];
        var tags = new List<MeshTag>();
        for (int i = 0, t = at + 4; i < count; i++, t += 56)
        {
            float F(int offset) => BitConverter.ToSingle(header, t + offset);
            tags.Add(new MeshTag(BitConverter.ToUInt32(header, t + 4), new Matrix4x4(
                F(8), F(12), F(16), 0,
                F(20), F(24), F(28), 0,
                F(32), F(36), F(40), 0,
                F(44), F(48), F(52), 1)));
        }
        return tags;
    }
}

/// <summary>Triangles drawn with one material: three vertex numbers per triangle.</summary>
public sealed record MeshPart(int Material, int[] Triangles);

/// <summary>
/// A model's material: its textures by slot. Each material in the c-file is a block starting with
/// its size, two checksums, flags (usually 0x40), texture count (u16) and two constant counts (u8
/// each), three run-time pointers and a 0, then 12 bytes per texture: where its name is in the
/// file's texture name list, a checksum naming the slot (normal map, diffuse, specular, ...) and the
/// slot number (low 16 bits). The materials follow the mesh block; the name list is near them.
/// </summary>
public sealed class ModelMaterial
{
    /// <summary>Slot checksum of the diffuse (colour) texture: always the "_di" / "_d" one.</summary>
    public const uint DiffuseSlot = 0x69B48F91;

    /// <summary>Each texture: its slot (the game's shader's name for it, hashed like zone property names) and its file.</summary>
    public IReadOnlyList<(uint Slot, string Texture)> Textures { get; }

    /// <summary>
    /// The material's constants: name (hashed the same way) and value, a float4 each. The game's
    /// shaders name them; the common ones (of 7,170 materials): Diffuse_Color (multiplies the colour
    /// texture; often 0.53, 0.22 or 0.16 grey), Normal_Height (how strongly the normal map bends the
    /// surface), Specular_Power and Specular_Color (shine), Glow_Color and Self_Illumination (glow),
    /// Diffuse_TilingU and _V.
    /// </summary>
    public IReadOnlyList<(uint Name, Vector4 Value)> Constants { get; }

    /// <summary>
    /// The material's flags (+12), usually 0x40. The low two bits say how the game draws it, as
    /// counted over all 7,170 materials against the shaders their constants belong to: 0 solid
    /// (cloth, alien skin and walkers too, though their shaders take a Main_Opacity), 1 laid over
    /// what is behind it (glass, water, holograms, glowing masks: <see cref="Blended"/>), 2 cut out
    /// where its colour texture's alpha is low (hair, capes, the "ir_at_*" shaders: <see cref="CutOut"/>).
    /// </summary>
    public uint Flags { get; }

    /// <summary>Drawn over what is behind it, by its colour texture's alpha (see <see cref="Flags"/>).</summary>
    public bool Blended => (Flags & 3) == 1;

    /// <summary>Cut out where its colour texture's alpha is below the game's threshold (see <see cref="Flags"/>).</summary>
    public bool CutOut => (Flags & 3) == 2;

    ModelMaterial(IReadOnlyList<(uint Slot, string Texture)> textures, IReadOnlyList<(uint Name, Vector4 Value)> constants, uint flags)
    {
        Textures = textures;
        Constants = constants;
        Flags = flags;
    }

    /// <summary>The colour texture, e.g. "rock_di.tga"; null if the material has none.</summary>
    public string? Diffuse => Textures.FirstOrDefault(t => t.Slot == DiffuseSlot).Texture;

    /// <summary>The texture in a slot, by the shader's name for it ("Normal_Map", "Specular_Map", "Glow_Map", ...); null if none.</summary>
    public string? Texture(string slot) => Textures.FirstOrDefault(t => t.Slot == NameHashes.PropertyHash(slot)).Texture;

    /// <summary>A constant by the shader's name for it ("Specular_Power", ...); null if the material hasn't it.</summary>
    public Vector4? Constant(string name)
    {
        uint hash = NameHashes.PropertyHash(name);
        foreach ((uint n, Vector4 value) in Constants)
            if (n == hash) return value;
        return null;
    }

    /// <summary>
    /// All materials, in order (the order render blocks number them in): at the offsets given, or
    /// else every material block found after the mesh block.
    /// </summary>
    /// <param name="nameList">Where the texture name list starts, if known (else it is searched for).</param>
    /// <param name="count">How many materials there are, if known (else every material block after <paramref name="from"/> is taken).</param>
    internal static List<ModelMaterial> ReadAll(byte[] header, int from, IReadOnlyList<int>? offsets, int? nameList = null, int? count = null)
    {
        var found = new List<(int Offset, List<(uint Slot, uint NameOffset)> Textures)>();
        if (offsets != null)
        {
            foreach (int at in offsets)
                found.Add((at, ReadTextures(header, at) ?? []));
        }
        else
        {
            for (int at = (from + 3) & ~3; at + 0x30 <= header.Length; at += 4)
            {
                if (found.Count == count) break;
                if (ReadTextures(header, at) is not { } textures) continue;
                found.Add((at, textures));
                at += (int)BitConverter.ToUInt32(header, at) - 4;
            }
        }

        // The texture name list: the place where every name offset points at a ".tga" name. (Static
        // meshes and characters also give it in the header at 0x18.)
        int names = nameList ?? FindNameList(header, found.SelectMany(m => m.Textures.Select(t => t.NameOffset)).Distinct().ToList());
        return found.Select(m => new ModelMaterial(m.Textures.Select(t => (t.Slot, names < 0 ? "" : CString(header, names + (int)t.NameOffset))).ToList(),
                                                   ReadConstants(header, m.Offset, m.Textures.Count),
                                                   m.Offset >= 0 && m.Offset + 16 <= header.Length ? BitConverter.ToUInt32(header, m.Offset + 12) : 0x40)).ToList();
    }

    /// <summary>
    /// A material block's constants: after its textures, their name hashes (u32 each), then from the
    /// next multiple of 16 bytes their values (a float4 each); the count is at +18.
    /// </summary>
    static List<(uint Name, Vector4 Value)> ReadConstants(byte[] header, int at, int textures)
    {
        var constants = new List<(uint, Vector4)>();
        if (at < 0 || at + 0x24 > header.Length) return constants;
        int count = header[at + 18], namesAt = at + 0x24 + textures * 12, valuesAt = (namesAt + count * 4 + 15) & ~15;
        for (int c = 0; c < count && valuesAt + c * 16 + 16 <= header.Length; c++)
        {
            int v = valuesAt + c * 16;
            constants.Add((BitConverter.ToUInt32(header, namesAt + c * 4),
                           new Vector4(BitConverter.ToSingle(header, v), BitConverter.ToSingle(header, v + 4), BitConverter.ToSingle(header, v + 8), BitConverter.ToSingle(header, v + 12))));
        }
        return constants;
    }

    /// <summary>A material block's textures (slot checksum, name offset), or null if it isn't a material block.</summary>
    static List<(uint Slot, uint NameOffset)>? ReadTextures(byte[] header, int at)
    {
        if (at < 0 || at + 0x30 > header.Length) return null;
        uint size = BitConverter.ToUInt32(header, at);
        int textures = BitConverter.ToUInt16(header, at + 16);
        int constants = header[at + 18], maxConstants = header[at + 19];
        if (size < 0x40 || size > 0x4000 || at + size > header.Length || textures < 1 || textures > 16 ||
            constants > maxConstants || at + 0x24 + textures * 12 > header.Length)
            return null;
        var slots = new List<(uint, uint)>();
        for (int t = 0; t < textures; t++)
        {
            int d = at + 0x24 + t * 12;
            if (BitConverter.ToUInt16(header, d + 8) != t) return null;
            slots.Add((BitConverter.ToUInt32(header, d + 4), BitConverter.ToUInt32(header, d)));
        }
        return slots;
    }

    static int FindNameList(byte[] header, List<uint> offsets)
    {
        if (offsets.Count == 0) return -1;
        byte[] tga = Encoding.ASCII.GetBytes(".tga");
        for (int at = header.AsSpan().IndexOf(tga); at >= 0; )
        {
            int start = at;
            while (start > 0 && header[start - 1] >= 0x20 && header[start - 1] < 0x7F) start--;
            if (offsets.All(o => start + o < header.Length && CString(header, start + (int)o).EndsWith(".tga", StringComparison.OrdinalIgnoreCase)))
                return start;
            int next = header.AsSpan(at + 4).IndexOf(tga);
            at = next < 0 ? -1 : at + 4 + next;
        }
        return -1;
    }

    static string CString(byte[] bytes, int at)
    {
        int end = Array.IndexOf(bytes, (byte)0, at);
        return Encoding.Latin1.GetString(bytes, at, (end < 0 ? bytes.Length : end) - at);
    }
}
