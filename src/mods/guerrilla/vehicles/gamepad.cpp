// Guerrilla's controller layout in vehicles (Xbox / XInput), for Guerrilla's vehicles and Armageddon's alike. A part of
// guerrilla_vehicles.dll.
//
// Armageddon's controller layouts (four schemes, Options > Controls) are set by 0x4E82A0 (cdecl: the scheme 0..3, the
// player's control set 0..1): the shared table (0xF7D550), then the scheme's on-foot, axis, vehicle (from 0xF7EA88:
// table, count per scheme) and exo tables, each row the action, the button, a second button and a stick direction
// (16 bytes), written into the control set (0x11BEBA0 + action * 20 + set * 0xCF0: +0 the button, +4 the second,
// +0x10 the stick direction) by 0x4E8480, each button through 0x4E6CD0 (with Options' swap of the bumpers and
// triggers, the byte at 0x11C058D: 4 <-> 6, 5 <-> 7, except the menus' actions 0x68 .. 0x7F). The buttons are the
// input layer's numbers, the same in both games: 0 A, 1 B, 2 X, 3 Y, 4 LB, 5 RB, 6 LT, 7 RT, 8 .. 11 the d-pad's
// right, up, left, down, 12 Start, 13 Back, 14 the left stick's click, 15 the right stick's.
//
// Armageddon's vehicle layout is made for its walkers and its tank: A to go, B to brake, X to get out, RT to fire,
// LT to zoom; no handbrake and no seat change. Guerrilla's (rfg.exe Control_button_default_configs, its Xbox 360
// and Xbox One table, 0x12D1DF8: CBA_VDC_*) is a driver's: RT accelerates, LT brakes and reverses, X is the
// handbrake, A fires the vehicle's gun, Y gets in and out, LB changes seats, RB looks back, the right stick's click
// cycles the camera; a walker's four melee attacks LT, RT, RB, LB and its charge A; a tank's guns RT and LT.
// After the game sets a layout, the vehicles' actions are set to Guerrilla's ([GuerrillaVehicles] GuerrillaPad=1),
// and Armageddon's own vehicle actions Guerrilla hasn't are moved off the buttons Guerrilla's now use:
//   veh_reload (X, the handbrake's) and the zoom (LT, the brake's): none; the force field's overcharge (A) to B;
//   the vision mode (LB or Y) to the d-pad's up; the walker's shockwave (the right stick's click) to the left
//   stick's click; the tank's alternate fire buttons none; a flyer's exit (veh_flyer_action, X) to Y, as getting
//   out of anything else. A flyer's own up, down and guns are Armageddon's (Guerrilla's player never flew).
// Options' swap of the bumpers and triggers is kept, as the game's own layouts keep it.
#define RFA_MOD_NAME "guerrilla_vehicles"
#include "rfa_mod.h"
#include <cstring>

