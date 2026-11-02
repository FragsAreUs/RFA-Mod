using System.Text;

namespace RfaFormats.Meshes;

/// <summary>
/// A zone's terrain file (.cterrain_pc, "TERR" version 42) in its parts, read and written in the order
/// and with the alignment the game's loader walks it (0x6B9640; every one of Armageddon's 119 terrain
/// files is read to its last byte and written back the same). Positions inside are counted from the
/// file's start; "to 4" and "to 16" pad with zeros to that multiple.
///
///   header, 40 bytes: "TERR", 42, whether the zone has ground (1 or 0), the texture names' count and
///     their block's size, the foliage meshes' count and their block's size, the stitch names' block
///     size, a word the loader never reads (a different one per file, low half 0x0020), the stitch
///     placements' count
///   the texture names (NUL-separated, padded), to 4; the stitch names and 20 bytes a placement, to 4;
///     the foliage mesh names, to 4
///   terrain_data, 0x420 bytes (see <see cref="Data"/> for the fields the game's own files set)
///   with ground: the ground material (<see cref="MaterialSet"/>), then its collision height field
///     ("MCKH" section) unless terrain_data says there is none
///   to 4, a word a sub-zone (filled at load)
///   with ground and cliff materials: 8 bytes, a word a cliff material twice, then each material:
///     to 4, its names block (its size, then the names), to 16, the material
///   to 4, a word (the stitch pieces' collision count), to 16, their "MCKH" section (Armageddon's
///     are all empty: its stitch pieces are in the stitch lists)
///   to 4, 8 bytes an invisible barrier, to 16, their "MCKH" section (<see cref="TerrainBarriers"/>)
///   to 4; with ground: the layer map (each 1 m cell's ground effect material, 2 bits), a word a
///     material, their names (NUL-terminated), to 4, a word a material (its index, filled at load)
///   to 4; foliage if terrain_data has layers (<see cref="ZoneFoliage"/>): 32 bytes a layer, 16 a
///     model, each layer's model names then to 4, 12 bytes a cell, 16 a patch, to 16, and the
///     scattering's buffer
///   with ground: to 4, the far meshes' nine 48-byte records and nine version-9 mesh blocks (each
///     its size at +8; see FarGround), then their material (<see cref="MaterialSet"/>, one)
///   to 4, 24 bytes an occluder
/// A material is its size (the whole of it, this word too), then its header, textures, constant
/// names and values (see MaterialBlocks); a size of 0 is no material (4 bytes).
/// </summary>
public sealed class TerrainFile
{
    public const uint Signature = 0x52524554, CurrentVersion = 42;
    public const int DataSize = 0x420;

    public uint Version { get; set; } = CurrentVersion;
    public bool HasGround { get; set; }
    public uint Id { get; set; }
    public int TextureNameCount { get; set; }
    public byte[] TextureNames { get; set; } = [];
    public int FoliageMeshCount { get; set; }
    public byte[] FoliageMeshNames { get; set; } = [];
    public byte[] StitchNames { get; set; } = [];
    public byte[] StitchPlacements { get; set; } = [];

    /// <summary>
    /// terrain_data (0x420 bytes). The fields Armageddon's own files set: +0x00 1 with ground; +0x04
    /// the zone's box (from x, y, z, to x, y, z); +0x1C, +0x20 the height field's samples (512, 512);
    /// +0x24 the occluders; +0x4C the sub-zones (9); +0x58, +0x5C the layer map's cells (512, 512),
    /// +0x60 its bits a cell (2), +0x64 its bytes, +0x6C its materials; +0x94 the barriers; +0x9C −1
    /// when there is no height field; +0xA0 the cliff materials; +0xAC the foliage layers, +0xB4 its
    /// cells, +0xBC its patches, +0xC4 its buffer's bytes. The rest is filled at load.
    /// </summary>
    public byte[] Data { get; set; } = new byte[DataSize];

