// Guerrilla's kill regions: the trigger_regions whose region_type is "kill human". There are 25 in its
// zones: 21 boxes at the bottoms of its pits and chasms (region_kill_type cliff) and 4 minefields (mine).
// Armageddon's own trigger_region never reads region_type (its loader 0x7F9A00 takes the box, the shape,
// enabled and trigger_flags only; Armageddon kills with a class of its own, damage_region), so here is
// what Guerrilla does with them each frame, in its trigger_region_do_frame (0xAD6600 in Re-Mars-tered):
// - A region counts while it is enabled. With trigger_flags not_in_mission it rests while a mission runs,
//   and with not_in_activity while an activity runs.
// - Every human's place (its +4) is tested against the region, and so is where it was the frame before
//   (human::last_pos). On the frame a human comes in, and it isn't dead already, the region:
//   1. sets the human's killed_by_killzone flag and clears its invulnerable flag;
//   2. for a minefield, sets off the mine_killzone explosion where the human is (explosion_create, no
//      owner, Guerrilla's identity turn);
//   3. kills it: human_damage(human, no attacker, -1, DBT_NONE, ...).
// - The local player is killed again on every frame he stays inside. Guerrilla skips that for a player
//   with never_die, but then maybe_kill_player (0x77F850) kills even that player inside one; Armageddon
//   has no never_die, so here he simply dies.
// - Vehicles themselves aren't touched (only in Wrecking Crew, not ported); a human counts by its own place.
// - The enter and exit events Guerrilla's scripts wait for (0x14, 0x15) aren't posted: no mission script
//   is ported.
//
// The kill is Armageddon's own instant death from its damage_region (0x7C2160-0x7C2220): the same two
// flags (Armageddon's killed_by_killzone is human +0xBC bit 14, invulnerable +0xB0 bit 6), a damage
// record (0x42E960 makes it: +0 the victim, +4 the attacker, +8 the damage, +0xC the kind) given to the
// human damage function (0x85E0C0) with -1, which kills, as Guerrilla's -1 does. The explosion is
// explosion_create (0x6E6930), its explosion found by name (0x6EAE30); explosions.xtbl has mine_killzone
// in both games.
//
// The regions come from the player's own copy of Guerrilla: editor\GuerrillaBuilder.exe writes them to
// mods\guerrilla_campaign\kill_regions.txt with the people. [GuerrillaCampaign] KillRegions=0 turns them off.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "kill_regions.h"
#include <stdlib.h>
#include <fstream>
#include <sstream>
#include <string>
#include <iterator>
#include <unordered_map>
#include <vector>

