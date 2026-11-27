using System;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Threading.Tasks;

namespace RfaEditorMcpLauncher;

/// <summary>
/// RfaEditorMcp.exe: starts the editor's MCP server without holding bin\editor's files open.
///
/// Every Claude session set up with the rfa-editor MCP server starts one of these and keeps it running for
/// as long as the session lasts, editor open or not. Loaded the usual way, the server would hold
/// RfaEditorMcp.Server.dll, RfaFormats.dll, RfaEditor.dll and the rest open all that time, and copying a new
/// build into bin\editor would fail ("being used by another process"). So this program reads each of those
/// libraries (and its .pdb, for line numbers in errors) into memory and loads it from there: the files stay
/// free to be replaced while the server runs. A server keeps the code it started with; the session gets the
/// new build the next time it starts the server.
///
/// It doesn't exit on its own when the editor is closed: in an interactive Claude Code session a server that
/// exits stays down until it's reconnected by hand, and the server's tools work on the game's files without
/// the editor anyway.
///
/// Only this small program (RfaEditorMcp.exe and RfaEditorMcp.dll) stays open, and its build only changes
/// when this file or RfaEditorMcp.csproj does.
/// </summary>
static class Launcher
{
    static int Main(string[] args)
    {
        var libraries = new InMemoryLibraries(AppContext.BaseDirectory);
        Assembly server = libraries.LoadFromAssemblyName(new AssemblyName("RfaEditorMcp.Server"));
        MethodInfo run = server.GetType("RfaEditorMcp.Program", throwOnError: true)!
            .GetMethod("Run", BindingFlags.Public | BindingFlags.Static)
            ?? throw new MissingMethodException("RfaEditorMcp.Program", "Run");
        var running = (Task<int>)run.Invoke(null, BindingFlags.DoNotWrapExceptions, null, [args], null)!;
        return running.GetAwaiter().GetResult();
    }
}

/// <summary>
/// Loads the libraries found in one folder from copies in memory, so the files themselves aren't kept open.
/// Anything not in the folder (.NET's own libraries, WPF, Windows Forms) loads from .NET's shared folders as usual.
/// </summary>
sealed class InMemoryLibraries(string folder) : AssemblyLoadContext("RfaEditorMcp libraries in memory")
{
    protected override Assembly? Load(AssemblyName name)
    {
        // Translated resources (de\Wpf.Ui.resources.dll, ...) are in a folder named after their language.
        string file = string.IsNullOrEmpty(name.CultureName)
            ? Path.Combine(folder, name.Name + ".dll")
            : Path.Combine(folder, name.CultureName, name.Name + ".dll");
        if (!File.Exists(file)) return null;

        string symbolsFile = Path.ChangeExtension(file, ".pdb");
        using var code = new MemoryStream(File.ReadAllBytes(file));
        using MemoryStream? symbols = File.Exists(symbolsFile) ? new MemoryStream(File.ReadAllBytes(symbolsFile)) : null;
        return LoadFromStream(code, symbols);
    }

    // Native libraries (rfa_audio.dll, soft_oal.dll) aren't handled here: the server doesn't use them, and
    // Windows would load one from bin\editor and keep it open.
}
