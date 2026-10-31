// rfa_loader.dll: injected into the game by RFAModLauncher before any game code runs.
// Loads every DLL in the mods folder (alphabetically) and calls its RFAMod_Init export.
// Also writes a crash report (log lines + logs\crash-*.dmp) if the game crashes, lets files in
// overrides\ replace files inside the game's archives, and gives the game's menu scripts access
// to rfa_mods.ini.
#define RFA_MOD_NAME "loader"
#include "rfa_mod.h"
#include "rfa_game.h"
#include <DbgHelp.h>
#include <algorithm>
#include <map>
#include <vector>

#pragma comment(linker, "/EXPORT:RFALoader_Init=_RFALoader_Init@4")

namespace {

// Catches crashes in a mod's init so they end up in the log instead of a silent exit.
bool CallModInit(void(__cdecl* init)()) {
    __try {
        init();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ---------------------------------------------------------------------------------------------
// Crash reports

using MiniDumpWriteDumpFn = decltype(&MiniDumpWriteDump);
MiniDumpWriteDumpFn g_writeDump;
LPTOP_LEVEL_EXCEPTION_FILTER g_gameFilter;  // the game's own handler, still called after ours
decltype(&SetUnhandledExceptionFilter) o_SetUnhandledExceptionFilter = SetUnhandledExceptionFilter;

struct CrashJob {
    EXCEPTION_POINTERS* exception;
    DWORD threadId;
};

// "module+offset" for an address, e.g. "amdxx32.dll+00DEA307".
void DescribeAddress(DWORD address, char* out, size_t size) {
    HMODULE module = nullptr;
    char path[MAX_PATH];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module) &&
        GetModuleFileNameA(module, path, MAX_PATH)) {
        const char* name = strrchr(path, '\\');
        snprintf(out, size, "%s+%08lX", name ? name + 1 : path, address - reinterpret_cast<DWORD>(module));
    } else {
        snprintf(out, size, "%08lX", address);
    }
}

// True if `address` points into the code of a loaded exe or DLL. Such values on the stack are
// mostly return addresses, so they show which functions led to the crash (some may be stale).
bool LooksLikeReturnAddress(DWORD address) {
    MEMORY_BASIC_INFORMATION mbi;
    const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return address >= 0x10000 && VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) &&
           mbi.Type == MEM_IMAGE && (mbi.Protect & exec);
}

void LogCrash(const CrashJob& job) {
    const EXCEPTION_RECORD* rec = job.exception->ExceptionRecord;
    const CONTEXT* ctx = job.exception->ContextRecord;
    char where[MAX_PATH + 16];
    DescribeAddress(reinterpret_cast<DWORD>(rec->ExceptionAddress), where, sizeof(where));
    rfa::Log("CRASH: exception %08lX at %s (thread %lu)", rec->ExceptionCode, where, job.threadId);
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
        ULONG_PTR kind = rec->ExceptionInformation[0];
        rfa::Log("  while %s address %08lX", kind == 8 ? "executing" : kind == 1 ? "writing" : "reading",
                 static_cast<DWORD>(rec->ExceptionInformation[1]));
    }
    rfa::Log("  EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX EBP=%08lX ESP=%08lX", ctx->Eax,
             ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp);

    // Probable callers: values on the stack that look like return addresses.
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reinterpret_cast<void*>(ctx->Esp), &mbi, sizeof(mbi))) return;
    auto top = reinterpret_cast<const DWORD*>(ctx->Esp);
    auto end = reinterpret_cast<const DWORD*>(static_cast<BYTE*>(mbi.BaseAddress) + mbi.RegionSize);
    int found = 0;
    for (const DWORD* p = top; p < end && p < top + 4096 && found < 30; ++p) {
        if (!LooksLikeReturnAddress(*p)) continue;
        DescribeAddress(*p, where, sizeof(where));
        rfa::Log("  stack+%04X: %s", static_cast<unsigned>((p - top) * 4), where);
        ++found;
    }
}

