using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Media3D;
using System.Windows.Threading;

namespace RfaEditor;

/// <summary>Something the Assets panel lists, to add to the zone.</summary>
/// <param name="what">
/// What is added: for the level editor a zone object (a copy of it), a schema class (a new one of
/// it), a chunk or a piece of level geometry. The panel only shows it; its document adds it.
/// </param>
sealed class AssetItem(string icon, Brush colour, string name, string? detail, string note, string about, string tip, object what)
{
    public string Icon { get; } = icon;
    public Brush Colour { get; } = colour;
    public string Name { get; } = name;

    /// <summary>A second, dim name after the first (with the space before it).</summary>
    public string Detail { get; } = detail is { Length: > 0 } ? "  " + detail : "";

    /// <summary>A short note at the row's right end (how many, which pack).</summary>
    public string Note { get; } = note;

    /// <summary>A line or two under the preview.</summary>
    public string About { get; } = about;

    public string Tip { get; } = tip;
    public object What { get; } = what;
}

/// <summary>A folder on the Assets panel's left: what is in it directly, and its own folders.</summary>
/// <param name="note">Said when it has nothing to list (and why).</param>
sealed class AssetFolder(string name, string icon, Brush? colour, IReadOnlyList<AssetItem> items, IReadOnlyList<AssetFolder>? folders = null, string? note = null)
{
    public string Name { get; } = name;
    public string Icon { get; } = icon;
    public Brush? Colour { get; } = colour;
    public IReadOnlyList<AssetItem> Items { get; } = items;
    public IReadOnlyList<AssetFolder> Folders { get; } = folders ?? [];
    public string? Note { get; } = note;

    /// <summary>Everything in it and in its folders, each once.</summary>
    public List<AssetItem> All()
    {
        var all = new List<AssetItem>(Items);
        var seen = new HashSet<AssetItem>(Items);
        foreach (AssetFolder folder in Folders)
            foreach (AssetItem item in folder.All())
                if (seen.Add(item)) all.Add(item);
        return all;
    }
}

/// <summary>What the Assets panel shows things from, and adds them to: a document (the level editor).</summary>
interface IAssetSource
{
    /// <summary>The folders, made again each time they are asked for (what the document has may have changed).</summary>
    IReadOnlyList<AssetFolder> AssetFolders();

    /// <summary>A thing's model for the preview, or null to show its icon.</summary>
    Model3DGroup? AssetPreview(AssetItem item);

    /// <summary>Adds a thing where the middle of the view points (double-click, Enter).</summary>
    void AddAsset(AssetItem item);
}

/// <summary>
/// Where the Assets panel was: which folders are open, which is chosen, what Find has, the thing
/// picked, and how far down each folder's list was scrolled. Shared by every level's panel (a level
/// opened next starts where the last one was) and kept between runs in assets_panel.txt next to the
/// editor (lines key=value: open=, chosen=, find=, picked=, scroll=offset|folder|find).
/// </summary>
static class AssetPlace
{
    static readonly string FilePath = System.IO.Path.Combine(AppContext.BaseDirectory, "assets_panel.txt");
    const int MostScrolls = 200;

    public static bool Known;                 // read from the file, or set since
    public static HashSet<string> Open = [];
    public static string? Chosen;
    public static string Find = "";
    public static string? Picked;             // the thing's name and detail
    public static readonly Dictionary<string, double> Scroll = [];  // "folder\nfind" -> how far down
    static DispatcherTimer? saveDelay;

    static AssetPlace()
    {
        try
        {
            if (!System.IO.File.Exists(FilePath)) return;
            foreach (string line in System.IO.File.ReadAllLines(FilePath))
            {
                int eq = line.IndexOf('=');
                if (eq <= 0) continue;
                string key = line[..eq], value = line[(eq + 1)..];
                switch (key)
                {
                    case "open": Open.Add(value); break;
                    case "chosen": Chosen = value; break;
                    case "find": Find = value; break;
                    case "picked": Picked = value; break;
                    case "scroll":
                        string[] parts = value.Split('|', 3);
                        if (parts.Length == 3 && double.TryParse(parts[0], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double offset))
                            Scroll[ScrollKey(parts[1], parts[2])] = offset;
                        break;
                }
            }
            Known = true;
        }
        catch (Exception ex) when (ex is System.IO.IOException or UnauthorizedAccessException)
        {
            EditorLog.Write("The Assets panel's place couldn't be read: " + ex.Message);
        }
    }

