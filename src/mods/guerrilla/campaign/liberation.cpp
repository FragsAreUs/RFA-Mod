// Guerrilla's liberation of Mars, as its code runs it.
//
// Districts (territories.xtbl, territory terr01): each has EDF control points (Parker 160, Dust
// 380, Badlands 250, Oasis 300, Manufacturing 300, Eos 410) and civilian morale (0..100), and the
// cells of the 16 × 16 zone grid it covers (grid indexes CCRR: column × 100 + row).
//
// EDF control (gfm_change_edf_control): changes are rounded to whole points and clamped to
// 0..max; it falls when the player destroys EDF buildings (each once, by the building's importance:
// low 1-3, medium 10/12/15, high 80 or its own value), wins activities, and when scripts lower it
// (lower_edf_control n lowers 2n). A district is liberated when its control is 0 and none of its
// missions is left: the EDF's progression, the marauders' and the tech level then rise to the
// district's own, if it has them. Scripts hear of it through edf_control_lowered (old % > n ≥ new %).
//
// Morale (gfm_change_district_morale): changes are whole points, at least 1 (difficulty scales
// gains), clamped to 0..max; scripts hear civilian_morale_raised / _lowered (old % < n ≤ new %, or
// the reverse). Missions may use a temporary morale instead, and an offset on top
// (gfm_get_effective_morale), which is what morale's effects read.
//
// Alert (gsm_do_frame, once a second): a value from 0 to 3100, and a level that rises at 250
// (yellow), 1000 (orange), 3000 (red) and falls back only at 0, 300, 1100 (alert_level.xtbl: each
// level's band, the next level's minimum and the previous level's maximum). Each step adds:
// - what was reported since the last (alm_report_damage, alm_report_death; alert_value.xtbl): the
//   importance's multiplier × the damage (0 to 1) × the kind's damage multiplier (people and vehicles
//   80, buildings 300) × the difficulty (0.75, 1, 1.1, 1.2), less what the same thing was counted for
//   before, × the alert multiplier of the player's weapon or vehicle; a death adds the importance's
//   multiplier × the kind's death value × the difficulty on top. Only within 200 m of the player; in
//   green only buildings' damage (from 5 % at a time) and buildings' and vehicles' ends count;
// - in green, 75 for each of the EDF's people who sees the player and may engage (alert_sources.cpp);
// and takes away:
// - the level's decay (40, 40, 45, 50 a second), ramping to 5 × over 25 s, once 20 s have passed
//   with nothing reported and none of the EDF seeing the player;
// - above green, 10 when the player is 40 m from where the last report found him, 50 beyond 120 m.
// The alert never falls while the player is in a tank or a walker, or in one of the EDF's restricted
// areas (restricted_areas.cpp). Falling to green forgets where
// the last report was, and nothing counts for half a second. Missions and activities cap it between
// two levels, and an activity may hold it still. Not saved: a level starts green.
//
// Not ported yet: buildings' damage reports, the EDF's answer to each level (orders, reinforcements),
// the sounds and tooltip of a level change; what
// lowers control and morale by itself (EDF buildings destroyed, civilians and guerrillas killed,
// propaganda destroyed, the player's death); the control and morale meters and messages; the
// liberation screen, radio lines and unlocks. Guerrilla's difficulty for the alert, control and morale
// comes from its difficulty.xtbl at the game's level (difficulty::Level(): Armageddon's Custom reads as
// Medium); its combat items are difficulty.cpp's.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "liberation.h"
#include "difficulty.h"
#include "activities.h"
#include "alert_sources.h"
#include "restricted_areas.h"
#include "handle_kinds.h"
#include "messages.h"
#include "save.h"
#include "script_words.h"
#include "tables.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace campaign::liberation {
namespace {

// ---- Districts

struct DistrictState : District {
    std::vector<int> cells;  // CCRR
};
std::vector<DistrictState> g_districts;
// The world's zones, as the world converter places them: zone (column, row)'s corner at
// x = (column - 8) × 511, z = (7 - row) × 511.
constexpr float kZoneWidth = 511.0f;

struct Mission {
    std::string name;
    std::string district;
};
std::vector<Mission> g_missions;

void LoadDistricts() {
    std::vector<tables::Element> territories;
    if (!tables::Load("territories", territories)) return;
    for (const tables::Element& territory : territories) {
        if (_stricmp(territory.Text("Name").c_str(), "terr01") != 0) continue;
        for (const tables::Element* list : territory.Children("Districts"))
            for (const tables::Element& entry : list->children) {
                DistrictState district;
                district.name = entry.Text("Name");
                district.index = entry.Int("district_index");
                district.controlMax = entry.Number("Control_max");
                district.control = entry.Number("Control", district.controlMax);
                district.moraleMax = entry.Number("Morale_max", 100.0f);
                district.morale = entry.Number("Morale");
                district.controlStart = district.control;
                district.moraleStart = district.morale;
                district.liberatedTech = entry.Int("liberated_tech");
                district.liberatedMarauder = entry.Int("liberated_marauder");
                district.liberatedEdf = entry.Int("liberated_edf");
                district.minEdfProgression = entry.Int("min_progression_edf");
                if (const tables::Element* flags = entry.Child("District_Flags"))
                    for (const tables::Element& flag : flags->children) {
                        district.fixedMorale |= _stricmp(flag.text.c_str(), "disable_morale_change") == 0;
                        district.fixedControl |= _stricmp(flag.text.c_str(), "disable_control_change") == 0;
                    }
                for (const tables::Element* grid : entry.Children("Grid_index"))
                    for (const tables::Element& cell : grid->children) district.cells.push_back(cell.Int("Index", -1));
                g_districts.push_back(district);
            }
    }
    rfa::Log("territories.xtbl: %u districts", static_cast<unsigned>(g_districts.size()));
    std::vector<tables::Element> missions;
    if (tables::Load("missions", missions))
        for (const tables::Element& mission : missions) g_missions.push_back({mission.Text("Name"), mission.Text("district")});
}

int Resolve(int district) { return district < 0 ? PlayerDistrict() : district; }

float Percent(float value, float most) { return most > 0.0f ? value / most : 0.0f; }

bool MissionDone(const std::string& name) { return save::Int("missions", name.c_str(), 0) != 0; }

int MissionsLeft(const DistrictState& district) {
    int left = 0;
    for (const Mission& mission : g_missions)
        if (_stricmp(mission.district.c_str(), district.name.c_str()) == 0 && !MissionDone(mission.name)) ++left;
    return left;
}

// ---- Progression

int g_techLevel = 0, g_edfProgression = 0, g_marauderProgression = 0;
int g_salvage = 0;

void SaveDistrict(const DistrictState& district) {
    const std::string section = "district " + district.name;
    save::SetNumber(section.c_str(), "control", district.control);
    save::SetNumber(section.c_str(), "morale", district.morale);
    save::SetInt(section.c_str(), "liberated", district.liberated);
}

void LoadSaved() {
    for (DistrictState& district : g_districts) {
        const std::string section = "district " + district.name;
        district.control = save::Number(section.c_str(), "control", district.control);
        district.morale = save::Number(section.c_str(), "morale", district.morale);
        district.liberated = save::Int(section.c_str(), "liberated", district.liberated) != 0;
    }
    g_techLevel = save::Int("progression", "tech_level", 0);
    g_edfProgression = save::Int("progression", "edf", 0);
    g_marauderProgression = save::Int("progression", "marauder", 0);
    g_salvage = save::Int("progression", "salvage", 0);
}

// district::liberate_district: control 0 and none of its missions left (the user, 2026-11-18: liberation
// comes with the district's last campaign mission, as Guerrilla's). Guerrilla sets the levels to the
// district's; in free roam a district can be liberated out of story order, so a level only goes up here
// (the user's choice, 2026-11-18).
void MaybeLiberate(DistrictState& district) {
    if (district.liberated || district.control > 0.0f || MissionsLeft(district) > 0) return;
    district.liberated = true;
    if (district.liberatedEdf > g_edfProgression) SetEdfProgression(district.liberatedEdf);
    if (district.liberatedMarauder > g_marauderProgression) SetMarauderProgression(district.liberatedMarauder);
    if (district.liberatedTech > g_techLevel) SetTechLevel(district.liberatedTech);
    SaveDistrict(district);
    rfa::Log("%s is liberated", district.name.c_str());
}

// ---- Morale

bool g_useTemporaryMorale = false;
float g_temporaryMorale = 0.0f;
constexpr float kTemporaryMoraleMax = 1000.0f;
float g_moraleOffset = 0.0f;
// difficulty.xtbl at Easy, Medium, Hard, Insane: MENU_MORALE_BOOSTS and MENU_MORALE_PENALITIES (gains and
// losses × these), MENU_MORALE_PLAYER_DEATH (the player's death), and the control changes' multiplier,
// which Guerrilla's table keeps as MENU_CONTROL_PARKER (difficulty item 22; Insane 0.8).
float g_moraleBoosts[4] = {1.0f, 1.0f, 0.8f, 0.8f};
float g_moralePenalties[4] = {1.0f, 1.0f, 1.0f, 1.0f};
float g_playerDeathMorale[4] = {-2.0f, -3.0f, -4.0f, -5.0f};
float g_controlMultiplier[4] = {1.0f, 1.0f, 1.0f, 0.8f};

// ---- The HUD's tool tips for control and morale (gfm_change_edf_control, gfm_change_district_morale)

constexpr int kControlTipMs = 1500;   // "EDF_Control", while control is under 200
constexpr float kControlTipBelow = 200.0f;
constexpr int kMoraleTipMs = 500;     // "Morale", when it rises

// ---- Alert

// alert_level.xtbl: each level's band (SolidXMin..SolidXMax) and decay rate per second.
struct AlertBand {
    float min, max, decay;
};
AlertBand g_bands[handles::kAlertLevels] = {{0, 0, 40}, {250, 300, 40}, {1000, 1100, 45}, {3000, 3100, 50}};
float g_decayDelay = 20.0f;       // decay_start_period: seconds without contact before it falls
float g_decayRamp = 25.0f;        // decay_ramp_period
float g_decayMultiplier = 5.0f;   // MaxDecayMultiplier
float g_contactGrowth = 75.0f;    // ContactGrowRate: a second, in green, for each of the EDF's people seeing the player
float g_buildingThreshold = 0.05f;                        // structure_damage_threshold: a building's new damage counts in green from this much
float g_awayNear = 40.0f, g_awayFar = 120.0f;             // LHA_Min_Decay_Distance, LHA_Max_Decay_Distance: from the last report
float g_awayDecayNear = 10.0f, g_awayDecayFar = 50.0f;    // LHA_Minimum_Decay, LHA_Maximum_Decay: a step

// alert_value.xtbl: what damage and deaths are worth, for people, buildings and vehicles: a multiplier
// by importance (none, low, medium, high), the damage multiplier and the death value.
struct AlertValues {
    float importance[4];
    float damage, death;
};
AlertValues g_humanValues = {{0.0f, 1.0f, 1.5f, 2.0f}, 80.0f, 1.0f};
AlertValues g_structureValues = {{0.0f, 1.0f, 4.0f, 15.0f}, 300.0f, 0.5f};
AlertValues g_vehicleValues = {{0.0f, 2.0f, 4.0f, 6.0f}, 80.0f, 2.0f};
// difficulty.xtbl, MENU_ENEMY_ALERT_LEVEL_EVENTS: Easy, Medium, Hard, Insane, picked by the game's
// own difficulty (0x10F7E98: 0 easy .. 3 insane).
float g_alertDifficulty[4] = {0.75f, 1.0f, 1.1f, 1.2f};

float g_alertValue = 0.0f;
int g_alertLevel = handles::AlertGreen;
float g_minAlert = 0.0f, g_maxAlert = 3100.0f;
float g_nextDelta = 0.0f;         // Next_alert_delta: what the reports add at the next step
int g_lastStep = -1;              // when the last step ran, on the game's clock (ms)
int g_decayFrom = 0;              // Last_los_time: when decay may start
bool g_restartDecayWait = false;  // Reset_decay_timer: the level just rose
int g_graceUntil = -1;            // Alert_level_grace_timer
bool g_lastReportKnown = false;   // Last_hostile_action_pos: where the player was at the last report
Vector g_lastReportAt{};
bool g_log = false;               // [GuerrillaCampaign] Log=1

constexpr float kReportRange = 200.0f;  // reports count within this of the player, over the ground
constexpr int kGraceMs = 500;

void LoadAlertTable() {
    std::vector<tables::Element> rows;
    if (!tables::Load("alert_level", rows) || rows.empty()) return;
    const tables::Element& row = rows[0];
    g_bands[handles::AlertGreen].decay = row.Number("DecayRateGreen", 40.0f);
    g_bands[handles::AlertYellow] = {row.Number("SolidYellowMin", 250.0f), row.Number("SolidYellowMax", 300.0f), row.Number("DecayRateYellow", 40.0f)};
    g_bands[handles::AlertOrange] = {row.Number("SolidOrangeMin", 1000.0f), row.Number("SolidOrangeMax", 1100.0f), row.Number("DecayRateOrange", 45.0f)};
    g_bands[handles::AlertRed] = {row.Number("SolidRedMin", 3000.0f), row.Number("SolidRedMax", 3100.0f), row.Number("DecayRateRed", 50.0f)};
    g_decayDelay = row.Number("decay_start_period", 20.0f);
    g_decayRamp = row.Number("decay_ramp_period", 25.0f);
    g_decayMultiplier = row.Number("MaxDecayMultiplier", 5.0f);
    g_contactGrowth = row.Number("ContactGrowRate", 75.0f);
    g_buildingThreshold = row.Number("structure_damage_threshold", 0.05f);
    g_buildingThreshold = g_buildingThreshold < 0.0f ? 0.0f : g_buildingThreshold > 1.0f ? 1.0f : g_buildingThreshold;
    g_awayNear = row.Number("LHA_Min_Decay_Distance", 40.0f);
    g_awayFar = row.Number("LHA_Max_Decay_Distance", 120.0f);
    g_awayDecayNear = row.Number("LHA_Minimum_Decay", 10.0f);
    g_awayDecayFar = row.Number("LHA_Maximum_Decay", 50.0f);
    g_maxAlert = g_bands[handles::AlertRed].max;
}

void LoadAlertValues() {
    std::vector<tables::Element> rows;
    if (!tables::Load("alert_value", rows)) return;
    for (const tables::Element& row : rows) {
        const std::string name = row.Text("Name");
        AlertValues* values = _stricmp(name.c_str(), "Human") == 0 ? &g_humanValues
                              : _stricmp(name.c_str(), "Structure") == 0 ? &g_structureValues
                              : _stricmp(name.c_str(), "Vehicle") == 0 ? &g_vehicleValues : nullptr;
        if (!values) continue;
        values->importance[1] = row.Number("importance_mul_low", values->importance[1]);
        values->importance[2] = row.Number("importance_mul_medium", values->importance[2]);
        values->importance[3] = row.Number("importance_mul_high", values->importance[3]);
        values->damage = row.Number("damage_multiplier", values->damage);
        values->death = row.Number("death_value", values->death);
    }
}

// The difficulty item of that name, wherever the table keeps it (its items are lists inside its entry).
const tables::Element* DifficultyItem(const tables::Element& in, const char* name) {
    if (_stricmp(in.Text("Name").c_str(), name) == 0 && in.Child("Easy")) return &in;
    for (const tables::Element& child : in.children)
        if (const tables::Element* found = DifficultyItem(child, name)) return found;
    return nullptr;
}

void LoadDifficultyItem(const std::vector<tables::Element>& rows, const char* name, float values[4]) {
    for (const tables::Element& row : rows)
        if (const tables::Element* item = DifficultyItem(row, name)) {
            values[0] = item->Number("Easy", values[0]);
            values[1] = item->Number("Medium", values[1]);
            values[2] = item->Number("Hard", values[2]);
            values[3] = item->Number("Insane", values[3]);
            return;
        }
}

void LoadAlertDifficulty() {
    std::vector<tables::Element> rows;
    if (!tables::Load("difficulty", rows)) return;
    LoadDifficultyItem(rows, "MENU_ENEMY_ALERT_LEVEL_EVENTS", g_alertDifficulty);
    LoadDifficultyItem(rows, "MENU_MORALE_BOOSTS", g_moraleBoosts);
    LoadDifficultyItem(rows, "MENU_MORALE_PENALITIES", g_moralePenalties);
    LoadDifficultyItem(rows, "MENU_MORALE_PLAYER_DEATH", g_playerDeathMorale);
    LoadDifficultyItem(rows, "MENU_CONTROL_PARKER", g_controlMultiplier);
}

int DifficultyIndex() { return difficulty::Level(); }

float AlertDifficulty() { return g_alertDifficulty[difficulty::Level()]; }

const char* LevelName(int level) {
    static const char* const names[handles::kAlertLevels] = {"green", "yellow", "orange", "red"};
    return level >= 0 && level < handles::kAlertLevels ? names[level] : "?";
}

// get_alert_level and change_alert_level: from the current level, down while the value is at or below
// the previous level's maximum (green's: 0), up while it is at or above the next level's minimum.
void EvaluateAlertLevel() {
    const int before = g_alertLevel;
    int level = g_alertLevel;
    while (level > handles::AlertGreen && g_alertValue <= (level == handles::AlertYellow ? 0.0f : g_bands[level - 1].max)) --level;
    while (level < handles::AlertRed && g_alertValue >= g_bands[level + 1].min) ++level;
    if (level == before) return;
    g_alertLevel = level;
    if (level == handles::AlertGreen) {
        // reset_from_alert_state: the last report's place is forgotten, and nothing counts for a moment.
        g_lastReportKnown = false;
        g_graceUntil = GameTime() + kGraceMs;
    }
    if (level > before) {
        g_restartDecayWait = true;
        words::PostAlertLevelRaised(level);
    }
    rfa::Log("Alert level %s (value %.0f); the player %s", LevelName(level), g_alertValue, restricted_areas::PlayerPlace().c_str());
}

// get_decay_delta: nothing until the wait since the last contact is over; then the level's rate,
// times 1 to the multiplier as the ramp period goes by.
float DecayOver(int now, int ms) {
    if (now < g_decayFrom) return 0.0f;
    float ramp = (now - g_decayFrom) / 1000.0f / g_decayRamp * g_decayMultiplier;
    ramp = ramp < 1.0f ? 1.0f : ramp > g_decayMultiplier ? g_decayMultiplier : ramp;
    return g_bands[g_alertLevel].decay * (ms / 1000.0f) * ramp;
}

// get_dist_delta: above green, what leaving the place of the last report takes off each step.
float AwayDecay() {
    if (g_alertLevel <= handles::AlertGreen || !g_lastReportKnown) return 0.0f;
    const Vector at = PositionOf(Player());
    const float dx = at.x - g_lastReportAt.x, dy = at.y - g_lastReportAt.y, dz = at.z - g_lastReportAt.z;
    const float squared = dx * dx + dy * dy + dz * dz;
    if (squared < g_awayNear * g_awayNear) return 0.0f;
    return squared < g_awayFar * g_awayFar ? g_awayDecayNear : g_awayDecayFar;
}

// gsm_do_frame, once a second of the game's clock.
void AlertStep() {
    const int now = GameTime();
    if (g_graceUntil >= 0 && now >= g_graceUntil) g_graceUntil = -1;
    if (g_lastStep < 0 || now < g_lastStep) g_lastStep = now;
    const int ms = now - g_lastStep;
    if (ms < 1000) return;
    g_lastStep = now;

    const int seeing = alert_sources::EdfSeeingPlayer();
    if (g_nextDelta > 0.0f || seeing > 0 || g_restartDecayWait) {
        g_decayFrom = now + static_cast<int>(g_decayDelay * 1000.0f);
        g_restartDecayWait = false;
    }
    const float decay = DecayOver(now, ms);
    const float growth = g_alertLevel <= handles::AlertGreen ? seeing * g_contactGrowth * (ms / 1000.0f) : 0.0f;
    const float away = AwayDecay();
    if (!activities::AlertLocked()) {
        float change = g_nextDelta + growth - decay - away;
        if (change < 0.0f && (alert_sources::PlayerInTankOrWalker() || restricted_areas::PlayerInside())) change = 0.0f;
        if (g_log && (change != 0.0f || seeing > 0))
            rfa::LogDetail("Alert %.0f %+.0f (%s; reports %+.0f, %d of the EDF seeing the player %+.0f, decay -%.0f, away -%.0f)", g_alertValue, change,
                     LevelName(g_alertLevel), g_nextDelta, seeing, growth, decay, away);
        if (change != 0.0f) SetAlertValue(g_alertValue + change);
    }
    g_nextDelta = 0.0f;
    EvaluateAlertLevel();
}

const AlertValues* ValuesFor(int kind) {
    return kind == ReportHuman ? &g_humanValues : kind == ReportStructure ? &g_structureValues : kind == ReportVehicle ? &g_vehicleValues : nullptr;
}

// get_total_alert_value: what that much damage to something of that importance is worth.
float WorthOf(const AlertValues& values, int importance, float damage) {
    if (importance < 1 || importance > 3) return 0.0f;
    return values.importance[importance] * damage * values.damage * AlertDifficulty();
}

bool NearPlayer(const Vector& at) {
    void* player = Player();
    if (!player) return false;
    const Vector here = PositionOf(player);
    const float dx = at.x - here.x, dz = at.z - here.z;
    return dx * dx + dz * dz < kReportRange * kReportRange;
}

// A report counted: its worth goes into the next step, the player's place is the last report's, and
// the thing's damage so far is what it has been counted for.
void Count(DamageReport& report, float worth, const char* what) {
    g_nextDelta += worth;
    g_lastReportAt = PositionOf(Player());
    g_lastReportKnown = true;
    report.reported = report.damage;
    // Always written, but for a vehicle's damage, which comes many times a second (Log=1 for that).
    if (g_log || report.kind != ReportVehicle || report.damage >= 1.0f) rfa::LogDetail("Alert: %s, +%.0f (%.0f now)", what, worth, g_alertValue);
}

}  // namespace

