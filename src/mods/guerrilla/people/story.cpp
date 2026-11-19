// Guerrilla's story people at its safehouses (piece 4, 2026-11-13; the user: "(a)", Guerrilla's way). [RM, A]
//
// Guerrilla's zones put its story people under action nodes at nine safehouses: special NPCs (squad nodes with
// special_npc: Samanya, OldCoot, RFC), safehouse VIPs (safehouse_vip: Carmen, Jorge, Kyle) and radio operators
// (radio_operator: whoever of the "Radio Operator" group is loaded; the people build gives each group its own
// version, so they spawn as any squad). Its resource manager keeps a human slot for each of the first two kinds:
// - HUMAN_SPECIAL: find_local_special_npc_nodes (RM 0xB51C30) takes the special node nearest the player among those
//   streamed in; its character takes the slot (srm_special_human_reserved RM 0xB5CE70, reserve_slot type 8) and
//   that node spawns; another one nearer frees it first (srm_special_human_free RM 0xB5CEE0).
// - HUMAN_SAFEHOUSE_VIP: when a VIP node streams in and none is chosen (safehouse_vip_notify RM 0x7909A0,
//   stream_notify_select_spawn RM 0x790430): with VIP_spawn_probability (0.8, the tweak table) one of the VIP nodes
//   under the same top parent (the safehouse: find_top_parent_object), but the characters the scripts disabled
//   (disable_safehouse_vip: safehouse_vip_disable RM 0x7909F0), at random; its character takes the slot (type 9) and
//   it spawns once streamed in; else none this time. Its node or set streaming out lets it go (stream_notify_unload
//   RM 0x7905A0).
//
// Here, from mods\guerrilla_world\story_nodes.txt (the world build: each node's kind, its handle as placed, its
// squad, its character, its place, its group), once a second in our worlds:
// - The node spawn (guerrilla_people.cpp's hook) makes nothing for a story node but the one this file spawns.
// - A character is held in the level's category as Armageddon's level resource holds its own (0x761D20(8, 3, …),
//   which a level's resource in force allows). It is kept until the game's own resource change lets it go (the
//   district changing: its state machine 0x762730 drops what the new resource doesn't list): Armageddon's release
//   (0x760F60) refuses while a resource is in force, and forcing it past that (the pools' state 0x19734CC set to none
//   for the call, the first piece 4 build) isn't a plain release (0x761020 goes on to 0x760020, which loads and holds
//   the container, and puts the record on a pool list) and was the likely cause of the 2026-11-13 14:46 crash at
//   exit. When a slot changes hands, only the person spawned from it is taken away.
// - Room: the district resources list 14 (Guerrilla's 9 guerrilla, 3 EDF, 2 Marauder types), which with the player
//   leaves two of the people's 17 stream slots (the pool 0x19728E8) for the special NPC and the VIP.
// Not here yet: saving the disabled VIPs (safehouse_vip_save RM 0x775540; with the campaign), and the radio
// operator's and the VIP's lines while the player is in a safehouse (safehouse_vip_do_frame RM 0x7907B0: the
// radio operator every 15-25 s over the radio persona, opr_amb_batch01; the VIP every 10-20 s; with the voices).

#define RFA_MOD_NAME "guerrilla_people"
#include "rfa_mod.h"
#include "story.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

constexpr DWORD kPosition = 0x04, kHandle = 0x68, kNpcAi = 0x174C, kAiSquad = 0x7D4;
constexpr DWORD kNone = 0xFFFFFFFF;
void* const kWorld = reinterpret_cast<void*>(0x1870630);
using WorldObjectFn = BYTE*(__fastcall*)(void* world, void*, DWORD handle);
using CountFn = int(__fastcall*)(void* world, void*);
using AtFn = BYTE*(__fastcall*)(void* world, void*, int index);
auto const WorldObject = reinterpret_cast<WorldObjectFn>(0x6CB9E0);
auto const NpcCount = reinterpret_cast<CountFn>(0x424480);   // world::get_num_objects<npc>
auto const NpcAt = reinterpret_cast<AtFn>(0x4244D0);
auto const GoneOrDead = reinterpret_cast<bool(__cdecl*)(BYTE* object)>(0x42ADC0);
auto const RemoveObject = reinterpret_cast<int(__fastcall*)(void* world, void*, DWORD handle, int flag)>(0x6D56A0);
auto const Player = reinterpret_cast<BYTE*(__cdecl*)()>(0x8B92D0);
using NodeSpawnFn = DWORD(__fastcall*)(BYTE* node, void*, DWORD element, DWORD iconType);
auto const NodeSpawn = reinterpret_cast<NodeSpawnFn>(0x800040);
int GameTime() { return *reinterpret_cast<const int*>(0x1121300); }

