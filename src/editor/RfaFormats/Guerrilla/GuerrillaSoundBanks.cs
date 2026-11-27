using RfaFormats.Tables;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Guerrilla's sounds as banks of Armageddon's kind, for any part of the builder (the vehicles, the guns):
/// each Guerrilla cue becomes a Wwise event named "RFG_" + the cue's name (<see cref="EventName"/>), which
/// Armageddon's tables can name wherever they name a sound. A part makes one of these, asks for its cues
/// (<see cref="OneShot"/>, or builds its own objects into <see cref="Bank"/>), then <see cref="Write"/>s:
///   - the banks, one for each of Guerrilla's sound banks the cues are in (<see cref="BankPrefix"/> + its
///     name: rfg_veh_civ, ...), new files in overrides\build\pc\cache\ (the bank and its media);
///   - their rows in audio_banks_patch.xtbl (mods\guerrilla_world\, which that mod hands the game; the
///     rows of the other parts and Armageddon's own kept), streaming and loaded at boot;
///   - their loops' stop events, as pairs (loop event, stop event) in an override of the game's
///     audio_info.vad_pc (Armageddon's pairs and the other parts' kept): stopping a looping sound, the game
///     posts its pair's stop event.
/// Each part lists what it wrote in its own file in the mod's folder (<see cref="Remove"/> takes it away).
///
/// A one-shot cue is played as Guerrilla has it: its wave (or one of its waves at random, not the same
/// twice running), its volume (the cue's gain and its XACT sound's and track's dB), at the game object in
/// 3D with Guerrilla's distances for it (foley_settings.xtbl Min_Max_Distances: full volume to the
/// nearer, then fading to nothing at the farther along Guerrilla's logarithmic rolloff: see
/// <see cref="Attenuation"/>), as many at once as the cue allows.
/// </summary>
public sealed class GuerrillaSoundBanks
{
    /// <summary>The bank table the game reads after audio_banks.xtbl; written into the guerrilla_world mod's folder.</summary>
    public const string BankTable = "audio_banks_patch.xtbl";

    /// <summary>The game's loops' stop events (Volition's "VWAI"), overridden in its archive.</summary>
    public const string StopPairFile = "audio_info.vad_pc", StopPairArchive = "sounds.vpp_pc";

    /// <summary>A Guerrilla cue's event name in our banks.</summary>
    public static string EventName(string cue) => "RFG_" + cue;

    public GuerrillaSounds Sounds { get; }

    /// <summary>The banks' names start with this (rfg_ for the vehicles; another part its own, so that two parts never write the same file).</summary>
    public string BankPrefix { get; }

    /// <summary>
    /// One of Guerrilla's attack-loop-release sets (foley_alr.xtbl): a sound that lasts as long as something does (a
    /// held gun, a horn, a turret turning, an effect), in three cues: the attack (once, at the start), the loop, and
    /// the release (once, at the end: a gun's tail); for some NPC guns a far loop and a far release too.
    /// <paramref name="Shots"/> shots are in the loop's wave, one every <paramref name="RateMs"/>.
    /// Guerrilla's own way with one (its audiolib_att_loop_rel_start and _stop, and the thread that ends them):
    ///   - at the start it plays the attack, and the loop <paramref name="RateMs"/> later (at once without an attack),
    ///     from a random one of its shots; the release is made ready, paused;
    ///   - with a far loop, and the listener at least that cue's nearer distance away, the far loop and far release
    ///     are played as well (the near ones always: past their own farther distance they are silent);
    ///   - at the stop the loop fades out over the time between two shots at most, and the release starts at once.
    /// </summary>
    public sealed record AlrSet(string Name, string? Attack, string Loop, string? Release, string? FarLoop, string? FarRelease, int Shots, int RateMs, bool ThreeD);

    readonly string part;
    readonly List<(float Min, float Max)> distances;
    readonly List<AlrSet> alrSets;
    readonly Dictionary<string, string> farVariants;
    readonly Dictionary<string, WwiseBankWriter> banks = new(StringComparer.OrdinalIgnoreCase);
    readonly Dictionary<uint, uint> stopPairs = [];
    readonly List<string> log = [];

    /// <param name="part">Names the part's list file (<c>&lt;part&gt;_sound_banks.txt</c>).</param>
    public GuerrillaSoundBanks(string guerrillaCache, Packfile guerrillaTables, string part, string bankPrefix)
    {
        this.part = part;
        BankPrefix = bankPrefix;
        Sounds = new GuerrillaSounds(guerrillaCache);
        distances = Distances(guerrillaTables);
        alrSets = AlrSets(guerrillaTables);
        farVariants = FarVariants(guerrillaTables);
    }

    /// <summary>A field's text anywhere under an element (null if it isn't there or is empty).</summary>
    static string? Deep(XtblElement e, string name)
    {
        foreach (var c in e.Elements)
        {
            if (c.Name.Equals(name, StringComparison.OrdinalIgnoreCase)) return c.Value?.Trim() is { Length: > 0 } text ? text : null;
            if (c.HasElements && Deep(c, name) is { } found) return found;
        }
        return null;
    }

    /// <summary>Foley_alr.xtbl's sets that have a loop.</summary>
    static List<AlrSet> AlrSets(Packfile guerrillaTables)
    {
        var list = new List<AlrSet>();
        if (guerrillaTables.Find("foley_alr.xtbl") is not { } file) return list;
        foreach (var e in XtblFile.FromBytes(guerrillaTables.Read(file)).Entries)
            if (Deep(e, "Name") is { } name && Deep(e, "Near_Loop") is { } loop)
                list.Add(new AlrSet(name, Deep(e, "Near_Attack"), loop, Deep(e, "Near_Release"), Deep(e, "Far_Loop"), Deep(e, "Far_Release"),
                                    (int)Number(Deep(e, "Num_Shots")), (int)Number(Deep(e, "Repeat_Rate")),
                                    "True".Equals(Deep(e, "IS_3D"), StringComparison.OrdinalIgnoreCase)));
        return list;
    }

    /// <summary>Foley_settings.xtbl's FarVariants: a cue, and the cue Guerrilla plays with it for a listener far off.</summary>
    static Dictionary<string, string> FarVariants(Packfile guerrillaTables)
    {
        var pairs = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var table = XtblFile.FromBytes(guerrillaTables.Read(guerrillaTables.Find("foley_settings.xtbl") ?? throw new InvalidDataException("no foley_settings.xtbl in Guerrilla's misc.vpp_pc")));
        void Walk(XtblElement e)
        {
            foreach (var c in e.Elements)
            {
                if (c.Name.Equals("NearFarVariants", StringComparison.OrdinalIgnoreCase))
                {
                    if (Deep(c, "NearFoley") is { } near && Deep(c, "FarFoley") is { } far) pairs[near] = far;
                }
                else if (c.HasElements) Walk(c);
            }
        }
        foreach (var entry in table.Entries) Walk(entry);
        return pairs;
    }

    /// <summary>The set a cue is the attack or the loop of (a weapon, a horn and a turret name a set by one of these), or null.</summary>
    public AlrSet? SetOf(string cue) =>
        alrSets.FirstOrDefault(s => cue.Equals(s.Attack, StringComparison.OrdinalIgnoreCase) || cue.Equals(s.Loop, StringComparison.OrdinalIgnoreCase));

    /// <summary>The set of this name (an effect names its set so), or null.</summary>
    public AlrSet? SetNamed(string name) => alrSets.FirstOrDefault(s => name.Equals(s.Name, StringComparison.OrdinalIgnoreCase));

