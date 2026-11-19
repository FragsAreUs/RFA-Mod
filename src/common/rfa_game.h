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
#include <string>

namespace rfa::game {

constexpr DWORD kPublicBetaTimestamp = 0x59567F58;     // the exe's link time, 2017-06-30
constexpr DWORD kDefaultBranchTimestamp = 0x57B0E035;  // 2016-08-14

inline DWORD ExeTimestamp() {
    auto base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    return nt->FileHeader.TimeDateStamp;
}

inline bool IsSupportedBuild() { return ExeTimestamp() == kPublicBetaTimestamp; }

// The launcher may start the game from its large-address-aware copy of the exe (LargeAddressAware=
// in rfa_mods.ini); it then names the installed exe, in the game folder, in this environment
// variable, which the game inherits.
constexpr wchar_t kInstalledExeVariable[] = L"RFA_GAME_EXE";

// The game's exe as installed: the one in the game folder, even when the game runs from the copy.
inline std::wstring InstalledExePath() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(kInstalledExeVariable, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

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
constexpr int LUA_REGISTRYINDEX = -10001;
constexpr int LUA_MULTRET = -1;

inline const auto lua_gettop = At<int(__cdecl*)(lua_State*)>(0xD904A0);
inline const auto lua_type = At<int(__cdecl*)(lua_State*, int)>(0xD90910);        // as `type` reads it: -1 none, 0 nil, 1 boolean, 3 number, 4 string
inline const auto lua_toboolean = At<int(__cdecl*)(lua_State*, int)>(0xD90B90);   // as `assert` reads it
inline const auto lua_pushnil = At<void(__cdecl*)(lua_State*)>(0xD90E50);          // as `tonumber` returns nil
inline const auto lua_tonumber = At<double(__cdecl*)(lua_State*, int)>(0xD90AE0);
inline const auto lua_tolstring = At<const char*(__cdecl*)(lua_State*, int, size_t*)>(0xD90BE0);
inline const auto lua_pushnumber = At<void(__cdecl*)(lua_State*, double)>(0xD90E80);
inline const auto lua_pushstring = At<void(__cdecl*)(lua_State*, const char*)>(0xD90F70);  // as `type` returns its name
inline const auto lua_pushcclosure = At<void(__cdecl*)(lua_State*, lua_CFunction, int)>(0xD91080);
inline const auto lua_pushboolean = At<void(__cdecl*)(lua_State*, int)>(0xD911A0);         // as `rawequal` returns
inline const auto lua_setfield = At<void(__cdecl*)(lua_State*, int, const char*)>(0xD91620);
inline const auto luaL_loadbuffer = At<int(__cdecl*)(lua_State*, const char*, size_t, const char*)>(0xD95730);
// These follow lapi.c's order, told apart by what each calls: index2adr (0xD905A0), luaS_newlstr
// (0xD9E1F0), luaV_gettable (0xD99C00), luaH_getnum (0xD9D790), luaH_next (0xD9CE50), luaD_pcall (0xD947D0).
inline const auto lua_settop = At<void(__cdecl*)(lua_State*, int)>(0xD904C0);              // the one after lua_gettop: nils up to the new top
inline const auto lua_getfield = At<void(__cdecl*)(lua_State*, int, const char*)>(0xD91270);  // index2adr, luaS_newlstr, luaV_gettable
inline const auto lua_rawgeti = At<void(__cdecl*)(lua_State*, int, int)>(0xD91370);        // index2adr, luaH_getnum
inline const auto lua_pcall = At<int(__cdecl*)(lua_State*, int, int, int)>(0xD91A90);      // index2adr (the error handler), luaD_pcall
inline const auto lua_next = At<int(__cdecl*)(lua_State*, int)>(0xD91D30);                 // index2adr, luaH_next

// lua_pop is a macro in Lua 5.1, not a function the game has.
inline void lua_pop(lua_State* L, int n) { lua_settop(L, -n - 1); }

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
