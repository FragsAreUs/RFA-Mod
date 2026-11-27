// rfa_sound.dll: RFA-Mod's own sound engine in the game (src\audio\, on OpenAL Soft), playing the sound banks named in
// [Audio] Banks= (ours, rfg*, by default) while Wwise goes on playing every other one as before. It is off unless
// [Audio] OwnPlayer=1: then nothing of the game's is touched. The plan is in docs\game-notes.md, "Wwise → OpenAL Soft:
// the hybrid's build plan" (this is its step 2), "the plan widened to Armageddon's own sounds" and "B1 built".
//
// Banks= names our banks (loose in overrides\build\pc\cache\) and Armageddon's own sound effects' banks too (B1): those
// are read as the game reads them, from the game's build\pc\cache\sounds.vpp_pc, with an override of a bank or of its
// media (overrides\build\pc\cache\sounds.vpp_pc\ or soundboot.vpp_pc\) read instead. Its music banks (mus_*) stay on
// Wwise until step M, and its voices until step D.
//
// Which engine an event goes to (worked out once, for every loaded event, before the hooks route to our player):
//   - one that only stops, pauses, mutes, sets volumes or states (no play): both. Wwise does it to its sounds, ours to
//     its own; the game's play is Wwise's.
//   - one of our banks' that plays: ours alone. But one that also acts on what Wwise plays (a bus, a state, a switch,
//     everything playing, or another bank's sound that is Wwise's), or plays something our player can't yet
//     (synthesized sounds, music, dialogue, or something missing), is played by Wwise, its other actions by ours too.
//   - one of the other banks' that plays: Wwise; its other actions on ours too when they reach what ours plays.
//
// The game reaches Wwise only through Volition's audiolib_wwise layer. Its functions this wraps (each is cdecl):
//
//   0xB98A70  post an event (object, play)            the player's events: our player plays them, Wwise isn't asked
//   0xB98DA0  stop a play (play)                      ours: our player stops it
//   0xB98CD0  pause or resume a play (object, play, pause)   ours: our player pauses or resumes it
//   0xB990A0  a switch (object, group, value)         Wwise as before, and our player too
//   0xB98F10  a game parameter (object or 0, id, value)       the same (but game_distance: our player measures it)
//   0xB996A0  a state (group, state)                  the same
//   0xB98990  unregister a sound object (object)      the same: our player stops what plays on it
//   0xB981C0  a sound object's places (object)        the same, for the objects our player plays on
//   0xB98190  the listener (front, top, place)        the same
//   0xB8AB30, 0xB8ABA0, 0xB8ABF0  pause, resume and stop the game's sounds (effects, music)   the same, for effects
//   0xB98A20  Wwise's callback (type, info): the end of one of Armageddon's events (for the mirror, below)
//   0xB98110  unload a bank (the bank manager's record; its id at +0x40)   Wwise as before; our player stops its sounds
//
// Nothing in this build seeks (0xB84EF0, the only function that asks 0xB98C30 to, has no caller) or calls StopAll
// (0xB97430). The one source plug-in the game stops itself (0xB95F60) is Wwise's Audio Input (0xC8), which stays on
// Wwise as every synthesized sound does.
//
// The mirror ([Audio] MirrorWwise=1): Armageddon's own sounds still play on Wwise, but its voice lines and the like must
// duck ours as they duck its own (init.bnk's ducking). Its banks are loaded here for reference only, never played: an
// event of theirs posted to Wwise tells our engine which ducking buses it plays through (rfa_audio_note_bus); its end
// (Wwise's callback, or the game's stop) takes it away, and one that never says it ended is let go after its length
// and 5 s.
//
// A play (the second argument of the post): +4 the cookie the game knows it by, +8 the event's id, +0xC the playing
// id (ours are 0x80000000 and up, so they never meet Wwise's), +0x1E a byte: posted and playing. The game learns
// what became of a play from messages it posts to itself (0xB96D70: message, cookie): 5 posted or resumed, 7 paused,
// 8 ended or stopped, 9 the post failed. Our player sends them as Wwise's callback 0xB98A20 would: 8 when a play
// ends by itself; a stop the game asked for (0xB98DA0) posts its own 8, as the original does.
//
// The game object is the sound object's own address. A sound object: +0 its handle, +0x24 its first place (a place
// is 3 floats, then 3 for the way it faces at +0xC, and the next place's address at +0x1C), +0x8B how many places.
#define RFA_MOD_NAME "rfa_sound"
#include "rfa_mod.h"
#include "rfa_game.h"
#include "rfa_audio.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

// ---- The game's side ---------------------------------------------------------------------------------------------

