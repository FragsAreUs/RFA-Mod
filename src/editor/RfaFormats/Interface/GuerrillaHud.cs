using System.Text;
using System.Text.RegularExpressions;
using RfaFormats.Textures;

namespace RfaFormats.Interface;

/// <summary>
/// Makes Red Faction: Guerrilla's HUD documents loadable in Armageddon, for the guerrilla_hud mod,
/// from the player's own copy of Guerrilla (read only; nothing converted is ever shipped).
///
/// Both games read the same documents and textures, but give some different pictures the same
/// names (both have a sheet called ui_bms_00, for one), and Guerrilla's HUD scripts read game data
/// Armageddon no longer has. So for each document this:
///   - renames every picture it shows, and every texture they come from, with the prefix "rfg_"
///     (Armageddon's own HUD is left as it is);
///   - gathers those textures into one texture file of its own, interface-&lt;document&gt;;
///   - points the document at our own script (in the mod's folder), which drives Guerrilla's
///     elements from Armageddon's data;
///   - packs the document with the script, and the texture file, into two containers as Armageddon
///     packs its own (&lt;doc&gt;.str2_pc and &lt;doc&gt;2.str2_pc), next to loose copies of the
///     document and script (the game reads each document once at start-up);
///   - and adds the document to the player's own vint_doc_containers.asm_pc (what each container
///     holds) and rfg.vint_proj (the interface project, which must list every document), and the
///     sheets it uses to guerrilla_hud_sheets.xtbl (where each picture is on its sheet): the game's
///     own sheet table with Guerrilla's sheets added, renamed, which rfg.vint_proj names in place
///     of the game's.
/// Everything is written as overrides: new files beside the game's, and replacements for the two
/// lists, made from the game's own each time.
/// </summary>
public static partial class GuerrillaHud
{
    public const string Prefix = "rfg_";

    /// <summary>The documents converted, and the pictures their scripts show besides those the document names.</summary>
    public static readonly IReadOnlyList<HudDocument> Documents =
    [
        new("rfg_ui_hud_health", ["ui_hud_blood_gradient", "ui_hud_radiation"]),
    ];

    /// <summary>
    /// Pictures from textures that aren't brought over, and one that looks the same that is: both
    /// are a single white, opaque pixel (checked in Guerrilla's sheets).
    /// </summary>
    static readonly Dictionary<string, string> SamePicture = new(StringComparer.OrdinalIgnoreCase)
    {
        ["ui_fend_base_square"] = "ui_hud_base_square",
    };

    static readonly uint ImageHash = VintDocument.Hash("image");
    // Our sheet table, in place of the game's bitmap_sheets. Not named rfg_..., so that taking the
    // world's rfg_* files away never takes it with them (see Remove).
    const string SheetTableName = "guerrilla_hud_sheets";
    const string WholeSheetSuffix = "_sheet";                // a whole-texture picture's texture, made a sheet of one
    const string AlwaysLoaded = "interface-backend";  // in Armageddon's project, loaded from start-up

