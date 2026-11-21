// The campaign's own save, an ini file next to the mod (Windows' profile functions read and write
// it as a plain file).
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "save.h"
#include "campaign.h"
#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <mutex>

namespace campaign::save {
namespace {

const std::wstring& File() {
    static const std::wstring file = ModFolder() + L"\\campaign_save.ini";
    return file;
}

std::wstring Wide(const char* text) { return std::wstring(text, text + strlen(text)); }

// What has been read, kept. Windows' profile functions open and search the file on every call, and some readers
// ask every frame (the car bomb's M.O.A.B. places, each up to twice a frame: 1.5 ms of an 8.4 ms frame in the
// 2026-11-21 profile). Only the functions here change the file, and each change forgets everything kept, so a read
// still gives what the file says. Kept by section, key and fallback (the answer when the key isn't there). A read
// that overlapped a change isn't kept: the generation it began in has passed.
std::mutex g_keptLock;
std::map<std::string, int> g_keptInts;
std::map<std::string, float> g_keptNumbers;
unsigned g_generation = 0;

std::string KeptName(const char* section, const char* key, const char* fallback) {
    return std::string(section) + '\n' + key + '\n' + fallback;
}

unsigned Generation() {
    std::lock_guard<std::mutex> lock(g_keptLock);
    return g_generation;
}

// After a change to the file: nothing kept is known to be what it says.
void Forget() {
    std::lock_guard<std::mutex> lock(g_keptLock);
    g_keptInts.clear();
    g_keptNumbers.clear();
    ++g_generation;
}

}  // namespace

int Int(const char* section, const char* key, int fallback) {
    char fallbackText[16];
    sprintf_s(fallbackText, "%d", fallback);
    const std::string name = KeptName(section, key, fallbackText);
    {
        std::lock_guard<std::mutex> lock(g_keptLock);
        const auto found = g_keptInts.find(name);
        if (found != g_keptInts.end()) return found->second;
    }
    const unsigned generation = Generation();
    const int value = static_cast<int>(GetPrivateProfileIntW(Wide(section).c_str(), Wide(key).c_str(), fallback, File().c_str()));
    std::lock_guard<std::mutex> lock(g_keptLock);
    if (generation == g_generation) g_keptInts[name] = value;
    return value;
}

float Number(const char* section, const char* key, float fallback) {
    char fallbackText[16];
    unsigned bits;
    memcpy(&bits, &fallback, sizeof(bits));
    sprintf_s(fallbackText, "%08X", bits);
    const std::string name = KeptName(section, key, fallbackText);
    {
        std::lock_guard<std::mutex> lock(g_keptLock);
        const auto found = g_keptNumbers.find(name);
        if (found != g_keptNumbers.end()) return found->second;
    }
    const unsigned generation = Generation();
    wchar_t text[64];
    GetPrivateProfileStringW(Wide(section).c_str(), Wide(key).c_str(), L"", text, 64, File().c_str());
    const float value = text[0] ? static_cast<float>(_wtof(text)) : fallback;
    std::lock_guard<std::mutex> lock(g_keptLock);
    if (generation == g_generation) g_keptNumbers[name] = value;
    return value;
}

void SetInt(const char* section, const char* key, int value) {
    CreateDirectoryW(ModFolder().c_str(), nullptr);
    WritePrivateProfileStringW(Wide(section).c_str(), Wide(key).c_str(), std::to_wstring(value).c_str(), File().c_str());
    Forget();
}

void SetNumber(const char* section, const char* key, float value) {
    CreateDirectoryW(ModFolder().c_str(), nullptr);
    wchar_t text[64];
    swprintf_s(text, L"%.6g", value);
    WritePrivateProfileStringW(Wide(section).c_str(), Wide(key).c_str(), text, File().c_str());
    Forget();
}

std::string ForObject(const char* kind, unsigned handle) {
    char name[64];
    sprintf_s(name, "%s %08X", kind, handle);
    return name;
}

}  // namespace campaign::save

namespace campaign::save {

std::vector<std::string> Keys(const char* section) {
    std::vector<std::string> keys;
    std::vector<wchar_t> text(65536);
    const DWORD length = GetPrivateProfileSectionW(Wide(section).c_str(), text.data(), static_cast<DWORD>(text.size()), File().c_str());
    for (const wchar_t* line = text.data(); line < text.data() + length && *line; line += wcslen(line) + 1) {
        std::string key;
        for (const wchar_t* c = line; *c && *c != L'='; ++c) key += static_cast<char>(*c);
        keys.push_back(key);
    }
    return keys;
}

std::string Bytes() {
    std::string bytes;
    FILE* file = nullptr;
    if (_wfopen_s(&file, File().c_str(), L"rb") != 0 || !file) return bytes;
    char buffer[16384];
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) bytes.append(buffer, read);
    fclose(file);
    return bytes;
}

void Replace(const unsigned char* data, int size) {
    // Windows' profile functions keep no copy: the file itself is the save, so replacing it, and forgetting what
    // Int and Number kept of the old one, is enough.
    DeleteFileW(File().c_str());
    if (data && size > 0) {
        CreateDirectoryW(ModFolder().c_str(), nullptr);
        FILE* file = nullptr;
        if (_wfopen_s(&file, File().c_str(), L"wb") == 0 && file) {
            fwrite(data, 1, static_cast<size_t>(size), file);
            fclose(file);
        }
    }
    Forget();
}

}  // namespace campaign::save
