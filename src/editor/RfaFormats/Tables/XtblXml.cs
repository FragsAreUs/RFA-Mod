using System.Text;

namespace RfaFormats.Tables;

/// <summary>
/// The XML of an .xtbl file, kept exactly as written so that saving an unchanged file gives back
/// the same bytes and an edit changes only what was edited.
///
/// The game's XML reader is looser than standard XML, and the files rely on it: tag names match
/// regardless of case (a table opens with &lt;Table&gt; and closes with &lt;/table&gt;), and some
/// elements have text next to their child elements. So this is a small reader of its own, not
/// System.Xml. A file is a list of parts: elements, text (values and the whitespace between
/// elements) and markup such as comments.
/// </summary>
public abstract class XtblPart
{
    public XtblElement? Parent { get; internal set; }

    internal abstract void WriteTo(StringBuilder output);
}

/// <summary>Text between tags, stored as written (entities such as &amp;amp; not decoded).</summary>
public sealed class XtblText : XtblPart
{
    public string Raw { get; set; }

    public XtblText(string raw) => Raw = raw;

    public bool IsWhitespace => string.IsNullOrWhiteSpace(Raw);

    internal override void WriteTo(StringBuilder output) => output.Append(Raw);
}

/// <summary>A comment (&lt;!-- --&gt;) or declaration (&lt;? ?&gt;), kept as written.</summary>
public sealed class XtblMarkup : XtblPart
{
    public string Raw { get; }

    public XtblMarkup(string raw) => Raw = raw;

    internal override void WriteTo(StringBuilder output) => output.Append(Raw);
}

public sealed class XtblElement : XtblPart
{
    readonly List<XtblPart> parts = [];

    public string Name { get; }
    string tagRest;    // anything after the name in the start tag (attributes; the game's files have none)
    string? closeTag;  // the end tag as written, e.g. "</table>"; null if self-closing or never closed
    bool selfClosing;

    public XtblElement(string name, string tagRest = "", bool selfClosing = false)
    {
        Name = name;
        this.tagRest = tagRest;
        this.selfClosing = selfClosing;
        closeTag = selfClosing ? null : $"</{name}>";
    }

    public IReadOnlyList<XtblPart> Parts => parts;
    public IEnumerable<XtblElement> Elements => parts.OfType<XtblElement>();
    public bool HasElements => parts.Any(p => p is XtblElement);

    /// <summary>The first child element with this name (case doesn't matter, as in the game).</summary>
    public XtblElement? Element(string name) =>
        Elements.FirstOrDefault(e => e.Name.Equals(name, StringComparison.OrdinalIgnoreCase));

    public IEnumerable<XtblElement> ElementsNamed(string name) =>
        Elements.Where(e => e.Name.Equals(name, StringComparison.OrdinalIgnoreCase));

    /// <summary>The text of a child element, or null if there is no such element.</summary>
    public string? ValueOf(string name) => Element(name)?.Value;

    /// <summary>
    /// The element's own text with entities decoded and surrounding whitespace removed. Setting it
    /// replaces the element's content with that text.
    /// </summary>
    public string Value
    {
        get => Decode(string.Concat(parts.OfType<XtblText>().Select(t => t.Raw))).Trim();
        set
        {
            foreach (XtblPart part in parts) part.Parent = null;
            parts.Clear();
            Add(new XtblText(Encode(value)));
            if (selfClosing)
            {
                selfClosing = false;
                closeTag = $"</{Name}>";
            }
        }
    }

    /// <summary>
    /// Adds a child element after the existing ones, indented like the file around it: each child
    /// on its own line, one tab deeper than this element, with the end tag lined up with the
    /// children (the game's own style).
    /// </summary>
    public XtblElement AddElement(string name, string? value = null)
    {
        var element = new XtblElement(name);
        if (value != null) element.Value = value;
        return AddElement(element);
    }

    /// <summary>Adds an element (one in no file yet, such as a <see cref="Copy"/>) after the existing children, indented the same way.</summary>
    public XtblElement AddElement(XtblElement element)
    {
        if (element.Parent != null) throw new ArgumentException($"{element} is already in a file");
        if (selfClosing)
        {
            selfClosing = false;
            closeTag = $"</{Name}>";
        }

        string lineBreak = LineBreak();
        string childIndent = lineBreak + Indent() + "\t";
        XtblElement? last = Elements.LastOrDefault();
        if (last == null)
        {
            // No children yet: replace any whitespace-only content.
            if (parts.All(p => p is XtblText { IsWhitespace: true })) parts.Clear();
            Add(new XtblText(childIndent));
            Add(element);
            Add(new XtblText(childIndent));
        }
        else
        {
            int at = parts.IndexOf(last) + 1;
            Insert(at, new XtblText(WhitespaceBefore(last) ?? childIndent));
            Insert(at + 1, element);
        }
        return element;
    }

    /// <summary>A copy of this element and everything in it, exactly as written (in no file yet).</summary>
    public XtblElement Copy()
    {
        var copy = new XtblElement(Name, tagRest, selfClosing) { closeTag = closeTag };
        foreach (XtblPart part in parts)
            copy.Add(part switch
            {
                XtblElement element => element.Copy(),
                XtblText text => new XtblText(text.Raw),
                XtblMarkup markup => new XtblMarkup(markup.Raw),
                _ => throw new InvalidOperationException("unknown part " + part.GetType().Name),
            });
        return copy;
    }

    /// <summary>
    /// Puts an element in right after one of this element's children, on a line of its own with
    /// the same indent as that child.
    /// </summary>
    public void InsertAfter(XtblElement child, XtblElement element)
    {
        int at = parts.IndexOf(child);
        if (at < 0) throw new ArgumentException($"{child} isn't in {this}");
        Insert(at + 1, new XtblText(WhitespaceBefore(child) ?? LineBreak() + Indent() + "\t"));
        Insert(at + 2, element);
    }

