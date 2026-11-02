using System.Text;
using RfaFormats.Zones;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's destructible objects in a zone (rfg_mover: buildings and what breaks in
/// pieces; general_mover: loose props) as Armageddon's zone objects, attached to one another and
/// linked as in Guerrilla's zone. Each is written as Volition's tools wrote Armageddon's own movers:
/// the schema's obj_chunk_ref with its model's destruction data, every property in the order the
/// tools used for its class (all of Armageddon's 58,754 rfg_movers and 22,872 general_movers have
/// the same order, with the ones they lack left out). The values:
///   - Guerrilla's: the chunk file (always the .cchk_pc name; a few of Guerrilla's say
///     .rfgchunk_pc), uid, placement (oP), mtype or gm_flags, box, number, handle (already starting
///     with the zone's place), the header's 0x10 (streaming_fixed: the game keeps the object among
///     Havok's fixed ones; both games' tools set it on movers attached to nothing), world_anchors
///     (the pieces held to the ground: the same list of u16 piece numbers in both games; an
///     rfg_mover with them also has an 8-byte anchor_data, zeros, as Armageddon's do),
///     game_destroyed_pct (how much of a building must fall for it to count as destroyed: the
///     campaign's targets), building_type (Bridge, Communications, Force_Field: flags both games
///     have), display_name (the name scripts find an object by, e.g. TRGT_EDF_PILLBOX: Armageddon's
///     object loader reads it from any object, as Guerrilla's does), Guerrilla's Props name where
///     Armageddon's table has it (it holds most of Guerrilla's), and its gameplay_props name, or
///     where Armageddon's table lacks it the entry the caller finds closest
///   - the schema's defaults: Props and gameplay_props otherwise ("Default"), and the switches
///     Armageddon added (no_magnet_gun, suppress_salvage, no_repair, ignore_shape_cutters,
///     mp_unimportant: all off)
///   - lod_props "None" (below), and the destruction checksum the chunk file's own (its header's
///     0x24), as in all of Armageddon's zones: Guerrilla's zones have it too, but for two
///     smokestacks (campaign targets) an older one than their files.
/// Guerrilla's chunk_flags (dynamic_link, world_anchor) aren't written: Armageddon's chunk flags are
/// other ones (casts_shadows, regenerate, ...), so it would read them as none, and it finds the links
/// and anchors in their own lists.
///
/// The destruction links differ in where they are kept. Guerrilla's are in the objects:
/// "dynamic_links" holds each link that names the object, 20 bytes a link (two u16 piece numbers,
/// flags, the two objects' handles, a number). Armageddon's are in the zone's extra block, 40 bytes a
/// link (the same five, then 0xFFFFFFFF twice before and after the number, and last the link's own
/// number), and "dynamic_links" lists the numbers of the links that name the object, then a 0 (u16
/// each). Links to objects not placed are left out, and attachments to them undone.
/// </summary>
public static class GuerrillaMovers
{
    const int GuerrillaLinkSize = 20;
    const uint None = 0xFFFFFFFF;
    const ushort StreamingFixed = 0x10;

    /// <summary>The order Volition's tools wrote each class's properties in (those an object lacks left out).</summary>
    static readonly string[] RfgMoverOrder =
    [
        "no_magnet_gun", "dest_checksum", "chunk_name", "game_destroyed_pct", "world_anchors", "oP", "gameplay_props", "building_type", "uid",
        "suppress_salvage", "no_repair", "mtype", "dynamic_links", "ignore_shape_cutters", "mp_unimportant", "lod_props", "anchor_data", "Props",
    ];
    static readonly string[] GeneralMoverOrder =
    [
        "no_magnet_gun", "dest_checksum", "chunk_name", "world_anchors", "gameplay_props", "oP", "building_type", "uid",
        "suppress_salvage", "no_repair", "dynamic_links", "ignore_shape_cutters", "gm_flags", "mp_unimportant", "lod_props", "Props",
    ];

