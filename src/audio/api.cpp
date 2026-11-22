// The flat C interface (rfa_audio.h): each call takes the engine's lock; the end callback is called outside it.
#define RFA_AUDIO_BUILD_DLL
#include "rfa_audio.h"

#include "engine.h"

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

// Reports the ends taken out under the lock, outside it.
void Report(std::vector<EndNote> ends, rfa_audio_end_callback callback, void* user)
{
    if (!callback) return;
    for (const EndNote& e : ends) callback(e.playingId, e.eventId, e.object, e.voices, user);
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
    for (uint32_t id : bank->nodes)
        if (const Node* n = E().library.FindNode(id))
            for (const Curve& c : n->curves)
                if (seen.insert(c.parameter).second) items.push_back(c.parameter);
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
    for (uint32_t id : bank->nodes)
        if (const Node* n = E().library.FindNode(id))
            for (const Curve& c : n->curves)
                if (c.parameter == parameter && !c.points.empty()) {
                    if (!any) low = high = c.points.front().x;
                    any = true;
                    for (const CurvePoint& p : c.points) {
                        low = std::min(low, p.x);
                        high = std::max(high, p.x);
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

int rfa_audio_is_playing(uint32_t playing_id)
{
    std::lock_guard<std::mutex> hold(E().lock);
    return E().IsPlaying(playing_id) ? 1 : 0;
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
    rfa_audio_end_callback callback;
    void* user;
    {
        std::lock_guard<std::mutex> hold(E().lock);
        E().Update();
        ends = E().TakeEnds();
        callback = E().endCallback;
        user = E().endUser;
    }
    Report(std::move(ends), callback, user);
}

int rfa_audio_render(float* out, int frames)
{
    std::vector<EndNote> ends;
    rfa_audio_end_callback callback;
    void* user;
    int rendered;
    {
        std::lock_guard<std::mutex> hold(E().lock);
        rendered = E().Render(out, frames);
        ends = E().TakeEnds();
        callback = E().endCallback;
        user = E().endUser;
    }
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
