// A sound's medium as OpenAL plays it: decoded whole into one buffer (kept in a cache for the next time), or
// streamed from the media file a piece at a time. PCM and Wwise ADPCM.
#pragma once

#include "bank.h"
#include "openal.h"
#include "vorbis.h"

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rfa_audio {

// A medium up to this many bytes decoded is decoded whole (and cached); a longer one is streamed.
constexpr int64_t WholeLimitBytes = 8ll << 20;
// A stream's piece, in frames: four of them queued (about 0.7 s at 48 kHz).
constexpr int StreamBufferFrames = 8192;

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

// The codec number a voice reports (rfa_audio_voice.codec): 1 PCM, 2 Wwise ADPCM, 4 Wwise Vorbis.
inline int CodecOf(const WemFormat& f) { return f.IsVorbis() ? 4 : f.IsAdpcm() ? 2 : 1; }

// Whether a medium is decoded whole: small enough, and played through at most once (a loop is streamed).
inline bool IsWhole(const WemFormat& f, int loopCount)
{
    return static_cast<int64_t>(f.frames) * f.channels * 2 <= WholeLimitBytes && loopCount <= 1;
}

// Reads a medium's format from its bank's media file (its .wem's head). Touches no cache, so any thread may call
// it while the bank is loaded. False with why.
bool ReadMediumInfo(const Bank& bank, uint32_t mediumId, MediumInfo& info, std::string& why);
// Reads a medium and decodes it whole into 16-bit samples (channels interleaved). Any thread. False with why.
bool DecodeWhole(const MediumInfo& info, std::vector<int16_t>& samples, std::string& why);

// Media decoded whole, kept while used and, unused, until the cache is over its size (least recently used first).
class MediaCache {
public:
    void Init(OpenAl* openal, int64_t limitBytes) { al = openal; limit = limitBytes; }
    // A medium's format: false with why (not in the media file, can't be read, a format not read).
    bool Info(const Bank& bank, uint32_t mediumId, MediumInfo& info, std::string& why);
    // A medium's format if it has been read already (nothing is read).
    bool KnownInfo(const Bank& bank, uint32_t mediumId, MediumInfo& info) const;
    // A format read elsewhere (ReadMediumInfo), kept.
    void PutInfo(const MediumInfo& info);
    // The medium decoded whole (from the cache, or decoded now). Null with why.
    std::shared_ptr<Decoded> Get(const MediumInfo& info, std::string& why);
    // The medium decoded whole if it is in the cache (nothing is decoded); null if not.
    std::shared_ptr<Decoded> Find(const MediumInfo& info);
    // Samples decoded elsewhere (DecodeWhole) into an OpenAL buffer, kept in the cache. Null with why.
    std::shared_ptr<Decoded> Put(const MediumInfo& info, const std::vector<int16_t>& samples, std::string& why);
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
    const Bank* OfBank() const { return info.bank; }
    bool failed = false;

private:
    bool DecodeRange(int64_t first, int count, int16_t* out);
    // Wwise Vorbis: decoded packet by packet in order, from a piece of its data read ahead.
    int ReadVorbis(int16_t* out, int frames);
    bool VorbisSeek(int64_t frame);  // decodes again from its first packet up to `frame`
    bool VorbisPacket(const uint8_t*& packet, size_t& size);
    bool VorbisRead(uint64_t from);
    std::unique_ptr<VorbisDecoder> vorbis;
    std::vector<uint8_t> vorbisPiece;      // its data from vorbisPieceAt
    uint64_t vorbisPieceAt = 0;
    uint64_t vorbisNext = 0;               // the next packet's place in its data
    std::vector<int16_t> vorbisPending;    // decoded, not yet given: frames from `position` on
    size_t vorbisPendingAt = 0;
    MediumInfo info;
    int64_t position = 0;
    int loopsLeft = 1;  // 0 for ever
    int64_t loopStart = 0, loopEnd = 0;
    bool ended = false;
    std::vector<uint8_t> raw;
    std::vector<int16_t> scratch;
};

}  // namespace rfa_audio
