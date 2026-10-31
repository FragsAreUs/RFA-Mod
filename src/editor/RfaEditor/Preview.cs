using System.Text;

namespace RfaEditor;

/// <summary>What the preview pane shows for a file: its text, or a hex dump of its start.</summary>
static class Preview
{
    const int MaxTextBytes = 1024 * 1024;
    const int HexBytes = 4096;

    public static string Of(byte[] data)
    {
        if (LooksLikeText(data))
        {
            string text = Encoding.UTF8.GetString(data, 0, Math.Min(data.Length, MaxTextBytes));
            return data.Length > MaxTextBytes ? text + "\n\n(only the first 1 MB is shown)" : text;
        }
        return HexDump(data, HexBytes) +
               (data.Length > HexBytes ? $"\n(binary file; the first {HexBytes} of {data.Length:N0} bytes are shown)" : "");
    }

    /// <summary>Text files (.xtbl, .lua, .txt, ...) have no zero bytes and almost no control characters.</summary>
    static bool LooksLikeText(byte[] data)
    {
        int sample = Math.Min(data.Length, 8192);
        if (sample == 0) return true;
        int odd = 0;
        for (int i = 0; i < sample; i++)
        {
            byte b = data[i];
            if (b == 0) return false;
            if (b < 0x20 && b != '\r' && b != '\n' && b != '\t') odd++;
        }
        return odd * 100 < sample;
    }

    static string HexDump(byte[] data, int limit)
    {
        var text = new StringBuilder();
        int length = Math.Min(data.Length, limit);
        for (int row = 0; row < length; row += 16)
        {
            text.Append($"{row:X8}  ");
            for (int i = 0; i < 16; i++)
                text.Append(row + i < length ? $"{data[row + i]:X2} " : "   ");
            text.Append(' ');
            for (int i = 0; i < 16 && row + i < length; i++)
            {
                byte b = data[row + i];
                text.Append(b >= 0x20 && b < 0x7F ? (char)b : '.');
            }
            text.AppendLine();
        }
        return text.ToString();
    }
}
