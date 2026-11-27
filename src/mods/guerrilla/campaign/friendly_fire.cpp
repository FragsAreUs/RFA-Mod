// Guerrilla's friendly fire, which Armageddon dropped: a guerrilla the player hurts, or who hears him kill one of his
// friends, turns on him until 20 s pass without more of it.
//
// Guerrilla's (Re-Mars-tered; the PDB's ai_perception::player_friendly_fire_info, 12 bytes at perception +0x1A0:
// enabled, damage_total, valid_time):
// - reset (RM 0x5ECF90, inlined in ai_perception::clear 0x63E400: the AI's start, its reset, death, leaving the
//   player's squad): enabled only for team Guerrilla (0) or Marauder (3), when the side isn't hostile to the player's,
//   and not for a guerrilla_tech (human_info flags +0x17D bit 5; no character table row of either Guerrilla sets it).
//   So a civilian who joins the guerrillas later keeps a disabled record.
// - get_effective_disposition (RM 0x665AF0): to the local player, an NPC whose side is ALLIED to his takes him for
//   HOSTILE while enabled, valid and damage_total >= Player_friendly_fire_threshold. Only allied ones: civilians are
//   FRIENDLY to the player's side and never turn; free roam's Marauders are hostile anyway. So it is the Red Faction's
//   own people, who stand around the safehouses.
// - register_fire (RM 0x5ECFF0; amount, the NPC, the attacker): nothing when disabled or the alert level is above
//   green (in a mission, only a Marauder hit by a non-Marauder). An amount above 0 is capped at threshold / 3, one of
//   0 or less (a kill heard, a shout) counts as the whole threshold. damage_total += it, the time set to
//   Player_friendly_fire_expire_time s; true when the total reaches the threshold.
// - do_frame (RM 0x5ED0D0): when that time has passed, damage_total 0 and the time unset: friendly again.
// What calls register_fire:
// - ai_perception::register_hit (RM 0x6B9270, from human_damage 0xACF4BF for an NPC who lives on): the local player's
//   hit with a weapon, by the damage (as human_damage has scaled it).
// - pain_sound_heard (RM 0x6B8E10, npc_sound_info_pain/_death::inform_npc 0xA33748; human_play_pain_sound 0xAB1260
//   from human_damage, after register_hit): -1 when the hearer isn't the attacker, the alert is green, the hearer's
//   side is friendly or better to the player's, the hearer's side friendly or better to the victim's
//   (human_teams_are_friendly), the victim is angry at the player himself (is_hostile) or dead, and the attacker is
//   the local player. Heard by NPCs within the sound's range of it (npc_sound_info::propagate RM 0xA83DE0): its
//   voice's audible range while it plays, else 15 m for pain (0xA0F9C0) and 20 m for death (0x1310FF4). A pain sound
//   at most once in 2 s for each human (+0xA10). (A silent death, human_damage's last flag, is heard only within
//   3 m; Armageddon's damage record doesn't say which deaths are silent, so all are heard as deaths.)
// - alert_enemy (RM 0x6B9680, alert_shout_enemy::inform_npc 0x6BBD4A; the shout made by battle_cry_action and
//   point_out_enemy_action when an NPC goes for an enemy): -1 when the alert is green, the shouter is angry
//   (is_hostile), the hearer's side is friendly or better to the guerrillas' and the enemy is the local player.
//   Heard within alert_shout's 25 m (0x12C840C) by NPCs whose side is ALLIED to the shouter's
//   (alert_generic::is_relevant_to_npc 0x60C5F0).
// - notice_projectile (RM 0x6BDD60): -1 for a projectile of the player's stuck to the NPC. Not ported.
// Numbers: tweak_table.xtbl Player_friendly_fire_threshold 75 and Player_friendly_fire_expire_time 20. So one kill a
// guerrilla hears turns him at once, as does an angry one's shout; the player's own shots take three hits (25 each).
//
// Armageddon (publicbeta): its get_effective_disposition (0x467B60) still asks perception +0x1A0's is_hostile for
// the local player, but is_hostile (0x923C70) is a folded "return false", and its notice of other people asks the
// teams' disposition (0x8A04D0) straight. So the records are kept here, by handle, and undercover.cpp's wrap of
// 0x8A04D0 asks Effective(): an angry guerrilla's answer to the player is hostile, and Armageddon's own AI notices
// him and fights, as the undercover rule's EDF do. The battle cry is taken as the moment an angry NPC's combat
// target (0x880B70) becomes the player.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "friendly_fire.h"
#include "campaign.h"
#include "handle_kinds.h"
#include "liberation.h"
#include "tables.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace campaign::friendly_fire {
namespace {

// ---- Armageddon's objects

void* const kWorld = reinterpret_cast<void*>(0x1870630);
using CountFn = int(__fastcall*)(void* world, void* unused);
using AtFn = BYTE*(__fastcall*)(void* world, void* unused, int index);
using ByHandleFn = BYTE*(__fastcall*)(void* world, void* unused, DWORD handle);
auto const HumanCount = reinterpret_cast<CountFn>(0x43A860);
auto const HumanAt = reinterpret_cast<AtFn>(0x43A8B0);
auto const HumanByHandle = reinterpret_cast<ByHandleFn>(0x436120);
using IsNpcFn = bool(__fastcall*)(const BYTE* human, void* unused);
auto const IsNpc = reinterpret_cast<IsNpcFn>(0x461130);
auto const TeamDisposition = reinterpret_cast<int(__cdecl*)(int team, int other)>(0x8A0460);
auto const CombatTarget = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human)>(0x880B70);
constexpr DWORD kHandle = 0x68, kTeam = 0x13B4, kInVehicle = 0x1160, kAi = 0x174C;
constexpr DWORD kNone = 0xFFFFFFFF;
constexpr int kGuerrillas = 0, kMarauders = 3;                    // human_teams (both games' numbers)
constexpr int kHostile = 0, kFriendly = 3, kAllied = 4;           // human_team_dispositions
constexpr int kBulletKind = 3, kExplosionKind = 2, kMeleeKind = 5; // damaged_by_types (both games): the weapons' hits

