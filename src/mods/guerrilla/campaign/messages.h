// Guerrilla's HUD messages and tool tips that the campaign's systems show (messages.cpp), queued for
// guerrilla_hud to take (GuerrillaCampaign_NextMessage).
#pragma once

namespace campaign::messages {

// hud_message_add_minor: a line on the HUD, Guerrilla's string tag (HM_TRUST_BETRAYED) and the
// picture shown with it (ui_hud_weapon_icon_sm_skull; null for none).
void Minor(const char* tag, const char* icon);
// A minor message whose text has a number in it (HM_GUERRILLAS_ASSIST: "%d guerrillas ..."), given in *ms.
void MinorCount(const char* tag, int count);
// ui_tool_tips_add_tip: a tool tip by its name in Guerrilla's tool_tips.xtbl ("Civilian_Death"), for so
// many milliseconds.
void Tip(const char* name, int ms);

}  // namespace campaign::messages
