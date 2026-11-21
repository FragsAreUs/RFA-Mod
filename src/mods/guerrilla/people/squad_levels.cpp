// Guerrilla's squads' weapons by level: as the campaign's levels rise, the squads made from then on carry better
// guns. A part of guerrilla_people.
//
// Guerrilla (squad_progression.xtbl, four sets: Primary EDF, Reinforcement EDF, Marauder, Guerrilla; squad_level_1
// to _8, a weighted list of character and loadout each): object_squad::spawn_squad_members (RM 0xA6FEA0) asks
// squad_progression_member_human_info (0x7A5660) for each member's character and loadout at its team's level
// (get_progression_level 0x78CC70: the tech level for guerrillas, the EDF progression for the EDF, the Marauder
// progression for Marauders; level n is squad_level_(n+1)), and the loadout is given as the human is made
// (squad_loadout_create 0x774030).
//
// Armageddon kept the member list and the loadout as the human is made: its squad's member list (0x80E7E0, thiscall:
// the squad, the spawn record; true when it has members) puts each member (0x58 bytes at the spawn's +0x10: +0 its
// human_info, +0x10 its loadout's index) from the squad's definition (+0x154), and the human's making gives that
// loadout (0x8605E0 reads it as "load_out", then 0x50D360). Ours are written at level 1 (GuerrillaSquads.cs). Here
// the list is wrapped: in our worlds, for a squad of a set (squad_levels.txt's squad lines; the EDF's reinforcements
// as the campaign names them, GuerrillaPeople_SquadSet), each member's loadout becomes a weighted pick of its set's
// level (the campaign's GuerrillaCampaign_ProgressionLevel); for the EDF among the entries of the member's own
// character where there are any, as Guerrilla picks the character with them. The game then gives that loadout.
//
// Not as Guerrilla's: the EDF's characters stay the level-1 ones the squads were written with (Guerrilla's higher
// levels bring Elites and riot shields, picked among the characters streamed in); only new squads change (as in
// Guerrilla, people already made keep theirs). Civilians' squads are left unarmed (Guerrilla gives its civilians the
// guerrillas' loadouts too; ours get theirs when they join, converts.cpp). [GuerrillaPeople] SquadLevels=0 turns it
// off.
#define RFA_MOD_NAME "guerrilla_people"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "people_api.h"
#include "campaign/campaign_api.h"
#include "squad_levels.h"
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr DWORD kFillMembers = 0x80E7E0;
constexpr size_t kFillMembersPrologue = 6;  // push ebp / mov ebp,esp / sub esp,68h
const BYTE kFillMembersStart[kFillMembersPrologue] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x68};
using FillMembersFn = bool(__fastcall*)(BYTE* squad, void* unused, BYTE* spawn);
FillMembersFn g_fillMembers = nullptr;

constexpr DWORD kSquadHandle = 0x68, kSquadDefinition = 0x154;
constexpr DWORD kSpawnMembers = 0x10, kSpawnMemberCount = 0x18, kMemberSize = 0x58, kMemberInfo = 0, kMemberLoadout = 0x10;
constexpr DWORD kInfoName = 0x24;  // human_info: its name
auto const DefinitionNamed = reinterpret_cast<BYTE*(__cdecl*)(const char* name)>(0x50C350);  // squads.xtbl's, or null
auto const LoadoutIndex = reinterpret_cast<int(__cdecl*)(const char* name)>(0x50D4A0);       // squad_loadout.xtbl's, -1 none

constexpr int kLevels = 8;
struct Entry {
    std::string character, loadout;
    double weight = 1;
    int index = -2;  // the loadout's index (-2: not looked up yet, -1: the game hasn't it)
};
using Levels = std::array<std::vector<Entry>, kLevels>;
std::map<std::string, Levels> g_sets;                     // by set name
std::unordered_map<std::string, std::string> g_squadSets;  // squad name (lower case) -> set
std::unordered_map<const BYTE*, std::string> g_definitionSets;
bool g_definitionsMapped = false;
std::vector<std::string> g_civilianSquads;                 // the civilian squads' names (squad_levels.txt's civilian lines)
std::unordered_map<const BYTE*, bool> g_civilianDefinitions;

std::mutex g_lock;  // the two below (the campaign names its squads from its own frame, on the game's thread too)
std::unordered_map<unsigned, std::string> g_handleSets;  // squads named by another mod
std::string g_nextSet;                                   // the set of squads made until it's cleared

bool g_ours = false;
unsigned g_changed = 0;

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

