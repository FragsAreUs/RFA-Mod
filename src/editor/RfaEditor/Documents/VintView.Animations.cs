using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;
using RfaFormats.Interface;

namespace RfaEditor;

/// <summary>
/// The document's animations (the Animations tab): one picked from the list plays over the screen, its tweens moving the
/// elements they name (by name, in this document) as the game would, looping by its own settings; the slider shows any
/// moment. Its tweens are listed; pick one to change it (its element, property, start, length, values, easing, loops),
/// or add and delete tweens and animations. Changes are saved with the document.
/// </summary>
public partial class VintView
{
    // A tween as listed.
    sealed record TweenRow(VintTween Tween)
    {
        public string Target => Tween.Target;
        public string Property => Tween.Property;
        public string Start => Tween.StartTime.ToString("0.###", CultureInfo.InvariantCulture);
        public string Length => Tween.Duration.ToString("0.###", CultureInfo.InvariantCulture) + (Tween.MaxLoops != 0 ? $" ×{(Tween.MaxLoops < 0 ? "∞" : (Tween.MaxLoops + 1).ToString())}" : "");
        public string From => Tween.StartValue?.ToString() ?? "";
        public string To => Tween.EndValue?.ToString() ?? "";
        public string Easing => Tween.Algorithm.Length > 0 ? Tween.Algorithm : "straight";
    }

    readonly Dictionary<VintNode, Dictionary<string, VintValue>> animated = [];  // values the animation gives elements now
    DispatcherTimer? animationTimer;
    DateTime playStarted;
    float playedBefore;
    bool scrubbing;  // moving the slider ourselves

    VintAnimation? Animation => AnimationList.SelectedItem is ComboBoxItem { Tag: VintAnimation a } ? a : null;

    void LoadAnimations()
    {
        StopAnimation();
        string? before = Animation?.Name;
        AnimationList.Items.Clear();
        if (screen == null) return;
        foreach (VintAnimation animation in screen.Animations)
            AnimationList.Items.Add(new ComboBoxItem { Content = $"{animation.Name} ({animation.Tweens.Count} tweens, {animation.Length:0.##} s)", Tag = animation });
        AnimationsTab.Header = screen.Animations.Count == 0 ? "Animations (none)" : $"Animations ({screen.Animations.Count})";
        int keep = screen.Animations.FindIndex(a => a.Name == before);
        AnimationList.SelectedIndex = screen.Animations.Count == 0 ? -1 : Math.Max(0, keep);
        if (screen.Animations.Count == 0) ShowTweens();
        // New animation / delete animation, after the list.
        if (AnimationList.Parent is WrapPanel bar && !bar.Children.OfType<Button>().Any(b => (string?)b.Tag == "animation-edit"))
        {
            var add = new Button { Content = "New animation", Tag = "animation-edit", Margin = new Thickness(0, 0, 6, 4), ToolTip = "Add an animation (with no tweens yet)" };
            add.Click += (_, _) => AddAnimation();
            var delete = new Button { Content = "Delete animation", Tag = "animation-edit", Margin = new Thickness(0, 0, 6, 4), ToolTip = "Delete the animation shown, and its tweens" };
            delete.Click += (_, _) => DeleteAnimation();
            bar.Children.Insert(1, add);
            bar.Children.Insert(2, delete);
        }
    }

    void OnAnimationChosen(object sender, SelectionChangedEventArgs e)
    {
        StopAnimation();
        ShowTweens();
    }

    void ShowTweens()
    {
        TweenPanel.Children.Clear();
        if (Animation is not { } animation)
        {
            TweenList.ItemsSource = null;
            AnimationText.Text = screen?.Animations.Count == 0 ? "This document has no animations." : "";
            return;
        }
        TweenList.ItemsSource = animation.Tweens.Select(t => new TweenRow(t)).ToList();
        AnimationTime.Maximum = Math.Max(0.01, animation.Length);
        scrubbing = true;
        AnimationTime.Value = 0;
        scrubbing = false;
        string loops = animation.MaxLoops < 0 ? $"loops for ever ({animation.LoopMode})" : animation.MaxLoops == 0 ? "plays once" : $"plays {animation.MaxLoops + 1} times ({animation.LoopMode})";
        AnimationText.Text = $"{animation.Length:0.##} s, {loops}";
        var addTween = new Button { Content = "Add tween", Margin = new Thickness(0, 0, 0, 8), ToolTip = "Add a tween to this animation: the picked element's alpha, from 1 to 0 over half a second" };
        addTween.Click += (_, _) => AddTween(animation);
        TweenPanel.Children.Add(addTween);
    }

