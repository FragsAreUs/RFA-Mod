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
