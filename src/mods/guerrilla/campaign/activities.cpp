// Guerrilla's activity framework, as its code runs it.
//
// Each kind of activity is one object (Guerrilla's Activities[17], indexed by activity_modes). A
// run is a node object (a demolitions master node, a raid node, ...) plus a row of activity.xtbl
// (base_activity_data: layers, scripts, voice, music, alert caps) plus the kind's own row. Each
// frame (activity_system_do_frame) every kind takes one step of its state machine: WAIT → LOADING
// (layers, voice, objects restored) → READY (offered) → STARTING → RUNNING → SHUTDOWN → WAIT, one
// virtual function per state; the kind's own logic decides when it starts, wins and loses.
//
// Modal activities (demolitions master, house arrest, raids, riding shotgun, Guerrilla Express,
// courier, convoy, area defense, tanks a lot) run one at a time: starting one kills the others
// that aren't waiting or shutting down; after one ends, no other is offered for 180 s plus 60 s
// for each one offered in the district, at most 600 s. Free ones (mayhem, killing spree,
// continuous attack, ...) run alongside. At alert ORANGE or RED every offered modal activity is
// withdrawn. Disabling (by a mission, a script, the console, ...) kills a run that is offered or
// running. A state change tells the scripts (activity_status_change). A run in progress isn't
// saved (Guerrilla warns when saving during one); each node keeps its own progress.
//
// The free kinds start themselves and show in the diversions panel (killing spree so far): Guerrilla's
// damage listeners hear every death (ReportDeath), and "the player's doing" is its own test
// (PlayerControlled).
//
// Natural spawning (natural_spawning.cpp): every 4 to 6 s a convoy (convoy.cpp) or a courier may be offered near the
// player, and activity_spawn.xtbl's defensive raids once they are ported. Not ported yet: the script groups, the voice
// and music, the vehicle and human reservations.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "campaign_api.h"
#include "kiosks/kiosks_api.h"
#include "activity_base.h"
#include "handle_kinds.h"
#include "liberation.h"
#include "managed_layers.h"
#include "script_words.h"
#include "tables.h"
#include "world_objects.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <cstddef>
#include <algorithm>
#include <iterator>
#include <unordered_set>

namespace campaign::activities {
namespace {

Activity* g_activities[kModes];
Mode g_current = ModeNone;   // Current_activity_mode
float g_cooldown = 0.0f;     // seconds before a modal activity may be offered again
std::vector<BaseData> g_baseData;

// Guerrilla's tweaks Activity_cooldown_base_period_ms and _per_ready_ms, and its cap.
constexpr float kCooldownBase = 180.0f, kCooldownPerReady = 60.0f, kCooldownMax = 600.0f;
// Interface states fade back to none 10 s after they change (activity_do_frame, WAIT).
constexpr float kInterfaceHold = 10.0f;
// A modal run stays in SHUTDOWN at least this long (activity_base_modal::activity_clean_up_once).
constexpr float kShutdownPeriod = 1.0f;
constexpr float kLayersWaitMost = 20.0f;  // a run's layers not in by then: it goes on without them

int AlertCapFrom(const std::string& name, int fallback) {
    static const char* const kNames[] = {"green", "yellow", "orange", "red"};
    for (int i = 0; i < 4; ++i)
        if (_stricmp(name.c_str(), kNames[i]) == 0) return i;
    return fallback;
}

void LoadBaseData() {
    std::vector<tables::Element> rows;
    if (!tables::Load("activity", rows)) return;
    for (const tables::Element& row : rows) {
        BaseData data;
        data.name = row.Text("Name");
        data.controlLimit = row.Number("control_limit", 1.0f);
        if (const tables::Element* groups = row.Child("script_groups"))
            for (const tables::Element* group : groups->Children("script_group_name")) data.scriptGroups.push_back(group->text);
        if (const tables::Element* layers = row.Child("managed_layer_info"))
            for (const tables::Element& layer : layers->children) data.layers.push_back(layer.text);
        data.minAlertCap = AlertCapFrom(row.Text("min_alert_cap"), 0);
        data.maxAlertCap = AlertCapFrom(row.Text("max_alert_cap"), 3);
        if (const tables::Element* voice = row.Child("voice_info")) data.voice = voice->Text("voice_type");
        if (const tables::Element* music = row.Child("music_info")) {
            data.music = music->Text("music_intensity");
            data.specialScore = music->Text("special_score");
        }
        data.disabled = row.Flag("disabled");
        data.allowGuerrillaReinforcement = row.Flag("guerrilla_reinforcement");
        g_baseData.push_back(data);
    }
    rfa::LogDetail("activity.xtbl: %u activities", static_cast<unsigned>(g_baseData.size()));
}

int ReadyCount() {
    int ready = 0;
    for (Activity* activity : g_activities)
        if (activity && activity->Modal() && activity->GetState() == State::Ready) ++ready;
    return ready;
}

// ---- Armageddon's objects, for "the player's doing" (PlayerControlled) and the deaths reported
//
// An object's handle +0x68, its type +0x7A (both games' object_type: 0 a human, 1 an item, 2 a mover, 3 a vehicle,
// 6 a turret), a human's vehicle +0x1160 (-1 none) and team +0x13B4, an item's owner +0x164 (gear's reading), a
// turret's gunner +0x9C (guerrilla_vehicles' turrets.cpp); the world's people (0x43A860 how many, 0x43A8B0 each),
// whether one drives the vehicle he is in (0x8695D0), how one team takes another (0x8A0460: the first team's
// disposition towards the second, Guerrilla's Current_human_team_dispositions[team][other]).
void* const kWorld = reinterpret_cast<void*>(0x1870630);
using CountFn = int(__fastcall*)(void* world, void* unused);
using AtFn = BYTE*(__fastcall*)(void* world, void* unused, int index);
auto const HumanCount = reinterpret_cast<CountFn>(0x43A860);
auto const HumanAt = reinterpret_cast<AtFn>(0x43A8B0);
auto const Driving = reinterpret_cast<bool(__cdecl*)(BYTE* human)>(0x8695D0);
auto const Disposition = reinterpret_cast<int(__cdecl*)(int team, int other)>(0x8A0460);
constexpr DWORD kHandle = 0x68, kObjectType = 0x7A, kInVehicle = 0x1160, kTeam = 0x13B4, kItemOwner = 0x164, kGunner = 0x9C;
constexpr int kHumanType = 0, kItemType = 1, kVehicleType = 3, kTurretType = 6;
constexpr DWORD kNoHandle = 0xFFFFFFFF;
// Guerrilla's vehicle::most_recent_driver and most_recent_driver_exit_time: a vehicle counts as the player's for
// 40 s after he left it (activity_register_damage_is_player_controlled's 40000 ms).
constexpr int kLeftVehicleMs = 40000;
DWORD g_lastVehicle = kNoHandle;  // the vehicle the player drove last (Armageddon has no most-recent-driver field)
int g_leftVehicleAt = -1;         // when he got out of it (game clock), -1 while he is in it

DWORD HandleOf(const void* object) { return *reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(object) + kHandle); }
int TypeOf(const void* object) { return static_cast<signed char>(static_cast<const BYTE*>(object)[kObjectType]); }

// Who drives the vehicle now (a person in it, driving), other than the player: Guerrilla's most recent driver changes.
bool SomeoneElseDrives(DWORD vehicle, const BYTE* player) {
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (human && human != player && *reinterpret_cast<const DWORD*>(human + kInVehicle) == vehicle && Driving(human)) return true;
    }
    return false;
}

