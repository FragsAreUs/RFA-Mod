// A spawner in the in-game menus (rfa_menu.h). A part of rfa_qol.dll. Open the console (the ` key),
// then Spawn in the bar along the bottom of the screen, for four windows:
//   Vehicles  every vehicle the game has, Armageddon's and the Guerrilla ones
//             editor\GuerrillaBuilder.exe made; Spawn (or a double-click) drops one in front of you,
//             as the console's drop_car <name> does, loading it first if the area hasn't;
//   Items     every item the game can drop (drop_item <name>);
//   Weapons   every weapon, given to you (give_player_item <its inventory item>);
//   Spawned   the vehicles you spawned, to remove one or all, and how full the game's vehicle slots
//             are.
// The idea, the first spawner (vehicle_spawner.dll), how to load a vehicle the area hasn't, the
// weapons and items, the eight tracked vehicles and the walkers' guard (all below) are
// Venom_Intellect's.
//
// The menus are drawn on the thread that draws the game, so whatever a window asks for is only noted
// there; it is carried out on the game's own thread, from a hook of drop_car's per-frame step
// (0x619360, from the gameplay update): console commands, reservations and removals alike.
//
// Vehicles: the game's vehicle list (0x148BD08: its entries, how many it holds, how many it has;
// 0x880 bytes an entry: +0 its name in the table, +4 the name drop_car takes, Display_Name). A
// vehicle must have loaded before the game makes one (0x762290). Where a level's spawn resource is
// in force (the pools' state 0x19734CC isn't 0: Wrecking Crew levels, our worlds among them, and
// multiplayer) the game makes only the vehicles it holds, and loads no other; so the spawner adds the
// vehicle to that resource, as the game adds a resource's own vehicles when it starts (0x761DC0,
// cdecl: category 8, owner 3, the vehicle; it makes room only by letting go of a reservation nothing
// uses), waits until it has streamed in, then drops it. drop_car alone gives up after 3 seconds
// (0x10FCA14). (Clearing the state instead, as the first spawner's F8 did, makes the game dump every
// reservation, freeing what vehicles already in the world use, and it crashes soon after.) Where none
// is in force (the campaign), drop_car loads it itself. The game makes no vehicle while you are in one
// (the player, 0x8B92D0(), +0x1160: the vehicle's handle, -1 on foot).
//
// Spawned: the vehicle the game's create (0x762A40, cdecl: the vehicle, where, which way, variant)
// returns for one of our spawns is kept by its handle (+0x68), eight at most: one more removes the
// oldest. A vehicle is removed by its handle (0x6D56A0, thiscall on the world 0x1870630: the handle,
// 0), so one already gone is passed over. The slots: the vehicle pool (0x1972900) holds `+0xC` slots
// (a list, how many at +4), of which 0x7612F0 says how many are free.
//
// Items: the game's item table (0x1BB1D44 the entries, 0x1BB1D3C how many; 0x168 bytes an entry, its
// name first), the one drop_item looks names up in (0x7CA730). Weapons: the weapon table (0x18F8048
// the entries, 0x18F804C how many; 0xE0 bytes an entry: +0 its name, +0x18 its inventory item, whose
// +0 is the name give_player_item takes; +0xB bit 4 fires from the body, a creature's). Turret and
// walker weapons share the inventory item generic_inventory_item, which would only give the first of
// them, so the list doesn't give those.
//
// Walkers: every frame the game's per-human debug update (0x636440, from 0x638CC0) casts each human
// with 0x5FB5C0 (thiscall on the world: the object if its subtype is 7, else 0) and reads the cast's
// +0x1C without checking it; a walker is a human of another subtype, so a spawned walker crashed the
// game a few seconds later. The update only computes and draws debug overlays, so the spawner leaves
// it out for an object the cast turns down.
#define RFA_MOD_NAME "spawner"
#include "rfa_mod.h"
#include "rfa_menu.h"
#include "rfa_qol.h"
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace {

// ---- The game

constexpr DWORD kVehicleList = 0x148BD08;  // entries, how many it holds, how many it has
constexpr DWORD kVehicleSize = 0x880;
auto& g_vehicleEntries = *reinterpret_cast<BYTE* const*>(kVehicleList);
auto& g_vehicleCount = *reinterpret_cast<const int*>(kVehicleList + 8);
int VehicleCount() { return g_vehicleEntries ? g_vehicleCount : 0; }
BYTE* Vehicle(int i) { return g_vehicleEntries + i * kVehicleSize; }
// An entry's name in the table (+0) and the one drop_car takes (+4; its own if it has none).
const char* TableName(int i) { return *reinterpret_cast<const char* const*>(Vehicle(i)); }
const char* DisplayName(int i) {
    const char* name = *reinterpret_cast<const char* const*>(Vehicle(i) + 4);
    return name && *name ? name : TableName(i);
}

auto& g_itemEntries = *reinterpret_cast<BYTE* const*>(0x1BB1D44);
auto& g_itemCount = *reinterpret_cast<const int*>(0x1BB1D3C);
constexpr DWORD kItemSize = 0x168;
int ItemCount() { return g_itemEntries ? g_itemCount : 0; }
const char* ItemName(int i) { return *reinterpret_cast<const char* const*>(g_itemEntries + i * kItemSize); }

auto& g_weaponEntries = *reinterpret_cast<BYTE* const*>(0x18F8048);
auto& g_weaponCount = *reinterpret_cast<const int*>(0x18F804C);
constexpr DWORD kWeaponSize = 0xE0, kWeaponInventoryItem = 0x18, kWeaponFlags = 0xB;
constexpr BYTE kFiresFromBody = 0x04;
int WeaponCount() { return g_weaponEntries ? g_weaponCount : 0; }
BYTE* Weapon(int i) { return g_weaponEntries + i * kWeaponSize; }
const char* WeaponName(int i) { return *reinterpret_cast<const char* const*>(Weapon(i)); }
// The name give_player_item takes, or "".
const char* WeaponItem(int i) {
    const BYTE* item = *reinterpret_cast<const BYTE* const*>(Weapon(i) + kWeaponInventoryItem);
    const char* name = item ? *reinterpret_cast<const char* const*>(item) : nullptr;
    return name ? name : "";
}
bool WeaponShared(int i) { return _stricmp(WeaponItem(i), "generic_inventory_item") == 0; }
bool WeaponFromBody(int i) { return (Weapon(i)[kWeaponFlags] & kFiresFromBody) != 0; }

// Runs one console command (printf-style).
auto const RunCommand = reinterpret_cast<int(__cdecl*)(const char* format, ...)>(0xC7BDB0);

// drop_car's per-frame step (cdecl, no arguments): tries to create the vehicle it waits for.
constexpr DWORD kDropStep = 0x619360;
constexpr size_t kDropStepPrologue = 11;  // push ebp / mov ebp,esp / push ecx / mov dword ptr [ebp-4],0
using DropStepFn = void(__cdecl*)();
DropStepFn g_dropStep;

// The game's vehicle create (cdecl): the made vehicle, or null.
constexpr DWORD kCreateVehicle = 0x762A40;
constexpr size_t kCreateVehiclePrologue = 6;  // push ebp / mov ebp,esp / sub esp,68h
using CreateVehicleFn = BYTE*(__cdecl*)(BYTE* vehicle, const float* where, const float* orientation, int variant);
CreateVehicleFn g_createVehicle;

// The game's per-human debug update (cdecl, the object), and its cast (see the top).
constexpr DWORD kHumanDebug = 0x636440;
constexpr size_t kHumanDebugPrologue = 6;  // push ebx / mov ebx,esp / sub esp,8
using HumanDebugFn = void(__cdecl*)(void* object);
HumanDebugFn g_humanDebug;
auto const CastHuman = reinterpret_cast<void*(__fastcall*)(void* world, void* unused, void* object)>(0x5FB5C0);

auto const VehicleLoaded = reinterpret_cast<bool(__cdecl*)(BYTE* vehicle)>(0x762290);
auto const ReserveInResource = reinterpret_cast<bool(__cdecl*)(int category, int owner, BYTE* vehicle)>(0x761DC0);
constexpr int kVehicleCategory = 8, kOwner = 3;
constexpr int kResourceInForce = 4;  // the pools' state once a change of spawn resource is done
auto& g_poolState = *reinterpret_cast<const volatile int*>(0x19734CC);  // 0: no spawn resource in force
auto const Player = reinterpret_cast<BYTE*(__cdecl*)()>(0x8B92D0);
constexpr DWORD kPlayerVehicle = 0x1160;  // the vehicle the player is in, by handle; -1 on foot
auto const RemoveByHandle = reinterpret_cast<int(__fastcall*)(void* world, void* unused, unsigned handle, int flag)>(0x6D56A0);
void* const kWorld = reinterpret_cast<void*>(0x1870630);
constexpr DWORD kHandle = 0x68;  // an object's handle
BYTE* const kVehiclePool = reinterpret_cast<BYTE*>(0x1972900);
auto const PoolRoom = reinterpret_cast<int(__fastcall*)(void* pool, void* unused)>(0x7612F0);
constexpr DWORD kPoolSlots = 0xC + 4;  // how many slots the pool holds (its list's count)

constexpr ULONGLONG kLoadTimeout = 30000;
constexpr int kMaxSpawned = 8;

// ---- Between the menus' thread and the game's

struct Request {
    enum Kind { kVehicle, kItem, kWeapon, kRemove, kRemoveAll } kind;
    int index;
};
struct Spawned {
    unsigned handle;
    std::string name;
};

std::mutex g_lock;  // all below
std::deque<Request> g_requests;
std::string g_status;
std::vector<Spawned> g_spawned;  // oldest first
int g_slotsFree = -1, g_slots = -1, g_stateShown = 0;

void Ask(Request::Kind kind, int index) {
    std::lock_guard<std::mutex> hold(g_lock);
    g_requests.push_back({kind, index});
}

void SetStatus(const std::string& status) {
    std::lock_guard<std::mutex> hold(g_lock);
    g_status = status;
}

// ---- The windows

bool g_vehiclesOpen = false, g_itemsOpen = false, g_weaponsOpen = false, g_spawnedOpen = false;
ImGuiTextFilter g_vehicleFilter, g_itemFilter, g_weaponFilter;
int g_pickedVehicle = -1, g_pickedItem = -1, g_pickedWeapon = -1;

std::string Status() {
    std::lock_guard<std::mutex> hold(g_lock);
    return g_status;
}

// A filtered list of `count` names with a button under it; returns the one to act on, or -1.
template <class NameFn, class TipFn, class UsableFn>
int DrawList(ImGuiTextFilter& filter, int count, int& picked, const char* action, NameFn name, TipFn tip, UsableFn usable) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    filter.Draw("##filter");
    if (picked >= count) picked = -1;
    int act = -1;
    if (ImGui::BeginListBox("##list", ImVec2(-FLT_MIN, -ImGui::GetFrameHeightWithSpacing() * 2))) {
        for (int i = 0; i < count; ++i) {
            const char* text = name(i);
            const char* hint = tip(i);
            if (!text || !*text || !(filter.PassFilter(text) || (hint && *hint && filter.PassFilter(hint)))) continue;
            ImGui::PushID(i);
            ImGui::BeginDisabled(!usable(i));
            if (ImGui::Selectable(text, picked == i, ImGuiSelectableFlags_AllowDoubleClick)) {
                picked = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) act = i;
            }
            ImGui::EndDisabled();
            if (hint && *hint) ImGui::SetItemTooltip("%s", hint);
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    ImGui::BeginDisabled(picked < 0 || !usable(picked));
    if (ImGui::Button(action)) act = picked;
    ImGui::EndDisabled();
    ImGui::SameLine();
    const std::string status = Status();
    if (!status.empty()) ImGui::TextDisabled("%s", status.c_str());
    else ImGui::TextDisabled("%d", count);
    return act;
}

void SizeWindow() {
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 18, ImGui::GetFontSize() * 26), ImGuiCond_FirstUseEver);
}

