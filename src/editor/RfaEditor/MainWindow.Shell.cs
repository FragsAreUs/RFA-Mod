using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Threading;
using TitleBar = Wpf.Ui.Controls.TitleBar;
using WindowBackdropType = Wpf.Ui.Controls.WindowBackdropType;

namespace RfaEditor;

// The window's frame and layout, Fluent: a title bar with the menus of the tab you are working in
// and where its file is; one layout of panels you can move (Shell\MainPage: the Explorer, what you
// open as tabs in the middle, the Outliner and Inspector, the Log and the Show panel); and a status
// line with what the editor last did, the tab's own status and how much memory it uses. On Windows
// 11 the title bar and the status line show the desktop's colours through them (Mica).
//
// The window's own XAML still lays out the old window (explorer, tabs, outliner, inspector, log);
// this takes its explorer and its preview into the new layout when the window starts. If that
// can't be done, the old window stays, which works on its own.
public partial class MainWindow
{
    MainPage? page;
    readonly ContentControl menuHost = new() { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(14, 0, 0, 0) };
    readonly ContentControl tabStatusHost = new() { VerticalAlignment = VerticalAlignment.Center };
    readonly TextBlock placeText = new() { Foreground = Theme.Brush("DimTextBrush"), FontSize = 12, VerticalAlignment = VerticalAlignment.Center,
                                           TextTrimming = TextTrimming.CharacterEllipsis, MaxWidth = 560 };
    readonly TextBlock memoryText = new() { Foreground = Theme.Brush("DimTextBrush"), VerticalAlignment = VerticalAlignment.Center,
                                            ToolTip = "How much memory the editor uses now" };
    Menu? windowMenu;  // for tabs without menus of their own (the preview, tables)

    /// <summary>Makes the layout, the title bar and the status line. Called once, when the window is made.</summary>
    void StartShell()
    {
        // The explorer: the part of the old window's layout that holds the tree of files.
        FrameworkElement explorer = Tree;
        while (explorer.Parent is FrameworkElement parent && !(parent is Grid { ColumnDefinitions.Count: >= 5 })) explorer = parent;
        if (explorer.Parent is not Grid oldLayout)
        {
            Log("The window's layout couldn't be made (its explorer wasn't found); the old one is used.");
            return;
        }
        try
        {
            oldLayout.Children.Remove(explorer);
            page = new MainPage(context, LogList, explorer, OnTabChosen, gamePanel.View);
        }
        catch (Exception ex)
        {
            if (explorer.Parent == null) oldLayout.Children.Add(explorer);
            Log($"The window's layout couldn't be made ({ex.GetType().Name}: {ex.Message}); the old one is used.");
            page = null;
            return;
        }
        // The panel has its own title now: the explorer's own heading goes.
        if (explorer is DockPanel { Children.Count: > 0 } explorerPanel && explorerPanel.Children[0] is Border heading) heading.Visibility = Visibility.Collapsed;

        // The old layout lets go of the preview of the picked file, and of its inspector, and stops
        // handing parts out. What it showed stays its visual child until it is laid out again, which
        // is done here, before the new layout takes them.
        Documents.SelectionChanged -= OnDocumentChosen;
        OutlinerHost.Content = null;
        InspectorHost.Content = null;
        var (previewDocument, previewTab) = documents["preview"];
        previewTab.Content = null;
        UpdateLayout();
        LetGo(previewDocument.View);
        if (previewDocument.Inspector is { } inspector) LetGo(inspector);

        // The new layout takes the old one's place under the window's top bar.
        var window = (DockPanel)Content;
        window.Children.Remove((UIElement)window.Children[^1]);
        window.Children.Add(page);

        // The preview is a tab of the new layout, brought forward when you pick a file.
        page.AddDocument("preview", previewDocument, "The file picked in the explorer", closable: false);
        Tree.SelectedItemChanged += (_, _) => page.Show("preview");
        SearchResults.SelectionChanged += (_, _) => page.Show("preview");

        // Tables and zones open as tabs of the new layout.
        context.OpenLevel = page.OpenLevel;
        context.OpenTable = page.OpenTable;
        Closing += OnShellClosing;

        UseFluentTitleBar();
        UseStatusLine();
        RelistWhenFilesChange();
        OnTabChosen(previewDocument, "The file picked in the explorer");
    }

