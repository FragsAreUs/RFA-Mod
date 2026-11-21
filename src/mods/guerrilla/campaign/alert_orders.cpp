// Guerrilla's alert orders (guerrilla_reinforcement_do_frame RM 0x7B9640: do_frame_assigned_alert_orders 0x7AF070
// for Yellow_alert_orders_handle at level 1 and Orange_alert_orders_handle at level 2), ported onto the EDF squads
// our world already has.
//
// Guerrilla, for each of the two orders:
//   - while the alert is at the orders' level or above (and EDF reinforcements aren't switched off), it keeps one set
//     of orders: ATTACK (gfm_orders_creation_params: type 2, the EDF's team, NORMAL_REINFORCEMENT_FLAGS) at the place
//     the EDF last saw the player (cached_orders_info, updated by its witnesses), updated each frame;
//   - when no squad has them, assign_reinforcement_squad (0x7870A0) gives them to the squad nearest the player
//     within 160 m (0x165649C: 25,600 squared) whose team is allied to the EDF (human_teams_get_disposition(EDF,
//     its team) == 4) and that can be reassigned (object_squad::can_be_reassigned 0xA3F2A0: not held by a mission
//     or a script, not dead, its orders not fixed);
//   - below the level, the orders are freed and the squad goes back to its own business.
// So at yellow one squad comes for the player, at orange a second.
//
// Armageddon kept the squads but not Guerrilla's orders manager (its script word orders_attack is empty), so the
// orders are carried out with what its AI has: the squad's guard-post leash taken off (guerrilla_people's
// GuerrillaPeople_ReleaseLeash), each member told the player is its enemy (0x46DB30 on its perception, as
// Guerrilla's force_enemy_knowledge) and sent running to where the EDF last saw him (the AI's go-to 0x431C20),
// again every few seconds while he isn't in its sight. A squad counts as ours while one of its members lives.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "people/people_api.h"
#include "alert_orders.h"
#include "activities.h"
#include "alert_sources.h"
#include "handle_kinds.h"
#include "liberation.h"
#include <math.h>
#include <vector>

namespace campaign::alert_orders {
namespace {

void* const kWorld = reinterpret_cast<void*>(0x1870630);
using CountFn = int(__fastcall*)(void* world, void* unused);
using AtFn = BYTE*(__fastcall*)(void* world, void* unused, int index);
auto const HumanCount = reinterpret_cast<CountFn>(0x43A860);
auto const HumanAt = reinterpret_cast<AtFn>(0x43A8B0);
constexpr DWORD kHandle = 0x68, kTeam = 0x13B4, kInVehicle = 0x1160, kAi = 0x174C, kPerception = 0x14, kAiSquad = 0x7D4;
constexpr DWORD kNone = 0xFFFFFFFF;
constexpr int kEdf = 1;
constexpr int kAllied = 4;  // human_team_dispositions: allied
auto const TeamDisposition = reinterpret_cast<int(__cdecl*)(int team, int other)>(0x8A0460);
using GoHostileFn = void*(__fastcall*)(BYTE* perception, void* unused, BYTE* target, bool alert);
auto const GoHostile = reinterpret_cast<GoHostileFn>(0x46DB30);
using GoToFn = int(__fastcall*)(BYTE* ai, void* unused, const float* place, int movement, int stance, int priority, DWORD cellLow, DWORD cellHigh,
                                bool flag);
auto const GoTo = reinterpret_cast<GoToFn>(0x431C20);
constexpr int kRun = 2, kStanding = 0, kLowPriority = 0;  // human_movement_states (walk 1, run 2, sprint 3)
using IsDeadFn = bool(__thiscall*)(void* object);
bool IsDead(BYTE* object) { return reinterpret_cast<IsDeadFn>((*reinterpret_cast<void***>(object))[4])(object); }

constexpr float kReach = 160.0f;      // assign_reinforcement_squad: within 160 m of the player
constexpr int kPeriodMs = 500;        // how often the orders are looked at
constexpr int kChaseAgainMs = 4000;   // members not seeing the player are sent at his last place again this often
constexpr int kFirstContact = 5;      // AI_BELIEF_FIRST_CONTACT: the player in sight now

struct Orders {
    int level;            // the alert level they start at (yellow, orange)
    const char* name;
    DWORD squad = kNone;  // the squad carrying them out, or none
    bool active = false;  // the alert is at their level (Guerrilla's orders exist)
    int sentAt = 0;       // the game's clock when its members were last sent
};
Orders g_orders[] = {{handles::AlertYellow, "yellow"}, {handles::AlertOrange, "orange"}};

bool g_on = true;
int g_next = 0;
bool g_haveSeen = false;
Vector g_lastSeen{};  // where the EDF last saw the player (cached_orders_info's player_pos)

float FlatDistance(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

DWORD SquadOf(BYTE* human) {
    BYTE* ai = *reinterpret_cast<BYTE**>(human + kAi);
    return ai ? *reinterpret_cast<const DWORD*>(ai + kAiSquad) : kNone;
}

// A squad's living members on foot (a squad in a vehicle isn't sent on foot).
std::vector<BYTE*> Members(DWORD squad, bool* inVehicle = nullptr) {
    std::vector<BYTE*> members;
    if (inVehicle) *inVehicle = false;
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!human || IsDead(human) || SquadOf(human) != squad) continue;
        if (*reinterpret_cast<const DWORD*>(human + kInVehicle) != kNone) {
            if (inVehicle) *inVehicle = true;
            continue;
        }
        members.push_back(human);
    }
    return members;
}

// assign_reinforcement_squad: the squad nearest the player within 160 m, its team allied to the EDF's, not carrying
// the other orders, none of it in a vehicle. A squad's place is its nearest member's.
DWORD PickSquad(BYTE* player, DWORD taken) {
    const Vector here = PositionOf(player);
    struct Near {
        DWORD squad;
        float distance;  // its nearest member's
    };
    std::vector<Near> nearby;
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!human || human == player || IsDead(human)) continue;
        const DWORD squad = SquadOf(human);
        if (squad == kNone || squad == 0 || squad == taken) continue;
        if (TeamDisposition(kEdf, *reinterpret_cast<const int*>(human + kTeam)) != kAllied) continue;
        const float distance = FlatDistance(PositionOf(human), here);
        if (distance >= kReach) continue;
        bool known = false;
        for (Near& n : nearby)
            if (n.squad == squad) {
                known = true;
                if (distance < n.distance) n.distance = distance;
            }
        if (!known) nearby.push_back({squad, distance});
    }
    DWORD best = kNone;
    float bestDistance = kReach;
    for (const Near& n : nearby) {
        if (n.distance >= bestDistance) continue;
        bool inVehicle = false;
        if (Members(n.squad, &inVehicle).empty() || inVehicle) continue;
        best = n.squad;
        bestDistance = n.distance;
    }
    return best;
}

