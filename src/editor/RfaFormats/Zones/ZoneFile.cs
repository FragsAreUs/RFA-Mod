using System.Numerics;
using System.Text;

namespace RfaFormats.Zones;

/// <summary>
/// A zone: the objects placed in one part of a level. The world is a grid of zones
/// (world_07_12.rfgzone_pc, dlc04_11_09.rfgzone_pc, ...) inside the level archives' containers;
/// each mission adds layers (world_07_12_128.layer_pc) with its own objects, and p_*.rfgzone_pc
/// hold persistent ones. All use the same layout, "ZONE" version 46:
///
///   0x00     "ZONE", version 46, object count, 0, size of the objects block, 0, size of the extra
///            block, then two hashes (the district), 0, and flags (4: no relation table) (40 bytes)
///   0x28     the relation table (87,368 bytes; see <see cref="ZoneRelationTable"/>); mission
///            layers (.layer_pc) don't have one
///   then     the objects, one after another, then the extra block: the destruction links (see
///            <see cref="DestructionLinkSize"/>)
///
/// Each object: class name hash, handle, bounding box (6 floats), flags (u16), size of the object
/// (u16, 56 + its properties), parent, sibling and child handles (0xFFFFFFFF = none), a number,
/// property count (u16) and size of the properties (u16); then each property: type (u16), size of
/// its value (u16), name hash, the value, and padding to 4 bytes. Types: 4 = text, 5 = a value
/// (by size: 1 = flag, 4 = number, 12 = vector, 16 = colour, 36 = rotation, 48 = position and
/// rotation, such as "oP"), 6 = a list (links between objects).
///
/// Objects can be added (copies of others, <see cref="Copy"/>) and removed (with the objects
/// attached to them and their destruction links), and properties can grow (longer text); the file
/// is then laid out again when written, and the relation table built again, as the game's tools
/// build it.
/// </summary>
public sealed class ZoneFile
{
    public const uint Signature = 0x454E4F5A;  // "ZONE"
    public const uint Version = 46;
    const int HeaderSize = 0x28;

    /// <summary>
    /// The extra block holds destruction links, 40 bytes each: two u16 numbers (pieces of the two
    /// objects, 0xFFFF for none), flags (u32), the two destructible objects' handles (rfg_mover,
    /// general_mover), 0xFFFFFFFF twice (nearly always), a number (a strength?), 0xFFFFFFFF twice,
    /// and the link's own number (the low three bytes; the top byte is a flag, 0 or 8). An
    /// object's "dynamic_links" lists the numbers of the links that name it, in order, then a 0.
    /// </summary>
    public const int DestructionLinkSize = 40;

    static readonly uint DynamicLinks = NameHashes.PropertyHash("dynamic_links");
    static readonly uint Uid = NameHashes.PropertyHash("uid");

    readonly byte[] header;          // the first 40 bytes; the counts and sizes are set when written
    readonly byte[]? relationTable;  // as read; written as it is while the objects are as read
    byte[] extra;                    // the destruction links
    readonly List<ZoneObject> objects;
    readonly ZoneObject[] asRead;
    readonly int[] asReadSizes;

    public IReadOnlyList<ZoneObject> Objects => objects;
    public uint DistrictHash => BitConverter.ToUInt32(header, 0x1C);
    public bool HasRelationTable => relationTable != null;

