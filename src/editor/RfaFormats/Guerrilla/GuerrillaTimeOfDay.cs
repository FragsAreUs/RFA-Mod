using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's time of day for a world ported from it: its sun and moons (the world's
/// .gtodx) and a set of curves for each of its districts (_Default.dtodx and _&lt;district&gt;.dtodx),
/// all XML, read by Armageddon by the world's and the districts' names.
///
/// The orbitals are Guerrilla's terr01.gtodx with Armageddon's names: its corona effect
/// (env_sun_corona; Guerrilla's effects don't load in it) and texture names without the extension (as
/// its world.gtodx; vfx_sun_01, deimos_d and phobos_d are in its misc.vpp_pc bitmaps).
///
/// The sets: Guerrilla gives each district its own (terr01_oasis, _eos, _badlands, ...; terr01_default
/// where no district is) and blends those of the zone you are in and its neighbours by where you stand
/// in it; Armageddon does the same with the districts of its territory (see <see cref="GuerrillaTerritory"/>),
/// loading &lt;world&gt;_&lt;district&gt;.dtodx for each. The games read most curves on other scales (Guerrilla's
/// fog density 15 where Armageddon's are 0.0001, its sky colours about a twentieth of Armageddon's):
/// a Guerrilla set as it is gave a black world. So each set is Armageddon's own (world_default:
/// exposure), with the district's sky and clouds as they are (the guerrilla_world mod draws them,
/// and sets their brightness itself), its sun's light (which the mod draws; world_default's is black
/// at noon), its ambient light from above and below with each key's brightness folded into its colour
/// (see <see cref="Linear"/>), which the mod's light pass lights the ground with, the colour of its
/// colour correction, which the mod applies and which turns Guerrilla's coloured light into its
/// colours (see <see cref="GuerrillaTint"/>), and its fog as it is: colour, density, fall-off with
/// height, ground and offset (see <see cref="Fog"/>), and its tone mapping (see <see cref="ToneMapping"/>).
/// Armageddon keeps those fog curves but never draws with them (its fog is the environment boxes', by
/// distance); the mod draws Guerrilla's height fog and its tone mapping from them.
/// </summary>
public static class GuerrillaTimeOfDay
{
    static readonly string[] Sky = ["east0", "east1", "east2", "east3", "west0", "west1", "west2", "west3", "zenith"];
    static readonly string[] WithSky = ["clouds_horizon_front_light_color", "clouds_horizon_rear_light_color", "horizon_mountain_color",
                                        "horizon_mountain_color_back", "horizon_mountain_fog_color", "clouds_front_light_color", "clouds_rear_light_color"];
    static readonly string[] AsTheyAre = ["cloud_layer01_speed", "cloud_layer23_speed", "cloud_backlight_power", "cloud_backlight_strength",
                                          "overhead_layer0_strength", "overhead_layer1_strength", "overhead_layer2_strength", "overhead_layer3_strength",
                                          "horizon_layer0_strength", "horizon_layer1_strength", "horizon_layer2_strength", "horizon_layer3_strength"];

    /// <summary>
    /// Guerrilla's fog numbers, and Guerrilla's own value for one a set leaves empty or out (its
    /// tod_district_parameters::set_defaults): the density, its fall-off with height (atmosphere scale),
    /// the fog's ground below the camera, an offset into the distance, and the range that ground is
    /// held to. Its districts all have density 15, fall-off 0.057 and ground −131, and leave the range
    /// empty (−1000 to 3).
    /// </summary>
    static readonly (string Name, float Default)[] Fog =
        [("fog_density_new", 0.007f), ("fog_atmosphere_scale", 0.001f), ("fog_ground", 0), ("fog_density_offset", 0), ("fog_camera_min", -1000), ("fog_camera_max", 3)];

    /// <summary>
    /// Guerrilla's tone mapping, which the guerrilla_world mod draws the picture with: the exposure's
    /// range and offset (every district leaves them empty: 3 and 10), the eye's adaption base and
    /// amount, how fast it adapts (iris rate), the range each measured sample is held to, and the
    /// bloom; Guerrilla's own value where a set leaves one empty or out.
    /// </summary>
    static readonly (string Name, float Default)[] ToneMapping =
        [("tonemap_lum_range", 3), ("tonemap_lum_offset", 10), ("eye_adaption_base", 0.2f), ("eye_adaption_amount", 0.3f), ("iris_rate", 3),
         ("luminance_min", 0.001f), ("luminance_max", 4), ("bloom_amount", 1)];

    /// <summary>
    /// The orbitals file and the sets of curves, from Armageddon's and Guerrilla's misc.vpp_pc: one
    /// named Default (Guerrilla's terr01_default) and one for each of the districts, by their names in
    /// the territory, from their sets in Guerrilla (terr01_ and Guerrilla's name in lower case, spaces as _).
    /// </summary>
    public static (byte[] Orbitals, IReadOnlyList<(string Name, byte[] Set)> Sets) Build(Packfile armageddonMisc, Packfile guerrillaMisc,
                                                                                       IEnumerable<(string Name, string Guerrilla)> districts)
    {
        string Text(Packfile archive, string name) =>
            Encoding.Latin1.GetString(archive.Read(archive.Find(name) ?? throw new InvalidDataException($"no {name} in {archive.Name}")));
        string orbitals = Text(guerrillaMisc, "terr01.gtodx")
            .Replace("<effect_name>env_sun_corona_a</effect_name>", "<effect_name>env_sun_corona</effect_name>")
            .Replace(".cvbm_pc</map_name>", "</map_name>");
        string armageddon = Text(armageddonMisc, "world_default.dtodx");
        var sets = new List<(string, byte[])>();
        foreach (var (name, theirs) in districts.Prepend(("Default", "Default")))
        {
            string guerrilla = Text(guerrillaMisc, $"terr01_{theirs.ToLowerInvariant().Replace(' ', '_')}.dtodx");
            sets.Add((name, Encoding.Latin1.GetBytes(Set(armageddon, guerrilla))));
        }
        return (Encoding.Latin1.GetBytes(orbitals), sets);
    }

