// Reading our sound banks, their media containers and .wem files (see bank.h).
#include "bank.h"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace rfa_audio {

uint32_t WwiseId(const char* name)
{
    uint32_t h = 2166136261u;
    for (const char* c = name; *c; c++) {
        unsigned char b = static_cast<unsigned char>(*c);
        if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b - 'A' + 'a');
        h = (h * 16777619u) ^ b;
    }
    return h;
}

const uint32_t GameDistance = WwiseId("game_distance");

namespace {

std::string Hex(uint32_t v)
{
    char text[16];
    snprintf(text, sizeof text, "%08X", v);
    return text;
}

// Little-endian values from a span of bytes. Reading past its end sets failed and gives zeros.
class Reader {
public:
    Reader(const uint8_t* bytes, size_t size) : p(bytes), end(bytes + size) {}
    bool failed = false;

    uint8_t U8() { uint8_t v = 0; Take(&v, 1); return v; }
    int8_t S8() { return static_cast<int8_t>(U8()); }
    uint16_t U16() { uint16_t v = 0; Take(&v, 2); return v; }
    int16_t S16() { int16_t v = 0; Take(&v, 2); return v; }
    uint32_t U32() { uint32_t v = 0; Take(&v, 4); return v; }
    int32_t S32() { int32_t v = 0; Take(&v, 4); return v; }
    float F32() { float v = 0; Take(&v, 4); return v; }
    void Skip(size_t n)
    {
        if (static_cast<size_t>(end - p) < n) { failed = true; p = end; return; }
        p += n;
    }
    size_t Left() const { return static_cast<size_t>(end - p); }

private:
    void Take(void* into, size_t n)
    {
        if (static_cast<size_t>(end - p) < n) { failed = true; p = end; return; }
        memcpy(into, p, n);
        p += n;
    }
    const uint8_t* p;
    const uint8_t* end;
};

// A curve's points (f32 x, f32 y, u32 shape each).
void ReadPoints(Reader& r, uint32_t count, std::vector<CurvePoint>& points)
{
    points.reserve(count);
    for (uint32_t i = 0; i < count && !r.failed; i++) {
        CurvePoint point;
        point.x = r.F32();
        point.y = r.F32();
        point.shape = r.U32();
        points.push_back(point);
    }
}

// An RTPC curve as a node has it: u32, u8, its parameter, the property, its id, the scaling, the points.
Curve ReadCurve(Reader& r)
{
    Curve curve;
    r.U32();
    r.U8();
    curve.parameter = r.U32();
    curve.property = r.U32();
    curve.id = r.U32();
    curve.scaling = r.U8();
    ReadPoints(r, r.U16(), curve.points);
    return curve;
}

// The settings every sound and container has (WwiseBankWriter.NodeBase; Armageddon's layout).
void ReadNodeBase(Reader& r, Node& node)
{
    r.U8();                       // override the parent's effects
    uint8_t slots = r.U8();
    if (slots) {
        r.U8();                   // bypass flags
        for (uint8_t i = 0; i < slots && !r.failed; i++) {
            r.U8();
            r.U32();              // the plug-in
            r.U8();
            r.Skip(r.U32());      // its parameters
            r.U32();
        }
        node.effectSlots = slots;
    }
    node.bus = r.U32();
    node.parent = r.U32();
    node.priority = r.U8();
    node.ownPriority = r.U8() != 0;
    node.priorityByDistance = r.U8() != 0;
    node.distanceOffset = r.S8();
    float props[12];
    for (float& f : props) f = r.F32();
    node.volume = props[0];
    node.volumeMin = props[1];
    node.volumeMax = props[2];
    node.pitch = props[6];
    node.pitchMin = props[7];
    node.pitchMax = props[8];
    node.lowPass = props[9];
    node.stateGroup = r.U32();
    node.ownPositioning = r.U8() != 0;
    if (node.ownPositioning) {
        r.S32();
        r.F32();
        r.F32();
        node.threeD = r.U8() != 0;
        if (node.threeD) {
            uint32_t positionType = r.U32();
            node.attenuation = r.U32();
            r.U8();               // spatialized
            if (positionType == 3) r.U8();  // game-defined: at the game object
            else {
                // user-defined (paths): play mode, its vertices, its playlist and ranges
                r.U32(); r.U8(); r.U32(); r.U8();
                r.Skip(16ull * r.U32());
                uint32_t playlists = r.U32();
                r.Skip(8ull * playlists);
                r.Skip(8ull * playlists);
            }
        } else {
            r.U8();               // the panner
        }
    }
    node.virtualFrom = r.U8();
    node.refusesWhenFull = r.U8() != 0;
    node.mostAtOnce = r.U16();
    node.whenQuiet = static_cast<WhenQuiet>(std::min<uint8_t>(r.U8(), 2));
    node.ownLimit = r.U8() != 0;
    node.ownQuiet = r.U8() != 0;
    r.U8();
    node.stateEntries = r.U16();
    r.Skip(9ull * node.stateEntries);
    uint16_t curves = r.U16();
    for (uint16_t i = 0; i < curves && !r.failed; i++) node.curves.push_back(ReadCurve(r));
}

void ReadChildren(Reader& r, Node& node)
{
    uint32_t count = r.U32();
    if (count > r.Left() / 4) { r.failed = true; return; }
    for (uint32_t i = 0; i < count; i++) node.children.push_back(r.U32());
}

// One object of the HIRC chunk. False if it isn't read to its exact end (a layout not known).
bool ReadObject(uint32_t type, uint32_t id, const uint8_t* body, size_t size, Bank& bank, std::unique_ptr<Node>& node,
                std::unique_ptr<Action>& action, std::unique_ptr<Event>& event, std::unique_ptr<Bus>& bus)
{
    Reader r(body, size);
    switch (type) {
    case TypeSound: {
        node = std::make_unique<Node>();
        Node& n = *node;
        n.codec = r.U32();
        n.streamType = r.U32();
        n.source = r.U32();
        n.file = r.U32();
        if (n.streamType != 1) { r.U32(); r.U32(); }  // in the bank: its offset and size there
        r.U8();
        if ((n.codec & 0xF) == 2) r.Skip(r.U32());     // a source plug-in's parameters (tone, silence)
        ReadNodeBase(r, n);
        n.loopCount = r.S16();
        r.S16();
        r.S16();
        break;
    }
    case TypeRanSeq: {
        node = std::make_unique<Node>();
        Node& n = *node;
        ReadNodeBase(r, n);
        n.containerLoops = r.S16();
        n.transitionMs = r.F32();
        n.transitionRandomMin = r.F32();
        n.transitionRandomMax = r.F32();
        n.avoidRepeat = r.U16();
        uint8_t flags[8];
        for (uint8_t& f : flags) f = r.U8();
        n.transitionMode = flags[0];
        n.randomMode = flags[1];
        n.sequence = flags[2] != 0;
        n.resetEachPlay = flags[4] != 0;
        n.continuous = flags[6] != 0;
        n.global = flags[7] != 0;
        ReadChildren(r, n);
        uint16_t entries = r.U16();
        for (uint16_t i = 0; i < entries && !r.failed; i++) {
            uint32_t child = r.U32();
            uint8_t weight = r.U8();
            n.playlist.emplace_back(child, weight);
        }
        break;
    }
    case TypeActorMixer:
        node = std::make_unique<Node>();
        ReadNodeBase(r, *node);
        ReadChildren(r, *node);
        break;
    case TypeLayer: {
        node = std::make_unique<Node>();
        Node& n = *node;
        ReadNodeBase(r, n);
        ReadChildren(r, n);
        n.layers = static_cast<int>(r.U32());
        for (int i = 0; i < n.layers && !r.failed; i++) {
            r.U32();
            uint16_t curves = r.U16();
            for (uint16_t k = 0; k < curves && !r.failed; k++) ReadCurve(r);
            r.U32();  // the crossfade parameter
            r.F32();
            uint32_t associated = r.U32();
            for (uint32_t j = 0; j < associated && !r.failed; j++) {
                r.U32();
                r.Skip(12ull * r.U32());
            }
        }
        break;
    }
    case TypeSwitch: {
        node = std::make_unique<Node>();
        Node& n = *node;
        ReadNodeBase(r, n);
        n.onState = r.U32() != 0;
        n.group = r.U32();
        n.defaultValue = r.U32();
        n.followsChanges = r.U8() != 0;
        ReadChildren(r, n);
        uint32_t values = r.U32();
        for (uint32_t i = 0; i < values && !r.failed; i++) {
            uint32_t value = r.U32();
            uint32_t count = r.U32();
            if (count > r.Left() / 4) { r.failed = true; break; }
            std::vector<uint32_t> children(count);
            for (uint32_t& c : children) c = r.U32();
            n.choices.emplace_back(value, std::move(children));
        }
        uint32_t params = r.U32();
        for (uint32_t i = 0; i < params && !r.failed; i++) {
            Node::SwitchChild s{};
            s.child = r.U32();
            s.firstOnly = r.U8();
            s.continuePlayback = r.U8();
            s.onSwitch = r.U32();
            s.fadeOutMs = r.S32();
            s.fadeInMs = r.S32();
            n.switchChildren.push_back(s);
        }
        break;
    }
    case TypeAction: {
        action = std::make_unique<Action>();
        Action& a = *action;
        a.type = r.U32();
        a.target = r.U32();
        if (a.Kind() == ActionBreak) {
            r.Skip(16);
        } else {
            // The head as WwiseBankWriter's ActionHead and Armageddon's: delay (and its random range), 33, the fade
            // (and its range), its curve, ...
            a.delayMs = r.S32();
            r.S32();
            r.S32();
            r.U32();
            a.fadeMs = r.S32();
            r.S32();
            r.S32();
            // The rest depends on the kind (a play's bank id); not needed: taken as read.
            if (a.Kind() == ActionPlay && r.Left() >= 4) {
                r.Skip(r.Left() - 4);
                a.bankId = r.U32();
            } else {
                r.Skip(r.Left());
            }
        }
        break;
    }
    case TypeEvent: {
        event = std::make_unique<Event>();
        uint32_t count = r.U32();
        if (count > r.Left() / 4) { r.failed = true; break; }
        for (uint32_t i = 0; i < count; i++) event->actions.push_back(r.U32());
        break;
    }
    case TypeBus: {
        bus = std::make_unique<Bus>();
        Bus& b = *bus;
        b.parent = r.U32();
        b.volume = r.F32();
        r.F32(); r.F32(); r.F32();
        b.killNewest = r.U8() != 0;
        b.mostAtOnce = r.U16();
        r.Skip(r.Left());  // the rest (states, ducking, ...) isn't needed yet
        break;
    }
    case TypeAttenuation:
        // Ours are flat: the fade is a curve on game_distance on the nodes (WwiseBankWriter.Attenuation).
        bank.attenuations.push_back(id);
        r.Skip(r.Left());
        break;
    default:
        bank.unknownObjects++;
        r.Skip(r.Left());
        break;
    }
    return !r.failed && r.Left() == 0;
}

}  // namespace

