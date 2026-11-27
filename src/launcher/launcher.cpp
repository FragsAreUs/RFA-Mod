// RFAModLauncher: starts Red Faction: Armageddon with the mod loader injected.
//
// Nothing in the game folder is modified. The game is started suspended and parked at its
// entry point while rfa_loader.dll is loaded into it, then released. All patches live in memory.
//
// The game folder is GameDir from rfa_mods.ini, or the launcher's own folder when the mod files
// were copied into the game folder, or (with Steam launch options "<launcher> %command%") the
// folder of the exe Steam passes on the command line.
//
// Before anything reads them, it adds the settings a newer RFA-Mod brings to your rfa_mods.ini and
// console.ini, from the templates in defaults\ (see settings_update.h).
//
// With LargeAddressAware=1 it starts the game from a copy of its exe in large_address\ with the
// "large address aware" mark set, so the 32-bit game gets 4 GB of memory to work in instead of 2
// (see LargeAddressCopy).
//
// If there's a Guerrilla world here built by an older (or newer) world builder than this RFA-Mod
// needs, it says so and asks before starting the game (see CheckGuerrillaWorld).
#define RFA_MOD_NAME "launcher"
#include "rfa_mod.h"
#include "rfa_game.h"
#include "settings_update.h"
#include <winternl.h>
#include <shellapi.h>
#include <vector>

static_assert(sizeof(void*) == 4, "Build the launcher as 32-bit: the game is a 32-bit process.");

