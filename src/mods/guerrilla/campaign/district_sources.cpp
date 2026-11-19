// What changes Guerrilla's EDF control and civilian morale by itself in free roam, as its code does.
// Every change goes through liberation's ChangeEdfControl / ChangeMorale (the difficulty's multipliers,
// whole points, the HUD's message and tool tips), in the player's own district unless said otherwise.
//
// EDF control (object_mover::drop_control, 0xA13D70 in Re-Mars-tered), when a building reaches its
// game-destroyed state: an EDF building, counted once (the world's destroyed table, kept in the
// campaign's save), lowers its own district's control by its points
// (object_mover::set_control_value 0xA13AF0, from its gameplay_properties row's importance and control:
// low 0/0.5/1/2/3 -> 0/1/1/2/3, else 1; medium 0/1/2/3 -> 0/10/12/15, else 10; high 1 -> 80, else the
// row's value). One of low importance says HM_EDF_PROPERTY_DESTROYED (ui_hud_diversion_icon_edf_low).
// Who brought it down doesn't matter.
//
// Propaganda (set_game_destroyed 0x716E70 -> gfm_propaganda_morale_change 0x7AB490): a building with the
// chunk flag propaganda (the EDF's billboards), game-destroyed for the first time (the same destroyed
// table), raises the player's district's morale by Propaganda_destroyed_morale_delta (5):
// HM_PROPAGANDA_DESTROYED (ui_hud_diversion_icon_propaganda). Its audio cue isn't ported.
//
// People killed (human_died -> gfm_control_morale_human_killed 0x7AB6E0), not the player:
// - by the player: a civilian Player_killed_civilian_morale_delta (-3), HM_TRUST_BETRAYED
//   (ui_hud_weapon_icon_sm_skull) and the tool tip Civilian_Death (1000 ms); a guerrilla
//   Player_killed_guerilla_morale_delta (-3), HM_TRUST_BETRAYED. (A hostage's own penalty and a raid's
//   come with house arrest and raids.)
// - by the EDF, within Max_morale_lost_dist (30 m) of the player: a civilian
//   EDF_killed_civilian_morale_delta (-2), HM_CIVILIAN_KILLED; a guerrilla EDF_killed_guerilla_morale_delta
//   (-1), HM_GUERRILLA_KILLED (both ui_hud_weapon_icon_sm_skull). (Not while a raid runs, when raids come.)
// The player's death (local_player_is_killed 0xA34620), out of a mission and not in a demolitions
// master challenge: difficulty.xtbl's MENU_MORALE_PLAYER_DEATH (-2 .. -5), no message.
// The numbers are Guerrilla's tweak_table.xtbl's (its own values when the table isn't there).
//
// Armageddon's human death: 0x85D5E0 (cdecl, the damage record: +0 the victim, +4 the attacker; its
// human damage, 0x85E0C0, calls it when health reaches 0; in multiplayer it sends the "human_died"
// report first), wrapped. The attacker is a human, or the vehicle a human drives: the player's own
// vehicle counts as the player.
//
// The buildings come from the player's own copy of Guerrilla: editor\GuerrillaBuilder.exe writes
// mods\guerrilla_campaign\edf_buildings.txt with the people (each EDF building with points, and every
// propaganda one: Guerrilla's handle, place, points, importance, propaganda). A building keeps
// Guerrilla's handle in our world unless the world converter had to give it another; then it is found
// by its place.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "campaign_api.h"
#include "district_sources.h"
#include "activities.h"
#include "activity_base.h"
#include "converts.h"
#include "liberation.h"
#include "messages.h"
#include "save.h"
#include "tables.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace campaign::district_sources {
namespace {

// ---- Armageddon's objects

void* const kWorld = reinterpret_cast<void*>(0x1870630);
using CountFn = int(__fastcall*)(void* world, void* unused);
using AtFn = BYTE*(__fastcall*)(void* world, void* unused, int index);
using ByHandleFn = BYTE*(__fastcall*)(void* world, void* unused, DWORD handle);
auto const BuildingCount = reinterpret_cast<CountFn>(0x4776E0);
auto const BuildingAt = reinterpret_cast<AtFn>(0x477730);
auto const HumanByHandle = reinterpret_cast<ByHandleFn>(0x436120);
constexpr DWORD kHandle = 0x68, kTeam = 0x13B4, kInVehicle = 0x1160;
constexpr DWORD kBuildingFlags = 0x150, kGameDestroyed = 1u << 4;
constexpr int kGuerrillas = 0, kEdf = 1, kCivilians = 2;  // both games' team numbers

DWORD HandleOf(const BYTE* object) { return *reinterpret_cast<const DWORD*>(object + kHandle); }

// ---- Guerrilla's numbers (tweak_table.xtbl)

float g_playerKilledCivilian = -3.0f;   // Player_killed_civilian_morale_delta
float g_playerKilledGuerrilla = -3.0f;  // Player_killed_guerilla_morale_delta
float g_edfKilledCivilian = -2.0f;      // EDF_killed_civilian_morale_delta
float g_edfKilledGuerrilla = -1.0f;     // EDF_killed_guerilla_morale_delta
float g_propaganda = 5.0f;              // Propaganda_destroyed_morale_delta
float g_moraleLostDistance = 30.0f;     // Max_morale_lost_dist

const char* const kSkull = "ui_hud_weapon_icon_sm_skull";
constexpr int kCivilianDeathTipMs = 1000;

void LoadTweaks() {
    std::vector<tables::Element> rows;
    if (!tables::Load("tweak_table", rows)) {
        rfa::Log("District sources: no tweak_table.xtbl; Guerrilla's usual numbers for morale");
        return;
    }
    struct Tweak {
        const char* name;
        float* value;
    };
    const Tweak tweaks[] = {{"Player_killed_civilian_morale_delta", &g_playerKilledCivilian}, {"Player_killed_guerilla_morale_delta", &g_playerKilledGuerrilla},
                            {"EDF_killed_civilian_morale_delta", &g_edfKilledCivilian},       {"EDF_killed_guerilla_morale_delta", &g_edfKilledGuerrilla},
                            {"Propaganda_destroyed_morale_delta", &g_propaganda},             {"Max_morale_lost_dist", &g_moraleLostDistance}};
    for (const tables::Element& row : rows)
        for (const Tweak& tweak : tweaks)
            if (_stricmp(row.Text("Name").c_str(), tweak.name) == 0) *tweak.value = row.Number("Value", *tweak.value);
}

// ---- The buildings that count

struct Listed {
    unsigned handle = 0;  // Guerrilla's
    Vector at{};
    float points = 0.0f;
    int importance = 0;   // 0 none .. 3 high
    bool propaganda = false;
};
std::unordered_map<unsigned, Listed> g_listed;                 // by Guerrilla's handle
std::unordered_map<long long, std::vector<unsigned>> g_byPlace;  // by the metre cell on the ground

long long Cell(float x, float z) { return (static_cast<long long>(floorf(x)) << 32) ^ static_cast<unsigned>(static_cast<int>(floorf(z))); }

void ReadList() {
    std::ifstream file(ModFolder() + L"\\edf_buildings.txt");
    for (std::string line; std::getline(file, line);) {
        std::istringstream in(line);
        std::string word, handle;
        Listed listed;
        int propaganda = 0;
        if (!(in >> word) || word != "building") continue;
        if (!(in >> handle >> listed.at.x >> listed.at.y >> listed.at.z >> listed.points >> listed.importance >> propaganda)) continue;
        listed.handle = static_cast<unsigned>(strtoul(handle.c_str(), nullptr, 16));
        listed.propaganda = propaganda != 0;
        g_listed[listed.handle] = listed;
        g_byPlace[Cell(listed.at.x, listed.at.z)].push_back(listed.handle);
    }
}

bool SamePlace(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz < 0.5f * 0.5f;
}

// The listed building a loaded one is: by its handle at its place, else by its place.
const Listed* Resolve(const BYTE* building) {
    const Vector at = PositionOf(building);
    const auto byHandle = g_listed.find(HandleOf(building));
    if (byHandle != g_listed.end() && SamePlace(byHandle->second.at, at)) return &byHandle->second;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            const auto cell = g_byPlace.find(Cell(at.x + dx, at.z + dz));
            if (cell == g_byPlace.end()) continue;
            for (unsigned handle : cell->second) {
                const Listed& listed = g_listed[handle];
                if (SamePlace(listed.at, at)) return &listed;
            }
        }
    return nullptr;
}

