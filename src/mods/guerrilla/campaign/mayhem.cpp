// Guerrilla's mayhem (activity_mayhem, mode 7, a free activity: it runs alongside the others and starts itself), and
// its cost of damage (activity_register_damage_get_cost), which riding shotgun's score will use too.
//
// One of Guerrilla's damage listeners, it keeps a score in millions of credits of the player's damage to what isn't
// friendly to the guerrillas (count_object_damage RM 0x7590A0: the EDF's, the Marauders', and what has no team, not the
// civilians' or the guerrillas'), and a timer (add_time 0x76EB00: while it runs on, time is added and kept between
// Mayhem_min_duration and Mayhem_max_duration, 0 and 10 s; once it has run out, a new one starts, the score and
// counts cleared first when mayhem isn't running):
// - a hit of 100 or more with the player's doing on either side, while no timer is set: Mayhem_start_duration (3 s)
//   (register_damage 0x7989E0; no score);
// - something killed with the player's doing, not a building: its cost added, and Time_per_score (100 s a million)
//   × the cost × 0.1 of time, a 3 s timer first if none is set (register_death 0x798A50);
// - a building's new damage, the building the player's doing: its cost × the damage fraction added, and 100 s a
//   million of time (register_building_damage 0x798B50);
// - a building destroyed (not a prop), the player's doing: its cost × Destroy_bonus (0.5), the same time
//   (register_building_destroyed 0x798BD0);
// - the player's explosion: 1 s (register_explosion 0x783540).
// Once the score reaches its first level (10 million) with a timer set, it starts (do_frame_inactive 0x76E8E0 →
// activity_start 0x7ADC40: Mayhem_max_duration on the timer, the tool tip Mayhem); it ends when the timer runs out
// (do_frame_active 0x758FD0 → end 0x7ADC90): morale in the player's district by the highest level reached
// (activity_mayhem.xtbl: 10, 20, 50, 100, 200 million give 3, 5, 7, 10, 15). Killed, it gives nothing (kill 0x798C80).
// The diversions panel shows MAYHEM with the score ("%.2f", get_ui_display_info 0x759010: the time left).
// Demolitions master kills it and keeps it off while a challenge runs (its activity_start 0x7AD7C1, end and kill);
// the Tutorial mission switches it off for good (mission_activate_post_save 0x79D7EF: activities::MissionActivated).
// Guerrilla's numbers are its activity_mayhem.xtbl's and tweak_table.xtbl's. Its render_hud (debug text) is never
// drawn in Guerrilla. Not ported: its sound (HUD_DIV_MAYHEM_ICON: the interface's sound banks are full).
//
// The cost (activity_register_damage_get_cost 0x7705C0), in millions: a person Human_cost (0.1); a vehicle its table's
// value (our vehicles.xtbl keeps Guerrilla's Value × 1,000,000: 0.02 a car, 3 a heavy tank; vehicle info +0x5C, a
// float), else Vehicle_cost (0.15); a
// building (rfg_mover) its own value where Guerrilla's gameplay_properties row has one (building_values.txt, by the
// builder), else its size (its local box's volume × Building_cost_per_volume, 0.0005; twice that for one that counts
// as a building: destruction data ready, fixed, 3 m on each side and over 2000 kg) × 4 for communications (its mover
// type flags bit 0), else × 1.1, 1.5, 2 by its importance; a prop its size; a turret Turret_cost (0.05); anything else
// nothing. Armageddon's mover: its local box (vtable +0xC), mass (vtable +0x5C), world box +0x7C / +0x88, destruction
// data state +0x188 (1 ready), move type +0x19D (0 fixed), mover type flags +0xA0, team +0x12E (signed), importance
// 0x8D75E0; an object's parent +0x6C.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "activity_base.h"
#include "alert_sources.h"
#include "liberation.h"
#include "messages.h"
#include "tables.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace campaign::activities {
namespace {

// ---- Armageddon's objects

constexpr DWORD kHandle = 0x68, kParent = 0x6C, kObjectType = 0x7A, kObjectSubtype = 0x7B, kTeam = 0x13B4;
constexpr DWORD kWorldBoxMin = 0x7C, kWorldBoxMax = 0x88, kMoverTypeFlags = 0xA0, kMoverTeam = 0x12E, kDestructionState = 0x188, kMoveType = 0x19D;
constexpr DWORD kVehicleInfo = 0x3DA8, kInfoValue = 0x5C;
constexpr int kHuman = 0, kItem = 1, kMover = 2, kVehicle = 3, kEffect = 4, kDebris = 5, kTurret = 6;
constexpr int kGeneralMover = 0, kRfgMover = 1;  // a mover's subtype
constexpr int kGuerrillas = 0, kFriendly = 3, kNoDisposition = 2;
constexpr DWORD kNoHandle = 0xFFFFFFFF;
constexpr float kValueScale = 1000000.0f;  // our vehicles.xtbl's Value: Guerrilla's millions × 1,000,000
auto const ImportanceOf = reinterpret_cast<int(__fastcall*)(const BYTE* mover, void* unused)>(0x8D75E0);  // 0 none .. 3 high
auto const Disposition = reinterpret_cast<int(__cdecl*)(int team, int other)>(0x8A0460);

int TypeOf(const void* object) { return static_cast<signed char>(static_cast<const BYTE*>(object)[kObjectType]); }
int SubtypeOf(const void* object) { return static_cast<signed char>(static_cast<const BYTE*>(object)[kObjectSubtype]); }
DWORD HandleOf(const void* object) { return *reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(object) + kHandle); }
bool IsBuilding(const void* object) { return TypeOf(object) == kMover && SubtypeOf(object) == kRfgMover; }
bool IsProp(const void* object) { return TypeOf(object) == kMover && SubtypeOf(object) == kGeneralMover; }

