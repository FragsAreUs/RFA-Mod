// Guerrilla's reinforcement squads (reinforcement_squad, RM: spawn_squad 0x7B4210, do_frame 0x7AF540,
// do_frame_spawned 0x79B400), shared by the EDF reinforcements and the continuous attacks: see attack_squads.h.
//
// Here:
//   - A squad is made with Armageddon's squad spawn (the console's drop_squad record, 0x507950 / 0x5080A0: its
//     definition at +0x34 from 0x50C350 by name, its vehicle group at +0x38 from 0x757340 by name, the orders at +0x98:
//     Attack 3, the EDF's team, the target's place and handle).
//   - It is kept as Guerrilla's do_frame_spawned keeps it: while its target lives, within 325 m of it, and while it
//     engages the target (one of it has it in sight) or closes on it, its timer kept at 10 s or more; 20 s at the start.
//   - Its members on foot who don't see the target are sent running to it, as the alert orders' squads are.
//   - Let go: its people on foot walk away and go out of view (the converts' SendAway); its vehicle, and its people
//     still in it, are taken away once nobody sees them.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "people/people_api.h"
#include "vehicles/vehicles_api.h"
#include "attack_squads.h"
#include "alert_sources.h"
#include "converts.h"
#include <math.h>
#include <string.h>