    public static string ScrollKey(string folder, string find) => folder + "\n" + find;

    /// <summary>Something changed: written a moment later (not at every key typed in Find, every step of a scroll).</summary>
    public static void Changed()
    {
        Known = true;
        if (saveDelay == null)
        {
            saveDelay = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(600) };
            saveDelay.Tick += (_, _) => Save();
            Dispatcher.CurrentDispatcher.ShutdownStarted += (_, _) => { if (saveDelay.IsEnabled) Save(); };
        }
        saveDelay.Stop();
        saveDelay.Start();
    }

    static void Save()
    {
        saveDelay?.Stop();
        try
        {
            var lines = new List<string> { "# Where the level editor's Assets panel was. The editor writes it." };
            lines.AddRange(Open.Order().Select(k => "open=" + k));
            if (Chosen != null) lines.Add("chosen=" + Chosen);
            if (Find.Length > 0) lines.Add("find=" + Find);
            if (Picked != null) lines.Add("picked=" + Picked);
            foreach ((string key, double offset) in Scroll.Where(s => s.Value > 0).Take(MostScrolls))
            {
                string[] parts = key.Split("\n", 2);
                lines.Add(string.Create(System.Globalization.CultureInfo.InvariantCulture, $"scroll={offset}|{parts[0]}|{parts[1]}"));
            }
            System.IO.File.WriteAllLines(FilePath, lines);
        }
        catch (Exception ex) when (ex is System.IO.IOException or UnauthorizedAccessException)
        {
            EditorLog.Write("The Assets panel's place couldn't be written: " + ex.Message);
        }
    }
}

/// <summary>
/// The Assets panel of a document: folders of things to add on the left, the chosen folder's
/// things in the middle (narrowed by Find), the picked one in 3D on the right. Double-click one
/// or press Enter to add it where the middle of the view points, or drag it into the view to put
/// it where you let go. What is dragged is the <see cref="AssetItem"/> itself
/// (<see cref="DragFormat"/>); the document's view takes the drop.
/// </summary>
public partial class AssetBrowser : UserControl
{
    /// <summary>The drag-and-drop format an <see cref="AssetItem"/> is carried in.</summary>
    internal static readonly string DragFormat = typeof(AssetItem).FullName!;

    /// <summary>Room left at the right end of a row for the scroll bar, which is drawn over the rows.</summary>
    internal const double ScrollBarRoom = 14;

    readonly IAssetSource source;
    readonly ModelViewer preview;
    readonly DispatcherTimer previewDelay = new() { Interval = TimeSpan.FromMilliseconds(120) };
    List<AssetItem> chosenItems = [];
    string chosenTitle = "";
    bool stale = true;
    bool refreshQueued;
    Point? pressedAt;
    AssetItem? pressedItem;
    bool restoring;           // the lists are being made again: what changes isn't the user moving
    bool scrollPending;       // the list was filled anew and its place not put back yet
    ScrollViewer? listScroll;

    internal AssetBrowser(IAssetSource source)
    {
        InitializeComponent();
        this.source = source;
        preview = new ModelViewer(PreviewViewport, PreviewArea);
        previewDelay.Tick += (_, _) => { previewDelay.Stop(); ShowPreview(); };
        IsVisibleChanged += (_, _) => { if (IsVisible && stale) Refresh(); };
        Sources.AddHandler(TreeViewItem.ExpandedEvent, new RoutedEventHandler(OnFolderOpenedOrClosed));
        Sources.AddHandler(TreeViewItem.CollapsedEvent, new RoutedEventHandler(OnFolderOpenedOrClosed));
        Items.AddHandler(ScrollViewer.ScrollChangedEvent, new ScrollChangedEventHandler(OnListScrolled));
        ShowPreview();
    }

