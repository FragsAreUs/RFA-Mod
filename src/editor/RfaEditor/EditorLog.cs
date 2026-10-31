using System.Diagnostics;
using System.IO;

namespace RfaEditor;

/// <summary>
/// The log panel's lines, also written to logs\editor.log in the mod folder (the last session's
/// kept as editor.prev.log), and what went wrong if the editor crashes: the error with where it
/// happened, and how much memory the editor was using. So a crash can be looked into afterwards.
/// Nothing is sent anywhere.
/// </summary>
static class EditorLog
{
    static StreamWriter? file;
    static string? path;

    /// <summary>Where the log is written, once started.</summary>
    public static string? FilePath => path;

    public static void Start(string modFolder)
    {
        try
        {
            string folder = Path.Combine(modFolder, "logs");
            Directory.CreateDirectory(folder);
            path = Path.Combine(folder, "editor.log");
            if (File.Exists(path)) File.Copy(path, Path.Combine(folder, "editor.prev.log"), overwrite: true);
            file = new StreamWriter(path, append: false) { AutoFlush = true };
            file.WriteLine($"RFA Editor started {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
        }
        catch (IOException) { file = null; }        // another editor has it open: this one keeps no file
        catch (UnauthorizedAccessException) { file = null; }
    }

    public static void Write(string line)
    {
        try { file?.WriteLine(line); }
        catch (IOException) { }
    }

    /// <summary>Writes down a crash: the error, where it happened, and the memory in use.</summary>
    public static void Crash(Exception error)
    {
        var me = Process.GetCurrentProcess();
        Write($"{DateTime.Now:HH:mm:ss} CRASH: {error}");
        Write($"Memory: {me.PrivateMemorySize64 / (1 << 20):N0} MB private, {me.WorkingSet64 / (1 << 20):N0} MB in use, " +
              $"{GC.GetTotalMemory(false) / (1 << 20):N0} MB managed");
    }
}
