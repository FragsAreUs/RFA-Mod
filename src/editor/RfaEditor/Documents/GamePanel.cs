using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;

namespace RfaEditor;

/// <summary>
/// The Game panel: the running game's window, docked inside the editor. The game keeps running on its
/// own (it is its own process); its window is made a child of one of the editor's, sized to fill the
/// panel. Click into it to play; Shift+F1 gives the mouse
/// and keys back to the editor. Undock or closing the editor gives the window back as it was. The
/// panel docks like the others (drag its title), so the game can sit beside a level, below it, or in
/// a window of its own.
///
/// The game has to be in Windowed mode (Options > Display; with rfa_qol's borderless mod that is
/// borderless fullscreen): its exclusive Fullscreen mode owns the screen and can't be docked. The
/// borderless mod knows when the window is docked (see src\mods\rfa_qol\borderless.cpp): it leaves
/// its size to the editor, keeps the game running when you click away (no pause menu) and mutes the
/// game's input while you work in the editor.
/// </summary>
sealed class GamePanel
{
    public FrameworkElement View { get; }

    const int HotKeyId = 0x5246;  // Shift+F1: back to the editor

    readonly EditorContext context;
    readonly GameHost host = new();
    readonly TextBlock status = new() { VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis };
    readonly Button startButton, dockButton, backButton;
    readonly CheckBox autoDockBox;
    readonly DispatcherTimer watch = new() { Interval = TimeSpan.FromMilliseconds(500) };

    IntPtr game;            // the game's window while docked
    Native.Saved saved;     // how it was before it was docked
    bool hotKeyRegistered;
    DateTime startedAt = DateTime.MinValue;


    static GamePanel? current;

    static bool controllerToGame;

    /// <summary>
    /// Whether the controller is the docked game's now, not the level editor's. Both read the same
    /// controller (one stick would fly the editor's camera and walk the player at once), so it is
    /// one's or the other's: swapped with the Controller button in either view, or by pressing both
    /// sticks in. The game is told through a property on its window, which the borderless mod reads.
    /// </summary>
    public static bool ControllerToGame => controllerToGame && current?.Docked == true;

    /// <summary>Raised when the controller changes hands (or the game is docked or undocked).</summary>
    public static event Action? ControllerChanged;

    public static void SwapController()
    {
        controllerToGame = !controllerToGame;
        current?.TellGameController();
        current?.context.Log(current.Docked
            ? (controllerToGame ? "Controller: the game's now." : "Controller: the editor's now.")
            : "Controller: the editor's (the game isn't docked). It goes to the game once it is docked, if set to.");
        ControllerChanged?.Invoke();
    }

    /// <summary>The text for a Controller button.</summary>
    public static string ControllerLabel => ControllerToGame ? "Controller: Game" : "Controller: Editor";

    void TellGameController()
    {
        if (Docked) Native.SetProp(game, ControllerProperty, controllerToGame ? 1 : 2);
    }

    const string ControllerProperty = "RFAModController";

    /// <summary>Where the panel can be put with its Place button (it can also be dragged anywhere by its title).</summary>
    public enum Place { Tab, Below, Beside, Window }

