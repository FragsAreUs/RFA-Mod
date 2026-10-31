using System.Windows;
using System.Windows.Controls;

namespace RfaEditor;

/// <summary>
/// Something open in the editor: a tab in the middle (a file preview, a table, a level). Besides
/// its own view it can give the window an outliner (the list of what is in it, top right) and an
/// inspector (the selected thing's details, bottom right), which the window shows while its tab is
/// the chosen one.
/// </summary>
interface IEditorDocument
{
    /// <summary>The tab's text; starts with "* " while there are unsaved changes.</summary>
    string Title { get; }

    /// <summary>The icon in front of the tab's text (one of <see cref="Icons"/>).</summary>
    string Icon { get; }

    FrameworkElement View { get; }
    FrameworkElement? Outliner { get; }
    FrameworkElement? Inspector { get; }

    /// <summary>Raised when the title changes (e.g. after an edit or a save).</summary>
    event Action? TitleChanged;

    /// <summary>Asks to save unsaved changes before the tab closes; false keeps it open.</summary>
    bool ConfirmClose();
}

/// <summary>What documents can use of the editor around them.</summary>
sealed class EditorContext
{
    public required Workspace Workspace { get; init; }
    public required GameAssets Assets { get; init; }

    /// <summary>Writes a line in the log panel at the bottom.</summary>
    public required Action<string> Log { get; init; }

    /// <summary>Call after files in overrides\ changed, so marks and details are brought up to date.</summary>
    public required Action OverridesChanged { get; init; }

    public required Action<GameFile> OpenTable { get; init; }
    public required Action<GameFile> OpenLevel { get; init; }
}

static class DocumentParts
{
    /// <summary>
    /// Takes a part out of a document's XAML so the window can show it elsewhere. A document lays
    /// out its outliner and inspector next to its view in one XAML file (so they can be designed and
    /// wired up together), and hands them over with this.
    /// </summary>
    public static FrameworkElement Detach(FrameworkElement part)
    {
        if (part.Parent is Panel panel) panel.Children.Remove(part);
        return part;
    }
}