    public MaterialSet? Ground { get; set; }
    public byte[]? HeightField { get; set; }
    public byte[] SubzoneSlots { get; set; } = [];
    public byte[] CliffHeader { get; set; } = [];
    public List<(byte[] Names, byte[] Material)> Cliffs { get; } = [];
    public int StitchCollisionCount { get; set; }
    public byte[] StitchCollision { get; set; } = EmptySection;
    public byte[] BarrierRecords { get; set; } = [];
    public byte[] BarrierSection { get; set; } = EmptySection;
    public LayerMapPart? LayerMap { get; set; }
    public byte[]? Foliage { get; set; }
    public byte[]? FoliageBuffer { get; set; }
    public byte[]? FarRecords { get; set; }
    public List<byte[]> FarBlocks { get; } = [];
    public MaterialSet? FarMaterial { get; set; }
    public byte[] Occluders { get; set; } = [];

    /// <summary>An empty "MCKH" collision section (version 5, its 16 bytes, no models).</summary>
    public static byte[] EmptySection => [.. "MCKH"u8, 5, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0];

    /// <summary>
    /// A set of materials as the ground's and the far meshes' are kept: their names block (its size,
    /// then the names, which the materials point into), a word for each of <see cref="Words"/> (the
    /// ground's: its names block's size again, once a material; the far meshes' have none), the
    /// material map (a run-time word and the count, then a run-time word a material), the materials'
    /// slots (the ground's only, a word each, filled at load) and the materials.
    /// </summary>
    public sealed record MaterialSet(byte[] Names, int[]? Words, byte[] Map, byte[] Slots, List<byte[]> Materials);

    /// <summary>
    /// The layer map: its cells, its materials' run-time name words, their names, their run-time index
    /// words. The far meshes come right after it (with no foliage), and the game reads their mesh blocks
    /// with steps to multiples of 16 counted from the file's start, so they must start at one; since the
    /// barriers end at one, the layer map's materials must take a multiple of 16 bytes: 8 a material and
    /// their names (each with its NUL) to a multiple of 4. The game's own files have one material,
    /// "Terrain" (4 + 8 + 4); <see cref="PaddedNames"/> makes up the length with names no cell uses.
    /// </summary>
    public sealed record LayerMapPart(byte[] Cells, byte[] NameWords, List<string> Names, byte[] IndexWords)
    {
        /// <summary>
        /// The names with as few of <paramref name="fillers"/> after them (effect materials the game
        /// knows, which no cell will use) as make the materials take a multiple of 16 bytes.
        /// </summary>
        public static List<string> PaddedNames(IReadOnlyList<string> names, IReadOnlyList<string> fillers)
        {
            static bool Fits(IReadOnlyCollection<string> list) => (list.Count * 8 + To(list.Sum(n => n.Length + 1), 4)) % 16 == 0;
            var unused = fillers.Where(f => !names.Contains(f, StringComparer.OrdinalIgnoreCase)).ToList();
            if (Fits(names.ToList())) return [.. names];
            foreach (string a in unused)
                if (Fits([.. names, a])) return [.. names, a];
            foreach (string a in unused)
                foreach (string b in unused.Where(b => b != a))
                    if (Fits([.. names, a, b])) return [.. names, a, b];
            throw new InvalidOperationException($"no filler names make the layer map's materials ({string.Join(", ", names)}) a multiple of 16 bytes");
        }
    }

    int D(int at) => BitConverter.ToInt32(Data, at);

    /// <summary>A terrain file in its parts. Throws <see cref="InvalidDataException"/> if it isn't one the game would read to its end.</summary>
    public static TerrainFile Read(byte[] file)
    {
        try { return ReadParts(file); }
        catch (ArgumentOutOfRangeException) { throw new InvalidDataException("the terrain file's parts run past its end"); }
        catch (IndexOutOfRangeException) { throw new InvalidDataException("the terrain file's parts run past its end"); }
    }

