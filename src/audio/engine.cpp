// The player (see engine.h): what an event does, how containers pick and go on, how a voice's level is worked
// out from its nodes, curves, distance and fades, and how voices share OpenAL's sources.
#include "engine.h"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace rfa_audio {

Engine& TheEngine()
{
    static Engine engine;
    return engine;
}

namespace {

constexpr int StreamBufferFrames = 8192;                // a stream's piece: 4 of them queued (about 0.7 s at 48 kHz)
constexpr int64_t WholeLimitBytes = 8ll << 20;          // a medium up to this decoded is decoded whole (and cached)
constexpr int RenderPiece = 480;                        // loopback: an update every 10 ms of sound at 48 kHz
constexpr uint32_t FirstPlayingId = 0x80000000u;        // ours never meet Wwise's (the hybrid's step 2)

float DbToGain(float db) { return db <= -200 ? 0.0f : std::pow(10.0f, db / 20.0f); }
float GainToDb(float gain) { return gain <= 1e-10f ? -999.0f : 20.0f * std::log10(gain); }

Vec3 Minus(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }
Vec3 Normalized(const Vec3& a, const Vec3& fallback)
{
    float l = Length(a);
    return l > 1e-6f ? Vec3{a.x / l, a.y / l, a.z / l} : fallback;
}

std::string Hex(uint32_t v)
{
    char text[16];
    snprintf(text, sizeof text, "%08X", v);
    return text;
}

std::string Number(double v, int decimals = 1)
{
    char text[32];
    snprintf(text, sizeof text, "%.*f", decimals, v);
    return text;
}

const char* TypeName(uint32_t type)
{
    switch (type) {
    case TypeSound: return "sound";
    case TypeRanSeq: return "random/sequence container";
    case TypeSwitch: return "switch container";
    case TypeActorMixer: return "actor-mixer";
    case TypeLayer: return "layer container";
    default: return "object";
    }
}

}  // namespace

float Fade::At(double t) const
{
    if (t >= end) return to;
    if (t <= start) return from;
    return from + (to - from) * static_cast<float>((t - start) / (end - start));
}

// ---- Opening and closing ---------------------------------------------------------------------------

Engine::~Engine()
{
    // Never joined here: at the process's end the thread is gone already, and a DLL's teardown mustn't wait.
    if (thread.joinable()) thread.detach();
}

bool Engine::Open(const rfa_audio_settings& settings, std::string& why)
{
    Close();
    std::lock_guard<std::mutex> hold(lock);
    if (!al.Load(why)) return false;
    loopback = settings.loopback != 0;
    maxSources = settings.max_sources > 0 ? settings.max_sources : 128;
    threshold = settings.volume_threshold_db < 0 ? settings.volume_threshold_db : Silent;
    int64_t cacheBytes = static_cast<int64_t>(settings.cache_mb > 0 ? settings.cache_mb : 64) << 20;
    std::vector<ALCint> attributes;
    if (loopback) {
        if (!al.alcLoopbackOpenDeviceSOFT || !al.alcRenderSamplesSOFT) {
            why = "this OpenAL Soft has no loopback device";
            al.Unload();
            return false;
        }
        sampleRate = settings.sample_rate > 0 ? settings.sample_rate : 48000;
        attributes.insert(attributes.end(), {ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT, ALC_FORMAT_TYPE_SOFT, ALC_FLOAT_SOFT, ALC_FREQUENCY, sampleRate});
        device = al.alcLoopbackOpenDeviceSOFT(nullptr);
    } else {
        if (settings.sample_rate > 0) attributes.insert(attributes.end(), {ALC_FREQUENCY, settings.sample_rate});
        device = al.alcOpenDevice(nullptr);
    }
    if (!device) {
        why = loopback ? "OpenAL Soft's loopback device didn't open" : "no sound device could be opened";
        al.Unload();
        return false;
    }
    // Sources: as many as allowed, mono or stereo. HRTF only when asked (headphones).
    attributes.insert(attributes.end(), {ALC_MONO_SOURCES, maxSources, ALC_STEREO_SOURCES, maxSources / 2 + 1,
                                         ALC_HRTF_SOFT, settings.hrtf ? ALC_TRUE : ALC_FALSE, 0});
    context = al.alcCreateContext(device, attributes.data());
    if (!context) {
        why = "OpenAL made no context (ALC error " + std::to_string(al.alcGetError(device)) + ")";
        al.alcCloseDevice(device);
        device = nullptr;
        al.Unload();
        return false;
    }
    al.alcMakeContextCurrent(context);
    ALCint rate = 0;
    al.alcGetIntegerv(device, ALC_FREQUENCY, 1, &rate);
    if (rate > 0) sampleRate = rate;
    // The distance model off: each voice's gain is worked out from its curves (as Wwise does), OpenAL only pans.
    // The listener stays at the origin facing -z; voices are placed relative to it (ToListener).
    al.alDistanceModel(AL_NONE);
    al.alDopplerFactor(0.0f);
    cache.Init(&al, cacheBytes);
    random.seed(loopback ? 12345u : GetTickCount());
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    clockStart = counter.QuadPart;
    clockRate = frequency.QuadPart;
    now = 0;
    renderedFrames = 0;
    nextPlayingId = FirstPlayingId;
    ownThread = settings.own_thread && !loopback;
    if (ownThread) {
        stopThread = false;
        thread = std::thread([this] {
            for (;;) {
                std::vector<EndNote> done;
                rfa_audio_end_callback callback;
                void* user;
                {
                    std::lock_guard<std::mutex> held(lock);
                    if (stopThread) break;
                    Update();
                    done = TakeEnds();
                    callback = endCallback;
                    user = endUser;
                }
                if (callback)
                    for (const EndNote& e : done) callback(e.playingId, e.eventId, e.object, e.voices, user);
                Sleep(10);
            }
        });
    }
    return true;
}

void Engine::Close()
{
    if (thread.joinable()) {
        {
            std::lock_guard<std::mutex> hold(lock);
            stopThread = true;
        }
        thread.join();
    }
    std::lock_guard<std::mutex> hold(lock);
    if (!context) return;
    for (auto& [id, play] : plays)
        for (auto& root : play->roots) Finish(*root);
    plays.clear();
    pending.clear();
    limited.clear();
    liveVoices.clear();
    ends.clear();
    cache.Clear();
    for (ALuint source : freeSources) al.alDeleteSources(1, &source);
    freeSources.clear();
    sourcesMade = 0;
    library.Clear();
    objects.clear();
    globalParameters.clear();
    states.clear();
    sequencePositions.clear();
    randomHistory.clear();
    al.alcMakeContextCurrent(nullptr);
    al.alcDestroyContext(context);
    al.alcCloseDevice(device);
    context = nullptr;
    device = nullptr;
    al.Unload();
}

