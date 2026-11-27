using System.IO;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using System.Windows.Threading;
using RfaFormats;
using RfaFormats.Audio;
using RfaFormats.Guerrilla;

namespace RfaEditor;

/// <summary>
/// The sound preview: a sound bank's events, played on rfa_audio's engine (src\audio\, the one the game's hybrid will
/// play our banks with, so what plays here is what the game will play). A pad places the sound around the listener;
/// sliders set the game parameters the bank's curves use (an engine's RPM), lists its switches (a weapon's
/// WEAPON_SOUND_TYPE); the voices playing are listed as they play. For an event made from one of Guerrilla's cues, that
/// cue can be played beside it from the player's copy of Guerrilla (its XACT waves), to compare.
/// </summary>
public partial class SoundPreview : UserControl
{
    const ulong SoundObject = 1, OriginalObject = 2;

    sealed record EventItem(uint Id, string Name, string Tags);
    sealed record VoiceRow(string What, string Codec, string Format, string Loop, string Level, string Distance, string Time, string State);

    readonly Action<string> log;
    readonly DispatcherTimer timer;
    uint bankId, initBankId;
    string modFolder = "";
    List<EventItem> allEvents = [];
    readonly Dictionary<uint, uint> parentOf = [], busOf = [];
    readonly Dictionary<uint, List<uint>> childrenOf = [];
    readonly Dictionary<uint, List<(uint Event, int FadeMs)>> stopsOf = [];
    readonly Dictionary<uint, uint> playing = [];          // our playing ids → their events
    readonly List<(Slider Slider, CheckBox Sweep, double Min, double Max)> sliders = [];
    readonly Random random = new();
    float padX, padZ;                                      // the sound's place: metres right of the listener, and ahead
    float range = 50;
    bool dragging;
    int generation;                                         // which bank a background search was for
    Ellipse? soundMark;
    Line? soundLine;

    // Kept for the editor's whole run: names found (searching reads ~20 MB), and Guerrilla's sound archive.
    static readonly Dictionary<uint, string> namesFound = [];
    static readonly HashSet<uint> namesSearched = [];
    static GuerrillaSounds? guerrillaSounds;
    static string? guerrillaSoundsFolder;
    static bool closesOnExit;

    public SoundPreview(Action<string> log)
    {
        InitializeComponent();
        this.log = log;
        RangeList.SelectedIndex = 2;
        timer = new DispatcherTimer(DispatcherPriority.Background) { Interval = TimeSpan.FromMilliseconds(100) };
        timer.Tick += (_, _) => OnTimer();
        Pad.Loaded += (_, _) => DrawPad();
        IsVisibleChanged += (_, _) =>
        {
            if (IsVisible && bankId != 0) timer.Start();
            else timer.Stop();
        };
        OriginalButton.IsEnabled = false;
    }

    // ---- Showing a bank ---------------------------------------------------------------------------