// Each frame: the vehicle the player is in, and when he left the last one.
void FollowPlayersVehicle() {
    const BYTE* player = static_cast<const BYTE*>(Player());
    if (!player) return;
    const DWORD vehicle = *reinterpret_cast<const DWORD*>(player + kInVehicle);
    if (vehicle != kNoHandle) {
        g_lastVehicle = vehicle;
        g_leftVehicleAt = -1;
    } else if (g_lastVehicle != kNoHandle && g_leftVehicleAt < 0) {
        g_leftVehicleAt = GameTime();
    }
}

std::unordered_set<DWORD> g_deathsReported;  // each death once (Armageddon may call its human death again)

// The marks (register_mark_object RM 0x770510 into two hash tables, 0x1651370 / 0x1651374, swapped every 20 s by
// activity_system_do_frame): a hit of 100 or more with the player's doing on either side marks both objects, which
// then count as the player's until the set they are in is dropped, 20 to 40 s later.
constexpr float kMarkDamage = 100.0f;
constexpr int kMarksSwapMs = 20000;
std::unordered_set<DWORD> g_marks, g_oldMarks;
int g_marksSwappedAt = -1;

bool Marked(const void* object) {
    const DWORD handle = HandleOf(object);
    return g_marks.count(handle) || g_oldMarks.count(handle);
}

void FollowMarks() {
    const int now = GameTime();
    if (g_marksSwappedAt < 0 || now - g_marksSwappedAt >= kMarksSwapMs || now < g_marksSwappedAt) {
        g_oldMarks.swap(g_marks);
        g_marks.clear();
        g_marksSwappedAt = now;
    }
}

// ---- The diversions panel (hud_ui_missions_determine_diversion_items RM 0x8C5490 and process_diversion_info
// 0x8C56A0): each free kind but mission vrail and continuous attack whose interface is RUNNING is listed (8 at most,
// farray<diversion_data,8>); a listed one that stops running shows ENDED, then goes once its interface state is more
// than 1 s old. Each frame every item is worked out again in list order (its place, which_one, its index + 1).
struct Diversion {
    Mode mode = ModeNone;
    bool ended = false;
    std::string value;  // its value text, kept from frame to frame (Guerrilla's item buffer +0x8C)
    int id = 0;         // ours, for the HUD to tell items apart
};
std::vector<Diversion> g_diversions;
int g_nextDiversionId = 1;
constexpr size_t kMostDiversions = 8;
constexpr float kDiversionShift = 1.0f;     // the first second shows STARTED; ENDED shows a second before it goes
constexpr int kDiversionFlashMs = 6000;     // Fade_diversion_time_ms: the icon flashes under 6 s left

