using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;
using RfaFormats.Tables;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's people's voices for the guerrilla_voices mod: the personas its free roam uses, each a bank
/// of Armageddon's kind, and the mod's table of who speaks which lines by which rules.
///
/// - **Personas** (rfg_voice_personas.xtbl; character.xtbl gives each character a list, and an NPC takes one): those
///   of Guerrilla's characters (multiplayer's left out) named as free roam's are, <see cref="FreeRoamPersona"/>:
///   the colonists of each district (GMM/GMF Parker, GDM/GDF Dust, GOM/GOF Oasis, GEM/GEF Eos), the EDF (EGM, ETM,
///   EEM), the Marauders (MSM) and Mason (PCM).
/// - **Lines** (rfg_voice_lines.xtbl): those of free roam's categories, <see cref="Categories"/>: Voice Foley (pain,
///   death, panic, ...), Combat, Ambient, Extra Special (checkpoints, restricted areas, ...) and Vehicle. Activities'
///   and missions' lines come with them.
/// - **A line** of a persona: its variations (<see cref="GuerrillaVoices.Variations"/>) as sounds in a sequence
///   container that plays them in turn (Guerrilla's get_next_valid_variation), 3D at the speaker with the line's
///   distances (its fade on the game's distance, as all our 3D sounds) on Armageddon's bus for spoken lines; its event
///   RFGV_ + persona + _ + line. One variation is the sound alone.
/// - **The bank:** one for every persona (rfgv_voices), Wwise ADPCM, every sound streamed from the media file, loaded
///   at boot: Armageddon's bank list holds 300 banks at most (<see cref="GuerrillaSoundBanks.GameBankLimit"/>), and
///   its own and our others' take 299. Its events (5.4 MB) need Wwise's default pool at 20 MB (guerrilla_world;
///   the user's OK, 2026-11-15).
/// - **The table** (mods\guerrilla_voices\voices.txt), lines of words:
///   <c>persona NAME TYPE BANK BANK_ID</c> (TYPE: Guerilla, EDF or Marauder, Guerrilla's spelling);
///   <c>has PERSONA LINE VARIATIONS</c>;
///   <c>rule LINE TYPE LOCAL_COOLDOWN_MS GLOBAL_COOLDOWN_MS CHANCE MIN_DELAY_MS MAX_DELAY_MS PRIORITY</c>;
///   <c>character NAME PERSONA...</c> (its free-roam personas).
/// </summary>
public static class GuerrillaVoiceBanks
{
    /// <summary>The sound part's name (its list in mods\guerrilla_world\: voices_sound_banks.txt), its banks' prefix, the mod and its table.</summary>
    public const string Part = "voices", BankPrefix = "rfgv_", ModName = "guerrilla_voices", TableFile = "voices.txt";

    /// <summary>Free roam's line categories (rfg_voice_lines.xtbl's _Editor Category, after "Entries:").</summary>
    public static readonly string[] Categories = ["Voice Foley", "Combat", "Ambient", "Extra Special", "Vehicle"];

