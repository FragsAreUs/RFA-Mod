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
    TypeState = 1, TypeSound = 2, TypeAction = 3, TypeEvent = 4, TypeRanSeq = 5, TypeSwitch = 6, TypeActorMixer = 7, TypeBus = 8,
    TypeLayer = 9, TypeAttenuation = 14,
};

// What a curve changes.
enum Property : uint32_t { PropertyVolume = 0, PropertyPitch = 2, PropertyLowPass = 3 };

// The action kinds (an action's type >> 12) and scopes (its low 12 bits).
enum ActionKind : uint32_t {
    ActionStop = 1, ActionPause = 2, ActionResume = 3, ActionPlay = 4, ActionMute = 6, ActionUnmute = 7,
    ActionSetPitch = 8, ActionResetPitch = 9, ActionSetVolume = 0xA, ActionResetVolume = 0xB, ActionSetLowPass = 0xE,
    ActionResetLowPass = 0xF, ActionSetState = 0x12, ActionSetSwitch = 0x60, ActionBreak = 0x90, ActionPostEvent = 0xA0,
};
// Scopes: on the event's game object, everywhere, on all (0x020), on all but the target (0x021), and a set switch's.
constexpr uint32_t ScopeAll = 0x020, ScopeAllBut = 0x021;
constexpr uint32_t ScopeGameObject = 0x011, ScopeEverywhere = 0x010;

struct CurvePoint {
    float x, y;
    uint32_t shape;  // the shape to the next point (Wwise's: 4 linear, 9 constant, the others CurveShape's)
};

// Wwise's curve shape `shape` from a segment's start (t 0) to its end (t 1): 0 to 1, as the curve's y moves.
float CurveShape(uint32_t shape, float t);

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

// An attenuation (object type 14): curves on a 3D sound's distance from the listener (x in metres), and a cone.
// Ours are flat (0 dB at any distance: our fades are curves on game_distance); Armageddon's fade by them.
struct Attenuation {
    uint32_t id = 0, bankId = 0;
    bool cone = false;
    float coneInner = 0, coneOuter = 0, coneOuterDb = 0, coneLowPass = 0;
    int volumeDry = -1, lowPass = -1, spread = -1;  // its curves for each (-1: none)
    std::vector<Curve> curves;                      // volume (dB as Wwise stores it), low-pass (0-100), spread (%)
    float maxDistance = 0;                          // where its volume curve ends
};

// What a state of a state group does (init.bnk's STMG): to volume and LFE (dB), pitch (cents) and low-pass (0-100).
struct StateValues {
    float volume = 0, lfe = 0, pitch = 0, lowPass = 0;
};
struct StateGroup {
    uint32_t id = 0;
    int defaultTransitionMs = 0;
    std::unordered_map<uint32_t, StateValues> states;
    struct Transition {
        uint32_t from, to;
        int ms;
    };
    std::vector<Transition> transitions;
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
    // A source plug-in sound: Wwise's silence (0x00650002) lasts silenceSeconds (and up to its random range more or
    // less); the others (tone, synthesis) aren't played.
    float silenceSeconds = 0, silenceLess = 0, silenceMore = 0;
    bool IsSourcePlugin() const { return (codec & 0xF) == 2; }
    bool IsSilence() const { return codec == 0x00650002u; }

    bool IsSound() const { return type == TypeSound; }
    bool IsAdpcm() const { return (codec & 0xFFFF0000u) == 0x00020000u; }
    const SwitchChild* SwitchParams(uint32_t child) const;
};

// An action (object type 3): its type (kind << 12 | scope), its target, a delay, then parameters of a size given:
// most kinds a fade (and its random range) and the fade's curve; the "set" kinds a value too; a state's or a
// switch's the group and its value; a play its bank's id after them.
struct Action {
    uint32_t id = 0, type = 0, target = 0, bankId = 0;
    int32_t delayMs = 0, fadeMs = 0;
    uint8_t fadeCurve = 4;
    float value = 0;                  // set volume (dB), set pitch (cents), set low-pass (0-100)
    uint32_t group = 0, groupValue = 0;  // set state, set switch
    uint32_t Kind() const { return type >> 12; }
    uint32_t Scope() const { return type & 0xFFF; }
};