void ReadLevels() {
    std::ifstream file(rfa::RootDir() + L"\\mods\\guerrilla_world\\squad_levels.txt");
    for (std::string line; std::getline(file, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        std::istringstream in(line);
        for (std::string field; std::getline(in, field, '\t');) f.push_back(field);
        if (f[0] == "level" && f.size() >= 6) {
            const int level = std::atoi(f[2].c_str());
            if (level < 1 || level > kLevels) continue;
            Entry entry;
            entry.character = f[3] == "-" ? "" : f[3];
            entry.loadout = f[4];
            entry.weight = std::atof(f[5].c_str());
            if (entry.weight > 0) g_sets[f[1]][level - 1].push_back(entry);
        } else if (f[0] == "squad" && f.size() >= 3) {
            g_squadSets[Lower(f[1])] = f[2];
        } else if (f[0] == "civilian" && f.size() >= 2) {
            g_civilianSquads.push_back(f[1]);
        }
    }
}

// The team whose level a set follows (Guerrilla's get_progression_level): 1 the EDF, 3 the Marauders, 0 guerrillas.
int TeamOf(const std::string& set) {
    if (set.find("EDF") != std::string::npos) return 1;
    if (set == "Marauder") return 3;
    return 0;
}

// The definitions of the squads named in the file, found once the game has read squads.xtbl.
void MapDefinitions() {
    if (g_definitionsMapped) return;
    for (const auto& [name, set] : g_squadSets)
        if (BYTE* definition = DefinitionNamed(name.c_str())) g_definitionSets[definition] = set;
    for (const std::string& name : g_civilianSquads)
        if (BYTE* definition = DefinitionNamed(name.c_str())) g_civilianDefinitions[definition] = true;
    g_definitionsMapped = !g_definitionSets.empty() || !g_civilianDefinitions.empty();
}

// ---- Civilian squads at orange and red alert (the live session, step 2, 2026-11-18)
//
// Guerrilla's object_spawn_node_do_frame (RM 0xAAC9D0), every 250 to 500 ms: at orange or red, do_alert_despawn
// (0xA6C9D0) asks every civilian squad (its team, the override or its definition's, civilian; not a managed layer's) to
// despawn all its members. Here, in our worlds: each person of one of our civilian squads (the builder's civilian lines)
// who is still a civilian (a convert isn't) and on foot is taken away once nobody sees them, as Guerrilla's
// can_delete_despawning_npc lets a despawning person go: out of view for a second 20 m or more away, or 120 m away.
// Guerrilla's respawn as the alert falls below orange (do_alert_respawn 0xA93FF0: the non-civilian nodes with a respawn
// speed roll their chance) isn't ported: Armageddon's node respawn handling isn't found.
using CountFn = int(__fastcall*)(void* world, void*);
using AtFn = BYTE*(__fastcall*)(void* world, void*, int index);
using ObjectFn = BYTE*(__fastcall*)(void* world, void*, DWORD handle);
using RemoveFn = int(__fastcall*)(void* world, void*, DWORD handle, int flag);
using InViewFn = bool(__cdecl*)(const float* centre, float radius);
auto const HumanCount = reinterpret_cast<CountFn>(0x424480);  // world::get_num_objects<npc>
auto const HumanAt = reinterpret_cast<AtFn>(0x4244D0);
auto const ObjectByHandle = reinterpret_cast<ObjectFn>(0x6CB9E0);
auto const RemoveObject = reinterpret_cast<RemoveFn>(0x6D56A0);
auto const InView = reinterpret_cast<InViewFn>(0x768400);
void* const kWorld = reinterpret_cast<void*>(0x1870630);
const auto& g_playerObject = *reinterpret_cast<BYTE* const*>(0x2271490);
constexpr DWORD kObjectHandle = 0x68, kTeam = 0x13B4, kInVehicle = 0x1160, kAi = 0x174C, kAiSquad = 0x7D4, kPosition = 0x04;
constexpr int kCivilian = 2;
constexpr float kDeleteNear = 20.0f, kDeleteFar = 120.0f;
bool g_alertDespawn = false;  // [GuerrillaPeople] CiviliansAtAlert
std::unordered_map<DWORD, int> g_unseenSince;  // a civilian being despawned: when they went out of view (-1 in view)
int g_nextAlertCheck = 0;
unsigned g_civiliansRemoved = 0;

int GameClock() { return *reinterpret_cast<const int*>(0x1121300); }

bool InCivilianSquad(const BYTE* human) {
    const BYTE* ai = *reinterpret_cast<BYTE* const*>(human + kAi);
    const DWORD squadHandle = ai ? *reinterpret_cast<const DWORD*>(ai + kAiSquad) : 0xFFFFFFFF;
    if (squadHandle == 0xFFFFFFFF) return false;
    const BYTE* squad = ObjectByHandle(kWorld, nullptr, squadHandle);
    if (!squad) return false;
    MapDefinitions();
    return g_civilianDefinitions.count(*reinterpret_cast<BYTE* const*>(squad + kSquadDefinition)) != 0;
}

void CivilianAlertFrame() {
    const int now = GameClock();
    if (now < g_nextAlertCheck && g_nextAlertCheck - now <= 500) return;
    g_nextAlertCheck = now + 250 + rand() % 251;
    const int level = GuerrillaCampaign_Alert(nullptr);
    if (level < 2 || !g_playerObject || g_civilianSquads.empty()) {
        g_unseenSince.clear();
        return;
    }
    const float* here = reinterpret_cast<const float*>(g_playerObject + kPosition);
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!human || human == g_playerObject || *reinterpret_cast<const int*>(human + kTeam) != kCivilian) continue;
        if (*reinterpret_cast<const DWORD*>(human + kInVehicle) != 0xFFFFFFFF || !InCivilianSquad(human)) continue;
        const DWORD handle = *reinterpret_cast<const DWORD*>(human + kObjectHandle);
        const float* at = reinterpret_cast<const float*>(human + kPosition);
        const float dx = at[0] - here[0], dy = at[1] - here[1], dz = at[2] - here[2];
        const float distance = sqrtf(dx * dx + dy * dy + dz * dz);
        const float head[3] = {at[0], at[1] + 1.0f, at[2]};
        int& since = g_unseenSince.try_emplace(handle, -1).first->second;
        if (InView(head, 1.0f)) since = -1;
        else if (since < 0) since = now;
        if (distance > kDeleteFar || (distance > kDeleteNear && since >= 0 && now - since > 1000)) {
            RemoveObject(kWorld, nullptr, handle, 0);
            g_unseenSince.erase(handle);
            ++g_civiliansRemoved;
            RFA_LOG_DETAIL_FIRST(40, "Civilian squads: a civilian taken away out of sight at %s alert (%u so far)", level == 2 ? "orange" : "red", g_civiliansRemoved);
        }
    }
    if (g_unseenSince.size() > 256) g_unseenSince.clear();
}

