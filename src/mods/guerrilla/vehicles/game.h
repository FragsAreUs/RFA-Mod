// The game's functions and objects the guerrilla_vehicles parts that drive NPCs share (traffic.cpp,
// crews.cpp, convoys.cpp, edf.cpp), and what GuerrillaBuilder.exe wrote for them (traffic.txt, crews.txt).
// Every address is Armageddon's (the publicbeta build the mods target).
#pragma once
#include <windows.h>
#include <cmath>
#include <string>
#include <vector>

namespace gv {

// ---- Objects
// An object: +0x04 its position, +0x10 its orientation (right, up, forward), +0x68 its handle. A human:
// +0x1160 the vehicle it's in (a handle; -1 on foot), +0x1700 who manages it (1 ambient), its ai_system at
// +0x174C (+0x651 bit 2: yield_next_frame, the planner, perception and firing skip it). A vehicle: its AI
// at +0x3E00 (its state first: 1 pathfind; the ambient wait's blocker +0x3F18 and timer +0x3F1C, stuck
// timers +0x3EE0 and +0x3EF8), its flags at +0x4FB0 (Guerrilla's vehicle_flags: bit 10 a wreck, bit 30 on fire).
constexpr DWORD kPosition = 0x04, kOrientation = 0x10, kHandle = 0x68, kInVehicle = 0x1160, kManagement = 0x1700, kNpcAi = 0x174C,
                kYield = 0x651, kVehicleAi = 0x3E00, kStuck1 = 0x3EE0, kStuck2 = 0x3EF8, kWaitBlocker = 0x3F18, kWaitTimer = 0x3F1C,
                kVehicleFlags = 0x4FB0;
constexpr int kAmbient = 1, kAutomobile = 5, kFlyer = 6;
constexpr DWORD kNone = 0xFFFFFFFF;

inline void* const kWorld = reinterpret_cast<void*>(0x1870630);
inline const char* WorldName() { return reinterpret_cast<const char*>(0x1870630 + 0x29EA); }
inline auto const Player = reinterpret_cast<BYTE*(__cdecl*)()>(0x8B92D0);
// Any object by its handle (world::object_from_handle 0x6CB9E0: null for none or one being destroyed). Not 0x424350,
// the typed lookup for one kind only (type 2, subtype 1: the rfg_movers, the buildings, as 0x4776E0 lists them),
// which gives null for a person or a vehicle.
inline auto const ObjectByHandle = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, DWORD handle)>(0x6CB9E0);
inline auto const VehicleByHandle = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, DWORD handle)>(0x42AF10);
inline auto const HumanByHandle = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, DWORD handle)>(0x436120);
inline auto const RemoveObject = reinterpret_cast<int(__fastcall*)(void* world, void* unused, DWORD handle, int flag)>(0x6D56A0);

// ---- Roads and driving
inline auto const ClosestRoad = reinterpret_cast<BYTE*(__cdecl*)(const float* where, bool special, float within, bool unknown, bool gpsOnly)>(0x7F6E10);
inline auto const NextRoadPlace = reinterpret_cast<bool(__cdecl*)(BYTE* vehicle, BYTE* road, float* out, const float* from)>(0x4B2BD0);
// ai_vehicle_travel_to_a_point_do_frame: 0 failed, 1 there, 2 on its way.
inline auto const TravelTo = reinterpret_cast<int(__cdecl*)(BYTE* npc, BYTE* vehicle, const float* target, DWORD cellLow, DWORD cellHigh, int roads,
                                                            float stopShort, int noEndBraking, int lightCells, int smashCells, int smoothing)>(0x4B18E0);
inline auto const SetVehicleAiState = reinterpret_cast<void(__cdecl*)(BYTE* vehicleAi, int state)>(0x703D20);
inline auto const OverrideSpeed = reinterpret_cast<void(__cdecl*)(BYTE* vehicle, float speed)>(0x4B1760);  // 0 clears it
inline auto const StopCar = reinterpret_cast<void(__cdecl*)(BYTE* vehicle)>(0x610820);                    // vehicle_set_controls_when_abandoned