// The characters' streaming (see the top).
auto const HumanInfo = reinterpret_cast<BYTE*(__cdecl*)(const char* name)>(0x88D170);
auto const CharacterLoaded = reinterpret_cast<bool(__cdecl*)(BYTE* info)>(0x762230);
auto const CharacterOnItsWay = reinterpret_cast<bool(__cdecl*)(BYTE* info)>(0x764ED0);
auto const HoldCharacter = reinterpret_cast<bool(__cdecl*)(int category, int categories, BYTE* info)>(0x761D20);
int* const kPoolsState = reinterpret_cast<int*>(0x19734CC);  // not 0 while a level's resource is in force (1-3 changing, 4 in force)
constexpr int kInForce = 4;
constexpr int kLevelCategory = 8, kLevelCategories = 3;      // as the level's own resource holds its characters (0x7657A0)

constexpr int kLookEvery = 1000;
constexpr float kVipSpawnProbability = 0.8f;  // Guerrilla's tweak table: VIP_spawn_probability

struct V3 { float x, y, z; };
V3 Pos(const BYTE* object) { return *reinterpret_cast<const V3*>(object + kPosition); }
float Distance2(const V3& a, const V3& b) { const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z; return dx * dx + dy * dy + dz * dz; }
DWORD HandleOf(const BYTE* object) { return *reinterpret_cast<const DWORD*>(object + kHandle); }
DWORD SquadOf(const BYTE* human) {
    const BYTE* ai = *reinterpret_cast<BYTE* const*>(human + kNpcAi);
    return ai ? *reinterpret_cast<const DWORD*>(ai + kAiSquad) : kNone;
}

struct Node {
    bool special = false;  // else a VIP
    DWORD handle = kNone;
    std::string squad, character;
    V3 at{};
    DWORD group = kNone;
};
std::vector<Node> g_nodes;
std::unordered_set<DWORD> g_storyHandles;

// What a slot holds now (Guerrilla's resource_slot_record for HUMAN_SPECIAL or HUMAN_SAFEHOUSE_VIP).
struct Slot {
    const char* kind;
    std::string character;  // held, or empty
    int node = -1;          // the node chosen (an index in g_nodes), or -1
    DWORD squad = kNone;    // the squad spawned from it, while it is about
    DWORD group = kNone;    // the VIPs' safehouse chosen (none chosen: kNone)
    bool loggedLoaded = false;
};
Slot g_special{"special NPC"}, g_vip{"VIP"};

CRITICAL_SECTION g_disabledLock;
bool g_lockMade = false;
std::unordered_set<std::string> g_disabledVips;  // lower case
const BYTE* g_spawning = nullptr;
int g_nextLook = 0, g_lastClock = 0;
int g_spawned = 0, g_held = 0, g_released = 0;

std::string Lower(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

bool Disabled(const std::string& character) {
    if (!g_lockMade) return false;
    EnterCriticalSection(&g_disabledLock);
    const bool disabled = g_disabledVips.count(Lower(character)) > 0;
    LeaveCriticalSection(&g_disabledLock);
    return disabled;
}

void ReadNodes() {
    std::ifstream file(rfa::RootDir() + L"\\mods\\guerrilla_world\\story_nodes.txt");
    if (!file) {
        rfa::Log("Story people: no mods\\guerrilla_world\\story_nodes.txt (the world built before it had them): none at the safehouses");
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == ';') continue;
        std::istringstream in(line);
        std::string kind;
        Node n;
        in >> kind >> std::hex >> n.handle >> std::dec >> n.squad >> n.character >> n.at.x >> n.at.y >> n.at.z >> std::hex >> n.group;
        if (!in || (kind != "special" && kind != "vip")) continue;
        n.special = kind == "special";
        g_storyHandles.insert(n.handle);
        g_nodes.push_back(n);
    }
    int special = 0;
    for (const Node& n : g_nodes) special += n.special;
    rfa::Log("Story people: %d of Guerrilla's story people's nodes at the safehouses read (%d special NPCs', %d VIPs'), held for the one special NPC and the "
             "one VIP Guerrilla streams in", static_cast<int>(g_nodes.size()), special, static_cast<int>(g_nodes.size()) - special);
}

