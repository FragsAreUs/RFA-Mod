// Red Faction: Guerrilla's people's voices, for the world ported from Guerrilla: each person speaks as one of their
// character's personas, by Guerrilla's rules for each line.
//
// What it plays is the builder's (editor\GuerrillaBuilder.exe, when it builds the world): one bank of every persona
// (rfgv_voices: every free-roam line of each, each line's variations played in turn, 3D at the speaker with the
// line's distances), which the game loads at boot (Armageddon's bank list holds 300 banks at most, so one bank for
// all; guerrilla_world gives Wwise's pool the room), and mods\guerrilla_voices\voices.txt (the personas, their lines,
// the lines' rules per persona type and the characters' personas). Should the bank be out, the mod asks for it.
//
// Guerrilla's way (human::play_voice_line, RM 0xA9A8F0, and human_voice_persona::can_play_line_internal, RM 0xA31AF0),
// a line doesn't play when:
// - the speaker's persona hasn't the line (status 3);
// - its Local_Cooldown runs: a timer of the persona's (7);
// - its Global_Cooldown runs: a timer of the line's for every persona of the speaker's type (8);
// - its Play_Chance says no;
// - the speaker is saying a line of the same or a higher priority (a higher one stops the line playing).
// It plays after a delay from its Default_Min_Delay to its Default_Max_Delay, on the speaker's own sound object
// (human +0x1158), so it follows them.
//
// For the other mods (the people and the alert level say when a person speaks): GuerrillaVoices_Play, _Prepare,
// _Stop, _Playing and _Persona, below.
//
// [GuerrillaVoices] in rfa_mods.ini: Enabled= (1; 0 leaves the game alone), Log= (0; 1 writes each line played or
// refused).
#define RFA_MOD_NAME "guerrilla_voices"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "voices_api.h"
#include "rfa_game.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

