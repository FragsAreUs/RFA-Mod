// Guerrilla's walkers in its world: its ambient people spawner (amb_spawn's do_frame_spawn_npc, RM 0xB5F640) and
// their wander (pedestrian_wander_action: civilian_ped_do_action_common, RM 0x690960; civilian_pick_next_ped_node,
// build_new_waypoints_normal), which Armageddon deleted, ported onto the code Armageddon kept. A part of
// guerrilla_people. GuerrillaBuilder.exe writes what it needs: mods\guerrilla_people\ambient.txt (each zone's
// walkers by day and by night, its spawn resource) and crews.txt (each resource's civilians), and the world part
// places Guerrilla's pedestrian navpoints (type 2) with their links.
//
// Spawning, as Guerrilla's: every 250 to 500 ms (500 to 1000 ms with 75 people or more in the world), in our
// worlds, where the player's zone has walkers, never with 90 people or more in the world: up to Guerrilla's cap
// (max_ped_count 0xB41180, its table 0x1335CF8 by alert level and density: green none 0, low 10, medium 20, high
// 30; yellow 0/8/10/20; orange 0/6/8/10; red 0/6/6/8; the night's density from 18:00 to 6:00, as
// game_clock_get_time_of_day has it), one at a time, on a pedestrian navpoint 20 to 100 m from the player
// (Player_spawn_region's 20 m, PED_NODE_COLLECTION_RADIUS) out of view, no more than a quarter of the cap toward
// one side of the player (max_ped_count_quad), facing along one of its links, as one of the zone's resource's
// civilians the game has streamed in (made with drop_human's create, 0x762D50). From the cap less 2 the farthest
// walker 20 m or more away starts leaving (select_despawning_npc, npc::start_active_despawn), one at a time, so
// the crowd changes. The alert level is guerrilla_campaign's (GuerrillaCampaign_Alert); green without it.
// Not ported yet: build_spawn_node_list weighs the nodes by the network's costs (here one is picked at random
// among those allowed), and the camera's own direction (pick_spawn_direction; here the player's sides).
//
// Walking, as Guerrilla's: each frame the walker's AI is asked to go to its next navpoint walking (Guerrilla's
// ai_system::pathfind_to, Armageddon's 0x431C20, as civilian_ped_do_action_common asks: walk, standing, low
// priority, no cell given), its AI otherwise sitting the frame out (the planner, perception and firing skip it,
// as for the traffic's drivers). Within 1.25 m of it (across the ground) the next is chosen: one of its links
// not among the last 8 it walked to, one ahead of it first (Guerrilla's directional choice), at random among
// those; a dead end turns it back. The walkers are NPC_MANAGER_AMBIENT (npc +0x1700 = 1), as Guerrilla's. Not
// ported yet: Guerrilla's pauses and animations at nodes (pedestrian_pause_action, pedestrian_anim_action) and its
// offset of the walk across the path. One that gets a flee belief (shots, explosions, damage near it) is given
// back to the game's AI until Guerrilla's panic (pedestrian_panicked_goal) is ported.
//
// Despawning, as Guerrilla's should_despawn_npc (0xB47700): 20 m or more away, 10 s after it was made, out of
// view for 5 s, and 20 s after it was made or beyond 120 m (MAX_PED_SPAWN_DISTANCE); a leaving one
// (can_delete_despawning_npc) once it is 20 m away and out of view for 5 s, or beyond 120 m.
//
// Left to others (the alert session's morale converts, 2026-11-13): a walker whose team changes from the one it was
// made with (a convert joins the player's, human +0x13B4: 0 guerrillas, 1 EDF, 2 civilians, 3 Marauders), or that another mod
// asks for (GuerrillaPeople_LeaveToGame), is given back to the game's AI and is no longer a walker: not walked, not
// counted against the cap, not despawned by these rules.
//
// Their side (the user's run of 2026-11-13 11:52: armed people in civilian clothes stood in Dust's square and didn't fight
// the EDF; two of them, team 0, died there): the district people are Guerrilla's guerrilla-team characters (Team
// Guerilla, an rfg_m16 in their inventory, flags "unarmed" and "use tech level"), and a walker made from its character
// was a guerrilla. Guerrilla's ped spawn (spawn_npc_navpoint, RM 0xB5EB20) gives a walker no loadout and keeps its
// character's own team (npc_flags use_default_team) only where it spawns inside a safehouse's area
// (is_inside_safehouse_area, RM 0xB4D590: each enabled safehouse's box over its trigger regions); everywhere else it is a
// civilian, who keeps the rifle a convert later fights with. So a walker made outside a safehouse's area is put on the
// civilians' team (the NPC's set_team, 0x8AB200); the areas are the vehicles' mods\guerrilla_vehicles\safehouses.txt
// (each safehouse's trigger region, a box or a sphere).
#define RFA_MOD_NAME "guerrilla_people"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "campaign/campaign_api.h"
#include "ambient.h"
#include "actions.h"
#include "voices.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

// ---- The game's (Armageddon's publicbeta build)
// An object: +0x04 its position, +0x10 its orientation (right, up, forward), +0x68 its handle. An NPC: +0x1700 who
// manages it (1 ambient), its ai_system at +0x174C (+0x651 bit 2: yield_next_frame). A navpoint: +0xF4 its type (a
// byte: 2 pedestrian), its links from +0x9C (navpoints), how many a short at +0xE8.
constexpr DWORD kPosition = 0x04, kOrientation = 0x10, kHandle = 0x68, kManagement = 0x1700, kNpcAi = 0x174C, kYield = 0x651;
constexpr DWORD kObjectType = 0x7A, kNavpointType = 0xF4, kNavpointLinks = 0x9C, kNavpointLinkCount = 0xE8;
constexpr int kMostLinks = (kNavpointLinkCount - kNavpointLinks) / 4;
constexpr int kNavpointObject = 10;  // an object's type byte for a navpoint (the world's navpoint list, 0x43AC80 → 0x436380)
constexpr int kPedestrianNavpoint = 2, kAmbient = 1;
constexpr DWORD kNone = 0xFFFFFFFF;
void* const kWorld = reinterpret_cast<void*>(0x1870630);

using WorldCountFn = int(__fastcall*)(void* world, void*);
using WorldAtFn = BYTE*(__fastcall*)(void* world, void*, int index);
using ByHandleFn = BYTE*(__fastcall*)(void* world, void*, DWORD handle);
auto const Player = reinterpret_cast<BYTE*(__cdecl*)()>(0x8B92D0);
auto const NpcCount = reinterpret_cast<WorldCountFn>(0x424480);       // world::get_num_objects<npc>
auto const NpcAt = reinterpret_cast<WorldAtFn>(0x4244D0);
auto const NavpointCount = reinterpret_cast<WorldCountFn>(0x43AC80);  // world::get_num_objects<navpoint>
auto const NavpointAt = reinterpret_cast<WorldAtFn>(0x43ACD0);
// world::object_from_handle, any object (0x424350, which the first build used, answers only objects of type 2,
// subtype 1: never a navpoint, so every walker's next navpoint was "gone" and it was sent to the one it stood on).
auto const ObjectByHandle = reinterpret_cast<ByHandleFn>(0x6CB9E0);
auto const HumanByHandle = reinterpret_cast<ByHandleFn>(0x436120);
auto const GoneOrDead = reinterpret_cast<bool(__cdecl*)(BYTE* object)>(0x42ADC0);  // true for null or a dead one
auto const RemoveObject = reinterpret_cast<int(__fastcall*)(void* world, void*, DWORD handle, int flag)>(0x6D56A0);
auto const HumanInfo = reinterpret_cast<BYTE*(__cdecl*)(const char* name)>(0x88D170);
auto const MakeHuman = reinterpret_cast<BYTE*(__cdecl*)(BYTE* info, const float* where, const float* orientation, void* variant)>(0x762D50);
using SetTeamFn = bool(__fastcall*)(BYTE* npc, void*, int team);
auto const SetTeam = reinterpret_cast<SetTeamFn>(0x8AB200);  // the NPC's set_team (it tells the AI too)
auto const InView = reinterpret_cast<bool(__cdecl*)(const float* centre, float radius)>(0x768400);
auto const FleeToSafetyBelief = reinterpret_cast<BYTE*(__cdecl*)(BYTE* npc)>(0x483C90);
auto const EnemyFleeBelief = reinterpret_cast<BYTE*(__cdecl*)(BYTE* npc)>(0x483B00);
// ai_system::pathfind_to (thiscall on the AI; ret 1Ch): the place, how it moves (1 walk), its stance (0 standing),
// the priority (0 low), the destination's nav cell (a 64-bit handle; all ones for none), a flag. It answers
// Guerrilla's ai_pathfind_results: 0 on its way, 1 not ready yet, 2 ragdolled, 3 an animation in the way, 4 in a
// vehicle, 5 danger, 6 no path, 7 its start isn't on the mesh, 8 the destination isn't.
using GoToFn = int(__fastcall*)(BYTE* ai, void*, const float* place, int movement, int stance, int priority, DWORD cellLow, DWORD cellHigh, bool flag);
auto const GoTo = reinterpret_cast<GoToFn>(0x431C20);
constexpr int kWalk = 1, kStanding = 0, kLowPriority = 0;
constexpr int kSprint = 3;  // human_movement_states: HMS_SPRINT
// A human's animation state override (0x86D0F0, cdecl: the human, the state; human +0x73C; -1 none), the state by name
// (0x97EAE0, its index in the game's states, -1 when none has that name): Armageddon's own sf_char_set_anim_state_override
// and run_around_panicked_action use them.
auto const FindState = reinterpret_cast<int(__cdecl*)(const char* name)>(0x97EAE0);
auto const SetStateOverride = reinterpret_cast<void(__cdecl*)(BYTE* human, int state)>(0x86D0F0);
// Guerrilla's character_instance::set_state (RM 0x802040: vanim_change_state with a state and an action, each an
// index_alias of the index and an alias, a blend and a flag) is Armageddon's 0x9691D0 (thiscall on the human's
// animation, *(human +0x760); vanim_change_state 0x980730; ret 0x18), as its ragdoll get-up calls it. An action by
// name: 0x97EB90 (the twin of 0x97EAE0, -1 when none has that name). The stance: 0x854B90 (cdecl: human, stance;
// human +0x728: 0 standing, 1 crouching).
struct IndexAlias { int index, alias; };
using SetAnimStateFn = void(__fastcall*)(BYTE* animation, void*, IndexAlias state, IndexAlias action, float blend, bool flag);
auto const SetAnimState = reinterpret_cast<SetAnimStateFn>(0x9691D0);
auto const FindAction = reinterpret_cast<int(__cdecl*)(const char* name)>(0x97EB90);
auto const SetStance = reinterpret_cast<void(__cdecl*)(BYTE* human, int stance)>(0x854B90);
constexpr DWORD kHumanAnimation = 0x760;
constexpr int kStandingStance = 0, kCrouchingStance = 1, kNoAlias = -1;
constexpr float kCowerBlend = 0.3f;  // MIN_PATH_DIST, the blend Guerrilla's cowers use
constexpr DWORD kBeliefHandle = 0x00, kBeliefPosition = 0x04, kBeliefType = 0x24;  // ai_belief: its source, its place, its kind
// Armageddon numbers the kinds its own way (their names, set up at 0xF14D80: ... corpse 11, friendly fire 12, building
// 13, vehicle 14, turret 15, exo 16, ally on turret 17, projectile 18, hazard 19, misc. sound 20, combat sound 21,
// bullet impact 22, explosion 23, building stress 24, ...); Guerrilla's AI_BELIEF_VEHICLE is 13, Armageddon's 14.
constexpr char kVehicleBelief = 14;  // flee_to_safety_goal leaves a vehicle to the vehicle goals (cower_goal, gawk_goal)
constexpr int kNoPath = 6, kStartOffMesh = 7, kDestinationOffMesh = 8;
int GameTime() { return *reinterpret_cast<const int*>(0x1121300); }   // ms; it stops while the game is paused
int GameHour() { return *reinterpret_cast<const BYTE*>(0x12F76D4); }
int GameMinute() { return *reinterpret_cast<const BYTE*>(0x12F76D5); }  // the game clock 0x12F76D0: date, hour, minute, second

