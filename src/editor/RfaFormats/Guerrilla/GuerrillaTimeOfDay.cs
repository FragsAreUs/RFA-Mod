using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's time of day for a world ported from it: its sun and moons (the world's
/// .gtodx) and a set of curves (its _Default.dtodx), both XML, read by Armageddon by the world's name.
///
/// The orbitals are Guerrilla's terr01.gtodx with Armageddon's names: its corona effect
/// (env_sun_corona; Guerrilla's effects don't load in it) and texture names without the extension (as
/// its world.gtodx; vfx_sun_01, deimos_d and phobos_d are in its misc.vpp_pc bitmaps).
///
/// The set: the games read most curves on other scales (Guerrilla's fog density 15 where
/// Armageddon's are 0.0001, its colour correction a blue tint × 0.54 where Armageddon's is grey × 4,
/// its sky colours about a twentieth of Armageddon's): Guerrilla's Oasis set as it is gave a black
/// world. So the set is Armageddon's own (world_default: exposure, colour correction, fog), with
/// Guerrilla's Oasis sky and clouds as they are (the guerrilla_world mod draws them, and sets their
/// brightness itself), its sun's light (which the mod draws; world_default's is black at noon), and
/// its ambient light from above and below with each key's brightness folded into its colour (see
/// <see cref="Linear"/>), which the mod's light pass lights the ground with.
/// </summary>
public static class GuerrillaTimeOfDay
{
    static readonly string[] Sky = ["east0", "east1", "east2", "east3", "west0", "west1", "west2", "west3", "zenith"];
    static readonly string[] WithSky = ["clouds_horizon_front_light_color", "clouds_horizon_rear_light_color", "horizon_mountain_color",
                                        "horizon_mountain_color_back", "horizon_mountain_fog_color", "clouds_front_light_color", "clouds_rear_light_color"];
    static readonly string[] AsTheyAre = ["cloud_layer01_speed", "cloud_layer23_speed", "cloud_backlight_power", "cloud_backlight_strength",
                                          "overhead_layer0_strength", "overhead_layer1_strength", "overhead_layer2_strength", "overhead_layer3_strength",
                                          "horizon_layer0_strength", "horizon_layer1_strength", "horizon_layer2_strength", "horizon_layer3_strength"];

    /// <summary>The orbitals file and the set of curves, from Armageddon's and Guerrilla's misc.vpp_pc.</summary>
    public static (byte[] Orbitals, byte[] Set) Build(Packfile armageddonMisc, Packfile guerrillaMisc)
    {
        string Text(Packfile archive, string name) =>
            Encoding.Latin1.GetString(archive.Read(archive.Find(name) ?? throw new InvalidDataException($"no {name} in {archive.Name}")));
        string orbitals = Text(guerrillaMisc, "terr01.gtodx")
            .Replace("<effect_name>env_sun_corona_a</effect_name>", "<effect_name>env_sun_corona</effect_name>")
            .Replace(".cvbm_pc</map_name>", "</map_name>");
        string day = Text(armageddonMisc, "world_default.dtodx");
        string oasis = Text(guerrillaMisc, "terr01_oasis.dtodx");
        foreach (string curve in Sky.Concat(WithSky).Concat(AsTheyAre).Append("tod_light_color")) day = Replace(day, curve, Block(oasis, curve));
        float ldrMax = Constant(day, "ldr_max") ?? throw new InvalidDataException("world_default's ldr_max changes over the day");
        if (Constant(day, "ldr_min") != 0) throw new InvalidDataException("world_default's ldr_min isn't 0");
        foreach (string curve in new[] { "ambient_color", "back_ambient_color" }) day = Replace(day, curve, Linear(oasis, curve, ldrMax));
        return (Encoding.Latin1.GetBytes(orbitals), Encoding.Latin1.GetBytes(day));
    }

    // Curves in a set: <name><key><time>t</time><value>...</value></key>...</name>. A colour's value is
    // red, green, blue and a brightness that multiplies them.
    static Regex Pattern(string name) => new($@"<{name}>.*?</{name}>", RegexOptions.Singleline);

    static string Block(string file, string name) =>
        Pattern(name).Match(file) is { Success: true } m ? m.Value : throw new InvalidDataException($"no {name} curve in the time of day set");

    static string Replace(string file, string name, string block)
    {
        Block(file, name);
        return Pattern(name).Replace(file, _ => block, 1);
    }

    static float[] Values(string text) => text.Split(' ', StringSplitOptions.RemoveEmptyEntries).Select(v => float.Parse(v, CultureInfo.InvariantCulture)).ToArray();

    /// <summary>A number curve's value, if it is the same all day (else null).</summary>
    static float? Constant(string file, string name) =>
        Regex.Matches(Block(file, name), @"<value>([^<]*)</value>").Select(m => Values(m.Groups[1].Value)[0]).Distinct().ToList() is { Count: 1 } one ? one[0] : null;

    /// <summary>
    /// A colour curve's block with each key's brightness folded into its colour, and the brightness
    /// set to the given one. For the ambient light: Armageddon (without Tod_hdr_enabled) turns an
    /// ambient brightness b into (b − ldr_min) / ldr_max held to 0–1, so a colour is never brighter
    /// than its own red, green and blue; Guerrilla used the brightness as it is.
    /// </summary>
    static string Linear(string file, string name, float brightness) =>
        Regex.Replace(Block(file, name), @"<value>([^<]*)</value>", m =>
        {
            float[] v = Values(m.Groups[1].Value);
            if (v.Length != 4) throw new InvalidDataException($"{name} isn't a colour curve");
            for (int i = 0; i < 3; i++) v[i] *= v[3];
            v[3] = brightness;
            return "<value>" + string.Join(" ", v.Select(x => x.ToString("F6", CultureInfo.InvariantCulture))) + "</value>";
        });
}
