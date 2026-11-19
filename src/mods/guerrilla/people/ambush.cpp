// Guerrilla's Marauder ambushes on foot (the missing NPCs' piece 2, 2026-11-13; the user: "move onto part 2 of the moral
// stuff/npc's").
//
// In Guerrilla the Marauders in their own lands are an ambush region's (marauder_ambush_region): 136 of its 160 Marauder
// squad nodes hang under its 61 regions in 25 zones from 04_08 to 12_10 (110 of them on foot, 27 riding), and they
// appear only when the player comes in. Armageddon dropped the class. GuerrillaBuilder.exe's world part places the
// on-foot nodes as squad spawn nodes of their own (no parent) and writes each region to
// mods\guerrilla_world\ambush_regions.txt: its place and axes (op), its box in its own axes (bb, TR_PROP_BOX), enabled,
// the day and night chances (MAR_PROP_DAY_PROB / NIGHT_PROB), the least and most squads (MAR_PROP_MIN / MAX_SPAWNS), and
// its nodes. This is Guerrilla's ambush_region_do_frame (RM 0xA91E00) for them:
//   - nothing while an activity or a mission runs (guerrilla_campaign's GuerrillaCampaign_Running, Guerrilla's
//     mission_is_active() || !activity_none()); a region fired then is aborted;
//   - when the player comes into a region's box (outside it the frame before, inside now; ix_point_in_box_oriented
//     on the region's place, axes and box) and the region isn't fired already: the chance (get_ambush_chance RM
//     0xA19E50: the night one only from 22:00 to 1:59, game_clock_get_tod_partitioned's mid night, the day one at
//     every other hour) is rolled, and if it comes up the region fires (trigger_ambush RM 0xA91820): unless the
//     Marauders are the player's friends by then (the disposition of the player's team towards them 3 or more,
//     Current_human_team_dispositions[0][3]) or a guerrilla squad hangs under it, between the least and the most
//     of its squad nodes (no more than it has), picked at random with none twice, each spawned (spawn_child_object);
//   - its squads attack the player (update_squad_orders RM 0xA919D0: ATTACK orders on the player, Guerrilla's
//     gfm_orders type 2). Armageddon's node spawn makes a squad's orders params (type, team, place, object target) from
//     the node's own orders (0x800A50: node +0xC4, default_orders by 0x802830's names, 1-based: Standard 1, Defend 2,
//     Attack 3, Immediate Attack 4, ..., Wait 10; Defend and Attack take the object given, the parent) or, with none,
//     from its parent (0x8007D0). While a region's node is being fired, its params are asked of 0x800A50 as Attack on
//     the player: the game's own Attack orders with the player as their object;
//   - every 1-2 s (squad_update_timer) while fired (update_squads RM 0xA91C90): a squad all dead is forgotten; once
//     the player is more than 400 m from the region's box (check_abort_conditions RM 0xA19E80, ABORT_AMBUSH_DIST_SQR
//     160,000) it is aborted and its squads leave (request_despawn_all_members: each member taken away once out of
//     view); with none left the region can fire again, the next time the player comes in.
// Riding and skirmishes (step 3 of the people's rest, 2026-11-15):
//   - the 27 riding nodes (squad_vehicle on, vehicle_type Guerrilla's Default Marauder group, "rfg_Default Marauder" as
//     the world build names it) are placed and fired as the others; Armageddon's own squad spawn seats them (the
//     node's vehicle_type read by 0x80C7D0 → 0x757340 into the squad's +0x104, then 0x80D410 → 0x80AC90 fills the
//     seats, as Guerrilla's spawn_squad_members → object_squad::spawn_vehicle RM 0xA6E620). Once a crew sits in its
//     vehicle (a member's +0x1160) it is handed to guerrilla_vehicles' chase (GuerrillaVehicles_Chase: Guerrilla's
//     chase_target in a vehicle, the gunner firing), and given back (GuerrillaVehicles_Release) before an abort takes
//     it away; the vehicle goes with its crew, out of view;
//   - skirmishes: Guerrilla's check_teams (RM 0xA67190) marks a region whose squads are of more than one team, and
//     find_skirmish_squads (RM 0xA3CC90) keeps one squad of each (EDF, Marauder, guerrilla). Every update its
//     update_squad_orders (RM 0xA919D0) sends a Marauder squad at the EDF squad, else the guerrilla one; an EDF squad
//     at the Marauders, else the guerrillas; a guerrilla squad at the EDF, else the Marauders: ATTACK on that squad's
//     first member, else on the player. Here the same each update, given with Armageddon's own re-ordering of a node's
//     squad (0x800D00, as the script's af_orders: thiscall on the node, the target, the orders) when the target changes.
// Not here yet: the ambush's voice lines (maybe_play_ambush_voice_line) and the members' alert of the player
// (ai_perception::alert_enemy on each; the attack orders name the player instead).
#define RFA_MOD_NAME "guerrilla_people"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "campaign/campaign_api.h"
#include "vehicles/vehicles_api.h"
#include "ambush.h"
#include "voices.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

