using System.Globalization;
using System.Text;

namespace RfaFormats.Zones;

/// <summary>
/// A logic handler's script: the "logic_handler_buffer" of an object_logic_handler, as Volition's tools
/// compiled it. The game reads it in object_logic_handler's serialize (RedFactionArmageddon_DX11.exe
/// 0x7ED230 -> 0x7EDC90, the memory form every one of the game's zones and mission layers has, version 11).
/// Reading and writing it back gives the same bytes for all 1241 of the game's handlers.
/// <code>
/// handler: u32 version (8..11), u32 owner handle, u32 n + object[n], u32 n + variable[n],
///          (version &lt; 11: u32), u32 n + array[n], zeros to a multiple of 4
/// object:  u32 type (0 statemachine, 1 timeline), u32 (0; 1 on some timelines), header (16), then its own
/// header:  4 bytes colour, 8 flag bytes, u32 (0)
/// statemachine: char name[32], i32 first state, i32 n + state[n], then for each state: i32 n + edge[n]
/// state:   i32 x, i32 y (graph), char name[16], u8 (1 on the first state), i32 n + call[n] on enter,
///          i32 n + call[n] each frame, i32 n + call[n] on exit, i32 length + comment, i32 n + object[n]
/// edge:    char from[16], char to[16], u32, u32, f32 min wait, f32 max wait (seconds), u8, u8,
///          i32 length + comment, condition
/// condition: u32 kind: 0 test, 1 not test (cstring "ec_..." name, i32 n + variable[n]); 2 and, 3 or, 4 xor (condition, condition)
/// call:    cstring "sf_..." name, i32 variable the result goes to (-1 none), u32, i32 n + variable[n]
/// variable: u16 length + name (empty in calls), u32 type, then its value (see <see cref="LogicValue"/>)
/// timeline: char name[32], i32, i32, 4 bytes, u32 n + track[n], u32 n + event[n], u32 n + action[n], u32
/// array:   i32 most elements (1..256), char name[16], i32, i32 n + variable[n], two lists of
///          (u32, i32 n + variable[n])
/// </code>
/// The fixed-length names keep whatever bytes follow their end in the game's files, so they are kept
/// as read (<see cref="FixedText"/>). Fields whose meaning isn't known yet are kept too.
/// </summary>
public sealed class LogicScript
{
    public uint Version { get; set; } = 11;
    public uint Owner { get; set; }
    public List<LogicObject> Objects { get; } = [];
    public List<LogicValue> Variables { get; } = [];
    public uint OldVersionWord { get; set; }
    public List<LogicArray> Arrays { get; } = [];

    /// <summary>The script in a buffer; null if it isn't one this reader understands.</summary>
    public static LogicScript? Read(byte[] data)
    {
        try
        {
            var r = new LogicReader(data);
            var script = new LogicScript { Version = r.U32() };
            if (script.Version < 8 || script.Version > 11) return null;
            r.Version = script.Version;
            script.Owner = r.U32();
            for (uint i = 0, n = r.U32(); i < n; i++) script.Objects.Add(LogicObject.Read(r));
            for (uint i = 0, n = r.U32(); i < n; i++) script.Variables.Add(LogicValue.Read(r));
            if (script.Version < 11) script.OldVersionWord = r.U32();
            for (uint i = 0, n = r.U32(); i < n; i++) script.Arrays.Add(LogicArray.Read(r));
            for (int i = r.Position; i < data.Length; i++) if (data[i] != 0) return null;
            return data.Length - r.Position < 4 ? script : null;
        }
        catch (Exception e) when (e is ArgumentException or IndexOutOfRangeException or InvalidDataException or ArgumentOutOfRangeException)
        {
            return null;
        }
    }

    public byte[] Write()
    {
        var w = new LogicWriter();
        w.U32(Version);
        w.U32(Owner);
        w.U32((uint)Objects.Count);
        foreach (LogicObject o in Objects) o.Write(w);
        w.U32((uint)Variables.Count);
        foreach (LogicValue v in Variables) v.Write(w);
        if (Version < 11) w.U32(OldVersionWord);
        w.U32((uint)Arrays.Count);
        foreach (LogicArray a in Arrays) a.Write(w);
        while (w.Length % 4 != 0) w.U8(0);
        return w.ToArray();
    }

    /// <summary>Every call and condition test in the script, nested ones too.</summary>
    public IEnumerable<LogicCall> Calls() => Objects.SelectMany(o => o.Calls());

    /// <summary>Every value that can refer to a variable or an array: calls' arguments and arrays' contents.</summary>
    public IEnumerable<LogicValue> ReferringValues() =>
        Calls().SelectMany(c => c.Arguments)
            .Concat(Arrays.SelectMany(a => a.Elements.Concat(a.ListA.SelectMany(l => l.Values)).Concat(a.ListB.SelectMany(l => l.Values))));

    /// <summary>Whether a call reads or sets a variable.</summary>
    public bool VariableUsed(int index) =>
        Calls().Any(c => c.Result == index) || ReferringValues().Any(v => v.Type == LogicValueType.Reference && v.Reference == index);

    /// <summary>Whether a call names an array.</summary>
    public bool ArrayUsed(LogicArray array)
    {
        int hash = (int)NameHashes.PropertyHash(array.Name.Text);
        return ReferringValues().Any(v => v.Type == LogicValueType.Reference && v.Reference == -1 && v.ReferenceExtra == hash);
    }

