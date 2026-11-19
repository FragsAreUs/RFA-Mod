// The gun on one of Guerrilla's vehicles while the player drives it: Guerrilla's rule. A part of
// guerrilla_vehicles.
//
// Both games hand a vehicle's gun to the player who drives it when nobody else is on it: getting into the
// driver's seat of a vehicle whose first gun belongs to the first passenger's seat (the scout cars, APCs,
// staff cars and supply trucks with a ring on the roof) makes the driver its gunner (the setting
// Player_vehicle_turret_override, on in both), and a gun a driver owns aims by itself (should_autoaim:
// a vehicle's gun, not a tank's, whose gunner is a player in the driver's seat).
//
// What "by itself" means is where the games differ. Guerrilla (turret::autoaim): a walker's gun follows the
// player's aim point while it can reach it; any other gun looks for the best enemy ahead, and with none
// points straight ahead of the vehicle. The enemy is someone of an unfriendly team, alive, within 50 m, whom
// the gun can hit and who is within about 11 degrees of where the gun points now (or within 10 m and 14
// degrees); the nearest and most central wins, and the one fired at last is kept while the gun's keep-target
// timer runs. Armageddon dropped the walker test: every gun a driving player owns follows the player's aim
// point, which suits its own vehicles (the driver of its LAV or walker aims the gun with the camera).
//
// So for a vehicle that isn't one of Armageddon's own, the gun's aiming (0x830C70) is Guerrilla's again:
// the walker's rule, else the search below, which is the game's own (it still runs it for a gun nobody
// drives) with the same settings (Turret_autoaim_*, alike in both games' tweak tables). Armageddon's own
// vehicles keep Armageddon's rule. In the gunner's seat the gun follows the mouse, and the view stays on the gun,
// as Guerrilla's (see "The gunner's camera" below).
//
// RFA-Mod's own addition, not Guerrilla's: the upgrade TURRET LINK (bought with salvage like Guerrilla's
// own; guerrilla_gear sells it) hands the driver the aim: once bought, the gun follows the player's aim point
// on Guerrilla's vehicles too. See "TURRET LINK" below.
//
// The guns on Guerrilla's vehicles also get Guerrilla's hinge motors and Guerrilla's weights for their parts
// (see "The hinges' motors" and "What a part ... weighs" below): with Armageddon's the gun swung to and fro
// around where it was sent, and a turn or a bump threw it off.
//
// The log says when a vehicle's gun gets or loses its gunner, and who aims it, and for the first minute the
// player drives with a gun, four times a second, where it aims and where the gun points.
#define RFA_MOD_NAME "guerrilla_vehicles"
#include "rfa_mod.h"
#include "gear/gear_api.h"
#include "game.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace gv {

namespace {

// ---- The game's turret (an object): its info at +0x98 (its name first; its flags at +0xD0, bit 6 automated),
// its gunner's handle at +0x9C, its own flags at +0xD0 (bit 0 a vehicle's, bit 7 it has a target of its own
// choosing), where it aims at +0x10C, the keep-target timer at +0x15C and whom it keeps at +0x160. Its forward
// (+0x28) is where the gun points.
constexpr DWORD kTurretInfo = 0x98, kGunner = 0x9C, kTurretFlags = 0xD0, kAimAt = 0x10C, kKeepTimer = 0x15C, kKeepTarget = 0x160;
constexpr DWORD kInfoFlags = 0xD0;
constexpr BYTE kHasOwnTarget = 0x80, kAutomated = 0x40;
constexpr DWORD kPlayerAimPoint = 0x176C;  // the player: where the crosshair is (Guerrilla's aim_pos)
constexpr DWORD kVehicleInfo = 0x3DA8;      // a vehicle: its vehicles.xtbl entry, the name first
constexpr int kWalker = 8;

constexpr DWORD kAutoaim = 0x830C70;           // turret::autoaim
constexpr size_t kAutoaimPrologue = 9;         // push ebp / mov ebp,esp / sub esp,130h
constexpr DWORD kAttachGunner = 0x836C10;      // turret::attach_new_operator
constexpr DWORD kDetachGunner = 0x8370D0;      // turret::detach_operator
constexpr size_t kGunnerPrologue = 6;          // push ebp / mov ebp,esp / sub esp,58h (50h)
using AutoaimFn = void(__fastcall*)(BYTE* turret, void* unused);
using GunnerFn = void(__fastcall*)(BYTE* turret, void* unused, BYTE* human);
AutoaimFn g_autoaim;
GunnerFn g_attachGunner, g_detachGunner;

auto const AimsByItself = reinterpret_cast<bool(__fastcall*)(BYTE* turret, void* unused)>(0x831580);        // turret::should_autoaim
auto const TurretWeapon = reinterpret_cast<BYTE*(__fastcall*)(BYTE* turret, void* unused)>(0x834600);       // turret::get_weapon_inst
auto const TurretVehicle = reinterpret_cast<BYTE*(__fastcall*)(BYTE* turret, void* unused)>(0x8345C0);      // turret::get_vehicle
auto const CanHit = reinterpret_cast<bool(__fastcall*)(BYTE* turret, void* unused, const V3* where)>(0x8364B0);  // turret::can_hit_target
auto const WeaponOwner = reinterpret_cast<DWORD(__fastcall*)(BYTE* weapon, void* unused)>(0x6C9D20);        // a handle
auto const VehicleOfHuman = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human)>(0x866960);                      // human_in_vehicle
auto const SeatOf = reinterpret_cast<int(__cdecl*)(BYTE* vehicle, BYTE* human)>(0x603F90);                 // vehicle_passenger_get_seat
auto const IsPlayer = reinterpret_cast<bool(__cdecl*)(BYTE* human)>(0x7B2600);
auto const Team = reinterpret_cast<int(__fastcall*)(BYTE* human, void* unused)>(0x862B60);
auto const Unfriendly = reinterpret_cast<bool(__cdecl*)(int team, int other)>(0x468EA0);                   // human_teams_are_unfriendly
auto const HumanCount = reinterpret_cast<int(__fastcall*)(void* world, void* unused)>(0x43A860);
auto const HumanAt = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int index)>(0x43A8B0);
auto const HeadPosition = reinterpret_cast<void(__cdecl*)(BYTE* human, V3* out)>(0x868A00);                // human_get_head_pos
auto const InProtectedSeat = reinterpret_cast<bool(__cdecl*)(BYTE* human)>(0x65EB70);                      // human_in_protected_vehicle_pos

// The settings (Turret_autoaim_max_distance, _close_distance, _min_dp, _close_dp, _angle_bias) and whether
// this is a multiplayer game.
float MaxDistance() { return *reinterpret_cast<const float*>(0x218BACC); }
float CloseDistance() { return *reinterpret_cast<const float*>(0x218BAC8); }
float MinDot() { return *reinterpret_cast<const float*>(0x218BAC0); }
float CloseDot() { return *reinterpret_cast<const float*>(0x218BABC); }
float AngleBias() { return *reinterpret_cast<const float*>(0x218BAB8); }
bool Multiplayer() { return *reinterpret_cast<const BYTE*>(0x2685118) != 0; }

// Armageddon's own vehicles with guns (its vehicles.xtbl): their drivers aim with the camera.
const char* const kArmageddons[] = {
    "walker_wreck", "Mar_Flyer_2", "Mar_Combug_1", "Mar_Combug_2", "Mar_Combug_3", "TestTurret", "RF_LAV_1", "RF_LAV_2",
    "Walker_Marauder_Heavy", "Walker_Marauder_Heavy_2", "Walker_Marauder_Heavy_8a", "Walker_Marauder_Heavy_8b", "light_walker_car",
    "Walker_Marauder_Light", "Walker_Marauder_Light_m1", "Cru_Tank_1", "Cru_Tank_2", "TestFlyer", "Behemoth", "Min_Rover_1"};

const char* Name(const BYTE* record) {
    const char* name = record ? *reinterpret_cast<const char* const*>(record) : nullptr;
    return name && *name ? name : "-";
}
const char* VehicleName(const BYTE* vehicle) { return vehicle ? Name(*reinterpret_cast<const BYTE* const*>(vehicle + kVehicleInfo)) : "-"; }
const char* TurretName(const BYTE* turret) { return Name(*reinterpret_cast<const BYTE* const*>(turret + kTurretInfo)); }

bool IsArmageddons(const BYTE* vehicle) {
    const char* name = VehicleName(vehicle);
    for (const char* own : kArmageddons)
        if (_stricmp(own, name) == 0) return true;
    return false;
}

bool IsDead(BYTE* object) {  // the object's own answer (its fifth virtual function)
    using Fn = bool(__fastcall*)(BYTE* self, void* unused);
    return (*reinterpret_cast<Fn**>(object))[4](object, nullptr);
}

float Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Minus(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 Along(const V3& from, const V3& direction, float distance) {
    return {from.x + direction.x * distance, from.y + direction.y * distance, from.z + direction.z * distance};
}

// Guerrilla's search (turret::autoaim past its walker test; Armageddon's 0x830DE5 on is the same): the best
// enemy for the gun, or straight ahead of the vehicle.
void AimAsGuerrillas(BYTE* turret, BYTE* driver, BYTE* vehicle) {
    BYTE& flags = turret[kTurretFlags];
    flags &= static_cast<BYTE>(~kHasOwnTarget);
    V3& aimAt = *reinterpret_cast<V3*>(turret + kAimAt);
    const V3 pointing = *reinterpret_cast<const V3*>(turret + 0x28);
    // From the gun's place, moved onto the vehicle's centre line.
    const V3 right = *reinterpret_cast<const V3*>(vehicle + kOrientation);
    V3 from = Pos(turret);
    from = Along(from, right, -Dot(Minus(from, Pos(vehicle)), right));

    const float range = MaxDistance();
    int* keepTimer = reinterpret_cast<int*>(turret + kKeepTimer);
    const bool keeping = TimerSet(keepTimer, nullptr) && !TimerElapsed(keepTimer, nullptr);
    const DWORD kept = *reinterpret_cast<const DWORD*>(turret + kKeepTarget);
    const int team = Team(driver, nullptr);

    BYTE* best = nullptr;       // someone in the open: aimed at the head
    BYTE* bestInside = nullptr;  // someone inside a vehicle that shields them: aimed at the vehicle
    float bestScore = 3.4e38f, bestInsideScore = 3.4e38f;
    V3 bestAt{}, bestInsideAt{};
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!human || human == driver || IsDead(human) || !Unfriendly(team, Team(human, nullptr))) continue;
        V3 head{};
        HeadPosition(human, &head);
        V3 to = Minus(head, from);
        if (std::fabs(to.x) > range || std::fabs(to.z) > range) continue;
        const float distance = std::sqrt(Dot(to, to));
        if (distance > range) continue;
        if (distance > 0.0001f) to = {to.x / distance, to.y / distance, to.z / distance};
        const float ahead = Dot(to, pointing);
        if (ahead < MinDot() && (distance > CloseDistance() || ahead < CloseDot())) continue;
        if (!CanHit(turret, nullptr, &head)) continue;
        if (keeping && Handle(human) == kept) {
            best = human;
            bestAt = head;
            break;
        }
        const float score = distance - AngleBias() * ahead;
        if (InProtectedSeat(human)) {
            BYTE* theirs = VehicleByHandle(kWorld, nullptr, VehicleOf(human));
            if (theirs && score < bestInsideScore) {
                bestInsideScore = score;
                bestInsideAt = Pos(theirs);
                bestInside = human;
            }
        } else if (score < bestScore) {
            bestScore = score;
            bestAt = head;
            best = human;
        }
    }
    if (best) {
        aimAt = bestAt;
        flags |= kHasOwnTarget;
        *reinterpret_cast<DWORD*>(turret + kKeepTarget) = Handle(best);
    } else if (bestInside) {
        aimAt = bestInsideAt;
        flags |= kHasOwnTarget;
    } else {
        aimAt = Along(Pos(vehicle), Forward(vehicle), range);
    }
}

// ---- TURRET LINK: RFA-Mod's own upgrade (NOT one of Guerrilla's), sold by guerrilla_gear beside Guerrilla's in
// its F7 window and the safehouses' shop (its upgrades.cpp, kOwnUpgrades). Once bought, the gun of a vehicle
// the player drives follows his aim point, which is the game's own rule, in place of Guerrilla's. Asked of
// guerrilla_gear twice a second while the player drives with a gun, so it takes hold in the vehicle, at
// once; without that mod, or before it is bought, Guerrilla's rule stays.
constexpr char kAimUpgrade[] = "rfa_turret_link";
bool AimUpgradeBought() {
    static ULONGLONG askedAt = 0;
    static bool bought = false;
    const ULONGLONG now = GetTickCount64();
    if (askedAt != 0 && now - askedAt < 500) return bought;
    askedAt = now;
    const bool has = GuerrillaGear_UpgradeLevel(kAimUpgrade) > 0;
    if (has != bought)
        rfa::Log(has ? "Turrets: TURRET LINK is bought (RFA-Mod's own upgrade): the gun of a vehicle the player drives follows his aim point from now on"
                     : "Turrets: TURRET LINK is no longer bought: a driver's gun aims by itself again, as Guerrilla's");
    bought = has;
    return bought;
}

// The driver whose gun this is, if Guerrilla's search is what it should do now; null leaves it to the game
// (nobody drives it, one of Armageddon's own vehicles, a walker's gun that can reach the player's aim point,
// or the player has bought TURRET LINK). `rule` says which it is, for the log.
BYTE* DriverForGuerrillasSearch(BYTE* turret, BYTE*& vehicle, const char*& rule) {
    rule = "the game's own rule (it isn't a gun that aims by itself in a single-player game)";
    if (Multiplayer() || !AimsByItself(turret, nullptr)) return nullptr;
    rule = "the game's own rule (an automated gun)";
    const BYTE* info = *reinterpret_cast<const BYTE* const*>(turret + kTurretInfo);
    if (!info || (info[kInfoFlags] & kAutomated)) return nullptr;
    rule = "the game's own rule (its weapon isn't the player's)";
    BYTE* weapon = TurretWeapon(turret, nullptr);
    BYTE* owner = weapon ? HumanByHandle(kWorld, nullptr, WeaponOwner(weapon, nullptr)) : nullptr;
    BYTE* player = Player();
    if (!owner || owner != player) return nullptr;
    rule = "the game's own rule (one of Armageddon's own vehicles, or none)";
    vehicle = VehicleOfHuman(owner);
    if (!vehicle || IsArmageddons(vehicle)) return nullptr;
    rule = "the game's own rule (TURRET LINK is bought: it follows the aim point)";
    if (AimUpgradeBought()) return nullptr;
    rule = "the game's own rule (a walker's gun that can reach the aim point)";
    if (VehicleSubtype(vehicle, nullptr) == kWalker && CanHit(turret, nullptr, reinterpret_cast<const V3*>(player + kPlayerAimPoint))) return nullptr;
    rule = "Guerrilla's rule";
    return owner;
}

// ---- The log: what the gun the player drives with does, four times a second for its first minute
// A turret's physics (0x4BE0D0 gives it), in radians: its gun's pitch at +0x98, its ring's heading at +0x9C
// (measured each frame from the ring's body, in the turret's own frame) and at +0xA0 the heading its ring's
// hinge is asked to hold (the same, counted through whole turns). A human: what its aim drifts by, heading at
// +0xABC and pitch at +0xAC0 (added to a gun it's on). The muzzle is what the game measures a player's gun
// from (calculate_aim_parameters, 0x834660).
constexpr DWORD kPitch = 0x98, kHeading = 0x9C, kAskedHeading = 0xA0, kHeadingDrift = 0xABC, kPitchDrift = 0xAC0;
constexpr int kWatchEvery = 250, kWatchLines = 240;
constexpr float kDegrees = 57.29578f;
auto const TurretPhysics = reinterpret_cast<BYTE*(__fastcall*)(BYTE* turret, void* unused)>(0x4BE0D0);
auto const Muzzle = reinterpret_cast<void(__fastcall*)(BYTE* weapon, void* unused, V3* where, float* orientation, int barrel)>(0x733BD0);  // weapon::get_muzzle_transform
DWORD g_watched = kNone;  // the gun's handle
int g_watchLines, g_nextWatch, g_lastWatch;
bool g_watchLog = false;  // [GuerrillaVehicles] TurretWatchLog=1 (off unless set: four lines a second while driving)
V3 g_lastPlace;
float g_lastHeading;

struct Angles { float right, up; };  // degrees
// A direction as seen from an object: how far right of its forward, and how far up.
Angles SeenFrom(const BYTE* object, const V3& direction, V3* parts = nullptr) {
    const V3 right = *reinterpret_cast<const V3*>(object + kOrientation), up = *reinterpret_cast<const V3*>(object + kOrientation + 12);
    const float x = Dot(direction, right), y = Dot(direction, up), z = Dot(direction, Forward(object));
    if (parts) *parts = {x, y, z};
    return {std::atan2(x, z) * kDegrees, std::atan2(y, std::sqrt(x * x + z * z)) * kDegrees};
}

void Watch(BYTE* turret, const char* rule) {
    if (Handle(turret) != g_watched || g_watchLines >= kWatchLines) return;
    const int now = GameTime();
    if (now < g_nextWatch) return;
    BYTE* vehicle = TurretVehicle(turret, nullptr);
    BYTE* weapon = TurretWeapon(turret, nullptr);
    BYTE* physics = TurretPhysics(turret, nullptr);
    BYTE* player = Player();
    if (!vehicle || !weapon || !physics || !player) return;
    V3 muzzle{};
    float gun[9] = {};
    Muzzle(weapon, nullptr, &muzzle, gun, -1);
    const V3 to = Minus(*reinterpret_cast<const V3*>(turret + kAimAt), muzzle);
    const Angles aims = SeenFrom(vehicle, to), points = SeenFrom(vehicle, {gun[6], gun[7], gun[8]}), mount = SeenFrom(vehicle, Forward(turret));
    V3 out{};
    SeenFrom(vehicle, Minus(muzzle, Pos(turret)), &out);
    const V3 place = Pos(vehicle), nose = Forward(vehicle);
    const float heading = std::atan2(nose.x, nose.z) * kDegrees;
    const float seconds = g_lastWatch ? (now - g_lastWatch) / 1000.0f : 0.0f;
    float turned = heading - g_lastHeading;
    if (turned > 180.0f) turned -= 360.0f;
    if (turned < -180.0f) turned += 360.0f;
    const float speed = seconds > 0.0f ? Distance(place, g_lastPlace) / seconds : 0.0f, turning = seconds > 0.0f ? turned / seconds : 0.0f;
    g_lastWatch = now;
    g_lastPlace = place;
    g_lastHeading = heading;
    g_nextWatch = now + kWatchEvery;
    ++g_watchLines;
    rfa::LogDetail("Turrets: watch %d, %s: it aims %.1f m from the muzzle, %+.1f right %+.1f up of the car's nose%s; the gun points %+.1f right %+.1f up; the game has "
             "its ring at %+.1f (asked for %+.1f) and its gun raised %+.1f; its mount faces %+.1f right %+.1f up; the muzzle is %+.2f right %+.2f up %+.2f ahead of "
             "the mount; the car does %.1f m/s, turning %+.0f a second; the driver's aim drift %+.2f %+.2f",
             g_watchLines, rule, std::sqrt(Dot(to, to)), aims.right, aims.up, (turret[kTurretFlags] & kHasOwnTarget) ? " (at an enemy)" : "", points.right, points.up,
             *reinterpret_cast<const float*>(physics + kHeading) * kDegrees, *reinterpret_cast<const float*>(physics + kAskedHeading) * kDegrees,
             *reinterpret_cast<const float*>(physics + kPitch) * kDegrees, mount.right, mount.up, out.x, out.y, out.z, speed, turning,
             *reinterpret_cast<const float*>(player + kHeadingDrift) * kDegrees, *reinterpret_cast<const float*>(player + kPitchDrift) * kDegrees);
}

void __fastcall Autoaim(BYTE* turret, void* unused) {
    BYTE* vehicle = nullptr;
    const char* rule = "";
    BYTE* driver = DriverForGuerrillasSearch(turret, vehicle, rule);
    if (driver) AimAsGuerrillas(turret, driver, vehicle);
    else g_autoaim(turret, unused);
    if (g_watched != kNone) Watch(turret, rule);
}

