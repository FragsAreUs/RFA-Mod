using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using RfaFormats.Interface;

namespace RfaEditor;

/// <summary>
/// The document's Lua script beside its screen: coloured (comments, strings, keywords), each string that names one of
/// its elements (vint_object_find("compass_group"), ...) underlined. Click one to pick that element; pick an element
/// and the script's lines naming it are marked and listed with its properties.
/// </summary>
public partial class VintView
{
    readonly HashSet<string> scriptNames = new(StringComparer.OrdinalIgnoreCase);          // element names the script uses
    readonly Dictionary<string, List<int>> scriptLines = new(StringComparer.OrdinalIgnoreCase); // and the lines it does on
    readonly Dictionary<string, List<Run>> scriptRuns = new(StringComparer.OrdinalIgnoreCase);
    readonly List<Inline> lineStarts = [];  // the first piece of each line, to show a line
    readonly List<Run> marked = [];

    static readonly HashSet<string> LuaWords =
    [
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in", "local", "nil", "not", "or",
        "repeat", "return", "then", "true", "until", "while",
    ];

    void LoadScript()
    {
        scriptNames.Clear();
        scriptLines.Clear();
        scriptRuns.Clear();
        lineStarts.Clear();
        marked.Clear();
        var paragraph = new Paragraph { Margin = new System.Windows.Thickness(0) };
        var flow = new FlowDocument(paragraph) { PageWidth = 4000, FontFamily = new FontFamily("Consolas"), FontSize = 12 };
        string? name = screen?.Script;
        string? text = name == null || sources == null ? null : sources.Script(name) ?? (name.EndsWith(".lua", StringComparison.OrdinalIgnoreCase) ? null : sources.Script(name + ".lua"));
        if (text == null)
        {
            ScriptTitle.Text = name == null ? "SCRIPT: NONE" : $"SCRIPT: {name.ToUpperInvariant()} (NOT FOUND)";
            ScriptCount.Text = "";
            ScriptBox.Document = flow;
            return;
        }
        ScriptTitle.Text = "SCRIPT: " + name!.ToUpperInvariant();
        var elementNames = new HashSet<string>(AllNodes().Select(n => n.Name), StringComparer.OrdinalIgnoreCase);
        foreach (VintAnimation animation in screen?.Animations ?? []) elementNames.Add(animation.Name);  // the script plays them by name
        Brush comment = new SolidColorBrush(Color.FromRgb(106, 153, 85)), str = new SolidColorBrush(Color.FromRgb(206, 145, 120)),
              keyword = new SolidColorBrush(Color.FromRgb(86, 156, 214)), number = new SolidColorBrush(Color.FromRgb(181, 206, 168)),
              element = new SolidColorBrush(Color.FromRgb(255, 200, 90));
        string[] lines = text.Replace("\r\n", "\n").Split('\n');
        for (int l = 0; l < lines.Length; l++)
        {
            Inline? last = paragraph.Inlines.LastInline;
            AddLine(paragraph, lines[l], l + 1, elementNames, comment, str, keyword, number, element);
            if (paragraph.Inlines.LastInline == last) paragraph.Inlines.Add(new Run(""));
            lineStarts.Add(last == null ? paragraph.Inlines.FirstInline : last.NextInline);
            paragraph.Inlines.Add(new LineBreak());
        }
        ScriptBox.Document = flow;
        ScriptCount.Text = $"{lines.Length:N0} lines, {scriptNames.Count} of its elements named";
    }

    // One line, in coloured pieces.
    void AddLine(Paragraph paragraph, string line, int number, HashSet<string> elementNames, Brush comment, Brush str, Brush keyword, Brush numberBrush, Brush element)
    {
        int i = 0;
        var plain = new System.Text.StringBuilder();
        void Flush()
        {
            if (plain.Length == 0) return;
            paragraph.Inlines.Add(new Run(plain.ToString()));
            plain.Clear();
        }
        while (i < line.Length)
        {
            char c = line[i];
            if (c == '-' && i + 1 < line.Length && line[i + 1] == '-')
            {
                Flush();
                paragraph.Inlines.Add(new Run(line[i..]) { Foreground = comment });
                return;
            }
            if (c is '"' or '\'')
            {
                Flush();
                int end = i + 1;
                while (end < line.Length && line[end] != c) end += line[end] == '\\' ? 2 : 1;
                end = Math.Min(end, line.Length - 1);
                string inside = line[(i + 1)..Math.Max(i + 1, end)];
                var run = new Run(line[i..(end + 1)]) { Foreground = str };
                if (elementNames.Contains(inside))
                {
                    run.Foreground = element;
                    run.TextDecorations = System.Windows.TextDecorations.Underline;
                    run.Tag = inside;
                    run.Cursor = Cursors.Hand;
                    scriptNames.Add(inside);
                    if (!scriptLines.TryGetValue(inside, out var at)) scriptLines[inside] = at = [];
                    at.Add(number);
                    if (!scriptRuns.TryGetValue(inside, out var runs)) scriptRuns[inside] = runs = [];
                    runs.Add(run);
                }
                paragraph.Inlines.Add(run);
                i = end + 1;
                continue;
            }
            if (char.IsLetter(c) || c == '_')
            {
                int end = i;
                while (end < line.Length && (char.IsLetterOrDigit(line[end]) || line[end] == '_')) end++;
                string word = line[i..end];
                if (LuaWords.Contains(word))
                {
                    Flush();
                    paragraph.Inlines.Add(new Run(word) { Foreground = keyword });
                }
                else plain.Append(word);
                i = end;
                continue;
            }
            if (char.IsDigit(c))
            {
                int end = i;
                while (end < line.Length && (char.IsLetterOrDigit(line[end]) || line[end] == '.')) end++;
                Flush();
                paragraph.Inlines.Add(new Run(line[i..end]) { Foreground = numberBrush });
                i = end;
                continue;
            }
            plain.Append(c);
            i++;
        }
        Flush();
    }

    // A click on an underlined name picks that element.
    void OnScriptClick(object sender, MouseButtonEventArgs e)
    {
        var position = ScriptBox.GetPositionFromPoint(e.GetPosition(ScriptBox), true);
        if (position?.Parent is not Run { Tag: string name }) return;
        if (AllNodes().FirstOrDefault(n => n.Name.Equals(name, StringComparison.OrdinalIgnoreCase)) is { } node) Select(node);
        else if (screen?.Animations.FindIndex(a => a.Name.Equals(name, StringComparison.OrdinalIgnoreCase)) is int at and >= 0)
        {
            BottomTabs.SelectedItem = AnimationsTab;  // an animation's name: show it
            AnimationList.SelectedIndex = at;
        }
    }

    // Marks the lines naming the picked element and shows the first.
    void ShowInScript(string name)
    {
        foreach (Run run in marked) run.Background = null;
        marked.Clear();
        if (!scriptRuns.TryGetValue(name, out var runs)) return;
        var mark = new SolidColorBrush(Color.FromArgb(90, 255, 170, 40));
        foreach (Run run in runs)
        {
            run.Background = mark;
            marked.Add(run);
        }
        runs[0].BringIntoView();
    }

    void ShowScriptLine(int line)
    {
        if (line < 1 || line > lineStarts.Count) return;
        Inline start = lineStarts[line - 1];
        start.BringIntoView();
        ScriptBox.Selection.Select(start.ContentStart, start.ContentEnd);
        ScriptBox.Focus();
    }
}
