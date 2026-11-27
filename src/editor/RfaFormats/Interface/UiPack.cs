using System.Text;
using System.Text.RegularExpressions;
using RfaFormats.Guerrilla;
using RfaFormats.Textures;

namespace RfaFormats.Interface;

/// <summary>
/// RFA-Mod's UI pack: new pictures for the main menu's Modern style (Options > Main Menu > Style), drawn
/// here (no art files needed) and packed the way the Guerrilla HUD's pictures are (see GuerrillaHud): a
/// document that brings nothing but their texture file (rfa_ui_modern, VintDocument.TexturesOnly), in its
/// two containers (rfa_ui_modern.str2_pc and rfa_ui_modern2.str2_pc in overrides\build\pc\cache\rfa), listed in
/// the interface's asset list (vint_doc_containers.asm_pc) and project (rfg.vint_proj), its pictures in the
/// project's sheet table. The main menu has the loader load the document (rfa_load_ui_pack) and puts the
/// pictures on its own elements: the highlight's ends and middle (vdo_button_highlight's left, center and
/// right) and the panel's corner (ui_main_menu's mask_corner), rounded.
///
/// The asset list, project and sheet table are shared with the Guerrilla HUD (GuerrillaHud.Build writes them
/// from the game's own and then has this add itself again): this starts from our override of each if there
/// is one, else from the game's, and takes out what an earlier build of it added before adding it.
/// </summary>
public static class UiPack
{
    public const string Document = "rfa_ui_modern";
    /// <summary>Raised when the pictures change, so that an older build is made again.</summary>
    public const int Version = 1;
    const string TextureFile = "interface-" + Document;
    const string Sheet = "rfa_ui_modern_sheet";  // the texture's name (without .tga), and its sheet's
    const int SheetWidth = 256, SheetHeight = 64;

    /// <summary>The pictures, where they are in the sheet. The highlight's are the game's own sizes (37, 10 and 36 × 39).</summary>
    static readonly (string Name, int X, int Y, int Width, int Height)[] Pictures =
    [
        ("rfa_modern_hl_left", 0, 0, 37, 39),
        ("rfa_modern_hl_center", 40, 0, 10, 39),
        ("rfa_modern_hl_right", 52, 0, 36, 39),
        ("rfa_modern_corner", 92, 0, 50, 50),
    ];

    static string CacheOut(string overridesFolder) => OverrideLayout.Cache(overridesFolder);
    static string VersionFile(string overridesFolder) => Path.Combine(OverrideLayout.RfaFolder(overridesFolder), Document + ".version");

    /// <summary>Whether the pack is there, of this version, and listed in the asset list and the project.</summary>
    public static bool Built(string overridesFolder)
    {
        string cache = CacheOut(overridesFolder);
        try
        {
            string own = OverrideLayout.RfaFolder(overridesFolder);
            if (!File.Exists(Path.Combine(own, Document + ".str2_pc")) || !File.Exists(Path.Combine(own, Document + "2.str2_pc"))) return false;
            if (!File.Exists(VersionFile(overridesFolder)) || File.ReadAllText(VersionFile(overridesFolder)).Trim() != Version.ToString()) return false;
            string asm = Path.Combine(cache, "interface.vpp_pc", "vint_doc_containers.asm_pc");
            string project = Path.Combine(cache, "misc.vpp_pc", "rfg.vint_proj");
            return File.Exists(asm) && Encoding.Latin1.GetString(File.ReadAllBytes(asm)).Contains(Document + "2", StringComparison.OrdinalIgnoreCase)
                && File.Exists(project) && File.ReadAllText(project, Encoding.Latin1).Contains($"<doc_name>{Document}</doc_name>", StringComparison.OrdinalIgnoreCase);
        }
        catch (IOException) { return false; }
    }

    /// <summary>Was it ever built (its containers there, whatever their version, also at the top of the cache from an older build)? Then a rebuild of the HUD adds it again.</summary>
    public static bool Installed(string overridesFolder) => File.Exists(Path.Combine(OverrideLayout.RfaFolder(overridesFolder), Document + ".str2_pc")) ||
                                                            File.Exists(Path.Combine(CacheOut(overridesFolder), Document + ".str2_pc"));

