// What Red Faction: Guerrilla's people need from Armageddon's engine that it doesn't do itself. The
// people are made from the player's own copy of Guerrilla (never shipped) by GuerrillaBuilder.exe: see
// RfaFormats\Guerrilla\GuerrillaPeople.cs. Armageddon's character code is Guerrilla's with fields
// added (its character table reader, 0x8860D0 and 0x886840, is Guerrilla's load_human_info_block),
// and it takes each character's bones from that character's own skeleton, so Guerrilla's people keep
// their own meshes, skeletons and animations. So far the mod gives them:
//
// Their skeletons: the game loads every skeleton its archives list while it starts (0x973460), and when
// it reads the character table it takes each character's from those, by name. Guerrilla's people's
// (greene.rig_pc, ...: none of them Armageddon's) are new files GuerrillaBuilder.exe writes into
// overrides\build\pc\cache\, in no archive's list, named in mods\guerrilla_people\rigs.txt; the mod has
// the game load each of those by name (0x973550) just before it reads its character table (0x8860D0,
// cdecl, three arguments: the first time it is called, for character.xtbl).
//
// The world's spawn resource: where a level's spawn resource is in force (the pools' state, 0x19734CC,
// isn't 0: Wrecking Crew levels, our worlds among them), the game makes only the people the resource
// holds and loads no other (its human create, 0x762D50, gives up on one that isn't loaded or on its
// way: 0x762230, 0x764ED0). The Guerrilla world's zones name its own resource, rfg_Default, which
// lists every person GuerrillaBuilder.exe made, so the game loads them itself (the mod no longer
// holds them in the level's category, 0x761D20(8, 3, ...), as it once did). Where the game would load
// its fallback resource instead, Armageddon's "Default", the mod gives it rfg_Default (see "The level's
// spawn resource" below). It writes in the log which of mods\guerrilla_people\people.txt are loaded,
// 5 and 30 seconds after the world starts.
//
// Their animations: Guerrilla kept its people's animations in its animations archive (anims.vpp_pc),
// which its game loads whole while it starts and keeps for good (anim_load_packfile: each animation
// overlaid on its place in the animation library with "keep" set, which an unload passes over). The
// people's animations are many characters' at once (every set builds on Male), and an animation packed
// in a character's own container belongs to whichever container loaded first: the next one's copy is
// passed over, and the first one's leaving takes it away from all (the container callbacks, 0x95DC00
// and 0x95DC90). So, as Guerrilla, they are loaded once and kept: GuerrillaBuilder.exe writes them into
// mods\guerrilla_people\animations\ (rfgp_<name>.anim_pc, Armageddon's packing), and once Armageddon
// has loaded its own archive (0x95E180, cdecl, the archive's path, from the start-up's anim_preload)
// the mod reads each one and overlays it the same way: its place by name (0xD8C2C0), or a new one
// (0xD875F0; the library holds 8,192 with guerrilla_vehicles), then 0xD8C0F0 with "keep" set.
//
// Room for the animations' names: the game keeps every animation's name ("x.anim", for each entry
// of anim_files.xtbl, and the sets', states' and actions' names) in one string pool, "Anim strings"
// (0x2684EC8), which a start-up initializer (0xF4A7E0, from the C runtime's list, after the mods are
// loaded) gives 128 KB of the game's own memory (0x25FC260). Armageddon's, Guerrilla's vehicles' and
// people's 4,679 animations' names alone take 126 KB, and one more than it holds stops the game while
// it starts ("Length of string pool exceeded"). The pool is one of the game's memory pools (its
// memory's start at +0x40, its size at +0x44, how much is used at +0x4C; every string it hands out is
// an address in that memory, and nothing else names the old memory), so the mod lets the initializer
// run and then gives the pool 512 KB of its own, with what is in it already. No code is changed. It
// does this whatever [GuerrillaPeople] Enabled= says: the animation lists hold the people's animations
// whether or not the rest of the mod is on.
//
// Room for Guerrilla's patrols: the game keeps every patrol route the loaded zones place (object_patrol)
// in an object pool its start-up makes (0xF2E480 → 0x7F9980: the pool set up, 0x71C5D0, a memory pool on
// the buffer inside the pool object, 0x428240, and the pool's fill, 0x7FC8D0, carving each slot's object,
// handle and byte out of it), of 24; Guerrilla's holds 72 (object_pool<object_patrol,72,1>), and its zones
// place up to 29 in one 3 × 3 block. Nothing else is sized by that count: the pool is reached only through
// its own object (0x20F3B10: how many it holds, a word at +0xA; its free list at +0xC; a byte a slot at
// +0x18, 0xFF while free). So the mod replaces the constructor with one doing the same on a buffer of its
// own, for Guerrilla's 72, after checking the constructor's buffer size and count are the game's (nothing
// changes if not). Always, whatever [GuerrillaPeople] Enabled= says: the world's zones place them.
// The world also keeps a list of each type's objects (0x1870630 +0x68, 12 bytes a type: its objects, how many it
// holds, how many it has; the zone loader 0x6D4880 adds each object made, saying "!type_objects[stub->obj_type]
// .full()" when the list is full and adding it anyway), each list as big as the count the type registers at
// start-up (0xF2E4A0 → 0x4FAD90, patrols' 24), not its pool. So the mod also replaces that registration with the
// same call (the same callbacks and name) giving Guerrilla's 72, after checking each value it pushes is the game's.
//
// Guerrilla's sides: both games keep the teams by the same numbers (0 Guerrilla's Red Faction, 1 its
// EDF, Armageddon's Legion, 2 civilians, 3 Marauders; Armageddon adds 4, its cultists) and how each
// team takes each other in a 5 × 5 table (0x21DC8D0: 0 hostile, 1 unfriendly, 2 neutral, 3 friendly,
// 4 allied), which a level's start resets to Armageddon's (0x8A0350, from 0xFCA1A8). Armageddon's make
// the Marauders the player's friends and civilians the EDF's enemies; Guerrilla's (its
// Default_human_team_dispositions): the EDF and the Marauders hostile to the guerrillas, the civilians
// friendly to them and unfriendly to the EDF, the Marauders hostile to everyone. In our worlds the mod
// writes Guerrilla's after each reset (through the game's own setter, 0x8A03A0, cdecl: the team, the
// other team, the disposition, both ways or not); the cultists keep Armageddon's.
#define RFA_MOD_NAME "guerrilla_people"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "people_api.h"
#include "rfa_game.h"
#include "ambient.h"
#include "ambush.h"
#include "story.h"
#include "actions.h"
#include "guards.h"
#include "squad_levels.h"
#include "voices.h"
#include <intrin.h>
#include <malloc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

// ---- Our worlds

// The level's world (its territory), as guerrilla_world reads it.
const char* WorldName() { return reinterpret_cast<const char*>(0x1870630 + 0x29EA); }
std::vector<std::string> g_worlds;  // [GuerrillaPeople] Worlds=

std::vector<std::string> SplitList(const std::wstring& text) {
    std::vector<std::string> names;
    std::string current;
    for (wchar_t c : text) {
        if (c == L',' || c == L' ' || c == L'\t') {
            if (!current.empty()) names.push_back(current);
            current.clear();
        } else {
            current += static_cast<char>(c);
        }
    }
    if (!current.empty()) names.push_back(current);
    return names;
}

bool InOurWorld() {
    const char* world = WorldName();
    for (const std::string& name : g_worlds)
        if (_stricmp(world, name.c_str()) == 0) return true;
    return false;
}

// One of the GuerrillaBuilder.exe lists, one name a line.
std::vector<std::string> ReadList(const wchar_t* file) {
    std::vector<std::string> names;
    std::ifstream list(rfa::RootDir() + L"\\mods\\guerrilla_people\\" + file);
    for (std::string line; std::getline(list, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line.size() < 64) names.push_back(line);
    }
    return names;
}

// ---- Their skeletons

constexpr DWORD kReadCharacters = 0x8860D0;
constexpr size_t kReadCharactersPrologue = 9;  // push ebp / mov ebp,esp / sub esp,10Ch
using ReadCharactersFn = void(__cdecl*)(int which, void* table, int dlc);
ReadCharactersFn g_readCharacters;

// Loads one skeleton by its file's name (cdecl; nothing if it is loaded already), and finds a loaded one (-1 if none).
auto const LoadSkeleton = reinterpret_cast<void(__cdecl*)(const char* name)>(0x973550);
auto const FindSkeleton = reinterpret_cast<int(__cdecl*)(const char* name)>(0xD8A040);
std::vector<std::string> g_skeletons;  // mods\guerrilla_people\rigs.txt
bool g_skeletonsLoaded = false;
void LogRigPool();  // (below, with the skeletons' pool)
void MasonsTableRead();  // (below, with Mason's Nano Forge light)
void LogCharacterNames();  // (below, with the characters' strings)

void __cdecl ReadCharacters(int which, void* table, int dlc) {
    if (!g_skeletonsLoaded) {
        g_skeletonsLoaded = true;
        for (const std::string& skeleton : g_skeletons) {
            LoadSkeleton(skeleton.c_str());
            if (FindSkeleton(skeleton.c_str()) >= 0) rfa::LogDetail("Skeletons: %s loaded", skeleton.c_str());
            else rfa::Log("Skeletons: %s could not be loaded (a character that names it will stop the game)", skeleton.c_str());
        }
        LogRigPool();
    }
    g_readCharacters(which, table, dlc);
    LogCharacterNames();
    MasonsTableRead();
}

// ---- Room for the skeletons
//
// Every skeleton the game loads (its archives' own while it starts, Guerrilla's vehicles' and people's
// after them) goes into one memory pool, "rigs" (0x26AFB48), which the game's start-up (0xA46BE9) makes
// 0x90000 bytes (576 KB) of its own memory through the game's memory pools' init (0x4280C0, a method:
// the memory, its size, the name, 4, 8). Armageddon's skeletons and Guerrilla's vehicles' and people's
// filled it to within a few bytes, and one more stops the game while it starts ("Failed aligned alloc
// ... in rigs pool"). So when the game sets that pool up the mod gives it 2 MB of its own instead (the
// pool is empty then, so nothing moves; no code is changed), whatever [GuerrillaPeople] Enabled= says, as
// Guerrilla's vehicles' skeletons need the room too; it logs how full it is once the people's are loaded.
constexpr DWORD kPoolInit = 0x4280C0;
constexpr size_t kPoolInitPrologue = 7;  // push ebp / mov ebp,esp / push ecx / mov [ebp-4],ecx
using PoolInitFn = void(__fastcall*)(void* pool, void* unused, void* memory, DWORD size, const char* name, int flag, int alignment);
PoolInitFn g_poolInit;
BYTE* const kRigPool = reinterpret_cast<BYTE*>(0x26AFB48);
constexpr DWORD kGameRigPoolBytes = 0x90000;  // its own: 576 KB
constexpr DWORD kRigPoolBytes = 0x200000;     // ours: 2 MB
alignas(16) BYTE g_rigPool[kRigPoolBytes];

// The characters' strings: the character table's reader (0x8860D0) keeps every character's names (its
// name, nickname, display name, outfit image, model, skeleton, morph, light tag and each prop's names, each
// once) in a string pool, "Human info strings" (0x21DC118), which it sets up on 0x1838 bytes (6,200) it takes
// the first time (kept at 0x21B74A4) through the same memory pools' init. Armageddon's and Guerrilla's 152
// characters' come to about 10.6 KB; past its end the pool says "Length of string pool exceeded" and hands
// out nothing, and the reader's next compare with it stops the game (C0000417). So when it is set up the mod
// gives it 64 KB of its own (the pool is empty then; the game's 6,200 bytes stay taken, unused; nothing else
// names them, and nothing frees them).
BYTE* const kHumanInfoStrings = reinterpret_cast<BYTE*>(0x21DC118);
constexpr DWORD kGameHumanInfoBytes = 0x1838;   // its own: 6,200 bytes
constexpr DWORD kHumanInfoBytes = 0x10000;      // ours: 64 KB
alignas(16) BYTE g_humanInfoStrings[kHumanInfoBytes];

// The squads' and spawn resources' names, the same way: the squad table's reader (0x50C000, then 0x50BA30 an
// entry) keeps each squad's Name in "Squad Def String Pool" (0x12F2E38) on 0x500 bytes (1,280) it takes once
// (kept at 0x12EFED0), and the spawn resources' reader (0x75F190, then 0x75ED30) each resource's Name in
// "Spawn Resource Data" (0x19706E0) on 0x400 (1,024; kept at 0x1969950). Armageddon's 51 squads and Guerrilla's
// 92 need about 2.1 KB, the 83 resources about 1.1 KB, so they get 16 KB and 8 KB of their own (the people
// build checks its tables' names fit them).
BYTE* const kSquadNames = reinterpret_cast<BYTE*>(0x12F2E38);
constexpr DWORD kGameSquadNameBytes = 0x500;    // its own: 1,280 bytes
constexpr DWORD kSquadNameBytes = 0x4000;       // ours: 16 KB
alignas(16) BYTE g_squadNames[kSquadNameBytes];
BYTE* const kResourceNames = reinterpret_cast<BYTE*>(0x19706E0);
constexpr DWORD kGameResourceNameBytes = 0x400; // its own: 1,024 bytes
constexpr DWORD kResourceNameBytes = 0x2000;    // ours: 8 KB
alignas(16) BYTE g_resourceNames[kResourceNameBytes];

void __fastcall PoolInit(void* pool, void* unused, void* memory, DWORD size, const char* name, int flag, int alignment) {
    if (pool == kRigPool && size == kGameRigPoolBytes) {
        g_poolInit(pool, unused, g_rigPool, kRigPoolBytes, name, flag, alignment);
        rfa::Log("Skeletons: their pool holds %lu KB (its own %lu KB)", kRigPoolBytes / 1024, kGameRigPoolBytes / 1024);
        return;
    }
    if (pool == kHumanInfoStrings && size == kGameHumanInfoBytes) {
        g_poolInit(pool, unused, g_humanInfoStrings, kHumanInfoBytes, name, flag, alignment);
        rfa::Log("Characters' names: their pool holds %lu KB (its own %lu bytes)", kHumanInfoBytes / 1024, kGameHumanInfoBytes);
        return;
    }
    if (pool == kSquadNames && size == kGameSquadNameBytes) {
        g_poolInit(pool, unused, g_squadNames, kSquadNameBytes, name, flag, alignment);
        rfa::Log("Squads' names: their pool holds %lu KB (its own %lu bytes)", kSquadNameBytes / 1024, kGameSquadNameBytes);
        return;
    }
    if (pool == kResourceNames && size == kGameResourceNameBytes) {
        g_poolInit(pool, unused, g_resourceNames, kResourceNameBytes, name, flag, alignment);
        rfa::Log("Spawn resources' names: their pool holds %lu KB (its own %lu bytes)", kResourceNameBytes / 1024, kGameResourceNameBytes);
        return;
    }
    g_poolInit(pool, unused, memory, size, name, flag, alignment);
}

void LogCharacterNames() {
    rfa::Log("Characters' names: %lu of their pool's %lu bytes used", *reinterpret_cast<const DWORD*>(kHumanInfoStrings + 0x4C),
             *reinterpret_cast<const DWORD*>(kHumanInfoStrings + 0x44));
}

void LogRigPool() {
    rfa::Log("Skeletons: %lu of their pool's %lu bytes used", *reinterpret_cast<const DWORD*>(kRigPool + 0x4C), *reinterpret_cast<const DWORD*>(kRigPool + 0x44));
}

// ---- Room for the animations' names

constexpr DWORD kAnimStringsInit = 0xF4A7E0;
constexpr size_t kAnimStringsInitPrologue = 8;  // push ebp / mov ebp,esp / push 1002B4Ch ("Anim strings")
using AnimStringsInitFn = void(__cdecl*)();
AnimStringsInitFn g_animStringsInit;

BYTE* const kAnimStrings = reinterpret_cast<BYTE*>(0x2684EC8);  // the pool
constexpr DWORD kPoolMemory = 0x40, kPoolSize = 0x44, kPoolUsed = 0x4C;
constexpr DWORD kGameAnimStringBytes = 0x20000;  // its own: 128 KB
constexpr DWORD kAnimStringBytes = 0x80000;      // ours: 512 KB
alignas(16) char g_animStrings[kAnimStringBytes];

// ---- Room for Guerrilla's patrols (see the top)

using PoolSetUpFn = void(__fastcall*)(void* pool, void* unused);
using MempoolMakeFn = void*(__fastcall*)(void* mempool, void* unused, void* buffer, DWORD size, const char* name, int one, int zero);
using MempoolEndFn = void(__fastcall*)(void* mempool, void* unused);
using PoolFillFn = void(__fastcall*)(void* pool, void* unused, void* mempool, int count);
auto const PoolSetUp = reinterpret_cast<PoolSetUpFn>(0x71C5D0);
auto const MempoolMake = reinterpret_cast<MempoolMakeFn>(0x428240);
auto const MempoolEnd = reinterpret_cast<MempoolEndFn>(0x427F40);
auto const FillPatrolPool = reinterpret_cast<PoolFillFn>(0x7FC8D0);
constexpr DWORD kPoolName = 0xF7101C;  // "object pool", the name the game's constructors give

// The constructor, and the values in it that say it is as expected: it pushes the buffer's size (4
// bytes: 24 × (the patrol's 0xA8 + 5) + 5) and the count (a one-byte push).
constexpr DWORD kPatrolPoolMake = 0x7F9980, kPatrolBufferSize = 0x7F999B, kPatrolCount = 0x7F99AF;
constexpr DWORD kPatrolSize = 0xA8, kGamePatrolBuffer = 0x103D;
constexpr int kGamePatrols = 24, kPatrols = 72;  // Guerrilla's 72

// A slot's bytes: its object, its handle (4) and its byte; then room for the memory pool's alignment.
constexpr DWORD PoolBytes(int count, DWORD size) { return count * (size + 5) + 64; }
alignas(16) BYTE g_patrolBuffer[PoolBytes(kPatrols, kPatrolSize)];

// What the game's constructor does, on the mod's buffer and with Guerrilla's count.
void* __fastcall MakePatrolPool(void* pool, void*) {
    PoolSetUp(pool, nullptr);
    alignas(16) BYTE mempool[0x60];  // the game's is 0x58 bytes on its stack
    MempoolMake(mempool, nullptr, g_patrolBuffer, sizeof(g_patrolBuffer), reinterpret_cast<const char*>(kPoolName), 1, 0);
    FillPatrolPool(pool, nullptr, mempool, kPatrols);
    MempoolEnd(mempool, nullptr);
    return pool;
}

// The patrols' registration (0xF2E4A0): it pushes nine values and calls 0x4FAD90 (thiscall) on the type's entry.
constexpr DWORD kPatrolRegistration = 0xF2E4A0, kRegisterType = 0x4FAD90, kPatrolTypeEntry = 0x20F3B04;
// Each push's value and where it is in the registration's code (push imm32 at +3, +10, ...; push imm8 at +8).
struct PushedValue { DWORD at; DWORD value; };
constexpr PushedValue kPatrolPushes[] = {
    {0xF2E4A4, 0x7F7E30}, {0xF2E4AB, 0x1070}, {0xF2E4B0, 0x7F7E80}, {0xF2E4B5, 0x7F7E60}, {0xF2E4BA, 0x813EA0},
    {0xF2E4BF, 0x7F7E40}, {0xF2E4C4, 0x7F7E10}, {0xF2E4C9, 0xFBE430}, {0xF2E4CE, kPatrolTypeEntry},
};
constexpr DWORD kPatrolRegisteredCount = 0xF2E4A9;  // push 18h: the count, one byte
using RegisterTypeFn = void(__fastcall*)(void* entry, void* unused, const char* name, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e,
                                         DWORD bytes, DWORD count, DWORD f);
auto const RegisterType = reinterpret_cast<RegisterTypeFn>(kRegisterType);

// The game's registration, with Guerrilla's count (and the bytes it books scaled to it).
void __cdecl RegisterPatrols() {
    RegisterType(reinterpret_cast<void*>(kPatrolTypeEntry), nullptr, reinterpret_cast<const char*>(0xFBE430), 0x7F7E10, 0x7F7E40, 0x813EA0,
                 0x7F7E60, 0x7F7E80, 0x1070 * kPatrols / kGamePatrols, kPatrols, 0x7F7E30);
}

bool PatrolRegistrationAsExpected() {
    for (const PushedValue& push : kPatrolPushes)
        if (rfa::CodeValue(push.at) != push.value) return false;
    return *reinterpret_cast<const BYTE*>(kPatrolRegisteredCount) == kGamePatrols;
}

void MakeRoomForPatrols() {
    // Both or neither: the pool and the world's list of patrols.
    if (!PatrolRegistrationAsExpected()) {
        rfa::Log("Patrols: the type's registration isn't as expected; the game keeps its %d", kGamePatrols);
        return;
    }
    if (rfa::CodeValue(kPatrolBufferSize) == kGamePatrolBuffer && *reinterpret_cast<const BYTE*>(kPatrolCount) == kGamePatrols &&
        rfa::ReplaceFunction(kPatrolPoolMake, reinterpret_cast<void*>(&MakePatrolPool))) {
        if (rfa::ReplaceFunction(kPatrolRegistration, reinterpret_cast<void*>(&RegisterPatrols)))
            rfa::Log("Patrols: the game holds %d at once (its own %d), as Guerrilla, in its pool and the world's list", kPatrols, kGamePatrols);
        else rfa::Log("Patrols: the pool holds %d, but the registration couldn't be replaced; the world's list keeps %d", kPatrols, kGamePatrols);
    }
    else rfa::Log("Patrols: the pool's constructor isn't as expected; the game keeps its %d (the world places more than fit in some places)", kGamePatrols);
}