    ZoneFile(byte[] bytes)
    {
        if (bytes.Length < HeaderSize || BitConverter.ToUInt32(bytes, 0) != Signature)
            throw new InvalidDataException("not a zone file (ZONE)");
        uint version = BitConverter.ToUInt32(bytes, 4);
        if (version != Version) throw new InvalidDataException($"zone version {version}; only version 46 is known");
        int count = (int)BitConverter.ToUInt32(bytes, 8);
        uint objectsSize = BitConverter.ToUInt32(bytes, 0x10), extraSize = BitConverter.ToUInt32(bytes, 0x18);

        // Whatever the objects and extra block don't take up is the relation table (or nothing).
        long tableSize = bytes.Length - HeaderSize - (long)objectsSize - extraSize;
        if (tableSize is not (0 or ZoneRelationTable.Size))
            throw new InvalidDataException("the zone's block sizes don't add up to its size");
        header = bytes[..HeaderSize];
        relationTable = tableSize == 0 ? null : bytes[HeaderSize..(HeaderSize + ZoneRelationTable.Size)];
        extra = bytes[(bytes.Length - (int)extraSize)..];

        objects = new List<ZoneObject>(count);
        int at = HeaderSize + (int)tableSize;
        for (int i = 0; i < count; i++)
        {
            if (at + ZoneObject.HeaderSize > bytes.Length) throw new InvalidDataException($"object {i} is past the end of the zone");
            int size = BitConverter.ToUInt16(bytes, at + 34);
            try
            {
                objects.Add(new ZoneObject(bytes[at..(at + size)]) { Index = i });
            }
            catch (InvalidDataException ex)
            {
                throw new InvalidDataException($"object {i}: {ex.Message}");
            }
            at += size;
        }
        asRead = [.. objects];
        asReadSizes = objects.Select(o => o.Size).ToArray();
    }

    public static ZoneFile Read(byte[] data) => new(data);

    /// <summary>The zone as a file, with every change made to it.</summary>
    public byte[] ToBytes()
    {
        var output = new MemoryStream();
        byte[] head = (byte[])header.Clone();
        BitConverter.TryWriteBytes(head.AsSpan(0x08), objects.Count);
        BitConverter.TryWriteBytes(head.AsSpan(0x10), objects.Sum(o => o.Size));
        BitConverter.TryWriteBytes(head.AsSpan(0x18), extra.Length);
        output.Write(head);
        if (relationTable != null)
        {
            bool asWasRead = objects.Count == asRead.Length && Enumerable.Range(0, asRead.Length).All(i => objects[i] == asRead[i] && objects[i].Size == asReadSizes[i]);
            output.Write(asWasRead
                ? relationTable  // the same objects in the same places: the game's own table
                : ZoneRelationTable.Build(objects.Select(o => (o.Handle, o.Size)).ToList(), BitConverter.ToUInt32(relationTable, 0)));
        }
        foreach (ZoneObject obj in objects) output.Write(obj.Bytes);
        output.Write(extra);
        return output.ToArray();
    }

    // ---- Adding objects ------------------------------------------------------------------------

    /// <summary>
    /// A copy of an object with a new handle, to add with <see cref="Insert"/>: its properties as
    /// they are, but attached to nothing (no parent, sibling or child) and without "dynamic_links"
    /// (its destruction links to the objects next to it, which belong to the original).
    /// </summary>
    public static ZoneObject Copy(ZoneObject source, uint handle)
    {
        var copy = new ZoneObject(source.Snapshot());
        copy.RemoveProperty(DynamicLinks);
        copy.Handle = handle;
        copy.Parent = copy.Sibling = copy.Child = ZoneObject.None;
        return copy;
    }

    /// <summary>
    /// A handle for a new object like <paramref name="like"/>: the same first two bytes (the zone,
    /// and its layer: 0x82 for world_08_02, 0x80 in its mission layer 128), and a number after the
    /// highest one used with them, here or in <paramref name="taken"/> (the zone's other files).
    /// </summary>
    public uint NewHandle(ZoneObject like, IEnumerable<uint> taken)
    {
        uint prefix = like.Handle & 0xFFFF0000;
        var used = objects.Select(o => o.Handle).Concat(taken).Where(h => (h & 0xFFFF0000) == prefix).Select(h => h & 0xFFFF).ToHashSet();
        uint next = used.Count == 0 ? 1 : used.Max() + 1;
        if (next > 0xFFFE)
        {
            next = 1;
            while (used.Contains(next)) next++;
            if (next > 0xFFFE) throw new InvalidOperationException("no handles are left for new objects here");
        }
        return prefix | next;
    }

