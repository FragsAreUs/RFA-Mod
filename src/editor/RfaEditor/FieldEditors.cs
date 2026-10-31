using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using RfaFormats.Tables;

namespace RfaEditor;

/// <summary>
/// Where a value lives in a table: an element that may not exist yet. Entries leave out fields that
/// have their default value, so an edit may first have to create the element (and the groups
/// around it).
/// </summary>
sealed class Slot
{
    readonly Slot? parent;
    readonly string name = "";
    readonly XtblElement? element;

    public Slot(XtblElement element) => this.element = element;

    public Slot(Slot parent, string name)
    {
        this.parent = parent;
        this.name = name;
    }

    /// <summary>The element, or null if the entry doesn't have it.</summary>
    public XtblElement? Find() => element ?? parent!.Find()?.Element(name);

    /// <summary>The element, created (with any missing parents) if needed.</summary>
    public XtblElement Create() => Find() ?? parent!.Create().AddElement(name);
}

/// <summary>An editor control for one value, kept in step with the element it edits.</summary>
sealed class ValueEditor
{
    public FrameworkElement Control { get; }
    public Action Refresh { get; set; } = () => { };
    public event Action? Edited;

    public ValueEditor(FrameworkElement control) => Control = control;

    public void OnEdited() => Edited?.Invoke();
}

/// <summary>
/// Builds the field tree for one table entry, the way Volition's table editor showed it: one row
/// per field of the table's description, in its order, with an editor that fits the field's type.
/// Hovering over a field's name shows its help text, default and allowed values. Every edit goes
/// straight into the table's XML; the window saves it.
/// </summary>
sealed class FieldEditors
{
    const double LabelWidth = 270;
    const double EditorWidth = 320;

    readonly Func<FieldDescription, IReadOnlyList<string>> referenceTargets;
    readonly Action changed;
    bool filling;  // true while controls are being filled in, so their change events are ignored

    /// <param name="referenceTargets">The names a Reference field can point to.</param>
    /// <param name="changed">Called after every edit.</param>
    public FieldEditors(Func<FieldDescription, IReadOnlyList<string>> referenceTargets, Action changed)
    {
        this.referenceTargets = referenceTargets;
        this.changed = changed;
    }

    /// <summary>The rows for an entry: its described fields, then anything the description doesn't mention.</summary>
    public List<TreeViewItem> EntryRows(XtblElement entry, FieldDescription? description)
    {
        var rows = new List<TreeViewItem>();
        var slot = new Slot(entry);
        if (description != null)
            rows.AddRange(description.Children.Select(field => FieldRow(field, new Slot(slot, field.Name))));

        var described = description?.Children.Select(f => f.Name).ToHashSet(StringComparer.OrdinalIgnoreCase) ?? [];
        var others = entry.Elements.Where(e => !described.Contains(e.Name)).ToList();
        if (description == null)
            rows.AddRange(others.Select(RawRow));
        else if (others.Count > 0)
        {
            var group = new TreeViewItem
            {
                Header = Note("Values the table description doesn't list (_Editor holds the entry's folder)"),
            };
            foreach (XtblElement other in others) group.Items.Add(RawRow(other));
            rows.Add(group);
        }
        return rows;
    }

    TreeViewItem FieldRow(FieldDescription field, Slot slot, string? label = null, UIElement? extra = null) => field.Type switch
    {
        FieldType.Selection => ValueRow(field, slot, SelectionEditor(field, slot), label, extra),
        FieldType.Reference => ValueRow(field, slot, ReferenceEditor(field, slot), label, extra),
        FieldType.Flags => FlagsRow(field, slot, label, extra),
        FieldType.Element or FieldType.Vector or FieldType.Color => GroupRow(field, slot, label, extra),
        FieldType.ComboElement => ComboRow(field, slot, label, extra),
        FieldType.List or FieldType.Grid => ListRow(field, slot, label, extra),
        _ => ValueRow(field, slot, TextEditor(field, slot), label, extra),
    };

    // ---- Single values -------------------------------------------------------------------------

