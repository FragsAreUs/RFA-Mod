// Reading our sound banks, their media containers and .wem files (see bank.h).
#include "bank.h"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

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
            node.positionType = r.U32();
            node.attenuation = r.U32();
            r.U8();               // spatialized
            if (node.positionType == 3) r.U8();  // game-defined: at the game object
            else {
                // user-defined (paths): the play mode, whether they loop, the transition time, a byte, the points
                // (x, y, z, the time to the next), the paths (a first point and a count) and their random ranges
                node.pathMode = r.U32();
                node.pathLoops = r.U8() != 0;
                node.pathTransitionMs = r.S32();
                r.U8();
                uint32_t points = r.U32();
                if (points > r.Left() / 16) {
                    r.failed = true;
                    return;
                }
                for (uint32_t i = 0; i < points; i++) {
                    Node::PathPoint p;
                    p.x = r.F32();
                    p.y = r.F32();
                    p.z = r.F32();
                    p.ms = r.S32();
                    node.pathPoints.push_back(p);
                }
                uint32_t paths = r.U32();
                if (paths > r.Left() / 16) {
                    r.failed = true;
                    return;
                }
                node.paths.resize(paths);
                for (auto& p : node.paths) {
                    p.first = r.U32();
                    p.count = r.U32();
                }
                for (auto& p : node.paths) {
                    p.rangeX = r.F32();
                    p.rangeZ = r.F32();
                }
                // A path past the points read is dropped.
                node.paths.erase(std::remove_if(node.paths.begin(), node.paths.end(),
                                                [&](const Node::Path& p) {
                                                    return p.count == 0 || p.first >= node.pathPoints.size() ||
                                                           p.count > node.pathPoints.size() - p.first;
                                                }),
                                 node.paths.end());
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
    // Its states, as a bus's: each the state, a flag, the state object it uses (with 0, the group's own values).
    node.stateEntries = r.U16();
    for (int i = 0; i < node.stateEntries && !r.failed; i++) {
        uint32_t state = r.U32();
        uint8_t custom = r.U8();
        uint32_t object = r.U32();
        node.stateObjects[state] = custom ? object : 0;
    }
    uint16_t curves = r.U16();
    for (uint16_t i = 0; i < curves && !r.failed; i++) node.curves.push_back(ReadCurve(r));
}

void ReadChildren(Reader& r, Node& node)
{
    uint32_t count = r.U32();
    if (count > r.Left() / 4) { r.failed = true; return; }
    for (uint32_t i = 0; i < count; i++) node.children.push_back(r.U32());
}

// A dialogue event (type 15), as Armageddon's voice banks have it: the tree's depth (its arguments' count), the
// arguments (state groups), the tree's size, a probability and a mode byte, then the tree's nodes, 12 bytes each: a key,
// then a first child and a count (a node above the last level) or the node to play (one on the last level), a weight and
// a probability. The root is the first node.
bool ReadDialogueEvent(const uint8_t* body, size_t size, DialogueEvent& d)
{
    Reader r(body, size);
    uint32_t depth = r.U32();
    if (depth == 0 || depth > 8) return false;
    for (uint32_t i = 0; i < depth; i++) d.arguments.push_back(r.U32());
    uint32_t treeBytes = r.U32();
    r.U8();  // probability
    r.U8();  // mode (best match)
    if (r.failed || treeBytes % 12 || treeBytes > r.Left()) return false;
    const uint8_t* tree = body + (size - r.Left());
    const uint32_t count = treeBytes / 12;
    r.Skip(treeBytes);
    if (r.failed || r.Left() != 0) return false;
    auto u32 = [&](uint32_t node, int at) {
        uint32_t v;
        memcpy(&v, tree + 12ull * node + at, 4);
        return v;
    };
    auto u16 = [&](uint32_t node, int at) {
        uint16_t v;
        memcpy(&v, tree + 12ull * node + at, 2);
        return v;
    };
    std::vector<uint32_t> keys;
    bool ok = true;
    std::function<void(uint32_t, uint32_t)> walk = [&](uint32_t node, uint32_t level) {
        if (!ok || node >= count) {
            ok = false;
            return;
        }
        if (level == depth) {
            d.paths.emplace_back(keys, u32(node, 4));
            return;
        }
        uint32_t first = u16(node, 4), children = u16(node, 6);
        for (uint32_t c = first; c < first + children && ok; c++) {
            if (c >= count) {
                ok = false;
                break;
            }
            keys.push_back(u32(c, 0));
            walk(c, level + 1);
            keys.pop_back();
        }
    };
    if (count) walk(0, 0);
    return ok;
}

