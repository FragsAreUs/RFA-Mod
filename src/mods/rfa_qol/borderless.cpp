// Borderless fullscreen for Red Faction: Armageddon (DX11 exe). A part of rfa_qol.dll.
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
// frees the cursor (no clipping, no recentring) and mutes input instead. It also asks the game to
// open its pause menu, as if you had pressed the pause key: switching away always pauses the game,
// as it does when the game sees the switch itself.
//
// The editor's Game tab can dock the game's window inside the editor (it makes the window a child of
// one of its own). While docked, the editor decides where the window is and how big: the game's own
// moves leave it where it is (to change its display mode it takes its window back; the editor docks
// it again after). Clicking into it gives the game the mouse and keys;
// clicking or switching away (Shift+F1 in the editor) frees the cursor and mutes the game's input, and
// the game keeps running without pausing, so you can watch your edits in it.
//
// With [Borderless] Enabled=0 every hook stays installed but only logs.
#define RFA_MOD_NAME "borderless"
#include "rfa_mod.h"
#include "rfa_game.h"
#include "rfa_qol.h"
#include <Xinput.h>

namespace {

// [Borderless] settings
bool g_enabled = true;
int g_monitorIndex = 0;
bool g_ignoreFocusLoss = true;
bool g_allowWindowsKey = true;

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
decltype(&ShowCursor) o_ShowCursor = ShowCursor;
decltype(&SetCapture) o_SetCapture = SetCapture;
decltype(&ReleaseCapture) o_ReleaseCapture = ReleaseCapture;
decltype(&SetFocus) o_SetFocus = SetFocus;
decltype(&SetForegroundWindow) o_SetForegroundWindow = SetForegroundWindow;
decltype(&SetActiveWindow) o_SetActiveWindow = SetActiveWindow;
decltype(&GetForegroundWindow) o_GetForegroundWindow = GetForegroundWindow;
decltype(&IsIconic) o_IsIconic = IsIconic;
decltype(&GetAsyncKeyState) o_GetAsyncKeyState = GetAsyncKeyState;
decltype(&RegisterRawInputDevices) o_RegisterRawInputDevices = RegisterRawInputDevices;
decltype(&SetWindowsHookExA) o_SetWindowsHookExA = SetWindowsHookExA;
XInputGetStateFn o_XInputGetState;

// The editor has docked the game's window inside its own (see the top).
bool Docked() { return g_hwnd && (GetWindowLongW(g_hwnd, GWL_STYLE) & WS_CHILD) != 0; }
bool Borderless() { return g_enabled && g_gameWindowed && !Docked(); }
bool HidingFocus() { return Borderless() && g_ignoreFocusLoss; }
// Input is muted while the game is in the background, if focus loss is hidden from it or it is docked.
bool Muting() { return g_inBackground && (HidingFocus() || Docked()); }

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
    rfa::LogDetail("  Monitor=%d: %ls %ldx%ld at %ld,%ld%s", ++*reinterpret_cast<int*>(param), mi.szDevice,
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
    if (windowed) rfa::LogDetail("Game is in Windowed mode: making it borderless fullscreen");
    else rfa::LogInfo("Game is in its own exclusive Fullscreen mode, so this mod stays out of the way. "
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
    if (away) {
        *g_pauseMenuRequest = 1;
        g_pauseRequested = true;
        rfa::LogDetail("Asked the game to open its pause menu");
    } else if (!away && g_pauseRequested) {
        if (*g_pauseMenuRequest) {
            *g_pauseMenuRequest = 0;
            rfa::LogDetail("The game didn't need to pause (menu or cutscene); request withdrawn");
        }
        g_pauseRequested = false;
    }
}

// Docked, the game's window and the editor's share one mouse pointer (Windows joins the input of a
// child window's thread to its parent's), so the pointer the game hides is hidden over the editor
// too. While the pointer belongs to the editor (you are working there, or it has left the game's
// window), it is shown: raised as often as it takes, lowered again when it goes back to the game.
// What the game asks for meanwhile (ShowCursor) is kept and done then. On the game's thread only.
int g_cursorRaised = 0;    // how often we raised the show count
int g_cursorDeferred = 0;  // the game's own ShowCursor calls while raised: shows minus hides
bool g_mouseInside = false;

void GivePointerToEditor(bool editor) {
    if (editor && g_cursorRaised == 0) {
        int count;
        do {
            count = o_ShowCursor(TRUE);
            ++g_cursorRaised;
        } while (count < 0 && g_cursorRaised < 64);
    } else if (!editor && g_cursorRaised > 0) {
        for (; g_cursorRaised > 0; --g_cursorRaised) o_ShowCursor(FALSE);
        for (; g_cursorDeferred > 0; --g_cursorDeferred) o_ShowCursor(TRUE);
        for (; g_cursorDeferred < 0; ++g_cursorDeferred) o_ShowCursor(FALSE);
    }
}

int WINAPI Hk_ShowCursor(BOOL show) {
    if (g_cursorRaised == 0) return o_ShowCursor(show);
    g_cursorDeferred += show ? 1 : -1;
    return show ? 0 : -1;
}

// Docked, the game must not take the mouse while you work in the editor. Every frame the game holds
// the mouse for its window (SetCapture, in 0x4E4070: with a controller in use, or in windowed play),
// and docked that takes it from the editor too, as they share their input: no click reached the
// editor any more. So while the pointer is the editor's the game's capture is only noted, and given
// when you click back into the game; and the game can't make its window the active or focused one
// itself (only your click does).
bool g_gameCapture = false;  // the game holds the mouse, as far as it knows

HWND WINAPI Hk_SetCapture(HWND hwnd) {
    if (hwnd != g_hwnd) return o_SetCapture(hwnd);
    g_gameCapture = true;
    if (Docked() && g_inBackground) return nullptr;
    return o_SetCapture(hwnd);
}

BOOL WINAPI Hk_ReleaseCapture() {
    g_gameCapture = false;
    if (Docked() && g_inBackground) return TRUE;  // it has none to give up (the editor may be dragging)
    return o_ReleaseCapture();
}

HWND WINAPI Hk_SetFocus(HWND hwnd) { return Docked() ? GetFocus() : o_SetFocus(hwnd); }
BOOL WINAPI Hk_SetForegroundWindow(HWND hwnd) { return Docked() ? TRUE : o_SetForegroundWindow(hwnd); }
HWND WINAPI Hk_SetActiveWindow(HWND hwnd) { return Docked() ? GetActiveWindow() : o_SetActiveWindow(hwnd); }

void OnFocusChanged(bool active) {
    if (g_inBackground == !active) return;
    g_inBackground = !active;
    bool docked = Docked();
    if (!HidingFocus() && !docked) return;
    rfa::LogDetail("You %s the game%s", active ? "returned to" : "switched away from", docked ? " (docked in the editor)" : "");
    if (!active) o_ClipCursor(nullptr);  // never leave the cursor trapped while you're in another app
    else if (g_gameClipSet) o_ClipCursor(&g_gameClip);
    if (!docked) RequestPauseMenu(!active);  // docked, it keeps running while you edit
    else {
        GivePointerToEditor(!active || !g_mouseInside);
        if (!active) {
            if (GetCapture() == g_hwnd) o_ReleaseCapture();
        } else if (g_gameCapture) {
            o_SetCapture(g_hwnd);
        }
    }
}

// Sits in front of the game's window procedure. Logs focus/size events and, with
// IgnoreFocusLoss, swallows the ones that would tell the game it lost focus.
LRESULT CALLBACK WndProcHook(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    bool hide = false;
    if (hwnd == g_hwnd) {
        // Docked or undocked just now (the editor does it, from its own process).
        static bool wasDocked = false;
        bool docked = Docked();
        if (docked != wasDocked) {
            wasDocked = docked;
            rfa::LogDetail(docked ? "The editor docked the game's window" : "The game's window is its own again");
            if (docked) {
                bool focused = GetFocus() == hwnd;
                if (!focused && GetCapture() == hwnd) o_ReleaseCapture();
                g_mouseInside = false;
                g_inBackground = !focused;
                if (!focused) o_ClipCursor(nullptr);
                GivePointerToEditor(true);  // until the mouse is over the game and you have clicked into it
            } else {
                GivePointerToEditor(false);
            }
        }
    }
    switch (msg) {
    case WM_ACTIVATEAPP:
        RFA_LOG_DETAIL_FIRST(100, "WM_ACTIVATEAPP %s", wp ? "gained" : "lost");
        OnFocusChanged(wp != FALSE);
        hide = (HidingFocus() || Docked()) && !wp;
        break;
    case WM_ACTIVATE:
        RFA_LOG_DETAIL_FIRST(100, "WM_ACTIVATE %u", LOWORD(wp));
        // Docked, the game's window is never activated again (the editor's is), so it must not be
        // told it was deactivated either: it would stop reading the controller for good.
        hide = (HidingFocus() || Docked()) && LOWORD(wp) == WA_INACTIVE;
        break;
    case WM_SETFOCUS:
        if (Docked()) OnFocusChanged(true);
        break;
    case WM_KILLFOCUS:
        // Docked, the window gets no activation messages of its own: focus is what says you left.
        if (Docked()) OnFocusChanged(false);
        hide = HidingFocus() || Docked();
        break;
    case WM_MOUSEMOVE:
        if (Docked() && !g_mouseInside) {
            g_mouseInside = true;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);  // tells us once, when it leaves (WM_MOUSELEAVE)
            if (!g_inBackground) GivePointerToEditor(false);
        }
        break;
    case WM_MOUSELEAVE:
        g_mouseInside = false;
        if (Docked()) GivePointerToEditor(true);
        break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        // Docked, a click into the game doesn't give it the keyboard by itself (the editor's window is
        // the one activated). The click that takes you back into the game is only that, not a shot.
        if (Docked() && GetFocus() != hwnd) {
            SetFocus(hwnd);
            return 0;
        }
        break;
    case WM_INPUT:
        // Docked, the mouse and keys would still reach the game while you work in the editor.
        if (Docked() && g_inBackground) return DefWindowProcW(hwnd, msg, wp, lp);
        break;
    case WM_SIZE:
        RFA_LOG_DETAIL_FIRST(100, "WM_SIZE type %u, %ux%u", static_cast<unsigned>(wp), LOWORD(lp), HIWORD(lp));
        hide = HidingFocus() && wp == SIZE_MINIMIZED;
        break;
    case WM_DISPLAYCHANGE: RFA_LOG_DETAIL_FIRST(20, "WM_DISPLAYCHANGE %ux%u", LOWORD(lp), HIWORD(lp)); break;
    case WM_SYSCOMMAND:
        RFA_LOG_DETAIL_FIRST(50, "WM_SYSCOMMAND %X", static_cast<unsigned>(wp & 0xFFF0));
        hide = HidingFocus() && (wp & 0xFFF0) == SC_MONITORPOWER;  // the game suspends on monitor off
        break;
    }
    if (hide && hwnd == g_hwnd) return DefWindowProcW(hwnd, msg, wp, lp);
    return g_gameWndProc(hwnd, msg, wp, lp);
}

ATOM WINAPI Hk_RegisterClassExW(const WNDCLASSEXW* wc) {
    if (!wc || g_gameWndProc) return o_RegisterClassExW(wc);
    rfa::LogDetail("RegisterClassExW class=%ls", ClassName(wc->lpszClassName));
    WNDCLASSEXW copy = *wc;
    g_gameWndProc = copy.lpfnWndProc;
    copy.lpfnWndProc = WndProcHook;
    return o_RegisterClassExW(&copy);
}

HWND WINAPI Hk_CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h,
                               HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    bool gameWindow = !g_hwnd && !parent && !(style & WS_CHILD);
    rfa::LogDetail("CreateWindowExW class=%ls title=\"%ls\" style=%08lX ex=%08lX %dx%d at %d,%d", ClassName(cls),
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
        rfa::LogDetail("  -> borderless %dx%d at %d,%d", w, h, x, y);
    }
    HWND hwnd = o_CreateWindowExW(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
    if (gameWindow && hwnd) g_hwnd = hwnd;
    return hwnd;
}

// The game switches modes by setting a new style (ApplyDisplayMode), then the extended
// style, then the position, so the style tells us which mode it is heading into.
// The game changing its display mode (video settings) while docked crashed it: it sets the graphics
// device to the new mode first and its window after, and died on the next frame. So before either,
// the game gets its own window back and changes mode as it always does; the editor sees the window
// gone from its panel and docks it again a few seconds later.
void TakeWindowBack() {
    rfa::LogDetail("The game is changing its display mode: taking its window out of the editor for that");
    LONG style = GetWindowLongW(g_hwnd, GWL_STYLE);
    o_SetWindowLongW(g_hwnd, GWL_STYLE, (style & ~WS_CHILD) | WS_POPUP);
    SetParent(g_hwnd, nullptr);
}

// The game's "apply this display mode" (publicbeta exe, 0x76B310; cdecl: the mode's number and the
// video settings; true if the device took the mode). Its first 9 bytes are push ebp / mov ebp,esp /
// sub esp,0xBC.
constexpr DWORD kApplyDisplayMode = 0x76B310;
constexpr size_t kApplyDisplayModePrologue = 9;
using ApplyDisplayModeFn = bool(__cdecl*)(int, void*);
ApplyDisplayModeFn o_ApplyDisplayMode = nullptr;

bool __cdecl Hk_ApplyDisplayMode(int mode, void* settings) {
    if (Docked()) TakeWindowBack();
    return o_ApplyDisplayMode(mode, settings);
}

LONG FilterWindowLong(HWND hwnd, int index, LONG value) {
    if (hwnd != g_hwnd || (index != GWL_STYLE && index != GWL_EXSTYLE)) return value;
    if (Docked()) TakeWindowBack();  // (a build without the hook below gets here first)
    // The game builds the new style from the one the window has, which while docked says "child".
    if (index == GWL_STYLE) value &= ~static_cast<LONG>(WS_CHILD);
    if (index == GWL_STYLE) NoteGameMode(value);
    LONG filtered = value;
    if (Borderless()) filtered = index == GWL_STYLE ? BorderlessStyle(value) : BorderlessExStyle(value);
    RFA_LOG_DETAIL_FIRST(20, "SetWindowLong(%s, %08lX) -> %08lX", index == GWL_STYLE ? "STYLE" : "EXSTYLE", value, filtered);
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
        RFA_LOG_DETAIL_FIRST(20, "SetWindowPos(%dx%d at %d,%d, flags %X, after %p)", cx, cy, x, y, flags, after);
        if (Docked()) {
            // Where it sits and how big is the editor's to say.
            flags |= SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER;
            flags &= ~SWP_FRAMECHANGED;
        } else if (Borderless() && !IsIconic(hwnd)) {
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
        RFA_LOG_DETAIL_FIRST(20, "ShowWindow(%d)", cmd);
        if ((Borderless() || Docked()) && cmd == SW_SHOWMAXIMIZED) cmd = SW_SHOW;
        if (Docked() && (cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED || cmd == SW_SHOWMINNOACTIVE)) return TRUE;
    }
    return o_ShowWindow(hwnd, cmd);
}

BOOL WINAPI Hk_ClipCursor(const RECT* rect) {
    if (rect) RFA_LOG_DETAIL_FIRST(10, "ClipCursor(%ld,%ld - %ld,%ld)", rect->left, rect->top, rect->right, rect->bottom);
    else RFA_LOG_DETAIL_FIRST(10, "ClipCursor(NULL)");
    g_gameClipSet = rect != nullptr;
    if (rect) g_gameClip = *rect;
    if (Muting()) return TRUE;  // applied once you are back in the game
    return o_ClipCursor(rect);
}

// For mouse-look the game snaps the cursor back to the middle of the screen every frame. It would
// keep doing that while you're in another app (it isn't told you left), so those calls are dropped.
BOOL WINAPI Hk_SetCursorPos(int x, int y) {
    if (Muting()) {
        RFA_LOG_DETAIL_FIRST(3, "Ignored the game moving the cursor to %d,%d while you're in another app", x, y);
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
        RFA_LOG_DETAIL_FIRST(50, "GetForegroundWindow: %s (%p)", isGame ? "the game" : "another window", fg);
    }
    // Docked, the foreground window is the editor's: the game is told it is itself, so it doesn't suspend.
    return (HidingFocus() || Docked()) && g_hwnd ? g_hwnd : fg;
}

BOOL WINAPI Hk_IsIconic(HWND hwnd) {
    return (HidingFocus() || Docked()) && hwnd == g_hwnd ? FALSE : o_IsIconic(hwnd);
}

// Input the game would otherwise still receive while you are in another app.
SHORT WINAPI Hk_GetAsyncKeyState(int key) {
    return Muting() ? 0 : o_GetAsyncKeyState(key);
}

// Docked, the editor says whose the controller is (its level view reads the same one): the window
// property "RFAModController" it sets on the game's window is 1 for the game's, wherever the
// keyboard is, and 2 for the editor's. Without it the controller goes with the keyboard.
DWORD WINAPI Hk_XInputGetState(DWORD user, XINPUT_STATE* state) {
    DWORD result = o_XInputGetState(user, state);
    if (result != ERROR_SUCCESS || !state) return result;
    bool mute = Muting();
    if (Docked()) {
        const auto owner = reinterpret_cast<INT_PTR>(GetPropW(g_hwnd, L"RFAModController"));
        if (owner == 1) mute = false;
        else if (owner == 2) mute = true;
    }
    if (mute) ZeroMemory(&state->Gamepad, sizeof(state->Gamepad));
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
        RFA_LOG_DETAIL_FIRST(5, "Let the Windows key through to Windows");
        return CallNextHookEx(nullptr, code, wp, lp);
    }
    if (Docked() && g_inBackground) return CallNextHookEx(nullptr, code, wp, lp);  // you're typing in the editor
    if (HidingFocus() || Docked()) {
        bool alt = (key->flags & LLKHF_ALTDOWN) != 0;
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        if ((alt && (vk == VK_TAB || vk == VK_ESCAPE || vk == VK_DELETE)) || (ctrl && vk == VK_ESCAPE)) {
            RFA_LOG_DETAIL_FIRST(20, "Kept %s+vk %02lX away from the game's keyboard hook", alt ? "Alt" : "Ctrl", vk);
            return CallNextHookEx(nullptr, code, wp, lp);
        }
    }
    return g_gameKeyboardHook(code, wp, lp);
}