    /// <summary>Puts an object (a <see cref="Copy"/>) into the zone at a place in its list.</summary>
    public void Insert(int index, ZoneObject obj)
    {
        if (objects.Contains(obj)) throw new InvalidOperationException("the object is in the zone already");
        if (objects.Any(o => o.Handle == obj.Handle)) throw new InvalidOperationException($"another object has the handle {obj.Handle:X8}");
        if (relationTable != null && objects.Count >= ZoneRelationTable.Entries)
            throw new InvalidOperationException($"a zone can hold at most {ZoneRelationTable.Entries:N0} objects");
        objects.Insert(index, obj);
        Renumber();
    }

    // ---- What names an object ------------------------------------------------------------------

    /// <summary>An object and every object attached to it, and to those, and so on (by their parent handles).</summary>
    public IReadOnlyList<ZoneObject> WithAttached(ZoneObject root)
    {
        var children = objects.Where(o => o.Parent != ZoneObject.None).ToLookup(o => o.Parent);
        var found = new List<ZoneObject> { root };
        var seen = new HashSet<ZoneObject> { root };
        for (int i = 0; i < found.Count; i++)
            foreach (ZoneObject child in children[found[i].Handle])
                if (seen.Add(child)) found.Add(child);
        return found;
    }

    /// <summary>
    /// What in the zone names an object (by its handle), outside <paramref name="alsoGoing"/>
    /// (objects removed with it): objects attached to it, and properties of other objects (links,
    /// handles). Its place among its parent's children and its destruction links aren't listed:
    /// <see cref="Remove(IReadOnlyCollection{ZoneObject})"/> takes care of those. Removing an object
    /// something names would leave that pointing at nothing.
    /// </summary>
    public IEnumerable<ZoneReference> ReferencesTo(ZoneObject target, IReadOnlyCollection<ZoneObject>? alsoGoing = null) =>
        ReferencesTo(target.Handle, objects.Where(o => o != target && (alsoGoing == null || !alsoGoing.Contains(o))));

    /// <summary>What in another file (a mission layer, say) names a handle: its objects' properties and links.</summary>
    public static IEnumerable<ZoneReference> ReferencesIn(ZoneFile other, uint handle) => ReferencesTo(handle, other.objects);

    static IEnumerable<ZoneReference> ReferencesTo(uint handle, IEnumerable<ZoneObject> objects)
    {
        foreach (ZoneObject obj in objects)
        {
            if (obj.Parent == handle) yield return new ZoneReference(obj, null, "attached to it");
            foreach (ZoneProperty property in obj.Properties)
            {
                if (property.NameHash == Uid) continue;
                for (int i = 0; i + 4 <= property.Size; i += 4)
                    if (BitConverter.ToUInt32(obj.Bytes, property.ValueAt + i) == handle)
                    {
                        yield return new ZoneReference(obj, property.NameHash, "names it");
                        break;
                    }
            }
        }
    }

    /// <summary>How many destruction links name an object.</summary>
    public int DestructionLinksOf(ZoneObject obj)
    {
        int count = 0;
        for (int at = 0; at + DestructionLinkSize <= extra.Length; at += DestructionLinkSize)
            if (BitConverter.ToUInt32(extra, at + 8) == obj.Handle || BitConverter.ToUInt32(extra, at + 12) == obj.Handle) count++;
        return count;
    }

    // ---- Removing objects ----------------------------------------------------------------------

    /// <summary>Takes one object out (see <see cref="Remove(IReadOnlyCollection{ZoneObject})"/>).</summary>
    public RemovedObjects Remove(ZoneObject obj) => Remove([obj]);

