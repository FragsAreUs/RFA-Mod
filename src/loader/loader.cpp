// rfa_loader.dll: injected into the game by RFAModLauncher before any game code runs.
// Loads every DLL in the mods folder (alphabetically) and calls its RFAMod_Init export.
// Also writes a crash report (log lines + logs\crash-*.dmp) if the game crashes, lets files in
// overrides\ replace files inside the game's archives, and gives the game's menu scripts access
// to rfa_mods.ini. Hosts the mods' in-game menus, drawn with Dear ImGui (menus.cpp).
#define RFA_MOD_NAME "loader"
#include "rfa_mod.h"
#include "rfa_game.h"
#include "menus.h"
#include <DbgHelp.h>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <map>
#include <set>
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
// The log, written off the game's threads
//
// Every mod's lines (rfa_mod.h's rfa::Log hands them to RFALoader_Log) are queued, in order, and a
// thread of the loader's own writes them to logs\rfa_mods.log every 20 ms, so no game thread waits on
// the disk. On a busy hard drive a write held the game's frame for seconds (the user's 18:53 run,
// 2026-11-15). A crash writes what is queued at once, and every line after it goes straight to the
// file (the process may end before the writer runs again). At exit, what is queued is written as the
// game calls ExitProcess (see WriteLogAsGameExits), and what is left at the loader's unloading.
constexpr DWORD kLogWriteEveryMs = 20;
CRITICAL_SECTION g_logQueueLock;  // the queue
CRITICAL_SECTION g_logWriteLock;  // the file: one batch at a time, in order
std::string g_logQueue;
HANDLE g_logFile = INVALID_HANDLE_VALUE;
volatile LONG g_logDirect = 0;   // set by a crash: lines are written at once from then on
volatile LONG g_logExiting = 0;  // set as the game exits: lines are written at once, without the locks
INIT_ONCE g_logOnce = INIT_ONCE_STATIC_INIT;

void WriteLogText(const std::string& text) {
    DWORD written;
    if (g_logFile != INVALID_HANDLE_VALUE && !text.empty())
        WriteFile(g_logFile, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}

// Writes what is queued (the write lock keeps a batch still being written ahead of the next).
void FlushLogQueue() {
    EnterCriticalSection(&g_logWriteLock);
    std::string text;
    EnterCriticalSection(&g_logQueueLock);
    text.swap(g_logQueue);
    LeaveCriticalSection(&g_logQueueLock);
    WriteLogText(text);
    LeaveCriticalSection(&g_logWriteLock);
}

DWORD WINAPI LogWriter(void*) {
    for (;;) {
        Sleep(kLogWriteEveryMs);
        FlushLogQueue();
    }
}

BOOL CALLBACK StartLog(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&g_logQueueLock);
    InitializeCriticalSection(&g_logWriteLock);
    CreateDirectoryW((rfa::RootDir() + L"\\logs").c_str(), nullptr);
    g_logFile = CreateFileW(rfa::LogPath().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (HANDLE thread = CreateThread(nullptr, 0, LogWriter, nullptr, 0, nullptr)) CloseHandle(thread);
    else InterlockedExchange(&g_logDirect, 1);  // no writer: every line written at once, as before
    return TRUE;
}

// A crash: the queue written now, and every line after it straight to the file.
void LogDirectFromNow() {
    InitOnceExecuteOnce(&g_logOnce, StartLog, nullptr, nullptr);
    InterlockedExchange(&g_logDirect, 1);
    if (!g_logExiting) FlushLogQueue();  // once the game exits, the queue is written already and the locks aren't used
}

// The game exits by ExitProcess (its _exit). Windows then ends every other thread and unloads the DLLs, the mods
// before the loader, so a mod that failed as it unloaded ended the game before the loader's DllMain wrote what was
// queued: a fail-fast (0xC0000409) skips the crash report too, and the log lost its last lines (the 2026-11-19 runs
// stopped at the game's scheduler_shutdown, a quarter of a second before its exit). So the queue is written here,
// while the other threads still run, and every line after it straight to the file. Those lines are written without
// the locks: a thread Windows ends next could be holding one (the writer, in the middle of a batch), and waiting for
// a lock whose owner has ended would hang the exit.
decltype(&ExitProcess) o_ExitProcess = ExitProcess;

void WINAPI WriteLogAsGameExits(UINT code) {
    InitOnceExecuteOnce(&g_logOnce, StartLog, nullptr, nullptr);
    InterlockedExchange(&g_logExiting, 1);
    FlushLogQueue();
    rfa::Log("The game exits (code %u)", code);
    o_ExitProcess(code);
}

void InstallLogAtExit() {
    if (!rfa::HookImport(GetModuleHandleW(nullptr), "KERNEL32.dll", "ExitProcess", &WriteLogAsGameExits, &o_ExitProcess))
        rfa::Log("Could not hook the game's ExitProcess: if a mod fails as the game exits, the log may lose its last lines");
}

// The process is ending (DllMain): the other threads are gone, perhaps holding the locks, so what is
// left is written without them.
void WriteLogAtExit() {
    std::string text;
    text.swap(g_logQueue);
    WriteLogText(text);
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
    const wchar_t* dumpSuffix;  // added to the dump's name: L"" for a crash, L"-first" for a first-chance fault
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

// True if `size` bytes from `address` can be read (committed, readable, not a guard page).
bool Readable(DWORD address, size_t size) {
    MEMORY_BASIC_INFORMATION mbi;
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY;
    return address >= 0x10000 && VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) &&
           mbi.State == MEM_COMMIT && (mbi.Protect & readable) && !(mbi.Protect & PAGE_GUARD) &&
           address + size <= reinterpret_cast<DWORD>(mbi.BaseAddress) + mbi.RegionSize;
}

// The call chain by the frame pointers, which the game's build keeps: [ebp] is the caller's ebp,
// [ebp + 4] the return address into the caller.
void LogFrames(const CONTEXT* ctx) {
    char where[MAX_PATH + 16];
    DWORD ebp = ctx->Ebp;
    for (int i = 0; i < 24 && Readable(ebp, 8); ++i) {
        const DWORD next = reinterpret_cast<const DWORD*>(ebp)[0], ret = reinterpret_cast<const DWORD*>(ebp)[1];
        if (!LooksLikeReturnAddress(ret)) break;
        DescribeAddress(ret, where, sizeof(where));
        rfa::Log("  frame %d: %s", i, where);
        if (next <= ebp) break;
        ebp = next;
    }
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
    LogFrames(ctx);

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
    swprintf_s(name, L"\\logs\\crash-%04d%02d%02d-%02d%02d%02d%s.dmp", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
               t.wSecond, job.dumpSuffix);
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
    LogDirectFromNow();
    if (InterlockedExchange(&reported, 1) == 0) {
        CrashJob job{exception, GetCurrentThreadId(), L""};
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
// only noted (a few at most) in case the crash filter never gets to run. One in the game's own
// code is reported in full (what it touched, the registers, the call chain) and the first such
// gets a dump of its own (logs\crash-*-first.dmp): the game can catch a fault in its code and
// quit, and the crash that may follow while it exits no longer shows where the fault was.
LONG CALLBACK NoteAccessViolation(EXCEPTION_POINTERS* exception) {
    static LONG noted = 0, dumped = 0;
    const EXCEPTION_RECORD* rec = exception->ExceptionRecord;
    // A stack overflow (calls that never stop calling each other) can end the game without the crash
    // filter ever running: report it here, from a fresh thread, as this one has no stack left.
    static LONG overflowed = 0;
    if (rec->ExceptionCode == EXCEPTION_STACK_OVERFLOW && InterlockedExchange(&overflowed, 1) == 0) {
        InterlockedExchange(&g_logDirect, 1);  // the writing is done on the report's fresh thread (no stack here)
        static CrashJob job;
        job = CrashJob{exception, GetCurrentThreadId(), L"-overflow"};
        if (HANDLE thread = CreateThread(nullptr, 0, WriteCrashReport, &job, 0, nullptr)) {
            WaitForSingleObject(thread, 30000);
            CloseHandle(thread);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && InterlockedIncrement(&noted) <= 5) {
        char where[MAX_PATH + 16];
        DescribeAddress(reinterpret_cast<DWORD>(rec->ExceptionAddress), where, sizeof(where));
        rfa::Log("First-chance access violation at %s (may be handled)", where);
        HMODULE module = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(rec->ExceptionAddress), &module) &&
            module == GetModuleHandleW(nullptr)) {
            if (rec->NumberParameters >= 2)
                rfa::Log("  while %s address %08lX", rec->ExceptionInformation[0] == 8 ? "executing"
                                                     : rec->ExceptionInformation[0] == 1 ? "writing" : "reading",
                         static_cast<DWORD>(rec->ExceptionInformation[1]));
            const CONTEXT* ctx = exception->ContextRecord;
            rfa::Log("  EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX EBP=%08lX ESP=%08lX", ctx->Eax,
                     ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp);
            LogFrames(ctx);
            if (InterlockedExchange(&dumped, 1) == 0) {
                CrashJob job{exception, GetCurrentThreadId(), L"-first"};
                if (HANDLE thread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
                        WriteDump(*static_cast<CrashJob*>(param));
                        return 0;
                    }, &job, 0, nullptr)) {
                    WaitForSingleObject(thread, 30000);
                    CloseHandle(thread);
                }
            }
        }
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
// The large-address-aware copy (the launcher's LargeAddressAware=)

// Started from the launcher's copy of its exe, the game still asks where its own exe is: WinMain
// first makes that folder the current one (0xA455B0), the DLC search looks in its build folder
// (0x519B50), and the C runtime keeps it as the program's name. So its imports of
// GetModuleFileNameA/W answer, for the game's own module, with the installed exe in the game folder,
// as when it is started from there. Other modules are answered as Windows answers.
std::wstring g_installedExe;
using GetModuleFileNameWFn = DWORD(WINAPI*)(HMODULE, LPWSTR, DWORD);
using GetModuleFileNameAFn = DWORD(WINAPI*)(HMODULE, LPSTR, DWORD);
GetModuleFileNameWFn o_GetModuleFileNameW;
GetModuleFileNameAFn o_GetModuleFileNameA;

bool IsGameModule(HMODULE module) { return module == nullptr || module == GetModuleHandleW(nullptr); }

// Copies `text` into `out` as GetModuleFileName does: cut short to fit with ERROR_INSUFFICIENT_BUFFER
// and the buffer's size returned, else the length.
template <class Char>
DWORD AnswerPath(const std::basic_string<Char>& text, Char* out, DWORD size) {
    if (size == 0) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    const DWORD length = static_cast<DWORD>(text.size());
    const DWORD copied = length < size ? length : size - 1;
    std::copy(text.begin(), text.begin() + copied, out);
    out[copied] = 0;
    if (length >= size) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return size;
    }
    SetLastError(ERROR_SUCCESS);
    return length;
}

DWORD WINAPI Hk_GetModuleFileNameW(HMODULE module, LPWSTR out, DWORD size) {
    if (!IsGameModule(module)) return o_GetModuleFileNameW(module, out, size);
    return AnswerPath(g_installedExe, out, size);
}

DWORD WINAPI Hk_GetModuleFileNameA(HMODULE module, LPSTR out, DWORD size) {
    if (!IsGameModule(module)) return o_GetModuleFileNameA(module, out, size);
    const int bytes = WideCharToMultiByte(CP_ACP, 0, g_installedExe.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(bytes > 0 ? bytes - 1 : 0, '\0');
    if (bytes > 1) WideCharToMultiByte(CP_ACP, 0, g_installedExe.c_str(), -1, narrow.data(), bytes, nullptr, nullptr);
    return AnswerPath(narrow, out, size);
}

void InstallInstalledExePath() {
    wchar_t running[MAX_PATH];
    GetModuleFileNameW(nullptr, running, MAX_PATH);
    g_installedExe = rfa::game::InstalledExePath();
    if (_wcsicmp(g_installedExe.c_str(), running) != 0) {
        HMODULE game = GetModuleHandleW(nullptr);
        const bool wide = rfa::HookImport(game, "KERNEL32.dll", "GetModuleFileNameW", &Hk_GetModuleFileNameW, &o_GetModuleFileNameW);
        const bool narrow = rfa::HookImport(game, "KERNEL32.dll", "GetModuleFileNameA", &Hk_GetModuleFileNameA, &o_GetModuleFileNameA);
        rfa::Log("Started from the large-address-aware copy; the game is told its program is %ls%s", g_installedExe.c_str(),
                 wide && narrow ? "" : " (COULD NOT hook its GetModuleFileName: it may look for its files next to the copy)");
    }
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory))
        rfa::Log("The game's address space: %llu MB (%s)", memory.ullTotalVirtual >> 20,
                 memory.ullTotalVirtual > (2ull << 30) ? "large address aware" : "the 2 GB of a 32-bit program not marked large address aware");
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
//
// A file outside any archive's folder that the game folder doesn't have is a new file, such as a
// new map's archive:
//   overrides\build\pc\cache\rfg01.vpp_pc
// When the game looks up a file and finds it nowhere, the loader hands it the new file at that
// path (build\pc\cache\rfg01.vpp_pc), or of that name, as a loose file. The game mounts archives
// this way too: before loading a level it asks whether build\pc\cache\<level>.vpp_pc exists
// (0x6CD1A0, through 0xC92010), then opens it with cf_open (0xC91470 → 0xC914E0), both through
// FindFile; the mount itself (0xCB5280 → 0xCB53C0) reads the archive from the file it was given.

struct NewFile {
    std::wstring path;       // full path of the file, in the overrides folder
    std::string ansiPath;    // the same, for the game's file functions
    std::string relative;    // its path inside overrides\ (and in the game folder), for the log
    LONG used = 0;
};
std::map<std::string, NewFile> g_newFiles;  // lower-case path in the game folder, and file name -> the file

struct Override {
    std::wstring path;       // full path of the replacement
    std::string ansiPath;    // the same path for the game's own file functions, which take ANSI text
    std::string relative;    // path inside overrides\, for the log
    DWORD archiveSize = 0;   // size of the file it replaces, in its archive
    LONG used = 0;           // set the first time the game reads it, so the log says so only once
};
// (A deque: adding to it leaves the replacements already there where they are, so the file hooks can
// keep one they found while more are added: see RFALoader_RescanOverrides.)
std::map<std::string, std::deque<Override>> g_overrides;  // lower-case file name -> replacements
size_t g_overrideCount = 0;

// The game looks files up on several threads, and the editor can ask for overrides\ and edits\ to be
// looked through again while the game runs (RFALoader_RescanOverrides): the hooks hold this shared
// while they search g_overrides and g_newFiles (never while they call the game), a rescan alone
// while it adds to them. Nothing is ever taken out, so what a hook found stays good.
SRWLOCK g_filesLock = SRWLOCK_INIT;
struct SharedFiles {
    SharedFiles() { AcquireSRWLockShared(&g_filesLock); }
    ~SharedFiles() { ReleaseSRWLockShared(&g_filesLock); }
};
std::set<std::wstring> g_seenFiles;  // every file of overrides\ and edits\ looked at, so a rescan only looks at new ones

struct ArchiveFile {
    std::string name;
    DWORD size;
    DWORD compressedSize;
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
        files.push_back({names.c_str() + entry.nameOffset, entry.size, entry.compressedSize});
    }
    return true;
}

