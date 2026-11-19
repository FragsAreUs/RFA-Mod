// Guerrilla's civilians joining the fight by morale, as its code does (Re-Mars-tered addresses).
//
// During a fight (morale_effect_do_frame 0x7B3DC0: encounter_active, which is just "the alert level is yellow,
// orange or red"), each frame (do_frame_convert 0x7B3D20), while no raid runs and the player is on foot:
// - how many may have joined: Number_of_converts (0x165C924) at trunc((effective morale % + 0.005) x 10), that
//   is 0, 0, 1, 1, 2, 2, 3, 4, 5, 6, 12 for morale 0 %, 10 %, ... 100 %, less those who joined already;
// - for each still allowed (convert_to_guerrilla 0x7B3C00), only if an enemy of the player is near
//   (find_nearest_enemy_of_player 0x788D70: the nearest living NPC within 50 m of the player who is hostile to him
//   and holds belief 1 about him), the civilian nearest the player within 60 m who can be converted
//   (morale_npc_can_be_converted 0x7AF310: a civilian, alive, with an AI, not in or entering a vehicle) joins
//   (morale_convert_single_npc 0x788E60): the guerrillas' team, the enemy's knowledge forced on it, out of its
//   squad, on the list of converts (32 at most), the map's gold head (cartographer::add_converted_guerrilla);
// - the HUD: HM_GUERRILLA_ASSIST for one, HM_GUERRILLAS_ASSIST with the number for more, and the tool tip
//   Guerrilla_Joined (2000 ms).
// When the fight is over (green again; release_all_converts 0x7717D0), every convert leaves (npc::start_active_
// despawn), the count starts again, and spontaneous joining waits 1 s.
//
// The spontaneous rising (human_died 0xAC7F3x: an EDF soldier killed a civilian ->
// morale_maybe_convert_civilians_spontaneously_around_event 0x7B3E40, EXECUTION_CONVERSION_RADIUS 30 m around the
// killer, MAX_SPONTANEOUS_CONVERSION_CHANCE 0.5): only at green, out of missions and activities, once that 1 s is
// over (morale_maybe_convert_civilian_spontaneously 0x7AF3E0). Each of up to 20 NPCs within 30 m of the killer may
// join: chance 0.5 x the district's morale % (where the player is), plus 0.25 for each one who already joined in
// this rising within 15 m of it. They join as above, knowing the killer. (Guerrilla also takes a rising one's
// squad of 3 or more with it; our walkers are alone. Its rising by gunshots and pain sounds is switched off in
// Guerrilla: Use_sound_propagation_spontaneous_conversion.)
//
// In Armageddon: the NPC's set_team (0x8AB200, thiscall: npc, team; it tells the AI, 0x430940), the perception's
// "go hostile on this one" (0x46DB30, thiscall on the NPC's AI +0x174C plus 0x14: the target, true; the enemy and
// first-contact beliefs, as Guerrilla's force_enemy_knowledge), the AI's leave_squad (0x4308F0, thiscall on the AI).
// Leaving: the convert walks away from the player (the AI's go-to 0x431C20) and is taken out of the world (0x6D56A0)
// once out of view 20 m away, or beyond 120 m, as Guerrilla's can_delete_despawning_npc. guerrilla_people's walkers
// let a convert go (GuerrillaPeople_LeaveToGame) and its minimap shows the gold head (GuerrillaPeople_Converted).
// Not here: the guerrilla men's animation set (npc_maybe_set_male_guerrilla_anim_set) is guerrilla_people's (it gives
// a guerrilla-team man rfg_Male as he draws a weapon). Not ported: a raid's own rules.
// [GuerrillaCampaign] Converts=0 turns it off.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "people/people_api.h"
#include "converts.h"
#include "activities.h"
#include "alert_sources.h"
#include "handle_kinds.h"
#include "liberation.h"
#include "messages.h"
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <vector>

