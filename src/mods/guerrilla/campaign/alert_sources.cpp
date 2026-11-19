// What Guerrilla's alert level hears from the world, read off Armageddon's objects.
//
// Guerrilla's alert rises by what the EDF sees and loses (gfm_alert, its "alm" and "gsm" functions):
// - Its people who see the player: in green, each of the EDF's people (anyone whose team is friendly
//   to the EDF or better) who has a player in sight (a "first contact" belief of one, ai_perception::
//   any_enemy_player_in_los), has a target, and may engage it (can_engage_target_reason) adds 75 a
//   second; at any level, one such holds the alert's decay off. "May engage" is Guerrilla's reach by
//   alert level, 30, 40, 60, 80 m from green to red (its Ai_engage_distance values, in its code), twice
//   that when the soldier is in a vehicle or the target in a flyer. Guerrilla's other answers there
//   aren't ported yet: a squad under attack orders engages at any range (the reinforcements' squads
//   will), a turret's crew at 2.5 times, and at most so many attack the player at once.
// - Its dead (human_died → alm_report_human_death): each of its people who dies within 200 m of the
//   player while the alert is above green, whoever killed them, is reported once, as full damage to
//   someone of low importance (what every person on a side is, unless a zone says otherwise).
// - Its vehicles (vehicle_apply_damage → alm_report_vehicle_damage, vehicle_blow_up →
//   alm_report_vehicle_death): damage to a vehicle with someone in it, whose last driver was the
//   EDF's, counts above green; a wrecked vehicle of the EDF's own (its vehicles.xtbl Default_Team)
//   counts at any level. Guerrilla leaves out damage done by the EDF's own people; this port can't tell
//   who did it, as it reads each vehicle's damage every frame instead of hooking the game's damage
//   functions, so that counts too.
// - Its buildings (rfg_mover::serialize → sdc_create_damage_tracking_report, rfg_mover::
//   do_frame_report → sdc_report_new_damage, sdc_do_frame): every building of the EDF's (its gameplay
//   properties' team) with an importance above none has a report, which holds the most damage the
//   building has had (its damage percentage: how much of its structural mass is gone; 1 once it is
//   dead) and stops following once the building counts as destroyed in the game (game_destroyed);
//   four times a second each report with new damage goes to the alert, a second at least after its
//   last time. Armageddon still has all of this but makes no report for a building whose team is
//   hostile to the player, and does nothing with a report (its alert is gone), so the mod keeps its
//   own, by handle. A building first seen (its zone just loaded) starts from the damage it has.
// What each is worth, and the rest of the rules, are liberation.cpp's (ReportDamage, ReportDeath).
// The EDF's people confronting the player ("the alert in one second") were part of the AI Armageddon
// deleted.
//
// Every address is Armageddon's (the publicbeta build): the world's people and vehicles (0x43A860 and
// 0x43A8B0, 0x477090 and 0x4770E0: how many, and each), a person's team (+0x13B4) and how one team
// takes another (0x8A0460), whether something is dead (the fifth virtual function, as Guerrilla's), a
// person's AI (+0x174C) with its perception 0x14 in, whose beliefs of a kind are a list (0x42DA30 the
// first, 0x476420 the next; a belief's +0 is whom it is about; kind 5 is the first contact, as in
// Guerrilla), a person's target (0x880B70), a vehicle's table entry (+0x3DA8: +0 its name, +0x30 its
// special type, 3 a tank and 4 an artillery tank, +0x4D8 its alert multiplier), its flags (+0x4FB0,
// Guerrilla's: bit 10 a wreck, bit 29 someone in it) and hit points (0x603920, 0x603930), and the
// weapon in a person's hands (+0x1394 → +8 → +0x20, its table entry: +0x58 its alert multiplier).
// A building (rfg_mover; the world's, 0x4776E0 and 0x477730): its team a signed byte at +0x12E, its
// importance from its gameplay properties (object_mover::get_importance_level, 0x8D75E0), its mover
// flags +0xF0 (bit 2: it makes no reports), its damage percentage +0x13C, its flags +0x150 (bit 4:
// game_destroyed).
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "alert_sources.h"
#include "handle_kinds.h"
#include "liberation.h"
#include <math.h>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace campaign::alert_sources {
namespace {

// ---- Armageddon's objects

void* const kWorld = reinterpret_cast<void*>(0x1870630);
using CountFn = int(__fastcall*)(void* world, void* unused);
using AtFn = BYTE*(__fastcall*)(void* world, void* unused, int index);
using ByHandleFn = BYTE*(__fastcall*)(void* world, void* unused, DWORD handle);
auto const HumanCount = reinterpret_cast<CountFn>(0x43A860);
auto const HumanAt = reinterpret_cast<AtFn>(0x43A8B0);
auto const HumanByHandle = reinterpret_cast<ByHandleFn>(0x436120);
auto const VehicleCount = reinterpret_cast<CountFn>(0x477090);
auto const VehicleAt = reinterpret_cast<AtFn>(0x4770E0);
auto const VehicleByHandle = reinterpret_cast<ByHandleFn>(0x42AF10);
auto const BuildingCount = reinterpret_cast<CountFn>(0x4776E0);
auto const BuildingAt = reinterpret_cast<AtFn>(0x477730);
auto const ObjectByHandle = reinterpret_cast<ByHandleFn>(0x424350);

constexpr DWORD kHandle = 0x68, kTeam = 0x13B4, kInVehicle = 0x1160, kAi = 0x174C, kPerception = 0x14, kHeldItem = 0x1394;
constexpr DWORD kVehicleInfo = 0x3DA8, kVehicleFlags = 0x4FB0;
constexpr DWORD kInfoSpecialType = 0x30, kInfoAlertMultiplier = 0x4D8, kWeaponAlertMultiplier = 0x58;
constexpr DWORD kMoverTeam = 0x12E, kMoverFlags = 0xF0, kDamagePercent = 0x13C, kBuildingFlags = 0x150;
constexpr DWORD kNoReports = 1u << 2, kGameDestroyed = 1u << 4;
auto const ImportanceOf = reinterpret_cast<int(__fastcall*)(BYTE* mover, void* unused)>(0x8D75E0);  // 0 none .. 3 high
constexpr DWORD kNone = 0xFFFFFFFF;
constexpr DWORD kWreck = 1u << 10, kOccupied = 1u << 29;  // Guerrilla's vehicle_flags m_corpse, m_occupied
constexpr int kTank = 3, kArtilleryTank = 4;               // a vehicle's special type (Guerrilla's base_vehicle_types)
constexpr int kFlyer = 6, kWalker = 8;                     // a vehicle's kind of object (Guerrilla's 5 and 6)

// The teams, by both games' numbers, and how one takes another (0 hostile .. 3 friendly, 4 allied).
constexpr int kGuerrillas = 0, kEdf = 1, kCivilians = 2, kMarauders = 3;
constexpr int kFriendly = 3;
auto const Disposition = reinterpret_cast<int(__cdecl*)(int team, int other)>(0x8A0460);

using IsDeadFn = bool(__thiscall*)(void* object);
bool IsDead(BYTE* object) {
    void** functions = *reinterpret_cast<void***>(object);
    return reinterpret_cast<IsDeadFn>(functions[4])(object);
}

auto const CombatTarget = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human)>(0x880B70);
auto const Driving = reinterpret_cast<bool(__cdecl*)(BYTE* human)>(0x8695D0);
auto const FirstBelief = reinterpret_cast<BYTE*(__fastcall*)(BYTE* perception, void* unused, int kind)>(0x42DA30);
auto const NextBelief = reinterpret_cast<BYTE*(__fastcall*)(BYTE* perception, void* unused, BYTE* belief)>(0x476420);
constexpr int kFirstContact = 5;  // AI_BELIEF_FIRST_CONTACT: someone in sight now
constexpr int kEnemy = 0;         // AI_BELIEF_ENEMY_EXISTS: someone taken for an enemy
auto const ObjectKind = reinterpret_cast<int(__fastcall*)(BYTE* object, void* unused)>(0x423530);
auto const HitPoints = reinterpret_cast<int(__cdecl*)(BYTE* vehicle)>(0x603920);
auto const MaxHitPoints = reinterpret_cast<int(__cdecl*)(BYTE* vehicle)>(0x603930);