HHOOK WINAPI Hk_SetWindowsHookExA(int id, HOOKPROC proc, HINSTANCE module, DWORD thread) {
    rfa::LogDetail("SetWindowsHookExA(%d, %p)", id, proc);
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
        RFA_LOG_DETAIL_FIRST(20, "RegisterRawInputDevices usage %X/%X flags %lX", copy[i].usUsagePage, copy[i].usUsage,
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
    if (set && set(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) rfa::LogDetail("Process is now per-monitor DPI aware");
    else rfa::Log("Could not change DPI awareness (error %lu); continuing", GetLastError());
}

}  // namespace

void borderless::Init() {
    g_enabled = rfa::IniInt(L"Borderless", L"Enabled", 1) != 0;
    g_monitorIndex = rfa::IniInt(L"Borderless", L"Monitor", 0);
    g_ignoreFocusLoss = rfa::IniInt(L"Borderless", L"IgnoreFocusLoss", 1) != 0;
    g_allowWindowsKey = rfa::IniInt(L"Borderless", L"AllowWindowsKey", 1) != 0;
    if (rfa::game::IsSupportedBuild())
        g_pauseMenuRequest = reinterpret_cast<BYTE*>(0x135B3F1);
    else
        rfa::Log("Not the publicbeta build; switching away won't open the pause menu");
    if (!g_enabled) rfa::LogDetail("Enabled=0: the game runs unmodified (with [Loader] Log=1 what its window does is logged)");
    if (g_enabled && rfa::IniInt(L"Borderless", L"DpiAware", 1)) MakeDpiAware();

    int count = 0;
    rfa::LogDetail("Monitors (set [Borderless] Monitor= to pick one, 0 = primary):");
    EnumDisplayMonitors(nullptr, nullptr, LogMonitor, reinterpret_cast<LPARAM>(&count));
    RECT r = TargetRect();
    rfa::LogInfo("In Windowed mode the game will cover %ldx%ld at %ld,%ld", r.right - r.left, r.bottom - r.top, r.left,
             r.top);

    if (g_enabled && rfa::game::IsSupportedBuild()) {
        o_ApplyDisplayMode = reinterpret_cast<ApplyDisplayModeFn>(
            rfa::HookFunction(kApplyDisplayMode, kApplyDisplayModePrologue, reinterpret_cast<void*>(&Hk_ApplyDisplayMode)));
        rfa::LogDetail("Hook of the game's display mode change: %s", o_ApplyDisplayMode ? "ok" : "FAILED");
    }

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
        {"USER32.dll", "ShowCursor", rfa::HookImport(game, "USER32.dll", "ShowCursor", &Hk_ShowCursor, &o_ShowCursor)},
        {"USER32.dll", "SetCapture", rfa::HookImport(game, "USER32.dll", "SetCapture", &Hk_SetCapture, &o_SetCapture)},
        {"USER32.dll", "ReleaseCapture", rfa::HookImport(game, "USER32.dll", "ReleaseCapture", &Hk_ReleaseCapture, &o_ReleaseCapture)},
        {"USER32.dll", "SetFocus", rfa::HookImport(game, "USER32.dll", "SetFocus", &Hk_SetFocus, &o_SetFocus)},
        {"USER32.dll", "SetForegroundWindow", rfa::HookImport(game, "USER32.dll", "SetForegroundWindow", &Hk_SetForegroundWindow, &o_SetForegroundWindow)},
        {"USER32.dll", "SetActiveWindow", rfa::HookImport(game, "USER32.dll", "SetActiveWindow", &Hk_SetActiveWindow, &o_SetActiveWindow)},
        {"USER32.dll", "SetCursorPos", rfa::HookImport(game, "USER32.dll", "SetCursorPos", &Hk_SetCursorPos, &o_SetCursorPos)},
        {"USER32.dll", "GetForegroundWindow", rfa::HookImport(game, "USER32.dll", "GetForegroundWindow", &Hk_GetForegroundWindow, &o_GetForegroundWindow)},
        {"USER32.dll", "IsIconic", rfa::HookImport(game, "USER32.dll", "IsIconic", &Hk_IsIconic, &o_IsIconic)},
        {"USER32.dll", "GetAsyncKeyState", rfa::HookImport(game, "USER32.dll", "GetAsyncKeyState", &Hk_GetAsyncKeyState, &o_GetAsyncKeyState)},
        {"USER32.dll", "RegisterRawInputDevices", rfa::HookImport(game, "USER32.dll", "RegisterRawInputDevices", &Hk_RegisterRawInputDevices, &o_RegisterRawInputDevices)},
        {"USER32.dll", "SetWindowsHookExA", rfa::HookImport(game, "USER32.dll", "SetWindowsHookExA", &Hk_SetWindowsHookExA, &o_SetWindowsHookExA)},
        {"XINPUT1_3.dll", "#2 (XInputGetState)", rfa::HookImport(game, "XINPUT1_3.dll", "#2", &Hk_XInputGetState, &o_XInputGetState)},
    };
    int hooked = 0;
    for (const auto& h : hooks) {
        if (h.ok) ++hooked;
        else rfa::Log("Hook %s!%s: FAILED", h.dll, h.func);
    }
    rfa::LogDetail("Hooked %d of the game's %d window, cursor and input functions", hooked, static_cast<int>(sizeof(hooks) / sizeof(hooks[0])));
}
