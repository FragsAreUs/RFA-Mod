using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows;
using RfaFormats;
using RfaFormats.Interface;

namespace RfaEditor;

/// <summary>
/// The preview of an interface document (.vint_doc: a menu or a HUD screen): <see cref="VintView"/>, made the first time
/// one is shown and laid over the preview's other views. Its pieces come from the game's files as the game sees them:
/// the picture sheets (the sheet tables the interface project rfg.vint_proj names: bitmap_sheets.xtbl, or with the
/// Guerrilla HUD installed guerrilla_hud_sheets.xtbl), the English strings (misc.vpp_pc's *_us.le_strings), the fonts' heights
/// (interface.vpp_pc's .vf3_pc), its sub-documents and its script (found by name, its own container first).
/// </summary>
public partial class FilePreview
{
    VintView? vintView;
    static Dictionary<string, (string, int, int, int, int)>? vintSheets;
    static Dictionary<uint, string>? vintStrings;
    static Dictionary<string, int>? vintFontHeights;

    void ShowVintDocument(GameFile file)
    {
        byte[] bytes = ReadAsGameSeesIt(file);
        VintTree document;
        try
        {
            document = VintTree.Read(bytes);
        }
        catch (Exception e) when (e is InvalidDataException or ArgumentException or IndexOutOfRangeException)
        {
            PreviewText.Text = $"This interface document can't be read: {e.Message}\n\n" + Preview.Of(bytes);
            return;
        }
        vintView ??= MakeVintView();
        PreviewText.Visibility = Visibility.Collapsed;
        vintView.Visibility = Visibility.Visible;

        string archive = file.Archive;
        string? container = file.Container;
        string name = Path.GetFileNameWithoutExtension(file.Name);
        string? script = document.MetadataValue("lua_script_file");
        var watch = new List<string> { file.Name, "rfg.vint_proj", "bitmap_sheets.xtbl" };
        watch.AddRange(SheetTableNames().Select(table => table + ".xtbl"));
        if (container != null) watch.Add(container);
        if (script != null) watch.AddRange([script, script.EndsWith(".lua", StringComparison.OrdinalIgnoreCase) ? script : script + ".lua"]);

        vintView.Show(new VintSources
        {
            Texture = texture => context.Assets.Texture(texture, archive, container, 4096, alpha: true),
            Sheets = vintSheets ??= ReadSheets(),
            Strings = vintStrings ??= ReadEnglishStrings(),
            FontHeights = vintFontHeights ??= ReadFontHeights(),
            Font = ReadFont,
            Document = OtherDocument,
            File = new VintFile(archive, container, file.Name),
            Save = SaveVintDocuments,
            Restore = () => RestoreVintDocument(new VintFile(archive, container, file.Name)),
            IsChanged = () => container != null ? Workspace.IsChanged(archive, container) : Workspace.IsChanged(archive, file.Name),
            Script = scriptName => context.Assets.ReadFile(scriptName, archive, container)?.Data is { } data ? Encoding.Latin1.GetString(data) : null,
            Reload = () =>
            {
                // Its files changed on disk: what is in overrides\ looked at again, then the document read as the game sees it.
                Workspace.RefreshOverrides();
                vintSheets = null;
                return VintTree.Read(ReadAsGameSeesIt(file));
            },
            DocumentName = name,
            WatchNames = watch,
            ModFolder = Workspace.ModFolder,
            Log = context.Log,
        }, document);
    }

    // A sub-document by name and where it is: in its own container where it has one (the game loads documents from their
    // containers, vint_doc_containers.asm_pc), read fresh through the workspace so saved changes show; else anywhere.
    (VintTree, VintFile)? OtherDocument(string name)
    {
        string fileName = name + ".vint_doc";
        foreach (var (archive, container) in context.Assets.PlacesOf(fileName).OrderBy(p => p.Container == null ? 1 : 0))
        {
            try
            {
                Packfile pack = container != null ? Workspace.Container(archive, container) : Workspace.Archive(archive);
                if (pack.Find(fileName) is not { } entry) continue;
                byte[] data = container != null ? pack.Read(entry) : Workspace.Read(archive, fileName);
                if (VintDocument.IsBinary(data)) return (VintTree.Read(data), new VintFile(archive, container, fileName));
            }
            catch (Exception e) when (e is IOException or InvalidDataException) { }
        }
        return null;
    }

