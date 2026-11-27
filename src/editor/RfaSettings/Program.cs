// RfaSettings.exe: a window for RFA-Mod's settings file, rfa_mods.ini, so you don't have to read its
// hundreds of lines to change one. Every setting shows with the comments the file has for it, a
// fitting control, whether it differs from this version's default (defaults\rfa_mods.ini) and when
// the game takes a change; Save changes only the values you changed. RfaFormats' ModSettings reads
// and writes the file.
//
//   RfaSettings.exe                      the window, for the rfa_mods.ini of the RFA-Mod it is in
//   RfaSettings.exe <file.ini>           the window, for that file (defaults\ still from this RFA-Mod)
//   RfaSettings.exe describe [file.ini]  lists each setting as the window sees it (kind, default, timing)
//   RfaSettings.exe set <file.ini> <Section>.<Key>=<value> ...   sets those values, as Save does
//   --template <defaults.ini> after any of them reads the defaults from that file instead.
// The two commands write to their output only when it is sent to a pipe or file (| more, > out.txt).
using System.IO;
using System.Text;
using System.Windows;
using RfaFormats;

namespace RfaSettings;

static class Program
{
    /// <summary>The RFA-Mod folder this program is in: editor\'s parent, with rfa_mods.ini or defaults\ in it.</summary>
    public static string ModFolder()
    {
        string here = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
        string? parent = Path.GetDirectoryName(here);
        if (parent != null && (File.Exists(Path.Combine(parent, "rfa_mods.ini")) || File.Exists(Path.Combine(parent, "defaults", "rfa_mods.ini"))))
            return parent;
        return here;
    }

    [STAThread]
    static int Main(string[] args)
    {
        var words = new List<string>(args);
        string? template = null;
        int templateAt = words.FindIndex(w => w.Equals("--template", StringComparison.OrdinalIgnoreCase));
        if (templateAt >= 0 && templateAt + 1 < words.Count)
        {
            template = Path.GetFullPath(words[templateAt + 1]);
            words.RemoveRange(templateAt, 2);
        }
        string folder = ModFolder();
        template ??= Path.Combine(folder, "defaults", "rfa_mods.ini");

        if (words.Count > 0 && words[0].Equals("describe", StringComparison.OrdinalIgnoreCase))
            return Command(() => Describe(words.Count > 1 ? Path.GetFullPath(words[1]) : Path.Combine(folder, "rfa_mods.ini"), template));
        if (words.Count > 0 && words[0].Equals("set", StringComparison.OrdinalIgnoreCase))
            return Command(() => Set(words.Skip(1).ToList(), template));

        string ini = words.Count > 0 ? Path.GetFullPath(words[0]) : Path.Combine(folder, "rfa_mods.ini");
        var app = new Application { ShutdownMode = ShutdownMode.OnMainWindowClose };
        RfaEditor.FluentLook.Load(app, new Uri("pack://application:,,,/RfaSettings;component/Theme.xaml"));
        return app.Run(new MainWindow(ini, template));
    }

    static int Command(Func<int> run)
    {
        var output = new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false)) { AutoFlush = true };
        Console.SetOut(output);
        Console.SetError(output);
        try { return run(); }
        catch (IOException e)
        {
            Console.Error.WriteLine(e.Message);
            return 1;
        }
    }

    static int Describe(string ini, string template)
    {
        ModSettings settings = ModSettings.Load(ini, template);
        Console.WriteLine($"{settings.Path} (defaults: {settings.TemplatePath ?? "none"})");
        foreach (ModSettingsSection section in settings.Sections)
        {
            Console.WriteLine();
            Console.WriteLine($"[{section.Name}]{(section.Title.Length > 0 ? "  " + section.Title : "")}{(section.InDefaults ? "" : "  (only in your file)")}");
            foreach (ModSetting s in section.Settings)
            {
                var notes = new List<string> { s.Kind.ToString() };
                if (s.Timing != SettingTiming.Unknown) notes.Add(s.Timing.ToString());
                if (s.Range is var (min, max)) notes.Add($"{min}-{max}");
                if (s.Choices.Count > 0) notes.Add("choices " + string.Join(" | ", s.Choices.Select(c => $"{(c.Value.Length == 0 ? "(empty)" : c.Value)} = {c.Meaning}")));
                if (!s.HasDefault) notes.Add("not in the defaults");
                else if (!s.IsDefault(s.Value)) notes.Add($"changed, default {s.Default}");
                if (!s.InYourFile) notes.Add("not in your file yet");
                if (s.SharesDescriptionOf != null) notes.Add("shares " + s.SharesDescriptionOf);
                Console.WriteLine($"  {s.Key}={s.Value}  ({string.Join("; ", notes)})");
            }
        }
        return 0;
    }

    static int Set(List<string> words, string template)
    {
        if (words.Count < 2)
        {
            Console.Error.WriteLine("RfaSettings.exe set <file.ini> <Section>.<Key>=<value> ...");
            return 1;
        }
        ModSettings settings = ModSettings.Load(Path.GetFullPath(words[0]), template);
        var changes = new Dictionary<ModSetting, string>();
        foreach (string word in words.Skip(1))
        {
            int dot = word.IndexOf('.'), equals = word.IndexOf('=');
            if (dot <= 0 || equals < dot)
            {
                Console.Error.WriteLine($"\"{word}\" isn't <Section>.<Key>=<value>");
                return 1;
            }
            string section = word[..dot], key = word[(dot + 1)..equals], value = word[(equals + 1)..];
            ModSetting? setting = settings.All.FirstOrDefault(s =>
                s.Section.Equals(section, StringComparison.OrdinalIgnoreCase) && s.Key.Equals(key, StringComparison.OrdinalIgnoreCase));
            if (setting == null)
            {
                Console.Error.WriteLine($"There is no {key} under [{section}].");
                return 1;
            }
            changes[setting] = value;
        }
        ModSettingsSaveResult result = settings.Save(changes);
        foreach (string line in result.Written) Console.WriteLine("Set " + line);
        foreach (string line in result.ChangedMeanwhile) Console.WriteLine("Changed by something else meanwhile: " + line);
        return 0;
    }
}
