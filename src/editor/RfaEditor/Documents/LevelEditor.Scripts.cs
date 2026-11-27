using System.Numerics;
using System.Windows;
using RfaFormats.Zones;

namespace RfaEditor;

// Logic handlers' scripts: the script editor (ScriptEditor) beside the level for a handler, writing back
// as one step to undo, and new handlers attached to the picked object.
public partial class LevelEditor
{
    static readonly uint LogicHandlerClass = NameHashes.ClassHash("object_logic_handler");
    static readonly uint ScriptProperty = NameHashes.PropertyHash("logic_handler_buffer");

    /// <summary>The script editors open, by their handler (one each).</summary>
    readonly Dictionary<ZoneObject, ScriptEditor> scriptEditors = [];

    /// <summary>Opens the script editor for a handler (brings it forward if it is open).</summary>
    void EditScript(ZoneObject handler, bool isNew = false)
    {
        if (scriptEditors.TryGetValue(handler, out ScriptEditor? open)) { open.Activate(); return; }
        if (!Editable(handler))
        {
            MessageBox.Show("This handler is in another of the zone's files: open that file to change its script.", "Level editor");
            return;
        }
        if (handler.Property(ScriptProperty) is not { } property || LogicScript.Read(property.Value) is not { } script)
        {
            MessageBox.Show("The editor can't read this handler's script.", "Level editor");
            return;
        }
        ZoneObject? owner = zone.Objects.FirstOrDefault(o => o.Handle == handler.Parent);
        var scriptContext = new ScriptContext(context.Assets.LogicFunctions, ScriptObjects(), () => selected?.Handle,
                                              handler.Parent, s => ApplyScript(handler, s))
        {
            TextChoices = context.Assets.ScriptTextChoices,
            GameRecipes = context.Assets.GameRecipes,
        };
        string title = $"Script of {(owner != null ? $"{names.Class(owner.ClassHash)} \"{Label(owner)}\"" : $"handler {handler.Handle:X8}")}";
        ScriptEditor editor = ScriptEditor.Open(Window.GetWindow(this), title, script, scriptContext, offerRecipes: isNew);
        scriptEditors[handler] = editor;
        editor.Closed += (_, _) => scriptEditors.Remove(handler);
    }

    /// <summary>The objects a script can name: this file's and the zone's other files' (a mission layer names the world's).</summary>
    List<ScriptObject> ScriptObjects()
    {
        var list = new List<ScriptObject>();
        var seen = new HashSet<uint>();
        foreach (ZoneObject obj in zone.Objects.Concat(RelatedFiles().SelectMany(f => f.Zone.Objects)))
        {
            if (obj.ClassHash == LogicHandlerClass || !seen.Add(obj.Handle)) continue;
            string label = Label(obj);
            list.Add(new ScriptObject(obj.Handle, names.Class(obj.ClassHash), label.StartsWith('#') ? "" : label));
        }
        return list;
    }

    /// <summary>Writes a script to its handler as one step to undo; null if done, else why not.</summary>
    string? ApplyScript(ZoneObject handler, LogicScript script)
    {
        if (!zone.Objects.Contains(handler)) return "The handler isn't in the zone any more (it was deleted, or its adding undone).";
        if (handler.Property(ScriptProperty) is not { } property) return "The handler has no script.";
        byte[] bytes = script.Write();
        if (bytes.Length > 0xFFFF) return $"The script came to {bytes.Length:N0} bytes; a handler holds at most 65,535.";
        if (property.Value.AsSpan().SequenceEqual(bytes)) return null;
        if (handler == selected) CommitEdit();  // a change being typed in the inspector, as a step of its own
        byte[] before = handler.Snapshot();
        property.SetValue(bytes);
        byte[] now = handler.Snapshot();
        Record(new Change(handler, "Edit script", () => handler.Restore(before), () => handler.Restore(now)));
        if (handler == selected)
        {
            StartEdit();
            ShowPropertyRows();
        }
        context.Log($"Script: wrote the script of handler {handler.Handle:X8} ({bytes.Length:N0} bytes).");
        return null;
    }

    void OnAddLogicHandler(object sender, RoutedEventArgs e)
    {
        if (selected == null) { MessageBox.Show("Pick the object the handler is for first (a trigger, a dummy, a spawn node...).", "Level editor"); return; }
        AddLogicHandler(selected);
    }

    /// <summary>
    /// Adds a new logic handler attached to an object (its "self": the game wants a handler's script to name the
    /// object it is attached to as its owner), and opens its script with the recipes offered.
    /// </summary>
    void AddLogicHandler(ZoneObject parent)
    {
        if (!context.Assets.IndexReady)
        {
            context.Log("The editor is still reading the game's files (to find handles the zone's other files use). Try again in a few seconds.");
            return;
        }
        if (!Editable(parent)) { MessageBox.Show("That object is in another of the zone's files: open that file to add a handler to it.", "Level editor"); return; }
        if (parent.ClassHash == LogicHandlerClass) { EditScript(parent); return; }
        CommitEdit();

        var script = new LogicScript { Owner = parent.Handle };
        script.Objects.Add(LogicObject.NewStateMachine("Statemachine1"));  // a handler with none fails to load
        var placement = new byte[48];  // where: nowhere of its own (0, 0, 0), not turned, as the game's handlers
        BitConverter.TryWriteBytes(placement.AsSpan(12), 1f);
        BitConverter.TryWriteBytes(placement.AsSpan(28), 1f);
        BitConverter.TryWriteBytes(placement.AsSpan(44), 1f);
        NewProperty[] properties =
        [
            new(ZoneProperty.ValueType, NameHashes.PropertyHash("oP"), placement),
            new(ZoneProperty.ListType, ScriptProperty, script.Write()),
        ];
        ZoneObject handler;
        try
        {
            handler = zone.AddNew(LogicHandlerClass, properties, parent.BoundsMin, parent.BoundsMax, TakenHandles());
            // The flags the zone's handlers have (a mission layer's are 1, the world zones' 0).
            var flags = zone.Objects.Where(o => o.ClassHash == LogicHandlerClass && o != handler).GroupBy(o => o.Flags)
                            .OrderByDescending(g => g.Count()).Select(g => (ushort?)g.Key).FirstOrDefault();
            handler.Flags = flags ?? (ushort)(zoneName.Contains("_128", StringComparison.OrdinalIgnoreCase) ? 1 : 0);
            zone.Attach(handler, parent);
        }
        catch (InvalidOperationException ex)
        {
            MessageBox.Show($"Couldn't add a logic handler: {ex.Message}.", "Level editor", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        scene?.Add(handler);
        RemovedObjects? removed = null;
        Record(new Change(handler, "Add logic handler",
            Undo: () => { removed = zone.Remove(handler); scene?.Remove(handler); },
            Redo: () => { if (removed != null) zone.PutBack(removed); scene?.Add(handler); },
            AddsOrRemoves: true));
        ObjectsChanged();
        PickInOutliner(handler);
        context.Log($"Add: a logic handler (handle {handler.Handle:X8}) attached to {names.Class(parent.ClassHash)} {parent.Handle:X8}.");
        EditScript(handler, isNew: true);
    }
}
