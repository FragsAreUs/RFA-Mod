using System.Numerics;
using System.Text;

namespace RfaFormats.Effects;

/// <summary>
/// A key curve of an emitter: values over the effect's time (its keys' times in seconds), each value 1 to 4 numbers
/// (Guerrilla's PDB: vfx_float_key {time, value}, vfx_vector_key {time, vector}, vfx_quat_key {time, quaternion}).
/// Between keys the value is blended in a straight line; before the first and after the last it holds.
/// </summary>
public sealed class KeyCurve
{
    readonly float[] times;
    readonly float[][] values;

    public KeyCurve(float[] times, float[][] values)
    {
        this.times = times;
        this.values = values;
    }

    public static readonly KeyCurve Zero = new([0], [[0, 0, 0, 0]]);

    public int Count => times.Length;

    public float[] At(float time)
    {
        if (times.Length == 0) return [0, 0, 0, 0];
        if (time <= times[0] || times.Length == 1) return values[0];
        for (int k = 1; k < times.Length; k++)
        {
            if (time > times[k]) continue;
            float span = times[k] - times[k - 1];
            float f = span <= 0 ? 1 : (time - times[k - 1]) / span;
            var blended = new float[values[k].Length];
            for (int i = 0; i < blended.Length; i++) blended[i] = values[k - 1][i] + (values[k][i] - values[k - 1][i]) * f;
            return blended;
        }
        return values[^1];
    }

    public float Number(float time) => At(time)[0];

    /// <summary>
    /// Reads <paramref name="count"/> keys at <paramref name="at"/> (an offset from the file's start, −1 for none), each
    /// <paramref name="keySize"/> bytes: its time first, its <paramref name="numbers"/> values at <paramref name="valueAt"/>
    /// in it. <paramref name="fallback"/> when there are none.
    /// </summary>
    public static KeyCurve Read(byte[] bytes, int count, uint at, int numbers, int keySize, int valueAt, KeyCurve fallback)
    {
        if (count <= 0 || count > 100_000 || at == 0xFFFFFFFF || at + (long)count * keySize > bytes.Length) return fallback;
        var times = new float[count];
        var values = new float[count][];
        for (int k = 0; k < count; k++)
        {
            int key = (int)at + k * keySize;
            times[k] = BitConverter.ToSingle(bytes, key);
            values[k] = new float[numbers];
            for (int i = 0; i < numbers; i++) values[k][i] = BitConverter.ToSingle(bytes, key + valueAt + 4 * i);
        }
        return new KeyCurve(times, values);
    }

    public static KeyCurve Constant(params float[] value) => new([0], [value]);
    public Vector4 Vector4(float time) => At(time) is var v && v.Length >= 4 ? new Vector4(v[0], v[1], v[2], v[3]) : System.Numerics.Vector4.One;
    public Vector3 Vector(float time) => At(time) is var v && v.Length >= 3 ? new Vector3(v[0], v[1], v[2]) : Vector3.Zero;

    public Quaternion Rotation(float time)
    {
        float[] v = At(time);
        if (v.Length < 4) return Quaternion.Identity;
        var q = new Quaternion(v[0], v[1], v[2], v[3]);
        return q.LengthSquared() < 1e-6f ? Quaternion.Identity : Quaternion.Normalize(q);
    }
}

/// <summary>
/// A value over a particle's life (0 at its birth, 1 at its end): start, then mid A and mid B at their offsets, then end;
/// without the mid values, straight from start to end.
/// </summary>
public readonly record struct LifeCurve(float Start, float MidA, float MidAOffset, float MidB, float MidBOffset, float End, bool UseMid)
{
    public float At(float life)
    {
        if (!UseMid) return Start + (End - Start) * life;
        float a = Math.Clamp(MidAOffset, 0, 1), b = Math.Clamp(MidBOffset, a, 1);
        if (life <= a) return a <= 0 ? MidA : Start + (MidA - Start) * (life / a);
        if (life <= b) return b <= a ? MidB : MidA + (MidB - MidA) * ((life - a) / (b - a));
        return b >= 1 ? End : MidB + (End - MidB) * ((life - b) / (1 - b));
    }
}

/// <summary>
/// Everything an emitter's block says about how its particles are made and move, as far as it is known (the block's
/// fields placed against Guerrilla's emitter in docs\game-notes.md, "Effects"; the curves in the PDB's order).
/// Distances in metres, times in seconds, Y up.
/// </summary>
public sealed class EmitterSettings
{
    // The 28 curves at +0x200: (count, offset) each; the first three are a vector, a rotation and a vector.
    const int Position = 0, RotationCurve = 1, Scale = 2, EmissionRate = 3, AngularStart = 4, AngularEnd = 5, Speed = 6,
              TextureAnimationRate = 7, SizeStart = 8, SizeStartVar = 9, SizeMidA = 10, SizeMidAVar = 11, SizeMidAOffset = 12, SizeMidB = 13, SizeMidBVar = 14,
              SizeMidBOffset = 15, SizeEnd = 16, SizeEndVar = 17, LengthStart = 18, LengthStartVar = 19, LengthMidA = 20,
              LengthMidAVar = 21, LengthMidAOffset = 22, LengthMidB = 23, LengthMidBVar = 24, LengthMidBOffset = 25, LengthEnd = 26,
              LengthEndVar = 27;

