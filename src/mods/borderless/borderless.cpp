// Borderless fullscreen for Red Faction: Armageddon (DX11 exe).
//
// Set the game's own display mode to Windowed (Options > Display). This mod then strips that
// window's frame and stretches it over the whole monitor. While the game is in its own exclusive
// fullscreen mode, the mod leaves it alone.
//
// Everything here goes through the game's import table. DXGI/D3D COM vtables are deliberately not
// patched: the Steam overlay patches them too, and when the game rebuilt its swap chain the two
// hooks ended up calling each other until the stack overflowed.
//
// With IgnoreFocusLoss=1 the game is never told it was alt-tabbed away from: focus messages are
// hidden from it, its own low-level keyboard hook doesn't see Alt+Tab and friends, and this mod
// frees the cursor (no clipping, no recentring) and mutes input instead. With PauseOnAltTab=1 it
// also asks the game to open its pause menu, as if you had pressed the pause key.
//
// With [Borderless] Enabled=0 every hook stays installed but only logs.
#define RFA_MOD_NAME "borderless"
#include "rfa_mod.h"
#include "rfa_game.h"
#include <Xinput.h>

namespace {

// [Borderless] settings
bool g_enabled = true;
int g_monitorIndex = 0;
bool g_ignoreFocusLoss = true;
bool g_allowWindowsKey = true;
bool g_pauseOnAltTab = true;

// The game's "open the pause menu" request in the publicbeta exe (its own setter is 0x582760). The
// HUD opens the menu on its next update, the same way as the pause key, unless a cutscene or
// another menu is up (0x57B774).
BYTE* g_pauseMenuRequest = nullptr;  // only set on the publicbeta build
bool g_pauseRequested = false;       // we set the flag on the way out

HWND g_hwnd = nullptr;        // the game's window
bool g_gameWindowed = false;  // the game's own display mode is Windowed (the only mode we touch)
WNDPROC g_gameWndProc = nullptr;
HOOKPROC g_gameKeyboardHook = nullptr;

bool g_inBackground = false;  // the user is in another app right now
bool g_gameClipSet = false;   // the cursor clip the game last asked for, restored when the user returns
RECT g_gameClip{};

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

decltype(&CreateWindowExW) o_CreateWindowExW = CreateWindowExW;
decltype(&RegisterClassExW) o_RegisterClassExW = RegisterClassExW;
decltype(&SetWindowLongA) o_SetWindowLongA = SetWindowLongA;
decltype(&SetWindowLongW) o_SetWindowLongW = SetWindowLongW;
decltype(&SetWindowPos) o_SetWindowPos = SetWindowPos;
decltype(&ShowWindow) o_ShowWindow = ShowWindow;
decltype(&ClipCursor) o_ClipCursor = ClipCursor;
decltype(&SetCursorPos) o_SetCursorPos = SetCursorPos;
decltype(&GetForegroundWindow) o_GetForegroundWindow = GetForegroundWindow;
decltype(&IsIconic) o_IsIconic = IsIconic;
decltype(&GetAsyncKeyState) o_GetAsyncKeyState = GetAsyncKeyState;
decltype(&RegisterRawInputDevices) o_RegisterRawInputDevices = RegisterRawInputDevices;
decltype(&SetWindowsHookExA) o_SetWindowsHookExA = SetWindowsHookExA;
XInputGetStateFn o_XInputGetState;

bool Borderless() { return g_enabled && g_gameWindowed; }
bool HidingFocus() { return Borderless() && g_ignoreFocusLoss; }

// ---------------------------------------------------------------------------------------------
// Window

struct MonitorSearch {
    int wanted;
    int seen;
    HMONITOR found;
};

BOOL CALLBACK FindMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
    auto search = reinterpret_cast<MonitorSearch*>(param);
    if (++search->seen == search->wanted) {
        search->found = monitor;
        return FALSE;
    }
    return TRUE;
}

BOOL CALLBACK LogMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);
    const RECT& r = mi.rcMonitor;
    rfa::Log("  Monitor=%d: %ls %ldx%ld at %ld,%ld%s", ++*reinterpret_cast<int*>(param), mi.szDevice,
             r.right - r.left, r.bottom - r.top, r.left, r.top, (mi.dwFlags & MONITORINFOF_PRIMARY) ? " (primary)" : "");
    return TRUE;
}

// The rectangle the game window should cover.
RECT TargetRect() {
    HMONITOR monitor = nullptr;
    if (g_monitorIndex > 0) {
        MonitorSearch search{g_monitorIndex, 0, nullptr};
        EnumDisplayMonitors(nullptr, nullptr, FindMonitor, reinterpret_cast<LPARAM>(&search));
        monitor = search.found;
    }
    if (!monitor) monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);
    return mi.rcMonitor;
}