// ---- The game's (Armageddon's publicbeta build)
constexpr DWORD kPosition = 0x04, kHandle = 0x68, kNpcAi = 0x174C, kAiSquad = 0x7D4, kTeam = 0x13B4, kInVehicle = 0x1160;
constexpr DWORD kNone = 0xFFFFFFFF;
void* const kWorld = reinterpret_cast<void*>(0x1870630);
auto& g_localPlayer = *reinterpret_cast<BYTE**>(0x2271490);
auto& g_dispositions = *reinterpret_cast<int(*)[5][5]>(0x21DC8D0);  // row the team, column the other (0 the player's side, 3 Marauders)
constexpr int kFriendly = 3;
using WorldObjectFn = BYTE*(__fastcall*)(void* world, void*, DWORD handle);
using CountFn = int(__fastcall*)(void* world, void*);
using AtFn = BYTE*(__fastcall*)(void* world, void*, int index);
auto const WorldObject = reinterpret_cast<WorldObjectFn>(0x6CB9E0);
auto const NpcCount = reinterpret_cast<CountFn>(0x424480);   // world::get_num_objects<npc>
auto const NpcAt = reinterpret_cast<AtFn>(0x4244D0);
auto const GoneOrDead = reinterpret_cast<bool(__cdecl*)(BYTE* object)>(0x42ADC0);  // true for null or a dead one (vfunc +0x10)
auto const InView = reinterpret_cast<bool(__cdecl*)(const float* centre, float radius)>(0x768400);
auto const RemoveObject = reinterpret_cast<int(__fastcall*)(void* world, void*, DWORD handle, int flag)>(0x6D56A0);
// The squad node's spawn (object_squad_spawn_node::spawn_child_object(minimap element, map icon type), thiscall,
// ret 8): through its hook in guerrilla_people.cpp, which holds an ambush node unless it is the one being fired here.
using NodeSpawnFn = DWORD(__fastcall*)(BYTE* node, void*, DWORD element, DWORD iconType);
auto const NodeSpawn = reinterpret_cast<NodeSpawnFn>(0x800040);
// The node's orders params (true when made): type +0x00 (Armageddon's 1-based names), team +0x04, place +0x08, object
// +0x14. From the node's own orders (cdecl: the node, its orders, the params, its parent object), else from its parent
// (cdecl: the node, the params, its parent object).
constexpr DWORD kNodeOwnOrders = 0x800A50, kNodeOrders = 0x8007D0;
constexpr size_t kNodeOwnOrdersPrologue = 6;  // push ebp / mov ebp,esp / sub esp,8
constexpr size_t kNodeOrdersPrologue = 6;     // push ebp / mov ebp,esp / sub esp,10h
using NodeOwnOrdersFn = bool(__cdecl*)(BYTE* node, int orders, BYTE* params, BYTE* parent);
using NodeOrdersFn = bool(__cdecl*)(BYTE* node, BYTE* params, BYTE* parent);
NodeOwnOrdersFn g_nodeOwnOrders = nullptr;
NodeOrdersFn g_nodeOrders = nullptr;
constexpr int kAttackOrders = 3;
constexpr DWORD kOrdersType = 0x00, kOrdersPlace = 0x08, kOrdersObject = 0x14;
// A node's squad given new orders on an object (the script's af_orders 0x94E470 → 0x800D00: thiscall on the node, the
// target object (not null), the orders 1..0x24; ret 8).
using NodeSetOrdersFn = void(__fastcall*)(BYTE* node, void*, BYTE* target, int orders);
auto const NodeSetOrders = reinterpret_cast<NodeSetOrdersFn>(0x800D00);
// guerrilla_vehicles' chase (crews.cpp); without that part the crew sits, as Armageddon's AI has it.
bool g_lookedForVehicles = false;
constexpr int kGuerrillaTeam = 0, kEdfTeam = 1, kMarauderTeam = 3;
int GameTime() { return *reinterpret_cast<const int*>(0x1121300); }   // ms; it stops while the game is paused
int GameHour() { return *reinterpret_cast<const BYTE*>(0x12F76D4); }

