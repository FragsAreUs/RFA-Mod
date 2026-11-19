// People who sit in a vehicle take bullets as in Guerrilla (a turret's gunner above all), the player changes seats
// as in Guerrilla (the driver's seat and the gun's), and a log of what happens when the player shoots riders and when
// he tries to get into a vehicle someone sits in. A part of guerrilla_vehicles; [GuerrillaVehicles]
// RiderDamageAsGuerrilla=1, ChangeSeatsAsGuerrilla=1 (0: Armageddon's), RiderLog=1 (0 off).
//
// Riders' damage. The user's tests (2026-11-12, EDF_Scout_1/2's gunners): the bullets reach the gunner (ray, hit
// boxes and human::process_bullet_hit all as Guerrilla's), the hit works out to 0 and his hit points don't move.
// Armageddon's human damage function (0x85E0C0, Guerrilla's human_damage RM 0xACED70; the hit reaches it through
// 0x87BF50) returns 0 at once for anyone in a vehicle (+0x1160 set) unless the damage is -1 (0x85E18D: Armageddon's
// "kill him", which it gives the riders of a vehicle that blows up). Guerrilla's human_damage has no such rule: only
// someone in the player's vehicle is spared (human_in_players_vehicle; Armageddon's 0x866D10, kept). So for a bullet
// (kind 3) that hits an NPC who sits in a vehicle, his vehicle's handle is set aside for that one test only: it is
// put back at the next call the function makes (0x4657A0, from 0x85E378, returning to 0x85E37D), before anything else
// reads it, so the rest (the vehicle's humans_invulnerable, someone in the player's vehicle, his death in the seat)
// sees him seated as before. The player in a vehicle stays untouched (process_bullet_hit returns first for him, in
// both games). A killed gunner falls as in Guerrilla: Armageddon kept Guerrilla's ragdoll push and its tweaks
// (Vi_impulse_ragdoll 200, _turret 300, _vert_damp 0.7, _vert_damp_turret 0.4, _guerilla_bonus 1.1; read at
// 0x65D2B1/0x65D2CF).
//
// Changing seats. Guerrilla's control_gameplay_process (RM 0x700C50) calls player_switch_seat (RM 0xA34200) when
// CBA_VDC_CHANGE_SEAT (the middle mouse button) is pressed in a vehicle: from any seat but the driver's to the
// driver's, else to the seat of the vehicle's first turret mount (vehicle_get_turret_mount 0), through
// vehicle_interaction_request_enter with that seat (vi_enter_data: specified_seat, method SPECIFIED, destination -1).
// Armageddon kept the control (90, veh_change_seat; ChangeSeat= gives it the key) and the parts (0x604240 the mount,
// 0x656410 the request) but nothing reads the control, so here the same is done when it is pressed.
//
// What the code of both games says, read side by side:
//
// Shooting. A bullet (Armageddon's fire_bullet 0x721400 → weapons_test_bullet 0x735430; Guerrilla's
// weapon::fire_bullet RM 0xBEA0C0 → weapons_test_bullet RM 0xBE7E90) casts one physics ray. Its filter (0x736120;
// Guerrilla's process_bullet_callback RM 0xBE4F40) lets no person stop the ray: it collects each person whose body
// the ray touches, and after the ray the bullet tests their hit boxes (0x869870; Guerrilla's human_get_hit_location)
// against whatever solid thing the ray met. Guerrilla's filter skips only a dead person with a ragdoll and someone
// in the shooter's own vehicle; Armageddon's skips also a hidden person (+0xB0 bit 2, Guerrilla's hflags.hidden one
// bit on) and a spectator (state 11). A seated person's body takes the vehicle's collision filter (Armageddon's
// get_default_collision_filter 0x8645A0, states 8 sitting and 9 driving; Guerrilla's RM 0xA52AF0), so rays meet
// it; only someone in the player's vehicle, or in a protected seat of an EDF vehicle (Guerrilla: the vehicle's
// default team; Armageddon: a flag of its vehicle entry, +0x875 bit 0, that its table reader never sets), gets the
// minimum filter. The hit (human::process_bullet_hit 0x87D8E0; RM 0xAD37B0) spares only the player in a vehicle.
// The damage (0x84CF00) is dropped for a person whose vehicle has Armageddon's humans_invulnerable (+0x875 bit 2:
// the entry's own field, or RF_LAV_1/2 and Cru_Tank_1/2); Guerrilla's vehicles don't have it.
//
// Getting in. player_use_vehicle (0x8B9680; RM 0xA9E160) asks vehicle_is_usable (0x606F60; RM 0xB8C160), then
// makes the driver leave (a "hijacked" reaction, the vehicle's hijacked flag, +0x4FB0 bit 19) and asks
// vehicle_interaction_request_enter (0x656410), which tells the riders in the player's way to get out. A rider
// makes the vehicle unusable if he is the player's enemy (unless tied up, +0x1554 bit 5), or an NPC whose orders
// aren't none or ambient (Armageddon's 0, 8, 9; Guerrilla's 0, 7 ambient action, 8 ambient ped) and who isn't an
// activity's driver (+0x1554 bit 0). Both games decide alike.
//
// The log, in Guerrilla's world (TrafficWorlds=), single player:
//   - each time the use prompt weighs a vehicle with riders for the player (once every 4 s a vehicle): whether it
//     offers it, each rider, his team, orders and flags, and Guerrilla's verdict on him; or, if no rider blocks, the
//     vehicle's flags;
//   - each time the player asks to get into a vehicle with riders: the answer, and for 8 s what the riders do;
//   - each change of seat: from which seat to which, and the game's answer;
//   - each of the player's bullets that passes a seated NPC (within 1 m of the ray): what the ray met, whether it
//     touched the rider's body and his hit boxes, his collision filter, and whether damage reached him: the damage
//     the game worked out (0x87BF50), his hit points before and after, whether the in-a-vehicle rule was set aside,
//     the vehicle's humans_invulnerable and his own invulnerability.
#define RFA_MOD_NAME "guerrilla_vehicles"
#include "rfa_mod.h"
#include "game.h"
#include "campaign/campaign_api.h"
#include <intrin.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace gv {

