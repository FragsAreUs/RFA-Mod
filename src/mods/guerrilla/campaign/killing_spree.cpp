// Guerrilla's killing spree (activity_killing_spree_data, mode 5, a free activity: it runs alongside the others and
// starts itself).
//
// One of Guerrilla's damage listeners, it hears every death (register_death RM 0x7ADAD0). A death counts when the
// victim is a human killed by the player (activity_register_damage_is_player_controlled on the killer), or knocked
// over by him and killed by a car, an object or a fall (ragdolled, its ragdoll source the player, its last hit
// DBT_CAR, DBT_LEVEL_OBJECT or DBT_WORLD); then by how the victim's team takes the player's:
// - hostile (the EDF, the Marauders), within 150 m of the player on the ground (x and z only): while a spree runs, one
//   more kill (record_kill 0x758D60), the timer kept at Killing_spree_additional_period_ms (7 s) at least; otherwise
//   a kill tallied (add_kill_to_tally 0x758D40), the tally starting again with Killing_spree_initial_time_ms (6 s) when
//   its time is out, and Killing_spree_initial_kills (3) in that time start one (start_killing_spree 0x7AD9C0: the
//   timer Killing_spree_start_period_ms, 7 s; the tool tip Killing_Spree);
// - friendly or allied (a civilian, a guerrilla): a spree running ends now, its reward given;
// - otherwise nothing.
// It ends when its timer runs out (do_frame_active 0x758D10): morale in the player's district, one point a kill
// (end 0x7ADA20: gfm_change_district_morale with the kill count; the difficulty scales it, as liberation::ChangeMorale
// does; Killing_spree_base_morale_delta is read by nothing). Killed (a mission, another activity), it gives nothing
// (kill 0x798990). The diversions panel shows KILLING SPREE with the kills (get_ui_display_info 0x758CB0: "%i", the
// time left). Guerrilla's numbers are its tweak_table.xtbl's (its code has none of its own).
// Not ported: its sound (HUD_DIV_SPREE_ICON: the interface's sound banks are full), the statistics count
// (STATS_KILLING_SPREES), Guerrilla's marks (an object the player hurt, step 5b).
//
// Armageddon's human: its position +4, its ragdoll's source (the human who knocked it over) +0x778 (the ragdoll
// start 0x8996F0 writes it), its ragdoll +0x794 (-1 none; I: gear's reading), its last hit's kind +0xF4C (its combat
// record at +0xF3C, as Guerrilla's human_combat at +0x928: last_hit_by_damage_type +0x10; both games' damaged_by_types).
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "activity_base.h"
#include "liberation.h"
#include "messages.h"
#include "tables.h"
#include <stdio.h>
#include <string.h>

namespace campaign::activities {
namespace {

constexpr DWORD kHandle = 0x68, kObjectType = 0x7A, kRagdollSource = 0x778, kRagdoll = 0x794, kLastHitKind = 0xF4C;
constexpr BYTE kHumanType = 0;  // object_type OT_HUMAN
constexpr int kByCar = 1, kByLevelObject = 4, kByWorld = 6;  // damaged_by_types
constexpr int kHostile = 0, kFriendly = 3;                   // human_team_dispositions
constexpr float kReachSquared = 22500.0f;                   // 150 m, on the ground

// tweak_table.xtbl (Guerrilla's values; its code keeps them in BSS, the table is their only source).
int g_startPeriodMs = 7000;       // Killing_spree_start_period_ms
int g_additionalPeriodMs = 7000;  // Killing_spree_additional_period_ms
int g_initialKills = 3;           // Killing_spree_initial_kills
int g_initialTimeMs = 6000;       // Killing_spree_initial_time_ms
constexpr int kTipMs = 500;       // ui_tool_tips_add_tip("Killing_Spree", 500)

void LoadTweaks() {
    std::vector<tables::Element> rows;
    if (!tables::Load("tweak_table", rows)) {
        rfa::Log("Killing spree: no tweak_table.xtbl; Guerrilla's usual numbers");
        return;
    }
    struct Tweak {
        const char* name;
        int* value;
    };
    const Tweak tweaks[] = {{"Killing_spree_start_period_ms", &g_startPeriodMs},
                            {"Killing_spree_additional_period_ms", &g_additionalPeriodMs},
                            {"Killing_spree_initial_kills", &g_initialKills},
                            {"Killing_spree_initial_time_ms", &g_initialTimeMs}};
    for (const tables::Element& row : rows)
        for (const Tweak& tweak : tweaks)
            if (_stricmp(row.Text("Name").c_str(), tweak.name) == 0)
                *tweak.value = static_cast<int>(row.Number("Value", static_cast<float>(*tweak.value)));
    rfa::Log("Killing spree: %d kills in %d ms start one, %d ms to go on, %d ms after each kill", g_initialKills, g_initialTimeMs,
             g_startPeriodMs, g_additionalPeriodMs);
}

class KillingSpreeActivity : public Activity {
public:
    KillingSpreeActivity() : Activity(KillingSpree, false, true) { LoadTweaks(); }