    public required string Name { get; init; }
    public required int ParticleType { get; init; }
    public required string Shape { get; init; }
    public required float[] ShapeSize { get; init; }  // sphere: radius; box: x y z; cylinder: radius, height; plane: x z
    public required int Parent { get; init; }
    public required KeyCurve[] Curves { get; init; }
    public required float WeightStart { get; init; }
    public required float WeightEnd { get; init; }
    public required bool RotationEnabled { get; init; }
    public required bool RandomStartRotation { get; init; }
    public required bool UseLength { get; init; }
    public required bool UseLengthMid { get; init; }
    public required bool UseSizeMid { get; init; }
    public required bool UseColourMid { get; init; }
    public required bool UseSaturation { get; init; }
    public required bool UseAdditive { get; init; }
    public required bool AccelerationEnabled { get; init; }
    public required bool AccelerationVariance { get; init; }
    public required bool UseLifetimeVariance { get; init; }
    public required bool UseSizeVariance { get; init; }
    public required bool UseLengthVariance { get; init; }
    public required Vector3 AccelerationStart { get; init; }
    public required Vector3 AccelerationStartVariance { get; init; }
    public required Vector3 AccelerationEnd { get; init; }
    public required Vector3 AccelerationEndVariance { get; init; }
    public required Vector4 ColourStart { get; init; }
    public required Vector4 ColourMidA { get; init; }
    public required float ColourMidAOffset { get; init; }
    public required Vector4 ColourMidB { get; init; }
    public required float ColourMidBOffset { get; init; }
    public required Vector4 ColourEnd { get; init; }
    public required float AlphaMidAOffset { get; init; }
    public required float AlphaMidBOffset { get; init; }
    public required LifeCurve Saturation { get; init; }
    public required LifeCurve Additive { get; init; }
    public required float YawVariance { get; init; }
    public required float PitchVariance { get; init; }
    public required float SpeedVariance { get; init; }
    public required float AngularStartVariance { get; init; }
    public required float AngularEndVariance { get; init; }
    public required float Lifetime { get; init; }
    public required float LifetimeVariance { get; init; }
    public required int MaxParticles { get; init; }

    /// <summary>For a node emitter (particle type 6, its particles not drawn): the emitters that send their particles
    /// from its particles as they fly (block +0x1D8 count, +0x1DC offset of their numbers). A ribbon among them strings
    /// the particles from each node into a trail.</summary>
    public required int[] Carries { get; init; }

    /// <summary>For mesh particles (type 10): the effect's own mesh each particle is (head +0x04 points to data starting
    /// with its name), or null.</summary>
    public required string? MeshName { get; init; }

    public KeyCurve PositionKeys => Curves[Position];
    public KeyCurve RotationKeys => Curves[RotationCurve];
    public KeyCurve ScaleKeys => Curves[Scale];
    public KeyCurve EmissionRateKeys => Curves[EmissionRate];
    public KeyCurve SpeedKeys => Curves[Speed];
    public KeyCurve AngularStartKeys => Curves[AngularStart];
    public KeyCurve AngularEndKeys => Curves[AngularEnd];

    /// <summary>How a flip-book texture plays: 1 through its frames once over a particle's life, 2 twice, ...; 0 one
    /// frame, picked at random for each particle.</summary>
    public KeyCurve TextureAnimationRateKeys => Curves[TextureAnimationRate];

    /// <summary>A particle's size over its life as the curves give it at <paramref name="time"/> (the effect's), with
    /// each value moved by up to its variance (<paramref name="random"/>: −1 to 1, four of them).</summary>
    public LifeCurve SizeAt(float time, ReadOnlySpan<float> random) => new(
        Varied(SizeStart, SizeStartVar, time, random[0], UseSizeVariance), Varied(SizeMidA, SizeMidAVar, time, random[1], UseSizeVariance),
        Curves[SizeMidAOffset].Number(time), Varied(SizeMidB, SizeMidBVar, time, random[2], UseSizeVariance),
        Curves[SizeMidBOffset].Number(time), Varied(SizeEnd, SizeEndVar, time, random[3], UseSizeVariance), UseSizeMid);

    public LifeCurve LengthAt(float time, ReadOnlySpan<float> random) => new(
        Varied(LengthStart, LengthStartVar, time, random[0], UseLengthVariance), Varied(LengthMidA, LengthMidAVar, time, random[1], UseLengthVariance),
        Curves[LengthMidAOffset].Number(time), Varied(LengthMidB, LengthMidBVar, time, random[2], UseLengthVariance),
        Curves[LengthMidBOffset].Number(time), Varied(LengthEnd, LengthEndVar, time, random[3], UseLengthVariance), UseLengthMid);

    float Varied(int curve, int variance, float time, float random, bool use) =>
        Math.Max(0, Curves[curve].Number(time) + (use ? Curves[variance].Number(time) * random : 0));

    /// <summary>The colour (red, green, blue) and alpha over a particle's life: alpha keeps its own mid offsets.</summary>
    public Vector4 ColourAt(float life)
    {
        static float Channel(float s, float a, float aOff, float b, float bOff, float e, bool mid, float life) =>
            new LifeCurve(s, a, aOff, b, bOff, e, mid).At(life);
        return new Vector4(
            Channel(ColourStart.X, ColourMidA.X, ColourMidAOffset, ColourMidB.X, ColourMidBOffset, ColourEnd.X, UseColourMid, life),
            Channel(ColourStart.Y, ColourMidA.Y, ColourMidAOffset, ColourMidB.Y, ColourMidBOffset, ColourEnd.Y, UseColourMid, life),
            Channel(ColourStart.Z, ColourMidA.Z, ColourMidAOffset, ColourMidB.Z, ColourMidBOffset, ColourEnd.Z, UseColourMid, life),
            Channel(ColourStart.W, ColourMidA.W, AlphaMidAOffset, ColourMidB.W, AlphaMidBOffset, ColourEnd.W, UseColourMid, life));
    }

    const uint None = 0xFFFFFFFF;

