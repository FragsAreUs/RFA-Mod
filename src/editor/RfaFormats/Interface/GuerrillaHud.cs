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

    /// <summary>
    /// The documents converted, and the pictures their scripts show besides those the document names.
    /// The mod loads them together in Guerrilla's world, so a texture one of them brings isn't brought
    /// again by a later one: rfg_ui_hud_health brings the sheet ui_bms_00 the others' frames are cut
    /// from (and rfg_ui_hud_reticles' reticles, rfg_ui_hud_weapon_sel's plates), rfg_ui_hud (the
    /// minimap) the map sheet ui_bms_maps,
    /// rfg_ui_fullscreen (the world map) Guerrilla's world map picture.
    /// </summary>
    public static readonly IReadOnlyList<HudDocument> Documents =
    [
        new("rfg_ui_hud_health", ["ui_hud_blood_gradient", "ui_hud_radiation"]),
        new("rfg_ui_hud_reticles", ["ui_hud_reti_assault"], GuerrillaScript: true, WeaponPictures: HudWeaponPictures.Reticles,
            GuerrillaTextSizes: true),
        new("rfg_ui_hud_weapon_sel", [], GuerrillaScript: true, WeaponPictures: HudWeaponPictures.Icons, GuerrillaTextSizes: true),
        // The minimap; its radar sweep's line (ui_minimap_scan_line, a whole texture in the document's own interface-maps,
        // which Guerrilla's cartographer drew itself: render_scan_line RM 0x863A60).
        new("rfg_ui_hud", ["ui_hud_base_square", "ui_map_icon_player", "ui_minimap_scan_line", "ui_map_icon_circ_fill_outline"]),
        new("rfg_ui_fullscreen", ["ui_hud_base_square", "ui_map_icon_player", WorldMap, .. HintKeys], GuerrillaScript: true, BridgedItems: true,
            ExtraTextures: ["interface-pc"], GuerrillaTextSizes: true),
        // Guerrilla's messages: its message lines, its use prompt (a hand, a ladder, a vehicle, a mission's start, the
        // upgrades), the weapon swap, the pickups beside the reticle (weapons, rounds, salvage) and the saving icon (its
        // picture is Guerrilla's utility document's, which isn't brought over); and its upgrade screen (the safehouses'
        // shops: their upgrades, pictures and blueprints from upgrades.xtbl). The upgrade screen's set_header,
        // set_button, set_hints and set_highlight are named as Armageddon's own scripts name theirs, and a global is read
        // from whichever document has it first, so they're renamed rfg_ui_upgrades_....
        new("rfg_ui_messages", ["ui_hud_icon_salvage", "ui_hud_save_icon", "ui_hud_hand_icon", "ui_hud_convoy_car_icon",
                                "ui_map_icon_mission_start", "ui_hud_icon_upgrade", .. GuerrillaHudMessages.Pictures],
            GuerrillaScript: true, BridgedItems: true, WeaponPictures: HudWeaponPictures.Icons, GuerrillaTextSizes: true),
        // An activity's corner panel (Guerrilla's script for every activity and mission, fed by ours: the timer, the
        // building's structure). Loaded with the HUD, so it comes before the screens (the shop's first): a texture is
        // brought by the first document that needs it, and a screen's document is loaded only while it's open.
        new("rfg_ui_hud_missions", [], GuerrillaScript: true, BridgedItems: true, GuerrillaTextSizes: true),
        new("rfg_ui_upgrades", ["ui_hud_icon_salvage"], GuerrillaScript: true, BridgedItems: true, UpgradePictures: true,
            RenamedGlobals: ["set_header", "set_button", "set_hints", "set_highlight"], GuerrillaTextSizes: true),
        // Guerrilla's weapon cabinet (a safehouse's locker, every ammo crate): the weapons found or bought, to put into
        // three of the four slots. Its pictures: the weapons' icons, the teams' logos (the white one in the crate's
        // ammo line), the PC keys 1 2 3 4 for the slots and Esc for back (ui_bms_pc_buttons: key_20 key_43 key_66
        // key_89, key_124). Its generic globals renamed (the weapon select has a handle_icon_scale and handle_highlight).
        new("rfg_ui_safehouse_weapons", ["ui_hud_guerilla_logo_large", "ui_hud_edf_logo_large", "ui_hud_ultor_logo", "ui_hud_marauder_logo",
                                         "ui_hud_guerilla_logo_white", "key_20", "key_43", "key_66", "key_89", "key_124"],
            GuerrillaScript: true, BridgedItems: true, WeaponPictures: HudWeaponPictures.Icons, ExtraTextures: ["interface-pc"],
            RenamedGlobals: ["LOCK", "MAX_ICONS", "handle_icon_scale", "handle_highlight"], GuerrillaTextSizes: true),
        // The campaign's screens: the district-liberated screen (the PC key Enter, key_178, in its hint) and demolitions
        // master's briefing and results (Enter and Esc, key_178 and key_124; the weapons' icons). STAMP_PAD is a global of
        // both rfg_ui_hud_missions and rfg_ui_demolitions: renamed in the latter, which comes and goes.
        new("rfg_ui_district_liberated", ["key_178"], GuerrillaScript: true, BridgedItems: true, ExtraTextures: ["interface-pc"],
            UpgradePictures: true, GuerrillaTextSizes: true),
        new("rfg_ui_demolitions", ["key_178", "key_124"], GuerrillaScript: true, BridgedItems: true, WeaponPictures: HudWeaponPictures.Icons,
            ExtraTextures: ["interface-pc"], RenamedGlobals: ["STAMP_PAD"], GuerrillaTextSizes: true),
        // Guerrilla's activity-complete screen, a Transporter's or a Collateral Damage's results (its raids', house arrest's
        // and area defense's are its mission-complete screen, rfg_ui_mission_complete): Enter and Esc (key_178, key_124)
        // in its hints. Its STAMP_PAD is rfg_ui_hud_missions' too, and its HINT_PAD the world map's: renamed.
        new("rfg_ui_activity_complete", ["key_178", "key_124"], GuerrillaScript: true, BridgedItems: true, ExtraTextures: ["interface-pc"],
            RenamedGlobals: ["STAMP_PAD", "HINT_PAD"], GuerrillaTextSizes: true),
        // Guerrilla's handbook (its pause menu's HANDBOOK): its pages and entries (GuerrillaHandbook, handbook.txt), the
        // weapons' icons its combat page shows, the PC keys A, D and Esc for its hints (ui_bms_pc_buttons: key_48, key_50,
        // key_124). The mod loads it only while it's open, and each entry's own picture from a document of its own, the
        // picked entry's alone (see the handbook pictures below). Its generic globals renamed (rfg_ui_upgrades has an
        // ORANGE and a BROWN, and Armageddon's scripts set_header and set_hints).
        new("rfg_ui_handbook", ["ui_hud_base_square", "key_48", "key_50", "key_124"],
            GuerrillaScript: true, BridgedItems: true, WeaponPictures: HudWeaponPictures.Icons, ExtraTextures: ["interface-pc"],
            RenamedGlobals: ["set_header", "set_page_tab", "add_row", "remove_row", "update_row", "update_row_position", "set_hints",
                             "set_gameplay", "set_actions", "set_combat", "set_vehicles", "set_background", "set_blast_from_past",
                             "set_tool_tips", "set_cutscene", "ORANGE", "DARK_ORANGE", "BROWN", "MAX_ROWS", "MAX_PAGES", "Row_handles",
                             "Num_rows", "Row_base_pos_x", "Row_base_pos_y", "Highlight_bar_size_x", "Highlight_bar_size_y", "Page_base_x",
                             "Page_base_y", "Scroll_bar_height", "Scroll_bar_top_y", "Scroll_bar_top_x", "Scroll_bar_bottom_y",
                             "INFO_ROTATION", "Icon_image_name"], GuerrillaTextSizes: true),
        // Guerrilla's help box: its tool tips (GuerrillaToolTips, tool_tips.txt), one at a time, with their icons. Its
        // generic globals renamed (Armageddon's HUD has a help box of its own).
        new("rfg_ui_hud_help", [], GuerrillaScript: true, BridgedItems: true, GuerrillaTextSizes: true, ToolTipPictures: true,
            RenamedGlobals: ["NUM_SCREEN_COORDS", "Screen_coords", "Screen_coords_handles", "Help_message_main_handle",
                             "Help_title_handle", "Help_text_handle", "Help_icon_handle", "Help_icon2_handle", "Help_arrow_handle",
                             "Help_hint_handle", "Help_bg_main", "Help_bg_body", "Help_bg_bottom", "Help_arrows_anim_handle",
                             "Help_main_anim_handle", "Help_main_alpha_twn_handle", "Help_main_scale_twn_handle", "init_handles_help",
                             "set_help_data", "set_help_language"]),
    ];

    /// <summary>
    /// Guerrilla's PC key pictures the world map's hints show (its sheet ui_bms_pc_buttons, in misc.vpp_pc's
    /// interface-pc; the number isn't a key code: W A S D, the mouse wheel, Esc and X, read off the sheet).
    /// </summary>
    static string[] HintKeys => ["key_06", "key_48", "key_49", "key_50", "key_133", "key_124", "key_75"];  // a property: Documents, above, reads it

    /// <summary>
    /// Guerrilla's picture of its whole world (terr01_l0.vpp_pc &gt; terr01_map.str2_pc), 1024 × 1024:
    /// its zone columns and rows 2 to 13, 85⅓ pixels a zone, north up. Its full-screen map shows it.
    /// </summary>
    public const string WorldMap = "terr01_map";

    /// <summary>
    /// Guerrilla's minimap shows each zone's own picture (terr01_l0.vpp_pc &gt; terr01_&lt;col&gt;_&lt;row&gt;_map.str2_pc,
    /// 512 × 512, a metre a pixel, north up), each in a container of its own, which its streaming
    /// loaded with the zone. Armageddon's interface can't stream them so: its memory for documents is a
    /// stack, and unloading one out of order frees every document loaded after it while they're still
    /// on screen (2026-11-07: three freezes). So every zone's picture, as Guerrilla made it, comes in one
    /// of four documents by quarter of the world (rfg_ui_map_zones_nw, _ne, _sw, _se: columns below 8 are
    /// west, rows below 8 north), which the mod loads with the HUD and keeps until the next level load.
    /// </summary>
    public static string ZoneSetDocument(int column, int row) =>
        "rfg_ui_map_zones_" + (row < 8 ? "n" : "s") + (column < 8 ? "w" : "e");
    static readonly string[] ZoneSets = ["rfg_ui_map_zones_nw", "rfg_ui_map_zones_ne", "rfg_ui_map_zones_sw", "rfg_ui_map_zones_se"];
    const int FirstZone = 2, LastZone = 13;

    /// <summary>The districts' names by zone, for the minimap (written next to our scripts, made from the player's copy).</summary>
    public const string DistrictsFile = "districts.txt";
    const string Language = "us";  // Guerrilla's strings files: territories_us.le_strings, ...

    /// <summary>
    /// Pictures from textures that aren't brought over, and one that looks the same that is: both
    /// are a single white, opaque pixel (checked in Guerrilla's sheets).
    /// </summary>
    static readonly Dictionary<string, string> SamePicture = new(StringComparer.OrdinalIgnoreCase)
    {
        ["ui_fend_base_square"] = "ui_hud_base_square",
    };

    static readonly uint ImageHash = VintDocument.Hash("image");
    static readonly uint FontHash = VintDocument.Hash("font");
    static readonly uint TextScaleHash = VintDocument.Hash("text_scale");
    static readonly uint ScaleHash = VintDocument.Hash("scale");

    /// <summary>
    /// How much smaller (or bigger) a text in Guerrilla's font <paramref name="font"/> was than Armageddon's font of
    /// the same name draws it: Guerrilla's font's height over Armageddon's (a .vf3_pc's u16 at 0x16, the height the
    /// game measures text with), or null if either game lacks it or they're the same. Measured 2026-11-12: font_body
    /// 33 / 50 (its letters' widths 0.66 of Armageddon's too: Armageddon's is half as big again); font_header 43 / 49
    /// (but Guerrilla's letters half as wide again as Armageddon's narrow ones: the height is what's matched);
    /// font_numbers the same in both. font_latin_numbers and font_header_latin_numbers are names, not files: Guerrilla
    /// gives them its font_body and font_header (its ui_initialize_fonts), and guerrilla_hud gives them Armageddon's,
    /// so they're measured as those.
    /// </summary>
    static float? FontScale(Packfile guerrillaInterface, Packfile gameInterface, string font)
    {
        static int? Height(Packfile archive, string font) =>
            archive.Find(font + ".vf3_pc") is { } entry && archive.Read(entry) is { Length: >= 0x18 } data &&
            BitConverter.ToUInt32(data, 0) == 0x544E4656 /* VFNT */ ? BitConverter.ToUInt16(data, 0x16) : null;
        font = font.ToLowerInvariant() switch
        {
            "font_latin_numbers" => "font_body",
            "font_header_latin_numbers" => "font_header",
            _ => font,
        };
        int? guerrilla = Height(guerrillaInterface, font), armageddon = Height(gameInterface, font);
        if (guerrilla is not > 0 || armageddon is not > 0 || guerrilla == armageddon) return null;
        return (float)guerrilla.Value / armageddon.Value;
    }

    // Our sheet table, in place of the game's bitmap_sheets. Not named rfg_..., so that taking the
    // world's rfg_* files away never takes it with them (see Remove).
    public const string SheetTableName = "guerrilla_hud_sheets";
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
        // Guerrilla's weapons' own pictures (Guerrilla.GuerrillaWeapons.HudPictures): their icons come with the
        // weapon select, their reticles with the reticles.
        var weaponPictures = Guerrilla.GuerrillaWeapons.HudPictures(guerrillaMisc);

        // Guerrilla's world: its map picture, and each zone's own (see ZoneDocument).
        Packfile guerrillaWorld = Packfile.Open(Path.Combine(guerrillaCache, "terr01_l0.vpp_pc"));
        PegFile worldMap = ReadBitmap(guerrillaWorld, WorldMap)
                           ?? throw new InvalidDataException($"Guerrilla's terr01_l0.vpp_pc has no {WorldMap}");

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
        var texturesBrought = new HashSet<string>(StringComparer.OrdinalIgnoreCase);  // by an earlier document
        var broughtByUpgradeScreens = new HashSet<string>(StringComparer.OrdinalIgnoreCase);  // by one showing upgrades
        foreach (HudDocument hud in Documents)
        {
            string name = hud.Name;
            string script = Path.Combine(scriptsFolder, name + ".lua");
            if (!File.Exists(script)) throw new FileNotFoundException($"our script for {name} is missing", script);
            var document = VintDocument.Read(Read(guerrillaInterface, name + ".vint_doc"));

            // Its texts at Guerrilla's sizes (HudDocument.GuerrillaTextSizes): each text_scale times its font's
            // FontScale, and the shims told which elements, for the text_scale Guerrilla's script sets.
            var textScales = new List<(string Element, float Scale)>();
            // Its texts with no size of their own in the document (no text_scale or scale to change: a document's values
            // are changed in place, not added): the shims give them their text_scale as the document starts, before its
            // init (2026-11-19: the Transporter's results screen's PRO / TARGET / BEST TIME labels, at Armageddon's
            // bigger font, ran into each other).
            var unsizedTexts = new List<(string Element, float Scale)>();
            if (hud.GuerrillaTextSizes)
            {
                var fontScales = new Dictionary<string, float?>(StringComparer.OrdinalIgnoreCase);
                int scaledByElement = 0;
                foreach (VintElement element in document.Elements().Where(e => e.Type.Equals("text", StringComparison.OrdinalIgnoreCase)))
                {
                    if (element.Properties.FirstOrDefault(p => p.NameHash == FontHash && p.ValueType == VintDocument.StringValue) is not { } fontProperty)
                        continue;
                    string font = document.Text(fontProperty.AsReference);
                    if (!fontScales.TryGetValue(font, out float? scale))
                        fontScales[font] = scale = FontScale(guerrillaInterface, gameInterface, font);
                    if (scale is not { } by) continue;
                    var sizes = element.Properties.Where(p => p.NameHash == TextScaleHash && p.ValueType == 7).ToList();
                    bool byElementScale = sizes.Count == 0;
                    // Some texts are sized by the element's own scale instead (the shop's rows): that, then. None of
                    // Guerrilla's scripts sets those texts' scale, so the shims needn't know them.
                    if (byElementScale) sizes = element.Properties.Where(p => p.NameHash == ScaleHash && p.ValueType == 7).ToList();
                    if (sizes.Count == 0)
                    {
                        unsizedTexts.Add((element.Name, by));  // sized by the shims as the document starts
                        continue;
                    }
                    foreach (VintProperty size in sizes)
                    {
                        var (x, y) = document.Vector2(size);
                        document.SetVector2(size, x * by, y * by);
                    }
                    if (byElementScale) scaledByElement++;
                    else textScales.Add((element.Name, by));
                }
                report.Add($"{name}: {textScales.Count + scaledByElement} texts made Guerrilla's size (" +
                           string.Join(", ", fontScales.Where(f => f.Value != null).Select(f => $"{f.Key} x{f.Value:0.###}")) + ")" +
                           (scaledByElement > 0 ? $", {scaledByElement} of them by their element's scale" : "") +
                           (unsizedTexts.Count == 0 ? "" : hud.GuerrillaScript
                               ? $"; {unsizedTexts.Count} with no size of their own sized as it starts ({string.Join(", ", unsizedTexts.Select(t => t.Element))})"
                               : $"; {unsizedTexts.Count} with no size of their own left as they are (no Guerrilla script to size them)"));
            }

            // The textures it can show: those of the texture files it depends on, from Guerrilla's
            // misc.vpp_pc (always loaded there) or the document's own texture container.
            Packfile? guerrillaTextures = guerrillaInterface.Find(name + ".vint_doc2.str2_pc") is { } entry
                ? Packfile.FromBytes(entry.Name, guerrillaInterface.Read(entry)) : null;
            var textures = new Dictionary<string, (PegFile Peg, PegTexture Texture)>(StringComparer.OrdinalIgnoreCase);
            foreach (VintReference dependency in document.Dependencies.Where(d => d.Kind == 0))
            {
                string file = document.Text(dependency);
                PegFile? peg = ReadPeg(guerrillaTextures, file) ?? ReadPeg(guerrillaMisc, file);
                if (peg == null)
                {
                    report.Add($"{name}: Guerrilla's texture file {file} not found; its pictures are left out");
                    continue;
                }
                foreach (PegTexture texture in peg.Textures) textures.TryAdd(Path.GetFileNameWithoutExtension(texture.Name), (peg, texture));
            }
            foreach (PegTexture texture in worldMap.Textures) textures.TryAdd(Path.GetFileNameWithoutExtension(texture.Name), (worldMap, texture));
            foreach (string extra in hud.ExtraTextures ?? [])
                if (ReadPeg(guerrillaMisc, extra) is { } extraPeg)
                    foreach (PegTexture texture in extraPeg.Textures) textures.TryAdd(Path.GetFileNameWithoutExtension(texture.Name), (extraPeg, texture));
                else report.Add($"{name}: Guerrilla's texture file {extra} not found; its pictures are left out");
            // Guerrilla's upgrades' blueprints: each a picture of its own in interface.vpp_pc (ui_blueprint_<x>.str2_pc).
            var upgradePictures = hud.UpgradePictures ? UpgradePictures(ReadText(guerrillaMisc, "upgrades.xtbl")) : [];
            foreach (var (_, _, blueprint) in upgradePictures)
                if (blueprint != null && ReadBitmap(guerrillaInterface, blueprint) is { } drawing)
                    foreach (PegTexture texture in drawing.Textures) textures.TryAdd(Path.GetFileNameWithoutExtension(texture.Name), (drawing, texture));

            // Guerrilla's map elements were drawn by its game code (the cartographer), which
            // Armageddon has only as a compass. Here each is a plain picture (its own picture, the
            // minimap's frame for one), and our script builds the map around it.
            int maps = 0;
            foreach (VintElement element in document.Elements().Where(e => e.Type.Equals("map", StringComparison.OrdinalIgnoreCase)))
            {
                document.Point(element.TypeReference, "bitmap");
                maps++;
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
            IEnumerable<string> scriptPictures = hud.WeaponPictures switch
            {
                HudWeaponPictures.Icons => hud.ScriptPictures.Concat(weaponPictures.Select(w => w.Icon)),
                HudWeaponPictures.Reticles => hud.ScriptPictures.Concat(weaponPictures.SelectMany(w => new[] { w.Reticle, w.FineAimReticle }).OfType<string>()),
                _ => hud.ScriptPictures,
            };
            scriptPictures = scriptPictures.Concat(upgradePictures.SelectMany(u => new[] { u.Icon, u.Blueprint }).OfType<string>());
            if (hud.ToolTipPictures) scriptPictures = scriptPictures.Concat(GuerrillaToolTips.Icons(guerrillaMisc));
            foreach (string picture in scriptPictures.Distinct(StringComparer.OrdinalIgnoreCase))
                if (TextureFor(picture) is { } texture && !needed.Contains(texture, StringComparer.OrdinalIgnoreCase)) needed.Add(texture);
                else if (TextureFor(picture) == null) report.Add($"{name}: its script's picture {picture} isn't in Guerrilla's textures");
            // Guerrilla's own script, when it runs here too: the pictures it names come along.
            string? guerrillaScript = hud.GuerrillaScript
                ? ReadText(Container(guerrillaInterface, name + ".vint_doc.str2_pc"), name + ".lua") : null;
            if (guerrillaScript != null)
                foreach (Match literal in ScriptString().Matches(guerrillaScript))
                    if (sheetOf.ContainsKey(literal.Groups[1].Value) && TextureFor(literal.Groups[1].Value) is { } texture &&
                        !needed.Contains(texture, StringComparer.OrdinalIgnoreCase)) needed.Add(texture);
            // The screens showing upgrades (the upgrade shop, the district-liberated screen) are each loaded only while
            // open, and never two at once: a texture one of them brought (the blueprints, each a texture of its own,
            // and the icons' sheet) isn't there while another is open, so that one brings its own copy.
            bool BroughtBefore(string texture) => texturesBrought.Contains(texture) && !(hud.UpgradePictures && broughtByUpgradeScreens.Contains(texture));
            var shared = needed.Where(BroughtBefore).ToList();
            needed.RemoveAll(BroughtBefore);
            foreach (string texture in needed) texturesBrought.Add(texture);
            if (hud.UpgradePictures) broughtByUpgradeScreens.UnionWith(needed);

            // One texture file with every texture it needs, renamed. The document waits for its
            // first texture file; any others it named point at one Armageddon always has loaded.
            // A picture that is a whole texture becomes the one picture of a sheet of its own
            // (the texture renamed <picture>_sheet): Armageddon finds sheet pictures through the
            // sheets it ties to their textures at start-up, which works for ours, while a whole
            // texture asked for by name came out as some other bitmap (black, with stray shapes).
            // A document whose textures all came with the documents before it brings none: its
            // texture container is empty, as Armageddon's own ui_fade_to_black2 is, and every
            // texture file it named points at one Armageddon always has loaded.
            string textureFile = "interface-" + name;
            bool ownTextures = needed.Count > 0;
            var dependencies = document.Dependencies.Where(d => d.Kind == 0).ToList();
            for (int i = 0; i < dependencies.Count; i++) document.Point(dependencies[i], i == 0 && ownTextures ? textureFile : AlwaysLoaded);
            bool IsSheet(string texture) => sheetOf.ContainsValue(texture);
            (byte[] pegHeader, byte[] pegData) = ownTextures ? MergePegs(needed.Select(t =>
                (textures[t].Peg, textures[t].Texture, Prefix + t + (IsSheet(t) ? "" : WholeSheetSuffix) + ".tga")).ToList()) : ([], []);
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
            byte[] scriptBytes = guerrillaScript != null
                ? Encoding.Latin1.GetBytes(GuerrillaScriptFor(name, guerrillaScript,
                                                              File.ReadAllText(Path.Combine(scriptsFolder, ShimsScript), Encoding.Latin1),
                                                              TextScalesScript(name, textScales, unsizedTexts) +
                                                              (hud.WeaponPictures == HudWeaponPictures.Reticles ? WeaponReticlesScript(weaponPictures) : "") +
                                                              (hud.UpgradePictures ? UpgradePicturesScript(upgradePictures, picture => TextureFor(picture) != null) : "") +
                                                              File.ReadAllText(script, Encoding.Latin1) +
                                                              UnsizedTextsScript(name, unsizedTexts), sheetOf.Keys, hud.BridgedItems,
                                                              hud.RenamedGlobals ?? []))
                : File.ReadAllBytes(script);
            byte[] main = Packfile.CreateContainer(documentTemplate, [(name + ".vint_doc", documentBytes), (name + ".lua", scriptBytes)]);
            byte[] art = Packfile.CreateContainer(textureTemplate, ownTextures
                ? [(textureFile + ".cpeg_pc", pegHeader), (textureFile + ".gpeg_pc", pegData)] : []);
            Write(Path.Combine(cacheOut, name + ".str2_pc"), main);
            Write(Path.Combine(cacheOut, name + "2.str2_pc"), art);
            Write(Path.Combine(cacheOut, name + ".vint_doc"), documentBytes);
            Write(Path.Combine(cacheOut, name + ".lua"), scriptBytes);

            containers.Containers.RemoveAll(c => c.Name.Equals(name, StringComparison.OrdinalIgnoreCase) ||
                                                 c.Name.Equals(name + "2", StringComparison.OrdinalIgnoreCase));
            containers.Containers.Add(Entry(documentEntry, name, Packfile.FromBytes(name + ".str2_pc", main),
                [(name + ".vint_doc", 0), (name + ".lua", 1)]));
            containers.Containers.Add(Entry(textureEntry, name + "2", Packfile.FromBytes(name + "2.str2_pc", art),
                ownTextures ? [(textureFile + ".cpeg_pc", 0)] : []));
            documentNames.Add(name);

            report.Add($"{name}: {renamed.Count} pictures renamed, from " +
                       (ownTextures ? $"{needed.Count} textures ({string.Join(", ", needed)}) in {textureFile}" : "no texture of its own") +
                       (hud.GuerrillaScript ? "; Guerrilla's own script" : "") +
                       (shared.Count > 0 ? $", and {string.Join(", ", shared)} from the documents before it" : "") +
                       (maps > 0 ? $"; {maps} map element{(maps == 1 ? "" : "s")} made a picture" : "") +
                       (unknown.Count > 0 ? $"; not found, left as they are: {string.Join(", ", unknown)}" : ""));
        }

        // Each zone's own picture, in the document of its quarter of the world (see ZoneSetDocument), which
        // brings nothing but those textures.
        int zonePicturesBytes = 0, zonePictures = 0;
        var zoneSets = new Dictionary<string, List<(PegFile Peg, PegTexture Texture, string Name)>>(StringComparer.OrdinalIgnoreCase);
        foreach (PackfileEntry entry in guerrillaWorld.Entries)
        {
            Match zone = ZoneMap().Match(entry.Name);
            if (!zone.Success) continue;
            string tile = Path.GetFileNameWithoutExtension(entry.Name);
            PegFile? peg = ReadBitmap(guerrillaWorld, tile);
            if (peg == null || peg.Textures.Count != 1) continue;
            string set = ZoneSetDocument(int.Parse(zone.Groups[1].Value), int.Parse(zone.Groups[2].Value));
            if (!zoneSets.TryGetValue(set, out var setTextures)) zoneSets[set] = setTextures = [];
            setTextures.Add((peg, peg.Textures[0], Prefix + tile + WholeSheetSuffix + ".tga"));
            wholeTextures.Add((tile, peg.Textures[0].Width, peg.Textures[0].Height));
            zonePictures++;
        }
        foreach (var (name, setTextures) in zoneSets)
        {
            string textureFile = "interface-" + name;
            (byte[] pegHeader, byte[] pegData) = MergePegs(setTextures);
            byte[] documentBytes = VintDocument.TexturesOnly(textureFile, name, name + ".lua");
            byte[] scriptBytes = Encoding.Latin1.GetBytes(TilesScript(name));
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
            containers.Containers.Add(Entry(textureEntry, name + "2", Packfile.FromBytes(name + "2.str2_pc", art), [(textureFile + ".cpeg_pc", 0)]));
            documentNames.Add(name);
            zonePicturesBytes += pegData.Length;
            report.Add($"{name}: {setTextures.Count} zones' own pictures ({pegData.Length / 1024} KB)");
        }
        report.Add($"rfg_ui_map_zones_*: {zonePictures} zones' own pictures in {zoneSets.Count} documents ({zonePicturesBytes / 1024} KB in all)");

        // The handbook's entries' pictures, each in a document of its own (GuerrillaHandbook.PictureDocument), as
        // Guerrilla streamed each entry's container ui_handbook_<x>.str2_pc while its entry was shown: the mod loads the
        // picked entry's alone, while the handbook is open. All of them at once took 13 MB of the interface's memory
        // and ran it out as the world loaded (2026-11-12).
        int handbookPictures = 0, handbookBytes = 0, handbookLargest = 0, handbookConverted = 0;
        foreach (string picture in GuerrillaHandbook.Pictures(guerrillaMisc))
        {
            PegFile? peg = ReadBitmap(guerrillaInterface, picture);
            if (peg == null || peg.Textures.Count != 1)
            {
                report.Add($"Guerrilla's handbook picture {picture} not found; its entry shows none");
                continue;
            }
            // Guerrilla's handbook pictures are 16-bit RGB 565 (format 403), which Armageddon's Direct3D 11 interface
            // doesn't draw (its own pictures are DXT or 32-bit: a 565 one came out a small grey box, 2026-11-12): made
            // 32-bit ARGB, as the blueprints are, opaque.
            PegTexture source = peg.Textures[0];
            if (source.Format is not (TextureFormat.Dxt1 or TextureFormat.Dxt3 or TextureFormat.Dxt5 or TextureFormat.Argb8888))
            {
                byte[] pixels = TextureCodec.Encode(TextureFormat.Argb8888, peg.Decode(source));
                byte[] header = PegFile.SingleHeader(source.Name, source.Width, source.Height, TextureFormat.Argb8888, 1, source.Flags);
                var padded = new byte[(pixels.Length + 15) & ~15];
                Array.Copy(pixels, padded, pixels.Length);
                peg = PegFile.Read(header, padded);
                handbookConverted++;
            }
            string name = GuerrillaHandbook.PictureDocument(picture);
            string textureFile = "interface-" + name;
            (byte[] pegHeader, byte[] pegData) = MergePegs([(peg, peg.Textures[0], Prefix + picture + WholeSheetSuffix + ".tga")]);
            byte[] documentBytes = VintDocument.TexturesOnly(textureFile, name, name + ".lua");
            byte[] scriptBytes = Encoding.Latin1.GetBytes(HandbookPictureScript(name));
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
            containers.Containers.Add(Entry(textureEntry, name + "2", Packfile.FromBytes(name + "2.str2_pc", art), [(textureFile + ".cpeg_pc", 0)]));
            documentNames.Add(name);
            wholeTextures.Add((picture, peg.Textures[0].Width, peg.Textures[0].Height));
            handbookPictures++;
            handbookBytes += pegData.Length;
            handbookLargest = Math.Max(handbookLargest, pegData.Length);
        }
        report.Add($"{GuerrillaHandbook.PictureDocumentPrefix}*: {handbookPictures} handbook pictures, a document each (one loaded at a time: " +
                   $"{handbookLargest / 1024} KB at most; {handbookBytes / 1024} KB in all; {handbookConverted} made 32-bit from 16-bit)");

        // The districts' names by zone, in the player's language, for the minimap.
        List<string> districts = DistrictLines(guerrillaMisc, report);
        Directory.CreateDirectory(scriptsFolder);
        File.WriteAllLines(Path.Combine(scriptsFolder, DistrictsFile), districts);
        // Guerrilla's map icons and places, for the minimap and the world map.
        report.AddRange(GuerrillaMapIcons.Build(guerrillaCache, scriptsFolder));
        // What the safehouses' lockers and the ammo crates' weapon cabinet shows and goes by.
        report.AddRange(GuerrillaWeaponCabinet.Build(guerrillaCache, scriptsFolder));
        // The handbook's pages and entries.
        report.AddRange(GuerrillaHandbook.Build(guerrillaCache, scriptsFolder));
        report.AddRange(GuerrillaToolTips.Build(guerrillaCache, scriptsFolder));
        // The texts of the HUD messages the campaign raises.
        report.AddRange(GuerrillaHudMessages.Build(guerrillaCache, scriptsFolder));

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
        var wholeNamed = wholeTextures.Where(w => !ZoneMap().IsMatch(w.Name + ".str2_pc")).Select(w => w.Name).ToList();
        var namedDocuments = documentNames.Where(d => !d.StartsWith("rfg_ui_map_", StringComparison.OrdinalIgnoreCase) &&
                                                      !d.StartsWith(GuerrillaHandbook.PictureDocumentPrefix, StringComparison.OrdinalIgnoreCase)).ToList();
        report.Add($"Added {string.Join(", ", namedDocuments)}" +
                   (documentNames.Count > namedDocuments.Count ? $" and {documentNames.Count - namedDocuments.Count} zone and handbook pictures' documents" : "") +
                   " to vint_doc_containers.asm_pc and rfg.vint_proj" +
                   (sheetTable != null ? $", and Guerrilla's sheets {string.Join(", ", sheetsBrought)} (renamed {Prefix}...) and one-picture " +
                                         $"sheets for {string.Join(", ", wholeNamed)}" +
                                         (zonePictures > 0 ? $" and {zonePictures} zone pictures" : "") +
                                         $" to the game's sheet table as {sheetTable}.xtbl, which rfg.vint_proj now names" : ""));
        // RFA-Mod's UI pack shares the asset list, project and sheet table just written from the game's: added again.
        if (UiPack.Installed(overridesFolder)) report.AddRange(UiPack.Build(gameFolder, overridesFolder));
        return report;
    }

    /// <summary>
    /// Takes away everything <see cref="Build"/> writes into <paramref name="overridesFolder"/>, so
    /// the game is back to its own HUD everywhere. All of it goes together: the project override
    /// names our sheet table, which holds every picture of the game's own HUD too, so the table
    /// must never go while the project override stays. Returns what was removed, a line each.
    /// </summary>
    public static List<string> Remove(string overridesFolder, string? scriptsFolder = null)
    {
        string cacheOut = Path.Combine(overridesFolder, "build", "pc", "cache");
        var files = new List<string>
        {
            Path.Combine(cacheOut, "misc.vpp_pc", "rfg.vint_proj"),
            Path.Combine(cacheOut, "interface.vpp_pc", "vint_doc_containers.asm_pc"),
            Path.Combine(cacheOut, SheetTableName + ".xtbl"),
        };
        foreach (string document in Documents.Select(d => d.Name).Concat(ZoneDocuments()))
            foreach (string file in new[] { ".str2_pc", "2.str2_pc", ".vint_doc", ".lua" })
                files.Add(Path.Combine(cacheOut, document + file));
        if (Directory.Exists(cacheOut))
            files.AddRange(Directory.GetFiles(cacheOut, GuerrillaHandbook.PictureDocumentPrefix + "*"));
        if (scriptsFolder != null)
        {
            files.Add(Path.Combine(scriptsFolder, DistrictsFile));
            files.Add(Path.Combine(scriptsFolder, GuerrillaMapIcons.FileName));
            files.Add(Path.Combine(scriptsFolder, GuerrillaMapIcons.RadioTagsFile));
            files.Add(Path.Combine(scriptsFolder, GuerrillaMapIcons.SafehousesFile));
            files.Add(Path.Combine(scriptsFolder, GuerrillaWeaponCabinet.FileName));
            files.Add(Path.Combine(scriptsFolder, GuerrillaHandbook.FileName));
            files.Add(Path.Combine(scriptsFolder, GuerrillaToolTips.FileName));
            files.Add(Path.Combine(scriptsFolder, GuerrillaHudMessages.FileName));
        }
        files.AddRange(OldFiles(overridesFolder));
        var report = new List<string>();
        foreach (string file in files.Where(File.Exists))
        {
            File.Delete(file);
            report.Add($"Removed {(file.StartsWith(overridesFolder, StringComparison.OrdinalIgnoreCase) ? Path.GetRelativePath(overridesFolder, file) : file)}");
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
        foreach (string quarter in new[] { "nw", "ne", "sw", "se" })  // the zone pictures, four to a document at first
            foreach (string file in new[] { ".str2_pc", "2.str2_pc", ".vint_doc", ".lua" })
                yield return Path.Combine(cacheOut, "rfg_map_tiles_" + quarter + file);
        for (int column = FirstZone; column <= LastZone; column++)  // then a document a zone (to 2026-11-07)
            for (int row = FirstZone; row <= LastZone; row++)
                foreach (string file in new[] { ".str2_pc", "2.str2_pc", ".vint_doc", ".lua" })
                    yield return Path.Combine(cacheOut, $"rfg_ui_map_{column:00}_{row:00}" + file);
    }

    /// <summary>The zone pictures' documents.</summary>
    static IEnumerable<string> ZoneDocuments() => ZoneSets;

    static byte[] Read(Packfile archive, string file) =>
        archive.Read(archive.Find(file) ?? throw new FileNotFoundException($"{archive.Name} has no {file}"));

    static string ReadText(Packfile archive, string file) => Encoding.Latin1.GetString(Read(archive, file));

    static Packfile Container(Packfile archive, string file) => Packfile.FromBytes(file, Read(archive, file));

    static PegFile? ReadPeg(Packfile? archive, string file)
    {
        if (archive?.Find(file + ".cpeg_pc") is not { } header || archive.Find(file + ".gpeg_pc") is not { } data) return null;
        return PegFile.Read(archive.Read(header), archive.Read(data));
    }

    /// <summary>Our shims for Guerrilla's scripts (what they call that Armageddon's interface Lua lacks), next to our scripts.</summary>
    public const string ShimsScript = "rfg_guerrilla_shims.lua";

    /// <summary>
    /// A document's script made of Guerrilla's own (read from the player's copy) and ours: our
    /// shims first, then Guerrilla's script with its init and cleanup renamed
    /// &lt;document&gt;_guerrilla_init and _guerrilla_cleanup (ours, last, has the document's own and
    /// calls them), every string that names one of Guerrilla's pictures renamed as the document's
    /// pictures are (rfg_...), its property calls made through the shims (tints differ), and its
    /// document clock calls naming the document, then our script.
    /// </summary>
    public static string GuerrillaScriptFor(string document, string guerrillaScript, string shims, string ours, IEnumerable<string> pictures,
                                            bool bridgedItems = false, IReadOnlyList<string>? renamedGlobals = null)
    {
        var pictureNames = new HashSet<string>(pictures, StringComparer.OrdinalIgnoreCase);
        string script = Regex.Replace(guerrillaScript, $@"\b{Regex.Escape(document)}_(init|cleanup)\b", m => $"{document}_guerrilla_{m.Groups[1].Value}");
        // Its globals named as another document's are (see HudDocument.RenamedGlobals): <document>_<name>.
        foreach (string global in renamedGlobals ?? [])
            script = Regex.Replace(script, $@"(?<![A-Za-z0-9_]){Regex.Escape(global)}(?![A-Za-z0-9_])", $"{document}_{global}");
        script = ScriptString().Replace(script, m => pictureNames.Contains(m.Groups[1].Value) ? $"\"{Prefix}{m.Groups[1].Value}\"" : m.Value);
        // Properties through the shims' rfg_vint_set_property / rfg_vint_get_property: Armageddon's
        // Lua takes and gives tints as 0-255, Guerrilla's scripts as 0-1.
        script = PropertyCall().Replace(script, m => "rfg_" + m.Value);
        // Data items our script hands Guerrilla's callbacks as tables (see HudDocument.BridgedItems).
        if (bridgedItems) script = DataItemCall().Replace(script, m => "rfg_" + m.Value);
        // The document's clock: both games read and set the clock of the document whose script is
        // running when no document is named; here each call names its own.
        string thisDocument = $"vint_document_find(\"{document}\")";
        script = GetTimeIndexCall().Replace(script, $"vint_get_time_index({thisDocument})");
        script = SetTimeIndexCall().Replace(script, m => $"vint_set_time_index({m.Groups[1].Value.Trim()}, {thisDocument})");
        const string newline = "\r\n";
        return shims.TrimEnd() + newline + newline +
               $"-- ---- Guerrilla's own script for {document}, from your copy of Guerrilla" + newline +
               script.TrimEnd() + newline + newline +
               $"-- ---- Ours for {document} (guerrilla_hud)" + newline +
               ours;
    }

    /// <summary>
    /// For a document whose texts were made Guerrilla's size (HudDocument.GuerrillaTextSizes): which, and by how much,
    /// for the shims' text_scale (rfg_text_scales). Nothing for the others.
    /// </summary>
    static string TextScalesScript(string document, IReadOnlyList<(string Element, float Scale)> scales,
                                   IReadOnlyList<(string Element, float Scale)> unsized)
    {
        if (scales.Count == 0 && unsized.Count == 0) return "";
        const string newline = "\r\n";
        var text = new StringBuilder();
        text.Append("-- Its texts made Guerrilla's size by their fonts (GuerrillaBuilder.exe): the text_scale its script sets is too." + newline);
        text.Append("-- Those after the table's end have no size of their own in the document: they're given it as it starts." + newline);
        text.Append($"rfg_text_scales(\"{document}\", {{" + newline);
        foreach (var (element, scale) in scales.Concat(unsized))
            text.Append($"\t{{ \"{element}\", {scale.ToString("0.#####", System.Globalization.CultureInfo.InvariantCulture)} }}," + newline);
        text.Append("}, {" + newline);
        foreach (var (element, _) in unsized)
            text.Append($"\t\"{element}\"," + newline);
        text.Append("})" + newline + newline);
        return text.ToString();
    }

    /// <summary>
    /// After our script: its document's init wrapped, so that the texts with no size of their own (see
    /// TextScalesScript) are given their text_scale first (the shims' rfg_size_unsized_texts), before our init and
    /// Guerrilla's lay anything out. Nothing when it has none.
    /// </summary>
    static string UnsizedTextsScript(string document, IReadOnlyList<(string Element, float Scale)> unsized)
    {
        if (unsized.Count == 0) return "";
        const string newline = "\r\n";
        return newline + newline +
               "-- Its texts with no size of their own in the document sized before anything else (GuerrillaBuilder.exe)." + newline +
               "do" + newline +
               $"\tlocal init_before_sizes = _GetAnyGlobalSilent(\"{document}_init\")" + newline +
               $"\tfunction {document}_init()" + newline +
               "\t\trfg_size_unsized_texts()" + newline +
               "\t\tif init_before_sizes ~= nil then" + newline +
               "\t\t\tinit_before_sizes()" + newline +
               "\t\tend" + newline +
               "\tend" + newline +
               "end" + newline;
    }

    /// <summary>
    /// For the reticles' script: Guerrilla's weapons' reticles by the picture each weapon names as its icon (the
    /// weapons build names Guerrilla's, "rfg_" + it, and the game gives the HUD the held weapon's), a table
    /// Rfg_weapon_reticles of { reticle, reticle while aiming finely (or nil) }. The first weapon of a picture wins.
    /// </summary>
    static string WeaponReticlesScript(IReadOnlyList<(string Weapon, string Icon, string? Reticle, string? FineAimReticle)> weapons)
    {
        const string newline = "\r\n";
        var text = new StringBuilder();
        text.Append("-- Guerrilla's weapons' reticles, by the picture each weapon shows: from your copy's weapons.xtbl (GuerrillaBuilder.exe)." + newline);
        text.Append("Rfg_weapon_reticles = {" + newline);
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var (weapon, icon, reticle, fine) in weapons)
            if (reticle != null && seen.Add(icon))
                text.Append($"\t[\"{Prefix}{icon}\"] = {{ \"{Prefix}{reticle}\", {(fine != null ? $"\"{Prefix}{fine}\"" : "nil")} }},\t-- {weapon}" + newline);
        text.Append("}" + newline + newline);
        return text.ToString();
    }

    /// <summary>
    /// Guerrilla's upgrades' pictures (upgrades.xtbl): each upgrade's Name, its Icon_Image (a weapon's icon) and its
    /// Blueprint (the drawing the upgrade screen shows), either absent.
    /// </summary>
    static List<(string Name, string? Icon, string? Blueprint)> UpgradePictures(string table)
    {
        var pictures = new List<(string, string?, string?)>();
        foreach (Match upgrade in Regex.Matches(table, @"<Upgrade>.*?</Upgrade>", RegexOptions.Singleline | RegexOptions.IgnoreCase))
            if (TagValue(upgrade.Value, "Name") is { } name)
                pictures.Add((name, TagValue(upgrade.Value, "Icon_Image"), TagValue(upgrade.Value, "Blueprint")));
        return pictures;
    }

    /// <summary>
    /// For the upgrade screen's script: Guerrilla's upgrades' pictures by upgrade name, a table Rfg_upgrade_pictures of
    /// { icon, blueprint } (renamed as the document's pictures are; nil for one Guerrilla's sheets don't have).
    /// </summary>
    static string UpgradePicturesScript(List<(string Name, string? Icon, string? Blueprint)> upgrades, Func<string, bool> known)
    {
        const string newline = "\r\n";
        string Picture(string? name) => name != null && known(name) ? $"\"{Prefix}{name}\"" : "nil";
        var text = new StringBuilder();
        text.Append("-- Guerrilla's upgrades' pictures (icon, blueprint) by upgrade: from your copy's upgrades.xtbl (GuerrillaBuilder.exe)." + newline);
        text.Append("Rfg_upgrade_pictures = {" + newline);
        foreach (var (name, icon, blueprint) in upgrades)
            text.Append($"\t[\"{name}\"] = {{ {Picture(icon)}, {Picture(blueprint)} }}," + newline);
        text.Append("}" + newline + newline);
        return text.ToString();
    }

    /// <summary>The script of a document of zone pictures: nothing to do, its init and cleanup only.</summary>
    static string TilesScript(string document) =>
        $"-- {document}: Guerrilla's zone pictures for its minimap, made by GuerrillaBuilder.exe from your copy of Guerrilla.\r\n" +
        "-- The document only brings them (the guerrilla_hud mod loads it with the HUD); the minimap and world map show them.\r\n" +
        $"function {document}_init()\r\nend\r\n\r\nfunction {document}_cleanup()\r\nend\r\n";

    /// <summary>The script of a handbook picture's document: nothing to do, its init and cleanup only.</summary>
    static string HandbookPictureScript(string document) =>
        $"-- {document}: one of Guerrilla's handbook pictures, made by GuerrillaBuilder.exe from your copy of Guerrilla.\r\n" +
        "-- The document only brings it (the guerrilla_hud mod loads it while its entry is shown); the handbook shows it.\r\n" +
        $"function {document}_init()\r\nend\r\n\r\nfunction {document}_cleanup()\r\nend\r\n";

    /// <summary>A single bitmap (&lt;name&gt;.cvbm_pc and .gvbm_pc, laid out as a texture file) in its own container &lt;name&gt;.str2_pc.</summary>
    static PegFile? ReadBitmap(Packfile archive, string name)
    {
        if (archive.Find(name + ".str2_pc") is not { } entry) return null;
        Packfile container = Packfile.FromBytes(entry.Name, archive.Read(entry));
        if (container.Find(name + ".cvbm_pc") is not { } header || container.Find(name + ".gvbm_pc") is not { } data) return null;
        return PegFile.Read(container.Read(header), container.Read(data));
    }

    /// <summary>
    /// Guerrilla's districts by zone, a line each, "CCRR=name": the zones each district of terr01
    /// lists (territories.xtbl, Grid_index), and its name as Guerrilla's HUD shows it, from the
    /// strings file territories_&lt;language&gt;.le_strings by its tag (its Display_name, or
    /// TERR_&lt;its name&gt;; the Tutorial district shows TERR_PARKER), or its own name if that has no
    /// string.
    /// </summary>
    static List<string> DistrictLines(Packfile guerrillaMisc, List<string> report)
    {
        Dictionary<uint, string> strings = guerrillaMisc.Find($"territories_{Language}.le_strings") is { } file
            ? ReadStrings(guerrillaMisc.Read(file)) : [];
        string table = ReadText(guerrillaMisc, "territories.xtbl");
        Match territory = Regex.Match(table, @"<Territory>\s*<Name>terr01</Name>.*?</Territory>", RegexOptions.Singleline | RegexOptions.IgnoreCase);
        if (!territory.Success) throw new InvalidDataException("Guerrilla's territories.xtbl has no terr01");
        table = territory.Value;
        var lines = new List<string>
        {
            "; Guerrilla's districts by zone (column and row), as its HUD names them: made by GuerrillaBuilder.exe from",
            "; your copy of Guerrilla (territories.xtbl and its strings). The minimap shows the name of the zone you're in.",
            "; CCRR=shown name (a zone), name:own name=shown name (a district)",
        };
        int count = 0;
        foreach (Match district in DistrictBlocks().Matches(table))
        {
            string name = TagValue(district.Value, "Name") ?? "";
            string tag = TagValue(district.Value, "Display_name") ?? "TERR_" + name.ToUpperInvariant().Replace(' ', '_');
            string shown = strings.GetValueOrDefault(VintDocument.Hash(tag.ToLowerInvariant()), name);
            foreach (Match index in GridIndexes().Matches(district.Value)) lines.Add($"{index.Groups[1].Value}={shown}");
            // The district by its own name too (the guerrilla_campaign mod names districts so), for the meters.
            if (name.Length > 0) lines.Add($"name:{name}={shown}");
            count++;
        }
        report.Add($"{DistrictsFile}: {count} districts, {lines.Count - 3 - count} zones, names from territories_{Language}.le_strings");
        return lines;
    }

    static string? TagValue(string xml, string tag)
    {
        Match match = Regex.Match(xml, $"<{tag}>([^<]*)</{tag}>", RegexOptions.IgnoreCase);
        return match.Success ? match.Groups[1].Value.Trim() : null;
    }

    /// <summary>
    /// A strings file (.le_strings): signature (u32), version (u16), count (u32), then per string its
    /// tag's hash (u32, <see cref="VintDocument.Hash"/> of the tag in lower case), its length in
    /// bytes (u16) and the text (UTF-16, NUL-terminated), the next string starting on a 4-byte boundary.
    /// </summary>
    internal static Dictionary<uint, string> ReadStrings(byte[] data)
    {
        var strings = new Dictionary<uint, string>();
        int count = (int)BitConverter.ToUInt32(data, 6);
        int at = 10;
        for (int i = 0; i < count && at + 6 <= data.Length; i++)
        {
            uint hash = BitConverter.ToUInt32(data, at);
            int length = BitConverter.ToUInt16(data, at + 4);
            if (at + 6 + length > data.Length) break;
            strings.TryAdd(hash, Encoding.Unicode.GetString(data, at + 6, length).TrimEnd('\0'));
            at = (at + 6 + length + 3) & ~3;
        }
        return strings;
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
    internal static AsmContainer Entry(AsmContainer template, string name, Packfile container, List<(string File, int Like)> primitives)
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
    /// The most sheets and pictures the game takes from the sheet table, once guerrilla_hud has raised its own 196 and 1,536
    /// at start-up (MovePictureTables). The game's reader drops whatever is past them without a word: with 215 sheets and
    /// 1,581 pictures, four zones' minimap pictures and the last handbook pages were missing (2026-11-19).
    /// </summary>
    public const int SheetLimit = 512, PictureLimit = 4096;

    /// <summary>How many sheets, and pictures on them, a sheet table has.</summary>
    public static (int Sheets, int Pictures) SheetTableCounts(string sheetsText)
    {
        int sheets = 0, pictures = 0;
        foreach (Match sheet in SheetBlocks().Matches(sheetsText))
        {
            sheets++;
            pictures += Math.Max(0, NameTags().Matches(sheet.Value).Count - 1);  // the first name is the sheet's
        }
        return (sheets, pictures);
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
    internal static (int At, int Length) SheetTableInProject(string projectText)
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
    internal static string WithDocuments(string projectText, List<string> documents, string? sheetTable)
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

    // A zone's own map picture's container in Guerrilla's world archive: column, row.
    [GeneratedRegex(@"^terr01_(\d\d)_(\d\d)_map\.str2_pc$", RegexOptions.IgnoreCase)]
    private static partial Regex ZoneMap();

    [GeneratedRegex(@"<District>.*?</District>", RegexOptions.Singleline | RegexOptions.IgnoreCase)]
    private static partial Regex DistrictBlocks();

    [GeneratedRegex(@"<Index>\s*(\d{4})\s*</Index>", RegexOptions.IgnoreCase)]
    private static partial Regex GridIndexes();

    // A Lua string that is a single name ("ui_hud_reti_tank"): the name.
    [GeneratedRegex("\"([A-Za-z0-9_]+)\"")]
    private static partial Regex ScriptString();

    // A call of vint_set_property or vint_get_property (the name, before its "(").
    [GeneratedRegex(@"(?<![A-Za-z0-9_])vint_[sg]et_property(?=\s*\()")]
    private static partial Regex PropertyCall();

    // A call of vint_dataitem_get, vint_dataitem_add_subscription or vint_datagroup_add_subscription (the name).
    [GeneratedRegex(@"(?<![A-Za-z0-9_])vint_data(item_get|item_add_subscription|group_add_subscription)(?=\s*\()")]
    private static partial Regex DataItemCall();

    // vint_get_time_index() with no document named.
    [GeneratedRegex(@"(?<![A-Za-z0-9_])vint_get_time_index\s*\(\s*\)")]
    private static partial Regex GetTimeIndexCall();

    // vint_set_time_index(time) with no document named: the time.
    [GeneratedRegex(@"(?<![A-Za-z0-9_])vint_set_time_index\s*\(([^(),]*)\)")]
    private static partial Regex SetTimeIndexCall();
}

/// <param name="ScriptPictures">Pictures its script shows that the document doesn't name (they come along too).</param>
/// <param name="GuerrillaScript">
/// Its script is Guerrilla's own, from the player's copy, between our shims and our script for it
/// (see <see cref="GuerrillaHud.GuerrillaScriptFor"/>); otherwise it is our script alone.
/// </param>
/// <param name="WeaponPictures">Guerrilla's weapons' pictures it shows too: their icons (the weapon select) or their reticles.</param>
/// <param name="BridgedItems">
/// Guerrilla's script reads data items Armageddon has nothing like (the world map's, which Guerrilla's
/// game code filled): our script calls its callbacks itself with a table of the values, so its
/// vint_dataitem_get and subscription calls go through the shims (rfg_vint_dataitem_get, ...).
/// </param>
/// <param name="ExtraTextures">Guerrilla's texture files (in its misc.vpp_pc) its script's pictures come from besides the document's own.</param>
/// <param name="UpgradePictures">It shows Guerrilla's upgrades' pictures (upgrades.xtbl): they come along, and its script gets Rfg_upgrade_pictures.</param>
/// <param name="RenamedGlobals">Globals of Guerrilla's script named as some of Armageddon's scripts name theirs: renamed &lt;document&gt;_&lt;name&gt;.</param>
/// <param name="GuerrillaTextSizes">
/// Its texts drawn at Guerrilla's sizes: Armageddon's fonts of the same names are bigger, so each text's text_scale (or,
/// lacking one, its scale) is scaled by its font (GuerrillaHud.FontScale), and so is any text_scale its script sets
/// (the shims, told by rfg_text_scales).
/// </param>
/// <param name="ToolTipPictures">It shows Guerrilla's tool tips (tool_tips.xtbl): their icons come along.</param>
public sealed record HudDocument(string Name, IReadOnlyList<string> ScriptPictures, bool GuerrillaScript = false,
                                 HudWeaponPictures WeaponPictures = HudWeaponPictures.None, bool BridgedItems = false,
                                 IReadOnlyList<string>? ExtraTextures = null, bool UpgradePictures = false,
                                 IReadOnlyList<string>? RenamedGlobals = null, bool GuerrillaTextSizes = false,
                                 bool ToolTipPictures = false);

/// <summary>Which of Guerrilla's weapons' pictures a HUD document brings along (see <see cref="GuerrillaHud.Build"/>).</summary>
public enum HudWeaponPictures { None, Icons, Reticles }