    /// <summary>Removes this element from its parent, together with the line break and indent before it.</summary>
    public void Remove()
    {
        if (Parent == null) return;
        List<XtblPart> siblings = Parent.parts;
        int at = siblings.IndexOf(this);
        if (at > 0 && siblings[at - 1] is XtblText { IsWhitespace: true } before)
        {
            siblings.RemoveAt(at - 1);
            before.Parent = null;
        }
        siblings.Remove(this);
        Parent = null;
    }

    internal void Add(XtblPart part) => Insert(parts.Count, part);

    void Insert(int index, XtblPart part)
    {
        part.Parent = this;
        parts.Insert(index, part);
    }

    internal void Close(string rawCloseTag) => closeTag = rawCloseTag;

    internal void MarkUnclosed() => closeTag = null;

    /// <summary>The whitespace written just before a child element, if any.</summary>
    string? WhitespaceBefore(XtblElement child)
    {
        int at = parts.IndexOf(child);
        return at > 0 && parts[at - 1] is XtblText { IsWhitespace: true } text ? text.Raw : null;
    }

    /// <summary>The tabs or spaces this element's own line starts with.</summary>
    string Indent()
    {
        string? before = Parent?.WhitespaceBefore(this);
        if (before == null) return "";
        int lineStart = before.LastIndexOf('\n');
        return before[(lineStart + 1)..];
    }

    string LineBreak()
    {
        for (XtblElement? e = this; e != null; e = e.Parent)
            foreach (XtblText text in e.parts.OfType<XtblText>())
                if (text.Raw.Contains('\n')) return text.Raw.Contains("\r\n") ? "\r\n" : "\n";
        return "\r\n";
    }

    internal override void WriteTo(StringBuilder output)
    {
        output.Append('<').Append(Name).Append(tagRest);
        if (selfClosing)
        {
            output.Append("/>");
            return;
        }
        output.Append('>');
        foreach (XtblPart part in parts) part.WriteTo(output);
        if (closeTag != null) output.Append(closeTag);
    }

    public static string Decode(string raw) =>
        raw.Contains('&')
            ? raw.Replace("&lt;", "<").Replace("&gt;", ">").Replace("&quot;", "\"").Replace("&apos;", "'").Replace("&amp;", "&")
            : raw;

    public static string Encode(string value) => value.Replace("&", "&amp;").Replace("<", "&lt;").Replace(">", "&gt;");

    public override string ToString() => $"<{Name}>";
}

/// <summary>Reads and writes the loose XML of .xtbl files.</summary>
public static class XtblXml
{
    /// <summary>
    /// Reads a file's text into a document element (a nameless element holding the top-level
    /// parts). Writing it back with <see cref="Write"/> gives the same text.
    /// </summary>
    public static XtblElement Parse(string text)
    {
        var document = new XtblElement("");
        document.MarkUnclosed();
        var open = new Stack<XtblElement>();
        open.Push(document);
        int pos = 0;
        while (pos < text.Length)
        {
            int tag = text.IndexOf('<', pos);
            if (tag != pos)
            {
                int end = tag < 0 ? text.Length : tag;
                open.Peek().Add(new XtblText(text[pos..end]));
                pos = end;
                continue;
            }

            if (Starts(text, pos, "<!--") || Starts(text, pos, "<?") || Starts(text, pos, "<!"))
            {
                string closer = Starts(text, pos, "<!--") ? "-->" : ">";
                int end = text.IndexOf(closer, pos + 2, StringComparison.Ordinal);
                end = end < 0 ? text.Length : end + closer.Length;
                open.Peek().Add(new XtblMarkup(text[pos..end]));
                pos = end;
                continue;
            }

            int close = text.IndexOf('>', pos);
            if (close < 0)  // a stray '<' at the very end
            {
                open.Peek().Add(new XtblText(text[pos..]));
                break;
            }
            string inside = text[(pos + 1)..close];

            if (inside.StartsWith('/'))
            {
                // End tag: close the innermost open element with this name, ignoring case. Any
                // elements opened inside it and never closed stay unclosed, as written.
                string name = inside[1..].Trim();
                if (open.Any(e => e.Name.Equals(name, StringComparison.OrdinalIgnoreCase) && e != document))
                {
                    while (!open.Peek().Name.Equals(name, StringComparison.OrdinalIgnoreCase))
                        open.Pop().MarkUnclosed();
                    open.Pop().Close(text[pos..(close + 1)]);
                }
                else
                {
                    open.Peek().Add(new XtblText(text[pos..(close + 1)]));  // an end tag with no start: keep it as text
                }
            }
            else
            {
                bool selfClosing = inside.EndsWith('/');
                string body = selfClosing ? inside[..^1] : inside;
                int nameEnd = 0;
                while (nameEnd < body.Length && !char.IsWhiteSpace(body[nameEnd])) nameEnd++;
                var element = new XtblElement(body[..nameEnd], body[nameEnd..], selfClosing);
                open.Peek().Add(element);
                if (!selfClosing)
                {
                    element.MarkUnclosed();  // until its end tag turns up
                    open.Push(element);
                }
            }
            pos = close + 1;
        }
        return document;
    }

    public static string Write(XtblElement document)
    {
        var output = new StringBuilder();
        foreach (XtblPart part in document.Parts) part.WriteTo(output);
        return output.ToString();
    }

    static bool Starts(string text, int at, string prefix) => string.CompareOrdinal(text, at, prefix, 0, prefix.Length) == 0;
}
