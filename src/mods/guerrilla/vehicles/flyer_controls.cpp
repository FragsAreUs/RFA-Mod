// Guerrilla's flyers flown the way Armageddon's are: its controls and its camera, picked in the F7 menus (Guerrilla >
// Armageddon's flyer controls) or with [GuerrillaVehicles] ArmageddonFlyerControls=1. A part of guerrilla_vehicles.dll.
//
// The player's flight is Armageddon's own in every flyer (0x61DA50, the player's flyer control each frame), and it
// takes all of its feel from the flyer's vehicles.xtbl entry (vehicle +0x3DA8):
//   - FlyerParams' limits, +0x7A8 to +0x7FB (FlyerParams' reader 0x62F540): the top speeds, accelerations and
//     decelerations forward, sideways and up (+0x7A8 .. +0x7C8), the least of them at low speed (+0x7CC), the turn
//     and the tilt (speed, acceleration, deceleration, +0x7D0 .. +0x7E4; with the mouse +0x7E8, +0x7EC), the bank
//     (+0x7F0 angle, +0x7F4 acceleration) and the least turning at low speed (+0x7F8);
//   - Steering's AirSteerPitchMaxAngle (+0x268, radians), which eases the pitch input as the nose nears it;
//   - its first camera (Cameras, 0x62E1C0: three of 0xA4 bytes from +0x294, the count at +0x480): the follow
//     distance and height, the look-at offsets (where the camera aims, and from where on getting in), the vertical
//     limits, the zoom (its magnification, and +0x2EC how much the zoom slows the stick and mouse), the steering
//     angle and speed of the camera, the recentring delays.
// GuerrillaBuilder.exe gives Guerrilla's flyers Guerrilla's values there (the Raven: 7.35 m/s² every way, turning and
// tilting at 2 degrees/s², and a camera without look-at offsets), so they fly and are watched as in Guerrilla. With
// this on, each of Guerrilla's flyers (mods\guerrilla_vehicles\flyers.txt) takes those three from an Armageddon
// flyer's entry ([GuerrillaVehicles] FlyerControlsLike=, Mar_Flyer_2: the NitroX), and gets its own back when it is
// turned off. The camera keeps the flyer's own place (see Compose), raised [GuerrillaVehicles] FlyerCameraRaise= metres
// (1.5; F7 Guerrilla > Flyer camera height). Nothing else changes: NPCs fly Guerrilla's flyers by Guerrilla's model (flyers.cpp), which keeps its
// own values, and a flyer's speed limits for the NPCs aren't these.
//
// Changes are made on the game's thread, in the vehicles' presim (flyers.cpp), never from the menu's: the menu only
// asks. A table read again (reload_vehicles) makes the entries anew; the flyer the player flies is put back to the
// chosen controls each frame, the others when the choice is made again.
#define RFA_MOD_NAME "guerrilla_vehicles"
#include "rfa_mod.h"
#include "rfa_menu.h"
#include "game.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace gv {

