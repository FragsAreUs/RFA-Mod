using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using RfaFormats.Interface;

namespace RfaEditor;

/// <summary>Where a document is: its archive, its container (or none: loose in the archive) and its file name.</summary>
sealed record VintFile(string Archive, string? Container, string FileName);

/// <summary>Where an interface document's pieces come from, and where its changes go (see FilePreview.Vint.cs).</summary>
sealed class VintSources
{
    /// <summary>A texture by its name (with .tga), as a picture at full size; null if not found.</summary>
    public required Func<string, BitmapSource?> Texture { get; init; }

    /// <summary>The pictures cut out of sheet textures (bitmap_sheets.xtbl), by name.</summary>
    public required IReadOnlyDictionary<string, (string Sheet, int X, int Y, int Width, int Height)> Sheets { get; init; }

    /// <summary>The game's English text by its tag's hash.</summary>
    public required IReadOnlyDictionary<uint, string> Strings { get; init; }

    /// <summary>Each font's line height in pixels (from its .vf3_pc), by name.</summary>
    public required IReadOnlyDictionary<string, int> FontHeights { get; init; }

    /// <summary>One of the game's fonts by name (font_body, ...), or null.</summary>
    public required Func<string, GameFont?> Font { get; init; }

    /// <summary>Another document by name (a sub-document an element of type "document" shows) and where it is, or null.</summary>
    public required Func<string, (VintTree Document, VintFile File)?> Document { get; init; }

    /// <summary>This document's file.</summary>
    public required VintFile File { get; init; }

    /// <summary>Writes changed documents where the game reads them (overrides\, containers rebuilt); returns what was done.</summary>
    public required Func<IReadOnlyList<(VintFile File, byte[] Bytes)>, string> Save { get; init; }

    /// <summary>Puts the game's own copy of this document back; returns what was done.</summary>
    public required Func<string> Restore { get; init; }

    /// <summary>Whether the game reads a changed copy of this document (or of its container).</summary>
    public required Func<bool> IsChanged { get; init; }

    /// <summary>A script's text by its file name, or null.</summary>
    public required Func<string, string?> Script { get; init; }

    /// <summary>Reads this document again, as the game will see it now (after its files changed); null if it can't.</summary>
    public required Func<VintTree?> Reload { get; init; }

    /// <summary>The document's name, as the game's vint_doc_reload takes it.</summary>
    public required string DocumentName { get; init; }

    /// <summary>The file names whose change (in the mod folder) means the document should be read again.</summary>
    public required IReadOnlyList<string> WatchNames { get; init; }

    public required string ModFolder { get; init; }
    public required Action<string> Log { get; init; }
}

/// <summary>
/// An interface document (.vint_doc: a menu or a HUD screen) drawn from its elements as the game lays them out, with its
/// element tree (add, copy and delete elements), the picked element's properties (change, add or remove one and the
/// screen is drawn again at once; <b>Save</b> writes the document anew in overrides\), and its Lua script beside it, the element names it uses linked both ways. While it shows,
/// saving its document or script in the mod folder (overrides\, mods\) draws it again from the new files, and
/// <b>Reload in game</b> has the running game load it again. Drawing: VintView.Render.cs; the script: VintView.Script.cs.
/// </summary>
public partial class VintView : UserControl
{
    VintSources? sources;
    VintTree? document;
    VintScreen? screen;
    readonly Dictionary<string, (VintTree, VintFile)?> subTrees = new(StringComparer.OrdinalIgnoreCase);  // kept until saved or undone
    readonly HashSet<VintTree> touched = [];  // documents changed here and not saved
    readonly Dictionary<string, VintScreen?> subScreens = new(StringComparer.OrdinalIgnoreCase);
    readonly Dictionary<string, VintFile> subFiles = new(StringComparer.OrdinalIgnoreCase);
    readonly HashSet<VintNode> changed = [];
    readonly HashSet<string> hiddenHere = [];  // elements (by path) unticked in the tree: left out of the drawing only
    VintNode? selected;
    string? selectedPath;
    FileSystemWatcher? watcher;
    readonly DispatcherTimer reloadTimer;
    bool filling;  // setting the controls ourselves, not the user

    public VintView()
    {
        InitializeComponent();
        reloadTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        reloadTimer.Tick += (_, _) =>
        {
            reloadTimer.Stop();
            ReloadFromFiles();
        };
    }

    // ---- Showing a document ------------------------------------------------------------------------------

