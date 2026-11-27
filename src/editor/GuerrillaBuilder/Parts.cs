using System.IO;
using System.Diagnostics;
using System.Text;
using RfaFormats.Guerrilla;
using RfaFormats.Interface;

namespace GuerrillaBuilder;

/// <summary>What the builder makes, in the order it makes them: each part may use what the ones before it wrote.</summary>
enum Part { World, Hud, Vehicles, Weapons, People, Cloth }

enum State { NotBuilt, Built, BuildAgain }

sealed record PartStatus(State State, string Text);

/// <summary>A build's choices beyond which parts: the command line's options (none: the defaults).</summary>
sealed class Options
{
    /// <summary>The world's zones: null or "all" for every one, else a block such as 11-12,10-11.</summary>
    public string? Zones;
    /// <summary>The destructible objects to place: null for all, else the chunk files named (empty: none).</summary>
    public List<string>? Props;
    /// <summary>A block of zones to place them in only, such as 12,10.</summary>
    public string? PropsIn;
    public List<string>? Vehicles, Weapons, Characters;
}

/// <summary>
/// A build's lines: shown (in the window or on the command line) and written to
/// logs\guerrilla_builder.log, with the details of an error that only the file gets.
/// </summary>
sealed class BuildLog : IDisposable
{
    readonly Action<string> show;
    readonly StreamWriter? file;
    readonly object gate = new();

    /// <summary>Told what is being done now ("Building the vehicles (3 of 5)"), for the window's status line.</summary>
    public Action<string>? Stage;

    public BuildLog(string path, Action<string> show)
    {
        this.show = show;
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            file = new StreamWriter(path, append: false, new UTF8Encoding(false)) { AutoFlush = true };
            file.WriteLine($"GuerrillaBuilder, {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            show($"(No log file: {e.Message})");
        }
    }

    public void Line(string text)
    {
        lock (gate)
        {
            file?.WriteLine(text);
            show(text);
        }
    }

    /// <summary>For the log file only: what helps with a bug report, too long for the window.</summary>
    public void Detail(string text)
    {
        lock (gate) file?.WriteLine(text);
    }

    public void Dispose() => file?.Dispose();
}

static class Parts
{
    public static readonly Part[] All = Enum.GetValues<Part>();

    public static string Name(Part part) => part switch
    {
        Part.World => "World",
        Part.Hud => "HUD",
        _ => part.ToString(),
    };

    /// <summary>The part's name in a sentence: "the world", "the HUD", "the vehicles".</summary>
    public static string Noun(Part part) => "the " + (part == Part.Hud ? "HUD" : part.ToString().ToLowerInvariant());

    /// <summary>The part's word on the command line: world, hud, vehicles, weapons, people.</summary>
    public static string Word(Part part) => part.ToString().ToLowerInvariant();

    public static string Description(Part part) => part switch
    {
        Part.World => "Guerrilla's open world: its ground, roads, buildings, sky and effects. Takes a minute or more.",
        Part.Hud => "Guerrilla's HUD: health, minimap, world map, reticles and weapon select.",
        Part.Vehicles => "Guerrilla's vehicles, to spawn and drive.",
        Part.Weapons => "Guerrilla's guns (the ones ported so far), to spawn and use.",
        Part.People => "Guerrilla's EDF soldiers, and their patrols in the world.",
        Part.Cloth => "Guerrilla's cloth: Mason's coat swings as he moves and blows in the wind.",
        _ => "",
    };

    /// <summary>The parts a part needs built before it: their tables are what it adds to.</summary>
    static Part[] Needs(Part part) => part switch
    {
        // The vehicles' and weapons' effects and explosions are in the world's tables.
        Part.Vehicles or Part.Weapons => [Part.World],
        // The soldiers carry Guerrilla's guns.
        Part.People => [Part.World, Part.Weapons],
        // The cloth is worn by the people's props (Mason's coat).
        Part.Cloth => [Part.People],
        _ => [],
    };

