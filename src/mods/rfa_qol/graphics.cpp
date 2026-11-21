// A Graphics window in the in-game menus (rfa_menu.h). A part of rfa_qol.dll. Press F7 (or open the
// console), then Graphics in the bar along the bottom of the screen: sliders and switches for the
// picture of the world ported from Guerrilla, the [GuerrillaWorld] settings in rfa_mods.ini marked
// (live), and Mason's Nano Forge light ([GuerrillaPeople] MasonNanoLight, which shows the next time the
// game makes him). Letting go of a slider, or ticking a box, saves that one line of rfa_mods.ini, and
// guerrilla_world (guerrilla_people) reads the file again within a second, so the change shows while
// you look at it. The window reads the settings again whenever the file is saved, so it shows edits
// made by hand too.
#define RFA_MOD_NAME "graphics"
#include "rfa_mod.h"
#include "rfa_menu.h"
#include "rfa_qol.h"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

constexpr wchar_t kSection[] = L"GuerrillaWorld";

struct Slider {
    const wchar_t* key;
    const char* label;
    float min, max, fallback;
    bool whole;         // saved as a whole number
    const char* about;  // its tooltip
    float value;
};

struct Switch {
    const wchar_t* key;
    const char* label;
    bool fallback;
    const char* about;
    bool value;
    const wchar_t* section = kSection;  // another mod's ([GuerrillaPeople]): not reset by "Guerrilla's look"
};

Slider g_sliders[] = {
    {L"Exposure", "Exposure", 0.25f, 4, 1, false, "With Guerrilla's tone mapping: its exposure times this. 1 = Guerrilla's, higher is brighter", 0},
    {L"Brightness", "Sky brightness", 0, 10, 5, false, "The sky's time-of-day colours times this. 5 = Guerrilla's with its tone mapping", 0},
    {L"Ambient", "Sky light", 0, 3, 1, false, "The time of day's ambient light on the ground times this", 0},
    {L"Sun", "Sunlight", 0, 3, 1, false, "The sun's and moon's light times this", 0},
    {L"Shine", "Sun's shine", 0, 1, 1, false, "The sun's highlights on surfaces (by their specular maps). 0 = none", 0},
    {L"Bloom", "Bloom", 0, 2, 1, false, "With Guerrilla's tone mapping: the glow round bright things. 1 = as now, 0 = none", 0},
    {L"SunShafts", "Sun shafts", 0, 3, 1, false, "With Guerrilla's tone mapping: the glow streaming round the sun. 1 = Guerrilla's, 0 = none", 0},
    {L"SunCorona", "Sun corona", 0, 2, 1, false, "The sun's rays, glow, halo and lens flares when you look toward it. 1 = Guerrilla's, 0 = none", 0},
    {L"Fog", "Fog", 0, 3, 1, false, "How thick Guerrilla's haze is. 1 = Guerrilla's, 0 = none", 0},
    {L"FarClip", "View distance", 475, 1500, 475, true,
     "How far the camera draws, in metres. 475 = Guerrilla's: zones and buildings load out of sight. Farther shows more, "
     "and things appearing as they load", 0},
    {L"Sharpen", "Sharpening", 0, 100, 40, true, "Contrast-adaptive sharpening under the HUD. 0 = none, 100 = strongest", 0},
    {L"ClockSpeed", "Clock speed", 0, 480, 48, true, "Game seconds a real second: 48 makes a day in half an hour. 0 = the game's own clock", 0},
};

Switch g_switches[] = {
    {L"ToneMapping", "Guerrilla's tone mapping", true, "The picture's brightness adapts as in Guerrilla. Off = Armageddon's own exposure and grading", false},
    {L"ColourCorrection", "Guerrilla's colour correction", true, "Its time of day's tint over the light and sky. Off = pinker", false},
    {L"SunShadows", "Sun and moon shadows", true, "Off = sunlight goes through buildings", false},
    {L"CloudShadows", "Cloud shadows", true, "The clouds' shadows drifting over the ground, as Re-Mars-tered's. Needs sun shadows", false},
    {L"Particles", "Particles as Guerrilla's", true, "With its tone mapping: fire and smoke in a layer of their own, keeping their colours", false},
    {L"ParticleFog", "Fogged particles", true, "Smoke, fire and explosions fade into the haze far off, as Guerrilla's", false},
    {L"Dust", "Dust on the wind", true, "Guerrilla's dust blowing round you, thicker the stronger the wind", false},
    {L"MasonNanoLight", "Mason's Nano Forge light", true,
     "The always-on cyan glow at his left forearm, as Armageddon's player has it. Off = as Guerrilla's Mason. "
     "Shows the next time he is made: load the level again", false, L"GuerrillaPeople"},
};