// ---- Making vehicles and people
inline auto const MakeSpawnParameters = reinterpret_cast<void(__fastcall*)(BYTE* parameters, void* unused)>(0x624030);
inline auto const SpawnVehicle = reinterpret_cast<int(__cdecl*)(BYTE* parameters)>(0x761790);  // 0 made, 1 not yet, 2 refused
inline auto const VehicleLoaded = reinterpret_cast<bool(__cdecl*)(BYTE* entry)>(0x762290);
inline auto const HumanInfo = reinterpret_cast<BYTE*(__cdecl*)(const char* name)>(0x88D170);
inline auto const MakeHuman = reinterpret_cast<BYTE*(__cdecl*)(BYTE* info, const float* where, const float* orientation, void* variant)>(0x762D50);
inline auto const RequestEnter = reinterpret_cast<bool(__cdecl*)(BYTE* human, BYTE* vehicle, int* enter)>(0x656410);
inline auto const RequestExit = reinterpret_cast<bool(__cdecl*)(BYTE* human, BYTE* exit)>(0x656A20);  // vehicle_interaction_request_exit
inline auto const Seats = reinterpret_cast<int(__cdecl*)(BYTE* vehicle)>(0x6036A0);
inline auto const SeatOccupant = reinterpret_cast<BYTE*(__cdecl*)(BYTE* vehicle, int seat)>(0x603AE0);
inline auto const Driving = reinterpret_cast<bool(__cdecl*)(BYTE* human)>(0x8695D0);
inline auto const StartActiveDespawn = reinterpret_cast<void(__fastcall*)(BYTE* npc, void* unused, int type, BYTE* from)>(0x8AAEC0);  // run, then vanish
constexpr DWORD kVehicleList = 0x148BD08, kVehicleEntrySize = 0x880;

// ---- What vehicles and people are like
inline auto const VehicleSubtype = reinterpret_cast<int(__fastcall*)(BYTE* vehicle, void* unused)>(0x423530);  // 5 automobile, 6 flyer, 8 walker
inline auto const OnFire = reinterpret_cast<bool(__cdecl*)(BYTE* vehicle)>(0x6054E0);
inline auto const HitPoints = reinterpret_cast<int(__cdecl*)(BYTE* vehicle)>(0x603920);      // vehicle +0x4D18
inline auto const MaxHitPoints = reinterpret_cast<int(__cdecl*)(BYTE* vehicle)>(0x603930);   // its info's
inline auto const FleeToSafetyBelief = reinterpret_cast<BYTE*(__cdecl*)(BYTE* npc)>(0x483C90);
inline auto const EnemyFleeBelief = reinterpret_cast<BYTE*(__cdecl*)(BYTE* npc)>(0x483B00);
inline auto const CombatTarget = reinterpret_cast<BYTE*(__cdecl*)(BYTE* human)>(0x880B70);  // human +0xF8C: whom it's fighting (perception's choice)
inline auto const InView = reinterpret_cast<bool(__cdecl*)(const float* centre, float radius)>(0x768400);

// ---- Timestamps: an int, -1 unset, else when it runs out on the game's clock (ms, 0x1121300: it stops while paused).
inline auto const TimerSet = reinterpret_cast<bool(__fastcall*)(int* timer, void* unused)>(0xC8B240);
inline auto const TimerElapsed = reinterpret_cast<bool(__fastcall*)(int* timer, void* unused)>(0xC8B120);
inline auto const TimerStart = reinterpret_cast<void(__fastcall*)(int* timer, void* unused, int ms)>(0xC8B030);
inline auto const TimerClear = reinterpret_cast<void(__fastcall*)(int* timer, void* unused)>(0xD27670);
inline int GameTime() { return *reinterpret_cast<const int*>(0x1121300); }
inline int GameHour() { return *reinterpret_cast<const BYTE*>(0x12F76D4); }

// ---- Small things
struct V3 { float x, y, z; };
inline V3 Pos(const BYTE* object) { return *reinterpret_cast<const V3*>(object + kPosition); }
inline V3 Forward(const BYTE* object) { return *reinterpret_cast<const V3*>(object + kOrientation + 24); }
inline float Distance(const V3& a, const V3& b) { const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z; return std::sqrt(dx * dx + dy * dy + dz * dz); }
inline float FlatDistance(const V3& a, const V3& b) { const float dx = a.x - b.x, dz = a.z - b.z; return std::sqrt(dx * dx + dz * dz); }
inline DWORD Handle(const BYTE* object) { return *reinterpret_cast<const DWORD*>(object + kHandle); }
inline DWORD VehicleOf(const BYTE* human) { return *reinterpret_cast<const DWORD*>(human + kInVehicle); }
float Random();                       // 0..1
int RandomBetween(int low, int high);  // low..high
inline bool IsAutomobile(BYTE* vehicle) { return VehicleSubtype(vehicle, nullptr) == kAutomobile; }