std::string Engine::DeviceInfo() const
{
    if (!context) return "";
    const ALCchar* name = al.alcGetString(device, ALC_ALL_DEVICES_SPECIFIER);
    if (!name || !*name) name = al.alcGetString(device, ALC_DEVICE_SPECIFIER);
    ALCint hrtf = 0;
    al.alcGetIntegerv(device, ALC_HRTF_SOFT, 1, &hrtf);
    std::string text = loopback ? "loopback (no speakers)" : (name ? name : "?");
    text += ", " + std::to_string(sampleRate) + " Hz";
    if (hrtf) text += ", HRTF";
    const ALchar* version = al.alGetString(AL_VERSION);
    if (version) text += ", OpenAL " + std::string(version);
    return text;
}

// ---- Banks -----------------------------------------------------------------------------------------

uint32_t Engine::LoadBank(const std::wstring& bankPath, const std::wstring& mediaPath, std::string& why)
{
    if (!context) {
        why = "the engine isn't open";
        return 0;
    }
    // The bank's id, from its header: one loaded already is unloaded first (its sounds stop).
    HANDLE h = CreateFileW(bankPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        uint8_t head[16];
        DWORD read = 0;
        if (ReadFile(h, head, sizeof head, &read, nullptr) && read == sizeof head && memcmp(head, "BKHD", 4) == 0) {
            uint32_t id;
            memcpy(&id, head + 12, 4);
            UnloadBank(id);
        }
        CloseHandle(h);
    }
    Bank* bank = library.Load(bankPath, mediaPath, why);
    return bank ? bank->id : 0;
}

void Engine::UnloadBank(uint32_t bankId)
{
    if (!library.FindBank(bankId)) return;
    // What plays from it stops now, and its waiting actions go.
    std::function<void(Instance&)> stop = [&](Instance& in) {
        if (in.finished) return;
        if (in.node && in.node->bankId == bankId) Finish(in);
        else
            for (auto& c : in.children) stop(*c);
    };
    for (auto& [id, play] : plays)
        for (auto& root : play->roots) stop(*root);
    for (auto it = pending.begin(); it != pending.end();) {
        if (it->action->bankId == bankId || (library.FindNode(it->action->target) && library.FindNode(it->action->target)->bankId == bankId)) {
            if (Play* p = FindPlay(it->playingId)) p->pendingActions--;
            it = pending.erase(it);
        } else {
            ++it;
        }
    }
    Sweep();
    cache.ForgetBank(bankId);
    library.Unload(bankId);
}

// ---- Posting and stopping ----------------------------------------------------------------------------

uint32_t Engine::NewPlay(uint32_t eventId, uint64_t object)
{
    if (++nextPlayingId == 0) nextPlayingId = FirstPlayingId;
    auto play = std::make_unique<Play>();
    play->playingId = nextPlayingId;
    play->eventId = eventId;
    play->object = object;
    Object(object);
    plays[nextPlayingId] = std::move(play);
    return nextPlayingId;
}

Play* Engine::FindPlay(uint32_t playingId) const
{
    auto it = plays.find(playingId);
    return it == plays.end() ? nullptr : it->second.get();
}

uint32_t Engine::Post(uint32_t eventId, uint64_t object)
{
    const Event* event = library.FindEvent(eventId);
    if (!context || !event) return 0;
    uint32_t id = NewPlay(eventId, object);
    Play& play = *plays[id];
    for (uint32_t actionId : event->actions) {
        const Action* action = library.FindAction(actionId);
        if (!action) continue;
        if (action->delayMs > 0) {
            pending.push_back({now + action->delayMs / 1000.0, id, action});
            play.pendingActions++;
        } else {
            Execute(*action, play);
        }
    }
    return id;
}

uint32_t Engine::PlayNode(uint32_t nodeId, uint64_t object)
{
    const Node* node = library.FindNode(nodeId);
    if (!context || !node) return 0;
    uint32_t id = NewPlay(0, object);
    Play& play = *plays[id];
    if (auto in = Start(node, play, nullptr, 0)) play.roots.push_back(std::move(in));
    return id;
}

uint32_t Engine::PlayMedium(uint32_t bankId, uint32_t mediumId, bool loop, uint64_t object)
{
    Bank* bank = library.FindBank(bankId);
    if (!context || !bank) return 0;
    MediumInfo info;
    std::string why;
    if (!cache.Info(*bank, mediumId, info, why)) return 0;
    uint32_t id = NewPlay(0, object);
    Play& play = *plays[id];
    auto in = std::make_unique<Instance>();
    in->serial = ++nextSerial;
    in->play = &play;
    in->startedAt = now;
    auto voice = std::make_unique<Voice>();
    voice->medium = info;
    voice->mediumId = mediumId;
    voice->loopCount = loop ? 0 : 1;
    voice->channels = info.format.channels;
    voice->rate = info.format.rate;
    voice->codec = info.format.IsAdpcm() ? 2 : 1;
    voice->length = info.Seconds();
    voice->owner = in.get();
    if (static_cast<int64_t>(info.format.frames) * info.format.channels * 2 <= WholeLimitBytes) voice->decoded = cache.Get(info, why);
    in->voice = std::move(voice);
    Voice& v = *in->voice;
    if ((v.decoded || static_cast<int64_t>(info.format.frames) * info.format.channels * 2 > WholeLimitBytes) && BeginOnSource(v)) {
        liveVoices.push_back(&v);
        ApplySource(v, LevelOf(*in, Object(object)), Object(object));
        al.alSourcePlay(v.source);
        play.voicesStarted++;
        play.roots.push_back(std::move(in));
    }
    return id;
}

uint32_t Engine::PlaySamples(const int16_t* samples, int frames, int channels, int rate, bool loop, int loopStart, float volumeDb,
                             float pitchCents, bool threeD, uint64_t object)
{
    if (!context) return 0;
    std::string why;
    auto decoded = cache.FromSamples(samples, frames, channels, rate, loop, loopStart, why);
    if (!decoded) return 0;
    uint32_t id = NewPlay(0, object);
    Play& play = *plays[id];
    auto in = std::make_unique<Instance>();
    in->serial = ++nextSerial;
    in->play = &play;
    in->startedAt = now;
    in->ownVolume = volumeDb;
    in->ownPitch = pitchCents;
    in->ownThreeD = threeD && channels == 1;
    auto voice = std::make_unique<Voice>();
    voice->decoded = decoded;
    voice->loopCount = loop ? 0 : 1;
    voice->channels = channels;
    voice->rate = rate;
    voice->codec = 3;
    voice->length = decoded->Seconds();
    voice->owner = in.get();
    in->voice = std::move(voice);
    Voice& v = *in->voice;
    if (BeginOnSource(v)) {
        liveVoices.push_back(&v);
        ApplySource(v, LevelOf(*in, Object(object)), Object(object));
        al.alSourcePlay(v.source);
        play.voicesStarted++;
        play.roots.push_back(std::move(in));
    }
    return id;
}

void Engine::StopPlaying(uint32_t playingId, int fadeMs)
{
    Play* play = FindPlay(playingId);
    if (!play) return;
    for (auto& root : play->roots) StopInstance(*root, fadeMs);
    pending.erase(std::remove_if(pending.begin(), pending.end(), [&](const PendingAction& p) { return p.playingId == playingId; }), pending.end());
    play->pendingActions = 0;
}

