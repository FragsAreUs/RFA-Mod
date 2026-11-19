// guerrilla_kiosks: the pictures on the kiosks' screens, Guerrilla's kiosk_update_effects_and_action_nodes (RM 0xB2E8B0)
// and kiosk_start_effect_on_screen (RM 0xB2E550).
//
// Guerrilla, for each of a kiosk's four script points (its level object's Script_Point_Effects): a screen is a point
// flagged "Kiosk Small Screen" (0x40) or "Kiosk Large Screen" (0x20). While the kiosk is dead (is_killed) the effect
// on each live screen is stopped. Otherwise, on each screen still alive (its state SPES_NORMAL), the effect there is
// destroyed and the picture's effect for that size played in its place (looping, on the kiosk): while a broadcast
// plays, the broadcast's picture; else at alert yellow or above, Alert_Screen; else Idle_Screen (RF_Idle_Screen after
// the final mission, which our world doesn't have yet). The effect's expression variable PERCENT is set to the alert
// level / 3. Guerrilla does this as a kiosk streams in, when it is damaged, and for every kiosk when a broadcast
// starts, changes its picture or ends; its alert callback (public_address_alert_level_callback, RM 0xB3F200) sets
// the variable again on every screen as the alert level changes.
//
// Here the kiosks the world places (kiosks.txt) are looked at four times a second of play, and a screen whose picture
// isn't the one wanted (a kiosk loaded again, the alert or the broadcast changed) gets it. Damage is the game's own:
// Armageddon's script point code (its script_point_kill, 0x8D7C20) stops the effect a screen holds when it dies and
// plays the screen's death effect, as Guerrilla's does.
//
// Armageddon's pieces, found in its exe:
// - the mover's level object instance [mover+0xB4]: its base properties at +0 (their name at +0, and at +0x14 a
//   pointer to the script points' infos, 4 bytes each; an info's flags at +0x4C, read from the 9 names at 0x1101514:
//   0x20 Kiosk Large Screen, 0x40 Kiosk Small Screen); its script point count, a byte at +0x18; a script point's
//   record from 0x676F00 (thiscall: the instance, the index; null for none), Guerrilla's script_point_effect_inst:
//   +0 the script point's index, +8 the effect it holds (a handle, -1 none), +0x14 its state (0 normal, 1 stopped,
//   2 dying, 3 dead);
// - is_killed, virtual +0x10 (as Guerrilla's: rfg_mover reads +0x150 bit 4, general_mover +0x13C & 0x100);
// - an effect played on a script point of an object: 0x4EB9A0 (cdecl: the effect's number, the host's handle, the
//   script point's index, looping, -1, 0; returns the effect's handle), as Armageddon's script_point_kill plays one;
// - an effect object by handle 0x4D4360 (thiscall on the world), destroyed with the world's 0x6D5450 (Guerrilla's
//   world::destroy_object), stopped with 0x4F7AA0 (style 1, as Guerrilla's object_effect::stop);
// - an effect's own expression variable 0x4F8250 (thiscall: the effect, the number, the value; Guerrilla's
//   object_effect::set_expression_variable). Armageddon's numbers lost Guerrilla's 5 (jetpack fuel percent), so
//   Guerrilla's PERCENT (10) is Armageddon's effect_percent (9).
#define RFA_MOD_NAME "guerrilla_kiosks"
#include "rfa_mod.h"
#include "kiosks.h"

#include <cmath>
#include <unordered_map>