    /// <summary>A value with its editor, and a × button that removes it so the game uses its default.</summary>
    TreeViewItem ValueRow(FieldDescription field, Slot slot, ValueEditor editor, string? label, UIElement? extra)
    {
        var note = Note("");
        var reset = new Button { Content = "×", Padding = new Thickness(6, 0, 6, 0), Margin = new Thickness(6, 0, 0, 0),
                                 ToolTip = "Remove this value from the entry, so the game uses its default" };
        void ShowState()
        {
            bool set = slot.Find() != null;
            reset.Visibility = set && extra == null ? Visibility.Visible : Visibility.Collapsed;
            note.Text = set ? "" : field.Default is { Length: > 0 } fallback ? $"not set: the default is {fallback}" : "not set";
        }
        editor.Edited += ShowState;
        reset.Click += (_, _) =>
        {
            slot.Find()?.Remove();
            changed();
            editor.Refresh();
            ShowState();
        };
        ShowState();
        return new TreeViewItem { Header = Row(Label(field, label), editor.Control, reset, extra, note) };
    }

    /// <summary>Text, whole numbers, numbers and file names: a text box that refuses values the description doesn't allow.</summary>
    ValueEditor TextEditor(FieldDescription? field, Slot slot)
    {
        var box = new TextBox { Width = EditorWidth, VerticalContentAlignment = VerticalAlignment.Center };
        var editor = new ValueEditor(box);
        editor.Refresh = () =>
        {
            filling = true;
            box.Text = slot.Find()?.Value ?? "";
            filling = false;
            ShowProblem(box, null);
        };
        editor.Refresh();
        box.TextChanged += (_, _) =>
        {
            if (filling) return;
            string? problem = field?.Check(box.Text);
            ShowProblem(box, problem);
            if (problem != null) return;
            slot.Create().Value = box.Text;
            changed();
            editor.OnEdited();
        };
        return editor;
    }

    /// <summary>One of a fixed list of choices.</summary>
    ValueEditor SelectionEditor(FieldDescription field, Slot slot)
    {
        var box = new ComboBox { Width = EditorWidth };
        var editor = new ValueEditor(box);
        editor.Refresh = () =>
        {
            filling = true;
            string? current = slot.Find()?.Value;
            box.ItemsSource = current == null || field.Choices.Contains(current, StringComparer.OrdinalIgnoreCase)
                ? field.Choices
                : field.Choices.Append(current).ToList();
            box.SelectedItem = current == null ? null : ((IEnumerable<string>)box.ItemsSource).First(c => c.Equals(current, StringComparison.OrdinalIgnoreCase));
            filling = false;
        };
        editor.Refresh();
        box.SelectionChanged += (_, _) =>
        {
            if (filling || box.SelectedItem is not string choice) return;
            slot.Create().Value = choice;
            changed();
            editor.OnEdited();
        };
        return editor;
    }

    /// <summary>The name of an entry in another table: pick from the list, or type.</summary>
    ValueEditor ReferenceEditor(FieldDescription field, Slot slot)
    {
        var box = new ComboBox { Width = EditorWidth, IsEditable = true, IsTextSearchEnabled = true };
        var editor = new ValueEditor(box);
        editor.Refresh = () =>
        {
            filling = true;
            box.Text = slot.Find()?.Value ?? "";
            filling = false;
        };
        editor.Refresh();
        box.DropDownOpened += (_, _) =>
        {
            if (box.ItemsSource != null) return;
            filling = true;
            string text = box.Text;
            box.ItemsSource = referenceTargets(field);
            box.Text = text;
            filling = false;
        };
        box.AddHandler(TextBoxBase.TextChangedEvent, new TextChangedEventHandler((_, _) =>
        {
            // The drop-down also copies its text into its box when it first appears: not an edit.
            if (filling || box.Text == (slot.Find()?.Value ?? "")) return;
            slot.Create().Value = box.Text;
            changed();
            editor.OnEdited();
        }));
        return editor;
    }

    // ---- Groups --------------------------------------------------------------------------------

