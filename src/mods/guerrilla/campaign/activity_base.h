// One kind of activity, as Guerrilla's activity_base (with activity_base_modal and
// activity_base_free folded in by `modal`): a single object per kind, its state machine run once a
// frame by the framework (activities.cpp), the kind's own logic in the virtual functions.
#pragma once
#include "activities.h"

namespace campaign::activities {

class Activity {
public:
    Activity(Mode mode, bool modal, bool canAlwaysStart) : mode_(mode), modal_(modal), canAlwaysStart_(canAlwaysStart) {}
    virtual ~Activity() = default;

    Mode GetMode() const { return mode_; }
    bool Modal() const { return modal_; }
    State GetState() const { return state_; }
    unsigned Node() const { return node_; }

    // Offers a run from a node (start_activity_spawn): the run loads (LOADING).
    bool StartSpawn(unsigned node, const BaseData* data, int district);
    // The frame (activity_base::activity_do_frame): one step of the state machine.
    void DoFrame(float seconds);

    void Disable(DisableReason reason);  // kills a run that is offered or running
    void Enable(DisableReason reason);
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
    // The diversions panel (hud_ui_missions_process_diversion_info RM 0x8C56A0): the kind's title, a string tag
    // (activity_get_name: get_title), and its text and number (activity_get_ui_text: get_ui_display_info; the number
    // is the time left in ms, the icon flashing under 6 s), false for none.
    virtual const char* Title() const { return ""; }
    virtual bool UiText(std::string& text, int& number) const { return false; }
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
// After a modal run ends, no other is offered for a while (Guerrilla's cooldown).
void StartCooldown();

// activity_register_damage_is_player_controlled (RM 0x785790): whether something was the player's doing: the
// player; his vehicle, or the one he last drove for 40 s after he left it (while no one else has driven it); a
// turret he operates; an item he owns. (Guerrilla's marks, an object the player hurt by 100 or more in the last
// 20-40 s, come with the damage listeners, step 5b.) Armageddon's objects.
bool PlayerControlled(const void* object);
// Guerrilla's team dispositions (human_teams_get_disposition RM 0xA0D560): how the victim's team takes the
// player's (0 hostile .. 3 friendly, 4 allied), as Armageddon's table holds them.
int DispositionToPlayer(const void* human);
// A human died (district_sources' wrap of Armageddon's human death): each listener is told, once a death.
void ReportDeath(void* victim, void* killer);

// Killing spree (killing_spree.cpp).
Activity* MakeKillingSpree();

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