namespace {

constexpr wchar_t kSteamAppId[] = L"55110";
constexpr wchar_t kDefaultExe[] = L"RedFactionArmageddon_DX11.exe";
// rf4_launcher.exe always passes this. Without it the engine expects loose dev files instead of
// the shipped .vpp_pc archives and the first frames fail with DXGI_ERROR_DEVICE_REMOVED.
constexpr wchar_t kDefaultArgs[] = L"-packfiles";

bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring ParentDir(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

bool EndsWithNoCase(const std::wstring& s, const wchar_t* suffix) {
    size_t n = wcslen(suffix);
    return s.size() >= n && _wcsicmp(s.c_str() + s.size() - n, suffix) == 0;
}

std::wstring QuoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (wchar_t c : arg) {
        if (c == L'"') out += L'\\';
        out += c;
    }
    if (!arg.empty() && arg.back() == L'\\') out += L'\\';
    return out + L"\"";
}

std::wstring ErrorText(DWORD code) {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   code, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    std::wstring text = L"error " + std::to_wstring(code) + (msg ? L": " + std::wstring(msg) : L"");
    LocalFree(msg);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r')) text.pop_back();
    return text;
}

int Fail(const std::wstring& message) {
    rfa::Log("ERROR: %ls", message.c_str());
    MessageBoxW(nullptr, message.c_str(), L"RFA Mod Launcher", MB_ICONERROR | MB_OK);
    return 1;
}

// The Guerrilla world's format this RFA-Mod's mods need: the world builder's GuerrillaWorld.Format
// (raised whenever a world built before won't work with the mods any more), which it writes into
// mods\guerrilla_world\world.txt ("RFA-Mod Guerrilla world, format N") once a world is built.
constexpr int kGuerrillaWorldFormat = 13;

// When a Guerrilla world is here (overrides\build\pc\cache\rfg_l0.vpp_pc) but its stamp is missing
// (built before worlds were stamped) or names another format, says what to do and asks whether to
// start the game anyway. False: don't start it.
bool CheckGuerrillaWorld() {
    const std::wstring root = rfa::RootDir();
    if (!FileExists(root + L"\\overrides\\build\\pc\\cache\\rfg_l0.vpp_pc")) return true;  // no world built here
    int format = 0;
    FILE* file = nullptr;
    if (_wfopen_s(&file, (root + L"\\mods\\guerrilla_world\\world.txt").c_str(), L"r") == 0 && file) {
        char line[128] = "";
        if (fgets(line, sizeof(line), file)) sscanf_s(line, "RFA-Mod Guerrilla world, format %d", &format);
        fclose(file);
    }
    if (format == kGuerrillaWorldFormat) return true;
    const bool newer = format > kGuerrillaWorldFormat;
    if (format == 0)
        rfa::Log("Guerrilla world: built by an older RFA-Mod (no world.txt); this one needs format %d", kGuerrillaWorldFormat);
    else
        rfa::Log("Guerrilla world: format %d, but this RFA-Mod needs format %d", format, kGuerrillaWorldFormat);
    const std::wstring message =
        std::wstring(L"Your Guerrilla world was built by ") + (newer ? L"a newer" : L"an older") +
        L" version of RFA-Mod, so it won't look right with this one: the ground can show as a grid, and parts "
        L"of the world can be missing or out of place.\n\n"
        L"Build it again with this version's builder, editor\\GuerrillaBuilder.exe (a minute or more): "
        L"choose No and it opens, with what to build ticked.\n\n"
        L"Start the game anyway?";
    const int answer = MessageBoxW(nullptr, message.c_str(), L"RFA Mod Launcher", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
    if (answer != IDYES) {
        rfa::Log("Guerrilla world: not starting the game (you chose to build the world again first)");
        const std::wstring builder = root + L"\\editor\\GuerrillaBuilder.exe";
        if (FileExists(builder))
            ShellExecuteW(nullptr, L"open", builder.c_str(), nullptr, (root + L"\\editor").c_str(), SW_SHOWNORMAL);
        return false;
    }
    rfa::Log("Guerrilla world: starting the game anyway");
    return true;
}

std::wstring TrimSlashes(std::wstring dir) {
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    return dir;
}

// Windows gives a 32-bit program 4 GB of memory to work in (on 64-bit Windows) only if its exe is
// marked "large address aware" (IMAGE_FILE_LARGE_ADDRESS_AWARE in its file header), and reads the
// mark only as the process starts; the game's exe hasn't it. So the game is started from a copy of
// its exe with that one mark set, kept in large_address\ in the mod folder, the game folder left as
// it is. The copy carries the installed exe's last-write time, so it is made again whenever the
// game updates. The game runs in the game folder as before (the loader tells it the installed exe's
// path; see the loader). Returns the exe to start: the copy, or the installed one if it already
// has the mark; empty on failure, with `err` set.
std::wstring LargeAddressCopy(const std::wstring& exe, const std::wstring& exeName, std::wstring& err) {
    WIN32_FILE_ATTRIBUTE_DATA installed;
    if (!GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &installed)) {
        err = L"could not read " + exe + L", " + ErrorText(GetLastError());
        return L"";
    }
    // Reads the file header's characteristics (or writes them, with `set`).
    auto characteristics = [](const std::wstring& path, WORD& value, bool set) {
        HANDLE file = CreateFileW(path.c_str(), set ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        IMAGE_DOS_HEADER dos{};
        DWORD done = 0;
        bool ok = ReadFile(file, &dos, sizeof(dos), &done, nullptr) && done == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE;
        // The characteristics: after the "PE\0\0" signature, the file header's last word.
        const LONG at = dos.e_lfanew + sizeof(DWORD) + offsetof(IMAGE_FILE_HEADER, Characteristics);
        ok = ok && SetFilePointer(file, at, nullptr, FILE_BEGIN) == static_cast<DWORD>(at);
        if (ok && set) ok = WriteFile(file, &value, sizeof(value), &done, nullptr) && done == sizeof(value);
        else if (ok) ok = ReadFile(file, &value, sizeof(value), &done, nullptr) && done == sizeof(value);
        CloseHandle(file);
        return ok;
    };
    WORD flags = 0;
    if (!characteristics(exe, flags, false)) {
        err = L"could not read the file header of " + exe;
        return L"";
    }
    if (flags & IMAGE_FILE_LARGE_ADDRESS_AWARE) {
        rfa::Log("Large address aware: the game's own program already is");
        return exe;
    }

    const std::wstring folder = rfa::RootDir() + L"\\large_address";
    const std::wstring copy = folder + L"\\" + exeName;
    WIN32_FILE_ATTRIBUTE_DATA kept;
    WORD keptFlags = 0;
    if (GetFileAttributesExW(copy.c_str(), GetFileExInfoStandard, &kept) && kept.nFileSizeLow == installed.nFileSizeLow &&
        kept.nFileSizeHigh == installed.nFileSizeHigh && CompareFileTime(&kept.ftLastWriteTime, &installed.ftLastWriteTime) == 0 &&
        characteristics(copy, keptFlags, false) && (keptFlags & IMAGE_FILE_LARGE_ADDRESS_AWARE)) {
        rfa::Log("Large address aware: starting the game from %ls", copy.c_str());
        return copy;
    }
    CreateDirectoryW(folder.c_str(), nullptr);
    if (!CopyFileW(exe.c_str(), copy.c_str(), FALSE)) {
        err = L"could not copy " + exe + L" to " + copy + L", " + ErrorText(GetLastError());
        return L"";
    }
    WORD marked = flags | IMAGE_FILE_LARGE_ADDRESS_AWARE;
    if (!characteristics(copy, marked, true)) {
        err = L"could not mark " + copy + L" large address aware";
        return L"";
    }
    // (The header's checksum is left as it was: Windows checks it only for drivers and boot files.)
    HANDLE file = CreateFileW(copy.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        SetFileTime(file, nullptr, nullptr, &installed.ftLastWriteTime);
        CloseHandle(file);
    }
    rfa::Log("Large address aware: made %ls (a copy of the game's program with that mark set); starting the game from it", copy.c_str());
    return copy;
}

bool ReadMem(HANDLE proc, const void* addr, void* out, size_t size) {
    SIZE_T done = 0;
    return ReadProcessMemory(proc, addr, out, size, &done) && done == size;
}

bool WriteMem(HANDLE proc, void* addr, const void* data, size_t size) {
    SIZE_T done = 0;
    bool ok = WriteProcessMemory(proc, addr, data, size, &done) && done == size;
    FlushInstructionCache(proc, addr, size);
    return ok;
}

// Runs fn(arg) on a new thread inside the game and returns the thread's exit code.
bool RemoteCall(HANDLE proc, const void* fn, void* arg, DWORD timeoutMs, DWORD& result, std::wstring& err) {
    HANDLE thread = CreateRemoteThread(proc, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(const_cast<void*>(fn)),
                                       arg, 0, nullptr);
    if (!thread) {
        err = L"CreateRemoteThread failed, " + ErrorText(GetLastError());
        return false;
    }
    bool done = WaitForSingleObject(thread, timeoutMs) == WAIT_OBJECT_0;
    if (done) GetExitCodeThread(thread, &result);
    else err = L"timed out waiting for the mod loader inside the game";
    CloseHandle(thread);
    return done;
}

bool InjectLoader(const PROCESS_INFORMATION& pi, const std::wstring& loaderPath, std::wstring& err) {
    HANDLE proc = pi.hProcess;

    // Locate the game's entry point through its PEB.
    auto queryProcess = reinterpret_cast<decltype(&NtQueryInformationProcess)>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    PROCESS_BASIC_INFORMATION pbi{};
    PEB peb{};
    if (!queryProcess || queryProcess(proc, ProcessBasicInformation, &pbi, sizeof(pbi), nullptr) < 0 ||
        !ReadMem(proc, pbi.PebBaseAddress, &peb, sizeof(peb))) {
        err = L"could not read the game's process information";
        return false;
    }
    auto image = static_cast<BYTE*>(peb.Reserved3[1]);  // PEB::ImageBaseAddress
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS32 nt;
    if (!ReadMem(proc, image, &dos, sizeof(dos)) || !ReadMem(proc, image + dos.e_lfanew, &nt, sizeof(nt))) {
        err = L"could not read the game's executable headers";
        return false;
    }
    BYTE* entry = image + nt.OptionalHeader.AddressOfEntryPoint;
    rfa::Log("Game image at %p, entry point %p", image, entry);

    // Park the main thread at the entry point: Windows finishes loading the game's DLLs first, then
    // the thread loops there before any game code has run. This is the launcher's only machine
    // code: the two bytes of the x86 instruction "jump to yourself". The game's original two bytes
    // are put back before it is released.
    const BYTE spin[2] = {0xEB, 0xFE};
    BYTE saved[2];
    DWORD oldProtect;
    if (!ReadMem(proc, entry, saved, 2) || !VirtualProtectEx(proc, entry, 2, PAGE_EXECUTE_READWRITE, &oldProtect) ||
        !WriteMem(proc, entry, spin, 2)) {
        err = L"could not patch the entry point in memory, " + ErrorText(GetLastError());
        return false;
    }
    ResumeThread(pi.hThread);
    bool parked = false;
    for (int i = 0; i < 1500 && !parked; ++i) {
        Sleep(10);
        if (WaitForSingleObject(proc, 0) == WAIT_OBJECT_0) {
            err = L"the game exited during startup";
            return false;
        }
        SuspendThread(pi.hThread);
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(pi.hThread, &ctx) && ctx.Eip == reinterpret_cast<DWORD>(entry)) parked = true;
        else ResumeThread(pi.hThread);
    }
    if (!parked) {
        err = L"the game never reached its entry point";
        return false;
    }

    // LoadLibraryW(loader) inside the game. kernel32 sits at the same address in every
    // 32-bit process, so our own LoadLibraryW address is valid there too.
    size_t bytes = (loaderPath.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath || !WriteMem(proc, remotePath, loaderPath.c_str(), bytes)) {
        err = L"could not write into the game's memory";
        return false;
    }
    DWORD remoteBase = 0;
    auto loadLibrary = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!RemoteCall(proc, loadLibrary, remotePath, 30000, remoteBase, err)) return false;
    VirtualFreeEx(proc, remotePath, 0, MEM_RELEASE);
    if (!remoteBase) {
        err = L"Windows refused to load " + loaderPath + L" into the game (antivirus?)";
        return false;
    }

