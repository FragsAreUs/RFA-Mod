using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using RfaFormats;

namespace RfaEditor;

/// <summary>
/// The editor's window, laid out like other game editors (Nanoforge among them): the explorer of
/// the game's archives on the left, open documents as tabs in the middle (the preview of the picked
/// file, tables, levels), the chosen document's outliner and inspector on the right, and a log at
/// the bottom, with the running game among the tabs (the Game panel). The game folder is only ever
/// read.
/// </summary>
public partial class MainWindow : Wpf.Ui.Controls.FluentWindow
{
    readonly Workspace workspace;
    readonly EditorContext context;
    readonly FilePreview preview;
    readonly GamePanel gamePanel;
    readonly Dictionary<string, (IEditorDocument Document, TabItem Tab)> documents = new(StringComparer.OrdinalIgnoreCase);
    List<IndexedFile> searchIndex = [];
    GameFile? selectedFile;  // the file picked in the explorer, if a file is

    internal MainWindow(Workspace workspace)
    {
        InitializeComponent();
        this.workspace = workspace;
        context = new EditorContext
        {
            Workspace = workspace,
            Assets = new GameAssets(workspace),
            Log = Log,
            OverridesChanged = RefreshOverrideMarks,
            OpenTable = OpenTable,
            OpenLevel = OpenLevel,
        };
        GameFolderText.Text = $"Game: {workspace.GameFolder}      Overrides: {workspace.OverridesFolder}";
        preview = new FilePreview(context);
        AddDocument("preview", preview, closable: false);
        BuildTree();
        _ = BuildSearchIndexAsync();
        gamePanel = new GamePanel(context, PlaceGame);
        BuildUiPack();
    }

    /// <summary>
    /// RFA-Mod's UI pack (the main menu's Modern style's pictures, RfaFormats.Interface.UiPack), made if it isn't
    /// there yet, is older, or was taken out of the interface's lists (by the Guerrilla HUD's Remove), in the background.
    /// </summary>
    void BuildUiPack()
    {
        string game = workspace.GameFolder, overrides = workspace.OverridesFolder;
        Task.Run(() =>
        {
            try
            {
                if (RfaFormats.Interface.UiPack.Built(overrides)) return;
                List<string> lines = RfaFormats.Interface.UiPack.Build(game, overrides);
                Dispatcher.BeginInvoke(new Action(() =>
                {
                    foreach (string line in lines) Log("UI pack: " + line);
                    Log("UI pack: made. Restart the game to use it (Options > Main Menu > Style: Modern).");
                }));
            }
            catch (Exception ex) when (ex is IOException or InvalidDataException or UnauthorizedAccessException or InvalidOperationException)
            {
                Dispatcher.BeginInvoke(new Action(() => Log("UI pack: couldn't be made: " + ex.Message)));
            }
        });
    }

    // ---- Log -----------------------------------------------------------------------------------

    void Log(string text)
    {
        LogList.Items.Add(new LogLine($"{DateTime.Now:HH:mm:ss}", text));
        EditorLog.Write($"{DateTime.Now:HH:mm:ss} {text}");
        LogList.ScrollIntoView(LogList.Items[^1]);
        StatusText.Text = text;
    }

    // ---- Documents -----------------------------------------------------------------------------

    void AddDocument(string key, IEditorDocument document, bool closable = true)
    {
        var icon = new TextBlock { Text = document.Icon, Style = (Style)FindResource("Icon"), FontSize = 12, Margin = new Thickness(0, 0, 7, 0) };
        var title = new TextBlock { Text = document.Title, VerticalAlignment = VerticalAlignment.Center };
        var header = new StackPanel { Orientation = Orientation.Horizontal };
        header.Children.Add(icon);
        header.Children.Add(title);
        var tab = new TabItem { Header = header, Content = document.View };
        if (closable)
        {
            var close = new Button
            {
                Content = new TextBlock { Text = Icons.Close, Style = (Style)FindResource("Icon"), FontSize = 9 },
                Style = (Style)FindResource("ToolButton"), Width = 20, Height = 20, Margin = new Thickness(8, 0, 0, 0), ToolTip = "Close",
            };
            close.Click += (_, _) => CloseDocument(key);
            header.Children.Add(close);
        }
        else
        {
            header.Margin = new Thickness(0, 0, 6, 0);
        }
        document.TitleChanged += () => title.Text = document.Title;
        documents[key] = (document, tab);
        Documents.Items.Add(tab);
        Documents.SelectedItem = tab;
    }

