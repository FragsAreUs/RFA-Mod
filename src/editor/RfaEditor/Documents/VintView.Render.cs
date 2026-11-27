using System.Numerics;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Effects;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using RfaFormats.Interface;

namespace RfaEditor;

/// <summary>
/// Drawing an interface document: each element a canvas placed by its anchor, turned by its rotation and sized by its
/// scale inside its parent's, its alpha its opacity; a picture its image (cut out of its sheet texture and tinted by the
/// tints down the tree) source_se pixels big from its offset; a text its string (the game's English text for its tag, or
/// the words themselves) in a stand-in font as tall as the game's; a sub-document the document it names, drawn inside
/// it. Children are drawn by depth, the deepest first. Not drawn: masks (they cut their siblings in the game) and
/// multiply-blended elements (they darken what is behind them); additive blending and blur are drawn plainly; videos not.
/// </summary>
public partial class VintView
{
    readonly Dictionary<VintNode, Canvas> visuals = [];
    readonly Dictionary<(string Image, uint Tint), BitmapSource?> pictures = [];
    static readonly FontFamily StandInFont = new("Bahnschrift SemiCondensed, Segoe UI");

    void Draw()
    {
        Screen.Children.Clear();
        visuals.Clear();
        if (screen == null) return;
        // An animation playing: its values on the elements for this drawing only.
        var putBack = new List<(VintNode Node, string Name, VintValue? Value)>();
        foreach (var (node, values) in animated)
            foreach (var (name, value) in values)
            {
                putBack.Add((node, name, node.Values.TryGetValue(name, out VintValue? own) ? own : null));
                node.Values[name] = value;
            }
        try
        {
            foreach (VintNode root in screen.Roots.OrderByDescending(r => r.Depth)) AddNode(Screen, root, Vector3.One, 0);
        }
        finally
        {
            for (int i = putBack.Count - 1; i >= 0; i--)
            {
                var (node, name, value) = putBack[i];
                if (value == null) node.Values.Remove(name);
                else node.Values[name] = value;
            }
        }
        DrawSelection();
    }

    void AddNode(Panel parent, VintNode node, Vector3 parentTint, int nesting)
    {
        bool hidden = !node.Visible;
        if (hidden && HiddenBox.IsChecked != true || hiddenHere.Contains(node.Path)) return;
        // Masks cut their siblings and multiply-blended elements darken what is behind them (white ones change
        // nothing); neither can be drawn so here, so they're left out rather than painted over the screen.
        if (node.Mask || string.Equals(node.TextOf("render_mode"), "multiply", StringComparison.OrdinalIgnoreCase)) return;
        var canvas = new Canvas { Tag = node, Opacity = Math.Clamp(node.Alpha, 0, 1) * (hidden ? 0.4 : 1) };
        var place = new TransformGroup();
        place.Children.Add(new ScaleTransform(node.Scale.X, node.Scale.Y));
        place.Children.Add(new RotateTransform(node.Rotation * 180 / Math.PI));
        place.Children.Add(new TranslateTransform(node.Anchor.X, node.Anchor.Y));
        canvas.RenderTransform = place;
        Vector3 tint = parentTint * node.Tint;

        switch (node.Type)
        {
            case "bitmap" or "bitmap_circle" or "map":
                AddPicture(canvas, node, tint);
                break;
            case "text":
                AddText(canvas, node, tint);
                break;
            case "clip":
                if (node.ClipSize is Vector2 clip)
                {
                    Vector2 start = node.Offset ?? Vector2.Zero;
                    canvas.Clip = new RectangleGeometry(new Rect(start.X, start.Y, Math.Abs(clip.X), Math.Abs(clip.Y)));
                }
                break;
            case "document":
                if (nesting < 6 && SubScreen(node) is { } sub)
                    foreach (VintNode root in sub.Roots.OrderByDescending(r => r.Depth)) AddNode(canvas, root, tint, nesting + 1);
                break;
        }
        foreach (VintNode child in node.Children.OrderByDescending(c => c.Depth)) AddNode(canvas, child, tint, nesting + 1);
        parent.Children.Add(canvas);
        visuals[node] = canvas;
    }

