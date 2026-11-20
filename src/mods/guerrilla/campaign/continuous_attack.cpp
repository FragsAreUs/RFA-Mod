// Guerrilla's continuous attack (activity_continuous_attack_data, mode 14: a free activity, one at a time), on the
// framework: EDF squads sent at a target in waves, from activity_continuous_attack.xtbl's rows.
//
// Guerrilla (RM addresses):
//   - activity_cont_attack_start(target, crc of a row's name) 0x7ACFA0: no target, an unknown row or one already
//     running: no. Its disable flags cleared (level_init: a mission's doesn't stop it), then attack_init 0x7574D0: the
//     target fixed for the whole attack (a squad: its leader; a vehicle: whoever sits in seat 0; else itself), the
//     row's attacker_count; then the free start (activity_base_free::activity_start): RUNNING at once.
//   - do_frame_active 0x7B7330, every frame: each squad's reinforcement_squad::do_frame (attack_squads.cpp: kept while
//     it engages or closes on the target, let go farther than 325 m, after its 20 s timer, or when the target dies); a
//     squad gone frees its slot; then the spawn timer: the first squad at once, then one more each respawn_period
//     (whole seconds: 1.5 is 1) while fewer than attacker_count live (16 slots; a dead squad replaced one period
//     later; a failed spawn waits another period).
//   - add_new_squad 0x7B5760: the row's entry (only one: it; else a random one of the current sub-group whose vehicle
//     is loaded), at a spawn region (find_local_spawn_region 0x77FED0: any object_spawn_region in the world 50-350 m
//     from the player, 150-350 m for a squad with a vehicle, not one of the last 8 used if another will do; nothing
//     else checked), facing the target, attack orders on it.
//   - Ends (activity_cont_attack_end 0x7A0DE0, also its end and kill): every squad despawned (LOW), the row dropped.
//     Callers: the script word continuous_attack_end, a mission's end, area defense at each stage, and
//     activity_kill_all_except when a player-started activity begins. It shows nothing and counts nothing.
//
// Here:
//   - The spawn regions are Guerrilla's object_spawn_region (the world build writes them into campaign_objects.xml:
//     the world's zones', and each activity's layers', with that activity's name). A layer's count while its
//     activity's layers are loaded (managed_layers); the world's always.
//   - "Whose vehicle is loaded": every entry is taken as ready (I); a squad the game can't make yet (its vehicle's
//     model streaming in) waits for the next period, as Guerrilla's failed spawn does.
//   - A squad's car is handed to guerrilla_vehicles' chase driver (Guerrilla's chase_target_* pursuit, which reads the
//     attack's target) once it is within 250 m of the target, the chase driver's own reach, and again after a chase
//     ended by itself; flyers and walkers keep Armageddon's own AI with their attack orders.
//   - Members on foot who don't see the target are sent running to it every 4 s (as the EDF reinforcements').
//   - Not ported: the squads' disallow_reassignment, activity_pursuit_ca and limited_vehicle_exit flags (Armageddon's
//     fields not known); the Patrol type (its one row, Traffic Jam, is never started); the squad map icons (Guerrilla
//     never sets the switch that draws them); the per-entry veh_hp_scale_factor (no row sets it).
//   - The console: rfg_ca_start <row> (at the player; the row's own activity's spawn regions count as loaded, since
//     its layers come with that activity) and rfg_ca_end.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "vehicles/vehicles_api.h"
#include "activity_base.h"
#include "attack_squads.h"
#include "handle_kinds.h"
#include "managed_layers.h"
#include "tables.h"
#include "world_objects.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <deque>
#include <string>
#include <vector>

