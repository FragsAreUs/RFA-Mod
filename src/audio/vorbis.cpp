// Wwise's Vorbis decoded (see vorbis.h). The header rebuilding follows ww2ogg (Adam Gashlin, BSD-3-Clause, the
// "external codebooks" kind): the same fields, widths and checks, written here from its description.
#include "vorbis.h"

#define NOMINMAX
#include <windows.h>

#include <vorbis/codec.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace rfa_audio {

namespace {

// ---- Bits, as Vorbis packs them: the lowest bit of each byte first ------------------------------------------------

class BitReader {
public:
    BitReader(const uint8_t* bytes, size_t size) : p(bytes), size(size) {}
    uint32_t Read(int bits)
    {
        uint32_t value = 0;
        for (int i = 0; i < bits; i++, at++) {
            size_t byte = at >> 3;
            if (byte >= size) {
                past = true;
                continue;
            }
            if ((p[byte] >> (at & 7)) & 1) value |= 1u << i;
        }
        return value;
    }
    bool Past() const { return past; }
    size_t BitsRead() const { return at; }

private:
    const uint8_t* p;
    size_t size, at = 0;
    bool past = false;
};

class BitWriter {
public:
    void Write(uint32_t value, int bits)
    {
        for (int i = 0; i < bits; i++, at++) {
            if ((at >> 3) >= bytes.size()) bytes.push_back(0);
            if ((value >> i) & 1) bytes[at >> 3] |= static_cast<uint8_t>(1u << (at & 7));
        }
    }
    // A packet's type and "vorbis".
    void Header(int type)
    {
        Write(static_cast<uint32_t>(type), 8);
        for (const char* c = "vorbis"; *c; c++) Write(static_cast<uint8_t>(*c), 8);
    }
    std::vector<uint8_t> bytes;

private:
    size_t at = 0;
};

int ILog(uint32_t v)
{
    int bits = 0;
    while (v) {
        bits++;
        v >>= 1;
    }
    return bits;
}

// How many values a lookup of type 1 holds: the greatest v with v^dimensions <= entries (libvorbis's own way).
uint32_t QuantValues(uint32_t entries, uint32_t dimensions)
{
    if (!dimensions) return 0;
    long vals = static_cast<long>(std::floor(std::pow(static_cast<float>(entries), 1.0f / static_cast<float>(dimensions))));
    for (int guard = 0; guard < 1000; guard++) {
        long acc = 1, acc1 = 1;
        for (uint32_t i = 0; i < dimensions; i++) {
            acc *= vals;
            acc1 *= vals + 1;
        }
        if (acc <= static_cast<long>(entries) && acc1 > static_cast<long>(entries)) return static_cast<uint32_t>(vals);
        if (acc > static_cast<long>(entries)) vals--;
        else vals++;
    }
    return 0;
}

// ---- The codebook library -------------------------------------------------------------------------------------

struct Codebooks {
    std::vector<uint8_t> data;       // the codebooks one after another
    std::vector<uint32_t> offsets;   // where each starts, and one past the last
    std::string why = "the Vorbis codebooks (packed_codebooks.bin) aren't loaded";
    bool loaded = false;
};

Codebooks& TheCodebooks()
{
    static Codebooks books;
    return books;
}

std::mutex g_codebooksLock;

// A codebook rebuilt from its packed form into Vorbis's (ww2ogg's codebook_library::rebuild). False with why.
bool RebuildCodebook(uint32_t id, BitWriter& out, std::string& why)
{
    const Codebooks& books = TheCodebooks();
    if (id + 1 >= books.offsets.size()) {
        why = "a codebook number (" + std::to_string(id) + ") past the library's";
        return false;
    }
    uint32_t start = books.offsets[id], size = books.offsets[id + 1] - start;
    BitReader in(books.data.data() + start, size);
    uint32_t dimensions = in.Read(4), entries = in.Read(14);
    out.Write(0x564342, 24);  // "BCV"
    out.Write(dimensions, 16);
    out.Write(entries, 24);
    uint32_t ordered = in.Read(1);
    out.Write(ordered, 1);
    if (ordered) {
        out.Write(in.Read(5), 5);  // the first length
        uint32_t entry = 0;
        while (entry < entries) {
            int bits = ILog(entries - entry);
            uint32_t number = in.Read(bits);
            out.Write(number, bits);
            entry += number;
            if (in.Past()) break;
        }
        if (entry > entries) {
            why = "codebook " + std::to_string(id) + "'s lengths run past its entries";
            return false;
        }
    } else {
        uint32_t lengthBits = in.Read(3), sparse = in.Read(1);
        if (lengthBits == 0 || lengthBits > 5) {
            why = "codebook " + std::to_string(id) + " has codeword lengths of " + std::to_string(lengthBits) + " bits";
            return false;
        }
        out.Write(sparse, 1);
        for (uint32_t i = 0; i < entries; i++) {
            bool present = true;
            if (sparse) {
                uint32_t flag = in.Read(1);
                out.Write(flag, 1);
                present = flag != 0;
            }
            if (present) out.Write(in.Read(static_cast<int>(lengthBits)), 5);
        }
    }
    uint32_t lookup = in.Read(1);
    out.Write(lookup, 4);
    if (lookup == 1) {
        uint32_t minimum = in.Read(32), maximum = in.Read(32), valueBits = in.Read(4), sequence = in.Read(1);
        out.Write(minimum, 32);
        out.Write(maximum, 32);
        out.Write(valueBits, 4);
        out.Write(sequence, 1);
        uint32_t values = QuantValues(entries, dimensions);
        for (uint32_t i = 0; i < values; i++) out.Write(in.Read(static_cast<int>(valueBits) + 1), static_cast<int>(valueBits) + 1);
    }
    // Every byte used (a last byte with all its bits used leaves one zero byte after it).
    if (in.Past() || in.BitsRead() / 8 + 1 != size) {
        why = "codebook " + std::to_string(id) + " isn't the size the library gives it";
        return false;
    }
    return true;
}

// The setup header rebuilt from Wwise's packed setup packet (ww2ogg's generate_ogg_header, external codebooks).
bool RebuildSetup(const uint8_t* packet, size_t size, int channels, std::vector<uint8_t>& header, std::string& why)
{
    BitReader in(packet, size);
    BitWriter out;
    out.Header(5);
    auto fail = [&](const std::string& what) {
        why = "its Vorbis setup: " + what;
        return false;
    };
    // Codebooks: each a number into the library.
    uint32_t codebooks = in.Read(8) + 1;
    out.Write(codebooks - 1, 8);
    for (uint32_t i = 0; i < codebooks; i++)
        if (!RebuildCodebook(in.Read(10), out, why)) return fail(why);
    // Time-domain transforms: Vorbis's one placeholder.
    out.Write(0, 6);
    out.Write(0, 16);
    // Floors, all type 1.
    uint32_t floors = in.Read(6) + 1;
    out.Write(floors - 1, 6);
    for (uint32_t i = 0; i < floors; i++) {
        out.Write(1, 16);
        uint32_t partitions = in.Read(5);
        out.Write(partitions, 5);
        std::vector<uint32_t> classes(partitions);
        uint32_t maxClass = 0;
        for (uint32_t j = 0; j < partitions; j++) {
            classes[j] = in.Read(4);
            out.Write(classes[j], 4);
            maxClass = std::max(maxClass, classes[j]);
        }
        std::vector<uint32_t> dimensions(maxClass + 1);
        for (uint32_t j = 0; j <= maxClass; j++) {
            uint32_t d = in.Read(3);
            out.Write(d, 3);
            dimensions[j] = d + 1;
            uint32_t subclasses = in.Read(2);
            out.Write(subclasses, 2);
            if (subclasses) {
                uint32_t master = in.Read(8);
                out.Write(master, 8);
                if (master >= codebooks) return fail("a floor's master book past the codebooks");
            }
            for (uint32_t k = 0; k < (1u << subclasses); k++) {
                uint32_t book = in.Read(8);
                out.Write(book, 8);
                if (book > 0 && book - 1 >= codebooks) return fail("a floor's subclass book past the codebooks");
            }
        }
        out.Write(in.Read(2), 2);  // the multiplier
        uint32_t rangeBits = in.Read(4);
        out.Write(rangeBits, 4);
        for (uint32_t j = 0; j < partitions; j++)
            for (uint32_t k = 0; k < dimensions[classes[j]]; k++) out.Write(in.Read(static_cast<int>(rangeBits)), static_cast<int>(rangeBits));
    }
    // Residues.
    uint32_t residues = in.Read(6) + 1;
    out.Write(residues - 1, 6);
    for (uint32_t i = 0; i < residues; i++) {
        uint32_t type = in.Read(2);
        out.Write(type, 16);
        if (type > 2) return fail("a residue of type " + std::to_string(type));
        out.Write(in.Read(24), 24);  // begin
        out.Write(in.Read(24), 24);  // end
        out.Write(in.Read(24), 24);  // partition size - 1
        uint32_t classifications = in.Read(6) + 1;
        out.Write(classifications - 1, 6);
        uint32_t classbook = in.Read(8);
        out.Write(classbook, 8);
        if (classbook >= codebooks) return fail("a residue's class book past the codebooks");
        std::vector<uint32_t> cascade(classifications);
        for (uint32_t j = 0; j < classifications; j++) {
            uint32_t low = in.Read(3), high = 0;
            out.Write(low, 3);
            uint32_t flag = in.Read(1);
            out.Write(flag, 1);
            if (flag) {
                high = in.Read(5);
                out.Write(high, 5);
            }
            cascade[j] = high * 8 + low;
        }
        for (uint32_t j = 0; j < classifications; j++)
            for (int k = 0; k < 8; k++)
                if (cascade[j] & (1u << k)) {
                    uint32_t book = in.Read(8);
                    out.Write(book, 8);
                    if (book >= codebooks) return fail("a residue book past the codebooks");
                }
    }
    // Mappings, all type 0.
    uint32_t mappings = in.Read(6) + 1;
    out.Write(mappings - 1, 6);
    const int channelBits = ILog(static_cast<uint32_t>(channels - 1));
    for (uint32_t i = 0; i < mappings; i++) {
        out.Write(0, 16);
        uint32_t submaps = 1, flag = in.Read(1);
        out.Write(flag, 1);
        if (flag) {
            uint32_t s = in.Read(4);
            out.Write(s, 4);
            submaps = s + 1;
        }
        uint32_t polar = in.Read(1);
        out.Write(polar, 1);
        if (polar) {
            uint32_t steps = in.Read(8) + 1;
            out.Write(steps - 1, 8);
            for (uint32_t j = 0; j < steps; j++) {
                uint32_t magnitude = in.Read(channelBits), angle = in.Read(channelBits);
                out.Write(magnitude, channelBits);
                out.Write(angle, channelBits);
                if (angle == magnitude || magnitude >= static_cast<uint32_t>(channels) || angle >= static_cast<uint32_t>(channels))
                    return fail("a coupling step on channels it hasn't");
            }
        }
        uint32_t reserved = in.Read(2);
        out.Write(reserved, 2);
        if (reserved) return fail("a mapping's reserved bits set");
        if (submaps > 1)
            for (int j = 0; j < channels; j++) {
                uint32_t mux = in.Read(4);
                out.Write(mux, 4);
                if (mux >= submaps) return fail("a channel on a submap it hasn't");
            }
        for (uint32_t j = 0; j < submaps; j++) {
            out.Write(in.Read(8), 8);  // time config (unused)
            uint32_t floor = in.Read(8), residue = in.Read(8);
            out.Write(floor, 8);
            out.Write(residue, 8);
            if (floor >= floors || residue >= residues) return fail("a submap's floor or residue past theirs");
        }
    }
    // Modes.
    uint32_t modes = in.Read(6) + 1;
    out.Write(modes - 1, 6);
    for (uint32_t i = 0; i < modes; i++) {
        out.Write(in.Read(1), 1);  // block flag
        out.Write(0, 16);          // window type
        out.Write(0, 16);          // transform type
        uint32_t mapping = in.Read(8);
        out.Write(mapping, 8);
        if (mapping >= mappings) return fail("a mode's mapping past the mappings");
    }
    out.Write(1, 1);  // framing
    if (in.Past() || (in.BitsRead() + 7) / 8 != size) return fail("its packet isn't the size its fields take");
    header = std::move(out.bytes);
    return true;
}

}  // namespace

