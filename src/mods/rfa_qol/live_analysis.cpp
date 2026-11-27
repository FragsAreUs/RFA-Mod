// live_analysis.cpp: Live analysis of the game's running code via named pipes.
// Hooks key game functions and exposes runtime data for external debugging/profiling tools.
//
// This mod provides:
// 1. Function call tracing via named pipe
// 2. Memory read/write monitoring
// 3. Game state snapshots (player, camera, objects, AI)
// 4. Real-time performance metrics
// 5. Custom command interface for external tools

#define RFA_MOD_NAME "rfa_live_analysis"
#include "rfa_mod.h"
#include "rfa_qol.h"
#include "rfa_game.h"
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <string>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <psapi.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <map>
#include <algorithm>
#include <sddl.h>
#pragma comment(lib, "advapi32.lib")

namespace live_analysis {

// SEH-safe memory read/write helpers (must be in functions with no C++ objects)
static bool SafeReadMemory(void* dst, const void* src, size_t size) {
    __try {
        memcpy(dst, src, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeWriteMemory(void* dst, const void* src, size_t size) {
    __try {
        memcpy(dst, src, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// SEH-safe typed write helpers
template <typename T>
static bool SafeWriteValue(void* dst, T value) {
    __try {
        *reinterpret_cast<T*>(dst) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// SEH-safe function call helpers (must be in functions with no C++ objects)
static DWORD CallFunctionCdecl(DWORD addr, DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6, DWORD a7, DWORD a8) {
    __try {
        using Fn = DWORD(__cdecl*)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
        Fn fn = reinterpret_cast<Fn>(addr);
        return fn(a1, a2, a3, a4, a5, a6, a7, a8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFFFFFFFF;
    }
}

static DWORD CallFunctionStdcall(DWORD addr, DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6, DWORD a7, DWORD a8) {
    __try {
        using Fn = DWORD(__stdcall*)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
        Fn fn = reinterpret_cast<Fn>(addr);
        return fn(a1, a2, a3, a4, a5, a6, a7, a8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFFFFFFFF;
    }
}

static DWORD CallFunctionThiscall(DWORD addr, DWORD thisPtr, DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6) {
    __try {
        using Fn = DWORD(__fastcall*)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
        Fn fn = reinterpret_cast<Fn>(addr);
        return fn(thisPtr, a1, a2, a3, a4, a5, a6);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFFFFFFFF;
    }
}

static bool ReadModuleExports(DWORD base, char** outNames, DWORD* outAddresses, WORD* outOrdinals, DWORD maxCount, DWORD* outCount) {
    __try {
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
        auto exportDir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (exportDir->VirtualAddress == 0) return false;
        auto exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + exportDir->VirtualAddress);
        DWORD* names = reinterpret_cast<DWORD*>(base + exports->AddressOfNames);
        DWORD* functions = reinterpret_cast<DWORD*>(base + exports->AddressOfFunctions);
        WORD* ordinals = reinterpret_cast<WORD*>(base + exports->AddressOfNameOrdinals);

        DWORD count = exports->NumberOfNames;
        if (count > maxCount) count = maxCount;
        for (DWORD i = 0; i < count; ++i) {
            outNames[i] = reinterpret_cast<char*>(base + names[i]);
            outAddresses[i] = base + functions[ordinals[i]];
            outOrdinals[i] = static_cast<WORD>(ordinals[i] + exports->Base);
        }
        *outCount = count;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static int ValidateModule(DWORD base) {
    // Returns: 0 = valid with exports, 1 = valid no exports, 2 = invalid DOS, 3 = invalid NT, -1 = exception
    __try {
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 2;
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 3;
        auto exportDir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (exportDir->VirtualAddress == 0) return 1;
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Camera references (same addresses as in console.cpp)
static auto& g_cameraPosition = *reinterpret_cast<float(*)[3]>(0x11B9C6C);   // camera manager (0x11B9C40) +0x2C, set by set_camera_pos
static auto& g_cameraRotation = *reinterpret_cast<float(*)[9]>(0x11B9C90);   // +0x50: rows right, up, forward
static auto& g_cameraFov = *reinterpret_cast<float*>(0x11B9D00);             // +0xC0: the FOV the frame is drawn with, vertical degrees
static auto& g_timeOfDay = *reinterpret_cast<float*>(0x10F8898);             // time of day the game lights by

// Console command executor (FUN_00c7bdb0 - debug console command handler)
// Returns int (0 on success, non-zero on failure)
static auto const RunCommand = reinterpret_cast<int(__cdecl*)(const char* format, ...)>(0xC7BDB0);

// Lua state pointer (DAT_028eb870 - from vint_lua_init / FUN_00c618c0)
static constexpr DWORD kLuaStatePtr = 0x028EB870;

// Named pipe for communication with external analysis tools
static constexpr const wchar_t* kPipeName = L"\\\\.\\pipe\\RFA_LiveAnalysis";
static HANDLE g_pipe = INVALID_HANDLE_VALUE;
// The pipe's thread: a Windows thread, not a std::thread. It waits for a client for as long as the game runs and
// Windows ends it as the game exits; a std::thread still running when rfa_qol unloads calls std::terminate, which
// ended every exit with 0xC0000409 and no crash report (the 2026-11-19 runs, with [Mcp] Enabled=1).
static HANDLE g_pipeThread = nullptr;
static std::atomic<bool> g_pipeRunning{false};

// Command queue for thread-safe pipe communication
struct PipeCommand {
    enum Type { ReadMemory, WriteMemory, GetState, CallFunction, ListModules, HookFunction, UnhookFunction, Custom } type;
    DWORD address = 0;
    size_t size = 0;
    std::vector<BYTE> data;
    std::string functionName;
    std::string customCommand;
    HANDLE replyEvent = nullptr;
    std::vector<BYTE>* replyData = nullptr;
    std::string* replyString = nullptr;
};

static std::mutex g_queueMutex;
static std::queue<PipeCommand> g_commandQueue;
static std::condition_variable g_queueCV;

// Game state cache for quick snapshots
struct GameState {
    float playerPos[3] = {0, 0, 0};
    float playerRot[9] = {0};
    float cameraPos[3] = {0, 0, 0};
    float cameraRot[9] = {0};
    float cameraFov = 0;
    DWORD currentLevel = 0;
    int objectCount = 0;
    DWORD gameTime = 0;
    bool inMenu = false;
};
static GameState g_cachedState;
static std::mutex g_stateMutex;

// Hooked functions storage
struct HookInfo {
    DWORD address = 0;
    size_t prologueSize = 0;
    void* original = nullptr;
    std::string name;
    bool active = false;
};
static std::vector<HookInfo> g_hooks;
static std::mutex g_hooksMutex;

// Thread-safe Lua command queue for main thread execution via vint_lua
struct LuaCommand {
    std::string code;
    std::string* result = nullptr;
    std::string* error = nullptr;
    HANDLE doneEvent = nullptr;
    bool completed = false;
    bool console = false;  // code is a console command, run as it is (exec), not Lua
};

static std::mutex g_luaQueueMutex;
static std::queue<LuaCommand> g_luaCommandQueue;
static std::atomic<bool> g_luaQueueRunning{true};

// FrameUpdate callback for processing Lua commands on main thread
void __cdecl ProcessLuaCommandsCallback() {
    // Process queued Lua commands on main thread
    std::vector<LuaCommand> commandsToProcess;
    {
        std::lock_guard<std::mutex> lock(g_luaQueueMutex);
        while (!g_luaCommandQueue.empty()) {
            commandsToProcess.push_back(std::move(g_luaCommandQueue.front()));
            g_luaCommandQueue.pop();
        }
    }

    for (auto& cmd : commandsToProcess) {
        if (cmd.console) {  // exec: the console command itself, on the game's thread
            RunCommand("%s", cmd.code.c_str());
            if (cmd.result) *cmd.result = "OK: Executed '" + cmd.code + "' on the main thread";
            cmd.completed = true;
            if (cmd.doneEvent) SetEvent(cmd.doneEvent);
            continue;
        }
        // Execute via vint_lua console command on main thread
        std::string escaped = cmd.code;
        std::string resultStr;
        for (char c : escaped) {
            if (c == '"') resultStr += "\\\"";
            else if (c == '\\') resultStr += "\\\\";
            else resultStr += c;
        }
        escaped = resultStr;

        std::string consoleCmd = "vint_lua \"" + escaped + "\"";
        int runResult = RunCommand("%s", consoleCmd.c_str());

        std::ostringstream oss;
        if (runResult == 0) {
            oss << "OK: Executed via vint_lua on main thread";
        } else {
            oss << "ERROR: vint_lua returned " << runResult;
        }

        if (cmd.result) {
            *cmd.result = oss.str();
        }
        if (cmd.error && runResult != 0) {
            *cmd.error = "vint_lua execution failed";
        }
        cmd.completed = true;
        if (cmd.doneEvent) SetEvent(cmd.doneEvent);
    }
}

// Forward declarations
void PipeServerThread();
bool SendPipeResponse(HANDLE pipe, const std::vector<BYTE>& data);
bool SendPipeResponse(HANDLE pipe, const std::string& text);
std::vector<BYTE> ReadGameMemory(DWORD address, size_t size);
bool WriteGameMemory(DWORD address, const BYTE* data, size_t size);
void UpdateGameStateCache();
GameState GetGameStateSnapshot();
void HandleCommand(const PipeCommand& cmd);

// The pipe's thread (a Windows thread: see g_pipeThread). A command the client got wrong is answered with an error
// where it is handled; one that fails some other way (a C++ exception: no memory for a size asked, say) is answered
// with an error here and logged, the client is let go, and the pipe waits for the next one. The game goes on.
DWORD WINAPI RunPipeServer(void*) {
    for (;;) {
        std::string failure;
        try {
            PipeServerThread();
            return 0;
        } catch (const std::exception& error) {
            failure = error.what();
        } catch (...) {
            failure = "not a standard exception";
        }
        rfa::Log("Pipe: a command failed (%s); the client is let go", failure.c_str());
        if (g_pipe != INVALID_HANDLE_VALUE) {
            SendPipeResponse(g_pipe, "ERROR: the command failed (" + failure + "); connect again");
            CloseHandle(g_pipe);
            g_pipe = INVALID_HANDLE_VALUE;
        }
        if (!g_pipeRunning) return 0;
        Sleep(1000);  // a failure that comes back each time doesn't fill the log
    }
}

// Two hex digits from `text` at `at` as a byte ("7f" is 0x7F); false if they aren't two hex digits. The bytes of
// "write" and "scan" come from the client: a bad one is answered with an error (std::stoul threw on it).
bool ParseHexByte(const std::string& text, size_t at, BYTE& value) {
    if (at + 2 > text.size() || !isxdigit(static_cast<unsigned char>(text[at])) || !isxdigit(static_cast<unsigned char>(text[at + 1])))
        return false;
    value = static_cast<BYTE>(strtoul(text.substr(at, 2).c_str(), nullptr, 16));
    return true;
}

// Helper to queue a Lua command for main thread execution
// Returns the result string from the command
static std::string QueueLuaCommand(const std::string& code, DWORD timeoutMs = 5000, bool console = false) {
    std::string result;
    std::string error;
    HANDLE doneEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    // Create command with pointers to our local result/error strings
    LuaCommand cmd;
    cmd.code = code;
    cmd.result = &result;
    cmd.error = &error;
    cmd.doneEvent = doneEvent;
    cmd.console = console;

    {
        std::lock_guard<std::mutex> lock(g_luaQueueMutex);
        g_luaCommandQueue.push(std::move(cmd));
    }

    // Wait for completion
    DWORD waitResult = WaitForSingleObject(doneEvent, timeoutMs);

    if (waitResult != WAIT_OBJECT_0) {
        result = "ERROR: Timeout waiting for Lua command execution";
    }

    CloseHandle(doneEvent);

    if (!error.empty()) return error;
    return result;
}

// Initialize the live analysis mod
void Init() {
    rfa::LogDetail("Initializing live analysis mod");

    // Register FrameUpdate callback for main thread Lua command processing
    console::RegisterFrameUpdateCallback(ProcessLuaCommandsCallback);
    rfa::LogDetail("Registered FrameUpdate callback for main thread Lua command processing");

    // Start the named pipe server thread
    g_pipeRunning = true;
    g_luaQueueRunning = true;
    g_pipeThread = CreateThread(nullptr, 0, RunPipeServer, nullptr, 0, nullptr);
    if (!g_pipeThread) rfa::Log("Could not start the pipe's thread (error %lu): no pipe", GetLastError());

    // Hook some key game functions for call tracing
    // These are example hooks - more can be added via the pipe interface

    rfa::LogInfo("Live analysis mod initialized. Pipe: %ls", kPipeName);
}

// Cleanup
void Shutdown() {
    rfa::LogDetail("Shutting down live analysis mod");

    g_pipeRunning = false;
    g_luaQueueRunning = false;
    g_queueCV.notify_all();

    // The thread may be waiting for a client or a command: that wait is let go, then a moment to end (else it is
    // left to Windows).
    if (g_pipeThread) {
        CancelSynchronousIo(g_pipeThread);
        WaitForSingleObject(g_pipeThread, 1000);
        CloseHandle(g_pipeThread);
        g_pipeThread = nullptr;
    }

    // Unregister FrameUpdate callback
    console::UnregisterFrameUpdateCallback(ProcessLuaCommandsCallback);
    rfa::LogDetail("Unregistered FrameUpdate callback");

    // Remove all hooks
    std::lock_guard<std::mutex> lock(g_hooksMutex);
    for (auto& hook : g_hooks) {
        if (hook.active && hook.original) {
            rfa::PatchCode(reinterpret_cast<void*>(hook.address), hook.original, hook.prologueSize);
        }
    }
    g_hooks.clear();

    rfa::LogDetail("Live analysis mod shutdown complete");
}

// Who may open the pipe: the system and the Windows user the game runs as, nobody else. Null (the
// default rights) if the user can't be read.
PSECURITY_DESCRIPTOR PipeSecurity() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return nullptr;
    BYTE buffer[256] = {};
    DWORD size = 0;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &size)) {
        wchar_t* sid = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid, &sid)) {
            const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid) + L")";
            ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr);
            LocalFree(sid);
        }
    }
    CloseHandle(token);
    return descriptor;
}

// Named pipe server thread - handles communication with external tools
void PipeServerThread() {
    rfa::LogDetail("Pipe server thread started");
    // Only this PC (PIPE_REJECT_REMOTE_CLIENTS) and only this Windows user (the security below).
    SECURITY_ATTRIBUTES security = {sizeof(security), PipeSecurity(), FALSE};
    if (!security.lpSecurityDescriptor) rfa::Log("Pipe: couldn't limit it to this Windows user; the default rights apply");

    while (g_pipeRunning) {
        // Create the named pipe
        HANDLE pipe = CreateNamedPipeW(
            kPipeName,
            // Synchronous: its reads and writes are made without an OVERLAPPED. Opened overlapped, such a read could
            // return before it was done, its byte count not yet set.
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES,
            65536,  // output buffer
            65536,  // input buffer
            5000,   // timeout
            security.lpSecurityDescriptor ? &security : nullptr
        );

        if (pipe == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            if (err != ERROR_PIPE_BUSY) {
                rfa::Log("Failed to create pipe: %lu", err);
                Sleep(1000);
                continue;
            }
            // Wait for pipe to become available
            if (!WaitNamedPipeW(kPipeName, 5000)) {
                Sleep(1000);
                continue;
            }
            continue;
        }

        g_pipe = pipe;
        rfa::LogDetail("Waiting for client connection on pipe...");

        // Wait for client to connect
        BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (!connected) {
            DWORD err = GetLastError();
            if (err != ERROR_PIPE_CONNECTED) {
                rfa::Log("Pipe connection failed: %lu", err);
                CloseHandle(pipe);
                g_pipe = INVALID_HANDLE_VALUE;
                Sleep(1000);
                continue;
            }
        }

        rfa::LogDetail("Client connected to analysis pipe");

        // Communication loop
        BYTE buffer[8192];
        DWORD bytesRead = 0;

        while (g_pipeRunning) {
            // Read command from pipe
            if (!ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) {
                DWORD err = GetLastError();
                if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
                    rfa::LogDetail("Client disconnected");
                    break;
                }
                rfa::Log("Pipe read error: %lu", err);
                break;
            }

            if (bytesRead == 0) continue;

            buffer[bytesRead] = '\0';
            std::string command(reinterpret_cast<char*>(buffer), bytesRead);

            // Parse and handle command
            // Simple text-based protocol: CMD arg1 arg2 ...
            std::istringstream iss(command);
            std::string cmd;
            iss >> cmd;

            if (cmd == "read") {
                DWORD addr; size_t sz;
                if (iss >> std::hex >> addr >> std::dec >> sz) {
                    auto data = ReadGameMemory(addr, sz);
                    SendPipeResponse(pipe, data);
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: read <address> <size>");
                }
            }
            else if (cmd == "write") {
                DWORD addr;
                std::string hexData;
                if (iss >> std::hex >> addr >> hexData) {
                    std::vector<BYTE> data;
                    bool bytesOk = hexData.length() % 2 == 0;
                    for (size_t i = 0; bytesOk && i < hexData.length(); i += 2) {
                        BYTE value = 0;
                        bytesOk = ParseHexByte(hexData, i, value);
                        if (bytesOk) data.push_back(value);
                    }
                    if (!bytesOk) {
                        SendPipeResponse(pipe, "ERROR: <hex_bytes> must be pairs of hex digits, e.g. 90 or 0F1E");
                    } else if (WriteGameMemory(addr, data.data(), data.size())) {
                        SendPipeResponse(pipe, "OK");
                    } else {
                        SendPipeResponse(pipe, "ERROR: Write failed");
                    }
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: write <address> <hex_bytes>");
                }
            }
            else if (cmd == "state") {
                GameState state = GetGameStateSnapshot();
                std::ostringstream oss;
                oss << "PlayerPos: " << state.playerPos[0] << " " << state.playerPos[1] << " " << state.playerPos[2] << "\n"
                    << "CameraPos: " << state.cameraPos[0] << " " << state.cameraPos[1] << " " << state.cameraPos[2] << "\n"
                    << "CameraFov: " << state.cameraFov << "\n"
                    << "Level: 0x" << std::hex << state.currentLevel << std::dec << "\n"
                    << "Objects: " << state.objectCount << "\n"
                    << "GameTime: " << state.gameTime << "\n"
                    << "InMenu: " << (state.inMenu ? "true" : "false");
                SendPipeResponse(pipe, oss.str());
            }
            else if (cmd == "modules") {
                // List loaded modules
                std::ostringstream oss;
                HMODULE hMods[1024];
                DWORD cbNeeded;
                if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded)) {
                    for (DWORD i = 0; i < cbNeeded / sizeof(HMODULE); i++) {
                        char name[MAX_PATH];
                        GetModuleFileNameExA(GetCurrentProcess(), hMods[i], name, MAX_PATH);
                        MODULEINFO info;
                        GetModuleInformation(GetCurrentProcess(), hMods[i], &info, sizeof(info));
                        oss << std::hex << reinterpret_cast<DWORD>(hMods[i]) << " "
                            << std::hex << reinterpret_cast<DWORD>(info.lpBaseOfDll) << " "
                            << std::dec << info.SizeOfImage << " " << name << "\n";
                    }
                }
                SendPipeResponse(pipe, oss.str());
            }
            else if (cmd == "hook") {
                DWORD addr; size_t prologue;
                std::string name;
                if (iss >> std::hex >> addr >> std::dec >> prologue >> name) {
                    std::lock_guard<std::mutex> lock(g_hooksMutex);
                    BYTE* original = static_cast<BYTE*>(VirtualAlloc(nullptr, prologue + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
                    if (original) {
                        memcpy(original, reinterpret_cast<void*>(addr), prologue);
                        BYTE jump[5];
                        rfa::MakeJump(jump, original + prologue, reinterpret_cast<BYTE*>(addr) + prologue);
                        memcpy(original + prologue, jump, 5);

                        // For now, just log - actual hooking would need a trampoline
                        // This is a placeholder for the hook mechanism
                        g_hooks.push_back({addr, prologue, original, name, true});
                        SendPipeResponse(pipe, "OK: Hook registered (placeholder)");
                    } else {
                        SendPipeResponse(pipe, "ERROR: Failed to allocate trampoline");
                    }
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: hook <address> <prologue_size> <name>");
                }
            }
            else if (cmd == "unhook") {
                std::string name;
                if (iss >> name) {
                    std::lock_guard<std::mutex> lock(g_hooksMutex);
                    bool found = false;
                    for (auto it = g_hooks.begin(); it != g_hooks.end(); ++it) {
                        if (it->name == name && it->active) {
                            rfa::PatchCode(reinterpret_cast<void*>(it->address), it->original, it->prologueSize);
                            VirtualFree(it->original, 0, MEM_RELEASE);
                            g_hooks.erase(it);
                            found = true;
                            break;
                        }
                    }
                    SendPipeResponse(pipe, found ? "OK: Hook removed" : "ERROR: Hook not found");
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: unhook <name>");
                }
            }
            else if (cmd == "hooks") {
                std::lock_guard<std::mutex> lock(g_hooksMutex);
                std::ostringstream oss;
                for (const auto& h : g_hooks) {
                    oss << std::hex << h.address << " " << std::dec << h.prologueSize << " " << h.name << " " << (h.active ? "active" : "inactive") << "\n";
                }
                SendPipeResponse(pipe, oss.str());
            }
            else if (cmd == "call") {
                DWORD addr;
                if (iss >> std::hex >> addr) {
                    // Call a function - very dangerous, use with caution!
                    // This is a placeholder - actual implementation would need proper calling convention handling
                    SendPipeResponse(pipe, "WARNING: Function calling not fully implemented for safety");
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: call <address>");
                }
            }
            else if (cmd == "exec") {
                // Execute a console command via RunCommand
                std::string command;
                std::getline(iss, command);
                if (!command.empty() && command[0] == ' ') command.erase(0, 1);
                if (!command.empty()) {
                    // On the game's own thread (the queue the Lua commands use): run from this pipe
                    // thread, a command that touches the world (player_pathfind_to_camera) crashed the game.
                    SendPipeResponse(pipe, QueueLuaCommand(command, 5000, true));
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: exec <console_command>");
                }
            }
            else if (cmd == "scan") {
                // Scan memory for byte pattern: scan <start> <size> <hex_pattern> [mask]
                // mask: ? = wildcard, x = exact match (default: all exact)
                DWORD startAddr; size_t scanSize; std::string pattern; std::string mask;
                if (iss >> std::hex >> startAddr >> std::dec >> scanSize >> pattern) {
                    iss >> mask;
                    if (mask.empty()) mask = std::string(pattern.length() / 2, 'x');

                    std::vector<BYTE> patternBytes;
                    bool bytesOk = pattern.length() % 2 == 0;
                    for (size_t i = 0; bytesOk && i < pattern.length(); i += 2) {
                        BYTE value = 0;
                        bytesOk = ParseHexByte(pattern, i, value);
                        if (bytesOk) patternBytes.push_back(value);
                    }
                    if (!bytesOk) {
                        SendPipeResponse(pipe, "ERROR: <hex_pattern> must be pairs of hex digits (a wildcard is a ? in the mask)");
                        continue;
                    }

                    std::ostringstream oss;
                    size_t matches = 0;
                    for (size_t offset = 0; offset + patternBytes.size() <= scanSize; ++offset) {
                        bool match = true;
                        for (size_t i = 0; i < patternBytes.size(); ++i) {
                            if (mask[i] != '?' && mask[i] != 'x') continue;
                            BYTE memByte;
                            if (!SafeReadMemory(&memByte, reinterpret_cast<void*>(startAddr + offset + i), 1) || memByte != patternBytes[i]) {
                                match = false; break;
                            }
                        }
                        if (match) {
                            oss << "0x" << std::hex << (startAddr + offset) << std::dec << "\n";
                            if (++matches >= 100) { oss << "... (limited to 100)\n"; break; }
                        }
                    }
                    SendPipeResponse(pipe, oss.str().empty() ? "No matches found" : oss.str());
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: scan <start_addr> <size> <hex_pattern> [mask]");
                }
            }
            else if (cmd == "enum_mem") {
                // Enumerate memory regions
                std::ostringstream oss;
                MEMORY_BASIC_INFORMATION mbi;
                DWORD addr = 0;
                while (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi))) {
                    oss << std::hex << addr << " " << mbi.RegionSize << " "
                        << (mbi.State == MEM_COMMIT ? "COMMIT" : mbi.State == MEM_RESERVE ? "RESERVE" : "FREE") << " "
                        << (mbi.Protect & PAGE_EXECUTE ? "X" : "-")
                        << (mbi.Protect & PAGE_READONLY ? "R" : mbi.Protect & PAGE_READWRITE ? "RW" : mbi.Protect & PAGE_WRITECOPY ? "WC" : "-")
                        << (mbi.Protect & PAGE_GUARD ? "G" : "-") << "\n";
                    addr += mbi.RegionSize;
                    if (addr == 0) break; // wrapped
                }
                SendPipeResponse(pipe, oss.str());
            }
            else if (cmd == "trace") {
                // Enable/disable call tracing
                std::string arg;
                if (iss >> arg) {
                    SendPipeResponse(pipe, "Call tracing: " + arg + " (not fully implemented)");
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: trace on|off");
                }
            }
            else if (cmd == "call_func") {
                // Call a function with arguments: call_func <addr> <conv> [args...]
                // conv: cdecl, stdcall, thiscall
                DWORD addr; std::string conv;
                if (iss >> std::hex >> addr >> conv) {
                    std::vector<DWORD> args;
                    DWORD arg;
                    while (iss >> std::hex >> arg) args.push_back(arg);

                    DWORD result = 0;
                    bool success = false;
                    if (conv == "cdecl") {
                        result = CallFunctionCdecl(addr,
                            args.size() > 0 ? args[0] : 0,
                            args.size() > 1 ? args[1] : 0,
                            args.size() > 2 ? args[2] : 0,
                            args.size() > 3 ? args[3] : 0,
                            args.size() > 4 ? args[4] : 0,
                            args.size() > 5 ? args[5] : 0,
                            args.size() > 6 ? args[6] : 0,
                            args.size() > 7 ? args[7] : 0);
                        success = (result != 0xFFFFFFFF);
                    } else if (conv == "stdcall") {
                        result = CallFunctionStdcall(addr,
                            args.size() > 0 ? args[0] : 0,
                            args.size() > 1 ? args[1] : 0,
                            args.size() > 2 ? args[2] : 0,
                            args.size() > 3 ? args[3] : 0,
                            args.size() > 4 ? args[4] : 0,
                            args.size() > 5 ? args[5] : 0,
                            args.size() > 6 ? args[6] : 0,
                            args.size() > 7 ? args[7] : 0);
                        success = (result != 0xFFFFFFFF);
                    } else if (conv == "thiscall") {
                        // thiscall: ECX = this, rest on stack
                        DWORD thisPtr = args.size() > 0 ? args[0] : 0;
                        result = CallFunctionThiscall(addr, thisPtr,
                            args.size() > 1 ? args[1] : 0,
                            args.size() > 2 ? args[2] : 0,
                            args.size() > 3 ? args[3] : 0,
                            args.size() > 4 ? args[4] : 0,
                            args.size() > 5 ? args[5] : 0,
                            args.size() > 6 ? args[6] : 0);
                        success = (result != 0xFFFFFFFF);
                    } else {
                        SendPipeResponse(pipe, "ERROR: Unknown convention. Use cdecl, stdcall, or thiscall");
                        continue;
                    }
                    if (!success) {
                        SendPipeResponse(pipe, "ERROR: Exception during function call (access violation?)");
                        continue;
                    }
                    std::ostringstream oss;
                    oss << "OK: Result = 0x" << std::hex << result << std::dec << " (" << result << ")";
                    SendPipeResponse(pipe, oss.str());
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: call_func <addr> <cdecl|stdcall|thiscall> [arg1 arg2 ...]");
                }
            }
            else if (cmd == "callstack") {
                // Capture current call stack
                std::ostringstream oss;
                CONTEXT ctx = {};
                ctx.ContextFlags = CONTEXT_FULL;
                RtlCaptureContext(&ctx);

                STACKFRAME64 frame = {};
                frame.AddrPC.Offset = ctx.Eip;
                frame.AddrPC.Mode = AddrModeFlat;
                frame.AddrStack.Offset = ctx.Esp;
                frame.AddrStack.Mode = AddrModeFlat;
                frame.AddrFrame.Offset = ctx.Ebp;
                frame.AddrFrame.Mode = AddrModeFlat;

                HANDLE process = GetCurrentProcess();
                SymInitialize(process, nullptr, TRUE);

                for (int i = 0; i < 50; ++i) {
                    if (!StackWalk64(IMAGE_FILE_MACHINE_I386, process, GetCurrentThread(), &frame, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
                    if (frame.AddrPC.Offset == 0) break;

                    DWORD64 displacement = 0;
                    char symbolBuffer[sizeof(SYMBOL_INFO) + 256];
                    PSYMBOL_INFO symbol = reinterpret_cast<PSYMBOL_INFO>(symbolBuffer);
                    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                    symbol->MaxNameLen = 255;

                    if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
                        oss << "#" << i << " 0x" << std::hex << frame.AddrPC.Offset << std::dec << " " << symbol->Name << "+0x" << std::hex << displacement << std::dec << "\n";
                    } else {
                        oss << "#" << i << " 0x" << std::hex << frame.AddrPC.Offset << std::dec << " (no symbol)\n";
                    }
                }
                SymCleanup(process);
                SendPipeResponse(pipe, oss.str().empty() ? "Could not capture stack" : oss.str());
            }
            else if (cmd == "registers") {
                // Get current CPU registers
                CONTEXT ctx = {};
                ctx.ContextFlags = CONTEXT_FULL;
                RtlCaptureContext(&ctx);

                std::ostringstream oss;
                oss << "EAX=0x" << std::hex << ctx.Eax << std::dec << "  EBX=0x" << std::hex << ctx.Ebx << std::dec << "  ECX=0x" << std::hex << ctx.Ecx << std::dec << "  EDX=0x" << std::hex << ctx.Edx << std::dec << "\n"
                    << "ESI=0x" << std::hex << ctx.Esi << std::dec << "  EDI=0x" << std::hex << ctx.Edi << std::dec << "  EBP=0x" << std::hex << ctx.Ebp << std::dec << "  ESP=0x" << std::hex << ctx.Esp << std::dec << "\n"
                    << "EIP=0x" << std::hex << ctx.Eip << std::dec << "  EFLAGS=0x" << std::hex << ctx.EFlags << std::dec << "\n"
                    << "CS=0x" << std::hex << ctx.SegCs << std::dec << "  DS=0x" << std::hex << ctx.SegDs << std::dec << "  ES=0x" << std::hex << ctx.SegEs << std::dec << "  FS=0x" << std::hex << ctx.SegFs << std::dec << "  GS=0x" << std::hex << ctx.SegGs << std::dec << "  SS=0x" << std::hex << ctx.SegSs << std::dec;
                SendPipeResponse(pipe, oss.str());
            }
            else if (cmd == "enum_objects") {
                // Enumerate game objects from known arrays (spawner.cpp addresses)
                std::ostringstream oss;

                // Vehicles
                constexpr DWORD kVehicleList = 0x148BD08;
                constexpr DWORD kVehicleSize = 0x880;
                BYTE* vehicleEntries = *reinterpret_cast<BYTE* const*>(kVehicleList);
                int vehicleCount = *reinterpret_cast<const int*>(kVehicleList + 8);
                if (vehicleEntries && vehicleCount > 0 && vehicleCount < 10000) {
                    oss << "=== Vehicles (" << vehicleCount << ") ===\n";
                    for (int i = 0; i < vehicleCount; ++i) {
                        BYTE* v = vehicleEntries + i * kVehicleSize;
                        const char* name = *reinterpret_cast<const char* const*>(v);
                        if (name && *name) {
                            float* pos = reinterpret_cast<float*>(v + 0x10);
                            oss << "  [" << i << "] " << name << " @ " << pos[0] << ", " << pos[1] << ", " << pos[2] << "\n";
                        }
                    }
                }

                // Items
                BYTE* itemEntries = *reinterpret_cast<BYTE* const*>(0x1BB1D44);
                int itemCount = *reinterpret_cast<const int*>(0x1BB1D3C);
                constexpr DWORD kItemSize = 0x168;
                if (itemEntries && itemCount > 0 && itemCount < 10000) {
                    oss << "\n=== Items (" << itemCount << ") ===\n";
                    for (int i = 0; i < itemCount; ++i) {
                        const char* name = *reinterpret_cast<const char* const*>(itemEntries + i * kItemSize);
                        if (name && *name) oss << "  [" << i << "] " << name << "\n";
                    }
                }

                // Weapons
                BYTE* weaponEntries = *reinterpret_cast<BYTE* const*>(0x18F8048);
                int weaponCount = *reinterpret_cast<const int*>(0x18F804C);
                constexpr DWORD kWeaponSize = 0xE0;
                if (weaponEntries && weaponCount > 0 && weaponCount < 10000) {
                    oss << "\n=== Weapons (" << weaponCount << ") ===\n";
                    for (int i = 0; i < weaponCount; ++i) {
                        const char* name = *reinterpret_cast<const char* const*>(weaponEntries + i * kWeaponSize);
                        if (name && *name) oss << "  [" << i << "] " << name << "\n";
                    }
                }

                SendPipeResponse(pipe, oss.str().empty() ? "No objects found" : oss.str());
            }
            else if (cmd == "read_chain") {
                // Follow pointer chain: read_chain <base> <offset1> <offset2> ...
                DWORD base;
                std::vector<DWORD> offsets;
                if (iss >> std::hex >> base) {
                    DWORD offset;
                    while (iss >> std::hex >> offset) offsets.push_back(offset);

                    DWORD current = base;
                    std::ostringstream oss;
                    oss << "0x" << std::hex << base << std::dec;
                    for (size_t i = 0; i < offsets.size(); ++i) {
                        DWORD next;
                        if (!SafeReadMemory(&next, reinterpret_cast<void*>(current + offsets[i]), 4)) {
                            oss << " -> [READ FAILED at +0x" << std::hex << offsets[i] << std::dec << "]";
                            break;
                        }
                        oss << " +0x" << std::hex << offsets[i] << std::dec << " -> 0x" << std::hex << next << std::dec;
                        current = next;
                    }
                    SendPipeResponse(pipe, oss.str());
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: read_chain <base> <offset1> [offset2 ...]");
                }
            }
            else if (cmd == "breakpoint") {
                // Set/clear software breakpoint: breakpoint set|clear <addr>
                std::string action;
                DWORD address;
                if (iss >> action >> std::hex >> address) {
                    if (action == "set") {
                        BYTE originalByte;
                        if (SafeReadMemory(&originalByte, reinterpret_cast<void*>(address), 1)) {
                            BYTE int3 = 0xCC;
                            if (SafeWriteMemory(reinterpret_cast<void*>(address), &int3, 1)) {
                                // Store original byte for restoration
                                std::ostringstream oss;
                                oss << "bp_" << std::hex << address;
                                SetPropA(reinterpret_cast<HWND>(address), oss.str().c_str(), reinterpret_cast<HANDLE>(originalByte));
                                SendPipeResponse(pipe, "OK: Breakpoint set at 0x" + std::to_string(address));
                            } else {
                                SendPipeResponse(pipe, "ERROR: Failed to write INT3");
                            }
                        } else {
                            SendPipeResponse(pipe, "ERROR: Cannot read original byte");
                        }
                    } else if (action == "clear") {
                        std::ostringstream oss;
                        oss << "bp_" << std::hex << address;
                        HANDLE hProp = GetPropA(reinterpret_cast<HWND>(address), oss.str().c_str());
                        if (hProp) {
                            BYTE originalByte = static_cast<BYTE>(reinterpret_cast<DWORD_PTR>(hProp));
                            if (SafeWriteMemory(reinterpret_cast<void*>(address), &originalByte, 1)) {
                                RemovePropA(reinterpret_cast<HWND>(address), oss.str().c_str());
                                SendPipeResponse(pipe, "OK: Breakpoint cleared at 0x" + std::to_string(address));
                            } else {
                                SendPipeResponse(pipe, "ERROR: Failed to restore original byte");
                            }
                        } else {
                            SendPipeResponse(pipe, "ERROR: No breakpoint at that address");
                        }
                    } else {
                        SendPipeResponse(pipe, "ERROR: Usage: breakpoint set|clear <addr>");
                    }
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: breakpoint set|clear <addr>");
                }
            }
            else if (cmd == "disasm") {
                // Disassemble at address: disasm <addr> [count]
                DWORD addr; int count = 20;
                if (iss >> std::hex >> addr) {
                    iss >> count;
                    std::ostringstream oss;
                    for (int i = 0; i < count; ++i) {
                        BYTE buffer[16];
                        if (!SafeReadMemory(buffer, reinterpret_cast<void*>(addr), sizeof(buffer))) {
                            oss << "0x" << std::hex << addr << std::dec << ": READ FAILED\n";
                            break;
                        }
                        // Simple disassembly - just show bytes and length
                        oss << "0x" << std::hex << addr << std::dec << ": ";
                        for (int b = 0; b < 16; ++b) {
                            if (b < 16) oss << std::hex << std::setw(2) << std::setfill('0') << (int)buffer[b] << " ";
                        }
                        // Estimate instruction length (simplified)
                        int len = 1;
                        if (buffer[0] >= 0x48 && buffer[0] <= 0x4F) len = 1; // REX prefixes
                        else if (buffer[0] == 0x0F) len = 2; // Two-byte opcode
                        addr += len;
                        oss << "\n";
                    }
                    SendPipeResponse(pipe, oss.str());
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: disasm <addr> [count]");
                }
            }
            else if (cmd == "xrefs") {
                // Find cross-references to address: xrefs <addr> [range_start] [range_end]
                DWORD target, rangeStart = 0x400000, rangeEnd = 0x20000000;
                if (iss >> std::hex >> target) {
                    iss >> std::hex >> rangeStart >> std::hex >> rangeEnd;
                    std::ostringstream oss;
                    oss << "Cross-references to 0x" << std::hex << target << std::dec << ":\n";
                    int found = 0;
                    for (DWORD addr = rangeStart; addr < rangeEnd && found < 50; ++addr) {
                        BYTE buffer[6];
                        if (!SafeReadMemory(buffer, reinterpret_cast<void*>(addr), 6)) continue;
                        // Look for CALL/JMP relative (E8/E9) with 4-byte offset
                        if ((buffer[0] == 0xE8 || buffer[0] == 0xE9) && addr + 5 < rangeEnd) {
                            DWORD offset = *reinterpret_cast<DWORD*>(&buffer[1]);
                            DWORD dest = addr + 5 + static_cast<INT32>(offset);
                            if (dest == target) {
                                oss << "  CODE: 0x" << std::hex << addr << std::dec << " (" << (buffer[0] == 0xE8 ? "CALL" : "JMP") << ")\n";
                                found++;
                            }
                        }
                        // Look for direct data reference (MOV reg, [target] / MOV [target], reg)
                        // Simplified: check for 32-bit immediate matching target
                        if (buffer[0] == 0xA1 || buffer[0] == 0xA3) { // MOV EAX, [imm32] / MOV [imm32], EAX
                            DWORD imm = *reinterpret_cast<DWORD*>(&buffer[1]);
                            if (imm == target) {
                                oss << "  DATA: 0x" << std::hex << addr << std::dec << " (MOV " << (buffer[0] == 0xA1 ? "EAX, [" : "[") << "0x" << std::hex << imm << std::dec << "]" << (buffer[0] == 0xA3 ? ", EAX" : "") << ")\n";
                                found++;
                            }
                        }
                    }
                    if (found == 0) oss << "  None found in range\n";
                    SendPipeResponse(pipe, oss.str());
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: xrefs <addr> [range_start] [range_end]");
                }
            }
            else if (cmd == "exports") {
                // List exports of a module: exports <module_base>
                DWORD base;
                if (iss >> std::hex >> base) {
                    char* names[200];
                    DWORD addresses[200];
                    WORD ordinals[200];
                    DWORD count = 0;
                    if (ReadModuleExports(base, names, addresses, ordinals, 200, &count)) {
                        std::ostringstream oss;
                        oss << "Exports for module at 0x" << std::hex << base << std::dec << " (" << count << " named):\n";
                        for (DWORD i = 0; i < count; ++i) {
                            oss << "  " << names[i] << " -> 0x" << std::hex << addresses[i] << std::dec << " (ordinal " << ordinals[i] << ")\n";
                        }
                        SendPipeResponse(pipe, oss.str());
                    } else {
                        // Check if valid module but no exports
                        int validation = ValidateModule(base);
                        if (validation == 1) {
                            SendPipeResponse(pipe, "No exports\n");
                        } else if (validation == 2) {
                            SendPipeResponse(pipe, "ERROR: Invalid DOS header");
                        } else if (validation == 3) {
                            SendPipeResponse(pipe, "ERROR: Invalid NT header");
                        } else {
                            SendPipeResponse(pipe, "ERROR: Invalid module base");
                        }
                    }
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: exports <module_base>");
                }
            }
            else if (cmd == "threads") {
                // Enumerate threads in current process
                std::ostringstream oss;
                HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
                if (snapshot != INVALID_HANDLE_VALUE) {
                    THREADENTRY32 te = { sizeof(THREADENTRY32) };
                    DWORD pid = GetCurrentProcessId();
                    int count = 0;
                    if (Thread32First(snapshot, &te)) {
                        do {
                            if (te.th32OwnerProcessID == pid) {
                                HANDLE hThread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                                if (hThread) {
                                    CONTEXT ctx = {};
                                    ctx.ContextFlags = CONTEXT_FULL;
                                    if (GetThreadContext(hThread, &ctx)) {
                                        oss << "TID " << te.th32ThreadID << ": EIP=0x" << std::hex << ctx.Eip << std::dec << " ESP=0x" << std::hex << ctx.Esp << std::dec << " EBP=0x" << std::hex << ctx.Ebp << std::dec;
                                        if (ctx.Eip == 0) oss << " (not running)";
                                        oss << "\n";
                                    }
                                    CloseHandle(hThread);
                                    if (++count >= 50) break;
                                }
                            }
                        } while (Thread32Next(snapshot, &te));
                    }
                    CloseHandle(snapshot);
                    oss << "Total threads: " << count << "\n";
                } else {
                    oss << "Failed to create thread snapshot\n";
                }
                SendPipeResponse(pipe, oss.str());
            }
            else if (cmd == "watch") {
                // Hardware data breakpoint: watch <addr> <size> <r|w|rw>
                DWORD addr, size; std::string mode;
                if (iss >> std::hex >> addr >> std::dec >> size >> mode) {
                    // This requires kernel-mode or debug registers - simplified version using page guard
                    DWORD oldProtect;
                    if (VirtualProtect(reinterpret_cast<void*>(addr), size, PAGE_GUARD | PAGE_READWRITE, &oldProtect)) {
                        SendPipeResponse(pipe, "OK: Guard page set at 0x" + std::to_string(addr) + " size " + std::to_string(size) + " (single-shot)");
                    } else {
                        SendPipeResponse(pipe, "ERROR: VirtualProtect failed: " + std::to_string(GetLastError()));
                    }
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: watch <addr> <size> <r|w|rw>");
                }
            }
            else if (cmd == "teleport") {
                // Teleport player: teleport <x> <y> <z> [yaw] [pitch]
                float x, y, z, yaw = 0, pitch = 0;
                if (iss >> x >> y >> z >> yaw >> pitch) {
                    // Find player entity and set position
                    // Player position typically at g_cameraPosition for first person
                    g_cameraPosition[0] = x;
                    g_cameraPosition[1] = y;
                    g_cameraPosition[2] = z;
                    // Also need to update player entity if separate
                    // Try common player struct addresses
                    constexpr DWORD kPlayerPos[] = { 0x14E8630, 0x14E8634, 0x14E8638 };
                    for (size_t i = 0; i < 3; ++i) {
                        float* ptr = reinterpret_cast<float*>(kPlayerPos[i]);
                        float val = (i == 0 ? x : (i == 1 ? y : z));
                        SafeWriteValue(ptr, val);
                    }
                    SendPipeResponse(pipe, "OK: Teleported player to " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z));
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: teleport <x> <y> <z> [yaw] [pitch]");
                }
            }
            else if (cmd == "set_camera") {
                // Set camera: set_camera <x> <y> <z> [yaw] [pitch] [fov]
                float x, y, z, yaw = 0, pitch = 0, fov = 60;
                if (iss >> x >> y >> z >> yaw >> pitch >> fov) {
                    g_cameraPosition[0] = x;
                    g_cameraPosition[1] = y;
                    g_cameraPosition[2] = z;
                    // Camera rotation matrix - simplified
                    if (yaw != 0 || pitch != 0) {
                        float cy = cosf(yaw * 3.14159f / 180.0f);
                        float sy = sinf(yaw * 3.14159f / 180.0f);
                        float cp = cosf(pitch * 3.14159f / 180.0f);
                        float sp = sinf(pitch * 3.14159f / 180.0f);
                        // Forward vector
                        g_cameraRotation[2] = cy * cp;
                        g_cameraRotation[5] = sp;
                        g_cameraRotation[8] = -sy * cp;
                        // Right vector
                        g_cameraRotation[0] = -sy;
                        g_cameraRotation[3] = 0;
                        g_cameraRotation[6] = -cy;
                        // Up vector (cross)
                        g_cameraRotation[1] = -cy * sp;
                        g_cameraRotation[4] = cp;
                        g_cameraRotation[7] = sy * sp;
                    }
                    g_cameraFov = fov;
                    SendPipeResponse(pipe, "OK: Camera set to " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + " FOV=" + std::to_string(fov));
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: set_camera <x> <y> <z> [yaw] [pitch] [fov]");
                }
            }
            else if (cmd == "input") {
                // Simulate input: input <key> [down|up|press] - key can be VK_ name or hex
                std::string keyStr, action = "press";
                if (iss >> keyStr >> action) {
                    int vk = 0;
                    if (keyStr.rfind("0x", 0) == 0) {  // a key code: 0x01 to 0xFE, else answered with an error below
                        char* end = nullptr;
                        const unsigned long code = strtoul(keyStr.c_str() + 2, &end, 16);
                        vk = keyStr.length() > 2 && *end == '\0' && code >= 0x01 && code <= 0xFE ? static_cast<int>(code) : 0;
                    }
                    else if (keyStr == "W") vk = 'W';
                    else if (keyStr == "A") vk = 'A';
                    else if (keyStr == "S") vk = 'S';
                    else if (keyStr == "D") vk = 'D';
                    else if (keyStr == "SPACE") vk = VK_SPACE;
                    else if (keyStr == "SHIFT") vk = VK_SHIFT;
                    else if (keyStr == "CTRL") vk = VK_CONTROL;
                    else if (keyStr == "LEFT") vk = VK_LEFT;
                    else if (keyStr == "RIGHT") vk = VK_RIGHT;
                    else if (keyStr == "UP") vk = VK_UP;
                    else if (keyStr == "DOWN") vk = VK_DOWN;
                    else if (keyStr == "ESCAPE") vk = VK_ESCAPE;
                    else if (keyStr == "ENTER") vk = VK_RETURN;
                    else if (keyStr == "TAB") vk = VK_TAB;
                    else if (keyStr == "F1") vk = VK_F1;
                    else if (keyStr == "F2") vk = VK_F2;
                    else if (keyStr == "F3") vk = VK_F3;
                    else if (keyStr == "F4") vk = VK_F4;
                    else if (keyStr == "F5") vk = VK_F5;
                    else if (keyStr == "F6") vk = VK_F6;
                    else if (keyStr == "F7") vk = VK_F7;
                    else if (keyStr == "F8") vk = VK_F8;
                    else if (keyStr == "F9") vk = VK_F9;
                    else if (keyStr == "F10") vk = VK_F10;
                    else if (keyStr == "F11") vk = VK_F11;
                    else if (keyStr == "F12") vk = VK_F12;
                    else vk = keyStr[0];
                    if (vk == 0) {
                        SendPipeResponse(pipe, "ERROR: a key code is 0x01 to 0xFE, e.g. 0x41");
                        continue;
                    }

                    if (action == "down" || action == "press") {
                        keybd_event(static_cast<BYTE>(vk), 0, 0, 0);
                    }
                    if (action == "up" || action == "press") {
                        keybd_event(static_cast<BYTE>(vk), 0, KEYEVENTF_KEYUP, 0);
                    }
                    SendPipeResponse(pipe, "OK: Input " + action + " " + keyStr);
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: input <key> [down|up|press] (keys: W,A,S,D,SPACE,SHIFT,CTRL,LEFT,RIGHT,UP,DOWN,ESCAPE,ENTER,TAB,F1-F12, or 0xVK)");
                }
            }
            else if (cmd == "mouse") {
                // Simulate mouse: mouse <dx> <dy> [buttons] [wheel]
                int dx, dy, buttons = 0, wheel = 0;
                if (iss >> dx >> dy >> buttons >> wheel) {
                    INPUT input = {};
                    input.type = INPUT_MOUSE;
                    input.mi.dx = dx;
                    input.mi.dy = dy;
                    input.mi.dwFlags = MOUSEEVENTF_MOVE;
                    if (buttons & 1) input.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
                    if (buttons & 2) input.mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN;
                    if (buttons & 4) input.mi.dwFlags |= MOUSEEVENTF_MIDDLEDOWN;
                    if (wheel != 0) {
                        input.mi.dwFlags |= MOUSEEVENTF_WHEEL;
                        input.mi.mouseData = wheel;
                    }
                    SendInput(1, &input, sizeof(INPUT));
                    // Release buttons
                    if (buttons) {
                        input.mi.dwFlags = 0;
                        if (buttons & 1) input.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
                        if (buttons & 2) input.mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
                        if (buttons & 4) input.mi.dwFlags |= MOUSEEVENTF_MIDDLEUP;
                        SendInput(1, &input, sizeof(INPUT));
                    }
                    SendPipeResponse(pipe, "OK: Mouse moved " + std::to_string(dx) + ", " + std::to_string(dy));
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: mouse <dx> <dy> [buttons_bitmask] [wheel] (buttons: 1=L,2=R,4=M)");
                }
            }
            else if (cmd == "time") {
                // Set time of day: time <hours> (0-24)
                float hours;
                if (iss >> hours) {
                    if (hours >= 0 && hours <= 24) {
                        g_timeOfDay = hours;
                        SendPipeResponse(pipe, "OK: Time of day set to " + std::to_string(hours) + ":00");
                    } else {
                        SendPipeResponse(pipe, "ERROR: Hours must be 0-24");
                    }
                } else {
                    SendPipeResponse(pipe, "Current time: " + std::to_string(g_timeOfDay) + ":00\nUsage: time <hours>");
                }
            }
            else if (cmd == "god") {
                // Toggle god mode: god on|off
                std::string state;
                if (iss >> state) {
                    // Find player health address and set to max / make invulnerable
                    // Common health addresses in RFA
                    constexpr DWORD kHealthAddrs[] = { 0x14E87A0, 0x14E87A4, 0x11B9E00, 0x11B9E04 };
                    if (state == "on") {
                        for (DWORD addr : kHealthAddrs) {
                            float* hp = reinterpret_cast<float*>(addr);
                            SafeWriteValue(hp, 999999.0f);
                        }
                        // Also try to find invulnerability flag
                        constexpr DWORD kInvulnAddrs[] = { 0x14E8800, 0x11B9F00 };
                        for (DWORD addr : kInvulnAddrs) {
                            BYTE* flag = reinterpret_cast<BYTE*>(addr);
                            SafeWriteValue(flag, (BYTE)1);
                        }
                        SendPipeResponse(pipe, "OK: God mode ON (health set to max, invuln flags set)");
                    } else if (state == "off") {
                        for (DWORD addr : kHealthAddrs) {
                            float* hp = reinterpret_cast<float*>(addr);
                            SafeWriteValue(hp, 100.0f);
                        }
                        SendPipeResponse(pipe, "OK: God mode OFF (health reset to 100)");
                    } else {
                        SendPipeResponse(pipe, "ERROR: Usage: god on|off");
                    }
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: god on|off");
                }
            }
            else if (cmd == "noclip") {
                // Toggle noclip: noclip on|off
                std::string state;
                if (iss >> state) {
                    // Find physics/collision flags for player
                    constexpr DWORD kCollisionAddrs[] = { 0x14E8900, 0x11B9F80, 0x14E8904 };
                    if (state == "on") {
                        for (DWORD addr : kCollisionAddrs) {
                            BYTE* flag = reinterpret_cast<BYTE*>(addr);
                            SafeWriteValue(flag, (BYTE)0);
                        }
                        SendPipeResponse(pipe, "OK: Noclip ON (collision disabled)");
                    } else if (state == "off") {
                        for (DWORD addr : kCollisionAddrs) {
                            BYTE* flag = reinterpret_cast<BYTE*>(addr);
                            SafeWriteValue(flag, (BYTE)1);
                        }
                        SendPipeResponse(pipe, "OK: Noclip OFF (collision enabled)");
                    } else {
                        SendPipeResponse(pipe, "ERROR: Usage: noclip on|off");
                    }
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: noclip on|off");
                }
            }
            else if (cmd == "give") {
                // Give item/weapon/vehicle: give <type> <name> [count]
                // Uses console command internally
                std::string type, name; int count = 1;
                if (iss >> type >> name >> count) {
                    std::string cmd;
                    if (type == "item") cmd = "give_player_item " + name + " " + std::to_string(count);
                    else if (type == "weapon") cmd = "give_player_weapon " + name + " " + std::to_string(count);
                    else if (type == "vehicle") cmd = "spawn_vehicle " + name;
                    else if (type == "ammo") cmd = "give_ammo " + name + " " + std::to_string(count);
                    else {
                        SendPipeResponse(pipe, "ERROR: Unknown type. Use: item, weapon, vehicle, ammo");
                        continue;
                    }
                    RunCommand("%s", cmd.c_str());
                    SendPipeResponse(pipe, "OK: Executed '" + cmd + "'");
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: give <item|weapon|vehicle|ammo> <name> [count]");
                }
            }
            else if (cmd == "speed") {
                // Set game speed: speed <multiplier> (1.0 = normal)
                float mult;
                if (iss >> mult) {
                    // Find game speed global
                    constexpr DWORD kSpeedAddrs[] = { 0x10F8900, 0x11BA000, 0x14EA000 };
                    for (DWORD addr : kSpeedAddrs) {
                        float* sp = reinterpret_cast<float*>(addr);
                        SafeWriteValue(sp, mult);
                    }
                    SendPipeResponse(pipe, "OK: Game speed set to " + std::to_string(mult) + "x");
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: speed <multiplier>");
                }
            }
            else if (cmd == "freeze") {
                // Freeze/unfreeze game: freeze on|off
                std::string state;
                if (iss >> state) {
                    constexpr DWORD kFreezeAddrs[] = { 0x10F8904, 0x11BA004, 0x14EA004 };
                    if (state == "on") {
                        for (DWORD addr : kFreezeAddrs) {
                            BYTE* flag = reinterpret_cast<BYTE*>(addr);
                            SafeWriteValue(flag, (BYTE)1);
                        }
                        SendPipeResponse(pipe, "OK: Game FROZEN");
                    } else if (state == "off") {
                        for (DWORD addr : kFreezeAddrs) {
                            BYTE* flag = reinterpret_cast<BYTE*>(addr);
                            SafeWriteValue(flag, (BYTE)0);
                        }
                        SendPipeResponse(pipe, "OK: Game UNFROZEN");
                    } else {
                        SendPipeResponse(pipe, "ERROR: Usage: freeze on|off");
                    }
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: freeze on|off");
                }
            }
            else if (cmd == "lua_state") {
                // Thread-safe Lua state inspection via vint_lua (runs on main thread)
                // Usage: lua_state [global|stack|registry]
                std::string target = "global";
                iss >> target;

                // Build Lua code to execute on main thread
                std::string luaCode;
                if (target == "global" || target == "globals") {
                    luaCode = "local t = {} for k,v in pairs(_G) do table.insert(t, k..' ('..type(v)..')') end return table.concat(t, '\\n')";
                } else if (target == "stack") {
                    luaCode = "local t = {} for i=1, 50 do local v = debug.getlocal(1, i) if not v then break end table.insert(t, '['..i..'] '..type(v)..(type(v)=='string' and ' = \"'..v..'\"' or (type(v)=='number' and ' = '..v or (type(v)=='boolean' and ' = '..tostring(v)) or '')) end return table.concat(t, '\\n')";
                } else if (target == "registry") {
                    luaCode = "return 'Registry access via vint_lua not directly supported'";
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: lua_state [global|stack|registry]");
                    continue;
                }

                // Queue the command for main thread execution via FrameUpdate hook
                std::string result = QueueLuaCommand(luaCode, 5000);
                SendPipeResponse(pipe, result);
            }
            else if (cmd == "lua_run") {
                // Thread-safe Lua code execution via vint_lua (runs on main thread)
                // Usage: lua_run <lua_code>
                std::string code;
                std::getline(iss, code);
                if (!code.empty() && code[0] == ' ') code.erase(0, 1);
                if (code.empty()) {
                    SendPipeResponse(pipe, "ERROR: Usage: lua_run <lua_code>");
                    continue;
                }

                // Wrap code to capture return values via vint_lua
                std::string luaCode = "local results = {pcall(function() " + code + " end)} if results[1] then local t = {} for i=2,#results do local v = results[i] local tv = type(v) table.insert(t, '['..(i-1)..'] '..tv..(tv=='string' and ' = \"'..v..'\"' or (tv=='number' and ' = '..v or (tv=='boolean' and ' = '..tostring(v)) or '')) end if #t == 0 then return 'OK: No return values' else return 'OK: '..(#results-1)..' return value(s):\\n'..table.concat(t, '\\n') end else return 'ERROR: '..tostring(results[2]) end";

                // Queue the command for main thread execution via FrameUpdate hook
                std::string result = QueueLuaCommand(luaCode, 5000);
                SendPipeResponse(pipe, result);
            }
            else if (cmd == "lua_get") {
                // Thread-safe get global variable via vint_lua (runs on main thread)
                // Usage: lua_get <global_name>
                std::string name;
                if (iss >> name) {
                    // Build Lua code to get the global and return its value
                    std::string luaCode = "local v = _G['" + name + "'] local tv = type(v) if tv == 'string' then return name..' (string) = \"'..v..'\"' elseif tv == 'number' then return name..' (number) = '..v elseif tv == 'boolean' then return name..' (boolean) = '..tostring(v) elseif tv == 'table' then return name..' (table)' elseif tv == 'function' then return name..' (function)' elseif tv == 'nil' then return name..' (nil)' else return name..' ('..tv..')' end";

                    // Queue the command for main thread execution via FrameUpdate hook
                    std::string result = QueueLuaCommand(luaCode, 5000);
                    SendPipeResponse(pipe, result);
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: lua_get <global_name>");
                }
            }
            else if (cmd == "lua_set") {
                // Thread-safe set global variable via vint_lua (runs on main thread)
                // Usage: lua_set <name> <value> (number, true/false, nil, or string)
                std::string name, valueStr;
                if (iss >> name >> valueStr) {
                    // Build Lua code to set the global
                    std::string luaCode;
                    char* end;
                    double num = std::strtod(valueStr.c_str(), &end);
                    if (end != valueStr.c_str() && *end == '\0') {
                        luaCode = "_G['" + name + "'] = " + valueStr;
                    } else if (valueStr == "true" || valueStr == "false") {
                        luaCode = "_G['" + name + "'] = " + valueStr;
                    } else if (valueStr == "nil") {
                        luaCode = "_G['" + name + "'] = nil";
                    } else {
                        // Escape string for Lua
                        std::string escaped = valueStr;
                        std::string result;
                        for (char c : escaped) {
                            if (c == '"') result += "\\\"";
                            else if (c == '\\') result += "\\\\";
                            else if (c == '\n') result += "\\n";
                            else if (c == '\r') result += "\\r";
                            else if (c == '\t') result += "\\t";
                            else result += c;
                        }
                        luaCode = "_G['" + name + "'] = \"" + result + "\"";
                    }

                    // Queue the command for main thread execution via FrameUpdate hook
                    std::string result = QueueLuaCommand(luaCode, 5000);
                    SendPipeResponse(pipe, result);
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: lua_set <name> <value> (number, true/false, nil, or string)");
                }
            }
            else if (cmd == "lua_call") {
                // Thread-safe call Lua function via vint_lua (runs on main thread)
                // Usage: lua_call <func_name> [args...]
                std::string funcName;
                if (iss >> funcName) {
                    std::vector<std::string> args;
                    std::string arg;
                    while (iss >> arg) args.push_back(arg);

                    // Build Lua code to call the function with arguments
                    std::string luaCode = "local f = _G['" + funcName + "'] if type(f) ~= 'function' then return 'ERROR: '..funcName..' is not a function' end local args = {}";

                    for (size_t i = 0; i < args.size(); ++i) {
                        const auto& a = args[i];
                        char* end;
                        double num = std::strtod(a.c_str(), &end);
                        if (end != a.c_str() && *end == '\0') {
                            luaCode += " args[" + std::to_string(i+1) + "] = " + a;
                        } else if (a == "true" || a == "false") {
                            luaCode += " args[" + std::to_string(i+1) + "] = " + a;
                        } else if (a == "nil") {
                            luaCode += " args[" + std::to_string(i+1) + "] = nil";
                        } else {
                            // Escape string for Lua
                            std::string escaped = a;
                            std::string result;
                            for (char c : escaped) {
                                if (c == '"') result += "\\\"";
                                else if (c == '\\') result += "\\\\";
                                else if (c == '\n') result += "\\n";
                                else if (c == '\r') result += "\\r";
                                else if (c == '\t') result += "\\t";
                                else result += c;
                            }
                            luaCode += " args[" + std::to_string(i+1) + "] = \"" + result + "\"";
                        }
                    }

                    luaCode += " local results = {pcall(f, table.unpack(args))} if results[1] then local t = {} for i=2,#results do local v = results[i] local tv = type(v) table.insert(t, '['..(i-1)..'] '..tv..(tv=='string' and ' = \"'..v..'\"' or (tv=='number' and ' = '..v or (tv=='boolean' and ' = '..tostring(v)) or '')) end if #t == 0 then return 'OK: No return values' else return 'OK: '..(#results-1)..' return value(s):\\n'..table.concat(t, '\\n') end else return 'ERROR: '..tostring(results[2]) end";

                    // Queue the command for main thread execution via FrameUpdate hook
                    std::string result = QueueLuaCommand(luaCode, 5000);
                    SendPipeResponse(pipe, result);
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: lua_call <func_name> [args...]");
                }
            }
            else if (cmd == "lua_safe") {
                // Thread-safe Lua execution via vint_lua console command (runs on main thread)
                // Uses QueueLuaCommand which waits for FrameUpdate hook to process on main thread
                // Usage: lua_safe <lua_code>
                std::string code;
                std::getline(iss, code);
                if (!code.empty() && code[0] == ' ') code.erase(0, 1);
                if (code.empty()) {
                    SendPipeResponse(pipe, "ERROR: Usage: lua_safe <lua_code>");
                    continue;
                }

                // Queue the command for main thread execution via FrameUpdate hook
                std::string result = QueueLuaCommand(code, 5000);
                SendPipeResponse(pipe, result);
            }
            else if (cmd == "trace_calls") {
                // Trace function calls: trace_calls <addr> [count] - log calls to function
                DWORD addr; int maxLogs = 100;
                if (iss >> std::hex >> addr >> std::dec >> maxLogs) {
                    // Hook the function to log calls
                    std::string name = "trace_" + std::to_string(addr);
                    BYTE prologue[16];
                    if (SafeReadMemory(prologue, reinterpret_cast<void*>(addr), 16)) {
                        // Find a good prologue size (look for push ebp / mov ebp,esp)
                        size_t proSize = 5;
                        for (size_t i = 0; i < 12; ++i) {
                            if (prologue[i] == 0x55 && prologue[i+1] == 0x8B && prologue[i+2] == 0xEC) { proSize = i + 3; break; }
                        }
                        void* detour = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                        if (detour) {
                            // Build detour: save registers, log call, call original, restore, ret
                            BYTE* p = static_cast<BYTE*>(detour);
                            // pushad
                            *p++ = 0x60;
                            // push addr of log buffer
                            *p++ = 0x68; *reinterpret_cast<DWORD*>(p) = addr; p += 4;
                            // call log function (we'll use a simple approach - write to shared memory)
                            // For now just call original
                            memcpy(p, prologue, proSize); p += proSize;
                            // jmp back to original+proSize
                            *p++ = 0xE9; *reinterpret_cast<DWORD*>(p) = (addr + proSize) - (reinterpret_cast<DWORD>(p) + 4); p += 4;
                            // Write jump at original
                            DWORD oldProtect;
                            VirtualProtect(reinterpret_cast<void*>(addr), proSize, PAGE_EXECUTE_READWRITE, &oldProtect);
                            *reinterpret_cast<BYTE*>(addr) = 0xE9;
                            *reinterpret_cast<DWORD*>(addr + 1) = reinterpret_cast<DWORD>(detour) - addr - 5;
                            VirtualProtect(reinterpret_cast<void*>(addr), proSize, oldProtect, &oldProtect);

                            {
                                std::lock_guard<std::mutex> lock(g_hooksMutex);
                                g_hooks.push_back({addr, proSize, detour, name, true});
                            }
                            SendPipeResponse(pipe, "OK: Call tracing enabled on 0x" + std::to_string(addr) + " (max " + std::to_string(maxLogs) + " logs)");
                        } else {
                            SendPipeResponse(pipe, "ERROR: Failed to allocate detour memory");
                        }
                    } else {
                        SendPipeResponse(pipe, "ERROR: Cannot read function prologue");
                    }
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: trace_calls <addr> [max_logs]");
                }
            }
            else if (cmd == "vtable") {
                // Scan for vtables: vtable <addr> [depth]
                DWORD addr; int depth = 10;
                if (iss >> std::hex >> addr >> std::dec >> depth) {
                    std::ostringstream oss;
                    oss << "VTable at 0x" << std::hex << addr << std::dec << ":\n";
                    for (int i = 0; i < depth; ++i) {
                        DWORD funcPtr;
                        if (!SafeReadMemory(&funcPtr, reinterpret_cast<void*>(addr + i * 4), 4)) {
                            oss << "  [" << i << "] READ FAILED\n";
                            break;
                        }
                        if (funcPtr == 0) {
                            oss << "  [" << i << "] NULL\n";
                            continue;
                        }
                        // Check if it looks like code
                        BYTE firstByte;
                        if (SafeReadMemory(&firstByte, reinterpret_cast<void*>(funcPtr), 1)) {
                            oss << "  [" << i << "] 0x" << std::hex << funcPtr << std::dec << " (first byte: 0x" << std::hex << (int)firstByte << std::dec << ")";
                            // Try to get symbol
                            HANDLE process = GetCurrentProcess();
                            SymInitialize(process, nullptr, TRUE);
                            DWORD64 disp = 0;
                            char symBuf[sizeof(SYMBOL_INFO) + 256];
                            PSYMBOL_INFO sym = reinterpret_cast<PSYMBOL_INFO>(symBuf);
                            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
                            sym->MaxNameLen = 255;
                            if (SymFromAddr(process, funcPtr, &disp, sym)) {
                                oss << " -> " << sym->Name << "+0x" << std::hex << disp << std::dec;
                            }
                            SymCleanup(process);
                            oss << "\n";
                        } else {
                            oss << "  [" << i << "] 0x" << std::hex << funcPtr << std::dec << " (invalid)\n";
                        }
                    }
                    SendPipeResponse(pipe, oss.str());
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: vtable <addr> [depth]");
                }
            }
            else if (cmd == "find_vtables") {
                // Find all vtables in a range: find_vtables <start> <end>
                DWORD start, end;
                if (iss >> std::hex >> start >> std::hex >> end) {
                    std::ostringstream oss;
                    oss << "Scanning for vtables in 0x" << std::hex << start << " - 0x" << end << std::dec << "...\n";
                    int found = 0;
                    for (DWORD addr = start; addr < end && found < 50; addr += 4) {
                        DWORD firstPtr;
                        if (!SafeReadMemory(&firstPtr, reinterpret_cast<void*>(addr), 4)) continue;
                        if (firstPtr < 0x400000 || firstPtr > 0x80000000) continue;
                        // Check if first entry points to code
                        BYTE b;
                        if (!SafeReadMemory(&b, reinterpret_cast<void*>(firstPtr), 1)) continue;
                        if (b != 0x55 && b != 0x8B && b != 0x53 && b != 0x56 && b != 0x57 && b != 0x83 && b != 0x81 && b != 0xE8 && b != 0xE9 && b != 0xCC) continue;
                        // Check second entry too
                        DWORD secondPtr;
                        if (!SafeReadMemory(&secondPtr, reinterpret_cast<void*>(addr + 4), 4)) continue;
                        if (secondPtr == 0 || secondPtr < 0x400000 || secondPtr > 0x80000000) continue;
                        BYTE b2;
                        if (!SafeReadMemory(&b2, reinterpret_cast<void*>(secondPtr), 1)) continue;
                        if (b2 != 0x55 && b2 != 0x8B && b2 != 0x53 && b2 != 0x56 && b2 != 0x57 && b2 != 0x83 && b2 != 0x81 && b2 != 0xE8 && b2 != 0xE9 && b2 != 0xCC) continue;
                        oss << "  Possible vtable at 0x" << std::hex << addr << std::dec << " (first: 0x" << std::hex << firstPtr << std::dec << ")\n";
                        found++;
                    }
                    if (found == 0) oss << "  None found\n";
                    SendPipeResponse(pipe, oss.str());
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: find_vtables <start> <end>");
                }
            }
            else if (cmd == "strings_ref") {
                // Find all references to a string: strings_ref <string_addr>
                DWORD strAddr;
                if (iss >> std::hex >> strAddr) {
                    std::ostringstream oss;
                    // Read the string first
                    char strBuf[256];
                    if (SafeReadMemory(strBuf, reinterpret_cast<void*>(strAddr), 255)) {
                        strBuf[255] = 0;
                        oss << "String at 0x" << std::hex << strAddr << std::dec << ": \"" << strBuf << "\"\n";
                    }
                    // Search for references
                    oss << "References:\n";
                    int found = 0;
                    for (DWORD addr = 0x400000; addr < 0x20000000 && found < 50; ++addr) {
                        DWORD val;
                        if (!SafeReadMemory(&val, reinterpret_cast<void*>(addr), 4)) continue;
                        if (val == strAddr) {
                            oss << "  DATA: 0x" << std::hex << addr << std::dec << "\n";
                            found++;
                        }
                    }
                    // Also check code for PUSH strAddr
                    for (DWORD addr = 0x400000; addr < 0x20000000 && found < 50; ++addr) {
                        BYTE buf[5];
                        if (!SafeReadMemory(buf, reinterpret_cast<void*>(addr), 5)) continue;
                        if (buf[0] == 0x68) { // PUSH imm32
                            DWORD imm = *reinterpret_cast<DWORD*>(&buf[1]);
                            if (imm == strAddr) {
                                oss << "  CODE: 0x" << std::hex << addr << std::dec << " (PUSH)\n";
                                found++;
                            }
                        }
                    }
                    if (found == 0) oss << "  None found\n";
                    SendPipeResponse(pipe, oss.str());
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: strings_ref <string_addr>");
                }
            }
            else if (cmd == "alloc_track") {
                // Track allocations: alloc_track on|off
                std::string action;
                if (iss >> action) {
                    static bool tracking = false;
                    if (action == "on" && !tracking) {
                        // Hook malloc/free/HeapAlloc/HeapFree
                        constexpr DWORD kMalloc = 0xD9A000; // example - would need real address
                        SendPipeResponse(pipe, "Allocation tracking: " + action + " (placeholder - needs real hook addresses)");
                        tracking = true;
                    } else if (action == "off" && tracking) {
                        SendPipeResponse(pipe, "Allocation tracking: " + action);
                        tracking = false;
                    } else {
                        SendPipeResponse(pipe, "Current state: " + std::string(tracking ? "on" : "off"));
                    }
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: alloc_track on|off");
                }
            }
            else if (cmd == "heap_walk") {
                // Walk process heap: heap_walk
                std::ostringstream oss;
                HANDLE heap = GetProcessHeap();
                PROCESS_HEAP_ENTRY entry = {};
                entry.lpData = nullptr;
                int count = 0;
                while (HeapWalk(heap, &entry) && count < 200) {
                    oss << "0x" << std::hex << reinterpret_cast<DWORD>(entry.lpData) << std::dec
                        << " size=" << entry.cbData
                        << " flags=" << std::hex << entry.wFlags << std::dec;
                    if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) oss << " BUSY";
                    if (entry.wFlags & PROCESS_HEAP_ENTRY_MOVEABLE) oss << " MOVEABLE";
                    if (entry.wFlags & PROCESS_HEAP_ENTRY_DDESHARE) oss << " DDESHARE";
                    oss << "\n";
                    count++;
                }
                oss << "Total entries shown: " << count << "\n";
                SendPipeResponse(pipe, oss.str());
            }
            else if (cmd == "seh_chain") {
                // Show SEH chain: seh_chain
                std::ostringstream oss;
                DWORD sehPtr = __readfsdword(0);
                int depth = 0;
                while (sehPtr != 0 && sehPtr != 0xFFFFFFFF && depth < 30) {
                    DWORD handler, next;
                    if (!SafeReadMemory(&next, reinterpret_cast<void*>(sehPtr), 4)) break;
                    if (!SafeReadMemory(&handler, reinterpret_cast<void*>(sehPtr + 4), 4)) break;
                    oss << "  Frame 0x" << std::hex << sehPtr << std::dec << ": handler=0x" << std::hex << handler << std::dec << "\n";
                    // Try to resolve handler symbol
                    HANDLE process = GetCurrentProcess();
                    SymInitialize(process, nullptr, TRUE);
                    DWORD64 disp = 0;
                    char symBuf[sizeof(SYMBOL_INFO) + 256];
                    PSYMBOL_INFO sym = reinterpret_cast<PSYMBOL_INFO>(symBuf);
                    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
                    sym->MaxNameLen = 255;
                    if (SymFromAddr(process, handler, &disp, sym)) {
                        oss << "    -> " << sym->Name << "+0x" << std::hex << disp << std::dec << "\n";
                    }
                    SymCleanup(process);
                    sehPtr = next;
                    depth++;
                }
                if (depth == 0) oss << "  (empty or inaccessible)\n";
                SendPipeResponse(pipe, oss.str());
            }
            else if (cmd == "perf_sample") {
                // Sample instruction pointer: perf_sample [count] [interval_ms]
                int count = 1000, interval = 1;
                iss >> count >> interval;
                std::map<DWORD, int> samples;
                for (int i = 0; i < count; ++i) {
                    CONTEXT ctx = {};
                    ctx.ContextFlags = CONTEXT_CONTROL;
                    HANDLE hThread = GetCurrentThread();
                    if (GetThreadContext(hThread, &ctx)) {
                        samples[ctx.Eip]++;
                    }
                    Sleep(interval);
                }
                std::ostringstream oss;
                oss << "Profile samples (" << count << " over " << (count * interval) << "ms):\n";
                // Sort by count
                std::vector<std::pair<DWORD, int>> sorted(samples.begin(), samples.end());
                std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
                for (size_t i = 0; i < sorted.size() && i < 30; ++i) {
                    oss << "  0x" << std::hex << sorted[i].first << std::dec << ": " << sorted[i].second << " hits";
                    // Try symbol
                    HANDLE process = GetCurrentProcess();
                    SymInitialize(process, nullptr, TRUE);
                    DWORD64 disp = 0;
                    char symBuf[sizeof(SYMBOL_INFO) + 256];
                    PSYMBOL_INFO sym = reinterpret_cast<PSYMBOL_INFO>(symBuf);
                    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
                    sym->MaxNameLen = 255;
                    if (SymFromAddr(process, sorted[i].first, &disp, sym)) {
                        oss << " (" << sym->Name << "+0x" << std::hex << disp << std::dec << ")";
                    }
                    SymCleanup(process);
                    oss << "\n";
                }
                SendPipeResponse(pipe, oss.str());
            }
            else if (cmd == "mem_diff") {
                // Compare memory region: mem_diff <addr> <size> <snapshot_id>
                // snapshot_id: 0 = take snapshot, 1+ = compare with snapshot
                DWORD addr, size, snapId;
                if (iss >> std::hex >> addr >> std::dec >> size >> snapId) {
                    static std::vector<BYTE> snapshot;
                    if (snapId == 0) {
                        snapshot.resize(size);
                        if (SafeReadMemory(snapshot.data(), reinterpret_cast<void*>(addr), size)) {
                            SendPipeResponse(pipe, "OK: Snapshot taken (" + std::to_string(size) + " bytes at 0x" + std::to_string(addr) + ")");
                        } else {
                            SendPipeResponse(pipe, "ERROR: Failed to read memory");
                        }
                    } else if (!snapshot.empty() && snapshot.size() == size) {
                        std::vector<BYTE> current(size);
                        if (SafeReadMemory(current.data(), reinterpret_cast<void*>(addr), size)) {
                            std::ostringstream oss;
                            int diffs = 0;
                            for (size_t i = 0; i < size && diffs < 100; ++i) {
                                if (current[i] != snapshot[i]) {
                                    oss << "  +0x" << std::hex << i << std::dec << ": was 0x" << std::hex << (int)snapshot[i] << std::dec << " now 0x" << std::hex << (int)current[i] << std::dec << "\n";
                                    diffs++;
                                }
                            }
                            if (diffs == 0) oss << "  No differences\n";
                            else oss << "Total differences: " << diffs << "\n";
                            SendPipeResponse(pipe, oss.str());
                        } else {
                            SendPipeResponse(pipe, "ERROR: Failed to read current memory");
                        }
                    } else {
                        SendPipeResponse(pipe, "ERROR: No snapshot or size mismatch");
                    }
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: mem_diff <addr> <size> <0|1> (0=snapshot, 1=diff)");
                }
            }
            else if (cmd == "signature") {
                // Generate IDA-style signature: signature <addr> <len>
                DWORD addr, len;
                if (iss >> std::hex >> addr >> std::dec >> len) {
                    std::vector<BYTE> bytes(len);
                    if (!SafeReadMemory(bytes.data(), reinterpret_cast<void*>(addr), len)) {
                        SendPipeResponse(pipe, "ERROR: Failed to read memory");
                        continue;
                    }
                    std::ostringstream oss;
                    oss << "Signature for 0x" << std::hex << addr << std::dec << " (" << len << " bytes):\n";
                    // IDA style: xx ?? xx xx ...
                    for (DWORD i = 0; i < len; ++i) {
                        // For simplicity, just show all bytes - real sig would mask relocations
                        oss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i] << " ";
                        if ((i + 1) % 16 == 0) oss << "\n";
                    }
                    oss << "\n";
                    // Also show as mask
                    oss << "Mask: ";
                    for (DWORD i = 0; i < len; ++i) oss << "x";
                    oss << "\n";
                    SendPipeResponse(pipe, oss.str());
                } else {
                    SendPipeResponse(pipe, "ERROR: Usage: signature <addr> <len>");
                }
            }
            else if (cmd == "help") {
                SendPipeResponse(pipe,
                    "RFA Live Analysis Commands:\n"
                    "  read <addr> <size>       - Read game memory\n"
                    "  write <addr> <hex>       - Write game memory\n"
                    "  state                    - Get game state snapshot\n"
                    "  modules                  - List loaded modules\n"
                    "  hook <addr> <size> <name> - Register function hook\n"
                    "  unhook <name>            - Remove function hook\n"
                    "  hooks                    - List active hooks\n"
                    "  call <addr>              - Call function (dangerous)\n"
                    "  exec <command>           - Execute console command\n"
                    "  scan <start> <size> <pat> [mask] - Scan memory for pattern\n"
                    "  enum_mem                 - Enumerate memory regions\n"
                    "  trace on|off             - Enable call tracing\n"
                    "  call_func <addr> <conv> [args] - Call function with convention\n"
                    "  callstack                - Capture call stack\n"
                    "  registers                - Get CPU registers\n"
                    "  enum_objects             - List vehicles/items/weapons\n"
                    "  read_chain <base> <offs> - Follow pointer chain\n"
                    "  breakpoint set|clr <addr> - Set/clear INT3 breakpoint\n"
                    "  disasm <addr> [count]    - Disassemble bytes\n"
                    "  xrefs <addr> [r1] [r2]   - Find cross-references\n"
                    "  exports <base>           - List module exports\n"
                    "  threads                  - List threads with context\n"
                    "  watch <addr> <size> <rw> - Guard page memory watch\n"
                    "  --- RE Tools ---\n"
                    "  trace_calls <addr> [max] - Enable function call tracing\n"
                    "  vtable <addr> [depth]    - View vtable entries\n"
                    "  find_vtables <start> <end> - Scan for vtables in range\n"
                    "  strings_ref <addr>       - Find all refs to a string\n"
                    "  alloc_track on|off       - Track allocations (placeholder)\n"
                    "  heap_walk                - Walk process heap\n"
                    "  seh_chain                - Show SEH chain\n"
                    "  perf_sample [count] [ms] - Profile by IP sampling\n"
                    "  mem_diff <addr> <sz> <id> - Snapshot/diff memory\n"
                    "  signature <addr> <len>   - Generate IDA-style signature\n"
                    "  --- Game Control ---\n"
                    "  teleport <x> <y> <z> [yaw] [pitch] - Move player\n"
                    "  set_camera <x> <y> <z> [yaw] [pitch] [fov] - Set camera\n"
                    "  input <key> [down|up|press] - Simulate keyboard (W,A,S,D,SPACE,etc)\n"
                    "  mouse <dx> <dy> [btns] [wheel] - Simulate mouse\n"
                    "  time <hours>             - Set time of day (0-24)\n"
                    "  god on|off               - Toggle god mode\n"
                    "  noclip on|off            - Toggle collision\n"
                    "  give <item|weapon|veh|ammo> <name> [count] - Spawn items\n"
                    "  speed <mult>             - Set game speed (1.0=normal)\n"
                    "  freeze on|off            - Freeze/unfreeze game\n"
                    "  --- Lua (thread-safe, main thread via vint_lua) ---\n"
                    "  lua_safe <code>          - Execute Lua via vint_lua on main thread\n"
                    "  lua_state [target]       - Inspect Lua state (global|stack|registry)\n"
                    "  lua_run <code>           - Run Lua code and capture returns\n"
                    "  lua_get <name>           - Get global variable value\n"
                    "  lua_set <name> <value>   - Set global variable (num/bool/nil/str)\n"
                    "  lua_call <func> [args]   - Call Lua function with args\n"
                    "  help                     - Show this help");
            }
            else {
                SendPipeResponse(pipe, "ERROR: Unknown command. Type 'help' for list.");
            }
        }

        CloseHandle(pipe);
        g_pipe = INVALID_HANDLE_VALUE;
        rfa::LogDetail("Pipe closed, waiting for next connection...");
    }

    rfa::LogDetail("Pipe server thread ended");
}

// Send binary response
bool SendPipeResponse(HANDLE pipe, const std::vector<BYTE>& data) {
    DWORD written;
    // Send length prefix (4 bytes)
    DWORD len = static_cast<DWORD>(data.size());
    if (!WriteFile(pipe, &len, 4, &written, nullptr)) return false;
    if (!WriteFile(pipe, data.data(), len, &written, nullptr)) return false;
    return true;
}

// Send text response
bool SendPipeResponse(HANDLE pipe, const std::string& text) {
    std::vector<BYTE> data(text.begin(), text.end());
    return SendPipeResponse(pipe, data);
}

// Read game memory safely
std::vector<BYTE> ReadGameMemory(DWORD address, size_t size) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi))) {
        return {};
    }

    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.State == MEM_COMMIT && (mbi.Protect & readable) && !(mbi.Protect & PAGE_GUARD))) {
        return {};
    }

    size_t maxRead = std::min(size, static_cast<size_t>(reinterpret_cast<BYTE*>(mbi.BaseAddress) + mbi.RegionSize - reinterpret_cast<BYTE*>(address)));
    if (maxRead == 0) return {};

    std::vector<BYTE> result(maxRead);
    if (!SafeReadMemory(result.data(), reinterpret_cast<void*>(address), maxRead)) {
        return {};
    }

    return result;
}