void Engine::StopObject(uint64_t object, int fadeMs)
{
    for (auto& [id, play] : plays)
        if (object == 0 || play->object == object) StopPlaying(id, fadeMs);
}

bool Engine::IsPlaying(uint32_t playingId) const
{
    const Play* play = FindPlay(playingId);
    return play && !play->ended;
}

void Engine::Execute(const Action& action, Play& play)
{
    bool everywhere = action.Scope() == ScopeEverywhere;
    switch (action.Kind()) {
    case ActionPlay:
        if (const Node* node = library.FindNode(action.target))
            if (auto in = Start(node, play, nullptr, action.fadeMs)) play.roots.push_back(std::move(in));
        break;
    case ActionStop:
        StopMatching(action.target, play.object, everywhere, action.fadeMs);
        break;
    case ActionBreak:
        BreakMatching(action.target, play.object, everywhere);
        break;
    default:
        break;  // pause, resume, mute, ...: none in our banks (the hybrid's step 3 copies Armageddon's bus-wide ones)
    }
}

void Engine::RunPending()
{
    for (size_t i = 0; i < pending.size();) {
        if (pending[i].at > now) {
            i++;
            continue;
        }
        PendingAction due = pending[i];
        pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
        if (Play* play = FindPlay(due.playingId)) {
            play->pendingActions--;
            Execute(*due.action, *play);
        }
        i = 0;  // a stop may have taken others away
    }
}

// Whether a node is the target or under it (its parents, up to the top; or the bus it goes to).
bool Engine::Matches(const Node* node, uint32_t target) const
{
    for (int depth = 0; node && depth < 64; depth++) {
        if (node->id == target) return true;
        if (!node->parent) {
            for (uint32_t bus = node->bus; bus; bus = library.FindBus(bus) ? library.FindBus(bus)->parent : 0)
                if (bus == target) return true;
            return false;
        }
        node = library.FindNode(node->parent);
    }
    return false;
}

void Engine::StopMatching(uint32_t target, uint64_t object, bool everywhere, int fadeMs)
{
    std::function<void(Instance&)> visit = [&](Instance& in) {
        if (in.finished) return;
        if (in.node && Matches(in.node, target)) {
            StopInstance(in, fadeMs);
            return;
        }
        for (auto& c : in.children) visit(*c);
    };
    for (auto& [id, play] : plays)
        if (everywhere || play->object == object)
            for (auto& root : play->roots) visit(*root);
    // A play of it waiting (delayed) on that object doesn't start.
    for (auto it = pending.begin(); it != pending.end();) {
        Play* p = FindPlay(it->playingId);
        if (p && it->action->Kind() == ActionPlay && (everywhere || p->object == object) && it->action->target == target) {
            p->pendingActions--;
            it = pending.erase(it);
        } else {
            ++it;
        }
    }
}

void Engine::BreakMatching(uint32_t target, uint64_t object, bool everywhere)
{
    std::function<void(Instance&)> visit = [&](Instance& in) {
        if (in.finished) return;
        if (in.node && Matches(in.node, target)) {
            BreakInstance(in);
            return;
        }
        for (auto& c : in.children) visit(*c);
    };
    for (auto& [id, play] : plays)
        if (everywhere || play->object == object)
            for (auto& root : play->roots) visit(*root);
}

void Engine::StopInstance(Instance& in, int fadeMs)
{
    if (in.finished) return;
    if (fadeMs <= 0) {
        Finish(in);
        return;
    }
    double end = now + fadeMs / 1000.0;
    if (in.stopping && in.fade.end <= end) return;  // already fading out, sooner
    in.fade = Fade{in.fade.At(now), 0.0f, now, end};
    in.stopping = true;
}

// A break: a container starts nothing more, a looping sound plays to the end of its pass.
void Engine::BreakInstance(Instance& in)
{
    in.broken = true;
    if (!in.voice) return;
    Voice& v = *in.voice;
    if (v.loopCount == 1) return;
    if (v.virtualVoice && v.length > 0) v.startedAt = now - std::fmod(now - v.startedAt, v.length);
    v.loopCount = 1;
    if (v.stream) v.stream->StopLooping();
    else if (v.source) al.alSourcei(v.source, AL_LOOPING, AL_FALSE);
}

void Engine::Finish(Instance& in)
{
    if (in.finished) return;
    in.finished = true;
    for (auto& c : in.children) Finish(*c);
    if (in.voice) {
        Voice& v = *in.voice;
        ReleaseSource(v);
        v.ended = true;
        v.stream.reset();
        liveVoices.erase(std::remove(liveVoices.begin(), liveVoices.end(), &v), liveVoices.end());
    }
    if (in.limitOwner) {
        auto& list = limited[in.limitOwner];
        list.erase(std::remove(list.begin(), list.end(), &in), list.end());
        if (list.empty()) limited.erase(in.limitOwner);
    }
}

// ---- Starting nodes ------------------------------------------------------------------------------------

// The node whose voice limit counts this one: itself or the first parent with settings of its own (or the top).
const Node* Engine::LimitOwner(const Node* node) const
{
    for (int depth = 0; node && depth < 64; depth++) {
        if (node->ownLimit || !node->parent) return node->mostAtOnce > 0 ? node : nullptr;
        node = library.FindNode(node->parent);
    }
    return nullptr;
}

std::unique_ptr<Instance> Engine::Start(const Node* node, Play& play, Instance* parent, int fadeInMs)
{
    if (!node) return nullptr;
    // Its voice limit (counted once on the way down: a container's children count in its own place).
    const Node* owner = LimitOwner(node);
    bool counts = owner != nullptr;
    for (Instance* p = parent; p && counts; p = p->parent)
        if (p->limitOwner == owner->id) counts = false;
    if (counts) {
        auto& list = limited[owner->id];
        if (static_cast<int>(list.size()) >= owner->mostAtOnce) {
            if (owner->refusesWhenFull) {
                refused++;
                return nullptr;
            }
            Finish(*list.front());  // the oldest ends for it
        }
    }
    auto in = std::make_unique<Instance>();
    in->serial = ++nextSerial;
    in->play = &play;
    in->node = node;
    in->parent = parent;
    in->startedAt = now;
    if (fadeInMs > 0) in->fade = Fade{0.0f, 1.0f, now, now + fadeInMs / 1000.0};
    if (node->volumeMin != node->volumeMax)
        in->randomVolume = std::uniform_real_distribution<float>(std::min(node->volumeMin, node->volumeMax), std::max(node->volumeMin, node->volumeMax))(random);
    if (node->pitchMin != node->pitchMax)
        in->randomPitch = std::uniform_real_distribution<float>(std::min(node->pitchMin, node->pitchMax), std::max(node->pitchMin, node->pitchMax))(random);
    if (counts) {
        in->limitOwner = owner->id;
        limited[owner->id].push_back(in.get());
    }
    GameObject& object = Object(play.object);
    bool started = false;
    switch (node->type) {
    case TypeSound:
        started = StartVoice(*in);
        break;
    case TypeRanSeq:
        if (!node->continuous) {
            // Step: one child each time it plays.
            uint32_t child = Pick(*in);
            started = child && StartChild(*in, child, 0);
        } else {
            in->loopsLeft = node->containerLoops;
            in->picksLeft = static_cast<int>(node->playlist.empty() ? node->children.size() : node->playlist.size());
            started = StartNextPick(*in);
            if (node->transitionMode == 5 && node->transitionMs > 0) {
                in->nextAt = now + node->transitionMs / 1000.0;
                started = started || !in->exhausted;
            }
        }
        break;
    case TypeSwitch: {
        in->switchValue = SwitchValue(*node, object);
        for (const auto& [value, children] : node->choices)
            if (value == in->switchValue)
                for (uint32_t child : children) started = StartChild(*in, child, 0) || started;
        break;
    }
    case TypeLayer:
        for (uint32_t child : node->children) started = StartChild(*in, child, 0) || started;
        break;
    default:
        break;  // an actor-mixer isn't played itself
    }
    if (!started) {
        Finish(*in);
        return nullptr;
    }
    return in;
}

