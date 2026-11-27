// Guerrilla's EDF convoys in its world (object_convoy, its spawner and its lead and follow driving, which
// Armageddon deleted), ported onto the code Armageddon kept. A part of guerrilla_vehicles.
// GuerrillaBuilder.exe writes mods\guerrilla_vehicles\convoys.txt: each convoy's convoys.xtbl row (Destroy or
// Capture, difficulty, normal and evade speeds, its hours), its end point (where it sets off) and members in
// order (role, how many people, the vehicles of its group), and its route along the roads.
//
// Who sends them: in Guerrilla every convoy is its convoy activity's (activity_convoy_data): the natural spawner
// offers one (do_activity_spawn_frame 0x7A83F0), and the activity watches it and steers it (attacked, every role to
// break and attack, the break-and-attack squads let go, the capture target's crew, the end). While guerrilla_campaign
// runs its activities that is so here too (its convoy.cpp, through the GuerrillaVehicles_Convoy* calls below). Without
// them (guerrilla_campaign off, or its Activities=0) this part sends its own, as Guerrilla's spawner would after the
// campaign (convoys' collect_spawn_objects 0x780DA0: any convoy): every 4 to 6 s, one convoy at a time, the end point
// nearest the player that is more than 150 m from it, whose hours (tod_info) include the game's hour and which hasn't
// sent one in the last 10 minutes; none in the first [GuerrillaVehicles] ConvoyWaitAfterLoad= seconds in the world
// (600, Guerrilla's: an end point loads with its 10 minutes set). Such a convoy also decides for itself when it's
// attacked, and what then happens (below).
// Its crews are EDF people of the zone's spawn resource (crews.txt), as many as its squad has (1, 2, 4 or 7;
// at most the seats).
//
// While nobody is near, the convoy is a point moving along its route at its normal speed (Guerrilla simulates
// far squads); once that point is within 300 m of the player its vehicles are made on the route, the lead
// furthest ahead and each next one 20 m behind (Guerrilla's spawn 0xA61F20), and taken away again (back to
// the point) if they all get 450 m from the player out of view before it is attacked.
//
// Driving (lead_convoy_patrol_action, ai_vehicle_do_follow_frame): only drivers sit frames out (gunners fire
// with their own AI). The lead drives to the next route point (passed within 20 m, looking 3 ahead), on the
// roads, stopping 10 m short, at the convoy's speed (its evade speed once attacked), halving it while a gap
// between two of its vehicles is over 30 m and stopping while one is over 50 m (not once attacked). Each
// follower aims at the point on the route 20 m behind the vehicle ahead, at that vehicle's speed + 0.5 × how
// far its place is ahead of it − 0.4 (the convoy's speed + 5 m/s while more than 12.5 m off it; braking at
// 1 m/s or less), stopping 5 m short with no braking at the end.
//
// The activity's convoy: attacked (its own flag, latched) once a vehicle of it is at least 1 % damaged with its crew
// fighting the player; the rest is the activity's. At the route's end (convoy_at_destination 0x7AD460,
// convert_despawning_squad 0x7AD320) its targets still in line park, locked for the player and NPC drivers, their crews
// out to defend them, and every other member goes once out of sight; it is then over. Not ported: Guerrilla's
// update_npc_beliefs (each crew member told of the player every 1-3 s while it runs: Armageddon's call for it isn't
// found) and m_scripted_abandon on the running capture target (what Armageddon's AI does with it isn't known).
//
// A convoy of its own: attacked (update_attacked_status: a vehicle at least 1 % damaged with its crew fighting the
// player or the player near, or someone of it killed or a vehicle lost): the evade speed; its Break and Attack vehicles
// leave the convoy and chase the player (crews.cpp); a Capture convoy's target's crew gets out, so the player can take
// it. Ending: at the route's end the targets stay parked, their crews out to guard them, and the others go once out of
// sight; with no vehicle left it's over.
#define RFA_MOD_NAME "guerrilla_vehicles"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "campaign/campaign_api.h"
#include "vehicles_api.h"
#include "crews.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace gv {