    string? listedSignature;  // what the explorer and the search list, in short (see Workspace.ListedSignatureReader)

    /// <summary>
    /// When the window comes back to the front and the files the editor lists have changed (the
    /// Guerrilla builder wrote new files, or took them away, while the editor was open), the explorer
    /// and the search are made again, so they list what the game will see. The disk is looked at on
    /// another thread, so coming back to the window isn't held up.
    /// </summary>
    void RelistWhenFilesChange()
    {
        Func<string> read = workspace.ListedSignatureReader();
        bool looking = false;
        try
        {
            listedSignature = read();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
        Activated += async (_, _) =>
        {
            if (looking) return;
            looking = true;
            try
            {
                string now = await Task.Run(read);
                if (now == listedSignature) return;
                listedSignature = now;
                Log("The files the editor lists have changed (new files in overrides\\ or the mods' data): the explorer and the search are made again.");
                BuildTree();
                _ = BuildSearchIndexAsync();
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                // (files being written just now: looked at again the next time)
            }
            finally
            {
                looking = false;
            }
        };
    }

    /// <summary>
    /// Makes sure an element the old layout showed has no parent left there, so the new layout can
    /// show it: a content presenter still holding it is emptied and laid out again.
    /// </summary>
    static void LetGo(FrameworkElement element)
    {
        if (System.Windows.Media.VisualTreeHelper.GetParent(element) is not ContentPresenter presenter) return;
        presenter.Content = null;
        presenter.Measure(presenter.RenderSize);
        presenter.Arrange(new Rect(presenter.RenderSize));
    }

    /// <summary>The tab you work in changed: its menus go in the title bar, its status in the status line.</summary>
    void OnTabChosen(IEditorDocument? document, string? where)
    {
        var parts = document as IDocumentParts;
        menuHost.Content = parts?.PartFor("menu") ?? (windowMenu ??= WindowMenu());
        tabStatusHost.Content = parts?.PartFor("status");
        placeText.Text = where == null ? "" : where.Replace("\\", "  ›  ").Replace(" > ", "  ›  ");
        placeText.ToolTip = where;
    }

    /// <summary>The menus for tabs without their own.</summary>
    Menu WindowMenu()
    {
        var menu = new Menu { IsMainMenu = false, Background = System.Windows.Media.Brushes.Transparent };
        var file = new MenuItem { Header = "_File" };
        var overrides = new MenuItem { Header = "Open the _overrides folder", ToolTip = "Where your changes are saved (the game's own files are never changed)" };
        overrides.Click += OnOpenOverridesFolder;
        var close = new MenuItem { Header = "E_xit" };
        close.Click += (_, _) => Close();
        file.Items.Add(overrides);
        file.Items.Add(new Separator());
        file.Items.Add(close);
        menu.Items.Add(file);
        return menu;
    }

    /// <summary>
    /// Moves the window's top bar into a Fluent title bar: the editor's icon and name and the menus on
    /// the left, where the tab's file is in the middle, the top bar's buttons (Overrides folder, ...)
    /// on the right, before the buttons to shrink, enlarge and close the window. Then the window draws
    /// its own title bar, with Mica behind it where Windows has it.
    /// </summary>
    void UseFluentTitleBar()
    {
        if (TopBar.Parent is not Border top) return;
        var header = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        var trailing = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 8, 0) };
        foreach (UIElement child in TopBar.Children.Cast<UIElement>().ToList())
        {
            TopBar.Children.Remove(child);
            if (child == GameFolderText) continue;  // goes in the status line (UseStatusLine)
            if (DockPanel.GetDock(child) == Dock.Right) trailing.Children.Insert(0, child);  // the first one docked right is the rightmost
            else header.Children.Add(child);
        }
        header.Children.Add(menuHost);

