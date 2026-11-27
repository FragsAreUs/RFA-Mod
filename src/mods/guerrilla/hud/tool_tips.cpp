// Guerrilla's tool tips (see tool_tips.h), by its rules as Re-Mars-tered has them:
//   - raising one (ui_tool_tips_add_tip RM 0x8C6820): not while the player is dead or missing; with tool tips switched
//     off only the Always_Show ones ([GuerrillaHud] ToolTips=0; Guerrilla's options menu had it); a tip whose Play_Times
//     it has already played (0: never) isn't queued, and is counted as played as it's queued; Run_Paused ones ignore the
//     delay; at most four wait at once (its Tool_tip_queue), else the new one is dropped;
//   - showing them (tool_tips_queue_process 0x8D9F00), the first in the queue: once its delay is up, the player alive,
//     and the game not paused (unless it's Run_Paused). A Pause tip pauses the game, stays at least a second, then shows
//     "PRESS <key> TO CONTINUE" (HM_PRESS_START) and closes when that key is let go (Guerrilla's tooltip_resume; here
//     [GuerrillaKeys] TipContinue); the game goes on. Any other tip stays its Duration (counting only while the game
//     runs, unless Run_Paused), then goes. The next one waits for the next update;
//   - what the box shows (ui_tool_tip_process_info 0x8EB900, its HelpUpdate item): the title, the text, the icon (the
//     one it was raised with, else its own), where on the screen (1-5: NE NW SE SW centre), its arrow (6: none), the
//     duration in seconds (-1 for a Pause tip), the hint, and whether it pauses.
//   - Minor tips (Minor_tip: Guerrilla's hud_message_add_minor) are shown as the others for now: the only one,
//     Jumping, never plays (Play_Times 0).
// Not yet: Guerrilla's checks for dialogue playing (a Pause tip waits for it), the scope, a camera move and a cutscene;
// its handbook's tool tips page (each tip played opens its entry there); the tip's sound (its interface bank).
// Guerrilla's texts name buttons as [BUTTON:<action>]: each is given its key's name here (ours from [GuerrillaKeys]
// and [GuerrillaVehicles]; Armageddon's own actions by its PC defaults), two side by side joined by "/".
#define RFA_MOD_NAME "guerrilla_hud"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "hud_api.h"
#include "rfa_game.h"
#include "keys.h"
#include "tool_tips.h"
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct Tip {
    std::string name, title, text, icon, animation;
    int screen = 5, arrow = 6, plays = 0, durationMs = 5000;
    bool pause = false, runPaused = false, minor = false, profile = false, always = false;
    int played = 0;  // times queued so far (Guerrilla's Tool_tips_times_displayed)
};

struct Instance {
    Tip* tip = nullptr;
    std::string icon;            // the icon it was raised with ("" for its own)
    double timeLeft = -1.0;      // seconds
    bool running = false;
    ULONGLONG showAt = 0;        // its delay is up then
};

constexpr size_t kQueueSlots = 4;  // Guerrilla's Tool_tip_queue
std::vector<Tip> g_tips;
std::string g_continueText = "PRESS [BUTTON:MENU_START] TO CONTINUE";
std::deque<Instance> g_queue;
int g_version = 0;
bool g_pausedByTip = false;      // our pause count taken for the Pause tip up now
bool g_continueDown = false;     // the continue key went down while it could close (it closes as the key comes up)
bool g_read = false;
std::recursive_mutex g_lock;

auto const PauseGame = reinterpret_cast<void(__cdecl*)(int, int, int, int)>(0x520280);    // as the console's "pause"
auto const UnpauseGame = reinterpret_cast<void(__cdecl*)(int, int)>(0x520310);
auto const GameIsPaused = reinterpret_cast<bool(__cdecl*)()>(0x520260);                   // its pause count above 0

std::wstring TipsPath() { return rfa::RootDir() + L"\\mods\\guerrilla_hud\\tool_tips.txt"; }
std::wstring PlayedPath() { return rfa::RootDir() + L"\\mods\\guerrilla_hud\\tool_tips_played.txt"; }

std::vector<std::string> Fields(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        const size_t bar = line.find('|', start);
        fields.push_back(line.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (bar == std::string::npos) return fields;
        start = bar + 1;
    }
}

// "\n" (the two characters) a new line.
std::string Unescape(const std::string& field) {
    std::string text;
    for (size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 1 < field.size() && (field[i + 1] == 'n' || field[i + 1] == 'N')) {
            text += '\n';
            ++i;
        } else {
            text += field[i];
        }
    }
    return text;
}

Tip* Find(const char* name) {
    if (!name) return nullptr;
    for (Tip& t : g_tips)
        if (_stricmp(t.name.c_str(), name) == 0) return &t;
    return nullptr;
}