// ---- Alert

int AlertLevel() { return g_alertLevel; }
float AlertValue() { return g_alertValue; }
bool AlertGrace() { return g_graceUntil >= 0; }

// gsm_find_alert_value_percentage: full inside the level's band; below it, how far the value has come
// from where the level would drop; above it, how far toward the next level.
float AlertMeter() {
    const AlertBand& band = g_bands[g_alertLevel];
    const float lower = g_alertLevel == handles::AlertGreen ? 0.0f : g_bands[g_alertLevel - 1].max;
    const float upper = g_alertLevel == handles::AlertRed ? band.max : g_bands[g_alertLevel + 1].min;
    const float value = g_alertValue;
    float part = 0.0f;
    if (value <= 0.0f) part = 0.0f;
    else if (value >= band.min && value <= band.max) part = 1.0f;
    else if (value < band.min) part = value <= lower ? 0.0f : (value - lower) / (band.min - lower);
    else part = value >= upper ? 1.0f : (value - band.max) / (upper - band.max);
    part = part < 0.0f ? 0.0f : part > 1.0f ? 1.0f : part;
    return g_alertLevel * 25.0f + part * 25.0f;
}

void ReportDamage(DamageReport& report, const char* what) {
    if (AlertGrace() || report.damage >= 1.0f) return;  // the end of a thing is ReportDeath's
    const AlertValues* values = ValuesFor(report.kind);
    if (!values) return;
    if (!NearPlayer(report.at)) {
        report.reported = report.damage;
        return;
    }
    // In green only a building's damage counts, and only from the threshold up (less waits, adding up).
    if (g_alertLevel <= handles::AlertGreen && (report.kind != ReportStructure || report.damage - report.reported < g_buildingThreshold)) return;
    const float worth = (WorthOf(*values, report.importance, report.damage) - WorthOf(*values, report.importance, report.reported)) *
                        alert_sources::PlayerMultiplier();
    if (worth > 0.0f) Count(report, worth, what);
}

