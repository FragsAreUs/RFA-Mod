// One kind of activity, as Guerrilla's activity_base (with activity_base_modal and
// activity_base_free folded in by `modal`): a single object per kind, its state machine run once a
// frame by the framework (activities.cpp), the kind's own logic in the virtual functions.
#pragma once
#include "activities.h"
#include "world_objects.h"

struct GuerrillaCampaignIcon;  // campaign_api.h

namespace campaign::activities {

class Activity {
public:
    Activity(Mode mode, bool modal, bool canAlwaysStart) : mode_(mode), modal_(modal), canAlwaysStart_(canAlwaysStart) {}
    virtual ~Activity() = default;

    Mode GetMode() const { return mode_; }
    bool Modal() const { return modal_; }
    bool CanAlwaysStart() const { return canAlwaysStart_; }
    State GetState() const { return state_; }
    unsigned Node() const { return node_; }

    // Offers a run from a node (start_activity_spawn): the run loads (LOADING).
    bool StartSpawn(unsigned node, const BaseData* data, int district);
    // The frame (activity_base::activity_do_frame): one step of the state machine.
    void DoFrame(float seconds);

    void Disable(DisableReason reason);  // kills a run that is offered or running
    void Enable(DisableReason reason);
    void KillNow() { Kill(); }           // activity_kill, whatever its state (a free kind's clears what it gathered)
    void Withdraw() {  // an offered run, no longer offered
        if (state_ == State::Ready) Kill();
    }
    bool Disabled() const { return disabled_ != 0; }

    virtual void Reset();
    virtual void LevelInit();
    virtual bool CanInitialize() const;
    bool WasInitialized() const { return state_ >= State::Ready && state_ <= State::Running; }
    virtual bool IsActive() const;
    virtual bool LockAlertLevel() const { return false; }
    virtual HudInfo Hud() const { return {}; }

    // A free kind starts itself (activity_base_free::activity_start RM 0x7ACB30): unless it is offered or running
    // already, it must be able to initialize, and it runs in the player's district, straight to RUNNING.
    bool StartFree();
    // Guerrilla's damage listeners (its table of four, RM 0x12DB0C4: riding shotgun, mayhem, killing spree, area
    // defense): told of every death while not disabled or suspended, even while not running (activity_register_death
    // RM 0x759DB0, from human_died: the victim and its killer, a human or the object that killed it).
    virtual bool Listens() const { return false; }
    virtual void RegisterDeath(void* victim, void* killer) {}
    // The other four (register_damage 0x799CA0, register_building_damage 0x759E00, register_building_destroyed
    // 0x759E70, register_explosion 0x759F20): a hit (the two objects, as the game's damage gave them, and the damage),
    // a building's new damage (its damage fraction's change since the last report, and the fraction before), a building
    // counted as destroyed, an explosion (its owner).
    virtual void RegisterDamage(void* victim, void* attacker, float damage) {}
    virtual void RegisterBuildingDamage(void* building, float damage, float before) {}
    virtual void RegisterBuildingDestroyed(void* building) {}
    virtual void RegisterExplosion(void* owner) {}
    // The diversions panel (hud_ui_missions_process_diversion_info RM 0x8C56A0): the kind's title, a string tag
    // (activity_get_name: get_title), and its text and number (activity_get_ui_text: get_ui_display_info; the number
    // is the time left in ms, the icon flashing under 6 s), false for none.
    virtual const char* Title() const { return ""; }
    virtual bool UiText(std::string& text, int& number) const { return false; }
    // Natural spawning (do_activity_spawn_frame RM 0x7A83F0, natural_spawning.cpp): the nodes a run of this kind could
    // start at now, for a player at `player` (collect_spawn_objects, vt+0xAC; false for none); the activity.xtbl row a run
    // from one of them uses (get_base_activity_data 0x769320; null for none); and, once the spawner started a run there,
    // what the kind keeps of it (a convoy end point's 10 minutes).
    virtual bool CollectSpawnObjects(const Vector& player, std::vector<const world::Object*>& nodes) const { return false; }
    virtual const BaseData* NodeBaseData(const world::Object& node) const { return nullptr; }
    virtual bool NodeResting(const world::Object& node) const { return false; }  // a convoy end point's 10 minutes
    virtual void NaturallySpawned(const world::Object& node) {}
    // The maps (render_icons vt+0x74, while READY, STARTING or RUNNING; get_icon_image_name vt+0x88): the kind's
    // temporary map icons now (how many written), its picture's name for the offer's hint (null for none), and where
    // its icon for the node it was started at is (the GPS key's target: false while it has none).
    virtual int MapIcons(GuerrillaCampaignIcon* icons, int most) const { return 0; }
    virtual const char* IconImageName() const { return nullptr; }
    virtual bool SpawnedIcon(unsigned node, Vector& at) const { return false; }
    Interface GetInterface() const { return interface_; }
    float InterfaceTime() const { return interfaceTime_; }  // seconds since the interface state changed

protected:
    // Guerrilla's steps (the virtual functions of activity_base, in its order).
    virtual bool Initialize();     // LOADING finished: READY (offered)
    virtual bool BeginStartUp();   // READY -> STARTING
    virtual bool Start();          // STARTING -> RUNNING
    virtual void End();            // -> SHUTDOWN
    virtual void Kill() { End(); }
    virtual bool CleanUpCompleted() const;
    virtual bool StartingCompleted() const { return true; }
    virtual void Complete();       // SHUTDOWN -> WAIT
    virtual void FrameInactive(float) {}
    virtual void FrameReady(float) {}
    virtual void FrameStarting(float) {}
    virtual void FrameActive(float) {}
    virtual void FrameCleanUp(float seconds);
    virtual void CleanUpOnce();
    virtual void RecordVictory(const Rewards& rewards);
    virtual void RecordLoss();