void FollowDiversions() {
    for (size_t i = 0; i < g_diversions.size();) {
        Activity* activity = Get(g_diversions[i].mode);
        if (!activity || !activity->IsActive()) {
            if (!activity || activity->InterfaceTime() > kDiversionShift) {
                g_diversions.erase(g_diversions.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            g_diversions[i].ended = true;
        } else {
            g_diversions[i].ended = false;
        }
        ++i;
    }
    for (Activity* activity : g_activities) {
        if (!activity || activity->Modal() || activity->GetMode() == MissionVrail || activity->GetMode() == ContinuousAttack) continue;
        if (activity->GetInterface() != Interface::Running) continue;
        bool listed = false;
        for (const Diversion& item : g_diversions) listed = listed || item.mode == activity->GetMode();
        if (listed || g_diversions.size() >= kMostDiversions) continue;
        Diversion item;
        item.mode = activity->GetMode();
        item.id = g_nextDiversionId++;
        g_diversions.push_back(item);
    }
}

// ---- The game's console: a command to try an activity before the game can offer it.

// A command object registered with 0xC7BD70 (thiscall: the object, name, description, type,
// handler); type 3 runs its handler, cdecl with no arguments, which reads its own arguments: with
// 0x28FF1C0 set it is being run (not asked for help), 0xC7D570(2, 0) reads a word into 0x28FF0B8.
// As the game's own "scriptx" command does (0x915890).
using RegisterCommandFn = void*(__fastcall*)(void* command, void* unused, const char* name, const char* description,
                                            int type, void (*handler)());
auto const RegisterCommand = reinterpret_cast<RegisterCommandFn>(0xC7BD70);
using ParseArgumentsFn = void(__cdecl*)(int kinds, int flags);
auto const ParseArguments = reinterpret_cast<ParseArgumentsFn>(0xC7D570);
auto& g_commandRunning = *reinterpret_cast<int*>(0x28FF1C0);
auto const g_commandWord = reinterpret_cast<const char*>(0x28FF0B8);
BYTE g_startCommand[32];
BYTE g_spawnCommand[32];

using ConsolePrintFn = void(__cdecl*)(const char* format, ...);
auto const ConsolePrint = reinterpret_cast<ConsolePrintFn>(0xC7C440);

void StartCommand() {
    if (!g_commandRunning) return;
    ParseArguments(2, 0);
    if (StartDemolitionsMaster(g_commandWord)) return;
    // Why not, in the console too: Guerrilla's demolitions_master_can_be_started (no activity running,
    // the activity's cooldown over, the alert green).
    char why[160];
    if (AnyRunning()) sprintf_s(why, "an activity is running");
    else if (g_cooldown > 0.0f) sprintf_s(why, "the activities' cooldown, %.0f s more (Guerrilla's: 180 s after one ends)", g_cooldown);
    else if (liberation::AlertLevel() != handles::AlertGreen) sprintf_s(why, "the alert level isn't green");
    else sprintf_s(why, "see logs\\rfa_mods.log");
    rfa::Log("rfg_dm_start: couldn't start \"%s\": %s", g_commandWord, why);
    ConsolePrint("rfg_dm_start: couldn't start \"%s\": %s\n", g_commandWord, why);
}

}  // namespace

// ---- Activity

bool Activity::StartSpawn(unsigned node, const BaseData* data, int district) {
    if (node == 0xFFFFFFFF || state_ != State::Wait || !CanInitialize()) return false;
    Reset();
    node_ = node;
    data_ = data;
    district_ = district;
    ChangeState(State::Loading);
    // Its layers start loading (managed_layer_set::start_layer_loading), named as the activity's table row.
    layersRequested_ = data && !data->layers.empty() && managed_layers::Request(data->name.c_str());
    return true;
}

void Activity::Reset() {
    state_ = State::Wait;
    interface_ = Interface::None;
    node_ = 0xFFFFFFFF;
    data_ = nullptr;
    cleanedUp_ = victoryRecorded_ = active_ = false;
    shutdownTime_ = 0.0f;
    layersRequested_ = false;
}

void Activity::LevelInit() {
    managed_layers::LevelInit();
    disabled_ = 0;
    Reset();
    if (modal_ && g_current == mode_) g_current = ModeNone;
}

bool Activity::CanInitialize() const {
    if (disabled_ || (state_ != State::Wait && state_ != State::Loading)) return false;
    if (!RestrictedGroupAllows(mode_)) return false;
    if (!modal_) return true;
    // Modal: after the cooldown; at RED only Guerrilla Express.
    if (g_cooldown > 0.0f) return false;
    return liberation::AlertLevel() < handles::AlertRed || mode_ == Delivery;
}

bool Activity::IsActive() const { return modal_ ? g_current == mode_ : active_; }

bool Activity::Initialize() {
    if (!CanInitialize()) return false;
    // activity_base_modal::activity_initialize (RM 0x7960C0): Last_activity_spawned, the node the GPS key goes to.
    if (modal_) SetLastSpawned(node_, mode_);
    ChangeInterface(Interface::Ready);
    ChangeState(State::Ready);
    return true;
}

bool Activity::BeginStartUp() {
    if (state_ != State::Ready) return false;
    if (modal_)
        for (Activity* other : g_activities)
            if (other && other != this && other->modal_ && other->state_ != State::Wait && other->state_ != State::Shutdown) other->Kill();
    ChangeState(State::Starting);
    return true;
}

bool Activity::Start() {
    if (modal_) {
        if (g_current != ModeNone && g_current != mode_) return false;
        g_current = mode_;
    } else {
        active_ = true;
    }
    if (data_) liberation::SetAlertCap(data_->minAlertCap, data_->maxAlertCap);
    ChangeInterface(Interface::Running);
    ChangeState(State::Running);
    // Its "At Start" layers' objects made (activity_base::create_managed_layer), behind the start's fade.
    if (layersRequested_ && data_) managed_layers::Create(data_->name.c_str());
    return true;
}

void Activity::End() {
    if (state_ == State::Wait || state_ == State::Shutdown) return;
    if (interface_ != Interface::Victory && interface_ != Interface::Loss) ChangeInterface(Interface::Killed);
    if (data_) liberation::SetAlertCap(handles::AlertGreen, handles::AlertRed);
    if (modal_) {
        if (g_current == mode_) g_current = ModeNone;
        StartCooldown();
        SpawnModalEnded();
    } else {
        active_ = false;
    }
    cleanedUp_ = false;
    ChangeState(State::Shutdown);
}

void Activity::CleanUpOnce() {
    cleanedUp_ = true;
    shutdownTime_ = modal_ ? kShutdownPeriod : 0.0f;
}

void Activity::FrameCleanUp(float seconds) {
    if (!cleanedUp_) CleanUpOnce();
    shutdownTime_ -= seconds;
}

bool Activity::CleanUpCompleted() const { return cleanedUp_ && shutdownTime_ <= 0.0f; }

// Back to WAIT; the scripts are told for the node that just ended, and the result stays on show.
void Activity::Complete() {
    // Its layers' objects taken away and their files let go (start_layer_unloading), after its own clean-up (a
    // challenge's weapons given back first, so a propane tank in the player's hands goes with them).
    if (layersRequested_ && data_) managed_layers::Release(data_->name.c_str());
    const Interface shown = interface_;
    const float shownTime = interfaceTime_;
    const unsigned node = node_;
    Reset();
    interface_ = shown;  // (for kInterfaceHold)
    interfaceTime_ = shownTime;
    node_ = node;
    ChangeState(State::Wait);
    node_ = 0xFFFFFFFF;
}

void Activity::RecordVictory(const Rewards& rewards) {
    if (victoryRecorded_) return;
    victoryRecorded_ = true;
    ChangeInterface(Interface::Victory);
    // apply_standard_rewards: EDF control down, morale up, salvage, in the run's district.
    if (rewards.control != 0.0f) liberation::ChangeEdfControl(-rewards.control, district_);
    if (rewards.morale != 0.0f) liberation::ChangeMorale(rewards.morale, district_);
    if (rewards.salvage != 0) liberation::AddSalvage(rewards.salvage);
    // kiosk_unlock_from_activity (its only caller is record_victory, RM 0x755BB9): the EDF news lines this win unlocks
    // (kiosk_lines.xtbl's, by the activity's name; guerrilla_kiosks keeps them).
    if (data_)
        if (const int unlocked = GuerrillaKiosks_UnlockActivity(data_->name.c_str()); unlocked > 0)
            rfa::LogDetail("Activities: %s won: %d kiosk lines unlocked", data_->name.c_str(), unlocked);
}

void Activity::RecordLoss() { ChangeInterface(Interface::Loss); }

bool Activity::StartFree() {
    if (modal_) return false;
    // activity_base_free::activity_start: unless it was initialized already (offered or running), it must be able to
    // initialize, then start_activity_spawn(no node, the player's district) and activity_base::activity_start.
    if (!WasInitialized()) {
        if (!CanInitialize()) return false;
        Reset();
        district_ = liberation::PlayerDistrict();
    }
    return Start();
}

void Activity::ChangeState(State state) {
    state_ = state;
    stateTime_ = 0.0f;
    if (node_ != 0xFFFFFFFF) words::PostActivityStatusChange(node_, static_cast<int>(state));
}

void Activity::ChangeInterface(Interface state) {
    interface_ = state;
    interfaceTime_ = 0.0f;
}

void Activity::Disable(DisableReason reason) {
    if (reason == Everything) return;
    disabled_ |= 1u << reason;
    if (state_ == State::Ready || state_ == State::Running) Kill();
}

void Activity::Enable(DisableReason reason) { disabled_ = reason == Everything ? 0 : disabled_ & ~(1u << reason); }

void Activity::DoFrame(float seconds) {
    stateTime_ += seconds;
    interfaceTime_ += seconds;
    if (disabled_) {
        if (state_ == State::Ready || state_ == State::Running) Kill();
        if (state_ != State::Shutdown) return;
    }
    switch (state_) {
    case State::Wait:
        FrameInactive(seconds);
        if (interface_ != Interface::None && interfaceTime_ > kInterfaceHold) ChangeInterface(Interface::None);
        break;
    case State::Loading:
        // Guerrilla waits here for the activity's layers (layer_resources_loaded), voice, restored objects and
        // reserved vehicles; of those only the layers are ported. Not in after kLayersWaitMost: on without them.
        if (layersRequested_ && data_ && !managed_layers::Loaded(data_->name.c_str())) {
            if (stateTime_ < kLayersWaitMost) break;
            rfa::Log("Activities: %s's layers didn't load in %.0f s; the run goes on without them", data_->name.c_str(), kLayersWaitMost);
            managed_layers::Release(data_->name.c_str());
            layersRequested_ = false;
        }
        if (!Initialize()) Kill();
        break;
    case State::Ready:
        // activity_base::activity_do_frame_ready (RM 0x7697D0), which every kind's own ready frame calls first: the hint 5 s
        // after a natural spawn.
        OfferHintFrame();
        FrameReady(seconds);
        break;
    case State::Starting:
        FrameStarting(seconds);
        if (state_ == State::Starting && StartingCompleted() && !Start()) Kill();
        break;
    case State::Running:
        FrameActive(seconds);
        break;
    case State::Shutdown:
        FrameCleanUp(seconds);
        if (CleanUpCompleted()) Complete();
        break;
    }
}

// ---- The framework

Activity* Get(Mode mode) { return mode > ModeNone && mode < kModes ? g_activities[mode] : nullptr; }
void Register(Activity* activity) { g_activities[activity->GetMode()] = activity; }
Mode CurrentMode() { return g_current; }
void SetCurrentMode(Mode mode) { g_current = mode; }

void StartCooldown() {
    const float cooldown = kCooldownBase + kCooldownPerReady * ReadyCount();
    g_cooldown = cooldown > kCooldownMax ? kCooldownMax : cooldown;
}

void ClearCooldown() { g_cooldown = 0.0f; }
float CooldownLeft() { return g_cooldown; }

const BaseData* FindBaseData(const std::string& name) {
    for (const BaseData& data : g_baseData)
        if (_stricmp(data.name.c_str(), name.c_str()) == 0) return &data;
    return nullptr;
}

bool PlayerControlled(const void* object) {
    const BYTE* player = static_cast<const BYTE*>(Player());
    if (!object || !player) return false;
    if (Marked(object)) return true;
    const DWORD playerHandle = HandleOf(player);
    switch (TypeOf(object)) {
    case kHumanType:
        return object == player;
    case kItemType:
        return *reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(object) + kItemOwner) == playerHandle;
    case kVehicleType: {
        const DWORD vehicle = HandleOf(object);
        if (*reinterpret_cast<const DWORD*>(player + kInVehicle) == vehicle) return true;
        return vehicle == g_lastVehicle && g_leftVehicleAt >= 0 && GameTime() - g_leftVehicleAt < kLeftVehicleMs &&
               !SomeoneElseDrives(vehicle, player);
    }
    case kTurretType:
        return *reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(object) + kGunner) == playerHandle;
    default:
        return false;
    }
}