    /// <summary>Takes a variable out and renumbers the references to those after it (it should be unused, see <see cref="VariableUsed"/>).</summary>
    public void RemoveVariable(int index)
    {
        Variables.RemoveAt(index);
        foreach (LogicCall c in Calls()) if (c.Result > index) c.Result--;
        foreach (LogicValue v in ReferringValues())
            if (v.Type == LogicValueType.Reference && v.Reference > index) v.Reference--;
    }

    /// <summary>Renames an array, and the references to it (they name it by a hash of its name).</summary>
    public void RenameArray(LogicArray array, string name)
    {
        int old = (int)NameHashes.PropertyHash(array.Name.Text);
        array.Name.Text = name;
        int now = (int)NameHashes.PropertyHash(name);
        foreach (LogicValue v in ReferringValues())
            if (v.Type == LogicValueType.Reference && v.Reference == -1 && v.ReferenceExtra == old) v.ReferenceExtra = now;
    }

    /// <summary>The script written out for people to read.</summary>
    public string ToText(LogicFunctions? functions = null, Func<uint, string?>? objectName = null) =>
        new LogicText(this, functions, objectName).Write();

    /// <summary>A variable's name for people: its own, or var and its number.</summary>
    public string VariableName(int index) =>
        index >= 0 && index < Variables.Count && Variables[index].Name is { Length: > 0 } name ? name : $"var{index}";
}

/// <summary>A name in a fixed-length field: its text, and the field's bytes as read (kept while the text is unchanged).</summary>
public sealed class FixedText
{
    string text;
    byte[]? raw;

    public FixedText(string text) { this.text = text; }
    FixedText(string text, byte[] raw) { this.text = text; this.raw = raw; }

    public string Text
    {
        get => text;
        set { if (value != text) { text = value; raw = null; } }
    }

    internal static FixedText Read(LogicReader r, int length)
    {
        byte[] raw = r.Bytes(length);
        int end = Array.IndexOf(raw, (byte)0);
        return new FixedText(Encoding.Latin1.GetString(raw, 0, end < 0 ? raw.Length : end), raw);
    }

    internal void Write(LogicWriter w, int length)
    {
        if (raw != null && raw.Length == length) { w.Bytes(raw); return; }
        byte[] bytes = Encoding.Latin1.GetBytes(text);
        if (bytes.Length >= length) bytes = bytes[..(length - 1)];
        w.Bytes(bytes);
        w.Bytes(new byte[length - bytes.Length]);
    }

    public override string ToString() => text;
}

public enum LogicObjectKind { StateMachine = 0, Timeline = 1 }

/// <summary>A state machine or a timeline.</summary>
public sealed class LogicObject
{
    public LogicObjectKind Kind { get; set; }
    public uint Unknown { get; set; }
    /// <summary>The 16-byte header every logic object starts with: colour (4), flags (8), a word (always 0).</summary>
    public byte[] Header { get; set; } = [0xFF, 0x00, 0xFF, 0xFF, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0];
    public FixedText Name { get; set; } = new("new Statemachine");

    // A state machine's
    public int FirstState { get; set; }
    public List<LogicState> States { get; } = [];

    // A timeline's
    public int TimelineA { get; set; }
    public int TimelineB { get; set; }
    public byte[] TimelineC { get; set; } = new byte[4];
    public List<LogicTrack> Tracks { get; } = [];
    public List<LogicEvent> Events { get; } = [];
    public List<LogicEvent> Actions { get; } = [];
    public uint TimelineTail { get; set; }

    internal static LogicObject Read(LogicReader r)
    {
        var o = new LogicObject { Kind = (LogicObjectKind)r.U32(), Unknown = r.U32(), Header = r.Bytes(16) };
        o.Name = FixedText.Read(r, 32);
        switch (o.Kind)
        {
            case LogicObjectKind.StateMachine:
                o.FirstState = r.I32();
                int states = r.I32();
                for (int i = 0; i < states; i++) o.States.Add(LogicState.Read(r));
                foreach (LogicState s in o.States)
                    for (int i = 0, n = r.I32(); i < n; i++) s.Edges.Add(LogicEdge.Read(r));
                break;
            case LogicObjectKind.Timeline:
                o.TimelineA = r.I32();
                o.TimelineB = r.I32();
                o.TimelineC = r.Bytes(4);
                for (uint i = 0, n = r.U32(); i < n; i++) o.Tracks.Add(LogicTrack.Read(r));
                for (uint i = 0, n = r.U32(); i < n; i++) o.Events.Add(LogicEvent.Read(r, false));
                for (uint i = 0, n = r.U32(); i < n; i++) o.Actions.Add(LogicEvent.Read(r, true));
                o.TimelineTail = r.U32();
                break;
            default:
                throw new InvalidDataException($"logic object type {(int)o.Kind}");
        }
        return o;
    }

