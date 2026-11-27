// The player: game objects and the listener, events and their actions, containers, voices on OpenAL sources.
#pragma once

#include "bank.h"
#include "media.h"
#include "openal.h"
#include "rfa_audio.h"
#include "worker.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rfa_audio {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct GameObject {
    uint64_t id = 0;
    Vec3 position, front{0, 0, 1};
    bool placed = false;  // never given a place: it is where the listener is
    std::unordered_map<uint32_t, uint32_t> switches;
    std::unordered_map<uint32_t, float> parameters;
    float obstruction = 0, occlusion = 0;                   // 0-100, as the game gives Wwise
    std::vector<std::pair<uint32_t, float>> environments;   // the reverbs its sounds go to, each by how much (0-1)
    // Its speed (m/s), from its places over time (for Doppler); the place and time it was last measured from.
    Vec3 velocity, sampled;
    double sampledAt = -1;
};

// A gain moving in a straight line from one value to another over a time.
struct Fade {
    float from = 1, to = 1;
    double start = 0, end = 0;
    float At(double now) const;
};

struct Play;

// A voice: a sound instance's medium on an OpenAL source, or virtual (no source: too quiet, or none free).
// A voice moving on its node's paths (3D on paths, around the listener): the node, the path, its random offset, and
// when it set out on it.
struct PathState {
    const Node* node = nullptr;
    uint32_t path = 0;
    float dx = 0, dz = 0;
    double start = 0;
};

struct Voice {
    MediumInfo medium;
    std::shared_ptr<Decoded> decoded;  // decoded whole (from the cache), or
    std::unique_ptr<Stream> stream;    // streamed from the file, read here (a loopback device), or
    std::shared_ptr<StreamFeed> feed;  // streamed from the file, read by the media worker
    std::shared_ptr<MediumLoad> load;  // its medium being read by the worker: it waits, with no source, until then
    bool loadToResume = false;         // the load is for a virtual voice heard again (else for its start)
    bool silence = false;              // Wwise's silence plug-in: no sound, its length only (always virtual)
    float appliedLowPass = -1;         // the low-pass last set on its source (-1: none yet)
    bool streamStarted = false;        // its stream has played (a stop after this, with pieces queued, is an underrun)
    ALuint source = 0;
    ALuint buffers[4] = {};            // a stream's, queued in turn
    std::vector<ALuint> spare;         // a stream's buffers not queued (waiting for pieces)
    int loopCount = 1;                 // 0 for ever
    bool threeD = false;
    int channels = 0, rate = 0, codec = 0;
    double length = 0;                 // one pass, in seconds
    double startedAt = 0;              // when it started (again)
    bool virtualVoice = false, ended = false, everPlayed = false;
    double quietGraceUntil = 0;        // too quiet at its start: it waits, virtual, till then for its place (0: none)
    PathState path;                    // on its node's paths (path.node null: at its object)
    uint32_t sentTo[4] = {};           // the reverbs its sends go to now, and by how much (set again only on a change)
    float sentGain[4] = {};
    float levelDb = -999, distance = 0;
    int priority = 50;
    WhenQuiet whenQuiet = WhenQuiet::PlayOn;
    uint32_t mediumId = 0, bus = 0;
    int64_t framesQueued = 0;          // a stream's frames given to OpenAL
    struct Instance* owner = nullptr;  // the sound instance it plays for
};

// A node playing (a sound, a container), or samples given; containers hold their children's instances.
struct Instance {
    uint64_t serial = 0;
    Play* play = nullptr;
    const Node* node = nullptr;        // null: samples or a medium given
    Instance* parent = nullptr;
    std::vector<std::unique_ptr<Instance>> children;
    Fade fade;
    bool stopping = false, broken = false, finished = false;
    double startedAt = 0;
    float randomVolume = 0, randomPitch = 0;
    uint32_t limitOwner = 0;
    // a continuous container
    int picksLeft = 0, loopsLeft = 0, sequencePosition = 0;
    double nextAt = 0;
    bool exhausted = false;            // its passes are played: nothing more starts
    std::vector<uint32_t> shuffleRound;
    // a switch container
    uint32_t switchValue = 0;
    // samples given
    float ownVolume = 0, ownPitch = 0;
    bool ownThreeD = false;
    std::unique_ptr<Voice> voice;
};

