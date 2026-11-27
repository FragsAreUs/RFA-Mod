using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using Microsoft.Win32;
using RfaFormats;
using RfaFormats.Levels;

namespace RfaEditor;

// "New level": a brand-new level for one of the game's modes, listed in that mode's map list (see
// NewLevel). "Spawn resources": what a level can spawn (its spawn_resource.xtbl entry), changed.
public partial class MainWindow
{
    const string TablesArchive = @"build\pc\cache\table.vpp_pc";

    Window Dialog(string title)
    {
        var window = new Window
        {
            Title = title,
            Owner = this,
            SizeToContent = SizeToContent.WidthAndHeight,
            ResizeMode = ResizeMode.NoResize,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
        };
        if (TryFindResource("WindowBrush") is System.Windows.Media.Brush back) window.Background = back;
        if (TryFindResource("TextBrush") is System.Windows.Media.Brush fore) window.Foreground = fore;
        return window;
    }

    /// <summary>A label-and-control grid for a dialog; Row adds a line.</summary>
    sealed class DialogForm
    {
        public readonly Grid Grid = new() { Margin = new Thickness(12) };
        int row;
        public DialogForm(double width)
        {
            Grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            Grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(width) });
        }
        public void Row(string label, FrameworkElement control, string? tip = null)
        {
            Grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            var text = new TextBlock { Text = label, Margin = new Thickness(0, 4, 10, 4), VerticalAlignment = VerticalAlignment.Top, ToolTip = tip };
            control.Margin = new Thickness(0, 3, 0, 3);
            control.ToolTip ??= tip;
            Grid.SetRow(text, row);
            Grid.SetRow(control, row);
            Grid.SetColumn(control, 1);
            Grid.Children.Add(text);
            Grid.Children.Add(control);
            row++;
        }
        public void Buttons(Window window, string okText)
        {
            var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 10, 0, 0) };
            var ok = new Button { Content = okText, IsDefault = true, MinWidth = 80, Margin = new Thickness(0, 0, 6, 0) };
            var cancel = new Button { Content = "Cancel", IsCancel = true, MinWidth = 80 };
            ok.Click += (_, _) => window.DialogResult = true;
            buttons.Children.Add(ok);
            buttons.Children.Add(cancel);
            Grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            Grid.SetRow(buttons, row);
            Grid.SetColumnSpan(buttons, 2);
            Grid.Children.Add(buttons);
            window.Content = Grid;
        }
    }

    // ---- New level ---------------------------------------------------------------------------------

    void OnNewLevel(object sender, RoutedEventArgs e) => ShowNewLevel(0);

    /// <summary>
    /// The mode New level opens for once the editor has read the game's files ("ruin" or "infestation"):
    /// the game's main menu (Level editor > Create level) starts the editor with --new-level &lt;mode&gt;.
    /// </summary>
    internal string? NewLevelAtStart { get; set; }

    /// <summary>
    /// A level to make, all of it chosen in the game's main menu (Level editor > Create level, its own page): the menu
    /// starts the editor with --new-level &lt;mode&gt; --name ... and the editor makes it once it has read the game's files,
    /// with no dialog, and opens it.
    /// </summary>
    internal sealed record NewLevelRequest(NewLevel.Mode Mode, string Name, string DisplayName, bool NoGround, int Size,
                                           string GroundTextures, string Sky, bool Replace);

    internal NewLevelRequest? NewLevelMadeAtStart { get; set; }

    /// <summary>
    /// What the game's main menu asks for, from the arguments it starts the editor with (or sends an open one, see
    /// <see cref="GameMenuPipe"/>): --new-level ruin|infestation opens New level for the mode; with --name (the Create
    /// level page chose everything) the level is made with no dialog: --name &lt;file name&gt; --display &lt;name in the
    /// menu&gt; --ground new|none --size &lt;metres&gt; --textures &lt;level&gt; --sky &lt;sky&gt; [--replace].
    /// </summary>
    internal static (string? Mode, NewLevelRequest? Request) MenuRequestFrom(string[] args)
    {
        string? Arg(string name)
        {
            int at = Array.FindIndex(args, a => a.Equals(name, StringComparison.OrdinalIgnoreCase));
            return at >= 0 && at + 1 < args.Length ? args[at + 1] : null;
        }
        int newLevel = Array.FindIndex(args, a => a.Equals("--new-level", StringComparison.OrdinalIgnoreCase));
        string? mode = newLevel >= 0 ? (newLevel + 1 < args.Length ? args[newLevel + 1] : "ruin") : null;
        if (mode == null || Arg("--name") is not { } levelName) return (mode, null);
        var request = new NewLevelRequest(
            mode.Equals("infestation", StringComparison.OrdinalIgnoreCase) ? NewLevel.Mode.Infestation : NewLevel.Mode.Ruin,
            levelName.Trim().ToLowerInvariant(), (Arg("--display") ?? "").Trim(),
            (Arg("--ground") ?? "new").Equals("none", StringComparison.OrdinalIgnoreCase),
            int.TryParse(Arg("--size"), out int size) ? Math.Clamp(size, 16, 255) : 255,
            (Arg("--textures") ?? "").Trim(), (Arg("--sky") ?? "").Trim(),
            args.Any(a => a.Equals("--replace", StringComparison.OrdinalIgnoreCase)));
        return (null, request);
    }

    /// <summary>
    /// The game's main menu asked this editor, already open, for what it would have started a new one for (its
    /// arguments, through <see cref="GameMenuPipe"/>): the game docked, and New level or the level it chose. With the
    /// game's files read already, that happens now and the menu's processing screen ends; else once they are.
    /// </summary>
    internal void FromGameMenu(string[] args)
    {
        if (WindowState == WindowState.Minimized) WindowState = WindowState.Normal;
        Activate();
        Topmost = true;  // over the game (it may be fullscreen), then back to normal
        Topmost = false;
        if (args.Any(a => a.Equals("--dock", StringComparison.OrdinalIgnoreCase))) ShowGameToDock();
        var (mode, request) = MenuRequestFrom(args);
        if (mode == null && request == null) return;
        NewLevelAtStart = mode;
        NewLevelMadeAtStart = request;
        if (!context.Assets.IndexReady)
        {
            Log(request != null ? $"New level {request.Name} is made once the editor has read the game's files (a minute or so)."
                                : "New level opens once the editor has read the game's files (a minute or so).");
            return;
        }
        MakeLevelFromGameMenu();
        SignalGameMenuReady();
        NewLevelFromGameMenu();
    }

    /// <summary>Tells the game's main menu, waiting with its processing screen up, that the editor is ready (Local\RFAModEditorReady).</summary>
    static void SignalGameMenuReady()
    {
        if (System.Threading.EventWaitHandle.TryOpenExisting(@"Local\RFAModEditorReady", out var ready))
            using (ready) ready.Set();
    }

    /// <summary>Makes <see cref="NewLevelMadeAtStart"/>'s level, if the menu asked for one (once the files are read).</summary>
    void MakeLevelFromGameMenu()
    {
        if (NewLevelMadeAtStart is not { } asked) return;
        NewLevelMadeAtStart = null;
        Log($"New level {asked.Name} for {asked.Mode}, as the game's menu asked.");
        if (WindowState == WindowState.Minimized) WindowState = WindowState.Normal;
        Activate();
        string start = asked.GroundTextures;
        if (!asked.NoGround && start.Length == 0)
            start = NewLevel.LevelsWithGround(workspace, context.Assets).FirstOrDefault() ?? "";
        if (!asked.NoGround && start.Length == 0) { Log("New level: the game has no level with ground textures to paint the ground with."); return; }
        if (!Regex.IsMatch(asked.Name, "^[a-z][a-z0-9_]{1,30}$")) { Log("New level: the file name has to be lower case letters, digits and _ (2 to 31, a letter first)."); return; }
        bool exists = File.Exists(Path.Combine(workspace.OverridesFolder, $@"build\pc\cache\{asked.Name}.vpp_pc"));
        if (exists && !asked.Replace) { Log($"New level: there is already a level {asked.Name}; the menu didn't say to make it again."); return; }
        if (exists) foreach (var (key, _) in OpenTabs($@"level:build\pc\cache\{asked.Name}.vpp_pc")) CloseTab(key);
        _ = MakeNewLevel(asked.Mode, start, asked.Name, asked.DisplayName.Length > 0 ? asked.DisplayName : asked.Name, null, exists, asked.NoGround,
                         asked.Size, string.IsNullOrWhiteSpace(asked.Sky) ? NewLevel.DefaultSky : asked.Sky);
    }

    /// <summary>
    /// What the main menu's Create level page offers (the game's levels with ground textures, its skies), written next to
    /// the editor (new_level_choices.txt: texture=... and sky=... lines) each time it has read the game's files: the menu
    /// can't read the archives itself.
    /// </summary>
    void WriteNewLevelChoices()
    {
        try
        {
            var lines = new List<string> { "# What the game's main menu offers in Level editor > Create level. The editor writes it." };
            lines.AddRange(NewLevel.LevelsWithGround(workspace, context.Assets).Select(l => "texture=" + l));
            lines.AddRange(NewLevel.Skies(workspace).Select(s => "sky=" + s));
            File.WriteAllLines(Path.Combine(AppContext.BaseDirectory, "new_level_choices.txt"), lines);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            EditorLog.Write("The main menu's Create level choices couldn't be written: " + ex.Message);
        }
    }

    /// <summary>Opens New level for <see cref="NewLevelAtStart"/>'s mode, if one was asked for (once the files are read).</summary>
    void NewLevelFromGameMenu()
    {
        if (NewLevelAtStart is not { } asked) return;
        NewLevelAtStart = null;
        int modeIndex = asked.Equals("infestation", StringComparison.OrdinalIgnoreCase) ? 1 : 0;
        Log($"New level for {(modeIndex == 1 ? "Infestation" : "Ruin")}, as the game's menu asked.");
        if (WindowState == WindowState.Minimized) WindowState = WindowState.Normal;
        Activate();
        Topmost = true;  // over the game (it may be fullscreen), then back to normal
        Topmost = false;
        ShowNewLevel(modeIndex);
    }

    async void ShowNewLevel(int modeIndex)
    {
        if (!context.Assets.IndexReady)
        {
            Log("New level: the editor is still reading the game's files. Try again in a few seconds.");
            return;
        }
        Window window = Dialog("New level");
        var form = new DialogForm(300);
        var mode = new ComboBox { ItemsSource = new[] { "Ruin", "Infestation" }, SelectedIndex = modeIndex };
        var groundLevels = NewLevel.LevelsWithGround(workspace, context.Assets);
        var template = new ComboBox { ItemsSource = groundLevels, SelectedIndex = groundLevels.Count > 0 ? 0 : -1 };
        var fileName = new TextBox { Text = "" };
        var displayName = new TextBox { Text = "" };
        var ground = new ComboBox { ItemsSource = new[] { "New, flat (made from nothing: to sculpt and paint)", "None (for caves and interiors built from pieces)" }, SelectedIndex = 0 };
        var sizes = new[] { 32, 64, 96, 128, 160, 192, 224, 255 };
        var groundSize = new ComboBox { ItemsSource = sizes.Select(v => v == 255 ? "255 m (the whole zone)" : $"{v} m").ToList(), SelectedIndex = sizes.Length - 1 };
        var skies = NewLevel.Skies(workspace);
        var sky = new ComboBox { ItemsSource = skies, SelectedIndex = Math.Max(0, skies.FindIndex(s => s.Equals(NewLevel.DefaultSky, StringComparison.OrdinalIgnoreCase))),
                                 IsEditable = true, MaxDropDownHeight = 400 };
        void GroundChoices() => groundSize.IsEnabled = template.IsEnabled = ground.SelectedIndex == 0;
        ground.SelectionChanged += (_, _) => GroundChoices();
        var picture = new TextBox { IsReadOnly = true, Text = "" };
        var browse = new Button { Content = "Choose...", Margin = new Thickness(6, 0, 0, 0), MinWidth = 70 };
        var clear = new Button { Content = "None", Margin = new Thickness(6, 0, 0, 0), MinWidth = 50 };
        browse.Click += (_, _) =>
        {
            var pick = new OpenFileDialog { Title = "A picture for the level's preview", Filter = "Pictures|*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff|All files|*.*" };
            if (pick.ShowDialog(window) == true) picture.Text = pick.FileName;
        };
        clear.Click += (_, _) => picture.Text = "";
        var pictureRow = new DockPanel();
        DockPanel.SetDock(clear, Dock.Right);
        DockPanel.SetDock(browse, Dock.Right);
        pictureRow.Children.Add(clear);
        pictureRow.Children.Add(browse);
        pictureRow.Children.Add(picture);
        form.Row("Mode", mode, "Which mode's map list it is in");
        form.Row("File name", fileName, "The level's file name (its archive, <name>.vpp_pc): lower case letters, digits and _");
        form.Row("Name in the menu", displayName, "What the map list shows (every language)");
        form.Row("Ground", ground, "New: ground made from nothing (flat, in the middle of the zone). None: no ground at all: " +
                                   "build the floor, walls and ceiling from stitch pieces, terrain pieces and chunks");
        form.Row("Ground size", groundSize, "How big the new ground is: a square in the middle of the zone; past it the level ends in a drop");
        form.Row("Ground textures", template, "Which of the game's ground texture sets the new ground is painted with (the set this level's ground has: " +
                                              "only its textures are used, nothing else of the level). Paint its layers in Sculpt");
        form.Row("Sky", sky, "The level's sky (the game's sky effects): its environment box names it, and its files go with the level");
        form.Row("Preview picture", pictureRow, "Shown for it in the map list (and Infestation's lobby and loading screen): fitted to the game's picture size, " +
                                                "its middle kept. None: a title card with its name");
        form.Row("", new TextBlock
        {
            Text = "Everything in it is new: its zone and objects (player starts, the mode's spawns and markers, the intro camera, the sky's box), its ground, " +
                   "navigation, stitch list, map list row, strings, pictures and spawn resources.",
            TextWrapping = TextWrapping.Wrap, Opacity = 0.75, Width = 300,
        });
        form.Buttons(window, "Make");
        if (window.ShowDialog() != true) return;

        string name = fileName.Text.Trim().ToLowerInvariant();
        string shown = displayName.Text.Trim();
        bool noGround = ground.SelectedIndex == 1;
        string start = template.SelectedItem as string ?? "";
        if (!noGround && start.Length == 0) { Log("New level: the game has no level with ground textures to paint the ground with."); return; }
        if (!Regex.IsMatch(name, "^[a-z][a-z0-9_]{1,30}$")) { Log("New level: the file name has to be lower case letters, digits and _ (2 to 31, a letter first)."); return; }
        if (shown.Length == 0) shown = name;
        var chosenMode = mode.SelectedIndex == 0 ? NewLevel.Mode.Ruin : NewLevel.Mode.Infestation;
        bool replace = false;
        if (File.Exists(Path.Combine(workspace.OverridesFolder, $@"build\pc\cache\{name}.vpp_pc")))
        {
            if (MessageBox.Show(this, $"There is already a level {name} made by the editor. Make it again, new? Its edits are lost.",
                                "New level", MessageBoxButton.OKCancel, MessageBoxImage.Question) != MessageBoxResult.OK) return;
            foreach (var (key, _) in OpenTabs($@"level:build\pc\cache\{name}.vpp_pc")) CloseTab(key);
            replace = true;
        }
        string chosenSky = string.IsNullOrWhiteSpace(sky.Text) ? NewLevel.DefaultSky : sky.Text.Trim();
        await MakeNewLevel(chosenMode, start, name, shown, picture.Text.Length > 0 ? picture.Text : null, replace, noGround,
                           sizes[Math.Max(0, groundSize.SelectedIndex)], chosenSky);
    }

    /// <summary>Makes a new level and opens it (New level's dialog, or the game's menu): see <see cref="NewLevel.Make"/>.</summary>
    async Task MakeNewLevel(NewLevel.Mode chosenMode, string start, string name, string shown, string? picture, bool replace, bool noGround,
                            int size, string chosenSky)
    {
        Mouse.OverrideCursor = Cursors.Wait;
        try
        {
            List<string> report = new NewLevel(workspace, context.Assets).Make(chosenMode, start, name, shown, picture, replace, noGround, size, chosenSky);
            Log($"New level {name} ({chosenMode}):");
            foreach (string line in report) Log("  " + line);
            Log("  The game reads its map lists at start-up: restart it to see the level in the menu.");
            RefreshOverrideMarks();
            BuildTree();
            Task index = BuildSearchIndexAsync();
            string archive = $@"build\pc\cache\{name}.vpp_pc";
            Packfile pack = workspace.Archive(archive);
            if (pack.Find(name + ".rfgzone_pc") is { } zone) context.OpenLevel(new GameFile(pack, zone, archive, null));
            // Its ground, level geometry and sky are found through the file index, which is read again
            // (a minute) to have the new archive: the level is drawn again once it has.
            Mouse.OverrideCursor = null;
            Log("  Its ground shows once the editor has read the new level's files (a minute or so).");
            await index;
            foreach (var (_, document) in OpenTabs($@"level:build\pc\cache\{name}.vpp_pc"))
                if (document is LevelEditor editor) editor.FilesIndexedAgain();
        }
        catch (Exception ex) when (ex is InvalidDataException or IOException or UnauthorizedAccessException or NotSupportedException or ArgumentException or InvalidOperationException
                                       or System.Runtime.InteropServices.COMException or FormatException)
        {
            Log($"New level: {name} couldn't be made: {ex.Message}");
            EditorLog.Write($"  where: {ex}");
        }
        finally
        {
            Mouse.OverrideCursor = null;
        }
    }

    // ---- Delete level ------------------------------------------------------------------------------

    /// <summary>Deletes a level the editor made, all of it (see <see cref="NewLevel.Remove"/>).</summary>
    void OnDeleteLevel(object sender, RoutedEventArgs e)
    {
        List<NewLevel.MadeLevel> made = NewLevel.Made(workspace);
        if (made.Count == 0)
        {
            Log("Delete level: there are no levels made with New level.");
            return;
        }
        Window window = Dialog("Delete level");
        var form = new DialogForm(360);
        var list = new ListBox { ItemsSource = made, SelectionMode = SelectionMode.Extended, Height = 180, SelectedIndex = 0 };
        form.Row("Levels made", list, "The levels made with New level (and any left in no map list, or only their edits). Ctrl or Shift: more than one");
        form.Row("", new TextBlock
        {
            Text = "Deletes each from its map list, its spawn resources, its name in every language, its pictures, and its archive with all its edits. It can't be undone.",
            TextWrapping = TextWrapping.Wrap, Opacity = 0.75, Width = 360,
        });
        form.Buttons(window, "Delete");
        if (window.ShowDialog() != true) return;
        var chosen = list.SelectedItems.Cast<NewLevel.MadeLevel>().ToList();
        if (chosen.Count == 0) return;
        if (MessageBox.Show(this, $"Delete {string.Join(", ", chosen.Select(c => c.Name))} for good? This can't be undone.", "Delete level",
                            MessageBoxButton.OKCancel, MessageBoxImage.Warning) != MessageBoxResult.OK) return;
        foreach (NewLevel.MadeLevel level in chosen)
        {
            foreach (var (key, _) in OpenTabs($@"level:build\pc\cache\{level.Name}.vpp_pc")) CloseTab(key);
            try
            {
                Log($"Deleted level {level.Name}:");
                foreach (string line in new NewLevel(workspace, context.Assets).Remove(level.Name)) Log("  " + line);
            }
            catch (Exception ex) when (ex is InvalidDataException or IOException or UnauthorizedAccessException)
            {
                Log($"Delete level: {level.Name} couldn't be deleted: {ex.Message}");
            }
        }
        Log("  The game reads its map lists at start-up: restart it.");
        RefreshOverrideMarks();
        BuildTree();
        _ = BuildSearchIndexAsync();
    }

    // ---- Spawn resources ---------------------------------------------------------------------------

    /// <summary>A level with a row naming its spawn resources: which table, its key, the entry it names.</summary>
    sealed record ResourceUser(string Table, string Row, string KeyTag, string Key, string Kind, string Entry)
    {
        public override string ToString() => $"{Key}  ({Kind}: {Entry})";
    }

    List<ResourceUser> ResourceUsers()
    {
        var users = new List<ResourceUser>();
        foreach (var (table, row, key, kind) in new[]
                 {
                     ("wrecking_crew_maps.xtbl", "wc_map_list", "file_name", "Ruin"),
                     ("mp_levels.xtbl", "mp_level_list", "Filename", "Multiplayer"),
                     ("missions.xtbl", "mission", "Name", "Mission"),
                 })
        {
            string text;
            try { text = Encoding.Latin1.GetString(workspace.Read(TablesArchive, table)); }
            catch (Exception ex) when (ex is IOException or InvalidDataException) { continue; }
            foreach (Match m in Regex.Matches(text, $@"<{row}>.*?</{row}>", RegexOptions.Singleline))
            {
                string k = Regex.Match(m.Value, $@"<{key}>([^<]*)</{key}>").Groups[1].Value.Trim();
                string entry = Regex.Match(m.Value, @"<resource_reservation>([^<]*)</resource_reservation>").Groups[1].Value.Trim();
                if (k.Length > 0 && entry.Length > 0) users.Add(new ResourceUser(table, row, key, k, kind, entry));
            }
        }
        return users;
    }

    string? lastResourceLevel;

    void OnSpawnResources(object sender, RoutedEventArgs e)
    {
        string resources;
        List<string> characters, vehicles, exos;
        try
        {
            resources = Encoding.Latin1.GetString(workspace.Read(TablesArchive, "spawn_resource.xtbl"));
            characters = SpawnResource.RowNames(Encoding.Latin1.GetString(workspace.Read(TablesArchive, "character.xtbl")), "Character");
            vehicles = SpawnResource.RowNames(Encoding.Latin1.GetString(workspace.Read(TablesArchive, "vehicles.xtbl")), "Vehicle");
            exos = SpawnResource.RowNames(Encoding.Latin1.GetString(workspace.Read(TablesArchive, "exo.xtbl")), "Exo");
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException)
        {
            Log("Spawn resources: the game's tables couldn't be read: " + ex.Message);
            return;
        }
        List<ResourceUser> users = ResourceUsers();
        if (users.Count == 0) { Log("Spawn resources: no level names its spawn resources."); return; }

        Window window = Dialog("Spawn resources");
        var form = new DialogForm(380);
        var level = new ComboBox { ItemsSource = users, MaxDropDownHeight = 400 };
        var shared = new TextBlock { TextWrapping = TextWrapping.Wrap, Opacity = 0.75 };
        var own = new CheckBox { Content = "Give this level its own (changing them changes no other level's)" };
        var population = new StackPanel { Orientation = Orientation.Horizontal };
        var sides = new Dictionary<string, TextBox>();
        foreach (string side in SpawnResource.Sides)
        {
            population.Children.Add(new TextBlock { Text = side, Margin = new Thickness(0, 3, 4, 0) });
            var box = new TextBox { Width = 36, Margin = new Thickness(0, 0, 10, 0) };
            sides[side] = box;
            population.Children.Add(box);
        }
        var rotate = new CheckBox { Content = "Rotate slots" };
        var look = new ComboBox { ItemsSource = characters, IsEditable = true, IsTextSearchEnabled = true, MaxDropDownHeight = 400 };

        // A list of names, with a choice to add and a button to take the picked ones out.
        (FrameworkElement View, List<string> Items, Action<IEnumerable<string>> Set) NameList(List<string> choices, string what)
        {
            var items = new List<string>();
            var box = new ListBox { Height = 110, SelectionMode = SelectionMode.Extended };
            var pick = new ComboBox { ItemsSource = choices, IsEditable = true, IsTextSearchEnabled = true, MaxDropDownHeight = 400 };
            var add = new Button { Content = "Add", MinWidth = 60, Margin = new Thickness(6, 0, 0, 0) };
            var remove = new Button { Content = "Remove", MinWidth = 60, Margin = new Thickness(6, 0, 0, 0) };
            void Show() { box.ItemsSource = null; box.ItemsSource = items; }
            add.Click += (_, _) =>
            {
                string name = (pick.Text ?? "").Trim();
                if (name.Length == 0 || items.Contains(name, StringComparer.OrdinalIgnoreCase)) return;
                if (!choices.Contains(name, StringComparer.OrdinalIgnoreCase)) Log($"Spawn resources: {name} isn't one of the game's {what} (added anyway).");
                items.Add(name);
                Show();
            };
            remove.Click += (_, _) =>
            {
                foreach (string s in box.SelectedItems.Cast<string>().ToList()) items.Remove(s);
                Show();
            };
            var line = new DockPanel { Margin = new Thickness(0, 4, 0, 0) };
            DockPanel.SetDock(remove, Dock.Right);
            DockPanel.SetDock(add, Dock.Right);
            line.Children.Add(remove);
            line.Children.Add(add);
            line.Children.Add(pick);
            var panel = new StackPanel();
            panel.Children.Add(box);
            panel.Children.Add(line);
            return (panel, items, names => { items.Clear(); items.AddRange(names); Show(); });
        }
        var (npcView, npcItems, setNpcs) = NameList(characters, "characters (character.xtbl)");
        var (vehicleView, vehicleItems, setVehicles) = NameList(vehicles, "vehicles (vehicles.xtbl)");
        var (exoView, exoItems, setExos) = NameList(exos, "exos (exo.xtbl)");

        SpawnResource? showing = null;
        void ShowLevel()
        {
            if (level.SelectedItem is not ResourceUser user) return;
            showing = SpawnResource.Read(resources, user.Entry) ?? new SpawnResource { Name = user.Entry };
            int sharing = users.Count(u => u.Entry.Equals(user.Entry, StringComparison.OrdinalIgnoreCase));
            shared.Text = sharing > 1
                ? $"{user.Entry} is used by {sharing} levels: {string.Join(", ", users.Where(u => u.Entry.Equals(user.Entry, StringComparison.OrdinalIgnoreCase)).Select(u => u.Key).Take(8))}{(sharing > 8 ? "..." : "")}"
                : $"{user.Entry} is this level's only.";
            own.IsChecked = sharing > 1;
            own.IsEnabled = sharing > 1;
            setNpcs(showing.Characters);
            setVehicles(showing.Vehicles);
            setExos(showing.Exos);
            look.Text = showing.PlayerLook ?? "";
            rotate.IsChecked = showing.RotateSlots == true;
            foreach (var (side, box) in sides) box.Text = showing.Populations.GetValueOrDefault(side) ?? "";
        }
        level.SelectionChanged += (_, _) => ShowLevel();
        level.SelectedItem = users.FirstOrDefault(u => u.Key.Equals(lastResourceLevel, StringComparison.OrdinalIgnoreCase)) ?? users[0];

        form.Row("Level", level, "The level (its row in the Ruin, multiplayer or mission table) and the spawn resources it names");
        form.Row("", shared);
        form.Row("", own);
        form.Row("Characters", npcView, "The people it can spawn (character.xtbl)");
        form.Row("Vehicles", vehicleView, "The vehicles it can spawn (vehicles.xtbl)");
        form.Row("Exos", exoView, "The exos it can spawn (exo.xtbl)");
        form.Row("Player look", look, "The character the player is, here (empty: the default)");
        form.Row("Populations", population, "How many of each side's people may be about (empty: not given)");
        form.Row("", rotate);
        form.Buttons(window, "Save");
        if (window.ShowDialog() != true || level.SelectedItem is not ResourceUser chosen || showing == null) return;
        lastResourceLevel = chosen.Key;

        var made = new SpawnResource { Name = chosen.Entry, PlayerLook = look.Text.Trim().Length > 0 ? look.Text.Trim() : null };
        made.Characters.AddRange(npcItems);
        made.Vehicles.AddRange(vehicleItems);
        made.Exos.AddRange(exoItems);
        made.RotateSlots = rotate.IsChecked == true ? true : showing.RotateSlots == null ? null : false;
        foreach (var (side, box) in sides)
            if (box.Text.Trim().Length > 0) made.Populations[side] = box.Text.Trim();
        try
        {
            if (own.IsChecked == true && own.IsEnabled)
            {
                var names = SpawnResource.Names(resources).ToHashSet(StringComparer.OrdinalIgnoreCase);
                string name = chosen.Key;
                for (int n = 2; names.Contains(name); n++) name = $"{chosen.Key}_{n}";
                made.Name = name;
                string table = Encoding.Latin1.GetString(workspace.Read(TablesArchive, chosen.Table));
                string changed = LevelFiles.WithRowValue(table, chosen.Row, chosen.KeyTag, chosen.Key, "resource_reservation", name)
                                 ?? throw new InvalidDataException($"{chosen.Table}'s row for {chosen.Key} couldn't be changed");
                Log("Spawn resources: written" + SpawnResourceFiles.Write(workspace, made, chosen.Entry) + ".");
                workspace.WriteOverride(TablesArchive, chosen.Table, Encoding.Latin1.GetBytes(changed));
                Log($"Spawn resources: {chosen.Key} has its own now, \"{name}\" (from {chosen.Entry}; {chosen.Table} names it).");
            }
            else
            {
                Log("Spawn resources: written" + SpawnResourceFiles.Write(workspace, made) + ".");
                Log($"Spawn resources: {chosen.Entry} saved.");
            }
            LevelEditor.MarkSpawnResourcesChosen(made.Name);  // (the level editor won't take any of it out)
            Log($"  {made.Characters.Count} characters, {made.Vehicles.Count} vehicles, {made.Exos.Count} exos{(made.PlayerLook != null ? ", the player as " + made.PlayerLook : "")}. " +
                "The game reads its tables at start-up: restart it for them to apply.");
            RefreshOverrideMarks();
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException or UnauthorizedAccessException)
        {
            Log("Spawn resources: couldn't be saved: " + ex.Message);
        }
    }
}
