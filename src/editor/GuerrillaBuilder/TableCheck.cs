using System.Globalization;
using System.IO;
using System.Text;
using RfaFormats;
using RfaFormats.Guerrilla;
using RfaFormats.Tables;

namespace GuerrillaBuilder;

/// <summary>
/// Checks every table a build wrote (the .xtbl files in mods\ and overrides\) against the table's own
/// description, as Armageddon's table reader takes them at start-up, so what would stop the game with
/// "Could not find required attribute ...", "... is not a Boolean value" or the like shows up in the
/// build instead. For each entry and each field the description names, recursively (groups, the
/// items of lists and grids, the chosen group of a combo): a required field that is missing, a
/// number that isn't one or is out of range, a choice that isn't one of the description's choices
/// (Armageddon's yes/no fields are choices), a flag that isn't one of its flags.
///
/// The descriptions mark nearly every field required, and Armageddon's own entries leave many of
/// them out without harm; so a problem counts only if no entry of Armageddon's own copy of the
/// table has it (same kind, same field): what the game's own entries do, its reader takes. A table
/// Armageddon hasn't is checked without that allowance.
/// </summary>
static class TableCheck
{
    sealed record Problem(string Kind, string Path, string Entry, string Detail)
    {
        /// <summary>What makes two problems the same: the kind and field, and for a flag or a choice the value too (each value reported apart).</summary>
        public string Signature => Kind is "unknown flag in" or "not a choice of" ? Kind + " " + Path + " " + Detail : Kind + " " + Path;

        /// <summary>
        /// A missing required field always stops the game's reader ("Could not find required attribute",
        /// and the next value read in its place: "... is not a Boolean value"); the rest (a choice, flag or
        /// number the description doesn't allow) stop it for some tables and not others, so they're
        /// listed to look into but don't fail the build.
        /// </summary>
        public bool Stops => Kind is "missing" or "too long:";
    }

