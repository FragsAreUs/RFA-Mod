// Guerrilla's backpacks, worn by the player in Guerrilla's world: its jetpack (an upgrade in its campaign)
// and the nine Re-Mars-tered sells in its single player (Thrust, Fleetfoot, Stealth, Vision, Firepower,
// Concussion, Heal, Rhino, Tremor). Which one is worn is the upgrades' (upgrades.cpp: bought in the F7
// window, Wear); their numbers are Guerrilla's mp_backpacks.xtbl, written by the weapons build as
// mods\guerrilla_gear\backpacks.txt.
//
// Guerrilla's rules, kept:
//   - the jetpack (player_update_jetpack, RM 0xA9E430): jump held in the air, once he has left the
//     ground, while it has fuel: the climb gets Jetpack_acceleration (18 m/s²) × (1 falling, else
//     1 − min(climb, 5 m/s) / 5) each second; it burns its fuel_use_per_second (0.3) while it pushes, and
//     refills at Jetpack_charge_rate (0.1 a second, × 2 with the Jetpack Recharge upgrade,
//     upgrade_jetpack_charge_multiplier) while it doesn't (multi_object_backpack::process, RM 0x9F3E7C);
//   - the others are switched on with their own key: with fuel (uses_fuel) one switches on only with its
//     minimum_fuel_to_activate, takes its fuel_activation_cost, burns fuel_use_per_second while on and goes
//     off when it runs dry (or when the key is pressed again, if player_deactivatable), and refills at
//     fuel_regen_per_second while off.
//
// The key: Guerrilla's keys' Backpack (rfa_mods.ini [GuerrillaKeys] Backpack=, set in F7 > Guerrilla > Keys,
// guerrilla_hud; the older [GuerrillaGear] BackpackKey= where it isn't set; B unless either is): Armageddon has no
// backpack button, and its Nano Forge ability button (nano_use) stays the Nano Forge's.
//
// Switching one on is Guerrilla's: the key starts its ramp-up (start_ramp_up, 300 ms: control_gameplay_process's
// player_toggle_backpack_activation(player, false, 300)), and once that is over it activates (process_ramp_up ->
// activate, RM 0x9F2300) if it still has the fuel: the fuel_activation_cost taken, its kaboom and its ripple set off
// at once, and a pack whose activatable block has no deactivatable block (Concussion) goes off again straight away.
// What each pack does while on (each from its own active_abilities in Guerrilla's table):
//   Jetpack: climbs (above). Thrust: 180 ms after it goes on, straight up (climb max(climb, 2) + 10 m/s) or, with
//   jump held as it goes on, the slam down (climb - 10), on until he lands; no damage taken; bullets hit him as steel.
//   Fleetfoot: runs and sprints x 1.45. Firepower: bullets, melee and the hammer x 1.25, explosions x 1.25, and an
//   explosive impact where his bullets land. Concussion: Guerrilla's mp_shockwave_blast and its ripple (people
//   within 6 m thrown and hurt, vehicles and movers pushed) as it goes on. Tremor: mp_quake as it goes on and every
//   300 ms, shaking his camera. Heal: 400 hit points a second to everyone within 5.5 m. Rhino: its charge at
//   8.2 m/s, throwing people and vehicles it runs into and cutting through buildings; no damage taken; bullets hit
//   him as steel. Stealth: his fade, the EDF lose him. Vision: Guerrilla's x-ray.
// All ten are worn on Mason's back as Guerrilla's rfg_PC wears them (below, "The worn pack on his back"), with
// their effects, camera shakes and sounds (below, "The packs' effects"). Not in yet: Vision's and Heal's material
// effects (Armageddon's matfx.xtbl hasn't them), Rhino's limits on his other controls while charging.
// The speed and damage factors are the player's own upgrade parameters (below), so walking (as against
// running) isn't faster; damage taken goes through the "Last Gasp" ones, which the pack overrides while on.
#define RFA_MOD_NAME "guerrilla_gear"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "gear_api.h"
#include "hud/hud_api.h"
#include "world/world_api.h"
#include "rfa_menu.h"
#include "guerrilla_gear.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <intrin.h>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

auto const ControlHeld = reinterpret_cast<bool(__cdecl*)(int control, int controller, int unused)>(0x4E4800);
const int* const kLocalController = reinterpret_cast<const int*>(0x11C0580);
constexpr int kJump = 0x14;  // Armageddon's control "jump"
const float* const kFrameTime = reinterpret_cast<const float*>(0x11217C8);

// Guerrilla's jetpack, from its code (Re-Mars-tered: player_update_jetpack's constants).
constexpr float kJetpackAcceleration = 18.0f;  // Jetpack_acceleration (m/s²)
constexpr float kJetpackClimbCap = 5.0f;        // the climb (m/s) the push tapers to nothing at
constexpr float kJetpackUseRate = 0.35f;        // Jetpack_use_rate (its table's jetpack.fuel_use_per_second first)
constexpr float kJetpackChargeRate = 0.1f;      // Jetpack_charge_rate
constexpr float kJetpackRechargeUpgrade = 2.0f;  // Upgrade_jetpack_increased_charge (Jetpack Recharge)
constexpr float kLongestStep = 0.1f;            // seconds: a longer gap (the game paused) counts as this

struct Worn {
    std::string item;           // its items_3d name (Guerrilla's; ours may have "rfg_" before it)
    std::string characterProp;  // the wearer's point it hangs on ("" for a character model that follows his bones)
    std::string itemProp;       // the tag of its own it hangs by (Guerrilla's Item_Prop), or ""
};

// One of a pack's effects while it is on (Guerrilla's effects_alr::effects: a loop and an attack effect, at a point:
// on the pack, one of its own tags; on him, one of his bones; "" for its middle).
struct PackEffect {
    std::string loop, attack, attach;
};

struct Pack {
    int id = -1;
    std::string name;
    std::vector<Worn> worn;                     // its models, as Guerrilla's characters wear it
    std::vector<PackEffect> onPack, onHim;      // its activation's backpack_effects and player_effects
    std::vector<PackEffect> onPackAlternate;    // alternate_backpack_effects (Thrust's slam down)
    std::string sfxAttack, sfxLoop, sfxRelease; // its sfx_alr set's cues (backpacks.txt sfx_attack/loop/release)
    std::map<std::string, std::string> values;  // Guerrilla's fields by their path
    float Number(const char* path, float otherwise) const {
        auto found = values.find(path);
        return found == values.end() ? otherwise : static_cast<float>(std::atof(found->second.c_str()));
    }
    bool Flag(const char* path) const {
        auto found = values.find(path);
        return found != values.end() && _stricmp(found->second.c_str(), "True") == 0;
    }
    bool Has(const char* path) const { return values.count(path) != 0; }
};

std::map<int, Pack> g_packs;
UINT g_key = 0;

// The worn pack's state (the game's thread).
int g_wearing = -1;
float g_fuel = 1.0f;
bool g_on = false;           // an activated pack switched on, or the jetpack pushing
bool g_keyWasDown = false;
LARGE_INTEGER g_lastStep{};

void ReadPacks() {
    std::ifstream list(gear::Folder() + L"backpacks.txt");
    std::string line;
    Pack* pack = nullptr;
    while (std::getline(list, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const std::string key = line.substr(0, tab), value = line.substr(tab + 1);
        if (key == "pack") {
            const size_t second = value.find('\t');
            const int id = std::atoi(value.c_str());
            pack = &g_packs[id];
            pack->id = id;
            pack->name = second == std::string::npos ? value : value.substr(second + 1);
        } else if (key == "wear" && pack) {  // wear <item> <Character_Prop> <Item_Prop>, "-" for none
            std::istringstream fields(value);
            Worn worn;
            std::string characterProp, itemProp;
            std::getline(fields, worn.item, '\t');
            std::getline(fields, characterProp, '\t');
            std::getline(fields, itemProp, '\t');
            worn.characterProp = characterProp == "-" ? std::string() : characterProp;
            worn.itemProp = itemProp == "-" ? std::string() : itemProp;
            if (!worn.item.empty()) pack->worn.push_back(worn);
        } else if (pack && (key == "sfx_attack" || key == "sfx_loop" || key == "sfx_release")) {
            (key == "sfx_attack" ? pack->sfxAttack : key == "sfx_loop" ? pack->sfxLoop : pack->sfxRelease) = value;
        } else if (pack) {
            // A pack's effects: backpacks.txt gives each <effect>'s fields in turn, so a field met again starts the next.
            static const std::string kOnPack = "activatable.activation_effects.backpack_effects.effect.";
            static const std::string kOnHim = "activatable.activation_effects.player_effects.effect.";
            static const std::string kOnPackAlternate = "activatable.activation_effects.alternate_backpack_effects.effect.";
            std::vector<PackEffect>* list = key.rfind(kOnPack, 0) == 0 ? &pack->onPack : key.rfind(kOnHim, 0) == 0 ? &pack->onHim
                                          : key.rfind(kOnPackAlternate, 0) == 0 ? &pack->onPackAlternate : nullptr;
            if (list) {
                const std::string field = key.substr((list == &pack->onPack ? kOnPack : list == &pack->onHim ? kOnHim : kOnPackAlternate).size());
                std::string PackEffect::*member = field == "loop_effect" ? &PackEffect::loop : field == "attack_effect" ? &PackEffect::attack
                                                : field == "attach_point_name" ? &PackEffect::attach : nullptr;
                if (member && value != "False") {
                    if (list->empty() || !((list->back()).*member).empty()) list->emplace_back();
                    (list->back()).*member = value;
                }
            }
            pack->values[key] = value;
        }
    }
}

// A key by its setting's name: a letter, a digit, Tab or F1-F12 (guerrilla_hud's keys.h); 0 for none.
UINT KeyNamed(const std::wstring& name) {
    if (_wcsicmp(name.c_str(), L"Tab") == 0) return VK_TAB;
    if (name.size() >= 2 && (name[0] == L'F' || name[0] == L'f')) {
        const int n = _wtoi(name.c_str() + 1);
        if (n >= 1 && n <= 12) return VK_F1 + n - 1;
    }
    if (name.size() == 1 && iswalnum(name[0])) return towupper(name[0]);
    return 0;
}

std::string KeyName(UINT key) {
    if (!key) return "none";
    if (key == VK_TAB) return "Tab";
    if (key >= VK_F1 && key <= VK_F12) return "F" + std::to_string(key - VK_F1 + 1);
    return std::string(1, static_cast<char>(key));
}

// The key is Guerrilla's keys' (guerrilla_hud: rfa_mods.ini [GuerrillaKeys] Backpack=, changed in F7 > Guerrilla >
// Keys); while guerrilla_hud runs, the mod asks it for the key (GuerrillaHud_Key("Backpack"), a virtual-key code, 0 for
// none) whenever its GuerrillaHud_KeysVersion() changes, so a change in the window takes at once.
int g_hudVersionSeen = -1;

void FollowHudKey() {
    if (!guerrilla_hud::Running()) return;
    const int version = GuerrillaHud_KeysVersion();
    if (version == g_hudVersionSeen) return;
    const bool first = g_hudVersionSeen == -1;
    g_hudVersionSeen = version;
    const UINT key = static_cast<UINT>(GuerrillaHud_Key("Backpack"));
    if (key == g_key) return;
    rfa::LogDetail("Backpacks: the key is %s (%s)", KeyName(key).c_str(),
             first ? "Guerrilla's keys, as guerrilla_hud reads them" : "changed in F7 > Guerrilla > Keys");
    g_key = key;
}

// Each kind of key press the packs don't take, in the log (a diagnostic; at most kKeyLines a run).
constexpr int kKeyLines = 12;
int g_keyLines = 0;
bool g_rawWasDown = false;

void LogKey(const char* what) {
    if (g_keyLines >= kKeyLines) return;
    ++g_keyLines;
    rfa::LogDetail("Backpacks: the key went down; %s", what);
}

// Whether the backpack key went down this frame: the game's window in front, the menus not up.
bool KeyPressed() {
    FollowHudKey();
    bool down = false;
    if (g_key) {
        DWORD process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &process);
        const bool raw = (GetAsyncKeyState(static_cast<int>(g_key)) & 0x8000) != 0;
        const bool front = process == GetCurrentProcessId();
        const bool menus = rfa::menu::Want(RFA_MENUS_KEYBOARD) || rfa::menu::Want(RFA_MENUS_SHOWN);
        down = raw && front && !menus;
        if (raw && !g_rawWasDown && !down) LogKey(!front ? "the game's window isn't in front, so no pack takes it" : "the menus (F7 or the console) are up, so no pack takes it");
        g_rawWasDown = raw;
    }
    const bool pressed = down && !g_keyWasDown;
    g_keyWasDown = down;
    return pressed;
}

// This frame's time: the game's (0x11217C8, the one its movement step uses), else the clock's.
float StepSeconds() {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    float seconds = g_lastStep.QuadPart ? static_cast<float>(now.QuadPart - g_lastStep.QuadPart) / static_cast<float>(frequency.QuadPart) : 0.0f;
    g_lastStep = now;
    const float game = *kFrameTime;
    if (game > 0.0f && game <= kLongestStep) return game;
    return seconds < 0.0f ? 0.0f : seconds > kLongestStep ? kLongestStep : seconds;
}

void Refill(float rate, float seconds) { g_fuel = std::fmin(1.0f, g_fuel + rate * seconds); }

// ---- The jetpack