    void CloseDocument(string key)
    {
        if (!documents.TryGetValue(key, out var open) || !open.Document.ConfirmClose()) return;
        Documents.Items.Remove(open.Tab);
        documents.Remove(key);
    }

    /// <summary>Shows the chosen document's outliner and inspector on the right.</summary>
    void OnDocumentChosen(object sender, SelectionChangedEventArgs e)
    {
        if (e.Source != Documents) return;  // a selection change inside a document bubbling up
        IEditorDocument? document = documents.Values.FirstOrDefault(d => d.Tab == Documents.SelectedItem).Document;
        OutlinerHost.Content = document?.Outliner ?? Hint("Open a level or a table to see what is in it here.");
        InspectorHost.Content = document?.Inspector;
    }

    static TextBlock Hint(string text) => new()
    {
        Text = text, Margin = new Thickness(8), TextWrapping = TextWrapping.Wrap,
        Foreground = (System.Windows.Media.Brush)Application.Current.Resources["DimTextBrush"],
    };

    void ShowPreview() => Documents.SelectedItem = documents["preview"].Tab;

    /// <summary>Opens a table in the table editor, or brings its tab forward if it's open already.</summary>
    void OpenTable(GameFile file)
    {
        string key = "table:" + file.Name;
        if (documents.TryGetValue(key, out var open))
        {
            Documents.SelectedItem = open.Tab;
            return;
        }
        Try(() => AddDocument(key, new TableEditor(context, file.Archive, file.Name)));
    }

    /// <summary>Opens a zone in the level editor, or brings its tab forward if it's open already.</summary>
    void OpenLevel(GameFile file)
    {
        string key = $"level:{file.Where}>{file.Name}";
        if (documents.TryGetValue(key, out var open))
        {
            Documents.SelectedItem = open.Tab;
            return;
        }
        Mouse.OverrideCursor = Cursors.Wait;
        try
        {
            Try(() => AddDocument(key, new LevelEditor(context, file.Archive, file.Container, file.Name)));
        }
        finally
        {
            Mouse.OverrideCursor = null;
        }
    }

    void OnSourceInitialized(object? sender, EventArgs e)
    {
        DarkTitleBar.Apply(this);
        StartShell();  // the workspaces: see MainWindow.Shell.cs
    }

    void OnClosing(object? sender, CancelEventArgs e)
    {
        foreach (var (document, tab) in documents.Values.ToList())
        {
            Documents.SelectedItem = tab;
            if (!document.ConfirmClose())
            {
                e.Cancel = true;
                return;
            }
        }
        // (The layout's tabs are asked next, in MainWindow.Shell.cs; the game gets its own window
        // back once the window is closing for certain, see OnShellClosing.)
    }

    // ---- Explorer ------------------------------------------------------------------------------

    void BuildTree()
    {
        var folders = workspace.Archives()
            .GroupBy(archive => Path.GetDirectoryName(archive) ?? "")
            .Select(group => new FolderNode(group.Key, group.Select(archive => new ArchiveNode(workspace, archive))));
        Tree.ItemsSource = folders.ToList();
    }

    void OnTreeItemExpanded(object sender, RoutedEventArgs e)
    {
        if ((e.OriginalSource as TreeViewItem)?.DataContext is TreeNode node && !node.ChildrenLoaded)
            Try(() => node.LoadChildren());
    }