    /// <summary>Reads emitter number <paramref name="index"/> of a version-36 effect (check it with <see cref="EffectFile.Read"/> first).</summary>
    public static EmitterSettings Read(byte[] bytes, int index)
    {
        int head = (int)U(bytes, 0x34) + index * 0x40;
        int b = (int)U(bytes, head + 0x18);
        float F(int at) => BitConverter.ToSingle(bytes, b + at);
        bool Flag(int at) => bytes[b + at] != 0;
        Vector3 V3(int at) => new(F(at), F(at + 4), F(at + 8));
        Vector4 V4(int at) => new(F(at), F(at + 4), F(at + 8), F(at + 12));
        LifeCurve Life(int at, bool mid) => new(F(at), F(at + 4), F(at + 8), F(at + 12), F(at + 16), F(at + 20), mid);

        var curves = new KeyCurve[28];
        for (int c = 0; c < 28; c++)
        {
            int count = (int)U(bytes, b + 0x200 + 8 * c);
            uint at = U(bytes, b + 0x204 + 8 * c);
            int numbers = c is Position or Scale ? 3 : c == RotationCurve ? 4 : 1;
            KeyCurve fallback = c == RotationCurve ? KeyCurve.Constant(0, 0, 0, 1) : c == Scale ? KeyCurve.Constant(1, 1, 1) : KeyCurve.Zero;
            curves[c] = KeyCurve.Read(bytes, count, at, numbers, 4 + 4 * numbers, 4, fallback);
        }

        uint shapeSize = U(bytes, b + 0x2E0), shapeAt = U(bytes, b + 0x2E4);
        var shape = new List<float>();
        for (int i = 0; shapeAt != None && i + 4 <= shapeSize && shapeAt + i + 4 <= bytes.Length; i += 4)
            shape.Add(BitConverter.ToSingle(bytes, (int)shapeAt + i));

        return new EmitterSettings
        {
            Name = Text(bytes, U(bytes, head)),
            ParticleType = bytes[b + 0x02],
            Shape = EffectSummary.ShapeOf(U(bytes, head + 0x08)),
            ShapeSize = [.. shape],
            Parent = (int)U(bytes, head + 0x10),
            Curves = curves,
            WeightStart = F(0x08),
            WeightEnd = F(0x0C),
            RotationEnabled = Flag(0x12),
            RandomStartRotation = Flag(0x13),
            UseLength = Flag(0x14),
            UseLengthMid = Flag(0x15),
            UseSizeMid = Flag(0x18),
            UseColourMid = Flag(0x19),
            UseSaturation = Flag(0x1A),
            UseAdditive = Flag(0x1B),
            AccelerationEnabled = Flag(0x1D),
            AccelerationVariance = Flag(0x1E),
            UseLifetimeVariance = Flag(0x1F),
            UseSizeVariance = Flag(0x20),
            UseLengthVariance = Flag(0x21),
            AccelerationStart = V3(0x24),
            AccelerationStartVariance = V3(0x30),
            AccelerationEnd = V3(0x3C),
            AccelerationEndVariance = V3(0x48),
            ColourStart = V4(0x54),
            ColourMidA = V4(0x64),
            ColourMidAOffset = F(0x74),
            ColourMidB = V4(0x78),
            ColourMidBOffset = F(0x88),
            ColourEnd = V4(0x8C),
            AlphaMidAOffset = F(0xDC),
            AlphaMidBOffset = F(0xE0),
            Saturation = Life(0x160, true),
            Additive = Life(0x188, true),
            YawVariance = F(0x1B8),
            PitchVariance = F(0x1BC),
            SpeedVariance = F(0x1C0),
            AngularStartVariance = F(0x1C4),
            AngularEndVariance = F(0x1C8),
            Lifetime = F(0x1CC),
            LifetimeVariance = F(0x1D0),
            MaxParticles = (int)U(bytes, b + 0x1D4),
            Carries = ReadNumbers(bytes, (int)U(bytes, b + 0x1D8), U(bytes, b + 0x1DC)),
            MeshName = U(bytes, head + 0x04) is var meshAt && meshAt != None ? Text(bytes, meshAt) : null,
        };
    }

    static uint U(byte[] bytes, int at) => at >= 0 && at + 4 <= bytes.Length ? BitConverter.ToUInt32(bytes, at) : None;

    static int[] ReadNumbers(byte[] bytes, int count, uint at)
    {
        if (count <= 0 || count > 64 || at == None || at + 4L * count > bytes.Length) return [];
        return [.. Enumerable.Range(0, count).Select(k => (int)BitConverter.ToUInt32(bytes, (int)at + 4 * k))];
    }

    static string Text(byte[] bytes, uint at)
    {
        if (at == None || at >= bytes.Length) return "";
        int end = Array.IndexOf(bytes, (byte)0, (int)at);
        return System.Text.Encoding.Latin1.GetString(bytes, (int)at, (end < 0 ? bytes.Length : end) - (int)at);
    }
}

/// <summary>One particle as it flies.</summary>
public sealed class Particle
{
    public int Emitter;
    public int Id;    // in the order they were made
    public int Node;  // the node particle it was sent from (its Id), or 0
    public Vector3 Position, Velocity, AccelerationStartOffset, AccelerationEndOffset;
    public float Age, Lifetime, Angle, AngularStart, AngularEnd, TextureAnimationRate;
    public int RandomFrame;  // the frame of a flip-book texture that doesn't play (its rate 0)
    public LifeCurve Size, Length;

    /// <summary>Which frame of a flip book with <paramref name="frames"/> frames it shows now.</summary>
    public int Frame(int frames)
    {
        if (frames <= 1) return 0;
        if (TextureAnimationRate <= 0) return RandomFrame % frames;
        return (int)(Life * TextureAnimationRate * frames) % frames;
    }

    public float Life => Lifetime <= 0 ? 1 : Math.Clamp(Age / Lifetime, 0, 1);
}

/// <summary>
/// An effect's particles over time, as far as the files say how: each emitter sends particles from its shape (placed
/// and turned by its position, rotation and scale keys) at its emission rate, upward (the emitter's Y) with its speed,
/// spread by its yaw and pitch variances; they fall by their weight, speed up by their acceleration, turn, and die at
/// the end of their lifetime. An emitter with a parent sends its particles from the parent's living particles.
/// Not the game's own code: what it shows is close, not exact.
/// </summary>
public sealed class EffectSimulation
{
    const float Gravity = 9.81f;

    readonly Random random = new(1);
    readonly float[] emitDebt;  // particles owed to each emitter, carried between steps
    readonly int[] carrier;     // for each emitter, the node emitter it rides on, or -1
    readonly int[] nextNode;    // which of the carrier's particles it sends from next
    int lastId;

    public IReadOnlyList<EmitterSettings> Emitters { get; }
    public IReadOnlyList<FilterSettings> Filters { get; }

    /// <summary>Which filters act (all, at first); one switched off leaves its emitters' particles alone.</summary>
    public bool[] FiltersOn { get; }

    public float Duration { get; }
    public List<Particle> Particles { get; } = [];

    /// <summary>The effect's time: from 0, to its duration (and again from 0, when looping).</summary>
    public float Time { get; private set; }