    bool Listens() const override { return true; }
    const char* Title() const override { return "ACT_KILLING_SPREE"; }

    bool UiText(std::string& text, int& number) const override {
        char kills[16];
        sprintf_s(kills, "%i", count_);
        text = kills;
        number = TimeLeft();
        return true;
    }

    void RegisterDeath(void* victim, void* killer) override {
        BYTE* player = static_cast<BYTE*>(Player());
        if (!victim || !player) return;
        const BYTE* human = static_cast<const BYTE*>(victim);
        if (human[kObjectType] != kHumanType) return;  // a human's death only (the listeners hear vehicles' and props' too)
        const DWORD playerHandle = *reinterpret_cast<const DWORD*>(player + kHandle);
        // Knocked over by the player and killed by a car, an object or the world.
        bool knockedOver = false;
        if (*reinterpret_cast<const int*>(human + kRagdoll) != -1 && *reinterpret_cast<const DWORD*>(human + kRagdollSource) == playerHandle) {
            const int kind = *reinterpret_cast<const int*>(human + kLastHitKind);
            knockedOver = kind == kByCar || kind == kByLevelObject || kind == kByWorld;
        }
        if (!PlayerControlled(killer) && !knockedOver) return;
        const int disposition = DispositionToPlayer(victim);
        if (disposition == kHostile) {
            const Vector at = PositionOf(victim), from = PositionOf(player);
            const float dx = at.x - from.x, dz = at.z - from.z;
            if (dx * dx + dz * dz > kReachSquared) return;
            if (IsActive()) {
                // record_kill
                ++count_;
                if (TimeLeft() < g_additionalPeriodMs) expiresAt_ = GameTime() + g_additionalPeriodMs;
                rfa::LogDetail("Killing spree: %d kills", count_);
                return;
            }
            if (expiresAt_ < 0 || GameTime() >= expiresAt_) {
                count_ = 0;
                expiresAt_ = GameTime() + g_initialTimeMs;
            }
            ++count_;  // add_kill_to_tally
            if (count_ >= g_initialKills) StartSpree();
        } else if (disposition >= kFriendly) {
            if (IsActive()) {
                rfa::Log("Killing spree: a friend killed; it ends with %d kills", count_);
                End();
            }
        }
    }

    void LevelInit() override {
        Activity::LevelInit();
        ResetSpree();
    }

protected:
    void FrameActive(float) override {
        if (GameTime() >= expiresAt_) End();
    }

    // end 0x7ADA20: morale, one point a kill, then end_common.
    void End() override {
        if (!IsActive()) return;
        rfa::Log("Killing spree: over with %d kills: morale %+d", count_, count_);
        if (count_ > 0) liberation::ChangeMorale(static_cast<float>(count_), -1);
        Activity::End();
        ResetSpree();
    }

    // kill 0x798990: no morale.
    void Kill() override {
        if (IsActive()) rfa::Log("Killing spree: stopped with %d kills (nothing given)", count_);
        Activity::End();
        ResetSpree();
    }

private:
    void StartSpree() {
        if (!StartFree()) return;
        expiresAt_ = GameTime() + g_startPeriodMs;
        messages::Tip("Killing_Spree", kTipMs);
        rfa::Log("Killing spree: started with %d kills", count_);
    }

    void ResetSpree() {
        count_ = 0;
        expiresAt_ = -1;
    }

    int TimeLeft() const {
        if (expiresAt_ < 0) return 0;
        const int left = expiresAt_ - GameTime();
        return left > 0 ? left : 0;
    }

    int count_ = 0;
    int expiresAt_ = -1;  // the game clock's ms (-1: none)
};

}  // namespace

Activity* MakeKillingSpree() {
    static KillingSpreeActivity spree;
    return &spree;
}

}  // namespace campaign::activities