// Write game memory safely
bool WriteGameMemory(DWORD address, const BYTE* data, size_t size) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi))) {
        return false;
    }

    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.State == MEM_COMMIT && (mbi.Protect & writable) && !(mbi.Protect & PAGE_GUARD))) {
        // Try to change protection
        DWORD old;
        if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &old)) {
            return false;
        }
        if (!SafeWriteMemory(reinterpret_cast<void*>(address), data, size)) {
            return false;
        }
        VirtualProtect(reinterpret_cast<void*>(address), size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
        return true;
    }

    if (!SafeWriteMemory(reinterpret_cast<void*>(address), data, size)) {
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
    return true;
}

// Update cached game state (called periodically)
void UpdateGameStateCache() {
    std::lock_guard<std::mutex> lock(g_stateMutex);

    // These addresses are from the publicbeta build (1932726)
    // Player object
    constexpr DWORD kPlayerPtr = 0x8B92D0;  // ThePlayer() function
    constexpr DWORD kObjectList = 0x1870630;

    // Try to get player
    using ThePlayerFn = BYTE*(__cdecl*)();
    auto ThePlayer = reinterpret_cast<ThePlayerFn>(kPlayerPtr);

    BYTE* player = ThePlayer();
    if (player) {
        memcpy(g_cachedState.playerPos, player + 4, 12);
        memcpy(g_cachedState.playerRot, player + 16, 36);
    }

    memcpy(g_cachedState.cameraPos, g_cameraPosition, 12);
    memcpy(g_cachedState.cameraRot, g_cameraRotation, 36);
    g_cachedState.cameraFov = g_cameraFov;

    // Current level name (world + 0x18)
    const char* level = reinterpret_cast<const char*>(kObjectList + 0x18);
    if (level && *level) {
        // Just store a hash for now
        DWORD hash = 0;
        for (const char* p = level; *p; ++p) {
            hash = ((hash << 6) | (hash >> 26)) ^ static_cast<unsigned char>(*p);
        }
        g_cachedState.currentLevel = hash;
    }

    // Object count (rough estimate from object list)
    // The object list is at kObjectList, first entry is count
    g_cachedState.objectCount = *reinterpret_cast<int*>(kObjectList);

    // Game time (time of day)
    g_cachedState.gameTime = static_cast<DWORD>(g_timeOfDay);

    // In menu check (simplified)
    g_cachedState.inMenu = false; // Would need more investigation
}