// The jetpack in the log (a diagnostic): each push's start and end with the fuel and his climb, the climb
// it asked for and what the climb was the frame after (whether the lift held through the game's own
// movement), and why jump held in the air didn't push. At most kJetLines a run.
constexpr int kJetLines = 24;
int g_jetLines = 0;
float g_climbAsked = 0.0f;   // the climb the last push left him with
bool g_checkClimb = false;   // look at the climb on the next frame
bool g_jumpWasHeld = false;

void LogJet(const char* format, ...) {
    if (g_jetLines >= kJetLines) return;
    ++g_jetLines;
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    rfa::LogDetail("Backpacks: %s", text);
}

void Jetpack(BYTE* player, const Pack& pack, float seconds) {
    const bool held = ControlHeld(kJump, *kLocalController, 0);
    const bool airborne = gear::Airborne(player);
    if (g_checkClimb) {
        g_checkClimb = false;
        LogJet("the jetpack's lift a frame on: climb %.2f m/s (it left him at %.2f)", gear::Climb(player), g_climbAsked);
    }
    const bool pushing = held && g_fuel > 0.0f && airborne;
    if (held && !g_jumpWasHeld && !pushing)
        LogJet("jump held, the jetpack doesn't push: %s", !airborne ? "on the ground (or not on foot)" : "no fuel");
    g_jumpWasHeld = held;
    if (pushing) {
        const float use = pack.Number("jetpack.fuel_use_per_second", kJetpackUseRate);
        g_fuel = std::fmax(0.0f, g_fuel - use * seconds);
        const float climb = gear::Climb(player);
        const float taper = climb < 0.0f ? 1.0f : 1.0f - std::fmin(climb, kJetpackClimbCap) / kJetpackClimbCap;
        gear::Lift(player, kJetpackAcceleration * taper * seconds);
        g_climbAsked = gear::Climb(player);
        if (!g_on) {
            LogJet("the jetpack pushes (fuel %.2f, climb %.2f -> %.2f m/s)", g_fuel, climb, g_climbAsked);
            g_checkClimb = true;
        }
    } else {
        const float charge = upgrades::BoughtLevel("jetpack") >= 2 ? kJetpackRechargeUpgrade : 1.0f;
        Refill(kJetpackChargeRate * charge, seconds);
    }
    if (pushing != g_on) {
        g_on = pushing;
        if (!pushing) {
            if (g_fuel <= 0.0f) rfa::LogDetail("Backpacks: the jetpack ran out of fuel");
            else LogJet("the jetpack stops (fuel %.2f, climb %.2f m/s)", g_fuel, gear::Climb(player));
        }
    }
}

// ---- What the packs' abilities use of the game's
//
// The player's upgrade parameters: 76 floats at player+0x2C60 (names at 0x1102F90; the game's getter
// 0x8CB580), which the game rebuilds from its defaults and the upgrades owned now and then (0x8CBDD0), not
// every frame. A pack scales or sets one while it is on, from the game's own value, and puts that back when
// it goes off; a value the game has rebuilt meanwhile is taken as its new own.
//   [27] Speed Mult Sprint, [28] Speed Mult Run (0x8BD870: run while human+0x724 is 2, sprint while 3);
//   [38] Bullet, [39] Explosion, [40] Melee, [44] Sledgehammer Damage Mult (applied when the player deals it);
//   [71] Last Gasp health %, [72] Last Gasp damage multiplier (the player's damage taken, 0x85E935: with [71]
//   at 1 or more, every hit's damage × [72]).
constexpr DWORD kUpgradeParameters = 0x2C60;
constexpr int kParameterCount = 76;
constexpr int kSpeedSprint = 27, kSpeedRun = 28, kBulletDamage = 38, kExplosionDamage = 39, kMeleeDamage = 40,
              kHammerDamage = 44, kLastGaspHealth = 71, kLastGaspDamage = 72;

// Health (human+0xF20 max hit points, an int; +0xF24 hit points, a float; +0x1180 what the game's
// regeneration (0x857E10) climbs back to, which must stay at least the hit points). The world's humans:
// 0x43A860 (thiscall: world) how many, 0x43A8B0 (thiscall: world, index) each; a human's position at +4.
constexpr DWORD kMaxHitPoints = 0xF20, kHitPoints = 0xF24, kRegenCeiling = 0x1180, kPosition = 0x4, kOrient = 0x10;
void* const kWorld = reinterpret_cast<void*>(0x1870630);
auto const HumanCount = reinterpret_cast<int(__fastcall*)(void* world, void* unused)>(0x43A860);
auto const HumanAt = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int index)>(0x43A8B0);

// Explosions: explosions.xtbl's entry by name (0x6EAE30, cdecl; the world build has Guerrilla's), and the
// game's explosion (0x6E6930, cdecl: the entry, its owner and source object, where, its orientation, a
// direction or null, the damage's source or null, a flag, and whether in multiplayer too), as the Nano Forge's
// Impact sets one off at the body's centre (0x84DBB0, thiscall: human, out) with the human's orientation.
struct Vec3 {
    float x, y, z;
};
auto const ExplosionNamed = reinterpret_cast<const void*(__cdecl*)(const char* name)>(0x6EAE30);
auto const MakeExplosion = reinterpret_cast<bool(__cdecl*)(const void* info, BYTE* owner, BYTE* source, const Vec3* where,
                                                          const float* orient, const Vec3* direction, const void* damageSource,
                                                          char flag, bool multiplayer)>(0x6E6930);
auto const BodyCentre = reinterpret_cast<void(__fastcall*)(BYTE* human, void* unused, Vec3* out)>(0x84DBB0);

// The parameters a pack holds now, each with the game's own value.
struct Held {
    bool held = false;
    float own = 0.0f;      // the game's value
    float written = 0.0f;  // ours, as written
};
Held g_held[kParameterCount];
BYTE* g_heldPlayer = nullptr;

float* Parameters(BYTE* player) { return reinterpret_cast<float*>(player + kUpgradeParameters); }

// This frame's wishes: index → (scale, or set) the game's own value.
struct Wish {
    int index;
    bool set;
    float value;
};

// Writes this frame's wishes and puts back the parameters no longer wished for.
void HoldParameters(BYTE* player, const Wish* wishes, int count) {
    if (player != g_heldPlayer) {  // another player (a level loaded): what was held went with the old one
        for (Held& h : g_held) h = Held{};
        g_heldPlayer = player;
    }
    float* p = Parameters(player);
    bool wished[kParameterCount] = {};
    for (int i = 0; i < count; ++i) {
        const Wish& w = wishes[i];
        Held& h = g_held[w.index];
        if (!h.held || p[w.index] != h.written) h.own = p[w.index];  // first time, or the game rebuilt it
        h.held = true;
        h.written = w.set ? w.value : h.own * w.value;
        p[w.index] = h.written;
        wished[w.index] = true;
    }
    for (int i = 0; i < kParameterCount; ++i)
        if (g_held[i].held && !wished[i]) {
            if (p[i] == g_held[i].written) p[i] = g_held[i].own;
            g_held[i] = Held{};
        }
}

// ---- The packs' abilities while on

ULONGLONG g_onSince = 0;     // when the worn pack was switched on (GetTickCount64)
bool g_boosted = false;      // Thrust: its boost given since switched on
ULONGLONG g_nextKaboom = 0;  // Tremor: when the next explosion goes off (0: none)

BYTE* PackModel();                                             // (the worn pack's first model, below)
const std::string* Field(const Pack& pack, const char* path);  // (a pack's field, below)
void ShakeNamedOnce(const std::string& name, float scale);     // (a camera shake, below)
const float kIdentity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

// Guerrilla's use_kaboom (RM 0x958350): its kaboom_explosion_name (the world build's explosions.xtbl) set off where
// the pack is (explosion_create at the pack's place, Guerrilla's identity turn, him its owner and source; kaboom_damages_user
// False for both packs: the game's explosion keeps him out as its owner's), then its kaboom_camera_shake shaken at its
// kaboom_camera_shake_user_intensity (he is the local player: Guerrilla's fall-off by distance is nothing).
void Kaboom(BYTE* player, const Pack& pack) {
    const std::string* name = Field(pack, "activatable.active_abilities.kaboom.kaboom_explosion_name");
    const void* info = name ? ExplosionNamed(name->c_str()) : nullptr;
    if (!info) {
        rfa::Log("Backpacks: no explosion \"%s\" in explosions.xtbl (build the world again)", name ? name->c_str() : "");
        return;
    }
    Vec3 at{};
    if (const BYTE* model = PackModel()) at = *reinterpret_cast<const Vec3*>(model + kPosition);
    else BodyCentre(player, nullptr, &at);
    MakeExplosion(info, player, player, &at, kIdentity, nullptr, nullptr, 0, false);
    if (const std::string* shake = Field(pack, "activatable.active_abilities.kaboom.kaboom_camera_shake.kaboom_camera_shake"))
        ShakeNamedOnce(*shake, std::clamp(pack.Number("activatable.active_abilities.kaboom.kaboom_camera_shake.kaboom_camera_shake_user_intensity", 1.0f), 0.0f, 1.0f));
}

// Heal (Unique_ID 7): health_regen_per_second (400) to everyone within health_regen_radius (5.5 m), enemies
// too while on (health_regen_heal_enemies), the player included; as the game's own regeneration, the hit
// points and what regeneration climbs back to.
void Heal(BYTE* player, const Pack& pack, float seconds) {
    const float rate = pack.Number("activatable.active_abilities.health_regen.health_regen_per_second", 400.0f);
    const float radius = pack.Number("activatable.active_abilities.health_regen.health_regen_radius", 5.5f);
    const Vec3 at = *reinterpret_cast<const Vec3*>(player + kPosition);
    const int count = HumanCount(kWorld, nullptr);
    for (int i = 0; i < count; ++i) {
        BYTE* human = HumanAt(kWorld, nullptr, i);
        if (!human) continue;
        const Vec3 pos = *reinterpret_cast<const Vec3*>(human + kPosition);
        const float dx = pos.x - at.x, dy = pos.y - at.y, dz = pos.z - at.z;
        if (dx * dx + dy * dy + dz * dz > radius * radius) continue;
        float& hp = *reinterpret_cast<float*>(human + kHitPoints);
        const float max = static_cast<float>(*reinterpret_cast<const int*>(human + kMaxHitPoints));
        if (hp <= 0.0f || hp >= max) continue;  // the dead stay dead
        hp = std::fmin(max, hp + rate * seconds);
        float& ceiling = *reinterpret_cast<float*>(human + kRegenCeiling);
        if (ceiling < hp) ceiling = hp;
    }
}

// Thrust (Unique_ID 1): Guerrilla's jump boost (multi_object_backpack::use_jump_boost, apply_jump_boost RM
// 0x957190): jump_boost_delay_ms (180) after it goes on, his sideways speed x jump_boost_horizontal (0: straight
// up or down) and his climb max(climb, 2) + jump_boost_vertical (10) up, or climb - 10 down: down when jump is held
// as it goes on (prepare_type_specific_info_for_toggle_packet: CBA_OFC_JUMP), the slam, with its alternate effects
// (PackOn). It stays on until he is back on the ground after the boost (multi_object_backpack::process, 0x9F3BDD:
// deactivate), and a slam's landing plays jump_boost_downthrust_ground_hit_effect (mp_thrust_dust) where he lands.
void PackAttack(BYTE* player, const Pack& pack);  // (its attack effects, below)
int PlayEffectNamed(BYTE* player, const Pack& pack, const std::string& name, bool looping, bool onHim, const std::string& attach);
bool g_thrustUp = true;       // this switch-on's direction
bool g_thrustLeftGround = false;

// false once he has landed after the boost (it goes off).
bool Thrust(BYTE* player, const Pack& pack, ULONGLONG now) {
    if (g_boosted) {
        const bool airborne = gear::Airborne(player);
        if (airborne) g_thrustLeftGround = true;
        if (airborne || !g_thrustLeftGround) return true;
        if (!g_thrustUp) {
            auto found = pack.values.find("activatable.active_abilities.jump_boost.jump_boost_downthrust_ground_hit_effect");
            if (found != pack.values.end()) PlayEffectNamed(player, pack, found->second, false, true, std::string());
        }
        rfa::LogDetail("Backpacks: Thrust landed%s; off", g_thrustUp ? "" : " from its slam");
        return false;
    }
    const float delay = pack.Number("activatable.active_abilities.jump_boost.jump_boost_delay_ms", 180.0f);
    if (now - g_onSince < static_cast<ULONGLONG>(delay)) return true;
    g_boosted = true;
    g_thrustLeftGround = gear::Airborne(player);
    const float horizontal = pack.Number("activatable.active_abilities.jump_boost.jump_boost_horizontal", 0.0f);
    const float vertical = pack.Number("activatable.active_abilities.jump_boost.jump_boost_vertical", 10.0f);
    const float before = gear::JumpBoost(player, horizontal, vertical, g_thrustUp);
    rfa::LogDetail("Backpacks: Thrust %s, climb %.1f -> %.1f m/s (%s; sideways x %.1f)", g_thrustUp ? "boost" : "slam down", before, gear::Climb(player),
             g_thrustLeftGround ? "in the air" : "from the ground", horizontal);
    PackAttack(player, pack);  // Guerrilla's use_jump_boost -> effects_alr_attack
    return true;
}

// A pack with a kaboom block in its active abilities (Concussion, 6, Guerrilla's Shockwave; Tremor, 9): its kaboom as it
// activates (Activate, below), then, while it stays on, again each kaboom_delay_ms (Guerrilla's process: the kaboom's
// kaboom_explosion_delay_timer, which use_kaboom sets). Concussion never stays on (it has no deactivatable block), so
// only Tremor's repeats: mp_quake every 300 ms.
bool HasKaboom(const Pack& pack) { return Field(pack, "activatable.active_abilities.kaboom.kaboom_explosion_name") != nullptr; }