// ---- Room for Guerrilla's squads' members (the user, 2026-11-06: "Yes, raise it")
//
// Every squad's leader and member is a record of (character, loadout, variant) the squad table's reader shares between
// squads (0x50BCE0 → 0x50BDD0: found in the list 0x12EFF0C, else a new one from its pool, 0x50C440). The pool is made
// at start-up (0xF1D740 → 0x50CB70, as the patrols': the pool set up, a memory pool on the buffer inside the pool
// object, then its fill, 0x4D64A0, carving 12-byte slots) for 64; with none left the member, so its squad, fails, and
// every later squad in the table with it. Armageddon's own squads use 43; Guerrilla's district squads bring each
// civilian and each guerrilla look with its weapons (95 in all with Armageddon's). So the mod builds it on a buffer of
// its own for 256, after checking the constructor's buffer size and count are the game's. Nothing else names the
// count (the pool object 0x12EFEF0 is reached through its initializer, its client 0x12EFED4/0x12EFF0C and its
// destructor only). Always, whatever [GuerrillaPeople] Enabled= says: the squad table holds Guerrilla's squads.
auto const FillMemberPool = reinterpret_cast<PoolFillFn>(0x4D64A0);
constexpr DWORD kMemberPoolMake = 0x50CB70, kMemberBufferSize = 0x50CB8B, kMemberCount = 0x50CB9F;
constexpr DWORD kMemberSize = 12, kGameMemberBuffer = 0x445;
constexpr int kGameMembers = 64, kMembers = 256;
alignas(16) BYTE g_memberBuffer[PoolBytes(kMembers, kMemberSize)];

void* __fastcall MakeMemberPool(void* pool, void*) {
    PoolSetUp(pool, nullptr);
    alignas(16) BYTE mempool[0x60];  // the game's is 0x58 bytes on its stack
    MempoolMake(mempool, nullptr, g_memberBuffer, sizeof(g_memberBuffer), reinterpret_cast<const char*>(kPoolName), 1, 0);
    FillMemberPool(pool, nullptr, mempool, kMembers);
    MempoolEnd(mempool, nullptr);
    return pool;
}

void MakeRoomForSquadMembers() {
    if (rfa::CodeValue(kMemberBufferSize) == kGameMemberBuffer && *reinterpret_cast<const BYTE*>(kMemberCount) == kGameMembers &&
        rfa::ReplaceFunction(kMemberPoolMake, reinterpret_cast<void*>(&MakeMemberPool)))
        rfa::Log("Squad members: the game holds %d (its own %d)", kMembers, kGameMembers);
    else rfa::Log("Squad members: the pool's constructor isn't as expected; the game keeps its %d (squads past them aren't read)", kGameMembers);
}

// ---- Room for Guerrilla's squad loadouts (the user's Round 1, 2026-11-06: marauder_squad_6 "invalid")
//
// The game keeps squad_loadout.xtbl's entries in a fixed table at 0x12F33A0 (its items' pointer, capacity and count,
// then 36 entries of 0x1C bytes in place), set up at start-up by 0x50D5B0 (from the initializer 0xF1D850). The reader
// (0x50D160 → 0x50CF90, added by 0x7642F0) drops every entry past the 36th without a word (the table can't grow:
// 0x7476F0), and a squad member naming a dropped loadout fails its whole squad (0x50BD6B), so a zone naming the squad
// gets "invalid squad definition". Armageddon's own loadouts take 31; Guerrilla's 20 make 51. Everything reaches the
// entries through the items' pointer (0x50D530) and the count (0x55B3D0), and the exit's clean-up (0xF62490 →
// 0x926130) does nothing, so the mod builds the table on a buffer of its own for 128, after checking the constructor
// is the game's. Always, whatever [GuerrillaPeople] Enabled= says: the loadout table holds Guerrilla's loadouts.
constexpr DWORD kLoadoutTableMake = 0x50D5B0;
constexpr DWORD kLoadoutMakePush = 0x50D5B8;      // push 50D280h: each entry's constructor
constexpr DWORD kLoadoutCountPushes[] = {0x50D5BD, 0x50D5CF};  // push 24h, twice: the count
constexpr DWORD kLoadoutSizePush = 0x50D5BF;      // push 1Ch: an entry's bytes
constexpr DWORD kLoadoutSize = 0x1C;
constexpr int kGameLoadouts = 36, kLoadouts = 128;
using LoadoutMakeFn = void*(__fastcall*)(void* loadout, void*);
using ArraySetUpFn = void(__fastcall*)(void* array, void*, void* items, int capacity, int count);
auto const LoadoutMake = reinterpret_cast<LoadoutMakeFn>(0x50D280);
auto const ArraySetUp = reinterpret_cast<ArraySetUpFn>(0x47CE00);
alignas(16) BYTE g_loadoutBuffer[kLoadouts * kLoadoutSize];

void* __fastcall MakeLoadoutTable(void* table, void*) {
    for (int i = 0; i < kLoadouts; ++i) LoadoutMake(g_loadoutBuffer + i * kLoadoutSize, nullptr);
    ArraySetUp(table, nullptr, g_loadoutBuffer, kLoadouts, 0);
    return table;
}

bool LoadoutTableAsExpected() {
    if (rfa::CodeValue(kLoadoutMakePush) != 0x50D280) return false;
    for (DWORD at : kLoadoutCountPushes)
        if (*reinterpret_cast<const BYTE*>(at) != kGameLoadouts) return false;
    return *reinterpret_cast<const BYTE*>(kLoadoutSizePush) == kLoadoutSize;
}

void MakeRoomForLoadouts() {
    if (LoadoutTableAsExpected() && rfa::ReplaceFunction(kLoadoutTableMake, reinterpret_cast<void*>(&MakeLoadoutTable)))
        rfa::Log("Squad loadouts: the game holds %d (its own %d)", kLoadouts, kGameLoadouts);
    else rfa::Log("Squad loadouts: the table's constructor isn't as expected; the game keeps its %d (loadouts past them are dropped, and squads naming them aren't read)", kGameLoadouts);
}

// ---- Room for the spawn resources (the user, 2026-11-11: "yes you can raise the limit if needed")
//
// spawn_resource.xtbl's reader (0x75F190 → 0x75EA90, each entry) takes a 0x1B0-byte entry from the table's pool
// (0x1969960, through its pointer at 0x196997C = the pool's +0x1C, which its set-up 0x71C5D0 points at itself;
// 0x75F390 → 0x75F680: none when the free list's count, a word at +0xE, is 0, and then the entry is dropped without a
// word) and files it by name in a hash table (0x1970740: a count byte at +4, 64 bucket bytes from +5, a free list of
// 64 one-byte slots; insert 0x516070, find 0x5160A0, clear 0x972F30, remove 0x75F5A0; FindSpawnResource 0x75EC70 asks
// it, then the pool for the slot). Both hold 64: the pool's start-up (0xF293F0 → 0x75F960: the pool set up, a memory
// pool on the 0x6D45 bytes inside it from +0x30, then 0x75F9B0(that, 64): the slots' words +0x14, the entries +0x10,
// a byte a slot +0x18, the count at +0xA) and the table's code (its buckets and hash, 0x40, built in). Armageddon's
// own table has 75 entries, the Guerrilla world adds 8 (83): those that read become entries until the 64 are taken,
// and rfg_eos, rfg_oasis and rfg_parker, its last three, never were (a level started in Eos or Parker then had no
// resource in force: Darius as the player, Armageddon's "Default" asked for the level, its people). So the mod makes
// the pool on a buffer of its own for kSpawnResources and keeps the names itself: its own table answers insert, find,
// clear and remove for 0x1970740 (the four are shared with other tables, so any other is passed to the game's code),
// names as the game's, letters' case aside (its hash lowers each letter, 0xDAD89A), each naming the entry's slot.
// Nothing else reads the table (its 7 uses are those calls, its constructor and destructor), and nothing else holds
// 64 for it (the slots are words). Sized for what is still to come: Armageddon's 75 and the world's 8 now; Guerrilla's
// own table has 23 (Default and a mission's each), and more districts' and activities' versions: 256, 0x1B5 bytes a
// slot (112 KB). All or nothing: every value checked first, the four hooks in before the pool is made bigger, and if
// one isn't the hooks pass everything to the game's code. Always, whatever [GuerrillaPeople] Enabled= says.
constexpr DWORD kResourcePoolMake = 0x75F960, kResourceBufferSize = 0x75F97B, kResourceCount = 0x75F98F, kResourcePoolName = 0x75F976;
constexpr DWORD kGameResourceBuffer = 0x6D45, kResourceEntry = 0x1B0;
constexpr int kGameSpawnResources = 64, kSpawnResources = 256;
constexpr DWORD kResourceTable = 0x1970740;
alignas(16) BYTE g_resourceBuffer[PoolBytes(kSpawnResources, kResourceEntry)];
using ResourcePoolSizeFn = void(__fastcall*)(void* pool, void* unused, void* mempool, int count);
auto const SizeResourcePool = reinterpret_cast<ResourcePoolSizeFn>(0x75F9B0);

void* __fastcall MakeResourcePool(void* pool, void*) {
    PoolSetUp(pool, nullptr);
    alignas(16) BYTE mempool[0x60];  // the game's is 0x58 bytes on its stack
    MempoolMake(mempool, nullptr, g_resourceBuffer, sizeof(g_resourceBuffer), reinterpret_cast<const char*>(kPoolName), 1, 0);
    SizeResourcePool(pool, nullptr, mempool, kSpawnResources);
    MempoolEnd(mempool, nullptr);
    return pool;
}

// The names' table: lower-cased name → the entry's slot.
std::unordered_map<std::string, int> g_resourceSlots;
bool g_ownResourceTable = false;  // all four hooks in: our table answers for 0x1970740

