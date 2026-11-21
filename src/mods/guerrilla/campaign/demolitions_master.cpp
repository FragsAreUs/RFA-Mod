// Guerrilla's demolitions master: bring one building down with the weapons given, in the time
// given, without leaving the area.
//
// A challenge is a node attached to its target building (the node's parent), a row of
// activity_demolitions_master.xtbl (weapons, time limit, par time, area, rewards, texts) and a row
// of activity.xtbl. The player starts it at the node; Guerrilla shows the briefing, fades to black,
// rebuilds the building (the node's restore list, from its script's activity_restore_object), swaps
// the player's inventory for the table's weapons and clears the area. Then, checked every 250 ms:
//   - the building is down (gone, or the game's own "destroyed" flag, which also sends the scripts'
//     building_destroyed event) → success, 7.5 s to watch it fall;
//   - the time limit runs out (with none the timer counts up) → out of time;
//   - farther than Active_Range from the building's middle: 5 s to come back (not when 5 s or less
//     are left), then out of range; farther than 85 m → out of range at once;
//   - no ammo left and the building isn't collapsing, for 2.5 s → out of ammo; the player dies → fail.
// Then the results screen, the player is warped back to where they started with their own
// inventory and vehicle. The first win gives the row's salvage; beating the par time ("PRO TIME":
// elapsed ≤ par + 999 ms) the first time gives the same again. Each node keeps whether it is done,
// discovered, par beaten, how many times, and the best time.
//
// The mod runs the challenge's own rules: the use prompt at the node (the use key starts it; the console's
// rfg_dm_start <je_dm_01..16> too), the briefing and results (guerrilla_hud shows them through
// GuerrillaCampaign_Activity and answers through GuerrillaCampaign_ActivityChoose; with no HUD asking, the
// briefing is accepted and the results closed by themselves), the fade to black and back, the building
// rebuilt (world::restore_object), the area cleared and the AI ignoring the player, the death check, the
// warp back to where the player started, the play area centred as Guerrilla's (the building's box, or its layer's
// "Demo Master" marker), the player's weapons swapped for the challenge's and given back, the out-of-ammo loss. Not
// ported yet: the layer's supplies (barrels, propane tanks, ammo boxes, walkers, the turret); Unlimited_Ammo (no
// row sets it).
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "campaign_api.h"
#include "gear/gear_api.h"
#include "activity_base.h"
#include "alert_sources.h"
#include "converts.h"
#include "messages.h"
#include "handle_kinds.h"
#include "liberation.h"
#include "save.h"
#include "script_engine.h"
#include "tables.h"
#include "world_objects.h"
#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unordered_map>

namespace campaign::activities {
namespace {

// One row of activity_demolitions_master.xtbl (demolitions_master_level_data).
struct Weapon {
    std::string weapon;  // weapons.xtbl name, or none for objects the activity's layer places
    std::string icon;    // Non_Weapon_Icon_Name, for those
    int ammo = -1;
};
struct Level {
    std::string name;
    std::string baseData;     // its activity.xtbl row
    std::string bodyText;     // Demo_Info_Body_Text
    std::string radioMessage;
    bool useVehicleSpawn = false;
    std::vector<Weapon> weapons;
    int timeLimit = 0;        // seconds; 0 = none, the timer counts up
    int parTime = 10;
    bool unlimitedAmmo = false;
    bool multipleTargets = false;
    std::string briefingMessage = "ACT_DEMO_MASTER_SUBTITLE";
    std::string cornerMessage = "ACT_DM_DESTROY_BUILDING";
    std::string infoTitle1, infoTitle2;
    std::string success, failAmmo, failTime, failRange, failDeath;
    float activeRange = 25.0f;
    Rewards rewards;
};

// A node's progress (dm_stats), kept in the campaign's save under its handle.
struct Progress {
    bool completed = false, discovered = false, parBeaten = false;
    int timesCompleted = 0;
    int bestTime = 0;  // ms
};

// Guerrilla's demolitions_master_states.
enum class Step { Wait, Ready, LoadingLayers, PregameBriefing, PregameAccept, PregameReject, Active, Complete, PostgameBriefing, PostgameReject, PostgameDelay };

constexpr float kHardRange = 85.0f;        // beyond this, out of range at once
constexpr float kRangeGrace = 5.0f;        // seconds to come back into the area
constexpr float kPollPeriod = 0.25f;       // do_frame_polling_period
constexpr float kSuccessDelay = 7.5f;      // watching the building fall
constexpr float kFailureDelay = 0.1f;
constexpr int kParSlackMs = 999;
constexpr float kUseDistance = 1.0f;       // the "use" prompt at a node (Guerrilla compares squared distance with 1)
constexpr float kOutOfAmmoGrace = 2.5f;    // out of ammo this long (the building not coming down) loses
constexpr float kCollapseQuiet = 1.0f;     // the building counts as coming down while its damage rose this recently
constexpr float kTargetMarkMin = 4.0f;     // the smallest target mark on the minimap (m)

std::vector<Level> g_levels;

const Level* FindLevel(const std::string& name) {
    for (const Level& level : g_levels)
        if (_stricmp(level.name.c_str(), name.c_str()) == 0) return &level;
    return nullptr;
}

void LoadLevels() {
    std::vector<tables::Element> rows;
    if (!tables::Load("activity_demolitions_master", rows)) return;
    for (const tables::Element& row : rows) {
        Level level;
        level.name = row.Text("Name");
        level.baseData = row.Text("base_activity_data");
        level.bodyText = row.Text("Demo_Info_Body_Text");
        level.radioMessage = row.Text("Radio_Message");
        level.useVehicleSpawn = row.Flag("Use_Vehicle_Spawn");
        if (const tables::Element* list = row.Child("Weapon_List"))
            for (const tables::Element& entry : list->children)
                level.weapons.push_back({entry.Text("Weapon"), entry.Text("Non_Weapon_Icon_Name"), entry.Int("Ammo", -1)});
        level.timeLimit = row.Int("Time_Limit", 0);
        level.parTime = row.Int("Par_Time", 10);
        level.unlimitedAmmo = row.Flag("Unlimited_Ammo");
        level.multipleTargets = row.Flag("Multiple_Target_Buildings");
        level.briefingMessage = row.Text("Briefing_Destroy_Message", "ACT_DEMO_MASTER_SUBTITLE");
        level.cornerMessage = row.Text("Corner_HUD_Destroy_Message", "ACT_DM_DESTROY_BUILDING");
        level.infoTitle1 = row.Text("Info_Title_1");
        level.infoTitle2 = row.Text("Info_Title_2");
        level.success = row.Text("Success_String");
        level.failAmmo = row.Text("Failure_String_Ammo");
        level.failTime = row.Text("Failure_String_Time");
        level.failRange = row.Text("Failure_String_Range");
        level.failDeath = row.Text("Failure_String_Death");
        level.activeRange = row.Number("Active_Range", 25.0f);
        if (const tables::Element* rewards = row.Child("Standard_Rewards")) {
            level.rewards.control = rewards->Number("EDF_Control_Reduction");
            level.rewards.morale = rewards->Number("Morale_Increase");
            level.rewards.salvage = rewards->Int("Salvage_Reward");
        }
        g_levels.push_back(level);
    }
    rfa::LogDetail("activity_demolitions_master.xtbl: %u challenges", static_cast<unsigned>(g_levels.size()));
}

Progress LoadProgress(const world::Object& node) {
    // The converter gives the node's properties as Guerrilla's zone has them; the campaign's save
    // has the player's own.
    Progress progress;
    const std::string section = save::ForObject("demolitions", node.handle);
    progress.completed = save::Int(section.c_str(), "complete", node.Property("demolitions_master_complete") == "true") != 0;
    progress.discovered = save::Int(section.c_str(), "discovered", node.Property("demolitions_master_discovered") == "true") != 0;
    progress.parBeaten = save::Int(section.c_str(), "par_beaten", node.Property("demolitions_master_par_beaten") == "true") != 0;
    progress.timesCompleted = save::Int(section.c_str(), "times_completed", atoi(node.Property("demolitions_master_times_completed", "0").c_str()));
    progress.bestTime = save::Int(section.c_str(), "best_time", atoi(node.Property("demolitions_master_best_time", "0").c_str()));
    return progress;
}

// Each node's "done", for the map (NodeDone): read once at start-up, kept up to date on a win.
std::unordered_map<unsigned, bool> g_done;

void SaveProgress(unsigned node, const Progress& progress) {
    g_done[node] = progress.completed;
    const std::string section = save::ForObject("demolitions", node);
    save::SetInt(section.c_str(), "complete", progress.completed);
    save::SetInt(section.c_str(), "discovered", progress.discovered);
    save::SetInt(section.c_str(), "par_beaten", progress.parBeaten);
    save::SetInt(section.c_str(), "times_completed", progress.timesCompleted);
    save::SetInt(section.c_str(), "best_time", progress.bestTime);
}

// The world's buildings (rfg_mover): how many, and each (Armageddon's 0x4776E0 / 0x477730 on the world).
// A building's flags (+0x150) bit 4 is game_destroyed, the test Guerrilla's challenge wins by
// (activity_demolitions_master::activity_should_end: the target's flags.game_destroyed).
void* const kWorld = reinterpret_cast<void*>(0x1870630);
using CountFn = int(__fastcall*)(void* world, void* unused);
using AtFn = BYTE*(__fastcall*)(void* world, void* unused, int index);
auto const BuildingCount = reinterpret_cast<CountFn>(0x4776E0);
auto const BuildingAt = reinterpret_cast<AtFn>(0x477730);
constexpr DWORD kBuildingFlags = 0x150, kGameDestroyed = 1u << 4;
constexpr DWORD kBuildingDamage = 0x13C;  // its damage_pct (rfg_mover::serialize 0x6857D0), a float

constexpr DWORD kHandle = 0x68;  // an object's handle

bool IsBuilding(const void* object) {
    if (!object) return false;
    const int count = BuildingCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i)
        if (BuildingAt(kWorld, nullptr, i) == object) return true;
    return false;
}

// The building standing at a place (within 2 m of it), or null: for a target the world converter gave
// another handle, because ours already had Guerrilla's.
BYTE* BuildingAtPlace(const Vector& place) {
    BYTE* best = nullptr;
    float bestDistance = 2.0f * 2.0f;
    const int count = BuildingCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* building = BuildingAt(kWorld, nullptr, i);
        if (!building) continue;
        const Vector at = PositionOf(building);
        const float dx = at.x - place.x, dy = at.y - place.y, dz = at.z - place.z;
        const float distance = dx * dx + dy * dy + dz * dz;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = building;
        }
    }
    return best;
}

