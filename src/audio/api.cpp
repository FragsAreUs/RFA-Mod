// The flat C interface (rfa_audio.h): each call takes the engine's lock; the end callback is called outside it.
// Built into a program or mod of its own (RFA_AUDIO_STATIC: the game's rfa_sound.dll), nothing is exported.
#ifndef RFA_AUDIO_STATIC
#define RFA_AUDIO_BUILD_DLL
#endif
#include "rfa_audio.h"

#include "engine.h"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <set>

using namespace rfa_audio;

namespace {

Engine& E() { return TheEngine(); }

// Copies text into a caller's buffer (cut to fit, always ended). Returns the text's whole length.
int CopyText(const std::string& text, char* out, int size)
{
    if (out && size > 0) {
        size_t n = std::min(text.size(), static_cast<size_t>(size - 1));
        memcpy(out, text.data(), n);
        out[n] = 0;
    }
    return static_cast<int>(text.size());
}

// Reports the starts and the ends taken out under the lock, outside it.
void Report(std::vector<EndNote> ends, rfa_audio_end_callback callback, void* user)
{
    if (!callback) return;
    for (const EndNote& e : ends) callback(e.playingId, e.eventId, e.object, e.voices, user);
}

void ReportStarts(std::vector<StartNote> starts, rfa_audio_start_callback callback, void* user)
{
    if (!callback) return;
    for (const StartNote& s : starts) callback(s.playingId, s.bankId, s.mediumId, user);
}

template <typename Item>
int Fill(const std::vector<Item>& items, Item* out, int max)
{
    if (out)
        for (int i = 0; i < max && i < static_cast<int>(items.size()); i++) out[i] = items[i];
    return static_cast<int>(items.size());
}

}  // namespace

extern "C" {

int rfa_audio_open(const rfa_audio_settings* settings, char* error, int error_size)
{
    rfa_audio_settings defaults{};
    std::string why;
    bool ok = E().Open(settings ? *settings : defaults, why);
    CopyText(ok ? "" : why, error, error_size);
    return ok ? 1 : 0;
}

void rfa_audio_close(void) { E().Close(); }

void rfa_audio_set_library_folder(const wchar_t* folder)
{
    std::lock_guard<std::mutex> hold(E().lock);
    std::wstring f = folder ? folder : L"";
    if (!f.empty() && f.back() != L'\\' && f.back() != L'/') f += L'\\';
    E().al.folder = f;
}

int rfa_audio_device_info(char* text, int size)
{
    std::lock_guard<std::mutex> hold(E().lock);
    return CopyText(E().DeviceInfo(), text, size);
}

uint32_t rfa_audio_load_bank(const wchar_t* bank_path, const wchar_t* media_path, char* error, int error_size)
{
    std::lock_guard<std::mutex> hold(E().lock);
    std::string why;
    uint32_t id = bank_path ? E().LoadBank(bank_path, media_path ? media_path : L"", why) : 0;
    if (!bank_path) why = "no bank named";
    CopyText(id ? "" : why, error, error_size);
    return id;
}

uint32_t rfa_audio_load_bank_from_archive(const wchar_t* archive, const char* bank_name, char* error, int error_size)
{
    std::lock_guard<std::mutex> hold(E().lock);
    std::string why = "no archive or bank named";
    uint32_t id = archive && bank_name ? E().LoadBankFromArchive(archive, bank_name, why) : 0;
    CopyText(id ? "" : why, error, error_size);
    return id;
}

uint32_t rfa_audio_load_game_bank(const wchar_t* archive, const wchar_t* const* folders, int folder_count, const char* bank_name,
                                  char* error, int error_size)
{
    if (!archive || !bank_name) {
        CopyText("no archive or bank named", error, error_size);
        return 0;
    }
    // Its overrides, looked for before the engine's lock is taken.
    std::wstring wideName;
    for (const char* c = bank_name; *c; c++) wideName += static_cast<wchar_t>(static_cast<unsigned char>(*c));
    std::wstring looseBank, looseMedia;
    for (int i = 0; folders && i < folder_count; i++) {
        if (!folders[i]) continue;
        std::wstring bank = std::wstring(folders[i]) + wideName + L".bnk_pc", media = std::wstring(folders[i]) + wideName + L"_media.bnk_pc";
        if (looseBank.empty() && GetFileAttributesW(bank.c_str()) != INVALID_FILE_ATTRIBUTES) looseBank = bank;
        if (looseMedia.empty() && GetFileAttributesW(media.c_str()) != INVALID_FILE_ATTRIBUTES) looseMedia = media;
    }
    std::lock_guard<std::mutex> hold(E().lock);
    std::string why;
    uint32_t id = E().LoadBankFromArchive(archive, bank_name, why, looseBank, looseMedia);
    CopyText(id ? "" : why, error, error_size);
    return id;
}

void rfa_audio_stop_bank(uint32_t bank_id, int fade_ms)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().StopBank(bank_id, fade_ms);
}

