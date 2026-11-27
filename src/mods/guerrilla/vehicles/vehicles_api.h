// What guerrilla_vehicles gives the other parts of rfa_guerrilla.dll (they were its DLL's exports; each is described
// where it is made, too). While the vehicles part isn't running (guerrilla_vehicles=0 under [Mods]), each answers as
// its missing DLL did: no chase is handed over.
#pragma once

// For the people part's Marauder ambushes and the campaign's attacks (crews.cpp): from the next frame on, the
// vehicle's driver (an NPC in seat 0) chases the target, the other occupants keeping their own AI. False if the
// vehicle or target isn't there, the vehicle isn't a car (a flyer or walker keeps its own AI), or nobody but the
// player drives it. Call on the game's thread.
bool GuerrillaVehicles_Chase(unsigned long vehicleHandle, unsigned long targetHandle);

// The same, the chase giving up beyond `giveUpMeters` (flat) from the target, in place of Chase's 250 m: for the
// campaign's attacks, whose squads Guerrilla keeps within 325 m of their target. False also for giveUpMeters <= 0.
bool GuerrillaVehicles_ChaseWithin(unsigned long vehicleHandle, unsigned long targetHandle, float giveUpMeters);

// True while a vehicle handed over with GuerrillaVehicles_Chase(Within) still chases (a chase ends by itself when
// the target is gone or past its give-up distance, the crew gets out to fight, or nobody can drive). Hand it over
// again to restart.
bool GuerrillaVehicles_Chasing(unsigned long vehicleHandle);

// Gives a vehicle handed over with GuerrillaVehicles_Chase back to the game now.
void GuerrillaVehicles_Release(unsigned long vehicleHandle);

// For guerrilla_campaign's natural spawner: the player is inside an enabled safehouse's area (safehouse_contains_player).
bool GuerrillaVehicles_PlayerInSafehouse();

// For guerrilla_campaign's convoy activity (convoy.cpp, Guerrilla's activity_convoy_data), which watches and steers
// Guerrilla's object_convoy (convoys.cpp). While the campaign runs the convoys (GuerrillaCampaign_RunsConvoys), only
// these start one.
//
// spawn_convoy (RM 0x780FA0 → convoy_create 0xA8F100): the convoy of a convoys.xtbl row (convoys.txt's name) sets off
// from its end point now, a point far away until the player comes within 300 m. False if no convoy of that name was
// built, or one is out already.
bool GuerrillaVehicles_ConvoySpawn(const char* name);

// One member of the convoy (squad_info: a squad and its vehicle), as the activity's update_squad_status 0x76B8C0 and
// update_squad_vehicles 0x76B960 keep them.
struct GuerrillaVehiclesConvoyMember {
    int role;               // 0 target, 1 defender, 2 break and attack (convoys.xtbl's Role; 2 once every role is)
    unsigned long vehicle;  // its vehicle's handle, 0xFFFFFFFF while the convoy is only a point
    int vehicleKilled;      // its vehicle was made and is destroyed or gone (vehicle_killed)
    int squadKilled;        // its people were made and are all dead or gone (squad_killed)
    int leftConvoy;         // it left the convoy's line (squad_left_convoy: to attack)
    int driverless;         // nobody of its crew is at the wheel any more (update_target_vehicle_driver)
    int noticedPlayer;      // one of its crew fights the player (has_player_enemy_belief)
    float damage;           // its vehicle's damage, 0..1
    float speed;            // its vehicle's speed (m/s) while its crew drives it in line, else 0
    float x, y, z;          // where it is (the convoy's point while it's one)
};
struct GuerrillaVehiclesConvoyState {
    int size;            // sizeof(GuerrillaVehiclesConvoyState): the caller sets it
    int members;         // how many (12 at most)
    int simulated;       // only a point far away: no vehicle made
    int routePoints;     // the route's points
    int nextPoint;       // the lead's next one (next_visited_index)
    int atDestination;   // its lead reached the route's end (convoy_do_frame → activity_convoy_at_destination): its
                         // targets parked with their crews out, the others gone once out of sight
    int attacked;        // its own attacked flag (a vehicle at least 1 % damaged with its crew fighting the player)
};
// The convoy now, its members into `members` (at most `most`); false when there is none.
bool GuerrillaVehicles_ConvoyStatus(GuerrillaVehiclesConvoyState* state, GuerrillaVehiclesConvoyMember* members, int most);
// begin_start_up's convoy flag attacked (RM 0x780A50): its evade speed.
void GuerrillaVehicles_ConvoyAttacked();
// Every role break and attack (update_target_vehicle_driver 0x780B30, capture_target_left_convoy 0x757930).
void GuerrillaVehicles_ConvoyAllAttack();
// release_attack_squads (0x7B1BB0) and update_squad_orders (0x7AD2C0): each break-and-attack member that hasn't yet
// leaves the line and goes for the player (Guerrilla's chase in a vehicle; on foot, Armageddon's own fight).
void GuerrillaVehicles_ConvoyReleaseAttackers();
// process_target_driver_flags (0x76A6A0): whether the capture target's crew may get out (its vehicle at least 30 %
// damaged or stopped over 2 s); out they get once it's stopped, or it can't be driven.
void GuerrillaVehicles_ConvoyTargetMayExit(bool may);
// target_vehicle_delivered (0x76BC00): the player out of the delivered target, the vehicle stopped and locked for him
// and NPC drivers (vehicle flags disable_for_npc_drivers and disable_for_player).
void GuerrillaVehicles_ConvoyDelivered(unsigned long vehicle);
// The convoy object destroyed (object_convoy::free_this 0xA8E1B0: the activity's end or kill): what is only a point goes,
// the crews made are given back to the game.
void GuerrillaVehicles_ConvoyRemove();
// The level started again (a reload, as after the player's death; the activity's level_init): the convoy object went
// with the old level, so the activity's convoy is let go without touching anything it made, and its row is free again.
void GuerrillaVehicles_ConvoyForget();
// The activity's convoy's route (its patrol's points, as it drives them): x and z in pairs into xz, at most `most`
// points; how many were written (0: no convoy out).
int GuerrillaVehicles_ConvoyRoute(float* xz, int most);
