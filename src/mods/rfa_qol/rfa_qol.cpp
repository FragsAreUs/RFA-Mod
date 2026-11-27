// rfa_qol.dll: RFA-Mod's quality-of-life mods in one DLL, so mods\ holds fewer files. Its parts,
// each in a source file of its own:
//   borderless      borderless fullscreen and a clean alt-tab             [Borderless]
//   console         the developer console, console.ini, the editor's link [Console]
//   fov             a wider field of view                                 [FOV]
//   video_settings  video settings kept in video_settings.cfg             (no settings)
//   spawner         Spawn in the in-game menus: vehicles, items, weapons  (no settings)
//                   and the vehicles you spawned
//   limits          Limits in the in-game menus: how full the engine's    (no settings)
//                   fixed tables are
//   graphics        Graphics in the in-game menus: the ported world's     [GuerrillaWorld]
//                   picture settings, saved as you change them
//   streaming       a fix for a race in the game's zone stream-out         (no settings)
//                   (two worker threads; a crash when zones stream out)
//   animations      the .anim_pc files of mods\animations, in place of    (no settings)
//                   the game's animations of their names, or new ones
//   tables          the .xtbl files of mods\tables, in place of the        (no settings)
//                   tables the game reads while it starts, or new ones
//   ruin_ai         enemies that fight in Ruin levels (editor-made ones)  [Ruin]
//   live analysis   a pipe for the MCP servers (live_analysis_mcp.exe):    [Mcp]
//                   only when [Mcp] Enabled=1 (off by default; Options > Experimentals
//                   > MCP servers turns it on and off while the game runs)
// Each works as it did when it was a DLL of its own: its settings section in rfa_mods.ini, its own
// switch under [Mods] (borderless=, console=, fov=, video_settings=, spawner=, limits=, graphics=, streaming=, animations=, tables=; 0 = off, on when missing) and
// its own name in the log. They start in the order they loaded as separate DLLs.
//
// Those old DLLs (borderless.dll, console.dll, fov.dll, video_settings.dll) may still be in mods\
// when an update was copied over an older install; the loader skips them while this DLL is there.
#define RFA_MOD_NAME "rfa_qol"
#include "rfa_mod.h"
#include "rfa_qol.h"

namespace {

using InitFn = void (*)();

// Catches a crash in one part's Init, so it ends up in the log and the other parts still start.
bool CallInit(InitFn init) {
    __try {
        init();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Starts a part unless its switch under [Mods] is 0. Returns whether it is running.
bool StartPart(const wchar_t* name, InitFn init) {
    if (!rfa::IniInt(L"Mods", name, 1)) {
        rfa::LogDetail("Skipping %ls (disabled in [Mods])", name);
        return false;
    }
    if (!CallInit(init)) {
        rfa::Log("%ls crashed while starting", name);
        return false;
    }
    return true;
}

bool g_consoleRunning = false;
bool g_fovRunning = false;
bool g_spawnerRunning = false;
bool g_limitsRunning = false;
bool g_graphicsRunning = false;
bool g_ruinAiRunning = false;
bool g_liveAnalysisRunning = false;

// The live analysis pipe, on or off as [Mcp] Enabled says (and [Mods] rfa_live_analysis= allows).
void FollowMcpSwitch() {
    const bool wanted = rfa::IniInt(L"Mcp", L"Enabled", 0) != 0;
    if (wanted && !g_liveAnalysisRunning) {
        g_liveAnalysisRunning = StartPart(L"rfa_live_analysis", live_analysis::Init);
    } else if (!wanted && g_liveAnalysisRunning) {
        live_analysis::Shutdown();
        g_liveAnalysisRunning = false;
        rfa::Log("Live analysis is off now ([Mcp] Enabled=0): the pipe is closed");
    } else if (!wanted) {
        rfa::Log("Live analysis is off ([Mcp] Enabled=0): no pipe is opened");
    }
}

}  // namespace

extern "C" __declspec(dllexport) void __cdecl RFAMod_Init() {
    // The parts' routine lines (rfa::LogDetail) follow the loader's switch: [Loader] Log=.
    rfa::ReadLogSwitch(L"Loader");
    StartPart(L"borderless", borderless::Init);
    g_consoleRunning = StartPart(L"console", console::Init);
    g_fovRunning = StartPart(L"fov", fov::Init);
    StartPart(L"video_settings", video_settings::Init);
    g_spawnerRunning = StartPart(L"spawner", spawner::Init);
    g_limitsRunning = StartPart(L"limits", limits::Init);
    g_graphicsRunning = StartPart(L"graphics", graphics::Init);
    StartPart(L"streaming", streaming::Init);
    StartPart(L"animations", animations::Init);
    StartPart(L"tables", tables::Init);
    g_ruinAiRunning = StartPart(L"ruin_ai", ruin_ai::Init);
    // The live analysis pipe lets a program on this PC read and change the running game, for the MCP
    // servers. It is opened only when the player turns the MCP servers on ([Mcp] Enabled=1, or Options >
    // Experimentals > MCP servers, which opens and closes it at once).
    FollowMcpSwitch();
}

// A console command for the next frame, from the loader's rfa_console_command (the main menu's Guerrilla
// World uses it). 0 if the console part is off (it runs them) or the line is too long.
extern "C" __declspec(dllexport) int __cdecl RFAQol_QueueConsoleCommand(const char* line) {
    return g_consoleRunning && console::QueueCommand(line) ? 1 : 0;
}

// The game's menus changed a setting: fov and ruin_ai listen, and [Mcp] Enabled opens or closes the pipe.
extern "C" __declspec(dllexport) void __cdecl RFAMod_SettingChanged(const char* section, const char* key) {
    if (g_fovRunning) fov::SettingChanged(section, key);
    if (g_ruinAiRunning) ruin_ai::SettingChanged(section, key);
    if (_stricmp(section, "Mcp") == 0 && _stricmp(key, "Enabled") == 0) FollowMcpSwitch();
}

// The in-game menus (rfa_menu.h): the spawner's, the limits' and the graphics'.
extern "C" __declspec(dllexport) void __cdecl RFAMod_Menu(const RFAMenu* menu) {
    if (g_spawnerRunning) spawner::Menu(menu);
    if (g_limitsRunning) limits::Menu(menu);
    if (g_graphicsRunning) graphics::Menu(menu);
}