std::string LowerName(const char* name) {
    std::string lower = name ? name : "";
    for (char& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return lower;
}

using TableInsertFn = bool(__fastcall*)(void* table, void* unused, const char* key, const int* slot);
using TableFindFn = bool(__fastcall*)(void* table, void* unused, const char* key, int* slot);
using TableClearFn = void(__fastcall*)(void* table, void* unused);
constexpr DWORD kTableInsert = 0x516070, kTableFind = 0x5160A0, kTableClear = 0x972F30, kResourceTableRemove = 0x75F5A0;
constexpr size_t kTableInsertPrologue = 7;  // push ebp / mov ebp,esp / push ecx / mov [ebp-4],ecx
constexpr size_t kTableFindPrologue = 6;    // push ebp / mov ebp,esp / sub esp,8
constexpr size_t kTableClearPrologue = 6;   // push ebp / mov ebp,esp / sub esp,8
TableInsertFn g_tableInsert = nullptr;
TableFindFn g_tableFind = nullptr;
TableClearFn g_tableClear = nullptr;

bool __fastcall TableInsert(void* table, void* unused, const char* key, const int* slot) {
    if (!g_ownResourceTable || table != reinterpret_cast<void*>(kResourceTable)) return g_tableInsert(table, unused, key, slot);
    if (!key || !slot) return false;
    g_resourceSlots[LowerName(key)] = *slot;
    return true;
}

bool __fastcall TableFind(void* table, void* unused, const char* key, int* slot) {
    if (!g_ownResourceTable || table != reinterpret_cast<void*>(kResourceTable)) return g_tableFind(table, unused, key, slot);
    if (!key) return false;
    auto found = g_resourceSlots.find(LowerName(key));
    if (found == g_resourceSlots.end()) return false;
    if (slot) *slot = found->second;
    return true;
}

void __fastcall TableClear(void* table, void* unused) {
    if (!g_ownResourceTable || table != reinterpret_cast<void*>(kResourceTable)) {
        g_tableClear(table, unused);
        return;
    }
    g_resourceSlots.clear();
}

// The table's remove (only this table's: replaced whole; thiscall, the name).
bool __fastcall ResourceTableRemove(void* table, void* unused, const char* key) {
    (void)table;
    (void)unused;
    return key && g_resourceSlots.erase(LowerName(key)) > 0;
}

bool ResourcePoolAsExpected() {
    return rfa::CodeValue(kResourceBufferSize) == kGameResourceBuffer && *reinterpret_cast<const BYTE*>(kResourceCount) == kGameSpawnResources &&
           rfa::CodeValue(kResourcePoolName) == kPoolName &&
           *reinterpret_cast<const BYTE*>(kTableFind + 9) == 0x6A && *reinterpret_cast<const BYTE*>(kTableFind + 0xA) == kGameSpawnResources &&  // push 40h
           *reinterpret_cast<const BYTE*>(kResourceTableRemove + 0xA) == 0x6A && *reinterpret_cast<const BYTE*>(kResourceTableRemove + 0xB) == kGameSpawnResources;
}

void MakeRoomForSpawnResources() {
    if (!ResourcePoolAsExpected()) {
        rfa::Log("Spawn resources: the table's code isn't as expected; the game keeps its %d (entries past them are dropped)", kGameSpawnResources);
        return;
    }
    g_tableInsert = reinterpret_cast<TableInsertFn>(rfa::HookFunction(kTableInsert, kTableInsertPrologue, reinterpret_cast<void*>(&TableInsert)));
    g_tableFind = reinterpret_cast<TableFindFn>(rfa::HookFunction(kTableFind, kTableFindPrologue, reinterpret_cast<void*>(&TableFind)));
    g_tableClear = reinterpret_cast<TableClearFn>(rfa::HookFunction(kTableClear, kTableClearPrologue, reinterpret_cast<void*>(&TableClear)));
    if (!g_tableInsert || !g_tableFind || !g_tableClear) {
        rfa::Log("Spawn resources: could not hook the names' table; the game keeps its %d", kGameSpawnResources);
        return;  // the hooks that are in pass everything to the game's code
    }
    if (!rfa::ReplaceFunction(kResourceTableRemove, reinterpret_cast<void*>(&ResourceTableRemove))) {
        rfa::Log("Spawn resources: could not replace the names' remove; the game keeps its %d", kGameSpawnResources);
        return;
    }
    g_ownResourceTable = true;
    if (rfa::ReplaceFunction(kResourcePoolMake, reinterpret_cast<void*>(&MakeResourcePool)))
        rfa::Log("Spawn resources: the game holds %d (its own %d), in its pool and the names' table", kSpawnResources, kGameSpawnResources);
    else rfa::Log("Spawn resources: the names' table is the mod's, but the pool's constructor couldn't be replaced; the game keeps %d", kGameSpawnResources);
}

// ---- Guerrilla's standing squads spread out from their node (the user's Round 1, 2026-11-06: "Static Spawn Error")
//
// A squad on foot with Standard orders spawns "static", at its node: the squad's spawn (0x80EC20 → 0x80E2F0, by the
// orders' type; 0x80E1C0 for Standard) gets the node's place and the static flag (+0x3E0 bit 5), and a squad of several
// (0x758C20 → 0x75B010) needs one object_spawn_point placed under the node for each member (gathered by 0x75E0C0);
// with too few the game writes "Static Spawn Error, not enough spawn points placed under spawn node 0x…" and nobody
// spawns. Armageddon's levels place the points (all but three of its own nodes of several have them). Guerrilla has no
// spawn points: its squads spread out from the node over the people's nav mesh (object_squad::spawn_squad_members), and
// its Default orders are our Standard. So in our worlds a squad whose node has fewer points than members spreads out
// from the node's place the way the game places every squad that isn't static (0x759270: from the nav cell under the
// spawn's place, cell by cell, a member to each free spot). A node with enough points keeps the game's own way.
constexpr DWORD kSquadStaticSpawn = 0x75B010;
constexpr size_t kSquadStaticSpawnPrologue = 9;  // push ebp / mov ebp,esp / sub esp,420h
constexpr DWORD kSpawnNode = 0x3B0;              // in the squad's spawn: its node (an object)
constexpr DWORD kSpawnMemberCount = 0x18;        // the count of the members' list at +0x10
using SquadStaticSpawnFn = bool(__cdecl*)(BYTE* spawn);
using SpawnPointListFn = void(__fastcall*)(void* list, void*);
using GatherSpawnPointsFn = void(__cdecl*)(void* node, void* list, int, int);
using SpreadOverNavMeshFn = void(__cdecl*)(BYTE* spawn);
using AllMembersSpawnedFn = bool(__fastcall*)(BYTE* spawn, void*);
using ObjectHandleFn = DWORD(__fastcall*)(void* object, void*);
auto const SpawnPointListMake = reinterpret_cast<SpawnPointListFn>(0xA347C0);  // farray<object*, 256>
auto const SpawnPointListFree = reinterpret_cast<SpawnPointListFn>(0x926130);
auto const GatherSpawnPoints = reinterpret_cast<GatherSpawnPointsFn>(0x75E0C0);
auto const SpreadOverNavMesh = reinterpret_cast<SpreadOverNavMeshFn>(0x759270);
auto const AllMembersSpawned = reinterpret_cast<AllMembersSpawnedFn>(0x75E840);
auto const ObjectHandle = reinterpret_cast<ObjectHandleFn>(0x424850);
SquadStaticSpawnFn g_squadStaticSpawn = nullptr;
int g_spreadSquadsLogged = 0;

int SpawnPointsUnder(void* node) {
    alignas(16) BYTE list[0x420];  // the game's is 0x410 bytes on its stack: its count at +8, the 256 in place from +0xC
    SpawnPointListMake(list, nullptr);
    GatherSpawnPoints(node, list, 0, 0);
    const int count = *reinterpret_cast<const int*>(list + 8);
    SpawnPointListFree(list, nullptr);
    return count;
}

// ---- Guerrilla's guard posts: their squads leashed to the post's box (the user's Round 2, 2026-11-06: "Yes, use the box")
//
// STAND-IN until Guerrilla's guard post AI (go_to_guard_post, guard_at_post, guard_at_post_scan and its guard nodes) is
// ported after the safe spot. In Guerrilla a squad whose node hangs under a bounding box holding guard nodes guards that
// box: its members stand at the guard nodes and fight from there. Armageddon deleted that AI (object_guard_node is
// editor-only in it), so under Standard orders the squad hunted the player down walks the pathfinder won't take ("Failed
// pathfind, probably bad navmesh in the area": the EDF post over the Dust safehouse, a 500-730 m way round, past the
// search's limit of distance x 1.5 + 150, at most 500, the same in Guerrilla). The world build places the box with the
// node under it (GuerrillaPopulation.GuardBoxOf); here, as such a node spawns its squad (0x800040, the node's spawn:
// thiscall, two arguments, the squad's handle or -1), the squad is leashed to the box with Armageddon's own leash to an
// anchor's bounding box (type 4; the squad's leash 0x810990(type, anchor), which only scripts use, sf_update_leash
// 0x94E2DF; the check 0x810710 keeps it inside the anchor's oriented box). Only in our worlds, and only for a node whose
// parent (object +0x6C) is a bounding box.
// Since 2026-11-15 a post the world build lists (guard_posts.txt) gets Guerrilla's guard post AI instead (guards.cpp:
// its members at the post's guard nodes); the leash is left only for a world built before that list.
constexpr DWORD kNodeSpawn = 0x800040;
constexpr size_t kNodeSpawnPrologue = 9;  // push ebp / mov ebp,esp / sub esp,0ECh
constexpr DWORD kObjectParent = 0x6C;     // an object's parent's handle (Guerrilla's object::parent, +0x70 there)
constexpr DWORD kWorld = 0x1870630;
constexpr int kBoxLeash = 4;              // the leash's types: 1-3 Low/Medium/High (30/50/70 m round the anchor), 4 inside its box
constexpr DWORD kNoHandle = 0xFFFFFFFF;
using NodeSpawnFn = DWORD(__fastcall*)(BYTE* node, void*, DWORD a, DWORD b);
using WorldObjectFn = BYTE*(__fastcall*)(void* world, void*, DWORD handle);
using AsBoundingBoxFn = BYTE*(__fastcall*)(void* world, void*, BYTE* object);
using SetLeashFn = void(__fastcall*)(BYTE* squad, void*, int type, DWORD anchor);
auto const WorldObject = reinterpret_cast<WorldObjectFn>(0x6CB9E0);
auto const AsBoundingBox = reinterpret_cast<AsBoundingBoxFn>(0x452170);
auto const SetLeash = reinterpret_cast<SetLeashFn>(0x810990);
NodeSpawnFn g_nodeSpawn = nullptr;
int g_leashesLogged = 0;

DWORD __fastcall NodeSpawn(BYTE* node, void* edx, DWORD a, DWORD b) {
    if (node && InOurWorld() && AmbushHoldsNode(node)) return kNoHandle;  // a Marauder ambush's: its region spawns it (ambush.cpp)
    if (node && InOurWorld() && StoryHoldsNode(node)) return kNoHandle;   // a story person's: story.cpp spawns the one it streams in
    const DWORD squadHandle = g_nodeSpawn(node, edx, a, b);
    if (squadHandle == kNoHandle || !node || !InOurWorld()) return squadHandle;
    void* world = reinterpret_cast<void*>(kWorld);
    const DWORD boxHandle = *reinterpret_cast<const DWORD*>(node + kObjectParent);
    if (boxHandle == kNoHandle) return squadHandle;
    BYTE* parent = WorldObject(world, nullptr, boxHandle);
    BYTE* squad = WorldObject(world, nullptr, squadHandle);
    if (!parent || !squad || !AsBoundingBox(world, nullptr, parent)) return squadHandle;
    if (GuardsPostSpawned(ObjectHandle(node, nullptr), boxHandle, squadHandle)) return squadHandle;  // Guerrilla's guard post AI (guards.cpp)
    // A world built before guard_posts.txt: the box leash, as before the guard post AI.
    SetLeash(squad, nullptr, kBoxLeash, boxHandle);
    if (g_leashesLogged < 64) {
        ++g_leashesLogged;
        rfa::LogDetail("Squads: node 0x%08X's squad 0x%08X leashed to its guard post's box 0x%08X (no guard_posts.txt: the world was built before the guard post AI)",
                 ObjectHandle(node, nullptr), squadHandle, boxHandle);
    }
    return squadHandle;
}

// ---- A guard post's squad asks for no way out of its post (the user's test of 2026-11-10: "Failed pathfind" still came)
//
// Armageddon's leash doesn't hold a squad in. Its one check (0x42E5B0, in each member's AI update: the squad's 0x810710
// on the member's own place) only marks a member that is already outside, to walk it back. A member inside still asks
// for a way to its target, and over the Dust safehouse that walk is 500-730 m round the hill, past the search's limit:
// every request ran a whole search that failed and reported "Failed pathfind, probably bad navmesh in the area" (from
// inside the box to the player on the base floor). The squad stayed at its post only because the search gave up.
// So, still as the stand-in for Guerrilla's guard post AI: in our worlds a member of a squad leashed to a box (type 4,
// which only NodeSpawn above gives) gets no path to a place outside that box. The human's path query (0x5C0B90: cdecl,
// the human, start, goal, two of its path's fields, the 64-bit flags, a bool; the path or null; reached from
// human_pathfind_to 0x854EE0 -> 0x892D80 -> 0x892E10, which takes null as any search that found nothing) answers null
// without searching. The goal is tested with the game's own leash check at the box's own height, since only the ground
// plan counts (a member's feet are on the box's floor, and a goal can lie just under it).
constexpr DWORD kHumanPath = 0x5C0B90;
constexpr size_t kHumanPathPrologue = 9;  // push ebp / mov ebp,esp / sub esp,84h
constexpr DWORD kNpcAi = 0x174C;          // an NPC's AI (the game's getter 0x426250)
constexpr DWORD kAiSquad = 0x7D4;         // the AI's squad's handle (0x4310E0)
constexpr DWORD kSquadLeashAnchor = 0x144, kSquadLeashType = 0x148;
constexpr DWORD kObjectPlace = 4;         // an object's position (x, y, z)
using HumanPathFn = void*(__cdecl*)(BYTE* human, const float* start, const float* goal, DWORD pathA, DWORD pathB, DWORD flagsLow, DWORD flagsHigh, bool flag);
using AsNpcFn = BYTE*(__fastcall*)(void* world, void*, BYTE* object);
using SquadOfFn = BYTE*(__fastcall*)(void* world, void*, DWORD handle);
using OutsideLeashFn = bool(__fastcall*)(BYTE* squad, void*, const float* place);
auto const AsNpc = reinterpret_cast<AsNpcFn>(0x476ED0);                // world::object_cast<npc>
auto const SquadOf = reinterpret_cast<SquadOfFn>(0xA35A10);            // the squad of a handle, or null
auto const OutsideLeash = reinterpret_cast<OutsideLeashFn>(0x810710);  // whether a place is outside the squad's leash
HumanPathFn g_humanPath = nullptr;
int g_pathsTurnedDown = 0;

// The squad an NPC belongs to, or null (also for the player and anything that isn't an NPC).
BYTE* SquadOfNpc(BYTE* human) {
    void* world = reinterpret_cast<void*>(kWorld);
    if (!human || !AsNpc(world, nullptr, human)) return nullptr;
    const BYTE* ai = *reinterpret_cast<BYTE* const*>(human + kNpcAi);
    if (!ai) return nullptr;
    return SquadOf(world, nullptr, *reinterpret_cast<const DWORD*>(ai + kAiSquad));
}

// ---- Failed paths: who asks (the user, 2026-11-11: an error shown in game means something to fix, never to hide)
//
// Besides the guard posts, people ask for ways the search can't give. In rfg_04_10 (2026-11-11 20:07-20:08) patrol node
// 0x4A070509's squad asked nine times for one fixed point 60-70 m east of its route's start: on neither its route (four
// navpoints within 25 m) nor the player (about 190 m away then). Guerrilla's switched-off cells (flag 0x08; 18 % of the
// map's people's cells, which only one mission ever switches on) make the way round 611-631 m, past the search's limit
// of about 250. Guerrilla's own search (the same mesh, the same flags refused, the same limit, the same failure: the
// person's last_pf_failed, the goal kept) fails the same request quietly; Armageddon reports it. All of the AI's moves
// ask through one go-to helper (0x431C20), so which move asks isn't known from the code alone: until it is, each failed
// request in our worlds is logged with the code that asked (the game's return addresses up the stack, named offline
// from its RTTI) and where the player is. This part is built with frame pointers so the walk up the stack starts from
// here and climbs the game's own frames (the publicbeta exe keeps them).
constexpr DWORD kHumanSpawnNode = 0x1590;                       // a human's squad node (the fall report's "Spawn")
auto& g_localPlayer = *reinterpret_cast<BYTE**>(0x2271490);     // the local player (0x8B92D0 returns it)
#pragma optimize("y", off)
// Each request is written the first time its asker (the game's frames) and goal (to 4 m) come up, up to 256 kinds,
// and every 100th failure with the count, so one person asking every second doesn't hide the others.
int g_failedPaths = 0;
std::unordered_set<unsigned long long> g_failedPathKinds;
void LogFailedPath(BYTE* human, const float* goal) {
    ++g_failedPaths;
    void* frames[16] = {};
    const USHORT count = RtlCaptureStackBackTrace(0, 16, frames, nullptr);
    char chain[160] = "";
    size_t used = 0;
    int shown = 0;
    unsigned long long kind = static_cast<unsigned long long>(static_cast<long long>(floorf(goal[0] / 4)) & 0xFFFFF) << 40 |
                              static_cast<unsigned long long>(static_cast<long long>(floorf(goal[2] / 4)) & 0xFFFFF) << 20;
    for (USHORT i = 0; i < count && shown < 8; ++i) {
        const DWORD at = reinterpret_cast<DWORD>(frames[i]);
        if (at < 0x401000 || at >= 0x1000000) continue;  // the game's code only (its .text), not this DLL's
        used += sprintf_s(chain + used, sizeof(chain) - used, shown ? " %06X" : "%06X", at);
        if (shown >= 3 && shown < 6) kind ^= static_cast<unsigned long long>(at) * (shown + 1);  // the asking move's frames
        ++shown;
    }
    const bool firstOfItsKind = g_failedPathKinds.size() < 256 && g_failedPathKinds.insert(kind).second;
    if (!firstOfItsKind && g_failedPaths % 100 != 0) return;
    const float* from = reinterpret_cast<const float*>(human + kObjectPlace);
    const float dx = goal[0] - from[0], dz = goal[2] - from[2];
    float goalToPlayer = -1;
    if (g_localPlayer) {
        const float* player = reinterpret_cast<const float*>(g_localPlayer + kObjectPlace);
        goalToPlayer = sqrtf((player[0] - goal[0]) * (player[0] - goal[0]) + (player[2] - goal[2]) * (player[2] - goal[2]));
    }
    BYTE* squad = SquadOfNpc(human);
    rfa::LogDetail("Paths: no way found for NPC 0x%08X (squad 0x%08X, leash type %d, spawn node 0x%08X) from (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f), %.0f m straight; "
             "the goal is %.0f m from the player; asked through %s (%d so far)",
             ObjectHandle(human, nullptr), squad ? ObjectHandle(squad, nullptr) : kNoHandle, squad ? *reinterpret_cast<const int*>(squad + kSquadLeashType) : 0,
             *reinterpret_cast<const DWORD*>(human + kHumanSpawnNode), from[0], from[1], from[2], goal[0], goal[1], goal[2], sqrtf(dx * dx + dz * dz), goalToPlayer,
             shown ? chain : "(no frames)", g_failedPaths);
}

bool GoalOutOfReach(BYTE* human, const float* start, const float* goal);  // below, with the people's nav cells
thread_local BYTE* g_searchingFor = nullptr;  // the NPC in our worlds whose way the game's query is finding (CellSearch)

// The game's search, and the log line when it finds nothing for an NPC in our worlds.
void* AskTheGame(BYTE* human, const float* start, const float* goal, DWORD pathA, DWORD pathB, DWORD flagsLow, DWORD flagsHigh, bool flag) {
    const bool npc = human && goal && InOurWorld() && AsNpc(reinterpret_cast<void*>(kWorld), nullptr, human);
    if (npc && GoalOutOfReach(human, start, goal)) return nullptr;
    BYTE* const outer = g_searchingFor;
    g_searchingFor = npc ? human : nullptr;
    void* path = g_humanPath(human, start, goal, pathA, pathB, flagsLow, flagsHigh, flag);
    g_searchingFor = outer;
    if (!path && npc) LogFailedPath(human, goal);
    return path;
}

void* __cdecl HumanPath(BYTE* human, const float* start, const float* goal, DWORD pathA, DWORD pathB, DWORD flagsLow, DWORD flagsHigh, bool flag) {
    if (!human || !goal || !InOurWorld()) return g_humanPath(human, start, goal, pathA, pathB, flagsLow, flagsHigh, flag);
    // A guard at its post (guards.cpp) goes only to its post's guard nodes, as Guerrilla's guard_at_post_action.
    if (const int guard = GuardsGoalAllowed(ObjectHandle(human, nullptr), goal); guard >= 0) {
        if (guard) return AskTheGame(human, start, goal, pathA, pathB, flagsLow, flagsHigh, flag);
        ++g_pathsTurnedDown;
        if (g_pathsTurnedDown <= 16 || g_pathsTurnedDown % 200 == 0)
            rfa::LogDetail("Guard posts: a guard 0x%08X asked for a way to (%.1f, %.1f, %.1f), not to one of its post's guard nodes; none given, it fights from its post "
                     "(%d so far)", ObjectHandle(human, nullptr), goal[0], goal[1], goal[2], g_pathsTurnedDown);
        return nullptr;
    }
    BYTE* squad = SquadOfNpc(human);
    if (!squad || *reinterpret_cast<const int*>(squad + kSquadLeashType) != kBoxLeash)
        return AskTheGame(human, start, goal, pathA, pathB, flagsLow, flagsHigh, flag);
    const DWORD boxHandle = *reinterpret_cast<const DWORD*>(squad + kSquadLeashAnchor);
    const BYTE* box = boxHandle == kNoHandle ? nullptr : WorldObject(reinterpret_cast<void*>(kWorld), nullptr, boxHandle);
    if (!box) return AskTheGame(human, start, goal, pathA, pathB, flagsLow, flagsHigh, flag);
    const float* boxPlace = reinterpret_cast<const float*>(box + kObjectPlace);
    const float goalAtBoxHeight[3] = {goal[0], boxPlace[1], goal[2]};
    if (!OutsideLeash(squad, nullptr, goalAtBoxHeight)) return AskTheGame(human, start, goal, pathA, pathB, flagsLow, flagsHigh, flag);
    ++g_pathsTurnedDown;
    if (g_pathsTurnedDown <= 16 || g_pathsTurnedDown % 200 == 0) {
        const float* from = reinterpret_cast<const float*>(human + kObjectPlace);
        rfa::LogDetail("Guard posts: a guard of squad 0x%08X at (%.1f, %.1f, %.1f) asked for a way to (%.1f, %.1f, %.1f), outside its post's box 0x%08X; "
                 "none given, it holds its post (%d so far)",
                 ObjectHandle(squad, nullptr), from[0], from[1], from[2], goal[0], goal[1], goal[2], boxHandle, g_pathsTurnedDown);
    }
    return nullptr;
}
#pragma optimize("", on)

// ---- Who falls out of the world (the user's test of 2026-11-10: "An NPC 0x8AFF00AA fell out of the world and died")
//
// Under each loaded zone's ground lies a box that ends whatever enters it (made per zone by 0x815570, its top 10 m
// under the zone's lowest ground; its callback 0x78D660 puts the player back on the ground and hands everything else
// to 0x78D2E0, cdecl, the object: a living human is reported, then it and its children are removed with the world's
// delete 0x6D5450). Guerrilla's box does the same (game_aabb_phantom::addOverlappingCollidable, RM 0x83BE10: the
// player back on the ground, anything else deleted), only without the report. So the report means a living person
// went through the ground. The ground's collision is the zone's own height field, streamed with the zone (500 m, as
// Guerrilla's 510): three falls in rfg_04_10 (2026-11-11) dropped 19-27 m through ground that was loaded. What the log
// must tell is how they got there: this line, before the game's report, says whether ground collision is under them
// and what the falls watch (below) last saw of them; the watch writes the moment one goes under, while it still can.
constexpr DWORD kLeftTheWorld = 0x78D2E0;
constexpr size_t kLeftTheWorldPrologue = 6;  // push ebp / mov ebp,esp / sub esp,24h
constexpr DWORD kObjectType = 0x7A;          // an object's type (the game's getter 0x422AD0): 0 a human
using LeftTheWorldFn = void(__cdecl*)(BYTE* object);
// 0x7748E0 casts its ray from (x, *height + 1, z) down to y -1000 (with `upward`, from *height - 1 up to +1000) with
// the ray filter 0x1975F00 (layer 0x1C: the ground and its rocks, not buildings) and writes the hit's height. A ray
// from the top must start at 999 (the world's box ends at 1000): from any lower start it misses ground above it.
// (The first version of this line passed 0 and so said "NO ground loaded" over any ground higher than 1 m.)
using GroundHeightFn = bool(__cdecl*)(float* height, float x, float z, float* normal, bool upward);
auto const GroundHeight = reinterpret_cast<GroundHeightFn>(0x7748E0);
constexpr float kRayFromTheTop = 998.0f;
LeftTheWorldFn g_leftTheWorld = nullptr;

// The height of the ground's collision under (x, z), from the top; false where there is none.
bool GroundUnder(float x, float z, float& height) {
    height = kRayFromTheTop;
    return GroundHeight(&height, x, z, nullptr, false);
}

float PlayerDistance(const float* at) {
    if (!g_localPlayer) return -1;
    const float* player = reinterpret_cast<const float*>(g_localPlayer + kObjectPlace);
    return sqrtf((player[0] - at[0]) * (player[0] - at[0]) + (player[2] - at[2]) * (player[2] - at[2]));
}

// ---- The falls watch: every living NPC in our worlds looked at about every 0.2 s (a dozen a frame, through the
// world's list of NPCs: count 0x424480, one 0x4244D0, both thiscall on the world; its list by the NPC's subtype, 2,
// at world +0x308; the first version walked 0x477090 / 0x4770E0, the world's VEHICLES, type 3, and so never looked
// at anyone). Each look asks for the ground's collision under it from the top. On it (within 0.75 m) or above it is
// remembered (each look asks down from 3 m over its feet, and from the top only where nothing is below: a rock over a
// place stops a ray from the top); the first look that finds it more than 1.5 m under that collision, or over none at
// all, is written
// once, with what the look before saw (on the ground, or how high above it: on a building, a vehicle, debris, or in
// the air), where it last stood on the ground, how fast it moved since, and how long it has existed. 30 s into each
// stay in our worlds it writes how many it looks at, so a quiet log means no falls, not no watch.
using HumanCountFn = int(__fastcall*)(void* world, void*);
using HumanAtFn = BYTE*(__fastcall*)(void* world, void*, int index);
using GoneOrDeadFn = bool(__cdecl*)(BYTE* object);
auto const HumanCount = reinterpret_cast<HumanCountFn>(0x424480);  // world::get_num_objects<npc>
auto const HumanAt = reinterpret_cast<HumanAtFn>(0x4244D0);        // world::get_object<npc>(index)
auto const GoneOrDead = reinterpret_cast<GoneOrDeadFn>(0x42ADC0);  // true for null or a dead one (vfunc +0x10)
constexpr int kLooksPerFrame = 12;
constexpr float kOnGround = 0.75f, kUnderGround = 1.5f;
constexpr float kLookFrom = 3.0f;  // how far over an NPC's feet the look down starts (0x7748E0 starts at the height given + 1)

struct FallWatch {
    const BYTE* human = nullptr;   // a handle used again for another human starts over
    DWORD firstSeen = 0, seenAt = 0;
    bool looked = false, everOnGround = false, reported = false;
    DWORD lookAt = 0;              // the last look
    float look[3]{};
    bool lookHadGround = false;
    float lookAbove = 0;           // its height over the ground's collision then
    DWORD onGroundAt = 0;          // the last look that found it on the ground's collision
    float onGround[3]{};
};
std::unordered_map<DWORD, FallWatch> g_fallWatch;
int g_fallWatchNext = 0;
DWORD g_fallWatchPruned = 0;
DWORD g_fallWatchSince = 0;      // when this stay in our worlds began (0: not in one)
bool g_fallWatchTold = false;    // its 30 s line written
int g_fallWatchLooks = 0, g_fallWatchUnder = 0;

// What the watch last saw of a human, for a log line.
void DescribeBefore(const FallWatch* w, const float* now, DWORD tick, char* out, size_t size) {
    if (!w || !w->looked) {
        strcpy_s(out, size, "the watch never saw it");
        return;
    }
    char look[160];
    const float dt = (tick - w->lookAt) / 1000.0f;
    if (!w->lookHadGround)
        sprintf_s(look, "the look %.2f s before, at (%.1f, %.1f, %.1f): no ground collision under it", dt, w->look[0], w->look[1], w->look[2]);
    else if (w->lookAbove >= -kOnGround && w->lookAbove <= kOnGround)
        sprintf_s(look, "the look %.2f s before, at (%.1f, %.1f, %.1f): it stood ON the ground (%.1f m over its collision)", dt, w->look[0], w->look[1],
                  w->look[2], w->lookAbove);
    else if (w->lookAbove > kOnGround)
        sprintf_s(look, "the look %.2f s before, at (%.1f, %.1f, %.1f): %.1f m ABOVE the ground (on a building, a vehicle or debris, or in the air)", dt,
                  w->look[0], w->look[1], w->look[2], w->lookAbove);
    else
        sprintf_s(look, "the look %.2f s before, at (%.1f, %.1f, %.1f): already %.1f m under the ground", dt, w->look[0], w->look[1], w->look[2], -w->lookAbove);
    const float dx = now[0] - w->look[0], dy = now[1] - w->look[1], dz = now[2] - w->look[2];
    const float speed = dt > 0 ? sqrtf(dx * dx + dy * dy + dz * dz) / dt : 0;
    char ground[128];
    if (w->everOnGround)
        sprintf_s(ground, "last on the ground %.2f s before, at (%.1f, %.1f, %.1f)", (tick - w->onGroundAt) / 1000.0f, w->onGround[0], w->onGround[1], w->onGround[2]);
    else
        strcpy_s(ground, "never seen on the ground");
    sprintf_s(out, size, "%s; moving %.1f m/s since (%.1f m/s down); %s; it has existed %.1f s", look, speed, dt > 0 ? -dy / dt : 0, ground,
              (tick - w->firstSeen) / 1000.0f);
}

const FallWatch* WatchOf(BYTE* human) {
    auto found = g_fallWatch.find(ObjectHandle(human, nullptr));
    return found != g_fallWatch.end() && found->second.human == human ? &found->second : nullptr;
}

void ReportUnderground(BYTE* human, const float* at, bool hasGround, float ground, const FallWatch& w, DWORD tick) {
    char before[512], where[128];
    DescribeBefore(&w, at, tick, before, sizeof(before));
    if (hasGround)
        sprintf_s(where, "%.1f m UNDER the ground: ground collision IS there (its top at %.1f)", ground - at[1], ground);
    else
        strcpy_s(where, "over NO ground collision at all (a ray from 999 m down met nothing)");
    BYTE* squad = SquadOfNpc(human);
    RFA_LOG_DETAIL_FIRST(64, "Falls watch: NPC 0x%08X at (%.1f, %.1f, %.1f) is %s; %s; squad 0x%08X (leash type %d), spawn node 0x%08X; the player %.0f m away",
                  ObjectHandle(human, nullptr), at[0], at[1], at[2], where, before, squad ? ObjectHandle(squad, nullptr) : kNoHandle,
                  squad ? *reinterpret_cast<const int*>(squad + kSquadLeashType) : 0, *reinterpret_cast<const DWORD*>(human + kHumanSpawnNode),
                  PlayerDistance(at));
}

// ---- Transitions' cells (the run of 2026-11-11 23:07: "Transition navpoint (0x…) at <…> with invalid navcell", 62)
//
// A transition (types 11, 12, 15, 16, 21, 24) stands on a cell of the people's mesh, kept at the navpoint's +0x98. The
// world build writes it as Armageddon's own do (navpoint_data version 4: the cell's zone prefix × 2^24, layer × 2^15,
// number), and the navpoint's serializer (vtable slot 5, 0x7EFA70) turns that into the cell only if the cell's zone is
// among the loaded zones with its mesh in (0x5D23B0 / 0x5D2400; the zone's people's layer +0x98, cells +0x14 × 36,
// their count a word at +0x10) as it reads the navpoint; it keeps no handle to try again with. The start zone's mesh
// is in by then; the zones round it at a level's start aren't yet (all 62 were in rfg_06_10, 07_08, 07_10, 08_09 and
// 08_10, round Dust's 07_09), so their transitions were left without one, and the game's check at the end of the
// load (0x5D17D0, from 0x5D1790 ← 0x544150: each navpoint of type 11, 12 or 16 with none) reported them. The world
// build also writes each one's cell to mods\guerrilla_world\transition_cells.txt (its handle, the cell's), and the
// mod gives a transition still without one its cell as the serializer would: before that check, and each second
// after for zones streamed in later. In our worlds only.
constexpr DWORD kNavpointType = 0xF4, kNavpointCell = 0x98;
constexpr DWORD kZonePeopleLayer = 0x98, kZoneVehicleLayer = 0xA4, kLayerCellCount = 0x10, kLayerCells = 0x14, kNavCellSize = 36;
using WorldCountFn = int(__fastcall*)(void* world, void*);
using WorldAtFn = BYTE*(__fastcall*)(void* world, void*, int index);
auto const NavpointCount = reinterpret_cast<WorldCountFn>(0x43AC80);  // world::get_num_objects<navpoint>
auto const NavpointAt = reinterpret_cast<WorldAtFn>(0x43ACD0);
auto const NavZoneCount = reinterpret_cast<WorldCountFn>(0x5D23B0);   // the loaded zones (obj_zone)
auto const NavZoneAt = reinterpret_cast<WorldAtFn>(0x5D2400);
constexpr DWORD kNavCheck = 0x5D17D0;
constexpr size_t kNavCheckPrologue = 6;  // push ebp / mov ebp,esp / sub esp,34h
using NavCheckFn = void(__cdecl*)();
NavCheckFn g_navCheck = nullptr;
std::unordered_map<DWORD, unsigned long long> g_transitionCells;
bool g_transitionCellsRead = false;
int g_cellPasses = 0;

// The world build also lists the pedestrians (type 2) it gave the nearest switched-on cell, those on buildings' floors
// and walkways over the ground's switched-off cells (the run of 2026-11-12 15:24, Eos).
bool IsCellNavpoint(int type) { return type == 2 || type == 11 || type == 12 || type == 15 || type == 16 || type == 21 || type == 24; }

void ReadTransitionCells() {
    g_transitionCellsRead = true;
    std::ifstream file(rfa::RootDir() + L"\\mods\\guerrilla_world\\transition_cells.txt");
    DWORD navpoint = 0;
    unsigned long long cell = 0;
    while (file >> std::hex >> navpoint >> cell) g_transitionCells[navpoint] = cell;
    rfa::Log("Transitions: %u cells read from mods\\guerrilla_world\\transition_cells.txt", static_cast<unsigned>(g_transitionCells.size()));
}

// Gives each listed transition still without a cell its cell; how many it gave, and how many are still without one.
int GiveTransitionsCells(int& without) {
    without = 0;
    if (!g_transitionCellsRead) ReadTransitionCells();
    if (g_transitionCells.empty()) return 0;
    void* world = reinterpret_cast<void*>(kWorld);
    int given = 0;
    const int navpoints = NavpointCount(world, nullptr), zones = NavZoneCount(world, nullptr);
    for (int i = 0; i < navpoints; ++i) {
        BYTE* navpoint = NavpointAt(world, nullptr, i);
        if (!navpoint || !IsCellNavpoint(static_cast<signed char>(navpoint[kNavpointType])) || *reinterpret_cast<void**>(navpoint + kNavpointCell)) continue;
        auto listed = g_transitionCells.find(ObjectHandle(navpoint, nullptr));
        if (listed == g_transitionCells.end()) continue;
        const unsigned long long handle = listed->second;
        const DWORD zoneByte = static_cast<DWORD>(handle >> 24) & 0xFF, index = static_cast<DWORD>(handle) & 0x7FFF;
        const DWORD layerAt = (handle & 0x8000) ? kZoneVehicleLayer : kZonePeopleLayer;
        BYTE* cell = nullptr;
        for (int z = 0; z < zones && !cell; ++z) {
            BYTE* zone = NavZoneAt(world, nullptr, z);
            if (!zone || ObjectHandle(zone, nullptr) >> 24 != zoneByte) continue;
            const BYTE* layer = *reinterpret_cast<BYTE* const*>(zone + layerAt);
            if (layer && index < *reinterpret_cast<const WORD*>(layer + kLayerCellCount))
                cell = *reinterpret_cast<BYTE* const*>(layer + kLayerCells) + index * kNavCellSize;
        }
        if (cell) {
            *reinterpret_cast<BYTE**>(navpoint + kNavpointCell) = cell;
            ++given;
        }
        else ++without;
    }
    return given;
}

void __cdecl NavCheck() {
    if (InOurWorld()) {
        int without = 0;
        const int given = GiveTransitionsCells(without);
        rfa::LogDetail("Transitions: %d given their cell before the game's check (their zone's mesh came in after them), %d still without one (their zone's mesh not in)",
                 given, without);
    }
    g_navCheck();
}

void RunTransitionCells() {  // each second in our worlds: zones streamed in since
    if (!InOurWorld() || !g_navCheck) return;
    int without = 0;
    const int given = GiveTransitionsCells(without);
    if (given > 0 && g_cellPasses < 32) {
        ++g_cellPasses;
        rfa::LogDetail("Transitions: %d given their cell as their zone's mesh came in", given);
    }
}

// ---- Goals out of the search's reach (the run of 2026-11-12 16:43, Eos; the user's choice: "Don't pick unreachable goals")
//
// An EDF patrol's member chasing a target on the sunken strip west of the Eos base (go_to_believed_location: Guerrilla's
// own action, RM do_action 0x6B5E50, Armageddon's 0x4898A0) asked for a way from the base's door down to the strip:
// 41 m straight, but the only walk over the people's cells is 304 m round, and the search gives up past its budget
// (0x5BDF60: 1.5 x the distance + 150, about 211 m there; capped at 350 or 500). Armageddon's search (0x5BD200) then
// reports "Failed pathfind, probably bad navmesh in the area"; Guerrilla's (the same budget, RM 0xB02D00) fails the same
// way without a word, and its action avoids itself for 7 s. So in our worlds an NPC's goal is checked before the game
// searches for it (the human's path query, 0x5C0B90, which every AI go-to reaches through ai_system::pathfind_to
// 0x431C20): a goal the search can't reach within its own budget isn't asked for, and the query answers "no way" as a
// failed search does, so the action takes its own failure branch (go_to_believed_location: avoid itself, 7 s in our
// worlds as in Guerrilla, see MatchGuerrillaAvoidTime). The budget stays the game's.
//
// The check walks the same cells as the game's search (the start's and goal's cells by the game's own lookup, 0x5B9AA0;
// switched-on cells only, from edge midpoint to edge midpoint, a step dropped once its way so far plus 0.75 x its
// straight distance to the goal passes the budget), with no costs of its own, so it never finds less than the game's
// would. It turns a goal down only when it is sure: both cells found, in one zone's layer and on one island of it, no
// edge of that zone met on the way (the walk might go on through the next one), and the goal not reached within the
// game's budget as it is (no room added: the Eos walk was 241 m by edge midpoints against a budget of about 211, so
// even 15 % more would have let it through). Anything else goes to the game as before, and a walk the game still can't
// find is still reported. A layer's cells (36 bytes): neighbours +0x00/+0x04/+0x08 (pointers, -1 none), vertex numbers +0x14/+0x16/
// +0x18 (into the layer's vertices, +0x0C), flags +0x1F (0x08 and 0x10 switched off), island +0x20; the layer's outer
// edges (+0x20, their count a word at +0x1C; 8 bytes, the cell's number first) are where it meets the next zone.
constexpr DWORD kLayerVertices = 0x0C, kLayerOuterEdgeCount = 0x1C, kLayerOuterEdges = 0x20, kOuterEdgeSize = 8;
constexpr DWORD kCellVertices = 0x14, kCellFlags = 0x1F, kCellIsland = 0x20;
constexpr BYTE kCellSwitchedOff = 0x18;
constexpr float kLongestWalkLogged = 3000.0f;  // how far the log's walk is measured
constexpr size_t kCellQuerySize = 0x30;
using CellQueryFn = void(__fastcall*)(BYTE* query, void*, const float* place, bool vehicles);   // 0x42C1A0
using CellQueryRulesFn = void(__fastcall*)(BYTE* query, void*, bool a, bool b);              // 0x463190
using FindCellFn = BYTE*(__cdecl*)(BYTE* query);                                               // 0x5B9AA0
using SearchBudgetFn = float(__cdecl*)(BYTE* startCell, const float* start, BYTE* goalCell, const float* goal, bool a, bool b);  // 0x5BDF60
auto const CellQuery = reinterpret_cast<CellQueryFn>(0x42C1A0);
auto const CellQueryRules = reinterpret_cast<CellQueryRulesFn>(0x463190);
auto const FindCell = reinterpret_cast<FindCellFn>(0x5B9AA0);
auto const SearchBudget = reinterpret_cast<SearchBudgetFn>(0x5BDF60);
int g_goalsTurnedDown = 0;

// The people's cell under a place, by the game's own lookup (as a navpoint's stream-in asks it), or null.
BYTE* PeopleCellAt(const float* place) {
    alignas(16) BYTE query[kCellQuerySize] = {};
    CellQuery(query, nullptr, place, false);
    CellQueryRules(query, nullptr, true, true);
    return FindCell(query);
}

// The loaded people's layer holding a cell, or null.
BYTE* LayerOfCell(const BYTE* cell) {
    void* world = reinterpret_cast<void*>(kWorld);
    const int zones = NavZoneCount(world, nullptr);
    for (int z = 0; z < zones; ++z) {
        BYTE* zone = NavZoneAt(world, nullptr, z);
        BYTE* layer = zone ? *reinterpret_cast<BYTE**>(zone + kZonePeopleLayer) : nullptr;
        if (!layer) continue;
        const BYTE* cells = *reinterpret_cast<BYTE* const*>(layer + kLayerCells);
        const int count = *reinterpret_cast<const WORD*>(layer + kLayerCellCount);
        if (cells && cell >= cells && cell < cells + count * kNavCellSize && (cell - cells) % kNavCellSize == 0) return layer;
    }
    return nullptr;
}

// ---- For guerrilla_campaign's reinforcements (the live session, step 2, 2026-11-18)
//
// Guerrilla's nav-mesh questions its reinforcements ask, over Armageddon's cells (the layout above):
//   - build_cell_info (RM 0x787430): the first open cell (flag 0x08 clear) within 20 m of a probe, collected as
//     havok_collect_nav_cells does (the people's mesh, or the vehicles' for a squad with vehicles), its centroid the place;
//   - bb_pf_find_closest_point_on_nav_mesh_slow (0xB0EDB0), for pick_desired_reinforcement_group (0x771250: Flyer when the
//     nearest point is 15 m or more away, or none): every loaded zone's people's layer, every open cell, only its border
//     edges (no neighbour across, or a closed one), the nearest point on each edge; the nearest of all.
constexpr BYTE kCellClosed = 0x08;  // Guerrilla's test (ours elsewhere also treats 0x10 as off)

const float* CellVertex(const BYTE* layer, const BYTE* cell, int k) {
    return *reinterpret_cast<const float* const*>(layer + kLayerVertices) + 3 * *reinterpret_cast<const WORD*>(cell + kCellVertices + 2 * k);
}

bool OpenCellNear(const float* probe, float within, bool vehicles, float* centroid) {
    void* world = reinterpret_cast<void*>(kWorld);
    const int zones = NavZoneCount(world, nullptr);
    for (int z = 0; z < zones; ++z) {
        const BYTE* zone = NavZoneAt(world, nullptr, z);
        const BYTE* layer = zone ? *reinterpret_cast<BYTE* const*>(zone + (vehicles ? kZoneVehicleLayer : kZonePeopleLayer)) : nullptr;
        if (!layer || !*reinterpret_cast<const float* const*>(layer + kLayerVertices)) continue;
        const BYTE* cells = *reinterpret_cast<BYTE* const*>(layer + kLayerCells);
        const int count = *reinterpret_cast<const WORD*>(layer + kLayerCellCount);
        for (int i = 0; cells && i < count; ++i) {
            const BYTE* cell = cells + i * kNavCellSize;
            if (cell[kCellFlags] & kCellClosed) continue;
            float middle[3] = {0, 0, 0};
            for (int k = 0; k < 3; ++k)
                for (int c = 0; c < 3; ++c) middle[c] += CellVertex(layer, cell, k)[c] / 3.0f;
            const float dx = middle[0] - probe[0], dy = middle[1] - probe[1], dz = middle[2] - probe[2];
            if (dx * dx + dy * dy + dz * dz > within * within) continue;
            for (int c = 0; c < 3; ++c) centroid[c] = middle[c];
            return true;
        }
    }
    return false;
}

float NearestNavMeshEdge(const float* point) {
    void* world = reinterpret_cast<void*>(kWorld);
    const int zones = NavZoneCount(world, nullptr);
    float best = -1.0f;
    for (int z = 0; z < zones; ++z) {
        const BYTE* zone = NavZoneAt(world, nullptr, z);
        const BYTE* layer = zone ? *reinterpret_cast<BYTE* const*>(zone + kZonePeopleLayer) : nullptr;
        if (!layer || !*reinterpret_cast<const float* const*>(layer + kLayerVertices)) continue;
        const BYTE* cells = *reinterpret_cast<BYTE* const*>(layer + kLayerCells);
        const int count = *reinterpret_cast<const WORD*>(layer + kLayerCellCount);
        for (int i = 0; cells && i < count; ++i) {
            const BYTE* cell = cells + i * kNavCellSize;
            if (cell[kCellFlags] & kCellClosed) continue;
            for (int e = 0; e < 3; ++e) {
                const float* a = CellVertex(layer, cell, e);
                const float* b = CellVertex(layer, cell, (e + 1) % 3);
                // A border edge: no open neighbour shares both its ends.
                bool shared = false;
                for (int n = 0; n < 3 && !shared; ++n) {
                    const BYTE* next = *reinterpret_cast<BYTE* const*>(cell + 4 * n);
                    if (!next || next == reinterpret_cast<const BYTE*>(-1) || (next[kCellFlags] & kCellClosed)) continue;
                    const bool sameLayer = next >= cells && next < cells + count * kNavCellSize;  // most are: no search
                    const BYTE* nextLayer = sameLayer ? layer : LayerOfCell(next);
                    if (!nextLayer) continue;
                    int same = 0;
                    for (int k = 0; k < 3; ++k) {
                        const float* v = CellVertex(nextLayer, next, k);
                        if ((fabsf(v[0] - a[0]) < 0.01f && fabsf(v[1] - a[1]) < 0.01f && fabsf(v[2] - a[2]) < 0.01f) ||
                            (fabsf(v[0] - b[0]) < 0.01f && fabsf(v[1] - b[1]) < 0.01f && fabsf(v[2] - b[2]) < 0.01f))
                            ++same;
                    }
                    shared = same >= 2;
                }
                if (shared) continue;
                const float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                const float length2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
                float t = length2 > 0 ? ((point[0] - a[0]) * ab[0] + (point[1] - a[1]) * ab[1] + (point[2] - a[2]) * ab[2]) / length2 : 0.0f;
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                const float dx = a[0] + ab[0] * t - point[0], dy = a[1] + ab[1] * t - point[1], dz = a[2] + ab[2] * t - point[2];
                const float d = sqrtf(dx * dx + dy * dy + dz * dz);
                if (best < 0 || d < best) best = d;
            }
        }
    }
    return best;
}

struct WalkResult {
    bool reached = false;     // the goal's cell reached within the limit
    bool undecided = false;   // the walk met the zone's edge or left its layer: the game's search may still go on
    float length = 0;         // the walk's length (edge midpoint to edge midpoint) when reached
};

// The shortest walk from a place in one cell to a place in another over the loaded zones' switched-on people's cells,
// by edge midpoints. With a budget, a step whose way so far plus 0.75 x its straight distance to the goal passes it is
// dropped (the game's search's rule); without one (budget 0), the walk is measured up to kLongestWalkLogged.
// The same walk over every loaded zone's people's layer (2026-11-13, the user's Oasis run: "Failed pathfind ... from
// (1246.0, 34.0, 768.9) to (1321.3, 1.9, 788.4)" and "Pathfinding ran out of nav heap objects": a squad's chase of
// 77 m straight whose walk is 395 m, past the game's budget of 266 m; the walk above stopped at the zone's edge,
// "undecided", so the game was asked and searched until its nav heap (at most 4,096 nodes, 0x5ECA20) ran out). A cell's
// neighbour may be in another zone's layer: the walk follows it there. It still stops, undecided, at an open edge of a
// border cell (no neighbour across) or a neighbour in no loaded layer, where the game's own search could go on.
struct LoadedLayer { const BYTE* cells; int count; const float* vertices; std::vector<char> border; };

WalkResult WalkAcross(const BYTE* startCell, const float* start, const BYTE* goalCell, const float* goal, float budget) {
    WalkResult result;
    std::vector<LoadedLayer> layers;
    void* world = reinterpret_cast<void*>(kWorld);
    const int zones = NavZoneCount(world, nullptr);
    for (int z = 0; z < zones; ++z) {
        const BYTE* zone = NavZoneAt(world, nullptr, z);
        const BYTE* layer = zone ? *reinterpret_cast<BYTE* const*>(zone + kZonePeopleLayer) : nullptr;
        if (!layer) continue;
        LoadedLayer l{*reinterpret_cast<BYTE* const*>(layer + kLayerCells), *reinterpret_cast<const WORD*>(layer + kLayerCellCount),
                      *reinterpret_cast<const float* const*>(layer + kLayerVertices), {}};
        if (!l.cells || !l.vertices || l.count <= 0) continue;
        l.border.assign(l.count, 0);
        const BYTE* outer = *reinterpret_cast<BYTE* const*>(layer + kLayerOuterEdges);
        const int outerCount = *reinterpret_cast<const WORD*>(layer + kLayerOuterEdgeCount);
        for (int e = 0; outer && e < outerCount; ++e) {
            const int cell = *reinterpret_cast<const WORD*>(outer + e * kOuterEdgeSize);
            if (cell < l.count) l.border[cell] = 1;
        }
        layers.push_back(std::move(l));
    }
    auto LayerOf = [&](const BYTE* cell) -> const LoadedLayer* {
        for (const LoadedLayer& l : layers)
            if (cell >= l.cells && cell < l.cells + l.count * kNavCellSize && (cell - l.cells) % kNavCellSize == 0) return &l;
        return nullptr;
    };
    auto Vertex = [](const LoadedLayer& l, const BYTE* cell, int k) { return l.vertices + 3 * *reinterpret_cast<const WORD*>(cell + kCellVertices + 2 * k); };
    auto Distance = [](const float* a, const float* b) {
        return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
    };
    if (!LayerOf(startCell) || !LayerOf(goalCell)) { result.undecided = true; return result; }
    std::unordered_map<const BYTE*, float> best;
    std::unordered_map<const BYTE*, std::array<float, 3>> at;
    using Entry = std::pair<float, const BYTE*>;  // the step's estimate (way so far + straight to the goal), its cell
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    best[startCell] = 0;
    at[startCell] = {start[0], start[1], start[2]};
    open.push({Distance(start, goal), startCell});
    const float limit = budget > 0 ? budget : kLongestWalkLogged;
    while (!open.empty()) {
        const auto [estimate, cell] = open.top();
        open.pop();
        const float way = best[cell];
        if (estimate > way + Distance(at[cell].data(), goal) + 0.01f) continue;  // an older, longer step to this cell
        if (cell == goalCell) {
            result.reached = true;
            result.length = way + Distance(at[cell].data(), goal);
            return result;
        }
        const LoadedLayer& l = *LayerOf(cell);
        const bool onBorder = l.border[(cell - l.cells) / kNavCellSize] != 0;
        for (int k = 0; k < 3; ++k) {
            const BYTE* next = *reinterpret_cast<BYTE* const*>(cell + 4 * k);
            if (next == reinterpret_cast<const BYTE*>(-1) || !next) {
                if (onBorder && budget > 0) { result.undecided = true; return result; }  // the zone's edge, joined to nothing here
                continue;
            }
            const LoadedLayer* nextLayer = LayerOf(next);
            if (!nextLayer) {
                if (budget > 0) { result.undecided = true; return result; }  // into a zone not loaded: the game's search may go on there
                continue;
            }
            if (next[kCellFlags] & kCellSwitchedOff) continue;
            // The edge they share: the midpoint of the two vertices both have (else the next cell's middle).
            float through[3] = {0, 0, 0};
            int shared = 0;
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b) {
                    const float* va = Vertex(l, cell, a);
                    const float* vb = Vertex(*nextLayer, next, b);
                    if (fabsf(va[0] - vb[0]) < 0.01f && fabsf(va[1] - vb[1]) < 0.01f && fabsf(va[2] - vb[2]) < 0.01f) {
                        for (int c = 0; c < 3; ++c) through[c] += va[c];
                        ++shared;
                    }
                }
            if (shared != 2) {
                shared = 0;
                for (int c = 0; c < 3; ++c) through[c] = 0;
                for (int b = 0; b < 3; ++b, ++shared)
                    for (int c = 0; c < 3; ++c) through[c] += Vertex(*nextLayer, next, b)[c];
            }
            for (int c = 0; c < 3; ++c) through[c] /= static_cast<float>(shared);
            const float wayThere = way + Distance(at[cell].data(), through);
            if (auto known = best.find(next); known != best.end() && known->second <= wayThere) continue;
            const float straight = Distance(through, goal);
            if (wayThere + (budget > 0 ? 0.75f * straight : 0) > limit) continue;  // past the budget (the game's rule)
            best[next] = wayThere;
            at[next] = {through[0], through[1], through[2]};
            open.push({wayThere + straight, next});
        }
    }
    return result;
}

