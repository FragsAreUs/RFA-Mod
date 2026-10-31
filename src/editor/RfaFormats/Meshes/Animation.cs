using System.Numerics;

namespace RfaFormats.Meshes;

/// <summary>
/// An animation (.anim_pc): how each bone of a skeleton (see <see cref="Rig"/>) turns and moves
/// over time, and how the whole character moves over the clip. 1,409 in anims.vpp_pc, 1,719
/// different in all. Read the way the game's own code reads them (its animation library: the
/// block reader at 0xD7B260, the rotation keys at 0xD87AC0, the position keys at 0xD88BE0).
///
/// Header:
///   0x00   "ANIM"
///   0x04   version (10)
///   0x05   flags: 0x02 key times are u16 (else u8); 0x10 an 8-byte extension at 0x30 (not used
///          here); 0x20 scale keys (3 floats each; nearly always 1, 1, 1); 0x40 weight curves (a
///          number from 0 to 1 over time per bone, probably how strongly it follows this animation
///          when blended over another: flinches weigh only the upper body); 0x80 key counts are u16
///          (else u8)
///   0x06   length in frames (u16); the game counts 80 time units a frame and 4,800 a second, so
///          60 frames a second
///   0x0A   how many bones it animates (u8); 0x0B how many bones its skeleton has (u8)
///   0x18   1.0; 0x1C the character's movement over the whole clip (3 floats)
///   0x28   where the list of animated bones starts (u32: one byte per bone, its number in the
///          skeleton), or 0 when it animates every bone, in order (no list)
///   0x2C   where the character's own movement (the root track) starts (u32)
///
/// After the list of bones (or the header: 0x30, or 0x38 with flag 0x10): weight curves if flag
/// 0x40 (a key count per bone, then all the values as floats from a 4-byte boundary, then their
/// times), then one block of keys per animated bone, in the list's order. At 0x2C: a weight curve
/// for one more track (flag 0x40), and its block, the character's own movement, to the end of the
/// file. A block is:
///   rotation key count, position key count, and with flag 0x20 the scale key count (u8 each, or
///     u16 from a 2-byte boundary with flag 0x80)
///   rotation runs, 4 bytes each, until they cover all rotation keys (see <see cref="ReadRotations"/>)
///   rotation keys, 3 bytes each; their times (u8, or u16 from a 2-byte boundary with flag 0x02)
///   from a 2-byte boundary, position runs, 8 bytes each: key count (u16) and a starting point
///     (3 × s16, in 1/64 m), until they cover all position keys
///   position keys, 3 bytes each (s8, in 1/4000 m from their run's starting point); their times
///   with flag 0x20, from a 4-byte boundary: the scale keys (3 floats each), then their times
/// A bone with no rotation keys keeps its rest pose's rotation (none); one with no position keys
/// stays at its rest offset from its parent. Position keys are added to that offset.
///
/// All 1,719 read to their exact end but one (plyr_wklr_dr_enter in mar_heavy_walker.ccar_pc.str2_pc,
/// whose keys don't follow this layout). A few tracks repeat their first key.
/// </summary>
public sealed class Animation
{
    /// <summary>Frames a second: the game counts 4,800 time units a second and 80 a frame.</summary>
    public const float FramesPerSecond = 60;

    public int Flags { get; }
    /// <summary>Length in frames (<see cref="FramesPerSecond"/> a second).</summary>
    public int Frames { get; }
    /// <summary>How many bones the skeleton it was made for has.</summary>
    public int SkeletonBones { get; }
    /// <summary>How far the character moves over the whole clip (from the header).</summary>
    public Vector3 Movement { get; }
    /// <summary>One track per bone it animates.</summary>
    public IReadOnlyList<AnimationTrack> Tracks { get; }
    /// <summary>The character's own movement over the clip (walking forward, jumping down), as positions; its bone is -1.</summary>
    public AnimationTrack Root { get; }

    public float Seconds => Frames / FramesPerSecond;

    Animation(int flags, int frames, int skeletonBones, Vector3 movement, List<AnimationTrack> tracks, AnimationTrack root)
    {
        Flags = flags;
        Frames = frames;
        SkeletonBones = skeletonBones;
        Movement = movement;
        Tracks = tracks;
        Root = root;
    }