    public GamePanel(EditorContext context, Action<Place> place)
    {
        this.context = context;
        current = this;
        startButton = ToolbarButton(Icons.Start, "Start the game", "Starts the game through RFAModLauncher (Steam must be running)", OnStart);
        dockButton = ToolbarButton(Icons.Game, "Dock", "Takes the running game's window into this panel", OnDockOrUndock);
        backButton = ToolbarButton(Icons.Select, "Back to the editor", "Gives the mouse and keys back to the editor (Shift+F1 does too)", (_, _) => BackToEditor());
        Button controllerButton = ToolbarButton(Icons.Game, ControllerLabel,
            "Whose the controller is: the docked game's or the level editor's. Click to swap (or press both sticks in).", (_, _) => SwapController());
        ControllerChanged += () => SetLabel(controllerButton, ControllerLabel);
        Button placeButton = ToolbarButton(Icons.Display, "Place", "Where this panel sits. You can also drag it by its title onto any side of any panel.", (_, _) => { });
        var places = new ContextMenu();
        foreach ((string text, Place where) in new[]
                 {
                     ("Under the documents (below the level view)", Place.Below), ("Beside the documents", Place.Beside),
                     ("As a tab with the documents", Place.Tab), ("In a window of its own", Place.Window),
                 })
        {
            var item = new MenuItem { Header = text };
            item.Click += (_, _) => place(where);
            places.Items.Add(item);
        }
        placeButton.Click += (_, _) =>
        {
            places.PlacementTarget = placeButton;
            places.Placement = System.Windows.Controls.Primitives.PlacementMode.Bottom;
            places.IsOpen = true;
        };
        autoDockBox = new CheckBox
        {
            Content = "Dock automatically", IsChecked = true, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(10, 0, 10, 0),
            ToolTip = "Docks the game here as soon as its window is up, while this panel is showing",
        };

        var tools = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        tools.Children.Add(startButton);
        tools.Children.Add(dockButton);
        tools.Children.Add(backButton);
        tools.Children.Add(controllerButton);
        tools.Children.Add(placeButton);
        tools.Children.Add(autoDockBox);
        var bar = new DockPanel();
        DockPanel.SetDock(tools, System.Windows.Controls.Dock.Left);
        bar.Children.Add(tools);
        bar.Children.Add(status);
        var toolbar = new Border
        {
            Background = (Brush)Application.Current.FindResource("PanelHeaderBrush"),
            BorderBrush = (Brush)Application.Current.FindResource("BorderBrush"),
            BorderThickness = new Thickness(0, 0, 0, 1), Padding = new Thickness(6, 4, 6, 4), Child = bar,
        };
        var view = new DockPanel { Background = Brushes.Black };
        DockPanel.SetDock(toolbar, System.Windows.Controls.Dock.Top);
        view.Children.Add(toolbar);
        view.Children.Add(host);
        View = view;

        host.Resized += Fit;
        host.HotKey += BackToEditor;
        host.Destroying += Undock;  // a child window dies with its parent: never take the game with it
        watch.Tick += (_, _) => Watch();
        watch.Start();
        // The editor going away for any reason gives the game its window back first.
        AppDomain.CurrentDomain.ProcessExit += (_, _) => Release();
        AppDomain.CurrentDomain.UnhandledException += (_, _) => Release();
        Application.Current.Exit += (_, _) => Release();
        Watch();
    }

    /// <summary>Gives the game its own window back (the editor is closing).</summary>
    public void Release()
    {
        Undock();
        watch.Stop();
    }

    Button ToolbarButton(string icon, string text, string tip, RoutedEventHandler click)
    {
        var content = new StackPanel { Orientation = Orientation.Horizontal };
        content.Children.Add(new TextBlock { Text = icon, Style = (Style)Application.Current.FindResource("Icon"), FontSize = 12, Margin = new Thickness(0, 0, 6, 0) });
        var label = new TextBlock { Text = text };
        content.Children.Add(label);
        var button = new Button { Content = content, ToolTip = tip, Margin = new Thickness(0, 0, 6, 0), Tag = label };
        button.Click += click;
        return button;
    }

    static void SetLabel(Button button, string text) => ((TextBlock)button.Tag).Text = text;

    bool Docked => game != IntPtr.Zero;

    // ---- Starting and finding the game ---------------------------------------------------------

    void OnStart(object sender, RoutedEventArgs e)
    {
        string launcher = Path.Combine(context.Workspace.ModFolder, "RFAModLauncher.exe");
        if (!File.Exists(launcher))
        {
            context.Log($"Can't start the game: {launcher} isn't there. Build it with build.cmd.");
            return;
        }
        try
        {
            Process.Start(new ProcessStartInfo(launcher) { UseShellExecute = true, WorkingDirectory = context.Workspace.ModFolder });
            startedAt = DateTime.Now;
            autoDockBox.IsChecked = true;
            context.Log("Starting the game. It is docked in the Game panel once its window is up; set Options > Display to Windowed if it isn't.");
        }
        catch (Exception ex)
        {
            context.Log("Starting the game failed: " + ex.Message);
        }
        Watch();
    }

