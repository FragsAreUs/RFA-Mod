using System.Globalization;
using System.Numerics;

namespace RfaFormats.Interface;

/// <summary>
/// An interface document's elements as they are laid out at one resolution, to draw and change them: each element's
/// properties by name (its base values, with the resolution's own where it has them), over the document's tree
/// (<see cref="VintTree"/>), so values can be changed, added and removed, elements added, copied, renamed and deleted,
/// and the document written out again. The property names are the 28 of the 36 the game's 115 documents use that the
/// game's scripts, tables and project name.
/// <para>How the game lays an element out (as read from the documents' values): its anchor is its place in its
/// parent, then it is turned by its rotation (radians) and sized by its scale (negative flips it); a picture or text is
/// drawn from its offset (pixels: a 176-pixel picture at −88, −88 is centred on its anchor), or where its auto_offset
/// ("nw", "c", "se", ...) puts it; a picture is its image (a piece of a sheet texture, or a whole texture) drawn
/// source_se pixels wide and high; tint (red, green, blue, 0..1) and alpha multiply down the tree; children are drawn by
/// their depth, the deepest first.</para>
/// </summary>
public sealed class VintScreen
{
    public const string DefaultResolution = "1280x720";

    /// <summary>The properties known: name, value type and the value a new one starts with (type 4 is text).</summary>
    public static readonly IReadOnlyList<(string Name, byte Type, string Default)> Properties =
    [
        ("anchor", 7, "0, 0"), ("offset", 7, "0, 0"), ("auto_offset", 4, "nw"), ("scale", 7, "1, 1"), ("rotation", 3, "0"),
        ("depth", 1, "0"), ("visible", 5, "1"), ("alpha", 3, "1"), ("tint", 6, "1, 1, 1"), ("image", 4, "ui_hud_square"),
        ("source_se", 7, "32, 32"), ("text_tag", 4, "NEW TEXT"), ("font", 4, "font_body"), ("text_scale", 7, "1, 1"),
        ("wrap_width", 3, "200"), ("word_wrap", 5, "1"), ("horz_align", 4, "center"), ("vert_align", 4, "center"),
        ("force_case", 4, "upper"), ("shadow_enabled", 5, "1"), ("leading", 1, "0"), ("render_mode", 4, "additive_alpha"),
        ("blur", 3, "0"), ("mask", 5, "1"), ("clip_size", 7, "100, 100"), ("clip_enabled", 5, "1"), ("document_name", 4, ""),
        ("end_angle", 3, "0"),
    ];

    /// <summary>The element types the game knows (and how many of the game's elements are each).</summary>
    public static readonly IReadOnlyList<string> ElementTypes = ["group", "bitmap", "text", "clip", "document", "bitmap_circle", "map", "video"];

    /// <summary>The property names known, by hash.</summary>
    public static readonly IReadOnlyDictionary<uint, string> PropertyNames = Properties.ToDictionary(p => VintDocument.Hash(p.Name), p => p.Name);

    public string Resolution { get; }
    public VintTree Tree { get; }
    public List<VintNode> Roots { get; }
    public IReadOnlyList<string> Resolutions { get; }
    public string? Script { get; }
    public IReadOnlyList<string> TextureFiles { get; }

    VintScreen(VintTree tree, string resolution, List<VintNode> roots, IReadOnlyList<string> resolutions)
    {
        Tree = tree;
        Resolution = resolution;
        Roots = roots;
        Resolutions = resolutions;
        Script = tree.MetadataValue("lua_script_file");
        TextureFiles = tree.TextureFiles();
        Animations = VintAnimation.ReadAll(tree);
    }

    /// <summary>The document's animations (see <see cref="VintAnimation"/>).</summary>
    public List<VintAnimation> Animations { get; }

    /// <summary>Points the tweens that move an element by one name at another (after it was renamed); returns how many.</summary>
    public int Retarget(string from, string to)
    {
        int count = 0;
        foreach (VintTween tween in Animations.SelectMany(a => a.Tweens))
            if (tween.Target.Equals(from, StringComparison.OrdinalIgnoreCase))
            {
                tween.Set("target_name", new VintValue(VintDocument.StringValue, null, to));
                count++;
            }
        return count;
    }