namespace voices {
namespace {

// The game's sounds: 0xB846E0 plays an event on a sound object (with a delay) and gives the playing sound's handle;
// 0xB85110 stops a sound by its handle; 0xB84D20 is 0 once a sound has ended.
using PlaySoundEventFn = int(__cdecl*)(DWORD soundObject, DWORD eventId, DWORD* handle, int unused, int delayMs);
auto const PlaySoundEvent = reinterpret_cast<PlaySoundEventFn>(0xB846E0);
using StopSoundFn = int(__cdecl*)(DWORD* handle);
auto const StopSound = reinterpret_cast<StopSoundFn>(0xB85110);
using SoundGameObjectFn = DWORD(__cdecl*)(DWORD handle);
auto const SoundGameObject = reinterpret_cast<SoundGameObjectFn>(0xB84D20);

// The game's banks, by their Wwise id (audio_banks' wwise_id), as its own personas' are: 0x5396D0 loads one that is
// listed and not loaded; the bank manager (0x1303F48, thiscall 0xB8F3C0) says how one stands.
using BankFn = void(__cdecl*)(DWORD bankId);
auto const LoadBank = reinterpret_cast<BankFn>(0x5396D0);
using BankStateFn = int(__fastcall*)(void* manager, void* unused, DWORD bankId);
auto const BankState = reinterpret_cast<BankStateFn>(0xB8F3C0);
constexpr DWORD kBankManager = 0x1303F48;
constexpr int kBankNotLoaded = 0x64, kBankLoaded = 0x66;  // 0x65 while it loads

// A human: its handle (+0x68), its character (human_info +0x4FC, whose name is at +0x24), its sound object (+0x1158).
constexpr DWORD kObjectHandle = 0x68, kHumanInfo = 0x4FC, kInfoName = 0x24, kHumanSoundObject = 0x1158;

constexpr ULONGLONG kSpeakerRestMs = 300000;  // a speaker not heard of for this long is forgotten

bool g_log = false;

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return text;
}

// Wwise's id of a name: FNV-1 (32 bits) of its lower-case letters.
DWORD WwiseNameId(const std::string& name) {
    DWORD h = 2166136261u;
    for (char c : name) h = (h * 16777619u) ^ static_cast<BYTE>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return h;
}

struct Rule {
    int localMs = 0, globalMs = 0, chance = 100, minDelayMs = 0, maxDelayMs = 0, priority = 3;
};

struct Persona {
    std::string name, type, bank;
    DWORD bankId = 0;
    bool asked = false;        // its bank asked for (it should be in from boot)
    ULONGLONG lastUsed = 0;
    std::unordered_map<std::string, int> lines;           // lower-case line → its variations
    std::unordered_map<std::string, ULONGLONG> localUntil;  // lower-case line → when its Local_Cooldown ends
};

struct Speaker {
    int persona = -1;
    DWORD playing = 0;  // the line's sound handle
    int priority = -1;
    std::string line;
    ULONGLONG seen = 0;
};

std::vector<Persona> g_personas;
std::unordered_map<std::string, int> g_personaIndex;               // lower-case persona → index
std::unordered_map<std::string, Rule> g_rules;                     // lower-case "line|type" → rule
std::unordered_map<std::string, ULONGLONG> g_globalUntil;          // lower-case "line|type" → when its Global_Cooldown ends
std::unordered_map<std::string, std::vector<int>> g_characters;    // lower-case character → its personas
std::unordered_map<DWORD, Speaker> g_speakers;                     // by the human's handle
ULONGLONG g_nextTidy = 0;

bool ReadTable() {
    const std::wstring path = rfa::RootDir() + L"\\mods\\guerrilla_voices\\voices.txt";
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"r") != 0 || !file) return false;
    char text[2048];
    while (fgets(text, sizeof(text), file)) {
        std::vector<std::string> w;
        char* context = nullptr;
        for (char* p = strtok_s(text, " \t\r\n", &context); p; p = strtok_s(nullptr, " \t\r\n", &context)) w.emplace_back(p);
        if (w.empty()) continue;
        if (w[0] == "persona" && w.size() >= 5) {
            Persona p;
            p.name = w[1];
            p.type = w[2];
            p.bank = w[3];
            p.bankId = static_cast<DWORD>(strtoul(w[4].c_str(), nullptr, 10));
            g_personaIndex[Lower(p.name)] = static_cast<int>(g_personas.size());
            g_personas.push_back(std::move(p));
        } else if (w[0] == "has" && w.size() >= 4) {
            auto found = g_personaIndex.find(Lower(w[1]));
            if (found != g_personaIndex.end()) g_personas[found->second].lines[Lower(w[2])] = atoi(w[3].c_str());
        } else if (w[0] == "rule" && w.size() >= 9) {
            Rule r;
            r.localMs = atoi(w[3].c_str());
            r.globalMs = atoi(w[4].c_str());
            r.chance = atoi(w[5].c_str());
            r.minDelayMs = atoi(w[6].c_str());
            r.maxDelayMs = atoi(w[7].c_str());
            r.priority = atoi(w[8].c_str());
            g_rules[Lower(w[1] + "|" + w[2])] = r;
        } else if (w[0] == "character" && w.size() >= 3) {
            std::vector<int>& list = g_characters[Lower(w[1])];
            for (size_t i = 2; i < w.size(); ++i) {
                auto found = g_personaIndex.find(Lower(w[i]));
                if (found != g_personaIndex.end()) list.push_back(found->second);
            }
        }
    }
    fclose(file);
    return !g_personas.empty();
}

int StateOf(const Persona& p) { return BankState(reinterpret_cast<void*>(kBankManager), nullptr, p.bankId); }

bool Playing(DWORD handle) { return handle != 0 && SoundGameObject(handle) != 0; }

// Forget the speakers not heard of for a while; at most once every 5 s, from whichever call comes.
void Tidy(ULONGLONG now) {
    if (now < g_nextTidy) return;
    g_nextTidy = now + 5000;
    for (auto it = g_speakers.begin(); it != g_speakers.end();)
        it = now - it->second.seen > kSpeakerRestMs && !Playing(it->second.playing) ? g_speakers.erase(it) : std::next(it);
}