void DrawVehicles() {
    SizeWindow();
    if (ImGui::Begin("Vehicles", &g_vehiclesOpen)) {
        const int act = DrawList(
            g_vehicleFilter, VehicleCount(), g_pickedVehicle, "Spawn", DisplayName,
            [](int i) { const char* table = TableName(i); return table && strcmp(table, DisplayName(i)) != 0 ? table : ""; },
            [](int) { return true; });
        if (act >= 0) Ask(Request::kVehicle, act);
    }
    ImGui::End();
}

void DrawItems() {
    SizeWindow();
    if (ImGui::Begin("Items", &g_itemsOpen)) {
        const int act = DrawList(g_itemFilter, ItemCount(), g_pickedItem, "Drop", ItemName, [](int) { return ""; },
                                 [](int) { return true; });
        if (act >= 0) Ask(Request::kItem, act);
    }
    ImGui::End();
}

void DrawWeapons() {
    SizeWindow();
    if (ImGui::Begin("Weapons", &g_weaponsOpen)) {
        const int act = DrawList(
            g_weaponFilter, WeaponCount(), g_pickedWeapon, "Give", WeaponName,
            [](int i) {
                return WeaponShared(i) ? "A turret's or walker's: shares its inventory item with others, so it can't be given"
                       : WeaponFromBody(i) ? "A creature's: fires from the body, with no gun"
                                           : "";
            },
            [](int i) { return *WeaponItem(i) && !WeaponShared(i); });
        if (act >= 0) Ask(Request::kWeapon, act);
    }
    ImGui::End();
}