    public static VintScreen Read(VintTree tree, string resolution = DefaultResolution)
    {
        var resolutions = new SortedSet<string>(StringComparer.OrdinalIgnoreCase) { DefaultResolution };
        foreach (VintRawNode root in tree.Roots) Gather(root);
        void Gather(VintRawNode raw)
        {
            foreach (var (r, _) in raw.Variants) resolutions.Add(tree.Text(r));
            foreach (VintRawNode child in raw.Children) Gather(child);
        }
        var screen = new VintScreen(tree, resolution, [], [.. resolutions]);
        screen.Roots.AddRange(tree.Roots.Select(r => screen.NodeOf(r, null)));
        return screen;
    }

    // An element as laid out at this resolution: its base values, then this resolution's own over them.
    VintNode NodeOf(VintRawNode raw, VintNode? parent)
    {
        var node = new VintNode(Tree.Text(raw.Name), Tree.Text(raw.Type), parent, raw);
        void Take(VintRawProperty property)
        {
            string name = NameOf(property.Hash);
            node.Sources[name] = property;
            node.Values[name] = property.Type == VintDocument.StringValue
                ? new VintValue(property.Type, null, Tree.Text(property.StringIndex))
                : new VintValue(property.Type, property.Numbers, null);
        }
        foreach (VintRawProperty property in raw.Base) Take(property);
        foreach (var (r, properties) in raw.Variants)
            if (Tree.Text(r).Equals(Resolution, StringComparison.OrdinalIgnoreCase))
                foreach (VintRawProperty property in properties) Take(property);
        foreach (VintRawNode child in raw.Children) node.Children.Add(NodeOf(child, node));
        return node;
    }

    public static string NameOf(uint hash) =>
        PropertyNames.TryGetValue(hash, out string? known) || VintTween.PropertyNames.TryGetValue(hash, out known) ? known! : hash.ToString("X8");

    static uint HashOf(string name) =>
        uint.TryParse(name, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint hash) && name.Length == 8 && !PropertyNames.Values.Contains(name)
            ? hash : VintDocument.Hash(name);

    /// <summary>
    /// Writes an element's values, name and removed values back into the document's tree: a value it had where it was
    /// read (this resolution's own, or the base one), a new one into its base values, a removed one out of every list.
    /// </summary>
    public void Apply(VintNode node)
    {
        VintRawNode raw = node.Raw;
        raw.Name = Tree.StringIndex(node.Name);
        foreach (string removed in node.Removed)
        {
            uint hash = HashOf(removed);
            raw.Base.RemoveAll(p => p.Hash == hash);
            foreach (var (_, properties) in raw.Variants) properties.RemoveAll(p => p.Hash == hash);
            node.Sources.Remove(removed);
        }
        node.Removed.Clear();
        foreach (var (name, value) in node.Values)
        {
            // A value as it was read keeps its bytes (a float can't hold every u32 exactly).
            if (node.Sources.TryGetValue(name, out VintRawProperty? same) && same.Type == value.Type &&
                (value.Type == VintDocument.StringValue ? Tree.Text(same.StringIndex) == value.Text
                                                        : same.Numbers is { } read && value.Numbers is { } now && read.SequenceEqual(now)))
                continue;
            byte[] bytes = value.Type == VintDocument.StringValue
                ? BitConverter.GetBytes((uint)Tree.StringIndex(value.Text ?? ""))
                : VintRawProperty.Encode(value.Type, value.Numbers ?? [0, 0, 0]);
            if (node.Sources.TryGetValue(name, out VintRawProperty? property) && property.Type == value.Type)
            {
                property.Value = bytes;
                continue;
            }
            if (property != null)
            {
                raw.Base.Remove(property);
                foreach (var (_, properties) in raw.Variants) properties.Remove(property);
            }
            var added = new VintRawProperty(value.Type, HashOf(name), bytes);
            raw.Base.Add(added);
            node.Sources[name] = added;
        }
    }

