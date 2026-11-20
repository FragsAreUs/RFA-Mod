// What guerrilla_campaign gives the other parts of rfa_guerrilla.dll (they were its DLL's exports; each is described
// where it is made, too). While the campaign isn't running (guerrilla_campaign=0 under [Mods]), each answers as
// its missing DLL did: nothing there (0, false, null, and -1 where -1 says "not known").
#pragma once

// ---- Activities (activities.cpp), for guerrilla_hud's minimap, world map and screens, and the parts' ambient life

// 1 while an activity or a mission runs (Guerrilla's !activity_none() || mission_is_active()). *activityMode: the
// current modal kind (Guerrilla's activity_modes: 11 demolitions master, 4 Guerrilla Express, ...; 0 none);
// *mission: 1 while a mission runs (none is ported yet: 0). All 0 while the campaign's systems rest.
int GuerrillaCampaign_Running(int* activityMode, int* mission);

// 1 when the activity at a node is completed (its start icon turns grey): the node by Guerrilla's handle, as
// campaign_objects.xml has it. Only demolitions master keeps progress yet.
int GuerrillaCampaign_ActivityDone(unsigned node);

// The same by place, for a map icon that knows only where it is (map_icons.txt): the node within 2 m of (x, z) on
// the ground, or 0.
int GuerrillaCampaign_ActivityDoneAt(float x, float z);

// What GuerrillaCampaign_Activity fills, for the HUD. The texts are Guerrilla's string tags (or names), valid until
// the next call.
struct GuerrillaCampaignActivity {
    int mode;                // Guerrilla's activity_modes: 11 demolitions master; 0 none
    int screen;              // 0 none, 1 the briefing (accept or cancel), 2 running (the corner panel), 3 the results (close)
    const char* title;       // ACT_DEMO_MASTER_NAME
    const char* subtitle;    // the briefing's (Briefing_Destroy_Message)
    const char* body;        // the briefing's text (Demo_Info_Body_Text)
    const char* objective;   // the corner panel's (Corner_HUD_Destroy_Message)
    const char* timer;       // "mm:ss": the 5 s back into the area, else the time left, else the time so far
    const char* result;      // the results: the row's Success_String / Failure_String_Time / _Range / _Ammo / _Death
    const char* failure;     // the results, lost: MENU_YOU_DIED, ACTIVITY_FAIL_OUT_OF_TIME, _LEFT_AREA, _OUT_OF_AMMO
    const char* weapon[4];   // the briefing's weapons (weapons.xtbl names, or a layer prop's icon); null past the last
    int ammo[4];             // their rounds (-1: none shown)
    int timeLimit, parTime;  // seconds (limit 0: none)
    int bestMs, elapsedMs;   // the best time (0: none yet), this run's
    int status;              // running: 0, 1 unstable (out of ammo while collapsing), 2 destroyed
    int success, proTime;    // the results: won; done within the par time
    int outOfRange;          // running: the 5 s back into the area are counting
    int salvage;             // the results: salvage given
    const char* distance;    // running: Transporter's distance to its end ("%.2f" under 10 m, else "%.0f"), "" for others
};

// The running activity's screen: 1 and *info filled in, or 0 (*info all 0) when none is up.
int GuerrillaCampaign_Activity(GuerrillaCampaignActivity* info);

// The HUD's answer: the briefing, 1 accept or 0 cancel; the results, anything closes them.
void GuerrillaCampaign_ActivityChoose(int accept);

// For guerrilla_save, as the player's death timer runs out outside a mission (Armageddon's game-over screen's start,
// 0x578DA0): Guerrilla's gamestate_gp_simulate_frame gave the death to the running activity that takes one (its
// process_player_death: the Transporter's results screen, FAILED), and raised the death screen only when none did. 1
// when the campaign's activity took it (no game-over screen then), 0 otherwise.
int GuerrillaCampaign_TakePlayerDeath();

// For guerrilla_save: the world a reload at a spot (GuerrillaSave_ReloadAt) asked for has opened and runs again (its
// first gameplay step). Told directly: a save carries the campaign's state only when it has some, so the state's being
// handed back (GuerrillaCampaign_LoadState) can't say it (2026-11-19 21:26: a fresh campaign's first retry).
void GuerrillaCampaign_ReloadedAtSpot();