void DrawSpawned() {
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 18, ImGui::GetFontSize() * 14), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Spawned", &g_spawnedOpen)) {
        std::vector<Spawned> spawned;
        int free, slots, state;
        {
            std::lock_guard<std::mutex> hold(g_lock);
            spawned = g_spawned;
            free = g_slotsFree;
            slots = g_slots;
            state = g_stateShown;
        }
        if (slots >= 0)
            ImGui::Text("Vehicle slots: %d of %d free%s", free, slots, state ? " (the area's spawn resource is in force)" : "");
        ImGui::TextDisabled("The vehicles you spawned (the oldest goes after %d):", kMaxSpawned);
        for (size_t i = 0; i < spawned.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton("Remove")) Ask(Request::kRemove, static_cast<int>(spawned[i].handle));
            ImGui::SameLine();
            ImGui::Text("%s", spawned[i].name.c_str());
            ImGui::PopID();
        }
        if (spawned.empty()) ImGui::TextDisabled("  none");
        ImGui::BeginDisabled(spawned.empty());
        if (ImGui::Button("Remove all")) Ask(Request::kRemoveAll, 0);
        ImGui::EndDisabled();
    }
    ImGui::End();
}

// ---- On the game's thread

BYTE* g_loading = nullptr;  // a vehicle added to the spawn resource, until it has streamed in
std::string g_loadingName;
ULONGLONG g_loadingSince = 0;
std::string g_expected;  // the vehicle one of our drop_cars may make (its create is noted for Spawned)
ULONGLONG g_expectedSince = 0;
bool g_inDropStep = false;  // drop_car's step is running: a vehicle made now is drop_car's

