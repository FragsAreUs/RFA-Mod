using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Documents;
using System.Windows.Media;
using RfaFormats;

namespace RfaSettings;

/// <summary>
/// One setting in the window: its name, marks (differs from the default, unsaved, when the game takes
/// it), its explanation from the file's comments, a control that fits its value, and reset.
/// </summary>
sealed class SettingRow : Border
{
    public ModSetting Setting { get; }
    /// <summary>The value the control shows: yours from the file, or what you changed it to.</summary>
    public string Value { get; private set; }

    readonly MainWindow owner;
    readonly WrapPanel marks = new();
    readonly TextBlock description = new();
    readonly TextBlock warning = new();
    readonly Button reset;
    readonly Action<string> showValue;
    readonly Border differsMark, unsavedMark;
    bool compact;

    public SettingRow(ModSetting setting, string value, MainWindow owner)
    {
        Setting = setting;
        Value = value;
        this.owner = owner;

        Background = Brush("PanelBrush");
        BorderBrush = Brushes.Transparent;
        BorderThickness = new Thickness(3, 0, 0, 0);
        CornerRadius = new CornerRadius(4);
        Padding = new Thickness(14, 10, 12, 10);
        Margin = new Thickness(0, 0, 0, 6);

        var grid = new Grid();
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(36) });
        Child = grid;

        // Name and marks, the explanation, and a warning about the value typed
        var text = new StackPanel();
        marks.Children.Add(new TextBlock
        {
            Text = setting.Key, FontWeight = FontWeights.SemiBold, FontSize = 14, Margin = new Thickness(0, 0, 10, 2),
            VerticalAlignment = VerticalAlignment.Center,
        });
        differsMark = Mark("Differs from the default", "ChangedBrush", $"This version's default is {Shown(setting.Default)}.");
        unsavedMark = Mark("Not saved yet", "AccentBrush", "Save writes it into rfa_mods.ini.");
        marks.Children.Add(differsMark);
        marks.Children.Add(unsavedMark);
        if (TimingMark(setting) is Border timing) marks.Children.Add(timing);
        if (!setting.HasDefault)
            marks.Children.Add(Mark("Not one of this version's settings", "DimTextBrush",
                "Your file has it, this version's defaults don't: it's kept as it is. A mod of your own may read it, or an older version had it."));
        else if (!setting.InYourFile)
            marks.Children.Add(Mark("New: not in your file yet", "DimTextBrush",
                "The launcher adds it to your file with its default the next time it starts; change it here and Save adds it now."));
        text.Children.Add(marks);
        description.TextWrapping = TextWrapping.Wrap;
        description.Foreground = Brush("DimTextBrush");
        description.FontSize = 12;
        description.Margin = new Thickness(0, 4, 0, 0);
        text.Children.Add(description);
        warning.TextWrapping = TextWrapping.Wrap;
        warning.FontSize = 12;
        warning.Margin = new Thickness(0, 4, 0, 0);
        warning.Visibility = Visibility.Collapsed;
        text.Children.Add(warning);
        grid.Children.Add(text);

        // The control
        FrameworkElement control = MakeControl(out showValue);
        control.Margin = new Thickness(18, 0, 0, 0);
        control.VerticalAlignment = VerticalAlignment.Top;
        Grid.SetColumn(control, 1);
        grid.Children.Add(control);

        reset = new Button
        {
            Content = new TextBlock { Text = "", Style = (Style)Application.Current.FindResource("Icon"), FontSize = 14 },
            Margin = new Thickness(6, 0, 0, 0), Padding = new Thickness(6), VerticalAlignment = VerticalAlignment.Top,
            ToolTip = $"Back to the default: {Shown(setting.Default)}",
        };
        reset.Click += (_, _) => { if (Setting.Default != null) showValue(Setting.Default); };
        Grid.SetColumn(reset, 2);
        grid.Children.Add(reset);

        ShowDescription(compact: false);
        Refresh();
    }

    /// <summary>
    /// Shows the whole explanation, or, for a setting that shares the one of the setting just above
    /// it on screen (Console's InputScale, TextScale's), a line saying so with the explanation as a tool tip.
    /// </summary>
    public void ShowDescription(bool compact)
    {
        this.compact = compact;
        description.Inlines.Clear();
        description.ToolTip = null;
        if (compact && Setting.SharesDescriptionOf != null)
        {
            description.Inlines.Add(new Run($"Explained with {Setting.SharesDescriptionOf}, above."));
            description.ToolTip = new TextBlock { Text = Setting.Description.Replace("\n", "\n\n"), TextWrapping = TextWrapping.Wrap, MaxWidth = 520 };
            return;
        }
        string[] paragraphs = Setting.Description.Length > 0 ? Setting.Description.Split('\n') : ["(The file has no explanation for this one.)"];
        for (int i = 0; i < paragraphs.Length; i++)
        {
            if (i > 0) description.Inlines.Add(new LineBreak());
            description.Inlines.Add(new Run(paragraphs[i]));
        }
    }

    /// <summary>The value was changed in its control.</summary>
    void Changed(string value)
    {
        Value = value;
        owner.ValueChanged(this);
        Refresh();
    }

    /// <summary>The marks, the reset button and the warning, for the value shown.</summary>
    public void Refresh()
    {
        bool differs = Setting.HasDefault && !Setting.IsDefault(Value);
        bool unsaved = owner.IsUnsaved(Setting);
        differsMark.Visibility = differs ? Visibility.Visible : Visibility.Collapsed;
        unsavedMark.Visibility = unsaved ? Visibility.Visible : Visibility.Collapsed;
        BorderBrush = unsaved ? Brush("AccentBrush") : differs ? Brush("ChangedBrush") : Brushes.Transparent;
        reset.Visibility = differs ? Visibility.Visible : Visibility.Hidden;

        string? problem = Problem(Setting, Value);
        string? caution = problem == null ? Caution() : null;
        warning.Text = problem ?? caution ?? "";
        warning.Foreground = Brush(problem != null ? "ErrorBrush" : "ChangedBrush");
        warning.Visibility = warning.Text.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
    }

    /// <summary>What is wrong with a value, so it can't be saved, or null.</summary>
    public static string? Problem(ModSetting setting, string value) =>
        setting.Kind == SettingKind.Number && !ModSettings.TryNumber(value, out _)
            ? "This one takes a number (with a . for decimals, as 1.5)." : null;

    string? Caution()
    {
        if (Setting.Range is var (min, max) && ModSettings.TryNumber(Value, out double number) && (number < min || number > max))
            return $"Its explanation gives {Number(min)} to {Number(max)}.";
        if (Setting.Kind == SettingKind.Folder && Value.Trim().Trim('"').Length > 0 && !Directory.Exists(Value.Trim().Trim('"')))
            return "There is no such folder on this PC.";
        return null;
    }

    static string Number(double value) => value.ToString(System.Globalization.CultureInfo.InvariantCulture);

    // ---- Controls ----------------------------------------------------------------------------------

    FrameworkElement MakeControl(out Action<string> show)
    {
        // A switch: only while the value is 0 or 1 (another value, typed into the file, gets a text box).
        if (Setting.Kind == SettingKind.Toggle && Value.Trim() is "0" or "1")
        {
            var toggle = new Wpf.Ui.Controls.ToggleSwitch
            {
                IsChecked = Value.Trim() == "1", OnContent = "On", OffContent = "Off", MinWidth = 120,
                ToolTip = "On = 1, Off = 0",
            };
            toggle.Checked += (_, _) => Changed("1");
            toggle.Unchecked += (_, _) => Changed("0");
            show = value => toggle.IsChecked = value.Trim() == "1";
            return toggle;
        }

        // A list of the values the comments name, which you can still type another value into.
        if (Setting.Choices.Count > 0)
        {
            var combo = new ComboBox
            {
                IsEditable = true, IsTextSearchEnabled = false, Width = Setting.Kind == SettingKind.Number ? 220 : 300,
                ToolTip = "Pick one of the values its explanation names, or type another",
            };
            foreach (SettingChoice choice in Setting.Choices)
            {
                var item = new ComboBoxItem { Content = $"{(choice.Value.Length == 0 ? "(empty)" : choice.Value)}  –  {choice.Meaning}" };
                TextSearch.SetText(item, choice.Value);
                combo.Items.Add(item);
            }
            combo.Text = Value;
            combo.AddHandler(TextBoxBase.TextChangedEvent, new TextChangedEventHandler((_, _) => Changed(combo.Text)));
            show = value => combo.Text = value;
            return combo;
        }

        var box = new TextBox
        {
            Text = Value, Width = Setting.Kind switch { SettingKind.Number => 140, SettingKind.Folder => 360, _ => 300 },
            HorizontalContentAlignment = Setting.Kind == SettingKind.Number ? HorizontalAlignment.Right : HorizontalAlignment.Left,
        };
        box.TextChanged += (_, _) => Changed(box.Text);
        show = value => box.Text = value;
        if (Setting.Kind != SettingKind.Folder) return box;

        // A folder: typed, or picked.
        var browse = new Button { Content = "Browse…", Margin = new Thickness(6, 0, 0, 0) };
        browse.Click += (_, _) =>
        {
            string current = box.Text.Trim().Trim('"');
            var dialog = new Microsoft.Win32.OpenFolderDialog { Title = $"{Setting.Key}: pick the folder" };
            if (current.Length > 0 && Directory.Exists(current)) dialog.InitialDirectory = current;
            if (dialog.ShowDialog(Window.GetWindow(this)) == true) box.Text = dialog.FolderName;
        };
        var panel = new StackPanel { Orientation = Orientation.Horizontal };
        panel.Children.Add(box);
        panel.Children.Add(browse);
        return panel;
    }

    // ---- Marks ---------------------------------------------------------------------------------------

    static Border? TimingMark(ModSetting setting) => setting.Timing switch
    {
        SettingTiming.Live => Mark("Live", "AccentBrush", "Changes in the running game as soon as you save." +
                                   (setting.TimingNote != null ? "\n\n" + setting.TimingNote : "")),
        SettingTiming.GameStart => Mark("When the game starts", "DimTextBrush", "The game reads it when it starts: if it's running, start it again for a change to show." +
                                        (setting.TimingNote != null ? "\n\nIts explanation: " + setting.TimingNote : "")),
        SettingTiming.LevelLoad => Mark("When the level loads", "DimTextBrush", "Load the level again for a change to show." +
                                        (setting.TimingNote != null ? "\n\nIts explanation: " + setting.TimingNote : "")),
        _ => null,
    };

    static Border Mark(string text, string brush, string tip) => new()
    {
        BorderBrush = Brush(brush), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(4),
        Padding = new Thickness(6, 0, 6, 1), Margin = new Thickness(0, 0, 6, 2), VerticalAlignment = VerticalAlignment.Center,
        Child = new TextBlock { Text = text, FontSize = 11, Foreground = Brush(brush) },
        ToolTip = new TextBlock { Text = tip, TextWrapping = TextWrapping.Wrap, MaxWidth = 460 },
    };

    static string Shown(string? value) => value == null ? "(none)" : value.Length == 0 ? "(empty)" : value;

    static Brush Brush(string name) => (Brush)Application.Current.FindResource(name);
}