struct Event {
    uint32_t id = 0, bankId = 0;
    std::vector<uint32_t> actions;
};

// A bus (object type 8, init.bnk's), laid out as Armageddon's: its parent; volume, LFE, pitch and low-pass; its voice
// limit; the state group it follows (and its states, each Wwise's own values or a state object's); its ducking of other
// buses while something plays through it (back up after its recovery time, never more than its maximum duck); its
// effects (read, not played); its curves on game parameters (the volume sliders are these).
struct Bus {
    uint32_t id = 0, parent = 0, bankId = 0;
    float volume = 0, lfe = 0, pitch = 0, lowPass = 0;
    bool killNewest = false;
    uint16_t mostAtOnce = 0;
    uint32_t stateGroup = 0;
    int recoveryMs = 0;
    float maxDuckDb = Silent;
    struct Duck {
        uint32_t bus;
        float db;
        int fadeOutMs, fadeInMs;
        uint8_t curve;
    };
    std::vector<Duck> ducks;
    int effectSlots = 0;
    std::vector<Curve> curves;
    std::unordered_map<uint32_t, uint32_t> stateObjects;  // a state of its group → the state object it uses (0: the group's)
};

// A medium's place in its bank's media file.
struct MediumEntry {
    uint32_t id = 0, offset = 0, size = 0;
};

// A .wem's format, read from its RIFF chunks.
struct WemFormat {
    uint16_t tag = 0;          // 0xFFFE or 1: PCM; 2: Wwise ADPCM; 0xFFFF: Wwise Vorbis
    int channels = 0, rate = 0, blockAlign = 0, bits = 0;
    uint32_t dataOffset = 0, dataBytes = 0;  // within the medium (past a voice's lip-sync block, if any)
    int64_t frames = 0;
    bool hasLoop = false;
    int64_t loopStart = 0, loopEnd = 0;      // the loop's frames, its end included
    // Wwise Vorbis (its vorb chunk): the samples, the setup packet's and the first audio packet's places in the
    // data, and the two block sizes as powers of 2 (the smaller first).
    bool hasVorb = false;
    uint32_t vorbisSamples = 0, vorbisSetup = 0, vorbisAudio = 0;
    uint8_t vorbisBlocks[2] = {};
    uint32_t avgBytesPerSecond = 0;
    // A voice line of Armageddon's starts with a lip-sync block ("DMAV"): its size (the .wem starts after it).
    uint32_t lipSyncBytes = 0;
    bool IsAdpcm() const { return tag == 2; }
    bool IsVorbis() const { return tag == 0xFFFF; }
};

// Reads a .wem's chunks up to its data. False with why.
bool ReadWemFormat(const uint8_t* bytes, size_t size, WemFormat& format, std::string& why);

// A media file, read at offsets (opened for reading only, shared).
class MediaFile {
public:
    ~MediaFile();
    bool Open(const std::wstring& path, std::string& why);
    // A part of a file as the media file: [base, base + size) of an archive that holds it uncompressed.
    bool OpenPart(const std::wstring& path, uint64_t base, uint64_t size, std::string& why);
    bool ReadAt(uint64_t offset, void* into, uint32_t size) const;
    bool IsOpen() const { return handle != nullptr; }
private:
    void* handle = nullptr;
    uint64_t base = 0, limit = ~0ull;
};

