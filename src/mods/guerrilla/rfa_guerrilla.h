// rfa_guerrilla.dll: the mods that bring Red Faction: Guerrilla's world and campaign into Armageddon, in one DLL.
// Its nine parts each have a folder of their own here (campaign\, gear\, ...), and each keeps the name it had as a
// DLL of its own (guerrilla_campaign.dll, ...): its switch under [Mods] (guerrilla_campaign=, ...), its name in the
// log and its data folder in mods\ (mods\guerrilla_campaign\, ...).
//
// A part calls another through the other's header (campaign\campaign_api.h, ...). While a part isn't running
// (switched off under [Mods], or it crashed while starting), what it gives the others answers as its missing DLL
// did: nothing there.
#pragma once

struct RFAMenu;
namespace rfa::game {
struct lua_State;
}

// Each part's start (called once, in this order, by rfa_guerrilla.cpp), its menus and Lua functions where it has them,
// and whether it is running: switched on under [Mods] and started without a crash.
namespace guerrilla_campaign {
void Init();
bool Running();
}  // namespace guerrilla_campaign

namespace guerrilla_gear {
void Init();
void Menu(const RFAMenu* menu);
bool Running();
}  // namespace guerrilla_gear

namespace guerrilla_hud {
void Init();
void Menu(const RFAMenu* menu);
void RegisterLua(rfa::game::lua_State* L);
bool Running();
}  // namespace guerrilla_hud

namespace guerrilla_kiosks {
void Init();
bool Running();
}  // namespace guerrilla_kiosks

namespace guerrilla_people {
void Init();
bool Running();
}  // namespace guerrilla_people

namespace guerrilla_save {
void Init();
void RegisterLua(rfa::game::lua_State* L);
bool Running();
}  // namespace guerrilla_save

namespace guerrilla_vehicles {
void Init();
bool Running();
}  // namespace guerrilla_vehicles

namespace guerrilla_voices {
void Init();
bool Running();
}  // namespace guerrilla_voices

namespace guerrilla_world {
void Init();
bool Running();
}  // namespace guerrilla_world
