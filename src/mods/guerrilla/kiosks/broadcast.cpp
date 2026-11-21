// guerrilla_kiosks: the EDF's news broadcast, Guerrilla's kiosk_do_frame (RM 0xB3ABF0) and the functions it calls.
//
// One broadcast at a time, at one kiosk (or bar radio):
// - Waiting (kiosk_maybe_activate, RM 0xB3AB10): at first 15-30 s (Kiosk_inactive_delay, random up to twice it).
//   Nothing starts during a mission; at alert yellow or above the wait starts again (15 s). When it runs out, the
//   first kiosk of the list within 50 m of the player (on x and z) that isn't destroyed
//   (kiosk_find_potential_kiosk_index, RM 0xB2E440) starts one; with none, the wait again.
// - Starting (kiosk_set_active(true), RM 0xB3A990): the session's line count 0 and its cut-off 3-5 (random),
//   every line's "played this session" cleared, the broadcast's picture (Active_Screen) on every kiosk; the
//   intro (Kiosk_Music_Stinger) and the loop (Kiosk_Music_Loop, 2,360 ms later) at the kiosk; the first line
//   1,500 + 2,360 ms later.
// - Each line (kiosk_find_available_line, RM 0xB34A90): of the available lines not played this session, those
//   played the fewest times; one of them at random. Played at the kiosk; the next one its length + 1,500 ms
//   later; its play count and the session's up; its picture (Screen_Effect) on every kiosk once its delay has
//   passed (until then the broadcast's own). A line played its Min_replay_count times leaves the list while more
//   than 5 are left. With no line to play, or one that won't play, the broadcast ends.
// - Ending (kiosk_start_deactivate, RM 0xB2E6E0): at the session's cut-off or with no kiosk within 50 m: the loop
//   stopped, the station ID line and the outro (Kiosk_Music_Out) together; when both have played, everything
//   stopped, the screens back to idle (or alert), and the next broadcast no sooner than 120 s on
//   (Kiosk_inactive_delay_long; 15 s when it ended otherwise).
// - The sounds follow the kiosk (kiosk_update_current_sound_pos, RM 0xB3AB70); the kiosk gone, it ends. The alert
//   rising from green ends it too (Guerrilla's public_address_alert_level_callback, RM 0xB3F200).
// - Lines are unlocked (kiosk_unlock_from_*): those that start unlocked, and a mission's or an activity's when it
//   is won, and the script action kiosk_unlock_line (GuerrillaKiosks_UnlockLine, kiosks_api.h, for guerrilla_campaign). A line is added only
//   while its play count is under its Min_replay_count; past 5 lines, the one played most over its count leaves.
// - The lines' play counts and which are available are kept (Guerrilla's public_address_kiosk_save) in
//   mods\guerrilla_kiosks\kiosk_save.ini until guerrilla_save keeps them with each save.
//
// The sounds are the builder's (mods\guerrilla_kiosks\kiosk_sounds.txt: Wwise events made of Guerrilla's cues and
// its KIOSK persona's lines, with their lengths, which Guerrilla takes from its audiolib_play_get_play_time). They
// play on a sound object of the mod's own, moved to the kiosk each frame (0xB83000 makes one at a place, as the game
// does for its own lone sounds; 0xB83C40 moves one; 0xB846E0 plays an event on it, with a delay, and gives a
// handle; 0xB85110 stops a sound by its handle, playing the loop's stop event where it has one).
#define RFA_MOD_NAME "guerrilla_kiosks"
#include "rfa_mod.h"
#include "campaign/campaign_api.h"
#include "kiosks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <unordered_map>