// ---- The log: how the player's walker moves (Guerrilla's walk on the spot, 2026-11-13)
// Guerrilla moves a walker by its animation's root motion alone (walker_process_movement_presim RM 0xBB83A0:
// animlib_get_position_change, unrotated into the walker's frame); our walkers' clips keep it (header and root track).
// Armageddon's own step of that name (0x63EB30, cdecl (walker); the name is in the exe at 0xFA1588) works leg by leg:
// its legs from +0xED10, how many at +0xEF20 (the loop's test at 0x63EC6F), each through 0x644C80, 0x645520 and
// 0x643DA0. While the player drives one of Guerrilla's walkers, four times a second for its first 30 s (120 lines),
// the log gives its legs, how far those steps moved it themselves, and how far it really went since the line before.
// [GuerrillaVehicles] WalkerLog=0 turns it off.
constexpr DWORD kWalkerPresim = 0x63EB30, kWalkerLegCount = 0xEF20;
constexpr int kWalkerEvery = 250, kWalkerLines = 120;
using WalkerPresimFn = void(__cdecl*)(BYTE* walker);
WalkerPresimFn g_walkerPresim;
DWORD g_walkerWatched = kNone;
int g_walkerLines, g_walkerSteps;
DWORD g_walkerSince;
V3 g_walkerFrom;
float g_walkerStepMoved;

float Apart(const V3& a, const V3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

void __cdecl WalkerPresim(BYTE* walker) {
    const V3 before = walker ? Pos(walker) : V3{};
    g_walkerPresim(walker);
    BYTE* player = walker ? Player() : nullptr;
    if (!player || VehicleOf(player) != Handle(walker) || IsArmageddons(walker)) return;
    const DWORD now = GetTickCount();
    if (g_walkerWatched != Handle(walker)) {  // a new walker: start its watch
        g_walkerWatched = Handle(walker);
        g_walkerLines = g_walkerSteps = 0;
        g_walkerStepMoved = 0;
        g_walkerSince = now;
        g_walkerFrom = before;
    }
    if (g_walkerLines >= kWalkerLines) return;
    ++g_walkerSteps;
    g_walkerStepMoved += Apart(Pos(walker), before);
    if (now - g_walkerSince < static_cast<DWORD>(kWalkerEvery)) return;
    const float seconds = (now - g_walkerSince) / 1000.0f, went = Apart(Pos(walker), g_walkerFrom);
    ++g_walkerLines;
    rfa::LogDetail("Walkers: watch %d, %s: %d legs; %d movement steps in %.2f s moved it %.2f m themselves; it went %.2f m (%.2f m/s), now at %.1f %.1f %.1f",
             g_walkerLines, VehicleName(walker), *reinterpret_cast<const int*>(walker + kWalkerLegCount), g_walkerSteps, seconds, g_walkerStepMoved,
             went, seconds > 0 ? went / seconds : 0.0f, Pos(walker).x, Pos(walker).y, Pos(walker).z);
    g_walkerSteps = 0;
    g_walkerStepMoved = 0;
    g_walkerSince = now;
    g_walkerFrom = Pos(walker);
}

void StartWalkerLog() {
    if (!rfa::ReadLogSwitch(L"GuerrillaVehicles", L"WalkerLog")) return;
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0xB8, 0x24, 0x1D, 0x00, 0x00};  // push ebp / mov ebp,esp / mov eax,1D24h
    if (memcmp(reinterpret_cast<const void*>(kWalkerPresim), opening, sizeof(opening)) != 0) {
        rfa::Log("Walkers: the walker's movement step (0x%08lX) isn't as expected; no walker log", kWalkerPresim);
        return;
    }
    g_walkerPresim = reinterpret_cast<WalkerPresimFn>(rfa::HookFunction(kWalkerPresim, sizeof(opening), reinterpret_cast<void*>(&WalkerPresim)));
    if (g_walkerPresim) rfa::Log("Walkers: the player's Guerrilla walker's movement is logged (four lines a second, its first 30 s)");
}

// ---- The log: how the player's car handles (the user, 2026-11-18: "very hard to turn/steer ... as if you are on a
// boat and have a very wide turn radius", after Re-Mars-tered)
// Our vehicles.xtbl holds Guerrilla's handling as it is, and both games read and apply the steering the same way (the
// angle × pi/180, the full-steering speed × 0.44704, Havok's default steering). What's left is the car as
// Havok has it, which this logs. Havok's main steering step (hkpVehicleDefaultSteering::calcMainSteeringAngle,
// 0xB07C10, thiscall (steering, dt, the vehicle instance, its input, the output)) runs for every car each physics step;
// for the player's car (its chassis within 3 m of the player's vehicle) the log gives, once, the car as built:
//   - the steering: its max angle (+8, rad) and full-steering speed (+0xC, m/s);
//   - the vehicle data (instance +0x1C): wheels (+0x20), the roll/pitch/yaw torque factors and extra torque (+0x60),
//     the chassis' unit inertias yaw/roll/pitch (+0x74), the friction equalizer, the solver's mass ratio, and each
//     wheel's params (+0x8C, 40 bytes: radius, mass, width, friction, viscosity, max friction, slip angle, axle);
//   - the chassis body (instance +0x18): its mass (1 / +0x1AC), its inverse inertia (+0x1A0), its centre of mass
//     against its origin (+0x140 against +0x120);
// then four times a second for its first minute (240 lines): the steering input and the angle Havok gives, the speed
// along the car and in all, the yaw rate, how far it rolls, the turning radius it drives (speed / yaw rate) against the
// one its wheelbase and angle give (wheelbase / tan(angle)), and each wheel's contact friction, side and forward slip,
// side force and skid. The layout is Havok 2010's, corrected for Armageddon's older Havok (see
// kWheelsInfo); the wheel counts are checked first, and the raw numbers are given if they don't agree. [GuerrillaVehicles] DriveLog=1 turns it on (off unless set).
constexpr DWORD kMainSteeringAngle = 0xB07C10;
using MainSteeringFn = void(__fastcall*)(BYTE* steering, void* unused, float dt, BYTE* instance, BYTE* input, float* output);
MainSteeringFn g_mainSteering;
// Armageddon's Havok is older: its vehicle instance has no m_vehicleSimulation, so everything after the
// velocity damper (+0x44) is 4 bytes earlier, and its WheelInfo (still 0xE0) has no m_noSlipIdealSpinVelocity (the live
// session's read, 2026-11-18 22:28: each wheel's side force equals its axle's in the instance's friction status).
constexpr DWORD kChassis = 0x18, kVehicleData = 0x1C, kSuspension = 0x34, kWheelsInfo = 0x48, kMainAngle = 0xBC, kWheelAngles = 0xC0;
constexpr DWORD kWheelSkid = 0xC8, kWheelSideForce = 0xCC, kWheelForwardSlip = 0xD0, kWheelSideSlip = 0xD4;
constexpr DWORD kWheelSuspensionLength = 0x70;  // the suspension's length now: its full length while the wheel's ray finds no ground
constexpr DWORD kWheelInfoSize = 0xE0, kWheelParamSize = 0x28;
constexpr DWORD kBodyPosition = 0x120, kBodyCentre = 0x140, kBodyInverseInertia = 0x1A0, kBodyVelocity = 0x1B0, kBodySpin = 0x1C0;
constexpr DWORD kBodyAxisRight = 0xE0 + 0x10, kBodyAxisUp = 0xE0 + 0x20, kBodyAxisForward = 0xE0 + 0x30;  // the transform's columns
constexpr int kDriveEvery = 250, kDriveLines = 240;
bool g_driveLog = false;
bool g_wheelRays = false;  // Guerrilla's wheel-ray rules on Guerrilla's cars (below: "A wheel's ray, as Guerrilla casts it")
constexpr DWORD kWheelCollide = 0x3C, kCollideFilterInfo = 0xC;  // the instance's wheel collide; its rays' collision filter info
constexpr DWORD kCollidableFilterInfo = 0x1C;                     // a collidable's collision filter info (its broadphase handle's)
DWORD g_driveCar = kNone, g_driveNext;
int g_driveLines;