namespace campaign::activities {
namespace {

// ---- Armageddon's objects

void* const kWorld = reinterpret_cast<void*>(0x1870630);
constexpr DWORD kNone = 0xFFFFFFFF;
constexpr DWORD kObjectType = 0x7A;
constexpr int kVehicleType = 3;
auto const SeatOccupant = reinterpret_cast<BYTE*(__cdecl*)(BYTE* vehicle, int seat)>(0x603AE0);
auto const VehicleSubtype = reinterpret_cast<int(__fastcall*)(BYTE* vehicle, void* unused)>(0x423530);  // as guerrilla_vehicles' game.h
constexpr int kAutomobile = 5;  // 6 flyer, 8 walker
constexpr DWORD kHandle = 0x68;
DWORD HandleOf(const BYTE* object) { return *reinterpret_cast<const DWORD*>(object + kHandle); }
float FlatDistance2(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}

// ---- Guerrilla's numbers

constexpr int kSlots = 16;                  // farray<reinforcement_squad,16>
constexpr int kMostByScript = 15;           // the script words' cap on attacker_count
constexpr float kFootNear = 50.0f;          // find_local_spawn_region: [0x12DB874] 2500
constexpr float kVehicleNear = 150.0f;      // [0x12DAF04] 22500
constexpr float kFar = 350.0f;              // 122500
constexpr size_t kRecentRegions = 8;        // Last_spawn_region_list
constexpr float kChaseReach = attack_squads::kTooFar;  // the chase given up beyond reinforcement_squad's 325 m leash (GuerrillaVehicles_ChaseWithin)
constexpr int kChaseAgainMs = 4000;         // ours: members on foot sent to the target again
constexpr int kHandAgainMs = 5000;          // ours: a car whose chase ended by itself isn't handed over again before then
constexpr float kFlyerHeight = 30.0f;       // ours, as the reinforcements': a flyer starts 30 m up

// ---- activity_continuous_attack.xtbl (base_continuous_attack_data::load 0x780180)

struct Entry {
    std::string squad, group;  // squad_def; vehicle_group ("" on foot)
    int subGroup = 0;          // sub_group_index
    bool stayInVehicle = false;
};
struct Row {
    std::string name;
    int count = 0;     // attacker_count: squads
    int periodMs = 0;  // respawn_period, whole seconds
    bool patrol = false;
    std::vector<Entry> entries;
};
std::vector<Row> g_rows;

void LoadRows() {
    std::vector<tables::Element> rows;
    if (!tables::Load("activity_continuous_attack", rows)) return;
    for (const tables::Element& row : rows) {
        Row r;
        r.name = row.Text("Name");
        r.count = row.Int("attacker_count", 0);
        if (r.count < 0) r.count = 0;
        const float period = row.Number("respawn_period", 0.0f);
        r.periodMs = period <= 0.0f ? 0 : static_cast<int>(period) * 1000;  // truncated to whole seconds, as Guerrilla's
        r.patrol = _stricmp(row.Text("Type").c_str(), "Patrol") == 0;
        if (const tables::Element* list = row.Child("squad_list"))
            for (const tables::Element* info : list->Children("squad_info")) {
                Entry e;
                e.squad = info->Text("squad_def");
                e.group = info->Text("vehicle_group");
                e.subGroup = info->Int("sub_group_index", 0);
                e.stayInVehicle = info->Flag("stay_in_vehicle", false);
                if (!e.squad.empty()) r.entries.push_back(e);
            }
        if (r.name.empty() || r.entries.empty()) continue;
        g_rows.push_back(r);
    }
    rfa::Log("activity_continuous_attack.xtbl: %u continuous attacks", static_cast<unsigned>(g_rows.size()));
}

const Row* RowNamed(const char* name) {
    if (!name) return nullptr;
    for (const Row& r : g_rows)
        if (_stricmp(r.name.c_str(), name) == 0) return &r;
    return nullptr;
}

// ---- The spawn regions (campaign_objects.xml's object_spawn_region)

struct Region {
    DWORD handle;
    Vector at;
    std::string layer;  // the activity whose layers hold it, "-" for the world's
};
std::vector<Region> g_regions;
bool g_regionsRead = false;
std::deque<DWORD> g_recent;      // Last_spawn_region_list (kept over attacks, as Guerrilla's)
std::string g_consoleLayer;      // rfg_ca_start: its row's own activity, counted as loaded

void ReadRegions() {
    if (g_regionsRead) return;
    g_regionsRead = true;
    world::Load();
    int fromLayers = 0;
    for (const world::Object* o : world::OfClass("object_spawn_region")) {
        Region r{o->handle, o->pos, o->Property("layer", "-")};
        if (r.layer != "-") ++fromLayers;
        g_regions.push_back(r);
    }
    rfa::Log("Continuous attacks: %u spawn regions (%d in the world's zones, %d in activities' layers)%s", static_cast<unsigned>(g_regions.size()),
             static_cast<int>(g_regions.size()) - fromLayers, fromLayers,
             g_regions.empty() ? ": none (editor\\GuerrillaBuilder.exe build campaign makes them); no attacker can come" : "");
}

bool RegionLoaded(const Region& r) {
    if (r.layer == "-") return true;
    if (!g_consoleLayer.empty() && _stricmp(r.layer.c_str(), g_consoleLayer.c_str()) == 0) return true;
    return managed_layers::Loaded(r.layer.c_str());
}

// find_local_spawn_region: a region at the right distance from the player, not one of the last 8 used if another will
// do; the one picked joins the last 8. Null for none.
const Region* FindRegion(bool onFoot, const Vector& player) {
    const float nearest = onFoot ? kFootNear : kVehicleNear;
    std::vector<const Region*> fresh, used;
    for (const Region& r : g_regions) {
        if (!RegionLoaded(r)) continue;
        const float d2 = FlatDistance2(r.at, player);
        if (d2 < nearest * nearest || d2 > kFar * kFar) continue;
        bool recent = false;
        for (DWORD h : g_recent) recent = recent || h == r.handle;
        (recent ? used : fresh).push_back(&r);
    }
    if (fresh.empty() && used.empty()) return nullptr;
    if (fresh.empty()) g_recent.clear();
    const std::vector<const Region*>& from = fresh.empty() ? used : fresh;
    const Region* pick = from[rand() % from.size()];
    g_recent.push_back(pick->handle);
    if (g_recent.size() > kRecentRegions) g_recent.pop_front();
    return pick;
}

// ---- The console

using RegisterCommandFn = void*(__fastcall*)(void* command, void* unused, const char* name, const char* description, int type, void (*handler)());
auto const RegisterCommand = reinterpret_cast<RegisterCommandFn>(0xC7BD70);
using ParseArgumentsFn = void(__cdecl*)(int kinds, int flags);
auto const ParseArguments = reinterpret_cast<ParseArgumentsFn>(0xC7D570);
auto& g_commandRunning = *reinterpret_cast<int*>(0x28FF1C0);
auto const g_commandWord = reinterpret_cast<const char*>(0x28FF0B8);
using ConsolePrintFn = void(__cdecl*)(const char* format, ...);
auto const ConsolePrint = reinterpret_cast<ConsolePrintFn>(0xC7C440);
BYTE g_startCommand[32], g_endCommand[32];

void StartCommand();
void EndCommand();

// ---- The activity

class ContinuousAttackActivity : public Activity {
public:
    ContinuousAttackActivity() : Activity(ContinuousAttack, false, true) {}