// The speaker for a person (their persona taken once, from their character's list, as Guerrilla takes one at random),
// or null if they aren't one of Guerrilla's characters with a free-roam persona.
Speaker* SpeakerOf(const BYTE* human, ULONGLONG now) {
    if (!human) return nullptr;
    const DWORD handle = *reinterpret_cast<const DWORD*>(human + kObjectHandle);
    auto found = g_speakers.find(handle);
    if (found != g_speakers.end()) {
        found->second.seen = now;
        return found->second.persona >= 0 ? &found->second : nullptr;
    }
    Speaker s;
    s.seen = now;
    const BYTE* info = *reinterpret_cast<const BYTE* const*>(human + kHumanInfo);
    const char* character = info ? *reinterpret_cast<const char* const*>(info + kInfoName) : nullptr;
    if (character) {
        auto list = g_characters.find(Lower(character));
        if (list != g_characters.end() && !list->second.empty())
            s.persona = list->second[(handle * 2654435761u >> 16) % list->second.size()];
    }
    Speaker& kept = g_speakers[handle] = s;
    return kept.persona >= 0 ? &kept : nullptr;
}

// Whether a persona's bank is in; asks for it if not.
bool Ready(Persona& p, ULONGLONG now) {
    p.lastUsed = now;
    const int state = StateOf(p);
    if (state == kBankLoaded) return true;
    if (state == kBankNotLoaded) {
        LoadBank(p.bankId);
        if (g_log && !p.asked) rfa::LogDetail("Voices: loading %s's bank %s", p.name.c_str(), p.bank.c_str());
        p.asked = true;
    }
    return false;
}

}  // namespace

// The statuses of GuerrillaVoices_Play (Guerrilla's play_voice_status where it has one).
enum Status {
    kPlayed = 0,
    kLoading = 1,      // the persona's bank is being loaded (asked for): try again in a moment
    kNoLine = 3,       // the persona hasn't the line
    kLocalCooldown = 7,
    kGlobalCooldown = 8,
    kChance = 9,       // Play_Chance said no
    kBusy = 10,        // saying a line of the same or a higher priority
    kNoPersona = 11,   // not one of Guerrilla's characters with a free-roam persona
    kNoSoundObject = 12,
    kNotPlayed = 13,   // the game didn't play the event
};

int Play(const BYTE* human, const char* lineName, int priority) {
    const ULONGLONG now = GetTickCount64();
    Tidy(now);
    Speaker* speaker = SpeakerOf(human, now);
    if (!speaker || !lineName) return kNoPersona;
    Persona& p = g_personas[speaker->persona];
    const std::string line = Lower(lineName);
    auto has = p.lines.find(line);
    if (has == p.lines.end()) return kNoLine;
    if (!Ready(p, now)) return kLoading;
    const std::string ruleKey = Lower(line + "|" + p.type);
    const auto ruleFound = g_rules.find(ruleKey);
    const Rule rule = ruleFound != g_rules.end() ? ruleFound->second : Rule{};
    const int wanted = priority >= 0 ? priority : rule.priority;
    if (p.localUntil[line] > now) return kLocalCooldown;
    if (g_globalUntil[ruleKey] > now) return kGlobalCooldown;
    if (rule.chance < 100 && rand() % 100 >= rule.chance) return kChance;
    if (Playing(speaker->playing)) {
        if (wanted <= speaker->priority) return kBusy;
        StopSound(&speaker->playing);  // Guerrilla's human::stop_voice for a line of a higher priority
    }
    const DWORD soundObject = *reinterpret_cast<const DWORD*>(human + kHumanSoundObject);
    if (!soundObject) return kNoSoundObject;
    const int delay = rule.minDelayMs + (rule.maxDelayMs > rule.minDelayMs ? rand() % (rule.maxDelayMs - rule.minDelayMs + 1) : 0);
    DWORD handle = 0;
    PlaySoundEvent(soundObject, WwiseNameId("RFGV_" + p.name + "_" + lineName), &handle, 0, delay);
    if (!handle) {
        RFA_LOG_FIRST(5, "Voices: %s's line %s didn't play (the game gave no handle)", p.name.c_str(), lineName);
        return kNotPlayed;
    }
    p.localUntil[line] = now + delay + rule.localMs;
    g_globalUntil[ruleKey] = now + delay + rule.globalMs;
    speaker->playing = handle;
    speaker->priority = wanted;
    speaker->line = line;
    if (g_log) rfa::LogDetail("Voices: %s says %s (priority %d, in %d ms)", p.name.c_str(), lineName, wanted, delay);
    return kPlayed;
}

