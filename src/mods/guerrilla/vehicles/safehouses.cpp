// Guerrilla's safehouses' parked vehicles: a safehouse's spots filled again when the player walks into it, as
// Guerrilla's. A part of guerrilla_vehicles, run each frame from traffic's hook on the spawn nodes' frame.
//
// Guerrilla has no menu to take a car from: each safehouse (object_safehouse) owns vehicle spawn nodes, the spots
// where its cars stand (up to 4; its zone attaches them to it), and an area (a trigger_region, a box or a
// sphere). Each frame (safehouse_do_frame_postsim, RM 0xA93B40) it finds the enabled safehouse whose area holds
// the player; on the frame he comes into one from outside any, that safehouse's spots are filled again
// (object_safehouse::respawn_vehicles, RM 0xA93AF0), each by object_safehouse_try_vehicle_spawn (RM 0xA931C0):
//   - its car still within sqrt(4000) = 63 m of its spot (Safehouse_respawn_vehicle_dist_squared): nothing;
//   - its car farther, and the player within 63 m of it: nothing (he's using it or near it);
//   - its car farther and the player not near it: that car is taken away (world::destroy_object) and a new one
//     made at the spot;
//   - no car, or a wreck: a new one made at the spot, unless a vehicle or the player is within 5 m of it.
// A new car is the node's own spawn (spawn_child_object): one of the node's group, empty. Between visits the spots
// work as every other node's (both games make a node's car as the player comes within 250 m). A car whose model
// isn't streamed in yet is asked for and made by the game's spawn step as soon as it is (see PickFromGroup in
// guerrilla_vehicles.cpp): "waiting" in the log.
//
// Armageddon has no safehouse class, and the world build places Guerrilla's nodes without their parent link, so
// GuerrillaBuilder.exe writes which spots are whose and each safehouse's area: mods\guerrilla_vehicles\
// safehouses.txt. The game's node for a spot is the one standing on it (within half a metre: the world build
// keeps Guerrilla's positions).
//
// Guerrilla also keeps its ambient people off a safehouse's spots (object_vehicle_spawn_node::allow_ambient_action,
// RM 0xA3E790: no ambient driver parks there, no ambient walker takes its car). Those ambient actions are part of
// the population layer Armageddon deleted and the mods haven't ported, so nothing takes or parks at a spot yet;
// whatever ports them must skip these spots.
//
// One difference, for safety: a car of a spot with someone aboard is never taken away (Guerrilla's would be).
#define RFA_MOD_NAME "guerrilla_vehicles"
#include "rfa_mod.h"
#include "game.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace gv {

namespace {

constexpr float kRespawnDistanceSquared = 4000.0f;  // Safehouse_respawn_vehicle_dist_squared (RM 0x1694614)
constexpr float kClear = 5.0f;                      // nobody and no vehicle this near a spot for a new car
constexpr float kOnSpot = 0.5f;                     // a node this near a spot is that spot's

// The world's vehicle spawn nodes and vehicles (world::get_num_objects / get_object, thiscall on the world), a
// node's own spawn (thiscall, the vehicle to make or -1 for one of its group: the car's handle, or the old one
// when none was made) and the world's destroy (thiscall: the object, false as Guerrilla's).
auto const NodeCount = reinterpret_cast<int(__fastcall*)(void* world, void* unused)>(0x804C00);
auto const NodeAt = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int index)>(0x804C50);
auto const VehicleCount = reinterpret_cast<int(__fastcall*)(void* world, void* unused)>(0x477090);
auto const VehicleAt = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int index)>(0x4770E0);
auto const SpawnChild = reinterpret_cast<DWORD(__fastcall*)(BYTE* node, void* unused, DWORD vehicle)>(0x7FE5A0);
auto const DestroyObject = reinterpret_cast<bool(__fastcall*)(void* world, void* unused, BYTE* object, bool flag)>(0x6D5450);
auto const NodeHeldBack = reinterpret_cast<bool(__fastcall*)(BYTE* node, void* unused)>(0x801400);  // true: the game makes nothing there now
constexpr DWORD kNodeVtable = 0xFC0E64;  // object_vehicle_spawn_node's
constexpr DWORD kNodeCar = 0xA8;         // a node: its car's handle
constexpr DWORD kNodeFlags = 0xAC;       // a node: its flags (bit 1: spawn again at the next step, set when the model isn't streamed in yet)