void WriteDump(const CrashJob& job) {
    if (!g_writeDump) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[64];
    swprintf_s(name, L"\\logs\\crash-%04d%02d%02d-%02d%02d%02d.dmp", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
               t.wSecond);
    const std::wstring path = rfa::RootDir() + name;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION info{job.threadId, job.exception, FALSE};
    auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                           MiniDumpWithUnloadedModules);
    BOOL ok = g_writeDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &info, nullptr, nullptr);
    CloseHandle(file);
    rfa::Log("  crash dump %s: %ls", ok ? "written" : "FAILED", path.c_str());
}

// Runs on a fresh thread: after a stack overflow the crashed thread has almost no stack left.
DWORD WINAPI WriteCrashReport(LPVOID param) {
    const auto& job = *static_cast<CrashJob*>(param);
    LogCrash(job);
    WriteDump(job);
    return 0;
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* exception) {
    static LONG reported = 0;
    if (InterlockedExchange(&reported, 1) == 0) {
        CrashJob job{exception, GetCurrentThreadId()};
        if (HANDLE thread = CreateThread(nullptr, 0, WriteCrashReport, &job, 0, nullptr)) {
            WaitForSingleObject(thread, 30000);
            CloseHandle(thread);
        }
    }
    return g_gameFilter ? g_gameFilter(exception) : EXCEPTION_CONTINUE_SEARCH;
}

// Keeps our filter installed when the game sets its own; the game's still runs after ours.
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI Hk_SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter) {
    LPTOP_LEVEL_EXCEPTION_FILTER previous = g_gameFilter;
    g_gameFilter = filter;
    return previous;
}

// First-chance access violations can be handled by the code that caused them, so these are
// only noted (a few at most) in case the crash filter never gets to run.
LONG CALLBACK NoteAccessViolation(EXCEPTION_POINTERS* exception) {
    static LONG noted = 0;
    const EXCEPTION_RECORD* rec = exception->ExceptionRecord;
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && InterlockedIncrement(&noted) <= 5) {
        char where[MAX_PATH + 16];
        DescribeAddress(reinterpret_cast<DWORD>(rec->ExceptionAddress), where, sizeof(where));
        rfa::Log("First-chance access violation at %s (may be handled)", where);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void InstallCrashReporter() {
    if (!rfa::IniInt(L"Loader", L"CrashReports", 1)) return;
    if (HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll"))
        g_writeDump = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    g_gameFilter = SetUnhandledExceptionFilter(CrashFilter);
    bool hooked = rfa::HookImport(GetModuleHandleW(nullptr), "KERNEL32.dll", "SetUnhandledExceptionFilter",
                                  &Hk_SetUnhandledExceptionFilter, &o_SetUnhandledExceptionFilter);
    AddVectoredExceptionHandler(0, NoteAccessViolation);
    rfa::Log("Crash reports on (dumps go to logs\\crash-*.dmp)%s", hooked ? "" : "; could not hook the game's own handler");
}

// ---------------------------------------------------------------------------------------------
// Lua: script overrides and settings access for the game's menu scripts

using rfa::game::lua_State;
using LoadBufferFn = int(__cdecl*)(lua_State*, const char*, size_t, const char*);
using RegisterFn = void(__cdecl*)(lua_State*);
LoadBufferFn g_loadBuffer;           // the game's luaL_loadbuffer
RegisterFn g_registerGameFunctions;  // the game's function that registers its C functions with Lua
std::vector<HMODULE> g_mods; // loaded mods, told when a setting changes from the game's menus

// File name a chunk was loaded from, or empty for chunks made from code strings.
std::string ScriptFileName(const char* chunkName) {
    if (!chunkName) return "";
    if (*chunkName == '@' || *chunkName == '=') ++chunkName;
    size_t len = strlen(chunkName);
    if (len == 0 || len > 120 || strpbrk(chunkName, "\r\n ()=")) return "";
    const char* base = (std::max)(strrchr(chunkName, '\\'), strrchr(chunkName, '/'));
    std::string name = base ? base + 1 : chunkName;
    if (name.size() < 4 || _stricmp(name.c_str() + name.size() - 4, ".lua") != 0) name += ".lua";
    return name;
}

bool ReadWholeFile(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    out.resize(GetFileSize(f, nullptr));
    DWORD read = 0;
    bool ok = ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size();
    CloseHandle(f);
    return ok;
}

std::string Narrow(const std::wstring& text) {
    char out[MAX_PATH * 2];
    if (!WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, out, sizeof(out), nullptr, nullptr)) out[0] = '\0';
    return out;
}

// Like Narrow, but empty if some character has no equivalent in the game's (ANSI) file functions.
std::string NarrowExact(const std::wstring& text) {
    char out[MAX_PATH * 2];
    BOOL lossy = FALSE;
    if (!WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, out, sizeof(out), nullptr, &lossy) || lossy) return "";
    return out;
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return text;
}