// Whether the walk from a place in one cell to a place in another can't be found within the game's search budget (and
// the log line when it can't); `moved` when the places are the ones the game's query moved onto the mesh.
bool OutOfReach(BYTE* human, BYTE* startCell, const float* from, BYTE* goalCell, const float* goal, bool moved) {
    // The game's budget, as its search asks it for a person (the larger of its two caps, so the check never asks less).
    const float budget = std::max(SearchBudget(startCell, from, goalCell, goal, false, false), SearchBudget(startCell, from, goalCell, goal, true, false));
    if (!(budget > 0)) return false;
    const WalkResult withinBudget = WalkAcross(startCell, from, goalCell, goal, budget);
    if (withinBudget.reached || withinBudget.undecided) return false;
    ++g_goalsTurnedDown;
    if (g_goalsTurnedDown <= 64 || g_goalsTurnedDown % 100 == 0) {
        const WalkResult walk = WalkAcross(startCell, from, goalCell, goal, 0);
        const float dx = goal[0] - from[0], dz = goal[2] - from[2];
        BYTE* squad = SquadOfNpc(human);
        char walked[64];
        if (walk.reached) sprintf_s(walked, "the walk %.0f m", walk.length);
        else sprintf_s(walked, "no walk within %.0f m in this zone", kLongestWalkLogged);
        rfa::LogDetail("Goals: NPC 0x%08X (squad 0x%08X) asked for a way from (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f)%s: %.0f m straight, %s, past the search's "
                 "budget of %.0f m; not searched for (its action goes on as when no way is found, as Guerrilla's) (%d so far)",
                 ObjectHandle(human, nullptr), squad ? ObjectHandle(squad, nullptr) : kNoHandle, from[0], from[1], from[2], goal[0], goal[1], goal[2],
                 moved ? " (the places the game's query moved onto its cells)" : "", sqrtf(dx * dx + dz * dz), walked, budget, g_goalsTurnedDown);
    }
    return true;
}

bool GoalOutOfReach(BYTE* human, const float* start, const float* goal) {
    const float* from = start ? start : reinterpret_cast<const float*>(human + kObjectPlace);
    BYTE* startCell = PeopleCellAt(from);
    BYTE* goalCell = PeopleCellAt(goal);
    if (!startCell || !goalCell || startCell == goalCell) return false;
    // Another zone's cell, or another island, no longer goes to the game unasked: the walk follows the zones' cells.
    if (!LayerOfCell(startCell) || !LayerOfCell(goalCell)) return false;
    return OutOfReach(human, startCell, from, goalCell, goal, false);
}