    /// <summary>Whether its emitters are still sending (false once a one-off play has passed its duration).</summary>
    public bool Emitting { get; private set; } = true;

    public bool Loop { get; set; } = true;

    public EffectSimulation(byte[] effect)
    {
        EffectFile.Read(effect);  // the offsets checked
        int count = BitConverter.ToInt32(effect, 0x30);
        Emitters = [.. Enumerable.Range(0, Math.Max(0, count)).Select(i => EmitterSettings.Read(effect, i))];
        Duration = BitConverter.ToSingle(effect, 0x14);
        Filters = FilterSettings.ReadAll(effect);
        FiltersOn = [.. Enumerable.Repeat(true, Filters.Count)];
        emitDebt = new float[Emitters.Count];
        nextNode = new int[Emitters.Count];
        carrier = [.. Enumerable.Repeat(-1, Emitters.Count)];
        for (int n = 0; n < Emitters.Count; n++)
            foreach (int carried in Emitters[n].Carries)
                if (carried >= 0 && carried < Emitters.Count && carried != n && carrier[carried] < 0) carrier[carried] = n;
    }

    /// <summary>The node emitter an emitter rides on (its particles sent from that one's), or −1.</summary>
    public int CarrierOf(int emitter) => carrier[emitter];

    public void Restart()
    {
        Particles.Clear();
        Array.Clear(emitDebt);
        Time = 0;
        Emitting = true;
    }

    /// <summary>Moves everything on by <paramref name="seconds"/>.</summary>
    public void Step(float seconds)
    {
        if (seconds <= 0) return;
        // The living age and move.
        for (int i = Particles.Count - 1; i >= 0; i--)
        {
            Particle p = Particles[i];
            p.Age += seconds;
            if (p.Age >= p.Lifetime)
            {
                Particles.RemoveAt(i);
                continue;
            }
            EmitterSettings e = Emitters[p.Emitter];
            float life = p.Life;
            Vector3 acceleration = new(0, -Gravity * (e.WeightStart + (e.WeightEnd - e.WeightStart) * life), 0);
            if (e.AccelerationEnabled)
                acceleration += Vector3.Lerp(e.AccelerationStart + p.AccelerationStartOffset, e.AccelerationEnd + p.AccelerationEndOffset, life);
            acceleration += FilterPush(p);
            p.Velocity += acceleration * seconds;
            p.Position += p.Velocity * seconds;
            if (e.RotationEnabled) p.Angle += (p.AngularStart + (p.AngularEnd - p.AngularStart) * life) * MathF.PI / 180 * seconds;
        }

        // New ones, at each emitter's rate at this time.
        if (Emitting)
        {
            for (int e = 0; e < Emitters.Count; e++)
            {
                EmitterSettings settings = Emitters[e];
                // One riding on node particles sends from each of them at its rate.
                List<Particle>? nodes = carrier[e] >= 0 ? Particles.Where(p => p.Emitter == carrier[e]).ToList() : null;
                if (nodes is { Count: 0 })
                {
                    emitDebt[e] = 0;
                    continue;
                }
                emitDebt[e] += Math.Max(0, settings.EmissionRateKeys.Number(Time)) * seconds * (nodes?.Count ?? 1);
                int alive = Particles.Count(p => p.Emitter == e);
                while (emitDebt[e] >= 1)
                {
                    emitDebt[e] -= 1;
                    if (settings.MaxParticles > 0 && alive >= settings.MaxParticles) continue;
                    Particle? node = nodes?[nextNode[e]++ % nodes.Count];
                    if (Emit(e, node)) alive++;
                }
            }
        }

        Time += seconds;
        if (Duration > 0 && Time >= Duration)
        {
            if (Loop) Time %= Duration;
            else
            {
                Time = Duration;
                Emitting = false;
            }
        }
    }

    float Signed() => (float)(random.NextDouble() * 2 - 1);

    /// <summary>
    /// How much the filters (forces) acting on a particle's emitter speed it up: a magnet toward its place (away, when
    /// its force is below 0; only within its radius, when it has one), a vortex around its axis (its scale's X along,
    /// Y inward, Z along the axis), turbulence a random push of its force. Their strength is the files' number taken
    /// as metres a second each second: a guess.
    /// </summary>
    Vector3 FilterPush(Particle p)
    {
        Vector3 push = Vector3.Zero;
        for (int f = 0; f < Filters.Count; f++)
        {
            FilterSettings filter = Filters[f];
            if (!FiltersOn[f] || !filter.Affects.Contains(p.Emitter)) continue;
            Vector3 place = filter.Position.Vector(Time);
            Vector3 toward = place - p.Position;
            float distance = toward.Length();
            switch (filter.Type)
            {
                case FilterSettings.Magnet:
                    float radius = filter.Radius.Number(Time);
                    if (distance > 1e-4f && (radius <= 0 || distance <= radius)) push += toward / distance * filter.Force.Number(Time);
                    break;
                case FilterSettings.Vortex:
                    Vector3 axis = Vector3.Transform(Vector3.UnitY, filter.Rotation.Rotation(Time));
                    Vector3 fromAxis = -toward - Vector3.Dot(-toward, axis) * axis;  // from the axis to the particle
                    if (fromAxis.LengthSquared() < 1e-8f) break;
                    Vector3 outward = Vector3.Normalize(fromAxis);
                    Vector3 scale = filter.VortexScale.Vector(Time);
                    push += Vector3.Cross(axis, outward) * scale.X - outward * scale.Y + axis * scale.Z;
                    break;
                case FilterSettings.Turbulence:
                    push += new Vector3(Signed(), Signed(), Signed()) * filter.Force.Number(Time);
                    break;
            }
        }
        return push;
    }

    /// <summary>An orientor acting on an emitter: the way its particles lie (their face turned by it), or null when none
    /// does; its particles then lie in a plane in the world instead of facing the camera.</summary>
    public Quaternion? OrientorOf(int emitter)
    {
        for (int f = 0; f < Filters.Count; f++)
            if (FiltersOn[f] && Filters[f].Type == FilterSettings.Orientor && Filters[f].Affects.Contains(emitter)) return Filters[f].Rotation.Rotation(Time);
        return null;
    }