// The world's destroyed table: Guerrilla's handles counted already (saved in the campaign's save).
std::unordered_set<unsigned> g_counted;
constexpr const char* kCountedSection = "destroyed";

void ReadCounted() {
    g_counted.clear();
    for (const std::string& key : save::Keys(kCountedSection)) g_counted.insert(static_cast<unsigned>(strtoul(key.c_str(), nullptr, 16)));
}

void MarkCounted(unsigned handle) {
    if (!g_counted.insert(handle).second) return;
    char key[16];
    sprintf_s(key, "%08X", handle);
    save::SetInt(kCountedSection, key, 1);
}

// Each loaded building, by our handle: which listed one it is, and whether it was down when last seen.
struct Seen {
    const Listed* listed;
    bool destroyed;
};
std::unordered_map<DWORD, Seen> g_seen;
int g_next = 0;
constexpr int kPeriodMs = 250;
bool g_said = false;

void Destroyed(const BYTE* building, const Listed& listed) {
    if (g_counted.count(listed.handle)) return;
    MarkCounted(listed.handle);
    const Vector at = PositionOf(building);
    if (listed.propaganda) {
        rfa::LogDetail("District sources: propaganda %08X destroyed at (%.0f, %.0f, %.0f)", listed.handle, at.x, at.y, at.z);
        liberation::ChangeMorale(g_propaganda, -1, "HM_PROPAGANDA_DESTROYED", "ui_hud_diversion_icon_propaganda");
    }
    if (listed.points > 0.0f) {
        const int district = liberation::DistrictAt(at);
        rfa::LogDetail("District sources: EDF building %08X destroyed at (%.0f, %.0f, %.0f): %.0f points of control", listed.handle, at.x, at.y, at.z, listed.points);
        const bool low = listed.importance <= 1;
        liberation::ChangeEdfControl(-listed.points, district, low ? "HM_EDF_PROPERTY_DESTROYED" : nullptr, low ? "ui_hud_diversion_icon_edf_low" : nullptr);
    }
}

