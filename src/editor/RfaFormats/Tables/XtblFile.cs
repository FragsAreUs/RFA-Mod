using System.Text;

namespace RfaFormats.Tables;

/// <summary>
/// An .xtbl table: the game's data tables (weapons, vehicles, characters, effects, ...). A typical
/// file:
///
///   &lt;root&gt;
///     &lt;Table&gt;              the entries, e.g. one &lt;Weapon&gt; per weapon
///     &lt;TableTemplates&gt;     saved entries to start new ones from (Volition's editor)
///     &lt;TableDescription&gt;   what every field of an entry is: type, range, choices, help text
///     &lt;EntryCategories&gt;    the folders entries are sorted into in the editor
///   &lt;/root&gt;
///
/// Each entry has &lt;_Editor&gt;&lt;Category&gt;Entries:Alien&lt;/Category&gt;&lt;/_Editor&gt; for its folder. Some
/// tables have no description; a few files have another layout altogether.
/// </summary>
public sealed class XtblFile
{
    public XtblElement Document { get; }
    public XtblElement? Root { get; }
    public XtblElement? Table { get; }
    public FieldDescription? Description { get; }

    public XtblFile(string text)
    {
        Document = XtblXml.Parse(text);
        Root = Document.Elements.FirstOrDefault();
        Table = Root?.Element("Table");
        XtblElement? description = Root?.Element("TableDescription");
        if (description != null) Description = FieldDescription.Read(description);
    }

    /// <summary>The game's files are plain ASCII; Latin-1 keeps any other byte exactly as it was.</summary>
    public static XtblFile FromBytes(byte[] data) => new(Encoding.Latin1.GetString(data));

    public byte[] ToBytes() => Encoding.Latin1.GetBytes(XtblXml.Write(Document));

    public IEnumerable<XtblElement> Entries => Table?.Elements ?? [];

    /// <summary>What an entry is called: its Name field, else the first value it has.</summary>
    public static string EntryName(XtblElement entry) =>
        entry.ValueOf("Name") is { Length: > 0 } name ? name
        : entry.Elements.Select(e => e.Value).FirstOrDefault(v => v.Length > 0) ?? entry.Name;

    /// <summary>The entry's folder in Volition's editor, e.g. ["Entries", "Alien"]; empty if it has none.</summary>
    public static string[] EntryCategory(XtblElement entry) =>
        entry.Element("_Editor")?.ValueOf("Category")?.Split(':', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries) ?? [];

    // ---- Adding and removing entries ----------------------------------------------------------
    // Everything else in the file stays as written: an entry added goes on a line of its own, indented
    // like its neighbours, and a removed one takes only its own line with it.

    /// <summary>Whether an entry has a Name field of its own (most tables' entries do; the game finds them by it).</summary>
    public static bool HasNameField(XtblElement entry) => entry.Element("Name") != null;

    /// <summary>A name no entry in the table has (ignoring case): the one given, else it with _2, _3, ... added.</summary>
    public string UnusedName(string wanted)
    {
        var taken = new HashSet<string>(Entries.Select(EntryName), StringComparer.OrdinalIgnoreCase);
        if (!taken.Contains(wanted)) return wanted;
        for (int n = 2; ; n++)
            if (!taken.Contains($"{wanted}_{n}")) return $"{wanted}_{n}";
    }

    /// <summary>
    /// A copy of an entry with every field the same, put right after it, under a new name (if it has
    /// a Name field; give it one no other entry has, as the game finds entries by name).
    /// </summary>
    public XtblElement CopyEntry(XtblElement entry, string name)
    {
        if (Table == null || entry.Parent != Table) throw new ArgumentException("not an entry of this table");
        XtblElement copy = entry.Copy();
        if (copy.Element("Name") is XtblElement field) field.Value = name;
        Table.InsertAfter(entry, copy);
        return copy;
    }

    /// <summary>
    /// A new entry at the end of the table with only a name, and the folder given (Volition's
    /// editor's _Editor/Category): every other field is left out, so the game uses its defaults.
    /// The kind of entry (&lt;Weapon&gt;, &lt;Item&gt;, ...) is the one the table's description
    /// describes, else that of its entries.
    /// </summary>
    public XtblElement NewEntry(string name, IReadOnlyList<string> category)
    {
        string kind = Description?.Name ?? Entries.FirstOrDefault()?.Name
            ?? throw new InvalidOperationException("the table has no description or entries to tell what an entry is");
        if (Table == null) throw new InvalidOperationException("the file has no <Table>");
        XtblElement entry = Table.AddElement(kind);
        entry.AddElement("Name", name);
        if (category.Count > 0) entry.AddElement("_Editor").AddElement("Category", string.Join(":", category));
        return entry;
    }

    /// <summary>The saved entries Volition's editor started new ones from (&lt;TableTemplates&gt;; 46 tables have some).</summary>
    public IEnumerable<XtblElement> Templates => Root?.Element("TableTemplates")?.Elements ?? [];

    /// <summary>A new entry at the end of the table, a copy of a template under a new name.</summary>
    public XtblElement NewEntryFrom(XtblElement template, string name)
    {
        if (Table == null) throw new InvalidOperationException("the file has no <Table>");
        XtblElement entry = template.Copy();
        if (entry.Element("Name") is XtblElement field) field.Value = name;
        else entry.AddElement("Name", name);
        return Table.AddElement(entry);
    }

    /// <summary>Takes an entry out of the table.</summary>
    public void RemoveEntry(XtblElement entry)
    {
        if (Table == null || entry.Parent != Table) throw new ArgumentException("not an entry of this table");
        entry.Remove();
    }

    /// <summary>
    /// The values a Reference field can point to, e.g. for "Item.Name": the Name of every &lt;Item&gt;
    /// entry in this table.
    /// </summary>
    public IEnumerable<string> ReferenceTargets(string? field)
    {
        string entryName = "", valueName = "Name";
        if (!string.IsNullOrEmpty(field))
        {
            int dot = field.IndexOf('.');
            entryName = dot < 0 ? field : field[..dot];
            if (dot >= 0) valueName = field[(dot + 1)..];
        }
        return Entries
            .Where(e => entryName.Length == 0 || e.Name.Equals(entryName, StringComparison.OrdinalIgnoreCase))
            .Select(e => e.ValueOf(valueName))
            .OfType<string>()
            .Where(v => v.Length > 0)
            .Distinct(StringComparer.OrdinalIgnoreCase);
    }
}
