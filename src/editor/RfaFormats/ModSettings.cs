using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace RfaFormats;

/// <summary>What a setting's value is, so a window can give it a fitting control.</summary>
public enum SettingKind
{
    /// <summary>Anything: a name, a list, a key.</summary>
    Text,
    /// <summary>1 = on, 0 = off.</summary>
    Toggle,
    /// <summary>A whole or decimal number.</summary>
    Number,
    /// <summary>A folder on your PC (GameDir, GuerrillaDir).</summary>
    Folder,
}

/// <summary>When a changed setting reaches the game, as its comments say.</summary>
public enum SettingTiming
{
    /// <summary>The comments don't say.</summary>
    Unknown,
    /// <summary>Marked (live): it changes in the running game as soon as the file is saved.</summary>
    Live,
    /// <summary>Read when the game starts: start it again for a change to show.</summary>
    GameStart,
    /// <summary>Read when a level or zone loads: load it again.</summary>
    LevelLoad,
}

/// <summary>A value the comments name, with what it means ("0 = the game's own").</summary>
public sealed record SettingChoice(string Value, string Meaning);

/// <summary>One setting of rfa_mods.ini, with what its comments say about it.</summary>
public sealed class ModSetting
{
    public required string Section { get; init; }
    public required string Key { get; init; }
    /// <summary>Your value: in your file, or this version's default when your file doesn't have the setting yet.</summary>
    public required string Value { get; init; }
    /// <summary>This version's value (defaults\rfa_mods.ini); null for a setting only your file has.</summary>
    public string? Default { get; init; }
    /// <summary>False when your file doesn't have it yet (the launcher adds it when it next starts).</summary>
    public bool InYourFile { get; init; }
    /// <summary>The comments above it, as paragraphs (each a line of its own).</summary>
    public string Description { get; init; } = "";
    /// <summary>The setting just above whose comments this one shares (Console's InputScale shares TextScale's), or null.</summary>
    public string? SharesDescriptionOf { get; init; }
    /// <summary>The "; --- Sky" heading it is under in its section, or null.</summary>
    public string? Group { get; init; }
    public SettingKind Kind { get; init; }
    public SettingTiming Timing { get; init; }
    /// <summary>The comments' words about when it takes effect ("Read when the game starts."), or null.</summary>
    public string? TimingNote { get; init; }
    /// <summary>Values the comments name, for a list to pick from (you may still type another).</summary>
    public IReadOnlyList<SettingChoice> Choices { get; init; } = [];
    /// <summary>The range the comments give ("30-170"), or null.</summary>
    public (double Min, double Max)? Range { get; init; }

    /// <summary>True when the setting is this version's but your file doesn't have it, or the other way round.</summary>
    public bool HasDefault => Default != null;

    /// <summary>True if <paramref name="value"/> is this version's default (numbers by value, words in any case).</summary>
    public bool IsDefault(string value) => Default != null && SameValue(Kind, value, Default);

    /// <summary>True if the two values mean the same: numbers by value ("1.50" = "1.5"), the rest in any case.</summary>
    public static bool SameValue(SettingKind kind, string a, string b)
    {
        a = a.Trim();
        b = b.Trim();
        if (kind is SettingKind.Number or SettingKind.Toggle && ModSettings.TryNumber(a, out double x) && ModSettings.TryNumber(b, out double y))
            return x == y;
        if (kind == SettingKind.Folder) a = a.TrimEnd('\\', '/');
        if (kind == SettingKind.Folder) b = b.TrimEnd('\\', '/');
        return string.Equals(a, b, StringComparison.OrdinalIgnoreCase);
    }

    public override string ToString() => $"[{Section}] {Key}={Value}";
}

/// <summary>A [section] of rfa_mods.ini.</summary>
public sealed class ModSettingsSection
{
    public required string Name { get; init; }
    /// <summary>The title in the banner above it ("Where your games are, and how the game is started"), or "".</summary>
    public string Title { get; init; } = "";
    /// <summary>False for a section only your file has.</summary>
    public bool InDefaults { get; init; } = true;
    public List<ModSetting> Settings { get; } = [];
    public override string ToString() => $"[{Name}]";
}