using IsDeadFn = bool(__thiscall*)(const void* object);
bool IsDead(const BYTE* object) {
    void* const* functions = *reinterpret_cast<void* const* const*>(object);
    return reinterpret_cast<IsDeadFn>(functions[4])(object);
}
DWORD HandleOf(const BYTE* object) { return *reinterpret_cast<const DWORD*>(object + kHandle); }
int TeamOf(const BYTE* human) { return *reinterpret_cast<const int*>(human + kTeam); }
float Distance2(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// ---- Guerrilla's numbers

float g_threshold = 75.0f;       // Player_friendly_fire_threshold
float g_expireSeconds = 20.0f;   // Player_friendly_fire_expire_time
constexpr float kPainHeard = 15.0f;   // npc_sound_info_pain::get_default_sound_distance
constexpr float kDeathHeard = 20.0f;  // npc_sound_info_death's (0x1310FF4)
constexpr float kShoutHeard = 25.0f;  // alert_shout::get_default_sound_distance (0x12C840C)
constexpr int kPainEveryMs = 2000;    // human_play_pain_sound's timer (+0xA10)
constexpr int kLookEveryMs = 250;     // how often new NPCs get their records
constexpr int kMostLines = 30;        // lines in the log a level

// ---- The records

struct Record {
    bool enabled = false;
    float total = 0.0f;  // damage_total
    int until = -1;      // valid_time: when it runs out on the game's clock; -1 unset
    bool wasTarget = false;  // the player was its combat target at the last look (the battle cry's moment)
};

bool g_on = false;  // set by Init (which also makes the lock)
std::unordered_map<DWORD, Record> g_records;
std::unordered_map<DWORD, int> g_painAfter;  // a human's next pain sound may come at this time
std::unordered_set<DWORD> g_deaths;          // each death heard once
int g_lastLook = -1;
int g_lines = 0;

// What the disposition wrap reads (it may run off the main thread): the angry ones' handles.
CRITICAL_SECTION g_lock;
std::vector<DWORD> g_angry;
volatile LONG g_angryCount = 0;
struct Hit {
    DWORD victim, attacker;
    float done;
    int kind;
};
std::vector<Hit> g_hits;  // queued by HumanDamaged, taken by Frame

void Line(const char* fmt, ...) {
    if (g_lines >= kMostLines) return;
    ++g_lines;
    char text[512];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(text, sizeof text, _TRUNCATE, fmt, args);
    va_end(args);
    rfa::LogDetail("Friendly fire: %s%s", text, g_lines == kMostLines ? " (no more of these lines this level)" : "");
}

bool IsAngry(const Record& r) { return r.enabled && r.until >= 0 && r.total >= g_threshold; }

// player_friendly_fire_info::reset, at the NPC's first sight here (Guerrilla's: when its AI starts).
Record MakeRecord(const BYTE* npc, const BYTE* player) {
    Record r;
    const int team = TeamOf(npc);
    r.enabled = (team == kGuerrillas || team == kMarauders) && TeamDisposition(team, TeamOf(player)) > kHostile;
    return r;
}

Record& RecordOf(const BYTE* npc, const BYTE* player) {
    auto found = g_records.find(HandleOf(npc));
    if (found != g_records.end()) return found->second;
    return g_records.emplace(HandleOf(npc), MakeRecord(npc, player)).first->second;
}

// player_friendly_fire_info::register_fire.
bool RegisterFire(Record& r, float amount) {
    if (!r.enabled) return false;
    if (amount > 0.0f) amount = (std::min)(amount, g_threshold / 3.0f);
    else amount = g_threshold;
    if (liberation::AlertLevel() > handles::AlertGreen) return false;
    r.total += amount;
    r.until = GameTime() + static_cast<int>(g_expireSeconds * 1000.0f);
    return r.total >= g_threshold;
}

// A record that went over the threshold: says so once.
void Turned(const BYTE* npc, bool before, const Record& r, const char* why, float metres) {
    if (before || !IsAngry(r)) return;
    Line("%08X (team %d) turns on the player: %s %.0f m away (%.0f of %.0f, for %.0f s)", HandleOf(npc), TeamOf(npc), why, metres, r.total,
         g_threshold, g_expireSeconds);
}

bool Hears(const BYTE* hearer, const BYTE* source, const BYTE* player, float range, const Vector& from) {
    if (!hearer || hearer == source || hearer == player || IsDead(hearer)) return false;
    if (!IsNpc(hearer, nullptr) || !*reinterpret_cast<BYTE* const*>(hearer + kAi)) return false;
    return Distance2(PositionOf(hearer), from) <= range * range;
}

// npc_sound_info_pain / _death heard (ai_perception::pain_sound_heard's friendly-fire part); the attacker is the player.
void PainHeard(const BYTE* victim, const BYTE* player, float range, bool victimDead) {
    if (liberation::AlertLevel() != handles::AlertGreen) return;
    if (!victimDead) {
        auto found = g_records.find(HandleOf(victim));
        if (found == g_records.end() || !IsAngry(found->second)) return;
    }
    const Vector from = PositionOf(victim);
    const int victimTeam = TeamOf(victim), playerTeam = TeamOf(player);
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* hearer = HumanAt(kWorld, nullptr, i);
        if (!Hears(hearer, victim, player, range, from)) continue;
        const int team = TeamOf(hearer);
        if (TeamDisposition(team, playerTeam) < kFriendly || TeamDisposition(team, victimTeam) < kFriendly) continue;
        Record& r = RecordOf(hearer, player);
        const bool before = IsAngry(r);
        RegisterFire(r, -1.0f);
        Turned(hearer, before, r, victimDead ? "heard him kill someone of his friends'," : "heard him hurt an angry friend,",
               sqrtf(Distance2(PositionOf(hearer), from)));
    }
}