// The same check on the cells the game's search really uses (2026-11-13, the user's Parker run, 17:05: 14 "Failed
// pathfind, probably bad navmesh in the area" that got past the check above, e.g. "from: nch: 0x00004a0000201aeb, pos
// (-1804.6, 17.1, -1252.3) to: nch: 0x00004a0000202e36, pos (-1814.7, 18.9, -1271.9)", a walker fleeing to (-1814.5,
// 17.1, -1275.0)). A goal with no cell under it (a flee's place inside a building, or over a drop) has none by the
// lookup above, so it went to the game, and the query (0x5C0B90) moves it to a cell near it itself (0x5BA130, else
// 0x5C1500): there one 1.8 m up and 3.6 m across, at the EDF base one 4.7 m down, both on the start's island, whose
// walk is past the budget. The query then calls the search between the two cells (0x5C1960: cdecl; the start's cell and
// navpoint, the goal's, the two places, whether for a vehicle; the path or null), which, with both cells on one island
// (0x5BCBB0 on the cells' +0x20), runs the cells' A* (0x5BDC00) that reports the failure. So for an NPC in our worlds
// that search is checked too, with the query's own cells and places, and answers null without searching when the walk
// can't be within the budget; the query answers "no way", as for any failed search (a flee tries its next way).
constexpr DWORD kCellSearch = 0x5C1960;
constexpr size_t kCellSearchPrologue = 9;  // push ebp / mov ebp,esp / sub esp,0C0h
using CellSearchFn = void*(__cdecl*)(BYTE* startCell, void* startNavpoint, BYTE* goalCell, void* goalNavpoint, const float* start, const float* goal, bool vehicle);
using SameIslandFn = bool(__fastcall*)(const BYTE* startIsland, void*, const BYTE* goalIsland);
auto const SameIsland = reinterpret_cast<SameIslandFn>(0x5BCBB0);
CellSearchFn g_cellSearch = nullptr;

void* __cdecl CellSearch(BYTE* startCell, void* startNavpoint, BYTE* goalCell, void* goalNavpoint, const float* start, const float* goal, bool vehicle) {
    BYTE* const human = g_searchingFor;
    if (human && !vehicle && startCell && goalCell && startCell != goalCell && start && goal && SameIsland(startCell + kCellIsland, nullptr, goalCell + kCellIsland)
        && OutOfReach(human, startCell, start, goalCell, goal, true))
        return nullptr;
    return g_cellSearch(startCell, startNavpoint, goalCell, goalNavpoint, start, goal, vehicle);
}

// Guerrilla's go_to_believed_location avoids itself for 7 s after a failed way (RM 0x6B607B, push 1B58h); Armageddon's
// for 4 s (0x489C9A, push 0FA0h). In our worlds its own time, set as the world changes.
constexpr DWORD kBelievedAvoidTime = 0x489C9B;  // the push's value
constexpr DWORD kGameAvoidTime = 4000, kGuerrillaAvoidTime = 7000;

void MatchGuerrillaAvoidTime(bool ours) {
    const DWORD want = ours ? kGuerrillaAvoidTime : kGameAvoidTime, have = rfa::CodeValue(kBelievedAvoidTime);
    if (have == want) return;
    if (have != kGameAvoidTime && have != kGuerrillaAvoidTime) {
        RFA_LOG_FIRST(1, "Goals: go_to_believed_location's avoid time isn't as expected (%lu at 0x%08lX); left as it is", have, kBelievedAvoidTime);
        return;
    }
    if (rfa::PatchCode(reinterpret_cast<void*>(kBelievedAvoidTime), &want, sizeof(want)))
        RFA_LOG_FIRST(8, "Goals: go_to_believed_location avoids itself for %lu s after a failed way (%s)", want / 1000, ours ? "Guerrilla's" : "Armageddon's");
}

// ---- Navpoints made while the game runs (the run of 2026-11-12 11:06: "Invalid transition navpoint (0x89FF1A70) ...
// can not find nav cell", four times, after the user blew holes in the walls of Dust's town hall compound)
//
// When a blast opens a hole in a wall, the game's hole code (0x5D2B60, each frame from 0x540F50, → 0x5D2EB0; Guerrilla's
// dynamic_hole_request_process_destruction_event → create_navpoints_for_hole) makes navpoints at the hole, among them a
// linked pair of transitions (type 11, 0x5D39ED and 0x5D3A3A), one on either side of it. Their props (0x5D45A0) hold a
// place and a type, no navpoint_data. The serializer (0x7EFA70) sets a navpoint's version (+0xD4) only from
// navpoint_data, and the constructor (0x7EF9D0) never clears it, so a navpoint made in a pool slot that last held one
// read from a zone file (version 4, its cell named in the file) keeps that 4. The stream-in (0x7F08A0) then takes it for
// one whose cell came with it, skips the lookup by place, and reports a transition without a cell. Guerrilla's navpoint
// has no version: its stream looks a transition's cell up whenever it has none (and, finding no open one there, joins
// it to the nearest open place, as guerrilla_world's ring search does here). So the version is cleared before the
// serializer reads the props, as a new navpoint's would be; one with navpoint_data gets its own from it as before.
// In our worlds only.
constexpr DWORD kNavpointSerialize = 0x7EFA70;
constexpr size_t kNavpointSerializePrologue = 9;  // push ebp / mov ebp,esp / sub esp,0F8h
constexpr DWORD kNavpointVersion = 0xD4;
using NavpointSerializeFn = bool(__fastcall*)(BYTE* navpoint, void* unused, void* props, bool reading);
NavpointSerializeFn g_navpointSerialize = nullptr;
int g_navpointsWithoutData = 0;  // logged, the first 32

bool __fastcall NavpointSerialize(BYTE* navpoint, void* unused, void* props, bool reading) {
    if (!reading || !navpoint || !InOurWorld()) return g_navpointSerialize(navpoint, unused, props, reading);
    int& version = *reinterpret_cast<int*>(navpoint + kNavpointVersion);
    const int left = version;
    version = 0;
    const bool result = g_navpointSerialize(navpoint, unused, props, reading);
    if (version == 0 && g_navpointsWithoutData++ < 32) {  // no navpoint_data: made while the game runs (a blown hole's)
        const float* at = reinterpret_cast<const float*>(navpoint + kObjectPlace);
        if (left != 0)
            rfa::LogDetail("Navpoints: one made at (%.1f, %.1f, %.1f), type %d, with no navpoint_data had version %d, left in its slot by the "
                     "navpoint before it; cleared, so the game looks its nav cell up by place, as for any new navpoint",
                     at[0], at[1], at[2], static_cast<signed char>(navpoint[kNavpointType]), left);
        else
            rfa::LogDetail("Navpoints: one made at (%.1f, %.1f, %.1f), type %d, with no navpoint_data, in a slot with no version left; the game "
                     "looks its nav cell up by place", at[0], at[1], at[2], static_cast<signed char>(navpoint[kNavpointType]));
    }
    return result;
}

void RunFallsWatch() {
    if (!InOurWorld()) {
        if (!g_fallWatch.empty()) g_fallWatch.clear();
        g_fallWatchSince = 0;
        return;
    }
    void* world = reinterpret_cast<void*>(kWorld);
    const int count = HumanCount(world, nullptr);
    const DWORD tick = GetTickCount();
    if (g_fallWatchSince == 0) {
        g_fallWatchSince = tick;
        g_fallWatchTold = false;
        g_fallWatchLooks = g_fallWatchUnder = 0;
    }
    if (!g_fallWatchTold && tick - g_fallWatchSince >= 30000) {
        g_fallWatchTold = true;
        rfa::LogDetail("Falls watch: running; %d NPCs in the world's list now, %d looks at living ones in 30 s, %d found under the ground", count,
                 g_fallWatchLooks, g_fallWatchUnder);
    }
    if (count <= 0) return;
    for (int n = 0; n < kLooksPerFrame && n < count; ++n) {
        if (g_fallWatchNext >= count) g_fallWatchNext = 0;
        BYTE* human = HumanAt(world, nullptr, g_fallWatchNext++);
        if (!human || human == g_localPlayer || !AsNpc(world, nullptr, human) || GoneOrDead(human)) continue;
        FallWatch& w = g_fallWatch[ObjectHandle(human, nullptr)];
        if (w.human != human) {
            w = FallWatch{};
            w.human = human;
            w.firstSeen = tick;
        }
        w.seenAt = tick;
        const float* at = reinterpret_cast<const float*>(human + kObjectPlace);
        // What it stands on: the first ground or rock surface down from 3 m over its feet. From the top the ray stops
        // on whatever rock is highest over the place (an arch, an overhang: rfg_07_09's pkr_lm_lbrdge1 over people on
        // the ground 24 m below it, the 2026-11-11 23:09-23:11 catches), so that is asked only where nothing is below.
        float ground = at[1] + kLookFrom - 1;
        bool hasGround = GroundHeight(&ground, at[0], at[2], nullptr, false);
        bool deepUnder = false;
        if (!hasGround) {
            hasGround = GroundUnder(at[0], at[2], ground);
            deepUnder = hasGround && ground > at[1];  // ground over it and none under it: through the ground
        }
        if (hasGround && !deepUnder && at[1] - ground < -kUnderGround) {
            // A surface over its feet: a roof or overhang it stands under (a second surface at its feet, looking on
            // down from just under that one), or ground it is inside of (nothing at its feet: under the ground's
            // collision there is none). rfg_07_09's hill post: a rock 1.7 m over a guard on the ground, 2026-11-11 23:46.
            float under = ground - 1.05f;  // the ray starts 1 m over the height given: 5 cm under that surface
            if (GroundHeight(&under, at[0], at[2], nullptr, false) && fabsf(at[1] - under) <= kOnGround + 0.25f) ground = under;
        }
        const float above = at[1] - ground;
        ++g_fallWatchLooks;
        if (!w.reported && (!hasGround || deepUnder || above < -kUnderGround)) {
            ReportUnderground(human, at, hasGround, ground, w, tick);
            w.reported = true;
            ++g_fallWatchUnder;
        }
        if (hasGround && above >= -kOnGround && above <= kOnGround) {
            w.everOnGround = true;
            w.onGroundAt = tick;
            memcpy(w.onGround, at, sizeof(w.onGround));
        }
        w.looked = true;
        w.lookAt = tick;
        memcpy(w.look, at, sizeof(w.look));
        w.lookHadGround = hasGround;
        w.lookAbove = above;
    }
    if (tick - g_fallWatchPruned > 10000) {  // forget the ones not seen for 20 s
        g_fallWatchPruned = tick;
        for (auto i = g_fallWatch.begin(); i != g_fallWatch.end();)
            i = tick - i->second.seenAt > 20000 ? g_fallWatch.erase(i) : std::next(i);
    }
}

// What went under the world, as the game reports and removes it (on the main thread).
void LeftTheWorldNow(BYTE* object) {
    if (object && InOurWorld() && static_cast<signed char>(object[kObjectType]) == 0) {
        const float* at = reinterpret_cast<const float*>(object + kObjectPlace);
        float ground = 0;
        const bool hasGround = GroundUnder(at[0], at[2], ground);
        BYTE* squad = SquadOfNpc(object);
        char groundText[128], before[512];
        if (hasGround) sprintf_s(groundText, "ground collision under it: YES, its top at %.1f (%.1f m above it)", ground, ground - at[1]);
        else strcpy_s(groundText, "ground collision under it: NO, none at all (a ray from 999 m down met nothing)");
        DescribeBefore(WatchOf(object), at, GetTickCount(), before, sizeof(before));
        RFA_LOG_FIRST(64, "Falls: %s 0x%08X went under the world at (%.1f, %.1f, %.1f): %s; the watch: %s; squad 0x%08X (leash type %d), spawn node 0x%08X; the player %.0f m away",
                      object == g_localPlayer ? "the player" : AsNpc(reinterpret_cast<void*>(kWorld), nullptr, object) ? "NPC" : "a human",
                      ObjectHandle(object, nullptr), at[0], at[1], at[2], groundText, before, squad ? ObjectHandle(squad, nullptr) : kNoHandle,
                      squad ? *reinterpret_cast<const int*>(squad + kSquadLeashType) : 0, *reinterpret_cast<const DWORD*>(object + kHumanSpawnNode),
                      PlayerDistance(at));
    }
    g_leftTheWorld(object);
}

// The 21:22 freeze (the user's run of 2026-11-13 21:15): a Marauder
// heavy walker, the player on its turret, fell into the box under the zone. The box's callback (0x78D660) runs inside a
// Havok job, so 0x78D2E0 deleted the walker (the world's delete 0x6D5450) on a job thread; taking it apart used a pool
// another thread owns, the game's assert ("Concurrency bug, another thread using this object pool.", 0xC99A60, int 3)
// stopped that thread while it held Havok's constraint lock ([obj+0xBC], 0xAC2450), and the main thread and six workers
// waited on it for good. So a call on any thread but the main one is queued by handle (once) and returns; the main
// thread's next frame (GameplayFrame) looks each one up and, if it is still there, reports and removes it as the game
// would. The player isn't affected: the callback puts the player back on the ground in its own branch (0x78D6B5), which
// never reaches 0x78D2E0. Guerrilla's box (RM 0x83BE10) defers its delete too, though only in multiplayer (RM 0x995550).
DWORD g_mainThread = 0;            // the game's main thread, from its first gameplay frame
std::mutex g_leftLock;
std::vector<DWORD> g_leftQueue;    // handles of what went under the world on another thread
int g_leftDeferred = 0;

void __cdecl LeftTheWorld(BYTE* object) {
    if (object && g_mainThread && GetCurrentThreadId() != g_mainThread) {
        const DWORD handle = ObjectHandle(object, nullptr);
        if (handle != kNoHandle) {
            std::lock_guard<std::mutex> hold(g_leftLock);
            if (std::find(g_leftQueue.begin(), g_leftQueue.end(), handle) == g_leftQueue.end()) g_leftQueue.push_back(handle);
            return;
        }
    }
    LeftTheWorldNow(object);
}

// Each frame on the main thread: what went under the world on another thread since the last one.
void RunLeftTheWorld() {
    std::vector<DWORD> handles;
    {
        std::lock_guard<std::mutex> hold(g_leftLock);
        if (g_leftQueue.empty()) return;
        handles.swap(g_leftQueue);
    }
    void* world = reinterpret_cast<void*>(kWorld);
    for (const DWORD handle : handles) {
        BYTE* object = WorldObject(world, nullptr, handle);
        if (!object) continue;  // gone already
        ++g_leftDeferred;
        RFA_LOG_FIRST(32, "Falls: object 0x%08X (type %d) went into the box under the zone on a physics job's thread; removed on the main thread's frame, "
                          "whose thread owns the game's pools (%d so far)", handle, static_cast<signed char>(object[kObjectType]), g_leftDeferred);
        LeftTheWorldNow(object);
    }
}

// ---- People back from far off (the run of 2026-11-12 03:04: three "fell out of the world", each first seen walking
// flat into rising ground, 1.5 m under it, 65-137 m from the player)
//
// A person whose apparent distance from the camera is over 200 m is moved without its controller (the humans'
// controller pre-step, the check at 0x852236; Guerrilla's human_process_controllers_pre_integrate, RM 0xA53258): its place
// plus its speed, its height following its path only toward a node off the nav mesh, so over the mesh's cells it
// moves flat. The apparent distance (0x491120 / 0x76A7F0) is the distance × tan(FOV / 2) / tan(22.5°), so a wider
// FOV brings that 200 m nearer (at VerticalFov=95 about 76 m). When the person comes nearer again the game puts it
// back on the ground (0x852294; Guerrilla's RM 0xA533E1): toward a node on a cell it writes the ground's height at
// its x, z straight into its place (the call at 0x85235E), with no path it takes a higher ground only (0x852404).
// Guerrilla's havok_terrain_height (RM 0x83B3F0) reads the terrain's top whatever the person's height; Armageddon's
// 0x7748E0 looks only from 1 m over the height given down, and where that finds nothing it writes 0. So a person
// more than 1 m under the ground was put at height 0 and fell into the box under the zone (top at -4.5 at Dust).
// At those two calls in our worlds, a look that finds nothing asks again from the top, as Guerrilla's does; where
// no ground is loaded at all the person keeps its own height rather than 0.
constexpr DWORD kGroundHeight = 0x7748E0;
constexpr size_t kGroundHeightPrologue = 9;     // push ebp / mov ebp,esp / sub esp,98h
constexpr DWORD kBackTowardCell = 0x852363;     // returned to by the call for a person whose path's next node is on a cell
constexpr DWORD kBackWithoutPath = 0x852409;    // returned to by the call for a person with no path
constexpr float kPutFarLower = 2.0f;            // a put-back this much under the person's feet is written in the log
GroundHeightFn g_groundHeight = nullptr;

bool __cdecl BackFromFarGround(float* height, float x, float z, float* normal, bool upward) {
    const DWORD caller = reinterpret_cast<DWORD>(_ReturnAddress());
    if (upward || (caller != kBackTowardCell && caller != kBackWithoutPath) || !InOurWorld())
        return g_groundHeight(height, x, z, normal, upward);
    const float feet = *height;
    const DWORD handle = caller == kBackTowardCell ? ObjectHandle(reinterpret_cast<BYTE*>(height) - kObjectPlace - sizeof(float), nullptr) : kNoHandle;
    const float* player = g_localPlayer ? reinterpret_cast<const float*>(g_localPlayer + kObjectPlace) : nullptr;
    const float away = player ? sqrtf((player[0] - x) * (player[0] - x) + (player[2] - z) * (player[2] - z)) : -1;
    if (g_groundHeight(height, x, z, normal, false)) {
        if (*height < feet - kPutFarLower)
            RFA_LOG_DETAIL_FIRST(16, "Far people: 0x%08X back from far off at (%.1f, %.1f, %.1f) was put %.1f m LOWER, at %.1f (the first ground under its feet); the player %.0f m away",
                          handle, x, feet, z, feet - *height, *height, away);
        return true;
    }
    *height = kRayFromTheTop;
    if (g_groundHeight(height, x, z, normal, false)) {
        RFA_LOG_DETAIL_FIRST(32, "Far people: 0x%08X back from far off at (%.1f, %.1f, %.1f), %.1f m under the ground, put back on it at %.1f, as Guerrilla does (Armageddon's look from its feet found nothing and would have put it at height 0); the player %.0f m away",
                      handle, x, feet, z, *height - feet, *height, away);
        return true;
    }
    *height = feet;
    RFA_LOG_DETAIL_FIRST(16, "Far people: 0x%08X back from far off at (%.1f, %.1f, %.1f) has no ground loaded under it; it keeps its height (Armageddon would have put it at 0); the player %.0f m away",
                  handle, x, feet, z, away);
    return false;
}

// ---- Far mode by the real distance (the user, 2026-11-12: "Use real distance")
//
// The far-mode test (0x852234, fcomp qword ptr [0xF78610]: the 200.0 other code shares) compares the person's
// apparent distance, measured with the camera FOV at 0x11B9CFC (camera manager +0xBC), with 200 m. Its address
// operand is pointed at g_farApparent, which is set before each controller step to 200 × tan(FOV / 2) / tan(22.5°),
// never under 200. So a wider FOV no longer brings far mode nearer than 200 m from the camera (Guerrilla's, at the
// engine's 45°), and a narrower one (a scope) still takes it further, as in Guerrilla. In Armageddon's own levels it
// stays 200. The render LOD and the controller step's other tests keep the FOV-scaled distance.
// [GuerrillaPeople] FarModeRealDistance=0 keeps the game's own test. Every 30 s in our worlds the log says how many
// people are in far mode and what the humans' controller step costs (pre-step, Havok integration, post-step).
constexpr DWORD kFarTestOperand = 0x852236;       // the address in that fcomp
constexpr DWORD kFarDistanceConstant = 0xF78610;  // 200.0, a double
constexpr double kFarDistance = 200.0;
constexpr double kReferenceHalfFov = 0.3926925063;  // tan of this in the game's measure (0x76A7F0, 0xFB4958): 22.5°
constexpr DWORD kHumanControllers = 0x851B70;     // cdecl (frame time): the humans' controller pre-step, integration and post-step
constexpr size_t kHumanControllersPrologue = 9;   // push ebx / mov ebx,esp / sub esp,8 / and esp,0FFFFFFF0h
const float* const kDistanceFov = reinterpret_cast<const float*>(0x11B9CFC);  // vertical degrees
constexpr DWORD kHumanFarFlags = 0xBC;            // a human's: bit 0x20 while in far mode (set at 0x852910, cleared at 0x8522B3)
constexpr DWORD kInFarMode = 0x20;
constexpr DWORD kFarReportEvery = 30000;
using HumanControllersFn = void(__cdecl*)(float frameTime);
HumanControllersFn g_humanControllers = nullptr;
double g_farApparent = kFarDistance;
bool g_farRealDistance = false;
bool g_farToldThisStay = false;
DWORD g_farReportAt = 0;
double g_controllersMs = 0, g_controllersSlowest = 0;
int g_controllersFrames = 0;

// How much farther the game's measure makes a person look than it is, at a camera FOV (1 at the engine's 45°).
double FovScale(float fov) { return tan(fov * 0.5 * 3.14159265358979 / 180.0) / tan(kReferenceHalfFov); }
double FarApparentFor(float fov) {
    const double scale = FovScale(fov);
    return kFarDistance * (scale > 1.0 ? scale : 1.0);
}
// Where, from the camera, far mode begins now.
double FarModeStarts(bool realDistance) {
    const double scale = FovScale(*kDistanceFov);
    return kFarDistance / (realDistance && scale > 1.0 ? 1.0 : scale);
}

void ReportFarMode(DWORD tick) {
    int people = 0, farOff = 0;  // (not "far": a Windows header's macro)
    void* world = reinterpret_cast<void*>(kWorld);
    const int count = HumanCount(world, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(world, nullptr, i);
        if (!human || GoneOrDead(human)) continue;
        ++people;
        if (*reinterpret_cast<const DWORD*>(human + kHumanFarFlags) & kInFarMode) ++farOff;
    }
    rfa::LogDetail("Far mode: %d of %d people in it (past %.0f m from the camera%s); the humans' controller step took %.2f ms a frame on average, the slowest %.2f ms, over %d frames",
             farOff, people, FarModeStarts(g_farRealDistance), g_farRealDistance ? ", the real distance" : ", the game's own measure", g_controllersFrames ? g_controllersMs / g_controllersFrames : 0.0,
             g_controllersSlowest, g_controllersFrames);
    g_controllersMs = g_controllersSlowest = 0;
    g_controllersFrames = 0;
    g_farReportAt = tick;
}

