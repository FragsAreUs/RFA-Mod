// Shared helpers for RFA mods: paths, config, logging and hooking.
// Header-only, so every mod DLL gets its own copy. Define RFA_MOD_NAME before including: it is the
// name each line logged from that source file starts with.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#ifndef RFA_MOD_NAME
#define RFA_MOD_NAME "mod"
#endif

namespace rfa {

// Folder that holds rfa_mods.ini. Found by walking up from this module's own location,
// so it works for the launcher/loader (bin\) and for mods (bin\mods\).
inline const std::wstring& RootDir() {
    static int marker;
    static const std::wstring root = [] {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&marker), &self);
        wchar_t buf[MAX_PATH] = {};
        GetModuleFileNameW(self, buf, MAX_PATH);
        std::wstring dir = buf;
        dir.resize(dir.find_last_of(L'\\'));
        std::wstring d = dir;
        for (int i = 0; i < 3; ++i) {
            if (GetFileAttributesW((d + L"\\rfa_mods.ini").c_str()) != INVALID_FILE_ATTRIBUTES) return d;
            size_t slash = d.find_last_of(L'\\');
            if (slash == std::wstring::npos || slash < 3) break;
            d.resize(slash);
        }
        return dir;
    }();
    return root;
}

inline std::wstring IniPath() { return RootDir() + L"\\rfa_mods.ini"; }

inline int IniInt(const wchar_t* section, const wchar_t* key, int def) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, def, IniPath().c_str()));
}

inline std::wstring IniString(const wchar_t* section, const wchar_t* key, const wchar_t* def = L"") {
    wchar_t buf[1024];
    GetPrivateProfileStringW(section, key, def, buf, 1024, IniPath().c_str());
    return buf;
}

inline std::wstring LogPath() { return RootDir() + L"\\logs\\rfa_mods.log"; }

// rfa_loader.dll's log (RFALoader_Log): it queues a line and a thread of its own writes it, so no game
// thread waits on the disk. Null outside the game (the launcher) or with an older loader: the line is
// then written by the caller.
using LoaderLogFn = void(__cdecl*)(const char* line, unsigned length);
inline LoaderLogFn LoaderLog() {
    static const LoaderLogFn log = [] {
        const HMODULE loader = GetModuleHandleW(L"rfa_loader.dll");
        return loader ? reinterpret_cast<LoaderLogFn>(GetProcAddress(loader, "RFALoader_Log")) : nullptr;
    }();
    return log;
}

// Appends one line to logs\rfa_mods.log, starting with `module`. Every module appends to the same file
// (through the loader's queue in the game).
inline void LogAs(const char* module, const char* fmt, va_list ap) {
    char line[2048];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = snprintf(line, sizeof(line), "[%02d:%02d:%02d.%03d] [%s] ", t.wHour, t.wMinute, t.wSecond,
                     t.wMilliseconds, module);
    vsnprintf(line + n, sizeof(line) - n - 2, fmt, ap);
    strcat_s(line, "\r\n");
    const unsigned length = static_cast<unsigned>(strlen(line));
    if (const LoaderLogFn log = LoaderLog()) {
        log(line, length);
    } else {
        static HANDLE file = [] {
            CreateDirectoryW((RootDir() + L"\\logs").c_str(), nullptr);
            return CreateFileW(LogPath().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }();
        DWORD written;
        if (file != INVALID_HANDLE_VALUE) WriteFile(file, line, length, &written, nullptr);
    }
    OutputDebugStringA(line);
}

// Appends one line to logs\rfa_mods.log, starting with RFA_MOD_NAME. It is in an unnamed namespace
// so that every source file has its own copy with its own name: a DLL made of several parts, each
// in its own file (rfa_qol.dll), logs each part under that part's name.
namespace {
inline void Log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    LogAs(RFA_MOD_NAME, fmt, ap);
    va_end(ap);
}
}  // namespace