    /// <summary>Checks the tables, then the world's rocks' collision (<see cref="CollisionCheck"/>), its ladders (<see cref="LadderCheck"/>), the cloth (<see cref="ClothCheck"/>) and the hinges (<see cref="ConstraintCheck"/>); logs what it finds. False if any table has a problem the game would stop on, the rocks' collision trees hide collision, a ladder doesn't hang on its building, a cloth won't load or fit its wearer, or a hinge doesn't join two movers of its zone.</summary>
    public static bool Run(Setup setup, BuildLog log)
    {
        if (setup.Game == null) return true;
        string cache = Path.Combine(setup.Game, "build", "pc", "cache");
        var archives = new[] { "misc.vpp_pc", "table.vpp_pc" }
            .Select(a => Path.Combine(cache, a)).Where(File.Exists).Select(Packfile.Open).ToList();
        var files = new List<string>();
        foreach (string folder in new[] { Path.Combine(setup.ModFolder, "mods"), setup.Overrides })
            if (Directory.Exists(folder)) files.AddRange(Directory.EnumerateFiles(folder, "*.xtbl", SearchOption.AllDirectories));
        // Guerrilla's own tables for the guerrilla_campaign mod: only that mod reads them (its own reader), never the game.
        string campaignTables = Path.Combine(setup.CampaignFolder, GuerrillaCampaign.TablesFolder) + Path.DirectorySeparatorChar;
        files.RemoveAll(f => f.StartsWith(campaignTables, StringComparison.OrdinalIgnoreCase));

        int checkedTables = 0, bad = 0, doubtful = 0;
        foreach (string file in files.Order(StringComparer.OrdinalIgnoreCase))
        {
            XtblFile table;
            try { table = XtblFile.FromBytes(File.ReadAllBytes(file)); }
            catch (Exception e) when (e is IOException or InvalidDataException or FormatException)
            {
                log.Line($"Table check: {Relative(setup, file)} can't be read: {e.Message}");
                bad++;
                continue;
            }
            if (table.Description == null) continue;
            checkedTables++;

            // What Armageddon's own entries of this table get away with (a table written under a name of
            // its own is compared with the game's table it copies).
            var allowed = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            string name = Path.GetFileName(file);
            if (RenamedTables.TryGetValue(name, out string? original)) name = original;
            foreach (var archive in archives)
                if (archive.Find(name) is { } entry)
                {
                    var own = XtblFile.FromBytes(archive.Read(entry));
                    if (own.Description == null) break;
                    foreach (var e in own.Entries) foreach (var p in CheckEntry(e, own.Description.Children)) allowed.Add(p.Signature);
                    break;
                }

            var problems = table.Entries.SelectMany(e => CheckEntry(e, table.Description.Children))
                                        .Where(p => !allowed.Contains(p.Signature) && !GameReaderKnows(name, p)).ToList();
            if (problems.Count == 0) continue;
            var stopping = problems.Where(p => p.Stops).ToList();
            var others = problems.Where(p => !p.Stops).ToList();
            if (stopping.Count > 0)
            {
                bad++;
                Report(log, $"Table check: {Relative(setup, file)}: {stopping.Select(p => p.Entry).Distinct().Count()} entries the game's reader stops on:", stopping);
            }
            if (others.Count > 0)
            {
                doubtful++;
                Report(log, $"Table check: {Relative(setup, file)}: {others.Select(p => p.Entry).Distinct().Count()} entries with values Armageddon's description doesn't allow (to look into: the reader stops on some):", others);
            }
        }
        if (!SquadsCheck(setup, archives, log)) bad++;
        if (!ActivityLayersCheck(setup, log)) bad++;
        // The sound banks the game will list, Armageddon's and ours, against the most its bank list holds.
        string worldFolder = Path.Combine(setup.ModFolder, "mods", "guerrilla_world");
        int banks = GuerrillaSoundBanks.BankRows(setup.Game, worldFolder);
        log.Line($"Sound bank check: {banks} of the {GuerrillaSoundBanks.GameBankLimit} sound banks Armageddon can list are used " +
                 (banks <= GuerrillaSoundBanks.GameBankLimit
                     ? $"({GuerrillaSoundBanks.GameBankLimit - banks} left)"
                     : $"({banks - GuerrillaSoundBanks.GameBankLimit} too many: the game would load no sound at all; {GuerrillaSoundBanks.BankTable} in mods\\guerrilla_world\\)"));
        if (banks > GuerrillaSoundBanks.GameBankLimit) bad++;
        // The interface's sheet table against the sheets and pictures the game takes from it (guerrilla_hud's raised limits).
        string sheetTable = setup.NewFile(RfaFormats.Guerrilla.OverrideLayout.Hud, RfaFormats.Interface.GuerrillaHud.SheetTableName + ".xtbl");
        if (File.Exists(sheetTable))
        {
            var (sheets, pictures) = RfaFormats.Interface.GuerrillaHud.SheetTableCounts(Encoding.Latin1.GetString(File.ReadAllBytes(sheetTable)));
            bool fits = sheets <= RfaFormats.Interface.GuerrillaHud.SheetLimit && pictures <= RfaFormats.Interface.GuerrillaHud.PictureLimit;
            log.Line($"Picture sheet check: {sheets} of the {RfaFormats.Interface.GuerrillaHud.SheetLimit} sheets and {pictures} of the " +
                     $"{RfaFormats.Interface.GuerrillaHud.PictureLimit} pictures the game takes are used in {Path.GetFileName(sheetTable)}" +
                     (fits ? "" : ": too many, the game would drop the last ones (minimap zones, HUD and handbook pictures)"));
            if (!fits) bad++;
        }
        if (!DistanceCheck(setup, log)) bad++;
        log.Line(bad == 0
            ? $"Table check: none of the {checkedTables} tables lacks a field the game requires" + (doubtful > 0 ? $"; {doubtful} have values to look into (above)" : "")
            : $"Table check: {bad} of {checkedTables} tables have entries the game stops on at start-up (above)" + (doubtful > 0 ? $"; {doubtful} have values to look into" : ""));
        // And the world's rocks as the game's physics will take them.
        bool solid = CollisionCheck.Run(setup, log);
        // And its ladders, each on its building (LadderCheck).
        bool ladders = LadderCheck.Run(setup, log);
        // And the cloth, as the game will load and wear it (ClothCheck).
        bool cloth = ClothCheck.Run(setup, log);
        // And the hinges, each joining two movers of its zone (ConstraintCheck).
        bool constraints = ConstraintCheck.Run(setup, log);
        // And our sound banks, every event played without speakers on rfa_audio's engine (SoundCheck).
        bool sounds = SoundCheck.Run(setup, log);
        // And the parts' files: each one a part wrote still there, and no new file's name twice (FileLists).
        bool layout = new FileLists(setup).Check(log);
        return bad == 0 && solid && ladders && cloth && constraints && sounds && layout;
    }