    /// <summary>Free roam's personas: the districts' colonists, the EDF's, the Marauders' and Mason's.</summary>
    public static readonly Regex FreeRoamPersona = new(@"^(G[MDOE][MF]|E[GTE]M|MSM|PCM)\d\d$", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

    /// <summary>The mod's folder, beside the world's (mods\guerrilla_world\).</summary>
    public static string Folder(string worldModFolder) => Path.Combine(Path.GetDirectoryName(Path.GetFullPath(worldModFolder))!, ModName);

    /// <summary>What <see cref="Write"/> wrote, deleted: the table and the banks (their files, rows and list).</summary>
    public static IEnumerable<string> Remove(string game, string overrides, string worldModFolder)
    {
        string table = Path.Combine(Folder(worldModFolder), TableFile);
        if (File.Exists(table))
        {
            File.Delete(table);
            yield return $"Deleted {table}";
        }
        foreach (string line in GuerrillaSoundBanks.Remove(game, overrides, worldModFolder, Part)) yield return line;
    }

    /// <summary>Builds the personas' banks and the mod's table (see the class). Returns what to log.</summary>
    /// <param name="guerrillaTables">Guerrilla's misc.vpp_pc (its tables).</param>
    public static IEnumerable<string> Write(Packfile guerrillaTables, string guerrillaCache, string game, string overrides, string worldModFolder)
    {
        XtblFile? Table(string name) => guerrillaTables.Find(name) is { } index ? XtblFile.FromBytes(guerrillaTables.Read(index)) : null;
        var personaTable = Table("rfg_voice_personas.xtbl");
        var lineTable = Table("rfg_voice_lines.xtbl");
        var characterTable = Table("character.xtbl");
        if (personaTable == null || lineTable == null || characterTable == null)
        {
            yield return "Guerrilla's misc.vpp_pc lacks rfg_voice_personas.xtbl, rfg_voice_lines.xtbl or character.xtbl: no voices built";
            yield break;
        }

        // The personas' types.
        var typeOf = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var p in personaTable.Entries)
            if (p.ValueOf("Name")?.Trim() is { Length: > 0 } name) typeOf[name] = p.ValueOf("Persona_Type")?.Trim() ?? "Guerilla";

        // Free roam's lines and their rules per persona type.
        var lines = new List<string>();
        var rules = new List<string>();
        foreach (var line in lineTable.Entries)
        {
            string category = line.Element("_Editor")?.ValueOf("Category")?.Trim() ?? "";
            if (category.StartsWith("Entries:", StringComparison.OrdinalIgnoreCase)) category = category["Entries:".Length..];
            if (!Categories.Contains(category, StringComparer.OrdinalIgnoreCase) || line.ValueOf("Name")?.Trim() is not { Length: > 0 } name) continue;
            lines.Add(name);
            foreach (var type in line.Element("Types")?.ElementsNamed("Entry") ?? [])
            {
                static int Number(string? text) => int.TryParse(new string((text ?? "").Trim().TakeWhile(c => char.IsDigit(c) || c == '-').ToArray()),
                                                               NumberStyles.Integer, CultureInfo.InvariantCulture, out int n) ? n : 0;
                rules.Add(string.Create(CultureInfo.InvariantCulture,
                    $"rule {name} {type.ValueOf("Persona_Type")?.Trim() ?? "Guerilla"} {Number(type.ValueOf("Local_Cooldown"))} {Number(type.ValueOf("Global_Cooldown"))} " +
                    $"{Number(type.ValueOf("Play_Chance"))} {Number(type.ValueOf("Default_Min_Delay"))} {Number(type.ValueOf("Default_Max_Delay"))} " +
                    $"{Number(type.ValueOf("Default_Priority"))}"));
            }
        }

        // The characters' free-roam personas, and so the personas to build.
        var characters = new List<string>();
        var personas = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var c in characterTable.Entries)
        {
            string name = c.ValueOf("Name")?.Trim() ?? "";
            if (name.Length == 0 || name.StartsWith("MP_", StringComparison.OrdinalIgnoreCase)) continue;
            var own = (c.Element("Personas")?.ElementsNamed("Persona") ?? []).Select(p => p.Value.Trim())
                .Where(p => FreeRoamPersona.IsMatch(p) && typeOf.ContainsKey(p)).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
            if (own.Count == 0) continue;
            characters.Add($"character {name} {string.Join(' ', own)}");
            personas.UnionWith(own);
        }

