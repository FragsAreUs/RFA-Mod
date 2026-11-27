using System.IO;
using System.Windows;

namespace RfaEditor;

/// <summary>
/// The preview of a sound bank (.bnk_pc, or its media file <c>_media.bnk_pc</c>): <see cref="SoundPreview"/>, made the first
/// time one is shown and laid over the preview's other views.
/// </summary>
public partial class FilePreview
{
    SoundPreview? soundPreview;
    string? initBankCopy;

    void ShowSoundBank(GameFile file)
    {
        soundPreview ??= MakeSoundPreview();
        PreviewText.Visibility = Visibility.Collapsed;
        soundPreview.Visibility = Visibility.Visible;
        // The bank itself, whichever of its two files was picked.
        string name = file.Name.EndsWith("_media.bnk_pc", StringComparison.OrdinalIgnoreCase) ? file.Name[..^"_media.bnk_pc".Length] + ".bnk_pc" : file.Name;
        string mediaName = name[..^".bnk_pc".Length] + "_media.bnk_pc";
        string? bankPath, mediaPath, note = null;
        if (file.Container == null && Workspace.IsLooseFolder(file.Archive))
        {
            // Ours: loose files in overrides\, read where they are.
            bankPath = Workspace.LooseFilePath(file.Archive, name);
            mediaPath = Workspace.LooseFilePath(file.Archive, mediaName);
            if (!File.Exists(mediaPath)) mediaPath = null;
        }
        else if (file.Container == null && IsUncompressedArchive(Workspace.ArchivePath(file.Archive)))
        {
            // One of Armageddon's own in sounds.vpp_pc or voices.vpp_pc: read in place, its media (Wwise Vorbis) too.
            note = "One of Armageddon's own banks, read from its archive: its sound effects play; its music, dialogue, states and " +
                   "synthesized sounds don't yet (RFA-Mod's sound engine learns them a step at a time).";
            soundPreview.ShowBank(name[..^".bnk_pc".Length], null, Workspace.ModFolder, InitBank(), note, Workspace.ArchivePath(file.Archive));
            return;
        }
        else
        {
            // One in a compressed archive (soundboot.vpp_pc, voiceboot.vpp_pc): a copy to read. Its media are in
            // sounds.vpp_pc or voices.vpp_pc; its events are listed and described.
            bankPath = Copy(file.Archive, name);
            mediaPath = null;
            note = "A copy of one of Armageddon's own banks from a compressed archive: its events can be listed and described; " +
                   "to play it, open the same bank in sounds.vpp_pc or voices.vpp_pc.";
        }
        if (bankPath == null || !File.Exists(bankPath))
        {
            soundPreview.Visibility = Visibility.Collapsed;
            PreviewText.Visibility = Visibility.Visible;
            PreviewText.Text = $"{name} isn't beside it.";
            return;
        }
        soundPreview.ShowBank(bankPath, mediaPath, Workspace.ModFolder, InitBank(), note);
    }

    // Whether an archive keeps its files uncompressed (its flags' bit 1 clear), so a bank's media can be read in place.
    static bool IsUncompressedArchive(string path)
    {
        try
        {
            using var stream = File.OpenRead(path);
            var header = new byte[0x150];
            if (stream.Read(header, 0, header.Length) != header.Length) return false;
            return BitConverter.ToUInt32(header, 0) == 0x51890ACE && BitConverter.ToUInt32(header, 4) == 6 && (BitConverter.ToUInt32(header, 0x14C) & 1) == 0;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    void HideSoundPreview()
    {
        if (soundPreview == null) return;
        soundPreview.Hide();
        soundPreview.Visibility = Visibility.Collapsed;
    }

    SoundPreview MakeSoundPreview()
    {
        var preview = new SoundPreview(context.Log) { Visibility = Visibility.Collapsed };
        ViewPart.Children.Add(preview);
        return preview;
    }

    // Armageddon's init.bnk_pc as the game will see it (our override, if there is one), for its buses' volumes.
    string? InitBank()
    {
        if (initBankCopy != null) return initBankCopy;
        string? archive = Workspace.Archives().FirstOrDefault(a => Path.GetFileName(a).Equals("soundboot.vpp_pc", StringComparison.OrdinalIgnoreCase));
        return archive == null ? null : initBankCopy = Copy(archive, "init.bnk_pc");
    }

    // A copy of a file in an archive, in the editor's temporary folder; null if it can't be read.
    string? Copy(string archive, string name)
    {
        try
        {
            string folder = Path.Combine(Path.GetTempPath(), "RfaEditor", "sound");
            Directory.CreateDirectory(folder);
            string path = Path.Combine(folder, name);
            File.WriteAllBytes(path, Workspace.Read(archive, name));
            return path;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            context.Log($"Sound preview: {name} can't be read from {archive} ({e.Message})");
            return null;
        }
    }
}