    /// <summary>
    /// Our sound banks' 3D nodes (those placing themselves: their children take it from them) and their fade on the
    /// game's distance (<see cref="WwiseBankWriter.GameDistance"/>; Wwise's own attenuation doesn't fade them in game).
    /// False if one has no fade. A line warns past <see cref="MostDistanceCurves"/> curves: Wwise goes through every
    /// curve on a game parameter each time the game sets it for a sound, and with one on each of the voices' 5,362
    /// lines (2026-11-18) the fades stopped working in game.
    /// </summary>
    static bool DistanceCheck(Setup setup, BuildLog log)
    {
        byte[] placed = [0x00, 0x00, 0xC8, 0x42, 0x01, 0x03, 0x00, 0x00, 0x00];  // 3D, game-defined (WwiseBankWriter.NodeBase)
        int withFade = 0, without = 0, sharedIds = 0;
        var lacking = new List<string>();
        // Each curve's id: Armageddon's are all different, and ours must be too (726 of our fades had one id, 2026-11-19).
        var curveIds = new HashSet<uint>();
        foreach (string file in RfaFormats.Guerrilla.OverrideLayout.FindAll(setup.Overrides, "rfg*.bnk_pc")
                                    .Where(f => !f.EndsWith("_media.bnk_pc", StringComparison.OrdinalIgnoreCase)))
        {
            byte[] d = File.ReadAllBytes(file);
            for (int o = 0; o + 8 <= d.Length;)
            {
                int size = BitConverter.ToInt32(d, o + 4);
                if (size < 0 || o + 8 + size > d.Length) break;
                if (Encoding.ASCII.GetString(d, o, 4) == "HIRC")
                    for (int k = 0, count = BitConverter.ToInt32(d, o + 8), p = o + 12; k < count && p + 12 <= o + 8 + size; k++)
                    {
                        uint type = BitConverter.ToUInt32(d, p);
                        int length = BitConverter.ToInt32(d, p + 4);
                        if (length < 4) break;
                        int body = p + 12, end = p + 8 + length;
                        int at = type is 2 or 5 or 6 or 7 or 9 ? d.AsSpan(body, end - body).IndexOf(placed) : -1;
                        if (at >= 0)
                        {
                            // After it: the attenuation (4), spatialized and dynamic (2), the voice settings (8), no states (2), the curves.
                            int q = body + at + placed.Length + 4 + 2 + 8 + 2;
                            int curves = BitConverter.ToUInt16(d, q);
                            bool fades = false;
                            q += 2;
                            for (int c = 0; c < curves && q + 20 <= end; c++)
                            {
                                uint parameter = BitConverter.ToUInt32(d, q + 5), property = BitConverter.ToUInt32(d, q + 9);
                                if (!curveIds.Add(BitConverter.ToUInt32(d, q + 13))) sharedIds++;
                                int points = BitConverter.ToUInt16(d, q + 18);
                                if (parameter == WwiseBankWriter.GameDistance && property == 0) fades = true;
                                q += 20 + 12 * points;
                            }
                            if (fades) withFade++;
                            else { without++; if (lacking.Count < 5) lacking.Add($"{Path.GetFileName(file)} {BitConverter.ToUInt32(d, p + 8):X8}"); }
                        }
                        p = end;
                    }
                o += 8 + size;
            }
        }
        log.Line($"Sound distance check: {withFade} of our 3D sounds and mixers fade on the game's distance" +
                 (without > 0 ? $"; {without} don't (they'd play at full volume however far: {string.Join(", ", lacking)})" : "") +
                 (sharedIds > 0 ? $"; {sharedIds} of their curves have an id another curve has (to look into: banks built before 2026-11-19 shared them; building the world, vehicles and weapons parts gives each its own)" : $"; their {curveIds.Count} curves' ids all different") +
                 (withFade > MostDistanceCurves ? $"; that is past the {MostDistanceCurves} curves the fades are known to work with (to look into: hang more of them from mixers)" : ""));
        // (Shared curve ids are warned of, not failed: bin's banks built before the writer gave each its own still have them.)
        return without == 0;
    }

    /// <summary>How many fade curves on the game's distance our banks may carry before the check warns (772 worked; 6,100 didn't).</summary>
    const int MostDistanceCurves = 2000;

