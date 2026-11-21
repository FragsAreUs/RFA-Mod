// Guerrilla's car bomb (activity_car_bomb_data, mode 15, a free activity) with its M.O.A.B.s ("Blast from the Past":
// object_bftp_node, the bomb nodes).
//
// Every third radio tag found shows its pair's M.O.A.B. on the maps (guerrilla_hud's radio tags: GuerrillaHud_BombShown).
// A shown M.O.A.B. that isn't taken is a world item, Guerrilla's bftp_bomb (create_bftp_bomb RM 0xA60FD0: its flags
// simulate, no_save, preplaced, shadows; its light wep_remote_charge_light_a looping on its "blinking_light" tag), made
// while the player is near it and taken away past that (object_bftp_node::stream 0xA611E0). Each frame
// (blast_from_the_past_process_car_bombs 0xAA4C60): the player in an automobile (not a flyer or a walker) with a
// M.O.A.B. within 5 m of his car touches it (blast_from_the_past_touch_item 0xA61350): nothing while a car bomb runs;
// a car armed already says HM_BFTP_CAR_ALREADY_ARMED; else the car is armed (vehicle_set_bomb_status, ARMED), the car
// bomb starts with it, the light and the item go, its map icon hides, and the tool tip Blast. On foot at it:
// HM_BFTP_GET_A_CAR ("acquire a vehicle to set M.O.A.B.").
// The car bomb (do_frame_active 0x76A220): the car gone, it ends and the M.O.A.B. is back at its place
// (blast_from_the_past_reset_last_bomb 0xA61570); the car destroyed, the bomb is spent (blast_from_the_past_bomb_destroyed
// 0xA15C50): Armageddon's vehicle blow-up still makes an armed car's explosion its Bomb_Explosion (vehicle_info +0x594,
// our vehicles.xtbl's rfg_vehicle_bomb: radius 10, crumble 15, exp_carbomb), as Guerrilla's. Killed (a mission, another
// activity), the car is disarmed and the M.O.A.B. is back. No timer and no reward of its own. The diversions panel shows
// M.O.A.B.: INSTALLED for a second, then ARMED, flashing while the player is out of the car (get_ui_display_info
// 0x757350: "f" in it, "t" out). The M.O.A.B. icons hide while a car bomb runs or the player sits in an armed car
// (the cartographer's, through GuerrillaCampaign_BombIconShown).
// Kept per save (the campaign's save, section "bftp"): each M.O.A.B. spent. Guerrilla also saves the armed car (its
// vehicle::serialize), which Armageddon's saves don't: a M.O.A.B. on a car that isn't blown up yet is back at its place
// after a load. Not ported: the handbook's entry of the pair, the sounds (MIS_BFTP_CARBOMB_ATTACH: the interface's
// banks are full), the statistics, offensive raid 01's scripted car bomb (with raids), entering an armed car
// starting the car bomb again (Armageddon keeps no armed car across a load).
//
// Armageddon's: a vehicle's bomb status +0x4D6C (0x60F240 cdecl: the vehicle, 0 none or 1 armed); an object's type
// +0x7A and subtype +0x7B (3 and 5: an automobile); a human's vehicle +0x1160; items made as props.cpp makes them
// (0x7CF440, 0x465320, 0x9195A0, 0x7CCF00, 0x611000, 0x694580; removed by 0x6D5450); effects (0x4EF370, 0x4EB460;
// stopped by 0x4D4360 → 0x4F7AA0); the item's tag by its vtable +0x2C.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "campaign_api.h"
#include "hud/hud_api.h"
#include "activity_base.h"
#include "messages.h"
#include "save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace campaign::activities {
namespace {

// ---- Armageddon's objects

void* const kWorld = reinterpret_cast<void*>(0x1870630);
constexpr DWORD kNone = 0xFFFFFFFF;
constexpr DWORD kPosition = 0x04, kHandle = 0x68, kObjectType = 0x7A, kObjectSubtype = 0x7B, kHumanVehicle = 0x1160, kBombStatus = 0x4D6C;
constexpr int kVehicleType = 3, kAutomobile = 5;
constexpr int kBombNone = 0, kBombArmed = 1;
auto const SetBombStatus = reinterpret_cast<void(__cdecl*)(BYTE* vehicle, int status)>(0x60F240);
auto const ItemInfoNamed = reinterpret_cast<BYTE*(__cdecl*)(const char* name)>(0x7CF440);
auto const InitFlags = reinterpret_cast<void(__thiscall*)(DWORD* flags)>(0x465320);
auto const InitWord = reinterpret_cast<void(__thiscall*)(DWORD* word)>(0x9195A0);
auto const MakeProps = reinterpret_cast<void(__thiscall*)(BYTE* props, BYTE* item, const float* position, const float* orientation,
                                                          DWORD* flags, DWORD* word, DWORD collide, float unknown, int none)>(0x7CCF00);
auto const CreateObject = reinterpret_cast<BYTE*(__thiscall*)(void* world, BYTE* props, DWORD owner)>(0x611000);
auto const FreeProps = reinterpret_cast<void(__thiscall*)(BYTE* props)>(0x694580);
auto const DestroyObject = reinterpret_cast<void(__thiscall*)(void* world, BYTE* object, bool now)>(0x6D5450);
const DWORD* const kCollisionSetting = reinterpret_cast<const DWORD*>(0x1975E94);
using TagNamedFn = int(__thiscall*)(BYTE* object, const char* name);
constexpr DWORD kTagNamed = 0x2C;
auto const EffectNumber = reinterpret_cast<int(__cdecl*)(const char* name)>(0x4EF370);
using EffectsPlayFn = int(__cdecl*)(int effect, const float* where, const float* turn, bool looping, DWORD host, int hostTag, DWORD source,
                                    bool onCamera, DWORD unused1, DWORD unused2);
auto const EffectsPlay = reinterpret_cast<EffectsPlayFn>(0x4EB460);
auto const EffectObject = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int handle)>(0x4D4360);
auto const StopEffect = reinterpret_cast<void(__fastcall*)(BYTE* effect, void* unused, int style)>(0x4F7AA0);
// create_bftp_bomb's item flags: simulate, no_save, preplaced, cast_shadows, cast_drop_shadows (and RM 0xFFFC6064).
constexpr DWORD kItemFlagsKept = 0xFFFC0000, kItemFlags = 0x6064;
const float kIdentity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
const char* const kBombItem = "bftp_bomb";
const char* const kBombLight = "rfg_wep_remote_charge_light_a";  // Guerrilla's, as the world build adds it
const char* const kLightTag = "blinking_light";

