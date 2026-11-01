using System.Numerics;
using System.Text;
using RfaFormats.Zones;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's model materials rebuilt for Armageddon's shaders. Guerrilla's are its own
/// ("rfg-nodecal", "rfg-nospecnodecal", "rfg-metal", "rfg-singledecal", "rfg-standard", ...: 33 in
/// its world chunk files), none of which Armageddon has, so each material is written for the
/// nearest of Armageddon's by the textures it has: colour, normal and specular map ir_default (as
/// Guerrilla's metal, brdf and environment-mapped kinds, which lose their extras); colour and normal
/// ir_nospec; colour and specular ir_nonormal; colour only ir_nospec_nonormal; with a decal
/// ir_singledecal, with two ir_twodecal. A cut-out material (flags 2) is ir_at_default, and so is
/// any other see-through one (flags 1). Guerrilla's glass (its shaders rfg-glass, flags 1, and
/// rfg-eosglass, flags 0) is ir_carglass, the shader Armageddon's glass buildings draw their whole
/// panes with, blended (flags 0x41) as Armageddon's glass is: Guerrilla's colour map (its alpha the
/// pane's see-through), white for the pattern map, Armageddon's reflection cube
/// str_gls_cube_default_a (from its dlc01_precache), and its window panes' values (no dust, opacity
/// 1, reflection brightness 0.5 and contrast 0.1). Not ir_glass_shatter, which those buildings give
/// their broken-off pieces: it reads four UV channels, the last three the corners of each shard's
/// triangle, and discards every pixel outside it; Guerrilla's panes have no such channels, and drawn
/// with it they show in fragments.
///
/// A material block in both games: its size, its shader's and its own name hashes, flags (Guerrilla 0
/// / 1 / 2, Armageddon 0x40 | the same), the texture count (u16), the constant count and room (u8s),
/// three run-time words; Armageddon then a 0; then 12 bytes a texture (where its name starts in the
/// file's name list, its slot hash, its number: Guerrilla's from +0x20, Armageddon's +0x24), the
/// constants' name hashes, and from the next multiple of 16 room for their values (a float4 each).
/// Both games hash the shader, slot and constant names as zone property names (lower-case CRC-32), and
/// share the colour, normal and specular slots; Guerrilla's decal slots (Decal_Map_1, Decal_Map_2) are
/// Armageddon's Decal1_Map and Decal2_Map, its constants named longer (Diffuse_Map_TilingU for
/// Diffuse_TilingU, Normal_Map_Height for Normal_Height, Decal_Map_1_Opacity for Decal1_Opacity).
/// A slot the chosen shader has and the material hasn't is filled with a built-in texture (see
/// <see cref="GuerrillaBuiltIns"/>): flat normals, no specular, white. A slot whose texture Guerrilla
/// names "missing" counts as one the material hasn't (see <see cref="IsEmpty"/>).
///
/// Values: Guerrilla's shine is the half-way vector's (N · H)^Specular_Power (0–3,000, median 100),
/// Armageddon's the reflection's (R · V)^(Specular_Power × 5.12) (its power capped at 50). The
/// reflection's angle is about twice the half-way vector's, so an exponent of Guerrilla's is about four
/// times Armageddon's for the same highlight: its powers are taken / (4 × 5.12). Guerrilla's
/// Specular_Alpha scales its specular colour (Armageddon's goes past 1). Constants Guerrilla has no
/// counterpart for are given Armageddon's commonest values.
/// </summary>
public static class GuerrillaMaterials
{
    /// <summary>The built-in textures filling slots a material hasn't: Guerrilla's own built-ins for white and flat normals, and ours for no specular.</summary>
    public const string White = "misc-white", FlatNormal = "flat-normalmap", NoSpecular = "rfg_nospec";

    /// <summary>The reflection cube glass is given: Armageddon's own (in its dlc01_precache.vpp_pc), named as it is.</summary>
    public const string GlassReflection = "str_gls_cube_default_a";

