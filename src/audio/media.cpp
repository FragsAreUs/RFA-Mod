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

bool ReadMediumInfo(const Bank& bank, uint32_t mediumId, MediumInfo& info, std::string& why)
{
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
    // A voice line of Armageddon's: a lip-sync block ("DMAV") first, the .wem after it on a 2 KB boundary. The voice
    // banks' index gives the .wem's size alone: the medium's bytes are the block's and then that many.
    uint32_t lipSync = 0;
    if (size >= 4 && memcmp(head, "DMAV", 4) == 0) {
        // Its own .wem only: one whose RIFF size is the size given (a block with no sound after it, which some are, is
        // followed by the next medium, whose RIFF this would otherwise take).
        for (uint32_t at = 2048; at <= 16384; at += 2048) {
            uint8_t riff[8] = {};
            uint32_t riffSize = 0;
            if (bank.mediaFile->ReadAt(entry->second.offset + at, riff, 8) && memcmp(riff, "RIFF", 4) == 0) {
                memcpy(&riffSize, riff + 4, 4);
                if (riffSize + 8ull == entry->second.size) lipSync = at;
                break;
            }
        }
        if (!lipSync) {
            why = "its medium is a lip-sync block with no sound after it";
            return false;
        }
        size = std::min<uint32_t>(entry->second.size, sizeof head);
        if (!bank.mediaFile->ReadAt(entry->second.offset + lipSync, head, size)) {
            why = "its medium can't be read";
            return false;
        }
    }
    MediumInfo read;
    read.bank = &bank;
    read.entry = entry->second;
    if (!ReadWemFormat(head, size, read.format, why)) return false;
    read.format.lipSyncBytes = lipSync;
    read.format.dataOffset += lipSync;
    read.entry.size += lipSync;
    if (read.format.dataOffset + static_cast<uint64_t>(read.format.dataBytes) > read.entry.size) {
        why = "its medium's data runs past its place in the media file";
        return false;
    }
    info = read;
    return true;
}

bool DecodeWhole(const MediumInfo& info, std::vector<int16_t>& samples, std::string& why)
{
    const WemFormat& f = info.format;
    std::vector<uint8_t> raw(f.dataBytes);
    if (f.dataBytes && !info.bank->mediaFile->ReadAt(info.entry.offset + static_cast<uint64_t>(f.dataOffset), raw.data(), f.dataBytes)) {
        why = "its medium's data can't be read";
        return false;
    }
    if (f.IsVorbis()) return DecodeVorbisWhole(f, raw.data(), raw.size(), samples, why);
    samples.assign(static_cast<size_t>(f.frames) * f.channels, 0);
    if (f.IsAdpcm()) DecodeWwiseAdpcm(raw.data(), f.channels, f.frames / AdpcmBlockFrames, samples.data());
    else if (!samples.empty()) memcpy(samples.data(), raw.data(), std::min(samples.size() * 2, raw.size()));
    return true;
}

bool MediaCache::KnownInfo(const Bank& bank, uint32_t mediumId, MediumInfo& info) const
{
    auto known = infos.find(KeyOf(bank.id, mediumId));
    if (known == infos.end()) return false;
    info = known->second;
    return true;
}

void MediaCache::PutInfo(const MediumInfo& info) { infos[KeyOf(info.bank->id, info.entry.id)] = info; }

bool MediaCache::Info(const Bank& bank, uint32_t mediumId, MediumInfo& info, std::string& why)
{
    if (KnownInfo(bank, mediumId, info)) return true;
    if (!ReadMediumInfo(bank, mediumId, info, why)) return false;
    PutInfo(info);
    return true;
}

std::shared_ptr<Decoded> MediaCache::Find(const MediumInfo& info)
{
    auto found = byKey.find(KeyOf(info.bank->id, info.entry.id));
    if (found == byKey.end()) return nullptr;
    entries.splice(entries.begin(), entries, found->second);  // now the most recent
    return found->second->decoded;
}

std::shared_ptr<Decoded> MediaCache::Get(const MediumInfo& info, std::string& why)
{
    if (auto found = Find(info)) return found;
    std::vector<int16_t> samples;
    if (!DecodeWhole(info, samples, why)) return nullptr;
    return Put(info, samples, why);
}