    /// <summary>
    /// Takes objects out of the zone (usually one with the objects attached to it,
    /// <see cref="WithAttached"/>). Call <see cref="ReferencesTo"/> first: nothing else should name
    /// them. The rest is kept in order:
    ///   an object attached to one that stays is taken out of its parent's children (the parent's
    ///     child, or the sibling before it, then points past it);
    ///   the destruction links naming any of them go, the others are numbered again from 0 (each
    ///     keeping its flag), and every object's "dynamic_links" follows (one left with none loses
    ///     the property, as objects without links have none in the game's files).
    /// </summary>
    /// <returns>What is needed to put them back: see <see cref="PutBack"/>.</returns>
    public RemovedObjects Remove(IReadOnlyCollection<ZoneObject> going)
    {
        var set = going.ToHashSet();
        var places = objects.Select((obj, index) => (obj, index)).Where(x => set.Contains(x.obj)).ToList();
        if (places.Count != set.Count) throw new InvalidOperationException("an object to remove isn't in the zone");
        var handles = set.Select(o => o.Handle).ToHashSet();
        var byHandle = objects.GroupBy(o => o.Handle).ToDictionary(g => g.Key, g => g.First());
        var before = new Dictionary<ZoneObject, byte[]>();
        void Changing(ZoneObject obj) => before.TryAdd(obj, obj.Snapshot());

        // Objects that stay and point at one going (a parent's first child, the sibling before):
        // past it, to the next sibling that stays.
        uint NextStaying(uint handle)
        {
            while (handles.Contains(handle) && byHandle.TryGetValue(handle, out ZoneObject? obj)) handle = obj.Sibling;
            return handle;
        }
        foreach (ZoneObject other in objects.Where(o => !set.Contains(o)))
        {
            if (handles.Contains(other.Child))
            {
                Changing(other);
                other.Child = NextStaying(other.Child);
            }
            if (handles.Contains(other.Sibling))
            {
                Changing(other);
                other.Sibling = NextStaying(other.Sibling);
            }
        }

        // The destruction links.
        byte[] extraBefore = extra;
        int links = extra.Length / DestructionLinkSize;
        var renumbered = new int[links];
        var kept = new MemoryStream();
        int keptCount = 0;
        for (int i = 0; i < links; i++)
        {
            int at = i * DestructionLinkSize;
            if (handles.Contains(BitConverter.ToUInt32(extra, at + 8)) || handles.Contains(BitConverter.ToUInt32(extra, at + 12)))
            {
                renumbered[i] = -1;
                continue;
            }
            byte[] link = extra[at..(at + DestructionLinkSize)];
            uint flag = BitConverter.ToUInt32(link, 36) & 0xFF000000;
            BitConverter.TryWriteBytes(link.AsSpan(36), flag | (uint)keptCount);
            kept.Write(link);
            renumbered[i] = keptCount++;
        }
        kept.Write(extra.AsSpan(links * DestructionLinkSize));  // nothing, in every zone; kept if ever there
        if (keptCount != links)
        {
            extra = kept.ToArray();
            foreach (ZoneObject other in objects.Where(o => !set.Contains(o)))
            {
                if (other.Property(DynamicLinks) is not { } list) continue;
                byte[] value = list.Value;
                var numbers = new List<ushort>();
                for (int i = 0; i + 2 < value.Length; i += 2)  // the last u16 is the closing 0
                {
                    int old = BitConverter.ToUInt16(value, i);
                    if (old < links && renumbered[old] >= 0) numbers.Add((ushort)renumbered[old]);
                }
                var newValue = new byte[(numbers.Count + 1) * 2];
                for (int i = 0; i < numbers.Count; i++) BitConverter.TryWriteBytes(newValue.AsSpan(i * 2), numbers[i]);
                if (newValue.AsSpan().SequenceEqual(value)) continue;
                Changing(other);
                if (numbers.Count == 0) other.RemoveProperty(DynamicLinks);
                else list.SetValue(newValue);
            }
        }

        foreach ((ZoneObject obj, int index) in places.OrderByDescending(p => p.index)) objects.RemoveAt(index);
        Renumber();
        return new RemovedObjects(places, before.Select(b => (b.Key, b.Value)).ToList(), extraBefore);
    }

    /// <summary>Puts removed objects back where they were, and the objects and destruction links changed with them as they were.</summary>
    public void PutBack(RemovedObjects removed)
    {
        foreach ((ZoneObject obj, int index) in removed.Objects.OrderBy(p => p.Index)) objects.Insert(index, obj);
        foreach ((ZoneObject obj, byte[] state) in removed.Changed) obj.Restore(state);
        extra = removed.ExtraBefore;
        Renumber();
    }