    /// <summary>The parts built on a part, which go when it goes.</summary>
    static Part[] BuiltOn(Part part) => part switch
    {
        // Everything else names the world's tables (its effects, explosions and texture room).
        Part.World => [Part.Hud, Part.Vehicles, Part.Weapons, Part.People],
        Part.Weapons => [Part.People],
        Part.People => [Part.Cloth],
        _ => [],
    };

    /// <summary>A file each part writes, last or nearly: there if the part is built.</summary>
    static string Marker(Setup setup, Part part) => part switch
    {
        Part.World => setup.NewFile(OverrideLayout.World, GuerrillaWorld.World + "_l0.vpp_pc"),
        Part.Hud => Path.Combine(setup.Cache, "misc.vpp_pc", "rfg.vint_proj"),
        Part.Vehicles => Path.Combine(setup.WorldFolder, GuerrillaArsenal.Table),
        Part.Weapons => Path.Combine(setup.Overrides, GuerrillaWeapons.ItemList),
        Part.People => Path.Combine(setup.PeopleFolder, GuerrillaPeople.PeopleList),
        Part.Cloth => Path.Combine(setup.Overrides, GuerrillaCloth.AssetList),
        _ => throw new ArgumentOutOfRangeException(nameof(part)),
    };

    public static bool IsBuilt(Setup setup, Part part) => File.Exists(Marker(setup, part));

    public static PartStatus Status(Setup setup, Part part)
    {
        if (!IsBuilt(setup, part)) return new(State.NotBuilt, "Not built");
        var record = new Record(setup.WorldFolder).Of(part);
        if (record == null) return new(State.BuildAgain, "Build again: built by an older RFA-Mod, or not finished");
        if (part == Part.World && WorldFormat(setup) != GuerrillaWorld.Format)
            return new(State.BuildAgain, "Build again: made for another version of the mods");
        if (new FileLists(setup).Of(part).Count == 0)
            return new(State.BuildAgain, "Build again: built before each part had a folder of its own");
        if (setup.Version != null && record.GetValueOrDefault("By") != setup.Version)
            return new(State.BuildAgain, $"Build again: built by {ShortVersion(record.GetValueOrDefault("By"))}");
        string text = $"Built {record.GetValueOrDefault("Built")}";
        if (record.GetValueOrDefault("Options") is { Length: > 0 } options) text += $" ({options})";
        return new(State.Built, text);
    }

