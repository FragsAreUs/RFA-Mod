using System.IO;
using RfaFormats.Audio;

namespace GuerrillaBuilder;

/// <summary>
/// Plays our sound banks without speakers, on rfa_audio's engine (the one the editor previews them with, and the game's
/// hybrid will play them with) and OpenAL Soft's loopback device, which renders as fast as it can:
/// - each bank must load with every object read to its end and every child, target and medium it names there;
/// - the axes: a mono sound placed on the listener's left must come out louder on the left, and on the right on the right;
/// - every event is posted on a game object of its own, at the listener. One that plays once must make a sound and end
///   by its longest medium's length (and its delays) and 2 s more. One that loops is left 2 s, then its stop event (the
///   event whose stop or break acts on what it plays) is posted, and it must end within that stop's fade and 2 s more.
///   An event on a switch (a weapon's WEAPON_SOUND_TYPE, an engine's RFG_ENGINE_LISTENER) is posted once for each value
///   the bank lists, and must make a sound for one of them at least. An event of stops alone must find its targets.
/// False if any of that fails. On the 40 banks of 2026-11-21 (6,548 events) it takes about 20 s.
/// </summary>
static class SoundCheck
{
    const int Rate = 24000;           // what it renders at: half the device's usual, as fast again
    const int Piece = Rate / 10;      // 0.1 s a step
    const float Margin = 2.0f;        // seconds an event may take past what its media say
    const int MostListed = 40;        // problems listed in the log (all counted)

    public static bool Run(Setup setup, BuildLog log)
    {
        if (!RfaAudio.Installed)
        {
            log.Line("Sound check: skipped, rfa_audio.dll or soft_oal.dll isn't beside GuerrillaBuilder.exe");
            return true;
        }
        var bankFiles = Directory.Exists(setup.Cache)
            ? Directory.EnumerateFiles(setup.Cache, "rfg*.bnk_pc").Where(f => !f.EndsWith("_media.bnk_pc", StringComparison.OrdinalIgnoreCase))
                       .Order(StringComparer.OrdinalIgnoreCase).ToList()
            : [];
        if (bankFiles.Count == 0)
        {
            log.Line("Sound check: no sound banks of ours built here");
            return true;
        }
        try
        {
            RfaAudio.Open(loopback: true, ownThread: false, sampleRate: Rate, maxSources: 256, cacheMb: 256);
        }
        catch (Exception e) when (e is InvalidOperationException or DllNotFoundException or EntryPointNotFoundException or BadImageFormatException)
        {
            log.Line($"Sound check: the engine didn't open: {e.Message}");
            return false;
        }
        try
        {
            return Check(setup, bankFiles, log);
        }
        finally
        {
            RfaAudio.OnEnded(null);
            RfaAudio.Close();
        }
    }