namespace {

// The ids of a library's banks that Wwise made from names, by kind (rfa_audio_named_ids).
int NamedIds(const Library& lib, uint32_t* ids, int* kinds, int max)
{
    std::set<std::pair<int, uint32_t>> found;
    auto put = [&](int kind, uint32_t id) {
        if (id) found.insert({kind, id});
    };
    auto curves = [&](const std::vector<Curve>& list) {
        for (const Curve& c : list) put(RFA_AUDIO_NAMED_PARAMETER, c.parameter);
    };
    for (auto& [bankId, bank] : lib.Banks()) {
        put(RFA_AUDIO_NAMED_BANK, bankId);
        for (uint32_t e : bank->events) put(RFA_AUDIO_NAMED_EVENT, e);
        for (uint32_t id : bank->dialogueEvents) put(RFA_AUDIO_NAMED_DIALOGUE, id);
        for (auto& [id, type] : bank->unread)
            if (type == 15) put(RFA_AUDIO_NAMED_DIALOGUE, id);  // (one not read as laid out)
        for (uint32_t b : bank->buses) {
            put(RFA_AUDIO_NAMED_BUS, b);
            if (const Bus* bus = lib.FindBus(b)) {
                put(RFA_AUDIO_NAMED_STATE_GROUP, bus->stateGroup);
                curves(bus->curves);
            }
        }
        for (uint32_t id : bank->nodes) {
            const Node* n = lib.FindNode(id);
            if (!n) continue;
            curves(n->curves);
            for (const Node::Layer& l : n->layerList) {
                curves(l.curves);
                put(RFA_AUDIO_NAMED_PARAMETER, l.crossfadeParameter);
            }
            put(RFA_AUDIO_NAMED_STATE_GROUP, n->stateGroup);
            if (n->type != TypeSwitch) continue;
            put(n->onState ? RFA_AUDIO_NAMED_STATE_GROUP : RFA_AUDIO_NAMED_SWITCH_GROUP, n->group);
            for (const auto& [value, children] : n->choices) put(n->onState ? RFA_AUDIO_NAMED_STATE : RFA_AUDIO_NAMED_SWITCH, value);
        }
        for (uint32_t id : bank->actions) {
            const Action* a = lib.FindAction(id);
            if (!a) continue;
            if (a->Kind() == ActionSetState) {
                put(RFA_AUDIO_NAMED_STATE_GROUP, a->group);
                put(RFA_AUDIO_NAMED_STATE, a->groupValue);
            } else if (a->Kind() == ActionSetSwitch) {
                put(RFA_AUDIO_NAMED_SWITCH_GROUP, a->group);
                put(RFA_AUDIO_NAMED_SWITCH, a->groupValue);
            }
        }
        for (uint32_t id : bank->attenuations)
            if (const Attenuation* at = lib.FindAttenuation(id)) curves(at->curves);
    }
    for (auto& [id, group] : lib.StateGroups()) {
        put(RFA_AUDIO_NAMED_STATE_GROUP, id);
        for (auto& [state, values] : group.states) put(RFA_AUDIO_NAMED_STATE, state);
    }
    for (auto& [id, value] : lib.ParameterDefaults()) put(RFA_AUDIO_NAMED_PARAMETER, id);
    int n = 0;
    for (auto& [kind, id] : found) {
        if (n < max) {
            if (ids) ids[n] = id;
            if (kinds) kinds[n] = kind;
        }
        n++;
    }
    return n;
}

}  // namespace