    static readonly uint NormalMap = H("Normal_Map"), DiffuseMap = H("Diffuse_Map"), SpecularMap = H("Specular_Map"),
        Decal1Map = H("Decal1_Map"), Decal2Map = H("Decal2_Map"), GuerrillaDecal1 = H("Decal_Map_1"), GuerrillaDecal2 = H("Decal_Map_2"),
        PatternMap = H("Pattern_map"), ReflectionMap = H("reflection_map");

    /// <summary>Guerrilla's glass shaders.</summary>
    static readonly HashSet<uint> GuerrillaGlass = [H("rfg-glass"), H("rfg-eosglass")];

    /// <summary>One of Armageddon's shaders: its texture slots in its sampler order, and its constants (from the shader's header, without the engine's own).</summary>
    sealed record Shader(string Name, uint[] Slots, string[] Constants);

    static readonly Shader Default = new("ir_default", [NormalMap, DiffuseMap, SpecularMap],
        ["Normal_Height", "Specular_Power", "Diffuse_TilingU", "Diffuse_TilingV", "Diffuse_color", "Specular_Color"]);
    static readonly Shader NoSpec = new("ir_nospec", [NormalMap, DiffuseMap],
        ["Normal_Height", "Diffuse_TilingU", "Diffuse_TilingV", "Diffuse_color"]);
    static readonly Shader NoNormal = new("ir_nonormal", [DiffuseMap, SpecularMap],
        ["Specular_Power", "Diffuse_color", "Specular_Color", "Diffuse_TilingU", "Diffuse_TilingV"]);
    static readonly Shader ColourOnly = new("ir_nospec_nonormal", [DiffuseMap],
        ["Diffuse_color", "Diffuse_TilingU", "Diffuse_TilingV"]);
    static readonly Shader SingleDecal = new("ir_singledecal", [NormalMap, DiffuseMap, SpecularMap, Decal1Map],
        ["Normal_Height", "Specular_Power", "Diffuse_TilingU", "Diffuse_TilingV", "Diffuse_color", "Specular_Color",
         "Decal1_TilingU", "Decal1_TilingV", "Decal1_Opacity", "Decal1_Color"]);
    static readonly Shader TwoDecal = new("ir_twodecal", [NormalMap, DiffuseMap, SpecularMap, Decal1Map, Decal2Map],
        ["Normal_Height", "Specular_Power", "Diffuse_TilingU", "Diffuse_TilingV", "Diffuse_color", "Specular_Color",
         "Decal1_TilingU", "Decal1_TilingV", "decal2_tilingU", "decal2_tilingV", "Decal1_Opacity", "Decal2_Opacity",
         "Decal1_Color", "decal2_color", "reflection_brightness", "reflection_contrast"]);
    static readonly Shader CutOut = new("ir_at_default", [DiffuseMap, NormalMap, SpecularMap],
        ["Diffuse_TilingU", "Diffuse_TilingV", "Normal_Height", "Specular_Power", "Diffuse_color", "Specular_Color"]);
    static readonly Shader Glass = new("ir_carglass", [DiffuseMap, PatternMap, ReflectionMap],
        ["Diffuse_color", "Dust_Amount", "reflection_brightness", "reflection_contrast", "Dust_Color", "Main_Opacity"]);

    /// <summary>Whether the Guerrilla material block at <paramref name="at"/> is glass (one of Guerrilla's glass shaders).</summary>
    internal static bool IsGlass(byte[] file, int at) => GuerrillaGlass.Contains(BitConverter.ToUInt32(file, at + 0x04));