bool LoadVorbisCodebooks(const std::wstring& path, std::string& why)
{
    std::lock_guard<std::mutex> hold(g_codebooksLock);
    Codebooks& books = TheCodebooks();
    if (books.loaded) return true;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        books.why = why = "the Vorbis codebooks (packed_codebooks.bin) aren't beside OpenAL Soft";
        return false;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    std::vector<uint8_t> bytes(static_cast<size_t>(std::min<LONGLONG>(size.QuadPart, 16 << 20)));
    DWORD read = 0;
    bool ok = !bytes.empty() && ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
    CloseHandle(h);
    // Its last 4 bytes: where the table of the codebooks' places starts (each 4 bytes, then one past the last).
    uint32_t tableAt = 0;
    if (ok && bytes.size() >= 8) memcpy(&tableAt, bytes.data() + bytes.size() - 4, 4);
    if (!ok || tableAt == 0 || tableAt >= bytes.size() - 4 || (bytes.size() - tableAt) % 4 != 0) {
        books.why = why = "the Vorbis codebooks file isn't ww2ogg's packed_codebooks.bin";
        return false;
    }
    size_t count = (bytes.size() - tableAt) / 4;
    books.offsets.resize(count);
    memcpy(books.offsets.data(), bytes.data() + tableAt, count * 4);
    for (uint32_t offset : books.offsets)
        if (offset > tableAt) {
            books.why = why = "the Vorbis codebooks file's table runs past its codebooks";
            books.offsets.clear();
            return false;
        }
    bytes.resize(tableAt);
    books.data = std::move(bytes);
    books.loaded = true;
    books.why.clear();
    return true;
}

