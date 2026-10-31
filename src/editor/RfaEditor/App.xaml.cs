using System.IO;
using System.Windows;
using Microsoft.Win32;
using RfaFormats;

namespace RfaEditor;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        // Nothing here is tied to one PC: the mod folder is wherever the editor was put (next to
        // rfa_mods.ini), and the game is wherever its GameDir says, or that folder itself.
        string modFolder = Workspace.FindModFolder();
        EditorLog.Start(modFolder);
        DispatcherUnhandledException += (_, args) =>
        {
            EditorLog.Crash(args.Exception);
            MessageBox.Show($"The editor ran into a problem it can't recover from and has to close:\n\n{args.Exception.Message}\n\n" +
                            $"What happened is written in {EditorLog.FilePath ?? "the log"}.",
                            "RFA Editor", MessageBoxButton.OK, MessageBoxImage.Error);
        };
        AppDomain.CurrentDomain.UnhandledException += (_, args) => { if (args.ExceptionObject is Exception error) EditorLog.Crash(error); };
        string? gameFolder = Workspace.FindGameFolder(modFolder) ?? AskForGameFolder(modFolder);
        if (gameFolder == null)
        {
            MessageBox.Show($"The editor needs the game folder (the one with {GameFolder.ExeName}). " +
                            "Set GameDir in rfa_mods.ini, or choose the folder when the editor starts.",
                            "RFA Editor", MessageBoxButton.OK, MessageBoxImage.Information);
            Shutdown();
            return;
        }
        new MainWindow(new Workspace(gameFolder, modFolder)).Show();
    }

    /// <summary>
    /// Asks where the game is (any copy: Steam, GOG, ...), and keeps the answer as GameDir in
    /// rfa_mods.ini, so neither the editor nor the launcher has to ask again.
    /// </summary>
    static string? AskForGameFolder(string modFolder)
    {
        var dialog = new OpenFolderDialog { Title = $"Where is Red Faction: Armageddon? Choose the folder with {GameFolder.ExeName}" };
        if (dialog.ShowDialog() != true || !GameFolder.IsGameFolder(dialog.FolderName)) return null;
        string ini = Path.Combine(modFolder, "rfa_mods.ini");
        if (File.Exists(ini)) IniFile.Write(ini, "Launcher", "GameDir", dialog.FolderName);
        return dialog.FolderName;
    }
}
