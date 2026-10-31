using System.Windows;
using System.Windows.Media;

namespace RfaEditor;

/// <summary>Colours and styles from Theme.xaml, for controls made in code.</summary>
static class Theme
{
    public static Brush Brush(string key) => (Brush)Application.Current.FindResource(key);

    public static Style Style(string key) => (Style)Application.Current.FindResource(key);

    /// <summary>An icon (one of <see cref="Icons"/>) as text, at a size and colour.</summary>
    public static System.Windows.Controls.TextBlock Icon(string icon, double size = 14, Brush? colour = null)
    {
        var text = new System.Windows.Controls.TextBlock { Text = icon, Style = Style("Icon"), FontSize = size };
        if (colour != null) text.Foreground = colour;
        return text;
    }
}