std::string VorbisUnavailable()
{
    std::lock_guard<std::mutex> hold(g_codebooksLock);
    return TheCodebooks().loaded ? std::string() : TheCodebooks().why;
}

bool NextVorbisPacket(const uint8_t* data, size_t bytes, size_t& at, const uint8_t*& packet, size_t& size)
{
    if (at + 6 > bytes) return false;
    uint16_t length;
    memcpy(&length, data + at, 2);
    if (at + 6 + length > bytes) return false;
    packet = data + at + 6;
    size = length;
    at += 6 + static_cast<size_t>(length);
    return true;
}

struct VorbisDecoder::State {
    vorbis_info info;
    vorbis_comment comment;
    vorbis_dsp_state dsp;
    vorbis_block block;
    bool infoMade = false, dspMade = false;
    int channels = 0;
    ogg_int64_t packetNumber = 3;
    ~State()
    {
        if (dspMade) {
            vorbis_block_clear(&block);
            vorbis_dsp_clear(&dsp);
        }
        if (infoMade) {
            vorbis_comment_clear(&comment);
            vorbis_info_clear(&info);
        }
    }
};

VorbisDecoder::VorbisDecoder() = default;
VorbisDecoder::~VorbisDecoder() = default;

bool VorbisDecoder::IsOpen() const { return s && s->dspMade; }

