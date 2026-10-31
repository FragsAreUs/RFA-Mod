using System.IO;
using System.Text.RegularExpressions;
using RfaFormats.Zones;

namespace RfaEditor;

/// <summary>
/// Another file of the same zone, drawn with the one being edited so the zone looks as it does in
/// the game. A zone's mission layers (world_08_02_128.layer_pc, in missions.vpp_pc's
/// Mission_01A.str2_pc) hold what a mission adds to it: its enemies (squad spawns), triggers,
/// force fields, turrets, vehicles. For a mission layer, they are the zone itself and its other
/// layers. They can be seen and picked, but are changed in their own tab.
/// </summary>
sealed class RelatedFile
{
    public required string Name { get; init; }
    public required string Archive { get; init; }
    public string? Container { get; init; }
    public required ZoneFile Zone { get; init; }

    /// <summary>Its objects in 3D, built with the rest of the view.</summary>
    public LevelScene? Scene { get; set; }

    /// <summary>Whether it is drawn (its chip is on).</summary>
    public bool Shown { get; set; } = true;

    public bool IsLayer => Name.EndsWith(".layer_pc", StringComparison.OrdinalIgnoreCase);

    /// <summary>What its chip and outliner group say: the mission a layer belongs to (its container), or the zone's name.</summary>
    public string Label => IsLayer && Container != null ? Path.GetFileNameWithoutExtension(Container) : Path.GetFileNameWithoutExtension(Name);

    public string Description => IsLayer
        ? $"What the mission {Label} adds to this zone ({Name}): its enemies, triggers, force fields and the like. " +
          "Pick one to see it; open the layer to change it."
        : $"The zone's own objects ({Name}). Pick one to see it; open the zone to change it.";

    public string Icon => IsLayer ? Icons.Mission : Icons.Zone;

    /// <summary>
    /// The other files of the zone a zone or mission layer belongs to: the zone (world_08_02.rfgzone_pc)
    /// and every layer of it (world_08_02_128.layer_pc, _129, ...), but not <paramref name="openName"/>
    /// itself. Each is read where the game has it (the zone's own container first). Needs the index.
    /// </summary>
    public static List<RelatedFile> Find(GameAssets assets, string openName, string nearArchive, string? nearContainer, Action<string> log)
    {
        string zone = LevelGeometry.ZoneNames(openName).Last();
        var layer = new Regex("^" + Regex.Escape(zone) + @"_\d{3,}\.layer_pc$", RegexOptions.IgnoreCase);
        var found = new List<RelatedFile>();
        foreach (string name in assets.NamesWhere(layer.IsMatch).Order(StringComparer.OrdinalIgnoreCase).Prepend(zone + ".rfgzone_pc"))
        {
            if (name.Equals(openName, StringComparison.OrdinalIgnoreCase) || assets.ReadFile(name, nearArchive, nearContainer) is not { } file) continue;
            try
            {
                found.Add(new RelatedFile { Name = name, Archive = file.Archive, Container = file.Container, Zone = ZoneFile.Read(file.Data) });
            }
            catch (Exception ex) when (ex is InvalidDataException or NotSupportedException)
            {
                log($"{name} couldn't be read: {ex.Message}");
            }
        }
        return found;
    }
}