namespace campaign::converts {
namespace {

// ---- Armageddon's people

void* const kWorld = reinterpret_cast<void*>(0x1870630);
using CountFn = int(__fastcall*)(void* world, void* unused);
using AtFn = BYTE*(__fastcall*)(void* world, void* unused, int index);
using ByHandleFn = BYTE*(__fastcall*)(void* world, void* unused, DWORD handle);
auto const HumanCount = reinterpret_cast<CountFn>(0x43A860);
auto const HumanAt = reinterpret_cast<AtFn>(0x43A8B0);
auto const HumanByHandle = reinterpret_cast<ByHandleFn>(0x436120);
constexpr DWORD kHandle = 0x68, kTeam = 0x13B4, kInVehicle = 0x1160, kAi = 0x174C, kPerception = 0x14;
constexpr int kGuerrillas = 0, kEdf = 1, kCivilians = 2;
constexpr int kHostile = 0;
auto const Disposition = reinterpret_cast<int(__cdecl*)(BYTE* human, BYTE* other)>(0x8A04D0);  // human-to-human
auto const TeamDisposition = reinterpret_cast<int(__cdecl*)(int team, int other)>(0x8A0460);
constexpr int kAllied = 3;  // friendly or better (human_teams_are_allies)
using SetTeamFn = bool(__fastcall*)(BYTE* npc, void* unused, int team);
auto const SetTeam = reinterpret_cast<SetTeamFn>(0x8AB200);
using GoHostileFn = void*(__fastcall*)(BYTE* perception, void* unused, BYTE* target, bool alert);
auto const GoHostile = reinterpret_cast<GoHostileFn>(0x46DB30);
using LeaveSquadFn = bool(__fastcall*)(BYTE* ai, void* unused);
auto const LeaveSquad = reinterpret_cast<LeaveSquadFn>(0x4308F0);
using GoToFn = int(__fastcall*)(BYTE* ai, void* unused, const float* place, int movement, int stance, int priority, DWORD cellLow, DWORD cellHigh,
                                bool flag);
auto const GoTo = reinterpret_cast<GoToFn>(0x431C20);
constexpr int kWalk = 1, kStanding = 0, kLowPriority = 0;  // as guerrilla_people's walkers
constexpr DWORD kNone = 0xFFFFFFFF;
auto const InView = reinterpret_cast<bool(__cdecl*)(const float* centre, float radius)>(0x768400);
auto const RemoveObject = reinterpret_cast<int(__fastcall*)(void* world, void* unused, DWORD handle, int flag)>(0x6D56A0);

using IsDeadFn = bool(__thiscall*)(void* object);
bool IsDead(BYTE* object) {
    void** functions = *reinterpret_cast<void***>(object);
    return reinterpret_cast<IsDeadFn>(functions[4])(object);
}
DWORD HandleOf(const BYTE* object) { return *reinterpret_cast<const DWORD*>(object + kHandle); }
int TeamOf(const BYTE* human) { return *reinterpret_cast<const int*>(human + kTeam); }
BYTE* AiOf(BYTE* human) { return *reinterpret_cast<BYTE**>(human + kAi); }

float Distance2(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}
float FlatDistance2(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}

// ---- Guerrilla's numbers

const int kConverts[11] = {0, 0, 1, 1, 2, 2, 3, 4, 5, 6, 12};  // Number_of_converts
constexpr size_t kMostConverts = 32;                           // All_npcs_converted
constexpr float kConvertRadius = 60.0f;                        // find_nearest_convert_npc (0x1651394)
constexpr float kEnemyRadius = 50.0f;                          // Max_enemy_dist_for_guerrilla_convert_sqr: 2500
constexpr int kEnemyBelief = 1;                                // the belief find_nearest_enemy_of_player asks for
constexpr int kJoinedTipMs = 2000;                             // Guerrilla_Joined
constexpr int kSpontaneousWaitMs = 1000;                       // after a fight, before a rising
constexpr float kExecutionRadius = 30.0f;                      // EXECUTION_CONVERSION_RADIUS
constexpr float kRisingChance = 0.5f;                          // MAX_SPONTANEOUS_CONVERSION_CHANCE
constexpr float kRisingNearby = 15.0f;                         // a joined one within 15 m (225 squared) ...
constexpr float kRisingNearbyBonus = 0.25f;                    // ... adds this to the chance
constexpr int kMostRising = 20;                                // farray<npc*, 20>
constexpr float kLeaveDistance = 40.0f;                        // where a leaving convert walks to, away from the player
constexpr float kDeleteNear = 20.0f, kDeleteFar = 120.0f;      // can_delete_despawning_npc

// ---- The converts

bool g_on = true;
std::vector<DWORD> g_converts;  // All_npcs_converted: they count until the fight is over
struct Leaving {
    DWORD handle;
    int outOfViewSince;
};
std::vector<Leaving> g_leaving;
bool g_inFight = false;
int g_risingAfter = -1;  // the game's clock when a rising may come again (-1: any time)

bool IsConvert(DWORD handle) {
    return std::find(g_converts.begin(), g_converts.end(), handle) != g_converts.end() ||
           std::any_of(g_leaving.begin(), g_leaving.end(), [handle](const Leaving& l) { return l.handle == handle; });
}

// morale_npc_can_be_converted.
bool CanConvert(BYTE* npc, BYTE* player) {
    if (!npc || npc == player || TeamOf(npc) != kCivilians || IsDead(npc) || !AiOf(npc)) return false;
    if (*reinterpret_cast<const DWORD*>(npc + kInVehicle) != kNone) return false;
    return !IsConvert(HandleOf(npc));
}

// find_nearest_enemy_of_player.
BYTE* NearestEnemyOfPlayer(BYTE* player) {
    const Vector here = PositionOf(player);
    const DWORD playerHandle = HandleOf(player);
    BYTE* best = nullptr;
    float bestDistance = kEnemyRadius * kEnemyRadius;
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!human || human == player || IsDead(human) || !AiOf(human)) continue;
        if (Disposition(player, human) != kHostile) continue;
        const float distance = Distance2(PositionOf(human), here);
        if (distance >= bestDistance) continue;
        if (!alert_sources::Believes(human, kEnemyBelief, playerHandle)) continue;
        bestDistance = distance;
        best = human;
    }
    return best;
}