namespace campaign::kill_regions {
namespace {

// ---- Armageddon's people

void* const kWorld = reinterpret_cast<void*>(0x1870630);
using CountFn = int(__fastcall*)(void* world, void* unused);
using AtFn = BYTE*(__fastcall*)(void* world, void* unused, int index);
auto const HumanCount = reinterpret_cast<CountFn>(0x43A860);
auto const HumanAt = reinterpret_cast<AtFn>(0x43A8B0);

constexpr DWORD kHandle = 0x68, kTeam = 0x13B4, kInVehicle = 0x1160;
constexpr DWORD kFlags = 0xB0, kFlags4 = 0xBC;  // human_flags, its first and fourth words
constexpr DWORD kInvulnerable = 1u << 6;        // in the first word
constexpr DWORD kKilledByKillzone = 1u << 14;   // in the fourth

using IsDeadFn = bool(__thiscall*)(void* object);
bool IsDead(BYTE* object) {
    void** functions = *reinterpret_cast<void***>(object);
    return reinterpret_cast<IsDeadFn>(functions[4])(object);
}

// ---- Armageddon's damage and explosions

using DamageInfoInitFn = void(__thiscall*)(void* info);
using HumanDamageFn = float(__cdecl*)(void* info);
auto const DamageInfoInit = reinterpret_cast<DamageInfoInitFn>(0x42E960);
auto const HumanDamage = reinterpret_cast<HumanDamageFn>(0x85E0C0);
constexpr size_t kDamageInfoSize = 0x30;
constexpr DWORD kDamageVictim = 0x0, kDamageAttacker = 0x4, kDamageAmount = 0x8, kDamageKind = 0xC;
constexpr int kDamagedByNone = 0;  // Guerrilla's DBT_NONE, as its kill regions give
constexpr float kKill = -1.0f;     // damage -1: dies whatever its health

using ExplosionLookupFn = const void*(__cdecl*)(const char* name);
using ExplosionCreateFn = void(__cdecl*)(const void* explosion, void* owner, void* hit, const Vector* at, const float* turn, const Vector* direction,
                                         const void* weapon, int a8, int a9);
auto const ExplosionLookup = reinterpret_cast<ExplosionLookupFn>(0x6EAE30);
auto const ExplosionCreate = reinterpret_cast<ExplosionCreateFn>(0x6E6930);
const char* const kMineExplosion = "mine_killzone";  // Guerrilla's MINE_KILLZONE_EXPLOSION_NAME
const float kIdentity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

// ---- The regions

struct Region {
    unsigned handle = 0;
    std::string zone;
    bool mine = false, sphere = false, enabled = true, notInMission = false, notInActivity = false;
    Vector at{}, right{}, up{}, forward{}, least{}, most{};
    float radius = 0;
    Vector boxLeast{}, boxMost{};  // the world's box around it, to pass over most people quickly
};

std::vector<Region> g_regions;
bool g_on = true;                 // KillRegions=
bool g_said = false;              // the log has how many there are
const void* g_mineExplosion = nullptr;
bool g_lookedUp = false;

// Where each human was the frame before (Guerrilla's human::last_pos), by handle.
struct Seen {
    Vector at;
    unsigned frame;
};
std::unordered_map<DWORD, Seen> g_seen;
unsigned g_frame = 0;
int g_npcLines = 0;                // NPC kills written to the log this level
constexpr int kMostNpcLines = 40;
unsigned g_playerLoggedIn = 0;     // the region the player's death in was logged for (0: none)
int g_playerKilledAt = 0;          // the game's clock then
bool g_playerAliveSaid = false;    // the log says he lived through it

float Dot(const Vector& a, const Vector& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vector Read3(std::istringstream& in) {
    Vector v{};
    in >> v.x >> v.y >> v.z;
    return v;
}

// ix_point_in_box_oriented / ix_point_in_sphere.
bool Inside(const Region& region, const Vector& p) {
    if (p.x < region.boxLeast.x || p.x > region.boxMost.x || p.y < region.boxLeast.y || p.y > region.boxMost.y || p.z < region.boxLeast.z ||
        p.z > region.boxMost.z)
        return false;
    const Vector d{p.x - region.at.x, p.y - region.at.y, p.z - region.at.z};
    if (region.sphere) return Dot(d, d) <= region.radius * region.radius;
    const float x = Dot(d, region.right), y = Dot(d, region.up), z = Dot(d, region.forward);
    return x >= region.least.x && x <= region.most.x && y >= region.least.y && y <= region.most.y && z >= region.least.z && z <= region.most.z;
}

void WorldBox(Region& region) {
    if (region.sphere) {
        const float r = region.radius;
        region.boxLeast = {region.at.x - r, region.at.y - r, region.at.z - r};
        region.boxMost = {region.at.x + r, region.at.y + r, region.at.z + r};
        return;
    }
    region.boxLeast = {1e30f, 1e30f, 1e30f};
    region.boxMost = {-1e30f, -1e30f, -1e30f};
    for (int corner = 0; corner < 8; ++corner) {
        const float x = corner & 1 ? region.most.x : region.least.x;
        const float y = corner & 2 ? region.most.y : region.least.y;
        const float z = corner & 4 ? region.most.z : region.least.z;
        const Vector p{region.at.x + region.right.x * x + region.up.x * y + region.forward.x * z,
                       region.at.y + region.right.y * x + region.up.y * y + region.forward.y * z,
                       region.at.z + region.right.z * x + region.up.z * y + region.forward.z * z};
        region.boxLeast = {p.x < region.boxLeast.x ? p.x : region.boxLeast.x, p.y < region.boxLeast.y ? p.y : region.boxLeast.y,
                           p.z < region.boxLeast.z ? p.z : region.boxLeast.z};
        region.boxMost = {p.x > region.boxMost.x ? p.x : region.boxMost.x, p.y > region.boxMost.y ? p.y : region.boxMost.y,
                          p.z > region.boxMost.z ? p.z : region.boxMost.z};
    }
}

const char* Kind(const Region& region) { return region.mine ? "a minefield" : "a pit"; }

// What trigger_region_do_frame does to a human who came in (entering), or to the player still inside.
void Kill(BYTE* human, const Region& region, bool entering) {
    *reinterpret_cast<DWORD*>(human + kFlags4) |= kKilledByKillzone;
    *reinterpret_cast<DWORD*>(human + kFlags) &= ~kInvulnerable;
    Vector* at = reinterpret_cast<Vector*>(human + 4);
    if (entering && region.mine && g_mineExplosion) ExplosionCreate(g_mineExplosion, nullptr, nullptr, at, kIdentity, nullptr, nullptr, 0, 0);
    alignas(8) BYTE damage[kDamageInfoSize];
    DamageInfoInit(damage);
    *reinterpret_cast<BYTE**>(damage + kDamageVictim) = human;
    *reinterpret_cast<void**>(damage + kDamageAttacker) = nullptr;
    *reinterpret_cast<float*>(damage + kDamageAmount) = kKill;
    *reinterpret_cast<int*>(damage + kDamageKind) = kDamagedByNone;
    HumanDamage(damage);

    const bool inVehicle = *reinterpret_cast<const DWORD*>(human + kInVehicle) != 0xFFFFFFFF;
    if (human == Player()) {
        if (g_playerLoggedIn == region.handle) return;
        g_playerLoggedIn = region.handle;
        g_playerKilledAt = GameTime();
        g_playerAliveSaid = false;
        rfa::Log("Kill region 0x%08X (%s, %s of Guerrilla's): the player came in at (%.0f, %.0f, %.0f)%s and was killed", region.handle, region.zone.c_str(), Kind(region),
                 at->x, at->y, at->z, inVehicle ? " in a vehicle" : "");
        return;
    }
    if (g_npcLines > kMostNpcLines) return;
    if (g_npcLines++ == kMostNpcLines) {
        rfa::LogDetail("Kill regions: more NPCs killed this level; no more are written to the log");
        return;
    }
    rfa::LogDetail("Kill region 0x%08X (%s, %s of Guerrilla's): killed an NPC 0x%08X (team %d)%s at (%.0f, %.0f, %.0f)", region.handle, region.zone.c_str(), Kind(region),
             *reinterpret_cast<const DWORD*>(human + kHandle), *reinterpret_cast<const int*>(human + kTeam), inVehicle ? " in a vehicle" : "", at->x, at->y, at->z);
}

}  // namespace

void Init() {
    g_on = rfa::IniInt(L"GuerrillaCampaign", L"KillRegions", 1) != 0;
    if (!g_on) {
        rfa::Log("Guerrilla's kill regions are off (KillRegions=0 in [GuerrillaCampaign])");
        return;
    }
    std::ifstream file(ModFolder() + L"\\kill_regions.txt");
    for (std::string line; std::getline(file, line);) {
        std::istringstream in(line);
        std::string word, handle, kind, shape;
        if (!(in >> word) || word != "region") continue;
        Region region;
        int enabled = 1, notInMission = 0, notInActivity = 0;
        if (!(in >> handle >> region.zone >> kind >> shape >> enabled >> notInMission >> notInActivity)) continue;
        region.handle = static_cast<unsigned>(strtoul(handle.c_str(), nullptr, 16));
        region.mine = kind == "mine";
        region.sphere = shape == "sphere";
        region.enabled = enabled != 0;
        region.notInMission = notInMission != 0;
        region.notInActivity = notInActivity != 0;
        region.at = Read3(in);
        region.right = Read3(in);
        region.up = Read3(in);
        region.forward = Read3(in);
        region.least = Read3(in);
        region.most = Read3(in);
        if (!(in >> region.radius)) continue;
        WorldBox(region);
        g_regions.push_back(region);
    }
    if (g_regions.empty())
        rfa::Log("No kill_regions.txt (editor\\GuerrillaBuilder.exe build people writes it): Guerrilla's pits and minefields don't kill");
}

void Enter() {
    g_seen.clear();
    g_npcLines = 0;
    g_playerLoggedIn = 0;
    if (!g_on || g_regions.empty()) return;
    if (!g_lookedUp) {
        g_lookedUp = true;
        g_mineExplosion = ExplosionLookup(kMineExplosion);
    }
    if (g_said) return;
    g_said = true;
    int mines = 0;
    for (const Region& region : g_regions) mines += region.mine ? 1 : 0;
    rfa::Log("kill_regions.txt: %u of Guerrilla's kill regions (%u pits, %d minefields); the mines' explosion %s %s", static_cast<unsigned>(g_regions.size()),
             static_cast<unsigned>(g_regions.size()) - mines, mines, kMineExplosion, g_mineExplosion ? "found" : "NOT FOUND in explosions.xtbl (the minefields kill without it)");
}

void Frame(bool activityRunning) {
    if (!g_on || g_regions.empty()) return;
    ++g_frame;
    // Guerrilla's missions aren't ported, so none runs: not_in_mission never rests a region here.
    const bool missionRunning = false;
    BYTE* player = static_cast<BYTE*>(Player());
    bool playerInside = false;
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!human) continue;
        const DWORD handle = *reinterpret_cast<const DWORD*>(human + kHandle);
        const Vector now = PositionOf(human);
        auto seen = g_seen.find(handle);
        const Vector before = seen != g_seen.end() ? seen->second.at : now;  // first seen: as if it had stood there
        g_seen[handle] = {now, g_frame};
        for (const Region& region : g_regions) {
            if (!region.enabled || (region.notInMission && missionRunning) || (region.notInActivity && activityRunning)) continue;
            const bool nowIn = Inside(region, now);
            if (!nowIn) continue;
            if (human == player) playerInside = true;
            const bool wasIn = Inside(region, before);
            if (!wasIn && !IsDead(human)) Kill(human, region, true);
            if (human == player && !IsDead(human)) Kill(human, region, false);
        }
    }
    // A second after his death in one, the player should be dead: say so if he isn't.
    if (g_playerLoggedIn && player && !g_playerAliveSaid && GameTime() - g_playerKilledAt > 1000 && !IsDead(player)) {
        g_playerAliveSaid = true;
        rfa::Log("Kill region 0x%08X: the player is STILL ALIVE a second after its kill (inside it: %s)", g_playerLoggedIn, playerInside ? "yes" : "no");
    }
    if (!playerInside && GameTime() - g_playerKilledAt > 1000) g_playerLoggedIn = 0;
    // People gone from the world: forget them now and then.
    if (g_frame % 600 == 0)
        for (auto it = g_seen.begin(); it != g_seen.end();) it = it->second.frame != g_frame ? g_seen.erase(it) : std::next(it);
}

}  // namespace campaign::kill_regions