int Prepare(const BYTE* human) {
    const ULONGLONG now = GetTickCount64();
    Tidy(now);
    Speaker* speaker = SpeakerOf(human, now);
    if (!speaker) return kNoPersona;
    return Ready(g_personas[speaker->persona], now) ? kPlayed : kLoading;
}

Speaker* Known(const BYTE* human) {
    if (!human) return nullptr;
    auto found = g_speakers.find(*reinterpret_cast<const DWORD*>(human + kObjectHandle));
    return found != g_speakers.end() && found->second.persona >= 0 ? &found->second : nullptr;
}

}  // namespace voices

void guerrilla_voices::Init() {
    using namespace voices;
    if (!rfa::game::IsSupportedBuild()) {
        rfa::Log("Not the publicbeta build; nothing to do");
        return;
    }
    if (rfa::IniInt(L"GuerrillaVoices", L"Enabled", 1) == 0) {
        rfa::Log("Off (Enabled=0 in [GuerrillaVoices])");
        return;
    }
    g_log = rfa::ReadLogSwitch(L"GuerrillaVoices");
    srand(static_cast<unsigned>(time(nullptr)));
    if (!ReadTable()) {
        rfa::Log("No mods\\guerrilla_voices\\voices.txt (build the world with editor\\GuerrillaBuilder.exe): no voices");
        return;
    }
    size_t lines = 0;
    for (const Persona& p : g_personas) lines += p.lines.size();
    rfa::Log("%u personas (%u lines between them), %u characters, %u rules from voices.txt; their bank loads with the game's at boot",
             static_cast<unsigned>(g_personas.size()), static_cast<unsigned>(lines), static_cast<unsigned>(g_characters.size()),
             static_cast<unsigned>(g_rules.size()));
}

// ---- For the other mods (human: the person's object, as the game's object_from_handle gives it)

// Plays one of Guerrilla's lines (rfg_voice_lines.xtbl's name: voc_pain, voc_panic, com_ally_combat_flee, ...) on a
// person, by Guerrilla's rules for it; priority -1 takes the line's own (0-9). Returns voices::Status: 0 played, 1 the
// persona's bank is loading (asked for; try again shortly), 3 the persona hasn't the line, 7 / 8 its local / global
// cooldown runs, 9 its chance said no, 10 the person is saying a line of the same or a higher priority, 11 not one of
// Guerrilla's characters with a free-roam persona, 12 no sound object, 13 the game didn't play it.
int GuerrillaVoices_Play(const void* human, const char* line, int priority) {
    if (!guerrilla_voices::Running()) return voices::kNoPersona;
    return voices::Play(static_cast<const BYTE*>(human), line, priority);
}

// Gets a person's persona ready (its bank loading), e.g. as they come near: 0 ready, 1 loading, 11 no persona.
int GuerrillaVoices_Prepare(const void* human) {
    if (!guerrilla_voices::Running()) return voices::kNoPersona;
    return voices::Prepare(static_cast<const BYTE*>(human));
}

// Stops the line a person is saying: 1 if one was playing.
int GuerrillaVoices_Stop(const void* human) {
    if (!guerrilla_voices::Running()) return 0;
    voices::Speaker* s = voices::Known(static_cast<const BYTE*>(human));
    if (!s || !voices::Playing(s->playing)) return 0;
    voices::StopSound(&s->playing);
    s->playing = 0;
    return 1;
}

// Whether a person is saying a line: 1 or 0.
int GuerrillaVoices_Playing(const void* human) {
    if (!guerrilla_voices::Running()) return 0;
    voices::Speaker* s = voices::Known(static_cast<const BYTE*>(human));
    return s && voices::Playing(s->playing) ? 1 : 0;
}

// A person's persona (GMM01, ETM03, ...), or null if they have none (yet: it is taken at their first call).
const char* GuerrillaVoices_Persona(const void* human) {
    if (!guerrilla_voices::Running()) return nullptr;
    voices::Speaker* s = voices::Known(static_cast<const BYTE*>(human));
    return s ? voices::g_personas[s->persona].name.c_str() : nullptr;
}