DWORD HandleOf(const BYTE* object) { return *reinterpret_cast<const DWORD*>(object + kHandle); }
Vector Position(const BYTE* object) { return *reinterpret_cast<const Vector*>(object + kPosition); }
float Squared(const Vector& a, const Vector& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}
bool IsDead(BYTE* object) {  // the fifth virtual function, as Guerrilla's vehicle::is_killed
    using IsDeadFn = bool(__thiscall*)(void* object);
    return reinterpret_cast<IsDeadFn>((*reinterpret_cast<void***>(object))[4])(object);
}

// The player's car: his vehicle, an automobile.
BYTE* PlayersCar() {
    const BYTE* player = static_cast<const BYTE*>(Player());
    if (!player) return nullptr;
    const DWORD handle = *reinterpret_cast<const DWORD*>(player + kHumanVehicle);
    BYTE* vehicle = handle != kNone ? static_cast<BYTE*>(ObjectFromHandle(handle)) : nullptr;
    if (!vehicle || vehicle[kObjectType] != kVehicleType || vehicle[kObjectSubtype] != kAutomobile) return nullptr;
    return vehicle;
}

// ---- The M.O.A.B.s (mods\guerrilla_hud\radio_tags.txt's bomb lines: the pair, the place)

constexpr float kTouchSquared = 25.0f;          // a car within 5 m (blast_from_the_past_process_car_bombs)
constexpr float kOnFootSquared = 1.5f * 1.5f;   // on foot at it (Guerrilla's item touch; I: as near as a tag)
constexpr float kSaidAgainSquared = 8.0f * 8.0f;  // a refusal said again once the player has gone this far and back
constexpr float kMakeSquared = 150.0f * 150.0f, kTakeAwaySquared = 200.0f * 200.0f;  // the item made near the player
constexpr int kTestPair = -2;                     // rfg_moab_here's, not one of Guerrilla's nodes
const char* const kSection = "bftp";