    // Writes changed documents where the game reads them: into their containers (rebuilt in overrides\, the asset lists'
    // sizes with them), or as overrides of loose files. A loose copy whose archive also has it in a container of its
    // name (interface.vpp_pc has ui_hud.vint_doc beside ui_hud.str2_pc) goes into the container too: that is the copy the
    // game loads.
    string SaveVintDocuments(IReadOnlyList<(VintFile File, byte[] Bytes)> writes)
    {
        var all = new List<(VintFile File, byte[] Bytes)>(writes);
        foreach (var (f, bytes) in writes.Where(w => w.File.Container == null))
        {
            string twin = Path.ChangeExtension(f.FileName, ".str2_pc");
            if (Workspace.Archive(f.Archive).Find(twin) != null && Workspace.Container(f.Archive, twin).Find(f.FileName) != null &&
                !all.Any(w => w.File.Container == twin && w.File.FileName.Equals(f.FileName, StringComparison.OrdinalIgnoreCase)))
                all.Add((new VintFile(f.Archive, twin, f.FileName), bytes));
        }
        var done = new List<string>();
        foreach (var group in all.GroupBy(w => (w.File.Archive, w.File.Container)))
        {
            if (group.Key.Container is string inContainer)
            {
                var files = group.ToDictionary(w => w.File.FileName, w => w.Bytes, StringComparer.OrdinalIgnoreCase);
                try { done.Add(ContainerEdits.Put(Workspace, group.Key.Archive, inContainer, files)); }
                catch (Exception e) when (e is FileNotFoundException or DirectoryNotFoundException or InvalidDataException)
                {
                    done.Add(ContainerEdits.Replace(Workspace, group.Key.Archive, inContainer, files));  // no game copy to compare with
                }
            }
            else
                foreach (var (f, bytes) in group)
                {
                    byte[]? original = null;
                    try { original = Workspace.ReadOriginal(f.Archive, f.FileName); }
                    catch (Exception e) when (e is IOException or InvalidDataException) { }
                    if (original != null && original.AsSpan().SequenceEqual(bytes))
                    {
                        Workspace.DeleteOverride(f.Archive, f.FileName);
                        done.Add($"{f.FileName} is the game's own again.");
                    }
                    else done.Add("wrote " + Workspace.RelativeToMod(Workspace.WriteOverride(f.Archive, f.FileName, bytes)) + ".");
                }
        }
        Workspace.RefreshOverrides();
        context.OverridesChanged();
        return string.Join(" ", done);
    }

    // The game's own copy of a document back (in its container, and its container twin for a loose copy).
    string RestoreVintDocument(VintFile f)
    {
        var done = new List<string>();
        void FromContainer(string container)
        {
            Packfile original = Packfile.FromBytes(container, Workspace.ReadOriginal(f.Archive, container));
            if (original.Find(f.FileName) is not { } entry) throw new InvalidDataException($"the game's own {container} has no {f.FileName}");
            done.Add(ContainerEdits.Put(Workspace, f.Archive, container, new Dictionary<string, byte[]> { [f.FileName] = original.Read(entry) }));
        }
        if (f.Container != null) FromContainer(f.Container);
        else
        {
            if (Workspace.IsChanged(f.Archive, f.FileName))
            {
                Workspace.DeleteOverride(f.Archive, f.FileName);
                done.Add($"{f.FileName} is the game's own again.");
            }
            string twin = Path.ChangeExtension(f.FileName, ".str2_pc");
            if (Workspace.Archive(f.Archive).Find(twin) != null && Workspace.IsChanged(f.Archive, twin)) FromContainer(twin);
        }
        Workspace.RefreshOverrides();
        context.OverridesChanged();
        return done.Count > 0 ? string.Join(" ", done) : $"{f.FileName} is the game's own.";
    }

    // The sheet tables the game reads: those the interface project (rfg.vint_proj, as the game sees it) names in its
    // <bmp_sheet_file>s, by name without ".xtbl". The Guerrilla HUD's project override names guerrilla_hud_sheets (the
    // game's own table with Guerrilla's sheets added) in place of bitmap_sheets; with none named, bitmap_sheets.
    List<string> SheetTableNames()
    {
        var names = new List<string>();
        if (Workspace.ReadAsGameSeesIt("rfg.vint_proj") is { } project)
            foreach (Match m in Regex.Matches(Encoding.Latin1.GetString(project), @"<bmp_sheet_file>\s*<filename>([^<]+)</filename>", RegexOptions.Singleline))
            {
                string name = m.Groups[1].Value.Trim();
                if (name.EndsWith(".xtbl", StringComparison.OrdinalIgnoreCase)) name = name[..^5];
                if (name.Length > 0 && !names.Contains(name, StringComparer.OrdinalIgnoreCase)) names.Add(name);
            }
        if (names.Count == 0) names.Add("bitmap_sheets");
        return names;
    }