    internal void Write(LogicWriter w)
    {
        w.U32((uint)Kind);
        w.U32(Unknown);
        w.Bytes(Header);
        Name.Write(w, 32);
        if (Kind == LogicObjectKind.StateMachine)
        {
            w.I32(FirstState);
            w.I32(States.Count);
            foreach (LogicState s in States) s.Write(w);
            foreach (LogicState s in States)
            {
                w.I32(s.Edges.Count);
                foreach (LogicEdge e in s.Edges) e.Write(w);
            }
        }
        else
        {
            w.I32(TimelineA);
            w.I32(TimelineB);
            w.Bytes(TimelineC);
            w.U32((uint)Tracks.Count);
            foreach (LogicTrack t in Tracks) t.Write(w);
            w.U32((uint)Events.Count);
            foreach (LogicEvent e in Events) e.Write(w);
            w.U32((uint)Actions.Count);
            foreach (LogicEvent e in Actions) e.Write(w);
            w.U32(TimelineTail);
        }
    }

    /// <summary>A copy of it, made by writing it and reading it back (nothing shared with this one).</summary>
    public LogicObject Clone()
    {
        var w = new LogicWriter();
        Write(w);
        return Read(new LogicReader(w.ToArray()));
    }

    /// <summary>A new state machine with one state, "Start".</summary>
    public static LogicObject NewStateMachine(string name)
    {
        var machine = new LogicObject { Kind = LogicObjectKind.StateMachine, Name = new FixedText(name) };
        machine.States.Add(new LogicState { Name = new FixedText("Start"), Flag = 1, X = 70, Y = 80 });
        return machine;
    }

    /// <summary>Which state is first: the index, and each state's flag (the game's files set it on the first one only).</summary>
    public void SetFirstState(int index)
    {
        FirstState = index;
        for (int i = 0; i < States.Count; i++) States[i].Flag = (byte)(i == index ? 1 : 0);
    }

    /// <summary>Renames a state, and every edge that names it (the game joins edges to states by name).</summary>
    public void RenameState(LogicState state, string name)
    {
        string old = state.Name.Text;
        state.Name.Text = name;
        foreach (LogicState s in States)
            foreach (LogicEdge e in s.Edges)
            {
                if (e.From.Text == old) e.From.Text = name;
                if (e.To.Text == old) e.To.Text = name;
            }
    }

    /// <summary>Takes a state out, and the edges into it; keeps the first state where it was if it can.</summary>
    public void RemoveState(LogicState state)
    {
        int index = States.IndexOf(state);
        if (index < 0) return;
        States.RemoveAt(index);
        foreach (LogicState s in States) s.Edges.RemoveAll(e => e.To.Text == state.Name.Text);
        int first = FirstState == index ? 0 : FirstState > index ? FirstState - 1 : FirstState;
        SetFirstState(States.Count == 0 ? 0 : Math.Min(first, States.Count - 1));
    }

    /// <summary>A new state, placed to the right of the others on Volition's graph.</summary>
    public LogicState AddState(string name)
    {
        var state = new LogicState
        {
            Name = new FixedText(name),
            X = States.Count == 0 ? 70 : States.Max(s => s.X) + 160,
            Y = States.Count == 0 ? 80 : States[^1].Y,
        };
        States.Add(state);
        if (States.Count == 1) SetFirstState(0);
        return state;
    }

    /// <summary>A new edge from one state to another, taken when its condition holds (always, to start with).</summary>
    public static LogicEdge NewEdge(LogicState from, LogicState to) =>
        new() { From = new FixedText(from.Name.Text), To = new FixedText(to.Name.Text) };

    public IEnumerable<LogicCall> Calls()
    {
        foreach (LogicState s in States)
        {
            foreach (LogicCall c in s.OnEnter.Concat(s.EachFrame).Concat(s.OnExit)) yield return c;
            foreach (LogicObject inner in s.Inner) foreach (LogicCall c in inner.Calls()) yield return c;
            foreach (LogicEdge e in s.Edges) foreach (LogicCall c in e.Condition.Tests()) yield return c;
        }
        foreach (LogicTrack t in Tracks) foreach (LogicCall c in t.Condition.Tests()) yield return c;
        foreach (LogicEvent e in Events.Concat(Actions))
        {
            foreach (LogicCall c in e.Calls) yield return c;
            foreach (LogicObject inner in e.Inner) foreach (LogicCall c in inner.Calls()) yield return c;
        }
    }
}

public sealed class LogicState
{
    public int X { get; set; }
    public int Y { get; set; }
    public FixedText Name { get; set; } = new("State");
    public byte Flag { get; set; }
    public List<LogicCall> OnEnter { get; } = [];
    public List<LogicCall> EachFrame { get; } = [];
    public List<LogicCall> OnExit { get; } = [];
    public byte[] Comment { get; set; } = [];
    /// <summary>State machines and timelines that run inside this state.</summary>
    public List<LogicObject> Inner { get; } = [];
    public List<LogicEdge> Edges { get; } = [];

    internal static LogicState Read(LogicReader r)
    {
        var s = new LogicState { X = r.I32(), Y = r.I32(), Name = FixedText.Read(r, 16), Flag = r.U8() };
        foreach (List<LogicCall> list in new[] { s.OnEnter, s.EachFrame, s.OnExit })
            for (int i = 0, n = r.I32(); i < n; i++) list.Add(LogicCall.Read(r, false));
        s.Comment = r.Bytes(r.I32());
        for (int i = 0, n = r.I32(); i < n; i++) s.Inner.Add(LogicObject.Read(r));
        return s;
    }