// A file in one of the game's archives (.vpp_pc, version 6): where its bytes are, and whether they're compressed.
struct ArchiveFile {
    uint64_t offset = 0, size = 0;
    bool compressed = false;
};
// Every file's name (lower case) in an archive's directory. False with why (not an archive of version 6).
bool ListArchive(const std::wstring& archive, std::vector<std::string>& names, std::string& why);
// Finds files by name (lower case) in an archive's directory. False with why (not an archive of version 6).
bool FindInArchive(const std::wstring& archive, const std::vector<std::string>& names, std::vector<ArchiveFile>& found,
                   std::vector<bool>& present, std::string& why);

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
    std::vector<uint32_t> stateObjects;
    std::vector<std::pair<uint32_t, uint32_t>> unread;  // objects of kinds not read (id, type): music, dialogue
    bool armageddons = false;  // read from one of the game's archives: Armageddon's own (its buses' voice limits apply)
};

// Every loaded bank's objects, found by id (as Wwise finds them, whichever bank they're in).
class Library {
public:
    // Reads a bank file and its media file. Returns the bank (kept here) or null with why.
    Bank* Load(const std::wstring& bankPath, const std::wstring& mediaPath, std::string& why);
    // A bank of Armageddon's own and its media, read in place from an archive that holds them uncompressed
    // (sounds.vpp_pc, voices.vpp_pc): `name` without its .bnk_pc. Null with why. A loose file given for the bank or
    // its media (an override of the game's copy) is read instead of the archive's.
    Bank* LoadFromArchive(const std::wstring& archive, const std::string& name, std::string& why,
                          const std::wstring& looseBank = L"", const std::wstring& looseMedia = L"");
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
    const Attenuation* FindAttenuation(uint32_t id) const;
    // The global settings (init.bnk's STMG, when it is loaded): the game parameters' defaults (0 if not given), the
    // state groups.
    float ParameterDefault(uint32_t parameter) const;
    const StateGroup* FindStateGroup(uint32_t id) const;
    // A state object (type 1): the values a node or bus uses for one state instead of the group's.
    const StateValues* FindStateObject(uint32_t id) const;
    // An object of a kind not read (music, dialogue) in a loaded bank: its type; 0 if none is.
    uint32_t UnreadType(uint32_t id) const;
    const std::unordered_map<uint32_t, std::unique_ptr<Bus>>& AllBuses() const { return buses; }
    const std::unordered_map<uint32_t, StateGroup>& StateGroups() const { return stateGroups; }
    const std::unordered_map<uint32_t, float>& ParameterDefaults() const { return parameterDefaults; }
    float VolumeThreshold() const { return volumeThreshold; }
private:
    void CheckLinks(Bank& bank);
    // A bank's bytes read, with its media file opened (or null, with why in mediaProblem).
    Bank* Parse(const std::vector<uint8_t>& bytes, const std::wstring& bankPath, const std::wstring& mediaPath,
                std::unique_ptr<MediaFile> media, const std::string& mediaProblem, std::string& why);
    std::map<uint32_t, std::unique_ptr<Bank>> banks;
    std::unordered_map<uint32_t, std::unique_ptr<Node>> nodes;
    std::unordered_map<uint32_t, std::unique_ptr<Action>> actions;
    std::unordered_map<uint32_t, std::unique_ptr<Event>> events;
    std::unordered_map<uint32_t, std::unique_ptr<Bus>> buses;
    std::unordered_map<uint32_t, std::unique_ptr<Attenuation>> attenuations;
    std::unordered_map<uint32_t, StateValues> stateObjects;
    std::unordered_map<uint32_t, uint32_t> unreadObjects;
    std::unordered_map<uint32_t, float> parameterDefaults;
    std::unordered_map<uint32_t, StateGroup> stateGroups;
    float volumeThreshold = 0;  // 0 until an STMG is read
};

// Wwise's id of a name: FNV-1 of its lower-case letters.
uint32_t WwiseId(const char* name);

// The game parameter Armageddon sets on positioned objects (their distance from the listener).
extern const uint32_t GameDistance;

}  // namespace rfa_audio