namespace {

// ---- The game's functions and fields (Armageddon's, publicbeta)
constexpr DWORD kUsable = 0x606F60;          // vehicle_is_usable(user, enter data, vehicle, bool, bool), cdecl
constexpr DWORD kRequestEnter = 0x656410;    // vehicle_interaction_request_enter(human, vehicle, enter data), cdecl
constexpr DWORD kTestBullet = 0x735430;      // weapons_test_bullet(fire info, shooter, bool, bool, bool), cdecl
constexpr DWORD kBulletFilter = 0x736120;    // the bullet ray's filter(object, its data): false lets the ray go on, cdecl
constexpr DWORD kHitLocation = 0x869870;     // human_get_hit_location(..., human, ...): -1 none, cdecl, 9 arguments
constexpr DWORD kBulletHit = 0x87D8E0;       // human::process_bullet_hit(attacker, damage, fire info), thiscall
constexpr DWORD kDamageCalc = 0x87BF50;      // the damage a hit does (damage record), cdecl, a float
constexpr DWORD kHumanDamage = 0x85E0C0;     // human damage (damage record), cdecl, a float: the damage done
constexpr DWORD kCopyHitInfo = 0x4657A0;     // copies a record (thiscall, one pointer): the human damage's first call after
constexpr DWORD kAfterRiderRule = 0x85E37D;  // its in-a-vehicle rule, from 0x85E378 (where that call returns to)
// A damage record (0x42E960 makes it): +0 the victim, +4 the attacker, +8 the damage, +0xC its kind (3 a bullet).
constexpr DWORD kVictim = 0x00, kDamage = 0x08, kKind = 0x0C;
constexpr int kBulletDamage = 3;
constexpr float kKill = -1.0f;               // the damage that kills outright
constexpr DWORD kHumansInvulnerable = 0x875; // a vehicle's entry: bit 2, Armageddon's humans_invulnerable
constexpr DWORD kInVehicleField = 0x1160;    // a human: the vehicle he is in (a handle; -1 none)
// Changing seats.
constexpr int kChangeSeatControl = 90;       // veh_change_seat
constexpr DWORD kControlSet = 0x11C0580;     // the player's control set (-1 for the first)
constexpr int kDriverSeat = 0, kExiting = 10;
constexpr DWORD kViInstance = 0x4138;        // a vehicle: its vehicle interaction; +0xAC its seat records
constexpr DWORD kSeatRecords = 0xAC, kDestinationSeat = 0x5C;  // a seat record: +0x54 its enter data, +0x5C the seat it went to
// fire_bullet's two calls of the bullet test (where each returns to): only these are shots; the game's other calls
// (the reticle, the AI's line of fire) aren't.
constexpr DWORD kShotReturns[] = {0x72168D, 0x721703};

constexpr DWORD kFlags = 0x1554;        // a human: +0x1554 bit 0 an activity's driver, bit 5 tied up
constexpr DWORD kHumanFlags = 0xB0;     // bit 2 hidden
constexpr DWORD kState = 0x720;         // 7 entering a vehicle, 8 sitting, 9 driving, 10 getting out
constexpr DWORD kHitPoints = 0xF24;     // a float
constexpr DWORD kObjectFlags = 0x54;    // an object: +0x54 bit 6 (vehicle_is_usable refuses it)
constexpr DWORD kVehicleFlags2 = 0x4FB4;
constexpr DWORD kHijacked = 1u << 19;   // +0x4FB0
constexpr DWORD kVehicleInfo = 0x3DA8;  // a vehicle: its vehicles.xtbl entry, the name first
constexpr DWORD kMinimumFilter = 0x1975E68;  // the collision filter a person in the player's vehicle or a protected seat gets
// fire info: +0x00 the ray's start, +0x18 its end, +0x24 what it met (a handle), +0x28 where.
constexpr DWORD kRayStart = 0x00, kRayEnd = 0x18, kRayHit = 0x24, kRayHitAt = 0x28;

auto const IsPlayer = reinterpret_cast<bool(__cdecl*)(BYTE* human)>(0x7B2600);
auto const IsDead = reinterpret_cast<bool(__cdecl*)(BYTE* human)>(0x42ADC0);
auto const IsNpc = reinterpret_cast<bool(__fastcall*)(BYTE* human, void* unused)>(0x461130);
auto const Unfriendly = reinterpret_cast<bool(__cdecl*)(BYTE* human, BYTE* other)>(0x42ACF0);  // as vehicle_is_usable asks it
auto const Team = reinterpret_cast<int(__fastcall*)(BYTE* human, void* unused)>(0x862B60);
auto const NpcAi = reinterpret_cast<BYTE*(__fastcall*)(BYTE* npc, void* unused)>(0x426250);
auto const OrdersType = reinterpret_cast<int(__fastcall*)(BYTE* ai, void* unused)>(0x4309B0);
auto const HumanCast = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, BYTE* object)>(0x4362C0);  // null if not a person
auto const HumanCount = reinterpret_cast<int(__fastcall*)(void* world, void* unused)>(0x43A860);
auto const HumanAt = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int index)>(0x43A8B0);
auto const HeadPosition = reinterpret_cast<void(__cdecl*)(BYTE* human, V3* out)>(0x868A00);
auto const SeatOf = reinterpret_cast<int(__cdecl*)(BYTE* vehicle, BYTE* human)>(0x603F90);
auto const InProtectedSeat = reinterpret_cast<bool(__cdecl*)(BYTE* human)>(0x65EB70);
auto const DefaultFilter = reinterpret_cast<DWORD(__cdecl*)(BYTE* human)>(0x8645A0);     // get_default_collision_filter
auto const VehicleFilter = reinterpret_cast<DWORD(__cdecl*)(BYTE* vehicle)>(0x60EA80);   // vehicle_get_collision_filter
auto const Invulnerable = reinterpret_cast<bool(__fastcall*)(BYTE* human, void* unused)>(0x84DB40);  // his own flags
auto const JustPressed = reinterpret_cast<bool(__cdecl*)(int control, int set, int* device)>(0x4E50F0);
auto const InVehicleInteraction = reinterpret_cast<bool(__cdecl*)(BYTE* human, BYTE* vehicle)>(0x8669B0);
auto const TurretMount = reinterpret_cast<BYTE*(__cdecl*)(BYTE* vehicle, int index)>(0x604240);  // vehicle_get_turret_mount
auto const FindRider = reinterpret_cast<int(__fastcall*)(BYTE* interaction, void* unused, BYTE* human)>(0x64BC70);  // his seat record, -1
auto const SeatRecord = reinterpret_cast<BYTE*(__fastcall*)(BYTE* records, void* unused, int index)>(0x435E60);
auto const MakeEnterData = reinterpret_cast<void(__fastcall*)(BYTE* enter, void* unused)>(0x4B13F0);  // all -1, no flags

