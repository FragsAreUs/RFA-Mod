using System.Numerics;
using System.Windows;
using System.Windows.Media.Imaging;
using System.Windows.Media.Media3D;
using RfaFormats.Meshes;

namespace RfaEditor.Render;

/// <summary>
/// What the game's shaders know about a surface beyond its colour texture, carried on the WPF
/// material the level view's builders make (an attached property, so it rides along wherever the
/// material goes), for the Direct3D view's game light to use. Texture names only: the textures are
/// read when the Direct3D view first needs them.
///
/// From the game's shaders (ir_default_s, ir_defaultglow_s):
/// the normal map's sideways bend is (2·alpha − 1)(2·red − 1) and its up-down bend 2·green − 1,
/// bent towards flat by Normal_Height; shine is as sharp as Specular_Power × 5.12, coloured by the
/// specular map × Specular_Color; glow is the glow map × Glow_Color × Self_Illumination, fading
/// towards the surface's edges (or middle) by Glow_Falloff_Power and Glow_Falloff_Inversion.
///
/// See-through: as the material's flags say (<see cref="ModelMaterial.Flags"/>). Blended ones
/// (glass, water, holograms) are laid over what is behind them by their colour texture's alpha
/// times Main_Opacity; cut-out ones (hair, capes, the bullet holes laid over the first mission's
/// walls: ter_crs_ms1_dcl_a, bullet_holes_001_di.tga) are solid where that alpha is at least the
/// game's threshold (taken as 0.5) and missing elsewhere. Every other material is solid, Main_Opacity
/// or not: cloth, walkers and alien skin take one too, and their colour textures' alpha holds
/// something else (the tentacles' is nearly 0 all over).
///
/// Alien skin (ir_alienskin and ir_alienskin_simple: the Legion's creatures, tentacles and pods),
/// recognised by its constants: it alone has Rim_Light_Amount without Glow_Falloff_Inversion, and
/// the full one SSS_Power. It doesn't multiply its colour by Diffuse_Color: it moves it towards
/// Diffuse_Color × the colour's brightness by Rim_Light_Amount (0 on the grunts, whose
/// Diffuse_Color is red). Its glow shows only where the specular map's alpha is (squared; 1 to 4%
/// of the texture on average), faded where the light is strong (by Rim_Light_Power, or for the
/// simple one Glow_Falloff_Power) and, for the full one, towards the edges by Glow_Falloff_Power.
/// The game reads its glow map through the mesh's second texture coordinates, scrolling; the editor
/// reads only the first, so it takes the glow map's average colour. Shine is the specular map's
/// green × Specular_Color. (Its rim of light-coloured "subsurface" scattering isn't drawn.)
///
/// Picture: where game light needs another colour picture than the one WPF shows (the ground,
/// whose WPF picture is lifted for even light; see TerrainPainter), made when first needed.
/// DiffuseMap: the colour texture's name, so the Direct3D view can use it as the game stores it
/// (full size) rather than WPF's small decoded copy.
/// </summary>
sealed record GameMaterial(string? NormalMap, string? SpecularMap, string? GlowMap,
                           Vector4 DiffuseColour, float NormalHeight, float SpecularPower, Vector4 SpecularColour,
                           Vector4 GlowColour, float SelfIllumination, float GlowFalloffPower, float GlowFalloffInversion,
                           float? Opacity, Lazy<BitmapSource>? Picture = null, string? DiffuseMap = null,
                           bool CutOut = false, AlienSkin? Skin = null)
{
    /// <summary>A surface with nothing of the game's but its own picture for game light (the ground).</summary>
    public static GameMaterial WithPicture(Func<BitmapSource> paint) =>
        new(null, null, null, Vector4.One, 1, 10, Vector4.One, Vector4.One, 0, 1, 1, null, new Lazy<BitmapSource>(paint));

    /// <summary>Whether the material is laid over what is behind it (blended; <see cref="Opacity"/> is then its Main_Opacity).</summary>
    public bool SeeThrough => Opacity != null;

    /// <summary>Whether a model's material needs its colour texture's alpha: blended or cut out.</summary>
    public static bool UsesAlpha(ModelMaterial material) => material.Blended || material.CutOut;

    /// <summary>How solid a model's material is drawn as a whole: its Main_Opacity if it is blended, else 1.</summary>
    public static double WholeOpacity(ModelMaterial material) =>
        material.Blended ? Math.Clamp(material.Constant("Main_Opacity")?.X ?? 1, 0, 1) : 1;

    public static readonly DependencyProperty Property =
        DependencyProperty.RegisterAttached("GameMaterial", typeof(GameMaterial), typeof(GameMaterial));

    /// <summary>The game material a WPF material carries, if any.</summary>
    public static GameMaterial? Of(Material? material) => material?.GetValue(Property) as GameMaterial;

    /// <summary>A model's material, as the game's shaders see it (their defaults where it doesn't say).</summary>
    public static GameMaterial From(ModelMaterial material)
    {
        float Number(string name, float otherwise) => material.Constant(name)?.X ?? otherwise;
        string? Map(string slot) => material.Texture(slot) is { Length: > 0 } name ? name : null;
        AlienSkin? skin = material.Constant("Rim_Light_Amount") is { } amount && material.Constant("Glow_Falloff_Inversion") == null
            ? new AlienSkin(material.Constant("SSS_Power") != null, amount.X, Number("Rim_Light_Power", 1))
            : null;
        return new GameMaterial(Map("Normal_Map"), Map("Specular_Map"), Map("Glow_Map"),
                                material.Constant("Diffuse_Color") ?? Vector4.One,
                                Number("Normal_Height", 1), Number("Specular_Power", 10), material.Constant("Specular_Color") ?? Vector4.One,
                                material.Constant("Glow_Color") ?? Vector4.One, Number("Self_Illumination", 1),
                                Number("Glow_Falloff_Power", 1), Number("Glow_Falloff_Inversion", 1),
                                material.Blended ? (float)WholeOpacity(material) : null, DiffuseMap: Map("Diffuse_Map") ?? material.Diffuse,
                                CutOut: material.CutOut, Skin: skin);
    }

    /// <summary>The material carrying this (a copy if the one given is frozen), frozen.</summary>
    public Material Attach(Material material)
    {
        Material carrier = material.IsFrozen ? material.Clone() : material;
        carrier.SetValue(Property, this);
        carrier.Freeze();
        return carrier;
    }
}

/// <summary>
/// An alien skin material's own numbers (see <see cref="GameMaterial"/>): whether it is the full
/// shader (ir_alienskin) or the simple one, how far its colour moves towards Diffuse_Color
/// (Rim_Light_Amount), and how quickly its glow fades in strong light (Rim_Light_Power).
/// </summary>
sealed record AlienSkin(bool Full, float RimLightAmount, float RimLightPower);
