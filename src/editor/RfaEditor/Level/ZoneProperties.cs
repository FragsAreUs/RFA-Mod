using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using RfaFormats.Zones;

namespace RfaEditor;

/// <summary>
/// The selected object's properties, each with an editor that fits its value: text (longer text
/// makes the object larger; the zone is laid out again when saved), a check box for flags,
/// numbers and lists of numbers (vectors, colours, rotations). Lists of links are shown but not
/// edited. The position and rotation ("oP") are left out: the Transform section shows them.
/// </summary>
static class ZonePropertyRows
{
    const double LabelWidth = 140;

    /// <param name="changed">Called after a valid edit, with the property changed (while typing, too).</param>
    /// <param name="committed">Called when an edit is finished: a text box is left, or a check box clicked (one undo step).</param>
    public static IEnumerable<UIElement> For(ZoneObject obj, ZoneNames names, Action<ZoneProperty> changed, Action committed)
    {
        foreach (ZoneProperty property in obj.Properties)
        {
            string name = names.Property(property.NameHash);
            if (name == "oP" && property.Size == 48) continue;
            var label = new TextBlock
            {
                Text = Readable(name), Width = LabelWidth, VerticalAlignment = VerticalAlignment.Center,
                TextTrimming = TextTrimming.CharacterEllipsis, Foreground = Theme.Brush("DimTextBrush"),
                ToolTip = $"{name}\nType {property.Type} ({TypeName(property.Type)}), {property.Size} bytes",
            };
            var row = new DockPanel { Margin = new Thickness(0, 2, 0, 2) };
            DockPanel.SetDock(label, Dock.Left);
            row.Children.Add(label);
            UIElement editor = Editor(property, name, changed);
            if (editor is TextBox box) box.LostKeyboardFocus += (_, _) => committed();
            if (editor is CheckBox check) check.Click += (_, _) => committed();
            row.Children.Add(editor);
            yield return row;
        }
    }

    /// <summary>A property's name for people: "chunk_name" becomes "Chunk name". The real name is in its tool tip.</summary>
    static string Readable(string name)
    {
        string spaced = name.Replace('_', ' ').Trim();
        return spaced.Length == 0 ? name : char.ToUpperInvariant(spaced[0]) + spaced[1..];
    }

    static string TypeName(int type) => type switch
    {
        ZoneProperty.TextType => "text",
        ZoneProperty.ValueType => "value",
        ZoneProperty.ListType => "list",
        _ => "unknown",
    };

    static UIElement Editor(ZoneProperty property, string name, Action<ZoneProperty> changed)
    {
        switch (property.Type)
        {
            case ZoneProperty.TextType:
                return TextEditor(property.Text, $"Text, up to {ZoneProperty.LongestText} characters", text =>
                {
                    if (text.Length > ZoneProperty.LongestText) return $"at most {ZoneProperty.LongestText} characters";
                    property.SetText(text);
                    changed(property);
                    return null;
                });
            case ZoneProperty.ValueType when property.Size == 1:
                var check = new CheckBox { IsChecked = property.Value[0] != 0, VerticalAlignment = VerticalAlignment.Center };
                check.Click += (_, _) =>
                {
                    property.SetValue([check.IsChecked == true ? (byte)1 : (byte)0]);
                    changed(property);
                };
                return check;
            case ZoneProperty.ValueType when property.Size == 4 && LooksLikeWholeNumber(name, property.Value):
                uint whole = BitConverter.ToUInt32(property.Value);
                return TextEditor(whole.ToString(CultureInfo.InvariantCulture), $"A whole number (0x{whole:X8})", text =>
                {
                    if (!uint.TryParse(text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out uint value)) return "must be a whole number";
                    property.SetValue(BitConverter.GetBytes(value));
                    changed(property);
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
                return TextEditor(string.Join(", ", property.Floats().Select(f => f.ToString("0.####", CultureInfo.InvariantCulture))), hint, text =>
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
                    changed(property);
                    return null;
                });
            default:
                return new TextBlock
                {
                    Text = $"{property.Size} bytes" + (property.Type == ZoneProperty.ListType ? " (links to other objects; not editable yet)" : ""),
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

    /// <summary>A text box that applies its value when it is valid, and shows why when it isn't.</summary>
    static TextBox TextEditor(string text, string tip, Func<string, string?> apply)
    {
        var box = new TextBox { Text = text, ToolTip = tip, VerticalContentAlignment = VerticalAlignment.Center };
        box.TextChanged += (_, _) =>
        {
            string? problem = apply(box.Text);
            if (problem == null)
            {
                box.ClearValue(Control.BorderBrushProperty);
                box.ToolTip = tip;
            }
            else
            {
                box.BorderBrush = Theme.Brush("ErrorBrush");
                box.ToolTip = "Not applied: " + problem;
            }
        };
        return box;
    }
}