namespace {

enum class Role { Target, Defender, Attack };
const char* RoleName(Role role) { return role == Role::Target ? "target" : role == Role::Attack ? "break-and-attack" : "defender"; }

struct MemberInfo {
    Role role;
    int people;
    std::vector<std::string> vehicles;
};
struct Definition {
    std::string name, district;
    bool capture = false;
    bool disabled = false;     // convoy_disabled: held until a district is liberated (convoys.txt's last field)
    float normal = 8, evade = 8;
    int startHour = 0, endHour = 0;
    V3 at{};
    std::vector<MemberInfo> members;
    std::vector<V3> route;
    std::vector<float> along;  // each route point's distance along the route
    int nextAllowed = 0;       // the game's clock: the end point's 10 minutes
};
std::vector<Definition> g_definitions;

struct Member {
    const MemberInfo* info;
    Role role;                 // its role now (every one break and attack once the activity says so)
    Crew crew;
    VehicleSpawn spawn;
    bool spawning = false, gone = false, left = false;
    DWORD vehicle = kNone;     // the vehicle made for it, kept after it left the line (the activity counts it)
    bool driverless = false;   // its crew lost the wheel
    Chase chase;
    int routeIndex = 0;
    int nextExitAsk = 0;       // the capture target's crew: when to ask them out again (the game's clock)
    bool abandoning = false;   // the capture target, its crew asked out: braked, no longer driven along the route
};
struct Convoy {
    Definition* def;
    std::vector<Member> members;
    bool spawned = false, attacked = false, ended = false;
    bool forActivity = false;  // the campaign's convoy activity's
    bool atDestination = false;
    bool targetMayExit = false;
    float ghost = 0;  // how far along the route the lead is, while it's only a point
    int next = 1;     // the next route point for the lead
    int lastTime = -1;
};
std::vector<Convoy> g_convoys;  // at most one
int g_nextTry = -1, g_enteredWorld = -1, g_waitAfterLoad = 600000;
bool g_inWorld = false;

constexpr float kSpacing = 20.0f, kSpawnRange = 300.0f, kGhostRange = 450.0f, kPassed = 20.0f, kMinStart = 150.0f;
constexpr int kCooldown = 600000;
// The vehicle's second flags word (Guerrilla's +0x3F28, Armageddon's +0x4FB4, its bits above 16 one higher than
// Guerrilla's): player_may_capture (bit 17), disable_for_npc_drivers (18), disable_for_player (19).
constexpr DWORD kVehicleFlags2 = 0x4FB4, kPlayerMayCapture = 1u << 17, kLocked = (1u << 18) | (1u << 19);

bool IsDead(BYTE* object) {  // the object's own answer (its fifth virtual function)
    using Fn = bool(__fastcall*)(BYTE* self, void* unused);
    return (*reinterpret_cast<Fn**>(object))[4](object, nullptr);
}

bool ReadConvoys() {
    std::ifstream file(rfa::RootDir() + L"\\mods\\guerrilla_vehicles\\convoys.txt");
    for (std::string line; std::getline(file, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        std::istringstream in(line);
        std::string kind, name;
        in >> kind >> name;
        if (kind == "convoy") {
            Definition def;
            def.name = name;
            std::string type;
            int difficulty;
            if (!(in >> type >> difficulty >> def.normal >> def.evade >> def.startHour >> def.endHour >> def.at.x >> def.at.y >> def.at.z >> def.district)) continue;
            def.capture = type == "Capture";
            int disabled = 0;
            if (in >> disabled) def.disabled = disabled != 0;  // older files lack it
            g_definitions.push_back(def);
            continue;
        }
        if (g_definitions.empty() || g_definitions.back().name != name) continue;
        Definition& def = g_definitions.back();
        if (kind == "member") {
            std::string role, vehicles;
            MemberInfo member{};
            if (!(in >> role >> member.people >> vehicles)) continue;
            member.role = role == "Target" ? Role::Target : role == "Attack" ? Role::Attack : Role::Defender;
            member.vehicles = Split(vehicles, ',');
            def.members.push_back(member);
        } else if (kind == "route") {
            for (std::string point; in >> point;) {
                V3 p{};
                if (sscanf_s(point.c_str(), "%f,%f,%f", &p.x, &p.y, &p.z) == 3) def.route.push_back(p);
            }
            float total = 0;
            for (size_t i = 0; i < def.route.size(); ++i) {
                if (i > 0) total += Distance(def.route[i - 1], def.route[i]);
                def.along.push_back(total);
            }
        }
    }
    g_definitions.erase(std::remove_if(g_definitions.begin(), g_definitions.end(), [](const Definition& d) { return d.members.empty() || d.route.size() < 2; }),
                        g_definitions.end());
    return !g_definitions.empty();
}

float RouteLength(const Definition& def) { return def.along.back(); }

// The point (and the way the route goes there) `s` metres along the route.
V3 PointAt(const Definition& def, float s, V3* direction = nullptr, int* index = nullptr) {
    s = std::clamp(s, 0.0f, RouteLength(def));
    size_t i = 0;
    while (i + 2 < def.route.size() && def.along[i + 1] < s) ++i;
    const V3 a = def.route[i], b = def.route[i + 1];
    const float span = (std::max)(def.along[i + 1] - def.along[i], 0.001f), t = (s - def.along[i]) / span;
    if (direction) *direction = {b.x - a.x, 0, b.z - a.z};
    if (index) *index = static_cast<int>(i);
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// How far along the route a place is, looking near the route point it was at last (`hint`, kept up to date).
float Along(const Definition& def, const V3& p, int& hint) {
    float best = 1e9f, bestS = 0;
    const int from = (std::max)(0, hint - 3), to = (std::min)(static_cast<int>(def.route.size()) - 2, hint + 8);
    for (int i = from; i <= to; ++i) {
        const V3 a = def.route[i], b = def.route[i + 1];
        const float dx = b.x - a.x, dz = b.z - a.z, length2 = dx * dx + dz * dz;
        const float t = length2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.z - a.z) * dz) / length2, 0.0f, 1.0f) : 0.0f;
        const V3 on{a.x + dx * t, a.y, a.z + dz * t};
        const float d = FlatDistance(on, p);
        if (d < best) { best = d; bestS = def.along[i] + t * (def.along[i + 1] - def.along[i]); hint = i; }
    }
    return bestS;
}