// One object of the HIRC chunk. False if it isn't read to its exact end (a layout not known).
bool ReadObject(uint32_t type, uint32_t id, const uint8_t* body, size_t size, Bank& bank, std::unique_ptr<Node>& node,
                std::unique_ptr<Action>& action, std::unique_ptr<Event>& event, std::unique_ptr<Bus>& bus,
                std::unique_ptr<Attenuation>& attenuation, std::unique_ptr<StateValues>& state)
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
        if (n.IsSourcePlugin()) {
            // A source plug-in's parameters: Wwise's silence has its length and its random range, less and more.
            uint32_t bytes = r.U32();
            if (n.IsSilence() && bytes >= 12) {
                n.silenceSeconds = r.F32();
                n.silenceLess = r.F32();
                n.silenceMore = r.F32();
                bytes -= 12;
            }
            r.Skip(bytes);
        }
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
            Node::Layer layer;
            layer.id = r.U32();
            uint16_t curves = r.U16();
            for (uint16_t k = 0; k < curves && !r.failed; k++) layer.curves.push_back(ReadCurve(r));
            layer.crossfadeParameter = r.U32();
            layer.crossfadeDefault = r.F32();
            uint32_t associated = r.U32();
            for (uint32_t j = 0; j < associated && !r.failed; j++) {
                uint32_t child = r.U32();
                std::vector<CurvePoint> points;
                uint32_t count = r.U32();
                if (count > r.Left() / 12) {
                    r.failed = true;
                    break;
                }
                ReadPoints(r, count, points);
                layer.children.emplace_back(child, std::move(points));
            }
            n.layerList.push_back(std::move(layer));
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
        // The head (WwiseBankWriter's ActionHead and Armageddon's): the delay and its random range, then the size of
        // the parameters that follow (0 for a break or a post of another event).
        a.delayMs = r.S32();
        r.S32();
        r.S32();
        uint32_t params = r.U32();
        if (params > r.Left()) { r.failed = true; break; }
        Reader p(body + (size - r.Left()), params);
        r.Skip(params);
        if (params == 8) {
            // A set state or set switch: the group and its value.
            a.group = p.U32();
            a.groupValue = p.U32();
        } else if (params >= 13) {
            // The fade, its random range, its curve; a "set" kind's value 4 bytes after (then its own random range).
            a.fadeMs = p.S32();
            p.S32();
            p.S32();
            a.fadeCurve = p.U8();
            if (params >= 21) {
                p.Skip(4);
                a.value = p.F32();
            }
        }
        if (a.Kind() == ActionPlay && r.Left() >= 4) a.bankId = r.U32();
        r.Skip(r.Left());
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
        b.lfe = r.F32();
        b.pitch = r.F32();
        b.lowPass = r.F32();
        b.killNewest = r.U8() != 0;
        b.mostAtOnce = r.U16();
        r.Skip(3);
        b.stateGroup = r.U32();
        b.recoveryMs = static_cast<int>(r.U32());
        b.maxDuckDb = r.F32();
        r.U32();
        uint32_t ducks = r.U32();
        for (uint32_t i = 0; i < ducks && !r.failed; i++) {
            Bus::Duck d;
            d.bus = r.U32();
            d.db = r.F32();
            d.fadeOutMs = r.S32();
            d.fadeInMs = r.S32();
            d.curve = r.U8();
            b.ducks.push_back(d);
        }
        // Its effects, as a node's (read, not played).
        uint8_t slots = r.U8();
        if (slots) {
            r.U8();
            for (uint8_t i = 0; i < slots && !r.failed; i++) {
                r.U8();
                r.U32();
                r.U8();
                r.Skip(r.U32());
                r.U32();
            }
            b.effectSlots = slots;
        }
        uint16_t curves = r.U16();
        for (uint16_t i = 0; i < curves && !r.failed; i++) b.curves.push_back(ReadCurve(r));
        // Its states: each the state, a flag, the state object it uses (with 0, the group's own values).
        uint32_t states = r.U32();
        for (uint32_t i = 0; i < states && !r.failed; i++) {
            uint32_t state = r.U32();
            uint8_t custom = r.U8();
            uint32_t object = r.U32();
            b.stateObjects[state] = custom ? object : 0;
        }
        break;
    }
    case TypeState: {
        // A state object: volume, LFE, pitch and low-pass for one state, then how each is meant (4 bytes).
        state = std::make_unique<StateValues>();
        state->volume = r.F32();
        state->lfe = r.F32();
        state->pitch = r.F32();
        state->lowPass = r.F32();
        r.Skip(4);
        bank.stateObjects.push_back(id);
        break;
    }
    case TypeAttenuation: {
        // A cone (its inner and outer angles, the outer volume and low-pass), which curve each of volume (dry; and
        // wet for the game's and the user's sends), low-pass and spread uses (-1: none), the curves (a scaling,
        // then points), and curves on game parameters (read, not used). Ours are flat: our fades are curves on
        // game_distance on the nodes (WwiseBankWriter.Attenuation).
        attenuation = std::make_unique<Attenuation>();
        Attenuation& at = *attenuation;
        at.cone = r.U8() != 0;
        if (at.cone) {
            at.coneInner = r.F32();
            at.coneOuter = r.F32();
            at.coneOuterDb = r.F32();
            at.coneLowPass = r.F32();
        }
        int8_t uses[5];
        for (int8_t& u : uses) u = r.S8();
        uint8_t count = r.U8();
        for (uint8_t i = 0; i < count && !r.failed; i++) {
            Curve c;
            c.scaling = r.U8();
            ReadPoints(r, r.U16(), c.points);
            at.curves.push_back(std::move(c));
        }
        auto use = [&](int8_t index) { return index >= 0 && index < static_cast<int>(at.curves.size()) ? index : -1; };
        at.volumeDry = use(uses[0]);
        at.lowPass = use(uses[3]);
        at.spread = use(uses[4]);
        if (at.volumeDry >= 0) at.maxDistance = at.curves[at.volumeDry].LastX();
        uint16_t onParameters = r.U16();
        for (uint16_t i = 0; i < onParameters && !r.failed; i++) ReadCurve(r);
        bank.attenuations.push_back(id);
        break;
    }
    default:
        bank.unknownObjects++;
        bank.unread.emplace_back(id, type);
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