namespace campaign::attack_squads {
namespace {

void* const kWorld = reinterpret_cast<void*>(0x1870630);
using CountFn = int(__fastcall*)(void* world, void* unused);
using AtFn = BYTE*(__fastcall*)(void* world, void* unused, int index);
auto const HumanCount = reinterpret_cast<CountFn>(0x43A860);
auto const HumanAt = reinterpret_cast<AtFn>(0x43A8B0);
constexpr DWORD kHandle = 0x68, kInVehicle = 0x1160, kAi = 0x174C, kAiSquad = 0x7D4;
constexpr DWORD kNone = 0xFFFFFFFF;
constexpr int kEdf = 1;
using IsDeadFn = bool(__thiscall*)(void* object);
bool IsDead(BYTE* object) { return reinterpret_cast<IsDeadFn>((*reinterpret_cast<void***>(object))[4])(object); }
DWORD HandleOf(const BYTE* object) { return *reinterpret_cast<const DWORD*>(object + kHandle); }

// Armageddon's squad-making (the console's drop_squad).
using SquadDefinitionFn = BYTE*(__cdecl*)(const char* name);
auto const SquadDefinition = reinterpret_cast<SquadDefinitionFn>(0x50C350);
using VehicleGroupFn = void*(__cdecl*)(const char* name);
auto const VehicleGroup = reinterpret_cast<VehicleGroupFn>(0x757340);  // a vehicle_spawn_group_info by name, or null
using SpawnParametersFn = void(__fastcall*)(BYTE* parameters, void* unused);
auto const SpawnParameters = reinterpret_cast<SpawnParametersFn>(0x507950);
using SpawnSquadFn = BYTE*(__fastcall*)(BYTE* parameters, void* unused);
auto const SpawnSquad = reinterpret_cast<SpawnSquadFn>(0x5080A0);
constexpr size_t kParametersSize = 0x100;  // 0xB8 used
constexpr DWORD kPlace = 0x0, kOrientation = 0x10, kDefinition = 0x34, kVehicleGroup = 0x38;
constexpr DWORD kOrdersType = 0x98, kOrdersTeam = 0x9C, kOrdersPlace = 0xA0, kOrdersTarget = 0xAC;
// The record's squad_flags (+0x50, the "squad_flags" property, which the squad keeps at +0x164: 0x80CA2E), laid out as
// Guerrilla's squad_flags_struct: bit 1 inside_load_dist (set while the squad is within 150 m of the player, 0x50D9D0
// from the squads' loop 0x810AC3: [0xFC1A08] = 150²), bit 3 force_spawn. The member spawn's test (0x80FE70) lets a squad
// make its people only with one of them set (Guerrilla's load radius is 250 m, MAX_SQUAD_LOAD_DISTANCE, its squad
// spawn manager simulating the squads beyond it).
constexpr DWORD kSquadFlags = 0x50, kForceSpawn = 1u << 3;
constexpr int kAttackOrders = 3;  // Armageddon's orders (0x110212C): Standard 1, Defend 2, Attack 3

using GoToFn = int(__fastcall*)(BYTE* ai, void* unused, const float* place, int movement, int stance, int priority, DWORD cellLow, DWORD cellHigh,
                                bool flag);
auto const GoTo = reinterpret_cast<GoToFn>(0x431C20);
constexpr int kRun = 2, kStanding = 0, kLowPriority = 0;
constexpr int kFirstContact = 5;
auto const InView = reinterpret_cast<bool(__cdecl*)(const float* centre, float radius)>(0x768400);
auto const RemoveObject = reinterpret_cast<int(__fastcall*)(void* world, void* unused, DWORD handle, int flag)>(0x6D56A0);

float Distance2(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

std::vector<Squad> g_leaving;  // squads let go whose vehicles (and people in them) go once unseen

// The game's squad (object_squad): the vehicle group it rides in (+0x104, from the spawn record's vehicle_ref; its member
// spawn 0x80D410 asks for a vehicle while it is set, 0x80C790) and the vehicle it made (+0x10C: 0x80B5E0 reads it back
// through the world's vehicle by handle, 0x42AF10).
constexpr DWORD kSquadVehicleGroup = 0x104, kSquadVehicle = 0x10C;
using VehicleByHandleFn = BYTE*(__fastcall*)(void* world, void* unused, DWORD handle);
auto const VehicleByHandle = reinterpret_cast<VehicleByHandleFn>(0x42AF10);

// Its squad's vehicle: the one the game's squad made, else one of its members sits in.
void FindVehicle(Squad& squad, const std::vector<BYTE*>& members) {
    if (squad.vehicle != kNone) return;
    squad.vehicle = VehicleOf(squad, members);
}

void* GroupNamed(const std::string& name, std::string& found) {
    found = "rfg_" + name;  // the world build's names for Guerrilla's groups
    if (void* group = VehicleGroup(found.c_str())) return group;
    found = name;
    return VehicleGroup(found.c_str());
}

}  // namespace

bool GroupExists(const std::string& name) {
    std::string found;
    return !name.empty() && GroupNamed(name, found) != nullptr;
}

bool IsFlyerGroup(const std::string& name) { return strstr(name.c_str(), "flyer") || strstr(name.c_str(), "Flyer"); }

Made Make(const Order& order, Squad& out, std::string& why) {
    BYTE* definition = SquadDefinition(order.squad.c_str());
    if (!definition) {
        why = "no squad " + order.squad + " in squads.xtbl";
        return Made::Failed;
    }
    BYTE* target = static_cast<BYTE*>(ObjectFromHandle(order.target));
    if (!target) {
        why = "its target is gone";
        return Made::Failed;
    }
    void* group = nullptr;
    std::string groupName;
    if (!order.group.empty()) {
        group = GroupNamed(order.group, groupName);
        if (!group) {
            RFA_LOG_FIRST(16, "Attack squads (%s): no vehicle group %s; %s comes on foot", order.who, order.group.c_str(), order.squad.c_str());
            groupName.clear();
        }
    }
    const Vector at = PositionOf(target);
    alignas(8) BYTE parameters[kParametersSize] = {};
    SpawnParameters(parameters, nullptr);
    *reinterpret_cast<Vector*>(parameters + kPlace) = order.place;
    // Facing the target (the matrix's rows: right, up, forward; Guerrilla's squad_orient, its y zeroed).
    float fx = at.x - order.place.x, fz = at.z - order.place.z;
    const float length = sqrtf(fx * fx + fz * fz);
    fx = length > 0.01f ? fx / length : 0.0f;
    fz = length > 0.01f ? fz / length : 1.0f;
    const float orientation[9] = {fz, 0.0f, -fx, 0.0f, 1.0f, 0.0f, fx, 0.0f, fz};
    memcpy(parameters + kOrientation, orientation, sizeof(orientation));
    *reinterpret_cast<BYTE**>(parameters + kDefinition) = definition;
    *reinterpret_cast<void**>(parameters + kVehicleGroup) = group;
    *reinterpret_cast<int*>(parameters + kOrdersType) = kAttackOrders;
    *reinterpret_cast<int*>(parameters + kOrdersTeam) = kEdf;
    *reinterpret_cast<Vector*>(parameters + kOrdersPlace) = at;
    *reinterpret_cast<DWORD*>(parameters + kOrdersTarget) = order.target;
    if (order.forceSpawn) *reinterpret_cast<DWORD*>(parameters + kSquadFlags) |= kForceSpawn;
    // Its members' weapons by guerrilla_people's squad set (named for the squads made now, and by its handle for
    // members made later).
    if (order.weaponSet) GuerrillaPeople_NextSquadsSet(order.weaponSet);
    BYTE* squad = SpawnSquad(parameters, nullptr);
    if (order.weaponSet) GuerrillaPeople_NextSquadsSet(nullptr);
    if (!squad) {
        why = "the game made no " + order.squad + (groupName.empty() ? std::string() : " in " + groupName);
        return Made::Wait;
    }
    if (order.weaponSet) GuerrillaPeople_SquadSet(HandleOf(squad), order.weaponSet);
    out = Squad{};
    out.squad = HandleOf(squad);
    out.rides = group != nullptr;
    out.target = order.target;
    out.what = order.squad + (groupName.empty() ? std::string(" on foot") : " in " + groupName);
    out.madeAt = GameTime();
    out.usefulUntil = out.madeAt + kFirstUseMs;
    return Made::Made;
}

const char* Gone(Squad& squad, const std::vector<BYTE*>& members, int now) {
    if (!members.empty()) {
        if (!squad.hadMembers) {
            squad.hadMembers = true;
            squad.usefulUntil = now + kFirstUseMs;  // Guerrilla's squad is whole at its spawn: its timer from now
            if (squad.rides) {
                FindVehicle(squad, members);
                RFA_LOG_FIRST(24, "Attack squads: %s came %.1f s after it was made (%u of it; its vehicle %s)", squad.what.c_str(),
                              (now - squad.madeAt) / 1000.0f, static_cast<unsigned>(members.size()), squad.vehicle != kNone ? "with them" : "not seen");
            }
        }
        return nullptr;
    }
    if (!ObjectFromHandle(squad.squad)) return "its squad is gone";
    if (squad.hadMembers) return "all of it dead or gone";
    if (now - squad.madeAt > (squad.rides ? kMakeVehicleMs : kMakeMembersMs))
        return squad.rides ? "its vehicle and people never came (its model may not have streamed in)" : "the game never made its people";
    return nullptr;  // its people are still to come
}

DWORD VehicleOf(const Squad& squad, const std::vector<BYTE*>& members) {
    if (const BYTE* object = static_cast<const BYTE*>(ObjectFromHandle(squad.squad))) {
        const DWORD made = *reinterpret_cast<const DWORD*>(object + kSquadVehicle);
        if (made != kNone && made != 0 && VehicleByHandle(kWorld, nullptr, made)) return made;
    }
    for (BYTE* member : members) {
        const DWORD vehicle = *reinterpret_cast<const DWORD*>(member + kInVehicle);
        if (vehicle != kNone) return vehicle;
    }
    return kNone;
}

std::vector<BYTE*> Members(DWORD squad) {
    std::vector<BYTE*> members;
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!human || IsDead(human)) continue;
        BYTE* ai = *reinterpret_cast<BYTE**>(human + kAi);
        if (ai && *reinterpret_cast<const DWORD*>(ai + kAiSquad) == squad) members.push_back(human);
    }
    return members;
}