bool HoursAllow(const Definition& def) {
    const int hour = GameHour();
    if (def.startHour == def.endHour) return true;
    if (def.endHour > def.startHour) return def.startHour <= hour && hour < def.endHour;
    return hour >= def.startHour || hour < def.endHour;  // across midnight
}

// Whether any district is liberated: Guerrilla's district::liberate_district (RM 0x7A5CE0) clears convoy_disabled on
// every convoy end point, so a held one sends convoys from the first liberation on. Asked of guerrilla_campaign
// (GuerrillaCampaign_DistrictLiberated) for the districts the end points name; without it, held ones stay held.
bool AnyDistrictLiberated() {
    if (!guerrilla_campaign::Running()) return false;
    for (const Definition& def : g_definitions) {
        std::string name = def.district;
        std::replace(name.begin(), name.end(), '_', ' ');  // convoys.txt writes spaces as _
        if (name != "-" && GuerrillaCampaign_DistrictLiberated(name.c_str()) == 1) return true;
    }
    return false;
}

void SetOff(Definition& def, int clock, bool forActivity) {
    Convoy convoy;
    convoy.def = &def;
    convoy.forActivity = forActivity;
    for (const MemberInfo& info : def.members) {
        Member member;
        member.info = &info;
        member.role = info.role;
        convoy.members.push_back(member);
    }
    convoy.ghost = kSpacing * (convoy.members.size() - 1);  // the lead, furthest ahead
    convoy.lastTime = clock;
    g_convoys.push_back(convoy);
}

void TryStart(const BYTE* player, int clock) {
    const V3 at = Pos(player);
    Definition* best = nullptr;
    const bool heldOnesGo = AnyDistrictLiberated();
    for (Definition& def : g_definitions) {
        if (def.disabled && !heldOnesGo) continue;
        if (clock < def.nextAllowed || !HoursAllow(def) || FlatDistance(def.at, at) <= kMinStart) continue;
        if (!best || FlatDistance(def.at, at) < FlatDistance(best->at, at)) best = &def;
    }
    if (!best) return;
    best->nextAllowed = clock + kCooldown;
    SetOff(*best, clock, false);
    RFA_LOG_DETAIL_FIRST(40, "Convoys: %s sets off (%s, %u vehicles, from %s, %.0f m away)", best->name.c_str(), best->capture ? "capture" : "destroy",
                  static_cast<unsigned>(best->members.size()), best->district.c_str(), FlatDistance(best->at, at));
}

// Makes its vehicles along the route behind the lead's point.
bool BeginSpawning(Convoy& convoy) {
    const Definition& def = *convoy.def;
    int k = 0;
    for (Member& member : convoy.members) {
        if (member.gone) continue;
        V3 direction;
        const V3 where = PointAt(def, convoy.ghost - kSpacing * k, &direction, &member.routeIndex);
        BYTE* entry = PickVehicle(member.info->vehicles);
        if (!entry) {
            RFA_LOG_DETAIL_FIRST(10, "Convoys: %s: none of its %s's vehicles is known to the game", def.name.c_str(), RoleName(member.info->role));
            return false;
        }
        PrepareSpawn(member.spawn, entry, {where.x, where.y + 0.5f, where.z}, direction, def.normal, 0x40B);
        member.spawning = true;
        ++k;
    }
    PointAt(def, convoy.ghost, nullptr, &convoy.next);
    convoy.next = (std::min)(convoy.next + 1, static_cast<int>(def.route.size()) - 1);
    convoy.spawned = true;
    return true;
}

void TakeAway(Convoy& convoy) {  // its vehicles and people gone at once (back to a point, or a failed start)
    for (Member& member : convoy.members) {
        member.spawning = false;
        if (!member.gone) Despawn(member.crew);
        member.crew = Crew{};  // made afresh when it comes back
        if (!member.gone) member.vehicle = kNone;
    }
    convoy.spawned = false;
}

void Retire(Convoy& convoy) {
    for (Member& member : convoy.members)
        if (!member.gone) {
            RetireWhenUnseen(member.crew);
            member.gone = true;
        }
}

// The vehicle's second flags word.
DWORD& Flags2(BYTE* vehicle) { return *reinterpret_cast<DWORD*>(vehicle + kVehicleFlags2); }

