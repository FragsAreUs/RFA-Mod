using System.IO;
using System.Numerics;
using System.Windows;
using System.Windows.Controls;
using RfaFormats;
using RfaFormats.Zones;

namespace RfaEditor;

// The classes only Volition's tools could make (a logic handler's script, a road's or spline's points, an
// animated rig's bones: GameAssets.ExampleClasses), added as copies of the game's own: in the Add list
// and the Assets panel, every different one the game's levels have (a rig by its name, a handler by the
// words in its script), each with what it hangs from and what hangs from it (the whole set: a trigger
// with its handler and marker, a monolith's rigs), its handles new and every handle the set's objects
// name of each other (their links, a rig's children, a handler's script) changed to the copies'.
public partial class LevelEditor
{
    /// <summary>The Add list's rows for them (or a note while the game's levels are being looked through).</summary>
    void AddExampleRows(string filter)
    {
        Task<IReadOnlyDictionary<uint, List<ObjectExample>>> found = context.Assets.ObjectExamples();
        if (!found.IsCompletedSuccessfully)
        {
            AddList.Items.Add(AddListHeading("FROM THE GAME'S LEVELS"));
            AddList.Items.Add(new ListBoxItem
            {
                IsEnabled = false, Focusable = false,
                Content = new TextBlock
                {
                    Text = "Logic handlers, roads, splines, animated rigs and constraints: being found in the game's levels (once). Open the list again in a moment.",
                    FontSize = 11, Foreground = Theme.Brush("DimTextBrush"), TextWrapping = TextWrapping.Wrap,
                },
            });
            found.ContinueWith(_ => Dispatcher.BeginInvoke(new Action(() => { if (AddPopup.IsOpen) FillAddList(AddFilterBox.Text.Trim()); })));
            return;
        }
        bool heading = false;
        foreach (var (classHash, examples) in found.Result.OrderBy(e => names.Class(e.Key)))
            foreach (ObjectExample example in examples)
            {
                string title = $"{ClassTitle(example.Class)}: {example.Label}";
                if (filter.Length > 0 && !title.Contains(filter, StringComparison.OrdinalIgnoreCase) && !example.Class.Contains(filter, StringComparison.OrdinalIgnoreCase)) continue;
                if (!heading) AddList.Items.Add(AddListHeading("FROM THE GAME'S LEVELS (MADE BY VOLITION'S TOOLS)"));
                heading = true;
                AddList.Items.Add(AddListRow(ObjectKind.Of(example.Class, false), title, example.Place.Zone, "",
                    $"A copy of {example.Place.Zone}'s {example.Class} ({example.Label}, handle {example.Place.Handle:X8}), with the objects it hangs from " +
                    "and those hanging from it, where the middle of the view points. Its data (script, points, bones) as the game's; every property editable.",
                    example));
            }
    }

    static string ClassTitle(string className)
    {
        string name = className.StartsWith("object_", StringComparison.OrdinalIgnoreCase) ? className[7..] : className;
        name = name.Replace('_', ' ');
        return name.Length == 0 ? className : char.ToUpperInvariant(name[0]) + name[1..];
    }