    void Renumber()
    {
        for (int i = 0; i < objects.Count; i++) objects[i].Index = i;
    }
}

/// <summary>Something that names an object: another object's property (<paramref name="PropertyHash"/>), or an object attached to it.</summary>
public readonly record struct ZoneReference(ZoneObject? From, uint? PropertyHash, string How);

/// <summary>
/// Objects taken out of a zone and where each was; the objects changed with them, as they were
/// before; and the destruction links as they were.
/// </summary>
public sealed record RemovedObjects(IReadOnlyList<(ZoneObject Object, int Index)> Objects,
                                    IReadOnlyList<(ZoneObject Object, byte[] Before)> Changed, byte[] ExtraBefore);

public sealed class ZoneObject
{
    public const uint None = 0xFFFFFFFF;
    internal const int HeaderSize = 56;

    byte[] bytes;  // its header (56 bytes) and properties, as in the file

    internal ZoneObject(byte[] bytes)
    {
        this.bytes = bytes;
        ReadProperties();
    }

    /// <summary>Where it is in its zone's list (from 0).</summary>
    public int Index { get; internal set; }

    public uint ClassHash => U32(0);
    public uint Handle { get => U32(4); internal set => SetU32(4, value); }
    public ushort Flags => BitConverter.ToUInt16(bytes, 32);

    /// <summary>The bounding box, in world coordinates (the game uses it to decide what to draw).</summary>
    public Vector3 BoundsMin => ReadVector(8);
    public Vector3 BoundsMax => ReadVector(20);

    /// <summary>The object it is attached to, if any (<see cref="None"/>).</summary>
    public uint Parent { get => U32(36); internal set => SetU32(36, value); }

    /// <summary>The next object attached to the same parent.</summary>
    public uint Sibling { get => U32(40); internal set => SetU32(40, value); }

    /// <summary>The first object attached to it.</summary>
    public uint Child { get => U32(44); internal set => SetU32(44, value); }

    public List<ZoneProperty> Properties { get; } = [];

    internal byte[] Bytes => bytes;
    internal int Size => bytes.Length;

    public ZoneProperty? Property(uint nameHash) => Properties.FirstOrDefault(p => p.NameHash == nameHash);
    public ZoneProperty? Property(string name) => Property(NameHashes.PropertyHash(name));

    /// <summary>Where the object is and how it is turned: its "oP" property if it has one, else
    /// the middle of its bounding box, unturned.</summary>
    public (Vector3 Position, Matrix4x4 Rotation) Placement
    {
        get
        {
            if (Property("oP") is { Size: 48 } op)
            {
                float[] f = op.Floats();
                // The 9 numbers after the position are the right, up and forward directions.
                var rotation = new Matrix4x4(f[3], f[4], f[5], 0, f[6], f[7], f[8], 0, f[9], f[10], f[11], 0, 0, 0, 0, 1);
                return (new Vector3(f[0], f[1], f[2]), rotation);
            }
            return ((BoundsMin + BoundsMax) / 2, Matrix4x4.Identity);
        }
    }

    /// <summary>The object's bytes (header and properties), for undo.</summary>
    public byte[] Snapshot() => (byte[])bytes.Clone();

    /// <summary>Puts back bytes taken with <see cref="Snapshot"/> (they may be a different size, if a property grew since).</summary>
    public void Restore(byte[] state)
    {
        bytes = (byte[])state.Clone();
        ReadProperties();
    }