using UsableFn = bool(__cdecl*)(BYTE* user, void* enter, BYTE* vehicle, bool a, bool b);
using RequestEnterFn = bool(__cdecl*)(BYTE* human, BYTE* vehicle, int* enter);
using TestBulletFn = bool(__cdecl*)(BYTE* info, BYTE* shooter, bool a, bool b, bool c);
using BulletFilterFn = bool(__cdecl*)(BYTE* object, BYTE* data);
using HitLocationFn = int(__cdecl*)(void* a0, BYTE* human, void* a2, void* a3, void* a4, float a5, void* a6, void* a7, void* a8);
using BulletHitFn = void(__fastcall*)(BYTE* victim, void* unused, BYTE* attacker, float damage, BYTE* info);
using DamageCalcFn = float(__cdecl*)(BYTE* damage);
using HumanDamageFn = float(__cdecl*)(BYTE* damage);
using CopyHitInfoFn = void*(__fastcall*)(void* self, void* unused, const void* source);
UsableFn g_usable;
RequestEnterFn g_requestEnter;
TestBulletFn g_testBullet;
BulletFilterFn g_bulletFilter;
HitLocationFn g_hitLocation;
BulletHitFn g_bulletHit;
DamageCalcFn g_damageCalc;
HumanDamageFn g_humanDamage;
CopyHitInfoFn g_copyHitInfo;
bool g_asGuerrilla;    // RiderDamageAsGuerrilla
bool g_changeSeats;    // ChangeSeatsAsGuerrilla

bool Multiplayer() { return *reinterpret_cast<const BYTE*>(0x2685118) != 0; }
bool Watching() { return !Multiplayer() && InTrafficWorld(); }

template <typename T>
T Field(const BYTE* object, DWORD offset) { return *reinterpret_cast<const T*>(object + offset); }

const char* VehicleName(const BYTE* vehicle) {
    const BYTE* info = vehicle ? Field<const BYTE*>(vehicle, kVehicleInfo) : nullptr;
    const char* name = info ? *reinterpret_cast<const char* const*>(info) : nullptr;
    return name && *name ? name : "-";
}

const char* StateName(int state) {
    switch (state) {
    case 0: return "standing";
    case 3: return "on a turret";
    case 4: return "dead";
    case 7: return "getting in";
    case 8: return "sitting";
    case 9: return "driving";
    case 10: return "getting out";
    default: return "another state";
    }
}

// ---- Getting in: who is in the player's way, and what each game says of him
struct Verdict {
    bool enemy = false, tied = false, npc = false, activityDriver = false, ordersAllowed = true;
    int team = -1, orders = -1;
    DWORD flags = 0;
    bool blocks = false;  // Armageddon's verdict, which is Guerrilla's
};

Verdict Judge(BYTE* rider, BYTE* user) {
    Verdict v;
    v.flags = Field<DWORD>(rider, kFlags);
    v.team = Team(rider, nullptr);
    v.enemy = Unfriendly(rider, user);
    v.tied = (v.flags >> 5) & 1;
    v.npc = IsNpc(rider, nullptr);
    v.activityDriver = v.flags & 1;
    if (v.npc) {
        BYTE* ai = NpcAi(rider, nullptr);
        v.orders = ai ? OrdersType(ai, nullptr) : -1;
        v.ordersAllowed = v.orders == 0 || v.orders == 8 || v.orders == 9;
    }
    v.blocks = (v.enemy && !v.tied) || (v.npc && !v.ordersAllowed && !v.activityDriver);
    return v;
}

const char* OrdersName(int orders) {
    switch (orders) {
    case -1: return "none known";
    case 0: return "none";
    case 8: return "ambient action";
    case 9: return "ambient ped";
    default: return "other orders";
    }
}

struct Seen {
    DWORD handle;
    int until;
};
Seen g_seen[16];

bool LoggedLately(DWORD handle) {
    const int now = GameTime();
    Seen* oldest = &g_seen[0];
    for (Seen& seen : g_seen) {
        if (seen.handle == handle) {
            if (now < seen.until) return true;
            seen.until = now + 4000;
            return false;
        }
        if (seen.until < oldest->until) oldest = &seen;
    }
    *oldest = {handle, now + 4000};
    return false;
}

// The use prompt's two questions (0x8B1AB0, the use search's vehicle: may he get in; else may he flip it).
constexpr DWORD kPromptAsksEnter = 0x8B1B52, kPromptAsksFlip = 0x8B1BB4;

void LogRiders(BYTE* user, BYTE* vehicle, bool usable, DWORD from, bool a, bool b) {
    const int seats = Seats(vehicle);
    int riders = 0;
    bool anyBlocks = false;
    for (int seat = 0; seat < seats; ++seat) {
        BYTE* rider = SeatOccupant(vehicle, seat);
        if (!rider) continue;
        if (riders++ == 0) {
            if (LoggedLately(Handle(vehicle))) return;
            const char* asked = from == kPromptAsksEnter ? "the use prompt" : (from == kPromptAsksFlip ? "the use prompt (to flip it)" : "the game");
            if (usable)
                rfa::LogDetail("Riders: %s offers the player %s (0x%08lX) with its riders in:", asked, VehicleName(vehicle), Handle(vehicle));
            else
                rfa::Log("Riders: the player can't get into %s (0x%08lX; asked by %s with %d, %d):", VehicleName(vehicle), Handle(vehicle), asked, a, b);
        }
        const Verdict v = Judge(rider, user);
        anyBlocks |= v.blocks;
        rfa::LogDetail("Riders:   seat %d: 0x%08lX, %s, team %d (%s the player's enemy), %s, orders %d (%s), flags 0x%08lX (tied up %d, activity driver %d)"
                 " → Armageddon: %s; Guerrilla: %s",
                 seat, Handle(rider), StateName(Field<int>(rider, kState)), v.team, v.enemy ? "is" : "not", v.npc ? "an NPC" : "not an NPC",
                 v.orders, OrdersName(v.orders), v.flags, v.tied, v.activityDriver, v.blocks ? "blocks" : "lets him in",
                 v.enemy && !v.tied ? "blocks (an enemy in the seat: kill him first)"
                 : v.blocks         ? "blocks (an NPC with orders that aren't none or ambient)"
                                    : "lets him in (the rider is made to get out)");
    }
    if (riders && !anyBlocks && !usable)
        rfa::LogDetail("Riders:   no rider blocks: the game refused before the riders (vehicle flags 0x%08lX 0x%08lX, object flags 0x%02X)",
                 Field<DWORD>(vehicle, kVehicleFlags), Field<DWORD>(vehicle, kVehicleFlags2), Field<BYTE>(vehicle, kObjectFlags));
}

