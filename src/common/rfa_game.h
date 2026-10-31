// What mods share about the game's exe (RedFactionArmageddon_DX11.exe, 32-bit, no ASLR).
//
// The mods are written for the Steam "publicbeta" branch, build 1932726 (linked 2017-06-30): a
// "Release" (not "Release Final") build that still has the developer console. Every address below
// belongs to that exe, so mods check IsSupportedBuild() before touching game code. The default
// branch (build 1291145) has the same code at different addresses; it is detected only to say so
// in the log.
#pragma once
#include <windows.h>
#include <stddef.h>

namespace rfa::game {

constexpr DWORD kPublicBetaTimestamp = 0x59567F58;     // the exe's link time, 2017-06-30
constexpr DWORD kDefaultBranchTimestamp = 0x57B0E035;  // 2016-08-14

inline DWORD ExeTimestamp() {
    auto base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    return nt->FileHeader.TimeDateStamp;
}

inline bool IsSupportedBuild() { return ExeTimestamp() == kPublicBetaTimestamp; }

inline const char* BuildDescription() {
    switch (ExeTimestamp()) {
    case kPublicBetaTimestamp: return "Steam publicbeta branch (build 1932726), supported";
    case kDefaultBranchTimestamp:
        return "Steam default branch (build 1291145). The mods need the publicbeta branch: Steam > "
               "Properties > Game Versions & Betas > publicbeta. Until then they leave the game alone";
    default: return "unknown. The mods leave the game's code alone";
    }
}

template <typename T>
T At(DWORD address) { return reinterpret_cast<T>(static_cast<UINT_PTR>(address)); }

// The game embeds Lua 5.1 for its menus; these are its API functions, all cdecl. Found from Lua's
// base library (assert, type, tonumber, rawequal, loadstring, pcall), whose small functions call
// them, and from the game's own registration loop.
struct lua_State;
using lua_CFunction = int(__cdecl*)(lua_State*);
constexpr int LUA_GLOBALSINDEX = -10002;

inline const auto lua_gettop = At<int(__cdecl*)(lua_State*)>(0xD904A0);
inline const auto lua_tonumber = At<double(__cdecl*)(lua_State*, int)>(0xD90AE0);
inline const auto lua_tolstring = At<const char*(__cdecl*)(lua_State*, int, size_t*)>(0xD90BE0);
inline const auto lua_pushnumber = At<void(__cdecl*)(lua_State*, double)>(0xD90E80);
inline const auto lua_pushcclosure = At<void(__cdecl*)(lua_State*, lua_CFunction, int)>(0xD91080);
inline const auto lua_setfield = At<void(__cdecl*)(lua_State*, int, const char*)>(0xD91620);

inline void lua_register(lua_State* L, const char* name, lua_CFunction fn) {
    lua_pushcclosure(L, fn, 0);
    lua_setfield(L, LUA_GLOBALSINDEX, name);
}

// What the mod loader hooks.
constexpr DWORD kLuaLoadBuffer = 0xD95730;             // luaL_loadbuffer: compiles every Lua script
constexpr size_t kLuaLoadBufferPrologue = 6;           // push ebp / mov ebp,esp / sub esp,8
constexpr DWORD kRegisterGameLuaFunctions = 0x583AB0;  // adds the game's C functions to the menu Lua
constexpr size_t kRegisterGameLuaFunctionsPrologue = 9;  // push ebp / mov ebp,esp / sub esp,4F8h

// The game's file system ("cfile", from the exe's own strings). Every file the game reads, whether
// a loose file on disk or a file inside a .vpp_pc archive, is first looked up by name with
// FindFile, which tries each search path in turn: loose files, archives, virtual folders. Opening a
// file (cf_open, 0xC914E0), asking whether one exists (0xC92010) and the streaming of .str2_pc
// containers (0x912A70) all go through it, then read the file the way the result says.
struct FileFindResult {
    int source;          // +0x000: 0 = loose file on disk, 1 = inside an archive, 2 = virtual folder
    int pathType;        // +0x004: which kind of search path found it (0 for loose files)
    char name[256];      // +0x008: the name; for a loose file, the path it is opened with
    BYTE unknown[0x10];  // +0x108
    DWORD size;          // +0x118: file size in bytes
    void* archiveEntry;  // +0x11C: the archive's directory entry, for files inside an archive
};
static_assert(offsetof(FileFindResult, size) == 0x118 && sizeof(FileFindResult) == 0x120);

// FindFile(result, name, flag) looks a file up by name; returns true if found.
constexpr DWORD kFindFile = 0xC90960;
constexpr size_t kFindFilePrologue = 10;  // push ebp / mov ebp,esp / push -1 / push <handler table>
// FindLooseFile(result, path, pathType) fills in `result` for a file on disk: it opens `path` as
// given (relative to the game folder, or a full path), notes the size and closes it again. Only
// pathType 0 is used on PC.
inline const auto FindLooseFile = At<bool(__cdecl*)(FileFindResult*, const char*, int)>(0xCDCAF0);

}  // namespace rfa::game