void Attacked(Convoy& convoy, const BYTE* player, const char* why) {
    convoy.attacked = true;
    RFA_LOG_DETAIL_FIRST(40, "Convoys: %s is attacked (%s)", convoy.def->name.c_str(), why);
    if (convoy.forActivity) return;  // what follows is the activity's
    for (Member& member : convoy.members) {
        if (member.gone) continue;
        BYTE* vehicle = VehicleByHandle(kWorld, nullptr, member.crew.vehicle);
        if (!vehicle) continue;
        if (member.role == Role::Attack) {
            member.left = true;
            member.chase = Chase{};
            member.chase.target = Handle(player);
        } else if (member.role == Role::Target && convoy.def->capture) {
            GetOut(member.crew, vehicle, false);  // Guerrilla's m_scripted_abandon: its crew bails out, the player may take it
            member.gone = true;
        }
    }
}

void AtDestination(Convoy& convoy) {
    RFA_LOG_DETAIL_FIRST(40, "Convoys: %s reached its destination", convoy.def->name.c_str());
    if (convoy.forActivity) {
        // convoy_at_destination: each target still in line parked (persistent no more, DEFEND orders at its vehicle, its
        // crew out, the vehicle locked: 0x60000 in Guerrilla's flags); every other member despawned out of sight
        // (request_despawn_all_members), the ones chasing the player too.
        for (Member& member : convoy.members) {
            BYTE* vehicle = member.crew.vehicle != kNone ? VehicleByHandle(kWorld, nullptr, member.crew.vehicle) : nullptr;
            if (vehicle && member.role == Role::Target && !member.left && !member.gone) {
                Flags2(vehicle) |= kLocked;
                GetOut(member.crew, vehicle, false);
            } else if (!member.crew.people.empty() || member.crew.vehicle != kNone) {
                RetireWhenUnseen(member.crew);
            }
            member.gone = true;
        }
        convoy.atDestination = true;
        convoy.ended = true;
        return;
    }
    for (Member& member : convoy.members) {
        if (member.gone || member.left) continue;
        BYTE* vehicle = VehicleByHandle(kWorld, nullptr, member.crew.vehicle);
        if (vehicle && member.role == Role::Target) GetOut(member.crew, vehicle, false);  // parked, its crew guarding it
        else RetireWhenUnseen(member.crew);
        member.gone = true;
    }
    convoy.ended = true;
}