// The people about from a squad: taken away (Guerrilla lets its slot go once its person is gone).
void TakeAway(DWORD squad) {
    if (squad == kNone) return;
    const int people = NpcCount(kWorld, nullptr);
    for (int i = 0; i < people; ++i)
        if (BYTE* human = NpcAt(kWorld, nullptr, i); human && SquadOf(human) == squad) RemoveObject(kWorld, nullptr, HandleOf(human), 0);
}

bool SquadAbout(DWORD squad) {
    if (squad == kNone) return false;
    const int people = NpcCount(kWorld, nullptr);
    for (int i = 0; i < people; ++i)
        if (BYTE* human = NpcAt(kWorld, nullptr, i); human && !GoneOrDead(human) && SquadOf(human) == squad) return true;
    return false;
}

void Hold(Slot& slot, const std::string& character) {
    BYTE* info = HumanInfo(character.c_str());
    if (!info) {
        RFA_LOG_FIRST(8, "Story people: the game has no character %s; its %s isn't streamed in", character.c_str(), slot.kind);
        return;
    }
    slot.character = character;
    slot.loggedLoaded = false;
    if (CharacterLoaded(info) || CharacterOnItsWay(info) || HoldCharacter(kLevelCategory, kLevelCategories, info)) {
        ++g_held;
        RFA_LOG_DETAIL_FIRST(40, "Story people: %s held for the %s slot (Guerrilla's HUMAN_%s)", character.c_str(), slot.kind, &slot == &g_special ? "SPECIAL" : "SAFEHOUSE_VIP");
    }
    else RFA_LOG_FIRST(8, "Story people: %s couldn't be held for the %s slot (the level's category is full?)", character.c_str(), slot.kind);
}

// Frees a slot: its person is taken away; its character is kept until the district changes (see the top).
void Let(Slot& slot) {
    TakeAway(slot.squad);
    slot.squad = kNone;
    if (!slot.character.empty()) {
        ++g_released;
        RFA_LOG_DETAIL_FIRST(40, "Story people: the %s slot is free; %s kept until the district changes (the game lets it go then)", slot.kind,
                      slot.character.c_str());
    }
    slot.character.clear();
    slot.node = -1;
    slot.group = kNone;
}

// Spawns the slot's node once its character is in and its zone streamed in; a squad gone while its node is about
// stays gone until the node streams in again (its respawn_speed is None).
void SpawnWhenReady(Slot& slot) {
    if (slot.node < 0 || slot.character.empty()) return;
    const Node& n = g_nodes[slot.node];
    BYTE* node = WorldObject(kWorld, nullptr, n.handle);
    if (!node) {
        slot.squad = kNone;  // streamed out: it spawns again when it's back
        return;
    }
    if (slot.squad != kNone) return;
    BYTE* info = HumanInfo(slot.character.c_str());
    if (!info || !CharacterLoaded(info)) return;
    if (!slot.loggedLoaded) {
        slot.loggedLoaded = true;
        RFA_LOG_DETAIL_FIRST(40, "Story people: %s is loaded", slot.character.c_str());
    }
    g_spawning = node;
    const DWORD squad = NodeSpawn(node, nullptr, kNone, 0);
    g_spawning = nullptr;
    if (squad == kNone) {
        RFA_LOG_DETAIL_FIRST(8, "Story people: node 0x%08X (%s) made no squad", n.handle, n.squad.c_str());
        return;
    }
    slot.squad = squad;
    ++g_spawned;
    rfa::LogDetail("Story people: %s (%s) spawned at (%.0f, %.0f, %.0f), node 0x%08X, as Guerrilla's %s", slot.character.c_str(), n.squad.c_str(), n.at.x, n.at.y, n.at.z,
             n.handle, &slot == &g_special ? "special NPC (the one nearest the player)" : "safehouse VIP (one a safehouse)");
}

