namespace RfaFormats.Interface;

/// <summary>
/// A document's animations (after its elements in the file: an animation node and its tween children), as read and played
/// and changed. Their properties, named by the game's scripts and by guessing (see docs\game-notes.md, "Animations"):
/// an animation has max_loops (−1 for ever, 0 once, n more times) and loop_mode ("cycle" starts again, "bounce" plays
/// back); a tween has target_name (the element it moves, found by name in the document) and target_name_crc (CRC-32 of
/// that name in lower case), target_property (alpha, anchor, scale, rotation, blur, tint, ...), start_time and duration
/// (seconds), start_value and end_value (of the property's type), start_value_type and end_value_type ("stage" in every
/// one of the game's 1,927), algorithm (the easing: ease_in_quad, ease_out_quad, ease_in_out_quad, ease_in_exp,
/// ease_out_exp, ease_in_cubic; none is straight), and its own max_loops and loop_mode.
/// </summary>
public sealed class VintAnimation
{
    public required VintRawNode Raw { get; init; }
    public required VintTree Tree { get; init; }
    public string Name => Tree.Text(Raw.Name);
    public List<VintTween> Tweens { get; } = [];

    public int MaxLoops => (int)(VintTween.Value(Tree, Raw, "max_loops")?.Numbers?[0] ?? 0);
    public string LoopMode => VintTween.Value(Tree, Raw, "loop_mode")?.Text ?? "cycle";

    /// <summary>How long one play takes: until its last tween ends (with that tween's own loops).</summary>
    public float Length => Tweens.Count == 0 ? 0 : Tweens.Max(t => t.StartTime + t.Duration * (t.MaxLoops >= 0 ? t.MaxLoops + 1 : 1));

    /// <summary>A play's time (seconds since it started) as a time in one play, looping as it loops; −1 once it has ended.</summary>
    public float TimeIn(float played, out bool ended)
    {
        ended = false;
        float length = Math.Max(Length, 1e-3f);
        int loops = MaxLoops;
        float plays = played / length;
        if (loops >= 0 && plays >= loops + 1)
        {
            ended = true;
            bool backwards = LoopMode == "bounce" && loops % 2 == 1;
            return backwards ? 0 : length;
        }
        int k = (int)plays;
        float into = played - k * length;
        return LoopMode == "bounce" && k % 2 == 1 ? length - into : into;
    }

    public static List<VintAnimation> ReadAll(VintTree tree)
    {
        var animations = new List<VintAnimation>();
        foreach (VintRawNode raw in tree.Animations)
        {
            var animation = new VintAnimation { Raw = raw, Tree = tree };
            foreach (VintRawNode child in raw.Children)
                if (tree.Text(child.Type) == "tween") animation.Tweens.Add(new VintTween { Raw = child, Tree = tree });
            animations.Add(animation);
        }
        return animations;
    }
}

/// <summary>A tween: one property of one element, moved from a value to another over a time (see <see cref="VintAnimation"/>).</summary>
public sealed class VintTween
{
    public required VintRawNode Raw { get; init; }
    public required VintTree Tree { get; init; }

    /// <summary>The names of a tween's and an animation's properties, by hash.</summary>
    public static readonly IReadOnlyDictionary<uint, string> PropertyNames = new[]
    {
        "target_name", "target_name_crc", "target_property", "start_time", "duration", "start_value", "end_value",
        "start_value_type", "end_value_type", "algorithm", "max_loops", "loop_mode",
    }.ToDictionary(VintDocument.Hash, n => n);

    public static readonly IReadOnlyList<string> Algorithms =
        ["", "ease_in_quad", "ease_out_quad", "ease_in_out_quad", "ease_in_exp", "ease_out_exp", "ease_in_out_exp", "ease_in_cubic", "ease_out_cubic"];

