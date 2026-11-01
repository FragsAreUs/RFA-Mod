using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using RfaFormats.Zones;

namespace RfaEditor;

/// <summary>
/// The selected object's properties in the inspector, as the game's zone schema describes them
/// (rfg_zonex_format.txt, Volition's level editor's own; see <see cref="ZoneSchema"/>): in the
/// schema's order, each with its name and an editor for its type. Text; a list of choices for an
/// enum (its own, and the entries of the table it names, such as squads.xtbl's); a check box per
/// flag; a check box for true or false; whole numbers, numbers and handles; X, Y, Z for a vector;
/// a swatch and red, green, blue and alpha for a colour; the lowest and highest corners of a box.
///
/// As in Volition's editor, HIDDEN properties aren't shown and READ_ONLY ones (set by its tools: a
/// model's name, a light's curve files) can't be changed, unless <c>showAll</c> is on. Properties
/// the game's files leave out at their default (most lights have no light_flags) are shown as not
/// set, and setting one adds it to the object. CLOE_ONLY properties were never written into zone
/// files, so objects don't have them; with <c>showAll</c> their names are listed. Properties the
/// schema doesn't describe (those Volition's tools added: links, a navpoint's data, a model's
/// destruction data) are shown as before, with editors guessed from their values. The position and
/// rotation ("oP", or a navpoint's "just_pos") are left out: the Transform section shows them.
/// </summary>
sealed class ZonePropertyRows
{
    const double LabelWidth = 140;
    static readonly uint PositionAndRotation = NameHashes.PropertyHash("oP");
    static readonly uint NavpointPosition = NameHashes.PropertyHash("just_pos");

    readonly ZoneNames names;
    readonly ZoneSchema? schema;
    readonly Func<SchemaPopulate, IReadOnlyList<string>> choicesOf;

    /// <param name="choicesOf">An enum's choices from the table it names (see <see cref="SchemaPopulate"/>).</param>
    public ZonePropertyRows(ZoneNames names, ZoneSchema? schema, Func<SchemaPopulate, IReadOnlyList<string>> choicesOf)
    {
        this.names = names;
        this.schema = schema;
        this.choicesOf = choicesOf;
    }