void ReportDeath(DamageReport& report, const char* what) {
    if (AlertGrace()) return;
    if (!NearPlayer(report.at)) {
        report.reported = report.damage;
        return;
    }
    const AlertValues* values = ValuesFor(report.kind);
    if (!values || report.importance < 1 || report.importance > 3) return;
    if (g_alertLevel <= handles::AlertGreen && report.kind != ReportStructure && report.kind != ReportVehicle) return;
    const float worth = (WorthOf(*values, report.importance, report.damage) - WorthOf(*values, report.importance, report.reported)) *
                        alert_sources::PlayerMultiplier();
    if (worth <= 0.0f) return;
    Count(report, worth + values->importance[report.importance] * values->death * AlertDifficulty(), what);
}

void SetAlertValue(float value) {
    g_alertValue = value < g_minAlert ? g_minAlert : value > g_maxAlert ? g_maxAlert : value;
    EvaluateAlertLevel();
}

void SetAlertLevel(int level) {
    if (level < handles::AlertGreen || level > handles::AlertRed) return;
    SetAlertValue(g_bands[level].min);
}

// gsm_set_alert_level_cap: from the lowest level's minimum to the highest level's maximum.
void SetAlertCap(int lowest, int highest) {
    if (lowest < handles::AlertGreen) lowest = handles::AlertGreen;
    if (highest > handles::AlertRed) highest = handles::AlertRed;
    if (lowest > highest) lowest = highest;
    g_minAlert = g_bands[lowest].min;
    g_maxAlert = g_bands[highest].max;
    SetAlertValue(g_alertValue);
}

