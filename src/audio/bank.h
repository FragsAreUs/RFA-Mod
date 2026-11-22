// Our sound banks as GuerrillaBuilder's WwiseBankWriter writes them (Wwise bank version 48, laid out as
// Armageddon's own), their media containers (Volition's VWSB) and the .wem files in them.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rfa_audio {

// Wwise's silence, in dB: its volume curves fall to it, and the volume threshold is it by default.
constexpr float Silent = -96.3f;

// The object kinds of a bank's HIRC chunk.
enum ObjectType : uint32_t {
    TypeSound = 2, TypeAction = 3, TypeEvent = 4, TypeRanSeq = 5, TypeSwitch = 6, TypeActorMixer = 7, TypeBus = 8,
    TypeLayer = 9, TypeAttenuation = 14,
};

// What a curve changes.
enum Property : uint32_t { PropertyVolume = 0, PropertyPitch = 2, PropertyLowPass = 3 };

// The action kinds (an action's type >> 12) and scopes (its low 12 bits).
enum ActionKind : uint32_t { ActionStop = 1, ActionPause = 2, ActionResume = 3, ActionPlay = 4, ActionMute = 6, ActionUnmute = 7, ActionBreak = 0x90 };
constexpr uint32_t ScopeGameObject = 0x011, ScopeEverywhere = 0x010;

struct CurvePoint {
    float x, y;
    uint32_t shape;  // the shape to the next point: 4 linear, 9 constant (others are read as linear)
};

// A curve: a game parameter's value (x) to a property (y), between points.
struct Curve {
    uint32_t parameter = 0, property = 0, id = 0;
    uint8_t scaling = 0;  // 2: a volume curve's values as Wwise stores them (96.3 × (gain − 1)); 0: as they are
    std::vector<CurvePoint> points;

    // y at x: held flat beyond the first and last points.
    float ValueAt(float x) const;
    // For a volume curve: its gain at x (Wwise interpolates the stored values in straight lines, then turns them
    // into dB: 20·log10(1 + y / 96.3) below 0, −20·log10(1 − y / 96.3) above; so in gain the line is straight).
    float GainAt(float x) const;
    float LastX() const { return points.empty() ? 0.0f : points.back().x; }
};

// When a voice is too quiet to hear.
enum class WhenQuiet : uint8_t { PlayOn = 0, End = 1, Virtual = 2 };

// A sound, container or actor-mixer: the node settings every one has, then its own.
struct Node {
    uint32_t id = 0, type = 0, bankId = 0;
    uint32_t bus = 0, parent = 0;
    uint8_t priority = 50;
    bool ownPriority = false, priorityByDistance = false;
    int8_t distanceOffset = 0;
    // Wwise's twelve fixed properties: volume and its random range, LFE and its, pitch and its, low-pass and its.
    float volume = 0, volumeMin = 0, volumeMax = 0, pitch = 0, pitchMin = 0, pitchMax = 0, lowPass = 0;
    uint32_t stateGroup = 0;
    bool ownPositioning = false, threeD = false;
    uint32_t attenuation = 0;
    uint8_t virtualFrom = 0;         // a virtual voice starts again: 0 from its beginning
    bool refusesWhenFull = false;    // with mostAtOnce playing, a new one doesn't start (else the oldest ends)
    uint16_t mostAtOnce = 0;         // 0: no limit
    WhenQuiet whenQuiet = WhenQuiet::PlayOn;
    bool ownLimit = false, ownQuiet = false;  // these settings are its own, not its parent's
    int effectSlots = 0, stateEntries = 0;
    std::vector<Curve> curves;
    std::vector<uint32_t> children;

    // A sound
    uint32_t codec = 0;       // the plug-in: 0x00010001 PCM, 0x00020001 Wwise ADPCM
    uint32_t streamType = 0;  // 1: streamed from the media file
    uint32_t source = 0, file = 0;
    int16_t loopCount = 1;    // 0: for ever

    // A random or sequence container
    int16_t containerLoops = 1;  // 0: for ever (continuous only)
    float transitionMs = 0, transitionRandomMin = 0, transitionRandomMax = 0;
    uint16_t avoidRepeat = 0;
    uint8_t transitionMode = 0;  // 0 none, 5 trigger rate (a child every transitionMs, overlapping), others: one after another
    uint8_t randomMode = 0;      // 0 normal, 1 shuffle
    bool sequence = false, resetEachPlay = false, continuous = false, global = false;
    std::vector<std::pair<uint32_t, uint8_t>> playlist;  // child, weight

