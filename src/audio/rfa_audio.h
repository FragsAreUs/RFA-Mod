/*
 * rfa_audio: RFA-Mod's own sound engine for the Guerrilla sound banks GuerrillaBuilder writes (Wwise bank
 * version 48, the 8 object kinds our writer makes), played through OpenAL Soft.
 *
 * It knows nothing of the game. The editor uses it (bin\editor\rfa_audio.dll, x64) to preview our banks and
 * GuerrillaBuilder to check them without speakers (a loopback device); the game will use the same sources
 * (x86, in rfa_guerrilla.dll) to play our banks itself while Wwise keeps Armageddon's.
 *
 * OpenAL Soft (soft_oal.dll, LGPL-2.0-or-later) is loaded from this DLL's own folder when rfa_audio_open is
 * called; without it, rfa_audio_open fails and says why.
 *
 * Every function may be called from any thread: one lock guards the engine. The end callback is called
 * outside that lock (from rfa_audio_tick, rfa_audio_render or the engine's own thread), so it may call back in.
 *
 * Positions are the game's: x right, y up, z forward (left-handed), in metres. Ids are Wwise's: FNV-1 of the
 * lower-case name. A game object is any non-zero number the caller chooses; it is made when first used.
 *
 * Copyright (C) RFA-Mod contributors. GPL-3.0-or-later.
 */
#ifndef RFA_AUDIO_H
#define RFA_AUDIO_H

#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(RFA_AUDIO_BUILD_DLL)
#define RFA_AUDIO_API __declspec(dllexport)
#elif defined(RFA_AUDIO_STATIC)
#define RFA_AUDIO_API
#else
#define RFA_AUDIO_API __declspec(dllimport)
#endif

/* ---- Starting and stopping ------------------------------------------------------------------------- */

typedef struct rfa_audio_settings {
    int loopback;          /* 1: no speakers; sound is made only by rfa_audio_render (for checks) */
    int sample_rate;       /* 0: the device's own (loopback: 48000) */
    int max_sources;       /* OpenAL sources at most (0: 128); past them, priority decides */
    int cache_mb;          /* decoded sounds kept in memory (0: 64) */
    int own_thread;        /* 1: the engine updates itself every 10 ms (the editor); 0: the caller calls rfa_audio_tick */
    int hrtf;              /* 1: ask OpenAL Soft for HRTF (headphones) */
    float volume_threshold_db; /* too quiet to hear below this (0: -96.3, Wwise's default as Armageddon's init.bnk_pc has it) */
} rfa_audio_settings;

/* Opens OpenAL Soft and the engine. 1 on success; else 0, with why in error. Opening again first closes. */
RFA_AUDIO_API int rfa_audio_open(const rfa_audio_settings* settings, char* error, int error_size);

/* Stops everything, unloads every bank and closes OpenAL. */
RFA_AUDIO_API void rfa_audio_close(void);

/* The OpenAL device's name, its output rate and whether HRTF is on, as text ("" when closed). */
RFA_AUDIO_API int rfa_audio_device_info(char* text, int size);

/* ---- Banks ----------------------------------------------------------------------------------------- */

/* Loads a bank (<name>.bnk_pc) and its media (<name>_media.bnk_pc; may be null or missing: then its sounds
   have no media). Returns the bank's id (non-zero), or 0 with why in error. A bank already loaded is loaded
   again. Problems that don't stop it loading (missing media, unknown children) are kept: rfa_audio_bank_problems.
   Armageddon's init.bnk_pc can be loaded the same way, for its buses' volumes (rfa_audio_bus_volume). */
RFA_AUDIO_API uint32_t rfa_audio_load_bank(const wchar_t* bank_path, const wchar_t* media_path, char* error, int error_size);

/* Unloads a bank: its sounds stop first. */
RFA_AUDIO_API void rfa_audio_unload_bank(uint32_t bank_id);

/* The ids of a bank's events (its object type 4). Returns how many there are (fills at most max). */
RFA_AUDIO_API int rfa_audio_bank_events(uint32_t bank_id, uint32_t* ids, int max);