// Checks one file in overrides\ and adds it to g_overrides. `relative` is its path inside overrides\.
void AddOverride(const std::wstring& root, const std::wstring& relative) {
    const std::string shown = Narrow(relative);
    const size_t slash = relative.find_last_of(L'\\');
    const std::wstring archive = slash == std::wstring::npos ? L"" : relative.substr(0, slash);
    const std::string name = Narrow(relative.substr(slash + 1));
    std::wstring gameDir = rfa::game::InstalledExePath();
    gameDir.resize(gameDir.find_last_of(L'\\'));
    if (archive.size() < 7 || _wcsicmp(archive.c_str() + archive.size() - 7, L".vpp_pc") != 0) {
        // Not in an archive's folder: a new file, if the game folder hasn't one there.
        if (GetFileAttributesW((gameDir + L"\\" + relative).c_str()) != INVALID_FILE_ATTRIBUTES) {
            rfa::Log("Override %s skipped: the game folder has this file, and whole game files can't be replaced; "
                     "put replacements in the folder of the archive they come from, "
                     "e.g. overrides\\build\\pc\\cache\\interface.vpp_pc\\%s", shown.c_str(), name.c_str());
            return;
        }
        const std::wstring path = root + L"\\" + relative;
        const std::string ansiPath = NarrowExact(path);
        if (ansiPath.empty() || ansiPath.size() >= sizeof(rfa::game::FileFindResult::name)) {
            rfa::Log("New file %s skipped: the game can't open files at this path (it must be shorter than 256 "
                     "characters, without letters outside your Windows language)", shown.c_str());
            return;
        }
        NewFile added{path, ansiPath, shown};
        g_newFiles[Lower(shown)] = added;
        g_newFiles.try_emplace(Lower(name), added);  // also by its name alone, if no other new file has it
        rfa::LogDetail("New file: %s", shown.c_str());
        return;
    }
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
    rfa::LogDetail("Override: %s", shown.c_str());
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
        else if (_wcsnicmp(name.c_str(), L"README", 6) != 0 && g_seenFiles.insert(L"overrides\\" + path).second) AddOverride(root, path);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

// edits\ does for the archives the mod adds (new archives in overrides\, such as the Guerrilla
// world's build\pc\cache\rfg_l1.vpp_pc, which the Guerrilla builder made) what overrides\ does for
// the game's: edits\build\pc\cache\rfg_l1.vpp_pc\1h_c0312.str2_pc replaces that archive's copy of
// 1h_c0312.str2_pc. (overrides\ can't hold them: the archive itself is a file there, with the name the
// folder would have.) The editor writes its changes to those archives here, so the archives
// themselves stay as they were built. Beside each, <file>.rfabase gives the size and compressed size
// the archive's copy had when the change was made: if the archive was built again since and its copy
// is different, the change is skipped (it was made to the old one).
void AddEdit(const std::wstring& root, const std::wstring& relative) {
    const std::string shown = "edits\\" + Narrow(relative);
    const size_t slash = relative.find_last_of(L'\\');
    const std::wstring archive = slash == std::wstring::npos ? L"" : relative.substr(0, slash);
    const std::string name = Narrow(relative.substr(slash + 1));
    if (name.size() > 8 && _stricmp(name.c_str() + name.size() - 8, ".rfabase") == 0) return;  // (read with its file)
    if (archive.size() < 7 || _wcsicmp(archive.c_str() + archive.size() - 7, L".vpp_pc") != 0) {
        rfa::Log("Edit %s skipped: it isn't in an archive's folder", shown.c_str());
        return;
    }
    std::wstring gameDir = rfa::game::InstalledExePath();
    gameDir.resize(gameDir.find_last_of(L'\\'));
    if (GetFileAttributesW((gameDir + L"\\" + archive).c_str()) != INVALID_FILE_ATTRIBUTES) {
        rfa::Log("Edit %s skipped: %ls is the game's archive; its files are overridden in overrides\\", shown.c_str(), archive.c_str());
        return;
    }
    std::vector<ArchiveFile> files;
    if (!ReadArchiveDirectory(rfa::RootDir() + L"\\overrides\\" + archive, files)) {
        rfa::Log("Edit %s skipped: there is no archive overrides\\%ls", shown.c_str(), archive.c_str());
        return;
    }
    const std::string key = Lower(name);
    auto original = std::find_if(files.begin(), files.end(), [&](const ArchiveFile& f) { return Lower(f.name) == key; });
    if (original == files.end()) {
        rfa::Log("Edit %s skipped: overrides\\%ls has no file named %s", shown.c_str(), archive.c_str(), name.c_str());
        return;
    }
    const std::wstring path = root + L"\\" + relative;
    // Made to the archive as it is now? (Its copy's sizes then, in <file>.rfabase.)
    std::string base;
    if (ReadWholeFile(path + L".rfabase", base)) {
        unsigned long size = 0, compressed = 0;
        if (sscanf_s(base.c_str(), "%lu %lu", &size, &compressed) == 2 && (size != original->size || compressed != original->compressedSize)) {
            rfa::Log("Edit %s skipped: the archive was built again since this change was made (its %s is another now); "
                     "make the change again in the editor", shown.c_str(), name.c_str());
            return;
        }
    }
    if (auto sameName = g_overrides.find(key); sameName != g_overrides.end()) {
        for (const Override& other : sameName->second) {
            if (other.archiveSize == original->size) {
                rfa::Log("Edit %s skipped: %s already replaces the same %s", shown.c_str(), other.relative.c_str(), name.c_str());
                return;
            }
        }
    }
    const std::string ansiPath = NarrowExact(path);
    if (ansiPath.empty() || ansiPath.size() >= sizeof(rfa::game::FileFindResult::name)) {
        rfa::Log("Edit %s skipped: the game can't open files at this path (it must be shorter than 256 "
                 "characters, without letters outside your Windows language)", shown.c_str());
        return;
    }
    Override& added = g_overrides[key].emplace_back();
    added.path = path;
    added.ansiPath = ansiPath;
    added.relative = shown;
    added.archiveSize = original->size;
    ++g_overrideCount;
    rfa::Log("Edit: %s", shown.c_str());
}

// Walks edits\ (and its subfolders), as FindOverrides does overrides\.
void FindEdits(const std::wstring& root, const std::wstring& relative) {
    WIN32_FIND_DATAW fd;
    const std::wstring folder = relative.empty() ? root : root + L"\\" + relative;
    HANDLE find = FindFirstFileW((folder + L"\\*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring path = relative.empty() ? name : relative + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) FindEdits(root, path);
        else if (_wcsnicmp(name.c_str(), L"README", 6) != 0 && g_seenFiles.insert(L"edits\\" + path).second) AddEdit(root, path);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

// The game's Lua hands each run-time error of a script (the message, with the script and line) to
// one callback: 0xD93590 calls the pointer at 0x34FA99C, which 0xD935D0 sets. The game's own
// (0xB77320, set by its script start-up, 0xB77240) only keeps the last message in a buffer, so a
// menu whose script fails just stays empty. Ours writes each error to the log too, then passes it
// on. It is put in place (again, if the game has set its own since) whenever a script is compiled.
using LuaErrorCallbackFn = void(__cdecl*)(lua_State*, const char*);
auto& g_luaErrorCallback = *reinterpret_cast<LuaErrorCallbackFn*>(0x34FA99C);
auto const SetLuaErrorCallback = reinterpret_cast<void(__cdecl*)(LuaErrorCallbackFn)>(0xD935D0);
LuaErrorCallbackFn g_gameLuaErrorCallback;

// An error raised while the game's callback handles one (a menu script reading a global that isn't
// there, say: the menus' strict globals raise again from inside it) came back here, over and over,
// until the stack ran out and the game ended. An error inside that is now only logged.
thread_local int g_inLuaError = 0;

void __cdecl LogLuaError(lua_State* L, const char* message) {
    RFA_LOG_FIRST(100, "Lua error: %s", message ? message : "(no message)");
    if (g_inLuaError) return;
    ++g_inLuaError;
    if (g_gameLuaErrorCallback) g_gameLuaErrorCallback(L, message);
    --g_inLuaError;
}

void WatchLuaErrors() {
    if (g_luaErrorCallback == &LogLuaError) return;
    g_gameLuaErrorCallback = g_luaErrorCallback;
    SetLuaErrorCallback(&LogLuaError);
}

// Every Lua chunk the game compiles passes through here; an override with the same file name
// replaces it.
int __cdecl LoadBufferHook(lua_State* L, const char* buffer, size_t size, const char* chunkName) {
    WatchLuaErrors();
    const std::string name = ScriptFileName(chunkName);
    if (!name.empty()) {
        RFA_LOG_DETAIL_FIRST(60, "Lua script loaded: %s", chunkName);
        const Override* replacement = nullptr;
        {
            SharedFiles shared;
            auto found = g_overrides.find(Lower(name));
            if (found != g_overrides.end()) replacement = &found->second.front();
        }
        std::string text;
        if (replacement && ReadWholeFile(replacement->path, text)) {
            rfa::LogDetail("Using override %s", replacement->relative.c_str());
            return g_loadBuffer(L, text.data(), text.size(), chunkName);
        }
    }
    return g_loadBuffer(L, buffer, size, chunkName);
}

using rfa::game::FileFindResult;
using FindFileFn = bool(__cdecl*)(FileFindResult*, const char*, bool);
FindFileFn g_findFile;  // the game's FindFile

// Forward declarations for FindNewFile
extern bool g_logLookups;
bool IsLevelFile(const char* name);

// A file the game found nowhere: a new file in overrides\ at that path (build\pc\cache\rfg01.vpp_pc,
// or .\build\pc\cache\..., with / or \), or of that name (also when the game asks with a full
// path), as a loose file. A request it can't answer whose name holds a new file's own name (rfg01
// in rfg01.rfgzone_pc) is logged, to show what a new map still lacks.
bool FindNewFile(FileFindResult* result, const char* name) {
    std::string key = Lower(name);
    std::replace(key.begin(), key.end(), '/', '\\');
    if (key.rfind(".\\", 0) == 0) key.erase(0, 2);
    NewFile* file = nullptr;
    {
        SharedFiles shared;
        auto found = g_newFiles.find(key);
        if (found == g_newFiles.end()) found = g_newFiles.find(key.substr(key.find_last_of('\\') + 1));
        // The console's "level <zone>" (0x58E9A0) first checks that the level is there, as a world's
        // first layer: build\pc\cache\<zone>_l0.vpp_pc (0x6CD1A0). A level the editor made (New level)
        // is one archive, <zone>.vpp_pc, as the game's Ruin and Infestation maps are (which the check
        // doesn't find either: they are started from their menus, which don't check): it answers for it.
        // The level is then loaded by its zone's name, as from the menus.
static const std::string kLayer0 = "_l0.vpp_pc";
        if (found == g_newFiles.end() && key.size() >= kLayer0.size() &&
            key.compare(key.size() - kLayer0.size(), kLayer0.size(), kLayer0) == 0) {
            std::string level = key.substr(0, key.size() - kLayer0.size()) + ".vpp_pc";
            found = g_newFiles.find(level);
            if (found == g_newFiles.end()) found = g_newFiles.find(level.substr(level.find_last_of('\\') + 1));
            // Also try the filename alone in case the request was a bare filename
            if (found == g_newFiles.end()) {
                std::string levelName = level.substr(level.find_last_of('\\') + 1);
                found = g_newFiles.find(levelName);
            }
            if (found != g_newFiles.end()) {
                rfa::Log("Level load: mapped %s -> %s (single-archive level)", name, found->second.relative.c_str());
            } else {
                rfa::Log("Level load: asked for %s (_l0.vpp_pc) but no single-archive level found for %s", name, level.c_str());
            }
        }
        if (found == g_newFiles.end()) {
            if (g_logLookups && IsLevelFile(name)) {
                rfa::LogDetail("Level load: file not found in new files: %s", name);
            }
            for (const auto& [newKey, newFile] : g_newFiles) {
                const std::string stem = newKey.substr(newKey.find_last_of('\\') + 1, newKey.find_last_of('.') - newKey.find_last_of('\\') - 1);
                if (stem.size() >= 3 && key.find(stem) != std::string::npos) {
                    RFA_LOG_DETAIL_FIRST(100, "The game looked for %s and found it nowhere (no new file is called that)", name);
                    break;
                }
            }
            return false;
        }
        file = &found->second;
    }
    result->source = -1;
    result->pathType = -1;
    result->name[0] = '\0';
    result->size = 0;
    result->archiveEntry = nullptr;
    const bool ok = rfa::game::FindLooseFile(result, file->ansiPath.c_str(), 0);
    if (InterlockedExchange(&file->used, 1) == 0) {
        if (ok) rfa::LogDetail("Using new file %s (the game asked for %s)", file->relative.c_str(), name);
        else rfa::Log("New file %s could not be opened (the game asked for %s)", file->relative.c_str(), name);
    }
    return ok;
}

// Every file the game looks up by name passes through here. The game finds it first; if it found it
// in an archive and an override replaces that archive's copy (same size), the result is changed to
// point at the override instead.
// [Loader] LogLookups=1: every lookup of a level's files (zones, layers, archives, containers,
// asset lists, terrain, stitch lists, navigation, tables, time of day), with what the game found,
// goes to the log. For working out which files a level loads (up to 4,000 lines a run).
bool g_logLookups = false;

bool IsLevelFile(const char* name) {
    static const char* const kinds[] = {".rfgzone_pc", ".layer_pc", ".vpp_pc", ".str2_pc", ".asm_pc", ".cterrain_pc",
                                        ".ctmesh_pc", ".stchlst_pc", ".navdata_pc", ".precache", ".xtbl",
                                        ".gtodx", ".dtodx"};
    const size_t length = strlen(name);
    for (const char* kind : kinds) {
        const size_t size = strlen(kind);
        if (length >= size && _stricmp(name + length - size, kind) == 0) return true;
    }
    return false;
}

// Ruin (the game calls it Wrecking Crew) keeps its maps in a list: a pointer per map, read from
// wrecking_crew_maps.xtbl, one wc_map_list entry per map. The menu shows the list, and a match
// starts the map at the chosen place in it.
struct RuinWeaponList {  // the game's growable array: the weapons a map's par score unlocks, as checksums
    DWORD* items;
    int capacity;
    int count;
};

struct RuinMap {
    const char* fileName;           // +0x00 ruin01: the level's archive is build\pc\cache\ruin01.vpp_pc
    DWORD fileNameChecksum;         // +0x04 of the name, ignoring case
    const char* displayName;        // +0x08 a string id, RUIN_MAP_01
    const char* displayNameUpper;   // +0x0C
    const char* imageName;          // +0x10
    int timeOfDayHour;              // +0x14
    int timeOfDayMinute;            // +0x18
    void* resourceReservation;      // +0x1C
    int roundTimeMs;                // +0x20 total_chaos_round_time_ms
    float timeBonusTarget;          // +0x24 total_chaos_time_bonus_target
    int timeBonusTimeMs;            // +0x28 total_chaos_time_bonus_time_ms
    bool locked;                    // +0x2C is_locked
    bool lockedUntilEarned;         // +0x2D a base-game map, until the map before it unlocks it
    int dlc;                        // +0x30 the DLC the table came from, or -1
    int scoreboardIndex;            // +0x34
    int parScore;                   // +0x38 par_unlocks: the score to beat,
    int maxScoreStat;               // +0x3C
    bool unlocksExo;                // +0x40
    const char* unlocksMap;         // +0x44 the map that opens when it is beaten,
    RuinWeaponList unlocksWeapons;  // +0x48 and the weapons
    BYTE notRead54[0x14];           // +0x54 (the reader leaves these alone)
    int ambientBank;                // +0x68 Audio
    DWORD musicTrigger;             // +0x6C
    float damageToScore;            // +0x70 Damage_To_Score_Multiplier
    int nanoforgeAbility;           // +0x74
};
static_assert(offsetof(RuinMap, lockedUntilEarned) == 0x2D && offsetof(RuinMap, unlocksWeapons) == 0x48 &&
                  offsetof(RuinMap, nanoforgeAbility) == 0x74, "RuinMap must match the game's map record");

inline RuinMap** const g_ruinMaps = rfa::game::At<RuinMap**>(0x12F39E8);  // room for 35
inline int& g_ruinMapCount = *rfa::game::At<int*>(0x12F3A74);
inline const int& g_chosenRuinMap = *rfa::game::At<const int*>(0x10F857C);
inline void* const kRuinMapPool = rfa::game::At<void*>(0x12F3DA8);     // the "WC mempool": room for 32 maps
inline void* const kRuinStringPool = rfa::game::At<void*>(0x12F3C78);  // keeps the maps' names for good
inline const DWORD& kNoChecksum = *rfa::game::At<const DWORD*>(0x290A190);
inline const DWORD& kNoMusicTrigger = *rfa::game::At<const DWORD*>(0x10724B8);

// The game's table helpers, as its reader uses them. An entry is an XML element (its name at +0, its
// text at +0xC). The "required" ones log an error when the field is missing; the others don't.
struct XmlElement {
    const char* name;
    void* notUsed[2];
    const char* text;
};
inline const auto FirstEntry = rfa::game::At<void*(__cdecl*)(void* table, const char* name)>(0xC8F730);
inline const auto NextEntry = rfa::game::At<void*(__cdecl*)(void* table, void* after, const char* name)>(0xC8F780);
inline const auto EntryText = rfa::game::At<const char*(__cdecl*)(void* entry, const char* field)>(0xC8C800);
inline const auto OptionalText = rfa::game::At<const char*(__cdecl*)(void* entry, const char* field)>(0xC8C8E0);
inline const auto RequiredChild = rfa::game::At<void*(__cdecl*)(void* entry, const char* name)>(0xC8C710);
inline const auto FirstChild = rfa::game::At<void*(__cdecl*)(void* entry, const char* name)>(0xC8C7A0);
inline const auto NextChild = rfa::game::At<void*(__cdecl*)(void* entry, void* after, const char* name)>(0xC8C7C0);
inline const auto ReadInt = rfa::game::At<void(__cdecl*)(int& out, void* entry, const char* field)>(0xC8CAC0);
inline const auto ReadFloat = rfa::game::At<void(__cdecl*)(float& out, void* entry, const char* field)>(0xC8CD30);
inline const auto ReadBool = rfa::game::At<void(__cdecl*)(bool& out, void* entry, const char* field)>(0xC8CE00);
inline const auto ReadOptionalBool = rfa::game::At<bool(__cdecl*)(bool& out, void* entry, const char* field)>(0xC8CE60);
// What the reader looks names up with: "hh:mm", a spawn resource reservation, a stat, a sound
// bank (from a field), a Nano Forge ability.
inline const auto ReadTimeOfDay = rfa::game::At<void(__cdecl*)(int* hour, int* minute, const char* text)>(0x9F6970);
inline const auto FindResourceReservation = rfa::game::At<void*(__cdecl*)(const char* name)>(0x75F2F0);
inline const auto FindStat = rfa::game::At<int(__cdecl*)(const char* name)>(0x554860);
inline const auto ReadSoundBank = rfa::game::At<int(__cdecl*)(void* entry, const char* field)>(0x537A90);
inline const auto FindNanoforgeAbility = rfa::game::At<int(__cdecl*)(const char* name)>(0x71A210);
// Methods (the object in ECX), called as __fastcall with EDX unused. KeepString copies a name into
// the string pool (or finds it there, ignoring case); NameChecksum works out a name's checksum into
// `out` and returns `out`; NewRuinMap takes a record from the pool (nullptr when it is full); AddWeapon
// adds to a weapon list, growing it.
inline const auto KeepString =
    rfa::game::At<const char*(__fastcall*)(void* pool, void* unused, const char* text, int, int)>(0xC97DB0);
inline const auto NameChecksum =
    rfa::game::At<DWORD*(__fastcall*)(DWORD* out, void* unused, const char* text, DWORD seed, int maxLength)>(0xC8BEB0);
inline const auto NewRuinMap = rfa::game::At<RuinMap*(__fastcall*)(void* pool, void* unused, int)>(0x512CE0);
inline const auto AddWeapon = rfa::game::At<int(__fastcall*)(RuinWeaponList* list, void* unused, const DWORD& weapon)>(0x49C480);

const char* KeepRuinString(const char* text) { return KeepString(kRuinStringPool, nullptr, text, 0, 0); }
DWORD RuinNameChecksum(const char* text) {
    DWORD checksum;
    return *NameChecksum(&checksum, nullptr, text, 0, -1);
}

// The game's own reader (0x50E690, cdecl: the table, and the DLC it came from or -1) stops at five
// maps, and after reading each it sets these by the map's place in the list, whatever the table
// says: the five maps the game was made for, in their table's order, with that same table's values.
// So a table with a new map would start the wrong level, and no more than five could be listed.
constexpr int kRuinShippedMaps = 5, kRuinMostMaps = 32;  // 32: what the map pool holds
const char* const kShippedRuinMaps[kRuinShippedMaps] = {"ruin01", "ruin08", "ruin06", "ruin07", "ruin03"};
constexpr float kShippedDamageToScore[kRuinShippedMaps] = {0.6f, 0.85f, 3.75f, 0.56f, 0.85f};
constexpr int kShippedRoundTimeMs = 60000, kShippedTimeBonusMs = 15000;

// One wc_map_list entry, read as the game reads it.
void ReadRuinMap(RuinMap* map, void* entry, int dlc) {
    map->dlc = dlc;
    map->lockedUntilEarned = dlc == -1;
    map->fileName = nullptr;
    map->fileNameChecksum = kNoChecksum;
    if (const char* text = EntryText(entry, "file_name")) {
        map->fileName = KeepRuinString(text);
        map->fileNameChecksum = RuinNameChecksum(map->fileName);
    }
    const char* text = EntryText(entry, "display_name");
    map->displayName = text ? KeepRuinString(text) : nullptr;
    text = EntryText(entry, "display_name_uppercase");
    map->displayNameUpper = text ? KeepRuinString(text) : nullptr;
    text = EntryText(entry, "image_name");
    map->imageName = text ? KeepRuinString(text) : nullptr;
    if (const char* time = EntryText(entry, "time_of_day")) ReadTimeOfDay(&map->timeOfDayHour, &map->timeOfDayMinute, time);
    const char* reservation = OptionalText(entry, "resource_reservation");
    map->resourceReservation = reservation ? FindResourceReservation(reservation) : nullptr;
    map->locked = true;
    ReadOptionalBool(map->locked, entry, "is_locked");
    ReadInt(map->roundTimeMs, entry, "total_chaos_round_time_ms");
    ReadFloat(map->timeBonusTarget, entry, "total_chaos_time_bonus_target");
    ReadInt(map->timeBonusTimeMs, entry, "total_chaos_time_bonus_time_ms");
    ReadInt(map->scoreboardIndex, entry, "scoreboard_index");

    void* par = RequiredChild(entry, "par_unlocks");
    ReadInt(map->parScore, par, "par_score");
    map->maxScoreStat = FindStat(EntryText(par, "max_score_stat"));
    ReadBool(map->unlocksExo, par, "unlocks_exo");
    const char* unlocks = OptionalText(par, "unlocks_map");
    map->unlocksMap = unlocks ? KeepRuinString(unlocks) : nullptr;
    map->unlocksWeapons.count = 0;
    void* weapons = RequiredChild(par, "unlocks_weapons");
    for (void* weapon = FirstChild(weapons, "weapon_info"); weapon; weapon = NextChild(weapons, weapon, "weapon_info"))
        AddWeapon(&map->unlocksWeapons, nullptr, RuinNameChecksum(static_cast<XmlElement*>(weapon)->text));

    map->ambientBank = 0;
    map->musicTrigger = kNoMusicTrigger;
    if (void* audio = RequiredChild(entry, "Audio")) {
        map->ambientBank = ReadSoundBank(audio, "Ambient_Bank_Name");
        if (const char* music = OptionalText(audio, "Music_Trigger_Name")) map->musicTrigger = RuinNameChecksum(music);
    }
    ReadFloat(map->damageToScore, entry, "Damage_To_Score_Multiplier");
    const char* ability = OptionalText(entry, "Nanoforge_Ability");
    map->nanoforgeAbility = ability ? FindNanoforgeAbility(ability) : 0;
}

// The loader's reader, in the game's reader's place: up to 32 maps, each started by its own
// file_name. For the first five places it keeps what the game sets by place (the round times, the
// scoreboard and the damage-to-score multiplier), so the game's five maps are as they always were.
// A map past the five is as its table gives it, and one from the base table is open from the start:
// the game opens each base-game map from the one before it (its unlocks_map, once that map's par
// score is beaten), which the five shipped maps don't do for a new one.
void __cdecl ReadRuinMaps(void* table, int dlc) {
    if (!table) return;
    std::string leftOut;
    for (void* entry = FirstEntry(table, "wc_map_list"); entry; entry = NextEntry(table, entry, "wc_map_list")) {
        const int place = g_ruinMapCount;
        RuinMap* map = place < kRuinMostMaps ? NewRuinMap(kRuinMapPool, nullptr, 0) : nullptr;
        if (!map) {
            const char* fileName = EntryText(entry, "file_name");
            leftOut += (leftOut.empty() ? "" : ", ") + std::string(fileName ? fileName : "(no file_name)");
            continue;
        }
        ReadRuinMap(map, entry, dlc);
        if (place < kRuinShippedMaps) {
            const char* shipped = kShippedRuinMaps[place];
            if (!map->fileName) {
                map->fileName = KeepRuinString(shipped);
                map->fileNameChecksum = RuinNameChecksum(map->fileName);
            } else if (_stricmp(map->fileName, shipped) != 0) {
                rfa::Log("Ruin map %d is %s, as its table says (the game would have started %s)", place, map->fileName, shipped);
            }
            map->roundTimeMs = kShippedRoundTimeMs;
            map->timeBonusTimeMs = kShippedTimeBonusMs;
            map->scoreboardIndex = place;
            map->damageToScore = kShippedDamageToScore[place];
        } else {
            if (dlc == -1) map->lockedUntilEarned = false;
            rfa::Log("Ruin map %d: %s%s", place, map->fileName ? map->fileName : "(no name)", dlc == -1 ? ", unlocked" : "");
        }
        g_ruinMaps[place] = map;
        ++g_ruinMapCount;
    }
    if (!leftOut.empty()) rfa::Log("Ruin has room for %d maps; left out of its list: %s", kRuinMostMaps, leftOut.c_str());
}

constexpr DWORD kReadRuinMaps = 0x50E690;

// Quitting a Ruin match from its pause menu (rf4_pause_menu_quit_game → 0x58D8C0) goes straight to the
// main menu, and Ruin stays set up: its tables, its map list and its spawn resource reservation
// (0x50E1D0 says so). Every other way out of Ruin leaves it first, through Ruin's exit (0x5613C0: Ruin's
// leaving state, then the state it is given): Ruin's own menu (rf4_wrecking_crew_end_wrecking_crew), a
// load that fails, the death menu, the console's wc_shutdown. So a level loaded next without passing
// through Ruin (the console's `level`) loaded with Ruin's set-up still in place, and the player was gone by
// its first frame: the sound listener read his position from nothing (0x541EAE) and the game crashed. The
// loader's copy of the quit (cdecl, no arguments) does what the game's does, but leaves Ruin on the way
// to the main menu, as those others do.
constexpr DWORD kQuitToMainMenu = 0x58D8C0;
inline const auto StopSoundsA = rfa::game::At<void(__cdecl*)(int, int)>(0xB8AB30);
inline const auto StopSoundsB = rfa::game::At<void(__cdecl*)(int, int)>(0xB8ABF0);
inline const auto PauseGame = rfa::game::At<void(__cdecl*)(int, int, int, int)>(0x520280);  // counted pauses
inline const BYTE& g_multiplayer = *rfa::game::At<const BYTE*>(0x2685118);
inline const auto LeaveMultiplayer = rfa::game::At<void(__cdecl*)(int)>(0x9E5F80);
inline const auto OtherModeSetUp = rfa::game::At<bool(__cdecl*)()>(0x518A50);  // [0x12F5D6E]; its own exit is 0x561340
inline const auto MarkOtherModeQuit = rfa::game::At<void(__cdecl*)(bool)>(0x519680);
inline const auto SetGameState = rfa::game::At<void(__cdecl*)(int state, int)>(0x53FCD0);
inline const auto RuinSetUp = rfa::game::At<bool(__cdecl*)()>(0x50E1D0);
inline const auto LeaveRuin = rfa::game::At<void(__cdecl*)(int thenState)>(0x5613C0);
constexpr int kMainMenuState = 0;

void __cdecl QuitToMainMenu() {
    StopSoundsA(1, 1);
    StopSoundsB(1, 1);
    PauseGame(1, 1, 0, 0);
    if (g_multiplayer) LeaveMultiplayer(1);
    if (OtherModeSetUp()) MarkOtherModeQuit(true);
    if (RuinSetUp()) {
        rfa::Log("Quitting to the main menu: leaving Ruin on the way, as its own menu does");
        LeaveRuin(kMainMenuState);
    } else {
        SetGameState(kMainMenuState, 0);
    }
}

void InstallRuinQuit() {
    if (!rfa::ReplaceFunction(kQuitToMainMenu, reinterpret_cast<const void*>(&QuitToMainMenu)))
        rfa::Log("Could not replace the quit to the main menu; quitting a Ruin match leaves Ruin set up");
}

// With LogLookups=1, the Ruin map list is logged too when a Ruin match begins
// (rf4_wrecking_crew_begin_match → 0x50F400): the map chosen and every map's file name.
constexpr DWORD kBeginRuinMatch = 0x50F400;
constexpr size_t kBeginRuinMatchPrologue = 9;  // push ebp / mov ebp,esp / sub esp,110h
using BeginRuinMatchFn = void(__cdecl*)();
BeginRuinMatchFn g_beginRuinMatch;

void __cdecl BeginRuinMatchHook() {
    std::string names;
    for (int i = 0; i < g_ruinMapCount && i < 35; ++i) {
        const char* name = g_ruinMaps[i] ? g_ruinMaps[i]->fileName : nullptr;
        names += (i ? ", " : "") + std::to_string(i) + " " + (name ? name : "(none)");
    }
    rfa::Log("Ruin match beginning: map %d of %d; the game's list: %s", g_chosenRuinMap, g_ruinMapCount, names.c_str());
    g_beginRuinMatch();
}

bool __cdecl FindFileHook(FileFindResult* result, const char* name, bool flag) {
    const bool found = g_findFile(result, name, flag);
    if (g_logLookups && name && IsLevelFile(name)) {
        const char* sourceStr = !found                 ? "not found"
                      : result->source == 1 ? "in an archive"
                      : result->source == 2 ? "in a virtual folder"
                      : result->source == 0 ? "loose file"
                                             : "found";
        rfa::LogDetail("Level lookup: %s -> %s (%lu bytes)", name, sourceStr, found ? result->size : 0ul);
    }
    if (!found && name) return FindNewFile(result, name);
    if (!found || result->source != 1 || !name) return found;  // not found, or not inside an archive

    Override* o = nullptr;
    {
        SharedFiles shared;
        auto overrides = g_overrides.find(Lower(name));
        if (overrides == g_overrides.end() || IsLuaScript(overrides->first)) return found;
        auto match = std::find_if(overrides->second.begin(), overrides->second.end(),
                                  [&](const Override& candidate) { return candidate.archiveSize == result->size; });
        if (match == overrides->second.end()) {
            RFA_LOG_DETAIL_FIRST(20, "Override %s not used here: the game loaded another archive's %s (%lu bytes)",
                          overrides->second.front().relative.c_str(), name, result->size);
            return found;
        }
        o = &*match;
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
    if (InterlockedExchange(&o->used, 1) == 0) {
        if (ok) rfa::LogDetail("Using override %s", o->relative.c_str());
        else rfa::Log("Override %s could not be opened; the game's own file is used", o->relative.c_str());
    }
    if (!ok) *result = gameCopy;
    return true;
}

void Widen(const char* text, wchar_t* out, int size) {
    if (!MultiByteToWideChar(CP_ACP, 0, text ? text : "", -1, out, size)) out[0] = L'\0';
}

// ---- The main menu's music (Options > Main Menu > Music: [MainMenu] Music= in rfa_mods.ini) -----------
//
// The main menu plays its music by setting the music mode "Main Menu and Loading" (game_music_modes.xtbl;
// 0x53E2A0, the mode's name, 1, 1 or 0, 0) when it starts (0x590470, the push at 0x59059E) and when it is
// shown again (0x590CB0, the push at 0x590CE5). Both pushes are pointed at the chosen mode's name instead:
// any of the game's 36 modes (each mission's, quest's, Infestation's and Ruin's music, the credits'). The
// loading screens keep the menu's own. The list is in the order Options > Main Menu > Music shows it.
constexpr const char* kMusicModes[] = {
    "Main Menu and Loading",
    "Mission 1", "Mission 2", "Mission 3", "Mission 4", "Mission 5", "Mission 6", "Mission 7", "Mission 8",
    "Mission 9", "Mission 12", "Mission 13", "Mission 14", "Mission 16", "Mission 16b", "Mission 17a",
    "Mission 17b", "Mission 17c",
    "Quest 01", "Quest 02", "Quest 03", "Quest 04", "Quest 06", "Quest 07", "Quest 08", "Quest 10",
    "MP_Alpha", "MP_Bravo", "MP_Charlie", "MP_Delta", "MP_Echo", "MP_Foxtrot", "MP_Golf", "MP_Hotel",
    "Ruin_Mode", "Credits",
};
constexpr int kMusicModeCount = static_cast<int>(sizeof(kMusicModes) / sizeof(kMusicModes[0]));
constexpr DWORD kMenuMusicPushes[] = {0x59059E, 0x590CE5};  // push imm32 ("Main Menu and Loading", 0xF89A24)
constexpr DWORD kMenuMusicName = 0xF89A24;
auto const SetMusicMode = reinterpret_cast<void(__cdecl*)(const char*, bool, bool, int)>(0x53E2A0);
auto const ActivateMusicTrigger = reinterpret_cast<int(__cdecl*)(const char*)>(0x53E7F0);
int g_menuMusic = 0;

// Points the main menu's pushes at the chosen mode's name. False if the code isn't as expected.
bool SetMenuMusic(int index) {
    if (index < 0 || index >= kMusicModeCount) index = 0;
    const DWORD name = index == 0 ? kMenuMusicName : reinterpret_cast<DWORD>(kMusicModes[index]);
    for (DWORD push : kMenuMusicPushes) {
        const BYTE* code = reinterpret_cast<const BYTE*>(push);
        if (code[0] != 0x68) {
            rfa::Log("Main menu music: the code at 0x%08lX isn't the push it should be; the game's own music plays", push);
            return false;
        }
        if (!rfa::PatchCode(reinterpret_cast<void*>(push + 1), &name, sizeof(name))) return false;
    }
    g_menuMusic = index;
    return true;
}

void InstallMenuMusic() {
    const int index = rfa::IniInt(L"MainMenu", L"Music", 0);
    if (index != 0 && SetMenuMusic(index)) rfa::Log("Main menu music: %s's", kMusicModes[g_menuMusic]);
}

// Options > Main Menu changed the music: the menu plays it now (it is open behind the options screen).
void MenuMusicChanged(int index) {
    if (index == g_menuMusic || !SetMenuMusic(index)) return;
    rfa::Log("Main menu music: %s's", kMusicModes[g_menuMusic]);
    SetMusicMode(kMusicModes[g_menuMusic], true, true, 0);
    if (g_menuMusic == 0) ActivateMusicTrigger("Main Menu");  // as the menu starts its own
}

// ---- RFA-Mod's UI pack (the main menu's Modern style's pictures: the editor's RfaFormats UiPack) ------------
//
// A document of pictures only, rfa_ui_modern, in overrides\build\pc\cache, listed in the interface's asset list
// and project overrides. The game reads those lists once, as it starts: a document it doesn't know stops it
// ("no SRID"), so it is loaded only if all of it was there when the game started.
constexpr char kUiPackDocument[] = "rfa_ui_modern";
bool g_uiPackThere = false;

bool FileHolds(const std::wstring& path, const char* text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::string bytes(GetFileSize(file, nullptr), '\0');
    DWORD read = 0;
    const bool ok = !bytes.empty() && ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    return ok && bytes.find(text, 0) != std::string::npos;
}

void FindUiPack() {
    const std::wstring cache = rfa::RootDir() + L"\\overrides\\build\\pc\\cache\\";
    g_uiPackThere = GetFileAttributesW((cache + L"rfa_ui_modern.str2_pc").c_str()) != INVALID_FILE_ATTRIBUTES &&
                    GetFileAttributesW((cache + L"rfa_ui_modern2.str2_pc").c_str()) != INVALID_FILE_ATTRIBUTES &&
                    FileHolds(cache + L"interface.vpp_pc\\vint_doc_containers.asm_pc", "rfa_ui_modern2") &&
                    FileHolds(cache + L"misc.vpp_pc\\rfg.vint_proj", "<doc_name>rfa_ui_modern</doc_name>");
    rfa::Log(g_uiPackThere ? "UI pack: rfa_ui_modern is there (the main menu's Modern style uses its pictures)"
                           : "UI pack: not there yet (the editor makes it; the main menu's Modern style does without its pictures)");
}

// Lua: rfa_load_ui_pack() -> true if the UI pack's document is being loaded (it is there), false if not. The menu
// waits for vint_document_find to find it before it shows its pictures.
int __cdecl LuaLoadUiPack(lua_State* L) {
    if (g_uiPackThere) {
        using LoadDocumentFn = unsigned short(__cdecl*)(const char*, int, float, int, int, int, int);
        // As guerrilla_hud loads its documents while playing (the console's vint_doc_load does so too).
        reinterpret_cast<LoadDocumentFn>(0x563990)(kUiPackDocument, 0, 0.0f, 0, 0, 0x80, 0);
    }
    rfa::game::lua_pushboolean(L, g_uiPackThere);
    return 1;
}

// Lua: rfa_processing_popup(on) shows or hides the game's processing screen (ui_popup_halt), as the game does
// while it loads or saves: 0x5A3FF0 (the kind: 4, PLEASE WAIT, its row in the table at 0x10FB838; not on
// black) and 0x5A4060 (true: now, past the kind's least time on screen). The menu puts its own words on it.
// The kind on screen is at 0x10FB830 (-1: none). The game takes the screen down from many places (some two
// dozen callers of 0x5A4060) and a wait asks again each frame, so it is shown again only when it isn't ours.
//
// While a wait is on, the game isn't let take it down: HaltHideHook and HaltShowHook turn away its calls to hide
// the screen or put another kind in its place (the menu's own hide goes past them). The hold lasts as long as
// the wait asks each frame, and lapses two seconds after it stops asking, so a wait that dies can't leave the
// screen up for good.
using HaltShowFn = void(__cdecl*)(int kind, bool onBlack);
using HaltHideFn = void(__cdecl*)(bool now);
constexpr DWORD kHaltShow = 0x5A3FF0;
constexpr size_t kHaltShowPrologue = 6;   // push ebp / mov ebp,esp / sub esp,8
constexpr DWORD kHaltHide = 0x5A4060;
constexpr size_t kHaltHidePrologue = 10;  // push ebp / mov ebp,esp / movzx eax,byte ptr [1363209h]
constexpr int kHaltOurs = 4;              // MENU_PLEASE_WAIT
HaltShowFn g_haltShow = reinterpret_cast<HaltShowFn>(kHaltShow);
HaltHideFn g_haltHide = reinterpret_cast<HaltHideFn>(kHaltHide);
ULONGLONG g_haltHeldUntil = 0;
bool g_haltReleasing = false;

bool HaltHeld() {
    return !g_haltReleasing && GetTickCount64() < g_haltHeldUntil &&
           *reinterpret_cast<volatile int*>(0x10FB830) == kHaltOurs;
}

void __cdecl HaltShowHook(int kind, bool onBlack) {
    if (kind != kHaltOurs && kind != -1 && HaltHeld()) return;  // another kind in its place: not now
    g_haltShow(kind, onBlack);
}

void __cdecl HaltHideHook(bool now) {
    if (HaltHeld()) return;  // the game taking it down: not while the wait is on
    g_haltHide(now);
}

void InstallProcessingHold() {
    auto show = reinterpret_cast<HaltShowFn>(
        rfa::HookFunction(kHaltShow, kHaltShowPrologue, reinterpret_cast<void*>(&HaltShowHook)));
    auto hide = reinterpret_cast<HaltHideFn>(
        rfa::HookFunction(kHaltHide, kHaltHidePrologue, reinterpret_cast<void*>(&HaltHideHook)));
    if (show) g_haltShow = show;
    if (hide) g_haltHide = hide;
    if (!show || !hide)
        rfa::Log("Could not hook the processing screen; the game may take it down while the menu waits (it is put back)");
}

// ---- A level that fails to load: back to the main menu, which says so --------------------------------------------
//
// The world load (0x6C8940, fastcall, ECX = the world; false when its territory, the level's archives, couldn't be
// read: "Could not find territory %s", the name at world +0x2554) is watched. Its caller (gs_load_process 0x55E380)
// then unloads and goes back to the main menu (Ruin: 0x5613C0(0)), but the game has been seen closing instead: it
// leaves through its exit state (0x14, set with 0x53FCD0) when the window's close flag (0x293A88A, read by
// 0xCB1150) is up. While it recovers from a failed load, neither is let through: the exit becomes the main menu and
// the close flag reads as down. The main menu then shows a message (rfa_failed_level) and the recovery ends; it
// also lapses after a minute.
using WorldLoadFn = unsigned(__fastcall*)(void* world);
using SetStateFn = void(__cdecl*)(int state, bool now);
using QuitAskedFn = bool(__cdecl*)();
constexpr DWORD kWorldLoad = 0x6C8940;
constexpr size_t kWorldLoadPrologue = 9;   // push ebp / mov ebp,esp / sub esp,9Ch
constexpr DWORD kSetState = 0x53FCD0;
constexpr size_t kSetStatePrologue = 6;    // push ebp / mov ebp,esp / sub esp,10h
constexpr DWORD kQuitAsked = 0xCB1150;
constexpr size_t kQuitAskedPrologue = 8;   // push ebp / mov ebp,esp / mov al,byte ptr [293A88Ah]
constexpr int kStateExit = 0x14, kStateFrontEnd = 0;
WorldLoadFn g_worldLoad = nullptr;
SetStateFn g_setState = nullptr;
QuitAskedFn g_quitAsked = nullptr;
SRWLOCK g_failedLock = SRWLOCK_INIT;
std::string g_failedLevel;               // for the main menu's message, until it has shown it
volatile ULONGLONG g_failedAt = 0;       // when it failed (0: not recovering)
volatile bool g_failedCloseLogged = false;

bool RecoveringFromFailedLoad() {
    const ULONGLONG at = g_failedAt;
    return at != 0 && GetTickCount64() - at < 60000;
}

unsigned __fastcall WorldLoadHook(void* world) {
    auto bytes = static_cast<const char*>(world);
    // (Not a load put off or called off: those return false too, with these set.)
    const bool real = *reinterpret_cast<const int*>(bytes + 0x23F4) == 0 && !(static_cast<BYTE>(bytes[0x2514]) & 0x24);
    const unsigned loaded = g_worldLoad(world);
    if (real && !(loaded & 0xFF)) {
        char name[128];
        strncpy_s(name, bytes + 0x2554, _TRUNCATE);
        AcquireSRWLockExclusive(&g_failedLock);
        g_failedLevel = name;
        ReleaseSRWLockExclusive(&g_failedLock);
        g_failedCloseLogged = false;
        g_failedAt = GetTickCount64();
        rfa::Log("Level %s failed to load (its archive wasn't found, or its files couldn't be read): back to the main menu", name);
    }
    return loaded;
}

void __cdecl SetStateHook(int state, bool now) {
    if (state == kStateExit && RecoveringFromFailedLoad()) {
        rfa::Log("The game was about to close after the failed load: it goes to the main menu instead");
        state = kStateFrontEnd;
    }
    g_setState(state, now);
}

bool __cdecl QuitAskedHook() {
    const bool asked = g_quitAsked();
    if (!asked || !RecoveringFromFailedLoad()) return asked;
    *reinterpret_cast<volatile BYTE*>(0x293A88A) = 0;
    if (!g_failedCloseLogged) {
        g_failedCloseLogged = true;
        rfa::Log("The game's window was told to close while it went back from the failed load: it stays open");
    }
    return false;
}

void InstallLoadFailure() {
    g_worldLoad = reinterpret_cast<WorldLoadFn>(rfa::HookFunction(kWorldLoad, kWorldLoadPrologue, reinterpret_cast<void*>(&WorldLoadHook)));
    if (!g_worldLoad) {
        rfa::Log("Could not hook the world load; a level that fails to load may close the game");
        return;
    }
    g_setState = reinterpret_cast<SetStateFn>(rfa::HookFunction(kSetState, kSetStatePrologue, reinterpret_cast<void*>(&SetStateHook)));
    g_quitAsked = reinterpret_cast<QuitAskedFn>(rfa::HookFunction(kQuitAsked, kQuitAskedPrologue, reinterpret_cast<void*>(&QuitAskedHook)));
    if (!g_setState || !g_quitAsked) rfa::Log("Could not hook the game's exit; a level that fails to load may still close the game");
}

// Lua: rfa_failed_level() -> the level that failed to load since the main menu last asked, and whether it is a custom
// one (no archive of the game's), or nil. Asking ends the recovery (see above).
int __cdecl LuaFailedLevel(lua_State* L) {
    AcquireSRWLockExclusive(&g_failedLock);
    const std::string name = g_failedLevel;
    g_failedLevel.clear();
    ReleaseSRWLockExclusive(&g_failedLock);
    g_failedAt = 0;
    if (name.empty()) {
        rfa::game::lua_pushnil(L);
        return 1;
    }
    std::wstring gameDir = rfa::game::InstalledExePath();
    gameDir.resize(gameDir.find_last_of(L'\\'));
    const std::wstring archive = gameDir + L"\\build\\pc\\cache\\" + std::wstring(name.begin(), name.end()) + L".vpp_pc";
    rfa::game::lua_pushstring(L, name.c_str());
    rfa::game::lua_pushboolean(L, GetFileAttributesW(archive.c_str()) == INVALID_FILE_ATTRIBUTES);
    return 2;
}

int __cdecl LuaProcessingPopup(lua_State* L) {
    const bool on = rfa::game::lua_toboolean(L, 1) != 0;
    if (on) {
        g_haltHeldUntil = GetTickCount64() + 2000;
        if (*reinterpret_cast<volatile int*>(0x10FB830) != kHaltOurs) g_haltShow(kHaltOurs, false);
    } else {
        g_haltHeldUntil = 0;
        g_haltReleasing = true;
        g_haltHide(true);
        g_haltReleasing = false;
    }
    return 0;
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
    if (_stricmp(section, "MainMenu") == 0 && _stricmp(key, "Music") == 0) MenuMusicChanged(value);
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

// ---- The level editor, from the game's main menu (ui_main_menu_pc.lua's Level editor) ----------------
//
// rfa_open_level_editor([mode]) starts editor\RfaEditor.exe (with a mode, on New level for it); rfa_custom_levels() returns the names of the Ruin
// levels made with the editor's New level (the rows of our wrecking_crew_maps.xtbl whose archive the game
// folder hasn't); rfa_delete_custom_level(name) starts the editor with no window to delete one, all of it,
// as its Delete level does, and rfa_delete_custom_level_done() says when that has finished: nil while it
// runs, then the editor's exit code (0 deleted, 1 not a level of ours, 2 no game folder, 3 couldn't).

// One of the mod's tools in editor\ (RfaEditor.exe, GuerrillaBuilder.exe), started with its folder as its own.
bool StartTool(const wchar_t* name, const std::wstring& arguments, bool window, HANDLE* process) {
    const std::wstring exe = rfa::RootDir() + L"\\editor\\" + name;
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        rfa::Log("Main menu: %ls isn't there (build the editor, or copy bin\\editor\\ next to the loader)", exe.c_str());
        return false;
    }
    std::wstring command = L"\"" + exe + L"\"" + (arguments.empty() ? L"" : L" " + arguments);
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = window ? SW_SHOWNORMAL : SW_HIDE;
    PROCESS_INFORMATION info{};
    const std::wstring folder = rfa::RootDir() + L"\\editor";
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, window ? 0 : CREATE_NO_WINDOW, nullptr,
                        folder.c_str(), &startup, &info)) {
        rfa::Log("Main menu: %ls couldn't be started (error %lu)", exe.c_str(), GetLastError());
        return false;
    }
    CloseHandle(info.hThread);
    if (process) *process = info.hProcess;
    else CloseHandle(info.hProcess);
    return true;
}

bool StartEditor(const std::wstring& arguments, bool window, HANDLE* process) {
    return StartTool(L"RfaEditor.exe", arguments, window, process);
}

// Lua: rfa_guerrilla_world_built() -> whether editor\GuerrillaBuilder.exe has made the Guerrilla world
// (its archives, overrides\build\pc\cache\rfg_l0.vpp_pc and rfg_l1.vpp_pc)
int __cdecl LuaGuerrillaWorldBuilt(lua_State* L) {
    const std::wstring cache = rfa::RootDir() + L"\\overrides\\build\\pc\\cache\\";
    const bool built = GetFileAttributesW((cache + L"rfg_l0.vpp_pc").c_str()) != INVALID_FILE_ATTRIBUTES ||
                       GetFileAttributesW((cache + L"rfg_l1.vpp_pc").c_str()) != INVALID_FILE_ATTRIBUTES;
    rfa::game::lua_pushboolean(L, built);
    return 1;
}

// Lua: rfa_console_command(line) -> true if rfa_qol's console will run it (as typed) at the start of the next
// frame: not from inside the menu's script, which a level load would take away under it.
int __cdecl LuaConsoleCommand(lua_State* L) {
    const char* line = rfa::game::lua_tolstring(L, 1, nullptr);
    using QueueFn = int(__cdecl*)(const char*);
    bool queued = false;
    for (HMODULE mod : g_mods)
        if (auto queue = reinterpret_cast<QueueFn>(GetProcAddress(mod, "RFAQol_QueueConsoleCommand")))
            if (!queued) queued = queue(line) != 0;
    if (!queued) rfa::Log("Main menu: \"%s\" couldn't be run: it needs rfa_qol's console (console=1 under [Mods])", line ? line : "");
    rfa::game::lua_pushboolean(L, queued);
    return 1;
}

// Lua: rfa_open_guerrilla_builder() -> true if editor\GuerrillaBuilder.exe was started
int __cdecl LuaOpenGuerrillaBuilder(lua_State* L) {
    const bool started = StartTool(L"GuerrillaBuilder.exe", L"", true, nullptr);
    if (started) rfa::Log("Guerrilla builder started from the main menu");
    rfa::game::lua_pushboolean(L, started);
    return 1;
}

// The editor started from the menu, and the event it sets once it has read (indexed) the game's files
// (Local\RFAModEditorReady: RfaEditor's MainWindow sets it after "Ready: ... files can be searched").
HANDLE g_editorStarted = nullptr;
HANDLE g_editorReady = nullptr;

// Lua: rfa_level_editor_ready() -> nil while the editor started from the menu is reading the game's files,
// true once it has, false if it closed first (or wasn't started).
int __cdecl LuaLevelEditorReady(lua_State* L) {
    if (g_editorReady && WaitForSingleObject(g_editorReady, 0) == WAIT_OBJECT_0) {
        rfa::game::lua_pushboolean(L, 1);
        return 1;
    }
    if (g_editorStarted && WaitForSingleObject(g_editorStarted, 0) == WAIT_TIMEOUT) {
        rfa::game::lua_pushnil(L);
        return 1;
    }
    rfa::game::lua_pushboolean(L, 0);
    return 1;
}

// An editor that is open already is asked over its pipe (\\.\pipe\RFAModEditor, the editor's GameMenuPipe) instead of
// starting another: one line, the arguments; it answers with its process id, which is watched as a started one is.
bool SendToOpenEditor(const std::wstring& arguments, HANDLE* process) {
    const wchar_t* name = L"\\\\.\\pipe\\RFAModEditor";
    HANDLE pipe = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY && WaitNamedPipeW(name, 2000))
        pipe = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return false;  // no editor open
    std::string line;
    for (wchar_t c : arguments) line.push_back(static_cast<char>(c));  // (ASCII: the menu keeps to it)
    line.push_back('\n');
    DWORD written = 0, read = 0;
    char answer[32] = {};
    const bool sent = WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) && written == line.size() &&
                      ReadFile(pipe, answer, sizeof(answer) - 1, &read, nullptr) && read > 0;
    const DWORD error = sent ? 0 : GetLastError();
    CloseHandle(pipe);
    if (!sent) {
        rfa::Log("Main menu: the open level editor didn't answer (error %lu); starting another", error);
        return false;
    }
    const DWORD pid = strtoul(answer, nullptr, 10);
    if (process) {
        *process = pid ? OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr;
        if (!*process) *process = GetCurrentProcess();  // (can't watch it: taken as open, which it is)
    }
    rfa::Log("Main menu: the level editor was open already (pid %lu): it was asked instead of starting another", pid);
    return true;
}

// Lua: rfa_open_level_editor([mode]) -> true if the editor was started (or the open one asked). With "ruin" or "infestation" it
// opens New level for that mode, as a base, once it has read the game's files.
// rfa_open_level_editor(mode, name, display, ground, size, textures, sky, replace): the Create level page chose it all
// (ground "new" or "none", size in metres, textures a level of the game's or "" for the first, replace: make the
// editor's level of that name again); the editor makes the level with no dialog once it has read the files, and opens it.
int __cdecl LuaOpenLevelEditor(lua_State* L) {
    using namespace rfa::game;
    const int given = lua_gettop(L);
    auto Text = [&](int at) -> std::string {
        const char* text = given >= at ? lua_tolstring(L, at, nullptr) : nullptr;
        return text ? text : "";
    };
    auto Wide = [](const std::string& text) { return std::wstring(text.begin(), text.end()); };  // (ASCII: the menu keeps to it)
    auto Quoted = [&](const std::string& text) { return L"\"" + Wide(text) + L"\""; };
    const std::string mode = Text(1), name = Text(2);
    const bool known = _stricmp(mode.c_str(), "ruin") == 0 || _stricmp(mode.c_str(), "infestation") == 0;
    // --dock: the editor docks this game in its Game panel.
    std::wstring arguments = L"--dock";
    if (known) arguments += L" --new-level " + Wide(mode);
    // The Create level page chose everything: the editor makes the level with no dialog (see its App.xaml.cs).
    const bool chosen = known && !name.empty();
    if (chosen) {
        const int size = given >= 5 ? static_cast<int>(lua_tonumber(L, 5)) : 255;
        arguments += L" --name " + Quoted(name) + L" --display " + Quoted(Text(3)) +
                     L" --ground " + (Text(4) == "none" ? L"none" : L"new") + L" --size " + std::to_wstring(size) +
                     L" --textures " + Quoted(Text(6)) + L" --sky " + Quoted(Text(7));
        if (given >= 8 && lua_toboolean(L, 8)) arguments += L" --replace";
    }
    if (!g_editorReady) g_editorReady = CreateEventW(nullptr, TRUE, FALSE, L"Local\\RFAModEditorReady");
    if (g_editorReady) ResetEvent(g_editorReady);
    if (g_editorStarted) CloseHandle(g_editorStarted);
    g_editorStarted = nullptr;
    const bool started = SendToOpenEditor(arguments, &g_editorStarted) || StartEditor(arguments, true, &g_editorStarted);
    if (started && chosen) rfa::Log("Level editor started from the main menu to make %s, a new %s level", name.c_str(), mode.c_str());
    else if (started) rfa::Log(known ? "Level editor started from the main menu, for a new %s level" : "Level editor started from the main menu%s",
                               known ? mode.c_str() : "");
    lua_pushboolean(L, started);
    return 1;
}

// ---- Level editor > Create level, the menu's own page ------------------------------------------------------------

// A file name New level takes: lower case letters, digits and _, 2 to 31, a letter first (as the editor checks).
bool GoodLevelName(const std::string& name) {
    if (name.size() < 2 || name.size() > 31 || name[0] < 'a' || name[0] > 'z') return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

// "ok", "bad" (not a file name New level takes), "game" (one of the game's own levels) or "taken" (a level the editor
// made already: it can be made again, new).
const char* LevelNameCheck(const std::string& name) {
    if (!GoodLevelName(name)) return "bad";
    std::wstring gameDir = rfa::game::InstalledExePath();
    gameDir.resize(gameDir.find_last_of(L'\\'));
    const std::wstring archive = L"\\build\\pc\\cache\\" + std::wstring(name.begin(), name.end()) + L".vpp_pc";
    if (GetFileAttributesW((rfa::RootDir() + L"\\overrides" + archive).c_str()) != INVALID_FILE_ATTRIBUTES) return "taken";
    if (GetFileAttributesW((gameDir + archive).c_str()) != INVALID_FILE_ATTRIBUTES) return "game";
    return "ok";
}

// Lua: rfa_new_level_check(name) -> "ok", "bad", "game" or "taken" (see LevelNameCheck)
int __cdecl LuaNewLevelCheck(lua_State* L) {
    const char* name = rfa::game::lua_tolstring(L, 1, nullptr);
    rfa::game::lua_pushstring(L, LevelNameCheck(name ? name : ""));
    return 1;
}

// Lua: rfa_new_level_suggest(mode) -> a free file name for a new level of the mode: custom_ruin_01, custom_ruin_02...
int __cdecl LuaNewLevelSuggest(lua_State* L) {
    const char* mode = rfa::game::lua_tolstring(L, 1, nullptr);
    const std::string stem = (mode && _stricmp(mode, "infestation") == 0) ? "custom_infestation_" : "custom_ruin_";
    std::string name = stem + "01";
    for (int n = 1; n < 100; ++n) {
        char number[4];
        sprintf_s(number, "%02d", n);
        if (strcmp(LevelNameCheck(stem + number), "ok") == 0) {
            name = stem + number;
            break;
        }
    }
    rfa::game::lua_pushstring(L, name.c_str());
    return 1;
}

// What the page offers for ground textures and sky: editor\new_level_choices.txt, which the editor writes each time it
// has read the game's files (texture=<level> and sky=<sky> lines). Read again each time the page asks how many.
std::vector<std::string> g_choiceTextures, g_choiceSkies;

void ReadNewLevelChoices() {
    g_choiceTextures.clear();
    g_choiceSkies.clear();
    const std::wstring path = rfa::RootDir() + L"\\editor\\new_level_choices.txt";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    std::string text(GetFileSize(file, nullptr), '\0');
    DWORD read = 0;
    const bool ok = !text.empty() && ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) return;
    text.resize(read);
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("texture=", 0) == 0 && line.size() > 8) g_choiceTextures.push_back(line.substr(8));
        else if (line.rfind("sky=", 0) == 0 && line.size() > 4) g_choiceSkies.push_back(line.substr(4));
    }
}