// ---- Guerrilla's ambient behaviours (step 4b of the people's rest, 2026-11-15) [RM, A]
// Guerrilla's ambient_spawn_behavior.xtbl groups (Refinery Supervisor, Refinery Miner, Dust Drunk, Dust Worker, Dust
// Ambient), each of up to 20 behaviours: an animation set variant (Animation_Override, by
// vanim_get_animation_set_variant_from_name: drunk, tired, oppressed, confident, ...; miner, supervisor and depressed
// aren't variants, so give none), an ambient prop (Ambient_Prop), a chance, the hours (Day_Start..Day_End), a morale
// range, MaleFemaleRatio (stored + 1). A person made with an ambient behaviour id takes one in human::serialize (RM
// 0xADB1BE): the hour with its minutes, the district's morale over its most, a roll of 0..100; the first behaviour
// whose hours and morale take the hour and morale, and whose chance × (1 + ratio for a woman, 1 - ratio for a man,
// human_info's female flag) is over the roll, else the roll less its chance; then its variant set
// (vanim_set_base_animation_set_variant) and its prop the person's (+0xC88), made by amb_prop_create. The id is the
// zone's ambient_spawn_info's default_ambient_behavior (all of Guerrilla's 41 are Dust Ambient), or an
// object_ambient_behavior_region's group within its radius (4: three in Parker, one in 07_12, Dust Drunk and Dust
// Worker). Armageddon kept it all (its loader 0x7522F0, the human's ambient_behavior property, its pick in 0x8605E0:
// the variant 0x98C310 on the human's animation +0x760 → +4 with +0xB8 |= 0x4000, the prop at +0x1338, made by
// amb_prop_create 0x750F10; it ships Guerrilla's ambient_prop_info.xtbl itself, 0x750E40), but a walker made by
// drop_human's create isn't given one, so here the pick is made as Guerrilla's as each walker is made (ambient.txt's
// behaviour, default, region and female lines, from the people build) and applied with Armageddon's own steps.
auto const VariantNamed = reinterpret_cast<int(__cdecl*)(const char* name)>(0x98C530);         // vanim_get_animation_set_variant_from_name
auto const SetBaseVariant = reinterpret_cast<void(__cdecl*)(void* animate, int variant)>(0x98C310);  // vanim_set_base_animation_set_variant
auto const AmbientPropNamed = reinterpret_cast<BYTE*(__cdecl*)(const char* name)>(0x7510F0);    // amb_prop_info_find
auto const MakeAmbientProp = reinterpret_cast<bool(__cdecl*)(BYTE* info, BYTE* human)>(0x750F10);  // amb_prop_create
constexpr DWORD kHumanAnimationOwner = 0x760, kHumanStateFlags = 0xB8, kAmbientPropInfo = 0x1338;
constexpr DWORD kVariantSet = 0x4000;
struct Behavior {
    std::string variant, prop;
    float chance = 0, start = 0, end = 24, moraleLeast = 0, moraleMost = 1, ratio = 0;
};
struct BehaviorRegion { float place[3]{}; float radius = 0; std::string group; };

// ---- Guerrilla's numbers
constexpr int kCaps[4][4] = {{0, 10, 20, 30}, {0, 8, 10, 20}, {0, 6, 8, 10}, {0, 6, 6, 8}};  // [alert][density], 0x1335CF8
constexpr int kMostPeople = 90, kBusyPeople = 75;  // no spawn from 90 people in the world; spawning slows from 75
constexpr float kNearest = 20.0f, kFarthestNode = 100.0f, kFarthest = 120.0f, kReached = 1.25f, kApart = 3.0f;
constexpr int kOldWaypoints = 8;
constexpr int kYoung = 10000, kOld = 20000, kUnseen = 5000;  // ms

struct V3 { float x, y, z; };
V3 Pos(const BYTE* object) { return *reinterpret_cast<const V3*>(object + kPosition); }
DWORD Handle(const BYTE* object) { return *reinterpret_cast<const DWORD*>(object + kHandle); }
float Distance(const V3& a, const V3& b) { const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z; return std::sqrt(dx * dx + dy * dy + dz * dz); }
float FlatDistance(const V3& a, const V3& b) { const float dx = a.x - b.x, dz = a.z - b.z; return std::sqrt(dx * dx + dz * dz); }
float Random() { return static_cast<float>(std::rand()) / RAND_MAX; }
int RandomBelow(int count) { return count <= 1 ? 0 : static_cast<int>(std::rand() / (RAND_MAX + 1.0) * count); }

// ---- What GuerrillaBuilder.exe wrote
struct Zone {
    int col, row, day, night;  // densities 0 none .. 3 high
    std::string resource, entry;
};
struct Files {
    std::vector<std::pair<std::string, std::vector<Behavior>>> behaviors;  // each group's
    std::vector<std::pair<std::pair<int, int>, std::string>> defaults;     // a zone's group
    std::vector<BehaviorRegion> behaviorRegions;
    std::vector<std::string> women;
    float zoneWidth = 511.0f;
    int columnAtZero = 8, rowAtZero = 7;
    std::vector<Zone> zones;
    std::vector<std::pair<std::string, std::vector<std::string>>> civilians;  // resource → its civilians (crews.txt's "Guerilla")
    std::vector<std::pair<std::string, std::vector<std::string>>> walkers;    // a rotating district's version → its civilians (crews.txt's "Walkers")
};
Files g_files;
bool g_on = false;

std::vector<std::string> Split(const std::string& text, char by) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in(text);
    while (std::getline(in, part, by))
        if (!part.empty()) parts.push_back(part);
    return parts;
}

