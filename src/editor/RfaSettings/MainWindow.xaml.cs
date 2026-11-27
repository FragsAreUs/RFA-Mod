using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;
using RfaFormats;

namespace RfaSettings;

/// <summary>
/// The settings window (see Program.cs). What you change is kept here until you save; Save hands only
/// those values to ModSettings.Save, which writes them into the file as it is on disk then, so what
/// the game or the launcher wrote meanwhile stays. When the file changes on disk the window shows
/// what it has now, keeping your unsaved changes.
/// </summary>
public partial class MainWindow : Window
{
    readonly string iniPath;
    readonly string templatePath;
    ModSettings settings = null!;
    /// <summary>Your unsaved values, by "section key" (they outlast reading the file again).</summary>
    readonly Dictionary<string, string> unsaved = new(StringComparer.OrdinalIgnoreCase);
    readonly Dictionary<string, SettingRow> rows = new(StringComparer.OrdinalIgnoreCase);
    readonly List<SectionItem> sectionItems = [];
    FileSystemWatcher? watcher;
    readonly DispatcherTimer fileChanged = new() { Interval = TimeSpan.FromMilliseconds(400) };
    readonly DispatcherTimer gameCheck = new() { Interval = TimeSpan.FromSeconds(3) };
    DateTime writtenAt;  // the file's time after our own save, so that change isn't taken for someone else's

    public MainWindow(string iniPath, string templatePath)
    {
        this.iniPath = iniPath;
        this.templatePath = templatePath;
        InitializeComponent();
        SourceInitialized += (_, _) => RfaEditor.DarkTitleBar.Apply(this);
        Title = $"Settings · RFA-Mod · {iniPath}";
        PreviewKeyDown += OnKey;
        Closing += OnClosing;
        fileChanged.Tick += (_, _) => { fileChanged.Stop(); OnFileChangedOnDisk(); };
        gameCheck.Tick += (_, _) => ShowGameNote();
        Loaded += (_, _) =>
        {
            if (!Read(keepView: false)) { Close(); return; }
            Watch();
            ShowGameNote();
            gameCheck.Start();
        };
    }

    static string Id(ModSetting setting) => setting.Section + " " + setting.Key;

    internal bool IsUnsaved(ModSetting setting) => unsaved.ContainsKey(Id(setting));

    /// <summary>A row's value changed: unsaved, unless it's the file's value again.</summary>
    internal void ValueChanged(SettingRow row)
    {
        if (row.Value.Trim() == row.Setting.Value) unsaved.Remove(Id(row.Setting));
        else unsaved[Id(row.Setting)] = row.Value;
        ShowStatus();
    }

    // ---- Reading the file, and the view ------------------------------------------------------------

    /// <summary>Reads the file (again). False, after saying why, when it can't be read.</summary>
    bool Read(bool keepView)
    {
        try
        {
            settings = ModSettings.Load(iniPath, templatePath);
        }
        catch (IOException e)
        {
            MessageBox.Show(this, e.Message, "RFA-Mod settings", MessageBoxButton.OK, MessageBoxImage.Error);
            return false;
        }
        // An unsaved value that is what the file has now isn't a change any more.
        foreach (ModSetting setting in settings.All)
            if (unsaved.TryGetValue(Id(setting), out string? value) && value.Trim() == setting.Value) unsaved.Remove(Id(setting));

        string? picked = (SectionList.SelectedItem as SectionItem)?.Section.Name;
        double scroll = Scroller.VerticalOffset;
        rows.Clear();
        sectionItems.Clear();
        foreach (ModSettingsSection section in settings.Sections) sectionItems.Add(new SectionItem(section));
        SectionList.ItemsSource = null;
        SectionList.ItemsSource = sectionItems;
        SectionList.SelectedItem = sectionItems.FirstOrDefault(item => item.Section.Name == picked) ?? sectionItems.FirstOrDefault();
        ShowRows();
        if (keepView) Scroller.ScrollToVerticalOffset(scroll);
        ShowStatus();
        return true;
    }