bool Kabooms(BYTE* player, const Pack& pack, ULONGLONG now) {
    if (!g_nextKaboom || now < g_nextKaboom) return true;
    Kaboom(player, pack);
    g_nextKaboom = now + static_cast<ULONGLONG>(pack.Number("activatable.active_abilities.kaboom.kaboom_delay_ms", 300.0f));
    return true;
}

void Stealth(BYTE* player, const Pack& pack, ULONGLONG now);  // (Stealth, below)
void StealthOff(BYTE* player);
void Rhino(BYTE* player, const Pack& pack, ULONGLONG now);    // (Rhino's charge, below)

// A pack's turn while on; false when it is spent.
bool WhileOn(BYTE* player, const Pack& pack, float seconds) {
    const ULONGLONG now = GetTickCount64();
    switch (pack.id) {
        case 1: return Thrust(player, pack, now);
        case 3: Stealth(player, pack, now); return true;
        case 7: Heal(player, pack, seconds); return true;
        case 8: Rhino(player, pack, now); return true;
        default: return HasKaboom(pack) ? Kabooms(player, pack, now) : true;  // Tremor's quakes; Vision's x-ray: Activate
    }
}

// The upgrade parameters a pack scales while on, from its active_abilities: movement_speed_factor (run and
// sprint), direct_damage_dealt_factor (bullets, melee, the hammer), area_damage_dealt_factor (explosions),
// damage_taken_factor (Last Gasp: every hit × it).
int Wishes(const Pack& pack, Wish* out) {
    int n = 0;
    auto factor = [&](const char* field) { return pack.Number((std::string("activatable.active_abilities.") + field).c_str(), 1.0f); };
    if (const float speed = factor("movement_speed_factor"); speed != 1.0f) {
        out[n++] = {kSpeedRun, false, speed};
        out[n++] = {kSpeedSprint, false, speed};
    }
    if (const float direct = factor("direct_damage_dealt_factor"); direct != 1.0f) {
        out[n++] = {kBulletDamage, false, direct};
        out[n++] = {kMeleeDamage, false, direct};
        out[n++] = {kHammerDamage, false, direct};
    }
    if (const float area = factor("area_damage_dealt_factor"); area != 1.0f) out[n++] = {kExplosionDamage, false, area};
    if (const float taken = factor("damage_taken_factor"); taken < 1.0f) {
        out[n++] = {kLastGaspHealth, true, 1.0f};
        out[n++] = {kLastGaspDamage, true, taken};
    }
    return n;
}

// ---- The packs' effects, camera shakes and sounds
//
// Guerrilla's multi_object_backpack plays a pack's effects on the pack itself (effects_loop, effects_alr_attack:
// effects_play with the pack's handle and a tag of its own, -1 for its middle), from its mp_backpacks entry
// (backpacks.txt): put on, pickup_effects' attack_effect (Thrust's mp_thrust_equip_sound); switched on
// (start_ramp_up), the ramp-up's backpack_effects (mp_thrust_activate) and camera_shake (bp_ramp_thrust), then
// the activation_camera_shake (bp_active_thrust) and the loop_effect (mp_thrust_loop) until it goes off; as
// its ability acts (Thrust's boost), the attack_effect (mp_thrust_attack); a switch-on without the fuel, the
// failed_activation_foley (MP_BACKPACK_THRUST_NO_FUEL); refuelled to what it needs to switch on, the
// fuel_refill_foley (MP_BACKPACK_THRUST_REFUELED). The effects are the world's ("rfg_" + Guerrilla's name,
// with their own sounds), the shakes and foley the weapons build's (GuerrillaWeapons.BackpackEffects,
// BackpackMedia: all ten's; one that isn't built plays nothing, and says so once).
//   - an effect: its number by name (0x4EF370; -1 none), played with 0x4EB460 (cdecl: the number, where, the
//     turn, looping, host_handle, host_tag, source_handle, attached_to_camera, 0, 0: the effect's properties,
//     0x4EB7F0), so it rides on the pack's model (on the player while the pack has none on him); stopped as
//     guerrilla_world stops its dust (0x4D4360 the object, 0x4F7AA0(1));
//   - a camera shake: camera_shake.xtbl's by name (0x4CB8C0; null none), shaken with 0x4CB5B0 (cdecl: the
//     shake, 1.0, 1.0), as a gun's Fire_Camera_Shake (0x73DDE9);
//   - a sound: the event RFG_<cue> (Wwise's id, FNV-1 of the lower-case name) posted (0xB846E0) on the
//     player's own sound object (human+0x1158, made with him, 0x862DD4).
using EffectNumberFn = int(__cdecl*)(const char* name);
auto const EffectNumber = reinterpret_cast<EffectNumberFn>(0x4EF370);
using EffectsPlayFn = int(__cdecl*)(int effect, const float* where, const float* turn, bool looping, DWORD host, int hostTag, DWORD source,
                                    bool onCamera, DWORD unused1, DWORD unused2);
auto const EffectsPlay = reinterpret_cast<EffectsPlayFn>(0x4EB460);
auto const EffectObject = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int handle)>(0x4D4360);
auto const StopEffect = reinterpret_cast<void(__fastcall*)(BYTE* effect, void* unused, int style)>(0x4F7AA0);
auto const ShakeNamed = reinterpret_cast<void*(__cdecl*)(const char* name)>(0x4CB8C0);
auto const CameraShake = reinterpret_cast<bool(__cdecl*)(void* shake, float scale, float other)>(0x4CB5B0);
auto const PlaySoundEvent = reinterpret_cast<int(__cdecl*)(DWORD soundObject, DWORD eventId, DWORD* handle, int unused, int delayMs)>(0xB846E0);
constexpr DWORD kHumanSoundObject = 0x1158;
const char* const kEquipEffect = "pickup_effects.player_effects.effect.attack_effect";
const char* const kRampEffect = "activatable.activation_effects.ramp_up_effects.backpack_effects";
const char* const kRampShake = "activatable.activation_effects.ramp_up_effects.camera_shake";
const char* const kActiveShake = "activatable.activation_effects.activation_camera_shake";
const char* const kNoFuelFoley = "activatable.activation_effects.failed_activation_foley";
const char* const kRefuelFoley = "activatable.uses_fuel.fuel_refill_foley";

auto const HandleOfObject = reinterpret_cast<DWORD(__thiscall*)(const BYTE* object)>(0x424850);  // [object+0x68]

std::vector<int> g_loopEffects;     // the worn pack's loop effects while it is on
std::string g_sfxLoop, g_sfxRelease;  // its sound set's loop playing, and the release that ends it
int g_effectLogs = 0;
bool g_wearingSeen = false;         // the pack worn as the game starts has been seen
std::vector<std::string> g_missing;  // what isn't built, said once each
BYTE* PackModel();                   // (the worn pack's first model, below)
void VisionOn(bool on);              // (Vision's x-ray, below)
int PointOn(BYTE* player, const std::string& name, std::string& how);  // (a point of his: below)

void Missing(const std::string& what) {
    if (std::find(g_missing.begin(), g_missing.end(), what) != g_missing.end() || g_missing.size() >= 48) return;
    g_missing.push_back(what);
    rfa::Log("Backpacks: %s isn't built (build Guerrilla's world and weapons again); not played", what.c_str());
}

const std::string* Field(const Pack& pack, const char* path) {
    auto found = pack.values.find(path);
    return found == pack.values.end() || found->second.empty() || _stricmp(found->second.c_str(), "False") == 0 ? nullptr : &found->second;
}

// A point to play an effect at (Guerrilla's effects_alr_store_handle_info: the attach_point_name's tag): on the
// pack, a tag of its model's own (its function +0x2C, by the lower-case name); on him, his skeleton's tag or bone of
// that name (as a prop's point); -1, the middle, for none or one not found.
int EffectPoint(BYTE* player, BYTE* host, const std::string& attach) {
    if (attach.empty()) return -1;
    if (host == player) {
        std::string how;
        return PointOn(player, attach, how);
    }
    std::string lower = attach;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    using TagFn = int(__thiscall*)(BYTE* object, const char* name);
    const TagFn tagNamed = *reinterpret_cast<const TagFn*>(*reinterpret_cast<BYTE* const*>(host) + 0x2C);
    return tagNamed(host, lower.c_str());
}

// Plays one of the pack's effects (by Guerrilla's name: ours is "rfg_" + it) on its model, or on him; its handle, or -1.
int PlayEffectNamed(BYTE* player, const Pack& pack, const std::string& name, bool looping, bool onHim, const std::string& attach) {
    if (name.empty()) return -1;
    int effect = EffectNumber(("rfg_" + name).c_str());
    if (effect < 0) effect = EffectNumber(name.c_str());
    if (effect < 0) {
        Missing("the effect " + name);
        return -1;
    }
    BYTE* host = onHim ? nullptr : PackModel();
    if (!host) host = player;
    const int point = EffectPoint(player, host, attach);
    const int handle = EffectsPlay(effect, reinterpret_cast<const float*>(host + kPosition), reinterpret_cast<const float*>(host + kOrient), looping,
                                   HandleOfObject(host), point, HandleOfObject(player), false, 0, 0);
    if (g_effectLogs < 24) {
        ++g_effectLogs;
        rfa::LogDetail("Backpacks: %s plays %s%s on %s%s%s (point %d, handle %d)", pack.name.c_str(), name.c_str(), looping ? " (looping)" : "",
                 host == player ? "him" : "its model", attach.empty() ? "" : " at ", attach.c_str(), point, handle);
    }
    return handle;
}

// The pack's effect named by that field (one effect's name, at the attach point that field names, if given).
int PlayPackEffect(BYTE* player, const Pack& pack, const char* path, bool looping, bool onHim = false, const char* attachPath = nullptr) {
    const std::string* name = Field(pack, path);
    const std::string* attach = attachPath ? Field(pack, attachPath) : nullptr;
    return name ? PlayEffectNamed(player, pack, *name, looping, onHim, attach ? *attach : std::string()) : -1;
}

void StopPackLoop() {
    for (int handle : g_loopEffects)
        if (BYTE* effect = EffectObject(kWorld, nullptr, handle)) StopEffect(effect, nullptr, 1);
    g_loopEffects.clear();
}

void ShakeNamedOnce(const std::string& name, float scale) {
    void* shake = ShakeNamed(("rfg_" + name).c_str());
    if (!shake) shake = ShakeNamed(name.c_str());
    if (!shake) {
        Missing("the camera shake " + name);
        return;
    }
    CameraShake(shake, scale, 1.0f);
}

void ShakePack(const Pack& pack, const char* path, float scale = 1.0f) {
    if (const std::string* name = Field(pack, path)) ShakeNamedOnce(*name, scale);
}

DWORD WwiseNameId(const std::string& name) {
    DWORD h = 2166136261u;
    for (char c : name) h = (h * 16777619u) ^ static_cast<BYTE>(std::tolower(static_cast<unsigned char>(c)));
    return h;
}

// An event (the weapons build's RFG_<cue>, or a loop's RFG_<cue>_STOP) posted on his own sound object.
void PostEvent(BYTE* player, const std::string& event) {
    const DWORD soundObject = player ? *reinterpret_cast<const DWORD*>(player + kHumanSoundObject) : 0;
    if (event.empty() || !soundObject) return;
    DWORD handle = 0;
    PlaySoundEvent(soundObject, WwiseNameId(event), &handle, 0, 0);
    const bool stop = event.size() > 5 && event.compare(event.size() - 5, 5, "_STOP") == 0;
    if (!handle && !stop) Missing("the sound " + event);
}

void PackSound(BYTE* player, const Pack& pack, const char* path) {
    if (const std::string* cue = Field(pack, path)) PostEvent(player, "RFG_" + *cue);
}

// Its attack effects (Guerrilla's effects_alr_attack): as Thrust boosts, as the others go on.
void PackAttack(BYTE* player, const Pack& pack) {
    for (const PackEffect& e : pack.onPack) PlayEffectNamed(player, pack, e.attack, false, false, e.attach);
    for (const PackEffect& e : pack.onHim) PlayEffectNamed(player, pack, e.attack, false, true, e.attach);
}

// The key pressed: Guerrilla's start_ramp_up (RM 0x97D070): its ramp-up's effect on the pack (at the ramp-up's attach
// point) and on him, and its ramp-up camera shake (Thrust's slam down: the alternate ramp-up).
void RampUp(BYTE* player, const Pack& pack) {
    if (pack.id == 1 && !g_thrustUp) {
        PlayPackEffect(player, pack, "activatable.activation_effects.alternate_ramp_up_effects.backpack_effects", false, false,
                       "activatable.activation_effects.alternate_ramp_up_effects.attach_point_name");
        ShakePack(pack, "activatable.activation_effects.alternate_ramp_up_effects.camera_shake");
    } else {
        PlayPackEffect(player, pack, kRampEffect, false, false, "activatable.activation_effects.ramp_up_effects.attach_point_name");
        PlayPackEffect(player, pack, "activatable.activation_effects.ramp_up_effects.player_effects", false, true);
        ShakePack(pack, kRampShake);
    }
}