void Remove(unsigned handle) {
    const int removed = RemoveByHandle(kWorld, nullptr, handle, 0);
    rfa::LogDetail("Removing vehicle %08X: %s", handle, removed ? "removed" : "already gone");
}

void Drop(const std::string& name) {
    RunCommand("drop_car %s", name.c_str());
    rfa::LogDetail("Spawning %s", name.c_str());
    SetStatus("Spawned " + name);
    g_expected = name;
    g_expectedSince = GetTickCount64();
}

void SpawnVehicle(int i) {
    if (i < 0 || i >= VehicleCount()) return;
    const BYTE* player = Player();
    if (player && *reinterpret_cast<const int*>(player + kPlayerVehicle) != -1) {
        SetStatus("Get out of your vehicle first");
        return;
    }
    BYTE* vehicle = Vehicle(i);
    const std::string name = DisplayName(i);
    g_loading = nullptr;
    if (VehicleLoaded(vehicle) || !g_poolState) {
        Drop(name);
        return;
    }
    // While the area's resource is still changing (state 1-3, as after a reload) a model reserved can't stream
    // yet, and the change waits for what it doesn't want to be let go: reserve only once it's in force (4).
    if (g_poolState != kResourceInForce) {
        rfa::LogDetail("%s: not yet; this area's spawn resource is still changing (state %d)", name.c_str(), static_cast<int>(g_poolState));
        SetStatus("This area is still loading its people and vehicles; try again in a moment");
        return;
    }
    if (!ReserveInResource(kVehicleCategory, kOwner, vehicle)) {
        rfa::Log("%s: the game has no room for it in this area's spawn resource", name.c_str());
        SetStatus("No room for " + name + " here");
        return;
    }
    rfa::LogDetail("%s: added to this area's spawn resource; it spawns once it has streamed in", name.c_str());
    SetStatus("Loading " + name + "...");
    g_loading = vehicle;
    g_loadingName = name;
    g_loadingSince = GetTickCount64();
}

void Carry(const Request& request) {
    switch (request.kind) {
    case Request::kVehicle: SpawnVehicle(request.index); break;
    case Request::kItem:
        if (request.index < ItemCount()) {
            rfa::LogDetail("Dropping item %s", ItemName(request.index));
            RunCommand("drop_item %s", ItemName(request.index));
            SetStatus(std::string("Dropped ") + ItemName(request.index));
        }
        break;
    case Request::kWeapon:
        if (request.index < WeaponCount() && *WeaponItem(request.index)) {
            rfa::LogDetail("Giving weapon %s (%s)", WeaponName(request.index), WeaponItem(request.index));
            RunCommand("give_player_item %s", WeaponItem(request.index));
            SetStatus(std::string("Gave ") + WeaponName(request.index));
        }
        break;
    case Request::kRemove: {
        const unsigned handle = static_cast<unsigned>(request.index);
        Remove(handle);
        std::lock_guard<std::mutex> hold(g_lock);
        for (auto it = g_spawned.begin(); it != g_spawned.end(); ++it)
            if (it->handle == handle) {
                g_spawned.erase(it);
                break;
            }
        break;
    }
    case Request::kRemoveAll: {
        std::vector<Spawned> all;
        {
            std::lock_guard<std::mutex> hold(g_lock);
            all.swap(g_spawned);
        }
        for (const Spawned& one : all) Remove(one.handle);
        break;
    }
    }
}