void SavePlayed() {
    FILE* file = nullptr;
    if (_wfopen_s(&file, PlayedPath().c_str(), L"wb") != 0 || !file) return;
    fprintf(file, "; Guerrilla's tool tips played so far (the guerrilla_hud mod): name|times. Delete this file to see them again.\n");
    for (const Tip& t : g_tips)
        if (t.played > 0) fprintf(file, "%s|%d\n", t.name.c_str(), t.played);
    fclose(file);
}

void ReadFile() {
    g_read = true;
    FILE* file = nullptr;
    if (_wfopen_s(&file, TipsPath().c_str(), L"rb") != 0 || !file) {
        rfa::Log("Tool tips: mods\\guerrilla_hud\\tool_tips.txt is missing (run editor\\GuerrillaBuilder.exe); no tool tips");
        return;
    }
    char line[4096];
    while (fgets(line, sizeof line, file)) {
        std::string text = line;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        if (text.empty() || text[0] == ';') continue;
        const std::vector<std::string> f = Fields(text);
        if (f[0] == "tip" && f.size() >= 11) {
            Tip t;
            t.name = f[1];
            t.title = Unescape(f[2]);
            t.text = Unescape(f[3]);
            t.icon = f[4];
            t.animation = f[5];
            t.screen = atoi(f[6].c_str());
            t.arrow = atoi(f[7].c_str());
            t.plays = atoi(f[8].c_str());
            t.durationMs = atoi(f[9].c_str());
            const std::string& flags = f[10];
            t.pause = flags.size() > 0 && flags[0] == '1';
            t.runPaused = flags.size() > 1 && flags[1] == '1';
            t.minor = flags.size() > 2 && flags[2] == '1';
            t.profile = flags.size() > 3 && flags[3] == '1';
            t.always = flags.size() > 4 && flags[4] == '1';
            g_tips.push_back(t);
        } else if (f[0] == "text" && f.size() >= 3 && f[1] == "continue") {
            g_continueText = Unescape(f[2]);
        }
    }
    fclose(file);
    if (_wfopen_s(&file, PlayedPath().c_str(), L"rb") == 0 && file) {
        while (fgets(line, sizeof line, file)) {
            std::string text = line;
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            if (text.empty() || text[0] == ';') continue;
            const std::vector<std::string> f = Fields(text);
            if (f.size() >= 2)
                if (Tip* t = Find(f[0].c_str())) t->played = atoi(f[1].c_str());
        }
        fclose(file);
    }
    int played = 0;
    for (const Tip& t : g_tips) played += t.played > 0 ? 1 : 0;
    rfa::LogDetail("Tool tips: %u of Guerrilla's, %d played before; %s", static_cast<unsigned>(g_tips.size()), played,
             rfa::IniInt(L"GuerrillaHud", L"ToolTips", 1) ? "on" : "off ([GuerrillaHud] ToolTips=0: only the always-shown ones)");
}

// ---- Guerrilla's button names in the tips' texts

// A key's name as the texts show it: upper case.
std::string Upper(std::string text) {
    for (char& c : text) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return text;
}

std::string KeyName(int code) {
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) return std::string(1, static_cast<char>(code));
    if (code == VK_TAB) return "TAB";
    if (code == VK_RETURN) return "ENTER";
    if (code == VK_SPACE) return "SPACEBAR";
    if (code >= VK_F1 && code <= VK_F12) return "F" + std::to_string(code - VK_F1 + 1);
    return "";
}

std::string Setting(const wchar_t* section, const wchar_t* name, const wchar_t* fallback) {
    const std::wstring value = rfa::IniString(section, name, fallback);
    std::string narrow;
    for (wchar_t c : value) narrow += static_cast<char>(c);
    return Upper(narrow);
}