    void AddPicture(Canvas canvas, VintNode node, Vector3 tint)
    {
        string? name = node.Image;
        BitmapSource? picture = name == null ? null : Picture(name, tint);
        Vector2 size = node.Size ?? (picture != null ? new Vector2(picture.PixelWidth, picture.PixelHeight) : new Vector2(32, 32));
        float w = Math.Abs(size.X), h = Math.Abs(size.Y);
        if (w < 0.01f || h < 0.01f) return;
        Vector2 start = node.Start(new Vector2(w, h));
        FrameworkElement shown;
        if (picture != null)
            shown = new Image { Source = picture, Width = w, Height = h, Stretch = Stretch.Fill, Tag = node };
        else
        {
            // A picture that isn't found: its place, and its name.
            var missing = new Grid { Width = w, Height = h, Tag = node, Background = Brushes.Transparent };
            missing.Children.Add(new Rectangle { Stroke = new SolidColorBrush(Color.FromArgb(160, 255, 120, 120)), StrokeDashArray = [3, 2], StrokeThickness = 1 });
            if (w > 40 && h > 10 && name != null)
                missing.Children.Add(new TextBlock { Text = name, FontSize = Math.Min(11, h * 0.8), Foreground = Brushes.IndianRed, Margin = new Thickness(2, 0, 0, 0) });
            shown = missing;
        }
        Canvas.SetLeft(shown, start.X);
        Canvas.SetTop(shown, start.Y);
        canvas.Children.Add(shown);
        if (OutlineBox.IsChecked == true) AddOutline(canvas, start, w, h);
    }

    void AddText(Canvas canvas, VintNode node, Vector3 tint)
    {
        string text = TextOf(node);
        if (text.Length == 0) return;
        string font = node.TextOf("font") ?? "font_body";
        string align = (node.TextOf("horz_align") ?? "left").ToLowerInvariant();
        float wrap = node.WrapWidth > 0 && node.WordWrap ? node.WrapWidth : 0;
        int leading = (int)(node.Values.TryGetValue("leading", out VintValue? lead) ? lead.Numbers?[0] ?? 0 : 0);
        FrameworkElement block;
        float w, h;
        // The game's own font, from its sheet; else a stand-in font as tall as the game's.
        if (GameFontsBox.IsChecked == true && GlyphPicture(font, text, tint, wrap, align, node.Shadow, leading) is { } picture)
        {
            block = new Image { Source = picture, Width = picture.PixelWidth, Height = picture.PixelHeight, Tag = node };
            (w, h) = (picture.PixelWidth, picture.PixelHeight);
        }
        else
        {
            int line = sources?.FontHeights.GetValueOrDefault(font) is int height and > 0 ? height : 40;
            var standIn = new TextBlock
            {
                Text = text,
                Tag = node,
                FontFamily = StandInFont,
                FontSize = line * 0.72,
                FontWeight = font.Contains("header") ? FontWeights.Bold : FontWeights.Normal,
                Foreground = new SolidColorBrush(Colour(tint)),
                TextAlignment = align == "center" ? TextAlignment.Center : align == "right" ? TextAlignment.Right : TextAlignment.Left,
            };
            if (wrap > 0)
            {
                standIn.Width = wrap;
                standIn.TextWrapping = TextWrapping.Wrap;
            }
            if (node.Shadow) standIn.Effect = new DropShadowEffect { Color = Colors.Black, ShadowDepth = 2, BlurRadius = 2, Opacity = 0.8 };
            standIn.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            block = standIn;
            (w, h) = ((float)standIn.DesiredSize.Width, (float)standIn.DesiredSize.Height);
        }
        Vector2 scale = node.TextScale;
        // Held at its anchor by its alignment (across) and vertical alignment, then moved by its offset.
        float x = align == "center" ? -w / 2 : align == "right" ? -w : 0;
        string vertical = (node.TextOf("vert_align") ?? "top").ToLowerInvariant();
        float y = vertical == "center" ? -h / 2 : vertical == "bottom" ? -h : 0;
        Vector2 offset = node.Offset ?? Vector2.Zero;
        var holder = new Canvas { RenderTransform = new ScaleTransform(scale.X, scale.Y) };
        Canvas.SetLeft(block, x + offset.X / Math.Max(0.01f, Math.Abs(scale.X)));
        Canvas.SetTop(block, y + offset.Y / Math.Max(0.01f, Math.Abs(scale.Y)));
        holder.Children.Add(block);
        canvas.Children.Add(holder);
        if (OutlineBox.IsChecked == true)
        {
            var outline = new Canvas { RenderTransform = new ScaleTransform(scale.X, scale.Y) };
            AddOutline(outline, new Vector2(x + offset.X / Math.Max(0.01f, Math.Abs(scale.X)), y + offset.Y / Math.Max(0.01f, Math.Abs(scale.Y))), w, h);
            canvas.Children.Add(outline);
        }
    }