// A mover's local box (its vtable +0xC, thiscall: the box's lower and upper corners): its volume.
float LocalVolume(const void* mover) {
    using LocalBoxFn = void(__fastcall*)(const void* self, void* unused, Vector* low, Vector* high);
    Vector low{}, high{};
    reinterpret_cast<LocalBoxFn>((*static_cast<void* const* const*>(mover))[0xC / 4])(mover, nullptr, &low, &high);
    return fabsf((high.x - low.x) * (high.y - low.y) * (high.z - low.z));
}

float MassOf(const void* mover) {
    using MassFn = float(__fastcall*)(const void* self, void* unused);
    return reinterpret_cast<MassFn>((*static_cast<void* const* const*>(mover))[0x5C / 4])(mover, nullptr);
}

bool Fixed(const BYTE* mover) { return mover[kMoveType] == 0; }

// rfg_mover::has_building_size: fixed, its world box 3 m or more on each side.
bool HasBuildingSize(const BYTE* mover) {
    if (!Fixed(mover)) return false;
    const Vector low = *reinterpret_cast<const Vector*>(mover + kWorldBoxMin), high = *reinterpret_cast<const Vector*>(mover + kWorldBoxMax);
    return high.x - low.x >= 3.0f && high.y - low.y >= 3.0f && high.z - low.z >= 3.0f;
}

// rfg_mover::has_building_mass: fixed, more than 2000 kg.
bool HasBuildingMass(const BYTE* mover) { return Fixed(mover) && MassOf(mover) > 2000.0f; }

// ---- Guerrilla's numbers

float g_humanCost = 0.1f;               // Human_cost
float g_vehicleCost = 0.15f;            // Vehicle_cost
float g_turretCost = 0.05f;             // Turret_cost
float g_costPerVolume = 0.0005f;        // Building_cost_per_volume
int g_startDuration = 3000;             // Mayhem_start_duration
int g_minDuration = 0;                  // Mayhem_min_duration (the code's 1000, the table's 0)
int g_maxDuration = 10000;              // Mayhem_max_duration
float g_timePerScore = 100000.0f;       // activity_mayhem.xtbl Time_per_score (ms a million)
float g_destroyBonus = 0.5f;            // Destroy_bonus
struct Level {
    float threshold, reward;            // millions, morale
};
std::vector<Level> g_levels = {{10.0f, 3.0f}, {20.0f, 5.0f}, {50.0f, 7.0f}, {100.0f, 10.0f}, {200.0f, 15.0f}};
constexpr int kExplosionMs = 1000;      // register_explosion's
constexpr int kTipMs = 3000;            // ui_tool_tips_add_tip("Mayhem", 3000)