// The set a squad follows, or empty for none.
std::string SetOf(const BYTE* squad) {
    {
        std::lock_guard<std::mutex> hold(g_lock);
        if (auto named = g_handleSets.find(*reinterpret_cast<const unsigned*>(squad + kSquadHandle)); named != g_handleSets.end()) return named->second;
        if (!g_nextSet.empty()) return g_nextSet;
    }
    MapDefinitions();
    const auto found = g_definitionSets.find(*reinterpret_cast<BYTE* const*>(squad + kSquadDefinition));
    return found == g_definitionSets.end() ? std::string() : found->second;
}

std::mt19937& Random() {
    static std::mt19937 random(std::random_device{}());
    return random;
}

// A weighted pick among the entries (Guerrilla's int_rand_range over the summed weights) whose loadout the game has.
const Entry* Pick(std::vector<const Entry*>& entries) {
    double total = 0;
    for (const Entry* entry : entries) total += entry->weight;
    if (total <= 0) return nullptr;
    double at = std::uniform_real_distribution<double>(0.0, total)(Random());
    for (const Entry* entry : entries) {
        if (at < entry->weight) return entry;
        at -= entry->weight;
    }
    return entries.back();
}

int IndexOf(Entry& entry) {
    if (entry.index == -2) entry.index = LoadoutIndex(entry.loadout.c_str());
    return entry.index;
}

