using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using RfaFormats;
using RfaFormats.Textures;
using RfaFormats.Effects;
using RfaFormats.Zones;

namespace RfaEditor;

/// <summary>
/// The preview of an effect (.cefct_pc, or the .gefct_pc beside it): <see cref="EffectView"/>, made the first time one
/// is shown and laid over the preview's other views.
/// </summary>
public partial class FilePreview
{
    EffectView? effectView;
    Dictionary<uint, string>? effectShaders;  // the .fxo files' names by hash
    List<(string Name, string Visual, string? Sound, string? Folder)>? effectEntries;  // effects.xtbl, as the game sees it

    void ShowEffect(GameFile file)
    {
        // The .cefct_pc holds the effect; a .gefct_pc only its meshes' vertices.
        string name = file.Entry.Extension == "gefct_pc" ? Path.ChangeExtension(file.Name, ".cefct_pc") : file.Name;
        byte[]? bytes = file.Entry.Extension == "gefct_pc"
            ? context.Assets.ReadFile(name, file.Archive, file.Container)?.Data
            : ReadAsGameSeesIt(file);
        if (bytes == null)
        {
            PreviewText.Text = $"The vertices of an effect's meshes. Its effect, {name}, isn't beside it.\n\n" + Preview.Of(ReadAsGameSeesIt(file));
            return;
        }
        EffectSummary effect;
        try
        {
            effect = EffectSummary.Read(bytes);
        }
        catch (Exception e) when (e is InvalidDataException or NotSupportedException)
        {
            PreviewText.Text = $"This effect can't be read: {e.Message}\n\n" + Preview.Of(bytes);
            return;
        }
        effectView ??= MakeEffectView();
        PreviewText.Visibility = Visibility.Collapsed;
        effectView.Visibility = Visibility.Visible;

        // The shaders (.fxo_pc) are all in misc.vpp_pc; a material names its shader by the hash of the file's name.
        Dictionary<uint, string> shaders = effectShaders ??= Workspace.Archives()
            .Where(a => Path.GetFileName(a).Equals("misc.vpp_pc", StringComparison.OrdinalIgnoreCase))
            .SelectMany(a => Workspace.Archive(a).Entries.Where(e => e.Extension == "fxo_pc").Select(e => e.Name[..^".fxo_pc".Length]))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .GroupBy(NameHashes.PropertyHash).ToDictionary(g => g.Key, g => g.First());
        effectEntries ??= ReadEffectEntries();
        string visual = Path.GetFileNameWithoutExtension(name);
        var entries = effectEntries.Where(e => e.Visual.Equals(visual, StringComparison.OrdinalIgnoreCase))
                                   .Select(e => new EffectView.TableEntry(e.Name, e.Sound, e.Folder)).ToList();
        string archive = file.Archive;
        string? container = file.Container;
        // Each texture's flip-book tiles (from its texture file's entry).
        var tiles = new Dictionary<string, (int, int)>(StringComparer.OrdinalIgnoreCase);
        foreach (string bitmap in effect.Bitmaps.Distinct(StringComparer.OrdinalIgnoreCase))
            if (context.Assets.FindTexture(bitmap, archive, container) is var (peg, texture)) tiles[bitmap] = peg.AnimationTiles(texture);
        // Its meshes' vertices: the .gefct_pc beside it (the file picked, if that was it).
        string verticesName = Path.ChangeExtension(name, ".gefct_pc");
        byte[]? vertices = file.Entry.Extension == "gefct_pc" ? ReadAsGameSeesIt(file) : context.Assets.ReadFile(verticesName, archive, container)?.Data;
        effectView.Show(effect, bytes, vertices, name, entries, texture => context.Assets.Texture(texture, archive, container, 256, alpha: true),
                        texture => Pixels(context.Assets.Texture(texture, archive, container, 256, alpha: true)), tiles,
                        hash => shaders.GetValueOrDefault(hash));
    }

    // A texture's picture (blue, green, red, alpha) as the player's pixels (red, green, blue, alpha).
    static RgbaImage? Pixels(BitmapSource? picture)
    {
        if (picture == null) return null;
        if (picture.Format != PixelFormats.Bgra32) picture = new FormatConvertedBitmap(picture, PixelFormats.Bgra32, null, 0);
        int width = picture.PixelWidth, height = picture.PixelHeight;
        var bgra = new byte[width * height * 4];
        picture.CopyPixels(bgra, width * 4, 0);
        for (int i = 0; i < bgra.Length; i += 4) (bgra[i], bgra[i + 2]) = (bgra[i + 2], bgra[i]);
        return new RgbaImage(width, height, bgra);
    }

    // Each <Effect> of effects.xtbl: its Name, visual, sound and _Editor folder.
    List<(string, string, string?, string?)> ReadEffectEntries()
    {
        var entries = new List<(string, string, string?, string?)>();
        if (Workspace.ReadAsGameSeesIt("effects.xtbl") is not { } bytes) return entries;
        string table = Encoding.Latin1.GetString(bytes);
        static string? Tag(string xml, string tag) =>
            Regex.Match(xml, $"<{tag}>([^<]*)</{tag}>", RegexOptions.IgnoreCase) is { Success: true } m ? m.Groups[1].Value.Trim() : null;
        foreach (Match effect in Regex.Matches(table, @"<Effect>(.*?)</Effect>", RegexOptions.Singleline | RegexOptions.IgnoreCase))
        {
            string xml = effect.Groups[1].Value;
            if (Tag(xml, "Name") is not { Length: > 0 } entryName || Tag(xml, "visual") is not { Length: > 0 } visual) continue;
            entries.Add((entryName, visual, Tag(xml, "sound") is { Length: > 0 } sound ? sound : null, Tag(xml, "Category")));
        }
        return entries;
    }

    void HideEffect()
    {
        if (effectView == null) return;
        effectView.Hide();
        effectView.Visibility = Visibility.Collapsed;
    }

    EffectView MakeEffectView()
    {
        var view = new EffectView(context.Log) { Visibility = Visibility.Collapsed };
        ViewPart.Children.Add(view);
        return view;
    }
}
