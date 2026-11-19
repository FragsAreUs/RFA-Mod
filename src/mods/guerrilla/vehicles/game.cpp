// The shared pieces of game.h: small helpers, and reading what GuerrillaBuilder.exe wrote.
#define RFA_MOD_NAME "guerrilla_vehicles"
#include "rfa_mod.h"
#include "campaign/campaign_api.h"
#include "game.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace gv {

float Random() { return static_cast<float>(std::rand()) / RAND_MAX; }
int RandomBetween(int low, int high) {  // the whole range, however long (RAND_MAX is only 32767)
    return high <= low ? low : low + static_cast<int>(std::rand() / (RAND_MAX + 1.0) * (high - low + 1));
}

void Orientation(const V3& forward, float* m) {
    const V3 up{0, 1, 0}, right{forward.z, 0, -forward.x};
    const float values[9] = {right.x, right.y, right.z, up.x, up.y, up.z, forward.x, forward.y, forward.z};
    memcpy(m, values, sizeof(values));
}

void SitOut(BYTE* npc) {
    BYTE* ai = *reinterpret_cast<BYTE**>(npc + kNpcAi);
    if (ai) ai[kYield] |= 2;
}

BYTE* VehicleEntry(const std::string& name) {
    BYTE* entries = *reinterpret_cast<BYTE* const*>(kVehicleList);
    const int count = *reinterpret_cast<const int*>(kVehicleList + 8);
    for (int i = 0; entries && i < count; ++i) {
        const char* entryName = *reinterpret_cast<const char* const*>(entries + i * kVehicleEntrySize);
        if (entryName && _stricmp(entryName, name.c_str()) == 0) return entries + i * kVehicleEntrySize;
    }
    return nullptr;
}

BYTE* PickVehicle(const std::vector<std::string>& names) {
    std::vector<BYTE*> entries, loaded;
    for (const std::string& name : names)
        if (BYTE* entry = VehicleEntry(name)) entries.push_back(entry);
    for (BYTE* entry : entries)
        if (VehicleLoaded(entry)) loaded.push_back(entry);
    const auto& from = loaded.empty() ? entries : loaded;
    return from.empty() ? nullptr : from[std::rand() % from.size()];
}

bool RoadPlace(const V3& around, V3& where, V3& forward) {
    BYTE* road = ClosestRoad(&around.x, false, 60.0f, false, false);
    if (!road || !(*reinterpret_cast<const DWORD*>(road + 0xB4) & 2)) return false;  // road flags: 2 ambient
    const int count = *reinterpret_cast<const int*>(road + 0x98);
    const BYTE* points = *reinterpret_cast<BYTE* const*>(road + 0x9C);  // 28 bytes each: position, curve handle, width
    if (count < 2 || !points) return false;
    int best = 0;
    for (int i = 1; i < count; ++i)
        if (Distance(*reinterpret_cast<const V3*>(points + i * 28), around) < Distance(*reinterpret_cast<const V3*>(points + best * 28), around)) best = i;
    const int next = best + 1 < count ? best + 1 : best - 1;
    const V3 a = *reinterpret_cast<const V3*>(points + best * 28), b = *reinterpret_cast<const V3*>(points + next * 28);
    V3 along{b.x - a.x, 0, b.z - a.z};
    if (next < best) along = {-along.x, 0, -along.z};
    const float length = std::sqrt(along.x * along.x + along.z * along.z);
    if (length < 0.01f) return false;
    if (Random() < 0.5f) along = {-along.x, 0, -along.z};
    forward = {along.x / length, 0, along.z / length};
    const float width = *reinterpret_cast<const float*>(points + best * 28 + 24);
    // Its right (the up vector crossed with forward), half the road's width out: Guerrilla's traffic drives on the right.
    where = {a.x + forward.z * width * 0.5f, a.y + 0.5f, a.z - forward.x * width * 0.5f};
    return true;
}

void EndWait(BYTE* vehicle) {
    TimerClear(reinterpret_cast<int*>(vehicle + kWaitTimer), nullptr);
    TimerClear(reinterpret_cast<int*>(vehicle + kStuck1), nullptr);
    TimerClear(reinterpret_cast<int*>(vehicle + kStuck2), nullptr);
}

float Damage(BYTE* vehicle) {
    const int most = MaxHitPoints(vehicle), now = HitPoints(vehicle);
    return most > 0 ? std::clamp(1.0f - static_cast<float>(now) / most, 0.0f, 1.0f) : 0.0f;
}

namespace {
struct Made {
    DWORD vehicle;
    const char* maker;
};
std::vector<Made> g_made;
struct Age {
    DWORD vehicle;
    int ms;
};
std::vector<Age> g_ages;
}  // namespace

void NoteMadeBy(DWORD vehicle, const char* maker) {
    if (g_made.size() >= 256) g_made.erase(g_made.begin());
    g_made.push_back({vehicle, maker});
}

const char* MadeBy(DWORD vehicle) {
    for (auto made = g_made.rbegin(); made != g_made.rend(); ++made)
        if (made->vehicle == vehicle) return made->maker;
    return nullptr;
}

void NoteModelAge(DWORD vehicle, int ms) {
    if (g_ages.size() >= 256) g_ages.erase(g_ages.begin());
    g_ages.push_back({vehicle, ms});
}

