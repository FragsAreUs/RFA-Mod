// Wwise's Vorbis as Red Faction: Armageddon's media hold it (bank version 48, a "vorb" chunk of 0x34 bytes):
//  - the data chunk holds the setup packet, then the audio packets, each after a 6-byte header (u16 its size, u32
//    the granule: its last sample's place);
//  - there are no identification or comment packets (their facts are in fmt and vorb), and the setup packet names
//    each codebook by a 10-bit number instead of holding it: one of libvorbis's 598 standard codebooks, kept in
//    ww2ogg's packed_codebooks.bin (src\third_party\ww2ogg, BSD), whose packed form is rebuilt into Vorbis's own.
//    (Not its aoTuV 6.03 library, which later Wwise used: with that, a residue's class book can't hold its classes.)
//  - the floors, residues, mappings and modes are packed too: Vorbis's fields without the ones Wwise fixes (the
//    floor and mapping types, the window and transform types), narrower where Vorbis's are wide.
// Decoding rebuilds the three standard Vorbis headers as ww2ogg does (its "external codebooks" kind) and gives the
// audio packets, unchanged, to libvorbis (src\third_party\libvorbis, BSD).
#pragma once

#include "bank.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rfa_audio {

// Loads the codebook library once (later calls do nothing). Any thread may decode once it has loaded.
bool LoadVorbisCodebooks(const std::wstring& path, std::string& why);
// Why Vorbis can't be decoded ("" once the codebooks are loaded).
std::string VorbisUnavailable();

// The packets of a Wwise Vorbis data chunk, in order: the next one at `at` (a place in the data), its bytes and its
// size, `at` moved past it. False at the data's end (or a packet that runs past it).
bool NextVorbisPacket(const uint8_t* data, size_t bytes, size_t& at, const uint8_t*& packet, size_t& size);

// One medium's audio packets decoded in turn.
class VorbisDecoder {
public:
    VorbisDecoder();
    ~VorbisDecoder();
    VorbisDecoder(const VorbisDecoder&) = delete;
    VorbisDecoder& operator=(const VorbisDecoder&) = delete;

    // Its format (channels, rate, block sizes) and its setup packet (without the 6-byte header). False with why.
    bool Open(const WemFormat& format, const uint8_t* setup, size_t setupBytes, std::string& why);
    bool IsOpen() const;
    // One audio packet: its samples appended to `out` (16-bit, channels interleaved). Returns the frames it added
    // (the first packet after Open or Restart adds none: it only primes the decoder).
    int Decode(const uint8_t* packet, size_t bytes, std::vector<int16_t>& out);
    // Back to the beginning: the next packet decoded is taken as the first audio packet.
    void Restart();

private:
    struct State;
    std::unique_ptr<State> s;
};

// A whole medium's data chunk (its setup and audio packets) decoded into exactly format.frames frames. False with why.
bool DecodeVorbisWhole(const WemFormat& format, const uint8_t* data, size_t bytes, std::vector<int16_t>& samples, std::string& why);

}  // namespace rfa_audio