// ---- Guerrilla's numbers
constexpr float kAbortDistance = 400.0f;              // sqrt(ABORT_AMBUSH_DIST_SQR 160,000)
constexpr int kUpdateLeast = 1000, kUpdateMost = 2000;  // squad_update_timer.set_rand(1000, 2000)

struct Region {
    DWORD handle = kNone;
    float place[3]{}, axes[9]{}, least[3]{}, most[3]{};  // its box: least..most along its own axes (rows: right, up, forward)
    float worldLeast[3]{}, worldMost[3]{};              // the box's world-axis bounds (Guerrilla's get_bbox, for the abort)
    bool enabled = true, guerrillas = false, marauders = false;
    float day = 0, night = 0;
    int fewest = 1, mostSquads = 1;
    std::vector<DWORD> nodes;     // its squad nodes, as placed
    std::vector<bool> rides;      // each node's squad rides (squad_vehicle)
    struct Squad { DWORD handle = kNone, node = kNone; bool riding = false, handed = false, shouted = false; DWORD vehicle = kNone, target = kNone; };
    std::vector<Squad> squads;    // the squads it made, while fired
    bool known = false, inside = false, fired = false, aborting = false;
    int nextUpdate = 0;
};
std::vector<Region> g_regions;
std::unordered_set<DWORD> g_heldNodes;
const BYTE* g_firing = nullptr;  // the node being fired now (its spawn passes the hold; its orders become the attack)
int g_lastClock = 0, g_fired = 0, g_rolledOut = 0, g_aborted = 0, g_squadsMade = 0, g_handed = 0, g_skirmishOrders = 0;

DWORD HandleOf(const BYTE* object) { return *reinterpret_cast<const DWORD*>(object + kHandle); }
const float* PlaceOf(const BYTE* object) { return reinterpret_cast<const float*>(object + kPosition); }

bool InsideBox(const Region& r, const float* p) {
    const float d[3] = {p[0] - r.place[0], p[1] - r.place[1], p[2] - r.place[2]};
    for (int k = 0; k < 3; ++k) {
        const float along = d[0] * r.axes[3 * k] + d[1] * r.axes[3 * k + 1] + d[2] * r.axes[3 * k + 2];
        if (along < r.least[k] || along > r.most[k]) return false;
    }
    return true;
}

float DistanceToWorldBox(const Region& r, const float* p) {
    float sum = 0;
    for (int k = 0; k < 3; ++k) {
        const float off = p[k] < r.worldLeast[k] ? r.worldLeast[k] - p[k] : p[k] > r.worldMost[k] ? p[k] - r.worldMost[k] : 0;
        sum += off * off;
    }
    return sqrtf(sum);
}

bool CampaignRunning() { return GuerrillaCampaign_Running(nullptr, nullptr) != 0; }

// The squad an NPC is in, or kNone.
DWORD SquadOf(BYTE* human) {
    const BYTE* ai = *reinterpret_cast<BYTE* const*>(human + kNpcAi);
    return ai ? *reinterpret_cast<const DWORD*>(ai + kAiSquad) : kNone;
}