constexpr DWORD kCallback = 0xB98A20, kUnloadBank = 0xB98110;
constexpr DWORD kPost = 0xB98A70, kStop = 0xB98DA0, kPause = 0xB98CD0, kSwitch = 0xB990A0, kParameter = 0xB98F10,
                kState = 0xB996A0, kUnregister = 0xB98990, kPlaces = 0xB981C0, kListener = 0xB98190,
                kPauseSounds = 0xB8AB30, kResumeSounds = 0xB8ABA0, kStopSounds = 0xB8ABF0;
constexpr DWORD kWwiseStarted = 0x274552A;  // a byte: audiolib_wwise_system_init has run
constexpr uint32_t kGameDistance = 0x85C3E987;  // Wwise's id of "game_distance"
constexpr uint32_t kFirstOurs = 0x80000000u;
enum Message { kPosted = 5, kPaused = 7, kEnded = 8 };

auto const PostGameMessage = reinterpret_cast<bool(__cdecl*)(int message, uint32_t cookie)>(0xB96D70);

using PostFn = int(__cdecl*)(uint8_t* object, uint8_t* play);
using StopFn = int(__cdecl*)(uint8_t* play);
using PauseFn = int(__cdecl*)(uint8_t* object, uint8_t* play, uint32_t pause);
using SwitchFn = int(__cdecl*)(uint8_t* object, uint32_t group, uint32_t value);
using ParameterFn = int(__cdecl*)(uint8_t* object, uint32_t id, float value);
using StateFn = int(__cdecl*)(uint32_t group, uint32_t state);
using ObjectFn = int(__cdecl*)(uint8_t* object);
using ListenerFn = int(__cdecl*)(const float* listener);
using SoundsFn = int(__cdecl*)(uint32_t effects, uint32_t music);
using CallbackFn = void(__cdecl*)(uint32_t type, const uint8_t* info);

PostFn g_post;
StopFn g_stop;
PauseFn g_pause;
SwitchFn g_switch;
ParameterFn g_parameter;
StateFn g_state;
ObjectFn g_unregister, g_places, g_unloadBank;
ListenerFn g_listener;
SoundsFn g_pauseSounds, g_resumeSounds, g_stopSounds;
CallbackFn g_callback;

// Each hooked function: where, how many bytes its first whole instructions take (copied out so it can still run),
// what those bytes are (checked first: a different exe isn't touched), our hook, and the original's pointer.
struct Hook {
    const char* name;
    DWORD address;
    size_t prologue;
    std::vector<uint8_t> expected;
    void* hook;
    void** original;
};

uint32_t U32(const uint8_t* p, int offset) { return *reinterpret_cast<const uint32_t*>(p + offset); }
bool WwiseStarted() { return *reinterpret_cast<const uint8_t*>(kWwiseStarted) != 0; }

// ---- Our player's side -------------------------------------------------------------------------------------------

std::atomic<bool> g_ready{false};        // the engine is open and the banks loaded: the hooks route to it

// Where each loaded event goes (see the top); one not loaded goes to Wwise. Filled before g_ready, read only after.
enum class Route : uint8_t { Wwise, Ours, Both, WwiseAndControls };
std::unordered_map<uint32_t, Route> g_routes;
std::unordered_set<uint32_t> g_ownedBanks;  // the banks Banks= names (filled before g_ready, read only after)

Route RouteFor(uint32_t eventId)
{
    auto it = g_routes.find(eventId);
    return it == g_routes.end() ? Route::Wwise : it->second;
}

std::mutex g_playsLock;                  // our plays' cookies; taken before the engine's lock, never inside it
struct OurPlay {
    uint32_t cookie;
    bool stoppedByGame;
};
std::unordered_map<uint32_t, OurPlay> g_plays;

std::mutex g_objectsLock;                // the sound objects our player has played on, and the listener's place
std::unordered_set<uintptr_t> g_objects;
float g_listenerPlace[3] = {};

bool Ready() { return g_ready.load(std::memory_order_acquire); }

// Our engine opens at once, its banks take a while (half a minute with Armageddon's): what the game sets meanwhile
// (switches, game parameters, states, the listener) is passed on from when it is open, and the global game parameters
// and states set before that (the options' volume categories at start-up, RF4_STATES) are kept and passed on when it
// opens. The game sends them only when they change, so they would be missing for good. g_globalsLock is taken before
// the engine's lock, never inside it.
std::atomic<bool> g_open{false};
std::mutex g_globalsLock;
std::unordered_map<uint32_t, float> g_earlyParameters;
std::unordered_map<uint32_t, uint32_t> g_earlyStates;

bool Open() { return g_open.load(std::memory_order_acquire); }

// Our engine is open: what was kept goes to it, and from now on everything goes straight to it.
void PassOnEarlyGlobals()
{
    std::lock_guard<std::mutex> hold(g_globalsLock);
    for (auto& [id, value] : g_earlyParameters) rfa_audio_set_parameter(0, id, value);
    for (auto& [group, state] : g_earlyStates) rfa_audio_set_state(group, state);
    if (!g_earlyParameters.empty() || !g_earlyStates.empty())
        rfa::LogDetail("Passed on %zu game parameters and %zu states the game set before our engine opened", g_earlyParameters.size(),
                       g_earlyStates.size());
    g_earlyParameters.clear();
    g_earlyStates.clear();
    g_open.store(true, std::memory_order_release);
}

