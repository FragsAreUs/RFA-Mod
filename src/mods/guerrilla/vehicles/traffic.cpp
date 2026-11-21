// Guerrilla's traffic in its world: its ambient vehicle spawner (amb_spawn's do_frame_spawn_vehicle and
// spawn_single_vehicle, despawn), which Armageddon deleted, ported onto the code Armageddon kept. A part of
// guerrilla_vehicles. GuerrillaBuilder.exe writes what it needs: mods\guerrilla_vehicles\traffic.txt (each
// zone's vehicle group, its traffic's density by day and by night, its spawn resource; each group's
// vehicles) and mods\guerrilla_people\crews.txt (each resource's people by side, the people part's).
//
// Spawning, as Guerrilla's: every 250 to 500 ms (2 to 4 s after a spawn), in the world named in
// [GuerrillaVehicles] TrafficWorlds= (rfg), while the player's zone has traffic: up to its cap (Guerrilla's
// table at green alert: none, low 4, medium 6, high 10; the night's density from 18:00 to 6:00 by the game
// clock), a car of the zone's group on an ambient road (a road whose file flag 1 is set: Armageddon keeps it
// as bit 2 of the road's flags, +0xB4) 60 to 260 m from the player, not within 30 m of another, facing
// along the road on its right side; a model already streamed in first, else one the game is asked to
// stream (guerrilla_vehicles' spawn reserves it). Its people: 1 to 3 (as many as it has seats) of the zone's
// resource's civilians and guerrillas (the people the game streams there), made in the car's place
// (0x762D50, drop_human's) and put straight into their seats (vehicle_interaction_request_enter 0x656410,
// teleporting, as Guerrilla's do_npc_spawn_vehicle: the driver first).
//
// Driving: Armageddon has no order or goal that drives an NPC along roads to a place (drive_to_location's
// flags are read by nothing), so the mod drives its traffic's drivers itself every frame, as Guerrilla's
// ambient_veh_drive_action did: each occupant's AI told to sit this frame out (its ai_system's
// yield_next_frame, +0x651 bit 2: the planner, perception and firing skip it; death doesn't), the car's AI
// in its pathfind state at 9 m/s (AMBIENT_VEHICLE_SPEED), and ai_vehicle_travel_to_a_point_do_frame
// (0x4B18E0, Guerrilla's 0x68B1F0) toward a road destination 150 m or more ahead (the game's own
// ai_vehicle_pick_flee_destination_road, 0x4B2BD0: a flood along the roads), with roads on (it routes along
// them: the road splines' A*, lanes, the vehicle navigation at the ends), stopping 10 m short, a new
// destination within 20 m of it. A car whose driver is gone or out (shot, pulled out, the player took it)
// is handed back to the game.
//
// Reactions, as Guerrilla's: the drivers count as ambient (npc +0x1700 = 1, NPC_MANAGER_AMBIENT, which
// Armageddon never sets), so the game's own avoidance inside travel_to sets Guerrilla's ambient waits (a
// person or the player ahead, the player's car, a queue: 500 to 1500 ms; vehicle AI +0x3F1C, the blocker at
// +0x3F18); while one runs the car brakes (vehicle_set_controls_when_abandoned 0x610820) and may honk
// (civilian_horn_do_honk's rule: 20% a honk of 500 to 2000 ms, else 10 to 15 s before the next roll). A
// driver with a flee belief (ai_get_flee_to_safety_belief 0x483C90: an explosion, damage, a bullet impact,
// combat sounds within 10 s, 30 s for civilians; ai_get_enemy_flee_belief 0x483B00) drives off at its
// car's full speed (the 9 m/s cap cleared), away from the threat, stopping only for the player on foot.
// A car on fire, wrecked or flipped: its people get out (at once if it burns, else once it's nearly
// stopped: 1.5 m/s) and run (npc::start_active_despawn 0x8AAEC0, Armageddon's own "run, then vanish").
// No new car while the world has more than 90 people or 28 cars (Guerrilla's caps). Despawning, as
// Guerrilla's should_despawn_vehicle: a car more than 60 m away whose driver has been out of view 5 s,
// 20 s after it was made (10 s if it's beyond the 260 m ring); any car 280 m away; its people first.
#define RFA_MOD_NAME "guerrilla_vehicles"
#include "rfa_mod.h"
#include "crews.h"
#include "cars.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

void StartTraffic();
void ReportSpawnPools();  // guerrilla_vehicles.cpp: what the spawn pools hold, for the log

