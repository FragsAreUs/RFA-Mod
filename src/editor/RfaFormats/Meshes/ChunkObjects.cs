using System.Numerics;
using System.Text;

namespace RfaFormats.Meshes;

/// <summary>
/// The objects in a destructible object file (.cchk_pc, magic 0xB0CEEFA5, version 62). One file
/// can hold several: a fence's straight and corner sections, six sizes of cardboard box, the 38
/// wall sections of str_war_edf_des. A zone object names its file ("chunk_name") and which object
/// in it by number ("uid"); the file's object ids say which object that is. Each object is either
/// breakable (a "destroyable": built from groups of pieces) or not (a "general object": one mesh).
///
/// Layout (as in Red Faction: Guerrilla's version 56, read with RFGM.Formats as a guide, except
/// where noted). Header: 0x10 where the mesh block starts, 0x1C/0x20 where the collision data
/// starts and its size, 0x28 where the destruction data starts, 0x158 the number of general
/// objects, 0x180 script points, 0x18C object ids.
///   general objects, 100 bytes each (RFA's are 4 bytes longer), right before their names, which
///     end at the destruction data: name (run-time), box (from, to), position, rotation (9 floats),
///     two collision models (u16), flags (u16), mesh: the submesh to draw (i16), and another (i16)
///   destruction data (from the next multiple of 128): the number of breakable objects and 4
///     run-time bytes for each; then for each, from a multiple of 16: a 44-byte header (piece
///     count at +12, link count at +20, group count at +28); from a multiple of 128 its pieces,
///     64 bytes each (box, position, centre of mass, mass, ..., link count at +62); 12 bytes per
///     piece (the submesh it is drawn with at +6); the pieces' link numbers (u16); 16 bytes per
///     link; the groups ("dlods"), 60 bytes each: name hash, position, rotation (9 floats), the
///     submesh drawn while the group is whole (u16), its first piece (u16), how many (u8); then a
///     tree of boxes (20-byte nodes written depth first; a node with 0 objects has two children
///     after it, one with more has that many 4-byte object numbers), 20 bytes and more run-time
///     data (its size at +12), and a buffer that starts with its own size. The last one ends where
///     the collision data starts, which checks the reading.
///   after the collision data: script points (116 bytes each), then from a multiple of 64 the
///     object ids, 808 bytes each in RFA (520 in Guerrilla): uid, name (24 bytes), the object's
///     number, 1 if it is breakable (else a general object; −1: the file as a whole,
///     as in stitch pieces), snap points.
/// A breakable object is drawn whole as its groups: each group's whole submesh, turned and moved
/// by the group's rotation and position. Its broken pieces are each group's pieces, placed the
/// same way. Submesh vertices are relative to their group (several groups can share one).
///
/// A general object is drawn as its submesh, as stored: its record's position and rotation are
/// where it sat in the file's own scene (the first mission's burnt-out LAV body 1.57 m up, on its
/// scene's ground), which the zone's placement already includes. Drawn with them, that body floated
/// 1.6 m over the ground the game shows it on, while the game reported it at the zone's position.
/// 368 of the game's 740 general objects have one (most a few cm; hanging snottites 5.8 m).
/// </summary>
public sealed class ChunkObjects
{
    public const uint Signature = 0xB0CEEFA5;

    /// <summary>The objects, in the order of the file's object ids.</summary>
    public IReadOnlyList<ChunkObject> Objects { get; }

    ChunkObjects(IReadOnlyList<ChunkObject> objects) => Objects = objects;

    /// <summary>The object with a uid, or null.</summary>
    public ChunkObject? WithUid(uint uid) => Objects.FirstOrDefault(o => o.Uid == uid);

    /// <summary>The objects of a destructible object's c-file. Throws <see cref="InvalidDataException"/> if they can't be read.</summary>
    /// <param name="submeshCount">How many submeshes the model has, to check the numbers read.</param>
    public static ChunkObjects Read(byte[] data, int submeshCount)
    {
        try
        {
            return ReadObjects(data, submeshCount);
        }
        catch (ArgumentOutOfRangeException)
        {
            throw new InvalidDataException("the objects run past the end of the file");
        }
    }