    void LevelInit() override {
        static bool commandsAdded = false;
        if (!commandsAdded) {
            commandsAdded = true;
            RegisterCommand(g_startCommand, nullptr, "rfg_ca_start", "Starts one of Guerrilla's continuous attacks at you (a row of activity_continuous_attack.xtbl)", 3,
                            &StartCommand);
            RegisterCommand(g_endCommand, nullptr, "rfg_ca_end", "Ends the continuous attack running", 3, &EndCommand);
        }
        Activity::LevelInit();
        squads_.clear();  // gone with the old level
        Clear();
    }

    // activity_cont_attack_start: false with no target, no such row, or one already running.
    bool Begin(unsigned targetHandle, const Row* row, const char* who) {
        if (!row) return false;
        BYTE* object = static_cast<BYTE*>(ObjectFromHandle(targetHandle));
        if (!object) {
            rfa::Log("Continuous attack %s (%s): no target", row->name.c_str(), who);
            return false;
        }
        if (IsActive()) {
            rfa::Log("Continuous attack %s (%s): %s runs already", row->name.c_str(), who, row_ ? row_->name.c_str() : "one");
            return false;
        }
        // level_init → activity_reset: its disable flags cleared (a mission's doesn't stop it) and back to WAIT, so an
        // attack ended this frame (area defense's next stage) starts again at once.
        for (int reason = ByActivity; reason < Everything; ++reason) Enable(static_cast<DisableReason>(reason));
        Reset();
        // attack_init: the target, fixed for the whole attack.
        target_ = ResolveTarget(object);
        row_ = row;
        count_ = row->count;
        subGroup_ = 0;
        spawnAt_ = GameTime();  // the first squad at once
        chasedAt_ = 0;
        ReadRegions();
        if (!StartFree()) {
            rfa::Log("Continuous attack %s (%s): the framework didn't start it", row->name.c_str(), who);
            Clear();
            return false;
        }
        rfa::Log("Continuous attack %s (%s): started at %08X: %d squads at once, one every %d s, %u kinds", row->name.c_str(), who, target_, count_,
                 row->periodMs / 1000, static_cast<unsigned>(row->entries.size()));
        return true;
    }

