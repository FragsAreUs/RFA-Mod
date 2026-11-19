// Crewed vehicles and Guerrilla's chase in a vehicle (see crews.h). A part of guerrilla_vehicles.
//
// The chase, as Guerrilla's (chase_target_goal and its actions): the driver sits each
// frame out of its own AI and the mod drives it with the game's own vehicle driving (0x4B18E0, as traffic);
// a gunner (a crew member on a vehicle's passenger turret seat) keeps its own AI, so it aims and fires at
// whom it perceives (Armageddon kept man_turret_in_vehicle and attack_from_turret). Without a gunner the car
// rams: it aims at where the target will be (its speed + 5 m/s assumed for the lead, 3 s at most), a
// side-swipe 5 m past its side when alongside, at the target's speed + 30 m/s with no braking at the end;
// once the target is on foot (or in a stopped vehicle) within 40 m the crew stops (to 1.5 m/s) and gets
// out, Armageddon's kill_enemy then fighting on foot (Guerrilla's chase goal dropping to 2000 below
// kill_enemy's 2500; Armageddon has no get_out_of_vehicle action, so the mod asks for the exit,
// vehicle_interaction_request_exit 0x656A20). With a gunner it never gets out while the vehicle works: a
// slow target (under 5 m/s) is circled at the gun's range less 15 m (25 m on foot, 47 m in a vehicle), a
// moving vehicle flanked 20 m to one side (led by 1 s, the side kept 5 s) at its speed + 30 m/s; within
// 30 m of a target at 3 m/s or less it stops and lets the gunner shoot (chase_target_hold_position, looked
// at again every 1 to 3.5 s; it drives again once the target is above 9 m/s or 30 m away). The roads are
// used only beyond 75 m; it stops 3 m short. On fire, wrecked or flipped: everyone out. A target gone or
// 250 m away: the chase is over.
#define RFA_MOD_NAME "guerrilla_vehicles"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "vehicles_api.h"
#include "crews.h"
#include <algorithm>
#include <cstring>