    /// <summary>
    /// Converts every document in <see cref="Documents"/>. <paramref name="scriptsFolder"/> holds our
    /// scripts (&lt;document&gt;.lua); <paramref name="overridesFolder"/> is the mod's overrides\.
    /// Returns what was done, a line each.
    /// </summary>
    public static List<string> Build(string guerrillaFolder, string gameFolder, string scriptsFolder, string overridesFolder)
    {
        var report = new List<string>();
        string guerrillaCache = Path.Combine(guerrillaFolder, "build", "pc", "cache");
        string gameCache = Path.Combine(gameFolder, "build", "pc", "cache");
        Packfile guerrillaInterface = Packfile.Open(Path.Combine(guerrillaCache, "interface.vpp_pc"));
        Packfile guerrillaMisc = Packfile.Open(Path.Combine(guerrillaCache, "misc.vpp_pc"));
        Packfile gameInterface = Packfile.Open(Path.Combine(gameCache, "interface.vpp_pc"));
        Packfile gameMisc = Packfile.Open(Path.Combine(gameCache, "misc.vpp_pc"));
        if (guerrillaInterface.FormatVersion != Packfile.GuerrillaVersion)
            throw new InvalidDataException($"{guerrillaFolder} isn't Red Faction: Guerrilla (its interface.vpp_pc is version {guerrillaInterface.FormatVersion})");

        string guerrillaSheetsText = ReadText(guerrillaMisc, "bitmap_sheets.xtbl");
        Dictionary<string, string> sheetOf = SheetPictures(guerrillaSheetsText);  // picture -> sheet

        // Armageddon packs each document in two containers: <doc> (document and script) and <doc>2
        // (textures). Our documents are listed like the flyer's HUD, which the game streams in while
        // playing (type 11), not like ui_hud, which it reads at start-up (type 12, as the HUD's
        // preloaded parts are).
        Packfile documentTemplate = Container(gameInterface, "ui_flyer_hud.str2_pc");
        Packfile textureTemplate = Container(gameInterface, "ui_flyer_hud2.str2_pc");
        AsmFile containers = AsmFile.Read(Read(gameInterface, "vint_doc_containers.asm_pc"));
        AsmContainer documentEntry = containers.Containers.First(c => c.Name.Equals("ui_flyer_hud", StringComparison.OrdinalIgnoreCase));
        AsmContainer textureEntry = containers.Containers.First(c => c.Name.Equals("ui_flyer_hud2", StringComparison.OrdinalIgnoreCase));

        string cacheOut = Path.Combine(overridesFolder, "build", "pc", "cache");
        var sheetsBrought = new List<string>();
        var wholeTextures = new List<(string Name, int Width, int Height)>();  // whole-texture pictures, each made a sheet
        var documentNames = new List<string>();
        foreach (HudDocument hud in Documents)
        {
            string name = hud.Name;
            string script = Path.Combine(scriptsFolder, name + ".lua");
            if (!File.Exists(script)) throw new FileNotFoundException($"our script for {name} is missing", script);
            var document = VintDocument.Read(Read(guerrillaInterface, name + ".vint_doc"));

            // The textures it can show: those of the texture files it depends on, from Guerrilla's
            // misc.vpp_pc (always loaded there) or the document's own texture container.
            Packfile? ownTextures = guerrillaInterface.Find(name + ".vint_doc2.str2_pc") is { } entry
                ? Packfile.FromBytes(entry.Name, guerrillaInterface.Read(entry)) : null;
            var textures = new Dictionary<string, (PegFile Peg, PegTexture Texture)>(StringComparer.OrdinalIgnoreCase);
            foreach (VintReference dependency in document.Dependencies.Where(d => d.Kind == 0))
            {
                string file = document.Text(dependency);
                PegFile? peg = ReadPeg(ownTextures, file) ?? ReadPeg(guerrillaMisc, file);
                if (peg == null)
                {
                    report.Add($"{name}: Guerrilla's texture file {file} not found; its pictures are left out");
                    continue;
                }
                foreach (PegTexture texture in peg.Textures) textures.TryAdd(Path.GetFileNameWithoutExtension(texture.Name), (peg, texture));
            }

            // Each picture, and the texture it comes from (its own, or the sheet it is cut from).
            var needed = new List<string>();
            string? TextureFor(string picture)
            {
                picture = SamePicture.GetValueOrDefault(picture, picture);
                if (textures.ContainsKey(picture)) return picture;
                return sheetOf.TryGetValue(picture, out string? sheet) && textures.ContainsKey(sheet) ? sheet : null;
            }
            var renamed = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
            var unknown = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (VintElement element in document.Elements())
                foreach (VintProperty property in element.Properties.Where(p => p.NameHash == ImageHash && p.ValueType == VintDocument.StringValue))
                {
                    string picture = document.Text(property.AsReference);
                    if (TextureFor(picture) is not { } texture)
                    {
                        if (picture.Length > 0 && !picture.Equals("null", StringComparison.OrdinalIgnoreCase)) unknown.Add(picture);
                        continue;
                    }
                    if (!needed.Contains(texture, StringComparer.OrdinalIgnoreCase)) needed.Add(texture);
                    document.Point(property.AsReference, Prefix + SamePicture.GetValueOrDefault(picture, picture));
                    renamed.Add(picture);
                }
            foreach (string picture in hud.ScriptPictures)
                if (TextureFor(picture) is { } texture && !needed.Contains(texture, StringComparer.OrdinalIgnoreCase)) needed.Add(texture);
                else if (TextureFor(picture) == null) report.Add($"{name}: its script's picture {picture} isn't in Guerrilla's textures");

            // One texture file with every texture it needs, renamed. The document waits for its
            // first texture file; any others it named point at one Armageddon always has loaded.
            // A picture that is a whole texture becomes the one picture of a sheet of its own
            // (the texture renamed <picture>_sheet): Armageddon finds sheet pictures through the
            // sheets it ties to their textures at start-up, which works for ours, while a whole
            // texture asked for by name came out as some other bitmap (black, with stray shapes).
            string textureFile = "interface-" + name;
            var dependencies = document.Dependencies.Where(d => d.Kind == 0).ToList();
            for (int i = 0; i < dependencies.Count; i++) document.Point(dependencies[i], i == 0 ? textureFile : AlwaysLoaded);
            bool IsSheet(string texture) => sheetOf.ContainsValue(texture);
            (byte[] pegHeader, byte[] pegData) = MergePegs(needed.Select(t =>
                (textures[t].Peg, textures[t].Texture, Prefix + t + (IsSheet(t) ? "" : WholeSheetSuffix) + ".tga")).ToList());
            foreach (string texture in needed)
            {
                if (IsSheet(texture))
                {
                    if (!sheetsBrought.Contains(texture, StringComparer.OrdinalIgnoreCase)) sheetsBrought.Add(texture);
                }
                else if (!wholeTextures.Any(w => w.Name.Equals(texture, StringComparison.OrdinalIgnoreCase)))
                {
                    wholeTextures.Add((texture, textures[texture].Texture.Width, textures[texture].Texture.Height));
                }
            }

            foreach (var (key, value) in document.Metadata)
                if (document.Text(key).Equals("lua_script_file", StringComparison.OrdinalIgnoreCase)) document.Point(value, name + ".lua");

            byte[] documentBytes = document.ToBytes();
            byte[] scriptBytes = File.ReadAllBytes(script);
            byte[] main = Packfile.CreateContainer(documentTemplate, [(name + ".vint_doc", documentBytes), (name + ".lua", scriptBytes)]);
            byte[] art = Packfile.CreateContainer(textureTemplate, [(textureFile + ".cpeg_pc", pegHeader), (textureFile + ".gpeg_pc", pegData)]);
            Write(Path.Combine(cacheOut, name + ".str2_pc"), main);
            Write(Path.Combine(cacheOut, name + "2.str2_pc"), art);
            Write(Path.Combine(cacheOut, name + ".vint_doc"), documentBytes);
            Write(Path.Combine(cacheOut, name + ".lua"), scriptBytes);

            containers.Containers.RemoveAll(c => c.Name.Equals(name, StringComparison.OrdinalIgnoreCase) ||
                                                 c.Name.Equals(name + "2", StringComparison.OrdinalIgnoreCase));
            containers.Containers.Add(Entry(documentEntry, name, Packfile.FromBytes(name + ".str2_pc", main),
                [(name + ".vint_doc", 0), (name + ".lua", 1)]));
            containers.Containers.Add(Entry(textureEntry, name + "2", Packfile.FromBytes(name + "2.str2_pc", art),
                [(textureFile + ".cpeg_pc", 0)]));
            documentNames.Add(name);

            report.Add($"{name}: {renamed.Count} pictures renamed, from {needed.Count} textures ({string.Join(", ", needed)}) in {textureFile}" +
                       (unknown.Count > 0 ? $"; not found, left as they are: {string.Join(", ", unknown)}" : ""));
        }

        string listsOut = Path.Combine(cacheOut, "interface.vpp_pc");
        Write(Path.Combine(listsOut, "vint_doc_containers.asm_pc"), containers.ToBytes());

        // The sheet table: the game reads its start-up tables from table.vpp_pc held in memory
        // (0xC8E760), past the overrides, so an override of bitmap_sheets.xtbl itself is never
        // seen. And the project names just one sheet table (only its first <bmp_sheet_file> is
        // read, 0xC502F5). So the project names ours instead: the game's own table with
        // Guerrilla's sheets added, under a name that isn't in table.vpp_pc, so that the game looks
        // it up by name and the loader hands over our new file.
        string projectText = ReadText(gameMisc, "rfg.vint_proj");
        string? sheetTable = sheetsBrought.Count + wholeTextures.Count > 0 ? SheetTableName : null;
        if (sheetTable != null)
        {
            var (at, length) = SheetTableInProject(projectText);
            string gameSheets = ReadText(gameMisc, projectText.Substring(at, length) + ".xtbl");
            Write(Path.Combine(cacheOut, sheetTable + ".xtbl"),
                  Encoding.Latin1.GetBytes(WithSheets(gameSheets, guerrillaSheetsText, sheetsBrought, wholeTextures)));
        }
        string miscOut = Path.Combine(cacheOut, "misc.vpp_pc");
        Write(Path.Combine(miscOut, "rfg.vint_proj"), Encoding.Latin1.GetBytes(WithDocuments(projectText, documentNames, sheetTable)));
        foreach (string old in OldFiles(overridesFolder).Where(File.Exists))
        {
            File.Delete(old);
            report.Add($"Removed {Path.GetFileName(old)}, written by an earlier version");
        }
        report.Add($"Added {string.Join(", ", documentNames)} to vint_doc_containers.asm_pc and rfg.vint_proj" +
                   (sheetTable != null ? $", and Guerrilla's sheets {string.Join(", ", sheetsBrought)} (renamed {Prefix}...) and one-picture " +
                                         $"sheets for {string.Join(", ", wholeTextures.Select(w => w.Name))} to the game's sheet table as " +
                                         $"{sheetTable}.xtbl, which rfg.vint_proj now names" : ""));
        return report;
    }

