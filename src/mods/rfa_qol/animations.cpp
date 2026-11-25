// Animations from a folder: every `.anim_pc` in `mods\animations\` is put in the game's animation library at its
// start, in place of the game's animation of that name (wherever the game keeps its own: anims.vpp_pc, a level's or a
// vehicle's .str2_pc container, the DLC's), or as a new animation when the game has none of that name. A part of
// rfa_qol.dll; [Mods] animations=0 turns it off. The Blender add-on's "Export to Game" writes here.
//
// The library (0x34C7570, 4500 places of 32 bytes: +0x0C flags, bit 0 "kept"; +0x18 the size, +0x1C the data) is
// emptied once, at the game's start (0x967B90, run once: 0xD89F00 from 0x967BD2), before the animation archives load
// (0x95DF70) and the sets that name them (0x96A9D0). Every way the game puts data in a place goes through 0xD8C000
// (place, data, size, keep): anims.vpp_pc's preload (0x8E800D, with the whole file, read into a 16-aligned buffer)
// and the containers' streaming alike. It leaves a place that is kept or already has data as it is; the streaming
// skips loading into a place that has data (0xD8C220), and the unloading (0xD8C120) skips kept places.
// So the call at 0x967BD2 is pointed here: the library is emptied as before, then each file of the folder is read
// into a 16-aligned buffer that is never freed, given a place by its name (0xD875F0: the place of that name, or a new
// one) and put there kept (0xD8C0F0 (name, data, size, keep): through 0xD8C000, which also resolves the file's own
// offsets). The game's own animation of that name then never loads over it.
//
// A new name plays only where an anim set names it (a state or action of a Skeleton_Set table, "name.animx"), as the
// game's own do: the editor's table editor makes those.
#define RFA_MOD_NAME "animations"
#include "rfa_mod.h"
#include "rfa_qol.h"

#include <malloc.h>
#include <cstring>
#include <string>

namespace animations {

namespace {

constexpr DWORD kEmptyLibraryCall = 0x967BD2;  // call 0xD89F00, in 0x967B90
constexpr DWORD kEmptyLibrary = 0xD89F00;
constexpr DWORD kFindAnimation = 0xD8C2C0;     // cdecl (name) -> place, or -1
constexpr DWORD kNewAnimation = 0xD875F0;      // cdecl (name) -> place (the one of that name, or a new one), or -1
constexpr DWORD kOverlayAnimation = 0xD8C0F0;  // cdecl (name, data, size, keep) -> place, or -1
constexpr size_t kLongestName = 58;            // "stem.anim"
constexpr DWORD kLargestFile = 64u << 20;

using EmptyLibraryFn = void(__cdecl*)();
using FindAnimationFn = int(__cdecl*)(const char* name);
using NewAnimationFn = int(__cdecl*)(const char* name);
using OverlayAnimationFn = int(__cdecl*)(const char* name, void* data, DWORD size, bool keep);

std::wstring Folder() { return rfa::RootDir() + L"\\mods\\animations"; }

// The whole file in a 16-aligned buffer (as the game reads them), or null.
void* ReadAnimation(const std::wstring& path, DWORD& size) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return nullptr;
    LARGE_INTEGER length{};
    void* data = nullptr;
    DWORD read = 0;
    if (GetFileSizeEx(file, &length) && length.QuadPart >= 0x30 && length.QuadPart <= kLargestFile &&
        (data = _aligned_malloc(static_cast<size_t>(length.QuadPart), 16)) != nullptr &&
        (!ReadFile(file, data, static_cast<DWORD>(length.QuadPart), &read, nullptr) || read != length.QuadPart)) {
        _aligned_free(data);
        data = nullptr;
    }
    CloseHandle(file);
    size = read;
    return data;
}

void LoadFolder() {
    const std::wstring folder = Folder();
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW((folder + L"\\*.anim_pc").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return;
    int replaced = 0, added = 0, failed = 0;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring stem = found.cFileName;
        stem.resize(stem.size() - 8);  // ".anim_pc"
        char name[kLongestName + 8] = {};
        const int length = WideCharToMultiByte(CP_ACP, 0, stem.c_str(), -1, name, sizeof(name) - 5, nullptr, nullptr);
        if (length <= 1 || length - 1 + 5 > static_cast<int>(kLongestName)) {
            rfa::Log("Animations: %ls is skipped: its name is longer than %u characters", found.cFileName, unsigned(kLongestName - 5));
            ++failed;
            continue;
        }
        for (char* c = name; *c; ++c) *c = static_cast<char>(tolower(static_cast<unsigned char>(*c)));
        strcat_s(name, ".anim");
        DWORD size = 0;
        void* data = ReadAnimation(folder + L"\\" + found.cFileName, size);
        const BYTE* bytes = static_cast<const BYTE*>(data);
        if (!data || memcmp(bytes, "ANIM", 4) != 0 || bytes[4] != 10) {
            rfa::Log("Animations: %ls is skipped: it isn't an animation file (version 10) that could be read", found.cFileName);
            if (data) _aligned_free(data);
            ++failed;
            continue;
        }
        const bool had = reinterpret_cast<FindAnimationFn>(kFindAnimation)(name) != -1;
        if (reinterpret_cast<NewAnimationFn>(kNewAnimation)(name) == -1 ||
            reinterpret_cast<OverlayAnimationFn>(kOverlayAnimation)(name, data, size, true) == -1) {
            rfa::Log("Animations: %s has no place in the game's animation library (it holds 4500)", name);
            _aligned_free(data);
            ++failed;
            continue;
        }
        // The buffer stays the library's for as long as the game runs.
        rfa::LogDetail("Animations: %s (%lu bytes) %s", name, size, had ? "in place of the game's" : "added");
        ++(had ? replaced : added);
    } while (FindNextFileW(search, &found));
    FindClose(search);
    rfa::Log("Animations from %ls: %d in place of the game's, %d new%s", folder.c_str(), replaced, added,
             failed ? " (some skipped: see above)" : "");
}

void __cdecl EmptyLibraryThenLoad() {
    reinterpret_cast<EmptyLibraryFn>(kEmptyLibrary)();
    LoadFolder();
}

}  // namespace

void Init() {
    const BYTE* call = reinterpret_cast<const BYTE*>(kEmptyLibraryCall);
    if (call[0] != 0xE8 || kEmptyLibraryCall + 5 + rfa::CodeValue(kEmptyLibraryCall + 1) != kEmptyLibrary) {
        rfa::Log("Animations: the game's start isn't as expected; animations in mods\\animations aren't loaded");
        return;
    }
    BYTE code[5];
    rfa::MakeJump(code, call, reinterpret_cast<const void*>(&EmptyLibraryThenLoad));
    code[0] = 0xE8;  // a call, as it was
    if (!rfa::PatchCode(const_cast<BYTE*>(call), code, sizeof(code))) {
        rfa::Log("Animations: couldn't change the game's start; animations in mods\\animations aren't loaded");
        return;
    }
    rfa::LogDetail("Animations: the files in mods\\animations are loaded at the game's start");
}

}  // namespace animations
