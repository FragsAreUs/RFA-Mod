// OpenAL Soft, loaded at run time: soft_oal.dll from this DLL's own folder (never a fixed path), its functions
// found with GetProcAddress. Without it the engine doesn't open, and says why; nothing links against it.
#pragma once

#define AL_NO_PROTOTYPES
#define ALC_NO_PROTOTYPES
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>

#include <string>

namespace rfa_audio {

struct OpenAl {
    // ALC
    LPALCOPENDEVICE alcOpenDevice = nullptr;
    LPALCCLOSEDEVICE alcCloseDevice = nullptr;
    LPALCCREATECONTEXT alcCreateContext = nullptr;
    LPALCDESTROYCONTEXT alcDestroyContext = nullptr;
    LPALCMAKECONTEXTCURRENT alcMakeContextCurrent = nullptr;
    LPALCGETERROR alcGetError = nullptr;
    LPALCGETSTRING alcGetString = nullptr;
    LPALCGETINTEGERV alcGetIntegerv = nullptr;
    LPALCISEXTENSIONPRESENT alcIsExtensionPresent = nullptr;
    LPALCGETPROCADDRESS alcGetProcAddress = nullptr;
    // Loopback (ALC_SOFT_loopback): sound made on request, for checks without speakers
    LPALCLOOPBACKOPENDEVICESOFT alcLoopbackOpenDeviceSOFT = nullptr;
    LPALCISRENDERFORMATSUPPORTEDSOFT alcIsRenderFormatSupportedSOFT = nullptr;
    LPALCRENDERSAMPLESSOFT alcRenderSamplesSOFT = nullptr;
    // AL
    LPALGETERROR alGetError = nullptr;
    LPALISEXTENSIONPRESENT alIsExtensionPresent = nullptr;
    LPALGETENUMVALUE alGetEnumValue = nullptr;
    LPALGETSTRING alGetString = nullptr;
    LPALDISTANCEMODEL alDistanceModel = nullptr;
    LPALDOPPLERFACTOR alDopplerFactor = nullptr;
    LPALLISTENERF alListenerf = nullptr;
    LPALLISTENER3F alListener3f = nullptr;
    LPALLISTENERFV alListenerfv = nullptr;
    LPALGENSOURCES alGenSources = nullptr;
    LPALDELETESOURCES alDeleteSources = nullptr;
    LPALSOURCEI alSourcei = nullptr;
    LPALSOURCEF alSourcef = nullptr;
    LPALSOURCE3F alSource3f = nullptr;
    LPALGETSOURCEI alGetSourcei = nullptr;
    LPALGETSOURCEF alGetSourcef = nullptr;
    LPALSOURCEPLAY alSourcePlay = nullptr;
    LPALSOURCEPAUSE alSourcePause = nullptr;
    LPALSOURCESTOP alSourceStop = nullptr;
    LPALSOURCEREWIND alSourceRewind = nullptr;
    LPALSOURCEQUEUEBUFFERS alSourceQueueBuffers = nullptr;
    LPALSOURCEUNQUEUEBUFFERS alSourceUnqueueBuffers = nullptr;
    LPALGENBUFFERS alGenBuffers = nullptr;
    LPALDELETEBUFFERS alDeleteBuffers = nullptr;
    LPALBUFFERDATA alBufferData = nullptr;
    LPALBUFFERIV alBufferiv = nullptr;

    // EFX filters (null if OpenAL Soft hasn't them)
    LPALGENFILTERS alGenFilters = nullptr;
    LPALDELETEFILTERS alDeleteFilters = nullptr;
    LPALFILTERI alFilteri = nullptr;
    LPALFILTERF alFilterf = nullptr;
    bool HasFilters() const { return alGenFilters != nullptr; }

    void* module = nullptr;   // the DLL (an HMODULE)
    std::wstring path;        // where it was found
    std::wstring folder;      // where to look, with a final backslash (empty: beside this DLL or program)

    // Loads soft_oal.dll from `folder` (or beside this DLL) and finds its functions. False with why in error.
    bool Load(std::string& error);
    void Unload();
};

// The folder this DLL (or program) was loaded from, with a final backslash.
std::wstring OwnFolder();

}  // namespace rfa_audio