struct Bomb {
    int pair = 0;
    Vector at{};
    DWORD item = kNone;   // the world item, while made
    int light = -1;       // its light (an effect handle)
    bool inUse = false;   // on a car now (Guerrilla's collected, until the car bomb ends)
    bool said = false;    // a refusal said, until the player goes away
};
std::vector<Bomb> g_bombs;
bool g_bombsRead = false;
bool g_noItemSaid = false;

std::string Key(int pair) { return "bomb " + std::to_string(pair); }
bool Spent(int pair) { return pair != kTestPair && save::Int(kSection, Key(pair).c_str(), 0) != 0; }

void ReadBombs() {
    g_bombsRead = true;
    FILE* file = nullptr;
    if (_wfopen_s(&file, (rfa::RootDir() + L"\\mods\\guerrilla_hud\\radio_tags.txt").c_str(), L"r") != 0 || !file) {
        rfa::Log("Car bomb: no mods\\guerrilla_hud\\radio_tags.txt (editor\\GuerrillaBuilder.exe build hud makes it): no M.O.A.B.s");
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "bomb|", 5) != 0) continue;
        Bomb bomb;
        if (sscanf_s(line + 5, "%d|%f|%f|%f", &bomb.pair, &bomb.at.x, &bomb.at.y, &bomb.at.z) == 4) g_bombs.push_back(bomb);
    }
    fclose(file);
    rfa::LogDetail("Car bomb: %u M.O.A.B. places", static_cast<unsigned>(g_bombs.size()));
}

// create_bftp_bomb: the item at its place, its light on its tag.
void MakeItem(Bomb& bomb) {
    BYTE* info = ItemInfoNamed(kBombItem);
    if (!info) info = ItemInfoNamed((std::string("rfg_") + kBombItem).c_str());  // as the weapons build names one Armageddon has too
    if (!info) {
        if (!g_noItemSaid) {
            g_noItemSaid = true;
            rfa::Log("Car bomb: the item %s isn't built (editor\\GuerrillaBuilder.exe build weapons makes it); no M.O.A.B. shows", kBombItem);
        }
        return;
    }
    DWORD flags = 0, word = 0;
    InitFlags(&flags);
    flags = (flags & kItemFlagsKept) | kItemFlags;
    InitWord(&word);
    alignas(16) BYTE props[64] = {};
    MakeProps(props, info, &bomb.at.x, kIdentity, &flags, &word, *kCollisionSetting, -1.0f, -1);
    BYTE* object = CreateObject(kWorld, props, kNone);
    FreeProps(props);
    if (!object) {
        RFA_LOG_FIRST(8, "Car bomb: the M.O.A.B. %d couldn't be made (the world's items are all in use?)", bomb.pair);
        return;
    }
    bomb.item = HandleOf(object);
    const TagNamedFn tagNamed = *reinterpret_cast<const TagNamedFn*>(*reinterpret_cast<BYTE* const*>(object) + kTagNamed);
    const int tag = tagNamed(object, kLightTag);
    const int number = EffectNumber(kBombLight);
    bomb.light = number >= 0 ? EffectsPlay(number, &bomb.at.x, kIdentity, true, bomb.item, tag, bomb.item, false, 0, 0) : -1;
    rfa::LogDetail("Car bomb: the M.O.A.B. %d made at (%.0f, %.0f, %.0f)%s", bomb.pair, bomb.at.x, bomb.at.y, bomb.at.z, bomb.light >= 0 ? ", its light on" : "");
}

void TakeItemAway(Bomb& bomb) {
    if (bomb.light >= 0)
        if (BYTE* effect = EffectObject(kWorld, nullptr, bomb.light)) StopEffect(effect, nullptr, 1);
    bomb.light = -1;
    if (bomb.item != kNone)
        if (BYTE* object = static_cast<BYTE*>(ObjectFromHandle(bomb.item))) DestroyObject(kWorld, object, true);
    bomb.item = kNone;
}

// Whether a M.O.A.B. may lie at its place now: shown (its radio tags found; the test's always), not spent, not on a car.
bool Lies(const Bomb& bomb) { return !bomb.inUse && !Spent(bomb.pair) && (bomb.pair == kTestPair || GuerrillaHud_BombShown(bomb.pair)); }

Bomb* BombOfPair(int pair) {
    for (Bomb& bomb : g_bombs)
        if (bomb.pair == pair) return &bomb;
    return nullptr;
}

// ---- The car bomb

class CarBombActivity : public Activity {
public:
    CarBombActivity() : Activity(CarBomb, false, true) {}

