// Guerrilla's tool tips (tool_tips.cpp): the help boxes its game raised as things first happened, shown one at a time
// in its document rfg_ui_hud_help, by its rules (RM ui_tool_tips_add_tip 0x8C6820, tool_tips_queue_process 0x8D9F00,
// ui_tool_tip_process_info 0x8EB900). Part of guerrilla_hud. The tips come from your copy of Guerrilla
// (mods\guerrilla_hud\tool_tips.txt, written by GuerrillaBuilder.exe); how many times each has played is kept in
// mods\guerrilla_hud\tool_tips_played.txt (one list for every save, as the handbook's).
//
// The other parts raise one through hud_api.h:
//   bool GuerrillaHud_ToolTip(const char* name, int delayMs, const char* icon)
// name: a tip's Name in tool_tips.xtbl ("Locker", "EDF_Control", ...); delayMs: how long to wait before it shows
// (Guerrilla's second argument); icon: a picture in place of the tip's own, or null. False if the tip isn't known.
#pragma once
#include <windows.h>

namespace rfa::game { struct lua_State; }

namespace tool_tips {

void Read();                                                      // tool_tips.txt and the counts, as the mod starts
bool Add(const char* name, int delayMs = 0, const char* icon = nullptr);  // ui_tool_tips_add_tip
void Remove(const char* name);                                    // ui_tool_tips_remove_tip
bool Played(const char* name);                                    // shown at least once (ui_tool_tips_tip_has_been_played)
void Step(double seconds, bool playerAlive);                      // each HUD update in our world
void LevelShutdown();                                             // a level load: the queue emptied, the game unpaused
bool HoldsGame();                                                 // a tip that paused the game is up
void Register(rfa::game::lua_State* L);                           // rfg_tool_tip()

}  // namespace tool_tips