    /// <summary>
    /// Builds the parts asked for, in order, with the ones they need that aren't built yet. False if
    /// one failed (the parts after it aren't built: they may need it).
    /// </summary>
    public static bool Build(Setup setup, IReadOnlyCollection<Part> asked, Options options, BuildLog log)
    {
        if (setup.GameProblem is { } gameProblem) { log.Line(gameProblem); return false; }
        if (setup.GuerrillaProblem is { } guerrillaProblem) { log.Line(guerrillaProblem); return false; }
        var parts = new SortedSet<Part>(asked);
        for (bool added = true; added;)
        {
            added = false;
            foreach (var part in parts.ToList())
                foreach (var need in Needs(part).Where(n => !parts.Contains(n) && !IsBuilt(setup, n)))
                {
                    parts.Add(need);
                    added = true;
                    log.Line($"{Name(part)} need {Noun(need)}, which isn't built yet: building it too.");
                }
        }
        log.Line($"Guerrilla:  {setup.Guerrilla}");
        log.Line($"Armageddon: {setup.Game}");
        log.Line($"Building:   {string.Join(", ", parts.Select(Name))}");
        var record = new Record(setup.WorldFolder);
        var clock = Stopwatch.StartNew();
        // The vehicles' turrets name Guerrilla's turret weapons, which the weapons part makes; and the
        // weapons part makes the turrets' part items from the list the vehicles part writes. So on a
        // first build, where the vehicles run before there are any weapons, the vehicles get no
        // turrets: the two are built once more at the end, now that each has what the other made.
        bool weaponsBuiltBefore = IsBuilt(setup, Part.Weapons);
        int number = 0;
        foreach (var part in parts)
        {
            number++;
            log.Stage?.Invoke($"Building {Noun(part)} ({number} of {parts.Count})...");
            log.Line("");
            log.Line($"==== {Name(part)} ({number} of {parts.Count}) ====");
            if (!Run(setup, part, options, log, record)) return false;
        }
        if (!weaponsBuiltBefore && parts.Contains(Part.Vehicles) && parts.Contains(Part.Weapons))
            foreach (var part in new[] { Part.Vehicles, Part.Weapons })
            {
                log.Stage?.Invoke($"Building {Noun(part)} again, for the vehicles' turrets...");
                log.Line("");
                log.Line($"==== {Name(part)} again (a first build: the vehicles' turrets need the weapons, and the weapons the turrets' list) ====");
                if (!Run(setup, part, options, log, record, again: true)) return false;
            }
        // Every table written, checked as the game's reader will take it (see TableCheck).
        log.Stage?.Invoke("Checking the tables...");
        log.Line("");
        log.Line("==== Tables ====");
        if (!TableCheck.Run(setup, log))
        {
            log.Line("");
            log.Line($"Built in {Took(clock.Elapsed)}, but the game stops at start-up on the tables above. This is a bug in the builder: please report it, with logs\\guerrilla_builder.log.");
            return false;
        }
        log.Line("");
        log.Line($"Done in {Took(clock.Elapsed)}. Start the game with RFAModLauncher.exe; in the developer console (the ` key) type: level rfg_12_11");
        return true;
    }

    static bool Run(Setup setup, Part part, Options options, BuildLog log, Record record, bool again = false)
    {
        record.Forget(part);  // until it's finished: a part left half-made shows as not finished
        var clock = Stopwatch.StartNew();
        var lists = new FileLists(setup);
        var before = lists.Look();
        string guerrilla = setup.Guerrilla!, game = setup.Game!;
        string usedOptions = "";
        try
        {
            switch (part)
            {
                case Part.World:
                    usedOptions = BuildWorld(setup, options, log);
                    break;
                case Part.Hud:
                    foreach (string line in GuerrillaHud.Build(guerrilla, game, setup.HudFolder, setup.Overrides)) log.Line(line);
                    break;
                case Part.Vehicles:
                    foreach (string line in GuerrillaArsenal.Build(guerrilla, game, setup.Overrides, setup.WorldFolder, options.Vehicles ?? [.. GuerrillaArsenal.DefaultVehicles]))
                        log.Line(line);
                    usedOptions = options.Vehicles is { } vehicles ? string.Join(",", vehicles) : "";
                    break;
                case Part.Weapons:
                    foreach (string line in GuerrillaWeapons.Build(guerrilla, game, setup.Overrides, setup.WorldFolder, options.Weapons ?? [.. GuerrillaWeapons.DefaultWeapons]))
                        log.Line(line);
                    usedOptions = options.Weapons is { } weapons ? string.Join(",", weapons) : "";
                    break;
                case Part.People:
                    foreach (string line in GuerrillaPeople.Build(guerrilla, game, setup.Overrides, setup.WorldFolder, setup.PeopleFolder,
                                                                  options.Characters ?? [.. GuerrillaPeople.DefaultCharacters]))
                        log.Line(line);
                    // What the EDF answers the player's alert level by: Guerrilla's tables for the campaign's systems.
                    foreach (string line in GuerrillaCampaign.Build(guerrilla, setup.CampaignFolder)) log.Line(line);
                    usedOptions = options.Characters is { } characters ? string.Join(",", characters) : "";
                    break;
                case Part.Cloth:
                    foreach (string line in GuerrillaCloth.Build(guerrilla, game, setup.Overrides)) log.Line(line);
                    break;
            }
        }
        catch (Exception error)
        {
            Failed(log, $"{Name(part)} failed", error);
            return false;
        }
        var values = new Dictionary<string, string>
        {
            ["Built"] = DateTime.Now.ToString("yyyy-MM-dd HH:mm"),
            ["By"] = setup.Version ?? OwnBuild,
            ["Guerrilla"] = guerrilla,
        };
        if (usedOptions.Length > 0) values["Options"] = usedOptions;
        record.Set(part, values);
        int files = lists.Save(part, before, again);
        log.Line($"{Name(part)}: done in {Took(clock.Elapsed)}; {files} files, listed in mods\\guerrilla_world\\built\\{Word(part)}.txt");
        return true;
    }