bool Engine::StartChild(Instance& in, uint32_t child, int fadeInMs)
{
    auto started = Start(library.FindNode(child), *in.play, &in, fadeInMs);
    if (!started) return false;
    in.children.push_back(std::move(started));
    return true;
}

// A continuous container's next child; false once its passes are played (or the pick doesn't start).
bool Engine::StartNextPick(Instance& in)
{
    const Node& n = *in.node;
    int size = static_cast<int>(n.playlist.empty() ? n.children.size() : n.playlist.size());
    if (size == 0) {
        in.exhausted = true;
        return false;
    }
    if (in.picksLeft <= 0) {
        // A pass is played: another (for ever with a loop count of 0).
        if (n.containerLoops != 0 && --in.loopsLeft <= 0) {
            in.exhausted = true;
            return false;
        }
        in.picksLeft = size;
        in.sequencePosition = 0;
    }
    in.picksLeft--;
    uint32_t child = Pick(in);
    return child && StartChild(in, child, 0);
}

uint32_t Engine::Pick(Instance& in)
{
    const Node& n = *in.node;
    std::vector<std::pair<uint32_t, uint8_t>> list = n.playlist;
    if (list.empty())
        for (uint32_t c : n.children) list.emplace_back(c, 50);
    if (list.empty()) return 0;
    int size = static_cast<int>(list.size());
    uint64_t key = n.global ? n.id : (n.id ^ (in.play->object * 0x9E3779B97F4A7C15ull)) | (1ull << 63);
    if (n.sequence) {
        if (n.continuous) return list[in.sequencePosition++ % size].first;
        int& position = sequencePositions[key];
        uint32_t child = list[position % size].first;
        position = (position + 1) % size;
        return child;
    }
    // Random: not one of the last few (avoidRepeat), or, shuffled, not one played this round.
    std::vector<uint32_t>& history = randomHistory[key];
    std::vector<uint32_t> excluded;
    if (n.randomMode == 1) {
        if (static_cast<int>(history.size()) >= size) {
            uint32_t last = history.back();
            history.clear();
            if (n.avoidRepeat && size > 1) history.push_back(last);  // the round starts again, but not with the last one
        }
        excluded = history;
    } else {
        int avoid = std::min<int>(n.avoidRepeat, size - 1);
        excluded.assign(history.end() - std::min<int>(avoid, static_cast<int>(history.size())), history.end());
    }
    float total = 0;
    for (const auto& [child, weight] : list)
        if (std::find(excluded.begin(), excluded.end(), child) == excluded.end()) total += weight > 0 ? weight : 1;
    uint32_t picked = list[0].first;
    if (total > 0) {
        float at = std::uniform_real_distribution<float>(0.0f, total)(random);
        for (const auto& [child, weight] : list) {
            if (std::find(excluded.begin(), excluded.end(), child) != excluded.end()) continue;
            picked = child;
            at -= weight > 0 ? weight : 1;
            if (at < 0) break;
        }
    }
    history.push_back(picked);
    if (n.randomMode != 1 && history.size() > static_cast<size_t>(size)) history.erase(history.begin());
    if (n.randomMode == 1 && history.size() > static_cast<size_t>(size)) history.erase(history.begin());
    return picked;
}

uint32_t Engine::SwitchValue(const Node& node, const GameObject& object) const
{
    if (node.onState) {
        auto it = states.find(node.group);
        return it == states.end() ? node.defaultValue : it->second;
    }
    auto it = object.switches.find(node.group);
    return it == object.switches.end() ? node.defaultValue : it->second;
}

bool Engine::StartVoice(Instance& in)
{
    const Node& n = *in.node;
    Bank* bank = library.FindBank(n.bankId);
    auto voice = std::make_unique<Voice>();
    std::string why;
    if (!bank || !cache.Info(*bank, n.source, voice->medium, why)) return false;
    Voice& v = *voice;
    v.owner = &in;
    v.mediumId = n.source;
    v.loopCount = n.loopCount < 0 ? 1 : n.loopCount;
    v.channels = v.medium.format.channels;
    v.rate = v.medium.format.rate;
    v.codec = v.medium.format.IsAdpcm() ? 2 : 1;
    v.length = v.medium.Seconds();
    in.voice = std::move(voice);
    GameObject& object = Object(in.play->object);
    Level level = LevelOf(in, object);
    v.threeD = level.threeD;
    v.priority = level.priority;
    v.whenQuiet = level.whenQuiet;
    v.bus = level.bus;
    v.distance = level.distance;
    v.levelDb = level.db + GainToDb(level.fadeGain);
    v.startedAt = now;
    if (level.db < threshold) {
        if (level.whenQuiet == WhenQuiet::End) {
            endedQuiet++;
            return false;
        }
        if (level.whenQuiet == WhenQuiet::Virtual) {
            v.virtualVoice = true;
            liveVoices.push_back(&v);
            in.play->voicesStarted++;
            return true;
        }
    }
    bool whole = static_cast<int64_t>(v.medium.format.frames) * v.channels * 2 <= WholeLimitBytes && v.loopCount <= 1;
    if (whole) {
        v.decoded = cache.Get(v.medium, why);
        if (!v.decoded) return false;
    }
    if (!BeginOnSource(v)) {
        if (v.whenQuiet != WhenQuiet::Virtual) return false;
        v.virtualVoice = true;  // no source free: it waits, virtual, for one
        liveVoices.push_back(&v);
        in.play->voicesStarted++;
        return true;
    }
    liveVoices.push_back(&v);
    in.play->voicesStarted++;
    ApplySource(v, level, object);
    al.alSourcePlay(v.source);
    return true;
}

// ---- Voices on sources -----------------------------------------------------------------------------