DWORD HandleOf(const BYTE* object) { return *reinterpret_cast<const DWORD*>(object + kHandle); }
int TeamOf(const BYTE* human) { return *reinterpret_cast<const int*>(human + kTeam); }
bool WithTheEdf(int team) { return Disposition(team, kEdf) >= kFriendly; }
BYTE* VehicleOf(const BYTE* human) {
    const DWORD handle = *reinterpret_cast<const DWORD*>(human + kInVehicle);
    return handle == kNone ? nullptr : VehicleByHandle(kWorld, nullptr, handle);
}
const BYTE* InfoOf(const BYTE* vehicle) { return *reinterpret_cast<const BYTE* const*>(vehicle + kVehicleInfo); }

float Squared(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// A multiplier read from one of the game's tables: 1 if it isn't a sensible number.
float Multiplier(const BYTE* entry, DWORD offset) {
    const float value = *reinterpret_cast<const float*>(entry + offset);
    return value >= 0.0f && value <= 100.0f ? value : 1.0f;
}

// ---- Guerrilla's vehicles' teams (vehicle_teams.txt, from the player's Guerrilla: name, tab, team)

std::unordered_map<std::string, int> g_vehicleTeams;
bool g_vehicleTeamsRead = false;

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return text;
}

void ReadVehicleTeams() {
    g_vehicleTeamsRead = true;
    std::ifstream file(ModFolder() + L"\\vehicle_teams.txt");
    int edf = 0;
    for (std::string line; std::getline(file, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const std::string team = Lower(line.substr(tab + 1));
        const int number = team == "edf" ? kEdf : team == "guerilla" || team == "guerrilla" ? kGuerrillas : team == "civilian" ? kCivilians
                           : team == "marauder" ? kMarauders : -1;
        if (number < 0) continue;
        g_vehicleTeams[Lower(line.substr(0, tab))] = number;
        edf += number == kEdf;
    }
    if (g_vehicleTeams.empty())
        rfa::Log("No vehicle_teams.txt (editor\\GuerrillaBuilder.exe build people writes it): the EDF's wrecked vehicles won't raise the alert");
    else
        rfa::Log("vehicle_teams.txt: %u vehicles, %d the EDF's", static_cast<unsigned>(g_vehicleTeams.size()), edf);
}

// The team a vehicle is made for (Guerrilla's Default_Team), or -1.
int OwnTeam(const BYTE* vehicle) {
    const BYTE* info = InfoOf(vehicle);
    const char* name = info ? *reinterpret_cast<const char* const*>(info) : nullptr;
    if (!name) return -1;
    const auto found = g_vehicleTeams.find(Lower(name));
    return found == g_vehicleTeams.end() ? -1 : found->second;
}

float DamageOf(BYTE* vehicle) {
    const int most = MaxHitPoints(vehicle), now = HitPoints(vehicle);
    if (most <= 0) return 0.0f;
    const float damage = 1.0f - static_cast<float>(now) / static_cast<float>(most);
    return damage < 0.0f ? 0.0f : damage > 1.0f ? 1.0f : damage;
}

// ---- Who was counted

std::unordered_set<DWORD> g_deadPeople;  // each one's death is looked at once (Guerrilla's death-reported flag)
struct VehicleSeen {
    float reported = 0.0f;       // its damage when last looked at (vehicle::last_damage_reported)
    bool wreckSeen = false;      // its end was looked at (vehicle_flags death_reported)
    int lastDriverTeam = -1;     // team_of_most_recent_driver
};
std::unordered_map<DWORD, VehicleSeen> g_vehicles;
struct BuildingSeen {
    float damage = 0.0f;         // the most it has had (gfm_damage_report damage_level)
    float reported = 0.0f;       // what it has been counted for (damage_level_reported)
    int mayReportAt = 0;         // on the game's clock (the report's can_report timer)
    bool destroyedSeen = false;  // it counted as destroyed in the game
};
std::unordered_map<DWORD, BuildingSeen> g_buildings;
int g_frames = 0;
int g_buildingsNext = 0;         // when the buildings are looked at next (Guerrilla's Report_period)
bool g_buildingsSaid = false;    // the log has how many are watched, once a level
constexpr int kBuildingsPeriodMs = 250, kReportAgainMs = 1000;

// The EDF's engaging reach by alert level, in metres (Guerrilla's code, 0x12C8CF4), and what it is
// multiplied by for a soldier in a vehicle or a target in a flyer (0x12C86B0).
constexpr float kEngageReach[handles::kAlertLevels] = {30.0f, 40.0f, 60.0f, 80.0f};
constexpr float kVehicleReach = 2.0f;

// ai_perception::any_enemy_player_in_los: a first-contact belief about the player.
bool HasPlayerInSight(BYTE* human, DWORD playerHandle) { return Believes(human, kFirstContact, playerHandle); }

void LookAtPeople(BYTE* player) {
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!human || human == player) continue;
        const int team = TeamOf(human);
        if (!IsDead(human)) {
            // Who drives what, for the vehicles' reports.
            if (BYTE* vehicle = VehicleOf(human))
                if (Driving(human)) {
                    const auto seen = g_vehicles.find(HandleOf(vehicle));
                    if (seen != g_vehicles.end()) seen->second.lastDriverTeam = team;
                }
            continue;
        }
        // alm_report_human_death: each death once; nothing in green or in the grace after an alert.
        if (!g_deadPeople.insert(HandleOf(human)).second) continue;
        if (liberation::AlertGrace() || liberation::AlertLevel() < handles::AlertYellow || !WithTheEdf(team)) continue;
        liberation::DamageReport report;
        report.handle = HandleOf(human);
        report.kind = liberation::ReportHuman;
        report.importance = 1;  // low: what human::serialize gives everyone on a side
        report.damage = 1.0f;
        report.at = PositionOf(human);
        liberation::ReportDeath(report, "one of the EDF's people killed");
    }
}