    bool Emit(int index, Particle? node)
    {
        EmitterSettings e = Emitters[index];
        Vector3 origin = e.PositionKeys.Vector(Time);
        Quaternion turn = e.RotationKeys.Rotation(Time);
        Vector3 scale = e.ScaleKeys.Vector(Time);
        if (scale == Vector3.Zero) scale = Vector3.One;

        // From a node particle it rides on; from the parent's particles, if it has one (and some are alive).
        if (node != null)
            origin = node.Position;
        else if (e.Parent >= 0 && e.Parent < Emitters.Count && e.Parent != index)
        {
            var parents = Particles.Where(p => p.Emitter == e.Parent).ToList();
            if (parents.Count == 0) return false;
            origin = parents[random.Next(parents.Count)].Position;
        }
        else
            origin += Vector3.Transform(InShape(e) * scale, turn);

        // Upward along the emitter, spread by the yaw and pitch variances.
        float tilt = Math.Abs(e.PitchVariance) * Signed(), sideways = Math.Abs(e.YawVariance) * Signed();
        Vector3 direction = Vector3.Transform(Vector3.UnitY, Quaternion.CreateFromYawPitchRoll(0, tilt, sideways));
        direction = Vector3.Transform(direction, turn);
        float speed = Math.Max(0, e.SpeedKeys.Number(Time) + e.SpeedVariance * Signed());

        Span<float> sizeRandom = [Signed(), Signed(), Signed(), Signed()];
        Span<float> lengthRandom = [Signed(), Signed(), Signed(), Signed()];
        float lifetime = e.Lifetime + (e.UseLifetimeVariance ? e.LifetimeVariance * Signed() : 0);
        if (lifetime <= 0.01f) return false;
        Vector3 Vary(Vector3 v) => new(v.X * Signed(), v.Y * Signed(), v.Z * Signed());
        Particles.Add(new Particle
        {
            Emitter = index,
            Id = ++lastId,
            Node = node?.Id ?? 0,
            Position = origin,
            Velocity = direction * speed,
            AccelerationStartOffset = e.AccelerationVariance ? Vary(e.AccelerationStartVariance) : Vector3.Zero,
            AccelerationEndOffset = e.AccelerationVariance ? Vary(e.AccelerationEndVariance) : Vector3.Zero,
            Lifetime = lifetime,
            Angle = e.RandomStartRotation ? (float)(random.NextDouble() * Math.PI * 2) : 0,
            AngularStart = e.AngularStartKeys.Number(Time) + e.AngularStartVariance * Signed(),
            AngularEnd = e.AngularEndKeys.Number(Time) + e.AngularEndVariance * Signed(),
            TextureAnimationRate = e.TextureAnimationRateKeys.Number(Time),
            RandomFrame = random.Next(1 << 20),
            Size = e.SizeAt(Time, sizeRandom),
            Length = e.LengthAt(Time, lengthRandom),
        });
        return true;
    }

    // A place in the emitter's shape: a point; in a sphere; in a box (its sizes, around its middle); in a cylinder
    // standing on Y (radius, height); on a plane lying in X and Z.
    Vector3 InShape(EmitterSettings e)
    {
        float[] s = e.ShapeSize;
        float Size(int i) => i < s.Length ? Math.Abs(s[i]) : 0;
        switch (e.Shape)
        {
            case "sphere":
                Vector3 v;
                do v = new Vector3(Signed(), Signed(), Signed()); while (v.LengthSquared() > 1);
                return v * Size(0);
            case "box":
                return new Vector3(Signed() * Size(0), Signed() * Size(1), Signed() * Size(2)) * 0.5f;
            case "cylinder":
                float angle = (float)(random.NextDouble() * Math.PI * 2), r = Size(0) * MathF.Sqrt((float)random.NextDouble());
                return new Vector3(MathF.Cos(angle) * r, Signed() * Size(1) * 0.5f, MathF.Sin(angle) * r);
            case "plane":
                return new Vector3(Signed() * Size(0) * 0.5f, 0, Signed() * Size(1) * 0.5f);
            default:
                return Vector3.Zero;
        }
    }
}

/// <summary>
/// A corona: a glow facing the camera, drawn added to what is behind it (Guerrilla's PDB:
/// vfx_effect_template::corona, laid out with 4-byte offsets: 0x108 bytes). Its curves run over the effect's time:
/// position, radius at near and far (blended by the camera's distance between the radius distances), its width as a
/// part of its height (a streak when small), its colour at near and far (blended by the colour distances), saturation
/// and an intensity it is multiplied by. Distances in metres.
/// </summary>
public sealed class CoronaSettings
{
    public required string Name { get; init; }
    public required KeyCurve Position { get; init; }
    public required KeyCurve RadiusNear { get; init; }
    public required KeyCurve RadiusFar { get; init; }
    public required KeyCurve WidthScale { get; init; }
    public required KeyCurve ColourNear { get; init; }
    public required KeyCurve ColourFar { get; init; }
    public required KeyCurve Saturation { get; init; }
    public required KeyCurve Intensity { get; init; }
    public required KeyCurve RadiusDistanceNear { get; init; }
    public required KeyCurve RadiusDistanceFar { get; init; }
    public required KeyCurve ColourDistanceNear { get; init; }
    public required KeyCurve ColourDistanceFar { get; init; }

