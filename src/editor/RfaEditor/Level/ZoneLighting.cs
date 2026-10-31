using System.Numerics;
using RfaFormats.Zones;

namespace RfaEditor;

/// <summary>
/// A zone's lighting as the game's renderer uses it, for the Direct3D view's game lighting. Read
/// from the zone's objects; how each part lights is taken from the game's own shaders (see
/// docs\game-notes.md, "Lighting"):
///
/// Lamps (obj_light; 38,240 in the game). type_enum is "omni" (29,322), "spot" (5,124), "tube"
/// (3,507: a line of light, tube_length long) or "ambient" (287: light from one side over an
/// area). Color is red, green, blue and a brightness (usually 1, up to 150); Back_Color likewise,
/// for the side facing away. A lamp is full strength to atten_start metres, and fades to nothing
/// over atten_range more: strength (1 - t)^atten_power, t going from 0 to 1 over the fade (spots
/// fade with a cube, and use the power on their cone). A surface facing the lamp gets its colour, one
/// side-on a colour half way to the back colour, one facing away the back colour. A spot shines
/// along the light's forward direction, full inside hotspot_size degrees, fading over
/// hotspot_falloff_size more.
///
/// Environments (object_env_settings: boxes, Bb around oP). Each says which settings it gives
/// (affects_coloration: the ambient light and tint; affects_hdr: the exposure; affects_fog); where
/// several hold a point, the smallest box gives each setting. The ambient is front_color from above,
/// Back_Color from below (each colour times its fourth number), blended by which way a surface faces.
/// Exposure: min_exposure and max_exposure (the game adapts between them towards desired_brightness;
/// most caves fix both at 3).
///
/// Lamps that flicker or sway name curves (see <see cref="LightCurve"/>): multi_curve for brightness,
/// trans_curve for position, played at anim_speed_multiplier, from anim_start_offset_ms, the
/// brightness curve's effect times anim_inten_multiplier.
///
/// Shadows: a lamp with casts_shadows set throws them (561 lamps, 543 of them spots), as dark as
/// shadow_density, out to shadow_distance metres from the camera; shadow_projection is
/// "perspective" (along its cone) or "orthographic" (straight along it). See LampShadows.
///
/// The sky: outdoors, the game draws the sky with an effect, a dome textured with a picture of the
/// whole sky as seen from below (straight up in the middle, the horizon round the edge;
/// vfx_sky_day.tga and the like). An environment names it as one of its camera effects
/// (cam_effect_1 to 3: "env_sur_day_sky", "env_mis7_night_sky", ...), shown while the camera is in
/// its box, or the zone places it (an object_effect's effect_type, an effect_streaming_node's
/// streamed_effect; the first mission's world_08_03 does).
/// </summary>
sealed class ZoneLighting
{
    public enum LampKind { Omni, Spot, Tube, Ambient }

    /// <summary>
    /// A lamp, in the game's coordinates. Colours are already times their brightness. Flicker and
    /// Sway are its curves, if it has any (brightness and position), played at CurveSpeed from
    /// CurveStart seconds, the flicker times CurveStrength. Whether it casts shadows, how dark, to
    /// how far from the camera, and whether straight along it rather than along its cone.
    /// </summary>
    public sealed record Lamp(LampKind Kind, Vector3 Position, Vector3 Forward, Vector3 Front, Vector3 Back,
                              float FullTo, float GoneAt, float Power, float Hotspot, float Falloff, float TubeLength,
                              LightCurve? Flicker = null, LightCurve? Sway = null, float CurveSpeed = 1, float CurveStart = 0, float CurveStrength = 1,
                              bool CastsShadows = false, float ShadowDensity = 1, float ShadowDistance = 30, bool ShadowOrthographic = false)
    {
        public bool Moves => Flicker != null || Sway != null;

        /// <summary>The lamp's brightness multiplier and position offset at a time (in seconds).</summary>
        public (float Brightness, Vector3 Offset) At(float seconds)
        {
            float t = CurveStart + seconds * CurveSpeed;
            float brightness = Flicker != null ? Flicker.At(t).X * CurveStrength : 1;
            Vector4 sway = Sway?.At(t) ?? Vector4.Zero;
            return (brightness, new Vector3(sway.X, sway.Y, sway.Z));
        }
    }

    /// <summary>The settings in force at a point (from the environments around it).</summary>
    /// <param name="Sky">The sky's picture (a texture's file name, "vfx_sky_day.tga"); null where there is no sky.</param>
    public readonly record struct Settings(Vector3 AmbientAbove, Vector3 AmbientBelow, Vector3 Tint, float Exposure,
                                           Vector3 FogColour, float FogNear, float FogFar, bool Fog, string? Sky = null);