// ---- Districts

int DistrictCount() { return static_cast<int>(g_districts.size()); }
const District* GetDistrict(int district) {
    return district >= 0 && district < DistrictCount() ? &g_districts[district] : nullptr;
}

int DistrictAt(const Vector& position) {
    const int column = static_cast<int>(floorf(position.x / kZoneWidth)) + 8;
    const int row = 7 - static_cast<int>(floorf(position.z / kZoneWidth));
    const int cell = column * 100 + row;
    for (size_t i = 0; i < g_districts.size(); ++i)
        for (int c : g_districts[i].cells)
            if (c == cell) return static_cast<int>(i);
    return -1;
}

int DistrictNamed(const char* name) {
    for (size_t i = 0; i < g_districts.size(); ++i)
        if (_stricmp(g_districts[i].name.c_str(), name) == 0) return static_cast<int>(i);
    return -1;
}

int PlayerDistrict() {
    void* player = Player();
    return player ? DistrictAt(PositionOf(player)) : -1;
}

void ChangeEdfControl(float delta, int which, const char* tag, const char* icon) {
    const int index = Resolve(which);
    if (index < 0 || index >= DistrictCount() || delta == 0.0f) return;
    DistrictState& district = g_districts[index];
    if (district.fixedControl) return;
    delta = roundf(delta * g_controlMultiplier[DifficultyIndex()]);
    float control = district.control + delta;
    control = control < 0.0f ? 0.0f : control > district.controlMax ? district.controlMax : control;
    if (control == district.control) return;
    if (tag) messages::Minor(tag, icon);
    const float before = Percent(district.control, district.controlMax);
    district.controlBefore = district.control;
    district.controlChangedAt = GameTime();
    ++district.changes;
    district.control = control;
    SaveDistrict(district);
    words::PostEdfControlLowered(before, Percent(control, district.controlMax));
    if (control < kControlTipBelow) messages::Tip("EDF_Control", kControlTipMs);
    rfa::Log("%s: EDF control %.0f -> %.0f / %.0f%s%s", district.name.c_str(), district.controlBefore, control, district.controlMax, tag ? ", " : "",
             tag ? tag : "");
    MaybeLiberate(district);
}

