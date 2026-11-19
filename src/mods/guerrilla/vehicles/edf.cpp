// Guerrilla's EDF ambient actions in its world (amb_edf_action_manager_do_frame 0x651F30 and its actions,
// which Armageddon deleted), ported onto the code Armageddon kept. A part of guerrilla_vehicles.
//
// As Guerrilla's: in the worlds named in [GuerrillaVehicles] TrafficWorlds=, from 20 s after the player is in
// one, while no action runs, every 0.5 to 1 s one of four is tried at random (each 1 in 4): a fly-by (at most
// every 2 to 4 min), a drive-by, dumping a civilian (not ported: it needs the civilians' own action), or
// harassing the player (at most every 4 to 8 min). As Guerrilla's ambient_edf_info::amb_do_spawn (RM 0x651E70), a new
// action starts only at green alert, with the player outside the safehouses, no activity running and his district not
// liberated (guerrilla_campaign's state); one already under way carries on.
// - Drive-by: when the player is within 30 m of an ambient road (its point nearest the player: the meeting
//   point), an EDF scout (Default EDF: EDF_Scout_1-4, a driver and a gunner) is made on an ambient road 150 to
//   250 m away and drives along the roads at 9 m/s to the meeting point, then on. Its gunner keeps its own AI:
//   once it has the player as its target the crew chases the player (crews.cpp). Taken away once out of sight
//   350 m away before it got there, or after it: 200 m away, or 60 m away and out of view.
// - Harass: the same with an unarmed scout (Default EDF NoTurret); within 40 m it drives at the player at
//   40 m/s, stopping 3 m short; within 15 m it's done (Guerrilla then has it shout and suspect the player:
//   not ported) and drives on as a drive-by.
// - Fly-by: an EDF Raven (EDF_flyer: EDF_AAXAir_1, _2, _Gauss) with its pilot is made 300 m from the player in
//   one of the 8 compass directions (the diagonals ~424 m, as Guerrilla's) and flies at 20 m/s, 30 m above the
//   player's height, in a straight line through a point within 100 m of the player and 1000 m beyond; its pilot
//   never fires. Taken away 400 m away. It flies with Guerrilla's flight model (flyers.cpp).
#define RFA_MOD_NAME "guerrilla_vehicles"
#include "rfa_mod.h"
#include "crews.h"
#include <algorithm>