void LookAtBuildings() {
    const int now = GameTime();
    if (now < g_next && g_next - now <= kPeriodMs) return;
    g_next = now + kPeriodMs;
    const int count = BuildingCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        const BYTE* building = BuildingAt(kWorld, nullptr, i);
        if (!building) continue;
        const bool destroyed = (*reinterpret_cast<const DWORD*>(building + kBuildingFlags) & kGameDestroyed) != 0;
        const DWORD handle = HandleOf(building);
        auto seen = g_seen.find(handle);
        if (seen == g_seen.end()) {
            const Listed* listed = Resolve(building);
            g_seen[handle] = {listed, destroyed};
            // Down before we saw it (a save from before this): in the table, not counted now.
            if (listed && destroyed) MarkCounted(listed->handle);
            continue;
        }
        if (destroyed && !seen->second.destroyed) {
            seen->second.destroyed = true;
            if (seen->second.listed) Destroyed(building, *seen->second.listed);
        }
    }
    if (!g_said && !g_seen.empty()) {
        g_said = true;
        unsigned listed = 0;
        for (const auto& entry : g_seen) listed += entry.second.listed ? 1 : 0;
        rfa::Log("District sources: %u of the %d buildings loaded give control or are propaganda", listed, count);
    }
}

// ---- People killed

using DiedFn = void(__cdecl*)(BYTE* damage);
DiedFn g_died = nullptr;
constexpr DWORD kDied = 0x85D5E0;
constexpr size_t kDiedPrologue = 9;  // push ebp / mov ebp,esp / sub esp,0C0h
const BYTE kDiedStart[kDiedPrologue] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00};
constexpr DWORD kDamageVictim = 0x0, kDamageAttacker = 0x4;
std::unordered_set<DWORD> g_counted_deaths;  // each human once (it may be called again)
int g_deathLines = 0;                        // deaths written to the log this level (the first few)
// The player's death: counted once a life. Guerrilla's world goes on after it (he wakes at a safehouse);
// Armageddon's reloads the last save, which carries the campaign's state from before the death, so the
// death's morale is given again once that save's state is back (ReloadState), as Guerrilla keeps it.
bool g_playerDeathCounted = false;
float g_deathMorale = 0.0f;  // given at the last death, not yet given again after a reload
int g_deathDistrict = -1;
constexpr int kMostDeathLines = 20;