    /// <summary>
    /// Shows a bank: loads it (and its media) into the engine, lists its events, its game parameters and switches.
    /// <paramref name="initBank"/>: Armageddon's init.bnk_pc (a file), for its buses' volumes, or null.
    /// <paramref name="archive"/>: one of Armageddon's own archives that holds the bank and its media uncompressed
    /// (sounds.vpp_pc, voices.vpp_pc): the bank is read there, in place (<paramref name="bankPath"/> is then its name).
    /// </summary>
    public void ShowBank(string bankPath, string? mediaPath, string modFolder, string? initBank, string? note, string? archive = null)
    {
        Hide();
        this.modFolder = modFolder;
        generation++;
        DetailsBox.Text = "";
        EventList.ItemsSource = null;
        ParameterPanel.Children.Clear();
        SwitchPanel.Children.Clear();
        sliders.Clear();
        if (!OpenEngine(out string why))
        {
            StatusText.Text = why;
            DetailsBox.Text = why;
            CountText.Text = "";
            return;
        }
        if (initBank != null && initBankId == 0)
        {
            try { initBankId = RfaAudio.LoadBank(initBank, null); }
            catch (InvalidDataException e) { log($"Sound preview: Armageddon's init.bnk_pc didn't load ({e.Message}): buses count as 0 dB"); }
        }
        try
        {
            bankId = archive != null ? RfaAudio.LoadBankFromArchive(archive, bankPath) : RfaAudio.LoadBank(bankPath, mediaPath);
        }
        catch (InvalidDataException e)
        {
            StatusText.Text = "This bank can't be read: " + e.Message;
            DetailsBox.Text = StatusText.Text;
            return;
        }
        ReadTree();
        uint[] events = RfaAudio.BankEvents(bankId);
        allEvents = [.. events.Select(Item).OrderBy(e => e.Name.Length == 8 && uint.TryParse(e.Name, System.Globalization.NumberStyles.HexNumber, null, out _) ? 1 : 0)
                                          .ThenBy(e => e.Name, StringComparer.OrdinalIgnoreCase)];
        ShowEvents();
        BuildParameters();
        BuildSwitches();
        string[] problems = RfaAudio.BankProblems(bankId);
        var objects = RfaAudio.BankObjects(bankId);
        string Count(uint type, string what) => objects.Count(o => o.Type == type) is int n and > 0 ? $"{n:N0} {what}" : "";
        string bankName = archive != null ? bankPath + ".bnk_pc" : System.IO.Path.GetFileName(bankPath);
        string media = archive != null ? $"\nIts media: {bankPath}_media.bnk_pc in {System.IO.Path.GetFileName(archive)}"
                     : mediaPath != null ? $"\nIts media: {System.IO.Path.GetFileName(mediaPath)}" : "\nNo media file beside it: its sounds can't play.";
        DetailsBox.Text = $"{bankName}: " + string.Join(", ", new[]
            {
                Count(4, "events"), Count(3, "actions"), Count(2, "sounds"), Count(5, "random/sequence containers"), Count(6, "switch containers"),
                Count(9, "layer containers"), Count(7, "actor-mixers"), Count(8, "buses"),
            }.Where(s => s.Length > 0)) +
            media +
            (note != null ? "\n" + note : "") +
            (problems.Length > 0 ? "\n\nProblems reading it:\n  " + string.Join("\n  ", problems) : "") +
            "\n\nPick an event to see what it plays.";
        StatusText.Text = RfaAudio.DeviceInfo();
        ApplyPlace();
        if (IsVisible) timer.Start();
        FindNamesAndTags(events);
    }

    /// <summary>Stops what this preview plays and lets the bank go (the preview shows something else).</summary>
    public void Hide()
    {
        timer.Stop();
        if (!RfaAudio.IsOpen) return;
        RfaAudio.RemoveObject(SoundObject);
        RfaAudio.RemoveObject(OriginalObject);
        if (bankId != 0) RfaAudio.UnloadBank(bankId);
        bankId = 0;
        playing.Clear();
        VoiceList.ItemsSource = null;
    }

    static bool OpenEngine(out string why)
    {
        why = "";
        if (RfaAudio.IsOpen) return true;
        if (!RfaAudio.Installed)
        {
            why = "The sound engine isn't beside the editor (rfa_audio.dll and soft_oal.dll in bin\\editor\\): build.cmd builds it.";
            return false;
        }
        try
        {
            RfaAudio.Open(ownThread: true, maxSources: 128, cacheMb: 256);
            RfaAudio.SetListener(0, 0, 0);
            if (!closesOnExit && Application.Current != null)
            {
                Application.Current.Exit += (_, _) => RfaAudio.Close();
                closesOnExit = true;
            }
            return true;
        }
        catch (Exception e) when (e is InvalidOperationException or DllNotFoundException or EntryPointNotFoundException or BadImageFormatException)
        {
            why = "The sound engine didn't open: " + e.Message;
            return false;
        }
    }

    // Each node's parent and children, its bus, and which events stop or break each node: a loop's stop event is the one
    // that acts on what it plays (or on anything under or above it), as the game's stop pairs have it.
    void ReadTree()
    {
        parentOf.Clear();
        childrenOf.Clear();
        busOf.Clear();
        stopsOf.Clear();
        foreach (var o in RfaAudio.BankObjects(bankId))
        {
            if (o.Type is not (2 or 5 or 6 or 7 or 9)) continue;
            parentOf[o.Id] = o.Parent;
            busOf[o.Id] = o.Bus;
            if (o.Parent == 0) continue;
            if (!childrenOf.TryGetValue(o.Parent, out var list)) childrenOf[o.Parent] = list = [];
            list.Add(o.Id);
        }
        foreach (uint e in RfaAudio.BankEvents(bankId))
            foreach (var a in RfaAudio.EventActions(e))
                if (a.Kind is 1 or 0x90)
                {
                    if (!stopsOf.TryGetValue(a.Target, out var list)) stopsOf[a.Target] = list = [];
                    if (!list.Any(s => s.Event == e)) list.Add((e, a.FadeMs));
                }
    }