// Lua: rfa_new_level_choices(kind) -> how many there are of "texture" or "sky" (0: the editor hasn't listed them yet)
int __cdecl LuaNewLevelChoices(lua_State* L) {
    const char* kind = rfa::game::lua_tolstring(L, 1, nullptr);
    ReadNewLevelChoices();
    const auto& list = (kind && strcmp(kind, "sky") == 0) ? g_choiceSkies : g_choiceTextures;
    rfa::game::lua_pushnumber(L, static_cast<double>(list.size()));
    return 1;
}

// Lua: rfa_new_level_choice(kind, n) -> the n'th (from 1) of rfa_new_level_choices' list, or nil
int __cdecl LuaNewLevelChoice(lua_State* L) {
    const char* kind = rfa::game::lua_tolstring(L, 1, nullptr);
    const int n = static_cast<int>(rfa::game::lua_tonumber(L, 2));
    const auto& list = (kind && strcmp(kind, "sky") == 0) ? g_choiceSkies : g_choiceTextures;
    if (n >= 1 && n <= static_cast<int>(list.size())) rfa::game::lua_pushstring(L, list[n - 1].c_str());
    else rfa::game::lua_pushnil(L);
    return 1;
}

// Lua: rfa_text_begin(text, kind) -> true if typing has begun: the keyboard types into `text` ("file": a file name,
// lower case letters, digits and _, up to 31; anything else: a name, up to 40) until Enter or Esc (see menus.cpp).
int __cdecl LuaTextBegin(lua_State* L) {
    const char* text = rfa::game::lua_tolstring(L, 1, nullptr);
    const char* kind = rfa::game::lua_tolstring(L, 2, nullptr);
    const bool file = kind && strcmp(kind, "file") == 0;
    rfa::game::lua_pushboolean(L, BeginTyping(text ? text : "", file, file ? 31 : 40));
    return 1;
}