    static bool Check(Setup setup, List<string> bankFiles, BuildLog log)
    {
        var started = DateTime.UtcNow;
        var problems = new List<string>();
        var events = new List<(string Bank, uint BankId, uint Event)>();
        foreach (string file in bankFiles)
        {
            string name = Path.GetFileNameWithoutExtension(file), media = Path.Combine(setup.Cache, name + "_media.bnk_pc");
            uint id;
            try { id = RfaAudio.LoadBank(file, File.Exists(media) ? media : null); }
            catch (InvalidDataException e)
            {
                problems.Add($"{name}: doesn't load: {e.Message}");
                continue;
            }
            if (!File.Exists(media)) problems.Add($"{name}: no media file ({name}_media.bnk_pc)");
            foreach (string p in RfaAudio.BankProblems(id)) problems.Add($"{name}: {p}");
            foreach (uint e in RfaAudio.BankEvents(id)) events.Add((name, id, e));
        }
        var names = SoundEventNames.Find(setup.ModFolder, events.Select(e => e.Event));
        string Name(uint id) => SoundEventNames.NameOf(id, names);

        // When each play ends, and how many voices it started.
        var ended = new Dictionary<uint, int>();
        RfaAudio.OnEnded((playing, _, _, voices) => ended[playing] = voices);

        string axes = CheckAxes(events.Select(e => e.Event).ToList(), ended, problems);

        // Each node's children (from every bank's parents), so a loop's stop event is found by what it acts on: anything
        // under what the loop plays, or above it.
        var childrenOf = new Dictionary<uint, List<uint>>();
        var parentOf = new Dictionary<uint, uint>();
        foreach (uint bankId in events.Select(e => e.BankId).Distinct())
            foreach (var o in RfaAudio.BankObjects(bankId))
                if (o.Type is 2 or 5 or 6 or 7 or 9)
                {
                    parentOf[o.Id] = o.Parent;
                    if (o.Parent == 0) continue;
                    if (!childrenOf.TryGetValue(o.Parent, out var list)) childrenOf[o.Parent] = list = [];
                    list.Add(o.Id);
                }
        HashSet<uint> Around(IEnumerable<uint> targets)
        {
            var nodes = new HashSet<uint>();
            var todo = new Stack<uint>(targets);
            while (todo.Count > 0)
            {
                uint node = todo.Pop();
                if (nodes.Add(node) && childrenOf.TryGetValue(node, out var children))
                    foreach (uint c in children) todo.Push(c);
            }
            foreach (uint t in targets)
                for (uint p = parentOf.GetValueOrDefault(t); p != 0 && nodes.Add(p); p = parentOf.GetValueOrDefault(p)) { }
            return nodes;
        }

        // Which events stop or break each node.
        var stopsOf = new Dictionary<uint, List<(uint Event, int FadeMs)>>();
        foreach (var (_, _, e) in events)
            foreach (var a in RfaAudio.EventActions(e))
                if (a.Kind is 1 or 0x90)
                {
                    if (!stopsOf.TryGetValue(a.Target, out var list)) stopsOf[a.Target] = list = [];
                    if (!list.Any(s => s.Event == e)) list.Add((e, a.FadeMs));
                }

        int oneShots = 0, loops = 0, stopOnly = 0, switched = 0, unpaired = 0;
        double rendered = 0;
        ulong nextObject = 1000;
        foreach (var (bank, bankId, e) in events)
        {
            var info = RfaAudio.GetEventInfo(e);
            if (info.Missing > 0) problems.Add($"{bank}: {Name(e)} refers to {info.Missing} object(s) or media that aren't there");
            if (info.Plays == 0)
            {
                stopOnly++;
                continue;
            }
            var targets = RfaAudio.EventActions(e).Where(a => a.Kind == 4).Select(a => a.Target).ToList();
            var stops = Around(targets).SelectMany(t => stopsOf.TryGetValue(t, out var s) ? s : []).Where(s => s.Event != e)
                                       .DistinctBy(s => s.Event).ToList();
            uint[] values = info.SwitchGroup != 0 ? RfaAudio.BankSwitchValues(bankId, info.SwitchGroup) : [];
            if (values.Length > 0) switched++;
            bool anySound = false;
            foreach (uint? value in values.Length > 0 ? values.Select(v => (uint?)v) : [null])
            {
                ulong obj = nextObject++;
                if (value is uint v) RfaAudio.SetSwitch(obj, info.SwitchGroup, v);
                string what = Name(e) + (value is uint w ? $" ({SoundEventNames.NameOf(info.SwitchGroup)} = {SoundEventNames.NameOf(w)})" : "");
                double start = RfaAudio.Time;
                uint playing = RfaAudio.Post(e, obj);
                if (playing == 0)
                {
                    problems.Add($"{bank}: {what} doesn't post");
                    continue;
                }
                bool done = false;
                if (info.Loops != 0)
                {
                    Advance(2.0, playing, ended);
                    if (!ended.ContainsKey(playing))
                    {
                        // On for ever: its stop event ends it (or, with none, it is stopped as the mods would by its playing id).
                        int fade = 0;
                        if (stops.Count > 0)
                            foreach (var s in stops)
                            {
                                RfaAudio.Post(s.Event, obj);
                                fade = Math.Max(fade, s.FadeMs);
                            }
                        else
                        {
                            unpaired++;
                            RfaAudio.StopPlaying(playing, 100);
                            fade = 100;
                        }
                        done = Advance(fade / 1000.0 + Margin, playing, ended);
                        if (!done) problems.Add($"{bank}: {what} loops and doesn't end after {(stops.Count > 0 ? string.Join(", ", stops.Select(s => Name(s.Event))) : "being stopped")}");
                    }
                    else done = true;
                }
                else
                {
                    double longest = info.LongestSeconds >= 0 ? info.LongestSeconds : 60;
                    done = Advance(Math.Min(longest, 600) + Margin, playing, ended);
                    if (!done) problems.Add($"{bank}: {what} hasn't ended {longest + Margin:0.#} s after it was posted (its media {longest:0.##} s)");
                }
                if (!done)
                {
                    RfaAudio.StopPlaying(playing);
                    Advance(0.1, playing, ended);
                }
                if (ended.TryGetValue(playing, out int voices) && voices > 0) anySound = true;
                rendered += RfaAudio.Time - start;
                RfaAudio.RemoveObject(obj);
            }
            if (!anySound) problems.Add($"{bank}: {Name(e)} made no sound{(values.Length > 0 ? " for any value of " + SoundEventNames.NameOf(info.SwitchGroup) : "")}");
            if (info.Loops != 0) loops++;
            else oneShots++;
        }

        var status = RfaAudio.GetStatus();
        int unnamed = events.Count(e => !names.ContainsKey(e.Event));
        log.Line($"Sound check: {bankFiles.Count} banks, {events.Count:N0} events: {oneShots:N0} played once and ended, {loops} looped and ended on their " +
                 $"stop event{(unpaired > 0 ? $" ({unpaired} have none and were stopped by their playing id)" : "")}, {switched} posted once for each " +
                 $"value of their switch, {stopOnly} stops alone; {rendered:N0} s of sound rendered in {(DateTime.UtcNow - started).TotalSeconds:0} s " +
                 $"({status.Underruns} stream underruns, {status.VoicesRefused} voices refused by their limits); {axes}" +
                 (unnamed > 0 ? $"; {unnamed} events' names not found (shown by id)" : ""));
        foreach (string p in problems.Take(MostListed)) log.Line("Sound check: " + p);
        if (problems.Count > MostListed) log.Line($"Sound check: ... and {problems.Count - MostListed} more problems");
        if (problems.Count > 0) log.Line($"Sound check: {problems.Count} problems (above)");
        return problems.Count == 0;
    }

