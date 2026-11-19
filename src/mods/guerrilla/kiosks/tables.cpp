// guerrilla_kiosks: Guerrilla's kiosk tables and the world's kiosks, from mods\guerrilla_kiosks\ (written there by
// editor\GuerrillaBuilder.exe when it builds the world, from the player's own copy of Guerrilla; never shipped).
//
// - pa_defaults.xtbl (Guerrilla's public_address_defaults_table_load, RM 0xB23E50): the kiosks' persona, the station
//   ID line, the three music cues, the screens shown idle, while broadcasting, at alert and after the final mission,
//   and the timings. Kiosk_max_dist_sq (2500: 50 m) and Kiosk_min_available_lines (5) are in Guerrilla's exe.
// - kiosk_screens.xtbl (kiosk_screen_table_load, RM 0xB23C40): up to 16 pictures, each a Large_Effect and a
//   Small_Effect (effects.xtbl names; ours are "rfg_" + the name).
// - kiosk_lines.xtbl (kiosk_line_table_load, RM 0xB23D00): up to 256 lines.
// - kiosks.txt: the kiosks the world places, a line each: zone handle parent gameplay_props props x y z.
#define RFA_MOD_NAME "guerrilla_kiosks"
#include "rfa_mod.h"
#include "kiosks.h"

#include <cstdio>
#include <cstdlib>

namespace kiosks::tables {
namespace {

constexpr size_t kMostScreens = 16;  // Guerrilla's Kiosk_screen_effects
constexpr size_t kMostLines = 256;   // Guerrilla's Kiosk_lines

std::vector<Screen> g_screens;
std::vector<Line> g_lines;
Defaults g_defaults;
std::vector<Kiosk> g_kiosks;

// An effect's number by its name in the game's effect table (0x4EF370; -1 none).
using EffectNumberFn = int(__cdecl*)(const char* name);
auto const EffectNumber = reinterpret_cast<EffectNumberFn>(0x4EF370);

bool ReadText(const wchar_t* name, std::string& text) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, (ModFolder() + L"\\" + name).c_str(), L"rb") != 0 || !file) return false;
    char buffer[16384];
    size_t got;
    text.clear();
    while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, got);
    fclose(file);
    return true;
}

// The table's entries part (<Table> ... </Table>), leaving its description out.
std::string TablePart(const std::string& text) {
    const size_t from = text.find("<Table>");
    if (from == std::string::npos) return std::string();
    const size_t to = text.find("</Table>", from);
    return text.substr(from + 7, to == std::string::npos ? std::string::npos : to - from - 7);
}

// Each <tag> ... </tag> block in order.
std::vector<std::string> Blocks(const std::string& text, const char* tag) {
    std::vector<std::string> blocks;
    const std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
    for (size_t at = text.find(open); at != std::string::npos; at = text.find(open, at)) {
        const size_t end = text.find(close, at);
        if (end == std::string::npos) break;
        blocks.push_back(text.substr(at + open.size(), end - at - open.size()));
        at = end + close.size();
    }
    return blocks;
}

// The text of the first <tag> in the block, trimmed ("" if none).
std::string Value(const std::string& block, const char* tag) {
    const std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
    const size_t at = block.find(open);
    if (at == std::string::npos) return std::string();
    const size_t end = block.find(close, at);
    if (end == std::string::npos) return std::string();
    std::string value = block.substr(at + open.size(), end - at - open.size());
    const size_t first = value.find_first_not_of(" \t\r\n"), last = value.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string() : value.substr(first, last - first + 1);
}

int IntValue(const std::string& block, const char* tag, int fallback) {
    const std::string value = Value(block, tag);
    return value.empty() ? fallback : atoi(value.c_str());
}

int ScreenIndex(const std::string& name) {
    for (size_t i = 0; i < g_screens.size(); ++i)
        if (_stricmp(g_screens[i].name.c_str(), name.c_str()) == 0) return static_cast<int>(i);
    return -1;
}

bool LoadScreens() {
    std::string text;
    if (!ReadText(L"kiosk_screens.xtbl", text)) return false;
    for (const std::string& block : Blocks(TablePart(text), "Kiosk_Screen")) {
        if (g_screens.size() >= kMostScreens) break;
        Screen screen;
        screen.name = Value(block, "Name");
        screen.largeName = Value(block, "Large_Effect");
        screen.smallName = Value(block, "Small_Effect");
        if (!screen.name.empty()) g_screens.push_back(screen);
    }
    return true;
}

bool LoadDefaults() {
    std::string text;
    if (!ReadText(L"pa_defaults.xtbl", text)) return false;
    const std::vector<std::string> entries = Blocks(TablePart(text), "PA_Defaults");
    if (entries.empty()) return false;
    const std::string& entry = entries.front();
    Defaults& d = g_defaults;
    if (std::string persona = Value(entry, "Kiosk_persona"); !persona.empty()) d.persona = persona;
    d.stationId = Value(entry, "Kiosk_Station_ID");
    d.musicIntro = Value(entry, "Kiosk_Music_Stinger");
    d.musicLoop = Value(entry, "Kiosk_Music_Loop");
    d.musicOutro = Value(entry, "Kiosk_Music_Out");
    d.idleScreen = ScreenIndex(Value(entry, "Idle_Screen"));
    d.activeScreen = ScreenIndex(Value(entry, "Active_Screen"));
    d.alertScreen = ScreenIndex(Value(entry, "Alert_Screen"));
    d.rfIdleScreen = ScreenIndex(Value(entry, "RF_Idle_Screen"));
    // Other_Settings, as Guerrilla's reader takes them (its Kiosk_Music_Loop_Delay is the one there, not Foleys').
    const std::vector<std::string> other = Blocks(entry, "Other_Settings");
    const std::string& o = other.empty() ? entry : other.front();
    d.inactiveDelayMs = IntValue(o, "Kiosk_inactive_delay", d.inactiveDelayMs);
    d.inactiveDelayLongMs = IntValue(o, "Kiosk_inactive_delay_long", d.inactiveDelayLongMs);
    d.sessionMinLines = IntValue(o, "Kiosk_Session_Min_Lines", d.sessionMinLines);
    d.sessionMaxLines = IntValue(o, "Kiosk_session_max_lines", d.sessionMaxLines);
    d.musicLoopDelayMs = IntValue(o, "Kiosk_music_loop_delay", d.musicLoopDelayMs);
    d.lineDelayMs = IntValue(o, "Kiosk_Line_Delay", d.lineDelayMs);
    return true;
}