    /// <param name="showAll">Show HIDDEN properties too, and let HIDDEN and READ_ONLY ones be changed.</param>
    /// <param name="changed">Called after a valid edit (while typing, too).</param>
    /// <param name="committed">Called when an edit is finished: a text box is left, a box ticked, a choice made (one undo step).</param>
    /// <param name="relaidOut">Called after a property was added to the object or taken out of it: its properties
    /// were read afresh, so the rows must be made again.</param>
    public IEnumerable<UIElement> For(ZoneObject obj, bool showAll, Action changed, Action committed, Action relaidOut)
    {
        SchemaClass? type = schema?.Class(obj.ClassHash);
        var described = new Dictionary<SchemaProperty, ZoneProperty>();
        var undescribed = new List<ZoneProperty>();
        foreach (ZoneProperty property in obj.Properties)
        {
            if (property.NameHash == PositionAndRotation && property.Size == 48 || property.NameHash == NavpointPosition && property.Size == 12) continue;
            if (schema?.Property(obj.ClassHash, property.NameHash) is { } known && !described.ContainsKey(known)) described[known] = property;
            else undescribed.Add(property);
        }

        var row = new RowMaker(this, obj, changed, committed, relaidOut);
        IEnumerable<SchemaProperty> listed = type?.Properties ?? schema?.Base?.Properties ?? [];
        foreach (SchemaProperty property in listed)
        {
            if (property.Hidden && !showAll) continue;
            bool editable = showAll || !(property.ReadOnly || property.Hidden);
            if (described.TryGetValue(property, out ZoneProperty? value)) yield return row.Described(property, value, editable);
            else if (property.Exported) yield return row.NotSet(property, editable);
        }
        // Properties of other classes (none in the game's zones), then those the schema doesn't describe.
        foreach (var (property, value) in described.Where(d => !listed.Contains(d.Key)))
            if (showAll || !property.Hidden) yield return row.Described(property, value, showAll || !(property.ReadOnly || property.Hidden));
        foreach (ZoneProperty property in undescribed) yield return row.Undescribed(property);

        if (showAll && type != null && type.Properties.Where(p => p.EditorOnly).Select(p => p.Name).ToList() is { Count: > 0 } editorOnly)
            yield return new TextBlock
            {
                Text = "Volition's editor only, never in the game's files: " + string.Join(", ", editorOnly),
                Foreground = Theme.Brush("DimTextBrush"), FontSize = 11, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 6, 0, 0),
            };
    }

    /// <summary>A property's name for people: "chunk_name" becomes "Chunk name". The real name is in its tool tip.</summary>
    static string Readable(string name)
    {
        string spaced = name.Replace('_', ' ').Trim();
        return spaced.Length == 0 ? name : char.ToUpperInvariant(spaced[0]) + spaced[1..];
    }

    /// <summary>Makes the rows for one object.</summary>
    sealed class RowMaker(ZonePropertyRows rows, ZoneObject obj, Action changed, Action committed, Action relaidOut)
    {
        // ---- Rows ------------------------------------------------------------------------------

        /// <summary>A property the schema describes and the object has.</summary>
        public UIElement Described(SchemaProperty property, ZoneProperty value, bool editable)
        {
            bool fits = value.Type == property.ZoneType && (property.ZoneSize == 0 || value.Size == property.ZoneSize);
            if (fits && property.Type == SchemaType.Color)
            {
                // Its swatch beside the name, and its four numbers on a line of their own (they don't fit beside it).
                (FrameworkElement swatch, FrameworkElement numbers) = ColorEditor(value);
                swatch.HorizontalAlignment = HorizontalAlignment.Left;
                numbers.Margin = new Thickness(0, 3, 0, 2);
                swatch.IsEnabled = numbers.IsEnabled = editable;
                var block = new StackPanel();
                block.Children.Add(Row(Label(property, editable, $"In the zone: {TypeName(value.Type)}, {value.Size} bytes"), swatch));
                block.Children.Add(numbers);
                return block;
            }
            FrameworkElement editor = !fits ? Guessed(value, property.Name)
                : property.Type switch
                {
                    SchemaType.Enum => EnumEditor(property, value),
                    SchemaType.Flags => FlagsEditor(property, value),
                    SchemaType.Bool => BoolEditor(value),
                    SchemaType.Int => IntEditor(value),
                    SchemaType.Handle => HandleEditor(value),
                    SchemaType.Float => FloatEditor(value),
                    SchemaType.Vector => NumbersEditor(value, [("X", "AxisXBrush"), ("Y", "AxisYBrush"), ("Z", "AxisZBrush")], 0.05),
                    SchemaType.Box => BoxEditor(value),
                    _ => TextEditor(value),
                };
            editor.IsEnabled = editable;
            return Row(Label(property, editable, $"In the zone: {TypeName(value.Type)}, {value.Size} bytes"), editor);
        }

        /// <summary>
        /// A property the schema describes that the object doesn't have: the game's files leave some
        /// out at their default. Flags can be ticked straight away (the property is added with the
        /// first flag and taken out with the last, as in the game's files); anything else is added
        /// with its default.
        /// </summary>
        public UIElement NotSet(SchemaProperty property, bool editable)
        {
            FrameworkElement editor;
            if (property.Type == SchemaType.Flags)
                editor = FlagsEditor(property, null);
            else
            {
                var panel = new DockPanel();
                var set = new Button { Content = "Set", Padding = new Thickness(8, 1, 8, 1), Margin = new Thickness(6, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center,
                                       ToolTip = $"Add {property.Name} to this object, with its default ({Shown(property.Default)})" };
                DockPanel.SetDock(set, Dock.Right);
                set.Click += (_, _) =>
                {
                    byte[] value = property.DefaultValue(rows.choicesOf) ?? [0];
                    obj.AddProperty(property.ZoneType, NameHashes.PropertyHash(property.ZoneName), value);
                    Finished(laidOut: true);
                };
                panel.Children.Add(set);
                panel.Children.Add(new TextBlock
                {
                    Text = $"not set (default {Shown(property.Default)})", Foreground = Theme.Brush("DimTextBrush"), FontStyle = FontStyles.Italic,
                    VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis,
                });
                editor = panel;
            }
            editor.IsEnabled = editable;
            return Row(Label(property, editable, "The object doesn't have it: the game's files leave it out at its default"), editor);
        }

        /// <summary>A property the schema doesn't describe: its name from the game's exe, and an editor guessed from its value.</summary>
        public UIElement Undescribed(ZoneProperty property)
        {
            string name = rows.names.Property(property.NameHash);
            var label = new TextBlock
            {
                Text = Readable(name), Width = LabelWidth, VerticalAlignment = VerticalAlignment.Center,
                TextTrimming = TextTrimming.CharacterEllipsis, Foreground = Theme.Brush("DimTextBrush"),
                ToolTip = $"{name}\nNot in the zone schema: written by Volition's tools (in the zone: {TypeName(property.Type)}, {property.Size} bytes)",
            };
            return Row(label, Guessed(property, name));
        }

        static string Shown(string? value) => string.IsNullOrEmpty(value) ? "empty" : value;

        static FrameworkElement Row(FrameworkElement label, FrameworkElement editor)
        {
            var row = new DockPanel { Margin = new Thickness(0, 2, 0, 2) };
            DockPanel.SetDock(label, Dock.Left);
            row.Children.Add(label);
            row.Children.Add(editor);
            return row;
        }

        /// <summary>The name, with a lock when it can't be changed, and everything the schema says about it on hover.</summary>
        static FrameworkElement Label(SchemaProperty property, bool editable, string inZone)
        {
            var label = new DockPanel { Width = LabelWidth, VerticalAlignment = VerticalAlignment.Center, Background = Brushes.Transparent };
            if (!editable)
            {
                TextBlock locked = Theme.Icon(Icons.Locked, 10, Theme.Brush("DimTextBrush"));
                locked.Margin = new Thickness(0, 0, 5, 0);
                DockPanel.SetDock(locked, Dock.Left);
                label.Children.Add(locked);
            }
            label.Children.Add(new TextBlock
            {
                Text = Readable(property.Name), TextTrimming = TextTrimming.CharacterEllipsis, VerticalAlignment = VerticalAlignment.Center,
                Foreground = Theme.Brush("DimTextBrush"),
            });

            var lines = new List<string> { property.Name + (property.ZoneName != property.Name ? $" (written as {property.ZoneName})" : "") };
            if (property.Description is { Length: > 0 } description) lines.Add(description);
            lines.Add(property.Type switch
            {
                SchemaType.Text => "Text" + (property.OpenFile ? ": a file name (Volition's editor picked it from a file)" : ""),
                SchemaType.Handle => "Another object (its handle), or none",
                SchemaType.Int => "A whole number",
                SchemaType.Float => "A number",
                SchemaType.Bool => "On or off",
                SchemaType.Vector => "A vector: X, Y, Z",
                SchemaType.Color => "A colour: red, green, blue and alpha, 0 to 1 (more is brighter)",
                SchemaType.Box => "A box: its lowest corner and its highest, around the object",
                SchemaType.Flags => "Flags: " + string.Join(", ", property.FlagNames),
                SchemaType.Enum => "One of: " + (property.Choices.Count > 0 ? string.Join(", ", property.Choices.Take(12)) + (property.Choices.Count > 12 ? ", ..." : "") : "") +
                                   (property.Populate != null ? (property.Choices.Count > 0 ? ", or " : "") + $"the entries of {property.Populate.Table}" : ""),
                _ => property.Tag,
            } + $"   (schema type {property.Tag})");
            if (property.Default != null) lines.Add("Default: " + Shown(property.Default));
            if (property.ReadOnly) lines.Add("Read-only in Volition's editor: its tools set it.");
            if (property.Hidden) lines.Add("Hidden in Volition's editor.");
            if (!editable) lines.Add("Tick \"Show hidden and read-only properties\" below to change it anyway.");
            lines.Add(inZone);
            label.ToolTip = string.Join("\n", lines);
            return label;
        }

        static string TypeName(int type) => type switch
        {
            ZoneProperty.TextType => "text",
            ZoneProperty.ValueType => "a value",
            ZoneProperty.ListType => "a list",
            _ => $"type {type}",
        };

        /// <summary>An edit was made; with <paramref name="laidOut"/> the object's properties were read afresh.</summary>
        void Finished(bool laidOut = false)
        {
            changed();
            committed();
            if (laidOut) relaidOut();
        }

        // ---- Editors by type -------------------------------------------------------------------

        FrameworkElement TextEditor(ZoneProperty property) =>
            TextBoxEditor(property.Text, $"Text, up to {ZoneProperty.LongestText} characters", text =>
            {
                if (text.Length > ZoneProperty.LongestText) return $"at most {ZoneProperty.LongestText} characters";
                property.SetText(text);
                return null;
            });

        FrameworkElement BoolEditor(ZoneProperty property)
        {
            var check = new CheckBox { IsChecked = property.Value[0] != 0, VerticalAlignment = VerticalAlignment.Center };
            check.Click += (_, _) =>
            {
                property.SetValue([check.IsChecked == true ? (byte)1 : (byte)0]);
                Finished();
            };
            return check;
        }

        FrameworkElement IntEditor(ZoneProperty property)
        {
            int whole = BitConverter.ToInt32(property.Value);
            return TextBoxEditor(whole.ToString(CultureInfo.InvariantCulture), $"A whole number (0x{whole:X8})", text =>
            {
                if (!int.TryParse(text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out int value)) return "must be a whole number";
                property.SetValue(BitConverter.GetBytes(value));
                return null;
            });
        }

        FrameworkElement HandleEditor(ZoneProperty property)
        {
            uint handle = BitConverter.ToUInt32(property.Value);
            return TextBoxEditor(handle == ZoneObject.None ? "none" : handle.ToString("X8"),
                                 "Another object's handle (8 hex digits, as the outliner shows them), or none", text =>
            {
                string typed = text.Trim();
                uint value;
                if (typed.Length == 0 || typed.Equals("none", StringComparison.OrdinalIgnoreCase) || typed == "-1") value = ZoneObject.None;
                else if (!uint.TryParse(typed.StartsWith("0x", StringComparison.OrdinalIgnoreCase) ? typed[2..] : typed, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out value))
                    return "must be a handle (hex) or none";
                property.SetValue(BitConverter.GetBytes(value));
                return null;
            });
        }

        FrameworkElement FloatEditor(ZoneProperty property) =>
            TextBoxEditor(property.Floats()[0].ToString("0.#####", CultureInfo.InvariantCulture), "A number", text =>
            {
                if (!float.TryParse(text.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out float value) || !float.IsFinite(value)) return "must be a number";
                property.SetValue(BitConverter.GetBytes(value));
                return null;
            });

        /// <summary>Numbers side by side, each with its label (X, Y, Z; R, G, B, A), dragged or typed.</summary>
        FrameworkElement NumbersEditor(ZoneProperty property, (string Label, string Brush)[] parts, double step, int first = 0, Action? afterEach = null)
        {
            var grid = new Grid();
            float[] start = property.Floats();
            for (int i = 0; i < parts.Length; i++)
            {
                grid.ColumnDefinitions.Add(new ColumnDefinition());
                int at = first + i;
                var field = new NumberField(parts[i].Label, Theme.Brush(parts[i].Brush), "0.###", step, $"{parts[i].Label} ({property.Size / 4} numbers in all)")
                {
                    Margin = new Thickness(i == 0 ? 0 : 4, 0, 0, 0),
                };
                field.Show(start[at]);
                field.Changed += number =>
                {
                    byte[] value = property.Value;
                    BitConverter.TryWriteBytes(value.AsSpan(at * 4), (float)number);
                    property.SetValue(value);
                    afterEach?.Invoke();
                    changed();
                };
                field.Committed += committed;
                Grid.SetColumn(field, i);
                grid.Children.Add(field);
            }
            return grid;
        }

        /// <summary>A swatch of the colour (as far as the screen can show it), and its red, green, blue and alpha.</summary>
        (FrameworkElement Swatch, FrameworkElement Numbers) ColorEditor(ZoneProperty property)
        {
            var swatch = new Border
            {
                Width = 44, Height = 20, CornerRadius = new CornerRadius(4), BorderBrush = Theme.Brush("StrongBorderBrush"), BorderThickness = new Thickness(1),
                VerticalAlignment = VerticalAlignment.Center,
                ToolTip = "The colour as the screen can show it: values above 1 are brighter still",
            };
            void ShowSwatch()
            {
                float[] c = property.Floats();
                byte Channel(float v) => (byte)Math.Round(Math.Clamp(v, 0, 1) * 255);
                swatch.Background = new SolidColorBrush(Color.FromRgb(Channel(c[0]), Channel(c[1]), Channel(c[2])));
            }
            ShowSwatch();
            return (swatch, NumbersEditor(property, [("R", "AxisXBrush"), ("G", "AxisYBrush"), ("B", "AxisZBrush"), ("A", "DimTextBrush")], 0.01, afterEach: ShowSwatch));
        }

        /// <summary>
        /// A box's lowest and highest corners, around the object. A force field's box in its own
        /// space (object_space_bounding_box_min and _max) is its box again, so it follows.
        /// </summary>
        FrameworkElement BoxEditor(ZoneProperty property)
        {
            void Follow()
            {
                byte[] box = property.Value;
                if (obj.Property("object_space_bounding_box_min") is { Size: 12 } min) min.SetValue(box[..12]);
                if (obj.Property("object_space_bounding_box_max") is { Size: 12 } max) max.SetValue(box[12..]);
            }
            var panel = new StackPanel();
            (string, string)[] axes = [("X", "AxisXBrush"), ("Y", "AxisYBrush"), ("Z", "AxisZBrush")];
            foreach ((string corner, int first) in new[] { ("Lowest corner", 0), ("Highest corner", 3) })
            {
                FrameworkElement numbers = NumbersEditor(property, axes, 0.05, first, Follow);
                numbers.ToolTip = corner;
                numbers.Margin = new Thickness(0, first == 0 ? 0 : 3, 0, 0);
                panel.Children.Add(numbers);
            }
            return panel;
        }

        /// <summary>
        /// One of the enum's choices: its own, then its table's entries (read when the list is first
        /// opened, or when typing). A value that is neither (the game's DLC uses a few its tables
        /// don't list) is kept and shown first. Typing finds a choice; only a choice is applied.
        /// </summary>
        FrameworkElement EnumEditor(SchemaProperty schemaProperty, ZoneProperty property)
        {
            string current = property.Text;
            var box = new ComboBox
            {
                IsEditable = schemaProperty.Populate != null, IsTextSearchEnabled = true, VerticalAlignment = VerticalAlignment.Center,
                ItemsPanel = new ItemsPanelTemplate(new FrameworkElementFactory(typeof(VirtualizingStackPanel))),
                ToolTip = schemaProperty.Populate != null ? $"Pick, or type to find one of the entries of {schemaProperty.Populate.Table}" : null,
            };
            bool filling = true, loaded = false;
            List<string> Choices(bool full)
            {
                IEnumerable<string> choices = schemaProperty.Choices;
                if (full && schemaProperty.Populate != null) choices = choices.Concat(rows.choicesOf(schemaProperty.Populate));
                var list = choices.Distinct(StringComparer.OrdinalIgnoreCase).ToList();
                if (current.Length > 0 && !list.Contains(current, StringComparer.OrdinalIgnoreCase)) list.Insert(0, current);
                return list;
            }
            void Load()
            {
                if (loaded) return;
                loaded = true;
                filling = true;
                string text = box.Text;
                box.ItemsSource = Choices(full: true);
                box.Text = text;
                filling = false;
            }
            box.ItemsSource = Choices(full: schemaProperty.Populate == null);
            box.SelectedItem = ((List<string>)box.ItemsSource).FirstOrDefault(c => c.Equals(current, StringComparison.OrdinalIgnoreCase));
            if (box.IsEditable) box.Text = current;
            filling = false;

            void Apply(string text)
            {
                if (filling) return;
                if (box.IsEditable) Load();
                string? choice = ((List<string>)box.ItemsSource).FirstOrDefault(c => c.Equals(text, StringComparison.OrdinalIgnoreCase));
                if (choice == null)
                {
                    box.BorderBrush = Theme.Brush("ErrorBrush");
                    box.ToolTip = $"Not applied: \"{text}\" isn't one of the choices";
                    return;
                }
                box.ClearValue(Control.BorderBrushProperty);
                if (choice == property.Text) return;
                property.SetText(choice);
                changed();
            }
            box.DropDownOpened += (_, _) => Load();
            box.SelectionChanged += (_, _) =>
            {
                if (box.SelectedItem is not string choice) return;
                Apply(choice);
                if (!box.IsDropDownOpen && !box.IsKeyboardFocusWithin) committed();
            };
            if (box.IsEditable) box.AddHandler(TextBoxBase.TextChangedEvent, new TextChangedEventHandler((_, _) => Apply(box.Text)));
            box.DropDownClosed += (_, _) => committed();
            box.LostKeyboardFocus += (_, _) => committed();
            return box;
        }

        /// <summary>
        /// A check box per flag (and any the value names that the schema doesn't list). Zone files
        /// write them as text, each name followed by a space; one with none set is left out of the
        /// object, so ticking the first adds the property (<paramref name="property"/> is null when
        /// it isn't there) and clearing the last takes it out.
        /// </summary>
        FrameworkElement FlagsEditor(SchemaProperty schemaProperty, ZoneProperty? property)
        {
            var set = SchemaProperty.FlagsIn(property?.Text).ToList();
            var flags = schemaProperty.FlagNames.Concat(set.Where(s => !schemaProperty.FlagNames.Contains(s, StringComparer.OrdinalIgnoreCase))).ToList();
            var panel = new WrapPanel { VerticalAlignment = VerticalAlignment.Center };
            uint nameHash = NameHashes.PropertyHash(schemaProperty.ZoneName);
            foreach (string flag in flags)
            {
                var check = new CheckBox
                {
                    Content = flag.Replace('_', ' '), IsChecked = set.Contains(flag, StringComparer.OrdinalIgnoreCase), Margin = new Thickness(0, 1, 10, 1),
                    ToolTip = flag,
                };
                check.Click += (_, _) =>
                {
                    set.RemoveAll(s => s.Equals(flag, StringComparison.OrdinalIgnoreCase));
                    if (check.IsChecked == true) set.Add(flag);
                    var ordered = flags.Where(f => set.Contains(f, StringComparer.OrdinalIgnoreCase)).ToList();  // in the schema's order, as the game's files have them
                    ZoneProperty? now = obj.Property(nameHash);
                    if (ordered.Count == 0)
                    {
                        obj.RemoveProperty(nameHash);
                        Finished(laidOut: true);
                    }
                    else if (now == null)
                    {
                        obj.AddProperty(ZoneProperty.TextType, nameHash, [.. System.Text.Encoding.Latin1.GetBytes(SchemaProperty.FlagsText(ordered)), 0]);
                        Finished(laidOut: true);
                    }
                    else
                    {
                        now.SetText(SchemaProperty.FlagsText(ordered));
                        Finished();
                    }
                };
                panel.Children.Add(check);
            }
            return panel;
        }

        // ---- Guessed from the value -------------------------------------------------------------

        /// <summary>
        /// An editor for a property the schema doesn't describe, from its value: text, a check box for
        /// one byte, numbers and lists of numbers. Lists of links are shown but not edited.
        /// </summary>
        FrameworkElement Guessed(ZoneProperty property, string name)
        {
            switch (property.Type)
            {
                case ZoneProperty.TextType:
                    return TextEditor(property);
                case ZoneProperty.ValueType when property.Size == 1:
                    return BoolEditor(property);
                case ZoneProperty.ValueType when property.Size == 4 && LooksLikeWholeNumber(name, property.Value):
                    uint whole = BitConverter.ToUInt32(property.Value);
                    return TextBoxEditor(whole.ToString(CultureInfo.InvariantCulture), $"A whole number (0x{whole:X8})", text =>
                    {
                        if (!uint.TryParse(text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out uint value)) return "must be a whole number";
                        property.SetValue(BitConverter.GetBytes(value));
                        return null;
                    });
                case ZoneProperty.ValueType when property.Size % 4 == 0 && property.Size <= 64:
                    string hint = property.Size switch
                    {
                        12 => "x, y, z",
                        16 => "4 numbers (e.g. a colour: red, green, blue, strength)",
                        36 => "a rotation: the right, up and forward directions (3 numbers each)",
                        48 => "a position x, y, z, then a rotation: right, up and forward directions",
                        _ => $"{property.Size / 4} numbers",
                    };
                    return TextBoxEditor(string.Join(", ", property.Floats().Select(f => f.ToString("0.####", CultureInfo.InvariantCulture))), hint, text =>
                    {
                        string[] parts = text.Split([',', ' ', ';'], StringSplitOptions.RemoveEmptyEntries);
                        if (parts.Length != property.Size / 4) return $"needs {property.Size / 4} numbers";
                        var value = new byte[property.Size];
                        for (int i = 0; i < parts.Length; i++)
                        {
                            if (!float.TryParse(parts[i], NumberStyles.Float, CultureInfo.InvariantCulture, out float number)) return $"'{parts[i]}' isn't a number";
                            BitConverter.TryWriteBytes(value.AsSpan(i * 4), number);
                        }
                        property.SetValue(value);
                        return null;
                    });
                default:
                    // Lists are links (obj_links, dynamic_links) or data Volition's tools packed (navpoint_data, logic_handler_buffer, ...).
                    string what = property.Type != ZoneProperty.ListType ? ""
                        : name.Contains("link", StringComparison.OrdinalIgnoreCase) ? " (links to other objects; not editable yet)"
                        : " (data Volition's tools made; not editable yet)";
                    return new TextBlock
                    {
                        Text = $"{property.Size} bytes" + what,
                        Foreground = Theme.Brush("DimTextBrush"), VerticalAlignment = VerticalAlignment.Center, TextWrapping = TextWrapping.Wrap,
                    };
            }
        }

        /// <summary>
        /// A 4-byte value can be a number with a fraction or a whole number (handles, checksums, flags);
        /// the zone doesn't say which. Names tell for the usual whole-number ones; otherwise a value that
        /// makes a sensible number with a fraction is taken as one.
        /// </summary>
        static bool LooksLikeWholeNumber(string name, byte[] value)
        {
            string n = name.ToLowerInvariant();
            if (n.Contains("handle") || n.Contains("uid") || n.Contains("checksum") || n.Contains("flags") || n.Contains("index") ||
                n.Contains("count") || n.Contains("num_") || n.EndsWith("_id") || n == "mtype")
                return true;
            uint bits = BitConverter.ToUInt32(value);
            if (bits == 0) return false;
            float f = BitConverter.UInt32BitsToSingle(bits);
            return !float.IsFinite(f) || Math.Abs(f) < 1e-5f || Math.Abs(f) > 1e7f;
        }

        /// <summary>A text box that applies its value when it is valid, and shows why when it isn't. Leaving it finishes the edit.</summary>
        TextBox TextBoxEditor(string text, string tip, Func<string, string?> apply)
        {
            var box = new TextBox { Text = text, ToolTip = tip, VerticalContentAlignment = VerticalAlignment.Center };
            box.TextChanged += (_, _) =>
            {
                string? problem = apply(box.Text);
                if (problem == null)
                {
                    box.ClearValue(Control.BorderBrushProperty);
                    box.ToolTip = tip;
                    changed();
                }
                else
                {
                    box.BorderBrush = Theme.Brush("ErrorBrush");
                    box.ToolTip = "Not applied: " + problem;
                }
            };
            box.LostKeyboardFocus += (_, _) => committed();
            return box;
        }
    }
}
