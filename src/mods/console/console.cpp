// Improvements to the developer console of the publicbeta build (opened with the ` key).
//
// The mod draws the console itself, in place of the game's drawing function (0xC7B890), with the
// game's own rectangle and text functions, laid out like Quake 3's console:
//   - a dark panel behind it (console_set_bg_color still changes its colour), a lighter strip for
//     the line you type, and a coloured line along its edge;
//   - bigger text (the console's font, scaled), the line you type bigger still, and a bright caret
//     where you type;
//   - the commands you typed in amber and errors in red, so they stand out from the rest;
//   - long lines wrapped onto the next line (the game cut them at 126 characters), and the same
//     line printed again and again shown once, with how many times;
//   - a scroll bar, and a row of ^ marks while scrolled back;
//   - the build stamp just above the typed line, on the right: the game's version, Steam build,
//     internal name and link time, and RFA-Mod's version;
//   - it slides open and shut in a sixth of a second.
//
// Notify lines: while the console is shut, the last 4 lines it printed show at the top left of the
// screen for 3 seconds. The game itself keeps nothing it prints while the console is shut.
//
// History: the game keeps only its last 256 lines. The mod keeps its own copy of every line the
// console prints (10000 by default) and also writes them to logs\console.log. Scroll with the
// mouse wheel (3 lines a notch), Page Up / Page Down (a page; with Shift, a line), Ctrl+Home (the
// oldest line) and End or Ctrl+End (back to the newest). While the console is open the wheel
// doesn't reach the game, so it no longer switches weapons behind it. The mouse works on it too
// (see "The mouse" below): drag the scroll bar, or hold or click the middle button to autoscroll.
//
// The render debug panel (` + X): it takes the mouse only while the console is open (the game frees
// the mouse for the console). While the panel is up, the console moves to the bottom of the screen,
// below the panel's windows, and is drawn under them, so its text no longer covers them.
//
// [Console] in rfa_mods.ini: its height, text sizes, darkness, history length, the log file, the
// notify lines and the build stamp.
//
// console.ini: console settings this mod applies for you, as <console name>=<value>:
//   [Startup]   once, when the game has finished starting (right after its own gamestart.txt)
//   [Gameplay]  every time gameplay starts after loading (right after the game's own per-level
//               scripts and anything set with console_gp_enter)
// Each name is looked up in the game's console command list to see what kind it is:
//   on/off settings ("Toggles X")   1 / 0 become the console's own "on" / "off"
//   number settings ("Sets X")      the value is passed as written
//   commands                        1 / 0 become "on" / "off", anything else is passed as written,
//                                   and an empty value just runs the command
//
// The level editor's link: the game shows the editor (bin\editor) its camera and what is picked,
// every frame, through shared memory, and runs the commands the editor puts in bin\link (see "The
// level editor's link" below).
#define RFA_MOD_NAME "console"
#include "rfa_mod.h"
#include "rfa_game.h"
#include <algorithm>
#include <climits>
#include <ctime>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace {

using rfa::game::At;

// ---------------------------------------------------------------------------------------------
// Settings: [Console] in rfa_mods.ini

struct Settings {
    float height = 0.5f;          // how much of the screen the console covers (the game's own: 0.4)
    float textScale = 1.25f;      // the output's size, times the console font's
    float inputScale = 1.5f;      // the typed line's
    int opacity = 85;             // how dark the panel behind it is, in percent
    size_t historyLines = 10000;  // lines kept for scrolling back
    bool logToFile = true;        // also write them to logs\console.log
    bool notify = true;           // keep what it prints while shut, and show the newest lines for a moment
    bool buildStamp = true;       // the build stamp in its corner
};
Settings g_settings;

float IniFloat(const wchar_t* key, const wchar_t* fallback) {
    return static_cast<float>(_wtof(rfa::IniString(L"Console", key, fallback).c_str()));
}

void ReadSettings() {
    g_settings.height = std::clamp(rfa::IniInt(L"Console", L"Height", 50), 20, 100) / 100.0f;
    g_settings.textScale = std::clamp(IniFloat(L"TextScale", L"1.25"), 0.5f, 4.0f);
    g_settings.inputScale = std::clamp(IniFloat(L"InputScale", L"1.5"), 0.5f, 4.0f);
    g_settings.opacity = std::clamp(rfa::IniInt(L"Console", L"Opacity", 85), 0, 100);
    g_settings.historyLines = static_cast<size_t>(std::clamp(rfa::IniInt(L"Console", L"HistoryLines", 10000), 256, 200000));
    g_settings.logToFile = rfa::IniInt(L"Console", L"LogToFile", 1) != 0;
    g_settings.notify = rfa::IniInt(L"Console", L"Notify", 1) != 0;
    g_settings.buildStamp = rfa::IniInt(L"Console", L"BuildStamp", 1) != 0;
}

// ---------------------------------------------------------------------------------------------
// The game's console and 2D drawing, in the publicbeta exe

struct Colour {
    BYTE r, g, b, a;
};

auto& g_consoleReady = *reinterpret_cast<BYTE*>(0x28FF1C8);    // console_init (0xC7DCD0) has run
auto& g_consoleSliding = *reinterpret_cast<BYTE*>(0x28FF1C9);  // sliding open or shut
auto& g_consoleOpen = *reinterpret_cast<BYTE*>(0x28FF1CA);
auto& g_textMode = *reinterpret_cast<BYTE*>(0x11212E0);        // output goes to the text window instead (not on PC)
auto& g_drawRectangle = *reinterpret_cast<void**>(0x28FF1DC);  // what console_init was given to draw with:
auto& g_drawText = *reinterpret_cast<void**>(0x28FF1E0);       //   0xA44640 and 0xA446C0
auto& g_screenLeft = *reinterpret_cast<int*>(0x28FF1E4);       // where text starts (0xD04A90), plus
auto& g_offsetX = *reinterpret_cast<int*>(0x11212FC);          //   console_offset_x
auto& g_screenWidth = *reinterpret_cast<int*>(0x28FF1E8);
auto& g_screenHeight = *reinterpret_cast<int*>(0x28FF1EC);
auto& g_background = *reinterpret_cast<Colour*>(0x290767C);    // console_set_bg_color sets it
auto& g_typed = *reinterpret_cast<const char(*)[0x400]>(0x28FB640);  // the line being typed (up to 124 characters)
auto& g_prompted = *reinterpret_cast<const char(*)[0x80]>(0x29075F8);  // "] " and that line: what Enter echoes
auto& g_caret = *reinterpret_cast<int*>(0x28FF1D0);            // where in the typed line the caret is
auto& g_caretShown = *reinterpret_cast<BYTE*>(0x28F723D);      // blinks every 300 ms
auto& g_caretTimer = *reinterpret_cast<int*>(0x28FF1F4);       // a timer of the game's: when to blink next
auto& g_scrollLine = *reinterpret_cast<int*>(0x28FBA40);       // the game's own scrolling: kept at -1 (not scrolled)
auto& g_frameSeconds = *reinterpret_cast<float*>(0x11217CC);   // how long the last frame took
auto& g_consoleFont = *reinterpret_cast<int*>(0x110AAF0);      // the font "ug-debug"

constexpr Colour kGameBackground = {16, 16, 16, 128};  // the game's own, half see-through grey
constexpr float kSlidesPerSecond = 6;                  // open or shut in a sixth of a second, as in Quake 3

// The game's timer methods (the timer in ECX, called as __fastcall with EDX unused): has it run
// out (0xC8B550), and run out in so many milliseconds from now (0xC8B4B0).
auto const TimerDone = reinterpret_cast<bool(__fastcall*)(int* timer, void* unused)>(0xC8B550);
auto const TimerStart = reinterpret_cast<void(__fastcall*)(int* timer, void* unused, int milliseconds)>(0xC8B4B0);

// 2D drawing. 0x2DD7B30 is the drawing mode the console, the debug text and the menus all use; the
// renderer takes its own form of it, which 0xD05C40 makes (as 0xD06300, the game's text call, does).
struct RenderMode {
    int value;
};
constexpr DWORD kMode2D = 0x2DD7B30;
using DrawRectangleFn = void(__cdecl*)(int x, int y, int width, int height, const Colour* colour);
auto const SetColour = At<void(__cdecl*)(const Colour*)>(0xD04E20);
auto const ToRenderMode = At<void(__cdecl*)(RenderMode*, const void* mode)>(0xD05C40);
auto const Renderer = At<void*(__cdecl*)()>(0x4FD5C0);
// How big text is at scale 1: width and height in pixels of its first `length` characters (-1 = all).
auto const MeasureText = At<void(__cdecl*)(int* width, int* height, const char* text, int length, int font)>(0xD061F0);
// The renderer's text method (the renderer in ECX), which takes a scale; the game always passes 1.
using DrawTextScaledFn = void(__fastcall*)(void* renderer, void* unused, float x, float y, const char* text, float scale,
                                           bool centred, int font, const RenderMode* mode);
auto const DrawTextScaled = reinterpret_cast<DrawTextScaledFn>(0xD18A80);

void DrawRectangle(int x, int y, int width, int height, Colour colour) {
    if (width > 0 && height > 0) reinterpret_cast<DrawRectangleFn>(g_drawRectangle)(x, y, width, height, &colour);
}