        // Room in Armageddon's bank list (GuerrillaSoundBanks.GameBankLimit): every other row stays, ours come on top.
        int listed = GuerrillaSoundBanks.BankRows(game, worldModFolder, BankPrefix);
        if (listed + 1 > GuerrillaSoundBanks.GameBankLimit)
        {
            foreach (string line in Remove(game, overrides, worldModFolder)) yield return line;
            yield return $"no voices built: their bank would make {listed + 1} of the {GuerrillaSoundBanks.GameBankLimit} sound banks Armageddon can " +
                         $"list ({listed} listed already), and past that the game loads no sound at all";
            yield break;
        }
        var banks = new GuerrillaSoundBanks(guerrillaCache, guerrillaTables, Part, BankPrefix) { Adpcm = true };
        var bank = banks.Bank("voices");
        var mixers = new Dictionary<uint, (uint Id, List<uint> Children)>();  // by attenuation
        var table = new List<string>();
        var problems = new List<string>();
        int lineCount = 0, waves = 0;
        double seconds = 0;
        foreach (string persona in personas)
        {
            GuerrillaVoices? voices;
            try { voices = GuerrillaVoices.Open(guerrillaCache, persona); }
            catch (InvalidDataException e) { problems.Add($"{persona}: {e.Message}"); continue; }
            if (voices == null) { problems.Add($"{persona}: not in Guerrilla's personas.vpp_pc / voices_r.vpp_pc"); continue; }
            var has = new List<string>();
            foreach (string line in lines)
            {
                IReadOnlyList<GuerrillaVoices.Line> variations;
                var sounds = new List<(uint Medium, float Volume)>();
                try
                {
                    variations = voices.Variations(line);
                    foreach (var v in variations)
                    {
                        var wave = voices.ReadWave(v);
                        seconds += (double)wave.Samples.Length / wave.Channels / wave.Rate;
                        sounds.Add((bank.Medium(wave), GuerrillaSoundBanks.Db(v.Volume) + v.SoundDb));
                    }
                }
                catch (Exception e) when (e is InvalidDataException or NotSupportedException) { problems.Add($"{persona} {line}: {e.Message}"); continue; }
                if (sounds.Count == 0) continue;
                string key = $"{persona}:{line}";
                // The line hangs from its distances' mixer, which places it in 3D and fades it (one curve on the
                // game's distance for all the lines of those distances, not one each: see WwiseBankWriter.Mixer).
                uint attenuation = banks.Attenuation(bank, variations[0].DistanceIndex);
                if (!mixers.TryGetValue(attenuation, out var mixer))
                    mixers[attenuation] = mixer = (bank.ObjectId("mixer:" + attenuation.ToString("X8")), []);
                var own = new WwiseBankWriter.Settings
                {
                    Priority = GuerrillaSoundBanks.Priority(variations[0].Rank, false), OwnPriority = true, EndsWhenQuiet = true,
                };
                uint played;
                if (sounds.Count == 1)
                {
                    own.Volume = sounds[0].Volume;
                    played = bank.Sound(key, sounds[0].Medium, false, own, mixer.Id);
                }
                else
                {
                    uint sequence = bank.ObjectId("sequence:" + key);
                    var children = sounds.Select((m, i) => bank.Sound($"{key}~{i}", m.Medium, false, new WwiseBankWriter.Settings { Volume = m.Volume }, sequence)).ToList();
                    played = bank.Sequence(key, children, own, mixer.Id);
                }
                mixer.Children.Add(played);
                bank.Event(EventName(persona, line), bank.Play(key, played));
                has.Add($"has {persona} {line} {sounds.Count}");
                lineCount++;
                waves += sounds.Count;
            }
            table.Add($"persona {persona} {typeOf[persona]} {bank.Name} {WwiseBankWriter.Id(bank.Name)}");
            table.AddRange(has);
        }
        // The mixers, after their children: 3D on Armageddon's bus for spoken lines, each with its distances' fade.
        foreach (var (attenuation, mixer) in mixers)
            bank.Mixer(attenuation.ToString("X8"), mixer.Children, new WwiseBankWriter.Settings(),
                       new WwiseBankWriter.Output(GuerrillaKiosks.DialogueBus, attenuation, true));
        table.AddRange(rules);
        table.AddRange(characters);
        string folder = Folder(worldModFolder);
        Directory.CreateDirectory(folder);
        File.WriteAllLines(Path.Combine(folder, TableFile), table, new UTF8Encoding(false));
        foreach (string line in banks.Write(game, overrides, worldModFolder)) yield return "sounds: " + line;

        // What it costs: the media on disk, the banks Wwise holds while loaded.
        long Size(string file) => OverrideLayout.Find(overrides, file) is { } path ? new FileInfo(path).Length : 0;
        var built = banks.Banks.ToList();
        long media = built.Sum(b => Size(b.Name + "_media.bnk_pc")), events = built.Sum(b => Size(b.Name + ".bnk_pc"));
        var check = built.Aggregate((Worst: double.MaxValue, Sum: 0d, Count: 0), (a, b) =>
            (Math.Min(a.Worst, b.AdpcmCheck.Worst), a.Sum + b.AdpcmCheck.Sum, a.Count + b.AdpcmCheck.Count));
        string average = (check.Sum / Math.Max(1, check.Count)).ToString("0.#", CultureInfo.InvariantCulture);
        string worst = (check.Count > 0 ? check.Worst : 0).ToString("0.#", CultureInfo.InvariantCulture);
        yield return
            $"{personas.Count} personas (named by {characters.Count} characters), {lineCount} of their lines from {lines.Count} free-roam " +
            $"lines, {waves} variations, {(int)Math.Round(seconds / 60)} min; media {media / 1048576} MB (Wwise ADPCM, streamed), events " +
            $"{events / 1024} KB in {bank.Name}, loaded at boot; decoded again the " +
            $"waves are {average} dB from Guerrilla's on average, the worst {worst} dB; " +
            $"mods\\{ModName}\\{TableFile}: {table.Count} lines ({rules.Count} rules)" +
            (problems.Count > 0 ? $"; left out: {string.Join("; ", problems.Take(12))}{(problems.Count > 12 ? $" and {problems.Count - 12} more" : "")}" : "") +
            (banks.Log.Count > 0 ? $" ({string.Join("; ", banks.Log.Distinct())})" : "");
    }

    /// <summary>A persona's line's event: RFGV_ + persona + _ + line, in capitals (the mod hashes the same name).</summary>
    public static string EventName(string persona, string line) => $"RFGV_{persona}_{line}".ToUpperInvariant();
}