    /// <summary>A new element of a type under a parent (or at the top), with the values a new one of its type needs.</summary>
    public VintNode Add(VintNode? parent, string type, string name)
    {
        var raw = new VintRawNode { Name = Tree.StringIndex(name), Type = Tree.StringIndex(type) };
        string[] starting = type switch
        {
            "bitmap" or "bitmap_circle" or "map" => ["anchor", "image", "source_se", "auto_offset", "tint", "alpha", "depth"],
            "text" => ["anchor", "text_tag", "font", "text_scale", "tint", "alpha", "depth"],
            "clip" => ["anchor", "clip_size", "clip_enabled", "depth"],
            "document" => ["anchor", "document_name", "depth"],
            _ => ["anchor", "depth"],
        };
        foreach (string property in starting)
        {
            var (_, valueType, start) = Properties.First(p => p.Name == property);
            string value = property == "anchor" && parent == null ? "640, 360" : start;  // a new top element in the middle
            raw.Base.Add(valueType == VintDocument.StringValue
                ? new VintRawProperty(valueType, VintDocument.Hash(property), BitConverter.GetBytes((uint)Tree.StringIndex(value)))
                : new VintRawProperty(valueType, VintDocument.Hash(property), VintRawProperty.Encode(valueType, Parse(value))));
        }
        (parent?.Raw.Children ?? Tree.Roots).Add(raw);
        VintNode node = NodeOf(raw, parent);
        (parent?.Children ?? Roots).Add(node);
        return node;
    }

    /// <summary>A copy of an element and everything under it, right after it, named "&lt;name&gt;_copy".</summary>
    public VintNode Copy(VintNode node)
    {
        Apply(node);
        VintRawNode raw = node.Raw.Copy();
        raw.Name = Tree.StringIndex(node.Name + "_copy");
        List<VintRawNode> rawSiblings = node.Parent?.Raw.Children ?? Tree.Roots;
        rawSiblings.Insert(rawSiblings.IndexOf(node.Raw) + 1, raw);
        VintNode copy = NodeOf(raw, node.Parent);
        List<VintNode> siblings = node.Parent?.Children ?? Roots;
        siblings.Insert(siblings.IndexOf(node) + 1, copy);
        return copy;
    }

    /// <summary>Deletes an element and everything under it.</summary>
    public void Delete(VintNode node)
    {
        (node.Parent?.Raw.Children ?? Tree.Roots).Remove(node.Raw);
        (node.Parent?.Children ?? Roots).Remove(node);
    }

    static float[] Parse(string numbers) =>
        [.. numbers.Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries).Select(n => float.Parse(n, CultureInfo.InvariantCulture))];

    /// <summary>The document as it is now (call <see cref="Apply"/> on the elements changed first).</summary>
    public byte[] ToBytes() => Tree.ToBytes();

    /// <summary>Whether an element is one of this screen's (rather than a sub-document's).</summary>
    public bool Holds(VintNode node)
    {
        VintNode root = node;
        while (root.Parent != null) root = root.Parent;
        return Roots.Contains(root);
    }

    /// <summary>Every element, depth first.</summary>
    public IEnumerable<VintNode> All()
    {
        var stack = new Stack<VintNode>(Enumerable.Reverse(Roots));
        while (stack.Count > 0)
        {
            VintNode node = stack.Pop();
            yield return node;
            for (int i = node.Children.Count - 1; i >= 0; i--) stack.Push(node.Children[i]);
        }
    }

    /// <summary>A value of a known property as it starts on a new element (its type's default).</summary>
    public static VintValue? NewValue(string name)
    {
        int index = Properties.ToList().FindIndex(p => p.Name == name);
        if (index < 0) return null;
        var (_, type, start) = Properties[index];
        return type == VintDocument.StringValue ? new VintValue(4, null, start) : new VintValue(type, Parse(start), null);
    }
}

/// <summary>A property's value: its type (as the document's: 1-3 a number, 4 text, 5 a byte, 6 a colour, 7 two numbers) and the numbers or the text.</summary>
public sealed record VintValue(byte Type, float[]? Numbers, string? Text)
{
    public override string ToString() => Text ?? (Numbers == null ? "" : string.Join(", ", Numbers.Select(n => n.ToString("0.###", CultureInfo.InvariantCulture))));

