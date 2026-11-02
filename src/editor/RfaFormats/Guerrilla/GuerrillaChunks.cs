using RfaFormats.Meshes;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's destructible objects (.cchk_pc version 55: its buildings, fences, crates
/// and props) written as Armageddon's (version 62). The g-file (vertices and indices) stays as it is.
/// The c-file is rewritten section by section, in the same order and with the same alignments (in
/// brackets), every section but a few as it was:
///   header (0x200 bytes)        the same, but for the version and the g-file's size (Guerrilla's at
///                               0x1B4, Armageddon's at 0x1BC); the sections' offsets and sizes are
///                               set to where they land
///   mesh block (at 0x200)       version 6 → 9 (<see cref="MeshBlockConversion"/>)
///   texture names (16)          their size, then the names the materials use (renamed if asked)
///   materials (16)              a run-time word, the count, a run-time word each; from a multiple of
///                               16 a 0 each; from a multiple of 16 the material blocks, each rebuilt
///                               for one of Armageddon's shaders (<see cref="GuerrillaMaterials"/>)
///   general objects (128)       88 → 100 bytes each (12 run-time bytes more), then their names;
///                               header 0x14 is the size from 0x200 to their end
///   destruction data (128)      the same, but each link's last half-word (flags: Armageddon's 0x10 in
///                               98% of its links, Guerrilla's always 0) and each piece's run-time
///                               state (36 → 40 bytes: +0 −1 → 0, +24 → 0, +34 0xFF → 0, four 0s
///                               more), with the offsets in that run-time block moved on to match
///   collision (64)              Havok 5.5 → 2010 (<see cref="ChunkCollision"/>; a stitch piece's one
///                               triangle mesh under a tree written from nothing: <see cref="PieceCollision"/>)
///   script points (64)          the same, 116 bytes each
///   object ids (64)             520 → 808 bytes: uid, name, number, breakable, snap point count
///                               (40 bytes), then room for 16 snap points (48 bytes each: rotation,
///                               position) where Guerrilla has room for 10
///   the rest (64)               the same: a word for each breakable object, then their piece trees
/// Read against both games' files (Guerrilla's 576 world chunk files, Armageddon's 1,240): every
/// Guerrilla one converts and reads back with this library's readers.
/// </summary>
public static class GuerrillaChunks
{
    public const uint Signature = 0xB0CEEFA5;
    const int GuerrillaVersion = 55, Version = 62;
    const int GuerrillaGeneralSize = 88, GeneralSize = 100;
    const int GuerrillaPieceState = 36, PieceState = 40;
    const int ScriptPointSize = 116, GuerrillaIdSize = 520, IdSize = 808, IdHeader = 40, SnapPointSize = 48;
    const ushort LinkFlags = 0x10;

    // Guerrilla's panes of glass are left as ordinary pieces. Armageddon flags each of its own
    // (piece byte +63, 0x40: "shatterable") and shatters a flagged piece into shards, deleting it;
    // but each of its 439 is alone in its group and drawn with ir_glass_shatter. Guerrilla never
    // sets the flag, and many of its panes share a group (335 groups of several glass pieces, a
    // window broken up beforehand): flagged, they crashed the game when shattered (a group split
    // reaching a deleted piece: "split_start_idx >= 0 && split_start_idx < max_split_instance_infos",
    // and reads of a piece state never set up) and drew placeholder shards.

    /// <summary>
    /// The Armageddon c-file for a Guerrilla one, and the textures its materials name (as Guerrilla
    /// names them, without ".tga"; built-ins among them). With <paramref name="rename"/>, the file
    /// names each texture by what it gives for Guerrilla's name instead.
    /// </summary>
    public static (byte[] CFile, IReadOnlyList<string> Textures) Convert(byte[] file, Func<string, string>? rename = null)
    {
        try
        {
            return ConvertFile(file, rename ?? (name => name));
        }
        catch (ArgumentOutOfRangeException)
        {
            throw new InvalidDataException("the file's sections run past its end");
        }
    }