namespace gv {

namespace {
constexpr int kBoardingTime = 10000, kSpawnPatience = 20000;
V3 Add(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 Sub(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 Scale(const V3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
V3 FlatUnit(const V3& a) {
    const float length = std::sqrt(a.x * a.x + a.z * a.z);
    return length > 0.001f ? V3{a.x / length, 0, a.z / length} : V3{0, 0, 1};
}
V3 RightOf(const V3& forward) { return {forward.z, 0, -forward.x}; }
}  // namespace

void PrepareSpawn(VehicleSpawn& spawn, BYTE* entry, const V3& where, const V3& forward, float speed, DWORD flags) {
    memset(spawn.parameters, 0, sizeof(spawn.parameters));
    MakeSpawnParameters(spawn.parameters, nullptr);
    *reinterpret_cast<BYTE**>(spawn.parameters + 0x00) = entry;
    *reinterpret_cast<V3*>(spawn.parameters + 0x04) = where;
    Orientation(FlatUnit(forward), reinterpret_cast<float*>(spawn.parameters + 0x18));
    *reinterpret_cast<int*>(spawn.parameters + 0x3C) = -1;      // any variant
    *reinterpret_cast<float*>(spawn.parameters + 0x40) = speed;
    *reinterpret_cast<int*>(spawn.parameters + 0x44) = -1;      // no parent
    *reinterpret_cast<int*>(spawn.parameters + 0x4C) = 3;       // priority, as drop_car's
    *reinterpret_cast<DWORD*>(spawn.parameters + 0x58) = flags;
    const char* name = *reinterpret_cast<const char* const*>(entry);
    spawn.model = name ? name : "?";
    spawn.since = GameTime();
}

int TrySpawn(VehicleSpawn& spawn, BYTE** made) {
    const int result = SpawnVehicle(spawn.parameters);
    BYTE* vehicle = *reinterpret_cast<BYTE**>(spawn.parameters + 0x5C);
    if (result == 0 && vehicle) { *made = vehicle; return 0; }
    if (result == 1 && GameTime() - spawn.since < kSpawnPatience) return 1;
    RFA_LOG_DETAIL_FIRST(20, "Crews: %s wasn't made (%s)", spawn.model.c_str(), result == 1 ? "not streamed in within 20 s" : "the game refused it");
    return 2;
}

bool Settling(const Crew& crew) { return crew.boardedAt < 0 || GameTime() - crew.boardedAt < 5000; }

bool Board(Crew& crew, BYTE* vehicle) {
    if (crew.waiting.empty()) {
        if (crew.boardedAt < 0) crew.boardedAt = GameTime();
        return true;
    }
    if (crew.boardingSince < 0) crew.boardingSince = GameTime();
    if (GameTime() - crew.boardingSince > kBoardingTime) {  // the rest never streamed in: the vehicle goes with whom it has
        RFA_LOG_FIRST(20, "Crews: %u of a crew couldn't be made (not streamed in); %s goes without them", static_cast<unsigned>(crew.waiting.size()),
                      crew.people.empty() ? "nobody aboard, it" : "the vehicle");
        crew.waiting.clear();
        return true;
    }
    for (auto name = crew.waiting.begin(); name != crew.waiting.end();) {
        BYTE* info = HumanInfo(name->c_str());
        float orientation[9];
        memcpy(orientation, vehicle + kOrientation, sizeof(orientation));
        BYTE* human = info ? MakeHuman(info, reinterpret_cast<const float*>(vehicle + kPosition), orientation, nullptr) : nullptr;
        if (!human) {
            if (!info) name = crew.waiting.erase(name);  // not one of the game's characters
            else ++name;                                 // not streamed in yet: next time
            continue;
        }
        int enter[4] = {-1, 1, -1, 5};  // any seat, the best free one; teleport, minimal processing
        RequestEnter(human, vehicle, enter);
        crew.people.push_back({Handle(human), *reinterpret_cast<const int*>(human + kManagement)});
        name = crew.waiting.erase(name);
    }
    return crew.waiting.empty();
}

void Track(Crew& crew, BYTE* vehicle) {
    const V3 at = Pos(vehicle);
    const int clock = GameTime();
    if (crew.lastTime >= 0 && clock > crew.lastTime) {
        const float dt = (clock - crew.lastTime) / 1000.0f;
        crew.velocity = Scale(Sub(at, crew.lastPosition), 1.0f / dt);
        crew.speed = std::sqrt(crew.velocity.x * crew.velocity.x + crew.velocity.y * crew.velocity.y + crew.velocity.z * crew.velocity.z);
    }
    crew.lastPosition = at;
    crew.lastTime = clock;
}

BYTE* Driver(const Crew& crew, BYTE* vehicle) {
    if (crew.people.empty()) return nullptr;
    BYTE* driver = HumanByHandle(kWorld, nullptr, crew.people[0].handle);
    return driver && VehicleOf(driver) == Handle(vehicle) && Driving(driver) ? driver : nullptr;
}

BYTE* Gunner(const Crew& crew, BYTE* vehicle) {
    if (!HasGunnerSeat(vehicle)) return nullptr;
    for (size_t i = 1; i < crew.people.size(); ++i)
        if (BYTE* human = HumanByHandle(kWorld, nullptr, crew.people[i].handle))
            if (VehicleOf(human) == Handle(vehicle)) return human;
    return nullptr;
}

void YieldDriver(const Crew& crew, BYTE* vehicle) {
    if (BYTE* driver = Driver(crew, vehicle)) SitOut(driver);
}

bool NoticedPlayer(const Crew& crew) {
    BYTE* player = Player();
    if (!player) return false;
    for (const Person& person : crew.people)
        if (BYTE* human = HumanByHandle(kWorld, nullptr, person.handle))
            if (CombatTarget(human) == player) return true;
    return false;
}

bool LostSomeone(const Crew& crew) {
    for (const Person& person : crew.people)
        if (!HumanByHandle(kWorld, nullptr, person.handle)) return true;
    return false;
}

bool Unusable(BYTE* vehicle) {
    if (OnFire(vehicle)) return true;
    if (*reinterpret_cast<const DWORD*>(vehicle + kVehicleFlags) & (1u << 10)) return true;  // a wreck
    return *reinterpret_cast<const float*>(vehicle + kOrientation + 16) < 0.3f;             // the up vector's y: flipped
}

void HandBack(Crew& crew, BYTE* vehicle) {
    if (vehicle) {
        OverrideSpeed(vehicle, 0);
        SetVehicleAiState(vehicle + kVehicleAi, 0);
    }
    for (const Person& person : crew.people)
        if (BYTE* human = HumanByHandle(kWorld, nullptr, person.handle)) *reinterpret_cast<int*>(human + kManagement) = person.management;
}

void GetOut(Crew& crew, BYTE* vehicle, bool keepGunner) {
    BYTE* gunner = keepGunner ? Gunner(crew, vehicle) : nullptr;
    StopCar(vehicle);
    for (const Person& person : crew.people) {
        BYTE* human = HumanByHandle(kWorld, nullptr, person.handle);
        if (!human || human == gunner || VehicleOf(human) != Handle(vehicle)) continue;
        BYTE exit[4] = {};  // vi_exit_data: nothing asked
        RequestExit(human, exit);
    }
    HandBack(crew, vehicle);
}

void Despawn(Crew& crew) {
    for (const Person& person : crew.people) RemoveObject(kWorld, nullptr, person.handle, 0);
    if (crew.vehicle != kNone) RemoveObject(kWorld, nullptr, crew.vehicle, 0);
    crew.people.clear();
    crew.vehicle = kNone;
}

namespace {

// Crews (and vehicles) waiting to be taken away out of sight: each object goes once it's out of view and 60 m
// from the player, or 300 m away.
std::vector<DWORD> g_retiring;

void RetireFrame(const BYTE* player) {
    const V3 at = Pos(player);
    for (auto handle = g_retiring.begin(); handle != g_retiring.end();) {
        BYTE* object = ObjectByHandle(kWorld, nullptr, *handle);
        if (!object) { handle = g_retiring.erase(handle); continue; }
        const V3 where = Pos(object);
        const V3 middle{where.x, where.y + 1.0f, where.z};
        const float away = Distance(where, at);
        if (away > 300.0f || (away > 60.0f && !InView(&middle.x, 3.0f))) {
            RemoveObject(kWorld, nullptr, *handle, 0);
            handle = g_retiring.erase(handle);
            continue;
        }
        ++handle;
    }
}

// The vehicles handed over with GuerrillaVehicles_Chase (the people part's ambushes).
struct Handed {
    Crew crew;
    Chase chase;
};
std::vector<Handed> g_handed;

}  // namespace

void RetireWhenUnseen(Crew& crew) {
    for (const Person& person : crew.people)
        if (BYTE* human = HumanByHandle(kWorld, nullptr, person.handle)) {
            SitOut(human);
            g_retiring.push_back(person.handle);
        }
    if (crew.vehicle != kNone) {
        if (BYTE* vehicle = VehicleByHandle(kWorld, nullptr, crew.vehicle)) StopCar(vehicle);
        g_retiring.push_back(crew.vehicle);
    }
    crew.people.clear();
    crew.vehicle = kNone;
}

ChaseResult ChaseFrame(Crew& crew, Chase& chase, BYTE* vehicle) {
    BYTE* driver = Driver(crew, vehicle);
    BYTE* player = Player();
    if (!driver || (player && VehicleOf(player) == Handle(vehicle))) return ChaseResult::Over;
    BYTE* target = ObjectByHandle(kWorld, nullptr, chase.target);
    if (!target) return ChaseResult::Lost;
    BYTE* targetHuman = HumanByHandle(kWorld, nullptr, chase.target);
    BYTE* targetVehicle = targetHuman && VehicleOf(targetHuman) != kNone ? VehicleByHandle(kWorld, nullptr, VehicleOf(targetHuman)) : nullptr;
    BYTE* tracked = targetVehicle ? targetVehicle : target;
    const int clock = GameTime();
    const V3 at = Pos(tracked);
    if (chase.targetTime >= 0 && clock > chase.targetTime) {
        const V3 velocity = Scale(Sub(at, chase.targetLast), 1000.0f / (clock - chase.targetTime));
        chase.targetVelocity = Add(Scale(chase.targetVelocity, 0.5f), Scale(velocity, 0.5f));  // smoothed over a few frames
    }
    chase.targetLast = at;
    chase.targetTime = clock;
    const float targetSpeed = std::sqrt(chase.targetVelocity.x * chase.targetVelocity.x + chase.targetVelocity.z * chase.targetVelocity.z);
    const bool targetMoving = targetVehicle && targetSpeed > 3.0f;
    const V3 me = Pos(vehicle);
    const float away = FlatDistance(me, at);
    if (away > 250.0f) return ChaseResult::Lost;
    Track(crew, vehicle);
    YieldDriver(crew, vehicle);
    const bool gunner = Gunner(crew, vehicle) != nullptr;

    // Out, to fight on foot: the vehicle unusable, or (no gunner) the target on foot or stopped within 40 m.
    if (chase.state != 2 && (Unusable(vehicle) || (!gunner && !targetMoving && away < 40.0f))) {
        chase.state = 2;
        RFA_LOG_DETAIL_FIRST(20, "Crews: a chasing crew gets out to fight on foot (%s)", Unusable(vehicle) ? "its vehicle is unusable" : "its target is close and on foot");
    }
    if (chase.state == 2) {
        StopCar(vehicle);
        if (!OnFire(vehicle) && crew.speed > 1.5f) return ChaseResult::Chasing;
        GetOut(crew, vehicle, false);  // unusable: everyone; else there is no gunner anyway
        return ChaseResult::OnFoot;
    }
    // Holding still for the gunner.
    if (gunner && away < 30.0f && targetSpeed <= 3.0f && chase.state != 1) {
        chase.state = 1;
        chase.holdCheck = clock + RandomBetween(1000, 3500);
    }
    if (chase.state == 1) {
        if (clock >= chase.holdCheck) {
            if (!gunner || targetSpeed > 9.0f || away >= 30.0f) chase.state = 0;
            else chase.holdCheck = clock + RandomBetween(1000, 3500);
        }
        if (chase.state == 1) {
            StopCar(vehicle);
            return ChaseResult::Chasing;
        }
    }

    V3 destination;
    float speed;
    int noEndBraking = 0;
    if (!gunner) {  // ramming
        const float lead = std::clamp(away / (targetSpeed + 5.0f), 0.0f, 3.0f);
        destination = Add(at, Scale(chase.targetVelocity, lead));
        if (targetVehicle && away < 10.0f) {
            const V3 theirs = FlatUnit(Forward(targetVehicle));
            const V3 mine = FlatUnit(Forward(vehicle));
            if (mine.x * theirs.x + mine.z * theirs.z > 0.7f) destination = Add(destination, Scale(RightOf(theirs), 5.0f * chase.side));
        }
        speed = targetSpeed + 30.0f;
        noEndBraking = 1;
    } else if (targetSpeed < 5.0f) {  // circling
        const float radius = targetVehicle ? 47.0f : 25.0f;
        const V3 out = FlatUnit(Sub(me, at));
        const float turn = 0.61f * chase.side;  // 35°
        const V3 around{out.x * std::cos(turn) - out.z * std::sin(turn), 0, out.x * std::sin(turn) + out.z * std::cos(turn)};
        destination = Add(at, Scale(around, radius));
        if (FlatDistance(me, destination) < 8.0f) chase.side = -chase.side;
        speed = away < radius * 0.6f ? 15.0f : 20.0f;
    } else {  // flanking
        if (chase.sideUntil < 0 || clock >= chase.sideUntil) {
            chase.side = Random() < 0.5f ? -1 : 1;
            chase.sideUntil = clock + 5000;
        }
        const V3 heading = FlatUnit(chase.targetVelocity);
        destination = Add(Add(at, chase.targetVelocity), Scale(RightOf(heading), 20.0f * chase.side));
        speed = targetSpeed + 30.0f;
    }
    destination.y = at.y;
    BYTE* ai = vehicle + kVehicleAi;
    if (*reinterpret_cast<const int*>(ai) != 1) SetVehicleAiState(ai, 1);
    OverrideSpeed(vehicle, speed);
    TravelTo(driver, vehicle, &destination.x, kNone, kNone, away > 75.0f ? 1 : 0, 3.0f, noEndBraking, 0, 0, 1);
    return ChaseResult::Chasing;
}

void StartChases() {}

void ChasesFrame() {
    if (!InTrafficWorld()) {
        g_handed.clear();
        g_retiring.clear();
        return;
    }
    BYTE* player = Player();
    if (!player) return;
    RetireFrame(player);
    for (auto handed = g_handed.begin(); handed != g_handed.end();) {
        BYTE* vehicle = VehicleByHandle(kWorld, nullptr, handed->crew.vehicle);
        const ChaseResult result = vehicle ? ChaseFrame(handed->crew, handed->chase, vehicle) : ChaseResult::Over;
        if (result == ChaseResult::Chasing) { ++handed; continue; }
        if (vehicle && result != ChaseResult::OnFoot) HandBack(handed->crew, vehicle);
        RFA_LOG_FIRST(40, "Crews: a handed-over chase ended (%s)", result == ChaseResult::OnFoot ? "the crew got out to fight" : result == ChaseResult::Lost ? "the target is gone or far" : "the crew can't drive");
        handed = g_handed.erase(handed);
    }
}

}  // namespace gv

using namespace gv;

// For the other parts (the people part's Marauder ambushes): from the next frame on, the vehicle's driver (an
// NPC in seat 0) chases the target as above, the other occupants keeping their own AI. False if the vehicle
// or target isn't there, or nobody but the player drives it. Call on the game's thread.
bool GuerrillaVehicles_Chase(DWORD vehicleHandle, DWORD targetHandle) {
    if (!guerrilla_vehicles::Running()) return false;
    BYTE* vehicle = VehicleByHandle(kWorld, nullptr, vehicleHandle);
    if (!vehicle || !ObjectByHandle(kWorld, nullptr, targetHandle)) return false;
    BYTE* driver = SeatOccupant(vehicle, 0);
    if (!driver || driver == Player()) return false;
    g_handed.erase(std::remove_if(g_handed.begin(), g_handed.end(), [&](const Handed& h) { return h.crew.vehicle == vehicleHandle; }), g_handed.end());
    Handed handed;
    handed.crew.vehicle = vehicleHandle;
    handed.crew.people.push_back({Handle(driver), *reinterpret_cast<const int*>(driver + kManagement)});
    for (int seat = 1; seat < Seats(vehicle); ++seat)
        if (BYTE* human = SeatOccupant(vehicle, seat)) handed.crew.people.push_back({Handle(human), *reinterpret_cast<const int*>(human + kManagement)});
    handed.chase.target = targetHandle;
    g_handed.push_back(handed);
    RFA_LOG_DETAIL_FIRST(40, "Crews: a crew of %u was handed a chase", static_cast<unsigned>(handed.crew.people.size()));
    return true;
}

// Gives a vehicle handed over with GuerrillaVehicles_Chase back to the game now.
void GuerrillaVehicles_Release(DWORD vehicleHandle) {
    if (!guerrilla_vehicles::Running()) return;
    for (auto handed = g_handed.begin(); handed != g_handed.end(); ++handed)
        if (handed->crew.vehicle == vehicleHandle) {
            HandBack(handed->crew, VehicleByHandle(kWorld, nullptr, vehicleHandle));
            g_handed.erase(handed);
            return;
        }
}
