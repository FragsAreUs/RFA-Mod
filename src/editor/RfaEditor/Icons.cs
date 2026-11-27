using System.IO;
using System.Windows.Media;

namespace RfaEditor;

/// <summary>
/// The icons the editor uses: characters of the Fluent System Icons font (Microsoft's, MIT), which
/// comes inside WPF UI's DLL, so they show on any PC, Linux under Wine too (shown with the "Icon"
/// style in Theme.xaml). XAML uses them as {x:Static local:Icons.Save}. Each is the icon's 20-pixel
/// design, named after it (WPF UI's SymbolRegular lists them all, with their codes).
/// </summary>
public static class Icons
{
    // Panels and the explorer
    public const string Folder = "";      // Folder20
    public const string Archive = "";     // Archive20
    public const string Container = "";   // Layer20
    public const string Table = "";       // Table20
    public const string Texture = "";     // Image20
    public const string Model = "";       // Cube20
    public const string Zone = "";        // Map20
    public const string Script = "";      // Code20
    public const string Sound = "";       // Speaker220
    public const string Animation = "";   // Run20
    public const string Effect = "";      // Sparkle20
    public const string Text = "";        // DocumentText20
    public const string List = "";        // List20
    public const string File = "";        // Document20
    public const string Search = "";      // Search20
    public const string Info = "";        // Info20
    public const string Log = "";         // TextBulletListLtr20
    public const string Preview = "";     // Eye20
    public const string OpenFolder = "";  // FolderOpen20
    public const string Close = "";       // Dismiss20

    // Level editor tools and actions
    public const string Select = "";      // Cursor20
    public const string Move = "";        // ArrowMove20
    public const string Rotate = "";      // ArrowRotateClockwise20
    public const string Snap = "";        // Grid20
    public const string Undo = "";        // ArrowUndo20
    public const string Redo = "";        // ArrowRedo20
    public const string Focus = "";       // Target20
    public const string ShowAll = "";     // FullScreenMaximize20
    public const string Save = "";        // Save20
    public const string Restore = "";     // ArrowReset20
    public const string Help = "";        // QuestionCircle20
    public const string Add = "";         // Add20 (a plus)
    public const string Duplicate = "";   // Copy20
    public const string Delete = "";      // Delete20 (a bin)
    public const string Game = "";        // XboxController20: show in the running game
    public const string Link = "";        // Link20: follow the running game
    public const string Settled = "";     // ArrowDownload20: props where the running game has them
    public const string Display = "";     // Desktop20: the Direct3D view
    public const string Sculpt = "";      // PaintBrush20: sculpt the ground
    public const string NavMesh = "";     // Path20: navigation mesh editing
    public const string GameLight = "";   // WeatherSunny20: light the level as the game does
    public const string Magnets = "";     // PlugConnected20: moved objects join the models near them
    public const string Live = "";        // ArrowSync20: changes go to the running game as they are made
    public const string AutoReload = "";  // ArrowClockwise20: the running game loads the level again by itself

    // The Scene panel
    public const string Shown = "";       // Eye20: drawn
    public const string Hidden = "";      // EyeOff20: not drawn
    public const string Terrain = "";     // Earth20: the ground
    public const string Foliage = "";     // LeafTwo20: debris scattered over the ground
    public const string Pieces = "";      // CubeTree20: the level geometry's pieces

    // Kinds of objects in a level
    public const string Light = "";       // Lightbulb20
    public const string Path = "";        // Location20
    public const string Start = "";       // Flag20
    public const string Region = "";      // SelectObject20
    public const string Spawn = "";       // People20
    public const string Pickup = "";      // Box20
    public const string Mission = "";     // Flag20: a mission layer shown with its zone
    public const string Locked = "";      // LockClosed20: a property Volition's editor kept read-only
    public const string Marker = "";      // Circle20
}

/// <summary>A kind of file, by its extension: its name, icon and colour in the explorer.</summary>
public sealed record FileKind(string Name, string Icon, Brush Colour)
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
        "bnk_pc" or "mbnk_pc" or "ctdg_pc" => Sound,
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