    static (byte[], IReadOnlyList<string>) ConvertFile(byte[] g, Func<string, string> rename)
    {
        int I(int at) => BitConverter.ToInt32(g, at);
        if (g.Length < 0x200 || BitConverter.ToUInt32(g, 0) != Signature || I(4) != GuerrillaVersion)
            throw new InvalidDataException("not a Guerrilla destructible object file (version 55)");
        if (I(0x10) != 0x200) throw new InvalidDataException("the mesh block isn't at 0x200");
        var o = new Output();
        o.Add(g.AsSpan(0, 0x200));
        o.Put(0x04, Version);
        o.Put(0x1BC, I(0x1B4));
        o.Put(0x1B4, 0);

        // The mesh block.
        int mesh = I(0x10);
        o.Add(MeshBlockConversion.FromVersion6(g, mesh));

        // The texture names and materials.
        int names = Align(mesh + I(mesh + 8), 16), nameSize = I(names);
        var nameList = new TextureNameList(g.AsSpan(names + 4, nameSize));
        int counts = Align(names + 4 + nameSize, 16), materialCount = I(counts + 4);
        int zeros = Align(counts + 8 + materialCount * 4, 16), blocks = Align(zeros + materialCount * 4, 16);
        var materials = new List<byte[]>();
        int at = blocks;
        for (int k = 0; k < materialCount; k++)
        {
            materials.Add(GuerrillaMaterials.Convert(g, at, nameList));
            at += I(at);
        }
        int materialsEnd = at;
        // The name list written again: the names the materials use, renamed.
        var textures = new List<string>();
        var finalNames = new TextureNameList([]);
        foreach (byte[] m in materials)
            for (int t = 0; t < BitConverter.ToUInt16(m, 0x10); t++)
            {
                string name = nameList.At(BitConverter.ToInt32(m, 0x24 + t * 12));
                name = name.EndsWith(".tga", StringComparison.OrdinalIgnoreCase) ? name[..^4] : name;
                if (!textures.Contains(name, StringComparer.OrdinalIgnoreCase)) textures.Add(name);
                BitConverter.TryWriteBytes(m.AsSpan(0x24 + t * 12), finalNames.Add(rename(name) + ".tga"));
            }
        byte[] list = finalNames.ToArray();
        o.Align(16);
        o.Add(BitConverter.GetBytes(list.Length));
        o.Add(list);
        o.Align(16);
        o.Add(g.AsSpan(counts, 8 + materialCount * 4));
        o.Align(16);
        o.Add(g.AsSpan(zeros, materialCount * 4));
        o.Align(16);
        foreach (byte[] m in materials) o.Add(m);

        // The general objects, from the next multiple of 64, and their names, which end where header 0x14 says.
        int generalCount = I(0x158), beforeDestruction = 0x200 + I(0x14);
        int records = Align(materialsEnd, 64), recordsEnd = records + generalCount * GuerrillaGeneralSize;
        if (recordsEnd > beforeDestruction || generalCount == 0 && recordsEnd != beforeDestruction)
            throw new InvalidDataException("the general objects don't end where the header says");
        o.Align(64);
        for (int k = 0; k < generalCount; k++)
        {
            o.Add(g.AsSpan(records + k * GuerrillaGeneralSize, GuerrillaGeneralSize));
            o.Add(new byte[GeneralSize - GuerrillaGeneralSize]);
        }
        o.Add(g.AsSpan(recordsEnd, beforeDestruction - recordsEnd));
        o.Put(0x14, o.Count - 0x200);

        // The destruction data.
        o.Align(128);
        int destruction = o.Count;
        o.Put(0x28, destruction);
        int destructionEnd = ConvertDestruction(g, I(0x28), o);
        o.Put(0x2C, o.Count - destruction);
        if (Align(destructionEnd, 64) != I(0x1C)) throw new InvalidDataException("the destruction data doesn't end where the collision starts");

        // The collision: a destructible object's shapes, or a stitch piece's one triangle mesh under a tree.
        o.Align(64);
        byte[] collision;
        if (PieceCollision.IsMesh(g))
        {
            var (vertices, triangles) = PieceCollision.Read(g);
            collision = PieceCollision.Section(vertices, triangles);
        }
        else collision = ChunkCollision.Section(ChunkCollision.Read(g));
        o.Put(0x1C, o.Count);
        o.Put(0x20, collision.Length);
        o.Add(collision);

        // The script points and object ids.
        int scriptPoints = Align(I(0x1C) + I(0x20), 64), scriptPointCount = I(0x180);
        o.Align(64);
        o.Add(g.AsSpan(scriptPoints, scriptPointCount * ScriptPointSize));
        int ids = Align(scriptPoints + scriptPointCount * ScriptPointSize, 64), idCount = I(0x18C);
        o.Align(64);
        for (int k = 0; k < idCount; k++)
        {
            int id = ids + k * GuerrillaIdSize;
            if (I(id + 36) is < 0 or > (GuerrillaIdSize - IdHeader) / SnapPointSize) throw new InvalidDataException($"object id {k} has {I(id + 36)} snap points");
            o.Add(g.AsSpan(id, GuerrillaIdSize));
            o.Add(new byte[IdSize - GuerrillaIdSize]);
        }

        // The rest: a word for each breakable object and their piece trees, as they are.
        int rest = Align(ids + idCount * GuerrillaIdSize, 64);
        if (rest < g.Length)
        {
            o.Align(64);
            o.Add(g.AsSpan(rest));
        }
        o.Align(64);
        return (o.ToArray(), textures);
    }