    /// <summary>
    /// Armageddon's material block for the Guerrilla one at <paramref name="at"/> in <paramref name="file"/>.
    /// Its textures' names are in <paramref name="names"/>, and the built-ins it needs are added there.
    /// </summary>
    internal static byte[] Convert(byte[] file, int at, TextureNameList names)
    {
        int textureCount = BitConverter.ToUInt16(file, at + 0x10), constantCount = file[at + 0x12];
        uint flags = BitConverter.ToUInt32(file, at + 0x0C);
        var textures = new Dictionary<uint, int>();  // slot → where its name starts
        for (int t = 0; t < textureCount; t++)
        {
            int e = at + 0x20 + t * 12;
            uint slot = BitConverter.ToUInt32(file, e + 4);
            if (IsEmpty(names.At(BitConverter.ToInt32(file, e)))) continue;
            textures.TryAdd(slot == GuerrillaDecal1 ? Decal1Map : slot == GuerrillaDecal2 ? Decal2Map : slot, BitConverter.ToInt32(file, e));
        }
        var constants = new Dictionary<uint, Vector4>();
        int hashesAt = at + 0x20 + textureCount * 12, valuesAt = at + Align(0x20 + textureCount * 12 + constantCount * 4, 16);
        for (int c = 0; c < constantCount; c++)
            constants[BitConverter.ToUInt32(file, hashesAt + c * 4)] = Read(file, valuesAt + c * 16);

        bool glass = IsGlass(file, at), cutOut = !glass && (flags & 3) != 0;
        Shader shader = glass ? Glass
            : cutOut ? CutOut
            : textures.ContainsKey(Decal2Map) ? TwoDecal
            : textures.ContainsKey(Decal1Map) ? SingleDecal
            : textures.ContainsKey(NormalMap) && textures.ContainsKey(SpecularMap) ? Default
            : textures.ContainsKey(NormalMap) ? NoSpec
            : textures.ContainsKey(SpecularMap) ? NoNormal
            : ColourOnly;

        int room = shader.Constants.Length * 2;
        int hashes = 0x24 + shader.Slots.Length * 12, values = Align(hashes + shader.Constants.Length * 4, 16), size = values + room * 16;
        var block = new byte[size];
        void Put(int o, uint v) => BitConverter.TryWriteBytes(block.AsSpan(o), v);
        Put(0x00, (uint)size);
        Put(0x04, H(shader.Name));
        Array.Copy(file, at + 0x08, block, 0x08, 4);   // the material's own name hash
        Put(0x0C, 0x40u | (glass ? 1u : cutOut ? 2u : 0u));
        BitConverter.TryWriteBytes(block.AsSpan(0x10), (ushort)shader.Slots.Length);
        block[0x12] = (byte)shader.Constants.Length;
        block[0x13] = (byte)room;
        Array.Copy(file, at + 0x14, block, 0x14, 12);  // run-time words
        for (int t = 0; t < shader.Slots.Length; t++)
        {
            uint slot = shader.Slots[t];
            int name = textures.TryGetValue(slot, out int n) && slot != ReflectionMap ? n
                : names.Add((slot == NormalMap ? FlatNormal : slot == SpecularMap ? NoSpecular : slot == ReflectionMap ? GlassReflection : White) + ".tga");
            Put(0x24 + t * 12, (uint)name);
            Put(0x28 + t * 12, slot);
            Put(0x2C + t * 12, (uint)t);
        }
        for (int c = 0; c < shader.Constants.Length; c++)
        {
            Put(hashes + c * 4, H(shader.Constants[c]));
            Write(block, values + c * 16, Value(shader.Constants[c], constants, glass));
        }
        return block;
    }

    /// <summary>
    /// Whether a slot's texture is Guerrilla's name for none: "missing" (in 3,363 world chunk
    /// materials, 196 decal slots, 101 specular maps and 9 environment maps; never a colour or normal
    /// map, and no file has the name). Such a slot is left out, as if the material hadn't it: filled
    /// with white it painted white decals over objects and made their metal shine pale.
    /// </summary>
    static bool IsEmpty(string texture) =>
        string.Equals(texture, "missing", StringComparison.OrdinalIgnoreCase) || string.Equals(texture, "missing.tga", StringComparison.OrdinalIgnoreCase);