    // ---- The game's fonts ----------------------------------------------------------------------------------------

    readonly Dictionary<string, (GameFont Font, byte[] Pixels, int Width, int Height)?> fonts = new(StringComparer.OrdinalIgnoreCase);
    readonly Dictionary<string, BitmapSource?> glyphPictures = [];

    // A font and its sheet's pixels (blue, green, red, alpha), read once; null if either can't be found.
    (GameFont Font, byte[] Pixels, int Width, int Height)? FontOf(string name)
    {
        if (fonts.TryGetValue(name, out var known)) return known;
        (GameFont, byte[], int, int)? found = null;
        if (sources?.Font(name) is { } font && Texture(font.TextureName) is { } sheet)
        {
            var bgra = sheet.Format == PixelFormats.Bgra32 ? sheet : new FormatConvertedBitmap(sheet, PixelFormats.Bgra32, null, 0);
            var pixels = new byte[bgra.PixelWidth * bgra.PixelHeight * 4];
            bgra.CopyPixels(pixels, bgra.PixelWidth * 4, 0);
            found = (font, pixels, bgra.PixelWidth, bgra.PixelHeight);
        }
        return fonts[name] = found;
    }

    /// <summary>
    /// Text drawn with one of the game's fonts: its letters (turned back into the game's codes) cut from the font's
    /// sheet, placed by their advances, the font's letter spacing and its kerning pairs, broken into lines at the
    /// wrap width (in the font's pixels, before the text's scale) and at line breaks, each line held left, centred or
    /// right; coloured by the tint, with a shadow below and right of it when the element has one. Null when the font or
    /// its sheet isn't found.
    /// </summary>
    BitmapSource? GlyphPicture(string fontName, string text, Vector3 tint, float wrap, string align, bool shadow, int leading)
    {
        string key = $"{fontName}|{text}|{tint}|{wrap}|{align}|{shadow}|{leading}";
        if (glyphPictures.TryGetValue(key, out BitmapSource? known)) return known;
        if (FontOf(fontName) is not var (font, sheet, sheetWidth, sheetHeight)) return glyphPictures[key] = null;

        string codes = GameStrings.GameCodes(text, "us");
        var lines = new List<string>();
        foreach (string paragraph in codes.Replace("\r", "").Split('\n'))
        {
            if (wrap <= 0 || font.Measure(paragraph) <= wrap)
            {
                lines.Add(paragraph);
                continue;
            }
            var line = new StringBuilder();
            foreach (string word in paragraph.Split(' '))
            {
                string tried = line.Length == 0 ? word : line + " " + word;
                if (line.Length > 0 && font.Measure(tried) > wrap)
                {
                    lines.Add(line.ToString());
                    line.Clear().Append(word);
                }
                else line.Clear().Append(tried);
            }
            lines.Add(line.ToString());
        }
        int step = font.Height + leading, extra = shadow ? 2 : 0;
        int width = Math.Max(1, lines.Max(font.Measure)) + extra, height = Math.Max(1, lines.Count * step - leading) + extra;
        if (width > 8192 || height > 8192) return glyphPictures[key] = null;
        var output = new byte[width * height * 4];

        void Letters(int dx, int dy, float r, float g, float b, float alpha)
        {
            for (int l = 0; l < lines.Count; l++)
            {
                string line = lines[l];
                int lineWidth = font.Measure(line);
                int pen = (align == "center" ? (width - extra - lineWidth) / 2 : align == "right" ? width - extra - lineWidth : 0) + dx;
                int top = l * step + dy;
                for (int i = 0; i < line.Length; i++)
                {
                    if (font.GlyphOf(line[i]) is { } glyph)
                    {
                        for (int y = 0; y < font.Height; y++)
                        {
                            int sy = glyph.Y + y, oy = top + y;
                            if (sy < 0 || sy >= sheetHeight || oy < 0 || oy >= height) continue;
                            for (int x = 0; x < glyph.Width; x++)
                            {
                                int sx = glyph.X + x, ox = pen + x;
                                if (sx < 0 || sx >= sheetWidth || ox < 0 || ox >= width) continue;
                                int s = (sy * sheetWidth + sx) * 4, o = (oy * width + ox) * 4;
                                float a = sheet[s + 3] / 255f * alpha;
                                if (a <= 0) continue;
                                float keep = 1 - a;
                                output[o] = (byte)Math.Min(255, sheet[s] * b * a + output[o] * keep);
                                output[o + 1] = (byte)Math.Min(255, sheet[s + 1] * g * a + output[o + 1] * keep);
                                output[o + 2] = (byte)Math.Min(255, sheet[s + 2] * r * a + output[o + 2] * keep);
                                output[o + 3] = (byte)Math.Min(255, 255 * a + output[o + 3] * keep);
                            }
                        }
                        pen += glyph.Advance;
                    }
                    if (i + 1 < line.Length) pen += font.Spacing + font.Kern(line[i], line[i + 1]);
                }
            }
        }
        if (shadow) Letters(2, 2, 0, 0, 0, 0.8f);
        Letters(0, 0, Math.Clamp(tint.X, 0, 1), Math.Clamp(tint.Y, 0, 1), Math.Clamp(tint.Z, 0, 1), 1);
        // Premultiplied as written: unpremultiply for a plain Bgra32 picture.
        for (int o = 0; o < output.Length; o += 4)
        {
            int a = output[o + 3];
            if (a is 0 or 255) continue;
            output[o] = (byte)Math.Min(255, output[o] * 255 / a);
            output[o + 1] = (byte)Math.Min(255, output[o + 1] * 255 / a);
            output[o + 2] = (byte)Math.Min(255, output[o + 2] * 255 / a);
        }
        var picture = BitmapSource.Create(width, height, 96, 96, PixelFormats.Bgra32, null, output, width * 4);
        picture.Freeze();
        return glyphPictures[key] = picture;
    }