// Wwise's curve shapes (AkCurveInterpolation), from a segment's start (t 0) to its end (t 1). The forms are the usual
// readings of Wwise's own graphs: the logarithmic ones rise fast, the exponential ones slow, the S ones ease in and out.
float CurveShape(uint32_t shape, float t)
{
    constexpr float Pi = 3.14159265f;
    t = std::clamp(t, 0.0f, 1.0f);
    switch (shape) {
    case 0: return 1.0f - std::pow(1.0f - t, 3.0f);                       // logarithmic, base 3
    case 1: return std::sin(t * Pi / 2);                                  // sine (constant power, rising)
    case 2: return 1.0f - std::pow(1.0f - t, 1.41f);                      // logarithmic, base 1.41
    case 3: return 2.0f * t - 0.5f * (1.0f - std::cos(Pi * t));           // inverted S
    case 5: return 0.5f * (1.0f - std::cos(Pi * t));                      // S
    case 6: return std::pow(t, 1.41f);                                    // exponential, base 1.41
    case 7: return 1.0f - std::cos(t * Pi / 2);                           // reciprocal sine
    case 8: return t * t * t;                                             // exponential, base 3
    case 9: return 0.0f;                                                  // constant: the start's value
    default: return t;                                                    // 4 linear
    }
}

float Curve::ValueAt(float x) const { return ValueAt(points, x); }