bool g_open = false;
FILETIME g_iniWritten = {};   // when rfa_mods.ini was last saved, as last read
ULONGLONG g_lastCheck = 0;    // when that was last looked at

float ReadValue(const wchar_t* key, float fallback, const wchar_t* section = kSection) {
    const std::wstring text = rfa::IniString(section, key);
    return text.empty() ? fallback : static_cast<float>(_wtof(text.c_str()));
}

void ReadAll() {
    for (Slider& slider : g_sliders) {
        slider.value = ReadValue(slider.key, slider.fallback);
        if (slider.value < slider.min) slider.value = slider.min;
        if (slider.value > slider.max) slider.value = slider.max;
    }
    for (Switch& option : g_switches) option.value = ReadValue(option.key, option.fallback ? 1.0f : 0.0f, option.section) != 0;
}

// Once a second, while no slider is held: if rfa_mods.ini was saved since (by this window, or by
// hand), read the settings again.
void ReadIfSaved() {
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastCheck < 1000 || ImGui::IsAnyItemActive()) return;
    g_lastCheck = now;
    WIN32_FILE_ATTRIBUTE_DATA ini;
    if (!GetFileAttributesExW(rfa::IniPath().c_str(), GetFileExInfoStandard, &ini)) return;
    if (CompareFileTime(&ini.ftLastWriteTime, &g_iniWritten) == 0) return;
    g_iniWritten = ini.ftLastWriteTime;
    ReadAll();
}

void Save(const wchar_t* key, const wchar_t* text, const wchar_t* section = kSection) {
    if (!WritePrivateProfileStringW(section, key, text, rfa::IniPath().c_str()))
        rfa::Log("Could not save %ls=%ls in rfa_mods.ini (error %lu)", key, text, GetLastError());
}

void Save(const Slider& slider) {
    wchar_t text[32];
    if (slider.whole) swprintf(text, 32, L"%d", static_cast<int>(slider.value + 0.5f));
    else swprintf(text, 32, L"%.3g", slider.value);
    Save(slider.key, text);
}

void Save(const Switch& option) { Save(option.key, option.value ? L"1" : L"0", option.section); }

void DrawWindow() {
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 24, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Graphics", &g_open)) {
        ImGui::End();
        return;
    }
    ReadIfSaved();
    ImGui::TextDisabled("The world ported from Guerrilla. Armageddon's own levels keep their look.");
    ImGui::PushItemWidth(-ImGui::GetFontSize() * 10);
    for (Slider& slider : g_sliders) {
        ImGui::SliderFloat(slider.label, &slider.value, slider.min, slider.max, slider.whole ? "%.0f" : "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (slider.about) ImGui::SetItemTooltip("%s", slider.about);
        if (ImGui::IsItemDeactivatedAfterEdit()) Save(slider);
    }
    ImGui::PopItemWidth();
    ImGui::Separator();
    for (Switch& option : g_switches) {
        if (ImGui::Checkbox(option.label, &option.value)) Save(option);
        if (option.about) ImGui::SetItemTooltip("%s", option.about);
    }
    ImGui::Separator();
    if (ImGui::Button("Guerrilla's look")) {
        for (Slider& slider : g_sliders) {
            slider.value = slider.fallback;
            Save(slider);
        }
        for (Switch& option : g_switches) {
            if (option.section != kSection) continue;  // another mod's choice, not the world's look
            option.value = option.fallback;
            Save(option);
        }
    }
    ImGui::SetItemTooltip("Every setting here back to its default (but Mason's Nano Forge light)");
    ImGui::SameLine();
    ImGui::TextDisabled("Saved in rfa_mods.ini, [GuerrillaWorld] (Mason's light: [GuerrillaPeople])");
    ImGui::End();
}

}  // namespace

void graphics::Init() { rfa::LogDetail("Graphics window ready: press F7, then Graphics in the bar at the bottom"); }

void graphics::Menu(const RFAMenu* menu) {
    if (!rfa::menu::Use(menu)) return;
    if (menu->stage == RFA_MENU_BAR) {
        if (ImGui::MenuItem("Graphics", nullptr, &g_open) && g_open) g_lastCheck = 0;  // look at the file straight away
    } else if (g_open) {
        DrawWindow();
    }
}