// The key of one of Guerrilla's button actions, by name.
std::string ButtonName(const std::string& action) {
    const std::string a = Upper(action);
    struct OurKey { const char* action; keys::Action key; };
    static const OurKey ours[] = {
        {"MAP", keys::Map}, {"USE_BACKPACK", keys::Backpack}, {"MENU_UP_NO_STICK", keys::Up}, {"MENU_DOWN_NO_STICK", keys::Down},
        {"MENU_UP", keys::Up}, {"MENU_DOWN", keys::Down}, {"MENU_LEFT", keys::Left}, {"MENU_RIGHT", keys::Right},
        {"MENU_START", keys::TipContinue}, {"TOOLTIP_RESUME", keys::TipContinue}, {"GPS_MARKER", keys::GpsMarker},
    };
    for (const OurKey& k : ours)
        if (a == k.action) {
            const std::string name = KeyName(keys::Key(k.key));
            return name.empty() ? "(NO KEY)" : name;
        }
    // guerrilla_vehicles' driving keys ([GuerrillaVehicles], its defaults Guerrilla's PC ones).
    if (a == "VEH_ACCELERATE") return Setting(L"GuerrillaVehicles", L"Accelerate", L"W");
    if (a == "VEH_BRAKE") return Setting(L"GuerrillaVehicles", L"Brake", L"S");
    if (a == "VEH_HANDBRAKE") return Setting(L"GuerrillaVehicles", L"Handbrake", L"Spacebar");
    if (a == "VEH_CHANGE_SEAT") return Setting(L"GuerrillaVehicles", L"ChangeSeat", L"Mouse3");
    // The weapon cabinet's slots: the keys 1 2 3 (its screen's PC keys).
    if (a == "WM_LEFT") return "1";
    if (a == "WM_UP") return "2";
    if (a == "WM_RIGHT") return "3";
    // Armageddon's own actions, by its PC defaults (not read from the player's controls yet).
    struct Fixed { const char* action; const char* key; };
    static const Fixed fixed[] = {
        {"PRIMARY_ATTACK", "LEFT MOUSE"}, {"VEH_FIREARM", "LEFT MOUSE"}, {"VEH_TANK_FIRE_PRI", "LEFT MOUSE"},
        {"VEH_TANK_FIRE_SEC", "RIGHT MOUSE"}, {"ZOOM", "RIGHT MOUSE"}, {"ZOOM_IN_OUT", "MOUSE WHEEL"},
        {"WEAPON_MENU", "MOUSE WHEEL"}, {"SPRINT", "SHIFT"}, {"JUMP", "SPACEBAR"}, {"MOVE", "W A S D"},
        {"STRAFE", "A D"}, {"TANK_STEER", "A D"}, {"TANK_STEER_VERT", "W S"}, {"CAM_ROTATE", "MOUSE"},
        {"GAMEPLAY_PAUSE", "ESC"}, {"OF_ACTION", "E"},
    };
    for (const Fixed& f : fixed)
        if (a == f.action) return f.key;
    // Anything else by the action's own words (MELEE_ATTACK: "MELEE ATTACK").
    std::string words = a;
    for (char& c : words)
        if (c == '_') c = ' ';
    return words;
}

// A tip's text with its [BUTTON:<action>] tags given their keys.
std::string WithKeys(const std::string& text) {
    std::string out;
    size_t i = 0;
    bool lastWasKey = false;
    while (i < text.size()) {
        if (text[i] == '[' && _strnicmp(text.c_str() + i, "[BUTTON:", 8) == 0) {
            const size_t end = text.find(']', i);
            if (end != std::string::npos) {
                if (lastWasKey) out += "/";
                out += ButtonName(text.substr(i + 8, end - i - 8));
                i = end + 1;
                lastWasKey = true;
                continue;
            }
        }
        out += text[i++];
        lastWasKey = false;
    }
    return out;
}

void Unpause() {
    if (g_pausedByTip) {
        g_pausedByTip = false;
        UnpauseGame(1, 1);
    }
}

// The first in the queue done with: off the screen (its handbook entry would open here).
void Finish() {
    if (!g_queue.empty()) {
        if (g_queue.front().running) RFA_LOG_DETAIL_FIRST(100, "Tool tip %s closed", g_queue.front().tip->name.c_str());
        g_queue.pop_front();
    }
    Unpause();
    g_continueDown = false;
    ++g_version;
}

}  // namespace

namespace tool_tips {

std::string KeysNamed(const std::string& text) { return WithKeys(text); }

void Read() {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    if (!g_read) ReadFile();
}

bool Add(const char* name, int delayMs, const char* icon) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    if (!g_read) ReadFile();
    Tip* tip = Find(name);
    if (!tip) {
        RFA_LOG_FIRST(20, "Tool tip %s: not one of Guerrilla's (tool_tips.txt)", name ? name : "(none)");
        return false;
    }
    if (!rfa::IniInt(L"GuerrillaHud", L"ToolTips", 1) && !tip->always) return true;
    for (const Instance& i : g_queue)
        if (i.tip == tip) return true;  // already waiting or up
    if (tip->plays >= 0 && tip->plays <= tip->played) return true;  // played as often as it plays (0: never)
    if (g_queue.size() >= kQueueSlots) {
        RFA_LOG_DETAIL_FIRST(20, "Tool tip %s dropped: %u are waiting already (Guerrilla's most)", tip->name.c_str(), static_cast<unsigned>(kQueueSlots));
        return true;
    }
    ++tip->played;
    SavePlayed();
    Instance instance;
    instance.tip = tip;
    instance.icon = icon ? icon : "";
    instance.showAt = GetTickCount64() + (tip->runPaused || delayMs <= 0 ? 0 : static_cast<ULONGLONG>(delayMs));
    g_queue.push_back(instance);
    RFA_LOG_DETAIL_FIRST(100, "Tool tip %s queued (time %d of %d; shows after %d ms)", tip->name.c_str(), tip->played, tip->plays,
                  tip->runPaused || delayMs <= 0 ? 0 : delayMs);
    return true;
}