    static void AddOutline(Canvas canvas, Vector2 start, float w, float h)
    {
        var box = new Rectangle { Width = w, Height = h, Stroke = new SolidColorBrush(Color.FromArgb(110, 120, 200, 255)), StrokeThickness = 1, IsHitTestVisible = false };
        Canvas.SetLeft(box, start.X);
        Canvas.SetTop(box, start.Y);
        canvas.Children.Add(box);
    }

    /// <summary>What a text element shows: the game's English text for its tag, or the tag's words themselves; upper or
    /// lower case as its force_case says.</summary>
    string TextOf(VintNode node)
    {
        string tag = node.TextOf("text_tag") ?? "";
        string text = tag.Length > 0 && sources != null && sources.Strings.TryGetValue(GameStrings.Hash(tag), out string? found) ? found : tag;
        return (node.TextOf("force_case") ?? "").ToLowerInvariant() switch
        {
            "upper" => text.ToUpperInvariant(),
            "lower" => text.ToLowerInvariant(),
            _ => text,
        };
    }

    // The document a "document" element shows, read once (and kept, with any changes tried on it).
    VintScreen? SubScreen(VintNode node)
    {
        string? name = node.TextOf("document_name");
        if (string.IsNullOrEmpty(name) || sources == null || screen == null) return null;
        if (subScreens.TryGetValue(name, out VintScreen? known)) return known;
        VintScreen? sub = null;
        try
        {
            // Its tree read once, and kept (with changes made to it here) until saved or undone.
            if (!subTrees.TryGetValue(name, out var entry)) subTrees[name] = entry = sources.Document(name);
            if (entry is var (tree, file))
            {
                sub = VintScreen.Read(tree, screen.Resolution);
                subFiles[name] = file;
            }
        }
        catch (Exception e) when (e is System.IO.InvalidDataException or ArgumentException or IndexOutOfRangeException)
        {
            sources.Log($"Interface preview: the sub-document {name} can't be read ({e.Message})");
        }
        return subScreens[name] = sub;
    }

    // ---- Pictures ----------------------------------------------------------------------------------------------