// The mirror (see the top): each of Armageddon's events posted to Wwise that plays through a ducking bus, by its
// play's cookie, with those buses and when to let it go (0: when it ends; a loop).
std::atomic<bool> g_mirror{false};
std::mutex g_mirrorLock;  // taken before the engine's lock, never inside it
struct Mirrored {
    std::vector<uint32_t> buses;
    uint64_t expires;
};
std::unordered_map<uint32_t, Mirrored> g_mirrored;
std::unordered_map<uint32_t, std::vector<uint32_t>> g_eventBuses;  // worked out once an event

void EndMirror(uint32_t cookie)
{
    std::vector<uint32_t> buses;
    {
        std::lock_guard<std::mutex> hold(g_mirrorLock);
        auto it = g_mirrored.find(cookie);
        if (it == g_mirrored.end()) return;
        buses = std::move(it->second.buses);
        g_mirrored.erase(it);
    }
    for (uint32_t bus : buses) rfa_audio_note_bus(bus, -1);
}

void Mirror(uint32_t eventId, uint32_t cookie)
{
    std::vector<uint32_t> buses, expired;
    uint64_t now = GetTickCount64();
    {
        std::lock_guard<std::mutex> hold(g_mirrorLock);
        auto known = g_eventBuses.find(eventId);
        if (known == g_eventBuses.end()) {
            uint32_t found[16];
            int n = rfa_audio_event_ducking_buses(eventId, found, 16);
            known = g_eventBuses.emplace(eventId, std::vector<uint32_t>(found, found + std::clamp(n, 0, 16))).first;
        }
        buses = known->second;
        // Those that never said they ended, let go.
        for (auto it = g_mirrored.begin(); it != g_mirrored.end();) {
            if (it->second.expires && it->second.expires < now) {
                expired.insert(expired.end(), it->second.buses.begin(), it->second.buses.end());
                it = g_mirrored.erase(it);
            } else {
                ++it;
            }
        }
        if (!buses.empty()) {
            rfa_audio_event_info info{};
            rfa_audio_get_event_info(eventId, &info);
            auto& m = g_mirrored[cookie];
            expired.insert(expired.end(), m.buses.begin(), m.buses.end());  // a cookie used again
            m.buses = buses;
            m.expires = info.loops ? 0 : now + static_cast<uint64_t>((std::max(0.0f, info.longest_s) + 5.0f) * 1000.0f);
        }
    }
    for (uint32_t bus : expired) rfa_audio_note_bus(bus, -1);
    for (uint32_t bus : buses) rfa_audio_note_bus(bus, 1);
}
bool Ours(uint32_t playingId) { return playingId >= kFirstOurs; }

// The place of a sound object nearest the listener, and the way it faces there (false: it has no place).
bool NearestPlace(const uint8_t* object, float place[3], float front[3])
{
    int count = object[0x8B];
    if (count < 1) return false;
    float listener[3];
    {
        std::lock_guard<std::mutex> hold(g_objectsLock);
        memcpy(listener, g_listenerPlace, sizeof listener);
    }
    const uint8_t* node = object + 0x24;
    const float* best = nullptr;
    float bestDistance = 0;
    for (int i = 0; i < count && i < 64 && node; i++) {
        const float* p = reinterpret_cast<const float*>(node);
        float dx = p[0] - listener[0], dy = p[1] - listener[1], dz = p[2] - listener[2];
        float distance = dx * dx + dy * dy + dz * dz;
        if (!best || distance < bestDistance) {
            best = p;
            bestDistance = distance;
        }
        node = *reinterpret_cast<const uint8_t* const*>(node + 0x1C);
    }
    memcpy(place, best, 3 * sizeof(float));
    memcpy(front, best + 3, 3 * sizeof(float));
    return true;
}

void SendPlace(const uint8_t* object)
{
    float place[3], front[3];
    if (NearestPlace(object, place, front)) rfa_audio_set_position(reinterpret_cast<uintptr_t>(object), place, front);
}

// A play of ours ended by itself (from the engine's thread, outside its lock): the game hears it as from Wwise.
void OnEnd(uint32_t playingId, uint32_t eventId, uint64_t, int voices, void*)
{
    uint32_t cookie = 0;
    bool tell = false;
    {
        std::lock_guard<std::mutex> hold(g_playsLock);
        auto it = g_plays.find(playingId);
        if (it == g_plays.end()) return;
        cookie = it->second.cookie;
        tell = !it->second.stoppedByGame;
        g_plays.erase(it);
    }
    if (voices == 0 && rfa::DetailLog()) {
        // Each event once, the first 60: what made no sound at all (refused by a limit, a switch with nothing for its
        // value, nothing it can play; one too quiet is written by the engine with its level's parts).
        static std::mutex lock;
        static std::unordered_set<uint32_t> told;
        bool first;
        {
            std::lock_guard<std::mutex> hold(lock);
            first = told.size() < 60 && told.insert(eventId).second;
        }
        if (first) rfa::LogDetail("Our event %08X ended without starting a voice (a limit, or a switch with nothing for its value)", eventId);
    }
    if (tell) PostGameMessage(kEnded, cookie);
}