const Node::SwitchChild* Node::SwitchParams(uint32_t child) const
{
    for (const SwitchChild& s : switchChildren)
        if (s.child == child) return &s;
    return nullptr;
}

float Curve::ValueAt(float x) const
{
    if (points.empty()) return 0.0f;
    if (x <= points.front().x) return points.front().y;
    if (x >= points.back().x) return points.back().y;
    for (size_t i = 0; i + 1 < points.size(); i++) {
        const CurvePoint& a = points[i];
        const CurvePoint& b = points[i + 1];
        if (x > b.x) continue;
        if (a.shape == 9 || b.x <= a.x) return a.y;  // constant
        float t = (x - a.x) / (b.x - a.x);
        return a.y + (b.y - a.y) * t;
    }
    return points.back().y;
}

float Curve::GainAt(float x) const
{
    float y = ValueAt(x);
    if (scaling == 2) {
        if (y <= Silent) return 0.0f;
        return y <= 0 ? 1.0f + y / -Silent : 1.0f / (1.0f - y / -Silent);
    }
    return std::pow(10.0f, y / 20.0f);  // plain dB
}

bool ReadWemFormat(const uint8_t* bytes, size_t size, WemFormat& f, std::string& why)
{
    if (size < 12 || memcmp(bytes, "RIFF", 4) != 0 || memcmp(bytes + 8, "WAVE", 4) != 0) { why = "not a RIFF/WAVE file"; return false; }
    size_t at = 12;
    bool haveFormat = false;
    while (at + 8 <= size) {
        const uint8_t* chunk = bytes + at;
        uint32_t length;
        memcpy(&length, chunk + 4, 4);
        const uint8_t* body = chunk + 8;
        size_t available = size - at - 8;
        if (memcmp(chunk, "fmt ", 4) == 0 && length >= 16 && available >= 16) {
            memcpy(&f.tag, body, 2);
            uint16_t channels, align, bits;
            uint32_t rate;
            memcpy(&channels, body + 2, 2);
            memcpy(&rate, body + 4, 4);
            memcpy(&align, body + 12, 2);
            memcpy(&bits, body + 14, 2);
            f.channels = channels;
            f.rate = static_cast<int>(rate);
            f.blockAlign = align;
            f.bits = bits;
            haveFormat = true;
        } else if (memcmp(chunk, "smpl", 4) == 0 && length >= 36 + 24 && available >= 36 + 24) {
            uint32_t loops;
            memcpy(&loops, body + 28, 4);
            if (loops > 0) {
                uint32_t start, end;
                memcpy(&start, body + 36 + 8, 4);
                memcpy(&end, body + 36 + 12, 4);
                f.hasLoop = true;
                f.loopStart = start;
                f.loopEnd = end;
            }
        } else if (memcmp(chunk, "data", 4) == 0) {
            f.dataOffset = static_cast<uint32_t>(at + 8);
            f.dataBytes = length;
            break;
        }
        at += 8 + length + (length & 1);
    }
    if (!haveFormat) { why = "no fmt chunk"; return false; }
    if (f.dataOffset == 0) { why = "no data chunk in the first part read"; return false; }
    if (f.channels < 1 || f.channels > 8 || f.rate < 1000 || f.rate > 192000) { why = "an unusual format (channels or rate)"; return false; }
    if (f.tag == 2) {
        if (f.blockAlign != 36 * f.channels) { why = "Wwise ADPCM with blocks of " + std::to_string(f.blockAlign) + " bytes"; return false; }
        f.frames = static_cast<int64_t>(f.dataBytes / f.blockAlign) * 64;
    } else if (f.tag == 1 || f.tag == 0xFFFE) {
        if (f.bits != 16) { why = std::to_string(f.bits) + "-bit PCM (16 read)"; return false; }
        f.frames = f.dataBytes / (2u * f.channels);
    } else {
        why = "codec " + std::to_string(f.tag) + " (PCM and Wwise ADPCM read)";
        return false;
    }
    if (f.hasLoop) {
        if (f.loopEnd >= f.frames) f.loopEnd = f.frames - 1;
        if (f.loopStart > f.loopEnd) f.hasLoop = false;
    }
    return true;
}