void ReadFiles() {
    const std::wstring folder = rfa::RootDir() + L"\\mods\\guerrilla_people\\";
    std::ifstream ambient(folder + L"ambient.txt");
    for (std::string line; std::getline(ambient, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        std::istringstream in(line);
        std::string kind;
        in >> kind;
        if (kind == "behavior" || kind == "default" || kind == "region" || kind == "female") {
            const auto f = Split(line, '\t');
            auto Number = [](const std::string& t) { return static_cast<float>(std::atof(t.c_str())); };
            if (kind == "behavior" && f.size() >= 10) {
                Behavior b{f[2] == "-" ? "" : f[2], f[3] == "-" ? "" : f[3], Number(f[4]), Number(f[5]), Number(f[6]), Number(f[7]), Number(f[8]), Number(f[9])};
                auto group = std::find_if(g_files.behaviors.begin(), g_files.behaviors.end(), [&](const auto& g) { return g.first == f[1]; });
                if (group == g_files.behaviors.end()) group = g_files.behaviors.insert(g_files.behaviors.end(), {f[1], {}});
                group->second.push_back(b);
            }
            else if (kind == "default" && f.size() >= 4) g_files.defaults.push_back({{std::atoi(f[1].c_str()), std::atoi(f[2].c_str())}, f[3]});
            else if (kind == "region" && f.size() >= 6) {
                BehaviorRegion r;
                r.place[0] = Number(f[1]), r.place[1] = Number(f[2]), r.place[2] = Number(f[3]);
                r.radius = Number(f[4]);
                r.group = f[5];
                g_files.behaviorRegions.push_back(r);
            }
            else if (kind == "female" && f.size() >= 2) g_files.women.push_back(f[1]);
            continue;
        }
        if (kind == "grid") in >> g_files.zoneWidth >> g_files.columnAtZero >> g_files.rowAtZero;
        else if (kind == "zone") {
            Zone zone{};
            if (in >> zone.col >> zone.row >> zone.day >> zone.night >> zone.resource) {
                in >> zone.entry;
                zone.day = std::clamp(zone.day, 0, 3);
                zone.night = std::clamp(zone.night, 0, 3);
                g_files.zones.push_back(zone);
            }
        }
    }
    std::ifstream crews(folder + L"crews.txt");
    for (std::string line; std::getline(crews, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto fields = Split(line, '\t');
        if (fields.size() == 3 && _stricmp(fields[1].c_str(), "Guerilla") == 0) g_files.civilians.push_back({fields[0], Split(fields[2], ',')});
        if (fields.size() == 3 && _stricmp(fields[1].c_str(), "Walkers") == 0) g_files.walkers.push_back({fields[0], Split(fields[2], ',')});
    }
}

const Zone* ZoneAt(const V3& where) {
    const int col = static_cast<int>(std::floor(where.x / g_files.zoneWidth)) + g_files.columnAtZero;
    const int row = g_files.rowAtZero - static_cast<int>(std::floor(where.z / g_files.zoneWidth));
    for (const Zone& zone : g_files.zones)
        if (zone.col == col && zone.row == row) return &zone;
    return nullptr;
}

// The district's morale over its most, as Guerrilla's pick reads it (gfm_get_district_morale / _max): guerrilla_campaign's
// GuerrillaCampaign_District for the player's district; a half without it (Guerrilla's groups all take 0..1).
float DistrictMorale() {
    GuerrillaCampaignDistrict info{};
    if (!GuerrillaCampaign_District(-1, &info) || info.moraleMax <= 0) return 0.5f;
    return info.morale / info.moraleMax;
}

int g_behaviorsGiven = 0, g_behaviorPropsMade = 0;

// ---- The districts' civilians in batches (step 4c, 2026-11-15; the user: "Batches") [RM, A]
// Guerrilla streams 9 civilian types of a district's group at once and swaps one for another of the group each time the
// player has gone 80 m (rotate_character_usage RM 0xB5E730), its slot clean-up letting the unused go. Armageddon lets
// a character type go only through its own change of spawn resource (0x7628B0 sets the resource and state 1; the state
// machine 0x762730, which guerrilla_people runs each frame in our worlds, lets go of what the new one doesn't list and
// loads the rest). So the people build writes up to 3 versions of a district's resource (rfg_dust, rfg_dust_r2,
// rfg_dust_r3: the same EDF, Marauders and 5 civilians its squads use, and 4 others of the group each,
// GuerrillaDistricts.Rotation), and here the version in force moves on, as Guerrilla's 80 m a swap would have swapped
// the 4: once the player has gone 300 m since the last change and 90 s have passed, at alert green or yellow, when no
// person of a civilian type going out is in view within 100 m (it would vanish). The game asking for the district's
// resource (its zone's) gets the version in force (AmbientResourceFor, from guerrilla_people's hook on 0x7628B0).
auto const FindSpawnResource = reinterpret_cast<void*(__cdecl*)(const char* name)>(0x75EC70);
auto const SetLevelResourceThroughHook = reinterpret_cast<int(__cdecl*)(void* resource, int again)>(0x7628B0);
const int* const kResourceState = reinterpret_cast<const int*>(0x19734CC);
constexpr int kResourceInForce = 4;
constexpr DWORD kHumanCharacter = 0x4FC;  // a human: its character entry
constexpr float kRotateDistance = 300.0f, kGoingOutSeen = 100.0f;
constexpr int kRotateLeast = 90000, kRotateCheckEvery = 2000;
struct Rotating { std::string base; int versions = 1, at = 0, lastSwitch = 0; V3 lastPlace{}; bool placed = false; };
std::vector<Rotating> g_rotating;
std::string g_currentBase;  // the rotating district whose resource the game last asked for
int g_nextRotateCheck = 0, g_rotations = 0, g_rotationsHeld = 0;
// Behind [GuerrillaPeople] RotateCivilians (0 when it isn't set: no changes; 1 turns them on): the user's run of 2026-11-18 22:27-22:35 ("vehicles keep
// despawning along with all of the npc's") showed Armageddon's change of spawn resource doesn't swap only what differs:
// state 1 lets go of every person and vehicle model (streamed people 17 → 1, vehicle models 6 → 0) and states 2-4 load
// them all again (people 15, models 3), so every car and person of the district went at each change. Guerrilla's
// spawn-resource manager swaps one civilian at a time and keeps what's in use streamed. Off, each district stays on
// its first version; the proper way (only the 4 walker slots swapped, by our own load and release of one character
// type, once that is proven safe) is the later step.
bool g_rotateOn = false;

std::string VersionName(const Rotating& r, int v) { return v == 0 ? r.base : r.base + "_r" + std::to_string(v + 1); }

Rotating* RotatingFor(const std::string& name) {
    for (Rotating& r : g_rotating)
        if (_stricmp(r.base.c_str(), name.c_str()) == 0) return &r;
    return nullptr;
}

const std::vector<std::string>* WalkersOf(const std::string& version) {
    for (const auto& [name, people] : g_files.walkers)
        if (_stricmp(name.c_str(), version.c_str()) == 0) return &people;
    return nullptr;
}

// The rotating districts: a base resource with a Walkers line and its versions after it (_r2, _r3) in crews.txt.
void FindRotating() {
    for (const auto& [name, people] : g_files.walkers) {
        if (name.size() > 3 && name.compare(name.size() - 3, 2, "_r") == 0 && std::isdigit(static_cast<unsigned char>(name.back()))) continue;  // a version
        Rotating r;
        r.base = name;
        while (r.versions < 3 && WalkersOf(VersionName(r, r.versions))) ++r.versions;
        if (r.versions > 1) g_rotating.push_back(r);
    }
}

// Every 2 s in our worlds: the district in force moves on to its next version when Guerrilla's walk of the player would
// have swapped its rotating civilians, and none of those going out is in view near the player.
void RotateFrame(const V3& player, int now, int alert) {
    if (!g_rotateOn || g_rotating.empty() || now < g_nextRotateCheck) return;
    g_nextRotateCheck = now + kRotateCheckEvery;
    Rotating* r = RotatingFor(g_currentBase);
    if (!r || *kResourceState != kResourceInForce) return;
    if (!r->placed) {
        r->placed = true;
        r->lastPlace = player;
        r->lastSwitch = now;
        return;
    }
    const float moved = Distance(player, r->lastPlace);
    if (moved < kRotateDistance || now - r->lastSwitch < kRotateLeast || alert > 1) return;
    const int next = (r->at + 1) % r->versions;
    const auto* in = WalkersOf(VersionName(*r, next));
    const auto* out = WalkersOf(VersionName(*r, r->at));
    void* resource = FindSpawnResource(VersionName(*r, next).c_str());
    if (!in || !out || !resource) return;
    std::vector<std::string> goingOut, comingIn;
    for (const std::string& c : *out)
        if (std::none_of(in->begin(), in->end(), [&](const std::string& k) { return _stricmp(k.c_str(), c.c_str()) == 0; })) goingOut.push_back(c);
    for (const std::string& c : *in)
        if (std::none_of(out->begin(), out->end(), [&](const std::string& k) { return _stricmp(k.c_str(), c.c_str()) == 0; })) comingIn.push_back(c);
    std::vector<BYTE*> outInfos;
    for (const std::string& c : goingOut)
        if (BYTE* info = HumanInfo(c.c_str())) outInfos.push_back(info);
    const int people = NpcCount(kWorld, nullptr);
    for (int i = 0; i < people; ++i) {
        BYTE* human = NpcAt(kWorld, nullptr, i);
        if (!human || GoneOrDead(human)) continue;
        const BYTE* character = *reinterpret_cast<BYTE* const*>(human + kHumanCharacter);
        if (std::find(outInfos.begin(), outInfos.end(), character) == outInfos.end()) continue;
        const V3 place = Pos(human);
        const V3 head{place.x, place.y + 1.0f, place.z};
        if (Distance(place, player) <= kGoingOutSeen && InView(&head.x, 1.0f)) {
            ++g_rotationsHeld;
            RFA_LOG_DETAIL_FIRST(8, "Walkers: %s's civilians stay as they are for now: a %s going out is in view nearby", r->base.c_str(),
                          goingOut.empty() ? "person" : goingOut.front().c_str());
            return;
        }
    }
    auto Join = [](const std::vector<std::string>& v) {
        std::string s;
        for (const std::string& c : v) s += (s.empty() ? "" : ", ") + c;
        return s.empty() ? std::string("none") : s;
    };
    SetLevelResourceThroughHook(resource, 0);
    r->at = next;
    r->lastSwitch = now;
    r->lastPlace = player;
    ++g_rotations;
    rfa::LogDetail("Walkers: %s's civilians change to version %d of %d (%s), after %.0f m: in %s; out %s (%d changes so far)", r->base.c_str(), next + 1, r->versions,
             VersionName(*r, next).c_str(), moved, Join(comingIn).c_str(), Join(goingOut).c_str(), g_rotations);
}

// Guerrilla's pick of an ambient behaviour for a walker just made (see the top), applied with Armageddon's own steps.
void GiveBehavior(BYTE* human, const std::string& character, const V3& place, const Zone& zone) {
    const std::string* group = nullptr;
    for (const BehaviorRegion& r : g_files.behaviorRegions) {
        const float dx = place.x - r.place[0], dy = place.y - r.place[1], dz = place.z - r.place[2];
        if (dx * dx + dy * dy + dz * dz <= r.radius * r.radius) {
            group = &r.group;
            break;
        }
    }
    if (!group)
        for (const auto& [at, name] : g_files.defaults)
            if (at.first == zone.col && at.second == zone.row) {
                group = &name;
                break;
            }
    if (!group) return;
    auto found = std::find_if(g_files.behaviors.begin(), g_files.behaviors.end(), [&](const auto& g) { return _stricmp(g.first.c_str(), group->c_str()) == 0; });
    if (found == g_files.behaviors.end()) return;
    const float hour = static_cast<float>(GameHour()) + static_cast<float>(GameMinute()) / 60.0f;
    const float morale = DistrictMorale();
    const bool woman = std::any_of(g_files.women.begin(), g_files.women.end(), [&](const std::string& w) { return _stricmp(w.c_str(), character.c_str()) == 0; });
    float roll = Random() * 100.0f;
    for (const Behavior& b : found->second) {
        if (hour < b.start || hour > b.end || morale < b.moraleLeast || morale > b.moraleMost) continue;
        const float factor = woman ? 1.0f + b.ratio : 1.0f - b.ratio;
        if (b.chance * factor <= roll) {
            roll -= b.chance;
            continue;
        }
        bool variantSet = false, propMade = false;
        if (!b.variant.empty()) {
            const int variant = VariantNamed(b.variant.c_str());
            BYTE* owner = *reinterpret_cast<BYTE**>(human + kHumanAnimationOwner);
            void* animate = owner ? *reinterpret_cast<void**>(owner + 4) : nullptr;
            if (variant >= 0 && animate) {
                SetBaseVariant(animate, variant);
                *reinterpret_cast<DWORD*>(human + kHumanStateFlags) |= kVariantSet;
                variantSet = true;
            }
        }
        if (!b.prop.empty())
            if (BYTE* info = AmbientPropNamed(b.prop.c_str())) {
                *reinterpret_cast<BYTE**>(human + kAmbientPropInfo) = info;
                propMade = MakeAmbientProp(info, human);
                if (propMade) ++g_behaviorPropsMade;
            }
        ++g_behaviorsGiven;
        RFA_LOG_DETAIL_FIRST(24, "Walkers: %s takes Guerrilla's ambient behaviour of %s: %s%s%s%s (at %.1f h, morale %.2f)", character.c_str(), group->c_str(),
                      b.variant.empty() ? "no variant" : b.variant.c_str(), variantSet ? "" : b.variant.empty() ? "" : " (not a variant)",
                      b.prop.empty() ? "" : propMade ? ", holding " : ", without ", b.prop.c_str(), hour, morale);
        return;
    }
}

const std::vector<std::string>* CiviliansOf(const std::string& resource) {
    if (const Rotating* r = RotatingFor(resource))  // a rotating district: the version in force's (or its base's) civilians
        if (const auto* walkers = WalkersOf(VersionName(*r, _stricmp(g_currentBase.c_str(), r->base.c_str()) == 0 ? r->at : 0))) return walkers;
    const std::vector<std::string>* fallback = nullptr;
    for (const auto& [name, people] : g_files.civilians) {
        if (people.empty()) continue;
        if (_stricmp(name.c_str(), resource.c_str()) == 0) return &people;
        if (_stricmp(name.c_str(), "rfg_Default") == 0) fallback = &people;
    }
    return fallback;
}

// Guerrilla's alert level from guerrilla_campaign (0 green .. 3 red); green if it isn't there or not playing.
int AlertLevel() {
    static bool looked = false;
    if (!looked) {
        looked = true;
        rfa::LogDetail(guerrilla_campaign::Running() ? "Walkers: the alert level is guerrilla_campaign's"
                                               : "Walkers: guerrilla_campaign's alert level isn't there; the walkers' cap is green's");
    }
    const int level = GuerrillaCampaign_Alert(nullptr);
    return level >= 0 && level <= 3 ? level : 0;
}

// ---- Navpoints
BYTE* Navpoint(DWORD handle) {
    if (handle == kNone) return nullptr;
    BYTE* object = ObjectByHandle(kWorld, nullptr, handle);
    return object && object[kObjectType] == kNavpointObject && static_cast<signed char>(object[kNavpointType]) == kPedestrianNavpoint ? object : nullptr;
}

// A pedestrian navpoint's links to other pedestrian navpoints.
std::vector<BYTE*> Links(const BYTE* navpoint) {
    std::vector<BYTE*> links;
    const int count = std::clamp(static_cast<int>(*reinterpret_cast<const short*>(navpoint + kNavpointLinkCount)), 0, kMostLinks);
    BYTE* const* list = reinterpret_cast<BYTE* const*>(navpoint + kNavpointLinks);
    for (int i = 0; i < count; ++i)
        if (list[i] && static_cast<signed char>(list[i][kNavpointType]) == kPedestrianNavpoint) links.push_back(list[i]);
    return links;
}

// The orientation for a forward vector on the ground (right, up, forward).
void Facing(V3 forward, float* m) {
    forward.y = 0;
    const float length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    if (length < 0.001f) forward = {0, 0, 1};
    else { forward.x /= length; forward.z /= length; }
    const float values[9] = {forward.z, 0, -forward.x, 0, 1, 0, forward.x, 0, forward.z};
    memcpy(m, values, sizeof(values));
}

// ---- The walkers
struct Walker {
    DWORD human = kNone;
    DWORD target = kNone, from = kNone;  // the navpoint it walks to, and the one it came from
    std::vector<DWORD> old;               // the last navpoints it walked to (ai_pedestrian_info's old_waypoints)
    V3 goal{};
    int made = 0, outOfViewSince = -1;
    bool leaving = false;                 // started to despawn
    bool walked = true;                   // the mod walks it (false once given back to the game's AI)
    int management = 0;                   // who managed it before
    V3 lastPlace{};
    int lastMoveCheck = 0, stuck = 0;
    int team = -1;                        // its team when it was made (a change means it joined a side)
    // Guerrilla's panic (flee_to_safety_goal and flee_action): fleeing from the danger until a while after it is gone,
    // looking again every 1.5-3 s; cornered, it cowers for 2-4 s.
    bool fleeing = false, cowering = false;
    V3 danger{}, fleeGoal{}, fleeAway{};  // the danger, where it runs to, and the way away from the danger
    int fleeUntil = 0, nextFleeLook = 0, cowerUntil = 0, fleeTry = 0;
    // Guerrilla's cower_goal (a vehicle about to run it over) and gawk_goal (a heavy vehicle near): cowering from what
    // comes at it (looked at again every 200-300 ms), and the gawk timer pair (ai_system::gawk_timer).
    bool incoming = false, gawking = false, fetal = false;
    int nextNodeLook = 0;  // when it next looks for an action node to use (3b)
    int nextGreet = 0;     // when it next may greet the player
    int nextHazardLook = 0, gawkWindow = -1, gawkLength = -1;
};
std::vector<Walker> g_walkers;
int g_nextSpawn = 0, g_nextReport = 0, g_lastClock = 0;
// For the reports.
int g_made = 0, g_despawned = 0, g_died = 0, g_fled = 0, g_reachedNodes = 0, g_goToFailures = 0, g_notStreamed = 0, g_noNode = 0, g_leftToOthers = 0;
int g_madeCivilians = 0, g_madeInSafehouses = 0;
// The safehouses' areas (enabled ones): a box (place, right/up/forward axes, least, most along them) or a sphere.
struct SafehouseArea { bool box = false; float place[3]{}, axes[9]{}, least[3]{}, most[3]{}, radius = 0; };
std::vector<SafehouseArea> g_safehouseAreas;
constexpr int kCivilianTeam = 2;

void ReadSafehouseAreas() {
    std::ifstream file(rfa::RootDir() + L"\\mods\\guerrilla_vehicles\\safehouses.txt");
    std::string line;
    bool enabled = false;
    while (std::getline(file, line)) {
        std::istringstream in(line);
        std::string kind, shape, handle, tag;
        in >> kind;
        if (kind == "safehouse") {
            int on = 0;
            in >> handle >> tag >> on;
            enabled = on != 0;
        }
        else if (kind == "area" && enabled) {
            SafehouseArea a;
            in >> shape;
            for (float& v : a.place) in >> v;
            if (shape == "box") {
                a.box = true;
                for (float& v : a.axes) in >> v;
                for (float& v : a.least) in >> v;
                for (float& v : a.most) in >> v;
            }
            else in >> a.radius;
            if (in) g_safehouseAreas.push_back(a);
        }
    }
}

bool InSafehouseArea(const V3& p) {
    for (const SafehouseArea& a : g_safehouseAreas) {
        const float d[3] = {p.x - a.place[0], p.y - a.place[1], p.z - a.place[2]};
        if (!a.box) {
            if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= a.radius * a.radius) return true;
            continue;
        }
        bool inside = true;
        for (int k = 0; k < 3 && inside; ++k) {
            const float along = d[0] * a.axes[3 * k] + d[1] * a.axes[3 * k + 1] + d[2] * a.axes[3 * k + 2];
            inside = along >= a.least[k] && along <= a.most[k];
        }
        if (inside) return true;
    }
    return false;
}
constexpr DWORD kTeam = 0x13B4;
SRWLOCK g_leaveLock = SRWLOCK_INIT;
std::unordered_set<DWORD> g_leaveToGame;  // walkers other mods asked for (GuerrillaPeople_LeaveToGame)
int g_results[9] = {};
int g_cowered = 0, g_alertNow = 0, g_cowersIncoming = 0, g_gawks = 0, g_fetal = 0;
// The cap last worked out, for the reports: how many may walk here now and why.
int g_cap = -1;
char g_capWhy[160] = "";

bool IsOld(const Walker& w, DWORD navpoint) { return std::find(w.old.begin(), w.old.end(), navpoint) != w.old.end(); }
void Remember(Walker& w, DWORD navpoint) {
    w.old.push_back(navpoint);
    if (static_cast<int>(w.old.size()) > kOldWaypoints) w.old.erase(w.old.begin());
}

// The next navpoint from the one reached: Guerrilla's normal choice, as read (build_new_waypoints_normal and
// _directional): a link not walked to lately, ahead of the walker first, at random; a dead end back the way it came.
BYTE* NextNavpoint(const Walker& w, const BYTE* reached, const V3& heading) {
    const auto links = Links(reached);
    if (links.empty()) return nullptr;
    const V3 at = Pos(reached);
    std::vector<BYTE*> ahead, fresh;
    for (BYTE* link : links) {
        if (IsOld(w, Handle(link))) continue;
        fresh.push_back(link);
        const V3 to = Pos(link);
        if ((to.x - at.x) * heading.x + (to.z - at.z) * heading.z > 0) ahead.push_back(link);
    }
    if (!ahead.empty()) return ahead[RandomBelow(static_cast<int>(ahead.size()))];
    if (!fresh.empty()) return fresh[RandomBelow(static_cast<int>(fresh.size()))];
    return links[RandomBelow(static_cast<int>(links.size()))];
}

// The pedestrian navpoint nearest a place within 30 m (PED_NODE_SEARCH_AREA_RADIUS), as civilian_pick_new_ped_node
// falls back to; null if none.
BYTE* NearestNavpoint(const V3& place, DWORD except) {
    BYTE* best = nullptr;
    float bestDistance = 30.0f;
    const int count = NavpointCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* navpoint = NavpointAt(kWorld, nullptr, i);
        if (!navpoint || static_cast<signed char>(navpoint[kNavpointType]) != kPedestrianNavpoint || Handle(navpoint) == except) continue;
        const float d = Distance(Pos(navpoint), place);
        if (d < bestDistance) { bestDistance = d; best = navpoint; }
    }
    return best;
}

