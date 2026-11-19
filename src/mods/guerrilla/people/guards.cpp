// Guerrilla's guard posts (2026-11-15; the main session, for the user: "Guerrilla's guard post AI, to replace the
// leash-to-box stand-in"). [RM, A]
//
// In Guerrilla a squad on Defend orders on a bounding box that holds guard nodes (object_guard_node: its place and facing,
// angle_left, angle_right, group_id) guards that post:
// - each member reserves one of the box's guard nodes (guard_selection_choose_node RM 0x6614E0 → pick_best_node RM
//   0x661160): the nearest free node of no group (group_id 0); else, in the group with the fewest nodes taken, its
//   nearest free one;
// - it walks there (go_to_guard_post_action RM 0x6A2B40, firing at its target on the way) and is at its post within 1 m
//   across and 1.5 m in height (ai_system::is_at_guard_post RM 0x68EFA0; AT_GUARD_POST_Y_DIST);
// - there it stands facing the node's way and fights from it (default_guard_action RM 0x6A3310, guard_at_post_action
//   RM 0x6A3830): its moves are to guard nodes of its post.
// Armageddon deleted all of it (object_guard_node is editor-only in it). Here, from mods\guerrilla_world\guard_posts.txt
// (the world build: each placed post's box, by its handle as placed, with its guard nodes), once a frame in our worlds:
// - a squad spawned from a node under a post's box (the node-spawn hook, guerrilla_people.cpp) is that post's; its
//   living members are found by their spawn node (human +0x1590), and each reserves a node by Guerrilla's rule;
// - out of a fight its AI sits out (the action spots' yield bit, AI +0x651), and the AI's go-to (0x431C20) walks it to
//   its node; there Armageddon's teleport (0x854720, as sf_char_teleport) sets it on the node, facing the node's way;
// - in a fight (its combat target 0x880B70, or the flee-to-safety and enemy-flee beliefs 0x483C90 / 0x483B00) the game's
//   AI has it, and the human's path query (HumanPath, guerrilla_people.cpp) gives it ways only to its post's guard
//   nodes, so it fights from its post; 5 s after the fight it walks back to its node;
// - a squad sent out from its post (GuerrillaPeople_ReleaseLeash) leaves its nodes for good.
// Not here yet: Guerrilla's scan between angle_left and angle_right (do_guard_idle_action_scan RM 0x660CD0, the head's
// look-at), its turns to the node's other facings every 10-15 s (guard_at_post_action, human_move_turn_to_target), its
// idle animations (do_guard_idle_action_animate RM 0x5EAC20), and its moves to other nodes of its group after a while.

#define RFA_MOD_NAME "guerrilla_people"
#include "rfa_mod.h"
#include "guards.h"
#include <cfloat>
#include <climits>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr DWORD kPosition = 0x04, kHandle = 0x68, kNpcAi = 0x174C, kYield = 0x651, kHumanSpawnNode = 0x1590, kAiSquad = 0x7D4;
constexpr DWORD kNone = 0xFFFFFFFF;
void* const kWorld = reinterpret_cast<void*>(0x1870630);
using CountFn = int(__fastcall*)(void* world, void*);
using AtFn = BYTE*(__fastcall*)(void* world, void*, int index);
auto const NpcCount = reinterpret_cast<CountFn>(0x424480);
auto const NpcAt = reinterpret_cast<AtFn>(0x4244D0);
auto const WorldObject = reinterpret_cast<BYTE*(__fastcall*)(void* world, void*, DWORD handle)>(0x6CB9E0);
auto const GoneOrDead = reinterpret_cast<bool(__cdecl*)(BYTE* object)>(0x42ADC0);
auto const FleeToSafetyBelief = reinterpret_cast<BYTE*(__cdecl*)(BYTE* npc)>(0x483C90);
auto const EnemyFleeBelief = reinterpret_cast<BYTE*(__cdecl*)(BYTE* npc)>(0x483B00);
auto const CombatTarget = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human)>(0x880B70);
using GoToFn = int(__fastcall*)(BYTE* ai, void*, const float* place, int movement, int stance, int priority, DWORD cellLow, DWORD cellHigh, bool flag);
auto const GoTo = reinterpret_cast<GoToFn>(0x431C20);
constexpr int kWalk = 1, kStanding = 0, kLowPriority = 0, kNoPath = 6, kStartOffMesh = 7, kDestinationOffMesh = 8;
struct V3 { float x, y, z; };
struct M3 { V3 r, u, f; };
auto const Teleport = reinterpret_cast<void(__cdecl*)(BYTE* human, const V3* place, const M3* orientation, bool flag)>(0x854720);
int GameTime() { return *reinterpret_cast<const int*>(0x1121300); }