// The game gives its windowed mode a caption and its fullscreen mode a bare popup.
bool IsWindowedStyle(LONG style) { return (style & WS_CAPTION) == WS_CAPTION; }

void NoteGameMode(LONG style) {
    bool windowed = IsWindowedStyle(style);
    static bool known = false;
    if (known && windowed == g_gameWindowed) return;
    known = true;
    g_gameWindowed = windowed;
    if (windowed) rfa::Log("Game is in Windowed mode: making it borderless fullscreen");
    else rfa::Log("Game is in its own exclusive Fullscreen mode, so this mod stays out of the way. "
                  "For borderless, set Options > Display to Windowed.");
}

LONG BorderlessStyle(LONG style) {
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MAXIMIZE);
    // WS_MINIMIZEBOX keeps "click the taskbar button to minimize" working.
    return style | WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU;
}

LONG BorderlessExStyle(LONG ex) {
    return ex & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_TOPMOST);
}

const wchar_t* ClassName(LPCWSTR cls) {
    static wchar_t atom[16];
    if (!IS_INTRESOURCE(cls)) return cls;
    swprintf_s(atom, L"#%u", static_cast<unsigned>(reinterpret_cast<UINT_PTR>(cls)));
    return atom;
}

// Pauses the game when you switch away mid-play. A request the game didn't act on (cutscene,
// menu already open) is withdrawn when you come back so the menu doesn't pop up later.
void RequestPauseMenu(bool away) {
    if (!g_pauseMenuRequest) return;
    if (away && g_pauseOnAltTab) {
        *g_pauseMenuRequest = 1;
        g_pauseRequested = true;
        rfa::Log("Asked the game to open its pause menu");
    } else if (!away && g_pauseRequested) {
        if (*g_pauseMenuRequest) {
            *g_pauseMenuRequest = 0;
            rfa::Log("The game didn't need to pause (menu or cutscene); request withdrawn");
        }
        g_pauseRequested = false;
    }
}

void OnFocusChanged(bool active) {
    if (g_inBackground == !active) return;
    g_inBackground = !active;
    if (!HidingFocus()) return;
    rfa::Log("You %s the game", active ? "returned to" : "switched away from");
    if (!active) o_ClipCursor(nullptr);  // never leave the cursor trapped while you're in another app
    else if (g_gameClipSet) o_ClipCursor(&g_gameClip);
    RequestPauseMenu(!active);
}

// Sits in front of the game's window procedure. Logs focus/size events and, with
// IgnoreFocusLoss, swallows the ones that would tell the game it lost focus.
LRESULT CALLBACK WndProcHook(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    bool hide = false;
    switch (msg) {
    case WM_ACTIVATEAPP:
        RFA_LOG_FIRST(100, "WM_ACTIVATEAPP %s", wp ? "gained" : "lost");
        OnFocusChanged(wp != FALSE);
        hide = HidingFocus() && !wp;
        break;
    case WM_ACTIVATE:
        RFA_LOG_FIRST(100, "WM_ACTIVATE %u", LOWORD(wp));
        hide = HidingFocus() && LOWORD(wp) == WA_INACTIVE;
        break;
    case WM_KILLFOCUS:
        hide = HidingFocus();
        break;
    case WM_SIZE:
        RFA_LOG_FIRST(100, "WM_SIZE type %u, %ux%u", static_cast<unsigned>(wp), LOWORD(lp), HIWORD(lp));
        hide = HidingFocus() && wp == SIZE_MINIMIZED;
        break;
    case WM_DISPLAYCHANGE: RFA_LOG_FIRST(20, "WM_DISPLAYCHANGE %ux%u", LOWORD(lp), HIWORD(lp)); break;
    case WM_SYSCOMMAND:
        RFA_LOG_FIRST(50, "WM_SYSCOMMAND %X", static_cast<unsigned>(wp & 0xFFF0));
        hide = HidingFocus() && (wp & 0xFFF0) == SC_MONITORPOWER;  // the game suspends on monitor off
        break;
    }
    if (hide && hwnd == g_hwnd) return DefWindowProcW(hwnd, msg, wp, lp);
    return g_gameWndProc(hwnd, msg, wp, lp);
}