void Head(Walker& w, BYTE* navpoint) {
    w.target = navpoint ? Handle(navpoint) : kNone;
    if (navpoint) w.goal = Pos(navpoint);
}

// Gives a walker back to the game's AI (its manager as before).
void EndCower(Walker& w, BYTE* human);

void GiveBack(Walker& w, BYTE* human) {
    ActionsWalkerStop(w.human);
    EndCower(w, human);
    w.incoming = w.gawking = false;
    w.walked = false;
    *reinterpret_cast<int*>(human + kManagement) = w.management;
}

void SitOut(BYTE* human) {
    BYTE* ai = *reinterpret_cast<BYTE**>(human + kNpcAi);
    if (ai) ai[kYield] |= 2;
}

// One frame of a walker's walk.
// ---- Guerrilla's panic (piece 5a, 2026-11-13; the user: "Reactions and panic first")
// flee_to_safety_goal (RM get_fitness 0x66E260: 0x170C, high, whenever ai_get_flee_to_safety_belief RM 0x66DC10 gives a
// belief that isn't of a vehicle, AI_BELIEF_VEHICLE 13) and its flee_action (RM do_action 0x6AA5D0): the person runs from
// the danger (combat_maneuver_get_retreat_dir_unified: away from it over the nav mesh, set_direct_movement_to_pos),
// looks again every 1.5-3 s (set_rand 1500, 3000), and when it can't get away it cowers (play_random_cower_animation:
// one of the cower states, 2-4 s, set_rand 2000, 4000). Here the belief is Armageddon's own (0x483C90, the same
// function; or 0x483B00 for an enemy to flee), the way away the AI's go-to at sprint toward a point 25 m from the danger
// (straight away first, then 35, 70 and 110 degrees off to either side), and the cower an animation state override of
// the civilians' cower states (cower stand, cower crouch, crouch cower 1 and 2). Guerrilla's ambient people who fled
// leave once out of view (npc_flags ambient_flee, flee_despawn_action), so a walker that fled leaves 5 s after the
// danger. The commit (RM 0x6ABA80) also gives a civilian the AFRAID animation variant (7), which Guerrilla's civilian
// sets have nothing for (no set is of that variant), and within 20 m of the player says VOICE_LINE_PANIC (125), or
// VOICE_LINE_COMBAT_ALLY_FLEE (58) when the EDF are the shooters: not said yet, Guerrilla's people's voices aren't
// ported. The vehicles (cower_goal, gawk_goal) are below.
constexpr float kFleeDistance = 25.0f;
constexpr int kFleeLingers = 5000, kFleeLookLeast = 1500, kFleeLookMost = 3000, kCowerLeast = 2000, kCowerMost = 4000;
// npc::play_random_cower_animation (RM 0xA83CB0; flee_action calls it with false): one of its states 10-13 at random,
// "cower stand" through "stand to cower" (action 356), "cower crouch" standing, "crouch cower 1" and "2" crouching;
// the blend 0.3. cower_incoming_action::enter (RM 0x5F27E0): from above (the way to it more than 0.5 up) "cower stand",
// else "stand cower incoming idle" (state 14) through "stand cower incoming front" (352: within 45 degrees of its
// facing), "... right" (354: on its right) or "... left" (353); it stands. Guerrilla's civilian sets haven't the
// incoming ones: their parent, the soldiers' Male, has them (male_rfl_std_cwr_incomng_*).
struct CowerClip { const char* state; const char* action; int stance; };
const CowerClip kRandomCowers[] = {{"cower stand", "stand to cower", kStandingStance},
                                   {"cower crouch", nullptr, kStandingStance},
                                   {"crouch cower 1", nullptr, kCrouchingStance},
                                   {"crouch cower 2", nullptr, kCrouchingStance}};