bool Engine::AcquireSource(Voice& voice, ALuint& source)
{
    if (!freeSources.empty()) {
        source = freeSources.back();
        freeSources.pop_back();
        return true;
    }
    if (sourcesMade < maxSources) {
        al.alGetError();
        al.alGenSources(1, &source);
        if (al.alGetError() == AL_NO_ERROR) {
            sourcesMade++;
            return true;
        }
        maxSources = sourcesMade;  // OpenAL has no more
    }
    // None free: the voice of the lowest priority (the quietest of those) gives its up, if lower than this one.
    Voice* victim = nullptr;
    for (Voice* v : liveVoices)
        if (v->source && v != &voice && (!victim || v->priority < victim->priority || (v->priority == victim->priority && v->levelDb < victim->levelDb)))
            victim = v;
    if (!victim || victim->priority >= voice.priority) return false;
    stolen++;
    ReleaseSource(*victim);
    if (victim->whenQuiet == WhenQuiet::Virtual) victim->virtualVoice = true;
    else victim->ended = true;
    source = freeSources.back();
    freeSources.pop_back();
    return true;
}

void Engine::ReleaseSource(Voice& v)
{
    if (!v.source) return;
    al.alSourceStop(v.source);
    al.alSourcei(v.source, AL_BUFFER, 0);  // detaches its buffer, or unqueues a stream's
    freeSources.push_back(v.source);
    v.source = 0;
    if (v.buffers[0]) {
        al.alDeleteBuffers(4, v.buffers);
        memset(v.buffers, 0, sizeof v.buffers);
    }
    v.stream.reset();
}

// Puts a voice on a source from its beginning: its buffer, or its stream's first pieces. Not yet playing.
bool Engine::BeginOnSource(Voice& v)
{
    ALuint source;
    if (!AcquireSource(v, source)) return false;
    v.source = source;
    al.alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
    al.alSourcef(source, AL_ROLLOFF_FACTOR, 0.0f);
    al.alSourcef(source, AL_MAX_GAIN, 16.0f);
    if (v.decoded) {
        al.alSourcei(source, AL_BUFFER, static_cast<ALint>(v.decoded->buffer));
        al.alSourcei(source, AL_LOOPING, v.loopCount == 0 ? AL_TRUE : AL_FALSE);
    } else {
        al.alSourcei(source, AL_LOOPING, AL_FALSE);
        al.alSourcei(source, AL_BUFFER, 0);
        std::string why;
        v.stream = std::make_unique<Stream>();
        if (!v.stream->Open(v.medium, v.loopCount, why)) {
            ReleaseSource(v);
            return false;
        }
        al.alGenBuffers(4, v.buffers);
        int queued = 0;
        for (ALuint buffer : v.buffers) {
            if (!FillStream(v, buffer)) break;
            al.alSourceQueueBuffers(source, 1, &buffer);
            queued++;
        }
        if (!queued) {
            ReleaseSource(v);
            return false;
        }
    }
    v.virtualVoice = false;
    v.everPlayed = true;
    v.startedAt = now;
    v.framesQueued = 0;
    return true;
}

bool Engine::FillStream(Voice& v, ALuint buffer)
{
    if (!v.stream || v.stream->AtEnd()) return false;
    streamScratch.resize(static_cast<size_t>(StreamBufferFrames) * v.channels);
    int frames = v.stream->Read(streamScratch.data(), StreamBufferFrames);
    if (frames <= 0) return false;
    al.alBufferData(buffer, Format16(al, v.channels), streamScratch.data(), frames * v.channels * 2, v.rate);
    return true;
}

void Engine::ServiceStream(Voice& v)
{
    ALint processed = 0;
    al.alGetSourcei(v.source, AL_BUFFERS_PROCESSED, &processed);
    while (processed-- > 0) {
        ALuint buffer = 0;
        al.alSourceUnqueueBuffers(v.source, 1, &buffer);
        if (FillStream(v, buffer)) al.alSourceQueueBuffers(v.source, 1, &buffer);
    }
    ALint state = 0, queued = 0;
    al.alGetSourcei(v.source, AL_SOURCE_STATE, &state);
    al.alGetSourcei(v.source, AL_BUFFERS_QUEUED, &queued);
    if (state != AL_PLAYING) {
        if (queued > 0) {
            underruns++;  // it ran dry before it was refilled: on again
            al.alSourcePlay(v.source);
        } else {
            v.ended = true;
        }
    }
}

// The voice's place relative to the listener, in OpenAL's terms: the listener's right is +x, up +y, front -z.
// The game's axes are left-handed (x right, y up, z forward), so right = up × front.
Vec3 Engine::ToListener(const Vec3& position) const
{
    Vec3 front = Normalized(listenerFront, {0, 0, 1});
    Vec3 right = Normalized(Cross(listenerUp, front), {1, 0, 0});
    Vec3 up = Cross(front, right);
    Vec3 relative = Minus(position, listenerPosition);
    return {Dot(relative, right), Dot(relative, up), -Dot(relative, front)};
}

void Engine::ApplySource(Voice& v, const Level& level, const GameObject& object)
{
    if (!v.source) return;
    float gain = DbToGain(level.db) * level.fadeGain;
    al.alSourcef(v.source, AL_GAIN, std::min(gain, 16.0f));
    al.alSourcef(v.source, AL_PITCH, std::clamp(std::pow(2.0f, level.pitchCents / 1200.0f), 0.05f, 8.0f));
    if (level.threeD && v.channels == 1 && object.placed) {
        Vec3 p = ToListener(object.position);
        al.alSource3f(v.source, AL_POSITION, p.x, p.y, p.z);
    } else {
        al.alSource3f(v.source, AL_POSITION, 0.0f, 0.0f, 0.0f);
    }
}

float Engine::ParameterValue(const GameObject& object, uint32_t parameter, float distance) const
{
    if (auto it = object.parameters.find(parameter); it != object.parameters.end()) return it->second;
    if (parameter == GameDistance) return distance;
    if (auto it = globalParameters.find(parameter); it != globalParameters.end()) return it->second;
    return 0.0f;
}

// A voice's level: its nodes' volumes and curves (each node up to the top), the bus's, the random ranges picked;
// the fades apart. Placing, priority and the too-quiet rule come from the first node with its own, or the top.
Engine::Level Engine::LevelOf(const Instance& in, const GameObject& object) const
{
    Level level;
    level.distance = object.placed ? Length(Minus(object.position, listenerPosition)) : 0.0f;
    for (const Instance* i = &in; i; i = i->parent) {
        level.fadeGain *= i->fade.At(now);
        level.db += i->randomVolume;
        level.pitchCents += i->randomPitch;
    }
    if (!in.node) {
        level.db += in.ownVolume;
        level.pitchCents += in.ownPitch;
        level.threeD = in.ownThreeD;
        return level;
    }
    float curveGain = 1.0f;
    bool placed = false, prioritised = false, quietRule = false;
    const Node* n = in.node;
    for (int depth = 0; n && depth < 64; depth++) {
        level.db += n->volume;
        level.pitchCents += n->pitch;
        for (const Curve& c : n->curves) {
            float x = ParameterValue(object, c.parameter, level.distance);
            if (c.property == PropertyVolume) curveGain *= c.GainAt(x);
            else if (c.property == PropertyPitch) level.pitchCents += c.ValueAt(x);
        }
        bool top = n->parent == 0;
        if (!placed && (n->ownPositioning || top)) {
            level.threeD = n->ownPositioning && n->threeD;
            placed = true;
        }
        if (!prioritised && (n->ownPriority || top)) {
            level.priority = n->priority;
            prioritised = true;
        }
        if (!quietRule && (n->ownQuiet || top)) {
            level.whenQuiet = n->whenQuiet;
            quietRule = true;
        }
        if (top) {
            level.bus = n->bus;
            break;
        }
        n = library.FindNode(n->parent);
    }
    level.db += GainToDb(curveGain);
    level.db += library.BusVolume(level.bus);
    return level;
}