    // ---- Playing -------------------------------------------------------------------------------------------

    void OnAnimationPlay(object sender, RoutedEventArgs e)
    {
        if (Animation == null) return;
        animationTimer ??= new DispatcherTimer(DispatcherPriority.Render) { Interval = TimeSpan.FromMilliseconds(33) };
        animationTimer.Tick -= OnAnimationTick;
        animationTimer.Tick += OnAnimationTick;
        playStarted = DateTime.UtcNow;
        playedBefore = 0;
        animationTimer.Start();
    }

    void OnAnimationTick(object? sender, EventArgs e)
    {
        if (Animation is not { } animation)
        {
            StopAnimation();
            return;
        }
        float played = playedBefore + (float)(DateTime.UtcNow - playStarted).TotalSeconds;
        float time = animation.TimeIn(played, out bool ended);
        ShowAt(animation, time);
        if (ended) animationTimer?.Stop();
    }

    void OnAnimationStop(object sender, RoutedEventArgs e) => StopAnimation(redraw: true);

    void StopAnimation(bool redraw = false)
    {
        animationTimer?.Stop();
        bool had = animated.Count > 0;
        animated.Clear();
        if (redraw && had) Draw();
    }

    void OnAnimationScrub(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (scrubbing || Animation is not { } animation) return;
        animationTimer?.Stop();
        ShowAt(animation, (float)AnimationTime.Value);
    }

    // The screen at a moment of an animation: each tween's value on the elements of its target's name.
    void ShowAt(VintAnimation animation, float time)
    {
        if (screen == null) return;
        animated.Clear();
        var byName = screen.All().GroupBy(n => n.Name, StringComparer.OrdinalIgnoreCase).ToDictionary(g => g.Key, g => g.ToList(), StringComparer.OrdinalIgnoreCase);
        // In start order, so a later tween of the same property wins once it has begun.
        foreach (VintTween tween in animation.Tweens.OrderBy(t => t.StartTime))
        {
            if (tween.At(time) is not { } value || !byName.TryGetValue(tween.Target, out var targets)) continue;
            foreach (VintNode target in targets)
            {
                if (!animated.TryGetValue(target, out var values)) animated[target] = values = new(StringComparer.OrdinalIgnoreCase);
                values[tween.Property] = value;
            }
        }
        Draw();
        scrubbing = true;
        AnimationTime.Value = Math.Clamp(time, 0, AnimationTime.Maximum);
        scrubbing = false;
        AnimationText.Text = $"{time:0.00} of {animation.Length:0.##} s";
    }

    // ---- Changing tweens -------------------------------------------------------------------------------------