bool VorbisDecoder::Open(const WemFormat& f, const uint8_t* setup, size_t setupBytes, std::string& why)
{
    s.reset();
    if (!TheCodebooks().loaded) {
        why = VorbisUnavailable();
        return false;
    }
    // Identification and comment headers, from fmt and vorb.
    BitWriter id;
    id.Header(1);
    id.Write(0, 32);  // version
    id.Write(static_cast<uint32_t>(f.channels), 8);
    id.Write(static_cast<uint32_t>(f.rate), 32);
    id.Write(0, 32);  // highest bit rate
    id.Write(f.avgBytesPerSecond * 8, 32);
    id.Write(0, 32);  // lowest
    id.Write(f.vorbisBlocks[0], 4);
    id.Write(f.vorbisBlocks[1], 4);
    id.Write(1, 1);
    BitWriter comment;
    comment.Header(3);
    const char vendor[] = "RFA-Mod, from Wwise's Vorbis";
    comment.Write(static_cast<uint32_t>(strlen(vendor)), 32);
    for (const char* c = vendor; *c; c++) comment.Write(static_cast<uint8_t>(*c), 8);
    comment.Write(0, 32);  // no comments
    comment.Write(1, 1);
    std::vector<uint8_t> setupHeader;
    if (!RebuildSetup(setup, setupBytes, f.channels, setupHeader, why)) return false;

    auto state = std::make_unique<State>();
    vorbis_info_init(&state->info);
    vorbis_comment_init(&state->comment);
    state->infoMade = true;
    std::vector<uint8_t>* headers[3] = {&id.bytes, &comment.bytes, &setupHeader};
    for (int i = 0; i < 3; i++) {
        ogg_packet op{};
        op.packet = headers[i]->data();
        op.bytes = static_cast<long>(headers[i]->size());
        op.b_o_s = i == 0;
        op.granulepos = 0;
        op.packetno = i;
        if (int error = vorbis_synthesis_headerin(&state->info, &state->comment, &op)) {
            static const char* names[3] = {"identification", "comment", "setup"};
            why = std::string("libvorbis refused its rebuilt ") + names[i] + " header (" + std::to_string(error) + ")";
            return false;
        }
    }
    if (vorbis_synthesis_init(&state->dsp, &state->info) != 0) {
        why = "libvorbis couldn't start decoding it";
        return false;
    }
    vorbis_block_init(&state->dsp, &state->block);
    state->dspMade = true;
    state->channels = f.channels;
    s = std::move(state);
    return true;
}