    internal void Write(LogicWriter w)
    {
        w.I32(X);
        w.I32(Y);
        Name.Write(w, 16);
        w.U8(Flag);
        foreach (List<LogicCall> list in new[] { OnEnter, EachFrame, OnExit })
        {
            w.I32(list.Count);
            foreach (LogicCall c in list) c.Write(w);
        }
        w.I32(Comment.Length);
        w.Bytes(Comment);
        w.I32(Inner.Count);
        foreach (LogicObject o in Inner) o.Write(w);
    }
}

/// <summary>A way out of a state: to another state, once its condition holds (after the wait).</summary>
public sealed class LogicEdge
{
    public FixedText From { get; set; } = new("");
    public FixedText To { get; set; } = new("");
    public uint Unknown0 { get; set; } = 1;
    public uint Unknown1 { get; set; }
    public float MinWait { get; set; }
    public float MaxWait { get; set; }
    public byte Byte0 { get; set; }
    public byte Byte1 { get; set; }
    public byte[] Comment { get; set; } = [];
    public LogicCondition Condition { get; set; } = LogicCondition.Test(LogicCall.NewTest("ec_always_true"));

    internal static LogicEdge Read(LogicReader r)
    {
        var e = new LogicEdge
        {
            From = FixedText.Read(r, 16), To = FixedText.Read(r, 16), Unknown0 = r.U32(), Unknown1 = r.U32(),
            MinWait = r.F32(), MaxWait = r.F32(), Byte0 = r.U8(), Byte1 = r.U8(),
        };
        e.Comment = r.Bytes(r.I32());
        e.Condition = LogicCondition.Read(r);
        return e;
    }

    internal void Write(LogicWriter w)
    {
        From.Write(w, 16);
        To.Write(w, 16);
        w.U32(Unknown0);
        w.U32(Unknown1);
        w.F32(MinWait);
        w.F32(MaxWait);
        w.U8(Byte0);
        w.U8(Byte1);
        w.I32(Comment.Length);
        w.Bytes(Comment);
        Condition.Write(w);
    }
}

public enum LogicConditionKind { Test = 0, NotTest = 1, And = 2, Or = 3, Xor = 4 }

/// <summary>A test ("ec_..."), possibly negated, or two conditions joined by and / or / xor.</summary>
public sealed class LogicCondition
{
    public LogicConditionKind Kind { get; set; }
    public LogicCall? Call { get; set; }
    public LogicCondition? Left { get; set; }
    public LogicCondition? Right { get; set; }

    public static LogicCondition Test(LogicCall call, bool not = false) => new() { Kind = not ? LogicConditionKind.NotTest : LogicConditionKind.Test, Call = call };

    internal static LogicCondition Read(LogicReader r)
    {
        var c = new LogicCondition { Kind = (LogicConditionKind)r.U32() };
        switch (c.Kind)
        {
            case LogicConditionKind.Test:
            case LogicConditionKind.NotTest:
                c.Call = LogicCall.Read(r, true);
                break;
            case LogicConditionKind.And:
            case LogicConditionKind.Or:
            case LogicConditionKind.Xor:
                c.Left = Read(r);
                c.Right = Read(r);
                break;
            default:
                throw new InvalidDataException($"condition kind {(int)c.Kind}");
        }
        return c;
    }

    internal void Write(LogicWriter w)
    {
        w.U32((uint)Kind);
        if (Call != null) Call.Write(w);
        else { Left!.Write(w); Right!.Write(w); }
    }

    public IEnumerable<LogicCall> Tests()
    {
        if (Call != null) yield return Call;
        if (Left != null) foreach (LogicCall c in Left.Tests()) yield return c;
        if (Right != null) foreach (LogicCall c in Right.Tests()) yield return c;
    }
}

/// <summary>
/// A call of one of the game's script functions: an action ("sf_...", statemachine_functions.xtbl)
/// or a test ("ec_...", edge_conditions.xtbl). Its arguments are in the order of the table's Parameters.
/// </summary>
public sealed class LogicCall
{
    public string Function { get; set; } = "";
    public bool IsTest { get; private init; }
    /// <summary>The handler variable an action's result goes to (variables - set boolean: the one it sets); -1 none.</summary>
    public int Result { get; set; } = -1;
    public uint Unknown { get; set; }
    public List<LogicValue> Arguments { get; } = [];

    public static LogicCall NewTest(string function) => new() { Function = function, IsTest = true };
    public static LogicCall NewAction(string function) => new() { Function = function };

    /// <summary>A call of a function with each of its parameters at a starting value (see <see cref="LogicValue.DefaultFor"/>).</summary>
    public static LogicCall Of(LogicFunctions.Function function)
    {
        LogicCall call = function.IsTest ? NewTest(function.Code) : NewAction(function.Code);
        foreach (LogicFunctions.Parameter p in function.Parameters) call.Arguments.Add(LogicValue.DefaultFor(p.Type));
        return call;
    }

    internal static LogicCall Read(LogicReader r, bool test)
    {
        var c = new LogicCall { Function = r.CString(), IsTest = test };
        if (test && !c.Function.StartsWith("ec_", StringComparison.Ordinal)) throw new InvalidDataException("condition isn't an ec_ function");
        if (!test)
        {
            c.Result = r.I32();
            c.Unknown = r.U32();
        }
        for (int i = 0, n = r.I32(); i < n; i++) c.Arguments.Add(LogicValue.Read(r));
        return c;
    }