// What one post made: the event's actions, waiting and played.
struct Play {
    uint32_t playingId = 0, eventId = 0;
    uint64_t object = 0;
    std::vector<std::unique_ptr<Instance>> roots;
    int pendingActions = 0;
    int voicesStarted = 0;             // voices it started (virtual ones too); 0: it never made a sound
    bool ended = false;
    bool paused = false;               // paused on its own (rfa_audio_pause_playing): its time stands still
    double pausedAt = 0;
    bool controlsOnly = false;         // its event's plays left out (rfa_audio_post_controls): another engine plays them
    bool startNoted = false;           // its first voice's start told (rfa_audio_set_start_callback)
};

// What OpenAL Soft's panning loses for a mono sound (dB, all outputs' power against the sound's), on a grid of
// directions around the listener: 24 azimuths (every 15 degrees from ahead, clockwise) by 7 elevations (every 30 degrees
// from below to above); and for one in the middle, not spatialized (a 2D voice). Measured at opening (MeasurePanning).
struct PanningLoss {
    static constexpr int Azimuths = 24, Elevations = 7;
    float db[Elevations][Azimuths] = {};
    float centre = 0;
    bool valid = false;
    float At(const Vec3& place) const;
};

struct PendingAction {
    double at = 0;
    uint32_t playingId = 0;
    const Action* action = nullptr;
};

struct StartNote {
    uint32_t playingId, bankId, mediumId;
};

struct EndNote {
    uint32_t playingId, eventId;
    uint64_t object;
    int voices;
};

class Engine {
public:
    ~Engine();
    std::mutex lock;          // every public call holds it
    Library library;
    OpenAl al;
    rfa_audio_end_callback endCallback = nullptr;
    rfa_audio_start_callback startCallback = nullptr;
    void* startUser = nullptr;
    std::vector<StartNote> starts;
    std::vector<StartNote> TakeStarts()
    {
        std::vector<StartNote> taken;
        taken.swap(starts);
        return taken;
    }
    int MediumPrefix(uint32_t bankId, uint32_t mediumId, uint8_t* out, int max);
    rfa_audio_log_callback logCallback = nullptr;  // (rfa_audio_set_log_callback)
    void* logUser = nullptr;
    int quietLogsLeft = 0;
    std::unordered_set<uint32_t> quietLogged;     // the events already written as ended too quiet
    void* endUser = nullptr;

    bool Open(const rfa_audio_settings& settings, std::string& why);
    void Close();
    bool IsOpen() const { return context != nullptr; }
    std::string DeviceInfo() const;

    uint32_t LoadBank(const std::wstring& bank, const std::wstring& media, std::string& why);
    // One of Armageddon's banks from an archive, its loose overrides (bank, media) read instead where given; one loaded
    // already is loaded again, or kept as it is (keepLoaded).
    uint32_t LoadBankFromArchive(const std::wstring& archive, const std::string& name, std::string& why, const std::wstring& looseBank = L"",
                                 const std::wstring& looseMedia = L"", bool keepLoaded = false);
    void UnloadBank(uint32_t bankId);
    void StopBank(uint32_t bankId, int fadeMs);

    uint32_t Post(uint32_t eventId, uint64_t object, bool controlsOnly = false);
    uint32_t PlayNode(uint32_t nodeId, uint64_t object);
    uint32_t PlayMedium(uint32_t bankId, uint32_t mediumId, bool loop, uint64_t object);
    uint32_t PlaySamples(const int16_t* samples, int frames, int channels, int rate, bool loop, int loopStart, float volumeDb,
                         float pitchCents, bool threeD, uint64_t object);
    void StopPlaying(uint32_t playingId, int fadeMs);
    void StopObject(uint64_t object, int fadeMs);
    bool IsPlaying(uint32_t playingId) const;

    void SetPosition(uint64_t object, const float* position, const float* front);
    void SetListener(const float* position, const float* front, const float* up);
    // Space (step 4): an object's obstruction and occlusion, its environments (the reverbs its sounds go to), and
    // Doppler (0 off; 1 as sound travels; the speeds are measured from the places given).
    void SetObstruction(uint64_t object, float obstruction, float occlusion);
    void SetEnvironments(uint64_t object, const uint32_t* ids, const float* values, int count);
    void SetDoppler(float factor);
    // Guerrilla's occlusion (engine.cpp, "Guerrilla's occlusion"): a ray of the listener's as the game casts it, which
    // occlusion 3D sounds get (0 the obstruction through init.bnk's curves, Armageddon's way; 1 Guerrilla's), and how it
    // stands (the voices muffled at the last update, the most, the listener's enclosure, the room's reverb type).
    void SetListenerRay(int direction, int hits, const float* distances, const int* materials, bool blocked);
    void SetOcclusionModel(int model) { occlusionModel = model == 1 ? 1 : 0; }
    int OcclusionStatus(float& most, float& enclosure, int& reverbType) const;
    void SetParameter(uint64_t object, uint32_t parameter, float value);
    void ResetParameter(uint64_t object, uint32_t parameter);
    void SetSwitch(uint64_t object, uint32_t group, uint32_t value);
    void SetState(uint32_t group, uint32_t state);
    void RemoveObject(uint64_t object);
    // Everything paused, its time standing still (the game's pause), or going on again.
    void PauseAll(bool pause);
    bool PausedAll() const { return pausedAll; }
    // One playing id paused, or going on again.
    void PausePlay(uint32_t playingId, bool pause);