// Lua: rfa_text_state() -> what is typed, and -1 (not typing), 0 (typing), 1 (Enter: kept) or 2 (Esc: put back)
int __cdecl LuaTextState(lua_State* L) {
    std::string text;
    const int state = TypingState(&text);
    rfa::game::lua_pushstring(L, text.c_str());
    rfa::game::lua_pushnumber(L, state);
    return 2;
}

// Lua: rfa_text_end() -> the keyboard is the game's again
int __cdecl LuaTextEnd(lua_State*) {
    EndTyping();
    return 0;
}

// The Ruin levels made with New level: rows of our wrecking_crew_maps.xtbl (the editor writes it to
// table.vpp_pc's folder in overrides\, or misc.vpp_pc's: every table is in both) with no game archive.
std::vector<std::string> CustomRuinLevels() {
    std::vector<std::string> levels;
    std::wstring gameDir = rfa::game::InstalledExePath();
    gameDir.resize(gameDir.find_last_of(L'\\'));
    for (const wchar_t* archive : {L"table.vpp_pc", L"misc.vpp_pc"}) {
        const std::wstring path = rfa::RootDir() + L"\\overrides\\build\\pc\\cache\\" + archive + L"\\wrecking_crew_maps.xtbl";
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) continue;
        std::string text(GetFileSize(file, nullptr), '\0');
        DWORD read = 0;
        const bool ok = !text.empty() && ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
        CloseHandle(file);
        if (!ok) continue;
        text.resize(read);
        for (size_t row = text.find("<wc_map_list>"); row != std::string::npos; row = text.find("<wc_map_list>", row + 1)) {
            const size_t end = text.find("</wc_map_list>", row);
            const size_t open = text.find("<file_name>", row);
            if (end == std::string::npos || open == std::string::npos || open > end) continue;
            const size_t start = open + 11, close = text.find("</file_name>", start);
            if (close == std::string::npos || close > end) continue;
            std::string name = text.substr(start, close - start);
            name.erase(0, name.find_first_not_of(" \t\r\n"));
            name.erase(name.find_last_not_of(" \t\r\n") + 1);
            if (name.empty() || name.find_first_of("\\/:*?\"<>| ") != std::string::npos) continue;
            const std::wstring wide(name.begin(), name.end());
            if (GetFileAttributesW((gameDir + L"\\build\\pc\\cache\\" + wide + L".vpp_pc").c_str()) != INVALID_FILE_ATTRIBUTES) continue;
            if (std::find(levels.begin(), levels.end(), name) == levels.end()) levels.push_back(name);
        }
        break;  // the first copy found is the one the game reads
    }
    return levels;
}

