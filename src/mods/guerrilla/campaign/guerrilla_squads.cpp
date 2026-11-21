// Guerrilla's Red Faction squads joining the player's fights by morale, as its code does (Re-Mars-tered
// addresses; guerrilla_squad_info, kept by the reinforcement controller rfc_do_frame 0x7B9640, which runs every
// 500 to 1500 ms).
//
// With no squad of its own (guerrilla_squad_info::do_frame_none 0x7B6EA0), one comes when:
// - guerrilla reinforcements aren't switched off (Disable_guerrilla_reinforcement, a script word) and the activity
//   running, if any, allows them (activity_allow_guerrilla_reinforcements);
// - the alert level is orange or red, the respawn wait is over, the player is on foot;
// - guerrilla_reinforcement.xtbl has a row for the district's effective morale (find_guerrilla_info 0x771320: the
//   row with the highest morale_level at or under it; none under 40 %): its squad and its respawn_delay;
// - at least 2 of the EDF's side are within 80 m of the player (count_near_by_edf 0x787290: NPCs allied to the
//   EDF, 6400 squared).
// It is made (do_spawn 0x7B3890) on a road 150 to 250 m from the player (pick_guerrilla_spawn_location 0x79AB70,
// 22500 and 62500 squared) with Defend orders on the player (his place and handle).
// While it lives (do_frame_live 0x75B1B0) the respawn wait is the row's respawn_delay from now (so it counts from
// the squad's end); back at green it is let go (release_squad); the first time it is within 60 m of the player the
// HUD says HM_GUERRILLA_REINFORCEMENTS.
//
// The rows (squads guerilla_squad_1 to _4: 40 % 30 s, 45 % 20 s, 50 % 25 s, 60 % 15 s, 70 % 20 s, 80 % 10 s,
// 90 % 10 s, 99 % 5 s) come from Guerrilla's own table. Guerrilla makes a guerilla_squad_N's members from the
// Guerrilla team's squad_progression with the zone's looks; our world has a version of each for every district
// group, which guerrilla_people names for a place (GuerrillaPeople_GuerrillaSquadAt).
//
// In Armageddon: the console's drop_squad (0x507A60) way of making a squad. The squad_spawn_parameters
// (constructor 0x507950): the place +0, the orientation's nine numbers +0x10, the squad definition +0x34 (0x50C350 by
// name; its team at +0xC), the orders' type +0x98 (Armageddon's Defend is 2: Standard 1, Defend 2, Attack 3,
// Immediate Attack 4, Patrol 5), the team +0x9C, a position +0xA0 and the target's handle +0xAC; 0x5080A0 makes it
// (the squad, or null). Roads: the closest road to a point (0x7F6E10) and its points (+0x98 how many, +0x9C 28 bytes
// each). A member is an NPC whose AI (+0x174C) names the squad (+0x7D4). Let go, its members leave as the converts
// do (converts::SendAway). [GuerrillaCampaign] GuerrillaSquads=0 turns it off.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "people/people_api.h"
#include "guerrilla_squads.h"
#include "activities.h"
#include "converts.h"
#include "handle_kinds.h"
#include "liberation.h"
#include "messages.h"
#include "tables.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace campaign::guerrilla_squads {
namespace {

// ---- Armageddon's people and squads

void* const kWorld = reinterpret_cast<void*>(0x1870630);
using CountFn = int(__fastcall*)(void* world, void* unused);
using AtFn = BYTE*(__fastcall*)(void* world, void* unused, int index);
auto const HumanCount = reinterpret_cast<CountFn>(0x43A860);
auto const HumanAt = reinterpret_cast<AtFn>(0x43A8B0);
constexpr DWORD kHandle = 0x68, kTeam = 0x13B4, kInVehicle = 0x1160, kAi = 0x174C, kAiSquad = 0x7D4;
constexpr int kEdf = 1;
constexpr int kAllied = 4, kFriendly = 3;
auto const TeamDisposition = reinterpret_cast<int(__cdecl*)(int team, int other)>(0x8A0460);
constexpr DWORD kNone = 0xFFFFFFFF;

using IsDeadFn = bool(__thiscall*)(void* object);
bool IsDead(BYTE* object) {
    void** functions = *reinterpret_cast<void***>(object);
    return reinterpret_cast<IsDeadFn>(functions[4])(object);
}
DWORD HandleOf(const BYTE* object) { return *reinterpret_cast<const DWORD*>(object + kHandle); }
int TeamOf(const BYTE* human) { return *reinterpret_cast<const int*>(human + kTeam); }

using SquadDefinitionFn = BYTE*(__cdecl*)(const char* name);
auto const SquadDefinition = reinterpret_cast<SquadDefinitionFn>(0x50C350);
using SpawnParametersFn = void(__fastcall*)(BYTE* parameters, void* unused);
auto const SpawnParameters = reinterpret_cast<SpawnParametersFn>(0x507950);
using SpawnSquadFn = BYTE*(__fastcall*)(BYTE* parameters, void* unused);
auto const SpawnSquad = reinterpret_cast<SpawnSquadFn>(0x5080A0);
constexpr size_t kParametersSize = 0x100;  // 0xB8 used (drop_squad's)
constexpr DWORD kPlace = 0x0, kOrientation = 0x10, kDefinition = 0x34, kOrdersType = 0x98, kOrdersTeam = 0x9C, kOrdersPlace = 0xA0, kOrdersTarget = 0xAC;
constexpr DWORD kDefinitionTeam = 0xC;
constexpr int kStandardOrders = 1, kDefendOrders = 2;

auto const ClosestRoad = reinterpret_cast<BYTE*(__cdecl*)(const float* where, bool special, float within, bool unknown, bool gpsOnly)>(0x7F6E10);
constexpr DWORD kRoadPointCount = 0x98, kRoadPoints = 0x9C, kRoadPointSize = 28;

float Distance2(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// ---- Guerrilla's numbers

struct Row {
    std::string squad;  // guerilla_squad_N
    int size = 1;
    float morale = 0.0f;  // 0..1
    int delayMs = 0;
};
std::vector<Row> g_rows;
constexpr float kEdfNear = 80.0f;
constexpr int kEdfNeeded = 2;
constexpr float kSpawnNear = 150.0f, kSpawnFar = 250.0f;
constexpr float kToldWithin = 60.0f;

void LoadRows() {
    std::vector<tables::Element> rows;
    if (!tables::Load("guerrilla_reinforcement", rows)) return;
    for (const tables::Element& entry : rows) {
        Row row;
        row.squad = entry.Text("Name");
        if (row.squad.empty()) continue;
        const char* number = strrchr(row.squad.c_str(), '_');
        row.size = number ? atoi(number + 1) : 1;
        float level = entry.Number("morale_level") / 100.0f;
        row.morale = level < 0.0f ? 0.0f : level > 1.0f ? 1.0f : level;
        row.delayMs = static_cast<int>(entry.Number("respawn_delay") * 1000.0f);
        g_rows.push_back(row);
    }
}

// find_guerrilla_info: the row with the highest morale level at or under the morale.
const Row* RowFor(float morale) {
    const Row* best = nullptr;
    for (const Row& row : g_rows)
        if (row.morale <= morale && (!best || row.morale > best->morale)) best = &row;
    return best;
}

// count_near_by_edf.
int EdfNear(BYTE* player) {
    const Vector here = PositionOf(player);
    int edf = 0;
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!human || human == player || IsDead(human)) continue;
        if (TeamDisposition(TeamOf(human), kEdf) != kAllied) continue;
        if (Distance2(PositionOf(human), here) < kEdfNear * kEdfNear) ++edf;
    }
    return edf;
}

