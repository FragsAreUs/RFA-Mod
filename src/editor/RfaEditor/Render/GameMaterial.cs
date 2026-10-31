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
/// From the game's shaders (ir_default_s, ir_defaultglow_s; see docs\game-notes.md, "Lighting"):
/// the normal map's sideways bend is (2·alpha − 1)(2·red − 1) and its up-down bend 2·green − 1,
/// bent towards flat by Normal_Height; shine is as sharp as Specular_Power × 5.12, coloured by the
/// specular map × Specular_Color; glow is the glow map × Glow_Color × Self_Illumination, fading
/// towards the surface's edges (or middle) by Glow_Falloff_Power and Glow_Falloff_Inversion.
///
/// See-through: only the game's see-through shaders take Main_Opacity (alpha-tested "ir_at_*",
/// glass, water, holograms, masks, magma, the sky; 721 materials), so a material with it is drawn
/// see-through, its colour texture's alpha times Main_Opacity: the bullet holes laid over the first
/// mission's walls (ter_crs_ms1_dcl_a, bullet_holes_001_di.tga), for one.
///
/// Picture: where game light needs another colour picture than the one WPF shows (the ground,
/// whose WPF picture is lifted for even light; see TerrainPainter), made when first needed.
/// DiffuseMap: the colour texture's name, so the Direct3D view can use it as the game stores it
/// (full size) rather than WPF's small decoded copy.
/// </summary>
sealed record GameMaterial(string? NormalMap, string? SpecularMap, string? GlowMap,
                           Vector4 DiffuseColour, float NormalHeight, float SpecularPower, Vector4 SpecularColour,
                           Vector4 GlowColour, float SelfIllumination, float GlowFalloffPower, float GlowFalloffInversion,
                           float? Opacity, Lazy<BitmapSource>? Picture = null, string? DiffuseMap = null)
{
    /// <summary>A surface with nothing of the game's but its own picture for game light (the ground).</summary>
    public static GameMaterial WithPicture(Func<BitmapSource> paint) =>
        new(null, null, null, Vector4.One, 1, 10, Vector4.One, Vector4.One, 0, 1, 1, null, new Lazy<BitmapSource>(paint));

    /// <summary>Whether the material is drawn see-through (it has a Main_Opacity).</summary>
    public bool SeeThrough => Opacity != null;

    /// <summary>Whether a model's material is drawn see-through (see <see cref="SeeThrough"/>).</summary>
    public static bool IsSeeThrough(ModelMaterial material) => material.Constant("Main_Opacity") != null;

    public static readonly DependencyProperty Property =
        DependencyProperty.RegisterAttached("GameMaterial", typeof(GameMaterial), typeof(GameMaterial));

    /// <summary>The game material a WPF material carries, if any.</summary>
    public static GameMaterial? Of(Material? material) => material?.GetValue(Property) as GameMaterial;

    /// <summary>A model's material, as the game's shaders see it (their defaults where it doesn't say).</summary>
    public static GameMaterial From(ModelMaterial material)
    {
        float Number(string name, float otherwise) => material.Constant(name)?.X ?? otherwise;
        string? Map(string slot) => material.Texture(slot) is { Length: > 0 } name ? name : null;
        return new GameMaterial(Map("Normal_Map"), Map("Specular_Map"), Map("Glow_Map"),
                                material.Constant("Diffuse_Color") ?? Vector4.One,
                                Number("Normal_Height", 1), Number("Specular_Power", 10), material.Constant("Specular_Color") ?? Vector4.One,
                                material.Constant("Glow_Color") ?? Vector4.One, Number("Self_Illumination", 1),
                                Number("Glow_Falloff_Power", 1), Number("Glow_Falloff_Inversion", 1),
                                material.Constant("Main_Opacity")?.X, DiffuseMap: Map("Diffuse_Map") ?? material.Diffuse);
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