void Engine::UpdateVoice(Instance& in, GameObject& object)
{
    Voice& v = *in.voice;
    if (v.ended) return;
    Level level = LevelOf(in, object);
    v.levelDb = level.db + GainToDb(level.fadeGain);
    v.distance = level.distance;
    v.priority = level.priority;
    bool quiet = level.db < threshold;
    if (v.source) {
        if (quiet && v.whenQuiet == WhenQuiet::End) {
            endedQuiet++;
            v.ended = true;
            ReleaseSource(v);
            return;
        }
        if (quiet && v.whenQuiet == WhenQuiet::Virtual) {
            ReleaseSource(v);  // it keeps its time from when it started; it starts again from its beginning
            v.virtualVoice = true;
            return;
        }
        ApplySource(v, level, object);
        if (v.decoded) {
            ALint state = 0;
            al.alGetSourcei(v.source, AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED) v.ended = true;
        } else {
            ServiceStream(v);
        }
    } else if (v.virtualVoice) {
        float pitch = std::clamp(std::pow(2.0f, level.pitchCents / 1200.0f), 0.05f, 8.0f);
        if (v.loopCount != 0 && now - v.startedAt >= v.length * std::max(1, v.loopCount) / pitch) {
            v.ended = true;
            return;
        }
        if (!quiet) {
            if (!v.decoded && static_cast<int64_t>(v.medium.format.frames) * v.channels * 2 <= WholeLimitBytes && v.loopCount <= 1) {
                std::string why;
                v.decoded = cache.Get(v.medium, why);
                if (!v.decoded) {
                    v.ended = true;
                    return;
                }
            }
            if (BeginOnSource(v)) {
                ApplySource(v, level, object);
                al.alSourcePlay(v.source);
            }
        }
    } else {
        v.ended = true;
    }
    if (v.ended) ReleaseSource(v);
}

void Engine::UpdateInstance(Instance& in, GameObject& object)
{
    if (in.finished) return;
    if (in.stopping && now >= in.fade.end) {
        Finish(in);
        return;
    }
    const Node* n = in.node;
    if (!n || n->type == TypeSound) {
        if (in.voice) UpdateVoice(in, object);
        if (!in.voice || in.voice->ended) Finish(in);
        return;
    }
    size_t count = in.children.size();
    for (size_t i = 0; i < count; i++) UpdateInstance(*in.children[i], object);
    if (!in.stopping) {
        if (n->type == TypeSwitch && n->followsChanges) {
            uint32_t value = SwitchValue(*n, object);
            if (value != in.switchValue) {
                in.switchValue = value;
                // The children playing for the old value stop (fading out), those of the new one start (fading in).
                for (auto& c : in.children) {
                    if (c->finished || c->stopping || !c->node) continue;
                    const Node::SwitchChild* params = n->SwitchParams(c->node->id);
                    StopInstance(*c, params ? params->fadeOutMs : 0);
                }
                for (const auto& [choice, children] : n->choices)
                    if (choice == value)
                        for (uint32_t child : children) {
                            const Node::SwitchChild* params = n->SwitchParams(child);
                            StartChild(in, child, params ? params->fadeInMs : 0);
                        }
            }
        }
        if (n->type == TypeRanSeq && n->continuous && !in.broken && !in.exhausted) {
            if (n->transitionMode == 5 && n->transitionMs > 0) {
                // Trigger rate: a child every so often, whether the last has ended or not.
                for (int guard = 0; in.nextAt > 0 && now >= in.nextAt && guard < 64; guard++) {
                    StartNextPick(in);
                    if (in.exhausted) break;
                    float wait = n->transitionMs;
                    if (n->transitionRandomMin != 0 || n->transitionRandomMax != 0)
                        wait += std::uniform_real_distribution<float>(std::min(n->transitionRandomMin, n->transitionRandomMax),
                                                                      std::max(n->transitionRandomMin, n->transitionRandomMax))(random);
                    in.nextAt += std::max(1.0f, wait) / 1000.0;
                }
            } else {
                // One after another.
                bool playing = std::any_of(in.children.begin(), in.children.end(), [](const auto& c) { return !c->finished; });
                if (!playing) StartNextPick(in);
            }
        }
    }
    in.children.erase(std::remove_if(in.children.begin(), in.children.end(), [](const auto& c) { return c->finished; }), in.children.end());
    bool more = n->type == TypeRanSeq && n->continuous && !in.broken && !in.stopping && !in.exhausted;
    if (in.children.empty() && !more) Finish(in);
}

// ---- Updating --------------------------------------------------------------------------------------

void Engine::Update()
{
    if (!context) return;
    if (!loopback) {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        now = static_cast<double>(counter.QuadPart - clockStart) / static_cast<double>(clockRate);
    }
    RunPending();
    for (auto& [id, play] : plays) {
        GameObject& object = Object(play->object);
        size_t count = play->roots.size();
        for (size_t i = 0; i < count; i++) UpdateInstance(*play->roots[i], object);
    }
    Sweep();
    cache.Trim();
}

// Takes finished instances out, and reports the plays that have ended.
void Engine::Sweep()
{
    for (auto it = plays.begin(); it != plays.end();) {
        Play& play = *it->second;
        play.roots.erase(std::remove_if(play.roots.begin(), play.roots.end(), [](const auto& r) { return r->finished; }), play.roots.end());
        if (play.roots.empty() && play.pendingActions <= 0) {
            play.ended = true;
            ends.push_back({play.playingId, play.eventId, play.object, play.voicesStarted});
            it = plays.erase(it);
        } else {
            ++it;
        }
    }
}

int Engine::Render(float* out, int frames)
{
    if (!context || !loopback) return 0;
    static thread_local std::vector<float> scratch;
    int rendered = 0;
    while (rendered < frames) {
        int n = std::min(RenderPiece * sampleRate / 48000 + 1, frames - rendered);
        Update();
        float* into = out ? out + 2ll * rendered : nullptr;
        if (!into) {
            scratch.resize(2ull * n);
            into = scratch.data();
        }
        al.alcRenderSamplesSOFT(device, into, n);
        renderedFrames += n;
        now = static_cast<double>(renderedFrames) / sampleRate;
        rendered += n;
    }
    return rendered;
}

