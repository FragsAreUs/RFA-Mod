#define RFA_MOD_NAME "guerrilla_gear"
// Guerrilla's "auto-aim curved trail" guns (its only one: the Enforcer) draw their tracers Guerrilla's way.
//
// Guerrilla's weapon::fire_bullet (RM 0xBEA92C) sends a gun with that flag (weapon info +0x0D bit 0x20) to
// weapon::create_curved_tracers (RM 0xBDC710), never to create_tracers. Its weapon_create_curved_tracer_base
// (RM 0xBDC110) shapes the curve, but plays a tracer only for a player wearing a multiplayer backpack whose
// abilities name a bullet_tracer (player +0x1170 → backpack_info → +0xEC), which only MP Commando's active one does.
// So in the campaign the Enforcer draws no tracer at all: its own Tracer_Effect is never used.
//
// Armageddon's fire_bullet (0x721400) instead takes its curved path (0x731D30) only when the aim found a target, and
// otherwise the straight tracer (0x731F00), both with the gun's own Tracer_Effect. For a Guerrilla gun with the flag
// (its firing mode's flags +4 bit 2, "auto aim curved trail" in our weapons.xtbl), both now do what Guerrilla's
// does without a backpack: nothing. Armageddon's own guns (its Enforcer has the flag too) are untouched.
#include "rfa_mod.h"
#include "guerrilla_gear.h"
#include <cstring>

namespace {

constexpr DWORD kStraightTracers = 0x731F00;  // a weapon's bullet tracer (thiscall: owner, start, end, type, effect), ret 0x14
constexpr DWORD kCurvedTracers = 0x731D30;    // its curved one (thiscall: owner, start, aim point, end, type, effect), ret 0x18
constexpr size_t kPrologue = 6;               // push ebp / mov ebp,esp / sub esp,10h (both)

constexpr DWORD kWeaponInfo = 0x188;          // a weapon -> its weapons.xtbl entry (+0 its name)
constexpr DWORD kShotFlags = 4;               // a firing mode's shot flags
constexpr BYTE kCurvedTrail = 2;              // "auto aim curved trail" (the reader at 0x74FF6F)
auto const ShotInfo = reinterpret_cast<BYTE*(__fastcall*)(BYTE* weapon, void* unused)>(0x42A190);  // the current firing mode's

using StraightFn = void(__fastcall*)(BYTE* weapon, void* unused, int owner, const float* start, const float* end, int type, int effect);
using CurvedFn = void(__fastcall*)(BYTE* weapon, void* unused, int owner, const float* start, const float* aim, const float* end, int type, int effect);
StraightFn g_straight;
CurvedFn g_curved;

// Whether this shot is a Guerrilla gun's with the curved trail: ours are the weapons.xtbl entries named rfg_.
bool GuerrillaCurvedTrail(BYTE* weapon) {
    const BYTE* info = weapon ? *reinterpret_cast<const BYTE* const*>(weapon + kWeaponInfo) : nullptr;
    const char* name = info ? *reinterpret_cast<const char* const*>(info) : nullptr;
    if (!name || _strnicmp(name, "rfg_", 4) != 0) return false;
    const BYTE* shot = ShotInfo(weapon, nullptr);
    return shot && (shot[kShotFlags] & kCurvedTrail);
}

void __fastcall StraightTracers(BYTE* weapon, void* unused, int owner, const float* start, const float* end, int type, int effect) {
    if (GuerrillaCurvedTrail(weapon)) return;  // Guerrilla's create_curved_tracers: no backpack tracer, so none
    g_straight(weapon, unused, owner, start, end, type, effect);
}

void __fastcall CurvedTracers(BYTE* weapon, void* unused, int owner, const float* start, const float* aim, const float* end, int type, int effect) {
    if (GuerrillaCurvedTrail(weapon)) return;
    g_curved(weapon, unused, owner, start, aim, end, type, effect);
}

bool Opens(DWORD address, const BYTE* opening, size_t size) { return memcmp(reinterpret_cast<const void*>(address), opening, size) == 0; }

}  // namespace

void curved_trail::Init() {
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10};
    if (Opens(kStraightTracers, opening, sizeof(opening)) && Opens(kCurvedTracers, opening, sizeof(opening))) {
        g_straight = reinterpret_cast<StraightFn>(rfa::HookFunction(kStraightTracers, kPrologue, reinterpret_cast<void*>(&StraightTracers)));
        g_curved = reinterpret_cast<CurvedFn>(rfa::HookFunction(kCurvedTracers, kPrologue, reinterpret_cast<void*>(&CurvedTracers)));
    }
    if (!g_straight || !g_curved) rfa::Log("Curved trail: the game's tracer code isn't as expected; the Enforcer draws Armageddon's tracers");
}