/// <summary>What <see cref="ModSettings.Save"/> did.</summary>
public sealed class ModSettingsSaveResult
{
    /// <summary>Settings whose value was set (or added, when your file didn't have them yet).</summary>
    public List<string> Written { get; } = [];
    /// <summary>
    /// Settings that something else (the game's own menus, say) changed in the file after it was read;
    /// those you didn't change keep that value.
    /// </summary>
    public List<string> ChangedMeanwhile { get; } = [];
}

/// <summary>
/// rfa_mods.ini for a settings window: your file's settings, each with the comments that explain it,
/// its default and a fitting kind of control, all from the comment lines in this version's template
/// (defaults\rfa_mods.ini), so a new setting shows with no code of its own. Nothing is kept here but
/// what was read; <see cref="Save"/> reads the file again and changes only the values given, so the
/// file's comments, order, line breaks, settings this version doesn't know, and values the game
/// wrote meanwhile all stay exactly as they are, byte for byte.
///
/// The rules the comments follow (the template's top says them too): a setting's comments are the
/// lines starting with ; just above it; "; --- Name" heads a group; the lines between two "; ----"
/// rules above a [section] are its title; "1 = ..." and "0 = ..." say what a switch's values do;
/// "(live)" marks a setting the game reads again as the file is saved.
/// </summary>
public sealed class ModSettings
{
    /// <summary>Your file (bin\rfa_mods.ini).</summary>
    public string Path { get; }
    /// <summary>This version's template (defaults\rfa_mods.ini), or null if there is none.</summary>
    public string? TemplatePath { get; }
    public List<ModSettingsSection> Sections { get; } = [];
    /// <summary>False when your file is missing: saving makes it from the template.</summary>
    public bool FileExists { get; }

    ModSettings(string path, string? templatePath, bool fileExists)
    {
        Path = path;
        TemplatePath = templatePath;
        FileExists = fileExists;
    }

    public IEnumerable<ModSetting> All => Sections.SelectMany(section => section.Settings);

    /// <summary>
    /// The mod folder's rfa_mods.ini, with defaults\rfa_mods.ini as the template when there is one.
    /// Throws (IOException, with a message for a person) when a file can't be read.
    /// </summary>
    public static ModSettings Load(string path, string? templatePath)
    {
        if (templatePath != null && !File.Exists(templatePath)) templatePath = null;
        bool exists = File.Exists(path);
        if (!exists && templatePath == null) throw new IOException($"There is no {path}, and no defaults\\rfa_mods.ini to make it from.");
        IniText yours = IniText.Read(exists ? path : templatePath!);
        IniText? template = templatePath != null ? IniText.Read(templatePath) : null;

        var settings = new ModSettings(path, templatePath, exists);
        List<Entry> yourEntries = Entries(yours.Lines);
        List<Entry> templateEntries = template != null ? Entries(template.Lines) : yourEntries;
        var yourValues = FirstOfEach(yourEntries);
        var used = new HashSet<Entry>();

        // This version's sections and settings in the template's order, with your values; your own
        // settings it doesn't have after its own, in their section; your own sections at the end.
        foreach (var group in templateEntries.GroupBy(entry => entry.Section, StringComparer.OrdinalIgnoreCase))
        {
            Entry first = group.First();
            var section = new ModSettingsSection { Name = first.Section, Title = first.SectionTitle };
            foreach (Entry entry in group)
            {
                if (entry.Key.Length == 0) continue;
                yourValues.TryGetValue(Id(entry), out Entry? mine);
                if (mine != null) used.Add(mine);
                if (section.Settings.Any(s => s.Key.Equals(entry.Key, StringComparison.OrdinalIgnoreCase))) continue;
                section.Settings.Add(Describe(entry, mine?.Value ?? entry.Value, template != null ? entry.Value : null, mine != null || template == null));
            }
            if (template != null)
            {
                foreach (Entry mine in yourEntries.Where(e => e.Key.Length > 0 && !used.Contains(e) && e.Section.Equals(section.Name, StringComparison.OrdinalIgnoreCase)))
                {
                    if (!yourValues.TryGetValue(Id(mine), out Entry? firstOfYours) || firstOfYours != mine) continue;  // a second copy: the game reads the first
                    used.Add(mine);
                    section.Settings.Add(Describe(mine, mine.Value, null, true));
                }
            }
            settings.Sections.Add(section);
        }
        if (template != null)
        {
            foreach (var group in yourEntries.Where(e => e.Key.Length > 0 && !used.Contains(e))
                         .GroupBy(entry => entry.Section, StringComparer.OrdinalIgnoreCase))
            {
                if (settings.Sections.Any(s => s.Name.Equals(group.Key, StringComparison.OrdinalIgnoreCase))) continue;
                var section = new ModSettingsSection { Name = group.First().Section, Title = group.First().SectionTitle, InDefaults = false };
                foreach (Entry mine in group)
                {
                    if (!yourValues.TryGetValue(Id(mine), out Entry? firstOfYours) || firstOfYours != mine) continue;
                    section.Settings.Add(Describe(mine, mine.Value, null, true));
                }
                settings.Sections.Add(section);
            }
        }
        settings.Sections.RemoveAll(section => section.Settings.Count == 0);
        return settings;
    }