    /// <summary>Renders until the play ends or the time is up. True if it ended.</summary>
    static bool Advance(double seconds, uint playing, Dictionary<uint, int> ended)
    {
        double until = RfaAudio.Time + seconds;
        while (!ended.ContainsKey(playing) && RfaAudio.Time < until) RfaAudio.Render(Piece);
        return ended.ContainsKey(playing);
    }

    /// <summary>
    /// A mono 3D sound on the listener's left must come out louder on the left, and on its right on the right (the game's
    /// axes, left-handed, against OpenAL's right-handed ones). The listener is at the origin facing +z (the game's forward).
    /// </summary>
    static string CheckAxes(List<uint> events, Dictionary<uint, int> ended, List<string> problems)
    {
        RfaAudio.SetListener(0, 0, 0);
        foreach (uint e in events)
        {
            var info = RfaAudio.GetEventInfo(e);
            if (info.ThreeD == 0 || info.Loops != 0 || info.SwitchGroup != 0 || info.LongestSeconds < 0.3f || info.FarthestMetres < 10) continue;
            // It must be mono: a stereo sound isn't placed.
            const ulong probe = 1;
            RfaAudio.SetPosition(probe, -4, 0, 0);
            uint playing = RfaAudio.Post(e, probe);
            RfaAudio.Render(Rate / 50);
            bool mono = RfaAudio.Voices().Any(v => v.PlayingId == playing && v.Channels == 1 && v.ThreeD != 0 && v.IsVirtual == 0);
            RfaAudio.StopPlaying(playing);
            RfaAudio.Render(Piece);
            if (!mono) continue;
            double Balance(float x)
            {
                RfaAudio.SetPosition(probe, x, 0, 0);
                uint p = RfaAudio.Post(e, probe);
                var buffer = new float[2 * (int)(Math.Min(info.LongestSeconds, 1.5f) * Rate)];
                RfaAudio.Render(buffer.Length / 2, buffer);
                RfaAudio.StopPlaying(p);
                RfaAudio.Render(Piece);
                double left = 0, right = 0;
                for (int i = 0; i + 1 < buffer.Length; i += 2)
                {
                    left += buffer[i] * buffer[i];
                    right += buffer[i + 1] * buffer[i + 1];
                }
                return 10 * Math.Log10((left + 1e-12) / (right + 1e-12));  // dB louder on the left
            }
            double onLeft = Balance(-4), onRight = Balance(4);
            RfaAudio.RemoveObject(probe);
            if (onLeft > 3 && onRight < -3) return $"axes right (a sound 4 m to the left {onLeft:0.#} dB louder on the left, to the right {-onRight:0.#} dB louder on the right)";
            problems.Add($"the axes are wrong: a sound 4 m to the left is {onLeft:0.#} dB louder on the left, one to the right {onRight:0.#} dB");
            return "axes WRONG";
        }
        return "axes not checked (no mono 3D sound found)";
    }
}
