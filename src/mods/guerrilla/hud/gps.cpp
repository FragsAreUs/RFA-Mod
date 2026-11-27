// Guerrilla's GPS route (see gps.h). Guerrilla (RM addresses):
// - cartographer::process (0x87CE30): every 2 s (Minimap_gps_delay 0x16611BC) update_gps (0x8739B0): with a beacon, the
//   beacon moved onto the object it is attached to, taken away within 20 m of the player (2D, Minimap_gps_radius), then
//   m_valid_gps_path = calculate_gps_route(&m_gps_path, player, beacon, !valid). (Its GPS target boxes, object_bounding_box
//   type 1, aren't among our worlds' objects: not ported.) m_gps_index falls 16 a second (GPS_trail_rate) and starts
//   again at 16 below 0.
// - calculate_gps_route (0x873480): rfg_pf_create_route_segment(start, end, out, true) (Armageddon's 0x5C83D0, the same
//   code: both ends snapped to a road within 250 m, the AI vehicles' road search), a route of 2 points or more; else
//   none (its straight-line fallback returns false too: never drawn). m_first_gps_point = the route's first point. The
//   curve's knots: within 10 m of a road (object_road_get_closest_road(player, true, 10, true, false)), [start, start,
//   route..., last]; farther, [route[0], route..., last] (the way to the road not drawn). At most 512.
// - create_path_route_points (0x86C650): a Catmull-Rom curve through the knots (cardinal_spline, tension 0; Armageddon's
//   get_point 0xCA7710, 3D with the heights: the same per component) walked from u = 1 in steps of 0.1 to the knots - 2,
//   a point every `spacing` metres (an overshoot pulled back to exactly that), each with a running number (dropped
//   points counted too); kept within the map's reach; at most 384.
// The road search runs on the game's main thread (the HUD update), as Guerrilla's did, through the road stage our
// guerrilla_vehicles part guards (RoadStage).
#define RFA_MOD_NAME "guerrilla_hud"
#include "rfa_mod.h"
#include "gps.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace gps {
namespace {

struct V3 {
    float x, y, z;
};

// Armageddon's farray<vector,512> (no table of functions, unlike Guerrilla's): its data, room and count, then the room.
constexpr int kMostRoutePoints = 512;
struct RouteArray {
    V3* data;
    int capacity;
    int count;
    V3 storage[kMostRoutePoints];
};
using CreateRouteSegmentFn = bool(__cdecl*)(const V3* start, const V3* end, RouteArray* out, bool addStart);
auto const CreateRouteSegment = reinterpret_cast<CreateRouteSegmentFn>(0x5C83D0);
// object_road_get_closest_road's short form (pos, flag, within, flag, gps only).
using ClosestRoadFn = BYTE*(__cdecl*)(const float* where, bool special, float within, bool unknown, bool gpsOnly);
auto const ClosestRoad = reinterpret_cast<ClosestRoadFn>(0x7F6E10);
// Its long form, as rfg_pf_create_route_segment snaps each end (0x5C84B9: pos, 1, the road's point out, the distance
// out, 250, 1, 0, 0, 1): for the log when a search finds nothing.
using ClosestRoadLongFn = BYTE*(__cdecl*)(const float* where, bool special, float* point, float* distance, float within, bool unknown,
                                          DWORD a, DWORD b, bool other);
auto const ClosestRoadLong = reinterpret_cast<ClosestRoadLongFn>(0x7F6E50);
constexpr float kSnapReach = 250.0f;       // rfg_pf_create_route_segment's (0xF9AFC0)
// For the log when a search finds nothing. The road stage's search (0x5C87B0) is a session on the route map 0x13CB39C
// (begun at 0x5C966D by 0x5ED940): +4 the session's number, +8 the nodes made, +0x10 those still open, +0x18 its room,
// what the Common_mempool (0x26AF908) has free at the start (+0x44 its end, less +0x4C its top) over 60 bytes a node,
// at most 3,064. It starts from the far end's road and goes along each road and across its junctions (the next road by
// handle: world::object_from_handle 0x6CB9E0), and has the route when it comes to the start's road (that object).
constexpr DWORD kRouteMap = 0x13CB39C, kCommonMempool = 0x26AF908, kWorld = 0x1870630;
constexpr DWORD kObjectHandle = 0x68;
constexpr DWORD kRoadJunctionCount = 0xA0, kRoadJunctions = 0xA4, kJunctionBytes = 8;  // as the search reads them (0x5C9B76)
auto const ObjectFromHandle = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, DWORD handle)>(0x6CB9E0);
// The world's roads (as the closest-road search 0x7F6E50 lists them): how many, and each.
auto const RoadCount = reinterpret_cast<int(__fastcall*)(void* world, void* unused)>(0x448390);
auto const RoadAt = reinterpret_cast<BYTE*(__fastcall*)(void* world, void* unused, int index)>(0x4483E0);
// cardinal_spline::get_point (thiscall: the spline {points, count, tension}, the point out, u).
struct Spline {
    const V3* points;
    int count;
    float tension;
};
using SplinePointFn = void(__thiscall*)(const Spline* spline, V3* out, float u);
auto const SplinePoint = reinterpret_cast<SplinePointFn>(0xCA7710);
// The ground's height under (x, z), from the top (0x7748E0: its ray from *height + 1 down; guerrilla_people's GroundUnder).
using GroundHeightFn = bool(__cdecl*)(float* height, float x, float z, float* normal, bool upward);
auto const GroundHeight = reinterpret_cast<GroundHeightFn>(0x7748E0);
constexpr float kRayFromTheTop = 998.0f;