namespace {

struct Block {
    DWORD offset, size;
};
constexpr Block kBlocks[] = {
    {0x268, 4},     // AirSteerPitchMaxAngle
    {0x294, 0xA4},  // the first camera
    {0x7A8, 0x54},  // FlyerParams' limits for the player's flight
};
constexpr DWORD kCameraIsTurret = 0x294 + 0x4C;  // the first camera's Turret_Camera (+0x2E0)
constexpr DWORD kCameraCount = 0x480;

std::atomic<bool> g_armageddon{false};  // what the player chose
std::atomic<bool> g_pending{false};     // a choice not yet made on the game's thread
std::atomic<bool> g_logNext{true};      // say it in the log (not while the height is dragged)
std::atomic<float> g_raise{0.0f};       // metres the camera is raised (look-at and follow height)
bool g_menuValue = false;               // the menu's copies (the menu's thread)
float g_menuRaise = 0.0f;
bool g_started = false;
std::string g_like = "Mar_Flyer_2";

// Each of Guerrilla's flyers' own values, and what was last written over them (the game's thread).
struct Kept {
    std::vector<BYTE> own, written;
};
std::map<std::string, Kept> g_kept;

std::vector<BYTE> Read(const BYTE* info) {
    std::vector<BYTE> bytes;
    for (const Block& b : kBlocks) bytes.insert(bytes.end(), info + b.offset, info + b.offset + b.size);
    return bytes;
}

void Write(BYTE* info, const std::vector<BYTE>& bytes) {
    size_t at = 0;
    for (const Block& b : kBlocks) {
        std::memcpy(info + b.offset, bytes.data() + at, b.size);
        at += b.size;
    }
}

// Whether an entry's first camera is one to drive with (not a turret's), and it has one.
bool DrivingCamera(const BYTE* info) {
    return *reinterpret_cast<const int*>(info + kCameraCount) > 0 && info[kCameraIsTurret] == 0;
}

// The camera's place is the flyer's own: where it looks (Lookat_Offset, Lookat_Vehicle_Offset, Lookat_Enter_Offset),
// Lookfrom_Height, Enter_Distance, Follow_Distance and Follow_Height (+0x00 .. +0x33 of a camera), and the look-at
// and follow distance control points (+0x6C .. +0xA3), all measured on the vehicle: the NitroX's, on a flyer the
// Raven's size, put the camera at its console. How it moves is Armageddon's: its turning speeds, steering, zoom,
// recentring and vertical limits (+0x34 .. +0x6B). Then raised by the chosen height (the look-at's Y, +0x04, and
// Follow_Height, +0x30).
constexpr size_t kCameraAt = 4;  // the camera's place in Read's bytes (after AirSteerPitchMaxAngle)
constexpr size_t kPlaceEnd = 0x34, kPointsAt = 0x6C, kCameraSize = 0xA4;
constexpr size_t kLookatY = 0x04, kFollowHeight = 0x30;

std::vector<BYTE> Compose(const std::vector<BYTE>& own, const std::vector<BYTE>& theirs, float raise) {
    std::vector<BYTE> bytes = theirs;
    std::memcpy(bytes.data() + kCameraAt, own.data() + kCameraAt, kPlaceEnd);
    std::memcpy(bytes.data() + kCameraAt + kPointsAt, own.data() + kCameraAt + kPointsAt, kCameraSize - kPointsAt);
    for (size_t at : {kLookatY, kFollowHeight}) {
        float value;
        std::memcpy(&value, bytes.data() + kCameraAt + at, 4);
        value += raise;
        std::memcpy(bytes.data() + kCameraAt + at, &value, 4);
    }
    return bytes;
}

// One of Guerrilla's flyers' entries to the choice. True if it was changed.
bool Choose(BYTE* info, const std::string& name, bool armageddon) {
    BYTE* like = VehicleEntry(g_like);
    if (!info || !like || like == info || !DrivingCamera(like) || !DrivingCamera(info)) return false;
    const std::vector<BYTE> now = Read(info);
    auto kept = g_kept.find(name);
    if (armageddon) {
        if (kept == g_kept.end()) kept = g_kept.emplace(name, Kept{now, {}}).first;
        else if (now != kept->second.written) kept->second.own = now;  // the table was read again: its own anew
        const std::vector<BYTE> wanted = Compose(kept->second.own, Read(like), g_raise);
        if (now == wanted) return false;
        Write(info, wanted);
        kept->second.written = wanted;
        return true;
    }
    if (kept == g_kept.end()) return false;
    const bool ours = now == kept->second.written;  // else the table was read again: it has its own already
    if (ours) Write(info, kept->second.own);
    g_kept.erase(kept);
    return ours;
}

void ChooseAll(bool armageddon, bool log) {
    int changed = 0, flyers = 0;
    for (const std::string& name : GuerrillaFlyerNames()) {
        BYTE* info = VehicleEntry(name);
        if (!info) continue;
        ++flyers;
        if (Choose(info, name, armageddon)) ++changed;
    }
    if (!log) return;
    if (!VehicleEntry(g_like))
        rfa::Log("Flyer controls: no %s among the vehicles ([GuerrillaVehicles] FlyerControlsLike=); Guerrilla's flyers keep theirs", g_like.c_str());
    else if (armageddon)
        rfa::Log("Flyer controls: Guerrilla's flyers fly with Armageddon's controls and camera, %s's, the camera placed as each flyer's own and "
                 "raised %.1f m (%d of %d changed)", g_like.c_str(), g_raise.load(), changed, flyers);
    else
        rfa::Log("Flyer controls: Guerrilla's flyers fly with their own controls and camera (%d of %d changed)", changed, flyers);
}

void Save(bool armageddon) {
    WritePrivateProfileStringW(L"GuerrillaVehicles", L"ArmageddonFlyerControls", armageddon ? L"1" : L"0", rfa::IniPath().c_str());
}

}  // namespace

