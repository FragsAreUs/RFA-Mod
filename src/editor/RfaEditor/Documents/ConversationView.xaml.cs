using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;
using RfaFormats;
using RfaFormats.Audio;

namespace RfaEditor;

/// <summary>
/// A conversation (.ctdg_pc): who speaks, and which voice line each says, in order, with the pause before each line,
/// named from the game's own tables (<see cref="VoiceNames"/>). Its lines play as the game plays them: each persona's
/// voice bank is read in place from voices.vpp_pc, the line is found in the persona's dialogue event (persona, line), and
/// its node plays alone on rfa_audio's engine (src\audio\). Each speaker stands at a place of their own in front of you.
/// </summary>
public partial class ConversationView : UserControl
{
    const ulong FirstSpeakerObject = 100;  // the sound preview uses 1 and 2

    // A line as listed. Node: what plays it (0: not in its persona's bank).
    sealed class Row : INotifyPropertyChanged
    {
        public required int Index { get; init; }
        public required ConversationFile.Line Line { get; init; }
        public required string Speaker { get; init; }
        public required string LineName { get; init; }
        public required string? FolderName { get; init; }
        public uint Node { get; set; }
        public string State { get; set; } = "";
        public uint PlayingId { get; set; }

        public string Playing { get; private set; } = "";
        public string Number => (Index + 1).ToString();
        public string Pause => Line.PauseMs == 0 ? "" : Line.PauseMs < 0 ? $"{-Line.PauseMs} ms early" : $"{Line.PauseMs} ms";
        public string Folder => FolderName ?? "";

        public event PropertyChangedEventHandler? PropertyChanged;