    List<(uint Event, int FadeMs)> StopEventsOf(uint eventId)
    {
        var targets = RfaAudio.EventActions(eventId).Where(a => a.Kind == 4).Select(a => a.Target).ToList();
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
        return [.. nodes.SelectMany(n => stopsOf.TryGetValue(n, out var s) ? s : []).Where(s => s.Event != eventId).DistinctBy(s => s.Event)];
    }

    // The bus of the node an event plays (its top node's).
    uint BusOf(uint eventId)
    {
        foreach (var a in RfaAudio.EventActions(eventId).Where(a => a.Kind == 4))
        {
            uint node = a.Target;
            for (int depth = 0; depth < 64 && parentOf.GetValueOrDefault(node) != 0; depth++) node = parentOf[node];
            if (busOf.GetValueOrDefault(node) is uint bus and not 0) return bus;
        }
        return 0;
    }

    EventItem Item(uint id) => new(id, NameOf(id), "");

    static string NameOf(uint id) => namesFound.TryGetValue(id, out var n) ? n : SoundEventNames.NameOf(id);

    // The events' names (from the mods' tables and texts) and what each does, found off the UI thread.
    void FindNamesAndTags(uint[] events)
    {
        int forBank = generation;
        string folder = modFolder;
        var unsearched = events.Where(e => !namesSearched.Contains(e)).ToList();
        CountText.Text = $"{events.Length:N0} events" + (unsearched.Count > 0 ? "; finding their names ..." : "");
        Task.Run(() =>
        {
            var found = unsearched.Count > 0 ? SoundEventNames.Find(folder, unsearched) : [];
            var tags = new Dictionary<uint, string>();
            foreach (uint e in events)
            {
                if (forBank != generation) return (found, tags, false);
                var info = RfaAudio.GetEventInfo(e);
                var words = new List<string>();
                if (info.Plays == 0) words.Add("stop");
                if (info.Loops != 0) words.Add("loop");
                if (info.ThreeD != 0) words.Add("3D");
                if (info.SwitchGroup != 0) words.Add("switch");
                if (info.Missing != 0) words.Add("missing!");
                tags[e] = string.Join(" ", words);
            }
            return (found, tags, true);
        }).ContinueWith(t =>
        {
            if (t.IsFaulted || forBank != generation || !t.Result.Item3) return;
            foreach (var (id, name) in t.Result.found) namesFound[id] = name;
            foreach (uint e in unsearched) namesSearched.Add(e);
            var tags = t.Result.tags;
            uint? selected = (EventList.SelectedItem as EventItem)?.Id;
            allEvents = [.. events.Select(e => new EventItem(e, NameOf(e), tags.GetValueOrDefault(e, "")))
                                  .OrderBy(e => namesFound.ContainsKey(e.Id) ? 0 : 1).ThenBy(e => e.Name, StringComparer.OrdinalIgnoreCase)];
            ShowEvents(selected);
            int unnamed = events.Count(e => !namesFound.ContainsKey(e));
            CountText.Text = $"{events.Length:N0} events" + (unnamed > 0 ? $" ({unnamed} not named: shown by id)" : "") +
                             ". Double-click or Enter plays one.";
        }, TaskScheduler.FromCurrentSynchronizationContext());
    }

    void ShowEvents(uint? select = null)
    {
        string text = SearchBox.Text.Trim();
        var shown = text.Length == 0 ? allEvents
            : allEvents.Where(e => e.Name.Contains(text, StringComparison.OrdinalIgnoreCase) || e.Id.ToString("X8").Contains(text, StringComparison.OrdinalIgnoreCase)).ToList();
        EventList.ItemsSource = shown;
        if (select is uint id && shown.FirstOrDefault(e => e.Id == id) is { } again) EventList.SelectedItem = again;
    }

    void OnSearch(object sender, TextChangedEventArgs e) => ShowEvents((EventList.SelectedItem as EventItem)?.Id);