constexpr double kDelaySeconds = 2.0;     // Minimap_gps_delay
constexpr float kTrailRate = 16.0f;       // GPS_trail_rate
constexpr float kTrailPeriod = 16.0f;
constexpr float kNearRoad = 10.0f;        // Min_GPS_dist
constexpr int kMostMapPoints = 384;       // pts: farray<vector,384>

// A road in two objects (2026-11-26 14:07 run: 51 of the world's 312 road objects). Our builder writes each of
// Guerrilla's roads into its zone and again into the zone's persistent p_ zone, as Guerrilla has them. Guerrilla's world
// keeps one object per handle (a zone's object whose handle has a stub reuses it: world::visit_stub RM 0x93B860);
// Armageddon's makes a second one while the zone is loaded. The road search crosses junctions by handle
// (world::object_from_handle), so it only ever reaches the object a handle names: a search whose ends were snapped onto
// the other copies used up all the roads it could reach (2,921 nodes) and found nothing, every time. So while the GPS's
// search runs, the closest-road search (0x7F6E50: the snaps of rfg_pf_create_route_segment and of its road stage) gives
// the object the road's handle names in place of a second copy. The copies are the same bytes, so the snap's point and
// place along the road are the same. Other callers get the game's answer as it is.
bool g_inSearch = false;
int g_swapped = 0;
ClosestRoadLongFn g_closestRoad = nullptr;  // the game's own, once hooked

BYTE* __cdecl ClosestRoadNamed(const float* where, bool special, float* point, float* distance, float within, bool unknown, DWORD a, DWORD b,
                               bool other) {
    BYTE* road = g_closestRoad(where, special, point, distance, within, unknown, a, b, other);
    if (!road || !g_inSearch) return road;
    const DWORD handle = *reinterpret_cast<const DWORD*>(road + kObjectHandle);
    BYTE* named = ObjectFromHandle(reinterpret_cast<void*>(kWorld), nullptr, handle);
    if (!named || named == road) return road;
    ++g_swapped;
    RFA_LOG_DETAIL_FIRST(10, "GPS: a road search's end was on road 0x%08lX's second copy; it gets the one the handle names", handle);
    return named;
}

double g_timer = 0.0;
float g_index = kTrailPeriod;
bool g_valid = false;
bool g_nearRoad = false;
V3 g_first{};
std::vector<V3> g_knots;
int g_searches = 0;

}  // namespace

void Start() {
    const BYTE opening[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x8C, 0x08, 0x00, 0x00};  // push ebp / mov ebp,esp / sub esp,88Ch
    if (memcmp(reinterpret_cast<const void*>(0x7F6E50), opening, sizeof(opening)) != 0) {
        rfa::Log("GPS: the closest-road search (0x7F6E50) isn't as expected; a route whose end is on a road's second copy won't be found");
        return;
    }
    g_closestRoad = reinterpret_cast<ClosestRoadLongFn>(rfa::HookFunction(0x7F6E50, sizeof(opening), reinterpret_cast<void*>(&ClosestRoadNamed)));
    if (!g_closestRoad) rfa::Log("GPS: could not hook the closest-road search (0x7F6E50); a route whose end is on a road's second copy won't be found");
}