// Lua: rfa_custom_levels() -> the names, as separate values ({rfa_custom_levels()} makes a list)
int __cdecl LuaCustomLevels(lua_State* L) {
    const std::vector<std::string> levels = CustomRuinLevels();
    for (const std::string& level : levels) rfa::game::lua_pushstring(L, level.c_str());
    return static_cast<int>(levels.size());
}

HANDLE g_deleting = nullptr;  // the editor deleting a level, while it runs
std::string g_deletingName;

// Lua: rfa_delete_custom_level(name) -> true if the editor was started to delete it
int __cdecl LuaDeleteCustomLevel(lua_State* L) {
    const char* name = rfa::game::lua_tolstring(L, 1, nullptr);
    bool started = false;
    if (g_deleting) rfa::Log("Delete custom level: already deleting %s", g_deletingName.c_str());
    else if (name) {
        const std::vector<std::string> levels = CustomRuinLevels();
        if (std::find(levels.begin(), levels.end(), std::string(name)) == levels.end())
            rfa::Log("Delete custom level: %s isn't a level made with the editor", name);
        else if ((started = StartEditor(L"--delete-level " + std::wstring(name, name + strlen(name)), false, &g_deleting))) {
            g_deletingName = name;
            rfa::Log("Delete custom level: the editor is deleting %s (what it does goes to logs\\editor.log)", name);
        }
    }
    rfa::game::lua_pushboolean(L, started);
    return 1;
}