void LookAtVehicles() {
    const int count = VehicleCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* vehicle = VehicleAt(kWorld, nullptr, i);
        if (!vehicle) continue;
        const DWORD handle = HandleOf(vehicle);
        const DWORD flags = *reinterpret_cast<const DWORD*>(vehicle + kVehicleFlags);
        const float damage = DamageOf(vehicle);
        const bool wreck = (flags & kWreck) != 0 || damage >= 1.0f;
        auto found = g_vehicles.find(handle);
        if (found == g_vehicles.end()) {
            // First seen: what it has had so far isn't the player's doing.
            VehicleSeen seen;
            seen.reported = damage;
            seen.wreckSeen = wreck;
            g_vehicles[handle] = seen;
            continue;
        }
        VehicleSeen& seen = found->second;
        liberation::DamageReport report;
        report.handle = handle;
        report.kind = liberation::ReportVehicle;
        report.importance = 1;  // low, as Guerrilla reports every vehicle
        report.reported = seen.reported;
        report.at = PositionOf(vehicle);
        if (wreck) {
            // alm_report_vehicle_death: one of the EDF's own, once.
            if (seen.wreckSeen) continue;
            seen.wreckSeen = true;
            if (liberation::AlertGrace() || !WithTheEdf(OwnTeam(vehicle))) continue;
            report.damage = 1.0f;
            liberation::ReportDeath(report, "an EDF vehicle wrecked");
        } else if (damage > seen.reported) {
            // alm_report_vehicle_damage: someone in it, and its last driver the EDF's.
            if (!liberation::AlertGrace() && (flags & kOccupied) && seen.lastDriverTeam >= 0 && WithTheEdf(seen.lastDriverTeam)) {
                report.damage = damage;
                liberation::ReportDamage(report, "an EDF vehicle damaged");
            }
            seen.reported = damage;  // looked at, counted or not (vehicle::update_last_damage_reported)
        }
    }
}

