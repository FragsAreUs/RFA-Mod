// The campaign's own save, an ini file next to the mod (Windows' profile functions read and write
// it as a plain file).
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "save.h"
#include "campaign.h"
#include <stdio.h>
#include <stdlib.h>

namespace campaign::save {
namespace {

const std::wstring& File() {
    static const std::wstring file = ModFolder() + L"\\campaign_save.ini";
    return file;
}

std::wstring Wide(const char* text) { return std::wstring(text, text + strlen(text)); }

}  // namespace

int Int(const char* section, const char* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(Wide(section).c_str(), Wide(key).c_str(), fallback, File().c_str()));
}

float Number(const char* section, const char* key, float fallback) {
    wchar_t text[64];
    GetPrivateProfileStringW(Wide(section).c_str(), Wide(key).c_str(), L"", text, 64, File().c_str());
    return text[0] ? static_cast<float>(_wtof(text)) : fallback;
}

void SetInt(const char* section, const char* key, int value) {
    CreateDirectoryW(ModFolder().c_str(), nullptr);
    WritePrivateProfileStringW(Wide(section).c_str(), Wide(key).c_str(), std::to_wstring(value).c_str(), File().c_str());
}

void SetNumber(const char* section, const char* key, float value) {
    CreateDirectoryW(ModFolder().c_str(), nullptr);
    wchar_t text[64];
    swprintf_s(text, L"%.6g", value);
    WritePrivateProfileStringW(Wide(section).c_str(), Wide(key).c_str(), text, File().c_str());
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
    // Windows' profile functions keep no copy: the file itself is the save, so replacing it is enough.
    DeleteFileW(File().c_str());
    if (!data || size <= 0) return;
    CreateDirectoryW(ModFolder().c_str(), nullptr);
    FILE* file = nullptr;
    if (_wfopen_s(&file, File().c_str(), L"wb") != 0 || !file) return;
    fwrite(data, 1, static_cast<size_t>(size), file);
    fclose(file);
}

}  // namespace campaign::save