// alert_shout_enemy from an angry NPC about the player (alert_enemy's friendly-fire part).
void Shout(const BYTE* shouter, const BYTE* player) {
    if (liberation::AlertLevel() != handles::AlertGreen) return;
    const Vector from = PositionOf(shouter);
    const int shouterTeam = TeamOf(shouter);
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* hearer = HumanAt(kWorld, nullptr, i);
        if (!Hears(hearer, shouter, player, kShoutHeard, from)) continue;
        const int team = TeamOf(hearer);
        if (TeamDisposition(team, shouterTeam) != kAllied || TeamDisposition(team, kGuerrillas) < kFriendly) continue;
        Record& r = RecordOf(hearer, player);
        const bool before = IsAngry(r);
        RegisterFire(r, -1.0f);
        Turned(hearer, before, r, "an angry friend shouted about him,", sqrtf(Distance2(PositionOf(hearer), from)));
    }
}

bool IsPlayerOrHisVehicle(DWORD handle, const BYTE* player) {
    if (handle == kNone) return false;
    const DWORD vehicle = *reinterpret_cast<const DWORD*>(player + kInVehicle);
    return handle == HandleOf(player) || (vehicle != kNone && handle == vehicle);
}

void PublishAngry() {
    std::vector<DWORD> angry;
    for (const auto& [handle, r] : g_records)
        if (IsAngry(r)) angry.push_back(handle);
    EnterCriticalSection(&g_lock);
    g_angry.swap(angry);
    InterlockedExchange(&g_angryCount, static_cast<LONG>(g_angry.size()));
    LeaveCriticalSection(&g_lock);
}

}  // namespace

