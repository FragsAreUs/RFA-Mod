using System.Text;

namespace RfaFormats.Effects;

/// <summary>
/// What an effect (.cefct_pc, version 36) is made of, for showing it: its textures, emitters, lights, coronas, meshes,
/// filters and expressions, each with the fields known (the layout is in <see cref="EffectFile"/>; the fields' meanings
/// in docs\game-notes.md, "Effects"). Read only: offsets are from the file's start, −1 for none.
/// </summary>
public sealed class EffectSummary
{
    public sealed record Material(uint Shader, IReadOnlyList<(uint Sampler, int Bitmap, int Slot)> Textures);

    public sealed record Emitter(
        string Name, string Shape, int ParticleType, int MaxParticles, float Lifetime, float LifetimeVariance,
        float[] ColourStart, float[] ColourEnd, bool HalfRes, bool QuarterRes, bool Soft, int Parent, string? MeshName,
        Material? Material);

    public sealed record Light(string Name, int Type, float HotspotSize, float HotspotFalloff, float AttenuationStart, float AttenuationEnd);
    public sealed record Corona(string Name, Material? Material);
    public sealed record Mesh(string Name, int Type);

    public string Name { get; private init; } = "";
    public float Duration { get; private init; }
    public float Radius { get; private init; }
    public IReadOnlyList<string> Bitmaps { get; private init; } = [];
    public IReadOnlyList<Emitter> Emitters { get; private init; } = [];
    public IReadOnlyList<Light> Lights { get; private init; } = [];
    public IReadOnlyList<Corona> Coronas { get; private init; } = [];
    public IReadOnlyList<Mesh> Meshes { get; private init; } = [];
    public IReadOnlyList<string> Filters { get; private init; } = [];
    public int Expressions { get; private init; }

    const uint None = 0xFFFFFFFF;

    public static EffectSummary Read(byte[] bytes)
    {
        EffectFile file = EffectFile.Read(bytes);  // checks the signature, version and every offset
        var r = new Reader(bytes);
        return new EffectSummary
        {
            Name = file.Name,
            Duration = r.F(0x14),
            Radius = r.F(0x18),
            Bitmaps = r.List(0x1C, 0x20, 4, at => r.Text(r.U(at)) ?? ""),
            Emitters = r.List(0x30, 0x34, 0x40, at => ReadEmitter(r, at)),
            Lights = r.List(0x38, 0x3C, 0x94, at => new Light(r.Text(r.U(at)) ?? "", r.S16(at + 0x04), r.F(at + 0x28), r.F(at + 0x2C), r.F(at + 0x30), r.F(at + 0x34))),
            Coronas = r.List(0x48, 0x4C, 0x108, at => new Corona(r.Text(r.U(at)) ?? "", ReadMaterial(r, at + 0x04))),
            Meshes = r.List(0x28, 0x2C, 0x80, at => new Mesh(r.Text(r.U(at)) ?? "", (int)r.U(at + 0x04))),
            Filters = r.List(0x40, 0x44, 0x40, at => r.Text(r.U(at)) ?? ""),
            Expressions = (int)r.U(0x08),
        };
    }

    // The emitter's head (0x40) and its block (0x2E8, where +0x18 points).
    static Emitter ReadEmitter(Reader r, int head)
    {
        uint block = r.U(head + 0x18);
        uint material = r.U(head + 0x1C);
        uint meshData = r.U(head + 0x04);  // mesh particles (type 10): data that starts with the mesh's name
        bool hasBlock = block != None;
        int b = (int)block;
        return new Emitter(
            r.Text(r.U(head)) ?? "",
            ShapeOf(r.U(head + 0x08)),
            hasBlock ? r.Byte(b + 0x02) : -1,
            hasBlock ? (int)r.U(b + 0x1D4) : 0,
            hasBlock ? r.F(b + 0x1CC) : 0,
            hasBlock ? r.F(b + 0x1D0) : 0,
            hasBlock ? r.Colour(b + 0x54) : [0, 0, 0, 0],
            hasBlock ? r.Colour(b + 0x8C) : [0, 0, 0, 0],
            r.Byte(head + 0x0C) != 0, r.Byte(head + 0x0D) != 0, r.Byte(head + 0x0F) != 0,
            (int)r.U(head + 0x10),
            meshData != None ? r.Text(meshData) : null,
            material != None ? ReadMaterial(r, (int)material) : null);
    }