    internal void Write(LogicWriter w)
    {
        w.CString(Function);
        if (!IsTest)
        {
            w.I32(Result);
            w.U32(Unknown);
        }
        w.I32(Arguments.Count);
        foreach (LogicValue v in Arguments) v.Write(w);
    }
}

public enum LogicValueType { Number = 0, Vector = 1, String = 2, Object = 3, Boolean = 4, Reference = 5 }

/// <summary>
/// A value: a handler variable, an array element or a call's argument (0x9395A0).
/// <code>
/// Number (0):  u8 player, u8 owner, u32 form: 0 i32 whole number, 1 f32, 2..5 u32 handle + i32 number
/// Vector (1):  u8 player, u8 owner, u8, u32 form: 0/5 three f32, 1/2 u32 handle + i32 number (an object's position), 3/4 nothing
/// String (2):  char[32]
/// Object (3):  u8 player, u8 owner, u32 handle, i32 number. Handle FFFFFFFF with player: the player; with owner: the handler's owner
/// Boolean (4): u8
/// Reference (5): i32 variable index; -1 an array by name: i32 hash of its name; -2: i32 (not known)
/// </code>
/// </summary>
public sealed class LogicValue
{
    public byte[] NameBytes { get; set; } = [];
    public LogicValueType Type { get; set; }
    public bool Player { get; set; }
    public bool Owner { get; set; }
    public byte VectorFlag { get; set; }
    public uint Form { get; set; }
    public int Whole { get; set; }
    public float Number { get; set; }
    public float X { get; set; }
    public float Y { get; set; }
    public float Z { get; set; }
    public uint Handle { get; set; } = 0xFFFFFFFF;
    public int HandleNumber { get; set; } = -1;
    public FixedText Text { get; set; } = new("");
    public bool Boolean { get; set; }
    public int Reference { get; set; }
    public int ReferenceExtra { get; set; }

    /// <summary>A handler variable's name (the values in calls have none).</summary>
    public string Name
    {
        get => Encoding.Latin1.GetString(NameBytes).TrimEnd('\0');
        set => NameBytes = Encoding.Latin1.GetBytes(value.Length > 31 ? value[..31] : value);
    }

    /// <summary>A starting value for a parameter of a type the function tables name (ObjHandle, Number, Boolean, String, Vector).</summary>
    public static LogicValue DefaultFor(string parameterType) => parameterType.Trim().ToLowerInvariant() switch
    {
        "number" => OfNumber(0),
        "boolean" => OfBoolean(false),
        "string" => OfText(""),
        "vector" => new() { Type = LogicValueType.Vector, Form = 0 },
        _ => OfObject(0xFFFFFFFF),
    };

    public LogicValue Clone()
    {
        var w = new LogicWriter();
        Write(w);
        return Read(new LogicReader(w.ToArray()));
    }

    /// <summary>Whether it names an object by its handle (an object, or an object's number or position).</summary>
    public bool NamesObject =>
        Type == LogicValueType.Object || Type == LogicValueType.Number && Form >= 2 || Type == LogicValueType.Vector && Form is 1 or 2;

    public static LogicValue OfWhole(int value) => new() { Type = LogicValueType.Number, Form = 0, Whole = value };
    public static LogicValue OfNumber(float value) => new() { Type = LogicValueType.Number, Form = 1, Number = value };
    public static LogicValue OfBoolean(bool value) => new() { Type = LogicValueType.Boolean, Boolean = value };
    public static LogicValue OfText(string value) => new() { Type = LogicValueType.String, Text = new(value) };
    public static LogicValue OfObject(uint handle) => new() { Type = LogicValueType.Object, Handle = handle };
    public static LogicValue OfPlayer() => new() { Type = LogicValueType.Object, Player = true };
    public static LogicValue OfSelf() => new() { Type = LogicValueType.Object, Owner = true };
    public static LogicValue OfVariable(int index) => new() { Type = LogicValueType.Reference, Reference = index };
    public static LogicValue OfArray(string name) => new() { Type = LogicValueType.Reference, Reference = -1, ReferenceExtra = (int)NameHashes.PropertyHash(name) };

    internal static LogicValue Read(LogicReader r)
    {
        int nameLength = r.U16();
        if (nameLength > 31) throw new InvalidDataException("variable name too long");
        var v = new LogicValue { NameBytes = r.Bytes(nameLength), Type = (LogicValueType)r.U32() };
        switch (v.Type)
        {
            case LogicValueType.Number:
                v.Player = r.U8() != 0; v.Owner = r.U8() != 0; v.Form = r.U32();
                switch (v.Form)
                {
                    case 0: v.Whole = r.I32(); break;
                    case 1: v.Number = r.F32(); break;
                    case 2 or 3 or 4 or 5: v.Handle = r.U32(); v.HandleNumber = r.I32(); break;
                    default: throw new InvalidDataException($"number form {v.Form}");
                }
                break;
            case LogicValueType.Vector:
                v.Player = r.U8() != 0; v.Owner = r.U8() != 0; v.VectorFlag = r.U8(); v.Form = r.U32();
                switch (v.Form)
                {
                    case 0 or 5: v.X = r.F32(); v.Y = r.F32(); v.Z = r.F32(); break;
                    case 1 or 2: v.Handle = r.U32(); v.HandleNumber = r.I32(); break;
                    case 3 or 4: break;
                    default: throw new InvalidDataException($"vector form {v.Form}");
                }
                break;
            case LogicValueType.String:
                v.Text = FixedText.Read(r, 32);
                break;
            case LogicValueType.Object:
                v.Player = r.U8() != 0; v.Owner = r.U8() != 0; v.Handle = r.U32(); v.HandleNumber = r.I32();
                break;
            case LogicValueType.Boolean:
                v.Boolean = r.U8() != 0;
                break;
            case LogicValueType.Reference:
                v.Reference = r.I32();
                if (v.Reference is -1 or -2) v.ReferenceExtra = r.I32();
                break;
            default:
                throw new InvalidDataException($"value type {(int)v.Type}");
        }
        return v;
    }