    /// <summary>Any number of named flags: one check box per flag the description lists.</summary>
    TreeViewItem FlagsRow(FieldDescription field, Slot slot, string? label, UIElement? extra)
    {
        var item = new TreeViewItem { Header = Row(Label(field, label), extra) };
        bool Has(string flag) => slot.Find()?.ElementsNamed("Flag").Any(f => f.Value.Equals(flag, StringComparison.OrdinalIgnoreCase)) == true;

        // Flags in the data that the description doesn't list are shown too, so nothing is hidden.
        var flags = field.Flags.ToList();
        foreach (string inData in slot.Find()?.ElementsNamed("Flag").Select(f => f.Value) ?? [])
            if (!flags.Contains(inData, StringComparer.OrdinalIgnoreCase)) flags.Add(inData);

        foreach (string flag in flags)
        {
            var check = new CheckBox { Content = flag, IsChecked = Has(flag) };
            check.Checked += (_, _) =>
            {
                if (!Has(flag)) slot.Create().AddElement("Flag", flag);
                changed();
            };
            check.Unchecked += (_, _) =>
            {
                foreach (XtblElement set in slot.Find()?.ElementsNamed("Flag").Where(f => f.Value.Equals(flag, StringComparison.OrdinalIgnoreCase)).ToList() ?? [])
                    set.Remove();
                changed();
            };
            item.Items.Add(new TreeViewItem { Header = check });
        }
        return item;
    }

    /// <summary>A group of fields (Element), or the X/Y/Z of a Vector, or the R/G/B of a Color.</summary>
    TreeViewItem GroupRow(FieldDescription field, Slot slot, string? label, UIElement? extra)
    {
        var item = new TreeViewItem { Header = Row(Label(field, label), extra) };
        foreach (FieldDescription child in field.Children)
            item.Items.Add(FieldRow(child, new Slot(slot, child.Name)));
        return item;
    }

