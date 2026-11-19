// What Guerrilla's activities hear of the game's damage (its activity_register_* dispatchers), from Armageddon's own
// functions: each wrapped once at start-up (whole-function wraps: the game's function first or last, then a report),
// the report posted to the activities' framework (activities.cpp, told on the main thread: these may run in the
// physics jobs).
//
// Guerrilla's callers and Armageddon's matches (Armageddon publicbeta):
// - vehicle_apply_damage (RM 0xBC0880 → register_damage(vehicle, attacker, its damage)): 0x6077A0, cdecl, 11
//   arguments (the vehicle, its part, the attacker, the weapon, the kind, where, the direction, a scale, the damage,
//   the impulse, a multiplayer flag); it changes its damage inside its own frame, so the damage done is the hit
//   points before less after (0x603920).
// - vehicle_blow_up (RM 0xBB7195 → register_death(vehicle, killer)): 0x5FD1F0, cdecl (the vehicle, the killer, a
//   flag, the weapon, exploding); it returns at once when its flags +0x4FB0 bit 9 or +0x4FB4 bit 30 are set on entry,
//   so then nothing is reported; it calls itself for what it drags along, each once.
// - object_mover::process_bullet_hit, process_melee_hit (vtable +0x74, +0x78 of both the buildings' and the props'
//   movers) and object_mover_process_explosion_hit (RM 0xAA266E, 0xA13A4A, 0xAA27C1 → register_damage(attacker,
//   mover, the raw damage)): 0x8D9280 (thiscall: the attacker, the damage, the fire info), 0x8DA940 (thiscall, 8:
//   the attacker, the damage, ...), 0x8D9450 (cdecl, 8: the mover, the attacker, the damage, ...).
// - rfg_dyn_apply_shape_cutter (RM 0x733CDE → mark_objects(the player if the cut is his, else null; the mover)):
//   Armageddon's cutter (0x67A5D0) still calls 0x9C7CD0(the cut's player or null, the mover) there (its return
//   address 0x67A6D5); only that call is taken.
// - rfg_mover::do_frame_report (RM 0x71F74B → register_building_damage(mover, its damage fraction's change, the
//   fraction before)): 0x68AE10, thiscall, no arguments: the fraction (+0x13C) before and after; nothing when the
//   mover makes no reports (+0xF0 bit 2) or has no destruction data (0x42A1F0 null), as Guerrilla returns then.
// - set_game_destroyed_common (RM 0x716D45 → register_building_destroyed(mover)): 0x68B2E0, cdecl (the mover, destroyed):
//   reported when it becomes destroyed (true, the flag +0x150 bit 4 clear on entry).
// - explosion_create_internal (RM 0x6EFC2D → register_explosion(owner, info)): 0x6E5060, cdecl, 8 (the info, the
//   owner, the source, ...).
// Elsewhere: the human damage (0x85E0C0) is guerrilla_vehicles' wrap (riders.cpp), a prop's damage (0x8CEC80)
// guerrilla_world's: each calls GuerrillaCampaign_HumanDamaged / _PropDamaged (campaign_api.h); the human death
// district_sources'. Not matched yet: Guerrilla's vehicle collision mark (havok_collide_vehicle_apply_damage, a fixed
// 200), its building-piece-against-building marks (rfg_dyn_apply_object_complex), an item's bullet and melee hits.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "campaign_api.h"
#include "activity_base.h"
#include <intrin.h>
#include <string.h>

#pragma intrinsic(_ReturnAddress)