// The orientation for a forward vector (right, up, forward).
void Orientation(const V3& forward, float* m);
// Tells an NPC's AI to sit this frame out (its planner, perception and firing).
void SitOut(BYTE* npc);
// A vehicle's entry in the game's list (vehicles.xtbl), by name; null if none.
BYTE* VehicleEntry(const std::string& name);
// One of the models: one already streamed in, else one at random (the spawn asks for it); null if none is known.
BYTE* PickVehicle(const std::vector<std::string>& names);
// A place on an ambient road near a point, facing along it (either way at random), on its right; false if none.
bool RoadPlace(const V3& around, V3& where, V3& forward);
// Clears a vehicle's ambient wait and stuck timers.
void EndWait(BYTE* vehicle);
// A vehicle's damage, 0 (none) to 1.
float Damage(BYTE* vehicle);
// Which of the mod's spawners made a vehicle (traffic, a convoy, the EDF's drive-bys, a safehouse refill), for the
// log; null for one the mod didn't make. The last 256 are remembered.
void NoteMadeBy(DWORD vehicle, const char* maker);
const char* MadeBy(DWORD vehicle);
// For a vehicle whose spawn waited for its model: how long after the model streamed in it was made (ms; -1 unknown:
// its model was in before anything asked for it). Noted by the vehicle spawn's hook (guerrilla_vehicles.cpp).
void NoteModelAge(DWORD vehicle, int ms);
int ModelAge(DWORD vehicle);

// ---- What GuerrillaBuilder.exe wrote (mods\guerrilla_vehicles\traffic.txt, mods\guerrilla_people\crews.txt)
struct Zone {
    int col, row;
    std::string group, resource;
    int day, night;  // 0 none .. 3 high
};
struct Data {
    std::vector<Zone> zones;                                                    // zones with traffic
    float zoneWidth = 511.0f;
    int columnAtZero = 8, rowAtZero = 7;
    std::vector<std::pair<std::pair<int, int>, std::string>> resources;          // every zone's spawn resource
    std::vector<std::pair<std::string, std::vector<std::string>>> groups;        // traffic's vehicle groups
    std::vector<std::pair<std::string, std::vector<std::string>>> edf;           // the EDF's ambient actions' vehicles
    std::vector<std::string> gunnerVehicles;                                     // vehicles with a gunner's seat
    std::vector<std::string> dropOff;                                            // Default Guerilla: the drop-off's vehicles
    std::vector<std::pair<std::pair<std::string, std::string>, std::vector<std::string>>> people;  // (resource, team) → characters
};
const Data& Files();  // read once
std::vector<std::string> Split(const std::string& text, char by);
const Zone* TrafficZoneAt(const V3& where);
const std::string& ResourceAt(const V3& where);  // the zone's spawn resource (rfg_Default if none)
const std::vector<std::string>* People(const std::string& resource, const char* team);  // "Guerilla", "EDF", "Marauder"
const std::vector<std::string>* EdfVehicles(const char* action);                       // "driveby", "harass", "flyby"
const std::vector<std::string>* DropOffVehicles();  // the player's team's default group, Default Guerilla (drop_off.cpp)
bool HasGunnerSeat(BYTE* vehicle);
bool InTrafficWorld();  // the player's world is one named in [GuerrillaVehicles] TrafficWorlds=
void ReadWorlds(const std::wstring& worlds);

// Someone the mod made and drives with: who managed them before (given back with the vehicle).
struct Person {
    DWORD handle;
    int management;
};

// The parts that run each frame from traffic's hook on the spawn nodes' frame (traffic.cpp), started by it.
void StartConvoys();  // convoys.cpp
void ConvoysFrame();
void StartEdf();      // edf.cpp
void EdfFrame();
void StartChases();   // crews.cpp
void ChasesFrame();
void StartDropOff();  // drop_off.cpp
void DropOffFrame();
bool PlayerInSafehouse();  // safehouses.cpp: the player is in a safehouse's area
// guerrilla_campaign's state (campaign_api.h): the alert level (0 green .. 3 red; 0 without the campaign
// or while its systems rest), whether an activity or mission runs, and whether the player's district is liberated.
int CampaignAlert();
bool CampaignActivityRunning();
bool PlayerDistrictLiberated();
void StartSafehouses();  // safehouses.cpp
void SafehousesFrame();
void StartRiders();      // riders.cpp (a log: shooting riders, getting into occupied vehicles)
void RidersFrame();
void StartFlyers();   // flyers.cpp (its own hook, on the vehicles' presim)
bool FlyersFly();     // Guerrilla's flight model is on: NPCs can fly Guerrilla's flyers

}  // namespace gv