namespace {

using namespace gv;

auto const HonkHorn = reinterpret_cast<void(__cdecl*)(DWORD vehicle)>(0x5FB030);     // automobile_honk_horn (a live automobile's handle)
auto const ReleaseHorn = reinterpret_cast<void(__cdecl*)(DWORD vehicle)>(0x5FB110);  // automobile_release_horn
constexpr float kSpeed = 9.0f, kMinSpawn = 60.0f, kMaxSpawn = 260.0f, kApart = 30.0f, kDespawn = 280.0f, kReached = 20.0f, kStopShort = 10.0f;
// Guerrilla's caps by alert level and density (max_vehicle_count 0xB41110, its table 0x13360C8): green, yellow, orange, red.
constexpr int kCaps[4][4] = {{0, 4, 6, 10}, {0, 4, 6, 8}, {0, 2, 3, 4}, {0, 1, 2, 3}};
// The world's counts by kind (World +0x308 + kind × 12, the count at +8): people (2) and automobiles (5).
constexpr DWORD kPeopleCount = 0x1870958, kCarCount = 0x187097C;
// Guerrilla's 90 people (its NPC pool holds 100, as Armageddon's) and 28 cars: its pool's 32 less 4
// (near_auto_population_cap RM 0xB47170). The pool holds kCarsAtOnce here (cars.h), so the cars' cap keeps
// Guerrilla's distance from it.
constexpr int kMostPeople = 90, kMostCars = kCarsAtOnce - (kGuerrillaCars - 28);
constexpr float kHonkChance = 0.2f, kBailSpeed = 1.5f, kNear = 60.0f;

struct Car {
    DWORD vehicle;
    std::vector<Person> people;  // the driver first
    std::vector<std::string> waiting;  // people not made yet
    V3 destination{};
    bool hasDestination = false, driving = false;
    int failures = 0;  // in a row
    int since = 0;     // the game's clock
    // On the game's clock.
    int made = 0, outOfViewSince = -1, flippedSince = -1;
    int horn = -1;  // the horn rule's timer
    bool hornDown = false, fleeing = false, bailing = false;
    V3 lastPosition{};
    int lastTime = -1;
    float speed = 0;  // m/s, from its last frame's move
};
std::vector<Car> g_cars;
struct Pending {
    BYTE parameters[0x100];  // vehicle_spawn_params (0x60 bytes and more)
    std::string resource, model;
    int since;
};
std::vector<Pending> g_pending;
int g_nextSpawn = 0;

// A model of one of traffic's vehicle groups.
BYTE* PickFromGroup(const std::string& group) {
    for (const auto& [name, vehicles] : Files().groups)
        if (_stricmp(name.c_str(), group.c_str()) == 0) return PickVehicle(vehicles);
    return nullptr;
}

void TrySpawn(const BYTE* player, const Zone& zone) {
    const bool night = [] { const int hour = GameHour(); return hour >= 18 || hour < 6; }();
    const int density = std::clamp(night ? zone.night : zone.day, 0, 3);
    if (static_cast<int>(g_cars.size() + g_pending.size()) >= kCaps[CampaignAlert()][density]) return;
    // Guerrilla's caps on the whole world (do_frame_spawn_vehicle): more than 90 people or 28 cars (of its 32), no
    // new car; here kMostCars of kCarsAtOnce.
    const int people = *reinterpret_cast<const int*>(kPeopleCount), cars = *reinterpret_cast<const int*>(kCarCount);
    RFA_LOG_DETAIL_FIRST(1, "Traffic: the world has %d people and %d cars (no new car above %d or %d)", people, cars, kMostPeople, kMostCars);
    const bool counted = people >= 0 && people < 100000 && cars >= 0 && cars < 100000;
    if (counted && (people > kMostPeople || cars > kMostCars)) {
        RFA_LOG_DETAIL_FIRST(5, "Traffic: no new car: the world has %d people and %d cars", people, cars);
        return;
    }
    const V3 at = Pos(player);
    for (int attempt = 0; attempt < 6; ++attempt) {
        const float angle = Random() * 6.2831853f, distance = kMinSpawn + Random() * (kMaxSpawn - kMinSpawn);
        V3 where, forward;
        if (!RoadPlace({at.x + std::cos(angle) * distance, at.y, at.z + std::sin(angle) * distance}, where, forward)) continue;
        const float fromPlayer = Distance(where, at);
        if (fromPlayer < kMinSpawn || fromPlayer > kMaxSpawn) continue;
        bool crowded = false;
        for (const Car& car : g_cars)
            if (BYTE* vehicle = VehicleByHandle(kWorld, nullptr, car.vehicle))
                if (Distance(Pos(vehicle), where) < kApart) crowded = true;
        if (crowded) continue;
        BYTE* entry = PickFromGroup(zone.group);
        if (!entry) return;
        Pending pending{};
        MakeSpawnParameters(pending.parameters, nullptr);
        *reinterpret_cast<BYTE**>(pending.parameters + 0x00) = entry;
        *reinterpret_cast<V3*>(pending.parameters + 0x04) = where;
        Orientation(forward, reinterpret_cast<float*>(pending.parameters + 0x18));
        *reinterpret_cast<int*>(pending.parameters + 0x3C) = -1;          // any variant
        *reinterpret_cast<float*>(pending.parameters + 0x40) = kSpeed;    // already moving
        *reinterpret_cast<int*>(pending.parameters + 0x44) = -1;          // no parent
        *reinterpret_cast<int*>(pending.parameters + 0x4C) = 3;           // priority, as drop_car's
        *reinterpret_cast<DWORD*>(pending.parameters + 0x58) = 0x40B;     // Guerrilla's: no save, one attempt, on a road, moving
        pending.resource = zone.resource;
        if (const char* name = *reinterpret_cast<const char* const*>(entry)) pending.model = name;
        pending.since = GameTime();
        g_pending.push_back(pending);
        return;
    }
}

void MadeCar(BYTE* vehicle, const std::string& resource, const std::string& model) {
    NoteMadeBy(Handle(vehicle), "traffic");
    Car car;
    car.vehicle = Handle(vehicle);
    car.since = GameTime();
    car.made = GameTime();
    if (const auto* people = People(resource, "Guerilla")) {  // the zone's civilians and guerrillas
        // spawn_single_vehicle (RM 0xB5EE60): 1 to 3 aboard, at most its seats; 2 fewer at orange or red or while an
        // activity runs (at least 1); only the driver while the world holds more than 75 people.
        const int seats = (std::max)(1, Seats(vehicle));
        int count = (std::min)(seats, 1 + std::rand() % 3);
        if (CampaignAlert() >= 2 || CampaignActivityRunning()) count = (std::max)(1, count - 2);
        const int inWorld = *reinterpret_cast<const int*>(kPeopleCount);
        if (inWorld > 75 && inWorld < 100000) count = 1;
        for (int i = 0; i < count; ++i) car.waiting.push_back((*people)[std::rand() % people->size()]);
    }
    RFA_LOG_DETAIL_FIRST(100, "Traffic: %s on the road, %u aboard (%s)", model.c_str(), static_cast<unsigned>(car.waiting.size()), resource.c_str());
    g_cars.push_back(car);
}

void Board(Car& car, BYTE* vehicle) {
    for (auto name = car.waiting.begin(); name != car.waiting.end();) {
        BYTE* info = HumanInfo(name->c_str());
        float orientation[9];
        memcpy(orientation, vehicle + kOrientation, sizeof(orientation));
        BYTE* human = info ? MakeHuman(info, reinterpret_cast<const float*>(vehicle + kPosition), orientation, nullptr) : nullptr;
        if (!human) {
            if (!info) name = car.waiting.erase(name);  // not one of the game's characters
            else ++name;                                 // not streamed in yet: next time
            continue;
        }
        int enter[4] = {-1, 1, -1, 5};  // any seat, the best; teleport, minimal processing
        RequestEnter(human, vehicle, enter);
        SitOut(human);
        int& management = *reinterpret_cast<int*>(human + kManagement);
        car.people.push_back({Handle(human), management});
        management = kAmbient;  // Guerrilla's ambient traffic: the game's avoidance gives it ambient waits
        name = car.waiting.erase(name);
    }
}

void LetGoOfHorn(Car& car, BYTE* vehicle) {
    if (car.hornDown && vehicle && IsAutomobile(vehicle)) ReleaseHorn(car.vehicle);
    car.hornDown = false;
}

// Guerrilla's civilian_horn_do_honk, each frame a car waits: a roll (20%) for a honk of 500 to 2000 ms,
// else 10 to 15 s before the next roll.
void HornFrame(Car& car, BYTE* vehicle) {
    if (!IsAutomobile(vehicle)) return;
    if (TimerSet(&car.horn, nullptr) && !TimerElapsed(&car.horn, nullptr)) return;
    if (TimerSet(&car.horn, nullptr)) {  // ran out
        if (car.hornDown) {
            LetGoOfHorn(car, vehicle);
            TimerStart(&car.horn, nullptr, 10000 + std::rand() % 5001);
        } else TimerClear(&car.horn, nullptr);
        return;
    }
    if (Random() <= kHonkChance) {
        HonkHorn(car.vehicle);
        car.hornDown = true;
        TimerStart(&car.horn, nullptr, 500 + std::rand() % 1501);
        RFA_LOG_DETAIL_FIRST(5, "Traffic: a car honks at what's in its way");
    } else {
        TimerStart(&car.horn, nullptr, 10000 + std::rand() % 5001);
        LetGoOfHorn(car, vehicle);
    }
}

// A honk lasts its 500 to 2000 ms, waiting or not.
void HornDone(Car& car, BYTE* vehicle) {
    if (car.hornDown && TimerSet(&car.horn, nullptr) && TimerElapsed(&car.horn, nullptr)) {
        LetGoOfHorn(car, vehicle);
        TimerStart(&car.horn, nullptr, 10000 + std::rand() % 5001);
    }
}

// Unusable as Guerrilla's ai_vehicle_is_usable has it, for traffic: on fire, a wreck, or flipped (on its
// side or roof for 1.5 s).
const char* Unusable(Car& car, BYTE* vehicle, int now) {
    if (OnFire(vehicle)) return "it's on fire";
    if (*reinterpret_cast<const DWORD*>(vehicle + kVehicleFlags) & (1u << 10)) return "it's wrecked";
    const float upY = *reinterpret_cast<const float*>(vehicle + kOrientation + 16);  // the up vector's y
    if (upY >= 0.3f) { car.flippedSince = -1; return nullptr; }
    if (car.flippedSince < 0) car.flippedSince = now;
    return now - car.flippedSince >= 1500 ? "it's flipped" : nullptr;
}

// Whether the game has asked one of the car's people out of his seat: npc +0x1554 bit 21, Guerrilla's npc_flags
// force_out_of_vehicle (+0xD04 bit 18; Armageddon's word has three more bits first, as tied_up's 2 → 5).
constexpr DWORD kNpcFlags = 0x1554, kForceOutOfVehicle = 1u << 21;
bool AskedOut(const Car& car) {
    for (const Person& person : car.people)
        if (BYTE* human = HumanByHandle(kWorld, nullptr, person.handle))
            if (VehicleOf(human) == car.vehicle && (*reinterpret_cast<const DWORD*>(human + kNpcFlags) & kForceOutOfVehicle)) return true;
    return false;
}

const char* VehicleNameOf(const BYTE* vehicle) {
    const BYTE* entry = vehicle ? *reinterpret_cast<const BYTE* const*>(vehicle + 0x3DA8) : nullptr;  // its vehicles.xtbl entry, the name first
    const char* name = entry ? *reinterpret_cast<const char* const*>(entry) : nullptr;
    return name && *name ? name : "a car";
}

// Gives the car and its people back to the game: its people as they were before (their AI no longer
// told to sit frames out once the car isn't ours).
void HandBack(Car& car, BYTE* vehicle) {
    LetGoOfHorn(car, vehicle);
    if (vehicle && car.driving) {
        OverrideSpeed(vehicle, 0);
        SetVehicleAiState(vehicle + kVehicleAi, 0);
    }
    car.driving = false;
    for (const Person& person : car.people)
        if (BYTE* human = HumanByHandle(kWorld, nullptr, person.handle)) *reinterpret_cast<int*>(human + kManagement) = person.management;
}

void Despawn(Car& car) {
    for (const Person& person : car.people) RemoveObject(kWorld, nullptr, person.handle, 0);
    RemoveObject(kWorld, nullptr, car.vehicle, 0);
}

// The car's people get out (once it's nearly stopped, at once if it burns) and run; true once they have.
bool BailOut(Car& car, BYTE* vehicle, const char* why) {
    if (!car.bailing) {
        car.bailing = true;
        RFA_LOG_DETAIL_FIRST(20, "Traffic: a car's people get out (%s)", why);
    }
    StopCar(vehicle);
    LetGoOfHorn(car, vehicle);
    if (!OnFire(vehicle) && car.speed > kBailSpeed) {
        for (const Person& person : car.people)
            if (BYTE* human = HumanByHandle(kWorld, nullptr, person.handle)) SitOut(human);
        return false;
    }
    HandBack(car, vehicle);
    for (const Person& person : car.people) {
        BYTE* human = HumanByHandle(kWorld, nullptr, person.handle);
        if (!human || *reinterpret_cast<const DWORD*>(human + kInVehicle) != car.vehicle) continue;
        BYTE exit[4] = {};  // vi_exit_data: nothing asked
        RequestExit(human, exit);
        StartActiveDespawn(human, nullptr, 2, vehicle);  // run from the car, then vanish (Armageddon's despawn goal)
    }
    return true;
}

// Guerrilla's should_despawn_vehicle: away more than 60 m, its driver out of view 5 s, 20 s after the car
// was made (10 s beyond the spawn ring); always 280 m away.
bool ShouldDespawn(Car& car, BYTE* vehicle, BYTE* driver, const V3& player, int now) {
    const V3 position = Pos(vehicle);
    const float away = Distance(position, player);
    if (away > kDespawn) return true;
    const V3 at = driver ? Pos(driver) : position;
    const V3 head{at.x, at.y + 0.85f, at.z};
    if (away <= kNear || InView(&head.x, 1.25f)) { car.outOfViewSince = -1; return false; }
    if (car.outOfViewSince < 0) car.outOfViewSince = now;
    const int age = now - car.made;
    if (age < 10000 || now - car.outOfViewSince < 5000) return false;
    return age >= 20000 || away > kMaxSpawn;
}

// ---- Guerrilla's aggressive despawn of cars (do_frame_despawn_auto_aggressive, RM 0xB4DDE0)
// The world holds kCarsAtOnce cars at once (64: the automobile pool, guerrilla_vehicles.cpp; Guerrilla's 32, cars.h);
// one more is refused with "Ran out of slots for object type (automobile), zone (...)" (world::create_object
// 0x6D44E0), which Armageddon shows as an error. Guerrilla never gets there: every frame (do_frame_despawn RM
// 0xB4DE70, outside its timer), while the world has 30 cars or more (its 32 less 2; here kCarsAtOnce less 2), it
// takes one away: one fading out to be despawned (vehicle flags
// bit 11) that is off screen, else the farthest car from the player with nobody in it that is off screen
// (find_farthest_offscreen_auto RM 0xB4D950); world::destroy_object either way. Off screen is Guerrilla's
// vehicle::is_offscreen (RM 0xB64CA0: the car's box grown by a quarter against the view); here the game's view
// test on a sphere of 5 m round the car (0x768400), about a big truck's box grown so.
constexpr int kAggressiveCars = kCarsAtOnce - (kGuerrillaCars - 30);  // 62
constexpr DWORD kCarIndices = 0x1870974;     // the automobiles' list (World +0x308 + 5 × 12): their indices (ushort), then +8 how many
constexpr DWORD kObjectTable = 0x187068C;    // World +0x5C: every object by index
constexpr DWORD kDespawnAfterFade = 1u << 11;  // vehicle flags (+0x4FB0): m_despawn_after_fade
constexpr float kOffscreenRadius = 5.0f;
auto const DestroyObject = reinterpret_cast<bool(__fastcall*)(void* world, void* unused, BYTE* object, bool flag)>(0x6D5450);

bool Offscreen(const BYTE* car) {
    V3 middle = Pos(car);
    middle.y += 1.0f;
    return !InView(&middle.x, kOffscreenRadius);
}
bool Empty(BYTE* car) {
    const int seats = Seats(car);
    for (int seat = 0; seat < seats; ++seat)
        if (SeatOccupant(car, seat)) return false;
    return true;
}
bool BeingDestroyed(const BYTE* object) { return (object[0x55] & 0x80) != 0; }

// The most cars the world has held this run, for the log: each new high from 24 on, with how much of the game's
// address space is in use then (GlobalMemoryStatusEx: its total less what's free; a 32-bit game has 4 GB), so a
// run shows what the cars cost beyond their 24 KB slots (their physics, their models' instances, their AI).
void NoteMostCars(int count) {
    static int most = 0;
    if (count <= most || count > 1000) return;
    most = count;
    if (count < 24) return;
    MEMORYSTATUSEX memory{sizeof(memory)};
    const bool measured = GlobalMemoryStatusEx(&memory) != 0;
    const unsigned long inUse = measured ? static_cast<unsigned long>((memory.ullTotalVirtual - memory.ullAvailVirtual) >> 20) : 0;
    RFA_LOG_DETAIL_FIRST(64, "Cars: %d in the world, the most this run (it holds %d; traffic stops at %d, the farthest empty car goes from %d on); the game's "
                  "address space in use: %lu MB", count, kCarsAtOnce, kMostCars, kAggressiveCars, inUse);
}

void AggressiveDespawn(BYTE* player) {
    const int count = *reinterpret_cast<const int*>(kCarCount);
    NoteMostCars(count);
    if (count < kAggressiveCars) return;
    const unsigned short* indices = *reinterpret_cast<const unsigned short* const*>(kCarIndices);
    BYTE* const* objects = *reinterpret_cast<BYTE* const* const*>(kObjectTable);
    if (!indices || !objects) return;
    BYTE* fading = nullptr;
    BYTE* farthest = nullptr;
    float farthestAway = 0.0f;
    for (int i = 0; i < count; ++i) {
        BYTE* car = objects[indices[i]];
        if (!car || BeingDestroyed(car)) continue;
        if (*reinterpret_cast<const DWORD*>(car + kVehicleFlags) & kDespawnAfterFade) {
            if (Offscreen(car)) { fading = car; break; }
            continue;
        }
        if (!Empty(car)) continue;
        const float away = Distance(Pos(car), Pos(player));
        if (away > farthestAway && Offscreen(car)) {
            farthestAway = away;
            farthest = car;
        }
    }
    BYTE* gone = fading ? fading : farthest;
    if (!gone) return;
    RFA_LOG_DETAIL_FIRST(40, "Cars: %d in the world (one taken away from %d on, of %d: Guerrilla's 30 of 32); %s taken away, %.0f m from the player, off screen",
                  count, kAggressiveCars, kCarsAtOnce, fading ? "one fading out" : "the farthest empty one", Distance(Pos(gone), Pos(player)));
    DestroyObject(kWorld, nullptr, gone, false);
}

// ---- Vehicles near the player that don't show (the log)
// A vehicle draws at its fade's alpha (0x60F2B0: while fading in, the time so far over the fade's; while fading out,
// the rest of it, and 0 once done, when Guerrilla's vehicle_post_sim_fade_despawn (RM 0xB79FB0; Armageddon's
// 0x60E530) destroys a vehicle to be despawned; else 1), unless it's hidden (+0x4FB0 bit 18, 0x605500). The user
// found one at the Badlands safehouse (2026-11-13, a Min_ATV_MG2 made with three others after the respawn's reload)
// with a body and no picture, and the log couldn't tell why (its model's files are whole, the game's fades and
// despawns are Guerrilla's, nothing of ours removed it). So twice a second, a vehicle within 40 m of the player
// that would draw at alpha 0, is hidden, or whose model isn't loaded is logged once with its state, and so is the
// vehicle the player gets into, each time.
// Every new vehicle fades in over a second (0x5FFAC0, at its making, unless +0x4FB4 bit 24: 0x605660 with 1000 ms;
// the alpha is the game clock's elapsed over that), so one seen in its first 50 ms read 0.01 and was logged as not
// showing though it went on to show (Walker_Min, 2026-11-21 12:19, 9 ms after its making). A vehicle is logged only
// once it has still not shown 2 s after the watch first saw it so: a fade that never ends, or one restarted.
auto const VehicleAlpha = reinterpret_cast<float(__fastcall*)(BYTE* vehicle, void* unused)>(0x60F2B0);
auto const VehicleCount = reinterpret_cast<int(__fastcall*)(void* world, void* unused)>(0x477090);
auto const VehicleAt = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int index)>(0x4770E0);
constexpr DWORD kFadingIn = 1u << 15, kFadingOut = 1u << 16, kFromNano = 1u << 17, kHidden = 1u << 18;
constexpr float kLookRadius = 40.0f;
constexpr int kUnseenPatience = 2000;
std::vector<DWORD> g_unseenLogged;
std::vector<std::pair<DWORD, int>> g_unseenSince;  // vehicles not showing yet, and when the watch first saw them so
DWORD g_lastBoarded = kNone;
int g_nextLook = 0;