int rfa_audio_named_ids(uint32_t* ids, int* kinds, int max)
{
    std::lock_guard<std::mutex> hold(E().lock);
    return NamedIds(E().library, ids, kinds, max);
}

void rfa_audio_set_obstruction(uint64_t object, float obstruction, float occlusion)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().SetObstruction(object, obstruction, occlusion);
}

void rfa_audio_set_environments(uint64_t object, const uint32_t* environments, const float* values, int count)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().SetEnvironments(object, environments, values, count);
}

void rfa_audio_set_doppler(float factor)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().SetDoppler(factor);
}

void rfa_audio_set_listener_ray(int direction, int hits, const float* distances, const int* materials, int blocked)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().SetListenerRay(direction, hits, distances, materials, blocked != 0);
}

void rfa_audio_set_occlusion_model(int model)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().SetOcclusionModel(model);
}

int rfa_audio_occlusion_status(float* most, float* enclosure, int* reverb_type)
{
    std::lock_guard<std::mutex> hold(E().lock);
    float m = 0, e = 0;
    int r = 0;
    int voices = E().OcclusionStatus(m, e, r);
    if (most) *most = m;
    if (enclosure) *enclosure = e;
    if (reverb_type) *reverb_type = r;
    return voices;
}

int rfa_audio_event_reach(uint32_t event_id)
{
    std::lock_guard<std::mutex> hold(E().lock);
    return E().EventReach(event_id);
}

int rfa_audio_event_routes(const uint32_t* banks_here, int count, uint32_t* events, int* routes, int max)
{
    std::unordered_set<uint32_t> here;
    for (int i = 0; banks_here && i < count; i++) here.insert(banks_here[i]);
    std::lock_guard<std::mutex> hold(E().lock);
    // The banks played here first: an event in several banks (Armageddon's have some) goes by the first that holds it.
    std::vector<const Bank*> banks;
    for (auto& [id, bank] : E().library.Banks()) banks.push_back(bank.get());
    std::stable_partition(banks.begin(), banks.end(), [&](const Bank* b) { return here.count(b->id) != 0; });
    std::unordered_set<uint32_t> seen;
    int n = 0;
    for (const Bank* bank : banks)
        for (uint32_t e : bank->events) {
            if (!seen.insert(e).second) continue;
            if (n < max) {
                if (events) events[n] = e;
                if (routes) routes[n] = E().EventRoute(e, bank->id, here);
            }
            n++;
        }
    return n;
}

void rfa_audio_unload_bank(uint32_t bank_id)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().UnloadBank(bank_id);
}

int rfa_audio_bank_events(uint32_t bank_id, uint32_t* ids, int max)
{
    std::lock_guard<std::mutex> hold(E().lock);
    Bank* bank = E().library.FindBank(bank_id);
    return bank ? Fill(bank->events, ids, max) : 0;
}

