// Guerrilla's managed layers for its activities, on Armageddon's own managed layer code.
//
// Guerrilla: each activity.xtbl row names its layers (managed_layer_info/layer_file_N, and when each is made:
// layer_load_time). base_activity_data::load gives them to managed_layer_load (RM 0x79D5D0), a managed_layer_set whose
// container is named after the activity (je_dm_15.str2_pc). As a run of it is offered (LOADING) the set starts loading
// (managed_layer_set::start_layer_loading 0x79D550) and the run waits for layer_resources_loaded (0x75E680); as it starts
// (the "At Start" layers) activity_base::create_managed_layer (0x795BE0) makes their objects (serialize_layer_objects
// 0x78D990 → world::serialize_layer); as it shuts down start_layer_unloading (0x78D8D0) takes them away.
//
// Armageddon kept the same machinery for its missions (its managed_layer_loading.cpp): the sets are rf4_layer_definition.xtbl's
// rows (read at start-up, 0x4FF360), each a container named in missions.vpp_pc's Layer_Container.asm_pc. GuerrillaBuilder.exe
// writes Guerrilla's activities' sets into that table (named as the activity), their containers and the asm (step 3 of the
// port's order). Here, the calls (all thiscall on the set unless said):
//   - find a set by name 0x4FF5C0 (cdecl);
//   - start loading 0x4FEC60 (false while another set holds the one slot: 0x7F4D90 allows one set at a time);
//   - its resources loaded 0x4FEE80; already serialized 0x4FEE50; serialize 0x4FEEF0 (world::serialize_layer 0x6D5D30, up to
//     512 objects a layer, their handles kept in the list at 0x12DE2F0);
//   - start unloading 0x4FECE0 (each listed object of the set's layers deleted, the containers released);
//   - member of the set's layers 0x4FF230 (an object's handle).
// The game unloads every set as a level shuts down (0x4FF550).
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "managed_layers.h"
#include <string.h>
#include <string>

namespace campaign::managed_layers {
namespace {

using FindSetFn = BYTE*(__cdecl*)(const char* name);
using SetCallFn = bool(__fastcall*)(BYTE* set, void* unused);
using MemberFn = bool(__fastcall*)(BYTE* set, void* unused, unsigned handle);
constexpr DWORD kFindSet = 0x4FF5C0, kStartLoading = 0x4FEC60, kResourcesLoaded = 0x4FEE80, kSerialized = 0x4FEE50, kSerialize = 0x4FEEF0,
                kStartUnloading = 0x4FECE0, kMember = 0x4FF230;
constexpr DWORD kLayerObjects = 0x12DE2F0;  // the handles of the layers' objects made (farray: count at +8)

bool g_ready = false;   // the functions are where they should be
BYTE* g_set = nullptr;  // the set loading or loaded, and whose
std::string g_activity;

bool LooksLikeFunction(DWORD address) {
    const BYTE* code = reinterpret_cast<const BYTE*>(address);
    return code[0] == 0x55 && code[1] == 0x8B && code[2] == 0xEC;  // push ebp / mov ebp,esp
}

bool Mine(const char* activity) { return g_set && activity && _stricmp(g_activity.c_str(), activity) == 0; }

}  // namespace

void Init() {
    g_ready = true;
    for (DWORD address : {kFindSet, kStartLoading, kResourcesLoaded, kSerialized, kSerialize, kStartUnloading, kMember})
        if (!LooksLikeFunction(address)) {
            rfa::Log("Managed layers: the game's managed layer code (0x%08lX) isn't as expected; activities' layers won't load", address);
            g_ready = false;
            return;
        }
}

bool Request(const char* activity) {
    if (!g_ready || !activity) return false;
    if (Mine(activity)) return true;
    if (g_set) {
        rfa::Log("Managed layers: %s's layers can't load while %s's are (the game loads one set at a time)", activity, g_activity.c_str());
        return false;
    }
    BYTE* set = reinterpret_cast<FindSetFn>(kFindSet)(activity);
    if (!set) {
        RFA_LOG_FIRST(16, "Managed layers: no set %s in rf4_layer_definition.xtbl (build the world again with GuerrillaBuilder.exe); the run has no layers",
                      activity);
        return false;
    }
    if (!reinterpret_cast<SetCallFn>(kStartLoading)(set, nullptr)) {
        rfa::Log("Managed layers: the game didn't start loading %s's layers (its layer slot is taken)", activity);
        return false;
    }
    g_set = set;
    g_activity = activity;
    rfa::LogDetail("Managed layers: %s's layers loading", activity);
    return true;
}

bool Loaded(const char* activity) { return Mine(activity) && reinterpret_cast<SetCallFn>(kResourcesLoaded)(g_set, nullptr); }

void Create(const char* activity) {
    if (!Mine(activity) || reinterpret_cast<SetCallFn>(kSerialized)(g_set, nullptr)) return;
    if (!reinterpret_cast<SetCallFn>(kResourcesLoaded)(g_set, nullptr)) {
        rfa::Log("Managed layers: %s's layers aren't loaded yet; its objects aren't made", activity);
        return;
    }
    const int before = *reinterpret_cast<const int*>(kLayerObjects + 8);
    const bool made = reinterpret_cast<SetCallFn>(kSerialize)(g_set, nullptr);
    const int after = *reinterpret_cast<const int*>(kLayerObjects + 8);
    rfa::Log(made ? "Managed layers: %s's layers made: %d objects" : "Managed layers: the game didn't make %s's layers' objects (%d listed)", activity,
             after - before);
}

void Release(const char* activity) {
    if (!Mine(activity)) return;
    reinterpret_cast<SetCallFn>(kStartUnloading)(g_set, nullptr);
    rfa::LogDetail("Managed layers: %s's layers taken away", activity);
    g_set = nullptr;
    g_activity.clear();
}

bool Member(unsigned handle) { return g_set && reinterpret_cast<MemberFn>(kMember)(g_set, nullptr, handle); }

void LevelInit() {
    g_set = nullptr;
    g_activity.clear();
}

}  // namespace campaign::managed_layers