template <typename T> T At(const BYTE* p, DWORD offset) { return *reinterpret_cast<const T*>(p + offset); }
float Dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void LogCarAsBuilt(BYTE* steering, BYTE* instance, const char* name) {
    BYTE* data = At<BYTE*>(instance, kVehicleData);
    BYTE* body = At<BYTE*>(instance, kChassis);
    const int wheels = data ? At<signed char>(data, 0x20) : -1;
    const int infos = At<int>(instance, kWheelsInfo + 4), params = data ? At<int>(data, 0x8C + 4) : -1;
    rfa::LogDetail("Driving: %s as built: steering max %.3f rad (%.1f deg), full steering to %.1f m/s (%.1f mph); %d wheels (infos %d, params %d)", name,
             At<float>(steering, 8), At<float>(steering, 8) * 57.29578f, At<float>(steering, 0xC), At<float>(steering, 0xC) / 0.44704f, wheels, infos,
             params);
    if (!data || !body || wheels < 2 || wheels > 8 || infos != wheels || params != wheels) {
        rfa::Log("Driving: the vehicle's data doesn't read as Havok's layout here; data +0x60.. %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX",
                 data ? At<DWORD>(data, 0x60) : 0, data ? At<DWORD>(data, 0x64) : 0, data ? At<DWORD>(data, 0x68) : 0, data ? At<DWORD>(data, 0x6C) : 0,
                 data ? At<DWORD>(data, 0x70) : 0, data ? At<DWORD>(data, 0x74) : 0, data ? At<DWORD>(data, 0x78) : 0, data ? At<DWORD>(data, 0x7C) : 0);
        return;
    }
    const float inverseMass = At<float>(body, kBodyInverseInertia + 0xC);
    const float* origin = reinterpret_cast<const float*>(body + kBodyPosition);
    const float* centre = reinterpret_cast<const float*>(body + kBodyCentre);
    const float offset[3] = {centre[0] - origin[0], centre[1] - origin[1], centre[2] - origin[2]};
    const float* right = reinterpret_cast<const float*>(body + kBodyAxisRight);
    const float* up = reinterpret_cast<const float*>(body + kBodyAxisUp);
    const float* forward = reinterpret_cast<const float*>(body + kBodyAxisForward);
    rfa::LogDetail("Driving: %s's torque factors roll %.2f pitch %.2f yaw %.2f extra %.2f; unit inertia yaw %.2f roll %.2f pitch %.2f; friction equalizer %.2f, "
             "positional friction to %.1f m/s, solver mass ratio %.1f; chassis mass %.0f kg, inverse inertia %.5f %.5f %.5f; centre of mass %.2f right "
             "%.2f up %.2f ahead of its origin",
             name, At<float>(data, 0x60), At<float>(data, 0x64), At<float>(data, 0x68), At<float>(data, 0x6C), At<float>(data, 0x74), At<float>(data, 0x78),
             At<float>(data, 0x7C), At<float>(data, 0x80), At<float>(data, 0x70), At<float>(data, 0x88), inverseMass > 0 ? 1.0f / inverseMass : 0.0f,
             At<float>(body, kBodyInverseInertia), At<float>(body, kBodyInverseInertia + 4), At<float>(body, kBodyInverseInertia + 8),
             Dot3(offset, right), Dot3(offset, up), Dot3(offset, forward));
    const BYTE* param = At<BYTE*>(data, 0x8C);
    for (int w = 0; param && w < wheels; ++w, param += kWheelParamSize)
        rfa::LogDetail("Driving: %s wheel %d (axle %d): radius %.2f, mass %.0f, width %.2f, friction %.2f, viscosity %.2f, max friction %.2f, slip angle %.3f",
                 name, w, At<signed char>(param, 0x24), At<float>(param, 0), At<float>(param, 4), At<float>(param, 8), At<float>(param, 0xC),
                 At<float>(param, 0x10), At<float>(param, 0x14), At<float>(param, 0x18));
    // The suspension as the game built it (0x5EFBC0, Guerrilla's automobile_create_havok_suspension RM 0xBA5110 line for
    // line): per wheel its length (the lesser of compression + extension and the rest point), the rest point (gravity's
    // share over the spring strength, plus the compression distance), Wheel_Spring_Pow, the spring and its two dampings.
    const BYTE* suspension = At<BYTE*>(instance, kSuspension);
    const int springs = suspension ? At<int>(suspension, 0x14 + 4) : 0;
    for (int w = 0; suspension && w < wheels && w < springs && At<int>(suspension, 8 + 4) > w; ++w) {
        const BYTE* wheel = At<BYTE*>(suspension, 8) + w * 0x30;
        const BYTE* spring = At<BYTE*>(suspension, 0x14) + w * 0xC;
        rfa::LogDetail("Driving: %s wheel %d suspension: length %.2f, rest %.2f, spring power %.2f; strength %.1f, damping %.2f compressing, %.2f relaxing",
                 name, w, At<float>(wheel, 0x20), At<float>(At<BYTE*>(suspension, 0x20), w * 4), At<float>(At<BYTE*>(suspension, 0x6C), w * 4),
                 At<float>(spring, 0), At<float>(spring, 4), At<float>(spring, 8));
    }
    const BYTE* collide = At<BYTE*>(instance, kWheelCollide);
    if (collide)
        rfa::LogDetail("Driving: %s's wheel rays: %s; their collision filter info %08lX, the chassis's %08lX", name,
                 g_wheelRays ? "Guerrilla's rules (its own parts, non-solid things and surfaces steeper than 60 degrees skipped; grip x 0.25 + 0.75 x "
                               "the ground's levelness)"
                             : "Armageddon's",
                 At<DWORD>(collide, kCollideFilterInfo), At<DWORD>(body + 0x10, kCollidableFilterInfo));
}

bool g_driveWheels = true;  // false once the wheels' state has faulted a read (Armageddon's wheel array isn't RM's layout)

void DriveLogStep(BYTE* steering, BYTE* instance, BYTE* input, float* output) {
    BYTE* player = Player();
    BYTE* car = player && VehicleOf(player) != kNone ? VehicleByHandle(kWorld, nullptr, VehicleOf(player)) : nullptr;
    BYTE* body = At<BYTE*>(instance, kChassis);
    if (!car || !body || IsArmageddons(car)) return;
    const float* place = reinterpret_cast<const float*>(body + kBodyPosition);
    const V3 at = Pos(car);
    if ((place[0] - at.x) * (place[0] - at.x) + (place[1] - at.y) * (place[1] - at.y) + (place[2] - at.z) * (place[2] - at.z) > 9.0f) return;
    const DWORD now = GetTickCount();
    if (g_driveCar != Handle(car)) {
        g_driveCar = Handle(car);
        g_driveLines = 0;
        g_driveNext = now;
        LogCarAsBuilt(steering, instance, VehicleName(car));
    }
    if (g_driveLines >= kDriveLines || static_cast<int>(now - g_driveNext) < 0) return;
    g_driveNext = now + kDriveEvery;
    ++g_driveLines;
    const float* velocity = reinterpret_cast<const float*>(body + kBodyVelocity);
    const float* spin = reinterpret_cast<const float*>(body + kBodySpin);
    const float* up = reinterpret_cast<const float*>(body + kBodyAxisUp);
    const float* forward = reinterpret_cast<const float*>(body + kBodyAxisForward);
    const float* right = reinterpret_cast<const float*>(body + kBodyAxisRight);
    const float speed = std::sqrt(Dot3(velocity, velocity)), ahead = Dot3(velocity, forward), yaw = Dot3(spin, up);
    const float angle = output[0];
    const int wheels = g_driveWheels ? At<int>(instance, kWheelsInfo + 4) : 0;
    const BYTE* info = g_driveWheels ? At<BYTE*>(instance, kWheelsInfo) : nullptr;
    float wheelbase = 0;
    if (info && wheels >= 4) {  // the first wheel's hardpoint to the last's, along the car
        const float* first = reinterpret_cast<const float*>(info + 0x50);
        const float* last = reinterpret_cast<const float*>(info + (wheels - 1) * kWheelInfoSize + 0x50);
        const float between[3] = {first[0] - last[0], first[1] - last[1], first[2] - last[2]};
        wheelbase = std::fabs(Dot3(between, forward));
    }
    const float driven = std::fabs(yaw) > 0.01f ? std::fabs(ahead / yaw) : 0.0f;
    const float geometric = std::fabs(angle) > 0.001f && wheelbase > 0 ? wheelbase / std::tan(std::fabs(angle)) : 0.0f;
    char wheelText[480] = "";
    for (int w = 0; info && w < wheels && w < 6; ++w) {
        const BYTE* one = info + w * kWheelInfoSize;
        char part[80];
        snprintf(part, sizeof(part), "; w%d %s f%.2f len %.2f slip %.1f/%.1f side %.0f", w, At<BYTE*>(one, 0x24) || At<float>(one, 0x20) > 0 ? "on" : "off",
                 At<float>(one, 0x20), At<float>(one, kWheelSuspensionLength), At<float>(one, kWheelSideSlip), At<float>(one, kWheelForwardSlip),
                 At<float>(one, kWheelSideForce));
        strncat_s(wheelText, part, _TRUNCATE);
    }
    // The handling it is on (the instance's +0x130 bit 1, set by 0x648B30 from the handling reset 0x5F2D39: the AI's) and
    // the body's damping (its motion's +0xB4/+0xB6, half-floats: a float's top 16 bits), which the vehicle step 0xB04E10
    // also puts on the tyre solver.
    const bool aiHandling = (At<BYTE>(instance, 0x130) & 2) != 0;
    auto half = [](WORD bits) { const DWORD whole = static_cast<DWORD>(bits) << 16; float value; memcpy(&value, &whole, 4); return value; };
    rfa::LogDetail("Driving: watch %d, %s (%s handling, damping %.2f, spin %.2f): steer input %+.2f -> angle %+.1f deg; speed %.1f m/s (%.1f ahead); "
             "yaw %+.2f rad/s; up %.2f, roll %+.2f; turning radius %.1f m driven, %.1f m by its wheelbase %.2f m and angle%s",
             g_driveLines, VehicleName(car), aiHandling ? "the AI's" : "the player's", half(At<WORD>(body, 0x194)), half(At<WORD>(body, 0x196)),
             input ? At<float>(input, 8) : 0.0f, angle * 57.29578f, speed, ahead, yaw, up[1], right[1], driven, geometric, wheelbase, wheelText);
}

// Every read of the car's Havok state is guarded: the layout is RM's, and Armageddon's wheel array isn't it (2026-11-18
// 22:09:09, the first wheel's hardpoint read faulted, guerrilla_vehicles+0x2D3B5, the game went down after). A fault
// gives a line; the wheels are left out after the first, and the car's log stops after a second.
void __fastcall MainSteering(BYTE* steering, void* unused, float dt, BYTE* instance, BYTE* input, float* output) {
    g_mainSteering(steering, unused, dt, instance, input, output);
    if (!instance || !output) return;
    __try {
        DriveLogStep(steering, instance, input, output);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        if (g_driveWheels) {
            g_driveWheels = false;
            rfa::Log("Driving: a read of the car's Havok state faulted; the wheels' state is left out from now on");
        } else {
            g_driveLines = kDriveLines;
            rfa::Log("Driving: a read of the car's Havok state faulted again; this car's log stops");
        }
    }
}

// ---- A car's tyre friction, built once as Guerrilla builds it (the "boat" turning, 2026-11-18)
// Both games give the car's body the same inertia (the table's unit inertias × the .ccar_pc's inertia tensor × mass), and
// both build Havok's friction description (each axle's effective mass and the friction circle the solver uses) from the
// vehicle data's unit inertias, while its yaw still holds the tensor-scaled value. Guerrilla builds it once
// (hkpCustomVehicleSimulation::init RM 0xB89B80 → 0xD71510): its AI/driver reset (automobile_reset_handling_for_ai RM
// 0xB733F0) writes the raw table yaw back into the data (+0x74) and re-inits only the data (0xB63B40), never the
// description. Armageddon's data init (0xB02B70, from the reset 0x5F2CF0 through vtbl+0x40) rebuilds the description each
// time (0xB409A0, cdecl (its cinfo, the description at data +0xA4)), so after the first reset ours came from the raw yaw:
// 6.3× too little on the pickup, 1.8× on the scout. The solver took the chassis for that much easier to turn than its
// body is, and let the front axle slide: understeer, wide circles past 30 km/h (the live session's read and measure,
// 22:28-22:33: fronts side-slipping 8-12 m/s, rears 1-4). So for Guerrilla's cars the description built when the car
// is made stays through a reset, as Guerrilla's. Armageddon's own cars keep Armageddon's. [GuerrillaVehicles]
// FrictionAsGuerrilla=0 turns it off.
constexpr DWORD kBuildFriction = 0xB409A0, kHandlingBlockOffset = 0x50F8, kFrictionDescription = 0xA4;
using BuildFrictionFn = void(__cdecl*)(void* cinfo, BYTE* description);
BuildFrictionFn g_buildFriction;
auto const ActiveHandlingBlock = reinterpret_cast<BYTE*(__fastcall*)(BYTE* block, void* unused)>(0x5F7590);
std::vector<BYTE*> g_frictionBuilt;  // descriptions built when their car was made
BYTE* g_frictionKept = nullptr;      // the description a reset is to leave as it is