    /// <summary>Builds it (again) into <paramref name="overridesFolder"/>. Returns what was done, a line each.</summary>
    public static List<string> Build(string gameFolder, string overridesFolder)
    {
        var report = new List<string>();
        string gameCache = Path.Combine(gameFolder, "build", "pc", "cache");
        Packfile gameInterface = Packfile.Open(Path.Combine(gameCache, "interface.vpp_pc"));
        Packfile gameMisc = Packfile.Open(Path.Combine(gameCache, "misc.vpp_pc"));
        string cacheOut = CacheOut(overridesFolder);

        // The texture, and the document that brings it, in their containers (as the flyer HUD's: streamed in when loaded).
        RgbaImage image = DrawSheet();
        byte[] pixels = TextureCodec.Encode(TextureFormat.Argb8888, image);
        byte[] pegHeader = PegFile.SingleHeader(Sheet + ".tga", SheetWidth, SheetHeight, TextureFormat.Argb8888, 1, 0);
        var pegData = new byte[(pixels.Length + 15) & ~15];
        pixels.CopyTo(pegData, 0);
        byte[] documentBytes = VintDocument.TexturesOnly(TextureFile, Document, Document + ".lua");
        byte[] scriptBytes = Encoding.Latin1.GetBytes(
            $"-- {Document}: RFA-Mod's pictures for the main menu's Modern style, made by the RFA editor (UiPack).\r\n" +
            "-- The document only brings them; the main menu loads it (rfa_load_ui_pack) and shows them.\r\n" +
            $"function {Document}_init()\r\nend\r\n\r\nfunction {Document}_cleanup()\r\nend\r\n");
        Packfile documentTemplate = Container(gameInterface, "ui_flyer_hud.str2_pc");
        Packfile textureTemplate = Container(gameInterface, "ui_flyer_hud2.str2_pc");
        byte[] main = Packfile.CreateContainer(documentTemplate, [(Document + ".vint_doc", documentBytes), (Document + ".lua", scriptBytes)]);
        byte[] art = Packfile.CreateContainer(textureTemplate, [(TextureFile + ".cpeg_pc", pegHeader), (TextureFile + ".gpeg_pc", pegData)]);
        Write(OverrideLayout.NewRfaFile(overridesFolder, Document + ".str2_pc"), main);
        Write(OverrideLayout.NewRfaFile(overridesFolder, Document + "2.str2_pc"), art);
        Write(OverrideLayout.NewRfaFile(overridesFolder, Document + ".vint_doc"), documentBytes);
        Write(OverrideLayout.NewRfaFile(overridesFolder, Document + ".lua"), scriptBytes);
        string oldVersion = Path.Combine(cacheOut, Document + ".version");  // an older build's, at the top of the cache
        if (File.Exists(oldVersion)) File.Delete(oldVersion);
        report.Add($"{Document}: {Pictures.Length} pictures in a {SheetWidth} × {SheetHeight} texture ({string.Join(", ", Pictures.Select(p => p.Name))})");

        // The asset list: ours, or the game's.
        string asmPath = Path.Combine(cacheOut, "interface.vpp_pc", "vint_doc_containers.asm_pc");
        AsmFile containers = AsmFile.Read(File.Exists(asmPath) ? File.ReadAllBytes(asmPath) : Read(gameInterface, "vint_doc_containers.asm_pc"));
        AsmFile gameContainers = AsmFile.Read(Read(gameInterface, "vint_doc_containers.asm_pc"));
        AsmContainer documentEntry = gameContainers.Containers.First(c => c.Name.Equals("ui_flyer_hud", StringComparison.OrdinalIgnoreCase));
        AsmContainer textureEntry = gameContainers.Containers.First(c => c.Name.Equals("ui_flyer_hud2", StringComparison.OrdinalIgnoreCase));
        containers.Containers.RemoveAll(c => c.Name.Equals(Document, StringComparison.OrdinalIgnoreCase) ||
                                             c.Name.Equals(Document + "2", StringComparison.OrdinalIgnoreCase));
        containers.Containers.Add(GuerrillaHud.Entry(documentEntry, Document, Packfile.FromBytes(Document + ".str2_pc", main),
            [(Document + ".vint_doc", 0), (Document + ".lua", 1)]));
        containers.Containers.Add(GuerrillaHud.Entry(textureEntry, Document + "2", Packfile.FromBytes(Document + "2.str2_pc", art),
            [(TextureFile + ".cpeg_pc", 0)]));
        Write(asmPath, containers.ToBytes());

        // The project: ours, or the game's; the document in its list, and its sheet table ours (the game reads the
        // table named there by name, past the overrides of the one it names itself: see GuerrillaHud.Build).
        string projectPath = Path.Combine(cacheOut, "misc.vpp_pc", "rfg.vint_proj");
        string projectText = File.Exists(projectPath) ? File.ReadAllText(projectPath, Encoding.Latin1) : ReadText(gameMisc, "rfg.vint_proj");
        var (at, length) = GuerrillaHud.SheetTableInProject(projectText);
        string sheetTable = projectText.Substring(at, length);
        string? ourSheets = OverrideLayout.Find(overridesFolder, GuerrillaHud.SheetTableName + ".xtbl");
        string sheetsText = sheetTable.Equals(GuerrillaHud.SheetTableName, StringComparison.OrdinalIgnoreCase) && ourSheets != null
            ? File.ReadAllText(ourSheets, Encoding.Latin1)
            : ReadText(gameMisc, sheetTable + ".xtbl");
        bool listed = projectText.Contains($"<doc_name>{Document}</doc_name>", StringComparison.OrdinalIgnoreCase);
        projectText = GuerrillaHud.WithDocuments(projectText, listed ? new List<string>() : new List<string> { Document }, GuerrillaHud.SheetTableName);
        Write(projectPath, Encoding.Latin1.GetBytes(projectText));
        Write(OverrideLayout.NewFile(overridesFolder, OverrideLayout.Hud, GuerrillaHud.SheetTableName + ".xtbl"), Encoding.Latin1.GetBytes(WithOurSheet(sheetsText)));
        report.Add($"Listed in vint_doc_containers.asm_pc and rfg.vint_proj; its pictures in the sheet table {GuerrillaHud.SheetTableName}.xtbl");

        Write(VersionFile(overridesFolder), Encoding.Latin1.GetBytes(Version.ToString()));
        return report;
    }

