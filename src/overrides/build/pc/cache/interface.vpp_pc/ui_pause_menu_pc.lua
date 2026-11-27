-- Override of ui_pause_menu_pc.lua from <game>\build\pc\cache\interface.vpp_pc (the pause menu on
-- PC). rfa_loader.dll gives the game this file instead of its own copy.
--
-- Adds Options > Experimentals (single player only): the Gameplay screen opened with
-- Rfa_options_page set to "experimentals", which makes it show RFA-Mod's own settings (see
-- ui_options_gameplay.lua). In the Guerrilla world, SAVE GAME saves the moment you're in (guerrilla_save's
-- rfg_save_can_save, rfg_save_game and rfg_save_state), and HANDBOOK opens Guerrilla's handbook (guerrilla_hud's
-- rfg_handbook_available and rfg_handbook_open). Every line we added or changed is marked [RFA]; everything else is the
-- game's original script. Only the [RFA] lines are this project's (GNU GPL version 3 or later, see
-- LICENSE); the rest belongs to the game's owners. To compare with the original, extract it with the
-- editor.

--------------
-- MENU IDS --
--------------

-- Main pause menu items
local UPGRADES_ID					= 1
local SAVE_GAME_ID				= 2
local LOAD_GAME_ID				= 3
local OPTIONS_ID					= 4
local RETURN_TO_LOBBY_ID		= 5
local QUIT_ID						= 6
local LOAD_CHECKPOINT_ID		= 7
local WC_RESTORE_MAP_ID			= 8
local EXTRAS_ID					= 10
local CHEATS_ID					= 11
local PLAYER_STATS_ID			= 12
local RFG_SAVE_GAME_ID			= 13 -- [RFA] SAVE GAME in the Guerrilla world (guerrilla_save)
local RFG_HANDBOOK_ID			= 14 -- [RFA] HANDBOOK in the Guerrilla world (guerrilla_hud)

-- Options menu
local OPTIONS_CONTROLS_ID		= 101
local OPTIONS_GAMEPLAY_ID		= 102
local OPTIONS_AUDIO_ID			= 103
local OPTIONS_VIDEO_ID			= 104
local OPTIONS_EXPERIMENTALS_ID	= 105 -- [RFA]

-- Extras menu
local EXTRAS_PLAYER_STATS_ID	= 201
local EXTRAS_AUDIO_LOGS_ID		= 202

-- Tiers
local PAUSE_MENU_TIER			= 1
local OPTIONS_TIER				= 2
local EXTRAS_TIER					= 3

-- Demo Mode
local DEMO_MODE = false

-- Special next-screen strings
local NEXT_SCREEN_SAVE_GAME	= "next_screen_save_game"
local NEXT_SCREEN_LOAD_GAME	= "next_screen_load_game"