MediaFile::~MediaFile()
{
    if (handle) CloseHandle(handle);
}

bool MediaFile::Open(const std::wstring& path, std::string& why)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        why = "can't open it (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    handle = h;
    return true;
}

bool MediaFile::ReadAt(uint64_t offset, void* into, uint32_t size) const
{
    if (!handle) return false;
    OVERLAPPED at{};
    at.Offset = static_cast<DWORD>(offset);
    at.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD read = 0;
    return ReadFile(handle, into, size, &read, &at) && read == size;
}

namespace {

bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& bytes, std::string& why)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        why = "can't open it (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    if (size.QuadPart > (1ll << 30)) {
        CloseHandle(h);
        why = "too big for a bank";
        return false;
    }
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    bool ok = bytes.empty() || (ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size());
    CloseHandle(h);
    if (!ok) why = "can't read it";
    return ok;
}

std::string Narrow(const std::wstring& text)
{
    std::string out;
    for (wchar_t c : text) out += c < 128 ? static_cast<char>(c) : '?';
    return out;
}

}  // namespace

Bank* Library::Load(const std::wstring& bankPath, const std::wstring& mediaPath, std::string& why)
{
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(bankPath, bytes, why)) return nullptr;
    auto bank = std::make_unique<Bank>();
    bank->path = bankPath;
    bank->mediaPath = mediaPath;
    size_t slash = bankPath.find_last_of(L"\\/");
    bank->name = Narrow(slash == std::wstring::npos ? bankPath : bankPath.substr(slash + 1));

    // Its objects, read before any is kept: a bank that doesn't read changes nothing.
    std::vector<std::unique_ptr<Node>> newNodes;
    std::vector<std::unique_ptr<Action>> newActions;
    std::vector<std::unique_ptr<Event>> newEvents;
    std::vector<std::unique_ptr<Bus>> newBuses;
    bool sawHeader = false;
    size_t at = 0;
    while (at + 8 <= bytes.size()) {
        const uint8_t* chunk = bytes.data() + at;
        uint32_t length;
        memcpy(&length, chunk + 4, 4);
        if (length > bytes.size() - at - 8) { why = "a chunk runs past the file's end"; return nullptr; }
        const uint8_t* body = chunk + 8;
        if (memcmp(chunk, "BKHD", 4) == 0 && length >= 8) {
            uint32_t version;
            memcpy(&version, body, 4);
            memcpy(&bank->id, body + 4, 4);
            if (version != 48) { why = "bank version " + std::to_string(version) + " (48 read)"; return nullptr; }
            sawHeader = true;
        } else if (memcmp(chunk, "HIRC", 4) == 0 && length >= 4) {
            uint32_t count;
            memcpy(&count, body, 4);
            size_t p = 4;
            for (uint32_t i = 0; i < count; i++) {
                if (p + 12 > length) { why = "its object list runs past its chunk"; return nullptr; }
                uint32_t type, size, id;
                memcpy(&type, body + p, 4);
                memcpy(&size, body + p + 4, 4);
                memcpy(&id, body + p + 8, 4);
                if (size < 4 || p + 8 + size > length) { why = "object " + Hex(id) + " runs past its chunk"; return nullptr; }
                std::unique_ptr<Node> node;
                std::unique_ptr<Action> action;
                std::unique_ptr<Event> event;
                std::unique_ptr<Bus> bus;
                if (!ReadObject(type, id, body + p + 12, size - 4, *bank, node, action, event, bus))
                    bank->problems.push_back("object " + Hex(id) + " (type " + std::to_string(type) + ") isn't laid out as read; left out");
                else if (node) { node->id = id; node->type = type; node->bankId = bank->id; newNodes.push_back(std::move(node)); }
                else if (action) { action->id = id; action->bankId = action->bankId ? action->bankId : bank->id; newActions.push_back(std::move(action)); }
                else if (event) { event->id = id; event->bankId = bank->id; newEvents.push_back(std::move(event)); }
                else if (bus) { bus->id = id; bus->bankId = bank->id; newBuses.push_back(std::move(bus)); }
                p += 8 + size;
            }
        }
        at += 8 + length;
    }
    if (!sawHeader) { why = "no bank header (BKHD)"; return nullptr; }
    if (bank->unknownObjects) bank->problems.push_back(std::to_string(bank->unknownObjects) + " objects of kinds not read (music, dialogue, states, ...)");

    // Its media file: the VWSB index ("VWSBPC  ", 0, 2, 1, the bank's id, the header's size + 16, a count, then
    // (id, offset, 0, size) a medium).
    if (!mediaPath.empty() && GetFileAttributesW(mediaPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        auto file = std::make_unique<MediaFile>();
        std::string problem;
        if (!file->Open(mediaPath, problem)) {
            bank->problems.push_back("its media file: " + problem);
        } else {
            uint8_t head[28];
            if (!file->ReadAt(0, head, sizeof head) || memcmp(head, "VWSBPC  ", 8) != 0) {
                bank->problems.push_back("its media file isn't a VWSB container");
            } else {
                uint32_t count;
                memcpy(&count, head + 24, 4);
                std::vector<uint8_t> index(16ull * count);
                if (count > 1000000 || (count && !file->ReadAt(28, index.data(), static_cast<uint32_t>(index.size())))) {
                    bank->problems.push_back("its media file's index can't be read");
                } else {
                    for (uint32_t i = 0; i < count; i++) {
                        MediumEntry m;
                        memcpy(&m.id, &index[16 * i], 4);
                        memcpy(&m.offset, &index[16 * i + 4], 4);
                        memcpy(&m.size, &index[16 * i + 12], 4);
                        bank->media[m.id] = m;
                    }
                    bank->mediaFile = std::move(file);
                }
            }
        }
    } else if (!mediaPath.empty()) {
        bank->problems.push_back("no media file beside it");
    }

    // A bank loaded again replaces the one before.
    Unload(bank->id);
    for (auto& n : newNodes) { bank->nodes.push_back(n->id); nodes[n->id] = std::move(n); }
    for (auto& a : newActions) { bank->actions.push_back(a->id); actions[a->id] = std::move(a); }
    for (auto& e : newEvents) { bank->events.push_back(e->id); events[e->id] = std::move(e); }
    for (auto& b : newBuses) { bank->buses.push_back(b->id); buses[b->id] = std::move(b); }
    Bank* kept = bank.get();
    banks[bank->id] = std::move(bank);
    CheckLinks(*kept);
    return kept;
}