int rfa_audio_bank_objects(uint32_t bank_id, rfa_audio_object_info* out, int max)
{
    std::lock_guard<std::mutex> hold(E().lock);
    Bank* bank = E().library.FindBank(bank_id);
    if (!bank) return 0;
    std::vector<rfa_audio_object_info> items;
    for (uint32_t id : bank->nodes) {
        const Node* n = E().library.FindNode(id);
        if (!n) continue;
        rfa_audio_object_info o{};
        o.id = id;
        o.type = n->type;
        o.parent = n->parent;
        o.children = static_cast<int>(n->children.size());
        o.bus = n->bus;
        if (n->IsSound()) {
            o.medium = n->source;
            o.codec = n->IsAdpcm() ? 2 : (n->codec & 0xFFFF0000u) == 0x00010000u ? 1 : static_cast<int>(n->codec & 0xFF);
            o.loops = n->loopCount;
            o.has_medium = bank->media.count(n->source) ? 1 : 0;
        } else if (n->type == TypeRanSeq) {
            o.loops = n->containerLoops;
        }
        items.push_back(o);
    }
    for (uint32_t id : bank->actions) {
        rfa_audio_object_info o{};
        o.id = id;
        o.type = TypeAction;
        items.push_back(o);
    }
    for (uint32_t id : bank->events) {
        rfa_audio_object_info o{};
        o.id = id;
        o.type = TypeEvent;
        if (const Event* e = E().library.FindEvent(id)) o.children = static_cast<int>(e->actions.size());
        items.push_back(o);
    }
    for (uint32_t id : bank->buses) {
        rfa_audio_object_info o{};
        o.id = id;
        o.type = TypeBus;
        if (const Bus* b = E().library.FindBus(id)) o.parent = b->parent;
        items.push_back(o);
    }
    return Fill(items, out, max);
}

int rfa_audio_bank_parameters(uint32_t bank_id, uint32_t* ids, int max)
{
    std::lock_guard<std::mutex> hold(E().lock);
    Bank* bank = E().library.FindBank(bank_id);
    if (!bank) return 0;
    std::set<uint32_t> seen;
    std::vector<uint32_t> items;
    auto add = [&](uint32_t parameter) {
        if (parameter && seen.insert(parameter).second) items.push_back(parameter);
    };
    for (uint32_t id : bank->nodes)
        if (const Node* n = E().library.FindNode(id)) {
            for (const Curve& c : n->curves) add(c.parameter);
            for (const Node::Layer& l : n->layerList) {  // a layer's own curves, and its crossfade
                for (const Curve& c : l.curves) add(c.parameter);
                if (!l.children.empty()) add(l.crossfadeParameter);
            }
        }
    return Fill(items, ids, max);
}

int rfa_audio_bank_switch_groups(uint32_t bank_id, uint32_t* groups, int max)
{
    std::lock_guard<std::mutex> hold(E().lock);
    Bank* bank = E().library.FindBank(bank_id);
    if (!bank) return 0;
    std::set<uint32_t> seen;
    std::vector<uint32_t> items;
    for (uint32_t id : bank->nodes)
        if (const Node* n = E().library.FindNode(id))
            if (n->type == TypeSwitch && !n->onState && seen.insert(n->group).second) items.push_back(n->group);
    return Fill(items, groups, max);
}

int rfa_audio_bank_switch_values(uint32_t bank_id, uint32_t group, uint32_t* values, int max)
{
    std::lock_guard<std::mutex> hold(E().lock);
    Bank* bank = E().library.FindBank(bank_id);
    if (!bank) return 0;
    std::set<uint32_t> seen;
    std::vector<uint32_t> items;
    for (uint32_t id : bank->nodes)
        if (const Node* n = E().library.FindNode(id))
            if (n->type == TypeSwitch && n->group == group)
                for (const auto& [value, children] : n->choices)
                    if (seen.insert(value).second) items.push_back(value);
    return Fill(items, values, max);
}