int DispositionToPlayer(const void* human) {
    const BYTE* player = static_cast<const BYTE*>(Player());
    if (!human || !player) return 2;  // TEAM_DISPOSITION_NONE
    return Disposition(*reinterpret_cast<const int*>(static_cast<const BYTE*>(human) + kTeam), *reinterpret_cast<const int*>(player + kTeam));
}

// ---- The reports: posted from any thread, told on the main thread
//
// Armageddon runs some of its damage and destruction in its physics jobs, on other threads, so each report is
// posted with the objects' handles and told to the listeners at the next frame of the activities (Update), the
// objects looked up again by handle (gone by then: left out). Guerrilla told them at once; a frame later changes
// nothing its rules read.
enum class Posted { Damage, Death, BuildingDamage, BuildingDestroyed, Explosion, Mark };
struct Report {
    Posted kind;
    DWORD a, b;    // the objects' handles (kNoHandle: none)
    float x, y;    // the damage; a building's change and its damage before
};
CRITICAL_SECTION g_reportsLock;
bool g_reportsLockMade = false;
std::vector<Report> g_reports;
constexpr size_t kMostReports = 4096;  // a frame's at most (more are left out, said once a level)
bool g_reportsFullSaid = false;

DWORD HandleOrNone(const void* object) { return object ? HandleOf(object) : kNoHandle; }