// Log only the first `limit` times this line is reached (for hooks that may fire every frame).
#define RFA_LOG_FIRST(limit, ...)                \
    do {                                         \
        static int rfa_count_;                   \
        if (rfa_count_ < (limit)) {              \
            ++rfa_count_;                        \
            rfa::Log(__VA_ARGS__);               \
        }                                        \
    } while (0)

// ---- What is logged, and what only on request
//
// rfa::Log is for what a player or we need to see every time: what a mod found, read and hooked as it
// started (a line or a few), real problems (errors, failed hooks, crashes, stuck frames) and rare
// changes a player cares about (a district liberated, a level loaded). The console shows these too.
//
// rfa::LogDetail is for everything routine: reports every few seconds, a line for each spawn, sound,
// prop, path or file. Those lines are written only while the mod's own switch is on (Log= in its
// section of rfa_mods.ini, read as the mod starts with rfa::ReadLogSwitch), and are marked
// "[module:log]" so the console leaves them out: they go to logs\rfa_mods.log only. A mod's narrower
// switches (DriveLog=, PropLog=, ...) add their own detail lines and need Log=1 too.

// A DLL holding one mod has one switch, which its RFAMod_Init reads. A DLL holding several mods, each with its
// own Log= (rfa_guerrilla.dll's nine parts), has one switch per part. A part's source files all log under the
// part's name (RFA_MOD_NAME), and they follow that name's switch once a file with that name has read it. A name
// whose switch nobody read follows the DLL's switch, which is the one read last. That is how rfa_qol.dll's
// parts all follow [Loader] Log=, which its main file reads.

// The DLL's switch: the one read last, by any of its source files.
inline bool& DetailLogSwitch() {
    static bool on = false;
    return on;
}

// The switch of one name: whether a source file with that name has read it yet, and whether it is on.
struct LogSwitch {
    bool read = false;
    bool on = false;
};

// A number for a name (FNV-1a), the same in every source file, so that all the files with one name share one switch.
constexpr unsigned LogNameKey(const char* name) {
    unsigned key = 2166136261u;
    for (; *name; ++name) key = (key ^ static_cast<unsigned char>(*name)) * 16777619u;
    return key;
}

// The switch of the name whose number is Key: one per name in each DLL.
template <unsigned Key>
LogSwitch& NamedLogSwitch() {
    static LogSwitch named;
    return named;
}

namespace {
// The switch of this source file's name (RFA_MOD_NAME).
inline LogSwitch& OwnLogSwitch() { return NamedLogSwitch<LogNameKey(RFA_MOD_NAME)>(); }

// Whether this source file writes its detail lines: its name's switch once read, else the DLL's. Code that does
// work only in order to log it checks this first.
inline bool DetailLog() {
    const LogSwitch& own = OwnLogSwitch();
    return own.read ? own.on : DetailLogSwitch();
}

// Reads Log= in `section` (off when missing) as this file's name's switch, and the DLL's, and returns it.
inline bool ReadLogSwitch(const wchar_t* section) {
    LogSwitch& own = OwnLogSwitch();
    own.on = IniInt(section, L"Log", 0) != 0;
    own.read = true;
    DetailLogSwitch() = own.on;
    return own.on;
}

// One of a mod's narrower switches (DriveLog=, PropLog=, ...), which is on only when both it and the mod's Log= are.
// Call it after ReadLogSwitch.
inline bool ReadLogSwitch(const wchar_t* section, const wchar_t* key) {
    return DetailLog() && IniInt(section, key, 0) != 0;
}
}  // namespace

namespace {
// A routine line for logs\rfa_mods.log, starting with "[RFA_MOD_NAME:log]": written only while this
// DLL's switch is on, never shown in the console.
inline void LogDetail(const char* fmt, ...) {
    if (!DetailLog()) return;
    va_list ap;
    va_start(ap, fmt);
    LogAs(RFA_MOD_NAME ":log", fmt, ap);
    va_end(ap);
}
}  // namespace

