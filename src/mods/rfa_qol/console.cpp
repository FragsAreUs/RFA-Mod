// Improvements to the developer console of the publicbeta build (opened with the ` key). A part of
// rfa_qol.dll.
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
// What the console shows besides its own output (see "What the game keeps to itself" below):
//   - the game's hidden output, marked [game]: its debug output, which only a debugger saw, and the
//     error reports it flashes on the screen for a moment;
//   - RFA-Mod's own log (logs\rfa_mods.log) as it grows, each line marked with the module's name.
//
// Typing (see "Typing" below): Ctrl+V or Shift+Insert pastes, cleaned up into something the console
// can run; several lines are run one at a time, each with Enter. Tab completes only the command's
// name, lists the matches with what each does, and with arguments typed says what the command is.
// A % in a line you run is now taken as it is (the game read it as a format).
//
// The render debug panel (` + X): it takes the mouse only while the console is open (the game frees
// the mouse for the console). While the panel is up, the console moves to the bottom of the screen,
// below the panel's windows, and is drawn under them, so its text no longer covers them.
//
// [Console] in rfa_mods.ini: its height, text sizes, darkness, history length, the log file, the
// notify lines, the build stamp, and whether it shows the game's hidden output and RFA-Mod's log.
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
#include "rfa_qol.h"
#include "rfa_menu.h"
#include <algorithm>
#include <climits>
#include <ctime>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// FrameUpdate callback registration variables (visible to both anonymous namespace and global console namespace)
namespace console {
using FrameUpdateCallback = void(__cdecl*)();
static std::vector<FrameUpdateCallback> g_frameUpdateCallbacks;
static std::mutex g_frameUpdateCallbacksMutex;
// Console commands the game's menus asked for (rfa_console_command), run at the start of the next frame.
static std::vector<std::string> g_menuCommands;
static std::mutex g_menuCommandsMutex;
}  // namespace console

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
    bool gameLog = true;          // also show the game's hidden output: its debug output and error reports
    bool modLog = true;           // also show logs\rfa_mods.log
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
    g_settings.gameLog = rfa::IniInt(L"Console", L"GameLog", 1) != 0;
    g_settings.modLog = rfa::IniInt(L"Console", L"ModLog", 1) != 0;
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
auto& g_typed = *reinterpret_cast<char(*)[0x400]>(0x28FB640);     // the line being typed (up to 124 characters)
auto& g_prompted = *reinterpret_cast<char(*)[0x80]>(0x29075F8);   // "] " and that line: what Enter echoes
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

enum class Kind : BYTE {
    Output,   // what the console printed
    Command,  // what you typed, as Enter echoes it
    Error,
    Warning,
    Info,     // the mod's own messages in the console (pasting, Tab): drawn as output, never taken for errors
    Hidden,   // the game's hidden output
    Mod,      // RFA-Mod's log
};

struct Line {
    std::string text;
    Kind kind;
    bool fromConsole;  // the console's own output (only these are notify lines)
    int repeats;       // printed this many times in a row
    ULONGLONG printed; // when it was last printed (GetTickCount64), for the notify lines
};

constexpr Colour kOutputColour = {225, 225, 225, 255};
constexpr Colour kCommandColour = {255, 196, 80, 255};
constexpr Colour kErrorColour = {255, 112, 96, 255};
constexpr Colour kWarningColour = {240, 205, 100, 255};
constexpr Colour kHiddenColour = {150, 172, 205, 255};
constexpr Colour kModColour = {110, 205, 185, 255};
constexpr Colour kTypedColour = {255, 255, 255, 255};
constexpr Colour kAccentColour = {90, 170, 255, 255};  // the prompt, the edge line and the scroll bar
constexpr Colour kDimColour = {160, 160, 160, 255};
constexpr Colour kCaretColour = {255, 196, 80, 255};

Colour LineColour(Kind kind) {
    switch (kind) {
    case Kind::Command: return kCommandColour;
    case Kind::Error: return kErrorColour;
    case Kind::Warning: return kWarningColour;
    case Kind::Hidden: return kHiddenColour;
    case Kind::Mod: return kModColour;
    default: return kOutputColour;
    }
}

// The game prints from other threads too (loading), so the history is only touched under this lock.
std::mutex g_historyLock;
std::deque<Line> g_history;
int g_back = 0;      // how many lines the bottom of the view is above the newest (0 = following new output)
int g_maxBack = 0;   // the most it can be: the oldest line at the top (worked out as the console is drawn)
int g_pageLines = 1; // lines on screen, for Page Up / Page Down
bool g_drawsConsole = false;  // the mod draws the console, so the keys and the wheel move its history

// The game's help lists commands indented (" name - what it does"), so those are never errors.
bool LooksLikeError(const std::string& text) {
    if (!text.empty() && text[0] == ' ') return false;
    std::string lower = text;
    for (char& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    for (const char* word : {"error", "unknown command", "invalid", "failed"})
        if (lower.find(word) != std::string::npos) return true;
    return false;
}

// ---- The console's files, off the game's thread
// The game's frame never waits on the disk for the console. A thread of the console's own does all of
// its file work every 15 ms: it writes console.log, reads back what the mods wrote to rfa_mods.log
// (the lines the console shows), and watches the editor's link folder (bin\link\to_game.txt, read and
// deleted). The frame only takes what that thread has queued. The user's 18:53 run (2026-11-15) had a
// 5.6 s frame with the main thread in WriteFile on console.log, and 1 s stalls, on a busy 5400 rpm drive.
constexpr DWORD kFilesEveryMs = 15;
constexpr size_t kMostQueuedModLines = 20000;  // when the console stops taking them, the oldest go
std::mutex g_filesLock;
std::vector<std::string> g_consoleLogQueue;  // lines for console.log
std::vector<std::string> g_modLines;         // whole lines read from rfa_mods.log, for the console
volatile ULONGLONG g_modLinesWantedAt = 0;   // when the console last took them (read only while it does)
struct EditorBatch {
    std::vector<std::string> lines;
    ULONGLONG ageSeconds;  // how long ago the editor wrote them
};
std::vector<EditorBatch> g_editorBatches;
void StartConsoleFiles();

void WriteToLog(const std::string& text) {
    if (!g_settings.logToFile) return;
    StartConsoleFiles();
    std::lock_guard<std::mutex> lock(g_filesLock);
    g_consoleLogQueue.push_back(text + "\r\n");
}

void AddLine(std::string text, Kind kind, bool fromConsole = true) {
    std::string clean;  // the font draws no tabs or other control characters
    for (char c : text) {
        if (c == '\t') clean += "    ";
        else if (static_cast<unsigned char>(c) >= 0x20) clean += c;
    }
    if ((kind == Kind::Output || kind == Kind::Hidden || kind == Kind::Mod) && LooksLikeError(clean)) kind = Kind::Error;
    std::lock_guard<std::mutex> lock(g_historyLock);
    if (!clean.empty() && !g_history.empty() && g_history.back().text == clean && g_history.back().kind == kind &&
        g_history.back().fromConsole == fromConsole) {
        ++g_history.back().repeats;  // the same line again: counted, not added
        g_history.back().printed = GetTickCount64();
        return;
    }
    WriteToLog(clean);
    g_history.push_back({std::move(clean), kind, fromConsole, 1, GetTickCount64()});
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
// What the game keeps to itself, and RFA-Mod's log
//
// The game's debug print (0xCA9F10: what it loads, what it couldn't), its failed checks ("<file>(<line>):
// V_ERROR: ...", 0xC7B1D0 through 0xCA9DA0) and one library's errors (0xA6B000) all end in Windows'
// OutputDebugStringA, which only a debugger sees. The mod takes the exe's import of it, so mods that
// hook those functions themselves are left alone.
//
// Its error reports (0x545D40, through 0x545BD0: "GENERAL-ERROR: ...", "ART-WARNING: ...") go into a
// queue of 164 (0x1308780, 256 bytes each, the count at 0x13130D0) that is drawn on the screen for a
// while and then emptied (0x545DD0, 0x545BB0). The mod reads the new ones each frame.
//
// logs\rfa_mods.log is what the launcher, the loader and every mod write (started afresh by the
// launcher each time); the mod reads what has been added to it ten times a second.

// Lines of hidden output: one per '\n', empty ones left out, each marked with where it came from.
void AddHidden(const char* tag, const char* text, Kind kind) {
    const char* end = text + strlen(text);
    while (text < end) {
        const char* newline = strchr(text, '\n');
        if (!newline) newline = end;
        std::string line(text, newline);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) AddLine(tag + line, kind, false);
        text = newline + 1;
    }
}

using OutputDebugStringFn = void(WINAPI*)(LPCSTR);
OutputDebugStringFn g_outputDebugString;

void WINAPI GameDebugString(LPCSTR text) {
    g_outputDebugString(text);
    if (text) AddHidden("[game] ", text, Kind::Hidden);
}

constexpr int kMaxReports = 0xA4;
auto& g_reports = *reinterpret_cast<const char(*)[kMaxReports][0x100]>(0x1308780);
auto& g_reportCount = *reinterpret_cast<const int*>(0x13130D0);
int g_reportsRead = 0;

void ReadReports() {
    const int count = g_reportCount;
    if (count < 0 || count > kMaxReports) return;
    if (count < g_reportsRead) g_reportsRead = 0;  // shown and emptied since
    for (; g_reportsRead < count; ++g_reportsRead) {
        const char* report = g_reports[g_reportsRead];
        const std::string text(report, strnlen(report, sizeof(g_reports[0])));
        AddHidden("[game] ", text.c_str(), text.find("-ERROR:") != std::string::npos ? Kind::Error : Kind::Warning);
    }
}

HANDLE g_modLog = INVALID_HANDLE_VALUE;
LONGLONG g_modLogRead = 0;  // how much of it the console has
std::string g_modLogRest;   // the start of a line still being written

// A line for the file only: a mod's routine line, "[module:log] text" (rfa_mod.h's rfa::LogDetail), or a part's
// start-up line, "[module:info] text" (rfa::LogInfo).
bool IsDetailLine(const std::string& line) {
    if (line.empty() || line[0] != '[') return false;
    const size_t end = line.find(']');
    return end != std::string::npos && ((end >= 4 && line.compare(end - 4, 4, ":log") == 0) ||
                                         (end >= 5 && line.compare(end - 5, 5, ":info") == 0));
}

// "[12:34:56.789] [module] text" without the time, which the console doesn't show for its own lines.
// Only the mods' problems are shown (rfa::Log): their routine and start-up lines are left out.
void AddModLine(std::string line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() > 15 && line[0] == '[' && line[13] == ']' && line[14] == ' ') line.erase(0, 15);
    if (!line.empty() && !IsDetailLine(line)) AddLine(line, Kind::Mod, false);
}

// On the console's files thread: what the mods wrote to rfa_mods.log since the last look, as whole lines.
void ReadModLogLines(std::vector<std::string>& lines) {
    if (g_modLog == INVALID_HANDLE_VALUE) {
        g_modLog = CreateFileW(rfa::LogPath().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g_modLog == INVALID_HANDLE_VALUE) return;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(g_modLog, &size)) return;
    if (size.QuadPart < g_modLogRead) {  // started afresh
        g_modLogRead = 0;
        g_modLogRest.clear();
    }
    while (g_modLogRead < size.QuadPart) {
        char buffer[16384];
        LARGE_INTEGER at;
        at.QuadPart = g_modLogRead;
        DWORD read = 0;
        if (!SetFilePointerEx(g_modLog, at, nullptr, FILE_BEGIN) || !ReadFile(g_modLog, buffer, sizeof(buffer), &read, nullptr) ||
            read == 0)
            return;
        g_modLogRead += read;
        g_modLogRest.append(buffer, read);
        for (size_t newline; (newline = g_modLogRest.find('\n')) != std::string::npos;) {
            lines.push_back(g_modLogRest.substr(0, newline));
            g_modLogRest.erase(0, newline + 1);
        }
    }
}

// On the game's thread: the mods' lines the files thread has read, into the console.
void ReadModLog() {
    StartConsoleFiles();
    g_modLinesWantedAt = GetTickCount64();
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(g_filesLock);
        lines.swap(g_modLines);
    }
    for (std::string& line : lines) AddModLine(std::move(line));
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
constexpr int kEscape = 0x01, kEnter = 0x1C, kNumpadEnter = 0x9C, kV = 0x2F, kInsert = 0xD2;
constexpr int kLinesPerNotch = 3;

// Pasting (see "Typing" below): the pasted lines still to come after the one in the typed line.
std::deque<std::string> g_pasted;
bool g_nextPastedAfterEnter = false;
void Paste();
bool TypeIn(const std::string& text, bool replaceLine);

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

