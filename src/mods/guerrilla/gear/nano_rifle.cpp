#define RFA_MOD_NAME "guerrilla_gear"
// Guerrilla's nano rifle on vehicles: its seated riders' effects removed with the vehicle, and a watch on the freezes
// seen with it.
//
// Guerrilla's vehicle::start_nano_rifle_fade_out (RM 0xBB6190) lives on in Armageddon as its material effect fade,
// 0x604540 (thiscall vehicle: fade row, killed, attacker, position, lifetime), which our nano rifle reaches through
// its firing mode's Material_Effect_Fade rfg_Nano (the weapons build's matfx_fades.xtbl row). One difference is in
// that copy itself: Guerrilla asks for the material effect's parameters as material_effects_get_parameters(1, 2000)
// on every hit, a kill or not; Armageddon's passes the killed bool as the flags (0x6F7C90(out, killed, 2000)).
// Flags bit 0 is what puts the effect on the mesh: the material effects' update (0x6F5208; Guerrilla's
// material_effects_update_render_all, RM 0x6F1841) swaps the effect's material map onto the instance (its virtual
// +0x6C; Guerrilla's +0x4C) only while bit 0 is set, then clears it. So with 0 a hit never glows, as the user saw
// (2026-11-26: two hits, no glow, the kill's fade-out fine). NOT ported yet: with Guerrilla's 1 on a hit (a build of
// 2026-11-26 03:06) the game froze after four plain hits on an occupied EDF scout (03:51:49), drawing a mesh instance
// at the scout's place with an empty material map; whose instance, and why, isn't known, so hits keep Armageddon's 0
// (no glow) until it is.
//
// The watch (problems only): the game froze twice (2026-11-19 with Armageddon's own nano rifle, 2026-11-26 01:59
// with ours, both on an occupied Guerrilla vehicle) drawing a mesh instance whose material map (+0x128) had no
// array (+4): 0xD352E0 → 0xD37F90 → 0xD3AF40 reads it unchecked. Armageddon's fade sends a rider out only if he can
// quick-exit (0x65EBE0); otherwise he dissolves in his seat (the person fade, 0x87CE40) while the vehicle fades out
// and is removed (vehicle_fade_out, 0x6056D0, 2000 ms). So each nano kill logs its riders and what became of them,
// each person fade is logged, and an instance about to be drawn with an empty map is logged (where it is, and the
// nano fades just before it) before the game reads it.
//
// The cause, found with that watch (2026-11-26 03:02: a rider who couldn't get out dissolved in his seat, and the game
// froze 2 s later, as the vehicle was removed): Guerrilla's vehicle_post_sim_fade_despawn (RM 0xB79FB0; Armageddon's
// 0x60E530, the same) kills the riders still alive and destroys the vehicle, and its destroy_notify
// (vi_instance::passengers_destroy, RM 0xBB1BD0) destroys every NPC still seated, the dissolving one too. Guerrilla's
// material effects go with the mesh they are on (a material_effect's on_ri_dead_cb); the person fade's effects in
// Armageddon (0x87CE40: their handles at human +0x7E0, 8 of them, and +0x800) stay behind and are drawn on a mesh that
// is gone. So when the vehicle is about to be removed, its seated riders' material effects are removed first
// (material_effects_remove 0x6F5720, as Armageddon's own human code clears that array, 0x8BBF2B; then 0x890660 for the
// rest): what Guerrilla's dead-mesh callback does, a moment earlier.
//
// The next freeze (2026-11-26 12:21, an occupied convoy vehicle, 2 s after the kill, as it was removed): the instance
// drawn was a vehicle part's (an rl_deformable_mesh_instance some 25 m off) whose material map had been freed. A part
// that gets its own mesh while the dissolve's map is on the body copies that map (0x613B20); the effect frees it when
// it ends. PartInstance gives the part the vehicle's original map instead, as Guerrilla does.
#include "rfa_mod.h"
#include "guerrilla_gear.h"
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace {

// ---- The game's functions (Armageddon's publicbeta build)

constexpr DWORD kVehicleFade = 0x604540;   // the material effect fade on a vehicle (thiscall), ret 0x1C
constexpr size_t kVehicleFadePrologue = 9;  // push ebp / mov ebp,esp / sub esp,88h
constexpr DWORD kPersonFade = 0x87CE40;     // the material effect fade on a person (thiscall: fade, killed, info, 0), ret 0x10
constexpr size_t kPersonFadePrologue = 6;   // push ebp / mov ebp,esp / sub esp,70h
constexpr DWORD kFadeDespawn = 0x60E530;  // vehicle_post_sim_fade_despawn (cdecl: vehicle), true once it destroyed it
constexpr size_t kFadeDespawnPrologue = 6; // push ebp / mov ebp,esp / sub esp,40h
constexpr DWORD kDrawPrep = 0xD352E0;       // a mesh instance's draw preparation (thiscall: flags), ret 4
constexpr size_t kDrawPrepPrologue = 9;     // push ebp / mov ebp,esp / sub esp,414h
constexpr DWORD kPartInstance = 0x613B20;   // a vehicle component's own mesh instance, made when it needs one (thiscall component: vehicle), ret 4
constexpr size_t kPartInstancePrologue = 8; // push ebp / mov ebp,esp / mov eax,41B0h

auto const MatfxNamed = reinterpret_cast<int(__cdecl*)(const char* name)>(0x6F7C40);
auto const FadeDamagedEffect = reinterpret_cast<int(__cdecl*)(int fade)>(0x6F7E30);  // a fade row's Damaged_Material_Effect (+4)
auto const FadeDeathEffect = reinterpret_cast<int(__cdecl*)(int fade)>(0x6F7E70);    // its Death_Material_Effect (+8)
auto const CanQuickExit = reinterpret_cast<bool(__cdecl*)(BYTE* human)>(0x65EBE0);
auto const SeatSlot = reinterpret_cast<int*(__fastcall*)(BYTE* seats, void* unused, int seat)>(0x435E60);  // vehicle +0x41E4: 8 riders' handles
auto const HumanFromHandle = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int handle)>(0x436120);
auto const RemoveMaterialEffect = reinterpret_cast<void(__cdecl*)(int effect)>(0x6F5720);
auto const RemoveHumanMaterialEffects = reinterpret_cast<void(__cdecl*)(BYTE* human)>(0x890660);  // +0x12C0's, +0x800, +0x1300
auto const HandleOf = reinterpret_cast<int(__fastcall*)(BYTE* object, void* unused)>(0x424850);
auto const VehicleMesh = reinterpret_cast<BYTE*(__fastcall*)(BYTE* mesh, void* unused)>(0x4F3850);  // vehicle +0x1C4, null until loaded
auto const OriginalMaterialMap = reinterpret_cast<void*(__cdecl*)(BYTE* instance)>(0x6F59D0);  // material_effects_get_original_material_map
void* const kWorld = reinterpret_cast<void*>(0x1870630);
constexpr DWORD kVehicleSeats = 0x41E4;
constexpr DWORD kVehicleFlags = 0x4FB0;          // Guerrilla's +0x3F24: bit 11 fading out, bit 16 (with it, not yet)
constexpr DWORD kFadingOut = 1u << 11, kFadeHeld = 1u << 16;
constexpr DWORD kHumanFadeEffects = 0x7E0;       // the person fade's material effects, 8 handles (-1 none)
constexpr int kHumanFadeEffectCount = 8;
constexpr int kSeats = 8;
constexpr DWORD kInstanceMaterialMap = 0x128;  // a mesh instance's material map; its +4 the materials' array
constexpr DWORD kInstancePosition = 0x28;      // after its 3 × 3 orientation at +4
constexpr DWORD kVehicleMesh = 0x1C4;
constexpr DWORD kMeshBody = 0x3984;            // the vehicle body's mesh instance
constexpr DWORD kPartOwnInstance = 0x1C;       // a component's own mesh instance (null until it has one)
constexpr int kSetMaterialMap = 0x6C / 4, kGetMaterialMap = 0x70 / 4;  // a mesh instance's virtuals (they only set/read +0x128)