    void Stop(const char* why) {
        if (!IsActive()) return;
        rfa::Log("Continuous attack %s: ended (%s)", row_ ? row_->name.c_str() : "?", why);
        End();
    }

    void SetCount(int count) {
        count_ = count < 0 ? 0 : count > kMostByScript ? kMostByScript : count;
        rfa::Log("Continuous attack %s: %d squads at once", row_ ? row_->name.c_str() : "-", count_);
    }
    void ChangeCount(int change) { SetCount(count_ + change); }
    void SetSubGroup(int group) {
        subGroup_ = group;
        rfa::Log("Continuous attack %s: its squads from group %d", row_ ? row_->name.c_str() : "-", group);
    }

protected:
    void FrameActive(float) override {
        if (!row_) return;
        const int now = GameTime();
        BYTE* target = static_cast<BYTE*>(ObjectFromHandle(target_));
        // Each squad's reinforcement_squad::do_frame: a slot is free once its squad is done (squad_despawned: by the
        // game's squad object, not its people alone, who come a few frames after it).
        for (size_t i = 0; i < squads_.size();) {
            attack_squads::Squad& squad = squads_[i];
            const std::vector<BYTE*> members = attack_squads::Members(squad.squad);
            if (const char* gone = attack_squads::Gone(squad, members, now)) {
                if (squad.rides && !squad.hadMembers) RFA_LOG_FIRST(24, "Continuous attack %s: %s is let go (%s)", row_->name.c_str(), squad.what.c_str(), gone);
                else RFA_LOG_DETAIL_FIRST(60, "Continuous attack %s: %s is done (%s)", row_->name.c_str(), squad.what.c_str(), gone);
                attack_squads::LetGo(squad);  // what's left of it (its car, its squad object) freed
                squads_.erase(squads_.begin() + i);
                continue;
            }
            if (members.empty()) {  // its people still to come
                ++i;
                continue;
            }
            if (const char* why = attack_squads::Follow(squad, members, now)) {
                RFA_LOG_DETAIL_FIRST(60, "Continuous attack %s: %s let go (%s)", row_->name.c_str(), squad.what.c_str(), why);
                attack_squads::LetGo(squad);
                squads_.erase(squads_.begin() + i);
                continue;
            }
            if (target) HandToChaseDriver(squad, members, target);
            ++i;
        }
        if (target && now - chasedAt_ >= kChaseAgainMs) {
            chasedAt_ = now;
            for (const attack_squads::Squad& squad : squads_) attack_squads::Chase(squad, target);
        }
        // While the target is dead no squad comes. Guerrilla keeps spawning at a dead target (the squads let go at once)
        // until a mission's death handling ends the attack or the reload starts the level again a few seconds later; ours
        // waits on the game-over screen as long as the player likes, and the let-go squads would fill the game's squads.
        // The reload starts it all again (GuerrillaCampaign_WorldOpened: level_init).
        const bool targetAlive = attack_squads::TargetAlive(target_);
        if (targetAlive != targetAlive_) {
            targetAlive_ = targetAlive;
            rfa::Log("Continuous attack %s: %s", row_->name.c_str(),
                     targetAlive ? "its target is alive again: squads come" : "its target is dead or gone: no more squads until it lives again or the level starts again");
        }
        // The spawn timer: when it runs out a squad comes (if fewer than the count live); with none set and room, it's
        // set to the period (a failed spawn waits another period).
        const int live = static_cast<int>(squads_.size());
        if (spawnAt_ >= 0) {
            if (now >= spawnAt_) {
                if (live < count_ && live < kSlots && targetAlive) AddSquad();
                spawnAt_ = -1;
            }
        } else if (live < count_) {
            spawnAt_ = now + row_->periodMs;
        }
    }