float FlatDistance(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

// The game's console output (printf-style), so a player trying it sees what happens.
using ConsolePrintFn = void(__cdecl*)(const char* format, ...);
auto const ConsolePrint = reinterpret_cast<ConsolePrintFn>(0xC7C440);

void NotPorted(const char* what) {
    static std::vector<std::string> said;
    for (const std::string& line : said)
        if (line == what) return;
    said.push_back(what);
    rfa::Log("Demolitions master: %s isn't ported yet", what);
}

// ---- Armageddon's calls for the run

// The screen fading to black and back (the interface's ui_fade_to_black): FadeOut(0, seconds) counts a
// fade up and darkens (0x578790), FadeIn(1, 0, seconds) counts it down (0x578720), IsBlack() (0x578830).
auto const FadeOut = reinterpret_cast<void(__cdecl*)(int unused, float seconds)>(0x578790);
auto const FadeIn = reinterpret_cast<void(__cdecl*)(int a, int b, float seconds)>(0x578720);
auto const IsBlack = reinterpret_cast<bool(__cdecl*)()>(0x578830);
constexpr float kFadeSeconds = 1.0f;
constexpr float kFadeWaitMost = 2.0f;  // never wait longer for the black
// world::restore_object (0x6CF800, thiscall: the world, the handle, false): the building rebuilt, as the
// console's restore_object and the script word of that name do.
auto const RestoreObject = reinterpret_cast<bool(__fastcall*)(void* world, void* unused, DWORD handle, bool flag)>(0x6CF800);
constexpr float kRestoreWaitMost = 5.0f;
// ai_ignore_player: the flag (0x11A777C) and its setter (0x4768F0), as the console command.
auto& g_aiIgnoresPlayer = *reinterpret_cast<const BYTE*>(0x11A777C);
auto const SetAiIgnoresPlayer = reinterpret_cast<void(__cdecl*)(bool ignore)>(0x4768F0);
// The console's warp: the zone at a place (0x6CB3C0 on the world: null outside the loaded ones), the
// player's own move (his virtual +0x28: place, orientation, true), his velocity zeroed (0x850990), then
// 0x7B0860(0, 1).
auto const ZoneAt = reinterpret_cast<void*(__fastcall*)(void* world, void* unused, const float* place)>(0x6CB3C0);
using MoveFn = void(__fastcall*)(BYTE* object, void* unused, const float* place, const float* orientation, bool flag);
auto const SetVelocity = reinterpret_cast<void(__cdecl*)(BYTE* human, const float* velocity)>(0x850990);
auto const AfterWarp = reinterpret_cast<void(__fastcall*)(BYTE* human, void* unused, int a, int b)>(0x7B0860);
// The use key, as the game reads it (whatever the player bound): the action for what he is doing
// (0x4E1B70) held (0x4E4800), for the local player (0x11C0580).
auto const UseAction = reinterpret_cast<int(__cdecl*)(int player)>(0x4E1B70);
auto const ButtonHeld = reinterpret_cast<bool(__cdecl*)(int action, int player)>(0x4E4800);
auto& g_localPlayer = *reinterpret_cast<const int*>(0x11C0580);
// People.
using CountFn2 = int(__fastcall*)(void* world, void* unused);
using AtFn2 = BYTE*(__fastcall*)(void* world, void* unused, int index);
auto const HumanCount = reinterpret_cast<CountFn2>(0x43A860);
auto const HumanAt = reinterpret_cast<AtFn2>(0x43A8B0);
auto const Disposition = reinterpret_cast<int(__cdecl*)(BYTE* human, BYTE* other)>(0x8A04D0);
constexpr DWORD kInVehicle = 0x1160, kAi = 0x174C, kOrientation = 0x10;
constexpr int kHostile = 0;
constexpr float kNpcSweepPeriod = 10.0f;  // despawn_npcs_for_demo_master every 10 s
using IsDeadFn2 = bool(__thiscall*)(void* object);
bool Dead(BYTE* object) { return reinterpret_cast<IsDeadFn2>((*reinterpret_cast<void***>(object))[4])(object); }

void WarpPlayer(const Vector& place, const float* orientation) {
    BYTE* player = static_cast<BYTE*>(Player());
    if (!player) return;
    if (!ZoneAt(kWorld, nullptr, &place.x)) {
        rfa::Log("Demolitions master: the start (%.0f, %.0f, %.0f) isn't in a loaded zone; the player stays where he is", place.x, place.y, place.z);
        return;
    }
    const MoveFn move = reinterpret_cast<MoveFn>((*reinterpret_cast<void***>(player))[0x28 / 4]);
    move(player, nullptr, &place.x, orientation, true);
    const float still[3] = {0, 0, 0};
    SetVelocity(player, still);
    AfterWarp(player, nullptr, 0, 1);
}

// despawn_npcs_for_demo_master: those who take the player for an enemy and know of him, or anyone in the
// area, leave (Guerrilla's active despawn).
void SweepNpcs(const Vector& center, float range) {
    BYTE* player = static_cast<BYTE*>(Player());
    if (!player) return;
    const DWORD playerHandle = *reinterpret_cast<const DWORD*>(player + kHandle);
    int sent = 0;
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!human || human == player || Dead(human) || !*reinterpret_cast<BYTE**>(human + kAi)) continue;
        if (*reinterpret_cast<const DWORD*>(human + kInVehicle) != 0xFFFFFFFF) continue;
        const bool inArea = FlatDistance(PositionOf(human), center) < range;
        const bool after = Disposition(human, player) == kHostile && (alert_sources::Believes(human, 0, playerHandle) || alert_sources::Believes(human, 1, playerHandle));
        if (!inArea && !after) continue;
        converts::SendAway(*reinterpret_cast<const DWORD*>(human + kHandle));
        ++sent;
    }
    if (sent) rfa::LogDetail("Demolitions master: %d people leave the area", sent);
}