    /// <summary>What was built with a far version or as a set, for the log.</summary>
    readonly SortedSet<string> builtSets = new(StringComparer.OrdinalIgnoreCase), builtFar = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Lines for the builder's log (what couldn't be had).</summary>
    public IReadOnlyList<string> Log => log;

    /// <summary>Foley_settings.xtbl's Min_Max_Distances, in order (a cue names one by its number).</summary>
    static List<(float, float)> Distances(Packfile guerrillaTables)
    {
        var table = XtblFile.FromBytes(guerrillaTables.Read(guerrillaTables.Find("foley_settings.xtbl") ?? throw new InvalidDataException("no foley_settings.xtbl in Guerrilla's misc.vpp_pc")));
        var list = new List<(float, float)>();
        void Walk(XtblElement e)
        {
            foreach (var c in e.Elements)
            {
                if (c.Name.Equals("Min_Max", StringComparison.OrdinalIgnoreCase))
                    list.Add((Number(c.ValueOf("Min_Distance")), Number(c.ValueOf("Max_Distance"))));
                else if (c.HasElements) Walk(c);
            }
        }
        foreach (var entry in table.Entries) Walk(entry);
        return list;
    }

    public static float Number(string? text) =>
        float.TryParse(text?.Trim(), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out float f) ? f : 0;

    /// <summary>Whether this part is already making the bank for a Guerrilla sound bank (some cue of it asked for).</summary>
    public bool Builds(string guerrillaSoundBank) => banks.ContainsKey(BankPrefix + guerrillaSoundBank.ToLowerInvariant());

    /// <summary>The bank for a Guerrilla sound bank (made the first time).</summary>
    public WwiseBankWriter Bank(string guerrillaSoundBank)
    {
        string name = BankPrefix + guerrillaSoundBank.ToLowerInvariant();
        if (!banks.TryGetValue(name, out var bank)) banks[name] = bank = new WwiseBankWriter(name) { Adpcm = Adpcm };
        return bank;
    }

    /// <summary>This part's banks hold Wwise ADPCM (see <see cref="WwiseBankWriter.Adpcm"/>).</summary>
    public bool Adpcm { get; init; }

    /// <summary>
    /// The codec of the world's sounds: the vehicles', effects' and weapons' parts. Every sound of ours is a stream
    /// the game's reader fetches piece by piece, and as uncompressed sound they starved in play (Wwise's "Source
    /// starvation", the runs of 2026-11-19: the player's engine's layers, other vehicles', destruction hits). Wwise
    /// ADPCM is 3.6 times fewer bytes, the cache of pieces holds 3.6 times more seconds of it, and it is close to
    /// Guerrilla's own waves, which are MS-ADPCM in blocks of 64 samples with their loops on those blocks. The main
    /// session, 2026-11-19: "On ADPCM: yes, go ahead".
    /// </summary>
    public const bool WorldSoundsAdpcm = true;

    /// <summary>
    /// The game loads this part's banks as it starts (audio_banks load_at_boot True, the default). False: they are
    /// listed only, and a mod loads each when it needs it (the people's voices: guerrilla_voices, by the bank's id).
    /// </summary>
    public bool LoadAtBoot { get; init; } = true;

    /// <summary>The banks made so far (for a part's own log: their media's sizes, an ADPCM bank's check).</summary>
    public IEnumerable<WwiseBankWriter> Banks => banks.Values;

    /// <summary>
    /// A clip of music: the cue played once in 2D on <paramref name="bus"/> at the highest priority, as the event
    /// "RFG_" + the cue + "_MUSIC", with a stop event (+ "_STOP": a fade of half a second) for whoever plays it to
    /// post. The events' names, the clip's length, rate and channels and its bank; null if the cue can't be had (logged).
    /// </summary>
    public (string Event, string StopEvent, double Seconds, int Rate, int Channels, string Bank)? MusicClip(string cueName, uint bus)
    {
        var cue = Sounds.FindCue(cueName);
        if (cue == null) { log.Add($"no cue {cueName} in Guerrilla's foley.aud_pc"); return null; }
        string eventName = EventName(cueName) + "_MUSIC", stopName = eventName + "_STOP", key = "music:" + cueName;
        GuerrillaSounds.Wave wave;
        string soundBank;
        try
        {
            soundBank = Sounds.SoundBankName(cue);
            var (waveBank, index) = Sounds.SoundOf(cue).Waves[0];
            wave = Sounds.ReadWave(waveBank, index);
        }
        catch (Exception e) when (e is InvalidDataException or NotSupportedException) { log.Add($"{cueName}: {e.Message}"); return null; }
        var bank = Bank(soundBank);
        if (!bank.Has(WwiseBankWriter.Id(eventName)))
        {
            uint? clip = CueObject(bank, cueName, key, loops: false, new() { Priority = 100 }, output: new(bus, 0, false));
            if (clip == null) return null;
            bank.Event(eventName, bank.Play(key, clip.Value));
            bank.Event(stopName, bank.Stop(key, clip.Value, 500));
        }
        return (eventName, stopName, (double)wave.Samples.Length / wave.Channels / wave.Rate, wave.Rate, wave.Channels, bank.Name);
    }

    /// <summary>
    /// Guerrilla's logarithmic rolloff (its audiolib_rolloff_curve_logarythmic_entries, RM 0x14F9138): the gain at each
    /// tenth of the way from a cue's nearer distance to its farther one (1 − log10(1 + 10t), but 0.02 at 0.9 and 0 at
    /// the end). Guerrilla's audiolib_calc_scaled_value (RM 0x483AC0) gives a sound full volume up to the nearer
    /// distance, nothing past the farther, and between them this curve at t = (distance − nearer) / (farther − nearer),
    /// read as straight lines between its points. Each sound category in foley.aud_pc names its curve (its
    /// rolloff_type, +0x11: 0 or 2 this one, 1 linear, 3 a near/far mix; audiolib_load_category_and_foley_data, RM
    /// 0x49AC54); every one of the 1,902 cues is in a category with this one, and so are the voice personas' lines (the
    /// KIOSK persona's category is 48). The two linear categories hold no cue.
    /// </summary>
    static readonly float[] GuerrillaRolloff = [1.0f, 0.699f, 0.523f, 0.398f, 0.301f, 0.222f, 0.155f, 0.097f, 0.046f, 0.02f, 0.0f];

    /// <summary>The gain <see cref="GuerrillaRolloff"/> gives at <paramref name="t"/> (0 the nearer distance, 1 the farther).</summary>
    public static float RolloffGain(float t)
    {
        if (t <= 0) return 1;
        if (t >= 1) return 0;
        float at = t * 10;
        int k = (int)at;
        return GuerrillaRolloff[k] + (GuerrillaRolloff[k + 1] - GuerrillaRolloff[k]) * (at - k);
    }

    /// <summary>
    /// Where the curve's points go (t from the nearer distance to the farther): Wwise draws an attenuation as straight
    /// lines between its points in dB, Guerrilla's curve is straight lines in gain, so the points are close enough
    /// together that the two agree to a fraction of a dB, closer where the gain nears nothing.
    /// </summary>
    static readonly float[] RolloffPoints =
        [0.05f, 0.1f, 0.15f, 0.2f, 0.25f, 0.3f, 0.35f, 0.4f, 0.45f, 0.5f, 0.55f, 0.6f, 0.65f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f,
         0.925f, 0.95f, 0.97f, 0.98f, 0.99f, 0.995f];

    /// <summary>The curve from <paramref name="near"/> to <paramref name="far"/>, and Wwise's silence at the end.</summary>
    static void AddRolloff(List<(float, float)> points, float near, float far)
    {
        foreach (float t in RolloffPoints) points.Add((near + (far - near) * t, Db(RolloffGain(t))));
        points.Add((far, WwiseBankWriter.Silent));
    }

    /// <summary>
    /// Guerrilla's distances number <paramref name="index"/> as an attenuation in <paramref name="bank"/>: full volume to
    /// the nearer, then Guerrilla's logarithmic rolloff (<see cref="GuerrillaRolloff"/>) to nothing at the farther, so a
    /// sound fades away as you leave its range (−13 dB half way, −27 dB at four fifths, −34 dB at nine tenths).
    /// </summary>
    public uint Attenuation(WwiseBankWriter bank, int index)
    {
        var (near, far) = index >= 0 && index < distances.Count ? distances[index] : (3f, 50f);
        if (far <= near) far = near + 1;
        var points = new List<(float, float)> { (0, 0), (near, 0) };
        AddRolloff(points, near, far);
        return bank.Attenuation($"log:{near}-{far}", points);
    }

    /// <summary>
    /// A far cue's attenuation. Guerrilla plays a far cue only for a listener at least the cue's nearer distance
    /// away, beside the near cue (which is silent past its own farther distance): so it is silent nearer than that,
    /// full there, and falls to nothing at its farther distance as any cue (<see cref="GuerrillaRolloff"/>).
    /// </summary>
    public uint FarAttenuation(WwiseBankWriter bank, int index)
    {
        var (near, far) = index >= 0 && index < distances.Count ? distances[index] : (50f, 250f);
        if (far <= near) far = near + 1;
        var points = new List<(float, float)> { (0, WwiseBankWriter.Silent), (near * 0.95f, WwiseBankWriter.Silent), (near, 0) };
        AddRolloff(points, near, far);
        return bank.Attenuation($"far-log:{near}-{far}", points);
    }

    /// <summary>
    /// Whether a far cue is heard farther off than its near cue. One of Guerrilla's pairs isn't (the rail driver's
    /// NPC shot: its far cue has the near cue's own distances, 1 to 80 m): played beside the near one it would only
    /// double it at every distance, so it is left out (logged).
    /// </summary>
    bool HeardFarther(string nearCue, string farCue)
    {
        float Farthest(string cue) => Sounds.FindCue(cue) is { } c && c.DistanceIndex >= 0 && c.DistanceIndex < distances.Count ? distances[c.DistanceIndex].Max : 0;
        if (Farthest(farCue) > Farthest(nearCue)) return true;
        log.Add($"{farCue}: the far version of {nearCue} is heard no farther than it ({Farthest(farCue)} m); not played");
        return false;
    }

    /// <summary>A far cue as an object in <paramref name="bank"/>, 3D with <see cref="FarAttenuation"/>; null if it can't be had (logged).</summary>
    uint? FarObject(WwiseBankWriter bank, string farCue, string key, bool loops, uint bus, WwiseBankWriter.Settings settings, uint parent = 0)
    {
        var cue = Sounds.FindCue(farCue);
        if (cue == null) { log.Add($"no cue {farCue} (a far version) in Guerrilla's foley.aud_pc"); return null; }
        return CueObject(bank, farCue, key, loops, settings, parent, new(bus, FarAttenuation(bank, cue.DistanceIndex), true));
    }

    /// <summary>A gain as dB (Wwise's silence at most).</summary>
    public static float Db(double gain) => gain <= 0 ? WwiseBankWriter.Silent : (float)Math.Max(WwiseBankWriter.Silent, 20 * Math.Log10(gain));

    /// <summary>
    /// A cue's sound as an object in <paramref name="bank"/> (under <paramref name="key"/>): a sound, or a random
    /// container of its waves' sounds; hanging from <paramref name="parent"/>, or (0) out to
    /// <paramref name="output"/>. <paramref name="settings"/> (curves, how many at once) may be given;
    /// its volume is the cue's added to the one given. Null if the cue or its waves can't be had (logged).
    /// </summary>
    public uint? CueObject(WwiseBankWriter bank, string cueName, string key, bool loops, WwiseBankWriter.Settings? settings = null,
                           uint parent = 0, WwiseBankWriter.Output? output = null, float retriggerMs = 0)
    {
        var cue = Sounds.FindCue(cueName);
        if (cue == null) { log.Add($"no cue {cueName} in Guerrilla's foley.aud_pc"); return null; }
        GuerrillaSounds.Sound sound;
        try { sound = Sounds.SoundOf(cue); }
        catch (Exception e) when (e is InvalidDataException or NotSupportedException) { log.Add($"{cueName}: {e.Message}"); return null; }
        // Left to this: Guerrilla's rank of the cue, as a loop's or a one-shot's priority (see Priority).
        settings ??= new WwiseBankWriter.Settings { Priority = Priority(cue, loops || retriggerMs > 0) };
        // A sound of several tracks (Guerrilla has one: a Marauder's step, and his gear with it): a layer of them,
        // each with its own volume.
        bool layered = sound.Also is { Count: > 0 } && retriggerMs <= 0;
        if (sound.Also is { Count: > 0 } && !layered) log.Add($"{cueName}: a repeated shot of several tracks; only its first is played");
        settings.Volume += Db(cue.Volume) + (layered ? 0 : sound.VolumeDb);
        settings.Pitch += layered ? 0 : sound.PitchCents;
        if (settings.MostAtOnce == 0 && cue.MostAtOnce > 0) settings.MostAtOnce = (ushort)cue.MostAtOnce;
        if (retriggerMs > 0)
        {
            // Played again and again at an interval (WwiseBankWriter.Retriggered): each of its waves a sound played once.
            uint repeating = bank.ObjectId("retriggered:" + key);
            var shots = new List<uint>();
            int shot = 0;
            foreach (var (waveBank, wave) in sound.Picks ? sound.Waves : [sound.Waves[0]])
                shots.Add(bank.Sound($"{key}#{shot++}", bank.Medium(Sounds.ReadWave(waveBank, wave)), false, new WwiseBankWriter.Settings(), repeating));
            return bank.Retriggered(key, shots, settings, retriggerMs, parent, output);
        }
        // One track: its wave's sound, or a random container of its waves' sounds.
        uint Track(GuerrillaSounds.Sound track, string trackKey, WwiseBankWriter.Settings trackSettings, uint under, WwiseBankWriter.Output? to)
        {
            if (track.Waves.Count == 1 || !track.Picks)
            {
                var (waveBank, wave) = track.Waves[0];
                return bank.Sound(trackKey, bank.Medium(Sounds.ReadWave(waveBank, wave), loops), loops, trackSettings, under, to);
            }
            uint container = bank.ObjectId("random:" + trackKey);
            var children = new List<uint>();
            int n = 0;
            foreach (var (waveBank, wave) in track.Waves)
                children.Add(bank.Sound($"{trackKey}#{n++}", bank.Medium(Sounds.ReadWave(waveBank, wave), loops), loops, new WwiseBankWriter.Settings(), container));
            return bank.Random(trackKey, children, trackSettings, under, to);
        }
        if (!layered) return Track(sound, key, settings, parent, output);
        uint layer = bank.ObjectId("layer:" + key);
        var tracks = new List<uint>();
        foreach (var track in new[] { sound }.Concat(sound.Also!))
            tracks.Add(Track(track, $"{key}~{tracks.Count}", new() { Volume = track.VolumeDb, Pitch = track.PitchCents }, layer, null));
        return bank.Layer(key, tracks, settings, parent, output);
    }

    /// <summary>The cue's distances' number (for <see cref="Attenuation"/>), or -1.</summary>
    public int DistanceIndex(string cueName) => Sounds.FindCue(cueName)?.DistanceIndex ?? -1;

    /// <summary>
    /// A cue played once at the game object it's posted on (3D, its distances), on <paramref name="bus"/>, as
    /// the event <see cref="EventName"/>(cue) in the bank of the cue's own sound bank; that name, or null if
    /// the cue can't be had (logged).
    /// </summary>
    public string? OneShot(string cueName, uint bus = WwiseBankWriter.VehicleEffectsBus, string suffix = "", WwiseBankWriter.Settings? settings = null,
                           string? named = null, string? also = null)
    {
        // (<paramref name="also"/>: a second cue the event plays with this one, each by its own distances; the
        // event's name then says both.)
        if (also != null) suffix += "_WITH_" + also;
        string eventName = named ?? EventName(cueName) + suffix, key = cueName + suffix;
        var cue = Sounds.FindCue(cueName);
        if (cue == null) { log.Add($"no cue {cueName} in Guerrilla's foley.aud_pc"); return null; }
        // Played once in the world: Guerrilla's rank as its priority, and out of earshot it ends at once.
        settings ??= new WwiseBankWriter.Settings { Priority = Priority(cue, false) };
        settings.EndsWhenQuiet = true;
        string soundBank;
        try { soundBank = Sounds.SoundBankName(cue); }
        catch (InvalidDataException e) { log.Add(e.Message); return null; }
        var bank = Bank(soundBank);
        if (bank.Has(WwiseBankWriter.Id(eventName))) return eventName;
        uint? sound = CueObject(bank, cueName, key, loops: false, settings, output: new(bus, Attenuation(bank, cue.DistanceIndex), true));
        if (sound == null) return null;
        // With a far version (foley_settings.xtbl's FarVariants: the big explosions, some NPC guns), that plays too:
        // each is silent where the other is heard, and the silent one ends at once.
        var actions = new List<uint> { bank.Play(key, sound.Value) };
        if (farVariants.TryGetValue(cueName, out string? farCue) && HeardFarther(cueName, farCue) &&
            FarObject(bank, farCue, key + ":far", false, bus,
                      new() { Priority = settings.Priority, EndsWhenQuiet = true, RefusesWhenFull = settings.RefusesWhenFull }) is { } far)
        {
            actions.Add(bank.Play(key + ":far", far));
            builtFar.Add($"{cueName} + {farCue}");
        }
        // The second cue: one object of it in the bank, whichever events play it.
        if (also != null)
        {
            if (Sounds.FindCue(also) is not { } alsoCue) log.Add($"no cue {also} (played with {cueName}) in Guerrilla's foley.aud_pc");
            else if (CueObject(bank, also, "also:" + also, false,
                               new() { Priority = Priority(alsoCue, false), EndsWhenQuiet = true, RefusesWhenFull = settings.RefusesWhenFull },
                               output: new(bus, Attenuation(bank, alsoCue.DistanceIndex), true)) is { } second)
                actions.Add(bank.Play(key + ":also", second));
        }
        bank.Event(eventName, [.. actions]);
        return eventName;
    }

    /// <summary>The shortest fade a set's loop ends with (Guerrilla cuts a loop without a shot rate at once, under its release).</summary>
    const int ShortestAlrFadeMs = 30;

    /// <summary>
    /// One of Guerrilla's attack-loop-release sets, 3D at the game object, as the event <paramref name="eventName"/>
    /// and its stop event (the name + "_STOP"), paired so the game posts the stop when it stops the sound, played as
    /// Guerrilla plays one (see <see cref="AlrSet"/>): the event plays the attack and, a shot's time later, the loop
    /// (and the far loop); the stop event fades the loops out over a shot's time and plays the release (and the far
    /// release). The attack and the releases play out whatever follows. Not as Guerrilla: the loop starts at its
    /// first shot, not a random one. <paramref name="settings"/>: the loop's (volume, priority, quiet behaviour, how
    /// many at once); the attack and release take its volume and a one-shot's priority.
    /// The event's name, or null if the loop can't be had (logged).
    /// </summary>
    public string? Alr(AlrSet set, string eventName, uint bus = WwiseBankWriter.VehicleEffectsBus, string suffix = "", WwiseBankWriter.Settings? settings = null)
    {
        string stopName = eventName + "_STOP", key = "alr:" + set.Name + suffix;
        var loopCue = Sounds.FindCue(set.Loop);
        if (loopCue == null) { log.Add($"no cue {set.Loop} (the loop of the set {set.Name}) in Guerrilla's foley.aud_pc"); return null; }
        string soundBank;
        try { soundBank = Sounds.SoundBankName(loopCue); }
        catch (InvalidDataException e) { log.Add(e.Message); return null; }
        var bank = Bank(soundBank);
        if (bank.Has(WwiseBankWriter.Id(eventName))) return eventName;
        float volume = settings?.Volume ?? 0;
        WwiseBankWriter.Settings Held() => new()
        {
            Volume = volume, Priority = settings?.Priority ?? Priority(loopCue, true), VirtualWhenQuiet = settings?.VirtualWhenQuiet ?? false,
            MostAtOnce = settings?.MostAtOnce ?? 0,
        };
        // A part played once: null if the set hasn't it or it can't be had.
        uint? Once(string? cueName, string what)
        {
            if (cueName == null) return null;
            if (Sounds.FindCue(cueName) is not { } cue) { log.Add($"no cue {cueName} (the {what} of the set {set.Name}) in Guerrilla's foley.aud_pc"); return null; }
            return CueObject(bank, cueName, $"{key}:{what}", false, new() { Volume = volume, Priority = Priority(cue, false), EndsWhenQuiet = true },
                             output: new(bus, Attenuation(bank, cue.DistanceIndex), true));
        }
        var play = new List<uint>();
        var stop = new List<uint>();
        uint? attack = Once(set.Attack, "attack");
        if (attack != null) play.Add(bank.Play(key + ":attack", attack.Value));
        int delay = attack != null ? set.RateMs : 0, fade = Math.Max(set.RateMs, ShortestAlrFadeMs);
        uint? loop = CueObject(bank, set.Loop, key + ":loop", true, Held(), output: new(bus, Attenuation(bank, loopCue.DistanceIndex), true));
        if (loop == null) return null;
        play.Add(bank.Play(key + ":loop", loop.Value, delay));
        stop.Add(bank.Stop(key + ":loop", loop.Value, fade));
        if (set.FarLoop != null && FarObject(bank, set.FarLoop, key + ":farloop", true, bus, Held()) is { } farLoop)
        {
            play.Add(bank.Play(key + ":farloop", farLoop, delay));
            stop.Add(bank.Stop(key + ":farloop", farLoop, fade));
        }
        if (Once(set.Release, "release") is { } release) stop.Add(bank.Play(key + ":release", release));
        if (set.FarRelease != null && Sounds.FindCue(set.FarRelease) is { } farReleaseCue &&
            FarObject(bank, set.FarRelease, key + ":farrelease", false, bus, new() { Volume = volume, Priority = Priority(farReleaseCue, false), EndsWhenQuiet = true }) is { } farRelease)
            stop.Add(bank.Play(key + ":farrelease", farRelease));
        bank.Event(eventName, [.. play]);
        bank.Event(stopName, [.. stop]);
        StopPair(eventName, stopName);
        builtSets.Add($"{set.Name} ({(attack != null ? "attack, " : "")}loop{(set.Release != null ? ", release" : "")}{(set.FarLoop != null ? ", far" : "")})");
        return eventName;
    }

    /// <summary>
    /// A cue played until the game stops it (a turret turning, a gun firing for as long as it's held; looping as the cue does): the
    /// event <see cref="EventName"/>(cue), and its stop event (the name + "_STOP": the loop fades out over
    /// <paramref name="fadeMs"/>), paired so the game posts the stop when it stops the sound. 3D at the object,
    /// the cue's distances. Its name, or null if the cue can't be had (logged).
    /// </summary>
    public string? Loop(string cueName, uint bus = WwiseBankWriter.VehicleEffectsBus, int fadeMs = 100, string suffix = "", WwiseBankWriter.Settings? settings = null,
                        string? named = null)
    {
        string eventName = named ?? EventName(cueName) + suffix, stopName = eventName + "_STOP", key = "loop:" + cueName + suffix;
        // A cue that is the attack or the loop of one of Guerrilla's attack-loop-release sets (a horn, a turret's
        // turning) is played as the set: attack, loop, and the release at the stop.
        if (SetOf(cueName) is { } set) return Alr(set, eventName, bus, suffix, settings);
        var cue = Sounds.FindCue(cueName);
        if (cue == null) { log.Add($"no cue {cueName} in Guerrilla's foley.aud_pc"); return null; }
        string soundBank;
        try { soundBank = Sounds.SoundBankName(cue); }
        catch (InvalidDataException e) { log.Add(e.Message); return null; }
        var bank = Bank(soundBank);
        if (bank.Has(WwiseBankWriter.Id(eventName))) return eventName;
        uint? sound = CueObject(bank, cueName, key, loops: cue.LoopCount != 0, settings, output: new(bus, Attenuation(bank, cue.DistanceIndex), true));
        if (sound == null) return null;
        bank.Event(eventName, bank.Play(key, sound.Value));
        bank.Event(stopName, bank.Stop(key, sound.Value, fadeMs));
        StopPair(eventName, stopName);
        return eventName;
    }

    /// <summary>
    /// A sound's priority from Guerrilla's rank of its cue (0, 4, 7, 10; 11 its music's). Armageddon lets 128 sounds
    /// play at once in all (its top bus; silent and virtual ones counted), and for one more it ends the oldest sound of
    /// the lowest priority, without a word. So: every sound that goes on (an engine, a held gun, a fire: ended, it
    /// stays silent) is above every sound that plays once, and within each kind Guerrilla's own rank decides: loops
    /// 70 (rank 0) to 92, one-shots 30 to 63. Armageddon's own sounds have 50.
    /// </summary>
    public static byte Priority(int rank, bool loops) => (byte)(loops ? 70 + 2 * Math.Clamp(rank, 0, 11) : 30 + 3 * Math.Clamp(rank, 0, 11));

    /// <inheritdoc cref="Priority(int, bool)"/>
    public static byte Priority(GuerrillaSounds.Cue cue, bool loops) => Priority(cue.Rank, loops);

    /// <summary>An explosion's sound is at least this (the one-shots' rank 7 is 51): hits and breaking pieces never end its tail.</summary>
    const byte ExplosionPriority = 55;

    /// <summary>How many of one looping effect sound play at once where Guerrilla's cue sets no number (fires side by side).</summary>
    const ushort EffectLoopsAtOnce = 4;

    /// <summary>
    /// The sound of an effect (effects.xtbl's sound: an explosion's bang, a bullet's or hammer's hit, a breaking
    /// piece, a fire's burning), as the event Armageddon's effect posts by name at the effect's place: the event's
    /// name ("RFG_" + the cue + "_FX"), and for a looping one how far off Guerrilla lets it be heard (its distances'
    /// farther one), which the effect's entry gives the game. Null if the cue can't be had (logged).
    /// The game plays a sound without a stop event once, when the effect starts, however far off. One with a stop
    /// event it takes for a loop: started within the distance, stopped past it and when the effect ends. So a cue
    /// that loops is made a loop with its stop event, the others play once.
    /// Those playing once go to Armageddon's effects' bus, an explosion's (<paramref name="explosion"/>) above the
    /// rest, so hits and breaking pieces never end an explosion's tail there; the loops to a bus without a limit, at
    /// the same level, silent ones taking no voice (see <see cref="WwiseBankWriter.EffectsBus"/>).
    /// </summary>
    /// <summary>Before a set's name, where <see cref="EffectSound"/> is asked for a set (an effect's sound_alr) and not a cue.</summary>
    public const string AlrPrefix = "alr:";

    public (string Event, float? HeardTo)? EffectSound(string cueName, bool explosion)
    {
        const string suffix = "_FX";
        // "cue+other": the effect's cue and a second one its event plays with it (a footstep and the walker's own
        // gear, see GuerrillaFootsteps).
        string? also = null;
        int plus = cueName.IndexOf('+');
        if (plus > 0)
        {
            also = cueName[(plus + 1)..];
            cueName = cueName[..plus];
        }
        // An effect's attack-loop-release set (its sound_alr; a thruster, a generator): the loop as an effect's loop,
        // the attack when the game starts it, the release when the game stops it (past its distance, or at the
        // effect's end).
        if (cueName.StartsWith(AlrPrefix, StringComparison.Ordinal))
        {
            if (SetNamed(cueName[AlrPrefix.Length..]) is not { } set) { log.Add($"no set {cueName[AlrPrefix.Length..]} in Guerrilla's foley_alr.xtbl"); return null; }
            if (Sounds.FindCue(set.Loop) is not { } loopCue) { log.Add($"no cue {set.Loop} (the loop of the set {set.Name}) in Guerrilla's foley.aud_pc"); return null; }
            var held = new WwiseBankWriter.Settings
            {
                Volume = WwiseBankWriter.EffectLoopBusDifference, Priority = Priority(loopCue, true), VirtualWhenQuiet = true,
                MostAtOnce = loopCue.MostAtOnce > 0 ? (ushort)0 : EffectLoopsAtOnce,
            };
            if (Alr(set, EventName(set.Name) + suffix, WwiseBankWriter.EffectLoopsBus, suffix, held) is not { } alr) return null;
            var (_, heardTo) = loopCue.DistanceIndex >= 0 && loopCue.DistanceIndex < distances.Count ? distances[loopCue.DistanceIndex] : (3f, 50f);
            return (alr, heardTo);
        }
        var cue = Sounds.FindCue(cueName);
        if (cue == null) { log.Add($"no cue {cueName} in Guerrilla's foley.aud_pc"); return null; }
        if (cue.LoopCount == 0)
        {
            byte priority = explosion ? Math.Max(Priority(cue, false), ExplosionPriority) : Priority(cue, false);
            // Where Guerrilla's cue has a number at once (a breaking piece's 3 to 5), one more doesn't start, as in
            // Guerrilla: a collapse posts over a hundred of these a second, and each one started in an older one's
            // place opened a stream (run 2026-11-11 02:45: Wwise's "Source starvation" for 20 destruction cues).
            return OneShot(cueName, WwiseBankWriter.EffectsBus, suffix, new() { Priority = priority, RefusesWhenFull = true }, also: also) is { } once ? (once, null) : null;
        }
        var settings = new WwiseBankWriter.Settings
        {
            Volume = WwiseBankWriter.EffectLoopBusDifference, Priority = Priority(cue, true), VirtualWhenQuiet = true,
            MostAtOnce = cue.MostAtOnce > 0 ? (ushort)0 : EffectLoopsAtOnce,  // (0: CueObject puts the cue's own number in)
        };
        if (Loop(cueName, WwiseBankWriter.EffectLoopsBus, 250, suffix, settings) is not { } loop) return null;
        var (_, far) = cue.DistanceIndex >= 0 && cue.DistanceIndex < distances.Count ? distances[cue.DistanceIndex] : (3f, 50f);
        return (loop, far);
    }

    /// <summary>
    /// A weapon's firing sound, as the event the game posts for it. The game posts a weapon's one event for four
    /// things, saying which with the switch WEAPON_SOUND_TYPE: a shot, a pull of the trigger with no ammunition, an
    /// overheated gun, and a turret turning fast (stopped again when it slows). So the event plays a switch container,
    /// as Armageddon's own weapons' do: <paramref name="cueName"/> for a shot, <paramref name="dryFireCue"/> (if any)
    /// with no ammunition, nothing for the other two.
    /// <paramref name="held"/>: the weapon's sound goes on for as long as it fires (Guerrilla's flag "looping effects",
    /// Armageddon's "looping fire effects": the game posts the event once when the firing starts and stops it when it
    /// ends), with a stop event the game is told of. A cue that loops by itself (the m16's) is then a loop, faded
    /// out at the stop. A cue that doesn't is one shot with its tail (the machine-gun turrets': 0.15 s of shot in a
    /// second of sound), which Guerrilla plays for every shot: it is played again every <paramref name="refireMs"/>
    /// (the weapon's time between shots), as Armageddon's own automatic guns' shots are, and the stop lets the last
    /// one play out. A weapon that isn't held has its event posted by the game for every shot, and the shots' sounds
    /// overlap.
    /// <paramref name="players"/>: the player's own gun, in 2D with the priority of Armageddon's player's guns (90);
    /// an NPC's is 3D at the gun, with the cue's distances.
    /// The event's name (the cue's, and which of these it is), or null if the cue can't be had (logged).
    /// </summary>
    public string? WeaponSound(string cueName, uint bus, bool held, bool players, string? dryFireCue = null, int refireMs = 0, int fadeMs = 100)
    {
        var cue = Sounds.FindCue(cueName);
        if (cue == null) { log.Add($"no cue {cueName} in Guerrilla's foley.aud_pc"); return null; }
        if (held && SetOf(cueName) is { } set) return WeaponAlr(set, bus, players, dryFireCue);
        bool retriggered = held && cue.LoopCount == 0;
        if (retriggered && refireMs <= 0)
        {
            log.Add($"{cueName}: a held weapon's shot, but the weapon has no time between shots; played once a shot");
            retriggered = held = false;
        }
        string eventName = EventName(cueName) + (players ? "_PLAYER" : "_NPC") + (held ? "_HELD" : "") + (retriggered ? $"_{refireMs}MS" : "");
        string stopName = eventName + "_STOP";
        string soundBank;
        try { soundBank = Sounds.SoundBankName(cue); }
        catch (InvalidDataException e) { log.Add(e.Message); return null; }
        var bank = Bank(soundBank);
        if (bank.Has(WwiseBankWriter.Id(eventName))) return eventName;
        string key = $"weapon:{cueName}:{(players ? "player" : "npc")}:{(held ? "held" : "shot")}{(retriggered ? ":" + refireMs : "")}";
        uint container = bank.ObjectId("switch:" + key);
        uint? shot;
        if (!players && !held && farVariants.TryGetValue(cueName, out string? farCue) && HeardFarther(cueName, farCue))
        {
            // An NPC's single shot with a far version (foley_settings.xtbl's FarVariants): both, each silent where the
            // other is heard, the silent one ended at once (the switch's setting below).
            uint both = bank.ObjectId("layer:" + key + ":fire");
            uint? nearShot = CueObject(bank, cueName, key + ":fire", loops: false, parent: both);
            if (nearShot == null) return null;
            var shots = new List<uint> { nearShot.Value };
            if (FarObject(bank, farCue, key + ":far", false, bus, new(), both) is { } farShot)
            {
                shots.Add(farShot);
                builtFar.Add($"{cueName} + {farCue}");
            }
            shot = bank.Layer(key + ":fire", shots, new(), container);
        }
        else
        {
            if (!players && held && farVariants.ContainsKey(cueName)) log.Add($"{cueName}: a held weapon's shot; its far version ({farVariants[cueName]}) isn't played");
            shot = CueObject(bank, cueName, key + ":fire", loops: held && !retriggered, parent: container, retriggerMs: retriggered ? refireMs : 0);
        }
        if (shot == null) return null;
        uint? dryFire = dryFireCue != null && Sounds.FindCue(dryFireCue) != null ? CueObject(bank, dryFireCue, key + ":dry", loops: false, parent: container) : null;
        // An NPC's held sound too far off to hear goes virtual (it takes no voice till it is heard), its single shot
        // out of earshot ends at once: the weapons' buses let 30 sounds play at once. A held sound is a loop's
        // priority (the player's own the highest), a single shot a one-shot's (the player's the one-shots' highest).
        byte priority = held ? (players ? Math.Max(Priority(cue, true), Priority(10, true)) : Priority(cue, true))
                             : (players ? Priority(10, false) : Priority(cue, false));
        var settings = new WwiseBankWriter.Settings
        {
            // (Guerrilla's number of the cue's sounds at once is on the cue's own object under this one, the sound
            // that plays the wave or the container right over its waves, where it counts waves playing: CueObject
            // puts it there. It was here as well, on the switch over everything a gun sounds.)
            Priority = priority, VirtualWhenQuiet = held && !players, EndsWhenQuiet = !held && !players,
        };
        var output = players ? new WwiseBankWriter.Output(bus, 0, false) : new WwiseBankWriter.Output(bus, Attenuation(bank, cue.DistanceIndex), true);
        bank.Switch(key, WwiseBankWriter.WeaponSoundType,
                    [(WwiseBankWriter.PrimaryFire, shot), (WwiseBankWriter.NoAmmo, dryFire), (WwiseBankWriter.Overheat, null), (WwiseBankWriter.TurretTurn, null)],
                    settings, 0, output);
        bank.Event(eventName, bank.Play(key, container));
        if (held)
        {
            // A loop fades out. A retriggered shot stops repeating and the last one plays out (the Break, as
            // Armageddon's own guns' stop events have); the slow fade after it only makes sure of the end.
            if (retriggered) bank.Event(stopName, bank.Break(key, container), bank.Stop(key, container, 1500));
            else bank.Event(stopName, bank.Stop(key, container, fadeMs));
            StopPair(eventName, stopName);
        }
        return eventName;
    }

    /// <summary>
    /// A held weapon's sound where its cue is the attack or the loop of one of Guerrilla's attack-loop-release sets
    /// (the automatic guns, the machine-gun turrets, the arc welder, the repair tool), played as Guerrilla plays the
    /// set (see <see cref="AlrSet"/>). The game posts the weapon's one event with the switch WEAPON_SOUND_TYPE (see
    /// <see cref="WeaponSound"/>) when the firing starts, and its stop event when it ends, as for any held weapon; so
    /// the set is three switch containers, each with a child for a shot only:
    ///   - the event plays the first (the attack; with no ammunition the dry fire cue) and, a shot's time later,
    ///     the second (the loop; for an NPC's gun with a far loop, both);
    ///   - the stop event stops the second, fading over a shot's time, and plays the third (the release, an NPC's far
    ///     release with it): the gun's tail. After a dry pull or a turret's turning the switch isn't a shot's, and the
    ///     stop plays nothing.
    /// The player's own gun is 2D with the player's guns' priority; an NPC's 3D with each cue's own distances, its
    /// loops virtual when too far to hear, its attack and release ended there.
    /// </summary>
    string? WeaponAlr(AlrSet set, uint bus, bool players, string? dryFireCue)
    {
        var loopCue = Sounds.FindCue(set.Loop);
        if (loopCue == null) { log.Add($"no cue {set.Loop} (the loop of the set {set.Name}) in Guerrilla's foley.aud_pc"); return null; }
        string eventName = EventName(set.Name) + (players ? "_PLAYER" : "_NPC") + "_ALR", stopName = eventName + "_STOP";
        string soundBank;
        try { soundBank = Sounds.SoundBankName(loopCue); }
        catch (InvalidDataException e) { log.Add(e.Message); return null; }
        var bank = Bank(soundBank);
        if (bank.Has(WwiseBankWriter.Id(eventName))) return eventName;
        string key = $"weapon:{set.Name}:{(players ? "player" : "npc")}:alr";
        byte heldPriority = players ? Math.Max(Priority(loopCue, true), Priority(10, true)) : Priority(loopCue, true);
        byte oncePriority = players ? Priority(10, false) : Priority(loopCue, false);
        // Where a part is heard: the player's in 2D; an NPC's at the gun, by its own cue's distances.
        WwiseBankWriter.Output? Place(string cueName) =>
            players ? null : new(bus, Attenuation(bank, Sounds.FindCue(cueName)?.DistanceIndex ?? -1), true);
        var switchOutput = players ? new WwiseBankWriter.Output(bus, 0, false) : new WwiseBankWriter.Output(bus, Attenuation(bank, loopCue.DistanceIndex), true);
        (uint Value, uint? Child)[] Choices(uint? shot, uint? dry = null) =>
            [(WwiseBankWriter.PrimaryFire, shot), (WwiseBankWriter.NoAmmo, dry), (WwiseBankWriter.Overheat, null), (WwiseBankWriter.TurretTurn, null)];
        var play = new List<uint>();
        var stop = new List<uint>();

        // The first: the attack, the dry fire.
        uint first = bank.ObjectId("switch:" + key + ":attack");
        uint? attack = set.Attack != null && Sounds.FindCue(set.Attack) != null ? CueObject(bank, set.Attack, key + ":attack", false, parent: first, output: Place(set.Attack)) : null;
        uint? dryFire = dryFireCue != null && Sounds.FindCue(dryFireCue) != null ? CueObject(bank, dryFireCue, key + ":dry", false, parent: first, output: Place(dryFireCue)) : null;
        if (attack != null || dryFire != null)
        {
            bank.Switch(key + ":attack", WwiseBankWriter.WeaponSoundType, Choices(attack, dryFire), new() { Priority = oncePriority, EndsWhenQuiet = !players }, 0, switchOutput);
            play.Add(bank.Play(key + ":attack", first));
        }

        // The second: the loop, a shot's time after the attack.
        uint second = bank.ObjectId("switch:" + key + ":loop"), loops = bank.ObjectId("layer:" + key + ":loop");
        bool far = !players && set.FarLoop != null && Sounds.FindCue(set.FarLoop) != null;
        uint? loop = CueObject(bank, set.Loop, key + ":loop", true, parent: far ? loops : second);
        if (loop == null) return null;
        if (far)
        {
            var both = new List<uint> { loop.Value };
            if (FarObject(bank, set.FarLoop!, key + ":farloop", true, bus, new(), loops) is { } farLoop) both.Add(farLoop);
            loop = bank.Layer(key + ":loop", both, new(), second);
        }
        bank.Switch(key + ":loop", WwiseBankWriter.WeaponSoundType, Choices(loop), new() { Priority = heldPriority, VirtualWhenQuiet = !players }, 0, switchOutput);
        int delay = attack != null ? set.RateMs : 0, fade = Math.Max(set.RateMs, ShortestAlrFadeMs);
        play.Add(bank.Play(key + ":loop", second, delay));
        stop.Add(bank.Stop(key + ":loop", second, fade));

        // The third: the release, played by the stop event.
        uint third = bank.ObjectId("switch:" + key + ":release"), releases = bank.ObjectId("layer:" + key + ":release");
        bool farTail = !players && set.FarRelease != null && Sounds.FindCue(set.FarRelease) != null;
        uint? release = set.Release != null && Sounds.FindCue(set.Release) != null
            ? CueObject(bank, set.Release, key + ":release", false, parent: farTail ? releases : third, output: Place(set.Release)) : null;
        if (release != null && farTail)
        {
            var both = new List<uint> { release.Value };
            if (FarObject(bank, set.FarRelease!, key + ":farrelease", false, bus, new(), releases) is { } farRelease) both.Add(farRelease);
            release = bank.Layer(key + ":release", both, new(), third);
        }
        if (release != null)
        {
            bank.Switch(key + ":release", WwiseBankWriter.WeaponSoundType, Choices(release), new() { Priority = oncePriority, EndsWhenQuiet = !players }, 0, switchOutput);
            stop.Add(bank.Play(key + ":release", third));
        }
        bank.Event(eventName, [.. play]);
        bank.Event(stopName, [.. stop]);
        StopPair(eventName, stopName);
        builtSets.Add($"{set.Name} ({(players ? "the player's" : "an NPC's")} gun: {(attack != null ? "attack, " : "")}loop{(release != null ? ", release" : "")}{(far ? ", far" : "")})");
        return eventName;
    }

    /// <summary>Armageddon's first bank, which holds its buses (their volumes, and how many sounds each lets play at once); overridden in its archive.</summary>
    public const string InitBank = "init.bnk_pc", InitBankArchive = "soundboot.vpp_pc";

    /// <summary>
    /// The bus over all the game's effects, vehicles, weapons and destruction, and how many sounds it lets play at once:
    /// Armageddon's 128, counting silent and virtual ones, past which it ends the oldest sound of the lowest priority
    /// without a word. Guerrilla had no such cap, and its world fills this one (a vehicle's engine alone is five
    /// or six loops; a firefight at a base did the rest: the run of 2026-11-11 00:43). The user, 2026-11-11: "you have
    /// the okay to raise the limits more": 256.
    /// </summary>
    public const uint TopBus = 0x1001382D;
    public const ushort GameTopBusLimit = 128, TopBusLimit = 256;

    /// <summary>
    /// The bus of the effects' sounds that play once (<see cref="WwiseBankWriter.EffectsBus"/>): Armageddon's 15 at once,
    /// its buses' under it counted in. Guerrilla's own categories have no such cap (only each cue's own number, 2 to 8
    /// for its destruction cues), and a car through a building fills 15 at once: the run of 2026-11-19 01:41 had 1,833
    /// destruction impacts in ten seconds, and the effects' one-shots, which give up when there is no room, were
    /// silent. The user, 2026-11-19 (through the main session), on raising the audio limits: 64.
    /// </summary>
    public const ushort GameEffectsBusLimit = 15, EffectsBusLimit = 64;

    static string InitBankPath(string overrides) => Path.Combine(overrides, "build", "pc", "cache", InitBankArchive, InitBank);

    /// <summary>
    /// Writes the override of Armageddon's init.bnk_pc: the player's own file as it is but for two numbers, the top
    /// bus's limit (<see cref="TopBusLimit"/>) and the effects' bus's (<see cref="EffectsBusLimit"/>). A bus is an
    /// object of type 8 in the bank's HIRC part: its id, its parent (4 bytes), volume, LFE, pitch and low-pass (4
    /// each), whether the newest is ended (1), then how many play at once (2). Each number is changed only where it
    /// is the game's own; else nothing is written and the build stops.
    /// </summary>
    static string WriteBusLimit(string game, string overrides)
    {
        var boot = Packfile.Open(Path.Combine(game, "build", "pc", "cache", InitBankArchive));
        byte[] bank = boot.Read(boot.Find(InitBank) ?? throw new InvalidDataException($"no {InitBank} in Armageddon's {InitBankArchive}"));
        const int limitAt = 4 + 4 + 16 + 1;  // from the object's id
        var raises = new (uint Bus, ushort Game, ushort Ours)[] { (TopBus, GameTopBusLimit, TopBusLimit), (WwiseBankWriter.EffectsBus, GameEffectsBusLimit, EffectsBusLimit) };
        var found = new Dictionary<uint, int>();
        for (int at = 0; at + 8 <= bank.Length;)
        {
            int size = BitConverter.ToInt32(bank, at + 4);
            if (size < 0 || at + 8 + size > bank.Length) break;
            if (System.Text.Encoding.ASCII.GetString(bank, at, 4) == "HIRC" && size >= 4)
                for (int k = 0, count = BitConverter.ToInt32(bank, at + 8), o = at + 12; k < count && o + 12 <= at + 8 + size; k++)
                {
                    uint type = BitConverter.ToUInt32(bank, o);
                    int length = BitConverter.ToInt32(bank, o + 4);
                    if (length < 4) break;
                    uint id = BitConverter.ToUInt32(bank, o + 8);
                    if (type == 8 && length >= limitAt + 2 && raises.Any(r => r.Bus == id)) found[id] = o + 8 + limitAt;
                    o += 8 + length;
                }
            at += 8 + size;
        }
        foreach (var (bus, armageddons, _) in raises)
        {
            if (!found.TryGetValue(bus, out int place)) throw new InvalidDataException($"Armageddon's {InitBank} has no bus {bus:X8}: its sounds' limit can't be raised");
            ushort limit = BitConverter.ToUInt16(bank, place);
            if (limit != armageddons)
                throw new InvalidDataException($"Armageddon's {InitBank}: the bus {bus:X8} lets {limit} sounds play at once, not the {armageddons} expected; left as it is");
        }
        foreach (var (bus, _, ours) in raises) BitConverter.GetBytes(ours).CopyTo(bank, found[bus]);
        string path = InitBankPath(overrides);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, bank);
        return $"the game's sounds at once: {TopBusLimit} (Armageddon's {GameTopBusLimit}), the top bus's limit, and the effects' one-shots {EffectsBusLimit} " +
               $"(Armageddon's {GameEffectsBusLimit}), in an override of its own {InitBankArchive}\\{InitBank}";
    }

    /// <summary>Says that posting <paramref name="stopEvent"/> stops the loop <paramref name="loopEvent"/> posted (the game posts it when it stops that sound).</summary>
    public void StopPair(string loopEvent, string stopEvent) => stopPairs[WwiseBankWriter.Id(loopEvent)] = WwiseBankWriter.Id(stopEvent);

    /// <summary>Writes the banks, their table rows and the stop pairs; the lines say what was written.</summary>
    public List<string> Write(string game, string overrides, string modFolder)
    {
        var lines = new List<string>();
        // The banks this part wrote before and doesn't now are taken away.
        var listed = Listed(modFolder, part);
        foreach (string old in listed.Banks.Where(b => !banks.ContainsKey(b)))
            foreach (string file in new[] { old + ".bnk_pc", old + "_media.bnk_pc" })
                OverrideLayout.Delete(overrides, file);
        long total = 0;
        foreach (var (name, bank) in banks.OrderBy(b => b.Key))
        {
            var (bankFile, mediaFile) = bank.Write();
            File.WriteAllBytes(OverrideLayout.NewFile(overrides, OverrideLayout.Sounds, name + ".bnk_pc"), bankFile);
            File.WriteAllBytes(OverrideLayout.NewFile(overrides, OverrideLayout.Sounds, name + "_media.bnk_pc"), mediaFile);
            total += bankFile.Length + mediaFile.Length;
            lines.Add($"{name}: {bank.Counts()} ({mediaFile.Length / 1048576.0:0.0} MB)");
        }
        // What the game's reader streams: the media as written against the same sound uncompressed.
        long pcm = banks.Values.Sum(b => b.MediaBytes.Pcm), written = banks.Values.Sum(b => b.MediaBytes.Written);
        if (Adpcm)
        {
            int waves = banks.Values.Sum(b => b.AdpcmCheck.Count), loops = banks.Values.Sum(b => b.AdpcmLoops.Loops), kept = banks.Values.Sum(b => b.AdpcmLoops.KeptPcm);
            double worst = banks.Values.Where(b => b.AdpcmCheck.Count > 0).Select(b => b.AdpcmCheck.Worst).DefaultIfEmpty(0).Min();
            double mean = waves > 0 ? banks.Values.Sum(b => b.AdpcmCheck.Sum) / waves : 0;
            lines.Add($"media {written / 1048576.0:0.0} MB as Wwise ADPCM ({pcm / 1048576.0:0.0} MB as PCM): {waves} waves, decoded again {mean:0.#} dB " +
                      $"from Guerrilla's on average, the worst {worst:0.#} dB; {loops} looping sounds' waves, {kept} of them kept uncompressed (their loop off ADPCM's blocks of 64 samples)");
        }
        else lines.Add($"media {written / 1048576.0:0.0} MB as PCM");
        if (builtSets.Count > 0) lines.Add($"attack-loop-release sets, played as Guerrilla plays them: {string.Join("; ", builtSets)}");
        if (builtFar.Count > 0) lines.Add($"far versions played beside the near cue: {string.Join("; ", builtFar)}");
        Directory.CreateDirectory(modFolder);
        WriteTable(game, modFolder, listed.Banks, banks.Keys, LoadAtBoot);
        WriteStopPairs(game, overrides, listed.Pairs, stopPairs);
        lines.Add(WriteBusLimit(game, overrides));
        File.WriteAllLines(ListFile(modFolder, part), [.. banks.Keys.Order().Select(b => "bank " + b), .. stopPairs.Select(p => $"pair {p.Key} {p.Value}")]);
        lines.Add($"{banks.Count} banks ({total / 1048576.0:0} MB) listed in mods\\guerrilla_world\\{BankTable}; {stopPairs.Count} loops' stop events in {StopPairArchive}\\{StopPairFile}");
        return lines;
    }

    static string ListFile(string modFolder, string part) => Path.Combine(modFolder, part + "_sound_banks.txt");

    /// <summary>What a part wrote last: its banks and its stop pairs' loop events.</summary>
    static (HashSet<string> Banks, HashSet<uint> Pairs) Listed(string modFolder, string part)
    {
        var banks = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var pairs = new HashSet<uint>();
        string list = ListFile(modFolder, part);
        // The first test's list (vehicle_sound_banks.txt: a bank a line) counts as the vehicles' too.
        foreach (string file in new[] { list, part == "vehicles" ? Path.Combine(modFolder, "vehicle_sound_banks.txt") : "" })
        {
            if (file.Length == 0 || !File.Exists(file)) continue;
            foreach (string line in File.ReadAllLines(file).Select(l => l.Trim()).Where(l => l.Length > 0))
            {
                var words = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (words[0] == "pair" && words.Length == 3 && uint.TryParse(words[1], out uint loop)) pairs.Add(loop);
                else if (words[0] == "bank" && words.Length == 2) banks.Add(words[1]);
                else if (words.Length == 1) banks.Add(words[0]);
            }
        }
        return (banks, pairs);
    }

    /// <summary>
    /// audio_banks_patch.xtbl: the one in the mod's folder if there is one (other parts' rows kept), else
    /// Armageddon's, with this part's earlier rows taken out and <paramref name="banks"/> added (streaming,
    /// loaded at boot, as Armageddon's vehicles bank). The game stops on a row whose id isn't its name's
    /// hash ("name_id == bank_id"), so the id is always that.
    /// </summary>
    static void WriteTable(string game, string modFolder, ISet<string> old, IEnumerable<string> banks, bool loadAtBoot)
    {
        string path = Path.Combine(modFolder, BankTable);
        var table = File.Exists(path) ? XtblFile.FromBytes(File.ReadAllBytes(path)) : GameTable(game) ?? throw new InvalidDataException($"no {BankTable} in Armageddon's misc.vpp_pc");
        foreach (var entry in table.Entries.ToList())
            if (old.Contains(XtblFile.EntryName(entry)) || banks.Contains(XtblFile.EntryName(entry), StringComparer.OrdinalIgnoreCase)) table.RemoveEntry(entry);
        foreach (string bank in banks.Order())
        {
            var row = new XtblElement("NewEntity");
            row.AddElement("Name", bank);
            row.AddElement("wwise_id", WwiseBankWriter.Id(bank).ToString());
            row.AddElement("streaming", "True");
            row.AddElement("load_at_boot", loadAtBoot ? "True" : "False");
            row.AddElement("ram_bank_xbox2", "False");
            row.AddElement("ram_bank_ps3", "False");
            row.AddElement("ram_bank_pc", "False");
            row.AddElement("cacheable_xbox2", "True");
            row.AddElement("cacheable_ps3", "True");
            row.AddElement("cacheable_pc", "True");
            row.AddElement("voice", "False");
            table.Table!.AddElement(row);
        }
        // (How many it lists against GameBankLimit is the table check's: a part writing before another has taken its old
        // rows away may pass the limit for a moment.)
        File.WriteAllBytes(path, table.ToBytes());
    }

    /// <summary>
    /// The most banks Armageddon can list. Its bank list is one object made as the game starts (0xB93C30, a static
    /// constructor: <c>push 12Ch</c> at 0xB93C8B → 0xB94250): a hash map of 300 buckets and 300 records inside it
    /// (0xB92F60: the bucket a name's hash modulo 300, records from +0x4B8), so the number can't be raised. The patch's
    /// rows are listed first; past the last record the game's own audio_banks.xtbl fails ("AUDIOLIB:SOUNDBANK:Failed to
    /// parse audio_banks.xtbl", "Audiolib Soundbanks Init Failed!!") and no sound at all loads.
    /// </summary>
    public const int GameBankLimit = 300;

    /// <summary>
    /// How many rows the game would list: Armageddon's audio_banks.xtbl and the patch (as written so far in
    /// <paramref name="modFolder"/>, else the game's own), leaving out those whose names start with <paramref name="exceptPrefix"/>.
    /// </summary>
    public static int BankRows(string game, string modFolder, string exceptPrefix = "\0")
    {
        var misc = Packfile.Open(Path.Combine(game, "build", "pc", "cache", "misc.vpp_pc"));
        int Count(XtblFile? table) =>
            table?.Entries.Count(e => !XtblFile.EntryName(e).StartsWith(exceptPrefix, StringComparison.OrdinalIgnoreCase)) ?? 0;
        string patch = Path.Combine(modFolder, BankTable);
        return Count(misc.Find("audio_banks.xtbl") is { } main ? XtblFile.FromBytes(misc.Read(main)) : null) +
               Count(File.Exists(patch) ? XtblFile.FromBytes(File.ReadAllBytes(patch)) : GameTable(game));
    }

    static XtblFile? GameTable(string game)
    {
        var misc = Packfile.Open(Path.Combine(game, "build", "pc", "cache", "misc.vpp_pc"));
        return misc.Find(BankTable) is { } entry ? XtblFile.FromBytes(misc.Read(entry)) : null;
    }

    // audio_info.vad_pc: "VWAIPC  ", u32 0, u16 1, u16 0x130, u16 0x18, u16 the pairs' count, u32 0x18 (where they
    // start), then (loop event, stop event) 8 bytes a pair. The game sizes its table at the count + 50.
    static string StopPairPath(string overrides) => Path.Combine(overrides, "build", "pc", "cache", StopPairArchive, StopPairFile);

    static byte[] GameStopPairs(string game)
    {
        var sounds = Packfile.Open(Path.Combine(game, "build", "pc", "cache", StopPairArchive));
        return sounds.Read(sounds.Find(StopPairFile) ?? throw new InvalidDataException($"no {StopPairFile} in Armageddon's {StopPairArchive}"));
    }

    static List<(uint Loop, uint Stop)> ReadStopPairs(byte[] b)
    {
        if (b.Length < 0x18 || System.Text.Encoding.ASCII.GetString(b, 0, 8) != "VWAIPC  ") throw new InvalidDataException($"{StopPairFile} isn't as expected");
        int count = BitConverter.ToUInt16(b, 0x12), at = BitConverter.ToInt32(b, 0x14);
        return Enumerable.Range(0, count).Select(i => (BitConverter.ToUInt32(b, at + 8 * i), BitConverter.ToUInt32(b, at + 8 * i + 4))).ToList();
    }

    static void WriteStopPairs(string game, string overrides, ISet<uint> old, IReadOnlyDictionary<uint, uint> ours)
    {
        string path = StopPairPath(overrides);
        byte[] gameFile = GameStopPairs(game);
        byte[] current = File.Exists(path) ? File.ReadAllBytes(path) : gameFile;
        var pairs = ReadStopPairs(current).Where(p => !old.Contains(p.Loop) && !ours.ContainsKey(p.Loop)).ToList();
        pairs.AddRange(ours.Select(p => (p.Key, p.Value)));
        if (pairs.Count > ushort.MaxValue) throw new InvalidDataException($"{pairs.Count} stop pairs are more than {StopPairFile} holds");
        var gamePairs = ReadStopPairs(gameFile);
        if (pairs.Count == gamePairs.Count && pairs.SequenceEqual(gamePairs))
        {
            if (File.Exists(path)) File.Delete(path);
            return;
        }
        var b = new List<byte>();
        b.AddRange(gameFile.AsSpan(0, 0x12).ToArray());       // its header as the game's, but the count
        b.AddRange(BitConverter.GetBytes((ushort)pairs.Count));
        b.AddRange(BitConverter.GetBytes(0x18));
        foreach (var (loop, stop) in pairs) { b.AddRange(BitConverter.GetBytes(loop)); b.AddRange(BitConverter.GetBytes(stop)); }
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, [.. b]);
    }

    /// <summary>Takes away a part's banks, its table rows and its stop pairs (the table and the stop pairs' file go once only Armageddon's own are left).</summary>
    public static IEnumerable<string> Remove(string game, string overrides, string modFolder, string part)
    {
        var (banks, pairs) = Listed(modFolder, part);
        foreach (string bank in banks)
            foreach (string file in new[] { bank + ".bnk_pc", bank + "_media.bnk_pc" })
                foreach (string path in OverrideLayout.Delete(overrides, file)) yield return $"Deleted {path}";
        string tablePath = Path.Combine(modFolder, BankTable);
        if (File.Exists(tablePath))
        {
            var table = XtblFile.FromBytes(File.ReadAllBytes(tablePath));
            foreach (var entry in table.Entries.Where(e => banks.Contains(XtblFile.EntryName(e))).ToList()) table.RemoveEntry(entry);
            List<string> gameRows = GameTable(game) is { } original ? [.. original.Entries.Select(XtblFile.EntryName)] : [];
            if (table.Entries.Select(XtblFile.EntryName).SequenceEqual(gameRows, StringComparer.OrdinalIgnoreCase))
            {
                File.Delete(tablePath);
                yield return $"Deleted {tablePath}";
                // No bank of ours is left: the game's own limit of sounds again.
                if (File.Exists(InitBankPath(overrides)))
                {
                    File.Delete(InitBankPath(overrides));
                    yield return $"Deleted {InitBankPath(overrides)}";
                }
            }
            else File.WriteAllBytes(tablePath, table.ToBytes());
        }
        string pairPath = StopPairPath(overrides);
        if (File.Exists(pairPath))
        {
            WriteStopPairs(game, overrides, pairs, new Dictionary<uint, uint>());
            if (!File.Exists(pairPath)) yield return $"Deleted {pairPath}";
        }
        foreach (string list in new[] { ListFile(modFolder, part), part == "vehicles" ? Path.Combine(modFolder, "vehicle_sound_banks.txt") : "" })
            if (list.Length > 0 && File.Exists(list)) { File.Delete(list); yield return $"Deleted {list}"; }
    }
}