int rfa_audio_parameter_range(uint32_t bank_id, uint32_t parameter, float* min, float* max)
{
    std::lock_guard<std::mutex> hold(E().lock);
    Bank* bank = E().library.FindBank(bank_id);
    if (!bank) return 0;
    bool any = false;
    float low = 0, high = 0;
    auto take = [&](const std::vector<CurvePoint>& points) {
        for (const CurvePoint& p : points) {
            if (!any) low = high = p.x;
            any = true;
            low = std::min(low, p.x);
            high = std::max(high, p.x);
        }
    };
    for (uint32_t id : bank->nodes)
        if (const Node* n = E().library.FindNode(id)) {
            for (const Curve& c : n->curves)
                if (c.parameter == parameter) take(c.points);
            for (const Node::Layer& l : n->layerList) {
                for (const Curve& c : l.curves)
                    if (c.parameter == parameter) take(c.points);
                if (l.crossfadeParameter == parameter)
                    for (const auto& [child, points] : l.children) take(points);
            }
        }
    if (min) *min = low;
    if (max) *max = high;
    return any ? 1 : 0;
}

int rfa_audio_bank_problems(uint32_t bank_id, char* text, int size)
{
    std::lock_guard<std::mutex> hold(E().lock);
    Bank* bank = E().library.FindBank(bank_id);
    std::string all;
    if (bank)
        for (const std::string& p : bank->problems) all += p + "\n";
    return CopyText(all, text, size);
}

int rfa_audio_event_actions(uint32_t event_id, rfa_audio_action_info* out, int max)
{
    std::lock_guard<std::mutex> hold(E().lock);
    const Event* event = E().library.FindEvent(event_id);
    if (!event) return 0;
    std::vector<rfa_audio_action_info> items;
    for (uint32_t id : event->actions)
        if (const Action* a = E().library.FindAction(id)) {
            rfa_audio_action_info o{};
            o.id = id;
            o.kind = a->Kind();
            o.scope = a->Scope();
            o.target = a->target;
            o.delay_ms = a->delayMs;
            o.fade_ms = a->fadeMs;
            items.push_back(o);
        }
    return Fill(items, out, max);
}

int rfa_audio_get_event_info(uint32_t event_id, rfa_audio_event_info* out)
{
    std::lock_guard<std::mutex> hold(E().lock);
    rfa_audio_event_info info{};
    E().EventInfo(event_id, info);
    if (out) *out = info;
    return info.found;
}

int rfa_audio_describe(uint32_t id, char* text, int size)
{
    std::lock_guard<std::mutex> hold(E().lock);
    return CopyText(E().Describe(id), text, size);
}

float rfa_audio_bus_volume(uint32_t bus_id)
{
    std::lock_guard<std::mutex> hold(E().lock);
    return E().library.BusVolume(bus_id);
}

uint32_t rfa_audio_post(uint32_t event_id, uint64_t object)
{
    std::lock_guard<std::mutex> hold(E().lock);
    return E().Post(event_id, object);
}

uint32_t rfa_audio_post_controls(uint32_t event_id, uint64_t object)
{
    std::lock_guard<std::mutex> hold(E().lock);
    return E().Post(event_id, object, true);
}

uint32_t rfa_audio_play_node(uint32_t node_id, uint64_t object)
{
    std::lock_guard<std::mutex> hold(E().lock);
    return E().PlayNode(node_id, object);
}

uint32_t rfa_audio_play_medium(uint32_t bank_id, uint32_t medium_id, int loop, uint64_t object)
{
    std::lock_guard<std::mutex> hold(E().lock);
    return E().PlayMedium(bank_id, medium_id, loop != 0, object);
}

uint32_t rfa_audio_play_samples(const int16_t* samples, int frames, int channels, int rate, int loop, int loop_start, float volume_db,
                                float pitch_cents, int three_d, uint64_t object)
{
    if (!samples || frames <= 0 || channels <= 0 || rate <= 0) return 0;
    std::lock_guard<std::mutex> hold(E().lock);
    return E().PlaySamples(samples, frames, channels, rate, loop != 0, loop_start, volume_db, pitch_cents, three_d != 0, object);
}

void rfa_audio_stop_playing(uint32_t playing_id, int fade_ms)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().StopPlaying(playing_id, fade_ms);
}

void rfa_audio_stop_object(uint64_t object, int fade_ms)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().StopObject(object, fade_ms);
}