// Who made a vehicle: one of the mod's spawners (NoteMadeBy), else a vehicle spawn node whose car it is (node
// +0xA8: the parked vehicles of Guerrilla's bases, camps and spots), else the game itself (a script, the Spawn
// menu, the console's drop_car, Armageddon's own).
auto const NodeCount = reinterpret_cast<int(__fastcall*)(void* world, void* unused)>(0x804C00);
auto const NodeAt = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int index)>(0x804C50);
constexpr DWORD kNodeCar = 0xA8;
// Its model's streaming (the vehicles' pool, see guerrilla_vehicles.cpp): the record (0x7618A0), reserved (0x75FE90)
// or in use (0x75FEB0), its container's state (0x909210 on the record's +8, 0x55B3D0: 3 streamed in).
auto const ModelRecord = reinterpret_cast<void*(__cdecl*)(void* entry)>(0x7618A0);
auto const ModelReserved = reinterpret_cast<bool(__fastcall*)(void* record, void* unused)>(0x75FE90);
auto const ModelInUse = reinterpret_cast<bool(__fastcall*)(void* record, void* unused)>(0x75FEB0);
auto const ModelContainer = reinterpret_cast<int(__fastcall*)(void* record, void* unused)>(0x55B3D0);
auto const ContainerState = reinterpret_cast<int(__cdecl*)(int container)>(0x909210);

