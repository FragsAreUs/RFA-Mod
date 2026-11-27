using System.Diagnostics;
using System.IO;
using System.Windows;

namespace RfaEditor;

// The top bar's "Mod settings": opens RfaSettings.exe, the window for rfa_mods.ini, which is next to
// the editor in bin\editor\ (a program of its own, so players can open it without the editor).
public partial class MainWindow
{
    void OnModSettings(object sender, RoutedEventArgs e)
    {
        string exe = System.IO.Path.Combine(AppContext.BaseDirectory, "RfaSettings.exe");
        if (!File.Exists(exe))
        {
            Log($"Mod settings: there is no {exe}; build.cmd builds it next to the editor.");
            return;
        }
        try
        {
            Process.Start(new ProcessStartInfo(exe) { UseShellExecute = false, WorkingDirectory = AppContext.BaseDirectory })?.Dispose();
        }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException)
        {
            Log($"Mod settings: couldn't start {exe}: {ex.Message}");
        }
    }
}