// One frame of a convoy; false once it's over.
bool ConvoyFrame(Convoy& convoy, BYTE* player, int clock) {
    Definition& def = *convoy.def;
    const V3 at = Pos(player);
    const float dt = std::clamp((clock - convoy.lastTime) / 1000.0f, 0.0f, 0.25f);
    convoy.lastTime = clock;
    if (convoy.forActivity && convoy.atDestination) return true;  // kept for the activity, which ends it

    if (!convoy.spawned) {  // only a point on the route
        convoy.ghost += def.normal * dt;
        if (convoy.ghost >= RouteLength(def)) {
            RFA_LOG_DETAIL_FIRST(40, "Convoys: %s reached its destination unseen", def.name.c_str());
            if (convoy.forActivity) {
                convoy.atDestination = convoy.ended = true;
                return true;
            }
            return false;
        }
        if (Distance(PointAt(def, convoy.ghost), at) < kSpawnRange && !BeginSpawning(convoy)) {
            TakeAway(convoy);
            return false;
        }
        return true;
    }

    // Its vehicles being made.
    bool waiting = false;
    for (Member& member : convoy.members) {
        if (!member.spawning) continue;
        BYTE* vehicle = nullptr;
        const int result = TrySpawn(member.spawn, &vehicle);
        if (result == 1) { waiting = true; continue; }
        member.spawning = false;
        if (result == 0 && vehicle) NoteMadeBy(Handle(vehicle), "a convoy");
        if (result == 2) {
            RFA_LOG_DETAIL_FIRST(10, "Convoys: %s couldn't make its vehicles; it's called off", def.name.c_str());
            TakeAway(convoy);
            return false;
        }
        member.crew.vehicle = member.vehicle = Handle(vehicle);
        // set_ready_flags (0x780A90): a capture convoy's target may be taken by the player (player_may_capture).
        if (convoy.forActivity && def.capture && member.role == Role::Target) Flags2(vehicle) |= kPlayerMayCapture;
        const int seats = (std::max)(1, Seats(vehicle));
        if (const auto* people = People(ResourceAt(Pos(vehicle)), "EDF"))
            for (int i = 0; i < (std::min)(seats, member.info->people); ++i) member.crew.waiting.push_back((*people)[std::rand() % people->size()]);
    }
    if (waiting) return true;

    // Who is still in it.
    const DWORD playerVehicle = VehicleOf(player);
    bool lostOne = false;
    float nearest = 1e9f;
    bool anyInView = false;
    for (Member& member : convoy.members) {
        if (member.gone) continue;
        BYTE* vehicle = VehicleByHandle(kWorld, nullptr, member.crew.vehicle);
        if (!vehicle) { member.gone = true; lostOne = true; continue; }
        if (playerVehicle == member.crew.vehicle) {  // the player took it
            HandBack(member.crew, vehicle);
            member.gone = true;
            lostOne = true;
            continue;
        }
        const V3 where = Pos(vehicle);
        const V3 middle{where.x, where.y + 1.5f, where.z};
        nearest = (std::min)(nearest, Distance(where, at));
        anyInView = anyInView || InView(&middle.x, 4.0f);
        if (!Board(member.crew, vehicle)) continue;
        Track(member.crew, vehicle);
        if (member.crew.people.empty()) {  // nobody could be made for it: no member, no attack
            RetireWhenUnseen(member.crew);
            member.gone = true;
            continue;
        }
        if (!Driver(member.crew, vehicle) && !member.left) {
            if (Settling(member.crew)) continue;
            HandBack(member.crew, vehicle);  // its driver dead or out
            member.gone = true;
            member.driverless = true;
            lostOne = true;
            continue;
        }
        // The activity's capture target: its crew out as soon as they may (process_target_driver_flags: 30 % damaged or
        // stood still over 2 s), moving or not. Guerrilla's target squad has m_scripted_abandon (set_active_flags_capture
        // RM 0x76AFB0: the target role's member flags 0x28, player_may_capture and m_scripted_abandon), and with it
        // get_out_of_vehicle_action::get_cost (RM 0x66FAB0) leaves the speed out (the action then jumps from a moving
        // car: ai_vehicle_should_get_out_of_moving_vehicle RM 0x5F4200). Waiting here for it to slow below 1.5 m/s kept
        // the crew in a target that drove on under fire (2026-11-26 14:41: no way to take it). The car is braked and
        // everyone asked out, again each second while the driver is still in it (an exit the game refuses at speed is
        // asked again as it slows); once he's out, the driver check above hands it back and the convoy all attacks
        // (update_target_vehicle_driver RM 0x780B30).
        if (convoy.forActivity && def.capture && member.role == Role::Target && (convoy.targetMayExit || member.abandoning) && !member.left) {
            member.abandoning = true;  // out of the line: not driven any more (the lead and followers below)
            const int now = GameTime();
            if (now >= member.nextExitAsk) {
                member.nextExitAsk = now + 1000;
                RFA_LOG_DETAIL_FIRST(20, "Convoys: %s's capture target 0x%08lX: its crew may get out, asked out at %.1f m/s", def.name.c_str(),
                                     static_cast<unsigned long>(Handle(vehicle)), member.crew.speed);
                GetOut(member.crew, vehicle, false);
            }
            continue;
        }
        if (convoy.attacked) continue;
        if (convoy.forActivity) {
            // object_convoy's attacked flag: a vehicle of it at least 1 % damaged while its crew fights the player.
            if (Damage(vehicle) >= 0.01f && NoticedPlayer(member.crew)) Attacked(convoy, player, "a vehicle was damaged while its crew fights you");
        } else if (LostSomeone(member.crew) || (Damage(vehicle) >= 0.01f && (NoticedPlayer(member.crew) || Distance(where, at) < 120.0f))) {
            Attacked(convoy, player, LostSomeone(member.crew) ? "someone of it was killed" : "a vehicle was damaged");
        }
    }
    if (!convoy.forActivity && !convoy.attacked && lostOne && !convoy.ended) Attacked(convoy, player, "a vehicle was lost");

    // Far from everyone, not yet attacked: back to a point on the route.
    if (!convoy.attacked && !convoy.ended && nearest > kGhostRange && !anyInView) {
        for (Member& member : convoy.members)
            if (!member.gone && !member.left)
                if (BYTE* vehicle = VehicleByHandle(kWorld, nullptr, member.crew.vehicle)) {
                    int hint = member.routeIndex;
                    convoy.ghost = Along(def, Pos(vehicle), hint);
                    break;
                }
        TakeAway(convoy);
        return true;
    }

    // Those that left to chase the player.
    bool chasing = false;
    for (Member& member : convoy.members) {
        if (member.gone || !member.left) continue;
        BYTE* vehicle = VehicleByHandle(kWorld, nullptr, member.crew.vehicle);
        if (vehicle && (!member.crew.waiting.empty() || (Settling(member.crew) && !Driver(member.crew, vehicle)))) { chasing = true; continue; }
        const ChaseResult result = vehicle ? ChaseFrame(member.crew, member.chase, vehicle) : ChaseResult::Over;
        if (result == ChaseResult::Chasing) { chasing = true; continue; }
        if (result == ChaseResult::Lost) RetireWhenUnseen(member.crew);
        else if (result == ChaseResult::Over && vehicle) HandBack(member.crew, vehicle);
        member.gone = true;
    }
    if (convoy.ended) return chasing || convoy.forActivity;

    // The lead and its followers, in order.
    Member* lead = nullptr;
    for (Member& member : convoy.members)
        if (!member.gone && !member.left && !member.abandoning) { lead = &member; break; }
    if (!lead) {
        if (convoy.forActivity) return true;  // nobody left in line: the activity decides what that means
        if (!chasing) RFA_LOG_DETAIL_FIRST(40, "Convoys: %s is over (no vehicle of it left)", def.name.c_str());
        convoy.ended = true;
        return chasing;
    }
    BYTE* leadVehicle = VehicleByHandle(kWorld, nullptr, lead->crew.vehicle);
    if (!leadVehicle || !lead->crew.waiting.empty()) return true;
    const V3 leadAt = Pos(leadVehicle);
    Along(def, leadAt, lead->routeIndex);  // keeps its place on the route up to date (the followers' and the point's)
    for (int k = 0; k < 3 && convoy.next + k < static_cast<int>(def.route.size()); ++k)
        if (FlatDistance(leadAt, def.route[convoy.next + k]) <= kPassed) { convoy.next += k + 1; break; }
    if (convoy.next >= static_cast<int>(def.route.size())) {
        AtDestination(convoy);
        return chasing || convoy.forActivity;
    }
    const float speed = convoy.attacked ? def.evade : def.normal;
    float leadSpeed = speed, gap = 0;
    const Member* previous = nullptr;
    for (const Member& member : convoy.members) {
        if (member.gone || member.left || member.abandoning) continue;
        if (previous)
            if (BYTE* a = VehicleByHandle(kWorld, nullptr, previous->crew.vehicle))
                if (BYTE* b = VehicleByHandle(kWorld, nullptr, member.crew.vehicle)) gap = (std::max)(gap, FlatDistance(Pos(a), Pos(b)));
        previous = &member;
    }
    if (!convoy.attacked && gap > 50.0f) leadSpeed = 0;        // wait for the others
    else if (!convoy.attacked && gap > 30.0f) leadSpeed *= 0.5f;
    BYTE* driver = Driver(lead->crew, leadVehicle);
    if (driver) {
        YieldDriver(lead->crew, leadVehicle);
        if (*reinterpret_cast<const int*>(leadVehicle + kVehicleAi) != 1) SetVehicleAiState(leadVehicle + kVehicleAi, 1);
        if (leadSpeed <= 0) StopCar(leadVehicle);
        else {
            OverrideSpeed(leadVehicle, leadSpeed);
            TravelTo(driver, leadVehicle, &def.route[convoy.next].x, kNone, kNone, 1, 10.0f, 0, 0, 0, 1);
        }
    }
    // Followers: each 20 m behind the vehicle ahead, along the route.
    Member* ahead = lead;
    for (Member& member : convoy.members) {
        if (&member == lead || member.gone || member.left || member.abandoning) continue;
        BYTE* vehicle = VehicleByHandle(kWorld, nullptr, member.crew.vehicle);
        BYTE* aheadVehicle = VehicleByHandle(kWorld, nullptr, ahead->crew.vehicle);
        BYTE* follower = vehicle ? Driver(member.crew, vehicle) : nullptr;
        if (!vehicle || !aheadVehicle || !follower || !member.crew.waiting.empty()) continue;
        const float aheadS = Along(def, Pos(aheadVehicle), ahead->routeIndex), mineS = Along(def, Pos(vehicle), member.routeIndex);
        const float placeS = aheadS - kSpacing;
        const V3 place = PointAt(def, (std::max)(placeS, 0.0f));
        float v = (std::max)(0.0f, ahead->crew.speed + 0.5f * (placeS - mineS) - 0.4f);
        if (FlatDistance(place, Pos(vehicle)) > 12.5f) v = speed + 5.0f;
        YieldDriver(member.crew, vehicle);
        if (*reinterpret_cast<const int*>(vehicle + kVehicleAi) != 1) SetVehicleAiState(vehicle + kVehicleAi, 1);
        if (v <= 1.0f) StopCar(vehicle);
        else {
            OverrideSpeed(vehicle, v);
            TravelTo(follower, vehicle, &place.x, kNone, kNone, 1, 5.0f, 1, 0, 0, 1);
        }
        ahead = &member;
    }
    return true;
}