void __cdecl BuildFriction(void* cinfo, BYTE* description) {
    if (description && description == g_frictionKept) {
        RFA_LOG_DETAIL_FIRST(10, "Driving: a Guerrilla car's tyre friction kept as built when it was made (Guerrilla's reset leaves it)");
        return;
    }
    g_buildFriction(cinfo, description);
    if (!description || g_frictionKept) return;
    if (std::find(g_frictionBuilt.begin(), g_frictionBuilt.end(), description) == g_frictionBuilt.end()) {
        if (g_frictionBuilt.size() >= 512) g_frictionBuilt.clear();
        g_frictionBuilt.push_back(description);
    }
}

void StartFrictionAsGuerrilla() {
    if (!rfa::IniInt(L"GuerrillaVehicles", L"FrictionAsGuerrilla", 1)) return;
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};  // push ebp / mov ebp,esp / and esp,0FFFFFFF0h
    if (memcmp(reinterpret_cast<const void*>(kBuildFriction), opening, sizeof(opening)) != 0) {
        rfa::Log("Driving: the tyre friction's build (0x%08lX) isn't as expected; a reset rebuilds it as Armageddon's", kBuildFriction);
        return;
    }
    g_buildFriction = reinterpret_cast<BuildFrictionFn>(rfa::HookFunction(kBuildFriction, sizeof(opening), reinterpret_cast<void*>(&BuildFriction)));
    if (g_buildFriction) rfa::Log("Driving: Guerrilla's cars keep the tyre friction built when they're made through a reset, as Guerrilla's");
}

void StartDriveLog() {
    g_driveLog = rfa::ReadLogSwitch(L"GuerrillaVehicles", L"DriveLog");
    if (!g_driveLog) return;
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};  // push ebp / mov ebp,esp / and esp,0FFFFFFF0h
    if (memcmp(reinterpret_cast<const void*>(kMainSteeringAngle), opening, sizeof(opening)) != 0) {
        rfa::Log("Driving: Havok's steering step (0x%08lX) isn't as expected; no drive log", kMainSteeringAngle);
        return;
    }
    g_mainSteering = reinterpret_cast<MainSteeringFn>(rfa::HookFunction(kMainSteeringAngle, sizeof(opening), reinterpret_cast<void*>(&MainSteering)));
    if (g_mainSteering) rfa::Log("Driving: the player's Guerrilla car's handling is logged (as built once, then four lines a second for a minute)");
}

// ---- A wheel's ray, as Guerrilla casts it (2026-11-19, "steering still rough when turning at speed")
// Both games cast one ray per wheel, from its hardpoint down the suspension to its full length plus the wheel's radius.
// (Guerrilla's cars never sweep a shape instead: hk_custom_vehicle_raycast_wheel_collide's sweep is only for an
// infinite-mass vehicle, which only "MarsRover1" is.) Two rules of Guerrilla's wheel loop (collideWheels RM 0xB8A390)
// differ from the Havok 2010 loop Armageddon runs (0xB075E0):
//  - Its ray collector (havok_closest_wheel_ray_hit_collector::addRayHit RM 0xB74AB0) skips the car's own chassis and
//    anything with the chassis's collision filter info, anything that isn't an entity (a phantom) or has no owner, and
//    any surface facing less than 60 degrees against the ray (ray . normal > -0.5); it keeps the closest of the rest.
//    Armageddon's (0xB073D0 → the wheels' AABB phantom's 0xACBDA0) keeps the closest hit of whatever the phantom holds.
//  - Its ground friction (calcSingleWheelGroundFriction RM 0xB63DF0) is the body's × (0.25 + 0.75 × the contact
//    normal's up, at least 0); Armageddon's (0xB076B0) is the body's alone.
// So on Guerrilla's cars the wheel's ray goes through the phantom's collidables as 0xACBDA0 sends it (into each one's
// own space; its shape's ray cast, here the one with a collector, vtable +0x24) and through Guerrilla's rules, and the
// friction gets Guerrilla's slope factor. Armageddon's own cars keep Armageddon's. [GuerrillaVehicles]
// WheelRaysAsGuerrilla=0 turns it off.
// With DriveLog, a line each time a wheel of the player's car loses the ground or finds it again: where (the hardpoint,
// the zone), how fast, what its ray last stood on (the object, its shape type and key, the normal, the suspension), and
// what this cast skipped.
constexpr DWORD kCollideWheels = 0xB075E0, kCastWheel = 0xB073D0;
constexpr DWORD kCollidePhantom = 0x10;   // the wheel collide's AABB phantom
constexpr DWORD kPhantomOverlaps = 0xD0;  // its overlapping collidables: data, the count at +4
constexpr DWORD kBodyParent = 0xC, kBodyMotion = 8, kCollidableOwner = 0x10, kCollidableKind = 0x18;  // hkpCdBody / hkpCollidable
constexpr DWORD kWheelHardpoint = 0x50, kWheelRayEnd = 0x60, kWheelDown = 0x80;  // a wheel's hardpoint, ray end, suspension direction
constexpr DWORD kOutSize = 0x60, kOutBody = 0x24, kOutNormal = 0x10, kOutFriction = 0x20, kOutKeys = 0x28, kOutLength = 0x48;  // a wheel's result
using CollideWheelsFn = void(__fastcall*)(BYTE* collide, void* unused, float dt, BYTE* instance, BYTE* out);
using CastWheelFn = void(__fastcall*)(BYTE* collide, void* unused, const BYTE* wheel, BYTE* out);
CollideWheelsFn g_collideWheels;
CastWheelFn g_castWheel;
auto const ShapeKeysOf = reinterpret_cast<void(__cdecl*)(DWORD* keys, int most, const BYTE* body)>(0xA77D20);  // Havok's, as its closest-hit collector
auto const ObjectOfBody = reinterpret_cast<BYTE*(__cdecl*)(const BYTE* body)>(0x779DB0);                      // a Havok body's game object
auto& g_zoneSize = *reinterpret_cast<const float*>(0x18705A0);

struct WheelSkips { int own, notSolid, steep; float steepUp; };

// Havok 2010's hkpRayHitCollector: slot 0 addRayHit(the body hit, the hit: its normal in world space, fraction, extra
// info), slot 1 the destructor, the early-out fraction at +4. Guerrilla's rules, then Havok's closest hit (0xA77CA0).
class WheelRayCollector {
public:
    virtual void AddRayHit(const BYTE* body, const BYTE* hit);
    virtual ~WheelRayCollector() {}
    float earlyOut = 1.0f;
    const BYTE* chassis = nullptr;  // the car's own collidable
    float down[3] = {0, -1, 0};     // the ray's direction (unit)
    float normal[4] = {};
    DWORD hitInfo[4] = {0x3F800000};  // the fraction (1.0 to begin with), the extra info and two more words, as Havok's output has them
    DWORD keys[8];
    const BYTE* root = nullptr;
    WheelSkips skips = {};
};

void WheelRayCollector::AddRayHit(const BYTE* body, const BYTE* hit) {
    const BYTE* top = body;
    while (At<const BYTE*>(top, kBodyParent)) top = At<const BYTE*>(top, kBodyParent);
    if (chassis && (top == chassis || At<DWORD>(top, kCollidableFilterInfo) == At<DWORD>(chassis, kCollidableFilterInfo))) {
        ++skips.own;
        return;
    }
    if (At<BYTE>(top, kCollidableKind) != 1 || !(top + At<signed char>(top, kCollidableOwner))) {
        ++skips.notSolid;
        return;
    }
    const float* n = reinterpret_cast<const float*>(hit);
    if (down[0] * n[0] + down[1] * n[1] + down[2] * n[2] > -0.5f) {
        if (!skips.steep++) skips.steepUp = n[1];
        return;
    }
    const float fraction = At<float>(hit, 0x10);
    if (!(fraction < *reinterpret_cast<const float*>(&hitInfo[0]))) return;
    memcpy(normal, n, sizeof(normal));
    memcpy(hitInfo, hit + 0x10, sizeof(hitInfo));
    memset(keys, 0xFF, sizeof(keys));
    ShapeKeysOf(keys, 8, body);
    root = top;
    earlyOut = fraction;
}

struct WheelCar { BYTE* instance; BYTE* vehicle; bool guerrilla; bool player; };
thread_local WheelCar t_wheelCar;       // the car whose wheels collideWheels is casting (castSingleWheel runs inside it)
thread_local WheelSkips t_wheelSkips[8];  // the player's car: what each wheel's last cast skipped

WheelCar WhoseWheels(BYTE* instance) {
    WheelCar car = {instance, nullptr, false, false};
    BYTE* chassis = instance ? At<BYTE*>(instance, kChassis) : nullptr;
    BYTE* object = chassis ? ObjectOfBody(chassis) : nullptr;
    BYTE* vehicle = object ? VehicleByHandle(kWorld, nullptr, Handle(object)) : nullptr;
    if (!vehicle || IsArmageddons(vehicle)) return car;
    car.vehicle = vehicle;
    car.guerrilla = true;
    BYTE* player = Player();
    car.player = player && VehicleOf(player) == Handle(vehicle);
    return car;
}