// Plays a cower and holds its state (Guerrilla's human_override_default_animation_state with set_state).
bool PlayCower(Walker& w, BYTE* human, const CowerClip& clip) {
    const int state = FindState(clip.state);
    if (state < 0) {
        RFA_LOG_FIRST(4, "Walkers: the game has no \"%s\" animation state; NPC 0x%08X stands", clip.state, w.human);
        return false;
    }
    const int action = clip.action ? FindAction(clip.action) : -1;
    if (clip.action && action < 0) RFA_LOG_FIRST(4, "Walkers: the game has no \"%s\" animation action; NPC 0x%08X cowers without it", clip.action, w.human);
    SetStance(human, clip.stance);
    SetStateOverride(human, state);
    if (BYTE* animation = *reinterpret_cast<BYTE**>(human + kHumanAnimation))
        SetAnimState(animation, nullptr, {state, kNoAlias}, {action, kNoAlias}, kCowerBlend, false);
    w.cowering = true;
    return true;
}

// Ends a cower (flee_action::cleanup, cower_incoming_action::cleanup: the override cleared; it stands).
void EndCower(Walker& w, BYTE* human) {
    if (w.cowering) {
        SetStateOverride(human, -1);
        SetStance(human, kStandingStance);
    }
    w.cowering = false;
}

// The danger a walker flees, from Guerrilla's flee-to-safety belief (or an enemy to flee), or null.
const BYTE* DangerBelief(BYTE* human) {
    const BYTE* belief = FleeToSafetyBelief(human);
    if (belief && static_cast<char>(belief[kBeliefType]) == kVehicleBelief) belief = nullptr;
    return belief ? belief : EnemyFleeBelief(human);
}

const float kFleeTurns[] = {0.0f, 35.0f, -35.0f, 70.0f, -70.0f, 110.0f, -110.0f};  // degrees off straight away
constexpr int kFleeWays = sizeof(kFleeTurns) / sizeof(kFleeTurns[0]);

bool NoWay(int result) { return result == kNoPath || result == kStartOffMesh || result == kDestinationOffMesh; }

// Cornered: it cowers where it is.
void Cower(Walker& w, BYTE* human, int now) {
    if (!PlayCower(w, human, kRandomCowers[std::rand() % 4])) {
        w.nextFleeLook = now + kCowerLeast;
        return;
    }
    w.cowerUntil = now + kCowerLeast + std::rand() % (kCowerMost - kCowerLeast + 1);
    ++g_cowered;
    RFA_LOG_DETAIL_FIRST(16, "Walkers: NPC 0x%08X can't get away from the danger at (%.1f, %.1f, %.1f); it cowers, as Guerrilla's flee_action (%d so far)",
                  w.human, w.danger.x, w.danger.y, w.danger.z, g_cowered);
}

// Runs toward the next way away from the danger, from w.fleeTry on. The AI's go-to takes a new place at once (0) and
// answers that there's no way (6, 7 or 8) then or on a later frame, so a way it turns down moves on to the next one;
// with none left, it cowers.
void FleeOn(Walker& w, BYTE* human, BYTE* ai, int now) {
    const V3 at = Pos(human);
    for (; w.fleeTry < kFleeWays; ++w.fleeTry) {
        const float r = kFleeTurns[w.fleeTry] * 0.01745329f, c = std::cos(r), s = std::sin(r);
        const float dx = w.fleeAway.x * c - w.fleeAway.z * s, dz = w.fleeAway.x * s + w.fleeAway.z * c;
        w.fleeGoal = {at.x + dx * kFleeDistance, at.y, at.z + dz * kFleeDistance};
        if (!NoWay(GoTo(ai, nullptr, &w.fleeGoal.x, kSprint, kStanding, kLowPriority, kNone, kNone, false))) return;
    }
    Cower(w, human, now);
}

void Flee(Walker& w, BYTE* human, BYTE* ai, int now) {
    SitOut(human);
    if (w.cowering) {
        if (now < w.cowerUntil) return;
        EndCower(w, human);
        w.nextFleeLook = 0;
    }
    if (now < w.nextFleeLook) {
        if (w.fleeTry < kFleeWays && NoWay(GoTo(ai, nullptr, &w.fleeGoal.x, kSprint, kStanding, kLowPriority, kNone, kNone, false))) {
            ++w.fleeTry;
            FleeOn(w, human, ai, now);
        }
        return;
    }
    // A new look (Guerrilla's flee_action looks again every 1.5-3 s): away from where the danger is now.
    w.nextFleeLook = now + kFleeLookLeast + std::rand() % (kFleeLookMost - kFleeLookLeast + 1);
    const V3 at = Pos(human);
    float dx = at.x - w.danger.x, dz = at.z - w.danger.z;
    const float length = std::sqrt(dx * dx + dz * dz);
    if (length < 0.1f) {
        const float a = static_cast<float>(std::rand()) / RAND_MAX * 6.2831853f;
        dx = std::cos(a), dz = std::sin(a);
    }
    else dx /= length, dz /= length;
    w.fleeAway = {dx, 0, dz};
    w.fleeTry = 0;
    FleeOn(w, human, ai, now);
}

// ---- Guerrilla's reactions to a vehicle (piece 5b, 2026-11-13). flee_to_safety_goal leaves a vehicle's belief to:
// - cower_goal (RM get_fitness 0x66E380, fitness 4000): not for one in a vehicle; the AI's hazard is a vehicle (or an
//   explosive projectile, which Armageddon's flee belief already puts first); not the local player's car if the
//   person is his friend (local_player_is_friendly); get_hazard_physics_info (RM 0x661CA0) says it will hit; and it
//   passes within half the vehicle's width + 0.5 m, or the vehicle is heavy (ai_perception::vehicle_is_heavy RM
//   0x5EF470: a walker, or vehicle_is_tank RM 0xB68D90: its info's special type Tank or Artillery Tank).
// - get_vehicle_hazard_info (RM 0x6616E0) for a vehicle on the ground (get_rolling_physics_info RM 0x5EAF90):
//   - the local player's car near his friend (within 10 m, slower than 15 m/s) counts as 3 times as fast (15 at most);
//   - nothing if the person is above it (height over flat distance > 0.5), if it moves away, or slower than 4 m/s on
//     the flat;
//   - it hits if its straight line passes within 1.2 x half its width (the time to pass: the distance along its way
//     over its flat speed);
//   - a heavy vehicle near a civilian, or the player's car near an ambient person, has a cone ahead of it: within
//     min(speed x 2.5, 22.5) m, as wide as 8.75 m x (the distance along it / that reach);
//   - only within 1.5 s (2.5 s: the player's car that isn't heavy, or a heavy one near a civilian).
// - cower_incoming_action (RM 0x5F27E0, 0x66DAA0): it stops and cowers, looking again every 200-300 ms.
// - gawk_goal (RM 0x6376C0, fitness 150) with get_gawk_target (RM 0x637570): a heavy vehicle it saw lately within 20 m
//   (25 m once gawking), not while it is in a tank, walker or flyer; gawk_action stops it and it stands looking at
//   the vehicle. Its timer pair (commit RM 0x6A0F60): the first gawk starts a 30-45 s window and a 5-15 s length;
//   it may gawk while the window runs, and not once the window and the length are over.
// Armageddon's pieces: the vehicle from Armageddon's flee-to-safety belief of a vehicle (kind 14, 0x483C90: it asks
// for one after projectiles); the object's last known box (+0x7C and +0x88, Guerrilla's +0x80, +0x8C: its width
// along x, as Guerrilla's get_bbox); the body's velocity (0x778440, 0x776770); the driver (0x603AE0); the
// disposition (0x8A04D0, 3 friendly or better); the vehicle's subtype (0x423530: 6 flyer, 8 walker) and its info's
// special type (+0x3DA8, its +0x30: 3 Tank, 4 Artillery Tank, as Guerrilla's).
// Not ported: a vehicle in the air (Guerrilla's get_ballistic_physics_info and get_straight_trajectory_physics_info,
// behind a virtual call whose Armageddon slot isn't known), the gawk's look at the vehicle (set_attention: Armageddon's
// look-at isn't found yet), and the cower-to-stand clip (character_instance::play_action: Armageddon's not found yet).
constexpr DWORD kObjectBody = 0x4C, kObjectBoxLeast = 0x7C, kObjectBoxMost = 0x88, kVehicleInfo = 0x3DA8, kVehicleSpecialType = 0x30;
constexpr char kVehicleObject = 3;
constexpr int kFlyerSubtype = 6, kWalkerSubtype = 8, kTankType = 3, kArtilleryTankType = 4, kFriendly = 3;
constexpr float kHumanCentre = 1.0f;  // human::get_center_pos, about its middle
constexpr int kHazardLookLeast = 200, kHazardLookMost = 300;
constexpr int kGawkWindowLeast = 30000, kGawkWindowMost = 45000, kGawkLengthLeast = 5000, kGawkLengthMost = 15000;
constexpr float kGawkDistance = 20.0f, kGawkingDistance = 25.0f;
constexpr int kNodeFirstLook = 5000, kNodeLookEvery = 2000, kNodeAgainLeast = 30000, kNodeAgainMost = 60000;
constexpr float kGreetDistance = 8.0f;
auto const VehicleSubtype = reinterpret_cast<int(__fastcall*)(BYTE* vehicle, void*)>(0x423530);
auto const HavokBody = reinterpret_cast<BYTE*(__cdecl*)(DWORD handle)>(0x778440);
auto const BodyVelocity = reinterpret_cast<void(__cdecl*)(BYTE* body, V3* out)>(0x776770);
auto const SeatOccupant = reinterpret_cast<BYTE*(__cdecl*)(BYTE* vehicle, int seat)>(0x603AE0);
auto const Disposition = reinterpret_cast<int(__cdecl*)(BYTE* human, BYTE* other)>(0x8A04D0);

