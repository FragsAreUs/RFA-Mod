// The player: game objects and the listener, events and their actions, containers, voices on OpenAL sources.
#pragma once

#include "bank.h"
#include "media.h"
#include "openal.h"
#include "rfa_audio.h"

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
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
};

// A gain moving in a straight line from one value to another over a time.
struct Fade {
    float from = 1, to = 1;
    double start = 0, end = 0;
    float At(double now) const;
};

struct Play;

// A voice: a sound instance's medium on an OpenAL source, or virtual (no source: too quiet, or none free).
struct Voice {
    MediumInfo medium;
    std::shared_ptr<Decoded> decoded;  // decoded whole (from the cache), or
    std::unique_ptr<Stream> stream;    // streamed from the file
    ALuint source = 0;
    ALuint buffers[4] = {};            // a stream's, queued in turn
    int loopCount = 1;                 // 0 for ever
    bool threeD = false;
    int channels = 0, rate = 0, codec = 0;
    double length = 0;                 // one pass, in seconds
    double startedAt = 0;              // when it started (again)
    bool virtualVoice = false, ended = false, everPlayed = false;
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
};

struct PendingAction {
    double at = 0;
    uint32_t playingId = 0;
    const Action* action = nullptr;
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
    void* endUser = nullptr;

    bool Open(const rfa_audio_settings& settings, std::string& why);
    void Close();
    bool IsOpen() const { return context != nullptr; }
    std::string DeviceInfo() const;

    uint32_t LoadBank(const std::wstring& bank, const std::wstring& media, std::string& why);
    void UnloadBank(uint32_t bankId);

    uint32_t Post(uint32_t eventId, uint64_t object);
    uint32_t PlayNode(uint32_t nodeId, uint64_t object);
    uint32_t PlayMedium(uint32_t bankId, uint32_t mediumId, bool loop, uint64_t object);
    uint32_t PlaySamples(const int16_t* samples, int frames, int channels, int rate, bool loop, int loopStart, float volumeDb,
                         float pitchCents, bool threeD, uint64_t object);
    void StopPlaying(uint32_t playingId, int fadeMs);
    void StopObject(uint64_t object, int fadeMs);
    bool IsPlaying(uint32_t playingId) const;

    void SetPosition(uint64_t object, const float* position, const float* front);
    void SetListener(const float* position, const float* front, const float* up);
    void SetParameter(uint64_t object, uint32_t parameter, float value);
    void ResetParameter(uint64_t object, uint32_t parameter);
    void SetSwitch(uint64_t object, uint32_t group, uint32_t value);
    void SetState(uint32_t group, uint32_t state);
    void RemoveObject(uint64_t object);

    // One update at the engine's time now (the wall clock, or the time rendered for loopback).
    void Update();
    int Render(float* out, int frames);
    double Now() const { return now; }
    bool Loopback() const { return loopback; }

    int Voices(rfa_audio_voice* out, int max);
    void Status(rfa_audio_status& out);

    // Ends to report, taken out under the lock and reported outside it.
    std::vector<EndNote> TakeEnds();

    // What a bank's tree looks like, for the editor and checks.
    void EventInfo(uint32_t eventId, rfa_audio_event_info& out);
    std::string Describe(uint32_t id);
    bool MediumSeconds(const Node& sound, double& seconds, std::string& why);

private:
    // Starting nodes
    std::unique_ptr<Instance> Start(const Node* node, Play& play, Instance* parent, int fadeInMs);
    bool StartChild(Instance& in, uint32_t child, int fadeInMs);
    bool StartNextPick(Instance& in);
    bool StartVoice(Instance& in);
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
    };
    Level LevelOf(const Instance& in, const GameObject& object) const;
    float ParameterValue(const GameObject& object, uint32_t parameter, float distance) const;
    bool AcquireSource(Voice& voice, ALuint& source);
    void ReleaseSource(Voice& voice);
    bool BeginOnSource(Voice& voice);
    void ApplySource(Voice& voice, const Level& level, const GameObject& object);
    bool FillStream(Voice& voice, ALuint buffer);
    void ServiceStream(Voice& voice);

    GameObject& Object(uint64_t id);
    const Node* LimitOwner(const Node* node) const;
    Vec3 ToListener(const Vec3& position) const;
    Play* FindPlay(uint32_t playingId) const;
    uint32_t NewPlay(uint32_t eventId, uint64_t object);
    void DescribeNode(const Node& node, int depth, std::string& text);

    ALCdevice* device = nullptr;
    ALCcontext* context = nullptr;
    bool loopback = false, ownThread = false;
    int sampleRate = 48000, maxSources = 128;
    float threshold = Silent;
    double now = 0;
    int64_t renderedFrames = 0;
    int64_t clockStart = 0, clockRate = 1;
    std::thread thread;
    bool stopThread = false;

    MediaCache cache;
    std::vector<ALuint> freeSources;
    int sourcesMade = 0;
    std::vector<Voice*> liveVoices;

    std::unordered_map<uint64_t, GameObject> objects;
    std::unordered_map<uint32_t, float> globalParameters;
    std::unordered_map<uint32_t, uint32_t> states;
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
    int64_t refused = 0, endedQuiet = 0, stolen = 0, underruns = 0;
};

Engine& TheEngine();

}  // namespace rfa_audio