    SettingRow Row(ModSetting setting)
    {
        if (!rows.TryGetValue(Id(setting), out SettingRow? row))
        {
            row = new SettingRow(setting, unsaved.TryGetValue(Id(setting), out string? value) ? value : setting.Value, this);
            rows[Id(setting)] = row;
        }
        return row;
    }

    string ValueOf(ModSetting setting) => unsaved.TryGetValue(Id(setting), out string? value) ? value : setting.Value;

    bool Differs(ModSetting setting) => setting.HasDefault && !setting.IsDefault(ValueOf(setting));

    void OnSectionPicked(object sender, SelectionChangedEventArgs e)
    {
        if (Search.Text.Trim().Length == 0 && OnlyChanged.IsChecked != true) ShowRows();
    }

    void OnFilterChanged(object sender, RoutedEventArgs e)
    {
        if (settings != null) ShowRows();
    }

    /// <summary>The section picked; or, while searching or showing only what differs, every setting that does, by section.</summary>
    void ShowRows()
    {
        foreach (SettingRow row in rows.Values) (row.Parent as Panel)?.Children.Remove(row);
        Rows.Children.Clear();
        string[] words = Search.Text.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        bool onlyDiffering = OnlyChanged.IsChecked == true;
        bool filtering = words.Length > 0 || onlyDiffering;
        SectionList.IsEnabled = !filtering;

        int shown = 0;
        IEnumerable<ModSettingsSection> sections = filtering
            ? settings.Sections
            : SectionList.SelectedItem is SectionItem item ? [item.Section] : [];
        foreach (ModSettingsSection section in sections)
        {
            var matching = section.Settings.Where(s => (!onlyDiffering || Differs(s)) && Matches(section, s, words)).ToList();
            if (matching.Count == 0) continue;
            AddSectionHeader(section, filtering);
            string? group = null;
            ModSetting? previous = null;
            foreach (ModSetting setting in matching)
            {
                if (setting.Group != group && setting.Group != null)
                    Rows.Children.Add(new TextBlock { Text = setting.Group.ToUpperInvariant(), Style = (Style)FindResource("SectionTitle") });
                group = setting.Group;
                SettingRow row = Row(setting);
                row.ShowDescription(compact: previous != null && setting.SharesDescriptionOf != null &&
                                            (previous.Key == setting.SharesDescriptionOf || previous.SharesDescriptionOf == setting.SharesDescriptionOf));
                Rows.Children.Add(row);
                previous = setting;
                shown++;
            }
        }
        if (shown == 0)
            Rows.Children.Add(new TextBlock
            {
                Text = filtering ? (onlyDiffering && words.Length == 0 ? "Every setting is at its default." : "No setting matches.") : "",
                Foreground = (System.Windows.Media.Brush)FindResource("DimTextBrush"), Margin = new Thickness(0, 12, 0, 0),
            });
        if (!filtering) Scroller.ScrollToTop();
    }

    void AddSectionHeader(ModSettingsSection section, bool small)
    {
        var header = new StackPanel { Margin = new Thickness(0, Rows.Children.Count == 0 ? 0 : 18, 0, 10) };
        header.Children.Add(new TextBlock { Text = $"[{section.Name}]", FontSize = small ? 16 : 22, FontWeight = FontWeights.SemiBold });
        string title = section.Title + (section.InDefaults ? "" : (section.Title.Length > 0 ? " · " : "") +
                                        "only in your file: this version has no such section, so it's kept as it is");
        if (title.Length > 0)
            header.Children.Add(new TextBlock
            {
                Text = title, Foreground = (System.Windows.Media.Brush)FindResource("DimTextBrush"), TextWrapping = TextWrapping.Wrap,
                Margin = new Thickness(0, 2, 0, 0),
            });
        Rows.Children.Add(header);
    }

