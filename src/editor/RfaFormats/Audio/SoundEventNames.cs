using System.Text;
using System.Text.RegularExpressions;

namespace RfaFormats.Audio;

/// <summary>
/// The names of our sound banks' events. A bank keeps only their ids (Wwise's hash of the name: <see cref="RfaAudio.Id"/>),
/// so names are found by hashing the words the mods and their tables use and keeping those that are an event's id:
/// the words of the tables and text files in mods\ and overrides\ (and the mods' DLLs), each as it is, with "RFG_" before
/// it, as a vehicle's engine ("RFG_" + vehicle + "_ENGINE"), and each of those with "_STOP" after it (GuerrillaSoundBanks'
/// own patterns), plus the voices' lines ("RFGV_" + persona + "_" + line, from mods\guerrilla_voices\voices.txt). On the
/// 40 banks of 2026-11-21 this names all 6,548 events but a handful (shown by their ids).
/// </summary>
public static class SoundEventNames
{
    static readonly Regex Word = new(@"[A-Za-z0-9_\-]{3,}", RegexOptions.Compiled | RegexOptions.CultureInvariant);
    static readonly string[] Kinds = [".xtbl", ".txt", ".lua", ".ini", ".dll", ".vad_pc"];

    /// <summary>
    /// The names of the given event ids that can be found from the mod folder (bin\ or a staging copy): id → name.
    /// Reads about 20 MB; meant to run off the UI thread.
    /// </summary>
    public static Dictionary<uint, string> Find(string modFolder, IEnumerable<uint> eventIds)
    {
        var wanted = new HashSet<uint>(eventIds);
        var names = new Dictionary<uint, string>();
        void Try(string name)
        {
            uint id = RfaAudio.Id(name);
            if (wanted.Contains(id) && !names.ContainsKey(id)) names[id] = name;
        }
        string voices = Path.Combine(modFolder, "mods", "guerrilla_voices", "voices.txt");
        if (File.Exists(voices))
            foreach (string line in File.ReadLines(voices))
            {
                var w = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (w.Length >= 3 && w[0] == "has") Try($"RFGV_{w[1]}_{w[2]}".ToUpperInvariant());
            }
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string folder in new[] { Path.Combine(modFolder, "mods"), Path.Combine(modFolder, "overrides") })
        {
            if (!Directory.Exists(folder)) continue;
            foreach (string file in Directory.EnumerateFiles(folder, "*", SearchOption.AllDirectories))
            {
                if (!Kinds.Contains(Path.GetExtension(file), StringComparer.OrdinalIgnoreCase)) continue;
                string text;
                try { text = Encoding.Latin1.GetString(File.ReadAllBytes(file)); }
                catch (IOException) { continue; }
                foreach (Match m in Word.Matches(text))
                {
                    if (!seen.Add(m.Value)) continue;
                    foreach (string name in new[] { m.Value, "RFG_" + m.Value, "RFG_" + m.Value + "_ENGINE" })
                    {
                        Try(name);
                        Try(name + "_STOP");
                    }
                }
            }
        }
        // A stop event is its event's name and _STOP, whichever way the event was found.
        foreach (string name in names.Values.ToList()) Try(name + "_STOP");
        return names;
    }

    /// <summary>The names of the parameters and switches our banks use (they're never in the banks themselves).</summary>
    public static readonly IReadOnlyDictionary<uint, string> Known = new[]
    {
        "game_distance", "RPM", "ENGINE_LOAD", "SPEED", "DAMAGE", "RFG_PLAYER_IN_VEHICLE",
        "WEAPON_SOUND_TYPE", "primary_fire", "no_ammo", "overheat", "turret_turn",
        "RFG_ENGINE_LISTENER", "player", "other",
    }.ToDictionary(RfaAudio.Id, n => n);

    /// <summary>A parameter's, switch's or event's name if known, else its id in hex.</summary>
    public static string NameOf(uint id, IReadOnlyDictionary<uint, string>? names = null) =>
        names != null && names.TryGetValue(id, out var n) ? n : Known.TryGetValue(id, out var k) ? k : id.ToString("X8");
}