using VehicleFadeFn = void(__fastcall*)(BYTE* vehicle, void* unused, int fade, int killed, BYTE* attacker, float x, float y, float z, int lifetime);
using PersonFadeFn = void(__fastcall*)(BYTE* human, void* unused, int fade, int killed, void* info, int zero);
using FadeDespawnFn = bool(__cdecl*)(BYTE* vehicle);
using DrawPrepFn = void(__fastcall*)(BYTE* instance, void* unused, int flags);
using PartInstanceFn = bool(__fastcall*)(BYTE* component, void* unused, BYTE* vehicle);
using SetMapFn = void(__fastcall*)(BYTE* instance, void* unused, void* map);
using GetMapFn = void*(__fastcall*)(BYTE* instance, void* unused);
PartInstanceFn g_partInstance;
VehicleFadeFn g_vehicleFade;
PersonFadeFn g_personFade;
FadeDespawnFn g_fadeDespawn;
DrawPrepFn g_drawPrep;

// Whether a fade row is ours: Guerrilla's nano rifle's two material effects (nanorifle_damage while hit,
// reconstructor_shards once killed), which no row of Armageddon's pairs.
bool IsNanoRifleFade(int fade) {
    static int damaged = -2, death = -2;
    if (damaged == -2) {
        damaged = MatfxNamed("nanorifle_damage");
        death = MatfxNamed("reconstructor_shards");
    }
    return fade > 0 && damaged >= 0 && FadeDamagedEffect(fade) == damaged && FadeDeathEffect(fade) == death;
}