float Curve::ValueAt(const std::vector<CurvePoint>& points, float x)
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
        return a.y + (b.y - a.y) * CurveShape(a.shape, t);
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
            memcpy(&f.avgBytesPerSecond, body + 8, 4);
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
        } else if (memcmp(chunk, "vorb", 4) == 0 && length >= 0x34 && available >= 0x34) {
            // Wwise's Vorbis (bank version 48): its sample count, where its setup packet and first audio packet are
            // in the data, and its two block sizes (as powers of 2).
            uint32_t samples, setup, audio;
            memcpy(&samples, body, 4);
            memcpy(&setup, body + 0x18, 4);
            memcpy(&audio, body + 0x1C, 4);
            f.vorbisSamples = samples;
            f.vorbisSetup = setup;
            f.vorbisAudio = audio;
            f.vorbisBlocks[0] = std::min(body[0x30], body[0x31]);
            f.vorbisBlocks[1] = std::max(body[0x30], body[0x31]);
            f.hasVorb = true;
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
    } else if (f.tag == 0xFFFF) {
        if (!f.hasVorb) { why = "Wwise Vorbis without the vorb chunk of bank version 48"; return false; }
        if (f.channels > 2) { why = std::to_string(f.channels) + "-channel Wwise Vorbis (mono and stereo read)"; return false; }
        if (f.vorbisBlocks[0] < 6 || f.vorbisBlocks[1] > 13 || f.vorbisSetup >= f.dataBytes || f.vorbisAudio >= f.dataBytes) {
            why = "Wwise Vorbis with a vorb chunk not as Armageddon's";
            return false;
        }
        f.frames = f.vorbisSamples;
    } else if (f.tag == 1 || f.tag == 0xFFFE) {
        if (f.bits != 16) { why = std::to_string(f.bits) + "-bit PCM (16 read)"; return false; }
        f.frames = f.dataBytes / (2u * f.channels);
    } else {
        why = "codec " + std::to_string(f.tag) + " (PCM, Wwise ADPCM and Wwise Vorbis read)";
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

bool MediaFile::OpenPart(const std::wstring& path, uint64_t partBase, uint64_t partSize, std::string& why)
{
    if (!Open(path, why)) return false;
    base = partBase;
    limit = partSize;
    return true;
}

bool MediaFile::ReadAt(uint64_t offset, void* into, uint32_t size) const
{
    if (!handle || offset > limit || size > limit - offset) return false;
    offset += base;
    OVERLAPPED at{};
    at.Offset = static_cast<DWORD>(offset);
    at.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD read = 0;
    return ReadFile(handle, into, size, &read, &at) && read == size;
}

// The game's archives, version 6 (see docs\game-notes.md, "Archives"; tools\research\vppx.py): a 2 KB header (its
// magic 0x51890ACE and version at 0, its flags at 0x14C: 1 compressed, 2 condensed; at 0x154 the file count, then
// the directory's, the names' and the data's sizes), the directory from 0x800 (24 bytes a file: its name's place,
// 4 bytes, its offset in the data, its size, its packed size, 4 bytes), the names, the data, each part on a 2 KB
// boundary.
static bool ReadArchive(const std::wstring& archive, std::vector<std::pair<std::string, ArchiveFile>>& files, std::string& why)
{
    files.clear();
    MediaFile file;
    if (!file.Open(archive, why)) return false;
    uint8_t header[0x16C];
    if (!file.ReadAt(0, header, sizeof header)) {
        why = "not one of the game's archives";
        return false;
    }
    uint32_t magic, version, flags, count, directoryBytes, namesBytes;
    memcpy(&magic, header, 4);
    memcpy(&version, header + 4, 4);
    memcpy(&flags, header + 0x14C, 4);
    memcpy(&count, header + 0x154, 4);
    memcpy(&directoryBytes, header + 0x15C, 4);
    memcpy(&namesBytes, header + 0x160, 4);
    if (magic != 0x51890ACE || version != 6 || count > 1000000 || directoryBytes < 24ull * count) {
        why = "not an archive of Armageddon's (version 6)";
        return false;
    }
    auto align = [](uint64_t n) { return (n + 0x7FF) & ~0x7FFull; };
    const uint64_t namesAt = 0x800 + align(directoryBytes), dataAt = namesAt + align(namesBytes);
    std::vector<uint8_t> directory(24ull * count), nameBytes(namesBytes);
    if ((count && !file.ReadAt(0x800, directory.data(), static_cast<uint32_t>(directory.size()))) ||
        (namesBytes && !file.ReadAt(namesAt, nameBytes.data(), namesBytes))) {
        why = "its directory can't be read";
        return false;
    }
    const bool compressed = (flags & 1) != 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t nameAt, offset, size;
        memcpy(&nameAt, &directory[24ull * i], 4);
        memcpy(&offset, &directory[24ull * i + 8], 4);
        memcpy(&size, &directory[24ull * i + 12], 4);
        if (nameAt >= nameBytes.size()) continue;
        std::string fileName;
        for (size_t c = nameAt; c < nameBytes.size() && nameBytes[c]; c++) fileName += static_cast<char>(tolower(nameBytes[c]));
        ArchiveFile where;
        where.offset = dataAt + offset;  // its place if uncompressed (a compressed one's isn't worked out here)
        where.size = size;
        where.compressed = compressed;
        files.emplace_back(std::move(fileName), where);
    }
    return true;
}

