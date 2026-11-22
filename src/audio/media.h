// A sound's medium as OpenAL plays it: decoded whole into one buffer (kept in a cache for the next time), or
// streamed from the media file a piece at a time. PCM and Wwise ADPCM.
#pragma once

#include "bank.h"
#include "openal.h"

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rfa_audio {

// Wwise's ADPCM as Armageddon's sound engine reads it (port of RfaFormats' WwiseAdpcm.Decode; the game's block
// decoder 0xEC0DE0): blocks of 36 bytes for 64 samples of one channel (the first sample, the step's index, a
// byte unused, 63 codes of 4 bits, the low half of each byte first), the channels' blocks one after another.
// Decodes `blocks` blocks of every channel into interleaved 16-bit samples (64 × channels a block).
void DecodeWwiseAdpcm(const uint8_t* data, int channels, int64_t blocks, int16_t* out);

// An OpenAL buffer format for 16-bit samples of so many channels (0 if OpenAL hasn't one).
ALenum Format16(const OpenAl& al, int channels);

// A medium decoded whole into an OpenAL buffer, its loop points set on it (AL_SOFT_loop_points).
struct Decoded {
    ALuint buffer = 0;
    int channels = 0, rate = 0, codec = 0;  // codec: 1 PCM, 2 ADPCM, 3 samples given
    int64_t frames = 0, bytes = 0;
    bool hasLoop = false;
    int64_t loopStart = 0, loopEnd = 0;     // its end included
    double Seconds() const { return rate ? static_cast<double>(frames) / rate : 0; }
};

// A medium's place and format (its .wem's header, read once).
struct MediumInfo {
    const Bank* bank = nullptr;
    MediumEntry entry;
    WemFormat format;
    double Seconds() const { return format.rate ? static_cast<double>(format.frames) / format.rate : 0; }
};

// Media decoded whole, kept while used and, unused, until the cache is over its size (least recently used first).
class MediaCache {
public:
    void Init(OpenAl* openal, int64_t limitBytes) { al = openal; limit = limitBytes; }
    // A medium's format: false with why (not in the media file, can't be read, a format not read).
    bool Info(const Bank& bank, uint32_t mediumId, MediumInfo& info, std::string& why);
    // The medium decoded whole (from the cache, or decoded now). Null with why.
    std::shared_ptr<Decoded> Get(const MediumInfo& info, std::string& why);
    // Samples given (not kept in the cache).
    std::shared_ptr<Decoded> FromSamples(const int16_t* samples, int frames, int channels, int rate, bool loop, int loopStart, std::string& why);
    // Frees unused buffers past the limit.
    void Trim();
    // Forgets a bank's media (it is being unloaded): its buffers go once unused.
    void ForgetBank(uint32_t bankId);
    void Clear();
    int64_t Bytes() const { return bytes; }

private:
    struct Entry {
        uint64_t key;
        std::shared_ptr<Decoded> decoded;
    };
    OpenAl* al = nullptr;
    int64_t limit = 64ll << 20, bytes = 0;
    std::list<Entry> entries;  // most recently used first
    std::unordered_map<uint64_t, std::list<Entry>::iterator> byKey;
    std::unordered_map<uint64_t, MediumInfo> infos;
    void Release(Decoded& d);
};

// A medium read from its file as it plays: pieces of decoded samples, its loop played as its sound says.
class Stream {
public:
    // loopCount: 0 for ever, 1 once, n times. False with why.
    bool Open(const MediumInfo& info, int loopCount, std::string& why);
    // Up to `frames` frames into out (channels interleaved). Returns how many (fewer only at its end).
    int Read(int16_t* out, int frames);
    bool AtEnd() const { return ended; }
    // A break: what plays now plays to its end, no more loops.
    void StopLooping() { if (loopsLeft != 1) loopsLeft = 1; }
    int64_t Position() const { return position; }
    const WemFormat& Format() const { return info.format; }
    bool failed = false;

private:
    bool DecodeRange(int64_t first, int count, int16_t* out);
    MediumInfo info;
    int64_t position = 0;
    int loopsLeft = 1;  // 0 for ever
    int64_t loopStart = 0, loopEnd = 0;
    bool ended = false;
    std::vector<uint8_t> raw;
    std::vector<int16_t> scratch;
};

}  // namespace rfa_audio