    static TerrainFile ReadParts(byte[] f)
    {
        uint U(int at) => BitConverter.ToUInt32(f, at);
        int I(int at) => BitConverter.ToInt32(f, at);
        byte[] Take(ref int at, int length) { byte[] b = f.AsSpan(at, length).ToArray(); at += length; return b; }
        if (f.Length < 0x28 + DataSize || U(0) != Signature) throw new InvalidDataException("not a terrain file");
        var t = new TerrainFile { Version = U(4), HasGround = U(8) != 0, TextureNameCount = I(12), FoliageMeshCount = I(20), Id = U(32) };
        if (t.Version != CurrentVersion) throw new InvalidDataException($"terrain file version {t.Version}, not {CurrentVersion}");
        int p = 0x28;
        t.TextureNames = Take(ref p, I(16)); p = To(p, 4);
        t.StitchNames = Take(ref p, I(28)); t.StitchPlacements = Take(ref p, I(36) * 20); p = To(p, 4);
        t.FoliageMeshNames = Take(ref p, I(24)); p = To(p, 4);
        t.Data = Take(ref p, DataSize);
        if (t.HasGround)
        {
            t.Ground = ReadSet(f, ref p, withWords: true);
            if (t.D(0x9C) != -1) t.HeightField = ReadSection(f, ref p);
        }
        p = To(p, 4); t.SubzoneSlots = Take(ref p, t.D(0x4C) * 4);
        int cliffs = t.D(0xA0);
        if (t.HasGround && cliffs > 0)
        {
            t.CliffHeader = Take(ref p, 8 + cliffs * 8);
            for (int k = 0; k < cliffs; k++)
            {
                p = To(p, 4);
                byte[] names = Take(ref p, 4 + I(p));
                p = To(p, 16);
                t.Cliffs.Add((names, ReadMaterial(f, ref p)));
            }
        }
        p = To(p, 4); t.StitchCollisionCount = I(p); p += 4; p = To(p, 16);
        t.StitchCollision = ReadSection(f, ref p);
        p = To(p, 4); t.BarrierRecords = Take(ref p, t.D(0x94) * 8); p = To(p, 16);
        t.BarrierSection = ReadSection(f, ref p);
        p = To(p, 4);
        if (t.HasGround && t.D(0x64) > 0)
        {
            byte[] cells = Take(ref p, t.D(0x64));
            int n = t.D(0x6C);
            byte[] nameWords = Take(ref p, n * 4);
            var names = new List<string>();
            for (int k = 0; k < n; k++)
            {
                int end = Array.IndexOf(f, (byte)0, p);
                names.Add(Encoding.Latin1.GetString(f, p, end - p));
                p = end + 1;
            }
            p = To(p, 4);
            t.LayerMap = new LayerMapPart(cells, nameWords, names, Take(ref p, n * 4));
        }
        p = To(p, 4);
        if (t.HasGround && t.D(0x78) > 0) throw new InvalidDataException("the terrain file has Guerrilla's undergrowth layers, which aren't read");
        if (t.D(0xAC) > 0)
        {
            int start = p, layers = t.D(0xAC);
            var models = Enumerable.Range(0, layers).Select(k => I(start + 32 * k)).ToList();
            p += layers * 32 + models.Sum() * 16;
            foreach (int count in models)
            {
                for (int m = 0; m < count; m++) p = Array.IndexOf(f, (byte)0, p) + 1;
                p = To(p, 4);
            }
            p += t.D(0xB4) * 12 + t.D(0xBC) * 16;
            t.Foliage = f[start..p];
            p = To(p, 16);
            t.FoliageBuffer = Take(ref p, t.D(0xC4));
        }
        if (t.HasGround)
        {
            p = To(p, 4);
            t.FarRecords = Take(ref p, 9 * 48);
            for (int k = 0; k < 9; k++)
            {
                if (U(p) != 9) throw new InvalidDataException($"far mesh block {k} isn't a version-9 mesh block");
                t.FarBlocks.Add(Take(ref p, I(p + 8)));
            }
            t.FarMaterial = ReadSet(f, ref p, withWords: false);
        }
        p = To(p, 4); t.Occluders = Take(ref p, t.D(0x24) * 24);
        if (p != f.Length) throw new InvalidDataException($"the terrain file's parts end at 0x{p:X}, not at its end (0x{f.Length:X})");
        return t;
    }