bool ListArchive(const std::wstring& archive, std::vector<std::string>& names, std::string& why)
{
    std::vector<std::pair<std::string, ArchiveFile>> files;
    if (!ReadArchive(archive, files, why)) return false;
    names.clear();
    for (auto& f : files) names.push_back(f.first);
    return true;
}

bool FindInArchive(const std::wstring& archive, const std::vector<std::string>& names, std::vector<ArchiveFile>& found,
                   std::vector<bool>& present, std::string& why)
{
    found.assign(names.size(), ArchiveFile{});
    present.assign(names.size(), false);
    std::vector<std::pair<std::string, ArchiveFile>> files;
    if (!ReadArchive(archive, files, why)) return false;
    for (auto& f : files)
        for (size_t k = 0; k < names.size(); k++)
            if (!present[k] && f.first == names[k]) {
                present[k] = true;
                found[k] = f.second;
            }
    return true;
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
    std::unique_ptr<MediaFile> media;
    std::string mediaProblem;
    if (!mediaPath.empty() && GetFileAttributesW(mediaPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        media = std::make_unique<MediaFile>();
        std::string problem;
        if (!media->Open(mediaPath, problem)) {
            media.reset();
            mediaProblem = "its media file: " + problem;
        }
    } else if (!mediaPath.empty()) {
        mediaProblem = "no media file beside it";
    }
    return Parse(bytes, bankPath, mediaPath, std::move(media), mediaProblem, why);
}

Bank* Library::LoadFromArchive(const std::wstring& archive, const std::string& name, std::string& why,
                               const std::wstring& looseBank, const std::wstring& looseMedia)
{
    std::string lower;
    for (char c : name) lower += static_cast<char>(tolower(static_cast<unsigned char>(c)));
    std::wstring wideName(name.begin(), name.end());
    std::vector<ArchiveFile> found;
    std::vector<bool> present;
    if (!FindInArchive(archive, {lower + ".bnk_pc", lower + "_media.bnk_pc"}, found, present, why)) return nullptr;
    std::vector<uint8_t> bytes;
    if (!looseBank.empty()) {
        if (!ReadWholeFile(looseBank, bytes, why)) {
            why = "its override " + Narrow(looseBank) + ": " + why;
            return nullptr;
        }
    } else {
        if (!present[0]) {
            why = name + ".bnk_pc isn't in that archive";
            return nullptr;
        }
        if (found[0].compressed) {
            why = "that archive is compressed (its banks are read from a copy, and their media aren't in it)";
            return nullptr;
        }
        if (found[0].size > (64u << 20)) {
            why = "too big for a bank";
            return nullptr;
        }
        bytes.resize(static_cast<size_t>(found[0].size));
        MediaFile whole;
        if (!whole.Open(archive, why) || (!bytes.empty() && !whole.ReadAt(found[0].offset, bytes.data(), static_cast<uint32_t>(bytes.size())))) {
            why = "its bytes can't be read from the archive";
            return nullptr;
        }
    }
    std::unique_ptr<MediaFile> media;
    std::string mediaProblem;
    if (!looseMedia.empty()) {
        media = std::make_unique<MediaFile>();
        std::string problem;
        if (!media->Open(looseMedia, problem)) {
            media.reset();
            mediaProblem = "its media file's override: " + problem;
        }
    } else if (present[1] && !found[1].compressed) {
        media = std::make_unique<MediaFile>();
        std::string problem;
        if (!media->OpenPart(archive, found[1].offset, found[1].size, problem)) {
            media.reset();
            mediaProblem = "its media file: " + problem;
        }
    } else {
        mediaProblem = present[1] ? "its media file is compressed in the archive" : "no media file beside it in the archive";
    }
    Bank* bank = Parse(bytes, looseBank.empty() ? archive + L"\\" + wideName + L".bnk_pc" : looseBank,
                       looseMedia.empty() ? archive + L"\\" + wideName + L"_media.bnk_pc" : looseMedia, std::move(media), mediaProblem, why);
    if (bank) bank->armageddons = true;
    return bank;
}

Bank* Library::Parse(const std::vector<uint8_t>& bytes, const std::wstring& bankPath, const std::wstring& mediaPath,
                     std::unique_ptr<MediaFile> media, const std::string& mediaProblem, std::string& why)
{
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
    std::vector<std::unique_ptr<Attenuation>> newAttenuations;
    std::vector<std::pair<uint32_t, StateValues>> newStates;
    std::unordered_map<uint32_t, float> newDefaults;
    std::unordered_map<uint32_t, StateGroup> newGroups;
    float newThreshold = 0;
    bool sawGlobals = false, sawEnvironments = false, sawEffects = false;
    std::vector<std::unique_ptr<DialogueEvent>> newDialogue;
    ObstructionCurves newObstruction;
    std::unordered_map<uint32_t, Reverb> newReverbs;
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
                std::unique_ptr<Attenuation> attenuation;
                std::unique_ptr<StateValues> state;
                if (type == TypeDialogueEvent) {
                    auto d = std::make_unique<DialogueEvent>();
                    if (ReadDialogueEvent(body + p + 12, size - 4, *d)) {
                        d->id = id;
                        d->bankId = bank->id;
                        newDialogue.push_back(std::move(d));
                        p += 8 + size;
                        continue;
                    }
                }
                if (!ReadObject(type, id, body + p + 12, size - 4, *bank, node, action, event, bus, attenuation, state))
                    bank->problems.push_back("object " + Hex(id) + " (type " + std::to_string(type) + ") isn't laid out as read; left out");
                else if (node) { node->id = id; node->type = type; node->bankId = bank->id; newNodes.push_back(std::move(node)); }
                else if (action) { action->id = id; action->bankId = action->bankId ? action->bankId : bank->id; newActions.push_back(std::move(action)); }
                else if (event) { event->id = id; event->bankId = bank->id; newEvents.push_back(std::move(event)); }
                else if (bus) { bus->id = id; bus->bankId = bank->id; newBuses.push_back(std::move(bus)); }
                else if (attenuation) { attenuation->id = id; attenuation->bankId = bank->id; newAttenuations.push_back(std::move(attenuation)); }
                else if (state) newStates.emplace_back(id, *state);
                p += 8 + size;
            }
        } else if (memcmp(chunk, "STMG", 4) == 0 && length >= 12) {
            // init.bnk's global settings: the volume threshold; the state groups (each its id, its default transition
            // time, its states with what each does, its transitions); switch groups driven by game parameters; the
            // game parameters' defaults.
            Reader g(body, length);
            newThreshold = g.F32();
            uint32_t groups = g.U32();
            for (uint32_t i = 0; i < groups && !g.failed; i++) {
                StateGroup sg;
                sg.id = g.U32();
                sg.defaultTransitionMs = static_cast<int>(g.U32());
                uint32_t states = g.U32();
                for (uint32_t k = 0; k < states && !g.failed; k++) {
                    uint32_t state = g.U32();
                    g.U32();
                    uint32_t bytes = g.U32();
                    StateValues v;
                    if (bytes >= 20) {
                        g.U32();  // the state's id again
                        v.volume = g.F32();
                        v.lfe = g.F32();
                        v.pitch = g.F32();
                        v.lowPass = g.F32();
                        g.Skip(bytes - 20);
                    } else {
                        g.Skip(bytes);
                    }
                    sg.states[state] = v;
                }
                uint32_t transitions = g.U32();
                for (uint32_t k = 0; k < transitions && !g.failed; k++) {
                    StateGroup::Transition t;
                    t.from = g.U32();
                    t.to = g.U32();
                    t.ms = static_cast<int>(g.U32());
                    sg.transitions.push_back(t);
                }
                newGroups[sg.id] = std::move(sg);
            }
            uint32_t switches = g.U32();
            for (uint32_t i = 0; i < switches && !g.failed; i++) {
                g.U32();
                g.U32();
                g.Skip(12ull * g.U32());
            }
            if (switches) bank->problems.push_back(std::to_string(switches) + " switch groups driven by game parameters (not read yet)");
            uint32_t parameters = g.U32();
            for (uint32_t i = 0; i < parameters && !g.failed; i++) {
                uint32_t parameter = g.U32();
                newDefaults[parameter] = g.F32();
            }
            if (g.failed || g.Left() != 0) bank->problems.push_back("its global settings (STMG) aren't laid out as read; left out");
            else sawGlobals = true;
        } else if (memcmp(chunk, "ENVS", 4) == 0) {
            // Obstruction's volume and low-pass curves, then occlusion's: each whether it is on, its scaling, its points.
            Reader g(body, length);
            ObstructionCurves c;
            Curve* curves[] = {&c.obstructionVolume, &c.obstructionLowPass, &c.occlusionVolume, &c.occlusionLowPass};
            bool* on[] = {&c.obstructionVolumeOn, &c.obstructionLowPassOn, &c.occlusionVolumeOn, &c.occlusionLowPassOn};
            for (int i = 0; i < 4 && !g.failed; i++) {
                *on[i] = g.U8() != 0;
                curves[i]->scaling = g.U8();
                curves[i]->property = i % 2 == 0 ? PropertyVolume : PropertyLowPass;
                ReadPoints(g, g.U16(), curves[i]->points);
            }
            if (g.failed || g.Left() != 0) bank->problems.push_back("its environments (ENVS) aren't laid out as read; left out");
            else {
                newObstruction = c;
                sawEnvironments = true;
            }
        } else if (memcmp(chunk, "FXPR", 4) == 0) {
            // The effects environments use: each its id, its plug-in, its parameters' size and the parameters, then 6
            // bytes (0 in Armageddon's). Matrix Reverb's parameters: the reverb time, its high-frequency ratio, the
            // delay lines, the dry and wet levels (dB), the pre-delay (s), a mode.
            Reader g(body, length);
            uint32_t count = g.U32();
            for (uint32_t i = 0; i < count && !g.failed; i++) {
                uint32_t id = g.U32(), plugin = g.U32(), bytes = g.U32();
                if (bytes > g.Left()) {
                    g.failed = true;
                    break;
                }
                if (plugin == 0x00730003 && bytes >= 24) {
                    Reverb rv;
                    rv.id = id;
                    rv.seconds = g.F32();
                    rv.hfRatio = g.F32();
                    rv.delays = g.U32();
                    rv.dryDb = g.F32();
                    rv.wetDb = g.F32();
                    rv.preDelay = g.F32();
                    g.Skip(bytes - 24);
                    newReverbs[id] = rv;
                } else {
                    g.Skip(bytes);
                }
                g.Skip(6);
            }
            if (g.failed || g.Left() != 0) {
                bank->problems.push_back("its effects (FXPR) aren't laid out as read; left out");
                newReverbs.clear();
            } else {
                sawEffects = true;
            }
        }
        at += 8 + length;
    }
    if (!sawHeader) { why = "no bank header (BKHD)"; return nullptr; }
    if (bank->unknownObjects) bank->problems.push_back(std::to_string(bank->unknownObjects) + " objects of kinds not read (music, dialogue, states, ...)");

    // Its media file: the VWSB index ("VWSBPC  ", 0, 2, 1, the bank's id, the header's size + 16, a count, then
    // (id, offset, 0, size) a medium).
    if (media) {
        auto file = std::move(media);
        {
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
    } else if (!mediaProblem.empty()) {
        bank->problems.push_back(mediaProblem);
    }

    // A bank loaded again replaces the one before.
    Unload(bank->id);
    for (auto& n : newNodes) { bank->nodes.push_back(n->id); nodes[n->id] = std::move(n); }
    for (auto& a : newActions) { bank->actions.push_back(a->id); actions[a->id] = std::move(a); }
    for (auto& e : newEvents) { bank->events.push_back(e->id); events[e->id] = std::move(e); }
    for (auto& b : newBuses) { bank->buses.push_back(b->id); buses[b->id] = std::move(b); }
    for (auto& a : newAttenuations) attenuations[a->id] = std::move(a);
    for (auto& d : newDialogue) { bank->dialogueEvents.push_back(d->id); dialogueEvents[d->id] = std::move(d); }
    for (auto& [id, v] : newStates) stateObjects[id] = v;
    for (auto& [id, type] : bank->unread) unreadObjects[id] = type;
    if (sawGlobals) {
        volumeThreshold = newThreshold;
        parameterDefaults = std::move(newDefaults);
        stateGroups = std::move(newGroups);
    }
    if (sawEnvironments) obstruction = newObstruction;
    if (sawEffects) reverbs = std::move(newReverbs);
    Bank* kept = bank.get();
    banks[bank->id] = std::move(bank);
    CheckLinks(*kept);
    return kept;
}

