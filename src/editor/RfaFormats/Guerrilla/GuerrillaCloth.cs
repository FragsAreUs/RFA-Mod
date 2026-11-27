using System.Text;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Guerrilla's cloth, for Armageddon: so far Mason's coat, whose tails swing as he moves and blow in the wind.
///
/// Both games move cloth with the same code of Volition's own (not Havok's): a cloth file (.sim_pc) gives a set of
/// nodes, each on a bone of the wearer's skeleton. Some are pinned to the body (anchors), the rest swing on links
/// between them, pulled by gravity and the wind, and kept out of the body by capsules on its bones. Each frame
/// the game moves the swinging nodes and sets their bones from them. The cloth files are in cloth_sim.vpp_pc,
/// each in a container of its own (x.sim_pc.str2_pc), and all of them are listed in its cloth_containers.asm_pc
/// and loaded at start-up. When an item or a person is made, the game looks a cloth up by the name of its mesh
/// (the file's own name, at +4). Guerrilla does that for every item and person. Armageddon does it only for an
/// item with the cloth_sim flag or a character with the "cloth sim" flag. Both read the weather's wind, which
/// the guerrilla_world mod blows as Guerrilla's in our worlds, so a ported cloth takes Guerrilla's wind with no
/// more work.
///
/// The file (version 1 in both games), from Guerrilla's cloth_sim_info:
///
///   +0x00  version (1); +0x04 the name (28 bytes); +0x20 passes; +0x24 air resistance; +0x28 and +0x2C the
///          wind's multiplier and constant; +0x30 gravity's multiplier; +0x34 how much of the wearer's motion
///          it takes; +0x38 and +0x3C two node counts (below); +0x40 links; +0x44 capsules; +0x48, +0x4C and
///          +0x50 where the nodes, links and capsules are (0 in the file: set at load)
///   +0x54  the nodes, 36 bytes each: bone (-1 none), parent node (-1 none), gravity link (the link to its
///          parent, by number; -1 none), a flag (below), which capsules it collides with (u32, a bit each), its
///          wind multiplier, its offset from its bone and its place on the body
///   then   the links, 16 bytes each: the two nodes (below), which capsules it collides with, its length and
///          how far it may stretch (a share of its length, in both games)
///   then   the capsules, 52 bytes each: bone, whether a capsule (else a sphere), radius, height, position,
///          axis, position from its bone
///
/// The two games' files differ in three ways (the rest is the same, record for record):
/// - The node counts. Guerrilla lists the anchors first: +0x38 is how many anchors, +0x3C how many swinging
///   nodes after them, and the node's flag byte is unused (0). Armageddon's files mix the anchors in: +0x38 is
///   all the nodes, +0x3C the anchors among them, and an anchor's flag byte is 1 (its cloth_sim_create,
///   0x96E7A0, sizes the nodes by +0x38 and the anchors' places by +0x3C, and copies the flag into each running
///   node, 0x96EB8D).
/// - A link's two nodes. Guerrilla's are 16 bits each (+0 and +2; its simulate reads them as words, 0x8078FD);
///   Armageddon's are a byte each (+0 and +1, the next two bytes unused; 0x97186B and every other reader). Read
///   Armageddon's way, Guerrilla's links all end at node 0 (the high byte of the first node), and the cloth
///   bunches up round that one anchor.
/// - The gravity link's sign. Guerrilla reads it as a signed byte (0x807B0B), so a link numbered past 127 (the
///   coat's hood, links 145-152) is "none" there; Armageddon reads it unsigned (0x971AD5) and would take it.
///   It's made -1 (none in both), as Guerrilla has it.
/// The capsules sit on the wearer's body (the wearer's bone matrices handed to each step; Guerrilla's
/// collider_rig), the nodes on the item's own skeleton. A cloth here is Guerrilla's file with those three put
/// Armageddon's way.
///
/// Its limits (Armageddon's, the same as Guerrilla's but the first): 64 cloth files loaded (0x96F6C1); 115
/// cloths running at once (0x96EC63); a 64 KB pool for the files (0xA46CC0, "Cloth Sim Mempool"), of which
/// Armageddon's own 25 take 34,788 bytes; a 256 KB pool for the cloths running.
///
/// It writes the containers as new files in overrides\build\pc\cache\guerrilla\cloth\ and its own cloth_containers.asm_pc over
/// Armageddon's (overrides\build\pc\cache\cloth_sim.vpp_pc\): Armageddon's list with ours after it.
/// </summary>
public static class GuerrillaCloth
{
    public const string ListArchive = "cloth_sim.vpp_pc", ListName = "cloth_containers.asm_pc";

    /// <summary>The container list written, over Armageddon's own.</summary>
    public const string AssetList = @"build\pc\cache\cloth_sim.vpp_pc\cloth_containers.asm_pc";

