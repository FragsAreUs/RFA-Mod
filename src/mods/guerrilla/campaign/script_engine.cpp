// Armageddon's action-script engine, and how the mod adds Guerrilla's words to it.
//
// Armageddon still has Guerrilla's script engine (.\scripting\action_script\*.cpp,
// managed_script_group.cpp): each zone it loads reads the zone's scripts, "<zone>*.scriptx" from the
// world's zonescript_<world>.vpp_pc (0x6CC3B0, each file by 0x9164C0). A script names its words
// (actions, events, functions), and the reader looks each up by name in one of three dictionaries:
// singly linked lists of entries {next, definition, word}, one list each, whose heads the game gets
// from 0x9194C0 (actions), 0x919560 (events) and 0x919520 (functions). Every word's entry is a
// static object the game links in at start-up with the entry's constructor (thiscall: entry, word,
// definition): 0x9198C0 for actions, 0x9196A0 for events, 0x919640 for functions. The definition
// gives the callback and the argument list ({five types, count}); when the script is read, each
// argument is checked against the list (0x927B10: the same type, a flag where a number is wanted,
// or a variable where an object or a timer is wanted).
//
// The mod adds a word the same way: its own entry and definition, linked in with the game's own
// constructor, before the first script is read (the mod starts before the game's static objects
// are made, so it waits for the first script). A word Armageddon kept as an empty function (49
// actions, 4 functions), or whose arguments it changed (4 actions and 2 events that take a string
// where Guerrilla's scripts give a voice line), keeps its entry and gets Guerrilla's callback and
// argument list put back into its definition.
//
// Arguments are read through the node's own conversions, its virtual functions: in Armageddon's
// table one slot earlier than in Guerrilla's (Guerrilla's script_node has one more function before
// them). Found in Armageddon's own callbacks: set_vehicle_hitpoints (0x91DE90: object +0x24, number
// as int +0x20), add_time_to_timer (0x91DC80: number +0x1C), debug_console_command (0x91C5A0:
// string +0x14), effect_spawn_at_script_point (0x91D1F0: point +0x30 by hidden result, game
// handle +0x2C, flag +0x18), objective_highlight_remove_list (0x91CB90: object list +0x28, ending
// with 0xFFFFFFFF).
//
// Game handles: the reader (script_node_parse, 0x926750) turns each argument element into a node
// by its tag: a number, a string (also <message>, <tool_tip>, <district>), a flag, an object, ...,
// and game handles of known kinds (<voice_line>, <team>, <vehicle_type>, <mission_objective>, ...),
// each looked up by its system. Guerrilla's reader knows more kinds than Armageddon's, for systems
// Armageddon dropped: <alert_level>, <air_bomb>, <convoy>, <raid_state>, <activity_state>,
// <pa_group>, <music_threshold>, <upgrades>, <vrail_type>, <cont_atttack_type>, <despawn_type>, and
// <destroyed_message> (a string). For a tag it doesn't know Armageddon's reader gives no node, and
// the script fails to load. The mod wraps the reader: tags of kinds the mod knows become game-handle
// nodes it makes itself, as the reader makes its own (from the node pool at 0x253E5BC, 0x928110,
// and the node's constructor for a game handle, 0x927930, which marks it type 23); a
// <destroyed_message> is read as the <message> it is.
//
// Events: the game's event manager (gem_post_event 0x918720, gem_register_for_event 0x9184F0) keeps
// its listeners in a pool with one list per event type: 92 lists (the pool at 0x2527C30; its lists
// at 0x2527C50 + 4 × type run to the pool's items at 0x2527DC0). An <event> in a script listens
// (script_event_evaluate, 0x923E30) for its definition's event type and asks the definition's
// callback whether an event matches the script's arguments. The game's own event types run to 89
// (each event class gives its type from a small function; none gives 90 or 91), so the mod's
// events use 91, and say which of Guerrilla's they are inside.
//
// An action's result (spawn_squad's squad, an air strike's handle, ...) goes to the script's
// <variable> through the game's action result, set with 0x927F10 (Guerrilla's
// scripting_set_action_return_value).
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "script_engine.h"
#include <stdlib.h>
#include <string.h>
#include <array>
#include <bitset>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace campaign::script {
namespace {

// ---- The game

// The three dictionaries: each function gives the dictionary, whose first field is the head of its
// list of entries.
struct Entry {
    Entry* next;
    void* definition;
    const char* word;
};
using DictionaryFn = Entry** (__cdecl*)();
auto const ActionDictionary = reinterpret_cast<DictionaryFn>(0x9194C0);
auto const EventDictionary = reinterpret_cast<DictionaryFn>(0x919560);
auto const FunctionDictionary = reinterpret_cast<DictionaryFn>(0x919520);

// An entry's constructor (thiscall: the entry, its word, its definition) fills it in and links it
// at the head of its dictionary.
using EntryConstructorFn = Entry*(__fastcall*)(Entry* entry, void* unused, const char* word, void* definition);
auto const MakeActionEntry = reinterpret_cast<EntryConstructorFn>(0x9198C0);
auto const MakeEventEntry = reinterpret_cast<EntryConstructorFn>(0x9196A0);
auto const MakeFunctionEntry = reinterpret_cast<EntryConstructorFn>(0x919640);

// The node's conversions: byte offsets into its table of virtual functions.
constexpr int kAsString = 0x14, kAsBool = 0x18, kAsNumber = 0x1C, kAsInt = 0x20, kAsObject = 0x24,
              kAsObjectList = 0x28, kAsHandle = 0x2C, kAsPoint = 0x30;
template <typename Fn>
Fn Conversion(Node* node, int offset) {
    return reinterpret_cast<Fn>(node->vtable[offset / 4]);
}

// The action result: 0x927F10(value).
using SetResultFn = void(__cdecl*)(unsigned value);
auto const SetResult = reinterpret_cast<SetResultFn>(0x927F10);

// The event manager: gem_post_event(event) tells every listener of the event's type;
// gem_register_for_event(notify) adds a listener (a copy of the notify, which it gives back),
// gem_unregister_for_event(that copy) takes it away.
using PostEventFn = void(__cdecl*)(EventData* event);
auto const PostEvent = reinterpret_cast<PostEventFn>(0x918720);
struct Notify {
    int eventType;
    ListenerFn callback;
    void* data;
};
using RegisterFn = Notify*(__cdecl*)(const Notify* notify);
using UnregisterFn = void(__cdecl*)(Notify* notify);
auto const Register = reinterpret_cast<RegisterFn>(0x9184F0);
auto const Unregister = reinterpret_cast<UnregisterFn>(0x9185C0);

// The script reader: one argument element of a script into a node (cdecl: the element, the script
// being read), null if it can't.
struct XmlElement {
    const char* name;  // its tag
    const void* links[2];
    const char* text;  // what it holds, or null (as the game's xml_get_string, 0xC8F940, reads it)
};
constexpr DWORD kParseNode = 0x926750;
constexpr size_t kParseNodePrologue = 9;  // push ebp / mov ebp,esp / sub esp,148h
using ParseNodeFn = Node*(__cdecl*)(XmlElement* element, void* script);
ParseNodeFn g_parseNode;

// Reading one .scriptx file (cdecl: its name): the mod puts its words in before the first.
constexpr DWORD kLoadScript = 0x9164C0;
constexpr size_t kLoadScriptPrologue = 6;  // push ebp / mov ebp,esp / sub esp,3Ch
using LoadScriptFn = int(__cdecl*)(const char* file);
LoadScriptFn g_loadScript;

// Making a game-handle node as the reader does: a node from its pool (the pool's alloc, thiscall,
// gives the memory), then the game handle's constructor (thiscall: the node, the value).
auto const g_nodePool = reinterpret_cast<void*>(0x253E5BC);
using PoolAllocFn = void*(__fastcall*)(void* pool, void* unused, int category);
using HandleNodeFn = Node*(__fastcall*)(void* memory, void* unused, unsigned value);
auto const AllocNode = reinterpret_cast<PoolAllocFn>(0x928110);
auto const MakeHandleNode = reinterpret_cast<HandleNodeFn>(0x927930);

// ---- The mod

struct HandleKind {
    std::string tag;
    HandleLookup lookup;
};
std::vector<HandleKind> g_handleKinds;

// The words waiting to go in, and the mod's own entries and definitions (they must stay put: the
// game keeps pointers to them).
const Word* g_words;
size_t g_wordCount;
std::once_flag g_installed;  // (zones, and so scripts, may load on a streaming thread)
std::deque<Entry> g_entries;
std::deque<ActionDefinition> g_actions;
std::deque<EventDefinition> g_events;
std::deque<FunctionDefinition> g_functions;
std::deque<ArgList> g_argLists;

Entry* Find(Entry* head, const char* word) {
    for (Entry* entry = head; entry; entry = entry->next)
        if (entry->word && _stricmp(entry->word, word) == 0) return entry;
    return nullptr;
}

// A word the mod doesn't do yet still has to be there, or every script naming it fails to load.
// Its callback says once, in the log, that a script asked for it; one small function per word, so
// the log can name it.
constexpr size_t kMaxWords = 256;
std::bitset<kMaxWords> g_reported;
void ReportNotPorted(size_t index) {
    if (index >= g_wordCount || g_reported[index]) return;
    g_reported[index] = true;
    rfa::Log("A script used %s, which isn't ported yet", g_words[index].name);
}
template <size_t Index>
void __cdecl NotPortedAction(Args) {
    ReportNotPorted(Index);
    if (Index < g_wordCount && g_words[Index].result != Void) SetResult(kNoObject);
}
template <size_t Index>
void __cdecl NotPortedFunction(Variant& value, Variant&, Args) {
    ReportNotPorted(Index);
    value = {};
    if (Index < g_wordCount && (g_words[Index].result == Object || g_words[Index].result == Handle)) value.object = kNoObject;
}
int __cdecl NotPortedEvent(const EventData*, Args) { return kEventIgnored; }
template <size_t... Index>
constexpr auto NotPortedActions(std::index_sequence<Index...>) {
    return std::array<ActionFn, sizeof...(Index)>{&NotPortedAction<Index>...};
}
template <size_t... Index>
constexpr auto NotPortedFunctions(std::index_sequence<Index...>) {
    return std::array<FunctionFn, sizeof...(Index)>{&NotPortedFunction<Index>...};
}
const auto kNotPortedActions = NotPortedActions(std::make_index_sequence<kMaxWords>());
const auto kNotPortedFunctions = NotPortedFunctions(std::make_index_sequence<kMaxWords>());

const ArgList* KeepArgs(const ArgList& args) {
    g_argLists.push_back(args);
    return &g_argLists.back();
}

bool SameArgs(const ArgList* a, const ArgList& b) {
    if (!a || a->count != b.count) return false;
    for (int i = 0; i < abs(b.count) && i < 5; ++i)
        if (a->types[i] != b.types[i]) return false;
    return true;
}

void InstallWords() {
    int added = 0, restored = 0, argsFixed = 0;
    for (size_t i = 0; i < g_wordCount; ++i) {
        const Word& word = g_words[i];
        if (word.kind == Kind::Action) {
            auto callback = word.callback ? reinterpret_cast<ActionFn>(word.callback) : kNotPortedActions[i];
            if (Entry* entry = Find(*ActionDictionary(), word.name)) {
                auto definition = static_cast<ActionDefinition*>(entry->definition);
                definition->callback = callback;
                definition->result = word.result;
                if (!SameArgs(definition->args, word.args)) {
                    definition->args = KeepArgs(word.args);
                    ++argsFixed;
                }
                ++restored;
            } else {
                g_actions.push_back({callback, KeepArgs(word.args), word.name, "guerrilla", word.result});
                g_entries.push_back({});
                MakeActionEntry(&g_entries.back(), nullptr, word.name, &g_actions.back());
                ++added;
            }
        } else if (word.kind == Kind::Function) {
            auto callback = word.callback ? reinterpret_cast<FunctionFn>(word.callback) : kNotPortedFunctions[i];
            if (Entry* entry = Find(*FunctionDictionary(), word.name)) {
                auto definition = static_cast<FunctionDefinition*>(entry->definition);
                definition->callback = callback;
                definition->result = word.result;
                if (!SameArgs(definition->args, word.args)) {
                    definition->args = KeepArgs(word.args);
                    ++argsFixed;
                }
                ++restored;
            } else {
                g_functions.push_back({callback, word.result, KeepArgs(word.args), word.name, "guerrilla"});
                g_entries.push_back({});
                MakeFunctionEntry(&g_entries.back(), nullptr, word.name, &g_functions.back());
                ++added;
            }
        } else {
            auto callback = word.callback ? reinterpret_cast<EventFn>(word.callback) : &NotPortedEvent;
            if (Entry* entry = Find(*EventDictionary(), word.name)) {
                // One Armageddon changed (the dialogue events take a string where Guerrilla's
                // scripts give a voice line): it becomes the mod's, told only of the mod's events.
                auto definition = static_cast<EventDefinition*>(entry->definition);
                definition->callback = callback;
                definition->eventType = kModEventType;
                definition->args = KeepArgs(word.args);
                ++restored;
                ++argsFixed;
                continue;
            }
            g_events.push_back({callback, kModEventType, KeepArgs(word.args), word.name});
            g_entries.push_back({});
            MakeEventEntry(&g_entries.back(), nullptr, word.name, &g_events.back());
            ++added;
        }
    }
    rfa::Log("Guerrilla's script words: %d added, %d put back to Guerrilla's (%d with Guerrilla's arguments)", added,
             restored, argsFixed);
}

int __cdecl LoadScript(const char* file) {
    std::call_once(g_installed, &InstallWords);
    return g_loadScript(file);
}

Node* __cdecl ParseNode(XmlElement* element, void* script) {
    if (element && element->name) {
        for (const HandleKind& kind : g_handleKinds) {
            if (_stricmp(element->name, kind.tag.c_str()) != 0) continue;
            const char* name = element->text ? element->text : "";
            const unsigned value = kind.lookup(name);
            if (value == kNoHandle) rfa::Log("Script: no %s named \"%s\"", kind.tag.c_str(), name);
            void* memory = AllocNode(g_nodePool, nullptr, 0);
            return memory ? MakeHandleNode(memory, nullptr, value) : nullptr;
        }
        // Guerrilla's <destroyed_message> is a string, as <message> is: the game's reader makes it
        // one when it is called that for the moment.
        if (_stricmp(element->name, "destroyed_message") == 0) {
            const char* tag = element->name;
            element->name = "message";
            Node* node = g_parseNode(element, script);
            element->name = tag;
            return node;
        }
    }
    return g_parseNode(element, script);
}

}  // namespace