// Also takes Ctrl+V and Shift+Insert (paste), and after Enter has run a pasted line, puts the next
// one in its place.
int __cdecl NextKey() {
    if (!g_readingConsoleKeys || !g_consoleOpen) return g_nextKey();
    if (rfa::menu::Want(RFA_MENUS_KEYBOARD) || rfa::menu::Want(RFA_MENUS_ALONE)) {  // the in-game menus': not the console's
        while (g_nextKey()) {}
        return 0;
    }
    if (g_nextPastedAfterEnter) {  // the game has run the line Enter sent
        g_nextPastedAfterEnter = false;
        if (!g_pasted.empty()) {
            TypeIn(g_pasted.front(), true);
            g_pasted.pop_front();
        }
    }
    for (;;) {
        const int key = g_nextKey();
        const int pressed = key & ~kGraveHeld;
        if (!key) return 0;
        if (pressed == (kV | kCtrl) || pressed == (kInsert | kShift)) {
            Paste();
            continue;
        }
        if (g_drawsConsole && ScrollKey(pressed)) continue;
        if (pressed == kEnter || pressed == kNumpadEnter) g_nextPastedAfterEnter = !g_pasted.empty();
        if (pressed == kEscape) g_pasted.clear();  // the game clears the line; the rest of a paste goes too
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
            if (line->fromConsole) recent.push_back(*line);  // not the logs it also shows
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
    if (g_consoleOpen && rfa::menu::Want(RFA_MENUS_MOUSE)) {  // over one of the in-game menus: theirs
        if (message == WM_LBUTTONUP) g_dragging = false;
        return;
    }
    if (g_consoleOpen && g_drawsConsole) {
        if (message == WM_MOUSEWHEEL) {
            g_autoScroll = false;
            g_wheel += GET_WHEEL_DELTA_WPARAM(wParam);
            for (; g_wheel >= WHEEL_DELTA; g_wheel -= WHEEL_DELTA) Scroll(kLinesPerNotch);
            for (; g_wheel <= -WHEEL_DELTA; g_wheel += WHEEL_DELTA) Scroll(-kLinesPerNotch);
            RFA_LOG_DETAIL_FIRST(3, "Scrolled the console with the mouse wheel");
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
            RFA_LOG_DETAIL_FIRST(3, "The console took a mouse click");
            return;
        }
    } else {
        g_wheel = 0;
    }
    g_mouseMessage(message, wParam, lParam);
}

void DrawConsole() {
    if (rfa::menu::Want(RFA_MENUS_ALONE)) return;  // the in-game menus were opened with their own key
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
    int hintWidth = 0;  // while pasted lines wait, how many, on the right
    if (!g_pasted.empty()) {
        char hint[96];
        snprintf(hint, sizeof(hint), "%u more pasted - Enter: run this, Esc: drop them", static_cast<unsigned>(g_pasted.size()));
        hintWidth = static_cast<int>(TextWidth(hint) * textScale) + 2 * padding;
        const float hintY = inputTop + (m.inputHeight - m.fontHeight * textScale) / 2;
        DrawString(static_cast<float>(width - 2 * m.scrollBarWidth - hintWidth + 2 * padding), hintY, hint, textScale, kDimColour);
    }
    const float room = static_cast<float>(width - left - promptWidth - 3 * m.scrollBarWidth - hintWidth) / inputScale;
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

    // The mouse on top of it all, over the console (menus and the render debug panel draw their own,
    // and so do the in-game menus while they are up).
    if (g_autoScroll) DrawAnchor(m);
    if (mouseIn && (OnConsole(mouse) || g_dragging || g_autoScroll) && !rfa::menu::Want(RFA_MENUS_SHOWN))
        DrawCursor(mouse.x, mouse.y, m.screenScale);
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
        rfa::LogDetail("console.ini [%s]: %s", section, typed.c_str());
        RunConsoleCommand("%s", typed.c_str());
        ++count;
    }
    fclose(file);
    if (count) rfa::LogDetail("console.ini [%s]: applied %d setting(s)", section, count);
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
// Typing: pasting, Tab, and a % in what you run

constexpr size_t kTypedRoom = 124;      // the game stops typing there (the echo "] <line>" fills 0x80 bytes)
constexpr size_t kMostPastedLines = 100;
constexpr size_t kMostPasted = 65536;   // characters read from the clipboard

// Puts `text` into the typed line at the caret (or in place of the line), as much as fits, as the
// game's typing does; false if some didn't fit.
bool TypeIn(const std::string& text, bool replaceLine) {
    std::string line = replaceLine ? "" : std::string(g_typed, strnlen(g_typed, sizeof(g_typed)));
    const size_t caret = replaceLine ? 0 : std::min(static_cast<size_t>(std::max(g_caret, 0)), line.size());
    const size_t room = line.size() < kTypedRoom ? kTypedRoom - line.size() : 0;
    const std::string fits = text.substr(0, room);
    line.insert(caret, fits);
    strcpy_s(g_typed, line.c_str());
    g_caret = static_cast<int>(caret + fits.size());
    snprintf(g_prompted, sizeof(g_prompted), "] %s", g_typed);  // as the game remakes it after each key
    g_caretShown = 1;
    TimerStart(&g_caretTimer, nullptr, 300);
    return fits.size() == text.size();
}

// Pasting (Ctrl+V or Shift+Insert). The console can only type plain ASCII on one line, so what is
// pasted is cleaned up first: tabs and other spaces become a space, typographic quotes, dashes and
// the ellipsis their ASCII forms, invisible and control characters go, and anything else it can't
// type is left out (and said so). Each line is trimmed, a "]" in front (a line copied from the
// console or console.log) is dropped, and empty and comment lines (; # //) are skipped. The first
// line goes in at the caret; the others wait, and each Enter runs the line and brings up the next
// (Esc drops them), so nothing pasted runs without you pressing Enter.

// Adds what the console can type of `c` to `line`; false if it can't type it at all.
bool AppendTypeable(wchar_t c, std::string& line) {
    if (c >= 0x20 && c < 0x7F) line += static_cast<char>(c);
    else if (c == L'\t' || c == 0xA0 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x3000) line += ' ';
    else if (c == 0x2018 || c == 0x2019 || c == 0x201A || c == 0x2032) line += '\'';
    else if (c == 0x201C || c == 0x201D || c == 0x201E || c == 0x2033) line += '"';
    else if ((c >= 0x2010 && c <= 0x2015) || c == 0x2212) line += '-';
    else if (c == 0x2026) line += "...";
    else if (c < 0x20 || c == 0x7F || (c >= 0x200B && c <= 0x200D) || c == 0x2060 || c == 0xFEFF) {}  // nothing to see
    else return false;
    return true;
}

std::wstring ClipboardText() {
    std::wstring text;
    if (!OpenClipboard(g_gameWindow)) return text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (auto characters = static_cast<const wchar_t*>(GlobalLock(data))) {
            const size_t most = std::min<size_t>(GlobalSize(data) / sizeof(wchar_t), kMostPasted);
            text.assign(characters, wcsnlen(characters, most));
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

std::vector<std::string> PastedLines(const std::wstring& text, int& leftOut) {
    std::vector<std::string> lines;
    std::string line;
    auto finish = [&] {
        std::string command = Trim(line);
        line.clear();
        if (!command.empty() && command[0] == ']') command = Trim(command.substr(1));
        if (command.empty() || command[0] == ';' || command[0] == '#' || command.rfind("//", 0) == 0) return;
        lines.push_back(command);
    };
    for (wchar_t c : text) {
        if (c == L'\r' || c == L'\n') finish();
        else if (!AppendTypeable(c, line)) ++leftOut;
    }
    finish();
    return lines;
}

void Paste() {
    int leftOut = 0;
    std::vector<std::string> lines = PastedLines(ClipboardText(), leftOut);
    if (leftOut > 0)
        AddLine("Pasted without " + std::to_string(leftOut) + " character(s) the console can't type", Kind::Warning);
    if (lines.empty()) {
        if (!leftOut) AddLine("Nothing to paste: the clipboard holds no text the console can run", Kind::Info);
        return;
    }
    if (lines.size() > kMostPastedLines) {
        AddLine("Pasted the first " + std::to_string(kMostPastedLines) + " of " + std::to_string(lines.size()) + " lines",
                Kind::Warning);
        lines.resize(kMostPastedLines);
    }
    bool cut = !TypeIn(lines[0], false);
    g_pasted.assign(lines.begin() + 1, lines.end());
    for (std::string& waiting : g_pasted) {
        if (waiting.size() <= kTypedRoom) continue;
        waiting.resize(kTypedRoom);
        cut = true;
    }
    if (cut)
        AddLine("A command line holds " + std::to_string(kTypedRoom) + " characters; the rest of a longer pasted line was left out",
                Kind::Warning);
    if (!g_pasted.empty())
        AddLine("Pasted " + std::to_string(lines.size()) + " lines: Enter runs each in turn, Esc drops the rest", Kind::Info);
    RFA_LOG_DETAIL_FIRST(3, "Pasted %u line(s) into the console", static_cast<unsigned>(lines.size()));
}

// Tab (in place of the game's 0xC7D200). The game matched the whole line against the command names,
// so with arguments typed it found nothing ("No console command found to match '<line>'"), and it
// listed matches by name alone. This completes the first word only, lists the matches with what
// each does, and with arguments typed leaves the line as it is and says what the command is.
constexpr DWORD kCompleteCommand = 0xC7D200;  // void(), from the key handling (0xC7CEF3)

void SayWhatItIs(const ConsoleCommand& command) {
    std::string line = std::string("  ") + command.name;
    if (command.description && *command.description) line += std::string(" - ") + command.description;
    AddLine(line, Kind::Info);
}

// What a setting takes (commands take whatever their own code reads).
void SayWhatItTakes(const ConsoleCommand& command) {
    const std::string name = command.name;
    switch (command.type) {
    case kOnOff: AddLine("  " + name + " on, " + name + " off, or " + name + " alone to switch it; ? " + name + " shows it", Kind::Info); break;
    case kWholeNumber: AddLine("  " + name + " <whole number>; ? " + name + " shows it", Kind::Info); break;
    case kNumber: AddLine("  " + name + " <number>; ? " + name + " shows it", Kind::Info); break;
    case kVector: AddLine("  a colour or vector setting; ? " + name + " shows it", Kind::Info); break;
    }
}

bool AsksAbout(const std::string& word) {  // the game's status (?) and help (help, man) words
    return word == "?" || _stricmp(word.c_str(), "help") == 0 || _stricmp(word.c_str(), "man") == 0;
}

void __cdecl CompleteCommand() {
    const std::string typed(g_typed, strnlen(g_typed, sizeof(g_typed)));
    // The command's name: the first word, or the next one after ?, help or man.
    size_t start = typed.find_first_not_of(' ');
    std::string lead;
    if (start != std::string::npos) {
        const size_t end = std::min(typed.find(' ', start), typed.size());
        if (end < typed.size() && AsksAbout(typed.substr(start, end - start))) {
            lead = typed.substr(start, end - start) + " ";
            start = typed.find_first_not_of(' ', end);
        }
    }
    if (start == std::string::npos) {
        AddLine("Tab completes a command's name: type how it starts first (help lists them all)", Kind::Info);
        return;
    }
    const size_t end = std::min(typed.find(' ', start), typed.size());
    const std::string word = typed.substr(start, end - start);
    std::vector<const ConsoleCommand*> matches;
    for (int i = 0; i < g_commandCount; ++i) {
        const ConsoleCommand* command = g_commands[i];
        if (command && command->name && _strnicmp(command->name, word.c_str(), word.size()) == 0) matches.push_back(command);
    }

    if (end < typed.size()) {  // arguments typed: the line stays, and what the command is
        if (const ConsoleCommand* command = FindCommand(word)) {
            SayWhatItIs(*command);
            SayWhatItTakes(*command);
            return;
        }
        AddLine("No console command is named '" + word + "'", Kind::Error);
        if (!matches.empty() && matches.size() <= 20) {
            AddLine("Commands starting with '" + word + "':", Kind::Info);
            for (const ConsoleCommand* command : matches) SayWhatItIs(*command);
        }
        return;
    }

    if (matches.empty()) {
        AddLine("No console command starts with '" + word + "'", Kind::Error);
        return;
    }
    if (matches.size() == 1) {
        TypeIn(lead + matches[0]->name + " ", true);
        return;
    }
    AddLine("Commands starting with '" + word + "' (" + std::to_string(matches.size()) + "):", Kind::Info);
    size_t common = strlen(matches[0]->name);  // as far as they all start the same
    for (const ConsoleCommand* command : matches) {
        SayWhatItIs(*command);
        size_t same = 0;
        while (same < common && command->name[same] &&
               tolower(static_cast<unsigned char>(command->name[same])) == tolower(static_cast<unsigned char>(matches[0]->name[same])))
            ++same;
        common = same;
    }
    if (common > word.size()) TypeIn(lead + std::string(matches[0]->name, common), true);
}

// The game's command runner (0xC7BDB0) is printf-style, and Enter hands it the typed line as its
// format, so a % in the line was read as a format (rubbish, or a crash). The typed line is now run
// as text; other callers' formats are written out first, into the 256 bytes the game uses.
constexpr size_t kRunConsoleCommandPrologue = 9;  // push ebp / mov ebp,esp / sub esp,128h
using RunConsoleCommandFn = int(__cdecl*)(const char* format, ...);
RunConsoleCommandFn g_runConsoleCommand;

int __cdecl RunCommandAsText(const char* format, ...) {
    if (format == g_typed) return g_runConsoleCommand("%s", format);
    char line[256];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format ? format : "", arguments);
    va_end(arguments);
    return g_runConsoleCommand("%s", line);
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
volatile bool g_linkFolderThere = false;  // bin\link is there (the editor's link is set up; set by the console's files thread)
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
        rfa::LogDetail("Editor link: picked 0x%08X", static_cast<unsigned>(g_lastPicked));
    }
}

bool RunEditorEdit(const std::string& line);  // the editor's live edits (below)

// Runs the commands the editor sent, if any, and deletes them.
// The renderer's lock (rl_is_locked, 0xD2EC00): set while a frame is drawn on the render thread (the
// main loop's handshake, 0x521D70: unlocked by 0x76AA50 as the game's update starts, locked again by
// 0x76AA80). Placing or making draw instances (and objects with models) while it is set fails the
// game's "rl_is_locked() == false" check and leaves the scene half changed, so the editor's commands
// run only while it isn't: from the frame hook when it isn't, and right after 0x76AA50 unlocks it.
bool RenderLocked() { return *reinterpret_cast<volatile BYTE*>(0x2E7A6A9) != 0; }

// On the console's files thread: the editor's commands file, if it is there, read and deleted.
void ReadEditorCommands(std::vector<EditorBatch>& batches) {
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
    EditorBatch batch;
    batch.ageSeconds = now.QuadPart > written.QuadPart ? (now.QuadPart - written.QuadPart) / 10000000 : 0;

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"r") != 0 || !file) return;  // try again next time
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), file)) batch.lines.push_back(Trim(buffer));
    fclose(file);
    DeleteFileW(path.c_str());
    batches.push_back(std::move(batch));
}