    void ChangeState(State state);
    void ChangeInterface(Interface state);

    const Mode mode_;
    const bool modal_;
    const bool canAlwaysStart_;
    State state_ = State::Wait;
    Interface interface_ = Interface::None;
    unsigned node_ = 0xFFFFFFFF;
    int district_ = -1;
    const BaseData* data_ = nullptr;
    unsigned disabled_ = 0;      // one bit per DisableReason
    float stateTime_ = 0.0f;     // seconds since the last state change
    float interfaceTime_ = 0.0f;
    bool cleanedUp_ = false;
    bool victoryRecorded_ = false;
    bool layersRequested_ = false;  // its managed layers asked for (managed_layers.cpp)
    float shutdownTime_ = 0.0f;  // modal: the run stays in SHUTDOWN at least 1 s
    bool active_ = false;        // free: running
};

// The framework's own: the current modal activity, and each kind's object.
Activity* Get(Mode mode);
void Register(Activity* activity);
Mode CurrentMode();
void SetCurrentMode(Mode mode);
// Guerrilla's activity_none (RM 0x7699C0): no modal activity is current, and no offensive raid or house arrest is
// loading, offered or starting (AnyRunning is its opposite).
bool NoneActive();
// The natural spawner (natural_spawning.cpp): its frame (activity_system_do_frame's every 4-6 s, RM 0x7ACBB0), its
// level start, a modal run's end (its 90-120 s, activity_base_modal::activity_end_common 0x769950), the last node it
// started (activity_get_last_spawned, for the GPS key: 0xFFFFFFFF none, and its kind), and the console's pass.
void SpawnFrame();
void SpawnLevelInit();
void SpawnModalEnded();
unsigned LastSpawnedNode(Mode* mode);
void SetLastSpawned(unsigned node, Mode mode);
void SpawnCommand();
void OfferHintFrame();  // an offered run's frame: the hint once its 5 s after a natural spawn are up
// Guerrilla's restricted group (activity_is_allowed_to_initialize RM 0x7551D0): mission vrail, convoy, courier and
// both raids; one of them may initialize only while no other is offered, starting or running.
bool RestrictedGroupAllows(Mode mode);
// After a modal run ends, no other is offered for a while (Guerrilla's cooldown). A level's start clears it (Guerrilla's
// level_init 0x797120, as a retry's reload does: ClearCooldown); the seconds left (CooldownLeft) for the log.
void StartCooldown();
void ClearCooldown();
float CooldownLeft();

// activity_register_damage_is_player_controlled (RM 0x785790): whether something was the player's doing: an object
// marked (below); the player; his vehicle, or the one he last drove for 40 s after he left it (while no one else has
// driven it); a turret he operates; an item he owns. Armageddon's objects.
bool PlayerControlled(const void* object);
// The marks (register_mark_object, mark_objects): an object hit by 100 or more with the player's doing on either
// side counts as the player's for 20-40 s (two sets of handles, the older dropped every 20 s).
void MarkObjects(const void* a, const void* b);
// The game's damage, as Guerrilla's dispatchers give it to the listeners (damage_feeds.cpp and the parts' wraps call
// these): a hit (activity_register_damage: the marks first), a death of anything but the player
// (activity_register_death), a building's new damage, a building destroyed, an explosion.
void ReportDamage(void* victim, void* attacker, float damage);
void ReportBuildingDamage(void* building, float damage, float before);
void ReportBuildingDestroyed(void* building);
void ReportExplosion(void* owner);
// activity_register_damage_get_cost (RM 0x7705C0): what damage to something costs, in millions of credits (mayhem's
// score; riding shotgun's later): people 0.1, vehicles their table's value, buildings their own value or their size
// and importance, props their size (mayhem.cpp).
float DamageCost(const void* object);
// A mission activated (mission_activate_post_save RM 0x79D7EF), by Guerrilla's mission mode: the Tutorial (4) kills
// mayhem and disables it for good. For the mission framework (step 14); nothing calls it yet.
void MissionActivated(int missionMode);
// Guerrilla's team dispositions (human_teams_get_disposition RM 0xA0D560): how the victim's team takes the
// player's (0 hostile .. 3 friendly, 4 allied), as Armageddon's table holds them.
int DispositionToPlayer(const void* human);
// Something died (a human: district_sources' wrap of Armageddon's human death; a vehicle or a prop: damage_feeds.cpp
// and guerrilla_world's prop wrap): each listener is told, once a death.
void ReportDeath(void* victim, void* killer);

// Transporter (delivery.cpp): the running run's corner panel and ring, the activity-complete screen's answer (1 its first
// hint, 2 its second), the player's death (as he dies; and as his death timer runs out, DeliveryTakePlayerDeath: true
// when the run took it), a start node's progress (completed), its progress read again after a load.
Activity* MakeDelivery();
bool DeliveryScreen(Screen& screen);
void DeliveryChoose(int choice);
void DeliveryPlayerDied();
bool DeliveryTakePlayerDeath();
void DeliveryReopened(const char* how);  // the world opened again (a retry's reload): its start is set up
bool DeliveryNodeDone(unsigned node);
void DeliveryReloadProgress();

// Killing spree (killing_spree.cpp), mayhem (mayhem.cpp), the car bomb with its M.O.A.B.s (car_bomb.cpp: its
// world items made and touched each frame, CarBombFrame).
Activity* MakeKillingSpree();
Activity* MakeMayhem();
Activity* MakeCarBomb();
void CarBombEnter();

// Continuous attack (continuous_attack.cpp): EDF squads sent in waves at a target, by a row of
// activity_continuous_attack.xtbl (activity_cont_attack_start: false with no target, no such row, or one running), its
// end, and the script words' changes (attacker_count capped at 15, the squads' sub-group).
Activity* MakeContinuousAttack();
bool ContinuousAttackStart(unsigned target, const char* row, const char* who);
void ContinuousAttackEnd(const char* why);
void ContinuousAttackClear(const char* why);  // ended, its squads taken away now (behind a fade)
void ContinuousAttackSetCount(int count);
void ContinuousAttackChangeCount(int change);
void ContinuousAttackSetSubGroup(int group);
void CarBombFrame();
// The damage feeds (damage_feeds.cpp): Armageddon's vehicle damage and end, the movers' hits, the buildings' damage
// reports and destruction, the explosions, wrapped once at start-up.
void StartDamageFeeds();

// Area defense, "HEAVY METAL" (area_defense.cpp): the use prompt at a start object (Guerrilla's icon, 0 none), the
// running run's play area for the minimap's ring (false: none), whether a node (or its start object) is completed, the
// progress read again after a load, the player's death.
Activity* MakeAreaDefense();
int AreaDefenseUsePrompt();
bool AreaDefenseRing(Vector& center, float& radius);
bool AreaDefenseNodeDone(unsigned handle);
void AreaDefenseReloadProgress();
void AreaDefensePlayerDied();
// For the HUD: the corner panel, the timer and a win's stamp (false: none); the results screen's answer (1 RETRY,
// 2 EXIT); the player's death timer ran out (true when the run took the death); a retry's reload is in.
bool AreaDefenseScreen(Screen& screen);
void AreaDefenseChoose(int choice);
bool AreaDefenseTakePlayerDeath();
void AreaDefenseReopened(const char* how);

// Convoys (convoy.cpp): Guerrilla's convoy activity, its convoys driven by guerrilla_vehicles' convoys.cpp; whether it
// runs the convoys (its table and theirs are there: else guerrilla_vehicles sends its own), and the console's start.
Activity* MakeConvoy();
bool ConvoysRun();
bool ConvoyNodeDone(unsigned node);  // a convoy end point's convoy won
void ConvoyReloadProgress();
// For the HUD: the convoy's corner panel (false: none), and its route on the maps (x and z pairs; how many points).
bool ConvoyScreen(Screen& screen);
int ConvoyRoute(float* xz, int most);

// Demolitions master (demolitions_master.cpp).
Activity* MakeDemolitionsMaster();
bool StartDemolitionsMaster(const char* level);  // by the table row's name, for trying it out
// Whether the activity at a node (Guerrilla's handle) is completed: a demolitions master node's own
// progress (the campaign's save over the zone's demolitions_master_complete); false for other nodes
// until their kinds are ported. Kept in memory, so it is cheap to ask every frame.
bool NodeDone(unsigned node);
// The nodes' progress read again from the campaign's save (after a game save was loaded).
void ReloadProgress();
// For the HUD and the death hook: demolitions master's screen now (false: none), the HUD's answer (the
// briefing: 1 accept, 0 cancel; the results: close), the use prompt at a node (Guerrilla's icon, 0 none),
// the player's death.
bool DemolitionsScreen(Screen& screen);
void DemolitionsChoose(int accept);
int DemolitionsUsePrompt();
void DemolitionsPlayerDied();

}  // namespace campaign::activities