    sealed record Environment(Vector3 Centre, Matrix4x4 Rotation, Vector3 Min, Vector3 Max, float Size,
                              bool Colour, bool Hdr, bool FogOn, Vector3 Above, Vector3 Below, Vector3 Tint,
                              float Exposure, Vector3 FogColour, float FogNear, float FogFar, string? Sky);

    public IReadOnlyList<Lamp> Lamps { get; }
    readonly List<Environment> environments;
    readonly string? placedSky;  // the sky an effect in the zone draws, if any

    /// <summary>Where no environment says otherwise: dim, as most of the game is underground.</summary>
    public static readonly Settings Default = new(new Vector3(0.25f), new Vector3(0.05f), Vector3.One, 1, Vector3.Zero, 0, 0, false);

    ZoneLighting(List<Lamp> lamps, List<Environment> environments, string? placedSky)
    {
        Lamps = lamps;
        this.environments = environments;
        this.placedSky = placedSky;
    }

    public int EnvironmentCount => environments.Count;

    /// <param name="curve">Reads a lamp curve by the name a lamp gives ("la_fire_flicker_001.lightmult"); null if it can't be found.</param>
    /// <param name="sky">The sky's picture an effect draws, by the effect's name ("env_sur_day_sky"); null if it isn't a sky or can't be found (see <see cref="SkyPicture"/>).</param>
    public static ZoneLighting Read(IEnumerable<ZoneObject> objects, Func<string, LightCurve?>? curve = null, Func<string, string?>? sky = null)
    {
        LightCurve? Curve(ZoneObject o, string property) =>
            curve != null && o.Property(property)?.Text is { Length: > 0 } name && !name.Equals("NONE", StringComparison.OrdinalIgnoreCase) ? curve(name) : null;
        string? Sky(ZoneObject o, params string[] properties) =>
            sky == null ? null : properties.Select(p => o.Property(p)?.Text).Where(name => name is { Length: > 0 } && !name.Equals("NONE", StringComparison.OrdinalIgnoreCase))
                                           .Select(name => sky(name!)).FirstOrDefault(picture => picture != null);
        uint lightClass = NameHashes.ClassHash("obj_light"), environmentClass = NameHashes.ClassHash("object_env_settings");
        uint effectClass = NameHashes.ClassHash("object_effect"), streamedClass = NameHashes.ClassHash("effect_streaming_node");
        var lamps = new List<Lamp>();
        var environments = new List<Environment>();
        string? placedSky = null;
        foreach (ZoneObject o in objects)
        {
            if (o.ClassHash == effectClass || o.ClassHash == streamedClass)
                placedSky ??= Sky(o, "effect_type", "streamed_effect");
            if (o.ClassHash == lightClass)
            {
                LampKind kind = o.Property("type_enum")?.Text switch
                {
                    "spot" => LampKind.Spot,
                    "tube" => LampKind.Tube,
                    "ambient" => LampKind.Ambient,
                    _ => LampKind.Omni,
                };
                (Vector3 position, Matrix4x4 rotation) = o.Placement;
                float start = Number(o, "atten_start") ?? 0, range = Number(o, "atten_range") ?? 5;
                lamps.Add(new Lamp(kind, position, new Vector3(rotation.M31, rotation.M32, rotation.M33),
                                   Colour(o, "Color") ?? Vector3.One, Colour(o, "Back_Color") ?? Vector3.Zero,
                                   start, start + Math.Max(0.01f, range), Number(o, "atten_power") ?? 2,
                                   Number(o, "hotspot_size") ?? 20, Number(o, "hotspot_falloff_size") ?? 10, Number(o, "tube_length") ?? 1,
                                   Curve(o, "multi_curve"), Curve(o, "trans_curve"),
                                   Number(o, "anim_speed_multiplier") ?? 1, (Number(o, "anim_start_offset_ms") ?? 0) / 1000,
                                   Number(o, "anim_inten_multiplier") ?? 1,
                                   Flag(o, "casts_shadows"), Number(o, "shadow_density") ?? 1, Number(o, "shadow_distance") ?? 30,
                                   o.Property("shadow_projection")?.Text == "orthographic"));
            }
            else if (o.ClassHash == environmentClass && o.Property("Bb") is { Size: 24 } box)
            {
                float[] b = box.Floats();
                (Vector3 centre, Matrix4x4 rotation) = o.Placement;
                Vector3 min = new(b[0], b[1], b[2]), max = new(b[3], b[4], b[5]);
                Vector3 size = max - min;
                float low = Number(o, "min_exposure") ?? 1, high = Number(o, "max_exposure") ?? low;
                environments.Add(new Environment(centre, rotation, min, max, size.X * size.Y * size.Z,
                    Flag(o, "affects_coloration"), Flag(o, "affects_hdr"), Flag(o, "affects_fog"),
                    Colour(o, "front_color") ?? Vector3.One, Colour(o, "Back_Color") ?? Vector3.Zero,
                    Colour(o, "scene_tint", brightness: false) ?? Vector3.One,
                    // An exposure range the game adapts within: the middle of it, as the editor doesn't adapt.
                    (low + high) / 2,
                    Colour(o, "fog_color", brightness: false) ?? Vector3.Zero, Number(o, "fog_near") ?? 0, Number(o, "fog_far") ?? 0,
                    Sky(o, "cam_effect_1", "cam_effect_2", "cam_effect_3")));
            }
        }
        return new ZoneLighting(lamps, environments, placedSky);
    }

