// Guerrilla's restricted areas: its EDF bases (object_restricted_area, 40 in its zones, which
// Armageddon has no class for). Each is a "yellow" polygon over the ground, in which:
// - an EDF soldier who sees the player takes him for an enemy, whatever the alert level
//   (ai_perception::record_los_result_unfriendly → go_hostile_on_unfriendly_target; undercover.cpp);
// - the alert level doesn't fall (gsm_do_frame; liberation.cpp).
// Around it a wider "warning" polygon (on Guerrilla's map; kept here for it). An area may be disabled
// (by its zone, or by a mission's script: enable_restricted_area, disable_restricted_area), and may let
// ambient people in (allow_ambient_peds; Guerrilla's ambient spawner and fleeing civilians ask).
//
// How Guerrilla tests a point (object_restricted_area::is_inside, 0xA1B310 in Re-Mars-tered): not
// disabled, inside the polygon's bounding box on the ground (x, z), then inside the polygon
// (restricted_area_2d_point_in_polygon). For a human in a vehicle the vehicle's place counts
// (ai_defense_is_human_in_restricted_area, 0x5FB6D0); for the player driving, Guerrilla tests the four
// corners of the vehicle's box too: here the vehicle's middle only.
//
// The areas come from the player's own copy of Guerrilla: editor\GuerrillaBuilder.exe writes them to
// mods\guerrilla_campaign\restricted_areas.txt with the people.
#define RFA_MOD_NAME "guerrilla_campaign"
#include "rfa_mod.h"
#include "rfa_guerrilla.h"
#include "campaign_api.h"
#include "restricted_areas.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace campaign::restricted_areas {
namespace {

struct Point {
    float x, z;
};

struct Area {
    unsigned handle = 0;
    std::string zone;
    bool disabled = false, allowAmbientPeople = false;
    std::vector<Point> yellow, warning;
    float minX = 0, minZ = 0, maxX = 0, maxZ = 0;  // the yellow polygon's box
};

std::vector<Area> g_areas;
int g_playerIn = -1;     // the area the player is in, or -1
bool g_said = false;     // the log has how many there are

constexpr DWORD kInVehicle = 0x1160;  // a human: the vehicle it is in (a handle; -1 on foot)

std::vector<Point> ReadPolygon(std::istringstream& in) {
    int count = 0;
    std::vector<Point> points;
    if (!(in >> count) || count < 0 || count > 64) return points;
    for (int i = 0; i < count; ++i) {
        Point p{};
        if (!(in >> p.x >> p.z)) return {};
        points.push_back(p);
    }
    return points;
}

// restricted_area_2d_point_in_polygon: the crossings of a ray from the point.
bool InPolygon(const std::vector<Point>& polygon, float x, float z) {
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Point& a = polygon[i];
        const Point& b = polygon[j];
        if ((a.z > z) != (b.z > z) && x < (b.x - a.x) * (z - a.z) / (b.z - a.z) + a.x) inside = !inside;
    }
    return inside;
}

// How far a point is from a polygon's edge, over the ground.
float DistanceToEdge(const std::vector<Point>& polygon, float x, float z) {
    float best = 1e30f;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const float ax = polygon[j].x, az = polygon[j].z, dx = polygon[i].x - ax, dz = polygon[i].z - az;
        const float length = dx * dx + dz * dz;
        float t = length > 0.0f ? ((x - ax) * dx + (z - az) * dz) / length : 0.0f;
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        const float ex = ax + t * dx - x, ez = az + t * dz - z;
        const float distance = sqrtf(ex * ex + ez * ez);
        best = distance < best ? distance : best;
    }
    return best;
}

// Where the player counts as being: his vehicle's place if he is in one.
Vector PlayerPosition() {
    void* player = Player();
    if (!player) return {};
    const unsigned vehicle = *reinterpret_cast<const unsigned*>(static_cast<const BYTE*>(player) + kInVehicle);
    void* vehicleObject = vehicle != 0xFFFFFFFF ? ObjectFromHandle(vehicle) : nullptr;
    return PositionOf(vehicleObject ? vehicleObject : player);
}