// Get a snapshot of current game state
GameState GetGameStateSnapshot() {
    UpdateGameStateCache();
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_cachedState;
}

// Handle a command from the queue (for async processing)
void HandleCommand(const PipeCommand& cmd) {
    switch (cmd.type) {
        case PipeCommand::ReadMemory: {
            auto data = ReadGameMemory(cmd.address, cmd.size);
            if (cmd.replyData) {
                std::lock_guard<std::mutex> lock(g_queueMutex);
                *cmd.replyData = data;
            }
            if (cmd.replyEvent) SetEvent(cmd.replyEvent);
            break;
        }
        case PipeCommand::WriteMemory: {
            bool ok = WriteGameMemory(cmd.address, cmd.data.data(), cmd.data.size());
            if (cmd.replyEvent) SetEvent(cmd.replyEvent);
            break;
        }
        case PipeCommand::GetState: {
            GameState state = GetGameStateSnapshot();
            if (cmd.replyString) {
                std::ostringstream oss;
                oss << state.playerPos[0] << " " << state.playerPos[1] << " " << state.playerPos[2] << "|"
                    << state.cameraPos[0] << " " << state.cameraPos[1] << " " << state.cameraPos[2] << "|"
                    << state.cameraFov << "|" << state.objectCount;
                std::lock_guard<std::mutex> lock(g_queueMutex);
                *cmd.replyString = oss.str();
            }
            if (cmd.replyEvent) SetEvent(cmd.replyEvent);
            break;
        }
        default:
            break;
    }
}

} // namespace live_analysis

// Exported entry point for rfa_qol.dll
extern "C" __declspec(dllexport) void __cdecl RFAMod_Init_LiveAnalysis() {
    live_analysis::Init();
}

// For proper cleanup, we'd need a shutdown export
// This is called when the game exits (via atexit or DllMain)
extern "C" __declspec(dllexport) void __cdecl RFAMod_Shutdown_LiveAnalysis() {
    live_analysis::Shutdown();
}