-- Make sure to update the IDS table to match this!
local MENU_INFOS = {
	DEMO = {
		{label = "MENU_OPTIONS_UC",	id = OPTIONS_ID},
	},

	MULTIPLAYER_CLIENT = {
		{label = "MENU_CONTROLS_UC",		id = OPTIONS_CONTROLS_ID },
		{label = "MENU_GAMEPLAY_UC",		id = OPTIONS_GAMEPLAY_ID},
		{label = "MENU_AUDIO_UC",			id = OPTIONS_AUDIO_ID},
		{label = "MENU_VIDEO_UC",			id = OPTIONS_VIDEO_ID},
		{label = "MENU_QUIT_GAME",		id = QUIT_ID},
	},
	
	MULTIPLAYER_HOST = {
		{label = "MENU_CONTROLS_UC",		id = OPTIONS_CONTROLS_ID },
		{label = "MENU_GAMEPLAY_UC",		id = OPTIONS_GAMEPLAY_ID},
		{label = "MENU_AUDIO_UC",			id = OPTIONS_AUDIO_ID},
		{label = "MENU_VIDEO_UC",			id = OPTIONS_VIDEO_ID},
		{label = "MENU_RETURN_TO_LOBBY",id = RETURN_TO_LOBBY_ID},
	},

	UPGRADES_ADDON = {
		{label = "MENU_PLAYER_UPGRADES",	id = UPGRADES_ID},
	},

	SAVE_ADDON = {	-- If you're in a profile and can save
		{label = "MENU_SAVE_GAME",		id = SAVE_GAME_ID},
	},

	RFG_SAVE_ADDON = { -- [RFA] In the Guerrilla world: saves the moment you're in (guerrilla_save)
		{label = "MENU_SAVE_GAME",		id = RFG_SAVE_GAME_ID}, -- [RFA]
	}, -- [RFA]

	LOAD_ADDON = { -- If you're in a profile and can load
		{label = "MENU_LOAD_GAME",		id = LOAD_GAME_ID},
	},
	
	CHECKPOINT_ADDON = { -- If you have state data in memory to load
		{label = "MENU_LOAD_CHECKPOINT",id = LOAD_CHECKPOINT_ID},
	},

	OPTIONS_ADDON = {		-- Options after the save/load
		{label = "MENU_OPTIONS_UC",	id = OPTIONS_ID},
	},

	EXTRAS_ADDON = { -- If you're in a profile (no extras otherwise)
		{label = "MENU_EXTRAS_UC",		id = EXTRAS_ID},
	},
	
	CHEATS_ADDON = {	-- Tack on if available
		{label = "MENU_CHEATS",			id = CHEATS_ID},
	},
	
	QUIT_ADDON = {		-- Quit is always at the end
		{label = "MENU_QUIT_GAME",		id = QUIT_ID},
	},

	OPTIONS_SUBMENU = {			-- Options sub-menu
		{label = "MENU_CONTROLS_UC",		id = OPTIONS_CONTROLS_ID },
		{label = "MENU_GAMEPLAY_UC",		id = OPTIONS_GAMEPLAY_ID},
		{label = "MENU_AUDIO_UC",			id = OPTIONS_AUDIO_ID},
		{label = "MENU_VIDEO_UC",			id = OPTIONS_VIDEO_ID},
		{label = "EXPERIMENTALS",			id = OPTIONS_EXPERIMENTALS_ID}, -- [RFA]
	},
	
	PLAYER_STATS_ADDON = { -- If you're in a profile and DLC
		{label = "MENU_PLAYER_STATS", id = PLAYER_STATS_ID},
	},
	
	EXTRAS_SUBMENU = {		-- Extras sub-menu (only available with a profile)
		{label = "MENU_PLAYER_STATS",	id = EXTRAS_PLAYER_STATS_ID},
		{label = "MENU_AUDIO_LOGS",	id = EXTRAS_AUDIO_LOGS_ID},
	},

	WRECKING_CREW = {
		{label = "MENU_RESTORE_MAP", 	id = WC_RESTORE_MAP_ID},
		{label = "MENU_OPTIONS_UC",		id = OPTIONS_ID},
		{label = "MENU_RUIN_EXIT_TO_MENU",	id = QUIT_ID},
	},
	
	WRECKING_CREW_CHEATS = {
		{label = "MENU_RESTORE_MAP", 	id = WC_RESTORE_MAP_ID},
		{label = "MENU_OPTIONS_UC",		id = OPTIONS_ID},
		{label = "MENU_CHEATS",				id = CHEATS_ID},
		{label = "MENU_RUIN_EXIT_TO_MENU", id = QUIT_ID},
	},

}

local Hint_data = {"ui_xbox_b", "MENU_BACK"}

local Top_menu_infos = {}	-- Generated on init

local Input_tracker
local Mouse_input_tracker
local Anims = {}
local List, Sub_list, Hint_bar

local Menu_first_time = true
local Tween_done = true

local Next_screen = nil
local Rfg_save_asked = false -- [RFA] SAVE GAME in the Guerrilla world asked guerrilla_save for a save