void ChangeMorale(float delta, int which, const char* tag, const char* icon) {
    const int index = Resolve(which);
    if (index < 0 || index >= DistrictCount()) return;
    if (tag) messages::Minor(tag, icon);  // shown even when nothing changes
    if (delta == 0.0f) return;
    DistrictState& district = g_districts[index];
    if (district.fixedMorale) return;
    // Whole points, at least one: gains x MENU_MORALE_BOOSTS, losses x MENU_MORALE_PENALITIES.
    const int difficulty = DifficultyIndex();
    float step = truncf(delta * (delta < 0.0f ? g_moralePenalties[difficulty] : g_moraleBoosts[difficulty]));
    if (fabsf(step) < 1.0f) step = delta < 0.0f ? -1.0f : 1.0f;
    float morale = district.morale + step;
    morale = morale < 0.0f ? 0.0f : morale > district.moraleMax ? district.moraleMax : morale;
    if (morale == district.morale) return;
    const float before = Percent(district.morale, district.moraleMax);
    district.moraleBefore = district.morale;
    district.moraleChangedAt = GameTime();
    ++district.changes;
    district.morale = morale;
    SaveDistrict(district);
    words::PostCivilianMoraleChange(before, Percent(morale, district.moraleMax));
    if (step > 0.0f) messages::Tip("Morale", kMoraleTipMs);
    rfa::Log("%s: morale %.0f -> %.0f / %.0f (%+.0f%s%s)", district.name.c_str(), district.moraleBefore, morale, district.moraleMax, step, tag ? ", " : "",
             tag ? tag : "");
}

