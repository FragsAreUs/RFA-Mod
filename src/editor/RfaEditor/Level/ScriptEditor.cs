using System.Globalization;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using RfaFormats.Zones;

namespace RfaEditor;

/// <summary>An object a script can name: its handle, class and outliner label.</summary>
sealed record ScriptObject(uint Handle, string Class, string Label)
{
    public string Text => Label.Length > 0 ? $"{Label}  ·  {Class}  ·  {Handle:X8}" : $"{Class}  ·  {Handle:X8}";
    public override string ToString() => Text;
}

/// <summary>
/// What a script editor works with: the function tables, the objects of the level a script can name,
/// the level's picked object (for "use the picked object"), the handler's own object (self), and how the
/// script is written back to its handler (null when it was, else why not).
/// </summary>
sealed record ScriptContext(LogicFunctions? Functions, IReadOnlyList<ScriptObject> Objects, Func<uint?> Picked, uint Self,
                            Func<LogicScript, string?> Apply)
{
    /// <summary>Texts a function's text parameter takes (function, its number, its name): a table's names, the game's own.</summary>
    public Func<string, int, string, IReadOnlyList<string>>? TextChoices { get; init; }
    /// <summary>Every state machine of the game's scripts, as recipes (read in the background the first time).</summary>
    public Func<Task<IReadOnlyList<LogicRecipe>>>? GameRecipes { get; init; }
}

/// <summary>
/// Edits a logic handler's script (<see cref="LogicScript"/>): its state machines, their states, the calls
/// each state makes on enter, each frame and on exit, the edges that move it on (where to, how long to wait,
/// the condition), and the handler's variables and arrays. The tree on the left is the script; the panel on
/// the right edits what is chosen in it. Calls are picked from the game's function tables, with each
/// parameter's own editor; objects are picked from the level's (those that fit first), or the level's picked
/// object is used. Recipes add ready-made state machines. Timelines are shown, not edited. The window stays
/// open beside the level (so objects can be picked there); OK and Apply write the script back.
/// </summary>
sealed class ScriptEditor : Window
{
    readonly LogicScript script;
    readonly LogicFunctions? functions;
    readonly Func<uint, string?>? objectName;
    readonly ScriptContext context;
    readonly Dictionary<uint, ScriptObject> objectsByHandle;
    readonly LogicText text;
    readonly TreeView tree = new();
    readonly StackPanel details = new() { Margin = new Thickness(10, 6, 10, 6) };
    readonly WrapPanel tools = new() { Margin = new Thickness(6, 6, 6, 0) };

    // What a tree item stands for.
    sealed record MachineNode(LogicObject Machine, List<LogicObject> Owner);
    sealed record StateNode(LogicObject Machine, LogicState State);
    sealed record CallNode(List<LogicCall> List, LogicCall Call, string When);
    sealed record EdgeNode(LogicObject Machine, LogicState State, LogicEdge Edge);
    sealed record VariableNode(int Index);
    sealed record ArrayNode(LogicArray Array);
    sealed record TimelineNode(LogicObject Timeline);
    sealed record GroupNode(string Name);

    /// <summary>Opens an editor for a script beside the level (not blocking it).</summary>
    /// <param name="offerRecipes">A new handler's: the recipes are offered first (an empty state machine if none is taken).</param>
    public static ScriptEditor Open(Window? owner, string title, LogicScript script, ScriptContext context, bool offerRecipes = false)
    {
        var window = new ScriptEditor(script, context) { Owner = owner, Title = title };
        window.Show();
        if (offerRecipes)
            window.Dispatcher.InvokeAsync(() =>
            {
                // A new handler has an empty state machine (one with none doesn't load); a recipe takes its place.
                LogicObject? placeholder = script.Objects.Count == 1 && IsEmpty(script.Objects[0]) ? script.Objects[0] : null;
                LogicObject? made = RecipeDialog.Choose(window, script, context);
                if (made != null && placeholder != null) script.Objects.Remove(placeholder);
                if (made == null && script.Objects.Count == 0)
                {
                    made = LogicObject.NewStateMachine("Statemachine1");
                    script.Objects.Add(made);
                }
                window.Rebuild(made == null ? null : new MachineNode(made, script.Objects));
            });
        return window;
    }

    static bool IsEmpty(LogicObject o) =>
        o.Kind == LogicObjectKind.StateMachine && o.States.Count == 1 && o.States[0] is var s &&
        s.OnEnter.Count + s.EachFrame.Count + s.OnExit.Count + s.Edges.Count + s.Inner.Count == 0;

    ScriptEditor(LogicScript script, ScriptContext context)
    {
        this.script = script;
        this.context = context;
        functions = context.Functions;
        objectsByHandle = context.Objects.GroupBy(o => o.Handle).ToDictionary(g => g.Key, g => g.First());
        objectName = h => objectsByHandle.TryGetValue(h, out ScriptObject? o) ? (o.Label.Length > 0 ? $"{o.Class} \"{o.Label}\"" : o.Class) : null;
        text = new LogicText(script, functions, objectName);

        Title = "Logic handler script";
        Width = 1100;
        Height = 720;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Background = Theme.Brush("WindowBrush");
        Foreground = Theme.Brush("TextBrush");

        var grid = new Grid();
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(5, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(4, GridUnitType.Star) });

        Grid.SetColumnSpan(tools, 3);
        grid.Children.Add(tools);

        tree.Margin = new Thickness(6);
        tree.Background = Theme.Brush("InputBrush");
        tree.Foreground = Theme.Brush("TextBrush");
        tree.FontFamily = new FontFamily("Consolas");
        tree.FontSize = 12;
        tree.SelectedItemChanged += (_, _) => ShowDetails();
        Grid.SetRow(tree, 1);
        grid.Children.Add(tree);

        var splitter = new GridSplitter { Width = 5, HorizontalAlignment = HorizontalAlignment.Stretch, Background = Theme.Brush("BorderBrush") };
        Grid.SetRow(splitter, 1);
        Grid.SetColumn(splitter, 1);
        grid.Children.Add(splitter);

