// Shared helpers for RFA mods: paths, config, logging and hooking.
// Header-only, so every mod DLL gets its own copy. Define RFA_MOD_NAME before including.
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

// Appends one line to logs\rfa_mods.log. Every module appends to the same file.
inline void Log(const char* fmt, ...) {
    static HANDLE file = [] {
        CreateDirectoryW((RootDir() + L"\\logs").c_str(), nullptr);
        return CreateFileW(LogPath().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }();
    char line[2048];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = snprintf(line, sizeof(line), "[%02d:%02d:%02d.%03d] [%s] ", t.wHour, t.wMinute, t.wSecond,
                     t.wMilliseconds, RFA_MOD_NAME);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof(line) - n - 2, fmt, ap);
    va_end(ap);
    strcat_s(line, "\r\n");
    DWORD written;
    if (file != INVALID_HANDLE_VALUE) WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    OutputDebugStringA(line);
}

// Log only the first `limit` times this line is reached (for hooks that may fire every frame).
#define RFA_LOG_FIRST(limit, ...)                \
    do {                                         \
        static int rfa_count_;                   \
        if (rfa_count_ < (limit)) {              \
            ++rfa_count_;                        \
            rfa::Log(__VA_ARGS__);               \
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