void DrawString(float x, float y, const char* text, float scale, Colour colour) {
    if (!*text || !colour.a) return;
    SetColour(&colour);
    RenderMode mode{-1};
    ToRenderMode(&mode, reinterpret_cast<const void*>(kMode2D));
    DrawTextScaled(Renderer(), nullptr, x, y, text, scale, false, g_consoleFont, &mode);
}

// The width of the first `length` characters at scale 1.
int TextWidth(const char* text, int length = -1) {
    int width = 0, height = 0;
    MeasureText(&width, &height, text, length, g_consoleFont);
    return width;
}

int FontHeight() {
    int width = 0, height = 0;
    MeasureText(&width, &height, "", -1, g_consoleFont);  // as the game measures its line height
    return height > 0 ? height : 15;
}

Colour Brighter(Colour colour, int amount, BYTE minimumAlpha) {
    auto add = [amount](BYTE value) { return static_cast<BYTE>(std::min(255, value + amount)); };
    return {add(colour.r), add(colour.g), add(colour.b), std::max(colour.a, minimumAlpha)};
}

// ---------------------------------------------------------------------------------------------
// The history: every line the console printed, and how far back the view is scrolled

enum class Kind : BYTE { Output, Command, Error };

struct Line {
    std::string text;
    Kind kind;
    int repeats;       // printed this many times in a row
    ULONGLONG printed; // when it was last printed (GetTickCount64), for the notify lines
};

constexpr Colour kOutputColour = {225, 225, 225, 255};
constexpr Colour kCommandColour = {255, 196, 80, 255};  // what you typed, as Enter echoes it
constexpr Colour kErrorColour = {255, 112, 96, 255};
constexpr Colour kTypedColour = {255, 255, 255, 255};
constexpr Colour kAccentColour = {90, 170, 255, 255};  // the prompt, the edge line and the scroll bar
constexpr Colour kDimColour = {160, 160, 160, 255};
constexpr Colour kCaretColour = {255, 196, 80, 255};

Colour LineColour(Kind kind) {
    return kind == Kind::Command ? kCommandColour : kind == Kind::Error ? kErrorColour : kOutputColour;
}

// The game prints from other threads too (loading), so the history is only touched under this lock.
std::mutex g_historyLock;
std::deque<Line> g_history;
int g_back = 0;      // how many lines the bottom of the view is above the newest (0 = following new output)
int g_maxBack = 0;   // the most it can be: the oldest line at the top (worked out as the console is drawn)
int g_pageLines = 1; // lines on screen, for Page Up / Page Down
bool g_drawsConsole = false;  // the mod draws the console, so the keys and the wheel move its history