// On the game's thread, with the renderer unlocked: the commands the files thread has read, run in order.
void RunEditorCommands() {
    std::vector<EditorBatch> batches;
    {
        std::lock_guard<std::mutex> lock(g_filesLock);
        if (g_editorBatches.empty()) return;
        batches.swap(g_editorBatches);
    }
    for (const EditorBatch& batch : batches) {
        if (batch.ageSeconds > kOldCommandsSeconds) {
            rfa::Log("Editor link: skipped commands the editor sent %u seconds ago", static_cast<unsigned>(batch.ageSeconds));
            continue;
        }
        for (const std::string& line : batch.lines) {
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (RunEditorEdit(line)) continue;
            if (line.size() > kMaxCommandLength) {
                rfa::Log("Editor link: a command longer than %u characters was skipped", static_cast<unsigned>(kMaxCommandLength));
                continue;
            }
            rfa::LogDetail("Editor link: %s", line.c_str());
            RunConsoleCommand("%s", line.c_str());
        }
    }
}

// The console's files thread (see "The console's files, off the game's thread"): every 15 ms, console.log
// written, rfa_mods.log read while the console takes its lines, and the editor's link folder watched.
DWORD WINAPI ConsoleFiles(void*) {
    HANDLE consoleLog = INVALID_HANDLE_VALUE;
    if (g_settings.logToFile) {
        CreateDirectoryW((rfa::RootDir() + L"\\logs").c_str(), nullptr);
        consoleLog = CreateFileW((rfa::RootDir() + L"\\logs\\console.log").c_str(), GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                 nullptr);
    }
    int linkFolderChecks = 0;
    for (std::vector<std::string> toWrite, modLines; ; Sleep(kFilesEveryMs)) {
        {
            std::lock_guard<std::mutex> lock(g_filesLock);
            toWrite.swap(g_consoleLogQueue);
        }
        if (consoleLog != INVALID_HANDLE_VALUE && !toWrite.empty()) {
            std::string text;
            for (const std::string& line : toWrite) text += line;
            DWORD written;
            WriteFile(consoleLog, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        }
        toWrite.clear();
        if (GetTickCount64() - g_modLinesWantedAt < 2000) {
            ReadModLogLines(modLines);
            if (!modLines.empty()) {
                std::lock_guard<std::mutex> lock(g_filesLock);
                for (std::string& line : modLines) g_modLines.push_back(std::move(line));
                if (g_modLines.size() > kMostQueuedModLines)
                    g_modLines.erase(g_modLines.begin(), g_modLines.begin() + (g_modLines.size() - kMostQueuedModLines));
            }
            modLines.clear();
        }
        if (linkFolderChecks-- <= 0) {  // about once a second
            g_linkFolderThere = GetFileAttributesW(LinkFolder().c_str()) != INVALID_FILE_ATTRIBUTES;
            linkFolderChecks = 60;
        }
        if (g_linkFolderThere) {
            std::vector<EditorBatch> batches;
            ReadEditorCommands(batches);
            if (!batches.empty()) {
                std::lock_guard<std::mutex> lock(g_filesLock);
                for (EditorBatch& batch : batches) g_editorBatches.push_back(std::move(batch));
            }
        }
    }
}

void StartConsoleFiles() {
    static std::once_flag started;
    std::call_once(started, [] {
        if (HANDLE thread = CreateThread(nullptr, 0, ConsoleFiles, nullptr, 0, nullptr)) CloseHandle(thread);
    });
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

// The editor's live edits (its Live button): lines among its commands that aren't the console's.
//   editor_move <handle> <x y z> <9 numbers>   moves and turns the object (rotation rows right, up,
//       forward) with the object's own "set position and rotation" (slot 10 of its functions, at
//       +0x28: thiscall, the position, the rotation, 1), as the mission scripts' object_set_position
//       (0x9203B0) does;
//   editor_chunk <asset list> <container>      loads a chunk the level hasn't (below);
//   editor_reload                              loads the level again and puts the player back (below);
//   editor_rescan                              has the loader look for new files in overrides\ and edits\;
//   editor_remove <handle>                     removes it (0x6D56A0, thiscall on the world: the handle, 0).
//   editor_create <handle>                     makes an object the game hasn't, from its block as a
//       zone file holds it (header, then properties), which the editor put in
//       bin\link\create_<HANDLE>.bin: as the game's own layer loader (0x6D5D30) makes a layer's
//       objects, first a stub for it (0x6D56E0, thiscall on the world: where the block's address
//       is), then the object from stub and block (0x6D5770, thiscall on the world: how many, the
//       stubs, the blocks). The block is kept: the game may point into it.
// An object the game hasn't now (not loaded, broken up, or only in the editor yet) is passed over.
using SetPlaceFn = void(__fastcall*)(void* object, void* unused, const float* position, const float* rotation, int now);
auto const RemoveObject = reinterpret_cast<int(__fastcall*)(void* world, void* unused, DWORD handle, int flag)>(0x6D56A0);

auto const MakeStub = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, BYTE** block)>(0x6D56E0);
auto const MakeFromStubs = reinterpret_cast<void(__fastcall*)(void* world, void* unused, int count, BYTE** stubs, BYTE** blocks)>(0x6D5770);
constexpr DWORD kStubNumber = 0x74;  // a stub's place in the lists it is made from (a short)
constexpr size_t kBlockHeader = 0x38, kBlockHandle = 4;

// A spawn node made live puts its squad, vehicle or exo down at once, as the mission scripts'
// spawn functions do (a node of the level's own does it when its zone streams in, so one made as the
// game runs would never): sf_spawn_squad (0x94F710) finds the node (0x425AE0, thiscall on the world:
// the handle) and spawns its squad (0x800040, thiscall on the node: -1, 0; the squad's handle or -1);
// sf_spawn_exo (0x94F520): 0x92DE90, then 0x8021C0 (-1); sf_of_spawn_vehicle (0x943510): 0x4B32F0,
// then 0x7FE5A0 (-1). Each then sets the flag 0x7EE790 sets. What a node spawned is kept by the node's
// handle, so taking the node away (editor_remove: the editor does that to make it again with new
// settings, too) takes it away as well.
auto const FindSquadNode = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, DWORD handle)>(0x425AE0);
auto const SpawnSquadFromNode = reinterpret_cast<DWORD(__fastcall*)(BYTE* node, void* unused, int a, int b)>(0x800040);
auto const FindExoNode = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, DWORD handle)>(0x92DE90);
auto const SpawnExoFromNode = reinterpret_cast<DWORD(__fastcall*)(BYTE* node, void* unused, int a)>(0x8021C0);
auto const FindVehicleNode = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, DWORD handle)>(0x4B32F0);
auto const SpawnVehicleFromNode = reinterpret_cast<DWORD(__fastcall*)(BYTE* node, void* unused, int a)>(0x7FE5A0);
auto const NoteSpawned = reinterpret_cast<void(__cdecl*)()>(0x7EE790);
std::map<DWORD, DWORD> g_nodeSpawned;  // a live-made node's handle -> what it spawned

// A zone class name's hash, as zone files keep it (the editor's NameHashes.ClassHash).
constexpr DWORD ClassHash(const char* name) {
    DWORD hash = 0;
    for (; *name; ++name) {
        const char c = *name >= 'A' && *name <= 'Z' ? static_cast<char>(*name - 'A' + 'a') : *name;
        hash = ((hash << 6) | (hash >> 26)) ^ static_cast<unsigned char>(c);
    }
    return hash;
}

void SpawnFromNode(DWORD handle, DWORD classHash) {
    void* world = reinterpret_cast<void*>(kObjectList);
    const char* what = nullptr;
    DWORD spawned = 0xFFFFFFFF;
    if (classHash == ClassHash("object_squad_spawn_node")) {
        what = "squad";
        if (BYTE* node = FindSquadNode(world, nullptr, handle)) spawned = SpawnSquadFromNode(node, nullptr, -1, 0);
    } else if (classHash == ClassHash("object_exo_spawn_node")) {
        what = "exo";
        if (BYTE* node = FindExoNode(world, nullptr, handle)) spawned = SpawnExoFromNode(node, nullptr, -1);
    } else if (classHash == ClassHash("object_vehicle_spawn_node")) {
        what = "vehicle";
        if (BYTE* node = FindVehicleNode(world, nullptr, handle)) spawned = SpawnVehicleFromNode(node, nullptr, -1);
    }
    if (!what) return;
    if (spawned == 0xFFFFFFFF) {
        rfa::Log("Editor link: node 0x%08X couldn't spawn its %s (not in its spawn resources, or the game has no room)", static_cast<unsigned>(handle), what);
        return;
    }
    NoteSpawned();
    g_nodeSpawned[handle] = spawned;
    rfa::LogDetail("Editor link: node 0x%08X spawned its %s (0x%08X)", static_cast<unsigned>(handle), what, static_cast<unsigned>(spawned));
}

void MakeEditorObject(DWORD handle) {
    void* world = reinterpret_cast<void*>(kObjectList);
    wchar_t name[40];
    swprintf_s(name, L"\\create_%08X.bin", static_cast<unsigned>(handle));
    const std::wstring path = LinkFolder() + name;
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) {
        rfa::LogDetail("Editor link: no block for object 0x%08X (%ls)", static_cast<unsigned>(handle), path.c_str());
        return;
    }
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    BYTE* block = size >= static_cast<long>(kBlockHeader) && size <= 0xFFFF ? static_cast<BYTE*>(_aligned_malloc(size + 16, 16)) : nullptr;
    const bool read = block && fread(block, 1, size, file) == static_cast<size_t>(size);
    fclose(file);
    DeleteFileW(path.c_str());
    if (!read || *reinterpret_cast<DWORD*>(block + kBlockHandle) != handle) {
        rfa::Log("Editor link: the block for object 0x%08X isn't one (%ld bytes)", static_cast<unsigned>(handle), size);
        return;
    }
    if (FindObject(world, nullptr, handle)) {
        rfa::Log("Editor link: the game already has an object 0x%08X; not made again", static_cast<unsigned>(handle));
        return;
    }
    BYTE* blocks[1] = {block};
    BYTE* stubs[1] = {MakeStub(world, nullptr, &blocks[0])};
    if (!stubs[0]) {
        rfa::Log("Editor link: the game made no stub for object 0x%08X (a class it doesn't make from zones?)", static_cast<unsigned>(handle));
        return;
    }
    *reinterpret_cast<WORD*>(stubs[0] + kStubNumber) = 0;
    MakeFromStubs(world, nullptr, 1, stubs, blocks);
    const bool made = stubs[0] && FindObject(world, nullptr, handle);
    if (made) rfa::LogDetail("Editor link: made object 0x%08X", static_cast<unsigned>(handle));
    else rfa::Log("Editor link: the game couldn't make object 0x%08X from its block", static_cast<unsigned>(handle));
    if (made) SpawnFromNode(handle, *reinterpret_cast<DWORD*>(block));
}

// editor_reload: the level is loaded again (the console's reload_level), and the player then put
// back where they stood (warp x y z), so a change that needs the level loaded again (new models,
// the ground, level geometry) doesn't also send you back to the level's start. The player (0x8B92D0)
// is gone or another while the level loads; once there is one again that has stayed put for 0.3
// seconds, the level is taken to be up.
auto const ThePlayer = reinterpret_cast<BYTE*(__cdecl*)()>(0x8B92D0);
struct ReloadReturn {
    bool waiting = false;
    float position[3]{};
    BYTE* playerThen = nullptr;
    bool sawLoading = false;
    int checks = 0, steady = 0;
    float last[3]{};
} g_reloadReturn;

void StartEditorReload() {
    BYTE* player = ThePlayer();
    g_reloadReturn = ReloadReturn{};
    if (player) {
        memcpy(g_reloadReturn.position, player + 4, sizeof(g_reloadReturn.position));
        g_reloadReturn.playerThen = player;
        g_reloadReturn.waiting = true;
    }
    rfa::LogDetail("Editor link: loading the level again%s", player ? "; the player goes back to where they stand" : "");
    RunConsoleCommand("reload_level");
}

// Ten times a second while a reload's return is waiting.
void ContinueEditorReload() {
    ReloadReturn& r = g_reloadReturn;
    if (!r.waiting) return;
    if (++r.checks > 1200) {  // two minutes: given up
        r.waiting = false;
        return;
    }
    BYTE* player = ThePlayer();
    if (!player || player != r.playerThen) r.sawLoading = true;
    if (!player || (!r.sawLoading && r.checks < 50)) {  // (no sign of loading after 5 seconds: it was quick)
        r.steady = 0;
        return;
    }
    float now[3];
    memcpy(now, player + 4, sizeof(now));
    r.steady = memcmp(now, r.last, sizeof(now)) == 0 || r.steady == 0 ? r.steady + 1 : 1;
    memcpy(r.last, now, sizeof(now));
    if (r.steady < 3) return;  // (still for 0.3 seconds)
    r.waiting = false;
    rfa::LogDetail("Editor link: the level is up again; the player goes back to %.1f, %.1f, %.1f", r.position[0], r.position[1], r.position[2]);
    RunConsoleCommand("warp %.3f %.3f %.3f", r.position[0], r.position[1], r.position[2]);
}

// editor_chunk <asset list> <container>: loads a chunk the level hasn't, so objects of it can be made
// at once (Live). The editor put a container holding the chunk's model and textures, and an asset
// list naming just that container, in overrides\live\ (new files, found by the loader once it has
// looked again: editor_rescan comes first). The asset list is read as the chunk system reads
// chunk_containers.asm_pc at the game's start (0x90A3A0, with the chunk system's pools, see 0x6AA9F0),
// which registers the container with the streaming system; then it is found by name (0x90A290) and
// loaded at once, as the game loads its preload chunks (0x908FC0: true once loaded). Loading the
// container's model makes the chunk known to the game (the chunk primitive type does that), so
// objects of it can be made. The result goes to link\chunk_<container>.txt for the editor.
auto const ReadAssetList = reinterpret_cast<bool(__cdecl*)(const char* name, void* tempPool, void* pool, int, int, int)>(0x90A3A0);
auto const FindContainer = reinterpret_cast<void*(__cdecl*)(const char* name)>(0x90A290);
auto const LoadContainerNow = reinterpret_cast<bool(__cdecl*)(void* container)>(0x908FC0);
constexpr DWORD kChunkTempPool = 0x26AF908, kChunkPool = 0x26AEE78;