std::vector<EndNote> Engine::TakeEnds()
{
    std::vector<EndNote> taken;
    taken.swap(ends);
    return taken;
}

// ---- Objects, listener, parameters ------------------------------------------------------------------

GameObject& Engine::Object(uint64_t id)
{
    GameObject& object = objects[id];
    object.id = id;
    return object;
}

void Engine::SetPosition(uint64_t object, const float* position, const float* front)
{
    GameObject& o = Object(object);
    if (position) {
        o.position = {position[0], position[1], position[2]};
        o.placed = true;
    }
    if (front) o.front = {front[0], front[1], front[2]};
}

void Engine::SetListener(const float* position, const float* front, const float* up)
{
    if (position) listenerPosition = {position[0], position[1], position[2]};
    listenerFront = front ? Vec3{front[0], front[1], front[2]} : Vec3{0, 0, 1};
    listenerUp = up ? Vec3{up[0], up[1], up[2]} : Vec3{0, 1, 0};
}

void Engine::SetParameter(uint64_t object, uint32_t parameter, float value)
{
    if (object == 0) globalParameters[parameter] = value;
    else Object(object).parameters[parameter] = value;
}

void Engine::ResetParameter(uint64_t object, uint32_t parameter)
{
    if (object == 0) globalParameters.erase(parameter);
    else Object(object).parameters.erase(parameter);
}

void Engine::SetSwitch(uint64_t object, uint32_t group, uint32_t value)
{
    if (value == 0) Object(object).switches.erase(group);  // not set: each switch container's default
    else Object(object).switches[group] = value;
}

void Engine::SetState(uint32_t group, uint32_t state) { states[group] = state; }

void Engine::RemoveObject(uint64_t object)
{
    StopObject(object, 0);
    Sweep();
    objects.erase(object);
}

// ---- What is playing -------------------------------------------------------------------------------

int Engine::Voices(rfa_audio_voice* out, int max)
{
    int count = 0;
    for (Voice* v : liveVoices) {
        if (count < max && out) {
            rfa_audio_voice& o = out[count];
            o = rfa_audio_voice{};
            Instance* in = v->owner;
            o.playing_id = in && in->play ? in->play->playingId : 0;
            o.event_id = in && in->play ? in->play->eventId : 0;
            o.sound_id = in && in->node ? in->node->id : 0;
            o.medium_id = v->mediumId;
            o.object = in && in->play ? in->play->object : 0;
            o.bus = v->bus;
            o.codec = v->codec;
            o.channels = v->channels;
            o.rate = v->rate;
            o.loops = v->loopCount == 0;
            o.is_virtual = v->virtualVoice;
            o.streamed = v->stream != nullptr || (!v->decoded && !v->virtualVoice);
            o.three_d = v->threeD;
            o.level_db = v->levelDb;
            o.distance = v->distance;
            o.length_s = static_cast<float>(v->length);
            o.priority = v->priority;
            if (v->source && v->decoded) {
                float seconds = 0;
                al.alGetSourcef(v->source, AL_SEC_OFFSET, &seconds);
                o.position_s = seconds;
            } else if (v->stream && v->rate) {
                ALint queued = 0;
                al.alGetSourcei(v->source, AL_BUFFERS_QUEUED, &queued);
                float seconds = 0;
                al.alGetSourcef(v->source, AL_SEC_OFFSET, &seconds);
                double decodedSeconds = static_cast<double>(v->stream->Position()) / v->rate;
                o.position_s = static_cast<float>(std::max(0.0, decodedSeconds - queued * static_cast<double>(StreamBufferFrames) / v->rate + seconds));
            } else {
                o.position_s = static_cast<float>(v->length > 0 ? std::fmod(now - v->startedAt, v->length) : 0);
            }
        }
        count++;
    }
    return count;
}

void Engine::Status(rfa_audio_status& out)
{
    out = rfa_audio_status{};
    out.playing = static_cast<int>(plays.size());
    for (Voice* v : liveVoices) {
        out.voices++;
        if (v->virtualVoice) out.virtual_voices++;
        if (v->stream) out.streams++;
    }
    out.sources_used = sourcesMade - static_cast<int>(freeSources.size());
    out.sources = maxSources;
    out.cache_bytes = cache.Bytes();
    out.banks = static_cast<int>(library.Banks().size());
    out.voices_refused = refused;
    out.voices_ended_quiet = endedQuiet;
    out.voices_stolen = stolen;
    out.underruns = underruns;
}

// ---- Looking at a bank's tree ------------------------------------------------------------------------

bool Engine::MediumSeconds(const Node& sound, double& seconds, std::string& why)
{
    Bank* bank = library.FindBank(sound.bankId);
    MediumInfo info;
    if (!bank || !cache.Info(*bank, sound.source, info, why)) return false;
    seconds = info.Seconds();
    return true;
}

void Engine::EventInfo(uint32_t eventId, rfa_audio_event_info& out)
{
    out = rfa_audio_event_info{};
    const Event* event = library.FindEvent(eventId);
    if (!event) return;
    out.found = 1;
    double longest = 0;
    bool unknownLength = false;
    std::function<void(const Node&, bool, double, int)> visit = [&](const Node& n, bool threeD, double delay, int depth) {
        if (depth > 32) return;
        if (n.ownPositioning) threeD = n.threeD;
        if (threeD) out.three_d = 1;
        for (const Curve& c : n.curves)
            if (c.parameter == GameDistance) out.farthest_m = std::max(out.farthest_m, c.LastX());
        if (n.type == TypeSound) {
            if (n.loopCount == 0) out.loops = 1;
            double seconds = 0;
            std::string why;
            if (MediumSeconds(n, seconds, why)) longest = std::max(longest, delay + seconds * std::max<int>(1, n.loopCount));
            else {
                out.missing++;
                unknownLength = true;
            }
        }
        if (n.type == TypeRanSeq && n.continuous && n.containerLoops == 0) out.loops = 1;
        if (n.type == TypeSwitch && !out.switch_group) out.switch_group = n.group;
        for (uint32_t child : n.children) {
            if (const Node* c = library.FindNode(child)) visit(*c, threeD, delay, depth + 1);
            else out.missing++;
        }
    };
    for (uint32_t actionId : event->actions) {
        const Action* a = library.FindAction(actionId);
        if (!a) {
            out.missing++;
            continue;
        }
        switch (a->Kind()) {
        case ActionPlay: {
            out.plays++;
            const Node* target = library.FindNode(a->target);
            if (!target) {
                out.missing++;
                break;
            }
            // Placing and fades come down from the target's parents too.
            bool threeD = false;
            bool found = false;
            for (const Node* p = target; p && !found; p = p->parent ? library.FindNode(p->parent) : nullptr) {
                if (p->ownPositioning || !p->parent) {
                    threeD = p->ownPositioning && p->threeD;
                    found = true;
                }
                if (p != target)
                    for (const Curve& c : p->curves)
                        if (c.parameter == GameDistance) out.farthest_m = std::max(out.farthest_m, c.LastX());
            }
            visit(*target, threeD, a->delayMs / 1000.0, 0);
            break;
        }
        case ActionStop: out.stops++; break;
        case ActionBreak: out.breaks++; break;
        default: out.others++; break;
        }
    }
    out.longest_s = unknownLength && longest == 0 ? -1.0f : static_cast<float>(longest);
}