    void BuildParameters()
    {
        foreach (uint parameter in RfaAudio.BankParameters(bankId).Where(p => p != RfaAudio.Id("game_distance")).OrderBy(p => SoundEventNames.NameOf(p)))
        {
            var (min, max) = RfaAudio.ParameterRange(bankId, parameter) ?? (0f, 1f);
            if (max <= min) max = min + 1;
            var row = new DockPanel { Margin = new Thickness(0, 0, 0, 4) };
            var name = new TextBlock { Text = SoundEventNames.NameOf(parameter), Width = 110, VerticalAlignment = VerticalAlignment.Center };
            var value = new TextBlock { Width = 56, VerticalAlignment = VerticalAlignment.Center, TextAlignment = TextAlignment.Right, Margin = new Thickness(6, 0, 6, 0) };
            var sweep = new CheckBox { Content = "Sweep", VerticalAlignment = VerticalAlignment.Center, ToolTip = "Move it from one end to the other and back, over 8 s" };
            var slider = new Slider { Minimum = min, Maximum = max, Value = min, VerticalAlignment = VerticalAlignment.Center, MinWidth = 120 };
            slider.ValueChanged += (_, _) =>
            {
                value.Text = slider.Value.ToString(max - min > 50 ? "0" : "0.00");
                RfaAudio.SetParameter(SoundObject, parameter, (float)slider.Value);
            };
            value.Text = min.ToString(max - min > 50 ? "0" : "0.00");
            RfaAudio.SetParameter(SoundObject, parameter, min);
            DockPanel.SetDock(name, Dock.Left);
            DockPanel.SetDock(sweep, Dock.Right);
            DockPanel.SetDock(value, Dock.Right);
            row.Children.Add(name);
            row.Children.Add(sweep);
            row.Children.Add(value);
            row.Children.Add(slider);
            ParameterPanel.Children.Add(row);
            sliders.Add((slider, sweep, min, max));
        }
        if (ParameterPanel.Children.Count == 0)
            ParameterPanel.Children.Add(new TextBlock { Text = "None in this bank's curves.", Foreground = (Brush)FindResource("DimTextBrush") });
    }

    void BuildSwitches()
    {
        foreach (uint group in RfaAudio.BankSwitchGroups(bankId))
        {
            var row = new DockPanel { Margin = new Thickness(0, 0, 0, 4) };
            var name = new TextBlock { Text = SoundEventNames.NameOf(group), Width = 160, VerticalAlignment = VerticalAlignment.Center };
            var list = new ComboBox { MinWidth = 140 };
            list.Items.Add(new ComboBoxItem { Content = "(not set: its default)", Tag = 0u });
            foreach (uint value in RfaAudio.BankSwitchValues(bankId, group)) list.Items.Add(new ComboBoxItem { Content = SoundEventNames.NameOf(value), Tag = value });
            list.SelectionChanged += (_, _) =>
            {
                if (list.SelectedItem is ComboBoxItem { Tag: uint value }) RfaAudio.SetSwitch(SoundObject, group, value);
            };
            // A weapon's sounds play nothing until the game sets which: start on the first value.
            list.SelectedIndex = list.Items.Count > 1 ? 1 : 0;
            DockPanel.SetDock(name, Dock.Left);
            row.Children.Add(name);
            row.Children.Add(list);
            SwitchPanel.Children.Add(row);
        }
        if (SwitchPanel.Children.Count == 0)
            SwitchPanel.Children.Add(new TextBlock { Text = "None in this bank.", Foreground = (Brush)FindResource("DimTextBrush") });
    }

    // ---- An event -----------------------------------------------------------------------------------