bool __fastcall FillMembers(BYTE* squad, void* unused, BYTE* spawn) {
    const bool filled = g_fillMembers(squad, unused, spawn);
    if (!filled || !g_ours || !squad || !spawn || !guerrilla_campaign::Running()) return filled;
    const std::string set = SetOf(squad);
    if (set.empty()) return filled;
    const int progression = GuerrillaCampaign_ProgressionLevel(TeamOf(set));
    auto levels = g_sets.find(set);
    if (progression < 0 || levels == g_sets.end()) return filled;
    const int level = progression >= kLevels ? kLevels - 1 : progression;
    std::vector<Entry>& entries = levels->second[level];
    BYTE* members = *reinterpret_cast<BYTE**>(spawn + kSpawnMembers);
    const int count = *reinterpret_cast<const int*>(spawn + kSpawnMemberCount);
    if (!members || count <= 0 || count > 64 || entries.empty()) return filled;
    const bool byCharacter = TeamOf(set) == 1;
    int changed = 0;
    for (int i = 0; i < count; ++i) {
        BYTE* member = members + i * kMemberSize;
        const BYTE* info = *reinterpret_cast<BYTE* const*>(member + kMemberInfo);
        const char* character = info ? *reinterpret_cast<const char* const*>(info + kInfoName) : nullptr;
        std::vector<const Entry*> mine, all;
        for (Entry& entry : entries) {
            if (IndexOf(entry) < 0) continue;
            all.push_back(&entry);
            if (byCharacter && character && _stricmp(entry.character.c_str(), character) == 0) mine.push_back(&entry);
        }
        const Entry* pick = Pick(mine.empty() ? all : mine);
        if (!pick) continue;
        *reinterpret_cast<int*>(member + kMemberLoadout) = pick->index;
        ++changed;
    }
    if (changed > 0) {
        ++g_changed;
        RFA_LOG_DETAIL_FIRST(60, "Squad levels: a squad of %s at level %d: %d of its %d members given their level's loadouts", set.c_str(), level + 1, changed,
                             count);
    }
    return filled;
}

}  // namespace

void StartSquadLevels() {
    ReadLevels();
    g_alertDespawn = rfa::IniInt(L"GuerrillaPeople", L"CiviliansAtAlert", 1) != 0 && !g_civilianSquads.empty();
    rfa::LogDetail(g_alertDespawn ? "Civilian squads: at orange and red alert %u civilian squads' people go once out of sight, as Guerrilla's"
                            : "Civilian squads: they stay at every alert level (CiviliansAtAlert=0, or no civilian lines in squad_levels.txt)",
             static_cast<unsigned>(g_civilianSquads.size()));
    if (rfa::IniInt(L"GuerrillaPeople", L"SquadLevels", 1) == 0) {
        rfa::LogDetail("Squad levels: off ([GuerrillaPeople] SquadLevels=0): every squad keeps its level-1 weapons");
        return;
    }
    if (g_sets.empty() || g_squadSets.empty()) {
        rfa::Log("Squad levels: no mods\\guerrilla_world\\squad_levels.txt (build the people again with GuerrillaBuilder.exe); every squad keeps its level-1 weapons");
        return;
    }
    if (memcmp(reinterpret_cast<const void*>(kFillMembers), kFillMembersStart, kFillMembersPrologue) != 0) {
        rfa::Log("Squad levels: the squad's member list (0x%08lX) isn't as expected; every squad keeps its level-1 weapons", kFillMembers);
        return;
    }
    g_fillMembers = reinterpret_cast<FillMembersFn>(rfa::HookFunction(kFillMembers, kFillMembersPrologue, reinterpret_cast<void*>(&FillMembers)));
    if (!g_fillMembers) {
        rfa::Log("Squad levels: could not hook the squad's member list; every squad keeps its level-1 weapons");
        return;
    }
    rfa::LogDetail("Squad levels: %u squads follow %u of Guerrilla's progression sets: their members get their level's weapons as they're made",
             static_cast<unsigned>(g_squadSets.size()), static_cast<unsigned>(g_sets.size()));
}

void SquadLevelsFrame(bool ours) {
    if (ours && !g_ours) {  // a level of ours: the campaign's levels
        if (g_fillMembers && !guerrilla_campaign::Running())
            RFA_LOG_DETAIL_FIRST(1, "Squad levels: guerrilla_campaign is off; every squad keeps its level-1 weapons");
    }
    if (!ours && g_ours) {
        std::lock_guard<std::mutex> hold(g_lock);
        g_handleSets.clear();
        g_nextSet.clear();
        g_unseenSince.clear();
    }
    g_ours = ours;
    if (ours && g_alertDespawn) CivilianAlertFrame();
}

// For guerrilla_campaign: a squad it made (by its handle) follows that progression set ("Reinforcement EDF" for the
// EDF's reinforcements, as Guerrilla's squad_flags.reinforcement_squad picks), whatever its squad definition.
void GuerrillaPeople_SquadSet(unsigned squad, const char* set) {
    if (!guerrilla_people::Running()) return;
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_handleSets.size() > 512) g_handleSets.clear();
    if (set && *set) g_handleSets[squad] = set;
    else g_handleSets.erase(squad);
}

// The same for every squad made until it's called again with null or "" (for a squad whose members are made before
// its maker has its handle).
void GuerrillaPeople_NextSquadsSet(const char* set) {
    if (!guerrilla_people::Running()) return;
    std::lock_guard<std::mutex> hold(g_lock);
    g_nextSet = set ? set : "";
}