const char* ImportanceName(int importance) { return importance == 3 ? "high" : importance == 2 ? "medium" : "low"; }

bool InLiberatedDistrict(const BYTE* building) {
    const liberation::District* district = liberation::GetDistrict(liberation::DistrictAt(PositionOf(building)));
    return district && district->liberated;
}

// sdc_do_frame, with the reports kept here.
void LookAtBuildings() {
    const int now = GameTime();
    if (now < g_buildingsNext && g_buildingsNext - now <= kBuildingsPeriodMs) return;  // not yet (unless the clock began again)
    g_buildingsNext = now + kBuildingsPeriodMs;
    const int count = BuildingCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* building = BuildingAt(kWorld, nullptr, i);
        if (!building) continue;
        // sdc_create_damage_tracking_report: the EDF's, of some importance.
        if (*reinterpret_cast<const signed char*>(building + kMoverTeam) != kEdf) continue;
        // Not in a liberated district: Guerrilla's liberation turns the EDF's buildings there to no team
        // (object_mover::check_liberation RM 0xA13860, on liberation and as each streams in), so they make no
        // reports. Ours keep their team; they're left out here instead.
        if (InLiberatedDistrict(building)) continue;
        const int importance = ImportanceOf(building, nullptr);
        if (importance < 1 || importance > 3) continue;
        const float percent = *reinterpret_cast<const float*>(building + kDamagePercent);
        const bool destroyed = (*reinterpret_cast<const DWORD*>(building + kBuildingFlags) & kGameDestroyed) != 0;
        const DWORD handle = HandleOf(building);
        auto found = g_buildings.find(handle);
        if (found == g_buildings.end()) {
            // First seen: what it has had so far isn't the player's doing.
            BuildingSeen seen;
            seen.damage = seen.reported = percent < 0.0f ? 0.0f : percent > 1.0f ? 1.0f : percent;
            seen.destroyedSeen = destroyed;
            g_buildings[handle] = seen;
            continue;
        }
        BuildingSeen& seen = found->second;
        // rfg_mover::do_frame_report → sdc_report_new_damage, until the building counts as destroyed.
        if (!destroyed && !(*reinterpret_cast<const DWORD*>(building + kMoverFlags) & kNoReports)) {
            const float damage = IsDead(building) ? 1.0f : percent;
            if (damage > seen.damage) seen.damage = damage > 1.0f ? 1.0f : damage;
        }
        if (destroyed && !seen.destroyedSeen) {
            seen.destroyedSeen = true;
            // Guerrilla lowers the district's EDF control here too (object_mover::drop_control): district_sources.cpp.
            rfa::LogDetail("An EDF building of %s importance is destroyed (%.0f %% gone)", ImportanceName(importance), percent * 100.0f);
        }
        if (seen.damage <= seen.reported || now < seen.mayReportAt) continue;
        liberation::DamageReport report;
        report.handle = handle;
        report.kind = liberation::ReportStructure;
        report.importance = importance;
        report.damage = seen.damage;
        report.reported = seen.reported;
        report.at = PositionOf(building);
        if (seen.damage >= 1.0f) liberation::ReportDeath(report, "an EDF building brought down");
        else liberation::ReportDamage(report, "an EDF building damaged");
        seen.reported = report.reported;
        seen.mayReportAt = now + kReportAgainMs;
    }
    if (!g_buildingsSaid && !g_buildings.empty()) {
        g_buildingsSaid = true;
        rfa::Log("Watching %u of the EDF's buildings in the zones loaded (of %d buildings)", static_cast<unsigned>(g_buildings.size()), count);
    }
}