bool TargetAlive(unsigned target) {
    BYTE* object = static_cast<BYTE*>(ObjectFromHandle(target));
    return object && !IsDead(object);
}

const char* Follow(Squad& squad, const std::vector<BYTE*>& members, int now) {
    BYTE* target = static_cast<BYTE*>(ObjectFromHandle(squad.target));
    if (!target || IsDead(target)) return "its target is dead or gone";
    FindVehicle(squad, members);
    const Vector here = PositionOf(target);
    float d2 = 1e30f;
    bool engaging = false;
    for (BYTE* member : members) {
        const float d = Distance2(PositionOf(member), here);
        d2 = d < d2 ? d : d2;
        engaging = engaging || alert_sources::Believes(member, kFirstContact, squad.target);
    }
    const bool closing = d2 < squad.lastDistance2 - 1.0f;
    squad.lastDistance2 = d2;
    if ((engaging || closing) && squad.usefulUntil < now + kKeepUseMs) squad.usefulUntil = now + kKeepUseMs;
    if (d2 > kTooFar * kTooFar) return "farther than 325 m from its target";
    if (now >= squad.usefulUntil) return "neither fighting nor coming closer";
    return nullptr;
}

void Chase(const Squad& squad, BYTE* target) {
    const Vector here = PositionOf(target);
    for (BYTE* member : Members(squad.squad)) {
        if (*reinterpret_cast<const DWORD*>(member + kInVehicle) != kNone) continue;
        if (alert_sources::Believes(member, kFirstContact, squad.target)) continue;
        BYTE* ai = *reinterpret_cast<BYTE**>(member + kAi);
        if (ai) GoTo(ai, nullptr, &here.x, kRun, kStanding, kLowPriority, kNone, kNone, false);
    }
}