// Lua: rfa_delete_custom_level_done() -> nil while the editor is deleting, else its exit code (0: deleted)
int __cdecl LuaDeleteCustomLevelDone(lua_State* L) {
    if (g_deleting && WaitForSingleObject(g_deleting, 0) == WAIT_TIMEOUT) {
        rfa::game::lua_pushnil(L);
        return 1;
    }
    DWORD code = 0;
    if (g_deleting) {
        GetExitCodeProcess(g_deleting, &code);
        CloseHandle(g_deleting);
        g_deleting = nullptr;
        rfa::Log(code == 0 ? "Delete custom level: %s deleted; the game reads its map list at start-up, so restart it"
                           : "Delete custom level: %s wasn't deleted (editor exit code %lu)", g_deletingName.c_str(), code);
    }
    rfa::game::lua_pushnumber(L, static_cast<double>(code));
    return 1;
}

// Runs instead of the game's function that registers its C functions with the menu Lua state:
// lets it do that, then adds ours, then each mod's: a mod that exports
//     extern "C" __declspec(dllexport) void __cdecl RFAMod_RegisterLua(rfa::game::lua_State* L);
// adds its own functions there (rfa::game::lua_register), for its interface scripts to call.
void __cdecl RegisterLuaFunctions(lua_State* L) {
    using namespace rfa::game;
    g_registerGameFunctions(L);
    lua_register(L, "rfa_get_setting", LuaGetSetting);
    lua_register(L, "rfa_set_setting", LuaSetSetting);
    lua_register(L, "rfa_log", LuaLog);
    lua_register(L, "rfa_open_level_editor", LuaOpenLevelEditor);
    lua_register(L, "rfa_custom_levels", LuaCustomLevels);
    lua_register(L, "rfa_delete_custom_level", LuaDeleteCustomLevel);
    lua_register(L, "rfa_delete_custom_level_done", LuaDeleteCustomLevelDone);
    lua_register(L, "rfa_guerrilla_world_built", LuaGuerrillaWorldBuilt);
    lua_register(L, "rfa_open_guerrilla_builder", LuaOpenGuerrillaBuilder);
    lua_register(L, "rfa_console_command", LuaConsoleCommand);
    lua_register(L, "rfa_level_editor_ready", LuaLevelEditorReady);
    lua_register(L, "rfa_load_ui_pack", LuaLoadUiPack);
    lua_register(L, "rfa_processing_popup", LuaProcessingPopup);
    lua_register(L, "rfa_failed_level", LuaFailedLevel);
    lua_register(L, "rfa_new_level_check", LuaNewLevelCheck);
    lua_register(L, "rfa_new_level_suggest", LuaNewLevelSuggest);
    lua_register(L, "rfa_new_level_choices", LuaNewLevelChoices);
    lua_register(L, "rfa_new_level_choice", LuaNewLevelChoice);
    lua_register(L, "rfa_text_begin", LuaTextBegin);
    lua_register(L, "rfa_text_state", LuaTextState);
    lua_register(L, "rfa_text_end", LuaTextEnd);
    rfa::Log("Added rfa_get_setting, rfa_set_setting, rfa_log and the level editor's functions to the game's menu Lua");
    WatchLuaErrors();
    rfa::Log("Lua script errors go to this log (\"Lua error: ...\")");
    using RegisterLuaFn = void(__cdecl*)(lua_State*);
    for (HMODULE mod : g_mods)
        if (auto registerLua = reinterpret_cast<RegisterLuaFn>(GetProcAddress(mod, "RFAMod_RegisterLua"))) {
            registerLua(L);
            wchar_t file[MAX_PATH];
            GetModuleFileNameW(mod, file, MAX_PATH);
            rfa::Log("%ls added its functions to the game's menu Lua", wcsrchr(file, L'\\') ? wcsrchr(file, L'\\') + 1 : file);
        }
}