// For guerrilla_save, at the first gameplay step after one of our worlds opened (fresh, from a slot, a respawn's or a
// reload's save), once the save's state has been given back: Guerrilla's level_init ran on every load, so the campaign
// starts its level again at its next frame (every part's Enter: what one level held, its squads, its continuous attack,
// gone). Without this, a reload from memory, which restarts neither the game's clock nor the player, left them running.
void GuerrillaCampaign_WorldOpened();

// Guerrilla's activity-complete screen's answer (guerrilla_hud's CloseActivityComplete, delivery.cpp): 1 its first
// hint (CONTINUE after a win, RETRY after a loss), 2 its second (RETRY after a won Transporter, EXIT after a loss).
void GuerrillaCampaign_ActivityCompleteChoose(int choice);

// The use prompt at the start of one of Guerrilla's activities: its icon (Guerrilla's use_object_icon; 1 the
// default hand), 0 none. The use key itself the campaign reads.
int GuerrillaCampaign_UsePrompt();

// A ring on the minimap's ground (CIRCLE_FILLED_OUTLINE).
struct GuerrillaCampaignMapRing {
    float x, z, radius;
    unsigned char r, g, b, a;
    int inverted;  // shade the outside
};

// The rings on the minimap's ground now (render_regions): demolitions master's play area, green (50, 200, 50, 80),
// red (200, 50, 50, 255) while the 5 s back into it are counting, and its target. How many were written.
int GuerrillaCampaign_MapRings(GuerrillaCampaignMapRing* rings, int most);

// One of the campaign's map icons for this frame: what Guerrilla's activities gave its cartographer each frame
// (cartographer::add_temporary_icon from their render_icons), drawn on the minimap and, when its row of
// minimap_icon_info.xtbl is VisibleOnFSMap, on the world map.
struct GuerrillaCampaignIcon {
    int size;              // sizeof(GuerrillaCampaignIcon): the caller sets it
    float x, y, z;         // where
    const char* picture;   // its IconName in minimap_icon_info.xtbl ("ui_map_icon_end_point")
    int placement;         // 0 plain (inside the minimap's zoom), 1 sticky (pinned to its rim beyond it)
    unsigned long color;   // 0xAARRGGBB, or 0 for the picture's own colour
    int onWorldMap;        // 1: on the world map too
};

// The campaign's map icons now: a running Transporter's end point (render_icons RM 0x7977F0: element 31
// MAP_ELEMENT_GUERILLA_EXP_END_POINT, sticky, teal 0, 0x77, 0x77). How many were written.
int GuerrillaCampaign_MapIcons(GuerrillaCampaignIcon* icons, int most);

// One item of Guerrilla's diversions panel (its DiversionInfo data item: which_one, diversion_type, fade_this_in,
// flash_this, header_text, status_text, value_text), worked out as hud_ui_missions_process_diversion_info (RM 0x8C56A0)
// did, for the free activities that run (killing spree, mayhem, car bomb).
struct GuerrillaCampaignDiversion {
    int id;              // the item's own number, kept while it is listed (for telling items apart)
    int whichOne;        // its place in the panel, from 1 (the items move up as one goes)
    int type;            // Guerrilla's LUA_DIVERSION_*: 1 mayhem, 3 killing spree, 5 car bomb; 0 another kind
    int fadeIn, flash;   // fade_this_in (its first second), flash_this (its icon, under 6 s left)
    const char* header;  // Guerrilla's string tags (ACT_KILLING_SPREE); valid until the next call
    const char* status;  // a tag (MISS_DIVERSION_STARTED_FEMININE, ACT_BOMB_ARMED), "" for none
    const char* value;   // its text as shown (the kills, the millions)
};
// The panel's items now, in order: how many were written (at most `most`; Guerrilla's panel holds 8).
int GuerrillaCampaign_Diversions(GuerrillaCampaignDiversion* items, int most);

// The game's damage for the activities' listeners (Guerrilla's activity_register_damage and _death), from the parts
// that wrap the game's functions (damage_feeds.cpp has the rest):
// - guerrilla_vehicles' wrap of the human damage (0x85E0C0), after the game's: its damage record and what it returned
//   (the damage done); a hit is reported when the victim lives on and is an NPC, as Guerrilla's human_damage does.
// - guerrilla_world's wrap of a prop's damage (general_mover::apply_damage 0x8CEC80), after the game's: the prop, the
//   attacker, and its hit points before the hit; its death is reported when they went from above 0 to 0 or less.
void GuerrillaCampaign_HumanDamaged(const void* record, float done);
void GuerrillaCampaign_PropDamaged(void* prop, void* attacker, int hitPointsBefore);