    public static Animation Read(byte[] data)
    {
        if (data.Length < 0x30 || data[0] != 'A' || data[1] != 'N' || data[2] != 'I' || data[3] != 'M')
            throw new InvalidDataException("not an animation (no \"ANIM\" at the start)");
        if (data[4] != 10) throw new InvalidDataException($"animation version {data[4]}; only version 10 is known");
        try
        {
            return ReadKeys(data);
        }
        catch (Exception ex) when (ex is IndexOutOfRangeException or ArgumentException)
        {
            throw new InvalidDataException("the animation's keys run past the end of the file (a layout this reader doesn't know)", ex);
        }
    }

    static Animation ReadKeys(byte[] data)
    {
        var reader = new Reader(data, data[5]);
        int frames = BitConverter.ToUInt16(data, 6);
        int animated = data[0xA], skeletonBones = data[0xB];
        var movement = new Vector3(BitConverter.ToSingle(data, 0x1C), BitConverter.ToSingle(data, 0x20), BitConverter.ToSingle(data, 0x24));
        int bonesAt = BitConverter.ToInt32(data, 0x28), rootAt = BitConverter.ToInt32(data, 0x2C);
        int keysAt = bonesAt != 0 ? bonesAt + animated : (data[5] & 0x10) != 0 ? 0x38 : 0x30;
        if (bonesAt is < 0 or > 0 and < 0x30 || keysAt > data.Length || rootAt < keysAt || rootAt > data.Length)
            throw new InvalidDataException("the animation's bone list or root track is outside the file");

        reader.At = keysAt;
        var weights = reader.ReadWeights(animated);
        var tracks = new List<AnimationTrack>(animated);
        for (int i = 0; i < animated; i++)
            tracks.Add(reader.ReadBlock(bonesAt != 0 ? data[bonesAt + i] : i, weights[i]));
        if (reader.At != rootAt)
            throw new InvalidDataException($"the bones' keys end at 0x{reader.At:X}, not where the root track starts (0x{rootAt:X})");

        reader.At = rootAt;
        AnimationTrack root = reader.ReadBlock(-1, reader.ReadWeights(1)[0]);
        if (reader.At != data.Length)
            throw new InvalidDataException($"the root track ends at 0x{reader.At:X}, not at the end of the file (0x{data.Length:X})");
        return new Animation(data[5], frames, skeletonBones, movement, tracks, root);
    }

    /// <summary>
    /// Where every bone of a skeleton is at a moment of the clip: each bone's transform from its own
    /// space (where its rest position is the origin, as in the rest pose) to the model's, so a point
    /// of the model in the rest pose, minus the bone's rest position, times this, is where that point
    /// is now. With <paramref name="move"/>, the character also moves along the root track (else it
    /// stays on the spot, as a run cycle seen on a treadmill).
    /// </summary>
    public Matrix4x4[] Pose(Rig rig, float frame, bool move = false)
    {
        var tracks = new AnimationTrack?[rig.Bones.Count];
        foreach (AnimationTrack track in Tracks)
            if (track.Bone < tracks.Length) tracks[track.Bone] = track;

        var world = new Matrix4x4[rig.Bones.Count];
        for (int i = 0; i < rig.Bones.Count; i++)
        {
            RigBone bone = rig.Bones[i];
            AnimationTrack? track = tracks[i];
            Vector3 scale = track?.ScaleAt(frame) ?? Vector3.One;
            Quaternion rotation = track?.RotationAt(frame) ?? Quaternion.Identity;
            Vector3 offset = bone.Offset + (track?.PositionAt(frame) ?? Vector3.Zero);
            Matrix4x4 local = Matrix4x4.CreateScale(scale) * Matrix4x4.CreateFromQuaternion(rotation) * Matrix4x4.CreateTranslation(offset);
            world[i] = bone.Parent >= 0 ? local * world[bone.Parent] : local;
        }
        if (move)
        {
            Matrix4x4 along = Matrix4x4.CreateTranslation(Root.PositionAt(frame));
            for (int i = 0; i < world.Length; i++) world[i] *= along;
        }
        return world;
    }