    static MaterialSet ReadSet(byte[] f, ref int p, bool withWords)
    {
        int I(int at) => BitConverter.ToInt32(f, at);
        p = To(p, 4);
        byte[] names = f.AsSpan(p, 4 + I(p)).ToArray();
        p += names.Length;
        int[]? words = null;
        if (withWords)
        {
            p = To(p, 4);
            int count = I(p);
            p += 4;
            p = To(p, 16);
            int mapLength = 8 + I(p + 4) * 4;
            byte[] map = f[p..(p + mapLength)];
            p += mapLength;
            words = new int[count];
            for (int k = 0; k < count; k++) words[k] = I(p + k * 4);
            p += count * 4;
            p = To(p, 16);
            int n = BitConverter.ToInt32(map, 4);
            byte[] slots = f[p..(p + n * 4)];
            p += slots.Length;
            var list = new List<byte[]>();
            for (int k = 0; k < n; k++) { p = To(p, 16); list.Add(ReadMaterial(f, ref p)); }
            return new MaterialSet(names, words, map, slots, list);
        }
        else
        {
            p = To(p, 16);
            int n = I(p + 4);
            byte[] map = f[p..(p + 8 + n * 4)];
            p += map.Length;
            var list = new List<byte[]>();
            for (int k = 0; k < n; k++) { p = To(p, 16); list.Add(ReadMaterial(f, ref p)); }
            return new MaterialSet(names, null, map, [], list);
        }
    }

    static byte[] ReadMaterial(byte[] f, ref int p)
    {
        int size = BitConverter.ToInt32(f, p);
        int length = size == 0 ? 4 : size;
        byte[] m = f[p..(p + length)];
        p += length;
        return m;
    }

    static byte[] ReadSection(byte[] f, ref int p)
    {
        if (Encoding.ASCII.GetString(f, p, 4) != "MCKH") throw new InvalidDataException($"no \"MCKH\" collision section at 0x{p:X}");
        int length = BitConverter.ToInt32(f, p + 8);
        byte[] s = f[p..(p + length)];
        p += length;
        return s;
    }

    /// <summary>The file, its header's counts and sizes taken from the parts.</summary>
    public byte[] Write()
    {
        var w = new List<byte>();
        void Put(byte[] b) => w.AddRange(b);
        void Word(int v) => w.AddRange(BitConverter.GetBytes(v));
        void Pad(int to) { while (w.Count % to != 0) w.Add(0); }
        Word(unchecked((int)Signature)); Word((int)Version); Word(HasGround ? 1 : 0);
        Word(TextureNameCount); Word(TextureNames.Length); Word(FoliageMeshCount); Word(FoliageMeshNames.Length);
        Word(StitchNames.Length); Word(unchecked((int)Id)); Word(StitchPlacements.Length / 20);
        Put(TextureNames); Pad(4);
        Put(StitchNames); Put(StitchPlacements); Pad(4);
        Put(FoliageMeshNames); Pad(4);
        Put(Data);
        if (HasGround)
        {
            WriteSet(w, Ground ?? throw new InvalidOperationException("a terrain file with ground needs its ground material"));
            if (D(0x9C) != -1) Put(HeightField ?? throw new InvalidOperationException("the terrain file's data says it has a height field"));
        }
        Pad(4); Put(SubzoneSlots);
        if (HasGround && Cliffs.Count > 0)
        {
            Put(CliffHeader);
            foreach (var (names, material) in Cliffs) { Pad(4); Put(names); Pad(16); Put(material); }
        }
        Pad(4); Word(StitchCollisionCount); Pad(16); Put(StitchCollision);
        Pad(4); Put(BarrierRecords); Pad(16); Put(BarrierSection);
        Pad(4);
        if (HasGround && LayerMap is { } map)
        {
            Put(map.Cells); Put(map.NameWords);
            foreach (string name in map.Names) { Put(Encoding.Latin1.GetBytes(name)); w.Add(0); }
            Pad(4); Put(map.IndexWords);
        }
        Pad(4);
        if (Foliage != null) { Put(Foliage); Pad(16); Put(FoliageBuffer ?? []); }
        if (HasGround)
        {
            Pad(4);
            if (w.Count % 16 != 0)
                throw new InvalidOperationException($"the far meshes would start at 0x{w.Count:X}, not a multiple of 16: the game's mesh reader steps to " +
                    "multiples of 16 within each block, counted from the file's start (see LayerMapPart for making up the length)");
            Put(FarRecords ?? throw new InvalidOperationException("a terrain file with ground needs its far meshes"));
            foreach (byte[] block in FarBlocks) Put(block);
            WriteSet(w, FarMaterial ?? throw new InvalidOperationException("a terrain file with ground needs its far meshes' material"));
        }
        Pad(4); Put(Occluders);
        return [.. w];
    }