float PlayerDeathMorale() { return g_playerDeathMorale[DifficultyIndex()]; }

void ReloadState() {
    for (DistrictState& district : g_districts) {
        district.control = district.controlStart;
        district.morale = district.moraleStart;
        district.liberated = false;
        district.controlChangedAt = district.moraleChangedAt = -1;
        ++district.changes;
    }
    LoadSaved();
    rfa::Log("The campaign's districts read again from its save");
}

// A story mission recorded done (Guerrilla's mission cleanup; the mission work will call this): its district is
// liberated now if its EDF control is already 0 and it was the district's last (district::liberate_district).
void RecordMissionDone(const char* mission) {
    if (!mission || !*mission) return;
    save::SetInt("missions", mission, 1);
    for (const Mission& done : g_missions)
        if (_stricmp(done.name.c_str(), mission) == 0)
            if (const int index = DistrictNamed(done.district.c_str()); index >= 0) MaybeLiberate(g_districts[index]);
}

// For testing before missions exist (the console's rfg_district_missions_done): every story mission of the district
// marked done, then liberation checked as the last one's end would. The number marked.
int MarkMissionsDone(int which) {
    const int index = Resolve(which);
    if (index < 0 || index >= DistrictCount()) return 0;
    DistrictState& district = g_districts[index];
    int marked = 0;
    for (const Mission& mission : g_missions)
        if (_stricmp(mission.district.c_str(), district.name.c_str()) == 0 && !MissionDone(mission.name)) {
            save::SetInt("missions", mission.name.c_str(), 1);
            ++marked;
        }
    MaybeLiberate(district);
    return marked;
}