// ---- The districts and the campaign's state (campaign_state.cpp)

// For guerrilla_hud's control and morale meters: one district (Guerrilla's MoraleUpdate data item has these). Times
// are the game's clock in milliseconds (-1: never changed); changes counts up with each change of control or morale.
struct GuerrillaCampaignDistrict {
    const char* name;  // Guerrilla's ("Dust"); stays valid
    int index;         // its district_index
    float control, controlMax, controlBefore;
    float morale, moraleMax, moraleBefore;
    float effectiveMorale;  // 0..1, what morale's effects read
    int liberated;
    int controlChangedAt, moraleChangedAt;
    unsigned changes;
};

// The district (-1: the player's): 1 and *info filled in, or 0 (none there).
int GuerrillaCampaign_District(int which, GuerrillaCampaignDistrict* info);

// For guerrilla_people's squad levels: a team's progression level, Guerrilla's get_progression_level (RM 0x78CC70):
// 0 guerrillas and 2 civilians the tech level (0-5), 1 the EDF's progression (0-7, at least the player's district's
// min_progression_edf), 3 the Marauders' (0-7). Level n is squad_level_(n+1). -1 while the campaign's systems rest.
int GuerrillaCampaign_ProgressionLevel(int team);

// For guerrilla_gear: whether the district of that name (Guerrilla's, as upgrades.xtbl's District_Liberated names
// it: "Parker", "Dust", ...) is liberated: 1 yes, 0 not yet, -1 no such district.
int GuerrillaCampaign_DistrictLiberated(const char* name);

// For guerrilla_save, when it makes a save of one of our worlds: the campaign's state into buffer (room bytes); the
// bytes written, or the bytes needed when buffer is null (or too small).
int GuerrillaCampaign_SaveState(unsigned char* buffer, int room);

// For guerrilla_save, when a save of one of our worlds is opened: the state it carried (null, 0: a fresh world, back
// to Guerrilla's starting values), read again by every system.
void GuerrillaCampaign_LoadState(const unsigned char* data, int size);

// ---- The rest

// 1 while a demolitions master challenge has swapped the player's weapons for its own (demolitions_master.cpp):
// guerrilla_gear's hammer top-up should leave his slots alone then.
int GuerrillaCampaign_KitLocked();

// For guerrilla_save's respawn at a safehouse (district_sources.cpp): called just before it takes the respawn
// save's campaign state, which already holds the death's morale.
void GuerrillaCampaign_RespawnSaved();

// For guerrilla_hud: Guerrilla's alert level (0 green, 1 yellow, 2 orange, 3 red) and, in *meter, what its HUD shows
// of it (0 to 100); -1 while the systems rest (guerrilla_campaign.cpp).
int GuerrillaCampaign_Alert(float* meter);

// For guerrilla_hud: the next message, 1 if there was one (messages.cpp). *tag is Guerrilla's string tag (a minor
// message) or tool tip name, *icon the picture shown with a message (null for none), *kind 0 for a minor message, 1
// for a tool tip, 2 for a minor message whose text takes a number (%d), 3 for a primary message (hud_message_add):
// *ms is a tool tip's time on screen, or kind 2's number. The texts stay valid.
int GuerrillaCampaign_NextMessage(const char** tag, const char** icon, int* kind, int* ms);

// The world map's and minimap's M.O.A.B. icon of a radio tag pair (car_bomb.cpp): 0 once that bomb is on a car or
// spent, and while a car bomb runs or the player sits in an armed car (Guerrilla's cartographer hid them so); else 1.
int GuerrillaCampaign_BombIconShown(int pair);

// For the people's ambient walkers and the map: whether a point on the ground is inside one of Guerrilla's
// restricted areas (restricted_areas.cpp); with forAmbientPeople, the areas that let ambient people in don't count.
bool GuerrillaCampaign_InRestrictedArea(float x, float z, bool forAmbientPeople);