// pick_guerrilla_spawn_location: the nearest road point 150 to 250 m from the player.
bool SpawnPlace(BYTE* player, Vector& place) {
    const Vector here = PositionOf(player);
    float best = 1e30f;
    for (int angle = 0; angle < 16; ++angle)
        for (float distance = kSpawnNear; distance <= kSpawnFar; distance += 25.0f) {
            const float a = angle * 3.14159265f / 8.0f;
            const Vector probe{here.x + cosf(a) * distance, here.y, here.z + sinf(a) * distance};
            BYTE* road = ClosestRoad(&probe.x, false, 60.0f, false, false);
            if (!road) continue;
            const int points = *reinterpret_cast<const int*>(road + kRoadPointCount);
            const BYTE* point = *reinterpret_cast<BYTE* const*>(road + kRoadPoints);
            if (points <= 0 || !point) continue;
            for (int i = 0; i < points; ++i) {
                const Vector at = *reinterpret_cast<const Vector*>(point + i * kRoadPointSize);
                const float d = Distance2(at, here);
                if (d <= kSpawnNear * kSpawnNear || d >= kSpawnFar * kSpawnFar || d >= best) continue;
                best = d;
                place = at;
            }
        }
    return best < 1e30f;
}

// ---- guerrilla_people's names for our versions of the squads