// Guerrilla's cast of one wheel's ray: Armageddon's phantom walk (0xACBDA0), Guerrilla's collector.
void CastAsGuerrilla(BYTE* collide, const BYTE* wheel, BYTE* out, BYTE* instance, WheelSkips* skips) {
    BYTE* phantom = At<BYTE*>(collide, kCollidePhantom);
    BYTE* chassis = At<BYTE*>(instance, kChassis);
    WheelRayCollector ray;
    ray.chassis = chassis ? chassis + 0x10 : nullptr;
    const float* down = reinterpret_cast<const float*>(wheel + kWheelDown);
    const float length = std::sqrt(down[0] * down[0] + down[1] * down[1] + down[2] * down[2]);
    if (length > 0) for (int i = 0; i < 3; ++i) ray.down[i] = down[i] / length;
    BYTE* world = phantom ? At<BYTE*>(phantom, 8) : nullptr;
    BYTE* filter = world ? At<BYTE*>(world, 0x7C) : nullptr;
    alignas(16) float input[12] = {};  // hkpShapeRayCastInput: from, to, the filter info, the shape collection filter
    reinterpret_cast<DWORD*>(input)[8] = At<DWORD>(collide, kCollideFilterInfo);
    reinterpret_cast<BYTE**>(input)[9] = filter ? filter + 0x10 : nullptr;  // Armageddon's cast asks for it (0xB07400)
    const float* from = reinterpret_cast<const float*>(wheel + kWheelHardpoint);
    const float* to = reinterpret_cast<const float*>(wheel + kWheelRayEnd);
    BYTE* const* list = phantom ? At<BYTE* const*>(phantom, kPhantomOverlaps) : nullptr;
    const int count = phantom ? At<int>(phantom, kPhantomOverlaps + 4) : 0;
    using CastFn = void(__fastcall*)(BYTE* shape, void* unused, const float* input, const BYTE* body, WheelRayCollector* collector);
    for (int i = 0; list && i < count; ++i) {
        const BYTE* collidable = list[i];
        BYTE* shape = collidable ? At<BYTE*>(collidable, 0) : nullptr;
        if (!shape) continue;
        const float* t = At<const float*>(collidable, kBodyMotion);  // its transform: three rotation columns, then where
        if (!t) continue;
        for (int end = 0; end < 2; ++end) {
            const float* p = end ? to : from;
            const float d[3] = {p[0] - t[12], p[1] - t[13], p[2] - t[14]};
            float* local = input + end * 4;
            for (int c = 0; c < 3; ++c) local[c] = t[c * 4] * d[0] + t[c * 4 + 1] * d[1] + t[c * 4 + 2] * d[2];
            local[3] = 0;
        }
        (*reinterpret_cast<CastFn**>(shape))[9](shape, nullptr, input, collidable, &ray);
    }
    if (ray.root) {
        memcpy(out, ray.normal, sizeof(ray.normal));
        memcpy(out + 0x10, ray.hitInfo, sizeof(ray.hitInfo));
        memcpy(out + 0x20, ray.keys, sizeof(ray.keys));
        *reinterpret_cast<const BYTE**>(out + 0x50) = ray.root;
    }
    if (skips) *skips = ray.skips;
}

void __fastcall CastWheel(BYTE* collide, void* unused, const BYTE* wheel, BYTE* out) {
    const WheelCar& car = t_wheelCar;
    if (!g_wheelRays || !car.guerrilla || !car.instance || !wheel || !out) {
        g_castWheel(collide, unused, wheel, out);
        return;
    }
    WheelSkips* skips = nullptr;
    if (car.player) {
        const BYTE* first = At<BYTE*>(car.instance, kWheelsInfo);
        const int index = first ? static_cast<int>(wheel - first) / static_cast<int>(kWheelInfoSize) : -1;
        if (index >= 0 && index < 8) skips = &t_wheelSkips[index];
    }
    CastAsGuerrilla(collide, wheel, out, car.instance, skips);
}

// The drop-out lines: the player's car's wheels against what they stood on.
struct WheelGround { bool on; DWORD since; float point[3]; float normal[3]; float length; DWORD object; int type, subtype, shape; DWORD key; };
WheelGround g_ground[8];
DWORD g_groundCar = kNone;
int g_groundLines = 0;
constexpr int kGroundLines = 160;

void NoteWheelGround(const WheelCar& car, BYTE* instance, const BYTE* out, int wheels) {
    if (Handle(car.vehicle) != g_groundCar) {
        g_groundCar = Handle(car.vehicle);
        g_groundLines = 0;
        memset(g_ground, 0, sizeof(g_ground));
    }
    const BYTE* info = At<BYTE*>(instance, kWheelsInfo);
    BYTE* chassis = At<BYTE*>(instance, kChassis);
    if (!info || !chassis) return;
    const float* v = reinterpret_cast<const float*>(chassis + kBodyVelocity);
    const float speed = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    const DWORD now = GetTickCount();
    for (int w = 0; w < wheels; ++w) {
        const BYTE* result = out + w * kOutSize;
        WheelGround& ground = g_ground[w];
        BYTE* body = At<BYTE*>(result, kOutBody);
        if (body) {
            if (!ground.on && ground.since && g_groundLines < kGroundLines) {
                ++g_groundLines;
                rfa::LogDetail("Driving: %s wheel %d back on the ground after %lu ms, %.1f m/s", VehicleName(car.vehicle), w, now - ground.since, speed);
            }
            ground.on = true;
            ground.since = now;
            memcpy(ground.point, result, sizeof(ground.point));
            memcpy(ground.normal, result + kOutNormal, sizeof(ground.normal));
            ground.length = At<float>(result, kOutLength);
            BYTE* object = ObjectOfBody(body);
            ground.object = object ? Handle(object) : kNone;
            ground.type = object ? At<signed char>(object, 0x7A) : -1;
            ground.subtype = object ? At<signed char>(object, 0x7B) : -1;
            const BYTE* shape = At<BYTE*>(body + 0x10, 0);
            ground.shape = shape ? At<int>(shape, 0xC) : -1;
            ground.key = At<DWORD>(result, kOutKeys);
            continue;
        }
        if (!ground.on) continue;
        ground.on = false;
        const DWORD before = now - ground.since;
        ground.since = now;
        if (g_groundLines >= kGroundLines) continue;
        ++g_groundLines;
        const BYTE* one = info + w * kWheelInfoSize;
        const float* at = reinterpret_cast<const float*>(one + kWheelHardpoint);
        const float* end = reinterpret_cast<const float*>(one + kWheelRayEnd);
        const float reach = std::sqrt((end[0] - at[0]) * (end[0] - at[0]) + (end[1] - at[1]) * (end[1] - at[1]) + (end[2] - at[2]) * (end[2] - at[2]));
        const int column = g_zoneSize > 0 ? static_cast<int>(std::floor((8 * g_zoneSize + at[0]) / g_zoneSize)) : -1;
        const int row = g_zoneSize > 0 ? static_cast<int>(std::floor((8 * g_zoneSize - at[2]) / g_zoneSize)) : -1;
        char skipped[96] = "";
        const WheelSkips& s = t_wheelSkips[w];
        if (g_wheelRays && (s.own || s.notSolid || s.steep))
            snprintf(skipped, sizeof(skipped), " (it skipped %d own, %d not solid, %d steep, the first facing up %.2f)", s.own, s.notSolid, s.steep, s.steepUp);
        rfa::LogDetail("Driving: %s wheel %d lost the ground at (%.1f, %.1f, %.1f), zone column %d row %d, %.1f m/s: its ray (%.2f m down) "
                 "found nothing%s; %lu ms before it stood on object %08lX (type %d/%d), shape type %d key %08lX, at (%.1f, %.1f, %.1f), "
                 "normal (%.2f, %.2f, %.2f), suspension %.2f m",
                 VehicleName(car.vehicle), w, at[0], at[1], at[2], column, row, speed, reach, skipped, before, ground.object, ground.type,
                 ground.subtype, ground.shape, ground.key, ground.point[0], ground.point[1], ground.point[2], ground.normal[0],
                 ground.normal[1], ground.normal[2], ground.length);
    }
}

bool g_groundLog = true;  // false once a read of the wheels' results has faulted

void NoteWheelGroundGuarded(const WheelCar& car, BYTE* instance, const BYTE* out, int wheels) {
    __try {
        NoteWheelGround(car, instance, out, wheels);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        g_groundLog = false;
        rfa::Log("Driving: a read of a wheel's ground faulted; the drop-out lines stop");
    }
}

void __fastcall CollideWheels(BYTE* collide, void* unused, float dt, BYTE* instance, BYTE* out) {
    const WheelCar car = WhoseWheels(instance);
    t_wheelCar = car;
    if (car.player) memset(t_wheelSkips, 0, sizeof(t_wheelSkips));
    g_collideWheels(collide, unused, dt, instance, out);
    t_wheelCar = WheelCar{};
    if (!car.guerrilla || !out) return;
    const BYTE* data = At<BYTE*>(instance, kVehicleData);
    const int wheels = data ? std::min<int>(At<signed char>(data, 0x20), 8) : 0;
    if (g_wheelRays)
        for (int w = 0; w < wheels; ++w) {
            BYTE* result = out + w * kOutSize;
            if (!At<BYTE*>(result, kOutBody)) continue;
            const float up = At<float>(result, kOutNormal + 4);
            *reinterpret_cast<float*>(result + kOutFriction) *= 0.25f + 0.75f * (up > 0 ? up : 0.0f);
        }
    if (car.player && g_driveLog && g_groundLog) NoteWheelGroundGuarded(car, instance, out, wheels);
}

void StartWheelRays() {
    g_wheelRays = rfa::IniInt(L"GuerrillaVehicles", L"WheelRaysAsGuerrilla", 1) != 0;
    if (!g_wheelRays && !g_driveLog) return;
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};  // push ebp / mov ebp,esp / and esp,0FFFFFFF0h
    if (memcmp(reinterpret_cast<const void*>(kCollideWheels), opening, sizeof(opening)) != 0 ||
        memcmp(reinterpret_cast<const void*>(kCastWheel), opening, sizeof(opening)) != 0) {
        g_wheelRays = false;
        rfa::Log("Driving: Havok's wheel rays (0x%08lX, 0x%08lX) aren't as expected; Guerrilla's cars cast them as Armageddon's", kCollideWheels, kCastWheel);
        return;
    }
    g_collideWheels = reinterpret_cast<CollideWheelsFn>(rfa::HookFunction(kCollideWheels, sizeof(opening), reinterpret_cast<void*>(&CollideWheels)));
    if (!g_collideWheels) {
        g_wheelRays = false;
        return;
    }
    if (g_wheelRays) g_castWheel = reinterpret_cast<CastWheelFn>(rfa::HookFunction(kCastWheel, sizeof(opening), reinterpret_cast<void*>(&CastWheel)));
    if (!g_castWheel) g_wheelRays = false;
    rfa::Log(g_wheelRays ? "Driving: Guerrilla's cars cast their wheels' rays by Guerrilla's rules (their own parts, non-solid things and surfaces "
                           "steeper than 60 degrees skipped) and grip less on slopes, as Guerrilla's"
                         : "Driving: Guerrilla's cars cast their wheels' rays as Armageddon's ([GuerrillaVehicles] WheelRaysAsGuerrilla=0)");
}