    bool Matches(ModSettingsSection section, ModSetting setting, string[] words)
    {
        if (words.Length == 0) return true;
        string text = $"{section.Name} {section.Title} {setting.Key} {setting.Group} {setting.Description} {ValueOf(setting)}";
        return words.All(word => text.Contains(word, StringComparison.OrdinalIgnoreCase));
    }

    void ShowStatus()
    {
        foreach (SectionItem item in sectionItems)
        {
            int differing = item.Section.Settings.Count(Differs);
            int notSaved = item.Section.Settings.Count(IsUnsaved);
            item.Badge = (differing > 0 ? $"{differing} changed" : "") + (notSaved > 0 ? (differing > 0 ? " · " : "") + $"{notSaved} unsaved" : "");
        }
        foreach (SettingRow row in rows.Values) row.Refresh();
        SaveButton.IsEnabled = unsaved.Count > 0;
        UndoButton.IsEnabled = unsaved.Count > 0;
        int total = settings.All.Count(), differs = settings.All.Count(Differs);
        Status.Text = unsaved.Count > 0
            ? $"{unsaved.Count} unsaved change{(unsaved.Count == 1 ? "" : "s")}."
            : $"{total} settings, {differs} differ{(differs == 1 ? "s" : "")} from the defaults." +
              (settings.FileExists ? "" : " Your rfa_mods.ini doesn't exist yet: Save makes it from the defaults.");
    }

    /// <summary>While the game runs: when your changes reach it.</summary>
    void ShowGameNote()
    {
        string exe = settings?.All.FirstOrDefault(s => s.Section.Equals("Launcher", StringComparison.OrdinalIgnoreCase) &&
                                                        s.Key.Equals("GameExe", StringComparison.OrdinalIgnoreCase))?.Value ?? "";
        string name = Path.GetFileNameWithoutExtension(exe.Length > 0 ? exe : "RedFactionArmageddon_DX11.exe");
        Process[] running = Process.GetProcessesByName(name);
        foreach (Process process in running) process.Dispose();
        GameNote.Text = "The game is running. Settings marked Live change in it as soon as you save; for the others, start the game again " +
                        "(or load the level again, where a setting says so).";
        GameNote.Visibility = running.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
    }

    // ---- Saving --------------------------------------------------------------------------------------

    bool Save()
    {
        var changes = new Dictionary<ModSetting, string>();
        var problems = new List<string>();
        foreach (ModSetting setting in settings.All)
        {
            if (!unsaved.TryGetValue(Id(setting), out string? value)) continue;
            if (SettingRow.Problem(setting, value) is string problem) problems.Add($"[{setting.Section}] {setting.Key}: {problem}");
            changes[setting] = value;
        }
        if (problems.Count > 0)
        {
            MessageBox.Show(this, "Nothing was saved yet. These need fixing first:\n\n" + string.Join("\n", problems),
                            "RFA-Mod settings", MessageBoxButton.OK, MessageBoxImage.Warning);
            return false;
        }

        ModSettingsSaveResult result;
        try
        {
            result = settings.Save(changes);
            writtenAt = File.GetLastWriteTimeUtc(iniPath);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            MessageBox.Show(this, $"Couldn't save rfa_mods.ini, so it is as it was and your changes are still here:\n\n{e.Message}",
                            "RFA-Mod settings", MessageBoxButton.OK, MessageBoxImage.Error);
            return false;
        }
        int live = changes.Keys.Count(s => s.Timing == SettingTiming.Live);
        unsaved.Clear();
        Read(keepView: true);
        Status.Text = $"Saved {result.Written.Count} setting{(result.Written.Count == 1 ? "" : "s")}" +
                      (GameNote.Visibility == Visibility.Visible && live < changes.Count
                          ? $"; {(live == 0 ? "they" : $"{changes.Count - live} of them")} reach the game when you start it again."
                          : ".");
        if (result.ChangedMeanwhile.Count > 0)
            MessageBox.Show(this, "While this window was open, something else changed these settings in the file (the game's own menus write " +
                                  "to it too). They keep what it wrote:\n\n" + string.Join("\n", result.ChangedMeanwhile),
                            "RFA-Mod settings", MessageBoxButton.OK, MessageBoxImage.Information);
        return true;
    }

