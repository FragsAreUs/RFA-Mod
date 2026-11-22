// Media: decoding, the cache, streams (see media.h).
#include "media.h"

#include <algorithm>
#include <cstring>

namespace rfa_audio {

namespace {

// The 89 steps and 16 index changes of the game's decoder (0x10AE320, 0x10AE300: the standard IMA ones).
const int16_t Steps[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173,
    190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
const int8_t IndexChanges[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

constexpr int AdpcmBlockBytes = 36, AdpcmBlockFrames = 64;

// What a code does to the sample and the index, as the game's decoder has it.
inline void Apply(int code, int& sample, int& index)
{
    int change = Steps[index] * (2 * (code & 7) + 1) / 8;
    sample = (code & 8) ? sample - change : sample + change;
    sample = std::clamp(sample, -32768, 32767);
    index = std::clamp(index + IndexChanges[code], 0, 88);
}

}  // namespace

void DecodeWwiseAdpcm(const uint8_t* data, int channels, int64_t blocks, int16_t* out)
{
    for (int64_t b = 0; b < blocks; b++)
        for (int c = 0; c < channels; c++) {
            const uint8_t* block = data + (b * channels + c) * AdpcmBlockBytes;
            int sample = static_cast<int16_t>(block[0] | (block[1] << 8));
            int index = std::min<int>(block[2], 88);
            int16_t* to = out + b * AdpcmBlockFrames * channels + c;
            to[0] = static_cast<int16_t>(sample);
            for (int n = 1; n < AdpcmBlockFrames; n++) {
                int both = block[4 + (n - 1) / 2];
                Apply((n - 1) % 2 == 0 ? both & 15 : both >> 4, sample, index);
                to[n * channels] = static_cast<int16_t>(sample);
            }
        }
}

ALenum Format16(const OpenAl& al, int channels)
{
    switch (channels) {
    case 1: return AL_FORMAT_MONO16;
    case 2: return AL_FORMAT_STEREO16;
    case 4: return al.alGetEnumValue("AL_FORMAT_QUAD16");
    case 6: return al.alGetEnumValue("AL_FORMAT_51CHN16");
    case 7: return al.alGetEnumValue("AL_FORMAT_61CHN16");
    case 8: return al.alGetEnumValue("AL_FORMAT_71CHN16");
    default: return 0;
    }
}

// ---- The cache ---------------------------------------------------------------------------------------

static uint64_t KeyOf(uint32_t bankId, uint32_t mediumId) { return (static_cast<uint64_t>(bankId) << 32) | mediumId; }

bool MediaCache::Info(const Bank& bank, uint32_t mediumId, MediumInfo& info, std::string& why)
{
    uint64_t key = KeyOf(bank.id, mediumId);
    if (auto known = infos.find(key); known != infos.end()) {
        info = known->second;
        return true;
    }
    auto entry = bank.media.find(mediumId);
    if (entry == bank.media.end() || !bank.mediaFile) {
        why = "its medium isn't in the media file";
        return false;
    }
    uint8_t head[1024];
    uint32_t size = std::min<uint32_t>(entry->second.size, sizeof head);
    if (!bank.mediaFile->ReadAt(entry->second.offset, head, size)) {
        why = "its medium can't be read";
        return false;
    }
    MediumInfo read;
    read.bank = &bank;
    read.entry = entry->second;
    if (!ReadWemFormat(head, size, read.format, why)) return false;
    if (read.format.dataOffset + static_cast<uint64_t>(read.format.dataBytes) > entry->second.size) {
        why = "its medium's data runs past its place in the media file";
        return false;
    }
    infos[key] = read;
    info = read;
    return true;
}

std::shared_ptr<Decoded> MediaCache::Get(const MediumInfo& info, std::string& why)
{
    uint64_t key = KeyOf(info.bank->id, info.entry.id);
    if (auto found = byKey.find(key); found != byKey.end()) {
        entries.splice(entries.begin(), entries, found->second);  // now the most recent
        return found->second->decoded;
    }
    const WemFormat& f = info.format;
    std::vector<uint8_t> raw(f.dataBytes);
    if (f.dataBytes && !info.bank->mediaFile->ReadAt(info.entry.offset + static_cast<uint64_t>(f.dataOffset), raw.data(), f.dataBytes)) {
        why = "its medium's data can't be read";
        return nullptr;
    }
    std::vector<int16_t> samples(static_cast<size_t>(f.frames) * f.channels);
    if (f.IsAdpcm()) DecodeWwiseAdpcm(raw.data(), f.channels, f.frames / AdpcmBlockFrames, samples.data());
    else if (!samples.empty()) memcpy(samples.data(), raw.data(), samples.size() * 2);
    auto decoded = FromSamples(samples.data(), static_cast<int>(f.frames), f.channels, f.rate, false, 0, why);
    if (!decoded) return nullptr;
    decoded->codec = f.IsAdpcm() ? 2 : 1;
    if (f.hasLoop) {
        decoded->hasLoop = true;
        decoded->loopStart = f.loopStart;
        decoded->loopEnd = f.loopEnd;
        // The loop's end is the last frame it plays (smpl's end is inclusive); OpenAL's end is the frame after.
        ALint points[2] = {static_cast<ALint>(f.loopStart), static_cast<ALint>(f.loopEnd + 1)};
        al->alGetError();
        al->alBufferiv(decoded->buffer, AL_LOOP_POINTS_SOFT, points);
        if (al->alGetError() != AL_NO_ERROR) decoded->hasLoop = false;  // the whole buffer loops instead
    }
    entries.push_front(Entry{key, decoded});
    byKey[key] = entries.begin();
    bytes += decoded->bytes;
    return decoded;
}

std::shared_ptr<Decoded> MediaCache::FromSamples(const int16_t* samples, int frames, int channels, int rate, bool loop, int loopStart, std::string& why)
{
    ALenum format = Format16(*al, channels);
    if (!format) {
        why = std::to_string(channels) + " channels (OpenAL has no format for them)";
        return nullptr;
    }
    if (frames <= 0) {
        why = "no samples";
        return nullptr;
    }
    auto decoded = std::shared_ptr<Decoded>(new Decoded(), [openal = al](Decoded* d) {
        if (d->buffer) openal->alDeleteBuffers(1, &d->buffer);
        delete d;
    });
    al->alGetError();
    al->alGenBuffers(1, &decoded->buffer);
    if (al->alGetError() != AL_NO_ERROR) {
        decoded->buffer = 0;
        why = "OpenAL made no buffer";
        return nullptr;
    }
    decoded->channels = channels;
    decoded->rate = rate;
    decoded->frames = frames;
    decoded->bytes = static_cast<int64_t>(frames) * channels * 2;
    decoded->codec = 3;
    al->alBufferData(decoded->buffer, format, samples, static_cast<ALsizei>(decoded->bytes), rate);
    if (al->alGetError() != AL_NO_ERROR) {
        why = "OpenAL took no samples";
        return nullptr;
    }
    if (loop && loopStart > 0 && loopStart < frames) {
        ALint points[2] = {loopStart, frames};
        al->alBufferiv(decoded->buffer, AL_LOOP_POINTS_SOFT, points);
        decoded->hasLoop = al->alGetError() == AL_NO_ERROR;
        decoded->loopStart = loopStart;
        decoded->loopEnd = frames - 1;
    }
    return decoded;
}

void MediaCache::Trim()
{
    // From the least recently used: those no voice holds (the cache's own reference is the only one).
    for (auto it = entries.end(); bytes > limit && it != entries.begin();) {
        --it;
        if (it->decoded.use_count() > 1) continue;
        bytes -= it->decoded->bytes;
        byKey.erase(it->key);
        it = entries.erase(it);
    }
}

void MediaCache::ForgetBank(uint32_t bankId)
{
    for (auto it = entries.begin(); it != entries.end();) {
        if (static_cast<uint32_t>(it->key >> 32) == bankId) {
            bytes -= it->decoded->bytes;
            byKey.erase(it->key);
            it = entries.erase(it);  // a voice still holding it keeps it until it ends
        } else {
            ++it;
        }
    }
    for (auto it = infos.begin(); it != infos.end();) {
        if (static_cast<uint32_t>(it->first >> 32) == bankId) it = infos.erase(it);
        else ++it;
    }
}

void MediaCache::Clear()
{
    entries.clear();
    byKey.clear();
    infos.clear();
    bytes = 0;
}

// ---- Streams -----------------------------------------------------------------------------------------

bool Stream::Open(const MediumInfo& medium, int loopCount, std::string& why)
{
    info = medium;
    const WemFormat& f = info.format;
    if (f.frames <= 0) {
        why = "an empty medium";
        return false;
    }
    loopsLeft = loopCount;
    // A sound that loops without a loop in its medium loops the whole of it.
    loopStart = f.hasLoop ? f.loopStart : 0;
    loopEnd = f.hasLoop ? f.loopEnd : f.frames - 1;
    position = 0;
    ended = false;
    return true;
}

// Frames [first, first + count) of the medium (never past its end).
bool Stream::DecodeRange(int64_t first, int count, int16_t* out)
{
    const WemFormat& f = info.format;
    uint64_t data = info.entry.offset + static_cast<uint64_t>(f.dataOffset);
    if (!f.IsAdpcm()) {
        uint32_t size = static_cast<uint32_t>(count) * f.channels * 2;
        return info.bank->mediaFile->ReadAt(data + static_cast<uint64_t>(first) * f.channels * 2, out, size);
    }
    // ADPCM: the blocks the frames are in, decoded, and the frames wanted copied out.
    int64_t firstBlock = first / AdpcmBlockFrames, lastBlock = (first + count - 1) / AdpcmBlockFrames;
    int64_t blocks = lastBlock - firstBlock + 1;
    raw.resize(static_cast<size_t>(blocks * f.blockAlign));
    if (!info.bank->mediaFile->ReadAt(data + static_cast<uint64_t>(firstBlock) * f.blockAlign, raw.data(), static_cast<uint32_t>(raw.size())))
        return false;
    scratch.resize(static_cast<size_t>(blocks * AdpcmBlockFrames * f.channels));
    DecodeWwiseAdpcm(raw.data(), f.channels, blocks, scratch.data());
    int64_t skip = first - firstBlock * AdpcmBlockFrames;
    memcpy(out, scratch.data() + skip * f.channels, static_cast<size_t>(count) * f.channels * 2);
    return true;
}

int Stream::Read(int16_t* out, int frames)
{
    const WemFormat& f = info.format;
    int written = 0;
    while (written < frames && !ended) {
        bool looping = loopsLeft != 1;
        int64_t end = looping ? loopEnd + 1 : f.frames;
        if (position >= end) {
            if (looping) {
                position = loopStart;
                if (loopsLeft > 1) loopsLeft--;
                continue;
            }
            ended = true;
            break;
        }
        int n = static_cast<int>(std::min<int64_t>(frames - written, end - position));
        if (!DecodeRange(position, n, out + static_cast<size_t>(written) * f.channels)) {
            failed = true;
            ended = true;
            break;
        }
        position += n;
        written += n;
    }
    return written;
}

}  // namespace rfa_audio
