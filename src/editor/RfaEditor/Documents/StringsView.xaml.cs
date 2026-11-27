using System.Windows.Controls;
using System.Windows.Data;
using RfaFormats.Interface;

namespace RfaEditor;

/// <summary>
/// A strings file (.le_strings): each string by its tag (named from the game's tables, else its hash), in every language
/// the file comes in (menu_us, menu_de, ... side by side), with a search over tags and texts. Read only.
/// </summary>
public partial class StringsView : UserControl
{
    // A string in every language shown: Texts[i] is the i-th column's.
    sealed class Row
    {
        public required uint Hash { get; init; }
        public required string Tag { get; init; }
        public required string[] Texts { get; init; }
        public required string Search { get; init; }  // tag and texts in lower case, for the search
    }

    List<Row> rows = [];
    List<string> languages = [];

    public StringsView()
    {
        InitializeComponent();
    }

    /// <summary>
    /// Shows the strings of one file, or of all of its languages: <paramref name="files"/> gives each language's (its name,
    /// "English", ...; or the file's name when it has no language) strings. <paramref name="tags"/>: the tags' names by hash.
    /// </summary>
    public void Show(string fileName, IReadOnlyList<(string Language, IReadOnlyList<(uint Hash, string Text)> Entries)> files, IReadOnlyDictionary<uint, string> tags, string? note)
    {
        languages = [.. files.Select(f => f.Language)];
        // Every tag in any of them, in the first file's order, then any others.
        var order = new List<uint>();
        var seen = new HashSet<uint>();
        foreach (var (_, entries) in files)
            foreach (var (hash, _) in entries)
                if (seen.Add(hash)) order.Add(hash);
        var texts = files.Select(f => f.Entries.GroupBy(e => e.Hash).ToDictionary(g => g.Key, g => g.First().Text)).ToList();
        rows = [.. order.Select(hash =>
        {
            string tag = tags.TryGetValue(hash, out var name) ? name : hash.ToString("X8");
            string[] all = [.. texts.Select(t => t.GetValueOrDefault(hash, ""))];
            return new Row { Hash = hash, Tag = tag, Texts = all, Search = (tag + "\n" + string.Join("\n", all)).ToLowerInvariant() };
        })];

        Columns.Columns.Clear();
        Columns.Columns.Add(new GridViewColumn { Header = "Tag", Width = 230, DisplayMemberBinding = new Binding(nameof(Row.Tag)) });
        double width = languages.Count switch { 1 => 640, 2 => 360, _ => 260 };
        for (int i = 0; i < languages.Count; i++)
            Columns.Columns.Add(new GridViewColumn { Header = languages[i], Width = width, DisplayMemberBinding = new Binding($"{nameof(Row.Texts)}[{i}]") });

        int named = rows.Count(r => tags.ContainsKey(r.Hash));
        SummaryText.Text = $"{fileName}: {rows.Count:N0} strings" +
                           (languages.Count > 1 ? $" in {languages.Count} languages ({string.Join(", ", languages)})" : "") +
                           $"; {named:N0} tags named from the game's tables, the rest shown by their hash." + (note != null ? " " + note : "");
        SearchBox.Text = "";
        ShowRows();
    }

    public void Hide()
    {
        rows = [];
        StringList.ItemsSource = null;
        DetailsBox.Text = "";
    }

    void OnSearch(object sender, TextChangedEventArgs e) => ShowRows();

    void ShowRows()
    {
        string query = SearchBox.Text.Trim().ToLowerInvariant();
        var shown = query.Length == 0 ? rows : rows.Where(r => r.Search.Contains(query)).ToList();
        StringList.ItemsSource = shown;
        CountText.Text = shown.Count == rows.Count ? $"{rows.Count:N0}" : $"{shown.Count:N0} of {rows.Count:N0}";
        DetailsBox.Text = shown.Count == 0 ? "No string has that in it." : "Pick a string to read it whole.";
    }

    void OnStringChosen(object sender, SelectionChangedEventArgs e)
    {
        if (StringList.SelectedItem is not Row row) return;
        var lines = new List<string> { $"{row.Tag}   (hash {row.Hash:X8})", "" };
        for (int i = 0; i < languages.Count; i++)
        {
            if (languages.Count > 1) lines.Add(languages[i] + ":");
            lines.Add(row.Texts[i].Length > 0 ? row.Texts[i] : "(none)");
            lines.Add("");
        }
        DetailsBox.Text = string.Join("\n", lines).TrimEnd();
    }
}