    /// <summary>The cloths ported so far, by the name of the mesh that wears it (the cloth file has its name).</summary>
    public static readonly IReadOnlyList<string> Ported = ["mason_coat"];

    /// <summary>The props wearing one of them: their items are given Armageddon's cloth_sim flag (GuerrillaPeople).</summary>
    public static bool Worn(string item) => WornBy.Contains(item);

    static readonly HashSet<string> WornBy = new(StringComparer.OrdinalIgnoreCase) { "Mason_Coat" };

    /// <summary>How many cloth files the game loads at most (0x96F6C1: cmp 40h).</summary>
    public const int MostFiles = 64;

    /// <summary>The pool the game keeps the cloth files in (0xA46CC0: 0x10000 bytes).</summary>
    public const int PoolBytes = 0x10000;

    const int HeaderSize = 0x54, NodeSize = 36, LinkSize = 16, CapsuleSize = 52;
    const byte None = 0xFF;

    /// <summary>A cloth file as read (see the class's summary).</summary>
    public sealed record Cloth(string Name, int Passes, float WindMultiplier, float WindConstant, int Nodes, int Anchors, int Links, int Capsules,
                               sbyte[] NodeBones, byte[] Parents, byte[] GravityLinks, byte[] Flags, (int A, int B)[] LinkNodes, int[] CapsuleBones);

    /// <summary>
    /// A cloth file, read as Guerrilla's (<paramref name="guerrilla"/>: the anchors counted first, then the
    /// swinging nodes; a link's nodes 16 bits each; a gravity link past 127 none) or as Armageddon's (all the
    /// nodes, then the anchors among them; a link's nodes a byte each; a gravity link unsigned). Throws if its
    /// size isn't what its counts make it.
    /// </summary>
    public static Cloth Read(byte[] data, bool guerrilla)
    {
        if (data.Length < HeaderSize) throw new InvalidDataException("too short for a cloth file");
        int version = BitConverter.ToInt32(data, 0);
        if (version != 1) throw new InvalidDataException($"a cloth file of version {version}, not 1");
        int first = BitConverter.ToInt32(data, 0x38), second = BitConverter.ToInt32(data, 0x3C);
        int links = BitConverter.ToInt32(data, 0x40), capsules = BitConverter.ToInt32(data, 0x44);
        int nodes = guerrilla ? first + second : first, anchors = guerrilla ? first : second;
        if (nodes is < 0 or > 255 || anchors < 0 || anchors > nodes || links is < 0 or > 0xFFFF || capsules is < 0 or > 32)
            throw new InvalidDataException($"a cloth's counts out of range ({nodes} nodes, {anchors} anchors, {links} links, {capsules} capsules)");
        long size = HeaderSize + (long)nodes * NodeSize + (long)links * LinkSize + (long)capsules * CapsuleSize;
        if (size != data.Length) throw new InvalidDataException($"a cloth of {nodes} nodes, {links} links and {capsules} capsules is {size} bytes, not {data.Length}");
        int end = Array.IndexOf(data, (byte)0, 4, 28);
        string name = Encoding.Latin1.GetString(data, 4, (end < 0 ? 32 : end) - 4);
        var bones = new sbyte[nodes];
        var parents = new byte[nodes];
        var gravity = new byte[nodes];
        var flags = new byte[nodes];
        for (int i = 0; i < nodes; i++)
        {
            int at = HeaderSize + i * NodeSize;
            bones[i] = (sbyte)data[at];
            parents[i] = data[at + 1];
            gravity[i] = guerrilla && (sbyte)data[at + 2] < 0 ? None : data[at + 2];
            flags[i] = data[at + 3];
        }
        int linksAt = HeaderSize + nodes * NodeSize, capsulesAt = linksAt + links * LinkSize;
        var linkNodes = new (int, int)[links];
        for (int i = 0; i < links; i++)
        {
            int at = linksAt + i * LinkSize;
            linkNodes[i] = guerrilla ? (BitConverter.ToUInt16(data, at), BitConverter.ToUInt16(data, at + 2)) : (data[at], data[at + 1]);
        }
        var capsuleBones = new int[capsules];
        for (int i = 0; i < capsules; i++) capsuleBones[i] = BitConverter.ToInt32(data, capsulesAt + i * CapsuleSize);
        return new Cloth(name, BitConverter.ToInt32(data, 0x20), BitConverter.ToSingle(data, 0x28), BitConverter.ToSingle(data, 0x2C),
                         nodes, anchors, links, capsules, bones, parents, gravity, flags, linkNodes, capsuleBones);
    }