void rfa_audio_pause_playing(uint32_t playing_id, int pause)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().PausePlay(playing_id, pause != 0);
}

int rfa_audio_node_ducking_buses(uint32_t node_id, uint32_t* buses, int max)
{
    std::lock_guard<std::mutex> hold(E().lock);
    std::vector<uint32_t> found = E().NodeDuckingBuses(node_id);
    for (int i = 0; i < max && i < static_cast<int>(found.size()); i++) buses[i] = found[i];
    return static_cast<int>(found.size());
}

int rfa_audio_event_ducking_buses(uint32_t event_id, uint32_t* buses, int max)
{
    std::lock_guard<std::mutex> hold(E().lock);
    std::vector<uint32_t> found = E().EventDuckingBuses(event_id);
    for (int i = 0; i < max && i < static_cast<int>(found.size()); i++) buses[i] = found[i];
    return static_cast<int>(found.size());
}

int rfa_audio_load_archive_banks(const wchar_t* archive, char* error, int error_size)
{
    std::vector<std::string> names;
    std::string why;
    if (!archive || !ListArchive(archive, names, why)) {
        CopyText(archive ? why : "no archive named", error, error_size);
        return -1;
    }
    int loaded = 0;
    for (const std::string& n : names) {
        if (n.size() < 7 || n.compare(n.size() - 7, 7, ".bnk_pc") != 0 || n.find("_media.") != std::string::npos) continue;
        std::lock_guard<std::mutex> hold(E().lock);
        std::string problem;
        if (E().LoadBankFromArchive(archive, n.substr(0, n.size() - 7), problem, L"", L"", true)) loaded++;
        else why = n + ": " + problem;
    }
    CopyText(loaded ? "" : why, error, error_size);
    return loaded;
}

int rfa_audio_archive_bank_names(const wchar_t* archive, char* text, int size)
{
    std::vector<std::string> names;
    std::string why;
    if (!archive || !ListArchive(archive, names, why)) {
        if (text && size > 0) text[0] = 0;
        return -1;
    }
    std::string list;
    for (const std::string& n : names)
        if (n.size() > 7 && n.compare(n.size() - 7, 7, ".bnk_pc") == 0 && n.find("_media.") == std::string::npos)
            list += n.substr(0, n.size() - 7) + "\n";
    CopyText(list, text, size);
    return static_cast<int>(list.size());
}

void rfa_audio_note_bus(uint32_t bus, int delta)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().NoteBus(bus, delta);
}

void rfa_audio_pause_all(int pause)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().PauseAll(pause != 0);
}

int rfa_audio_is_playing(uint32_t playing_id)
{
    std::lock_guard<std::mutex> hold(E().lock);
    return E().IsPlaying(playing_id) ? 1 : 0;
}

void rfa_audio_set_log_callback(rfa_audio_log_callback callback, void* user, int most)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().logCallback = callback;
    E().logUser = user;
    E().quietLogsLeft = most;
    E().quietLogged.clear();
}

void rfa_audio_set_start_callback(rfa_audio_start_callback callback, void* user)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().startCallback = callback;
    E().startUser = user;
}

int rfa_audio_medium_prefix(uint32_t bank_id, uint32_t medium_id, uint8_t* out, int max)
{
    std::lock_guard<std::mutex> hold(E().lock);
    return E().MediumPrefix(bank_id, medium_id, out, max);
}

int rfa_audio_bank_dialogue_events(uint32_t bank_id, uint32_t* ids, int max)
{
    std::lock_guard<std::mutex> hold(E().lock);
    Bank* bank = E().library.FindBank(bank_id);
    return bank ? Fill(bank->dialogueEvents, ids, max) : 0;
}

uint32_t rfa_audio_resolve_dialogue(uint32_t dialogue_event, const uint32_t* values, int count)
{
    std::lock_guard<std::mutex> hold(E().lock);
    const DialogueEvent* d = E().library.FindDialogueEvent(dialogue_event);
    return d && values && count > 0 ? d->Resolve(values, static_cast<size_t>(count)) : 0;
}