void __cdecl DropStep() {
    std::deque<Request> requests;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        requests.swap(g_requests);
    }
    for (const Request& request : requests) Carry(request);
    if (g_loading) {
        if (VehicleLoaded(g_loading)) {
            g_loading = nullptr;
            Drop(g_loadingName);
        } else if (GetTickCount64() - g_loadingSince > kLoadTimeout) {
            g_loading = nullptr;
            rfa::Log("%s didn't stream in within %llu s", g_loadingName.c_str(), kLoadTimeout / 1000);
            SetStatus(g_loadingName + " didn't load");
        }
    }
    if (!g_expected.empty() && GetTickCount64() - g_expectedSince > kLoadTimeout) g_expected.clear();
    g_inDropStep = true;
    g_dropStep();
    g_inDropStep = false;
    const int free = PoolRoom(kVehiclePool, nullptr);
    std::lock_guard<std::mutex> hold(g_lock);
    g_slotsFree = free;
    g_slots = *reinterpret_cast<const int*>(kVehiclePool + kPoolSlots);
    g_stateShown = g_poolState;
}

// Notes the vehicle one of our spawns made (on the game's thread, inside drop_car's step).
BYTE* __cdecl CreateVehicle(BYTE* vehicle, const float* where, const float* orientation, int variant) {
    BYTE* made = g_createVehicle(vehicle, where, orientation, variant);
    if (made && g_inDropStep && !g_expected.empty()) {
        Spawned spawned = {*reinterpret_cast<const unsigned*>(made + kHandle), g_expected};
        g_expected.clear();
        unsigned oldest = 0;
        {
            std::lock_guard<std::mutex> hold(g_lock);
            if (static_cast<int>(g_spawned.size()) >= kMaxSpawned) {
                oldest = g_spawned.front().handle;
                g_spawned.erase(g_spawned.begin());
            }
            g_spawned.push_back(spawned);
        }
        if (oldest) Remove(oldest);
    }
    return made;
}

void __cdecl HumanDebug(void* object) {
    if (object && !CastHuman(kWorld, nullptr, object)) {
        RFA_LOG_DETAIL_FIRST(3, "Left out the game's debug update of a human it can't cast (a walker), which would crash it");
        return;
    }
    g_humanDebug(object);
}

}  // namespace

void spawner::Init() {
    g_dropStep = reinterpret_cast<DropStepFn>(rfa::HookFunction(kDropStep, kDropStepPrologue, reinterpret_cast<void*>(&DropStep)));
    if (!g_dropStep) {
        rfa::Log("Could not hook drop_car's step; the Spawn windows can't spawn");
        return;
    }
    g_createVehicle = reinterpret_cast<CreateVehicleFn>(
        rfa::HookFunction(kCreateVehicle, kCreateVehiclePrologue, reinterpret_cast<void*>(&CreateVehicle)));
    if (!g_createVehicle) rfa::Log("Could not hook the vehicle create; Spawned won't list what you spawn");
    g_humanDebug = reinterpret_cast<HumanDebugFn>(rfa::HookFunction(kHumanDebug, kHumanDebugPrologue, reinterpret_cast<void*>(&HumanDebug)));
    if (!g_humanDebug) rfa::Log("Could not hook the humans' debug update; a spawned walker may crash the game");
    rfa::LogDetail("Spawn windows ready: open the console, then Spawn in the bar at the bottom");
}

void spawner::Menu(const RFAMenu* menu) {
    if (!g_dropStep || !rfa::menu::Use(menu)) return;
    if (menu->stage == RFA_MENU_BAR) {
        if (ImGui::BeginMenu("Spawn")) {
            ImGui::MenuItem("Vehicles", nullptr, &g_vehiclesOpen);
            ImGui::MenuItem("Items", nullptr, &g_itemsOpen);
            ImGui::MenuItem("Weapons", nullptr, &g_weaponsOpen);
            ImGui::MenuItem("Spawned", nullptr, &g_spawnedOpen);
            ImGui::EndMenu();
        }
        return;
    }
    if (g_vehiclesOpen) DrawVehicles();
    if (g_itemsOpen) DrawItems();
    if (g_weaponsOpen) DrawWeapons();
    if (g_spawnedOpen) DrawSpawned();
}