// The orders carried out: each member holds the player for its enemy and runs to where the EDF last saw him, unless
// it has him in sight (then its own combat goes on).
void Send(Orders& orders, BYTE* player, bool first) {
    const DWORD playerHandle = *reinterpret_cast<const DWORD*>(player + kHandle);
    int sent = 0;
    for (BYTE* member : Members(orders.squad)) {
        BYTE* ai = *reinterpret_cast<BYTE**>(member + kAi);
        if (!ai) continue;
        if (first) GoHostile(ai + kPerception, nullptr, player, true);
        if (alert_sources::Believes(member, kFirstContact, playerHandle)) continue;
        GoTo(ai, nullptr, &g_lastSeen.x, kRun, kStanding, kLowPriority, kNone, kNone, false);
        ++sent;
    }
    orders.sentAt = GameTime();
    if (first) {
        const BYTE* squad = static_cast<const BYTE*>(ObjectFromHandle(orders.squad));
        RFA_LOG_DETAIL_FIRST(40, "Alert orders (%s): EDF squad %08X sent at the player (%d on their way to (%.0f, %.0f, %.0f), %.0f m from the player)",
                      orders.name, orders.squad, sent, g_lastSeen.x, g_lastSeen.y, g_lastSeen.z,
                      squad ? FlatDistance(PositionOf(squad), PositionOf(player)) : -1.0f);
    }
}

void Free(Orders& orders, const char* why) {
    if (orders.squad != kNone) RFA_LOG_DETAIL_FIRST(40, "Alert orders (%s): EDF squad %08X let go (%s)", orders.name, orders.squad, why);
    orders.squad = kNone;
}

}  // namespace

void Init() {
    g_on = rfa::IniInt(L"GuerrillaCampaign", L"AlertOrders", 1) != 0;
    rfa::LogDetail(g_on ? "Alert orders: at yellow and orange the EDF send their nearest squad (within 160 m) at the player"
                  : "Alert orders: off (AlertOrders=0 in [GuerrillaCampaign])");
}

void Enter() {
    for (Orders& orders : g_orders) {
        orders.squad = kNone;
        orders.active = false;
    }
    g_haveSeen = false;
    g_next = 0;
    if (g_on && !guerrilla_people::Running()) rfa::LogDetail("Alert orders: guerrilla_people is off; a guard post's squad stays at its post");
}

void Frame() {
    if (!g_on) return;
    const int now = GameTime();
    if (now < g_next && g_next - now <= kPeriodMs) return;
    g_next = now + kPeriodMs;
    BYTE* player = static_cast<BYTE*>(Player());
    if (!player) return;
    const int level = liberation::AlertLevel();
    // Guerrilla's cached_orders_info: the player's place whenever the EDF see him.
    if (level > handles::AlertGreen && (!g_haveSeen || alert_sources::EdfSeeingPlayer() > 0)) {
        g_lastSeen = PositionOf(player);
        g_haveSeen = true;
    }
    if (level == handles::AlertGreen) g_haveSeen = false;
    // No orders while an activity runs (Guerrilla's test of 0x2229590 and its reinforcements switch, 0x22295CB).
    const bool allowed = !activities::AnyRunning();
    for (Orders& orders : g_orders) {
        const bool active = allowed && level >= orders.level;
        if (!active) {
            if (orders.active) Free(orders, level < orders.level ? "the alert fell below its level" : "an activity runs");
            orders.active = false;
            continue;
        }
        orders.active = true;
        if (orders.squad != kNone && Members(orders.squad).empty()) Free(orders, "none of it left on foot");
        if (orders.squad == kNone) {
            const DWORD other = &orders == &g_orders[0] ? g_orders[1].squad : g_orders[0].squad;
            orders.squad = PickSquad(player, other);
            if (orders.squad == kNone) continue;
            GuerrillaPeople_ReleaseLeash(orders.squad);
            Send(orders, player, true);
            continue;
        }
        if (now - orders.sentAt >= kChaseAgainMs) Send(orders, player, false);
    }
}

}  // namespace campaign::alert_orders