int VorbisDecoder::Decode(const uint8_t* packet, size_t bytes, std::vector<int16_t>& out)
{
    if (!IsOpen() || !bytes) return 0;
    ogg_packet op{};
    op.packet = const_cast<unsigned char*>(packet);
    op.bytes = static_cast<long>(bytes);
    op.granulepos = -1;
    op.packetno = s->packetNumber++;
    if (vorbis_synthesis(&s->block, &op) != 0) return 0;  // a packet it can't read adds nothing (as Wwise would skip it)
    vorbis_synthesis_blockin(&s->dsp, &s->block);
    int added = 0;
    float** pcm = nullptr;
    for (int n; (n = vorbis_synthesis_pcmout(&s->dsp, &pcm)) > 0;) {
        size_t at = out.size();
        out.resize(at + static_cast<size_t>(n) * s->channels);
        int16_t* o = out.data() + at;
        for (int i = 0; i < n; i++)
            for (int c = 0; c < s->channels; c++) {
                float v = pcm[c][i] * 32767.0f;
                *o++ = static_cast<int16_t>(std::clamp(v, -32768.0f, 32767.0f));
            }
        vorbis_synthesis_read(&s->dsp, n);
        added += n;
    }
    return added;
}

void VorbisDecoder::Restart()
{
    if (IsOpen()) vorbis_synthesis_restart(&s->dsp);
}

bool DecodeVorbisWhole(const WemFormat& f, const uint8_t* data, size_t bytes, std::vector<int16_t>& samples, std::string& why)
{
    size_t at = f.vorbisSetup;
    const uint8_t* packet;
    size_t size;
    if (!NextVorbisPacket(data, bytes, at, packet, size)) {
        why = "its Vorbis setup packet runs past its data";
        return false;
    }
    VorbisDecoder decoder;
    if (!decoder.Open(f, packet, size, why)) return false;
    const size_t want = static_cast<size_t>(f.frames) * f.channels;
    samples.clear();
    samples.reserve(want);
    at = f.vorbisAudio;
    while (samples.size() < want && NextVorbisPacket(data, bytes, at, packet, size)) decoder.Decode(packet, size, samples);
    samples.resize(want, 0);  // its sample count, exactly (the last packet's extra samples off, or silence if short)
    return true;
}

}  // namespace rfa_audio