// The HUD's results reasons (DemolitionsInfo's failure text).
const char* FailureTag(EndReason reason) {
    switch (reason) {
    case OutOfTime: return "ACTIVITY_FAIL_OUT_OF_TIME";
    case OutOfRange: return "ACTIVITY_FAIL_LEFT_AREA";
    case OutOfAmmo: return "ACTIVITY_FAIL_OUT_OF_AMMO";
    case PlayerDied: return "MENU_YOU_DIED";
    default: return "";
    }
}

// A Weapon_List entry without a weapon: something the challenge's layer places (its icon, and how many as its Ammo).
bool IsProp(const Weapon& weapon) { return weapon.weapon.empty() || _stricmp(weapon.weapon.c_str(), "none") == 0; }

// ---- The challenge's kit: finalize_activity_initialization (the player's inventory kept, all of it taken, the
// table's weapons given, the first one in hand) and reload_inventory (his own given back). Armageddon's inventory:
// the record in a slot (0x7A8290), the game's delete of a record (0x85C6A0, as its swap 0x8BEB00 calls it), the add
// into a slot (0x85B260), the record in a hand (0x84E840), the player's equip (0x8BB420); a gun's rounds are its
// magazine while in his hands and his ammo store's reserve of its type (as guerrilla_hud's weapon cabinet keeps them).
namespace kit {
constexpr DWORD kWeaponTable = 0x18F8048, kWeaponTableCount = 0x18F804C, kWeaponEntrySize = 0xE0;
constexpr DWORD kEntryInventoryItem = 0x18;  // a weapon entry -> its inventory item
constexpr DWORD kRecordWeapon = 0x18;        // an inventory record -> its weapon's handle
constexpr DWORD kWeaponEntry = 0x188;        // a weapon -> its weapon entry
constexpr DWORD kWeaponFiringMode = 0x1C8, kWeaponMagazine = 0x2E0, kModeMagazineSize = 0x13E;
constexpr DWORD kWeaponCarried = 0x2FC;      // the first firing mode's rounds carried besides the magazine
constexpr int kSlots = 4;
auto const RecordInSlot = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human, int slot)>(0x7A8290);
auto const DeleteRecord = reinterpret_cast<void(__cdecl*)(BYTE* human, BYTE* record, bool, bool, bool)>(0x85C6A0);
auto const GiveIntoSlot = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human, const void* inventoryItem, int rounds, int slot, bool, bool, bool)>(0x85B260);
auto const RecordInHand = reinterpret_cast<BYTE*(__fastcall*)(BYTE* human, void* unused, int hand)>(0x84E840);
auto const PlayerEquip = reinterpret_cast<bool(__cdecl*)(BYTE* player, BYTE* record, int hand, bool, bool)>(0x8BB420);
auto const FiringModeInfo = reinterpret_cast<BYTE*(__fastcall*)(BYTE* mode, void* unused)>(0x42A1C0);
auto const HumansAmmo = reinterpret_cast<void*(__fastcall*)(BYTE* human, void* unused)>(0x84E8F0);
auto const ReserveRounds = reinterpret_cast<int(__fastcall*)(void* ammo, void* unused, BYTE* weapon, int slot)>(0x70EF40);
auto const AddRounds = reinterpret_cast<int(__fastcall*)(void* ammo, void* unused, BYTE* weapon, int slot, int rounds)>(0x70F4F0);
auto const TakeRounds = reinterpret_cast<int(__fastcall*)(void* ammo, void* unused, BYTE* weapon, int slot, int rounds)>(0x70F650);
auto const SetMagazine = reinterpret_cast<void(__fastcall*)(BYTE* weapon, void* unused, int rounds, int mode)>(0x7310F0);

// Guerrilla's hammers (ammo type MELEE: always "ammo" to human_has_ammo), as guerrilla_gear builds them.
const char* const kHammers[] = {"rfg_sledgehammer", "sledge_upgrade_1", "sledge_upgrade_2"};

struct Kept {
    const void* item = nullptr;  // its inventory item
    std::string name;
    int rounds = 0;
};
Kept g_kept[kSlots];
bool g_haveKept[kSlots];
int g_heldSlot = -1;
bool g_swapped = false;

const BYTE* EntryNamed(const char* name) {
    BYTE* entries = *reinterpret_cast<BYTE* const*>(kWeaponTable);
    const int count = *reinterpret_cast<const int*>(kWeaponTableCount);
    for (int i = 0; entries && i < count; ++i) {
        const char* own = *reinterpret_cast<const char* const*>(entries + i * kWeaponEntrySize);
        if (own && _stricmp(own, name) == 0) return entries + i * kWeaponEntrySize;
    }
    return nullptr;
}