    /// <summary>One of several groups, e.g. a weapon's shot type: Bullet, Thrown, Launcher, ...</summary>
    TreeViewItem ComboRow(FieldDescription field, Slot slot, string? label, UIElement? extra)
    {
        var item = new TreeViewItem();
        var choose = new ComboBox { Width = EditorWidth };
        choose.Items.Add("(none)");
        foreach (FieldDescription variant in field.Children) choose.Items.Add(variant.Label);
        item.Header = Row(Label(field, label), choose, extra);

        FieldDescription? current = field.Children.FirstOrDefault(v => slot.Find()?.Element(v.Name) != null);
        choose.SelectedIndex = current == null ? 0 : field.Children.ToList().IndexOf(current) + 1;

        void ShowVariant()
        {
            item.Items.Clear();
            if (current == null) return;
            var variantSlot = new Slot(slot, current.Name);
            foreach (FieldDescription child in current.Children)
                item.Items.Add(FieldRow(child, new Slot(variantSlot, child.Name)));
        }
        ShowVariant();

        choose.SelectionChanged += (_, _) =>
        {
            FieldDescription? chosen = choose.SelectedIndex <= 0 ? null : field.Children[choose.SelectedIndex - 1];
            if (chosen == current) return;
            XtblElement? existing = current == null ? null : slot.Find()?.Element(current.Name);
            if (existing is { HasElements: true } &&
                MessageBox.Show($"Switch {field.Label} from {current!.Label} to {chosen?.Label ?? "none"}?\n\n" +
                                $"This entry's {current.Label} values are removed.", "Table editor",
                                MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes)
            {
                choose.SelectedIndex = field.Children.ToList().IndexOf(current) + 1;
                return;
            }
            existing?.Remove();
            if (chosen != null) slot.Create().AddElement(chosen.Name);
            current = chosen;
            changed();
            ShowVariant();
            item.IsExpanded = true;
        };
        return item;
    }

    /// <summary>Any number of items of one kind (List, Grid), with Add and Remove.</summary>
    TreeViewItem ListRow(FieldDescription field, Slot slot, string? label, UIElement? extra)
    {
        FieldDescription? itemField = field.Item;
        string itemName = field.ItemElementName;
        var item = new TreeViewItem();
        var add = new Button { Content = "Add", Padding = new Thickness(8, 0, 8, 0), Margin = new Thickness(6, 0, 0, 0) };
        var count = Note("");
        item.Header = Row(Label(field, label), add, extra, count);

        void ShowItems()
        {
            item.Items.Clear();
            List<XtblElement> items = slot.Find()?.ElementsNamed(itemName).ToList() ?? [];
            count.Text = items.Count == 1 ? "1 item" : $"{items.Count} items";
            add.IsEnabled = field.MaxItems is not int max || items.Count < max;
            int number = 0;
            foreach (XtblElement element in items)
            {
                number++;
                var remove = new Button { Content = "Remove", Padding = new Thickness(8, 0, 8, 0), Margin = new Thickness(6, 0, 0, 0) };
                remove.Click += (_, _) =>
                {
                    element.Remove();
                    changed();
                    ShowItems();
                };
                string itemLabel = $"{itemField?.Label ?? itemName} {number}";
                item.Items.Add(itemField == null
                    ? RawRow(element)
                    : FieldRow(itemField, new Slot(element), itemLabel, remove));
            }
        }
        ShowItems();

        add.Click += (_, _) =>
        {
            bool holdsValues = itemField != null && itemField.Type is not (FieldType.Element or FieldType.ComboElement or
                FieldType.List or FieldType.Grid or FieldType.Flags or FieldType.Vector or FieldType.Color);
            slot.Create().AddElement(itemName, holdsValues ? itemField!.Default ?? "" : null);
            changed();
            ShowItems();
            item.IsExpanded = true;
        };
        return item;
    }

    /// <summary>A value the description doesn't cover: shown as it is in the file, text editable.</summary>
    TreeViewItem RawRow(XtblElement element)
    {
        if (element.HasElements)
        {
            var group = new TreeViewItem { Header = Row(new TextBlock { Text = element.Name, Width = LabelWidth }) };
            foreach (XtblElement child in element.Elements) group.Items.Add(RawRow(child));
            return group;
        }
        return new TreeViewItem
        {
            Header = Row(new TextBlock { Text = element.Name, Width = LabelWidth }, TextEditor(null, new Slot(element)).Control),
        };
    }

    // ---- Layout --------------------------------------------------------------------------------

    static StackPanel Row(params UIElement?[] parts)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 1, 0, 1) };
        foreach (UIElement? part in parts)
            if (part != null) row.Children.Add(part);
        return row;
    }

    static TextBlock Label(FieldDescription field, string? label) => new()
    {
        Text = label ?? field.Label,
        Width = LabelWidth,
        VerticalAlignment = VerticalAlignment.Center,
        TextTrimming = TextTrimming.CharacterEllipsis,
        ToolTip = Help(field),
    };

    static TextBlock Note(string text) => new()
    {
        Text = text,
        Foreground = Brushes.Gray,
        VerticalAlignment = VerticalAlignment.Center,
        Margin = new Thickness(8, 0, 0, 0),
    };

    /// <summary>What hovering over a field's name shows: everything the description says about it.</summary>
    static string Help(FieldDescription field)
    {
        var lines = new List<string> { $"{field.Label}   ({field.TypeName}, written as <{field.Name}>)" };
        if (!string.IsNullOrWhiteSpace(field.Help)) lines.Add(field.Help);
        if (field.Default is { Length: > 0 }) lines.Add("Default: " + field.Default);
        if (field.Min != null || field.Max != null) lines.Add($"Allowed: {field.Min?.ToString() ?? "any"} to {field.Max?.ToString() ?? "any"}");
        if (field.ReferenceFile != null) lines.Add($"Points to: {field.ReferenceField} in {field.ReferenceFile}");
        if (field.Extension != null) lines.Add("File type: " + field.Extension);
        if (field.Unique) lines.Add("Must be different in every entry.");
        if (!field.Required) lines.Add("Optional.");
        return string.Join("\n", lines);
    }

    static void ShowProblem(Control control, string? problem)
    {
        if (problem == null)
        {
            control.ClearValue(Control.BorderBrushProperty);
            control.ClearValue(FrameworkElement.ToolTipProperty);
        }
        else
        {
            control.BorderBrush = Brushes.Red;
            control.ToolTip = "Not saved: " + problem;
        }
    }
}
