// Guerrilla's activities ("guerrilla actions"): the framework (activities.cpp) and its kinds
// (demolitions_master.cpp first).
#pragma once
#include "campaign.h"
#include <string>
#include <vector>

namespace campaign::activities {

// Guerrilla's activity_modes.
enum Mode : int {
    ModeNone = 0,
    VehicleRail,  // riding shotgun
    MissionVrail,
    TanksALot,
    Delivery,  // Guerrilla Express
    KillingSpree,
    Courier,
    Mayhem,
    RaidDefend,
    RaidAttack,
    HouseArrest,
    DemolitionsMaster,
    Assassin,
    Convoy,
    ContinuousAttack,
    CarBomb,
    AreaDefense,
    kModes,
};

// Guerrilla's base_activity_states (the same values as handles::ActivityState).
enum class State { Wait, Loading, Ready, Starting, Running, Shutdown };
// activity_interface_states: what the HUD shows of it.
enum class Interface { None, Ready, Running, Victory, Loss, Killed };
// activity_disable_reason (Everything clears them all).
enum DisableReason { ByActivity, ByMission, ByScript, ByConsole, BySatellite, ByOther, Everything };
// gamelog_complete_reasons: why a run ended.
enum EndReason { Failed, Success, PlayerDied, Cancelled, OutOfTime, VehicleAbandoned, VehicleDestroyed, Escaped, OutOfRange, OutOfAmmo, HostagesKilled, BuildingDestroyed, GoalNotReached };

// activity_standard_rewards.
struct Rewards {
    float control = 0.0f;  // EDF_Control_Reduction
    float morale = 0.0f;   // Morale_Increase
    int salvage = 0;       // Salvage_Reward
};

// One row of activity.xtbl (base_activity_data): what a run of an activity loads and holds.
struct BaseData {
    std::string name;
    float controlLimit = 1.0f;  // offered only while the district's EDF control is at most this
    std::vector<std::string> scriptGroups;
    std::vector<std::string> layers;
    int minAlertCap = 0, maxAlertCap = 3;  // green, red
    std::string voice;          // voice_info/voice_type
    std::string music;          // music_info/music_intensity
    std::string specialScore;   // music_info/special_score
    bool disabled = false;
    bool allowGuerrillaReinforcement = false;
};
const BaseData* FindBaseData(const std::string& name);

void Init();
void Enter();
void Update(float seconds);

// For the script words.
bool AnyRunning();                               // activity_is_active: a modal activity is current or starting
bool AlertLocked();                              // an activity holds the alert still
void Disable(DisableReason reason, bool free);   // suspend the modal (free: the free) activities
void DisableOffers(DisableReason reason);        // every modal one but Guerrilla Express and demolitions master
void Reinstate(DisableReason reason, bool free);

// What the HUD reads (Guerrilla's MissionInfo, MissionStatus, HudTimerInfo and DemolitionsInfo).
struct HudInfo {
    bool running = false;
    Mode mode = ModeNone;
    std::string title;         // e.g. "DEMOLITIONS MASTER"
    std::string objective;     // e.g. "DESTROY THE BUILDING"
    std::string timerText;     // "mm:ss"
    bool timerVisible = false;
    int status = 0;            // the kind's own (demolitions: 1 out of ammo while collapsing, 2 destroyed)
};
HudInfo Hud();

// What a run shows the player (guerrilla_hud's briefing, corner panel and results; Guerrilla's
// DemolitionsInfo and the mission panel's activity info). Texts are Guerrilla's string tags.
struct Screen {
    Mode mode = ModeNone;
    int screen = 0;  // 0 none, 1 the briefing (accept or cancel), 2 running (the corner panel), 3 the results
    std::string title, subtitle, body, objective, timer, result, failure;
    std::vector<std::string> weapons;  // the briefing's: weapons.xtbl names, or a layer prop's icon
    std::vector<int> ammo;             // their rounds (-1: none shown)
    int timeLimit = 0, parTime = 0;    // seconds (limit 0: none, the timer counts up)
    int bestMs = 0, elapsedMs = 0;
    int status = 0;                    // running: 0, 1 unstable (out of ammo while collapsing), 2 destroyed
    bool success = false, proTime = false, outOfRange = false;
    int salvage = 0;
    Vector center{};                   // the play area, for the minimap's ring
    float range = 0.0f;
    // The target, as Guerrilla's minimap colours it (cartographer::get_structure_color: the mover flag 0x20000000 set
    // by flag_demo_master_buildings_for_minimap_coloration): its box's middle and half its width.
    bool hasTarget = false, targetDown = false;
    Vector target{};
    float targetRadius = 0.0f;
};

}  // namespace campaign::activities