// What the bank refers to that isn't loaded: children, parents, targets, media.
void Library::CheckLinks(Bank& bank)
{
    int missingMedia = 0, inBank = 0;
    std::string firstMissing;
    for (uint32_t id : bank.nodes) {
        const Node& n = *nodes[id];
        for (uint32_t child : n.children)
            if (!FindNode(child)) bank.problems.push_back("node " + Hex(id) + "'s child " + Hex(child) + " isn't loaded");
        if (n.parent && !FindNode(n.parent)) bank.problems.push_back("node " + Hex(id) + "'s parent " + Hex(n.parent) + " isn't loaded");
        if (n.IsSound()) {
            if (n.streamType != 1) inBank++;
            else if (!bank.media.count(n.source)) {
                if (missingMedia++ == 0) firstMissing = Hex(n.source);
            }
        }
    }
    if (missingMedia) bank.problems.push_back(std::to_string(missingMedia) + " sounds' media aren't in the media file (the first " + firstMissing + ")");
    if (inBank) bank.problems.push_back(std::to_string(inBank) + " sounds keep their media in the bank (not read)");
    for (uint32_t id : bank.actions) {
        const Action& a = *actions[id];
        if (a.target && !FindNode(a.target) && a.Scope() == ScopeGameObject)
            bank.problems.push_back("action " + Hex(id) + "'s target " + Hex(a.target) + " isn't loaded");
    }
    for (uint32_t id : bank.events)
        for (uint32_t a : events[id]->actions)
            if (!FindAction(a)) bank.problems.push_back("event " + Hex(id) + "'s action " + Hex(a) + " isn't loaded");
}