    /// <summary>
    /// Sets the values given (setting → its new value) in the file as it is on disk now: each one's
    /// line keeps everything before its value, every other line stays byte for byte. A setting your
    /// file doesn't have yet is added after the last one of its section, with its comments from the
    /// template (a section it doesn't have, at the end, with its title). The new file is read back and
    /// checked against what was meant before it replaces yours; if anything in it is off, yours stays.
    /// </summary>
    public ModSettingsSaveResult Save(IReadOnlyDictionary<ModSetting, string> changes)
    {
        var result = new ModSettingsSaveResult();
        if (changes.Count == 0) return result;
        bool exists = File.Exists(Path);
        if (!exists && TemplatePath == null) throw new IOException($"There is no {Path}, and no defaults\\rfa_mods.ini to make it from.");
        IniText file = IniText.Read(exists ? Path : TemplatePath!);
        IniText? template = TemplatePath != null ? IniText.Read(TemplatePath) : null;

        // What the file has now, against what was shown: what changed meanwhile (the game's menus write too).
        var now = FirstOfEach(Entries(file.Lines));
        foreach (ModSetting setting in All)
        {
            bool there = now.TryGetValue(Id(setting.Section, setting.Key), out Entry? entry);
            if (setting.InYourFile && (!there || entry!.Value != setting.Value) && !changes.ContainsKey(setting))
                result.ChangedMeanwhile.Add($"[{setting.Section}] {setting.Key}" + (there ? $" is now {entry!.Value}" : " was taken out"));
        }

        var lines = new List<IniLine>(file.Lines);
        string lineBreak = file.Lines.Any(line => line.Ending == "\n") && !file.Lines.Any(line => line.Ending == "\r\n") ? "\n" : "\r\n";
        var expected = now.ToDictionary(pair => pair.Key, pair => pair.Value.Value, StringComparer.OrdinalIgnoreCase);
        foreach (var (setting, raw) in changes)
        {
            string value = Clean(raw);
            expected[Id(setting.Section, setting.Key)] = value;
            if (now.TryGetValue(Id(setting.Section, setting.Key), out Entry? entry))
            {
                lines[entry.Line] = lines[entry.Line].WithValue(value, file.Encoding);
            }
            else
            {
                Insert(lines, setting, value, template, file.Encoding, lineBreak);
            }
            result.Written.Add($"[{setting.Section}] {setting.Key}={value}");
        }

        // Read back what is about to be written: every setting must have the value meant, and nothing else may differ.
        byte[] bytes = IniText.Bytes(file.Mark, lines);
        IniText check = IniText.Parse(bytes);
        var after = FirstOfEach(Entries(check.Lines));
        foreach (var (id, value) in expected)
            if (!after.TryGetValue(id, out Entry? entry) || entry.Value != value)
                throw new IOException($"Saving would have changed {id.Replace('\u0001', ' ')} to \"{entry?.Value}\" instead of \"{value}\", so the file was left as it was.");
        if (after.Count != expected.Count)
            throw new IOException("Saving would have changed which settings the file has, so it was left as it was.");

        // Write next to it first, so yours is only replaced once all of it is there.
        string fresh = Path + ".new";
        File.WriteAllBytes(fresh, bytes);
        File.Move(fresh, Path, overwrite: true);
        return result;
    }