// Forgets the people, vehicles and buildings that are gone, now and then.
void Tidy() {
    for (auto it = g_deadPeople.begin(); it != g_deadPeople.end();)
        it = HumanByHandle(kWorld, nullptr, *it) ? std::next(it) : g_deadPeople.erase(it);
    for (auto it = g_vehicles.begin(); it != g_vehicles.end();)
        it = VehicleByHandle(kWorld, nullptr, it->first) ? std::next(it) : g_vehicles.erase(it);
    for (auto it = g_buildings.begin(); it != g_buildings.end();)
        it = ObjectByHandle(kWorld, nullptr, it->first) ? std::next(it) : g_buildings.erase(it);
}

}  // namespace

void Enter() {
    if (!g_vehicleTeamsRead) ReadVehicleTeams();
    g_deadPeople.clear();
    g_vehicles.clear();
    g_buildings.clear();
    g_frames = 0;
    g_buildingsNext = 0;
    g_buildingsSaid = false;
}

void Frame() {
    BYTE* player = static_cast<BYTE*>(Player());
    if (!player) return;
    LookAtVehicles();
    LookAtPeople(player);
    LookAtBuildings();
    if (++g_frames % 600 == 0) Tidy();
}

bool Believes(const void* human, int kind, unsigned about) {
    BYTE* ai = human ? *reinterpret_cast<BYTE* const*>(static_cast<const BYTE*>(human) + kAi) : nullptr;
    if (!ai) return false;
    BYTE* perception = ai + kPerception;
    int walked = 0;
    for (BYTE* belief = FirstBelief(perception, nullptr, kind); belief && walked < 64; belief = NextBelief(perception, nullptr, belief), ++walked)
        if (*reinterpret_cast<const DWORD*>(belief) == about) return true;
    return false;
}