Convoy* ActivityConvoy() {
    for (Convoy& convoy : g_convoys)
        if (convoy.forActivity) return &convoy;
    return nullptr;
}

// Whether the people made for a crew are all dead or gone (squad_killed); false while none was made.
bool AllLost(const Crew& crew) {
    if (crew.people.empty() || !crew.waiting.empty()) return false;
    for (const Person& person : crew.people)
        if (BYTE* human = HumanByHandle(kWorld, nullptr, person.handle); human && !IsDead(human)) return false;
    return true;
}

}  // namespace

void StartConvoys() {
    if (!rfa::IniInt(L"GuerrillaVehicles", L"Convoys", 1)) { rfa::LogDetail("Convoys: off ([GuerrillaVehicles] Convoys=0)"); return; }
    g_waitAfterLoad = (std::max)(0, rfa::IniInt(L"GuerrillaVehicles", L"ConvoyWaitAfterLoad", 600)) * 1000;
    if (!ReadConvoys()) { rfa::Log("Convoys: no mods\\guerrilla_vehicles\\convoys.txt (build the vehicles); no convoys"); return; }
    rfa::LogDetail("Convoys: Guerrilla's %u, the first no sooner than %d s in the world", static_cast<unsigned>(g_definitions.size()), g_waitAfterLoad / 1000);
}