    /// <summary>The game's main window (the biggest visible top-level window of its process), or zero.</summary>
    static IntPtr FindGameWindow()
    {
        string name = Path.GetFileNameWithoutExtension(RfaFormats.GameFolder.ExeName);
        var ids = new HashSet<uint>();
        foreach (Process process in Process.GetProcessesByName(name))
        {
            ids.Add((uint)process.Id);
            process.Dispose();
        }
        if (ids.Count == 0) return IntPtr.Zero;
        IntPtr best = IntPtr.Zero;
        long bestArea = 0;
        Native.EnumWindows((hwnd, _) =>
        {
            Native.GetWindowThreadProcessId(hwnd, out uint pid);
            if (!ids.Contains(pid) || !Native.IsWindowVisible(hwnd) || Native.GetWindow(hwnd, Native.GW_OWNER) != IntPtr.Zero) return true;
            Native.GetWindowRect(hwnd, out Native.RECT r);
            long area = (long)(r.Right - r.Left) * (r.Bottom - r.Top);
            if (area > bestArea)
            {
                best = hwnd;
                bestArea = area;
            }
            return true;
        }, IntPtr.Zero);
        return bestArea >= 320 * 200 ? best : IntPtr.Zero;
    }

    /// <summary>Twice a second: notices the game closing, docks it when it comes up, and says what's going on.</summary>
    void Watch()
    {
        if (Docked && !Native.IsWindow(game))
        {
            context.Log("The game closed.");
            ForgetGame();
        }
        else if (Docked && Native.GetAncestor(game, Native.GA_PARENT) != host.Handle)
        {
            // The borderless mod takes the window back while the game changes its display mode
            // (video settings): docked, that crashed the game.
            context.Log("The game took its window back to change its display mode. It is docked again in a moment.");
            ForgetGame();
            startedAt = DateTime.Now;  // give it a few seconds to finish, as when it starts
        }
        IntPtr found = Docked ? game : FindGameWindow();
        if (!Docked && found != IntPtr.Zero && autoDockBox.IsChecked == true && host.Handle != IntPtr.Zero && View.IsVisible)
        {
            // Just started: give it a moment to make its window and pick its mode first.
            if ((DateTime.Now - startedAt).TotalSeconds > 3) Dock(found);
        }
        bool running = Docked || found != IntPtr.Zero;
        startButton.IsEnabled = !running;
        dockButton.IsEnabled = running;
        SetLabel(dockButton, Docked ? "Undock" : "Dock");
        backButton.IsEnabled = Docked;
        status.Foreground = (Brush)Application.Current.FindResource("DimTextBrush");
        status.Text = Docked
            ? (InGame() ? "Playing: Shift+F1 or Alt+Tab gives the mouse and keys back to the editor."
                        : "Click the game to play. It keeps running while you work in the editor.")
            : running
                ? "The game is running in its own window. Dock brings it into this panel (it has to be in Windowed mode)."
                : (DateTime.Now - startedAt).TotalSeconds < 60 ? "Starting the game ..." : "The game isn't running. Start it here, or with RFAModLauncher.";
    }

    /// <summary>Whether the game has the keyboard now (you clicked into it).</summary>
    bool InGame()
    {
        if (!Docked) return false;
        uint thread = Native.GetWindowThreadProcessId(game, out _);
        var info = new Native.GUITHREADINFO { cbSize = Marshal.SizeOf<Native.GUITHREADINFO>() };
        return Native.GetGUIThreadInfo(thread, ref info) && info.hwndFocus == game;
    }

    // ---- Docking ---------------------------------------------------------------------------------