// Looks through overrides\ and edits\ again for files added since the game started (the editor asks
// for this before it has the game load a level again or reload a table, so a zone or table saved
// for the first time while the game runs is used at once). How many new files it saw. A file
// already known is read from the disk each time the game opens it, so a changed one needs no rescan.
bool g_overridesOn = false;
extern "C" __declspec(dllexport) int __cdecl RFALoader_RescanOverrides() {
    if (!g_overridesOn) return 0;
    AcquireSRWLockExclusive(&g_filesLock);
    const size_t before = g_seenFiles.size();
    FindOverrides(rfa::RootDir() + L"\\overrides", L"");
    FindEdits(rfa::RootDir() + L"\\edits", L"");
    const int added = static_cast<int>(g_seenFiles.size() - before);
    ReleaseSRWLockExclusive(&g_filesLock);
    if (added) rfa::Log("Overrides looked through again: %d new file(s)", added);
    return added;
}

void InstallOverrides() {
    using namespace rfa::game;
    // "ScriptOverrides" is the setting's old name, from when only Lua scripts could be overridden.
    if (!rfa::IniInt(L"Loader", L"Overrides", rfa::IniInt(L"Loader", L"ScriptOverrides", 1))) {
        rfa::Log("Overrides: off ([Loader] Overrides=0)");
        return;
    }
    g_overridesOn = true;
    FindOverrides(rfa::RootDir() + L"\\overrides", L"");
    FindEdits(rfa::RootDir() + L"\\edits", L"");  // changes to the archives the mod adds (the Guerrilla world's)
    g_logLookups = rfa::ReadLogSwitch(L"Loader", L"LogLookups");
    if (g_logLookups) {
        rfa::Log("Logging lookups of level files ([Loader] LogLookups=1)");
        g_beginRuinMatch = reinterpret_cast<BeginRuinMatchFn>(
            rfa::HookFunction(kBeginRuinMatch, kBeginRuinMatchPrologue, reinterpret_cast<void*>(&BeginRuinMatchHook)));
        if (!g_beginRuinMatch) rfa::Log("Could not hook the Ruin match start; its map list won't be logged");
    }
    g_loadBuffer = reinterpret_cast<LoadBufferFn>(
        rfa::HookFunction(kLuaLoadBuffer, kLuaLoadBufferPrologue, reinterpret_cast<void*>(&LoadBufferHook)));
    g_findFile = reinterpret_cast<FindFileFn>(
        rfa::HookFunction(kFindFile, kFindFilePrologue, reinterpret_cast<void*>(&FindFileHook)));
    if (rfa::ReplaceFunction(kReadRuinMaps, reinterpret_cast<const void*>(&ReadRuinMaps)))
        rfa::Log("Ruin takes up to %d maps from wrecking_crew_maps.xtbl, each started by its file_name", kRuinMostMaps);
    else
        rfa::Log("Could not replace the Ruin map reader; Ruin keeps its five maps, started by their place in its table");
    const auto newFiles = std::count_if(g_newFiles.begin(), g_newFiles.end(),
                                        [](const auto& file) { return file.first.find('\\') != std::string::npos; });
    rfa::Log("Overrides: %u in overrides\\, and %u new files; Lua scripts %s, other files %s",
             static_cast<unsigned>(g_overrideCount), static_cast<unsigned>(newFiles),
             g_loadBuffer ? "on" : "FAILED", g_findFile ? "on" : "FAILED");
}