bool LooksLikeError(const std::string& text) {
    std::string lower = text;
    for (char& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    for (const char* word : {"error", "unknown command", "invalid", "failed"})
        if (lower.find(word) != std::string::npos) return true;
    return false;
}

void WriteToLog(const std::string& text) {
    static HANDLE file = [] {
        if (!g_settings.logToFile) return INVALID_HANDLE_VALUE;
        CreateDirectoryW((rfa::RootDir() + L"\\logs").c_str(), nullptr);
        return CreateFileW((rfa::RootDir() + L"\\logs\\console.log").c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    }();
    if (file == INVALID_HANDLE_VALUE) return;
    const std::string line = text + "\r\n";
    DWORD written;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

void AddLine(std::string text, Kind kind) {
    std::string clean;  // the font draws no tabs or other control characters
    for (char c : text) {
        if (c == '\t') clean += "    ";
        else if (static_cast<unsigned char>(c) >= 0x20) clean += c;
    }
    if (kind == Kind::Output && LooksLikeError(clean)) kind = Kind::Error;
    std::lock_guard<std::mutex> lock(g_historyLock);
    if (!clean.empty() && !g_history.empty() && g_history.back().text == clean && g_history.back().kind == kind) {
        ++g_history.back().repeats;  // the same line again: counted, not added
        g_history.back().printed = GetTickCount64();
        return;
    }
    WriteToLog(clean);
    g_history.push_back({std::move(clean), kind, 1, GetTickCount64()});
    if (g_back > 0) ++g_back;  // while scrolled back, the view stays on the same lines
    while (g_history.size() > g_settings.historyLines) g_history.pop_front();
    g_back = std::min(g_back, static_cast<int>(g_history.size()) - 1);
}

// Text the game printed: one line per '\n', as the game splits it (so "" is an empty line).
void AddOutput(const char* text) {
    const char* end = text + strlen(text);
    do {
        const char* newline = strchr(text, '\n');
        if (!newline) newline = end;
        AddLine(std::string(text, newline), Kind::Output);
        text = newline + 1;
    } while (text < end);
}

// Positive: further back. Stops at the oldest line and at the newest.
void Scroll(int lines) {
    std::lock_guard<std::mutex> lock(g_historyLock);
    g_back = std::clamp(g_back + lines, 0, g_maxBack);
}

bool ScrolledBack() {
    std::lock_guard<std::mutex> lock(g_historyLock);
    return g_back > 0;
}

// The game's console print (0xC7C310): cdecl (text, flags). It keeps lines only while the console
// is open (and not in text mode), 126 characters at most; the mod keeps the same lines, whole, and
// with notify lines on, those it prints while shut too.
constexpr DWORD kConsolePrint = 0xC7C310;
constexpr size_t kConsolePrintPrologue = 6;  // push ebp / mov ebp,esp / sub esp,1Ch
using ConsolePrintFn = void(__cdecl*)(const char*, int);
ConsolePrintFn g_consolePrint;

void __cdecl ConsolePrint(const char* text, int flags) {
    g_consolePrint(text, flags);
    if (text && !g_textMode && (g_consoleOpen || g_settings.notify)) AddOutput(text);
}

// Adds a character to the console's composed line (0xC7D140): cdecl (char). Enter sends '\n',
// which puts "] <command>" into the game's lines before the command runs.
constexpr DWORD kConsoleEcho = 0xC7D140;
constexpr size_t kConsoleEchoPrologue = 8;  // push ebp / mov ebp,esp / push ecx / movsx eax,byte ptr [ebp+8]
using ConsoleEchoFn = void(__cdecl*)(char);
ConsoleEchoFn g_consoleEcho;

void __cdecl ConsoleEcho(char c) {
    if (c == '\n') AddLine(std::string(g_prompted, strnlen(g_prompted, sizeof(g_prompted))), Kind::Command);
    g_consoleEcho(c);
}

// ---------------------------------------------------------------------------------------------
// Keys and the mouse wheel

// The console's key handling (0xC7C580), each frame while it is open: it takes keys from the
// game's queue (0xCADA40) until there are none. A key is its DirectInput scan code, plus 0x1000
// with Shift held, 0x2000 with Alt, 0x4000 with Ctrl and 0x8000 while ` is held.
constexpr DWORD kConsoleKeys = 0xC7C580;
constexpr size_t kConsoleKeysPrologue = 6;  // push ebp / mov ebp,esp / sub esp,60h
constexpr DWORD kNextKey = 0xCADA40;        // int(): the next key pressed, 0 when there are none
constexpr size_t kNextKeyPrologue = 8;      // push ebp / mov ebp,esp / mov al,byte ptr [0293AC0Ch]
constexpr int kShift = 0x1000, kCtrl = 0x4000, kGraveHeld = 0x8000;
constexpr int kHome = 0xC7, kPageUp = 0xC9, kEnd = 0xCF, kPageDown = 0xD1;
constexpr int kLinesPerNotch = 3;

using VoidFn = void(__cdecl*)();
using NextKeyFn = int(__cdecl*)();
VoidFn g_consoleKeys;
NextKeyFn g_nextKey;
bool g_readingConsoleKeys = false;

int PageLines() { return std::max(1, g_pageLines - 1); }

// The keys that move through the history. Home and End without Ctrl still move the caret, except
// that End first comes back from scrolling. Returns whether the key was used.
bool ScrollKey(int key) {
    switch (key) {
    case kPageUp: Scroll(PageLines()); return true;
    case kPageUp | kShift: Scroll(1); return true;
    case kPageDown: Scroll(-PageLines()); return true;
    case kPageDown | kShift: Scroll(-1); return true;
    case kHome | kCtrl: Scroll(INT_MAX / 2); return true;
    case kEnd | kCtrl: Scroll(-INT_MAX / 2); return true;
    case kEnd:
        if (!ScrolledBack()) return false;
        Scroll(-INT_MAX / 2);
        return true;
    }
    return false;
}

void __cdecl ConsoleKeys() {
    g_readingConsoleKeys = true;
    g_consoleKeys();
    g_readingConsoleKeys = false;
    g_scrollLine = -1;
}

int __cdecl NextKey() {
    for (;;) {
        const int key = g_nextKey();
        if (!key || !g_drawsConsole || !g_readingConsoleKeys || !g_consoleOpen || !ScrollKey(key & ~kGraveHeld))
            return key;
    }
}

// ---------------------------------------------------------------------------------------------
// The render debug panel (` + X)
//
// ` + X (0x4D4F40) switches the debug display to mode 4, whose update (0x76E7C0) hands the panel
// the mouse (0x56F660); its draw (0x76EA00) runs from the world's draw (0x768BA0), before the 2D
// pass that draws the console (0x5227C0). The panel's windows are the children of its root: a
// ring through each window's `next`, from the root's `firstChild`.

constexpr DWORD kPanelDraw = 0x76EA00;
constexpr size_t kPanelDrawPrologue = 10;  // push ebp / mov ebp,esp / movzx eax,byte ptr [019742BCh]
auto& g_panelShown = *reinterpret_cast<BYTE*>(0x19742BC);

struct PanelWindow {
    void* functions;
    float left, top, right, bottom;  // on screen, in pixels
    BYTE unknown14[4];
    const PanelWindow* next;
    const PanelWindow* previous;
    BYTE unknown20[4];
    const PanelWindow* firstChild;
};
static_assert(offsetof(PanelWindow, next) == 0x18 && offsetof(PanelWindow, firstChild) == 0x24, "the game's layout");
auto& g_panelRoot = *reinterpret_cast<const PanelWindow*>(0x19750F8);

// The windows are objects in the exe's data sections; anything else isn't one.
bool InGameData(const void* pointer) {
    const auto address = reinterpret_cast<uintptr_t>(pointer);
    return address >= 0x10F4000 && address < 0x3711000 && address % 4 == 0;
}

// How far down the screen the panel's windows reach (0 if it has none).
float PanelBottom() {
    float bottom = 0;
    const PanelWindow* first = g_panelRoot.firstChild;
    const PanelWindow* window = first;
    for (int i = 0; i < 32 && InGameData(window); ++i) {
        if (window->top < window->bottom && window->bottom > 0 && window->bottom < g_screenHeight * 2.0f)
            bottom = std::max(bottom, window->bottom);
        window = window->next;
        if (window == first) break;
    }
    return bottom;
}

// ---------------------------------------------------------------------------------------------
// The build stamp: what the game and the mod are, like a development build's stamp, e.g.
//   Red Faction: Armageddon v1.01 build 1932726 RF4_pc_release_dx11 Fri Jun 30 16:42:00 2017 pc | RFA-Mod v0.4.1-beta

constexpr DWORD kGameVersionText = 0xF9A4F8;  // "v1.01", the version the pause menu shows
constexpr int kSteamBuild = 1932726;          // the publicbeta exe, the only one the mod changes

std::string g_buildStamp;

// The name the exe has in its debug information (its CodeView record: "RSDS", GUID, age, path).
std::string ExeDebugName() {
    auto base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    auto entry = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + directory.VirtualAddress);
    for (DWORD i = 0; directory.VirtualAddress && i < directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++i, ++entry) {
        if (entry->Type != IMAGE_DEBUG_TYPE_CODEVIEW || !entry->AddressOfRawData || entry->SizeOfData < 25) continue;
        auto record = reinterpret_cast<const char*>(base + entry->AddressOfRawData);
        if (memcmp(record, "RSDS", 4) != 0) continue;
        std::string name(record + 24, strnlen(record + 24, entry->SizeOfData - 24));
        name = name.substr(name.find_last_of("\\/") + 1);
        return name.substr(0, name.find_last_of('.'));
    }
    return "";
}

// When the exe was linked (its header's time stamp), as C's asctime writes a time, in UTC.
std::string ExeLinkTime() {
    const time_t linked = static_cast<time_t>(rfa::game::ExeTimestamp());
    tm utc;
    char text[64] = "";
    if (gmtime_s(&utc, &linked) == 0) strftime(text, sizeof(text), "%a %b %d %H:%M:%S %Y", &utc);
    return text;
}

// A release's version.txt (next to rfa_mods.ini) starts "RFA-Mod v0.4.1-beta, built from ...";
// without one (a build of your own), the day this mod was compiled.
std::string ModVersion() {
    FILE* file = nullptr;
    if (_wfopen_s(&file, (rfa::RootDir() + L"\\version.txt").c_str(), L"r") == 0 && file) {
        char line[256] = "";
        const bool read = fgets(line, sizeof(line), file) != nullptr;
        fclose(file);
        std::string text = read ? line : "";
        text = text.substr(0, text.find_first_of(",\r\n"));
        if (text.rfind("RFA-Mod ", 0) == 0) return text;
    }
    std::string date = __DATE__;  // "Nov  1 2026": one space is enough
    for (size_t at; (at = date.find("  ")) != std::string::npos;) date.erase(at, 1);
    return std::string("RFA-Mod dev build ") + date + " " + __TIME__;
}

void MakeBuildStamp() {
    char stamp[256];
    snprintf(stamp, sizeof(stamp), "Red Faction: Armageddon %s build %d %s %s pc | %s",
             reinterpret_cast<const char*>(kGameVersionText), kSteamBuild, ExeDebugName().c_str(), ExeLinkTime().c_str(),
             ModVersion().c_str());
    g_buildStamp = stamp;
    for (size_t at; (at = g_buildStamp.find("  ")) != std::string::npos;) g_buildStamp.erase(at, 1);  // a part left empty
}

// ---------------------------------------------------------------------------------------------
// Drawing the console

constexpr DWORD kConsoleDraw = 0xC7B890;  // void(): the game's console drawing, each frame from 0xC7D540

float g_shown = 0;  // how far it has slid open, 0 to 1
bool g_backgroundSet = false;
bool g_drawnUnderPanel = false;

constexpr Colour kStampColour = {125, 145, 170, 255};
constexpr Colour kShadowColour = {0, 0, 0, 200};
constexpr int kNotifyLines = 4;                  // as in Quake 3
constexpr ULONGLONG kNotifyMilliseconds = 3000;

// Slides it open or shut; false while it is shut.
bool Slide() {
    if (g_consoleSliding) {
        const float step = g_frameSeconds * kSlidesPerSecond;
        g_shown += g_consoleOpen ? step : -step;
        if (g_shown >= 1 || g_shown <= 0) {
            g_shown = std::clamp(g_shown, 0.0f, 1.0f);
            g_consoleSliding = 0;
        }
    } else {
        g_shown = g_consoleOpen ? 1.0f : 0.0f;
    }
    return g_shown > 0;
}

// A line as it is shown: with how many times it was printed in a row, if more than once.
std::string Shown(const Line& line) {
    return line.repeats > 1 ? line.text + "  (x" + std::to_string(line.repeats) + ")" : line.text;
}

// The longest start of `text` (at least one character) that fits in `width` at scale 1.
int CharactersThatFit(const char* text, int length, float width) {
    if (TextWidth(text, length) <= width) return length;
    int low = 1, high = length;
    while (low < high) {
        const int middle = (low + high + 1) / 2;
        if (TextWidth(text, middle) <= width) low = middle;
        else high = middle - 1;
    }
    return low;
}

// A line cut into the rows it takes up, breaking after a space where there is one.
std::vector<std::string> Wrap(const std::string& text, float width) {
    std::vector<std::string> rows;
    size_t start = 0;
    do {
        const char* rest = text.c_str() + start;
        const int left = static_cast<int>(text.size() - start);
        int fits = CharactersThatFit(rest, left, width);
        if (fits < left) {
            int space = fits - 1;
            while (space > 0 && rest[space] != ' ') --space;
            if (space >= fits / 2) fits = space + 1;
        }
        rows.emplace_back(rest, fits);
        start += fits;
    } while (start < text.size());
    return rows;
}

// Sizes and places that depend on the screen and the settings.
struct Metrics {
    float screenScale, textScale, inputScale;
    int fontHeight, rowHeight, typedHeight, padding, inputHeight, edge, left, scrollBarWidth;
    float textWidth;  // how wide output rows may be, at scale 1
};

Metrics Measure() {
    Metrics m;
    m.screenScale = std::max(1.0f, g_screenHeight / 1080.0f);  // bigger screens get bigger text
    m.textScale = g_settings.textScale * m.screenScale;
    m.inputScale = g_settings.inputScale * m.screenScale;
    m.fontHeight = FontHeight();
    m.rowHeight = static_cast<int>(m.fontHeight * m.textScale + 0.5f) + 2;
    m.typedHeight = static_cast<int>(m.fontHeight * m.inputScale + 0.5f);
    m.padding = static_cast<int>(4 * m.screenScale);
    m.inputHeight = m.typedHeight + 2 * m.padding;
    m.edge = std::max(2, static_cast<int>(2 * m.screenScale));
    m.left = g_screenLeft + g_offsetX + 2 * m.padding;
    m.scrollBarWidth = std::max(8, static_cast<int>(8 * m.screenScale));
    m.textWidth = static_cast<float>(g_screenWidth - m.left - 3 * m.scrollBarWidth) / m.textScale;
    return m;
}

// Notify lines, while the console is shut: the newest lines printed in the last 3 seconds, at
// most 4, at the top left of the screen with a shadow (there is no panel behind them).
void DrawNotify(const Metrics& m) {
    std::vector<Line> recent;
    {
        std::lock_guard<std::mutex> lock(g_historyLock);
        const ULONGLONG now = GetTickCount64();
        for (auto line = g_history.rbegin(); line != g_history.rend() && recent.size() < kNotifyLines; ++line) {
            if (now - line->printed > kNotifyMilliseconds) break;
            recent.push_back(*line);
        }
    }
    int y = 2 * m.padding;
    int rowsLeft = 2 * kNotifyLines;  // long lines wrap, but not without end
    for (auto line = recent.rbegin(); line != recent.rend(); ++line) {
        for (const std::string& row : Wrap(Shown(*line), m.textWidth)) {
            if (rowsLeft-- <= 0) return;
            DrawString(static_cast<float>(m.left + 1), static_cast<float>(y + 1), row.c_str(), m.textScale, kShadowColour);
            DrawString(static_cast<float>(m.left), static_cast<float>(y), row.c_str(), m.textScale, LineColour(line->kind));
            y += m.rowHeight;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The mouse
//
// While the console is open the game stops keeping the mouse in the middle of the screen
// (0x4E4070), but it hides Windows' cursor (0xCB13C0, 0xCB1820), so the mod draws its own over the
// console. On the console:
//   - the scroll bar: drag its thumb, or press on its track to jump there and drag from there;
//   - the middle button: scrolls by how far the mouse is above or below where it was pressed,
//     faster the further away, as Windows' autoscroll does. Hold it while moving, or click it and
//     move; another click ends it;
//   - a click on the typed line puts the caret there.
// Clicks on the console don't reach the game (or the menu behind it); clicks elsewhere do.

auto& g_gameWindow = *reinterpret_cast<HWND*>(0x293A880);  // what 0xCB1D10 returns

// What was drawn last frame, for finding what the mouse is on.
struct Drawn {
    bool shown;
    int top, bottom;  // the console, on screen
    bool scrollBar;
    int barX, barWidth, trackTop, trackBottom, thumbTop, thumbBottom, maxBack;
    int inputTop, inputBottom, typedX, first;  // the typed line, and its first character shown
    float inputScale;
};
Drawn g_drawn = {};

bool g_dragging = false;  // the scroll bar's thumb, held
int g_grab = 0;           // where on the thumb it is held
bool g_autoScroll = false;
bool g_autoHeld = false;  // the middle button is still down since it began
POINT g_anchor = {};
ULONGLONG g_autoBegan = 0, g_autoTick = 0;
float g_autoLines = 0;    // lines scrolled but not yet whole
bool g_swallowUp[3] = {};  // a press the console took, so the game doesn't get its release either: left, middle, right

// Window pixels to the size the game draws at (the same, unless the picture is scaled).
bool ToDrawn(POINT& point) {
    RECT client;
    if (!g_gameWindow || !GetClientRect(g_gameWindow, &client) || client.right <= 0 || client.bottom <= 0) return false;
    if (point.x < 0 || point.y < 0 || point.x >= client.right || point.y >= client.bottom) return false;
    point.x = MulDiv(point.x, g_screenWidth, client.right);
    point.y = MulDiv(point.y, g_screenHeight, client.bottom);
    return true;
}

bool MouseNow(POINT& point) {
    return GetCursorPos(&point) && ScreenToClient(g_gameWindow, &point) && ToDrawn(point);
}

// Whether a button is held right now, even if let go outside the window (no message then).
// Windows reads the physical buttons here, so swapped buttons are swapped back.
bool ButtonHeld(bool left) {
    const bool swapped = GetSystemMetrics(SM_SWAPBUTTON) != 0;
    const int key = left ? (swapped ? VK_RBUTTON : VK_LBUTTON) : VK_MBUTTON;
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

bool OnConsole(const POINT& point) { return g_drawn.shown && point.y >= g_drawn.top && point.y < g_drawn.bottom; }

bool OnScrollBar(const POINT& point) {
    return g_drawn.scrollBar && point.x >= g_drawn.barX - 2 * g_drawn.barWidth && point.y >= g_drawn.trackTop &&
           point.y < g_drawn.trackBottom;
}

// Scrolls so that the thumb's top is at `thumbTop`.
void ThumbTo(int thumbTop) {
    const int thumb = g_drawn.thumbBottom - g_drawn.thumbTop;
    const int travel = g_drawn.trackBottom - g_drawn.trackTop - thumb;
    if (travel <= 0 || g_drawn.maxBack <= 0) return;
    const int fromBottom = std::clamp(g_drawn.trackBottom - thumbTop - thumb, 0, travel);
    std::lock_guard<std::mutex> lock(g_historyLock);
    g_back = std::clamp((fromBottom * g_drawn.maxBack + travel / 2) / travel, 0, g_maxBack);
}

// The caret to the character nearest `x` on the typed line.
void CaretTo(int x) {
    const char* typed = g_typed;
    const int length = static_cast<int>(strnlen(typed, sizeof(g_typed)));
    const int first = std::min(g_drawn.first, length);
    int best = first, nearest = INT_MAX;
    for (int i = first; i <= length; ++i) {
        const int at = g_drawn.typedX + static_cast<int>(TextWidth(typed + first, i - first) * g_drawn.inputScale);
        if (abs(at - x) < nearest) {
            nearest = abs(at - x);
            best = i;
        }
        if (at > x) break;
    }
    g_caret = best;
    g_caretShown = 1;  // shown straight away, as when typing
    TimerStart(&g_caretTimer, nullptr, 300);
}

// A button pressed or let go, where the mouse is on the drawn screen (null: outside the window);
// true when the console took it.
bool ConsoleButton(int button, bool down, const POINT* at) {
    if (!down) {
        const bool swallow = g_swallowUp[button];
        g_swallowUp[button] = false;
        if (button == 0) g_dragging = false;
        if (button == 1 && g_autoScroll && g_autoHeld) {
            g_autoHeld = false;  // held and moved, or held a while: it ends; a quick click keeps it going
            const bool moved = !at || abs(at->y - g_anchor.y) > 12 || abs(at->x - g_anchor.x) > 12;
            if (moved || GetTickCount64() - g_autoBegan > 350) g_autoScroll = false;
        }
        return swallow;
    }
    g_swallowUp[button] = false;
    if (g_autoScroll) {  // any press ends autoscrolling
        g_autoScroll = false;
        g_swallowUp[button] = true;
        return true;
    }
    if (!at || !OnConsole(*at)) return false;
    const POINT point = *at;
    g_swallowUp[button] = true;
    if (button == 0 && OnScrollBar(point)) {
        const bool onThumb = point.y >= g_drawn.thumbTop && point.y < g_drawn.thumbBottom;
        g_grab = onThumb ? point.y - g_drawn.thumbTop : (g_drawn.thumbBottom - g_drawn.thumbTop) / 2;
        g_dragging = true;
        ThumbTo(point.y - g_grab);
    } else if (button == 0 && point.y >= g_drawn.inputTop && point.y < g_drawn.inputBottom) {
        CaretTo(point.x);
    } else if (button == 1) {
        g_autoScroll = g_autoHeld = true;
        g_anchor = point;
        g_autoBegan = g_autoTick = GetTickCount64();
        g_autoLines = 0;
    }
    return true;
}

// Each frame while the console is open: follows a held thumb, and scrolls while autoscrolling.
void FollowMouse(const POINT* mouse, float screenScale) {
    if (g_dragging) {
        if (!ButtonHeld(true)) g_dragging = false;
        else if (mouse) ThumbTo(mouse->y - g_grab);
    }
    if (!g_autoScroll) return;
    if (g_autoHeld && !ButtonHeld(false)) ConsoleButton(1, false, mouse);
    const ULONGLONG now = GetTickCount64();
    const float seconds = std::min(0.1f, (now - g_autoTick) / 1000.0f);
    g_autoTick = now;
    if (!mouse) return;
    const float away = static_cast<float>(abs(mouse->y - g_anchor.y)) / screenScale - 10;  // still within 10 pixels
    if (away > 0) {
        const float linesPerSecond = away * 0.2f + away * away * 0.002f;  // 50 pixels: 15, 300: 240
        g_autoLines += (mouse->y < g_anchor.y ? linesPerSecond : -linesPerSecond) * seconds;
    }
    const int whole = static_cast<int>(g_autoLines);
    if (whole) {
        Scroll(whole);
        g_autoLines -= whole;
    }
}

// The console is shut: nothing held, and the game gets every click again.
void StopMouse() {
    g_dragging = g_autoScroll = g_autoHeld = false;
    for (bool& swallow : g_swallowUp) swallow = false;
    g_drawn.shown = false;
}

// An arrow, drawn a pixel row at a time: white with a black edge.
void DrawCursor(int x, int y, float screenScale) {
    const int pixel = std::max(1, static_cast<int>(screenScale + 0.5f));
    constexpr int kRows = 13;
    for (int row = -1; row <= kRows; ++row) {
        const int width = std::clamp(row, 0, kRows - 1) * 2 / 3 + 1;
        DrawRectangle(x - pixel, y + row * pixel, (width + 2) * pixel, pixel, {0, 0, 0, 255});
        if (row >= 0 && row < kRows) DrawRectangle(x, y + row * pixel, width * pixel, pixel, {255, 255, 255, 255});
    }
}

// Where the middle button was pressed: a dot with arrows up and down.
void DrawAnchor(const Metrics& m) {
    const int size = std::max(6, static_cast<int>(6 * m.screenScale));
    DrawRectangle(g_anchor.x - size / 2, g_anchor.y - size / 2, size, size, kAccentColour);
    const float half = TextWidth("^") * m.textScale / 2;
    DrawString(g_anchor.x - half, static_cast<float>(g_anchor.y - size / 2 - m.rowHeight), "^", m.textScale, kAccentColour);
    DrawString(g_anchor.x - half, static_cast<float>(g_anchor.y + size / 2), "v", m.textScale, kAccentColour);
}

// The game's handler for mouse window messages (buttons, wheel, raw input): cdecl (message,
// wParam, lParam). It turns the buttons into the game's button states (0xCB08E0) and adds wheel
// movement to a total that weapon switching reads.
constexpr DWORD kMouseMessage = 0xCDF220;
constexpr size_t kMouseMessagePrologue = 6;  // push ebp / mov ebp,esp / sub esp,0Ch

using MouseMessageFn = void(__cdecl*)(UINT, WPARAM, LPARAM);
MouseMessageFn g_mouseMessage;
int g_wheel = 0;  // wheel movement not yet turned into whole notches (high-resolution wheels)

void __cdecl MouseMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_consoleOpen && g_drawsConsole) {
        if (message == WM_MOUSEWHEEL) {
            g_autoScroll = false;
            g_wheel += GET_WHEEL_DELTA_WPARAM(wParam);
            for (; g_wheel >= WHEEL_DELTA; g_wheel -= WHEEL_DELTA) Scroll(kLinesPerNotch);
            for (; g_wheel <= -WHEEL_DELTA; g_wheel += WHEEL_DELTA) Scroll(-kLinesPerNotch);
            RFA_LOG_FIRST(3, "Scrolled the console with the mouse wheel");
            return;  // the game doesn't see it
        }
        POINT point = {static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
        const bool inWindow = ToDrawn(point);
        int button = -1;
        bool down = false;
        switch (message) {
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: button = 0; down = true; break;
        case WM_LBUTTONUP: button = 0; break;
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: button = 1; down = true; break;
        case WM_MBUTTONUP: button = 1; break;
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: button = 2; down = true; break;
        case WM_RBUTTONUP: button = 2; break;
        }
        if (button >= 0 && ConsoleButton(button, down, inWindow ? &point : nullptr)) {
            RFA_LOG_FIRST(3, "The console took a mouse click");
            return;
        }
    } else {
        g_wheel = 0;
    }
    g_mouseMessage(message, wParam, lParam);
}

void DrawConsole() {
    if (!g_drawRectangle || !g_drawText) return;
    if (!g_backgroundSet) {  // the mod's darker panel, unless console_set_bg_color was used already
        g_backgroundSet = true;
        if (memcmp(&g_background, &kGameBackground, sizeof(Colour)) == 0)
            g_background = {10, 12, 16, static_cast<BYTE>(g_settings.opacity * 255 / 100)};
    }
    const Metrics m = Measure();
    if (!Slide()) {
        StopMouse();
        if (g_settings.notify) DrawNotify(m);
        return;
    }
    if (TimerDone(&g_caretTimer, nullptr)) {  // the caret's blink, as the game's drawing did it
        TimerStart(&g_caretTimer, nullptr, 300);
        g_caretShown = !g_caretShown;
    }
    // The build stamp gets a thin row of its own just above the typed line, at the font's own size.
    const bool stamped = g_settings.buildStamp && !g_buildStamp.empty();
    const float stampScale = m.screenScale;
    const int stampRow = stamped ? static_cast<int>(m.fontHeight * stampScale + 0.5f) + m.padding : 0;
    const int minimum = m.inputHeight + stampRow + 4 * m.rowHeight + 2 * m.padding;
    const int width = g_screenWidth, left = m.left, rowHeight = m.rowHeight, padding = m.padding;
    const float textScale = m.textScale, inputScale = m.inputScale;

    // At the top of the screen, or at the bottom below the render debug panel while it is up.
    const bool atBottom = g_panelShown != 0;
    int height = std::max(minimum, static_cast<int>(g_screenHeight * g_settings.height));
    if (atBottom) height = std::max(minimum, std::min(height, g_screenHeight - static_cast<int>(PanelBottom()) - padding));
    height = std::min(height, g_screenHeight);
    const int top = atBottom ? g_screenHeight - static_cast<int>(height * g_shown)
                             : static_cast<int>(height * g_shown) - height;

    // Top to bottom: the output, the build stamp's row, the typed line, the edge (at the top instead
    // while at the bottom of the screen).
    const int inputTop = top + height - (atBottom ? 0 : m.edge) - m.inputHeight;
    const int stampTop = inputTop - stampRow;
    DrawRectangle(0, top, width, height, g_background);
    DrawRectangle(0, inputTop, width, m.inputHeight, Brighter(g_background, 22, 225));
    DrawRectangle(0, inputTop - 1, width, 1, Brighter(g_background, 60, 255));
    DrawRectangle(0, atBottom ? top : top + height - m.edge, width, m.edge, kAccentColour);

    // The mouse: where it is, and the scroll bar or autoscroll it moves (from last frame's layout).
    POINT mouse;
    const bool mouseIn = g_consoleOpen && MouseNow(mouse);
    FollowMouse(mouseIn ? &mouse : nullptr, m.screenScale);

    // The output, from the newest line up (or from where it is scrolled back to).
    const int outputTop = top + (atBottom ? m.edge : 0) + padding;
    const int outputBottom = stampTop - (stamped ? 0 : padding);
    const int rows = std::max(1, (outputBottom - outputTop) / rowHeight);
    std::vector<Line> lines, oldest;
    int total, back;
    {
        std::lock_guard<std::mutex> lock(g_historyLock);
        total = static_cast<int>(g_history.size());
        back = std::min(g_back, std::max(0, total - 1));
        const int bottomLine = total - 1 - back;
        for (int i = std::max(0, bottomLine - rows + 1); i <= bottomLine; ++i) lines.push_back(g_history[i]);
        for (int i = 0; i < std::min(total, rows); ++i) oldest.push_back(g_history[i]);
    }
    const int outputRows = back > 0 ? rows - 1 : rows;  // scrolled back: the last row says so
    int y = outputBottom - (rows - outputRows) * rowHeight;
    int rowsLeft = outputRows;
    for (auto line = lines.rbegin(); line != lines.rend() && rowsLeft > 0; ++line) {
        const std::vector<std::string> wrapped = Wrap(Shown(*line), m.textWidth);
        for (auto row = wrapped.rbegin(); row != wrapped.rend() && rowsLeft > 0; ++row, --rowsLeft) {
            y -= rowHeight;
            DrawString(static_cast<float>(left), static_cast<float>(y), row->c_str(), textScale, LineColour(line->kind));
        }
    }

    // How far back it can go: until the oldest line is at the top (with the row of ^ marks).
    int maxBack = 0, rowsUsed = 0;
    for (int i = 0; i < static_cast<int>(oldest.size()); ++i) {
        rowsUsed += static_cast<int>(Wrap(Shown(oldest[i]), m.textWidth).size());
        if (rowsUsed >= rows - 1) {
            maxBack = total - 1 - i;
            break;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_historyLock);
        g_maxBack = maxBack;
        g_back = std::min(g_back, maxBack);
        g_pageLines = outputRows;
    }

    if (back > 0) {  // Quake 3's row of ^ marks under the text, and how far back it is
        char where[96];
        snprintf(where, sizeof(where), "%d of %d lines back - End: newest", back, total);
        const float rowY = static_cast<float>(outputBottom - rowHeight);
        const int whereX = width - 3 * m.scrollBarWidth - static_cast<int>(TextWidth(where) * textScale);
        DrawString(static_cast<float>(whereX), rowY, where, textScale, kDimColour);
        const int step = std::max(8, static_cast<int>(TextWidth("^   ") * textScale));
        for (int x = left; x + step < whereX; x += step) DrawString(static_cast<float>(x), rowY, "^", textScale, kAccentColour);
    }

    // The scroll bar: where the view is in the history. Brighter under the mouse and while held.
    Drawn drawn = {};
    drawn.shown = true;
    drawn.top = std::max(0, top);
    drawn.bottom = top + height;
    if (maxBack > 0) {
        const int trackHeight = outputBottom - outputTop;
        const int thumb = std::max(2 * rowHeight, trackHeight * outputRows / std::max(total, 1));
        const int thumbBottom = outputBottom - (trackHeight - thumb) * std::min(back, maxBack) / maxBack;
        const int x = width - 2 * m.scrollBarWidth;
        drawn.scrollBar = true;
        drawn.barX = x;
        drawn.barWidth = m.scrollBarWidth;
        drawn.trackTop = outputTop;
        drawn.trackBottom = outputBottom;
        drawn.thumbTop = thumbBottom - thumb;
        drawn.thumbBottom = thumbBottom;
        drawn.maxBack = maxBack;
        const bool lit = g_dragging || (mouseIn && mouse.x >= x - 2 * m.scrollBarWidth && mouse.y >= outputTop &&
                                        mouse.y < outputBottom);
        DrawRectangle(x, outputTop, m.scrollBarWidth, trackHeight, Brighter(g_background, lit ? 45 : 30, 160));
        DrawRectangle(x, thumbBottom - thumb, m.scrollBarWidth, thumb, lit ? Brighter(kAccentColour, 70, 255) : kAccentColour);
    }

    if (stamped) {  // right-aligned, as far left as the text if the screen is narrow
        const int stampWidth = static_cast<int>(TextWidth(g_buildStamp.c_str()) * stampScale);
        const int x = std::max(left, width - 2 * m.scrollBarWidth - stampWidth);
        DrawString(static_cast<float>(x), static_cast<float>(stampTop + m.padding / 2), g_buildStamp.c_str(), stampScale,
                   kStampColour);
    }

    // The line being typed: "]", then the text, moved left if needed to keep the caret in view.
    const float typedY = static_cast<float>(inputTop + padding);
    const int promptWidth = static_cast<int>(TextWidth("] ") * inputScale);
    DrawString(static_cast<float>(left), typedY, "]", inputScale, kAccentColour);
    const char* typed = g_typed;
    const int length = static_cast<int>(strnlen(typed, sizeof(g_typed)));
    const int caret = std::clamp(g_caret, 0, length);
    const float room = static_cast<float>(width - left - promptWidth - 3 * m.scrollBarWidth) / inputScale;
    int first = 0;
    while (first < caret && TextWidth(typed + first, caret - first) > room) ++first;
    const int typedX = left + promptWidth;
    const int fits = CharactersThatFit(typed + first, length - first, room);  // not past the scroll bar
    DrawString(static_cast<float>(typedX), typedY, std::string(typed + first, fits).c_str(), inputScale, kTypedColour);
    if (g_caretShown) {
        const int x = typedX + static_cast<int>(TextWidth(typed + first, caret - first) * inputScale);
        DrawRectangle(x, static_cast<int>(typedY), std::max(2, static_cast<int>(2 * inputScale)), m.typedHeight, kCaretColour);
    }
    drawn.inputTop = inputTop;
    drawn.inputBottom = inputTop + m.inputHeight;
    drawn.typedX = typedX;
    drawn.first = first;
    drawn.inputScale = inputScale;
    g_drawn = drawn;

    // The mouse on top of it all, over the console (menus and the render debug panel draw their own).
    if (g_autoScroll) DrawAnchor(m);
    if (mouseIn && (OnConsole(mouse) || g_dragging || g_autoScroll)) DrawCursor(mouse.x, mouse.y, m.screenScale);
}

bool ConsoleCanDraw() { return g_consoleReady && !g_textMode; }  // what 0xC7D540 checks before drawing it

// In place of the game's console drawing (0xC7B890), each frame from 0xC7D540.
void __cdecl ConsoleDraw() {
    if (g_drawnUnderPanel) {  // drawn already this frame, under the panel
        g_drawnUnderPanel = false;
        return;
    }
    DrawConsole();
}

using PanelDrawFn = void(__cdecl*)();
PanelDrawFn g_panelDraw;

void __cdecl PanelDraw() {
    if (g_panelShown && ConsoleCanDraw()) {
        DrawConsole();
        g_drawnUnderPanel = true;
    }
    g_panelDraw();
}

// ---------------------------------------------------------------------------------------------
// console.ini

// The game's own console functions and data in the publicbeta exe.
constexpr DWORD kRunConsoleCommand = 0xC7BDB0;  // int(const char* format, ...): what Enter in the console does
constexpr DWORD kRunScriptFile = 0xC7E080;      // int(const char* file): runs the game's own script files
constexpr size_t kRunScriptFilePrologue = 10;   // push ebp / mov ebp,esp / push -1 / push <handler table>
constexpr DWORD kGameplayStart = 0x540840;      // void(): the game's "gameplay starts" step after loading
constexpr size_t kGameplayStartPrologue = 9;    // push ebp / mov ebp,esp / sub esp,370h
constexpr size_t kMaxCommandLength = 200;       // the game formats each command into a 256-byte buffer

auto const RunConsoleCommand = reinterpret_cast<int(__cdecl*)(const char*, ...)>(kRunConsoleCommand);

// One entry of the game's console command list (registered by 0xC7BD70).
struct ConsoleCommand {
    const char* name;
    const char* description;
    void* handler;
    int type;  // see CommandType
};
enum CommandType { kOnOff = 0, kWholeNumber = 1, kNumber = 2, kCommand = 3, kVector = 4 };

auto& g_commands = *reinterpret_cast<ConsoleCommand* (*)[0xD48]>(0x28FBA88);  // sorted by name
auto& g_commandCount = *reinterpret_cast<int*>(0x28FF1C4);

const ConsoleCommand* FindCommand(const std::string& name) {
    for (int i = 0; i < g_commandCount; ++i)
        if (g_commands[i] && _stricmp(g_commands[i]->name, name.c_str()) == 0) return g_commands[i];
    return nullptr;
}

std::string Trim(const std::string& text) {
    const size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return text.substr(start, text.find_last_not_of(" \t\r\n") - start + 1);
}

// What to type after the name: 1 / 0 become on / off where the game expects on / off.
std::string Argument(const ConsoleCommand& command, const std::string& value) {
    if (command.type == kOnOff || command.type == kCommand) {
        if (value == "1") return "on";
        if (value == "0") return "off";
    }
    return value;
}

// Applies every setting in one [section] of bin\console.ini, in the order written.
void ApplySection(const char* section) {
    const std::wstring path = rfa::RootDir() + L"\\console.ini";
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"r") != 0 || !file) return;
    std::string current;
    int count = 0;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), file)) {
        const std::string line = Trim(buffer);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            current = Trim(line.substr(1, line.find(']') - 1));
            continue;
        }
        if (_stricmp(current.c_str(), section) != 0) continue;
        const size_t equals = line.find('=');
        const std::string name = Trim(line.substr(0, equals));
        const std::string value = equals == std::string::npos ? "" : Trim(line.substr(equals + 1));
        const ConsoleCommand* command = FindCommand(name);
        if (!command) {
            rfa::Log("console.ini [%s]: the console has no command named %s; skipped", section, name.c_str());
            continue;
        }
        const std::string argument = Argument(*command, value);
        const std::string typed = argument.empty() ? command->name : std::string(command->name) + " " + argument;
        if (typed.size() > kMaxCommandLength) {
            rfa::Log("console.ini [%s]: %s is longer than %u characters; skipped", section, name.c_str(),
                     static_cast<unsigned>(kMaxCommandLength));
            continue;
        }
        rfa::Log("console.ini [%s]: %s", section, typed.c_str());
        RunConsoleCommand("%s", typed.c_str());
        ++count;
    }
    fclose(file);
    if (count) rfa::Log("console.ini [%s]: applied %d setting(s)", section, count);
}

