// Guerrilla's liberation of Mars (liberation.cpp): the districts, EDF control and civilian morale,
// the alert level, the EDF's tech and progression levels, and the player's salvage.
#pragma once
#include "campaign.h"
#include <string>

namespace campaign::liberation {

void Init();
void Enter();
void Update(float seconds);

// ---- The alert level (handles::AlertLevel) and its value (Guerrilla's "notoriety", 0..3100).
int AlertLevel();
float AlertValue();
void SetAlertLevel(int level);     // the level's lowest value (gsm_set_alert_level)
void SetAlertValue(float value);   // clamped to the caps, the level follows (gsm_set_alert_value)
void SetAlertCap(int lowest, int highest);
// What Guerrilla's HUD shows of it: the level × 25 plus how far along its band the value is × 25
// (0-25 green, to 50 yellow, to 75 orange, above red).
float AlertMeter();
// The half second after the alert falls to green, in which nothing is counted.
bool AlertGrace();

// What the alert hears of: damage to, or the end of, one of the EDF's people, vehicles or buildings
// (Guerrilla's gfm_damage_report, with its object types). Damage runs from 0 to 1.
enum ReportKind { ReportHuman = 0, ReportStructure = 2, ReportVehicle = 3 };
struct DamageReport {
    unsigned handle = 0xFFFFFFFF;
    int kind = ReportHuman;
    int importance = 1;                   // 0 none, 1 low, 2 medium, 3 high
    float damage = 0.0f, reported = 0.0f; // now, and when last counted
    Vector at{};                          // where it is
};
// Guerrilla's alm_report_damage and alm_report_death: what the report adds to the alert at its next
// one-second step, if anything; `what` names it in the log ([GuerrillaCampaign] Log=1).
void ReportDamage(DamageReport& report, const char* what);
void ReportDeath(DamageReport& report, const char* what);

// ---- Districts: an index into Guerrilla's district list, -1 for none.
struct District {
    std::string name;
    int index = 0;  // Guerrilla's district_index
    float control = 0.0f, controlMax = 0.0f;
    float morale = 0.0f, moraleMax = 100.0f;
    bool liberated = false;
    int liberatedTech = 0, liberatedMarauder = 0, liberatedEdf = 0;  // set on liberation (0 = keep)
    int minEdfProgression = 0;
    bool fixedMorale = false, fixedControl = false;  // disable_morale_change, disable_control_change
    float controlStart = 0.0f, moraleStart = 0.0f;   // territories.xtbl's, for a fresh campaign
    // The last changes, for the HUD's meters (hud_ui_change_control / _morale): what changed, by how
    // much, when (the game's clock), and a count that goes up with each change.
    unsigned changes = 0;
    float controlBefore = 0.0f, moraleBefore = 0.0f;
    int controlChangedAt = -1, moraleChangedAt = -1;
};
int DistrictCount();
const District* GetDistrict(int district);
int DistrictAt(const Vector& position);
int DistrictNamed(const char* name);
int PlayerDistrict();

// EDF control and morale of a district (-1: the player's), changed as Guerrilla's
// gfm_change_edf_control and gfm_change_district_morale do.
// With a HUD message: Guerrilla's string tag and its picture (hud_message_add_minor).
void ChangeEdfControl(float delta, int district, const char* tag = nullptr, const char* icon = nullptr);
void ChangeMorale(float delta, int district, const char* tag = nullptr, const char* icon = nullptr);
void SetMorale(float morale, int district);
// difficulty.xtbl's MENU_MORALE_PLAYER_DEATH at the game's difficulty (-2 .. -5).
float PlayerDeathMorale();
// The districts, progression and salvage read again from the campaign's save (after a game save was
// loaded), from territories.xtbl's starting values up.
void ReloadState();
// Set outright (gfm_set_edf_control: clamped to 0..max, then the district may be liberated).
void SetEdfControl(float control, int district);
// A district is liberated at EDF control 0 with none of its story missions left (Guerrilla's rule; the user,
// 2026-11-18). A mission recorded done (the mission work will call it) may liberate its district.
void RecordMissionDone(const char* mission);
// Testing only, before missions exist: every story mission of the district (-1: the player's) marked done, then
// liberation checked. The number marked.
int MarkMissionsDone(int district);
// The district's story missions not done yet (-1: the player's).
int MissionsLeftIn(int district);
// What morale's effects read: the district's, or the temporary morale while a mission uses it,
// plus the offset (gfm_get_effective_morale), and that as a fraction of the most.
float EffectiveMoralePercent(int district);
void UseTemporaryMorale(bool use);
void ChangeTemporaryMorale(float delta, bool set);
void ChangeMoraleOffset(float delta, bool set);

// ---- The EDF's tech level and the progression levels (set by liberation and by scripts).
int TechLevel();
void SetTechLevel(int level);
void SetEdfProgression(int level);
void SetMarauderProgression(int level);
int MarauderProgressionLevel();
// edf_progression_level_internal (RM 0x78C140): the player's district's min_progression_edf (0-7) or the global
// level, whichever is higher; the global one where the player is in no district.
int EdfProgressionLevel();

// ---- Salvage, Guerrilla's money.
int Salvage();
void AddSalvage(int amount);

}  // namespace campaign::liberation