// Our weapon for one of Guerrilla's weapons.xtbl names: "rfg_" + it where Armageddon has the name, else the name.
const BYTE* EntryFor(const std::string& guerrilla) {
    if (const BYTE* ours = EntryNamed(("rfg_" + guerrilla).c_str())) return ours;
    return EntryNamed(guerrilla.c_str());
}

const char* NameOf(const BYTE* entry) { return entry ? *reinterpret_cast<const char* const*>(entry) : ""; }

bool IsHammer(const BYTE* entry) {
    for (const char* hammer : kHammers)
        if (_stricmp(NameOf(entry), hammer) == 0) return true;
    return false;
}

// A record's weapon entry (and its weapon), or null.
const BYTE* RecordEntry(BYTE* record, BYTE** weapon = nullptr) {
    BYTE* object = record ? static_cast<BYTE*>(ObjectFromHandle(*reinterpret_cast<const DWORD*>(record + kRecordWeapon))) : nullptr;
    if (weapon) *weapon = object;
    return object ? *reinterpret_cast<const BYTE* const*>(object + kWeaponEntry) : nullptr;
}

bool InHands(BYTE* player, const BYTE* weapon) {
    BYTE* held = nullptr;
    RecordEntry(RecordInHand(player, nullptr, 0), &held);
    return held && held == weapon;
}

// The rounds a weapon carries itself besides its magazine (its first firing mode's +0x134, what 0x731B00 reads): where a
// thrown weapon's stock is (the remote charges'), as the ammo store keeps none of it (2026-11-13: the store read 0 for
// the charge placer while its reloads went on, and the challenge's "1 charge" refilled from the given one's own stock).
int& CarriedOf(BYTE* weapon) { return *reinterpret_cast<int*>(weapon + kWeaponCarried); }

int RoundsOf(BYTE* player, BYTE* weapon) {
    void* ammo = HumansAmmo(player, nullptr);
    const int reserve = (ammo ? ReserveRounds(ammo, nullptr, weapon, 0) : 0) + (std::max)(0, CarriedOf(weapon));
    // A magazine reads -1 while it reloads (the HUD's "ran empty, -1 left"): none in it, not one fewer.
    return reserve + (InHands(player, weapon) ? (std::max)(0, *reinterpret_cast<const int*>(weapon + kWeaponMagazine)) : 0);
}

// A weapon given that many rounds in all: its magazine as full as they fill it, the rest in the reserve (all of them in
// the reserve for one put away, which takes its magazine's out as it's drawn).
void SetRounds(BYTE* player, BYTE* weapon, int total) {
    BYTE* mode = FiringModeInfo(weapon + kWeaponFiringMode, nullptr);
    const int size = mode ? *reinterpret_cast<const WORD*>(mode + kModeMagazineSize) : 0;
    const int magazine = (std::max)(0, (std::min)(total, size));
    const bool inHands = InHands(player, weapon);
    SetMagazine(weapon, nullptr, magazine, 0);  // as guerrilla_hud's SetRounds (tested in game); the reserve is read after it
    void* ammo = HumansAmmo(player, nullptr);
    if (!ammo) return;
    const int now = ReserveRounds(ammo, nullptr, weapon, 0), wanted = (std::max)(0, inHands ? total - magazine : total);
    if (now > wanted) TakeRounds(ammo, nullptr, weapon, 0, now - wanted);
    else if (now < wanted) AddRounds(ammo, nullptr, weapon, 0, wanted - now);
    // What the store didn't take (a thrown weapon's: none) the weapon carries itself; nothing more of its own.
    CarriedOf(weapon) = (std::max)(0, wanted - ReserveRounds(ammo, nullptr, weapon, 0));
}

void TakeAll(BYTE* player) {
    for (int s = 0; s < kSlots; ++s)
        if (BYTE* record = RecordInSlot(player, s)) DeleteRecord(player, record, false, false, false);
}

// One weapon into a slot with that many rounds (a hammer keeps its own: none); its record, or null.
BYTE* Give(BYTE* player, const BYTE* entry, int slot, int rounds) {
    const void* item = entry ? *reinterpret_cast<const void* const*>(entry + kEntryInventoryItem) : nullptr;
    BYTE* record = item ? GiveIntoSlot(player, item, 0, slot, false, false, false) : nullptr;
    BYTE* weapon = nullptr;
    if (record && RecordEntry(record, &weapon) && weapon && !IsHammer(entry)) SetRounds(player, weapon, rounds);
    return record;
}

bool Swapped() { return g_swapped; }

void Swap(BYTE* player, const Level& level) {
    if (g_swapped) return;
    BYTE* held = RecordInHand(player, nullptr, 0);
    g_heldSlot = -1;
    for (int s = 0; s < kSlots; ++s) {
        g_haveKept[s] = false;
        BYTE* record = RecordInSlot(player, s);
        BYTE* weapon = nullptr;
        const BYTE* entry = RecordEntry(record, &weapon);
        if (!entry || !weapon) continue;
        g_kept[s] = {*reinterpret_cast<const void* const*>(entry + kEntryInventoryItem), NameOf(entry), RoundsOf(player, weapon)};
        g_haveKept[s] = true;
        if (record == held) g_heldSlot = s;
    }
    TakeAll(player);
    // The table's weapons, Guerrilla's slots ((i + 3) % 4: the first in the hammer's); none at all, the sledgehammer.
    int given = 0;
    BYTE* first = nullptr;
    std::string list;
    for (const Weapon& w : level.weapons) {
        if (IsProp(w)) continue;
        const int slot = (given + 3) % kSlots;
        ++given;
        const BYTE* entry = EntryFor(w.weapon);
        BYTE* record = entry ? Give(player, entry, slot, (std::max)(0, w.ammo)) : nullptr;
        if (!record) {
            rfa::Log("Demolitions master %s: couldn't give %s (%s)", level.name.c_str(), w.weapon.c_str(), entry ? "the game refused" : "not built");
            continue;
        }
        if (!first) first = record;
        list += (list.empty() ? "" : ", ") + std::string(NameOf(entry)) + (IsHammer(entry) ? "" : " " + std::to_string((std::max)(0, w.ammo)));
    }
    if (given == 0) {
        first = Give(player, EntryNamed(kHammers[0]), 3, 0);
        list = first ? "rfg_sledgehammer (the list has none)" : "nothing (rfg_sledgehammer isn't built)";
    }
    if (first) PlayerEquip(player, first, 0, false, false);
    g_swapped = true;
    std::string kept;
    for (int s = 0; s < kSlots; ++s)
        if (g_haveKept[s]) kept += (kept.empty() ? "" : ", ") + g_kept[s].name + " " + std::to_string(g_kept[s].rounds);
    rfa::LogDetail("Demolitions master %s: the player's own kept (%s), given %s", level.name.c_str(), kept.empty() ? "none" : kept.c_str(), list.c_str());
}