namespace kiosks::broadcast {
namespace {

constexpr size_t kIsKilledSlot = 0x10 / 4;  // is_killed, as Guerrilla's
using IsKilledFn = bool(__thiscall*)(BYTE* object);
using MakeSoundObjectFn = DWORD(__cdecl*)(const float* place, int zero);
auto const MakeSoundObject = reinterpret_cast<MakeSoundObjectFn>(0xB83000);
using MoveSoundObjectFn = void(__cdecl*)(DWORD soundObject, const float* place, const float* turn);
auto const MoveSoundObject = reinterpret_cast<MoveSoundObjectFn>(0xB83C40);
using EndSoundObjectFn = void(__cdecl*)(DWORD soundObject);
auto const EndSoundObject = reinterpret_cast<EndSoundObjectFn>(0xB83200);
using PlaySoundEventFn = int(__cdecl*)(DWORD soundObject, DWORD eventId, DWORD* handle, int unused, int delayMs);
auto const PlaySoundEvent = reinterpret_cast<PlaySoundEventFn>(0xB846E0);
using StopSoundFn = int(__cdecl*)(DWORD* handle);
auto const StopSound = reinterpret_cast<StopSoundFn>(0xB85110);
using SoundGameObjectFn = DWORD(__cdecl*)(DWORD handle);
auto const SoundGameObject = reinterpret_cast<SoundGameObjectFn>(0xB84D20);  // 0 once the sound has ended
// For the check of the broadcast's 3D sound (see CheckSound).
using SoundObjectFromHandleFn = const BYTE*(__cdecl*)(DWORD soundObject);
auto const SoundObjectFromHandle = reinterpret_cast<SoundObjectFromHandleFn>(0xB812A0);  // takes audiolib's own lock
const float* const g_cameraPosition = reinterpret_cast<const float*>(0x11B9C6C);
const float* const g_listenerPosition = reinterpret_cast<const float*>(0x2745548);  // audiolib's, from the camera, given to Wwise
const float* const g_listenerFront = reinterpret_cast<const float*>(0x2745554);     // its front and top, given with it
const float* const g_listenerTop = reinterpret_cast<const float*>(0x2745560);

// Wwise's id of a name: FNV-1 (32 bits) of its lower-case letters.
DWORD WwiseNameId(const std::string& name) {
    DWORD h = 2166136261u;
    for (char c : name) h = (h * 16777619u) ^ static_cast<BYTE>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return h;
}

struct Sound {
    std::string event, stopEvent;
    int ms = 0;
};
Sound g_intro, g_loop, g_outro;
std::unordered_map<std::string, Sound> g_lineSounds;  // by the voice line's name (lower case)
bool g_soundsRead = false;

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return text;
}

void ReadSounds() {
    FILE* file = nullptr;
    if (_wfopen_s(&file, (ModFolder() + L"\\kiosk_sounds.txt").c_str(), L"r") != 0 || !file) return;
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char kind[16], a[160], b[160], c[160];
        int ms = 0;
        if (sscanf_s(line, "music %15s %159s %159s %d", kind, static_cast<unsigned>(sizeof(kind)), a, static_cast<unsigned>(sizeof(a)), b,
                     static_cast<unsigned>(sizeof(b)), &ms) == 4) {
            Sound s{a, strcmp(b, "-") == 0 ? std::string() : b, ms};
            if (strcmp(kind, "intro") == 0) g_intro = s;
            else if (strcmp(kind, "loop") == 0) g_loop = s;
            else if (strcmp(kind, "outro") == 0) g_outro = s;
        } else if (sscanf_s(line, "line %159s %159s %159s %d", a, static_cast<unsigned>(sizeof(a)), b, static_cast<unsigned>(sizeof(b)), c,
                            static_cast<unsigned>(sizeof(c)), &ms) == 4) {
            g_lineSounds[Lower(a)] = Sound{b, c, ms};
        }
    }
    fclose(file);
    g_soundsRead = true;
}

// A timer on the mod's own clock of play (Guerrilla's timestamp: invalid, or due at a time).
double g_clockMs = 0.0;
struct Timer {
    double due = -1.0;
    bool Valid() const { return due >= 0.0; }
    bool Elapsed() const { return Valid() && g_clockMs >= due; }
    void Set(int ms) { due = g_clockMs + (ms < 0 ? 0 : ms); }
    void SetRandom(int least, int most) { Set(least + (most > least ? rand() % (most - least + 1) : 0)); }
    void Invalidate() { due = -1.0; }
};

// The lines' state (Guerrilla's kiosk_line +0x1C play count, +0x20 played this session; Kiosk_available_lines).
struct LineState {
    int plays = 0;
    bool playedThisSession = false;
};
std::vector<LineState> g_lineStates;
std::vector<int> g_available;  // indices into tables::Lines()

bool g_active = false, g_shouldDeactivate = false;
// The line playing now, for the check two seconds into it (see Update).
std::string g_voiceLine;
double g_voiceStartedMs = 0.0;
int g_voiceMs = 0;
bool g_voiceChecked = true;
std::string g_endingWhy;  // why the ending started (kiosk_start_deactivate), said again when it is over
Timer g_inactiveTimer, g_nextLineTimer, g_screenTimer;
int g_sessionLines = 0, g_sessionCutoff = 0, g_kiosk = -1, g_nextScreen = -1, g_lastAlert = 0;
DWORD g_soundObject = 0, g_voice = 0, g_introPlaying = 0, g_loopPlaying = 0, g_outroPlaying = 0;
bool g_inited = false;

const std::wstring SavePath() { return ModFolder() + L"\\kiosk_save.ini"; }

void Save() {
    FILE* file = nullptr;
    if (_wfopen_s(&file, SavePath().c_str(), L"w") != 0 || !file) return;
    fputs("; guerrilla_kiosks: Guerrilla's kiosk lines, how often each has played and whether it is available (Guerrilla's "
          "public_address_kiosk_save). Delete this file to start them afresh.\n", file);
    const auto& lines = tables::Lines();
    for (size_t i = 0; i < lines.size(); ++i) {
        const bool available = std::find(g_available.begin(), g_available.end(), static_cast<int>(i)) != g_available.end();
        if (g_lineStates[i].plays > 0 || available) fprintf(file, "line %s %d %d\n", lines[i].name.c_str(), g_lineStates[i].plays, available ? 1 : 0);
    }
    fclose(file);
}