constexpr float kAtPostAcross = 1.0f, kAtPostHeight = 1.5f;  // is_at_guard_post: 1 m across, AT_GUARD_POST_Y_DIST 1.5
constexpr float kNodeReach = 1.5f;                            // a way asked in a fight: to within this of one of the post's nodes
constexpr int kCalmAgain = 5000, kNoWayAgain = 30000, kScanEvery = 1000, kReportEvery = 60000;

struct GuardNode { V3 at{}; float fx = 0, fz = 1, left = 45, right = 45; int group = 0; DWORD by = kNone; };
struct Post { std::vector<GuardNode> nodes; };
std::unordered_map<DWORD, Post> g_posts;        // by the box's handle as placed
std::unordered_map<DWORD, DWORD> g_postOfNode;  // a squad node's handle → its post's box (from the node-spawn hook)
std::unordered_set<DWORD> g_released;           // squads sent out from their posts
enum Phase { kGoing, kAtPost, kFighting, kNoWay };
struct Guard { DWORD box = kNone; int node = -1; Phase phase = kGoing; int until = 0; };
std::unordered_map<DWORD, Guard> g_guards;      // by the person's handle
DWORD g_thread = 0;                             // the thread GuardsFrame runs on (the game's main thread)
int g_lastClock = 0, g_nextScan = 0, g_nextReport = 0;
int g_squads = 0, g_taken = 0, g_arrived = 0, g_fights = 0, g_noWay = 0, g_refused = 0, g_noNode = 0;

DWORD HandleOf(const BYTE* o) { return *reinterpret_cast<const DWORD*>(o + kHandle); }
V3 Pos(const BYTE* o) { return *reinterpret_cast<const V3*>(o + kPosition); }
float Dist2(const V3& a, const V3& b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z); }
bool AtNode(const V3& at, const V3& node, float across) {
    const float dx = at.x - node.x, dz = at.z - node.z;
    return dx * dx + dz * dz <= across * across && std::fabs(at.y - node.y) < kAtPostHeight;
}

void ReadPosts() {
    std::ifstream file(rfa::RootDir() + L"\\mods\\guerrilla_world\\guard_posts.txt");
    if (!file) {
        rfa::Log("Guards: no mods\\guerrilla_world\\guard_posts.txt (the world built before it had them): guard posts' squads are leashed to their boxes");
        return;
    }
    int nodes = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == ';') continue;
        std::istringstream in(line);
        std::string kind, box;
        GuardNode n;
        if (!(in >> kind >> box >> n.at.x >> n.at.y >> n.at.z >> n.fx >> n.fz >> n.left >> n.right >> n.group) || kind != "guard") continue;
        g_posts[std::stoul(box, nullptr, 16)].nodes.push_back(n);
        ++nodes;
    }
    rfa::Log("Guards: %d of Guerrilla's guard posts read, %d guard nodes; their squads stand at the nodes and fight from them, as Guerrilla's",
             static_cast<int>(g_posts.size()), nodes);
}

// Guerrilla's pick_best_node: the nearest free node of no group; else, in the group with the fewest nodes taken, its
// nearest free one; or -1.
int PickNode(const Post& p, const V3& at) {
    int best = -1;
    float bestDistance = FLT_MAX;
    for (int i = 0; i < static_cast<int>(p.nodes.size()); ++i) {
        const GuardNode& n = p.nodes[i];
        if (n.group != 0 || n.by != kNone) continue;
        const float d = Dist2(at, n.at);
        if (d < bestDistance) bestDistance = d, best = i;
    }
    if (best >= 0) return best;
    std::unordered_map<int, int> takenIn;
    for (const GuardNode& n : p.nodes)
        if (n.group != 0 && n.by != kNone) ++takenIn[n.group];
    int fewest = INT_MAX;
    for (int i = 0; i < static_cast<int>(p.nodes.size()); ++i) {
        const GuardNode& n = p.nodes[i];
        if (n.group == 0 || n.by != kNone) continue;
        const int taken = takenIn[n.group];
        const float d = Dist2(at, n.at);
        if (taken < fewest || (taken == fewest && d < bestDistance)) fewest = taken, bestDistance = d, best = i;
    }
    return best;
}