float Distance(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// gfm_control_morale_human_killed, and local_player_is_killed's morale.
void HumanKilled(BYTE* victim, BYTE* attacker) {
    BYTE* player = static_cast<BYTE*>(Player());
    if (!player || !victim) return;
    if (victim == player) {
        activities::DemolitionsPlayerDied();
        if (g_playerDeathCounted) return;
        g_playerDeathCounted = true;
        if (activities::CurrentMode() == activities::DemolitionsMaster) return;
        g_deathMorale = liberation::PlayerDeathMorale();
        g_deathDistrict = liberation::PlayerDistrict();
        rfa::Log("District sources: the player died");
        liberation::ChangeMorale(g_deathMorale, g_deathDistrict);
        return;
    }
    if (!g_counted_deaths.insert(HandleOf(victim)).second) return;
    // Who: the player (or his vehicle), or a human.
    const DWORD playerVehicle = *reinterpret_cast<const DWORD*>(player + kInVehicle);
    const bool byPlayer = attacker && (attacker == player || (playerVehicle != 0xFFFFFFFF && HandleOf(attacker) == playerVehicle));
    const int victimTeam = *reinterpret_cast<const int*>(victim + kTeam);
    const bool attackerHuman = attacker && HumanByHandle(kWorld, nullptr, HandleOf(attacker)) == attacker;
    if (g_deathLines < kMostDeathLines) {
        ++g_deathLines;
        rfa::LogDetail("District sources: %08X (team %d) died, %.0f m from the player; the game names %s%s", HandleOf(victim), victimTeam,
                 Distance(PositionOf(victim), PositionOf(player)), byPlayer ? "the player" : attackerHuman ? "a human of team " : attacker ? "something else" : "no one",
                 attackerHuman && !byPlayer ? std::to_string(*reinterpret_cast<const int*>(attacker + kTeam)).c_str() : "");
    }
    if (byPlayer) {
        if (victimTeam == kCivilians) {
            liberation::ChangeMorale(g_playerKilledCivilian, -1, "HM_TRUST_BETRAYED", kSkull);
            messages::Tip("Civilian_Death", kCivilianDeathTipMs);
        } else if (victimTeam == kGuerrillas) {
            liberation::ChangeMorale(g_playerKilledGuerrilla, -1, "HM_TRUST_BETRAYED", kSkull);
        }
        return;
    }
    if (!attackerHuman) return;
    if (*reinterpret_cast<const int*>(attacker + kTeam) != kEdf) return;
    if (Distance(PositionOf(victim), PositionOf(player)) > g_moraleLostDistance) return;
    if (victimTeam == kCivilians) liberation::ChangeMorale(g_edfKilledCivilian, -1, "HM_CIVILIAN_KILLED", kSkull);
    else if (victimTeam == kGuerrillas) liberation::ChangeMorale(g_edfKilledGuerrilla, -1, "HM_GUERRILLA_KILLED", kSkull);
}

void __cdecl Died(BYTE* damage) {
    BYTE* victim = damage ? *reinterpret_cast<BYTE**>(damage + kDamageVictim) : nullptr;
    BYTE* attacker = damage ? *reinterpret_cast<BYTE**>(damage + kDamageAttacker) : nullptr;
    g_died(damage);
    if (!Playing()) return;
    HumanKilled(victim, attacker);
    // The activities that listen (human_died -> activity_register_death): killing spree.
    activities::ReportDeath(victim, attacker);
    // The spontaneous rising, when the EDF killed a civilian (human_died -> converts).
    if (victim && attacker && HumanByHandle(kWorld, nullptr, HandleOf(attacker)) == attacker) converts::HumanKilled(victim, attacker);
}

}  // namespace

void Init() {
    LoadTweaks();
    ReadList();
    ReadCounted();
    if (g_listed.empty())
        rfa::Log("No edf_buildings.txt (editor\\GuerrillaBuilder.exe build people writes it): destroying the EDF's buildings and propaganda changes nothing");
    else
        rfa::Log("edf_buildings.txt: %u of Guerrilla's buildings give control or are propaganda; %u counted already", static_cast<unsigned>(g_listed.size()),
                 static_cast<unsigned>(g_counted.size()));
    if (memcmp(reinterpret_cast<const void*>(kDied), kDiedStart, kDiedPrologue) != 0) {
        rfa::Log("District sources: the game's human death function isn't as expected; kills don't change morale");
        return;
    }
    g_died = reinterpret_cast<DiedFn>(rfa::HookFunction(kDied, kDiedPrologue, reinterpret_cast<void*>(&Died)));
    if (!g_died) rfa::Log("District sources: could not hook the game's human death function; kills don't change morale");
}

void Enter() {
    g_seen.clear();
    g_counted_deaths.clear();
    g_deathLines = 0;
    // A new life (any save's state has been given back by now).
    g_playerDeathCounted = false;
    g_deathMorale = 0.0f;
    g_deathDistrict = -1;
    g_next = 0;
    g_said = false;
}

void Frame() {
    if (!g_listed.empty()) LookAtBuildings();
}

void ReloadState() {
    ReadCounted();
    g_seen.clear();  // seen again as they are now, without counting
    if (g_deathMorale != 0.0f && g_deathDistrict >= 0) {
        rfa::Log("District sources: the player's death costs its morale again after the reload");
        liberation::ChangeMorale(g_deathMorale, g_deathDistrict);
        g_deathMorale = 0.0f;
        g_deathDistrict = -1;
    }
}

}  // namespace campaign::district_sources

// For guerrilla_save's respawn at a safehouse (Guerrilla's save_and_continue after a death): called just
// before it takes the respawn save's campaign state. That save is made after the death, so it already holds
// the death's morale: the re-give after the reload (ReloadState) is dropped, and the death costs it once.
void GuerrillaCampaign_RespawnSaved() {
    using namespace campaign::district_sources;
    if (!guerrilla_campaign::Running()) return;
    if (g_deathMorale != 0.0f) rfa::Log("District sources: the respawn's save already holds the death's morale");
    g_deathMorale = 0.0f;
    g_deathDistrict = -1;
}