void LetGo(Squad& squad) {
    const std::vector<BYTE*> members = Members(squad.squad);
    FindVehicle(squad, members);
    if (squad.chased && squad.vehicle != kNone && guerrilla_vehicles::Running()) GuerrillaVehicles_Release(squad.vehicle);
    squad.chased = false;
    for (BYTE* member : members)
        if (*reinterpret_cast<const DWORD*>(member + kInVehicle) == kNone) converts::SendAway(HandleOf(member));
    g_leaving.push_back(squad);
}

// Its vehicle (with the people still in it): once out of view and 60 m away, or 300 m away. Then its squad object, once
// none of its people is left.
void RetireLeaving(BYTE* player) {
    const Vector here = PositionOf(player);
    for (size_t i = 0; i < g_leaving.size();) {
        Squad& squad = g_leaving[i];
        if (squad.vehicle != kNone) {
            if (const BYTE* vehicle = static_cast<const BYTE*>(ObjectFromHandle(squad.vehicle))) {
                const Vector at = PositionOf(vehicle);
                const float d2 = Distance2(at, here);
                if (d2 > 300.0f * 300.0f || (d2 > 60.0f * 60.0f && !InView(&at.x, 5.0f))) {
                    for (BYTE* member : Members(squad.squad))
                        if (*reinterpret_cast<const DWORD*>(member + kInVehicle) == squad.vehicle) RemoveObject(kWorld, nullptr, HandleOf(member), 0);
                    RemoveObject(kWorld, nullptr, squad.vehicle, 0);
                    squad.vehicle = kNone;
                }
            } else {
                squad.vehicle = kNone;
            }
        }
        if (squad.vehicle == kNone && Members(squad.squad).empty()) {
            if (ObjectFromHandle(squad.squad)) RemoveObject(kWorld, nullptr, squad.squad, 0);
            g_leaving.erase(g_leaving.begin() + i);
            continue;
        }
        ++i;
    }
}

size_t Leaving() { return g_leaving.size(); }

int g_nextRetire = 0;

void Enter() {
    g_leaving.clear();
    g_nextRetire = 0;
}

void Frame() {
    const int now = GameTime();
    if (now < g_nextRetire && g_nextRetire - now <= 500) return;
    g_nextRetire = now + 500;
    if (BYTE* player = static_cast<BYTE*>(Player())) RetireLeaving(player);
}

}  // namespace campaign::attack_squads