    /// <summary>The world, as GuerrillaWorld.Build makes it; returns the options that made it other than all of it.</summary>
    static string BuildWorld(Setup setup, Options options, BuildLog log)
    {
        var zones = GuerrillaWorld.ZonesIn(Path.Combine(setup.Guerrilla!, "build", "pc", "cache"));
        var used = new List<string>();
        if (options.Zones != null && !options.Zones.Equals("all", StringComparison.OrdinalIgnoreCase))
        {
            var (cols, rows) = Block(options.Zones)
                ?? throw new ArgumentException($"--zones {options.Zones}: give columns and rows as from-to,from-to (such as 11-12,10-11), or all.");
            zones = zones.Where(z => z.Col >= cols.From && z.Col <= cols.To && z.Row >= rows.From && z.Row <= rows.To).ToList();
            if (zones.Count == 0) throw new ArgumentException($"Guerrilla has no zones in columns {cols.From}-{cols.To}, rows {rows.From}-{rows.To}.");
            used.Add($"zones {options.Zones}");
        }
        List<(int Col, int Row)>? propZones = null;
        if (options.PropsIn != null)
        {
            var (cols, rows) = Block(options.PropsIn)
                ?? throw new ArgumentException($"--props-in {options.PropsIn}: give columns and rows as from-to,from-to (such as 12,10).");
            propZones = zones.Where(z => z.Col >= cols.From && z.Col <= cols.To && z.Row >= rows.From && z.Row <= rows.To).ToList();
            used.Add($"objects in {options.PropsIn}");
        }
        List<string> props = options.Props ?? ["all"];
        if (options.Props != null) used.Add(props.Count == 0 ? "no objects" : $"objects {string.Join(",", props)}");
        log.Line($"Building {zones.Count} of Guerrilla's zones (a few minutes for all of them)...");
        GuerrillaWorld.Build(setup.Guerrilla!, setup.Game!, setup.Overrides, setup.WorldFolder, zones, GuerrillaWorld.DefaultStart, log.Line, props, propZones);
        // Where Mason wakes after a death (guerrilla_save's respawn): Guerrilla's safehouses' spots and player starts.
        try { log.Line(GuerrillaRespawns.Write(Path.Combine(setup.Guerrilla!, "build", "pc", "cache"), Path.Combine(setup.ModFolder, "mods", "guerrilla_save"))); }
        catch (Exception e) when (e is IOException or InvalidDataException or System.Xml.XmlException) { log.Line($"Respawns: not written ({e.Message}); guerrilla_save uses the safehouses' own places"); }
        log.Line($"Materials: {GuerrillaMaterials.LayeredMaterials:N0} given their layer (dirt, grime, snow, logos) as a decal, " +
                 $"{GuerrillaMaterials.ThirdUvDecals:N0} their decal at the third UV set");
        return string.Join(", ", used);
    }