void LoadNumbers() {
    std::vector<tables::Element> rows;
    if (tables::Load("tweak_table", rows)) {
        struct Tweak {
            const char* name;
            float* value;
        };
        const Tweak tweaks[] = {{"Human_cost", &g_humanCost}, {"Vehicle_cost", &g_vehicleCost}, {"Turret_cost", &g_turretCost},
                                {"Building_cost_per_volume", &g_costPerVolume}};
        struct IntTweak {
            const char* name;
            int* value;
        };
        const IntTweak ints[] = {{"Mayhem_start_duration", &g_startDuration}, {"Mayhem_min_duration", &g_minDuration}, {"Mayhem_max_duration", &g_maxDuration}};
        for (const tables::Element& row : rows) {
            const std::string name = row.Text("Name");
            for (const Tweak& tweak : tweaks)
                if (_stricmp(name.c_str(), tweak.name) == 0) *tweak.value = row.Number("Value", *tweak.value);
            for (const IntTweak& tweak : ints)
                if (_stricmp(name.c_str(), tweak.name) == 0) *tweak.value = static_cast<int>(row.Number("Value", static_cast<float>(*tweak.value)));
        }
    }
    rows.clear();
    if (tables::Load("activity_mayhem", rows) && !rows.empty()) {
        const tables::Element& entry = rows.front();
        if (const tables::Element* levels = entry.Child("Levels")) {
            std::vector<Level> read;
            for (const tables::Element* level : levels->Children("Level"))
                read.push_back({level->Number("Threshold", 0.0f), level->Number("Reward", 0.0f)});
            if (!read.empty()) g_levels = read;
        }
        g_timePerScore = entry.Number("Time_per_score", g_timePerScore);
        g_destroyBonus = entry.Number("Destroy_bonus", g_destroyBonus);
    }
    rfa::LogDetail("Mayhem: %u levels from %.0f million, %.0f ms of time a million, timer %d-%d ms (%d ms to begin)",
             static_cast<unsigned>(g_levels.size()), g_levels.empty() ? 0.0f : g_levels.front().threshold, g_timePerScore, g_minDuration, g_maxDuration,
             g_startDuration);
}

// ---- The buildings with a value of their own (mods\guerrilla_campaign\building_values.txt)

struct Valued {
    Vector at{};
    float value = -1.0f;
};
std::unordered_map<unsigned, Valued> g_valued;                 // by Guerrilla's handle
std::unordered_map<long long, std::vector<unsigned>> g_byPlace;  // by the metre cell on the ground

long long Cell(float x, float z) { return (static_cast<long long>(floorf(x)) << 32) ^ static_cast<unsigned>(static_cast<int>(floorf(z))); }

void ReadValues() {
    std::ifstream file(ModFolder() + L"\\building_values.txt");
    for (std::string line; std::getline(file, line);) {
        std::istringstream in(line);
        std::string word, handle;
        Valued valued;
        if (!(in >> word) || word != "value") continue;
        if (!(in >> handle >> valued.at.x >> valued.at.y >> valued.at.z >> valued.value)) continue;
        const unsigned h = static_cast<unsigned>(strtoul(handle.c_str(), nullptr, 16));
        g_valued[h] = valued;
        g_byPlace[Cell(valued.at.x, valued.at.z)].push_back(h);
    }
    if (g_valued.empty())
        rfa::Log("No building_values.txt (editor\\GuerrillaBuilder.exe build people writes it): mayhem prices every building by its size");
    else
        rfa::LogDetail("building_values.txt: %u buildings with a value of their own", static_cast<unsigned>(g_valued.size()));
}

bool SamePlace(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz < 0.5f * 0.5f;
}

// A building's own value (gameplay_properties' value): by its handle at its place, else by its place; -1 none.
float OwnValue(const BYTE* building) {
    const Vector at = PositionOf(building);
    const auto byHandle = g_valued.find(HandleOf(building));
    if (byHandle != g_valued.end() && SamePlace(byHandle->second.at, at)) return byHandle->second.value;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            const auto cell = g_byPlace.find(Cell(at.x + dx, at.z + dz));
            if (cell == g_byPlace.end()) continue;
            for (unsigned handle : cell->second)
                if (SamePlace(g_valued[handle].at, at)) return g_valued[handle].value;
        }
    return -1.0f;
}

