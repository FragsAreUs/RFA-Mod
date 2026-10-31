using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace RfaEditor;

/// <summary>
/// Asks Windows to draw a window's title bar dark, to match the editor (Windows 10 version 20H1
/// and later, and Windows 11; older versions ignore it). It only changes how this one window is
/// drawn while it is open: nothing is stored and no Windows setting is changed.
/// </summary>
static class DarkTitleBar
{
    const int UseImmersiveDarkMode = 20;  // DWMWA_USE_IMMERSIVE_DARK_MODE

    [DllImport("dwmapi.dll")]
    static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);

    public static void Apply(Window window)
    {
        IntPtr handle = new WindowInteropHelper(window).Handle;
        int on = 1;
        DwmSetWindowAttribute(handle, UseImmersiveDarkMode, ref on, sizeof(int));
    }
}