    /// <summary>
    /// The sky's picture an effect file (.cefct_pc) draws, by the textures it names: a sky's are
    /// named "vfx_sky_..." or "sky_..." (not its clouds or masks), and where an effect has two (the
    /// usual vfx_sky_day.tga and its own, as env_mis7_night_sky has vfx_sky_night_mis7.tga), its
    /// own. Null if it names none (it isn't a sky: lightning, dust, ...). All eleven sky effects in
    /// the game name one this way.
    /// </summary>
    public static string? SkyPicture(byte[] effectFile)
    {
        string text = System.Text.Encoding.Latin1.GetString(effectFile);
        // Whole names only: not the "sky_..." at the end of "env_sky_...".
        List<string> skies = System.Text.RegularExpressions.Regex.Matches(text, @"(?<![A-Za-z0-9_])(?:vfx_)?sky_[A-Za-z0-9_]*\.tga")
            .Select(m => m.Value)
            .Where(name => !name.Contains("cloud") && !name.Contains("mask"))
            .Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        return skies.FirstOrDefault(name => !name.Equals("vfx_sky_day.tga", StringComparison.OrdinalIgnoreCase)) ?? skies.FirstOrDefault();
    }

    /// <summary>
    /// The settings at a point (in the game's coordinates): for each, the smallest box around it that
    /// gives it. The sky likewise; where no box around the point names one (the editor's camera is
    /// often above the boxes), the sky the zone places, else the biggest box's.
    /// </summary>
    public Settings At(Vector3 point)
    {
        Environment? Smallest(Func<Environment, bool> gives) =>
            environments.Where(e => gives(e) && Holds(e, point)).MinBy(e => e.Size);
        Environment? colour = Smallest(e => e.Colour), hdr = Smallest(e => e.Hdr), fog = Smallest(e => e.FogOn);
        string? sky = Smallest(e => e.Sky != null)?.Sky ?? placedSky ?? environments.Where(e => e.Sky != null).MaxBy(e => e.Size)?.Sky;
        return new Settings(colour?.Above ?? Default.AmbientAbove, colour?.Below ?? Default.AmbientBelow, colour?.Tint ?? Vector3.One,
                            hdr?.Exposure ?? Default.Exposure,
                            fog?.FogColour ?? Vector3.Zero, fog?.FogNear ?? 0, fog?.FogFar ?? 0, fog != null && fog.FogFar > fog.FogNear, sky);
    }

    /// <summary>The sky pictures the zone names anywhere (for the log).</summary>
    public IEnumerable<string> Skies => environments.Select(e => e.Sky).Append(placedSky).OfType<string>().Distinct(StringComparer.OrdinalIgnoreCase);

    static bool Holds(Environment e, Vector3 point)
    {
        // Into the box's own space: from its middle, turned back by its rotation (whose rows are its axes).
        Vector3 d = point - e.Centre;
        var local = new Vector3(Vector3.Dot(d, new Vector3(e.Rotation.M11, e.Rotation.M12, e.Rotation.M13)),
                                Vector3.Dot(d, new Vector3(e.Rotation.M21, e.Rotation.M22, e.Rotation.M23)),
                                Vector3.Dot(d, new Vector3(e.Rotation.M31, e.Rotation.M32, e.Rotation.M33)));
        return local.X >= e.Min.X && local.Y >= e.Min.Y && local.Z >= e.Min.Z && local.X <= e.Max.X && local.Y <= e.Max.Y && local.Z <= e.Max.Z;
    }

    static float? Number(ZoneObject o, string name) => o.Property(name) is { Size: 4 } p && float.IsFinite(p.Floats()[0]) ? p.Floats()[0] : null;

    static bool Flag(ZoneObject o, string name) => o.Property(name) is { Size: 1 } p && p.Value[0] != 0;

    /// <summary>A colour property (red, green, blue, brightness), times its brightness unless told not to.</summary>
    static Vector3? Colour(ZoneObject o, string name, bool brightness = true)
    {
        if (o.Property(name) is not { Size: >= 12 } p) return null;
        float[] c = p.Floats();
        var colour = new Vector3(c[0], c[1], c[2]);
        return brightness && c.Length > 3 ? colour * c[3] : colour;
    }
}