    /// <summary>The same kind of value from text typed in ("1, 0.5", "center"); null if it can't be read as one.</summary>
    public VintValue? Parse(string typed)
    {
        if (Type == VintDocument.StringValue) return this with { Text = typed.Trim() };
        var parts = typed.Split([',', ' ', ';'], StringSplitOptions.RemoveEmptyEntries);
        var numbers = new List<float>();
        foreach (string part in parts)
        {
            if (!float.TryParse(part, NumberStyles.Float, CultureInfo.InvariantCulture, out float n)) return null;
            numbers.Add(n);
        }
        int wanted = Numbers?.Length ?? 1;
        return numbers.Count == wanted ? this with { Numbers = [.. numbers] } : null;
    }
}

/// <summary>An element as laid out: its name, type, properties by name, and children, over its node in the document's tree.</summary>
public sealed class VintNode(string name, string type, VintNode? parent, VintRawNode raw)
{
    public string Name { get; set; } = name;
    public string Type { get; } = type;
    public VintNode? Parent { get; } = parent;
    public VintRawNode Raw { get; } = raw;
    public List<VintNode> Children { get; } = [];
    public Dictionary<string, VintValue> Values { get; } = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Where each value was read from in the document's tree (to write it back); none for a value added here.</summary>
    public Dictionary<string, VintRawProperty> Sources { get; } = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Values removed here, to take out of the document when it is written.</summary>
    public HashSet<string> Removed { get; } = new(StringComparer.OrdinalIgnoreCase);

    float[]? N(string name) => Values.TryGetValue(name, out VintValue? v) ? v.Numbers : null;
    public string? TextOf(string name) => Values.TryGetValue(name, out VintValue? v) ? v.Text : null;

    public Vector2 Anchor => N("anchor") is { Length: 2 } a ? new Vector2(a[0], a[1]) : Vector2.Zero;
    public Vector2 Scale => N("scale") is { Length: 2 } s ? new Vector2(s[0], s[1]) : Vector2.One;
    public Vector2? Offset => N("offset") is { Length: 2 } o ? new Vector2(o[0], o[1]) : null;
    public Vector2? Size => N("source_se") is { Length: 2 } s ? new Vector2(s[0], s[1]) : null;
    public Vector2? ClipSize => N("clip_size") is { Length: 2 } s ? new Vector2(s[0], s[1]) : null;
    public float Rotation => N("rotation") is { Length: 1 } r ? r[0] : 0;
    public float Alpha => N("alpha") is { Length: 1 } a ? a[0] : 1;
    public int Depth => N("depth") is { Length: 1 } d ? (int)d[0] : 0;
    public Vector3 Tint => N("tint") is { Length: 3 } t ? new Vector3(t[0], t[1], t[2]) : Vector3.One;
    public bool Visible => N("visible") is not { Length: 1 } v || v[0] != 0;
    public bool Mask => N("mask") is { Length: 1 } m && m[0] != 0;
    public bool WordWrap => N("word_wrap") is { Length: 1 } w && w[0] != 0;
    public bool Shadow => N("shadow_enabled") is { Length: 1 } s && s[0] != 0;
    public float WrapWidth => N("wrap_width") is { Length: 1 } w ? w[0] : 0;
    public Vector2 TextScale => N("text_scale") is { Length: 2 } s ? new Vector2(s[0], s[1]) : Vector2.One;
    public string? Image => TextOf("image") is { Length: > 0 } i && !i.Equals("null", StringComparison.OrdinalIgnoreCase) ? i : null;

    /// <summary>Where a box of this size starts from its anchor: its offset, or where its auto_offset ("nw", "n", "ne",
    /// "w", "c", "e", "sw", "s", "se") puts it, or its top left.</summary>
    public Vector2 Start(Vector2 size)
    {
        if (Offset is Vector2 offset) return offset;
        string where = (TextOf("auto_offset") ?? "nw").ToLowerInvariant();
        float x = where.Contains('e') ? -size.X : where.Contains('w') ? 0 : -size.X / 2;
        float y = where.Contains('s') ? -size.Y : where.Contains('n') ? 0 : -size.Y / 2;
        return new Vector2(x, y);
    }

    /// <summary>Its path in the tree ("non_subtitles/main_hud/compass_group").</summary>
    public string Path => Parent == null ? Name : Parent.Path + "/" + Name;
}