// HUMAN_SPECIAL: the special node nearest the player among those streamed in.
void LookSpecial(const V3& player) {
    int nearest = -1;
    float best = 0;
    for (int i = 0; i < static_cast<int>(g_nodes.size()); ++i) {
        const Node& n = g_nodes[i];
        if (!n.special || !WorldObject(kWorld, nullptr, n.handle)) continue;
        const float d = Distance2(n.at, player);
        if (nearest < 0 || d < best) nearest = i, best = d;
    }
    const std::string want = nearest >= 0 ? g_nodes[nearest].character : std::string();
    if (Lower(want) != Lower(g_special.character)) {
        if (!g_special.character.empty()) Let(g_special);
        if (!want.empty()) Hold(g_special, want);
    }
    else if (nearest != g_special.node && g_special.squad != kNone) {  // the same character, another node nearer
        TakeAway(g_special.squad);
        g_special.squad = kNone;
    }
    g_special.node = nearest;
    SpawnWhenReady(g_special);
}

// HUMAN_SAFEHOUSE_VIP: one a safehouse, chosen as its VIP nodes stream in.
void LookVip(const V3& player) {
    if (g_vip.group != kNone) {
        bool about = false;
        for (const Node& n : g_nodes)
            if (!n.special && n.group == g_vip.group && WorldObject(kWorld, nullptr, n.handle)) about = true;
        if (!about) {  // the safehouse streamed out (stream_notify_unload)
            RFA_LOG_DETAIL_FIRST(40, "Story people: the safehouse 0x%08X streamed out; its VIP slot is free", g_vip.group);
            Let(g_vip);
        }
    }
    if (g_vip.group == kNone) {
        int nearest = -1;
        float best = 0;
        for (int i = 0; i < static_cast<int>(g_nodes.size()); ++i) {
            const Node& n = g_nodes[i];
            if (n.special || !WorldObject(kWorld, nullptr, n.handle)) continue;
            const float d = Distance2(n.at, player);
            if (nearest < 0 || d < best) nearest = i, best = d;
        }
        if (nearest < 0) return;
        g_vip.group = g_nodes[nearest].group;
        const float roll = static_cast<float>(std::rand()) / RAND_MAX;
        std::vector<int> set;
        for (int i = 0; i < static_cast<int>(g_nodes.size()); ++i)
            if (!g_nodes[i].special && g_nodes[i].group == g_vip.group && !Disabled(g_nodes[i].character)) set.push_back(i);
        if (roll > kVipSpawnProbability || set.empty()) {
            rfa::LogDetail("Story people: no VIP at the safehouse 0x%08X this time (%s)", g_vip.group,
                     set.empty() ? "all its VIPs disabled" : "Guerrilla's VIP_spawn_probability, 0.8");
            return;
        }
        g_vip.node = set[std::rand() % set.size()];
        rfa::LogDetail("Story people: %s picked as the safehouse 0x%08X's VIP (of %d)", g_nodes[g_vip.node].character.c_str(), g_vip.group, static_cast<int>(set.size()));
        Hold(g_vip, g_nodes[g_vip.node].character);
    }
    SpawnWhenReady(g_vip);
}

}  // namespace

void StartStory() {
    InitializeCriticalSection(&g_disabledLock);
    g_lockMade = true;
    ReadNodes();
}

bool StoryHoldsNode(const unsigned char* node) {
    return node && node != g_spawning && !g_storyHandles.empty() && g_storyHandles.count(HandleOf(node)) > 0;
}

void StoryDisableVip(const char* character, bool disable) {
    if (!character || !g_lockMade) return;
    EnterCriticalSection(&g_disabledLock);
    if (disable) g_disabledVips.insert(Lower(character));
    else g_disabledVips.erase(Lower(character));
    LeaveCriticalSection(&g_disabledLock);
    rfa::Log("Story people: the VIP %s %s (disable_safehouse_vip)", character, disable ? "disabled" : "enabled again");
}

void StoryFrame(bool ours) {
    if (g_nodes.empty()) return;
    const int now = GameTime();
    if (!ours || now < g_lastClock) {  // another world, or the level started over: the holds went with its resource
        g_special = Slot{"special NPC"};
        g_vip = Slot{"VIP"};
        g_lastClock = now;
        g_nextLook = 0;
        return;
    }
    g_lastClock = now;
    if (now < g_nextLook || *kPoolsState != kInForce) return;  // only once the level's resource is in force (not while it changes)
    g_nextLook = now + kLookEvery;
    BYTE* player = Player();
    if (!player) return;
    const V3 at = Pos(player);
    LookSpecial(at);
    LookVip(at);
}