typedef struct rfa_audio_object_info {
    uint32_t id;
    uint32_t type;         /* 2 sound, 3 action, 4 event, 5 random/sequence, 6 switch, 7 actor-mixer, 8 bus, 9 layer, 14 attenuation */
    uint32_t parent;       /* its parent node (0: none; it goes to a bus) */
    uint32_t medium;       /* a sound's medium id */
    int codec;             /* a sound's: 1 PCM, 2 Wwise ADPCM, else the plug-in id's low byte */
    int loops;             /* a sound's loop count, a container's: 0 for ever */
    int children;
    int has_medium;        /* a sound's medium is in the media file */
    uint32_t bus;          /* a node's bus (only those with no parent have one) */
} rfa_audio_object_info;

/* A bank's objects (sounds, containers, actions, events, ...). Returns how many there are. */
RFA_AUDIO_API int rfa_audio_bank_objects(uint32_t bank_id, rfa_audio_object_info* out, int max);

/* The game parameters a bank's curves use (game_distance among them), and its switch groups. */
RFA_AUDIO_API int rfa_audio_bank_parameters(uint32_t bank_id, uint32_t* ids, int max);
RFA_AUDIO_API int rfa_audio_bank_switch_groups(uint32_t bank_id, uint32_t* groups, int max);
/* The values a bank's switch containers list for a group (each once). */
RFA_AUDIO_API int rfa_audio_bank_switch_values(uint32_t bank_id, uint32_t group, uint32_t* values, int max);
/* The range of x over which a bank's curves on a parameter are defined (min, max); 0 if none. */
RFA_AUDIO_API int rfa_audio_parameter_range(uint32_t bank_id, uint32_t parameter, float* min, float* max);

/* What went wrong loading a bank, a line each (missing media, children or targets not in it, objects of
   kinds not read). Returns the text's length; text gets as much as fits. */
RFA_AUDIO_API int rfa_audio_bank_problems(uint32_t bank_id, char* text, int size);

typedef struct rfa_audio_action_info {
    uint32_t id;
    uint32_t kind;         /* 4 play, 1 stop, 0x90 break, 2 pause, 3 resume, ... (the action type >> 12) */
    uint32_t scope;        /* the type's low 12 bits: 0x011 the event's game object, 0x010 every object */
    uint32_t target;
    int delay_ms, fade_ms;
} rfa_audio_action_info;

/* An event's actions, in order. Returns how many there are (0 if the event isn't loaded). */
RFA_AUDIO_API int rfa_audio_event_actions(uint32_t event_id, rfa_audio_action_info* out, int max);

typedef struct rfa_audio_event_info {
    int found;             /* the event is in a loaded bank */
    int plays, stops, breaks, others;
    int loops;             /* something it plays goes on for ever (a looping sound, a continuous container) */
    int three_d;           /* something it plays is placed in 3D */
    int missing;           /* objects or media it needs that aren't loaded */
    float longest_s;       /* how long its longest medium is (one pass), with the delays; -1 if unknown */
    float farthest_m;      /* where its fade with distance ends (the last point of a curve on game_distance); 0 if none */
    uint32_t switch_group; /* the first switch group it depends on (0: none) */
} rfa_audio_event_info;

RFA_AUDIO_API int rfa_audio_get_event_info(uint32_t event_id, rfa_audio_event_info* out);

/* An event or object as readable text: its actions, its tree of containers and sounds, their settings. */
RFA_AUDIO_API int rfa_audio_describe(uint32_t id, char* text, int size);

/* A bus's volume with its parents' (dB), from the init bank if loaded; 0 if not known. */
RFA_AUDIO_API float rfa_audio_bus_volume(uint32_t bus_id);

/* ---- Playing --------------------------------------------------------------------------------------- */

/* Posts an event on a game object. Returns its playing id (non-zero), or 0 if the event isn't loaded. */
RFA_AUDIO_API uint32_t rfa_audio_post(uint32_t event_id, uint64_t object);

/* Plays one node of a bank (a sound or a container) on its own, as a play action would. */
RFA_AUDIO_API uint32_t rfa_audio_play_node(uint32_t node_id, uint64_t object);

/* Plays one medium of a loaded bank alone: 2D, at 0 dB, once (or for ever with loop). */
RFA_AUDIO_API uint32_t rfa_audio_play_medium(uint32_t bank_id, uint32_t medium_id, int loop, uint64_t object);

/* Plays sound given as 16-bit samples (channels interleaved): Guerrilla's own waves, to compare. loop_start
   is in frames (used when loop is 1). 3D at the object when three_d (and mono), else 2D. */
RFA_AUDIO_API uint32_t rfa_audio_play_samples(const int16_t* samples, int frames, int channels, int rate, int loop, int loop_start,
                                              float volume_db, float pitch_cents, int three_d, uint64_t object);

