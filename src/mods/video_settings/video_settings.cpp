// Keeps the game's video settings in bin\video_settings.cfg so they survive a restart.
//
// The game loads and saves its video options (resolution, Windowed, quality settings) through two
// small functions of its own, ReadSetting and WriteSetting, which only the video settings load/save
// code uses (16 calls each). They store the values under HKEY_LOCAL_MACHINE, which Windows refuses
// without administrator rights, so every save failed and each launch started at 1280x720
// fullscreen. Both Steam builds have this bug.
//
// This mod replaces those two functions with versions that use a plain cfg file. The game's
// registry code never runs for these settings, and nothing in this mod calls the registry.
#define RFA_MOD_NAME "video_settings"
#include "rfa_mod.h"
#include "rfa_game.h"

namespace {

// In the publicbeta exe; their only callers are the video settings load (0x5A7E40) and save (0x5A7C50).
constexpr DWORD kReadSetting = 0xCC6090;
constexpr DWORD kWriteSetting = 0xCC60C0;

const std::wstring& CfgPath() {
    static const std::wstring path = rfa::RootDir() + L"\\video_settings.cfg";
    return path;
}

void ToWide(const char* text, wchar_t* out, int size) {
    if (!MultiByteToWideChar(CP_ACP, 0, text ? text : "", -1, out, size)) out[0] = L'\0';
}

// Same contract as the game's ReadSetting: fills *out and returns 0, or fills in the game's
// default and returns -1 when the value isn't saved yet.
int __cdecl ReadSetting(const char* section, const char* name, DWORD* out, DWORD fallback) {
    wchar_t sectionW[128], nameW[128], text[32];
    ToWide(section, sectionW, _countof(sectionW));
    ToWide(name, nameW, _countof(nameW));
    GetPrivateProfileStringW(sectionW, nameW, L"", text, _countof(text), CfgPath().c_str());
    if (!text[0]) {
        *out = fallback;
        RFA_LOG_FIRST(20, "%s not saved yet, using the game's default %lu", name, fallback);
        return -1;
    }
    *out = wcstoul(text, nullptr, 10);
    RFA_LOG_FIRST(20, "Loaded %s=%lu", name, *out);
    return 0;
}

// Same contract as the game's WriteSetting: returns 0 on success.
int __cdecl WriteSetting(const char* section, const char* name, DWORD value) {
    wchar_t sectionW[128], nameW[128], text[16];
    ToWide(section, sectionW, _countof(sectionW));
    ToWide(name, nameW, _countof(nameW));
    swprintf_s(text, L"%lu", value);
    if (!WritePrivateProfileStringW(sectionW, nameW, text, CfgPath().c_str())) {
        rfa::Log("Could not write %ls (error %lu)", CfgPath().c_str(), GetLastError());
        return -1;
    }
    RFA_LOG_FIRST(40, "Saved %s=%lu", name, value);
    return 0;
}

}  // namespace

extern "C" __declspec(dllexport) void __cdecl RFAMod_Init() {
    bool exists = GetFileAttributesW(CfgPath().c_str()) != INVALID_FILE_ATTRIBUTES;
    rfa::Log("Video settings are kept in %ls%s", CfgPath().c_str(),
             exists ? "" : " (not created yet: set your options once in Options > Display)");

    if (!rfa::game::IsSupportedBuild()) {
        rfa::Log("Not the publicbeta build; the game's settings functions are left alone");
        return;
    }
    if (rfa::ReplaceFunction(kReadSetting, reinterpret_cast<void*>(&ReadSetting)) &&
        rfa::ReplaceFunction(kWriteSetting, reinterpret_cast<void*>(&WriteSetting)))
        rfa::Log("The game now loads and saves its video settings from the cfg file");
    else
        rfa::Log("Could not redirect the game's settings functions (error %lu)", GetLastError());
}
