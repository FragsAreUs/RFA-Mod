// Improvements to the developer console of the publicbeta build (opened with the ` key).
//
// Mouse wheel: scrolls the console's output while it is open, 3 lines per notch, the same way
// Page Up / Page Down do. While the console is open the wheel doesn't reach the game, so it no
// longer switches weapons behind it.
//
// The console keeps its last 256 output lines in a ring buffer. While you scroll back it draws the
// lines upward from a chosen bottom line and shows "[in scroll mode - press END to leave scroll
// mode]" underneath; End (or scrolling all the way down) returns to the newest output.
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
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr int kConsoleLines = 256;
constexpr int kLinesPerNotch = 3;
constexpr int kLineHeight = 15;  // the console font's line spacing in pixels

struct ConsoleLine {
    char text[0x84];
};

// The console's state in the publicbeta exe.
auto& g_lines = *reinterpret_cast<ConsoleLine(*)[kConsoleLines]>(0x28FF1F8);
auto& g_nextLine = *reinterpret_cast<int*>(0x28FBA44);    // where the next output line will be written
auto& g_scrollLine = *reinterpret_cast<int*>(0x28FBA40);  // bottom line while scrolled back; -1 = newest output
auto& g_consoleOpen = *reinterpret_cast<BYTE*>(0x28FF1CA);
auto& g_screenHeight = *reinterpret_cast<int*>(0x28FF1EC);
auto& g_consoleScreenShare = *reinterpret_cast<double*>(0xF784D0);  // 0.4: the console covers 40% of the screen

// The game's handler for mouse window messages (buttons, wheel, raw input): cdecl (message,
// wParam, lParam). It adds wheel movement to a total that weapon switching reads.
constexpr DWORD kMouseMessage = 0xCDF220;
constexpr size_t kMouseMessagePrologue = 6;  // push ebp / mov ebp,esp / sub esp,0Ch

int Wrap(int line) { return (line % kConsoleLines + kConsoleLines) % kConsoleLines; }

int NewestLine() { return Wrap(g_nextLine - 1); }

// Once the ring buffer is full, the slot the next line goes into still holds the oldest line.
int StoredLines() { return g_lines[g_nextLine].text[0] ? kConsoleLines : g_nextLine; }

// Output lines that fit above the input line and the scroll-mode notice.
int VisibleLines() {
    const int lines = static_cast<int>(g_screenHeight * g_consoleScreenShare) / kLineHeight - 2;
    return lines > 1 ? lines : 1;
}

// How many lines the bottom of the view is above the newest line (0 when following new output).
int LinesScrolledBack() { return g_scrollLine < 0 ? 0 : Wrap(NewestLine() - g_scrollLine); }

void ScrollBack(int lines) {
    const int furthest = StoredLines() - VisibleLines();  // the oldest line then sits at the top
    if (furthest <= 0) return;                             // everything fits on screen already
    int back = LinesScrolledBack() + lines;
    if (back > furthest) back = furthest;
    g_scrollLine = Wrap(NewestLine() - back);
}

void ScrollForward(int lines) {
    if (g_scrollLine < 0) return;
    const int back = LinesScrolledBack() - lines;
    g_scrollLine = back > 0 ? Wrap(NewestLine() - back) : -1;
}

using MouseMessageFn = void(__cdecl*)(UINT, WPARAM, LPARAM);
MouseMessageFn g_mouseMessage;
int g_wheel = 0;  // wheel movement not yet turned into whole notches (high-resolution wheels)

void __cdecl MouseMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_MOUSEWHEEL && g_consoleOpen) {
        g_wheel += GET_WHEEL_DELTA_WPARAM(wParam);
        for (; g_wheel >= WHEEL_DELTA; g_wheel -= WHEEL_DELTA) ScrollBack(kLinesPerNotch);
        for (; g_wheel <= -WHEEL_DELTA; g_wheel += WHEEL_DELTA) ScrollForward(kLinesPerNotch);
        RFA_LOG_FIRST(3, "Scrolled the console with the mouse wheel");
        return;  // the game doesn't see it
    }
    if (!g_consoleOpen) g_wheel = 0;
    g_mouseMessage(message, wParam, lParam);
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
    g_mouseMessage = reinterpret_cast<MouseMessageFn>(
        rfa::HookFunction(kMouseMessage, kMouseMessagePrologue, reinterpret_cast<void*>(&MouseMessage)));
    rfa::Log(g_mouseMessage ? "Mouse wheel scrolls the console (open it with the ` key)"
                            : "Could not hook the game's mouse handler; console scrolling is off");

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