/* Stops a playing id, fading out over fade_ms. */
RFA_AUDIO_API void rfa_audio_stop_playing(uint32_t playing_id, int fade_ms);
/* Stops everything on a game object (0: on every object). */
RFA_AUDIO_API void rfa_audio_stop_object(uint64_t object, int fade_ms);

/* 1 while a playing id is playing or waiting to (a delayed action). */
RFA_AUDIO_API int rfa_audio_is_playing(uint32_t playing_id);

/* Called when a playing id ends (all it played ended or was stopped), outside the engine's lock. */
/* voices: how many voices it started (0: it never made a sound). */
typedef void (*rfa_audio_end_callback)(uint32_t playing_id, uint32_t event_id, uint64_t object, int voices, void* user);
RFA_AUDIO_API void rfa_audio_set_end_callback(rfa_audio_end_callback callback, void* user);

/* ---- Game objects, the listener, game parameters, switches and states ------------------------------- */

/* A game object's place and the way it faces (front may be null). */
RFA_AUDIO_API void rfa_audio_set_position(uint64_t object, const float position[3], const float front[3]);
/* The listener: its place, front and up (front and up may be null: z forward, y up). */
RFA_AUDIO_API void rfa_audio_set_listener(const float position[3], const float front[3], const float up[3]);
/* A game parameter on an object, or for every object (object 0). game_distance is the engine's own: the
   object's distance from the listener, unless it is set here. */
RFA_AUDIO_API void rfa_audio_set_parameter(uint64_t object, uint32_t parameter, float value);
/* Takes a parameter set on an object (or every object's) away again. */
RFA_AUDIO_API void rfa_audio_reset_parameter(uint64_t object, uint32_t parameter);
/* A switch on an object; value 0 takes it away (each switch container then plays its default). */
RFA_AUDIO_API void rfa_audio_set_switch(uint64_t object, uint32_t group, uint32_t value);
RFA_AUDIO_API void rfa_audio_set_state(uint32_t group, uint32_t state);
/* Stops what plays on a game object and forgets it. */
RFA_AUDIO_API void rfa_audio_remove_object(uint64_t object);

/* ---- Running -------------------------------------------------------------------------------------- */

/* One update: delayed actions, containers, fades, curves, voices, streams. Not needed with own_thread. */
RFA_AUDIO_API void rfa_audio_tick(void);

/* Loopback only: renders frames of stereo float sound into out (2 × frames floats, may be null), moving the
   engine's time on by them, and updates before each piece. Returns the frames rendered. */
RFA_AUDIO_API int rfa_audio_render(float* out, int frames);

/* The engine's time in seconds (loopback: the time rendered). */
RFA_AUDIO_API double rfa_audio_time(void);

typedef struct rfa_audio_voice {
    uint32_t playing_id, event_id, sound_id, medium_id;
    uint64_t object;
    uint32_t bus;
    int codec;             /* 1 PCM, 2 Wwise ADPCM, 3 samples given (rfa_audio_play_samples) */
    int channels, rate;
    int loops;             /* 1 if it loops for ever (until stopped or broken) */
    int is_virtual;        /* too quiet, or no source free: it keeps its time but makes no sound */
    int streamed;          /* read from disk as it plays, not decoded whole */
    int three_d;
    float level_db;        /* its volume now (nodes, curves, distance, bus, fades); below -200: silent */
    float distance;        /* from the listener (m) */
    float position_s, length_s;
    int priority;
} rfa_audio_voice;

/* The voices playing. Returns how many there are (fills at most max). */
RFA_AUDIO_API int rfa_audio_voices(rfa_audio_voice* out, int max);

typedef struct rfa_audio_status {
    int playing;           /* playing ids */
    int voices, virtual_voices, streams, sources_used, sources;
    int64_t cache_bytes;
    int banks;
    int64_t voices_refused;   /* by a voice limit (refuse the newest) */
    int64_t voices_ended_quiet, voices_stolen, underruns;
} rfa_audio_status;

RFA_AUDIO_API int rfa_audio_get_status(rfa_audio_status* out);

/* ---- Ids ------------------------------------------------------------------------------------------ */

/* Wwise's id of a name: FNV-1 (32 bits) of its lower-case letters. */
RFA_AUDIO_API uint32_t rfa_audio_id(const char* name);

#ifdef __cplusplus
}
#endif

#endif