    // ---- Reading the comments ---------------------------------------------------------------------

    /// <summary>A setting as written: where, its value, and the comments above it.</summary>
    sealed class Entry
    {
        public string Section = "";
        public string SectionTitle = "";
        public string Key = "";       // "" for a section without settings
        public string Value = "";
        public int Line;
        public List<string> Comments = [];       // the comment lines just above it, as written
        public string? Group;
        public List<string> SectionIntro = [];   // the section's first comments (they may say how all of it is read)
        public Entry? SharesWith;                // the setting above whose comments it shares (none of its own, right under it)
    }

    static string Id(Entry entry) => Id(entry.Section, entry.Key);
    static string Id(string section, string key) => section.ToLowerInvariant() + "\u0001" + key.ToLowerInvariant();

    /// <summary>The first of each setting (the game reads the first; a section written twice is one).</summary>
    static Dictionary<string, Entry> FirstOfEach(List<Entry> entries)
    {
        var first = new Dictionary<string, Entry>(StringComparer.OrdinalIgnoreCase);
        foreach (Entry entry in entries)
            if (entry.Key.Length > 0) first.TryAdd(Id(entry), entry);
        return first;
    }

    static bool IsRule(string comment)
    {
        string text = comment.Trim();
        return text.Length >= 5 && text.All(c => c is '-' or '=');
    }

    static string? Heading(string comment)
    {
        string text = comment.Trim();
        return text.StartsWith("--- ", StringComparison.Ordinal) && !IsRule(text) ? text[4..].Trim() : null;
    }

    /// <summary>Each setting of the file with its comments, and an entry for each section with none.</summary>
    static List<Entry> Entries(List<IniLine> lines)
    {
        var entries = new List<Entry>();
        string section = "", title = "";
        string? group = null;
        bool sectionHasSettings = true;
        List<string> intro = [];
        var comments = new List<string>();
        bool afterHeader = false;
        for (int i = 0; i < lines.Count; i++)
        {
            IniLine line = lines[i];
            switch (line.Kind)
            {
                case IniLineKind.Blank:
                    comments.Clear();
                    afterHeader = false;
                    break;
                case IniLineKind.Comment:
                    if (Heading(line.Comment) is string heading)
                    {
                        group = heading;
                        comments.Clear();
                    }
                    else comments.Add(line.Comment);
                    break;
                case IniLineKind.Section:
                    if (!sectionHasSettings) entries.Add(new Entry { Section = section, SectionTitle = title });
                    section = line.Name;
                    title = string.Join(" ", comments.Where(c => !IsRule(c)).Select(c => c.Trim())).Trim();
                    group = null;
                    intro = [];
                    sectionHasSettings = false;
                    comments.Clear();
                    afterHeader = true;
                    break;
                case IniLineKind.Setting:
                    if (afterHeader) intro = [.. comments];
                    afterHeader = false;
                    sectionHasSettings = true;
                    var entry = new Entry
                    {
                        Section = section, SectionTitle = title, Key = line.Name, Value = line.Value, Line = i,
                        Comments = [.. comments.Where(c => !IsRule(c))], Group = group, SectionIntro = intro,
                    };
                    // Right under another setting with no comments of its own: it shares that one's (TextScale,
                    // InputScale), or the one that one shares (MapZoomIn, MapZoomOut, MapCentre).
                    if (entry.Comments.Count == 0 && entries.Count > 0 && entries[^1].Line == i - 1 && entries[^1].Key.Length > 0)
                    {
                        Entry above = entries[^1].SharesWith ?? entries[^1];
                        if (above.Comments.Count > 0) entry.SharesWith = above;
                    }
                    entries.Add(entry);
                    comments.Clear();
                    break;
            }
        }
        if (!sectionHasSettings) entries.Add(new Entry { Section = section, SectionTitle = title });
        return entries;
    }