    // activity_cont_attack_end / its end and kill: every squad let go, the row dropped.
    void End() override {
        for (attack_squads::Squad& squad : squads_) attack_squads::LetGo(squad);
        squads_.clear();
        Activity::End();
        Clear();
    }
    void Kill() override {
        if (IsActive()) rfa::Log("Continuous attack %s: stopped (another activity began)", row_ ? row_->name.c_str() : "?");
        End();
    }

private:
    // attack_init: a squad's leader (here its first member, I), a vehicle's driver, else the object.
    unsigned ResolveTarget(BYTE* object) {
        const DWORD handle = HandleOf(object);
        if (static_cast<signed char>(object[kObjectType]) == kVehicleType) {
            if (BYTE* driver = SeatOccupant(object, 0)) return HandleOf(driver);
            return handle;
        }
        const std::vector<BYTE*> members = attack_squads::Members(handle);
        if (!members.empty()) return HandleOf(members.front());
        return handle;
    }

    // pick_squad_index: the only entry, else a random one of the current sub-group.
    const Entry* PickEntry() const {
        if (row_->entries.size() == 1) return &row_->entries[0];
        std::vector<const Entry*> candidates;
        for (const Entry& e : row_->entries)
            if (e.subGroup == subGroup_) candidates.push_back(&e);
        return candidates.empty() ? nullptr : candidates[rand() % candidates.size()];
    }

    // add_new_squad.
    void AddSquad() {
        const Entry* entry = PickEntry();
        if (!entry) {
            RFA_LOG_FIRST(20, "Continuous attack %s: no squad of group %d", row_->name.c_str(), subGroup_);
            return;
        }
        BYTE* player = static_cast<BYTE*>(Player());
        if (!player) return;
        const bool onFoot = !attack_squads::GroupExists(entry->group);
        const Region* region = FindRegion(onFoot, PositionOf(player));
        // Once each time it begins to find none (and once when it finds one again): Guerrilla tries again each period.
        if (!region) {
            if (!noRegion_)
                rfa::Log("Continuous attack %s: no spawn region %.0f to %.0f m from the player (of %u; loaded: the world's and its activities' loaded layers); tried again each period",
                         row_->name.c_str(), onFoot ? kFootNear : kVehicleNear, kFar, static_cast<unsigned>(g_regions.size()));
            noRegion_ = true;
            return;
        }
        if (noRegion_) rfa::Log("Continuous attack %s: a spawn region in reach again", row_->name.c_str());
        noRegion_ = false;
        attack_squads::Order order;
        order.squad = entry->squad;
        order.group = onFoot ? std::string() : entry->group;
        order.place = region->at;
        if (!onFoot && attack_squads::IsFlyerGroup(entry->group)) order.place.y += kFlyerHeight;
        order.target = target_;
        order.who = row_->name.c_str();
        order.forceSpawn = true;  // its squads come from 50-350 m (150-350 m in a vehicle): made at once (see attack_squads.h)
        attack_squads::Squad squad;
        std::string why;
        switch (attack_squads::Make(order, squad, why)) {
        case attack_squads::Made::Made:
            squads_.push_back(squad);
            rfa::LogDetail("Continuous attack %s: %s from spawn region %08X%s, %.0f m from the player (%u of %d)", row_->name.c_str(), squad.what.c_str(),
                           region->handle, region->layer == "-" ? "" : (" (" + region->layer + ")").c_str(), sqrtf(FlatDistance2(region->at, PositionOf(player))),
                           static_cast<unsigned>(squads_.size()), count_);
            break;
        case attack_squads::Made::Wait:
            RFA_LOG_FIRST(12, "Continuous attack %s: %s; tried again next period", row_->name.c_str(), why.c_str());
            break;
        case attack_squads::Made::Failed:
            RFA_LOG_FIRST(20, "Continuous attack %s: %s", row_->name.c_str(), why.c_str());
            break;
        }
    }

