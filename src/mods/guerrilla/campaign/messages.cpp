// Guerrilla's HUD messages and tool tips from the campaign's systems, queued for the HUD.
//
// guerrilla_hud takes them one at a time, each frame, with GuerrillaCampaign_NextMessage. The tags and
// tool-tip names are Guerrilla's own (its le_strings and tool_tips.xtbl), so the HUD shows them as
// Guerrilla does. At most 32 wait; older ones are dropped when the HUD isn't taking them.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "campaign_api.h"
#include "messages.h"
#include <deque>
#include <mutex>

namespace campaign::messages {
namespace {

struct Message {
    const char* tag;   // static text: Guerrilla's string tag, or a tool tip's name
    const char* icon;  // static text, or null
    int kind;          // 0 a minor HUD message, 1 a tool tip, 2 a minor message with a number, 3 a primary message
    int ms;            // a tool tip's time on screen, or kind 2's number
};

std::deque<Message> g_queue;
std::mutex g_lock;  // the HUD may ask from its own thread
constexpr size_t kMost = 32;

void Add(const Message& message) {
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_queue.size() >= kMost) g_queue.pop_front();
    g_queue.push_back(message);
}

}  // namespace

void Minor(const char* tag, const char* icon) {
    if (tag) Add({tag, icon, 0, 0});
}

void MinorCount(const char* tag, int count) {
    if (tag) Add({tag, nullptr, 2, count});
}

void Tip(const char* name, int ms) {
    if (name) Add({name, nullptr, 1, ms});
}

void Primary(const char* tag) {
    if (tag) Add({tag, nullptr, 3, 0});
}

}  // namespace campaign::messages

// For guerrilla_hud: the next message, 1 if there was one. *tag is Guerrilla's string tag (a minor
// message) or tool tip name, *icon the picture shown with a message (null for none), *kind 0 for a
// minor message, 1 for a tool tip, 2 for a minor message whose text takes a number (%d): *ms is a tool
// tip's time on screen, or kind 2's number. The texts stay valid.
int GuerrillaCampaign_NextMessage(const char** tag, const char** icon, int* kind, int* ms) {
    using namespace campaign::messages;
    if (!guerrilla_campaign::Running()) return 0;
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_queue.empty()) return 0;
    const Message message = g_queue.front();
    g_queue.pop_front();
    if (tag) *tag = message.tag;
    if (icon) *icon = message.icon;
    if (kind) *kind = message.kind;
    if (ms) *ms = message.ms;
    return 1;
}
