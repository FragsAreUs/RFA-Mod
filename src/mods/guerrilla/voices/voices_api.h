// What guerrilla_voices gives the other parts of rfa_guerrilla.dll (they were its DLL's exports; each is described
// where it is made, too). human: the person's object, as the game's object_from_handle gives it. While the voices part
// isn't running (guerrilla_voices=0 under [Mods]), each answers as its missing DLL did: nobody speaks (Play and
// Prepare give 11, no persona; Stop, Playing 0; Persona null).
#pragma once

// Plays one of Guerrilla's lines (rfg_voice_lines.xtbl's name: voc_pain, voc_panic, com_ally_combat_flee, ...) on a
// person, by Guerrilla's rules for it; priority -1 takes the line's own (0-9). Returns voices::Status: 0 played, 1 the
// persona's bank is loading (asked for; try again shortly), 3 the persona hasn't the line, 7 / 8 its local / global
// cooldown runs, 9 its chance said no, 10 the person is saying a line of the same or a higher priority, 11 not one of
// Guerrilla's characters with a free-roam persona, 12 no sound object, 13 the game didn't play it.
int GuerrillaVoices_Play(const void* human, const char* line, int priority);

// Gets a person's persona ready (its bank loading), e.g. as they come near: 0 ready, 1 loading, 11 no persona.
int GuerrillaVoices_Prepare(const void* human);

// Stops the line a person is saying: 1 if one was playing.
int GuerrillaVoices_Stop(const void* human);

// Whether a person is saying a line: 1 or 0.
int GuerrillaVoices_Playing(const void* human);

// A person's persona (GMM01, ETM03, ...), or null if they have none (yet: it is taken at their first call).
const char* GuerrillaVoices_Persona(const void* human);
