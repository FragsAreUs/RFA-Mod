using System.Text;
using System.Text.RegularExpressions;

namespace RfaFormats.Audio;

/// <summary>
/// The names of the sound banks' events, switches, states and game parameters. A bank keeps only their ids (Wwise's
/// hash of the name: <see cref="RfaAudio.Id"/>), so names are found by hashing words and keeping those that are an id.
/// <para>Ours: the words of the tables and text files in mods\ and overrides\ (and the mods' DLLs), each as it is, with
/// "RFG_" before it, as a vehicle's engine ("RFG_" + vehicle + "_ENGINE"), and each of those with "_STOP" after it
/// (GuerrillaSoundBanks' own patterns), plus the voices' lines ("RFGV_" + persona + "_" + line, from
/// mods\guerrilla_voices\voices.txt). On the 40 banks of 2026-11-21 this names all 6,548 events but a handful.</para>
/// <para>Armageddon's: its own tables (<see cref="GameNames"/>), read from the player's copy of the game. They name every
/// event, bank, dialogue event, switch, state and game parameter of its sound and voice banks (2,019 ids, 2026-11-26),
/// all but its 138 buses, whose names are in none of its files. Those are shown by their ids.</para>
/// </summary>
public static class SoundEventNames
{
    static readonly Regex Word = new(@"[A-Za-z0-9_\-]{3,}", RegexOptions.Compiled | RegexOptions.CultureInvariant);
    static readonly Regex TableWord = new(@"[A-Za-z0-9_\-]{2,96}", RegexOptions.Compiled | RegexOptions.CultureInvariant);
    static readonly string[] Kinds = [".xtbl", ".txt", ".lua", ".ini", ".dll", ".vad_pc"];

    /// <summary>
    /// The names of the given event ids that can be found from the mod folder (bin\ or a staging copy), ours first, then
    /// Armageddon's own (<see cref="GameNames"/>, from <paramref name="gameFolder"/>, or the game folder rfa_mods.ini names):
    /// id → name. Reads about 20 MB; meant to run off the UI thread.
    /// </summary>
    public static Dictionary<uint, string> Find(string modFolder, IEnumerable<uint> eventIds, string? gameFolder = null)
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
        // The rest from Armageddon's own tables.
        var game = GameNames(gameFolder ?? GameFolderOf(modFolder));
        foreach (uint id in wanted)
            if (!names.ContainsKey(id) && game.TryGetValue(id, out var name)) names[id] = name;
        return names;
    }

    /// <summary>
    /// The game's folder for a mod folder, as the launcher finds it: GameDir under [Launcher] in its rfa_mods.ini, or the
    /// mod folder itself when the mod was copied into the game's folder. Null if neither is one.
    /// </summary>
    public static string? GameFolderOf(string modFolder)
    {
        string? gameDir = IniFile.Read(Path.Combine(modFolder, "rfa_mods.ini"), "Launcher", "GameDir")?.Trim().Trim('"');
        return GameFolder.IsGameFolder(gameDir) ? gameDir!.TrimEnd('\\') : GameFolder.IsGameFolder(modFolder) ? modFolder : null;
    }

    static readonly object gameLock = new();
    static Dictionary<uint, string>? gameNames;
    static string? gameNamesFolder;

    /// <summary>
    /// Every word of Armageddon's own tables, hashed: id → word. Read once a run from the game's build\pc\cache\misc.vpp_pc,
    /// which holds all of them (about 41,000 words in 14 MB of tables; under a second). audio_events.xtbl lists every
    /// event of its banks and audio_banks.xtbl the banks, so they come first. A persona of
    /// voice_dynamic_dialog_events.xtbl and audio_personas.xtbl names its dialogue event too, as "voice_" + persona. The
    /// switches, states and game parameters are in the other tables. Empty if the game isn't found.
    /// </summary>
    public static IReadOnlyDictionary<uint, string> GameNames(string? gameFolder)
    {
        lock (gameLock)
        {
            if (gameNames != null && string.Equals(gameNamesFolder, gameFolder, StringComparison.OrdinalIgnoreCase)) return gameNames;
            var names = new Dictionary<uint, string>();
            string? archive = gameFolder == null ? null : Path.Combine(gameFolder, "build", "pc", "cache", "misc.vpp_pc");
            if (archive != null && File.Exists(archive))
            {
                try
                {
                    var pack = Packfile.Open(archive);
                    static int Order(string name) => name.ToLowerInvariant() switch
                    {
                        "audio_events.xtbl" => 0,
                        "audio_banks.xtbl" => 1,
                        "voice_dynamic_dialog_events.xtbl" or "audio_personas.xtbl" => 2,
                        _ => 3,
                    };
                    var tables = pack.Entries.Where(e => e.Extension == "xtbl").OrderBy(e => Order(e.Name)).ToList();
                    byte[][] all = pack.ReadAll(tables);
                    for (int i = 0; i < tables.Count; i++)
                    {
                        bool personas = Order(tables[i].Name) == 2;
                        foreach (Match m in TableWord.Matches(Encoding.Latin1.GetString(all[i])))
                        {
                            names.TryAdd(RfaAudio.Id(m.Value), m.Value);
                            if (personas) names.TryAdd(RfaAudio.Id("voice_" + m.Value), "voice_" + m.Value);
                        }
                    }
                }
                catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException) { names.Clear(); }
            }
            gameNames = names;
            gameNamesFolder = gameFolder;
            return names;
        }
    }

    /// <summary>A name for an id if one is known (ours, then Armageddon's tables once <see cref="GameNames"/> has read them).</summary>
    public static bool TryName(uint id, out string name)
    {
        if (Known.TryGetValue(id, out name!)) return true;
        Dictionary<uint, string>? game;
        lock (gameLock) game = gameNames;
        if (game != null && game.TryGetValue(id, out name!)) return true;
        name = "";
        return false;
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
        names != null && names.TryGetValue(id, out var n) ? n : TryName(id, out var k) ? k : id.ToString("X8");
}