ATOM WINAPI Hk_RegisterClassExW(const WNDCLASSEXW* wc) {
    if (!wc || g_gameWndProc) return o_RegisterClassExW(wc);
    rfa::Log("RegisterClassExW class=%ls", ClassName(wc->lpszClassName));
    WNDCLASSEXW copy = *wc;
    g_gameWndProc = copy.lpfnWndProc;
    copy.lpfnWndProc = WndProcHook;
    return o_RegisterClassExW(&copy);
}

HWND WINAPI Hk_CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h,
                               HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    bool gameWindow = !g_hwnd && !parent && !(style & WS_CHILD);
    rfa::Log("CreateWindowExW class=%ls title=\"%ls\" style=%08lX ex=%08lX %dx%d at %d,%d", ClassName(cls),
             title ? title : L"", style, ex, w, h, x, y);
    if (gameWindow) NoteGameMode(static_cast<LONG>(style));
    if (gameWindow && Borderless()) {
        RECT r = TargetRect();
        style = BorderlessStyle(style);
        ex = BorderlessExStyle(ex);
        x = r.left;
        y = r.top;
        w = r.right - r.left;
        h = r.bottom - r.top;
        rfa::Log("  -> borderless %dx%d at %d,%d", w, h, x, y);
    }
    HWND hwnd = o_CreateWindowExW(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
    if (gameWindow && hwnd) g_hwnd = hwnd;
    return hwnd;
}

// The game switches modes by setting a new style (ApplyDisplayMode), then the extended
// style, then the position, so the style tells us which mode it is heading into.
LONG FilterWindowLong(HWND hwnd, int index, LONG value) {
    if (hwnd != g_hwnd || (index != GWL_STYLE && index != GWL_EXSTYLE)) return value;
    if (index == GWL_STYLE) NoteGameMode(value);
    LONG filtered = value;
    if (Borderless()) filtered = index == GWL_STYLE ? BorderlessStyle(value) : BorderlessExStyle(value);
    RFA_LOG_FIRST(20, "SetWindowLong(%s, %08lX) -> %08lX", index == GWL_STYLE ? "STYLE" : "EXSTYLE", value, filtered);
    return filtered;
}

LONG WINAPI Hk_SetWindowLongA(HWND hwnd, int index, LONG value) {
    return o_SetWindowLongA(hwnd, index, FilterWindowLong(hwnd, index, value));
}

LONG WINAPI Hk_SetWindowLongW(HWND hwnd, int index, LONG value) {
    return o_SetWindowLongW(hwnd, index, FilterWindowLong(hwnd, index, value));
}

BOOL WINAPI Hk_SetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags) {
    if (hwnd == g_hwnd) {
        RFA_LOG_FIRST(20, "SetWindowPos(%dx%d at %d,%d, flags %X, after %p)", cx, cy, x, y, flags, after);
        if (Borderless() && !IsIconic(hwnd)) {
            RECT r = TargetRect();
            x = r.left;
            y = r.top;
            cx = r.right - r.left;
            cy = r.bottom - r.top;
            flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
            if (after == HWND_TOPMOST) after = HWND_NOTOPMOST;  // stay alt-tab friendly
        }
    }
    return o_SetWindowPos(hwnd, after, x, y, cx, cy, flags);
}

BOOL WINAPI Hk_ShowWindow(HWND hwnd, int cmd) {
    if (hwnd == g_hwnd) {
        RFA_LOG_FIRST(20, "ShowWindow(%d)", cmd);
        if (Borderless() && cmd == SW_SHOWMAXIMIZED) cmd = SW_SHOW;
    }
    return o_ShowWindow(hwnd, cmd);
}

BOOL WINAPI Hk_ClipCursor(const RECT* rect) {
    if (rect) RFA_LOG_FIRST(10, "ClipCursor(%ld,%ld - %ld,%ld)", rect->left, rect->top, rect->right, rect->bottom);
    else RFA_LOG_FIRST(10, "ClipCursor(NULL)");
    g_gameClipSet = rect != nullptr;
    if (rect) g_gameClip = *rect;
    if (HidingFocus() && g_inBackground) return TRUE;  // applied once you are back in the game
    return o_ClipCursor(rect);
}

// For mouse-look the game snaps the cursor back to the middle of the screen every frame. It would
// keep doing that while you're in another app (it isn't told you left), so those calls are dropped.
BOOL WINAPI Hk_SetCursorPos(int x, int y) {
    if (HidingFocus() && g_inBackground) {
        RFA_LOG_FIRST(3, "Ignored the game moving the cursor to %d,%d while you're in another app", x, y);
        return TRUE;
    }
    return o_SetCursorPos(x, y);
}

// ---------------------------------------------------------------------------------------------
// Focus and input

