using System.IO;
using System.Text;
using System.Windows;
using RfaFormats;
using RfaFormats.Audio;

namespace RfaEditor;

/// <summary>
/// The preview of a conversation (.ctdg_pc): <see cref="ConversationView"/>, made the first time one is shown and laid
/// over the preview's other views. Its names come from the game's tables as the game sees them (overrides first).
/// </summary>
public partial class FilePreview
{
    ConversationView? conversationView;
    VoiceNames? voiceNames;

    void ShowConversation(GameFile file)
    {
        ConversationFile conversation;
        try
        {
            conversation = ConversationFile.Read(ReadAsGameSeesIt(file));
        }
        catch (InvalidDataException e)
        {
            PreviewText.Text = $"This conversation can't be read: {e.Message}\n\n" + Preview.Of(ReadAsGameSeesIt(file));
            return;
        }
        conversationView ??= MakeConversationView();
        PreviewText.Visibility = Visibility.Collapsed;
        conversationView.Visibility = Visibility.Visible;

        // voices.vpp_pc: its banks read in place (it is stored uncompressed), so the lines can play.
        string? voices = Workspace.Archives().FirstOrDefault(a => Path.GetFileName(a).Equals("voices.vpp_pc", StringComparison.OrdinalIgnoreCase));
        Packfile? voicesPack = voices == null ? null : Workspace.Archive(voices);
        string? voicesPath = voices != null && IsUncompressedArchive(Workspace.ArchivePath(voices)) ? Workspace.ArchivePath(voices) : null;
        voiceNames ??= ReadVoiceNames(voicesPack);
        conversationView.Show(conversation, file.Name, voiceNames, voicesPath, name => voicesPack?.Find(name) != null, InitBank());
    }

    // The personas' and lines' names: audio_personas.xtbl and audio_line_tags.xtbl, and voices.vpp_pc's bank names.
    VoiceNames ReadVoiceNames(Packfile? voicesPack)
    {
        string? Table(string name) => Workspace.ReadAsGameSeesIt(name) is { } bytes ? Encoding.Latin1.GetString(bytes) : null;
        var bankNames = voicesPack?.Entries.Where(e => e.Extension == "bnk_pc" && !e.Name.EndsWith("_media.bnk_pc", StringComparison.OrdinalIgnoreCase))
                                           .Select(e => e.Name[..^".bnk_pc".Length]);
        return new VoiceNames(Table("audio_personas.xtbl"), Table("audio_line_tags.xtbl"), bankNames);
    }

    void HideConversation()
    {
        if (conversationView == null) return;
        conversationView.Hide();
        conversationView.Visibility = Visibility.Collapsed;
    }

    ConversationView MakeConversationView()
    {
        var view = new ConversationView(context.Log) { Visibility = Visibility.Collapsed };
        ViewPart.Children.Add(view);
        return view;
    }
}