void Spawner(BYTE* vehicle, char* out, size_t size) {
    if (const char* maker = MadeBy(Handle(vehicle))) { sprintf_s(out, size, "%s", maker); return; }
    for (int i = 0, n = NodeCount(kWorld, nullptr); i < n; ++i) {
        BYTE* node = NodeAt(kWorld, nullptr, i);
        if (node && *reinterpret_cast<const DWORD*>(node + kNodeCar) == Handle(vehicle)) {
            const V3 at = Pos(node);
            sprintf_s(out, size, "a parked spot (node 0x%08lX at %.0f %.0f %.0f)", Handle(node), at.x, at.y, at.z);
            return;
        }
    }
    sprintf_s(out, size, "the game (a script, the Spawn menu, drop_car, Armageddon's own)");
}

void Streaming(BYTE* entry, char* out, size_t size) {
    void* record = entry ? ModelRecord(entry) : nullptr;
    if (!record) { sprintf_s(out, size, "no record"); return; }
    const int container = ModelContainer(record, nullptr);
    sprintf_s(out, size, "reserved %d, in use %d, container %s (state %d), loaded %d", ModelReserved(record, nullptr), ModelInUse(record, nullptr),
              container ? "held" : "none", container ? ContainerState(container) : -1, VehicleLoaded(entry));
}