    internal void Write(LogicWriter w)
    {
        w.U16((ushort)NameBytes.Length);
        w.Bytes(NameBytes);
        w.U32((uint)Type);
        switch (Type)
        {
            case LogicValueType.Number:
                w.U8(Player); w.U8(Owner); w.U32(Form);
                if (Form == 0) w.I32(Whole);
                else if (Form == 1) w.F32(Number);
                else { w.U32(Handle); w.I32(HandleNumber); }
                break;
            case LogicValueType.Vector:
                w.U8(Player); w.U8(Owner); w.U8(VectorFlag); w.U32(Form);
                if (Form is 0 or 5) { w.F32(X); w.F32(Y); w.F32(Z); }
                else if (Form is 1 or 2) { w.U32(Handle); w.I32(HandleNumber); }
                break;
            case LogicValueType.String:
                Text.Write(w, 32);
                break;
            case LogicValueType.Object:
                w.U8(Player); w.U8(Owner); w.U32(Handle); w.I32(HandleNumber);
                break;
            case LogicValueType.Boolean:
                w.U8(Boolean);
                break;
            case LogicValueType.Reference:
                w.I32(Reference);
                if (Reference is -1 or -2) w.I32(ReferenceExtra);
                break;
        }
    }
}

/// <summary>A timeline's pause track.</summary>
public sealed class LogicTrack
{
    public FixedText Name { get; set; } = new("");
    public byte[] Comment { get; set; } = [];
    public uint Value { get; set; }
    public LogicCondition Condition { get; set; } = LogicCondition.Test(LogicCall.NewTest("ec_always_true"));

    internal static LogicTrack Read(LogicReader r)
    {
        var t = new LogicTrack { Name = FixedText.Read(r, 32) };
        t.Comment = r.Bytes(r.I32());
        t.Value = r.U32();
        t.Condition = LogicCondition.Read(r);
        return t;
    }

    internal void Write(LogicWriter w)
    {
        Name.Write(w, 32);
        w.I32(Comment.Length);
        w.Bytes(Comment);
        w.U32(Value);
        Condition.Write(w);
    }
}

/// <summary>A timeline's event (calls at a time) or action (calls, plus logic objects that run in it).</summary>
public sealed class LogicEvent
{
    public bool IsAction { get; private init; }
    public int Start { get; set; }
    public int End { get; set; }
    public byte[] Comment { get; set; } = [];
    public uint Value { get; set; }
    public FixedText Name { get; set; } = new("");
    public List<LogicCall> Calls { get; } = [];
    public List<LogicObject> Inner { get; } = [];

    internal static LogicEvent Read(LogicReader r, bool action)
    {
        var e = new LogicEvent { IsAction = action, Start = r.I32(), End = r.I32() };
        e.Comment = r.Bytes(r.I32());
        if (action) e.Value = r.U32();
        e.Name = FixedText.Read(r, 32);
        for (int i = 0, n = r.I32(); i < n; i++) e.Calls.Add(LogicCall.Read(r, false));
        if (action) for (uint i = 0, n = r.U32(); i < n; i++) e.Inner.Add(LogicObject.Read(r));
        return e;
    }

    internal void Write(LogicWriter w)
    {
        w.I32(Start);
        w.I32(End);
        w.I32(Comment.Length);
        w.Bytes(Comment);
        if (IsAction) w.U32(Value);
        Name.Write(w, 32);
        w.I32(Calls.Count);
        foreach (LogicCall c in Calls) c.Write(w);
        if (IsAction)
        {
            w.U32((uint)Inner.Count);
            foreach (LogicObject o in Inner) o.Write(w);
        }
    }
}

/// <summary>A named array of values (squad spawn nodes, mostly), with two lists the game keeps per entry.</summary>
public sealed class LogicArray
{
    public int Most { get; set; }
    public FixedText Name { get; set; } = new("");
    public int Unknown { get; set; } = -1;
    public List<LogicValue> Elements { get; } = [];
    public List<(uint Id, List<LogicValue> Values)> ListA { get; } = [];
    public List<(uint Id, List<LogicValue> Values)> ListB { get; } = [];

    public LogicArray Clone()
    {
        var w = new LogicWriter();
        Write(w);
        return Read(new LogicReader(w.ToArray()));
    }

    internal static LogicArray Read(LogicReader r)
    {
        var a = new LogicArray { Most = r.I32(), Name = FixedText.Read(r, 16), Unknown = r.I32() };
        for (int i = 0, n = r.I32(); i < n; i++) a.Elements.Add(LogicValue.Read(r));
        foreach (var list in new[] { a.ListA, a.ListB })
            for (int i = 0, n = r.I32(); i < n; i++)
            {
                uint id = r.U32();
                var values = new List<LogicValue>();
                for (int j = 0, m = r.I32(); j < m; j++) values.Add(LogicValue.Read(r));
                list.Add((id, values));
            }
        return a;
    }

