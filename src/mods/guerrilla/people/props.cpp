// Guerrilla's ambient props (step 2 of the people's rest, 2026-11-15; the main session, for the user: "the smokers'
// cigarettes, Samanya's welding torch and sparks, the VIPs' binoculars and the rest of the action types' props").
// [RM, A]
//
// Guerrilla's action node type names an ambient prop (action_node_types.xtbl's ambinet_prop_info: ambient_prop_type,
// prop_creation_time, prop_removal_time), an entry of ambient_prop_info.xtbl: its item (items_3d.xtbl, the same name),
// the wearer's attach point (human_attach_point: r-hand or l-hand) and the item's own (obj_attach_point,
// weapon_handle), an effect while it is held (always_on_effect) and effects while an action plays (anim_effect_list).
// enter_action_node_action (RM 0x610140) makes it during or after the entry clip: amb_prop_create (RM 0xB4BB90) makes
// an item object of that item_info at the person (obj_item_props, world::create_object_internal) and hangs it on the
// wearer's point by its own (object::set_child), owned by the wearer (item::set_owner), as amb_prop_mount (RM 0xB40CE0)
// does for one picked up. Armageddon has no ambient props, so here the item (the weapons build ports the 18 props'
// items with their own preloaded containers) is made and hung on the hand the way guerrilla_gear's backpacks make a
// worn pack's model (Armageddon's own steps for a character's prop): items_3d by name 0x7CF440, its props 0x7CCF00 (no
// physics, a character's prop), the object made 0x611000, its owner (its function +0xA0) and attached 0x7B01D0 (the
// wearer's skeleton tag or bone of the point's name, 0xD8A140 / 0xD8A120; the item's own tag by its function +0x2C).
// The effects are the world build's (Guerrilla's under "rfg_" names), played looping on the prop (0x4EB460, as the
// backpacks' effects) and stopped as the world's dust (0x4D4360, 0x4F7AA0). Only Guerrilla's: an effect or item that
// isn't built is left out, and said once.

#define RFA_MOD_NAME "guerrilla_people"
#include "rfa_mod.h"
#include "props.h"
#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_set>

namespace {

void* const kWorld = reinterpret_cast<void*>(0x1870630);
constexpr unsigned long kNone = 0xFFFFFFFF;
constexpr DWORD kPosition = 0x04, kOrient = 0x10;
auto const ItemInfoNamed = reinterpret_cast<BYTE*(__cdecl*)(const char* name)>(0x7CF440);
auto const InitFlags = reinterpret_cast<void(__thiscall*)(DWORD* flags)>(0x465320);
auto const InitWord = reinterpret_cast<void(__thiscall*)(DWORD* word)>(0x9195A0);
auto const MakeProps = reinterpret_cast<void(__thiscall*)(BYTE* props, BYTE* item, const float* position, const float* orientation,
                                                          DWORD* flags, DWORD* word, DWORD collide, float unknown, int none)>(0x7CCF00);
auto const CreateObject = reinterpret_cast<BYTE*(__thiscall*)(void* world, BYTE* props, DWORD owner)>(0x611000);
auto const FreeProps = reinterpret_cast<void(__thiscall*)(BYTE* props)>(0x694580);
auto const SetPropType = reinterpret_cast<void(__thiscall*)(BYTE* typeField, int type)>(0x78F3A0);
auto const AttachToHuman = reinterpret_cast<bool(__thiscall*)(BYTE* human, BYTE* object, int point, int objectPoint, bool, bool)>(0x7B01D0);
auto const HandleOf = reinterpret_cast<DWORD(__thiscall*)(const BYTE* object)>(0x424850);
auto const ObjectAt = reinterpret_cast<BYTE*(__thiscall*)(void* world, DWORD handle)>(0x6CB9E0);
auto const DestroyObject = reinterpret_cast<void(__thiscall*)(void* world, BYTE* object, bool now)>(0x6D5450);
auto const SkeletonNamed = reinterpret_cast<BYTE*(__cdecl*)(const char* name)>(0x973430);
auto const SkeletonTag = reinterpret_cast<int(__cdecl*)(BYTE* skeleton, const char* name)>(0xD8A140);
auto const SkeletonBone = reinterpret_cast<int(__cdecl*)(BYTE* skeleton, const char* name)>(0xD8A120);
using SetOwnerFn = void(__thiscall*)(BYTE* object, DWORD owner, int unknown);
using TagNamedFn = int(__thiscall*)(BYTE* object, const char* name);
const DWORD* const kCollisionSetting = reinterpret_cast<const DWORD*>(0x1975E94);
constexpr DWORD kItemContainer = 0x18, kPropFlagsWord = 0x224, kPropType = 0xCA, kSetOwner = 0xA0, kTagNamed = 0x2C;
constexpr DWORD kCharacterEntry = 0x4FC, kSkeletonName = 0x58;
constexpr DWORD kNoPhysics = 0x2, kCharacterProp = 0x400;
using EffectNumberFn = int(__cdecl*)(const char* name);
auto const EffectNumber = reinterpret_cast<EffectNumberFn>(0x4EF370);
using EffectsPlayFn = int(__cdecl*)(int effect, const float* where, const float* turn, bool looping, DWORD host, int hostTag, DWORD source,
                                    bool onCamera, DWORD unused1, DWORD unused2);
auto const EffectsPlay = reinterpret_cast<EffectsPlayFn>(0x4EB460);
auto const EffectObject = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int handle)>(0x4D4360);
auto const StopEffect = reinterpret_cast<void(__fastcall*)(BYTE* effect, void* unused, int style)>(0x4F7AA0);

