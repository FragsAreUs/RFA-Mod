using System.IO;
using RfaFormats;

namespace RfaEditor;

/// <summary>One file inside an archive, or inside a .str2_pc container inside an archive.</summary>
public sealed class GameFile
{
    public Packfile Source { get; }
    public PackfileEntry Entry { get; }
    public string Archive { get; }      // path of the archive relative to the game folder
    public string? Container { get; }   // name of the .str2_pc it is in, if any

    public GameFile(Packfile source, PackfileEntry entry, string archive, string? container)
    {
        Source = source;
        Entry = entry;
        Archive = archive;
        Container = container;
    }

    public string Name => Entry.Name;
    public string Where => Container == null ? IndexedFile.Shown(Archive) : $"{IndexedFile.Shown(Archive)} > {Container}";

    /// <summary>
    /// The loader replaces files the game looks up by name, which are the files directly inside
    /// an archive. Files inside a .str2_pc container are loaded as part of the whole container, so
    /// they can only be replaced by overriding the container.
    /// </summary>
    public bool CanOverride => Container == null;

    public byte[] Read() => Source.Read(Entry);
}

/// <summary>A file name in the search index: where it is, without keeping anything open.</summary>
public sealed record IndexedFile(string Name, string Archive, string? Container)
{
    public string Where => Container == null ? Shown(Archive) : $"{Shown(Archive)} > {Container}";