    static ModSetting Describe(Entry entry, string value, string? defaultValue, bool inYourFile)
    {
        Entry described = entry.SharesWith ?? entry;
        string? shares = entry.SharesWith?.Key;
        string description = Paragraphs(described.Comments);
        string joined = description.Replace('\n', ' ');
        string basis = defaultValue ?? value;
        SettingKind kind = joined.Length == 0 ? SettingKind.Text : KindOf(entry.Key, basis, joined);
        var (timing, note) = TimingOf(description, entry.Group, string.Join(" ", entry.SectionIntro.Select(c => c.Trim())));
        return new ModSetting
        {
            Section = entry.Section, Key = entry.Key, Value = value, Default = defaultValue, InYourFile = inYourFile,
            Description = description, SharesDescriptionOf = shares, Group = entry.Group,
            Kind = kind, Timing = timing, TimingNote = note,
            Choices = kind switch
            {
                SettingKind.Toggle => [],
                SettingKind.Number => ChoicesIn(joined),
                _ => ChoicesIn(joined).Where(choice => choice.Value.Length == 0).ToList(),  // a name's list: only "empty = ..."
            },
            Range = kind == SettingKind.Number ? RangeIn(joined, basis) : null,
        };
    }

    /// <summary>
    /// The comment lines as paragraphs: "1 = ...", "0 = ..." and examples ("   GameDir=D:\...") start
    /// their own, lines indented under one carry it on, the rest run together.
    /// </summary>
    static string Paragraphs(List<string> comments)
    {
        var paragraphs = new List<string>();
        var current = new StringBuilder();
        bool example = false;
        foreach (string comment in comments)
        {
            string text = comment.Trim();
            if (text.Length == 0) continue;
            int indent = comment.Length - comment.TrimStart().Length;
            bool isExample = indent >= 3 && Regex.IsMatch(text, @"^[A-Za-z_][\w.]*=\S");
            bool startsItem = Regex.IsMatch(text, @"^(-?\d+(\.\d+)?\s?(=|:)\s|\d+ [a-z]|[Ee]mpty\s?=\s|With [^:]+: 1 = |\(live\))");
            if (current.Length > 0 && (isExample || example || startsItem))
            {
                paragraphs.Add(current.ToString());
                current.Clear();
            }
            if (current.Length > 0) current.Append(' ');
            current.Append(text);
            example = isExample;
        }
        if (current.Length > 0) paragraphs.Add(current.ToString());
        return string.Join("\n", paragraphs);
    }

    static readonly Regex Multiplier = new(@"times this|times the |as drawn now|higher (is )?(brighter|stronger)", RegexOptions.IgnoreCase);
    static readonly Regex Scale = new(
        @"\bhow (much|many|far|bright|strong|faint|thick|fast|long|big|often|dark|wide|loud|high)\b|\b\d+(\.\d+)?\s?-\s?\d+\b|\b1, 2\b|" +
        @"\bin (metres|meters|seconds|percent|degrees|minutes)\b|^(megabytes|seconds|lines)\b", RegexOptions.IgnoreCase);
    static readonly Regex CountsOn = new(@"\b1, 2\b");
    static readonly Regex SwitchWords = new(@"(^|\s|\(live\)\s)(1|0)\s?(=|:)\s|(^|\s)1 [a-z]+s\b", RegexOptions.IgnoreCase);

    static SettingKind KindOf(string key, string value, string description)
    {
        string v = value.Trim();
        if (key.EndsWith("Dir", StringComparison.OrdinalIgnoreCase) && description.Contains("folder", StringComparison.OrdinalIgnoreCase))
            return SettingKind.Folder;
        if (v is "0" or "1")
        {
            // A 0 or 1 is a switch unless the comments make it a number: a multiplier ("times this") or a
            // scale ("how far", "0-100", "1, 2, ...") without "1 = ..." words for a switch.
            if (Multiplier.IsMatch(description) || CountsOn.IsMatch(description)) return SettingKind.Number;
            bool scale = Scale.IsMatch(description);
            if (!scale || SwitchWords.IsMatch(description)) return SettingKind.Toggle;
            return SettingKind.Number;
        }
        return TryNumber(v, out _) ? SettingKind.Number : SettingKind.Text;
    }