    public static IReadOnlyList<CoronaSettings> ReadAll(byte[] bytes)
    {
        int count = BitConverter.ToInt32(bytes, 0x48);
        uint list = BitConverter.ToUInt32(bytes, 0x4C);
        var coronas = new List<CoronaSettings>();
        for (int i = 0; i < count && list != 0xFFFFFFFF && list + 0x108L * (i + 1) <= bytes.Length; i++)
        {
            int c = (int)list + 0x108 * i;
            // A float curve: (count, offset) at +at; a 4-number one has 32-byte keys (time, 12 bytes of padding, the colour).
            KeyCurve Float(int at, float fallback) =>
                KeyCurve.Read(bytes, BitConverter.ToInt32(bytes, c + at), BitConverter.ToUInt32(bytes, c + at + 4), 1, 8, 4, KeyCurve.Constant(fallback));
            KeyCurve Colour(int at) =>
                KeyCurve.Read(bytes, BitConverter.ToInt32(bytes, c + at), BitConverter.ToUInt32(bytes, c + at + 4), 4, 32, 16, KeyCurve.Constant(1, 1, 1, 1));
            uint nameAt = BitConverter.ToUInt32(bytes, c);
            coronas.Add(new CoronaSettings
            {
                Name = nameAt < bytes.Length ? System.Text.Encoding.Latin1.GetString(bytes, (int)nameAt, Math.Max(0, Array.IndexOf(bytes, (byte)0, (int)nameAt) - (int)nameAt)) : "",
                // The position's count is at +0x40, its keys' offset at +0x48 (+0x44 holds two flags).
                Position = KeyCurve.Read(bytes, BitConverter.ToInt32(bytes, c + 0x40), BitConverter.ToUInt32(bytes, c + 0x48), 3, 16, 4, KeyCurve.Constant(0, 0, 0)),
                RadiusNear = Float(0x5C, 1),
                RadiusFar = Float(0x64, 1),
                WidthScale = Float(0x6C, 1),
                ColourNear = Colour(0x7C),
                ColourFar = Colour(0x84),
                Saturation = Float(0x94, 1),
                Intensity = Float(0xA4, 1),
                RadiusDistanceNear = Float(0xBC, 0),
                RadiusDistanceFar = Float(0xC4, 100),
                ColourDistanceNear = Float(0xCC, 0),
                ColourDistanceFar = Float(0xD4, 100),
            });
        }
        return coronas;
    }

    static float Between(float distance, float near, float far) => far <= near ? 0 : Math.Clamp((distance - near) / (far - near), 0, 1);

    /// <summary>Its size (half its height, half its width) and colour at a time, seen from a distance.</summary>
    public (float HalfHeight, float HalfWidth, Vector4 Colour, float Saturation) At(float time, float distance)
    {
        float r = Between(distance, RadiusDistanceNear.Number(time), RadiusDistanceFar.Number(time));
        float radius = RadiusNear.Number(time) + (RadiusFar.Number(time) - RadiusNear.Number(time)) * r;
        float c = Between(distance, ColourDistanceNear.Number(time), ColourDistanceFar.Number(time));
        Vector4 colour = System.Numerics.Vector4.Lerp(ColourNear.Vector4(time), ColourFar.Vector4(time), c);
        return (radius, radius * Math.Max(0.01f, WidthScale.Number(time)), colour * new Vector4(Intensity.Number(time), Intensity.Number(time), Intensity.Number(time), 1),
                Math.Clamp(Saturation.Number(time), 0, 1));
    }
}

/// <summary>
/// A mesh of an effect (a model: glows, light cones, rings), with its vertices from the .gefct_pc beside it. The record
/// (0x80 bytes): +0x00 name, +0x04 type (1 normal, 2 facing the camera, 4 rod, 8 additive, 0x10 volume fog), key
/// curves (count, offset) +0x08 position, +0x10 rotation, +0x18 scale, +0x20 opacity, +0x28 Armageddon's own list,
/// +0x30 the mesh block (version 9, as <see cref="RfaFormats.Meshes.ModelFile"/>'s), +0x34 its materials, +0x3C how many.
/// The .gefct_pc holds each mesh's vertices and indices in turn, each mesh's part from a multiple of 16, the block's
/// offsets counting from its own part.
/// </summary>
public sealed class EffectMesh
{
    public required string Name { get; init; }
    public required int Type { get; init; }
    public required KeyCurve Position { get; init; }
    public required KeyCurve Rotation { get; init; }
    public required KeyCurve Scale { get; init; }
    public required KeyCurve Opacity { get; init; }
    public required Vector3[] Positions { get; init; }
    public required Vector2[] TextureCoordinates { get; init; }
    public required IReadOnlyList<(int Material, int[] Triangles)> Parts { get; init; }
    public required IReadOnlyList<RfaFormats.Meshes.ModelMaterial> Materials { get; init; }

    public bool Additive => (Type & 8) != 0;

    /// <summary>The colour texture of a material ("_d"/"_di" slot, else its first), or null.</summary>
    public string? TextureOf(int material) =>
        material >= 0 && material < Materials.Count ? Materials[material].Diffuse ?? Materials[material].Textures.FirstOrDefault().Texture : null;

    /// <summary>Every mesh of an effect; one whose data can't be read is left out. Without the .gefct_pc, none.</summary>
    public static IReadOnlyList<EffectMesh> ReadAll(byte[] effect, byte[]? vertices)
    {
        var meshes = new List<EffectMesh>();
        if (vertices == null) return meshes;
        int count = BitConverter.ToInt32(effect, 0x28);
        uint list = BitConverter.ToUInt32(effect, 0x2C);
        int names = (int)BitConverter.ToUInt32(effect, 0x24);  // the meshes' texture names, one after another
        int part = 0;  // where this mesh's part of the .gefct_pc starts
        for (int i = 0; i < count && list != 0xFFFFFFFF && list + 0x80L * (i + 1) <= effect.Length; i++)
        {
            int m = (int)list + 0x80 * i;
            int block = (int)BitConverter.ToUInt32(effect, m + 0x30);
            if (block <= 0 || block + 0x50 > effect.Length) continue;
            int partSize = (int)BitConverter.ToUInt32(effect, block + 0x0C);
            try
            {
                meshes.Add(Read(effect, vertices, m, block, part, names));
            }
            catch (Exception e) when (e is ArgumentException or IndexOutOfRangeException or InvalidDataException) { }
            part += (partSize + 15) & ~15;
        }
        return meshes;
    }

