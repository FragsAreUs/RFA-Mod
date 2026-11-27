using System.IO;
using System.Text;
using RfaFormats.Levels;

namespace RfaEditor;

/// <summary>
/// Where the game reads spawn_resource.xtbl from. Its own copy is table.vpp_pc's (the editor's changes go to the override
/// in overrides\build\pc\cache\table.vpp_pc\); but with the Guerrilla world installed, the guerrilla_world mod gives the
/// game mods\guerrilla_world\spawn_resource.xtbl in its place, whole (Armageddon's entries with the world's own; made by
/// GuerrillaBuilder.exe), and the override goes unread: a level's entry changed only there was never seen, and a level
/// whose entry was missing from it got Armageddon's Default. So every change to an entry is made in both.
/// </summary>
static class SpawnResourceFiles
{
    public const string Archive = @"build\pc\cache\table.vpp_pc";
    public const string Name = "spawn_resource.xtbl";

    static string GuerrillaCopy(Workspace workspace) => Path.Combine(workspace.ModFolder, "mods", "guerrilla_world", Name);

    /// <summary>The table as the editor keeps it (the override, else the game's).</summary>
    public static string Read(Workspace workspace) => Encoding.Latin1.GetString(workspace.Read(Archive, Name));

    /// <summary>
    /// Makes a change to the table (<paramref name="change"/> takes the table's text and gives it back changed, or null
    /// for no change), in the override and in the Guerrilla world's copy where there is one. Returns where it went, for
    /// the log ("" for the override alone).
    /// </summary>
    public static string Change(Workspace workspace, Func<string, string?> change)
    {
        if (change(Read(workspace)) is string changed) workspace.WriteOverride(Archive, Name, Encoding.Latin1.GetBytes(changed));
        string guerrilla = GuerrillaCopy(workspace);
        if (!File.Exists(guerrilla)) return "";
        if (change(Encoding.Latin1.GetString(File.ReadAllBytes(guerrilla))) is not string theirs) return "";
        File.WriteAllBytes(guerrilla, Encoding.Latin1.GetBytes(theirs));
        return " (and in mods\\guerrilla_world\\spawn_resource.xtbl, which the game reads in its place with the Guerrilla world installed)";
    }

    /// <summary>Whether the Guerrilla world's copy is there and hasn't the entry (it was made before the entry was).</summary>
    public static bool GuerrillaLacks(Workspace workspace, string entryName)
    {
        string guerrilla = GuerrillaCopy(workspace);
        return File.Exists(guerrilla) && SpawnResource.Read(Encoding.Latin1.GetString(File.ReadAllBytes(guerrilla)), entryName) == null;
    }

    /// <summary>Writes an entry (see <see cref="SpawnResource.WriteTo"/>: a new one laid out as <paramref name="like"/>'s) into both.</summary>
    public static string Write(Workspace workspace, SpawnResource entry, string? like = null) =>
        Change(workspace, table => entry.WriteTo(table, like));
}