bool __cdecl Usable(BYTE* user, void* enter, BYTE* vehicle, bool a, bool b) {
    const DWORD from = reinterpret_cast<DWORD>(_ReturnAddress());
    const bool usable = g_usable(user, enter, vehicle, a, b);
    if (!user || !vehicle || !Watching() || user != Player()) return usable;
    if (!usable || from == kPromptAsksEnter) LogRiders(user, vehicle, usable, from, a, b);
    return usable;
}

// For 8 s after the player asks to get into a vehicle with riders: what they do.
struct Watch {
    DWORD vehicle = kNone;
    int until = 0;
    DWORD riders[8] = {};
    int states[8] = {};
    DWORD in[8] = {};
} g_watch;

// What player_use_vehicle (0x8B9680, Guerrilla's RM 0xA9E160) weighs before it pulls a driver out: the person in
// or getting into the driver's seat (vehicle_passenger_in_or_entering_seat 0x603DF0: a seat record entering it,
// or someone whose own seat, human +0x1168, is it), as an NPC (0x476ED0), not an activity's driver (+0x1554 bit 0)
// and not defending the player (npc_is_defending_player 0x8ACA10). Then the hijacked reaction and flag.
auto const PersonInSeat = reinterpret_cast<BYTE*(__cdecl*)(BYTE* vehicle, int seat)>(0x603DF0);
auto const AsNpc = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, BYTE* object)>(0x476ED0);
auto const DefendingPlayer = reinterpret_cast<bool(__cdecl*)(BYTE* npc)>(0x8ACA10);
constexpr DWORD kOwnSeat = 0x1168;  // a human: the seat he sits in

void DescribeDriver(BYTE* vehicle, char* out, size_t size) {
    BYTE* driver = SeatOccupant(vehicle, kDriverSeat);
    BYTE* found = PersonInSeat(vehicle, kDriverSeat);
    BYTE* npc = found ? AsNpc(kWorld, nullptr, found) : nullptr;
    int record = -1, interaction = -1, destination = -1;
    if (driver) {
        BYTE* vi = vehicle + kViInstance;
        record = FindRider(vi, nullptr, driver);
        if (record != -1) {
            const BYTE* seatRecord = SeatRecord(vi + kSeatRecords, nullptr, record);
            interaction = Field<int>(seatRecord, 4);
            destination = Field<int>(seatRecord, kDestinationSeat);
        }
    }
    sprintf_s(out, size,
              "the driver 0x%08lX (his own seat %d, seat record %d: interaction %d, going to seat %d); the driver's seat's person %s%s%s",
              driver ? Handle(driver) : kNone, driver ? Field<int>(driver, kOwnSeat) : -1, record, interaction, destination,
              !found ? "none found" : (!npc ? "found, not an NPC" : "an NPC"),
              npc && (Field<DWORD>(npc, kFlags) & 1) ? ", an activity's driver" : "", npc && DefendingPlayer(npc) ? ", defending the player" : "");
}

bool __cdecl PlayerRequestEnter(BYTE* human, BYTE* vehicle, int* enter) {
    char driver[200] = "";
    const bool watch = human && vehicle && Watching() && human == Player() && VehicleOf(human) != Handle(vehicle);
    if (watch) DescribeDriver(vehicle, driver, sizeof(driver));
    const bool asked = g_requestEnter(human, vehicle, enter);
    if (!human || !vehicle || !Watching() || human != Player()) return asked;
    int riders = 0;
    for (int seat = 0, seats = Seats(vehicle); seat < seats; ++seat)
        if (SeatOccupant(vehicle, seat)) ++riders;
    if (!riders) return asked;
    RFA_LOG_DETAIL_FIRST(100, "Riders: the player asks to get into %s (0x%08lX, %d riders): %s; the vehicle %s hijacked%s%s", VehicleName(vehicle), Handle(vehicle),
                  riders, asked ? "the game takes it" : "the game refuses", (Field<DWORD>(vehicle, kVehicleFlags) & kHijacked) ? "is" : "isn't",
                  watch ? "; " : "", driver);
    g_watch = {};
    g_watch.vehicle = Handle(vehicle);
    g_watch.until = GameTime() + 8000;
    int n = 0;
    for (int seat = 0, seats = Seats(vehicle); seat < seats && n < 8; ++seat)
        if (BYTE* rider = SeatOccupant(vehicle, seat)) {
            g_watch.riders[n] = Handle(rider);
            g_watch.states[n] = Field<int>(rider, kState);
            g_watch.in[n] = VehicleOf(rider);
            ++n;
        }
    return asked;
}

void WatchFrame() {
    if (g_watch.vehicle == kNone) return;
    if (GameTime() > g_watch.until) {
        RFA_LOG_DETAIL_FIRST(100, "Riders: 8 s after the player asked to get into 0x%08lX: watching ends", g_watch.vehicle);
        g_watch.vehicle = kNone;
        return;
    }
    for (int i = 0; i < 8; ++i) {
        if (!g_watch.riders[i]) continue;
        BYTE* rider = HumanByHandle(kWorld, nullptr, g_watch.riders[i]);
        const int state = rider ? Field<int>(rider, kState) : -1;
        const DWORD in = rider ? VehicleOf(rider) : kNone;
        if (state == g_watch.states[i] && in == g_watch.in[i]) continue;
        RFA_LOG_DETAIL_FIRST(200, "Riders:   rider 0x%08lX: %s → %s, %s", g_watch.riders[i], StateName(g_watch.states[i]), rider ? StateName(state) : "gone",
                      in == kNone ? "out of the vehicle" : (in == g_watch.vehicle ? "in the vehicle" : "in another vehicle"));
        g_watch.states[i] = state;
        g_watch.in[i] = in;
    }
}