    /// <summary>
    /// Finds the properties in the bytes. The same properties as before (after an undo, say) keep
    /// their <see cref="ZoneProperty"/> objects, which only move; otherwise they are read afresh.
    /// </summary>
    void ReadProperties()
    {
        int count = BitConverter.ToUInt16(bytes, 52);
        var found = new List<(int Type, uint Name, int ValueAt, int Size)>();
        int p = HeaderSize;
        for (int k = 0; k < count; k++)
        {
            if (p + 8 > bytes.Length) throw new InvalidDataException("its properties run past its end");
            int valueSize = BitConverter.ToUInt16(bytes, p + 2);
            found.Add((BitConverter.ToUInt16(bytes, p), BitConverter.ToUInt32(bytes, p + 4), p + 8, valueSize));
            p = (p + 8 + valueSize + 3) & ~3;
        }
        if (p != bytes.Length || BitConverter.ToUInt16(bytes, 34) != bytes.Length) throw new InvalidDataException("its properties don't add up to its size");
        if (found.Count == Properties.Count && found.Select(f => (f.Type, f.Name)).SequenceEqual(Properties.Select(q => (q.Type, q.NameHash))))
        {
            for (int k = 0; k < found.Count; k++) Properties[k].MoveTo(found[k].ValueAt, found[k].Size);
            return;
        }
        Properties.Clear();
        foreach (var (type, name, valueAt, size) in found) Properties.Add(new ZoneProperty(this, type, name, valueAt, size));
    }

    /// <summary>Lays the object out again with one property's value changed to a different size.</summary>
    internal void Resize(ZoneProperty changed, byte[] value) => LayOut(Properties.Select(p => (p, p == changed ? value : p.Value)).ToList());

    /// <summary>Takes a property out of the object, if it has it.</summary>
    internal void RemoveProperty(uint nameHash)
    {
        if (Property(nameHash) is not { } going) return;
        LayOut(Properties.Where(p => p != going).Select(p => (p, p.Value)).ToList());
    }

    /// <summary>The object's bytes made again from its header and these properties and values (padding zeroed).</summary>
    void LayOut(List<(ZoneProperty Property, byte[] Value)> properties)
    {
        var output = new MemoryStream();
        output.Write(bytes, 0, HeaderSize);
        foreach ((ZoneProperty property, byte[] value) in properties)
        {
            output.Write(BitConverter.GetBytes((ushort)property.Type));
            output.Write(BitConverter.GetBytes((ushort)value.Length));
            output.Write(BitConverter.GetBytes(property.NameHash));
            output.Write(value);
            output.Write(new byte[(4 - value.Length % 4) % 4]);
        }
        if (output.Length > ushort.MaxValue) throw new InvalidOperationException("an object can be at most 65,535 bytes");
        byte[] laidOut = output.ToArray();
        BitConverter.TryWriteBytes(laidOut.AsSpan(34), (ushort)laidOut.Length);
        BitConverter.TryWriteBytes(laidOut.AsSpan(52), (ushort)properties.Count);
        BitConverter.TryWriteBytes(laidOut.AsSpan(54), (ushort)(laidOut.Length - HeaderSize));
        bytes = laidOut;
        ReadProperties();
    }

    /// <summary>Whether the object stores a rotation (in "oP"), so it can be turned.</summary>
    public bool CanTurn => Property("oP") is { Size: 48 };

    /// <summary>
    /// Turns the object: writes the rotation (rows: right, up, forward) into "oP", and turns its
    /// bounding box with it: the old box's corners, turned the same way around the object's
    /// position, give the new box (a little larger than needed, never too small).
    /// </summary>
    public void TurnTo(Matrix4x4 rotation)
    {
        if (Property("oP") is not { Size: 48 } op) return;
        (Vector3 position, Matrix4x4 old) = Placement;
        Matrix4x4.Invert(old, out Matrix4x4 undoOld);
        Matrix4x4 change = undoOld * rotation;

        Vector3 min = BoundsMin, max = BoundsMax;
        Vector3 newMin = new(float.MaxValue), newMax = new(float.MinValue);
        for (int corner = 0; corner < 8; corner++)
        {
            var point = new Vector3((corner & 1) == 0 ? min.X : max.X, (corner & 2) == 0 ? min.Y : max.Y, (corner & 4) == 0 ? min.Z : max.Z);
            Vector3 turned = Vector3.Transform(point - position, change) + position;
            newMin = Vector3.Min(newMin, turned);
            newMax = Vector3.Max(newMax, turned);
        }

        byte[] value = op.Value;
        float[] rows = [rotation.M11, rotation.M12, rotation.M13, rotation.M21, rotation.M22, rotation.M23, rotation.M31, rotation.M32, rotation.M33];
        for (int i = 0; i < 9; i++) BitConverter.TryWriteBytes(value.AsSpan(12 + i * 4), rows[i]);
        op.SetValue(value);
        WriteVector(8, newMin);
        WriteVector(20, newMax);
    }