// The engine's own lines (with Log=1): each voice ended too quiet to hear, with its level's parts.
void EngineLine(const char* line, void*) { rfa::LogDetail("%s", line); }

// ---- The hooks ---------------------------------------------------------------------------------------------------

int __cdecl PostHook(uint8_t* object, uint8_t* play)
{
    if (!Ready() || !play || !WwiseStarted()) return g_post(object, play);
    uint32_t eventId = U32(play, 8), cookie = U32(play, 4);
    const Route route = eventId ? RouteFor(eventId) : Route::Wwise;
    if (route != Route::Ours) {
        // On Wwise (the play is its): mirrored for ducking once Wwise has taken it; its stops, pauses, mutes and states
        // done by our player too, for its own sounds, whatever Wwise made of it.
        int result = g_post(object, play);
        if (result == 0 && U32(play, 0xC) && g_mirror) Mirror(eventId, cookie);
        if (route != Route::Wwise) {
            RFA_LOG_DETAIL_FIRST(1, "the first event for both engines posted (%08X)", eventId);
            rfa_audio_post_controls(eventId, reinterpret_cast<uintptr_t>(object));
        }
        return result;
    }
    RFA_LOG_DETAIL_FIRST(1, "the first event of ours posted (on thread %lu)", GetCurrentThreadId());
    uintptr_t id = reinterpret_cast<uintptr_t>(object);
    if (object) {
        {
            std::lock_guard<std::mutex> hold(g_objectsLock);
            g_objects.insert(id);
        }
        SendPlace(object);
    }
    uint32_t playingId;
    {
        // Held until the play is known here, so an end the engine reports at once still finds its cookie.
        std::lock_guard<std::mutex> hold(g_playsLock);
        playingId = rfa_audio_post(eventId, id);
        if (playingId) g_plays[playingId] = {cookie, false};
    }
    if (!playingId) return g_post(object, play);  // not loaded after all: Wwise plays it (it has every bank the game loaded)
    *reinterpret_cast<uint32_t*>(play + 0xC) = playingId;
    play[0x1E] = 1;
    PostGameMessage(kPosted, cookie);
    return 0;
}

int __cdecl StopHook(uint8_t* play)
{
    if (Ready() && play && g_mirror && !Ours(U32(play, 0xC))) EndMirror(U32(play, 4));
    if (!Ready() || !play || !WwiseStarted() || !Ours(U32(play, 0xC))) return g_stop(play);
    uint32_t playingId = U32(play, 0xC), cookie = U32(play, 4);
    {
        std::lock_guard<std::mutex> hold(g_playsLock);
        if (auto it = g_plays.find(playingId); it != g_plays.end()) it->second.stoppedByGame = true;
    }
    rfa_audio_stop_playing(playingId, 10);  // Wwise's stop is a cut too, but without a click
    play[0x1E] = 0;
    *reinterpret_cast<uint32_t*>(play + 0xC) = 0;
    PostGameMessage(kEnded, cookie);
    return 0;
}

int __cdecl PauseHook(uint8_t* object, uint8_t* play, uint32_t pause)
{
    if (!Ready() || !play || !WwiseStarted() || !Ours(U32(play, 0xC))) return g_pause(object, play, pause);
    bool pausing = (pause & 0xFF) != 0;
    rfa_audio_pause_playing(U32(play, 0xC), pausing ? 1 : 0);
    PostGameMessage(pausing ? kPaused : kPosted, U32(play, 4));
    return 0;
}

int __cdecl SwitchHook(uint8_t* object, uint32_t group, uint32_t value)
{
    int result = g_switch(object, group, value);
    if (Open() && object) rfa_audio_set_switch(reinterpret_cast<uintptr_t>(object), group, value);
    return result;
}

int __cdecl ParameterHook(uint8_t* object, uint32_t id, float value)
{
    int result = g_parameter(object, id, value);
    if (id == kGameDistance) return result;
    if (!object) {
        // A global one: kept until our engine opens, passed on in order after.
        std::lock_guard<std::mutex> hold(g_globalsLock);
        if (Open()) rfa_audio_set_parameter(0, id, value);
        else g_earlyParameters[id] = value;
    } else if (Open()) {
        rfa_audio_set_parameter(reinterpret_cast<uintptr_t>(object), id, value);
    }
    return result;
}

