// Guerrilla's GPS route: the road route from the player to the nav beacon, searched again every 2 s, drawn as arrows on
// the minimap and the world map. Part of guerrilla_hud (gps.cpp); the beacon itself and the Lua side are
// guerrilla_hud.cpp's ("The nav beacon").
#pragma once
#include <vector>

namespace gps {

// A map point (create_path_route_points RM 0x86C650): where it is (x, z metres) and its running number along the route.
struct Point {
    float x, z, seq;
};

// At the HUD's start: the closest-road search hooked, so the GPS's road search snaps onto the road object a handle names
// (not a road's second copy, from its zone and its persistent zone both loaded: gps.cpp).
void Start();
// A level's start: no route.
void Forget();
// cartographer::process (RM 0x87CE30), each HUD update in our worlds: the route's 2 s timer (Minimap_gps_delay) and the
// highlight's clock (m_gps_index). True when the 2 s ran out: the route is to be searched again now (update_gps).
bool Tick(double seconds);
// calculate_gps_route (RM 0x873480): the road route from `start` to `end` (x, y, z), or none.
void Search(const float* start, const float* end);
// No route (no beacon).
void Clear();
bool Valid();
// The route's road entry (m_first_gps_point), and whether the player was within 10 m of a road at the last search
// (Min_GPS_dist): off the road the beacon is drawn at the road entry (render_player_marker RM 0x871DF6).
const float* FirstPoint();
bool PlayerNearRoad();
// The arrows' points along the route, one every `spacing` metres, those within `radius` metres of (centreX, centreZ),
// at most 384 (pts: farray<vector,384>).
void MapPoints(std::vector<Point>& out, float spacing, float centreX, float centreZ, float radius);
// m_gps_index: the highlight's place, 16 to 0, falling 16 a second.
float Index();

}  // namespace gps
