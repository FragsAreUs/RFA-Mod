using System.Numerics;
using System.Text;

namespace RfaFormats.Meshes;

/// <summary>
/// A skeleton (.rig_pc): the bones a character (or anything animated) is moved by, in its rest
/// pose, and its tags (points things attach to: weapons, the camera). character.xtbl names each
/// character's rig ("x.rigx" is the file x.rig_pc); 178 in the game, all in misc.vpp_pc. Layout:
///
///   0x00     zeros (filled in at run time)
///   0x24     bone count; 0x28 and 0x2C two counts that add up to it (not known yet); 0x30 tag count
///   0x40     a hash per bone (u32)
///   then     the bones, 36 bytes each: where its name starts in the names (u32), its rest position
///            negated (3 floats), its offset from its parent (3 floats), its parent (-1 for none),
///            and -1. A bone's position is its parent's plus its offset (true of every bone of all
///            178 rigs); the rest pose has no rotations.
///   then     the tags, 60 bytes each: where its name starts, its rotation (rows: right, up,
///            forward), its position (from its bone), its bone, and -1
///   then     the names, NUL-ended
/// </summary>
public sealed class Rig
{
    public IReadOnlyList<RigBone> Bones { get; }
    public IReadOnlyList<RigTag> Tags { get; }

    Rig(List<RigBone> bones, List<RigTag> tags)
    {
        Bones = bones;
        Tags = tags;
    }

    public static Rig Read(byte[] data)
    {
        if (data.Length < 0x40) throw new InvalidDataException("too short for a rig");
        int boneCount = BitConverter.ToInt32(data, 0x24), tagCount = BitConverter.ToInt32(data, 0x30);
        if (boneCount is <= 0 or > 4096 || tagCount is < 0 or > 4096) throw new InvalidDataException("not a rig (bone or tag count out of range)");
        int bonesAt = 0x40 + 4 * boneCount, tagsAt = bonesAt + 36 * boneCount, namesAt = tagsAt + 60 * tagCount;
        if (namesAt > data.Length) throw new InvalidDataException("the rig's bones and tags run past its end");

        string Name(int offset) => namesAt + offset < data.Length
            ? Encoding.Latin1.GetString(data, namesAt + offset, Math.Max(0, Array.IndexOf(data, (byte)0, namesAt + offset) - namesAt - offset))
            : "";
        Vector3 ReadVector(int at) => new(BitConverter.ToSingle(data, at), BitConverter.ToSingle(data, at + 4), BitConverter.ToSingle(data, at + 8));

        var bones = new List<RigBone>(boneCount);
        for (int i = 0; i < boneCount; i++)
        {
            int at = bonesAt + 36 * i;
            int parent = BitConverter.ToInt32(data, at + 28);
            if (parent >= i || parent < -1) throw new InvalidDataException($"bone {i}'s parent ({parent}) isn't an earlier bone");
            bones.Add(new RigBone(Name(BitConverter.ToInt32(data, at)), parent, -ReadVector(at + 4), ReadVector(at + 16)));
        }
        var tags = new List<RigTag>(tagCount);
        for (int i = 0; i < tagCount; i++)
        {
            int at = tagsAt + 60 * i;
            var rotation = new Matrix4x4(
                BitConverter.ToSingle(data, at + 4), BitConverter.ToSingle(data, at + 8), BitConverter.ToSingle(data, at + 12), 0,
                BitConverter.ToSingle(data, at + 16), BitConverter.ToSingle(data, at + 20), BitConverter.ToSingle(data, at + 24), 0,
                BitConverter.ToSingle(data, at + 28), BitConverter.ToSingle(data, at + 32), BitConverter.ToSingle(data, at + 36), 0,
                0, 0, 0, 1);
            int bone = BitConverter.ToInt32(data, at + 52);
            tags.Add(new RigTag(Name(BitConverter.ToInt32(data, at)), bone, rotation, ReadVector(at + 40)));
        }
        return new Rig(bones, tags);
    }
}

/// <summary>A bone: its name, its parent (-1 for none), where it is in the rest pose, and its offset from its parent.</summary>
public sealed record RigBone(string Name, int Parent, Vector3 Position, Vector3 Offset);

/// <summary>A tag: a named point things attach to, on a bone, turned and placed relative to it.</summary>
public sealed record RigTag(string Name, int Bone, Matrix4x4 Rotation, Vector3 Position);
