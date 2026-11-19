// Guerrilla's vehicle drop-off in its world: a guerrilla drives a vehicle up to the player when the district's
// morale is high, and leaves it to him (vehicle_drop_off_do_frame RM 0x7B1170, do_drop_off_spawn 0x7B0E50,
// drop_off_vehicle_action; Armageddon has none of it). A part of guerrilla_vehicles, run each frame from
// traffic's hook on the spawn nodes' frame.
//
// Guerrilla's rule, every 1 to 2 s, all of these at once:
//   - the player has sprinted without stopping for 10 s (his do_sprint flag; Armageddon's player +0x1830 bit 0x20);
//   - 60 s since the last try (the cooldown starts even when the spawn fails; none at the start of a level);
//   - no mission or activity running, the alert green, the district's effective morale at least 30 %
//     (guerrilla_campaign's GuerrillaCampaign_Running, _Alert and _District);
//   - the player on foot, not in a safehouse (safehouses.cpp) and not in a restricted area
//     (GuerrillaCampaign_InRestrictedArea);
//   - no automobile of any kind within 50 m of him;
//   - not in multiplayer (Armageddon's byte 0x2685118).
// Then one guerrilla (guerilla_squad_1: one of the zone's spawn resource's Guerilla people, crews.txt) in a vehicle
// of the player's team's default group, "Default Guerilla" (traffic.txt's dropoff line), starts 60 m behind the
// camera (its look direction, flattened) and drives at the player (re-read each frame), stopping 10 m short. Within
// 20 m it honks once, for 1 s. Arrived, the driver greets him (amb_pc_greet_morale_high, Guerrilla's
// AMBIENT_PLAYER_GREET_HIGH_MORALE), the vehicle is left braked, and the driver gets out and goes (Guerrilla bars
// him from getting back in and despawns him; here Armageddon's despawn goal: he runs off, then vanishes). The
// vehicle stays, for the player.
//
// Not as Guerrilla's: it finds the nav-mesh cell nearest the spot 60 m behind (within 30 m) and drives there
// without roads; the mod has no nav-mesh query, so the vehicle starts on the ambient road nearest that spot (within
// 60 m of it, and 30 m or more from the player), and no drop-off is made where there's none. A drop-off that hasn't
// arrived in 90 s, or whose player went 200 m away, is taken away out of sight. [GuerrillaVehicles] VehicleDropOff=0
// turns it off.
#define RFA_MOD_NAME "guerrilla_vehicles"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "campaign/campaign_api.h"
#include "voices/voices_api.h"
#include "crews.h"
#include <algorithm>
#include <cmath>