int __cdecl StateHook(uint32_t group, uint32_t state)
{
    int result = g_state(group, state);
    std::lock_guard<std::mutex> hold(g_globalsLock);
    if (Open()) rfa_audio_set_state(group, state);
    else g_earlyStates[group] = state;
    return result;
}

int __cdecl UnregisterHook(uint8_t* object)
{
    int result = g_unregister(object);
    if (Open() && object) {
        {
            std::lock_guard<std::mutex> hold(g_objectsLock);
            g_objects.erase(reinterpret_cast<uintptr_t>(object));
        }
        rfa_audio_remove_object(reinterpret_cast<uintptr_t>(object));
    }
    return result;
}

int __cdecl PlacesHook(uint8_t* object)
{
    int result = g_places(object);
    if (Ready() && object) {
        bool ours;
        {
            std::lock_guard<std::mutex> hold(g_objectsLock);
            ours = g_objects.count(reinterpret_cast<uintptr_t>(object)) != 0;
        }
        if (ours) SendPlace(object);
    }
    return result;
}

// The listener: Wwise's AkListenerPosition, the way it faces (+0), its top (+0xC) and its place (+0x18).
int __cdecl ListenerHook(const float* listener)
{
    int result = g_listener(listener);
    if (Open() && listener) {
        {
            std::lock_guard<std::mutex> hold(g_objectsLock);
            memcpy(g_listenerPlace, listener + 6, sizeof g_listenerPlace);
        }
        rfa_audio_set_listener(listener + 6, listener, listener + 3);
    }
    return result;
}

// The game unloads a bank (its record: the bank's id at +0x40): Wwise stops what plays from it, and so does our player
// (which keeps the bank loaded: a bank the game loads again is ready at once).
int __cdecl UnloadBankHook(uint8_t* record)
{
    int result = g_unloadBank(record);
    if (Ready() && record) rfa_audio_stop_bank(U32(record, 0x40), 10);
    return result;
}

// Wwise's callback: the end of one of Armageddon's events (type 1, its cookie first in info) ends its mirror.
void __cdecl CallbackHook(uint32_t type, const uint8_t* info)
{
    if (type == 1 && info && g_mirror) EndMirror(U32(info, 0));
    g_callback(type, info);
}

int __cdecl PauseSoundsHook(uint32_t effects, uint32_t music)
{
    int result = g_pauseSounds(effects, music);
    if (Ready() && (effects & 0xFF)) rfa_audio_pause_all(1);
    return result;
}

int __cdecl ResumeSoundsHook(uint32_t effects, uint32_t music)
{
    int result = g_resumeSounds(effects, music);
    if (Ready() && (effects & 0xFF)) rfa_audio_pause_all(0);
    return result;
}

int __cdecl StopSoundsHook(uint32_t effects, uint32_t music)
{
    int result = g_stopSounds(effects, music);
    if (Ready() && (effects & 0xFF)) rfa_audio_stop_object(0, 0);
    return result;
}

// ---- Starting ----------------------------------------------------------------------------------------------------

struct Settings {
    std::vector<std::wstring> banks;  // name patterns (* for any letters), lower-case
    int voices = 128, cacheMb = 32;
    bool hrtf = false, mirror = true;
};