    /// <summary>
    /// Takes away everything <see cref="Build"/> writes into <paramref name="overridesFolder"/>, so
    /// the game is back to its own HUD everywhere. All of it goes together: the project override
    /// names our sheet table, which holds every picture of the game's own HUD too, so the table
    /// must never go while the project override stays. Returns what was removed, a line each.
    /// </summary>
    public static List<string> Remove(string overridesFolder)
    {
        string cacheOut = Path.Combine(overridesFolder, "build", "pc", "cache");
        var files = new List<string>
        {
            Path.Combine(cacheOut, "misc.vpp_pc", "rfg.vint_proj"),
            Path.Combine(cacheOut, "interface.vpp_pc", "vint_doc_containers.asm_pc"),
            Path.Combine(cacheOut, SheetTableName + ".xtbl"),
        };
        foreach (HudDocument hud in Documents)
            foreach (string file in new[] { ".str2_pc", "2.str2_pc", ".vint_doc", ".lua" })
                files.Add(Path.Combine(cacheOut, hud.Name + file));
        files.AddRange(OldFiles(overridesFolder));
        var report = new List<string>();
        foreach (string file in files.Where(File.Exists))
        {
            File.Delete(file);
            report.Add($"Removed {Path.GetRelativePath(overridesFolder, file)}");
        }
        if (report.Count == 0) report.Add("Nothing of Guerrilla's HUD was installed");
        return report;
    }