void __cdecl HumanControllers(float frameTime) {
    const bool ours = InOurWorld();
    g_farApparent = ours && g_farRealDistance ? FarApparentFor(*kDistanceFov) : kFarDistance;
    if (!ours) {
        g_farToldThisStay = false;
        g_humanControllers(frameTime);
        return;
    }
    const DWORD tick = GetTickCount();
    if (!g_farToldThisStay) {
        g_farToldThisStay = true;
        g_farReportAt = tick;
        g_controllersMs = g_controllersSlowest = 0;
        g_controllersFrames = 0;
        rfa::Log("Far mode: people switch to it past %.0f m from the camera%s (the camera's FOV for distances is %.1f degrees; the game's own test would switch at %.0f m)",
                 FarModeStarts(g_farRealDistance), g_farRealDistance ? ", by the real distance" : ", by the game's own measure", *kDistanceFov,
                 FarModeStarts(false));
    }
    if (!rfa::DetailLog()) {  // the step's timing and the "Far mode:" report are for the log (Log=1)
        g_humanControllers(frameTime);
        return;
    }
    LARGE_INTEGER start, end, frequency;
    QueryPerformanceCounter(&start);
    g_humanControllers(frameTime);
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    const double ms = (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
    g_controllersMs += ms;
    if (ms > g_controllersSlowest) g_controllersSlowest = ms;
    ++g_controllersFrames;
    if (tick - g_farReportAt >= kFarReportEvery) ReportFarMode(tick);
}

void FarModeByRealDistance() {
    g_farRealDistance = rfa::IniInt(L"GuerrillaPeople", L"FarModeRealDistance", 1) != 0;
    if (g_farRealDistance) {
        const DWORD ours = reinterpret_cast<DWORD>(&g_farApparent);
        if (rfa::CodeValue(kFarTestOperand) != kFarDistanceConstant || !rfa::PatchCode(reinterpret_cast<void*>(kFarTestOperand), &ours, sizeof(ours))) {
            rfa::Log("Far mode: the controller step's far test isn't as expected (0x%08lX at 0x%08lX); it keeps the game's own measure", rfa::CodeValue(kFarTestOperand),
                     kFarTestOperand);
            g_farRealDistance = false;
        }
    }
    g_humanControllers = reinterpret_cast<HumanControllersFn>(rfa::HookFunction(kHumanControllers, kHumanControllersPrologue, reinterpret_cast<void*>(&HumanControllers)));
    if (!g_humanControllers && g_farRealDistance) {
        // Without the hook nothing sets g_farApparent per frame: put the game's own test back.
        const DWORD game = kFarDistanceConstant;
        rfa::PatchCode(reinterpret_cast<void*>(kFarTestOperand), &game, sizeof(game));
        g_farRealDistance = false;
        rfa::Log("Far mode: could not hook the humans' controller step; far mode keeps the game's own measure, and its cost isn't written");
    }
    else if (!g_humanControllers) rfa::Log("Far mode: could not hook the humans' controller step; its cost isn't written");
}

bool __cdecl SquadStaticSpawn(BYTE* spawn) {
    void* node = *reinterpret_cast<void**>(spawn + kSpawnNode);
    if (!node || !InOurWorld()) return g_squadStaticSpawn(spawn);
    const int members = *reinterpret_cast<const int*>(spawn + kSpawnMemberCount);
    const int points = SpawnPointsUnder(node);
    if (points >= members) return g_squadStaticSpawn(spawn);
    SpreadOverNavMesh(spawn);
    const bool all = AllMembersSpawned(spawn, nullptr);
    if (g_spreadSquadsLogged < 32) {
        ++g_spreadSquadsLogged;
        rfa::LogDetail("Squads: node 0x%08X's %d spread out from it over the nav mesh, as in Guerrilla (%d spawn point(s) under it)%s",
                 ObjectHandle(node, nullptr), members, points, all ? "" : "; not all of them found room");
    }
    return all;
}

// The parts the people's animations need room from (they move the game's tables at start-up): the
// animation library and entries (guerrilla_vehicles: 8,192 for 4,500 and 4,224) and the map of every
// set's states and actions (guerrilla_gear: 16,384 for 6,344, which the game never checks; past it the
// heap is overwritten). Asked when the game starts its own start-up (every part has started by then), before
// it reads any animation list.
void WarnIfRoomMissing() {
    if (!guerrilla_vehicles::Running())
        rfa::Log("Animations: guerrilla_vehicles is off, so the game holds only its own 4,500 animations; Guerrilla's people's won't all fit (turn it on)");
    if (!guerrilla_gear::Running())
        rfa::Log("Animations: guerrilla_gear is off, so the game's map of the sets' states and actions keeps its own 6,344 places, which Guerrilla's people's sets go past (turn it on)");
}

void __cdecl AnimStringsInit() {
    WarnIfRoomMissing();
    g_animStringsInit();
    char*& memory = *reinterpret_cast<char**>(kAnimStrings + kPoolMemory);
    DWORD& size = *reinterpret_cast<DWORD*>(kAnimStrings + kPoolSize);
    const DWORD used = *reinterpret_cast<DWORD*>(kAnimStrings + kPoolUsed);
    if (size != kGameAnimStringBytes || used > size || !memory) {
        rfa::Log("Animation names: the pool isn't as expected (size %lu, used %lu); it keeps its own", size, used);
        return;
    }
    std::memcpy(g_animStrings, memory, used);
    memory = g_animStrings;
    size = kAnimStringBytes;
    rfa::Log("Animation names: the pool holds %lu KB (its own %lu KB)", kAnimStringBytes / 1024, kGameAnimStringBytes / 1024);
}

// ---- Their animations

constexpr DWORD kLoadAnimationArchive = 0x95E180;
constexpr size_t kLoadAnimationArchivePrologue = 9;  // push ebp / mov ebp,esp / sub esp,274h
using LoadAnimationArchiveFn = int(__cdecl*)(const char* path);
LoadAnimationArchiveFn g_loadAnimationArchive;

// The animation library: an animation's place by its name ("x.anim"; -1 if none), a new place for a
// name (its place if it has one; -1 if the library is full), and the data overlaid on the place a name
// has (keep: an unload passes over it).
auto const FindAnimation = reinterpret_cast<int(__cdecl*)(const char* name)>(0xD8C2C0);
auto const NewAnimation = reinterpret_cast<int(__cdecl*)(const char* name)>(0xD875F0);
auto const OverlayAnimation = reinterpret_cast<int(__cdecl*)(const char* name, void* data, unsigned size, bool keep)>(0xD8C0F0);

// The names stay with their places (the library keeps the pointer), so they are kept for good.
std::vector<std::unique_ptr<std::string>> g_animationNames;

// Reads one file whole into memory aligned as the game's animation memory (16 bytes), kept for good.
void* ReadAnimation(const std::wstring& path, unsigned& size) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return nullptr;
    const std::streamoff length = file.tellg();
    if (length <= 0 || length > 64 * 1024 * 1024) return nullptr;
    void* data = _aligned_malloc(static_cast<size_t>(length), 16);
    if (!data) return nullptr;
    file.seekg(0);
    if (!file.read(static_cast<char*>(data), length)) {
        _aligned_free(data);
        return nullptr;
    }
    size = static_cast<unsigned>(length);
    return data;
}