void Post(Posted kind, const void* a, const void* b, float x = 0.0f, float y = 0.0f) {
    if (!g_reportsLockMade || !Playing()) return;
    EnterCriticalSection(&g_reportsLock);
    if (g_reports.size() < kMostReports) g_reports.push_back({kind, HandleOrNone(a), HandleOrNone(b), x, y});
    else if (!g_reportsFullSaid) {
        g_reportsFullSaid = true;
        rfa::Log("Activities: more than %u damage reports in a frame; the rest left out this frame", static_cast<unsigned>(kMostReports));
    }
    LeaveCriticalSection(&g_reportsLock);
}

// The listeners (Guerrilla's table of four): each told while it isn't disabled or suspended, running or not.
template <typename Tell>
void TellListeners(Tell tell) {
    for (Activity* activity : g_activities)
        if (activity && activity->Listens() && !activity->Disabled()) tell(*activity);
}

void* ByHandle(DWORD handle) { return handle != kNoHandle ? ObjectFromHandle(handle) : nullptr; }

void TellReports() {
    std::vector<Report> reports;
    if (!g_reportsLockMade) return;
    EnterCriticalSection(&g_reportsLock);
    reports.swap(g_reports);
    LeaveCriticalSection(&g_reportsLock);
    for (const Report& report : reports) {
        switch (report.kind) {
        case Posted::Mark:  // activity_register_damage_mark_objects RM 0x799C60: both, when either is the player's doing
            if (PlayerControlled(ByHandle(report.a)) || PlayerControlled(ByHandle(report.b))) {
                if (report.a != kNoHandle) g_marks.insert(report.a);
                if (report.b != kNoHandle) g_marks.insert(report.b);
            }
            break;
        case Posted::Damage: {
            // activity_register_damage RM 0x799CA0: both marked when the damage is 100 or more and either is the
            // player's doing; then the listeners.
            void* victim = ByHandle(report.a);
            void* attacker = ByHandle(report.b);
            if (!victim && !attacker) break;
            if (report.x >= kMarkDamage && (PlayerControlled(victim) || PlayerControlled(attacker))) {
                if (report.a != kNoHandle) g_marks.insert(report.a);
                if (report.b != kNoHandle) g_marks.insert(report.b);
            }
            TellListeners([&](Activity& activity) { activity.RegisterDamage(victim, attacker, report.x); });
            break;
        }
        case Posted::Death: {
            // activity_register_death: from human_died (non-player humans), vehicle_blow_up and a prop's damage.
            void* victim = ByHandle(report.a);
            if (!victim || victim == Player() || !g_deathsReported.insert(report.a).second) break;
            void* killer = ByHandle(report.b);
            TellListeners([&](Activity& activity) { activity.RegisterDeath(victim, killer); });
            break;
        }
        case Posted::BuildingDamage:
            if (void* building = ByHandle(report.a))
                TellListeners([&](Activity& activity) { activity.RegisterBuildingDamage(building, report.x, report.y); });
            break;
        case Posted::BuildingDestroyed:
            if (void* building = ByHandle(report.a)) TellListeners([&](Activity& activity) { activity.RegisterBuildingDestroyed(building); });
            break;
        case Posted::Explosion: {
            void* owner = ByHandle(report.a);
            TellListeners([&](Activity& activity) { activity.RegisterExplosion(owner); });
            break;
        }
        }
    }
}

void MarkObjects(const void* a, const void* b) { Post(Posted::Mark, a, b); }
void ReportDamage(void* victim, void* attacker, float damage) { Post(Posted::Damage, victim, attacker, damage); }
void ReportDeath(void* victim, void* killer) { Post(Posted::Death, victim, killer); }
void ReportBuildingDamage(void* building, float damage, float before) { Post(Posted::BuildingDamage, building, nullptr, damage, before); }
void ReportBuildingDestroyed(void* building) { Post(Posted::BuildingDestroyed, building, nullptr); }
void ReportExplosion(void* owner) { Post(Posted::Explosion, owner, nullptr); }

void MissionActivated(int missionMode) {
    constexpr int kTutorial = 4;  // MISSION_MODE_TUTORIAL
    if (missionMode != kTutorial) return;
    // mission_activate_post_save: mayhem killed (when nothing disables it yet) and disabled for good (ADR_MISSION).
    if (Activity* mayhem = Get(Mayhem)) {
        rfa::LogDetail("Activities: the Tutorial mission switches mayhem off for good, as Guerrilla's");
        if (!mayhem->Disabled()) mayhem->KillNow();
        mayhem->Disable(ByMission);
    }
}

void Init() {
    managed_layers::Init();
    LoadBaseData();
    Register(MakeDemolitionsMaster());
    Register(MakeDelivery());
    Register(MakeKillingSpree());
    Register(MakeMayhem());
    Register(MakeCarBomb());
    Register(MakeContinuousAttack());
    Register(MakeAreaDefense());
    Register(MakeConvoy());
    InitializeCriticalSection(&g_reportsLock);
    g_reportsLockMade = true;
    StartDamageFeeds();
}

