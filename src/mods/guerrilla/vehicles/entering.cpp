// Getting into and out of a vehicle in Guerrilla's world, as Guerrilla lets the player. A part of
// guerrilla_vehicles.
//
// Both games pick the player's use action (the prompt, and what the use key does: pick something up, open a
// door, get into a vehicle, ...) only from the kinds of action his state allows: a mask of kinds, kind n allowed
// when bit n is set (Armageddon: player +0x27C4, tested by 0x8BFF20 for player_process_object_interaction
// 0x8BF830; Guerrilla: player +0x24B0, player_is_usable_object_allowed RM 0xA103A0). Kind 6 is getting into a
// vehicle. Each frame the game sets the masks for the player's state (a table of functions from 0xF7C028).
//
// Guerrilla's standing player has kind 6 (set_control_permissions_stand, RM 0x6FBCC0: 0x7FFF8; only while he
// carries something that isn't in his inventory, 0x7FF88, without it). Armageddon's on-foot permissions
// (0x4D8830: 0x57F84 or 0x31FBC) leave it out: its own player gets into a vehicle only by script. Nothing in
// Armageddon puts it back but the console's Controls_use_vehicle_enabled (0x11BE7F0, off), a debug switch that
// adds it in every state, in a vehicle too.
//
// Getting out goes through the same bit. In a vehicle the use key is the vehicle action (Exit=,
// guerrilla_vehicles.cpp), and the game's use handler (0x4E12B0) gets the player out (player_use_vehicle 0x8B9680
// → vehicle_interaction_request_exit 0x656A20, which picks the normal or quick exit) only if bit 6 is set.
// Guerrilla's handler (control_gameplay_just_pressed_action, RM 0x6F7210) asks nothing: in a vehicle the action
// always calls player_use_vehicle. Armageddon's in-vehicle permissions (0x4D9840, and 0x4D9BB0 for the states
// around it) set the mask to nothing (bit 6 only with the console switch), so the player couldn't get out.
// Adding bit 6 there offers nothing else: in a vehicle the use-target search stops before it looks for
// vehicles (0x8BFA0F).
//
// So in the worlds Guerrilla's rules run in ([GuerrillaVehicles] TrafficWorlds=, rfg), and with
// [GuerrillaVehicles] EnterVehiclesEverywhere=1 (the default) in Armageddon's own worlds too, single player:
//   - on foot, when the game has given the player its on-foot permissions (those with kind 7, using things),
//     kind 6 is added, as Guerrilla's standing player has it;
//   - in a vehicle, bit 6 is added, so the vehicle action gets him out, as Guerrilla's.
//
// Getting into Armageddon's own vehicles. The use key asks the vehicle's interaction to take him in
// (player_use_vehicle 0x8B9680 → 0x656410 → vehicle_interaction_request_enter 0x655A00: the person, the vehicle,
// and the request, 16 bytes: the waypoint, -, the seat, flags). Without flag 1 ("T", teleport) he walks to the
// interaction's enter waypoint first and then plays its enter animation. Guerrilla's vehicles have their own
// interactions (vehicle_interactions.xtbl, GuerrillaBuilder.exe's): the waypoint is at the door and the animation
// takes him in. Armageddon's own vehicles (its flyers, the LAVs, the combugs, the bomber, ...) name the table's
// Dummy, a placeholder: an ATV's waypoint (a metre to the left of the middle) and no animations at all, as
// Armageddon puts its player into a vehicle only by script, which teleports him in. Its waypoint is in or under
// the hull of anything bigger than an ATV, so the player was led about until he got near it, and got in late or
// not at all. So the player's own request for a vehicle whose interaction is one of [GuerrillaVehicles]
// TeleportInteractions= (Dummy) gets flag 1, as the game's vehicles_always_teleport (0x14E67EC) gives everyone's:
// he is put in the seat at once.
#define RFA_MOD_NAME "guerrilla_vehicles"
#include "rfa_mod.h"
#include "game.h"
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace gv {

namespace {

constexpr DWORD kOnFootPermissions = 0x4D8830;  // cdecl, the player
constexpr size_t kOnFootPermissionsPrologue = 6;  // push ebp / mov ebp,esp / sub esp,20h
constexpr DWORD kUseKinds = 0x27C4;               // the player: the kinds of use action allowed
constexpr DWORD kEnterVehicle = 1u << 6, kUseThings = 1u << 7;
constexpr DWORD kInVehiclePermissions = 0x4D9840;   // cdecl, the player: in a vehicle
constexpr DWORD kNearVehiclePermissions = 0x4D9BB0; // cdecl, the player: the states around it
constexpr size_t kInVehiclePrologue = 6, kNearVehiclePrologue = 6;  // push ebp / mov ebp,esp / sub esp,1Ch; push ebp / mov ebp,esp / mov eax,[ebp+8]
using PermissionsFn = void(__cdecl*)(BYTE* player);
PermissionsFn g_onFootPermissions, g_inVehiclePermissions, g_nearVehiclePermissions;

bool Multiplayer() { return *reinterpret_cast<const BYTE*>(0x2685118) != 0; }

// Where the player may get in and out by himself: Guerrilla's world, and with [GuerrillaVehicles]
// EnterVehiclesEverywhere=1 (the default) Armageddon's worlds too.
bool g_everywhere = true;
bool EnteringWorld() { return g_everywhere || InTrafficWorld(); }

void __cdecl OnFootPermissions(BYTE* player) {
    g_onFootPermissions(player);
    if (!player || Multiplayer() || !EnteringWorld()) return;
    DWORD& kinds = *reinterpret_cast<DWORD*>(player + kUseKinds);
    if (!(kinds & kUseThings) || (kinds & kEnterVehicle)) return;
    kinds |= kEnterVehicle;
    RFA_LOG_DETAIL_FIRST(1, "Entering: on foot the player may get into a vehicle (use kind 6), as Guerrilla's standing player");
}

// In a vehicle: the vehicle action gets him out, as Guerrilla's.
void LetOut(BYTE* player) {
    if (!player || Multiplayer() || !EnteringWorld()) return;
    DWORD& kinds = *reinterpret_cast<DWORD*>(player + kUseKinds);
    if (kinds & kEnterVehicle) return;
    kinds |= kEnterVehicle;
    RFA_LOG_DETAIL_FIRST(1, "Entering: in a vehicle the vehicle action gets the player out, as Guerrilla's");
}
void __cdecl InVehiclePermissions(BYTE* player) {
    g_inVehiclePermissions(player);
    LetOut(player);
}
void __cdecl NearVehiclePermissions(BYTE* player) {
    g_nearVehiclePermissions(player);
    LetOut(player);
}

// ---- Getting into Armageddon's own vehicles at once
constexpr DWORD kRequestEnter = 0x656410;      // cdecl bool (person, vehicle, request*)
constexpr size_t kRequestEnterPrologue = 7;     // push ebp / mov ebp,esp / push ecx / mov eax,[ebp+10h]
constexpr DWORD kVehicleInfo = 0x3DA8;          // a vehicle: its vehicles.xtbl entry
constexpr DWORD kInfoInteraction = 0x3C;        // the entry: its interaction's record (the name first)
constexpr DWORD kRequestSeat = 0;               // the request: the seat asked for (0 the driver's)
constexpr DWORD kRequestFlags = 12;             // the request: its flags (1: teleport)
constexpr DWORD kInfoFlags = 0x874;             // the entry: flags (bit 3: Player_Passenger_Only)
constexpr BYTE kPassengerOnly = 1u << 3;
constexpr DWORD kRecordSeats = 0x1C;            // the interaction's record: how many seats
constexpr BYTE kTeleport = 1;
using RequestEnterFn = bool(__cdecl*)(BYTE* person, BYTE* vehicle, BYTE* request);
RequestEnterFn g_requestEnter;
std::vector<std::string> g_teleportInteractions;

// The name of a vehicle's interaction (vehicle_interactions.xtbl), or null.
const char* InteractionName(BYTE* vehicle) {
    const BYTE* info = vehicle ? *reinterpret_cast<BYTE* const*>(vehicle + kVehicleInfo) : nullptr;
    const BYTE* record = info ? *reinterpret_cast<const BYTE* const*>(info + kInfoInteraction) : nullptr;
    return record ? *reinterpret_cast<const char* const*>(record) : nullptr;
}

bool __cdecl RequestEnter(BYTE* person, BYTE* vehicle, BYTE* request) {
    if (person && request && vehicle && person == Player() && !Multiplayer() && EnteringWorld()) {
        // Player_Passenger_Only (the entry's +0x874, bit 3): player_use_vehicle asks for seat 1 (passenger_1), and
        // a vehicle whose interaction has no such seat (the LAVs: Dummy, the driver's alone) refused him ("xSeat NA").
        // There he takes the seat it has, the driver's (the LAV's gun is on it).
        const BYTE* info = *reinterpret_cast<BYTE* const*>(vehicle + kVehicleInfo);
        const BYTE* record = info ? *reinterpret_cast<const BYTE* const*>(info + kInfoInteraction) : nullptr;
        int& seat = *reinterpret_cast<int*>(request + kRequestSeat);
        if (info && record && (info[kInfoFlags] & kPassengerOnly) && seat >= *reinterpret_cast<const int*>(record + kRecordSeats)) {
            RFA_LOG_FIRST(2, "Entering: %s is for the player as a passenger only, and its interaction (%s) has no passenger's seat: "
                             "he takes the driver's", *reinterpret_cast<const char* const*>(info) ? *reinterpret_cast<const char* const*>(info) : "?",
                          InteractionName(vehicle) ? InteractionName(vehicle) : "?");
            seat = 0;
        }
    }
    if (person && request && person == Player() && !Multiplayer() && EnteringWorld() && !(request[kRequestFlags] & kTeleport)) {
        const char* name = InteractionName(vehicle);
        for (const std::string& teleport : g_teleportInteractions)
            if (name && _stricmp(name, teleport.c_str()) == 0) {
                request[kRequestFlags] |= kTeleport;
                RFA_LOG_FIRST(1, "Entering: the player gets into Armageddon's own vehicles (interaction %s) at once, as its scripts put him in", name);
                break;
            }
    }
    return g_requestEnter(person, vehicle, request);
}

void StartTeleportEntering() {
    const std::wstring list = rfa::IniString(L"GuerrillaVehicles", L"TeleportInteractions", L"Dummy");
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(L',', start);
        if (end == std::wstring::npos) end = list.size();
        std::wstring part = list.substr(start, end - start);
        while (!part.empty() && iswspace(part.front())) part.erase(part.begin());
        while (!part.empty() && iswspace(part.back())) part.pop_back();
        if (!part.empty()) {
            std::string name;
            for (wchar_t c : part) name += static_cast<char>(c);  // the names are ASCII
            g_teleportInteractions.push_back(name);
        }
        start = end + 1;
    }
    if (g_teleportInteractions.empty()) {
        rfa::Log("Entering: the player walks to every vehicle's enter point ([GuerrillaVehicles] TeleportInteractions= is empty)");
        return;
    }
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x10};
    if (memcmp(reinterpret_cast<const void*>(kRequestEnter), opening, sizeof(opening)) != 0 ||
        !(g_requestEnter = reinterpret_cast<RequestEnterFn>(rfa::HookFunction(kRequestEnter, kRequestEnterPrologue, reinterpret_cast<void*>(&RequestEnter))))) {
        rfa::Log("Entering: the game's vehicle enter request isn't as expected; the player walks to Armageddon's own vehicles' enter point");
        return;
    }
    rfa::Log("Entering: the player gets into vehicles whose interaction is %ls at once ([GuerrillaVehicles] TeleportInteractions=)", list.c_str());
}