    const char* Title() const override { return "ACT_BOMB"; }

    // get_ui_display_info: "f" while the player is in the bomb's car (n 500000), else "t" (n 1000: ARMED, flashing).
    bool UiText(std::string& text, int& number) const override {
        BYTE* car = PlayersCar();
        const bool inIt = car && HandleOf(car) == vehicle_;
        text = inIt ? "f" : "t";
        number = inIt ? 500000 : 1000;
        return true;
    }

    // start_car_bomb 0x7ACF00: the armed car is the bomb's.
    bool StartBomb(BYTE* car, int pair) {
        if (!car || *reinterpret_cast<const int*>(car + kBombStatus) != kBombArmed || !StartFree()) return false;
        vehicle_ = HandleOf(car);
        pair_ = pair;
        return true;
    }

    DWORD Vehicle() const { return IsActive() ? vehicle_ : kNone; }

    void LevelInit() override {
        Activity::LevelInit();
        vehicle_ = kNone;
        pair_ = -1;
    }

protected:
    void FrameActive(float) override {
        BYTE* car = static_cast<BYTE*>(ObjectFromHandle(vehicle_));
        if (!car || car[kObjectType] != kVehicleType) {
            rfa::LogDetail("Car bomb: the car is gone; the M.O.A.B. is back at its place");
            End();
            return;
        }
        if (IsDead(car)) {
            // blast_from_the_past_bomb_destroyed: the bomb spent (the car's explosion was its Bomb_Explosion).
            rfa::LogDetail("Car bomb: the car blew up with the M.O.A.B.");
            if (pair_ != kTestPair && pair_ >= 0) save::SetInt(kSection, Key(pair_).c_str(), 1);
            if (Bomb* bomb = BombOfPair(pair_)) bomb->inUse = false;
            pair_ = -1;
            End();
        }
    }

    // end 0x796A80: blast_from_the_past_reset_last_bomb (a bomb not spent is back at its place), end_common.
    void End() override {
        if (!IsActive()) return;
        PutBack();
        Activity::End();
    }

    // kill 0x796AB0: the car disarmed, the M.O.A.B. back.
    void Kill() override {
        if (IsActive())
            if (BYTE* car = static_cast<BYTE*>(ObjectFromHandle(vehicle_)))
                if (*reinterpret_cast<const int*>(car + kBombStatus) == kBombArmed) {
                    SetBombStatus(car, kBombNone);
                    rfa::LogDetail("Car bomb: stopped; the car disarmed and the M.O.A.B. back at its place");
                }
        PutBack();
        Activity::End();
    }

private:
    void PutBack() {
        if (Bomb* bomb = BombOfPair(pair_)) bomb->inUse = false;
        pair_ = -1;
    }

    DWORD vehicle_ = kNone;
    int pair_ = -1;
};

CarBombActivity* g_carBomb = nullptr;

// blast_from_the_past_touch_item: the player's car on a M.O.A.B.
void Touch(Bomb& bomb, BYTE* car) {
    // Refused while a car bomb or Guerrilla Express (Transporter) runs (blast_from_the_past_touch_item RM 0xA6137A).
    const Activity* delivery = Get(Delivery);
    if (g_carBomb->IsActive() || (delivery && delivery->IsActive())) return;
    if (*reinterpret_cast<const int*>(car + kBombStatus) != kBombNone) {
        if (!bomb.said) messages::Primary("HM_BFTP_CAR_ALREADY_ARMED");
        bomb.said = true;
        return;
    }
    SetBombStatus(car, kBombArmed);
    if (!g_carBomb->StartBomb(car, bomb.pair)) {
        SetBombStatus(car, kBombNone);
        RFA_LOG_FIRST(8, "Car bomb: couldn't start (disabled?); the car isn't armed");
        return;
    }
    bomb.inUse = true;
    TakeItemAway(bomb);
    messages::Tip("Blast", 0);
    rfa::LogDetail("Car bomb: the M.O.A.B. %d set on the car %08X", bomb.pair, HandleOf(car));
}

// ---- The console: a M.O.A.B. to try (rfg_moab_here: one 8 m in front of the player, not one of Guerrilla's)

