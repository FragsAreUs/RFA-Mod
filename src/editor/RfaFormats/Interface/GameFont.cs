using System.Text;

namespace RfaFormats.Interface;

/// <summary>
/// One of the game's fonts (.vf3_pc, 15 in interface.vpp_pc): its letters' places on its sheet texture
/// (font_body_nobdr.tga in font_body_nobdr.cvbm_pc, ...), their widths, and how pairs of letters close up. Layout
/// (little-endian, read from all 15):
/// <list type="table">
/// <item><term>+0x00</term><description>"VFNT", version 2</description></item>
/// <item><term>+0x08</term><description>glyph count (168, 224, 247; 1,481 for Japanese), +0x0C the first character (32): glyph i is character 32 + i</description></item>
/// <item><term>+0x14, +0x16</term><description>u16s: the baseline (font_body 47) and the glyph height (50; rows on the sheet are 3 pixels more apart)</description></item>
/// <item><term>+0x1C</term><description>the space put between letters (−2, 0, −3)</description></item>
/// <item><term>+0x20</term><description>the kerning pair count</description></item>
/// <item><term>+0x28, +0x68</term><description>its .peg's and its texture's names (64 bytes each)</description></item>
/// <item><term>+0xBC</term><description>the kerning pairs, 4 bytes each: first letter, second letter, a signed move, a pad byte</description></item>
/// <item><term>then</term><description>per glyph 16 bytes: its advance (u32), its width (u32), 0, the index of its first kerning pair (s16, −1 for none), 0</description></item>
/// <item><term>then</term><description>per glyph its x on the sheet (u32), then per glyph its y (u32)</description></item>
/// </list>
/// The letters are the game's own codes, not Unicode past ASCII (see <see cref="GameStrings.Readable"/>).
/// </summary>
public sealed class GameFont
{
    public readonly record struct Glyph(int Advance, int Width, int X, int Y, int FirstKerning);

    public int FirstCharacter { get; }
    public int Baseline { get; }
    public int Height { get; }
    public int Spacing { get; }
    public string TextureName { get; }
    public IReadOnlyList<Glyph> Glyphs { get; }
    public IReadOnlyList<(int First, int Second, int Move)> Kerning { get; }

    GameFont(int first, int baseline, int height, int spacing, string texture, Glyph[] glyphs, (int, int, int)[] kerning)
    {
        FirstCharacter = first;
        Baseline = baseline;
        Height = height;
        Spacing = spacing;
        TextureName = texture;
        Glyphs = glyphs;
        Kerning = kerning;
    }

    public static GameFont Read(byte[] data)
    {
        if (data.Length < 0xBC || Encoding.ASCII.GetString(data, 0, 4) != "VFNT") throw new InvalidDataException("not a font (.vf3_pc)");
        int count = (int)BitConverter.ToUInt32(data, 0x08), first = (int)BitConverter.ToUInt32(data, 0x0C);
        int kerningCount = (int)BitConverter.ToUInt32(data, 0x20);
        int glyphsAt = 0xBC + 4 * kerningCount, xsAt = glyphsAt + 16 * count, ysAt = xsAt + 4 * count;
        if (count <= 0 || ysAt + 4 * count > data.Length) throw new InvalidDataException("the font's tables don't fit in it");
        var kerning = new (int, int, int)[kerningCount];
        for (int k = 0; k < kerningCount; k++) kerning[k] = (data[0xBC + 4 * k], data[0xBD + 4 * k], (sbyte)data[0xBE + 4 * k]);
        var glyphs = new Glyph[count];
        for (int i = 0; i < count; i++)
        {
            int at = glyphsAt + 16 * i;
            glyphs[i] = new Glyph((int)BitConverter.ToUInt32(data, at), (int)BitConverter.ToUInt32(data, at + 4),
                                  (int)BitConverter.ToUInt32(data, xsAt + 4 * i), (int)BitConverter.ToUInt32(data, ysAt + 4 * i), BitConverter.ToInt16(data, at + 12));
        }
        int end = Array.IndexOf(data, (byte)0, 0x68, 0x40);
        string texture = Encoding.Latin1.GetString(data, 0x68, (end < 0 ? 0xA8 : end) - 0x68);
        return new GameFont(first, BitConverter.ToUInt16(data, 0x14), BitConverter.ToUInt16(data, 0x16), BitConverter.ToInt32(data, 0x1C), texture, glyphs, kerning);
    }

    /// <summary>The glyph of a letter (in the game's codes), or null if the font hasn't it.</summary>
    public Glyph? GlyphOf(char code)
    {
        int i = code - FirstCharacter;
        return i >= 0 && i < Glyphs.Count && Glyphs[i].Width > 0 ? Glyphs[i] : null;
    }

    /// <summary>How far the second letter moves toward the first (its kerning pair's move), or 0.</summary>
    public int Kern(char first, char second)
    {
        if (GlyphOf(first) is not { FirstKerning: >= 0 } glyph) return 0;
        for (int k = glyph.FirstKerning; k < Kerning.Count && Kerning[k].First == first; k++)
            if (Kerning[k].Second == second) return Kerning[k].Move;
        return 0;
    }

    /// <summary>How wide a line of letters (in the game's codes) is: advances, the space between letters, kerning.</summary>
    public int Measure(string line)
    {
        int width = 0;
        for (int i = 0; i < line.Length; i++)
        {
            width += (GlyphOf(line[i])?.Advance ?? 0) + (i + 1 < line.Length ? Spacing + Kern(line[i], line[i + 1]) : 0);
        }
        return Math.Max(0, width);
    }
}