// The ramp-up over: Guerrilla's activate (RM 0x9F2300). Its activation camera shake, its sound set's attack and loop,
// and each of its effects' loops on the pack and on him; their attack effects too, but Thrust's, which go with its
// boost.
void PackOn(BYTE* player, const Pack& pack) {
    const bool alternate = pack.id == 1 && !g_thrustUp;  // Thrust's slam down: its alternate effects
    ShakePack(pack, kActiveShake);
    if (!pack.sfxAttack.empty()) PostEvent(player, "RFG_" + pack.sfxAttack);
    if (!pack.sfxLoop.empty()) PostEvent(player, "RFG_" + pack.sfxLoop);
    g_sfxLoop = pack.sfxLoop;
    g_sfxRelease = pack.sfxRelease;
    StopPackLoop();
    for (const PackEffect& e : alternate ? pack.onPackAlternate : pack.onPack)
        if (int handle = PlayEffectNamed(player, pack, e.loop, true, false, e.attach); handle != -1) g_loopEffects.push_back(handle);
    for (const PackEffect& e : pack.onHim)
        if (int handle = PlayEffectNamed(player, pack, e.loop, true, true, e.attach); handle != -1) g_loopEffects.push_back(handle);
    if (pack.id != 1) PackAttack(player, pack);
    if (pack.id == 4) VisionOn(true);
}

// Vision (4): Guerrilla's activate_dual_abilities starts its x-ray for the local player, xray_effect_start
// (XET_VISION_BACKPACK, 0; people seen through walls), and deactivate_dual_abilities stops it; Armageddon kept the
// same x-ray (0x6FA500 start, 0x6FA630 stop, cdecl: the type; its console's xray_humans uses 1).
auto const XrayStart = reinterpret_cast<void(__cdecl*)(int type)>(0x6FA500);
auto const XrayStop = reinterpret_cast<void(__cdecl*)(int type)>(0x6FA630);
constexpr int kVisionXray = 0;
bool g_xrayOn = false;

void VisionOn(bool on) {
    if (on == g_xrayOn) return;
    g_xrayOn = on;
    if (on) XrayStart(kVisionXray);
    else XrayStop(kVisionXray);
    rfa::LogDetail("Backpacks: Vision's x-ray %s", on ? "on" : "off");
}

// Off (by its key, spent, run dry, taken off): its loops end, its sound set's loop stops and its release plays.
void PackOff(BYTE* player) {
    VisionOn(false);
    StealthOff(player);
    StopPackLoop();
    if (!g_sfxLoop.empty()) PostEvent(player, "RFG_" + g_sfxLoop + "_STOP");
    if (!g_sfxRelease.empty()) PostEvent(player, "RFG_" + g_sfxRelease);
    g_sfxLoop.clear();
    g_sfxRelease.clear();
}

// ---- Stealth (3)
//
// Guerrilla's multi_object_backpack: switched on, stealth_player fades him (stealth_time_ms, 1000 ms, its
// stealth_timestamp); he is wholly stealthed (is_fully_stealthed, RM 0x958B50) once the fade is done and no
// re-stealth is waiting (time_until_restealth: restealth_time_ms, 1000 ms, after he attacks), and the EDF don't
// take a target in stealth (human_combat_get_target, human_is_player_in_stealth). Its look is the pack's material
// effect (items_3d MaterialEffect stealth_pack) on him, the pack and his gun (activatable.material_effects
// on_human, on_backpack, on_weapon: start_material_effects, RM 0x999BF0, material_effects_apply). Armageddon kept
// matfx.xtbl's stealth_pack (its second layer without Guerrilla's Stencil_test and Stencil_clear) and applies a
// material effect to a human as its Ambushers' cloak does (0x865030): its number by name (0x6F7C40), its parameters
// (0x6F7C90: out, 0, -1), on the human (0x890430: the human, the number, &parameters; 0x890660 takes it off) and on
// his gun (0x84E930 thiscall(human, 0) the gun; 0x7460A0 off; 0x745BA0: the gun, the human, the number, the
// parameters by value).
auto const MatfxNamed = reinterpret_cast<int(__cdecl*)(const char* name)>(0x6F7C40);
auto const MatfxParameters = reinterpret_cast<DWORD*(__cdecl*)(DWORD* out, int first, int second)>(0x6F7C90);
auto const MatfxOnHuman = reinterpret_cast<void(__cdecl*)(BYTE* human, int effect, const DWORD* parameters)>(0x890430);
auto const MatfxOffHuman = reinterpret_cast<void(__cdecl*)(BYTE* human)>(0x890660);
auto const EquippedGun = reinterpret_cast<BYTE*(__thiscall*)(BYTE* human, int which)>(0x84E930);
auto const MatfxOffGun = reinterpret_cast<void(__cdecl*)(BYTE* gun)>(0x7460A0);
auto const MatfxOnGun = reinterpret_cast<void(__cdecl*)(BYTE* gun, BYTE* human, int effect, DWORD p0, DWORD p1, DWORD p2, DWORD p3, DWORD p4)>(0x745BA0);
constexpr int kPrimaryAttack = 0x11, kAltAttack = 0x12, kMeleeAttack = 0x13;  // the controls (0xF7F0B8)

bool g_stealthed = false;       // the material effect on him
ULONGLONG g_stealthFadeFrom = 0;  // when the fade began (0: not fading or faded)
ULONGLONG g_restealthAt = 0;      // a re-stealth waiting after an attack (0: none)
BYTE* g_stealthGun = nullptr;
bool g_fullyStealthed = false;  // for GuerrillaGear_PlayerStealthed

void StealthLook(BYTE* player, const Pack& pack, bool on) {
    if (on == g_stealthed) return;
    g_stealthed = on;
    if (!on) {
        MatfxOffHuman(player);
        if (g_stealthGun) MatfxOffGun(g_stealthGun);
        g_stealthGun = nullptr;
        return;
    }
    const int effect = MatfxNamed("stealth_pack");
    if (effect < 0) {
        Missing("the material effect stealth_pack");
        return;
    }
    DWORD parameters[5] = {};
    MatfxParameters(parameters, 0, -1);
    if (pack.Flag("activatable.material_effects.on_human")) MatfxOnHuman(player, effect, parameters);
    if (pack.Flag("activatable.material_effects.on_weapon") && (g_stealthGun = EquippedGun(player, 0)) != nullptr)
        MatfxOnGun(g_stealthGun, player, effect, parameters[0], parameters[1], parameters[2], parameters[3], parameters[4]);
}

// Each frame while Stealth is on.
void Stealth(BYTE* player, const Pack& pack, ULONGLONG now) {
    const ULONGLONG fadeMs = static_cast<ULONGLONG>(pack.Number("activatable.active_abilities.stealth.stealth_time_ms", 1000.0f));
    const ULONGLONG restealthMs = static_cast<ULONGLONG>(pack.Number("activatable.active_abilities.stealth.restealth_time_ms", 1000.0f));
    const int controller = *kLocalController;
    const bool attacking = ControlHeld(kPrimaryAttack, controller, 0) || ControlHeld(kAltAttack, controller, 0) || ControlHeld(kMeleeAttack, controller, 0);
    if (attacking) {  // Guerrilla's event_player_attacked: unstealthed, again restealth_time_ms after
        if (g_stealthed) StealthLook(player, pack, false);
        g_restealthAt = now + restealthMs;
        g_stealthFadeFrom = 0;
    } else if (!g_stealthed && (!g_restealthAt || now >= g_restealthAt)) {
        StealthLook(player, pack, true);
        g_restealthAt = 0;
        g_stealthFadeFrom = now;
    }
    if (g_stealthFadeFrom && now - g_stealthFadeFrom >= fadeMs) g_stealthFadeFrom = 0;
    const bool fully = g_stealthed && !g_stealthFadeFrom && !g_restealthAt;
    if (fully != g_fullyStealthed) rfa::LogDetail("Backpacks: Stealth: %s", fully ? "wholly stealthed (the EDF lose him as a target)" : "seen");
    g_fullyStealthed = fully;
}

void StealthOff(BYTE* player) {
    for (auto& [id, pack] : g_packs)
        if (id == 3) StealthLook(player, pack, false);
    g_stealthFadeFrom = g_restealthAt = 0;
    if (g_fullyStealthed) rfa::LogDetail("Backpacks: Stealth: off");
    g_fullyStealthed = false;
}

// ---- Hitting people, vehicles and movers: Concussion's ripple and Rhino's charge
//
// Guerrilla's ripple (multi_object_backpack::use_ripple, RM 0x9F1B10) and Rhino's charge (its process, 0x9F2A70)
// hit what is about him with Guerrilla's own calls; here they are Armageddon's, which are Guerrilla's under other
// addresses (Armageddon's own Nano ripple, 0x713D60 and 0x714BA0, uses these same ones):
//   - the people within a range of a place: 0x495D40 (thiscall: the world; the place, the range, the handles out,
//     how many at most, no filter, no data; how many), each by its handle with 0x436120; the movers alike with
//     0x4AD4D0 and 0x437A00; the vehicles from the world's list (0x477090 how many, 0x4770E0 each) by their place;
//   - an object's type and subtype (+0x7A, +0x7B, signed bytes; 0x422AD0, 0x423530): 0 a person (subtype 3 an NPC,
//     4 the player: Guerrilla's 2 and 3), 2 a mover (subtype 1 a building, rfg_mover, whose flags +0x150 bit 3 is
//     invulnerable), 3 a vehicle (subtype 5 an automobile: Guerrilla's 4; 6 flyers, 8 walkers); dead: its fifth
//     virtual function; a person's invulnerable flag +0xB0 bit 6 (Guerrilla's hflags.invulnerable);
//   - human_ragdoll_start 0x8996F0 (cdecl: the human, no source -1, 0, false, 0, 0: Guerrilla's (human, false, -1);
//     true too for one already ragdolled), the ragdoll human+0x794 (Guerrilla's +0x344), havok_ragdoll_apply_point_
//     velocity 0x795AC0 (cdecl: the ragdoll, &velocity, &point, true) and havok_ragdoll_set_velocity 0x7957C0
//     (cdecl: the ragdoll, &velocity);
//   - human_damage: a damage record (0x42E960 makes it, 0x30 bytes: +0 the victim, +4 the attacker, +8 the damage,
//     +0xC the kind) to 0x85E0C0; the kind Guerrilla's DBT_NONE (0) for the ripple's and Rhino's people;
//   - vehicle_apply_damage 0x6077A0 (cdecl: the vehicle, its part -1, the attacker, no weapon, the kind (5,
//     DBT_MELEE), &where, &direction, 1.0, the damage as an int, &impulse, false: Guerrilla's with the part added),
//     and Guerrilla's vehicle_apply_point_velocity (RM 0xB65030) with Armageddon's parts: the velocity × the
//     vehicle's mass (its virtual +0x5C) as an impulse at the point on its body (+0x4C; 0x778440 -1 to none;
//     0x78BE00 makes Havok's vectors; 0x604500 thiscall: the body, &impulse, &point, which wakes the body too);
//   - a mover's push: explosion_modify_impulses 0x6E6C40 (&impulse, 0, &impulse, 0, its mass, 1, 0, 15, 0), then
//     havok_body_apply_linear_impulse 0x776F50 (its body +0x4C, &impulse).
// Guerrilla's ripple and Rhino also pass a weapon of their own (mp_backpack_ripple, mp_backpack_rhino) to
// human_damage; those weapons aren't ported, so ours pass none.
using CollectFn = int(__thiscall*)(void* world, const Vec3* at, float range, DWORD* out, int most, void* filter, const void* data);
auto const HumansWithin = reinterpret_cast<CollectFn>(0x495D40);
auto const MoversWithin = reinterpret_cast<CollectFn>(0x4AD4D0);
auto const HumanByHandle = reinterpret_cast<BYTE*(__thiscall*)(void* world, DWORD handle)>(0x436120);
auto const MoverByHandle = reinterpret_cast<BYTE*(__thiscall*)(void* world, DWORD handle)>(0x437A00);
auto const VehicleCount = reinterpret_cast<int(__fastcall*)(void* world, void* unused)>(0x477090);
auto const VehicleAt = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int index)>(0x4770E0);
constexpr DWORD kObjectType = 0x7A, kObjectSubtype = 0x7B, kBody = 0x4C, kRagdoll = 0x794;
constexpr int kPersonType = 0, kMoverType = 2, kVehicleType = 3;
constexpr int kBuildingSubtype = 1, kNpcSubtype = 3, kPlayerSubtype = 4, kAutomobileSubtype = 5;
constexpr DWORD kHumanFlags = 0xB0, kHumanInvulnerable = 1u << 6;
constexpr DWORD kMoverFlags = 0x150, kMoverInvulnerable = 1u << 3;
constexpr int kDamagedByNone = 0, kDamagedByMelee = 5;
constexpr int kMostCollected = 64;  // Guerrilla's collect_objects lists (0x40)

auto const RagdollStart = reinterpret_cast<bool(__cdecl*)(BYTE* human, DWORD source, int unknown, bool flag, int unknown2, int unknown3)>(0x8996F0);
auto const RagdollPointVelocity = reinterpret_cast<void(__cdecl*)(int ragdoll, const Vec3* velocity, const Vec3* point, bool flag)>(0x795AC0);
auto const RagdollVelocity = reinterpret_cast<void(__cdecl*)(int ragdoll, const Vec3* velocity)>(0x7957C0);
auto const DamageRecord = reinterpret_cast<void(__thiscall*)(BYTE* record)>(0x42E960);
auto const HumanDamage = reinterpret_cast<float(__cdecl*)(BYTE* record)>(0x85E0C0);
using VehicleDamageFn = bool(__cdecl*)(BYTE* vehicle, int part, BYTE* attacker, const void* weapon, int kind, const Vec3* where,
                                       const Vec3* direction, float scale, int damage, const Vec3* impulse, bool flag);