// ---- The log: a vehicle's gun getting and losing its gunner
void __fastcall AttachGunner(BYTE* turret, void* unused, BYTE* human) {
    g_attachGunner(turret, unused, human);
    BYTE* vehicle = turret ? TurretVehicle(turret, nullptr) : nullptr;
    if (!vehicle || !human) return;
    const bool player = IsPlayer(human);
    const int seat = SeatOf(vehicle, human);
    if (g_watchLog && player && AimsByItself(turret, nullptr)) {  // the player drives with it: watch it for a minute
        g_watched = Handle(turret);
        g_watchLines = g_nextWatch = g_lastWatch = 0;
    }
    const char* aims = !AimsByItself(turret, nullptr) ? "the gunner aims it"
                       : IsArmageddons(vehicle)       ? "it follows the player's aim point (Armageddon's rule for its own vehicles)"
                       : player && AimUpgradeBought() ? "it follows the player's aim point (TURRET LINK is bought)"
                       : VehicleSubtype(vehicle, nullptr) == kWalker ? "it follows the player's aim point while it can reach it, else it aims by itself (Guerrilla's walkers)"
                                                                     : "it aims by itself: enemies ahead, else straight ahead (Guerrilla's rule for a driver)";
    // The player's lines always (a test's evidence), NPCs' only a few: traffic's gunners come and go all the time.
    if (player)
        RFA_LOG_DETAIL_FIRST(200, "Turrets: %s on %s has a gunner: the player in seat %d (0 the driver's); %s", TurretName(turret), VehicleName(vehicle), seat, aims);
    else
        RFA_LOG_DETAIL_FIRST(20, "Turrets: %s on %s has a gunner: an NPC in seat %d; %s", TurretName(turret), VehicleName(vehicle), seat, aims);
}

void __fastcall DetachGunner(BYTE* turret, void* unused, BYTE* human) {
    BYTE* vehicle = turret ? TurretVehicle(turret, nullptr) : nullptr;
    BYTE* gunner = turret ? HumanByHandle(kWorld, nullptr, *reinterpret_cast<const DWORD*>(turret + kGunner)) : nullptr;
    const bool player = gunner && IsPlayer(gunner);
    if (turret && Handle(turret) == g_watched) g_watched = kNone;
    g_detachGunner(turret, unused, human);
    if (!vehicle || !gunner) return;
    if (player)
        RFA_LOG_DETAIL_FIRST(200, "Turrets: %s on %s lost its gunner (the player); it goes back to its middle for 10 s, then holds where it is", TurretName(turret),
                      VehicleName(vehicle));
    else
        RFA_LOG_DETAIL_FIRST(20, "Turrets: %s on %s lost its gunner (an NPC)", TurretName(turret), VehicleName(vehicle));
}

// ---- The hinges' motors of a gun on one of Guerrilla's vehicles: Guerrilla's
// A turret's ring and gun each turn on a hinge whose motor pulls it to the angle the game asks for (Havok's
// position motor; the game makes both in assemble_components, 0x83AFA0, with build_pos_motor, 0x83B8A0).
// Both games give the motor the same strength (the entry's Horsepower × 745.7), stiffness (1) and damping
// (0.75), but not the same speed for making up an error: Guerrilla's 0.1 × the error and nothing constant,
// Armageddon's 1.0 × the error plus a constant 2. Armageddon's own turrets are written for that with 100 to
// 10000 horsepower; Guerrilla's ring guns have 2, and at Armageddon's speeds such a motor can't stop the gun
// where it's sent: the gun swings past, back and past again (the user's "sways left and right": 5 degrees
// either way parked, 25 to 100 once the car moves). So Guerrilla's vehicles' guns get Guerrilla's two numbers.
constexpr DWORD kAssemble = 0x83AFA0;                               // assemble_components(physics, turret, vehicle)
constexpr size_t kAssemblePrologue = 9;                             // push ebp / mov ebp,esp / sub esp,0A0h
constexpr DWORD kRingMotor = 0x50, kGunMotor = 0x78;                // a turret's physics: its two hinges' position motors
constexpr DWORD kRecoveryByError = 0x1C, kRecoveryConstant = 0x20;  // a position motor: how fast it makes up an error
constexpr float kGuerrillasRecoveryByError = 0.1f, kGuerrillasRecoveryConstant = 0.0f;
using AssembleFn = bool(__cdecl*)(BYTE* physics, BYTE* turret, BYTE* vehicle);
AssembleFn g_assemble;

// ---- What a part of a gun on one of Guerrilla's vehicles weighs: Guerrilla's
// A turret part made from an item (a top, or a middle or base that isn't one of the vehicle's own components)
// gets a body of its own. On a vehicle Guerrilla weighs that body as the turret's entry says (the part's Mass,
// at least 5 kg), whatever the item weighs by itself (create_item_component). Armageddon weighs it as the item
// (0x83CBC0, which fills in what the body is made from: an item's own mass comes first, the entry's only
// without an item). Guerrilla's ring guns name no weight for their top, so there the gun is 5 kg; with
// Armageddon's rule it was the item's 100 kg, hung half a metre off the ring's axis on a 2-horsepower hinge:
// in a hard turn or over a bump it swung the ring round or dropped its nose, and stayed there.
constexpr DWORD kPartBody = 0x83CBC0;
constexpr size_t kPartBodyPrologue = 6;  // push ebx / mov ebx,esp / sub esp,8
constexpr DWORD kBodyMass = 0x6C;        // in what a body is made from: its mass
constexpr float kLightestPart = 5.0f;
// The place and the nine numbers of the orientation go through as they came.
using PartBodyFn = void(__cdecl*)(BYTE* body, BYTE* item, void* mesh, DWORD x, DWORD y, DWORD z, DWORD r0, DWORD r1, DWORD r2, DWORD r3, DWORD r4, DWORD r5,
                                  DWORD r6, DWORD r7, DWORD r8, DWORD other, int onVehicle, float entryMass);
PartBodyFn g_partBody;
BYTE* g_assembling;  // the turret of one of Guerrilla's vehicles the game is putting together now

void __cdecl PartBody(BYTE* body, BYTE* item, void* mesh, DWORD x, DWORD y, DWORD z, DWORD r0, DWORD r1, DWORD r2, DWORD r3, DWORD r4, DWORD r5, DWORD r6,
                      DWORD r7, DWORD r8, DWORD other, int onVehicle, float entryMass) {
    g_partBody(body, item, mesh, x, y, z, r0, r1, r2, r3, r4, r5, r6, r7, r8, other, onVehicle, entryMass);
    if (!g_assembling || !body || !item || !onVehicle) return;
    float& mass = *reinterpret_cast<float*>(body + kBodyMass);
    const float items = mass, guerrillas = entryMass > kLightestPart ? entryMass : kLightestPart;
    mass = guerrillas;
    RFA_LOG_DETAIL_FIRST(40, "Turrets: %s: its part %s weighs %g kg as Guerrilla's (its entry's %g, at least %g), not the item's own %g", TurretName(g_assembling),
                  Name(item), guerrillas, entryMass, kLightestPart, items);
}

bool __cdecl Assemble(BYTE* physics, BYTE* turret, BYTE* vehicle) {
    const char* on = vehicle ? VehicleName(vehicle) : "-";
    const bool guerrillas = turret && on[0] != '-' && !IsArmageddons(vehicle);
    g_assembling = guerrillas ? turret : nullptr;
    const bool made = g_assemble(physics, turret, vehicle);
    g_assembling = nullptr;
    if (!made || !physics || !guerrillas) return made;
    float was[2] = {-1.0f, -1.0f};
    int set = 0;
    for (DWORD part : {kRingMotor, kGunMotor}) {
        BYTE* motor = *reinterpret_cast<BYTE* const*>(physics + part);
        if (!motor) continue;
        if (!set) {
            was[0] = *reinterpret_cast<const float*>(motor + kRecoveryByError);
            was[1] = *reinterpret_cast<const float*>(motor + kRecoveryConstant);
        }
        *reinterpret_cast<float*>(motor + kRecoveryByError) = kGuerrillasRecoveryByError;
        *reinterpret_cast<float*>(motor + kRecoveryConstant) = kGuerrillasRecoveryConstant;
        ++set;
    }
    RFA_LOG_DETAIL_FIRST(30, "Turrets: %s on %s: %d hinge motors make up an error as Guerrilla's (%.2g of it a second and %.2g; the game's were %.2g and %.2g)",
                  TurretName(turret), on, set, kGuerrillasRecoveryByError, kGuerrillasRecoveryConstant, was[0], was[1]);
    return made;
}

bool Opens(DWORD address, const BYTE* opening, size_t size) { return memcmp(reinterpret_cast<const void*>(address), opening, size) == 0; }

void StartHinges() {
    if (!rfa::IniInt(L"GuerrillaVehicles", L"TurretHingesAsGuerrilla", 1)) {
        rfa::Log("Turrets: the guns' hinge motors on Guerrilla's vehicles stay Armageddon's ([GuerrillaVehicles] TurretHingesAsGuerrilla=0)");
        return;
    }
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00};
    const BYTE partOpening[] = {0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08, 0x83, 0xE4, 0xF0};
    if (Opens(kAssemble, opening, sizeof(opening)))
        g_assemble = reinterpret_cast<AssembleFn>(rfa::HookFunction(kAssemble, kAssemblePrologue, reinterpret_cast<void*>(&Assemble)));
    rfa::Log(g_assemble ? "Turrets: the guns on Guerrilla's vehicles get Guerrilla's hinge motors (they hold the gun where it's sent)"
                        : "Turrets: the game's turret assembly isn't as expected; the guns' hinge motors and parts' weights stay Armageddon's");
    if (!g_assemble) return;
    if (Opens(kPartBody, partOpening, sizeof(partOpening)))
        g_partBody = reinterpret_cast<PartBodyFn>(rfa::HookFunction(kPartBody, kPartBodyPrologue, reinterpret_cast<void*>(&PartBody)));
    rfa::Log(g_partBody ? "Turrets: the guns' parts on Guerrilla's vehicles weigh what Guerrilla gives them (the turret's entry's mass, at least 5 kg)"
                        : "Turrets: the game's turret part bodies aren't as expected; the parts weigh what their items do, as Armageddon's");
}