    // One update at the engine's time now (the wall clock, or the time rendered for loopback).
    void Update();
    int Render(float* out, int frames);
    double Now() const { return now; }
    bool Loopback() const { return loopback; }

    // For tests (tools\research\audio_worker_test.cpp): set before Open, a loopback device reads its media through
    // the worker as the game's and the editor's do, instead of inline; Waiting says whether a voice waits for it
    // (a medium being read, or a stream with nothing queued and more to come), so the test renders on only then.
    bool forceWorker = false;
    bool Waiting();

    int Voices(rfa_audio_voice* out, int max);
    void Status(rfa_audio_status& out);

    // Ends to report, taken out under the lock and reported outside it.
    std::vector<EndNote> TakeEnds();

    // What a bank's tree looks like, for the editor and checks.
    void EventInfo(uint32_t eventId, rfa_audio_event_info& out);
    // The buses an event's plays go through that duck other buses (for the hybrid's mirror of Wwise's sounds).
    std::vector<uint32_t> EventDuckingBuses(uint32_t eventId) const;
    std::vector<uint32_t> NodeDuckingBuses(uint32_t nodeId) const;  // a node's (a voice line's), as an event's
    // What an event's actions act on (RFA_AUDIO_REACH_*), and from it where the hybrid sends it (RFA_AUDIO_ROUTE_*),
    // the banks `here` played by this engine.
    int EventReach(uint32_t eventId) const;
    int EventRoute(uint32_t eventId, uint32_t bankId, const std::unordered_set<uint32_t>& here);
    std::string Describe(uint32_t id);
    bool MediumSeconds(const Node& sound, double& seconds, std::string& why);

private:
    // Starting nodes
    std::unique_ptr<Instance> Start(const Node* node, Play& play, Instance* parent, int fadeInMs);
    bool StartChild(Instance& in, uint32_t child, int fadeInMs);
    bool StartNextPick(Instance& in, int fadeInMs = 0);
    bool StartVoice(Instance& in);
    bool BeginVoice(Instance& in, bool counted);
    void FinishLoad(Instance& in);
    uint32_t Pick(Instance& in);
    uint32_t SwitchValue(const Node& node, const GameObject& object) const;
    void Sweep();

    // Updating
    void RunPending();
    void Execute(const Action& action, Play& play);
    void UpdateInstance(Instance& in, GameObject& object);
    void UpdateVoice(Instance& in, GameObject& object);
    void Finish(Instance& in);
    void StopInstance(Instance& in, int fadeMs);
    void BreakInstance(Instance& in);
    void StopMatching(uint32_t target, uint64_t object, bool everywhere, int fadeMs);
    void BreakMatching(uint32_t target, uint64_t object, bool everywhere);
    bool Matches(const Node* node, uint32_t target) const;

    // Voices
    struct Level {
        float db = 0;          // nodes, curves, distance, bus, random ranges: what "too quiet" is judged on
        float fadeGain = 1;    // the fades on its instance and those above it
        float pitchCents = 0;
        float distance = 0;
        bool threeD = false;
        int priority = 50;
        uint32_t bus = 0;
        WhenQuiet whenQuiet = WhenQuiet::PlayOn;
        float lowPass = 0;     // Wwise's low-pass (0 none, 100 the most): the nodes', their curves', the distance's
        float spread = 0;      // its attenuation's spread at its distance (0 a point, 100 all around)
        float occlusion = 0;   // Guerrilla's (0-5), when it muffles this voice
    };
    Level LevelOf(const Instance& in, const GameObject& object) const;
    // The node whose 3D settings a node takes (its own, or the first above it that has them, or the top's).
    const Node* PositioningNode(const Node* n) const;
    // 3D on paths: a voice set out on one of its node's paths, and where it is on them now (around the listener: x
    // right, y up, z ahead, metres).
    void StartPath(Voice& v, const Node* positioning);
    Vec3 PathPlace(const Voice& v) const;
    std::unordered_map<uint32_t, uint32_t> nextPath;  // each node's next path, played in sequence
    float ParameterValue(const GameObject& object, uint32_t parameter, float distance) const;
    // A voice ended too quiet, written to the log callback with its level's parts (once an event).
    void LogQuiet(const Instance& in, const GameObject& object, float levelDb);
    bool AcquireSource(Voice& voice, ALuint& source);
    void ReleaseSource(Voice& voice);
    bool BeginOnSource(Voice& voice);
    void ApplySource(Voice& voice, const Level& level, const GameObject& object);
    bool FillStream(Voice& voice, ALuint buffer);
    void QueueReady(Voice& voice);
    void ServiceStream(Voice& voice);
    // Paused plays: only what needs no time (a stop at once) goes on.
    void UpdatePaused(Instance& in);
    void PauseVoices(Instance& in, bool pause);
    void ShiftTimes(Instance& in, double seconds);
    void ClockNow();