void ConvoysFrame() {
    if (g_definitions.empty()) return;
    if (!InTrafficWorld()) {
        g_convoys.clear();
        g_inWorld = false;
        return;
    }
    BYTE* player = Player();
    if (!player) return;
    const int clock = GameTime();
    if (!g_inWorld) {
        g_inWorld = true;
        g_enteredWorld = clock;
        g_nextTry = 0;
        for (Definition& def : g_definitions) def.nextAllowed = 0;
    }
    for (auto convoy = g_convoys.begin(); convoy != g_convoys.end();) {
        if (ConvoyFrame(*convoy, player, clock)) ++convoy;
        else {
            Retire(*convoy);
            convoy = g_convoys.erase(convoy);
        }
    }
    // The campaign's convoy activity sends them while it runs (its natural spawner).
    if (GuerrillaCampaign_RunsConvoys()) return;
    if (!g_convoys.empty() || clock - g_enteredWorld < g_waitAfterLoad || clock < g_nextTry) return;
    g_nextTry = clock + RandomBetween(4000, 6000);
    TryStart(player, clock);
}

}  // namespace gv

using namespace gv;

bool GuerrillaVehicles_PlayerInSafehouse() { return guerrilla_vehicles::Running() && PlayerInSafehouse(); }

bool GuerrillaVehicles_ConvoySpawn(const char* name) {
    if (!guerrilla_vehicles::Running() || !name || !InTrafficWorld()) return false;
    // One out at a time; a finished activity convoy still kept (its end not told yet) makes room.
    for (auto convoy = g_convoys.begin(); convoy != g_convoys.end();)
        if (convoy->forActivity && convoy->ended) {
            Retire(*convoy);
            convoy = g_convoys.erase(convoy);
        } else {
            ++convoy;
        }
    if (!g_convoys.empty()) {
        rfa::Log("Convoys: %s can't set off: %s is out", name, g_convoys.front().def->name.c_str());
        return false;
    }
    for (Definition& def : g_definitions)
        if (_stricmp(def.name.c_str(), name) == 0) {
            SetOff(def, GameTime(), true);
            rfa::LogDetail("Convoys: %s sets off for the convoy activity (%s, %u vehicles, from %s)", def.name.c_str(), def.capture ? "capture" : "destroy",
                           static_cast<unsigned>(def.members.size()), def.district.c_str());
            return true;
        }
    rfa::Log("Convoys: no convoy %s in mods\\guerrilla_vehicles\\convoys.txt (build the vehicles again)", name);
    return false;
}

bool GuerrillaVehicles_ConvoyStatus(GuerrillaVehiclesConvoyState* state, GuerrillaVehiclesConvoyMember* members, int most) {
    if (!state || state->size < static_cast<int>(sizeof(GuerrillaVehiclesConvoyState))) return false;
    const Convoy* convoy = guerrilla_vehicles::Running() ? ActivityConvoy() : nullptr;
    if (!convoy) return false;
    const Definition& def = *convoy->def;
    state->members = static_cast<int>(convoy->members.size());
    state->simulated = convoy->spawned ? 0 : 1;
    state->routePoints = static_cast<int>(def.route.size());
    int next = convoy->next;
    if (!convoy->spawned) {  // a point: the route point after the one it passed
        PointAt(def, convoy->ghost, nullptr, &next);
        ++next;
    }
    state->nextPoint = convoy->atDestination ? state->routePoints : next;
    state->atDestination = convoy->atDestination ? 1 : 0;
    state->attacked = convoy->attacked ? 1 : 0;
    for (int i = 0; i < state->members && i < most && members; ++i) {
        const Member& member = convoy->members[i];
        GuerrillaVehiclesConvoyMember& out = members[i];
        out = GuerrillaVehiclesConvoyMember{};
        out.role = static_cast<int>(member.role);
        out.vehicle = member.vehicle;
        BYTE* vehicle = member.vehicle != kNone ? VehicleByHandle(kWorld, nullptr, member.vehicle) : nullptr;
        out.vehicleKilled = member.vehicle != kNone && (!vehicle || IsDead(vehicle)) ? 1 : 0;
        out.squadKilled = AllLost(member.crew) ? 1 : 0;
        out.leftConvoy = member.left ? 1 : 0;
        out.driverless = member.driverless ? 1 : 0;
        out.noticedPlayer = !member.crew.people.empty() && NoticedPlayer(member.crew) ? 1 : 0;
        out.damage = vehicle ? Damage(vehicle) : 0.0f;
        out.speed = vehicle && !member.gone ? member.crew.speed : 0.0f;
        const V3 where = vehicle ? Pos(vehicle) : PointAt(def, convoy->ghost - kSpacing * i);
        out.x = where.x;
        out.y = where.y;
        out.z = where.z;
    }
    return true;
}