// ---- The gunner's camera: Guerrilla's follows the gun
//
// Guerrilla (camera_turret_process RM 0x6DEA50): the mouse turns the gun itself (camera_turret_set_rotation →
// turret::set_turret_velocity, at the turret's own speeds and within its limits), camera_aim_turret puts the aim
// point along the muzzle, and turret_update_camera_post (RM 0x6DE030) places the camera from the gun's muzzle
// with the turret entry's offset (Default_Offset, Fine_Aim_Offset), looking where the gun points: the view never
// leaves the gun. Armageddon (its turret camera mode's frame 0x4BD350: camera_aim_turret 0x4BE320, the player's
// camera controls 0x8C1D70, then 0x4BD510) keeps the offsets but turns it round: the mouse turns the camera's
// look direction (0x11B9CA8), and while the gun runs on velocity motors (0x8C6A10) 0x4BD510 sets their speeds
// to close the gap between that look and the gun (0x83F6E0) in one frame. The camera can then look where the
// gun can't go (below its lower limit, or while it lags behind the mouse), and the view comes loose from the
// gun (the user's screenshot: the camera at Mason's head looking down at the ground, the gun pointing up).
//
// So on Guerrilla's vehicles, after 0x4BD510 has set this frame's speeds, the look direction is kept within
// kGunLead of where the gun points (as 0x4BE120 measures it: from the camera to the gun's aim point, asked
// without its smoothing, which it only moves when told). The gap still drives the gun, so the mouse aims as
// before, but the view stays on the gun, at its limits too, as Guerrilla's.
constexpr DWORD kTurretCameraPost = 0x4BD510;   // the turret camera's placing (Guerrilla's turret_update_camera_post)
constexpr size_t kTurretCameraPostPrologue = 9; // push ebp / mov ebp,esp / sub esp,1ACh
constexpr DWORD kCameraLook = 0x11B9CA8;        // the camera's look direction, which the mouse turns
constexpr DWORD kCameraMatrix = 0x11B9CB4;      // the camera's orientation (right, up, forward)
constexpr float kGunLead = 2.0f * 3.14159265f / 180.0f;  // how far the view may lead the gun (radians)
auto const MannedTurret = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human)>(0x881ED0);           // human_combat_get_manned_turret
auto const GunSeenFromCamera = reinterpret_cast<bool(__cdecl*)(V3* out, BYTE* turret, bool smooth)>(0x4BE120);
auto const GunOnVelocityMotors = reinterpret_cast<bool(__cdecl*)()>(0x8C6A10);
auto const MatrixFromForward = reinterpret_cast<void(__fastcall*)(void* matrix, void* unused, const V3* forward)>(0xC9DA10);
using CameraPostFn = void(__cdecl*)();
CameraPostFn g_turretCameraPost;

float Length(const V3& v) { return std::sqrt(Dot(v, v)); }

struct CameraWatch {
    int next = 0;    // when to log next (the game's clock)
    int lines = 0;
    float most = 0;  // the widest gap since the last line (degrees)
    int held = 0;    // frames held to the gun since the last line
    int frames = 0;
} g_cameraWatch;

void __cdecl TurretCameraPost() {
    g_turretCameraPost();
    BYTE* player = Player();
    BYTE* turret = player ? MannedTurret(player) : nullptr;
    BYTE* vehicle = turret ? TurretVehicle(turret, nullptr) : nullptr;
    if (!vehicle || IsArmageddons(vehicle) || !GunOnVelocityMotors()) return;
    V3 gun{};
    if (!GunSeenFromCamera(&gun, turret, false)) return;
    V3& look = *reinterpret_cast<V3*>(kCameraLook);
    const float lookLength = Length(look), gunLength = Length(gun);
    if (lookLength < 1e-4f || gunLength < 1e-4f) return;
    const V3 l{look.x / lookLength, look.y / lookLength, look.z / lookLength}, g{gun.x / gunLength, gun.y / gunLength, gun.z / gunLength};
    float cosine = Dot(l, g);
    cosine = cosine > 1 ? 1 : (cosine < -1 ? -1 : cosine);
    const float gap = std::acos(cosine);
    CameraWatch& w = g_cameraWatch;
    ++w.frames;
    if (gap * 57.29578f > w.most) w.most = gap * 57.29578f;
    if (gap > kGunLead) {
        // The look turned from the gun's way towards its own by kGunLead (on the great circle between them).
        const V3 across{l.x - g.x * cosine, l.y - g.y * cosine, l.z - g.z * cosine};
        const float acrossLength = Length(across);
        V3 held = g;
        if (acrossLength > 1e-5f) {
            const float c = std::cos(kGunLead), s = std::sin(kGunLead);
            held = {g.x * c + across.x / acrossLength * s, g.y * c + across.y / acrossLength * s, g.z * c + across.z / acrossLength * s};
        }
        look = {held.x * lookLength, held.y * lookLength, held.z * lookLength};
        MatrixFromForward(reinterpret_cast<void*>(kCameraMatrix), nullptr, &held);
        ++w.held;
    }
    const int now = GameTime();
    if (now >= w.next && w.lines < 120) {
        if (w.next) {
            rfa::LogDetail("Turrets: the gunner's camera on %s: the view led the gun by up to %.1f degrees in the last second; held to the gun in %d of %d frames",
                     VehicleName(vehicle), w.most, w.held, w.frames);
            ++w.lines;
        }
        w.next = now + 1000;
        w.most = 0;
        w.held = w.frames = 0;
    }
}

void StartGunnerCamera() {
    if (!rfa::IniInt(L"GuerrillaVehicles", L"GunnerCameraAsGuerrilla", 1)) {
        rfa::Log("Turrets: the gunner's camera looks where the mouse turns it, as Armageddon's ([GuerrillaVehicles] GunnerCameraAsGuerrilla=0)");
        return;
    }
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xAC, 0x01, 0x00, 0x00};
    if (Opens(kTurretCameraPost, opening, sizeof(opening)))
        g_turretCameraPost = reinterpret_cast<CameraPostFn>(rfa::HookFunction(kTurretCameraPost, kTurretCameraPostPrologue, reinterpret_cast<void*>(&TurretCameraPost)));
    rfa::Log(g_turretCameraPost ? "Turrets: on Guerrilla's vehicles the gunner's camera stays on the gun, as Guerrilla's (it leads it by at most 2 degrees)"
                                : "Turrets: the game's turret camera isn't as expected; the gunner's camera stays Armageddon's");
}

}  // namespace

void StartTurrets() {
    StartHinges();
    StartGunnerCamera();
    StartWalkerLog();
    StartDriveLog();
    StartFrictionAsGuerrilla();
    StartWheelRays();
    g_watchLog = rfa::ReadLogSwitch(L"GuerrillaVehicles", L"TurretWatchLog");
    if (!rfa::IniInt(L"GuerrillaVehicles", L"DriverGunAsGuerrilla", 1)) {
        rfa::Log("Turrets: a driver's gun follows the player's aim point on every vehicle, as Armageddon's ([GuerrillaVehicles] DriverGunAsGuerrilla=0)");
        return;
    }
    const BYTE autoaimOpening[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x30, 0x01, 0x00, 0x00};
    const BYTE attachOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x58};
    const BYTE detachOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x50};
    if (!Opens(kAutoaim, autoaimOpening, sizeof(autoaimOpening))) {
        rfa::Log("Turrets: the game's turret aiming isn't as expected; a driver's gun keeps Armageddon's rule");
        return;
    }
    g_autoaim = reinterpret_cast<AutoaimFn>(rfa::HookFunction(kAutoaim, kAutoaimPrologue, reinterpret_cast<void*>(&Autoaim)));
    if (!g_autoaim) {
        rfa::Log("Turrets: could not hook the turret aiming; a driver's gun keeps Armageddon's rule");
        return;
    }
    rfa::Log("Turrets: on Guerrilla's vehicles a driver's gun aims by itself as Guerrilla's (enemies ahead, else straight ahead; a walker's follows the aim point), "
             "until the player buys TURRET LINK (RFA-Mod's own upgrade): then it follows his aim point");
    if (Opens(kAttachGunner, attachOpening, sizeof(attachOpening)) && Opens(kDetachGunner, detachOpening, sizeof(detachOpening))) {
        g_attachGunner = reinterpret_cast<GunnerFn>(rfa::HookFunction(kAttachGunner, kGunnerPrologue, reinterpret_cast<void*>(&AttachGunner)));
        if (g_attachGunner) g_detachGunner = reinterpret_cast<GunnerFn>(rfa::HookFunction(kDetachGunner, kGunnerPrologue, reinterpret_cast<void*>(&DetachGunner)));
    }
    if (!g_attachGunner || !g_detachGunner) rfa::Log("Turrets: gunners coming and going aren't logged");
}

// Around the game's AI/driver handling reset (guerrilla_vehicles.cpp's hook on 0x5F2CF0): a Guerrilla car whose tyre
// friction was built when it was made keeps it (see "A car's tyre friction" above).
void BeginHandlingReset(BYTE* car) {
    g_frictionKept = nullptr;
    if (!g_buildFriction || !car || IsArmageddons(car)) return;
    BYTE* block = ActiveHandlingBlock(car + kHandlingBlockOffset, nullptr);
    BYTE* data = block ? *reinterpret_cast<BYTE**>(block) : nullptr;
    BYTE* description = data ? data + kFrictionDescription : nullptr;
    if (description && std::find(g_frictionBuilt.begin(), g_frictionBuilt.end(), description) != g_frictionBuilt.end()) g_frictionKept = description;
}
void EndHandlingReset() { g_frictionKept = nullptr; }

}  // namespace gv