struct Area {
    bool sphere = false;
    V3 at{}, right{}, up{}, forward{}, min{}, max{};
    float radius = 0.0f;
};
struct Spot {
    V3 at{};
    std::string group;
};
struct House {
    std::string handle, name;
    bool enabled = true;
    std::vector<Area> areas;
    std::vector<Spot> spots;
};
std::vector<House> g_houses;
int g_inside = -1;  // the safehouse the player was in at the last step, -1 none (Guerrilla's 0x1694678)

V3 Minus(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float DistanceSquared(const V3& a, const V3& b) {
    const V3 d = Minus(a, b);
    return Dot(d, d);
}

bool IsDead(BYTE* object) {  // the object's own answer (its fifth virtual function)
    using Fn = bool(__fastcall*)(BYTE* self, void* unused);
    return (*reinterpret_cast<Fn**>(object))[4](object, nullptr);
}

// Guerrilla's in_safehouse_area (RM 0xA3E490): any of its regions holds the point (a box: in its own frame,
// between its corners; a sphere: within its radius).
bool InArea(const House& house, const V3& point) {
    for (const Area& area : house.areas) {
        const V3 d = Minus(point, area.at);
        if (area.sphere) {
            if (Dot(d, d) <= area.radius * area.radius) return true;
            continue;
        }
        const float x = Dot(d, area.right), y = Dot(d, area.up), z = Dot(d, area.forward);
        if (x >= area.min.x && x <= area.max.x && y >= area.min.y && y <= area.max.y && z >= area.min.z && z <= area.max.z) return true;
    }
    return false;
}

// The game's node standing on a spot, or null (its zone not streamed in).
BYTE* NodeOn(const V3& spot) {
    const int count = NodeCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* node = NodeAt(kWorld, nullptr, i);
        if (node && *reinterpret_cast<DWORD*>(node) == kNodeVtable && DistanceSquared(Pos(node), spot) <= kOnSpot * kOnSpot) return node;
    }
    return nullptr;
}

bool Aboard(BYTE* vehicle) {
    const int seats = Seats(vehicle);
    for (int seat = 0; seat < seats; ++seat)
        if (SeatOccupant(vehicle, seat)) return true;
    return false;
}

// Whether a vehicle or the player is within 5 m of a spot (Guerrilla's world::collect_objects of each).
bool Blocked(const V3& spot, BYTE* player) {
    if (player && DistanceSquared(Pos(player), spot) <= kClear * kClear) return true;
    const int count = VehicleCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i)
        if (BYTE* vehicle = VehicleAt(kWorld, nullptr, i); vehicle && DistanceSquared(Pos(vehicle), spot) <= kClear * kClear) return true;
    return false;
}

enum class Outcome { Kept, LeftWithPlayer, LeftAboard, Blocked, Made, Waiting, HeldBack, NotMade, NoNode, Count };

// Guerrilla's object_safehouse_try_vehicle_spawn (RM 0xA931C0) for one spot.
Outcome Refill(BYTE* node, BYTE* player) {
    const V3 spot = Pos(node);
    BYTE* car = VehicleByHandle(kWorld, nullptr, *reinterpret_cast<const DWORD*>(node + kNodeCar));
    if (car && !IsDead(car)) {
        if (DistanceSquared(spot, Pos(car)) <= kRespawnDistanceSquared) return Outcome::Kept;
        if (player && DistanceSquared(Pos(car), Pos(player)) <= kRespawnDistanceSquared) return Outcome::LeftWithPlayer;
        if (Aboard(car)) return Outcome::LeftAboard;
        DestroyObject(kWorld, nullptr, car, false);
    }
    if (Blocked(spot, player)) return Outcome::Blocked;
    if (NodeHeldBack(node, nullptr)) return Outcome::HeldBack;
    const DWORD made = SpawnChild(node, nullptr, kNone);
    if (made != kNone && VehicleByHandle(kWorld, nullptr, made)) {
        NoteMadeBy(made, "a safehouse refill");
        return Outcome::Made;
    }
    return (*reinterpret_cast<const DWORD*>(node + kNodeFlags) & 2) ? Outcome::Waiting : Outcome::NotMade;
}