// ---- The squad

bool g_on = true;
DWORD g_squad = kNone;   // our squad (its handle), or none
const Row* g_row = nullptr;
bool g_told = false;     // HM_GUERRILLA_REINFORCEMENTS said
int g_respawnAt = -1;    // the game's clock when another may come (-1: any time)
int g_nextLook = 0;
bool g_noVersionSaid = false;

std::vector<BYTE*> Members(DWORD squad) {
    std::vector<BYTE*> members;
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!human) continue;
        BYTE* ai = *reinterpret_cast<BYTE**>(human + kAi);
        if (ai && *reinterpret_cast<const DWORD*>(ai + kAiSquad) == squad) members.push_back(human);
    }
    return members;
}

void Spawn(BYTE* player, const Row& row) {
    Vector place{};
    if (!SpawnPlace(player, place)) {
        RFA_LOG_DETAIL_FIRST(8, "Guerrilla squads: no road 150 to 250 m from the player; no squad comes");
        return;
    }
    const char* version = GuerrillaPeople_GuerrillaSquadAt(row.size, place.x, place.z);
    BYTE* definition = version ? SquadDefinition(version) : nullptr;
    if (!definition) {
        if (!g_noVersionSaid) {
            g_noVersionSaid = true;
            rfa::LogDetail("Guerrilla squads: no version of %s for (%.0f, %.0f) (%s); no squad comes there", row.squad.c_str(), place.x, place.z,
                     version ? version : "guerrilla_people names none");
        }
        return;
    }
    alignas(8) BYTE parameters[kParametersSize] = {};
    SpawnParameters(parameters, nullptr);
    *reinterpret_cast<Vector*>(parameters + kPlace) = place;
    float* orientation = reinterpret_cast<float*>(parameters + kOrientation);
    const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    memcpy(orientation, identity, sizeof(identity));
    *reinterpret_cast<BYTE**>(parameters + kDefinition) = definition;
    const int team = *reinterpret_cast<const int*>(definition + kDefinitionTeam);
    // Defend on the player, as Guerrilla's; drop_squad gives an allied team the same.
    const bool allied = TeamDisposition(team, TeamOf(player)) >= kFriendly;
    *reinterpret_cast<int*>(parameters + kOrdersType) = allied ? kDefendOrders : kStandardOrders;
    *reinterpret_cast<int*>(parameters + kOrdersTeam) = team;
    *reinterpret_cast<Vector*>(parameters + kOrdersPlace) = PositionOf(player);
    *reinterpret_cast<DWORD*>(parameters + kOrdersTarget) = HandleOf(player);
    BYTE* squad = SpawnSquad(parameters, nullptr);
    if (!squad) {
        RFA_LOG_FIRST(8, "Guerrilla squads: the game made no %s at (%.0f, %.0f, %.0f)", version, place.x, place.y, place.z);
        return;
    }
    g_squad = HandleOf(squad);
    g_row = &row;
    g_told = false;
    const Vector here = PositionOf(player);
    rfa::LogDetail("Guerrilla squads: %s (%s, %d) comes from (%.0f, %.0f, %.0f), %.0f m away (morale %.0f %%, %s)", version, row.squad.c_str(), row.size, place.x,
             place.y, place.z, sqrtf(Distance2(place, here)), liberation::EffectiveMoralePercent(-1) * 100.0f, allied ? "defending the player" : "NOT allied: standard orders");
}