        /// <summary>Marks the line while its play is heard.</summary>
        public void ShowPlaying()
        {
            string now = PlayingId != 0 && RfaAudio.IsOpen && RfaAudio.IsPlaying(PlayingId) ? "▶" : "";
            if (now == Playing) return;
            Playing = now;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Playing)));
        }
    }

    sealed record Person(string Name, int Lines, string Bank);

    readonly Action<string> log;
    readonly DispatcherTimer timer;
    readonly Dictionary<uint, uint> banks = [];   // persona → its voice bank, loaded by us
    readonly Dictionary<uint, ulong> speakers = []; // persona → its game object
    List<Row> rows = [];
    ConversationFile? conversation;
    uint initBankId;
    static bool closesOnExit;

    // Playing: the next line to start (-1: none), when (null: when the one playing allows), and whether it goes on.
    int next = -1;
    DateTime? startAt;
    Row? current;
    bool wholeConversation;

    public ConversationView(Action<string> log)
    {
        InitializeComponent();
        this.log = log;
        timer = new DispatcherTimer(DispatcherPriority.Background) { Interval = TimeSpan.FromMilliseconds(25) };
        timer.Tick += (_, _) => OnTimer();
    }

    // ---- Showing a conversation -------------------------------------------------------------------

    /// <summary>
    /// Shows a conversation. <paramref name="voicesArchive"/>: voices.vpp_pc (uncompressed, so its banks are read in
    /// place), or null if it isn't there; <paramref name="initBank"/>: a copy of init.bnk_pc, for its buses' volumes.
    /// </summary>
    public void Show(ConversationFile file, string fileName, VoiceNames names, string? voicesArchive, Func<string, bool> voicesHas, string? initBank)
    {
        Hide();
        conversation = file;
        rows = [.. file.Lines.Select((line, i) => new Row
        {
            Index = i,
            Line = line,
            Speaker = names.PersonaName(line.Persona),
            LineName = names.LineName(line.LineId),
            FolderName = names.Line(line.LineId)?.Folder,
        })];

        // Each persona's bank: <persona>.bnk_pc in voices.vpp_pc, its lines found in its dialogue event.
        string engineProblem = "";
        bool engine = voicesArchive != null && OpenEngine(out engineProblem);
        if (engine && initBank != null && initBankId == 0)
        {
            try { initBankId = RfaAudio.LoadBank(initBank, null); }
            catch (InvalidDataException e) { log($"Conversation: Armageddon's init.bnk_pc didn't load ({e.Message}): buses count as 0 dB"); }
        }
        var bankText = new Dictionary<uint, string>();
        var nodes = new Dictionary<(uint, uint), uint>();
        foreach (uint persona in file.Personas)
        {
            string? name = names.Persona(persona)?.Name;
            string bankName = (name ?? persona.ToString("X8")).ToLowerInvariant();
            if (name == null || !voicesHas(bankName + ".bnk_pc"))
            {
                bankText[persona] = name == null ? "unknown persona: no voice bank found by its id" : $"no {bankName}.bnk_pc in voices.vpp_pc";
                continue;
            }
            if (!engine)
            {
                bankText[persona] = $"{bankName}.bnk_pc in voices.vpp_pc" + (voicesArchive == null ? "" : $" (not loaded: {engineProblem})");
                continue;
            }
            try
            {
                uint bank = RfaAudio.LoadBankFromArchive(voicesArchive!, bankName);
                banks[persona] = bank;
                foreach (var (keys, node) in RfaAudio.BankDialogueEvents(bank).SelectMany(RfaAudio.DialogueLines))
                    if (keys.Length == 2) nodes.TryAdd((keys[0], keys[1]), node);
                bankText[persona] = $"{bankName}.bnk_pc in voices.vpp_pc";
            }
            catch (InvalidDataException e)
            {
                bankText[persona] = $"{bankName}.bnk_pc can't be read: {e.Message}";
            }
        }
        int speakerCount = file.Personas.Count;
        for (int i = 0; i < speakerCount; i++)
        {
            uint persona = file.Personas[i];
            speakers[persona] = FirstSpeakerObject + (ulong)i;
            // Side by side, 3 m apart, 2 m in front of you: so you hear who is who.
            if (engine) RfaAudio.SetPosition(speakers[persona], (float)((i - (speakerCount - 1) / 2.0) * 3.0), 0, 2);
        }

        foreach (Row row in rows)
        {
            if (nodes.TryGetValue((row.Line.Persona, row.Line.LineId), out uint node))
            {
                row.Node = node;
                row.State = "plays";
            }
            else
                row.State = banks.ContainsKey(row.Line.Persona) ? $"not in {row.Speaker}'s bank" : "not played here (no bank)";
        }

        PeopleList.ItemsSource = file.Personas
            .Select(p => new Person(names.PersonaName(p), file.Lines.Count(l => l.Persona == p), bankText.GetValueOrDefault(p, "")))
            .ToList();
        LineList.ItemsSource = rows;
        if (rows.Count > 0) LineList.SelectedIndex = 0;

        int missing = rows.Count(r => r.Node == 0);
        string verdict = !file.GameTakesIt
            ? $"The game won't play it: it takes 1 to {ConversationFile.MaxLines} lines."
            : missing > 0 && banks.Count == file.Personas.Count
                ? $"The game won't play it: {missing} of its lines aren't in their speaker's voice bank (it checks every line first)."
                : "";
        StatusText.Text = !engine && voicesArchive != null ? engineProblem
                        : voicesArchive == null ? "voices.vpp_pc isn't in the game folder (or is compressed): the lines can't play here."
                        : verdict;
        DetailsBox.Text = $"Conversation \"{file.Name}\" ({fileName}): {rows.Count} lines, {file.Personas.Count} speakers." +
                          (verdict.Length > 0 ? "\n" + verdict : "") +
                          "\nPick a line to see what it plays. A line's pause comes before it; a negative one starts it before the line before " +
                          "it ends.";
    }

    /// <summary>Stops the lines and lets the banks go (when another file is shown).</summary>
    public void Hide()
    {
        StopPlaying();
        if (RfaAudio.IsOpen)
        {
            foreach (ulong speaker in speakers.Values) RfaAudio.RemoveObject(speaker);
            foreach (uint bank in banks.Values) RfaAudio.UnloadBank(bank);
        }
        speakers.Clear();
        banks.Clear();
        rows = [];
        conversation = null;
        LineList.ItemsSource = null;
        PeopleList.ItemsSource = null;
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

    // ---- A line ---------------------------------------------------------------------------------------

    void OnLineChosen(object sender, SelectionChangedEventArgs e)
    {
        if (LineList.SelectedItem is not Row row || conversation == null) return;
        var text = new List<string>
        {
            $"Line {row.Index + 1} of {rows.Count}: {row.Speaker} ({row.Line.Persona:X8}) says {row.LineName} ({row.Line.LineId:X8})",
            row.FolderName != null ? $"Folder: {row.FolderName} (audio_line_tags.xtbl)" : "Not in audio_line_tags.xtbl: shown by its id.",
            row.Line.PauseMs switch
            {
                0 => "Starts as soon as the line before it ends.",
                < 0 => $"Starts {-row.Line.PauseMs} ms before the line before it ends.",
                _ => $"Starts {row.Line.PauseMs} ms after the line before it ends.",
            },
        };
        if (row.Node != 0)
            text.Add($"\nWhat plays it (node {row.Node:X8}):\n" + RfaAudio.Describe(row.Node));
        else
            text.Add("\n" + row.State);
        DetailsBox.Text = string.Join("\n", text);
    }

    void OnLineDoubleClick(object sender, MouseButtonEventArgs e) => OnPlayLine(sender, e);

    void OnLineKey(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.Enter) return;
        OnPlayLine(sender, e);
        e.Handled = true;
    }

    void OnPlayLine(object sender, RoutedEventArgs e)
    {
        if (LineList.SelectedItem is Row row) PlayFrom(row.Index, whole: false);
    }

    void OnPlayAll(object sender, RoutedEventArgs e)
    {
        if (rows.Count > 0) PlayFrom(LineList.SelectedItem is Row row ? row.Index : 0, whole: true);
    }

    void OnStop(object sender, RoutedEventArgs e)
    {
        StopPlaying();
        StatusText.Text = "Stopped.";
    }

    // ---- Playing, line after line ---------------------------------------------------------------------

    void PlayFrom(int index, bool whole)
    {
        StopPlaying();
        if (!RfaAudio.IsOpen) return;
        wholeConversation = whole;
        next = index;
        startAt = DateTime.UtcNow.AddMilliseconds(whole ? Math.Max(0, rows[index].Line.PauseMs) : 0);
        timer.Start();
    }

    void StopPlaying()
    {
        timer.Stop();
        next = -1;
        startAt = null;
        current = null;
        if (RfaAudio.IsOpen)
            foreach (ulong speaker in speakers.Values) RfaAudio.StopObject(speaker, 50);
        foreach (Row row in rows)
        {
            row.PlayingId = 0;
            row.ShowPlaying();
        }
    }

    void Start(Row row)
    {
        row.PlayingId = row.Node != 0 ? RfaAudio.PlayNode(row.Node, speakers[row.Line.Persona]) : 0;
        current = row;
        next = wholeConversation && row.Index + 1 < rows.Count ? row.Index + 1 : -1;
        startAt = null;
        StatusText.Text = $"Line {row.Index + 1} of {rows.Count}: {row.Speaker}, {row.LineName}" +
                          (row.PlayingId == 0 ? $" (doesn't play: {row.State})" : "");
        LineList.SelectedItem = row;
        LineList.ScrollIntoView(row);
    }

    void OnTimer()
    {
        if (!RfaAudio.IsOpen)
        {
            timer.Stop();
            return;
        }
        DateTime now = DateTime.UtcNow;
        if (next >= 0 && startAt is DateTime when && now >= when)
            Start(rows[next]);
        else if (next >= 0 && startAt == null && current != null)
        {
            int pause = rows[next].Line.PauseMs;
            bool stillPlaying = current.PlayingId != 0 && RfaAudio.IsPlaying(current.PlayingId);
            if (!stillPlaying)
                startAt = now.AddMilliseconds(Math.Max(0, pause));
            else if (pause < 0 && RemainingSeconds(current.PlayingId) is double left && left * 1000 <= -pause)
                startAt = now;
        }
        foreach (Row row in rows) row.ShowPlaying();
        if (next < 0 && rows.All(r => r.PlayingId == 0 || !RfaAudio.IsPlaying(r.PlayingId)))
        {
            timer.Stop();
            if (current != null) StatusText.Text = wholeConversation ? "The conversation has ended." : "The line has ended.";
            current = null;
        }
    }

    // How long a play has left, from its voice (null if it has none now).
    static double? RemainingSeconds(uint playingId)
    {
        foreach (var voice in RfaAudio.Voices())
            if (voice.PlayingId == playingId && voice.LengthSeconds > 0) return voice.LengthSeconds - voice.PositionSeconds;
        return null;
    }
}