    /// <summary>The sheet table with our sheet (one texture, its pictures) in place of an earlier one, before its end.</summary>
    static string WithOurSheet(string sheetsText)
    {
        string newline = sheetsText.Contains("\r\n") ? "\r\n" : "\n";
        sheetsText = Regex.Replace(sheetsText, $@"^[ \t]*<BitmapSheets>\s*<Name>{Sheet}</Name>.*?</BitmapSheets>[ \t]*\r?\n", "",
                                   RegexOptions.Singleline | RegexOptions.Multiline | RegexOptions.IgnoreCase);
        var added = new StringBuilder();
        added.Append($"\t<BitmapSheets>{newline}\t\t<Name>{Sheet}</Name>{newline}\t\t<Images>{newline}");
        foreach (var (name, x, y, width, height) in Pictures)
            added.Append($"\t\t\t<Properties>{newline}\t\t\t\t<Name>{name}</Name>{newline}\t\t\t\t<StartX>{x}</StartX>{newline}" +
                         $"\t\t\t\t<StartY>{y}</StartY>{newline}\t\t\t\t<ImageWidth>{width}</ImageWidth>{newline}" +
                         $"\t\t\t\t<ImageHeight>{height}</ImageHeight>{newline}\t\t\t\t</Properties>{newline}");
        added.Append($"\t\t\t</Images>{newline}\t\t<_Editor>{newline}\t\t\t<Category>Entries</Category>{newline}\t\t\t</_Editor>{newline}" +
                     $"\t\t</BitmapSheets>{newline}");
        int end = sheetsText.LastIndexOf("</Table>", StringComparison.OrdinalIgnoreCase);
        if (end < 0) throw new InvalidDataException("the sheet table has no </Table>");
        int lineStart = sheetsText.LastIndexOf('\n', end) + 1;
        return sheetsText[..lineStart] + added + sheetsText[lineStart..];
    }

    // ---- The pictures: white, so the menu colours them (its accent); their shape in their alpha ----------

    /// <summary>
    /// The highlight behind the picked entry: a solid bar down its left edge, a light band across, a brighter
    /// line along its bottom, the right end fading out; the panel's top right corner rounded.
    /// </summary>
    static RgbaImage DrawSheet()
    {
        var image = new RgbaImage(SheetWidth, SheetHeight);
        const int band = 70, line = 160;
        foreach (var (name, left, top, width, height) in Pictures)
            for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                {
                    bool bottom = y >= height - 2;
                    double alpha = name switch
                    {
                        "rfa_modern_hl_left" => x < 4 ? 255 : bottom ? line : band,
                        "rfa_modern_hl_center" => bottom ? line : band,
                        "rfa_modern_hl_right" => (bottom ? line : band) * Math.Max(0, 1 - x / (double)(width - 1)),
                        // A quarter circle around the square's bottom left, the radius its side: the panel's corner rounded.
                        _ => 255 * Math.Clamp(width - Math.Sqrt(x * x + (height - 1 - y) * (height - 1 - y)) + 0.5, 0, 1),
                    };
                    int at = ((top + y) * SheetWidth + left + x) * 4;
                    image.Pixels[at] = image.Pixels[at + 1] = image.Pixels[at + 2] = 255;
                    image.Pixels[at + 3] = (byte)Math.Round(alpha);
                }
        return image;
    }

    static void Write(string path, byte[] bytes)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, bytes);
    }

    static byte[] Read(Packfile archive, string file) =>
        archive.Read(archive.Find(file) ?? throw new FileNotFoundException($"{archive.Name} has no {file}"));

    static string ReadText(Packfile archive, string file) => Encoding.Latin1.GetString(Read(archive, file));

    static Packfile Container(Packfile archive, string file) => Packfile.FromBytes(file, Read(archive, file));
}