using RunScriptFileFn = int(__cdecl*)(const char*);
RunScriptFileFn g_runScriptFile;

// The game runs gamestart.txt once it has finished starting up; [Startup] follows it.
int __cdecl RunScriptFile(const char* file) {
    const int result = g_runScriptFile(file);
    if (file && _stricmp(file, "gamestart.txt") == 0) ApplySection("Startup");
    return result;
}

using GameplayStartFn = void(__cdecl*)();
GameplayStartFn g_gameplayStart;

void __cdecl GameplayStart() {
    g_gameplayStart();
    ApplySection("Gameplay");
}

// ---------------------------------------------------------------------------------------------
// The level editor's link
//
// The game shows the editor what it shows, every frame, in a small block of shared memory named
// "Local\RFAModEditorLink" (see SharedView, and GameLink.cs in the editor, which reads it while
// it follows the game). It holds the object picked in the game (the console's debug target: Q in
// slew mode, or target_by_handle), where the camera is, which way it looks and its field of view.
//
// The editor sends console commands back through a file, bin\link\to_game.txt (its "Show in
// game": set_camera_pos ..., target_by_handle ...), checked ten times a second while the folder
// bin\link exists (the editor makes it). Each line is run as if typed into the console, then the
// file is deleted. A file more than 30 seconds old (sent while the game wasn't running) is
// deleted without running it. The editor can also ask where objects are now (see
// AnswerWhereWanted).