    static readonly Regex LiveWords = new(@"whenever you save this file", RegexOptions.IgnoreCase);
    static readonly Regex StartWords = new(@"read (when the game starts|at start-?up|once,? when the game starts)|when the game starts|start the game again|restart the game|after a restart|while starting", RegexOptions.IgnoreCase);
    static readonly Regex LoadWords = new(@"load the (level|zone) again|the next time he is made|when (a|the) (level|zone|world|HUD document) loads", RegexOptions.IgnoreCase);
    static readonly Regex SectionStartWords = new(@"the rest (are|is) read when the game starts|(all|these|they) are read when the game starts", RegexOptions.IgnoreCase);

    /// <summary>
    /// When the comments say the setting reaches the game, and their sentence saying so. "(live)" counts
    /// at the start of a paragraph only (graphics= under [Mods] speaks of "the (live) settings" of another section).
    /// </summary>
    static (SettingTiming, string?) TimingOf(string paragraphs, string? group, string sectionIntro)
    {
        string description = paragraphs.Replace('\n', ' ');
        if (Regex.IsMatch(paragraphs, @"(^|\n)\(live\)", RegexOptions.IgnoreCase))
            return (SettingTiming.Live, "Marked (live): the running game takes a change as soon as you save.");
        if (Sentence(description, LiveWords) is string live) return (SettingTiming.Live, live);
        if (Sentence(description, StartWords) is string start) return (SettingTiming.GameStart, start);
        if (Sentence(description, LoadWords) is string load) return (SettingTiming.LevelLoad, load);
        if (group != null && Sentence(group, LoadWords) is not null) return (SettingTiming.LevelLoad, group);
        if (Sentence(sectionIntro, SectionStartWords) is string rest) return (SettingTiming.GameStart, rest);
        return (SettingTiming.Unknown, null);
    }

    /// <summary>The sentence of <paramref name="text"/> that <paramref name="words"/> match, or null.</summary>
    static string? Sentence(string text, Regex words)
    {
        Match match = words.Match(text);
        if (!match.Success) return null;
        int start = match.Index;
        while (start > 0 && !(text[start - 1] == ' ' && start >= 2 && text[start - 2] is '.' or ';')) start--;
        int end = text.IndexOf(". ", match.Index + match.Length, StringComparison.Ordinal);
        end = end < 0 ? text.Length : end + 1;
        return text[start..end].Trim();
    }

    static readonly Regex ChoiceWords = new(@"(?:^|[.;,(]\s+|:\s+|\s)(-?\d+(?:\.\d+)?|[Ee]mpty)\s?=\s([A-Za-z""'][^.;:,()]*)");

    /// <summary>The values the comments name, "48 = a day in half an hour", "Empty = only with the console open".</summary>
    static List<SettingChoice> ChoicesIn(string description)
    {
        var choices = new List<SettingChoice>();
        foreach (Match match in ChoiceWords.Matches(description))
        {
            string value = match.Groups[1].Value.Equals("empty", StringComparison.OrdinalIgnoreCase) ? "" : match.Groups[1].Value;
            string meaning = match.Groups[2].Value.Trim();
            if (meaning.Length == 0 || choices.Any(c => c.Value == value)) continue;
            choices.Add(new SettingChoice(value, meaning));
        }
        return choices;
    }

    /// <summary>The first range the comments give ("30-170", "0 to 255") that holds the default, or null.</summary>
    static (double, double)? RangeIn(string description, string value)
    {
        TryNumber(value, out double number);
        foreach (Match match in Regex.Matches(description, @"(?<![\w.-])(-?\d+(?:\.\d+)?)(?:\s?-\s?| to )(\d+(?:\.\d+)?)(?![\w.])"))
            if (TryNumber(match.Groups[1].Value, out double min) && TryNumber(match.Groups[2].Value, out double max) && min < max &&
                number >= min && number <= max)
                return (min, max);
        return null;
    }