// The last nano fades, for the empty-map report: what, its handle, where, when.
struct RecentFade {
    const char* what;
    int handle;
    float position[3];
    DWORD time;
};
RecentFade g_recent[12];
int g_recentNext;
std::mutex g_recentLock;

void Remember(const char* what, BYTE* object) {
    if (!object) return;
    RecentFade fade{};
    fade.what = what;
    fade.handle = HandleOf(object, nullptr);
    memcpy(fade.position, object + 4, sizeof(fade.position));
    fade.time = GetTickCount();
    std::lock_guard<std::mutex> hold(g_recentLock);
    g_recent[g_recentNext] = fade;
    g_recentNext = (g_recentNext + 1) % (sizeof(g_recent) / sizeof(g_recent[0]));
}

void __fastcall VehicleFade(BYTE* vehicle, void* unused, int fade, int killed, BYTE* attacker, float x, float y, float z, int lifetime) {
    const bool ours = vehicle && IsNanoRifleFade(fade);
    if (ours) Remember(killed ? "vehicle (killed)" : "vehicle hit", vehicle);
    if (ours && killed) {
        // Its riders, before the game sends them out or dissolves them in their seats.
        for (int seat = 0; seat < kSeats; ++seat) {
            const int* slot = SeatSlot(vehicle + kVehicleSeats, nullptr, seat);
            if (!slot || *slot == -1) continue;
            BYTE* rider = HumanFromHandle(kWorld, nullptr, *slot);
            if (!rider) continue;
            rfa::Log("Nano rifle: vehicle %08X killed: seat %d, rider %08X %s", HandleOf(vehicle, nullptr), seat, *slot,
                     CanQuickExit(rider) ? "gets out" : "can't get out: dissolves in his seat, the vehicle removed in 2 s");
        }
    }
    g_vehicleFade(vehicle, unused, fade, killed, attacker, x, y, z, lifetime);
}

