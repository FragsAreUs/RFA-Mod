// OpenAL Soft, loaded at run time (see openal.h).
#include "openal.h"

#define NOMINMAX
#include <windows.h>

namespace rfa_audio {

std::wstring OwnFolder()
{
    // This module: the address of a function in it gives its handle.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&OwnFolder), &self);
    wchar_t path[MAX_PATH * 4] = {};
    DWORD length = GetModuleFileNameW(self, path, static_cast<DWORD>(std::size(path)));
    std::wstring folder(path, length);
    size_t slash = folder.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : folder.substr(0, slash + 1);
}

namespace {

// One function by name, into a pointer of its type; a missing one is noted.
template <typename Function>
void Find(HMODULE module, Function& function, const char* name, std::string& missing)
{
    function = reinterpret_cast<Function>(GetProcAddress(module, name));
    if (!function) missing += (missing.empty() ? "" : ", ") + std::string(name);
}

}  // namespace

bool OpenAl::Load(std::string& error)
{
    if (module) return true;
    // soft_oal.dll, as OpenAL Soft's own binaries name it, beside us; OpenAL32.dll there as a second choice
    // (OpenAL Soft renamed, as its readme suggests). Never the system's router: we want this one.
    const std::wstring where = folder.empty() ? OwnFolder() : folder;
    for (const wchar_t* name : {L"soft_oal.dll", L"OpenAL32.dll"}) {
        std::wstring candidate = where + name;
        if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        // LOAD_WITH_ALTERED_SEARCH_PATH: what it needs is looked for beside it.
        HMODULE loaded = LoadLibraryExW(candidate.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!loaded) {
            error = "OpenAL Soft is there but didn't load (error " + std::to_string(GetLastError()) + ")";
            return false;
        }
        module = loaded;
        path = candidate;
        break;
    }
    if (!module) {
        std::string asked;
        for (wchar_t c : folder) asked += c < 128 ? static_cast<char>(c) : '?';
        error = folder.empty() ? "OpenAL Soft (soft_oal.dll) isn't beside rfa_audio.dll"
                               : "OpenAL Soft (soft_oal.dll) isn't in " + asked;
        return false;
    }
    HMODULE m = static_cast<HMODULE>(module);
    std::string missing;
    Find(m, alcOpenDevice, "alcOpenDevice", missing);
    Find(m, alcCloseDevice, "alcCloseDevice", missing);
    Find(m, alcCreateContext, "alcCreateContext", missing);
    Find(m, alcDestroyContext, "alcDestroyContext", missing);
    Find(m, alcMakeContextCurrent, "alcMakeContextCurrent", missing);
    Find(m, alcGetError, "alcGetError", missing);
    Find(m, alcGetString, "alcGetString", missing);
    Find(m, alcGetIntegerv, "alcGetIntegerv", missing);
    Find(m, alcIsExtensionPresent, "alcIsExtensionPresent", missing);
    Find(m, alcGetProcAddress, "alcGetProcAddress", missing);
    Find(m, alGetError, "alGetError", missing);
    Find(m, alIsExtensionPresent, "alIsExtensionPresent", missing);
    Find(m, alGetEnumValue, "alGetEnumValue", missing);
    Find(m, alGetString, "alGetString", missing);
    Find(m, alDistanceModel, "alDistanceModel", missing);
    Find(m, alDopplerFactor, "alDopplerFactor", missing);
    Find(m, alListenerf, "alListenerf", missing);
    Find(m, alListener3f, "alListener3f", missing);
    Find(m, alListenerfv, "alListenerfv", missing);
    Find(m, alGenSources, "alGenSources", missing);
    Find(m, alDeleteSources, "alDeleteSources", missing);
    Find(m, alSourcei, "alSourcei", missing);
    Find(m, alSourcef, "alSourcef", missing);
    Find(m, alSource3f, "alSource3f", missing);
    Find(m, alGetSourcei, "alGetSourcei", missing);
    Find(m, alGetSourcef, "alGetSourcef", missing);
    Find(m, alSourcePlay, "alSourcePlay", missing);
    Find(m, alSourcePause, "alSourcePause", missing);
    Find(m, alSourceStop, "alSourceStop", missing);
    Find(m, alSourceRewind, "alSourceRewind", missing);
    Find(m, alSourceQueueBuffers, "alSourceQueueBuffers", missing);
    Find(m, alSourceUnqueueBuffers, "alSourceUnqueueBuffers", missing);
    Find(m, alGenBuffers, "alGenBuffers", missing);
    Find(m, alDeleteBuffers, "alDeleteBuffers", missing);
    Find(m, alBufferData, "alBufferData", missing);
    Find(m, alBufferiv, "alBufferiv", missing);
    if (!missing.empty()) {
        error = "OpenAL Soft lacks " + missing;
        Unload();
        return false;
    }
    // The loopback functions are an extension's: found through alcGetProcAddress (null if it hasn't them).
    alcLoopbackOpenDeviceSOFT = reinterpret_cast<LPALCLOOPBACKOPENDEVICESOFT>(alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT"));
    alcIsRenderFormatSupportedSOFT = reinterpret_cast<LPALCISRENDERFORMATSUPPORTEDSOFT>(alcGetProcAddress(nullptr, "alcIsRenderFormatSupportedSOFT"));
    alcRenderSamplesSOFT = reinterpret_cast<LPALCRENDERSAMPLESSOFT>(alcGetProcAddress(nullptr, "alcRenderSamplesSOFT"));
    // EFX (its filters, for Wwise's low-pass): OpenAL Soft exports them; without them sounds aren't filtered.
    alGenFilters = reinterpret_cast<LPALGENFILTERS>(GetProcAddress(m, "alGenFilters"));
    alDeleteFilters = reinterpret_cast<LPALDELETEFILTERS>(GetProcAddress(m, "alDeleteFilters"));
    alFilteri = reinterpret_cast<LPALFILTERI>(GetProcAddress(m, "alFilteri"));
    alFilterf = reinterpret_cast<LPALFILTERF>(GetProcAddress(m, "alFilterf"));
    if (!alGenFilters || !alDeleteFilters || !alFilteri || !alFilterf) alGenFilters = nullptr;
    // EFX's effects and slots (reverb), and a source's sends: without them nothing reverberates.
    alGenEffects = reinterpret_cast<LPALGENEFFECTS>(GetProcAddress(m, "alGenEffects"));
    alDeleteEffects = reinterpret_cast<LPALDELETEEFFECTS>(GetProcAddress(m, "alDeleteEffects"));
    alEffecti = reinterpret_cast<LPALEFFECTI>(GetProcAddress(m, "alEffecti"));
    alEffectf = reinterpret_cast<LPALEFFECTF>(GetProcAddress(m, "alEffectf"));
    alGenAuxiliaryEffectSlots = reinterpret_cast<LPALGENAUXILIARYEFFECTSLOTS>(GetProcAddress(m, "alGenAuxiliaryEffectSlots"));
    alDeleteAuxiliaryEffectSlots = reinterpret_cast<LPALDELETEAUXILIARYEFFECTSLOTS>(GetProcAddress(m, "alDeleteAuxiliaryEffectSlots"));
    alAuxiliaryEffectSloti = reinterpret_cast<LPALAUXILIARYEFFECTSLOTI>(GetProcAddress(m, "alAuxiliaryEffectSloti"));
    alAuxiliaryEffectSlotf = reinterpret_cast<LPALAUXILIARYEFFECTSLOTF>(GetProcAddress(m, "alAuxiliaryEffectSlotf"));
    alSource3i = reinterpret_cast<LPALSOURCE3I>(GetProcAddress(m, "alSource3i"));
    alSpeedOfSound = reinterpret_cast<LPALSPEEDOFSOUND>(GetProcAddress(m, "alSpeedOfSound"));
    if (!alGenEffects || !alDeleteEffects || !alEffecti || !alEffectf || !alGenAuxiliaryEffectSlots || !alDeleteAuxiliaryEffectSlots ||
        !alAuxiliaryEffectSloti || !alAuxiliaryEffectSlotf || !alSource3i)
        alGenEffects = nullptr;
    return true;
}

void OpenAl::Unload()
{
    if (module) FreeLibrary(static_cast<HMODULE>(module));
    *this = OpenAl();
}

}  // namespace rfa_audio