    void OnSave(object sender, RoutedEventArgs e) => Save();

    void OnUndo(object sender, RoutedEventArgs e)
    {
        unsaved.Clear();
        Read(keepView: true);
    }

    void OnOpenFile(object sender, RoutedEventArgs e)
    {
        if (!File.Exists(iniPath))
        {
            MessageBox.Show(this, $"There is no {iniPath} yet: Save makes it.", "RFA-Mod settings");
            return;
        }
        try { Process.Start(new ProcessStartInfo(iniPath) { UseShellExecute = true })?.Dispose(); }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException)
        {
            MessageBox.Show(this, $"Couldn't open {iniPath}: {ex.Message}", "RFA-Mod settings");
        }
    }

    void OnKey(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.S && Keyboard.Modifiers == ModifierKeys.Control)
        {
            if (unsaved.Count > 0) Save();
            e.Handled = true;
        }
        else if (e.Key == Key.F && Keyboard.Modifiers == ModifierKeys.Control)
        {
            Search.Focus();
            e.Handled = true;
        }
    }

    void OnClosing(object? sender, CancelEventArgs e)
    {
        if (unsaved.Count == 0) return;
        MessageBoxResult answer = MessageBox.Show(this, $"Save your {unsaved.Count} change{(unsaved.Count == 1 ? "" : "s")} to rfa_mods.ini?",
                                                  "RFA-Mod settings", MessageBoxButton.YesNoCancel, MessageBoxImage.Question);
        if (answer == MessageBoxResult.Cancel || (answer == MessageBoxResult.Yes && !Save())) e.Cancel = true;
    }

    // ---- The file changing on disk (the game's menus, the launcher, a text editor) ---------------------

    void Watch()
    {
        string? folder = Path.GetDirectoryName(iniPath);
        if (folder == null || !Directory.Exists(folder)) return;
        watcher = new FileSystemWatcher(folder, Path.GetFileName(iniPath))
        {
            NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.Size,
        };
        FileSystemEventHandler changed = (_, _) => Dispatcher.BeginInvoke(() => { fileChanged.Stop(); fileChanged.Start(); });
        watcher.Changed += changed;
        watcher.Created += changed;
        watcher.Renamed += (_, _) => Dispatcher.BeginInvoke(() => { fileChanged.Stop(); fileChanged.Start(); });
        watcher.EnableRaisingEvents = true;
        Closed += (_, _) => watcher.Dispose();
    }

    void OnFileChangedOnDisk()
    {
        if (!File.Exists(iniPath) || File.GetLastWriteTimeUtc(iniPath) == writtenAt) return;
        writtenAt = File.GetLastWriteTimeUtc(iniPath);
        bool hadUnsaved = unsaved.Count > 0;
        if (!Read(keepView: true)) return;
        Status.Text = "rfa_mods.ini was changed by another program (the game's menus write to it too): this is what it has now." +
                      (hadUnsaved ? " Your unsaved changes are kept; Save writes only those." : "");
    }

    /// <summary>A section in the list on the left.</summary>
    sealed class SectionItem(ModSettingsSection section) : INotifyPropertyChanged
    {
        public ModSettingsSection Section { get; } = section;
        public string Name => Section.Name;
        public string Title => Section.Title;
        string badge = "";
        public string Badge
        {
            get => badge;
            set
            {
                if (badge == value) return;
                badge = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Badge)));
            }
        }
        public event PropertyChangedEventHandler? PropertyChanged;
    }
}
