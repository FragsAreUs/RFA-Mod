using System.Globalization;

namespace RfaFormats.Tables;

/// <summary>
/// How a table field is described in the table's own &lt;TableDescription&gt;: the information
/// Volition's table editor used to show and check each value. For example, from weapons.xtbl:
///
///   &lt;Element&gt;
///     &lt;Name&gt;Weapon_Class&lt;/Name&gt;  &lt;Type&gt;Selection&lt;/Type&gt;  &lt;Display_Name&gt;Weapon Class&lt;/Display_Name&gt;
///     &lt;Description&gt;the class of the weapon, ...&lt;/Description&gt;  &lt;Default&gt;bullet&lt;/Default&gt;
///     &lt;Choice&gt;bullet&lt;/Choice&gt;  &lt;Choice&gt;launcher&lt;/Choice&gt; ...
///   &lt;/Element&gt;
/// </summary>
public sealed class FieldDescription
{
    public string Name { get; init; } = "";
    public FieldType Type { get; init; }
    public string TypeName { get; init; } = "";  // as written, e.g. "Float"
    public string? DisplayName { get; init; }
    public string? Help { get; init; }           // <Description>
    public string? Default { get; init; }
    public bool Required { get; init; } = true;
    public bool Unique { get; init; }
    public double? Min { get; init; }
    public double? Max { get; init; }
    public int? MaxLength { get; init; }
    public IReadOnlyList<string> Choices { get; init; } = [];  // Selection
    public IReadOnlyList<string> Flags { get; init; } = [];    // Flags
    public string? ReferenceFile { get; init; }   // Reference: the table it points into, e.g. items_3d.xtbl
    public string? ReferenceField { get; init; }  // ...and which entries and field, e.g. "Item.Name"
    public string? Extension { get; init; }       // Filename
    public string? ItemName { get; init; }        // List: the element name of each item, if it differs
    public int? MinItems { get; init; }
    public int? MaxItems { get; init; }

    /// <summary>The fields inside an Element, the choices of a ComboElement, the item of a List or
    /// Grid, and the X/Y/Z or R/G/B of a Vector or Color.</summary>
    public IReadOnlyList<FieldDescription> Children { get; init; } = [];

    public string Label => DisplayName ?? Name.Replace('_', ' ');

    /// <summary>For a List or Grid: the description of one item.</summary>
    public FieldDescription? Item => Children.Count > 0 ? Children[0] : null;

    /// <summary>For a List or Grid: the element name each item is written with.</summary>
    public string ItemElementName => ItemName ?? Item?.Name ?? "Item";

    public bool HoldsFields => Type is FieldType.Element or FieldType.Vector or FieldType.Color;

    /// <summary>Reads one &lt;Element&gt; (or &lt;TableDescription&gt;, or Vector's &lt;X&gt;) of a description.</summary>
    public static FieldDescription Read(XtblElement element)
    {
        string typeName = element.ValueOf("Type") ?? "";
        FieldType type = Enum.TryParse(typeName, ignoreCase: true, out FieldType parsed) ? parsed : FieldType.Unknown;

        IEnumerable<XtblElement> children = element.ElementsNamed("Element");
        if (type == FieldType.Vector) children = new[] { "X", "Y", "Z" }.Select(element.Element).OfType<XtblElement>();
        if (type == FieldType.Color) children = new[] { "R", "G", "B" }.Select(element.Element).OfType<XtblElement>();

        XtblElement? reference = element.Element("Reference");
        return new FieldDescription
        {
            Name = element.ValueOf("Name") ?? element.Name,
            Type = type,
            TypeName = typeName,
            DisplayName = element.ValueOf("Display_Name"),
            Help = element.ValueOf("Description"),
            Default = element.ValueOf("Default"),
            Required = !"false".Equals(element.ValueOf("Required"), StringComparison.OrdinalIgnoreCase),
            Unique = "true".Equals(element.ValueOf("Unique"), StringComparison.OrdinalIgnoreCase),
            Min = Number(element.ValueOf("MinValue")),
            Max = Number(element.ValueOf("MaxValue")),
            MaxLength = (int?)Number(element.ValueOf("MaxLength")),
            Choices = element.ElementsNamed("Choice").Select(c => c.Value).ToList(),
            Flags = element.ElementsNamed("Flag").Select(f => f.Value).ToList(),
            ReferenceFile = reference?.ValueOf("File"),
            ReferenceField = reference?.ValueOf("Type"),
            Extension = element.ValueOf("Extension"),
            ItemName = element.ValueOf("Element_Name"),
            MinItems = (int?)Number(element.ValueOf("Min_Children")),
            MaxItems = (int?)Number(element.ValueOf("Max_Children") ?? element.ValueOf("MaxCount")),
            Children = children.Select(Read).ToList(),
        };
    }

    /// <summary>Why a value isn't allowed for this field, or null if it is.</summary>
    public string? Check(string value)
    {
        switch (Type)
        {
            case FieldType.Int:
                if (!long.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out long whole))
                    return "must be a whole number";
                return CheckRange(whole);
            case FieldType.Float:
                if (!double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out double number))
                    return "must be a number, e.g. 1.5";
                return CheckRange(number);
            default:
                return MaxLength is int max && value.Length > max ? $"can be at most {max} characters" : null;
        }
    }

    string? CheckRange(double value)
    {
        if (Min is double min && value < min) return $"must be at least {min.ToString(CultureInfo.InvariantCulture)}";
        if (Max is double max && value > max) return $"must be at most {max.ToString(CultureInfo.InvariantCulture)}";
        return null;
    }

    static double? Number(string? text) =>
        double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out double value) ? value : null;
}

/// <summary>The field types used by the game's table descriptions (all 13 of them).</summary>
public enum FieldType
{
    Unknown,
    String,
    Int,
    Float,
    Selection,     // one of a list of choices
    Flags,         // any number of named flags, each written as <Flag>name</Flag>
    Reference,     // the name of an entry in another table
    Filename,
    Element,       // a group of fields
    ComboElement,  // one of several groups, e.g. a weapon's shot type: Bullet, Thrown, Launcher, ...
    List,          // any number of items of one kind
    Grid,          // the same, shown as rows in Volition's editor
    Vector,        // X, Y, Z
    Color,         // R, G, B
}