// Guerrilla's kiosk_maybe_remove_available_line (RM 0xB2E670): past Kiosk_min_available_lines, the line played the
// most times over its Min_replay_count (at least its count) leaves the list.
void MaybeRemoveAvailableLine() {
    if (static_cast<int>(g_available.size()) <= tables::Settings().minAvailableLines) return;
    const auto& lines = tables::Lines();
    int best = -1, worst = -1;
    for (size_t k = 0; k < g_available.size(); ++k) {
        const int i = g_available[k];
        const int over = g_lineStates[i].plays - lines[i].minReplays;
        if (over > best) {
            best = over;
            worst = static_cast<int>(k);
        }
    }
    if (worst >= 0) g_available.erase(g_available.begin() + worst);
}

// Guerrilla's kiosk_unlock_from_index (RM 0xB349C0): the line made available while it has played fewer times than
// its Min_replay_count.
bool Unlock(int i) {
    if (i < 0 || i >= static_cast<int>(tables::Lines().size())) return false;
    if (g_lineStates[i].plays >= tables::Lines()[i].minReplays) return false;
    if (std::find(g_available.begin(), g_available.end(), i) == g_available.end()) g_available.push_back(i);
    MaybeRemoveAvailableLine();
    return true;
}

void Load() {
    const auto& lines = tables::Lines();
    g_lineStates.assign(lines.size(), LineState{});
    g_available.clear();
    FILE* file = nullptr;
    bool saved = false;
    if (_wfopen_s(&file, SavePath().c_str(), L"r") == 0 && file) {
        char text[256];
        while (fgets(text, sizeof(text), file)) {
            char name[160];
            int plays = 0, available = 0;
            if (sscanf_s(text, "line %159s %d %d", name, static_cast<unsigned>(sizeof(name)), &plays, &available) != 3) continue;
            saved = true;
            for (size_t i = 0; i < lines.size(); ++i)
                if (_stricmp(lines[i].name.c_str(), name) == 0) {
                    g_lineStates[i].plays = plays;
                    if (available) g_available.push_back(static_cast<int>(i));
                }
        }
        fclose(file);
    }
    // Guerrilla's kiosk_init_available_lines (RM 0xB34A20): the lines that start unlocked; with a save, its list.
    if (!saved)
        for (size_t i = 0; i < lines.size(); ++i)
            if (lines[i].startsUnlocked) g_available.push_back(static_cast<int>(i));
}

BYTE* KioskObject(int index) {
    const auto& all = tables::Kiosks();
    return index >= 0 && index < static_cast<int>(all.size()) ? ObjectFromHandle(all[index].handle) : nullptr;
}

bool IsKilled(BYTE* object) {
    const auto isKilled = reinterpret_cast<IsKilledFn>((*reinterpret_cast<void* const* const*>(object))[kIsKilledSlot]);
    return isKilled(object);
}

const float* PlaceOf(const BYTE* object) { return reinterpret_cast<const float*>(object + 4); }