    internal void Show(VintSources sources, VintTree document)
    {
        Hide();
        this.sources = sources;
        this.document = document;
        subScreens.Clear();
        subTrees.Clear();
        touched.Clear();
        changed.Clear();
        fonts.Clear();
        glyphPictures.Clear();
        hiddenHere.Clear();
        selected = null;
        selectedPath = null;
        Layout(VintScreen.DefaultResolution);
        Watch();
        StatusText.Text = sources.IsChanged() ? "The game reads your changed copy of this document." : "";
        ShowButtons();
    }

    void ShowButtons()
    {
        SaveButton.IsEnabled = touched.Count > 0;
        RestoreButton.IsEnabled = sources?.IsChanged() == true;
    }

    // Lays the document out at a resolution and shows it all again.
    void Layout(string resolution)
    {
        if (document == null) return;
        screen = VintScreen.Read(document, resolution);
        subScreens.Clear();
        subFiles.Clear();
        filling = true;
        ResolutionList.ItemsSource = screen.Resolutions;
        ResolutionList.SelectedItem = screen.Resolutions.FirstOrDefault(r => r.Equals(resolution, StringComparison.OrdinalIgnoreCase));
        filling = false;
        var (w, h) = Size(resolution);
        Screen.Width = w;
        Screen.Height = h;
        LoadScript();
        BuildTree();
        LoadAnimations();
        Draw();
        Reselect();
    }

    static (double Width, double Height) Size(string resolution)
    {
        var parts = resolution.ToLowerInvariant().Split('x');
        return parts.Length == 2 && double.TryParse(parts[0], out double w) && double.TryParse(parts[1], out double h) ? (w, h) : (1280, 720);
    }

    /// <summary>Draws it again (when textures became findable).</summary>
    public void Redraw()
    {
        if (screen != null) Draw();
    }

    public void Hide()
    {
        StopAnimation();
        watcher?.Dispose();
        watcher = null;
        reloadTimer.Stop();
        Screen.Children.Clear();
        Overlay.Children.Clear();
        ElementTree.Items.Clear();
        PropertyPanel.Children.Clear();
        ScriptBox.Document = new System.Windows.Documents.FlowDocument();
        screen = null;
        document = null;
    }

    void OnResolutionChosen(object sender, SelectionChangedEventArgs e)
    {
        if (filling || ResolutionList.SelectedItem is not string resolution) return;
        ApplyChanges();  // kept in the documents' trees, so they show at the other resolution too
        Layout(resolution);
    }

    // The values changed here written into their documents' trees.
    void ApplyChanges()
    {
        foreach (VintNode node in changed) ScreenOf(node)?.Apply(node);
        changed.Clear();
    }

    // The screen (this document's, or a sub-document's) an element is one of.
    VintScreen? ScreenOf(VintNode node) =>
        screen != null && screen.Holds(node) ? screen : subScreens.Values.FirstOrDefault(s => s != null && s.Holds(node));

    void Changed(VintNode node)
    {
        changed.Add(node);
        if (ScreenOf(node) is { } owner) touched.Add(owner.Tree);
        ShowButtons();
    }

    void OnRedraw(object sender, RoutedEventArgs e) => Draw();

    void OnUndoChanges(object sender, RoutedEventArgs e)
    {
        ReloadFromFiles();
        StatusText.Text = "Back to the document as it is in its file.";
    }

    // ---- Saving ----------------------------------------------------------------------------------------------

    // Writes the changed values into the document, and into each sub-document changed here, and saves them.
    void OnSave(object sender, RoutedEventArgs e)
    {
        if (sources == null || screen == null || document == null || touched.Count == 0) return;
        ApplyChanges();
        var writes = new List<(VintFile, byte[])>();
        if (touched.Contains(document)) writes.Add((sources.File, document.ToBytes()));
        foreach (var (name, entry) in subTrees)
            if (entry is var (tree, file) && touched.Contains(tree)) writes.Add((file, tree.ToBytes()));
        string done;
        try { done = sources.Save(writes); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            StatusText.Text = "It can't be saved: " + ex.Message;
            return;
        }
        ReloadFromFiles();
        StatusText.Text = "Saved: " + done + " Reload in game to see it there.";
        sources.Log("Interface preview: " + StatusText.Text);
        ShowButtons();
    }