    public string Name => Tree.Text(Raw.Name);
    public string Target => Value(Tree, Raw, "target_name")?.Text ?? "";
    public string Property => Value(Tree, Raw, "target_property")?.Text ?? "";
    public float StartTime => Value(Tree, Raw, "start_time")?.Numbers?[0] ?? 0;
    public float Duration => Value(Tree, Raw, "duration")?.Numbers?[0] ?? 0;
    public VintValue? StartValue => Value(Tree, Raw, "start_value");
    public VintValue? EndValue => Value(Tree, Raw, "end_value");
    public string Algorithm => Value(Tree, Raw, "algorithm")?.Text ?? "";
    public int MaxLoops => (int)(Value(Tree, Raw, "max_loops")?.Numbers?[0] ?? 0);
    public string LoopMode => Value(Tree, Raw, "loop_mode")?.Text ?? "cycle";

    // A node's property by name (its base value), as a value.
    internal static VintValue? Value(VintTree tree, VintRawNode raw, string name)
    {
        uint hash = VintDocument.Hash(name);
        foreach (VintRawProperty property in raw.Base)
            if (property.Hash == hash)
                return property.Type == VintDocument.StringValue
                    ? new VintValue(property.Type, null, tree.Text(property.StringIndex))
                    : new VintValue(property.Type, property.Numbers, null);
        return null;
    }

    /// <summary>Sets one of its properties (added if it hasn't it); its target's name also sets target_name_crc.</summary>
    public void Set(string name, VintValue value)
    {
        SetBytes(name, value.Type, value.Type == VintDocument.StringValue
            ? BitConverter.GetBytes((uint)Tree.StringIndex(value.Text ?? ""))
            : VintRawProperty.Encode(value.Type, value.Numbers ?? [0, 0, 0]));
        // The name's hash as it is (a float can't hold every u32 exactly).
        if (name == "target_name") SetBytes("target_name_crc", 2, BitConverter.GetBytes(VintDocument.Hash((value.Text ?? "").ToLowerInvariant())));
    }

    void SetBytes(string name, byte type, byte[] bytes)
    {
        uint hash = VintDocument.Hash(name);
        int at = Raw.Base.FindIndex(p => p.Hash == hash);
        var property = new VintRawProperty(type, hash, bytes);
        if (at >= 0) Raw.Base[at] = property;
        else Raw.Base.Add(property);
    }

    /// <summary>
    /// Its value at an animation's time: null before it starts; then from its start value to its end value by its easing
    /// over its duration, looping as its own loops say; its end value once done.
    /// </summary>
    public VintValue? At(float time)
    {
        if (StartValue is not { } first || EndValue is not { } last) return null;
        float local = time - StartTime;
        if (local < 0) return null;
        float duration = Math.Max(Duration, 1e-4f);
        float cycles = local / duration;
        int loops = MaxLoops;
        float f;
        if (loops >= 0 && cycles >= loops + 1) f = LoopMode == "bounce" && loops % 2 == 1 ? 0 : 1;
        else
        {
            int k = (int)cycles;
            f = cycles - k;
            if (LoopMode == "bounce" && k % 2 == 1) f = 1 - f;
        }
        f = Ease(Algorithm, f);
        if (first.Numbers == null || last.Numbers == null) return f < 1 ? first : last;  // text: switches at the end
        var numbers = new float[Math.Min(first.Numbers.Length, last.Numbers.Length)];
        for (int i = 0; i < numbers.Length; i++) numbers[i] = first.Numbers[i] + (last.Numbers[i] - first.Numbers[i]) * f;
        return first with { Numbers = numbers };
    }

    public static float Ease(string algorithm, float t) => algorithm switch
    {
        "ease_in_quad" => t * t,
        "ease_out_quad" => 1 - (1 - t) * (1 - t),
        "ease_in_out_quad" => t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t),
        "ease_in_exp" => t <= 0 ? 0 : MathF.Pow(2, 10 * (t - 1)),
        "ease_out_exp" => t >= 1 ? 1 : 1 - MathF.Pow(2, -10 * t),
        "ease_in_out_exp" => t <= 0 ? 0 : t >= 1 ? 1 : t < 0.5f ? MathF.Pow(2, 20 * t - 10) / 2 : (2 - MathF.Pow(2, -20 * t + 10)) / 2,
        "ease_in_cubic" => t * t * t,
        "ease_out_cubic" => 1 - MathF.Pow(1 - t, 3),
        _ => t,
    };
}
