// The player (see engine.h): what an event does, how containers pick and go on, how a voice's level is worked
// out from its nodes, curves, distance and fades, and how voices share OpenAL's sources.
#include "engine.h"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
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

constexpr int RenderPiece = 480;                        // loopback: an update every 10 ms of sound at 48 kHz
constexpr uint32_t FirstPlayingId = 0x80000000u;        // ours never meet Wwise's (the hybrid's step 2)
constexpr double QuietGraceSeconds = 0.25;              // how long a voice too quiet at its start waits for its place

float DbToGain(float db) { return db <= -200 ? 0.0f : std::pow(10.0f, db / 20.0f); }

// Wwise's low-pass value (0 none, 100 the most) as EFX's low-pass gain at 5 kHz: the value read as a cutoff from
// 20 kHz down to 20 Hz (three decades), and a one-pole filter's gain at 5 kHz for that cutoff.
float LowPassGainHF(float value)
{
    float cutoff = 20.0f * std::pow(10.0f, 3.0f * (1.0f - std::clamp(value, 0.0f, 100.0f) / 100.0f));
    float ratio = 5000.0f / cutoff;
    return std::clamp(1.0f / std::sqrt(1.0f + ratio * ratio), 0.0005f, 1.0f);
}
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

// How much level OpenAL Soft's panning loses for a mono sound in each direction, measured on a loopback device of the
// given layout (and HRTF): noise played from each direction of a grid, and the output's power (all channels) against
// the noise's. Wwise pans at a constant power (the channels' powers add up to the sound's, whichever way it comes from);
// OpenAL Soft's stereo pair loses 1.5 dB ahead and 5 dB behind, more with HRTF (tools\research\audio_hrtf_test.cpp).
// False if it can't be measured: then nothing is made up.
bool MeasurePanning(OpenAl& al, ALCint channels, int rate, bool hrtf, PanningLoss& out)
{
    if (!al.alcLoopbackOpenDeviceSOFT || !al.alcRenderSamplesSOFT) return false;
    ALCdevice* device = al.alcLoopbackOpenDeviceSOFT(nullptr);
    if (!device) return false;
    const ALCint attributes[] = {ALC_FORMAT_CHANNELS_SOFT, channels, ALC_FORMAT_TYPE_SOFT, ALC_FLOAT_SOFT, ALC_FREQUENCY, rate,
                                 ALC_HRTF_SOFT, hrtf ? ALC_TRUE : ALC_FALSE, 0};
    ALCcontext* context = al.alcCreateContext(device, attributes);
    if (!context) {
        al.alcCloseDevice(device);
        return false;
    }
    al.alcMakeContextCurrent(context);
    ALCint hrtfOn = 0, outputs = 2;
    al.alcGetIntegerv(device, ALC_HRTF_SOFT, 1, &hrtfOn);
    switch (channels) {
    case ALC_QUAD_SOFT: outputs = 4; break;
    case ALC_5POINT1_SOFT: outputs = 6; break;
    case ALC_6POINT1_SOFT: outputs = 7; break;
    case ALC_7POINT1_SOFT: outputs = 8; break;
    default: outputs = 2; break;
    }
    bool ok = (hrtfOn != 0) == hrtf;
    // White noise, looping, its power known.
    std::vector<int16_t> noise(4096);
    uint32_t seed = 12345;
    double inPower = 0;
    for (auto& v : noise) {
        seed = seed * 1664525u + 1013904223u;
        v = static_cast<int16_t>((static_cast<int32_t>(seed >> 16) - 32768) / 4);
        inPower += (v / 32768.0) * (v / 32768.0);
    }
    inPower /= noise.size();
    ALuint buffer = 0, source = 0;
    al.alGenBuffers(1, &buffer);
    al.alBufferData(buffer, AL_FORMAT_MONO16, noise.data(), static_cast<ALsizei>(noise.size() * 2), rate);
    al.alGenSources(1, &source);
    al.alDistanceModel(AL_NONE);
    al.alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    al.alSourcei(source, AL_LOOPING, AL_TRUE);
    al.alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
    std::vector<float> rendered(static_cast<size_t>(outputs) * 2048);
    // The loss (dB) of the source at a place (or unspatialized in the middle).
    auto loss = [&](float x, float y, float z, bool spatialized) {
        if (al.alIsExtensionPresent("AL_SOFT_source_spatialize")) al.alSourcei(source, AL_SOURCE_SPATIALIZE_SOFT, spatialized ? AL_TRUE : AL_FALSE);
        al.alSource3f(source, AL_POSITION, x, y, z);
        al.alSourcePlay(source);
        al.alcRenderSamplesSOFT(device, rendered.data(), 512);  // settles (HRTF's filters fade between directions)
        al.alcRenderSamplesSOFT(device, rendered.data(), 2048);
        al.alSourceStop(source);
        double power = 0;
        for (size_t i = 0; i < 2048u * outputs; i++) power += static_cast<double>(rendered[i]) * rendered[i];
        power /= 2048.0;
        return power > 0 ? static_cast<float>(-10.0 * std::log10(power / inPower)) : 0.0f;
    };
    if (ok) {
        out.centre = loss(0, 0, 0, false);
        for (int e = 0; e < PanningLoss::Elevations; e++)
            for (int a = 0; a < PanningLoss::Azimuths; a++) {
                double elevation = (-90.0 + 180.0 * e / (PanningLoss::Elevations - 1)) * 3.14159265358979 / 180.0;
                double azimuth = 360.0 * a / PanningLoss::Azimuths * 3.14159265358979 / 180.0;  // 0 ahead, clockwise (right)
                float x = static_cast<float>(std::cos(elevation) * std::sin(azimuth)), y = static_cast<float>(std::sin(elevation)),
                      z = static_cast<float>(-std::cos(elevation) * std::cos(azimuth));
                out.db[e][a] = loss(5 * x, 5 * y, 5 * z, true);
            }
        out.valid = true;
    }
    al.alDeleteSources(1, &source);
    al.alDeleteBuffers(1, &buffer);
    al.alcMakeContextCurrent(nullptr);
    al.alcDestroyContext(context);
    al.alcCloseDevice(device);
    return ok;
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
    // Wwise Vorbis's codebooks, beside OpenAL Soft: without them Armageddon's media don't play (each says why).
    std::string noVorbis;
    LoadVorbisCodebooks((al.folder.empty() ? OwnFolder() : al.folder) + L"packed_codebooks.bin", noVorbis);
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
    // A 2D sound as Wwise plays it: straight to the speakers, not panned (nor through HRTF), where OpenAL Soft can.
    directChannels = al.alIsExtensionPresent("AL_SOFT_direct_channels_remix") ? AL_REMIX_UNMATCHED_SOFT
                   : al.alIsExtensionPresent("AL_SOFT_direct_channels") ? AL_TRUE : 0;
    canUnspatialize = al.alIsExtensionPresent("AL_SOFT_source_spatialize") != AL_FALSE;
    // What OpenAL Soft's panning loses in each direction, against Wwise's constant power: made up for each voice
    // (ApplySource). Measured on a loopback device of this one's layout, with HRTF if this one has it.
    ALCint hrtfOn = 0, mode = 0;
    al.alcGetIntegerv(device, ALC_HRTF_SOFT, 1, &hrtfOn);
    if (al.alcIsExtensionPresent(device, "ALC_SOFT_output_mode")) al.alcGetIntegerv(device, ALC_OUTPUT_MODE_SOFT, 1, &mode);
    ALCint layout = mode == ALC_QUAD_SOFT || mode == ALC_5POINT1_SOFT || mode == ALC_6POINT1_SOFT || mode == ALC_7POINT1_SOFT ? mode : ALC_STEREO_SOFT;
    panning = PanningLoss{};
    MeasurePanning(al, layout, sampleRate, hrtfOn != 0, panning);
    al.alcMakeContextCurrent(context);
    cache.Init(&al, cacheBytes);
    random.seed(loopback ? 12345u : GetTickCount());
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    clockStart = counter.QuadPart;
    clockRate = frequency.QuadPart;
    now = 0;
    renderedFrames = 0;
    pausedAll = false;
    pausedCounter = pausedSince = 0;
    nextPlayingId = FirstPlayingId;
    inlineReads = loopback && !forceWorker;
    if (!inlineReads) worker.Start();
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
    worker.Stop();  // before the banks go: it may be reading their media
    std::lock_guard<std::mutex> hold(lock);
    if (!context) return;
    pausedAll = false;
    for (auto& [id, play] : plays)
        for (auto& root : play->roots) Finish(*root);
    plays.clear();
    pending.clear();
    limited.clear();
    liveVoices.clear();
    ends.clear();
    cache.Clear();
    for (ALuint source : freeSources) al.alDeleteSources(1, &source);
    if (al.HasFilters())
        for (auto& [source, filter] : sourceFilters) al.alDeleteFilters(1, &filter);
    sourceFilters.clear();
    actionValues.clear();
    busActionValues.clear();
    stateChanges.clear();
    ducking.clear();
    busDuckDb.clear();
    externalOn.clear();
    lastMixAt = 0;
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

uint32_t Engine::LoadBankFromArchive(const std::wstring& archive, const std::string& name, std::string& why, const std::wstring& looseBank,
                                     const std::wstring& looseMedia, bool keepLoaded)
{
    if (!context) {
        why = "the engine isn't open";
        return 0;
    }
    // One loaded already (by its header's id) is unloaded first, its sounds stopped; or kept, if asked.
    std::string lower;
    for (char c : name) lower += static_cast<char>(tolower(static_cast<unsigned char>(c)));
    MediaFile file;
    uint64_t at = 0;
    std::string ignored;
    bool open = false;
    if (!looseBank.empty()) {
        open = file.Open(looseBank, ignored);
    } else {
        std::vector<ArchiveFile> found;
        std::vector<bool> present;
        if (FindInArchive(archive, {lower + ".bnk_pc"}, found, present, why) && present[0] && !found[0].compressed) {
            open = file.Open(archive, ignored);
            at = found[0].offset;
        }
    }
    uint8_t head[16];
    if (open && file.ReadAt(at, head, sizeof head) && memcmp(head, "BKHD", 4) == 0) {
        uint32_t id;
        memcpy(&id, head + 12, 4);
        if (keepLoaded && library.FindBank(id)) return id;
        UnloadBank(id);
    }
    Bank* bank = library.LoadFromArchive(archive, name, why, looseBank, looseMedia);
    return bank ? bank->id : 0;
}

void Engine::StopBank(uint32_t bankId, int fadeMs)
{
    // Its sounds playing, as Wwise stops them when the game unloads the bank (the bank itself stays loaded here).
    std::function<void(Instance&)> stop = [&](Instance& in) {
        if (in.finished) return;
        if (in.node && in.node->bankId == bankId) StopInstance(in, fadeMs);
        else
            for (auto& c : in.children) stop(*c);
    };
    for (auto& [id, play] : plays)
        for (auto& root : play->roots) stop(*root);
    // Its events' waiting actions, and waiting plays of its sounds, go.
    for (auto it = pending.begin(); it != pending.end();) {
        const Node* target = it->action->Kind() == ActionPlay ? library.FindNode(it->action->target) : nullptr;
        if (it->action->bankId == bankId || (target && target->bankId == bankId)) {
            if (Play* p = FindPlay(it->playingId)) p->pendingActions--;
            it = pending.erase(it);
        } else {
            ++it;
        }
    }
    Sweep();
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
    worker.ForgetBank(bankId);  // returns once the worker reads none of its media
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

uint32_t Engine::Post(uint32_t eventId, uint64_t object, bool controlsOnly)
{
    const Event* event = library.FindEvent(eventId);
    if (!context || !event) return 0;
    uint32_t id = NewPlay(eventId, object);
    Play& play = *plays[id];
    play.controlsOnly = controlsOnly;
    for (uint32_t actionId : event->actions) {
        const Action* action = library.FindAction(actionId);
        if (!action || (controlsOnly && action->Kind() == ActionPlay)) continue;
        if (action->delayMs > 0) {
            pending.push_back({now + action->delayMs / 1000.0, id, action});
            play.pendingActions++;
        } else {
            Execute(*action, play);
        }
    }
    // Posted while everything is paused: what it started waits, paused, too.
    if (pausedAll)
        for (auto& root : play.roots) PauseVoices(*root, true);
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
    voice->codec = CodecOf(info.format);
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
    voice->threeD = in->ownThreeD;
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
        if (play.controlsOnly) break;  // its plays are another engine's (Post queues none; this guards)
        if (const Node* node = library.FindNode(action.target))
            if (auto in = Start(node, play, nullptr, action.fadeMs)) play.roots.push_back(std::move(in));
        break;
    case ActionStop:
        if (action.Scope() == ScopeAll || action.Scope() == ScopeAllBut) {
            // Every play but this one (and, for "all but", those that play the target).
            std::vector<uint32_t> ids;
            for (auto& [id, p] : plays) {
                if (p.get() == &play) continue;
                bool spared = false;
                if (action.Scope() == ScopeAllBut)
                    for (auto& root : p->roots)
                        if (root->node && Matches(root->node, action.target)) spared = true;
                if (!spared) ids.push_back(id);
            }
            for (uint32_t id : ids) StopPlaying(id, action.fadeMs);
        } else {
            StopMatching(action.target, play.object, everywhere, action.fadeMs);
        }
        break;
    case ActionBreak:
        BreakMatching(action.target, play.object, everywhere);
        break;
    case ActionSetState:
        SetState(action.group, action.groupValue);
        break;
    case ActionSetSwitch:
        Object(play.object).switches[action.group] = action.groupValue;
        break;
    case ActionPostEvent:
        // Another event posted on the same object (Armageddon's music banks do it), with a guard against a loop.
        if (postDepth < 8 && library.FindEvent(action.target)) {
            postDepth++;
            Post(action.target, play.object, play.controlsOnly);
            postDepth--;
        }
        break;
    case ActionSetVolume: case ActionResetVolume: case ActionMute: case ActionUnmute:
    case ActionSetPitch: case ActionResetPitch: case ActionSetLowPass: case ActionResetLowPass: {
        // On a node (a bus is the mix's, step 3), for the event's object or every one; "all" resets every node's.
        const uint32_t kind = action.Kind();
        auto apply = [&](ActionValues& v) {
            switch (kind) {
            case ActionSetVolume: v.volume = action.value; break;
            case ActionMute: v.muted = true; break;
            case ActionUnmute: v.muted = false; break;
            case ActionResetVolume: v.volume = 0; break;
            case ActionSetPitch: v.pitch = action.value; break;
            case ActionResetPitch: v.pitch = 0; break;
            case ActionSetLowPass: v.lowPass = action.value; break;
            case ActionResetLowPass: v.lowPass = 0; break;
            }
        };
        if (action.Scope() == ScopeAll || action.Scope() == ScopeAllBut) {
            if (kind == ActionResetVolume || kind == ActionUnmute || kind == ActionResetPitch || kind == ActionResetLowPass) {
                for (auto& [key, v] : actionValues)
                    if (action.Scope() == ScopeAll || key.first != action.target) apply(v);
                for (auto& [bus, v] : busActionValues)
                    if (action.Scope() == ScopeAll || bus != action.target) apply(v);
            }
        } else if (library.FindNode(action.target)) {
            apply(actionValues[ActionKey(action.target, everywhere ? 0 : play.object)]);
        } else if (library.FindBus(action.target)) {
            apply(busActionValues[action.target]);  // a bus: for every sound through it
        }
        for (auto it = actionValues.begin(); it != actionValues.end();) it = it->second.Empty() ? actionValues.erase(it) : std::next(it);
        for (auto it = busActionValues.begin(); it != busActionValues.end();) it = it->second.Empty() ? busActionValues.erase(it) : std::next(it);
        break;
    }
    case ActionPause: case ActionResume: {
        // The plays that play the node (on the event's object, or everywhere), or every play for "all".
        bool all = action.Scope() == ScopeAll || action.Scope() == ScopeAllBut;
        if (!all && !library.FindNode(action.target) && !library.FindBus(action.target)) break;
        std::vector<uint32_t> ids;
        for (auto& [id, p] : plays) {
            if (p.get() == &play || (!all && !everywhere && p->object != play.object)) continue;
            bool matches = all;
            for (auto& root : p->roots)
                if (root->node && Matches(root->node, action.target)) matches = action.Scope() != ScopeAllBut;
            if (matches) ids.push_back(id);
        }
        for (uint32_t id : ids) PausePlay(id, action.Kind() == ActionPause);
        break;
    }
    default:
        break;  // the rest (bus-wide ones among them) come with the mix, step 3
    }
}

void Engine::RunPending()
{
    if (pausedAll) return;  // the time stands still: nothing waiting is due
    for (size_t i = 0; i < pending.size();) {
        if (pending[i].at > now) {
            i++;
            continue;
        }
        if (Play* waiting = FindPlay(pending[i].playingId); waiting && waiting->paused) {
            i++;  // its time stands still: the action waits on (ShiftTimes moves it on resuming)
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
    else if (v.feed) v.feed->stopLooping = true;  // the pieces decoded ahead still play (up to about 0.7 s)
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
        v.load.reset();  // a read under way finishes for nothing
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
bool Engine::StartNextPick(Instance& in, int fadeInMs)
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
    return child && StartChild(in, child, fadeInMs);
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
    if (!bank) return false;
    // A source plug-in has no medium. Wwise's silence is a voice with no sound for its length (and a random part);
    // the others (tone, synthesis: a few of Armageddon's) aren't played.
    if (n.IsSourcePlugin()) {
        if (!n.IsSilence()) return false;
        auto silence = std::make_unique<Voice>();
        Voice& s = *silence;
        s.owner = &in;
        s.silence = true;
        s.virtualVoice = true;
        s.loopCount = n.loopCount < 0 ? 1 : n.loopCount;
        float more = n.silenceLess != n.silenceMore
                         ? std::uniform_real_distribution<float>(std::min(n.silenceLess, n.silenceMore), std::max(n.silenceLess, n.silenceMore))(random)
                         : 0.0f;
        s.length = std::max(0.0f, n.silenceSeconds + more);
        s.startedAt = now;
        in.voice = std::move(silence);
        liveVoices.push_back(&s);
        in.play->voicesStarted++;
        return true;
    }
    auto voice = std::make_unique<Voice>();
    Voice& v = *voice;
    v.owner = &in;
    v.mediumId = n.source;
    v.loopCount = n.loopCount < 0 ? 1 : n.loopCount;
    std::string why;
    if (inlineReads) {
        // The check: read now, in step with the time rendered.
        if (!cache.Info(*bank, n.source, v.medium, why)) {
            mediaFailed++;
            return false;
        }
    } else {
        // Its format not read yet, or decoded whole and not in the cache: the worker reads it, and the voice waits
        // (counted as started, with no source) until it has (FinishLoad).
        bool known = cache.KnownInfo(*bank, n.source, v.medium);
        if (!known || (IsWhole(v.medium.format, v.loopCount) && !cache.Find(v.medium))) {
            auto load = std::make_shared<MediumLoad>();
            load->bank = bank;
            load->mediumId = n.source;
            load->loopCount = v.loopCount;
            load->knownInfo = known;
            if (known) load->info = v.medium;
            v.load = load;
            v.startedAt = now;
            in.voice = std::move(voice);
            liveVoices.push_back(&v);
            in.play->voicesStarted++;
            worker.Load(std::move(load));
            return true;
        }
    }
    in.voice = std::move(voice);
    return BeginVoice(in, false);
}

// A medium the worker has read: kept, and the voice that waited for it starts (or, virtual, is heard again).
void Engine::FinishLoad(Instance& in)
{
    Voice& v = *in.voice;
    std::shared_ptr<MediumLoad> load = std::move(v.load);
    if (load->state.load() < 0) {
        mediaFailed++;
        v.ended = true;
        return;
    }
    cache.PutInfo(load->info);
    v.medium = load->info;
    std::string why;
    if (load->whole && IsWhole(v.medium.format, v.loopCount)) {
        v.decoded = cache.Put(load->info, load->samples, why);
        if (!v.decoded) {
            mediaFailed++;
            v.ended = true;
            return;
        }
    }
    if (!v.loadToResume) {
        if (!BeginVoice(in, true)) v.ended = true;
        return;
    }
    v.loadToResume = false;
    GameObject& object = Object(in.play->object);
    Level level = LevelOf(in, object);
    if (level.db >= threshold && BeginOnSource(v)) {
        ApplySource(v, level, object);
        al.alSourcePlay(v.source);
    }
}

// The rest of a voice's start, its medium's format known (and, decoded whole, in the cache unless it is read now).
// counted: it is already among the live voices and its play's voices (it waited for the worker).
bool Engine::BeginVoice(Instance& in, bool counted)
{
    Voice& v = *in.voice;
    std::string why;
    v.channels = v.medium.format.channels;
    v.rate = v.medium.format.rate;
    v.codec = CodecOf(v.medium.format);
    v.length = v.medium.Seconds();
    GameObject& object = Object(in.play->object);
    Level level = LevelOf(in, object);
    v.threeD = level.threeD;
    v.priority = level.priority;
    v.whenQuiet = level.whenQuiet;
    v.bus = level.bus;
    v.distance = level.distance;
    v.levelDb = level.db + GainToDb(level.fadeGain);
    v.startedAt = now;
    auto count = [&] {
        if (counted) return;
        liveVoices.push_back(&v);
        in.play->voicesStarted++;
    };
    if (level.db < threshold) {
        if (level.whenQuiet == WhenQuiet::End) {
            // Not ended yet: its object's place may not have come. The game places a sound object at its next update
            // (every 33 ms, a person's only while something plays on it) and Wwise starts a voice at its next render,
            // after that; here a voice starts at once, from the place the object last had. It waits a moment, virtual,
            // making no sound and ducking nothing, then ends if it is still too quiet.
            v.virtualVoice = true;
            v.quietGraceUntil = now + QuietGraceSeconds;
            count();
            return true;
        }
        if (level.whenQuiet == WhenQuiet::Virtual) {
            v.virtualVoice = true;
            count();
            return true;
        }
    }
    if (!LimitBuses(v)) return false;  // its bus is full (Armageddon's banks)
    if (IsWhole(v.medium.format, v.loopCount) && !v.decoded) {
        v.decoded = cache.Get(v.medium, why);  // in the cache (or a loopback device's, read now)
        if (!v.decoded) {
            mediaFailed++;
            return false;
        }
    }
    if (!BeginOnSource(v)) {
        noSource++;
        if (v.whenQuiet != WhenQuiet::Virtual && v.loopCount != 0) return false;
        v.virtualVoice = true;  // no source free: it waits, virtual, for one (a loop too: it mustn't be lost)
        count();
        return true;
    }
    count();
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
    // None free: the quietest voice (of those as quiet, the lowest priority) gives its up, if it is at least 6 dB quieter
    // than this one, or of a lower priority and no louder. Wwise mixes every voice, with no such cap, and its priorities
    // only choose within its voice limits: here, a world full of far loops (our ambience has priority 78, the voices'
    // lines 50) mustn't keep a near sound from being heard. The margin keeps two voices of about one level from taking
    // turns.
    Voice* victim = nullptr;
    for (Voice* v : liveVoices)
        if (v->source && v != &voice && (!victim || v->levelDb < victim->levelDb - 0.5f ||
                                          (v->levelDb <= victim->levelDb + 0.5f && v->priority < victim->priority)))
            victim = v;
    if (!victim || !(victim->levelDb + 6.0f < voice.levelDb || (victim->priority < voice.priority && victim->levelDb <= voice.levelDb)))
        return false;
    stolen++;
    ReleaseSource(*victim);
    // It goes on in silence, keeping its time, to come back when a source is free; or ends, if it would end when too
    // quiet to hear anyway and isn't a loop.
    if (victim->whenQuiet == WhenQuiet::Virtual || victim->loopCount == 0) victim->virtualVoice = true;
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
    v.spare.clear();
    v.stream.reset();
    if (v.feed) {
        v.feed->cancelled = true;  // the worker drops it
        v.feed.reset();
    }
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
    // 2D: a stereo sound's channels go straight to the speakers' (HRTF on, OpenAL Soft would place them at ±30° and
    // filter them: 2.5 dB quieter than Wwise), a mono one isn't spatialized. 3D: placed (and through HRTF if it is on).
    if (directChannels) al.alSourcei(source, AL_DIRECT_CHANNELS_SOFT, !v.threeD && v.channels > 1 ? directChannels : AL_FALSE);
    if (canUnspatialize) al.alSourcei(source, AL_SOURCE_SPATIALIZE_SOFT, v.threeD ? AL_TRUE : AL_FALSE);
    if (v.decoded) {
        al.alSourcei(source, AL_BUFFER, static_cast<ALint>(v.decoded->buffer));
        al.alSourcei(source, AL_LOOPING, v.loopCount == 0 ? AL_TRUE : AL_FALSE);
    } else {
        al.alSourcei(source, AL_LOOPING, AL_FALSE);
        al.alSourcei(source, AL_BUFFER, 0);
        std::string why;
        v.framesQueued = 0;
        if (inlineReads) {
            v.stream = std::make_unique<Stream>();
            if (!v.stream->Open(v.medium, v.loopCount, why)) {
                ReleaseSource(v);
                return false;
            }
        } else {
            // The worker decodes it a few pieces ahead; until the first is ready the source waits with none queued
            // (OpenAL leaves it stopped) and ServiceStream starts it.
            auto feed = std::make_shared<StreamFeed>();
            if (!feed->stream.Open(v.medium, v.loopCount, why)) {
                ReleaseSource(v);
                return false;
            }
            v.feed = feed;
            worker.Feed(std::move(feed));
        }
        al.alGenBuffers(4, v.buffers);
        v.spare.assign(std::begin(v.buffers), std::end(v.buffers));
        QueueReady(v);
        if (inlineReads && v.spare.size() == 4) {
            ReleaseSource(v);
            return false;
        }
        v.streamStarted = v.spare.size() < 4;
    }
    v.virtualVoice = false;
    v.quietGraceUntil = 0;
    v.everPlayed = true;
    v.startedAt = now;
    return true;
}

bool Engine::FillStream(Voice& v, ALuint buffer)
{
    int frames = 0;
    if (v.feed) {
        if (!v.feed->Take(streamScratch)) return false;
        worker.Wake();
        frames = v.channels > 0 ? static_cast<int>(streamScratch.size() / v.channels) : 0;
    } else {
        if (!v.stream || v.stream->AtEnd()) return false;
        streamScratch.resize(static_cast<size_t>(StreamBufferFrames) * v.channels);
        frames = v.stream->Read(streamScratch.data(), StreamBufferFrames);
    }
    if (frames <= 0) return false;
    al.alBufferData(buffer, Format16(al, v.channels), streamScratch.data(), frames * v.channels * 2, v.rate);
    v.framesQueued += frames;
    return true;
}

// A stream's spare buffers filled with what is ready and queued.
void Engine::QueueReady(Voice& v)
{
    while (!v.spare.empty() && FillStream(v, v.spare.back())) {
        ALuint buffer = v.spare.back();
        al.alSourceQueueBuffers(v.source, 1, &buffer);
        v.spare.pop_back();
    }
}

void Engine::ServiceStream(Voice& v)
{
    ALint processed = 0;
    al.alGetSourcei(v.source, AL_BUFFERS_PROCESSED, &processed);
    while (processed-- > 0) {
        ALuint buffer = 0;
        al.alSourceUnqueueBuffers(v.source, 1, &buffer);
        if (buffer) v.spare.push_back(buffer);
    }
    QueueReady(v);
    ALint state = 0, queued = 0;
    al.alGetSourcei(v.source, AL_SOURCE_STATE, &state);
    al.alGetSourcei(v.source, AL_BUFFERS_QUEUED, &queued);
    if (state == AL_PAUSED) return;
    if (state != AL_PLAYING) {
        if (queued > 0) {
            if (v.streamStarted) underruns++;  // it ran dry before it was refilled: on again
            v.streamStarted = true;
            al.alSourcePlay(v.source);
        } else if (!v.feed || v.feed->Finished()) {
            v.ended = true;  // its last piece has played
            if ((v.feed && v.feed->Failed()) || (v.stream && v.stream->failed)) mediaFailed++;
        }
        // else its first pieces (or the next, after running dry) are still being read: it starts when they come
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

// The loss for a place relative to the listener (OpenAL's: x right, y up, -z ahead), between the grid's directions.
float PanningLoss::At(const Vec3& p) const
{
    float horizontal = std::sqrt(p.x * p.x + p.z * p.z);
    if (horizontal < 1e-6f && std::abs(p.y) < 1e-6f) return centre;
    float azimuth = std::atan2(p.x, -p.z) * 180.0f / 3.14159265f;  // 0 ahead, 90 right
    if (azimuth < 0) azimuth += 360.0f;
    float elevation = std::atan2(p.y, horizontal) * 180.0f / 3.14159265f;
    float a = azimuth / 360.0f * Azimuths, e = (elevation + 90.0f) / 180.0f * (Elevations - 1);
    int a0 = static_cast<int>(a) % Azimuths, a1 = (a0 + 1) % Azimuths, e0 = std::clamp(static_cast<int>(e), 0, Elevations - 1),
        e1 = std::min(e0 + 1, Elevations - 1);
    float ta = a - std::floor(a), te = std::clamp(e - e0, 0.0f, 1.0f);
    float low = db[e0][a0] + (db[e0][a1] - db[e0][a0]) * ta, high = db[e1][a0] + (db[e1][a1] - db[e1][a0]) * ta;
    return low + (high - low) * te;
}

void Engine::ApplySource(Voice& v, const Level& level, const GameObject& object)
{
    if (!v.source) return;
    const bool centred = !(level.threeD && v.channels == 1 && object.placed);  // played at the listener: in the middle
    // What OpenAL Soft's panning loses for a mono voice, where it is (MeasurePanning), made up as Wwise pans.
    const Vec3 place = centred ? Vec3{} : ToListener(object.position);
    float makeUp = v.channels != 1 || !panning.valid ? 0.0f : centred ? panning.centre : panning.At(place);
    float gain = DbToGain(level.db + std::clamp(makeUp, 0.0f, 12.0f)) * level.fadeGain;
    al.alSourcef(v.source, AL_GAIN, std::min(gain, 16.0f));
    al.alSourcef(v.source, AL_PITCH, std::clamp(std::pow(2.0f, level.pitchCents / 1200.0f), 0.05f, 8.0f));
    if (!centred) {
        al.alSource3f(v.source, AL_POSITION, place.x, place.y, place.z);
    } else {
        al.alSource3f(v.source, AL_POSITION, 0.0f, 0.0f, 0.0f);
    }
    // Wwise's low-pass (0-100) as an EFX low-pass on the source, set again only when it moves.
    if (al.HasFilters() && std::abs(level.lowPass - v.appliedLowPass) > 0.5f) {
        if (level.lowPass < 0.5f) {
            al.alSourcei(v.source, AL_DIRECT_FILTER, AL_FILTER_NULL);
        } else {
            ALuint& filter = sourceFilters[v.source];
            if (!filter) {
                al.alGenFilters(1, &filter);
                al.alFilteri(filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
            }
            al.alFilterf(filter, AL_LOWPASS_GAIN, 1.0f);
            al.alFilterf(filter, AL_LOWPASS_GAINHF, LowPassGainHF(level.lowPass));
            al.alSourcei(v.source, AL_DIRECT_FILTER, static_cast<ALint>(filter));  // takes the filter's values now
        }
        v.appliedLowPass = level.lowPass;
    }
}

void Engine::LogQuiet(const Instance& in, const GameObject& object, float levelDb)
{
    if (!logCallback || quietLogsLeft <= 0 || !in.play || !quietLogged.insert(in.play->eventId).second) return;
    quietLogsLeft--;
    const float distance = object.placed ? Length(Minus(object.position, listenerPosition)) : 0.0f;
    std::string text = "Ended too quiet: event " + Hex(in.play->eventId) + ", sound " + (in.node ? Hex(in.node->id) : std::string("?")) + ", " +
                       Number(levelDb) + " dB (below " + Number(threshold) + "), " + Number(distance) + " m from the listener";
    text += object.placed ? " (its object at " + Number(object.position.x) + ", " + Number(object.position.y) + ", " + Number(object.position.z) + ")"
                          : " (its object never placed)";
    text += "; the listener at " + Number(listenerPosition.x) + ", " + Number(listenerPosition.y) + ", " + Number(listenerPosition.z) + ". Nodes:";
    uint32_t bus = 0;
    for (const Node* n = in.node; n; n = n->parent ? library.FindNode(n->parent) : nullptr) {
        text += " " + Hex(n->id) + " " + Number(n->volume);
        for (const Curve& c : n->curves)
            if (c.property == PropertyVolume) {
                float x = ParameterValue(object, c.parameter, distance);
                text += " [" + Hex(c.parameter) + " at " + Number(x, 2) + ": " + Number(GainToDb(c.GainAt(x))) + "]";
            }
        if (n->stateGroup) text += " (state group " + Hex(n->stateGroup) + ")";
        for (uint64_t on : {object.id, uint64_t(0)})
            if (auto set = actionValues.find(ActionKey(n->id, on)); set != actionValues.end())
                text += " (set " + Number(set->second.volume) + (set->second.muted ? ", muted" : "") + ")";
        if (!n->parent) bus = n->bus;
    }
    text += ". Buses:";
    for (uint32_t id : BusChain(bus)) {
        const Bus* b = library.FindBus(id);
        if (!b) {
            text += " " + Hex(id) + " (not loaded)";
            continue;
        }
        text += " " + Hex(id) + " " + Number(b->volume);
        for (const Curve& c : b->curves)
            if (c.property == PropertyVolume) {
                float x = GlobalParameter(c.parameter);
                text += " [" + Hex(c.parameter) + " at " + Number(x, 2) + ": " + Number(GainToDb(c.GainAt(x))) + "]";
            }
        if (b->stateGroup)
            if (auto state = states.find(b->stateGroup); state != states.end())
                text += " (state " + Hex(state->second) + ": " + Number(StateValuesFor(b->stateGroup, state->second, b).volume) + ")";
        if (auto set = busActionValues.find(id); set != busActionValues.end())
            text += " (set " + Number(set->second.volume) + (set->second.muted ? ", muted" : "") + ")";
        if (auto duck = busDuckDb.find(id); duck != busDuckDb.end()) text += " (ducked " + Number(duck->second) + ")";
    }
    logCallback(text.c_str(), logUser);
}

float Engine::ParameterValue(const GameObject& object, uint32_t parameter, float distance) const
{
    if (auto it = object.parameters.find(parameter); it != object.parameters.end()) return it->second;
    if (parameter == GameDistance) return distance;
    if (auto it = globalParameters.find(parameter); it != globalParameters.end()) return it->second;
    return library.ParameterDefault(parameter);  // init.bnk's default (0 if it gives none)
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
    const Node* priorityNode = nullptr;
    const Attenuation* attenuation = nullptr;
    for (int depth = 0; n && depth < 64; depth++) {
        level.db += n->volume;
        level.pitchCents += n->pitch;
        level.lowPass += n->lowPass;
        for (const Curve& c : n->curves) {
            float x = ParameterValue(object, c.parameter, level.distance);
            if (c.property == PropertyVolume) curveGain *= c.GainAt(x);
            else if (c.property == PropertyPitch) level.pitchCents += c.ValueAt(x);
            else if (c.property == PropertyLowPass) level.lowPass += c.ValueAt(x);
        }
        // A state group it follows (init.bnk's states: RF4_STATES and the like): its state's values.
        if (n->stateGroup) {
            if (const StateGroup* g = library.FindStateGroup(n->stateGroup)) {
                auto state = states.find(n->stateGroup);
                if (state != states.end())
                    if (auto v = g->states.find(state->second); v != g->states.end()) {
                        level.db += v->second.volume;
                        level.pitchCents += v->second.pitch;
                        level.lowPass += v->second.lowPass;
                    }
            }
        }
        // Set by an action (set volume, pitch or low-pass; mute) on this node, for this object or every one.
        for (uint64_t on : {object.id, uint64_t(0)})
            if (auto set = actionValues.find(ActionKey(n->id, on)); set != actionValues.end()) {
                level.db += set->second.volume;
                level.pitchCents += set->second.pitch;
                level.lowPass += set->second.lowPass;
                if (set->second.muted) level.fadeGain = 0;
            }
        bool top = n->parent == 0;
        if (!placed && (n->ownPositioning || top)) {
            level.threeD = n->ownPositioning && n->threeD;
            if (level.threeD) attenuation = library.FindAttenuation(n->attenuation);
            placed = true;
        }
        if (!prioritised && (n->ownPriority || top)) {
            level.priority = n->priority;
            priorityNode = n;
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
    // Its attenuation: volume and low-pass by the distance (Armageddon's; ours are flat).
    if (attenuation) {
        if (attenuation->volumeDry >= 0) curveGain *= attenuation->curves[attenuation->volumeDry].GainAt(level.distance);
        if (attenuation->lowPass >= 0) level.lowPass += attenuation->curves[attenuation->lowPass].ValueAt(level.distance);
        // Priority by distance: offset in full at the attenuation's end, in part nearer.
        if (priorityNode && priorityNode->priorityByDistance && attenuation->maxDistance > 0) {
            float part = std::min(1.0f, level.distance / attenuation->maxDistance);
            level.priority = std::clamp(level.priority + static_cast<int>(std::lround(priorityNode->distanceOffset * part)), 0, 100);
        }
    }
    level.db += GainToDb(curveGain);
    BusLevel(level.bus, level);
    level.lowPass = std::clamp(level.lowPass, 0.0f, 100.0f);
    return level;
}

void Engine::UpdateVoice(Instance& in, GameObject& object)
{
    Voice& v = *in.voice;
    if (v.ended) return;
    if (v.load) {
        if (v.load->state.load() == 0) return;  // still being read
        FinishLoad(in);
        if (v.ended) ReleaseSource(v);
        return;
    }
    if (v.silence) {
        if (v.loopCount != 0 && now - v.startedAt >= v.length * std::max(1, v.loopCount)) v.ended = true;
        return;
    }
    Level level = LevelOf(in, object);
    v.levelDb = level.db + GainToDb(level.fadeGain);
    v.distance = level.distance;
    v.priority = level.priority;
    bool quiet = level.db < threshold;
    if (v.source) {
        if (quiet && v.whenQuiet == WhenQuiet::End) {
            endedQuiet++;
            LogQuiet(in, object, v.levelDb);
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
        if (v.quietGraceUntil > 0 && quiet && now >= v.quietGraceUntil) {
            endedQuiet++;  // still too quiet once its place has come
            LogQuiet(in, object, v.levelDb);
            v.ended = true;
            return;
        }
        float pitch = std::clamp(std::pow(2.0f, level.pitchCents / 1200.0f), 0.05f, 8.0f);
        if (v.loopCount != 0 && now - v.startedAt >= v.length * std::max(1, v.loopCount) / pitch) {
            v.ended = true;
            return;
        }
        if (!quiet) {
            if (!v.decoded && IsWhole(v.medium.format, v.loopCount)) {
                if (!inlineReads && !(v.decoded = cache.Find(v.medium))) {
                    // Heard again, and no longer in the cache: the worker reads it, and it goes on virtual until then.
                    auto load = std::make_shared<MediumLoad>();
                    load->bank = v.medium.bank;
                    load->mediumId = v.mediumId;
                    load->loopCount = v.loopCount;
                    load->knownInfo = true;
                    load->info = v.medium;
                    v.load = load;
                    v.loadToResume = true;
                    worker.Load(std::move(load));
                    return;
                }
                std::string why;
                if (!v.decoded) v.decoded = cache.Get(v.medium, why);
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
    if (pausedAll || (in.play && in.play->paused)) {
        UpdatePaused(in);
        return;
    }
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
            } else if (n->transitionMode == 3 && n->transitionMs > 0) {
                // A delay: the next starts the transition's time (and its random part) after the last has ended.
                bool playing = std::any_of(in.children.begin(), in.children.end(), [](const auto& c) { return !c->finished; });
                if (playing) {
                    in.nextAt = 0;
                } else if (in.nextAt <= 0) {
                    float wait = n->transitionMs;
                    if (n->transitionRandomMin != 0 || n->transitionRandomMax != 0)
                        wait += std::uniform_real_distribution<float>(std::min(n->transitionRandomMin, n->transitionRandomMax),
                                                                      std::max(n->transitionRandomMin, n->transitionRandomMax))(random);
                    in.nextAt = now + std::max(0.0f, wait) / 1000.0;
                } else if (now >= in.nextAt) {
                    in.nextAt = 0;
                    StartNextPick(in);
                }
            } else if ((n->transitionMode == 1 || n->transitionMode == 2) && n->transitionMs > 0) {
                // A crossfade: the next starts the transition's time before the newest one's sound ends, fading in as
                // that one fades out (a container child's end isn't known ahead: it follows on as one after another).
                Instance* newest = nullptr;
                for (auto& c : in.children)
                    if (!c->finished && !c->stopping) newest = c.get();
                if (!newest) {
                    if (std::none_of(in.children.begin(), in.children.end(), [](const auto& c) { return !c->finished; })) StartNextPick(in);
                } else if (newest->voice && !newest->voice->load && newest->voice->length > 0 && newest->voice->loopCount == 1) {
                    const Voice& v = *newest->voice;
                    double left = v.length - (now - v.startedAt), overlap = n->transitionMs / 1000.0;
                    if (left <= overlap) {
                        StopInstance(*newest, static_cast<int>(std::max(0.0, left) * 1000));
                        StartNextPick(in, static_cast<int>(std::min(left, overlap) * 1000));
                    }
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

// ---- Pausing ---------------------------------------------------------------------------------------

// While paused only a stop that is due goes on (one with no fade, or whose fade has run out): nothing starts, and
// fades, waits and containers stand still. Sources stay paused.
void Engine::UpdatePaused(Instance& in)
{
    if (in.finished) return;
    if (in.stopping && now >= in.fade.end) {
        Finish(in);
        return;
    }
    for (auto& c : in.children) UpdatePaused(*c);
    in.children.erase(std::remove_if(in.children.begin(), in.children.end(), [](const auto& c) { return c->finished; }), in.children.end());
    if (in.node && in.node->type != TypeSound && in.children.empty() && in.stopping) Finish(in);
}

// Pauses (or plays again) the sources of what an instance plays. Played again only if nothing still pauses it:
// neither the pause of everything nor its own play's.
void Engine::PauseVoices(Instance& in, bool pause)
{
    for (auto& c : in.children) PauseVoices(*c, pause);
    if (!in.voice || !in.voice->source) return;
    ALint state = 0;
    al.alGetSourcei(in.voice->source, AL_SOURCE_STATE, &state);
    if (pause && state == AL_PLAYING) al.alSourcePause(in.voice->source);
    else if (!pause && state == AL_PAUSED && !pausedAll && !(in.play && in.play->paused)) al.alSourcePlay(in.voice->source);
}

// Moves an instance's times on by `seconds` (it was paused that long): its fade, its next pick, its voice's start.
void Engine::ShiftTimes(Instance& in, double seconds)
{
    in.fade.start += seconds;
    in.fade.end += seconds;
    in.startedAt += seconds;
    if (in.nextAt > 0) in.nextAt += seconds;
    if (in.voice) in.voice->startedAt += seconds;
    for (auto& c : in.children) ShiftTimes(*c, seconds);
}

void Engine::PauseAll(bool pause)
{
    if (!context || pause == pausedAll) return;
    if (!loopback) {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        if (pause) {
            ClockNow();
            pausedSince = counter.QuadPart;
        } else {
            pausedCounter += counter.QuadPart - pausedSince;
        }
    }
    pausedAll = pause;
    ClockNow();
    for (auto& [id, play] : plays)
        for (auto& root : play->roots) PauseVoices(*root, pause);
}

void Engine::PausePlay(uint32_t playingId, bool pause)
{
    Play* play = FindPlay(playingId);
    if (!context || !play || play->paused == pause) return;
    if (pause) {
        play->paused = true;
        play->pausedAt = now;
    } else {
        double seconds = now - play->pausedAt;
        play->paused = false;
        for (auto& root : play->roots) ShiftTimes(*root, seconds);
        for (PendingAction& p : pending)
            if (p.playingId == playingId) p.at += seconds;
    }
    for (auto& root : play->roots) PauseVoices(*root, pause);
}

// The engine's time: the wall clock less the time everything was paused, standing still while it is (a loopback
// device's time is the time rendered).
void Engine::ClockNow()
{
    if (loopback) return;
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    int64_t at = pausedAll ? pausedSince : counter.QuadPart;
    now = static_cast<double>(at - clockStart - pausedCounter) / static_cast<double>(clockRate);
}

// ---- Updating --------------------------------------------------------------------------------------

bool Engine::Waiting()
{
    for (Voice* v : liveVoices) {
        if (v->ended) continue;
        if (v->load) return true;
        if (v->feed && v->source && !v->feed->Finished()) {
            ALint queued = 0;
            al.alGetSourcei(v->source, AL_BUFFERS_QUEUED, &queued);
            ALint processed = 0;
            al.alGetSourcei(v->source, AL_BUFFERS_PROCESSED, &processed);
            if (queued - processed < 2) return true;
        }
    }
    return false;
}

void Engine::Update()
{
    if (!context) return;
    ClockNow();
    RunPending();
    UpdateMix();
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

// A state set: the buses that follow its group move to the new state's values over the group's transition time (one
// given for this change, or its default).
void Engine::SetState(uint32_t group, uint32_t state)
{
    uint32_t from = states.count(group) ? states[group] : 0;
    states[group] = state;
    if (from == state) return;
    double seconds = 0;
    if (const StateGroup* g = library.FindStateGroup(group)) {
        seconds = g->defaultTransitionMs / 1000.0;
        for (const auto& t : g->transitions)
            if ((t.from == from || t.from == 0) && (t.to == state || t.to == 0)) seconds = t.ms / 1000.0;
    }
    StateChange& c = stateChanges[group];
    c.from = from;
    c.to = state;
    c.at = now;
    c.seconds = seconds;
}

void Engine::NoteBus(uint32_t bus, int delta)
{
    int& n = externalOn[bus];
    n = std::max(0, n + delta);
    if (n == 0) externalOn.erase(bus);
}

// ---- The mix -------------------------------------------------------------------------------------------

// A bus and its parents, up to the master bus.
std::vector<uint32_t> Engine::BusChain(uint32_t bus) const
{
    std::vector<uint32_t> chain;
    for (int depth = 0; bus && depth < 32; depth++) {
        chain.push_back(bus);
        const Bus* b = library.FindBus(bus);
        bus = b ? b->parent : 0;
    }
    return chain;
}

float Engine::GlobalParameter(uint32_t parameter) const
{
    if (auto it = globalParameters.find(parameter); it != globalParameters.end()) return it->second;
    return library.ParameterDefault(parameter);
}

// What a state does to a bus that follows its group: the state object the bus names for it, or the group's own values.
StateValues Engine::StateValuesFor(uint32_t group, uint32_t state, const Bus* bus) const
{
    if (bus)
        if (auto it = bus->stateObjects.find(state); it != bus->stateObjects.end() && it->second)
            if (const StateValues* v = library.FindStateObject(it->second)) return *v;
    if (const StateGroup* g = library.FindStateGroup(group))
        if (auto it = g->states.find(state); it != g->states.end()) return it->second;
    return StateValues{};
}

// The bus a voice goes to, and its parents: their volumes, pitch and low-pass, their curves on game parameters (the
// volume sliders: global ones), their state group's state (moving between states over its transition), what actions
// set on them (a mute silences), and their ducking now.
void Engine::BusLevel(uint32_t busId, Level& level) const
{
    for (uint32_t id : BusChain(busId)) {
        const Bus* b = library.FindBus(id);
        if (!b) continue;
        level.db += b->volume;
        level.pitchCents += b->pitch;
        level.lowPass += b->lowPass;
        float gain = 1.0f;
        for (const Curve& c : b->curves) {
            float x = GlobalParameter(c.parameter);
            if (c.property == PropertyVolume) gain *= c.GainAt(x);
            else if (c.property == PropertyPitch) level.pitchCents += c.ValueAt(x);
            else if (c.property == PropertyLowPass) level.lowPass += c.ValueAt(x);
        }
        level.db += GainToDb(gain);
        if (b->stateGroup) {
            auto state = states.find(b->stateGroup);
            if (state != states.end()) {
                StateValues to = StateValuesFor(b->stateGroup, state->second, b), v = to;
                auto change = stateChanges.find(b->stateGroup);
                if (change != stateChanges.end() && change->second.seconds > 0 && now < change->second.at + change->second.seconds) {
                    StateValues from = StateValuesFor(b->stateGroup, change->second.from, b);
                    float t = static_cast<float>((now - change->second.at) / change->second.seconds);
                    v.volume = from.volume + (to.volume - from.volume) * t;
                    v.pitch = from.pitch + (to.pitch - from.pitch) * t;
                    v.lowPass = from.lowPass + (to.lowPass - from.lowPass) * t;
                }
                level.db += v.volume;
                level.pitchCents += v.pitch;
                level.lowPass += v.lowPass;
            }
        }
        if (auto set = busActionValues.find(id); set != busActionValues.end()) {
            level.db += set->second.volume;
            level.pitchCents += set->second.pitch;
            level.lowPass += set->second.lowPass;
            if (set->second.muted) level.fadeGain = 0;
        }
        if (auto duck = busDuckDb.find(id); duck != busDuckDb.end()) level.db += duck->second;
    }
}

// Ducking, once an update: a bus ducks the buses it names while something plays through it (ours, or Wwise's as the
// game's hooks tell), down over the duck's fade-out time, and back up over its fade-in time once nothing has played
// through it for its recovery time. A bus's ducking in all is no more than its maximum duck.
void Engine::UpdateMix()
{
    double dt = std::max(0.0, now - lastMixAt);
    lastMixAt = now;
    std::unordered_map<uint32_t, int> on = externalOn;
    for (const Voice* v : liveVoices)
        if (!v->ended && !v->silence && v->bus && !(v->virtualVoice && v->quietGraceUntil > 0))  // (one waiting for its place: Wwise wouldn't have started it)
            for (uint32_t id : BusChain(v->bus)) on[id]++;
    busDuckDb.clear();
    for (const auto& [id, bus] : library.AllBuses()) {
        if (bus->ducks.empty()) continue;
        bool active = on.count(id) != 0;
        for (const Bus::Duck& d : bus->ducks) {
            DuckState& st = ducking[{id, d.bus}];
            if (active) st.lastActive = now;
            float target = active || now - st.lastActive < bus->recoveryMs / 1000.0 ? d.db : 0.0f;
            float span = std::abs(d.db);
            if (target < st.db) st.db = std::max(target, st.db - static_cast<float>(span * dt / std::max(0.001, d.fadeOutMs / 1000.0)));
            else if (target > st.db) st.db = std::min(target, st.db + static_cast<float>(span * dt / std::max(0.001, d.fadeInMs / 1000.0)));
            if (st.db < 0) busDuckDb[d.bus] += st.db;
        }
    }
    for (auto& [id, db] : busDuckDb)
        if (const Bus* b = library.FindBus(id)) db = std::max(db, b->maxDuckDb);
}

// A bus's voice limit, at a voice's start (Armageddon's own banks only: ours keep their own limits, without the bus
// caps that cut them off on Wwise): with as many through the bus as it allows, the new one is refused, or the oldest
// ends, as the bus says. False if refused.
bool Engine::LimitBuses(Voice& v)
{
    const Instance* in = v.owner;
    const Bank* bank = in && in->node ? library.FindBank(in->node->bankId) : nullptr;
    if (!bank || !bank->armageddons || !v.bus) return true;
    for (uint32_t id : BusChain(v.bus)) {
        const Bus* b = library.FindBus(id);
        if (!b || !b->mostAtOnce) continue;
        int count = 0;
        Voice* oldest = nullptr;
        for (Voice* o : liveVoices) {
            if (o == &v || o->ended || o->silence || !o->bus) continue;
            std::vector<uint32_t> chain = BusChain(o->bus);
            if (std::find(chain.begin(), chain.end(), id) == chain.end()) continue;
            count++;
            if (!oldest || (o->owner && oldest->owner && o->owner->serial < oldest->owner->serial)) oldest = o;
        }
        if (count < b->mostAtOnce) continue;
        if (b->killNewest || !oldest) {
            refused++;
            return false;
        }
        oldest->ended = true;  // its instance finishes at its next update
        ReleaseSource(*oldest);
    }
    return true;
}

// Whether something an instance plays goes through a bus.
bool Engine::ThroughBus(const Instance& in, uint32_t bus) const
{
    if (in.voice && in.voice->bus) {
        std::vector<uint32_t> chain = BusChain(in.voice->bus);
        if (std::find(chain.begin(), chain.end(), bus) != chain.end()) return true;
    }
    for (const auto& c : in.children)
        if (ThroughBus(*c, bus)) return true;
    return false;
}


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
            o.streamed = v->stream != nullptr || v->feed != nullptr || (!v->decoded && !v->virtualVoice);
            o.three_d = v->threeD;
            o.level_db = v->levelDb;
            o.distance = v->distance;
            o.length_s = static_cast<float>(v->length);
            o.priority = v->priority;
            if (v->source && v->decoded) {
                float seconds = 0;
                al.alGetSourcef(v->source, AL_SEC_OFFSET, &seconds);
                o.position_s = seconds;
            } else if ((v->stream || v->feed) && v->source && v->rate) {
                // The frames given to OpenAL, less those queued and not yet played (a loop's frames count on).
                ALint queued = 0;
                al.alGetSourcei(v->source, AL_BUFFERS_QUEUED, &queued);
                float seconds = 0;
                al.alGetSourcef(v->source, AL_SEC_OFFSET, &seconds);
                double givenSeconds = static_cast<double>(v->framesQueued) / v->rate;
                double played = std::max(0.0, givenSeconds - queued * static_cast<double>(StreamBufferFrames) / v->rate + seconds);
                o.position_s = static_cast<float>(v->length > 0 ? std::fmod(played, v->length) : played);
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
        if (v->stream || v->feed) out.streams++;
    }
    out.sources_used = sourcesMade - static_cast<int>(freeSources.size());
    out.sources = maxSources;
    out.cache_bytes = cache.Bytes();
    out.banks = static_cast<int>(library.Banks().size());
    out.voices_refused = refused;
    out.voices_ended_quiet = endedQuiet;
    out.voices_stolen = stolen;
    out.underruns = underruns;
    out.media_failed = mediaFailed;
    out.voices_no_source = noSource;
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

std::vector<uint32_t> Engine::EventDuckingBuses(uint32_t eventId) const
{
    std::vector<uint32_t> found;
    const Event* event = library.FindEvent(eventId);
    if (!event) return found;
    for (uint32_t actionId : event->actions) {
        const Action* a = library.FindAction(actionId);
        if (!a || a->Kind() != ActionPlay) continue;
        const Node* n = library.FindNode(a->target);
        for (int depth = 0; n && n->parent && depth < 64; depth++) n = library.FindNode(n->parent);
        if (!n) continue;
        for (uint32_t bus : BusChain(n->bus))
            if (const Bus* b = library.FindBus(bus); b && !b->ducks.empty() && std::find(found.begin(), found.end(), bus) == found.end())
                found.push_back(bus);
    }
    return found;
}

int Engine::EventReach(uint32_t eventId) const
{
    const Event* event = library.FindEvent(eventId);
    if (!event) return 0;
    int reach = RFA_AUDIO_REACH_FOUND;
    for (uint32_t actionId : event->actions) {
        const Action* a = library.FindAction(actionId);
        if (!a) continue;
        const uint32_t kind = a->Kind(), scope = a->Scope();
        const bool plays = kind == ActionPlay || kind == ActionPostEvent;
        if (plays) reach |= RFA_AUDIO_REACH_PLAYS;
        if (kind == ActionSetState) {
            reach |= RFA_AUDIO_REACH_STATES;
            continue;
        }
        if (kind == ActionSetSwitch) {
            reach |= RFA_AUDIO_REACH_SWITCHES;
            continue;
        }
        if (!plays && (scope == ScopeAll || scope == ScopeAllBut)) {
            reach |= RFA_AUDIO_REACH_ALL;  // everything playing, whatever the target
            continue;
        }
        if (!plays && scope == ScopeEverywhere) reach |= RFA_AUDIO_REACH_EVERY_OBJECT;
        if (!a->target) continue;
        // Its target: a bus (every sound through it), or an object of another bank (or of none loaded, or not read).
        bool elsewhere;
        if (library.FindBus(a->target)) {
            reach |= RFA_AUDIO_REACH_BUSES;
            continue;
        } else if (kind == ActionPostEvent) {
            const Event* other = library.FindEvent(a->target);
            elsewhere = !other || other->bankId != event->bankId;
        } else {
            const Node* node = library.FindNode(a->target);
            elsewhere = !node || node->bankId != event->bankId;
        }
        if (elsewhere) reach |= plays ? RFA_AUDIO_REACH_PLAYS_OTHER_BANKS : RFA_AUDIO_REACH_ACTS_OTHER_BANKS;
    }
    return reach;
}

int Engine::EventRoute(uint32_t eventId, uint32_t bankId, const std::unordered_set<uint32_t>& here)
{
    const Event* event = library.FindEvent(eventId);
    const int reach = EventReach(eventId);
    if (!event || !(reach & RFA_AUDIO_REACH_FOUND)) return RFA_AUDIO_ROUTE_ELSEWHERE;
    if (!(reach & RFA_AUDIO_REACH_PLAYS)) return RFA_AUDIO_ROUTE_BOTH;
    const bool isHere = here.count(bankId) != 0;  // the bank it goes by (an event may be in several)
    bool reachesOther = (reach & (RFA_AUDIO_REACH_ALL | RFA_AUDIO_REACH_BUSES | RFA_AUDIO_REACH_STATES | RFA_AUDIO_REACH_SWITCHES)) != 0;
    if (!reachesOther && (reach & RFA_AUDIO_REACH_ACTS_OTHER_BANKS)) {
        // A stop, pause, mute or the like on another bank's sound: the other engine's, unless that bank is on this one.
        for (uint32_t actionId : event->actions) {
            const Action* a = library.FindAction(actionId);
            if (!a || !a->target) continue;
            const uint32_t kind = a->Kind();
            if (kind == ActionPlay || kind == ActionPostEvent || kind == ActionSetState || kind == ActionSetSwitch) continue;
            const Node* node = library.FindNode(a->target);
            if (!node || (here.count(node->bankId) != 0) != isHere) reachesOther = true;
        }
    }
    if (!isHere) return reachesOther ? RFA_AUDIO_ROUTE_ELSEWHERE_AND_CONTROLS : RFA_AUDIO_ROUTE_ELSEWHERE;
    rfa_audio_event_info info{};
    EventInfo(eventId, info);
    const bool playable = !info.synthesized && !info.not_read && !info.missing;
    return playable && !reachesOther ? RFA_AUDIO_ROUTE_HERE : RFA_AUDIO_ROUTE_ELSEWHERE_AND_CONTROLS;
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
        // An attenuation that fades (Armageddon's; ours are flat): where its volume curve ends.
        if (n.ownPositioning && n.threeD)
            if (const Attenuation* at = library.FindAttenuation(n.attenuation); at && at->volumeDry >= 0) {
                const Curve& c = at->curves[at->volumeDry];
                bool fades = std::any_of(c.points.begin(), c.points.end(), [](const CurvePoint& p) { return p.y < -0.5f; });
                if (fades) out.farthest_m = std::max(out.farthest_m, at->maxDistance);
            }
        if (n.type == TypeSound) {
            if (n.loopCount == 0) out.loops = 1;
            double seconds = 0;
            std::string why;
            if (n.IsSourcePlugin()) {
                // No medium: Wwise's silence lasts its length; a synthesized source isn't played yet.
                if (n.IsSilence()) longest = std::max(longest, delay + n.silenceSeconds * std::max<int>(1, n.loopCount));
                else out.synthesized++;
            } else if (MediumSeconds(n, seconds, why)) {
                longest = std::max(longest, delay + seconds * std::max<int>(1, n.loopCount));
            } else {
                out.missing++;
                unknownLength = true;
            }
        }
        if (n.type == TypeRanSeq && n.continuous && n.containerLoops == 0) out.loops = 1;
        if (n.type == TypeSwitch && !out.switch_group) out.switch_group = n.group;
        for (uint32_t child : n.children) {
            if (const Node* c = library.FindNode(child)) visit(*c, threeD, delay, depth + 1);
            else if (library.UnreadType(child)) out.not_read++;
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
                if (library.UnreadType(a->target)) out.not_read++;  // music or dialogue: in its bank, not read yet
                else out.missing++;
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
    if (n.type == TypeSound && n.IsSourcePlugin()) {
        text += n.IsSilence() ? ": silence, " + Number(n.silenceSeconds, 2) + " s" : ": a synthesized source (plug-in " + Hex(n.codec) + "; not played yet)";
    } else if (n.type == TypeSound) {
        Bank* bank = library.FindBank(n.bankId);
        MediumInfo info;
        std::string why;
        if (bank && cache.Info(*bank, n.source, info, why)) {
            const WemFormat& f = info.format;
            text += ": medium " + Hex(n.source) + " (" + (f.IsVorbis() ? "Wwise Vorbis" : f.IsAdpcm() ? "Wwise ADPCM" : "PCM") + ", " + std::to_string(f.channels) +
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