void SitOut(BYTE* human) {
    if (BYTE* ai = *reinterpret_cast<BYTE**>(human + kNpcAi)) ai[kYield] |= 2;
}

void Free(DWORD handle, Guard& g) {
    auto post = g_posts.find(g.box);
    if (post != g_posts.end() && g.node >= 0 && g.node < static_cast<int>(post->second.nodes.size()) && post->second.nodes[g.node].by == handle)
        post->second.nodes[g.node].by = kNone;
    g.node = -1;
}

void Step(BYTE* human, DWORD handle, Guard& g, int now) {
    Post& p = g_posts[g.box];
    if (g.phase == kNoWay && now < g.until) return;
    if (g.node < 0) {
        g.node = PickNode(p, Pos(human));
        if (g.node < 0) {
            ++g_noNode;
            RFA_LOG_DETAIL_FIRST(16, "Guards: NPC 0x%08X of post 0x%08X finds no free guard node (%d nodes); the game's AI keeps it", handle, g.box,
                          static_cast<int>(p.nodes.size()));
            g.phase = kNoWay;
            g.until = now + kNoWayAgain;
            return;
        }
        GuardNode& n = p.nodes[g.node];
        n.by = handle;
        ++g_taken;
        RFA_LOG_DETAIL_FIRST(32, "Guards: NPC 0x%08X of post 0x%08X takes guard node %d (group %d) at (%.1f, %.1f, %.1f), as Guerrilla's pick_best_node", handle, g.box,
                      g.node, n.group, n.at.x, n.at.y, n.at.z);
    }
    const GuardNode& n = p.nodes[g.node];
    if (FleeToSafetyBelief(human) || EnemyFleeBelief(human) || CombatTarget(human)) {
        if (g.phase != kFighting) {
            ++g_fights;
            RFA_LOG_DETAIL_FIRST(16, "Guards: NPC 0x%08X of post 0x%08X fights; the game's AI has it, its ways only to the post's guard nodes", handle, g.box);
        }
        g.phase = kFighting;
        g.until = now + kCalmAgain;
        return;
    }
    if (g.phase == kFighting && now < g.until) return;
    const V3 at = Pos(human);
    const bool atPost = AtNode(at, n.at, kAtPostAcross);
    if (g.phase == kAtPost && atPost) {
        SitOut(human);
        return;
    }
    g.phase = kGoing;
    SitOut(human);
    BYTE* ai = *reinterpret_cast<BYTE**>(human + kNpcAi);
    if (!atPost) {
        const int result = ai ? GoTo(ai, nullptr, &n.at.x, kWalk, kStanding, kLowPriority, kNone, kNone, false) : kNoPath;
        if (result == kNoPath || result == kStartOffMesh || result == kDestinationOffMesh) {
            ++g_noWay;
            RFA_LOG_FIRST(16, "Guards: NPC 0x%08X of post 0x%08X can't walk to its guard node at (%.1f, %.1f, %.1f) (the go-to's %d); tries again in 30 s", handle,
                          g.box, n.at.x, n.at.y, n.at.z, result);
            g.phase = kNoWay;
            g.until = now + kNoWayAgain;
        }
        return;
    }
    const float length = std::sqrt(n.fx * n.fx + n.fz * n.fz);
    const V3 f = length > 0.01f ? V3{n.fx / length, 0, n.fz / length} : V3{0, 0, 1};
    const M3 facing{{f.z, 0, -f.x}, {0, 1, 0}, f};
    Teleport(human, &n.at, &facing, true);
    g.phase = kAtPost;
    ++g_arrived;
    RFA_LOG_DETAIL_FIRST(32, "Guards: NPC 0x%08X of post 0x%08X is at its guard node %d, facing (%.2f, %.2f)", handle, g.box, g.node, f.x, f.z);
}

}  // namespace