template <typename Fn>
Fn Hook(DWORD address, size_t prologue, const BYTE* opening, size_t size, void* replacement) {
    if (memcmp(reinterpret_cast<const void*>(address), opening, size) != 0) return nullptr;
    return reinterpret_cast<Fn>(rfa::HookFunction(address, prologue, replacement));
}

}  // namespace

void StartEntering() {
    g_everywhere = rfa::IniInt(L"GuerrillaVehicles", L"EnterVehiclesEverywhere", 1) != 0;
    if (!rfa::IniInt(L"GuerrillaVehicles", L"EnterVehicles", 1)) {
        rfa::LogDetail("Entering: the player gets into and out of a vehicle only as Armageddon's allows ([GuerrillaVehicles] EnterVehicles=0)");
        return;
    }
    const BYTE onFoot[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20};
    const BYTE inVehicle[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C};
    const BYTE nearVehicle[] = {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08};
    g_onFootPermissions = Hook<PermissionsFn>(kOnFootPermissions, kOnFootPermissionsPrologue, onFoot, sizeof(onFoot), reinterpret_cast<void*>(&OnFootPermissions));
    g_inVehiclePermissions = Hook<PermissionsFn>(kInVehiclePermissions, kInVehiclePrologue, inVehicle, sizeof(inVehicle), reinterpret_cast<void*>(&InVehiclePermissions));
    g_nearVehiclePermissions =
        Hook<PermissionsFn>(kNearVehiclePermissions, kNearVehiclePrologue, nearVehicle, sizeof(nearVehicle), reinterpret_cast<void*>(&NearVehiclePermissions));
    if (g_onFootPermissions)
        rfa::LogDetail(g_everywhere ? "Entering: in every world the player may get into a vehicle on foot, as Guerrilla's ([GuerrillaVehicles] EnterVehiclesEverywhere=1)"
                                    : "Entering: in Guerrilla's world the player may get into a vehicle on foot, as Guerrilla's");
    else
        rfa::Log("Entering: the game's on-foot permissions aren't as expected; getting into a vehicle needs the console's Controls_use_vehicle_enabled");
    if (g_inVehiclePermissions && g_nearVehiclePermissions)
        rfa::LogDetail("Entering: the vehicle action gets the player out, as Guerrilla's");
    else
        rfa::Log("Entering: the game's in-vehicle permissions aren't as expected; getting out may need the console's Controls_use_vehicle_enabled");
    StartTeleportEntering();
}

}  // namespace gv