    /// <summary>One district's set: Armageddon's world_default with the district's curves as above.</summary>
    static string Set(string armageddon, string guerrilla)
    {
        string day = armageddon;
        foreach (string curve in Sky.Concat(WithSky).Concat(AsTheyAre).Append("tod_light_color")) day = Replace(day, curve, Block(guerrilla, curve));
        float ldrMax = Constant(day, "ldr_max") ?? throw new InvalidDataException("world_default's ldr_max changes over the day");
        if (Constant(day, "ldr_min") != 0) throw new InvalidDataException("world_default's ldr_min isn't 0");
        foreach (string curve in new[] { "ambient_color", "back_ambient_color" }) day = Replace(day, curve, Linear(guerrilla, curve, ldrMax));
        day = Replace(day, "fog_color", Block(guerrilla, "fog_color"));
        foreach (var (curve, value) in Fog.Concat(ToneMapping)) day = Replace(day, curve, NumberOr(guerrilla, curve, value));
        day = Replace(day, "color_correct_tint", GuerrillaTint(day, guerrilla));
        return day;
    }

    /// <summary>A number curve as a set has it, or, if the set leaves it empty or out, one key holding <paramref name="value"/> all day.</summary>
    static string NumberOr(string file, string name, float value) =>
        Pattern(name).Match(file) is { Success: true } m && m.Value.Contains("<key>") ? m.Value
            : $"<{name}>\n\t\t<key>\n\t\t\t<time>24</time>\n\t\t\t<value>{value.ToString("F6", CultureInfo.InvariantCulture)}</value>\n\t\t</key>\n\t</{name}>";

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

    /// <summary>
    /// The colour correction, color_correct_tint: Guerrilla's red, green and blue with Armageddon's
    /// fourth number. Guerrilla multiplies its whole picture by the tint, divided by its brightest
    /// channel, and ignores the fourth number; its light is pink (the Oasis sun at noon 2.4, 1.0, 1.8)
    /// and its tint cyan (0.35, 0.71, 1), so only the two together give its colours. Armageddon
    /// doesn't colour its picture by this tint (its own sets all carry the same grey), so the
    /// guerrilla_world mod reads it from here and multiplies what it draws by it. Armageddon does
    /// divide the tint by its brightest channel too, fourth number included, so each half hour gets
    /// the fourth number that, divided so, is world_default's divided so: whatever Armageddon reads
    /// from this curve besides the colour stays as it was. Every one of the day's 48 half hours is
    /// written, each as the games work it out between keys.
    /// </summary>
    static string GuerrillaTint(string armageddon, string guerrilla)
    {
        const string name = "color_correct_tint";
        var ours = Keys(Block(armageddon, name));
        var theirs = Keys(Block(guerrilla, name));
        var text = new StringBuilder($"<{name}>\n");
        for (int slot = 0; slot < 48; slot++)
        {
            // A set without the curve (End Game's) has Guerrilla's default, white: no correction.
            float[] colour = theirs.Count > 0 ? At(theirs, slot) : [1, 1, 1, 1], own = At(ours, slot);
            float brightest = Math.Max(colour[0], Math.Max(colour[1], colour[2]));
            float ownBrightest = Math.Max(own[0], Math.Max(own[1], own[2]));
            if (brightest <= 0 || ownBrightest <= 0) throw new InvalidDataException($"a black {name} at {slot}");
            colour[3] = own[3] * brightest / ownBrightest;
            text.Append($"\t\t<key>\n\t\t\t<time>{slot}</time>\n\t\t\t<value>")
                .Append(string.Join(" ", colour.Select(x => x.ToString("F6", CultureInfo.InvariantCulture))))
                .Append("</value>\n\t\t</key>\n");
        }
        return text.Append($"\t</{name}>").ToString();
    }

    // A curve's keys by time: half hours, 0-47.
    static List<(float Time, float[] Value)> Keys(string block) =>
        [.. Regex.Matches(block, @"<time>([^<]*)</time>.*?<value>([^<]*)</value>", RegexOptions.Singleline)
                 .Select(m => (Values(m.Groups[1].Value)[0], Values(m.Groups[2].Value))).OrderBy(k => k.Item1)];

    /// <summary>
    /// A curve's value at a half hour as the games fill it in: straight from the key before to the
    /// key after, round the clock (after the last key toward the first; tod_interpolated_var's
    /// build_unkeyed_values).
    /// </summary>
    static float[] At(List<(float Time, float[] Value)> keys, int slot)
    {
        if (keys.Count == 0) throw new InvalidDataException("a curve without keys");
        int after = keys.FindIndex(k => k.Time >= slot);
        if (after >= 0 && keys[after].Time == slot) return [.. keys[after].Value];
        var (nextTime, next) = after >= 0 ? keys[after] : (keys[0].Time + 48, keys[0].Value);
        var (lastTime, last) = after > 0 ? keys[after - 1] : (keys[^1].Time - (after == 0 ? 48 : 0), keys[^1].Value);
        float t = nextTime == lastTime ? 0 : (slot - lastTime) / (nextTime - lastTime);
        return [.. last.Zip(next, (a, b) => a + (b - a) * t)];
    }
}