namespace gv {

namespace {

constexpr DWORD kPlayerStateFlags = 0x1830, kSprinting = 0x20;  // the player: 0x20 while sprinting (as guerrilla_gear reads it)
constexpr DWORD kCameraLook = 0x11B9CA8;                         // the camera's look direction
constexpr DWORD kCarIndices = 0x1870974, kCarCount = 0x187097C;  // the world's automobiles: their indices (ushort)
constexpr DWORD kObjectTable = 0x187068C;                        // every object by index
auto const HonkHorn = reinterpret_cast<void(__cdecl*)(DWORD vehicle)>(0x5FB030);     // automobile_honk_horn
auto const ReleaseHorn = reinterpret_cast<void(__cdecl*)(DWORD vehicle)>(0x5FB110);  // automobile_release_horn
const auto& g_multiplayer = *reinterpret_cast<const BYTE*>(0x2685118);

constexpr int kSprintMs = 10000, kCooldownMs = 60000, kMostMs = 90000, kHonkMs = 1000, kLeaveMs = 10000;
constexpr float kBehind = 60.0f, kRoadWithin = 60.0f, kNotNearer = 30.0f, kNoCarWithin = 50.0f, kMorale = 0.30f;
constexpr float kStopShort = 10.0f, kArrived = 13.0f, kHonkWithin = 20.0f, kGiveUp = 200.0f;

enum class Stage { None, Spawning, Driving, Leaving };
struct DropOff {
    Stage stage = Stage::None;
    Crew crew;
    VehicleSpawn spawn;
    int started = -1, honkedAt = -1, leftAt = -1;
    bool honked = false, hornDown = false;
};
DropOff g_now;
bool g_on = false, g_inWorld = false;
int g_nextCheck = 0, g_allowedAt = 0, g_sprintSince = -1;

bool Sprinting(const BYTE* player) { return (*reinterpret_cast<const DWORD*>(player + kPlayerStateFlags) & kSprinting) != 0; }

bool CarWithin(const V3& at, float range) {
    const int count = *reinterpret_cast<const int*>(kCarCount);
    const WORD* indices = *reinterpret_cast<WORD* const*>(kCarIndices);
    BYTE* const* objects = *reinterpret_cast<BYTE* const* const*>(kObjectTable);
    if (!indices || !objects || count <= 0 || count > 1000) return false;
    for (int i = 0; i < count; ++i)
        if (const BYTE* car = objects[indices[i]]; car && FlatDistance(Pos(car), at) < range) return true;
    return false;
}

// Guerrilla's do_frame_spawn conditions, but for the sprint, the place and the cooldown (checked by the caller).
const char* WhyNot(const BYTE* player) {
    if (!guerrilla_campaign::Running()) return "no guerrilla_campaign";
    if (g_multiplayer) return "multiplayer";
    if (GuerrillaCampaign_Running(nullptr, nullptr)) return "an activity or mission runs";
    if (GuerrillaCampaign_Alert(nullptr) != 0) return "the alert isn't green";
    GuerrillaCampaignDistrict district{};
    if (!GuerrillaCampaign_District(-1, &district) || district.effectiveMorale < kMorale) return "the district's morale is under 30 %";
    if (VehicleOf(player) != kNone) return "the player drives";
    if (PlayerInSafehouse()) return "the player is in a safehouse";
    const V3 at = Pos(player);
    if (GuerrillaCampaign_InRestrictedArea(at.x, at.z, false)) return "the player is in a restricted area";
    if (CarWithin(at, kNoCarWithin)) return "a vehicle is within 50 m";
    return nullptr;
}

void Reset() { g_now = DropOff{}; }

void Retire(const char* why) {
    RFA_LOG_DETAIL_FIRST(20, "Drop-off: taken away out of sight (%s)", why);
    RetireWhenUnseen(g_now.crew);
    Reset();
}

void TryStart(const BYTE* player, int clock) {
    if (const char* why = WhyNot(player)) {
        RFA_LOG_DETAIL_FIRST(40, "Drop-off: the player sprinted 10 s, but %s", why);
        return;
    }
    g_allowedAt = clock + kCooldownMs;  // as Guerrilla's: the cooldown starts whether or not the spawn works
    const auto* vehicles = DropOffVehicles();
    if (!vehicles) {
        RFA_LOG_FIRST(1, "Drop-off: traffic.txt has no dropoff line (the Default Guerilla vehicles); rebuild the vehicles with GuerrillaBuilder.exe");
        return;
    }
    const V3 at = Pos(player);
    V3 look = *reinterpret_cast<const V3*>(kCameraLook);
    const float length = std::sqrt(look.x * look.x + look.z * look.z);
    if (length < 0.01f) return;
    const V3 spot{at.x - look.x / length * kBehind, at.y, at.z - look.z / length * kBehind};
    V3 where, forward;
    if (!RoadPlace(spot, where, forward) || FlatDistance(where, spot) > kRoadWithin || FlatDistance(where, at) < kNotNearer) {
        RFA_LOG_DETAIL_FIRST(20, "Drop-off: no ambient road near the spot 60 m behind the camera; none this time");
        return;
    }
    const std::vector<std::string>* people = People(ResourceAt(where), "Guerilla");
    BYTE* entry = PickVehicle(*vehicles);
    if (!people || people->empty() || !entry) return;
    Reset();
    PrepareSpawn(g_now.spawn, entry, where, {at.x - where.x, 0, at.z - where.z}, 5.0f, 0x40B);
    g_now.crew.waiting = {(*people)[std::rand() % people->size()]};  // guerilla_squad_1: one guerrilla
    g_now.stage = Stage::Spawning;
    g_now.started = clock;
    rfa::LogDetail("Drop-off: a guerrilla brings the player a %s from %.0f m away", g_now.spawn.model.c_str(), FlatDistance(where, at));
}

void Arrive(BYTE* vehicle, BYTE* driver, int clock) {
    GuerrillaVoices_Play(driver, "amb_pc_greet_morale_high", -1);
    if (g_now.hornDown) ReleaseHorn(g_now.crew.vehicle);
    GetOut(g_now.crew, vehicle, false);  // braked (vehicle_set_controls_when_abandoned), the driver out, handed back
    g_now.stage = Stage::Leaving;
    g_now.leftAt = clock;
    RFA_LOG_DETAIL_FIRST(40, "Drop-off: the guerrilla brought the player a %s and gets out", g_now.spawn.model.c_str());
}

void Frame(BYTE* player, int clock) {
    if (g_now.stage == Stage::Spawning) {
        BYTE* made = nullptr;
        const int result = TrySpawn(g_now.spawn, &made);
        if (result == 1) return;
        if (result == 2) { Reset(); return; }
        g_now.crew.vehicle = Handle(made);
        NoteMadeBy(g_now.crew.vehicle, "a guerrilla's drop-off");
        g_now.stage = Stage::Driving;
    }
    BYTE* vehicle = VehicleByHandle(kWorld, nullptr, g_now.crew.vehicle);
    if (!vehicle) { Reset(); return; }
    if (g_now.stage == Stage::Leaving) {
        // The driver, once out of the vehicle, goes (Guerrilla despawns him; he may not get back in).
        BYTE* driver = g_now.crew.people.empty() ? nullptr : HumanByHandle(kWorld, nullptr, g_now.crew.people[0].handle);
        if (driver && VehicleOf(driver) == kNone) StartActiveDespawn(driver, nullptr, 2, vehicle);
        if (!driver || VehicleOf(driver) == kNone || clock - g_now.leftAt > kLeaveMs) Reset();
        return;
    }
    if (VehicleOf(player) == g_now.crew.vehicle) {  // the player got in before it arrived
        HandBack(g_now.crew, vehicle);
        Reset();
        return;
    }
    if (!Board(g_now.crew, vehicle)) return;
    Track(g_now.crew, vehicle);
    if (g_now.crew.people.empty()) { Retire("nobody could be made for it"); return; }
    BYTE* driver = Driver(g_now.crew, vehicle);
    if (!driver) {
        if (Settling(g_now.crew)) return;
        HandBack(g_now.crew, vehicle);  // its driver dead or out
        Reset();
        return;
    }
    const V3 at = Pos(player), where = Pos(vehicle);
    const float away = FlatDistance(where, at);
    if (clock - g_now.started > kMostMs || away > kGiveUp) { Retire(away > kGiveUp ? "the player went off" : "it took over 90 s"); return; }
    if (g_now.hornDown && clock - g_now.honkedAt >= kHonkMs) {
        ReleaseHorn(g_now.crew.vehicle);
        g_now.hornDown = false;
    }
    if (away <= kArrived && g_now.crew.speed < 2.0f) { Arrive(vehicle, driver, clock); return; }
    if (!g_now.honked && away <= kHonkWithin) {  // once, for a second
        HonkHorn(g_now.crew.vehicle);
        g_now.honked = g_now.hornDown = true;
        g_now.honkedAt = clock;
    }
    YieldDriver(g_now.crew, vehicle);
    if (*reinterpret_cast<const int*>(vehicle + kVehicleAi) != 1) SetVehicleAiState(vehicle + kVehicleAi, 1);
    TravelTo(driver, vehicle, &at.x, kNone, kNone, 0, kStopShort, 0, 0, 0, 1);
}

}  // namespace

void StartDropOff() {
    g_on = rfa::IniInt(L"GuerrillaVehicles", L"VehicleDropOff", 1) != 0;
    rfa::Log(g_on ? "Drop-off: at 30 %% morale or more, sprinting 10 s on foot at green alert brings a guerrilla with a vehicle, as Guerrilla's"
                  : "Drop-off: off ([GuerrillaVehicles] VehicleDropOff=0)");
}

void DropOffFrame() {
    if (!g_on) return;
    if (!InTrafficWorld()) {
        if (g_inWorld) Reset();
        g_inWorld = false;
        return;
    }
    BYTE* player = Player();
    if (!player) return;
    const int clock = GameTime();
    if (!g_inWorld) {  // a level started: no first wait, as Guerrilla's timers start at 0
        g_inWorld = true;
        g_allowedAt = g_nextCheck = 0;
        g_sprintSince = -1;
    }
    if (g_now.stage != Stage::None) { Frame(player, clock); return; }
    // The sprint timer: reset every frame the player isn't sprinting.
    if (!Sprinting(player)) g_sprintSince = -1;
    else if (g_sprintSince < 0) g_sprintSince = clock;
    if (clock < g_nextCheck) return;
    g_nextCheck = clock + RandomBetween(1000, 2000);
    if (clock < g_allowedAt || g_sprintSince < 0 || clock - g_sprintSince < kSprintMs) return;
    TryStart(player, clock);
}

}  // namespace gv