auto const VehicleDamage = reinterpret_cast<VehicleDamageFn>(0x6077A0);
struct alignas(16) HavokVector {
    float x, y, z, w;
};
auto const ToHavok = reinterpret_cast<void(__cdecl*)(HavokVector* out, const Vec3* in)>(0x78BE00);
auto const BodyPointer = reinterpret_cast<BYTE*(__cdecl*)(DWORD body)>(0x778440);
auto const PointImpulse = reinterpret_cast<void(__thiscall*)(BYTE* body, const HavokVector* impulse, const HavokVector* point)>(0x604500);
using ModifyImpulsesFn = void(__cdecl*)(Vec3* out, Vec3* second, const Vec3* in, const Vec3* secondIn, float mass, float a, float b, float c, float d);
auto const ModifyImpulses = reinterpret_cast<ModifyImpulsesFn>(0x6E6C40);
auto const LinearImpulse = reinterpret_cast<void(__cdecl*)(DWORD body, const Vec3* impulse)>(0x776F50);
using MassFn = float(__thiscall*)(BYTE* object);
constexpr DWORD kMassFunction = 0x5C;
auto const AnyObjectAt = reinterpret_cast<BYTE*(__thiscall*)(void* world, DWORD handle)>(0x6CB9E0);  // any kind
using IsDeadFn = bool(__thiscall*)(BYTE* object);
constexpr DWORD kIsDeadFunction = 0x10;

int TypeOf(const BYTE* object) { return static_cast<signed char>(object[kObjectType]); }
int SubtypeOf(const BYTE* object) { return static_cast<signed char>(object[kObjectSubtype]); }
bool IsDead(BYTE* object) { return (*reinterpret_cast<const IsDeadFn*>(*reinterpret_cast<BYTE* const*>(object) + kIsDeadFunction))(object); }
float MassOf(BYTE* object) { return (*reinterpret_cast<const MassFn*>(*reinterpret_cast<BYTE* const*>(object) + kMassFunction))(object); }
const Vec3& PlaceOf(const BYTE* object) { return *reinterpret_cast<const Vec3*>(object + kPosition); }

