using System.IO;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using RfaFormats;
using RfaFormats.Meshes;
using RfaFormats.Tables;
using RfaFormats.Textures;
using RfaFormats.Zones;

namespace RfaEditor;

/// <summary>
/// Finds the game's models and textures by name, the way the game asks for them: a zone names a
/// model ("s_edf_tech_flyer.cchk_pc"), a model names its textures ("rock_di.tga"). Each is looked
/// for next to whatever asks (same container, or same archive) first, then anywhere, once the
/// index of every container is ready. Things found are kept, so each is read and decoded once.
/// </summary>
sealed class GameAssets
{
    readonly Workspace workspace;
    Dictionary<string, IndexedFile>? files;     // every file in an archive or container, by name
    ILookup<string, IndexedFile>? places;       // ...and every place each name is in
    Dictionary<string, TextureRef>? textures;   // every texture, by name
    readonly Dictionary<string, ModelFile?> models = new(StringComparer.OrdinalIgnoreCase);
    readonly Dictionary<string, BitmapSource?> bitmaps = new(StringComparer.OrdinalIgnoreCase);
    readonly Dictionary<string, XtblFile?> tables = new(StringComparer.OrdinalIgnoreCase);
    ZoneNames? zoneNames;
    ZoneSchema? zoneSchema;
    bool zoneSchemaRead;
    readonly Dictionary<string, IReadOnlyList<string>> choices = new(StringComparer.OrdinalIgnoreCase);  // enum choices, by their populate

    public GameAssets(Workspace workspace) => this.workspace = workspace;

    /// <summary>Whether the index of every container is ready (it is built in the background at start).</summary>
    public bool IndexReady => files != null;

    /// <summary>Raised once the index is ready, so views drawn without it can look again.</summary>
    public event Action? IndexBecameReady;

    public void SetIndex(IEnumerable<IndexedFile> allFiles, Dictionary<string, TextureRef> allTextures)
    {
        var byName = new Dictionary<string, IndexedFile>(StringComparer.OrdinalIgnoreCase);
        foreach (IndexedFile file in allFiles) byName.TryAdd(file.Name, file);
        files = byName;
        places = allFiles.ToLookup(f => f.Name, StringComparer.OrdinalIgnoreCase);
        textures = allTextures;
        // Misses noted before the index was ready aren't kept (see Model and Texture), so nothing to forget.
        IndexBecameReady?.Invoke();
    }

    /// <summary>
    /// Names for the class and property hashes in zone files, from the strings in the game's exe and
    /// the names in its zone schema (which has a few the exe doesn't).
    /// </summary>
    public ZoneNames ZoneNames => zoneNames ??= new ZoneNames(
        ZoneNames.StringsIn(File.ReadAllBytes(Path.Combine(workspace.GameFolder, GameFolder.ExeName))).Concat(ZoneSchema?.Names ?? []));

    /// <summary>
    /// The game's zone schema (<see cref="RfaFormats.Zones.ZoneSchema"/>: what every class of zone
    /// object is made of), as the game has it; null if it can't be found or read.
    /// </summary>
    public ZoneSchema? ZoneSchema
    {
        get
        {
            if (zoneSchemaRead) return zoneSchema;
            zoneSchemaRead = true;
            try
            {
                if (workspace.ReadAsGameSeesIt(RfaFormats.Zones.ZoneSchema.FileName) is { } data) zoneSchema = RfaFormats.Zones.ZoneSchema.FromBytes(data);
            }
            catch (InvalidDataException) { }
            return zoneSchema;
        }
    }

    /// <summary>
    /// An enum's choices from the table its schema names (<see cref="SchemaPopulate"/>), as the game
    /// sees the table (yours, if you changed it), read once. Empty if the table can't be found.
    /// </summary>
    public IReadOnlyList<string> Choices(SchemaPopulate populate)
    {
        string key = populate.ToString();
        if (choices.TryGetValue(key, out IReadOnlyList<string>? known)) return known;
        IReadOnlyList<string> found = [];
        if (workspace.ReadAsGameSeesIt(populate.Table) is { } data) found = populate.Values(XtblFile.FromBytes(data));
        choices[key] = found;
        return found;
    }

    /// <summary>Every place a file name is: archives, and containers in them. Empty until the index is ready.</summary>
    public IEnumerable<(string Archive, string? Container)> PlacesOf(string name) =>
        places == null ? [] : places[name].Select(f => (f.Archive, f.Container));

    /// <summary>Every file name in the game that matches (each once). Empty until the index is ready.</summary>
    public IEnumerable<string> NamesWhere(Func<string, bool> match) => files == null ? [] : files.Keys.Where(match);

    /// <summary>A model by its c-file name, or null if it can't be found or read.</summary>
    public ModelFile? Model(string name, string nearArchive, string? nearContainer)
    {
        if (models.TryGetValue(name, out ModelFile? known) && known != null) return known;
        ModelFile? model = null;
        try
        {
            if (Find(name, nearArchive, nearContainer) is (Packfile pack, PackfileEntry header) &&
                pack.Find(ModelFile.DataFileName(name)) is PackfileEntry data)
                model = ModelFile.Read(name, pack.Read(header), pack.Read(data));
        }
        catch (InvalidDataException) { }
        catch (NotSupportedException) { }
        if (model != null || IndexReady) models[name] = model;  // a miss is only final once the index is ready
        return model;
    }