constexpr DWORD kFrameUpdate = 0x544CB0;      // void(): called once a frame by the main loop (0xA44B90)
constexpr size_t kFrameUpdatePrologue = 6;    // push ebp / mov ebp,esp / sub esp,30h
constexpr ULONGLONG kCheckEveryMs = 100;
constexpr ULONGLONG kOldCommandsSeconds = 30;

auto& g_debugTarget = *reinterpret_cast<DWORD*>(0x10F78FC);           // the picked object's handle (set by target_by_handle 0x4D4E50)
auto& g_cameraPosition = *reinterpret_cast<float(*)[3]>(0x11B9C6C);   // camera manager (0x11B9C40) +0x2C, set by set_camera_pos
auto& g_cameraRotation = *reinterpret_cast<float(*)[9]>(0x11B9C90);   // +0x50: rows right, up, forward
auto& g_cameraFov = *reinterpret_cast<float*>(0x11B9D00);             // +0xC0: the FOV the frame is drawn with, vertical degrees

// The shared memory's layout, version 1 (80 bytes). While the game writes it, `sequence` is odd;
// the editor reads `sequence`, the rest, then `sequence` again, and reads again if it changed or
// was odd, so it never sees half of one frame and half of another.
struct SharedView {
    uint32_t magic;              // 'RFAL'
    uint32_t version;            // 1
    volatile LONG sequence;      // odd while being written
    uint32_t frame;              // counts the frames written
    ULONGLONG written;           // when, as a Windows file time (UTC): the editor can tell the game is running
    uint32_t picked;             // the picked object's handle
    float position[3];           // the camera, in world coordinates
    float rotation[9];           // its rows: right, up, forward
    float fov;                   // vertical degrees
};
static_assert(sizeof(SharedView) == 80, "the editor reads this layout");
constexpr wchar_t kSharedName[] = L"Local\\RFAModEditorLink";
constexpr uint32_t kSharedMagic = 0x4C414652;  // "RFAL"