void DescribeVisibility(const char* what, BYTE* vehicle, float distance) {
    const DWORD flags = *reinterpret_cast<const DWORD*>(vehicle + kVehicleFlags);
    BYTE* entry = *reinterpret_cast<BYTE* const*>(vehicle + 0x3DA8);
    const BYTE* o = vehicle + 0x54;  // the object's flags
    const V3 at = Pos(vehicle);
    char spawner[220], streaming[180];
    Spawner(vehicle, spawner, sizeof(spawner));
    Streaming(entry, streaming, sizeof(streaming));
    const int age = ModelAge(Handle(vehicle));
    char made[80];
    if (age >= 0) sprintf_s(made, "; made %d ms after its model streamed in", age);
    else sprintf_s(made, "; its model was in before its spawn asked");
    strcat_s(streaming, made);
    RFA_LOG_DETAIL_FIRST(120, "Vehicles: %s %s (0x%08lX, a %s, at %.0f %.0f %.0f, %.1f m from the player; made by %s): draws at alpha %.2f; fading in %d, "
                       "fading out %d (from the nano %d), despawn after fade %d, hidden %d; its model: %s; object flags %02X %02X %02X %02X",
                  what, VehicleNameOf(vehicle), Handle(vehicle),
                  VehicleSubtype(vehicle, nullptr) == kAutomobile ? "car" : (VehicleSubtype(vehicle, nullptr) == kFlyer ? "flyer" : "walker or other"),
                  at.x, at.y, at.z, distance, spawner, VehicleAlpha(vehicle, nullptr), (flags & kFadingIn) != 0, (flags & kFadingOut) != 0,
                  (flags & kFromNano) != 0, (flags & kDespawnAfterFade) != 0, (flags & kHidden) != 0, streaming, o[0], o[1], o[2], o[3]);
}