    void OnTweenChosen(object sender, SelectionChangedEventArgs e)
    {
        if (TweenList.SelectedItem is not TweenRow { Tween: var tween }) return;
        TweenPanel.Children.Clear();
        TweenPanel.Children.Add(new TextBlock { Text = tween.Name, FontWeight = FontWeights.SemiBold, Margin = new Thickness(0, 0, 0, 6) });
        void Field(string label, string name, string current, IEnumerable<string>? choices = null, string? help = null)
        {
            var row = new DockPanel { Margin = new Thickness(0, 0, 0, 4) };
            var text = new TextBlock { Text = label, Width = 80, VerticalAlignment = VerticalAlignment.Center, ToolTip = help };
            DockPanel.SetDock(text, Dock.Left);
            row.Children.Add(text);
            FrameworkElement editor;
            Func<string> read;
            if (choices != null)
            {
                var box = new ComboBox { IsEditable = true, ItemsSource = choices.ToList(), Text = current };
                editor = box;
                read = () => box.Text;
                box.LostFocus += (_, _) => Commit();
                box.SelectionChanged += (_, _) => Dispatcher.BeginInvoke(new Action(Commit));
            }
            else
            {
                var box = new TextBox { Text = current };
                editor = box;
                read = () => box.Text;
                box.LostFocus += (_, _) => Commit();
                box.KeyDown += (_, k) =>
                {
                    if (k.Key != Key.Enter) return;
                    Commit();
                    k.Handled = true;
                };
            }
            void Commit()
            {
                string typed = read().Trim();
                if (typed == current) return;
                if (TweenValue(tween, name, typed) is not { } value)
                {
                    if (editor is Control control) control.BorderBrush = (System.Windows.Media.Brush)FindResource("ErrorBrush");
                    return;
                }
                tween.Set(name, value);
                current = typed;
                if (editor is Control ok) ok.ClearValue(BorderBrushProperty);
                if (screen != null) touched.Add(screen.Tree);
                ShowButtons();
                RefreshTweens(tween);
            }
            row.Children.Add(editor);
            TweenPanel.Children.Add(row);
        }
        var elementNames = screen?.All().Select(n => n.Name).Distinct(StringComparer.OrdinalIgnoreCase).OrderBy(n => n, StringComparer.OrdinalIgnoreCase) ?? Enumerable.Empty<string>();
        Field("element", "target_name", tween.Target, elementNames, "The element it moves (by name)");
        Field("property", "target_property", tween.Property, VintScreen.Properties.Where(p => p.Type != 4 && p.Type != 5).Select(p => p.Name), "What of it it moves");
        Field("start", "start_time", tween.StartTime.ToString("0.###", CultureInfo.InvariantCulture), help: "When it starts, in seconds from the animation's start");
        Field("length", "duration", tween.Duration.ToString("0.###", CultureInfo.InvariantCulture), help: "How long it takes, in seconds");
        Field("from", "start_value", tween.StartValue?.ToString() ?? "", help: "The value it starts at (as many numbers as the property has)");
        Field("to", "end_value", tween.EndValue?.ToString() ?? "", help: "The value it ends at");
        Field("easing", "algorithm", tween.Algorithm, VintTween.Algorithms, "How it speeds up and slows down (empty: at a steady pace)");
        Field("loops", "max_loops", tween.MaxLoops.ToString(CultureInfo.InvariantCulture), help: "How many more times it plays: 0 once, -1 for ever");
        Field("loop mode", "loop_mode", tween.LoopMode, ["cycle", "bounce"], "cycle: from the start again; bounce: back and forth");
        var delete = new Button { Content = "Delete tween", Margin = new Thickness(0, 8, 0, 0) };
        delete.Click += (_, _) =>
        {
            if (Animation is not { } animation) return;
            animation.Raw.Children.Remove(tween.Raw);
            animation.Tweens.Remove(tween);
            if (screen != null) touched.Add(screen.Tree);
            ShowButtons();
            ShowTweens();
            StatusText.Text = $"Deleted the tween {tween.Name}: not saved yet.";
        };
        TweenPanel.Children.Add(delete);
    }

    // A typed value for one of a tween's properties, of the type that property takes (its values: of its target property's).
    VintValue? TweenValue(VintTween tween, string name, string typed)
    {
        switch (name)
        {
            case "target_name" or "target_property" or "algorithm" or "loop_mode":
                return new VintValue(4, null, typed);
            case "start_time" or "duration":
                return float.TryParse(typed, NumberStyles.Float, CultureInfo.InvariantCulture, out float seconds) && seconds >= 0 ? new VintValue(3, [seconds], null) : null;
            case "max_loops":
                return int.TryParse(typed, NumberStyles.Integer, CultureInfo.InvariantCulture, out int loops) ? new VintValue(1, [loops], null) : null;
            default:
                // A value of the property moved: its type as the tween's own value, else as the property's.
                VintValue? like = tween.StartValue ?? tween.EndValue ?? VintScreen.NewValue(tween.Property);
                return like?.Parse(typed);
        }
    }