// Each step on its own, guarded: a fault in the game's code is logged with the step, not a crash
// (unless another mod's exception handler gets to it first).
int GuardedReadAssetList(const char* name) {
    __try {
        return ReadAssetList(name, reinterpret_cast<void*>(kChunkTempPool), reinterpret_cast<void*>(kChunkPool), 0, 0, 0) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -static_cast<int>(GetExceptionCode() & 0x7FFFFFFF);
    }
}
void* GuardedFindContainer(const char* name, bool* faulted) {
    __try {
        return FindContainer(name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *faulted = true;
        return nullptr;
    }
}
int GuardedLoadContainer(void* container) {
    __try {
        return LoadContainerNow(container) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -static_cast<int>(GetExceptionCode() & 0x7FFFFFFF);
    }
}

void LoadEditorChunk(const std::string& assetList, const std::string& container) {
    bool faulted = false;
    rfa::LogDetail("Editor link: chunk container %s: looking it up", container.c_str());
    void* found = GuardedFindContainer(container.c_str(), &faulted);
    if (!found && !faulted) {
        rfa::LogDetail("Editor link: chunk container %s: reading the asset list %s", container.c_str(), assetList.c_str());
        const int read = GuardedReadAssetList(assetList.c_str());
        if (read < 0) rfa::LogDetail("Editor link: reading the asset list FAULTED (exception %08X)", static_cast<unsigned>(-read) | 0x80000000u);
        found = read < 0 ? nullptr : GuardedFindContainer(container.c_str(), &faulted);
        if (!found) rfa::Log("Editor link: the asset list %s was %s, but has no container %s", assetList.c_str(), read > 0 ? "read" : "not read", container.c_str());
    }
    int load = 0;
    if (found) {
        rfa::LogDetail("Editor link: chunk container %s: loading it (container record %p)", container.c_str(), found);
        load = GuardedLoadContainer(found);
        if (load < 0) rfa::LogDetail("Editor link: loading the container FAULTED (exception %08X)", static_cast<unsigned>(-load) | 0x80000000u);
    }
    const bool loaded = load > 0;
    if (loaded) rfa::LogDetail("Editor link: chunk container %s loaded", container.c_str());
    else rfa::Log("Editor link: chunk container %s NOT loaded", container.c_str());
    const std::wstring answer = LinkFolder() + L"\\chunk_" + std::wstring(container.begin(), container.end()) + L".txt";
    FILE* file = nullptr;
    if (_wfopen_s(&file, answer.c_str(), L"w") == 0 && file) {
        fputs(loaded ? "loaded" : "failed", file);
        fclose(file);
    }
}

// editor_chunk_files <group> <file>...: loads a chunk the level hasn't (Live), from its files, which the
// editor put loose in overrides\live\ (found by name once the loader has looked again: editor_rescan
// comes first). The chunk's group (<chunk>.rfgchunkx, .rfgstitchx for level geometry: the container
// the game looks for when it makes an object of the chunk) is made as the game makes its preload
// chunks' at start-up (0x6AAC00): started (0x909670: the name, the chunk pool, type 15, preload),
// its model added (0x9097A0: the file, the primitive type's name, allocator 2) and its textures
// ("chunk_texture", allocator 1), finished (0x909E60) and loaded at once (0x908FC0). (Reading an asset
// list instead, 0x90A3A0, faults: its temporary allocator is only open while the game starts.) A file
// the game can't find is fatal there, so each is looked for first (0xC92010).
constexpr bool g_makeChunkGroups = true;  // (see LoadEditorChunkFiles)

// The allocator a group's records (its container, names and primitive records) come from. The game's
// chunk pool (0x26AEE78) is its start-up pool, full once the game runs: asking it for more stalls the
// game and fails. The streaming system only asks its allocator (an object whose first word is its
// function table) "has a mark?" (+0x0C), for a mark (+0x5C, only if so) and for memory (+0x38:
// size, alignment, 0, 0): this one answers from the C heap. A group is never let go, nor its memory.
void* __fastcall LiveAllocate(void*, void*, unsigned size, unsigned alignment, int, int) {
    if (alignment < 4) alignment = 4;
    void* memory = _aligned_malloc(size ? size : 1, alignment);
    if (memory) memset(memory, 0, size);
    return memory;
}
bool __fastcall LiveHasMark(void*, void*) { return false; }
int __fastcall LiveMark(void*, void*) { return 0; }
void __fastcall LiveRelease(void*, void*, int, int, int) {}  // (+0x60: back to a mark; nothing kept)
struct LiveAllocator {
    void** table;
};
void* g_liveAllocatorTable[48];
LiveAllocator g_liveAllocator{g_liveAllocatorTable};
void* LiveGroupAllocator() {
    if (!g_liveAllocatorTable[0]) {
        for (void*& slot : g_liveAllocatorTable) slot = reinterpret_cast<void*>(&LiveMark);
        g_liveAllocatorTable[0x0C / 4] = reinterpret_cast<void*>(&LiveHasMark);
        g_liveAllocatorTable[0x18 / 4] = reinterpret_cast<void*>(&LiveHasMark);  // (has a parent allocator? no: +0x80 isn't asked)
        g_liveAllocatorTable[0x38 / 4] = reinterpret_cast<void*>(&LiveAllocate);
        g_liveAllocatorTable[0x60 / 4] = reinterpret_cast<void*>(&LiveRelease);
    }
    return &g_liveAllocator;
}
auto const StreamContainerStart = reinterpret_cast<bool(__cdecl*)(const char* name, void* pool, BYTE type, BYTE notPreload, BYTE)>(0x909670);
auto const StreamAddPrimitive = reinterpret_cast<void*(__cdecl*)(const char* file, const char* typeName, BYTE allocator, char split, int, BYTE flags, int)>(0x9097A0);
auto const StreamContainerFinish = reinterpret_cast<void*(__cdecl*)(BYTE, BYTE)>(0x909E60);
auto const GameFileExists = reinterpret_cast<bool(__cdecl*)(const char* name)>(0xC92010);
constexpr DWORD kPrimitiveTypeNames = 0x10FE40C;  // const char*[]: 3 stitch, 4 chunk, 5 chunk_texture

int GuardedFileExists(const char* name) {
    __try {
        return GameFileExists(name) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// 1 if the group was made, else a negative step: -1 start, -2 a file, -3 finish.
int GuardedMakeChunkGroup(const char* group, const std::vector<const char*>& files, int* failedFile) {
    bool started = false;
    __try {
        // As a level's chunk is listed when the game loads a level from loose files (0x6AE620): a group of
        // type 14 (not preloaded), the model in the chunk allocator (2), its textures in the texture one (1).
        if (!StreamContainerStart(group, LiveGroupAllocator(), 14, 1, 0)) return -1;
        started = true;
        const char* const* types = reinterpret_cast<const char* const*>(kPrimitiveTypeNames);
        for (size_t i = 0; i < files.size(); i++) {
            *failedFile = static_cast<int>(i);
            const char* dot = strrchr(files[i], '.');
            if (!dot) continue;
            if (_stricmp(dot, ".cchk_pc") == 0) StreamAddPrimitive(files[i], types[4], 2, 1, 0, 0, 0);
            else if (_stricmp(dot, ".cstch_pc") == 0) StreamAddPrimitive(files[i], types[3], 2, 1, 0, 0, 0);
            else if (_stricmp(dot, ".cvbm_pc") == 0 || _stricmp(dot, ".cpeg_pc") == 0) StreamAddPrimitive(files[i], types[5], 1, 1, 0, 0, 0);
        }
        *failedFile = -1;
        started = false;
        return StreamContainerFinish(0, 0) ? 1 : -3;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (started) {
            __try {
                StreamContainerFinish(0, 0);  // (the streaming system's group being made is closed)
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        return *failedFile >= 0 ? -2 : -1;
    }
}

auto const RequestContainerLoad = reinterpret_cast<bool(__cdecl*)(void* container)>(0x908F10);
auto const ContainerState = reinterpret_cast<int(__cdecl*)(void* container)>(0x909210);  // 3 loaded, 4 failed
struct ChunkLoading {
    std::string group;
    void* container;
    ULONGLONG since;
    bool onDemand = false;  // a group loaded when needed (flag 0x100): put back so once loaded
};
auto const SetOnDemand = reinterpret_cast<bool(__cdecl*)(void* container, bool onDemand)>(0x908480);
int GuardedSetOnDemand(void* container, bool onDemand) {
    __try {
        SetOnDemand(container, onDemand);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
std::vector<ChunkLoading> g_chunksLoading;

int GuardedRequestLoad(void* container) {
    __try {
        return RequestContainerLoad(container) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
int GuardedContainerState(void* container) {
    __try {
        return ContainerState(container);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 4;
    }
}

void AnswerChunk(const std::string& group, bool loaded) {
    if (loaded) rfa::LogDetail("Editor link: chunk %s loaded", group.c_str());
    else rfa::Log("Editor link: chunk %s NOT loaded", group.c_str());
    const std::wstring path = LinkFolder() + L"\\chunk_" + std::wstring(group.begin(), group.end()) + L".txt";
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"w") == 0 && file) {
        fputs(loaded ? "loaded" : "failed", file);
        fclose(file);
    }
}

// Whether the game's files are locked for reading now (cf_open's flag, 0x290A67D: see below).
bool CfileLocked() { return *reinterpret_cast<volatile BYTE*>(0x290A67D) != 0; }
auto const SetCfileLocked = reinterpret_cast<void(__cdecl*)(bool locked)>(0xC96450);
struct WaitingGroup { std::string line; ULONGLONG since; };
std::vector<WaitingGroup> g_groupsWaiting;
void LoadEditorChunkGroup(const std::string& line);

// Every frame: chunks asked to load that have (or failed, or are taking more than 30 seconds) are
// answered.
void WatchEditorChunks() {
    // Chunk groups waiting for the game's files to be free (LoadEditorChunkGroup): one at a time.
    if (!g_groupsWaiting.empty()) {
        WaitingGroup next = g_groupsWaiting.front();
        if (!CfileLocked()) {
            g_groupsWaiting.erase(g_groupsWaiting.begin());
            LoadEditorChunkGroup(next.line);
        } else if (GetTickCount64() - next.since > 30000) {
            g_groupsWaiting.erase(g_groupsWaiting.begin());
            char group[128] = {};
            sscanf_s(next.line.c_str() + 19, "%127s", group, static_cast<unsigned>(sizeof(group)));
            rfa::LogDetail("Editor link: chunk %s: the game's files stayed locked for 30 seconds", group);
            AnswerChunk(group, false);
        }
    }
    for (auto it = g_chunksLoading.begin(); it != g_chunksLoading.end();) {
        const int state = GuardedContainerState(it->container);
        const bool late = GetTickCount64() - it->since > 30000;
        if (state != 3 && state != 4 && !late) {
            ++it;
            continue;
        }
        if (late && state != 3) rfa::Log("Editor link: chunk %s: still not loaded after 30 seconds (state %d)", it->group.c_str(), state);
        if (it->onDemand && state == 3) GuardedSetOnDemand(it->container, true);
        AnswerChunk(it->group, state == 3);
        it = g_chunksLoading.erase(it);
    }
}

// editor_chunk_group <group> <asset list>: loads a chunk the level hasn't (Live). The editor put in
// overrides\live\ (found by name once the loader has looked again: editor_rescan comes first) the
// chunk's files packed in a container of their own and an asset list naming it and the chunk's group
// (<chunk>.rfgchunkx, or .rfgstitchx), as the Guerrilla world lists its own. If the game hasn't a group
// of that name, the list is read into its container records (0x90A3A0) with the allocator above for
// both its temporary memory and its records (the game's own are its start-up ones, closed or full once
// it runs). The group is then loaded as the game loads one (0x908FC0) but without waiting: a group
// loaded on demand (flag 0x100) is made loadable (0x908480, 0), asked to load (0x908F10), watched each
// frame (WatchEditorChunks), and put back once loaded.
int GuardedReadOwnAssetList(const char* name) {
    __try {
        return ReadAssetList(name, LiveGroupAllocator(), LiveGroupAllocator(), 0, 0, 0) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -static_cast<int>(GetExceptionCode() & 0x7FFFFFFF);
    }
}

void LoadEditorChunkGroup(const std::string& line) {
    char groupText[128] = {}, listText[128] = {};
    if (sscanf_s(line.c_str() + 19, "%127s %127s", groupText, static_cast<unsigned>(sizeof(groupText)), listText,
                 static_cast<unsigned>(sizeof(listText))) != 2) {
        rfa::Log("Editor link: editor_chunk_group needs a group and an asset list");
        return;
    }
    const std::string group = groupText;
    bool faulted = false;
    void* found = GuardedFindContainer(groupText, &faulted);
    if (!found && !faulted) {
        const bool lockedForCheck = CfileLocked();
        if (lockedForCheck) SetCfileLocked(false);
        const int exists = GuardedFileExists(listText);
        if (lockedForCheck) SetCfileLocked(true);
        if (exists <= 0) {
            rfa::Log("Editor link: chunk %s: the game can't find the asset list %s (is the loader up to date?)", groupText, listText);
            AnswerChunk(group, false);
            return;
        }
        rfa::LogDetail("Editor link: chunk %s: reading its asset list %s", groupText, listText);
        // As the game itself reads a file while it runs (0x501C30): its "no reading now" flag (cf_open,
        // 0xC91470, stops at "Someone is accessing cfile synchronously, but it is locked!" while it is
        // set, part way, after which the game couldn't find a level's files) cleared for the read and
        // set back after.
        const bool locked = CfileLocked();
        if (locked) SetCfileLocked(false);
        const int read = GuardedReadOwnAssetList(listText);
        if (locked) SetCfileLocked(true);
        if (read <= 0) {
            rfa::Log(read < 0 ? "Editor link: chunk %s: reading its asset list FAULTED (exception %08X)" : "Editor link: chunk %s: its asset list wasn't read%s",
                     groupText, read < 0 ? static_cast<unsigned>(-read) | 0x80000000u : 0u);
            AnswerChunk(group, false);
            return;
        }
        found = GuardedFindContainer(groupText, &faulted);
        if (!found) {
            rfa::Log("Editor link: chunk %s: its asset list was read, but has no such group", groupText);
            AnswerChunk(group, false);
            return;
        }
    }
    if (!found) {
        AnswerChunk(group, false);
        return;
    }
    const bool onDemand = (*reinterpret_cast<WORD*>(static_cast<BYTE*>(found) + 0x22) & 0x100) != 0;
    if (onDemand) GuardedSetOnDemand(found, false);
    if (GuardedRequestLoad(found) <= 0) {
        if (onDemand) GuardedSetOnDemand(found, true);
        AnswerChunk(group, false);
        return;
    }
    rfa::LogDetail("Editor link: chunk %s: loading", groupText);
    g_chunksLoading.push_back({group, found, GetTickCount64(), onDemand});
}

void LoadEditorChunkFiles(const std::string& line) {
    std::vector<std::string> words;
    for (size_t at = 19; at < line.size();) {
        const size_t end = line.find(' ', at);
        const std::string word = line.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (!word.empty()) words.push_back(word);
        if (end == std::string::npos) break;
        at = end + 1;
    }
    if (words.size() < 2) {
        rfa::Log("Editor link: editor_chunk_files needs a group name and the chunk's files");
        return;
    }
    const std::string group = words[0];
    auto reply = [&](bool loaded) { AnswerChunk(group, loaded); };
    bool faulted = false;
    void* found = GuardedFindContainer(group.c_str(), &faulted);
    if (!found && !faulted && !g_makeChunkGroups) {
        // Making a group the game's asset lists don't name stalls the game for most of a minute and
        // then fails (its container pool is full once the game has started): not tried.
        rfa::Log("Editor link: chunk %s: the game's asset lists have no group of that name (a chunk from the Guerrilla world or "
                 "another archive the game hasn't loaded); it shows after the level is saved and loaded again", group.c_str());
        reply(false);
        return;
    }
    if (!found && !faulted) {
        // The names stay with the streaming system: kept for good.
        std::vector<const char*> files;
        for (size_t i = 1; i < words.size(); i++) {
            const char* kept = _strdup(words[i].c_str());
            if (!kept) return;
            if (GuardedFileExists(kept) <= 0) {
                rfa::Log("Editor link: chunk %s: the game can't find its file %s (is the loader up to date?)", group.c_str(), kept);
                reply(false);
                return;
            }
            files.push_back(kept);
        }
        const char* keptGroup = _strdup(group.c_str());
        int failedFile = -1;
        rfa::LogDetail("Editor link: chunk %s: making its group of %d files", group.c_str(), static_cast<int>(files.size()));
        const int made = keptGroup ? GuardedMakeChunkGroup(keptGroup, files, &failedFile) : -1;
        if (made < 0) {
            rfa::Log("Editor link: chunk %s: making its group FAILED at %s%s", group.c_str(),
                     made == -1 ? "the start" : made == -2 ? "the file " : "the finish", made == -2 && failedFile >= 0 ? files[failedFile] : "");
            reply(false);
            return;
        }
        found = GuardedFindContainer(group.c_str(), &faulted);
    }
    // Asked to load (0x908F10), not loaded at once: the game streams it in over the next frames, and
    // the answer goes to the editor once it has (WatchEditorChunks). (Loading at once, 0x908FC0,
    // waits in a loop, for good if the streaming system can't load it.)
    if (!found || GuardedRequestLoad(found) <= 0) {
        reply(false);
        return;
    }
    rfa::LogDetail("Editor link: chunk %s: loading", group.c_str());
    g_chunksLoading.push_back({group, found, GetTickCount64()});
}

// Level geometry (stitch pieces), live. A zone's stitch list stays in memory as it was read (the
// loader, 0x6B8710, only turns its offsets into addresses); its terrain record (0x6BBA90, by the
// zone's .cterrain_pc name) has the terrain data at +0 (the zone object it was made for at +0xA8)
// and the list's header at +4: its first 9 words are each list's pieces, the next 9 their counts,
// and +0x94 the combined collision's parts (0 when each piece has its own body). A piece is 100 bytes:
// name +0, position +4, rotation +0x10 (3 rows), flags +0x34, its collision body +0x38 (when it has
// its own), its draw instances +0x3C (one per sub-chunk, +0x40 of them).
// When the level is made (0x6BBAC0) each piece gets its draw instances (0x76F280: an
// rl_stitch_mesh_instance per sub-chunk, placed with the instance's set position 0x602710 and set
// rotation 0x602740 and put in the scene, 0x768460) and, unless the level has one combined
// collision for all its pieces, a collision body of its own (0x6BC270, from the piece's chunk
// 0x6AC740 and the zone; the body told its piece, 0x776240). The same calls move, take away and
// make pieces now:
//   moved: its draw instances placed again, and its body (havok set position and rotation, 0x7765C0;
//     a body handle is the body). With a combined collision the piece has no body: one is made for
//     it where it now is (the combined one still has it where it was, until the level loads again);
//   deleted: its draw instances let go (0x76F8B0), its body put far below the world;
//   put back (undo): made again as the level made it;
//   new (a piece added in the editor): a piece of our own, outside the level's lists, made the
//     same way. Its chunk has to be loaded (the editor loads it first if the level hasn't).
auto const TerrainRecord = reinterpret_cast<BYTE*(__cdecl*)(const char* cterrainName)>(0x6BBA90);
auto const ChunkByName = reinterpret_cast<BYTE*(__cdecl*)(const char* name)>(0x6AC740);
auto const MakeStitchDraw = reinterpret_cast<void(__cdecl*)(BYTE* piece)>(0x76F280);
auto const FreeStitchDraw = reinterpret_cast<void(__cdecl*)(BYTE* piece)>(0x76F8B0);
auto const MakeStitchBody = reinterpret_cast<DWORD(__cdecl*)(BYTE* chunk, BYTE* zone, BYTE* piece)>(0x6BC270);
auto const BodyOf = reinterpret_cast<BYTE*(__cdecl*)(DWORD handle)>(0x778440);
auto const SetBodyUserData = reinterpret_cast<void(__cdecl*)(BYTE* body, int what, void* data)>(0x776240);
auto const SetBodyPlace = reinterpret_cast<void(__cdecl*)(BYTE* body, const float* position, const float* rotation)>(0x7765C0);
auto const SetDrawPosition = reinterpret_cast<void(__thiscall*)(void* instance, const float* position)>(0x602710);
auto const SetDrawRotation = reinterpret_cast<void(__thiscall*)(void* instance, const float* rotation)>(0x602740);
auto const UpdateInScene = reinterpret_cast<void(__cdecl*)(void* instance)>(0x768460);
auto const FreeBody = reinterpret_cast<void(__cdecl*)(DWORD handle, bool, bool, bool)>(0x7755C0);  // as the level frees its pieces' (handle, 0, 1, 1)
constexpr int kPieceSize = 100, kPiecePlace = 4, kPieceFlags = 0x34, kPieceBody = 0x38, kPieceDraws = 0x3C, kPieceDrawCount = 0x40;
constexpr int kChunkSubChunks = 0x158;
constexpr DWORD kNoBody = 0xFFFFFFFF;

// What we did to a piece beyond its own data: a body made for it (the level's collision being
// combined, or a new piece), and whether it is taken away now.
struct LivePiece {
    BYTE* record = nullptr;  // the terrain record it is of
    DWORD body = kNoBody;
    bool hidden = false;
};
std::vector<std::pair<BYTE*, LivePiece>> g_livePieces;  // by piece
struct SpawnedPiece {
    DWORD id = 0;
    BYTE* piece = nullptr;
    BYTE* record = nullptr;
    std::string name;
    int flags = 4;
};
std::vector<SpawnedPiece> g_spawnedPieces;

LivePiece& LivePieceOf(BYTE* piece, BYTE* record = nullptr) {
    for (auto& [at, live] : g_livePieces)
        if (at == piece) return live;
    g_livePieces.push_back({piece, LivePiece{record}});
    return g_livePieces.back().second;
}

bool HasDraws(BYTE* piece) {
    void** draws = *reinterpret_cast<void***>(piece + kPieceDraws);
    const int count = *reinterpret_cast<int*>(piece + kPieceDrawCount);
    for (int i = 0; draws && i < count; i++)
        if (draws[i]) return true;
    return false;
}

// The piece's draw instances where its data puts it now.
void PlaceDraws(BYTE* piece) {
    void** draws = *reinterpret_cast<void***>(piece + kPieceDraws);
    const int count = *reinterpret_cast<int*>(piece + kPieceDrawCount);
    const float* place = reinterpret_cast<float*>(piece + kPiecePlace);
    for (int i = 0; draws && i < count; i++) {
        if (!draws[i]) continue;
        SetDrawPosition(draws[i], place);
        SetDrawRotation(draws[i], place + 3);
        UpdateInScene(draws[i]);
    }
}

// The body the piece collides with as we can move it: its own, else one we made for it (made now
// if `make`; the chunk's collision model is needed).
DWORD BodyFor(BYTE* piece, BYTE* zone, bool ownBody, bool make) {
    if (ownBody) {
        const DWORD own = *reinterpret_cast<DWORD*>(piece + kPieceBody);
        if (own != kNoBody && own != 0) return own;
    }
    LivePiece& live = LivePieceOf(piece);
    if (live.body != kNoBody || !make || !zone) return live.body;
    BYTE* chunk = ChunkByName(*reinterpret_cast<const char**>(piece));
    if (!chunk) return kNoBody;
    live.body = MakeStitchBody(chunk, zone, piece);
    if (BYTE* body = live.body != kNoBody ? BodyOf(live.body) : nullptr) SetBodyUserData(body, 0xE, piece);
    return live.body;
}

void PlaceBody(DWORD handle, const float* place) {
    if (BYTE* body = handle != kNoBody ? BodyOf(handle) : nullptr) SetBodyPlace(body, place, place + 3);
}

// Puts a piece where `numbers` (position, 3 rotation rows) say, its drawing and its collision; makes
// its drawing again if it was taken away. 1 if done, else a reason (see the callers).
int GuardedPlacePiece(BYTE* piece, BYTE* record, BYTE* zone, bool ownBody, const float* numbers) {
    __try {
        memcpy(piece + kPiecePlace, numbers, 12 * sizeof(float));
        LivePiece& live = LivePieceOf(piece, record);
        if (!HasDraws(piece)) {
            if (!ChunkByName(*reinterpret_cast<const char**>(piece))) return -6;
            MakeStitchDraw(piece);  // made where the piece is
        } else {
            PlaceDraws(piece);
        }
        live.hidden = false;
        PlaceBody(BodyFor(piece, zone, ownBody, true), numbers);
        return HasDraws(piece) ? 1 : -7;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -5;
    }
}

int GuardedHidePiece(BYTE* piece, bool ownBody) {
    __try {
        FreeStitchDraw(piece);
        LivePiece& live = LivePieceOf(piece);
        live.hidden = true;
        // Its collision goes as the level's take-down lets a piece's go (0x7755C0: handle, 0, 1, 1); it
        // is made again if the piece is put back. (Put far away instead, a body outside the world's
        // broadphase has the game take the zone down.)
        if (ownBody) {
            DWORD& own = *reinterpret_cast<DWORD*>(piece + kPieceBody);
            if (own != kNoBody && own != 0) FreeBody(own, false, true, true);
            own = kNoBody;
        }
        if (live.body != kNoBody) FreeBody(live.body, false, true, true);
        live.body = kNoBody;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -5;
    }
}

// The level's own piece (list, index) of the zone whose .cterrain_pc is named, checked by its name;
// with its list's header and the zone object. A reason (below 0) if there's none.
int GuardedFindPiece(const char* cterrain, int list, int index, const char* name, BYTE** piece, BYTE** record, BYTE** header, BYTE** zone) {
    __try {
        *record = TerrainRecord(cterrain);
        if (!*record) return -1;
        *header = *reinterpret_cast<BYTE**>(*record + 4);
        BYTE* data = *reinterpret_cast<BYTE**>(*record);
        *zone = data ? *reinterpret_cast<BYTE**>(data + 0xA8) : nullptr;
        if (!*header || list < 0 || list >= 9) return -2;
        BYTE* pieces = *reinterpret_cast<BYTE**>(*header + list * 4);
        const int count = *reinterpret_cast<int*>(*header + 0x24 + list * 4);
        if (!pieces || index < 0 || index >= count) return -3;
        *piece = pieces + index * kPieceSize;
        const char* pieceName = *reinterpret_cast<const char**>(*piece);
        if (!pieceName || _stricmp(pieceName, name) != 0) return -4;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -5;
    }
}

const char* const kPieceWhy[] = {"", "the game has no terrain record of that name", "no such list", "no such piece",
                                 "the piece there has another name (the stitch list changed since the level loaded)",
                                 "the game's code faulted", "its chunk isn't loaded in the game", "the game made no drawing for it"};

bool ReadNumbers(const char* rest, float* numbers, int count) {
    for (int i = 0; i < count; i++) {
        char* next = nullptr;
        numbers[i] = strtof(rest, &next);
        if (next == rest) return false;
        rest = next;
    }
    return true;
}

bool CombinedCollision(BYTE* header) {
    __try {
        return *reinterpret_cast<void**>(header + 0x94) != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

void GuardedFreePiece(BYTE* piece, DWORD body, bool draws);

// Terrain records whose combined collision was swapped for a body per piece (below).
std::vector<BYTE*> g_ownBodyRecords;

// A level whose pieces share one combined collision (header +0x90 parts, their bodies +0x98) can't
// have one piece's collision moved. The first time one of its pieces is moved or taken away live,
// its collision is made as the game makes it when it has no combined one (0x6BBAC0): the combined
// bodies let go (their handles -1, so the take-down skips them; their shapes stay for the unloader),
// and each piece given a body of its own (0x6BC270, told its piece, at +0x38, which the take-down
// lets go). Pieces taken away live get none. Returns the number of bodies made, or -1 if it faulted.
int GuardedOwnBodies(BYTE* header, BYTE* zone) {
    int made = 0;
    __try {
        const int parts = *reinterpret_cast<int*>(header + 0x90);
        DWORD* combined = *reinterpret_cast<DWORD**>(header + 0x98);
        for (int i = 0; combined && i < parts; i++) {
            if (combined[i] != kNoBody) FreeBody(combined[i], false, true, true);
            combined[i] = kNoBody;
        }
        for (int list = 0; list < 9; list++) {
            BYTE* pieces = *reinterpret_cast<BYTE**>(header + list * 4);
            const int count = *reinterpret_cast<int*>(header + 0x24 + list * 4);
            for (int i = 0; pieces && i < count; i++) {
                BYTE* piece = pieces + i * kPieceSize;
                DWORD& own = *reinterpret_cast<DWORD*>(piece + kPieceBody);
                own = kNoBody;
                if (!HasDraws(piece)) continue;  // (taken away)
                BYTE* chunk = ChunkByName(*reinterpret_cast<const char**>(piece));
                if (!chunk) continue;
                own = MakeStitchBody(chunk, zone, piece);
                if (BYTE* body = own != kNoBody ? BodyOf(own) : nullptr) {
                    SetBodyUserData(body, 0xE, piece);
                    made++;
                }
            }
        }
        return made;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Whether the record's pieces each have a body of their own now (made so if they shared one).
bool OwnBodies(BYTE* record, BYTE* header, BYTE* zone) {
    if (!CombinedCollision(header)) return true;
    if (std::find(g_ownBodyRecords.begin(), g_ownBodyRecords.end(), record) != g_ownBodyRecords.end()) return true;
    // Bodies made earlier for its pieces (moved before this): let go, the pieces get their own.
    for (auto& [piece, live] : g_livePieces) {
        if (live.record != record || live.body == kNoBody) continue;
        GuardedFreePiece(piece, live.body, false);
        live.body = kNoBody;
    }
    const int made = GuardedOwnBodies(header, zone);
    g_ownBodyRecords.push_back(record);
    if (made >= 0)
        rfa::LogDetail("Editor link: the level geometry's combined collision swapped for a body per piece (%d), so pieces' collision moves with them", made);
    else
        rfa::Log("Editor link: swapping the level geometry's combined collision for a body per piece FAULTED", made);
    return made >= 0;
}

// editor_stitch <zone>.cterrain_pc <list> <index> <piece name> x y z <9 rotation numbers>: the level's
// own piece moved (or put back). editor_stitch_hide <zone>.cterrain_pc <list> <index> <piece name>: taken away.
void EditorStitchPiece(const std::string& line, bool hide) {
    const size_t skip = hide ? 19 : 14;
    char cterrain[96] = {}, name[96] = {};
    int list = -1, index = -1, used = 0;
    if (sscanf_s(line.c_str() + skip, "%95s %d %d %95s%n", cterrain, static_cast<unsigned>(sizeof(cterrain)), &list, &index, name,
                 static_cast<unsigned>(sizeof(name)), &used) != 4) {
        rfa::Log("Editor link: %s needs a terrain name, list, index and piece name%s", hide ? "editor_stitch_hide" : "editor_stitch",
                 hide ? "" : ", and 12 numbers");
        return;
    }
    float numbers[12];
    if (!hide && !ReadNumbers(line.c_str() + skip + used, numbers, 12)) return;
    BYTE *piece = nullptr, *record = nullptr, *header = nullptr, *zone = nullptr;
    int result = GuardedFindPiece(cterrain, list, index, name, &piece, &record, &header, &zone);
    if (result > 0) {
        const bool ownBody = OwnBodies(record, header, zone);
        result = hide ? GuardedHidePiece(piece, ownBody) : GuardedPlacePiece(piece, record, zone, ownBody, numbers);
    }
    static int done = 0, problems = 0;
    if (result > 0) {
        if (++done <= 5) rfa::LogDetail("Editor link: level geometry piece %s (%s list %d #%d) %s", name, cterrain, list, index, hide ? "taken away" : "moved");
    } else if (++problems <= 10) {
        rfa::LogDetail("Editor link: level geometry piece %s (%s list %d #%d) not %s: %s", name, cterrain, list, index, hide ? "taken away" : "moved",
                 kPieceWhy[-result]);
    }
}

void AnswerEditor(const std::string& name, const char* answer) {
    const std::wstring path = LinkFolder() + L"\\" + std::wstring(name.begin(), name.end()) + L".txt";
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"w") == 0 && file) {
        fputs(answer, file);
        fclose(file);
    }
}

// A new piece of our own: its data (as a stitch list's), an array for its draw instances, its name
// kept. Never freed (its body knows it).
BYTE* NewPiece(const char* name, int flags) {
    BYTE* piece = static_cast<BYTE*>(calloc(1, kPieceSize));
    char* kept = _strdup(name);
    void** draws = static_cast<void**>(calloc(8, sizeof(void*)));
    if (!piece || !kept || !draws) return nullptr;
    *reinterpret_cast<char**>(piece) = kept;
    piece[kPieceFlags] = static_cast<BYTE>(flags);
    *reinterpret_cast<DWORD*>(piece + kPieceBody) = kNoBody;
    *reinterpret_cast<void***>(piece + kPieceDraws) = draws;
    return piece;
}

int GuardedSubChunks(const char* name) {
    __try {
        BYTE* chunk = ChunkByName(name);
        return chunk ? *reinterpret_cast<int*>(chunk + kChunkSubChunks) : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int GuardedZoneOf(const char* cterrain, BYTE** record, BYTE** zone) {
    __try {
        *record = TerrainRecord(cterrain);
        if (!*record) return -1;
        BYTE* data = *reinterpret_cast<BYTE**>(*record);
        *zone = data ? *reinterpret_cast<BYTE**>(data + 0xA8) : nullptr;
        return *zone ? 1 : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -5;
    }
}

// editor_stitch_add <zone>.cterrain_pc <id> <piece name> <flags> x y z <9 rotation numbers>: a piece
// added in the editor made (or, made already, moved). The answer (link\stitch_<id>.txt: made, moved,
// nochunk or failed) tells the editor whether to fall back on a reload.
void EditorAddStitchPiece(const std::string& line) {
    char cterrain[96] = {}, name[96] = {};
    unsigned id = 0;
    int flags = 4, used = 0;
    if (sscanf_s(line.c_str() + 18, "%95s %x %95s %d%n", cterrain, static_cast<unsigned>(sizeof(cterrain)), &id, name,
                 static_cast<unsigned>(sizeof(name)), &flags, &used) != 4) {
        rfa::Log("Editor link: editor_stitch_add needs a terrain name, id, piece name, flags and 12 numbers");
        return;
    }
    float numbers[12];
    if (!ReadNumbers(line.c_str() + 18 + used, numbers, 12)) return;
    char answerName[32];
    sprintf_s(answerName, "stitch_%08X", id);
    BYTE *record = nullptr, *zone = nullptr;
    if (GuardedZoneOf(cterrain, &record, &zone) <= 0) {
        rfa::Log("Editor link: new level geometry piece %s: the game has no zone for %s", name, cterrain);
        AnswerEditor(answerName, "failed");
        return;
    }
    SpawnedPiece* spawned = nullptr;
    for (SpawnedPiece& s : g_spawnedPieces)
        if (s.id == id && s.record == record && _stricmp(s.name.c_str(), name) == 0 && s.flags == flags) spawned = &s;
    // Another piece made before for the same id (the piece was made another chunk, or its flags
    // changed, in the editor): taken away.
    for (SpawnedPiece& s : g_spawnedPieces)
        if (s.id == id && &s != spawned && !LivePieceOf(s.piece).hidden) GuardedHidePiece(s.piece, false);
    const bool made = spawned != nullptr;
    if (!spawned) {
        const int subChunks = GuardedSubChunks(name);
        if (subChunks < 0) {
            rfa::Log("Editor link: new level geometry piece %s: its chunk isn't loaded in the game", name);
            AnswerEditor(answerName, "nochunk");
            return;
        }
        if (subChunks > 1) {  // (the game doesn't make those as stitch pieces either)
            rfa::LogDetail("Editor link: new level geometry piece %s: its chunk has %d sub-chunks; stitch pieces have one", name, subChunks);
            AnswerEditor(answerName, "failed");
            return;
        }
        BYTE* piece = NewPiece(name, flags);
        if (!piece) {
            AnswerEditor(answerName, "failed");
            return;
        }
        *reinterpret_cast<int*>(piece + kPieceDrawCount) = subChunks;
        g_spawnedPieces.push_back({id, piece, record, name, flags});
        spawned = &g_spawnedPieces.back();
    }
    // Pieces of our own always have a body of their own (made for them, as for a combined collision).
    const int result = GuardedPlacePiece(spawned->piece, record, zone, false, numbers);
    static int count = 0;
    if (result > 0 && (!made && ++count <= 10)) rfa::LogDetail("Editor link: made level geometry piece %s (0x%08X)", name, id);
    if (result <= 0) rfa::LogDetail("Editor link: new level geometry piece %s not %s: %s", name, made ? "moved" : "made", kPieceWhy[-result]);
    AnswerEditor(answerName, result > 0 ? (made ? "moved" : "made") : result == -6 ? "nochunk" : "failed");
}

// editor_stitch_drop <id>: a piece added in the editor (made by editor_stitch_add) deleted there.
void EditorDropStitchPiece(const std::string& line) {
    const DWORD id = static_cast<DWORD>(strtoul(line.c_str() + 19, nullptr, 16));
    for (SpawnedPiece& s : g_spawnedPieces) {
        if (s.id != id || LivePieceOf(s.piece).hidden) continue;
        if (GuardedHidePiece(s.piece, false) > 0) rfa::LogDetail("Editor link: level geometry piece %s (0x%08X) taken away", s.name.c_str(), static_cast<unsigned>(id));
    }
}

// The game takes a zone's terrain and level geometry away (0x6BC900, the terrain record: when the level
// unloads or loads again): first the pieces we made and the bodies we made for its pieces go, while
// the chunks they are made of are still there.
constexpr DWORD kTerrainTakeDown = 0x6BC900;
constexpr size_t kTerrainTakeDownPrologue = 6;  // push ebp / mov ebp,esp / sub esp,34h
using TerrainTakeDownFn = void(__cdecl*)(BYTE* record);
TerrainTakeDownFn g_terrainTakeDown;

void GuardedFreePiece(BYTE* piece, DWORD body, bool draws) {
    __try {
        if (draws) FreeStitchDraw(piece);
        if (body != kNoBody) FreeBody(body, false, true, true);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        rfa::LogDetail("Editor link: letting go of a level geometry piece made live faulted");
    }
}

void __cdecl TerrainTakeDown(BYTE* record) {
    int freed = 0;
    for (auto it = g_spawnedPieces.begin(); it != g_spawnedPieces.end();) {
        if (it->record != record) {
            ++it;
            continue;
        }
        DWORD body = kNoBody;
        for (auto& [at, live] : g_livePieces)
            if (at == it->piece) std::swap(body, live.body);  // (not freed again below)
        GuardedFreePiece(it->piece, body, true);
        freed++;
        it = g_spawnedPieces.erase(it);
    }
    for (auto it = g_livePieces.begin(); it != g_livePieces.end();) {
        if (it->second.record != record) {
            ++it;
            continue;
        }
        if (it->second.body != kNoBody) GuardedFreePiece(it->first, it->second.body, false);
        it = g_livePieces.erase(it);
    }
    g_ownBodyRecords.erase(std::remove(g_ownBodyRecords.begin(), g_ownBodyRecords.end(), record), g_ownBodyRecords.end());
    if (freed) rfa::LogDetail("Editor link: %d level geometry pieces made live let go with their zone", freed);
    g_terrainTakeDown(record);
}

// ---- Terrain edits in the running level (editor_terrain <file> <patch>) -----------------------
//
// The game keeps a zone's terrain files in memory as they were read, laid out as in the file
// (their pointers set in place), and draws and collides with them from there:
//   <zone>.cterrain_pc (terrain_load_terrain 0x6B9640; its record by name 0x6BBA90): the terrain
//     data (record +0) inside the file; its ground collision, a Havok compressed height field, is a
//     Havok packfile loaded in place, so its u16 samples are read from the file's memory as the game
//     runs. The zone object (data +0xA8) keeps the field's body (zone +0x4C), made at the zone's
//     corner (zone +0x188) without rotation.
//   <zone>_N.ctmesh_pc (the sub-zones, 0x6B7A60; record by name: 0x6BCEE0 on 0x1855158): its data
//     D (record +0) at a known place in the file; D +0x1C its mesh data (rl_mesh_data: +0x0C vertex
//     count, +0x10 vertex size, +0x2C the vertices, in the g-file's memory), D +0x20 its rl_pc_mesh,
//     whose vertex buffer (+0x50) the device made at load (immutable: 0x00C2B740).
// The editor writes the bytes that differ (a patch file in link\): ranges of the c-file (values only:
// the same layout, so no pointer differs), and for a sub-zone its vertices. They are copied in; a
// sub-zone gets a new vertex buffer made from them (the old one is kept, not released, in case a
// frame being drawn still has it). Answers in link\terrain_<file>.txt: done or failed.
auto const SubzoneRecord = reinterpret_cast<bool(__thiscall*)(void* table, const char* name, BYTE** record)>(0x6BCEE0);
void* const kSubzones = reinterpret_cast<void*>(0x1855158);
auto const RenderDevice = reinterpret_cast<void*(__cdecl*)()>(0xC26500);
int g_terrainBuffersKept = 0;

void AnswerTerrain(const std::string& file, const char* answer) {
    if (strcmp(answer, "done") == 0) rfa::LogDetail("Editor link: terrain %s done", file.c_str());
    else rfa::LogDetail("Editor link: terrain %s %s", file.c_str(), answer);
    const std::wstring path = LinkFolder() + L"\\terrain_" + std::wstring(file.begin(), file.end()) + L".txt";
    FILE* out = nullptr;
    if (_wfopen_s(&out, path.c_str(), L"w") == 0 && out) {
        fputs(answer, out);
        fclose(out);
    }
}

// The patch: "RFTP", version 1, kind (0 terrain, 1 sub-zone), where the data is in the c-file (a
// sub-zone's D), the c-file's size, the range count; each range its offset, length and bytes; then
// (a sub-zone) the vertex count, size and bytes.
struct TerrainPatch {
    unsigned kind = 0, dataAt = 0, size = 0;
    std::vector<std::pair<unsigned, std::vector<BYTE>>> ranges;
    unsigned vertexCount = 0, vertexSize = 0;
    std::vector<BYTE> vertices;
    struct FarMesh { unsigned index, count, size; std::vector<BYTE> vertices; };
    std::vector<FarMesh> farMeshes;  // (the terrain file's far-away meshes: their vertices from the .gterrain_pc)
};

bool ReadTerrainPatch(const std::wstring& path, TerrainPatch& patch) {
    FILE* in = nullptr;
    if (_wfopen_s(&in, path.c_str(), L"rb") != 0 || !in) return false;
    std::vector<BYTE> bytes;
    BYTE buffer[65536];
    for (size_t n; (n = fread(buffer, 1, sizeof(buffer), in)) > 0;) bytes.insert(bytes.end(), buffer, buffer + n);
    fclose(in);
    size_t at = 0;
    auto U32 = [&](unsigned& value) {
        if (at + 4 > bytes.size()) return false;
        memcpy(&value, bytes.data() + at, 4);
        at += 4;
        return true;
    };
    unsigned magic = 0, version = 0, count = 0;
    if (!U32(magic) || magic != 0x50544652 || !U32(version) || version != 1 || !U32(patch.kind) || !U32(patch.dataAt) || !U32(patch.size) || !U32(count)) return false;
    for (unsigned r = 0; r < count; r++) {
        unsigned offset = 0, length = 0;
        if (!U32(offset) || !U32(length) || at + length > bytes.size() || offset + static_cast<unsigned long long>(length) > patch.size) return false;
        patch.ranges.emplace_back(offset, std::vector<BYTE>(bytes.begin() + at, bytes.begin() + at + length));
        at += length;
    }
    unsigned farCount = 0;
    if (patch.kind == 0 && U32(farCount))
        for (unsigned f = 0; f < farCount && f < 9; f++) {
            TerrainPatch::FarMesh mesh{};
            if (!U32(mesh.index) || !U32(mesh.count) || !U32(mesh.size)) return false;
            size_t length = static_cast<size_t>(mesh.count) * mesh.size;
            if (at + length > bytes.size()) return false;
            mesh.vertices.assign(bytes.begin() + at, bytes.begin() + at + length);
            at += length;
            patch.farMeshes.push_back(std::move(mesh));
        }
    if (patch.kind == 1 && U32(patch.vertexCount) && U32(patch.vertexSize)) {
        size_t length = static_cast<size_t>(patch.vertexCount) * patch.vertexSize;
        if (at + length > bytes.size()) return false;
        patch.vertices.assign(bytes.begin() + at, bytes.begin() + at + length);
    }
    return true;
}

// The terrain file's start in memory, from its data (inside it): "TERR", version 42, going back.
BYTE* GuardedTerrainBase(BYTE* data, unsigned size) {
    __try {
        for (BYTE* at = reinterpret_cast<BYTE*>(reinterpret_cast<uintptr_t>(data) & ~uintptr_t(3)); at + size > data && at > data - 0x800000; at -= 4)
            if (*reinterpret_cast<DWORD*>(at) == 0x52524554 && *reinterpret_cast<DWORD*>(at + 4) == 42) return at;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

auto const BodySetPosition = reinterpret_cast<void(__thiscall*)(BYTE* body, const float* position)>(0xACAFF0);
auto const MakeBox = reinterpret_cast<void*(__thiscall*)(void* box, const float* min, const float* max)>(0x4FAA20);
auto const SetMeshBounds = reinterpret_cast<void(__thiscall*)(BYTE* mesh, void* box)>(0x6BDB90);
auto const FreeBox = reinterpret_cast<void(__thiscall*)(void* box)>(0x926130);

struct BufferDesc { UINT width, usage, bind, cpu, misc, stride; };
struct InitialData { const void* bytes; UINT pitch, slicePitch; };

// A mesh's vertices replaced (in its g-file's memory: its mesh data +0x2C, +0x0C count, +0x10 size),
// a new vertex buffer made from them (the old one kept: a frame being drawn may still have it), and
// its bounds set from a box (min, max) as the load does. A reason if it couldn't be; null if done.
const char* NewVertices(BYTE* meshData, BYTE* mesh, unsigned count, unsigned size, const BYTE* bytes, const BYTE* boxMin, const BYTE* boxMax) {
    if (!meshData || !mesh) return "failed: a mesh is missing";
    if (*reinterpret_cast<unsigned*>(meshData + 0x0C) != count || meshData[0x10] != size) return "failed: a mesh's vertices differ from the editor's";
    BYTE* vertices = *reinterpret_cast<BYTE**>(meshData + 0x2C);
    if (!vertices) return "failed: a mesh has no vertices";
    memcpy(vertices, bytes, static_cast<size_t>(count) * size);
    void* device = RenderDevice();
    if (!device) return "failed: no render device";
    BufferDesc desc{count * size, 1, 1, 0, 0, 0};
    InitialData initial{vertices, 0, 0};
    void* made = nullptr;
    using CreateBufferFn = long(__stdcall*)(void* self, const BufferDesc*, const InitialData*, void** buffer);
    auto create = reinterpret_cast<CreateBufferFn>((*reinterpret_cast<void***>(device))[3]);
    if (create(device, &desc, &initial, &made) < 0 || !made) return "failed: a new vertex buffer couldn't be made";
    *reinterpret_cast<void**>(mesh + 0x50) = made;
    g_terrainBuffersKept++;
    alignas(16) BYTE box[0x40] = {};
    if (void* built = MakeBox(box, reinterpret_cast<const float*>(boxMin), reinterpret_cast<const float*>(boxMax))) {
        SetMeshBounds(mesh, built);
        FreeBox(box);
    }
    return nullptr;
}

bool GuardedCopy(BYTE* to, const BYTE* from, size_t length) {
    __try {
        memcpy(to, from, length);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A terrain file's ranges, and the height field's body placed again. A reason if it couldn't be.
const char* GuardedPatchTerrain(const char* name, const TerrainPatch& patch) {
    __try {
        BYTE* record = TerrainRecord(name);
        if (!record) return "failed: the level hasn't that terrain";
        BYTE* data = *reinterpret_cast<BYTE**>(record);
        if (!data) return "failed: no terrain data";
        BYTE* base = GuardedTerrainBase(data, patch.size);
        if (!base) return "failed: the terrain file wasn't found in memory";
        for (size_t r = 0; r < patch.ranges.size(); r++)
            if (!GuardedCopy(base + patch.ranges[r].first, patch.ranges[r].second.data(), patch.ranges[r].second.size())) return "failed: a range couldn't be written";
        // The far-away meshes (terrain data +0x40: nine records of 0x30, +4 its mesh data, +8 its
        // rl_pc_mesh, +0x0C and +0x18 its box: 0x770030), as the sub-zones below: their vertices, a new
        // vertex buffer, their bounds.
        if (BYTE* farRecords = *reinterpret_cast<BYTE**>(data + 0x40))
            for (size_t f = 0; f < patch.farMeshes.size(); f++) {
                const TerrainPatch::FarMesh& mesh = patch.farMeshes[f];
                if (mesh.index >= 9) continue;
                BYTE* record = farRecords + mesh.index * 0x30;
                if (const char* why = NewVertices(*reinterpret_cast<BYTE**>(record + 4), *reinterpret_cast<BYTE**>(record + 8),
                                                  mesh.count, mesh.size, mesh.vertices.data(), record + 0x0C, record + 0x18))
                    return why;
            }
        // The height field's body set where it already is (hkpRigidBody::setPosition, 0xACAFF0, with
        // its own position: its motion at +0xE0, the transform's translation at +0x120), so the world
        // works its box out again from the new heights: else ground raised above, or lowered below,
        // what the level loaded with isn't solid. (Placing it at the zone's corner instead put it
        // outside the world: "Tried to delete a zone ... outside the broadphase".)
        if (BYTE* zone = *reinterpret_cast<BYTE**>(data + 0xA8)) {
            DWORD handle = *reinterpret_cast<DWORD*>(zone + 0x4C);
            if (BYTE* body = handle != kNoBody && handle != 0 ? BodyOf(handle) : nullptr) {
                alignas(16) float position[4];
                memcpy(position, body + 0x120, sizeof(position));
                bool sane = true;
                for (int i = 0; i < 3; i++) sane = sane && position[i] == position[i] && position[i] > -100000 && position[i] < 100000;
                if (sane) BodySetPosition(body, position);
            }
        }
        return "done";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "failed: the game's code faulted";
    }
}


const char* GuardedPatchSubzone(const char* name, const TerrainPatch& patch) {
    __try {
        BYTE* record = nullptr;
        if (!SubzoneRecord(kSubzones, name, &record) || !record) return "failed: the level hasn't that sub-zone";
        BYTE* d = *reinterpret_cast<BYTE**>(record);
        if (!d) return "failed: no sub-zone data";
        BYTE* base = d - patch.dataAt;
        if (*reinterpret_cast<DWORD*>(base) != 0x5A425553) return "failed: the sub-zone file wasn't where expected in memory";
        for (size_t r = 0; r < patch.ranges.size(); r++)
            if (!GuardedCopy(base + patch.ranges[r].first, patch.ranges[r].second.data(), patch.ranges[r].second.size())) return "failed: a range couldn't be written";
        BYTE* meshData = *reinterpret_cast<BYTE**>(d + 0x1C);
        BYTE* mesh = *reinterpret_cast<BYTE**>(d + 0x20);
        if (!meshData || !mesh) return "failed: the sub-zone has no mesh";
        // Its bounds for drawing (the mesh's, +0x10, from the sub-zone's box, D +0x24 and +0x30, as the
        // load sets them: 0x6B7E20): else ground raised above, or lowered below, the box the level
        // loaded with is left out of view now and then (it flickers).
        alignas(16) BYTE box[0x40] = {};
        if (void* made = MakeBox(box, reinterpret_cast<float*>(d + 0x24), reinterpret_cast<float*>(d + 0x30))) {
            SetMeshBounds(mesh, made);
            FreeBox(box);
        }
        if (patch.vertices.empty()) return "done";
        unsigned count = *reinterpret_cast<unsigned*>(meshData + 0x0C), size = meshData[0x10];
        BYTE* vertices = *reinterpret_cast<BYTE**>(meshData + 0x2C);
        if (count != patch.vertexCount || size != patch.vertexSize || !vertices) return "failed: the sub-zone's vertices differ from the editor's";
        if (!GuardedCopy(vertices, patch.vertices.data(), patch.vertices.size())) return "failed: the vertices couldn't be written";
        void* device = RenderDevice();
        if (!device) return "failed: no render device";
        BufferDesc desc{count * size, 1, 1, 0, 0, 0};
        InitialData initial{vertices, 0, 0};
        void* made = nullptr;
        using CreateBufferFn = long(__stdcall*)(void* self, const BufferDesc*, const InitialData*, void** buffer);
        auto create = reinterpret_cast<CreateBufferFn>((*reinterpret_cast<void***>(device))[3]);
        if (create(device, &desc, &initial, &made) < 0 || !made) return "failed: the new vertex buffer couldn't be made";
        *reinterpret_cast<void**>(mesh + 0x50) = made;  // (the old one is kept: see the top)
        g_terrainBuffersKept++;
        return "done";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "failed: the game's code faulted";
    }
}

void EditorTerrain(const std::string& line) {
    char name[128] = {}, patchName[128] = {};
    if (sscanf_s(line.c_str() + 15, "%127s %127s", name, static_cast<unsigned>(sizeof(name)), patchName, static_cast<unsigned>(sizeof(patchName))) != 2) return;
    TerrainPatch patch;
    const std::wstring path = LinkFolder() + L"\\" + std::wstring(patchName, patchName + strlen(patchName));
    if (!ReadTerrainPatch(path, patch)) {
        AnswerTerrain(name, "failed: the patch couldn't be read");
        return;
    }
    _wremove(path.c_str());
    AnswerTerrain(name, patch.kind == 0 ? GuardedPatchTerrain(name, patch) : GuardedPatchSubzone(name, patch));
}

bool RunEditorEdit(const std::string& line) {
    if (line.rfind("editor_terrain ", 0) == 0) {
        EditorTerrain(line);
        return true;
    }
    if (line.rfind("editor_stitch ", 0) == 0 || line.rfind("editor_stitch_hide ", 0) == 0) {
        EditorStitchPiece(line, line.rfind("editor_stitch_hide ", 0) == 0);
        return true;
    }
    if (line.rfind("editor_stitch_add ", 0) == 0) {
        EditorAddStitchPiece(line);
        return true;
    }
    if (line.rfind("editor_stitch_drop ", 0) == 0) {
        EditorDropStitchPiece(line);
        return true;
    }
    if (line.rfind("editor_chunk_group ", 0) == 0) {
        LoadEditorChunkGroup(line);
        return true;
    }
    if (line.rfind("editor_chunk_files ", 0) == 0) {
        LoadEditorChunkFiles(line);
        return true;
    }
    if (line.rfind("editor_chunk ", 0) == 0) {
        char assetList[128] = {}, container[128] = {};
        if (sscanf_s(line.c_str() + 13, "%127s %127s", assetList, static_cast<unsigned>(sizeof(assetList)), container,
                     static_cast<unsigned>(sizeof(container))) == 2)
            LoadEditorChunk(assetList, container);
        return true;
    }
    if (line == "editor_level") {
        // Which level the game has loaded (the name it loads it by, kept in the world at +0x18, which
        // reload_level loads again: 0x4D4D40 → 0x6C87B0) and where the camera and the player are, for
        // the editor's "Game's level" (link\level.txt).
        const char* level = reinterpret_cast<const char*>(kObjectList + 0x18);
        char name[128] = {};
        strncpy_s(name, level, _TRUNCATE);
        BYTE* player = ThePlayer();
        float at[3] = {};
        if (player) memcpy(at, player + 4, sizeof(at));
        const std::wstring path = LinkFolder() + L"\\level.txt";
        FILE* file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"w") == 0 && file) {
            fprintf(file, "%s\n%.3f %.3f %.3f\n%d %.3f %.3f %.3f\n", name, g_cameraPosition[0], g_cameraPosition[1], g_cameraPosition[2],
                    player ? 1 : 0, at[0], at[1], at[2]);
            fclose(file);
        }
        rfa::LogDetail("Editor link: the editor asked which level is loaded: %s", name[0] ? name : "(none)");
        return true;
    }
    if (line == "editor_reload") {
        StartEditorReload();
        return true;
    }
    if (line == "editor_rescan") {
        // The loader looks through overrides\ and edits\ again, for files the editor saved for the
        // first time since the game started (before it has the game reload a level or a table).
        using RescanFn = int(__cdecl*)();
        if (auto rescan = reinterpret_cast<RescanFn>(GetProcAddress(GetModuleHandleW(L"rfa_loader.dll"), "RFALoader_RescanOverrides")))
            rescan();
        else rfa::Log("Editor link: this loader can't look through the overrides again (an older rfa_loader.dll)");
        return true;
    }
    if (line.rfind("editor_create ", 0) == 0) {
        MakeEditorObject(static_cast<DWORD>(strtoul(line.c_str() + 14, nullptr, 0)));
        return true;
    }
    const bool move = line.rfind("editor_move ", 0) == 0, remove = line.rfind("editor_remove ", 0) == 0;
    if (!move && !remove) return false;
    static int moved = 0, removed = 0, missing = 0;
    const char* text = line.c_str() + (move ? 12 : 14);
    char* rest = nullptr;
    const DWORD handle = static_cast<DWORD>(strtoul(text, &rest, 0));
    void* object = FindObject(reinterpret_cast<void*>(kObjectList), nullptr, handle);
    if (!object) {
        if (++missing <= 10) rfa::Log("Editor link: the game has no object 0x%08X now (%s skipped)", static_cast<unsigned>(handle), move ? "move" : "removal");
        return true;
    }
    if (remove) {
        // What a live-made node spawned goes with it.
        if (auto spawned = g_nodeSpawned.find(handle); spawned != g_nodeSpawned.end()) {
            if (FindObject(reinterpret_cast<void*>(kObjectList), nullptr, spawned->second))
                RemoveObject(reinterpret_cast<void*>(kObjectList), nullptr, spawned->second, 0);
            g_nodeSpawned.erase(spawned);
        }
        RemoveObject(reinterpret_cast<void*>(kObjectList), nullptr, handle, 0);
        if (++removed <= 10) rfa::LogDetail("Editor link: removed object 0x%08X", static_cast<unsigned>(handle));
        return true;
    }
    float numbers[12];
    for (float& number : numbers) {
        char* next = nullptr;
        number = strtof(rest, &next);
        if (next == rest) {
            rfa::Log("Editor link: editor_move needs a handle and 12 numbers");
            return true;
        }
        rest = next;
    }
    const auto setPlace = reinterpret_cast<SetPlaceFn>((*static_cast<void***>(object))[10]);
    setPlace(object, nullptr, numbers, numbers + 3, 1);
    if (++moved <= 5) rfa::LogDetail("Editor link: moved object 0x%08X to %.2f, %.2f, %.2f", static_cast<unsigned>(handle), numbers[0], numbers[1], numbers[2]);
    return true;
}

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
        rfa::LogDetail("Editor link: the editor asked where %u objects are (%d found); it asks again while it shows props as in the game",
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
    rfa::LogDetail("Editor link: told the editor the game's light (time %.2f, exposure %.2f to %.2f)", g_timeOfDay,
             RendererFloat(0xAE0), RendererFloat(0xAE4));
}

using FrameUpdateFn = void(__cdecl*)();
FrameUpdateFn g_frameUpdate;

// FrameUpdate callback registration for other mods to run code on the main thread
// (declared in rfa_qol.h, variables and functions defined in global console namespace)
using FrameUpdateCallback = void(__cdecl*)();

// 0x76AA50 (the handshake's "unlock the renderer"), replaced: as the game's, then the editor's commands,
// in the window where the renderer is unlocked.
constexpr DWORD kRenderUnlock = 0x76AA50;
void __cdecl RenderUnlock() {
    if (RenderLocked()) reinterpret_cast<void(__cdecl*)()>(0xD2EA20)();
    ++*reinterpret_cast<int*>(0x1973834);
    if (g_linkFolderThere) RunEditorCommands();
}

void __cdecl FrameUpdate() {
    g_frameUpdate();

    // Call registered FrameUpdate callbacks (from other mods like live_analysis)
    {
        std::lock_guard<std::mutex> lock(console::g_frameUpdateCallbacksMutex);
        for (auto callback : console::g_frameUpdateCallbacks) {
            callback();
        }
    }

    // Commands from the game's menus (Red Faction: Guerrilla > Guerrilla World: level rfg_12_11), run here,
    // on the main thread between frames, not from inside the menu's script.
    std::vector<std::string> menuCommands;
    {
        std::lock_guard<std::mutex> lock(console::g_menuCommandsMutex);
        menuCommands.swap(console::g_menuCommands);
    }
    for (const std::string& command : menuCommands) {
        rfa::LogDetail("Running \"%s\" for the game's menu", command.c_str());
        if (g_runConsoleCommand) g_runConsoleCommand("%s", command.c_str());
    }

    ShowEditor();
    if (g_drawsConsole && g_settings.gameLog) ReadReports();
    // The editor's commands are taken every frame (Live sends them as you drag), the rest ten times a second;
    // the console's files thread looks for them and for the link folder (see ConsoleFiles).
    StartConsoleFiles();
    const bool linkFolderThere = g_linkFolderThere;
    if (linkFolderThere && !RenderLocked()) RunEditorCommands();
    if (!g_chunksLoading.empty()) WatchEditorChunks();
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastCheck < kCheckEveryMs) return;
    g_lastCheck = now;
    if (g_drawsConsole && g_settings.modLog) ReadModLog();
    if (!linkFolderThere) return;  // the editor's link isn't set up
    ContinueEditorReload();
    AnswerWhereWanted();
    AnswerLightingWanted();
}

}  // namespace

namespace console {

// A console command, run as if typed at the start of the next frame (for the game's menus, through the
// loader's rfa_console_command: RFAQol_QueueConsoleCommand).
bool QueueCommand(const char* line) {
    if (!line || !*line || strlen(line) >= 250) return false;
    std::lock_guard<std::mutex> lock(g_menuCommandsMutex);
    g_menuCommands.emplace_back(line);
    return true;
}

void RegisterFrameUpdateCallback(FrameUpdateCallback callback) {
    std::lock_guard<std::mutex> lock(g_frameUpdateCallbacksMutex);
    g_frameUpdateCallbacks.push_back(callback);
}

void UnregisterFrameUpdateCallback(FrameUpdateCallback callback) {
    std::lock_guard<std::mutex> lock(g_frameUpdateCallbacksMutex);
    auto it = std::find(g_frameUpdateCallbacks.begin(), g_frameUpdateCallbacks.end(), callback);
    if (it != g_frameUpdateCallbacks.end()) {
        g_frameUpdateCallbacks.erase(it);
    }
}

}  // namespace console

void console::Init() {
    if (!rfa::game::IsSupportedBuild()) {
        rfa::Log("Not the publicbeta build (it has no developer console); nothing to do");
        return;
    }
    ReadSettings();
    MakeBuildStamp();
    rfa::LogInfo("Build stamp: %s", g_buildStamp.c_str());
    g_consolePrint =reinterpret_cast<ConsolePrintFn>(
        rfa::HookFunction(kConsolePrint, kConsolePrintPrologue, reinterpret_cast<void*>(&ConsolePrint)));
    g_consoleEcho = reinterpret_cast<ConsoleEchoFn>(
        rfa::HookFunction(kConsoleEcho, kConsoleEchoPrologue, reinterpret_cast<void*>(&ConsoleEcho)));
    g_drawsConsole = g_consolePrint && g_consoleEcho && rfa::ReplaceFunction(kConsoleDraw, reinterpret_cast<void*>(&ConsoleDraw));
    if (g_drawsConsole) {
        rfa::LogDetail("The mod draws the console: %d%% of the screen, text x%.2f, typed line x%.2f, %u lines kept%s",
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
    if (g_consoleKeys && g_nextKey && g_mouseMessage)
        rfa::LogDetail("The mouse wheel, Page Up / Page Down, Ctrl+Home and End scroll the console, and Ctrl+V pastes (open it with the ` key)");
    else
        rfa::Log("Could not hook the game's keys or mouse handler; console scrolling and pasting may be off");
    g_runConsoleCommand = reinterpret_cast<RunConsoleCommandFn>(
        rfa::HookFunction(kRunConsoleCommand, kRunConsoleCommandPrologue, reinterpret_cast<void*>(&RunCommandAsText)));
    if (!g_runConsoleCommand) rfa::Log("Could not hook the console's command runner; a %% in a typed line is still read as a format");
    if (g_drawsConsole) {
        if (rfa::ReplaceFunction(kCompleteCommand, reinterpret_cast<void*>(&CompleteCommand)))
            rfa::LogDetail("Tab completes the first word of a console line and lists what each match does");
        else
            rfa::Log("Could not replace the console's Tab completion; it is the game's own");
    }

    if (g_drawsConsole && g_settings.gameLog) {
        if (rfa::HookImport(GetModuleHandleW(nullptr), "KERNEL32.dll", "OutputDebugStringA", &GameDebugString, &g_outputDebugString))
            rfa::LogDetail("The console also shows the game's debug output and error reports, marked [game]");
        else
            rfa::Log("Could not take the game's debug output; the console shows its error reports only");
    }
    if (g_drawsConsole && g_settings.modLog)
        rfa::LogDetail("The console also shows the mods' problems from logs\\rfa_mods.log as they're written");

    g_runScriptFile = reinterpret_cast<RunScriptFileFn>(
        rfa::HookFunction(kRunScriptFile, kRunScriptFilePrologue, reinterpret_cast<void*>(&RunScriptFile)));
    g_terrainTakeDown = reinterpret_cast<TerrainTakeDownFn>(
        rfa::HookFunction(kTerrainTakeDown, kTerrainTakeDownPrologue, reinterpret_cast<void*>(&TerrainTakeDown)));
    if (!g_terrainTakeDown) rfa::Log("Could not hook the game's terrain take-down; level geometry made live may outlive its level");
    g_gameplayStart = reinterpret_cast<GameplayStartFn>(
        rfa::HookFunction(kGameplayStart, kGameplayStartPrologue, reinterpret_cast<void*>(&GameplayStart)));
    if (g_runScriptFile && g_gameplayStart)
        rfa::LogDetail("console.ini: [Startup] applies at startup, [Gameplay] every time gameplay starts");
    else
        rfa::Log("Could not hook the game's startup / gameplay points; console.ini is off");

    ShareWithEditor();
    {
        // (The game's own 0x76AA50 is checked first: push ebp / mov ebp,esp / call 0xD2EC00.)
        const BYTE expected[] = {0x55, 0x8B, 0xEC, 0xE8};
        if (memcmp(reinterpret_cast<void*>(kRenderUnlock), expected, sizeof(expected)) == 0 &&
            rfa::ReplaceFunction(kRenderUnlock, reinterpret_cast<void*>(&RenderUnlock)))
            rfa::LogDetail("Editor link: the editor's commands run while the renderer is unlocked");
        else
            rfa::Log("Editor link: could not take the renderer's unlock; the editor's commands wait for frames it is unlocked in");
    }
    g_frameUpdate = reinterpret_cast<FrameUpdateFn>(
        rfa::HookFunction(kFrameUpdate, kFrameUpdatePrologue, reinterpret_cast<void*>(&FrameUpdate)));
    if (g_frameUpdate)
        rfa::LogDetail("Editor link: the camera is shared every frame, and bin\\link is watched for the editor's commands");
    else
        rfa::Log("Could not hook the game's frame update; the editor link is off");
    g_toneMapStep = reinterpret_cast<ToneMapStepFn>(
        rfa::HookFunction(kToneMapStep, kToneMapStepPrologue, reinterpret_cast<void*>(&ToneMapStep)));
    if (!g_toneMapStep) rfa::Log("Could not hook the renderer's tone mapping; the editor won't be told the game's exposure");
}