    /// <summary>
    /// Takes the parts asked for away, with the parts built on them, last built first. False if one
    /// failed (the parts it's built on are left: they may still be needed).
    /// </summary>
    public static bool Remove(Setup setup, IReadOnlyCollection<Part> asked, BuildLog log)
    {
        if (setup.GameProblem is { } gameProblem) { log.Line(gameProblem); return false; }  // the parts' Removes read Armageddon's own tables
        var parts = WithBuiltOn(asked);
        foreach (var part in parts.Except(asked).Where(p => IsBuilt(setup, p)))
            log.Line($"{Name(part)}: taken away too, since it's built on {string.Join(" and ", asked.Where(a => BuiltOn(a).Contains(part)).Select(Noun))}.");
        var record = new Record(setup.WorldFolder);
        foreach (var part in parts.Reverse())
        {
            log.Stage?.Invoke($"Taking away {Noun(part)}...");
            log.Line("");
            log.Line($"==== {Name(part)} ====");
            try
            {
                var lines = part switch
                {
                    Part.World => RemoveWorld(setup),
                    Part.Hud => GuerrillaHud.Remove(setup.Overrides, setup.HudFolder),
                    Part.Vehicles => GuerrillaArsenal.Remove(setup.Game!, setup.Overrides, setup.WorldFolder),
                    Part.Weapons => GuerrillaWeapons.Remove(setup.Game!, setup.Overrides, setup.WorldFolder),
                    Part.People => GuerrillaPeople.Remove(setup.Game!, setup.Overrides, setup.WorldFolder, setup.PeopleFolder)
                        .Concat(GuerrillaCampaign.Remove(setup.CampaignFolder)),
                    Part.Cloth => GuerrillaCloth.Remove(setup.Game!, setup.Overrides),
                    _ => [],
                };
                int count = 0;
                foreach (string line in lines) { log.Line(line); count++; }
                // The part's new files its Remove didn't know of (FileLists).
                foreach (string line in new FileLists(setup).RemoveLeft(part)) { log.Line(line); count++; }
                if (count == 0) log.Line("Nothing was there.");
            }
            catch (Exception error)
            {
                Failed(log, $"Taking away {Noun(part)} failed", error);
                return false;
            }
            record.Forget(part);
        }
        log.Line("");
        log.Line("Done.");
        return true;
    }

    /// <summary>The parts with every part built on them, in build order.</summary>
    public static SortedSet<Part> WithBuiltOn(IEnumerable<Part> parts)
    {
        var all = new SortedSet<Part>(parts);
        foreach (var part in all.ToList()) all.UnionWith(BuiltOn(part));
        foreach (var part in all.ToList()) all.UnionWith(BuiltOn(part));
        return all;
    }