int MissionsLeftIn(int which) {
    const int index = Resolve(which);
    return index >= 0 && index < DistrictCount() ? MissionsLeft(g_districts[index]) : 0;
}

void SetEdfControl(float control, int which) {
    const int index = Resolve(which);
    if (index < 0 || index >= DistrictCount()) return;
    DistrictState& district = g_districts[index];
    const float before = Percent(district.control, district.controlMax);
    district.control = control < 0.0f ? 0.0f : control > district.controlMax ? district.controlMax : control;
    SaveDistrict(district);
    words::PostEdfControlLowered(before, Percent(district.control, district.controlMax));
    rfa::Log("%s: EDF control set to %.0f / %.0f", district.name.c_str(), district.control, district.controlMax);
    MaybeLiberate(district);
}

void SetMorale(float morale, int which) {
    const int index = Resolve(which);
    if (index < 0 || index >= DistrictCount()) return;
    DistrictState& district = g_districts[index];
    district.morale = morale < 0.0f ? 0.0f : morale > district.moraleMax ? district.moraleMax : morale;
    SaveDistrict(district);
}

float EffectiveMoralePercent(int which) {
    if (g_useTemporaryMorale) {
        const float morale = g_temporaryMorale + g_moraleOffset;
        return Percent(morale < 0.0f ? 0.0f : morale > kTemporaryMoraleMax ? kTemporaryMoraleMax : morale, kTemporaryMoraleMax);
    }
    const District* district = GetDistrict(Resolve(which));
    if (!district) return 0.0f;
    const float morale = district->morale + g_moraleOffset;
    return Percent(morale < 0.0f ? 0.0f : morale > district->moraleMax ? district->moraleMax : morale, district->moraleMax);
}