    /// <summary>
    /// Moves the object: its "oP" position (if it has one) and its bounding box, which moves with
    /// it. The rest of the zone stays as it is.
    /// </summary>
    public void MoveTo(Vector3 position)
    {
        Vector3 by = position - Placement.Position;
        if (Property("oP") is { Size: 48 } op)
        {
            byte[] value = op.Value;
            BitConverter.TryWriteBytes(value.AsSpan(0), position.X);
            BitConverter.TryWriteBytes(value.AsSpan(4), position.Y);
            BitConverter.TryWriteBytes(value.AsSpan(8), position.Z);
            op.SetValue(value);
        }
        WriteVector(8, BoundsMin + by);
        WriteVector(20, BoundsMax + by);
    }

    uint U32(int at) => BitConverter.ToUInt32(bytes, at);
    void SetU32(int at, uint value) => BitConverter.TryWriteBytes(bytes.AsSpan(at), value);

    Vector3 ReadVector(int offset) => new(
        BitConverter.ToSingle(bytes, offset), BitConverter.ToSingle(bytes, offset + 4), BitConverter.ToSingle(bytes, offset + 8));

    void WriteVector(int offset, Vector3 value)
    {
        BitConverter.TryWriteBytes(bytes.AsSpan(offset), value.X);
        BitConverter.TryWriteBytes(bytes.AsSpan(offset + 4), value.Y);
        BitConverter.TryWriteBytes(bytes.AsSpan(offset + 8), value.Z);
    }
}

/// <summary>One property of a zone object: its type, name (hash) and value.</summary>
public sealed class ZoneProperty
{
    public const int TextType = 4, ValueType = 5, ListType = 6;

    /// <summary>The longest text the editor lets a text property hold.</summary>
    public const int LongestText = 255;

    readonly ZoneObject owner;

    internal ZoneProperty(ZoneObject owner, int type, uint nameHash, int valueAt, int size)
    {
        this.owner = owner;
        Type = type;
        NameHash = nameHash;
        ValueAt = valueAt;
        Size = size;
    }

    public int Type { get; }
    public uint NameHash { get; }
    public int Size { get; private set; }

    /// <summary>Where its value starts in its object's bytes.</summary>
    internal int ValueAt { get; private set; }

    internal void MoveTo(int valueAt, int size)
    {
        ValueAt = valueAt;
        Size = size;
    }

    public byte[] Value => owner.Bytes.AsSpan(ValueAt, Size).ToArray();

    public string Text => Encoding.Latin1.GetString(Value).TrimEnd('\0');

    public float[] Floats() => Enumerable.Range(0, Size / 4).Select(i => BitConverter.ToSingle(owner.Bytes, ValueAt + i * 4)).ToArray();

    /// <summary>
    /// Changes the value. One of another size lays its object out again (the zone is laid out
    /// again when written): for longer text, and lists.
    /// </summary>
    public void SetValue(byte[] value)
    {
        if (value.Length == Size) value.CopyTo(owner.Bytes, ValueAt);
        else owner.Resize(this, value);
    }

    /// <summary>
    /// Changes a text value (at most <see cref="LongestText"/> characters). Text that fits in the
    /// space the old one took keeps it, the rest filled with NULs; longer text makes it larger.
    /// </summary>
    public void SetText(string text)
    {
        if (text.Length > LongestText) throw new ArgumentException($"the text can be at most {LongestText} characters");
        byte[] value = new byte[Math.Max(Size, text.Length + 1)];
        Encoding.Latin1.GetBytes(text).CopyTo(value, 0);
        SetValue(value);
    }
}