// The player's own back, as they were (reload_inventory).
void Restore(BYTE* player, const char* why) {
    if (!g_swapped || !player) return;
    g_swapped = false;
    TakeAll(player);
    BYTE* held = nullptr;
    int back = 0;
    for (int s = 0; s < kSlots; ++s) {
        if (!g_haveKept[s]) continue;
        const void* item = g_kept[s].item;
        BYTE* record = GiveIntoSlot(player, item, 0, s, false, false, false);
        BYTE* weapon = nullptr;
        const BYTE* entry = RecordEntry(record, &weapon);
        if (!record) {
            rfa::Log("Demolitions master: couldn't give back %s (slot %d)", g_kept[s].name.c_str(), s);
            continue;
        }
        if (weapon && !IsHammer(entry)) SetRounds(player, weapon, g_kept[s].rounds);
        if (s == g_heldSlot) held = record;
        ++back;
    }
    if (held) PlayerEquip(player, held, 0, false, false);
    rfa::LogDetail("Demolitions master: the player's own %d weapons given back (%s)", back, why);
}

// The level went (a reload brings the save's inventory): nothing to give back.
void Forget() { g_swapped = false; }

// human_has_ammo(player, true, false): a remote charge of his still out (guerrilla_gear's GuerrillaGear_PlacedExplosives),
// or a weapon in his slots with rounds, or a melee one (the hammers).
bool HasAmmo(BYTE* player) {
    if (!player) return true;
    if (!guerrilla_gear::Running()) {
        RFA_LOG_FIRST(1, "Demolitions master: guerrilla_gear is off; charges still out don't count as ammo");
    } else {
        GuerrillaGearExplosive out[64];
        const int count = (std::min)(GuerrillaGear_PlacedExplosives(out, 64), 64);
        for (int i = 0; i < count; ++i)
            if (out[i].kind == 0 && out[i].players) return true;
    }
    for (int s = 0; s < kSlots; ++s) {
        BYTE* weapon = nullptr;
        const BYTE* entry = RecordEntry(RecordInSlot(player, s), &weapon);
        if (!entry || !weapon) continue;
        if (IsHammer(entry) || RoundsOf(player, weapon) > 0) return true;
    }
    return false;
}
}  // namespace kit

class Demolitions : public Activity {
public:
    Demolitions() : Activity(DemolitionsMaster, true, false) {}

    bool StartAt(const world::Object& node) {
        // demolitions_master_can_be_started: no activity running, waiting, alert GREEN.
        if (AnyRunning() || step_ != Step::Wait || liberation::AlertLevel() != handles::AlertGreen) return false;
        level_ = FindLevel(node.Property("demolitions_master_type"));
        if (!level_) {
            rfa::Log("Demolitions master node %08X: no challenge \"%s\"", node.handle, node.Property("demolitions_master_type").c_str());
            return false;
        }
        // The target must be in the world (a building missing would count as already down). It keeps
        // Guerrilla's handle, unless the world converter had to give it another: then it is the building
        // at its place.
        unsigned target = node.parent;
        const void* building = ObjectFromHandle(target);
        if (!IsBuilding(building) && node.hasParentPos) {
            if (BYTE* atPlace = BuildingAtPlace(node.parentPos)) {
                rfa::Log("Demolitions master %s: target %08X isn't a building here; the building at its place is %08X", level_->name.c_str(), target,
                         *reinterpret_cast<const DWORD*>(atPlace + kHandle));
                building = atPlace;
                target = *reinterpret_cast<const DWORD*>(atPlace + kHandle);
            }
        }
        if (!building) {
            rfa::Log("Demolitions master %s: its target building %08X isn't in the world; not started", level_->name.c_str(), node.parent);
            ConsolePrint("Demolitions master %s: its target building %08X isn't in the world\n", level_->name.c_str(), node.parent);
            return false;
        }
        if (!StartSpawn(node.handle, FindBaseData(level_->baseData), liberation::DistrictAt(node.pos))) return false;
        target_ = target;
        targetIsBuilding_ = IsBuilding(building);
        const Vector at = PositionOf(building);
        rfa::LogDetail("Demolitions master %s: target %08X at (%.0f, %.0f, %.0f), %s, %.0f m from the node", level_->name.c_str(), target_, at.x, at.y, at.z,
                 targetIsBuilding_ ? "a building" : "NOT one of the world's buildings (won when it is gone)", FlatDistance(at, node.pos));
        // The play area's middle (do_startup_initialization, then activity_do_frame_active's LOADING_LAYERS): the
        // building's bounding-box centre, or the layer's "Demo Master" dummy where it has one.
        center_ = node.hasAreaCenter ? node.areaCenter : node.hasParentCenter ? node.parentCenter : at;
        // The target's mark on the minimap: its box's middle, half its width (at least kTargetMarkMin, to be seen).
        targetCenter_ = node.hasParentCenter ? node.parentCenter : at;
        targetRadius_ = node.hasParentSize ? 0.5f * (std::max)(node.parentSize.x, node.parentSize.z) : kTargetMarkMin;
        if (targetRadius_ < kTargetMarkMin) targetRadius_ = kTargetMarkMin;
        rfa::LogDetail("Demolitions master %s: the area (%.0f m) is centred on %s at (%.0f, %.0f, %.0f), %.0f m from the node", level_->name.c_str(),
                 level_->activeRange, node.hasAreaCenter ? "the layer's Demo Master marker" : node.hasParentCenter ? "the building's box" : "the building's place",
                 center_.x, center_.y, center_.z, FlatDistance(center_, node.pos));
        progress_ = LoadProgress(node);
        step_ = Step::LoadingLayers;
        // activity_demolitions_master_start: where the player stands, to warp him back at the end.
        if (BYTE* player = static_cast<BYTE*>(Player())) {
            startPosition_ = PositionOf(player);
            memcpy(startOrientation_, player + kOrientation, sizeof(startOrientation_));
            haveStart_ = true;
        }
        rfa::LogDetail("Demolitions master %s: started at node %08X, target building %08X", level_->name.c_str(), node.handle, target_);
        return true;
    }

    void Reset() override {
        Activity::Reset();
        step_ = Step::Wait;
        target_ = 0xFFFFFFFF;
        targetDown_ = success_ = false;
        reason_ = Success;
        choice_ = -1;
        screenAsked_ = false;
        fadedOut_ = false;
        haveStart_ = false;
        salvageGiven_ = 0;
        proTime_ = false;
        StopListening();
    }

    // A new level: the game has dropped every listener with the old one (Guerrilla's
    // gem_level_shutdown), so ours is forgotten, not taken away.
    void LevelInit() override {
        listener_ = nullptr;
        kit::Forget();
        Activity::LevelInit();
    }

    bool LockAlertLevel() const override { return IsActive(); }

    HudInfo Hud() const override {
        HudInfo info;
        info.running = step_ == Step::Active || step_ == Step::PostgameDelay;
        info.mode = DemolitionsMaster;
        info.title = "ACT_DEMO_MASTER_NAME";
        info.objective = level_ ? level_->cornerMessage : "";
        info.timerVisible = info.running;
        info.timerText = TimerText();
        info.status = targetDown_ ? 2 : 0;
        return info;
    }