SharedView* g_shared = nullptr;
DWORD g_lastPicked = 0;
ULONGLONG g_lastCheck = 0;

std::wstring LinkFolder() { return rfa::RootDir() + L"\\link"; }

// Makes the shared memory (or opens it, if an editor still has it from an earlier run).
void ShareWithEditor() {
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedView), kSharedName);
    if (!mapping) {
        rfa::Log("Editor link: could not make the shared memory (error %lu); the editor can't follow the game", GetLastError());
        return;
    }
    g_shared = static_cast<SharedView*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(SharedView)));
    if (!g_shared) return;
    g_shared->magic = kSharedMagic;
    g_shared->version = 1;
}

// Puts this frame's view into the shared memory.
void ShowEditor() {
    if (!g_shared) return;
    InterlockedIncrement(&g_shared->sequence);  // odd: being written
    g_shared->frame++;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    g_shared->written = (static_cast<ULONGLONG>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    g_shared->picked = g_debugTarget;
    for (int i = 0; i < 3; ++i) g_shared->position[i] = g_cameraPosition[i];
    for (int i = 0; i < 9; ++i) g_shared->rotation[i] = g_cameraRotation[i];
    g_shared->fov = g_cameraFov;
    InterlockedIncrement(&g_shared->sequence);  // even: done
    if (g_debugTarget != g_lastPicked) {
        g_lastPicked = g_debugTarget;
        rfa::Log("Editor link: picked 0x%08X", static_cast<unsigned>(g_lastPicked));
    }
}

// Runs the commands the editor sent, if any, and deletes them.
void RunEditorCommands() {
    const std::wstring path = LinkFolder() + L"\\to_game.txt";
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) return;
    FILETIME nowTime;
    GetSystemTimeAsFileTime(&nowTime);
    ULARGE_INTEGER now, written;
    now.LowPart = nowTime.dwLowDateTime;
    now.HighPart = nowTime.dwHighDateTime;
    written.LowPart = info.ftLastWriteTime.dwLowDateTime;
    written.HighPart = info.ftLastWriteTime.dwHighDateTime;
    const ULONGLONG ageSeconds = now.QuadPart > written.QuadPart ? (now.QuadPart - written.QuadPart) / 10000000 : 0;

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"r") != 0 || !file) return;  // try again next time
    std::vector<std::string> lines;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), file)) lines.push_back(Trim(buffer));
    fclose(file);
    DeleteFileW(path.c_str());

    if (ageSeconds > kOldCommandsSeconds) {
        rfa::Log("Editor link: skipped commands the editor sent %u seconds ago", static_cast<unsigned>(ageSeconds));
        return;
    }
    for (const std::string& line : lines) {
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.size() > kMaxCommandLength) {
            rfa::Log("Editor link: a command longer than %u characters was skipped", static_cast<unsigned>(kMaxCommandLength));
            continue;
        }
        rfa::Log("Editor link: %s", line.c_str());
        RunConsoleCommand("%s", line.c_str());
    }
}

