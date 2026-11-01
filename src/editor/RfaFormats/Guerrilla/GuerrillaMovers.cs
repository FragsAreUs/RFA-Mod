using System.Text;
using RfaFormats.Zones;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's destructible objects in a zone (rfg_mover: buildings and what breaks in
/// pieces; general_mover: loose props) as Armageddon's zone objects, attached to one another and
/// linked as in Guerrilla's zone. Each is laid out as one of Armageddon's own movers of its class (a
/// template: its header and properties), with Guerrilla's chunk file (always the .cchk_pc name; a few
/// of Guerrilla's say .rfgchunk_pc), uid, placement (oP), mtype or gm_flags,
/// box, number, handle (already starting with the zone's place), and world_anchors (the pieces held to
/// the ground: the same list of u16 piece numbers in both games; Armageddon has an 8-byte anchor_data,
/// zeros, beside every one); Guerrilla's Props and gameplay_props names where Armageddon's tables have
/// them (they hold most of Guerrilla's), else the template's. The destruction checksum is the chunk
/// file's own (its header's 0x24), as in all of Armageddon's zones: Guerrilla's zones have it too,
/// but for two smokestacks (campaign targets) an older one than their files.
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
    const int GuerrillaLinkSize = 20, HeaderSize = 56;
    const uint None = 0xFFFFFFFF;

    /// <summary>The movers as Armageddon's objects (handles not in <paramref name="taken"/>, which gains theirs), and the extra block's links for them.</summary>
    /// <param name="placed">The movers to place, in the zone's order.</param>
    /// <param name="all">Every object of their Guerrilla zone, by handle (to follow attachments through).</param>
    /// <param name="templates">Armageddon's mover to lay each class out as, by class hash.</param>
    /// <param name="isProps">Whether Armageddon's level_objects table has a Props name.</param>
    /// <param name="isGameplayProps">Whether Armageddon's gameplay_properties table has a name.</param>
    /// <param name="checksumOf">A chunk file's destruction checksum (its header's 0x24), by its name without extension.</param>
    public static (List<ZoneObject> Objects, byte[] Links) Convert(IReadOnlyList<GuerrillaZoneObject> placed, IReadOnlyDictionary<uint, GuerrillaZoneObject> all,
        IReadOnlyDictionary<uint, ZoneObject> templates, Func<string, bool> isProps, Func<string, bool> isGameplayProps, Func<string, uint> checksumOf, ISet<uint> taken)
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

        var objects = new List<ZoneObject>();
        foreach (var o in placed)
        {
            var template = templates[o.ClassHash];
            byte[] header = template.Snapshot()[..HeaderSize];
            void Put(int at, uint v) => BitConverter.TryWriteBytes(header.AsSpan(at), v);
            Put(4, handleOf[o.Handle]);
            float[] box = [o.BoundsMin.X, o.BoundsMin.Y, o.BoundsMin.Z, o.BoundsMax.X, o.BoundsMax.Y, o.BoundsMax.Z];
            for (int k = 0; k < 6; k++) BitConverter.TryWriteBytes(header.AsSpan(8 + k * 4), box[k]);
            uint parent = ParentOf(o);
            Put(36, parent == None ? None : handleOf[parent]);
            Put(40, nextSibling.TryGetValue(o.Handle, out uint s) ? handleOf[s] : None);
            Put(44, childrenOf.TryGetValue(o.Handle, out var kids) && kids.Count > 0 ? handleOf[kids[0]] : None);
            Put(48, o.Number);

            string chunk = Path.GetFileNameWithoutExtension(o.Text("chunk_name") ?? "");
            byte[]? anchors = o.Value("world_anchors");
            var properties = new List<(int Type, uint Name, byte[] Value)>();
            static byte[] Text(string s) => [.. Encoding.ASCII.GetBytes(s), 0];
            foreach (var t in template.Properties)
            {
                string? name = Name(t.NameHash);
                byte[]? value = name switch
                {
                    "chunk_name" => Text(chunk + ".cchk_pc"),
                    "dest_checksum" => BitConverter.GetBytes(checksumOf(chunk)),
                    "uid" or "oP" or "mtype" or "gm_flags" => o.Value(name) is { } v && v.Length == t.Size ? v : t.Value,
                    "Props" => Text(o.Text("Props") is { } props && isProps(props) ? props : "Default"),
                    "gameplay_props" => o.Text("gameplay_props") is { } g && isGameplayProps(g) ? Text(g) : t.Value,
                    "world_anchors" => anchors,
                    "anchor_data" => anchors == null ? null : new byte[8],
                    "dynamic_links" => null,  // written below
                    _ => t.Value,
                };
                if (value == null) continue;
                properties.Add((t.Type, t.NameHash, value));
                if (name == "chunk_name" && !template.Properties.Any(q => q.NameHash == NameHashes.PropertyHash("world_anchors")) && anchors != null)
                {
                    properties.Add((ZoneProperty.ListType, NameHashes.PropertyHash("world_anchors"), anchors));
                    properties.Add((ZoneProperty.ListType, NameHashes.PropertyHash("anchor_data"), new byte[8]));
                }
            }
            if (linksOf[o.Handle] is { Count: > 0 } mine)
            {
                byte[] list = new byte[(mine.Count + 1) * 2];
                for (int i = 0; i < mine.Count; i++) BitConverter.TryWriteBytes(list.AsSpan(i * 2), (ushort)mine[i]);
                int after = properties.FindIndex(q => q.Name == NameHashes.PropertyHash("world_anchors"));
                if (after < 0) after = properties.FindIndex(q => q.Name == NameHashes.PropertyHash("chunk_name"));
                properties.Insert(after + 1, (ZoneProperty.ListType, NameHashes.PropertyHash("dynamic_links"), list));
            }

            var bytes = new MemoryStream();
            bytes.Write(header);
            foreach (var (type, name, value) in properties)
            {
                bytes.Write(BitConverter.GetBytes((ushort)type));
                bytes.Write(BitConverter.GetBytes((ushort)value.Length));
                bytes.Write(BitConverter.GetBytes(name));
                bytes.Write(value);
                bytes.Write(new byte[(4 - value.Length % 4) % 4]);
            }
            byte[] laidOut = bytes.ToArray();
            BitConverter.TryWriteBytes(laidOut.AsSpan(34), (ushort)laidOut.Length);
            BitConverter.TryWriteBytes(laidOut.AsSpan(52), (ushort)properties.Count);
            BitConverter.TryWriteBytes(laidOut.AsSpan(54), (ushort)(laidOut.Length - HeaderSize));
            var obj = ZoneFile.Copy(template, handleOf[o.Handle]);
            obj.Restore(laidOut);
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

    static readonly Dictionary<uint, string> Names = new[]
    {
        "chunk_name", "dest_checksum", "uid", "oP", "mtype", "gm_flags", "Props", "gameplay_props", "world_anchors", "anchor_data", "dynamic_links",
    }.ToDictionary(NameHashes.PropertyHash, n => n);

    static string? Name(uint hash) => Names.GetValueOrDefault(hash);
}