void StartFlyerControls() {
    const std::wstring like = rfa::IniString(L"GuerrillaVehicles", L"FlyerControlsLike", L"Mar_Flyer_2");
    g_like.clear();
    for (wchar_t c : like) g_like += static_cast<char>(c);  // the names are ASCII
    const bool armageddon = rfa::IniInt(L"GuerrillaVehicles", L"ArmageddonFlyerControls", 0) != 0;
    const float raise = static_cast<float>(_wtof(rfa::IniString(L"GuerrillaVehicles", L"FlyerCameraRaise", L"1.5").c_str()));
    g_raise = raise;
    g_menuRaise = raise;
    g_armageddon = armageddon;
    g_menuValue = armageddon;
    g_pending = armageddon;  // made once the vehicles are read and one is in the world
    g_started = true;
    rfa::Log(armageddon ? "Flyer controls: Guerrilla's flyers fly with Armageddon's controls and camera, %s's ([GuerrillaVehicles] ArmageddonFlyerControls=1; F7 Guerrilla)"
                        : "Flyer controls: Guerrilla's flyers fly with their own controls and camera (F7 Guerrilla > Armageddon's flyer controls; %s's)",
             g_like.c_str());
}

// The vehicles' presim (flyers.cpp), on the game's thread: a choice made in the menu, and the player's own flyer kept
// to it (its entry made anew when the table was read again).
void FlyerControlsTick(BYTE* vehicle, bool playerFlies) {
    if (g_pending.exchange(false)) ChooseAll(g_armageddon, g_logNext.exchange(false));
    if (!playerFlies || !g_armageddon || !vehicle) return;
    BYTE* info = *reinterpret_cast<BYTE**>(vehicle + 0x3DA8);
    const char* name = info ? *reinterpret_cast<const char* const*>(info) : nullptr;
    if (name && Choose(info, name, true)) RFA_LOG_FIRST(4, "Flyer controls: %s given Armageddon's controls again (its entry was made anew)", name);
}

void FlyerControlsMenu(const RFAMenu* menu) {
    if (!g_started || !rfa::menu::Use(menu) || menu->stage != RFA_MENU_BAR) return;
    if (ImGui::BeginMenu("Guerrilla")) {
        if (ImGui::MenuItem("Armageddon's flyer controls", nullptr, &g_menuValue)) {
            g_armageddon = g_menuValue;
            g_logNext = true;
            g_pending = true;  // made on the game's thread (FlyerControlsTick)
            Save(g_menuValue);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Guerrilla's flyers (the Raven, ...) fly and are followed by the camera as %s, Armageddon's, is.\n"
                              "Off: as Guerrilla's. Takes effect at once, also in the air.", g_like.c_str());
        if (g_menuValue) {
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::SliderFloat("Flyer camera height", &g_menuRaise, -2.0f, 8.0f, "%.1f m")) {
                g_raise = g_menuRaise;
                g_pending = true;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                wchar_t text[32];
                swprintf_s(text, L"%.1f", g_menuRaise);
                WritePrivateProfileStringW(L"GuerrillaVehicles", L"FlyerCameraRaise", text, rfa::IniPath().c_str());
                g_logNext = true;
                g_pending = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How far the camera is raised above each flyer's own place for it, while flying with Armageddon's controls.");
        }
        ImGui::EndMenu();
    }
}

}  // namespace gv

// rfa_guerrilla.dll's own RFAMod_Menu calls each part's menus; this is the vehicles part's.
namespace guerrilla_vehicles {
void Menu(const RFAMenu* menu) { gv::FlyerControlsMenu(menu); }
}  // namespace guerrilla_vehicles