void Release(const std::vector<BYTE*>& members) {
    for (BYTE* member : members)
        if (!IsDead(member)) converts::SendAway(HandleOf(member));
    rfa::LogDetail("Guerrilla squads: green again; the squad goes home");
    g_squad = kNone;
    g_row = nullptr;
}

}  // namespace

void Init() {
    g_on = rfa::IniInt(L"GuerrillaCampaign", L"GuerrillaSquads", 1) != 0;
    LoadRows();
    if (!g_on) rfa::LogDetail("Guerrilla squads: off (GuerrillaSquads=0 in [GuerrillaCampaign])");
    else if (g_rows.empty()) rfa::Log("Guerrilla squads: no guerrilla_reinforcement.xtbl; no Red Faction squads come");
    else {
        float least = 1.0f;
        for (const Row& row : g_rows) least = row.morale < least ? row.morale : least;
        rfa::LogDetail("guerrilla_reinforcement.xtbl: %u rows; Red Faction squads join fights at orange and red from %.0f %% morale", static_cast<unsigned>(g_rows.size()),
                 least * 100.0f);
    }
}

void Enter() {
    g_squad = kNone;
    g_row = nullptr;
    g_told = false;
    g_respawnAt = -1;
    g_nextLook = 0;
    if (g_on && !guerrilla_people::Running()) rfa::LogDetail("Guerrilla squads: guerrilla_people is off; no Red Faction squads come");
}

void Frame() {
    if (!g_on || g_rows.empty() || !guerrilla_people::Running()) return;
    BYTE* player = static_cast<BYTE*>(Player());
    if (!player) return;
    const int now = GameTime();
    if (now < g_nextLook && g_nextLook - now <= 1500) return;  // rfc_do_frame: every 500 to 1500 ms
    g_nextLook = now + 500 + rand() % 1001;
    const int level = liberation::AlertLevel();
    if (g_squad != kNone) {
        const std::vector<BYTE*> members = Members(g_squad);
        bool alive = false;
        for (BYTE* member : members) alive |= !IsDead(member);
        if (!alive) {
            rfa::LogDetail("Guerrilla squads: the squad is gone; another may come in %d s", g_row ? g_row->delayMs / 1000 : 0);
            g_squad = kNone;
            g_row = nullptr;
            return;
        }
        if (g_row) g_respawnAt = now + g_row->delayMs;  // do_frame_live: the wait counts from the squad's end
        if (level == handles::AlertGreen) return Release(members);
        if (!g_told) {
            const Vector here = PositionOf(player);
            for (BYTE* member : members)
                if (!IsDead(member) && Distance2(PositionOf(member), here) < kToldWithin * kToldWithin) {
                    g_told = true;
                    messages::Minor("HM_GUERRILLA_REINFORCEMENTS", nullptr);
                    rfa::LogDetail("Guerrilla squads: the squad is here");
                    break;
                }
        }
        return;
    }
    // do_frame_none.
    if (level < handles::AlertOrange) return;
    if (g_respawnAt >= 0 && now < g_respawnAt) return;
    if (*reinterpret_cast<const DWORD*>(player + kInVehicle) != kNone) return;  // the player on foot
    if (activities::AnyRunning()) return;  // activity_allow_guerrilla_reinforcements: only demolitions master is ported, which allows none
    const Row* row = RowFor(liberation::EffectiveMoralePercent(-1));
    if (!row) return;
    if (EdfNear(player) < kEdfNeeded) return;
    Spawn(player, *row);
}

}  // namespace campaign::guerrilla_squads
