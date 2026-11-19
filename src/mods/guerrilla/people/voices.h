// Guerrilla's people's free-roam lines (voices.cpp), said through guerrilla_voices (its rules per line).
#pragma once

// Finds guerrilla_voices' GuerrillaVoices_Play; at start-up.
void StartVoices();
// Each frame on the game's thread: the pain, death, spotting and grenade lines of the people near the player.
void VoicesFrame(bool ours);
// A person says one of Guerrilla's lines (rfg_voice_lines' name), by guerrilla_voices' rules for it.
void VoiceSay(unsigned char* human, const char* line);
// The morale-led everyday lines (amb_pc_greet_morale_*, amb_talkself_morale_*) for the player's district now.
const char* VoiceGreetLine();
const char* VoiceTalkToSelfLine();