    // A switch container
    bool onState = false;
    uint32_t group = 0, defaultValue = 0;
    bool followsChanges = false;
    std::vector<std::pair<uint32_t, std::vector<uint32_t>>> choices;  // value, its children
    struct SwitchChild {
        uint32_t child;
        uint8_t firstOnly, continuePlayback;
        uint32_t onSwitch;  // 1: a change of the switch stops it
        int32_t fadeOutMs, fadeInMs;
    };
    std::vector<SwitchChild> switchChildren;

    // A layer container
    int layers = 0;

    bool IsSound() const { return type == TypeSound; }
    bool IsAdpcm() const { return (codec & 0xFFFF0000u) == 0x00020000u; }
    const SwitchChild* SwitchParams(uint32_t child) const;
};

struct Action {
    uint32_t id = 0, type = 0, target = 0, bankId = 0;
    int32_t delayMs = 0, fadeMs = 0;
    uint32_t Kind() const { return type >> 12; }
    uint32_t Scope() const { return type & 0xFFF; }
};

struct Event {
    uint32_t id = 0, bankId = 0;
    std::vector<uint32_t> actions;
};

struct Bus {
    uint32_t id = 0, parent = 0, bankId = 0;
    float volume = 0;
    bool killNewest = false;
    uint16_t mostAtOnce = 0;
};

// A medium's place in its bank's media file.
struct MediumEntry {
    uint32_t id = 0, offset = 0, size = 0;
};

// A .wem's format, read from its RIFF chunks.
struct WemFormat {
    uint16_t tag = 0;          // 0xFFFE or 1: PCM; 2: Wwise ADPCM
    int channels = 0, rate = 0, blockAlign = 0, bits = 0;
    uint32_t dataOffset = 0, dataBytes = 0;  // within the .wem
    int64_t frames = 0;
    bool hasLoop = false;
    int64_t loopStart = 0, loopEnd = 0;      // the loop's frames, its end included
    bool IsAdpcm() const { return tag == 2; }
};

// Reads a .wem's chunks up to its data. False with why.
bool ReadWemFormat(const uint8_t* bytes, size_t size, WemFormat& format, std::string& why);

// A media file, read at offsets (opened for reading only, shared).
class MediaFile {
public:
    ~MediaFile();
    bool Open(const std::wstring& path, std::string& why);
    bool ReadAt(uint64_t offset, void* into, uint32_t size) const;
    bool IsOpen() const { return handle != nullptr; }
private:
    void* handle = nullptr;
};

// A loaded bank: its objects (owned by the library), the media index and file.
struct Bank {
    uint32_t id = 0;
    std::string name;
    std::wstring path, mediaPath;
    std::vector<uint32_t> nodes, actions, events, buses, attenuations;
    std::unordered_map<uint32_t, MediumEntry> media;
    std::unique_ptr<MediaFile> mediaFile;
    std::vector<std::string> problems;
    int unknownObjects = 0;
};

// Every loaded bank's objects, found by id (as Wwise finds them, whichever bank they're in).
class Library {
public:
    // Reads a bank file and its media file. Returns the bank (kept here) or null with why.
    Bank* Load(const std::wstring& bankPath, const std::wstring& mediaPath, std::string& why);
    void Unload(uint32_t bankId);
    void Clear();

    const Node* FindNode(uint32_t id) const;
    const Action* FindAction(uint32_t id) const;
    const Event* FindEvent(uint32_t id) const;
    const Bus* FindBus(uint32_t id) const;
    Bank* FindBank(uint32_t id) const;
    const std::map<uint32_t, std::unique_ptr<Bank>>& Banks() const { return banks; }

    // A bus's volume with its parents' (dB).
    float BusVolume(uint32_t busId) const;
    // Where the attenuation's (or the node's game_distance curve's) fade ends.
private:
    void CheckLinks(Bank& bank);
    std::map<uint32_t, std::unique_ptr<Bank>> banks;
    std::unordered_map<uint32_t, std::unique_ptr<Node>> nodes;
    std::unordered_map<uint32_t, std::unique_ptr<Action>> actions;
    std::unordered_map<uint32_t, std::unique_ptr<Event>> events;
    std::unordered_map<uint32_t, std::unique_ptr<Bus>> buses;
};

// Wwise's id of a name: FNV-1 of its lower-case letters.
uint32_t WwiseId(const char* name);

// The game parameter Armageddon sets on positioned objects (their distance from the listener).
extern const uint32_t GameDistance;

}  // namespace rfa_audio