    /// <summary>
    /// What is wrong with a cloth file in Armageddon's way, if anything (null if nothing): its anchors flagged and
    /// counted; the parents, links and gravity links naming nodes and links it has; each swinging node linked to
    /// its parent (as both games' tools make them); and, given the bone counts of the item's own skeleton and of
    /// the wearer's body, every node's and capsule's bone one of their bones.
    /// </summary>
    public static string? Problem(Cloth cloth, int? nodeBones = null, int? capsuleBones = null)
    {
        int flagged = cloth.Flags.Count(f => f == 1), unparented = cloth.Parents.Count(p => p == None);
        if (flagged != cloth.Anchors || unparented != cloth.Anchors)
            return $"{cloth.Anchors} anchors counted, {flagged} flagged as anchors, {unparented} nodes without a parent";
        var linked = new HashSet<(int, int)>();
        for (int i = 0; i < cloth.Links; i++)
        {
            var (a, b) = cloth.LinkNodes[i];
            if (a >= cloth.Nodes || b >= cloth.Nodes) return $"link {i} joins nodes {a} and {b}, past its {cloth.Nodes}";
            if (a == b) return $"link {i} joins node {a} to itself";
            linked.Add((Math.Min(a, b), Math.Max(a, b)));
        }
        for (int i = 0; i < cloth.Nodes; i++)
        {
            int parent = cloth.Parents[i];
            if ((cloth.Flags[i] == 1) != (parent == None)) return $"node {i}: flagged {cloth.Flags[i]}, its parent {parent}";
            if (parent != None && parent >= cloth.Nodes) return $"node {i}'s parent {parent} is past its {cloth.Nodes} nodes";
            if (parent != None && !linked.Contains((Math.Min(i, parent), Math.Max(i, parent)))) return $"node {i} has no link to its parent {parent}";
            if (parent != None && cloth.GravityLinks[i] != None && cloth.GravityLinks[i] >= cloth.Links)
                return $"node {i}'s gravity link {cloth.GravityLinks[i]} is past its {cloth.Links} links";
            if (nodeBones is int count && cloth.NodeBones[i] >= count) return $"node {i}'s bone {cloth.NodeBones[i]} is past its skeleton's {count} bones";
        }
        if (capsuleBones is int all)
            for (int i = 0; i < cloth.Capsules; i++)
                if (cloth.CapsuleBones[i] >= all) return $"capsule {i}'s bone {cloth.CapsuleBones[i]} is past the wearer's {all} bones";
        return null;
    }

    /// <summary>
    /// Guerrilla's cloth file in Armageddon's way (see the class's summary): its node counts as Armageddon counts
    /// them and its anchors flagged, each link's two nodes a byte each, and a gravity link past 127 made none, as
    /// Guerrilla reads it; the rest as it is. (An anchor's wind multiplier, which Guerrilla's tool left as its
    /// 0xCC fill in mason_coat, is made 1, Armageddon's own anchors' value: an anchor doesn't move with the wind.)
    /// </summary>
    public static byte[] Convert(byte[] guerrilla)
    {
        var cloth = Read(guerrilla, guerrilla: true);
        for (int i = 0; i < cloth.Nodes; i++)
            if ((i < cloth.Anchors) != (cloth.Parents[i] == None))
                throw new InvalidDataException($"{cloth.Name}: node {i} {(i < cloth.Anchors ? "is among the anchors but has a parent" : "has no parent but comes after the anchors")}");
        byte[] ours = (byte[])guerrilla.Clone();
        BitConverter.TryWriteBytes(ours.AsSpan(0x38), cloth.Nodes);
        BitConverter.TryWriteBytes(ours.AsSpan(0x3C), cloth.Anchors);
        for (int i = 0; i < cloth.Nodes; i++)
        {
            int at = HeaderSize + i * NodeSize;
            ours[at + 2] = cloth.GravityLinks[i];
            ours[at + 3] = (byte)(i < cloth.Anchors ? 1 : 0);
            if (i < cloth.Anchors && BitConverter.ToUInt32(ours, at + 8) == 0xCCCCCCCC) BitConverter.TryWriteBytes(ours.AsSpan(at + 8), 1f);
        }
        int linksAt = HeaderSize + cloth.Nodes * NodeSize;
        for (int i = 0; i < cloth.Links; i++)
        {
            var (a, b) = cloth.LinkNodes[i];
            if (a >= None || b >= None) throw new InvalidDataException($"{cloth.Name}: link {i} joins nodes {a} and {b}; Armageddon names a link's nodes in a byte (up to 254)");
            int at = linksAt + i * LinkSize;
            ours[at] = (byte)a;
            ours[at + 1] = (byte)b;
            ours[at + 2] = ours[at + 3] = 0;
        }
        if (Problem(Read(ours, guerrilla: false)) is { } problem) throw new InvalidDataException($"{cloth.Name} converted: {problem}");
        return ours;
    }