bool HeavyVehicle(BYTE* vehicle, int subtype) {
    if (subtype == kWalkerSubtype) return true;
    const BYTE* info = *reinterpret_cast<BYTE* const*>(vehicle + kVehicleInfo);
    const int special = info ? *reinterpret_cast<const int*>(info + kVehicleSpecialType) : 0;
    return special == kTankType || special == kArtilleryTankType;
}

// Guerrilla's hazard_physics_info, for a vehicle on the ground.
struct Hazard {
    float halfWidth = 0, timeToPass = -1, distanceAtPass = -1;
    bool heavy = false, playerDrives = false, willImpact = false;
};

bool VehicleHazard(BYTE* human, BYTE* vehicle, BYTE* player, int team, Hazard& h) {
    const int subtype = VehicleSubtype(vehicle, nullptr);
    if (subtype == kFlyerSubtype) return false;  // in the air: Guerrilla's ballistic path, not ported
    const V3 least = *reinterpret_cast<const V3*>(vehicle + kObjectBoxLeast), most = *reinterpret_cast<const V3*>(vehicle + kObjectBoxMost);
    h.halfWidth = (most.x - least.x) * 0.5f;
    if (!(h.halfWidth > 0.2f && h.halfWidth < 25.0f)) {
        RFA_LOG_DETAIL_FIRST(4, "Walkers: vehicle 0x%08X's box (%.1f to %.1f across) isn't a vehicle's size; no cower from it", Handle(vehicle), least.x, most.x);
        return false;
    }
    BYTE* body = HavokBody(*reinterpret_cast<const DWORD*>(vehicle + kObjectBody));
    if (!body) return false;
    V3 v{};
    BodyVelocity(body, &v);
    const V3 at = Pos(human), car = Pos(vehicle);
    const V3 o{at.x - car.x, at.y + kHumanCentre - car.y, at.z - car.z};  // the vehicle to the person
    const float flat = std::sqrt(o.x * o.x + o.z * o.z);
    float speed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    h.playerDrives = player && SeatOccupant(vehicle, 0) == player;
    if (h.playerDrives && Disposition(human, player) >= kFriendly && flat * flat < 100.0f && speed < 15.0f) {
        const float faster = std::min(speed * 3.0f, 15.0f);
        if (faster <= 0) v = {0, 0, 0};
        else { const float k = faster / speed; v = {v.x * k, v.y * k, v.z * k}; }
        speed = faster;
    }
    h.heavy = HeavyVehicle(vehicle, subtype);
    const bool civilian = team == kCivilianTeam;
    // get_rolling_physics_info
    if (flat > 0 && o.y / flat > 0.5f) return false;
    if (v.x * o.x + v.y * o.y + v.z * o.z < 0) return false;
    const float flatSpeed = std::sqrt(v.x * v.x + v.z * v.z);
    if (flatSpeed < 4.0f) return false;
    const float along = (o.x * v.x + o.z * v.z) / flatSpeed;
    h.timeToPass = along / flatSpeed;
    h.distanceAtPass = std::sqrt(std::max(0.0f, flat * flat - along * along));
    h.willImpact = h.halfWidth * 1.2f >= h.distanceAtPass;
    // The cone ahead of the player's car, or of a heavy vehicle near a civilian.
    if ((h.playerDrives && !h.willImpact && !h.heavy) || (civilian && h.heavy)) {
        const float reach = std::min(speed * 2.5f, 22.5f), ahead = h.timeToPass * speed;
        if (reach >= ahead && reach > 0 && ahead / reach * 8.75f >= h.distanceAtPass) h.willImpact = true;
    }
    const float soonest = h.playerDrives ? (h.heavy ? 1.5f : 2.5f) : (civilian && h.heavy ? 2.5f : 1.5f);
    if (h.willImpact && h.timeToPass > soonest) h.willImpact = false;
    return true;
}

// The vehicle of Armageddon's flee-to-safety belief, when that is a vehicle's.
BYTE* BeliefVehicle(BYTE* human) {
    const BYTE* belief = FleeToSafetyBelief(human);
    if (!belief || static_cast<char>(belief[kBeliefType]) != kVehicleBelief) return nullptr;
    BYTE* object = ObjectByHandle(kWorld, nullptr, *reinterpret_cast<const DWORD*>(belief + kBeliefHandle));
    return object && static_cast<char>(object[kObjectType]) == kVehicleObject ? object : nullptr;
}

// cower_incoming_action::enter's choice of clip, by where the hazard is.
CowerClip IncomingCower(BYTE* human, BYTE* hazard) {
    const V3 at = Pos(human), from = Pos(hazard);
    V3 d{from.x - at.x, from.y - at.y, from.z - at.z};
    const float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (length > 0 && d.y / length >= 0.5f) return {"cower stand", nullptr, kStandingStance};
    const float flat = std::sqrt(d.x * d.x + d.z * d.z);
    if (flat > 0) d = {d.x / flat, 0, d.z / flat};
    const V3 right = *reinterpret_cast<const V3*>(human + kOrientation), forward = *reinterpret_cast<const V3*>(human + kOrientation + 24);
    const char* action = d.x * forward.x + d.z * forward.z >= 0.70710677f ? "stand cower incoming front"
                         : d.x * right.x + d.z * right.z >= 0 ? "stand cower incoming right"
                                                              : "stand cower incoming left";
    return {"stand cower incoming idle", action, kStandingStance};
}

void StandStill(BYTE* human, BYTE* ai) {
    const V3 at = Pos(human);
    GoTo(ai, nullptr, &at.x, kWalk, kStanding, kLowPriority, kNone, kNone, false);
}

// Cower_goal and gawk_goal; true while one of them has the walker.
bool VehicleReaction(Walker& w, BYTE* human, BYTE* ai, int now) {
    if (w.incoming && now < w.nextHazardLook) {
        SitOut(human);
        return true;
    }
    BYTE* vehicle = BeliefVehicle(human);
    BYTE* player = Player();
    const int team = *reinterpret_cast<const int*>(human + kTeam);
    Hazard h;
    const bool hazard = vehicle && VehicleHazard(human, vehicle, player, team, h);
    const bool friendsCar = h.playerDrives && Disposition(human, player) >= kFriendly;
    const bool cower = hazard && h.willImpact && !friendsCar && (h.distanceAtPass <= h.halfWidth + 0.5f || h.heavy);
    if (cower) {
        SitOut(human);
        if (!w.incoming) {
            StandStill(human, ai);
            PlayCower(w, human, IncomingCower(human, vehicle));
            w.incoming = true;
            w.gawking = false;
            ++g_cowersIncoming;
            RFA_LOG_DETAIL_FIRST(16, "Walkers: NPC 0x%08X cowers from vehicle 0x%08X coming at it (passes %.1f m off in %.1f s, half its width %.1f m%s), as Guerrilla's cower_goal (%d so far)",
                          w.human, Handle(vehicle), h.distanceAtPass, h.timeToPass, h.halfWidth, h.heavy ? ", heavy" : "", g_cowersIncoming);
        }
        else StandStill(human, ai);
        w.nextHazardLook = now + kHazardLookLeast + std::rand() % (kHazardLookMost - kHazardLookLeast + 1);
        return true;
    }
    if (w.incoming) {  // it has passed
        EndCower(w, human);
        w.incoming = false;
    }
    // gawk_goal: a heavy vehicle near, within its window.
    const bool inWindow = w.gawkWindow >= 0 && now < w.gawkWindow;
    const bool over = !inWindow && w.gawkLength >= 0 && now >= w.gawkLength;
    const float within = w.gawking ? kGawkingDistance : kGawkDistance;
    const bool target = vehicle && h.heavy && Distance(Pos(human), Pos(vehicle)) < within;
    if (!over && target) {
        if (w.gawkWindow < 0 || now >= w.gawkWindow) {
            w.gawkWindow = now + kGawkWindowLeast + std::rand() % (kGawkWindowMost - kGawkWindowLeast + 1);
            w.gawkLength = now + kGawkLengthLeast + std::rand() % (kGawkLengthMost - kGawkLengthLeast + 1);
        }
        if (!w.gawking) {
            w.gawking = true;
            ++g_gawks;
            RFA_LOG_DETAIL_FIRST(12, "Walkers: NPC 0x%08X stops to gawk at heavy vehicle 0x%08X, %.1f m away, as Guerrilla's gawk_goal (%d so far)", w.human, Handle(vehicle),
                          Distance(Pos(human), Pos(vehicle)), g_gawks);
        }
        SitOut(human);
        StandStill(human, ai);
        return true;
    }
    w.gawking = false;
    return false;
}

// ---- Guerrilla's fetal position (piece 5b). human_combat_unified_melee_hit (RM 0xAD13B0; Armageddon's 0x877BF0)
// starts a person's beaten_up_timer when a melee hit knocks it down (Armageddon's human +0x1014). When a civilian
// gets up from that ragdoll within 10 s (human_ragdoll_get_up RM 0xAD50D0, Armageddon's 0x89A510, both asking
// ai_system::should_go_to_fetal_position: RM 0x5DE970, Armageddon's 0x4307A0), it lies in the fetal state instead
// (Armageddon's state 9, stance 2) and sets in_fetal_position (hflags+8 bit 30; Armageddon's human +0xB8). Armageddon
// kept all of that but not Guerrilla's fetal_position_goal (RM 0x5F33B0, 110,000: above everything) and its action
// (RM 0x5F2AB0): it stays so while should_stay_in_fetal_position (RM 0x5DE930, Armageddon's 0x430750: the beating
// within 10 s), then the flag is cleared and it gets up. Nothing of Armageddon's clears the flag, so a walker would
// lie there for good. Its line under half health (VOICE_LINE_PATROL_BEAT_CIVILIAN_SUBMISSIVE_RESPONSE, 34) waits
// for the people's voices.
constexpr DWORD kHumanFlags3 = 0xB8, kInFetalPosition = 0x40000000;
auto const StayInFetal = reinterpret_cast<bool(__fastcall*)(BYTE* ai, void*)>(0x430750);