    internal void Write(LogicWriter w)
    {
        w.I32(Most);
        Name.Write(w, 16);
        w.I32(Unknown);
        w.I32(Elements.Count);
        foreach (LogicValue v in Elements) v.Write(w);
        foreach (var list in new[] { ListA, ListB })
        {
            w.I32(list.Count);
            foreach (var (id, values) in list)
            {
                w.U32(id);
                w.I32(values.Count);
                foreach (LogicValue v in values) v.Write(w);
            }
        }
    }
}

/// <summary>
/// The game's script functions, from statemachine_functions.xtbl (actions, "sf_...") and
/// edge_conditions.xtbl (tests, "ec_..."): each one's name for people and its parameters.
/// </summary>
public sealed class LogicFunctions
{
    public sealed record Parameter(string Type, string Name, string Description);
    public sealed record Function(string Code, string Name, string Category, string Comments, IReadOnlyList<Parameter> Parameters, bool IsTest);

    readonly Dictionary<string, Function> byCode = new(StringComparer.OrdinalIgnoreCase);

    public IEnumerable<Function> All => byCode.Values;

    public Function? Find(string code) => byCode.TryGetValue(code, out Function? f) ? f : null;

    /// <param name="actions">statemachine_functions.xtbl</param>
    /// <param name="tests">edge_conditions.xtbl</param>
    public LogicFunctions(Tables.XtblFile? actions, Tables.XtblFile? tests)
    {
        Add(actions, false);
        Add(tests, true);
    }

    void Add(Tables.XtblFile? table, bool tests)
    {
        foreach (Tables.XtblElement entry in table?.Entries ?? [])
        {
            if (entry.ValueOf("code_name") is not { Length: > 0 } code) continue;
            var parameters = new List<Parameter>();
            foreach (Tables.XtblElement p in entry.Element("Parameters")?.ElementsNamed("Param") ?? [])
                parameters.Add(new Parameter(p.ValueOf("Param_Type") ?? "", p.ValueOf("Param_Name") ?? "", p.ValueOf("Description") ?? ""));
            string category = entry.Element("_Editor")?.ValueOf("Category") ?? "";
            if (category.StartsWith("Entries:", StringComparison.Ordinal)) category = category["Entries:".Length..];
            byCode[code.Trim()] = new Function(code.Trim(), entry.ValueOf("Name")?.Trim() ?? code, category, entry.ValueOf("Comments")?.Trim() ?? "", parameters, tests);
        }
    }
}

/// <summary>Writes a script, or a part of one, out for people to read.</summary>
public sealed class LogicText(LogicScript script, LogicFunctions? functions, Func<uint, string?>? objectName)
{
    readonly StringBuilder text = new();
    readonly Dictionary<int, string> arrays = script.Arrays.GroupBy(a => (int)NameHashes.PropertyHash(a.Name.Text)).ToDictionary(g => g.Key, g => g.First().Name.Text);

    /// <summary>One state machine or timeline of the script, written out.</summary>
    public string Describe(LogicObject o)
    {
        text.Clear();
        Object(0, o);
        return text.ToString().TrimEnd();
    }

    public string Write()
    {
        text.Clear();
        for (int i = 0; i < script.Variables.Count; i++) Line(0, $"{script.VariableName(i)} = {Value(script.Variables[i])}");
        foreach (LogicArray a in script.Arrays)
            Line(0, $"array \"{a.Name}\" (up to {a.Most}): " + string.Join(", ", a.Elements.Select(Value)));
        if (script.Variables.Count > 0 || script.Arrays.Count > 0) text.AppendLine();
        foreach (LogicObject o in script.Objects) Object(0, o);
        return text.ToString().TrimEnd();
    }

    void Line(int depth, string line) => text.Append(' ', depth * 2).AppendLine(line);

    void Comment(int depth, byte[] comment)
    {
        if (comment.Length == 0) return;
        foreach (string line in Encoding.Latin1.GetString(comment).TrimEnd('\0').Split('\n'))
            Line(depth, "// " + line.TrimEnd('\r'));
    }

    void Object(int depth, LogicObject o)
    {
        if (o.Kind == LogicObjectKind.StateMachine)
        {
            Line(depth, $"statemachine \"{o.Name}\"");
            for (int i = 0; i < o.States.Count; i++)
            {
                LogicState s = o.States[i];
                Line(depth + 1, $"state \"{s.Name}\"" + (i == o.FirstState ? "  (first)" : ""));
                Comment(depth + 2, s.Comment);
                foreach (LogicCall c in s.OnEnter) Line(depth + 2, "on enter: " + Call(c));
                foreach (LogicCall c in s.EachFrame) Line(depth + 2, "each frame: " + Call(c));
                foreach (LogicCall c in s.OnExit) Line(depth + 2, "on exit: " + Call(c));
                foreach (LogicObject inner in s.Inner) Object(depth + 2, inner);
                foreach (LogicEdge e in s.Edges)
                {
                    string wait = e.MinWait == 0 && e.MaxWait == 0 ? ""
                        : e.MinWait == e.MaxWait ? $" after {N(e.MinWait)}s" : $" after {N(e.MinWait)}-{N(e.MaxWait)}s";
                    Line(depth + 2, $"-> \"{e.To}\"{wait} when {Condition(e.Condition)}");
                    Comment(depth + 3, e.Comment);
                }
            }
        }
        else
        {
            Line(depth, $"timeline \"{o.Name}\"");
            foreach (LogicTrack t in o.Tracks) Line(depth + 1, $"pause \"{t.Name}\" while {Condition(t.Condition)}");
            foreach (LogicEvent e in o.Events.Concat(o.Actions))
            {
                Line(depth + 1, $"{(e.IsAction ? "action" : "event")} \"{e.Name}\" at {e.Start}..{e.End}");
                Comment(depth + 2, e.Comment);
                foreach (LogicCall c in e.Calls) Line(depth + 2, Call(c));
                foreach (LogicObject inner in e.Inner) Object(depth + 2, inner);
            }
        }
    }

