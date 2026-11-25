// The parts of rfa_qol.dll (see rfa_qol.cpp), one source file each. A part starts with its Init,
// which rfa_qol.cpp calls when the part is on, in place of the RFAMod_Init it had as a DLL of its own.
#pragma once

namespace borderless {
void Init();
}

namespace console {
void Init();

// FrameUpdate callback registration for other mods
using FrameUpdateCallback = void(__cdecl*)();
void RegisterFrameUpdateCallback(FrameUpdateCallback callback);
void UnregisterFrameUpdateCallback(FrameUpdateCallback callback);
// Runs a console command at the start of the next frame (RFAQol_QueueConsoleCommand). False if it's too long.
bool QueueCommand(const char* line);
}

namespace fov {
void Init();
// A setting changed in the game's menus (RFAMod_SettingChanged).
void SettingChanged(const char* section, const char* key);
}  // namespace fov

namespace video_settings {
void Init();
}

struct RFAMenu;
namespace spawner {
void Init();
// Its part of the in-game menus (RFAMod_Menu).
void Menu(const RFAMenu* menu);
}  // namespace spawner

namespace limits {
void Init();
// Its part of the in-game menus (RFAMod_Menu).
void Menu(const RFAMenu* menu);
}  // namespace limits

namespace graphics {
void Init();
// Its part of the in-game menus (RFAMod_Menu).
void Menu(const RFAMenu* menu);
}  // namespace graphics

// A fix for a race in the game's zone stream-out (streaming.cpp). No settings.
namespace streaming {
void Init();
}

// Animations from mods\animations, in place of the game's or new (animations.cpp). No settings.
namespace animations {
void Init();
}

// Enemies that fight in Ruin levels ([Ruin] HostileAI=; ruin_ai.cpp).
namespace ruin_ai {
void Init();
// A setting changed in the game's menus (RFAMod_SettingChanged).
void SettingChanged(const char* section, const char* key);
}  // namespace ruin_ai

// Live analysis mod: named pipe interface for external debugging/profiling tools.
namespace live_analysis {
void Init();
// Closes the pipe (Options > Experimentals > MCP servers turned off while the game runs); Init opens it again.
void Shutdown();
}
