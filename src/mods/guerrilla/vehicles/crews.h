// Crewed vehicles: a vehicle the mod made (or was handed) with the people who drive it and man its guns,
// shared by convoys.cpp and edf.cpp; and Guerrilla's chase in a vehicle (chase_target_in_vehicle_action,
// chase_target_with_manned_turret, chase_target_hold_position: Armageddon deleted them), which they and the
// people part's Marauder ambushes (GuerrillaVehicles_Chase, below) use.
#pragma once
#include "game.h"
#include <string>
#include <vector>

namespace gv {

struct Crew {
    DWORD vehicle = kNone;
    std::vector<Person> people;        // the driver first
    std::vector<std::string> waiting;  // people still to be made and seated (their characters)
    int boardingSince = -1, boardedAt = -1;  // the game's clock
    // Its motion, from frame to frame.
    V3 lastPosition{}, velocity{};
    int lastTime = -1;
    float speed = 0;
};

// A vehicle being made: its spawn parameters, tried again each frame while its model streams in.
struct VehicleSpawn {
    BYTE parameters[0x100];  // vehicle_spawn_params (0x60 bytes and more)
    std::string model;
    int since = -1;
};
// Flags: Guerrilla's traffic's 0x40B (no save, one attempt, on a road, moving), or 0x403 for a flyer (the same, not on a road).
void PrepareSpawn(VehicleSpawn& spawn, BYTE* entry, const V3& where, const V3& forward, float speed, DWORD flags);
// 0 made (*made), 1 still waiting for its model, 2 failed (refused, or not streamed in within 20 s).
int TrySpawn(VehicleSpawn& spawn, BYTE** made);

// The crew's people still waiting made and seated (the driver first, each in the best free seat); true once all are.
bool Board(Crew& crew, BYTE* vehicle);
// The first 5 s after everyone sat down (a driver seated by teleport may not be at the wheel at once).
bool Settling(const Crew& crew);
// Keeps the crew's speed and velocity (from the vehicle's move since the last frame).
void Track(Crew& crew, BYTE* vehicle);
// The crew's driver, if it sits at this vehicle's wheel and is driving.
BYTE* Driver(const Crew& crew, BYTE* vehicle);
// A crew member aboard other than the driver, when the vehicle has a gunner's seat (traffic.txt "gunners").
BYTE* Gunner(const Crew& crew, BYTE* vehicle);
// Only the driver sits the frame out: the others keep their own AI, so a gunner aims and fires.
void YieldDriver(const Crew& crew, BYTE* vehicle);
// Any crew member's combat target is the player (only those not sitting frames out notice anyone).
bool NoticedPlayer(const Crew& crew);
// A crew member was made and is dead or gone now.
bool LostSomeone(const Crew& crew);
// Unusable as Guerrilla's ai_vehicle_is_usable has it: on fire, a wreck, or flipped (on its side or roof).
bool Unusable(BYTE* vehicle);
// Gives the crew and vehicle back to the game: the vehicle's AI and speed as before, the people's managers.
void HandBack(Crew& crew, BYTE* vehicle);
// The crew out of the vehicle (all but a gunner, if kept), handed back: on foot Armageddon's own AI fights.
void GetOut(Crew& crew, BYTE* vehicle, bool keepGunner);
// The crew and vehicle taken away now.
void Despawn(Crew& crew);
// The crew and vehicle taken away once nobody can see them (out of view and 60 m away, or 300 m away).
void RetireWhenUnseen(Crew& crew);

struct Chase {
    DWORD target = kNone;
    int state = 0;  // 0 chasing, 1 holding (the gunner shoots), 2 stopping to get out
    int side = 1;   // the side it circles or flanks on
    int sideUntil = -1, holdCheck = -1, lostSince = -1;
    V3 targetLast{}, targetVelocity{};
    int targetTime = -1;
};
enum class ChaseResult { Chasing, OnFoot, Lost, Over };
// One frame of Guerrilla's chase for the crew's driver (the gunner, if any, fires on its own): OnFoot once
// the crew got out to fight, Lost once the target is gone or far, Over once the crew can't drive (dead,
// out, the player took the vehicle).
ChaseResult ChaseFrame(Crew& crew, Chase& chase, BYTE* vehicle);

}  // namespace gv