    static ChunkObjects ReadObjects(byte[] data, int submeshCount)
    {
        if (data.Length < 0x200 || U32(data, 0) != Signature) throw new InvalidDataException("not a destructible object file");
        int collision = I32(data, 0x1C), collisionSize = I32(data, 0x20), destruction = I32(data, 0x28);
        int generalCount = I32(data, 0x158), scriptPoints = I32(data, 0x180), idCount = I32(data, 0x18C);
        if (collision <= 0 || destruction <= 0 || destruction > collision || collision + collisionSize > data.Length ||
            generalCount is < 0 or > 10000 || scriptPoints is < 0 or > 10000 || idCount is < 0 or > 10000)
            throw new InvalidDataException("the header's offsets and counts don't fit the file");

        List<IReadOnlyList<ChunkPart>> general = ReadGeneralObjects(data, destruction, generalCount, submeshCount);
        List<(IReadOnlyList<ChunkPart> Whole, IReadOnlyList<ChunkPart> Pieces)> breakable = ReadDestroyables(data, destruction, collision, submeshCount);

        var objects = new List<ChunkObject>();
        int at = Align(Align(collision + collisionSize, 64) + scriptPoints * 116, 64);
        if (at + idCount * 808 > data.Length) throw new InvalidDataException("the object ids run past the end of the file");
        for (int i = 0; i < idCount; i++, at += 808)
        {
            uint uid = U32(data, at);
            string name = Encoding.ASCII.GetString(data, at + 4, 24).Split('\0')[0];
            int index = I32(data, at + 28);
            bool isBreakable = I32(data, at + 32) != 0;
            if (index == -1)  // the file as a whole (stitch pieces)
                objects.Add(new ChunkObject(uid, name, false, [], []));
            else if (isBreakable && index >= 0 && index < breakable.Count)
                objects.Add(new ChunkObject(uid, name, true, breakable[index].Whole, breakable[index].Pieces));
            else if (!isBreakable && index >= 0 && index < general.Count)
                objects.Add(new ChunkObject(uid, name, false, general[index], []));
            else
                throw new InvalidDataException($"object id {name} names {(isBreakable ? "breakable object" : "general object")} {index}, which isn't there");
        }
        return new ChunkObjects(objects);
    }

    /// <summary>General objects: 100-byte records right before their names, which end just before the destruction data.</summary>
    static List<IReadOnlyList<ChunkPart>> ReadGeneralObjects(byte[] data, int destruction, int count, int submeshCount)
    {
        var objects = new List<IReadOnlyList<ChunkPart>>();
        if (count == 0) return objects;
        int at = destruction - 1;
        while (at > 0 && data[at] == 0) at--;  // the padding after the last name
        for (int n = 0; n < count; n++)
        {
            while (at > 0 && data[at] != 0) at--;
            if (n < count - 1) at--;
        }
        int names = at + 1, records = names - count * 100;
        if (records < 0) throw new InvalidDataException("the general objects don't fit before the destruction data");
        for (int i = 0; i < count; i++)
        {
            int r = records + i * 100;
            int submesh = BitConverter.ToInt16(data, r + 82);
            if (submesh >= submeshCount) throw new InvalidDataException($"general object {i} uses submesh {submesh} of {submeshCount}");
            // Its position (r + 28) and rotation (r + 40) in the file's own scene aren't used (see above).
            objects.Add(submesh < 0 ? [] : [new ChunkPart(submesh, Vector3.Zero, Matrix4x4.Identity)]);
        }
        return objects;
    }