// ---- Shooting: each of the player's bullets that passes someone seated
struct Rider {
    DWORD handle = kNone;
    bool touched = false;  // the ray met his body (the filter saw him)
    bool hidden = false;
    int hitBox = -2;       // -2 not tested, -1 tested, missed, else the hit box
    bool hit = false;      // the bullet's hit reached him
    float damage = 0, before = 0, after = 0;
    bool died = false;
    char why[240] = "";    // what the game worked out, and which of Armageddon's tests would have kept the hit points
};
constexpr int kMaxRiders = 8;
struct Shot {
    bool on = false;        // inside the bullet test of a shot of the player's
    bool pending = false;   // its log waits for the hit (the next frame)
    V3 start{}, end{}, hitAt{};
    DWORD hitHandle = kNone;
    int count = 0;
    Rider riders[kMaxRiders];
    float along[kMaxRiders] = {}, off[kMaxRiders] = {};
} g_shot;

Rider* Find(DWORD handle) {
    for (int i = 0; i < g_shot.count; ++i)
        if (g_shot.riders[i].handle == handle) return &g_shot.riders[i];
    return nullptr;
}
Rider* Note(BYTE* human) {
    const DWORD handle = Handle(human);
    if (Rider* known = Find(handle)) return known;
    if (g_shot.count >= kMaxRiders) return nullptr;
    Rider& rider = g_shot.riders[g_shot.count++];
    rider = {};
    rider.handle = handle;
    return &rider;
}
bool Seated(BYTE* human) { return human && VehicleOf(human) != kNone && !IsPlayer(human); }

V3 Lerp(const V3& a, const V3& b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }
// How far a point is from the ray, and how far along it (m).
void FromRay(const V3& start, const V3& end, const V3& p, float& along, float& off) {
    const V3 d{end.x - start.x, end.y - start.y, end.z - start.z}, w{p.x - start.x, p.y - start.y, p.z - start.z};
    const float length2 = d.x * d.x + d.y * d.y + d.z * d.z;
    float t = length2 > 0 ? (w.x * d.x + w.y * d.y + w.z * d.z) / length2 : 0;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    along = t * std::sqrt(length2);
    off = Distance(Lerp(start, end, t), p);
}

const char* WhatItMet(DWORD handle, BYTE* rider) {
    if (handle == kNone) return "nothing (the ray's end)";
    if (rider && handle == Handle(rider)) return "the rider";
    if (rider && handle == VehicleOf(rider)) return "the rider's vehicle";
    BYTE* object = ObjectByHandle(kWorld, nullptr, handle);
    if (!object) return "an object (gone)";
    if (HumanCast(kWorld, nullptr, object)) return "another person";
    switch (Field<BYTE>(object, 0x7A)) {
    case 3: return "another vehicle";
    case 2: return "a building or prop";
    default: return "another object";
    }
}

void FlushShot() {
    if (!g_shot.pending) return;
    g_shot.pending = false;
    for (int i = 0; i < g_shot.count; ++i) {
        const Rider& r = g_shot.riders[i];
        if (g_shot.off[i] < 0) continue;  // noted by the filter but not near the ray's line (another rider in the way)
        BYTE* rider = HumanByHandle(kWorld, nullptr, r.handle);
        BYTE* vehicle = rider ? VehicleByHandle(kWorld, nullptr, VehicleOf(rider)) : nullptr;
        const float hitAlong = Distance(g_shot.start, g_shot.hitAt);
        char filter[120] = "-";
        if (rider && vehicle) {
            const DWORD own = DefaultFilter(rider);
            sprintf_s(filter, "0x%08lX (%s; its vehicle's 0x%08lX%s)", own,
                      own == *reinterpret_cast<const DWORD*>(kMinimumFilter) ? "the minimum one" : (own == VehicleFilter(vehicle) ? "the vehicle's" : "another"),
                      VehicleFilter(vehicle), InProtectedSeat(rider) ? ", a protected seat" : "");
        }
        RFA_LOG_DETAIL_FIRST(400,
                      "Riders: a shot of the player's passed 0x%08lX (%s, seat %d of %s) %.2f m from the ray, %.1f m along it; the ray met %s at %.1f m;"
                      " his body %s%s; hit boxes %s; his body's filter %s; the hit %s",
                      r.handle, rider ? StateName(Field<int>(rider, kState)) : "gone", rider && vehicle ? SeatOf(vehicle, rider) : -1, VehicleName(vehicle),
                      g_shot.off[i], g_shot.along[i], WhatItMet(g_shot.hitHandle, rider), hitAlong, r.touched ? "touched" : "not touched",
                      r.hidden ? " (hidden)" : "", r.hitBox == -2 ? "not tested" : (r.hitBox == -1 ? "missed" : "hit"), filter,
                      r.hit ? "reached him" : "didn't reach him");
        if (r.hit)
            RFA_LOG_DETAIL_FIRST(400, "Riders:   damage %.1f (%s): hit points %.1f → %.1f%s", r.damage, r.why, r.before, r.after, r.died ? ", dead" : "");
    }
    g_shot.count = 0;
}

bool __cdecl TestBullet(BYTE* info, BYTE* shooter, bool a, bool b, bool c) {
    // Where the game's call returns to (the hook is reached by a jump from the function's start, so it's the caller's).
    const DWORD from = reinterpret_cast<DWORD>(_ReturnAddress());
    bool shot = false;
    for (DWORD at : kShotReturns) shot |= from == at;
    if (!shot || !info || !shooter || !Watching() || shooter != Player()) return g_testBullet(info, shooter, a, b, c);
    FlushShot();
    g_shot.on = true;
    g_shot.count = 0;
    const bool hit = g_testBullet(info, shooter, a, b, c);
    g_shot.on = false;
    g_shot.start = Field<V3>(info, kRayStart);
    g_shot.end = Field<V3>(info, kRayEnd);
    g_shot.hitAt = Field<V3>(info, kRayHitAt);
    g_shot.hitHandle = Field<DWORD>(info, kRayHit);
    // Who sits within 1 m of the ray: along the body, from the seat to the head.
    for (int i = 0; i < g_shot.count; ++i) g_shot.off[i] = -1;
    for (int i = 0, n = HumanCount(kWorld, nullptr); i < n; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!Seated(human) || IsDead(human)) continue;
        V3 head{};
        HeadPosition(human, &head);
        const V3 feet = Pos(human);
        float bestAlong = 0, bestOff = 1e9f;
        for (int k = 0; k <= 4; ++k) {
            float along, off;
            FromRay(g_shot.start, g_shot.end, Lerp(feet, head, k / 4.0f), along, off);
            if (off < bestOff) bestOff = off, bestAlong = along;
        }
        if (bestOff > 1.0f && !Find(Handle(human))) continue;  // (one the ray touched is logged however far it seems)
        Rider* rider = Note(human);
        if (!rider) break;
        const int index = static_cast<int>(rider - g_shot.riders);
        g_shot.off[index] = bestOff;
        g_shot.along[index] = bestAlong;
    }
    g_shot.pending = g_shot.count > 0;
    return hit;
}