bool On() { return g_on; }

void Init() {
    InitializeCriticalSection(&g_lock);
    if (rfa::IniInt(L"GuerrillaCampaign", L"FriendlyFire", 1) == 0) {
        rfa::LogDetail("Friendly fire: off (FriendlyFire=0 in [GuerrillaCampaign]): the guerrillas never turn on the player");
        return;
    }
    std::vector<tables::Element> rows;
    if (tables::Load("tweak_table", rows)) {
        for (const tables::Element& row : rows) {
            const std::string name = row.Text("Name");
            if (_stricmp(name.c_str(), "Player_friendly_fire_threshold") == 0) g_threshold = row.Number("Value", g_threshold);
            else if (_stricmp(name.c_str(), "Player_friendly_fire_expire_time") == 0) g_expireSeconds = row.Number("Value", g_expireSeconds);
        }
    }
    if (!(g_threshold > 0.0f)) g_threshold = 75.0f;
    g_on = true;
    rfa::LogDetail("Friendly fire: a guerrilla the player hurts (%.0f a hit at most) or who hears him kill a friend turns on him at %.0f, until %.0f s pass without more",
                   g_threshold / 3.0f, g_threshold, g_expireSeconds);
}

void Enter() {
    g_records.clear();
    g_painAfter.clear();
    g_deaths.clear();
    g_lastLook = -1;
    g_lines = 0;
    EnterCriticalSection(&g_lock);
    g_hits.clear();
    LeaveCriticalSection(&g_lock);
    PublishAngry();
}

void HumanDamaged(BYTE* victim, BYTE* attacker, float done, int kind) {
    if (!g_on || !victim) return;
    const Hit hit{HandleOf(victim), attacker ? HandleOf(attacker) : kNone, done, kind};
    EnterCriticalSection(&g_lock);
    if (g_hits.size() < 256) g_hits.push_back(hit);
    LeaveCriticalSection(&g_lock);
}

void HumanKilled(BYTE* victim, BYTE* attacker) {
    BYTE* player = static_cast<BYTE*>(Player());
    if (!g_on || !player || !victim || victim == player || !attacker) return;
    if (!g_deaths.insert(HandleOf(victim)).second) return;
    const bool angry = g_records.erase(HandleOf(victim)) > 0;  // ai_system::do_death_func → clear → reset
    if (angry) PublishAngry();
    if (!IsPlayerOrHisVehicle(HandleOf(attacker), player)) return;
    PainHeard(victim, player, kDeathHeard, true);
    PublishAngry();
}

