// Guerrilla's EDF reinforcements (guerrilla_reinforcement_do_frame RM 0x7B9640: Orange_reinforcements and
// Red_reinforcements, two reinforcement_allocations), ported onto Armageddon's own squad-making.
//
// Guerrilla, for each allocation (do_frame 0x7B9620, do_frame_active 0x7B94A0, do_frame_inactive 0x7B6F20):
//   - It is active while the alert is at its level or above (orange, red), so at red both send squads.
//   - With no profile, or with all of its squads gone and its timer run out, it picks a profile (pick_new_profile
//     0x79AE90 -> 0x79A990) from reinforcement_profile.xtbl:
//       - the profiles of its alert level at the EDF progression level (edf_progression_level_internal: the
//         player's district's min_progression_edf, or the story's level), else one level lower, and so on;
//       - of those, the ones of the kind the player calls for (pick_desired_reinforcement_group 0x771250): In
//         Vehicle while he's in a vehicle, Flyer while he's more than 15 m from the nav mesh, else On Foot;
//       - one of them at random.
//     Its timer is the profile's spawn_delay, counted from the moment its last squad is gone.
//   - It spawns each of the profile's squads (do_frame_spawn 0x7B9420, do_spawn 0x7B9380, spawn_squad 0x7B6FC0):
//     the squad_def (casual_squad_def on Easy) in its vehicle_group if it has one, with orders of type attack on
//     the player (his handle and place).
//       - On foot: at an EDF squad spawn node 80 to 160 m from the player, else on a road, else on a nav cell.
//       - In a vehicle: on a road 150 to 250 m away, else on a nav cell.
//   - Below its level, its squads go (do_frame_inactive: each squad's do_frame until it despawns), and it waits.
//
// Here:
//   - The places as Guerrilla's (master_spawn_info: build_node_info 0x787320, build_cell_info 0x787430):
//       - on foot, an EDF squad node 80 to 160 m from the player, at random among those loaded (the world build lists
//         them: mods\guerrilla_world\reinforcement_nodes.txt), else the nearest road point in the band, else a nav cell;
//       - in a vehicle, the road point, else a nav cell;
//       - a nav cell: four probes at the band's middle (120 m on foot, 200 m in vehicles) north, east, south and west of
//         the player, the first open cell within 20 m of one (the people's mesh, or the vehicles' for a squad with
//         vehicles), its centroid (guerrilla_people's GuerrillaPeople_OpenNavCell).
//   - The Flyer kind as Guerrilla's: the player 15 m or more from the people's nav mesh's nearest border edge, or none
//     loaded (guerrilla_people's GuerrillaPeople_NavMeshDistance: bb_pf_find_closest_point_on_nav_mesh_slow); it wins
//     over In Vehicle.
//   - A squad is made with Armageddon's squad spawn (the drop_squad record, 0x507950 / 0x5080A0: its vehicle group
//     at +0x38 from 0x757340 by name, the orders at +0x98 Attack 3 on the player).
//   - Its members on foot who don't see the player are sent running to where he is every 4 s, as the alert orders'
//     squads are.
//   - Guerrilla keeps under its people and vehicles limits with its spawn-resource slots; here no new squad comes
//     while the world holds 80 people or more.
//   - A squad is taken away as Guerrilla's (do_frame_spawned 0x79B400): farther than 325 m from the player, or neither
//     fighting him (one of it has him in sight) nor coming closer once its timer has run out (20 s from its spawn, kept
//     at 10 s or more while it fights or closes in). Below its level, all its squads go.
//   - A squad taken away: its people on foot walk away and go out of view (the converts' SendAway); its vehicle, and
//     its people still in it, are taken away once nobody sees them.
//   - The squads' own making, keeping and letting go are Guerrilla's reinforcement_squad, shared with the continuous
//     attacks: attack_squads.cpp. Here: the profiles, the places, the caps.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "people/people_api.h"
#include "reinforcements.h"
#include "activities.h"
#include "alert_sources.h"
#include "attack_squads.h"
#include "handle_kinds.h"
#include "liberation.h"
#include "tables.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace campaign::reinforcements {
namespace {

void* const kWorld = reinterpret_cast<void*>(0x1870630);
using CountFn = int(__fastcall*)(void* world, void* unused);
auto const HumanCount = reinterpret_cast<CountFn>(0x43A860);
constexpr DWORD kHandle = 0x68, kInVehicle = 0x1160;
constexpr DWORD kNone = 0xFFFFFFFF;
DWORD HandleOf(const BYTE* object) { return *reinterpret_cast<const DWORD*>(object + kHandle); }
using attack_squads::Squad;

auto const ClosestRoad = reinterpret_cast<BYTE*(__cdecl*)(const float* where, bool special, float within, bool unknown, bool gpsOnly)>(0x7F6E10);
constexpr DWORD kRoadPointCount = 0x98, kRoadPoints = 0x9C, kRoadPointSize = 28;
auto& g_difficulty = *reinterpret_cast<const int*>(0x10F7E98);  // 0 Easy

constexpr float kFootNear = 80.0f, kFootFar = 160.0f;          // Master_ped_spawn_info
constexpr float kFlyerOffMesh = 15.0f;                          // pick_desired_reinforcement_group: dist^2 >= 225
constexpr float kVehicleNear = 150.0f, kVehicleFar = 250.0f;   // Master_veh_spawn_info
constexpr float kFlyerHeight = 30.0f;                         // a flyer starts this far above its road point
constexpr int kPeriodMs = 500;
constexpr int kChaseAgainMs = 4000;
constexpr int kSpawnTriesMs = 20000;  // a squad that can't be made (its vehicle's model not in) is given up after this
constexpr int kMostPeople = 80;       // no new squad while the world holds this many people

// ---- reinforcement_profile.xtbl

enum Kind : unsigned { kKindOnFoot = 1, kKindInVehicle = 2, kKindFlyer = 4 };
struct SquadInfo {
    std::string squad, casual, group;
};
struct Profile {
    std::string name;
    int alert = 0;        // handles::AlertOrange / AlertRed
    int progression = 0;
    unsigned kinds = 0;
    std::vector<SquadInfo> squads;
    int delayMs = 0;
};
std::vector<Profile> g_profiles;

void LoadProfiles() {
    std::vector<tables::Element> rows;
    if (!tables::Load("reinforcement_profile", rows)) return;
    for (const tables::Element& row : rows) {
        Profile p;
        p.name = row.Text("Name");
        const std::string alert = row.Text("alert_level");
        p.alert = _stricmp(alert.c_str(), "Red") == 0 ? handles::AlertRed : _stricmp(alert.c_str(), "Orange") == 0 ? handles::AlertOrange : -1;
        if (p.name.empty() || p.alert < 0) continue;
        p.progression = row.Int("progression_level");
        if (const tables::Element* types = row.Child("reinforcement_type"))
            for (const tables::Element& flag : types->children) {
                if (_stricmp(flag.text.c_str(), "On Foot") == 0) p.kinds |= kKindOnFoot;
                else if (_stricmp(flag.text.c_str(), "In Vehicle") == 0) p.kinds |= kKindInVehicle;
                else if (_stricmp(flag.text.c_str(), "Flyer") == 0) p.kinds |= kKindFlyer;
            }
        if (const tables::Element* list = row.Child("squad_list"))
            for (const tables::Element& info : list->children)
                if (!info.Text("squad_def").empty()) p.squads.push_back({info.Text("squad_def"), info.Text("casual_squad_def"), info.Text("vehicle_group")});
        p.delayMs = static_cast<int>(row.Number("spawn_delay", 20.0f) * 1000.0f);
        if (!p.squads.empty()) g_profiles.push_back(p);
    }
}

// pick_new_profile: the allocation's level at the progression (else one lower, ...), the kind wanted, one at random.
const Profile* PickProfile(int alert, int progression, unsigned kind) {
    for (int level = progression; level >= 0; --level) {
        std::vector<const Profile*> atLevel, ofKind;
        for (const Profile& p : g_profiles)
            if (p.alert == alert && p.progression == level) atLevel.push_back(&p);
        if (atLevel.empty()) continue;
        for (const Profile* p : atLevel)
            if (p->kinds & kind) ofKind.push_back(p);
        return ofKind.empty() ? nullptr : ofKind[rand() % ofKind.size()];
    }
    return nullptr;
}

// ---- The squads sent (attack_squads' Squad: reinforcement_squad)

struct Allocation {
    int level;
    const char* name;
    const Profile* profile = nullptr;
    size_t next = 0;      // the profile's next squad to make
    int tryingSince = -1; // the game's clock when the next one was first tried
    int readyAt = 0;      // the game's clock when another profile may be picked
    bool active = false;
    int chasedAt = 0;
    std::vector<Squad> squads;
};
Allocation g_allocations[] = {{handles::AlertOrange, "orange"}, {handles::AlertRed, "red"}};

bool g_on = true;
int g_next = 0;

float Distance2(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// The EDF's squad nodes as placed (the world build's reinforcement_nodes.txt).
struct Node {
    DWORD handle;
    Vector at;
};
std::vector<Node> g_nodes;

void ReadNodes() {
    FILE* file = nullptr;
    if (_wfopen_s(&file, (rfa::RootDir() + L"\\mods\\guerrilla_world\\reinforcement_nodes.txt").c_str(), L"r") != 0 || !file) {
        rfa::Log("Reinforcements: no mods\\guerrilla_world\\reinforcement_nodes.txt (the world built before it); squads on foot come on roads or nav cells");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        Node node{};
        if (sscanf_s(line, "node %lx %f %f %f", &node.handle, &node.at.x, &node.at.y, &node.at.z) == 4) g_nodes.push_back(node);
    }
    fclose(file);
    rfa::LogDetail("Reinforcements: %u of the EDF's squad nodes as places for squads on foot", static_cast<unsigned>(g_nodes.size()));
}

// A random loaded EDF squad node between the two distances (build_node_info: no sight test, uniform), or false.
bool NodePlace(const Vector& here, float nearest, float farthest, Vector& place) {
    std::vector<Vector> places;
    for (const Node& node : g_nodes) {
        const void* object = ObjectFromHandle(node.handle);
        if (!object) continue;
        const Vector at = PositionOf(object);
        const float d = Distance2(at, here);
        if (d >= nearest * nearest && d <= farthest * farthest) places.push_back(at);
    }
    if (places.empty()) return false;
    place = places[rand() % places.size()];
    return true;
}

// build_cell_info: four probes at the band's middle along +Z, +X, -Z, -X; the first open cell within 20 m of one.
bool CellPlace(const Vector& here, float nearest, float farthest, bool vehicles, Vector& place) {
    if (!guerrilla_people::Running()) return false;
    const float middle = (nearest + farthest) / 2.0f;
    const float directions[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
    for (const auto& d : directions) {
        float out[3];
        if (GuerrillaPeople_OpenNavCell(here.x + d[0] * middle, here.y, here.z + d[1] * middle, 20.0f, vehicles ? 1 : 0, out)) {
            place = {out[0], out[1], out[2]};
            return true;
        }
    }
    return false;
}

// The road point nearest the player between the two distances, or false.
bool RoadPlace(const Vector& here, float nearest, float farthest, Vector& place) {
    float best = 1e30f;
    for (int angle = 0; angle < 16; ++angle)
        for (float distance = nearest; distance <= farthest; distance += 20.0f) {
            const float a = angle * 3.14159265f / 8.0f;
            const Vector probe{here.x + cosf(a) * distance, here.y, here.z + sinf(a) * distance};
            BYTE* road = ClosestRoad(&probe.x, false, 60.0f, false, false);
            if (!road) continue;
            const int points = *reinterpret_cast<const int*>(road + kRoadPointCount);
            const BYTE* point = *reinterpret_cast<BYTE* const*>(road + kRoadPoints);
            if (points <= 0 || !point) continue;
            for (int i = 0; i < points; ++i) {
                const Vector at = *reinterpret_cast<const Vector*>(point + i * kRoadPointSize);
                const float d = Distance2(at, here);
                if (d <= nearest * nearest || d >= farthest * farthest || d >= best) continue;
                best = d;
                place = at;
            }
        }
    return best < 1e30f;
}

// One of the profile's squads made: true when made, or when it can't be and is given up.
bool SpawnOne(Allocation& a, BYTE* player, const SquadInfo& info) {
    const std::string name = g_difficulty == 0 && !info.casual.empty() ? info.casual : info.squad;
    const Vector here = PositionOf(player);
    Vector place{};
    const bool inVehicle = attack_squads::GroupExists(info.group);
    const float nearest = inVehicle ? kVehicleNear : kFootNear, farthest = inVehicle ? kVehicleFar : kFootFar;
    const char* where = "a squad node";
    if (inVehicle || !NodePlace(here, nearest, farthest, place)) {
        where = "a road";
        if (!RoadPlace(here, nearest, farthest, place)) {
            where = "a nav cell";
            if (!CellPlace(here, nearest, farthest, inVehicle, place)) {
                RFA_LOG_DETAIL_FIRST(16, "Reinforcements (%s): no squad node, road or nav cell %.0f to %.0f m from the player; %s waits", a.name, nearest, farthest,
                              name.c_str());
                return false;
            }
        }
    }
    RFA_LOG_DETAIL_FIRST(40, "Reinforcements (%s): %s comes from %s", a.name, name.c_str(), where);
    if (inVehicle && attack_squads::IsFlyerGroup(info.group)) place.y += kFlyerHeight;
    // Its members take the Reinforcement EDF set's weapons at the EDF's level (Guerrilla's squad_flags.reinforcement_squad;
    // guerrilla_people's squad levels).
    attack_squads::Order order;
    order.squad = name;
    order.group = inVehicle ? info.group : std::string();
    order.place = place;
    order.target = HandleOf(player);
    order.weaponSet = "Reinforcement EDF";
    order.who = a.name;
    Squad squad;
    std::string why;
    const attack_squads::Made made = attack_squads::Make(order, squad, why);
    if (made == attack_squads::Made::Failed) {
        RFA_LOG_DETAIL_FIRST(16, "Reinforcements (%s): %s; skipped", a.name, why.c_str());
        return true;
    }
    if (made == attack_squads::Made::Wait) {
        const int now = GameTime();
        if (a.tryingSince < 0) a.tryingSince = now;
        if (now - a.tryingSince < kSpawnTriesMs) return false;  // its vehicle's model may still be streaming in
        rfa::LogDetail("Reinforcements (%s): %s at (%.0f, %.0f, %.0f) in %d s; given up", a.name, why.c_str(), place.x, place.y, place.z, kSpawnTriesMs / 1000);
        return true;
    }
    a.squads.push_back(squad);
    rfa::LogDetail("Reinforcements (%s, %s): %s from (%.0f, %.0f, %.0f), %.0f m away, attacking the player", a.name, a.profile->name.c_str(), squad.what.c_str(),
             place.x, place.y, place.z, sqrtf(Distance2(place, here)));
    return true;
}

void Release(Allocation& a, const char* why) {
    if (!a.squads.empty()) rfa::LogDetail("Reinforcements (%s): %u squads let go (%s)", a.name, static_cast<unsigned>(a.squads.size()), why);
    for (Squad& squad : a.squads) attack_squads::LetGo(squad);
    a.squads.clear();
    a.profile = nullptr;
    a.next = 0;
    a.tryingSince = -1;
}

void Chase(Allocation& a, BYTE* player) {
    for (const Squad& squad : a.squads) attack_squads::Chase(squad, player);
}

void FrameAllocation(Allocation& a, BYTE* player, int level, bool allowed, int now) {
    const bool active = allowed && level >= a.level;
    if (!active) {
        if (a.active || !a.squads.empty()) Release(a, level < a.level ? "the alert fell below its level" : "an activity runs");
        a.active = false;
        a.readyAt = 0;
        return;
    }
    a.active = true;
    // reinforcement_squad::do_frame_spawned (attack_squads' Follow): a squad with nobody left is done; one farther than
    // 325 m from the player, or one that neither engages him nor closes on him once its timer has run out, is taken
    // away (and the next wave comes nearer).
    for (size_t i = 0; i < a.squads.size();) {
        Squad& squad = a.squads[i];
        const std::vector<BYTE*> members = attack_squads::Members(squad.squad);
        if (const char* gone = attack_squads::Gone(squad, members, now)) {
            RFA_LOG_DETAIL_FIRST(60, "Reinforcements (%s): %s is done (%s)", a.name, squad.what.c_str(), gone);
            attack_squads::LetGo(squad);  // what's left of it (its vehicle, its squad object) freed
            a.squads.erase(a.squads.begin() + i);
            continue;
        }
        if (members.empty()) {  // its people still to come
            ++i;
            continue;
        }
        if (const char* why = attack_squads::Follow(squad, members, now)) {
            RFA_LOG_DETAIL_FIRST(60, "Reinforcements (%s): %s taken away (%s)", a.name, squad.what.c_str(), why);
            attack_squads::LetGo(squad);
            a.squads.erase(a.squads.begin() + i);
            continue;
        }
        ++i;
    }
    if (a.profile && a.next < a.profile->squads.size()) {
        if (HumanCount(kWorld, nullptr) >= kMostPeople) return;
        if (SpawnOne(a, player, a.profile->squads[a.next])) {
            ++a.next;
            a.tryingSince = -1;
        }
        return;
    }
    if (a.profile && a.squads.empty()) {
        // The wave is over: the next after the profile's spawn_delay.
        a.readyAt = now + a.profile->delayMs;
        rfa::LogDetail("Reinforcements (%s): %s's squads are all gone; another in %d s", a.name, a.profile->name.c_str(), a.profile->delayMs / 1000);
        a.profile = nullptr;
        a.next = 0;
        return;
    }
    if (!a.profile && now >= a.readyAt) {
        // pick_desired_reinforcement_group: Flyer when the player is 15 m or more from the nav mesh (or none is loaded),
        // else In Vehicle while he drives, else On Foot.
        const bool driving = *reinterpret_cast<const DWORD*>(player + kInVehicle) != kNone;
        const Vector at = PositionOf(player);
        const bool people = guerrilla_people::Running();  // without guerrilla_people, no flyers
        const float offMesh = people ? GuerrillaPeople_NavMeshDistance(at.x, at.y, at.z) : 0.0f;
        const bool flyer = people && (offMesh < 0 || offMesh >= kFlyerOffMesh);
        const Kind kind = flyer ? kKindFlyer : driving ? kKindInVehicle : kKindOnFoot;
        const char* why = flyer ? (offMesh < 0 ? "off the nav mesh (none loaded)" : "off the nav mesh") : driving ? "in a vehicle" : "on foot";
        const int progression = liberation::EdfProgressionLevel();
        a.profile = PickProfile(a.level, progression, kind);
        if (!a.profile) {
            RFA_LOG_DETAIL_FIRST(16, "Reinforcements (%s): no profile for progression %d, the player %s", a.name, progression, why);
            a.readyAt = now + 5000;
            return;
        }
        a.next = 0;
        a.tryingSince = -1;
        rfa::LogDetail("Reinforcements (%s): profile %s (progression %d, the player %s%s), %u squads", a.name, a.profile->name.c_str(), progression, why,
                 flyer && offMesh >= 0 ? (" " + std::to_string(static_cast<int>(offMesh)) + " m").c_str() : "",
                 static_cast<unsigned>(a.profile->squads.size()));
    }
    if (now - a.chasedAt >= kChaseAgainMs) {
        a.chasedAt = now;
        Chase(a, player);
    }
}

}  // namespace

void Init() {
    g_on = rfa::IniInt(L"GuerrillaCampaign", L"Reinforcements", 1) != 0;
    LoadProfiles();
    ReadNodes();
    if (!g_on) rfa::LogDetail("Reinforcements: off (Reinforcements=0 in [GuerrillaCampaign])");
    else if (g_profiles.empty()) rfa::Log("Reinforcements: no reinforcement_profile.xtbl; the EDF send no squads");
    else rfa::LogDetail("reinforcement_profile.xtbl: %u profiles; the EDF send squads at orange and red", static_cast<unsigned>(g_profiles.size()));
}

void Enter() {
    for (Allocation& a : g_allocations) {
        a.squads.clear();
        a.profile = nullptr;
        a.next = 0;
        a.tryingSince = -1;
        a.readyAt = 0;
        a.active = false;
    }
    g_next = 0;
}

void Frame() {
    if (!g_on || g_profiles.empty()) return;
    const int now = GameTime();
    if (now < g_next && g_next - now <= kPeriodMs) return;
    g_next = now + kPeriodMs;
    BYTE* player = static_cast<BYTE*>(Player());
    if (!player) return;
    const int level = liberation::AlertLevel();
    const bool allowed = !activities::AnyRunning();  // Guerrilla's test of 0x2229590 and its switch 0x22295CB
    for (Allocation& a : g_allocations) FrameAllocation(a, player, level, allowed, now);
}

}  // namespace campaign::reinforcements
