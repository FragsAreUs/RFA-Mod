using RfaFormats;

namespace RfaEditor;

/// <summary>One file inside an archive, or inside a .str2_pc container inside an archive.</summary>
sealed class GameFile
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
    public string Where => Container == null ? Archive : $"{Archive} > {Container}";

    /// <summary>
    /// The loader replaces files the game looks up by name, which are the files directly inside
    /// an archive. Files inside a .str2_pc container are loaded as part of the whole container, so
    /// they can only be replaced by overriding the container.
    /// </summary>
    public bool CanOverride => Container == null;

    public byte[] Read() => Source.Read(Entry);
}

/// <summary>A file name in the search index: where it is, without keeping anything open.</summary>
sealed record IndexedFile(string Name, string Archive, string? Container)
{
    public string Where => Container == null ? Archive : $"{Archive} > {Container}";
    public FileKind Kind => FileKind.Of(Name);
}

static class FileIndex
{
    /// <summary>
    /// Every file directly inside the archives (each as its path relative to the game folder, and
    /// where it is: see <see cref="Workspace.ArchivePath"/>). Quick: only the archives' name lists are read.
    /// </summary>
    public static List<IndexedFile> ArchiveFiles(IReadOnlyList<(string Archive, string Path)> archives)
    {
        var files = new List<IndexedFile>();
        foreach ((string archive, string path) in archives)
            foreach (PackfileEntry entry in Packfile.Open(path).Entries)
                files.Add(new IndexedFile(entry.Name, archive, null));
        return files;
    }

    /// <summary>
    /// Every file inside the .str2_pc containers, and where every texture is (by its name, such as
    /// "rock_di.tga", which is how models ask for them), whether its texture file is inside a
    /// container or directly in an archive. Slower: each container is read out of its archive (and
    /// decompressed) to get its name list and the headers of its texture files.
    /// </summary>
    public static List<IndexedFile> ContainerFiles(IReadOnlyList<(string Archive, string Path)> archives, IProgress<string> progress, Dictionary<string, TextureRef> textures)
    {
        var files = new List<IndexedFile>();
        int done = 0;
        foreach ((string archive, string path) in archives)
        {
            Packfile pack = Packfile.Open(path);
            NoteTextures(pack, archive, null, textures);
            foreach (PackfileEntry entry in pack.Entries.Where(e => e.IsContainer))
            {
                Packfile container = Packfile.FromBytes(entry.Name, pack.Read(entry));
                foreach (PackfileEntry inner in container.Entries)
                    files.Add(new IndexedFile(inner.Name, archive, entry.Name));
                NoteTextures(container, archive, entry.Name, textures);
                if (++done % 100 == 0) progress.Report($"Indexing containers for search: {done} read...");
            }
        }
        return files;
    }

    /// <summary>Notes where each texture in a packfile's texture files is (the first place found wins).</summary>
    static void NoteTextures(Packfile pack, string archive, string? container, Dictionary<string, TextureRef> textures)
    {
        foreach (PackfileEntry entry in pack.Entries.Where(e => e.Extension is "cpeg_pc" or "cvbm_pc"))
            foreach (string texture in RfaFormats.Textures.PegFile.TextureNames(pack.Read(entry)))
                textures.TryAdd(texture, new TextureRef(archive, container, entry.Name));
    }
}

/// <summary>Where a texture is: its texture file (header half), in a container or directly in an archive.</summary>
sealed record TextureRef(string Archive, string? Container, string PegHeader);