    /// <summary>
    /// The rotation keys of a block, as the game decodes them (0xD87AC0). Keys come in runs; each
    /// run has a 4-byte header:
    ///   bytes 0-2  bit 7: whether that axis (x, y, z) takes big steps; bits 0-6: the axis's
    ///              starting value's top bits (signed: value × 256)
    ///   byte 3     bits 0-3: the run's key count - 1; bit 4: w is negative; bits 7, 6, 5: whether
    ///              x, y, z take doubled steps. A step is 1, 2 (doubled), 4 (big) or 32 (big, doubled)
    /// Each key is 3 signed bytes, how many steps x, y and z change from the run's previous key (the
    /// first key's from the starting value). An axis is (start × 256 + steps × step), cut to 16 bits,
    /// over 16383; w makes the rotation's length 1. The game's rotations turn the other way from
    /// System.Numerics' (as read, a run cycle swings its feet out sideways and bends its knees
    /// forwards; turned round, the feet go forward and back and the knees bend as knees do), so each
    /// is kept turned round: (-x, -y, -z, w).
    /// </summary>
    static Quaternion[] ReadRotations(byte[] data, int runsAt, int keysAt, int count)
    {
        var rotations = new Quaternion[count];
        int key = 0;
        for (int run = runsAt; key < count; run += 4)
        {
            int keysInRun = (data[run + 3] & 0x0F) + 1;
            bool negativeW = (data[run + 3] & 0x10) != 0;
            var start = new int[3];
            var step = new int[3];
            for (int axis = 0; axis < 3; axis++)
            {
                start[axis] = (sbyte)(data[run + axis] << 1) >> 1;  // the low 7 bits, signed
                bool big = (data[run + axis] & 0x80) != 0, doubled = (data[run + 3] & (0x80 >> axis)) != 0;
                step[axis] = big ? (doubled ? 32 : 4) : (doubled ? 2 : 1);
            }
            var steps = new int[3];
            for (int k = 0; k < keysInRun && key < count; k++, key++)
            {
                var value = new float[3];
                for (int axis = 0; axis < 3; axis++)
                {
                    steps[axis] += (sbyte)data[keysAt + key * 3 + axis];
                    value[axis] = (short)(start[axis] * 256 + steps[axis] * step[axis]) / 16383f;
                }
                float lengthSquared = Math.Min(1, value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
                float w = MathF.Sqrt(1 - lengthSquared);
                rotations[key] = new Quaternion(-value[0], -value[1], -value[2], negativeW ? -w : w);
            }
        }
        return rotations;
    }

    /// <summary>Reads the parts of an animation file in order, as the game's block reader (0xD7B260) does.</summary>
    sealed class Reader(byte[] data, int flags)
    {
        public int At;
        bool WideCounts => (flags & 0x80) != 0;
        bool WideTimes => (flags & 0x02) != 0;

        void Align(int boundary) => At = (At + boundary - 1) / boundary * boundary;

        int Count()
        {
            if (!WideCounts) return data[At++];
            int count = BitConverter.ToUInt16(data, At);
            At += 2;
            return count;
        }

        int[] Times(int count)
        {
            if (WideTimes) Align(2);
            var times = new int[count];
            for (int i = 0; i < count; i++)
                times[i] = WideTimes ? BitConverter.ToUInt16(data, At + 2 * i) : data[At + i];
            At += count * (WideTimes ? 2 : 1);
            return times;
        }

        /// <summary>Weight curves (flag 0x40): a key count per track, then every value, then every time. None without the flag.</summary>
        public List<AnimationKey<float>>[] ReadWeights(int tracks)
        {
            var curves = new List<AnimationKey<float>>[tracks];
            for (int i = 0; i < tracks; i++) curves[i] = [];
            if ((flags & 0x40) == 0) return curves;
            var counts = new int[tracks];
            for (int i = 0; i < tracks; i++) counts[i] = Count();  // the game doesn't align these
            int total = counts.Sum();
            Align(4);
            int valuesAt = At;
            At += 4 * total;
            int[] times = Times(total);
            for (int i = 0, key = 0; i < tracks; i++)
                for (int k = 0; k < counts[i]; k++, key++)
                    curves[i].Add(new(times[key], BitConverter.ToSingle(data, valuesAt + 4 * key)));
            return curves;
        }

        public AnimationTrack ReadBlock(int bone, List<AnimationKey<float>> weights)
        {
            if (WideCounts) Align(2);
            int rotationCount = Count(), positionCount = Count();
            int scaleCount = (flags & 0x20) != 0 ? Count() : 0;

            // Rotations: runs until they cover the keys, then the keys, then their times.
            int runsAt = At;
            for (int covered = 0; covered < rotationCount; At += 4)
                covered += (data[At + 3] & 0x0F) + 1;
            int keysAt = At;
            At += rotationCount * 3;
            Quaternion[] rotations = ReadRotations(data, runsAt, keysAt, rotationCount);
            int[] rotationTimes = Times(rotationCount);

            // Positions: runs of (count, starting point), then the keys, then their times.
            Align(2);
            var positionStarts = new List<(int Count, Vector3 Start)>();
            for (int covered = 0; covered < positionCount; At += 8)
            {
                int count = BitConverter.ToUInt16(data, At);
                positionStarts.Add((count, new Vector3(BitConverter.ToInt16(data, At + 2), BitConverter.ToInt16(data, At + 4), BitConverter.ToInt16(data, At + 6)) / 64f));
                covered += count;
            }
            var positions = new Vector3[positionCount];
            int key = 0;
            foreach (var (count, start) in positionStarts)
                for (int k = 0; k < count && key < positionCount; k++, key++)
                    positions[key] = start + new Vector3((sbyte)data[At + key * 3], (sbyte)data[At + key * 3 + 1], (sbyte)data[At + key * 3 + 2]) / 4000f;
            At += positionCount * 3;
            int[] positionTimes = Times(positionCount);

            // Scales (flag 0x20): plain floats.
            var scales = new Vector3[scaleCount];
            if (scaleCount > 0)
            {
                Align(4);
                for (int i = 0; i < scaleCount; i++)
                    scales[i] = new Vector3(BitConverter.ToSingle(data, At + 12 * i), BitConverter.ToSingle(data, At + 12 * i + 4), BitConverter.ToSingle(data, At + 12 * i + 8));
                At += 12 * scaleCount;
            }
            int[] scaleTimes = scaleCount > 0 ? Times(scaleCount) : [];

            return new AnimationTrack(bone,
                rotations.Select((r, i) => new AnimationKey<Quaternion>(rotationTimes[i], r)).ToList(),
                positions.Select((p, i) => new AnimationKey<Vector3>(positionTimes[i], p)).ToList(),
                scales.Select((s, i) => new AnimationKey<Vector3>(scaleTimes[i], s)).ToList(),
                weights);
        }
    }
}

/// <summary>A key: a value at a frame.</summary>
public readonly record struct AnimationKey<T>(int Frame, T Value);

/// <summary>
/// One bone's keys: how it turns (in its parent's space), how far it moves from its rest offset
/// from its parent, its scale (rarely anything but 1), and its weight (see <see cref="Animation"/>).
/// Between keys, rotations turn evenly (slerp) and the rest change in a straight line, as in the
/// game; before the first key and after the last, the nearest key holds.
/// </summary>
public sealed class AnimationTrack(int bone, IReadOnlyList<AnimationKey<Quaternion>> rotations, IReadOnlyList<AnimationKey<Vector3>> positions,
                                   IReadOnlyList<AnimationKey<Vector3>> scales, IReadOnlyList<AnimationKey<float>> weights)
{
    /// <summary>The bone's number in the skeleton; -1 for the character's own movement (the root track).</summary>
    public int Bone { get; } = bone;
    public IReadOnlyList<AnimationKey<Quaternion>> Rotations { get; } = rotations;
    public IReadOnlyList<AnimationKey<Vector3>> Positions { get; } = positions;
    public IReadOnlyList<AnimationKey<Vector3>> Scales { get; } = scales;
    public IReadOnlyList<AnimationKey<float>> Weights { get; } = weights;

    public Quaternion RotationAt(float frame)
    {
        if (Rotations.Count == 0) return Quaternion.Identity;
        var (a, b, t) = Between(Rotations, frame);
        return Quaternion.Slerp(Rotations[a].Value, Rotations[b].Value, t);
    }

    public Vector3 PositionAt(float frame)
    {
        if (Positions.Count == 0) return Vector3.Zero;
        var (a, b, t) = Between(Positions, frame);
        return Vector3.Lerp(Positions[a].Value, Positions[b].Value, t);
    }

    public Vector3 ScaleAt(float frame)
    {
        if (Scales.Count == 0) return Vector3.One;
        var (a, b, t) = Between(Scales, frame);
        return Vector3.Lerp(Scales[a].Value, Scales[b].Value, t);
    }

    /// <summary>The keys on either side of a frame and how far between them it is (0 to 1).</summary>
    static (int, int, float) Between<T>(IReadOnlyList<AnimationKey<T>> keys, float frame)
    {
        if (frame <= keys[0].Frame) return (0, 0, 0);
        if (frame >= keys[^1].Frame) return (keys.Count - 1, keys.Count - 1, 0);
        int low = 0, high = keys.Count - 1;
        while (high - low > 1)
        {
            int middle = (low + high) / 2;
            if (keys[middle].Frame <= frame) low = middle;
            else high = middle;
        }
        return (low, high, (frame - keys[low].Frame) / (keys[high].Frame - keys[low].Frame));
    }
}