namespace gv {

namespace {

enum Action { kNoAction = 0, kFlyBy = 2, kDriveBy = 3, kDumpCivilian = 4, kHarass = 5 };
const char* ActionName(int action) { return action == kFlyBy ? "fly-by" : action == kHarass ? "harassing" : "drive-by"; }

struct Ambient {
    int action = kNoAction;
    Crew crew;
    VehicleSpawn spawn;
    bool spawning = false, complete = false, chasing = false, hasDestination = false;
    V3 meet{}, beyond{}, destination{};
    Chase chase;
    float closest = 1e9f;  // how near the player it came (a fly-by goes once it's past and 400 m off)
    int started = -1;      // the game's clock
};
Ambient g_now;
int g_nextTry = 0, g_firstTry = 0, g_flyByAllowed = 0, g_harassAllowed = 0;
bool g_inWorld = false, g_on = false;

constexpr float kSpeed = 9.0f, kFlySpeed = 20.0f;
// A flyer's requests (flyer::set_request_flyto_point, set_request_max_velocity_mag; thiscall).
auto const FlyToRaw = reinterpret_cast<void(__fastcall*)(BYTE* flyer, void* unused, const V3* point, bool stop)>(0x61D370);
auto const FlyAtMostRaw = reinterpret_cast<void(__fastcall*)(BYTE* flyer, void* unused, float speed)>(0x61D2F0);
void FlyTo(BYTE* flyer, const V3* point, bool stop) { FlyToRaw(flyer, nullptr, point, stop); }
void FlyAtMost(BYTE* flyer, float speed) { FlyAtMostRaw(flyer, nullptr, speed); }

void Reset() {
    g_now = Ambient{};
}

void Retire() {
    RetireWhenUnseen(g_now.crew);
    Reset();
}

std::vector<std::string> CrewNames(const V3& where, int count) {
    std::vector<std::string> names;
    if (const auto* people = People(ResourceAt(where), "EDF"))
        for (int i = 0; i < count; ++i) names.push_back((*people)[std::rand() % people->size()]);
    return names;
}

// The point of the ambient road nearest the player, if the player is within 30 m of one (Guerrilla's drive-by test).
bool MeetingPoint(const V3& at, V3& meet) {
    BYTE* road = ClosestRoad(&at.x, false, 60.0f, false, false);
    if (!road) return false;
    const DWORD flags = *reinterpret_cast<const DWORD*>(road + 0xB4);
    if (!(flags & 2) || (flags & 0x100)) return false;  // ambient; not no_reinforcements
    const int count = *reinterpret_cast<const int*>(road + 0x98);
    const BYTE* points = *reinterpret_cast<BYTE* const*>(road + 0x9C);
    if (count < 1 || !points) return false;
    float best = 1e9f;
    for (int i = 0; i < count; ++i) {
        const V3 p = *reinterpret_cast<const V3*>(points + i * 28);
        if (FlatDistance(p, at) < best) { best = FlatDistance(p, at); meet = p; }
    }
    return best <= 30.0f;
}

void TryDriveBy(const BYTE* player, int clock, bool harass) {
    if (harass) {
        if (clock < g_harassAllowed) return;
        g_harassAllowed = clock + RandomBetween(240000, 480000);
    }
    const auto* vehicles = EdfVehicles(harass ? "harass" : "driveby");
    if (!vehicles) return;
    const V3 at = Pos(player);
    V3 meet;
    if (!MeetingPoint(at, meet)) return;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const float angle = Random() * 6.2831853f, distance = 150.0f + Random() * 100.0f;
        V3 where, forward;
        if (!RoadPlace({at.x + std::cos(angle) * distance, at.y, at.z + std::sin(angle) * distance}, where, forward)) continue;
        if (Distance(where, at) < 150.0f || Distance(where, at) > 250.0f) continue;
        BYTE* entry = PickVehicle(*vehicles);
        if (!entry) return;
        Reset();
        PrepareSpawn(g_now.spawn, entry, where, {meet.x - where.x, 0, meet.z - where.z}, kSpeed, 0x40B);
        g_now.action = harass ? kHarass : kDriveBy;
        g_now.spawning = true;
        g_now.meet = meet;
        g_now.crew.waiting = CrewNames(where, 2);  // basic_squad_2: a driver and a gunner
        RFA_LOG_DETAIL_FIRST(40, "EDF: a %s (%s) from %.0f m away", ActionName(g_now.action), g_now.spawn.model.c_str(), Distance(where, at));
        return;
    }
}

void TryFlyBy(const BYTE* player, int clock) {
    if (clock < g_flyByAllowed) return;
    g_flyByAllowed = clock + RandomBetween(120000, 240000);  // restarted on every try, as Guerrilla's
    const auto* vehicles = EdfVehicles("flyby");
    if (!vehicles || !FlyersFly()) return;
    static const float kDirections[8][2] = {{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}};  // not normalised, as Guerrilla's
    const V3 at = Pos(player);
    const float* direction = kDirections[std::rand() % 8];
    const V3 start{at.x + direction[0] * 300.0f, at.y + 30.0f, at.z + direction[1] * 300.0f};
    const V3 via{at.x + (Random() * 200.0f - 100.0f), at.y, at.z + (Random() * 200.0f - 100.0f)};
    const float dx = via.x - start.x, dz = via.z - start.z, length = (std::max)(std::sqrt(dx * dx + dz * dz), 1.0f);
    BYTE* entry = PickVehicle(*vehicles);
    if (!entry) return;
    Reset();
    PrepareSpawn(g_now.spawn, entry, start, {dx, 0, dz}, kFlySpeed, 0x403);
    g_now.action = kFlyBy;
    g_now.spawning = true;
    g_now.beyond = {start.x + dx / length * 1000.0f, at.y + 30.0f, start.z + dz / length * 1000.0f};
    g_now.crew.waiting = CrewNames(start, 1);  // basic_squad_1: the pilot
    RFA_LOG_DETAIL_FIRST(40, "EDF: a fly-by (%s)", g_now.spawn.model.c_str());
}

void ActionFrame(BYTE* player) {
    if (g_now.spawning) {
        BYTE* made = nullptr;
        const int result = TrySpawn(g_now.spawn, &made);
        if (result == 1) return;
        g_now.spawning = false;
        if (result == 2) { Reset(); return; }
        g_now.crew.vehicle = Handle(made);
        NoteMadeBy(g_now.crew.vehicle, "the EDF's drive-by, fly-by or harassing");
    }
    BYTE* vehicle = VehicleByHandle(kWorld, nullptr, g_now.crew.vehicle);
    if (!vehicle) { Reset(); return; }
    if (VehicleOf(player) == g_now.crew.vehicle) {  // the player took it
        HandBack(g_now.crew, vehicle);
        Reset();
        return;
    }
    if (!Board(g_now.crew, vehicle)) return;
    Track(g_now.crew, vehicle);
    if (g_now.crew.people.empty()) { Retire(); return; }  // nobody could be made for it
    const V3 at = Pos(player), where = Pos(vehicle);
    const float away = FlatDistance(where, at);
    g_now.closest = (std::min)(g_now.closest, away);
    if (g_now.started < 0) g_now.started = GameTime();
    if (!g_now.chasing && GameTime() - g_now.started > 120000) {  // stuck somewhere (a route that never works out): gone
        RFA_LOG_DETAIL_FIRST(20, "EDF: a %s took over 2 minutes; it's taken away", ActionName(g_now.action));
        Retire();
        return;
    }

    if (g_now.chasing) {
        const ChaseResult result = ChaseFrame(g_now.crew, g_now.chase, vehicle);
        if (result == ChaseResult::Chasing) return;
        RFA_LOG_DETAIL_FIRST(40, "EDF: a %s's chase ended (%s)", ActionName(g_now.action),
                      result == ChaseResult::OnFoot ? "its crew got out to fight" : result == ChaseResult::Lost ? "the player got away" : "its crew can't drive");
        if (result == ChaseResult::Lost) Retire();
        else {
            if (result == ChaseResult::Over) HandBack(g_now.crew, vehicle);
            Reset();  // on foot or out: Armageddon's own from now on
        }
        return;
    }
    BYTE* driver = Driver(g_now.crew, vehicle);
    if (!driver) {
        if (Settling(g_now.crew)) return;
        HandBack(g_now.crew, vehicle);  // its driver dead or out
        Reset();
        return;
    }
    if (g_now.action != kFlyBy && NoticedPlayer(g_now.crew)) {
        RFA_LOG_DETAIL_FIRST(40, "EDF: a %s's crew noticed the player and gives chase", ActionName(g_now.action));
        g_now.chasing = true;
        g_now.chase = Chase{};
        g_now.chase.target = Handle(player);
        return;
    }
    YieldDriver(g_now.crew, vehicle);
    if (*reinterpret_cast<const int*>(vehicle + kVehicleAi) != 1) SetVehicleAiState(vehicle + kVehicleAi, 1);

    if (g_now.action == kFlyBy) {
        // Gone once past the player and 400 m off (it starts up to ~424 m away), or at its point.
        if ((away > 400.0f && away > g_now.closest + 50.0f) || away > 700.0f || FlatDistance(where, g_now.beyond) < 30.0f) { Retire(); return; }
        // Guerrilla's goto_with_flyer's requests for a fly-by, set straight (the flight model flies them): the point
        // beyond, 30 m above the player's height, at 20 m/s.
        g_now.beyond.y = at.y + 30.0f;
        FlyTo(vehicle, &g_now.beyond, false);
        FlyAtMost(vehicle, kFlySpeed);
        return;
    }
    // Drive-bys and harassing.
    if (!g_now.complete) {
        if (FlatDistance(where, g_now.meet) < 20.0f) g_now.complete = true;
        else if (Distance(where, at) > 350.0f) { Retire(); return; }
    } else if (away > 200.0f) { Retire(); return; }
    else if (away >= 60.0f) {
        const V3 middle{where.x, where.y + 1.5f, where.z};
        if (!InView(&middle.x, 3.0f)) { Retire(); return; }
    }
    if (g_now.action == kHarass && away < 40.0f && !g_now.complete) {
        OverrideSpeed(vehicle, 40.0f);
        TravelTo(driver, vehicle, &at.x, kNone, kNone, 0, 3.0f, 0, 0, 0, 1);
        if (away < 15.0f) g_now.complete = true;
        return;
    }
    OverrideSpeed(vehicle, kSpeed);
    if (g_now.complete && (!g_now.hasDestination || FlatDistance(where, g_now.destination) < 20.0f)) {  // on along the roads
        const V3 forward = Forward(vehicle);
        const V3 behind{where.x - forward.x * 50, where.y, where.z - forward.z * 50};
        BYTE* road = ClosestRoad(&where.x, false, 60.0f, false, false);
        g_now.hasDestination = road && NextRoadPlace(vehicle, road, &g_now.destination.x, &behind.x);
        if (!g_now.hasDestination) { Retire(); return; }
    }
    const V3& target = g_now.complete ? g_now.destination : g_now.meet;
    if (TravelTo(driver, vehicle, &target.x, kNone, kNone, 1, 10.0f, 1, 0, 0, 1) == 0) g_now.hasDestination = false;
}

}  // namespace