void LoadOurAnimations() {
    const std::wstring folder = rfa::RootDir() + L"\\mods\\guerrilla_people\\animations\\";
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW((folder + L"*.anim_pc").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return;
    unsigned loaded = 0, failed = 0, noRoom = 0;
    size_t bytes = 0;
    do {
        std::wstring file = found.cFileName;
        std::string stem;
        for (wchar_t c : file.substr(0, file.size() - 8)) stem += static_cast<char>(c);  // without .anim_pc
        if (stem.empty() || stem.size() > 58) { ++failed; continue; }
        g_animationNames.push_back(std::make_unique<std::string>(stem + ".anim"));
        const char* name = g_animationNames.back()->c_str();
        if (FindAnimation(name) < 0 && NewAnimation(name) < 0) { ++noRoom; continue; }
        unsigned size = 0;
        void* data = ReadAnimation(folder + file, size);
        if (!data) { ++failed; continue; }
        // (A place that has data already, or is kept already, is passed over: the game's own wins.)
        if (OverlayAnimation(name, data, size, true) < 0) { _aligned_free(data); ++noRoom; continue; }
        ++loaded;
        bytes += size;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    rfa::Log("Animations: %u of Guerrilla's people's loaded and kept (%.1f MB)", loaded, bytes / 1048576.0);
    if (noRoom) rfa::Log("Animations: %u had no place in the animation library (it is full: is guerrilla_vehicles on?); the people won't play them", noRoom);
    if (failed) rfa::Log("Animations: %u could not be read", failed);
}

int __cdecl LoadAnimationArchive(const char* path) {
    const int result = g_loadAnimationArchive(path);
    // (The animation lists have been read by now: how full their names left the pool.)
    rfa::Log("Animation names: %lu of the pool's %lu bytes used", *reinterpret_cast<const DWORD*>(kAnimStrings + kPoolUsed),
             *reinterpret_cast<const DWORD*>(kAnimStrings + kPoolSize));
    LoadOurAnimations();
    return result;
}

// ---- The world's spawn resource (how far its people have loaded, for the log)

auto const FindCharacter = reinterpret_cast<void*(__cdecl*)(const char* name)>(0x88D170);
constexpr const char* PlayerCharacterName = "rfg_PC";  // Guerrilla's player, Mason (GuerrillaPeople.PlayerCharacter)
auto const CharacterLoaded = reinterpret_cast<bool(__cdecl*)(void* character)>(0x762230);
auto const CharacterOnItsWay = reinterpret_cast<bool(__cdecl*)(void* character)>(0x764ED0);
// The pools' state (a method of it): whether a level's spawn resource is in force.
auto const ResourceInForce = reinterpret_cast<bool(__fastcall*)(void* pools, void* unused)>(0x75FC10);
void* const kPools = reinterpret_cast<void*>(0x19734CC);
std::vector<std::string> g_people;  // mods\guerrilla_people\people.txt

// The level's spawn resource: the game picks the people a level loads by a resource, set as the
// level's (0x7628B0: the one to load, kept at 0x19734D0, which its spawn resource manager then holds
// in the level's category, 0x762600) or applied at once (0x7625A0); both cdecl, the resource first.
// It names them from the zone the player stands in (its obj_zone's spawn_resource_data), the mission,
// a cutscene or a script, and any name its table hasn't, or none, gives its fallback, the table's
// "Default" (made first by the table's loader, 0x75EA90, kept at 0x1969954): Armageddon's aliens,
// soldiers and Marauders. In our worlds some path gave that one (the zones all name rfg_Default), and
// its people's effects ran the effect pools out. So in our worlds the mod gives the game the world's
// own resource (rfg_Default, found by name with 0x75EC70) wherever it would load the fallback, and
// says in the log who asked for it (the return address).
constexpr DWORD kSetLevelResource = 0x7628B0;
constexpr size_t kSetLevelResourcePrologue = 7;  // push ebp / mov ebp,esp / cmp dword ptr [ebp+8],0
constexpr DWORD kApplyLevelResource = 0x7625A0;
constexpr size_t kApplyLevelResourcePrologue = 6;  // push ebp / mov ebp,esp / push ecx / push 0
using SetLevelResourceFn = int(__cdecl*)(void* resource, int again);
using ApplyLevelResourceFn = int(__cdecl*)(void* resource);
SetLevelResourceFn g_setLevelResource;
ApplyLevelResourceFn g_applyLevelResource;
auto const FindSpawnResource = reinterpret_cast<void*(__cdecl*)(const char* name)>(0x75EC70);
void* const* const kFallbackResource = reinterpret_cast<void* const*>(0x1969954);
constexpr const char* kWorldResource = "rfg_Default";
int g_swapsLogged = 0;

// The resource to load: the world's own in place of the fallback in our worlds; else as given.
void* WorldResource(void* resource, const void* caller) {
    if (!resource || !InOurWorld()) return resource;
    const char* name = *static_cast<const char* const*>(resource);  // an entry starts with its name
    const bool fallback = resource == *kFallbackResource || (name && _stricmp(name, "Default") == 0);
    if (!fallback) return resource;
    void* ours = FindSpawnResource(kWorldResource);
    if (g_swapsLogged < 20) {
        ++g_swapsLogged;
        if (ours) rfa::Log("Spawn resource: the game was loading Armageddon's \"Default\" in world \"%s\" (asked from 0x%p); the world's %s instead", WorldName(), caller, kWorldResource);
        else rfa::Log("Spawn resource: the game is loading Armageddon's \"Default\" in world \"%s\" (asked from 0x%p), and the table has no %s (the people not built?)", WorldName(), caller, kWorldResource);
    }
    return ours ? ours : resource;
}

int __cdecl SetLevelResource(void* resource, int again) {
    void* given = WorldResource(resource, _ReturnAddress());
    if (InOurWorld()) given = AmbientResourceFor(given);  // a rotating district's version in force (ambient.cpp, step 4c)
    return g_setLevelResource(given, again);
}

int __cdecl ApplyLevelResource(void* resource) {
    return g_applyLevelResource(WorldResource(resource, _ReturnAddress()));
}

// ---- The player: Guerrilla's own in our worlds
//
// The game picks the player's character once per level load (0x8B77B0, cdecl: the start position;
// Guerrilla's player_local_get_human_info_for_creation): multiplayer's choice; else a DLC uniform the
// player chose in Armageddon's menu (its checksum at 0x12F6BA0, 0x51A580 reads it); else the start
// zone's spawn resource's Player_Override, which in the Guerrilla world is Guerrilla's player, Mason
// (GuerrillaBuilder.exe's people); else Armageddon's RF4_PC. So that Mason is the player there whatever
// uniform is chosen, the mod sets the chosen uniform aside while the game picks, in our worlds only,
// and puts it back straight after (Armageddon's own levels keep it).
constexpr DWORD kPickPlayer = 0x8B77B0;
constexpr size_t kPickPlayerPrologue = 6;  // push ebp / mov ebp,esp / sub esp,10h
using PickPlayerFn = void*(__cdecl*)(const void* startPosition);
PickPlayerFn g_pickPlayer;
DWORD* const kChosenUniform = reinterpret_cast<DWORD*>(0x12F6BA0);
int g_uniformsLogged = 0;

// The player's animation set: when the game makes the player's human (0x8605E0 → 0x86091D, the
// player only), in multiplayer or a Wrecking Crew level (0x511930: [0x10F83FC] isn't -1; our worlds are
// those) it gives the player's character instance Armageddon's player set, "vs_player" (0x862B00,
// cdecl: the human's +0x760; the instance at +4, its sets at +0xC and +0x10, found by name 0x98C290),
// whatever set its character names. Its clips are Darius's (137 bones, bound by bone number), so on
// Guerrilla's player, Mason, nothing plays. In our worlds the player keeps the set his character gives
// him (Mason's rfg_mason; Darius's is vs_player anyway); the mod says which in the log.
constexpr DWORD kGivePlayerSet = 0x862B00;
constexpr size_t kGivePlayerSetPrologue = 10;  // push ebp / mov ebp,esp / movzx eax,byte ptr [2685118h]
using GivePlayerSetFn = void(__cdecl*)(void* human);
GivePlayerSetFn g_givePlayerSet;
auto const FindAnimationSet = reinterpret_cast<void*(__cdecl*)(const char* name)>(0x98C290);
int g_setsLogged = 0;

const char* SetName(void* set) {
    if (!set) return "none";
    for (const char* name : {"rfg_mason", "vs_player", "rfg_player"})
        if (FindAnimationSet(name) == set) return name;
    return "another of its own";
}

void __cdecl GivePlayerSet(void* human) {
    BYTE* instance = human ? *reinterpret_cast<BYTE**>(static_cast<BYTE*>(human) + 4) : nullptr;
    void* own = instance ? *reinterpret_cast<void**>(instance + 0x10) : nullptr;
    if (!own && instance) own = *reinterpret_cast<void**>(instance + 0xC);
    if (!instance || !own || !InOurWorld()) {
        g_givePlayerSet(human);
        if (instance && g_setsLogged < 10) {
            ++g_setsLogged;
            rfa::Log("Player: animation set %s (world \"%s\")", SetName(*reinterpret_cast<void**>(instance + 0x10)), WorldName());
        }
        return;
    }
    // Keep his own: the instance's current set and its base.
    *reinterpret_cast<void**>(instance + 0x10) = own;
    *reinterpret_cast<void**>(instance + 0xC) = own;
    if (g_setsLogged < 10) {
        ++g_setsLogged;
        rfa::Log("Player: keeps his own animation set, %s, in world \"%s\" (the game would give him vs_player)", SetName(own), WorldName());
    }
}

// Guerrilla's guerrilla men (npc_maybe_set_male_guerrilla_anim_set, RM 0xA0F8C0; the user's run of 2026-11-13: the
// guerrillas in civilian clothes didn't fight). Guerrilla's district people are civilians to look at, and a man of the
// guerrillas' or the civilians' team who draws a gun or a melee weapon (draw_firearm_action::enter RM 0x658A74,
// draw_melee_action::enter RM 0x658B2E, and a convert, morale_convert_single_npc) is given the soldiers' "Male" set
// (NPC_male_guerrilla_anim_set) in place of his civilian one, unless his human info marks him otherwise (+0x17C bit 0:
// the women keep theirs). Armageddon's draw actions are the same (vtables 0xF75C18 and 0xF75C48, enter in slot 4:
// 0x492E70, 0x493050, thiscall (npc, action), ret 8). Here, in our worlds, a man of team 0 or 2 whose set is the male
// civilians' (rfg_Civilian_Male) gets rfg_Male, Guerrilla's Male, as he draws: the animation instance (the human's
// +0x760, its +4) keeps its set at +0x10 and its current one at +0xC, as the game's own player set (0x862B00) writes.
constexpr DWORD kDrawFirearmEnter = 0x492E70, kDrawMeleeEnter = 0x493050;
constexpr size_t kDrawEnterPrologue = 6;  // push ebp / mov ebp,esp / sub esp,8
constexpr DWORD kHumanAnimation = 0x760, kHumanTeam = 0x13B4;
using DrawEnterFn = int(__fastcall*)(void* action, void*, BYTE* npc, void* aiAction);
DrawEnterFn g_drawFirearmEnter = nullptr, g_drawMeleeEnter = nullptr;
int g_guerrillaMen = 0, g_drawsSeen = 0;

// An NPC's animation set by name, for the log (the sets Guerrilla's people use).
const char* NpcSetName(void* set) {
    if (!set) return "none";
    for (const char* name : {"rfg_Civilian_Male", "rfg_Civilian_Female", "rfg_Male", "rfg_Female", "rfg_elite", "rfg_scavenger"})
        if (FindAnimationSet(name) == set) return name;
    return "another";
}

// Guerrilla's npc_maybe_set_male_guerrilla_anim_set: `why` says when (as he draws, or as he joins the guerrillas).
void MaybeGuerrillaMan(BYTE* npc, const char* why) {
    if (!npc || !InOurWorld()) return;
    const int team = *reinterpret_cast<const int*>(npc + kHumanTeam);
    BYTE* animation = *reinterpret_cast<BYTE**>(npc + kHumanAnimation);
    BYTE* instance = animation ? *reinterpret_cast<BYTE**>(animation + 4) : nullptr;
    // The first few in each run, whoever they are: whether the game's NPCs go through here at all.
    if (++g_drawsSeen <= 8)
        rfa::LogDetail("Guerrilla men: NPC 0x%08X (team %d, set %s) %s", ObjectHandle(npc, nullptr), team,
                 NpcSetName(instance ? *reinterpret_cast<void**>(instance + 0x10) : nullptr), why);
    if (team != 0 && team != 2) return;
    if (!instance) return;
    void* civilianMale = FindAnimationSet("rfg_Civilian_Male");
    void* male = FindAnimationSet("rfg_Male");
    if (!civilianMale || !male) {
        RFA_LOG_FIRST(1, "Guerrilla men: rfg_Civilian_Male or rfg_Male isn't loaded; a guerrilla in civilian clothes keeps his civilian set");
        return;
    }
    if (*reinterpret_cast<void**>(instance + 0x10) != civilianMale && *reinterpret_cast<void**>(instance + 0xC) != civilianMale) return;
    *reinterpret_cast<void**>(instance + 0x10) = male;
    *reinterpret_cast<void**>(instance + 0xC) = male;
    if (++g_guerrillaMen <= 16)
        rfa::LogDetail("Guerrilla men: NPC 0x%08X (team %d) %s: Guerrilla's Male set in place of the male civilians' (%d so far)", ObjectHandle(npc, nullptr), team, why,
                 g_guerrillaMen);
}

// Guerrilla's grenade rule (2026-11-13; the user: "got rag dolled by their nades and could not get back up as they
// where throwing them non stop"). Guerrilla throws only when the thrower's own timer (human_combat's
// throw_grenade_timestamp) and the one all its AI share (Global_ai_grenade_cooldown_timer) are both over
// (get_weapon_for_offhand_attack RM 0x626BA0, attack_grenade_enter_common RM 0x69A4B0); a throw on foot sets them to
// 15-30 s and 10-20 s, so one grenade every 10-20 s among all of them. Armageddon kept the timers (0x49D220: the
// shared 0x11B5774, the thrower's own npc +0x1034) but its check (0x49D170, cdecl: the NPC → may it throw) lets the
// last thrower (0x11B56BC) through whatever the shared timer says, and three of its four callers don't start the
// thrower's own: one Marauder could throw again and again. In our worlds the check is Guerrilla's (both timers over,
// no last-thrower exemption); elsewhere Armageddon's.
constexpr DWORD kMayThrow = 0x49D170;
constexpr size_t kMayThrowPrologue = 6;  // push ebp / mov ebp,esp / mov ecx,[ebp+8]
constexpr DWORD kOwnThrowTimer = 0x1034;
int* const kSharedThrowTimer = reinterpret_cast<int*>(0x11B5774);
using TimerFn = bool(__fastcall*)(int* timer, void*);
auto const TimerSet = reinterpret_cast<TimerFn>(0xC8B240);      // timestamp::valid
auto const TimerElapsed = reinterpret_cast<TimerFn>(0xC8B120);  // timestamp::elapsed
using MayThrowFn = bool(__cdecl*)(BYTE* npc);
MayThrowFn g_mayThrow = nullptr;
int g_throwsHeld = 0;

bool TimerOver(int* timer) { return !TimerSet(timer, nullptr) || TimerElapsed(timer, nullptr); }

bool __cdecl MayThrow(BYTE* npc) {
    if (!npc || !InOurWorld()) return g_mayThrow(npc);
    const bool may = TimerOver(reinterpret_cast<int*>(npc + kOwnThrowTimer)) && TimerOver(kSharedThrowTimer);
    if (!may && g_mayThrow(npc) && ++g_throwsHeld <= 8)
        rfa::LogDetail("Grenades: NPC 0x%08X may not throw yet (Guerrilla's rule: its own and the shared wait both over; Armageddon's check would let it)",
                 ObjectHandle(npc, nullptr));
    return may;
}

int __fastcall DrawFirearmEnter(void* action, void* edx, BYTE* npc, void* aiAction) {
    MaybeGuerrillaMan(npc, "draws a gun");
    return g_drawFirearmEnter(action, edx, npc, aiAction);
}

int __fastcall DrawMeleeEnter(void* action, void* edx, BYTE* npc, void* aiAction) {
    MaybeGuerrillaMan(npc, "draws a melee weapon");
    return g_drawMeleeEnter(action, edx, npc, aiAction);
}

// Guerrilla's third call of it, morale_convert_single_npc (RM 0x788EF6): a convert gets the set as he joins, already
// holding his gun (our converts were walkers, who carry their character's rifle and never draw). The alert session's
// converts tell us each one (GuerrillaPeople_Converted, below); the set is given on the game's frame.
SRWLOCK g_newConvertsLock = SRWLOCK_INIT;
std::vector<DWORD> g_newConverts;

void GiveConvertsTheirSet() {
    std::vector<DWORD> converts;
    AcquireSRWLockExclusive(&g_newConvertsLock);
    converts.swap(g_newConverts);
    ReleaseSRWLockExclusive(&g_newConvertsLock);
    for (DWORD handle : converts)
        if (BYTE* npc = WorldObject(reinterpret_cast<void*>(kWorld), nullptr, handle)) {
            MaybeGuerrillaMan(npc, "joins the guerrillas");
            VoiceSay(npc, "com_ally_combat_join");  // Guerrilla's maybe_play_join_player_voice_line (RM 0x5E4220)
        }
}

// The world's player is Guerrilla's whatever the start zone gives: the game takes the Player_Override of the spawn
// resource of the zone at the start position (the zone at it, 0x6CB3C0; its resource, +0xC0; that one's player,
// +0x1A8), and where that zone has no resource in force it falls back to Armageddon's RF4_PC, Darius. That happened in
// every level started in Parker or Eos (`level rfg_04_10`, `level rfg_05_03`; 2026-11-11 19:51, 21:02, 21:26,
// 22:17: the game then also asked for Armageddon's "Default" for the level, 0x761F21), never in Dust, though every
// one of the world's resources names rfg_PC: the zones' rfg_parker and rfg_eos weren't in force at the start (why is
// written 5 s in: ReportOurResources). Guerrilla has one player, Mason, so in our worlds the pick ends on him.
constexpr DWORD kZoneSpawnResource = 0xC0, kResourcePlayer = 0x1A8;
using ZoneAtFn = BYTE*(__fastcall*)(void* world, void*, const void* position);
auto const ZoneAt = reinterpret_cast<ZoneAtFn>(0x6CB3C0);

void* __cdecl PickPlayer(const void* startPosition) {
    if (!InOurWorld()) return g_pickPlayer(startPosition);
    const DWORD uniform = *kChosenUniform;
    *kChosenUniform = 0;
    void* player = g_pickPlayer(startPosition);
    *kChosenUniform = uniform;
    void* guerrillas = FindCharacter(PlayerCharacterName);
    if (guerrillas && player != guerrillas) {
        BYTE* zone = startPosition ? ZoneAt(reinterpret_cast<void*>(kWorld), nullptr, startPosition) : nullptr;
        const BYTE* resource = zone ? *reinterpret_cast<BYTE* const*>(zone + kZoneSpawnResource) : nullptr;
        RFA_LOG_FIRST(16, "Player: the game picked %s in world \"%s\" (the zone at the start: %s, its spawn resource %s, that one's player %s); Guerrilla's %s instead",
                      player == FindCharacter("RF4_PC") ? "Armageddon's RF4_PC (Darius)" : "another character", WorldName(),
                      zone ? "found" : "none", resource ? "set" : "NONE", resource && *reinterpret_cast<void* const*>(resource + kResourcePlayer) ? "set" : "none",
                      PlayerCharacterName);
        player = guerrillas;
    }
    if (uniform && g_uniformsLogged < 5) {
        ++g_uniformsLogged;
        rfa::Log("Player: in world \"%s\" the world's own player, not the uniform chosen in Armageddon's menu (kept for its own levels)", WorldName());
    }
    return player;
}

// Writes which of the people are loaded, on their way, or neither (`after`: how long in the world).
// Which of the world's spawn resources (the names in mods\guerrilla_world\spawn_resource.xtbl starting "rfg_") the
// game's table holds (0x75EC70 by name), and the people that share a container handle (character +0x60): the table's
// loader drops a whole entry listing two characters with the same handle (0x75EFA0 → 0x75EC0D), and a zone naming a
// dropped one has no resource in force (Darius as the player, Armageddon's "Default" asked for the level).
constexpr DWORD kCharacterContainer = 0x60;
void ReportOurResources() {
    std::ifstream file(rfa::RootDir() + L"\\mods\\guerrilla_world\\spawn_resource.xtbl", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string held, dropped;
    for (size_t at = text.find("<Name>rfg_"); at != std::string::npos; at = text.find("<Name>rfg_", at + 1)) {
        const size_t from = at + 6, to = text.find('<', from);
        if (to == std::string::npos) break;
        const std::string name = text.substr(from, to - from);
        std::string& list = FindSpawnResource(name.c_str()) ? held : dropped;
        list += (list.empty() ? "" : ", ") + name;
    }
    std::vector<std::pair<DWORD, std::string>> containers;
    for (const std::string& name : g_people)
        if (void* character = FindCharacter(name.c_str()))
            containers.emplace_back(*reinterpret_cast<const DWORD*>(static_cast<BYTE*>(character) + kCharacterContainer), name);
    std::sort(containers.begin(), containers.end());
    std::string shared;
    for (size_t i = 1; i < containers.size(); ++i)
        if (containers[i].first == containers[i - 1].first && shared.size() < 1500) {
            char line[160];
            sprintf_s(line, "%s%s & %s (0x%08X)", shared.empty() ? "" : "; ", containers[i - 1].second.c_str(), containers[i].second.c_str(), containers[i].first);
            shared += line;
        }
    rfa::Log("Spawn resources: the game's table holds %s; DROPPED (not in the table): %s; people sharing a container handle: %s",
             held.empty() ? "none of ours" : held.c_str(), dropped.empty() ? "none" : dropped.c_str(), shared.empty() ? "none" : shared.c_str());
}

void ReportOurPeople(const char* after) {
    rfa::Log("Squads' names: %lu of their pool's %lu bytes used; spawn resources' names: %lu of %lu",
             *reinterpret_cast<const DWORD*>(kSquadNames + 0x4C), *reinterpret_cast<const DWORD*>(kSquadNames + 0x44),
             *reinterpret_cast<const DWORD*>(kResourceNames + 0x4C), *reinterpret_cast<const DWORD*>(kResourceNames + 0x44));
    std::string loaded, coming, not_loaded, missing;
    auto add = [](std::string& list, const std::string& name) { list += (list.empty() ? "" : ", ") + name; };
    for (const std::string& name : g_people) {
        void* character = FindCharacter(name.c_str());
        if (!character) add(missing, name);
        else if (CharacterLoaded(character)) add(loaded, name);
        else if (CharacterOnItsWay(character)) add(coming, name);
        else add(not_loaded, name);
    }
    rfa::Log("People %s in the world (the world's spawn resource %s): loaded: %s; on their way: %s; not loaded: %s%s%s", after,
             ResourceInForce(kPools, nullptr) ? "in force" : "not in force", loaded.empty() ? "none" : loaded.c_str(),
             coming.empty() ? "none" : coming.c_str(), not_loaded.empty() ? "none" : not_loaded.c_str(),
             missing.empty() ? "" : "; not in the game's character table (the people not built, or the table not handed over): ",
             missing.c_str());
}

// ---- Guerrilla's sides

constexpr DWORD kResetDispositions = 0x8A0350;
constexpr size_t kResetDispositionsPrologue = 5;  // push ebp / mov ebp,esp / push ecx / push esi
using ResetDispositionsFn = void(__cdecl*)();
ResetDispositionsFn g_resetDispositions;
auto const SetDisposition = reinterpret_cast<void(__cdecl*)(int team, int other, int disposition, bool bothWays)>(0x8A03A0);
auto& g_dispositions = *reinterpret_cast<int(*)[5][5]>(0x21DC8D0);

// Guerrilla's: Default_human_team_dispositions (RM 0x1310F38), row the team, column the other.
constexpr int kHostile = 0, kUnfriendly = 1, kFriendly = 3, kAllied = 4;
constexpr int kGuerrillaDispositions[4][4] = {
    {kAllied, kHostile, kFriendly, kHostile},     // Guerrilla's Red Faction (the player's team)
    {kHostile, kAllied, kUnfriendly, kHostile},   // the EDF
    {kFriendly, kUnfriendly, kAllied, kHostile},  // civilians
    {kHostile, kHostile, kHostile, kAllied},      // Marauders
};

bool DispositionsAreGuerrillas() {
    for (int t = 0; t < 4; ++t)
        for (int o = 0; o < 4; ++o)
            if (t != o && g_dispositions[t][o] != kGuerrillaDispositions[t][o]) return false;
    return true;
}

void SetGuerrillaDispositions() {
    if (DispositionsAreGuerrillas()) return;
    for (int t = 0; t < 4; ++t)
        for (int o = 0; o < 4; ++o)
            if (t != o) SetDisposition(t, o, kGuerrillaDispositions[t][o], false);
    rfa::Log("Sides: Guerrilla's (the EDF and the Marauders hostile to the player's side, civilians friendly to it and unfriendly to the EDF, the Marauders hostile to all)");
}

int g_levelStarts = 0;  // each level's start (a reload too): the people's reports are written again after it

void __cdecl ResetDispositions() {
    ++g_levelStarts;
    g_resetDispositions();
    if (InOurWorld()) SetGuerrillaDispositions();
}

// ---- A jump turned down, in the log
//
// Armageddon's jump (0x85A5A0, cdecl: the human and three flags; Guerrilla's human_jump) turns a jump
// down when the human can't jump now (its checks below; and the jump timers: Jump_Delay_ms after a
// landing, 1.1 s between the player's jumps), else sets one to come (+0x11E8 1) 0.1 s later (+0x11EC),
// with its height (+0x11F0). The mod writes in the log a jump turned down by one of its other checks
// (not the timers: a press again too soon is normal), with each check's value, for the first few after
// each world starts. (Written while Mason didn't leave the ground; the cause was his jump animations:
// Guerrilla's had a small hop of their own that held him down, and Armageddon's jump, fall and landing
// clips moved onto him fixed it.) Nothing is changed.
constexpr DWORD kHumanJump = 0x85A5A0;
constexpr size_t kHumanJumpPrologue = 6;  // push ebp / mov ebp,esp / sub esp,28h
using HumanJumpFn = void(__cdecl*)(void* human, bool first, bool second, bool third);
HumanJumpFn g_humanJump;
constexpr int kJumpsToLog = 12;
int g_jumpsLogged = 0;

// The jump's own checks (each as the jump calls it), to tell which turned one down.
auto const JumpBlocked = reinterpret_cast<bool(__cdecl*)(void* human)>(0x866D70);
auto const JumpBusy = reinterpret_cast<bool(__cdecl*)(void* human, int what)>(0x8669B0);
auto const ActionPlaying = reinterpret_cast<int(__thiscall*)(void* actions, int category)>(0x98DAA0);
auto const JumpRefused = reinterpret_cast<bool(__cdecl*)(void* human)>(0x8803D0);

const char* Whose(void* human) {
    void* character = *reinterpret_cast<void**>(static_cast<BYTE*>(human) + 0x4FC);
    if (character && character == FindCharacter(PlayerCharacterName)) return "Mason (rfg_PC)";
    return "Armageddon's character";
}

void __cdecl HumanJump(void* human, bool first, bool second, bool third) {
    g_humanJump(human, first, second, third);
    if (!human || g_jumpsLogged >= kJumpsToLog) return;
    BYTE* h = static_cast<BYTE*>(human);
    if (h[0x11E8]) return;  // set to come
    const bool blocked = JumpBlocked(human), busy = JumpBusy(human, 0), refused = JumpRefused(human);
    const int state = *reinterpret_cast<int*>(h + 0x770), mode = *reinterpret_cast<int*>(h + 0x730), action = ActionPlaying(h + 0x6F0, -1);
    const DWORD b0Bit21 = (*reinterpret_cast<DWORD*>(h + 0xB0) >> 21) & 1, b4Bit11 = (*reinterpret_cast<DWORD*>(h + 0xB4) >> 11) & 1;
    if (!blocked && !busy && !refused && state != 2 && mode != 4 && !action && !(b0Bit21 && !b4Bit11)) return;  // a timer: pressed again too soon
    ++g_jumpsLogged;
    rfa::LogDetail("Jump: %s in world \"%s\" (flags %d %d %d): turned down; checks: blocked %d, state +0x770 %d, +0x730 %d, "
             "+0xB0 bit 21 %lu, +0xB4 bit 11 %lu, busy %d, an action playing %d, refused %d",
             Whose(human), WorldName(), first, second, third, blocked, state, mode, b0Bit21, b4Bit11, busy, action, refused);
}

// ---- Mason's Nano Forge light ([GuerrillaPeople] MasonNanoLight=)
//
// Mason's character entry takes Armageddon's player's fields where Guerrilla's has none (GuerrillaBuilder.exe's
// people), among them its Persistent_Effects: one, nano_light, the Nano Forge's glow, an always-on cyan lamp
// (its effect's RF_Light01, colour 0 0.30 0.75) about 5 cm off muzzle_flash0 on his left forearm. Guerrilla's
// Mason has none, and next to his coat that lamp lights the coat's reflective tape. MasonNanoLight=1 (the
// default) keeps it, as Armageddon's player has it; 0 takes it out of his character's effects. The game
// starts a character's persistent effects when it makes the human, so a change shows the next time Mason is
// made (the level loaded again, or a save). The mod reads the setting once a second (the F7 Graphics window's
// switch saves it), and keeps his effects as the table gave them, to put the lamp back.
// A character's persistent effects, as the table reader keeps them (0x888FFE-0x8893BA): +0x20C a list of
// 16-byte entries (the effect's number first, as 0x4EF370 gives it for a name), +0x210 how many (8 at most).
constexpr size_t kEffectList = 0x20C, kEffectCount = 0x210, kEffectBytes = 16;
constexpr int kMostEffects = 8;
auto const EffectNumber = reinterpret_cast<int(__cdecl*)(const char* name)>(0x4EF370);
BYTE* g_masonEntry = nullptr;                      // his character entry, when last read
BYTE g_masonEffects[kMostEffects * kEffectBytes];  // its effects as the table gave them
int g_masonEffectCount = 0;
int g_nanoLightShown = -1;                         // the setting last applied (-1: none yet)

void ApplyMasonNanoLight() {
    BYTE* entry = static_cast<BYTE*>(FindCharacter(PlayerCharacterName));
    if (!entry) return;
    if (entry != g_masonEntry) {
        const int count = *reinterpret_cast<int*>(entry + kEffectCount);
        const BYTE* list = *reinterpret_cast<BYTE**>(entry + kEffectList);
        if (count < 0 || count > kMostEffects || (count > 0 && !list)) {
            rfa::Log("Mason's Nano Forge light: his character's persistent effects aren't as expected (%d); left as they are", count);
            g_masonEntry = entry;
            g_masonEffectCount = -1;
            return;
        }
        g_masonEntry = entry;
        g_masonEffectCount = count;
        if (count > 0) std::memcpy(g_masonEffects, list, count * kEffectBytes);
        g_nanoLightShown = -1;
    }
    if (g_masonEffectCount < 0) return;
    const int wanted = rfa::IniInt(L"GuerrillaPeople", L"MasonNanoLight", 1) != 0 ? 1 : 0;
    if (wanted == g_nanoLightShown) return;
    g_nanoLightShown = wanted;
    const int nanoLight = EffectNumber("nano_light");
    BYTE* list = *reinterpret_cast<BYTE**>(entry + kEffectList);
    int kept = 0;
    for (int i = 0; i < g_masonEffectCount; ++i) {
        const BYTE* effect = g_masonEffects + i * kEffectBytes;
        if (!wanted && nanoLight >= 0 && *reinterpret_cast<const int*>(effect) == nanoLight) continue;
        std::memcpy(list + kept * kEffectBytes, effect, kEffectBytes);
        ++kept;
    }
    *reinterpret_cast<int*>(entry + kEffectCount) = kept;
    rfa::Log("Mason's Nano Forge light: %s (MasonNanoLight=%d; %d of his %d persistent effects; shows the next time he is made: load the level again)",
             wanted ? "on, as Armageddon's player has it" : "off, as Guerrilla's Mason", wanted, kept, g_masonEffectCount);
}

// After the character table is read: his effects as it gave them, then the setting.
void MasonsTableRead() {
    g_masonEntry = nullptr;
    ApplyMasonNanoLight();
}

// ---- Once a second in the game's play (its gameplay update, 0x4FE860, cdecl)

constexpr DWORD kGameplayFrame = 0x4FE860;
constexpr size_t kGameplayFramePrologue = 8;  // push ebp / mov ebp,esp / push 0F4240h
using GameplayFrameFn = void(__cdecl*)();
GameplayFrameFn g_gameplayFrame;
DWORD g_lastCheck = 0, g_enteredAt = 0;
bool g_wasOurs = false;
int g_reports = 0;  // the people's reports written since entering the world (at 5 s and 30 s)

// ---- The spawn resource's changes in our worlds (each frame)
//
// The game changes spawn resource through a state machine (0x762730, cdecl, returns the state; the state at
// 0x19734CC): 1 lets go of what the new resource doesn't want, then asks the stream allocators to switch to
// their spawn mode (0x908DF0(1)) and goes to 2, which applies the resource (0x762600); 3 waits until it has
// loaded; 4 it is in force. In Armageddon's own levels the spawn resources' frame (0x7619D0, from the gameplay
// update at 0x4FE883) runs it every frame while a state is set (0x75FC10), but not in a Wrecking Crew match
// (0x511930: [0x10F83FC] isn't -1), which our worlds run as. Then only the mission system's update runs it
// (0x503DE0 at 0x503EC4, reached from the loading state through 0x6C9110, and only while a mission is set,
// [0x10F80C8], whose flags allow it). A first load got through; a reload after dying (die, ` + R) didn't: the
// state stayed at 1 for good, the zone's resource (rfg_dust) was never applied, so no one came, and with the
// allocators never switched every vehicle model failed "SLOT ALLOC FAIL" (the user's 03:05 and 03:15 runs,
// 2026-11-07). So in our worlds the mod runs the machine each frame while a state is set, as the game's frame
// does in its own levels.
auto const RunSpawnResourceChange = reinterpret_cast<int(__cdecl*)()>(0x762730);
auto const InWreckingCrewMatch = reinterpret_cast<bool(__cdecl*)()>(0x511930);

void RunSpawnResourceChanges() {
    if (!InWreckingCrewMatch() || !ResourceInForce(kPools, nullptr) || !InOurWorld()) return;
    static int loggedStart = -1;
    const int before = *static_cast<const int*>(kPools);
    if (loggedStart != g_levelStarts) {
        loggedStart = g_levelStarts;
        RFA_LOG_DETAIL_FIRST(40, "Spawn resource: the mod runs its change each frame in world \"%s\" (a Wrecking Crew match; state %d)", WorldName(), before);
    }
    const int after = RunSpawnResourceChange();
    if (after != before) RFA_LOG_DETAIL_FIRST(60, "Spawn resource: its change went from state %d to %d (run by the mod; 4 = in force)", before, after);
}

void __cdecl GameplayFrame() {
    g_mainThread = GetCurrentThreadId();
    g_gameplayFrame();
    RunLeftTheWorld();
    RunSpawnResourceChanges();
    RunFallsWatch();
    GiveConvertsTheirSet();
    AmbientFrame(InOurWorld());  // Guerrilla's walkers (ambient.cpp)
    AmbushFrame(InOurWorld());   // Guerrilla's Marauder ambushes (ambush.cpp)
    StoryFrame(InOurWorld());    // Guerrilla's story people at the safehouses (story.cpp)
    ActionsFrame(InOurWorld());  // Guerrilla's action nodes for the people under them (actions.cpp)
    GuardsFrame(InOurWorld());   // Guerrilla's guard posts (guards.cpp)
    VoicesFrame(InOurWorld());   // Guerrilla's pain, death, spotting and grenade lines (voices.cpp)
    SquadLevelsFrame(InOurWorld());  // Guerrilla's squads' weapons by level (squad_levels.cpp)
    const DWORD now = GetTickCount();
    if (now - g_lastCheck < 1000) return;
    g_lastCheck = now;
    const bool ours = InOurWorld();
    static int seenStarts = 0;
    if (ours && g_levelStarts != seenStarts) {  // the level loaded again in our world (a reload, a checkpoint)
        g_enteredAt = now;
        g_reports = 0;
    }
    seenStarts = g_levelStarts;
    if (ours != g_wasOurs) {
        g_wasOurs = ours;
        g_enteredAt = now;
        g_reports = 0;
        g_jumpsLogged = 0;
        rfa::Log("World \"%s\": %s", WorldName(), ours ? "Guerrilla's people and sides" : "Armageddon's own");
        MatchGuerrillaAvoidTime(ours);
    }
    ApplyMasonNanoLight();
    if (!ours) return;
    if (g_resetDispositions) SetGuerrillaDispositions();
    RunTransitionCells();
    if (!g_people.empty() && g_reports < 2 && now - g_enteredAt >= (g_reports == 0 ? 5000u : 30000u)) {
        ReportOurPeople(g_reports == 0 ? "5 s" : "30 s");
        if (g_reports == 0) ReportOurResources();
        ++g_reports;
    }
}

}  // namespace

void guerrilla_people::Init() {
    if (rfa::ReadLogSwitch(L"GuerrillaPeople")) rfa::Log("Log=1: routine lines and the diagnoses (\"[guerrilla_people:log]\") are on");
    if (!rfa::game::IsSupportedBuild()) {
        rfa::Log("Not the publicbeta build; nothing to do");
        return;
    }
    // Before the game's own initializers run (the mods are loaded first): always, see above.
    g_poolInit = reinterpret_cast<PoolInitFn>(rfa::HookFunction(kPoolInit, kPoolInitPrologue, reinterpret_cast<void*>(&PoolInit)));
    if (!g_poolInit) rfa::Log("Skeletons: could not hook the memory pools' set-up; their pool keeps its 576 KB (too few for Guerrilla's people and vehicles)");
    g_animStringsInit = reinterpret_cast<AnimStringsInitFn>(rfa::HookFunction(kAnimStringsInit, kAnimStringsInitPrologue, reinterpret_cast<void*>(&AnimStringsInit)));
    if (!g_animStringsInit) rfa::Log("Animation names: could not hook the pool's start-up; it keeps its 128 KB (too few for Guerrilla's people and vehicles)");
    MakeRoomForPatrols();
    MakeRoomForSquadMembers();
    MakeRoomForLoadouts();
    MakeRoomForSpawnResources();
    if (rfa::IniInt(L"GuerrillaPeople", L"Enabled", 1) == 0) {
        rfa::Log("Off ([GuerrillaPeople] Enabled=0)");
        return;
    }
    g_worlds = SplitList(rfa::IniString(L"GuerrillaPeople", L"Worlds", L"rfg"));
    g_skeletons = ReadList(L"rigs.txt");
    g_people = ReadList(L"people.txt");
    StartAmbient();
    StartAmbushes();
    StartStory();
    StartActions();
    StartGuards();
    StartVoices();
    StartSquadLevels();
    if (!g_skeletons.empty()) {
        g_readCharacters = reinterpret_cast<ReadCharactersFn>(rfa::HookFunction(kReadCharacters, kReadCharactersPrologue, reinterpret_cast<void*>(&ReadCharacters)));
        if (g_readCharacters) rfa::Log("Skeletons: %u of Guerrilla's people's own to load before the character table", static_cast<unsigned>(g_skeletons.size()));
        else rfa::Log("Skeletons: could not hook the character table's reading; Guerrilla's people's skeletons won't be loaded, and a character naming one stops the game");
    }
    g_loadAnimationArchive = reinterpret_cast<LoadAnimationArchiveFn>(rfa::HookFunction(kLoadAnimationArchive, kLoadAnimationArchivePrologue, reinterpret_cast<void*>(&LoadAnimationArchive)));
    if (!g_loadAnimationArchive) rfa::Log("Animations: could not hook the animation archive's loading; Guerrilla's people's animations won't be loaded (they stand still)");
    g_setLevelResource = reinterpret_cast<SetLevelResourceFn>(rfa::HookFunction(kSetLevelResource, kSetLevelResourcePrologue, reinterpret_cast<void*>(&SetLevelResource)));
    g_applyLevelResource = reinterpret_cast<ApplyLevelResourceFn>(rfa::HookFunction(kApplyLevelResource, kApplyLevelResourcePrologue, reinterpret_cast<void*>(&ApplyLevelResource)));
    if (!g_setLevelResource || !g_applyLevelResource)
        rfa::Log("Spawn resource: could not hook the level's spawn resource; Armageddon's own people may load in our worlds");
    g_pickPlayer = reinterpret_cast<PickPlayerFn>(rfa::HookFunction(kPickPlayer, kPickPlayerPrologue, reinterpret_cast<void*>(&PickPlayer)));
    if (!g_pickPlayer) rfa::Log("Player: could not hook the player's choice; a DLC uniform chosen in Armageddon's menu still wins in our worlds");
    g_givePlayerSet = reinterpret_cast<GivePlayerSetFn>(rfa::HookFunction(kGivePlayerSet, kGivePlayerSetPrologue, reinterpret_cast<void*>(&GivePlayerSet)));
    if (!g_givePlayerSet) rfa::Log("Player: could not hook the player's animation set; Guerrilla's player gets Armageddon's (and stands still)");
    g_mayThrow = reinterpret_cast<MayThrowFn>(rfa::HookFunction(kMayThrow, kMayThrowPrologue, reinterpret_cast<void*>(&MayThrow)));
    if (g_mayThrow) rfa::Log("Grenades: in our worlds an NPC throws only when its own wait and the shared one are both over, as Guerrilla");
    else rfa::Log("Grenades: could not hook the throw check; Armageddon's lets the last thrower throw again at once");
    g_drawFirearmEnter = reinterpret_cast<DrawEnterFn>(rfa::HookFunction(kDrawFirearmEnter, kDrawEnterPrologue, reinterpret_cast<void*>(&DrawFirearmEnter)));
    g_drawMeleeEnter = reinterpret_cast<DrawEnterFn>(rfa::HookFunction(kDrawMeleeEnter, kDrawEnterPrologue, reinterpret_cast<void*>(&DrawMeleeEnter)));
    if (!g_drawFirearmEnter || !g_drawMeleeEnter)
        rfa::Log("Guerrilla men: could not hook the draw actions; a guerrilla in civilian clothes keeps his civilian set when he draws");
    g_resetDispositions = reinterpret_cast<ResetDispositionsFn>(rfa::HookFunction(kResetDispositions, kResetDispositionsPrologue, reinterpret_cast<void*>(&ResetDispositions)));
    if (!g_resetDispositions) rfa::Log("Sides: could not hook the teams' reset; Armageddon's dispositions stay");
    if (rfa::DetailLog()) {  // a diagnosis only: it changes nothing in the game
        g_humanJump = reinterpret_cast<HumanJumpFn>(rfa::HookFunction(kHumanJump, kHumanJumpPrologue, reinterpret_cast<void*>(&HumanJump)));
        if (!g_humanJump) rfa::Log("Jump: could not hook the jump; a jump turned down isn't written in the log");
    }
    g_gameplayFrame = reinterpret_cast<GameplayFrameFn>(rfa::HookFunction(kGameplayFrame, kGameplayFramePrologue, reinterpret_cast<void*>(&GameplayFrame)));
    if (!g_gameplayFrame) rfa::Log("Could not hook the gameplay update; Guerrilla's sides are set only at a level's start, and the people's loading isn't reported");
    g_squadStaticSpawn = reinterpret_cast<SquadStaticSpawnFn>(rfa::HookFunction(kSquadStaticSpawn, kSquadStaticSpawnPrologue, reinterpret_cast<void*>(&SquadStaticSpawn)));
    if (!g_squadStaticSpawn) rfa::Log("Squads: could not hook the static spawn; a standing squad of several with no spawn points under its node doesn't spawn");
    g_nodeSpawn = reinterpret_cast<NodeSpawnFn>(rfa::HookFunction(kNodeSpawn, kNodeSpawnPrologue, reinterpret_cast<void*>(&NodeSpawn)));
    if (!g_nodeSpawn) rfa::Log("Squads: could not hook the squad nodes' spawn; Guerrilla's guard posts' squads aren't leashed to their boxes (they chase the player)");
    g_humanPath = reinterpret_cast<HumanPathFn>(rfa::HookFunction(kHumanPath, kHumanPathPrologue, reinterpret_cast<void*>(&HumanPath)));
    g_cellSearch = reinterpret_cast<CellSearchFn>(rfa::HookFunction(kCellSearch, kCellSearchPrologue, reinterpret_cast<void*>(&CellSearch)));
    if (!g_cellSearch) rfa::Log("Goals: could not hook the search between two cells; a goal the game moves onto the mesh is still searched for past its budget (\"Failed pathfind\")");
    if (!g_humanPath) rfa::Log("Guard posts: could not hook the people's path query; a guard post's squad still asks for ways out of its post (\"Failed pathfind\" where the way is too long)");
    g_navCheck = reinterpret_cast<NavCheckFn>(rfa::HookFunction(kNavCheck, kNavCheckPrologue, reinterpret_cast<void*>(&NavCheck)));
    if (!g_navCheck) rfa::Log("Transitions: could not hook the load's navigation check; transitions of the zones round the start may stay without their cell");
    g_navpointSerialize = reinterpret_cast<NavpointSerializeFn>(
        rfa::HookFunction(kNavpointSerialize, kNavpointSerializePrologue, reinterpret_cast<void*>(&NavpointSerialize)));
    if (!g_navpointSerialize)
        rfa::Log("Navpoints: could not hook the navpoints' serializer; a transition made at a blown hole may keep a stale version and stay without its nav cell");
    g_leftTheWorld = reinterpret_cast<LeftTheWorldFn>(rfa::HookFunction(kLeftTheWorld, kLeftTheWorldPrologue, reinterpret_cast<void*>(&LeftTheWorld)));
    if (!g_leftTheWorld) rfa::Log("Falls: could not hook the game's handling of what leaves the world; who falls out isn't written here");
    g_groundHeight = reinterpret_cast<GroundHeightFn>(rfa::HookFunction(kGroundHeight, kGroundHeightPrologue, reinterpret_cast<void*>(&BackFromFarGround)));
    if (!g_groundHeight) rfa::Log("Far people: could not hook the ground's height; a person back from far off more than 1 m under the ground is put at height 0 and falls out of the world");
    FarModeByRealDistance();
    rfa::Log("Guerrilla's people in world(s) %ls: %u character(s) made by editor\\GuerrillaBuilder.exe", rfa::IniString(L"GuerrillaPeople", L"Worlds", L"rfg").c_str(),
             static_cast<unsigned>(g_people.size()));
}


// ---- Markers for Guerrilla's minimap (the HUD session's request of 2026-11-12; the rules: the shared note
// minimap-icons-survey.md, items 10, 12 and 25)
//
// Guerrilla puts these on its minimap, besides the people blips the HUD already draws:
//   12/25 a squad's map icon (object_squad's map_icon property, SQUAD_PROP_MAP_ICON, read by object_squad::serialize
//         RM 0xAAD3DE and squad_creation_parameters::create_squad RM 0x7756ED): its picture on every living member,
//         plain (NORMAL), sticky (STICKY) or only the 3 nearest sticky (CLOSEST_STICKY; map_icon_types 0, 1, 2;
//         object_squad::assign_icons_to_members RM 0xA3F700). None of Guerrilla's 663 world squad nodes has one; they
//         come with missions' and activities' squads, so whatever ports those names the squad here.
//   25    the player's ally and escort squads (player +0x24C4 / +0x24C8): a sticky ui_map_icon_star_blue on each
//         member (ally_squad_reassign_icons RM 0xA33FC0, escort_squad_reassign_icons RM 0xA34040). Armageddon has
//         one script-driven player squad instead (player_squad_add_npc ...); nothing fills either in our worlds yet,
//         so whatever makes them (the alert session's morale converts, a mission's escort) names each person here.
//   10    a civilian converted by morale (morale_convert_single_npc RM 0x788EF0 -> add_converted_guerrilla RM
//         0x874320, up to 5 at once): ui_hud_house_arrest_head_icon in gold (240, 200, 52) on the person for 500 ms,
//         shrinking and fading, each next one starting 250 ms after the one before, at any distance
//         (render_guerrilla_converts RM 0x860020). The marker's age says where it is in that.
// A person in a vehicle is marked at the vehicle (Guerrilla's render_icons uses the vehicle's place). Markers only in
// our worlds; the dead and the gone are left out.
// (The marker's struct, GuerrillaPeopleMarker, is in people_api.h.)

namespace {
constexpr int kStickyPlacement = 1, kConvertPlacement = 3;
constexpr DWORD kConvertColor = 0xFFF0C834;   // (240, 200, 52)
constexpr int kConvertShown = 500, kConvertGap = 250, kMostConverts = 5;  // RM 0x860020, 0x874320
const char* const kAllyStar = "ui_map_icon_star_blue";
const char* const kConvertHead = "ui_hud_house_arrest_head_icon";
constexpr DWORD kHumanVehicle = 0x1160;       // the vehicle a human is in (as guerrilla_vehicles and the HUD read it)
struct SquadIcon { std::string icon; int placement; };
struct Convert { DWORD handle; int since; };
SRWLOCK g_markersLock = SRWLOCK_INIT;
std::unordered_map<DWORD, SquadIcon> g_squadIcons;       // squad handle -> its map icon
std::unordered_map<DWORD, int> g_playerSquadPeople;      // person -> 1 ally, 2 escort
std::vector<Convert> g_converts;                         // the pops still to show, oldest first
int MarkerTime() { return *reinterpret_cast<const int*>(0x1121300); }  // the game's ms (stops while it is paused)

// Where a person's marker goes: the vehicle it is in, else itself.
void MarkerPlace(BYTE* human, float* at) {
    const DWORD vehicleHandle = *reinterpret_cast<const DWORD*>(human + kHumanVehicle);
    const BYTE* vehicle = vehicleHandle == kNoHandle ? nullptr : WorldObject(reinterpret_cast<void*>(kWorld), nullptr, vehicleHandle);
    const float* from = reinterpret_cast<const float*>((vehicle ? vehicle : human) + kObjectPlace);
    at[0] = from[0], at[1] = from[1], at[2] = from[2];
}
}  // namespace

// A squad's map icon (Guerrilla's map_icon: the picture's name and its placement, 0 plain, 1 sticky, 2 closest sticky),
// drawn on each living member; a null or empty picture takes it off.
void GuerrillaPeople_SetSquadMapIcon(DWORD squad, const char* icon, int placement) {
    if (!guerrilla_people::Running()) return;
    AcquireSRWLockExclusive(&g_markersLock);
    if (!icon || !*icon) g_squadIcons.erase(squad);
    else g_squadIcons[squad] = {icon, placement < 0 || placement > 2 ? 0 : placement};
    ReleaseSRWLockExclusive(&g_markersLock);
}

// A person joins (1 ally, 2 escort) or leaves (0) the player's squads: Guerrilla's sticky blue star on them.
void GuerrillaPeople_SetPlayerSquadMember(DWORD person, int kind) {
    if (!guerrilla_people::Running()) return;
    AcquireSRWLockExclusive(&g_markersLock);
    if (kind == 1 || kind == 2) g_playerSquadPeople[person] = kind;
    else g_playerSquadPeople.erase(person);
    ReleaseSRWLockExclusive(&g_markersLock);
}

// A civilian converted by morale: the gold head's pop on them, queued as Guerrilla's (5 at most, 250 ms apart).
void GuerrillaPeople_Converted(DWORD person) {
    if (!guerrilla_people::Running()) return;
    AcquireSRWLockExclusive(&g_newConvertsLock);
    g_newConverts.push_back(person);  // Guerrilla's men's set as he joins (GiveConvertsTheirSet)
    ReleaseSRWLockExclusive(&g_newConvertsLock);
    AcquireSRWLockExclusive(&g_markersLock);
    const int now = MarkerTime();
    const int since = g_converts.empty() ? now : std::max(now, g_converts.back().since + kConvertGap);
    if (static_cast<int>(g_converts.size()) < kMostConverts) g_converts.push_back({person, since});
    ReleaseSRWLockExclusive(&g_markersLock);
}

// The markers to draw now, up to `capacity`; how many were written. Squads' icons and the player's squads first, then
// the converts' pops.
int GuerrillaPeople_MapMarkers(GuerrillaPeopleMarker* out, int capacity) {
    if (!out || capacity <= 0 || !guerrilla_people::Running() || !InOurWorld()) return 0;
    void* world = reinterpret_cast<void*>(kWorld);
    int written = 0;
    AcquireSRWLockExclusive(&g_markersLock);
    if (!g_squadIcons.empty() || !g_playerSquadPeople.empty()) {
        const int people = HumanCount(world, nullptr);
        for (int i = 0; i < people && written < capacity; ++i) {
            BYTE* human = HumanAt(world, nullptr, i);
            if (!human || GoneOrDead(human)) continue;
            const DWORD handle = ObjectHandle(human, nullptr);
            if (auto ally = g_playerSquadPeople.find(handle); ally != g_playerSquadPeople.end()) {
                GuerrillaPeopleMarker& m = out[written++];
                m = {handle, 0, 0, 0, kAllyStar, kStickyPlacement, 0, -1};
                MarkerPlace(human, &m.x);
                if (written >= capacity) break;
            }
            BYTE* squad = SquadOfNpc(human);
            auto icon = squad ? g_squadIcons.find(ObjectHandle(squad, nullptr)) : g_squadIcons.end();
            if (icon == g_squadIcons.end()) continue;
            GuerrillaPeopleMarker& m = out[written++];
            m = {handle, 0, 0, 0, icon->second.icon.c_str(), icon->second.placement, 0, -1};
            MarkerPlace(human, &m.x);
        }
    }
    const int now = MarkerTime();
    while (!g_converts.empty() && now - g_converts.front().since > kConvertShown) g_converts.erase(g_converts.begin());
    for (const Convert& c : g_converts) {
        if (written >= capacity) break;
        if (now < c.since) break;  // not begun yet (each starts 250 ms after the one before)
        BYTE* human = WorldObject(world, nullptr, c.handle);
        if (!human || GoneOrDead(human)) continue;
        GuerrillaPeopleMarker& m = out[written++];
        m = {c.handle, 0, 0, 0, kConvertHead, kConvertPlacement, kConvertColor, now - c.since};
        MarkerPlace(human, &m.x);
    }
    ReleaseSRWLockExclusive(&g_markersLock);
    return written;
}


// ---- For the alert session's morale, converts and fighters (2026-11-13; the shared note activities-morale-plan.md, steps 4-6)
//
// Guerrilla's guerrilla squads (guerilla_squad_1 to _6) name a team and a size; their members come from the Guerrilla
// team's squad_progression at the world's tech level, with the zone's human group giving their looks (its progression
// names Jayde). The world build writes each as one version a district group (GuerrillaDistricts.SquadFor:
// rfg_<group code>_gsq_<n>, the group's civilians with the progression's level-1 loadouts), for every group a zone uses.
// This names the version for a place: the zone's spawn resource (ambient.txt) gives its group. Guerrilla's are 1, 2, 3,
// 4 and 6 (each its size; there is no guerilla_squad_5), so 5 has none; nor has a zone with no group (the wastes).
const char* GuerrillaPeople_GuerrillaSquadAt(int size, float x, float z) {
    if (size < 1 || size > 6 || size == 5 || !guerrilla_people::Running() || !InOurWorld()) return nullptr;
    const char* resource = AmbientZoneResource(x, z);
    if (!resource || _strnicmp(resource, "rfg_", 4) != 0 || _stricmp(resource, "rfg_Default") == 0) return nullptr;
    // The group's code: the first two letters of each word of the resource's name after "rfg_" (rfg_dust_parker: dupa).
    std::string code;
    for (const char* word = resource + 4; *word;) {
        const char* end = strchr(word, '_');
        const size_t length = end ? static_cast<size_t>(end - word) : strlen(word);
        code.append(word, std::min<size_t>(length, 2));
        if (!end) break;
        word = end + 1;
    }
    if (code.empty()) return nullptr;
    static SRWLOCK lock = SRWLOCK_INIT;
    static std::unordered_map<std::string, std::string> names;  // kept, so the pointer stays good
    char name[32];
    sprintf_s(name, "rfg_%s_gsq_%d", code.c_str(), size);
    AcquireSRWLockExclusive(&lock);
    const char* kept = names.emplace(name, name).first->second.c_str();
    ReleaseSRWLockExclusive(&lock);
    return kept;
}

// A walker another mod takes over (a civilian turned guerrilla by morale): the walkers let it go to the game's AI.
// The walkers also let go on their own of one whose team changes.
void GuerrillaPeople_LeaveToGame(DWORD person) {
    if (guerrilla_people::Running()) AmbientLeaveToGame(person);
}

// For guerrilla_campaign's reinforcements (Guerrilla's build_cell_info): an open nav cell whose centroid is within `within`
// metres of (x, y, z), on the people's mesh or (vehicles 1) the vehicles'; 1 and its centroid in out[3], or 0.
int GuerrillaPeople_OpenNavCell(float x, float y, float z, float within, int vehicles, float* out) {
    const float probe[3] = {x, y, z};
    return out && guerrilla_people::Running() && OpenCellNear(probe, within, vehicles != 0, out) ? 1 : 0;
}

// For guerrilla_campaign's reinforcements (Guerrilla's pick_desired_reinforcement_group): how far (x, y, z) is from the
// nearest border edge of the loaded people's nav mesh, in metres; -1 when none is loaded.
float GuerrillaPeople_NavMeshDistance(float x, float y, float z) {
    if (!guerrilla_people::Running()) return -1.0f;
    const float point[3] = {x, y, z};
    return NearestNavMeshEdge(point);
}

// Guerrilla's disable_safehouse_vip (its scripts' action; safehouse_vip_disable RM 0x7909F0): a VIP character (by its
// name: Carmen, Jorge, Kyle) left out of the safehouses' choice (disable non-zero) or let back in. For the campaign's
// scripts. Not saved yet (Guerrilla's safehouse_vip_save).
void GuerrillaPeople_DisableSafehouseVip(const char* character, int disable) {
    if (guerrilla_people::Running()) StoryDisableVip(character, disable != 0);
}

// A squad sent out from its post: its leash is taken off (type 0, no anchor; 0x810990), and with it the refusal of
// ways out of a guard post's box (HumanPath, which only holds squads on the box leash, type 4).
void GuerrillaPeople_ReleaseLeash(DWORD squadHandle) {
    if (!guerrilla_people::Running()) return;
    GuardsReleaseSquad(squadHandle);  // its members leave their guard nodes (guards.cpp)
    BYTE* squad = squadHandle == kNoHandle ? nullptr : WorldObject(reinterpret_cast<void*>(kWorld), nullptr, squadHandle);
    if (!squad) return;
    const int type = *reinterpret_cast<const int*>(squad + kSquadLeashType);
    if (type == 0) return;
    SetLeash(squad, nullptr, 0, kNoHandle);
    RFA_LOG_DETAIL_FIRST(32, "Squads: squad 0x%08X's leash (type %d, anchor 0x%08X) taken off: sent out from its post", squadHandle, type,
                  *reinterpret_cast<const DWORD*>(squad + kSquadLeashAnchor));
}