    static List<(IReadOnlyList<ChunkPart> Whole, IReadOnlyList<ChunkPart> Pieces)> ReadDestroyables(byte[] data, int destruction, int collision, int submeshCount)
    {
        var objects = new List<(IReadOnlyList<ChunkPart>, IReadOnlyList<ChunkPart>)>();
        int at = Align(destruction, 128);
        int count = I32(data, at);
        if (count is < 0 or > 10000) throw new InvalidDataException("bad breakable object count");
        at += 4 + count * 4;
        for (int d = 0; d < count; d++)
        {
            at = Align(at, 16);
            int pieceCount = I32(data, at + 12), linkCount = I32(data, at + 20), groupCount = I32(data, at + 28);
            if (pieceCount is < 0 or > 100000 || linkCount is < 0 or > 1000000 || groupCount is < 0 or > 100000)
                throw new InvalidDataException($"breakable object {d}: bad counts");
            at = Align(at + 44, 128);
            int pieces = at;
            at += pieceCount * 64;
            int pieceData = at;
            at += pieceCount * 12;
            for (int p = 0; p < pieceCount; p++) at += data[pieces + p * 64 + 62] * 2;  // each piece's link numbers
            at = Align(at, 4) + linkCount * 16;
            at = Align(at, 4);

            var whole = new List<ChunkPart>();
            var broken = new List<ChunkPart>();
            for (int g = 0; g < groupCount; g++, at += 60)
            {
                Vector3 position = Vector3At(data, at + 4);
                Matrix4x4 rotation = RotationAt(data, at + 16);
                int submesh = BitConverter.ToUInt16(data, at + 52), first = BitConverter.ToUInt16(data, at + 54), many = data[at + 56];
                if (submesh >= submeshCount || first + many > pieceCount) throw new InvalidDataException($"breakable object {d}, group {g}: numbers out of range");
                whole.Add(new ChunkPart(submesh, position, rotation));
                for (int p = first; p < first + many; p++)
                {
                    int pieceSubmesh = BitConverter.ToUInt16(data, pieceData + p * 12 + 6);
                    if (pieceSubmesh < submeshCount) broken.Add(new ChunkPart(pieceSubmesh, position, rotation));
                }
            }
            at = SkipBoxTree(data, Align(at, 4));
            at = Align(at, 4);
            at += 20 + I32(data, at + 12);  // run-time data and its buffer
            at = Align(at, 16);
            at += I32(data, at);  // the transform buffer, which starts with its own size
            at = Align(at, 16);
            objects.Add((whole, broken));
        }
        if (Align(at, 64) != collision) throw new InvalidDataException("the breakable objects don't end where the collision data starts");
        return objects;
    }

    /// <summary>Skips a tree of 20-byte box nodes written depth first; returns where it ends.</summary>
    static int SkipBoxTree(byte[] data, int root)
    {
        int end = root + 20;
        void Walk(int node)
        {
            if (I32(data, node) < 0) return;
            while (I32(data, node) == 0)  // a node with two children, written next
            {
                if (end + 40 > data.Length) throw new InvalidDataException("the box tree runs past the end of the file");
                int children = end;
                end += 40;
                Walk(children);
                int second = children + 20;
                if (I32(data, second) < 0) return;
                node = second;
            }
            end += I32(data, node) * 4;  // a leaf: its object numbers
        }
        Walk(root);
        return end;
    }

    static Vector3 Vector3At(byte[] data, int at) => new(F32(data, at), F32(data, at + 4), F32(data, at + 8));

    /// <summary>A rotation stored as 9 floats, rows right, up, forward (as zone objects' rotations).</summary>
    static Matrix4x4 RotationAt(byte[] data, int at) => new(
        F32(data, at), F32(data, at + 4), F32(data, at + 8), 0,
        F32(data, at + 12), F32(data, at + 16), F32(data, at + 20), 0,
        F32(data, at + 24), F32(data, at + 28), F32(data, at + 32), 0,
        0, 0, 0, 1);

    static int Align(int value, int to) => (value + to - 1) / to * to;
    static uint U32(byte[] data, int at) => BitConverter.ToUInt32(data, at);
    static int I32(byte[] data, int at) => BitConverter.ToInt32(data, at);
    static float F32(byte[] data, int at) => BitConverter.ToSingle(data, at);
}

/// <summary>One object in a destructible object file.</summary>
/// <param name="Uid">The number zone objects name it by.</param>
/// <param name="Breakable">Whether it breaks into pieces (else it is one mesh).</param>
/// <param name="Whole">What is drawn while it is whole (empty for the file as a whole: draw its first submesh).</param>
/// <param name="Pieces">What it breaks into (none for general objects).</param>
public sealed record ChunkObject(uint Uid, string Name, bool Breakable, IReadOnlyList<ChunkPart> Whole, IReadOnlyList<ChunkPart> Pieces);

/// <summary>A submesh drawn turned and moved within its object (rotation rows: right, up, forward).</summary>
public readonly record struct ChunkPart(int Submesh, Vector3 Position, Matrix4x4 Rotation)
{
    /// <summary>The part's placement as one matrix (points as rows: p × rotation + position).</summary>
    public Matrix4x4 Transform => Rotation with { M41 = Position.X, M42 = Position.Y, M43 = Position.Z };
}
