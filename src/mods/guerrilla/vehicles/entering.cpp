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
// So in the worlds Guerrilla's rules run in ([GuerrillaVehicles] TrafficWorlds=, rfg), single player:
//   - on foot, when the game has given the player its on-foot permissions (those with kind 7, using things),
//     kind 6 is added, as Guerrilla's standing player has it;
//   - in a vehicle, bit 6 is added, so the vehicle action gets him out, as Guerrilla's.
#define RFA_MOD_NAME "guerrilla_vehicles"
#include "rfa_mod.h"
#include "game.h"
#include <cstring>

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

void __cdecl OnFootPermissions(BYTE* player) {
    g_onFootPermissions(player);
    if (!player || Multiplayer() || !InTrafficWorld()) return;
    DWORD& kinds = *reinterpret_cast<DWORD*>(player + kUseKinds);
    if (!(kinds & kUseThings) || (kinds & kEnterVehicle)) return;
    kinds |= kEnterVehicle;
    RFA_LOG_FIRST(1, "Entering: on foot in Guerrilla's world the player may get into a vehicle (use kind 6), as Guerrilla's standing player");
}

// In a vehicle: the vehicle action gets him out, as Guerrilla's.
void LetOut(BYTE* player) {
    if (!player || Multiplayer() || !InTrafficWorld()) return;
    DWORD& kinds = *reinterpret_cast<DWORD*>(player + kUseKinds);
    if (kinds & kEnterVehicle) return;
    kinds |= kEnterVehicle;
    RFA_LOG_FIRST(1, "Entering: in a vehicle in Guerrilla's world the vehicle action gets the player out, as Guerrilla's");
}
void __cdecl InVehiclePermissions(BYTE* player) {
    g_inVehiclePermissions(player);
    LetOut(player);
}
void __cdecl NearVehiclePermissions(BYTE* player) {
    g_nearVehiclePermissions(player);
    LetOut(player);
}

template <typename Fn>
Fn Hook(DWORD address, size_t prologue, const BYTE* opening, size_t size, void* replacement) {
    if (memcmp(reinterpret_cast<const void*>(address), opening, size) != 0) return nullptr;
    return reinterpret_cast<Fn>(rfa::HookFunction(address, prologue, replacement));
}

}  // namespace

void StartEntering() {
    if (!rfa::IniInt(L"GuerrillaVehicles", L"EnterVehicles", 1)) {
        rfa::Log("Entering: the player gets into and out of a vehicle only as Armageddon's allows ([GuerrillaVehicles] EnterVehicles=0)");
        return;
    }
    const BYTE onFoot[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20};
    const BYTE inVehicle[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C};
    const BYTE nearVehicle[] = {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08};
    g_onFootPermissions = Hook<PermissionsFn>(kOnFootPermissions, kOnFootPermissionsPrologue, onFoot, sizeof(onFoot), reinterpret_cast<void*>(&OnFootPermissions));
    g_inVehiclePermissions = Hook<PermissionsFn>(kInVehiclePermissions, kInVehiclePrologue, inVehicle, sizeof(inVehicle), reinterpret_cast<void*>(&InVehiclePermissions));
    g_nearVehiclePermissions =
        Hook<PermissionsFn>(kNearVehiclePermissions, kNearVehiclePrologue, nearVehicle, sizeof(nearVehicle), reinterpret_cast<void*>(&NearVehiclePermissions));
    rfa::Log(g_onFootPermissions ? "Entering: in Guerrilla's world the player may get into a vehicle on foot, as Guerrilla's"
                                 : "Entering: the game's on-foot permissions aren't as expected; getting into a vehicle needs the console's Controls_use_vehicle_enabled");
    rfa::Log(g_inVehiclePermissions && g_nearVehiclePermissions
                 ? "Entering: in Guerrilla's world the vehicle action gets the player out, as Guerrilla's"
                 : "Entering: the game's in-vehicle permissions aren't as expected; getting out may need the console's Controls_use_vehicle_enabled");
}

}  // namespace gv