    GameObject& Object(uint64_t id);
    const Node* LimitOwner(const Node* node) const;
    Vec3 ToListener(const Vec3& position) const;
    Vec3 ToListenerDirection(const Vec3& direction) const;  // a direction (a speed) as the listener sees it
    // A speed measured from places over time: smoothed, and nothing once the places stop coming.
    void Measure(Vec3& velocity, Vec3& sampled, double& sampledAt, const Vec3& place);
    // Reverb: init.bnk's Matrix Reverb sets (FXPR) as EFX reverbs, each on a slot of its own, made the first time an
    // object's environment names it; a voice's sends to its object's environments, by how much.
    struct ReverbSlot {
        ALuint slot = 0, effect = 0;
    };
    std::unordered_map<uint32_t, ReverbSlot> reverbSlots;
    ALuint ReverbSlotFor(uint32_t environment);
    void ApplySends(Voice& v, const GameObject& object, bool spatial);
    int maxSends = 0;                                  // OpenAL Soft's sends a source (0: no reverb)
    std::unordered_map<ALuint, std::vector<ALuint>> sendFilters;  // each source's sends' gains
    float dopplerFactor = 0;
    Vec3 listenerVelocity, listenerSampled;
    double listenerSampledAt = -1;
    // Guerrilla's occlusion: the listener's rays (its audiolib_raycast_data: the first three hits, valid when the first is
    // within 45 m), the room they make (audiolib_room_dimensions), the listener's enclosure (its listener_fov).
    struct ListenerRay {
        int hits = 0;
        float distance[3] = {};
        int material[3] = {};
        bool valid = false, blocked = false;
    };
    ListenerRay listenerRays[26];
    struct Room {
        int shape = 0, size = 0, reverbType = 0;
        float volume = 0, height = 0, northSouth = 0, eastWest = 0, northEastSouthWest = 0, northWestSouthEast = 0;
        bool valid = false;
    } room;
    float listenerEnclosure = 0;
    int occlusionModel = 0;
    int occludedVoices = 0;  // at the last update, and the most of them
    float mostOcclusion = 0;
    float RayDistance(int direction) const;
    void WorkOutRoom();
    float GuerrillaOcclusion(const Vec3& toSound, float distance, float distanceMin) const;
    Play* FindPlay(uint32_t playingId) const;
    uint32_t NewPlay(uint32_t eventId, uint64_t object);
    void DescribeNode(const Node& node, int depth, std::string& text);

    ALCdevice* device = nullptr;
    ALCcontext* context = nullptr;
    bool loopback = false, ownThread = false;
    bool inlineReads = false;  // media read here, under the lock (a loopback device, the check); else by the worker
    int sampleRate = 48000, maxSources = 128;
    int directChannels = 0;        // AL_DIRECT_CHANNELS_SOFT's value for a 2D stereo voice (0: OpenAL Soft can't)
    bool canUnspatialize = false;  // AL_SOURCE_SPATIALIZE_SOFT is there (2D mono voices aren't panned)
    bool canSpread = false;        // AL_SOURCE_RADIUS is there (AL_EXT_SOURCE_RADIUS: a spread sound)
    PanningLoss panning;           // what OpenAL Soft's panning loses for a mono voice, by direction (made up for)
    float threshold = Silent;
    double now = 0;
    int64_t renderedFrames = 0;
    int64_t clockStart = 0, clockRate = 1;
    std::thread thread;
    bool stopThread = false;