    // After a tween changed: the list (the same one picked) and the screen at the moment shown.
    void RefreshTweens(VintTween changed)
    {
        if (Animation is not { } animation) return;
        var rows = animation.Tweens.Select(t => new TweenRow(t)).ToList();
        TweenList.SelectionChanged -= OnTweenChosen;
        TweenList.ItemsSource = rows;
        TweenList.SelectedItem = rows.FirstOrDefault(r => r.Tween == changed);
        TweenList.SelectionChanged += OnTweenChosen;
        AnimationTime.Maximum = Math.Max(0.01, animation.Length);
        ShowAt(animation, (float)AnimationTime.Value);
        StatusText.Text = $"Changed the tween {changed.Name}: not saved yet.";
    }

    void AddTween(VintAnimation animation)
    {
        if (screen == null) return;
        VintTree tree = screen.Tree;
        string target = selected?.Name ?? screen.Roots.FirstOrDefault()?.Name ?? "";
        var raw = new VintRawNode { Name = tree.StringIndex($"{target}_alpha_twn"), Type = tree.StringIndex("tween") };
        animation.Raw.Children.Add(raw);
        var tween = new VintTween { Raw = raw, Tree = tree };
        tween.Set("target_name", new VintValue(4, null, target));
        tween.Set("target_property", new VintValue(4, null, "alpha"));
        tween.Set("start_time", new VintValue(3, [0], null));
        tween.Set("duration", new VintValue(3, [0.5f], null));
        tween.Set("max_loops", new VintValue(1, [0], null));
        tween.Set("start_value_type", new VintValue(4, null, "stage"));
        tween.Set("start_value", new VintValue(3, [1], null));
        tween.Set("end_value_type", new VintValue(4, null, "stage"));
        tween.Set("end_value", new VintValue(3, [0], null));
        animation.Tweens.Add(tween);
        touched.Add(tree);
        ShowButtons();
        ShowTweens();
        var rows = (List<TweenRow>)TweenList.ItemsSource;
        TweenList.SelectedItem = rows.FirstOrDefault(r => r.Tween == tween);
        StatusText.Text = $"Added a tween fading {target} out: not saved yet.";
    }

    void AddAnimation()
    {
        if (screen == null) return;
        VintTree tree = screen.Tree;
        var names = new HashSet<string>(screen.Animations.Select(a => a.Name), StringComparer.OrdinalIgnoreCase);
        string name = "new_anim";
        for (int i = 2; names.Contains(name); i++) name = $"new_anim_{i}";
        var raw = new VintRawNode { Name = tree.StringIndex(name), Type = tree.StringIndex("animation") };
        raw.Base.Add(new VintRawProperty(1, VintDocument.Hash("max_loops"), VintRawProperty.Encode(1, [0])));
        tree.Animations.Add(raw);
        screen.Animations.Add(new VintAnimation { Raw = raw, Tree = tree });
        touched.Add(tree);
        ShowButtons();
        LoadAnimations();
        AnimationList.SelectedIndex = screen.Animations.Count - 1;
        StatusText.Text = $"Added the animation {name}: not saved yet. The scripts play animations by name (lua: vint_object_find).";
    }

    void DeleteAnimation()
    {
        if (screen == null || Animation is not { } animation) return;
        if (MessageBox.Show(Window.GetWindow(this)!, $"Delete the animation {animation.Name} and its {animation.Tweens.Count} tweens?" +
                            (scriptNames.Contains(animation.Name) ? " The script names it." : ""), "Delete animation",
                            MessageBoxButton.OKCancel, MessageBoxImage.Question) != MessageBoxResult.OK) return;
        screen.Tree.Animations.Remove(animation.Raw);
        screen.Animations.Remove(animation);
        touched.Add(screen.Tree);
        ShowButtons();
        LoadAnimations();
        StatusText.Text = $"Deleted the animation {animation.Name}: not saved yet.";
    }
}
