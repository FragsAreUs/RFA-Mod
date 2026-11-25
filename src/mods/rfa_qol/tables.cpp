// Tables from a folder: each `.xtbl` in `mods\tables\` is handed to the game in place of its table of that name, or as
// a new table, when the game reads it. A part of rfa_qol.dll; [Mods] tables=0 turns it off. The Blender add-on writes
// here: a built vehicle's vehicles.xtbl entry, and a vehicle's animations' (vehicle_interactions.xtbl,
// anim_set_filenames.xtbl, anim_files.xtbl).
//
// The game reads most of its tables once, while it starts, from its own copy of table.vpp_pc in memory: the table
// reader (0xC8E760) asks the copy for a table by its file name (0xC8FCD0, a method of the copy: true with the
// table's bytes, or false, and the reader opens the file by name instead), so overrides\ (which answers files the
// game opens) never sees them. The reader's one call of the lookup (0xC8E797) is pointed here: a table of the folder
// is answered with its bytes (kept for as long as the game runs: the game reads from them), any other goes on to the
// game's lookup (which the guerrilla_world mod may have hooked to hand over the Guerrilla world's tables).
//
// When the guerrilla_world mod is on and has a table of the name (mods\guerrilla_world\), that one is the game's: the
// tools that write here put their entries into that copy then (and remove theirs from here).
#define RFA_MOD_NAME "tables"
#include "rfa_mod.h"
#include "rfa_qol.h"

#include <cstring>
#include <string>
#include <vector>

namespace tables {

namespace {

constexpr DWORD kLookupCall = 0xC8E797;  // call 0xC8FCD0, in the table reader 0xC8E760 (mov ecx, the copy, just before)
constexpr DWORD kLookup = 0xC8FCD0;
constexpr DWORD kLargestTable = 64u << 20;

struct CachedTable {
    const void* data;
    DWORD size;
};
using LookupFn = bool(__fastcall*)(void* cache, void* unused, const char* name, CachedTable* out);

struct Table {
    std::string name;         // its file name, lower case
    std::vector<BYTE> bytes;  // with a 0 after it
    bool told = false;
};
std::vector<Table> g_tables;

std::wstring Folder() { return rfa::RootDir() + L"\\mods\\tables"; }

bool ReadWhole(const std::wstring& path, std::vector<BYTE>& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length{};
    bool ok = GetFileSizeEx(file, &length) && length.QuadPart > 0 && length.QuadPart <= static_cast<long long>(kLargestTable);
    if (ok) {
        out.resize(static_cast<size_t>(length.QuadPart));
        DWORD read = 0;
        ok = ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size();
    }
    CloseHandle(file);
    return ok;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

// Whether the guerrilla_world mod hands the game its own table of this name.
bool GuerrillaWorldHas(const std::wstring& file) {
    if (rfa::IniInt(L"Mods", L"guerrilla_world", 1) == 0) return false;
    if (GetFileAttributesW((rfa::RootDir() + L"\\mods\\guerrilla_world.dll").c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    return GetFileAttributesW((rfa::RootDir() + L"\\mods\\guerrilla_world\\" + file).c_str()) != INVALID_FILE_ATTRIBUTES;
}

void LoadFolder() {
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW((Folder() + L"\\*.xtbl").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (GuerrillaWorldHas(found.cFileName)) {
            rfa::Log("Tables: mods\\tables\\%ls is left out: the guerrilla_world mod hands the game its own (mods\\guerrilla_world\\%ls)",
                     found.cFileName, found.cFileName);
            continue;
        }
        char name[MAX_PATH] = {};
        if (!WideCharToMultiByte(CP_ACP, 0, found.cFileName, -1, name, sizeof(name), nullptr, nullptr)) continue;
        Table table;
        table.name = Lower(name);
        if (!ReadWhole(Folder() + L"\\" + found.cFileName, table.bytes)) {
            rfa::Log("Tables: mods\\tables\\%ls could not be read; the game keeps its own", found.cFileName);
            continue;
        }
        table.bytes.push_back(0);
        g_tables.push_back(std::move(table));
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

bool __fastcall Lookup(void* cache, void* unused, const char* name, CachedTable* out) {
    if (name && out) {
        const char* file = name;
        for (const char* c = name; *c; ++c)
            if (*c == '\\' || *c == '/') file = c + 1;
        const std::string key = Lower(file);
        for (Table& table : g_tables) {
            if (table.name != key) continue;
            out->data = table.bytes.data();
            out->size = static_cast<DWORD>(table.bytes.size() - 1);
            if (!table.told) {
                table.told = true;
                rfa::Log("Tables: the game reads mods\\tables\\%s (%lu bytes)", table.name.c_str(), out->size);
            }
            return true;
        }
    }
    return reinterpret_cast<LookupFn>(kLookup)(cache, unused, name, out);
}

}  // namespace

void Init() {
    LoadFolder();
    if (g_tables.empty()) return;
    const BYTE* call = reinterpret_cast<const BYTE*>(kLookupCall);
    if (call[0] != 0xE8 || kLookupCall + 5 + rfa::CodeValue(kLookupCall + 1) != kLookup) {
        rfa::Log("Tables: the game's table reader isn't as expected; the tables in mods\\tables aren't used");
        g_tables.clear();
        return;
    }
    BYTE code[5];
    rfa::MakeJump(code, call, reinterpret_cast<const void*>(&Lookup));
    code[0] = 0xE8;  // a call, as it was
    if (!rfa::PatchCode(const_cast<BYTE*>(call), code, sizeof(code))) {
        rfa::Log("Tables: couldn't change the game's table reader; the tables in mods\\tables aren't used");
        g_tables.clear();
        return;
    }
    rfa::Log("Tables: %u table(s) from mods\\tables are handed to the game when it reads them", static_cast<unsigned>(g_tables.size()));
}

}  // namespace tables