// ---- count_object_damage: whose it is

// human_teams_get_team: a person's team, a building's, a vehicle's (alert_sources::VehicleTeam); a prop has none (-1).
int TeamOf(const void* object) {
    switch (TypeOf(object)) {
    case kHuman:
        return *reinterpret_cast<const int*>(static_cast<const BYTE*>(object) + kTeam);
    case kMover:
        return IsBuilding(object) ? static_cast<signed char>(static_cast<const BYTE*>(object)[kMoverTeam]) : -1;
    case kVehicle:
        return alert_sources::VehicleTeam(object);
    default:
        return -1;
    }
}

// count_object_damage: not a piece of a building (a mover with a parent); counted when the guerrillas don't take its
// team as friendly (a team outside 0..3, or none, is TEAM_DISPOSITION_NONE).
bool Counted(const void* object) {
    if (!object) return false;
    if (SubtypeOf(object) == kRfgMover && *reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(object) + kParent) != kNoHandle) return false;
    const int team = TeamOf(object);
    const int disposition = team >= 0 && team <= 3 ? Disposition(kGuerrillas, team) : kNoDisposition;
    return disposition < kFriendly;
}

// ---- Mayhem

class MayhemActivity : public Activity {
public:
    MayhemActivity() : Activity(Mayhem, false, true) {
        LoadNumbers();
        ReadValues();
    }

    bool Listens() const override { return true; }
    const char* Title() const override { return "ACT_MAYHEM"; }

    bool UiText(std::string& text, int& number) const override {
        char score[32];
        sprintf_s(score, "%.2f", score_);
        text = score;
        number = TimeLeft();
        return true;
    }

    void LevelInit() override {
        Activity::LevelInit();
        ResetMayhem();
        timerSet_ = false;
        highScore_ = 0.0f;
    }

    void RegisterDamage(void* victim, void* attacker, float damage) override {
        if (damage >= 100.0f && (PlayerControlled(victim) || PlayerControlled(attacker)) && !timerSet_) AddTime(g_startDuration);
    }

    void RegisterDeath(void* victim, void* killer) override {
        if (!victim || IsBuilding(victim)) return;  // buildings come by their own reports
        if (!PlayerControlled(killer) && !PlayerControlled(victim)) return;
        if (!timerSet_) AddTime(g_startDuration);
        const float cost = DamageCost(victim);
        if (!Counted(victim) || cost == 0.0f) return;
        score_ += cost;
        AddTime(static_cast<int>(g_timePerScore * cost * 0.1f));
        if (TypeOf(victim) == kHuman) ++humansKilled_;
        else if (TypeOf(victim) == kVehicle) ++vehiclesDestroyed_;
    }

    void RegisterBuildingDamage(void* building, float damage, float) override {
        if (!PlayerControlled(building) || !Counted(building)) return;
        const float delta = DamageCost(building) * damage;
        score_ += delta;
        AddTime(static_cast<int>(g_timePerScore * delta));
    }

    void RegisterBuildingDestroyed(void* building) override {
        if (IsProp(building) || !PlayerControlled(building) || !Counted(building)) return;
        const float delta = DamageCost(building) * g_destroyBonus;
        score_ += delta;
        AddTime(static_cast<int>(g_timePerScore * delta));
        const BYTE* mover = static_cast<const BYTE*>(building);
        if (HasBuildingSize(mover) && HasBuildingMass(mover)) ++structuresDestroyed_;
    }

    void RegisterExplosion(void* owner) override {
        if (!owner || owner != Player()) return;
        AddTime(kExplosionMs);
        ++explosionsCaused_;
    }

protected:
    void FrameInactive(float) override {
        if (LevelReached() >= 0 && timerSet_) StartMayhem();
    }

    void FrameActive(float) override {
        if (TimerOut()) End();
    }

    // end 0x7ADC90: the timer gone, the best kept, morale by the level reached.
    void End() override {
        if (!IsActive()) return;
        Activity::End();
        timerSet_ = false;
        if (score_ > highScore_) highScore_ = score_;
        const int level = LevelReached();
        rfa::LogDetail("Mayhem: over at %.2f million (%d killed, %d vehicles, %d buildings, %d explosions): level %d%s", score_, humansKilled_,
                 vehiclesDestroyed_, structuresDestroyed_, explosionsCaused_, level + 1, level >= 0 ? "" : ", no reward");
        if (level >= 0 && g_levels[static_cast<size_t>(level)].reward != 0.0f) liberation::ChangeMorale(g_levels[static_cast<size_t>(level)].reward, -1);
    }