// find_nearest_convert_npc.
BYTE* NearestConvertible(BYTE* player) {
    const Vector here = PositionOf(player);
    BYTE* best = nullptr;
    float bestDistance = kConvertRadius * kConvertRadius;
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!CanConvert(human, player)) continue;
        const float distance = Distance2(PositionOf(human), here);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = human;
        }
    }
    return best;
}

// morale_convert_single_npc.
void Convert(BYTE* npc, BYTE* enemy, const char* why) {
    const DWORD handle = HandleOf(npc);
    if (g_converts.size() >= kMostConverts) return;
    GuerrillaPeople_LeaveToGame(handle);  // guerrilla_people's walkers let it go first
    SetTeam(npc, nullptr, kGuerrillas);
    if (BYTE* ai = AiOf(npc)) {
        if (enemy) GoHostile(ai + kPerception, nullptr, enemy, true);
        LeaveSquad(ai, nullptr);
    }
    g_converts.push_back(handle);
    GuerrillaPeople_Converted(handle);  // guerrilla_people's minimap gold head
    const Vector at = PositionOf(npc);
    rfa::LogDetail("Converts: civilian %08X at (%.0f, %.0f, %.0f) joins the guerrillas (%s)%s%s", handle, at.x, at.y, at.z, why, enemy ? ", against " : "",
             enemy ? (TeamOf(enemy) == kEdf ? "the EDF" : "an enemy") : "");
}

// do_frame_convert and convert_to_guerrilla.
void ConvertForFight(BYTE* player) {
    if (*reinterpret_cast<const DWORD*>(player + kInVehicle) != kNone) return;  // the player on foot
    int tenth = static_cast<int>((liberation::EffectiveMoralePercent(-1) + 0.005f) * 10.0f);
    tenth = tenth < 0 ? 0 : tenth > 10 ? 10 : tenth;
    const int allowed = kConverts[tenth] - static_cast<int>(g_converts.size());
    if (allowed <= 0) return;
    BYTE* enemy = NearestEnemyOfPlayer(player);
    if (!enemy) return;
    int joined = 0;
    for (int i = 0; i < allowed; ++i) {
        BYTE* npc = NearestConvertible(player);
        if (!npc) break;
        Convert(npc, enemy, "morale");
        ++joined;
    }
    if (joined == 0) return;
    if (joined == 1) messages::Minor("HM_GUERRILLA_ASSIST", nullptr);
    else messages::MinorCount("HM_GUERRILLAS_ASSIST", joined);
    messages::Tip("Guerrilla_Joined", kJoinedTipMs);
    rfa::Log("Converts: %d joined (morale %.0f %%, %d may at this morale, %u now)", joined, liberation::EffectiveMoralePercent(-1) * 100.0f, kConverts[tenth],
             static_cast<unsigned>(g_converts.size()));
}

void WalkAway(DWORD handle, const Vector& from) {
    BYTE* npc = HumanByHandle(kWorld, nullptr, handle);
    if (!npc || IsDead(npc)) return;
    if (BYTE* ai = AiOf(npc)) {
        Vector at = PositionOf(npc);
        const float dx = at.x - from.x, dz = at.z - from.z, length = sqrtf(dx * dx + dz * dz);
        const Vector away = length > 0.1f ? Vector{at.x + dx / length * kLeaveDistance, at.y, at.z + dz / length * kLeaveDistance} : at;
        GoTo(ai, nullptr, &away.x, kWalk, kStanding, kLowPriority, kNone, kNone, false);
    }
    g_leaving.push_back({handle, -1});
}