    // For the HUD: what of the run it shows now (briefing, corner panel, results).
    void Fill(Screen& screen) {
        screenAsked_ = true;
        screen = {};
        if (!level_ || step_ == Step::Wait || step_ == Step::Complete) return;
        screen.mode = DemolitionsMaster;
        screen.screen = step_ == Step::PregameBriefing ? 1 : step_ == Step::Active || step_ == Step::PostgameDelay ? 2 : step_ == Step::PostgameBriefing ? 3 : 0;
        screen.title = "ACT_DEMO_MASTER_NAME";
        screen.subtitle = level_->briefingMessage;
        screen.body = level_->bodyText;
        screen.objective = level_->cornerMessage;
        screen.timer = TimerText();
        for (size_t i = 0; i < level_->weapons.size() && i < 4; ++i) {
            const Weapon& weapon = level_->weapons[i];
            screen.weapons.push_back(weapon.weapon.empty() || _stricmp(weapon.weapon.c_str(), "none") == 0 ? weapon.icon : weapon.weapon);
            screen.ammo.push_back(weapon.ammo);
        }
        screen.timeLimit = level_->timeLimit;
        screen.parTime = level_->parTime;
        screen.bestMs = progress_.completed ? progress_.bestTime : 0;
        screen.elapsedMs = static_cast<int>(elapsed_ * 1000.0f);
        screen.status = targetDown_ ? 2 : 0;
        screen.success = success_;
        screen.result = ResultText();
        screen.failure = success_ ? "" : FailureTag(reason_);
        screen.salvage = salvageGiven_;
        screen.proTime = proTime_;
        screen.outOfRange = rangeTimer_ > 0.0f;
        screen.center = center_;
        screen.range = level_->activeRange;
        screen.hasTarget = true;
        screen.target = targetCenter_;
        screen.targetRadius = targetRadius_;
        screen.targetDown = targetDown_;
    }

    // The HUD's answer: at the briefing, accept (1) or cancel (0); at the results, close (any).
    void Choose(int accept) { choice_ = accept ? 1 : 0; }

    // The player's death (demolitions_master_process_player_death).
    // His own weapons go back at once: the respawn's save (guerrilla_save's, made after the death) must hold his, not
    // the challenge's.
    void OnPlayerDeath() {
        kit::Restore(static_cast<BYTE*>(Player()), "the player died");
        if (step_ == Step::Active) Ending(false, PlayerDied);
    }

    // The use prompt at a node (player_is_near_demolitions_master_node): Guerrilla's icon (1, the default
    // hand), or 0.
    int UsePrompt() const { return offerNode_ != nullptr ? 1 : 0; }

protected:
    bool Initialize() override {
        if (!Activity::Initialize()) return false;
        if (target_ == 0xFFFFFFFF) {
            rfa::Log("Demolitions master %s: the node has no building", level_->name.c_str());
            End();
            return false;
        }
        return true;
    }

    void FrameReady(float seconds) override {
        // activity_do_frame_ready: the briefing (rfg_ui_demolitions; Accept or Cancel).
        if (step_ == Step::LoadingLayers) {
            step_ = Step::PregameBriefing;
            choice_ = -1;
            screenAsked_ = false;
            briefingTime_ = 0.0f;
            rfa::LogDetail("Demolitions master %s: the briefing", level_->name.c_str());
        }
        if (step_ == Step::PregameBriefing) {
            briefingTime_ += seconds;
            // A HUD that doesn't show the briefing never answers: go on as if accepted.
            if (choice_ < 0 && !screenAsked_ && briefingTime_ > 1.0f) choice_ = 1;
            if (choice_ == 0) {  // demolitions_master_reject
                rfa::LogDetail("Demolitions master %s: the briefing was cancelled", level_->name.c_str());
                step_ = Step::PregameReject;
                End();
                return;
            }
            if (choice_ == 1) {  // demolitions_master_accept
                step_ = Step::PregameAccept;
                fadeTime_ = 0.0f;
                FadeOut(0, kFadeSeconds);
                fadedOut_ = true;
            }
            return;
        }
        if (step_ == Step::PregameAccept) {
            // do_loading_process: once black, the building rebuilt and the area made ready, then start up.
            fadeTime_ += seconds;
            if (!IsBlack() && fadeTime_ < kFadeWaitMost) return;
            if (!RestoreObject(kWorld, nullptr, target_, false)) rfa::Log("Demolitions master %s: the game didn't rebuild %08X (it may stand)", level_->name.c_str(), target_);
            restoreTime_ = 0.0f;
            // clean_area: the AI ignores the player, the area's people leave.
            aiIgnoredBefore_ = g_aiIgnoresPlayer != 0;
            SetAiIgnoresPlayer(true);
            SweepNpcs(center_, level_->activeRange + 10.0f);
            if (!BeginStartUp()) {
                step_ = Step::PregameReject;
                Unwind();
                End();
            }
        }
    }

    void FrameStarting(float seconds) override { restoreTime_ += seconds; }

    // activity_starting_completed: the restore list done (the building back and standing).
    bool StartingCompleted() const override {
        const BYTE* building = static_cast<const BYTE*>(ObjectFromHandle(target_));
        const bool standing = building && !(targetIsBuilding_ && (*reinterpret_cast<const DWORD*>(building + kBuildingFlags) & kGameDestroyed));
        return standing || restoreTime_ > kRestoreWaitMost;
    }

    bool Start() override {
        if (!Activity::Start()) return false;
        // activity_start RM 0x7AD7C1: mayhem killed and kept off while the challenge runs (ADR_ACTIVITY).
        if (Activity* mayhem = Get(Mayhem)) {
            mayhem->KillNow();
            mayhem->Disable(ByActivity);
        }
        // finalize_activity_initialization: the inventory swapped for the challenge's weapons.
        for (const Weapon& weapon : level_->weapons) {
            if (IsProp(weapon))
                rfa::LogDetail("Demolitions master %s: prop %s x%d (placed by the challenge's layer)", level_->name.c_str(), weapon.icon.c_str(), weapon.ammo);
            else
                rfa::LogDetail("Demolitions master %s: weapon %s, ammo %d", level_->name.c_str(), weapon.weapon.c_str(), weapon.ammo);
        }
        if (BYTE* player = static_cast<BYTE*>(Player())) kit::Swap(player, *level_);
        if (level_->unlimitedAmmo) NotPorted("Unlimited_Ammo (no challenge of Guerrilla's sets it)");
        timeRemaining_ = static_cast<float>(level_->timeLimit);
        elapsed_ = 0.0f;
        rangeTimer_ = 0.0f;
        pollTimer_ = 0.0f;
        sweepTimer_ = kNpcSweepPeriod;
        outOfAmmoTime_ = 0.0f;
        lastDamage_ = -1.0f;
        damageChangedAt_ = 0.0f;
        // The building must be standing now; one still down counts as won at once (it wasn't rebuilt).
        const BYTE* building = static_cast<const BYTE*>(ObjectFromHandle(target_));
        if (building && targetIsBuilding_ && (*reinterpret_cast<const DWORD*>(building + kBuildingFlags) & kGameDestroyed))
            rfa::Log("Demolitions master %s: its building is still down after %.0f s; the game didn't rebuild it", level_->name.c_str(), restoreTime_);
        listener_ = script::Listen(script::kBuildingDestroyedEvent, &BuildingDestroyed, this);
        step_ = Step::Active;
        if (fadedOut_) {
            FadeIn(1, 0, kFadeSeconds);
            fadedOut_ = false;
        }
        messages::Tip("Demo_Master", 2500);
        ConsolePrint("Demolitions master %s: go! (par %d s, limit %d s, area %.0f m)\n", level_->name.c_str(), level_->parTime,
                     level_->timeLimit, level_->activeRange);
        return true;
    }

