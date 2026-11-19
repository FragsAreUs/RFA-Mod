// Guerrilla's ambient props (props.cpp): the items its people hold at an action node (a cigarette, binoculars, a
// welding torch, ...), for actions.cpp.
#pragma once
#include <string>
#include <vector>

struct PropKind {
    std::string item;                        // Guerrilla's ambient_prop_info's name (its items_3d entry; ours "rfg_" + it or it)
    std::string humanPoint, itemPoint;       // human_attach_point (a tag of the wearer's skeleton) and obj_attach_point (the item's)
    std::string permEffect, permPoint;       // always_on_effect: perm_effect_type at perm_effect_attach_name ("" none)
    bool duringEntry = true;                 // prop_creation_time: During Entry Anim (else After Entry Anim)
    bool atExit = true;                      // prop_removal_time: Exit Animation (else Node Complete)
    struct ActionEffect { std::string action, effect, point; };
    std::vector<ActionEffect> actionEffects;  // anim_effect_list: an effect while an action plays
};

// Makes the prop and puts it in the person's hand; its handle, or 0 (logged).
unsigned long PropPutOn(unsigned char* human, const PropKind& kind);
// Takes it away (destroys it), with its effects.
void PropTakeOff(unsigned long prop);
// Plays one of Guerrilla's effects (ours: "rfg_" + its name) looping on the prop at a tag of its own; the effect's
// handle, or -1. Stopped with PropStopEffect.
int PropPlayEffect(unsigned long prop, const std::string& effect, const std::string& point);
void PropStopEffect(int effect);