    /// <summary>Adds a copy of one of the game's objects (see the top of this file) at a place, and picks it.</summary>
    void AddExample(ObjectExample example, Vector3 at)
    {
        if (!context.Assets.IndexReady)
        {
            context.Log("The editor is still reading the game's files (to find handles the zone's other files use). Try again in a few seconds.");
            return;
        }
        ChunkPlacement place = example.Place;
        ZoneFile from;
        ZoneObject original;
        try
        {
            Packfile pack = place.Container == null ? Workspace.Archive(place.Archive) : Workspace.Container(place.Archive, place.Container);
            from = ZoneFile.Read(pack.Read(pack.Find(place.Zone) ?? throw new InvalidDataException($"{place.Zone} isn't there any more")));
            original = from.Objects.FirstOrDefault(o => o.Handle == place.Handle) ?? throw new InvalidDataException($"{place.Zone} has no object {place.Handle:X8} any more");
        }
        catch (Exception ex) when (ex is InvalidDataException or IOException)
        {
            context.Log($"Add: {example.Class} ({example.Label}) couldn't be read from {place.Zone}: {ex.Message}");
            return;
        }
        // The whole set: from the topmost object it hangs from.
        ZoneObject top = original;
        var byHandle = from.Objects.GroupBy(o => o.Handle).ToDictionary(g => g.Key, g => g.First());
        for (int guard = 0; guard < 64 && top.Parent != ZoneObject.None && byHandle.TryGetValue(top.Parent, out ZoneObject? parent); guard++) top = parent;
        IReadOnlyList<ZoneObject> group = from.WithAttached(top);
        if (!CanPlaceAt(at, $"A copy of {example.Class}")) return;
        CommitEdit();
        IReadOnlyList<ZoneObject> copies;
        try
        {
            copies = zone.AddCopiesFrom(from, group, TakenHandles(), at - original.Placement.Position);
        }
        catch (InvalidOperationException ex)
        {
            MessageBox.Show($"Couldn't add a copy of {example.Class}: {ex.Message}.", "Level editor", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }
        int renamed = RenameHandles(copies, group.Select(o => o.Handle).Zip(copies.Select(c => c.Handle)).ToDictionary(p => p.First, p => p.Second));
        foreach (ZoneObject copy in copies) scene?.Add(copy);
        RemovedObjects? removed = null;
        Record(new Change(copies[0], "Add",
            Undo: () => { removed = zone.Remove(copies.ToList()); foreach (ZoneObject copy in copies) scene?.Remove(copy); },
            Redo: () => { if (removed != null) zone.PutBack(removed); foreach (ZoneObject copy in copies) scene?.Add(copy); },
            AddsOrRemoves: true));
        ObjectsChanged();
        // The set's models (a monolith's claws and rocks, a tentacle's rocks) this zone's world hasn't: brought in on saving.
        var needs = new List<string>();
        NoteChunksToBring(copies, ChunkContainer()?.Container ?? zoneName, needs);
        ZoneObject picked = copies[Math.Max(0, group.ToList().IndexOf(original))];
        PickInOutliner(picked);
        context.Log($"Add: a copy of {place.Zone}'s {example.Class} ({example.Label})" +
                    (copies.Count > 1 ? $" with the {copies.Count - 1} object(s) of its set ({string.Join(", ", copies.Where(c => c != picked).GroupBy(c => names.Class(c.ClassHash)).Select(g => $"{g.Count()} {g.Key}"))})" : "") +
                    $", where the view points; {renamed} handle(s) in their links and data changed to the copies'." +
                    (needs.Count > 0 ? $" Brought in on saving: {string.Join(", ", needs)}." : ""));
    }

    /// <summary>
    /// Every handle of <paramref name="map"/>'s originals that the copies name (in their lists: links, a rig's children,
    /// a handler's script; and in 4-byte values), at a 4-byte boundary, changed to its copy's. Returns how many.
    /// </summary>
    static int RenameHandles(IEnumerable<ZoneObject> copies, Dictionary<uint, uint> map)
    {
        int changed = 0;
        foreach (ZoneObject copy in copies)
            foreach (ZoneProperty property in copy.Properties.ToList())
            {
                if (!(property.Type == ZoneProperty.ListType || property.Type == ZoneProperty.ValueType && property.Size == 4)) continue;
                byte[] value = property.Value;
                bool any = false;
                for (int i = 0; i + 4 <= value.Length; i += 4)
                    if (map.TryGetValue(BitConverter.ToUInt32(value, i), out uint to))
                    {
                        BitConverter.TryWriteBytes(value.AsSpan(i), to);
                        any = true;
                        changed++;
                    }
                if (any) property.SetValue(value);
            }
        return changed;
    }
}