void Enter() {
    static bool commandAdded;
    if (!commandAdded) {
        commandAdded = true;
        RegisterCommand(g_startCommand, nullptr, "rfg_dm_start", "Starts one of Guerrilla's demolitions master challenges (je_dm_01..16)", 3,
                        &StartCommand);
        RegisterCommand(g_spawnCommand, nullptr, "rfg_act_spawn",
                        "Runs Guerrilla's natural activity spawner once now (a convoy or courier near you), saying what stopped it", 3, &SpawnCommand);
    }
    SpawnLevelInit();
    g_cooldown = 0.0f;
    g_lastVehicle = kNoHandle;
    g_leftVehicleAt = -1;
    g_deathsReported.clear();
    g_diversions.clear();
    g_marks.clear();
    g_oldMarks.clear();
    g_marksSwappedAt = -1;
    if (g_reportsLockMade) {
        EnterCriticalSection(&g_reportsLock);
        g_reports.clear();
        g_reportsFullSaid = false;
        LeaveCriticalSection(&g_reportsLock);
    }
    for (Activity* activity : g_activities)
        if (activity) activity->LevelInit();
    CarBombEnter();
}

void Update(float seconds) {
    if (g_cooldown > 0.0f) g_cooldown -= seconds;
    FollowPlayersVehicle();
    FollowMarks();
    TellReports();
    // At ORANGE or RED, offered modal activities are withdrawn when none is running (Guerrilla
    // spares Guerrilla Express).
    if (liberation::AlertLevel() >= handles::AlertOrange && g_current == ModeNone)
        for (Activity* activity : g_activities)
            if (activity && activity->Modal() && activity->GetMode() != Delivery) activity->Withdraw();
    // Then, every 4 to 6 s, the natural spawner (do_activity_spawn_frame), before the kinds' own frames.
    SpawnFrame();
    for (Activity* activity : g_activities)
        if (activity) activity->DoFrame(seconds);
    CarBombFrame();
    FollowDiversions();
}

bool AnyRunning() {
    // Guerrilla's !activity_none(): a modal activity is current, or an offensive raid or a house
    // arrest is loading, offered or starting.
    if (g_current != ModeNone) return true;
    for (Mode mode : {RaidAttack, HouseArrest})
        if (Activity* activity = Get(mode))
            if (activity->GetState() >= State::Loading && activity->GetState() <= State::Starting) return true;
    return false;
}

bool NoneActive() { return !AnyRunning(); }

bool RestrictedGroupAllows(Mode mode) {
    static const Mode kGroup[] = {MissionVrail, Convoy, Courier, RaidDefend, RaidAttack};
    if (std::find(std::begin(kGroup), std::end(kGroup), mode) == std::end(kGroup)) return true;
    for (Mode other : kGroup)
        if (other != mode)
            if (const Activity* activity = Get(other); activity && (activity->WasInitialized() || activity->IsActive())) return false;
    return true;
}

bool AlertLocked() {
    for (Activity* activity : g_activities)
        if (activity && activity->LockAlertLevel()) return true;
    return false;
}

void Disable(DisableReason reason, bool free) {
    for (Activity* activity : g_activities)
        if (activity && activity->Modal() != free) activity->Disable(reason);
}

void DisableOffers(DisableReason reason) {
    for (Activity* activity : g_activities)
        if (activity && activity->Modal() && activity->GetMode() != Delivery && activity->GetMode() != DemolitionsMaster)
            activity->Disable(reason);
}

void Reinstate(DisableReason reason, bool free) {
    for (Activity* activity : g_activities)
        if (activity && activity->Modal() != free) activity->Enable(reason);
}

HudInfo Hud() {
    if (Activity* current = Get(g_current)) return current->Hud();
    return {};
}

}  // namespace campaign::activities

// ---- For the other mods (guerrilla_hud's minimap and world map).

// 1 while an activity or a mission runs (Guerrilla's !activity_none() || mission_is_active(): its
// minimap then hides the activities' starts, the safehouse and the missions' starts, and pins the
// important EDF buildings to the rim only when nothing runs). *activityMode: the current modal kind
// (Guerrilla's activity_modes: 11 demolitions master, 4 Guerrilla Express, ...; 0 none); *mission: 1
// while a mission runs (none is ported yet: 0). All 0 while the campaign's systems rest.
int GuerrillaCampaign_Running(int* activityMode, int* mission) {
    using namespace campaign;
    const bool playing = guerrilla_campaign::Running() && Playing();
    if (activityMode) *activityMode = playing ? static_cast<int>(activities::CurrentMode()) : 0;
    if (mission) *mission = 0;
    return playing && activities::AnyRunning() ? 1 : 0;
}

// 1 when the activity at a node is completed (its start icon turns grey): the node by Guerrilla's
// handle, as campaign_objects.xml has it. Only demolitions master keeps progress yet.
int GuerrillaCampaign_ActivityDone(unsigned node) {
    return guerrilla_campaign::Running() && campaign::activities::NodeDone(node) ? 1 : 0;
}

// The same by place, for a map icon that knows only where it is (map_icons.txt): the node within 2 m of
// (x, z) on the ground, or 0.
int GuerrillaCampaign_ActivityDoneAt(float x, float z) {
    if (!guerrilla_campaign::Running()) return 0;
    const campaign::world::Object* node = campaign::world::Near(x, z, 2.0f);
    return node && campaign::activities::NodeDone(node->handle) ? 1 : 0;
}

// ---- For guerrilla_hud: the running activity's screens, its answer, the use prompt, the minimap's rings.

// (Their structs, GuerrillaCampaignActivity and GuerrillaCampaignMapRing, are in campaign_api.h.)

namespace {
campaign::activities::Screen g_shown;  // what the last call gave (the texts point into it)
}  // namespace