// What the bank refers to that isn't loaded: children, parents, targets, media.
void Library::CheckLinks(Bank& bank)
{
    int missingMedia = 0, inBank = 0, plugins = 0, synthesized = 0;
    std::string firstMissing;
    for (uint32_t id : bank.nodes) {
        const Node& n = *nodes[id];
        for (uint32_t child : n.children)
            if (!FindNode(child)) bank.problems.push_back("node " + Hex(id) + "'s child " + Hex(child) + " isn't loaded");
        if (n.parent && !FindNode(n.parent)) bank.problems.push_back("node " + Hex(id) + "'s parent " + Hex(n.parent) + " isn't loaded");
        if (n.IsSound()) {
            if (n.IsSourcePlugin()) {
                plugins++;
                if (!n.IsSilence()) synthesized++;
            } else if (n.streamType != 1) inBank++;
            else if (!bank.media.count(n.source)) {
                if (missingMedia++ == 0) firstMissing = Hex(n.source);
            }
        }
    }
    if (missingMedia) bank.problems.push_back(std::to_string(missingMedia) + " sounds' media aren't in the media file (the first " + firstMissing + ")");
    if (inBank) bank.problems.push_back(std::to_string(inBank) + " sounds keep their media in the bank (not read)");
    if (synthesized) bank.problems.push_back(std::to_string(synthesized) + " of its " + std::to_string(plugins) + " source plug-in sounds are synthesized (tone, synthesis): not played yet");
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
    for (uint32_t id : bank.attenuations) attenuations.erase(id);
    for (uint32_t id : bank.stateObjects) stateObjects.erase(id);
    for (auto& [id, type] : bank.unread) unreadObjects.erase(id);
    for (uint32_t id : bank.dialogueEvents) dialogueEvents.erase(id);
    banks.erase(it);
}