    // Its car's driver to guerrilla_vehicles' chase driver as soon as he sits in it (Guerrilla's squad drives at the
    // target from its spawn; Armageddon's attack orders would have the crew get out and walk), if the car is within
    // the chase's reach of the target; handed over again once back within it after a chase ended by itself (lost, the
    // crew out to fight). Flyers and walkers keep their own AI with their attack orders (the chase drives cars only).
    // The car is the one the game's squad made (attack_squads::VehicleOf: object_squad +0x10C).
    void HandToChaseDriver(attack_squads::Squad& squad, const std::vector<BYTE*>& members, BYTE* target) {
        if (squad.ownAi || !guerrilla_vehicles::Running()) return;
        if (squad.vehicle == kNone) squad.vehicle = attack_squads::VehicleOf(squad, members);
        if (squad.vehicle == kNone) return;
        BYTE* vehicle = static_cast<BYTE*>(ObjectFromHandle(squad.vehicle));
        if (!vehicle) return;
        const float away = sqrtf(FlatDistance2(PositionOf(vehicle), PositionOf(target)));
        const int now = GameTime();
        if (squad.chased) {
            if (GuerrillaVehicles_Chasing(squad.vehicle)) return;
            squad.chased = false;
            // Not handed again at once: a crew that stops to get out and fight (the chase's rule near a target on foot)
            // still sits in its seats for a moment, and a chase handed then ends the next frame (2026-11-20: in pairs
            // 10 ms apart). By then it is out, or the chase lost it farther off and may take it again.
            squad.handAgainAt = now + kHandAgainMs;
            RFA_LOG_FIRST(24, "Continuous attack %s: %s's chase ended by itself, %.0f m from the target", row_->name.c_str(), squad.what.c_str(), away);
        }
        if (now < squad.handAgainAt) return;
        if (VehicleSubtype(vehicle, nullptr) != kAutomobile) {
            squad.ownAi = true;
            RFA_LOG_FIRST(24, "Continuous attack %s: %s keeps its own AI (not a car)", row_->name.c_str(), squad.what.c_str());
            return;
        }
        if (away > kChaseReach) {
            if (std::find(farTold_.begin(), farTold_.end(), squad.squad) == farTold_.end()) {  // once a squad
                farTold_.push_back(squad.squad);
                if (farTold_.size() > 64) farTold_.erase(farTold_.begin());
                RFA_LOG_FIRST(24, "Continuous attack %s: %s's car is %.0f m from the target, beyond the chase's %.0f m: not handed over yet", row_->name.c_str(),
                              squad.what.c_str(), away, kChaseReach);
            }
            return;
        }
        squad.chased = GuerrillaVehicles_ChaseWithin(squad.vehicle, target_, kChaseReach);  // false (no driver seated yet...): tried again later
        if (squad.chased) RFA_LOG_FIRST(24, "Continuous attack %s: %s handed to the chase driver, %.0f m from the target", row_->name.c_str(), squad.what.c_str(), away);
    }