void RefillAll(const House& house, BYTE* player) {
    int counts[static_cast<int>(Outcome::Count)] = {};
    for (const Spot& spot : house.spots) {
        BYTE* node = NodeOn(spot.at);
        ++counts[static_cast<int>(node ? Refill(node, player) : Outcome::NoNode)];
    }
    auto n = [&counts](Outcome outcome) { return counts[static_cast<int>(outcome)]; };
    RFA_LOG_DETAIL_FIRST(100, "Safehouses: the player came into %s (%s): its %zu spots: %d made, %d waiting for their model to stream in (made as soon as it has), "
                  "%d kept (its car still there), %d left (the player near the car), %d left (someone aboard), %d blocked (a vehicle or the player within 5 m), "
                  "%d held back by the game (the node not streamed in, or node spawning off), %d refused by the game's spawn, %d not loaded",
                  house.name.c_str(), house.handle.c_str(), house.spots.size(), n(Outcome::Made), n(Outcome::Waiting), n(Outcome::Kept), n(Outcome::LeftWithPlayer),
                  n(Outcome::LeftAboard), n(Outcome::Blocked), n(Outcome::HeldBack), n(Outcome::NotMade), n(Outcome::NoNode));
}

V3 Read3(std::istringstream& in) {
    V3 v{};
    in >> v.x >> v.y >> v.z;
    return v;
}

void ReadHouses() {
    std::ifstream file(rfa::RootDir() + L"\\mods\\guerrilla_vehicles\\safehouses.txt");
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream in(line);
        std::string word;
        if (!(in >> word) || word[0] == '#') continue;
        if (word == "safehouse") {
            House house;
            int enabled = 1;
            in >> house.handle >> house.name >> enabled;
            house.enabled = enabled != 0;
            g_houses.push_back(house);
        } else if (word == "area" && !g_houses.empty()) {
            std::string shape;
            in >> shape;
            Area area;
            area.at = Read3(in);
            if (shape == "sphere") {
                area.sphere = true;
                in >> area.radius;
            } else {
                area.right = Read3(in);
                area.up = Read3(in);
                area.forward = Read3(in);
                area.min = Read3(in);
                area.max = Read3(in);
            }
            if (in) g_houses.back().areas.push_back(area);
        } else if (word == "node" && !g_houses.empty()) {
            Spot spot;
            spot.at = Read3(in);
            in >> spot.group;
            if (in) g_houses.back().spots.push_back(spot);
        }
    }
}

}  // namespace

void StartSafehouses() {
    ReadHouses();
    size_t spots = 0;
    for (const House& house : g_houses) spots += house.spots.size();
    if (g_houses.empty())
        rfa::Log("Safehouses: no mods\\guerrilla_vehicles\\safehouses.txt (build the vehicles); a safehouse's spots aren't filled again");
    else
        rfa::LogDetail("Safehouses: %zu of Guerrilla's with %zu parked vehicles' spots; a safehouse's spots are filled again as the player walks into it, as Guerrilla's",
                 g_houses.size(), spots);
}

// Whether the player is in a safehouse's area (safehouse_contains_player), as of the last step.
bool PlayerInSafehouse() { return g_inside >= 0; }

// Guerrilla's safehouse_do_frame_postsim, its part for the parked vehicles. Each frame, in the traffic's worlds.
void SafehousesFrame() {
    if (g_houses.empty()) return;
    const bool wasInside = g_inside >= 0;
    g_inside = -1;
    BYTE* player = InTrafficWorld() ? Player() : nullptr;
    if (!player) return;
    const V3 at = Pos(player);
    for (int i = 0; i < static_cast<int>(g_houses.size()); ++i)
        if (g_houses[i].enabled && InArea(g_houses[i], at)) g_inside = i;
    if (g_inside >= 0 && !wasInside) RefillAll(g_houses[g_inside], player);
}

}  // namespace gv