void StartGuards() { ReadPosts(); }

bool GuardsPostSpawned(unsigned long node, unsigned long box, unsigned long squad) {
    auto post = g_posts.find(box);
    if (post == g_posts.end()) return false;
    g_postOfNode[node] = box;
    g_released.erase(squad);
    ++g_squads;
    RFA_LOG_DETAIL_FIRST(64, "Guards: node 0x%08lX's squad 0x%08lX guards post 0x%08lX (%d guard nodes), as Guerrilla's", node, squad, box,
                  static_cast<int>(post->second.nodes.size()));
    return true;
}

void GuardsReleaseSquad(unsigned long squad) {
    if (g_posts.empty()) return;
    g_released.insert(squad);
    RFA_LOG_DETAIL_FIRST(16, "Guards: squad 0x%08lX sent out from its post: its members leave their guard nodes", squad);
}

int GuardsGoalAllowed(unsigned long human, const float* goal) {
    if (!goal || GetCurrentThreadId() != g_thread) return -1;
    auto guard = g_guards.find(human);
    if (guard == g_guards.end()) return -1;
    auto post = g_posts.find(guard->second.box);
    if (post == g_posts.end()) return -1;
    const V3 to{goal[0], goal[1], goal[2]};
    for (const GuardNode& n : post->second.nodes)
        if (AtNode(to, n.at, kNodeReach)) return 1;
    ++g_refused;
    return 0;
}

void GuardsFrame(bool ours) {
    if (g_posts.empty()) return;
    g_thread = GetCurrentThreadId();
    const int now = GameTime();
    if (!ours || now < g_lastClock) {  // another world, or the level started over
        for (auto& [handle, g] : g_guards) Free(handle, g);
        g_guards.clear();
        g_released.clear();
        g_lastClock = now;
        return;
    }
    g_lastClock = now;
    if (now >= g_nextScan) {
        g_nextScan = now + kScanEvery;
        std::unordered_set<DWORD> about;
        const int people = NpcCount(kWorld, nullptr);
        for (int i = 0; i < people; ++i) {
            BYTE* human = NpcAt(kWorld, nullptr, i);
            if (!human || GoneOrDead(human)) continue;
            auto post = g_postOfNode.find(*reinterpret_cast<const DWORD*>(human + kHumanSpawnNode));
            if (post == g_postOfNode.end()) continue;
            const BYTE* ai = *reinterpret_cast<BYTE**>(human + kNpcAi);
            if (ai && g_released.count(*reinterpret_cast<const DWORD*>(ai + kAiSquad))) continue;  // sent out: the game's
            const DWORD handle = HandleOf(human);
            about.insert(handle);
            if (!g_guards.count(handle)) g_guards[handle].box = post->second;
        }
        for (auto it = g_guards.begin(); it != g_guards.end();) {
            if (about.count(it->first)) {
                ++it;
                continue;
            }
            Free(it->first, it->second);
            it = g_guards.erase(it);
        }
    }
    for (auto& [handle, g] : g_guards) {
        BYTE* human = WorldObject(kWorld, nullptr, handle);
        if (human && !GoneOrDead(human)) Step(human, handle, g, now);
    }
    if (now >= g_nextReport) {
        g_nextReport = now + kReportEvery;
        if (g_squads > 0) {
            int atPost = 0, fighting = 0;
            for (const auto& [handle, g] : g_guards) atPost += g.phase == kAtPost, fighting += g.phase == kFighting;
            rfa::LogDetail("Guards: %d guards at their posts now (%d at their nodes, %d in a fight); %d squads at posts so far, %d nodes taken, %d arrivals, "
                     "%d fights, %d with no way to their node, %d with no free node, %d ways refused (not to a guard node)",
                     static_cast<int>(g_guards.size()), atPost, fighting, g_squads, g_taken, g_arrived, g_fights, g_noWay, g_noNode, g_refused);
        }
    }
}