std::shared_ptr<Decoded> MediaCache::Put(const MediumInfo& info, const std::vector<int16_t>& samples, std::string& why)
{
    uint64_t key = KeyOf(info.bank->id, info.entry.id);
    if (auto found = Find(info)) return found;  // another voice's read got there first
    const WemFormat& f = info.format;
    if (samples.size() < static_cast<size_t>(f.frames) * f.channels) {
        why = "its medium decoded short";
        return nullptr;
    }
    auto decoded = FromSamples(samples.data(), static_cast<int>(f.frames), f.channels, f.rate, false, 0, why);
    if (!decoded) return nullptr;
    decoded->codec = CodecOf(f);
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

// ---- Wwise Vorbis streamed ----------------------------------------------------------------------------------------

// Reads its data from `from` on, as much as a packet can take and more (a packet is at most 64 KB and 6 bytes).
bool Stream::VorbisRead(uint64_t from)
{
    const WemFormat& f = info.format;
    uint32_t bytes = static_cast<uint32_t>(std::min<uint64_t>(128 * 1024, f.dataBytes - from));
    vorbisPiece.resize(bytes);
    vorbisPieceAt = from;
    return info.bank->mediaFile->ReadAt(info.entry.offset + f.dataOffset + from, vorbisPiece.data(), bytes);
}

bool Stream::VorbisPacket(const uint8_t*& packet, size_t& size)
{
    const WemFormat& f = info.format;
    auto held = [&](uint64_t bytes) { return vorbisNext >= vorbisPieceAt && vorbisNext + bytes <= vorbisPieceAt + vorbisPiece.size(); };
    if (vorbisNext + 6 > f.dataBytes) return false;
    if (!held(6) && !VorbisRead(vorbisNext)) return false;
    uint16_t length;
    memcpy(&length, vorbisPiece.data() + (vorbisNext - vorbisPieceAt), 2);
    if (vorbisNext + 6 + length > f.dataBytes) return false;
    if (!held(6ull + length) && !VorbisRead(vorbisNext)) return false;
    packet = vorbisPiece.data() + (vorbisNext - vorbisPieceAt) + 6;
    size = length;
    vorbisNext += 6ull + length;
    return true;
}

bool Stream::VorbisSeek(int64_t frame)
{
    const WemFormat& f = info.format;
    if (!vorbis) {
        // Opened on first use (on the worker's thread, not under the engine's lock): its setup packet read first.
        vorbisNext = f.vorbisSetup;
        const uint8_t* setup;
        size_t size;
        std::string why;
        auto decoder = std::make_unique<VorbisDecoder>();
        if (!VorbisPacket(setup, size) || !decoder->Open(f, setup, size, why)) return false;
        vorbis = std::move(decoder);
    } else {
        vorbis->Restart();
    }
    vorbisNext = f.vorbisAudio;
    vorbisPending.clear();
    vorbisPendingAt = 0;
    position = 0;
    // Decoded from the start and dropped up to `frame` (Armageddon's loops start at 0, so this is rare).
    while (position < frame) {
        size_t have = (vorbisPending.size() - vorbisPendingAt) / f.channels;
        if (!have) {
            vorbisPending.clear();
            vorbisPendingAt = 0;
            const uint8_t* packet;
            size_t size;
            if (!VorbisPacket(packet, size)) break;
            vorbis->Decode(packet, size, vorbisPending);
            continue;
        }
        int64_t drop = std::min<int64_t>(static_cast<int64_t>(have), frame - position);
        vorbisPendingAt += static_cast<size_t>(drop) * f.channels;
        position += drop;
    }
    position = frame;
    return true;
}

int Stream::ReadVorbis(int16_t* out, int frames)
{
    const WemFormat& f = info.format;
    if (!vorbis && !VorbisSeek(0)) {
        failed = true;
        ended = true;
        return 0;
    }
    int written = 0;
    while (written < frames && !ended) {
        bool looping = loopsLeft != 1;
        int64_t end = looping ? loopEnd + 1 : f.frames;
        if (position >= end) {
            if (looping) {
                if (!VorbisSeek(loopStart)) {
                    failed = ended = true;
                    break;
                }
                if (loopsLeft > 1) loopsLeft--;
                continue;
            }
            ended = true;
            break;
        }
        size_t have = (vorbisPending.size() - vorbisPendingAt) / f.channels;
        if (!have) {
            vorbisPending.clear();
            vorbisPendingAt = 0;
            const uint8_t* packet;
            size_t size;
            if (VorbisPacket(packet, size)) vorbis->Decode(packet, size, vorbisPending);
            else vorbisPending.assign(static_cast<size_t>(std::min<int64_t>(end - position, 4096)) * f.channels, 0);  // short: silence to its count
            continue;
        }
        int n = static_cast<int>(std::min<int64_t>({static_cast<int64_t>(frames - written), static_cast<int64_t>(have), end - position}));
        memcpy(out + static_cast<size_t>(written) * f.channels, vorbisPending.data() + vorbisPendingAt, static_cast<size_t>(n) * f.channels * 2);
        vorbisPendingAt += static_cast<size_t>(n) * f.channels;
        position += n;
        written += n;
    }
    return written;
}

int Stream::Read(int16_t* out, int frames)
{
    if (info.format.IsVorbis()) return ReadVorbis(out, frames);
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
