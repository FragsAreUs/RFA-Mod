// Guerrilla's people's free-roam lines (the main session, for the user: "make people speak", 2026-11-18). [RM, A]
//
// Guerrilla's people speak through human::play_voice_line (RM 0xA9AC00, by enum voice_lines; RM 0xA9A8F0 by handle),
// called from about a hundred places, most of them AI actions Armageddon deleted. guerrilla_voices (the kiosk
// session's) plays a line by its rfg_voice_lines name on a person with Guerrilla's rules for it (the persona has it,
// its local and global cooldowns, its chance, its priority, its delay); here is when, as Guerrilla's callers say it:
//   - pain and death: human_play_pain_sound (RM 0xAB1260, from human_damage RM 0xACED70): voc_pain on a hit, voc_death
//     on the killing one. Armageddon's human_damage (0x85E0C0) is its own and guerrilla_vehicles hooks it, so here a
//     person within 60 m of the player is watched each frame: its hit points (+0xF24) going down say voc_pain, its
//     death voc_death;
//   - spotting: record_los_result_enemy and its kin (RM 0x6B9A22, ...): an EDF com_src_spot_enemy, a Marauder
//     com_enemy_gen_spot_to_ally, a guerrilla com_enemy_edf_spot_to_ally or com_enemy_mar_spot_to_ally by the enemy's
//     side; here as a person's combat target (0x880B70) comes from none;
//   - a grenade: ai_perception::threw_grenade (RM 0x5EEE07, VOICE_LINE_COMBAT_THROW_GRENADE_GROUP):
//     com_wep_grenade_throw_to_ally; here as the thrower's own throw timer (npc +0x1034) is set again;
//   - the others where our own code does what Guerrilla's action did: a walker's flee (flee_action::enter RM 0x5F2E6D:
//     voc_panic; flee_to_safety_goal RM 0x6ABBEE: com_ally_combat_flee for a guerrilla), a convert joining
//     (maybe_play_join_player_voice_line RM 0x5E4220: com_ally_combat_join), an ambush's squad
//     (maybe_play_ambush_voice_line RM 0xA66EA0: voc_ambush), the everyday lines at an action node that allows them
//     (play_ambient_voice_lines RM 0x68F320: every 5-15 s the player within 15 m greeted, amb_pc_greet_morale_*,
//     else amb_talkself_morale_*, by the district's morale) and a walker greeting the player near it.
// [GuerrillaPeople] VoiceLog=1 writes a summary each minute (off by default).

#define RFA_MOD_NAME "guerrilla_people"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "campaign/campaign_api.h"
#include "voices/voices_api.h"
#include "voices.h"
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr DWORD kPosition = 0x04, kHandle = 0x68, kHitPoints = 0xF24, kTeam = 0x13B4, kOwnThrowTimer = 0x1034;
void* const kWorld = reinterpret_cast<void*>(0x1870630);
using CountFn = int(__fastcall*)(void* world, void*);
using AtFn = BYTE*(__fastcall*)(void* world, void*, int index);
auto const NpcCount = reinterpret_cast<CountFn>(0x424480);
auto const NpcAt = reinterpret_cast<AtFn>(0x4244D0);
auto const GoneOrDead = reinterpret_cast<bool(__cdecl*)(BYTE* object)>(0x42ADC0);
auto const CombatTarget = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human)>(0x880B70);
auto const Player = reinterpret_cast<BYTE*(__cdecl*)()>(0x8B92D0);
int GameTime() { return *reinterpret_cast<const int*>(0x1121300); }
constexpr int kGuerrillaTeam = 0, kEdfTeam = 1, kMarauderTeam = 3;
constexpr float kHeard = 60.0f;  // people farther from the player aren't watched (their lines are 3D anyway)

bool g_log = false, g_told = false;

// Whether guerrilla_voices runs, which plays the lines (GuerrillaVoices_Play); said in the log the first time it does.
bool Play() {
    if (!guerrilla_voices::Running()) return false;
    if (!g_told) {
        g_told = true;
        rfa::LogDetail("Voices: Guerrilla's people speak their free-roam lines through guerrilla_voices (pain, death, spotting, grenades, panic, joining, "
                 "ambushes, everyday lines)");
    }
    return true;
}
struct Watched { float hp = -1; bool dead = false, hadTarget = false; int throwTimer = 0; int seen = 0; };
std::unordered_map<DWORD, Watched> g_watched;
int g_nextWatch = 0, g_nextReport = 0, g_lastClock = 0;
int g_said = 0, g_refused = 0;
std::unordered_map<std::string, int> g_byLine;