// ---------------------------------------------------------------------------------------------
// Overrides: files in overrides\ that replace files inside the game's archives, which stay untouched.
//
// overrides\ mirrors the game folder. A folder named after an archive holds replacements for files
// inside that archive:
//   overrides\build\pc\cache\interface.vpp_pc\ui_options_gameplay.lua
// replaces ui_options_gameplay.lua from <game>\build\pc\cache\interface.vpp_pc. At startup the
// loader checks that each archive exists and really contains the file, and logs every override it
// will use.
//
// Lua scripts are swapped when the game compiles them (LoadBufferHook). Every other file is swapped
// when the game looks it up by name (FindFileHook): the loader hands back the replacement as a
// loose file on disk, which the game then reads like any other.
//
// The game asks for files by name, and some names are in several archives with different contents
// (a container in each multiplayer map, for example), of which the game uses the one in the archive
// it has loaded. An override only replaces the copy from its own archive: the loader notes the size
// of that copy at startup and swaps a lookup only when the game found a copy of that size. Archives
// with identical copies (every table is in both table.vpp_pc and misc.vpp_pc) share the override.

struct Override {
    std::wstring path;       // full path of the replacement
    std::string ansiPath;    // the same path for the game's own file functions, which take ANSI text
    std::string relative;    // path inside overrides\, for the log
    DWORD archiveSize = 0;   // size of the file it replaces, in its archive
    LONG used = 0;           // set the first time the game reads it, so the log says so only once
};
std::map<std::string, std::vector<Override>> g_overrides;  // lower-case file name -> replacements
size_t g_overrideCount = 0;

struct ArchiveFile {
    std::string name;
    DWORD size;
};