        var scroller = new ScrollViewer { Content = details, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        Grid.SetRow(scroller, 1);
        Grid.SetColumn(scroller, 2);
        grid.Children.Add(scroller);

        var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(6) };
        var ok = new Button { Content = "OK", MinWidth = 80, Margin = new Thickness(0, 0, 6, 0), ToolTip = "Write the script to its handler and close." };
        var apply = new Button { Content = "Apply", MinWidth = 80, Margin = new Thickness(0, 0, 6, 0), ToolTip = "Write the script to its handler (one undo step) and keep editing." };
        var cancel = new Button { Content = "Close", IsCancel = true, MinWidth = 80, ToolTip = "Close without writing what wasn't applied." };
        ok.Click += (_, _) => { if (WriteBack()) Close(); };
        apply.Click += (_, _) => WriteBack();
        cancel.Click += (_, _) => Close();
        buttons.Children.Add(ok);
        buttons.Children.Add(apply);
        buttons.Children.Add(cancel);
        Grid.SetRow(buttons, 2);
        Grid.SetColumnSpan(buttons, 3);
        grid.Children.Add(buttons);

        Content = grid;
        Rebuild(null);
    }

    bool WriteBack()
    {
        string? problem = Problem() ?? context.Apply(script);
        if (problem == null) return true;
        MessageBox.Show(this, problem, Title, MessageBoxButton.OK, MessageBoxImage.Warning);
        return false;
    }

    // ---- The tree ------------------------------------------------------------------------------

    /// <summary>Makes the tree again, choosing the item for the same part of the script as <paramref name="keep"/>.</summary>
    void Rebuild(object? keep)
    {
        object? key = keep == null ? null : Key(keep);
        tree.Items.Clear();

        var variables = Item(new GroupNode("Variables"), $"Variables ({script.Variables.Count})");
        for (int i = 0; i < script.Variables.Count; i++) variables.Items.Add(Item(new VariableNode(i)));
        tree.Items.Add(variables);
        var arrays = Item(new GroupNode("Arrays"), $"Arrays ({script.Arrays.Count})");
        foreach (LogicArray a in script.Arrays) arrays.Items.Add(Item(new ArrayNode(a)));
        tree.Items.Add(arrays);
        foreach (LogicObject o in script.Objects) tree.Items.Add(ObjectItem(o, script.Objects));

        if (key != null && Find(tree.Items, key) is { } found)
        {
            for (var parent = found.Parent as TreeViewItem; parent != null; parent = parent.Parent as TreeViewItem) parent.IsExpanded = true;
            found.IsSelected = true;
            found.BringIntoView();
        }
        else ShowDetails();
        UpdateTools();
    }

    TreeViewItem ObjectItem(LogicObject o, List<LogicObject> owner)
    {
        if (o.Kind != LogicObjectKind.StateMachine) return Item(new TimelineNode(o));
        var machine = Item(new MachineNode(o, owner));
        machine.IsExpanded = true;
        foreach (LogicState s in o.States)
        {
            var state = Item(new StateNode(o, s));
            foreach (LogicCall c in s.OnEnter) state.Items.Add(Item(new CallNode(s.OnEnter, c, "on enter")));
            foreach (LogicCall c in s.EachFrame) state.Items.Add(Item(new CallNode(s.EachFrame, c, "each frame")));
            foreach (LogicCall c in s.OnExit) state.Items.Add(Item(new CallNode(s.OnExit, c, "on exit")));
            foreach (LogicObject inner in s.Inner) state.Items.Add(ObjectItem(inner, s.Inner));
            foreach (LogicEdge e in s.Edges) state.Items.Add(Item(new EdgeNode(o, s, e)));
            machine.Items.Add(state);
        }
        return machine;
    }

    TreeViewItem Item(object node, string? header = null) =>
        new() { Tag = node, Header = header ?? Header(node), Foreground = Theme.Brush("TextBrush") };

    static TreeViewItem? Find(ItemCollection items, object key)
    {
        foreach (TreeViewItem item in items.OfType<TreeViewItem>())
        {
            if (item.Tag != null && Equals(Key(item.Tag), key)) return item;
            if (Find(item.Items, key) is { } inner) return inner;
        }
        return null;
    }

    /// <summary>The part of the script an item stands for (the same after the tree is made again).</summary>
    static object Key(object node) => node switch
    {
        MachineNode m => m.Machine,
        StateNode s => s.State,
        CallNode c => c.Call,
        EdgeNode e => e.Edge,
        ArrayNode a => a.Array,
        TimelineNode t => t.Timeline,
        _ => node,  // records compare by value: a variable by its number, a group by its name
    };

    string Header(object node) => node switch
    {
        MachineNode m => $"statemachine \"{m.Machine.Name}\"",
        StateNode s => $"state \"{s.State.Name}\"" + (s.Machine.States.IndexOf(s.State) == s.Machine.FirstState ? "  (first)" : ""),
        CallNode c => $"{c.When}: {text.Call(c.Call)}",
        EdgeNode e => $"-> \"{e.Edge.To}\"{Wait(e.Edge)} when {text.Condition(e.Edge.Condition)}",
        VariableNode v => $"{script.VariableName(v.Index)} = {text.Value(script.Variables[v.Index])}",
        ArrayNode a => $"array \"{a.Array.Name}\" ({a.Array.Elements.Count})",
        TimelineNode t => $"timeline \"{t.Timeline.Name}\" (shown, not edited)",
        GroupNode g => g.Name,
        _ => node.ToString() ?? "",
    };

    static string Wait(LogicEdge e) =>
        e.MinWait == 0 && e.MaxWait == 0 ? "" : e.MinWait == e.MaxWait ? $" after {N(e.MinWait)}s" : $" after {N(e.MinWait)}-{N(e.MaxWait)}s";

    static string N(float f) => f.ToString("0.####", CultureInfo.InvariantCulture);

    object? Selected => (tree.SelectedItem as TreeViewItem)?.Tag;

    /// <summary>After a value changed: the chosen item's text, and its state's (a state's text doesn't depend on its items).</summary>
    void Refresh()
    {
        if (tree.SelectedItem is TreeViewItem item && item.Tag != null) item.Header = Header(item.Tag);
    }

    // ---- The tool bar --------------------------------------------------------------------------

    void UpdateTools()
    {
        tools.Children.Clear();
        object? node = Selected;
        LogicObject? machine = node switch { MachineNode m => m.Machine, StateNode s => s.Machine, EdgeNode e => e.Machine, _ => MachineOf(node) };
        LogicState? state = node switch { StateNode s => s.State, EdgeNode e => e.State, CallNode c => StateOf(c.Call), _ => null };

        Tool("+ State machine", () =>
        {
            var made = LogicObject.NewStateMachine(UniqueName("Statemachine", script.Objects.Select(o => o.Name.Text)));
            script.Objects.Add(made);
            return new MachineNode(made, script.Objects);
        });
        if (machine != null)
            Tool("+ State", () =>
            {
                LogicState made = machine.AddState(UniqueName("State", machine.States.Select(s => s.Name.Text)));
                return new StateNode(machine, made);
            });
        if (state != null)
        {
            Tool("+ On enter", () => AddCall(state.OnEnter, "on enter"));
            Tool("+ Each frame", () => AddCall(state.EachFrame, "each frame"));
            Tool("+ On exit", () => AddCall(state.OnExit, "on exit"));
            if (machine != null)
                Tool("+ Edge", () =>
                {
                    LogicState to = machine.States.FirstOrDefault(s => s != state) ?? state;
                    LogicEdge made = LogicObject.NewEdge(state, to);
                    state.Edges.Add(made);
                    return new EdgeNode(machine, state, made);
                });
            if (machine != null)
                Tool("+ Inner state machine", () =>
                {
                    var made = LogicObject.NewStateMachine(UniqueName("Statemachine", state.Inner.Select(o => o.Name.Text)));
                    state.Inner.Add(made);
                    return new MachineNode(made, state.Inner);
                });
        }
        Tool("+ Variable", () =>
        {
            var made = LogicValue.OfBoolean(false);
            made.Name = UniqueName("Variable", Enumerable.Range(0, script.Variables.Count).Select(script.VariableName));
            script.Variables.Add(made);
            return new VariableNode(script.Variables.Count - 1);
        });
        Tool("+ From recipe...", () => RecipeDialog.Choose(this, script, context) is { } made ? new MachineNode(made, script.Objects) : null);
        Tool("+ Array", () =>
        {
            var made = new LogicArray { Most = 16, Name = new FixedText(UniqueName("Array", script.Arrays.Select(a => a.Name.Text), 15)) };
            script.Arrays.Add(made);
            return new ArrayNode(made);
        });

        if (node is MachineNode or StateNode or CallNode or EdgeNode or VariableNode or ArrayNode)
        {
            tools.Children.Add(new Border { Width = 12 });
            Tool("Up", () => Move(node, -1));
            Tool("Down", () => Move(node, +1));
            Tool("Delete", () => Delete(node));
        }
    }

    void Tool(string label, Func<object?> act)
    {
        var button = new Button { Content = label, Margin = new Thickness(0, 0, 4, 4), Padding = new Thickness(8, 2, 8, 2) };
        button.Click += (_, _) =>
        {
            object? select = act();
            Rebuild(select ?? Selected);
        };
        tools.Children.Add(button);
    }

    object AddCall(List<LogicCall> list, string when)
    {
        LogicFunctions.Function? f = functions?.Find("sf_vf_set_boolean") ?? functions?.All.FirstOrDefault(x => !x.IsTest);
        LogicCall made = f != null ? LogicCall.Of(f) : LogicCall.NewAction("sf_vf_set_boolean");
        list.Add(made);
        return new CallNode(list, made, when);
    }

    static string UniqueName(string stem, IEnumerable<string> taken, int most = 31)
    {
        var used = new HashSet<string>(taken, StringComparer.OrdinalIgnoreCase);
        for (int i = 1; ; i++)
        {
            string name = $"{stem}{i}";
            if (name.Length > most) name = name[^most..];
            if (!used.Contains(name)) return name;
        }
    }

    LogicObject? MachineOf(object? node)
    {
        if (node is not CallNode c) return null;
        foreach (LogicObject o in AllMachines())
            if (o.States.Any(s => s.OnEnter.Contains(c.Call) || s.EachFrame.Contains(c.Call) || s.OnExit.Contains(c.Call))) return o;
        return null;
    }

    LogicState? StateOf(LogicCall call) =>
        AllMachines().SelectMany(o => o.States).FirstOrDefault(s => s.OnEnter.Contains(call) || s.EachFrame.Contains(call) || s.OnExit.Contains(call));

    IEnumerable<LogicObject> AllMachines()
    {
        IEnumerable<LogicObject> Of(LogicObject o) => new[] { o }.Concat(o.States.SelectMany(s => s.Inner).SelectMany(Of));
        return script.Objects.SelectMany(Of);
    }

    object? Move(object node, int by)
    {
        static void Shift<T>(List<T> list, T item, int by)
        {
            int at = list.IndexOf(item), to = at + by;
            if (at < 0 || to < 0 || to >= list.Count) return;
            list.RemoveAt(at);
            list.Insert(to, item);
        }
        switch (node)
        {
            case MachineNode m: Shift(m.Owner, m.Machine, by); break;
            case StateNode s:
                LogicState first = s.Machine.States[Math.Clamp(s.Machine.FirstState, 0, s.Machine.States.Count - 1)];
                Shift(s.Machine.States, s.State, by);
                s.Machine.SetFirstState(s.Machine.States.IndexOf(first));
                break;
            case CallNode c: Shift(c.List, c.Call, by); break;
            case EdgeNode e: Shift(e.State.Edges, e.Edge, by); break;
            case ArrayNode a: Shift(script.Arrays, a.Array, by); break;
            case VariableNode:
                MessageBox.Show(this, "Variables are kept in their order: calls refer to them by their place.", Title);
                break;
        }
        return node;
    }

    object? Delete(object node)
    {
        switch (node)
        {
            case MachineNode m:
                if (Ask($"Delete state machine \"{m.Machine.Name}\" and everything in it?")) m.Owner.Remove(m.Machine);
                return null;
            case StateNode s:
                if (s.Machine.States.Count == 1) { MessageBox.Show(this, "A state machine needs at least one state.", Title); return node; }
                if (Ask($"Delete state \"{s.State.Name}\", its calls, and the edges into it?")) s.Machine.RemoveState(s.State);
                return new MachineNode(s.Machine, []);
            case CallNode c:
                c.List.Remove(c.Call);
                return null;
            case EdgeNode e:
                e.State.Edges.Remove(e.Edge);
                return new StateNode(e.Machine, e.State);
            case VariableNode v:
                if (script.VariableUsed(v.Index)) { MessageBox.Show(this, $"{script.VariableName(v.Index)} is used by a call; change those first.", Title); return node; }
                script.RemoveVariable(v.Index);
                return null;
            case ArrayNode a:
                if (script.ArrayUsed(a.Array)) { MessageBox.Show(this, $"Array \"{a.Array.Name}\" is used by a call; change those first.", Title); return node; }
                script.Arrays.Remove(a.Array);
                return null;
        }
        return node;
    }

    bool Ask(string question) => MessageBox.Show(this, question, Title, MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes;

    /// <summary>What would stop the game reading the script; null if nothing.</summary>
    string? Problem()
    {
        if (script.Objects.Count == 0) return "The script has no state machine: the game won't load a handler without one.";
        foreach (LogicObject m in AllMachines())
        {
            if (m.States.Count == 0) return $"State machine \"{m.Name}\" has no states.";
            var names = new HashSet<string>(StringComparer.Ordinal);
            foreach (LogicState s in m.States)
            {
                if (s.Name.Text.Length == 0) return $"A state of \"{m.Name}\" has no name.";
                if (!names.Add(s.Name.Text)) return $"\"{m.Name}\" has two states called \"{s.Name}\".";
            }
            foreach (LogicState s in m.States)
                foreach (LogicEdge e in s.Edges)
                    if (!names.Contains(e.To.Text)) return $"An edge of \"{m.Name}\" / \"{s.Name}\" goes to \"{e.To}\", which isn't one of its states.";
        }
        return null;
    }

    // ---- The details panel ---------------------------------------------------------------------

    void ShowDetails()
    {
        details.Children.Clear();
        UpdateTools();
        switch (Selected)
        {
            case MachineNode m: MachineDetails(m.Machine); break;
            case StateNode s: StateDetails(s); break;
            case CallNode c: Section(c.When); CallEditor(c.Call); break;
            case EdgeNode e: EdgeDetails(e); break;
            case VariableNode v: VariableDetails(v.Index); break;
            case ArrayNode a: ArrayDetails(a.Array); break;
            case TimelineNode t:
                Section($"Timeline \"{t.Timeline.Name}\"");
                Hint("Timelines are shown here, not edited. Their events:");
                foreach (LogicEvent ev in t.Timeline.Events.Concat(t.Timeline.Actions))
                {
                    Hint($"{(ev.IsAction ? "action" : "event")} \"{ev.Name}\" at {ev.Start}..{ev.End}");
                    foreach (LogicCall c in ev.Calls) Hint("    " + text.Call(c));
                }
                break;
            default:
                Hint("Choose a state machine, a state, a call, an edge or a variable on the left to change it. " +
                     "The buttons above add new ones next to what is chosen.");
                break;
        }
    }

    void Section(string title) => details.Children.Add(new TextBlock { Text = title, FontWeight = FontWeights.Bold, FontSize = 14, Margin = new Thickness(0, 4, 0, 8) });

    void Hint(string hint) => details.Children.Add(new TextBlock
    {
        Text = hint, Foreground = Theme.Brush("DimTextBrush"), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 2, 0, 2),
    });

    void Row(string label, FrameworkElement control, string? tip = null, Panel? into = null)
    {
        var grid = new Grid { Margin = new Thickness(0, 2, 0, 2) };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(130) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        var name = new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis, ToolTip = tip ?? label };
        Grid.SetColumn(control, 1);
        grid.Children.Add(name);
        grid.Children.Add(control);
        (into ?? details).Children.Add(grid);
    }

    /// <summary>A text box that applies what is typed while it is valid (red border and a tip while not).</summary>
    TextBox TextEditor(string value, Func<string, string?> apply, bool many = false)
    {
        var box = new TextBox { Text = value, AcceptsReturn = many, TextWrapping = many ? TextWrapping.Wrap : TextWrapping.NoWrap, MinHeight = many ? 44 : 0 };
        box.TextChanged += (_, _) =>
        {
            string? problem = apply(box.Text);
            if (problem == null) { box.ClearValue(Control.BorderBrushProperty); box.ToolTip = null; Refresh(); }
            else { box.BorderBrush = Theme.Brush("ErrorBrush"); box.ToolTip = "Not applied: " + problem; }
        };
        return box;
    }

    static string? NameProblem(string name, int most) =>
        name.Length == 0 ? "needs a name" : name.Length > most ? $"at most {most} letters" : name.Any(c => c > 0xFF) ? "only plain letters" : null;

    void MachineDetails(LogicObject machine)
    {
        Section("State machine");
        Row("Name", TextEditor(machine.Name.Text, t => NameProblem(t, 31) ?? Do(() => machine.Name.Text = t)));
        var first = new ComboBox { ItemsSource = machine.States.Select(s => s.Name.Text).ToList(), SelectedIndex = machine.FirstState };
        first.SelectionChanged += (_, _) => { if (first.SelectedIndex >= 0) { machine.SetFirstState(first.SelectedIndex); Rebuild(Selected); } };
        Row("First state", first, "The state it is in when the handler starts.");
    }

    void StateDetails(StateNode node)
    {
        LogicState state = node.State;
        Section("State");
        var name = TextEditor(state.Name.Text, t =>
        {
            if (NameProblem(t, 15) is { } problem) return problem;
            if (node.Machine.States.Any(s => s != state && s.Name.Text == t)) return "another state has that name";
            node.Machine.RenameState(state, t);
            return null;
        });
        name.LostKeyboardFocus += (_, _) => Rebuild(Selected);  // the edges that name it
        Row("Name", name, "At most 15 letters. Edges name the states they go to, so they are renamed too.");
        var first = new CheckBox { IsChecked = node.Machine.States.IndexOf(state) == node.Machine.FirstState, Content = "The state machine starts here" };
        first.Checked += (_, _) => { node.Machine.SetFirstState(node.Machine.States.IndexOf(state)); Rebuild(Selected); };
        Row("First", first);
        Row("Comment", TextEditor(Latin1(state.Comment), t => Do(() => state.Comment = Encoding.Latin1.GetBytes(t)), many: true));
        Hint($"{state.OnEnter.Count} call(s) on enter, {state.EachFrame.Count} each frame, {state.OnExit.Count} on exit, {state.Edges.Count} edge(s) out. " +
             "Choose one on the left to change it, or add one with the buttons above.");
    }

    void EdgeDetails(EdgeNode node)
    {
        LogicEdge edge = node.Edge;
        Section($"Edge from \"{node.State.Name}\"");
        var to = new ComboBox { ItemsSource = node.Machine.States.Select(s => s.Name.Text).ToList(), SelectedItem = edge.To.Text };
        to.SelectionChanged += (_, _) => { if (to.SelectedItem is string s) { edge.To.Text = s; Refresh(); } };
        Row("Goes to", to);
        Row("Wait at least", TextEditor(N(edge.MinWait), t => Float(t, out float f) ?? Do(() => edge.MinWait = f)), "Seconds in the state before the edge can be taken.");
        Row("Wait at most", TextEditor(N(edge.MaxWait), t => Float(t, out float f) ?? Do(() => edge.MaxWait = f)), "The game picks a wait between the two; the most has to be at least the least.");
        Row("Comment", TextEditor(Latin1(edge.Comment), t => Do(() => edge.Comment = Encoding.Latin1.GetBytes(t)), many: true));
        details.Children.Add(new TextBlock { Text = "Taken when", FontWeight = FontWeights.Bold, Margin = new Thickness(0, 10, 0, 4) });
        details.Children.Add(ConditionEditor(edge.Condition, c => { edge.Condition = c; ShowDetails(); Refresh(); }));
    }

    /// <summary>A condition: a test (its function and arguments, and whether it is turned around), or two joined by and / or / xor.</summary>
    FrameworkElement ConditionEditor(LogicCondition condition, Action<LogicCondition> replace)
    {
        var panel = new StackPanel();
        var border = new Border
        {
            BorderBrush = Theme.Brush("StrongBorderBrush"), BorderThickness = new Thickness(1), Padding = new Thickness(6), Margin = new Thickness(0, 2, 0, 2),
            Child = panel,
        };
        if (condition.Call != null)
        {
            var not = new CheckBox { Content = "Not (when the test fails)", IsChecked = condition.Kind == LogicConditionKind.NotTest, Margin = new Thickness(0, 0, 0, 4) };
            not.Checked += (_, _) => { condition.Kind = LogicConditionKind.NotTest; Refresh(); };
            not.Unchecked += (_, _) => { condition.Kind = LogicConditionKind.Test; Refresh(); };
            panel.Children.Add(not);
            CallEditor(condition.Call, panel);
            var join = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 4, 0, 0) };
            foreach (LogicConditionKind kind in new[] { LogicConditionKind.And, LogicConditionKind.Or })
            {
                var button = new Button { Content = $"{kind.ToString().ToLowerInvariant()} another test", Padding = new Thickness(6, 1, 6, 1), Margin = new Thickness(0, 0, 4, 0) };
                button.Click += (_, _) => replace(new LogicCondition { Kind = kind, Left = condition, Right = NewTest() });
                join.Children.Add(button);
            }
            panel.Children.Add(join);
        }
        else
        {
            var op = new ComboBox { ItemsSource = new[] { "and", "or", "xor" }, SelectedIndex = (int)condition.Kind - 2, Width = 90, HorizontalAlignment = HorizontalAlignment.Left };
            op.SelectionChanged += (_, _) => { condition.Kind = (LogicConditionKind)(op.SelectedIndex + 2); Refresh(); };
            panel.Children.Add(new TextBlock { Text = "Both sides, joined by:", Foreground = Theme.Brush("DimTextBrush") });
            panel.Children.Add(op);
            panel.Children.Add(Side(condition.Left!, c => { condition.Left = c; ShowDetails(); Refresh(); }, () => replace(condition.Right!)));
            panel.Children.Add(Side(condition.Right!, c => { condition.Right = c; ShowDetails(); Refresh(); }, () => replace(condition.Left!)));
        }
        return border;
    }

    FrameworkElement Side(LogicCondition side, Action<LogicCondition> replace, Action remove)
    {
        var panel = new StackPanel { Margin = new Thickness(12, 4, 0, 0) };
        var drop = new Button { Content = "Take this side out", HorizontalAlignment = HorizontalAlignment.Right, Padding = new Thickness(6, 1, 6, 1) };
        drop.Click += (_, _) => remove();
        panel.Children.Add(drop);
        panel.Children.Add(ConditionEditor(side, replace));
        return panel;
    }

    LogicCondition NewTest()
    {
        LogicFunctions.Function? f = functions?.Find("ec_always_true");
        return LogicCondition.Test(f != null ? LogicCall.Of(f) : LogicCall.NewTest("ec_always_true"));
    }

    /// <summary>A call: which function, where its result goes (actions), and its arguments by the table's parameters.</summary>
    void CallEditor(LogicCall call, Panel? into = null)
    {
        Panel panel = into ?? details;
        LogicFunctions.Function? function = functions?.Find(call.Function);
        if (functions != null)
        {
            var pick = new Button
            {
                Content = (function?.Name ?? call.Function) + "   \u25BE", HorizontalContentAlignment = HorizontalAlignment.Left,
                Padding = new Thickness(6, 2, 6, 2), ToolTip = "Choose the function from the list, by searching or by category.",
            };
            pick.Click += (_, _) =>
            {
                if (FunctionPicker.Choose(this, functions, call.IsTest, function) is not { } chosen || chosen.Code == call.Function) return;
                call.Function = chosen.Code;
                call.Arguments.Clear();
                foreach (LogicFunctions.Parameter p in chosen.Parameters) call.Arguments.Add(LogicValue.DefaultFor(p.Type));
                ShowDetails();
                Refresh();
            };
            Row(call.IsTest ? "Test" : "Function", pick, function?.Comments, panel);
            if (function?.Comments is { Length: > 0 } comments)
                panel.Children.Add(new TextBlock { Text = comments, Foreground = Theme.Brush("DimTextBrush"), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(130, 0, 0, 4) });
        }
        else
        {
            Row(call.IsTest ? "Test" : "Function", TextEditor(call.Function, t => t.Length == 0 ? "needs a name" : Do(() => call.Function = t)), null, panel);
        }

        if (!call.IsTest)
        {
            var result = new ComboBox
            {
                ItemsSource = new[] { "(none)" }.Concat(Enumerable.Range(0, script.Variables.Count).Select(script.VariableName)).ToList(),
                SelectedIndex = call.Result >= 0 && call.Result < script.Variables.Count ? call.Result + 1 : 0,
            };
            result.SelectionChanged += (_, _) => { call.Result = result.SelectedIndex - 1; Refresh(); };
            Row("Result into", result, "The variable the call sets (variables - set boolean / set number / ... set the one chosen here).", panel);
        }

        for (int i = 0; i < call.Arguments.Count; i++)
        {
            LogicFunctions.Parameter? p = function != null && i < function.Parameters.Count ? function.Parameters[i] : null;
            int at = i;
            Row(p?.Name is { Length: > 0 } label ? label : $"Argument {i + 1}",
                ValueEditor(call.Arguments[i], p?.Type, v => call.Arguments[at] = v, references: true,
                            classes: p == null ? null : LogicHints.ClassesFor(call.Function, p.Name),
                            texts: () => TextChoices(call.Function, at, p?.Name ?? "")),
                p == null ? null : $"{p.Type}: {p.Description}", panel);
        }
        if (function != null && call.Arguments.Count != function.Parameters.Count)
            panel.Children.Add(new TextBlock
            {
                Text = $"The table gives this function {function.Parameters.Count} parameter(s); the call has {call.Arguments.Count}. Pick the function again to start its arguments afresh.",
                Foreground = Theme.Brush("ErrorBrush"), TextWrapping = TextWrapping.Wrap,
            });
    }

    void VariableDetails(int index)
    {
        LogicValue variable = script.Variables[index];
        Section("Variable");
        Row("Name", TextEditor(variable.Name, t => NameProblem(t, 31) ?? Do(() => script.Variables[index].Name = t)));
        var type = new ComboBox { ItemsSource = new[] { "Boolean", "Number", "Object", "Vector", "String" }, SelectedItem = TypeName(variable) };
        type.SelectionChanged += (_, _) =>
        {
            if (type.SelectedItem is not string chosen || chosen == TypeName(variable)) return;
            LogicValue made = LogicValue.DefaultFor(chosen == "Object" ? "ObjHandle" : chosen);
            made.NameBytes = variable.NameBytes;
            script.Variables[index] = made;
            ShowDetails();
            Refresh();
        };
        Row("Type", type, "What it holds. The calls that read or set it should be of the same type.");
        Row("Starts as", ValueEditor(variable, TypeName(variable) == "Object" ? "ObjHandle" : TypeName(variable),
                                     v => { v.NameBytes = script.Variables[index].NameBytes; script.Variables[index] = v; }, references: false));
        Hint(script.VariableUsed(index) ? "Calls in this script use it." : "No call uses it yet.");
    }

    static string TypeName(LogicValue v) => v.Type switch
    {
        LogicValueType.Number => "Number",
        LogicValueType.Vector => "Vector",
        LogicValueType.String => "String",
        LogicValueType.Object => "Object",
        _ => "Boolean",
    };

    void ArrayDetails(LogicArray array)
    {
        Section("Array");
        Row("Name", TextEditor(array.Name.Text, t =>
        {
            if (NameProblem(t, 15) is { } problem) return problem;
            if (script.Arrays.Any(a => a != array && a.Name.Text.Equals(t, StringComparison.OrdinalIgnoreCase))) return "another array has that name";
            script.RenameArray(array, t);
            return null;
        }), "At most 15 letters. Calls name arrays by name, so they follow.");
        Row("Holds at most", TextEditor(array.Most.ToString(CultureInfo.InvariantCulture), t =>
            int.TryParse(t, NumberStyles.Integer, CultureInfo.InvariantCulture, out int n) && n >= 1 && n <= 256 ? Do(() => array.Most = n) : "1 to 256"));
        bool objects = array.Elements.All(e => e.Type == LogicValueType.Object);
        if (objects)
            Row("Objects", TextEditor(string.Join(" ", array.Elements.Select(e => e.Handle.ToString("X8"))), t =>
            {
                var handles = new List<uint>();
                foreach (string part in t.Split([' ', ',', '\n', '\r', '\t'], StringSplitOptions.RemoveEmptyEntries))
                {
                    if (!uint.TryParse(part, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint h)) return $"'{part}' isn't a handle in hex";
                    handles.Add(h);
                }
                if (handles.Count > array.Most) return $"more than {array.Most}";
                // Keep each handle's value as read (its number) where the handle is unchanged.
                var old = array.Elements.ToList();
                array.Elements.Clear();
                foreach (uint h in handles) array.Elements.Add(old.FirstOrDefault(e => e.Handle == h) ?? LogicValue.OfObject(h));
                return null;
            }, many: true), "The objects' handles in hex (as the outliner shows them), separated by spaces.");
        else
            Hint("Its elements: " + string.Join(", ", array.Elements.Select(text.Value)) + " (shown, not edited).");
    }

    // ---- Values --------------------------------------------------------------------------------

    /// <summary>
    /// A value's editor: what kind of value it is (by the parameter's type: an object, the player, a variable, ...)
    /// and the value itself. <paramref name="set"/> puts a new value in its place when the kind changes.
    /// </summary>
    FrameworkElement ValueEditor(LogicValue value, string? parameterType, Action<LogicValue> set, bool references, IReadOnlyList<string>? classes = null,
                                 Func<IReadOnlyList<string>>? texts = null)
    {
        string type = (parameterType ?? "").Trim().ToLowerInvariant() switch
        {
            "number" => "number",
            "boolean" => "boolean",
            "string" => "string",
            "vector" => "vector",
            "objhandle" => "object",
            _ => value.Type switch
            {
                LogicValueType.Number => "number",
                LogicValueType.Boolean => "boolean",
                LogicValueType.String => "string",
                LogicValueType.Vector => "vector",
                _ => "object",
            },
        };
        var kinds = type switch
        {
            "number" => new List<string> { "number", "whole number" },
            "boolean" => new List<string> { "true or false" },
            "string" => new List<string> { "text" },
            "vector" => new List<string> { "x, y, z", "position of an object" },
            _ => new List<string> { "object", "the player", "self (the handler's object)" },
        };
        if (references)
        {
            kinds.Add("variable");
            if (type == "object") kinds.Add("array");
        }
        string current = KindOf(value);
        if (!kinds.Contains(current)) kinds.Add(current);

        var panel = new DockPanel();
        var kind = new ComboBox { ItemsSource = kinds, SelectedItem = current, Width = 150, Margin = new Thickness(0, 0, 4, 0) };
        DockPanel.SetDock(kind, Dock.Left);
        panel.Children.Add(kind);
        var body = new ContentControl();
        panel.Children.Add(body);
        LogicValue live = value;

        void Body()
        {
            body.Content = BodyFor(live, KindOf(live), v => { live = v; set(v); }, classes ?? [], texts);
        }
        kind.SelectionChanged += (_, _) =>
        {
            if (kind.SelectedItem is not string chosen || chosen == KindOf(live)) return;
            LogicValue made = chosen switch
            {
                "number" => LogicValue.OfNumber(0),
                "whole number" => LogicValue.OfWhole(0),
                "true or false" => LogicValue.OfBoolean(false),
                "text" => LogicValue.OfText(""),
                "x, y, z" => new LogicValue { Type = LogicValueType.Vector, Form = 0 },
                "position of an object" => new LogicValue { Type = LogicValueType.Vector, Form = 1 },
                "object" => LogicValue.OfObject(0xFFFFFFFF),
                "the player" => LogicValue.OfPlayer(),
                "self (the handler's object)" => LogicValue.OfSelf(),
                "variable" => LogicValue.OfVariable(Math.Max(0, Enumerable.Range(0, script.Variables.Count).FirstOrDefault(i => Fits(script.Variables[i], type), 0))),
                "array" => script.Arrays.Count > 0 ? LogicValue.OfArray(script.Arrays[0].Name.Text) : LogicValue.OfVariable(0),
                _ => live,
            };
            live = made;
            set(made);
            Body();
            Refresh();
        };
        Body();
        return panel;
    }

    static bool Fits(LogicValue variable, string type) => type switch
    {
        "number" => variable.Type == LogicValueType.Number,
        "boolean" => variable.Type == LogicValueType.Boolean,
        "string" => variable.Type == LogicValueType.String,
        "vector" => variable.Type == LogicValueType.Vector,
        _ => variable.Type == LogicValueType.Object,
    };

    static string KindOf(LogicValue v) => v.Type switch
    {
        LogicValueType.Reference => v.Reference >= 0 ? "variable" : v.Reference == -1 ? "array" : "as read",
        LogicValueType.Number => v.Form == 0 ? "whole number" : v.Form == 1 ? "number" : "as read",
        LogicValueType.Boolean => "true or false",
        LogicValueType.String => "text",
        LogicValueType.Vector => v.Form == 0 ? "x, y, z" : v.Form == 1 ? "position of an object" : "as read",
        _ => v.Player ? "the player" : v.Owner ? "self (the handler's object)" : "object",  // (an object not chosen yet: none)
    };

    FrameworkElement BodyFor(LogicValue v, string kind, Action<LogicValue> set, IReadOnlyList<string> classes, Func<IReadOnlyList<string>>? texts)
    {
        switch (kind)
        {
            case "number":
                return TextEditor(N(v.Number), t => Float(t, out float f) ?? Do(() => v.Number = f));
            case "whole number":
                return TextEditor(v.Whole.ToString(CultureInfo.InvariantCulture), t =>
                    int.TryParse(t.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out int n) ? Do(() => v.Whole = n) : "must be a whole number");
            case "true or false":
                var box = new CheckBox { IsChecked = v.Boolean, VerticalAlignment = VerticalAlignment.Center };
                box.Checked += (_, _) => { v.Boolean = true; Refresh(); };
                box.Unchecked += (_, _) => { v.Boolean = false; Refresh(); };
                return box;
            case "text":
                if (texts?.Invoke() is { Count: > 0 } choices)
                {
                    // A name from the table the parameter names (an explosion, an objective, a persona...) or the game's scripts, or typed.
                    var combo = new ComboBox { IsEditable = true, IsTextSearchEnabled = true, ItemsSource = choices, Text = v.Text.Text, ToolTip = $"{choices.Count} names to choose from, or type one" };
                    void TakeText()
                    {
                        string typed = combo.Text ?? "";
                        if (typed.Length > 31) { combo.BorderBrush = Theme.Brush("ErrorBrush"); combo.ToolTip = "at most 31 letters"; return; }
                        combo.ClearValue(Control.BorderBrushProperty);
                        if (typed != v.Text.Text) { v.Text.Text = typed; Refresh(); }
                    }
                    combo.DropDownClosed += (_, _) => TakeText();
                    combo.LostKeyboardFocus += (_, _) => TakeText();
                    combo.SelectionChanged += (_, _) => { if (combo.SelectedItem is string s) { combo.Text = s; TakeText(); } };
                    return combo;
                }
                return TextEditor(v.Text.Text, t => t.Length > 31 ? "at most 31 letters" : Do(() => v.Text.Text = t));
            case "x, y, z":
                return TextEditor($"{N(v.X)}, {N(v.Y)}, {N(v.Z)}", t =>
                {
                    string[] parts = t.Split([',', ' ', ';'], StringSplitOptions.RemoveEmptyEntries);
                    if (parts.Length != 3) return "needs 3 numbers";
                    var f = new float[3];
                    for (int i = 0; i < 3; i++)
                        if (!float.TryParse(parts[i], NumberStyles.Float, CultureInfo.InvariantCulture, out f[i])) return $"'{parts[i]}' isn't a number";
                    v.X = f[0]; v.Y = f[1]; v.Z = f[2];
                    return null;
                });
            case "object":
            case "position of an object":
                return ObjectPicker(v.Handle, classes, h => { if (h != v.Handle) { v.Handle = h; v.HandleNumber = -1; } Refresh(); });
            case "variable":
                var names = Enumerable.Range(0, script.Variables.Count).Select(i => $"{script.VariableName(i)} ({TypeName(script.Variables[i]).ToLowerInvariant()})").ToList();
                var pick = new ComboBox { ItemsSource = names, SelectedIndex = v.Reference < names.Count ? v.Reference : -1 };
                pick.SelectionChanged += (_, _) => { if (pick.SelectedIndex >= 0) { v.Reference = pick.SelectedIndex; Refresh(); } };
                return pick;
            case "array":
                var arrays = script.Arrays.Select(a => a.Name.Text).ToList();
                int at = script.Arrays.FindIndex(a => (int)NameHashes.PropertyHash(a.Name.Text) == v.ReferenceExtra);
                var choose = new ComboBox { ItemsSource = arrays, SelectedIndex = at };
                choose.SelectionChanged += (_, _) =>
                {
                    if (choose.SelectedItem is string name) { v.Reference = -1; v.ReferenceExtra = (int)NameHashes.PropertyHash(name); Refresh(); }
                };
                return choose;
            case "as read":
                return new TextBlock { Text = text.Value(v) + "  (kept as it is)", Foreground = Theme.Brush("DimTextBrush"), VerticalAlignment = VerticalAlignment.Center };
            default:  // the player, self, none: nothing more to say
                return new TextBlock();
        }
    }

    /// <summary>
    /// An object of the level, picked from a list (the objects of the classes that fit first, by the game's own
    /// scripts' use, see <see cref="LogicHints"/>), typed as a handle in hex, or the level's picked object.
    /// </summary>
    internal FrameworkElement ObjectPicker(uint handle, IReadOnlyList<string> classes, Action<uint> set)
    {
        var rank = classes.Select((c, i) => (c, i)).ToDictionary(x => x.c, x => x.i, StringComparer.OrdinalIgnoreCase);
        List<ScriptObject> fitting = context.Objects.Where(o => rank.ContainsKey(o.Class))
            .OrderBy(o => rank[o.Class]).ThenBy(o => o.Label, StringComparer.OrdinalIgnoreCase).ToList();
        List<ScriptObject> all = context.Objects.OrderBy(o => o.Class).ThenBy(o => o.Label, StringComparer.OrdinalIgnoreCase).ToList();

        var box = new ComboBox
        {
            IsEditable = true, IsTextSearchEnabled = true, StaysOpenOnEdit = true,
            ItemsSource = fitting.Count > 0 ? fitting : all,
            ToolTip = "Pick an object (those that fit first), or type its handle in hex.",
        };
        TextSearch.SetTextPath(box, nameof(ScriptObject.Text));
        void Show(uint h)
        {
            ScriptObject? known = objectsByHandle.GetValueOrDefault(h);
            if (known != null && !((IEnumerable<ScriptObject>)box.ItemsSource).Contains(known)) box.ItemsSource = all;
            box.SelectedItem = known;
            box.Text = known?.Text ?? (h == 0xFFFFFFFF ? "" : h.ToString("X8"));
        }
        Show(handle);
        void Take()
        {
            if (box.SelectedItem is ScriptObject chosen) { set(chosen.Handle); return; }
            string typed = box.Text.Trim();
            int dot = typed.LastIndexOf('·');
            if (dot >= 0) typed = typed[(dot + 1)..].Trim();
            if (typed.Length == 0) { set(0xFFFFFFFF); box.ClearValue(Control.BorderBrushProperty); }
            else if (uint.TryParse(typed, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint h)) { set(h); box.ClearValue(Control.BorderBrushProperty); }
            else if (typed.Length > 0) box.BorderBrush = Theme.Brush("ErrorBrush");
        }
        box.DropDownClosed += (_, _) => Take();
        box.LostKeyboardFocus += (_, _) => Take();

        var everything = new CheckBox
        {
            Content = "all", VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(4, 0, 0, 0), IsChecked = fitting.Count == 0,
            IsEnabled = fitting.Count > 0, ToolTip = "List every object of the level, not only those that fit.",
        };
        everything.Checked += (_, _) => box.ItemsSource = all;
        everything.Unchecked += (_, _) => box.ItemsSource = fitting;
        var picked = new Button { Content = "\u25CE", Padding = new Thickness(6, 0, 6, 0), Margin = new Thickness(4, 0, 0, 0), ToolTip = "Use the object picked in the level." };
        picked.Click += (_, _) =>
        {
            if (context.Picked() is not uint h) { MessageBox.Show(this, "Pick an object in the level (its view or outliner) first.", Title); return; }
            set(h);
            Show(h);
        };
        var self = new Button { Content = "self", Padding = new Thickness(6, 0, 6, 0), Margin = new Thickness(4, 0, 0, 0), ToolTip = "The object this handler is attached to." };
        self.Click += (_, _) => { set(context.Self); Show(context.Self); };

        var row = new DockPanel();
        DockPanel.SetDock(self, Dock.Right);
        DockPanel.SetDock(picked, Dock.Right);
        DockPanel.SetDock(everything, Dock.Right);
        row.Children.Add(self);
        row.Children.Add(picked);
        row.Children.Add(everything);
        row.Children.Add(box);
        return row;
    }

    /// <summary>Texts for a parameter: the script's own arrays for an array's name, else the context's (tables, the game's scripts).</summary>
    IReadOnlyList<string> TextChoices(string function, int parameter, string name)
    {
        if (name.Contains("array", StringComparison.OrdinalIgnoreCase))
            return script.Arrays.Select(a => a.Name.Text).ToList();
        return context.TextChoices?.Invoke(function, parameter, name) ?? [];
    }

    static string? Float(string t, out float f) =>
        float.TryParse(t.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out f) ? null : "must be a number";

    static string? Do(Action act)
    {
        act();
        return null;
    }

    static string Latin1(byte[] bytes) => Encoding.Latin1.GetString(bytes).TrimEnd('\0');
}
