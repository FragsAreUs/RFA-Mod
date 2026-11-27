// Enemies that fight in Ruin levels. A part of rfa_qol.dll; [Mods] ruin_ai=0 turns it off, and the
// setting itself is [Ruin] HostileAI= (0 the game's way, the default; 1 enemies fight), which Options >
// Experimentals changes while you play.
//
// The game's own Ruin levels have no enemies, and Ruin mode switches the AI's hostility off where it
// asks "is this Ruin?" (ruin_is_ruin_mode's own test, 0x50E1D0, a byte at 0x12F3A73):
//   0x734EF0  an AI's awareness of a target: always "not aware" in Ruin (0x734F42);
//   0x874E60  a character's reaction to being hit: skipped in Ruin (0x874E71);
//   0x85E0C0  the damage a hit does: none in Ruin for one kind of hit (0x85E166).
// A level made with the editor can have squads (squad spawn nodes), and they then walk about but never
// fight: you are never their enemy. With the setting on, those three calls ask our function instead,
// which answers "not Ruin" while Ruin's enemies are wanted and the game's own answer otherwise; every
// other Ruin rule (the score, the timer, the menus) still sees Ruin.
//
// Monoliths, always (whatever the setting): a monolith copied into a level (an object_anim_rig set: monolith_medium's
// rig, its claws' and root's collision movers, its rock rig, its gun and damage region, and its logic handler) emerges
// when its handler's condition ec_mp_infestation_monolith_should_activate (0x92B720) says so, which asks Infestation's
// wave records (0xA34470, cdecl: the mover; +0xC of the record naming it). Only Infestation has them, so anywhere else
// the monolith stayed buried. Its call (0x92B795) asks us instead: in a Ruin level (where the game's own levels have no
// monoliths) "yes", so a placed monolith emerges as the level starts; everywhere else the game's own answer.
#define RFA_MOD_NAME "ruin_ai"
#include "rfa_mod.h"
#include "rfa_qol.h"

namespace ruin_ai {
namespace {

constexpr DWORD kRuinMode = 0x50E1D0;
constexpr DWORD kCalls[] = {0x734F42, 0x874E71, 0x85E166};
// (0x8CBAC0's Ruin test, at 0x8CBAD5, was one of them at first: it isn't the AI's hostility after all but
// a check of something unlocked by levels of a value (0x8CB620's states 0-4), likely upgrades; left as it is.)

auto const RuinMode = reinterpret_cast<bool(__cdecl*)()>(kRuinMode);

bool g_hostile = false;

constexpr DWORD kMonolithActiveCall = 0x92B795, kMonolithActive = 0xA34470;
auto const MonolithActive = reinterpret_cast<bool(__cdecl*)(void* mover)>(kMonolithActive);
bool __cdecl MonolithShouldActivate(void* mover) { return RuinMode() || MonolithActive(mover); }

// An E8 call at `at` to `from` made to call `to` instead; false (and said) if the bytes aren't that call.
bool Redirect(DWORD at, DWORD from, const void* to) {
    BYTE* call = reinterpret_cast<BYTE*>(at);
    if (call[0] != 0xE8 || at + 5 + *reinterpret_cast<int32_t*>(call + 1) != from) {
        rfa::Log("0x%08X isn't the call to 0x%08X it should be; left as it is", static_cast<unsigned>(at), static_cast<unsigned>(from));
        return false;
    }
    BYTE bytes[5] = {0xE8};
    const int32_t relative = static_cast<int32_t>(reinterpret_cast<DWORD>(to) - (at + 5));
    memcpy(bytes + 1, &relative, 4);
    return rfa::PatchCode(call, bytes, sizeof(bytes));
}

void ReadSetting() { g_hostile = rfa::IniInt(L"Ruin", L"HostileAI", 0) != 0; }

// What the three calls ask now: Ruin, unless its enemies are to fight.
bool __cdecl RuinModeForAi() { return g_hostile ? false : RuinMode(); }

}  // namespace

void Init() {
    ReadSetting();
    int patched = 0;
    // Only the calls we expect (E8 to 0x50E1D0); anything else is left alone.
    for (DWORD at : kCalls)
        if (Redirect(at, kRuinMode, reinterpret_cast<const void*>(&RuinModeForAi))) ++patched;
    if (Redirect(kMonolithActiveCall, kMonolithActive, reinterpret_cast<const void*>(&MonolithShouldActivate)))
        rfa::Log("Monoliths placed in a Ruin level emerge as it starts (Infestation and the story keep the game's way)");
    rfa::Log("%d of %d of the AI's Ruin checks ask us; enemies %s in Ruin levels ([Ruin] HostileAI=%d)", patched,
             static_cast<int>(sizeof(kCalls) / sizeof(kCalls[0])), g_hostile ? "fight" : "don't fight (the game's way)", g_hostile ? 1 : 0);
}

void SettingChanged(const char* section, const char* key) {
    if (_stricmp(section, "Ruin") != 0 || _stricmp(key, "HostileAI") != 0) return;
    ReadSetting();
    rfa::Log("enemies %s in Ruin levels now", g_hostile ? "fight" : "don't fight");
}

}  // namespace ruin_ai