void Forget() {
    g_timer = 0.0;
    g_index = kTrailPeriod;
    Clear();
}

void Clear() {
    g_valid = false;
    g_knots.clear();
    g_first = {};
}

bool Tick(double seconds) {
    g_index -= kTrailRate * static_cast<float>(seconds);
    if (g_index < 0.0f) g_index = kTrailPeriod;
    g_timer += seconds;
    if (g_timer < kDelaySeconds) return false;
    g_timer = 0.0;
    return true;
}

void Search(const float* start, const float* end) {
    g_first = {};
    static RouteArray route;  // 6 KB: not on the stack
    route.data = route.storage;
    route.capacity = kMostRoutePoints;
    route.count = 0;
    const V3 from{start[0], start[1], start[2]};
    V3 to{end[0], end[1], end[2]};
    // A beacon with no height (a safehouse's, from map_icons.txt) on the ground under it, so the road search's 250 m
    // reach is measured from there.
    if (to.y == 0.0f) {
        float height = kRayFromTheTop;
        if (GroundHeight(&height, to.x, to.z, nullptr, false)) to.y = height;
    }
    const BYTE* map = reinterpret_cast<const BYTE*>(kRouteMap);
    const BYTE* mempool = reinterpret_cast<const BYTE*>(kCommonMempool);
    const DWORD sessionBefore = *reinterpret_cast<const DWORD*>(map + 4);
    const long mempoolFree = static_cast<long>(*reinterpret_cast<const DWORD*>(mempool + 0x44) - *reinterpret_cast<const DWORD*>(mempool + 0x4C));
    const ULONGLONG began = GetTickCount64();
    const int swappedBefore = g_swapped;
    g_inSearch = g_closestRoad != nullptr;
    const bool found = CreateRouteSegment(&from, &to, &route, true) && route.count >= 2;
    g_inSearch = false;
    const int swapped = g_swapped - swappedBefore;
    const ULONGLONG took = GetTickCount64() - began;
    const bool searched = *reinterpret_cast<const DWORD*>(map + 4) != sessionBefore;
    const int nodes = *reinterpret_cast<const int*>(map + 8), open = *reinterpret_cast<const int*>(map + 0x10),
              room = *reinterpret_cast<const int*>(map + 0x18);
    if (++g_searches <= 5 || took > 50)
        RFA_LOG_DETAIL_FIRST(40, "GPS: the road search from (%.0f, %.0f) to (%.0f, %.0f): %s, %d points, %llu ms; %s; %d snaps moved off a second copy",
                             from.x, from.z, to.x, to.z, found ? "a route" : "none", route.count, took,
                             searched ? "its road search ran" : "no road search ran", swapped);
    if (!found) {
        g_valid = false;
        // Why, for the first failures: each end's road as the search snaps it (none within 250 m: that end's roads
        // aren't loaded, or there are none), whether that road is the object its handle names (a second road object with
        // the same handle, from the zone and its persistent zone, would never be reached across a junction), and the
        // search's own count (no room: the mempool; all its room used; or the roads it reached all used up).
        static int failures = 0;
        if (++failures <= 10) {
            float startPoint[3] = {}, endPoint[3] = {};
            float startDistance = -1.0f, endDistance = -1.0f;
            BYTE* startRoad = ClosestRoadLong(&from.x, true, startPoint, &startDistance, kSnapReach, true, 0, 0, true);
            BYTE* endRoad = ClosestRoadLong(&to.x, true, endPoint, &endDistance, kSnapReach, true, 0, 0, true);
            void* world = reinterpret_cast<void*>(kWorld);
            auto described = [&](BYTE* road, char* out, size_t size) {
                if (!road) {
                    snprintf(out, size, "NONE within 250 m");
                    return;
                }
                const DWORD handle = *reinterpret_cast<const DWORD*>(road + kObjectHandle);
                const BYTE* named = ObjectFromHandle(world, nullptr, handle);
                snprintf(out, size, "0x%08lX (%s)", handle,
                         named == road ? "the object its handle names" : named ? "NOT the object its handle names: a second copy" : "its handle names nothing");
            };
            char startText[96], endText[96];
            described(startRoad, startText, sizeof(startText));
            described(endRoad, endText, sizeof(endText));
            // The world's roads: those whose handle names another object or none, and their junctions (a road's +0xA0
            // count, +0xA4 the list, 8 bytes each: the other road's handle, this point, its point) whose road the search
            // can't find by handle.
            const int roads = RoadCount(world, nullptr);
            int copies = 0, unnamed = 0, junctions = 0, deadJunctions = 0;
            for (int i = 0; i < roads; ++i) {
                BYTE* road = RoadAt(world, nullptr, i);
                if (!road) continue;
                const BYTE* named = ObjectFromHandle(world, nullptr, *reinterpret_cast<const DWORD*>(road + kObjectHandle));
                if (!named) ++unnamed;
                else if (named != road) ++copies;
                const int links = *reinterpret_cast<const int*>(road + kRoadJunctionCount);
                const BYTE* link = *reinterpret_cast<BYTE* const*>(road + kRoadJunctions);
                for (int j = 0; link && j < links; ++j, link += kJunctionBytes) {
                    ++junctions;
                    if (!ObjectFromHandle(world, nullptr, *reinterpret_cast<const DWORD*>(link))) ++deadJunctions;
                }
            }
            rfa::Log("GPS: no route from (%.0f, %.0f, %.0f) to (%.0f, %.0f, %.0f): the start's road %s, %.0f m; the end's %s, %.0f m", from.x, from.y,
                     from.z, to.x, to.y, to.z, startText, startDistance, endText, endDistance);
            rfa::Log("GPS:   the search: %s; %d nodes made of room for %d (%ld bytes free in the Common_mempool), %d still open; the world's "
                     "roads %d: %d not the object their handle names, %d whose handle names nothing; junctions %d, %d to a road its handle "
                     "doesn't find",
                     searched ? "ran" : "DIDN'T RUN (an early way out)", nodes, room, mempoolFree, open, roads, copies, unnamed, junctions,
                     deadJunctions);
        }
        return;
    }
    RFA_LOG_DETAIL_FIRST(5, "GPS: the route's search made %d nodes of room for %d", nodes, room);
    const int count = route.count < kMostRoutePoints ? route.count : kMostRoutePoints;
    g_first = route.storage[0];
    g_nearRoad = ClosestRoad(start, true, kNearRoad, true, false) != nullptr;
    g_knots.clear();
    if (g_nearRoad) {
        g_knots.push_back(from);
        g_knots.push_back(from);
    } else {
        g_knots.push_back(route.storage[0]);
    }
    for (int i = 0; i < count && static_cast<int>(g_knots.size()) < kMostRoutePoints - 1; ++i) g_knots.push_back(route.storage[i]);
    g_knots.push_back(route.storage[count - 1]);
    g_valid = true;
}