Vec3 Minus(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
// Guerrilla's vector::normalize_safe: a vector of no length stays one.
Vec3 Normal(const Vec3& v) {
    const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return length > 1e-6f ? Vec3{v.x / length, v.y / length, v.z / length} : Vec3{0.0f, 0.0f, 0.0f};
}

// The throw both use: from a place towards the one hit, at a speed, its climb at least a minimum.
Vec3 Throw(const Vec3& from, const Vec3& to, float speed, float minimumClimb) {
    const Vec3 d = Normal(Minus(to, from));
    Vec3 v{d.x * speed, d.y * speed, d.z * speed};
    if (v.y < minimumClimb) v.y = minimumClimb;
    return v;
}

void Hurt(BYTE* victim, BYTE* attacker, float damage) {
    alignas(8) BYTE record[0x30];
    DamageRecord(record);
    *reinterpret_cast<BYTE**>(record + 0x0) = victim;
    *reinterpret_cast<BYTE**>(record + 0x4) = attacker;
    *reinterpret_cast<float*>(record + 0x8) = damage;
    *reinterpret_cast<int*>(record + 0xC) = kDamagedByNone;
    HumanDamage(record);
}

// Guerrilla's vehicle_apply_point_velocity.
void PointVelocity(BYTE* vehicle, const Vec3& velocity, const Vec3& point) {
    BYTE* body = BodyPointer(*reinterpret_cast<const DWORD*>(vehicle + kBody));
    if (!body) return;
    const float mass = MassOf(vehicle);
    const Vec3 impulse{velocity.x * mass, velocity.y * mass, velocity.z * mass};
    HavokVector havokImpulse{}, havokPoint{};
    ToHavok(&havokImpulse, &impulse);
    ToHavok(&havokPoint, &point);
    PointImpulse(body, &havokImpulse, &havokPoint);
}

// Concussion (6): Guerrilla's use_ripple, from his place (his activation: activate calls it with his position; a melee
// hit while a pack with a ripple is on: attempt_ripple, MeleeHit below). Its ripple_effect_name on the pack (Guerrilla
// plays it with the pack as its host, at ripple_effect_attach_point_name, none for Concussion); in single player
// then the people, the vehicles and the movers within ripple_range of the place (6 m):
//   - each person but him and the invulnerable: thrown away from him at ripple_player_hit_speed (12 m/s), the climb at
//     least ripple_player_minimum_y_velocity (8): ragdolled, the throw given to the ragdoll at his place
//     (ripple_process_npcs_hit), then hurt for ripple_player_damage (130);
//   - each automobile: the same throw, ripple_player_damage × 4 as vehicle damage (melee) at its place, upwards, the throw
//     its impulse, then half the throw as a point velocity at its place (ripple_process_vehicles_hit);
//   - each mover with a body: pushed away from the place by ripple_mover_impulse (4000), through
//     explosion_modify_impulses.
// Its ripple_local_player_camera_shake is for a player it hits (multiplayer): none in single player.
const char* const kRipple = "activatable.active_abilities.ripple.";
int g_rippleLogs = 0;

bool HasRipple(const Pack& pack) { return pack.Has((std::string(kRipple) + "ripple_range").c_str()); }

void Ripple(BYTE* player, const Pack& pack, const Vec3& at) {
    auto number = [&](const char* field, float otherwise) { return pack.Number((std::string(kRipple) + field).c_str(), otherwise); };
    const float range = number("ripple_range", 0.0f), speed = number("ripple_player_hit_speed", 0.0f);
    const float climb = number("ripple_player_minimum_y_velocity", 0.0f), damage = number("ripple_player_damage", 0.0f);
    const int moverImpulse = static_cast<int>(number("ripple_mover_impulse", 0.0f));
    if (const std::string* effect = Field(pack, (std::string(kRipple) + "ripple_effect_name").c_str())) {
        const std::string* point = Field(pack, (std::string(kRipple) + "ripple_effect_attach_point_name").c_str());
        PlayEffectNamed(player, pack, *effect, false, false, point ? *point : std::string());
    }
    const Vec3 him = PlaceOf(player);
    DWORD handles[kMostCollected];
    int people = 0, vehicles = 0, movers = 0;
    const int humanCount = HumansWithin(kWorld, &at, range, handles, kMostCollected, nullptr, nullptr);
    for (int i = 0; i < humanCount && i < kMostCollected; ++i) {
        BYTE* human = HumanByHandle(kWorld, handles[i]);
        if (!human || human == player || (*reinterpret_cast<const DWORD*>(human + kHumanFlags) & kHumanInvulnerable)) continue;
        const Vec3 throwing = Throw(him, PlaceOf(human), speed, climb);
        if (RagdollStart(human, 0xFFFFFFFF, 0, false, 0, 0)) RagdollPointVelocity(*reinterpret_cast<const int*>(human + kRagdoll), &throwing, &him, true);
        Hurt(human, player, damage);
        ++people;
    }
    const int vehicleCount = VehicleCount(kWorld, nullptr);
    for (int i = 0; i < vehicleCount; ++i) {
        BYTE* vehicle = VehicleAt(kWorld, nullptr, i);
        if (!vehicle || SubtypeOf(vehicle) != kAutomobileSubtype) continue;
        const Vec3 d = Minus(PlaceOf(vehicle), at);
        if (d.x * d.x + d.y * d.y + d.z * d.z > range * range) continue;
        const Vec3 throwing = Throw(him, PlaceOf(vehicle), speed, climb);
        const Vec3 up{0.0f, 1.0f, 0.0f};
        VehicleDamage(vehicle, -1, player, nullptr, kDamagedByMelee, &PlaceOf(vehicle), &up, 1.0f, static_cast<int>(damage * 4.0f), &throwing, false);
        PointVelocity(vehicle, Vec3{throwing.x * 0.5f, throwing.y * 0.5f, throwing.z * 0.5f}, PlaceOf(vehicle));
        ++vehicles;
    }
    if (moverImpulse) {
        const int moverCount = MoversWithin(kWorld, &at, range, handles, kMostCollected, nullptr, nullptr);
        for (int i = 0; i < moverCount && i < kMostCollected; ++i) {
            BYTE* mover = MoverByHandle(kWorld, handles[i]);
            if (!mover || *reinterpret_cast<const DWORD*>(mover + kBody) == 0xFFFFFFFF) continue;
            const Vec3 d = Normal(Minus(PlaceOf(mover), at));
            const float push = static_cast<float>(moverImpulse);
            Vec3 impulse{d.x * push, d.y * push, d.z * push};
            ModifyImpulses(&impulse, nullptr, &impulse, nullptr, MassOf(mover), 1.0f, 0.0f, 15.0f, 0.0f);
            LinearImpulse(*reinterpret_cast<const DWORD*>(mover + kBody), &impulse);
            ++movers;
        }
    }
    if (g_rippleLogs < 24) {
        ++g_rippleLogs;
        rfa::LogDetail("Backpacks: %s's ripple at (%.1f, %.1f, %.1f): %d people thrown and hurt (%.0f), %d vehicles, %d movers pushed, within %.1f m",
                 pack.name.c_str(), at.x, at.y, at.z, people, damage, vehicles, movers, range);
    }
}

// Rhino (8): Guerrilla's charge and rhino abilities while it is on (process, RM 0x9F2A70).
//   - The charge: his move_speed (Guerrilla's human +0x28C) charge_top_speed (8.2 m/s) × (time on / charge_ramp_up_time_
//     seconds)^2.5 while ramping up, × (fuel time left / charge_ramp_down_time_seconds) while ramping down (both 0 for
//     Rhino: its full speed at once), and his own walking left out (control_gameplay_process with a charge). Armageddon's
//     player has no move_speed to set, so his controller's velocity along the ground is that speed towards where the
//     camera has him look (gear::CameraLook), each frame before the controller's step; his climb stays his.
//   - The hits: unless the cast timer (rhino_shape_cutter_timer) runs, a sphere of 0.855 m cast along his velocity
//     (havok_spherecast; Armageddon's 0x79A4A0, cdecl: the ray, its hits; the ray made by 0x424AD0 with the game's
//     default collision, Guerrilla's COLLIDE_RAY_DEFAULT; +0x20 the filter's data, +0x24 the filter, cdecl (object,
//     data) -> take it; its hits made and freed by 0x424B40 / 0x424CD0): from 0.95 m up his body and 0.475 m behind
//     him, 0.095 m higher, as far as his speed × min(0.45, max(fuel time left, 0.4)) / 0.95 (at least 0.1 s). Guerrilla's
//     filter (rhino_cast_filter_callback, 0x97C170) takes a mover (not an invulnerable building), a living NPC or
//     automobile not hit yet this charge (none of them with under 0.2 s of fuel left), and marks the cast as having hit;
//     a cast that hit nothing waits 100 ms for the next.
//   - What the first hit is: a mover: a shape cutter 0.475 m along his velocity from the hit (guerrilla_world's
//     GuerrillaWorld_CutBuildings, as Guerrilla's shape_cutter_props: his turn, 0.95 m, mp_rhino_cutter, him its source),
//     and the rhino_mover_hit_effect there at most every 500 ms (Guerrilla's active Rhino names none); an NPC (64 at most
//     a charge): thrown from the hit at rhino_player_hit_speed (12 m/s), the climb at least rhino_player_minimum_y_
//     velocity (8), hurt for rhino_player_hit_damage (225), ragdolled with that velocity (rhino_process_npc_hit); an
//     automobile: the same throw, the damage × 4 as melee damage at the hit (its normal the direction, the throw its
//     impulse), then the throw as a point velocity at the hit (rhino_process_vehicle_hit). Its rhino_local_player_camera_
//     shake and radial blur are for a player it hits (multiplayer): none in single player.
struct Ray {
    float from[3], to[3];
    float radius;
    int passThrough;   // -1
    void* filterData;  // +0x20
    void* filter;      // +0x24
    DWORD collision, collector, flags;
};
static_assert(sizeof(Ray) == 0x34, "the game's ray");
struct RayHit {
    float at[3], centre[3], normal[3];
    float along;
    DWORD handle;
    DWORD rest[3];
};
static_assert(sizeof(RayHit) == 0x38, "the game's ray hit");
struct RayHits {
    RayHit* hits;
    int count;
    DWORD capacityAndFlags;
};
auto const RayInit = reinterpret_cast<void(__thiscall*)(Ray* ray)>(0x424AD0);
auto const HitsInit = reinterpret_cast<void(__thiscall*)(RayHits* hits)>(0x424B40);
auto const HitsFree = reinterpret_cast<void(__thiscall*)(RayHits* hits)>(0x424CD0);
auto const SphereCast = reinterpret_cast<bool(__cdecl*)(const Ray* ray, RayHits* hits)>(0x79A4A0);

const char* const kRhino = "activatable.active_abilities.rhino.";
const char* const kCharge = "activatable.active_abilities.charge.";
constexpr float kRhinoCastRadius = 0.855f, kRhinoCastUp = 0.95f, kRhinoCastBehind = 0.475f, kRhinoCastLift = 0.095f;
constexpr float kRhinoCutAhead = 0.475f, kRhinoCutRadius = 0.95f;
constexpr ULONGLONG kRhinoIdleCastMs = 100, kRhinoMoverEffectMs = 500;
constexpr size_t kRhinoMostHits = 64;

std::vector<DWORD> g_rhinoHit;     // the NPCs and vehicles hit this charge (Guerrilla's rhino_hit_npcs)
ULONGLONG g_rhinoNextCast = 0;     // its cast timer (0: cast now)
ULONGLONG g_rhinoEffectAt = 0;     // its mover hit effect's timer
bool g_cutLooked = false;
int g_rhinoLogs = 0;
float g_rhinoSpeedAsked = -1.0f;   // the charge's speed set, looked at a frame on (a diagnostic, once a charge)
bool g_rhinoSpeedLooked = false;

void RhinoLog(const char* format, ...) {
    if (g_rhinoLogs >= 40) return;
    ++g_rhinoLogs;
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    rfa::LogDetail("Backpacks: Rhino: %s", text);
}

struct RhinoCastData {
    BYTE* player;
    bool hit;          // the filter took something (Guerrilla's data +4)
    bool nearlyEmpty;  // under 0.2 s of fuel left (+5)
};

bool RhinoHitAlready(const BYTE* object) {
    return std::find(g_rhinoHit.begin(), g_rhinoHit.end(), HandleOfObject(object)) != g_rhinoHit.end();
}

// Guerrilla's rhino_cast_filter_callback.
bool __cdecl RhinoCastFilter(BYTE* object, void* data) {
    auto* cast = static_cast<RhinoCastData*>(data);
    if (!cast || !object) return false;
    const int type = TypeOf(object), subtype = SubtypeOf(object);
    bool take = false;
    if (type == kMoverType) {
        if (subtype == kBuildingSubtype && (*reinterpret_cast<const DWORD*>(object + kMoverFlags) & kMoverInvulnerable)) return false;
        take = true;
    } else if ((type == kPersonType && subtype == kNpcSubtype && object != cast->player) || (type == kVehicleType && subtype == kAutomobileSubtype)) {
        if (IsDead(object) || cast->nearlyEmpty) return false;
        take = !RhinoHitAlready(object);
    }
    if (take) cast->hit = true;
    return take;
}

void Rhino(BYTE* player, const Pack& pack, ULONGLONG now) {
    auto field = [&](const char* block, const char* name, float otherwise) { return pack.Number((std::string(block) + name).c_str(), otherwise); };
    const float use = pack.Number("activatable.uses_fuel.fuel_use_per_second", 0.0f);
    const float timeLeft = use > 0.0f ? g_fuel / use : 1000.0f;  // Guerrilla's fuel_timer: when it runs dry, in seconds
    float velocity[3] = {};
    if (!gear::Velocity(player, velocity)) return;  // not on foot
    if (g_rhinoSpeedAsked >= 0.0f) {
        RhinoLog("its speed a frame on: %.1f m/s along the ground (it set %.1f)", std::sqrt(velocity[0] * velocity[0] + velocity[2] * velocity[2]), g_rhinoSpeedAsked);
        g_rhinoSpeedAsked = -1.0f;
    }
    // The charge.
    if (pack.Has((std::string(kCharge) + "charge_top_speed").c_str())) {
        const float top = field(kCharge, "charge_top_speed", 0.0f);
        const float rampUp = field(kCharge, "charge_ramp_up_time_seconds", 0.0f), rampDown = field(kCharge, "charge_ramp_down_time_seconds", 0.0f);
        const float since = static_cast<float>(now - g_onSince) / 1000.0f;
        float share = 1.0f;
        if (rampUp > 0.0f && since < rampUp) share = std::pow(since / rampUp, 2.5f);
        else if (rampDown > 0.0f && timeLeft < rampDown) share = timeLeft / rampDown;
        share = std::fmax(0.0f, std::fmin(1.0f, share));
        float look[3] = {};
        gear::CameraLook(player, look);
        float x = look[0], z = look[2], length = std::sqrt(x * x + z * z);
        if (length < 1e-4f) {  // looking straight up or down: his facing
            const float* forward = reinterpret_cast<const float*>(player + kOrient + 0x18);
            x = forward[0];
            z = forward[2];
            length = std::sqrt(x * x + z * z);
        }
        if (length > 1e-4f) {
            const float speed = top * share;
            velocity[0] = x / length * speed;
            velocity[2] = z / length * speed;
            gear::SetVelocity(player, velocity);
            if (!g_rhinoSpeedLooked) {
                g_rhinoSpeedLooked = true;
                g_rhinoSpeedAsked = speed;
                RhinoLog("the charge: %.1f m/s towards where he looks (charge_top_speed %.1f)", speed, top);
            }
        }
    }
    // The hits.
    if (!pack.Has((std::string(kRhino) + "rhino_player_hit_damage").c_str()) || now < g_rhinoNextCast) return;
    const Vec3 v{velocity[0], velocity[1], velocity[2]};
    const float speed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    float lookahead = std::fmin(0.45f, std::fmax(timeLeft, 0.4f));
    if (lookahead < 0.1f) lookahead = 0.1f;
    const float length = speed * lookahead / 0.95f;
    const Vec3 dir = Normal(v);
    const Vec3 him = PlaceOf(player);
    const Vec3 start{him.x - dir.x * kRhinoCastBehind, him.y + kRhinoCastUp - dir.y * kRhinoCastBehind, him.z - dir.z * kRhinoCastBehind};
    RhinoCastData data{player, false, timeLeft < 0.2f};
    Ray ray;
    RayInit(&ray);
    ray.from[0] = start.x;
    ray.from[1] = start.y + kRhinoCastLift;
    ray.from[2] = start.z;
    ray.to[0] = start.x + dir.x * length;
    ray.to[1] = start.y + dir.y * length + kRhinoCastLift;
    ray.to[2] = start.z + dir.z * length;
    ray.radius = kRhinoCastRadius;
    ray.filterData = &data;
    ray.filter = reinterpret_cast<void*>(&RhinoCastFilter);
    ray.collector = 0;  // the nearest
    ray.flags = 0;
    RayHits hits;
    HitsInit(&hits);
    if (SphereCast(&ray, &hits) && hits.count >= 1 && hits.hits) {
        const RayHit hit = hits.hits[0];
        const Vec3 centre{hit.centre[0], hit.centre[1], hit.centre[2]};
        BYTE* object = AnyObjectAt(kWorld, hit.handle);
        const int type = object ? TypeOf(object) : -1, subtype = object ? SubtypeOf(object) : -1;
        const float hitSpeed = field(kRhino, "rhino_player_hit_speed", 0.0f), climb = field(kRhino, "rhino_player_minimum_y_velocity", 0.0f);
        const float damage = field(kRhino, "rhino_player_hit_damage", 0.0f);
        if (type == kMoverType) {
            const float cut[3] = {centre.x + dir.x * kRhinoCutAhead, centre.y + dir.y * kRhinoCutAhead, centre.z + dir.z * kRhinoCutAhead};
            if (!g_cutLooked) {
                g_cutLooked = true;
                if (!guerrilla_world::Running()) rfa::Log("Backpacks: Rhino: guerrilla_world is off; it doesn't cut through buildings");
            }
            const bool cutting = GuerrillaWorld_CutBuildings(cut, reinterpret_cast<const float*>(player + kOrient), kRhinoCutRadius, "mp_rhino_cutter",
                                                             HandleOfObject(player));
            RhinoLog("ran into a mover (subtype %d) at (%.1f, %.1f, %.1f): %s", subtype, centre.x, centre.y, centre.z, cutting ? "cut" : "no cutter made");
            if (now >= g_rhinoEffectAt) {
                if (const std::string* effect = Field(pack, (std::string(kRhino) + "rhino_mover_hit_effect").c_str())) {
                    int number = EffectNumber(("rfg_" + *effect).c_str());
                    if (number < 0) number = EffectNumber(effect->c_str());
                    if (number >= 0) EffectsPlay(number, cut, reinterpret_cast<const float*>(player + kOrient), false, 0xFFFFFFFF, -1, 0xFFFFFFFF, false, 0, 0);
                    else Missing("the effect " + *effect);
                }
                g_rhinoEffectAt = now + kRhinoMoverEffectMs;
            }
        } else if (type == kPersonType && subtype == kNpcSubtype && g_rhinoHit.size() < kRhinoMostHits) {
            const Vec3 throwing = Throw(centre, PlaceOf(object), hitSpeed, climb);
            Hurt(object, player, damage);
            if (RagdollStart(object, 0xFFFFFFFF, 0, false, 0, 0)) RagdollVelocity(*reinterpret_cast<const int*>(object + kRagdoll), &throwing);
            g_rhinoHit.push_back(HandleOfObject(object));
            RhinoLog("ran into a person: hurt for %.0f, thrown at (%.1f, %.1f, %.1f) m/s", damage, throwing.x, throwing.y, throwing.z);
        } else if (type == kVehicleType && subtype == kAutomobileSubtype && g_rhinoHit.size() < kRhinoMostHits) {
            const Vec3 throwing = Throw(centre, PlaceOf(object), hitSpeed, climb);
            const Vec3 at{hit.at[0], hit.at[1], hit.at[2]}, normal{hit.normal[0], hit.normal[1], hit.normal[2]};
            VehicleDamage(object, -1, player, nullptr, kDamagedByMelee, &at, &normal, 1.0f, static_cast<int>(damage * 4.0f), &throwing, false);
            PointVelocity(object, throwing, at);
            g_rhinoHit.push_back(HandleOfObject(object));
            RhinoLog("ran into a vehicle: %d damage, pushed at (%.1f, %.1f, %.1f) m/s", static_cast<int>(damage * 4.0f), throwing.x, throwing.y, throwing.z);
        }
    }
    HitsFree(&hits);
    g_rhinoNextCast = data.hit ? 0 : now + kRhinoIdleCastMs;
}

// ---- The packs switched on with their key

// Guerrilla's ramp-up: 300 ms from the key (player_toggle_backpack_activation's, from control_gameplay_process), then
// activate if the pack can still (process_ramp_up -> can_activate: its fuel).
constexpr ULONGLONG kRampUpMs = 300;
ULONGLONG g_rampUntil = 0;  // a ramp-up running (0: none)

// Whether its activatable block has a deactivatable block (backpacks.txt gives that block's fields, or the block
// alone): without one Guerrilla's activate leaves the pack off again as it finishes.
bool Deactivatable(const Pack& pack) {
    static const std::string kBlock = "activatable.deactivatable";
    for (const auto& [path, value] : pack.values)
        if (path == kBlock || path.rfind(kBlock + ".", 0) == 0) return true;
    return false;
}

// Guerrilla's activate, for what this mod has of it: the fuel taken, on, its effects, shakes and sounds (PackOn), its
// kaboom (use_kaboom) and its ripple from his place (use_ripple); Rhino's charge starts with no one hit yet; off again
// at once without a deactivatable block.
void Activate(BYTE* player, const Pack& pack, float cost) {
    g_on = true;
    g_onSince = GetTickCount64();
    g_boosted = false;
    g_fuel = std::fmax(0.0f, g_fuel - cost);
    g_rhinoHit.clear();
    g_rhinoNextCast = 0;
    g_rhinoEffectAt = 0;
    g_rhinoSpeedLooked = false;
    rfa::LogDetail("Backpacks: %s on (fuel %.2f)", pack.name.c_str(), g_fuel);
    PackOn(player, pack);  // its effects, shakes and sounds
    g_nextKaboom = 0;
    if (HasKaboom(pack)) {
        Kaboom(player, pack);
        g_nextKaboom = g_onSince + static_cast<ULONGLONG>(pack.Number("activatable.active_abilities.kaboom.kaboom_delay_ms", 300.0f));
    }
    if (HasRipple(pack)) Ripple(player, pack, PlaceOf(player));
    if (!Deactivatable(pack)) {
        g_on = false;
        PackOff(player);
        rfa::LogDetail("Backpacks: %s spent as it went on (Guerrilla's: no deactivatable block; fuel %.2f)", pack.name.c_str(), g_fuel);
    }
}

void Activated(BYTE* player, const Pack& pack, float seconds) {
    const bool fuel = pack.Has("activatable.uses_fuel.fuel_use_per_second") || pack.Has("activatable.uses_fuel.minimum_fuel_to_activate");
    const float use = pack.Number("activatable.uses_fuel.fuel_use_per_second", 0.0f);
    const float regen = pack.Number("activatable.uses_fuel.fuel_regen_per_second", 0.0f);
    const float cost = pack.Number("activatable.uses_fuel.fuel_activation_cost", 0.0f);
    const float minimum = pack.Number("activatable.uses_fuel.minimum_fuel_to_activate", 0.0f);
    const bool playerOff = pack.Flag("activatable.deactivatable.player_deactivatable");
    const ULONGLONG now = GetTickCount64();
    if (KeyPressed()) {
        if (!g_on && !g_rampUntil) {
            if (!fuel || g_fuel + 0.0001f >= minimum) {
                if (pack.id == 1) g_thrustUp = !ControlHeld(kJump, *kLocalController, 0);  // Thrust: jump held, the slam down
                g_rampUntil = now + kRampUpMs;
                RampUp(player, pack);
            } else {
                rfa::LogDetail("Backpacks: %s needs %.2f fuel to switch on (has %.2f)", pack.name.c_str(), minimum, g_fuel);
                PackSound(player, pack, kNoFuelFoley);
            }
        } else if (g_on && playerOff) {
            g_on = false;
            PackOff(player);
            rfa::LogDetail("Backpacks: %s off (fuel %.2f)", pack.name.c_str(), g_fuel);
        }
    }
    if (g_rampUntil && now >= g_rampUntil) {
        g_rampUntil = 0;
        if (!fuel || g_fuel + 0.0001f >= minimum) Activate(player, pack, cost);
        else rfa::LogDetail("Backpacks: %s's ramp-up over, but it has %.2f fuel of the %.2f it needs: not on", pack.name.c_str(), g_fuel, minimum);
    }
    if (g_on) {
        if (!WhileOn(player, pack, seconds)) {
            g_on = false;
            PackOff(player);
            rfa::LogDetail("Backpacks: %s spent (fuel %.2f)", pack.name.c_str(), g_fuel);
            return;
        }
        g_fuel = std::fmax(0.0f, g_fuel - use * seconds);
        if (fuel && g_fuel <= 0.0f) {
            g_on = false;
            PackOff(player);
            rfa::LogDetail("Backpacks: %s ran dry", pack.name.c_str());
        }
    } else {
        const float before = g_fuel;
        Refill(regen, seconds);
        if (fuel && minimum > 0.0f && before + 0.0001f < minimum && g_fuel + 0.0001f >= minimum) PackSound(player, pack, kRefuelFoley);
    }
}

// ---- The worn pack on his back
//
// Guerrilla's rfg_PC lists its packs as a prop (Backpack, Chance 0) and puts the worn one on at run time
// (human_wear_prop). Armageddon picks a character's props once, as it is made (0x862BD0, for each prop
// 0x8636D1-0x8638D9), so the mod makes the worn pack's models and attaches them the way that code does:
//   - the item: items_3d by name (0x7CF440); ours carry a container of their own (+0x18), which takes
//     the props maker 0x7CCF00 (thiscall: props, the item, &position, &orientation, &flags, &word, the
//     world's collision setting [0x1975E94], -1.0f, -1; ret 0x20);
//   - the flags (0x465320, thiscall) | 2 (no physics) | 0x400 (a character's prop); the word (0x9195A0,
//     thiscall) set to the human's +0x224;
//   - the object made (0x611000, thiscall: the world, &props, the human's handle), the props let go
//     (0x694580); its prop type (0x78F3A0, thiscall: object+0xCA, the type: -1, Armageddon has no
//     "jetpack") and its owner (its function +0xA0: the human's handle, 0);
//   - attached (0x7B01D0, thiscall: the human, the object, the point, the object's own point, false, true).
//     The point, as the character load finds a prop's (0x88A740): the Character_Prop's tag on the wearer's
//     skeleton (0xD8A140: the skeleton, the name; bone count + the tag), else the bone of that name
//     (0xD8A120), else -1 for a character model, which follows his bones; the skeleton
//     0x973430(<entry+0x58>.rig_pc). The object's own point: a prop with an Item_Prop is made with the flag
//     0x80, and the game's prop step (0x864C80) attaches it again by that tag of the model's own, found by
//     name with the object's function +0x2C (thiscall: the name; -1 if none), so it sits on him as Guerrilla's
//     (Thrust's Attach_To_Spine2 on spine2); that step finds the prop in his character entry's list, where
//     Guerrilla's packs aren't, so the mod gives the tag itself (and no 0x80). Without it the model's middle
//     sat on the bone, inside him (the user's 11:30 run).
// Taken off (the world's destroy 0x6D5450) when another pack or none is worn, or another player. Its handle
// is looked up with the world's own lookup for any object (0x6CB9E0; 0x436250 finds only one kind, so the
// models seemed gone each second and were made again, one inside the other).
// Only for Mason (the models are Guerrilla's for his skeleton), a second after he is first seen.
auto const ItemInfoNamed = reinterpret_cast<BYTE*(__cdecl*)(const char* name)>(0x7CF440);
auto const InitFlags = reinterpret_cast<void(__thiscall*)(DWORD* flags)>(0x465320);
auto const InitWord = reinterpret_cast<void(__thiscall*)(DWORD* word)>(0x9195A0);
auto const MakeProps = reinterpret_cast<void(__thiscall*)(BYTE* props, BYTE* item, const float* position, const float* orientation,
                                                          DWORD* flags, DWORD* word, DWORD collide, float unknown, int none)>(0x7CCF00);
auto const CreateObject = reinterpret_cast<BYTE*(__thiscall*)(void* world, BYTE* props, DWORD owner)>(0x611000);
auto const FreeProps = reinterpret_cast<void(__thiscall*)(BYTE* props)>(0x694580);
auto const SetPropType = reinterpret_cast<void(__thiscall*)(BYTE* typeField, int type)>(0x78F3A0);
auto const AttachToHuman = reinterpret_cast<bool(__thiscall*)(BYTE* human, BYTE* object, int point, int objectPoint, bool, bool)>(0x7B01D0);
auto const HandleOf = reinterpret_cast<DWORD(__thiscall*)(const BYTE* object)>(0x424850);
auto const ObjectAt = reinterpret_cast<BYTE*(__thiscall*)(void* world, DWORD handle)>(0x6CB9E0);
auto const DestroyObject = reinterpret_cast<void(__thiscall*)(void* world, BYTE* object, bool now)>(0x6D5450);
auto const SkeletonNamed = reinterpret_cast<BYTE*(__cdecl*)(const char* name)>(0x973430);
auto const SkeletonTag = reinterpret_cast<int(__cdecl*)(BYTE* skeleton, const char* name)>(0xD8A140);
auto const SkeletonBone = reinterpret_cast<int(__cdecl*)(BYTE* skeleton, const char* name)>(0xD8A120);
// A character model (items_3d character_mesh, inherit_bone_transforms) follows its wearer's bones once bound
// to him: 0x850010 (thiscall: the human, the item; false for an item without its character instance, object
// +0x55 bit 5), as the game's own prop step (0x864FE0, for a prop whose item has flags 0x6000) and its add of a
// carried item (0x84FFF6) do after attaching it; it marks the item bound (+0x108 |= 0x20000).
auto const BindToBones = reinterpret_cast<bool(__thiscall*)(BYTE* human, BYTE* item)>(0x850010);
using SetOwnerFn = void(__thiscall*)(BYTE* object, DWORD owner, int unknown);
using TagNamedFn = int(__thiscall*)(BYTE* object, const char* name);
const DWORD* const kCollisionSetting = reinterpret_cast<const DWORD*>(0x1975E94);
constexpr DWORD kItemContainer = 0x18;    // an item's container of its own (null: it's in its character's)
constexpr DWORD kPropFlagsWord = 0x224;   // human: the word a prop takes
constexpr DWORD kPropType = 0xCA;         // object: its prop type
constexpr DWORD kSetOwner = 0xA0;         // object: the function that names its owner
constexpr DWORD kTagNamed = 0x2C;         // object: the function that finds a tag of its model by name
constexpr DWORD kCharacterEntry = 0x4FC;  // human: its character entry
constexpr DWORD kSkeletonName = 0x58;     // character entry: its skeleton's name
constexpr DWORD kNoPhysics = 0x2, kCharacterProp = 0x400;
constexpr ULONGLONG kSettleMs = 1000;

BYTE* g_dressed = nullptr;        // the player wearing the models, and which pack
int g_dressedPack = -1;
std::vector<DWORD> g_models;      // the models' handles
BYTE* g_seenPlayer = nullptr;
ULONGLONG g_seenSince = 0;
ULONGLONG g_nextModelCheck = 0;

void TakeOff() {
    StopPackLoop();  // it rides on the model
    for (DWORD handle : g_models)
        if (BYTE* object = ObjectAt(kWorld, handle)) DestroyObject(kWorld, object, true);
    g_models.clear();
    g_dressed = nullptr;
    g_dressedPack = -1;
}

BYTE* PackModel() {
    for (DWORD handle : g_models)
        if (BYTE* object = ObjectAt(kWorld, handle)) return object;
    return nullptr;
}

// The point a model hangs on: its Character_Prop's tag on his skeleton, else that bone, else -1.
int PointOn(BYTE* player, const std::string& name, std::string& how) {
    if (name.empty()) {
        how = "follows his bones";
        return -1;
    }
    const BYTE* entry = *reinterpret_cast<BYTE* const*>(player + kCharacterEntry);
    const char* rig = entry ? *reinterpret_cast<const char* const*>(entry + kSkeletonName) : nullptr;
    if (!rig || !*rig) {
        how = "no skeleton name in his entry";
        return -1;
    }
    std::string file = rig;
    if (const size_t dot = file.find_last_of('.'); dot != std::string::npos) file.resize(dot);
    file += ".rig_pc";
    BYTE* skeleton = SkeletonNamed(file.c_str());
    if (!skeleton) {
        how = "his skeleton " + file + " isn't loaded";
        return -1;
    }
    int point = SkeletonTag(skeleton, name.c_str());
    if (point >= 0) {
        how = name + " (a tag of " + file + ")";
        return point;
    }
    point = SkeletonBone(skeleton, name.c_str());
    how = point >= 0 ? name + " (a bone of " + file + ")" : "no " + name + " on " + file;
    return point;
}

// One model made and attached; its handle, or 0.
DWORD PutOnModel(BYTE* player, const Pack& pack, const Worn& worn) {
    BYTE* item = ItemInfoNamed(worn.item.c_str());
    std::string name = worn.item;
    if (!item) {
        name = "rfg_" + worn.item;
        item = ItemInfoNamed(name.c_str());
    }
    if (!item) {
        rfa::Log("Backpacks: %s's model %s isn't an item (build the weapons part again)", pack.name.c_str(), worn.item.c_str());
        return 0;
    }
    if (!*reinterpret_cast<void* const*>(item + kItemContainer)) {
        rfa::Log("Backpacks: %s's model %s has no container of its own; not worn", pack.name.c_str(), name.c_str());
        return 0;
    }
    DWORD flags = 0, word = 0;
    InitFlags(&flags);
    flags |= kNoPhysics | kCharacterProp;
    InitWord(&word);
    word = *reinterpret_cast<const DWORD*>(player + kPropFlagsWord);
    alignas(16) BYTE props[64] = {};
    MakeProps(props, item, reinterpret_cast<const float*>(player + kPosition), reinterpret_cast<const float*>(player + kOrient),
              &flags, &word, *kCollisionSetting, -1.0f, -1);
    const DWORD owner = HandleOf(player);
    BYTE* object = CreateObject(kWorld, props, owner);
    FreeProps(props);
    if (!object) {
        rfa::Log("Backpacks: %s's model %s couldn't be made", pack.name.c_str(), name.c_str());
        return 0;
    }
    SetPropType(object + kPropType, -1);
    const SetOwnerFn setOwner = *reinterpret_cast<const SetOwnerFn*>(*reinterpret_cast<BYTE* const*>(object) + kSetOwner);
    setOwner(object, owner, 0);
    std::string how;
    const int point = PointOn(player, worn.characterProp, how);
    int tag = -1;  // the model's own point that goes on his (Guerrilla's Item_Prop), as the game's prop step finds it
    if (!worn.itemProp.empty()) {  // a model's tags are named in lower case (their hashes: Thrust's attach_to_spine2)
        std::string lower = worn.itemProp;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const TagNamedFn tagNamed = *reinterpret_cast<const TagNamedFn*>(*reinterpret_cast<BYTE* const*>(object) + kTagNamed);
        tag = tagNamed(object, lower.c_str());
    }
    const bool attached = AttachToHuman(player, object, point, tag, false, true);
    const bool bound = attached && worn.characterProp.empty() && BindToBones(player, object);
    char itsOwn[96] = "";
    if (!worn.itemProp.empty())
        std::snprintf(itsOwn, sizeof itsOwn, tag >= 0 ? ", by its own tag %s (%d)" : ", its own tag %s isn't on the model: by its middle",
                      worn.itemProp.c_str(), tag);
    rfa::LogDetail("Backpacks: %s's model %s %s (%s%s%s)", pack.name.c_str(), name.c_str(), attached ? "on his back" : "made but not attached",
             how.c_str(), itsOwn, !worn.characterProp.empty() ? "" : bound ? ", bound to his bones" : ", NOT bound to his bones");
    if (!attached) {
        DestroyObject(kWorld, object, true);
        return 0;
    }
    return HandleOf(object);
}

// The plain models hang on a point of his (Thrust's, Heal's: spine2); the character models (the jetpack and
// its straps, Fleetfoot, Stealth, Vision, Firepower, Concussion, Rhino, Tremor) follow his bones (bound, above).
constexpr bool kCharacterModelsToo = true;

void PutOn(BYTE* player, const Pack& pack) {
    g_dressed = player;
    g_dressedPack = pack.id;
    if (pack.worn.empty()) rfa::Log("Backpacks: %s has no model to wear in backpacks.txt (build the weapons part again)", pack.name.c_str());
    for (const Worn& worn : pack.worn) {
        if (worn.characterProp.empty() && !kCharacterModelsToo) {
            rfa::LogDetail("Backpacks: %s's model %s follows his bones (a character model): not worn yet", pack.name.c_str(), worn.item.c_str());
            continue;
        }
        if (const DWORD handle = PutOnModel(player, pack, worn)) g_models.push_back(handle);
    }
}

// Keeps the worn pack's models on Mason: put on, swapped, taken off, made again if the game took them.
void Dress(BYTE* player, int worn) {
    const ULONGLONG now = GetTickCount64();
    if (player != g_seenPlayer) {
        g_seenPlayer = player;
        g_seenSince = now;
    }
    const bool wanted = worn >= 0 && gear::PlaysMason(player) && now - g_seenSince >= kSettleMs;
    if (g_dressed && (!wanted || g_dressed != player || g_dressedPack != worn)) TakeOff();
    if (!wanted) return;
    if (g_dressed && now >= g_nextModelCheck) {  // a model the game has taken away (a level loaded): all again
        g_nextModelCheck = now + 1000;
        for (DWORD handle : g_models)
            if (!ObjectAt(kWorld, handle)) {
                g_models.clear();
                g_dressed = nullptr;
                break;
            }
    }
    if (g_dressed) return;
    auto found = g_packs.find(worn);
    if (found != g_packs.end()) {
        PutOn(player, found->second);
    } else {
        g_dressed = player;  // a pack backpacks.txt hasn't: nothing to wear, and nothing to try again
        g_dressedPack = worn;
    }
}

// ---- The packs' bullet hit effects: Firepower's explosive impacts, Thrust's and Rhino's steel
//
// Guerrilla's weapons_create_bullet_hit_effects (RM 0xBDBDC0) asks the shooter's backpack and the hit player's for
// effects while they are on (get_backpack_abilities): the shooter's additional_bullet_hit_effect_other (Firepower's
// wep_bullet_impact_exp), played where the bullet lands besides its own effect, and its replacement_bullet_hit_effect_
// other_player in place of it when the one hit is a player (multiplayer only); the hit player's additional_bullet_hit_
// effect_self besides, and his replacement_bullet_hit_effect_self (Thrust's and Rhino's wep_bullet_impact_steelsolid)
// in place of the material's effect (the weapon's own effect and the decal stay). Armageddon kept the same four in
// its own (0x72C840, cdecl: the shooter, the shot's record, a scale; the hit's place at the record's +0x28), but reads
// them from the player's Nano ability while one is on: 0x8BEFE0 (cdecl: the player; that ability's record, else null),
// its +0x190, +0x194, +0x198 and +0x19C, each an effect's record, played with 0x4EB4B0 (it plays +0x194 on any person
// hit). So while a pack with any of them is on, 0x8BEFE0's two calls from 0x72C840 (told by where they return to) get
// a copy of the Nano ability's record, or an empty one, holding the pack's effects; +0x194 stays the Nano ability's,
// there being no other players. An effect's record: its number by name (0x4EF370), then 0x4EF320.
using ActiveAbilityFn = BYTE*(__cdecl*)(BYTE* player);
constexpr DWORD kActiveAbility = 0x8BEFE0;
constexpr BYTE kActiveAbilityPrologue[] = {0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x08, 0x00};  // push ebp / mov ebp,esp / cmp dword ptr [ebp+8],0
constexpr DWORD kBulletHitEffects = 0x72C840, kBulletHitEffectsEnd = 0x72CC0D;         // Armageddon's weapons_create_bullet_hit_effects
constexpr size_t kAbilityRecordSize = 0x2E8;                                             // an abilities.xtbl record
ActiveAbilityFn g_activeAbility = nullptr;
alignas(16) BYTE g_bulletAbility[kAbilityRecordSize];
auto const EffectRecord = reinterpret_cast<const BYTE*(__cdecl*)(int number)>(0x4EF320);
auto const LocalPlayerHuman = reinterpret_cast<BYTE*(__cdecl*)()>(0x8B92D0);

struct BulletEffect {
    const char* field;  // in the pack's active_abilities
    DWORD slot;         // in Armageddon's ability record
};
const BulletEffect kBulletEffects[] = {
    {"additional_bullet_hit_effect_other", 0x190},
    {"additional_bullet_hit_effect_self", 0x198},
    {"replacement_bullet_hit_effect_self", 0x19C},
};
std::vector<std::string> g_bulletEffectsSaid;  // each pack effect's first use, in the log

const BYTE* EffectRecordNamed(const std::string& name) {
    int number = EffectNumber(("rfg_" + name).c_str());
    if (number < 0) number = EffectNumber(name.c_str());
    if (number < 0) {
        Missing("the effect " + name);
        return nullptr;
    }
    return EffectRecord(number);
}

BYTE* __cdecl ActiveAbility(BYTE* player) {
    BYTE* own = g_activeAbility(player);
    const DWORD from = reinterpret_cast<DWORD>(_ReturnAddress());
    if (from < kBulletHitEffects || from >= kBulletHitEffectsEnd || !player || !g_on || player != LocalPlayerHuman()) return own;
    auto found = g_packs.find(g_wearing);
    if (found == g_packs.end()) return own;
    const BYTE* effects[std::size(kBulletEffects)] = {};
    bool any = false;
    for (size_t i = 0; i < std::size(kBulletEffects); ++i)
        if (const std::string* name = Field(found->second, (std::string("activatable.active_abilities.") + kBulletEffects[i].field).c_str())) {
            effects[i] = EffectRecordNamed(*name);
            if (!effects[i]) continue;
            any = true;
            const std::string said = found->second.name + " " + kBulletEffects[i].field;
            if (std::find(g_bulletEffectsSaid.begin(), g_bulletEffectsSaid.end(), said) == g_bulletEffectsSaid.end() && g_bulletEffectsSaid.size() < 16) {
                g_bulletEffectsSaid.push_back(said);
                rfa::LogDetail("Backpacks: %s's %s %s plays at a bullet's hit (%s)", found->second.name.c_str(), kBulletEffects[i].field, name->c_str(),
                         kBulletEffects[i].slot == 0x190 ? "his shots" : "shots that hit him");
            }
        }
    if (!any) return own;
    if (own) memcpy(g_bulletAbility, own, sizeof g_bulletAbility);
    else memset(g_bulletAbility, 0, sizeof g_bulletAbility);
    for (size_t i = 0; i < std::size(kBulletEffects); ++i)
        if (effects[i]) *reinterpret_cast<const BYTE**>(g_bulletAbility + kBulletEffects[i].slot) = effects[i];
    return g_bulletAbility;
}

void BulletEffects() {
    if (memcmp(reinterpret_cast<const void*>(kActiveAbility), kActiveAbilityPrologue, sizeof kActiveAbilityPrologue) != 0) {
        rfa::Log("Backpacks: the active ability's getter (0x%08lX) isn't the publicbeta's; the packs' bullet hit effects are off", kActiveAbility);
        return;
    }
    g_activeAbility = reinterpret_cast<ActiveAbilityFn>(rfa::HookFunction(kActiveAbility, sizeof kActiveAbilityPrologue, reinterpret_cast<void*>(&ActiveAbility)));
    if (!g_activeAbility) rfa::Log("Backpacks: could not wrap the active ability's getter; the packs' bullet hit effects are off");
    else rfa::LogDetail("Backpacks: the packs' bullet hit effects (Firepower's, Thrust's and Rhino's) go through the game's own bullet hit effects");
}

// Guerrilla's attempt_ripple's timer (ripple_timer_ms; Concussion has none: every hit).
ULONGLONG g_rippleNext = 0;

}  // namespace