void ReadRegions() {
    std::ifstream file(rfa::RootDir() + L"\\mods\\guerrilla_world\\ambush_regions.txt");
    if (!file) {
        rfa::Log("Ambushes: no mods\\guerrilla_world\\ambush_regions.txt (the world built before it had them): no Marauder ambushes");
        return;
    }
    std::unordered_map<DWORD, size_t> byHandle;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == ';') continue;
        std::istringstream in(line);
        std::string kind;
        in >> kind;
        if (kind == "region") {
            Region r;
            int enabled = 1;
            in >> std::hex >> r.handle >> std::dec;
            for (float& v : r.place) in >> v;
            for (float& v : r.axes) in >> v;
            for (float& v : r.least) in >> v;
            for (float& v : r.most) in >> v;
            in >> enabled >> r.day >> r.night >> r.fewest >> r.mostSquads;
            if (!in) continue;
            r.enabled = enabled != 0;
            // The box's world bounds: its eight corners through its axes.
            for (int k = 0; k < 3; ++k) r.worldLeast[k] = 1e9f, r.worldMost[k] = -1e9f;
            for (int corner = 0; corner < 8; ++corner) {
                const float local[3] = {(corner & 1) ? r.most[0] : r.least[0], (corner & 2) ? r.most[1] : r.least[1], (corner & 4) ? r.most[2] : r.least[2]};
                for (int c = 0; c < 3; ++c) {
                    const float w = r.place[c] + local[0] * r.axes[c] + local[1] * r.axes[3 + c] + local[2] * r.axes[6 + c];
                    r.worldLeast[c] = std::min(r.worldLeast[c], w);
                    r.worldMost[c] = std::max(r.worldMost[c], w);
                }
            }
            byHandle[r.handle] = g_regions.size();
            g_regions.push_back(r);
        }
        else if (kind == "node") {
            DWORD region = kNone, node = kNone;
            std::string squad, riding;
            in >> std::hex >> region >> node >> std::dec >> squad;
            auto at = byHandle.find(region);
            if (!in || at == byHandle.end()) continue;
            in >> riding;
            Region& r = g_regions[at->second];
            r.nodes.push_back(node);
            r.rides.push_back(riding == "riding");
            g_heldNodes.insert(node);
            if (_strnicmp(squad.c_str(), "marauder", 8) == 0) r.marauders = true;
            if (squad.find("_gsq_") != std::string::npos || _strnicmp(squad.c_str(), "guerilla", 8) == 0) r.guerrillas = true;
        }
    }
    int nodes = 0, off = 0, riding = 0;
    for (const Region& r : g_regions) {
        nodes += static_cast<int>(r.nodes.size()), off += r.enabled ? 0 : 1;
        for (bool rides : r.rides) riding += rides ? 1 : 0;
    }
    rfa::Log("Ambushes: %d of Guerrilla's Marauder ambush regions read (%d switched off), %d squad nodes (%d of them riding) held until their region fires",
             static_cast<int>(g_regions.size()), off, nodes, riding);
}

// The attack: as the node being fired gets its orders, they are Guerrilla's ambush orders, the player attacked.
bool __cdecl NodeOwnOrders(BYTE* node, int orders, BYTE* params, BYTE* parent) {
    if (node == g_firing && g_localPlayer) return g_nodeOwnOrders(node, kAttackOrders, params, g_localPlayer);
    return g_nodeOwnOrders(node, orders, params, parent);
}

// A node with no orders of its own (its params from its parent): the same, set after.
bool __cdecl NodeOrders(BYTE* node, BYTE* params, BYTE* parent) {
    const bool made = g_nodeOrders(node, params, parent);
    if (!made || !params || node != g_firing || !g_localPlayer) return made;
    *reinterpret_cast<int*>(params + kOrdersType) = kAttackOrders;
    memcpy(params + kOrdersPlace, PlaceOf(g_localPlayer), 3 * sizeof(float));
    *reinterpret_cast<DWORD*>(params + kOrdersObject) = HandleOf(g_localPlayer);
    return made;
}

void Fire(Region& r, float chance, bool night) {
    if (r.marauders && g_dispositions[0][3] >= kFriendly) {
        RFA_LOG_DETAIL_FIRST(8, "Ambushes: region 0x%08X not fired: the Marauders are the player's friends", r.handle);
        return;
    }
    if (r.guerrillas) return;  // Guerrilla's trigger_ambush makes nothing in a region a guerrilla squad hangs under
    const int count = static_cast<int>(r.nodes.size());
    if (count == 0) return;
    const int fewest = std::min(r.fewest, count), most = std::max(fewest, std::min(r.mostSquads, count));
    const int wanted = fewest + (most > fewest ? std::rand() % (most - fewest + 1) : 0);
    std::vector<int> order(count);
    for (int i = 0; i < count; ++i) order[i] = i;
    for (int i = count - 1; i > 0; --i) std::swap(order[i], order[std::rand() % (i + 1)]);
    int made = 0;
    for (int k = 0; k < wanted; ++k) {
        BYTE* node = WorldObject(kWorld, nullptr, r.nodes[order[k]]);
        if (!node) continue;  // its zone isn't streamed in
        g_firing = node;
        const DWORD squad = NodeSpawn(node, nullptr, kNone, 0);
        g_firing = nullptr;
        if (squad == kNone) continue;
        Region::Squad fired;
        fired.handle = squad;
        fired.node = r.nodes[order[k]];
        fired.riding = r.rides[order[k]];
        fired.target = HandleOf(g_localPlayer);  // the node's orders: Attack on the player
        r.squads.push_back(fired);
        ++made;
    }
    ++g_fired;
    g_squadsMade += made;
    r.fired = true;
    r.aborting = false;
    r.nextUpdate = GameTime() + kUpdateLeast + std::rand() % (kUpdateMost - kUpdateLeast + 1);
    const float* p = PlaceOf(g_localPlayer);
    rfa::LogDetail("Ambushes: region 0x%08X fired (the player at (%.0f, %.0f, %.0f); its %s chance %.0f %%): %d of its %d squad nodes picked (%d to %d), %d squad(s) made",
             r.handle, p[0], p[1], p[2], night ? "night" : "day", chance * 100, wanted, count, fewest, most, made);
}