    void OnDockOrUndock(object sender, RoutedEventArgs e)
    {
        if (Docked)
        {
            autoDockBox.IsChecked = false;  // you undocked it: don't take it straight back
            Undock();
            context.Log("Undocked the game: it has its own window again.");
        }
        else
        {
            IntPtr found = FindGameWindow();
            if (found == IntPtr.Zero) context.Log("The game isn't running (or hasn't made its window yet).");
            else Dock(found);
        }
        Watch();
    }

    void Dock(IntPtr window)
    {
        if (Docked || host.Handle == IntPtr.Zero) return;
        saved = Native.Save(window);
        if (Native.LooksExclusive(window, saved))
        {
            autoDockBox.IsChecked = false;
            context.Log("The game is in its exclusive Fullscreen mode, which can't be docked. Set Options > Display to Windowed, then Dock.");
            return;
        }
        Native.GetClientRect(window, out Native.RECT client);
        int width = client.Right - client.Left, height = client.Bottom - client.Top;

        // A child's style first, then its new parent (as Windows asks).
        Native.SetWindowLongPtr(window, Native.GWL_STYLE, (IntPtr)Native.ChildStyle(saved.Style));
        Native.SetWindowLongPtr(window, Native.GWL_EXSTYLE, (IntPtr)Native.ChildExStyle(saved.ExStyle));
        if (Native.SetParent(window, host.Handle) == IntPtr.Zero && Marshal.GetLastWin32Error() != 0)
        {
            int error = Marshal.GetLastWin32Error();
            Native.Restore(window, saved);
            context.Log($"Docking the game failed (Windows error {error}).");
            return;
        }
        game = window;
        TellGameController();
        ControllerChanged?.Invoke();
        Fit();
        Native.SetWindowPos(game, IntPtr.Zero, 0, 0, 0, 0,
            Native.SWP_NOMOVE | Native.SWP_NOSIZE | Native.SWP_NOZORDER | Native.SWP_FRAMECHANGED | Native.SWP_SHOWWINDOW | Native.SWP_ASYNCWINDOWPOS);
        hotKeyRegistered = Native.RegisterHotKey(host.Handle, HotKeyId, Native.MOD_SHIFT | Native.MOD_NOREPEAT, (uint)KeyInterop.VirtualKeyFromKey(Key.F1));
        context.Log($"Docked the game ({width}x{height}). Click it to play; Shift+F1 brings you back to the editor.");
    }

    void Undock()
    {
        if (!Docked) return;
        IntPtr window = game;
        if (Native.IsWindow(window)) Native.RemoveProp(window, ControllerProperty);
        ForgetGame();
        if (Native.IsWindow(window)) Native.Restore(window, saved);
    }

    void ForgetGame()
    {
        if (hotKeyRegistered) Native.UnregisterHotKey(host.Handle, HotKeyId);
        hotKeyRegistered = false;
        game = IntPtr.Zero;
        ControllerChanged?.Invoke();
    }

    /// <summary>Sizes the game to fill the panel, whatever the panel's shape.</summary>
    void Fit()
    {
        if (!Docked) return;
        Native.GetClientRect(host.Handle, out Native.RECT area);
        int width = area.Right - area.Left, height = area.Bottom - area.Top;
        if (width <= 0 || height <= 0) return;
        // Asynchronously: a game busy loading a level must never hold up the editor.
        Native.SetWindowPos(game, IntPtr.Zero, 0, 0, width, height, Native.SWP_NOZORDER | Native.SWP_NOACTIVATE | Native.SWP_ASYNCWINDOWPOS);
    }

    /// <summary>Takes the mouse and keys away from the game, back to the editor.</summary>
    void BackToEditor()
    {
        Native.ClipCursor(IntPtr.Zero);
        Window? window = Window.GetWindow(View);
        if (window != null)
        {
            IntPtr editor = new WindowInteropHelper(window).Handle;
            Native.SetFocus(editor);
            window.Activate();
            Keyboard.Focus(dockButton);
        }
        Watch();
    }
}

/// <summary>The editor's window the game is docked in: a plain black child window.</summary>
sealed class GameHost : HwndHost
{
    public event Action? Resized, HotKey, Destroying;

