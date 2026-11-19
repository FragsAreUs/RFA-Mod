// How many cars the world holds at once, for every part of guerrilla_vehicles that needs it.
//
// "Cars" here are the game's automobiles: everything on wheels or treads (cars, trucks, ATVs, tanks); flyers
// and walkers have pools of their own (8 and 4, the same in both games). The game's own pool holds 16 at
// once; Guerrilla's holds 32. At the user's ask (2026-11-13: "we need to raise the limit on the amount of
// cars that can be spawned") it holds 64: the automobile pool and the world's list of them are made that
// big (guerrilla_vehicles.cpp), and Guerrilla's rules on a full world keep their distance from it
// (traffic.cpp): no new traffic car above it - 4 (Guerrilla's 28 of 32), and from it - 2 on (Guerrilla's
// 30) the farthest empty car out of view is taken away.
#pragma once

namespace gv {

constexpr int kGameCars = 16, kGuerrillaCars = 32, kCarsAtOnce = 64;

}  // namespace gv
