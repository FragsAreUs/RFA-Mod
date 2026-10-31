using System.Windows.Media;

namespace RfaEditor;

/// <summary>
/// The kinds of objects in a level, as the level editor groups them: each has a name, an icon and
/// a colour, used for the object's marker in the 3D view, its row in the outliner and its title in
/// the inspector, and for the "Show" chips that hide or show a whole kind at once.
/// </summary>
sealed class ObjectKind
{
    public static readonly ObjectKind Models = new("Models", Icons.Model, Color.FromRgb(140, 170, 215),
        "Objects that place a model: buildings, props and other things that can be destroyed");
    public static readonly ObjectKind Lights = new("Lights", Icons.Light, Color.FromRgb(255, 225, 110), "Lights");
    public static readonly ObjectKind Paths = new("Paths", Icons.Path, Color.FromRgb(90, 160, 255),
        "Navigation points, leap nodes and road paths that characters and vehicles follow");
    public static readonly ObjectKind Starts = new("Starts", Icons.Start, Color.FromRgb(80, 235, 120),
        "Where the player and missions start");
    public static readonly ObjectKind Regions = new("Regions", Icons.Region, Color.FromRgb(255, 95, 95),
        "Trigger and damage regions, and other boxes that mark out an area");
    public static readonly ObjectKind Effects = new("Effects", Icons.Effect, Color.FromRgb(240, 120, 240), "Effects, such as fire and smoke");
    public static readonly ObjectKind Spawns = new("Spawns", Icons.Spawn, Color.FromRgb(255, 160, 60),
        "Where characters, vehicles and items appear");
    public static readonly ObjectKind Pickups = new("Pickups", Icons.Pickup, Color.FromRgb(120, 220, 210),
        "Items and weapons lying in the level: ammo, salvage, audio logs, pods, guns");
    public static readonly ObjectKind Other = new("Other", Icons.Marker, Color.FromRgb(165, 168, 178), "Everything else");

    /// <summary>Every kind, in the order the editor lists them.</summary>
    public static List<ObjectKind> All { get; } = [Models, Pickups, Lights, Paths, Starts, Regions, Effects, Spawns, Other];

    ObjectKind(string name, string icon, Color colour, string description)
    {
        Name = name;
        Icon = icon;
        Colour = colour;
        Description = description;
        Brush = new SolidColorBrush(colour);
        Brush.Freeze();
    }

    public string Name { get; }
    public string Icon { get; }
    public Color Colour { get; }
    public SolidColorBrush Brush { get; }
    public string Description { get; }

    /// <summary>An object's kind, from its class (e.g. "obj_light") and whether it places a model of its own.</summary>
    public static ObjectKind Of(string className, bool placesModel) => className.ToLowerInvariant() switch
    {
        _ when placesModel => Models,
        "item" or "weapon" => Pickups,
        "obj_light" => Lights,
        "navpoint" or "object_leap_node" or "object_path_road" => Paths,
        "player_start" or "object_mission_start_node" => Starts,
        "trigger_region" or "damage_region" or "object_bounding_box" => Regions,
        "object_effect" or "effect_streaming_node" => Effects,
        _ when className.Contains("spawn", StringComparison.OrdinalIgnoreCase) => Spawns,
        _ => Other,
    };
}