    // Find RFALoader_Init's offset by loading the same DLL here (its DllMain does nothing).
    HMODULE local = LoadLibraryW(loaderPath.c_str());
    auto init = local ? reinterpret_cast<BYTE*>(GetProcAddress(local, "RFALoader_Init")) : nullptr;
    if (!init) {
        err = L"rfa_loader.dll is missing its RFALoader_Init export";
        return false;
    }
    const void* remoteInit = reinterpret_cast<BYTE*>(remoteBase) + (init - reinterpret_cast<BYTE*>(local));
    DWORD modCount = 0;
    if (!RemoteCall(proc, remoteInit, nullptr, 60000, modCount, err)) return false;
    rfa::Log("Mod loader ready, %lu mod(s) loaded", modCount);

    // Restore the original entry-point bytes and let the game run.
    if (!WriteMem(proc, entry, saved, 2)) {
        err = L"could not restore the entry point";
        return false;
    }
    VirtualProtectEx(proc, entry, 2, oldProtect, &oldProtect);
    ResumeThread(pi.hThread);
    return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Keep the previous session's log around for comparison.
    CreateDirectoryW((rfa::RootDir() + L"\\logs").c_str(), nullptr);
    MoveFileExW(rfa::LogPath().c_str(), (rfa::RootDir() + L"\\logs\\rfa_mods.prev.log").c_str(),
                MOVEFILE_REPLACE_EXISTING);
    rfa::Log("RFA mod launcher starting from %ls", rfa::RootDir().c_str());