using RegisterCommandFn = void*(__fastcall*)(void* command, void* unused, const char* name, const char* description, int type, void (*handler)());
auto const RegisterCommand = reinterpret_cast<RegisterCommandFn>(0xC7BD70);
auto& g_commandRunning = *reinterpret_cast<int*>(0x28FF1C0);
using ConsolePrintFn = void(__cdecl*)(const char* format, ...);
auto const ConsolePrint = reinterpret_cast<ConsolePrintFn>(0xC7C440);
BYTE g_hereCommand[32];
constexpr DWORD kOrient = 0x10;

void HereCommand() {
    if (!g_commandRunning) return;
    const BYTE* player = static_cast<const BYTE*>(Player());
    if (!player || !Playing()) {
        ConsolePrint("rfg_moab_here: only in Guerrilla's world\n");
        return;
    }
    Bomb* test = BombOfPair(kTestPair);
    if (!test) {
        g_bombs.push_back({});
        test = &g_bombs.back();
        test->pair = kTestPair;
    }
    if (test->inUse) {
        ConsolePrint("rfg_moab_here: the test M.O.A.B. is on a car now\n");
        return;
    }
    TakeItemAway(*test);
    const float* orient = reinterpret_cast<const float*>(player + kOrient);  // its rows: right, up, forward
    const Vector at = Position(player);
    test->at = {at.x + orient[6] * 8.0f, at.y, at.z + orient[8] * 8.0f};
    ConsolePrint("rfg_moab_here: a M.O.A.B. 8 m in front of you: drive a car onto it\n");
}

}  // namespace

Activity* MakeCarBomb() {
    static CarBombActivity carBomb;
    g_carBomb = &carBomb;
    return &carBomb;
}

void CarBombEnter() {
    static bool commandAdded = false;
    if (!commandAdded) {
        commandAdded = true;
        RegisterCommand(g_hereCommand, nullptr, "rfg_moab_here", "Puts a M.O.A.B. (Guerrilla's car bomb) 8 m in front of you, to try the car bomb", 3,
                        &HereCommand);
    }
    if (!g_bombsRead) ReadBombs();
    for (auto it = g_bombs.begin(); it != g_bombs.end();) {
        if (it->pair == kTestPair) {
            it = g_bombs.erase(it);
            continue;
        }
        it->item = kNone;  // the world is new: its items are gone with it
        it->light = -1;
        it->inUse = false;
        it->said = false;
        ++it;
    }
}

void CarBombFrame() {
    const BYTE* player = static_cast<const BYTE*>(Player());
    if (!player || !g_carBomb) return;
    const Vector me = Position(player);
    BYTE* car = PlayersCar();
    const bool onFoot = *reinterpret_cast<const DWORD*>(player + kHumanVehicle) == kNone;
    for (Bomb& bomb : g_bombs) {
        const float away = Squared(bomb.at, me);
        // object_bftp_node::stream: made near the player while it lies there, taken away otherwise.
        if (bomb.item != kNone && (!Lies(bomb) || away > kTakeAwaySquared || !ObjectFromHandle(bomb.item))) TakeItemAway(bomb);
        if (bomb.item == kNone && Lies(bomb) && away < kMakeSquared) MakeItem(bomb);
        if (bomb.item == kNone) continue;
        if (bomb.said && away > kSaidAgainSquared) bomb.said = false;
        if (car) {
            if (Squared(bomb.at, Position(car)) <= kTouchSquared) Touch(bomb, car);
        } else if (onFoot && away <= kOnFootSquared && !bomb.said) {
            messages::Primary("HM_BFTP_GET_A_CAR");
            bomb.said = true;
        }
    }
}

}  // namespace campaign::activities

// The world map's and minimap's M.O.A.B. icon of a pair (the cartographer's MAP_ELEMENT_CARBOMB): not once the bomb is
// on a car or spent, nor while a car bomb runs or the player sits in an armed car.
int GuerrillaCampaign_BombIconShown(int pair) {
    using namespace campaign::activities;
    if (!guerrilla_campaign::Running() || !campaign::Playing()) return 1;
    if (g_carBomb && g_carBomb->IsActive()) return 0;
    if (const Activity* delivery = Get(Delivery); delivery && delivery->IsActive()) return 0;  // the cartographer's, RM 0x877D28
    if (const BYTE* car = PlayersCar(); car && *reinterpret_cast<const int*>(car + kBombStatus) != kBombNone) return 0;
    if (Spent(pair)) return 0;
    if (const Bomb* bomb = BombOfPair(pair); bomb && bomb->inUse) return 0;
    return 1;
}