    /// <summary>The movers as Armageddon's objects (handles not in <paramref name="taken"/>, which gains theirs), and the extra block's links for them.</summary>
    /// <param name="placed">The movers to place, in the zone's order (written in it, but each parent before its children).</param>
    /// <param name="all">Every object of their Guerrilla zone, by handle (to follow attachments through).</param>
    /// <param name="schema">The game's schema (obj_chunk_ref's defaults and building types).</param>
    /// <param name="isProps">Whether Armageddon's level_objects table has a Props name.</param>
    /// <param name="gameplayPropsOf">Armageddon's gameplay_properties entry for one of Guerrilla's names (its own, or the closest), or null.</param>
    /// <param name="checksumOf">A chunk file's destruction checksum (its header's 0x24), by its name without extension.</param>
    public static (List<ZoneObject> Objects, byte[] Links) Convert(IReadOnlyList<GuerrillaZoneObject> placed, IReadOnlyDictionary<uint, GuerrillaZoneObject> all,
        ZoneSchema schema, Func<string, bool> isProps, Func<string, string?> gameplayPropsOf, Func<string, uint> checksumOf, ISet<uint> taken)
    {
        // Handles: Guerrilla's, unless taken.
        var handleOf = new Dictionary<uint, uint>();
        foreach (var o in placed)
        {
            uint handle = o.Handle;
            while (!taken.Add(handle)) handle = (handle & 0xFFFF0000) | ((handle + 1) & 0xFFFF);
            handleOf[o.Handle] = handle;
        }

        // The links between placed objects, each once, numbered in the order they are first named.
        var links = new List<byte[]>();
        var linkNumber = new Dictionary<string, int>();
        var linksOf = placed.ToDictionary(o => o.Handle, _ => new List<int>());
        foreach (var o in placed)
        {
            if (o.Value("dynamic_links") is not { } list) continue;
            for (int at = 0; at + GuerrillaLinkSize <= list.Length; at += GuerrillaLinkSize)
            {
                uint a = BitConverter.ToUInt32(list, at + 8), b = BitConverter.ToUInt32(list, at + 12);
                if (!handleOf.ContainsKey(a) || !handleOf.ContainsKey(b)) continue;
                string key = System.Convert.ToHexString(list, at, GuerrillaLinkSize);
                if (!linkNumber.TryGetValue(key, out int n))
                {
                    n = links.Count;
                    linkNumber[key] = n;
                    var link = new byte[ZoneFile.DestructionLinkSize];
                    Array.Fill(link, (byte)0xFF, 16, 8);
                    Array.Fill(link, (byte)0xFF, 28, 8);
                    Array.Copy(list, at, link, 0, 8);                       // the two pieces, flags
                    BitConverter.TryWriteBytes(link.AsSpan(8), handleOf[a]);
                    BitConverter.TryWriteBytes(link.AsSpan(12), handleOf[b]);
                    Array.Copy(list, at + 16, link, 24, 4);                 // the number
                    BitConverter.TryWriteBytes(link.AsSpan(36), n);         // its own number
                    links.Add(link);
                    linksOf[a].Add(n);
                    if (b != a) linksOf[b].Add(n);
                }
            }
        }

        // Attachments: to a placed parent only; its placed children in the order of Guerrilla's chain
        // (first child, then each one's sibling), any the chain misses after them.
        uint ParentOf(GuerrillaZoneObject o) => handleOf.ContainsKey(o.Parent) ? o.Parent : None;
        var childrenOf = new Dictionary<uint, List<uint>>();
        foreach (var o in placed)
        {
            var kids = new List<uint>();
            uint c = o.Child;
            for (int steps = 0; c != None && steps <= all.Count && all.TryGetValue(c, out var child); steps++, c = child.Sibling)
                if (handleOf.ContainsKey(c) && child.Parent == o.Handle) kids.Add(c);
            childrenOf[o.Handle] = kids;
        }
        foreach (var o in placed)
        {
            uint parent = ParentOf(o);
            if (parent != None && !childrenOf[parent].Contains(o.Handle)) childrenOf[parent].Add(o.Handle);
        }
        var nextSibling = new Dictionary<uint, uint>();
        foreach (var kids in childrenOf.Values)
            for (int i = 0; i + 1 < kids.Count; i++) nextSibling[kids[i]] = kids[i + 1];

        // Written each parent before its children. Armageddon makes a zone's objects about 500 at a
        // time and ties each one to its parent as it adds it, finding the parent only among those
        // made so far; Guerrilla's files mostly list children first, and its general_movers late, so
        // pipes on a substation or reactor core lost their parent ("Invalidating parent handle ...").
        var indexOf = new Dictionary<uint, int>();
        for (int i = 0; i < placed.Count; i++) indexOf.TryAdd(placed[i].Handle, i);
        var order = new List<GuerrillaZoneObject>();
        var visited = new HashSet<int>();
        void Visit(int i)
        {
            if (!visited.Add(i)) return;
            if (ParentOf(placed[i]) is var parent && parent != None && indexOf.TryGetValue(parent, out int p)) Visit(p);
            order.Add(placed[i]);
        }
        for (int i = 0; i < placed.Count; i++) Visit(i);

        var chunkRef = schema.Class("obj_chunk_ref") ?? throw new InvalidDataException("the schema has no obj_chunk_ref");
        byte[] Default(string name) => chunkRef.Property(name)?.DefaultValue() ?? throw new InvalidDataException($"the schema has no default for {name}");
        var buildingTypes = new HashSet<string>(chunkRef.Property("building_type")?.FlagNames ?? [], StringComparer.OrdinalIgnoreCase);
        uint rfgMover = NameHashes.ClassHash("rfg_mover");
        var objects = new List<ZoneObject>();
        foreach (var o in order)
        {
            string chunk = Path.GetFileNameWithoutExtension(o.Text("chunk_name") ?? "");
            byte[]? anchors = o.Value("world_anchors");
            byte[]? linkList = null;
            if (linksOf[o.Handle] is { Count: > 0 } mine)
            {
                linkList = new byte[(mine.Count + 1) * 2];
                for (int i = 0; i < mine.Count; i++) BitConverter.TryWriteBytes(linkList.AsSpan(i * 2), (ushort)mine[i]);
            }
            string[] types = [.. SchemaProperty.FlagsIn(o.Text("building_type")).Where(buildingTypes.Contains)];
            byte[]? Value(string name) => name switch
            {
                "chunk_name" => Text(chunk + ".cchk_pc"),
                "dest_checksum" => BitConverter.GetBytes(checksumOf(chunk)),
                "oP" => o.Value("oP") is { Length: 48 } placement ? placement : Unturned((o.BoundsMin + o.BoundsMax) / 2),
                "uid" or "mtype" or "gm_flags" => o.Value(name) is { Length: 4 } v ? v : new byte[4],
                // (Left out at the schema's default, -1: the game's own.)
                "game_destroyed_pct" => o.Value(name) is { Length: 4 } pct && BitConverter.ToSingle(pct) >= 0 ? pct : null,
                "world_anchors" => anchors,
                "anchor_data" => anchors == null ? null : new byte[8],
                "dynamic_links" => linkList,
                "Props" => Text(o.Text("Props") is { } props && isProps(props) ? props : null) ?? Default("Props"),
                "gameplay_props" => Text(o.Text("gameplay_props") is { } g ? gameplayPropsOf(g) : null) ?? Default("gameplay_props"),
                // (Left out with none, as the tools did.)
                "building_type" => types.Length > 0 ? Text(SchemaProperty.FlagsText(types)) : null,
                // Armageddon's fade-out class (lod_properties.xtbl): Guerrilla's objects have none and
                // don't fade by size; a clutter class (2_clutter_small, as Armageddon's crates and
                // props have) faded every crate and prop out from 25 m.
                "lod_props" => Text("None"),
                _ => Default(name),  // Armageddon's switches
            };
            var properties = new List<NewProperty>();
            foreach (string name in o.ClassHash == rfgMover ? RfgMoverOrder : GeneralMoverOrder)
                if (Value(name) is { } value) properties.Add(new NewProperty(TypeOf(name), NameHashes.PropertyHash(name), value));
            // Armageddon's tools never named a mover; the game finds properties by name, so it goes last.
            if (o.Text("display_name") is { Length: > 0 } displayName)
                properties.Add(new NewProperty(ZoneProperty.TextType, NameHashes.PropertyHash("display_name"), Text(displayName)!));

            var obj = ZoneObject.Create(o.ClassHash, handleOf[o.Handle], o.Number, o.BoundsMin, o.BoundsMax, properties, (ushort)(o.Flags & StreamingFixed));
            uint parent = ParentOf(o);
            obj.Parent = parent == None ? None : handleOf[parent];
            obj.Sibling = nextSibling.TryGetValue(o.Handle, out uint s) ? handleOf[s] : None;
            obj.Child = childrenOf.TryGetValue(o.Handle, out var kids) && kids.Count > 0 ? handleOf[kids[0]] : None;
            objects.Add(obj);
        }
        return (objects, [.. links.SelectMany(l => l)]);
    }