    void FrameActive(float seconds) override {
        if (step_ == Step::PostgameDelay) {
            postTimer_ -= seconds;
            if (postTimer_ <= 0.0f) Results();
            return;
        }
        if (step_ == Step::PostgameBriefing) {
            resultsTime_ += seconds;
            if (choice_ < 0 && !screenAsked_ && resultsTime_ > 0.5f) choice_ = 1;  // no HUD to show them
            if (choice_ >= 0) {
                step_ = Step::PostgameReject;
                fadeTime_ = 0.0f;
                FadeOut(0, kFadeSeconds);
                fadedOut_ = true;
            }
            return;
        }
        if (step_ == Step::PostgameReject) {
            // do_post_activity_loading_process: black, the warp back, then the victory or loss.
            fadeTime_ += seconds;
            if (!IsBlack() && fadeTime_ < kFadeWaitMost) return;
            Unwind();
            Finish();
            return;
        }
        if (step_ != Step::Active) return;
        elapsed_ += seconds;
        if (level_->timeLimit > 0) {
            timeRemaining_ -= seconds;
            if (timeRemaining_ <= 0.0f) return Ending(false, OutOfTime);
        }
        if (rangeTimer_ > 0.0f) {
            rangeTimer_ -= seconds;
            if (rangeTimer_ <= 0.0f) return Ending(false, OutOfRange);
        }
        sweepTimer_ -= seconds;
        if (sweepTimer_ <= 0.0f) {
            sweepTimer_ = kNpcSweepPeriod;
            SweepNpcs(center_, level_->activeRange);
        }
        pollTimer_ -= seconds;
        if (pollTimer_ > 0.0f) return;
        pollTimer_ = kPollPeriod;
        // activity_should_end: the building down (its game_destroyed flag, the game's event, or gone), out
        // of ammo (with the weapons swap, piece 3b).
        const BYTE* building = static_cast<const BYTE*>(ObjectFromHandle(target_));
        if (building && targetIsBuilding_ && (*reinterpret_cast<const DWORD*>(building + kBuildingFlags) & kGameDestroyed)) targetDown_ = true;
        if (targetDown_ || !building) return Ending(true, Success);
        void* player = Player();
        if (!player) return;
        // Out of ammo (activity_should_end with Check_demo_master_ammo: human_has_ammo(player, true, false)): while the
        // building is still coming down wait, else after 2.5 s the loss. Guerrilla asks its stress evaluation whether the
        // building is collapsing (stress_evaluation_is_active); here, whether its damage (+0x13C) still rose in the last second.
        const float damage = targetIsBuilding_ ? *reinterpret_cast<const float*>(building + kBuildingDamage) : 0.0f;
        if (damage != lastDamage_) {
            lastDamage_ = damage;
            damageChangedAt_ = elapsed_;
        }
        if (kit::Swapped() && !kit::HasAmmo(static_cast<BYTE*>(player))) {
            if (elapsed_ - damageChangedAt_ < kCollapseQuiet) {
                outOfAmmoTime_ = 0.0f;
            } else {
                if (outOfAmmoTime_ == 0.0f) rfa::LogDetail("Demolitions master %s: out of ammo, the building isn't coming down", level_->name.c_str());
                outOfAmmoTime_ += kPollPeriod;
                if (outOfAmmoTime_ >= kOutOfAmmoGrace) return Ending(false, OutOfAmmo);
            }
        } else {
            outOfAmmoTime_ = 0.0f;
        }
        const float distance = FlatDistance(PositionOf(player), center_);
        if (distance > kHardRange) return Ending(false, OutOfRange);
        if (distance > level_->activeRange) {
            if (rangeTimer_ <= 0.0f && !(level_->timeLimit > 0 && timeRemaining_ <= kRangeGrace)) {
                rangeTimer_ = kRangeGrace;
                messages::Minor("ACT_DM_5_SECONDS", nullptr);
                ConsolePrint("Demolitions master: 5 seconds to return to the area\n");
            }
        } else {
            rangeTimer_ = 0.0f;
        }
    }

    void End() override {
        StopListening();
        Activity::End();
        // end 0x797AE2 and kill 0x797B32: mayhem allowed again.
        if (Activity* mayhem = Get(Mayhem)) mayhem->Enable(ByActivity);
    }

private:
    static int __cdecl BuildingDestroyed(const script::GameEvent* event, void* notify) {
        // The listener's data (notify +8) is the challenge.
        auto self = *reinterpret_cast<Demolitions* const*>(static_cast<BYTE*>(notify) + 8);
        if (self && event && event->values[0] == self->target_) self->targetDown_ = true;
        return script::kEventOk;
    }

    void StopListening() {
        script::StopListening(listener_);
        listener_ = nullptr;
    }

    // get_display_time: the out-of-range countdown, else the countdown, else the time so far.
    std::string TimerText() const {
        float shown = rangeTimer_ > 0.0f ? rangeTimer_ : level_ && level_->timeLimit > 0 ? timeRemaining_ : elapsed_;
        if (shown < 0.0f) shown = 0.0f;
        char text[16];
        sprintf_s(text, "%02d:%02d", static_cast<int>(shown) / 60, static_cast<int>(shown) % 60);
        return text;
    }

    std::string ResultText() const {
        if (!level_) return "";
        return success_ ? level_->success
               : reason_ == OutOfTime ? level_->failTime
               : reason_ == OutOfRange ? level_->failRange
               : reason_ == OutOfAmmo ? level_->failAmmo
                                      : level_->failDeath;
    }

    // begin_ending_sequence: the result, then a moment before the end.
    void Ending(bool success, EndReason reason) {
        success_ = success;
        reason_ = reason;
        rangeTimer_ = 0.0f;
        postTimer_ = success ? kSuccessDelay : kFailureDelay;
        step_ = Step::PostgameDelay;
        rfa::LogDetail("Demolitions master %s: %s after %.1f s", level_->name.c_str(), ResultText().c_str(), elapsed_);
        ConsolePrint("Demolitions master %s: %s (%.1f s)\n", level_->name.c_str(), ResultText().c_str(), elapsed_);
    }