int ModelAge(DWORD vehicle) {
    for (auto age = g_ages.rbegin(); age != g_ages.rend(); ++age)
        if (age->vehicle == vehicle) return age->ms;
    return -1;
}

// ---- The files

std::vector<std::string> Split(const std::string& text, char by) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in(text);
    while (std::getline(in, part, by))
        if (!part.empty()) parts.push_back(part);
    return parts;
}

namespace {

Data Read() {
    Data data;
    std::ifstream traffic(rfa::RootDir() + L"\\mods\\guerrilla_vehicles\\traffic.txt");
    for (std::string line; std::getline(traffic, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        std::istringstream in(line);
        std::string kind;
        in >> kind;
        if (kind == "grid") in >> data.zoneWidth >> data.columnAtZero >> data.rowAtZero;
        else if (kind == "zone") {
            Zone zone{};
            if (in >> zone.col >> zone.row >> zone.group >> zone.day >> zone.night >> zone.resource) data.zones.push_back(zone);
        } else if (kind == "group" || kind == "edf") {
            std::string name, vehicles;
            if (in >> name >> vehicles) (kind == "group" ? data.groups : data.edf).push_back({name, Split(vehicles, ',')});
        } else if (kind == "people") {
            int col, row;
            std::string resource;
            if (in >> col >> row >> resource) data.resources.push_back({{col, row}, resource});
        } else if (kind == "gunners") {
            std::string vehicles;
            if (in >> vehicles) data.gunnerVehicles = Split(vehicles, ',');
        } else if (kind == "dropoff") {
            std::string vehicles;
            if (in >> vehicles) data.dropOff = Split(vehicles, ',');
        }
    }
    std::ifstream crews(rfa::RootDir() + L"\\mods\\guerrilla_people\\crews.txt");
    for (std::string line; std::getline(crews, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto fields = Split(line, '\t');
        if (fields.size() == 3) data.people.push_back({{fields[0], fields[1]}, Split(fields[2], ',')});
    }
    return data;
}

std::vector<std::string> g_worlds;

}  // namespace

const Data& Files() {
    static const Data data = Read();
    return data;
}

static std::pair<int, int> ZoneOf(const V3& where) {
    const Data& data = Files();
    return {static_cast<int>(std::floor(where.x / data.zoneWidth)) + data.columnAtZero, data.rowAtZero - static_cast<int>(std::floor(where.z / data.zoneWidth))};
}

const Zone* TrafficZoneAt(const V3& where) {
    const auto [col, row] = ZoneOf(where);
    for (const Zone& zone : Files().zones)
        if (zone.col == col && zone.row == row) return &zone;
    return nullptr;
}

const std::string& ResourceAt(const V3& where) {
    static const std::string fallback = "rfg_Default";
    const auto at = ZoneOf(where);
    for (const auto& [zone, resource] : Files().resources)
        if (zone == at) return resource;
    return fallback;
}

const std::vector<std::string>* People(const std::string& resource, const char* team) {
    const std::vector<std::string>* fallback = nullptr;
    for (const auto& [key, names] : Files().people) {
        if (_stricmp(key.second.c_str(), team) != 0 || names.empty()) continue;
        if (_stricmp(key.first.c_str(), resource.c_str()) == 0) return &names;
        if (_stricmp(key.first.c_str(), "rfg_Default") == 0) fallback = &names;
    }
    return fallback;
}

const std::vector<std::string>* DropOffVehicles() { return Files().dropOff.empty() ? nullptr : &Files().dropOff; }

const std::vector<std::string>* EdfVehicles(const char* action) {
    for (const auto& [name, vehicles] : Files().edf)
        if (_stricmp(name.c_str(), action) == 0 && !vehicles.empty()) return &vehicles;
    return nullptr;
}

bool HasGunnerSeat(BYTE* vehicle) {
    BYTE* info = *reinterpret_cast<BYTE**>(vehicle + 0x3DA8);  // its vehicles.xtbl entry; the name first
    const char* name = info ? *reinterpret_cast<const char* const*>(info) : nullptr;
    if (!name) return false;
    for (const std::string& gunner : Files().gunnerVehicles)
        if (_stricmp(gunner.c_str(), name) == 0) return true;
    return false;
}

void ReadWorlds(const std::wstring& worlds) {
    std::string narrow;
    for (wchar_t c : worlds) narrow += static_cast<char>(c & 0x7F);  // world names are plain ASCII
    g_worlds = Split(narrow, ',');
}

// guerrilla_campaign's (campaign_api.h): green, nothing running and nothing liberated while it is off.
int CampaignAlert() {
    const int level = GuerrillaCampaign_Alert(nullptr);
    return level >= 0 && level <= 3 ? level : 0;
}

bool CampaignActivityRunning() { return GuerrillaCampaign_Running(nullptr, nullptr) != 0; }

bool PlayerDistrictLiberated() {
    GuerrillaCampaignDistrict info{};
    return GuerrillaCampaign_District(-1, &info) && info.liberated != 0;
}

bool InTrafficWorld() {
    for (const std::string& world : g_worlds)
        if (_stricmp(WorldName(), world.c_str()) == 0) return true;
    return false;
}

}  // namespace gv