void GuerrillaVehicles_ConvoyAttacked() {
    if (Convoy* convoy = guerrilla_vehicles::Running() ? ActivityConvoy() : nullptr; convoy && !convoy->attacked) {
        convoy->attacked = true;
        RFA_LOG_DETAIL_FIRST(40, "Convoys: %s is attacked (its activity runs)", convoy->def->name.c_str());
    }
}

void GuerrillaVehicles_ConvoyAllAttack() {
    if (Convoy* convoy = guerrilla_vehicles::Running() ? ActivityConvoy() : nullptr)
        for (Member& member : convoy->members) member.role = Role::Attack;
}

void GuerrillaVehicles_ConvoyReleaseAttackers() {
    Convoy* convoy = guerrilla_vehicles::Running() ? ActivityConvoy() : nullptr;
    BYTE* player = Player();
    if (!convoy || !player || convoy->ended) return;
    for (Member& member : convoy->members) {
        if (member.role != Role::Attack || member.left || member.gone || member.crew.vehicle == kNone) continue;
        if (!VehicleByHandle(kWorld, nullptr, member.crew.vehicle)) continue;
        member.left = true;  // leave_convoy_formation, then ATTACK orders on the player
        member.chase = Chase{};
        member.chase.target = Handle(player);
    }
}

void GuerrillaVehicles_ConvoyTargetMayExit(bool may) {
    if (Convoy* convoy = guerrilla_vehicles::Running() ? ActivityConvoy() : nullptr) convoy->targetMayExit = may;
}

void GuerrillaVehicles_ConvoyDelivered(unsigned long vehicleHandle) {
    if (!guerrilla_vehicles::Running()) return;
    BYTE* vehicle = VehicleByHandle(kWorld, nullptr, vehicleHandle);
    if (!vehicle) return;
    if (BYTE* player = Player(); player && VehicleOf(player) == vehicleHandle) {
        BYTE exit[4] = {};  // vi_exit_data: nothing asked
        RequestExit(player, exit);
    }
    Flags2(vehicle) |= kLocked;
    StopCar(vehicle);  // vehicle_set_controls_when_abandoned (vehicle_reposition_on_ground: Armageddon's not found)
}

void GuerrillaVehicles_ConvoyRemove() {
    if (!guerrilla_vehicles::Running()) return;
    for (auto convoy = g_convoys.begin(); convoy != g_convoys.end();) {
        if (!convoy->forActivity) {
            ++convoy;
            continue;
        }
        // object_convoy::free_this: simulated squads destroyed (here: a point has nothing made), the others stay as the
        // game's own squads (persistent no more): given back to the game with their vehicles.
        for (Member& member : convoy->members) {
            member.spawning = false;
            if (member.gone || member.crew.vehicle == kNone) continue;
            if (BYTE* vehicle = VehicleByHandle(kWorld, nullptr, member.crew.vehicle)) HandBack(member.crew, vehicle);
            else RetireWhenUnseen(member.crew);
            member.gone = true;
        }
        convoy = g_convoys.erase(convoy);
    }
}

int GuerrillaVehicles_ConvoyRoute(float* xz, int most) {
    const Convoy* convoy = guerrilla_vehicles::Running() && xz ? ActivityConvoy() : nullptr;
    if (!convoy || most <= 0) return 0;
    const std::vector<V3>& route = convoy->def->route;
    const int count = (std::min)(static_cast<int>(route.size()), most);
    for (int i = 0; i < count; ++i) {
        xz[i * 2] = route[i].x;
        xz[i * 2 + 1] = route[i].z;
    }
    return count;
}

void GuerrillaVehicles_ConvoyForget() {
    // A reload restarts neither the world's name nor the clock, so ConvoysFrame doesn't see it: the old level's
    // handles may name other objects now, so none is looked up.
    for (auto convoy = g_convoys.begin(); convoy != g_convoys.end();)
        if (convoy->forActivity) {
            rfa::LogDetail("Convoys: %s let go (the level started again)", convoy->def->name.c_str());
            convoy = g_convoys.erase(convoy);
        } else {
            ++convoy;
        }
}