    // An update keeps your own settings files: give them the settings it brings, keeping your values.
    settings::Update(rfa::RootDir(), L"rfa_mods.ini");
    settings::Update(rfa::RootDir(), L"console.ini");

    // When Steam launches us via "%command%", argv[1] is the game's own launcher exe.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring commandLineDir, passthrough;
    int firstArg = 1;
    if (argc > 1 && EndsWithNoCase(argv[1], L".exe") && FileExists(argv[1])) {
        commandLineDir = ParentDir(argv[1]);
        firstArg = 2;
        rfa::Log("Started with game path %ls", argv[1]);
    }
    for (int i = firstArg; i < argc; ++i) passthrough += L" " + QuoteArg(argv[i]);
    LocalFree(argv);

    // Game folder: GameDir from the ini, else our own folder (mod installed into the game
    // folder), else the folder Steam passed on the command line.
    const std::wstring exeName = rfa::IniString(L"Launcher", L"GameExe", kDefaultExe);
    std::wstring gameDir = TrimSlashes(rfa::IniString(L"Launcher", L"GameDir"));
    if (gameDir.empty() && FileExists(rfa::RootDir() + L"\\" + exeName)) gameDir = rfa::RootDir();
    if (gameDir.empty()) gameDir = commandLineDir;
    if (gameDir.empty())
        return Fail(L"Don't know where the game is installed.\n\nSet GameDir in " + rfa::IniPath() +
                    L"\nto your Red Faction: Armageddon folder, or copy the mod files into the game folder.");