int rfa_audio_dialogue_lines(uint32_t dialogue_event, int* arguments, uint32_t* keys, uint32_t* nodes, int max)
{
    std::lock_guard<std::mutex> hold(E().lock);
    const DialogueEvent* d = E().library.FindDialogueEvent(dialogue_event);
    if (!d) return 0;
    const size_t args = d->arguments.size();
    if (arguments) *arguments = static_cast<int>(args);
    int n = 0;
    for (const auto& [path, node] : d->paths) {
        if (n < max) {
            for (size_t k = 0; keys && k < args && k < path.size(); k++) keys[static_cast<size_t>(n) * args + k] = path[k];
            if (nodes) nodes[n] = node;
        }
        n++;
    }
    return n;
}

void rfa_audio_set_end_callback(rfa_audio_end_callback callback, void* user)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().endCallback = callback;
    E().endUser = user;
}

void rfa_audio_set_position(uint64_t object, const float position[3], const float front[3])
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().SetPosition(object, position, front);
}

void rfa_audio_set_listener(const float position[3], const float front[3], const float up[3])
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().SetListener(position, front, up);
}

void rfa_audio_set_parameter(uint64_t object, uint32_t parameter, float value)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().SetParameter(object, parameter, value);
}

void rfa_audio_reset_parameter(uint64_t object, uint32_t parameter)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().ResetParameter(object, parameter);
}

void rfa_audio_set_switch(uint64_t object, uint32_t group, uint32_t value)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().SetSwitch(object, group, value);
}

void rfa_audio_set_state(uint32_t group, uint32_t state)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().SetState(group, state);
}

void rfa_audio_remove_object(uint64_t object)
{
    std::lock_guard<std::mutex> hold(E().lock);
    E().RemoveObject(object);
}

void rfa_audio_tick(void)
{
    std::vector<EndNote> ends;
    std::vector<StartNote> starts;
    rfa_audio_end_callback callback;
    rfa_audio_start_callback startCallback;
    void* user;
    void* startUser;
    {
        std::lock_guard<std::mutex> hold(E().lock);
        E().Update();
        ends = E().TakeEnds();
        callback = E().endCallback;
        user = E().endUser;
        starts = E().TakeStarts();
        startCallback = E().startCallback;
        startUser = E().startUser;
    }
    ReportStarts(std::move(starts), startCallback, startUser);
    Report(std::move(ends), callback, user);
}

int rfa_audio_render(float* out, int frames)
{
    std::vector<EndNote> ends;
    std::vector<StartNote> starts;
    rfa_audio_end_callback callback;
    rfa_audio_start_callback startCallback;
    void* user;
    void* startUser;
    int rendered;
    {
        std::lock_guard<std::mutex> hold(E().lock);
        rendered = E().Render(out, frames);
        ends = E().TakeEnds();
        callback = E().endCallback;
        user = E().endUser;
        starts = E().TakeStarts();
        startCallback = E().startCallback;
        startUser = E().startUser;
    }
    ReportStarts(std::move(starts), startCallback, startUser);
    Report(std::move(ends), callback, user);
    return rendered;
}

double rfa_audio_time(void)
{
    std::lock_guard<std::mutex> hold(E().lock);
    return E().Now();
}

int rfa_audio_voices(rfa_audio_voice* out, int max)
{
    std::lock_guard<std::mutex> hold(E().lock);
    return E().Voices(out, max);
}

int rfa_audio_get_status(rfa_audio_status* out)
{
    std::lock_guard<std::mutex> hold(E().lock);
    rfa_audio_status status{};
    E().Status(status);
    if (out) *out = status;
    return 1;
}

uint32_t rfa_audio_id(const char* name) { return name ? WwiseId(name) : 0; }

}  // extern "C"