void Frame() {
    if (!g_on) return;
    BYTE* player = static_cast<BYTE*>(Player());
    if (!player) return;
    const int now = GameTime();
    bool changed = false;

    // New NPCs get their records (reset), the gone and the dead lose theirs.
    if (g_lastLook < 0 || now < g_lastLook || now - g_lastLook >= kLookEveryMs) {
        g_lastLook = now;
        std::unordered_set<DWORD> here;
        const int count = HumanCount(kWorld, nullptr);
        for (int i = 0; i < count; ++i) {
            BYTE* human = HumanAt(kWorld, nullptr, i);
            if (!human || human == player || IsDead(human) || !IsNpc(human, nullptr)) continue;
            here.insert(HandleOf(human));
            RecordOf(human, player);
        }
        for (auto it = g_records.begin(); it != g_records.end();) {
            if (here.count(it->first)) { ++it; continue; }
            changed = changed || IsAngry(it->second);
            it = g_records.erase(it);
        }
        for (auto it = g_painAfter.begin(); it != g_painAfter.end();) {
            if (now >= it->second || it->second - now > kPainEveryMs) it = g_painAfter.erase(it);  // passed (or from another clock)
            else ++it;
        }
    }

    // The hits since the last frame: register_hit, then the pain sound (human_damage's order).
    std::vector<Hit> hits;
    EnterCriticalSection(&g_lock);
    hits.swap(g_hits);
    LeaveCriticalSection(&g_lock);
    for (const Hit& hit : hits) {
        BYTE* victim = HumanByHandle(kWorld, nullptr, hit.victim);
        if (!victim || victim == player || IsDead(victim)) continue;
        const auto pain = g_painAfter.find(hit.victim);
        const bool painPlays = pain == g_painAfter.end() || now >= pain->second;
        if (painPlays) g_painAfter[hit.victim] = now + kPainEveryMs;
        if (!IsPlayerOrHisVehicle(hit.attacker, player)) continue;
        if (hit.attacker == HandleOf(player) && (hit.kind == kBulletKind || hit.kind == kExplosionKind || hit.kind == kMeleeKind)) {
            Record& r = RecordOf(victim, player);
            const bool before = IsAngry(r);
            RegisterFire(r, hit.done);
            Turned(victim, before, r, "hurt by him", sqrtf(Distance2(PositionOf(victim), PositionOf(player))));
            changed = changed || IsAngry(r) != before;
        }
        if (painPlays) {
            PainHeard(victim, player, kPainHeard, false);
            changed = true;
        }
    }

    // The battle cries: an angry one going for the player shouts.
    std::vector<DWORD> shouters;
    for (auto& [handle, r] : g_records) {
        if (!IsAngry(r)) {
            r.wasTarget = false;
            continue;
        }
        BYTE* npc = HumanByHandle(kWorld, nullptr, handle);
        const bool target = npc && CombatTarget(npc) == player;
        if (target && !r.wasTarget) shouters.push_back(handle);
        r.wasTarget = target;
    }
    for (DWORD handle : shouters)
        if (BYTE* npc = HumanByHandle(kWorld, nullptr, handle)) {
            Shout(npc, player);
            changed = true;
        }

    // do_frame: the records whose time has passed.
    for (auto& [handle, r] : g_records) {
        if (r.until < 0 || now < r.until) continue;
        if (IsAngry(r)) {
            Line("%08X calms down: %.0f s without more", handle, g_expireSeconds);
            changed = true;
        }
        r.total = 0.0f;
        r.until = -1;
    }
    if (changed) PublishAngry();
}

int Effective(const BYTE* npc, const BYTE* other, int disposition) {
    if (!g_on || disposition != kAllied || !npc || !other || npc == other) return disposition;
    if (InterlockedCompareExchange(&g_angryCount, 0, 0) == 0 || other != Player()) return disposition;
    const DWORD handle = HandleOf(npc);
    EnterCriticalSection(&g_lock);
    const bool angry = std::find(g_angry.begin(), g_angry.end(), handle) != g_angry.end();
    LeaveCriticalSection(&g_lock);
    return angry ? kHostile : disposition;
}

}  // namespace campaign::friendly_fire
