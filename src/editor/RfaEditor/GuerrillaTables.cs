using System.IO;
using System.Text.RegularExpressions;
using RfaFormats.Tables;

namespace RfaEditor;

/// <summary>
/// The tables the guerrilla_world mod hands the game in place of its own while it starts (its TableCacheFind hook:
/// mods\guerrilla_world\&lt;table&gt;, GuerrillaBuilder.exe's, Armageddon's entries with the Guerrilla world's added).
/// The game reads them that way only while it starts; a table read again later (reload_weapons, ...) comes from
/// the file, which is the override in overrides\ when there is one. So an override made from Armageddon's own
/// table took the Guerrilla world's entries out of the running game when it was read again (its weapons: the
/// player's were left without theirs, and the weapon list crashed the game), and was itself never read at start.
/// The table editor therefore edits the Guerrilla world's copy where there is one, and saves the same table to
/// both: the game then reads one table whether it starts or reads it again.
/// </summary>
static class GuerrillaTables
{
    /// <summary>The tables guerrilla_world hands over (its g_builtTables), the .xtbl ones.</summary>
    static readonly HashSet<string> Substituted = new(StringComparer.OrdinalIgnoreCase)
    {
        "gameplay_properties.xtbl", "lod_properties.xtbl", "vfx.xtbl", "effects.xtbl", "explosions.xtbl",
        "level_objects.xtbl", "vehicles.xtbl", "vehicle_interactions.xtbl", "spawn_group_vehicle.xtbl", "turrets.xtbl",
        "vehicle_lights.xtbl", "footsteps.xtbl", "blendTree_filenames.xtbl", "anim_set_filenames.xtbl", "anim_files.xtbl",
        "anim_states.xtbl", "anim_actions.xtbl", "glass.xtbl", "weapons.xtbl", "items_3d.xtbl", "items_inventory.xtbl",
        "ammo.xtbl", "weapon_firing_patterns.xtbl", "aim_drift.xtbl", "camera_shake.xtbl", "projectiles.xtbl", "melee.xtbl",
        "rfg_melee_groups.xtbl", "rfg_bullet_groups.xtbl", "character.xtbl", "ai_properties.xtbl", "squads.xtbl",
        "squad_loadout.xtbl", "spawn_resource.xtbl",
    };

    /// <summary>The Guerrilla world's copy of a table of one of the game's archives, if it is installed and hands it over; else null.</summary>
    public static string? CopyFor(Workspace workspace, string archive, string fileName)
    {
        if (!Substituted.Contains(fileName) || workspace.IsLooseFolder(archive) || workspace.IsNewArchive(archive)) return null;
        string path = Path.Combine(workspace.ModFolder, "mods", "guerrilla_world", fileName);
        return File.Exists(path) ? path : null;
    }

    /// <summary>
    /// Carries an override's changes (made to Armageddon's own table, <paramref name="original"/>) into the Guerrilla
    /// world's table: each entry the override changed or added is put in by its name (in place of the Guerrilla
    /// world's of that name, or at the end), and each it deleted is taken out where the Guerrilla world left it as
    /// Armageddon has it. The names of the entries changed, added and deleted.
    /// </summary>
    public static List<string> CarryOver(XtblFile guerrilla, XtblFile original, XtblFile yours)
    {
        var done = new List<string>();
        if (guerrilla.Table == null || original.Table == null || yours.Table == null) return done;
        Dictionary<string, XtblElement> originals = ByName(original), theirs = ByName(guerrilla), mine = ByName(yours);

        foreach ((string key, XtblElement entry) in mine)
        {
            if (originals.TryGetValue(key, out XtblElement? was) && Same(was, entry)) continue;  // not changed by the override
            if (theirs.TryGetValue(key, out XtblElement? their))
            {
                if (Same(their, entry)) continue;
                guerrilla.Table.InsertAfter(their, entry.Copy());
                their.Remove();
                done.Add(XtblFile.EntryName(entry));
            }
            else
            {
                guerrilla.Table.AddElement(entry.Copy());
                done.Add(XtblFile.EntryName(entry) + " (added)");
            }
        }
        foreach ((string key, XtblElement was) in originals)
        {
            if (mine.ContainsKey(key) || !theirs.TryGetValue(key, out XtblElement? their) || !Same(their, was)) continue;
            their.Remove();
            done.Add(XtblFile.EntryName(was) + " (deleted)");
        }
        return done;
    }

    /// <summary>A table's named entries by kind and name (the first of a name).</summary>
    static Dictionary<string, XtblElement> ByName(XtblFile file)
    {
        var map = new Dictionary<string, XtblElement>(StringComparer.OrdinalIgnoreCase);
        foreach (XtblElement entry in file.Entries)
            if (XtblFile.HasNameField(entry)) map.TryAdd(entry.Name + "\n" + XtblFile.EntryName(entry), entry);
        return map;
    }

    static readonly Regex BetweenTags = new(@">\s+<", RegexOptions.Compiled);

    /// <summary>Whether two entries hold the same (whatever their indenting).</summary>
    static bool Same(XtblElement a, XtblElement b) => Text(a) == Text(b);

    static string Text(XtblElement entry) => BetweenTags.Replace(XtblXml.Write(entry), "><").Trim();
}