    void OnTreeSelectionChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        selectedFile = (e.NewValue as FileNode)?.File;
        switch (e.NewValue)
        {
            case FileNode file: preview.ShowFile(file.File, file); break;
            case ArchiveNode archive: preview.ShowArchive(archive); break;
            case FolderNode folder: preview.ShowFolder(folder); break;
            default: preview.ShowNothing(); break;
        }
        ShowPreview();
    }

    void OnFileDoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (selectedFile != null) FilePreview.OpenInEditor(selectedFile, context);
    }

    async Task BuildSearchIndexAsync()
    {
        List<(string, string, Func<Packfile>)> archives = workspace.Archives().Select(archive => (archive, workspace.ArchivePath(archive), workspace.Open(archive))).ToList();
        var progress = new Progress<string>(text => StatusText.Text = text);
        var started = System.Diagnostics.Stopwatch.StartNew();
        // Reading the containers is disk work (a minute or so the first time; then only archives that changed
        // are read again, see FileIndex). Done at the lowest processor priority (the disk as usual: Windows' background
        // mode gave it away and, with the game loading beside it, the index took minutes); at normal priority while
        // the game's main menu waits for it (Create level: its processing screen is up).
        bool menuWaits = NewLevelAtStart != null || NewLevelMadeAtStart != null;
        try
        {
            searchIndex = await Task.Run(() => Native.InBackground(() => FileIndex.ArchiveFiles(archives)));
            StatusText.Text = $"{searchIndex.Count:N0} files in the archives. Reading the containers for search, models and textures...";
            var textures = new Dictionary<string, TextureRef>(StringComparer.OrdinalIgnoreCase);
            List<IndexedFile> inContainers = await Task.Run(() => FileIndex.ContainerFiles(archives, progress, textures, background: !menuWaits));
            searchIndex = [.. searchIndex, .. inContainers];
            context.Assets.SetIndex(searchIndex, textures);
            Log($"Ready: {searchIndex.Count:N0} files can be searched (read in {started.Elapsed.TotalSeconds:0.0} s).");
            WriteNewLevelChoices();
            MakeLevelFromGameMenu();  // (its level made first, so the game's processing screen stays up while it is)
            // The game's main menu (Level editor > Create level) shows a popup until then: it waits for this.
            SignalGameMenuReady();
            NewLevelFromGameMenu();
            if (SearchBox.Text.Length > 0) OnSearchChanged(SearchBox, null!);
        }
        catch (Exception ex)
        {
            Log("Reading the containers failed: " + ex.Message);
        }
    }

    void OnSearchChanged(object sender, TextChangedEventArgs e)
    {
        string text = SearchBox.Text.Trim();
        bool searching = text.Length > 0;
        SearchHint.Visibility = SearchBox.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
        Tree.Visibility = searching ? Visibility.Collapsed : Visibility.Visible;
        SearchResults.Visibility = searching ? Visibility.Visible : Visibility.Collapsed;
        if (!searching) return;

        const int limit = 2000;
        var matches = searchIndex.Where(f => f.Name.Contains(text, StringComparison.OrdinalIgnoreCase)).Take(limit + 1).ToList();
        SearchResults.ItemsSource = matches.Take(limit).ToList();
        StatusText.Text = matches.Count > limit ? $"More than {limit} files match; showing the first {limit}." : $"{matches.Count} files match.";
    }

    void OnSearchSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (SearchResults.SelectedItem is not IndexedFile found) return;
        Try(() =>
        {
            Packfile source = found.Container == null
                ? workspace.Archive(found.Archive)
                : workspace.Container(found.Archive, found.Container);
            PackfileEntry entry = source.Find(found.Name) ?? throw new FileNotFoundException(found.Name);
            selectedFile = new GameFile(source, entry, found.Archive, found.Container);
            preview.ShowFile(selectedFile, null);
            ShowPreview();
        });
    }

    // ---- Overrides -----------------------------------------------------------------------------

    /// <summary>Looks at overrides\ again (files may have been added or removed there) and updates
    /// the "changed" marks and the picked file's details.</summary>
    void RefreshOverrideMarks()
    {
        workspace.RefreshOverrides();
        foreach (TreeNode node in Tree.Items.OfType<TreeNode>().SelectMany(n => n.LoadedNodes()))
            (node as FileNode)?.UpdateChangedMark();
        preview.RefreshDetails();
    }

    void OnActivated(object? sender, EventArgs e) => RefreshOverrideMarks();

    void OnOpenOverridesFolder(object sender, RoutedEventArgs e)
    {
        Directory.CreateDirectory(workspace.OverridesFolder);
        Process.Start("explorer.exe", $"\"{workspace.OverridesFolder}\"");
    }

    void Try(Action action)
    {
        try
        {
            action();
        }
        catch (Exception ex)
        {
            Log($"{ex.GetType().Name}: {ex.Message}");
            EditorLog.Write($"  where: {ex}");  // the whole error, with where it happened, for the log file only
        }
    }
}

/// <summary>A line in the log panel: when, and what happened.</summary>
sealed record LogLine(string Time, string Text);