void Remove(const char* name) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    for (size_t i = 0; i < g_queue.size(); ++i)
        if (g_queue[i].tip && _stricmp(g_queue[i].tip->name.c_str(), name) == 0) {
            if (i == 0) {
                Finish();
            } else {
                g_queue.erase(g_queue.begin() + static_cast<std::ptrdiff_t>(i));
            }
            return;
        }
}

bool Played(const char* name) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    const Tip* tip = Find(name);
    return tip && tip->played > 0;
}

bool HoldsGame() {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    return !g_queue.empty() && g_queue.front().running && g_queue.front().tip->pause;
}

void Step(double seconds, bool playerAlive) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    if (g_queue.empty()) return;
    Instance& now = g_queue.front();
    const Tip& tip = *now.tip;
    const bool paused = GameIsPaused();
    if (!now.running) {
        if (paused && !tip.runPaused) return;
        if (GetTickCount64() < now.showAt || !playerAlive) return;
        now.running = true;
        if (tip.pause) {
            // Guerrilla paused the game (or, already paused, held its input) and kept the box a second at least.
            if (!paused) {
                PauseGame(1, 1, 0, 0);
                g_pausedByTip = true;
            }
            now.timeLeft = 1.0;
        } else {
            now.timeLeft = tip.durationMs / 1000.0;
        }
        g_continueDown = false;
        ++g_version;
        RFA_LOG_DETAIL_FIRST(100, "Tool tip %s shown%s", tip.name.c_str(), tip.pause ? " (the game paused until the continue key)" : "");
        return;
    }
    if (now.timeLeft > 0.0 && playerAlive) {
        if (paused && !tip.pause && !tip.runPaused) return;  // stands still while the game is paused
        now.timeLeft -= seconds;
        if (now.timeLeft <= 0.0 && tip.pause) ++g_version;  // its hint shows now
        return;
    }
    if (tip.pause && playerAlive) {
        const int key = keys::Key(keys::TipContinue);
        const bool down = key && (GetAsyncKeyState(key) & 0x8000) != 0;
        if (down) {
            g_continueDown = true;
            return;
        }
        if (!g_continueDown) return;
    }
    Finish();
}

void LevelShutdown() {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    g_queue.clear();
    Unpause();
    g_continueDown = false;
    ++g_version;
}

// rfg_tool_tip() -> version, visible, title, text, icon, animation, anchor, arrow, duration, hint, is_paused, game_paused
//   What Guerrilla's help box shows now (its HelpUpdate item's ten values, after the version, which goes up with each
//   change), and whether the game is paused now (its tweens stand still then).
int __cdecl LuaToolTip(rfa::game::lua_State* L) {
    using namespace rfa::game;
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    lua_pushnumber(L, g_version);
    const bool visible = !g_queue.empty() && g_queue.front().running;
    lua_pushboolean(L, visible);
    if (!visible) return 2;
    const Instance& now = g_queue.front();
    const Tip& tip = *now.tip;
    lua_pushstring(L, WithKeys(tip.title).c_str());
    lua_pushstring(L, WithKeys(tip.text).c_str());
    lua_pushstring(L, now.icon.empty() ? tip.icon.c_str() : now.icon.c_str());
    lua_pushstring(L, tip.animation.c_str());
    lua_pushnumber(L, tip.screen);
    lua_pushnumber(L, tip.arrow);
    lua_pushnumber(L, tip.pause ? -1.0 : tip.durationMs / 1000.0);
    lua_pushstring(L, tip.pause && now.timeLeft <= 0.0 ? WithKeys(g_continueText).c_str() : "");
    lua_pushboolean(L, tip.pause);
    lua_pushboolean(L, GameIsPaused());
    return 12;
}

void Register(rfa::game::lua_State* L) {
    rfa::game::lua_register(L, "rfg_tool_tip", LuaToolTip);
}

}  // namespace tool_tips

// Other mods raise a tip by its name (see tool_tips.h).
bool GuerrillaHud_ToolTip(const char* name, int delayMs, const char* icon) {
    return guerrilla_hud::Running() && tool_tips::Add(name, delayMs, icon);
}

bool GuerrillaHud_ToolTipPlayed(const char* name) { return guerrilla_hud::Running() && name && tool_tips::Played(name); }