    const std::wstring exe = gameDir + L"\\" + exeName;
    if (!FileExists(exe))
        return Fail(L"Could not find the game executable:\n" + exe + L"\n\nCheck GameDir and GameExe in " +
                    rfa::IniPath());
    const std::wstring loader = rfa::RootDir() + L"\\rfa_loader.dll";
    if (!FileExists(loader)) return Fail(L"Missing " + loader);
    if (!CheckGuerrillaWorld()) return 0;

    std::wstring args = rfa::IniString(L"Launcher", L"GameArgs", kDefaultArgs);
    std::wstring cmdLine = QuoteArg(exe) + (args.empty() ? L"" : L" " + args) + passthrough;
    rfa::Log("Starting %ls", cmdLine.c_str());

    // Without an app id the game asks Steam to restart it, which would drop our mods.
    if (!GetEnvironmentVariableW(L"SteamAppId", nullptr, 0)) SetEnvironmentVariableW(L"SteamAppId", kSteamAppId);
    if (!GetEnvironmentVariableW(L"SteamGameId", nullptr, 0)) SetEnvironmentVariableW(L"SteamGameId", kSteamAppId);

    // The program to start: the game's own, or its large-address-aware copy. Started from the copy,
    // the game is told the installed exe's path (rfa::game::kInstalledExeVariable) and finds its DLLs
    // (steam_api.dll, binkw32.dll) in the game folder, which leads its PATH as well as being its
    // current folder.
    std::wstring program = exe;
    if (rfa::IniInt(L"Launcher", L"LargeAddressAware", 1)) {
        std::wstring copyErr;
        program = LargeAddressCopy(exe, exeName, copyErr);
        if (program.empty()) {
            rfa::Log("Large address aware: %ls; starting the game's own program (2 GB)", copyErr.c_str());
            program = exe;
        }
    }
    if (program != exe) {
        SetEnvironmentVariableW(rfa::game::kInstalledExeVariable, exe.c_str());
        const DWORD pathLength = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        std::wstring path(pathLength, L'\0');
        if (pathLength) path.resize(GetEnvironmentVariableW(L"PATH", path.data(), pathLength));
        SetEnvironmentVariableW(L"PATH", (gameDir + (path.empty() ? L"" : L";" + path)).c_str());
    }

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');
    if (!CreateProcessW(program.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, gameDir.c_str(),
                        &si, &pi))
        return Fail(L"Could not start the game, " + ErrorText(GetLastError()));

    // Hand our right to take the foreground to the game; without it the game starts behind
    // other windows and immediately runs its "alt-tabbed away" code.
    AllowSetForegroundWindow(pi.dwProcessId);

    std::wstring err;
    if (!InjectLoader(pi, loader, err)) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return Fail(L"Could not load mods into the game:\n" + err);
    }
    rfa::Log("Game running (pid %lu)", pi.dwProcessId);

    DWORD exitCode = 0;
    if (rfa::IniInt(L"Launcher", L"WaitForExit", 1)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exitCode);
        rfa::Log("Game exited with code %lu", exitCode);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(exitCode);
}