bool LoadLines() {
    std::string text;
    if (!ReadText(L"kiosk_lines.xtbl", text)) return false;
    for (const std::string& block : Blocks(TablePart(text), "Kiosk_Line")) {
        if (g_lines.size() >= kMostLines) break;
        Line line;
        line.name = Value(block, "Name");
        if (line.name.empty()) continue;
        line.minReplays = IntValue(block, "Min_replay_count", 1);
        if (line.minReplays <= 0) line.minReplays = 1;  // as Guerrilla's reader
        line.startsUnlocked = _stricmp(Value(block, "Starts_unlocked").c_str(), "True") == 0;
        line.mission = Value(block, "Unlocked_by_mission");
        line.activity = Value(block, "Unlocked_by_activity");
        line.screen = ScreenIndex(Value(block, "Screen_Effect"));
        line.screenDelayMs = IntValue(block, "Screen_Effect_Delay", 0);
        g_lines.push_back(line);
    }
    return true;
}

bool LoadKiosks() {
    std::string text;
    if (!ReadText(L"kiosks.txt", text)) return false;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(start, end - start);
        start = end + 1;
        char zone[64], gameplay[96], props[96];
        unsigned long handle, parent;
        Kiosk kiosk;
        if (sscanf_s(line.c_str(), "%63s %lx %lx %95s %95s %f %f %f", zone, static_cast<unsigned>(sizeof(zone)), &handle, &parent, gameplay,
                     static_cast<unsigned>(sizeof(gameplay)), props, static_cast<unsigned>(sizeof(props)), &kiosk.at[0], &kiosk.at[1], &kiosk.at[2]) != 8)
            continue;
        kiosk.zone = zone;
        kiosk.handle = handle;
        kiosk.parent = parent;
        kiosk.gameplay = gameplay;
        kiosk.props = props;
        g_kiosks.push_back(kiosk);
    }
    return true;
}

}  // namespace

bool Load() {
    if (!LoadScreens() || !LoadDefaults() || !LoadKiosks()) {
        rfa::Log("mods\\guerrilla_kiosks has no kiosk_screens.xtbl, pa_defaults.xtbl or kiosks.txt (build the world with "
                 "editor\\GuerrillaBuilder.exe); Guerrilla's kiosks stay dark");
        return false;
    }
    const bool lines = LoadLines();
    unsigned radios = 0;
    for (const Kiosk& k : g_kiosks) radios += _stricmp(k.props.c_str(), "-") == 0 || k.gameplay.find("radio") != std::string::npos;
    rfa::Log("%u kiosks read from mods\\guerrilla_kiosks\\kiosks.txt (%u of them radios); %u screens from kiosk_screens.xtbl (idle %s, "
             "broadcasting %s, alert %s); %u lines from kiosk_lines.xtbl%s",
             static_cast<unsigned>(g_kiosks.size()), radios, static_cast<unsigned>(g_screens.size()),
             g_defaults.idleScreen >= 0 ? g_screens[g_defaults.idleScreen].name.c_str() : "none",
             g_defaults.activeScreen >= 0 ? g_screens[g_defaults.activeScreen].name.c_str() : "none",
             g_defaults.alertScreen >= 0 ? g_screens[g_defaults.alertScreen].name.c_str() : "none",
             static_cast<unsigned>(g_lines.size()), lines ? "" : " (none: no kiosk_lines.xtbl)");
    return true;
}

const std::vector<Screen>& Screens() { return g_screens; }
const std::vector<Line>& Lines() { return g_lines; }
const Defaults& Settings() { return g_defaults; }
const std::vector<Kiosk>& Kiosks() { return g_kiosks; }

void FindEffects() {
    static bool said = false;
    unsigned found = 0, wanted = 0;
    std::string missing;
    for (Screen& screen : g_screens) {
        for (auto [name, number] : {std::pair<const std::string*, int*>{&screen.largeName, &screen.large}, {&screen.smallName, &screen.small}}) {
            if (name->empty()) continue;
            ++wanted;
            if (*number < 0) *number = EffectNumber(("rfg_" + *name).c_str());
            if (*number >= 0) ++found;
            else if (missing.size() < 400) missing += (missing.empty() ? "" : ", ") + *name;
        }
    }
    if (!said) {
        said = true;
        rfa::Log("Screens: %u of the %u screen effects are in the game's effect table%s%s", found, wanted, missing.empty() ? "" : "; missing: ",
                 missing.c_str());
    }
}

}  // namespace kiosks::tables