    void OnEventChosen(object sender, SelectionChangedEventArgs e)
    {
        if (EventList.SelectedItem is not EventItem item) return;
        var info = RfaAudio.GetEventInfo(item.Id);
        var lines = new List<string> { item.Name + (item.Name != item.Id.ToString("X8") ? $" ({item.Id:X8})" : "") };
        var what = new List<string>();
        if (info.Plays > 0) what.Add($"plays {info.Plays}");
        if (info.Stops > 0) what.Add($"stops {info.Stops}");
        if (info.Breaks > 0) what.Add($"breaks {info.Breaks}");
        if (info.Loops != 0) what.Add("goes on until stopped");
        what.Add(info.ThreeD != 0 ? "3D at the sound's place" : "2D");
        if (info.FarthestMetres > 0) what.Add($"fades out by {info.FarthestMetres:0.#} m");
        if (info.LongestSeconds > 0) what.Add($"its longest medium {info.LongestSeconds:0.##} s");
        if (info.SwitchGroup != 0) what.Add($"on the switch {SoundEventNames.NameOf(info.SwitchGroup)}");
        if (info.Missing > 0) what.Add($"{info.Missing} objects or media missing");
        lines.Add(string.Join("; ", what) + ".");
        var stops = StopEventsOf(item.Id);
        if (info.Loops != 0)
            lines.Add(stops.Count > 0 ? "Stop posts " + string.Join(", ", stops.Select(s => NameOf(s.Event))) + ", as the game does." : "It has no stop event: Stop fades it out.");
        uint bus = BusOf(item.Id);
        if (bus != 0) lines.Add($"Its bus {bus:X8}: {RfaAudio.BusVolume(bus):0.#} dB with its parents'{(initBankId == 0 ? " (Armageddon's init.bnk_pc not read: 0 dB)" : "")}.");
        lines.Add("");
        lines.Add(Named(RfaAudio.Describe(item.Id)));
        // Guerrilla's original, if this event was made from one of its cues or lines.
        var original = OriginalOf(item.Name, out string? whyNot);
        OriginalButton.IsEnabled = original != null;
        OriginalButton.ToolTip = original != null
            ? $"Play {original.Text} beside it, from your copy of Guerrilla: its wave, volume, pitch and loop, on this event's bus " +
              "(but not its fade with distance: it plays as near)"
            : whyNot;
        if (original != null) lines.Insert(2, $"Guerrilla's original: {original.Text}.");
        DetailsBox.Text = string.Join("\n", lines);
        // The pad's edge a little past where it fades out.
        if (info.FarthestMetres > 0)
            foreach (ComboBoxItem option in RangeList.Items)
                if (float.Parse((string)option.Tag) >= info.FarthestMetres * 1.1f || option == RangeList.Items[^1])
                {
                    RangeList.SelectedItem = option;
                    break;
                }
    }

    // The ids in a description turned into names where they're known.
    static string Named(string text) => Regex.Replace(text, @"\b[0-9A-F]{8}\b", m =>
    {
        uint id = uint.Parse(m.Value, System.Globalization.NumberStyles.HexNumber);
        return namesFound.TryGetValue(id, out var n) ? n : SoundEventNames.Known.TryGetValue(id, out var k) ? k : m.Value;
    });

    void OnEventDoubleClick(object sender, MouseButtonEventArgs e) => OnPlay(sender, e);

    void OnEventKey(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            OnPlay(sender, e);
            e.Handled = true;
        }
    }

    void OnPlay(object sender, RoutedEventArgs e)
    {
        if (EventList.SelectedItem is not EventItem item || bankId == 0) return;
        ApplyPlace();
        uint id = RfaAudio.Post(item.Id, SoundObject);
        if (id == 0) StatusText.Text = $"{item.Name} didn't post";
        else playing[id] = item.Id;
    }

    // What plays stops as the game stops it: each play's stop event, or (with none) a short fade.
    void OnStop(object sender, RoutedEventArgs e)
    {
        var posted = new HashSet<uint>();
        foreach (var (playingId, eventId) in playing.ToList())
        {
            if (!RfaAudio.IsPlaying(playingId))
            {
                playing.Remove(playingId);
                continue;
            }
            var stops = StopEventsOf(eventId);
            if (stops.Count == 0) RfaAudio.StopPlaying(playingId, 300);
            foreach (var s in stops)
                if (posted.Add(s.Event)) RfaAudio.Post(s.Event, SoundObject);
        }
        RfaAudio.StopObject(OriginalObject, 300);
    }

    void OnStopAll(object sender, RoutedEventArgs e)
    {
        RfaAudio.StopObject(0, 0);
        playing.Clear();
    }

    // ---- Guerrilla's original ---------------------------------------------------------------------------

    // What Guerrilla played for an event, and how to play it: its waves (decoded), each with its volume, pitch and loop.
    sealed record Track(GuerrillaSounds.Wave Wave, float VolumeDb, float PitchCents, bool Loops);
    sealed record Original(string Text, Func<List<Track>> Tracks);

    static float Db(float gain) => 20 * (float)Math.Log10(Math.Max(gain, 1e-5f));

    /// <summary>
    /// Guerrilla's original of an event: a voice's line (RFGV_ + persona + _ + line: its variations in turn, as Guerrilla's
    /// get_next_valid_variation plays them), a foley cue (RFG_ + the cue's name, after taking off what GuerrillaSoundBanks
    /// adds: _PLAYER, _NPC, _HELD, _120MS, _ALR, _MUSIC, _FAR), or a kiosk's line (RFG_ + a line of the KIOSK persona).
    /// </summary>
    Original? OriginalOf(string eventName, out string? whyNot)
    {
        whyNot = null;
        if (eventName.EndsWith("_STOP", StringComparison.OrdinalIgnoreCase))
        {
            whyNot = "A stop event plays nothing of its own";
            return null;
        }
        string? cache = GuerrillaCache(out whyNot);
        if (cache == null) return null;
        if (eventName.StartsWith("RFGV_", StringComparison.OrdinalIgnoreCase) && eventName.IndexOf('_', 5) is int split and > 5)
        {
            string persona = eventName[5..split], line = eventName[(split + 1)..];
            var personaVoices = Voices(cache, persona);
            var variations = personaVoices?.Variations(line) ?? [];
            if (personaVoices == null || variations.Count == 0)
            {
                whyNot = $"Guerrilla's persona {persona} has no line {line}";
                return null;
            }
            string key = persona + "/" + line;
            return new Original($"{persona}'s line {line} ({variations.Count} variation(s), in turn)", () =>
            {
                int turn = nextVariation.GetValueOrDefault(key);
                nextVariation[key] = (turn + 1) % variations.Count;
                var v = variations[turn % variations.Count];
                return [new Track(personaVoices.ReadWave(v), Db(v.Volume) + v.SoundDb, 0, false)];
            });
        }
        if (!eventName.StartsWith("RFG_", StringComparison.OrdinalIgnoreCase))
        {
            whyNot = "Only events made from Guerrilla's cues (RFG_) and voices (RFGV_) have an original here";
            return null;
        }
        var sounds = Sounds(cache, out whyNot);
        string name = eventName[4..];
        for (int i = 0; i < 5 && sounds != null; i++)
        {
            foreach (string candidate in new[] { name, name.ToLowerInvariant(), name.ToUpperInvariant() })
                if (sounds.FindCue(candidate) is { } cue)
                    return new Original($"Guerrilla's cue {cue.Name} (volume {Db(cue.Volume):0.#} dB, {(cue.LoopCount == 255 ? "loops" : "once")}, rank {cue.Rank}" +
                                        (cue.MostAtOnce > 0 ? $", at most {cue.MostAtOnce} at once" : "") + ")", () =>
                    {
                        var sound = sounds.SoundOf(cue);
                        return [.. new[] { sound }.Concat(sound.Also ?? []).Select(track =>
                        {
                            var (bank, index) = track.Picks ? track.Waves[random.Next(track.Waves.Count)] : track.Waves[0];
                            return new Track(sounds.ReadWave(bank, index), Db(cue.Volume) + track.VolumeDb, track.PitchCents, cue.LoopCount == 255);
                        })];
                    });
            var trimmed = Regex.Replace(name, @"_(PLAYER|NPC|HELD|ALR|MUSIC|FAR|\d+MS)$", "", RegexOptions.IgnoreCase);
            if (trimmed == name) break;
            name = trimmed;
        }
        // A kiosk's line: the KIOSK persona's (GuerrillaKiosks: RFG_ + the line).
        var kiosk = Voices(cache, "KIOSK");
        if (kiosk?.Find(eventName[4..]) is { } kioskLine)
            return new Original($"the kiosk's line {kioskLine.Name}", () => [new Track(kiosk.ReadWave(kioskLine), Db(kioskLine.Volume) + kioskLine.SoundDb, 0, false)]);
        whyNot ??= $"Guerrilla has no cue or kiosk line named {eventName[4..]}";
        return null;
    }
    readonly Dictionary<string, int> nextVariation = [];

    // Guerrilla's cache folder (GuerrillaDir in rfa_mods.ini, as the Guerrilla builder keeps it).
    string? GuerrillaCache(out string? whyNot)
    {
        whyNot = null;
        string? guerrilla = IniFile.Read(System.IO.Path.Combine(modFolder, "rfa_mods.ini"), "Launcher", "GuerrillaDir")?.Trim().Trim('"');
        if (string.IsNullOrEmpty(guerrilla))
        {
            whyNot = "Set GuerrillaDir under [Launcher] in rfa_mods.ini (the Guerrilla builder does) to play Guerrilla's originals";
            return null;
        }
        return System.IO.Path.Combine(guerrilla, "build", "pc", "cache");
    }

    static GuerrillaSounds? Sounds(string cache, out string? whyNot)
    {
        whyNot = null;
        if (guerrillaSounds != null && guerrillaSoundsFolder == cache) return guerrillaSounds;
        try
        {
            guerrillaSounds = new GuerrillaSounds(cache);
            guerrillaSoundsFolder = cache;
            return guerrillaSounds;
        }
        catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException)
        {
            whyNot = $"Guerrilla's sounds didn't open ({e.Message})";
            return null;
        }
    }