int GuerrillaCampaign_Activity(GuerrillaCampaignActivity* info) {
    using namespace campaign;
    if (!info) return 0;
    memset(info, 0, sizeof(*info));
    if (!guerrilla_campaign::Running()) return 0;
    g_shown = {};
    if (!Playing()) return 0;
    // The running kind's: demolitions master's (briefing, corner panel, results), else Transporter's corner panel, else
    // area defense's (its corner panel, timer and a win's stamp), else the convoy's corner panel.
    if (!activities::DemolitionsScreen(g_shown) || g_shown.screen == 0) {
        g_shown = {};
        if (!activities::DeliveryScreen(g_shown) || g_shown.screen == 0) {
            g_shown = {};
            if (!activities::AreaDefenseScreen(g_shown) || g_shown.screen == 0) {
                g_shown = {};
                if (!activities::ConvoyScreen(g_shown) || g_shown.screen == 0) return 0;
            }
        }
    }
    info->mode = static_cast<int>(g_shown.mode);
    info->screen = g_shown.screen;
    info->title = g_shown.title.c_str();
    info->subtitle = g_shown.subtitle.c_str();
    info->body = g_shown.body.c_str();
    info->objective = g_shown.objective.c_str();
    info->timer = g_shown.timer.c_str();
    info->result = g_shown.result.c_str();
    info->failure = g_shown.failure.c_str();
    for (size_t i = 0; i < 4; ++i) {
        info->weapon[i] = i < g_shown.weapons.size() ? g_shown.weapons[i].c_str() : nullptr;
        info->ammo[i] = i < g_shown.ammo.size() ? g_shown.ammo[i] : -1;
    }
    info->timeLimit = g_shown.timeLimit;
    info->parTime = g_shown.parTime;
    info->bestMs = g_shown.bestMs;
    info->elapsedMs = g_shown.elapsedMs;
    info->status = g_shown.status;
    info->success = g_shown.success;
    info->proTime = g_shown.proTime;
    info->outOfRange = g_shown.outOfRange;
    info->salvage = g_shown.salvage;
    info->distance = g_shown.distance.c_str();
    info->count = g_shown.count.c_str();
    info->strength = g_shown.strength.c_str();
    return 1;
}

// The diversions panel's items now (hud_ui_missions_process_diversion_info RM 0x8C56A0), in list order: the header
// is the kind's title; while it runs, its text (activity_get_ui_text) and the icon flashing under 6 s left, and in its
// first second STARTED (MISS_DIVERSION_STARTED_FEMININE for killing spree and mayhem) fading in; a car bomb shows
// ACT_BOMB_INSTALLED in its first second and ACT_BOMB_ARMED while its text is "t", with no value; after it stops,
// ENDED (_FEMININE for those two; nothing new for a car bomb). Texts are upper-cased, as Guerrilla's.
namespace {
std::vector<std::string> g_diversionTexts;  // what the last call gave (the items' texts point into it)
}  // namespace

int GuerrillaCampaign_Diversions(GuerrillaCampaignDiversion* items, int most) {
    using namespace campaign::activities;
    if (!items || most <= 0 || !guerrilla_campaign::Running() || !campaign::Playing()) return 0;
    const int count = static_cast<int>(g_diversions.size()) < most ? static_cast<int>(g_diversions.size()) : most;
    g_diversionTexts.assign(static_cast<size_t>(count) * 2, std::string());
    for (int i = 0; i < count; ++i) {
        Diversion& item = g_diversions[static_cast<size_t>(i)];
        const Activity* activity = Get(item.mode);
        GuerrillaCampaignDiversion& out = items[i];
        out = {};
        out.id = item.id;
        out.whichOne = i + 1;
        out.type = item.mode == Mayhem ? 1 : item.mode == KillingSpree ? 3 : item.mode == CarBomb ? 5 : 0;  // LUA_DIVERSION_*
        const bool feminine = item.mode == KillingSpree || item.mode == Mayhem;
        std::string status;
        if (activity && activity->GetInterface() == Interface::Running) {
            const float shown = activity->InterfaceTime();
            if (shown <= 0.0f) item.value.clear();
            std::string text;
            int number = 0;
            if (activity->UiText(text, number)) {
                item.value = text;
                out.flash = number < kDiversionFlashMs ? 1 : 0;
            }
            if (item.mode == CarBomb) {
                if (shown < kDiversionShift) {
                    status = "ACT_BOMB_INSTALLED";
                    out.fadeIn = 1;
                }
                if (!item.value.empty() && item.value[0] == 't') status = "ACT_BOMB_ARMED";
                item.value.clear();
            } else if (shown < kDiversionShift) {
                status = feminine ? "MISS_DIVERSION_STARTED_FEMININE" : "MISS_DIVERSION_STARTED";
                out.fadeIn = 1;
            }
        } else if (item.mode != CarBomb) {
            status = feminine ? "MISS_DIVERSION_ENDED_FEMININE" : "MISS_DIVERSION_ENDED";
        }
        for (char& c : status) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        for (char& c : item.value) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        g_diversionTexts[static_cast<size_t>(i) * 2] = status;
        g_diversionTexts[static_cast<size_t>(i) * 2 + 1] = item.value;
        out.header = activity ? activity->Title() : "";
        out.status = g_diversionTexts[static_cast<size_t>(i) * 2].c_str();
        out.value = g_diversionTexts[static_cast<size_t>(i) * 2 + 1].c_str();
    }
    return count;
}

// The HUD's answer: the briefing, 1 accept or 0 cancel; the results, anything closes them.
void GuerrillaCampaign_ActivityChoose(int accept) {
    if (guerrilla_campaign::Running()) campaign::activities::DemolitionsChoose(accept);
}

// The use prompt at the start of one of Guerrilla's activities: its icon (Guerrilla's use_object_icon; 1 the
// default hand), 0 none. The use key itself the campaign reads.
int GuerrillaCampaign_UsePrompt() {
    using namespace campaign::activities;
    if (!guerrilla_campaign::Running() || !campaign::Playing()) return 0;
    if (const int icon = DemolitionsUsePrompt()) return icon;
    return AreaDefenseUsePrompt();
}