    /// <summary>
    /// The squads the world's tables (mods\guerrilla_world\) hand the game, as its squad and loadout readers take them
    /// (GuerrillaSquads.SquadProblems): the loadouts past the ones the game holds, loadouts whose weapon it hasn't,
    /// and squads naming a loadout or character it won't have, each of which it turns down without a word until a zone
    /// names the squad ("invalid squad definition"). False if any.
    /// </summary>
    static bool SquadsCheck(Setup setup, List<Packfile> archives, BuildLog log)
    {
        string world = Path.Combine(setup.ModFolder, "mods", "guerrilla_world");
        string squadsFile = Path.Combine(world, GuerrillaSquads.SquadTable), loadoutsFile = Path.Combine(world, GuerrillaSquads.LoadoutTable);
        if (!File.Exists(squadsFile) || !File.Exists(loadoutsFile)) return true;
        XtblFile? Game(string name) => archives.Select(a => a.Find(name) is { } e ? XtblFile.FromBytes(a.Read(e)) : null).FirstOrDefault(t => t != null);
        XtblFile? Ours(string name) => File.Exists(Path.Combine(world, name)) ? XtblFile.FromBytes(File.ReadAllBytes(Path.Combine(world, name))) : Game(name);
        HashSet<string>? Names(XtblFile? table) => table?.Entries.Select(XtblFile.EntryName).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var squads = XtblFile.FromBytes(File.ReadAllBytes(squadsFile));
        var problems = GuerrillaSquads.SquadProblems(squads, XtblFile.FromBytes(File.ReadAllBytes(loadoutsFile)),
                                                     Names(Ours("weapons.xtbl")), Names(Ours("character.xtbl")), Names(Game(GuerrillaSquads.SquadTable)));
        if (problems.Count == 0)
        {
            log.Line($"Squads check: all {squads.Entries.Count()} squads' members name loadouts and characters the game has");
            return true;
        }
        log.Line($"Squads check: {problems.Count} thing(s) the game would turn down (its squads naming them aren't read):");
        foreach (string problem in problems) log.Line("  " + problem);
        return false;
    }

    /// <summary>
    /// The activities' managed layers the world wrote (GuerrillaActivityLayers.Check): each set's container on disk and in
    /// missions.vpp_pc's asset list after Armageddon's own entries byte for byte, each row of the layer table naming one,
    /// each layer a layer the game reads (ZONE 46, flags 4) with its objects in its zone and layer, the sets and their
    /// names within the game's room. False if any; what the objects name that the game's tables lack is only noted.
    /// </summary>
    static bool ActivityLayersCheck(Setup setup, BuildLog log)
    {
        var found = GuerrillaActivityLayers.Check(setup.Game!, setup.Overrides, setup.WorldFolder);
        foreach (var (_, text) in found) log.Line($"Activity layers check: {text}");
        return !found.Any(f => f.Problem);
    }

    static void Report(BuildLog log, string heading, List<Problem> problems)
    {
        log.Line(heading);
        foreach (var group in problems.GroupBy(p => p.Signature).OrderByDescending(g => g.Count()).Take(12))
        {
            var first = group.First();
            var entries = group.Select(p => p.Entry).Distinct().ToList();
            string names = string.Join(", ", entries.Take(3)) + (entries.Count > 3 ? ", ..." : "");
            log.Line($"  {first.Kind} {first.Path}{(first.Detail.Length > 0 ? $" ({first.Detail})" : "")}: {group.Count()} time(s), in {names}");
        }
    }

    static IEnumerable<Problem> CheckEntry(XtblElement entry, IReadOnlyList<FieldDescription> fields)
    {
        string name = XtblFile.EntryName(entry);
        var found = new List<Problem>();
        CheckFields(entry, fields, "", name, found);
        return found;
    }

    static void CheckFields(XtblElement element, IReadOnlyList<FieldDescription> fields, string path, string entry, List<Problem> found)
    {
        foreach (var field in fields)
        {
            string at = path + field.Name;
            var child = element.Element(field.Name);
            if (child == null)
            {
                if (field.Required && field.Type != FieldType.Flags && field.Type != FieldType.List && field.Type != FieldType.Grid)
                    found.Add(new("missing", at, entry, ""));
                continue;
            }
            CheckValue(child, field, at, entry, found);
        }
    }