    /// <summary>Writes the destruction data at <paramref name="destruction"/> as Armageddon's; returns where Guerrilla's ends.</summary>
    static int ConvertDestruction(byte[] g, int destruction, Output o)
    {
        int I(int at) => BitConverter.ToInt32(g, at);
        int at = Align(destruction, 128), count = I(at);
        if (count is < 0 or > 10000) throw new InvalidDataException("bad breakable object count");
        o.Add(g.AsSpan(at, 4 + count * 4));
        at += 4 + count * 4;
        for (int d = 0; d < count; d++)
        {
            at = Align(at, 16); o.Align(16);
            int pieceCount = I(at + 12), linkCount = I(at + 20), groupCount = I(at + 28);
            if (pieceCount is < 0 or > 100000 || linkCount is < 0 or > 1000000 || groupCount is < 0 or > 100000)
                throw new InvalidDataException($"breakable object {d}: bad counts");
            o.Add(g.AsSpan(at, 44));
            at = Align(at + 44, 128); o.Align(128);

            // The pieces, their data and link numbers.
            int linkNumbers = 0;
            for (int p = 0; p < pieceCount; p++) linkNumbers += g[at + p * 64 + 62];
            int length = pieceCount * (64 + 12) + linkNumbers * 2;
            o.Add(g.AsSpan(at, length));
            at = Align(at + length, 4); o.Align(4);

            // The links, with Armageddon's flags.
            for (int l = 0; l < linkCount; l++)
            {
                byte[] link = g.AsSpan(at + l * 16, 16).ToArray();
                BitConverter.TryWriteBytes(link.AsSpan(14), LinkFlags);
                o.Add(link);
            }
            at = Align(at + linkCount * 16, 4); o.Align(4);

            // The groups and the tree of boxes.
            o.Add(g.AsSpan(at, groupCount * 60));
            at = Align(at + groupCount * 60, 4); o.Align(4);
            int treeEnd = BoxTreeEnd(g, at);
            o.Add(g.AsSpan(at, treeEnd - at));
            at = Align(treeEnd, 4); o.Align(4);

            // The run-time block: offsets of the link numbers, links and groups after the pieces' states, its size; then the states.
            int grow = pieceCount * (PieceState - GuerrillaPieceState), size = I(at + 12);
            o.Add(BitConverter.GetBytes(I(at)));
            o.Add(BitConverter.GetBytes(I(at + 4) + grow));
            o.Add(BitConverter.GetBytes(I(at + 8) + grow));
            o.Add(BitConverter.GetBytes(size + grow));
            o.Add(BitConverter.GetBytes(I(at + 16)));
            int data = at + 20;
            for (int p = 0; p < pieceCount; p++)
            {
                byte[] state = new byte[PieceState];
                Array.Copy(g, data + p * GuerrillaPieceState, state, 0, GuerrillaPieceState);
                BitConverter.TryWriteBytes(state.AsSpan(0), 0);
                BitConverter.TryWriteBytes(state.AsSpan(4), BitConverter.ToInt32(state, 4) + grow);  // where its link numbers are
                BitConverter.TryWriteBytes(state.AsSpan(24), 0);
                state[34] = 0;
                o.Add(state);
            }
            int states = pieceCount * GuerrillaPieceState;
            o.Add(g.AsSpan(data + states, size - states));
            at = Align(data + size, 16); o.Align(16);

            // The transform buffer, which starts with its own size.
            int buffer = I(at);
            o.Add(g.AsSpan(at, buffer));
            at = Align(at + buffer, 16); o.Align(16);
        }
        return at;
    }

    /// <summary>Where a tree of 20-byte box nodes written depth first ends (a node with 0 objects has its two children after it, one with more that many 4-byte object numbers).</summary>
    static int BoxTreeEnd(byte[] g, int root)
    {
        int I(int at) => BitConverter.ToInt32(g, at);
        int end = root + 20;
        void Walk(int node)
        {
            if (I(node) < 0) return;
            while (I(node) == 0)
            {
                int children = end;
                end += 40;
                Walk(children);
                int second = children + 20;
                if (I(second) < 0) return;
                node = second;
            }
            end += I(node) * 4;
        }
        Walk(root);
        return end;
    }

    static int Align(int value, int to) => (value + to - 1) / to * to;

    /// <summary>The file being written.</summary>
    sealed class Output
    {
        readonly List<byte> bytes = [];
        public int Count => bytes.Count;
        public void Add(ReadOnlySpan<byte> data) => bytes.AddRange(data);
        public void Align(int to) { while (bytes.Count % to != 0) bytes.Add(0); }
        public void Put(int at, int value) { byte[] b = BitConverter.GetBytes(value); for (int k = 0; k < 4; k++) bytes[at + k] = b[k]; }
        public byte[] ToArray() => [.. bytes];
    }
}