// The files inside a .vpp_pc archive (format version 6), with their sizes. Layout: a 2 KB header
// (file count and the size of each block at 0x154), then the directory (24 bytes per file, the
// size at +12) and then the names, each block starting on a 2 KB boundary.
bool ReadArchiveDirectory(const std::wstring& archive, std::vector<ArchiveFile>& files) {
    HANDLE f = CreateFileW(archive.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    struct Header {
        DWORD magic, version;
        BYTE unused[0x154 - 8];
        DWORD fileCount, archiveSize, directorySize, namesSize;
    } header{};
    struct Entry {
        DWORD nameOffset, zero, dataOffset, size, compressedSize, zero2;
    };
    DWORD read = 0;
    bool ok = ReadFile(f, &header, sizeof(header), &read, nullptr) && read == sizeof(header) &&
              header.magic == 0x51890ACE && header.version == 6 && header.namesSize < 64 * 1024 * 1024 &&
              header.fileCount < 1024 * 1024 && header.directorySize >= header.fileCount * sizeof(Entry);
    std::vector<Entry> directory(ok ? header.fileCount : 0);
    std::string names(ok ? header.namesSize : 0, '\0');
    if (ok) {
        const DWORD directoryBytes = header.fileCount * sizeof(Entry);
        const DWORD namesStart = 0x800 + ((header.directorySize + 0x7FF) & ~0x7FFu);
        ok = SetFilePointer(f, 0x800, nullptr, FILE_BEGIN) == 0x800 &&
             ReadFile(f, directory.data(), directoryBytes, &read, nullptr) && read == directoryBytes &&
             SetFilePointer(f, namesStart, nullptr, FILE_BEGIN) == namesStart &&
             ReadFile(f, names.data(), header.namesSize, &read, nullptr) && read == header.namesSize;
    }
    CloseHandle(f);
    if (!ok) return false;
    for (const Entry& entry : directory) {
        if (entry.nameOffset >= names.size()) return false;
        files.push_back({names.c_str() + entry.nameOffset, entry.size});
    }
    return true;
}

// Checks one file in overrides\ and adds it to g_overrides. `relative` is its path inside overrides\.
void AddOverride(const std::wstring& root, const std::wstring& relative) {
    const std::string shown = Narrow(relative);
    const size_t slash = relative.find_last_of(L'\\');
    const std::wstring archive = slash == std::wstring::npos ? L"" : relative.substr(0, slash);
    const std::string name = Narrow(relative.substr(slash + 1));
    if (archive.size() < 7 || _wcsicmp(archive.c_str() + archive.size() - 7, L".vpp_pc") != 0) {
        rfa::Log("Override %s skipped: put it in the folder of the archive it comes from, "
                 "e.g. overrides\\build\\pc\\cache\\interface.vpp_pc\\%s", shown.c_str(), name.c_str());
        return;
    }
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring gameDir = exe;
    gameDir.resize(gameDir.find_last_of(L'\\'));
    std::vector<ArchiveFile> files;
    if (!ReadArchiveDirectory(gameDir + L"\\" + archive, files)) {
        rfa::Log("Override %s skipped: the game has no archive %ls", shown.c_str(), archive.c_str());
        return;
    }
    const std::string key = Lower(name);
    auto original = std::find_if(files.begin(), files.end(), [&](const ArchiveFile& f) { return Lower(f.name) == key; });
    if (original == files.end()) {
        rfa::Log("Override %s skipped: %ls has no file named %s", shown.c_str(), archive.c_str(), name.c_str());
        return;
    }
    if (auto sameName = g_overrides.find(key); sameName != g_overrides.end()) {
        for (const Override& other : sameName->second) {
            if (other.archiveSize == original->size) {
                rfa::Log("Override %s skipped: %s already overrides the same %s", shown.c_str(),
                         other.relative.c_str(), name.c_str());
                return;
            }
        }
    }
    const std::wstring path = root + L"\\" + relative;
    const std::string ansiPath = NarrowExact(path);
    if (ansiPath.empty() || ansiPath.size() >= sizeof(rfa::game::FileFindResult::name)) {
        rfa::Log("Override %s skipped: the game can't open files at this path (it must be shorter than 256 "
                 "characters, without letters outside your Windows language)", shown.c_str());
        return;
    }
    Override& added = g_overrides[key].emplace_back();
    added.path = path;
    added.ansiPath = ansiPath;
    added.relative = shown;
    added.archiveSize = original->size;
    ++g_overrideCount;
    rfa::Log("Override: %s", shown.c_str());
}

bool IsLuaScript(const std::string& name) {
    return name.size() > 4 && _stricmp(name.c_str() + name.size() - 4, ".lua") == 0;
}

// Walks overrides\ (and its subfolders). README files are notes for people, not overrides.
void FindOverrides(const std::wstring& root, const std::wstring& relative) {
    WIN32_FIND_DATAW fd;
    const std::wstring folder = relative.empty() ? root : root + L"\\" + relative;
    HANDLE find = FindFirstFileW((folder + L"\\*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring path = relative.empty() ? name : relative + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) FindOverrides(root, path);
        else if (_wcsnicmp(name.c_str(), L"README", 6) != 0) AddOverride(root, path);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

// Every Lua chunk the game compiles passes through here; an override with the same file name
// replaces it.
int __cdecl LoadBufferHook(lua_State* L, const char* buffer, size_t size, const char* chunkName) {
    const std::string name = ScriptFileName(chunkName);
    if (!name.empty()) {
        RFA_LOG_FIRST(60, "Lua script loaded: %s", chunkName);
        auto found = g_overrides.find(Lower(name));
        std::string text;
        if (found != g_overrides.end() && ReadWholeFile(found->second.front().path, text)) {
            rfa::Log("Using override %s", found->second.front().relative.c_str());
            return g_loadBuffer(L, text.data(), text.size(), chunkName);
        }
    }
    return g_loadBuffer(L, buffer, size, chunkName);
}

using rfa::game::FileFindResult;
using FindFileFn = bool(__cdecl*)(FileFindResult*, const char*, bool);
FindFileFn g_findFile;  // the game's FindFile

// Every file the game looks up by name passes through here. The game finds it first; if it found it
// in an archive and an override replaces that archive's copy (same size), the result is changed to
// point at the override instead.
bool __cdecl FindFileHook(FileFindResult* result, const char* name, bool flag) {
    const bool found = g_findFile(result, name, flag);
    if (!found || result->source != 1 || !name) return found;  // not found, or not inside an archive

    auto overrides = g_overrides.find(Lower(name));
    if (overrides == g_overrides.end() || IsLuaScript(overrides->first)) return found;
    auto o = std::find_if(overrides->second.begin(), overrides->second.end(),
                          [&](const Override& candidate) { return candidate.archiveSize == result->size; });
    if (o == overrides->second.end()) {
        RFA_LOG_FIRST(20, "Override %s not used here: the game loaded another archive's %s (%lu bytes)",
                      overrides->second.front().relative.c_str(), name, result->size);
        return found;
    }

    // Start the result again the way the game's FindFile does, then let the game's own loose-file
    // lookup fill it in for the replacement.
    const FileFindResult gameCopy = *result;
    result->source = -1;
    result->pathType = -1;
    result->name[0] = '\0';
    result->size = 0;
    result->archiveEntry = nullptr;
    const bool ok = rfa::game::FindLooseFile(result, o->ansiPath.c_str(), 0);
    if (InterlockedExchange(&o->used, 1) == 0)
        rfa::Log(ok ? "Using override %s" : "Override %s could not be opened; the game's own file is used",
                 o->relative.c_str());
    if (!ok) *result = gameCopy;
    return true;
}

void Widen(const char* text, wchar_t* out, int size) {
    if (!MultiByteToWideChar(CP_ACP, 0, text ? text : "", -1, out, size)) out[0] = L'\0';
}

// Lua: rfa_get_setting(section, key, default) -> number from rfa_mods.ini
int __cdecl LuaGetSetting(lua_State* L) {
    using namespace rfa::game;
    const double fallback = lua_gettop(L) >= 3 ? lua_tonumber(L, 3) : 0.0;
    wchar_t section[64], key[64];
    Widen(lua_tolstring(L, 1, nullptr), section, 64);
    Widen(lua_tolstring(L, 2, nullptr), key, 64);
    lua_pushnumber(L, rfa::IniInt(section, key, static_cast<int>(fallback)));
    return 1;
}

// Lua: rfa_set_setting(section, key, value) saves to rfa_mods.ini and tells every mod about it.
int __cdecl LuaSetSetting(lua_State* L) {
    using namespace rfa::game;
    const char* section = lua_tolstring(L, 1, nullptr);
    const char* key = lua_tolstring(L, 2, nullptr);
    if (!section || !key) return 0;
    const double number = lua_tonumber(L, 3);
    const int value = static_cast<int>(number + (number < 0 ? -0.5 : 0.5));
    wchar_t sectionW[64], keyW[64];
    Widen(section, sectionW, 64);
    Widen(key, keyW, 64);
    if (rfa::IniInt(sectionW, keyW, value + 1) == value) return 0;  // unchanged
    WritePrivateProfileStringW(sectionW, keyW, std::to_wstring(value).c_str(), rfa::IniPath().c_str());
    rfa::Log("Menu changed [%s] %s=%d", section, key, value);
    using SettingChangedFn = void(__cdecl*)(const char*, const char*);
    for (HMODULE mod : g_mods)
        if (auto changed = reinterpret_cast<SettingChangedFn>(GetProcAddress(mod, "RFAMod_SettingChanged")))
            changed(section, key);
    return 0;
}

// Lua: rfa_log(text) writes to logs\rfa_mods.log, handy when editing override scripts.
int __cdecl LuaLog(lua_State* L) {
    const char* text = rfa::game::lua_tolstring(L, 1, nullptr);
    rfa::Log("lua: %s", text ? text : "(nil)");
    return 0;
}

// Runs instead of the game's function that registers its C functions with the menu Lua state:
// lets it do that, then adds ours.
void __cdecl RegisterLuaFunctions(lua_State* L) {
    using namespace rfa::game;
    g_registerGameFunctions(L);
    lua_register(L, "rfa_get_setting", LuaGetSetting);
    lua_register(L, "rfa_set_setting", LuaSetSetting);
    lua_register(L, "rfa_log", LuaLog);
    rfa::Log("Added rfa_get_setting, rfa_set_setting and rfa_log to the game's menu Lua");
}

void InstallOverrides() {
    using namespace rfa::game;
    // "ScriptOverrides" is the setting's old name, from when only Lua scripts could be overridden.
    if (!rfa::IniInt(L"Loader", L"Overrides", rfa::IniInt(L"Loader", L"ScriptOverrides", 1))) {
        rfa::Log("Overrides: off ([Loader] Overrides=0)");
        return;
    }
    FindOverrides(rfa::RootDir() + L"\\overrides", L"");
    g_loadBuffer = reinterpret_cast<LoadBufferFn>(
        rfa::HookFunction(kLuaLoadBuffer, kLuaLoadBufferPrologue, reinterpret_cast<void*>(&LoadBufferHook)));
    g_findFile = reinterpret_cast<FindFileFn>(
        rfa::HookFunction(kFindFile, kFindFilePrologue, reinterpret_cast<void*>(&FindFileHook)));
    rfa::Log("Overrides: %u in overrides\\; Lua scripts %s, other files %s", static_cast<unsigned>(g_overrideCount),
             g_loadBuffer ? "on" : "FAILED", g_findFile ? "on" : "FAILED");
}

void InstallLuaFunctions() {
    using namespace rfa::game;
    g_registerGameFunctions = reinterpret_cast<RegisterFn>(rfa::HookFunction(
        kRegisterGameLuaFunctions, kRegisterGameLuaFunctionsPrologue, reinterpret_cast<void*>(&RegisterLuaFunctions)));
    if (!g_registerGameFunctions) rfa::Log("Could not hook the game's Lua registration; rfa_* functions are off");
}

}  // namespace

// Called by the launcher on a remote thread while the game's main thread is parked.
// Returns the number of mods loaded.
extern "C" DWORD WINAPI RFALoader_Init(LPVOID) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    rfa::Log("Loader running in %ls (pid %lu)", exe, GetCurrentProcessId());
    rfa::Log("Game build: %s", rfa::game::BuildDescription());
    InstallCrashReporter();
    if (rfa::game::IsSupportedBuild()) {
        InstallOverrides();
        InstallLuaFunctions();
    }

    const std::wstring dir = rfa::RootDir() + L"\\mods\\";
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"*.dll").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) files.push_back(fd.cFileName);
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    std::sort(files.begin(), files.end());

    DWORD loaded = 0;
    for (const auto& file : files) {
        const std::wstring name = file.substr(0, file.size() - 4);
        if (!rfa::IniInt(L"Mods", name.c_str(), 1)) {
            rfa::Log("Skipping %ls (disabled in [Mods])", file.c_str());
            continue;
        }
        HMODULE mod = LoadLibraryW((dir + file).c_str());
        if (!mod) {
            rfa::Log("FAILED to load %ls (error %lu)", file.c_str(), GetLastError());
            continue;
        }
        auto init = reinterpret_cast<void(__cdecl*)()>(GetProcAddress(mod, "RFAMod_Init"));
        if (init && !CallModInit(init)) {
            rfa::Log("%ls crashed in RFAMod_Init", file.c_str());
            continue;
        }
        rfa::Log("Loaded %ls", file.c_str());
        g_mods.push_back(mod);
        ++loaded;
    }
    return loaded;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