    void OnRestore(object sender, RoutedEventArgs e)
    {
        if (sources == null) return;
        if (MessageBox.Show(Window.GetWindow(this)!, $"Go back to the game's own {sources.File.FileName}? Your saved changes to it are dropped.",
                            "Restore original", MessageBoxButton.OKCancel, MessageBoxImage.Question) != MessageBoxResult.OK) return;
        string done;
        try { done = sources.Restore(); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            StatusText.Text = "It can't be restored: " + ex.Message;
            return;
        }
        ReloadFromFiles();
        StatusText.Text = done;
    }

    // ---- Adding, copying and deleting elements ----------------------------------------------------------------

    // A menu of the element types; the one chosen is added inside the picked element (or at the top).
    void OnAddElement(object sender, RoutedEventArgs e)
    {
        var menu = new ContextMenu { PlacementTarget = AddButton, Placement = System.Windows.Controls.Primitives.PlacementMode.Bottom };
        foreach (string type in VintScreen.ElementTypes.Where(t => t != "video"))
        {
            var item = new MenuItem { Header = type switch
            {
                "group" => "Group (holds others)",
                "bitmap" => "Picture",
                "text" => "Text",
                "clip" => "Clip (cuts what is inside to a box)",
                "document" => "Sub-document (shows another document)",
                "bitmap_circle" => "Circle picture (a gauge)",
                "map" => "Map",
                _ => type,
            } };
            item.Click += (_, _) => AddElement(type);
            menu.Items.Add(item);
        }
        menu.IsOpen = true;
    }

    void AddElement(string type)
    {
        VintScreen? owner = selected != null ? ScreenOf(selected) : screen;
        if (owner == null) return;
        ApplyChanges();
        var names = new HashSet<string>(owner.All().Select(n => n.Name), StringComparer.OrdinalIgnoreCase);
        string name = "new_" + type;
        for (int i = 2; names.Contains(name); i++) name = $"new_{type}_{i}";
        VintNode added = owner.Add(selected, type, name);
        touched.Add(owner.Tree);
        Reshaped(added, $"Added {name} ({type}){(selected != null ? " in " + selected.Name : "")}: not saved yet.");
    }

    void OnCopyElement(object sender, RoutedEventArgs e)
    {
        if (selected == null || ScreenOf(selected) is not { } owner) return;
        ApplyChanges();
        VintNode copy = owner.Copy(selected);
        touched.Add(owner.Tree);
        Reshaped(copy, $"Copied {selected.Name} as {copy.Name}: not saved yet.");
    }

    void OnDeleteElement(object sender, RoutedEventArgs e)
    {
        if (selected == null || ScreenOf(selected) is not { } owner) return;
        VintNode gone = selected;
        int tweens = owner.Animations.SelectMany(a => a.Tweens).Count(t => t.Target.Equals(gone.Name, StringComparison.OrdinalIgnoreCase));
        var reasons = new List<string>();
        if (scriptNames.Contains(gone.Name)) reasons.Add("the script names it (it may fail without it)");
        if (tweens > 0) reasons.Add($"{tweens} tweens move it");
        if (reasons.Count > 0 &&
            MessageBox.Show(Window.GetWindow(this)!, $"{gone.Name}: {string.Join("; ", reasons)}. Delete it all the same?",
                            "Delete element", MessageBoxButton.OKCancel, MessageBoxImage.Warning) != MessageBoxResult.OK) return;
        ApplyChanges();
        owner.Delete(gone);
        touched.Add(owner.Tree);
        Reshaped(gone.Parent, $"Deleted {gone.Name}: not saved yet.");
    }

    // After the tree changed shape: the tree, the drawing and the pick shown again.
    void Reshaped(VintNode? pick, string status)
    {
        BuildTree();
        Draw();
        if (pick != null) Select(pick);
        else
        {
            selected = null;
            selectedPath = null;
            PropertyPanel.Children.Clear();
            Overlay.Children.Clear();
        }
        StatusText.Text = status;
        ShowButtons();
    }

    // ---- The tree ------------------------------------------------------------------------------------------

    void BuildTree()
    {
        ElementTree.Items.Clear();
        if (screen == null) return;
        foreach (VintNode root in screen.Roots) ElementTree.Items.Add(TreeItem(root, 0));
    }