void __fastcall PersonFade(BYTE* human, void* unused, int fade, int killed, void* info, int zero) {
    if (human && IsNanoRifleFade(fade)) {
        Remember(killed ? "person (killed)" : "person hit", human);
        if (killed) rfa::Log("Nano rifle: person %08X dissolves", HandleOf(human, nullptr));
    }
    g_personFade(human, unused, fade, killed, info, zero);
}

// Before the game removes a vehicle that has faded out (and with it every rider still seated): its riders' material
// effects go first, so nothing is left drawing on their meshes.
bool __cdecl FadeDespawn(BYTE* vehicle) {
    if (vehicle) {
        const DWORD flags = *reinterpret_cast<DWORD*>(vehicle + kVehicleFlags);
        if ((flags & kFadingOut) && !(flags & kFadeHeld)) {
            for (int seat = 0; seat < kSeats; ++seat) {
                const int* slot = SeatSlot(vehicle + kVehicleSeats, nullptr, seat);
                if (!slot || *slot == -1) continue;
                BYTE* rider = HumanFromHandle(kWorld, nullptr, *slot);
                if (!rider) continue;
                int removed = 0;
                int* effects = reinterpret_cast<int*>(rider + kHumanFadeEffects);
                for (int i = 0; i < kHumanFadeEffectCount; ++i)
                    if (effects[i] != -1) {
                        RemoveMaterialEffect(effects[i]);
                        effects[i] = -1;
                        ++removed;
                    }
                RemoveHumanMaterialEffects(rider);
                if (removed) rfa::LogDetail("Nano rifle: vehicle %08X removed: rider %08X's %d dissolve effects removed first", HandleOf(vehicle, nullptr), *slot, removed);
            }
        }
    }
    return g_fadeDespawn(vehicle);
}

// A vehicle component getting its own mesh instance (a part breaking off, say) takes its material map from the body's
// instance as it is now (0x613B20 reads the body's map through its virtual +0x70). While a material effect has swapped
// its own map onto the body (the nano kill's dissolve), the new part gets the effect's map, which the effect frees when
// it ends; the part, which the effect doesn't know, is then drawn with a freed map: the freeze. Guerrilla's
// vehicle_component::create_renderable_instance (RM 0xB7AE80) gives a new part the vehicle's own stored map, never an
// effect's, and its destroyable pieces ask material_effects_get_original_material_map (Armageddon's 0x6F59D0, which its
// own destroyable pieces use too). So while the part is made, the body shows its original map, and gets the effect's
// back after: the part starts with the vehicle's own materials, as in Guerrilla. With no effect on the body the two
// maps are the same and nothing changes.
bool __fastcall PartInstance(BYTE* component, void* unused, BYTE* vehicle) {
    BYTE* body = nullptr;
    void* current = nullptr;
    void* original = nullptr;
    if (component && vehicle && !*reinterpret_cast<BYTE**>(component + kPartOwnInstance)) {
        if (BYTE* mesh = VehicleMesh(vehicle + kVehicleMesh, nullptr)) body = *reinterpret_cast<BYTE**>(mesh + kMeshBody);
        if (body) {
            void** methods = *reinterpret_cast<void***>(body);
            current = reinterpret_cast<GetMapFn>(methods[kGetMaterialMap])(body, nullptr);
            original = OriginalMaterialMap(body);
        }
    }
    const bool swapped = body && current && original && current != original;
    if (swapped) reinterpret_cast<SetMapFn>((*reinterpret_cast<void***>(body))[kSetMaterialMap])(body, nullptr, original);
    const bool made = g_partInstance(component, unused, vehicle);
    if (swapped) {
        reinterpret_cast<SetMapFn>((*reinterpret_cast<void***>(body))[kSetMaterialMap])(body, nullptr, current);
        rfa::LogDetail("Nano rifle: vehicle %08X: a part made its own mesh while an effect was on the body; it gets the vehicle's own materials",
                       HandleOf(vehicle, nullptr));
    }
    return made;
}