    /// <summary>An Armageddon constant's value from the Guerrilla material's constants.</summary>
    static Vector4 Value(string constant, Dictionary<uint, Vector4> from, bool glass)
    {
        Vector4? Get(string name) => from.TryGetValue(H(name), out var v) ? v : null;
        static Vector4 X(float x) => new(x, 0, 0, 0);
        return constant switch
        {
            "Diffuse_color" => Get("Diffuse_Color") ?? Vector4.One,
            "Diffuse_TilingU" => Get("Diffuse_Map_TilingU") ?? X(1),
            "Diffuse_TilingV" => Get("Diffuse_Map_TilingV") ?? X(1),
            "Normal_Height" => Get("Normal_Map_Height") ?? X(1),
            "Specular_Power" => X(Math.Clamp((Get("Specular_Power")?.X ?? 100) / (4 * 5.12f), 0, 50)),
            "Specular_Color" => (Get("Specular_Color") ?? Vector4.One) * (Get("Specular_Alpha")?.X ?? 1),
            "Decal1_TilingU" => Get("Decal_Map_1_TilingU") ?? X(1),
            "Decal1_TilingV" => Get("Decal_map_1_TilingV") ?? X(1),
            "Decal1_Opacity" => Get("Decal_Map_1_Opacity") ?? X(1),
            "Decal2_Opacity" => Get("Decal_Map_2_Opacity") ?? X(1),
            "decal2_tilingU" or "decal2_tilingV" => X(1),
            "Decal1_Color" or "decal2_color" => Vector4.One,
            "reflection_brightness" => X(glass ? 0.5f : 0),
            "reflection_contrast" => X(glass ? 0.1f : 0),
            "Dust_Amount" => X(0),
            "Dust_Color" => Vector4.One,
            "Main_Opacity" => X(1),
            _ => throw new InvalidOperationException($"no value for {constant}"),
        };
    }

    static uint H(string name) => NameHashes.PropertyHash(name);
    static int Align(int value, int to) => (value + to - 1) / to * to;

    static Vector4 Read(byte[] d, int at) =>
        new(BitConverter.ToSingle(d, at), BitConverter.ToSingle(d, at + 4), BitConverter.ToSingle(d, at + 8), BitConverter.ToSingle(d, at + 12));

    static void Write(byte[] d, int at, Vector4 v)
    {
        BitConverter.TryWriteBytes(d.AsSpan(at), v.X); BitConverter.TryWriteBytes(d.AsSpan(at + 4), v.Y);
        BitConverter.TryWriteBytes(d.AsSpan(at + 8), v.Z); BitConverter.TryWriteBytes(d.AsSpan(at + 12), v.W);
    }
}

/// <summary>A model's texture name list (after its mesh block): names one after another, each named by where it starts.</summary>
sealed class TextureNameList(ReadOnlySpan<byte> list)
{
    readonly List<byte> bytes = [.. list];

    public byte[] ToArray() => [.. bytes];

    /// <summary>The name starting at <paramref name="offset"/>.</summary>
    public string At(int offset)
    {
        int end = offset;
        while (end < bytes.Count && bytes[end] != 0) end++;
        return Encoding.ASCII.GetString([.. bytes.GetRange(offset, end - offset)]);
    }

    /// <summary>Where a name starts: where it already is, else after the others (with two NULs, as Armageddon's lists end their names).</summary>
    public int Add(string name)
    {
        for (int at = 0; at < bytes.Count; at++)
            if ((at == 0 || bytes[at - 1] == 0) && bytes[at] != 0 && At(at) == name) return at;
        int start = bytes.Count;
        bytes.AddRange(Encoding.ASCII.GetBytes(name));
        bytes.Add(0); bytes.Add(0);
        return start;
    }
}