    /// <summary>
    /// Everything GuerrillaWorld.Build writes: its archives, territory, time of day, the overrides of
    /// Armageddon's texture name table and effects list, Guerrilla's material shaders (the people's
    /// skinned ones too: the world goes with everything built on it) and the world's tables and sky in
    /// mods\guerrilla_world\.
    /// </summary>
    static IEnumerable<string> RemoveWorld(Setup setup)
    {
        string world = GuerrillaWorld.World, cache = setup.Cache;
        foreach (string name in new[] { world + "_l0.vpp_pc", world + "_l1.vpp_pc", world + "_precache.vpp_pc", "zonescript_" + world + ".vpp_pc", world + ".gtodx" })
            foreach (string deleted in OverrideLayout.Delete(setup.Overrides, name)) yield return $"Deleted {deleted}";
        var files = new List<string>
        {
            Path.Combine(cache, "table.vpp_pc", "territories.xtbl"),
            Path.Combine(cache, "misc.vpp_pc", "bitmaps_pc"),
            Path.Combine(cache, "effects.vpp_pc", "vfx_containers.asm_pc"),
            Path.Combine(setup.ModFolder, "mods", "guerrilla_save", GuerrillaRespawns.File),
        };
        files.AddRange(OverrideLayout.FindAll(setup.Overrides, world + "_*.dtodx"));
        files.AddRange(OverrideLayout.FindAll(setup.Overrides, "gw_*.fxo_pc_dx11"));
        foreach (string table in new[] { "gameplay_properties.xtbl", "lod_properties.xtbl", "glass.xtbl", "vfx.xtbl", "effects.xtbl",
                                         "level_objects.xtbl", "explosions.xtbl", "explosions_armageddon_cuts.xtbl", GuerrillaMaterialEvents.FileName, GuerrillaWheelEffects.FileName, GuerrillaGusts.FileName, "clouds.bin", "stars.bin", "corona.bin", GuerrillaWorld.StampFile,
                                         GuerrillaSquads.PlacedList })
            files.Add(Path.Combine(setup.WorldFolder, table));
        if (Directory.Exists(setup.WorldFolder))
            files.AddRange(Directory.EnumerateFiles(setup.WorldFolder, "*.dds"));  // the sky's, the sun's and the moons' pictures
        foreach (string file in files.Where(File.Exists))
        {
            File.Delete(file);
            yield return $"Deleted {file}";
        }
        // Its people's footsteps: their rows out of the mod's footsteps.xtbl (the vehicles' rows are in it too).
        GuerrillaFootsteps.Remove(setup.WorldFolder);
        // Its effects' sounds: their banks, table rows and stop events.
        foreach (string line in GuerrillaSoundBanks.Remove(setup.Game!, setup.Overrides, setup.WorldFolder, GuerrillaWorld.EffectSoundPart)) yield return line;
        // Its music (so far the format test's bank and list).
        foreach (string line in GuerrillaSoundBanks.Remove(setup.Game!, setup.Overrides, setup.WorldFolder, GuerrillaMusic.Part)) yield return line;
        if (File.Exists(Path.Combine(setup.WorldFolder, GuerrillaMusic.TestList))) File.Delete(Path.Combine(setup.WorldFolder, GuerrillaMusic.TestList));
        // Its kiosks: the guerrilla_kiosks mod's tables and list.
        foreach (string line in GuerrillaKiosks.Remove(setup.Game!, setup.Overrides, setup.WorldFolder)) yield return line;
        // Its people's voices: the guerrilla_voices mod's table and the personas' banks.
        foreach (string line in GuerrillaVoiceBanks.Remove(setup.Game!, setup.Overrides, setup.WorldFolder)) yield return line;
        // Its activities' managed layers: their containers, the missions' asset list override and the layer table.
        foreach (string line in GuerrillaActivityLayers.Remove(setup.Game!, setup.Overrides, setup.WorldFolder)) yield return line;
    }