std::unordered_set<std::string> g_toldMissing;
int g_made = 0, g_logs = 0;

void TellMissing(const std::string& what) {
    if (g_toldMissing.size() < 48 && g_toldMissing.insert(what).second)
        rfa::Log("Props: %s isn't built (build Guerrilla's weapons and world again); left out", what.c_str());
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The wearer's point: a tag of its skeleton of that name, else its bone, else -1 (its middle).
int PointOn(BYTE* human, const std::string& name) {
    if (name.empty()) return -1;
    const BYTE* entry = *reinterpret_cast<BYTE* const*>(human + kCharacterEntry);
    const char* rig = entry ? *reinterpret_cast<const char* const*>(entry + kSkeletonName) : nullptr;
    if (!rig || !*rig) return -1;
    std::string file = rig;
    if (const size_t dot = file.find_last_of('.'); dot != std::string::npos) file.resize(dot);
    file += ".rig_pc";
    BYTE* skeleton = SkeletonNamed(file.c_str());
    if (!skeleton) return -1;
    const int tag = SkeletonTag(skeleton, name.c_str());
    return tag >= 0 ? tag : SkeletonBone(skeleton, name.c_str());
}

int TagOf(BYTE* object, const std::string& name) {
    if (name.empty()) return -1;
    const TagNamedFn tagNamed = *reinterpret_cast<const TagNamedFn*>(*reinterpret_cast<BYTE* const*>(object) + kTagNamed);
    return tagNamed(object, Lower(name).c_str());
}

}  // namespace

unsigned long PropPutOn(unsigned char* human, const PropKind& kind) {
    if (!human || kind.item.empty()) return 0;
    std::string name = "rfg_" + kind.item;
    BYTE* item = ItemInfoNamed(name.c_str());
    if (!item) {
        name = kind.item;
        item = ItemInfoNamed(name.c_str());
    }
    if (!item) {
        TellMissing("the prop " + kind.item);
        return 0;
    }
    if (!*reinterpret_cast<void* const*>(item + kItemContainer)) {
        TellMissing("the prop " + name + "'s own container");
        return 0;
    }
    DWORD flags = 0, word = 0;
    InitFlags(&flags);
    flags |= kNoPhysics | kCharacterProp;
    InitWord(&word);
    word = *reinterpret_cast<const DWORD*>(human + kPropFlagsWord);
    alignas(16) BYTE props[64] = {};
    MakeProps(props, item, reinterpret_cast<const float*>(human + kPosition), reinterpret_cast<const float*>(human + kOrient), &flags, &word,
              *kCollisionSetting, -1.0f, -1);
    const DWORD owner = HandleOf(human);
    BYTE* object = CreateObject(kWorld, props, owner);
    FreeProps(props);
    if (!object) {  // the world's item pool full (512, as Guerrilla's): this one goes without
        RFA_LOG_FIRST(8, "Props: %s couldn't be made (the world's items are all in use?); this one goes without", name.c_str());
        return 0;
    }
    SetPropType(object + kPropType, -1);
    const SetOwnerFn setOwner = *reinterpret_cast<const SetOwnerFn*>(*reinterpret_cast<BYTE* const*>(object) + kSetOwner);
    setOwner(object, owner, 0);
    const int point = PointOn(human, kind.humanPoint), tag = TagOf(object, kind.itemPoint);
    if (!AttachToHuman(human, object, point, tag, false, true)) {
        RFA_LOG_DETAIL_FIRST(8, "Props: %s made but not attached to NPC 0x%08lX (point %s %d, its own %s %d); taken away", name.c_str(), owner, kind.humanPoint.c_str(),
                      point, kind.itemPoint.c_str(), tag);
        DestroyObject(kWorld, object, true);
        return 0;
    }
    ++g_made;
    if (g_logs < 24) {
        ++g_logs;
        rfa::LogDetail("Props: NPC 0x%08lX holds %s (%s, point %d; its own %s, %d) (%d so far)", owner, name.c_str(), kind.humanPoint.c_str(), point,
                 kind.itemPoint.c_str(), tag, g_made);
    }
    return HandleOf(object);
}

void PropTakeOff(unsigned long prop) {
    if (!prop || prop == kNone) return;
    if (BYTE* object = ObjectAt(kWorld, prop)) DestroyObject(kWorld, object, true);
}

int PropPlayEffect(unsigned long prop, const std::string& effect, const std::string& point) {
    if (!prop || effect.empty()) return -1;
    BYTE* object = ObjectAt(kWorld, prop);
    if (!object) return -1;
    const int number = EffectNumber(("rfg_" + effect).c_str());  // Guerrilla's, as the world build adds it (never Armageddon's own)
    if (number < 0) {
        TellMissing("the effect " + effect);
        return -1;
    }
    return EffectsPlay(number, reinterpret_cast<const float*>(object + kPosition), reinterpret_cast<const float*>(object + kOrient), true, HandleOf(object),
                       TagOf(object, point), HandleOf(object), false, 0, 0);
}

void PropStopEffect(int effect) {
    if (effect < 0) return;
    if (BYTE* e = EffectObject(kWorld, nullptr, effect)) StopEffect(e, nullptr, 1);
}