    /// <summary>
    /// The cloths in <see cref="Ported"/>, from Guerrilla's cloth_sim.vpp_pc, converted and each packed in a
    /// container of Armageddon's own kind, and Armageddon's container list with them added. Returns what it did.
    /// Throws past the game's limits (64 files, the 64 KB pool): a raise is a choice to make first.
    /// </summary>
    public static IEnumerable<string> Build(string guerrilla, string game, string overrides)
    {
        var gameArchive = Packfile.Open(Path.Combine(game, "build", "pc", "cache", ListArchive));
        var list = AsmFile.Read(gameArchive.Read(gameArchive.Find(ListName) ?? throw new InvalidDataException($"no {ListName} in Armageddon's {ListArchive}")));
        int theirs = list.Containers.Count;
        var rfgArchive = Packfile.Open(Path.Combine(guerrilla, "build", "pc", "cache", ListArchive));
        string listFile = Path.Combine(overrides, AssetList);
        Directory.CreateDirectory(Path.GetDirectoryName(listFile)!);
        var lines = new List<string>();
        foreach (string mesh in Ported)
        {
            string file = mesh + ".sim_pc", containerName = file + ".str2_pc";
            if (list.Containers.Any(c => c.Name.Equals(file, StringComparison.OrdinalIgnoreCase)) || gameArchive.Find(containerName) != null)
            {
                lines.Add($"Cloth: {mesh}: Armageddon has a cloth of that name; Guerrilla's is left out");
                continue;
            }
            var source = rfgArchive.Find(containerName) ?? throw new InvalidDataException($"no {containerName} in Guerrilla's {ListArchive}");
            var container = rfgArchive.OpenContainer(source, containerName);
            byte[] ours = Convert(container.Read(container.Find(file) ?? throw new InvalidDataException($"no {file} in Guerrilla's {containerName}")));
            byte[] packed = Packfile.CreateContainer([(file, ours)]);
            File.WriteAllBytes(OverrideLayout.NewFile(overrides, OverrideLayout.Cloth, containerName), packed);
            list.Containers.Add(Entry(file, packed, ours));
            var cloth = Read(ours, guerrilla: false);
            lines.Add($"Cloth: {mesh}: {cloth.Nodes} nodes ({cloth.Anchors} pinned to the body), {cloth.Links} links, {cloth.Capsules} capsules, " +
                      $"{cloth.Passes} passes, wind × {cloth.WindMultiplier:0.###} + {cloth.WindConstant:0.###} ({ours.Length:N0} bytes)");
        }
        int files = list.Containers.Count;
        long bytes = list.Containers.Sum(c => c.Sizes.Sum(s => (long)s));
        if (files > MostFiles) throw new InvalidDataException($"{files} cloth files, past the game's {MostFiles}: raising it is a choice to make first");
        if (bytes > PoolBytes) throw new InvalidDataException($"the cloth files take {bytes:N0} bytes, past the game's pool of {PoolBytes:N0}: raising it is a choice to make first");
        File.WriteAllBytes(listFile, list.ToBytes());
        lines.Add($"Cloth: {files} cloth files listed (Armageddon's {theirs} and {files - theirs} of Guerrilla's), of the game's {MostFiles}; " +
                  $"{bytes:N0} of its {PoolBytes:N0} bytes for them");
        return lines;
    }

    /// <summary>A cloth container's entry in the list, as Armageddon's own: type 8, its one file of type 10 (flags 2, no data file).</summary>
    static AsmContainer Entry(string file, byte[] container, byte[] cloth)
    {
        var c = Packfile.FromBytes(file + ".str2_pc", container);
        var entry = new AsmContainer { Name = file, Type = 8, Flags = 0x80, DataOffset = (uint)c.DataStart, CompressedSize = c.CompressedDataSize };
        entry.Sizes.Add(cloth.Length);
        entry.Primitives.Add(new AsmPrimitive { Name = file, Type = 10, Allocator = 0, Flags = 2, SplitIndex = 0, HeaderSize = cloth.Length, DataSize = 0 });
        return entry;
    }

    /// <summary>Takes away what <see cref="Build"/> wrote: our containers (the listed ones Armageddon's archive hasn't) and the list. Returns what it did.</summary>
    public static IEnumerable<string> Remove(string game, string overrides)
    {
        string listFile = Path.Combine(overrides, AssetList);
        if (!File.Exists(listFile)) yield break;
        var gameArchive = Packfile.Open(Path.Combine(game, "build", "pc", "cache", ListArchive));
        foreach (var c in AsmFile.Read(File.ReadAllBytes(listFile)).Containers.Where(c => gameArchive.Find(c.FileName) == null))
            foreach (string file in OverrideLayout.Delete(overrides, c.FileName)) yield return $"Deleted {file}";
        File.Delete(listFile);
        yield return $"Deleted {listFile}";
    }
}