void LookForUnseenVehicles(BYTE* player) {
    if (!rfa::DetailLog()) return;  // a diagnosis for the log (Log=1)
    const int now = GameTime();
    if (now < g_nextLook) return;
    g_nextLook = now + 500;
    const DWORD in = VehicleOf(player);
    if (in != g_lastBoarded) {
        g_lastBoarded = in;
        if (BYTE* vehicle = in != kNone ? VehicleByHandle(kWorld, nullptr, in) : nullptr) DescribeVisibility("the player got into", vehicle, 0);
    }
    const V3 at = Pos(player);
    for (int i = 0, n = VehicleCount(kWorld, nullptr); i < n; ++i) {
        BYTE* vehicle = VehicleAt(kWorld, nullptr, i);
        if (!vehicle) continue;
        const float distance = Distance(Pos(vehicle), at);
        if (distance > kLookRadius) continue;
        const DWORD flags = *reinterpret_cast<const DWORD*>(vehicle + kVehicleFlags);
        BYTE* entry = *reinterpret_cast<BYTE* const*>(vehicle + 0x3DA8);
        const DWORD handle = Handle(vehicle);
        auto since = std::find_if(g_unseenSince.begin(), g_unseenSince.end(), [&](const std::pair<DWORD, int>& s) { return s.first == handle; });
        if (VehicleAlpha(vehicle, nullptr) > 0.05f && !(flags & kHidden) && entry && VehicleLoaded(entry)) {
            if (since != g_unseenSince.end()) g_unseenSince.erase(since);
            continue;
        }
        if (std::find(g_unseenLogged.begin(), g_unseenLogged.end(), handle) != g_unseenLogged.end()) continue;
        if (since == g_unseenSince.end()) {  // first seen so: wait, a fade-in may be under way
            if (g_unseenSince.size() < 200) g_unseenSince.push_back({handle, now});
            continue;
        }
        if (now - since->second < kUnseenPatience) continue;
        g_unseenSince.erase(since);
        if (g_unseenLogged.size() < 200) g_unseenLogged.push_back(handle);
        DescribeVisibility("near the player, still not showing 2 s on:", vehicle, distance);
    }
    // Vehicles gone or out of the radius before they showed: forgotten after a minute.
    g_unseenSince.erase(std::remove_if(g_unseenSince.begin(), g_unseenSince.end(), [&](const std::pair<DWORD, int>& s) { return now - s.second > 60000; }),
                        g_unseenSince.end());
}

// A walker's first 6 s (the log, Log=1). Area defense's given Walker_Min fell under the ground twice (2026-11-21
// 12:19 and 14:42), made by its layer's node 5 cm below the ground (Guerrilla's own height: its node spawns place
// a vehicle where the node is, static_spawn_placement 0x100, no snap to the ground); Walker_Min2 at the same node
// didn't. Each walker's height is logged as it settles, and a drop of 3 m in its first 6 s as a fall through the
// ground, with its state.
constexpr int kWalkerSubtype = 8, kWalkerWatch = 6000;
struct NewWalker {
    DWORD handle;
    int since, nextLog;
    float startY, lastY;
    bool fell;
};
std::vector<NewWalker> g_newWalkers;
std::vector<DWORD> g_walkersSeen;

void WatchNewWalkers(BYTE* player) {
    if (!rfa::DetailLog()) return;
    const int now = GameTime();
    for (int i = 0, n = VehicleCount(kWorld, nullptr); i < n; ++i) {
        BYTE* vehicle = VehicleAt(kWorld, nullptr, i);
        if (!vehicle || VehicleSubtype(vehicle, nullptr) != kWalkerSubtype) continue;
        const DWORD handle = Handle(vehicle);
        if (std::find(g_walkersSeen.begin(), g_walkersSeen.end(), handle) != g_walkersSeen.end()) continue;
        if (g_walkersSeen.size() >= 256) g_walkersSeen.erase(g_walkersSeen.begin());
        g_walkersSeen.push_back(handle);
        const float y = Pos(vehicle).y;
        g_newWalkers.push_back({handle, now, now, y, y, false});
    }
    const V3 me = Pos(player);
    for (auto walker = g_newWalkers.begin(); walker != g_newWalkers.end();) {
        BYTE* vehicle = VehicleByHandle(kWorld, nullptr, walker->handle);
        const int age = now - walker->since;
        if (!vehicle || age > kWalkerWatch || walker->fell) { walker = g_newWalkers.erase(walker); continue; }
        const V3 at = Pos(vehicle);
        if (!walker->fell && at.y < walker->startY - 3.0f) {
            walker->fell = true;
            char state[64];
            sprintf_s(state, "fell %.1f m in %d ms", walker->startY - at.y, age);
            DescribeVisibility(state, vehicle, Distance(at, me));
        } else if (now >= walker->nextLog) {
            RFA_LOG_DETAIL_FIRST(120, "Vehicles: walker %s (0x%08lX) %d ms after it appeared: at %.2f %.2f %.2f (%+.2f m from where it appeared, %+.2f since the last look), %.1f m from the player",
                                 VehicleNameOf(vehicle), walker->handle, age, at.x, at.y, at.z, at.y - walker->startY, at.y - walker->lastY, Distance(at, me));
            walker->lastY = at.y;
            walker->nextLog = now + (age < 1000 ? 250 : 1000);
        }
        ++walker;
    }
}