namespace kiosks::screens {
namespace {

constexpr size_t kMoverLevelObject = 0xB4, kMoverHandle = 0x68;
constexpr size_t kInstanceBase = 0x00, kInstancePointCount = 0x18;
constexpr size_t kBaseName = 0x00, kBasePointInfos = 0x14;
constexpr size_t kInfoFlags = 0x4C;
constexpr DWORD kLargeScreen = 0x20, kSmallScreen = 0x40;
constexpr size_t kPointIndex = 0x00, kPointEffect = 0x08, kPointState = 0x14;
constexpr int kPointNormal = 0;
constexpr int kMostPoints = 4;  // Guerrilla's level objects hold four
constexpr size_t kIsKilledSlot = 0x10 / 4;
constexpr int kEffectPercent = 9;  // Armageddon's effect_percent (Guerrilla's EFFECTS_VAR_PERCENT, 10)
constexpr float kLookEvery = 0.25f;  // seconds of play
constexpr DWORD kNone = 0xFFFFFFFF;

using PointRecordFn = BYTE*(__thiscall*)(BYTE* instance, int index);
auto const PointRecord = reinterpret_cast<PointRecordFn>(0x676F00);
using PlayOnPointFn = DWORD(__cdecl*)(int effect, DWORD host, int point, bool looping, int unknown, int unknown2);
auto const PlayOnPoint = reinterpret_cast<PlayOnPointFn>(0x4EB9A0);
using EffectObjectFn = BYTE*(__fastcall*)(void* world, void* unused, DWORD handle);
auto const EffectObject = reinterpret_cast<EffectObjectFn>(0x4D4360);
using DestroyObjectFn = bool(__fastcall*)(void* world, void* unused, BYTE* object, bool now);
auto const DestroyGameObject = reinterpret_cast<DestroyObjectFn>(0x6D5450);
using StopEffectFn = void(__fastcall*)(BYTE* effect, void* unused, int style);
auto const StopEffect = reinterpret_cast<StopEffectFn>(0x4F7AA0);
using SetVariableFn = void(__thiscall*)(BYTE* effect, int number, float value);
auto const SetEffectVariable = reinterpret_cast<SetVariableFn>(0x4F8250);
using IsKilledFn = bool(__thiscall*)(BYTE* object);

// What this mod put on each screen it holds: the effect's handle and the picture (by kiosk handle, a screen each).
struct Shown {
    DWORD effect[kMostPoints] = {kNone, kNone, kNone, kNone};
    int screen[kMostPoints] = {-1, -1, -1, -1};
    int alert = -1;      // the alert level the variable was last set for
    bool seen = false;   // logged once as found
    int ready = -1;      // its screens with a record last time (-1 before the first look)
};
std::unordered_map<DWORD, Shown> g_shown;
int g_broadcastScreen = -1;
float g_untilLook = 0.0f;
bool g_effectsFound = false;

void* World() { return reinterpret_cast<void*>(kWorld); }

// The picture Guerrilla's kiosk_start_effect_on_screen picks: the broadcast's while one plays, else the alert's at
// yellow and above, else the idle one.
int WantedScreen(int alert) {
    const tables::Defaults& d = tables::Settings();
    if (g_broadcastScreen >= 0) return g_broadcastScreen;
    if (alert >= 1 && d.alertScreen >= 0) return d.alertScreen;
    return d.idleScreen;
}

void SetPercent(DWORD effectHandle, int alert) {
    if (BYTE* effect = effectHandle == kNone ? nullptr : EffectObject(World(), nullptr, effectHandle))
        SetEffectVariable(effect, kEffectPercent, static_cast<float>(alert) / 3.0f);
}

const char* ScreenName(int screen) {
    const auto& all = tables::Screens();
    return screen >= 0 && screen < static_cast<int>(all.size()) ? all[screen].name.c_str() : "none";
}

// How far the player is from an object, in metres (-1 without a player).
float PlayerDistance(const BYTE* object) {
    const BYTE* player = Player();
    if (!player) return -1.0f;
    const float* p = reinterpret_cast<const float*>(player + 4);
    const float* o = reinterpret_cast<const float*>(object + 4);
    const float dx = p[0] - o[0], dy = p[1] - o[1], dz = p[2] - o[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// One kiosk loaded now: its screens as Guerrilla's code would have them.
//
// A screen's record (the script point's script_point_effect_inst) is there only while the game has the kiosk streamed
// in, near the player: Armageddon's rfg_mover::stream (0x6862D0) makes them as the mover streams in (0x8D8FA0, as
// Guerrilla's rfg_mover_setup_script_point_inst: for each script point its tag, its "<tag>_parent" piece in the
// destroyable shape, a record from the level objects' pool) and frees them as it streams out (Guerrilla's
// object_mover::stream → free_script_point_insts). So a kiosk farther off is in a loaded zone with its screen points
// (its level object's infos) but no records yet: its screens light up when it streams in, at the next look.
void UpdateKiosk(const tables::Kiosk& kiosk, BYTE* mover, int alert) {
    BYTE* instance = *reinterpret_cast<BYTE**>(mover + kMoverLevelObject);
    if (!instance) return;
    const BYTE* base = *reinterpret_cast<BYTE**>(instance + kInstanceBase);
    BYTE* const* infos = base ? *reinterpret_cast<BYTE* const* const*>(base + kBasePointInfos) : nullptr;
    const int points = *(instance + kInstancePointCount);
    Shown& shown = g_shown[kiosk.handle];
    const DWORD host = *reinterpret_cast<const DWORD*>(mover + kMoverHandle);
    const auto isKilled = reinterpret_cast<IsKilledFn>((*reinterpret_cast<void* const* const*>(mover))[kIsKilledSlot]);
    const bool killed = isKilled(mover);
    const int wanted = WantedScreen(alert);
    const auto& all = tables::Screens();
    int screenPoints = 0, ready = 0, changed = 0;  // its level object's screen points; those with a record now
    for (int i = 0; i < points && i < kMostPoints; ++i) {
        const BYTE* info = infos ? infos[i] : nullptr;
        if (!info) continue;
        const DWORD flags = *reinterpret_cast<const DWORD*>(info + kInfoFlags);
        const bool small = (flags & kSmallScreen) != 0, large = (flags & kLargeScreen) != 0;
        if (!small && !large) continue;  // not a screen
        ++screenPoints;
        BYTE* point = PointRecord(instance, i);
        if (!point) {  // not streamed in: no record yet
            shown.effect[i] = kNone;
            shown.screen[i] = -1;
            continue;
        }
        ++ready;
        DWORD& holds = *reinterpret_cast<DWORD*>(point + kPointEffect);
        const int state = *reinterpret_cast<const int*>(point + kPointState);
        if (killed) {
            // Guerrilla: a dead kiosk's live screens go dark.
            if (state == kPointNormal && holds != kNone) {
                if (BYTE* effect = EffectObject(World(), nullptr, holds)) StopEffect(effect, nullptr, 1);
                holds = kNone;
            }
            shown.effect[i] = kNone;
            shown.screen[i] = -1;
            continue;
        }
        if (state != kPointNormal) {  // dying or dead: the game's own code has it
            shown.effect[i] = kNone;
            shown.screen[i] = -1;
            continue;
        }
        // Already showing the picture wanted, the effect still there: nothing to do.
        if (holds != kNone && holds == shown.effect[i] && shown.screen[i] == wanted && EffectObject(World(), nullptr, holds)) continue;
        if (wanted < 0 || wanted >= static_cast<int>(all.size())) continue;
        const int effect = small ? all[wanted].small : all[wanted].large;
        if (effect < 0) continue;
        // Guerrilla's: the effect there destroyed, the picture's played in its place.
        if (holds != kNone)
            if (BYTE* old = EffectObject(World(), nullptr, holds)) DestroyGameObject(World(), nullptr, old, false);
        holds = PlayOnPoint(effect, host, *reinterpret_cast<const int*>(point + kPointIndex), true, -1, 0);
        shown.effect[i] = holds;
        shown.screen[i] = holds == kNone ? -1 : wanted;
        SetPercent(holds, alert);
        ++changed;
    }
    // Guerrilla's alert callback: every screen's variable again as the alert level changes.
    if (shown.alert != alert) {
        for (int i = 0; i < kMostPoints; ++i) SetPercent(shown.effect[i], alert);
        shown.alert = alert;
    }
    const float away = PlayerDistance(mover);
    if (!shown.seen) {
        shown.seen = true;
        if (screenPoints == 0)
            RFA_LOG_DETAIL_FIRST(60, "Screens: kiosk %08lX (%s, %s) loaded %.0f m away: it has no screens (a radio)", kiosk.handle, kiosk.gameplay.c_str(),
                          kiosk.props.c_str(), away);
        else if (ready == 0)
            RFA_LOG_DETAIL_FIRST(60, "Screens: kiosk %08lX (%s, %s) loaded %.0f m away: %d screens, not streamed in yet (they light up when the game streams "
                              "the kiosk in near you)%s",
                          kiosk.handle, kiosk.gameplay.c_str(), kiosk.props.c_str(), away, screenPoints, killed ? " (destroyed)" : "");
        else
            RFA_LOG_DETAIL_FIRST(60, "Screens: kiosk %08lX (%s, %s) loaded %.0f m away, streamed in: %d of its %d screens show %s", kiosk.handle,
                          kiosk.gameplay.c_str(), kiosk.props.c_str(), away, ready, screenPoints, killed ? "nothing (destroyed)" : ScreenName(wanted));
    } else if (ready > 0 && shown.ready == 0) {
        RFA_LOG_DETAIL_FIRST(120, "Screens: kiosk %08lX streamed in %.0f m away: %d of its %d screens show %s", kiosk.handle, away, ready, screenPoints,
                      killed ? "nothing (destroyed)" : ScreenName(wanted));
    } else if (ready == 0 && shown.ready > 0 && g_log) {
        rfa::LogDetail("Screens: kiosk %08lX streamed out %.0f m away: its screens' records are gone until it streams in again", kiosk.handle, away);
    } else if (changed > 0 && g_log) {
        rfa::LogDetail("Screens: kiosk %08lX: %d screens now show %s (alert %d)", kiosk.handle, changed, ScreenName(wanted), alert);
    }
    shown.ready = ready;
}

}  // namespace

void SetBroadcastScreen(int screen) { g_broadcastScreen = screen; }

void Update(float seconds) {
    g_untilLook -= seconds;
    if (g_untilLook > 0.0f) return;
    g_untilLook = kLookEvery;
    if (!g_effectsFound) {
        g_effectsFound = true;
        tables::FindEffects();
    }
    const int alert = AlertLevel();
    for (const tables::Kiosk& kiosk : tables::Kiosks()) {
        BYTE* mover = ObjectFromHandle(kiosk.handle);
        if (!mover) {
            g_shown.erase(kiosk.handle);  // not loaded: its screens go with it
            continue;
        }
        UpdateKiosk(kiosk, mover, alert);
    }
}

void Forget() {
    g_shown.clear();
    g_broadcastScreen = -1;
    g_effectsFound = false;
}

}  // namespace kiosks::screens