void Library::Unload(uint32_t bankId)
{
    auto it = banks.find(bankId);
    if (it == banks.end()) return;
    Bank& bank = *it->second;
    for (uint32_t id : bank.nodes) nodes.erase(id);
    for (uint32_t id : bank.actions) actions.erase(id);
    for (uint32_t id : bank.events) events.erase(id);
    for (uint32_t id : bank.buses) buses.erase(id);
    banks.erase(it);
}

void Library::Clear()
{
    banks.clear();
    nodes.clear();
    actions.clear();
    events.clear();
    buses.clear();
}

const Node* Library::FindNode(uint32_t id) const
{
    auto it = nodes.find(id);
    return it == nodes.end() ? nullptr : it->second.get();
}

const Action* Library::FindAction(uint32_t id) const
{
    auto it = actions.find(id);
    return it == actions.end() ? nullptr : it->second.get();
}

const Event* Library::FindEvent(uint32_t id) const
{
    auto it = events.find(id);
    return it == events.end() ? nullptr : it->second.get();
}

const Bus* Library::FindBus(uint32_t id) const
{
    auto it = buses.find(id);
    return it == buses.end() ? nullptr : it->second.get();
}

Bank* Library::FindBank(uint32_t id) const
{
    auto it = banks.find(id);
    return it == banks.end() ? nullptr : it->second.get();
}

float Library::BusVolume(uint32_t busId) const
{
    float total = 0;
    for (int depth = 0; busId && depth < 32; depth++) {
        const Bus* bus = FindBus(busId);
        if (!bus) break;
        total += bus->volume;
        busId = bus->parent;
    }
    return total;
}

}  // namespace rfa_audio