// Every frame on the game's thread (from the spawn nodes' frame, 0x8028B0).
void TrafficFrame() {
    if (!InTrafficWorld()) {
        g_cars.clear();
        g_pending.clear();
        return;
    }
    BYTE* player = Player();
    if (!player) return;
    AggressiveDespawn(player);
    LookForUnseenVehicles(player);
    WatchNewWalkers(player);
    const V3 at = Pos(player);
    const int now = GameTime();  // the game's clock: a pause doesn't count
    const DWORD playerVehicle = *reinterpret_cast<const DWORD*>(player + kInVehicle);

    // The spawns waiting for their model.
    for (auto pending = g_pending.begin(); pending != g_pending.end();) {
        const int result = SpawnVehicle(pending->parameters);
        BYTE* vehicle = *reinterpret_cast<BYTE**>(pending->parameters + 0x5C);
        if (result == 0 && vehicle) {
            MadeCar(vehicle, pending->resource, pending->model);
            g_nextSpawn = now + 2000 + std::rand() % 2000;
        }
        if (result == 1 && now - pending->since < 15000) { ++pending; continue; }
        if (result != 0) RFA_LOG_DETAIL_FIRST(20, "Traffic: %s wasn't made (%s)", pending->model.c_str(), result == 1 ? "not streamed in within 15 s" : "the game refused it");
        pending = g_pending.erase(pending);
    }
    // The cars.
    const int clock = GameTime();
    for (auto car = g_cars.begin(); car != g_cars.end();) {
        BYTE* vehicle = VehicleByHandle(kWorld, nullptr, car->vehicle);
        if (!vehicle) {  // gone (blown up, taken away): its people, if any, back to the game
            HandBack(*car, nullptr);
            car = g_cars.erase(car);
            continue;
        }
        const V3 position = Pos(vehicle);
        if (car->lastTime >= 0 && clock > car->lastTime) car->speed = Distance(position, car->lastPosition) * 1000.0f / static_cast<float>(clock - car->lastTime);
        car->lastPosition = position;
        car->lastTime = clock;
        BYTE* driver = car->people.empty() ? nullptr : HumanByHandle(kWorld, nullptr, car->people[0].handle);
        if (playerVehicle != car->vehicle && ShouldDespawn(*car, vehicle, driver, at, clock)) {
            HandBack(*car, vehicle);
            Despawn(*car);
            car = g_cars.erase(car);
            continue;
        }
        if (!car->waiting.empty() && now - car->since < 10000) {
            Board(*car, vehicle);
            driver = car->people.empty() ? nullptr : HumanByHandle(kWorld, nullptr, car->people[0].handle);
        }
        const bool seated = driver && *reinterpret_cast<const DWORD*>(driver + kInVehicle) == car->vehicle;
        if (playerVehicle == car->vehicle || (car->driving && !car->bailing && (!driver || !Driving(driver)))) {
            HandBack(*car, vehicle);  // the player took it, or its driver is dead or out: the game's from now on
            car = g_cars.erase(car);
            continue;
        }
        // On fire, wrecked or flipped: out, and away from it.
        if (car->driving || car->bailing) {
            const char* why = car->bailing ? "" : Unusable(*car, vehicle, clock);
            if (car->bailing || why) {
                if (BailOut(*car, vehicle, why ? why : "")) { car = g_cars.erase(car); continue; }
                ++car;
                continue;
            }
        }
        // Someone asked out of his seat (force_out_of_vehicle: the game sets it in its vi_request_npc_exit when another
        // takes his seat, the player getting in above all): Guerrilla's AI then gets him out (get_out_of_vehicle_goal,
        // RM 0x5EA660, the highest fitness while it's set), which it can't while he sits frames out. So the car is the
        // game's from now on, stopped, and he gets out with his exit, as in Guerrilla.
        if (AskedOut(*car)) {
            RFA_LOG_DETAIL_FIRST(40, "Traffic: someone in %s is asked out of his seat (another takes it): the car is the game's from now on, and he gets out",
                          VehicleNameOf(vehicle));
            StopCar(vehicle);
            HandBack(*car, vehicle);
            car = g_cars.erase(car);
            continue;
        }
        for (const Person& person : car->people)
            if (BYTE* human = HumanByHandle(kWorld, nullptr, person.handle)) SitOut(human);
        if (!seated || !Driving(driver)) {
            if (now - car->since > 10000 && car->people.empty()) {  // nobody could be made for it
                RFA_LOG_DETAIL_FIRST(20, "Traffic: nobody of its zone's people could be made for a car; it's taken away out of sight");
                Crew empty;
                empty.vehicle = car->vehicle;
                RetireWhenUnseen(empty);
                car = g_cars.erase(car);
                continue;
            }
            if (now - car->since > 15000) {  // its people never sat down: left to the game, parked
                RFA_LOG_DETAIL_FIRST(20, "Traffic: a car's driver didn't take the wheel in 15 s; the car is left to the game");
                HandBack(*car, vehicle);
                car = g_cars.erase(car);
                continue;
            }
            ++car;
            continue;
        }
        BYTE* ai = vehicle + kVehicleAi;
        if (*reinterpret_cast<const int*>(ai) != 1) SetVehicleAiState(ai, 1);
        if (!car->driving) { OverrideSpeed(vehicle, kSpeed); car->driving = true; }
        HornDone(*car, vehicle);
        // Fleeing (Guerrilla's ambient_veh_flee_action): the cap off, a destination away from the threat.
        BYTE* belief = FleeToSafetyBelief(driver);
        if (!belief) belief = EnemyFleeBelief(driver);
        V3 from{};
        bool fromThreat = false;
        if (belief) {  // a belief: +0 the object's handle, +4 where it was (Guerrilla's layout)
            from = *reinterpret_cast<const V3*>(belief + 4);
            fromThreat = std::isfinite(from.x) && std::isfinite(from.y) && std::isfinite(from.z) && Distance(from, position) < 300.0f;
        }
        if (!!belief != car->fleeing) {
            car->fleeing = !!belief;
            OverrideSpeed(vehicle, car->fleeing ? 0.0f : kSpeed);  // 0 clears the override: the car's own top speed
            if (car->fleeing) {
                car->hasDestination = false;
                RFA_LOG_DETAIL_FIRST(20, "Traffic: a driver flees%s", fromThreat ? " from what it saw" : "");
            }
        }
        // Waiting (the game's avoidance set it): brake, maybe honk, and don't drive on this frame. A fleeing
        // car only waits for the player on foot.
        int* wait = reinterpret_cast<int*>(vehicle + kWaitTimer);
        if (TimerSet(wait, nullptr)) {
            const bool running = !TimerElapsed(wait, nullptr);
            const bool forPlayer = *reinterpret_cast<const DWORD*>(vehicle + kWaitBlocker) == Handle(player);
            if (running && (!car->fleeing || forPlayer)) {
                RFA_LOG_DETAIL_FIRST(5, "Traffic: a car waits for %s in its way", forPlayer ? "the player" : "something");
                StopCar(vehicle);
                HornFrame(*car, vehicle);
                ++car;
                continue;
            }
            EndWait(vehicle);
        }
        if (!car->hasDestination || Distance(position, car->destination) < kReached) {
            const V3 forward = *reinterpret_cast<const V3*>(vehicle + kOrientation + 24);
            const V3 behind{position.x - forward.x * 50, position.y, position.z - forward.z * 50};
            const V3& away = car->fleeing && fromThreat ? from : behind;
            BYTE* road = ClosestRoad(&position.x, false, 60.0f, false, false);
            car->hasDestination = road && NextRoadPlace(vehicle, road, &car->destination.x, &away.x);
        }
        if (car->hasDestination) {
            const int result = TravelTo(driver, vehicle, &car->destination.x, 0xFFFFFFFF, 0xFFFFFFFF, 1, kStopShort, 0, 0, 0, 1);
            if (result == 0) {
                RFA_LOG_DETAIL_FIRST(20, "Traffic: a car couldn't find its way to (%.0f, %.0f, %.0f)", car->destination.x, car->destination.y, car->destination.z);
                car->hasDestination = false;
                ++car->failures;
            }
            else {
                if (result == 1) car->hasDestination = false;
                car->failures = 0;
            }
        } else ++car->failures;
        if (car->failures > 30) {  // lost its way 30 times in a row: taken away out of sight
            RFA_LOG_DETAIL_FIRST(20, "Traffic: a car lost its way; it's taken away out of sight");
            HandBack(*car, vehicle);
            Crew leaving;
            leaving.vehicle = car->vehicle;
            leaving.people = car->people;
            RetireWhenUnseen(leaving);
            car = g_cars.erase(car);
            continue;
        }
        ++car;
    }
    // A new car now and then, as Guerrilla's: every 250 to 500 ms.
    if (now < g_nextSpawn) return;
    g_nextSpawn = now + 250 + std::rand() % 250;
    if (const Zone* zone = TrafficZoneAt(at)) TrySpawn(player, *zone);
}