    // rl_particle_material_data: +0x00 the shader (CRC-32 of its .fxo name, see NameHashes.PropertyHash), +0x06 the
    // textures' count and +0x08 their offset: 12 bytes each, a sampler's hash, the effect's bitmap number and a slot.
    static Material? ReadMaterial(Reader r, int at)
    {
        if (at + 0x30 > r.Length) return null;
        int count = r.U16(at + 0x06);
        uint list = r.U(at + 0x08);
        var textures = new List<(uint, int, int)>();
        for (int k = 0; k < count && list != None && list + 12L * (k + 1) <= r.Length; k++)
        {
            int t = (int)list + 12 * k;
            textures.Add((r.U(t), (int)r.U(t + 4), (int)r.U(t + 8)));
        }
        return new Material(r.U(at), textures);
    }

    /// <summary>Where an emitter sends its particles from, by its class id.</summary>
    public static string ShapeOf(uint classId) => classId switch
    {
        0xEC276F11 => "point",
        0x1D815E4A => "sphere",
        0xB3269ACC => "box",
        0xFAA8D302 => "cylinder",
        0xA1CC7F44 => "plane",
        _ => classId.ToString("X8"),
    };

    /// <summary>How a particle is drawn, by its type (Armageddon's particle renderer, 0xC24D90).</summary>
    public static string ParticleTypeName(int type) => type switch
    {
        0 => "billboard",
        1 => "point sprite",
        2 => "ribbon",
        3 => "point",
        4 => "line",
        5 => "radial",
        6 => "node trail",
        7 => "drop",
        8 => "oriented",
        9 => "corona",
        10 => "mesh",
        -1 => "none",
        _ => $"type {type}",
    };

    /// <summary>An effect mesh's type flags: normal, facing, rod, additive, volume fog.</summary>
    public static string MeshTypeName(int type)
    {
        var words = new List<string>();
        if ((type & 1) != 0) words.Add("normal");
        if ((type & 2) != 0) words.Add("facing the camera");
        if ((type & 4) != 0) words.Add("rod");
        if ((type & 8) != 0) words.Add("additive");
        if ((type & 0x10) != 0) words.Add("volume fog");
        return words.Count == 0 ? $"type {type}" : string.Join(", ", words);
    }

    /// <summary>A light's type: 0 a point light, 1 a spot (turned by its forward keys), 2 another kind.</summary>
    public static string LightTypeName(int type) => type switch { 0 => "point", 1 => "spot", _ => $"type {type}" };

    sealed class Reader(byte[] bytes)
    {
        public int Length => bytes.Length;
        public uint U(int at) => at >= 0 && at + 4 <= bytes.Length ? BitConverter.ToUInt32(bytes, at) : None;
        public int U16(int at) => at >= 0 && at + 2 <= bytes.Length ? BitConverter.ToUInt16(bytes, at) : 0;
        public int S16(int at) => at >= 0 && at + 2 <= bytes.Length ? BitConverter.ToInt16(bytes, at) : 0;
        public float F(int at) => at >= 0 && at + 4 <= bytes.Length ? BitConverter.ToSingle(bytes, at) : 0;
        public byte Byte(int at) => at >= 0 && at < bytes.Length ? bytes[at] : (byte)0;
        public float[] Colour(int at) => [F(at), F(at + 4), F(at + 8), F(at + 12)];

        public string? Text(uint at)
        {
            if (at == None || at >= bytes.Length) return null;
            int end = Array.IndexOf(bytes, (byte)0, (int)at);
            return Encoding.Latin1.GetString(bytes, (int)at, (end < 0 ? bytes.Length : end) - (int)at);
        }

        public List<T> List<T>(int countAt, int offsetAt, int size, Func<int, T> read)
        {
            int count = (int)U(countAt);
            uint at = U(offsetAt);
            var items = new List<T>();
            if (count <= 0 || at == None) return items;
            for (int k = 0; k < count && at + (long)size * (k + 1) <= bytes.Length; k++) items.Add(read((int)at + k * size));
            return items;
        }
    }
}