    /// <summary>
    /// What the document has changed (objects added or deleted, the game's files read): the folders
    /// are made again, now if the panel is showing, else when it is next shown.
    /// </summary>
    internal void MarkStale()
    {
        stale = true;
        if (!IsVisible || refreshQueued) return;
        refreshQueued = true;
        Dispatcher.InvokeAsync(() => { refreshQueued = false; if (stale && IsVisible) Refresh(); }, DispatcherPriority.Background);
    }

    // ---- Folders -------------------------------------------------------------------------------

    /// <summary>A folder's row's tag: the folder, and its place (names joined by /; the root's is empty).</summary>
    sealed record FolderTag(AssetFolder Folder, string Key);

    /// <summary>Makes the folders again, keeping which are open, which is chosen and the picked thing.</summary>
    void Refresh()
    {
        using (HangWatch.While("making the Assets panel's lists")) RefreshNow();
    }

    void RefreshNow()
    {
        stale = false;
        IReadOnlyList<AssetFolder> folders = source.AssetFolders();
        // The first time, where the panel was last (in this level or another, this run or the last: see
        // AssetPlace); after that, as it is now. (At the very first, the top folders are open, so their
        // ways of sorting show.)
        bool first = Sources.Items.Count == 0;
        var open = new HashSet<string>(!first ? OpenFolders() : AssetPlace.Known ? AssetPlace.Open : folders.Select(f => f.Name));
        string? chosen = !first ? ((Sources.SelectedItem as TreeViewItem)?.Tag is FolderTag tag ? tag.Key : null) : AssetPlace.Chosen;
        object? picked = (Items.SelectedItem as AssetItem)?.What;
        string? pickedName = first ? AssetPlace.Picked : null;
        restoring = true;
        try
        {
            if (first && FindBox.Text.Length == 0 && AssetPlace.Find.Length > 0) FindBox.Text = AssetPlace.Find;
            Rebuild(folders, open, chosen, picked, pickedName);
        }
        finally
        {
            restoring = false;
        }
        AssetPlace.Open = OpenFolders().ToHashSet();
    }

    void Rebuild(IReadOnlyList<AssetFolder> folders, HashSet<string> open, string? chosen, object? picked, string? pickedName)
    {

        var everything = new AssetFolder("Everything", Icons.ShowAll, null, [], folders);
        Sources.Items.Clear();
        TreeViewItem root = FolderItem(everything, "", open, isRoot: true);
        Sources.Items.Add(root);
        foreach (AssetFolder folder in folders) Sources.Items.Add(FolderItem(folder, folder.Name, open));

        TreeViewItem? again = chosen == null ? null : Find(Sources.Items, chosen);
        (again ?? (TreeViewItem)Sources.Items[Math.Min(1, Sources.Items.Count - 1)]).IsSelected = true;  // (lists it: OnSourceChosen)
        AssetItem? same = picked != null ? chosenItems.FirstOrDefault(i => i.What.Equals(picked))
                        : pickedName != null ? chosenItems.FirstOrDefault(i => NameOf(i) == pickedName) : null;
        if (same != null && Items.Items.Contains(same))
        {
            Items.SelectedItem = same;
            // Where the list was scrolled is put back (ListChosen); with nowhere kept, the picked thing is shown.
            if (!AssetPlace.Scroll.ContainsKey(CurrentScrollKey())) Items.ScrollIntoView(same);
        }
    }

    /// <summary>A thing as remembered between levels and runs (its object isn't the same one): its name and detail.</summary>
    static string NameOf(AssetItem item) => item.Name + item.Detail;

    string CurrentScrollKey() => AssetPlace.ScrollKey((Sources.SelectedItem as TreeViewItem)?.Tag is FolderTag tag ? tag.Key : "", FindBox.Text.Trim());

    void OnFolderOpenedOrClosed(object sender, RoutedEventArgs e)
    {
        if (restoring) return;
        AssetPlace.Open = OpenFolders().ToHashSet();
        AssetPlace.Changed();
    }

    void OnListScrolled(object sender, ScrollChangedEventArgs e)
    {
        if (restoring || scrollPending || e.VerticalChange == 0) return;
        AssetPlace.Scroll[CurrentScrollKey()] = e.VerticalOffset;
        AssetPlace.Changed();
    }