    // A picture by name, tinted: a piece of its sheet texture (bitmap_sheets.xtbl), or a whole texture of that name.
    BitmapSource? Picture(string name, Vector3 tint)
    {
        uint key = (uint)(Math.Clamp(tint.X, 0, 1) * 255) << 16 | (uint)(Math.Clamp(tint.Y, 0, 1) * 255) << 8 | (uint)(Math.Clamp(tint.Z, 0, 1) * 255);
        if (pictures.TryGetValue((name, key), out BitmapSource? known)) return known;
        BitmapSource? picture = null;
        if (sources != null)
        {
            if (sources.Sheets.TryGetValue(name, out var piece) && Texture(piece.Sheet) is { } sheet)
            {
                int x = Math.Clamp(piece.X, 0, sheet.PixelWidth - 1), y = Math.Clamp(piece.Y, 0, sheet.PixelHeight - 1);
                int w = Math.Clamp(piece.Width, 1, sheet.PixelWidth - x), h = Math.Clamp(piece.Height, 1, sheet.PixelHeight - y);
                picture = new CroppedBitmap(sheet, new Int32Rect(x, y, w, h));
            }
            else picture = Texture(name);
        }
        if (picture != null) picture = Tinted(picture, tint);
        return pictures[(name, key)] = picture;
    }

    BitmapSource? Texture(string name)
    {
        if (sources == null) return null;
        string withTga = name.EndsWith(".tga", StringComparison.OrdinalIgnoreCase) ? name : name + ".tga";
        return sources.Texture(withTga) ?? sources.Texture(name);
    }

    // A picture with each pixel's colour multiplied by the tint (as the game's tint does).
    static BitmapSource Tinted(BitmapSource source, Vector3 tint)
    {
        var bgra = source.Format == PixelFormats.Bgra32 ? source : new FormatConvertedBitmap(source, PixelFormats.Bgra32, null, 0);
        int w = bgra.PixelWidth, h = bgra.PixelHeight;
        var pixels = new byte[w * h * 4];
        bgra.CopyPixels(pixels, w * 4, 0);
        if (tint != Vector3.One)
            for (int i = 0; i < pixels.Length; i += 4)
            {
                pixels[i] = (byte)(pixels[i] * Math.Clamp(tint.Z, 0, 1));
                pixels[i + 1] = (byte)(pixels[i + 1] * Math.Clamp(tint.Y, 0, 1));
                pixels[i + 2] = (byte)(pixels[i + 2] * Math.Clamp(tint.X, 0, 1));
            }
        var result = BitmapSource.Create(w, h, 96, 96, PixelFormats.Bgra32, null, pixels, w * 4);
        result.Freeze();
        return result;
    }

    static Color Colour(Vector3 tint) => Color.FromRgb((byte)(Math.Clamp(tint.X, 0, 1) * 255), (byte)(Math.Clamp(tint.Y, 0, 1) * 255), (byte)(Math.Clamp(tint.Z, 0, 1) * 255));

    // ---- Picking -------------------------------------------------------------------------------------------------

    // A click on the screen picks the picture or text under it (the nearest element of what was hit).
    void OnScreenClick(object sender, MouseButtonEventArgs e)
    {
        DependencyObject? at = e.OriginalSource as DependencyObject;
        while (at != null && at != Screen)
        {
            if (at is FrameworkElement { Tag: VintNode node })
            {
                Select(node);
                return;
            }
            at = VisualTreeHelper.GetParent(at);
        }
    }

    // A dashed box round the picked element (all it draws), or a cross at its place if it draws nothing.
    void DrawSelection()
    {
        Overlay.Children.Clear();
        if (selected == null || !visuals.TryGetValue(selected, out Canvas? canvas) || !Screen.IsVisible) return;
        Screen.UpdateLayout();  // the new visuals measured, so their bounds are known
        Rect bounds = VisualTreeHelper.GetDescendantBounds(canvas);
        var brush = new SolidColorBrush(Color.FromRgb(255, 170, 40));
        if (bounds.IsEmpty || bounds.Width < 1 && bounds.Height < 1)
        {
            Point p = canvas.TransformToAncestor(Screen).Transform(new Point(0, 0));
            Overlay.Children.Add(new Line { X1 = p.X - 8, Y1 = p.Y, X2 = p.X + 8, Y2 = p.Y, Stroke = brush, StrokeThickness = 2 });
            Overlay.Children.Add(new Line { X1 = p.X, Y1 = p.Y - 8, X2 = p.X, Y2 = p.Y + 8, Stroke = brush, StrokeThickness = 2 });
            return;
        }
        Rect onScreen = canvas.TransformToAncestor(Screen).TransformBounds(bounds);
        var box = new Rectangle { Width = onScreen.Width, Height = onScreen.Height, Stroke = brush, StrokeThickness = 2, StrokeDashArray = [4, 2] };
        Canvas.SetLeft(box, onScreen.X);
        Canvas.SetTop(box, onScreen.Y);
        Overlay.Children.Add(box);
    }
}