    /// <summary>Files earlier versions wrote that this one doesn't: an override of bitmap_sheets.xtbl (never read by the game), and the sheet table's old name.</summary>
    static IEnumerable<string> OldFiles(string overridesFolder)
    {
        string cacheOut = Path.Combine(overridesFolder, "build", "pc", "cache");
        yield return Path.Combine(cacheOut, "misc.vpp_pc", "bitmap_sheets.xtbl");
        yield return Path.Combine(cacheOut, "rfg_bitmap_sheets.xtbl");
    }

    static byte[] Read(Packfile archive, string file) =>
        archive.Read(archive.Find(file) ?? throw new FileNotFoundException($"{archive.Name} has no {file}"));

    static string ReadText(Packfile archive, string file) => Encoding.Latin1.GetString(Read(archive, file));

    static Packfile Container(Packfile archive, string file) => Packfile.FromBytes(file, Read(archive, file));

    static PegFile? ReadPeg(Packfile? archive, string file)
    {
        if (archive?.Find(file + ".cpeg_pc") is not { } header || archive.Find(file + ".gpeg_pc") is not { } data) return null;
        return PegFile.Read(archive.Read(header), archive.Read(data));
    }

    static void Write(string path, byte[] bytes)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, bytes);
    }

    /// <summary>
    /// A new texture file pair holding these textures under new names: each texture's 48-byte
    /// entry as it was (but where its data starts, and its name's address cleared), its data on a
    /// 16-byte boundary.
    /// </summary>
    static (byte[] Header, byte[] Data) MergePegs(List<(PegFile Peg, PegTexture Texture, string Name)> textures)
    {
        const int headerSize = 0x18, entrySize = 48;
        var data = new MemoryStream();
        var entries = new byte[textures.Count * entrySize];
        var names = new MemoryStream();
        for (int i = 0; i < textures.Count; i++)
        {
            var (peg, texture, name) = textures[i];
            while (data.Length % 16 != 0) data.WriteByte(0);
            byte[] source = peg.HeaderBytes;
            Array.Copy(source, headerSize + texture.Index * entrySize, entries, i * entrySize, entrySize);
            BitConverter.TryWriteBytes(entries.AsSpan(i * entrySize), (uint)data.Length);
            BitConverter.TryWriteBytes(entries.AsSpan(i * entrySize + 20), 0u);  // its name's address: 0 in Armageddon's files, left-over pointers in Guerrilla's
            data.Write(peg.TextureData(texture));
            names.Write(Encoding.Latin1.GetBytes(name));
            names.WriteByte(0);
        }
        var header = new byte[headerSize + entries.Length + names.Length];
        BitConverter.TryWriteBytes(header.AsSpan(0), PegFile.Magic);
        BitConverter.TryWriteBytes(header.AsSpan(4), PegFile.Version);
        BitConverter.TryWriteBytes(header.AsSpan(6), (ushort)0);                  // platform
        BitConverter.TryWriteBytes(header.AsSpan(8), (uint)header.Length);
        BitConverter.TryWriteBytes(header.AsSpan(12), (uint)data.Length);
        BitConverter.TryWriteBytes(header.AsSpan(16), (ushort)textures.Count);    // bitmaps
        BitConverter.TryWriteBytes(header.AsSpan(20), (ushort)textures.Count);    // textures
        BitConverter.TryWriteBytes(header.AsSpan(22), (ushort)16);                // alignment
        entries.CopyTo(header, headerSize);
        names.ToArray().CopyTo(header, headerSize + entries.Length);
        return (header, data.ToArray());
    }

    /// <summary>
    /// An asset list entry for a new container, copied from one of Armageddon's (its type, flags
    /// and its primitives' types): where the data starts, its compressed size, the size of every
    /// file in it, and per primitive (by the template primitive it copies) its file's size and its
    /// g-file's.
    /// </summary>
    static AsmContainer Entry(AsmContainer template, string name, Packfile container, List<(string File, int Like)> primitives)
    {
        var entry = new AsmContainer
        {
            Name = name,
            Type = template.Type,
            Flags = template.Flags,
            DataOffset = (uint)container.DataStart,
            CompressedSize = container.CompressedDataSize,
        };
        entry.Sizes.AddRange(container.Entries.Select(e => (int)e.Size));
        foreach (var (file, like) in primitives)
        {
            AsmPrimitive model = template.Primitives[like];
            string? dataFile = PegFile.DataFileName(file);
            entry.Primitives.Add(new AsmPrimitive
            {
                Name = file,
                Type = model.Type,
                Allocator = model.Allocator,
                Flags = model.Flags,
                SplitIndex = model.SplitIndex,
                HeaderSize = (int)container.Find(file)!.Size,
                DataSize = dataFile != null && container.Find(dataFile) is { } data ? (int)data.Size : 0,
            });
        }
        return entry;
    }

    /// <summary>Picture name -> the sheet it is cut from, from a bitmap_sheets.xtbl.</summary>
    static Dictionary<string, string> SheetPictures(string sheetsText)
    {
        var sheetOf = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (Match sheet in SheetBlocks().Matches(sheetsText))
        {
            var pictureNames = NameTags().Matches(sheet.Value).Select(m => m.Groups[2].Value).ToList();
            foreach (string picture in pictureNames.Skip(1)) sheetOf.TryAdd(picture, pictureNames[0]);
        }
        return sheetOf;
    }

    /// <summary>
    /// Armageddon's sheet table with Guerrilla's sheets added before its end, every name in them
    /// (the sheet's and its pictures') prefixed, and a sheet of one picture for each whole texture
    /// (the sheet named after its renamed texture, the picture after the picture), laid out as the
    /// game's own entries are.
    /// </summary>
    static string WithSheets(string gameText, string guerrillaText, List<string> sheets, List<(string Name, int Width, int Height)> wholeTextures)
    {
        var added = new StringBuilder();
        foreach (Match block in SheetBlocks().Matches(guerrillaText))
        {
            string sheet = NameTags().Match(block.Value).Groups[2].Value;
            if (!sheets.Contains(sheet, StringComparer.OrdinalIgnoreCase)) continue;
            added.Append(NameTags().Replace(block.Value, m => m.Groups[1].Value + Prefix + m.Groups[2].Value + m.Groups[3].Value));
        }
        foreach (var (name, width, height) in wholeTextures)
            added.Append($"\t<BitmapSheets>\n\t\t<Name>{Prefix}{name}{WholeSheetSuffix}</Name>\n\t\t<Images>\n\t\t\t<Properties>\n" +
                         $"\t\t\t\t<Name>{Prefix}{name}</Name>\n\t\t\t\t<StartX>0</StartX>\n\t\t\t\t<StartY>0</StartY>\n" +
                         $"\t\t\t\t<ImageWidth>{width}</ImageWidth>\n\t\t\t\t<ImageHeight>{height}</ImageHeight>\n\t\t\t\t</Properties>\n" +
                         "\t\t\t</Images>\n\t\t<_Editor>\n\t\t\t<Category>Entries</Category>\n\t\t\t</_Editor>\n\t\t</BitmapSheets>\n");
        int end = gameText.LastIndexOf("</Table>", StringComparison.OrdinalIgnoreCase);
        if (end < 0) throw new InvalidDataException("the game's sheet table has no </Table>");
        int lineStart = gameText.LastIndexOf('\n', end) + 1;
        string newline = gameText.Contains("\r\n") ? "\r\n" : "\n";
        return gameText[..lineStart] + added.ToString().ReplaceLineEndings(newline) + gameText[lineStart..];
    }

    /// <summary>The sheet table the project names (the game reads only its first &lt;bmp_sheet_file&gt;, 0xC502F5): where its file name is.</summary>
    static (int At, int Length) SheetTableInProject(string projectText)
    {
        int entry = projectText.IndexOf("<bmp_sheet_file>", StringComparison.OrdinalIgnoreCase);
        int start = entry < 0 ? -1 : projectText.IndexOf("<filename>", entry, StringComparison.OrdinalIgnoreCase);
        int end = start < 0 ? -1 : projectText.IndexOf("</filename>", start, StringComparison.OrdinalIgnoreCase);
        if (end < 0) throw new InvalidDataException("rfg.vint_proj names no sheet table (<bmp_sheet_file><filename>)");
        start += "<filename>".Length;
        return (start, end - start);
    }

    /// <summary>
    /// Armageddon's interface project with these documents added to the end of its list, and its
    /// sheet table swapped for ours, if there is one.
    /// </summary>
    static string WithDocuments(string projectText, List<string> documents, string? sheetTable)
    {
        string newline = projectText.Contains("\r\n") ? "\r\n" : "\n";
        int end = projectText.LastIndexOf("</documents>", StringComparison.OrdinalIgnoreCase);
        if (end < 0) throw new InvalidDataException("rfg.vint_proj has no </documents>");
        int lineStart = projectText.LastIndexOf('\n', end) + 1;
        var added = new StringBuilder();
        foreach (string document in documents)
            added.Append($"\t<document>{newline}\t\t<filename>{document}.vint_doc</filename>{newline}" +
                         $"\t\t<doc_name>{document}</doc_name>{newline}\t\t</document>{newline}");
        projectText = projectText[..lineStart] + added + projectText[lineStart..];
        if (sheetTable == null) return projectText;
        var (at, length) = SheetTableInProject(projectText);
        return projectText[..at] + sheetTable + projectText[(at + length)..];
    }

    [GeneratedRegex(@"^[ \t]*<BitmapSheets>.*?</BitmapSheets>[ \t]*\r?\n", RegexOptions.Singleline | RegexOptions.Multiline | RegexOptions.IgnoreCase)]
    private static partial Regex SheetBlocks();

    // A name: the opening tag, the name, the closing tag (tags match whatever their case).
    [GeneratedRegex(@"(<Name>)([^<]*)(</Name>)", RegexOptions.IgnoreCase)]
    private static partial Regex NameTags();
}

/// <param name="ScriptPictures">Pictures its script shows that the document doesn't name (they come along too).</param>
public sealed record HudDocument(string Name, IReadOnlyList<string> ScriptPictures);