    protected override HandleRef BuildWindowCore(HandleRef parent)
    {
        // The game is per-monitor DPI aware (rfa_qol makes it so) and the editor may not be: Windows
        // only lets windows of different DPI awareness be parent and child if the parent says so.
        IntPtr before = Native.SetThreadDpiHostingBehavior(Native.DPI_HOSTING_BEHAVIOR_MIXED);
        IntPtr hwnd = Native.CreateWindowEx(0, "STATIC", "", Native.WS_CHILD | Native.WS_VISIBLE | Native.WS_CLIPCHILDREN | Native.SS_BLACKRECT,
            0, 0, 100, 100, parent.Handle, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
        Native.SetThreadDpiHostingBehavior(before);
        return new HandleRef(this, hwnd);
    }

    protected override void DestroyWindowCore(HandleRef hwnd)
    {
        Destroying?.Invoke();
        Native.DestroyWindow(hwnd.Handle);
    }

    protected override IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg == Native.WM_SIZE) Resized?.Invoke();
        else if (msg == Native.WM_HOTKEY)
        {
            HotKey?.Invoke();
            handled = true;
            return IntPtr.Zero;
        }
        return base.WndProc(hwnd, msg, wParam, lParam, ref handled);
    }
}

static class Native
{
    public const int GWL_STYLE = -16, GWL_EXSTYLE = -20;
    public const uint GW_OWNER = 4, GA_PARENT = 1;
    public const int WM_SIZE = 0x0005, WM_HOTKEY = 0x0312;
    public const uint MOD_SHIFT = 0x4, MOD_NOREPEAT = 0x4000;
    public const long WS_CHILD = 0x40000000, WS_POPUP = 0x80000000, WS_VISIBLE = 0x10000000, WS_CLIPCHILDREN = 0x02000000,
        WS_CAPTION = 0x00C00000, WS_THICKFRAME = 0x00040000, WS_SYSMENU = 0x00080000, WS_MINIMIZEBOX = 0x00020000,
        WS_MAXIMIZEBOX = 0x00010000, WS_MAXIMIZE = 0x01000000, WS_MINIMIZE = 0x20000000, SS_BLACKRECT = 0x4;
    public const long WS_EX_APPWINDOW = 0x40000, WS_EX_TOPMOST = 0x8, WS_EX_WINDOWEDGE = 0x100, WS_EX_CLIENTEDGE = 0x200,
        WS_EX_DLGMODALFRAME = 0x1, WS_EX_STATICEDGE = 0x20000;
    public const uint SWP_NOSIZE = 0x1, SWP_NOMOVE = 0x2, SWP_NOZORDER = 0x4, SWP_NOACTIVATE = 0x10, SWP_FRAMECHANGED = 0x20,
        SWP_SHOWWINDOW = 0x40, SWP_ASYNCWINDOWPOS = 0x4000;
    public static readonly IntPtr DPI_HOSTING_BEHAVIOR_MIXED = 1;

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct GUITHREADINFO
    {
        public int cbSize, flags;
        public IntPtr hwndActive, hwndFocus, hwndCapture, hwndMenuOwner, hwndMoveSize, hwndCaret;
        public RECT rcCaret;
    }