    MediaCache cache;
    MediaWorker worker;       // reads media off the lock (not with a loopback device)
    bool pausedAll = false;
    int64_t pausedCounter = 0, pausedSince = 0;  // the clock's time paused in all, and when this pause began
    std::vector<ALuint> freeSources;
    std::unordered_map<ALuint, ALuint> sourceFilters;  // each source's low-pass filter, made when first needed
    int sourcesMade = 0;
    std::vector<Voice*> liveVoices;

    std::unordered_map<uint64_t, GameObject> objects;
    std::unordered_map<uint32_t, float> globalParameters;
    std::unordered_map<uint32_t, uint32_t> states;
    // What actions have set on a node (set volume, pitch, low-pass; mute), for one object or every one (0).
    // A value an action sets, moving to it over the action's fade along the fade's curve (Wwise's shapes).
    struct Ramp {
        float from = 0, to = 0;
        double start = 0, seconds = 0;
        uint32_t shape = 4;
        float At(double now) const;
        void Go(float value, double now, double fadeSeconds, uint32_t fadeShape);
        bool Settled(double now) const { return seconds <= 0 || now >= start + seconds; }
    };
    // What actions set on a node or bus: volume (dB), pitch (cents), low-pass (0-100), and a mute (0 heard, 1 silent:
    // silent, but not "too quiet", so a mute doesn't end voices or make them virtual).
    struct ActionValues {
        Ramp volume, pitch, lowPass, mute;
        float MuteGain(double now) const { return 1.0f - std::clamp(mute.At(now), 0.0f, 1.0f); }
        bool Empty(double now) const
        {
            return volume.Settled(now) && pitch.Settled(now) && lowPass.Settled(now) && mute.Settled(now) && volume.to == 0 && pitch.to == 0 &&
                   lowPass.to == 0 && mute.to == 0;
        }
    };
    using ActionKeyType = std::pair<uint32_t, uint64_t>;
    static ActionKeyType ActionKey(uint32_t node, uint64_t object) { return {node, object}; }
    std::map<ActionKeyType, ActionValues> actionValues;
    int postDepth = 0;  // events posted by events, nested

    // The mix (init.bnk's buses): each state group's last change (for its transition), what actions set on buses,
    // ducking (each ducking bus and bus it ducks: when something last played through it, the ducking now), each
    // ducked bus's ducking in all, sounds playing through a bus elsewhere (Wwise's, told by the game's hooks).
    struct StateChange {
        uint32_t from = 0, to = 0;
        double at = -1e9, seconds = 0;
    };
    std::unordered_map<uint32_t, StateChange> stateChanges;
    std::unordered_map<uint32_t, ActionValues> busActionValues;
    struct DuckState {
        double lastActive = -1e9;
        float db = 0;
    };
    std::map<std::pair<uint32_t, uint32_t>, DuckState> ducking;
    std::unordered_map<uint32_t, float> busDuckDb;
    std::unordered_map<uint32_t, int> externalOn;
    double lastMixAt = 0;
public:
    // A sound starts (delta 1) or ends (-1) on a bus outside this engine (Wwise's, in the hybrid): it ducks as ours do.
    void NoteBus(uint32_t bus, int delta);
private:
    void UpdateMix();
    void BusLevel(uint32_t bus, Level& level) const;
    std::vector<uint32_t> BusChain(uint32_t bus) const;
    StateValues StateValuesFor(uint32_t group, uint32_t state, const Bus* bus) const;
    float GlobalParameter(uint32_t parameter) const;
    bool LimitBuses(Voice& voice);
    bool ThroughBus(const Instance& in, uint32_t bus) const;
    Vec3 listenerPosition, listenerFront{0, 0, 1}, listenerUp{0, 1, 0};

    std::unordered_map<uint32_t, std::unique_ptr<Play>> plays;
    std::vector<PendingAction> pending;
    std::unordered_map<uint32_t, std::deque<Instance*>> limited;      // by limit owner: its instances, oldest first
    std::unordered_map<uint64_t, int> sequencePositions;             // a step sequence's place (by node, or node and object)
    std::unordered_map<uint64_t, std::vector<uint32_t>> randomHistory;  // a random container's last picks
    std::vector<EndNote> ends;
    uint32_t nextPlayingId = 0;
    uint64_t nextSerial = 0;
    std::mt19937 random;
    std::vector<int16_t> streamScratch;

    // Counts for the status
    int64_t refused = 0, endedQuiet = 0, stolen = 0, underruns = 0, mediaFailed = 0, noSource = 0;
};

Engine& TheEngine();

}  // namespace rfa_audio