namespace campaign::activities {
namespace {

template <typename Fn>
Fn Wrap(const char* name, DWORD address, std::initializer_list<BYTE> prologue, void* hook) {
    const std::vector<BYTE> expected(prologue);
    if (memcmp(reinterpret_cast<const void*>(address), expected.data(), expected.size()) != 0) {
        rfa::Log("Damage feeds: the game's %s (0x%X) isn't as expected; the activities don't hear of it", name, address);
        return nullptr;
    }
    Fn original = reinterpret_cast<Fn>(rfa::HookFunction(address, expected.size(), hook));
    if (!original) rfa::Log("Damage feeds: could not wrap the game's %s (0x%X); the activities don't hear of it", name, address);
    return original;
}

// ---- Vehicles

using VehicleDamageFn = bool(__cdecl*)(BYTE* vehicle, int part, BYTE* attacker, void* weapon, int kind, const float* at, const float* direction,
                                       float scale, int damage, const float* impulse, DWORD multiplayer);
VehicleDamageFn g_vehicleDamage;
auto const HitPoints = reinterpret_cast<int(__cdecl*)(BYTE* vehicle)>(0x603920);

bool __cdecl VehicleDamage(BYTE* vehicle, int part, BYTE* attacker, void* weapon, int kind, const float* at, const float* direction, float scale,
                           int damage, const float* impulse, DWORD multiplayer) {
    const int before = vehicle ? HitPoints(vehicle) : 0;
    const bool result = g_vehicleDamage(vehicle, part, attacker, weapon, kind, at, direction, scale, damage, impulse, multiplayer);
    if (vehicle) {
        const int done = before - HitPoints(vehicle);
        if (done > 0) ReportDamage(vehicle, attacker, static_cast<float>(done));
    }
    return result;
}

using BlowUpFn = void(__cdecl*)(BYTE* vehicle, BYTE* killer, DWORD flag, void* weapon, DWORD exploding);
BlowUpFn g_blowUp;
constexpr DWORD kVehicleFlags = 0x4FB0, kVehicleFlags2 = 0x4FB4;
constexpr DWORD kBlownUp = 1u << 9, kNoBlowUp = 1u << 30;  // its early outs

void __cdecl BlowUp(BYTE* vehicle, BYTE* killer, DWORD flag, void* weapon, DWORD exploding) {
    const bool goesOn = vehicle && !(*reinterpret_cast<const DWORD*>(vehicle + kVehicleFlags) & kBlownUp) &&
                        !(*reinterpret_cast<const DWORD*>(vehicle + kVehicleFlags2) & kNoBlowUp);
    g_blowUp(vehicle, killer, flag, weapon, exploding);
    if (goesOn) ReportDeath(vehicle, killer);
}

// ---- The movers' hits

using BulletHitFn = DWORD(__fastcall*)(BYTE* mover, void* unused, BYTE* attacker, float damage, void* info);
BulletHitFn g_bulletHit;
DWORD __fastcall BulletHit(BYTE* mover, void* unused, BYTE* attacker, float damage, void* info) {
    ReportDamage(attacker, mover, damage);
    return g_bulletHit(mover, unused, attacker, damage, info);
}

using MeleeHitFn = DWORD(__fastcall*)(BYTE* mover, void* unused, BYTE* attacker, float damage, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f);
MeleeHitFn g_meleeHit;
DWORD __fastcall MeleeHit(BYTE* mover, void* unused, BYTE* attacker, float damage, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f) {
    ReportDamage(attacker, mover, damage);
    return g_meleeHit(mover, unused, attacker, damage, a, b, c, d, e, f);
}

using ExplosionHitFn = DWORD(__cdecl*)(BYTE* mover, BYTE* attacker, float damage, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e);
ExplosionHitFn g_explosionHit;
DWORD __cdecl ExplosionHit(BYTE* mover, BYTE* attacker, float damage, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e) {
    ReportDamage(attacker, mover, damage);
    return g_explosionHit(mover, attacker, damage, a, b, c, d, e);
}

using CutMarkFn = DWORD(__cdecl*)(BYTE* player, BYTE* mover);
CutMarkFn g_cutMark;
constexpr DWORD kFromShapeCutter = 0x67A6D5;
DWORD __cdecl CutMark(BYTE* player, BYTE* mover) {
    if (reinterpret_cast<DWORD>(_ReturnAddress()) == kFromShapeCutter && mover) MarkObjects(player, mover);
    return g_cutMark(player, mover);
}

// ---- The buildings' damage and destruction

using FrameReportFn = DWORD(__fastcall*)(BYTE* mover, void* unused);
FrameReportFn g_frameReport;
constexpr DWORD kDamageFraction = 0x13C, kMoverFlags = 0xF0, kNoReports = 1u << 2;
auto const DestructionData = reinterpret_cast<void*(__fastcall*)(BYTE* mover, void* unused)>(0x42A1F0);

DWORD __fastcall FrameReport(BYTE* mover, void* unused) {
    const bool reports = mover && !(*reinterpret_cast<const DWORD*>(mover + kMoverFlags) & kNoReports) && DestructionData(mover, nullptr);
    const float before = reports ? *reinterpret_cast<const float*>(mover + kDamageFraction) : 0.0f;
    const DWORD result = g_frameReport(mover, unused);
    if (reports) ReportBuildingDamage(mover, *reinterpret_cast<const float*>(mover + kDamageFraction) - before, before);
    return result;
}

using SetDestroyedFn = DWORD(__cdecl*)(BYTE* mover, DWORD destroyed);
SetDestroyedFn g_setDestroyed;
constexpr DWORD kBuildingFlags = 0x150, kGameDestroyed = 1u << 4;
DWORD __cdecl SetDestroyed(BYTE* mover, DWORD destroyed) {
    const bool becomes = mover && (destroyed & 0xFF) && !(*reinterpret_cast<const DWORD*>(mover + kBuildingFlags) & kGameDestroyed);
    const DWORD result = g_setDestroyed(mover, destroyed);
    if (becomes) ReportBuildingDestroyed(mover);
    return result;
}

// ---- Explosions

using ExplosionFn = DWORD(__cdecl*)(void* info, BYTE* owner, BYTE* source, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e);
ExplosionFn g_explosion;
DWORD __cdecl Explosion(void* info, BYTE* owner, BYTE* source, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e) {
    ReportExplosion(owner);
    return g_explosion(info, owner, source, a, b, c, d, e);
}

}  // namespace

void StartDamageFeeds() {
    g_vehicleDamage = Wrap<VehicleDamageFn>("vehicle damage", 0x6077A0, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xE0, 0x02, 0x00, 0x00},
                                            reinterpret_cast<void*>(&VehicleDamage));
    g_blowUp = Wrap<BlowUpFn>("vehicle blow-up", 0x5FD1F0, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xA8, 0x01, 0x00, 0x00}, reinterpret_cast<void*>(&BlowUp));
    g_bulletHit = Wrap<BulletHitFn>("mover bullet hit", 0x8D9280, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x4C}, reinterpret_cast<void*>(&BulletHit));
    g_meleeHit = Wrap<MeleeHitFn>("mover melee hit", 0x8DA940, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10}, reinterpret_cast<void*>(&MeleeHit));
    g_explosionHit = Wrap<ExplosionHitFn>("mover explosion hit", 0x8D9450, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00},
                                          reinterpret_cast<void*>(&ExplosionHit));
    g_cutMark = Wrap<CutMarkFn>("shape cutter's player mark", 0x9C7CD0, {0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x0C, 0x00}, reinterpret_cast<void*>(&CutMark));
    g_frameReport = Wrap<FrameReportFn>("building damage report", 0x68AE10, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28}, reinterpret_cast<void*>(&FrameReport));
    g_setDestroyed = Wrap<SetDestroyedFn>("building destroyed", 0x68B2E0, {0x55, 0x8B, 0xEC, 0x0F, 0xB6, 0x45, 0x0C}, reinterpret_cast<void*>(&SetDestroyed));
    g_explosion = Wrap<ExplosionFn>("explosion", 0x6E5060, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x14, 0x03, 0x00, 0x00}, reinterpret_cast<void*>(&Explosion));
    const int wrapped = (g_vehicleDamage ? 1 : 0) + (g_blowUp ? 1 : 0) + (g_bulletHit ? 1 : 0) + (g_meleeHit ? 1 : 0) + (g_explosionHit ? 1 : 0) +
                        (g_cutMark ? 1 : 0) + (g_frameReport ? 1 : 0) + (g_setDestroyed ? 1 : 0) + (g_explosion ? 1 : 0);
    rfa::Log("Damage feeds: %d of 9 of the game's damage functions wrapped for the activities", wrapped);
}

}  // namespace campaign::activities