        top.Child = new TitleBar
        {
            Header = header,
            CenterContent = placeText,
            TrailingContent = trailing,
            Height = 48,  // the menus' own height in WPF UI's look
            Padding = new Thickness(10, 0, 0, 0),
            ShowHelp = false,
        };
        top.Background = System.Windows.Media.Brushes.Transparent;
        top.BorderThickness = new Thickness(0);
        top.Padding = new Thickness(0);

        ExtendsContentIntoTitleBar = true;
        WindowBackdropType = WindowBackdropType.Mica;  // only where Windows has it (Windows 11); elsewhere the window's own colour
    }

    /// <summary>
    /// The status line: what the editor last did on the left; on the right, the tab's own status (a
    /// zone's: whose it is and where saving goes), where the game and your changes are, and how much
    /// memory the editor uses.
    /// </summary>
    void UseStatusLine()
    {
        // The status bar lays its items out from the left, each docked as it says, the last filling
        // what is left: the new ones go before the editor's line of text, docked right (rightmost first).
        if (StatusText.Parent is not StatusBar bar) return;
        int at = 0;
        void Right(UIElement element)
        {
            var item = new StatusBarItem { Content = element, VerticalContentAlignment = VerticalAlignment.Center };
            DockPanel.SetDock(item, Dock.Right);
            bar.Items.Insert(at++, item);
        }
        Right(memoryText);
        Right(new Separator { Style = (Style)FindResource(StatusBar.SeparatorStyleKey), Margin = new Thickness(10, 3, 10, 3) });
        // Which copy of RFA-Mod this editor is first (its folder has the launcher, the mods and
        // overrides\), shortened to its last two folders, so two copies can't be mixed up.
        string modFolder = Path.GetFullPath(workspace.ModFolder).TrimEnd(Path.DirectorySeparatorChar);
        string parent = Path.GetFileName(Path.GetDirectoryName(modFolder) ?? "");
        string shortMod = (parent.Length > 0 ? "…\\" + parent + "\\" : "") + Path.GetFileName(modFolder);
        GameFolderText.Text = $"RFA-Mod: {shortMod}      Game: {workspace.GameFolder}";
        GameFolderText.ToolTip = $"This editor's copy of RFA-Mod: {modFolder}\nWhat you save goes to {workspace.OverridesFolder}; " +
                                 $"the game shows it when started with RFAModLauncher.exe from {modFolder}.\nThe game: {workspace.GameFolder}";
        GameFolderText.FontSize = 11;
        GameFolderText.MaxWidth = 420;
        Right(GameFolderText);
        Right(new Separator { Style = (Style)FindResource(StatusBar.SeparatorStyleKey), Margin = new Thickness(10, 3, 10, 3) });
        tabStatusHost.MaxWidth = 620;
        Right(tabStatusHost);
        StatusText.TextTrimming = TextTrimming.CharacterEllipsis;
        StatusText.VerticalAlignment = VerticalAlignment.Center;

        var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        timer.Tick += (_, _) => memoryText.Text = $"Memory {Environment.WorkingSet / (1024.0 * 1024 * 1024):0.0} GB";
        timer.Start();
        memoryText.Text = $"Memory {Environment.WorkingSet / (1024.0 * 1024 * 1024):0.0} GB";
    }

    /// <summary>
    /// The window is closing: after its own documents (OnClosing), every tab is asked about what it
    /// has unsaved, and any of them can keep the window open.
    /// </summary>
    void OnShellClosing(object? sender, CancelEventArgs e)
    {
        if (e.Cancel) return;  // one of the window's own documents already said no
        if (page != null)
        {
            if (!page.ConfirmClose())
            {
                e.Cancel = true;
                return;
            }
            page.WindowClosing();
        }
        gamePanel.Release();  // the game gets its own window back before the editor's goes
        CloseMcpServersOnExit();  // MainWindow.Mcp.cs
    }
}