bool __cdecl BulletFilter(BYTE* object, BYTE* data) {
    const bool stops = g_bulletFilter(object, data);
    if (g_shot.on && object)
        if (BYTE* human = HumanCast(kWorld, nullptr, object); Seated(human))
            if (Rider* rider = Note(human)) {
                rider->touched = true;
                rider->hidden = (Field<DWORD>(human, kHumanFlags) >> 2) & 1;
            }
    return stops;
}

int __cdecl HitLocation(void* a0, BYTE* human, void* a2, void* a3, void* a4, float a5, void* a6, void* a7, void* a8) {
    const int location = g_hitLocation(a0, human, a2, a3, a4, a5, a6, a7, a8);
    if (g_shot.on && Seated(human))
        if (Rider* rider = Note(human)) rider->hitBox = location;
    return location;
}

// ---- A bullet that hit an NPC who sits in a vehicle: while the game deals with it, who he is and what it did.
struct RiderHit {
    BYTE* victim = nullptr;
    bool calculated = false;
    float calculation = 0;  // the damage the game worked out (0x87BF50)
    bool ruleSetAside = false;  // the human damage's in-a-vehicle rule was set aside for him
} g_hit;

// The damage the game works out for a hit (0x87BF50: the attacker may hurt him, then the human damage 0x85E0C0).
float __cdecl DamageCalc(BYTE* damage) {
    const float worked = g_damageCalc(damage);
    if (g_hit.victim && damage && Field<BYTE*>(damage, kVictim) == g_hit.victim) {
        g_hit.calculated = true;
        g_hit.calculation = worked;
    }
    return worked;
}

// The rider whose vehicle is set aside for the human damage's in-a-vehicle rule, and that vehicle.
struct SetAside {
    BYTE* rider = nullptr;
    DWORD vehicle = kNone;
} g_setAside;

void PutBack() {
    if (!g_setAside.rider) return;
    *reinterpret_cast<DWORD*>(g_setAside.rider + kInVehicleField) = g_setAside.vehicle;
    g_setAside = {};
}

// Guerrilla's human_damage: a bullet hurts an NPC who sits in a vehicle as it would on foot.
bool RuleToSetAside(const BYTE* damage, BYTE* victim) {
    if (!g_asGuerrilla || !Watching() || Field<int>(damage, kKind) != kBulletDamage) return false;
    const float amount = Field<float>(damage, kDamage);
    if (!(amount > 0) || amount == kKill) return false;
    return VehicleOf(victim) != kNone && IsNpc(victim, nullptr) && !IsPlayer(victim);
}

float __cdecl HumanDamage(BYTE* damage) {
    BYTE* victim = damage ? Field<BYTE*>(damage, kVictim) : nullptr;
    if (!victim || g_setAside.rider || !RuleToSetAside(damage, victim)) {
        const float done = g_humanDamage(damage);
        GuerrillaCampaign_HumanDamaged(damage, done);  // the activities' listeners (Guerrilla's activity_register_damage)
        return done;
    }
    g_setAside = {victim, VehicleOf(victim)};
    *reinterpret_cast<DWORD*>(victim + kInVehicleField) = kNone;
    if (g_hit.victim == victim) g_hit.ruleSetAside = true;
    const float done = g_humanDamage(damage);
    PutBack();  // (if the function stopped before its next call)
    GuerrillaCampaign_HumanDamaged(damage, done);
    return done;
}

// The human damage's first call after its in-a-vehicle rule: the rider's vehicle goes back before anything else.
void* __fastcall CopyHitInfo(void* self, void* unused, const void* source) {
    if (g_setAside.rider && reinterpret_cast<DWORD>(_ReturnAddress()) == kAfterRiderRule) PutBack();
    return g_copyHitInfo(self, unused, source);
}

bool g_log;  // RiderLog

void __fastcall BulletHit(BYTE* victim, void* unused, BYTE* attacker, float damage, BYTE* info) {
    if (!g_log || !victim || !attacker || attacker != Player() || !Watching() || !Seated(victim) || !IsNpc(victim, nullptr)) {
        g_bulletHit(victim, unused, attacker, damage, info);
        return;
    }
    const RiderHit outer = g_hit;  // (a hit within a hit: kept, and put back)
    g_hit = {};
    g_hit.victim = victim;
    const float before = Field<float>(victim, kHitPoints);
    g_bulletHit(victim, unused, attacker, damage, info);
    const RiderHit seen = g_hit;
    g_hit = outer;

    BYTE* vehicle = VehicleByHandle(kWorld, nullptr, VehicleOf(victim));
    const BYTE* entry = vehicle ? Field<const BYTE*>(vehicle, kVehicleInfo) : nullptr;
    const bool vehicleInvulnerable = entry && ((entry[kHumansInvulnerable] >> 2) & 1);
    char why[240];
    char worked[32] = "not worked out";
    if (seen.calculated) sprintf_s(worked, "worked out %.1f", seen.calculation);
    sprintf_s(why, "%s; the in-a-vehicle rule %s; the vehicle's humans_invulnerable %d, his own invulnerability %d", worked,
              seen.ruleSetAside ? "set aside, as Guerrilla" : (g_asGuerrilla ? "not set aside" : "kept (RiderDamageAsGuerrilla=0)"),
              vehicleInvulnerable, Invulnerable(victim, nullptr));
    const float after = Field<float>(victim, kHitPoints);
    Rider* rider = Find(Handle(victim));
    if (!rider) {
        RFA_LOG_DETAIL_FIRST(200, "Riders: the player's bullet hit 0x%08lX (%s in %s): damage %.1f (%s), hit points %.1f → %.1f%s", Handle(victim),
                      StateName(Field<int>(victim, kState)), VehicleName(vehicle), damage, why, before, after, IsDead(victim) ? ", dead" : "");
        return;
    }
    rider->hit = true;
    rider->damage = damage;
    rider->before = before;
    rider->after = after;
    rider->died = IsDead(victim);
    strcpy_s(rider->why, why);
}

