// The profiler (profiler.cpp): how much of the game's main and render threads' time our code takes, sampled.
// For development: off unless [Loader] Profile=1.
#pragma once
#include <windows.h>
#include <vector>

// Starts the profiler when [Loader] Profile=1. `mods` are the loaded mods: their code and the loader's count as ours.
void StartProfiler(const std::vector<HMODULE>& mods);

// Each frame, from the swap chain's Present (menus.cpp): counts the frames and notes the thread that presents.
void ProfilerFramePresented();