function ui_pause_menu_init()
	DEMO_MODE = is_demo()

	local salvage_amount = get_local_player_salvage()
	local salvage_obj = Vdo_base_object:new("salvage_amount")
	salvage_obj:set_text(add_commas_to_int(salvage_amount))
	local salvage_bg = Vdo_base_object:new("salvage_bg")
	local salvage_tech_1 = Vdo_base_object:new("techno_text1")
	local salvage_tech_2 = Vdo_base_object:new("techno_text2")
	local salvage_w,salvage_h = salvage_obj:get_property("screen_size")
	salvage_bg:set_property("screen_size",salvage_w + 48, 40)
	salvage_tech_1:set_anchor(-1*(salvage_w + 50),-10)
	salvage_tech_2:set_anchor(-1*(salvage_w + 50),13)
	
	local salvage_group = Vdo_base_object:new("salvage_group")
	salvage_group:set_visible(rf4_wrecking_crew_is_initialized() == false) -- we don't care about salvage in Wrecking crew.
	
	Input_tracker = Vdo_input_tracker:new()
	Input_tracker:add_input("menu_select", "ui_pause_menu_button_a", 50)
	Input_tracker:add_input("menu_cancel", "ui_pause_menu_button_b", 50)
	Input_tracker:add_input("nav_up", "ui_pause_menu_nav_up", 50)
	Input_tracker:add_input("nav_down", "ui_pause_menu_nav_down", 50)
	Input_tracker:add_input("start_button", "ui_pause_menu_button_start", 50, true)
	Input_tracker:subscribe(true)
	
	Mouse_input_tracker = Vdo_input_tracker:new()

	-- Initialize the list object
	List = Vdo_menu_list:new("list")
	Sub_list = Vdo_menu_list:new("list2")

	-- Set the title and hints
	Hint_bar = Vdo_hint_bar:new("hint_bar", 0, vint_document_find("ui_background_ingame"))
	local hint_data = nil
	--Hint_bar:set_hints(hint_data)

	-- Set up the correct back button for the platform
	local back_button = get_button_name_for_action("menu_cancel")
	Hint_data[1] = back_button

	--disable the animations
	Anims.list_out = Vdo_anim_object:new("list_anim_out")
	Anims.list_in = Vdo_anim_object:new("list_anim_in")
	Anims.list_in2 = Vdo_anim_object:new("list_anim_in2")
	Anims.sub_list_out = Vdo_anim_object:new("list2_anim_out")
	Anims.sub_list_in = Vdo_anim_object:new("list2_anim_in")
	Anims.menu_in = Vdo_anim_object:new("menu_animate_in")
	Anims.menu_out = Vdo_anim_object:new("menu_animate_out")
	Anims.menu_tier_1_out = Vdo_anim_object:new("list_anim_out_screen")

	local menu_out_tween = Vdo_tween_object:new("list2_alpha_twn_3")
	menu_out_tween:set_end_event("ui_pause_menu_load_screen")

	local menu_tier_1_out_tween = Vdo_tween_object:new("list_alpha_twn2_1")
	menu_tier_1_out_tween:set_end_event("ui_pause_menu_load_screen")

	local tech_module = Vdo_tech_module:new("tech_module")
	tech_module:set_property("blur",10.0)
	tech_module:module_animate_in()

	local tech_meter = Vdo_tech_meter:new("tech_meter")
	tech_meter:set_property("blur",10.0)
	tech_meter:meter_animate_in()

	local tech_corner = Vdo_tech_corner:new("tech_corner_1")
	tech_corner:set_property( "blur", 2.0 )
	tech_corner:play()

	local alpha_tween = Vdo_tween_object:new("list_alpha_twn")
	alpha_tween:set_end_event("ui_pause_menu_anim_done")

	alpha_tween = Vdo_tween_object:new("list_alpha_twn_1")
	alpha_tween:set_end_event("ui_pause_menu_anim_done")

	alpha_tween = Vdo_tween_object:new("list2_alpha_twn_1")
	alpha_tween:set_end_event("ui_pause_menu_anim_done")

	alpha_tween = Vdo_tween_object:new("list2_alpha_twn_2")

	alpha_tween = Vdo_tween_object:new("list_alpha_twn_3")
	alpha_tween:set_end_event("ui_pause_menu_anim_done")

	alpha_tween = Vdo_tween_object:new("list_alpha_twn2")

	-- Construct the top level menu
	ui_pause_menu_construct_top_menu_from_flags()

	-- Draw the list for the first time
	ui_pause_menu_change_tier(Global_last_pause_tier, Global_last_pause_option)
	
	local ruin_tip_group = Vdo_base_object:new("ruin_challenge_group")
	if rf4_wrecking_crew_is_challenge_mode() then
		ruin_tip_group:set_visible(true)
	else
		ruin_tip_group:set_visible(false)
	end

	-- [RFA] Options > Main Menu > Style: Modern covers the pause menu too (vdo_button_highlight.lua's rfa_theme_*).
	local rfa_apply, rfa_screen = _GetAnyGlobalSilent("rfa_theme_apply"), _GetAnyGlobalSilent("rfa_theme_screen") -- [RFA]
	if rfa_apply ~= nil and rfa_screen ~= nil and rfa_apply() then -- [RFA]
		rfa_screen({List, Sub_list}) -- [RFA]
	end -- [RFA]
end

function ui_pause_menu_cleanup()
	-- Nuke all button subscriptions
	Input_tracker:subscribe(false)
	Mouse_input_tracker:subscribe(false)
	
	rf4_popup_close()
end

-- Constructs the top level pause menu based on init flags
function ui_pause_menu_construct_top_menu_from_flags()
	Top_menu_infos = {}
	local num_infos = 0

	-- Helper function that loops through a list of infos and tacks them onto the list
	local function tack_on_infos(infos)
		for i, info in ipairs(infos) do
			num_infos = num_infos + 1
			Top_menu_infos[num_infos] = info
		end
	end
	
	-- Show normal menu as needed
	Hint_bar:set_value("")
	
	if mp_is_multiplayer() then
		-- Set up the online version of the pause menu
		Hint_bar:set_header("MENU_OPTIONS_UC")
		if mp_is_host() then
			tack_on_infos(MENU_INFOS.MULTIPLAYER_HOST)
		else
			tack_on_infos(MENU_INFOS.MULTIPLAYER_CLIENT)
		end
		
	elseif rf4_wrecking_crew_is_initialized() then
		-- Set up the wrecking crew version of the pause menu
		Hint_bar:set_header("MENU_PAUSED")

		if rf4_cheats_options_allow_cheats_screen() then
			tack_on_infos(MENU_INFOS.WRECKING_CREW_CHEATS)
		else
			tack_on_infos(MENU_INFOS.WRECKING_CREW)
		end

	else
		-- Set up the single player menu
		Hint_bar:set_header("MENU_PAUSED")

		if DEMO_MODE == true then
			-- Use simple menu that doesn't have saving options
			tack_on_infos(MENU_INFOS.DEMO)

		else
			-- [RFA] In the Guerrilla world, Guerrilla's handbook first, as its pause menu had it: rfg_handbook_available()
			-- [RFA] says whether it shows there, and its word (guerrilla_hud); nil without that mod
			if rfg_handbook_available ~= nil then -- [RFA]
				local rfg_handbook, rfg_handbook_label = rfg_handbook_available() -- [RFA]
				if rfg_handbook == true then -- [RFA]
					tack_on_infos({ {label = rfg_handbook_label, id = RFG_HANDBOOK_ID} }) -- [RFA]
				end -- [RFA]
			end -- [RFA]

			if is_signed_into_profile() then
				-- Add save/load

				-- [RFA] In the Guerrilla world rfg_save_can_save() says whether SAVE GAME can save the moment
				-- [RFA] you're in (true) or not now (false); elsewhere, or without guerrilla_save, it gives nil
				local rfg_save = nil -- [RFA]
				if rfg_save_can_save ~= nil then -- [RFA]
					rfg_save = rfg_save_can_save() -- [RFA]
				end -- [RFA]
				if rfg_save == true then -- [RFA]
					tack_on_infos(MENU_INFOS.RFG_SAVE_ADDON) -- [RFA]
				elseif rfg_save == nil and have_state_data_in_memory() and savegame_is_owned() then -- [RFA] (elseif, rfg_save == nil)
					-- Only add save if we have something to save in memory and we're
					-- allowed to save (own our save game)
					tack_on_infos(MENU_INFOS.SAVE_ADDON)
				end

				tack_on_infos(MENU_INFOS.LOAD_ADDON)
			end

			if have_state_data_in_memory() then
				-- Only add checkpoint if we have something in memory
				tack_on_infos(MENU_INFOS.CHECKPOINT_ADDON)
			end

			-- General options
			tack_on_infos(MENU_INFOS.OPTIONS_ADDON)

			if rf4_cheats_options_allow_cheats_screen() then
				tack_on_infos(MENU_INFOS.CHEATS_ADDON)
			end
			
			if is_signed_into_profile() then
				if rf4_dlc_are_dlc04_missions_active() then
					-- Player stats is only around when you have a profile and are in DLC
					tack_on_infos(MENU_INFOS.PLAYER_STATS_ADDON)
				else
					-- Extras is only around when you have a profile and not in DLC
					tack_on_infos(MENU_INFOS.EXTRAS_ADDON)
				end
			end
		end

		-- And the quit option last...
		tack_on_infos(MENU_INFOS.QUIT_ADDON)
	end
end

function ui_pause_menu_nav_up(event, acceleration)
	if Tween_done then
		-- Check to see what list to send the input to
		local menu_list = (Global_last_pause_tier == PAUSE_MENU_TIER) and List or Sub_list
		
		if menu_list:get_selection() == 0 then
			-- If nothing on the menu is selected, set the top-most option as selected
			menu_list:set_selection(1)
			menu_list:set_highlight(true)
			Global_last_pause_option = menu_list:move_cursor(0, true)
		else
			-- Move highlight up
			Global_last_pause_option = menu_list:move_cursor(-1)
		end
	end
end

function ui_pause_menu_nav_down(event, acceleration)
	if Tween_done then
		-- Check to see what list to send the input to
		local menu_list = (Global_last_pause_tier == PAUSE_MENU_TIER) and List or Sub_list
		
		if menu_list:get_selection() == 0 then
			-- If nothing on the menu is selected, set the top-most option as selected
			menu_list:set_selection(1)
			menu_list:set_highlight(true)
			Global_last_pause_option = menu_list:move_cursor(0, true)
		else
			-- Move highlight down
			Global_last_pause_option = menu_list:move_cursor(1)
		end
	end
end

function ui_pause_menu_initiate_quit_game()
	-- Kill inputs
	Input_tracker:subscribe(false)
	Mouse_input_tracker:subscribe(false)

	local quit_string = "MENU_SALVAGE_WARNING_QUIT"

	if mp_is_multiplayer() then
		quit_string = "PLT_ONLINE_QUIT_WARNING"
	end
	
	if ruin_is_ruin_mode() then
		quit_string = "MENU_RUIN_MODE_QUIT_WARNING"
	end

	-- Start the sequence by asking the player if they really want to quit
	rf4_popup_create(quit_string, "ui_pause_menu_quit_game_return", {"MENU_ACCEPT", "MENU_CANCEL"}, 2)
end

function ui_pause_menu_quit_game_return(return_value)
	if return_value == 1 then
		-- Fix the globals
		pause_menu_reset_selections()

		rf4_pause_menu_quit_game()
	else
		-- Turn inputs back on
		Input_tracker:subscribe(true)
		Mouse_input_tracker:subscribe(true)
	end
end

function ui_pause_menu_initiate_return_to_lobby()
	-- Kill inputs
	Input_tracker:subscribe(false)

	local quit_string = "PLT_RETURN_TO_LOBBY_WARNING"

	-- Start the sequence by asking the player if they really want to quit
	rf4_popup_create(quit_string, "ui_pause_menu_return_to_lobby_return", {"MENU_ACCEPT", "MENU_CANCEL"}, 2)
end

function ui_pause_menu_return_to_lobby_return(return_value)
	if return_value == 1 then
		-- Fix the globals
		pause_menu_reset_selections()

		rf4_mp_end_round()
		rf4_pop_ui()
	else
		-- Turn inputs back on
		Input_tracker:subscribe(true)
	end
end

function ui_pause_menu_initiate_load_checkpoint()
	-- Kill inputs
	Input_tracker:subscribe(false)
	Mouse_input_tracker:subscribe(false)

	-- do the popup shit yo!
	rf4_popup_create("MENU_MISSION_CHECKPOINT_WARNING", "ui_pause_menu_load_checkpoint_return", {"MENU_ACCEPT", "MENU_CANCEL"}, 2)
end

function ui_pause_menu_load_checkpoint_return(return_value)
	if return_value == 1 then
		-- Make sure inputs aren't being used anymore
		Input_tracker:subscribe(false)
		Mouse_input_tracker:subscribe(false)
		pause_menu_reset_selections()

		rf4_pause_menu_load_checkpoint()
	else
		-- Turn inputs back on
		Input_tracker:subscribe(true)
		Mouse_input_tracker:subscribe(true)
	end
end

function ui_pause_menu_button_a(event, acceleration)
	if not Tween_done then
		return
	end
	
	-- Make sure there's a valid selection index before processing the A button (or click)
	local current_selection = (Global_last_pause_tier == PAUSE_MENU_TIER) and List:get_selection() or Sub_list:get_selection()
	if current_selection == 0 then
		return
	end

	play_ui_sound( "INT_SELECT" )

	-- Check whether we're in the sub-list or not to get the right ID
	local current_id = (Global_last_pause_tier == PAUSE_MENU_TIER) and List:get_id() or Sub_list:get_id()

	-- Helper function to set the next screen
	local function set_next_screen( screen, tier )
		Next_screen = screen
		if tier ~= PAUSE_MENU_TIER then
			Anims.menu_out:play()
		else
			List:set_property( "blur", 8.0 )
			Anims.menu_tier_1_out:play()
		end
		Tween_done = false

		-- Stop the input now, so no input can be read during the animation
		Input_tracker:subscribe(false)
		Mouse_input_tracker:subscribe(false)
	end

	local in_mp = mp_is_multiplayer()
	
	-- Now for the huge-ass if/thens for each ID...
	if current_id == UPGRADES_ID then
		--set_next_screen("ui_player_upgrades")

	elseif current_id == SAVE_GAME_ID then
		set_next_screen(NEXT_SCREEN_SAVE_GAME, PAUSE_MENU_TIER)
	elseif current_id == RFG_SAVE_GAME_ID then -- [RFA] asks the world for a save of the moment you're in
		if rfg_save_game() then -- [RFA] (guerrilla_save), then goes to the save screen as SAVE GAME does,
			Rfg_save_asked = true -- [RFA] once the save is made (ui_pause_menu_load_screen)
			set_next_screen(NEXT_SCREEN_SAVE_GAME, PAUSE_MENU_TIER) -- [RFA]
		end -- [RFA]

	elseif current_id == RFG_HANDBOOK_ID then -- [RFA] Guerrilla's handbook (guerrilla_hud): the mod keeps the game
		if rfg_handbook_open() then -- [RFA] paused and opens it as this menu closes; its back key opens this menu again
			pause_menu_reset_selections() -- [RFA]
			rf4_pop_ui() -- [RFA]
		end -- [RFA]

	elseif current_id == LOAD_GAME_ID then
		set_next_screen(NEXT_SCREEN_LOAD_GAME, PAUSE_MENU_TIER)

	elseif current_id == LOAD_CHECKPOINT_ID then
		ui_pause_menu_initiate_load_checkpoint()

	elseif current_id == OPTIONS_ID then
		ui_pause_menu_change_tier(OPTIONS_TIER, 1)
		
	elseif current_id == PLAYER_STATS_ID then
		set_next_screen("ui_sp_stats", PAUSE_MENU_TIER)

	elseif current_id == EXTRAS_ID then
		ui_pause_menu_change_tier(EXTRAS_TIER, 1)

	elseif current_id == CHEATS_ID then
		set_next_screen("ui_options_cheats", PAUSE_MENU_TIER)
		
	elseif current_id == QUIT_ID then
		if rf4_wrecking_crew_is_initialized() and not rf4_wrecking_crew_is_challenge_mode() then
			ui_pause_menu_quit_game_return(1)
		else
			ui_pause_menu_initiate_quit_game()
		end

	elseif current_id == RETURN_TO_LOBBY_ID then
		ui_pause_menu_initiate_return_to_lobby()

	elseif current_id == OPTIONS_CONTROLS_ID then
		set_next_screen("ui_options_controller", in_mp and PAUSE_MENU_TIER or OPTIONS_TIER)

	elseif current_id == OPTIONS_GAMEPLAY_ID then
		rawset(_UGGlobals, "Rfa_options_page", "gameplay") -- [RFA] (rawset: the menus refuse new globals)
		set_next_screen("ui_options_gameplay", in_mp and PAUSE_MENU_TIER or OPTIONS_TIER)

	elseif current_id == OPTIONS_EXPERIMENTALS_ID then -- [RFA]
		rawset(_UGGlobals, "Rfa_options_page", "experimentals") -- [RFA]
		set_next_screen("ui_options_gameplay", OPTIONS_TIER) -- [RFA]

	elseif current_id == OPTIONS_AUDIO_ID then
		set_next_screen("ui_options_audio", in_mp and PAUSE_MENU_TIER or OPTIONS_TIER)

	elseif current_id == OPTIONS_VIDEO_ID then
		set_next_screen("ui_options_video", in_mp and PAUSE_MENU_TIER or OPTIONS_TIER)

	elseif current_id == EXTRAS_PLAYER_STATS_ID then
		set_next_screen("ui_sp_stats", EXTRAS_TIER)
	
	elseif current_id == EXTRAS_AUDIO_LOGS_ID then
		set_next_screen("ui_audio_logs", EXTRAS_TIER)

	elseif current_id == WC_RESTORE_MAP_ID then
		if rf4_wrecking_crew_is_challenge_mode() then
			--destructive action needs a popup
			rf4_popup_create("MENU_RUIN_MODE_RESET_WARNING", "ui_pause_menu_wc_restore_map", {"MENU_ACCEPT", "MENU_CANCEL"}, 2)
		else
			ui_pause_menu_wc_restore_map(1)
		end
	end
end

function ui_pause_menu_wc_restore_map(return_value)
	if return_value == 1 then
		rf4_wrecking_crew_restore_map()
	end
end

function ui_pause_menu_button_b(event, acceleration)
	if Tween_done == true then
		play_ui_sound( "INT_BACK" )
		if Global_last_pause_tier ~= PAUSE_MENU_TIER then
			ui_pause_menu_change_tier(PAUSE_MENU_TIER, Global_last_pause_option)
		else
			pause_menu_reset_selections()
			rf4_pop_ui()
		end
	end
end

function ui_pause_menu_button_start(event, acceleration)
	play_ui_sound( "INT_BACK" )
	pause_menu_reset_selections()
	rf4_pop_ui()
end

function ui_pause_menu_mouse_click(event, target_handle, mouse_x, mouse_y)
	local menu_list = (Global_last_pause_tier == PAUSE_MENU_TIER) and List or Sub_list
	local new_index = menu_list:get_button_idx(target_handle)

	if new_index ~= 0 then
		menu_list:set_selection(new_index)
		ui_pause_menu_button_a(event, 0)
	elseif Hint_bar.hint_button_hit_boxes[1].handle == target_handle then
		ui_pause_menu_button_b(event, 0)
	end
end

function ui_pause_menu_mouse_move(event, target_handle, mouse_x, mouse_y)
	if not Tween_done then
		return
	end
	
	local menu_list = (Global_last_pause_tier == PAUSE_MENU_TIER) and List or Sub_list
	local new_index = menu_list:get_button_idx(target_handle)
	
	if new_index == 0 then
		if Hint_bar.hint_button_hit_boxes[1].handle == target_handle then
			Hint_bar:set_highlight(1)
		else
			Hint_bar:set_highlight(0)
		end
		return
	end

	if new_index ~= 0 then
		menu_list:set_selection(new_index)
		menu_list:set_highlight(true)
		Global_last_pause_option = menu_list:move_cursor(0, true)
		Hint_bar:set_highlight(0)
	end
end

function ui_pause_menu_change_tier(new_tier, current_option)
	if not Tween_done then
		return
	end
	local sub_infos = {}
	local menu_dag_group = Vdo_base_object:new("menu_dag_group")
	
	Mouse_input_tracker:remove_all()
	
	if new_tier == PAUSE_MENU_TIER then
		-- Get the selection pointing to the previous menu
		if Global_last_pause_tier == OPTIONS_TIER then
			current_option = ui_pause_menu_get_option_from_infos(Top_menu_infos, OPTIONS_ID)
			sub_infos = MENU_INFOS.OPTIONS_SUBMENU
		elseif Global_last_pause_tier == EXTRAS_TIER then
			current_option = ui_pause_menu_get_option_from_infos(Top_menu_infos, EXTRAS_ID)
			sub_infos = MENU_INFOS.EXTRAS_SUBMENU
		else
			sub_infos = Top_menu_infos
		end
		List:set_property("scale", 1.0, 1.0)
		List:draw_items(Top_menu_infos, current_option)
		List.w, List.h = List:get_property("screen_size")
		--draw the second list with temp data
		Sub_list:draw_items(sub_infos, Global_last_pause_option)
		Sub_list:set_property("alpha", 0)
		--reset the scale
		List:set_property("blur",0)
		--redraw the first list
		menu_dag_group:set_property("depth", -3)
		ui_pause_menu_reset_mask_size(1)
		List:set_highlight(true)
		Hint_bar:set_hints(Hint_data)
		
		List:add_mouse_input("ui_pause_menu", Mouse_input_tracker)
		Hint_bar:add_mouse_input("ui_pause_menu", Mouse_input_tracker)
		Mouse_input_tracker:subscribe(true)

		if Menu_first_time then
			Menu_first_time = false
			Anims.menu_in:play()
			Sub_list:set_property("alpha", 0)
			Tween_done = false
		else
			Anims.list_in:play()
			Anims.sub_list_out:play()
			Tween_done = false
		end
	else
		-- Set the new tier
		if new_tier == OPTIONS_TIER then
			sub_infos = MENU_INFOS.OPTIONS_SUBMENU
		elseif new_tier == EXTRAS_TIER then
			sub_infos = MENU_INFOS.EXTRAS_SUBMENU
		end

		Sub_list:set_property("scale", 1.0, 1.0)
		Sub_list:draw_items(sub_infos, current_option)

		List:set_property("alpha", 0 )
		Sub_list:set_property("alpha", 0 )
		menu_dag_group:set_property("depth", -5)

		--redraw the first list, incase we are coming back from a sub screen
		List:draw_items(Top_menu_infos, 1)
		--unhighlight the first list
		List:set_highlight(false)
		List:set_property("blur", 8.0)
		--shrink the first list
		Tween_done = false
		-- Redraw the second list
		Sub_list.w, Sub_list.h = Sub_list:get_property("screen_size")

		ui_pause_menu_reset_mask_size(2)
		-- Show the back button
		Hint_bar:set_hints(Hint_data)
		
		Sub_list:add_mouse_input("ui_pause_menu", Mouse_input_tracker)
		Hint_bar:add_mouse_input("ui_pause_menu", Mouse_input_tracker)
		Mouse_input_tracker:subscribe(true)

		Anims.sub_list_in:play()

		if Menu_first_time then
			Anims.list_in2:play()
			vint_set_property(vint_object_find("list_alpha_twn2"),"end_event", "ui_pause_menu_anim_done")
			List:set_property("scale", 0.6, 0.6)
		else
			Anims.list_out:play()
			vint_set_property(vint_object_find("list_alpha_twn2"),"end_event", "ui_pause_menu_anim_done")
		end
		Menu_first_time = false
	end
	
	-- Fix the globals
	Global_last_pause_tier = new_tier
	Global_last_pause_option = current_option
end

-- Matches an ID in an info table and returns the count
function ui_pause_menu_get_option_from_infos(infos, id)
	for i, info in ipairs(infos) do
		if info.id == id then
			return i
		end
	end

	return 1
end

function ui_pause_menu_anim_done(tween_h, event_name)
	Tween_done = true
end

function ui_pause_menu_load_screen(tween_h, event_name)
	Tween_done = true
	if Next_screen then
		-- Handle special codes
		if Next_screen == NEXT_SCREEN_SAVE_GAME and Rfg_save_asked then -- [RFA] the Guerrilla world: first
			Rfg_save_asked = false -- [RFA] wait for the save of the moment to be made
			thread_new("ui_pause_menu_rfg_save_wait") -- [RFA]
		elseif Next_screen == NEXT_SCREEN_SAVE_GAME then -- [RFA] (elseif)
			-- Don't push the screen in script, we need to wait for the game state to change
			rf4_saveload_start(false)
		elseif Next_screen == NEXT_SCREEN_LOAD_GAME then
			-- Don't push the screen in script, we need to wait for the game state to change
			rf4_saveload_start(true)
		else
			rf4_push_ui(Next_screen)
		end
	end
end

-- [RFA] SAVE GAME in the Guerrilla world: waits while guerrilla_save's save of the moment is made (a few
-- [RFA] frames; the world saves while the game is paused), then opens the save screen as SAVE GAME does.
-- [RFA] If it couldn't be made, the menu closes (logs\rfa_mods.log says why).
function ui_pause_menu_rfg_save_wait() -- [RFA]
	while rfg_save_state() == "saving" do -- [RFA]
		thread_yield() -- [RFA]
	end -- [RFA]
	if rfg_save_state() == "saved" then -- [RFA]
		rf4_saveload_start(false) -- [RFA]
	else -- [RFA]
		pause_menu_reset_selections() -- [RFA]
		rf4_pop_ui() -- [RFA]
	end -- [RFA]