// rfa::LogDetail only the first `limit` times this line is reached while the switch is on.
#define RFA_LOG_DETAIL_FIRST(limit, ...)         \
    do {                                         \
        static int rfa_count_;                   \
        if (rfa::DetailLog() && rfa_count_ < (limit)) { \
            ++rfa_count_;                        \
            rfa::LogDetail(__VA_ARGS__);         \
        }                                        \
    } while (0)

// Overwrite one pointer-sized slot in protected memory (import tables, COM vtables).
inline bool WritePointer(void** slot, void* value) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(slot, &mbi, sizeof(mbi))) return false;
    const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), (mbi.Protect & exec) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &old))
        return false;
    *slot = value;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

// Overwrite bytes in the game's code and flush the instruction cache.
inline bool PatchCode(void* address, const void* bytes, size_t size) {
    DWORD old;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(address, bytes, size);
    VirtualProtect(address, size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), address, size);
    return true;
}

// The 4-byte value at `address` in the game's code or data.
inline DWORD CodeValue(DWORD address) { return *reinterpret_cast<const DWORD*>(address); }

// Moves one of the game's tables (`name` in the log) from `game`, of `holds` entries of `entrySize`
// bytes, to `table`, of `newHolds`: each of `references` (the 4-byte values in the game's code that
// hold the table's address, or a field's in its first entry) gets the new table's, each of `limits`
// the new size. All are checked against the game's values first, and none changed if one differs.
template <size_t References, size_t Limits>
bool MoveTable(const char* name, DWORD game, DWORD entrySize, DWORD holds, const DWORD (&references)[References],
               const DWORD (&limits)[Limits], BYTE* table, DWORD newHolds) {
    for (DWORD at : references) {
        const DWORD value = CodeValue(at);
        if (value < game || value >= game + entrySize) {
            rfa::Log("%s: the game's code at 0x%08lX isn't as expected (0x%08lX); the game keeps its %lu", name, at, value, holds);
            return false;
        }
    }
    for (DWORD at : limits) {
        if (CodeValue(at) != holds) {
            rfa::Log("%s: the game's limit at 0x%08lX isn't %lu (it's %lu); the game keeps its own", name, at, holds, CodeValue(at));
            return false;
        }
    }
    const DWORD moved = reinterpret_cast<DWORD>(table);
    for (DWORD at : references) {
        const DWORD value = moved + (CodeValue(at) - game);
        if (!rfa::PatchCode(reinterpret_cast<void*>(at), &value, sizeof(value))) {
            rfa::Log("%s: could not change the game's code at 0x%08lX (part of it already points at the new table)", name, at);
            return false;
        }
    }
    for (DWORD at : limits) {
        if (!rfa::PatchCode(reinterpret_cast<void*>(at), &newHolds, sizeof(newHolds))) {
            rfa::Log("%s: could not change the game's limit at 0x%08lX", name, at);
            return false;
        }
    }
    rfa::Log("%s: the game holds %lu (its own %lu): the table moved to 0x%08lX, %u places in its code changed", name,
             newHolds, holds, moved, static_cast<unsigned>(References + Limits));
    return true;
}


// The one piece of machine code the hooks need: a 5-byte x86 "jump to target" instruction
// (opcode 0xE9, then the distance from the end of the instruction to the target).
inline void MakeJump(BYTE (&code)[5], const void* from, const void* to) {
    code[0] = 0xE9;
    const LONG distance = static_cast<LONG>(reinterpret_cast<DWORD>(to) - (reinterpret_cast<DWORD>(from) + 5));
    memcpy(code + 1, &distance, 4);
}

// Make every call to the game function at `address` run `replacement` instead. The replacement
// must take the same arguments with the same calling convention; the game's code never runs.
inline bool ReplaceFunction(DWORD address, const void* replacement) {
    BYTE jump[5];
    MakeJump(jump, reinterpret_cast<void*>(address), replacement);
    return PatchCode(reinterpret_cast<void*>(address), jump, sizeof(jump));
}