// release_all_converts: each convert leaves, as npc::start_active_despawn.
void ReleaseAll(BYTE* player) {
    if (g_converts.empty()) return;
    const Vector from = player ? PositionOf(player) : Vector{};
    for (DWORD handle : g_converts) WalkAway(handle, from);
    rfa::Log("Converts: the fight is over; %u guerrillas leave", static_cast<unsigned>(g_converts.size()));
    g_converts.clear();
}

// can_delete_despawning_npc: out of view for a moment 20 m away, or beyond 120 m.
void TickLeaving(BYTE* player) {
    const int now = GameTime();
    const Vector here = player ? PositionOf(player) : Vector{};
    for (auto it = g_leaving.begin(); it != g_leaving.end();) {
        BYTE* npc = HumanByHandle(kWorld, nullptr, it->handle);
        if (!npc) {
            it = g_leaving.erase(it);
            continue;
        }
        const Vector at = PositionOf(npc);
        const float distance = sqrtf(Distance2(at, here));
        const float head[3] = {at.x, at.y + 1.0f, at.z};
        if (InView(head, 1.0f)) it->outOfViewSince = -1;
        else if (it->outOfViewSince < 0) it->outOfViewSince = now;
        const bool gone = distance > kDeleteFar || (distance > kDeleteNear && it->outOfViewSince >= 0 && now - it->outOfViewSince > 1000);
        if (gone || IsDead(npc)) {
            if (!IsDead(npc)) RemoveObject(kWorld, nullptr, it->handle, 0);
            it = g_leaving.erase(it);
            continue;
        }
        ++it;
    }
}

}  // namespace

void Init() {
    g_on = rfa::IniInt(L"GuerrillaCampaign", L"Converts", 1) != 0;
    rfa::Log(g_on ? "Converts: civilians join the guerrillas by morale during fights, and may rise when the EDF kill one of them"
                  : "Converts: off (Converts=0 in [GuerrillaCampaign])");
}

void Enter() {
    g_converts.clear();
    g_leaving.clear();
    g_inFight = false;
    g_risingAfter = -1;
}

void SendAway(DWORD person) {
    void* player = Player();
    WalkAway(person, player ? PositionOf(player) : Vector{});
}

void Frame() {
    BYTE* player = static_cast<BYTE*>(Player());
    if (!player) return;
    if (!g_leaving.empty()) TickLeaving(player);
    if (!g_on) return;
    const int level = liberation::AlertLevel();
    const bool fight = level >= handles::AlertYellow && level <= handles::AlertRed;  // encounter_active
    if (fight) {
        g_inFight = true;
        g_risingAfter = -1;
        if (!activities::AnyRunning()) ConvertForFight(player);
    } else if (g_inFight) {
        g_inFight = false;
        ReleaseAll(player);
        g_risingAfter = GameTime() + kSpontaneousWaitMs;
    }
}

void HumanKilled(BYTE* victim, BYTE* killer) {
    if (!g_on || !victim || !killer) return;
    if (TeamOf(killer) != kEdf || TeamOf(victim) != kCivilians) return;
    BYTE* player = static_cast<BYTE*>(Player());
    if (!player) return;
    // morale_maybe_convert_civilian_spontaneously's conditions: green, no activity, the wait over, the killer
    // on the EDF's side.
    if (liberation::AlertLevel() != handles::AlertGreen || activities::AnyRunning()) return;
    if (g_risingAfter >= 0 && GameTime() < g_risingAfter) return;
    if (TeamDisposition(TeamOf(killer), kEdf) < kAllied) return;
    const liberation::District* district = liberation::GetDistrict(liberation::PlayerDistrict());
    const float morale = district && district->moraleMax > 0.0f ? district->morale / district->moraleMax : 0.0f;
    const Vector around = PositionOf(killer);
    std::vector<BYTE*> nearby;
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count && static_cast<int>(nearby.size()) < kMostRising; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (human && human != victim && human != player && Distance2(PositionOf(human), around) <= kExecutionRadius * kExecutionRadius) nearby.push_back(human);
    }
    std::vector<Vector> risen;
    for (BYTE* npc : nearby) {
        if (!CanConvert(npc, player)) continue;
        float chance = kRisingChance * morale;
        const Vector at = PositionOf(npc);
        for (const Vector& other : risen)
            if (FlatDistance2(other, at) <= kRisingNearby * kRisingNearby) chance += kRisingNearbyBonus;
        if (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) >= chance) continue;
        Convert(npc, killer, "the EDF killed a civilian in front of them");
        risen.push_back(at);
    }
    if (!risen.empty())
        rfa::Log("Converts: %u rose against the EDF after a killing (morale %.0f %%, %u around)", static_cast<unsigned>(risen.size()), morale * 100.0f,
                 static_cast<unsigned>(nearby.size()));
}

}  // namespace campaign::converts