end -- [RFA]

function ui_pause_menu_reset_mask_size( list_num )
	local list_w, list_h
	local mask_pad = 20
	local mask_corner = Vdo_base_object:new( "mask_corner" )
	local mask_bg1 = Vdo_base_object:new( "mask_bg1" )
	local mask_bg2 = Vdo_base_object:new( "mask_bg2" )
	local corner_w,corner_h = mask_corner:get_property( "screen_size" )

	if list_num == 1 then
		mask_corner:set_property( "anchor", List.w - corner_w - mask_pad, -1*( List.h + mask_pad ) )
		mask_bg1:set_property( "scale", List.w - corner_w - mask_pad, -1*( List.h + mask_pad ) )
		mask_bg2:set_property( "scale", List.w - mask_pad, -1*( List.h + mask_pad - corner_w ) )
	elseif list_num == 2 then
		mask_corner:set_property( "anchor", Sub_list.w - corner_w - mask_pad, -1*( Sub_list.h + mask_pad ) )
		mask_bg1:set_property( "scale", Sub_list.w - corner_w - mask_pad, -1*( Sub_list.h + mask_pad ) )
		mask_bg2:set_property( "scale", Sub_list.w - mask_pad, -1*( Sub_list.h + mask_pad - corner_w ) )
	end
end