    // The sheet tables (see SheetTableNames), then bitmap_sheets.xtbl for any picture they haven't: each sheet's name and
    // the pictures cut out of it (StartX, StartY, ImageWidth, ImageHeight).
    Dictionary<string, (string, int, int, int, int)> ReadSheets()
    {
        var pieces = new Dictionary<string, (string, int, int, int, int)>(StringComparer.OrdinalIgnoreCase);
        static int Number(string xml, string tag) =>
            Regex.Match(xml, $"<{tag}>\\s*(-?\\d+)\\s*</{tag}>") is { Success: true } m ? int.Parse(m.Groups[1].Value) : 0;
        foreach (string tableName in SheetTableNames().Append("bitmap_sheets").Distinct(StringComparer.OrdinalIgnoreCase))
        {
            if (Workspace.ReadAsGameSeesIt(tableName + ".xtbl") is not { } bytes) continue;
            string table = Encoding.Latin1.GetString(bytes);
            foreach (Match sheet in Regex.Matches(table, @"<BitmapSheets>\s*<Name>([^<]+)</Name>(.*?)</BitmapSheets>", RegexOptions.Singleline))
            {
                string sheetName = sheet.Groups[1].Value.Trim();
                foreach (Match picture in Regex.Matches(sheet.Groups[2].Value, @"<Properties>(.*?)</Properties>", RegexOptions.Singleline))
                {
                    string xml = picture.Groups[1].Value;
                    if (Regex.Match(xml, @"<Name>([^<]+)</Name>") is not { Success: true } n) continue;
                    pieces.TryAdd(n.Groups[1].Value.Trim(), (sheetName, Number(xml, "StartX"), Number(xml, "StartY"), Number(xml, "ImageWidth"), Number(xml, "ImageHeight")));
                }
            }
        }
        return pieces;
    }

    // Every English string of misc.vpp_pc (its *_us.le_strings), by its tag's hash.
    Dictionary<uint, string> ReadEnglishStrings()
    {
        var strings = new Dictionary<uint, string>();
        string? misc = Workspace.Archives().FirstOrDefault(a => Path.GetFileName(a).Equals("misc.vpp_pc", StringComparison.OrdinalIgnoreCase));
        if (misc == null) return strings;
        foreach (var entry in Workspace.Archive(misc).Entries.Where(e => e.Name.EndsWith("_us.le_strings", StringComparison.OrdinalIgnoreCase)))
        {
            try
            {
                foreach (var (hash, text) in GameStrings.Read(Workspace.Read(misc, entry.Name)).Entries) strings.TryAdd(hash, GameStrings.Readable(text, "us"));
            }
            catch (InvalidDataException) { }
        }
        return strings;
    }

    // Each font's line height (its .vf3_pc's u16 at +0x16: font_body 50, font_header 49), by name.
    Dictionary<string, int> ReadFontHeights()
    {
        var heights = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        string? iface = Workspace.Archives().FirstOrDefault(a => Path.GetFileName(a).Equals("interface.vpp_pc", StringComparison.OrdinalIgnoreCase));
        if (iface == null) return heights;
        foreach (var entry in Workspace.Archive(iface).Entries.Where(e => e.Extension == "vf3_pc"))
        {
            byte[] font = Workspace.Read(iface, entry.Name);
            if (font.Length > 0x18 && Encoding.ASCII.GetString(font, 0, 4) == "VFNT")
                heights[Path.GetFileNameWithoutExtension(entry.Name)] = BitConverter.ToUInt16(font, 0x16);
        }
        return heights;
    }

    // One of the game's fonts by name: its .vf3_pc in interface.vpp_pc (as the game sees it), read once a run.
    GameFont? ReadFont(string name)
    {
        if (vintFonts.TryGetValue(name, out GameFont? known)) return known;
        GameFont? font = null;
        try
        {
            if (Workspace.ReadAsGameSeesIt(name + ".vf3_pc") is { } bytes) font = GameFont.Read(bytes);
        }
        catch (InvalidDataException e)
        {
            context.Log($"Interface preview: the font {name} can't be read ({e.Message})");
        }
        return vintFonts[name] = font;
    }
    static readonly Dictionary<string, GameFont?> vintFonts = new(StringComparer.OrdinalIgnoreCase);

    void HideVintDocument()
    {
        if (vintView == null) return;
        vintView.Hide();
        vintView.Visibility = Visibility.Collapsed;
    }

    VintView MakeVintView()
    {
        var view = new VintView { Visibility = Visibility.Collapsed };
        ViewPart.Children.Add(view);
        context.Assets.IndexBecameReady += () =>
        {
            if (view.IsVisible) view.Redraw();  // textures in other containers can be found now
        };
        return view;
    }
}
