// Console commands for Guerrilla's districts (liberation.cpp), to try them before buildings, deaths
// and activities change them by themselves:
//   rfg_district                         the player's district and every district: EDF control,
//                                        morale, liberated, then the tech level, salvage and alert
//   rfg_district_set control <points>    sets the player's district's EDF control (0..its most;
//                                        Guerrilla's gfm_set_edf_control: liberation is checked)
//   rfg_district_set morale <points>     sets its morale (0..its most, 100)
//   rfg_district_missions_done           testing: marks the player's district's story missions done (none are
//                                        ported yet), so it liberates at EDF control 0, as Guerrilla's rule says
// What they change is saved with the campaign, as the game's own changes are.
//
// The game's console (as the "scriptx" command, 0x915890): a command object is registered with
// 0xC7BD70 (thiscall: the object, name, description, type, handler); type 3 runs the handler, cdecl
// with no arguments, which reads its arguments itself: while 0x28FF1C0 is set it is being run (not
// asked for help), and each call of 0xC7D570(kinds, 0) reads the next word into 0x28FF0B8 (its text,
// whatever the kind: 1 none, 2 a word, 8 a whole number, 16 a number with a fraction).
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "district_commands.h"
#include "campaign.h"
#include "liberation.h"
#include <stdlib.h>
#include <string.h>
#include <string>

namespace campaign::district_commands {
namespace {

using RegisterCommandFn = void*(__fastcall*)(void* command, void* unused, const char* name, const char* description, int type, void (*handler)());
auto const RegisterCommand = reinterpret_cast<RegisterCommandFn>(0xC7BD70);
using ParseArgumentsFn = void(__cdecl*)(int kinds, int flags);
auto const ParseArguments = reinterpret_cast<ParseArgumentsFn>(0xC7D570);
auto& g_commandRunning = *reinterpret_cast<int*>(0x28FF1C0);
auto const g_commandWord = reinterpret_cast<const char*>(0x28FF0B8);
using ConsolePrintFn = void(__cdecl*)(const char* format, ...);
auto const ConsolePrint = reinterpret_cast<ConsolePrintFn>(0xC7C440);

constexpr int kNone = 1, kWord = 2, kWhole = 8, kFraction = 16;
BYTE g_showCommand[32], g_setCommand[32], g_missionsCommand[32];

const char* AlertName(int level) {
    static const char* const names[] = {"green", "yellow", "orange", "red"};
    return level >= 0 && level < 4 ? names[level] : "?";
}

void PrintDistrict(int index, const char* label) {
    const liberation::District* district = liberation::GetDistrict(index);
    if (!district) return;
    const float control = district->controlMax > 0.0f ? district->control * 100.0f / district->controlMax : 0.0f;
    const float morale = district->moraleMax > 0.0f ? district->morale * 100.0f / district->moraleMax : 0.0f;
    ConsolePrint("%s%s: EDF control %.0f / %.0f (%.0f%%), morale %.0f / %.0f (%.0f%%; effective %.0f%%)%s\n", label, district->name.c_str(), district->control,
                 district->controlMax, control, district->morale, district->moraleMax, morale, liberation::EffectiveMoralePercent(index) * 100.0f,
                 district->liberated ? ", liberated" : "");
}

void Show() {
    if (!g_commandRunning) return;
    const int here = liberation::PlayerDistrict();
    if (here >= 0) PrintDistrict(here, "You are in ");
    else ConsolePrint("You aren't in one of Guerrilla's districts\n");
    for (int i = 0; i < liberation::DistrictCount(); ++i) PrintDistrict(i, "  ");
    ConsolePrint("Tech level %d, salvage %d, alert %s (%.0f)\n", liberation::TechLevel(), liberation::Salvage(), AlertName(liberation::AlertLevel()),
                 liberation::AlertValue());
}

void Set() {
    if (!g_commandRunning) return;
    ParseArguments(kWord | kNone, 0);
    const std::string what = g_commandWord;
    ParseArguments(kWord | kWhole | kFraction | kNone, 0);
    const std::string number = g_commandWord;
    const int here = liberation::PlayerDistrict();
    if ((what != "control" && what != "morale") || number.empty() || here < 0) {
        ConsolePrint(here < 0 ? "You aren't in one of Guerrilla's districts\n" : "rfg_district_set control <points> or rfg_district_set morale <points>\n");
        return;
    }
    const float value = static_cast<float>(atof(number.c_str()));
    if (what == "control") liberation::SetEdfControl(value, here);
    else liberation::SetMorale(value, here);
    rfa::Log("rfg_district_set %s %.0f in %s", what.c_str(), value, liberation::GetDistrict(here)->name.c_str());
    PrintDistrict(here, "Now ");
}

// Testing only: Guerrilla liberates a district at EDF control 0 once its story missions are done, and no missions
// are ported yet; this marks your district's done (saved with the campaign), so liberation can be tried.
void MissionsDone() {
    if (!g_commandRunning) return;
    const int here = liberation::PlayerDistrict();
    if (here < 0) {
        ConsolePrint("You aren't in one of Guerrilla's districts\n");
        return;
    }
    const int marked = liberation::MarkMissionsDone(here);
    rfa::Log("rfg_district_missions_done in %s: %d story missions marked done (testing)", liberation::GetDistrict(here)->name.c_str(), marked);
    ConsolePrint("%d story missions of %s marked done (testing); it liberates once its EDF control is 0 (rfg_district_set control 0)\n", marked,
                 liberation::GetDistrict(here)->name.c_str());
    PrintDistrict(here, "Now ");
}

}  // namespace

void Enter() {
    static bool added;
    if (added) return;
    added = true;
    RegisterCommand(g_showCommand, nullptr, "rfg_district", "Shows Guerrilla's districts: EDF control and morale", 3, &Show);
    RegisterCommand(g_setCommand, nullptr, "rfg_district_set", "Sets the EDF control or morale of your district: rfg_district_set control|morale <points>", 3, &Set);
    RegisterCommand(g_missionsCommand, nullptr, "rfg_district_missions_done",
                    "Testing: marks your district's story missions done, so it liberates at 0 EDF control (no missions are ported yet)", 3, &MissionsDone);
}

}  // namespace campaign::district_commands
