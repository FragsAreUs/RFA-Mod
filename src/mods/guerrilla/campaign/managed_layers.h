// Guerrilla's managed layers for its activities, on Armageddon's own managed layer code (managed_layers.cpp).
#pragma once

namespace campaign::managed_layers {

// Checks Armageddon's managed layer functions; at start-up.
void Init();
// A run of the activity (its rf4_layer_definition.xtbl set, named as the activity) starts loading its layers
// (managed_layer_set::start_layer_loading). False when the set isn't there or can't load now (logged).
bool Request(const char* activity);
// Its layers' files are in (layer_resources_loaded).
bool Loaded(const char* activity);
// Its layers' objects made in the world (create_managed_layer → serialize_layer_objects), once.
void Create(const char* activity);
// Its layers' objects taken away and its files let go (start_layer_unloading).
void Release(const char* activity);
// Whether an object (by handle) is one of the loaded layer's (member_of_layer).
bool Member(unsigned handle);
// A level started: nothing is loaded (the game unloads every set as a level shuts down).
void LevelInit();

}  // namespace campaign::managed_layers