    // do_activity_end: the progress and salvage counted (get_salvage_received), then the results screen.
    void Results() {
        const int elapsedMs = static_cast<int>(elapsed_ * 1000.0f);
        salvageGiven_ = 0;
        proTime_ = success_ && elapsedMs <= level_->parTime * 1000 + kParSlackMs;
        if (success_) {
            if (!progress_.completed) salvageGiven_ += level_->rewards.salvage;
            if (!progress_.parBeaten && proTime_) {
                progress_.parBeaten = true;
                salvageGiven_ += level_->rewards.salvage;
            }
            if (!progress_.completed || elapsedMs < progress_.bestTime) progress_.bestTime = elapsedMs;
            progress_.completed = true;
            ++progress_.timesCompleted;
            SaveProgress(node_, progress_);
        }
        step_ = Step::PostgameBriefing;
        choice_ = -1;
        screenAsked_ = false;
        resultsTime_ = 0.0f;
        rfa::LogDetail("Demolitions master %s: the results (%s, salvage %d%s)", level_->name.c_str(), success_ ? "won" : FailureTag(reason_), salvageGiven_,
                 proTime_ ? ", PRO TIME" : "");
    }

    // What the run changed, undone: the AI's ignoring, the warp back to the start, the fade.
    void Unwind() {
        kit::Restore(static_cast<BYTE*>(Player()), "the run is over");
        SetAiIgnoresPlayer(aiIgnoredBefore_);
        if (haveStart_) WarpPlayer(startPosition_, startOrientation_);
        if (fadedOut_) {
            FadeIn(1, 0, kFadeSeconds);
            fadedOut_ = false;
        }
    }

    void Finish() {
        End();
        if (success_) {
            Rewards rewards = level_->rewards;
            rewards.salvage = salvageGiven_;
            RecordVictory(rewards);
            ConsolePrint("Demolitions master %s: salvage %d, best %.1f s\n", level_->name.c_str(), salvageGiven_, progress_.bestTime / 1000.0f);
        } else {
            RecordLoss();
        }
        step_ = Step::Complete;
    }

    void FrameInactive(float) override {
        // activity_do_frame_inactive and player_is_near_demolitions_master_node: the offer at a node, about
        // 1 m from it, on foot, with nothing running, at green; the use key starts it.
        offerNode_ = nullptr;
        BYTE* player = static_cast<BYTE*>(Player());
        if (!player) return;
        const Vector here = PositionOf(player);
        const bool canStart = step_ == Step::Wait && !AnyRunning() && liberation::AlertLevel() == handles::AlertGreen && CanInitialize() &&
                              *reinterpret_cast<const DWORD*>(player + kInVehicle) == 0xFFFFFFFF;
        for (const world::Object* node : world::OfClass("object_demolitions_master_node")) {
            const float dx = node->pos.x - here.x, dy = node->pos.y - here.y, dz = node->pos.z - here.z;
            const float distance2 = dx * dx + dy * dy + dz * dz;
            if (distance2 < kUseDistance * kUseDistance * 4.0f && nearNode_ != node->handle) {
                nearNode_ = node->handle;
                ConsolePrint("Demolitions master here: rfg_dm_start %s\n", node->Property("demolitions_master_type").c_str());
            }
            if (canStart && distance2 < kUseDistance * kUseDistance) offerNode_ = node;
        }
        const bool use = ButtonHeld(UseAction(g_localPlayer), g_localPlayer);
        if (use && !useDown_ && offerNode_) {
            rfa::LogDetail("Demolitions master: the player starts %s at its node", offerNode_->Property("demolitions_master_type").c_str());
            const world::Object* node = offerNode_;
            offerNode_ = nullptr;
            StartAt(*node);
        }
        useDown_ = use;
    }

    const Level* level_ = nullptr;
    Step step_ = Step::Wait;
    unsigned target_ = 0xFFFFFFFF;
    Progress progress_;
    Vector startPosition_{};
    float startOrientation_[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    bool haveStart_ = false;
    Vector center_{};
    Vector targetCenter_{};
    float targetRadius_ = 0.0f;
    float timeRemaining_ = 0.0f, elapsed_ = 0.0f, rangeTimer_ = 0.0f, postTimer_ = 0.0f, pollTimer_ = 0.0f, sweepTimer_ = 0.0f;
    float briefingTime_ = 0.0f, resultsTime_ = 0.0f, fadeTime_ = 0.0f, restoreTime_ = 0.0f;
    float outOfAmmoTime_ = 0.0f, lastDamage_ = -1.0f, damageChangedAt_ = 0.0f;
    bool targetDown_ = false, success_ = false, targetIsBuilding_ = false;
    EndReason reason_ = Success;
    int choice_ = -1;          // the HUD's answer (-1: none yet)
    bool screenAsked_ = false; // the HUD asked what to show since the screen came up
    bool fadedOut_ = false;
    bool aiIgnoredBefore_ = false;
    bool useDown_ = true;      // a key held when the offer comes isn't a press
    int salvageGiven_ = 0;
    bool proTime_ = false;
    void* listener_ = nullptr;
    unsigned nearNode_ = 0xFFFFFFFF;
    const world::Object* offerNode_ = nullptr;
};

Demolitions* g_demolitions;

}  // namespace

Activity* MakeDemolitionsMaster() {
    LoadLevels();
    world::Load();
    int done = 0;
    for (const world::Object* node : world::OfClass("object_demolitions_master_node")) {
        g_done[node->handle] = LoadProgress(*node).completed;
        done += g_done[node->handle] ? 1 : 0;
    }
    if (!g_done.empty()) rfa::LogDetail("Demolitions master: %d of %u challenges done", done, static_cast<unsigned>(g_done.size()));
    g_demolitions = new Demolitions();
    return g_demolitions;
}

bool NodeDone(unsigned node) {
    const auto found = g_done.find(node);
    return (found != g_done.end() && found->second) || DeliveryNodeDone(node) || AreaDefenseNodeDone(node);
}

bool DemolitionsScreen(Screen& screen) {
    if (!g_demolitions) return false;
    g_demolitions->Fill(screen);
    return screen.mode != ModeNone;
}

void DemolitionsChoose(int accept) {
    if (g_demolitions) g_demolitions->Choose(accept);
}

int DemolitionsUsePrompt() { return g_demolitions ? g_demolitions->UsePrompt() : 0; }

void DemolitionsPlayerDied() {
    if (g_demolitions) g_demolitions->OnPlayerDeath();
}

void ReloadProgress() {
    DeliveryReloadProgress();
    AreaDefenseReloadProgress();
    for (const world::Object* node : world::OfClass("object_demolitions_master_node")) g_done[node->handle] = LoadProgress(*node).completed;
}

bool StartDemolitionsMaster(const char* level) {
    for (const world::Object* node : world::OfClass("object_demolitions_master_node"))
        if (_stricmp(node->Property("demolitions_master_type").c_str(), level) == 0) return g_demolitions->StartAt(*node);
    rfa::LogDetail("No demolitions master node of \"%s\" in campaign_objects.xml", level);
    return false;
}

}  // namespace campaign::activities

// 1 while a demolitions master challenge has swapped the player's weapons for its own: guerrilla_gear's hammer top-up
// (KeepHammer) should leave his slots alone then, as Guerrilla gives no hammer a challenge's list doesn't.
int GuerrillaCampaign_KitLocked() { return guerrilla_campaign::Running() && campaign::activities::kit::Swapped() ? 1 : 0; }
