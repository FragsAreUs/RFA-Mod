using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using RfaFormats.Tables;

namespace RfaEditor;

/// <summary>
/// The table editor: one .xtbl table. Its entries are in the outliner (in the folders Volition's
/// editor sorted them into), the selected entry's fields in the tab. It opens your override of the
/// table if there is one, else the game's own copy, and saves to overrides\. The game's file is
/// never changed. Entries can be added (empty, or from Volition's templates), copied and deleted;
/// the rest of the file stays exactly as written.
/// </summary>
public partial class TableEditor : UserControl, IEditorDocument
{
    readonly EditorContext context;
    readonly string archive;    // where the game's copy is, relative to the game folder
    readonly string fileName;
    readonly XtblFile table;
    readonly FieldEditors editors;
    readonly Dictionary<string, IReadOnlyList<string>> referenceTargets = new(StringComparer.OrdinalIgnoreCase);
    string? overridePath;       // the override being edited; null until the first save if it came from the game
    bool unsaved;

    internal TableEditor(EditorContext context, string archive, string fileName)
    {
        InitializeComponent();
        this.context = context;
        this.archive = archive;
        this.fileName = fileName;
        Outliner = DocumentParts.Detach(OutlinerPart);
        Inspector = DocumentParts.Detach(InspectorPart);
        overridePath = Workspace.OverrideFor(archive, fileName);
        table = XtblFile.FromBytes(Workspace.Read(archive, fileName));
        editors = new FieldEditors(ReferenceTargets, OnChanged);
        PreviewKeyDown += (_, e) =>
        {
            if (e.Key == Key.S && Keyboard.Modifiers == ModifierKeys.Control) { Save(); e.Handled = true; }
            if (e.Key == Key.D && Keyboard.Modifiers == ModifierKeys.Control && CopyEntryButton.IsEnabled) { CopyEntry(); e.Handled = true; }
        };
        NewEntryButton.IsEnabled = table.Table != null && (table.Description != null || table.Entries.Any());
        ShowSource();
        BuildEntryTree("");
    }

    Workspace Workspace => context.Workspace;

    public string Title => (unsaved ? "* " : "") + fileName;
    public string Icon => Icons.Table;
    public FrameworkElement View => this;
    public FrameworkElement? Outliner { get; }
    public FrameworkElement? Inspector { get; }
    public event Action? TitleChanged;

    void ShowSource()
    {
        TitleChanged?.Invoke();
        SourceText.Text = overridePath != null
            ? $"Editing your override {Workspace.RelativeToMod(overridePath)}. The game uses it instead of its own {fileName}."
            : $"The game's own {fileName} (from {archive}). Saving puts your changes in overrides\\; the game's file is never changed.";
        TableTitle.Text = fileName;
        if (!table.Entries.Any())
            EmptyHintText.Text = $"{fileName} has no entries in the game's files (its <Table> is empty), so there is nothing to edit here. " +
                                 "The game either doesn't use it or gets these values from elsewhere.";
        TableInfo.Text = $"{table.Entries.Count():N0} entries" +
                         (table.Description != null
                             ? $" of <{table.Description.Name}>, with {table.Description.Children.Count} fields described. " +
                               "Pick an entry in the outliner; hover over a field's name for what it does."
                             : ". This table has no description of its fields, so values are shown as they are written.");
    }

    // ---- Entries -------------------------------------------------------------------------------

    /// <summary>A folder of entries, from each entry's &lt;_Editor&gt;&lt;Category&gt;, e.g. "Entries:Alien".</summary>
    sealed class Folder
    {
        public SortedDictionary<string, Folder> Folders { get; } = new(StringComparer.OrdinalIgnoreCase);
        public List<(string Name, XtblElement Entry)> Entries { get; } = [];
    }