// ---- Changing seats (Guerrilla's player_switch_seat): from any seat but the driver's to the driver's, else to the
// seat of the vehicle's first gun.
void SwitchSeat(BYTE* player) {
    BYTE* vehicle = VehicleByHandle(kWorld, nullptr, VehicleOf(player));
    if (!vehicle || Field<int>(player, kState) == kExiting) return;
    BYTE* mount = TurretMount(vehicle, 0);
    const BYTE* mountInfo = mount ? Field<const BYTE*>(mount, 0) : nullptr;
    const int gunSeat = mountInfo ? Field<int>(mountInfo, 8) : -1;
    if (gunSeat == -1) {
        RFA_LOG_DETAIL_FIRST(20, "Change seat: %s has no gun seat; the player stays where he is", VehicleName(vehicle));
        return;
    }
    BYTE* interaction = vehicle + kViInstance;
    const int record = FindRider(interaction, nullptr, player);
    if (record == -1) return;
    const BYTE* seatRecord = SeatRecord(interaction + kSeatRecords, nullptr, record);
    const int to = Field<int>(seatRecord, kDestinationSeat) != kDriverSeat ? kDriverSeat : gunSeat;
    const int from = SeatOf(vehicle, player);
    if (to == from) return;
    BYTE enter[16];
    MakeEnterData(enter, nullptr);
    *reinterpret_cast<int*>(enter + 0) = to;  // specified_seat
    *reinterpret_cast<int*>(enter + 4) = 0;   // seat_selection_method: SPECIFIED
    *reinterpret_cast<int*>(enter + 8) = -1;  // destination_seat
    enter[12] = 0;                            // flags
    const bool asked = RequestEnter(player, vehicle, reinterpret_cast<int*>(enter));
    if (g_log)
        RFA_LOG_DETAIL_FIRST(100, "Change seat: in %s from seat %d to seat %d (%s): %s", VehicleName(vehicle), from, to, to == kDriverSeat ? "the driver's" : "the gun's",
                      asked ? "the game takes it" : "the game refuses");
}

void ChangeSeatFrame() {
    if (!g_changeSeats || !Watching()) return;
    BYTE* player = Player();
    if (!player || VehicleOf(player) == kNone) return;
    const int set = *reinterpret_cast<const int*>(kControlSet);
    if (!JustPressed(kChangeSeatControl, set == -1 ? 0 : set, nullptr)) return;
    BYTE* vehicle = VehicleByHandle(kWorld, nullptr, VehicleOf(player));
    if (!vehicle || !InVehicleInteraction(player, vehicle)) return;
    SwitchSeat(player);
}

// ---- Getting out when asked: Guerrilla's get_out_of_vehicle goal and action, which Armageddon dropped
//
// When someone takes an NPC's seat (the player getting into a car with a driver, above all), both games'
// vehicle interaction (vi_request_npc_exit: Guerrilla's RM 0xB814E0, Armageddon's in 0x64CC70) sets the NPC's
// force_out_of_vehicle (Guerrilla's npc +0xD04 bit 18, Armageddon's +0x1554 bit 21), asks him to make way and
// reserves his seat for the newcomer. In Guerrilla the AI does the rest: get_out_of_vehicle_goal (RM 0x5EA660)
// is the most wanted goal while the flag is set, and get_out_of_vehicle_action (enter RM 0x66FD90, do_action RM
// 0x646650, cleanup RM 0x5F4BD0), each frame: a driver leaves the car's controls (vehicle_set_controls_when_
// abandoned); while the car does more than 1.5 m/s he waits, unless ai_vehicle_should_get_out_of_moving_vehicle
// (RM 0x5F4200: no driver, the player driving, a driver he isn't friendly with, the car on fire or +0x3F24
// bits 20-21) says jump; then vehicle_interaction_request_exit with nothing asked (his normal exit); once he's
// out the flag is cleared. Armageddon has neither the goal nor the action (no such classes in its program), so
// the flag is set and nothing comes of it: the driver stays. Here the action runs as Guerrilla's, in Guerrilla's
// world ([GuerrillaVehicles] CarjackAsGuerrilla=1). (Guerrilla's driver also says a line, by his district's
// morale; Armageddon's people have other voice lines, so none is said.)
constexpr DWORD kNpcFlags = 0x1554, kForceOut = 1u << 21;
constexpr int kDriving = 9;
constexpr float kLeaveSpeed = 1.5f;  // m/s (Guerrilla's speed squared 2.25)
constexpr DWORD kOnFire = 1u << 30, kVehicleFlags2021 = 0x300000;  // vehicle +0x4FB0 (Guerrilla's +0x3F24)
auto const Disposition = reinterpret_cast<int(__cdecl*)(BYTE* human, BYTE* other)>(0x8A04D0);  // human_teams_get_disposition
bool g_carjack;  // CarjackAsGuerrilla

struct Leaving {
    DWORD npc, vehicle;
    V3 last{};
    int lastTime = -1;
    float speed = 0;
    int nextAsk = 0;
};
std::vector<Leaving> g_leaving;

// ai_vehicle_should_get_out_of_moving_vehicle.
bool JumpFromMovingVehicle(BYTE* npc, BYTE* vehicle) {
    BYTE* driver = SeatOccupant(vehicle, kDriverSeat);
    if (!driver || IsPlayer(driver) || Disposition(npc, driver) <= 1) return true;
    const DWORD flags = Field<DWORD>(vehicle, kVehicleFlags);
    return (flags & kOnFire) || (flags & kVehicleFlags2021);
}