// Make every call to the game function at `address` run `hook`, which can still call the original
// through the returned pointer (or nullptr on failure). `prologueSize` is the length of the
// function's first few instructions (at least 5 bytes, whole instructions): they are copied out so
// the original can still run them before continuing in the game's code.
inline void* HookFunction(DWORD address, size_t prologueSize, const void* hook) {
    auto function = reinterpret_cast<BYTE*>(address);
    auto original = static_cast<BYTE*>(VirtualAlloc(nullptr, prologueSize + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!original) return nullptr;
    memcpy(original, function, prologueSize);
    BYTE back[5];
    MakeJump(back, original + prologueSize, function + prologueSize);
    memcpy(original + prologueSize, back, sizeof(back));
    if (!ReplaceFunction(address, hook)) {
        VirtualFree(original, 0, MEM_RELEASE);
        return nullptr;
    }
    return original;
}

// Redirect `module`'s import of dll!func to `hook`. `original` receives the previous target,
// which may already be another mod's hook, so hooks from several mods chain together.
// `func` may be "#N" for imports by ordinal.
inline bool HookImportRaw(HMODULE module, const char* dll, const char* func, void* hook, void** original) {
    const bool byOrdinal = func[0] == '#';
    const WORD ordinal = byOrdinal ? static_cast<WORD>(atoi(func + 1)) : 0;
    auto base = reinterpret_cast<BYTE*>(module);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    for (auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (_stricmp(reinterpret_cast<char*>(base + imp->Name), dll) != 0) continue;
        auto slot = reinterpret_cast<void**>(base + imp->FirstThunk);
        if (!imp->OriginalFirstThunk) {
            // No name table: match by the address the slot currently points to.
            void* target = GetProcAddress(GetModuleHandleA(dll), byOrdinal ? MAKEINTRESOURCEA(ordinal) : func);
            for (; *slot; ++slot) {
                if (*slot != target) continue;
                *original = *slot;
                return WritePointer(slot, hook);
            }
            continue;
        }
        for (auto name = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk); name->u1.AddressOfData;
             ++name, ++slot) {
            if (IMAGE_SNAP_BY_ORDINAL(name->u1.Ordinal)) {
                if (!byOrdinal || IMAGE_ORDINAL(name->u1.Ordinal) != ordinal) continue;
            } else {
                auto byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + name->u1.AddressOfData);
                if (byOrdinal || strcmp(reinterpret_cast<char*>(byName->Name), func) != 0) continue;
            }
            if (*slot == hook) return true;
            *original = *slot;
            return WritePointer(slot, hook);
        }
    }
    return false;
}

template <class Fn>
bool HookImport(HMODULE module, const char* dll, const char* func, Fn hook, Fn* original) {
    return HookImportRaw(module, dll, func, reinterpret_cast<void*>(hook), reinterpret_cast<void**>(original));
}

// Point slot `index` of a COM object's vtable at `hook`. Objects of the same class share one
// vtable, so this hooks every instance. `original` is only filled the first time; a second
// class whose slot points somewhere else is refused rather than mixed up.
//
// Do NOT use this on DXGI/D3D objects (swap chains, factories, devices). Overlays such as Steam's
// patch the same vtables and re-patch when the game makes a new swap chain; both hooks then
// treat each other as "the original" and recurse until the stack overflows. Prefer HookImport,
// or vtables that belong to the game itself.
template <class Fn>
bool HookVTable(void* object, int index, Fn hook, Fn* original) {
    void** slot = *reinterpret_cast<void***>(object) + index;
    if (*slot == reinterpret_cast<void*>(hook)) return true;
    if (*original && reinterpret_cast<void*>(*original) != *slot) return false;
    *original = reinterpret_cast<Fn>(*slot);
    return WritePointer(slot, reinterpret_cast<void*>(hook));
}

}  // namespace rfa