bool Valid() { return g_valid; }
const float* FirstPoint() { return &g_first.x; }
bool PlayerNearRoad() { return g_nearRoad; }
float Index() { return g_index; }

void MapPoints(std::vector<Point>& out, float spacing, float centreX, float centreZ, float radius) {
    out.clear();
    if (!g_valid || g_knots.size() < 4 || spacing <= 0.0f) return;
    const Spline spline{g_knots.data(), static_cast<int>(g_knots.size()), 0.0f};
    const float last = static_cast<float>(g_knots.size()) - 2.0f;
    V3 previous{};
    SplinePoint(&spline, &previous, 1.0f);
    float along = 0.0f;
    int seq = 0;
    for (float u = 1.1f; u <= last + 1e-4f && static_cast<int>(out.size()) < kMostMapPoints; u += 0.1f) {
        V3 at{};
        SplinePoint(&spline, &at, u);
        float dx = at.x - previous.x, dz = at.z - previous.z;
        const float step = std::sqrt(dx * dx + dz * dz);
        along += step;
        if (along >= spacing && step > 1e-4f) {
            // An overshoot pulled back along the way to exactly `spacing`.
            const float back = (along - spacing) / step;
            V3 point{at.x - dx * back, at.y, at.z - dz * back};
            along = 0.0f;
            const float ox = point.x - centreX, oz = point.z - centreZ;
            if (ox * ox + oz * oz <= radius * radius) out.push_back({point.x, point.z, static_cast<float>(seq)});
            ++seq;
            at = point;
        }
        previous = at;
    }
}

}  // namespace gps