// Every 1-2 s while fired: Guerrilla's update_squads and check_abort_conditions.
void Update(Region& r, bool abortAll) {
    const float* p = PlaceOf(g_localPlayer);
    const float away = DistanceToWorldBox(r, p);
    if (!r.aborting && (abortAll || away > kAbortDistance)) {
        r.aborting = true;
        ++g_aborted;
        RFA_LOG_DETAIL_FIRST(32, "Ambushes: region 0x%08X aborted (%s); its squads leave once out of view", r.handle,
                      abortAll ? "an activity or mission runs" : "the player is past 400 m from its box");
    }
    if (!g_lookedForVehicles) {
        g_lookedForVehicles = true;
        if (!guerrilla_vehicles::Running()) rfa::Log("Ambushes: guerrilla_vehicles' chase isn't there; a riding ambush's crew sits in its vehicle");
    }
    // Each squad's people still about (and, when aborting, those out of view taken away), its first one, its team, its vehicle.
    struct About { int left = 0; BYTE* first = nullptr; int team = -1; DWORD vehicle = kNone; };
    std::unordered_map<DWORD, About> about;
    for (const Region::Squad& s : r.squads) about[s.handle] = About{};
    const int people = NpcCount(kWorld, nullptr);
    for (int i = 0; i < people; ++i) {
        BYTE* human = NpcAt(kWorld, nullptr, i);
        if (!human || GoneOrDead(human)) continue;
        auto squad = about.find(SquadOf(human));
        if (squad == about.end()) continue;
        const DWORD vehicle = *reinterpret_cast<const DWORD*>(human + kInVehicle);
        if (r.aborting) {
            const float* at = PlaceOf(human);
            const float head[3] = {at[0], at[1] + 1.0f, at[2]};
            if (!InView(head, 1.0f)) {
                if (vehicle != kNone) GuerrillaVehicles_Release(vehicle);
                RemoveObject(kWorld, nullptr, HandleOf(human), 0);
                const bool players = vehicle != kNone && *reinterpret_cast<const DWORD*>(g_localPlayer + kInVehicle) == vehicle;
                if (vehicle != kNone && !players && WorldObject(kWorld, nullptr, vehicle)) RemoveObject(kWorld, nullptr, vehicle, 0);  // its vehicle goes with it
                continue;
            }
        }
        About& a = squad->second;
        if (++a.left == 1) a.first = human, a.team = *reinterpret_cast<const int*>(human + kTeam);
        if (vehicle != kNone) a.vehicle = vehicle;
    }
    if (!r.aborting) {
        // Guerrilla's maybe_play_ambush_voice_line (RM 0xA66EA0): a member of each squad shouts as it comes (voc_ambush).
        for (Region::Squad& s : r.squads)
            if (!s.shouted && about[s.handle].first) {
                s.shouted = true;
                VoiceSay(about[s.handle].first, "voc_ambush");
            }
        // A riding crew, once seated, goes after the player (guerrilla_vehicles' chase).
        for (Region::Squad& s : r.squads) {
            const About& a = about[s.handle];
            if (!s.riding || s.handed || a.vehicle == kNone || !guerrilla_vehicles::Running()) continue;
            if (GuerrillaVehicles_Chase(a.vehicle, HandleOf(g_localPlayer))) {
                s.handed = true;
                s.vehicle = a.vehicle;
                ++g_handed;
                RFA_LOG_DETAIL_FIRST(32, "Ambushes: region 0x%08X's riding squad 0x%08X (vehicle 0x%08X) goes after the player (guerrilla_vehicles' chase)", r.handle, s.handle,
                              a.vehicle);
            }
        }
        // Guerrilla's update_squad_orders: each squad at another team's squad of the region, else at the player.
        auto FirstOf = [&](int team) -> BYTE* {
            for (const Region::Squad& s : r.squads)
                if (const About& a = about[s.handle]; a.left > 0 && a.team == team) return a.first;
            return nullptr;
        };
        for (Region::Squad& s : r.squads) {
            const About& a = about[s.handle];
            if (a.left == 0) continue;
            BYTE* target = nullptr;
            if (a.team == kMarauderTeam) target = FirstOf(kEdfTeam) ? FirstOf(kEdfTeam) : FirstOf(kGuerrillaTeam);
            else if (a.team == kEdfTeam) target = FirstOf(kMarauderTeam) ? FirstOf(kMarauderTeam) : FirstOf(kGuerrillaTeam);
            else if (a.team == kGuerrillaTeam) target = FirstOf(kEdfTeam) ? FirstOf(kEdfTeam) : FirstOf(kMarauderTeam);
            if (!target) target = g_localPlayer;
            if (HandleOf(target) == s.target) continue;
            BYTE* node = WorldObject(kWorld, nullptr, s.node);
            if (!node) continue;
            NodeSetOrders(node, nullptr, target, kAttackOrders);
            s.target = HandleOf(target);
            ++g_skirmishOrders;
            RFA_LOG_DETAIL_FIRST(32, "Ambushes: region 0x%08X's squad 0x%08X (team %d) attacks %s 0x%08X, as Guerrilla's update_squad_orders", r.handle, s.handle, a.team,
                          target == g_localPlayer ? "the player" : "another team's squad, its member", s.target);
        }
    }
    r.squads.erase(std::remove_if(r.squads.begin(), r.squads.end(), [&](const Region::Squad& s) { return about[s.handle].left == 0; }), r.squads.end());
    if (r.squads.empty()) {
        r.fired = r.aborting = false;
        RFA_LOG_DETAIL_FIRST(64, "Ambushes: region 0x%08X over (its squads dead or gone); it can fire again the next time the player comes in", r.handle);
    }
}

}  // namespace