// The spawn nodes' frame (cdecl, nothing), each frame from the gameplay update (0x4FE860).
constexpr DWORD kSpawnNodeFrame = 0x8028B0;
constexpr size_t kSpawnNodeFramePrologue = 6;  // push ebp / mov ebp,esp / sub esp,14h
using FrameFn = void(__cdecl*)();
FrameFn g_spawnNodeFrame;

void __cdecl SpawnNodeFrame() {
    g_spawnNodeFrame();
    ReportSpawnPools();
    TrafficFrame();
    gv::ConvoysFrame();
    gv::EdfFrame();
    gv::ChasesFrame();
    gv::SafehousesFrame();
    gv::RidersFrame();
    gv::DropOffFrame();
}

}  // namespace

void StartTraffic() {
    const std::wstring worlds = rfa::IniString(L"GuerrillaVehicles", L"TrafficWorlds", L"rfg");
    ReadWorlds(worlds);
    if (worlds.empty()) { rfa::LogDetail("Traffic: off ([GuerrillaVehicles] TrafficWorlds= names no world)"); return; }
    if (Files().zones.empty()) { rfa::Log("Traffic: no mods\\guerrilla_vehicles\\traffic.txt (build the vehicles); no traffic"); return; }
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14};
    if (memcmp(reinterpret_cast<const void*>(kSpawnNodeFrame), opening, sizeof(opening)) != 0) {
        rfa::Log("Traffic: the game's spawn node frame isn't as expected; no traffic");
        return;
    }
    g_spawnNodeFrame = reinterpret_cast<FrameFn>(rfa::HookFunction(kSpawnNodeFrame, kSpawnNodeFramePrologue, reinterpret_cast<void*>(&SpawnNodeFrame)));
    if (g_spawnNodeFrame)
        rfa::LogDetail("Traffic: Guerrilla's in %ls: %u zones, %u vehicle groups, %u resources' drivers", worlds.c_str(),
                       static_cast<unsigned>(Files().zones.size()), static_cast<unsigned>(Files().groups.size()),
                       static_cast<unsigned>(Files().people.size()));
    else
        rfa::Log("Traffic: could not hook the game's frame (%ls)", worlds.c_str());
    if (g_spawnNodeFrame) {
        gv::StartConvoys();
        gv::StartEdf();
        gv::StartChases();
        gv::StartSafehouses();
        gv::StartDropOff();
    }
}