std::wstring Lower(std::wstring s)
{
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

// A name against a pattern with * standing for any letters (none included).
bool Matches(const wchar_t* name, const wchar_t* pattern)
{
    if (!*pattern) return !*name;
    if (*pattern == L'*') return Matches(name, pattern + 1) || (*name && Matches(name + 1, pattern));
    return *name == *pattern && Matches(name + 1, pattern + 1);
}

Settings ReadSettings()
{
    Settings s;
    std::wstring list = Lower(rfa::IniString(L"Audio", L"Banks", L""));
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(L',', start);
        std::wstring part = list.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
        while (!part.empty() && iswspace(part.front())) part.erase(part.begin());
        while (!part.empty() && iswspace(part.back())) part.pop_back();
        if (!part.empty()) s.banks.push_back(part);
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    if (s.banks.empty()) s.banks.push_back(L"rfg*");
    s.voices = std::clamp(rfa::IniInt(L"Audio", L"Voices", 128), 16, 1024);
    s.cacheMb = std::clamp(rfa::IniInt(L"Audio", L"CacheMB", 32), 4, 512);
    s.hrtf = rfa::IniInt(L"Audio", L"HRTF", 0) != 0;
    s.mirror = rfa::IniInt(L"Audio", L"MirrorWwise", 1) != 0;
    return s;
}

std::string Narrow(const std::wstring& w)
{
    std::string s;
    for (wchar_t c : w) s += c < 128 ? static_cast<char>(c) : '?';
    return s;
}

bool Wanted(const std::wstring& name, const Settings& settings)
{
    for (const auto& pattern : settings.banks)
        if (Matches(name.c_str(), pattern.c_str())) return true;
    return false;
}

// The game's own build\pc\cache\ (beside its installed exe), where its archives are. The game may run from the launcher's
// large-address-aware copy of its exe (in the mod folder's large_address\): the installed exe is then the one the
// launcher names (rfa::game::InstalledExePath).
std::wstring GameCache()
{
    std::wstring folder = rfa::game::InstalledExePath();
    return folder.substr(0, folder.find_last_of(L"\\/") + 1) + L"build\\pc\\cache\\";
}

void LogProblems(uint32_t bank, const char* name)
{
    if (!rfa::DetailLog()) return;
    char problems[1024] = {};
    if (rfa_audio_bank_problems(bank, problems, sizeof problems) > 0) rfa::LogDetail("%s: %s", name, problems);
}

// Armageddon's banks that Banks= names, from the game's sounds.vpp_pc as the game reads them: an override of a bank or
// of its media (overrides\build\pc\cache\sounds.vpp_pc\, or soundboot.vpp_pc\, which holds copies of some) first. Its
// music banks stay on Wwise (step M); init.bnk_pc is loaded apart, for the buses.
int LoadArmageddonBanks(const Settings& settings, const std::wstring& overrides, int& failed)
{
    const std::wstring archive = GameCache() + L"sounds.vpp_pc";
    const int size = rfa_audio_archive_bank_names(archive.c_str(), nullptr, 0);
    if (size < 0) {
        rfa::Log("The game's build\\pc\\cache\\sounds.vpp_pc can't be read: Armageddon's sounds stay on Wwise");
        return 0;
    }
    std::string list(static_cast<size_t>(size) + 1, '\0');
    rfa_audio_archive_bank_names(archive.c_str(), list.data(), size + 1);
    list.resize(static_cast<size_t>(size));
    const std::wstring sounds = overrides + L"sounds.vpp_pc\\", boot = overrides + L"soundboot.vpp_pc\\";
    const wchar_t* folders[] = {sounds.c_str(), boot.c_str()};
    int loaded = 0;
    std::vector<std::string> music;
    for (size_t start = 0; start < list.size();) {
        size_t end = list.find('\n', start);
        if (end == std::string::npos) end = list.size();
        const std::string name = list.substr(start, end - start);
        start = end + 1;
        if (name.empty() || name == "init" || !Wanted(std::wstring(name.begin(), name.end()), settings)) continue;
        if (name.rfind("mus_", 0) == 0) {
            music.push_back(name);
            continue;
        }
        char error[512] = {};
        const uint32_t bank = rfa_audio_load_game_bank(archive.c_str(), folders, 2, name.c_str(), error, sizeof error);
        if (!bank) {
            rfa::Log("Armageddon's %s.bnk_pc didn't load (%s): its sounds stay on Wwise", name.c_str(), error);
            failed++;
            continue;
        }
        g_ownedBanks.insert(bank);
        loaded++;
        LogProblems(bank, (name + ".bnk_pc").c_str());
    }
    if (!music.empty())
        rfa::LogDetail("[Audio] Banks= names %zu of Armageddon's music banks (%s first): music stays on Wwise until step M",
                       music.size(), music.front().c_str());
    return loaded;
}

// Every loaded event's route (see the top; the engine works it out from what each event's actions reach).
void WorkOutRoutes(int& ours, int& both, int& wwiseAndControls)
{
    const std::vector<uint32_t> here(g_ownedBanks.begin(), g_ownedBanks.end());
    const int count = std::max(0, rfa_audio_event_routes(here.data(), static_cast<int>(here.size()), nullptr, nullptr, 0));
    std::vector<uint32_t> events(static_cast<size_t>(count));
    std::vector<int> routes(static_cast<size_t>(count));
    const int filled = std::min(count, rfa_audio_event_routes(here.data(), static_cast<int>(here.size()), events.data(), routes.data(), count));
    for (int i = 0; i < filled; i++) {
        switch (routes[i]) {
        case RFA_AUDIO_ROUTE_HERE: g_routes[events[i]] = Route::Ours; ours++; break;
        case RFA_AUDIO_ROUTE_BOTH: g_routes[events[i]] = Route::Both; both++; break;
        case RFA_AUDIO_ROUTE_ELSEWHERE_AND_CONTROLS: g_routes[events[i]] = Route::WwiseAndControls; wwiseAndControls++; break;
        default: break;  // Wwise alone, as for an event not loaded
        }
    }
}

// Opens the engine and loads the banks, on a thread of its own (the game starts meanwhile, on Wwise alone). The
// hooks route to our player once this is done; if anything fails, every sound stays on Wwise.
void Start(Settings settings)
{
    const std::wstring root = rfa::RootDir();
    const std::wstring cache = root + L"\\overrides\\build\\pc\\cache\\";
    rfa_audio_set_library_folder((root + L"\\mods\\openal\\").c_str());
    rfa_audio_settings open{};
    open.max_sources = settings.voices;
    open.cache_mb = settings.cacheMb;
    open.own_thread = 1;  // its own update every 10 ms, whatever the game's frame does
    open.hrtf = settings.hrtf ? 1 : 0;
    char error[512] = {};
    if (!rfa_audio_open(&open, error, sizeof error)) {
        rfa::Log("Our sound engine didn't start (%s): every sound stays on Wwise", error);
        return;
    }
    PassOnEarlyGlobals();
    // Armageddon's init.bnk_pc, as the game reads it (our override of it), for the volumes of the buses our sounds go to.
    std::wstring init = cache + L"soundboot.vpp_pc\\init.bnk_pc";
    if (GetFileAttributesW(init.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (!rfa_audio_load_bank(init.c_str(), nullptr, error, sizeof error))
            rfa::Log("init.bnk_pc didn't load (%s): the buses' volumes count as 0 dB", error);
    } else {
        rfa::LogDetail("No init.bnk_pc override in overrides\\build\\pc\\cache\\soundboot.vpp_pc\\: the buses' volumes count as 0 dB");
    }
    // Ours: the banks Banks= names among those loose in overrides\build\pc\cache\ (the Guerrilla world's, rfg*).
    int banks = 0, failed = 0;
    WIN32_FIND_DATAW found;
    HANDLE find = FindFirstFileW((cache + L"*.bnk_pc").c_str(), &found);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring file = found.cFileName, name = Lower(file.substr(0, file.size() - 7));
            if (name.size() > 6 && name.compare(name.size() - 6, 6, L"_media") == 0) continue;
            if (!Wanted(name, settings)) continue;
            std::wstring media = cache + file.substr(0, file.size() - 7) + L"_media.bnk_pc";
            uint32_t bank = rfa_audio_load_bank((cache + file).c_str(), media.c_str(), error, sizeof error);
            if (!bank) {
                rfa::Log("%ls didn't load (%s): its sounds stay on Wwise", file.c_str(), error);
                failed++;
                continue;
            }
            banks++;
            g_ownedBanks.insert(bank);
            LogProblems(bank, Narrow(file).c_str());
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    // Armageddon's own that Banks= names (B1).
    const int armageddons = LoadArmageddonBanks(settings, cache, failed);
    banks += armageddons;
    rfa_audio_set_end_callback(OnEnd, nullptr);
    if (rfa::DetailLog()) rfa_audio_set_log_callback(EngineLine, nullptr, 60);
    char device[256] = {};
    rfa_audio_device_info(device, sizeof device);
    if (!banks) {
        rfa::Log("No sound bank matched [Audio] Banks= (in overrides\\build\\pc\\cache\\ or the game's sounds.vpp_pc): every sound "
                 "stays on Wwise");
        return;
    }
    // The rest of Armageddon's banks, from the game's archives beside its exe, for reference only: never played, but its
    // events on Wwise duck ours as its own (the mirror), and their stops, pauses, mutes and states reach ours. A bank
    // loaded above is kept as it is.
    if (settings.mirror) {
        int reference = 0;
        for (const wchar_t* archive : {L"sounds.vpp_pc", L"voices.vpp_pc"}) {
            int n = rfa_audio_load_archive_banks((GameCache() + archive).c_str(), error, sizeof error);
            if (n < 0) rfa::Log("%ls can't be read (%s): Armageddon's sounds don't duck ours", archive, error);
            else reference += n;
        }
        if (reference > 0) {
            g_mirror = true;
            rfa::LogDetail("Read %d of Armageddon's banks for reference: its sounds on Wwise duck ours as its own", reference);
        }
    }
    int ours = 0, both = 0, wwiseAndControls = 0;
    WorkOutRoutes(ours, both, wwiseAndControls);
    g_ready.store(true, std::memory_order_release);
    rfa::LogInfo("Our sound engine plays %d banks (%d of them Armageddon's) on %s: %d events on ours alone, %d on both engines "
                 "(stops, pauses, mutes, states), %d played by Wwise with their other actions on ours too%s",
                 banks, armageddons, device, ours, both, wwiseAndControls, failed ? "; some banks didn't load (above)" : "");
}

// With Log=1: what our player is doing, every 10 s.
void StatusLoop()
{
    for (;;) {
        Sleep(10000);
        if (!Ready()) continue;
        rfa_audio_status s{};
        rfa_audio_get_status(&s);
        size_t plays;
        {
            std::lock_guard<std::mutex> hold(g_playsLock);
            plays = g_plays.size();
        }
        rfa::LogDetail("Playing %d (%zu the game knows), voices %d (%d virtual, %d streamed), sources %d of %d, cache %.1f MB; "
                       "so far %lld refused by their limits, %lld ended too quiet, %lld gave up their source, %lld found none free, "
                       "%lld stream underruns",
                       s.playing, plays, s.voices, s.virtual_voices, s.streams, s.sources_used, s.sources, s.cache_bytes / 1048576.0,
                       static_cast<long long>(s.voices_refused), static_cast<long long>(s.voices_ended_quiet),
                       static_cast<long long>(s.voices_stolen), static_cast<long long>(s.voices_no_source), static_cast<long long>(s.underruns));
    }
}

bool InstallHooks()
{
    const std::vector<uint8_t> shortStart = {0x55, 0x8B, 0xEC, 0x51, 0x0F, 0xB6, 0x05, 0x2A, 0x55, 0x74, 0x02};
    Hook hooks[] = {
        {"post", kPost, 5, {0x55, 0x8B, 0xEC, 0x6A, 0xFF}, reinterpret_cast<void*>(PostHook), reinterpret_cast<void**>(&g_post)},
        {"stop", kStop, 11, shortStart, reinterpret_cast<void*>(StopHook), reinterpret_cast<void**>(&g_stop)},
        {"pause", kPause, 11, shortStart, reinterpret_cast<void*>(PauseHook), reinterpret_cast<void**>(&g_pause)},
        {"switch", kSwitch, 11, shortStart, reinterpret_cast<void*>(SwitchHook), reinterpret_cast<void**>(&g_switch)},
        {"game parameter", kParameter, 11, shortStart, reinterpret_cast<void*>(ParameterHook), reinterpret_cast<void**>(&g_parameter)},
        {"state", kState, 11, shortStart, reinterpret_cast<void*>(StateHook), reinterpret_cast<void**>(&g_state)},
        {"unregister", kUnregister, 11, shortStart, reinterpret_cast<void*>(UnregisterHook), reinterpret_cast<void**>(&g_unregister)},
        {"places", kPlaces, 5, {0x55, 0x8B, 0xEC, 0x6A, 0xFF}, reinterpret_cast<void*>(PlacesHook), reinterpret_cast<void**>(&g_places)},
        {"listener", kListener, 11, shortStart, reinterpret_cast<void*>(ListenerHook), reinterpret_cast<void**>(&g_listener)},
        {"pause sounds", kPauseSounds, 11, {0x55, 0x8B, 0xEC, 0x51, 0x0F, 0xB6, 0x05, 0x26, 0x55, 0x74, 0x02},
         reinterpret_cast<void*>(PauseSoundsHook), reinterpret_cast<void**>(&g_pauseSounds)},
        {"resume sounds", kResumeSounds, 11, {0x55, 0x8B, 0xEC, 0x51, 0x0F, 0xB6, 0x05, 0x26, 0x55, 0x74, 0x02},
         reinterpret_cast<void*>(ResumeSoundsHook), reinterpret_cast<void**>(&g_resumeSounds)},
        {"stop sounds", kStopSounds, 10, {0x55, 0x8B, 0xEC, 0x0F, 0xB6, 0x05, 0x26, 0x55, 0x74, 0x02},
         reinterpret_cast<void*>(StopSoundsHook), reinterpret_cast<void**>(&g_stopSounds)},
        {"Wwise's callback", kCallback, 6, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08}, reinterpret_cast<void*>(CallbackHook),
         reinterpret_cast<void**>(&g_callback)},
        {"unload a bank", kUnloadBank, 11, {0x55, 0x8B, 0xEC, 0x51, 0x0F, 0xB6, 0x05, 0x29, 0x55, 0x74, 0x02},
         reinterpret_cast<void*>(UnloadBankHook), reinterpret_cast<void**>(&g_unloadBank)},
    };
    // Every one checked before any is changed: all or none.
    for (const Hook& h : hooks)
        if (memcmp(reinterpret_cast<const void*>(h.address), h.expected.data(), h.expected.size()) != 0) {
            rfa::Log("The game's %s function (0x%lX) isn't as expected: nothing hooked, every sound stays on Wwise", h.name,
                     static_cast<unsigned long>(h.address));
            return false;
        }
    for (const Hook& h : hooks) {
        void* original = rfa::HookFunction(h.address, h.prologue, h.hook);
        if (!original) {
            rfa::Log("Hooking the game's %s function (0x%lX) failed", h.name, static_cast<unsigned long>(h.address));
            return false;  // those hooked so far only pass on to Wwise while g_ready is false
        }
        *h.original = original;
    }
    return true;
}

}  // namespace

extern "C" __declspec(dllexport) void __cdecl RFAMod_Init()
{
    rfa::ReadLogSwitch(L"Audio");
    if (!rfa::IniInt(L"Audio", L"OwnPlayer", 0)) {
        rfa::LogDetail("[Audio] OwnPlayer=0: every sound plays on Wwise, as in the game");
        return;
    }
    if (!rfa::game::IsSupportedBuild()) {
        rfa::Log("Not the game build RFA-Mod targets: every sound stays on Wwise");
        return;
    }
    if (!InstallHooks()) return;
    std::thread(Start, ReadSettings()).detach();
    if (rfa::DetailLog()) std::thread(StatusLoop).detach();
}