void UseTemporaryMorale(bool use) { g_useTemporaryMorale = use; }

void ChangeTemporaryMorale(float delta, bool set) {
    const float morale = set ? delta : g_temporaryMorale + delta;
    g_temporaryMorale = morale < 0.0f ? 0.0f : morale > kTemporaryMoraleMax ? kTemporaryMoraleMax : morale;
}

void ChangeMoraleOffset(float delta, bool set) { g_moraleOffset = set ? delta : g_moraleOffset + delta; }

// ---- Progression and salvage

// Held to Guerrilla's ranges, as district::liberate_district clamps them: the tech level 0..5 (its
// Max_tech_level default), the EDF and Marauder progression 0..7.
int Clamped(int level, int most) { return level < 0 ? 0 : level > most ? most : level; }

int TechLevel() { return g_techLevel; }
void SetTechLevel(int level) {
    g_techLevel = Clamped(level, 5);
    save::SetInt("progression", "tech_level", g_techLevel);
}
void SetEdfProgression(int level) {
    g_edfProgression = Clamped(level, 7);
    save::SetInt("progression", "edf", g_edfProgression);
}
int EdfProgressionLevel() {
    const District* district = GetDistrict(PlayerDistrict());
    if (!district || district->minEdfProgression < 0 || district->minEdfProgression >= 8) return g_edfProgression;
    return district->minEdfProgression > g_edfProgression ? district->minEdfProgression : g_edfProgression;
}
int MarauderProgressionLevel() { return g_marauderProgression; }
void SetMarauderProgression(int level) {
    g_marauderProgression = Clamped(level, 7);
    save::SetInt("progression", "marauder", g_marauderProgression);
}

int Salvage() { return g_salvage; }
void AddSalvage(int amount) {
    g_salvage += amount;
    save::SetInt("progression", "salvage", g_salvage);
    rfa::LogDetail("Salvage +%d (%d); Armageddon's own salvage isn't fed yet", amount, g_salvage);
}

// ---- The frame

void Init() {
    LoadDistricts();
    LoadAlertTable();
    LoadAlertValues();
    LoadAlertDifficulty();
    g_log = rfa::DetailLog();  // [GuerrillaCampaign] Log=, read as the mod starts
    rfa::Log("Alert: yellow at %.0f, orange at %.0f, red at %.0f; %.0f a second for each of the EDF seeing the player in green; decay after %.0f s",
             g_bands[handles::AlertYellow].min, g_bands[handles::AlertOrange].min, g_bands[handles::AlertRed].min, g_contactGrowth, g_decayDelay);
}

void Enter() {
    LoadSaved();
    // The alert isn't saved: a level starts at green, with no caps (gsm_level_init_pre).
    g_minAlert = 0.0f;
    g_maxAlert = g_bands[handles::AlertRed].max;
    g_alertValue = 0.0f;
    g_alertLevel = handles::AlertGreen;
    g_nextDelta = 0.0f;
    g_lastStep = -1;
    g_decayFrom = GameTime();
    g_restartDecayWait = false;
    g_graceUntil = -1;
    g_lastReportKnown = false;
    g_useTemporaryMorale = false;
    g_moraleOffset = 0.0f;
}

void Update(float) { AlertStep(); }

}  // namespace campaign::liberation