int EdfHoldingPlayerForEnemy() {
    BYTE* player = static_cast<BYTE*>(Player());
    if (!player) return 0;
    const DWORD playerHandle = HandleOf(player);
    int holding = 0;
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!human || human == player || IsDead(human) || !WithTheEdf(TeamOf(human))) continue;
        if (Believes(human, kEnemy, playerHandle)) ++holding;
    }
    return holding;
}

int EdfSeeingPlayer() {
    BYTE* player = static_cast<BYTE*>(Player());
    if (!player) return 0;
    const DWORD playerHandle = HandleOf(player);
    const int level = liberation::AlertLevel();
    const float reach = kEngageReach[level < 0 ? 0 : level >= handles::kAlertLevels ? handles::kAlertLevels - 1 : level];
    int seeing = 0;
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!human || human == player || IsDead(human) || !WithTheEdf(TeamOf(human))) continue;
        if (!HasPlayerInSight(human, playerHandle)) continue;
        BYTE* target = CombatTarget(human);
        if (!target) continue;
        // can_engage_target_reason: within the level's reach of its target.
        BYTE* targetVehicle = VehicleOf(target);
        const bool further = VehicleOf(human) || (targetVehicle && ObjectKind(targetVehicle, nullptr) == kFlyer);
        const float within = reach * (further ? kVehicleReach : 1.0f);
        if (Squared(PositionOf(human), PositionOf(target)) > within * within) continue;
        ++seeing;
    }
    return seeing;
}

float PlayerMultiplier() {
    const BYTE* player = static_cast<const BYTE*>(Player());
    if (!player) return 1.0f;
    if (*reinterpret_cast<const DWORD*>(player + kInVehicle) != kNone) {
        const BYTE* vehicle = VehicleOf(player);
        const BYTE* info = vehicle ? InfoOf(vehicle) : nullptr;
        return info ? Multiplier(info, kInfoAlertMultiplier) : 1.0f;
    }
    const BYTE* item = *reinterpret_cast<const BYTE* const*>(player + kHeldItem);
    const BYTE* itemInfo = item ? *reinterpret_cast<const BYTE* const*>(item + 8) : nullptr;
    const BYTE* weapon = itemInfo ? *reinterpret_cast<const BYTE* const*>(itemInfo + 0x20) : nullptr;
    return weapon ? Multiplier(weapon, kWeaponAlertMultiplier) : 1.0f;
}

bool PlayerInTankOrWalker() {
    const BYTE* player = static_cast<const BYTE*>(Player());
    BYTE* vehicle = player ? VehicleOf(player) : nullptr;
    if (!vehicle) return false;
    const BYTE* info = InfoOf(vehicle);
    const int special = info ? *reinterpret_cast<const int*>(info + kInfoSpecialType) : 0;
    return special == kTank || special == kArtilleryTank || ObjectKind(vehicle, nullptr) == kWalker;
}

}  // namespace campaign::alert_sources