const char* AsString(Node* node) { return Conversion<const char*(__thiscall*)(Node*)>(node, kAsString)(node); }
bool AsBool(Node* node) { return Conversion<bool(__thiscall*)(Node*)>(node, kAsBool)(node); }
float AsNumber(Node* node) { return Conversion<float(__thiscall*)(Node*)>(node, kAsNumber)(node); }
int AsInt(Node* node) { return Conversion<int(__thiscall*)(Node*)>(node, kAsInt)(node); }
unsigned AsObject(Node* node) { return Conversion<unsigned(__thiscall*)(Node*)>(node, kAsObject)(node); }
const unsigned* AsObjectList(Node* node) { return Conversion<const unsigned*(__thiscall*)(Node*)>(node, kAsObjectList)(node); }
unsigned AsHandle(Node* node) { return Conversion<unsigned(__thiscall*)(Node*)>(node, kAsHandle)(node); }
ScriptPoint AsPoint(Node* node) {
    // Returned through a hidden result pointer, after `this`.
    ScriptPoint point{kNoObject, 0};
    Conversion<ScriptPoint*(__thiscall*)(Node*, ScriptPoint*)>(node, kAsPoint)(node, &point);
    return point;
}

void SetActionResult(unsigned value) { SetResult(value); }

void Post(EventData& event) {
    if (!g_words) return;  // the words aren't in ([GuerrillaCampaign] Scripts=0): nothing listens
    event.type = kModEventType;
    event.mark = kModEventMark;
    PostEvent(&event);
}

void* Listen(int eventType, ListenerFn callback, void* data) {
    const Notify notify{eventType, callback, data};
    return Register(&notify);
}

void StopListening(void* listener) {
    if (listener) Unregister(static_cast<Notify*>(listener));
}

void AddHandleKind(const char* tag, HandleLookup lookup) { g_handleKinds.push_back({tag, lookup}); }

void Install(const Word* words, size_t count) {
    if (count > kMaxWords) {
        rfa::Log("Too many script words (%u); only the first %u go in", static_cast<unsigned>(count),
                 static_cast<unsigned>(kMaxWords));
        count = kMaxWords;
    }
    g_words = words;
    g_wordCount = count;
}

bool HookScriptReader() {
    g_loadScript = reinterpret_cast<LoadScriptFn>(rfa::HookFunction(kLoadScript, kLoadScriptPrologue, reinterpret_cast<void*>(&LoadScript)));
    if (!g_loadScript) return false;
    g_parseNode = reinterpret_cast<ParseNodeFn>(rfa::HookFunction(kParseNode, kParseNodePrologue, reinterpret_cast<void*>(&ParseNode)));
    return g_parseNode != nullptr;
}

}  // namespace campaign::script