    /// <summary>The list was filled anew: once it is laid out, it is scrolled to where it was for this folder and Find.</summary>
    void PutBackScroll()
    {
        scrollPending = true;
        Dispatcher.InvokeAsync(() =>
        {
            listScroll ??= Descendant<ScrollViewer>(Items);
            if (listScroll != null && AssetPlace.Scroll.TryGetValue(CurrentScrollKey(), out double offset)) listScroll.ScrollToVerticalOffset(offset);
            Dispatcher.InvokeAsync(() => scrollPending = false, DispatcherPriority.Background);
        }, DispatcherPriority.Loaded);
    }

    static T? Descendant<T>(DependencyObject parent) where T : DependencyObject
    {
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(parent); i++)
        {
            DependencyObject child = VisualTreeHelper.GetChild(parent, i);
            if (child is T found) return found;
            if (Descendant<T>(child) is T inside) return inside;
        }
        return null;
    }

    /// <summary>A folder's row with its folders under it; its tag is the folder and its place (names joined by /).</summary>
    static TreeViewItem FolderItem(AssetFolder folder, string key, HashSet<string> open, bool isRoot = false)
    {
        int count = folder.All().Count;
        var header = new DockPanel();
        TextBlock icon = Theme.Icon(folder.Icon, 13, folder.Colour ?? Theme.Brush("DimTextBrush"));
        icon.Width = 18;
        icon.Margin = new Thickness(0, 0, 4, 0);
        DockPanel.SetDock(icon, Dock.Left);
        header.Children.Add(icon);
        var counted = new TextBlock { Text = count > 0 ? count.ToString("N0") : "", Foreground = Theme.Brush("DimTextBrush"), FontSize = 11,
                                      Margin = new Thickness(8, 0, ScrollBarRoom, 0), VerticalAlignment = VerticalAlignment.Center };
        DockPanel.SetDock(counted, Dock.Right);
        header.Children.Add(counted);
        header.Children.Add(new TextBlock { Text = folder.Name, TextTrimming = TextTrimming.CharacterEllipsis, VerticalAlignment = VerticalAlignment.Center,
                                            Foreground = count == 0 ? Theme.Brush("DimTextBrush") : Theme.Brush("TextBrush") });
        var item = new TreeViewItem
        {
            Header = header, Tag = new FolderTag(folder, key), HorizontalContentAlignment = HorizontalAlignment.Stretch,
            IsExpanded = open.Contains(key), ToolTip = folder.Note,
        };
        if (!isRoot)
            foreach (AssetFolder inside in folder.Folders) item.Items.Add(FolderItem(inside, key + "/" + inside.Name, open));
        return item;
    }

    IEnumerable<string> OpenFolders()
    {
        IEnumerable<string> In(ItemCollection items) => items.OfType<TreeViewItem>()
            .SelectMany(i => (i.IsExpanded && i.Tag is FolderTag tag ? [tag.Key] : Array.Empty<string>()).Concat(In(i.Items)));
        return In(Sources.Items);
    }

    static TreeViewItem? Find(ItemCollection items, string key)
    {
        foreach (TreeViewItem item in items.OfType<TreeViewItem>())
        {
            if (item.Tag is FolderTag tag && tag.Key == key) return item;
            if (Find(item.Items, key) is { } inside)
            {
                item.IsExpanded = true;
                return inside;
            }
        }
        return null;
    }

    void OnSourceChosen(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        ListChosen();
        if (restoring || (Sources.SelectedItem as TreeViewItem)?.Tag is not FolderTag tag) return;
        AssetPlace.Chosen = tag.Key;
        AssetPlace.Changed();
    }

    // ---- The list ------------------------------------------------------------------------------

    /// <summary>Lists what is in the chosen folder, narrowed by Find.</summary>
    void ListChosen()
    {
        if ((Sources.SelectedItem as TreeViewItem)?.Tag is not FolderTag(AssetFolder folder, string key))
        {
            chosenItems = [];
            chosenTitle = "";
        }
        else
        {
            chosenItems = folder.All();
            chosenTitle = key.Length == 0 ? "everything" : key.Replace("/", " › ");
            if (chosenItems.Count == 0 && folder.Note != null)
            {
                Items.ItemsSource = null;
                ListNote.Text = folder.Note;
                return;
            }
        }
        ApplyFind();
    }

    void OnFindChanged(object sender, TextChangedEventArgs e)
    {
        FindHint.Visibility = FindBox.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
        ApplyFind();
        if (restoring) return;
        AssetPlace.Find = FindBox.Text;
        AssetPlace.Changed();
    }

    void ApplyFind()
    {
        string find = FindBox.Text.Trim();
        List<AssetItem> shown = find.Length == 0
            ? chosenItems
            : chosenItems.Where(i => i.Name.Contains(find, StringComparison.OrdinalIgnoreCase) || i.Detail.Contains(find, StringComparison.OrdinalIgnoreCase)).ToList();
        Items.ItemsSource = shown;
        PutBackScroll();
        ListNote.Text = find.Length == 0
            ? $"{shown.Count:N0} in {chosenTitle}. Double-click one to add it where the view points, or drag it into the view."
            : $"{shown.Count:N0} of {chosenItems.Count:N0} in {chosenTitle} have \"{find}\" in their name.";
    }

    void OnItemChosen(object sender, SelectionChangedEventArgs e)
    {
        previewDelay.Stop();
        previewDelay.Start();  // (a moment later, so stepping down the list with the keys stays quick)
        if (restoring || Items.SelectedItem is not AssetItem item) return;
        AssetPlace.Picked = NameOf(item);
        AssetPlace.Changed();
    }

    AssetItem? ItemAt(object? hit) =>
        hit is DependencyObject element && ItemsControl.ContainerFromElement(Items, element) is ListBoxItem { DataContext: AssetItem item } ? item : null;

    void OnItemDoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (ItemAt(e.OriginalSource) is not { } item) return;
        e.Handled = true;
        source.AddAsset(item);
    }

    void OnItemKey(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.Enter || Items.SelectedItem is not AssetItem item) return;
        e.Handled = true;
        source.AddAsset(item);
    }

    // ---- Dragging into the view ----------------------------------------------------------------

    void OnItemPressed(object sender, MouseButtonEventArgs e)
    {
        pressedItem = ItemAt(e.OriginalSource);
        pressedAt = pressedItem == null ? null : e.GetPosition(Items);
    }

    void OnItemDragged(object sender, MouseEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed || pressedAt is not Point from || pressedItem is not { } item) return;
        Vector moved = e.GetPosition(Items) - from;
        if (Math.Abs(moved.X) < SystemParameters.MinimumHorizontalDragDistance && Math.Abs(moved.Y) < SystemParameters.MinimumVerticalDragDistance) return;
        pressedAt = null;
        pressedItem = null;
        Items.SelectedItem = item;
        DragDrop.DoDragDrop(Items, new DataObject(DragFormat, item), DragDropEffects.Copy);
    }

    // ---- The preview ---------------------------------------------------------------------------

    void ShowPreview()
    {
        if (Items.SelectedItem is not AssetItem item)
        {
            preview.Clear();
            PreviewViewport.Visibility = Visibility.Collapsed;
            PreviewIcon.Visibility = Visibility.Collapsed;
            PreviewTitle.Text = "";
            PreviewInfo.Text = "Pick something to see it here.";
            return;
        }
        PreviewTitle.Text = item.Name;
        PreviewInfo.Text = item.About;
        Model3DGroup? model = null;
        Mouse.OverrideCursor = Cursors.Wait;
        var note = HangWatch.While($"reading {item.Name}'s model for the Assets panel's preview");
        try
        {
            model = source.AssetPreview(item);
        }
        catch (Exception ex) when (ex is System.IO.InvalidDataException or NotSupportedException or InvalidOperationException)
        {
            PreviewInfo.Text = $"{item.About}\nIts model couldn't be read: {ex.Message}";
        }
        finally
        {
            Mouse.OverrideCursor = null;
            note.Dispose();
        }
        if (model != null)
        {
            PreviewIcon.Visibility = Visibility.Collapsed;
            PreviewViewport.Visibility = Visibility.Visible;
            preview.Show(model, frame: true);
        }
        else
        {
            preview.Clear();
            PreviewViewport.Visibility = Visibility.Collapsed;
            PreviewIcon.Text = item.Icon;
            PreviewIcon.Foreground = item.Colour;
            PreviewIcon.Visibility = Visibility.Visible;
        }
    }
}