    TreeViewItem TreeItem(VintNode node, int nesting)
    {
        var header = new TextBlock();
        header.Inlines.Add(new System.Windows.Documents.Run(node.Name));
        string tags = node.Type + (node.Visible ? "" : ", hidden") + (scriptNames.Contains(node.Name) ? ", in the script" : "");
        header.Inlines.Add(new System.Windows.Documents.Run("  " + tags) { Foreground = (Brush)FindResource("DimTextBrush"), FontSize = 11 });
        if (!node.Visible) header.Opacity = 0.65;
        // A box to leave it out of the drawing (only here: the document keeps it).
        var shown = new CheckBox
        {
            IsChecked = !hiddenHere.Contains(node.Path),
            Margin = new Thickness(0, 0, 4, 0),
            VerticalAlignment = VerticalAlignment.Center,
            ToolTip = "Draw this element (untick to leave it out of the picture here; the document keeps it)",
        };
        shown.Click += (_, _) =>
        {
            if (shown.IsChecked == true) hiddenHere.Remove(node.Path);
            else hiddenHere.Add(node.Path);
            Draw();
        };
        var row = new StackPanel { Orientation = Orientation.Horizontal };
        row.Children.Add(shown);
        row.Children.Add(header);
        var item = new TreeViewItem { Header = row, Tag = node, IsExpanded = nesting < 2 };
        foreach (VintNode child in node.Children) item.Items.Add(TreeItem(child, nesting + 1));
        // A sub-document's own elements, under its element.
        if (node.Type == "document" && SubScreen(node) is { } sub && nesting < 6)
            foreach (VintNode root in sub.Roots) item.Items.Add(TreeItem(root, nesting + 1));
        return item;
    }

    void OnElementChosen(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        if (filling || ElementTree.SelectedItem is not TreeViewItem { Tag: VintNode node }) return;
        Select(node, fromTree: true);
    }

    void Select(VintNode node, bool fromTree = false)
    {
        selected = node;
        selectedPath = node.Path;
        if (!fromTree) SelectInTree(node);
        ShowProperties();
        DrawSelection();
        ShowInScript(node.Name);
    }

    void SelectInTree(VintNode node)
    {
        TreeViewItem? Find(ItemCollection items)
        {
            foreach (TreeViewItem item in items.OfType<TreeViewItem>())
            {
                if (item.Tag == node) return item;
                if (Find(item.Items) is { } found)
                {
                    item.IsExpanded = true;
                    return found;
                }
            }
            return null;
        }
        if (Find(ElementTree.Items) is not { } target) return;
        filling = true;
        target.IsSelected = true;
        target.BringIntoView();
        filling = false;
    }

    // After the document was read again, the element picked before (by its path), if it is still there.
    void Reselect()
    {
        if (selectedPath == null || screen == null) return;
        if (AllNodes().FirstOrDefault(n => n.Path == selectedPath) is { } again) Select(again);
        else
        {
            selected = null;
            PropertyPanel.Children.Clear();
        }
    }

    // Every element, the sub-documents' too.
    IEnumerable<VintNode> AllNodes()
    {
        if (screen == null) yield break;
        var stack = new Stack<VintNode>(Enumerable.Reverse(screen.Roots));
        while (stack.Count > 0)
        {
            VintNode node = stack.Pop();
            yield return node;
            for (int i = node.Children.Count - 1; i >= 0; i--) stack.Push(node.Children[i]);
            if (node.Type == "document" && SubScreen(node) is { } sub)
                foreach (VintNode root in Enumerable.Reverse(sub.Roots)) stack.Push(root);
        }
    }

    // ---- The picked element's properties ----------------------------------------------------------------------

