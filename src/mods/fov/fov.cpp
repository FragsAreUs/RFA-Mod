// Field of view for Red Faction: Armageddon (publicbeta DX11 exe).
//
// Every frame the active camera mode hands its camera to the game's "finalise FOV" function, which
// copies the camera's FOV (45 degrees vertical unless something like aiming changes it) into the
// camera manager as the FOV the frame is drawn with. On 4:3 displays it multiplies by 1.125 first
// (the console's Camera_43_fov_modifier). This mod replaces that function with one that multiplies
// by FOV / 45 instead. Cutscene cameras don't use it.
//
// The vehicle and exo-suit camera mode doesn't use that function: it copies its camera's FOV into
// the camera manager itself. This mod wraps that camera mode and scales the FOV right after it.
//
// The result is capped at kMaxFov. Nothing in the engine limits it, and at 180 degrees or more the
// picture turns inside out. Cameras that start wider than 45 (some vehicles) would get there first.
#define RFA_MOD_NAME "fov"
#include "rfa_mod.h"
#include "rfa_game.h"
#include <math.h>
#include <stddef.h>

namespace {

constexpr float kDefaultFov = 45.0f;  // the engine's default camera FOV
constexpr int kMinFov = 30;
constexpr int kMaxFov = 170;          // also the cap for every camera the mod scales

// The parts of the game's cameras this mod uses.
struct Camera {
    char unknown[0x20];
    float fov;  // vertical degrees
};

struct CameraManager {
    int mode;  // the game leaves the FOV alone while this is 1
    char unknown1[0xBC];
    float fov;  // what the frame is drawn with
    char unknown2[0x7C];
    Camera* activeCamera;
};
static_assert(offsetof(CameraManager, fov) == 0xC0, "camera manager layout");
static_assert(offsetof(CameraManager, activeCamera) == 0x140, "camera manager layout");

// Addresses in the publicbeta exe.
auto& g_cameras = *reinterpret_cast<CameraManager*>(0x11B9C40);
auto& g_fovFrozen = *reinterpret_cast<BYTE*>(0x11B9AD5);  // the game leaves the FOV alone while set
constexpr DWORD kFinaliseFov = 0x4D32D0;        // thiscall: the camera
constexpr DWORD kVehicleCameraUpdate = 0x4C5370;  // cdecl: frame time, flag
constexpr size_t kVehicleCameraUpdatePrologue = 6;  // push ebp / mov ebp,esp / sub esp,74h

float g_scale = 1.0f;

float Scaled(float fov) {
    const float scaled = fov * g_scale;
    return scaled < kMaxFov ? scaled : static_cast<float>(kMaxFov);
}

// Replaces the game's "finalise FOV" function. __fastcall receives the camera the same way the
// game passes it (in ecx).
void __fastcall FinaliseFov(Camera* camera) {
    if (g_cameras.mode == 1 || g_fovFrozen != 0) return;
    g_cameras.fov = Scaled(camera->fov);
}

using VehicleCameraUpdateFn = void(__cdecl*)(float, bool);
VehicleCameraUpdateFn g_vehicleCameraUpdate;

// Runs the vehicle and exo-suit camera mode, then scales the FOV it copied over. If this frame
// didn't copy it, the manager's FOV doesn't equal the camera's and is left alone, so it is never
// scaled twice.
void __cdecl VehicleCameraUpdate(float frameTime, bool flag) {
    g_vehicleCameraUpdate(frameTime, flag);
    Camera* camera = g_cameras.activeCamera;
    if (camera && g_cameras.fov == camera->fov) g_cameras.fov = Scaled(camera->fov);
}

// Horizontal FOV that a vertical FOV gives on the primary monitor, for the log.
float HorizontalFov(float vertical) {
    float aspect = static_cast<float>(GetSystemMetrics(SM_CXSCREEN)) / GetSystemMetrics(SM_CYSCREEN);
    const float toRad = 3.14159265f / 180.0f;
    return 2.0f * atanf(tanf(vertical * toRad / 2.0f) * aspect) / toRad;
}

int ReadVerticalFov() {
    int vertical = rfa::IniInt(L"FOV", L"VerticalFov", 60);
    if (vertical < kMinFov || vertical > kMaxFov) {
        rfa::Log("VerticalFov=%d is outside %d-%d; using the game's 45", vertical, kMinFov, kMaxFov);
        vertical = 45;
    }
    return vertical;
}

void SetFov(int vertical) {
    g_scale = vertical / kDefaultFov;
    rfa::Log("FOV: %d vertical (about %.0f horizontal on this screen), %.3fx the game's 45", vertical,
             HorizontalFov(static_cast<float>(vertical)), g_scale);
}

}  // namespace

// The Gameplay menu changed a setting: apply a new FOV straight away.
extern "C" __declspec(dllexport) void __cdecl RFAMod_SettingChanged(const char* section, const char*) {
    if (_stricmp(section, "FOV") == 0) SetFov(ReadVerticalFov());
}

extern "C" __declspec(dllexport) void __cdecl RFAMod_Init() {
    if (!rfa::game::IsSupportedBuild()) {
        rfa::Log("Not the publicbeta build; the FOV is left alone");
        return;
    }
    SetFov(ReadVerticalFov());
    if (rfa::ReplaceFunction(kFinaliseFov, reinterpret_cast<void*>(&FinaliseFov)))
        rfa::Log("On-foot FOV set (capped at %d)", kMaxFov);
    if (rfa::IniInt(L"FOV", L"Vehicles", 1)) {
        g_vehicleCameraUpdate = reinterpret_cast<VehicleCameraUpdateFn>(rfa::HookFunction(
            kVehicleCameraUpdate, kVehicleCameraUpdatePrologue, reinterpret_cast<void*>(&VehicleCameraUpdate)));
        if (g_vehicleCameraUpdate) rfa::Log("Vehicle and exo-suit FOV set (capped at %d)", kMaxFov);
    }
}