// The game may poll these to notice it was alt-tabbed away from or minimized.
HWND WINAPI Hk_GetForegroundWindow() {
    static bool wasGame = false;
    HWND fg = o_GetForegroundWindow();
    bool isGame = g_hwnd && fg == g_hwnd;
    if (isGame != wasGame) {
        wasGame = isGame;
        RFA_LOG_FIRST(50, "GetForegroundWindow: %s (%p)", isGame ? "the game" : "another window", fg);
    }
    return HidingFocus() && g_hwnd ? g_hwnd : fg;
}

BOOL WINAPI Hk_IsIconic(HWND hwnd) {
    return HidingFocus() && hwnd == g_hwnd ? FALSE : o_IsIconic(hwnd);
}

// Input the game would otherwise still receive while you are in another app.
SHORT WINAPI Hk_GetAsyncKeyState(int key) {
    return HidingFocus() && g_inBackground ? 0 : o_GetAsyncKeyState(key);
}

DWORD WINAPI Hk_XInputGetState(DWORD user, XINPUT_STATE* state) {
    DWORD result = o_XInputGetState(user, state);
    if (result == ERROR_SUCCESS && state && HidingFocus() && g_inBackground) ZeroMemory(&state->Gamepad, sizeof(state->Gamepad));
    return result;
}

// The game's WH_KEYBOARD_LL hook sees every key on the system:
// - it watches for Alt+Tab, Alt+Esc, Ctrl+Esc and Alt+Del and suspends as soon as it sees one,
//   before Windows has even switched. Those keys still reach Windows; the game never sees them.
// - it swallows the Windows keys, even while you're in another app (no Start menu, no Win+Shift+S).
//   They go straight to Windows while you're away, and in borderless with AllowWindowsKey=1.
LRESULT CALLBACK KeyboardHookFilter(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION) return g_gameKeyboardHook(code, wp, lp);
    auto key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
    DWORD vk = key->vkCode;
    if ((vk == VK_LWIN || vk == VK_RWIN) && g_enabled && (g_inBackground || (Borderless() && g_allowWindowsKey))) {
        RFA_LOG_FIRST(5, "Let the Windows key through to Windows");
        return CallNextHookEx(nullptr, code, wp, lp);
    }
    if (HidingFocus()) {
        bool alt = (key->flags & LLKHF_ALTDOWN) != 0;
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        if ((alt && (vk == VK_TAB || vk == VK_ESCAPE || vk == VK_DELETE)) || (ctrl && vk == VK_ESCAPE)) {
            RFA_LOG_FIRST(20, "Kept %s+vk %02lX away from the game's keyboard hook", alt ? "Alt" : "Ctrl", vk);
            return CallNextHookEx(nullptr, code, wp, lp);
        }
    }
    return g_gameKeyboardHook(code, wp, lp);
}

HHOOK WINAPI Hk_SetWindowsHookExA(int id, HOOKPROC proc, HINSTANCE module, DWORD thread) {
    rfa::Log("SetWindowsHookExA(%d, %p)", id, proc);
    if (g_enabled && id == WH_KEYBOARD_LL && proc) {
        g_gameKeyboardHook = proc;
        proc = KeyboardHookFilter;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&g_gameKeyboardHook), &module);
    }
    return o_SetWindowsHookExA(id, proc, module, thread);
}

BOOL WINAPI Hk_RegisterRawInputDevices(PCRAWINPUTDEVICE devices, UINT count, UINT size) {
    RAWINPUTDEVICE copy[16];
    if (!devices || count > 16 || size != sizeof(RAWINPUTDEVICE)) return o_RegisterRawInputDevices(devices, count, size);
    for (UINT i = 0; i < count; ++i) {
        copy[i] = devices[i];
        RFA_LOG_FIRST(20, "RegisterRawInputDevices usage %X/%X flags %lX", copy[i].usUsagePage, copy[i].usUsage,
                      copy[i].dwFlags);
        // Foreground only, so the mouse doesn't drive the game while you're in another app.
        if (g_enabled && g_ignoreFocusLoss) copy[i].dwFlags &= ~(RIDEV_INPUTSINK | RIDEV_EXINPUTSINK);
    }
    return o_RegisterRawInputDevices(copy, count, size);
}

// Without this, Windows bitmap-stretches the game on displays with scaling above 100%.
void MakeDpiAware() {
    using SetContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto set = reinterpret_cast<SetContextFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                             "SetProcessDpiAwarenessContext"));
    if (set && set(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) rfa::Log("Process is now per-monitor DPI aware");
    else rfa::Log("Could not change DPI awareness (error %lu); continuing", GetLastError());
}

}  // namespace

