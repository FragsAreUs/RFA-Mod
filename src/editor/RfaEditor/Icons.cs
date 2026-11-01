using System.IO;
using System.Windows.Media;

namespace RfaEditor;

/// <summary>
/// The icons the editor uses: characters of the "Segoe MDL2 Assets" font that comes with Windows
/// (shown with the "Icon" style in Theme.xaml). XAML uses them as {x:Static local:Icons.Save}.
/// </summary>
public static class Icons
{
    // Panels and the explorer
    public const string Folder = "";
    public const string Archive = "";
    public const string Container = "";
    public const string Table = "";
    public const string Texture = "";
    public const string Model = "";
    public const string Zone = "";
    public const string Script = "";
    public const string Sound = "";
    public const string Animation = "";
    public const string Effect = "";
    public const string Text = "";
    public const string List = "";
    public const string File = "";
    public const string Search = "";
    public const string Info = "";
    public const string Log = "";
    public const string Preview = "";
    public const string OpenFolder = "";
    public const string Close = "";

    // Level editor tools and actions
    public const string Select = "";
    public const string Move = "";
    public const string Rotate = "";
    public const string Snap = "";
    public const string Undo = "";
    public const string Redo = "";
    public const string Focus = "";
    public const string ShowAll = "";
    public const string Save = "";
    public const string Restore = "";
    public const string Help = "";
    public const string Add = "";        // Add (a plus)
    public const string Duplicate = "";  // Copy
    public const string Delete = "";     // Delete (a bin)
    public const string Game = "\uE7FC";       // Game (a controller): show in the running game
    public const string Link = "\uE71B";       // Link: follow the running game
    public const string Settled = "\uE896";    // Download (an arrow down to a line): props where the running game has them
    public const string Display = "\uE7F4";    // A monitor: the Direct3D view
    public const string GameLight = "\uE793";  // A sun (Brightness): light the level as the game does

    // Kinds of objects in a level
    public const string Light = "";
    public const string Path = "";
    public const string Start = "";
    public const string Region = "";
    public const string Spawn = "";
    public const string Pickup = "\uE7B8";  // Package
    public const string Mission = "\uE7C1";  // Flag: a mission layer shown with its zone
    public const string Locked = "\uE72E";   // Lock: a property Volition's editor kept read-only
    public const string Marker = "";
}

/// <summary>A kind of file, by its extension: its name, icon and colour in the explorer.</summary>
sealed record FileKind(string Name, string Icon, Brush Colour)
{
    static readonly FileKind Table = new("table", Icons.Table, Brush("#6CC28A"));
    static readonly FileKind Texture = new("texture", Icons.Texture, Brush("#B48EF0"));
    static readonly FileKind Model = new("model", Icons.Model, Brush("#5AA9F0"));
    static readonly FileKind Zone = new("zone", Icons.Zone, Brush("#4EC9B0"));
    static readonly FileKind Container = new("container", Icons.Container, Brush("#D7A35E"));
    static readonly FileKind Script = new("script", Icons.Script, Brush("#E5C07B"));
    static readonly FileKind Sound = new("sound", Icons.Sound, Brush("#E07BA8"));
    static readonly FileKind Animation = new("animation", Icons.Animation, Brush("#7FB8C8"));
    static readonly FileKind Effect = new("effect", Icons.Effect, Brush("#F0A050"));
    static readonly FileKind Text = new("text", Icons.Text, Brush("#A9ADB8"));
    static readonly FileKind AssetList = new("asset list", Icons.List, Brush("#C9A26B"));
    static readonly FileKind Other = new("file", Icons.File, Brush("#8E9099"));

    public static readonly FileKind Folder = new("folder", Icons.Folder, Brush("#DCB45A"));
    public static readonly FileKind Archive = new("archive", Icons.Archive, Brush("#D7A35E"));

    public static FileKind Of(string fileName) => Path.GetExtension(fileName).TrimStart('.').ToLowerInvariant() switch
    {
        "xtbl" => Table,
        "cpeg_pc" or "gpeg_pc" or "cvbm_pc" or "gvbm_pc" or "png" => Texture,
        "cchk_pc" or "gchk_pc" or "csmesh_pc" or "gsmesh_pc" or "ccmesh_pc" or "gcmesh_pc" or "cstch_pc" or "gstch_pc" or
            "ctmesh_pc" or "gtmesh_pc" or "cfmesh_pc" or "ccar_pc" or "gcar_pc" or "cterrain_pc" or "gterrain_pc" => Model,
        "rfgzone_pc" or "layer_pc" => Zone,
        "str2_pc" or "vpp_pc" => Container,
        "lua" => Script,
        "bnk_pc" or "mbnk_pc" => Sound,
        "anim_pc" or "rig_pc" => Animation,
        "cefct_pc" or "gefct_pc" or "fxo_pc" or "fxo_pc_dx11" or "fxo" => Effect,
        "txt" or "le_strings" or "vint_doc" or "vint_proj" or "dat" => Text,
        "asm_pc" => AssetList,
        _ => Other,
    };

    static SolidColorBrush Brush(string hex)
    {
        var brush = new SolidColorBrush((Color)ColorConverter.ConvertFromString(hex));
        brush.Freeze();
        return brush;
    }
}