void StartAmbushes() {
    ReadRegions();
    if (g_regions.empty()) return;
    g_nodeOwnOrders = reinterpret_cast<NodeOwnOrdersFn>(rfa::HookFunction(kNodeOwnOrders, kNodeOwnOrdersPrologue, reinterpret_cast<void*>(&NodeOwnOrders)));
    g_nodeOrders = reinterpret_cast<NodeOrdersFn>(rfa::HookFunction(kNodeOrders, kNodeOrdersPrologue, reinterpret_cast<void*>(&NodeOrders)));
    if (!g_nodeOwnOrders || !g_nodeOrders)
        rfa::Log("Ambushes: could not hook the squad nodes' orders; an ambush's squads keep their node's orders instead of attacking the player");
}

bool AmbushHoldsNode(const unsigned char* node) {
    return node && node != g_firing && !g_heldNodes.empty() && g_heldNodes.count(HandleOf(node)) > 0;
}

void AmbushFrame(bool ours) {
    if (g_regions.empty()) return;
    const int now = GameTime();
    if (!ours || now < g_lastClock) {  // another world, or the level started over: what was out is gone with it
        for (Region& r : g_regions) r.known = r.inside = r.fired = r.aborting = false, r.squads.clear();
        g_lastClock = now;
        return;
    }
    g_lastClock = now;
    if (!g_localPlayer) return;
    const bool running = CampaignRunning();
    const float* p = PlaceOf(g_localPlayer);
    const int hour = GameHour();
    const bool night = hour >= 22 || hour <= 1;  // game_clock_get_tod_partitioned's GC_MID_NIGHT
    for (Region& r : g_regions) {
        if (r.fired && now >= r.nextUpdate) {
            r.nextUpdate = now + kUpdateLeast + std::rand() % (kUpdateMost - kUpdateLeast + 1);
            Update(r, running);
        }
        const bool inside = InsideBox(r, p);
        const bool cameIn = r.known && inside && !r.inside;
        r.known = true;
        r.inside = inside;
        if (!cameIn || r.fired || running || !r.enabled) continue;
        const float chance = night ? r.night : r.day;
        if (static_cast<float>(std::rand()) / RAND_MAX >= chance) {
            ++g_rolledOut;
            RFA_LOG_DETAIL_FIRST(32, "Ambushes: the player came into region 0x%08X; its %s chance of %.0f %% didn't come up", r.handle, night ? "night" : "day", chance * 100);
            continue;
        }
        Fire(r, chance, night);
    }
}