void Library::Clear()
{
    banks.clear();
    nodes.clear();
    actions.clear();
    events.clear();
    buses.clear();
    attenuations.clear();
    stateObjects.clear();
    unreadObjects.clear();
    dialogueEvents.clear();
    parameterDefaults.clear();
    stateGroups.clear();
    obstruction = ObstructionCurves{};
    reverbs.clear();
    volumeThreshold = 0;
}

const Attenuation* Library::FindAttenuation(uint32_t id) const
{
    auto it = attenuations.find(id);
    return it == attenuations.end() ? nullptr : it->second.get();
}

float Library::ParameterDefault(uint32_t parameter) const
{
    auto it = parameterDefaults.find(parameter);
    return it == parameterDefaults.end() ? 0.0f : it->second;
}

uint32_t Library::UnreadType(uint32_t id) const
{
    auto it = unreadObjects.find(id);
    return it == unreadObjects.end() ? 0 : it->second;
}

const StateValues* Library::FindStateObject(uint32_t id) const
{
    auto it = stateObjects.find(id);
    return it == stateObjects.end() ? nullptr : &it->second;
}

const StateGroup* Library::FindStateGroup(uint32_t id) const
{
    auto it = stateGroups.find(id);
    return it == stateGroups.end() ? nullptr : &it->second;
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

uint32_t DialogueEvent::Resolve(const uint32_t* values, size_t count) const
{
    uint32_t best = 0;
    int bestExact = -1;
    for (const auto& [keys, node] : paths) {
        if (keys.size() != count) continue;
        int exact = 0;
        bool fits = true;
        for (size_t i = 0; i < count && fits; i++) {
            if (keys[i] == values[i]) exact++;
            else if (keys[i] != 0) fits = false;
        }
        if (fits && exact > bestExact) {
            best = node;
            bestExact = exact;
        }
    }
    return best;
}

const DialogueEvent* Library::FindDialogueEvent(uint32_t id) const
{
    auto it = dialogueEvents.find(id);
    return it == dialogueEvents.end() ? nullptr : it->second.get();
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