    static EffectMesh Read(byte[] effect, byte[] data, int m, int at, int part, int names)
    {
        uint U(int o) => BitConverter.ToUInt32(effect, o);
        int submeshCount = (int)U(at + 0x14), submeshTable = at + (int)U(at + 0x18), vertexCount = (int)U(at + 0x1C);
        int vertexSize = effect[at + 0x20], uvChannels = effect[at + 0x22];
        int vertexOffset = part + (int)U(at + 0x3C), indexCount = (int)U(at + 0x40), indexOffset = part + (int)U(at + 0x44);
        int indexSize = effect[at + 0x48], topology = effect[at + 0x49], blockCount = BitConverter.ToUInt16(effect, at + 0x4A);
        if (indexSize is not (2 or 4) || vertexSize < 12 || vertexOffset + (long)vertexCount * vertexSize > data.Length ||
            indexOffset + (long)indexCount * indexSize > data.Length)
            throw new InvalidDataException("the mesh's data isn't where its block says");

        var positions = new Vector3[vertexCount];
        var uvs = new Vector2[vertexCount];
        int uvAt = vertexSize - 4 * uvChannels;
        for (int v = 0; v < vertexCount; v++)
        {
            int p = vertexOffset + v * vertexSize;
            positions[v] = new Vector3(BitConverter.ToSingle(data, p), BitConverter.ToSingle(data, p + 4), BitConverter.ToSingle(data, p + 8));
            if (uvChannels > 0) uvs[v] = new Vector2(BitConverter.ToInt16(data, p + uvAt), BitConverter.ToInt16(data, p + uvAt + 2)) / 1024f;
        }
        var indices = new int[indexCount];
        for (int i = 0; i < indexCount; i++)
            indices[i] = indexSize == 2 ? BitConverter.ToUInt16(data, indexOffset + i * 2) : BitConverter.ToInt32(data, indexOffset + i * 4);

        // The render blocks (after the submesh table, 20 bytes each): a material, the first index and how many.
        var parts = new List<(int, int[])>();
        int blocks = submeshTable + submeshCount * 48;
        for (int b = 0; b < blockCount; b++)
        {
            int r = blocks + b * 20;
            int material = BitConverter.ToUInt16(effect, r), first = (int)U(r + 4), n = (int)U(r + 8);
            if (first < 0 || n < 0 || first + n > indexCount) continue;
            parts.Add((material, Triangles(indices, first, n, topology == 0, vertexCount)));
        }

        int materialCount = (int)U(m + 0x3C);
        var materials = materialCount is > 0 and < 64
            ? RfaFormats.Meshes.ModelMaterial.ReadAll(effect, (int)U(m + 0x34), null, names, materialCount)
            : [];
        uint nameAt = U(m);
        KeyCurve Curve(int countAt, int numbers, KeyCurve fallback) =>
            KeyCurve.Read(effect, (int)U(m + countAt), U(m + countAt + 4), numbers, 4 + 4 * numbers, 4, fallback);
        return new EffectMesh
        {
            Name = nameAt < effect.Length ? Encoding.Latin1.GetString(effect, (int)nameAt, Math.Max(0, Array.IndexOf(effect, (byte)0, (int)nameAt) - (int)nameAt)) : "",
            Type = (int)U(m + 0x04),
            Position = Curve(0x08, 3, KeyCurve.Constant(0, 0, 0)),
            Rotation = Curve(0x10, 4, KeyCurve.Constant(0, 0, 0, 1)),
            Scale = Curve(0x18, 3, KeyCurve.Constant(1, 1, 1)),
            Opacity = Curve(0x20, 1, KeyCurve.Constant(1)),
            Positions = positions,
            TextureCoordinates = uvs,
            Parts = parts,
            Materials = materials,
        };
    }

    // Three vertex numbers a triangle, from a strip (each new index makes one with the two before it; ones with a
    // repeated corner join strips and are left out) or a list.
    static int[] Triangles(int[] indices, int first, int count, bool strip, int vertexCount)
    {
        var triangles = new List<int>();
        if (!strip)
        {
            for (int i = first; i + 2 < first + count; i += 3) triangles.AddRange([indices[i], indices[i + 1], indices[i + 2]]);
        }
        else
        {
            for (int i = first; i + 2 < first + count; i++)
            {
                int a = indices[i], b = indices[i + 1], c = indices[i + 2];
                if (a == b || b == c || a == c) continue;
                triangles.AddRange([a, b, c]);
            }
        }
        return triangles.All(v => v >= 0 && v < vertexCount) ? [.. triangles] : [];
    }
}

/// <summary>
/// A filter of an effect: a force on the particles of the emitters it lists (Guerrilla's PDB: vfx_effect_template::filter,
/// laid out with 4-byte offsets: 0x40 bytes). +0x00 name, +0x04 type (0 gravity, 1 wind, 2 magnet, 3 turbulence, 4 vortex,
/// 5 orientor), +0x08 flags (1 per emitter in all 19), +0x0C class id, +0x10 parent, +0x14/+0x18 the emitters it acts on
/// (count, offset of u32s), +0x1C/+0x20 meshes, +0x24/+0x28 position keys, +0x2C/+0x30 rotation keys, +0x34 run-time,
/// +0x38/+0x3C its own data (size, offset): magnet force and radius keys, turbulence force and noise keys, wind force,
/// noise and angular velocity keys (with its variance), vortex scale keys (vectors), each a (count, offset) pair.
/// </summary>
public sealed class FilterSettings
{
    public const int Gravity = 0, Wind = 1, Magnet = 2, Turbulence = 3, Vortex = 4, Orientor = 5;

    public required string Name { get; init; }
    public required int Type { get; init; }
    public required int[] Affects { get; init; }
    public required KeyCurve Position { get; init; }
    public required KeyCurve Rotation { get; init; }
    public required KeyCurve Force { get; init; }
    public required KeyCurve Radius { get; init; }
    public required KeyCurve VortexScale { get; init; }

    public string TypeName => Type switch
    {
        Gravity => "gravity",
        Wind => "wind",
        Magnet => "magnet",
        Turbulence => "turbulence",
        Vortex => "vortex",
        Orientor => "orientor",
        _ => $"type {Type}",
    };