    static void WriteSet(List<byte> w, MaterialSet set)
    {
        void Pad(int to) { while (w.Count % to != 0) w.Add(0); }
        Pad(4); w.AddRange(set.Names);
        if (set.Words is { } words)
        {
            Pad(4); w.AddRange(BitConverter.GetBytes(words.Length)); Pad(16);
            w.AddRange(set.Map);
            foreach (int word in words) w.AddRange(BitConverter.GetBytes(word));
            Pad(16);
            w.AddRange(set.Slots);
        }
        else
        {
            Pad(16); w.AddRange(set.Map);
        }
        foreach (byte[] material in set.Materials) { Pad(16); w.AddRange(material); }
    }

    static int To(int p, int multiple) => (p + multiple - 1) / multiple * multiple;

    /// <summary>
    /// Names as the header's texture list and the materials' names blocks keep them: each from an even
    /// place, followed by at least two NULs (the next starts at its length + 2, rounded up to even).
    /// Returns the bytes and where each name starts.
    /// </summary>
    public static (byte[] Bytes, int[] Starts) NameList(IEnumerable<string> names)
    {
        var bytes = new List<byte>();
        var starts = new List<int>();
        foreach (string name in names)
        {
            starts.Add(bytes.Count);
            bytes.AddRange(Encoding.ASCII.GetBytes(name));
            int next = (bytes.Count + 2 + 1) & ~1;
            while (bytes.Count < next) bytes.Add(0);
        }
        return ([.. bytes], [.. starts]);
    }

    /// <summary>
    /// A material set's names block written again with each texture name renamed (null keeps it), and
    /// the materials' texture entries pointed at their names' new places. Names no texture uses go.
    /// </summary>
    public static (byte[] Names, List<byte[]> Materials) Renamed(byte[] namesBlock, IReadOnlyList<byte[]> materials, Func<string, string?> rename)
    {
        string NameAt(int offset)
        {
            int start = 4 + offset, end = Array.IndexOf(namesBlock, (byte)0, start);
            return Encoding.ASCII.GetString(namesBlock, start, end - start);
        }
        var copies = materials.Select(m => (byte[])m.Clone()).ToList();
        var entries = copies.SelectMany(m => Textures(m).Select(t => (Material: m, t.EntryAt, Name: rename(NameAt(t.NameOffset)) ?? NameAt(t.NameOffset)))).ToList();
        var distinct = entries.Select(e => e.Name).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        var (bytes, starts) = NameList(distinct);
        foreach (var (material, entryAt, name) in entries)
            BitConverter.TryWriteBytes(material.AsSpan(entryAt), starts[distinct.FindIndex(n => n.Equals(name, StringComparison.OrdinalIgnoreCase))]);
        return ([.. BitConverter.GetBytes(bytes.Length), .. bytes], copies);
    }