    /// <summary>A zone file (with no links of its own) with these links as its extra block.</summary>
    public static byte[] WithLinks(byte[] zone, byte[] links)
    {
        if (BitConverter.ToInt32(zone, 0x18) != 0) throw new InvalidOperationException("the zone has links of its own");
        byte[] result = [.. zone, .. links];
        BitConverter.TryWriteBytes(result.AsSpan(0x18), links.Length);
        return result;
    }

    /// <summary>A property's type in zone files: text, a list (of u16s), or a value.</summary>
    static int TypeOf(string name) => name switch
    {
        "chunk_name" or "Props" or "gameplay_props" or "lod_props" or "building_type" => ZoneProperty.TextType,
        "world_anchors" or "anchor_data" or "dynamic_links" => ZoneProperty.ListType,
        _ => ZoneProperty.ValueType,
    };

    /// <summary>Text as zone files store it (then a NUL), or null for none.</summary>
    static byte[]? Text(string? text) => text == null ? null : [.. Encoding.Latin1.GetBytes(text), 0];

    /// <summary>A placement (oP) at a point, unturned.</summary>
    static byte[] Unturned(System.Numerics.Vector3 at) =>
        [.. new[] { at.X, at.Y, at.Z, 1, 0, 0, 0, 1, 0, 0, 0, 1 }.SelectMany(BitConverter.GetBytes)];
}