    static void CheckValue(XtblElement child, FieldDescription field, string at, string entry, List<Problem> found)
    {
        switch (field.Type)
        {
            case FieldType.Int:
            case FieldType.Float:
                if (!child.HasElements && field.Check(child.Value.Trim()) is { } why) found.Add(new("bad value of", at, entry, $"\"{child.Value.Trim()}\" {why}"));
                break;
            case FieldType.Selection:
                if (field.Choices.Count > 0 && !child.HasElements &&
                    !field.Choices.Any(c => c.Equals(child.Value.Trim(), StringComparison.OrdinalIgnoreCase)))
                    found.Add(new("not a choice of", at, entry, $"\"{child.Value.Trim()}\"; choices {string.Join("/", field.Choices.Take(6))}"));
                break;
            case FieldType.Flags:
                if (field.Flags.Count > 0)
                    foreach (var flag in child.ElementsNamed("Flag"))
                        if (!field.Flags.Any(f => f.Equals(flag.Value.Trim(), StringComparison.OrdinalIgnoreCase)))
                            found.Add(new("unknown flag in", at, entry, $"\"{flag.Value.Trim()}\""));
                break;
            case FieldType.Element:
                CheckFields(child, field.Children, at + "/", entry, found);
                break;
            case FieldType.Vector:
            case FieldType.Color:
                foreach (var part in field.Children)
                    if (child.Element(part.Name) is { } value && !double.TryParse(value.Value.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out _))
                        found.Add(new("bad value of", at + "/" + part.Name, entry, $"\"{value.Value.Trim()}\" must be a number"));
                break;
            case FieldType.ComboElement:
                // The group chosen: its one child names one of the description's choices.
                foreach (var chosen in child.Elements)
                    if (field.Children.FirstOrDefault(c => c.Name.Equals(chosen.Name, StringComparison.OrdinalIgnoreCase)) is { } choice)
                        CheckFields(chosen, choice.Children, at + "/" + choice.Name + "/", entry, found);
                break;
            case FieldType.List:
            case FieldType.Grid:
                if (field.Item is { } item)
                    foreach (var one in child.Elements.Where(e => e.Name.Equals(field.ItemElementName, StringComparison.OrdinalIgnoreCase)))
                        CheckValue(one, item, at + "/" + item.Name, entry, found);
                break;
            default:
                // Text: the reader copies it into a buffer of the description's MaxLength with a checked
                // copy, which ends the game ("invalid parameter", 0xC0000417) when it's longer.
                if (!child.HasElements && field.MaxLength is int most && child.Value.Trim().Length > most)
                    found.Add(new("too long:", at, entry, $"\"{child.Value.Trim()}\" has {child.Value.Trim().Length} characters, at most {most}"));
                break;
        }
    }

    static string Relative(Setup setup, string file) => Path.GetRelativePath(setup.ModFolder, file);

    /// <summary>
    /// Flags whose names the game's own reader takes though the table's description doesn't list them (the
    /// descriptions are older than the game's name lists), by table and field:
    /// - gameplay_properties.xtbl chunk_flags: the reader's name list (0x24577B0, made at start-up at 0xF38E20), bits
    ///   0x1 to 0x200000 in this order;
    /// - level_objects.xtbl's script point effects' Flags: the reader's list at 0x1101514 (read at 0x7ADD9D), bits 0x1
    ///   to 0x100 in this order.
    /// </summary>
    static readonly Dictionary<(string Table, string Field), HashSet<string>> GameFlagNames = new()
    {
        [("gameplay_properties.xtbl", "chunk_flags")] = new(StringComparer.OrdinalIgnoreCase)
        {
            "casts_shadows", "regenerate", "show_on_map", "prevent_anchor", "disable_collapse_effect", "casts_drop_shadows", "regrow_on_stream",
            "inherit_damage_pct", "invulnerable", "plume_on_death", "one_of_many", "mining", "supply_crate", "touch_terrain", "kiosk", "propaganda",
            "no_cover", "building", "no_physics", "no_melt_in_magma", "light_as_vehicle", "show_red_reticule",
        },
        [("level_objects.xtbl", "Inherited_Attributes/Script_Point_Effects/Script_Point_Effect/Flags")] = new(StringComparer.OrdinalIgnoreCase)
        {
            "Stop Custom on Detach", "Kill on Detach", "Prefix Effect No Loop", "No Bullet Damage", "No Melee Damage", "Kiosk Large Screen",
            "Kiosk Small Screen", "Night Only", "Kill on Move",
        },
    };

    /// <summary>A flag the description doesn't list but the game's own reader takes by that name (<see cref="GameFlagNames"/>).</summary>
    static bool GameReaderKnows(string table, Problem problem) =>
        problem.Kind == "unknown flag in" && GameFlagNames.TryGetValue((table.ToLowerInvariant(), problem.Path), out var names) &&
        names.Contains(problem.Detail.Trim('"'));

    /// <summary>Tables the parts write under a name of their own, and the game's table each copies.</summary>
    static readonly Dictionary<string, string> RenamedTables = new(StringComparer.OrdinalIgnoreCase)
    {
        ["guerrilla_hud_sheets.xtbl"] = "bitmap_sheets.xtbl",  // the HUD's sheet table (GuerrillaHud)
        ["explosions_armageddon_cuts.xtbl"] = "explosions.xtbl",  // the explosions cutting by Armageddon's radii (GuerrillaExplosions=0)
    };
}