// Guerrilla's kiosk_find_potential_kiosk_index: the first kiosk of the list within 50 m of the player (x and z) that
// isn't destroyed, or -1.
int PotentialKiosk(float* howFar = nullptr) {
    BYTE* player = Player();
    if (!player) return -1;
    const float* p = PlaceOf(player);
    const float most = tables::Settings().maxDistance;
    const auto& all = tables::Kiosks();
    for (size_t i = 0; i < all.size(); ++i) {
        BYTE* kiosk = ObjectFromHandle(all[i].handle);
        if (!kiosk || IsKilled(kiosk)) continue;
        const float* k = PlaceOf(kiosk);
        const float dx = p[0] - k[0], dz = p[2] - k[2];
        if (dx * dx + dz * dz < most * most) {
            if (howFar) *howFar = std::sqrt(dx * dx + dz * dz);
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Guerrilla's kiosk_find_available_line: of the available lines not played this session (and with a sound
// built), those played the fewest times; one of them at random.
int FindAvailableLine() {
    const auto& lines = tables::Lines();
    std::vector<int> fewest;
    for (int i : g_available) {
        if (g_lineStates[i].playedThisSession || !g_lineSounds.count(Lower(lines[i].name))) continue;
        if (fewest.empty() || g_lineStates[i].plays < g_lineStates[fewest[0]].plays) fewest.assign(1, i);
        else if (g_lineStates[i].plays == g_lineStates[fewest[0]].plays) fewest.push_back(i);
    }
    return fewest.empty() ? -1 : fewest[rand() % fewest.size()];
}

DWORD Play(const std::string& event, int delayMs = 0) {
    if (event.empty() || !g_soundObject) return 0;
    DWORD handle = 0;
    PlaySoundEvent(g_soundObject, WwiseNameId(event), &handle, 0, delayMs);
    return handle;
}

void Stop(DWORD& handle) {
    if (handle && SoundGameObject(handle) != 0) StopSound(&handle);
    handle = 0;
}

float Distance(const float* a, const float* b) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// What Guerrilla's logarithmic rolloff (audiolib_3d_system_init, RM 0x485A10) gives the newsreader at a distance, in
// dB: full volume to 5 m, silent at 70 m (DIST_VOC_KIOSK, the distances the builder gives the kiosk's sounds).
float GuerrillaKioskDb(float distance) {
    static const float gains[] = {1.0f, 0.699f, 0.523f, 0.398f, 0.301f, 0.222f, 0.155f, 0.097f, 0.046f, 0.02f, 0.0f};
    const float t = (distance - 5.0f) / 65.0f;
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return -96.3f;
    const int i = static_cast<int>(t * 10.0f);
    const float gain = gains[i] + (gains[i + 1] - gains[i]) * (t * 10.0f - i);
    return gain > 0.0f ? (std::max)(-96.3f, 20.0f * std::log10(gain)) : -96.3f;
}

// What Wwise itself holds of the broadcast's distance (read only). Every sound of ours fades on the game parameter
// game_distance, which the game sets on each sound object (guerrilla_world: "Sound distances"). Wwise keeps it in its
// RTPC manager (the pointer at 0x34FB8E0): game parameters in 193 lists by id from +4, each {id, next, its global value
// +8, the game objects' own values +0x0C..+0x10 (8 bytes each: the registered object, the value), the subscriptions
// +0x18..+0x1C}. A subscription is {the node, the property +4, ..., the game object it is for +0x0C, its kind +0x10,
// its curves +0x14..+0x18 (0x14 bytes each, the game parameter at +4)}. A node's id is at +0x0C, its play count (a
// 16-bit count) at +0x18: Wwise passes a game object's new value to a node's kind 2 subscription only while that count
// isn't 0, and never to kind 1 (0xE91A80, from SetRTPCInternal 0xE92730). The game objects are in Wwise's registry
// (the pointer at 0x34FB8D0): 193 lists by id from +0x28, each {next, the id, the registered object}.
// The sound engine's own thread may be changing all this while it is read, so a read that faults is said, not fatal.
constexpr DWORD kWwiseRtpcManager = 0x34FB8E0, kWwiseRegistry = 0x34FB8D0, kGameDistance = 0x85C3E987;
constexpr unsigned kWwiseLists = 193, kListedPlaying = 6;

struct WwiseView {
    bool parameterFound, registered, hasValue;
    float value, global;
    unsigned objectsWithValue, subscriptions, nodeSubscriptions, playing, listed;
    DWORD playingIds[kListedPlaying];
    int lineKind;  // the playing line's node's subscription kind, -1 without one
    unsigned lineCurves, linePlays;
};

bool ReadWwiseView(DWORD gameObject, DWORD lineNode, WwiseView* view) {
    __try {
        const BYTE* manager = *reinterpret_cast<const BYTE* const*>(kWwiseRtpcManager);
        const BYTE* registry = *reinterpret_cast<const BYTE* const*>(kWwiseRegistry);
        if (!manager || !registry) return false;
        const BYTE* registered = nullptr;
        for (const BYTE* item = *reinterpret_cast<const BYTE* const*>(registry + 0x28 + (gameObject % kWwiseLists) * 4); item;
             item = *reinterpret_cast<const BYTE* const*>(item))
            if (*reinterpret_cast<const DWORD*>(item + 4) == gameObject) {
                registered = *reinterpret_cast<const BYTE* const*>(item + 8);
                break;
            }
        view->registered = registered != nullptr;
        const BYTE* parameter = *reinterpret_cast<const BYTE* const*>(manager + 4 + (kGameDistance % kWwiseLists) * 4);
        while (parameter && *reinterpret_cast<const DWORD*>(parameter) != kGameDistance) parameter = *reinterpret_cast<const BYTE* const*>(parameter + 4);
        view->parameterFound = parameter != nullptr;
        if (!parameter) return true;
        view->global = *reinterpret_cast<const float*>(parameter + 8);
        const BYTE* values = *reinterpret_cast<const BYTE* const*>(parameter + 0x0C);
        const BYTE* valuesEnd = *reinterpret_cast<const BYTE* const*>(parameter + 0x10);
        view->objectsWithValue = values ? static_cast<unsigned>((valuesEnd - values) / 8) : 0;
        for (unsigned i = 0; registered && i < view->objectsWithValue; ++i)
            if (*reinterpret_cast<const BYTE* const*>(values + i * 8) == registered) {
                view->hasValue = true;
                view->value = *reinterpret_cast<const float*>(values + i * 8 + 4);
                break;
            }
        const BYTE* const* subscription = *reinterpret_cast<const BYTE* const* const*>(parameter + 0x18);
        const BYTE* const* subscriptionsEnd = *reinterpret_cast<const BYTE* const* const*>(parameter + 0x1C);
        for (; subscription && subscription < subscriptionsEnd; ++subscription) {
            const BYTE* s = *subscription;
            ++view->subscriptions;
            const DWORD kind = *reinterpret_cast<const DWORD*>(s + 0x10);
            if (kind != 1 && kind != 2) continue;
            ++view->nodeSubscriptions;
            const BYTE* node = *reinterpret_cast<const BYTE* const*>(s);
            const DWORD id = *reinterpret_cast<const DWORD*>(node + 0x0C);
            const unsigned plays = *reinterpret_cast<const WORD*>(node + 0x18);
            if (plays) {
                if (view->listed < kListedPlaying) view->playingIds[view->listed++] = id;
                ++view->playing;
            }
            if (id == lineNode) {
                view->lineKind = static_cast<int>(kind);
                view->linePlays = plays;
                for (const BYTE* curve = *reinterpret_cast<const BYTE* const*>(s + 0x14); curve < *reinterpret_cast<const BYTE* const*>(s + 0x18); curve += 0x14)
                    if (*reinterpret_cast<const DWORD*>(curve + 4) == kGameDistance) ++view->lineCurves;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The newsreader's line's node in our bank: WwiseBankWriter.ObjectId of "sound:line:<name>" in the builder's voice
// bank, rfgk_voc_kiosk (GuerrillaKiosks: the prefix rfgk_ and Guerrilla's wave bank voc_kiosk).
DWORD LineNode(const std::string& line) { return line.empty() ? 0 : WwiseNameId("rfg:rfgk_voc_kiosk:sound:line:" + line); }

// The check of the broadcast's 3D sound in game. The user, 2026-11-19 10:42: "kiosk still is playing at full volume
// even tho I walked away", with every fade on game_distance and the distance sent at least every 500 ms. Every 5 s of a
// broadcast (36 lines a run, the first at its start) it logs the camera's distance to the kiosk; the game's sound
// object (where: its first place +0x24; +0x8B how many places, 1 or more for a 3D one; and how far the game measured
// its listener at its last update, +0x0C); and what Wwise holds (WwiseView): the object registered, its game_distance
// and the global one, the subscriptions to game_distance and which of their nodes play now, and whether the playing
// line's node is among them. So the next run tells a distance Wwise never got from a fade Wwise has but doesn't apply.
double g_nextSoundCheckMs = 0.0;
int g_soundChecks = 0;

void CheckSound(const BYTE* kiosk) {
    if (g_clockMs < g_nextSoundCheckMs || g_soundChecks >= 36) return;
    g_nextSoundCheckMs = g_clockMs + 5000.0;
    ++g_soundChecks;
    const float camera = Distance(g_cameraPosition, PlaceOf(kiosk));
    const BYTE* object = SoundObjectFromHandle(g_soundObject);
    if (!object) {
        rfa::LogDetail("Broadcast sound: camera %.1f m from the kiosk; the game has no sound object for the broadcast's handle %lu", camera, g_soundObject);
        return;
    }
    const float* place = reinterpret_cast<const float*>(object + 0x24);
    char game[256];
    sprintf_s(game, "the game's sound object %08lX at %.1f %.1f %.1f (%.1f m from the kiosk, %u place(s)), its listener %.1f m away as the game measured it",
              static_cast<DWORD>(reinterpret_cast<uintptr_t>(object)), place[0], place[1], place[2], Distance(place, PlaceOf(kiosk)),
              static_cast<unsigned>(object[0x8B]), *reinterpret_cast<const float*>(object + 0x0C));
    const bool linePlaying = g_voice && SoundGameObject(g_voice) != 0;
    const char* playing = linePlaying ? "the line" : "no line";
    const char* music = g_loopPlaying && SoundGameObject(g_loopPlaying) != 0 ? ", the music's loop" : "";
    const DWORD lineNode = linePlaying ? LineNode(g_voiceLine) : 0;
    WwiseView view{};
    view.lineKind = -1;
    char wwise[640];
    if (!ReadWwiseView(static_cast<DWORD>(reinterpret_cast<uintptr_t>(object)), lineNode, &view))
        sprintf_s(wwise, "Wwise's state couldn't be read this time");
    else if (!view.parameterFound)
        sprintf_s(wwise, "Wwise has no game_distance at all (%s)", view.registered ? "the object is registered" : "nor the object registered");
    else {
        char ids[kListedPlaying * 9 + 1] = "";
        for (unsigned i = 0; i < view.listed; ++i) sprintf_s(ids + i * 9, sizeof(ids) - i * 9, " %08lX", view.playingIds[i]);
        char line[160];
        if (!lineNode) sprintf_s(line, "no line playing");
        else if (view.lineKind < 0) sprintf_s(line, "the line %s's node %08lX NOT subscribed", g_voiceLine.c_str(), lineNode);
        else
            sprintf_s(line, "the line %s's node %08lX subscribed (kind %d, %u curve(s) on game_distance, playing %u)", g_voiceLine.c_str(), lineNode, view.lineKind,
                      view.lineCurves, view.linePlays);
        sprintf_s(wwise, "Wwise: the object %s, its game_distance %s%.1f m (the global value %.1f m, %u objects with their own); %u subscriptions to "
                         "game_distance (%u of nodes), %u of their nodes playing:%s; %s",
                  view.registered ? "registered" : "NOT registered", view.hasValue ? "" : "NONE, so ", view.hasValue ? view.value : view.global, view.global,
                  view.objectsWithValue, view.subscriptions, view.nodeSubscriptions, view.playing, ids, line);
    }
    rfa::LogDetail("Broadcast sound: camera %.1f m from the kiosk; %s; playing %s%s; Guerrilla's curve gives the newsreader %.1f dB here; %s", camera, game, playing,
             music, GuerrillaKioskDb(camera), wwise);
    // The listener as audiolib gives it to Wwise (its front and top need length 1 and right angles), with Log=1.
    static const float zero[3] = {0.0f, 0.0f, 0.0f};
    if (g_soundChecks <= 2)
        rfa::LogDetail("Broadcast sound: audiolib's listener %.1f m from the camera, its front %.2f %.2f %.2f (length %.2f), top %.2f %.2f %.2f (length %.2f), "
                       "their dot product %.2f; the sound object's orientation %.2f %.2f %.2f",
                       Distance(g_listenerPosition, g_cameraPosition), g_listenerFront[0], g_listenerFront[1], g_listenerFront[2], Distance(g_listenerFront, zero),
                       g_listenerTop[0], g_listenerTop[1], g_listenerTop[2], Distance(g_listenerTop, zero),
                       g_listenerFront[0] * g_listenerTop[0] + g_listenerFront[1] * g_listenerTop[1] + g_listenerFront[2] * g_listenerTop[2],
                       reinterpret_cast<const float*>(object + 0x30)[0], reinterpret_cast<const float*>(object + 0x30)[1],
                       reinterpret_cast<const float*>(object + 0x30)[2]);
}

const char* ScreenName(int screen) {
    const auto& all = tables::Screens();
    return screen >= 0 && screen < static_cast<int>(all.size()) ? all[screen].name.c_str() : "none";
}

// Guerrilla's kiosk_set_active (RM 0xB3A990).
void SetActive(bool active, const char* why) {
    const tables::Defaults& d = tables::Settings();
    g_active = active;
    if (active) {
        g_inactiveTimer.Invalidate();
        g_nextLineTimer.Set(0);
        g_screenTimer.Invalidate();
        g_sessionLines = 0;
        g_shouldDeactivate = false;
        g_nextSoundCheckMs = 0.0;  // the sound's first check at once
        for (int i : g_available) g_lineStates[i].playedThisSession = false;
        g_sessionCutoff = d.sessionMinLines + (d.sessionMaxLines > d.sessionMinLines ? rand() % (d.sessionMaxLines - d.sessionMinLines + 1) : 0);
        screens::SetBroadcastScreen(d.activeScreen);
        return;
    }
    g_inactiveTimer.Set(d.inactiveDelayMs);
    g_nextLineTimer.Invalidate();
    g_screenTimer.Invalidate();
    Stop(g_voice);
    Stop(g_introPlaying);
    Stop(g_loopPlaying);
    Stop(g_outroPlaying);
    if (g_soundObject) EndSoundObject(g_soundObject);  // the broadcast's own sound object, made at its start
    g_soundObject = 0;
    if (g_kiosk >= 0) rfa::LogDetail("Broadcast: ended at kiosk %08lX after %d lines (%s)", tables::Kiosks()[g_kiosk].handle, g_sessionLines, why);
    g_kiosk = -1;
    screens::SetBroadcastScreen(-1);
}

// Guerrilla's kiosk_start_deactivate (RM 0xB2E6E0): the loop stopped; the station ID and the outro together.
void StartDeactivate(const std::string& why) {
    g_shouldDeactivate = true;
    g_endingWhy = why;
    Stop(g_loopPlaying);
    const tables::Defaults& d = tables::Settings();
    int stationMs = 0;
    auto station = g_lineSounds.find(Lower(d.stationId));
    if (station != g_lineSounds.end()) {
        Stop(g_voice);
        g_voice = Play(station->second.event);
        if (g_voice) stationMs = station->second.ms;
    }
    g_outroPlaying = Play(g_outro.event);
    g_nextLineTimer.Set(stationMs + (g_outroPlaying ? g_outro.ms : 0));
    rfa::LogDetail("Broadcast: ending (%s): station ID %s, outro %s", why.c_str(), stationMs ? "played" : "not played", g_outroPlaying ? "played" : "not played");
}

// How far the player is from an object on x and z (as Guerrilla measures a kiosk's 50 m), or -1.
float PlayerDistance(const BYTE* object) {
    const BYTE* player = Player();
    if (!player || !object) return -1.0f;
    const float* p = PlaceOf(player);
    const float* o = PlaceOf(object);
    const float dx = p[0] - o[0], dz = p[2] - o[2];
    return std::sqrt(dx * dx + dz * dz);
}

// guerrilla_campaign's mission flag (GuerrillaCampaign_Running's second answer; none is ported yet).
bool MissionRunning() {
    int mode = 0, mission = 0;
    GuerrillaCampaign_Running(&mode, &mission);
    return mission != 0;
}

}  // namespace

void Init() {
    srand(static_cast<unsigned>(time(nullptr)));
    Load();
    ReadSounds();
    rfa::LogDetail("Broadcast: %u of %u lines available (%u start unlocked), %u voice lines and %s built (kiosk_sounds.txt)%s",
             static_cast<unsigned>(g_available.size()), static_cast<unsigned>(tables::Lines().size()),
             static_cast<unsigned>(std::count_if(tables::Lines().begin(), tables::Lines().end(), [](const tables::Line& l) { return l.startsUnlocked; })),
             static_cast<unsigned>(g_lineSounds.size()), g_intro.event.empty() ? "no music" : "the music",
             g_soundsRead ? "" : ": none (build the world with editor\\GuerrillaBuilder.exe); no broadcast");
    g_inited = true;
}

void Update(float seconds) {
    if (!g_inited || !g_soundsRead || tables::Lines().empty()) return;
    g_clockMs += seconds * 1000.0;
    const tables::Defaults& d = tables::Settings();
    const int alert = AlertLevel();
    // Guerrilla's alert callback: the alert rising from green ends a broadcast.
    if (alert >= 1 && g_lastAlert == 0 && g_active) SetActive(false, "the alert rose");
    g_lastAlert = alert;
    if (!g_active) {
        if (!g_inactiveTimer.Valid()) g_inactiveTimer.SetRandom(d.inactiveDelayMs, 2 * d.inactiveDelayMs);
        // Guerrilla's kiosk_maybe_activate.
        if (MissionRunning()) return;
        if (alert >= 1) {
            g_inactiveTimer.Set(d.inactiveDelayMs);
            return;
        }
        if (!g_inactiveTimer.Elapsed()) return;
        float howFar = 0;
        const int kiosk = PotentialKiosk(&howFar);
        if (kiosk < 0) {
            g_inactiveTimer.Set(d.inactiveDelayMs);
            return;
        }
        // The broadcast's sounds play on a sound object of its own, made at the kiosk (and ended with the broadcast).
        g_soundObject = MakeSoundObject(PlaceOf(KioskObject(kiosk)), 0);
        if (!g_soundObject) {
            RFA_LOG_FIRST(3, "Broadcast: the game gave no sound object; none started");
            g_inactiveTimer.Set(d.inactiveDelayMs);
            return;
        }
        SetActive(true, "");
        g_kiosk = kiosk;
        g_introPlaying = Play(g_intro.event);
        g_loopPlaying = Play(g_loop.event, d.musicLoopDelayMs);
        g_nextLineTimer.Set(d.lineDelayMs + d.musicLoopDelayMs);
        const tables::Kiosk& k = tables::Kiosks()[kiosk];
        rfa::LogDetail("Broadcast: started at kiosk %08lX (%s) %.0f m away, %d lines this time (%u available); intro %s, loop %s; screens show %s",
                 k.handle, k.gameplay.c_str(), howFar, g_sessionCutoff, static_cast<unsigned>(g_available.size()),
                 g_introPlaying ? "playing" : "NOT PLAYING", g_loopPlaying ? "playing" : "NOT PLAYING", ScreenName(d.activeScreen));
        return;
    }
    // The sounds follow the kiosk (kiosk_update_current_sound_pos); the kiosk gone or destroyed, the broadcast ends.
    BYTE* kiosk = KioskObject(g_kiosk);
    if (!kiosk || IsKilled(kiosk)) {
        SetActive(false, kiosk ? "the kiosk was destroyed" : "the kiosk is gone");
        return;
    }
    MoveSoundObject(g_soundObject, PlaceOf(kiosk), nullptr);
    CheckSound(kiosk);
    // Two seconds into each line: whether Wwise is still playing it. The newsreader's lines are the first of
    // Guerrilla's voices in Wwise's ADPCM (each 5 s or longer), so "still playing" means the game decodes that codec.
    if (!g_voiceChecked && g_voice && g_clockMs - g_voiceStartedMs >= 2000.0) {
        g_voiceChecked = true;
        if (SoundGameObject(g_voice) != 0)
            RFA_LOG_DETAIL_FIRST(20, "Broadcast: line %s is still playing 2 s on (%.1f s long): Wwise took its file and is decoding it", g_voiceLine.c_str(),
                          g_voiceMs / 1000.0f);
        else
            RFA_LOG_FIRST(20, "Broadcast: line %s had ended 2 s on, of its %.1f s: it did not play (Wwise's reason, if it gave one, is in a "
                              "\"Wwise:\" line)", g_voiceLine.c_str(), g_voiceMs / 1000.0f);
    }
    if (!g_nextLineTimer.Elapsed()) {
        if (!g_nextLineTimer.Valid()) return;
        if (g_screenTimer.Elapsed()) {
            screens::SetBroadcastScreen(g_nextScreen);
            g_nextScreen = -1;
            g_screenTimer.Invalidate();
        }
        return;
    }
    if (g_shouldDeactivate) {
        SetActive(false, (g_endingWhy + "; the station ID and outro played").c_str());
        g_inactiveTimer.Set(d.inactiveDelayLongMs);
        return;
    }
    const int potential = PotentialKiosk();
    if (potential < 0) {
        char why[128];
        sprintf_s(why, "the player went out of range: %.0f m from the kiosk, over %.0f m", PlayerDistance(kiosk), d.maxDistance);
        StartDeactivate(why);
        return;
    }
    if (g_sessionLines >= g_sessionCutoff) {
        StartDeactivate("all " + std::to_string(g_sessionCutoff) + " of its lines played");
        return;
    }
    const int line = FindAvailableLine();
    if (line < 0) {
        StartDeactivate("no line left to play after " + std::to_string(g_sessionLines));
        return;
    }
    const tables::Line& l = tables::Lines()[line];
    const Sound& sound = g_lineSounds[Lower(l.name)];
    Stop(g_voice);
    g_voice = Play(sound.event);
    if (!g_voice) {
        rfa::Log("Broadcast: line %s (event %s) didn't play (its bank isn't loaded?)", l.name.c_str(), sound.event.c_str());
        SetActive(false, "a line didn't play");
        return;
    }
    g_voiceLine = l.name;
    g_voiceStartedMs = g_clockMs;
    g_voiceMs = sound.ms;
    g_voiceChecked = false;
    g_nextLineTimer.Set(sound.ms + d.lineDelayMs);
    ++g_lineStates[line].plays;
    ++g_sessionLines;
    g_lineStates[line].playedThisSession = true;
    if (l.screen >= 0) {
        g_screenTimer.Set(l.screenDelayMs);
        g_nextScreen = l.screen;
    }
    if (!g_screenTimer.Valid() || !g_screenTimer.Elapsed()) screens::SetBroadcastScreen(d.activeScreen);
    rfa::LogDetail("Broadcast: line %d of %d, %s (%.1f s; played %d of %d times; picture %s in %d ms)", g_sessionLines, g_sessionCutoff, l.name.c_str(),
             sound.ms / 1000.0f, g_lineStates[line].plays, l.minReplays, ScreenName(l.screen), l.screenDelayMs);
    if (g_lineStates[line].plays >= l.minReplays && static_cast<int>(g_available.size()) > d.minAvailableLines)
        g_available.erase(std::remove(g_available.begin(), g_available.end(), line), g_available.end());
    Save();
}

void Forget() {
    // Out of our worlds: the level's sound objects went with it, so nothing is stopped or ended here.
    g_active = false;
    g_kiosk = -1;
    g_soundObject = g_voice = g_introPlaying = g_loopPlaying = g_outroPlaying = 0;
    g_inactiveTimer.Invalidate();
    g_nextLineTimer.Invalidate();
    g_screenTimer.Invalidate();
    g_lastAlert = 0;
}

bool Active(DWORD* handle, float* place) {
    if (!g_active || g_kiosk < 0) return false;
    if (handle) *handle = tables::Kiosks()[g_kiosk].handle;
    if (place)
        if (BYTE* kiosk = KioskObject(g_kiosk)) memcpy(place, PlaceOf(kiosk), 3 * sizeof(float));
    return true;
}

int UnlockActivity(const char* activity) {
    if (!activity || !g_inited) return 0;
    int unlocked = 0;
    const auto& lines = tables::Lines();
    for (size_t i = 0; i < lines.size(); ++i)
        if (_stricmp(lines[i].activity.c_str(), activity) == 0 && Unlock(static_cast<int>(i))) ++unlocked;
    if (unlocked) {
        rfa::LogDetail("Broadcast: %d lines unlocked by the activity %s", unlocked, activity);
        Save();
    }
    return unlocked;
}

int UnlockMission(const char* mission) {
    if (!mission || !g_inited) return 0;
    int unlocked = 0;
    const auto& lines = tables::Lines();
    for (size_t i = 0; i < lines.size(); ++i)
        if (_stricmp(lines[i].mission.c_str(), mission) == 0 && Unlock(static_cast<int>(i))) ++unlocked;
    if (unlocked) {
        rfa::LogDetail("Broadcast: %d lines unlocked by the mission %s", unlocked, mission);
        Save();
    }
    return unlocked;
}

int UnlockLine(const char* line) {
    if (!line || !g_inited) return 0;
    const auto& lines = tables::Lines();
    for (size_t i = 0; i < lines.size(); ++i)
        if (_stricmp(lines[i].name.c_str(), line) == 0 && Unlock(static_cast<int>(i))) {
            rfa::LogDetail("Broadcast: line %s unlocked", line);
            Save();
            return 1;
        }
    return 0;
}

}  // namespace kiosks::broadcast