void backpacks::Init() {
    ReadPacks();
    BulletEffects();
    // [GuerrillaKeys] Backpack= (empty: no key), else the older [GuerrillaGear] BackpackKey=, else B. (Asked of
    // guerrilla_hud once the game runs, FollowHudKey: the same setting, and its F7 window's changes.)
    const wchar_t* const kUnset = L"\x01";
    const std::wstring set = rfa::IniString(L"GuerrillaKeys", L"Backpack", kUnset);
    const bool keys = set != kUnset;
    const std::wstring key = keys ? set : rfa::IniString(L"GuerrillaGear", L"BackpackKey", L"B");
    g_key = KeyNamed(key);
    const char* const where = keys ? "[GuerrillaKeys] Backpack" : "[GuerrillaGear] BackpackKey";
    if (g_packs.empty())
        rfa::Log("Backpacks: no mods\\guerrilla_gear\\backpacks.txt (build Guerrilla's weapons with GuerrillaBuilder.exe); none can be worn");
    else if (g_key)
        rfa::LogDetail("Backpacks: %zu of Guerrilla's; the jetpack climbs with jump held in the air, the others switch on with %s (%s)", g_packs.size(),
                 KeyName(g_key).c_str(), where);
    else
        rfa::LogDetail("Backpacks: %zu of Guerrilla's; the jetpack climbs with jump held in the air, the others have no key (%s is \"%ls\")",
                 g_packs.size(), where, key.c_str());
}

