// Many small files read as one (rfa_bundle.h): a mod's folder of animations (a thousand or more files of a few KB)
// is read file by file only the first time; then its files are kept together in one bundle beside the folder
// (<folder>.bundle) and read from it in one go while the folder's files are as they were (the same names, sizes and
// times). Opening a thousand small files is what took the time at the game's start: each costs a seek on a hard
// disk, and a scan by the antivirus.
#pragma once
#include <windows.h>
#include <malloc.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rfa {

struct BundledFile {
    std::wstring name;  // its file name, as in the folder
    void* data;         // aligned to 16 bytes, kept for good (the game keeps pointers to it)
    unsigned size;
};

namespace bundle_detail {

struct Listed {
    std::wstring name;
    unsigned size;
    uint64_t time;
};

constexpr char kMagic[8] = {'R', 'F', 'A', 'B', 'N', 'D', 'L', '1'};
constexpr unsigned kMostFile = 64u * 1024 * 1024;

inline unsigned Align16(unsigned at) { return (at + 15u) & ~15u; }

inline bool ReadAll(HANDLE file, void* into, DWORD size) {
    auto bytes = static_cast<char*>(into);
    while (size) {
        DWORD read = 0;
        if (!ReadFile(file, bytes, size, &read, nullptr) || read == 0) return false;
        bytes += read;
        size -= read;
    }
    return true;
}

inline bool WriteAll(HANDLE file, const void* from, DWORD size) {
    DWORD written = 0;
    return WriteFile(file, from, size, &written, nullptr) && written == size;
}

// The bundle's files, if it is the folder's as listed now (else empty).
inline std::vector<BundledFile> FromBundle(const std::wstring& path, const std::vector<Listed>& listed) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::vector<BundledFile> files;
    char magic[8];
    uint32_t count = 0, dataSize = 0;
    bool ok = ReadAll(file, magic, 8) && memcmp(magic, kMagic, 8) == 0 && ReadAll(file, &count, 4) && ReadAll(file, &dataSize, 4) &&
              count == listed.size();
    std::vector<uint32_t> offsets(ok ? count : 0);
    for (uint32_t i = 0; ok && i < count; ++i) {
        uint16_t length = 0;
        uint32_t size = 0, offset = 0;
        uint64_t time = 0;
        ok = ReadAll(file, &length, 2) && length == listed[i].name.size();
        std::wstring name(ok ? length : 0, L'\0');
        ok = ok && ReadAll(file, name.data(), length * 2u) && ReadAll(file, &size, 4) && ReadAll(file, &time, 8) && ReadAll(file, &offset, 4) &&
             _wcsicmp(name.c_str(), listed[i].name.c_str()) == 0 && size == listed[i].size && time == listed[i].time &&
             offset + static_cast<uint64_t>(size) <= dataSize;
        if (ok) offsets[i] = offset;
    }
    void* data = ok && dataSize ? _aligned_malloc(dataSize, 16) : nullptr;
    ok = ok && data && ReadAll(file, data, dataSize);
    CloseHandle(file);
    if (!ok) {
        if (data) _aligned_free(data);
        return {};
    }
    files.reserve(count);
    for (uint32_t i = 0; i < count; ++i) files.push_back({listed[i].name, static_cast<char*>(data) + offsets[i], listed[i].size});
    return files;
}

inline void* ReadOne(const std::wstring& path, unsigned size) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return nullptr;
    void* data = _aligned_malloc(size, 16);
    const bool ok = data && ReadAll(file, data, size);
    CloseHandle(file);
    if (!ok && data) {
        _aligned_free(data);
        data = nullptr;
    }
    return data;
}

// Writes the bundle (to a new file, then put in place); a bundle that can't be written is simply not used.
inline void WriteBundle(const std::wstring& path, const std::vector<Listed>& listed, const std::vector<BundledFile>& files) {
    uint32_t dataSize = 0;
    std::vector<uint32_t> offsets;
    for (const BundledFile& f : files) {
        offsets.push_back(dataSize);
        dataSize = Align16(dataSize + f.size);
    }
    const std::wstring temporary = path + L".new";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const uint32_t count = static_cast<uint32_t>(files.size());
    bool ok = WriteAll(file, kMagic, 8) && WriteAll(file, &count, 4) && WriteAll(file, &dataSize, 4);
    for (size_t i = 0; ok && i < files.size(); ++i) {
        const uint16_t length = static_cast<uint16_t>(listed[i].name.size());
        ok = WriteAll(file, &length, 2) && WriteAll(file, listed[i].name.data(), length * 2u) && WriteAll(file, &listed[i].size, 4) &&
             WriteAll(file, &listed[i].time, 8) && WriteAll(file, &offsets[i], 4);
    }
    static const char zeros[16] = {};
    uint32_t at = 0;
    for (size_t i = 0; ok && i < files.size(); ++i) {
        ok = WriteAll(file, files[i].data, files[i].size);
        at += files[i].size;
        const uint32_t pad = Align16(at) - at;
        ok = ok && (pad == 0 || WriteAll(file, zeros, pad));
        at += pad;
    }
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) DeleteFileW(temporary.c_str());
}

}  // namespace bundle_detail

// Every `pattern` file in `folder` (which ends in \), from <folder>.bundle when it matches them, else each from its own
// file (and the bundle is written for next time). `fromBundle` says which. Files that couldn't be read are left out
// and counted in `failed`.
inline std::vector<BundledFile> ReadFolderBundled(const std::wstring& folder, const wchar_t* pattern, bool* fromBundle = nullptr,
                                                  unsigned* failed = nullptr) {
    using namespace bundle_detail;
    if (fromBundle) *fromBundle = false;
    if (failed) *failed = 0;
    std::vector<Listed> listed;
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW((folder + pattern).c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return {};
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (found.nFileSizeHigh || found.nFileSizeLow == 0 || found.nFileSizeLow > kMostFile) {
            if (failed) ++*failed;
            continue;
        }
        const uint64_t time = (static_cast<uint64_t>(found.ftLastWriteTime.dwHighDateTime) << 32) | found.ftLastWriteTime.dwLowDateTime;
        listed.push_back({found.cFileName, found.nFileSizeLow, time});
    } while (FindNextFileW(search, &found));
    FindClose(search);
    std::sort(listed.begin(), listed.end(), [](const Listed& a, const Listed& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });

    std::wstring bundle = folder;
    while (!bundle.empty() && (bundle.back() == L'\\' || bundle.back() == L'/')) bundle.pop_back();
    bundle += L".bundle";
    std::vector<BundledFile> files = FromBundle(bundle, listed);
    if (!files.empty() || listed.empty()) {
        if (fromBundle) *fromBundle = !files.empty();
        return files;
    }

    std::vector<Listed> read;
    for (const Listed& l : listed) {
        if (void* data = ReadOne(folder + l.name, l.size)) {
            files.push_back({l.name, data, l.size});
            read.push_back(l);
        } else if (failed) {
            ++*failed;
        }
    }
    if (read.size() == listed.size()) WriteBundle(bundle, read, files);
    return files;
}

}  // namespace rfa