    void ShowProperties()
    {
        PropertyPanel.Children.Clear();
        if (selected is not { } node) return;
        PropertiesTitle.Text = "PROPERTIES OF " + node.Name.ToUpperInvariant();
        PropertyPanel.Children.Add(new TextBlock
        {
            Text = $"{node.Type}, at {node.Path}",
            Foreground = (Brush)FindResource("DimTextBrush"),
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 0, 0, 6),
        });
        // Its name (the scripts find elements by it).
        var nameRow = new DockPanel { Margin = new Thickness(0, 0, 0, 6) };
        var nameLabel = new TextBlock { Text = "name", Width = 110, VerticalAlignment = VerticalAlignment.Center, ToolTip = "Its name: the scripts find it by this" };
        DockPanel.SetDock(nameLabel, Dock.Left);
        var nameBox = new TextBox { Text = node.Name };
        void Rename()
        {
            string typed = nameBox.Text.Trim();
            if (typed.Length == 0 || typed == node.Name) return;
            string old = node.Name;
            node.Name = typed;
            selectedPath = node.Path;
            Changed(node);
            int moved = ScreenOf(node)?.Retarget(old, typed) ?? 0;  // its tweens follow it
            BuildTree();
            SelectInTree(node);
            PropertiesTitle.Text = "PROPERTIES OF " + node.Name.ToUpperInvariant();
            StatusText.Text = $"Renamed to {typed}: not saved yet." + (moved > 0 ? $" {moved} tweens moved with it." : "") +
                              (scriptNames.Contains(old) ? " The script still names it by its old name." : "");
            LoadAnimations();
        }
        nameBox.KeyDown += (_, e) =>
        {
            if (e.Key != Key.Enter) return;
            Rename();
            e.Handled = true;
        };
        nameBox.LostFocus += (_, _) => Rename();
        nameRow.Children.Add(nameLabel);
        nameRow.Children.Add(nameBox);
        PropertyPanel.Children.Add(nameRow);
        // Visible: a box, added to the element if it hasn't the property.
        var visible = new CheckBox { Content = "Visible", IsChecked = node.Visible, Margin = new Thickness(0, 0, 0, 6) };
        visible.Click += (_, _) =>
        {
            node.Values["visible"] = new VintValue(5, [visible.IsChecked == true ? 1 : 0], null);
            node.Removed.Remove("visible");
            Changed(node);
            Draw();
        };
        PropertyPanel.Children.Add(visible);
        if (node.Type == "text")
            PropertyPanel.Children.Add(new TextBlock
            {
                Text = "Shows: " + TextOf(node),
                TextWrapping = TextWrapping.Wrap,
                Margin = new Thickness(0, 0, 0, 6),
            });
        foreach (var (name, value) in node.Values.OrderBy(p => p.Key, StringComparer.OrdinalIgnoreCase))
        {
            if (name == "visible") continue;
            var row = new DockPanel { Margin = new Thickness(0, 0, 0, 4) };
            var label = new TextBlock { Text = name, Width = 110, VerticalAlignment = VerticalAlignment.Center, ToolTip = PropertyHelp(name) };
            DockPanel.SetDock(label, Dock.Left);
            var box = new TextBox { Text = value.ToString() };
            void Apply()
            {
                if (box.Text == node.Values[name].ToString()) return;
                if (node.Values[name].Parse(box.Text) is { } parsed)
                {
                    node.Values[name] = parsed;
                    Changed(node);
                    box.ClearValue(BorderBrushProperty);
                    Draw();
                    StatusText.Text = $"{node.Name}: {name} = {parsed} (not saved yet).";
                }
                else box.BorderBrush = (Brush)FindResource("ErrorBrush");
            }
            box.KeyDown += (_, e) =>
            {
                if (e.Key != Key.Enter) return;
                Apply();
                e.Handled = true;
            };
            box.LostFocus += (_, _) => Apply();
            // × takes the value off the element (the game then uses its default).
            var remove = new Button { Content = "\u00D7", Width = 24, Margin = new Thickness(4, 0, 0, 0), ToolTip = "Remove this value from the element (the game uses its default)" };
            DockPanel.SetDock(remove, Dock.Right);
            remove.Click += (_, _) =>
            {
                node.Values.Remove(name);
                node.Removed.Add(name);
                Changed(node);
                Draw();
                ShowProperties();
                StatusText.Text = $"{node.Name}: {name} removed (not saved yet).";
            };
            row.Children.Add(label);
            row.Children.Add(remove);
            row.Children.Add(box);
            PropertyPanel.Children.Add(row);
        }
        // A value it hasn't yet, from the properties known.
        var missing = VintScreen.Properties.Select(p => p.Name).Where(n => !node.Values.ContainsKey(n) && n != "visible").ToList();
        if (missing.Count > 0)
        {
            var addRow = new DockPanel { Margin = new Thickness(0, 6, 0, 0) };
            var addButton = new Button { Content = "Add", Margin = new Thickness(4, 0, 0, 0), ToolTip = "Give the element this value (it starts at a usual value)" };
            DockPanel.SetDock(addButton, Dock.Right);
            var list = new ComboBox { ItemsSource = missing, SelectedIndex = 0, ToolTip = "A property the element hasn't got" };
            addButton.Click += (_, _) =>
            {
                if (list.SelectedItem is not string adding || VintScreen.NewValue(adding) is not { } value) return;
                node.Values[adding] = value;
                node.Removed.Remove(adding);
                Changed(node);
                Draw();
                ShowProperties();
                StatusText.Text = $"{node.Name}: {adding} added (not saved yet).";
            };
            addRow.Children.Add(addButton);
            addRow.Children.Add(list);
            PropertyPanel.Children.Add(addRow);
        }
        // Where the script names it.
        if (scriptLines.TryGetValue(node.Name, out var lines) && lines.Count > 0)
        {
            PropertyPanel.Children.Add(new TextBlock { Text = "IN THE SCRIPT", Style = (Style)FindResource("SectionTitle"), Margin = new Thickness(0, 10, 0, 4) });
            var wrap = new WrapPanel();
            foreach (int line in lines.Distinct().Take(40))
            {
                var link = new Button { Content = $"line {line}", Margin = new Thickness(0, 0, 4, 4), ToolTip = "Show this line of the script" };
                link.Click += (_, _) => ShowScriptLine(line);
                wrap.Children.Add(link);
            }
            PropertyPanel.Children.Add(wrap);
        }
    }

    static string PropertyHelp(string name) => name switch
    {
        "anchor" => "Its place in its parent (pixels)",
        "offset" => "Where its picture or text starts from its anchor (pixels)",
        "auto_offset" => "Where it is held, when it has no offset: nw, n, ne, w, c, e, sw, s, se",
        "scale" => "Its size (x, y); a negative one flips it",
        "rotation" => "Its turn, in radians",
        "tint" => "Its colour (red, green, blue, 0 to 1), multiplied into its children",
        "alpha" => "How solid it is (0 to 1), multiplied into its children",
        "depth" => "Its order among its siblings: the deepest are drawn first, behind",
        "image" => "Its picture: a piece of a sheet texture (bitmap_sheets.xtbl) or a whole texture",
        "source_se" => "Its picture's size in pixels",
        "text_tag" => "Its text: a tag of the game's strings, or the words themselves",
        "font" => "Its font (font_body, font_header, ...)",
        "text_scale" => "Its text's size (x, y)",
        "document_name" => "The document it shows inside it",
        _ => name,
    };

    // ---- Live: its files saved, the game told ---------------------------------------------------------------

    void Watch()
    {
        if (sources == null || !Directory.Exists(sources.ModFolder)) return;
        try
        {
            watcher = new FileSystemWatcher(sources.ModFolder)
            {
                IncludeSubdirectories = true,
                NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.Size,
            };
            void Changed(object _, FileSystemEventArgs e)
            {
                string name = Path.GetFileName(e.FullPath);
                if (sources.WatchNames.Any(w => w.Equals(name, StringComparison.OrdinalIgnoreCase)))
                    Dispatcher.BeginInvoke(() =>
                    {
                        reloadTimer.Stop();
                        reloadTimer.Start();  // wait for the writing to finish
                    });
            }
            watcher.Changed += Changed;
            watcher.Created += Changed;
            watcher.Renamed += (s, e) => Changed(s, e);
            watcher.EnableRaisingEvents = true;
        }
        catch (Exception e) when (e is IOException or ArgumentException or UnauthorizedAccessException)
        {
            sources.Log($"Interface preview: can't watch {sources.ModFolder} for changes ({e.Message})");
        }
    }

    void ReloadFromFiles()
    {
        if (sources == null) return;
        VintTree? again;
        try { again = sources.Reload(); }
        catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException) { again = null; }
        if (again == null)
        {
            StatusText.Text = $"Its files changed, but it can't be read now ({DateTime.Now:HH:mm:ss}).";
            return;
        }
        document = again;
        changed.Clear();
        touched.Clear();
        subTrees.Clear();
        Layout(screen?.Resolution ?? VintScreen.DefaultResolution);
        StatusText.Text = $"Drawn again from your saved files at {DateTime.Now:HH:mm:ss}.";
        ShowButtons();
    }

    void OnReloadInGame(object sender, RoutedEventArgs e)
    {
        if (sources == null) return;
        try
        {
            new GameLink(sources.ModFolder).Send([$"vint_doc_reload {sources.DocumentName}"]);
            StatusText.Text = $"Sent to the game: vint_doc_reload {sources.DocumentName}. It loads the document again from your files " +
                              (touched.Count > 0 ? "(save your changes first: it sees only what is saved)." : "(what you have saved).");
        }
        catch (IOException ex)
        {
            StatusText.Text = "The game can't be told: " + ex.Message;
        }
    }
}