void backpacks::Step(BYTE* player) {
    const float seconds = StepSeconds();
    const int worn = upgrades::WornBackpack();
    Dress(player, worn);
    if (worn != g_wearing) {  // a pack put on starts full, as Guerrilla's picked up
        g_wearing = worn;
        g_fuel = 1.0f;
        if (g_on) PackOff(player);
        g_on = false;
        g_rampUntil = 0;
        auto found = g_packs.find(worn);
        if (worn >= 0) rfa::LogDetail("Backpacks: wearing %s", found != g_packs.end() ? found->second.name.c_str() : "an unknown pack");
        if (found != g_packs.end() && g_wearingSeen) PlayPackEffect(player, found->second, kEquipEffect, false);  // Guerrilla's pickup_effects
        g_wearingSeen = true;  // (not for the pack he wears as the game starts)
    }
    auto found = g_packs.find(worn);
    if (found == g_packs.end()) {
        if (KeyPressed()) LogKey(worn < 0 ? "no backpack is worn (Wear one in F7 > Guerrilla > Upgrades)" : "the worn pack isn't in backpacks.txt");
        HoldParameters(player, nullptr, 0);
        return;
    }
    if (worn == 0) {
        if (KeyPressed()) LogKey("the jetpack has no key: hold jump in the air");
        Jetpack(player, found->second, seconds);
    } else {
        Activated(player, found->second, seconds);
    }
    // The upgrade parameters the pack scales while it is on (put back the moment it isn't).
    Wish wishes[16];
    const int count = g_on && worn != 0 ? Wishes(found->second, wishes) : 0;
    HoldParameters(player, wishes, count);
}

float backpacks::Fuel() { return g_fuel; }
bool backpacks::On() { return g_on; }

// Guerrilla's human_combat_unified_melee_hit (RM 0xAD18C9): a hit by someone whose pack's abilities have a ripple calls
// its attempt_ripple, which ripples from where the hit landed (here: the one hit's place) unless its ripple_timer_ms
// runs. Only the player wears a pack, and only while on are its active abilities its (a pack's passive ones have no
// ripple); Guerrilla's Concussion goes off as it goes on, so in Guerrilla's single player this never ripples.
void backpacks::MeleeHit(BYTE* attacker, BYTE* target) {
    if (!attacker || !target || !g_on || attacker != LocalPlayerHuman()) return;
    auto found = g_packs.find(g_wearing);
    if (found == g_packs.end() || !HasRipple(found->second)) return;
    const ULONGLONG now = GetTickCount64();
    if (now < g_rippleNext) return;
    g_rippleNext = now + static_cast<ULONGLONG>(found->second.Number("activatable.active_abilities.ripple.ripple_timer_ms", 0.0f));
    Ripple(attacker, found->second, PlaceOf(target));
}

// For other mods (guerrilla_hud's backpack icon and fuel meter, as Guerrilla's hud_ui_multiplayer_process_backpack_
// data_item shows them): the worn pack's Unique_ID (0 the jetpack, 1 Thrust ... 9 Tremor; -1 none), and, where asked,
// its fuel (0 to 1), whether it is on (the jetpack: pushing) and its HUD icon (mp_backpacks.xtbl icon_name, e.g.
// ui_hud_backpack_icon_thrust; null for none). Read on any thread: the values are the game thread's last.
int GuerrillaGear_Backpack(float* fuel, int* on, const char** icon) {
    if (!guerrilla_gear::Running()) {
        if (fuel) *fuel = 0.0f;
        if (on) *on = 0;
        if (icon) *icon = nullptr;
        return -1;
    }
    const int worn = g_wearing;
    if (fuel) *fuel = g_fuel;
    if (on) *on = g_on ? 1 : 0;
    if (icon) {
        *icon = nullptr;
        auto found = g_packs.find(worn);
        if (found != g_packs.end())
            if (auto field = found->second.values.find("icon_name"); field != found->second.values.end()) *icon = field->second.c_str();
    }
    return worn;
}

// For other mods (guerrilla_world's PackFuel, for the backpacks' fuel gauge shader, gw_fuelgauge0c): Guerrilla's
// mp_fuels as multi_object_backpack::update_render (RM 0x97D210) sets it on the pack: its fuel; the fuel it needs to
// go on (uses_fuel's minimum_fuel_to_activate, else 0); 1 while it is on (the jetpack: while it pushes); 0. All 0
// with none worn. Returns 1 with one worn.
int GuerrillaGear_PackGauge(float* out) {
    if (!out) return 0;
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    if (!guerrilla_gear::Running()) return 0;
    auto found = g_packs.find(g_wearing);
    if (found == g_packs.end()) return 0;
    out[0] = g_fuel;
    out[1] = found->second.Number("activatable.uses_fuel.minimum_fuel_to_activate", 0.0f);
    out[2] = g_on ? 1.0f : 0.0f;
    return 1;
}

// For other mods (the alert session's EDF sight, as Guerrilla's human_combat_get_target skips a target that
// human_is_player_in_stealth): whether the player is wholly stealthed by the Stealth pack (multi_object_backpack::
// is_fully_stealthed): Stealth on, faded in, no re-stealth waiting after an attack.
int GuerrillaGear_PlayerStealthed() { return guerrilla_gear::Running() && g_fullyStealthed ? 1 : 0; }

// Guerrilla's get_max_rounds × the worn pack's abilities' ammo_factor: multi_object_backpack::get_backpack_abilities
// gives its active_abilities while it is on, else its passive_abilities (parse_backpacks_table reads those two
// blocks; the fields at the top of an entry, such as the Commando's ammo_factor 1.5, are read by nothing). No pack
// of Guerrilla's has an ammo_factor in either block, so this is 1 for all ten, as in Guerrilla.
float backpacks::AmmoFactor() {
    auto found = g_packs.find(upgrades::WornBackpack());
    if (found == g_packs.end()) return 1.0f;
    const float factor = found->second.Number(g_on ? "activatable.active_abilities.ammo_factor" : "passive_abilities.ammo_factor", 1.0f);
    return factor > 0.0f ? factor : 1.0f;
}