void ReportEmptyMap(BYTE* instance, BYTE* map) {
    float position[3];
    memcpy(position, instance + kInstancePosition, sizeof(position));
    std::string recent;
    {
        std::lock_guard<std::mutex> hold(g_recentLock);
        const DWORD now = GetTickCount();
        for (const RecentFade& fade : g_recent) {
            if (!fade.what) continue;
            char line[160];
            sprintf_s(line, "; %s %08X at %.1f %.1f %.1f, %lu ms ago", fade.what, fade.handle, fade.position[0], fade.position[1],
                      fade.position[2], now - fade.time);
            recent += line;
        }
    }
    RFA_LOG_FIRST(6, "Nano rifle watch: a mesh instance %p (first word %08X) at %.1f %.1f %.1f is drawn with an empty material map %p "
                     "(the game reads it unchecked: the freeze); nano fades before it%s",
                  instance, *reinterpret_cast<DWORD*>(instance), position[0], position[1], position[2], map,
                  recent.empty() ? ": none" : recent.c_str());
}

void __fastcall DrawPrep(BYTE* instance, void* unused, int flags) {
    if (instance) {
        BYTE* map = *reinterpret_cast<BYTE**>(instance + kInstanceMaterialMap);
        if (map && !*reinterpret_cast<void**>(map + 4)) ReportEmptyMap(instance, map);
    }
    g_drawPrep(instance, unused, flags);
}

bool Opens(DWORD address, const BYTE* opening, size_t size) { return memcmp(reinterpret_cast<const void*>(address), opening, size) == 0; }

}  // namespace

void nano_rifle::Init() {
    const BYTE vehicleFadeOpening[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00};
    const BYTE personFadeOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x70};
    const BYTE fadeDespawnOpening[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x40};
    const BYTE drawPrepOpening[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x14, 0x04, 0x00, 0x00};
    if (Opens(kVehicleFade, vehicleFadeOpening, sizeof(vehicleFadeOpening)))
        g_vehicleFade = reinterpret_cast<VehicleFadeFn>(rfa::HookFunction(kVehicleFade, kVehicleFadePrologue, reinterpret_cast<void*>(&VehicleFade)));
    if (Opens(kFadeDespawn, fadeDespawnOpening, sizeof(fadeDespawnOpening)))
        g_fadeDespawn = reinterpret_cast<FadeDespawnFn>(rfa::HookFunction(kFadeDespawn, kFadeDespawnPrologue, reinterpret_cast<void*>(&FadeDespawn)));
    if (!g_fadeDespawn) rfa::Log("Nano rifle: the game's fade-out removal of vehicles isn't as expected; a rider dissolving in his seat can freeze the game");
    const BYTE partInstanceOpening[] = {0x55, 0x8B, 0xEC, 0xB8, 0xB0, 0x41, 0x00, 0x00};
    if (Opens(kPartInstance, partInstanceOpening, sizeof(partInstanceOpening)))
        g_partInstance = reinterpret_cast<PartInstanceFn>(rfa::HookFunction(kPartInstance, kPartInstancePrologue, reinterpret_cast<void*>(&PartInstance)));
    if (!g_partInstance) rfa::Log("Nano rifle: the game's making of a vehicle part's mesh isn't as expected; a part breaking off a dissolving vehicle can freeze the game");
    if (Opens(kPersonFade, personFadeOpening, sizeof(personFadeOpening)))
        g_personFade = reinterpret_cast<PersonFadeFn>(rfa::HookFunction(kPersonFade, kPersonFadePrologue, reinterpret_cast<void*>(&PersonFade)));
    if (Opens(kDrawPrep, drawPrepOpening, sizeof(drawPrepOpening)))
        g_drawPrep = reinterpret_cast<DrawPrepFn>(rfa::HookFunction(kDrawPrep, kDrawPrepPrologue, reinterpret_cast<void*>(&DrawPrep)));
    if (!g_vehicleFade || !g_personFade || !g_drawPrep) rfa::Log("Nano rifle: the watch on dissolving riders isn't running (the game's code isn't as expected)");
}