    // kill 0x798C80: nothing given, everything cleared.
    void Kill() override {
        if (IsActive()) rfa::LogDetail("Mayhem: stopped at %.2f million (nothing given)", score_);
        Activity::End();
        ResetMayhem();
        timerSet_ = false;
    }

private:
    // activity_start 0x7ADC40: the most time on the timer, the tool tip, then the free start.
    void StartMayhem() {
        AddTime(g_maxDuration);
        if (!StartFree()) return;
        messages::Tip("Mayhem", kTipMs);
        rfa::LogDetail("Mayhem: started at %.2f million", score_);
    }

    // add_time 0x76EB00.
    void AddTime(int ms) {
        const int now = GameTime();
        if (timerSet_ && now < expiresAt_) {
            int left = expiresAt_ - now + ms;
            if (left < g_minDuration) left = g_minDuration;
            if (left > g_maxDuration) left = g_maxDuration;
            expiresAt_ = now + left;
            return;
        }
        if (!IsActive()) ResetMayhem();
        timerSet_ = true;
        expiresAt_ = now + ms;
    }

    // activity_reset: the score and the counts cleared.
    void ResetMayhem() {
        score_ = 0.0f;
        humansKilled_ = vehiclesDestroyed_ = structuresDestroyed_ = explosionsCaused_ = 0;
    }

    bool TimerOut() const { return !timerSet_ || GameTime() >= expiresAt_; }
    int TimeLeft() const {
        if (!timerSet_) return 0;
        const int left = expiresAt_ - GameTime();
        return left > 0 ? left : 0;
    }

    // get_level 0x759070: the highest level whose threshold the score reaches, -1 none.
    int LevelReached() const {
        int level = -1;
        for (size_t i = 0; i < g_levels.size(); ++i)
            if (score_ >= g_levels[i].threshold) level = static_cast<int>(i);
        return level;
    }

    float score_ = 0.0f;  // millions of credits
    int humansKilled_ = 0, vehiclesDestroyed_ = 0, structuresDestroyed_ = 0, explosionsCaused_ = 0;
    bool timerSet_ = false;  // mayhem_timer valid
    int expiresAt_ = 0;      // the game clock's ms
    float highScore_ = 0.0f;
};

}  // namespace

float DamageCost(const void* object) {
    if (!object) return 0.0f;
    switch (TypeOf(object)) {
    case kHuman:
        return g_humanCost;
    case kVehicle: {
        const BYTE* info = *reinterpret_cast<const BYTE* const*>(static_cast<const BYTE*>(object) + kVehicleInfo);
        // vehicle_info's value: a float (Armageddon's table reader for Value, 0xC8CD30, stores it with fstp).
        return info ? *reinterpret_cast<const float*>(info + kInfoValue) / kValueScale : g_vehicleCost;
    }
    case kTurret:
        return g_turretCost;
    case kMover: {
        const float volume = LocalVolume(object) * g_costPerVolume;
        if (!IsBuilding(object)) return volume;
        const BYTE* mover = static_cast<const BYTE*>(object);
        float cost = volume;
        if (mover[kDestructionState] == 1 && HasBuildingSize(mover) && HasBuildingMass(mover)) cost *= 2.0f;
        const float own = OwnValue(mover);
        if (own != -1.0f) return own;
        if (*reinterpret_cast<const unsigned short*>(mover + kMoverTypeFlags) & 1) return cost * 4.0f;  // MOVER_TYPE_FLAG_COMMUNICATIONS
        switch (ImportanceOf(mover, nullptr)) {
        case 1: return cost * 1.1f;
        case 2: return cost * 1.5f;
        case 3: return cost * 2.0f;
        default: return cost;
        }
    }
    default:  // items, effects, debris
        return 0.0f;
    }
}

Activity* MakeMayhem() {
    static MayhemActivity mayhem;
    return &mayhem;
}

}  // namespace campaign::activities