// The rings on the minimap's ground now (render_regions): demolitions master's play area, green (50, 200, 50, 80),
// red (200, 50, 50, 255) while the 5 s back into it are counting. How many were written.
int GuerrillaCampaign_MapRings(GuerrillaCampaignMapRing* rings, int most) {
    using namespace campaign;
    if (!rings || most <= 0 || !guerrilla_campaign::Running() || !Playing()) return 0;
    activities::Screen screen;
    // Area defense's ring while it runs (render_regions RM 0x76A020: add_decal of CIRCLE_FILLED_OUTLINE, Area_radius,
    // green 50, 200, 50, 150; it doesn't change while the player is out of it).
    Vector areaCenter{};
    float areaRadius = 0.0f;
    if (activities::AreaDefenseRing(areaCenter, areaRadius)) {
        rings[0] = {areaCenter.x, areaCenter.z, areaRadius, 50, 200, 50, 150, 0};
        return 1;
    }
    if (!activities::DemolitionsScreen(screen) || screen.screen != 2) {
        // Transporter's end (render_regions RM 0x76C780: add_decal of CIRCLE_FILLED_OUTLINE, End_Radius, 0, 0, 255, 192).
        activities::Screen delivery;
        if (!activities::DeliveryScreen(delivery) || delivery.screen != 2) return 0;
        rings[0] = {delivery.center.x, delivery.center.z, delivery.range, 0, 0, 255, 192, 0};
        return 1;
    }
    GuerrillaCampaignMapRing& ring = rings[0];
    ring = {screen.center.x, screen.center.z, screen.range, 50, 200, 50, 80, 0};
    if (screen.outOfRange) ring = {screen.center.x, screen.center.z, screen.range, 200, 50, 50, 255, 0};
    if (!screen.hasTarget || most < 2) return 1;
    // The target over it, in Guerrilla's colours for a demolitions master building (cartographer's 0x2BE5124 standing,
    // 0x2BE5130 down; set in 0x8801C0): Guerrilla tints the building's own shape, which our minimap doesn't draw, so
    // here a disc of its size.
    rings[1] = {screen.target.x, screen.target.z, screen.targetRadius, 148, 13, 0, 255, 0};
    if (screen.targetDown) rings[1] = {screen.target.x, screen.target.z, screen.targetRadius, 55, 55, 55, 255, 0};
    return 2;
}

// The player's death timer ran out (guerrilla_save, from Armageddon's game-over screen's start): Guerrilla's
// gamestate_gp_simulate_frame (RM 0x7EDB49) gave the death to the running activity, in this order: demolitions master
// (mode 11), raids (8, 9), house arrest (10), Collateral Damage (1), the Transporter (4), area defense (16); none: its
// death screen. Of those the Transporter and area defense take it here (demolitions master's still ends its run as the player dies: how
// Guerrilla brings its dead player back isn't found yet).
int GuerrillaCampaign_TakePlayerDeath() {
    using namespace campaign;
    if (!guerrilla_campaign::Running() || !Playing()) return 0;
    return activities::DeliveryTakePlayerDeath() || activities::AreaDefenseTakePlayerDeath() ? 1 : 0;
}

// guerrilla_save's reload at a spot runs again: a retry's start is set up (delivery.cpp).
void GuerrillaCampaign_ReloadedAtSpot() {
    if (!guerrilla_campaign::Running()) return;
    campaign::activities::DeliveryReopened("guerrilla_save's reload runs again");
    campaign::activities::AreaDefenseReopened("guerrilla_save's reload runs again");
}

// The campaign's map icons now (each activity's render_icons): a running Transporter's end point (RM 0x7977F0:
// cartographer::add_temporary_icon(the end node, 31 MAP_ELEMENT_GUERILLA_EXP_END_POINT, true, -1, 0, 0x77, 0x77, ...);
// its row: ui_map_icon_end_point, VisibleOnFSMap). (Its GPS marker, add_player_marker, comes with the GPS.)
int GuerrillaCampaign_MapIcons(GuerrillaCampaignIcon* icons, int most) {
    using namespace campaign;
    if (!icons || most <= 0 || icons[0].size < static_cast<int>(sizeof(GuerrillaCampaignIcon)) || !guerrilla_campaign::Running() || !Playing())
        return 0;
    int count = 0;
    activities::Screen delivery;
    if (activities::DeliveryScreen(delivery) && delivery.screen == 2) {
        GuerrillaCampaignIcon& icon = icons[count++];
        icon.x = delivery.center.x;
        icon.y = delivery.center.y;
        icon.z = delivery.center.z;
        icon.picture = "ui_map_icon_end_point";
        icon.placement = 1;
        icon.color = 0xFF007777;
        icon.onWorldMap = 1;
    }
    // Each kind's own (render_icons, while it is offered, starting or running): the convoy's.
    for (activities::Mode mode : {activities::Convoy}) {
        const activities::Activity* activity = activities::Get(mode);
        if (!activity || count >= most || !activity->WasInitialized()) continue;
        count += activity->MapIcons(icons + count, most - count);
    }
    return count;
}

int GuerrillaCampaign_ConvoyRoute(float* xz, int most) {
    using namespace campaign;
    if (!xz || most <= 0 || !guerrilla_campaign::Running() || !Playing()) return 0;
    return activities::ConvoyRoute(xz, most);
}

// set_gps_to_spawned_activity (RM 0x87E9F0): with no activity running (activity_none; a house arrest or offensive raid
// offered too, neither ported), the last naturally spawned node's map icon (its spawn object's temporary icon), while
// that kind's run is offered, starting or running.
int GuerrillaCampaign_GpsTarget(int forKey, float* x, float* y, float* z) {
    using namespace campaign;
    if (!guerrilla_campaign::Running() || !Playing()) return 0;
    if (forKey && !activities::NoneActive()) return 0;
    activities::Mode mode = activities::ModeNone;
    const unsigned node = activities::LastSpawnedNode(&mode);
    const activities::Activity* activity = node != 0xFFFFFFFF ? activities::Get(mode) : nullptr;
    Vector at{};
    if (!activity || !activity->WasInitialized() || activity->Node() != node || !activity->SpawnedIcon(node, at)) return 0;
    if (x) *x = at.x;
    if (y) *y = at.y;
    if (z) *z = at.z;
    return 1;
}