extern "C" __declspec(dllexport) void __cdecl RFAMod_Init() {
    g_enabled = rfa::IniInt(L"Borderless", L"Enabled", 1) != 0;
    g_monitorIndex = rfa::IniInt(L"Borderless", L"Monitor", 0);
    g_ignoreFocusLoss = rfa::IniInt(L"Borderless", L"IgnoreFocusLoss", 1) != 0;
    g_allowWindowsKey = rfa::IniInt(L"Borderless", L"AllowWindowsKey", 1) != 0;
    g_pauseOnAltTab = rfa::IniInt(L"Borderless", L"PauseOnAltTab", 1) != 0;
    if (rfa::game::IsSupportedBuild())
        g_pauseMenuRequest = reinterpret_cast<BYTE*>(0x135B3F1);
    else
        rfa::Log("Not the publicbeta build; Pause on Alt-Tab is off");
    if (!g_enabled) rfa::Log("Enabled=0: logging only, the game runs unmodified");
    if (g_enabled && rfa::IniInt(L"Borderless", L"DpiAware", 1)) MakeDpiAware();

    int count = 0;
    rfa::Log("Monitors (set [Borderless] Monitor= to pick one, 0 = primary):");
    EnumDisplayMonitors(nullptr, nullptr, LogMonitor, reinterpret_cast<LPARAM>(&count));
    RECT r = TargetRect();
    rfa::Log("In Windowed mode the game will cover %ldx%ld at %ld,%ld", r.right - r.left, r.bottom - r.top, r.left,
             r.top);

    HMODULE game = GetModuleHandleW(nullptr);
    struct {
        const char* dll;
        const char* func;
        bool ok;
    } hooks[] = {
        {"USER32.dll", "RegisterClassExW", rfa::HookImport(game, "USER32.dll", "RegisterClassExW", &Hk_RegisterClassExW, &o_RegisterClassExW)},
        {"USER32.dll", "CreateWindowExW", rfa::HookImport(game, "USER32.dll", "CreateWindowExW", &Hk_CreateWindowExW, &o_CreateWindowExW)},
        {"USER32.dll", "SetWindowLongA", rfa::HookImport(game, "USER32.dll", "SetWindowLongA", &Hk_SetWindowLongA, &o_SetWindowLongA)},
        {"USER32.dll", "SetWindowLongW", rfa::HookImport(game, "USER32.dll", "SetWindowLongW", &Hk_SetWindowLongW, &o_SetWindowLongW)},
        {"USER32.dll", "SetWindowPos", rfa::HookImport(game, "USER32.dll", "SetWindowPos", &Hk_SetWindowPos, &o_SetWindowPos)},
        {"USER32.dll", "ShowWindow", rfa::HookImport(game, "USER32.dll", "ShowWindow", &Hk_ShowWindow, &o_ShowWindow)},
        {"USER32.dll", "ClipCursor", rfa::HookImport(game, "USER32.dll", "ClipCursor", &Hk_ClipCursor, &o_ClipCursor)},
        {"USER32.dll", "SetCursorPos", rfa::HookImport(game, "USER32.dll", "SetCursorPos", &Hk_SetCursorPos, &o_SetCursorPos)},
        {"USER32.dll", "GetForegroundWindow", rfa::HookImport(game, "USER32.dll", "GetForegroundWindow", &Hk_GetForegroundWindow, &o_GetForegroundWindow)},
        {"USER32.dll", "IsIconic", rfa::HookImport(game, "USER32.dll", "IsIconic", &Hk_IsIconic, &o_IsIconic)},
        {"USER32.dll", "GetAsyncKeyState", rfa::HookImport(game, "USER32.dll", "GetAsyncKeyState", &Hk_GetAsyncKeyState, &o_GetAsyncKeyState)},
        {"USER32.dll", "RegisterRawInputDevices", rfa::HookImport(game, "USER32.dll", "RegisterRawInputDevices", &Hk_RegisterRawInputDevices, &o_RegisterRawInputDevices)},
        {"USER32.dll", "SetWindowsHookExA", rfa::HookImport(game, "USER32.dll", "SetWindowsHookExA", &Hk_SetWindowsHookExA, &o_SetWindowsHookExA)},
        {"XINPUT1_3.dll", "#2 (XInputGetState)", rfa::HookImport(game, "XINPUT1_3.dll", "#2", &Hk_XInputGetState, &o_XInputGetState)},
    };
    for (const auto& h : hooks) rfa::Log("Hook %s!%s: %s", h.dll, h.func, h.ok ? "ok" : "FAILED");
}