// ---- For the other parts' wraps

void GuerrillaCampaign_HumanDamaged(const void* record, float done) {
    // human_damage RM 0xACF4A2-0xACF4E3: activity_register_damage(victim, attacker, the damage done) when he lives on
    // and is an NPC. Armageddon's record: +0 the victim, +4 the attacker (the function may set it itself: read after),
    // the victim's hit points +0xF24, an NPC by 0x461130.
    using namespace campaign::activities;
    if (!record || !guerrilla_campaign::Running() || !(done > 0.0f)) return;
    BYTE* victim = *static_cast<BYTE* const*>(record);
    BYTE* attacker = *reinterpret_cast<BYTE* const*>(static_cast<const BYTE*>(record) + 4);
    if (!victim || *reinterpret_cast<const float*>(victim + 0xF24) <= 0.0f) return;
    using IsNpcFn = bool(__fastcall*)(BYTE* human, void* unused);
    if (!reinterpret_cast<IsNpcFn>(0x461130)(victim, nullptr)) return;
    ReportDamage(victim, attacker, done);
}

void GuerrillaCampaign_PropDamaged(void* prop, void* attacker, int hitPointsBefore) {
    // general_mover::apply_damage RM 0xA8AF5C-0xA8B032: activity_register_death(prop, attacker) when its hit points (its
    // level object's: Guerrilla's +0xB4 → +0x24, Armageddon's +0xB4 → +0x1C) go from above 0 to 0 or less. (Not its
    // fade flag, Armageddon's is_killed: a barrel at 0 hit points isn't faded.)
    if (!prop || hitPointsBefore <= 0 || !guerrilla_campaign::Running()) return;
    const BYTE* props = *reinterpret_cast<BYTE* const*>(static_cast<const BYTE*>(prop) + 0xB4);
    if (!props || *reinterpret_cast<const int*>(props + 0x1C) > 0) return;
    campaign::activities::ReportDeath(prop, attacker);
}