// Where objects are now, for the editor's "props as in the game". Clutter (general_mover: debris,
// barrels, crates, wreck pieces) is moved by physics: it falls and settles when the level starts,
// so the game shows it elsewhere than the zone file puts it.
//
// The editor writes bin\link\where_wanted.txt: the handles of the objects it wants, one per line
// (0x... hex). The game answers in bin\link\where_now.txt, a line per handle: the handle, then the
// object's position (3 numbers) and rotation rows right, up, forward (9 numbers) as it is this
// frame, or "gone" if the game has no such object now (not loaded, or broken up).

constexpr DWORD kObjectList = 0x1870630;  // the game's objects (the debug panel 0x6698C0 and target_by_handle use it)
constexpr DWORD kFindObject = 0x6CB9E0;   // object* (list, handle), a method of the list: null if none, or being removed

// The game's method takes the list in ECX and the handle on the stack, and clears the stack itself.
// __fastcall does the same with its first argument in ECX, its second in EDX (unused here) and
// the rest on the stack, so the method can be called as this.
using FindObjectFn = void*(__fastcall*)(void* list, void* unused, DWORD handle);
auto const FindObject = reinterpret_cast<FindObjectFn>(kFindObject);

// The start of an object as the game keeps it, as in Red Faction: Guerrilla: its class's function
// table, then its position, then its rotation (rows right, up, forward). The debug panel reads the
// position here for "CLOE pos" (0x66ABBC).
struct GameObject {
    void* functions;
    float position[3];
    float rotation[9];
};

void AnswerWhereWanted() {
    const std::wstring wanted = LinkFolder() + L"\\where_wanted.txt";
    FILE* file = nullptr;
    if (_wfopen_s(&file, wanted.c_str(), L"r") != 0 || !file) return;
    std::vector<DWORD> handles;
    char buffer[64];
    while (fgets(buffer, sizeof(buffer), file)) {
        const std::string line = Trim(buffer);
        if (!line.empty()) handles.push_back(static_cast<DWORD>(strtoul(line.c_str(), nullptr, 0)));
    }
    fclose(file);
    DeleteFileW(wanted.c_str());

    // Written under another name, then renamed, so the editor never reads half of it.
    const std::wstring answer = LinkFolder() + L"\\where_now.txt", partial = answer + L".tmp";
    if (_wfopen_s(&file, partial.c_str(), L"w") != 0 || !file) return;
    int found = 0;
    for (DWORD handle : handles) {
        auto* object = static_cast<const GameObject*>(FindObject(reinterpret_cast<void*>(kObjectList), nullptr, handle));
        if (!object) {
            fprintf(file, "0x%08X gone\n", static_cast<unsigned>(handle));
            continue;
        }
        ++found;
        fprintf(file, "0x%08X", static_cast<unsigned>(handle));
        for (float value : object->position) fprintf(file, " %.4f", value);
        for (float value : object->rotation) fprintf(file, " %.5f", value);
        fprintf(file, "\n");
    }
    fclose(file);
    MoveFileExW(partial.c_str(), answer.c_str(), MOVEFILE_REPLACE_EXISTING);
    static bool told = false;
    if (!told) {
        told = true;
        rfa::Log("Editor link: the editor asked where %u objects are (%d found); it asks again while it shows props as in the game",
                 static_cast<unsigned>(handles.size()), found);
    }
}

// The game's light, for calibrating the editor's "game light". The editor writes
// bin\link\lighting_wanted.txt; the game answers in bin\link\lighting_now.txt, a line each:
//   time <t>                     the time of day the game lights by (0x10F8898; the .dtodx curves' time)
//   tod_enabled <0/1>            whether the time of day is in use (0x12FD6D4)
//   tod_weights <a> <b> <c> <d>  how much each of the four loaded time-of-day sets counts
//   exposure <min> <max>         the tone-mapping stage's exposure this frame (rl_hdr's Exposure_min, _max)
//   desired_brightness <x>       and the brightness it adapts towards (Desired_brightness)
//   world_ambient, world_back_ambient <r g b>   the world's ambient from above and below
//                                (0x1870630, 0x187063C), which the time of day sets (0x768520)
//   render_ambient, render_back_ambient <r g b a>   what the renderer's lighting manager then
//                                holds (its +0x12C and +0x13C, set by 0x769BD0 and 0x769C00)
//   render_fog <11 numbers>      the block the lighting manager is handed each frame (0x1973924, its +0x100)
//   then each time-of-day curve below, as the game works it out now: every set's curve at the
//   time of day, by the game's own functions, times the set's weight, added up. Colours are
//   red, green, blue and a brightness, as in the files.
// The time-of-day sets are the level's .dtodx files (world_mission_01.dtodx, ...): curves over
// the day for the ambient, fog, sky and tone mapping, read by 0x558EE0 into a set (curve offsets below).

auto& g_timeOfDay = *reinterpret_cast<float*>(0x10F8898);
auto& g_worldAmbient = *reinterpret_cast<float(*)[6]>(0x1870630);  // from above (3 numbers), then from below (3)
auto& g_lighting = *reinterpret_cast<const BYTE**>(0x19737E0);     // the renderer's lighting manager (fed each frame by 0x768DF0)
auto& g_renderFog = *reinterpret_cast<float(*)[11]>(0x1973924);     // the block it is handed each frame (its +0x100)
auto& g_todEnabled = *reinterpret_cast<BYTE*>(0x12FD6D4);
auto& g_todSets = *reinterpret_cast<BYTE* (*)[4]>(0x12FD6B4);   // the sets d, c, b, a (0x12FD6C0 is the first)
auto& g_todWeights = *reinterpret_cast<float(*)[4]>(0x12FD6C4);  // their weights, in the same order

// The game's curve functions, methods of a curve (in ECX), called as __fastcall (EDX unused):
// a colour curve's value at a time into `out` (0x531B90, as tod_ambient's handler 0x52BE90 uses it),
// and a number curve's (0x531CB0, as tod_fog_density's 0x52C9F0 does).
using ColourAtFn = float*(__fastcall*)(void* curve, void* unused, float* out, float time);
using NumberAtFn = float(__fastcall*)(void* curve, void* unused, float time);
auto const ColourAt = reinterpret_cast<ColourAtFn>(0x531B90);
auto const NumberAt = reinterpret_cast<NumberAtFn>(0x531CB0);