    public string Condition(LogicCondition c) => c.Kind switch
    {
        LogicConditionKind.Test => Call(c.Call!),
        LogicConditionKind.NotTest => "not " + Call(c.Call!),
        _ => $"({Condition(c.Left!)} {c.Kind.ToString().ToLowerInvariant()} {Condition(c.Right!)})",
    };

    public string Call(LogicCall c)
    {
        LogicFunctions.Function? f = functions?.Find(c.Function);
        var args = new List<string>();
        for (int i = 0; i < c.Arguments.Count; i++)
        {
            string? label = f != null && i < f.Parameters.Count ? f.Parameters[i].Name : null;
            args.Add(label is { Length: > 0 } ? $"{label}: {Value(c.Arguments[i])}" : Value(c.Arguments[i]));
        }
        string call = $"{f?.Name ?? c.Function}({string.Join(", ", args)})";
        return c.Result >= 0 ? $"{script.VariableName(c.Result)} <- {call}" : call;
    }

    public string Value(LogicValue v) => v.Type switch
    {
        LogicValueType.Number => v.Form switch
        {
            0 => v.Whole.ToString(CultureInfo.InvariantCulture),
            1 => N(v.Number),
            _ => $"number of {Handle(v)}",
        },
        LogicValueType.Vector => v.Form switch
        {
            0 or 5 => $"<{N(v.X)}, {N(v.Y)}, {N(v.Z)}>",
            1 or 2 => $"position of {Handle(v)}",
            _ => $"vector ({v.Form})",
        },
        LogicValueType.String => $"\"{v.Text}\"",
        LogicValueType.Object => Handle(v),
        LogicValueType.Boolean => v.Boolean ? "true" : "false",
        LogicValueType.Reference => v.Reference switch
        {
            >= 0 => script.VariableName(v.Reference),
            -1 => arrays.TryGetValue(v.ReferenceExtra, out string? name) ? $"array \"{name}\"" : $"array {(uint)v.ReferenceExtra:X8}",
            _ => $"ref({v.Reference}, {(uint)v.ReferenceExtra:X8})",
        },
        _ => "?",
    };

    public string Handle(LogicValue v)
    {
        if (v.Player) return "player";
        if (v.Owner) return "self";
        if (v.Handle == 0xFFFFFFFF) return "none";
        return objectName?.Invoke(v.Handle) is { Length: > 0 } name ? $"{name} ({v.Handle:X8})" : v.Handle.ToString("X8");
    }

    static string N(float f) => f.ToString("0.####", CultureInfo.InvariantCulture);
}

sealed class LogicReader(byte[] data)
{
    public int Position;
    public uint Version = 11;

    public byte U8() => data[Position++];
    public ushort U16() { ushort v = BitConverter.ToUInt16(data, Position); Position += 2; return v; }
    public uint U32() { uint v = BitConverter.ToUInt32(data, Position); Position += 4; return v; }
    public int I32() { int v = BitConverter.ToInt32(data, Position); Position += 4; return v; }
    public float F32() { float v = BitConverter.ToSingle(data, Position); Position += 4; return v; }

    public byte[] Bytes(int n)
    {
        if (n < 0 || Position + n > data.Length) throw new InvalidDataException("past the end");
        byte[] v = data[Position..(Position + n)];
        Position += n;
        return v;
    }

    public string CString()
    {
        int end = Array.IndexOf(data, (byte)0, Position);
        if (end < 0) throw new InvalidDataException("unterminated name");
        string v = Encoding.Latin1.GetString(data, Position, end - Position);
        Position = end + 1;
        return v;
    }
}

sealed class LogicWriter
{
    readonly List<byte> data = [];
    public int Length => data.Count;
    public void U8(byte v) => data.Add(v);
    public void U8(bool v) => data.Add(v ? (byte)1 : (byte)0);
    public void U16(ushort v) => data.AddRange(BitConverter.GetBytes(v));
    public void U32(uint v) => data.AddRange(BitConverter.GetBytes(v));
    public void I32(int v) => data.AddRange(BitConverter.GetBytes(v));
    public void F32(float v) => data.AddRange(BitConverter.GetBytes(v));
    public void Bytes(byte[] v) => data.AddRange(v);
    public void CString(string v) { data.AddRange(Encoding.Latin1.GetBytes(v)); data.Add(0); }
    public byte[] ToArray() => data.ToArray();
}
