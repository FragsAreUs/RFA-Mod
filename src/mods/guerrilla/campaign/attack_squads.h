// Guerrilla's reinforcement squads (reinforcement_squad, RM): EDF squads sent at a target with attack orders, kept
// while they fight it or close on it, let go when they don't. Its EDF reinforcements (reinforcements.cpp) and its
// continuous attacks (continuous_attack.cpp) both send theirs so; each picks its own squads and places
// (attack_squads.cpp).
#pragma once
#include "campaign.h"
#include <windows.h>
#include <string>
#include <vector>

namespace campaign::attack_squads {

// One squad sent.
struct Squad {
    DWORD squad = 0xFFFFFFFF;
    DWORD vehicle = 0xFFFFFFFF;  // its vehicle, once one of it sits in one
    DWORD target = 0xFFFFFFFF;   // what it attacks
    std::string what;            // for the log: its squad and vehicle group
    int usefulUntil = 0;         // its timer: let go once it runs out while it neither engages nor closes on the target
    float lastDistance2 = 1e30f; // to the target, at the last look (is_closing_to_target)
    bool chased = false;         // its vehicle handed to guerrilla_vehicles' chase driver (continuous attacks)
    bool ownAi = false;          // its vehicle isn't a car (a flyer, a walker): it keeps its own AI
    bool hadMembers = false;     // the game has made its people (they come a few frames after the squad)
    bool rides = false;          // made with a vehicle group: its people come with its vehicle, once its model has streamed in
    int madeAt = 0;              // the game's clock when it was made
    int handAgainAt = 0;         // its car isn't handed to the chase driver again before then (a chase that ended by itself)
};

// What to make: a squad of squads.xtbl by name, in a vehicle spawn group if one is named (tried as the world build's
// "rfg_" name, then as it is), at a place, facing the target, with attack orders on it.
struct Order {
    std::string squad;         // squad_def
    std::string group;         // vehicle_group, "" on foot
    Vector place{};
    DWORD target = 0xFFFFFFFF; // the target's handle (it must be there)
    const char* weaponSet = nullptr;  // guerrilla_people's squad set for its members' weapons (null for none)
    const char* who = "";      // for the log
    // Its people made at once wherever it is (squad_flags force_spawn): Armageddon makes a squad's people only once it
    // has been within 150 m of the player (its inside_load_dist), so a squad sent from farther waits empty until then.
    bool forceSpawn = false;
};

enum class Made { Made, Wait, Failed };
// Made: out holds it. Wait: the game made nothing (its vehicle's model may still be streaming in): try again.
// Failed: no such squad (or the target is gone): don't.
Made Make(const Order& order, Squad& out, std::string& why);

// Whether a vehicle group exists by the name a table gives (the world build's "rfg_" name, or as it is).
bool GroupExists(const std::string& name);
bool IsFlyerGroup(const std::string& name);

// Its living members.
std::vector<BYTE*> Members(DWORD squad);

// Whether a target is there and not killed (the object's is_killed).
bool TargetAlive(unsigned target);

// Guerrilla's squad_despawned: whether a squad is done, by the game's own squad object, not by its people alone (a
// squad just made has none for a few frames: 2026-11-19, counting it gone then filled the game's 100 squads in 34 s).
// Done: its squad object is gone, or its people were made and are all dead or gone, or the game never made them
// (15 s; 60 s for a squad that rides, whose people the game makes only with its vehicle). Null to keep it; else why (a
// squad never filled is let go, its object freed). Its timer (Follow) starts when its people come, as a Guerrilla
// squad is whole at its spawn.
const char* Gone(Squad& squad, const std::vector<BYTE*>& members, int now);

// Its vehicle as the game's squad knows it (object_squad +0x10C, set by its member spawn 0x80D410 when it made the
// vehicle; the vehicle names the squad at +0x50A4), else one its people sit in. kNone while it has none.
DWORD VehicleOf(const Squad& squad, const std::vector<BYTE*>& members);

// reinforcement_squad::do_frame_spawned, each look: null to keep it; else why it is to go: the target dead or gone,
// farther than 325 m from the target, or neither engaging nor closing on it once its timer ran out (20 s from its
// spawn, kept at 10 s or more while it engages or closes). Its members are looked at.
const char* Follow(Squad& squad, const std::vector<BYTE*>& members, int now);

// Its members on foot who don't see the target, sent running to it (every few seconds; Guerrilla's
// update_target_beliefs, here as the alert orders' squads are chased).
void Chase(const Squad& squad, BYTE* target);

// A squad let go: its people on foot walk off and go out of view, its vehicle (and those in it) goes once unseen, and
// its squad object is freed once none of its people is left (Guerrilla's despawn_squad, LOW).
void LetGo(Squad& squad);

// The squads let go: their vehicles taken away (with the people still in them) once nobody sees them, and each squad
// object freed (the game's world::destroy_object) once none of its people is left.
void RetireLeaving(BYTE* player);

// How many squads are leaving (let go, their people or objects not yet gone).
size_t Leaving();

// A world starts: none leaving. Each frame of play: RetireLeaving every half second.
void Enter();
void Frame();

constexpr int kFirstUseMs = 20000;  // a squad's timer at its spawn
constexpr int kKeepUseMs = 10000;   // kept at least this long while it engages or closes
constexpr float kTooFar = 325.0f;   // farther than this from its target: let go
constexpr int kMakeMembersMs = 15000;  // ours: a squad whose people the game hasn't made by then is let go
constexpr int kMakeVehicleMs = 60000;  // ours: the same for a squad that rides (its vehicle's model may have to stream in first)

}  // namespace campaign::attack_squads