namespace gv {

namespace {

constexpr DWORD kSetLayout = 0x4E82A0;  // cdecl (scheme, control set)
constexpr size_t kSetLayoutPrologue = 7;  // push ebp / mov ebp,esp / cmp dword ptr [ebp+8],0
using SetLayoutFn = void(__cdecl*)(int scheme, int set);
SetLayoutFn g_setLayout;
auto const SetButton = reinterpret_cast<void(__cdecl*)(int action, int button, int second, int stick, int set)>(0x4E8480);
constexpr DWORD kSwapBumpersTriggers = 0x11C058D;

enum Button { None = -1, A = 0, B = 1, X = 2, Y = 3, LB = 4, RB = 5, LT = 6, RT = 7, DpadUp = 9, LeftClick = 14, RightClick = 15 };

struct Binding {
    int action;  // Armageddon's (the names at 0xF7F0B8)
    int button;
};
constexpr Binding kGuerrilla[] = {
    {62, RT},          // veh_accelerate          CBA_VDC_ACCELERATE
    {63, LT},          // veh_brake               CBA_VDC_BRAKE_REVERSE
    {64, X},           // veh_handbrake           CBA_VDC_HANDBRAKE
    {68, A},           // veh_firearm             CBA_VDC_FIREARM
    {70, RightClick},  // veh_camera              CBA_VDC_CYCLE_CAMERA_MODE
    {71, RB},          // veh_rear_view           CBA_VDC_LOOK_BACK
    {86, Y},           // veh_action              CBA_VDC_ACTION
    {90, LB},          // veh_change_seat         CBA_VDC_CHANGE_SEAT
    {76, LT},          // veh_mech_attack_1       CBA_VDC_MECH_MELEE1
    {77, RT},          // veh_mech_attack_2       CBA_VDC_MECH_MELEE2
    {78, RB},          // veh_mech_attack_3       CBA_VDC_MECH_MELEE3
    {79, LB},          // veh_mech_attack_4       CBA_VDC_MECH_MELEE4
    {80, A},           // veh_mech_charge         CBA_VDC_MECH_CHARGE
    {82, RT},          // veh_tank_fire_pri       CBA_VDC_TANK_FIRE_PRIMARY
    {83, LT},          // veh_tank_fire_sec       CBA_VDC_TANK_FIRE_SECONDARY
    // Armageddon's own, moved off Guerrilla's buttons
    {99, None},        // veh_reload (X)
    {88, None},        // veh_zoom (LT)
    {89, None},        // veh_zoom_toggle (LT)
    {84, None},        // veh_tank_fire_pri_alt (RT)
    {85, None},        // veh_tank_fire_sec_alt
    {100, B},          // veh_force_field_overcharge (A)
    {101, DpadUp},     // veh_vision_mode (LB, or Y)
    {81, LeftClick},   // veh_mech_shockwave (the right stick's click)
    {97, Y},           // veh_flyer_action (X): out of a flyer as out of anything else
};

int Swapped(int action, int button) {
    if (!*reinterpret_cast<const BYTE*>(kSwapBumpersTriggers) || (action >= 0x68 && action <= 0x7F)) return button;
    switch (button) {
    case 4: return 6;
    case 5: return 7;
    case 6: return 4;
    case 7: return 5;
    default: return button;
    }
}

void __cdecl SetLayout(int scheme, int set) {
    g_setLayout(scheme, set);
    if (set < 0 || set > 1) return;
    for (const Binding& b : kGuerrilla) SetButton(b.action, b.button < 0 ? -1 : Swapped(b.action, b.button), -1, -1, set);
    RFA_LOG_FIRST(4, "Gamepad: Guerrilla's controller layout in vehicles (layout %d, control set %d): RT accelerate, LT brake and reverse, "
                     "X handbrake, A fire, Y get out, LB change seat, RB look back, right stick click camera", scheme + 1, set);
}

}  // namespace

void StartGamepad() {
    if (!rfa::IniInt(L"GuerrillaVehicles", L"GuerrillaPad", 1)) {
        rfa::Log("Gamepad: Armageddon's controller layouts in vehicles ([GuerrillaVehicles] GuerrillaPad=0)");
        return;
    }
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x08, 0x00};
    if (memcmp(reinterpret_cast<const void*>(kSetLayout), opening, sizeof(opening)) != 0 ||
        !(g_setLayout = reinterpret_cast<SetLayoutFn>(rfa::HookFunction(kSetLayout, kSetLayoutPrologue, reinterpret_cast<void*>(&SetLayout))))) {
        rfa::Log("Gamepad: the game's controller layouts aren't as expected; vehicles keep Armageddon's controller layout");
        return;
    }
    rfa::Log("Gamepad: vehicles (Guerrilla's and Armageddon's) take Guerrilla's controller layout, whichever of Armageddon's is chosen ([GuerrillaVehicles] GuerrillaPad=1)");
}

}  // namespace gv