    /// <summary>
    /// A texture by name as a picture at most <paramref name="maxSize"/> pixels wide, or null. With
    /// <paramref name="alpha"/> its alpha channel is kept as see-through (for decals); otherwise it
    /// is shown opaque, as some textures use alpha for other things.
    /// </summary>
    public BitmapSource? Texture(string name, string nearArchive, string? nearContainer, int maxSize, bool alpha = false)
    {
        string key = $"{name}@{maxSize}{(alpha ? "+alpha" : "")}";
        if (bitmaps.TryGetValue(key, out BitmapSource? known) && known != null) return known;
        BitmapSource? bitmap = FindTexture(name, nearArchive, nearContainer) is var (peg, texture)
            ? Bitmaps.FromRgba(peg.Decode(texture, maxSize), alpha)
            : null;
        if (bitmap != null || IndexReady) bitmaps[key] = bitmap;
        return bitmap;
    }

    /// <summary>A game table (.xtbl) by name, read once; null if it can't be found (or the index isn't ready yet).</summary>
    public XtblFile? Table(string name)
    {
        if (tables.TryGetValue(name, out XtblFile? known)) return known;
        XtblFile? table = null;
        if (files != null && files.TryGetValue(name, out IndexedFile? found) && Pack(found.Archive, found.Container) is var pack && pack.Find(name) is { } entry)
            table = XtblFile.FromBytes(pack.Read(entry));
        if (table != null || IndexReady) tables[name] = table;
        return table;
    }

    /// <summary>The texture file holding a texture, and the texture in it.</summary>
    public (PegFile, PegTexture)? FindTexture(string name, string nearArchive, string? nearContainer)
    {
        if (nearContainer != null && FindTextureIn(workspace.Container(nearArchive, nearContainer), name) is { } local) return local;
        if (textures != null && textures.TryGetValue(name, out TextureRef? at))
            return ReadTexture(Pack(at.Archive, at.Container), at.PegHeader, name);
        return null;
    }

    /// <summary>
    /// Any file by name, as the game sees it, and where it was found: in the container given, else
    /// in the archive given, else anywhere (once the index is ready). Null if it can't be found.
    /// </summary>
    public (byte[] Data, string Archive, string? Container)? ReadFile(string name, string nearArchive, string? nearContainer)
    {
        if (nearContainer != null && workspace.Container(nearArchive, nearContainer) is var container && container.Find(name) is { } inContainer)
            return (container.Read(inContainer), nearArchive, nearContainer);
        if (workspace.Archive(nearArchive).Find(name) is { } inArchive)
            return (workspace.Archive(nearArchive).Read(inArchive), nearArchive, null);
        if (files != null && files.TryGetValue(name, out IndexedFile? found) && Pack(found.Archive, found.Container).Find(name) is { } entry)
            return (Pack(found.Archive, found.Container).Read(entry), found.Archive, found.Container);
        return null;
    }

    /// <summary>A file by name: in the container given, else in the archive given, else anywhere (index).</summary>
    (Packfile, PackfileEntry)? Find(string name, string nearArchive, string? nearContainer)
    {
        if (nearContainer != null && workspace.Container(nearArchive, nearContainer) is var container && container.Find(name) is { } inContainer)
            return (container, inContainer);
        if (workspace.Archive(nearArchive).Find(name) is { } inArchive)
            return (workspace.Archive(nearArchive), inArchive);
        if (files != null && files.TryGetValue(name, out IndexedFile? found))
        {
            Packfile pack = Pack(found.Archive, found.Container);
            if (pack.Find(name) is { } entry) return (pack, entry);
        }
        return null;
    }

    /// <summary>An archive, or a container in it, as the game sees it.</summary>
    Packfile Pack(string archive, string? container) => container == null ? workspace.Archive(archive) : workspace.Container(archive, container);

    static (PegFile, PegTexture)? FindTextureIn(Packfile pack, string name)
    {
        foreach (PackfileEntry entry in pack.Entries.Where(e => e.Extension is "cpeg_pc" or "cvbm_pc"))
            if (PegFile.TextureNames(pack.Read(entry)).Contains(name, StringComparer.OrdinalIgnoreCase))
                return ReadTexture(pack, entry.Name, name);
        return null;
    }

    /// <summary>One texture from a given texture file pair in a packfile.</summary>
    static (PegFile, PegTexture)? ReadTexture(Packfile pack, string headerName, string name)
    {
        if (pack.Find(headerName) is not { } header || pack.Find(PegFile.DataFileName(headerName)!) is not { } data) return null;
        PegFile peg = PegFile.Read(pack.Read(header), pack.Read(data));
        PegTexture? texture = peg.Textures.FirstOrDefault(t => t.Name.Equals(name, StringComparison.OrdinalIgnoreCase));
        return texture == null ? null : (peg, texture);
    }
}

static class Bitmaps
{
    /// <summary>A picture for the screen. Without <paramref name="alpha"/> every pixel is shown opaque.</summary>
    public static BitmapSource FromRgba(RgbaImage image, bool alpha)
    {
        var bgra = new byte[image.Pixels.Length];
        for (int i = 0; i < bgra.Length; i += 4)
        {
            bgra[i] = image.Pixels[i + 2];
            bgra[i + 1] = image.Pixels[i + 1];
            bgra[i + 2] = image.Pixels[i];
            bgra[i + 3] = alpha ? image.Pixels[i + 3] : (byte)255;
        }
        BitmapSource bitmap = BitmapSource.Create(image.Width, image.Height, 96, 96, PixelFormats.Bgra32, null, bgra, image.Width * 4);
        bitmap.Freeze();
        return bitmap;
    }
}