    // A persona of Guerrilla's voices, opened once (null if it isn't there).
    static GuerrillaVoices? Voices(string cache, string persona)
    {
        string key = cache + "|" + persona.ToUpperInvariant();
        if (personas.TryGetValue(key, out var known)) return known;
        try { return personas[key] = GuerrillaVoices.Open(cache, persona); }
        catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException) { return personas[key] = null; }
    }
    static readonly Dictionary<string, GuerrillaVoices?> personas = [];

    void OnPlayOriginal(object sender, RoutedEventArgs e)
    {
        if (EventList.SelectedItem is not EventItem item) return;
        var original = OriginalOf(item.Name, out _);
        if (original == null) return;
        try
        {
            var info = RfaAudio.GetEventInfo(item.Id);
            float bus = RfaAudio.BusVolume(BusOf(item.Id));
            ApplyPlace();
            RfaAudio.StopObject(OriginalObject, 50);
            int played = 0;
            bool loops = false;
            foreach (var t in original.Tracks())
            {
                loops |= t.Loops;
                if (RfaAudio.PlaySamples(t.Wave.Samples, t.Wave.Channels, t.Wave.Rate, t.Loops, t.Wave.LoopStart, t.VolumeDb + bus, t.PitchCents,
                                         info.ThreeD != 0 && t.Wave.Channels == 1, OriginalObject) != 0) played++;
            }
            StatusText.Text = played > 0 ? $"Playing {original.Text}{(loops ? ": loops until Stop" : "")}" : "Guerrilla's original didn't play";
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException or NotSupportedException)
        {
            StatusText.Text = $"Guerrilla's original can't be played: {ex.Message}";
        }
    }

    // ---- The pad --------------------------------------------------------------------------------------

    void OnRangeChosen(object sender, SelectionChangedEventArgs e)
    {
        if (RangeList.SelectedItem is ComboBoxItem { Tag: string metres }) range = float.Parse(metres);
        DrawPad();
    }

    void DrawPad()
    {
        if (Pad.ActualWidth <= 0) return;
        Pad.Children.Clear();
        double w = Pad.Width, h = Pad.Height, cx = w / 2, cy = h / 2;
        var dim = (Brush)FindResource("BorderBrush");
        var text = (Brush)FindResource("DimTextBrush");
        foreach (double part in new[] { 0.25, 0.5, 0.75, 1.0 })
        {
            double r = part * w / 2;
            var ring = new Ellipse { Width = 2 * r, Height = 2 * r, Stroke = dim, StrokeThickness = 1 };
            Canvas.SetLeft(ring, cx - r);
            Canvas.SetTop(ring, cy - r);
            Pad.Children.Add(ring);
            var label = new TextBlock { Text = $"{range * part:0.#} m", FontSize = 9, Foreground = text };
            Canvas.SetLeft(label, cx + 2);
            Canvas.SetTop(label, cy - r + 1);
            Pad.Children.Add(label);
        }
        Pad.Children.Add(new Line { X1 = 0, Y1 = cy, X2 = w, Y2 = cy, Stroke = dim });
        Pad.Children.Add(new Line { X1 = cx, Y1 = 0, X2 = cx, Y2 = h, Stroke = dim });
        foreach (var (words, x, y) in new[] { ("ahead", cx - 14, 2.0), ("left", 4.0, cy - 16), ("right", w - 30, cy - 16), ("behind", cx - 18, h - 16) })
        {
            var label = new TextBlock { Text = words, FontSize = 10, Foreground = text };
            Canvas.SetLeft(label, x);
            Canvas.SetTop(label, y);
            Pad.Children.Add(label);
        }
        // The listener: a triangle pointing ahead.
        Pad.Children.Add(new Polygon
        {
            Points = [new Point(cx, cy - 9), new Point(cx - 6, cy + 6), new Point(cx + 6, cy + 6)],
            Fill = (Brush)FindResource("TextBrush"),
        });
        soundLine = new Line { Stroke = (Brush)FindResource("AccentBrush"), StrokeThickness = 1, StrokeDashArray = [3, 3] };
        Pad.Children.Add(soundLine);
        soundMark = new Ellipse { Width = 14, Height = 14, Fill = (Brush)FindResource("AccentBrush"), Stroke = Brushes.White, StrokeThickness = 1.5 };
        Pad.Children.Add(soundMark);
        PlaceMark();
    }

    void PlaceMark()
    {
        if (soundMark == null || soundLine == null) return;
        double cx = Pad.Width / 2, cy = Pad.Height / 2, scale = Pad.Width / 2 / range;
        double x = cx + padX * scale, y = cy - padZ * scale;
        Canvas.SetLeft(soundMark, x - 7);
        Canvas.SetTop(soundMark, y - 7);
        soundLine.X1 = cx;
        soundLine.Y1 = cy;
        soundLine.X2 = x;
        soundLine.Y2 = y;
        float distance = MathF.Sqrt(padX * padX + padZ * padZ);
        PlaceText.Text = distance < 0.05f ? "At the listener" :
            $"{distance:0.0} m: {MathF.Abs(padX):0.0} m {(padX < 0 ? "left" : "right")}, {MathF.Abs(padZ):0.0} m {(padZ >= 0 ? "ahead" : "behind")}";
    }

    void MoveTo(Point p)
    {
        double cx = Pad.Width / 2, cy = Pad.Height / 2, scale = Pad.Width / 2 / range;
        padX = (float)Math.Clamp((p.X - cx) / scale, -range, range);
        padZ = (float)Math.Clamp((cy - p.Y) / scale, -range, range);
        ApplyPlace();
        PlaceMark();
    }

    void ApplyPlace()
    {
        if (!RfaAudio.IsOpen) return;
        RfaAudio.SetPosition(SoundObject, padX, 0, padZ);
        RfaAudio.SetPosition(OriginalObject, padX, 0, padZ);
    }

    void OnPadDown(object sender, MouseButtonEventArgs e)
    {
        dragging = true;
        Pad.CaptureMouse();
        MoveTo(e.GetPosition(Pad));
    }

    void OnPadMove(object sender, MouseEventArgs e)
    {
        if (dragging) MoveTo(e.GetPosition(Pad));
    }

    void OnPadUp(object sender, MouseButtonEventArgs e)
    {
        dragging = false;
        Pad.ReleaseMouseCapture();
    }

    void OnPadCentre(object sender, MouseButtonEventArgs e)
    {
        padX = padZ = 0;
        ApplyPlace();
        PlaceMark();
    }

    // ---- What is playing --------------------------------------------------------------------------------

    void OnTimer()
    {
        if (!RfaAudio.IsOpen || bankId == 0) return;
        // Sweeps: from one end to the other in 4 s, and back.
        foreach (var (slider, sweep, min, max) in sliders)
        {
            if (sweep.IsChecked != true) continue;
            double step = (max - min) * timer.Interval.TotalSeconds / 4;
            bool up = sweep.Tag is not false;
            double next = slider.Value + (up ? step : -step);
            if (next >= max) { next = max; sweep.Tag = false; }
            else if (next <= min) { next = min; sweep.Tag = true; }
            slider.Value = next;
        }
        foreach (uint id in playing.Keys.ToList())
            if (!RfaAudio.IsPlaying(id)) playing.Remove(id);
        var rows = new List<VoiceRow>();
        foreach (var v in RfaAudio.Voices().Where(v => v.GameObject is SoundObject or OriginalObject))
        {
            string what = v.Codec == 3 ? "Guerrilla's original" : NameOf(v.EventId) + (v.SoundId != 0 ? $" / {v.SoundId:X8}" : "");
            string codec = v.Codec switch { 1 => "PCM", 2 => "ADPCM", 3 => "XACT", 4 => "Vorbis", _ => "?" };
            string state = (v.IsVirtual != 0 ? "virtual" : v.Streamed != 0 ? "streamed" : "whole") + (v.ThreeD != 0 ? ", 3D" : ", 2D");
            rows.Add(new VoiceRow(what, codec, $"{v.Channels} ch {v.Rate / 1000.0:0.#} kHz", v.Loops != 0 ? "yes" : "",
                                  v.LevelDb < -200 ? "silent" : $"{v.LevelDb:0.0} dB", $"{v.Distance:0.0} m",
                                  $"{v.PositionSeconds:0.00} / {v.LengthSeconds:0.00}", state));
        }
        VoiceList.ItemsSource = rows;
    }
}