void Engine::DescribeNode(const Node& n, int depth, std::string& text)
{
    std::string indent(2 * depth + 2, ' ');
    text += indent + TypeName(n.type) + " " + Hex(n.id);
    if (n.type == TypeSound) {
        Bank* bank = library.FindBank(n.bankId);
        MediumInfo info;
        std::string why;
        if (bank && cache.Info(*bank, n.source, info, why)) {
            const WemFormat& f = info.format;
            text += ": medium " + Hex(n.source) + " (" + (f.IsAdpcm() ? "Wwise ADPCM" : "PCM") + ", " + std::to_string(f.channels) +
                    (f.channels == 1 ? " channel, " : " channels, ") + std::to_string(f.rate) + " Hz, " + Number(info.Seconds(), 2) + " s";
            if (f.hasLoop) text += ", loop " + Number(static_cast<double>(f.loopStart) / f.rate, 2) + "-" + Number(static_cast<double>(f.loopEnd + 1) / f.rate, 2) + " s";
            text += ")";
        } else {
            text += ": medium " + Hex(n.source) + " (" + why + ")";
        }
        text += n.loopCount == 0 ? ", loops for ever" : n.loopCount == 1 ? ", once" : ", " + std::to_string(n.loopCount) + " times";
    } else if (n.type == TypeRanSeq) {
        text += std::string(": ") + (n.sequence ? "sequence" : n.randomMode == 1 ? "random (shuffle)" : "random");
        text += n.continuous ? ", continuous" : ", step";
        if (n.continuous) text += n.containerLoops == 0 ? " for ever" : ", " + std::to_string(n.containerLoops) + " pass(es)";
        if (n.transitionMode == 5) text += ", a child every " + Number(n.transitionMs, 0) + " ms (trigger rate)";
        else if (n.transitionMode) text += ", transition " + std::to_string(n.transitionMode) + " " + Number(n.transitionMs, 0) + " ms";
        if (!n.sequence && n.avoidRepeat) text += ", not the last " + std::to_string(n.avoidRepeat) + " again";
        text += n.global ? ", global" : ", per object";
    } else if (n.type == TypeSwitch) {
        text += ": on " + std::string(n.onState ? "state group " : "switch group ") + Hex(n.group) + ", default " + Hex(n.defaultValue);
        if (n.followsChanges) text += ", follows changes";
    }
    text += "\n";
    // Its settings, where they say something.
    std::string settings;
    if (n.volume) settings += "volume " + Number(n.volume) + " dB; ";
    if (n.pitch) settings += "pitch " + Number(n.pitch, 0) + " cents; ";
    if (n.volumeMin != n.volumeMax) settings += "random volume " + Number(n.volumeMin) + " to " + Number(n.volumeMax) + " dB; ";
    if (n.pitchMin != n.pitchMax) settings += "random pitch " + Number(n.pitchMin, 0) + " to " + Number(n.pitchMax, 0) + " cents; ";
    if (!n.parent || n.ownPriority) settings += "priority " + std::to_string(n.priority) + "; ";
    if (n.mostAtOnce) settings += "at most " + std::to_string(n.mostAtOnce) + " at once (" + (n.refusesWhenFull ? "refuses the newest" : "ends the oldest") + "); ";
    if (!n.parent || n.ownQuiet)
        settings += std::string("too quiet: ") + (n.whenQuiet == WhenQuiet::End ? "ends" : n.whenQuiet == WhenQuiet::Virtual ? "virtual" : "plays on") + "; ";
    if (n.ownPositioning) settings += n.threeD ? "3D at the object; " : "2D; ";
    if (!n.parent) settings += "bus " + Hex(n.bus) + " (" + Number(library.BusVolume(n.bus)) + " dB); ";
    if (!settings.empty()) text += indent + "  " + settings.substr(0, settings.size() - 2) + "\n";
    for (const Curve& c : n.curves) {
        text += indent + "  curve on " + (c.parameter == GameDistance ? std::string("game_distance") : Hex(c.parameter)) + " -> " +
                (c.property == PropertyVolume ? "volume" : c.property == PropertyPitch ? "pitch" : "property " + std::to_string(c.property)) + ":";
        size_t shown = 0;
        for (const CurvePoint& p : c.points) {
            if (shown++ == 8) {
                text += " ...";
                break;
            }
            float y = p.y;
            std::string value;
            if (c.property == PropertyVolume && c.scaling == 2) {
                float gain = c.GainAt(p.x);
                value = gain <= 0 ? "silent" : Number(GainToDb(gain)) + " dB";
            } else {
                value = Number(y, c.property == PropertyPitch ? 0 : 1);
            }
            text += " (" + Number(p.x, 1) + ", " + value + ")";
        }
        text += "\n";
    }
    if (n.type == TypeSwitch) {
        for (const auto& [value, children] : n.choices) {
            text += indent + "  value " + Hex(value) + ":";
            for (uint32_t c : children) text += " " + Hex(c);
            text += children.empty() ? " nothing\n" : "\n";
        }
    }
    if (depth > 12) return;
    size_t shown = 0;
    for (uint32_t child : n.children) {
        if (shown++ == 24) {
            text += indent + "  ... " + std::to_string(n.children.size() - 24) + " more\n";
            break;
        }
        if (const Node* c = library.FindNode(child)) DescribeNode(*c, depth + 1, text);
        else text += indent + "  " + Hex(child) + " (not loaded)\n";
    }
}

std::string Engine::Describe(uint32_t id)
{
    std::string text;
    if (const Event* event = library.FindEvent(id)) {
        text += "event " + Hex(id) + ": " + std::to_string(event->actions.size()) + " action(s)\n";
        for (uint32_t actionId : event->actions) {
            const Action* a = library.FindAction(actionId);
            if (!a) {
                text += "  action " + Hex(actionId) + " (not loaded)\n";
                continue;
            }
            const char* kind = a->Kind() == ActionPlay ? "play" : a->Kind() == ActionStop ? "stop" : a->Kind() == ActionBreak ? "break" : "action";
            text += std::string("  ") + kind + " " + Hex(a->target);
            if (a->Scope() == ScopeEverywhere) text += " on every object";
            if (a->delayMs) text += " after " + std::to_string(a->delayMs) + " ms";
            if (a->fadeMs) text += std::string(a->Kind() == ActionPlay ? ", fading in over " : ", fading out over ") + std::to_string(a->fadeMs) + " ms";
            text += "\n";
            if (a->Kind() == ActionPlay)
                if (const Node* target = library.FindNode(a->target)) DescribeNode(*target, 1, text);
        }
        return text;
    }
    if (const Node* node = library.FindNode(id)) {
        DescribeNode(*node, 0, text);
        return text;
    }
    return "";
}

}  // namespace rfa_audio
