using System.IO;
using System.Windows;
using RfaFormats;
using RfaFormats.Interface;

namespace RfaEditor;

/// <summary>
/// The preview of a strings file (.le_strings): <see cref="StringsView"/>, made the first time one is shown and laid over
/// the preview's other views, with the file's other languages beside it (as the game sees them, overrides first).
/// </summary>
public partial class FilePreview
{
    StringsView? stringsView;
    static Dictionary<uint, string>? stringTags;  // the tags' names, from the game's tables, read once a run
    static string? stringTagsFolder;

    void ShowStrings(GameFile file)
    {
        var (baseName, language) = GameStrings.Split(file.Name);
        var files = new List<(string, IReadOnlyList<(uint, string)>)>();
        string? note = null;
        try
        {
            if (language == null || file.Container != null)
                files.Add((language == null ? file.Name : LanguageName(language), Readable(GameStrings.Read(ReadAsGameSeesIt(file)), language)));
            else
            {
                // Every language of it that its archive has, in the usual order.
                Packfile pack = Workspace.Archive(file.Archive);
                foreach (var (code, name) in GameStrings.Languages)
                {
                    string sibling = $"{baseName}_{code}.le_strings";
                    if (pack.Find(sibling) == null) continue;
                    try { files.Add((name, Readable(GameStrings.Read(Workspace.Read(file.Archive, sibling)), code))); }
                    catch (InvalidDataException e) { note = $"{sibling} can't be read ({e.Message})."; }
                }
            }
        }
        catch (InvalidDataException e)
        {
            PreviewText.Text = $"This strings file can't be read: {e.Message}\n\n" + Preview.Of(ReadAsGameSeesIt(file));
            return;
        }
        if (files.Count == 0)
        {
            PreviewText.Text = Preview.Of(ReadAsGameSeesIt(file));
            return;
        }
        stringsView ??= MakeStringsView();
        PreviewText.Visibility = Visibility.Collapsed;
        stringsView.Visibility = Visibility.Visible;
        note = (note != null ? note + " " : "") + "Accented, Polish and Russian letters are shown as letters (the game stores them as its fonts' " +
               "own codes); Japanese is shown as stored.";
        stringsView.Show(language == null ? file.Name : $"{baseName}_*.le_strings", files, StringTags(), note);
    }

    // Its strings in readable letters (the game stores accented and Cyrillic letters as its fonts' codes).
    static IReadOnlyList<(uint, string)> Readable(GameStrings strings, string? language) =>
        [.. strings.Entries.Select(e => (e.Hash, GameStrings.Readable(e.Text, language)))];

    static string LanguageName(string code) => GameStrings.Languages.FirstOrDefault(l => l.Code == code).Name ?? code;

    // The tags' names: every word of the game's tables in misc.vpp_pc, hashed (under a second, once a run).
    Dictionary<uint, string> StringTags()
    {
        string? misc = Workspace.Archives().FirstOrDefault(a => Path.GetFileName(a).Equals("misc.vpp_pc", StringComparison.OrdinalIgnoreCase));
        if (stringTags != null && stringTagsFolder == misc) return stringTags;
        stringTagsFolder = misc;
        if (misc == null) return stringTags = [];
        try
        {
            Packfile pack = Packfile.Open(Workspace.ArchivePath(misc));
            var tables = pack.Entries.Where(e => e.Extension == "xtbl").ToList();
            return stringTags = GameStrings.TagNames(pack.ReadAll(tables));
        }
        catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException)
        {
            context.Log($"Strings preview: the tags' names can't be read from misc.vpp_pc ({e.Message})");
            return stringTags = [];
        }
    }

    void HideStrings()
    {
        if (stringsView == null) return;
        stringsView.Hide();
        stringsView.Visibility = Visibility.Collapsed;
    }

    StringsView MakeStringsView()
    {
        var view = new StringsView { Visibility = Visibility.Collapsed };
        ViewPart.Children.Add(view);
        return view;
    }
}