bool Fetal(Walker& w, BYTE* human, BYTE* ai) {
    DWORD& flags = *reinterpret_cast<DWORD*>(human + kHumanFlags3);
    if (!(flags & kInFetalPosition)) {
        w.fetal = false;
        return false;
    }
    SitOut(human);
    if (!w.fetal) {
        w.fetal = true;
        ++g_fetal;
        RFA_LOG_DETAIL_FIRST(12, "Walkers: NPC 0x%08X was beaten down and lies in the fetal position, as Guerrilla's fetal_position_goal (%d so far)", w.human, g_fetal);
    }
    if (StayInFetal(ai, nullptr)) return true;
    flags &= ~kInFetalPosition;
    SetStance(human, kStandingStance);
    w.fetal = false;
    RFA_LOG_DETAIL_FIRST(12, "Walkers: NPC 0x%08X gets up from the fetal position (10 s after the beating)", w.human);
    return false;
}

void Walk(Walker& w, BYTE* human, int now) {
    BYTE* ai = *reinterpret_cast<BYTE**>(human + kNpcAi);
    if (!ai) return;
    if (Fetal(w, human, ai)) return;
    if (const BYTE* belief = DangerBelief(human)) {
        w.danger = *reinterpret_cast<const V3*>(belief + kBeliefPosition);
        if (!w.fleeing) {
            w.fleeing = true;
            w.nextFleeLook = 0;
            ++g_fled;
            // Guerrilla's flee_to_safety_goal: a guerrilla's com_ally_combat_flee, anyone else's voc_panic (flee_action).
            VoiceSay(human, *reinterpret_cast<const int*>(human + kTeam) == 0 ? "com_ally_combat_flee" : "voc_panic");
            RFA_LOG_DETAIL_FIRST(20, "Walkers: NPC 0x%08X flees from a danger at (%.1f, %.1f, %.1f) (Guerrilla's flee_to_safety_goal; belief kind %d)",
                          w.human, w.danger.x, w.danger.y, w.danger.z, static_cast<int>(static_cast<char>(belief[kBeliefType])));
        }
        w.fleeUntil = now + kFleeLingers;
    }
    if (w.fleeing) {
        if (now < w.fleeUntil || w.cowering) {
            Flee(w, human, ai, now);
            return;
        }
        w.fleeing = false;
        w.leaving = true;  // Guerrilla's ambient people who fled leave once out of view
    }
    if (VehicleReaction(w, human, ai, now)) return;
    // Guerrilla's use_action_node_goal for an ambient person (3b): at alert green or yellow it takes a free node within
    // 30 m whose type allows ambient people (actions.cpp walks it there and plays it), then walks on. Its fitness
    // (100) is above wandering's (80); how soon a person may use one again isn't read yet: here a first look 5-15 s
    // after it's made, then 30-60 s after each use, every 2 s between.
    if (ActionsWalkerBusy(w.human)) return;
    // A walker near the player greets him (Guerrilla's greet_player_action: amb_pc_greet_morale_*, by the district's
    // morale; guerrilla_voices keeps its cooldowns and chance).
    if (BYTE* player = Player(); player && now >= w.nextGreet && !w.leaving && g_alertNow <= 1 && Distance(Pos(human), Pos(player)) < kGreetDistance) {
        w.nextGreet = now + 3000;
        VoiceSay(human, VoiceGreetLine());
    }
    if (w.nextNodeLook == 0) w.nextNodeLook = now + kNodeFirstLook + std::rand() % kNodeFirstLook * 2;
    if (!w.leaving && g_alertNow <= 1 && now >= w.nextNodeLook) {
        w.nextNodeLook = now + kNodeLookEvery;
        const V3 here = Pos(human);
        if (ActionsWalkerStart(w.human, &here.x)) {
            w.nextNodeLook = now + kNodeAgainLeast + std::rand() % (kNodeAgainMost - kNodeAgainLeast + 1);
            return;
        }
    }
    SitOut(human);
    if (w.target == kNone && w.leaving) return;  // nowhere to walk: it stands until it's out of view
    const V3 at = Pos(human);
    BYTE* target = Navpoint(w.target);
    if (!target || FlatDistance(at, w.goal) < kReached) {
        BYTE* reached = target;
        V3 heading{0, 0, 0};
        if (BYTE* from = Navpoint(w.from)) { const V3 f = Pos(from); heading = {w.goal.x - f.x, 0, w.goal.z - f.z}; }
        if (reached) {
            ++g_reachedNodes;
            Remember(w, w.target);
            w.from = w.target;
        }
        BYTE* next = reached ? NextNavpoint(w, reached, heading) : NearestNavpoint(at, kNone);
        Head(w, next);
        w.stuck = 0;
        if (!next) {  // nowhere to go: it stands, and goes once out of view
            w.leaving = true;
            RFA_LOG_DETAIL_FIRST(10, "Walkers: NPC 0x%08X has no pedestrian navpoint to walk to at (%.1f, %.1f, %.1f); it leaves once out of view", w.human, at.x, at.y, at.z);
            return;
        }
    }
    // Guerrilla's pedestrian_panicked_goal (RM get_fitness 0x656330: ambient people, alert red, or during an air bomb) walks
    // the same network at sprint (wander_panicked_action RM 0x691170: civilian_ped_do_action_common with HMS_SPRINT).
    const int movement = g_alertNow >= 3 ? kSprint : kWalk;
    const int result = GoTo(ai, nullptr, &w.goal.x, movement, kStanding, kLowPriority, kNone, kNone, false);
    if (result >= 0 && result < 9) {
        if (g_results[result]++ == 0)
            rfa::LogDetail("Walkers: the AI's go-to first answered %d for NPC 0x%08X, to navpoint 0x%08X at (%.1f, %.1f, %.1f) from (%.1f, %.1f, %.1f)", result, w.human,
                     w.target, w.goal.x, w.goal.y, w.goal.z, at.x, at.y, at.z);
    }
    if (result == kNoPath || result == kStartOffMesh || result == kDestinationOffMesh) {
        ++g_goToFailures;
        Remember(w, w.target);  // not that one again soon
        BYTE* from = Navpoint(w.from);
        Head(w, from ? NextNavpoint(w, from, {0, 0, 0}) : NearestNavpoint(at, w.target));
        return;
    }
    // Stuck: not 0.5 m on in 4 s while on its way, then another way (three times, then it leaves).
    if (now - w.lastMoveCheck >= 4000) {
        if (Distance(at, w.lastPlace) < 0.5f && result == 0) {
            ++w.stuck;
            Remember(w, w.target);
            BYTE* from = Navpoint(w.from);
            Head(w, from ? NextNavpoint(w, from, {0, 0, 0}) : NearestNavpoint(at, w.target));
            if (w.stuck >= 3 && !w.leaving) {
                w.leaving = true;
                RFA_LOG_DETAIL_FIRST(20, "Walkers: NPC 0x%08X got stuck three times near (%.1f, %.1f, %.1f); it leaves once out of view", w.human, at.x, at.y, at.z);
            }
        }
        w.lastPlace = at;
        w.lastMoveCheck = now;
    }
}

// Guerrilla's should_despawn_npc, and can_delete_despawning_npc for a leaving one.
bool ShouldGo(const Walker& w, float away, int now) {
    const int unseen = w.outOfViewSince < 0 ? 0 : now - w.outOfViewSince;
    if (w.leaving) return (away > kNearest && unseen >= kUnseen) || away > kFarthest;
    if (away <= kNearest) return false;
    const int age = now - w.made;
    if (age < kYoung || unseen < kUnseen) return false;
    return age >= kOld || away > kFarthest;
}

// Which quarter round the player a place is in (the player's front, right, back, left).
int Side(const V3& place, const V3& player, const V3& forward) {
    const float dx = place.x - player.x, dz = place.z - player.z;
    const float ahead = dx * forward.x + dz * forward.z, right = dx * forward.z - dz * forward.x;
    if (std::fabs(ahead) >= std::fabs(right)) return ahead >= 0 ? 0 : 2;
    return right >= 0 ? 1 : 3;
}