    void BuildEntryTree(string filter)
    {
        EntryTree.Items.Clear();
        if (table.Table == null)
        {
            // Not a regular table: show the whole file as one entry.
            if (table.Root != null) EntryTree.Items.Add(new TreeViewItem { Header = "(whole file)", Tag = table.Root });
            return;
        }

        var top = new Folder();
        foreach (XtblElement entry in table.Entries)
        {
            string name = XtblFile.EntryName(entry);
            if (filter.Length > 0 && !name.Contains(filter, StringComparison.OrdinalIgnoreCase)) continue;
            Folder folder = top;
            foreach (string part in XtblFile.EntryCategory(entry))
            {
                if (!folder.Folders.TryGetValue(part, out Folder? sub)) folder.Folders[part] = sub = new Folder();
                folder = sub;
            }
            folder.Entries.Add((name, entry));
        }
        AddFolder(EntryTree, top, expand: true, filtering: filter.Length > 0);
    }

    static void AddFolder(ItemsControl parent, Folder folder, bool expand, bool filtering)
    {
        foreach ((string name, Folder sub) in folder.Folders)
        {
            var item = new TreeViewItem { Header = name, IsExpanded = expand || filtering };
            AddFolder(item, sub, expand: false, filtering);
            parent.Items.Add(item);
        }
        foreach ((string name, XtblElement entry) in folder.Entries.OrderBy(e => e.Name, StringComparer.OrdinalIgnoreCase))
            parent.Items.Add(new TreeViewItem { Header = name, Tag = entry });
    }

    void OnFilterChanged(object sender, TextChangedEventArgs e)
    {
        FilterHint.Visibility = FilterBox.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
        BuildEntryTree(FilterBox.Text.Trim());
    }

    void OnEntrySelected(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        bool picked = (e.NewValue as TreeViewItem)?.Tag is XtblElement;
        bool isEntry = (e.NewValue as TreeViewItem)?.Tag is XtblElement chosen && chosen.Parent == table.Table && table.Table != null;
        CopyEntryButton.IsEnabled = DeleteEntryButton.IsEnabled = isEntry;
        EntryView.Visibility = picked ? Visibility.Visible : Visibility.Collapsed;
        EmptyHint.Visibility = picked ? Visibility.Collapsed : Visibility.Visible;
        if ((e.NewValue as TreeViewItem)?.Tag is not XtblElement entry)
        {
            EntryTitle.Text = "";
            EntryInfo.Text = "";
            FieldTree.ItemsSource = null;
            return;
        }
        bool isWholeFile = entry == table.Root;
        EntryTitle.Text = isWholeFile ? fileName : XtblFile.EntryName(entry);
        string[] category = XtblFile.EntryCategory(entry);
        EntryInfo.Text = isWholeFile ? "" :
            $"<{entry.Name}> entry{(category.Length > 0 ? " in " + string.Join(" > ", category) : "")}. " +
            "Hover over a field's name for what it does. Fields that aren't set use the game's default.";
        FieldTree.ItemsSource = editors.EntryRows(entry, isWholeFile ? null : table.Description);
    }

    // ---- Adding, copying and deleting entries --------------------------------------------------

    XtblElement? SelectedEntry =>
        EntryTree.SelectedItem is TreeViewItem { Tag: XtblElement entry } && entry.Parent == table.Table && table.Table != null ? entry : null;

    void OnNewEntry(object sender, RoutedEventArgs e)
    {
        var templates = table.Templates.ToList();
        if (templates.Count == 0)
        {
            NewEmptyEntry();
            return;
        }
        // Tables with templates: pick an empty entry or one of them.
        var menu = new ContextMenu { PlacementTarget = NewEntryButton, Placement = System.Windows.Controls.Primitives.PlacementMode.Bottom };
        var empty = new MenuItem { Header = "Empty entry (every field the game's default)" };
        empty.Click += (_, _) => NewEmptyEntry();
        menu.Items.Add(empty);
        menu.Items.Add(new Separator());
        foreach (XtblElement template in templates)
        {
            var item = new MenuItem { Header = $"From the template \"{XtblFile.EntryName(template)}\"" };
            item.Click += (_, _) => Try(() =>
            {
                XtblElement entry = table.NewEntryFrom(template, table.UnusedName(XtblFile.EntryName(template)));
                EntriesChanged(entry, $"Added {XtblFile.EntryName(entry)} from the template {XtblFile.EntryName(template)}.");
            });
            menu.Items.Add(item);
        }
        menu.IsOpen = true;
    }

