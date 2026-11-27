using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;

namespace RfaEditor;

// The MCP servers Claude starts for this mod (see bin\: live_analysis_mcp.exe, which talks to the running
// game through rfa_live_analysis's pipe, and editor\RfaEditorMcp.exe, which edits the game's files), closed
// from the editor: the top bar's "MCP servers" button lists them, with how many of each are running.
// Closing one only ends that process; both close with the editor too. A Claude Code session that is open marks a closed
// one failed: Reconnect in its /mcp starts it again (a new session starts its own).
public partial class MainWindow
{
    static readonly (string Process, string Title)[] McpServers =
    [
        ("RfaEditorMcp", "Editor MCP server (RfaEditorMcp)"),
        ("live_analysis_mcp", "Live analysis MCP server (live_analysis_mcp)"),
    ];

    void OnMcpServers(object sender, RoutedEventArgs e)
    {
        var menu = new ContextMenu { PlacementTarget = (UIElement)sender, Placement = PlacementMode.Bottom };
        int total = 0;
        foreach (var (process, title) in McpServers)
        {
            int running = Running(process);
            total += running;
            string name = process;
            menu.Items.Add(new MenuItem
            {
                Header = $"Close the {title}" + (running == 0 ? " (not running)" : running == 1 ? "" : $" ({running} running)"),
                IsEnabled = running > 0,
                Command = new McpCommand(() => CloseMcp(name)),
            });
        }
        menu.Items.Add(new Separator());
        menu.Items.Add(new MenuItem
        {
            Header = "Close both",
            IsEnabled = total > 0,
            Command = new McpCommand(() => { foreach (var (process, _) in McpServers) CloseMcp(process); }),
        });
        menu.IsOpen = true;
    }

    static int Running(string process)
    {
        Process[] found = Process.GetProcessesByName(process);
        foreach (Process p in found) p.Dispose();
        return found.Length;
    }

    void CloseMcp(string process)
    {
        int closed = 0, failed = 0;
        foreach (Process p in Process.GetProcessesByName(process))
            using (p)
            {
                try
                {
                    p.Kill(entireProcessTree: true);
                    p.WaitForExit(2000);
                    closed++;
                }
                catch (Exception ex) when (ex is InvalidOperationException or System.ComponentModel.Win32Exception or NotSupportedException)
                {
                    failed++;
                    Log($"MCP servers: couldn't close {process} ({p.Id}): {ex.Message}");
                }
            }
        Log(closed == 0 && failed == 0
            ? $"MCP servers: {process} isn't running."
            : $"MCP servers: closed {closed} {process}" + (failed > 0 ? $", {failed} couldn't be closed" : "") +
              ". Reconnect it in a Claude session's /mcp to use its tools again.");
    }

    /// <summary>
    /// The editor is closing for certain: both MCP servers close with it, so none is left running (and holding its
    /// files) after the editor is gone. Claude Code marks a closed one failed in a session that is still open: pick
    /// Reconnect for it in that session's /mcp to use its tools again.
    /// </summary>
    static void CloseMcpServersOnExit()
    {
        foreach (var (process, _) in McpServers)
            foreach (Process p in Process.GetProcessesByName(process))
                using (p)
                {
                    try { p.Kill(entireProcessTree: true); }
                    catch (Exception ex) when (ex is InvalidOperationException or System.ComponentModel.Win32Exception or NotSupportedException)
                    {
                        EditorLog.Write($"MCP servers: couldn't close {process} ({p.Id}) on exit: {ex.Message}");
                    }
                }
    }

    /// <summary>A menu item's action.</summary>
    sealed class McpCommand(Action run) : System.Windows.Input.ICommand
    {
        public event EventHandler? CanExecuteChanged { add { } remove { } }
        public bool CanExecute(object? parameter) => true;
        public void Execute(object? parameter) => run();
    }
}