void GetOutFrame() {
    if (!g_carjack || !Watching()) return;
    const int now = GameTime();
    for (int i = 0, n = HumanCount(kWorld, nullptr); i < n; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!human || VehicleOf(human) == kNone || !(Field<DWORD>(human, kNpcFlags) & kForceOut) || !IsNpc(human, nullptr)) continue;
        bool known = false;
        for (const Leaving& l : g_leaving) known |= l.npc == Handle(human);
        if (known) continue;
        g_leaving.push_back({Handle(human), VehicleOf(human)});
        RFA_LOG_DETAIL_FIRST(60, "Carjack: 0x%08lX (%s) is asked out of %s (someone takes his seat): he gets out, as Guerrilla's", Handle(human),
                      StateName(Field<int>(human, kState)), VehicleName(VehicleByHandle(kWorld, nullptr, VehicleOf(human))));
    }
    for (auto l = g_leaving.begin(); l != g_leaving.end();) {
        BYTE* npc = HumanByHandle(kWorld, nullptr, l->npc);
        BYTE* vehicle = VehicleByHandle(kWorld, nullptr, l->vehicle);
        if (!npc || IsDead(npc) || !vehicle) { l = g_leaving.erase(l); continue; }
        if (VehicleOf(npc) != l->vehicle) {  // out: Guerrilla's cleanup clears the flag
            *reinterpret_cast<DWORD*>(npc + kNpcFlags) &= ~kForceOut;
            RFA_LOG_DETAIL_FIRST(60, "Carjack: 0x%08lX is out of %s", l->npc, VehicleName(vehicle));
            l = g_leaving.erase(l);
            continue;
        }
        const V3 at = Pos(vehicle);
        if (l->lastTime >= 0 && now > l->lastTime) l->speed = Distance(at, l->last) * 1000.0f / static_cast<float>(now - l->lastTime);
        l->last = at;
        l->lastTime = now;
        if (!InVehicleInteraction(npc, nullptr)) { ++l; continue; }
        if (Field<int>(npc, kState) == kDriving) StopCar(vehicle);
        if (l->speed > kLeaveSpeed && !JumpFromMovingVehicle(npc, vehicle)) { ++l; continue; }
        if (now >= l->nextAsk) {
            BYTE exit[4] = {};  // vi_exit_data: nothing asked (his normal exit)
            RequestExit(npc, exit);
            l->nextAsk = now + 500;
        }
        ++l;
    }
}

template <typename Fn>
Fn Hook(const char* what, DWORD address, std::initializer_list<BYTE> opening, void* replacement) {
    if (memcmp(reinterpret_cast<const void*>(address), opening.begin(), opening.size()) != 0) {
        rfa::Log("Riders: %s (0x%08lX) isn't as expected; left as the game's", what, address);
        return nullptr;
    }
    auto original = reinterpret_cast<Fn>(rfa::HookFunction(address, opening.size(), replacement));
    if (!original) rfa::Log("Riders: could not hook %s (0x%08lX)", what, address);
    return original;
}

}  // namespace

void RidersFrame() {
    ChangeSeatFrame();
    GetOutFrame();
    if (!g_usable) return;
    FlushShot();
    WatchFrame();
}

void StartRiders() {
    g_asGuerrilla = rfa::IniInt(L"GuerrillaVehicles", L"RiderDamageAsGuerrilla", 1) != 0;
    g_changeSeats = rfa::IniInt(L"GuerrillaVehicles", L"ChangeSeatsAsGuerrilla", 1) != 0;
    g_carjack = rfa::IniInt(L"GuerrillaVehicles", L"CarjackAsGuerrilla", 1) != 0;
    g_log = rfa::ReadLogSwitch(L"GuerrillaVehicles", L"RiderLog");
    rfa::Log(g_carjack ? "Carjack: in Guerrilla's world someone whose seat is taken gets out, as Guerrilla's ([GuerrillaVehicles] CarjackAsGuerrilla=1)"
                       : "Carjack: someone whose seat is taken stays in it, as in Armageddon ([GuerrillaVehicles] CarjackAsGuerrilla=0)");
    // The human damage is wrapped either way: the campaign's activities hear of its hits through it (HumanDamage);
    // the rider rule inside it follows RiderDamageAsGuerrilla (RuleToSetAside).
    g_humanDamage = Hook<HumanDamageFn>("the human damage", kHumanDamage, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x4C, 0x01, 0x00, 0x00},
                                        reinterpret_cast<void*>(&HumanDamage));
    if (g_humanDamage)
        g_copyHitInfo = Hook<CopyHitInfoFn>("the human damage's record copy", kCopyHitInfo, {0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC},
                                            reinterpret_cast<void*>(&CopyHitInfo));
    if (g_asGuerrilla) {
        rfa::Log(g_humanDamage && g_copyHitInfo
                     ? "Riders: in Guerrilla's world bullets hurt NPCs who sit in a vehicle, as in Guerrilla ([GuerrillaVehicles] RiderDamageAsGuerrilla=1)"
                     : "Riders: the game's human damage isn't as expected; riders keep Armageddon's rule (only a vehicle's end kills them)");
    } else {
        rfa::Log("Riders: bullets don't hurt NPCs who sit in a vehicle, as in Armageddon ([GuerrillaVehicles] RiderDamageAsGuerrilla=0)");
    }
    rfa::Log(g_changeSeats ? "Change seat: in Guerrilla's world the change seat key moves the player between the driver's seat and the gun's "
                             "([GuerrillaVehicles] ChangeSeatsAsGuerrilla=1)"
                           : "Change seat: off, as in Armageddon ([GuerrillaVehicles] ChangeSeatsAsGuerrilla=0)");
    if (g_log) {
        g_bulletHit = Hook<BulletHitFn>("human::process_bullet_hit", kBulletHit, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x24, 0x01, 0x00, 0x00},
                                        reinterpret_cast<void*>(&BulletHit));
        g_damageCalc = Hook<DamageCalcFn>("the damage's working out", kDamageCalc, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x58}, reinterpret_cast<void*>(&DamageCalc));
    }
    if (!g_log) return;  // the riders' log needs RiderLog=1 and Log=1
    g_usable = Hook<UsableFn>("vehicle_is_usable", kUsable, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x74}, reinterpret_cast<void*>(&Usable));
    g_requestEnter = Hook<RequestEnterFn>("vehicle_interaction_request_enter", kRequestEnter, {0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x10},
                                          reinterpret_cast<void*>(&PlayerRequestEnter));
    g_testBullet = Hook<TestBulletFn>("weapons_test_bullet", kTestBullet, {0x55, 0x8B, 0xEC, 0xB8, 0x64, 0x24, 0x00, 0x00}, reinterpret_cast<void*>(&TestBullet));
    g_bulletFilter = Hook<BulletFilterFn>("the bullet ray's filter", kBulletFilter, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C}, reinterpret_cast<void*>(&BulletFilter));
    g_hitLocation = Hook<HitLocationFn>("human_get_hit_location", kHitLocation, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x58, 0x02, 0x00, 0x00},
                                        reinterpret_cast<void*>(&HitLocation));
    rfa::Log("Riders: logging shots past seated people and getting into occupied vehicles in Guerrilla's world ([GuerrillaVehicles] RiderLog=1)%s",
             g_usable && g_requestEnter && g_testBullet && g_bulletFilter && g_hitLocation && g_bulletHit ? "" : " (some parts missing, above)");
}

}  // namespace gv