    /// <summary>How the game's window was before it was docked.</summary>
    public struct Saved
    {
        public long Style, ExStyle;
        public RECT Rect;
    }

    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr param);

    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr param);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr hwnd, uint cmd);
    [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SetPropW")] public static extern bool SetProp(IntPtr hwnd, string name, IntPtr value);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "RemovePropW")] public static extern IntPtr RemoveProp(IntPtr hwnd, string name);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll", SetLastError = true)] public static extern IntPtr SetParent(IntPtr child, IntPtr parent);
    [DllImport("user32.dll", SetLastError = true)] public static extern IntPtr GetWindowLongPtr(IntPtr hwnd, int index);
    [DllImport("user32.dll", SetLastError = true)] public static extern IntPtr SetWindowLongPtr(IntPtr hwnd, int index, IntPtr value);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool RegisterHotKey(IntPtr hwnd, int id, uint modifiers, uint vk);
    [DllImport("user32.dll")] public static extern bool UnregisterHotKey(IntPtr hwnd, int id);
    [DllImport("user32.dll")] public static extern bool ClipCursor(IntPtr rect);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint thread, ref GUITHREADINFO info);
    [DllImport("user32.dll")] public static extern bool DestroyWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiHostingBehavior(IntPtr value);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "CreateWindowExW")]
    static extern IntPtr CreateWindowExNative(int exStyle, string className, string title, int style, int x, int y, int width, int height,
                                             IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);

    public static IntPtr CreateWindowEx(int exStyle, string className, string title, long style, int x, int y, int width, int height,
                                        IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param) =>
        CreateWindowExNative(exStyle, className, title, unchecked((int)style), x, y, width, height, parent, menu, instance, param);

    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentThread();
    [DllImport("kernel32.dll")] static extern bool SetThreadPriority(IntPtr thread, int priority);

    /// <summary>
    /// Does long disk work at Windows' background priority (processor, disk and memory all give way
    /// to everything else), so it doesn't slow down what the user is doing or a game that is starting.
    /// </summary>
    public static T InBackground<T>(Func<T> work)
    {
        const int THREAD_MODE_BACKGROUND_BEGIN = 0x00010000, THREAD_MODE_BACKGROUND_END = 0x00020000;
        bool began = SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        try
        {
            return work();
        }
        finally
        {
            if (began) SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
        }
    }

    /// <summary>
    /// Does long work at the lowest processor priority but with the disk as usual: the game's index is read this
    /// way, since Windows' background mode (<see cref="InBackground"/>) also gives the disk away, and while the
    /// game loads it took minutes instead of one.
    /// </summary>
    public static T AtLowPriority<T>(Func<T> work)
    {
        const int THREAD_PRIORITY_LOWEST = -2, THREAD_PRIORITY_NORMAL = 0;
        bool lowered = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
        try
        {
            return work();
        }
        finally
        {
            if (lowered) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
        }
    }

    public static Saved Save(IntPtr window)
    {
        GetWindowRect(window, out RECT rect);
        return new Saved
        {
            Style = (long)GetWindowLongPtr(window, GWL_STYLE) & 0xFFFFFFFF,
            ExStyle = (long)GetWindowLongPtr(window, GWL_EXSTYLE) & 0xFFFFFFFF,
            Rect = rect,
        };
    }

    public static void Restore(IntPtr window, Saved saved)
    {
        SetWindowLongPtr(window, GWL_STYLE, (IntPtr)(saved.Style & ~WS_CHILD));
        SetParent(window, IntPtr.Zero);
        SetWindowLongPtr(window, GWL_STYLE, (IntPtr)saved.Style);
        SetWindowLongPtr(window, GWL_EXSTYLE, (IntPtr)saved.ExStyle);
        SetWindowPos(window, IntPtr.Zero, saved.Rect.Left, saved.Rect.Top, saved.Rect.Right - saved.Rect.Left, saved.Rect.Bottom - saved.Rect.Top,
            SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS);
    }

    public static long ChildStyle(long style) =>
        (style & ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_MAXIMIZE | WS_MINIMIZE))
        | WS_CHILD | WS_VISIBLE;

    public static long ChildExStyle(long ex) =>
        ex & ~(WS_EX_APPWINDOW | WS_EX_TOPMOST | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE);

    /// <summary>
    /// Whether the game looks like it is in its exclusive Fullscreen mode: a topmost frameless popup.
    /// (Windowed, it has a caption, or with the borderless mod a popup that isn't topmost.)
    /// </summary>
    public static bool LooksExclusive(IntPtr window, Saved saved) =>
        (saved.Style & WS_CAPTION) != WS_CAPTION && (saved.ExStyle & WS_EX_TOPMOST) != 0;
}