    void NewEmptyEntry() => Try(() =>
    {
        string kind = table.Description?.Name ?? table.Entries.First().Name;
        // In the folder of the entry picked, so it shows up next to what you were looking at.
        string[] category = SelectedEntry is XtblElement near ? XtblFile.EntryCategory(near) : [];
        XtblElement entry = table.NewEntry(table.UnusedName("new_" + kind.ToLowerInvariant()), category);
        EntriesChanged(entry, $"Added an empty <{kind}> entry, {XtblFile.EntryName(entry)}: every field it doesn't set is the game's default. " +
                              "Rename it in its Name field.");
    });

    void OnCopyEntry(object sender, RoutedEventArgs e) => CopyEntry();

    void CopyEntry() => Try(() =>
    {
        if (SelectedEntry is not XtblElement source) return;
        string name = XtblFile.EntryName(source);
        XtblElement copy = table.CopyEntry(source, table.UnusedName(name + "_copy"));
        EntriesChanged(copy, XtblFile.HasNameField(source)
            ? $"Copied {name} as {XtblFile.EntryName(copy)}. Rename it in its Name field; the game finds entries by name."
            : $"Copied {name}. This table's entries have no Name field, so the copy is identical: change what tells them apart.");
    });

    void OnEntryTreeKey(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Delete && DeleteEntryButton.IsEnabled)
        {
            DeleteEntry();
            e.Handled = true;
        }
    }

    void OnDeleteEntry(object sender, RoutedEventArgs e) => DeleteEntry();

    /// <summary>Deletes the picked entry, after saying which tables name it (the game may then fail to find it there).</summary>
    void DeleteEntry()
    {
        if (SelectedEntry is not XtblElement entry) return;
        string name = XtblFile.EntryName(entry);
        List<(string Table, int Count)> namers;
        Mouse.OverrideCursor = Cursors.Wait;
        try { namers = TablesNaming(entry); }
        finally { Mouse.OverrideCursor = null; }
        string where = namers.Count == 0
            ? "No table names it (scripts and levels weren't searched)."
            : "These tables name it, and may stop working without it:\n" +
              string.Join("\n", namers.Take(15).Select(n => $"   {n.Table} ({n.Count} time{(n.Count == 1 ? "" : "s")})")) +
              (namers.Count > 15 ? $"\n   and {namers.Count - 15} more" : "");
        if (MessageBox.Show($"Delete <{entry.Name}> {name} from {fileName}?\n\n{where}\n\nNothing is changed in overrides\\ until you save.",
                            "Delete entry", MessageBoxButton.YesNo, namers.Count > 0 ? MessageBoxImage.Warning : MessageBoxImage.Question) != MessageBoxResult.Yes)
            return;
        Try(() =>
        {
            table.RemoveEntry(entry);
            EntriesChanged(null, $"Deleted {name} from {fileName}.");
        });
    }

    /// <summary>
    /// The tables that have an entry's name as a whole value somewhere (this one counting only its
    /// other entries), as the game sees them (with your overrides): what may break if it goes.
    /// </summary>
    List<(string Table, int Count)> TablesNaming(XtblElement entry)
    {
        string name = XtblFile.EntryName(entry);
        var found = new List<(string, int)>();
        int here = table.Entries.Where(other => other != entry).Sum(other => Values(other).Count(v => v.Equals(name, StringComparison.OrdinalIgnoreCase)));
        if (here > 0) found.Add((fileName + " (other entries)", here));
        var value = new Regex(">\\s*" + Regex.Escape(name) + "\\s*<", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
        foreach (string other in context.Assets.NamesWhere(n => n.EndsWith(".xtbl", StringComparison.OrdinalIgnoreCase)).Distinct(StringComparer.OrdinalIgnoreCase).Order())
        {
            if (other.Equals(fileName, StringComparison.OrdinalIgnoreCase)) continue;
            if (context.Assets.PlacesOf(other).FirstOrDefault(p => p.Container == null) is not { Archive: string at }) continue;
            try
            {
                int count = value.Matches(Encoding.Latin1.GetString(Workspace.Read(at, other))).Count;
                if (count > 0) found.Add((other, count));
            }
            catch (Exception ex) when (ex is IOException or InvalidDataException) { }
        }
        return found;
    }

    static IEnumerable<string> Values(XtblElement element) =>
        element.HasElements ? element.Elements.SelectMany(Values) : [element.Value];

    /// <summary>After an entry was added or deleted: the outliner again (with the new one picked), the counts, and unsaved.</summary>
    void EntriesChanged(XtblElement? pick, string message)
    {
        OnChanged();
        ShowSource();
        FilterBox.Text = "";
        BuildEntryTree("");
        if (pick != null) Pick(EntryTree, pick);
        context.Log(message);
    }

    /// <summary>Selects an entry's row in the outliner, opening the folders it is in.</summary>
    static bool Pick(ItemsControl parent, XtblElement entry)
    {
        foreach (TreeViewItem item in parent.Items.OfType<TreeViewItem>())
        {
            if (item.Tag == entry)
            {
                item.IsSelected = true;
                item.BringIntoView();
                return true;
            }
            if (Pick(item, entry))
            {
                item.IsExpanded = true;
                return true;
            }
        }
        return false;
    }

    void Try(Action action)
    {
        try
        {
            action();
        }
        catch (Exception ex)
        {
            context.Log($"{ex.GetType().Name}: {ex.Message}");
            MessageBox.Show(ex.Message, "Table editor", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    /// <summary>The names a Reference field can point to, read from the table it names (your
    /// override of it if you have one). Each table is read once.</summary>
    IReadOnlyList<string> ReferenceTargets(FieldDescription field)
    {
        string file = field.ReferenceFile ?? "";
        if (file.Equals(fileName, StringComparison.OrdinalIgnoreCase))
            return table.ReferenceTargets(field.ReferenceField).Order(StringComparer.OrdinalIgnoreCase).ToList();

        string key = file + "|" + field.ReferenceField;
        if (!referenceTargets.TryGetValue(key, out IReadOnlyList<string>? names))
        {
            try
            {
                byte[]? data = Workspace.ReadAsGameSeesIt(file);
                names = data == null ? [] : XtblFile.FromBytes(data).ReferenceTargets(field.ReferenceField).Order(StringComparer.OrdinalIgnoreCase).ToList();
            }
            catch (Exception ex)
            {
                context.Log($"Couldn't read {file}: {ex.Message}");
                names = [];
            }
            referenceTargets[key] = names;
        }
        return names;
    }

    // ---- Saving --------------------------------------------------------------------------------

    void OnChanged()
    {
        if (!unsaved)
        {
            unsaved = true;
            SaveButton.IsEnabled = true;
            ShowSource();
        }
        if (EntryTree.SelectedItem is TreeViewItem { Tag: XtblElement entry } selected && entry != table.Root)
            selected.Header = XtblFile.EntryName(entry);  // in case the Name field was edited
    }

    void OnSave(object sender, RoutedEventArgs e) => Save();

    bool Save()
    {
        if (!unsaved) return true;
        try
        {
            bool first = overridePath == null;
            string target = overridePath ?? Workspace.OverridePath(archive, fileName);
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.WriteAllBytes(target, table.ToBytes());
            overridePath = target;
            unsaved = false;
            SaveButton.IsEnabled = false;
            Workspace.RefreshOverrides();
            ShowSource();
            context.Log(first
                ? $"Saved {fileName} as a new override, {Workspace.RelativeToMod(target)}. The game uses it from its next start through RFAModLauncher."
                : $"Saved {Workspace.RelativeToMod(target)}.");
            context.OverridesChanged();
            return true;
        }
        catch (Exception ex)
        {
            MessageBox.Show("Couldn't save: " + ex.Message, "Table editor", MessageBoxButton.OK, MessageBoxImage.Error);
            return false;
        }
    }

    public bool ConfirmClose()
    {
        if (!unsaved) return true;
        MessageBoxResult answer = MessageBox.Show($"Save your changes to {fileName} in overrides\\?", "Table editor",
                                                  MessageBoxButton.YesNoCancel, MessageBoxImage.Question);
        return answer == MessageBoxResult.No || (answer == MessageBoxResult.Yes && Save());
    }
}