// Guerrilla's spawn step: at the cap less 2 one starts leaving; under the cap one more.
void SpawnStep(const V3& at, const V3& forward, int now) {
    static const char* const kDensity[4] = {"none", "low", "medium", "high"};
    static const char* const kAlert[4] = {"green", "yellow", "orange", "red"};
    const Zone* zone = ZoneAt(at);
    const int hour = GameHour();
    const bool night = hour >= 18 || hour < 6;
    const int density = zone ? (night ? zone->night : zone->day) : 0;
    const int alert = AlertLevel();
    const int cap = kCaps[alert][density];
    // A line whenever the cap changes (another zone, day and night, the alert level).
    char why[160];
    if (zone)
        sprintf_s(why, "zone %02d_%02d (%s): %s by %s, %d:00, alert %s", zone->col, zone->row, zone->entry.c_str(), kDensity[density], night ? "night" : "day", hour,
                  kAlert[alert]);
    else
        sprintf_s(why, "no walkers in this zone (its ambient_spawn has none), %d:00", hour);
    static const Zone* lastZone = nullptr;
    static int lastKey = -1;
    const int key = density * 100 + alert * 10 + (night ? 1 : 0);
    if (cap != g_cap || zone != lastZone || key != lastKey)
        RFA_LOG_DETAIL_FIRST(60, "Walkers: the cap is now %d (%s); %d out", cap, why, static_cast<int>(g_walkers.size()));
    lastZone = zone;
    lastKey = key;
    g_cap = cap;
    strcpy_s(g_capWhy, why);
    if (!zone) return;  // no ambient_spawn with walkers here: Guerrilla's spawner does nothing either
    const int count = static_cast<int>(g_walkers.size());
    // The crowd changes: from the cap less 2, the farthest one 20 m or more away starts leaving (one at a time).
    const int nearCap = (std::max)(cap - 2, 0);
    if (nearCap <= count && std::none_of(g_walkers.begin(), g_walkers.end(), [](const Walker& w) { return w.leaving; })) {
        Walker* farthest = nullptr;
        float farthestAway = kNearest;
        for (Walker& w : g_walkers)
            if (BYTE* human = HumanByHandle(kWorld, nullptr, w.human)) {
                const float away = Distance(Pos(human), at);
                if (away > farthestAway) { farthestAway = away; farthest = &w; }
            }
        if (farthest) farthest->leaving = true;
    }
    if (count >= cap) return;
    // A quarter of the cap at most toward one side (max_ped_count_quad: a quarter, rounded up, at least 1).
    const int quarter = (std::max)(1, (cap + 3) / 4);
    int bySide[4] = {};
    std::vector<V3> places;
    for (const Walker& w : g_walkers)
        if (BYTE* human = HumanByHandle(kWorld, nullptr, w.human)) {
            places.push_back(Pos(human));
            ++bySide[Side(places.back(), at, forward)];
        }
    // The navpoints allowed: pedestrian ones 20 to 100 m away, out of view, none of the walkers within 3 m.
    std::vector<BYTE*> nodes;
    const int navpoints = NavpointCount(kWorld, nullptr);
    for (int i = 0; i < navpoints; ++i) {
        BYTE* navpoint = NavpointAt(kWorld, nullptr, i);
        if (!navpoint || static_cast<signed char>(navpoint[kNavpointType]) != kPedestrianNavpoint) continue;
        const V3 place = Pos(navpoint);
        const float away = Distance(place, at);
        if (away < kNearest || away > kFarthestNode || bySide[Side(place, at, forward)] >= quarter) continue;
        if (std::any_of(places.begin(), places.end(), [&](const V3& p) { return Distance(p, place) < kApart; })) continue;
        const V3 head{place.x, place.y + 1.0f, place.z};
        if (InView(&head.x, 1.0f)) continue;
        nodes.push_back(navpoint);
    }
    if (nodes.empty()) {
        ++g_noNode;
        RFA_LOG_DETAIL_FIRST(5, "Walkers: no pedestrian navpoint 20-100 m from the player out of view at (%.0f, %.0f, %.0f) (zone %02d_%02d, cap %d)", at.x, at.y, at.z,
                      zone->col, zone->row, cap);
        return;
    }
    BYTE* node = nodes[RandomBelow(static_cast<int>(nodes.size()))];
    const auto* civilians = CiviliansOf(zone->resource);
    if (!civilians) return;
    const auto links = Links(node);
    BYTE* first = links.empty() ? nullptr : links[RandomBelow(static_cast<int>(links.size()))];
    const V3 place = Pos(node);
    float orientation[9];
    Facing(first ? V3{Pos(first).x - place.x, 0, Pos(first).z - place.z} : forward, orientation);
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::string& name = (*civilians)[RandomBelow(static_cast<int>(civilians->size()))];
        BYTE* info = HumanInfo(name.c_str());
        BYTE* human = info ? MakeHuman(info, &place.x, orientation, nullptr) : nullptr;
        if (!human) {
            ++g_notStreamed;
            RFA_LOG_DETAIL_FIRST(10, "Walkers: %s (%s) %s", name.c_str(), zone->resource.c_str(), info ? "isn't streamed in yet" : "isn't one of the game's characters");
            continue;
        }
        Walker w;
        w.human = Handle(human);
        w.from = Handle(node);
        w.old.push_back(w.from);
        Head(w, first);
        w.made = now;
        w.lastPlace = place;
        w.lastMoveCheck = now;
        w.management = *reinterpret_cast<const int*>(human + kManagement);
        if (InSafehouseArea(place)) ++g_madeInSafehouses;  // Guerrilla's use_default_team: its character's own team
        else if (*reinterpret_cast<const int*>(human + kTeam) != kCivilianTeam) {
            SetTeam(human, nullptr, kCivilianTeam);
            ++g_madeCivilians;
        }
        w.team = *reinterpret_cast<const int*>(human + kTeam);
        *reinterpret_cast<int*>(human + kManagement) = kAmbient;
        GiveBehavior(human, name, place, *zone);  // Guerrilla's ambient behaviour (its variant and prop)
        g_walkers.push_back(w);
        ++g_made;
        RFA_LOG_DETAIL_FIRST(10, "Walkers: %s walks from navpoint 0x%08X at (%.1f, %.1f, %.1f), %.0f m away, in zone %02d_%02d (%s: %s by %s, alert %s, cap %d); %d out",
                      name.c_str(), w.from, place.x, place.y, place.z, Distance(place, at), zone->col, zone->row, zone->entry.c_str(), kDensity[density],
                      night ? "night" : "day", kAlert[alert], cap, static_cast<int>(g_walkers.size()));
        return;
    }
}

}  // namespace

void StartAmbient() {
    if (rfa::IniInt(L"GuerrillaPeople", L"Walkers", 1) == 0) {
        rfa::LogDetail("Walkers: off ([GuerrillaPeople] Walkers=0)");
        return;
    }
    ReadFiles();
    FindRotating();
    g_rotateOn = rfa::IniInt(L"GuerrillaPeople", L"RotateCivilians", 0) != 0;  // off unless set to 1 (the main session, 2026-11-18)
    if (!g_rotating.empty())
        rfa::LogDetail(g_rotateOn ? "Walkers: %u districts' civilians change in batches as the player goes about (their resources' versions; RotateCivilians=1: each "
                              "change reloads the district's people and vehicles)"
                            : "Walkers: %u districts have versions of their civilians; they don't change (RotateCivilians=0: Armageddon's change of spawn "
                              "resource reloads every person and vehicle)",
                 static_cast<unsigned>(g_rotating.size()));
    if (g_files.zones.empty()) {
        rfa::Log("Walkers: no mods\\guerrilla_people\\ambient.txt (build the people); no one walks");
        return;
    }
    size_t civilians = 0;
    for (const auto& [resource, people] : g_files.civilians) civilians += people.size();
    ReadSafehouseAreas();
    rfa::LogDetail("Walkers: Guerrilla's ped spawner: %u zones where people walk, %u resources' civilians (%u names) from crews.txt; %u safehouses' areas "
             "(mods\\guerrilla_vehicles\\safehouses.txt), where a walker keeps its character's own team; elsewhere it is a civilian",
             static_cast<unsigned>(g_files.zones.size()), static_cast<unsigned>(g_files.civilians.size()), static_cast<unsigned>(civilians),
             static_cast<unsigned>(g_safehouseAreas.size()));
    g_on = true;
}

void AmbientFrame(bool ours) {
    if (!g_on) return;
    if (!ours) {
        g_walkers.clear();  // the level's people are gone with it
        return;
    }
    BYTE* player = Player();
    if (!player) return;
    const int now = GameTime();
    if (now < g_lastClock) {  // the clock started over (a new level): what we knew is gone
        g_walkers.clear();
        g_nextSpawn = g_nextReport = 0;
    }
    g_lastClock = now;
    const V3 at = Pos(player);
    g_alertNow = AlertLevel();
    RotateFrame(at, now, g_alertNow);
    for (auto w = g_walkers.begin(); w != g_walkers.end();) {
        BYTE* human = HumanByHandle(kWorld, nullptr, w->human);
        if (!human || GoneOrDead(human)) {
            ++g_died;
            w = g_walkers.erase(w);
            continue;
        }
        const int team = *reinterpret_cast<const int*>(human + kTeam);
        bool asked = false;
        AcquireSRWLockExclusive(&g_leaveLock);
        if (!g_leaveToGame.empty()) asked = g_leaveToGame.erase(w->human) > 0;
        ReleaseSRWLockExclusive(&g_leaveLock);
        if (asked || (w->team >= 0 && team != w->team)) {
            if (w->walked) GiveBack(*w, human);
            ++g_leftToOthers;
            RFA_LOG_DETAIL_FIRST(32, "Walkers: NPC 0x%08X is no longer a walker (%s); the game's AI has it", w->human,
                          asked ? "another mod took it" : team == 0 ? "it joined the guerrillas" : "its team changed");
            w = g_walkers.erase(w);
            continue;
        }
        const V3 place = Pos(human);
        const V3 head{place.x, place.y + 1.0f, place.z};
        if (InView(&head.x, 1.0f)) w->outOfViewSince = -1;
        else if (w->outOfViewSince < 0) w->outOfViewSince = now;
        if (ShouldGo(*w, Distance(place, at), now)) {
            ActionsWalkerStop(w->human);
            RemoveObject(kWorld, nullptr, w->human, 0);
            ++g_despawned;
            w = g_walkers.erase(w);
            continue;
        }
        if (w->walked) Walk(*w, human, now);
        ++w;
    }
    if (now >= g_nextReport && (g_made > 0 || ZoneAt(at))) {
        g_nextReport = now + 30000;
        if (g_made > 0 || g_noNode > 0)
            rfa::LogDetail("Walkers: %d out of a cap of %d (%s), %d people in the world (no spawning from %d); %d made, %d gone out of view, %d died or were taken, "
                     "%d fled (%d cowered), %d cowered from a vehicle, %d gawked, %d lay fetal, %d left to others; %d made civilians, %d made in a safehouse's area on their own team; %d navpoints reached, %d go-tos without a way; %d not streamed in, %d times no navpoint to spawn on",
                     static_cast<int>(g_walkers.size()), g_cap < 0 ? 0 : g_cap, g_capWhy[0] ? g_capWhy : "not worked out yet", NpcCount(kWorld, nullptr), kMostPeople,
                     g_made, g_despawned, g_died, g_fled, g_cowered, g_cowersIncoming, g_gawks, g_fetal, g_leftToOthers, g_madeCivilians, g_madeInSafehouses, g_reachedNodes, g_goToFailures, g_notStreamed, g_noNode);
    }
    if (now < g_nextSpawn && g_nextSpawn - now <= 1000) return;
    const int people = NpcCount(kWorld, nullptr);
    g_nextSpawn = people >= kBusyPeople ? now + 500 + std::rand() % 501 : now + 250 + std::rand() % 251;
    if (people >= kMostPeople) {
        RFA_LOG_DETAIL_FIRST(5, "Walkers: no new walker: the world has %d people (Guerrilla's spawner stops at %d)", people, kMostPeople);
        return;
    }
    SpawnStep(at, *reinterpret_cast<const V3*>(player + kOrientation + 24), now);
}

void AmbientLeaveToGame(unsigned long person) {
    AcquireSRWLockExclusive(&g_leaveLock);
    g_leaveToGame.insert(person);
    ReleaseSRWLockExclusive(&g_leaveLock);
}

const char* AmbientZoneResource(float x, float z) {
    const Zone* zone = ZoneAt(V3{x, 0, z});
    return zone && !zone->resource.empty() ? zone->resource.c_str() : nullptr;
}

// The resource the game is given in place of the one it asks for: a rotating district's version in force (see
// RotateFrame); anything else as asked.
void* AmbientResourceFor(void* resource) {
    if (!resource || g_rotating.empty()) return resource;
    const char* name = *static_cast<const char* const*>(resource);  // an entry starts with its name
    if (!name) return resource;
    Rotating* r = RotatingFor(name);
    if (!r) return resource;
    g_currentBase = r->base;
    if (r->at == 0) return resource;
    void* version = FindSpawnResource(VersionName(*r, r->at).c_str());
    return version ? version : resource;
}
