// profiler.cpp: how much of the game's main and render threads' time our code takes (the loader's and the loaded
// mods'), sampled. For development: off unless [Loader] Profile=1.
//
// A thread of its own wakes every 2 ms, stops each watched thread for an instant, copies its registers and the top
// of its stack, and lets it go (as guerrilla_world's watchdog does: nothing is looked up, allocated or logged while a
// thread is stopped, so it can't wait on a lock the stopped thread holds). Then, for that sample:
//   - in our code: ours, counted at that address;
//   - in Windows or a driver: whoever called it, the first return address up the frame chain that is ours or the
//     game's (Windows' and the game's code keep frame pointers; failing that, the first such value on the stack);
//   - in the game's code: the game's, even when our code called it. "Under" counts those by the first of our
//     functions up the chain: a hook that wraps a game function shows the whole function there, so read it as
//     "game work run from our code", not as our cost.
// Every 30 s it writes a summary to logs\rfa_mods.log (file only, "[profile:log]"): the frames presented, each
// thread's samples, our share as ms a frame per DLL, and the addresses sampled (DLL offset:count).
// The linker's map files, which build.cmd writes next to the DLLs (rfa_loader.map, mods\rfa_qol.map,
// mods\rfa_guerrilla.map), name the sampled addresses, to sum them by part. Each look stops
// a thread for a few microseconds, about 1% of its time at 500 looks a second.
#define RFA_MOD_NAME "profile"
#include "rfa_mod.h"
#include "profiler.h"
#include <tlhelp32.h>
#include <cstdarg>
#include <cstdio>
#include <string>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace {

constexpr LONGLONG kSampleEvery100ns = 2 * 10000;  // 2 ms
constexpr double kSummaryEveryS = 30.0;
constexpr size_t kStackCopy = 16 * 1024;  // at most this much of a stopped thread's stack, from esp
constexpr int kFrameSteps = 64;           // frame-chain links followed to find the caller
constexpr int kStackScan = 1024;          // stack slots looked at when the chain gives out
constexpr int kMostOurs = 16;             // our DLLs: the loader and its mods
constexpr int kTableSize = 8192;          // addresses counted in a window (a power of two)
constexpr size_t kLineChars = 1800;       // the log takes lines of up to 2 KB

bool g_on = false;
volatile LONG g_frames = 0;         // frames presented since the last summary
volatile LONG g_presentThread = 0;  // the thread that presents

// Our DLLs' code: the loader's and the loaded mods'. The game's own: the exe.
struct OurModule {
    DWORD base, end;
    char name[40];
};
OurModule g_ours[kMostOurs];
int g_ourCount = 0;
DWORD g_gameBase = 0, g_gameEnd = 0;

enum class Where { Ours, Game, Other };

Where Classify(DWORD address, int* module) {
    if (address >= g_gameBase && address < g_gameEnd) return Where::Game;
    for (int i = 0; i < g_ourCount; ++i)
        if (address >= g_ours[i].base && address < g_ours[i].end) {
            *module = i;
            return Where::Ours;
        }
    return Where::Other;
}

// An address of ours as one number: which DLL (from 1) and where in it.
DWORD Key(int module, DWORD address) { return (static_cast<DWORD>(module + 1) << 26) | (address - g_ours[module].base); }

// Addresses and how often they were sampled, in a window. Fixed in size: no allocation while sampling.
struct AddressTable {
    DWORD keys[kTableSize];
    DWORD counts[kTableSize];
    int used;
    DWORD dropped;  // samples not counted: the table was three quarters full

    void Add(DWORD key) {
        DWORD slot = (key * 2654435761u) & (kTableSize - 1);
        for (int probe = 0; probe < kTableSize; ++probe, slot = (slot + 1) & (kTableSize - 1)) {
            if (keys[slot] == key) {
                ++counts[slot];
                return;
            }
            if (!keys[slot]) {
                if (used >= kTableSize * 3 / 4) break;
                keys[slot] = key;
                counts[slot] = 1;
                ++used;
                return;
            }
        }
        ++dropped;
    }
    void Clear() {
        memset(keys, 0, sizeof(keys));
        memset(counts, 0, sizeof(counts));
        used = 0;
        dropped = 0;
    }
};

// A thread watched, and what its samples showed in this window.
struct WatchedThread {
    const char* label;
    DWORD id;
    HANDLE handle;
    DWORD samples, game, other;
    DWORD ours[kMostOurs];
    AddressTable self, under;

    void ClearCounts() {
        samples = game = other = 0;
        memset(ours, 0, sizeof(ours));
        self.Clear();
        under.Clear();
    }
};
WatchedThread g_main = {"main"};
WatchedThread g_render = {"render"};
DWORD g_stack[kStackCopy / 4];