struct TodCurve {
    const char* name;
    size_t offset;  // in a set
    bool colour;
};
constexpr TodCurve kTodCurves[] = {
    {"ambient_color", 0x2080, true},     {"back_ambient_color", 0x23C0, true}, {"tod_light_color", 0x1D40, true},
    {"fog_color", 0x2700, true},         {"particle_ambient_color", 0x2A40, true},
    {"fog_density_new", 0x32A8, false},  {"fog_density_offset", 0x339C, false}, {"fog_atmosphere_scale", 0x31B4, false},
    {"fog_ground", 0x30C0, false},       {"fog_camera_min", 0x6E04, false},    {"fog_camera_max", 0x6EF8, false},
    {"fog_skybox_scale", 0x70E0, false}, {"fog_skybox_offset", 0x6FEC, false},
};

// The renderer's tone-mapping step (rl_hdr), a method called once a frame: the renderer in ECX.
// It hands the shader the renderer's exposure: +0xAD8 Desired_brightness, +0xAE0 Exposure_min,
// +0xAE4 Exposure_max (looked up by name at 0xD22720). Hooked only to learn where the renderer is.
constexpr DWORD kToneMapStep = 0xD22C60;
constexpr size_t kToneMapStepPrologue = 6;  // push ebp / mov ebp,esp / sub esp,50h
using ToneMapStepFn = void(__fastcall*)(void* renderer, void* unused);
ToneMapStepFn g_toneMapStep;
const BYTE* g_renderer = nullptr;

void __fastcall ToneMapStep(void* renderer, void* unused) {
    g_renderer = static_cast<const BYTE*>(renderer);
    g_toneMapStep(renderer, unused);
}

float RendererFloat(size_t offset) { return g_renderer ? *reinterpret_cast<const float*>(g_renderer + offset) : -1.0f; }

void AnswerLightingWanted() {
    const std::wstring wanted = LinkFolder() + L"\\lighting_wanted.txt";
    if (GetFileAttributesW(wanted.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    DeleteFileW(wanted.c_str());
    const std::wstring answer = LinkFolder() + L"\\lighting_now.txt", partial = answer + L".tmp";
    FILE* file = nullptr;
    if (_wfopen_s(&file, partial.c_str(), L"w") != 0 || !file) return;
    fprintf(file, "time %.4f\n", g_timeOfDay);
    fprintf(file, "tod_enabled %d\n", g_todEnabled ? 1 : 0);
    fprintf(file, "tod_weights %.4f %.4f %.4f %.4f\n", g_todWeights[3], g_todWeights[2], g_todWeights[1], g_todWeights[0]);
    fprintf(file, "exposure %.4f %.4f\n", RendererFloat(0xAE0), RendererFloat(0xAE4));
    fprintf(file, "desired_brightness %.4f\n", RendererFloat(0xAD8));
    fprintf(file, "world_ambient %.5f %.5f %.5f\n", g_worldAmbient[0], g_worldAmbient[1], g_worldAmbient[2]);
    fprintf(file, "world_back_ambient %.5f %.5f %.5f\n", g_worldAmbient[3], g_worldAmbient[4], g_worldAmbient[5]);
    if (g_lighting) {
        const float* ambient = reinterpret_cast<const float*>(g_lighting + 0x12C);
        const float* back = reinterpret_cast<const float*>(g_lighting + 0x13C);
        fprintf(file, "render_ambient %.5f %.5f %.5f %.5f\n", ambient[0], ambient[1], ambient[2], ambient[3]);
        fprintf(file, "render_back_ambient %.5f %.5f %.5f %.5f\n", back[0], back[1], back[2], back[3]);
    }
    fprintf(file, "render_fog");
    for (float value : g_renderFog) fprintf(file, " %.5f", value);
    fprintf(file, "\n");
    for (const TodCurve& curve : kTodCurves) {
        float total[4] = {0, 0, 0, 0};
        for (int set = 0; set < 4; ++set) {
            if (!g_todSets[set] || g_todWeights[set] == 0) continue;
            void* inSet = g_todSets[set] + curve.offset;
            if (curve.colour) {
                float value[4] = {0, 0, 0, 0};
                ColourAt(inSet, nullptr, value, g_timeOfDay);
                for (int i = 0; i < 4; ++i) total[i] += value[i] * g_todWeights[set];
            } else {
                total[0] += NumberAt(inSet, nullptr, g_timeOfDay) * g_todWeights[set];
            }
        }
        if (curve.colour) fprintf(file, "%s %.5f %.5f %.5f %.5f\n", curve.name, total[0], total[1], total[2], total[3]);
        else fprintf(file, "%s %.6f\n", curve.name, total[0]);
    }
    fclose(file);
    MoveFileExW(partial.c_str(), answer.c_str(), MOVEFILE_REPLACE_EXISTING);
    rfa::Log("Editor link: told the editor the game's light (time %.2f, exposure %.2f to %.2f)", g_timeOfDay,
             RendererFloat(0xAE0), RendererFloat(0xAE4));
}

using FrameUpdateFn = void(__cdecl*)();
FrameUpdateFn g_frameUpdate;

void __cdecl FrameUpdate() {
    g_frameUpdate();
    ShowEditor();
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastCheck < kCheckEveryMs) return;
    g_lastCheck = now;
    if (GetFileAttributesW(LinkFolder().c_str()) == INVALID_FILE_ATTRIBUTES) return;  // the editor's link isn't set up
    RunEditorCommands();
    AnswerWhereWanted();
    AnswerLightingWanted();
}

}  // namespace

extern "C" __declspec(dllexport) void __cdecl RFAMod_Init() {
    if (!rfa::game::IsSupportedBuild()) {
        rfa::Log("Not the publicbeta build (it has no developer console); nothing to do");
        return;
    }
    ReadSettings();
    MakeBuildStamp();
    rfa::Log("Build stamp: %s", g_buildStamp.c_str());
    g_consolePrint =reinterpret_cast<ConsolePrintFn>(
        rfa::HookFunction(kConsolePrint, kConsolePrintPrologue, reinterpret_cast<void*>(&ConsolePrint)));
    g_consoleEcho = reinterpret_cast<ConsoleEchoFn>(
        rfa::HookFunction(kConsoleEcho, kConsoleEchoPrologue, reinterpret_cast<void*>(&ConsoleEcho)));
    g_drawsConsole = g_consolePrint && g_consoleEcho && rfa::ReplaceFunction(kConsoleDraw, reinterpret_cast<void*>(&ConsoleDraw));
    if (g_drawsConsole) {
        rfa::Log("The mod draws the console: %d%% of the screen, text x%.2f, typed line x%.2f, %u lines kept%s",
                 static_cast<int>(g_settings.height * 100 + 0.5f), g_settings.textScale, g_settings.inputScale,
                 static_cast<unsigned>(g_settings.historyLines), g_settings.logToFile ? ", also in logs\\console.log" : "");
        g_panelDraw = reinterpret_cast<PanelDrawFn>(
            rfa::HookFunction(kPanelDraw, kPanelDrawPrologue, reinterpret_cast<void*>(&PanelDraw)));
        if (!g_panelDraw) rfa::Log("Could not hook the render debug panel's drawing; the console stays on top of it");
    } else {
        rfa::Log("Could not hook the console's output or drawing; the game draws it as before");
    }

    g_consoleKeys = reinterpret_cast<VoidFn>(
        rfa::HookFunction(kConsoleKeys, kConsoleKeysPrologue, reinterpret_cast<void*>(&ConsoleKeys)));
    g_nextKey = reinterpret_cast<NextKeyFn>(
        rfa::HookFunction(kNextKey, kNextKeyPrologue, reinterpret_cast<void*>(&NextKey)));
    g_mouseMessage = reinterpret_cast<MouseMessageFn>(
        rfa::HookFunction(kMouseMessage, kMouseMessagePrologue, reinterpret_cast<void*>(&MouseMessage)));
    rfa::Log(g_consoleKeys && g_nextKey && g_mouseMessage
                 ? "The mouse wheel, Page Up / Page Down, Ctrl+Home and End scroll the console (open it with the ` key)"
                 : "Could not hook the game's keys or mouse handler; console scrolling may be off");

    g_runScriptFile = reinterpret_cast<RunScriptFileFn>(
        rfa::HookFunction(kRunScriptFile, kRunScriptFilePrologue, reinterpret_cast<void*>(&RunScriptFile)));
    g_gameplayStart = reinterpret_cast<GameplayStartFn>(
        rfa::HookFunction(kGameplayStart, kGameplayStartPrologue, reinterpret_cast<void*>(&GameplayStart)));
    rfa::Log(g_runScriptFile && g_gameplayStart
                 ? "console.ini: [Startup] applies at startup, [Gameplay] every time gameplay starts"
                 : "Could not hook the game's startup / gameplay points; console.ini is off");

    ShareWithEditor();
    g_frameUpdate = reinterpret_cast<FrameUpdateFn>(
        rfa::HookFunction(kFrameUpdate, kFrameUpdatePrologue, reinterpret_cast<void*>(&FrameUpdate)));
    rfa::Log(g_frameUpdate ? "Editor link: the camera is shared every frame, and bin\\link is watched for the editor's commands"
                           : "Could not hook the game's frame update; the editor link is off");
    g_toneMapStep = reinterpret_cast<ToneMapStepFn>(
        rfa::HookFunction(kToneMapStep, kToneMapStepPrologue, reinterpret_cast<void*>(&ToneMapStep)));
    if (!g_toneMapStep) rfa::Log("Could not hook the renderer's tone mapping; the editor won't be told the game's exposure");
}