void StartEdf() {
    g_on = rfa::IniInt(L"GuerrillaVehicles", L"EdfPatrols", 1) != 0;
    rfa::Log(g_on ? "EDF: Guerrilla's drive-bys, fly-bys and harassing in the Guerrilla world" : "EDF: off ([GuerrillaVehicles] EdfPatrols=0)");
}

void EdfFrame() {
    if (!g_on) return;
    if (!InTrafficWorld()) {
        Reset();
        g_inWorld = false;
        return;
    }
    BYTE* player = Player();
    if (!player) return;
    const int clock = GameTime();
    if (!g_inWorld) {
        g_inWorld = true;
        g_firstTry = clock + 20000;
        g_nextTry = 0;
        g_flyByAllowed = clock + RandomBetween(120000, 240000);
        g_harassAllowed = clock + RandomBetween(240000, 480000);
    }
    if (g_now.action != kNoAction) { ActionFrame(player); return; }
    if (clock < g_firstTry || clock < g_nextTry) return;
    g_nextTry = clock + RandomBetween(500, 1000);
    if (CampaignAlert() > 0 || PlayerInSafehouse() || CampaignActivityRunning() || PlayerDistrictLiberated()) return;
    switch (RandomBetween(2, 5)) {
        case kFlyBy: TryFlyBy(player, clock); break;
        case kDriveBy: TryDriveBy(player, clock, false); break;
        case kHarass: TryDriveBy(player, clock, true); break;
        default: break;  // dumping a civilian: not ported
    }
}

}  // namespace gv