// Copies memory that may not be readable; false if it wasn't.
bool CopyIfReadable(void* to, const void* from, size_t size) {
    __try {
        memcpy(to, from, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// How much of a stopped thread's stack, from esp, can be copied (at most `most` bytes): the rest of the committed,
// readable stretch it is in, so the copy never faults (a fault would go through the loader's handler, which logs).
size_t StackBytes(DWORD esp, size_t most) {
    MEMORY_BASIC_INFORMATION info;
    const void* at = reinterpret_cast<const void*>(static_cast<uintptr_t>(esp));
    if (!VirtualQuery(at, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    const size_t left = static_cast<const BYTE*>(info.BaseAddress) + info.RegionSize - static_cast<const BYTE*>(at);
    return left < most ? left & ~static_cast<size_t>(3) : most;
}

// One look at a thread: stopped while its registers and stack are copied, then counted.
void Sample(WatchedThread& thread) {
    CONTEXT context = {};
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    size_t copied = 0;
    if (SuspendThread(thread.handle) == static_cast<DWORD>(-1)) return;
    const bool got = GetThreadContext(thread.handle, &context) != 0;
    if (got) {
        copied = StackBytes(context.Esp, sizeof(g_stack));
        if (!CopyIfReadable(g_stack, reinterpret_cast<const void*>(static_cast<uintptr_t>(context.Esp)), copied)) copied = 0;
    }
    ResumeThread(thread.handle);
    if (!got) return;
    ++thread.samples;

    int module = 0;
    const Where leaf = Classify(context.Eip, &module);
    if (leaf == Where::Ours) {
        ++thread.ours[module];
        thread.self.Add(Key(module, context.Eip));
        return;
    }
    // Up the frame chain: the first return address that is ours or the game's says whose a sample in Windows or a
    // driver is; under a game sample, the first one of ours is the function that ran that game code.
    bool game = leaf == Where::Game;
    bool decided = game;
    DWORD frame = context.Ebp;
    for (int step = 0; step < kFrameSteps; ++step) {
        if (frame < context.Esp || frame - context.Esp + 8 > copied || (frame & 3)) break;
        const DWORD* link = g_stack + (frame - context.Esp) / 4;
        const Where caller = Classify(link[1], &module);
        if (caller == Where::Ours) {
            if (!decided) {
                ++thread.ours[module];
                thread.self.Add(Key(module, link[1]));
                return;
            }
            thread.under.Add(Key(module, link[1]));
            break;
        }
        if (caller == Where::Game) game = decided = true;
        if (link[0] <= frame) break;
        frame = link[0];
    }
    // The chain gave out before saying whose it is: the first value on the stack that is ours or the game's.
    for (size_t slot = 0; !decided && slot < copied / 4 && slot < static_cast<size_t>(kStackScan); ++slot) {
        const Where caller = Classify(g_stack[slot], &module);
        if (caller == Where::Ours) {
            ++thread.ours[module];
            thread.self.Add(Key(module, g_stack[slot]));
            return;
        }
        if (caller == Where::Game) game = decided = true;
    }
    if (game) ++thread.game;
    else ++thread.other;
}

void ProfileLine(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    rfa::LogAs("profile:log", fmt, ap);
    va_end(ap);
}

// A table's addresses, one DLL at a time, in lines of up to kLineChars: "main self rfa_guerrilla.dll: 1a2b0:12 ...".
void WriteTable(const WatchedThread& thread, const char* kind, const AddressTable& table) {
    for (int module = 0; module < g_ourCount; ++module) {
        const std::string prefix = std::string(thread.label) + " " + kind + " " + g_ours[module].name + ":";
        std::string line = prefix;
        for (int slot = 0; slot < kTableSize; ++slot) {
            if (!table.keys[slot] || static_cast<int>(table.keys[slot] >> 26) != module + 1) continue;
            char entry[32];
            snprintf(entry, sizeof(entry), " %lx:%lu", table.keys[slot] & ((1u << 26) - 1), table.counts[slot]);
            if (line.size() + strlen(entry) > kLineChars) {
                ProfileLine("%s", line.c_str());
                line = prefix;
            }
            line += entry;
        }
        if (line.size() > prefix.size()) ProfileLine("%s", line.c_str());
    }
    if (table.dropped) ProfileLine("%s %s: %lu samples not counted (the table was full)", thread.label, kind, table.dropped);
}

void WriteSummary(int window, double seconds, LONG frames, const WatchedThread& thread) {
    if (!thread.samples) return;
    const double msPerFrame = frames > 0 ? seconds * 1000.0 / frames : 0.0;
    DWORD oursAll = 0;
    std::string perModule;
    for (int module = 0; module < g_ourCount; ++module) {
        oursAll += thread.ours[module];
        char part[96];
        snprintf(part, sizeof(part), "%s%s %.3f", perModule.empty() ? "" : ", ", g_ours[module].name,
                 msPerFrame * thread.ours[module] / thread.samples);
        perModule += part;
    }
    ProfileLine("Window %d, %s thread %lu: %lu samples; ours %.3f ms a frame (%.2f%%): %s; the game's %.1f%%, Windows' and "
                "drivers' %.1f%%",
                window, thread.label, thread.id, thread.samples, msPerFrame * oursAll / thread.samples,
                100.0 * oursAll / thread.samples, perModule.c_str(), 100.0 * thread.game / thread.samples,
                100.0 * thread.other / thread.samples);
    WriteTable(thread, "self", thread.self);
    WriteTable(thread, "under", thread.under);
}

DWORD WINAPI ProfilerThread(void*) {
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);  // an older Windows: coarser looks
    if (!timer) {
        ProfileLine("Profiler: no timer (error %lu); not sampling", GetLastError());
        return 0;
    }
    LARGE_INTEGER rate, windowStart, now;
    QueryPerformanceFrequency(&rate);
    QueryPerformanceCounter(&windowStart);
    InterlockedExchange(&g_frames, 0);
    int window = 0;
    for (;;) {
        LARGE_INTEGER due;
        due.QuadPart = -kSampleEvery100ns;
        SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
        WaitForSingleObject(timer, INFINITE);

        // The thread that presents, once it has (if it isn't the main thread).
        const DWORD presenter = static_cast<DWORD>(g_presentThread);
        if (presenter && presenter != g_main.id && presenter != g_render.id) {
            if (g_render.handle) CloseHandle(g_render.handle);
            g_render.id = presenter;
            g_render.handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, presenter);
            g_render.ClearCounts();
            ProfileLine("Profiler: the thread that presents is %lu (the render thread); sampling it too", presenter);
        }
        if (g_main.handle) Sample(g_main);
        if (g_render.handle) Sample(g_render);

        QueryPerformanceCounter(&now);
        const double seconds = static_cast<double>(now.QuadPart - windowStart.QuadPart) / rate.QuadPart;
        if (seconds < kSummaryEveryS) continue;
        const LONG frames = InterlockedExchange(&g_frames, 0);
        ++window;
        ProfileLine("Window %d: %.1f s, %ld frames, %.2f ms a frame", window, seconds, frames,
                    frames > 0 ? seconds * 1000.0 / frames : 0.0);
        WriteSummary(window, seconds, frames, g_main);
        WriteSummary(window, seconds, frames, g_render);
        g_main.ClearCounts();
        g_render.ClearCounts();
        windowStart = now;
    }
}

// The process's first thread, the one the game's code started on: its main thread.
DWORD FirstThreadOfProcess() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    const DWORD process = GetCurrentProcessId();
    DWORD first = 0;
    ULONGLONG firstAt = ~0ull;
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != process) continue;
        const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        FILETIME created, exited, kernel, user;
        if (GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
            const ULONGLONG at = (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
            if (at < firstAt) {
                firstAt = at;
                first = entry.th32ThreadID;
            }
        }
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    return first;
}

bool AddOurModule(HMODULE module) {
    if (!module || g_ourCount >= kMostOurs) return false;
    auto base = reinterpret_cast<BYTE*>(module);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    OurModule& ours = g_ours[g_ourCount];
    ours.base = static_cast<DWORD>(reinterpret_cast<uintptr_t>(module));
    ours.end = ours.base + nt->OptionalHeader.SizeOfImage;
    char path[MAX_PATH] = {};
    GetModuleFileNameA(module, path, MAX_PATH);
    const char* name = strrchr(path, '\\');
    strncpy_s(ours.name, name ? name + 1 : path, _TRUNCATE);
    ++g_ourCount;
    return true;
}

}  // namespace

void ProfilerFramePresented() {
    if (!g_on) return;
    InterlockedIncrement(&g_frames);
    InterlockedExchange(&g_presentThread, static_cast<LONG>(GetCurrentThreadId()));
}

void StartProfiler(const std::vector<HMODULE>& mods) {
    if (!rfa::IniInt(L"Loader", L"Profile", 0)) return;
    const HMODULE game = GetModuleHandleW(nullptr);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<BYTE*>(game) + reinterpret_cast<IMAGE_DOS_HEADER*>(game)->e_lfanew);
    g_gameBase = static_cast<DWORD>(reinterpret_cast<uintptr_t>(game));
    g_gameEnd = g_gameBase + nt->OptionalHeader.SizeOfImage;
    AddOurModule(GetModuleHandleW(L"rfa_loader.dll"));
    for (HMODULE mod : mods) AddOurModule(mod);
    g_main.id = FirstThreadOfProcess();
    g_main.handle = g_main.id ? OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_main.id) : nullptr;
    if (!g_main.handle) {
        ProfileLine("Profiler: could not open the game's main thread (%lu, error %lu); not sampling", g_main.id, GetLastError());
        return;
    }
    g_on = true;
    if (HANDLE thread = CreateThread(nullptr, 0, ProfilerThread, nullptr, 0, nullptr)) {
        SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST);  // on time, so the looks are evenly spread
        CloseHandle(thread);
        ProfileLine("Profiler on ([Loader] Profile=1): the main thread %lu, and the thread that presents, looked at every "
                    "2 ms; a summary every 30 s. %d DLLs of ours", g_main.id, g_ourCount);
    } else {
        g_on = false;
        ProfileLine("Profiler: could not start its thread (error %lu)", GetLastError());
    }
}