    public static bool TryNumber(string text, out double number) =>
        double.TryParse(text.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out number);

    /// <summary>A value as one line: no line breaks, no spaces round it (the game trims them anyway).</summary>
    static string Clean(string value) => value.Replace("\r", "").Replace("\n", " ").Trim();

    // ---- Adding a setting your file doesn't have yet ---------------------------------------------

    static void Insert(List<IniLine> lines, ModSetting setting, string value, IniText? template, Encoding encoding, string lineBreak)
    {
        // Its comments and title from the template, as written there.
        var comments = new List<string>();
        var sectionBanner = new List<string>();
        if (template != null)
        {
            Entry? source = Entries(template.Lines).FirstOrDefault(e => Id(e) == Id(setting.Section, setting.Key));
            if (source != null)
            {
                for (int i = source.Line - 1; i >= 0 && template.Lines[i].Kind == IniLineKind.Comment; i--) comments.Insert(0, template.Lines[i].Text);
                int header = template.Lines.FindIndex(l => l.Kind == IniLineKind.Section && l.Name.Equals(setting.Section, StringComparison.OrdinalIgnoreCase));
                for (int i = header - 1; i >= 0 && template.Lines[i].Kind == IniLineKind.Comment; i--) sectionBanner.Insert(0, template.Lines[i].Text);
            }
        }
        var added = comments.Select(text => IniLine.Make(text, encoding, lineBreak)).ToList();
        added.Add(IniLine.Make($"{setting.Key}={value}", encoding, lineBreak));

        // After the last setting of its section (the first time the section is written).
        int sectionAt = lines.FindIndex(l => l.Kind == IniLineKind.Section && l.Name.Equals(setting.Section, StringComparison.OrdinalIgnoreCase));
        if (sectionAt >= 0)
        {
            int at = sectionAt + 1;
            for (int i = sectionAt + 1; i < lines.Count && lines[i].Kind != IniLineKind.Section; i++)
                if (lines[i].Kind == IniLineKind.Setting) at = i + 1;
            lines.InsertRange(at, added);
            return;
        }
        // A section your file doesn't have: at the end, with its title.
        if (lines.Count > 0 && lines[^1].Ending.Length == 0) lines[^1] = lines[^1].WithEnding(lineBreak);
        if (lines.Count > 0 && lines[^1].Kind != IniLineKind.Blank) lines.Add(IniLine.Make("", encoding, lineBreak));
        lines.AddRange(sectionBanner.Select(text => IniLine.Make(text, encoding, lineBreak)));
        lines.Add(IniLine.Make($"[{setting.Section}]", encoding, lineBreak));
        lines.AddRange(added);
    }
}

enum IniLineKind { Blank, Comment, Section, Setting }

/// <summary>
/// One line of an ini file, kept as its bytes so a line nobody changes is written back exactly as
/// it was read, whatever its encoding.
/// </summary>
sealed class IniLine
{
    public byte[] Bytes = [];
    public string Ending = "";   // "\r\n", "\n", or "" for a last line without one
    public string Text = "";     // as decoded
    public IniLineKind Kind;
    public string Name = "";     // a section's or setting's name
    public string Value = "";    // a setting's value
    public string Comment = "";  // a comment's text after its ; (or #)

    public static IniLine Make(string text, Encoding encoding, string ending)
    {
        var line = new IniLine { Bytes = encoding.GetBytes(text), Ending = ending, Text = text };
        line.Classify();
        return line;
    }

    public IniLine WithEnding(string ending) => new()
    {
        Bytes = Bytes, Ending = ending, Text = Text, Kind = Kind, Name = Name, Value = Value, Comment = Comment,
    };