    /// <summary>
    /// An Armageddon material block written from its parts: its size (this word too); the shader's hash;
    /// the material's hash; its flags (0x40; the far meshes' 0x140); the texture count (u16), the
    /// constants' rows and the rows it has room for (u8s); three run-time words and a zero word; 12
    /// bytes a texture (where its name starts in the set's names block, its slot's hash, 0); the
    /// constants' hashes (a matrix's, then −1 for each further row); from the next multiple of 16,
    /// <paramref name="room"/> rows of values (the rest zero).
    /// </summary>
    public static byte[] Material(uint shader, uint name, uint flags, int room, IReadOnlyList<(int NameAt, uint Slot)> textures,
                                  IReadOnlyList<(uint Hash, System.Numerics.Vector4[] Rows)> constants)
    {
        int rows = constants.Sum(c => c.Rows.Length);
        if (rows > room || room > 255 || textures.Count > ushort.MaxValue) throw new ArgumentException("more constant rows than the material has room for");
        int hashesAt = 0x24 + textures.Count * 12, valuesAt = To(hashesAt + rows * 4, 16);
        var m = new byte[valuesAt + room * 16];
        void Word(int at, uint value) => BitConverter.TryWriteBytes(m.AsSpan(at), value);
        Word(0x00, (uint)m.Length); Word(0x04, shader); Word(0x08, name); Word(0x0C, flags);
        BitConverter.TryWriteBytes(m.AsSpan(0x10), (ushort)textures.Count);
        m[0x12] = (byte)rows;
        m[0x13] = (byte)room;
        for (int k = 0; k < textures.Count; k++) { Word(0x24 + k * 12, (uint)textures[k].NameAt); Word(0x28 + k * 12, textures[k].Slot); }
        int row = 0;
        foreach (var (hash, values) in constants)
            for (int r = 0; r < values.Length; r++, row++)
            {
                Word(hashesAt + row * 4, r == 0 ? hash : uint.MaxValue);
                for (int c = 0; c < 4; c++) BitConverter.TryWriteBytes(m.AsSpan(valuesAt + row * 16 + c * 4), values[r][c]);
            }
        return m;
    }

    /// <summary>A material map for <paramref name="count"/> materials (a run-time word, the count, a run-time word each).</summary>
    public static byte[] MaterialMap(int count)
    {
        var map = new byte[8 + count * 4];
        BitConverter.TryWriteBytes(map.AsSpan(4), count);
        return map;
    }

    /// <summary>
    /// A names block: its size, then the names (see <see cref="NameList"/>), a name given twice kept once,
    /// as the game's are; and where each name given starts.
    /// </summary>
    public static (byte[] Block, int[] Starts) NamesBlock(IReadOnlyList<string> names)
    {
        var distinct = names.Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        var (bytes, starts) = NameList(distinct);
        int[] at = names.Select(n => starts[distinct.FindIndex(d => d.Equals(n, StringComparison.OrdinalIgnoreCase))]).ToArray();
        return ([.. BitConverter.GetBytes(bytes.Length), .. bytes], at);
    }

    /// <summary>An Armageddon material's texture entries (12 bytes each from +0x24: its name's place in the names block, its slot's hash, its number): where each is and its name's place.</summary>
    public static IEnumerable<(int EntryAt, int NameOffset, uint Slot)> Textures(byte[] material)
    {
        int count = BitConverter.ToUInt16(material, 0x10);
        for (int k = 0; k < count; k++)
            yield return (0x24 + k * 12, BitConverter.ToInt32(material, 0x24 + k * 12), BitConverter.ToUInt32(material, 0x28 + k * 12));
    }

    /// <summary>
    /// Where an Armageddon material's constant is: the place of its first value row, and how many rows
    /// it has (a matrix's name is followed by −1 for each further row), or null if it hasn't it.
    /// </summary>
    public static (int ValuesAt, int Rows)? Constant(byte[] material, uint hash)
    {
        int textures = BitConverter.ToUInt16(material, 0x10), constants = material[0x12];
        int hashesAt = 0x24 + textures * 12, valuesAt = To(hashesAt + constants * 4, 16);
        for (int c = 0; c < constants; c++)
        {
            if (BitConverter.ToUInt32(material, hashesAt + c * 4) != hash) continue;
            int rows = 1;
            while (c + rows < constants && BitConverter.ToUInt32(material, hashesAt + (c + rows) * 4) == uint.MaxValue) rows++;
            return (valuesAt + c * 16, rows);
        }
        return null;
    }
}