    void Clear() {
        target_ = kNone;
        row_ = nullptr;
        count_ = 0;
        subGroup_ = 0;
        spawnAt_ = -1;
        chasedAt_ = 0;
        noRegion_ = false;
        targetAlive_ = true;
        farTold_.clear();
        g_consoleLayer.clear();
    }

    unsigned target_ = kNone;
    bool noRegion_ = false;     // the last try found no spawn region (logged once until one is found)
    bool targetAlive_ = true;   // the target was alive at the last frame (logged at each change)
    const Row* row_ = nullptr;
    int count_ = 0, subGroup_ = 0;
    int spawnAt_ = -1;  // the spawn timer (the game's clock), -1 not set
    int chasedAt_ = 0;
    std::vector<attack_squads::Squad> squads_;
    std::vector<DWORD> farTold_;  // squads whose car was said to be beyond the chase's reach
};

ContinuousAttackActivity* g_attack = nullptr;

// The activity layer a row belongs to: the longest activity name in the spawn regions' layers it begins with
// (je_ad_01_01 → je_ad_01, ls_ad_03a → ls_ad_03, je_td_03 → je_td_03), "" for none.
std::string RowLayer(const std::string& row) {
    std::string best;
    for (const Region& r : g_regions)
        if (r.layer != "-" && r.layer.size() > best.size() && _strnicmp(row.c_str(), r.layer.c_str(), r.layer.size()) == 0) best = r.layer;
    return best;
}

void StartCommand() {
    if (!g_commandRunning) return;
    ParseArguments(2, 0);
    const std::string name = g_commandWord;
    const Row* row = RowNamed(name.c_str());
    void* player = Player();
    if (!row || !player || !g_attack) {
        ConsolePrint("rfg_ca_start: %s\n", !row ? "no such row in activity_continuous_attack.xtbl" : "no player");
        return;
    }
    ReadRegions();
    const std::string layer = RowLayer(row->name);
    if (!g_attack->Begin(HandleOf(static_cast<BYTE*>(player)), row, "the console")) {
        ConsolePrint("rfg_ca_start: %s didn't start (see logs\\rfa_mods.log)\n", row->name.c_str());
        return;
    }
    g_consoleLayer = layer;  // for this run (its first squad comes at its next frame); cleared as it ends
    rfa::Log("rfg_ca_start %s: at the player%s", row->name.c_str(), layer.empty() ? "" : (", with " + layer + "'s spawn regions counted as loaded").c_str());
    ConsolePrint("rfg_ca_start: %s started at you\n", row->name.c_str());
}

void EndCommand() {
    if (!g_commandRunning || !g_attack) return;
    g_attack->Stop("the console's rfg_ca_end");
    ConsolePrint("rfg_ca_end: done\n");
}

}  // namespace

Activity* MakeContinuousAttack() {
    LoadRows();
    static ContinuousAttackActivity attack;
    g_attack = &attack;
    return &attack;
}

bool ContinuousAttackStart(unsigned target, const char* row, const char* who) {
    if (!g_attack) return false;
    const Row* found = RowNamed(row);
    if (!found) {
        rfa::Log("Continuous attack (%s): no row %s in activity_continuous_attack.xtbl", who, row ? row : "(none)");
        return false;
    }
    return g_attack->Begin(target, found, who);
}
void ContinuousAttackEnd(const char* why) {
    if (g_attack) g_attack->Stop(why);
}
void ContinuousAttackSetCount(int count) {
    if (g_attack) g_attack->SetCount(count);
}
void ContinuousAttackChangeCount(int change) {
    if (g_attack) g_attack->ChangeCount(change);
}
void ContinuousAttackSetSubGroup(int group) {
    if (g_attack) g_attack->SetSubGroup(group);
}

}  // namespace campaign::activities