    /// <summary>The same line with another value: everything up to the = (and the spaces after it) kept.</summary>
    public IniLine WithValue(string value, Encoding encoding)
    {
        int equals = Text.IndexOf('=');
        int start = equals + 1;
        while (start < Text.Length && Text[start] is ' ' or '\t') start++;
        string text = equals < 0 ? Text + "=" + value : Text[..start] + value;
        return Make(text, encoding, Ending);
    }

    /// <summary>The same reading as the launcher's (settings_update.h): ; or # comments, [section], name=value.</summary>
    public void Classify()
    {
        string trimmed = Text.Trim(' ', '\t');
        if (trimmed.Length == 0) Kind = IniLineKind.Blank;
        else if (trimmed[0] is ';' or '#')
        {
            Kind = IniLineKind.Comment;
            Comment = trimmed[1..];
        }
        else if (trimmed[0] == '[')
        {
            Kind = IniLineKind.Section;
            int close = trimmed.IndexOf(']');
            Name = (close < 0 ? trimmed[1..] : trimmed[1..close]).Trim(' ', '\t');
        }
        else
        {
            Kind = IniLineKind.Setting;
            int equals = trimmed.IndexOf('=');
            Name = (equals < 0 ? trimmed : trimmed[..equals]).Trim(' ', '\t');
            Value = equals < 0 ? "" : trimmed[(equals + 1)..].Trim(' ', '\t');
        }
    }
}

/// <summary>An ini file as lines of bytes, with the encoding to show them in.</summary>
sealed class IniText
{
    public byte[] Mark = [];  // a UTF-8 byte order mark, if the file has one (Notepad can save with one)
    public List<IniLine> Lines = [];
    public Encoding Encoding = new UTF8Encoding(false);

    public static IniText Read(string path)
    {
        byte[] bytes;
        try { bytes = File.ReadAllBytes(path); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { throw new IOException($"Couldn't read {path}: {e.Message}", e); }
        if (bytes.Length >= 2 && ((bytes[0] == 0xFF && bytes[1] == 0xFE) || (bytes[0] == 0xFE && bytes[1] == 0xFF)))
            throw new IOException($"{path} is saved as UTF-16 (Unicode), which the mods don't read. Save it as UTF-8 or ANSI in a text editor first.");
        return Parse(bytes);
    }

    public static IniText Parse(byte[] bytes)
    {
        var text = new IniText();
        int at = 0;
        if (bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        {
            text.Mark = bytes[..3];
            at = 3;
        }
        text.Encoding = EncodingOf(bytes.AsSpan(at));
        while (at < bytes.Length)
        {
            int end = Array.IndexOf(bytes, (byte)'\n', at);
            int next = end < 0 ? bytes.Length : end + 1;
            if (end < 0) end = bytes.Length;
            bool crlf = end < bytes.Length && end > at && bytes[end - 1] == '\r';
            int contentEnd = crlf ? end - 1 : end;
            var line = new IniLine
            {
                Bytes = bytes[at..contentEnd],
                Ending = end >= bytes.Length ? "" : crlf ? "\r\n" : "\n",
            };
            line.Text = text.Encoding.GetString(line.Bytes);
            line.Classify();
            text.Lines.Add(line);
            at = next;
        }
        return text;
    }

    public static byte[] Bytes(byte[] mark, List<IniLine> lines)
    {
        using var stream = new MemoryStream();
        stream.Write(mark);
        foreach (IniLine line in lines)
        {
            stream.Write(line.Bytes);
            stream.Write(Encoding.ASCII.GetBytes(line.Ending));
        }
        return stream.ToArray();
    }

    /// <summary>UTF-8 when the bytes are UTF-8 (plain ASCII is), else Windows' own code page, as the game reads ini files.</summary>
    static Encoding EncodingOf(ReadOnlySpan<byte> bytes)
    {
        try
        {
            new UTF8Encoding(false, throwOnInvalidBytes: true).GetCharCount(bytes);
            return new UTF8Encoding(false);
        }
        catch (DecoderFallbackException)
        {
            try
            {
                Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
                return Encoding.GetEncoding(0);
            }
            catch (Exception e) when (e is ArgumentException or NotSupportedException)
            {
                return Encoding.Latin1;
            }
        }
    }
}