void InstallLuaFunctions() {
    using namespace rfa::game;
    g_registerGameFunctions = reinterpret_cast<RegisterFn>(rfa::HookFunction(
        kRegisterGameLuaFunctions, kRegisterGameLuaFunctionsPrologue, reinterpret_cast<void*>(&RegisterLuaFunctions)));
    if (!g_registerGameFunctions) rfa::Log("Could not hook the game's Lua registration; rfa_* functions are off");
}

// Mods that were DLLs of their own and are now parts of another. An update copied over an older
// install leaves their old DLLs in mods\; while the DLL that holds them now is there too, the old
// ones are skipped, so their hooks don't go in twice.
struct MergedMod {
    const wchar_t* oldDll;
    const wchar_t* nowIn;
};
constexpr MergedMod kMergedMods[] = {
    {L"borderless.dll", L"rfa_qol.dll"},
    {L"console.dll", L"rfa_qol.dll"},
    {L"fov.dll", L"rfa_qol.dll"},
    {L"video_settings.dll", L"rfa_qol.dll"},
    {L"guerrilla_campaign.dll", L"rfa_guerrilla.dll"},
    {L"guerrilla_gear.dll", L"rfa_guerrilla.dll"},
    {L"guerrilla_hud.dll", L"rfa_guerrilla.dll"},
    {L"guerrilla_kiosks.dll", L"rfa_guerrilla.dll"},
    {L"guerrilla_people.dll", L"rfa_guerrilla.dll"},
    {L"guerrilla_save.dll", L"rfa_guerrilla.dll"},
    {L"guerrilla_vehicles.dll", L"rfa_guerrilla.dll"},
    {L"guerrilla_voices.dll", L"rfa_guerrilla.dll"},
    {L"guerrilla_world.dll", L"rfa_guerrilla.dll"},
};

// The DLL among `files` that now holds the mod `file` was, or nullptr.
const wchar_t* MergedInto(const std::wstring& file, const std::vector<std::wstring>& files) {
    for (const auto& merged : kMergedMods) {
        if (_wcsicmp(file.c_str(), merged.oldDll) != 0) continue;
        for (const auto& other : files)
            if (_wcsicmp(other.c_str(), merged.nowIn) == 0) return merged.nowIn;
    }
    return nullptr;
}

}  // namespace

// Called by the launcher on a remote thread while the game's main thread is parked.
// Returns the number of mods loaded.
extern "C" DWORD WINAPI RFALoader_Init(LPVOID) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    rfa::Log("Loader running in %ls (pid %lu)", exe, GetCurrentProcessId());
    rfa::Log("Game build: %s", rfa::game::BuildDescription());
    if (rfa::ReadLogSwitch(L"Loader"))
        rfa::Log("[Loader] Log=1: the loader also logs each file it adds, replaces or is asked for (\"[loader:log]\" lines)");
    InstallInstalledExePath();
    InstallCrashReporter();
    InstallLogAtExit();
    if (rfa::game::IsSupportedBuild()) {
        InstallOverrides();
        InstallRuinQuit();
        InstallLuaFunctions();
        InstallProcessingHold();
        InstallLoadFailure();
        InstallMenus(&g_mods);
        InstallMenuMusic();
        FindUiPack();
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
        if (const wchar_t* nowIn = MergedInto(file, files)) {
            rfa::Log("Skipping %ls: an old copy of a mod that is now part of %ls (you can delete it)",
                     file.c_str(), nowIn);
            continue;
        }
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

// A line for logs\rfa_mods.log from any module (rfa_mod.h's rfa::Log): queued for the log's writer, or
// written at once after a crash (see "The log, written off the game's threads").
extern "C" __declspec(dllexport) void __cdecl RFALoader_Log(const char* line, unsigned length) {
    InitOnceExecuteOnce(&g_logOnce, StartLog, nullptr, nullptr);
    if (g_logExiting) {  // the game is exiting: at once, without the locks (see WriteLogAsGameExits)
        WriteLogText(std::string(line, length));
        return;
    }
    if (g_logDirect) {
        EnterCriticalSection(&g_logWriteLock);
        FlushLogQueue();
        WriteLogText(std::string(line, length));
        LeaveCriticalSection(&g_logWriteLock);
        return;
    }
    EnterCriticalSection(&g_logQueueLock);
    g_logQueue.append(line, length);
    LeaveCriticalSection(&g_logQueueLock);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    if (reason == DLL_PROCESS_DETACH && reserved) WriteLogAtExit();  // the process is ending
    return TRUE;
}