    /// <summary>
    /// The four programs this one replaces, left in editor\ by older versions of RFA-Mod: deleted, so
    /// nobody runs them by mistake (they'd make parts out of step with the rest). Returns what it did.
    /// </summary>
    public static IEnumerable<string> TidyOldTools()
    {
        string folder = AppContext.BaseDirectory;
        foreach (string tool in new[] { "GuerrillaWorld", "GuerrillaHud", "GuerrillaArsenal", "GuerrillaPeople" })
        {
            var found = new[] { ".exe", ".dll", ".deps.json", ".runtimeconfig.json", ".pdb" }.Select(e => Path.Combine(folder, tool + e)).Where(File.Exists).ToList();
            if (found.Count == 0) continue;
            string? problem = null;
            foreach (string file in found)
            {
                try { File.Delete(file); }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException) { problem = e.Message; }
            }
            yield return problem == null
                ? $"Deleted {tool}.exe, left by an older RFA-Mod: this builder does its work now."
                : $"Couldn't delete {tool}.exe, left by an older RFA-Mod ({problem}); it isn't needed any more.";
        }
    }

    static void Failed(BuildLog log, string what, Exception error)
    {
        // Wrong files or folders are the user's to put right: their message is enough. Anything else
        // is a bug in the builder: the details go to the log file for a bug report.
        bool expected = error is IOException or InvalidDataException or UnauthorizedAccessException or NotSupportedException or ArgumentException;
        log.Line($"{what}: {error.Message}");
        if (!expected) log.Line("This looks like a bug in the builder. Please report it, with logs\\guerrilla_builder.log.");
        log.Detail(error.ToString());
    }

    const string OwnBuild = "a build of your own";

    static string ShortVersion(string? version) =>
        version == null ? "another version" : version.Split(',')[0].Trim();

    /// <summary>The world's format, from its stamp (0: none).</summary>
    static int WorldFormat(Setup setup)
    {
        string stamp = Path.Combine(setup.WorldFolder, GuerrillaWorld.StampFile);
        if (!File.Exists(stamp)) return 0;
        string line = File.ReadLines(stamp).FirstOrDefault() ?? "";
        const string prefix = "RFA-Mod Guerrilla world, format ";
        return line.StartsWith(prefix) && int.TryParse(line[prefix.Length..].Trim(), out int format) ? format : 0;
    }

    static string Took(TimeSpan time) =>
        time.TotalMinutes >= 1 ? $"{(int)time.TotalMinutes} min {time.Seconds} s" : $"{time.TotalSeconds:0.0} s";

    /// <summary>"11-12,10-11": columns 11 to 12, rows 10 to 11 (a single number for one).</summary>
    static ((int From, int To) Cols, (int From, int To) Rows)? Block(string text)
    {
        string[] parts = text.Split(',');
        if (parts.Length != 2) return null;
        static (int, int)? Range(string s)
        {
            string[] ends = s.Trim().Split('-');
            if (ends.Length is < 1 or > 2 || !int.TryParse(ends[0], out int from)) return null;
            int to = from;
            if (ends.Length == 2 && !int.TryParse(ends[1], out to)) return null;
            return (Math.Min(from, to), Math.Max(from, to));
        }
        return Range(parts[0]) is { } cols && Range(parts[1]) is { } rows ? (cols, rows) : null;
    }
}

/// <summary>
/// What the builder has built here, and with which RFA-Mod: mods\guerrilla_world\builder.ini, a
/// section a part. It shows what to build again after an update; without it the builder only
/// knows what is there.
/// </summary>
sealed class Record
{
    const string FileName = "builder.ini";
    readonly string path;
    readonly SortedDictionary<Part, Dictionary<string, string>> parts = [];

    public Record(string folder)
    {
        path = Path.Combine(folder, FileName);
        if (!File.Exists(path)) return;
        Dictionary<string, string>? current = null;
        foreach (string raw in File.ReadLines(path))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line.StartsWith(';')) continue;
            if (line.StartsWith('[') && line.EndsWith(']'))
                current = Enum.TryParse(line[1..^1].Trim(), ignoreCase: true, out Part part) ? parts[part] = new(StringComparer.OrdinalIgnoreCase) : null;
            else if (current != null && line.IndexOf('=') is int equals and > 0)
                current[line[..equals].Trim()] = line[(equals + 1)..].Trim();
        }
    }

    public IReadOnlyDictionary<string, string>? Of(Part part) => parts.GetValueOrDefault(part);

    public void Set(Part part, Dictionary<string, string> values)
    {
        parts[part] = values;
        Save();
    }

    public void Forget(Part part)
    {
        if (parts.Remove(part)) Save();
    }

    void Save()
    {
        if (parts.Count == 0)
        {
            if (File.Exists(path)) File.Delete(path);
            return;
        }
        var text = new StringBuilder();
        text.Append("; What editor\\GuerrillaBuilder.exe has built here, and with which RFA-Mod: it reads this to\r\n");
        text.Append("; show what to build again. Written by the builder; there's nothing to set here.\r\n");
        foreach (var (part, values) in parts)
        {
            text.Append($"\r\n[{part}]\r\n");
            foreach (var (key, value) in values) text.Append($"{key}={value}\r\n");
        }
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, text.ToString());
    }
}
