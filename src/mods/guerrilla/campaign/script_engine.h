// Armageddon's action-script engine as Red Faction: Guerrilla's mission scripts meet it, and how
// this mod adds to it. See script_engine.cpp for how each piece was found in the game.
#pragma once
#include <windows.h>
#include <stddef.h>

namespace campaign::script {

// Argument and result types: the game's script node types. Guerrilla's and Armageddon's agree up
// to the timer; Guerrilla then has one for air-strike handles (23) and the game handle (24), while
// Armageddon has only the game handle, as 23. A game handle is a value named in the script by its
// kind (<alert_level>Red</alert_level>, <air_bomb>FFZ</air_bomb>, ...) and looked up when the script
// is read.
enum Type : int {
    Void = 0,
    Number = 1,
    String = 2,
    Bool = 3,
    Object = 4,
    ObjectList = 5,
    Point = 6,
    VariableRef = 8,
    Timer = 22,
    Handle = 23,
};

// No object (object handles, and the end of an object list).
constexpr unsigned kNoObject = 0xFFFFFFFF;
// No value, for a game handle whose name wasn't found.
constexpr unsigned kNoHandle = 0xFFFFFFFF;

// A place in a script: an object and which of its script points.
struct ScriptPoint {
    unsigned object;
    int index;
};

// A node of a parsed script: an argument as the script gives it (a number, an object, a function
// to call, a variable, ...). The game's class; the mod only reads arguments through it.
struct Node {
    void** vtable;
    int nodeClass;  // atom, event, function, variable
    int type;       // Type
};

// A word's arguments, in the order of its argument list, ending with a null.
using Args = Node* const*;

// Reading an argument, as the node converts itself.
const char* AsString(Node* node);
bool AsBool(Node* node);
float AsNumber(Node* node);
int AsInt(Node* node);
unsigned AsObject(Node* node);
const unsigned* AsObjectList(Node* node);  // ends with kNoObject
unsigned AsHandle(Node* node);
ScriptPoint AsPoint(Node* node);

// A function's result.
union Variant {
    float number;
    const char* string;
    unsigned object;
    unsigned handle;
    bool flag;
    ScriptPoint point;
};
static_assert(sizeof(Variant) == 8, "the game's script_variant is 8 bytes");

// What the game asks of each kind of word. An action runs; a function gives a value (into
// `value`; `custom` is the node's own spare value); an event is asked, each time one of its kind
// happens, whether this one matches the script's arguments.
using ActionFn = void(__cdecl*)(Args args);
using FunctionFn = void(__cdecl*)(Variant& value, Variant& custom, Args args);
struct EventData;
using EventFn = int(__cdecl*)(const EventData* event, Args args);

// What an event callback answers: not this one; this one, and stop listening; this one, and go on.
enum EventResult : int { kEventIgnored = 0, kEventHandled = 1, kEventOk = 2 };

// The game's records of a word (the layouts in Armageddon's exe, the same as Guerrilla's).
struct ArgList {
    int types[5];
    int count;  // negative: the last argument may be given any number of times
};
struct ActionDefinition {
    ActionFn callback;
    const ArgList* args;
    const char* description;
    const char* group;
    int result;  // Type of the value it leaves for a <variable> (Void for none)
};
struct EventDefinition {
    EventFn callback;
    int eventType;  // the game's event type it listens for
    const ArgList* args;
    const char* description;
};
struct FunctionDefinition {
    FunctionFn callback;
    int result;  // Type
    const ArgList* args;
    const char* description;
    const char* group;
};
static_assert(sizeof(ActionDefinition) == 20 && sizeof(EventDefinition) == 16 && sizeof(FunctionDefinition) == 20 &&
              sizeof(ArgList) == 24, "the game's layouts");

enum class Kind { Action, Event, Function };

// One of Guerrilla's words the mod provides: added if Armageddon doesn't know it, otherwise its
// callback (and its argument list, where Armageddon changed it) put back to Guerrilla's.
struct Word {
    Kind kind;
    const char* name;
    ArgList args;
    int result;      // actions and functions: Type
    void* callback;  // ActionFn, EventFn or FunctionFn; null while the mod doesn't do it yet
};

// Puts the words into the game's dictionaries. Called once, before the first script is read.
void Install(const Word* words, size_t count);

// The value an action leaves for a <variable>.
void SetActionResult(unsigned value);

// The mod's events. Armageddon's event manager keeps its listeners in one list per event type (92
// of them); the game's own events use types up to 89. All of Guerrilla's events the mod adds share
// one type no game code uses, and say which of them they are in `kind`, with a mark so that a
// listener of the game's never takes one for its own.
constexpr int kModEventType = 91;
constexpr unsigned kModEventMark = 0x43474652;  // "RFGC"
struct EventData {
    int type = kModEventType;
    unsigned mark = kModEventMark;
    int kind = 0;                // which of Guerrilla's events: ModEvent
    unsigned values[4] = {};     // its values, as Guerrilla's event carries them
    float numbers[2] = {};
};
enum ModEvent : int {
    EventActivityStatusChange = 1,
    EventCivilianMoraleLowered,
    EventCivilianMoraleRaised,
    EventContinuousAttackReachedEnd,
    EventEdfControlLowered,
    EventHouseArrestDefenderChange,
    EventHumanStoppedBeingRendered,
    EventMissionVrailCompleted,
    EventObjectiveComplete,
    EventRaidComplete,
    EventRaidStatusChange,
    EventSatelliteReachesTarget,
    EventDialogueLineStarted,
    EventDialogueLineFinished,
    EventAlertLevelRaised,
};
// Is this one of the mod's events, of this kind?
inline bool IsModEvent(const EventData* event, int kind) {
    return event && event->type == kModEventType && event->mark == kModEventMark && event->kind == kind;
}
// Tells every script listening for it.
void Post(EventData& event);

// Listening to the game's own events, as a script's <event> does: the callback is told of every
// event of that type (return kEventOk to go on listening; kEventHandled stops it). Listeners must
// be taken away again (StopListening) before the level that made them ends.
constexpr int kBuildingDestroyedEvent = 2;  // {type, the building's handle}: the same in both games
struct GameEvent {
    int type;
    unsigned values[4];
};
using ListenerFn = int(__cdecl*)(const GameEvent* event, void* notify);
void* Listen(int eventType, ListenerFn callback, void* data);
void StopListening(void* listener);

// Game handles of the kinds Armageddon's script reader doesn't know: the mod reads them itself.
// Each kind's lookup turns a name from a script into its value (kNoHandle if unknown).
using HandleLookup = unsigned (*)(const char* name);
void AddHandleKind(const char* tag, HandleLookup lookup);

// Hooks the script reader (once). False if the game's code couldn't be hooked.
bool HookScriptReader();

}  // namespace campaign::script