int AreaAt(float x, float z, bool forAmbientPeople) {
    for (size_t i = 0; i < g_areas.size(); ++i) {
        const Area& area = g_areas[i];
        if (area.disabled || (forAmbientPeople && area.allowAmbientPeople)) continue;
        if (x < area.minX || x > area.maxX || z < area.minZ || z > area.maxZ) continue;
        if (InPolygon(area.yellow, x, z)) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

void Init() {
    std::ifstream file(ModFolder() + L"\\restricted_areas.txt");
    for (std::string line; std::getline(file, line);) {
        std::istringstream in(line);
        std::string word, handle;
        if (!(in >> word) || word != "area") continue;
        Area area;
        int disabled = 0, ambient = 0;
        if (!(in >> handle >> area.zone >> disabled >> ambient)) continue;
        area.handle = static_cast<unsigned>(strtoul(handle.c_str(), nullptr, 16));
        area.disabled = disabled != 0;
        area.allowAmbientPeople = ambient != 0;
        area.yellow = ReadPolygon(in);
        area.warning = ReadPolygon(in);
        if (area.yellow.size() < 3) continue;
        area.minX = area.maxX = area.yellow[0].x;
        area.minZ = area.maxZ = area.yellow[0].z;
        for (const Point& p : area.yellow) {
            area.minX = p.x < area.minX ? p.x : area.minX;
            area.maxX = p.x > area.maxX ? p.x : area.maxX;
            area.minZ = p.z < area.minZ ? p.z : area.minZ;
            area.maxZ = p.z > area.maxZ ? p.z : area.maxZ;
        }
        g_areas.push_back(area);
    }
    if (g_areas.empty())
        rfa::Log("No restricted_areas.txt (editor\\GuerrillaBuilder.exe build people writes it): the EDF's bases are no restricted areas");
}

void Enter() {
    g_playerIn = -1;
    if (!g_said) {
        g_said = true;
        if (!g_areas.empty()) rfa::Log("restricted_areas.txt: %u of Guerrilla's restricted areas (its EDF bases)", static_cast<unsigned>(g_areas.size()));
    }
}

void Frame() {
    if (!Player()) return;
    const Vector at = PlayerPosition();
    const int area = AreaAt(at.x, at.z, false);
    if (area == g_playerIn) return;
    if (area >= 0)
        rfa::Log("The player is in the EDF's restricted area 0x%08X (%s), at (%.0f, %.0f, %.0f): the EDF who see him there take him for an enemy, and the alert level doesn't fall",
                 g_areas[area].handle, g_areas[area].zone.c_str(), at.x, at.y, at.z);
    else
        rfa::Log("The player left the EDF's restricted area 0x%08X (%s), at (%.0f, %.0f, %.0f)", g_areas[g_playerIn].handle, g_areas[g_playerIn].zone.c_str(), at.x, at.y,
                 at.z);
    g_playerIn = area;
}

std::string PlayerPlace() {
    if (!Player()) return "(no player)";
    const Vector at = PlayerPosition();
    char text[200];
    if (g_playerIn >= 0) {
        sprintf_s(text, "at (%.0f, %.0f, %.0f), in the EDF's restricted area 0x%08X (%s)", at.x, at.y, at.z, g_areas[g_playerIn].handle, g_areas[g_playerIn].zone.c_str());
        return text;
    }
    int nearest = -1;
    float distance = 1e30f;
    for (size_t i = 0; i < g_areas.size(); ++i) {
        if (g_areas[i].disabled) continue;
        const float d = DistanceToEdge(g_areas[i].yellow, at.x, at.z);
        if (d < distance) {
            distance = d;
            nearest = static_cast<int>(i);
        }
    }
    if (nearest < 0) sprintf_s(text, "at (%.0f, %.0f, %.0f)", at.x, at.y, at.z);
    else
        sprintf_s(text, "at (%.0f, %.0f, %.0f), %.0f m from the EDF's restricted area 0x%08X (%s)", at.x, at.y, at.z, distance, g_areas[nearest].handle,
                  g_areas[nearest].zone.c_str());
    return text;
}

bool PlayerInside() { return g_playerIn >= 0; }

bool Inside(float x, float z, bool forAmbientPeople) { return AreaAt(x, z, forAmbientPeople) >= 0; }

}  // namespace campaign::restricted_areas

// For the other mods (the people's ambient walkers, the map): whether a point on the ground is inside
// one of Guerrilla's restricted areas; with forAmbientPeople, the areas that let ambient people in
// don't count.
bool GuerrillaCampaign_InRestrictedArea(float x, float z, bool forAmbientPeople) {
    return guerrilla_campaign::Running() && campaign::restricted_areas::Inside(x, z, forAmbientPeople);
}