    public static IReadOnlyList<FilterSettings> ReadAll(byte[] bytes)
    {
        int count = BitConverter.ToInt32(bytes, 0x40);
        uint list = BitConverter.ToUInt32(bytes, 0x44);
        var filters = new List<FilterSettings>();
        for (int i = 0; i < count && list != 0xFFFFFFFF && list + 0x40L * (i + 1) <= bytes.Length; i++)
        {
            int f = (int)list + 0x40 * i;
            uint U(int at) => at >= 0 && at + 4 <= bytes.Length ? BitConverter.ToUInt32(bytes, at) : 0xFFFFFFFF;
            int type = (int)U(f + 0x04);
            int dataSize = (int)U(f + 0x38);
            uint data = U(f + 0x3C);
            // One of its own data's (count, offset) pairs.
            KeyCurve Own(int pair, int numbers, KeyCurve fallback) =>
                data == 0xFFFFFFFF || pair * 8 + 8 > dataSize ? fallback
                : KeyCurve.Read(bytes, (int)U((int)data + pair * 8), U((int)data + pair * 8 + 4), numbers, 4 + 4 * numbers, 4, fallback);
            int affected = (int)U(f + 0x14);
            uint affectedAt = U(f + 0x18);
            uint nameAt = U(f);
            filters.Add(new FilterSettings
            {
                Name = nameAt < bytes.Length ? System.Text.Encoding.Latin1.GetString(bytes, (int)nameAt, Math.Max(0, Array.IndexOf(bytes, (byte)0, (int)nameAt) - (int)nameAt)) : "",
                Type = type,
                Affects = affected is > 0 and < 64 && affectedAt != 0xFFFFFFFF && affectedAt + 4L * affected <= bytes.Length
                    ? [.. Enumerable.Range(0, affected).Select(k => (int)U((int)affectedAt + 4 * k))]
                    : [],
                Position = KeyCurve.Read(bytes, (int)U(f + 0x24), U(f + 0x28), 3, 16, 4, KeyCurve.Constant(0, 0, 0)),
                Rotation = KeyCurve.Read(bytes, (int)U(f + 0x2C), U(f + 0x30), 4, 20, 4, KeyCurve.Constant(0, 0, 0, 1)),
                Force = type == Vortex ? KeyCurve.Constant(0) : Own(0, 1, KeyCurve.Constant(0)),
                Radius = type == Magnet ? Own(1, 1, KeyCurve.Constant(0)) : KeyCurve.Constant(0),
                VortexScale = type == Vortex ? Own(0, 3, KeyCurve.Constant(0, 0, 0)) : KeyCurve.Constant(0, 0, 0),
            });
        }
        return filters;
    }
}

/// <summary>
/// A light of an effect (0x94 bytes; the first 0x3C as Guerrilla's vfx_effect_template::light): +0x00 name, +0x04 type
/// (0 point: 509 of 598; 1 spot: 48; 2: 41, taken as a point), key curves (count, offset) +0x08 colour, +0x10 intensity,
/// +0x18 position, +0x20 forward (the spot's way), then +0x28 the spot's inner and +0x2C outer half-angle (radians), +0x30
/// the distance its light starts to fade and +0x34 where it is gone (metres). Intensities run 1 to 100.
/// </summary>
public sealed class LightSettings
{
    public required string Name { get; init; }
    public required int Type { get; init; }
    public required KeyCurve Colour { get; init; }
    public required KeyCurve Intensity { get; init; }
    public required KeyCurve Position { get; init; }
    public required KeyCurve Forward { get; init; }
    public required float Hotspot { get; init; }
    public required float Falloff { get; init; }
    public required float FadeStart { get; init; }
    public required float FadeEnd { get; init; }

    public bool Spot => Type == 1;

    public static IReadOnlyList<LightSettings> ReadAll(byte[] bytes)
    {
        int count = BitConverter.ToInt32(bytes, 0x38);
        uint list = BitConverter.ToUInt32(bytes, 0x3C);
        var lights = new List<LightSettings>();
        for (int i = 0; i < count && list != 0xFFFFFFFF && list + 0x94L * (i + 1) <= bytes.Length; i++)
        {
            int l = (int)list + 0x94 * i;
            KeyCurve Curve(int at, int numbers, KeyCurve fallback) =>
                KeyCurve.Read(bytes, BitConverter.ToInt32(bytes, l + at), BitConverter.ToUInt32(bytes, l + at + 4), numbers, 4 + 4 * numbers, 4, fallback);
            uint nameAt = BitConverter.ToUInt32(bytes, l);
            lights.Add(new LightSettings
            {
                Name = nameAt < bytes.Length ? System.Text.Encoding.Latin1.GetString(bytes, (int)nameAt, Math.Max(0, Array.IndexOf(bytes, (byte)0, (int)nameAt) - (int)nameAt)) : "",
                Type = BitConverter.ToInt16(bytes, l + 0x04),
                Colour = Curve(0x08, 3, KeyCurve.Constant(1, 1, 1)),
                Intensity = Curve(0x10, 1, KeyCurve.Constant(1)),
                Position = Curve(0x18, 3, KeyCurve.Constant(0, 0, 0)),
                Forward = Curve(0x20, 3, KeyCurve.Constant(0, -1, 0)),
                Hotspot = BitConverter.ToSingle(bytes, l + 0x28),
                Falloff = BitConverter.ToSingle(bytes, l + 0x2C),
                FadeStart = BitConverter.ToSingle(bytes, l + 0x30),
                FadeEnd = BitConverter.ToSingle(bytes, l + 0x34),
            });
        }
        return lights;
    }

    /// <summary>How much of its light reaches a point (0 to 1): full up to its fade start, gone at its fade end, and for a
    /// spot, full inside its inner angle and gone past its outer one.</summary>
    public float Reach(float time, Vector3 point)
    {
        Vector3 from = Position.Vector(time);
        Vector3 to = point - from;
        float distance = to.Length();
        float reach = FadeEnd <= FadeStart ? (distance <= FadeEnd ? 1 : 0) : 1 - Math.Clamp((distance - FadeStart) / (FadeEnd - FadeStart), 0, 1);
        if (reach <= 0 || !Spot || distance < 1e-4f) return reach;
        Vector3 way = Forward.Vector(time);
        if (way.LengthSquared() < 1e-8f) return reach;
        float angle = MathF.Acos(Math.Clamp(Vector3.Dot(Vector3.Normalize(way), to / distance), -1, 1));
        float inner = Math.Max(0, Hotspot), outer = Math.Max(inner + 1e-3f, Falloff);
        return reach * (1 - Math.Clamp((angle - inner) / (outer - inner), 0, 1));
    }
}