float Away(const BYTE* a, const BYTE* b) {
    const float* p = reinterpret_cast<const float*>(a + kPosition);
    const float* q = reinterpret_cast<const float*>(b + kPosition);
    const float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The player's district's morale over its most (guerrilla_campaign's GuerrillaCampaign_District), 0.5 without it.
float Morale() {
    GuerrillaCampaignDistrict info{};
    if (!GuerrillaCampaign_District(-1, &info) || info.moraleMax <= 0) return 0.5f;
    return info.morale / info.moraleMax;
}

}  // namespace

void StartVoices() {
    g_log = rfa::ReadLogSwitch(L"GuerrillaPeople", L"VoiceLog");
}

void VoiceSay(unsigned char* human, const char* line) {
    if (!human || !line || !Play()) return;
    const int status = GuerrillaVoices_Play(human, line, -1);
    if (status == 0) {
        ++g_said;
        if (g_log) ++g_byLine[line];
    }
    else ++g_refused;
}

const char* VoiceGreetLine() {
    const float m = Morale();
    return m < 1.0f / 3 ? "amb_pc_greet_morale_low" : m < 2.0f / 3 ? "amb_pc_greet_morale_med" : "amb_pc_greet_morale_high";
}

const char* VoiceTalkToSelfLine() {
    const float m = Morale();
    return m < 1.0f / 3 ? "amb_talkself_morale_low" : m < 2.0f / 3 ? "amb_talkself_morale_med" : "amb_talkself_morale_high";
}

void VoicesFrame(bool ours) {
    if (!Play()) return;
    const int now = GameTime();
    if (!ours || now < g_lastClock) {
        g_watched.clear();
        g_lastClock = now;
        return;
    }
    g_lastClock = now;
    if (now < g_nextWatch) return;
    g_nextWatch = now + 100;
    BYTE* player = Player();
    if (!player) return;
    const int people = NpcCount(kWorld, nullptr);
    for (int i = 0; i < people; ++i) {
        BYTE* human = NpcAt(kWorld, nullptr, i);
        if (!human || human == player || Away(human, player) > kHeard) continue;
        Watched& w = g_watched[*reinterpret_cast<const DWORD*>(human + kHandle)];
        const bool dead = GoneOrDead(human);
        const float hp = *reinterpret_cast<const float*>(human + kHitPoints);
        const int throwTimer = *reinterpret_cast<const int*>(human + kOwnThrowTimer);
        BYTE* target = dead ? nullptr : CombatTarget(human);
        if (w.seen != 0) {  // known from an earlier look: what changed since
            if (dead && !w.dead) VoiceSay(human, "voc_death");
            else if (!dead && w.hp > 0 && hp < w.hp - 0.01f) VoiceSay(human, "voc_pain");
            if (!dead && throwTimer != w.throwTimer && w.throwTimer != 0) VoiceSay(human, "com_wep_grenade_throw_to_ally");
            if (target && !w.hadTarget) {
                const int team = *reinterpret_cast<const int*>(human + kTeam);
                const int theirs = *reinterpret_cast<const int*>(target + kTeam);
                if (team == kEdfTeam) VoiceSay(human, "com_src_spot_enemy");
                else if (team == kMarauderTeam) VoiceSay(human, "com_enemy_gen_spot_to_ally");
                else if (team == kGuerrillaTeam && theirs == kEdfTeam) VoiceSay(human, "com_enemy_edf_spot_to_ally");
                else if (team == kGuerrillaTeam && theirs == kMarauderTeam) VoiceSay(human, "com_enemy_mar_spot_to_ally");
            }
        }
        w.dead = dead;
        w.hp = hp;
        w.throwTimer = throwTimer;
        w.hadTarget = target != nullptr;
        w.seen = now;
    }
    for (auto it = g_watched.begin(); it != g_watched.end();) it = now - it->second.seen > 5000 ? g_watched.erase(it) : std::next(it);
    if (g_log && now >= g_nextReport) {
        g_nextReport = now + 60000;
        std::string lines;
        for (const auto& [line, count] : g_byLine) lines += (lines.empty() ? "" : ", ") + line + " " + std::to_string(count);
        rfa::LogDetail("Voices: %d lines said, %d refused by their rules so far; %s", g_said, g_refused, lines.empty() ? "none yet" : lines.c_str());
    }
}
