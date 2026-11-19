// guerrilla_kiosks: what its parts share (see guerrilla_kiosks.cpp).
#pragma once
#include <windows.h>

#include <string>
#include <vector>

namespace kiosks {

constexpr DWORD kWorld = 0x1870630;  // the world object

extern bool g_log;  // [GuerrillaKiosks] Log=

const char* WorldName();                // the level open now ("rfg" in the ported world)
bool InOurWorld();                      // one of [GuerrillaKiosks] Worlds=
BYTE* ObjectFromHandle(DWORD handle);   // the object, or null (none, or being destroyed)
BYTE* Player();                         // the local player, or null
int AlertLevel();                       // Guerrilla's alert level from guerrilla_campaign: 0 green .. 3 red (0 without it)
const std::wstring& ModFolder();        // mods\guerrilla_kiosks

namespace tables {

// One of kiosk_screens.xtbl's pictures: its name and its effects for a large and a small screen (Guerrilla's
// kiosk_screen_effect; the effects are the world's, "rfg_" + Guerrilla's name, by number: -1 none).
struct Screen {
    std::string name, largeName, smallName;
    int large = -1, small = -1;
};

// One of kiosk_lines.xtbl's lines (Guerrilla's kiosk_line).
struct Line {
    std::string name;         // the voice line (kio_...)
    int minReplays = 1;       // Min_replay_count (at least 1)
    bool startsUnlocked = false;
    std::string mission, activity;  // Unlocked_by_mission / Unlocked_by_activity
    int screen = -1;          // Screen_Effect (an index into Screens()), -1 none
    int screenDelayMs = 0;    // Screen_Effect_Delay
};

// pa_defaults.xtbl's kiosk settings (Guerrilla's public_address_defaults_table_load), and the two the exe holds.
struct Defaults {
    std::string persona = "KIOSK", stationId, musicIntro, musicLoop, musicOutro;
    int idleScreen = -1, activeScreen = -1, alertScreen = -1, rfIdleScreen = -1;
    int inactiveDelayMs = 15000, inactiveDelayLongMs = 120000, sessionMinLines = 3, sessionMaxLines = 5;
    int musicLoopDelayMs = 2360, lineDelayMs = 1500;
    float maxDistance = 50.0f;     // Kiosk_max_dist_sq 2500 (x and z)
    int minAvailableLines = 5;     // Kiosk_min_available_lines
};

// A kiosk the world places (mods\guerrilla_kiosks\kiosks.txt).
struct Kiosk {
    std::string zone, gameplay, props;
    DWORD handle = 0xFFFFFFFF, parent = 0xFFFFFFFF;
    float at[3] = {};
};

bool Load();  // reads the mod's folder; false (and says why) if the kiosks can't run
const std::vector<Screen>& Screens();
const std::vector<Line>& Lines();
const Defaults& Settings();
const std::vector<Kiosk>& Kiosks();
void FindEffects();  // each screen's effects by number, once the game's effect table is read (again at each try)

}  // namespace tables

namespace screens {

void Update(float seconds);  // each frame of play in our worlds
void Forget();               // outside our worlds: what was put on the screens is forgotten
// While a broadcast plays: the picture every kiosk shows (an index into tables::Screens()), -1 for none (idle or
// alert, by the alert level). Guerrilla's Kiosk_current_active_screen_effect with Kiosk_is_active.
void SetBroadcastScreen(int screen);

}  // namespace screens

namespace broadcast {

void Init();                 // the lines' state (kiosk_save.ini) and the built sounds (kiosk_sounds.txt)
void Update(float seconds);  // each frame of play in our worlds
void Forget();               // outside our worlds
bool Active(DWORD* handle, float* place);  // the kiosk broadcasting now, if any
int UnlockActivity(const char* activity);  // the lines an activity unlocks when won; how many
int UnlockMission(const char* mission);    // the lines a mission unlocks when won; how many
int UnlockLine(const char* line);          // one line (the script action kiosk_unlock_line); 1 if unlocked

}  // namespace broadcast

}  // namespace kiosks