    /// <summary>
    /// Where a file is, as the search lists it: an archive by its path; the new files in overrides\
    /// (see <see cref="Workspace.NewFilesName"/>) and a mod's data said as such.
    /// </summary>
    public static string Shown(string archive) =>
        System.IO.Path.GetFileName(archive) == Workspace.NewFilesName ? $@"new file, overrides\{System.IO.Path.GetDirectoryName(archive)}"
        : archive.StartsWith(@"mods\", StringComparison.OrdinalIgnoreCase) ? $"{archive} (the mod's data)"
        : archive;

    public FileKind Kind => FileKind.Of(Name);
}

static class FileIndex
{
    /// <summary>
    /// Every file directly inside the archives (each as its path relative to the game folder, and a
    /// way to open it: see <see cref="Workspace.Open"/>; folders of loose files too). Quick: only the
    /// archives' name lists are read.
    /// </summary>
    public static List<IndexedFile> ArchiveFiles(IReadOnlyList<(string Archive, string Path, Func<Packfile> Open)> archives)
    {
        var files = new List<IndexedFile>();
        foreach ((string archive, _, Func<Packfile> open) in archives)
            foreach (PackfileEntry entry in open().Entries)
                files.Add(new IndexedFile(entry.Name, archive, null));
        return files;
    }

    /// <summary>What an archive's containers hold, and where its textures are, in the order found.</summary>
    sealed record Contents(List<(string Name, string Container)> Files, List<(string Texture, string? Container, string Peg)> Textures);

    /// <summary>
    /// Every file inside the .str2_pc containers, and where every texture is (by its name, such as
    /// "rock_di.tga", which is how models ask for them), whether its texture file is inside a
    /// container or directly in an archive. Each container is read out of its archive (and
    /// decompressed) to get its name list and the headers of its texture files: the slow part of
    /// reading the game's files. So what each archive holds is kept (index_cache.bin, see <see cref="Cache"/>)
    /// and read again only when the archive has changed (its size or time); archives are read side by
    /// side. With <paramref name="background"/>, at the lowest processor priority (see <see cref="Native.AtLowPriority"/>).
    /// </summary>
    public static List<IndexedFile> ContainerFiles(IReadOnlyList<(string Archive, string Path, Func<Packfile> Open)> archives, IProgress<string> progress,
                                                   Dictionary<string, TextureRef> textures, bool background)
    {
        Dictionary<string, (long Size, long Time, Contents Contents)> kept = Cache.Read();
        var found = new Contents?[archives.Count];
        var keys = new (long Size, long Time)?[archives.Count];
        var toRead = new List<int>();
        for (int i = 0; i < archives.Count; i++)
        {
            keys[i] = Key(archives[i].Path);
            if (keys[i] is { } key && kept.TryGetValue(archives[i].Path, out var was) && was.Size == key.Size && was.Time == key.Time)
                found[i] = was.Contents;
            else
                toRead.Add(i);
        }
        int done = 0, left = toRead.Count;
        if (left > 0) progress.Report($"Indexing containers for search: {left} of {archives.Count} archives to read (the rest are as they were)...");
        var options = new ParallelOptions { MaxDegreeOfParallelism = Math.Clamp(Environment.ProcessorCount / 2, 1, 4) };
        Parallel.ForEach(toRead, options, i =>
        {
            Contents read = background ? Native.AtLowPriority(() => ReadArchive(archives[i].Open())) : ReadArchive(archives[i].Open());
            found[i] = read;
            int now = Interlocked.Increment(ref done);
            progress.Report($"Indexing containers for search: {now} of {left} archives read...");
        });

        var files = new List<IndexedFile>();
        for (int i = 0; i < archives.Count; i++)
        {
            string archive = archives[i].Archive;
            foreach (var (name, container) in found[i]!.Files) files.Add(new IndexedFile(name, archive, container));
            foreach (var (texture, container, peg) in found[i]!.Textures) textures.TryAdd(texture, new TextureRef(archive, container, peg));
        }
        if (toRead.Count > 0)
        {
            var now = new Dictionary<string, (long, long, Contents)>(StringComparer.OrdinalIgnoreCase);
            for (int i = 0; i < archives.Count; i++)
                if (keys[i] is { } key) now[archives[i].Path] = (key.Size, key.Time, found[i]!);
            Cache.Write(now);
        }
        return files;
    }

    /// <summary>An archive file's size and time, which say whether it changed; null for a folder of loose files (always read).</summary>
    static (long Size, long Time)? Key(string path)
    {
        var info = new FileInfo(path);
        return info.Exists ? (info.Length, info.LastWriteTimeUtc.Ticks) : null;
    }

    static Contents ReadArchive(Packfile pack)
    {
        var contents = new Contents([], []);
        NoteTextures(pack, null, contents.Textures);
        foreach (PackfileEntry entry in pack.Entries.Where(e => e.IsContainer))
        {
            Packfile container = Packfile.FromBytes(entry.Name, pack.Read(entry));
            foreach (PackfileEntry inner in container.Entries)
                contents.Files.Add((inner.Name, entry.Name));
            NoteTextures(container, entry.Name, contents.Textures);
        }
        return contents;
    }

    /// <summary>Notes where each texture in a packfile's texture files is (the first place found wins, when they are put together).</summary>
    static void NoteTextures(Packfile pack, string? container, List<(string Texture, string? Container, string Peg)> textures)
    {
        foreach (PackfileEntry entry in pack.Entries.Where(e => e.Extension is "cpeg_pc" or "cvbm_pc"))
            foreach (string texture in RfaFormats.Textures.PegFile.TextureNames(pack.Read(entry)))
                textures.Add((texture, container, entry.Name));
    }

    /// <summary>
    /// What each archive's containers held when last read, by the archive's full path, with its size and time then:
    /// %LOCALAPPDATA%\RFA-Mod\index_cache.bin. Unreadable or of another version: read afresh.
    /// </summary>
    static class Cache
    {
        const string Magic = "RFAIDX";
        const int Version = 1;

        static string FilePath => System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RFA-Mod", "index_cache.bin");

        public static Dictionary<string, (long Size, long Time, Contents Contents)> Read()
        {
            var kept = new Dictionary<string, (long, long, Contents)>(StringComparer.OrdinalIgnoreCase);
            try
            {
                if (!File.Exists(FilePath)) return kept;
                using var reader = new BinaryReader(new BufferedStream(File.OpenRead(FilePath), 1 << 16));
                if (reader.ReadString() != Magic || reader.ReadInt32() != Version) return kept;
                int archives = reader.ReadInt32();
                for (int a = 0; a < archives; a++)
                {
                    string path = reader.ReadString();
                    long size = reader.ReadInt64(), time = reader.ReadInt64();
                    var contents = new Contents([], []);
                    int files = reader.ReadInt32();
                    for (int f = 0; f < files; f++) contents.Files.Add((reader.ReadString(), reader.ReadString()));
                    int textures = reader.ReadInt32();
                    for (int t = 0; t < textures; t++)
                    {
                        string texture = reader.ReadString();
                        string? container = reader.ReadBoolean() ? reader.ReadString() : null;
                        contents.Textures.Add((texture, container, reader.ReadString()));
                    }
                    kept[path] = (size, time, contents);
                }
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or EndOfStreamException or FormatException)
            {
                EditorLog.Write("The file index's cache couldn't be read (the game's files are read afresh): " + ex.Message);
                kept.Clear();
            }
            return kept;
        }

        public static void Write(Dictionary<string, (long Size, long Time, Contents Contents)> archives)
        {
            try
            {
                Directory.CreateDirectory(System.IO.Path.GetDirectoryName(FilePath)!);
                string temporary = FilePath + ".new";
                using (var writer = new BinaryWriter(new BufferedStream(File.Create(temporary), 1 << 16)))
                {
                    writer.Write(Magic);
                    writer.Write(Version);
                    writer.Write(archives.Count);
                    foreach (var (path, (size, time, contents)) in archives)
                    {
                        writer.Write(path);
                        writer.Write(size);
                        writer.Write(time);
                        writer.Write(contents.Files.Count);
                        foreach (var (name, container) in contents.Files)
                        {
                            writer.Write(name);
                            writer.Write(container);
                        }
                        writer.Write(contents.Textures.Count);
                        foreach (var (texture, container, peg) in contents.Textures)
                        {
                            writer.Write(texture);
                            writer.Write(container != null);
                            if (container != null) writer.Write(container);
                            writer.Write(peg);
                        }
                    }
                }
                File.Move(temporary, FilePath, overwrite: true);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                EditorLog.Write("The file index's cache couldn't be written (the next start reads the game's files afresh): " + ex.Message);
            }
        }
    }
}

/// <summary>Where a texture is: its texture file (header half), in a container or directly in an archive.</summary>
public sealed record TextureRef(string Archive, string? Container, string PegHeader);
