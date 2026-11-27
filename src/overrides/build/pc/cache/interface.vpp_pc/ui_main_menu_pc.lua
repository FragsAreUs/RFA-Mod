-- Override of ui_main_menu_pc.lua from <game>\build\pc\cache\interface.vpp_pc (the main menu on
-- PC). rfa_loader.dll gives the game this file instead of its own copy.
--
-- Groups the top menu by game: Red Faction: Armageddon (Continue, New Game, New Game+, Load Game, the
-- downloadable content, Infestation and Ruin) and Red Faction: Guerrilla (the Guerrilla world, and its
-- builder), then Level editor (Launch editor, Create level, Delete custom level), Options, Extras and Quit; Options > Main Menu sets how ([MainMenu] in
-- rfa_mods.ini: the layout by game or the game's classic one, Continue at the top, which entries show,
-- the descriptions, the decorations, opening where you left off, the style: Classic or Modern, a flat,
-- sharp look in one accent colour, see rfa_modern_style). Adds Level editor (Create level: starts the editor; Delete custom level: deletes a Ruin level made
-- with the editor's New level, through the loader's rfa_open_level_editor, rfa_custom_levels and
-- rfa_delete_custom_level). Adds Options > Experimentals: the Gameplay screen opened with Rfa_options_page set to
-- "experimentals", which makes it show RFA-Mod's own settings (see ui_options_gameplay.lua). Every
-- line we added or changed is marked [RFA]; everything else is the game's original script. Only the
-- [RFA] lines are this project's (GNU GPL version 3 or later, see LICENSE); the rest belongs to the
-- game's owners. To compare with the original, extract it with the editor.

--------------
-- MENU IDS --
--------------

-- Top menu general
local NEW_GAME_ID				= 1
local CONTINUE_ID				= 2
local LOAD_GAME_ID				= 3
local MULTIPLAYER_ID			= 4
local OPTIONS_ID				= 5
local EXTRAS_ID					= 6
local WRECKING_CREW_ID			= 7
local QUIT_GAME_ID				= 8
local NEW_GAME_PLUS_ID			= 9
local DLC_ID					= 10
local LEVEL_EDITOR_ID			= 11 -- [RFA]
local CAMPAIGN_ID				= 12 -- [RFA] Red Faction: Armageddon
local GUERRILLA_ID				= 13 -- [RFA] Red Faction: Guerrilla

-- Red Faction: Guerrilla sub-menu -- [RFA]
local GUERRILLA_WORLD_ID		= 601 -- [RFA]
local GUERRILLA_BUILD_ID		= 602 -- [RFA]

-- MP sub-menu
local MULTI_ONLINE_ID			= 101
local MULTI_SYSLINK_ID			= 102
local MULTI_OFFLINE_ID			= 103

-- Options sub-menu
local OPTIONS_CONTROLS_ID		= 201
local OPTIONS_GAMEPLAY_ID		= 202
local OPTIONS_AUDIO_ID			= 203
local OPTIONS_VIDEO_ID			= 204
local OPTIONS_EXPERIMENTALS_ID	= 205 -- [RFA]
local OPTIONS_MAIN_MENU_ID		= 206 -- [RFA]

-- DLC sub-menu
local DLC_OUTFITS_ID			= 301
local DLC_DLC04_MISSIONS_ID		= 302
local DLC_SHOW_STORE_ID			= 303


-- Level editor sub-menu -- [RFA]
local LEVEL_CREATE_ID			= 501 -- [RFA]
local LEVEL_DELETE_ID			= 502 -- [RFA]
local LEVEL_LAUNCH_ID			= 503 -- [RFA]

-- Extras sub-menu
local EXTRAS_PLAYER_STATS_ID	= 401
local EXTRAS_CUTSCENES_ID		= 402
local EXTRAS_AUDIO_LOGS_ID		= 403
local EXTRAS_CREDITS_ID			= 404

local MAIN_MENU_TIER			= 1
local MULTIPLAYER_TIER			= 2
local OPTIONS_TIER				= 3
local DLC_TIER					= 4
local EXTRAS_TIER				= 5
local LEVEL_EDITOR_TIER			= 6 -- [RFA]
local CAMPAIGN_TIER				= 7 -- [RFA] Red Faction: Armageddon
local GUERRILLA_TIER			= 8 -- [RFA] Red Faction: Guerrilla

-- Demo Mode
local DEMO_MODE = false

-- Special next-screen strings
local NEXT_SCREEN_LOAD_GAME	= "next_screen_load_game"
local NEXT_SCREEN_CREDITS		= "next_screen_credits"

----------
-- DATA --
----------

local MENU_DATA = {
	MAIN_CONTINUE = {
		{label = "MENU_CONTINUE_UC",	id = CONTINUE_ID},
	},

	MAIN_NEW_GAME = {
		{label = "MENU_NEW_GAME",		id = NEW_GAME_ID},
	},

	MAIN_NEW_GAME_PLUS = {
		{label = "MENU_NEW_GAME_PLUS", id = NEW_GAME_PLUS_ID},
	},

	MAIN_DEMO = {		-- Blast away all the stuff we don't want for demo mode
		{label = "MENU_NEW_GAME",		id = NEW_GAME_ID},
		{label = "MENU_OPTIONS_UC",	id = OPTIONS_ID},
	},

	MAIN_HAS_PROFILE = {
		{label = "MENU_LOAD_GAME",		id = LOAD_GAME_ID},
	},

	MAIN_DLC = {		-- DLC option
		{label = "MENU_DOWNLOADABLE_CONTENT",	id = DLC_ID},
	},

	MAIN_CAMPAIGN = { -- [RFA] the games, each with its own sub-menu
		{label = "RED FACTION: ARMAGEDDON",	id = CAMPAIGN_ID}, -- [RFA]
	}, -- [RFA]

	MAIN_GUERRILLA = { -- [RFA]
		{label = "RED FACTION: GUERRILLA",	id = GUERRILLA_ID}, -- [RFA]
	}, -- [RFA]

	MAIN_LEVEL_EDITOR = { -- [RFA]
		{label = "LEVEL EDITOR",		id = LEVEL_EDITOR_ID}, -- [RFA]
	}, -- [RFA]

	ARMAGEDDON_MODES = { -- [RFA] Armageddon's modes, under Red Faction: Armageddon (they were at the top)
		{label = "MENU_INFESTATION",	id = MULTIPLAYER_ID},
		{label = "MENU_RUIN_MODE",		id = WRECKING_CREW_ID},
	}, -- [RFA]

	GUERRILLA_SUB_MENU = { -- [RFA]
		{label = "GUERRILLA WORLD",			id = GUERRILLA_WORLD_ID}, -- [RFA]
		{label = "BUILD GUERRILLA WORLD",	id = GUERRILLA_BUILD_ID}, -- [RFA]
	}, -- [RFA]

	MAIN_GENERAL = {		-- Other stuff at top level ([RFA] Infestation, Ruin and Level editor have their own lists)
		{label = "MENU_OPTIONS_UC",	id = OPTIONS_ID},
		{label = "MENU_EXTRAS_UC",		id = EXTRAS_ID},
		{label = "MENU_QUIT_DESKTOP",	id = QUIT_GAME_ID},
	},

	DLC_SUB_OUTFITS = {
		{label = "MENU_OUTFITS",		id = DLC_OUTFITS_ID},
	},

	DLC_SUB_MISSIONS = {
		{label = "MENU_DLC04_MISSIONS_TITLE",	id = DLC_DLC04_MISSIONS_ID},
	},
	
	DLC_SUB_STORE = {
		{label = "MENU_SHOW_DLC_STORE_STEAM", id = DLC_SHOW_STORE_ID},
	},


	MULTIPLAYER_SUB_MENU = {			-- MP sub-menu
		{label = "NETWORK_PLATFORM_NAME",	id = MULTI_ONLINE_ID},
		{label = "NETWORK_LOCAL_NAME",		id = MULTI_SYSLINK_ID},		
		{label = "NETWORK_OFFLINE_NAME",	id = MULTI_OFFLINE_ID},
	},

	OPTIONS_SUB_MENU = {				-- Options sub-menu
		{label = "MENU_CONTROLS_UC",		id = OPTIONS_CONTROLS_ID},
		{label = "MENU_GAMEPLAY_UC",		id = OPTIONS_GAMEPLAY_ID},
		{label = "MENU_AUDIO_UC",			id = OPTIONS_AUDIO_ID},
		{label = "MENU_VIDEO_UC",			id = OPTIONS_VIDEO_ID},
		{label = "EXPERIMENTALS",			id = OPTIONS_EXPERIMENTALS_ID}, -- [RFA]
		{label = "MAIN MENU",				id = OPTIONS_MAIN_MENU_ID}, -- [RFA]
	},
	
	LEVEL_EDITOR_SUB_MENU = { -- [RFA]
		{label = "LAUNCH EDITOR",			id = LEVEL_LAUNCH_ID}, -- [RFA]
		{label = "CREATE LEVEL",			id = LEVEL_CREATE_ID}, -- [RFA]
		{label = "DELETE CUSTOM LEVEL",		id = LEVEL_DELETE_ID}, -- [RFA]
	}, -- [RFA]

	EXTRAS_SUB_MENU = {
		{label = "MENU_PLAYER_STATS",	id = EXTRAS_PLAYER_STATS_ID},
		{label = "MENU_CUTSCENES",		id = EXTRAS_CUTSCENES_ID},
		{label = "MENU_AUDIO_LOGS",	id = EXTRAS_AUDIO_LOGS_ID},
		{label = "MENU_CREDITS",		id = EXTRAS_CREDITS_ID},
	},
	
	CREDITS_SUB_MENU = {
		{label = "MENU_CREDITS",		id = EXTRAS_CREDITS_ID},
	}	
}
-------------
-- GLOBALS --
-------------

local Texticles = {}

local Top_menu_data = {}

local Input_tracker
local Mouse_input_tracker

local Anims = {}
local Hint_bar, List, Sub_list

local Tween_done = true

local Next_screen = nil

-- [RFA] Options > Main Menu > Style: Modern, and its Accent. The style is the whole interface's: it is in
-- [RFA] vdo_button_highlight.lua (rfa_theme_*), which every menu loads. Here the main menu sets the game's shared
-- [RFA] colours from the settings as it opens (rfa_theme_apply: every menu drawn after takes them, the pause menu
-- [RFA] and the options too), restyles its own screen (rfa_theme_screen), and has the loader load the UI pack, the
-- [RFA] style's pictures (rfa_load_ui_pack), which then stays loaded for the other menus.
local Rfa_modern = false -- [RFA]

function ui_main_menu_rfa_ui_pack_wait() -- [RFA] while the pack's document loads (a few frames)
	for i = 1, 600 do -- [RFA] (ten seconds at most)
		if vint_document_find("rfa_ui_modern") ~= 0 then -- [RFA]
			rfa_theme_screen({List, Sub_list}) -- [RFA] its pictures now
			return -- [RFA]
		end -- [RFA]
		thread_yield() -- [RFA]
	end -- [RFA]
	rfa_log("UI pack: rfa_ui_modern didn't load; the Modern style does without its pictures") -- [RFA]
end -- [RFA]

local function rfa_modern_style() -- [RFA]
	local apply = _GetAnyGlobalSilent("rfa_theme_apply") -- [RFA] (vdo_button_highlight.lua's)
	if apply == nil then return end -- [RFA]
	Rfa_modern = apply() -- [RFA]
	if not Rfa_modern then return end -- [RFA]
	rfa_theme_screen({List, Sub_list}) -- [RFA]
	Hint_bar:set_header("MENU_TITLE_UC", COLOR_HIGHLIGHT_BAR) -- [RFA] (the accent now)
	if vint_document_find("rfa_ui_modern") == 0 and _GetAnyGlobalSilent("rfa_load_ui_pack") ~= nil and rfa_load_ui_pack() then -- [RFA]
		thread_new("ui_main_menu_rfa_ui_pack_wait") -- [RFA]
	end -- [RFA]
end -- [RFA]

function ui_main_menu_init()

	DEMO_MODE = is_demo()
	
	Global_show_kb_controls = (pc_is_active_input_gamepad() == false)

	-- Subscribe to the button presses we need
	Input_tracker = Vdo_input_tracker:new()
	Input_tracker:add_input("menu_select", "ui_main_menu_button_a", 50)
	Input_tracker:add_input("menu_cancel", "ui_main_menu_button_b", 50)
	Input_tracker:add_input("nav_up", "ui_main_menu_nav_up", 50)
	Input_tracker:add_input("nav_down", "ui_main_menu_nav_down", 50)
	Input_tracker:subscribe(true)

	Mouse_input_tracker = Vdo_input_tracker:new()

	-- Set button text
	Hint_bar = Vdo_hint_bar:new("hint_bar", 0, vint_document_find("ui_background_video"))
	Hint_bar:set_hints( nil )
	Hint_bar:set_header("MENU_TITLE_UC", COLOR_HIGHLIGHT_BAR)
	Hint_bar:set_value("")	-- Updated later

	-- Initialize the list object
	List = Vdo_menu_list:new("list")
	Sub_list = Vdo_menu_list:new("list2")

	--Animation Stuff
	local alpha_tween = Vdo_tween_object:new("list_alpha_twn")
	alpha_tween:set_end_event("ui_main_menu_animate")

	alpha_tween = Vdo_tween_object:new("list_alpha_twn_1")
	alpha_tween:set_end_event("ui_main_menu_animate")

	alpha_tween = Vdo_tween_object:new("list2_alpha_twn_1")
	alpha_tween:set_end_event("ui_main_menu_animate")

	alpha_tween = Vdo_tween_object:new("list2_alpha_twn_2")

	alpha_tween = Vdo_tween_object:new("list_alpha_twn2")

	Anims.list_out = Vdo_anim_object:new("list_anim_out")
	Anims.list_in = Vdo_anim_object:new("list_anim_in")
	Anims.list_in2 = Vdo_anim_object:new("list_anim_in2")
	Anims.sub_list_out = Vdo_anim_object:new("list2_anim_out")
	Anims.sub_list_in = Vdo_anim_object:new("list2_anim_in")
	Anims.menu_in = Vdo_anim_object:new("menu_animate_in")
	Anims.menu_out = Vdo_anim_object:new("menu_animate_out")
	Anims.menu_tier_1_out = Vdo_anim_object:new("list_anim_out_screen")

	local menu_out_tween = Vdo_base_object:new("list2_alpha_twn_3")
	menu_out_tween:set_property("end_event", "ui_main_menu_load_screen")

	local menu_tier_1_out_tween = Vdo_base_object:new("list_alpha_twn2_1")
	menu_tier_1_out_tween:set_property("end_event", "ui_main_menu_load_screen")

	local menu_tier_1_in_tween = Vdo_tween_object:new("list2_scale_twn_1")
	menu_tier_1_in_tween:set_property("end_event", "ui_main_menu_animate")

	local tech_module = Vdo_tech_module:new("tech_module")
	tech_module:module_animate_in()

	local tech_meter = Vdo_tech_meter:new("tech_meter")
	tech_meter:meter_animate_in()

	local tech_corner = Vdo_tech_corner:new("tech_corner_1")
	tech_corner:play()

	-- Little scrolling texticles
	Texticles.scrolling_text1 = Vdo_scrolling_text:new("scrolling_text1")
	Texticles.scrolling_text2 = Vdo_scrolling_text:new("scrolling_text2")
	Texticles.scrolling_text3 = Vdo_scrolling_text:new("scrolling_text3")
	local text1_string = {
		[1] = "23horn75290348   324578345234785   2374589239485729";
		[2] = "259384572384           2349857398547tam87694";
		[3] = "457368954746           845gnuechtel4576498";
		[4] = "43297873                                                958sdrose67584938";
		[5] = "54898062                                                084533498023tam2311111";
		[6] = "3423425645345987345345                          54689kim93";
		[7] = "\n ";
		[8] = "879549738495765                                      0721334432";
		[9] = "4508montgomery570234                                                         534schnieder3049345";
		[10] = "453986tam48567498567                             56987496834756987547233245";
		[11] = "5schnieder8405   54                                081234874";
		[12] = "4325435345                                            50897596470394tam603485";
	}
	local text2_string = {
		[1] = "214523215234563        78   7899925349582734535                                                    984759schnieder423453745";
		[2] = "987653498679345        54  3290875montgomery544235324                      234098501montgomery8945689";
		[3] = "234589893284         096802356234789273485938533horn85";
		[4] = "20394850236753460983495gnuechtel72394568723945890685";
		[5] = "\n ";
		[6] = "04895234527349527134098511kim2309487520635         42039845620893457690348gnuechtel39246534890756983456";
		[7] = "45827394587234985678023094867923856               0895234609834kim683745638495654689734";
		[8] = "gnuechtel montgomery schnieder tam kim horn";
	}

	Texticles.scrolling_text1:run_text(text1_string, 0.001, true, 5)
	Texticles.scrolling_text2:run_text(text2_string, 0.001, true, 3)
	Texticles.scrolling_text3:run_text(text1_string, 0.01, true, 2)

	-- [RFA] Options > Main Menu > Decorations off: the scrolling numbers and the tech module, meter and corner hidden
	if rfa_get_setting("MainMenu", "Decorations", 1) == 0 then -- [RFA]
		for i, name in ipairs({"scrolling_text1", "scrolling_text2", "scrolling_text3", "tech_module", "tech_meter", "tech_corner_1"}) do -- [RFA]
			local object = vint_object_find(name) -- [RFA]
			if object ~= 0 then vint_set_property(object, "visible", false) end -- [RFA]
		end -- [RFA]
	end -- [RFA]

	vint_dataitem_add_subscription("main_menu_update", "update", "ui_main_menu_update")

	rfa_modern_style() -- [RFA] Options > Main Menu > Style
	ui_main_menu_update_top_menu_and_draw(nil, true) -- [RFA] (true: the menu is opening)
	ui_main_menu_rfa_back_from_create() -- [RFA] back from the Create level page: the editor making its level
	ui_main_menu_rfa_failed_level_check() -- [RFA] back from a level that failed to load: say so
end

function ui_main_menu_cleanup()
	-- Nuke the scrolling texticles
	for idx, val in pairs(Texticles) do
		val:object_destroy()
	end

	-- Nuke all button subscriptions
	Input_tracker:subscribe(false)
	Mouse_input_tracker:subscribe(false)
	
	Hint_bar:set_header("")
	Hint_bar:set_value("")

	rf4_popup_close()
end

function ui_main_menu_initiate_quit_game()
	-- Kill inputs
	Input_tracker:subscribe(false)
	Mouse_input_tracker:subscribe(false)

	local quit_string = "MENU_DESKTOP_QUIT_WARNING"

	-- Start the sequence by asking the player if they really want to quit
	rf4_popup_create(quit_string, "ui_main_menu_quit_game_return", {"MENU_ACCEPT", "MENU_CANCEL"}, 2)
end

function ui_main_menu_quit_game_return(return_value)

	if return_value == 1 then
		-- Fix the globals
		main_menu_reset_selections()

		rf4_main_menu_pc_quit_game()
	else
		-- Turn inputs back on
		Input_tracker:subscribe(true)
		Mouse_input_tracker:subscribe(true)
	end
end

-- [RFA] Options > Main Menu's settings ([MainMenu] in rfa_mods.ini), read each time the menu is made.
local Rfa_mm = {group = true, continue_at_top = true, guerrilla = true, level_editor = true, descriptions = true,
	decorations = true, remember = true} -- [RFA]
local function rfa_read_main_menu_settings() -- [RFA]
	local function on(key) return rfa_get_setting("MainMenu", key, 1) ~= 0 end -- [RFA]
	Rfa_mm.group = on("GroupByGame") -- [RFA]
	Rfa_mm.continue_at_top = on("ContinueAtTop") -- [RFA]
	Rfa_mm.guerrilla = on("ShowGuerrilla") -- [RFA]
	Rfa_mm.level_editor = on("ShowLevelEditor") -- [RFA]
	Rfa_mm.descriptions = on("Descriptions") -- [RFA]
	Rfa_mm.decorations = on("Decorations") -- [RFA]
	Rfa_mm.remember = on("Remember") -- [RFA]
end -- [RFA]

-- Constructs the top level menu menu based on init flags
function ui_main_menu_construct_top_menu_from_flags()

	Top_menu_data = {}
	local num_data = 0

	-- Helper function that loops through a list of data and tacks them onto the list
	local function tack_on_data(data)
		for i, info in ipairs(data) do
			num_data = num_data + 1
			Top_menu_data[num_data] = info
		end
	end

	if DEMO_MODE == true then
		-- Use the ultra simple style menu
		tack_on_data(MENU_DATA.MAIN_DEMO)
	else
		rfa_read_main_menu_settings() -- [RFA]
		if Rfa_mm.continue_at_top or not Rfa_mm.group then -- [RFA]
		-- Check for save games
		if rf4_main_menu_has_save_games() then
			tack_on_data(MENU_DATA.MAIN_CONTINUE)
		end
		end -- [RFA]

		if Rfa_mm.group then -- [RFA] by game: Continue, New Game, New Game+, Load Game, the DLC, Infestation and
			-- [RFA] Ruin under Red Faction: Armageddon (ui_main_menu_construct_campaign_sub_menu); the Guerrilla
			-- [RFA] world under Red Faction: Guerrilla
			tack_on_data(MENU_DATA.MAIN_CAMPAIGN) -- [RFA]
		else -- [RFA] the game's classic top menu
		tack_on_data(MENU_DATA.MAIN_NEW_GAME)

		-- Check for New Game+
		if rf4_main_menu_show_new_game_plus() then
			tack_on_data(MENU_DATA.MAIN_NEW_GAME_PLUS)
		end

		if is_signed_into_profile() then
			tack_on_data(MENU_DATA.MAIN_HAS_PROFILE)
		end

		-- Check for downloadable outfits or missions
		--if rf4_dlc_is_character_unlock_available() or rf4_dlc_are_dlc04_missions_available() then
			tack_on_data(MENU_DATA.MAIN_DLC)
		--end
		tack_on_data(MENU_DATA.ARMAGEDDON_MODES) -- [RFA] Infestation and Ruin, as the game had them
		end -- [RFA]
		if Rfa_mm.guerrilla then tack_on_data(MENU_DATA.MAIN_GUERRILLA) end -- [RFA]
		if Rfa_mm.level_editor then tack_on_data(MENU_DATA.MAIN_LEVEL_EDITOR) end -- [RFA]

		-- Everything else
		tack_on_data(MENU_DATA.MAIN_GENERAL)
	end
end

function ui_main_menu_update_top_menu_and_draw(saved_id, rfa_opening) -- [RFA] rfa_opening
	-- Now construct the top level menu from the flags
	ui_main_menu_construct_top_menu_from_flags()

	-- [RFA] A sub-menu whose entry isn't on the top menu now (Options > Main Menu changed it), or Open where
	-- [RFA] you left off turned off: start at the top.
	local rfa_tier_entry = {[CAMPAIGN_TIER] = CAMPAIGN_ID, [GUERRILLA_TIER] = GUERRILLA_ID, [LEVEL_EDITOR_TIER] = LEVEL_EDITOR_ID} -- [RFA]
	local rfa_entry = rfa_tier_entry[Global_last_main_menu_tier] -- [RFA]
	if Global_last_main_menu_tier == MULTIPLAYER_TIER and Rfa_mm.group then rfa_entry = CAMPAIGN_ID end -- [RFA]
	local rfa_entry_there = (rfa_entry == nil) -- [RFA]
	for i, option in ipairs(Top_menu_data) do -- [RFA]
		if option.id == rfa_entry then rfa_entry_there = true end -- [RFA]
	end -- [RFA]
	if not rfa_entry_there or (not Rfa_mm.remember and rfa_opening and Global_last_main_menu_tier ~= OPTIONS_TIER) then -- [RFA]
		Global_last_main_menu_tier = MAIN_MENU_TIER -- [RFA]
		Global_last_main_menu_option = 1 -- [RFA]
	end -- [RFA]

	if Global_last_main_menu_tier == MAIN_MENU_TIER then
		if saved_id ~= nil then
			Global_last_main_menu_option = 1

			-- We have an old ID selection, use this as the option if we can find it
			-- Special case: Continue (first option) always takes priority over New Game
			if saved_id ~= NEW_GAME_ID then
				for i, option in ipairs(Top_menu_data) do
					if option.id == saved_id then
						Global_last_main_menu_option = i
						break
					end
				end
			end
		end
	elseif Global_last_main_menu_tier == DLC_TIER then
		-- We need to make sure the DLC tier is still valid
		local found_dlc = false
		for i, option in ipairs(Top_menu_data) do
			if option.id == DLC_ID then
				found_dlc = true
				break
			end
		end
		
		if not found_dlc then
			-- Reset the main menu to the top option
			Global_last_main_menu_tier = MAIN_MENU_TIER
			Global_last_main_menu_option = 1
		end
	end

	-- Draw the list using the saved tier and option
	if Global_last_main_menu_tier == DLC_TIER and dlc_sub_menu_is_valid() == false then
		Global_last_main_menu_tier = MAIN_MENU_TIER
		Global_last_main_menu_option = ui_main_menu_get_option_from_data(Top_menu_data, DLC_ID)
	end
	if Global_last_main_menu_tier == DLC_TIER and Rfa_mm.group then -- [RFA] by game, the DLC is under Armageddon
		Global_last_main_menu_tier = CAMPAIGN_TIER -- [RFA]
		Global_last_main_menu_option = 1 -- [RFA]
	end -- [RFA]
	ui_main_menu_change_tier(Global_last_main_menu_tier, Global_last_main_menu_option, true)
end

function dlc_sub_menu_is_valid()
	if rf4_dlc_is_character_unlock_available() then
		return true
	end
	
	if rf4_dlc_are_dlc04_missions_available() then
		return true
	end
	
	return false
end

-- [RFA] Red Faction: Armageddon's sub-menu: what the top menu had for the story (Continue with saves, New Game, New Game+ when there is one,
-- [RFA] Load Game when signed in), then the downloadable content: its outfits and missions and the store
-- [RFA] when there are any (the DLC sub-menu's), else Downloadable Content (which opens the store); then Infestation and Ruin.
function ui_main_menu_construct_campaign_sub_menu() -- [RFA]
	local sub_data = {} -- [RFA]
	local function tack_on_data(data) -- [RFA]
		for i, info in ipairs(data) do -- [RFA]
			sub_data[#sub_data + 1] = info -- [RFA]
		end -- [RFA]
	end -- [RFA]
	if rf4_main_menu_has_save_games() then -- [RFA]
		tack_on_data(MENU_DATA.MAIN_CONTINUE) -- [RFA]
	end -- [RFA]
	tack_on_data(MENU_DATA.MAIN_NEW_GAME) -- [RFA]
	if rf4_main_menu_show_new_game_plus() then -- [RFA]
		tack_on_data(MENU_DATA.MAIN_NEW_GAME_PLUS) -- [RFA]
	end -- [RFA]
	if is_signed_into_profile() then -- [RFA]
		tack_on_data(MENU_DATA.MAIN_HAS_PROFILE) -- [RFA]
	end -- [RFA]
	if dlc_sub_menu_is_valid() then -- [RFA]
		tack_on_data(ui_main_menu_construct_dlc_sub_menu()) -- [RFA]
	else -- [RFA]
		tack_on_data(MENU_DATA.MAIN_DLC) -- [RFA]
	end -- [RFA]
	tack_on_data(MENU_DATA.ARMAGEDDON_MODES) -- [RFA]
	return sub_data -- [RFA]
end -- [RFA]

function ui_main_menu_construct_dlc_sub_menu()
	local sub_data = {}
	
	-- Helper function that loops through a list of data and tacks them onto the list
	local num_data = 0
	local function tack_on_data(data)
		for i, info in ipairs(data) do
			num_data = num_data + 1
			sub_data[num_data] = info
		end
	end
	
	if rf4_dlc_is_character_unlock_available() then
		tack_on_data(MENU_DATA.DLC_SUB_OUTFITS)
	end
	
	if rf4_dlc_are_dlc04_missions_available() then
		tack_on_data(MENU_DATA.DLC_SUB_MISSIONS)
	end
	
	if dlc_sub_menu_is_valid() then
		tack_on_data(MENU_DATA.DLC_SUB_STORE)
	end
	
	return sub_data
end

function ui_main_menu_nav_up(event, acceleration)
	if not Tween_done then
		return
	end

	--check to see what list to send the input to
	local menu_list = (Global_last_main_menu_tier == MAIN_MENU_TIER) and List or Sub_list

	if menu_list:get_selection() == 0 then
		-- If nothing on the menu is selected, set the top-most option as selected
		menu_list:set_selection(1)
		menu_list:set_highlight(true)
		Global_last_main_menu_option = menu_list:move_cursor(0, true)
	else
		-- Move highlight up
		Global_last_main_menu_option = menu_list:move_cursor(-1)
		
		if Global_last_main_menu_tier == MAIN_MENU_TIER then
			ui_main_menu_set_screen_hint()
		end
	end
end

function ui_main_menu_nav_down(event, acceleration)
	if not Tween_done then
		return
	end

	--check to see what list to sendacceleration the input to
	local menu_list = (Global_last_main_menu_tier == MAIN_MENU_TIER) and List or Sub_list

	if menu_list:get_selection() == 0 then
		-- If nothing on the menu is selected, set the top-most option as selected
		menu_list:set_selection(1)
		menu_list:set_highlight(true)
		Global_last_main_menu_option = menu_list:move_cursor(0, true)
	else
		-- Move highlight down
		Global_last_main_menu_option = menu_list:move_cursor(1)
		
		if Global_last_main_menu_tier == MAIN_MENU_TIER then
			ui_main_menu_set_screen_hint()
		end
	end
end

function ui_main_menu_set_screen_hint(new_tier)
	if not Rfa_mm.descriptions then -- [RFA] Options > Main Menu > Descriptions off
		Hint_bar:set_value("") -- [RFA]
		return -- [RFA]
	end -- [RFA]
	local current_id = List:get_id()
	if new_tier == MULTIPLAYER_TIER then
		current_id = MULTIPLAYER_ID
	elseif new_tier == OPTIONS_TIER then
		current_id = OPTIONS_ID
	elseif new_tier == DLC_TIER then	
		current_id = DLC_ID
	elseif new_tier == EXTRAS_TIER then
		current_id = EXTRAS_ID
	elseif new_tier == LEVEL_EDITOR_TIER then -- [RFA]
		current_id = LEVEL_EDITOR_ID -- [RFA]
	elseif new_tier == CAMPAIGN_TIER then -- [RFA]
		current_id = CAMPAIGN_ID -- [RFA]
	elseif new_tier == GUERRILLA_TIER then -- [RFA]
		current_id = GUERRILLA_ID -- [RFA]
	end
	
	-- Now for the huge-ass if/thens for each ID...
	if current_id == MULTIPLAYER_ID then
		Hint_bar:set_value("MENU_DESC_INFESTATION")

	elseif current_id == WRECKING_CREW_ID then
		Hint_bar:set_value("MENU_DESC_RUIN")

	elseif current_id == OPTIONS_ID then
		Hint_bar:set_value("MENU_DESC_OPTIONS")
		
	elseif current_id == EXTRAS_ID then
		Hint_bar:set_value("MENU_DESC_EXTRAS")

	elseif current_id == NEW_GAME_ID then
		Hint_bar:set_value("MENU_DESC_NEW")

	elseif current_id == CONTINUE_ID then
		Hint_bar:set_value("MENU_DESC_CONTINUE")

	elseif current_id == NEW_GAME_PLUS_ID then
		Hint_bar:set_value("MENU_DESC_PLUS")

	elseif current_id == LOAD_GAME_ID then
		Hint_bar:set_value("MENU_DESC_LOAD")

	elseif current_id == DLC_ID then
		Hint_bar:set_value("MENU_DESC_DLC")
	
	elseif current_id == CAMPAIGN_ID then -- [RFA]
		Hint_bar:set_value("Armageddon's story, its downloadable content, Infestation and Ruin") -- [RFA]

	elseif current_id == GUERRILLA_ID then -- [RFA]
		Hint_bar:set_value("Red Faction: Guerrilla's world, built from your own copy of Guerrilla") -- [RFA]

	elseif current_id == LEVEL_EDITOR_ID then -- [RFA]
		Hint_bar:set_value("Make your own Ruin levels in RFA-Mod's level editor, or delete the ones you made") -- [RFA]

	elseif current_id == LEVEL_LAUNCH_ID then -- [RFA]
		Hint_bar:set_value("Open RFA-Mod's level editor with the game docked in it, to edit any level or table") -- [RFA]

	elseif current_id == QUIT_GAME_ID then
		Hint_bar:set_value("MENU_DESC_QUIT")
	end
end

function ui_main_menu_button_a(event, acceleration)
	if not Tween_done then
		return
	end

	-- Make sure there's a valid selection index before processing the A button (or click)
	local current_selection = (Global_last_main_menu_tier == MAIN_MENU_TIER) and List:get_selection() or Sub_list:get_selection()
	if current_selection == 0 then
		return
	end

	play_ui_sound( "INT_SELECT" )

	-- Check whether we're in the sub-list or not to get the right ID
	local current_id = (Global_last_main_menu_tier == MAIN_MENU_TIER) and List:get_id() or Sub_list:get_id()

	-- Helper function to set the next screen
	local function set_next_screen( screen, tier )
		Next_screen = screen
		if tier ~= MAIN_MENU_TIER then
			-- These are sub-menu tiers
			Anims.menu_out:play()
		else
			Anims.menu_tier_1_out:play()
		end
		Tween_done = false

		-- Stop the input now, so no input can be read during the animation
		--Input_tracker:subscribe(false)
	end

	-- Now for the huge-ass if/thens for each ID...
	if current_id == MULTIPLAYER_ID then
		ui_main_menu_change_tier(MULTIPLAYER_TIER, 1)

	elseif current_id == WRECKING_CREW_ID then
		rf4_main_menu_start_wrecking_crew()

	elseif current_id == OPTIONS_ID then
		ui_main_menu_change_tier(OPTIONS_TIER, 1)

	elseif current_id == EXTRAS_ID then
		ui_main_menu_change_tier(EXTRAS_TIER, 1)

	elseif current_id == CAMPAIGN_ID then -- [RFA]
		ui_main_menu_change_tier(CAMPAIGN_TIER, 1) -- [RFA]

	elseif current_id == GUERRILLA_ID then -- [RFA]
		ui_main_menu_change_tier(GUERRILLA_TIER, 1) -- [RFA]

	elseif current_id == GUERRILLA_WORLD_ID then -- [RFA]
		ui_main_menu_rfa_guerrilla_world() -- [RFA]

	elseif current_id == GUERRILLA_BUILD_ID then -- [RFA]
		ui_main_menu_rfa_guerrilla_build() -- [RFA]

	elseif current_id == LEVEL_EDITOR_ID then -- [RFA]
		ui_main_menu_change_tier(LEVEL_EDITOR_TIER, 1) -- [RFA]

	elseif current_id == LEVEL_CREATE_ID then -- [RFA]
		if _GetAnyGlobalSilent("rfa_new_level_check") ~= nil then -- [RFA] the menu's own page (ui_options_gameplay.lua's create_level)
			rawset(_UGGlobals, "Rfa_options_page", "create_level") -- [RFA]
			set_next_screen("ui_options_gameplay", LEVEL_EDITOR_TIER) -- [RFA]
		else -- [RFA] an older loader: which mode, then the editor's own New level
			ui_main_menu_rfa_create_level() -- [RFA]
		end -- [RFA]

	elseif current_id == LEVEL_DELETE_ID then -- [RFA]
		ui_main_menu_rfa_delete_level(1) -- [RFA]

	elseif current_id == LEVEL_LAUNCH_ID then -- [RFA]
		ui_main_menu_rfa_launch_editor() -- [RFA]

	elseif current_id == NEW_GAME_ID then
		-- Don't animate out, just start a new game
		rf4_main_menu_new_game()

	elseif current_id == CONTINUE_ID then
		rf4_main_menu_continue()

	elseif current_id == NEW_GAME_PLUS_ID then
		rf4_main_menu_new_game_plus()

	elseif current_id == LOAD_GAME_ID then
		set_next_screen(NEXT_SCREEN_LOAD_GAME, Rfa_mm.group and CAMPAIGN_TIER or MAIN_MENU_TIER) -- [RFA] (by game: under Armageddon)

	elseif current_id == DLC_ID then
	
		if dlc_sub_menu_is_valid() == false then
			rf4_main_menu_show_dlc_store()
		else
			ui_main_menu_change_tier(DLC_TIER, 1)
		end

	elseif current_id == DLC_DLC04_MISSIONS_ID then
		rf4_main_menu_dlc04_start()

	elseif current_id == DLC_SHOW_STORE_ID then
		rf4_main_menu_show_dlc_store()
		
	elseif current_id == DLC_OUTFITS_ID then
		set_next_screen("ui_dlc_outfits", Rfa_mm.group and CAMPAIGN_TIER or DLC_TIER) -- [RFA] (by game: under Armageddon)

	elseif current_id == QUIT_GAME_ID then
		ui_main_menu_initiate_quit_game()

	elseif current_id == MULTI_ONLINE_ID then
		-- Don't animate out, just push to live
		rf4_main_menu_start_live()

	elseif current_id == MULTI_SYSLINK_ID then
		-- Don't animate out, just push to syslink
		rf4_main_menu_start_syslink()

	elseif current_id == MULTI_OFFLINE_ID then
		-- Don't animate out, just push to offline MP
		rf4_main_menu_start_offline()

	elseif current_id == OPTIONS_CONTROLS_ID then
		set_next_screen("ui_options_controller", OPTIONS_TIER)

	elseif current_id == OPTIONS_GAMEPLAY_ID then
		rawset(_UGGlobals, "Rfa_options_page", "gameplay") -- [RFA] (rawset: the menus refuse new globals)
		set_next_screen("ui_options_gameplay", OPTIONS_TIER)

	elseif current_id == OPTIONS_EXPERIMENTALS_ID then -- [RFA]
		rawset(_UGGlobals, "Rfa_options_page", "experimentals") -- [RFA]
		set_next_screen("ui_options_gameplay", OPTIONS_TIER) -- [RFA]

	elseif current_id == OPTIONS_MAIN_MENU_ID then -- [RFA]
		rawset(_UGGlobals, "Rfa_options_page", "main_menu") -- [RFA]
		set_next_screen("ui_options_gameplay", OPTIONS_TIER) -- [RFA]

	elseif current_id == OPTIONS_AUDIO_ID then
		set_next_screen("ui_options_audio", OPTIONS_TIER)

	elseif current_id == OPTIONS_VIDEO_ID then
		set_next_screen("ui_options_video", OPTIONS_TIER)
	
	elseif current_id == EXTRAS_PLAYER_STATS_ID then
		set_next_screen("ui_sp_stats", EXTRAS_TIER)
		
	elseif current_id == EXTRAS_CUTSCENES_ID then
		set_next_screen("ui_video_player", EXTRAS_TIER)
		
	elseif current_id == EXTRAS_AUDIO_LOGS_ID then
		set_next_screen("ui_audio_logs", EXTRAS_TIER)
		
	elseif current_id == EXTRAS_CREDITS_ID then
		set_next_screen(NEXT_SCREEN_CREDITS, EXTRAS_TIER)
	end
end

function ui_main_menu_button_b(event, acceleration)
	if Tween_done == true then
		if Global_last_main_menu_tier == MULTIPLAYER_TIER and Rfa_mm.group then -- [RFA] Infestation's sub-menu: back to Armageddon's
			local armageddon = ui_main_menu_construct_campaign_sub_menu() -- [RFA]
			ui_main_menu_change_tier(CAMPAIGN_TIER, ui_main_menu_get_option_from_data(armageddon, MULTIPLAYER_ID)) -- [RFA]
			play_ui_sound( "INT_BACK" ) -- [RFA]
		elseif Global_last_main_menu_tier ~= MAIN_MENU_TIER then -- [RFA] (elseif)
			ui_main_menu_change_tier(MAIN_MENU_TIER, 1)
			play_ui_sound( "INT_BACK" )
		else
			-- If on the main tier and Esc (or B) is pressed, prompt to quit to desktop
			ui_main_menu_initiate_quit_game()
		end
	end
end

function ui_main_menu_mouse_click(event, target_handle)
	if target_handle == INVALID_VDO_HANDLE then
		return
	end

	if Hint_bar.hint_button_hit_boxes[1].handle == target_handle then
		ui_main_menu_button_b()
		return
	end

	local menu_list = (Global_last_main_menu_tier == MAIN_MENU_TIER) and List or Sub_list
	local new_index = menu_list:get_button_idx(target_handle)

	if new_index ~= 0 then
		menu_list:set_selection(new_index)
		ui_main_menu_button_a()
	end
end

function ui_main_menu_mouse_move(event, target_handle, mouse_x, mouse_y)
	if not Tween_done then
		return
	end
	
	local menu_list = (Global_last_main_menu_tier == MAIN_MENU_TIER) and List or Sub_list
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
		Global_last_main_menu_option = menu_list:move_cursor(0, true)
		Hint_bar:set_highlight(0)
		
		if Global_last_main_menu_tier == MAIN_MENU_TIER then
			ui_main_menu_set_screen_hint()
		end
	end
end

-- [RFA] Red Faction: Guerrilla > Guerrilla World: the console's level rfg_12_11 (rfa_qol's rfa_console_command
-- [RFA] runs it at the next frame), once editor\\GuerrillaBuilder.exe has made the world (rfa_guerrilla_world_built);
-- [RFA] Build Guerrilla World starts the builder (rfa_open_guerrilla_builder).
function ui_main_menu_rfa_guerrilla_world() -- [RFA]
	if not rfa_guerrilla_world_built() then -- [RFA]
		ui_main_menu_rfa_message("The Guerrilla world isn't built yet: Build Guerrilla World makes it from your own copy of Red Faction: Guerrilla. Restart the game once it is built.") -- [RFA]
	elseif rfa_console_command("level rfg_12_11") then -- [RFA]
		Input_tracker:subscribe(false) -- [RFA] the level is loading
		Mouse_input_tracker:subscribe(false) -- [RFA]
	else -- [RFA]
		ui_main_menu_rfa_message("The Guerrilla world needs RFA-Mod's console: console=1 under [Mods] in rfa_mods.ini.") -- [RFA]
	end -- [RFA]
end -- [RFA]

function ui_main_menu_rfa_guerrilla_build() -- [RFA]
	if rfa_open_guerrilla_builder() then -- [RFA]
		ui_main_menu_rfa_message("The Guerrilla builder is starting in its own window: click Build there, then restart the game.") -- [RFA]
	else -- [RFA]
		ui_main_menu_rfa_message("The Guerrilla builder couldn't be started: editor\\GuerrillaBuilder.exe wasn't found next to the mod. logs\\rfa_mods.log says more.") -- [RFA]
	end -- [RFA]
end -- [RFA]

-- [RFA] Level editor > Create level and Delete custom level. The loader's functions (rfa_loader.dll)
-- [RFA] start the editor, list the Ruin levels made with its New level, and have it delete one (with no
-- [RFA] window; a thread waits for it). The game reads its map lists at start-up, so a deleted level
-- [RFA] leaves Ruin's list once the game starts again.
local Rfa_levels = {} -- [RFA] the levels listed in the popup
local Rfa_level_page = 1 -- [RFA] its first level's place
local Rfa_level_chosen = nil -- [RFA] the level to delete, once picked
local RFA_LEVELS_SHOWN = 2 -- [RFA] how many a popup lists at once (a popup has at most 4 options: these, MORE... and Cancel)

local function rfa_popup(message, callback, options) -- [RFA]
	Input_tracker:subscribe(false) -- [RFA]
	Mouse_input_tracker:subscribe(false) -- [RFA]
	rf4_popup_create(message, callback, options, #options) -- [RFA]
end -- [RFA]

function ui_main_menu_rfa_popup_done(return_value) -- [RFA] a popup with only OK
	Input_tracker:subscribe(true) -- [RFA]
	Mouse_input_tracker:subscribe(true) -- [RFA]
end -- [RFA]

function ui_main_menu_rfa_message(message) -- [RFA] a message with OK
	rfa_popup(message, "ui_main_menu_rfa_popup_done", {"MENU_OK"}) -- [RFA]
end -- [RFA]

-- [RFA] The game's own processing screen (ui_popup_halt: PROCESSING, its rings turning; the loader's
-- [RFA] rfa_processing_popup shows and hides it as the game does while it saves or loads), with our words in
-- [RFA] its big line: a popup made from inside another's callback didn't show.
-- [RFA] It stays until the wait is over: each frame of the wait asks the loader to hold it, and the loader
-- [RFA] turns away the game's own calls to take it down meanwhile (as the editor docks the game...). Should it
-- [RFA] go anyway, the next frame puts it back.
local Rfa_processing_words = nil -- [RFA] its words, while a wait is on
local Rfa_processing_frames = 0 -- [RFA] frames left to put them (the game sets its own as it opens)

local function rfa_processing(words) -- [RFA]
	Input_tracker:subscribe(false) -- [RFA]
	Mouse_input_tracker:subscribe(false) -- [RFA]
	Rfa_processing_words = words -- [RFA]
	Rfa_processing_frames = 30 -- [RFA]
	rfa_processing_popup(true) -- [RFA]
end -- [RFA]

local function rfa_processing_tick() -- [RFA] each frame of the wait
	if Rfa_processing_words == nil then return end -- [RFA]
	rfa_processing_popup(true) -- [RFA] back up if the game took it down (nothing if it is still ours)
	if vint_document_find("ui_popup_halt") == 0 then -- [RFA] not there (yet, or again): words once it is
		Rfa_processing_frames = 30 -- [RFA]
		return -- [RFA]
	end -- [RFA]
	if Rfa_processing_frames <= 0 then return end -- [RFA]
	-- [RFA] Its big line, found in its own document: ui_popup_halt_set_new_data, called from here, looks for its
	-- [RFA] objects in this one, doesn't find them and fails (which ended the wait, and with it the menu's input).
	local halt = vint_document_find("ui_popup_halt") -- [RFA]
	local word = vint_object_find("action_word", nil, halt) -- [RFA]
	if word ~= nil and word ~= 0 then vint_set_property(word, "text_tag", Rfa_processing_words) end -- [RFA]
	Rfa_processing_frames = Rfa_processing_frames - 1 -- [RFA]
end -- [RFA]

local function rfa_processing_wait_tick() -- [RFA] the wait's frame: nothing in it may end the wait (it gives the input back)
	local ok, error_text = pcall(rfa_processing_tick) -- [RFA]
	if not ok and rfa_log ~= nil then rfa_log("Main menu: the processing screen: " .. tostring(error_text)) end -- [RFA]
end -- [RFA]

local function rfa_processing_done() -- [RFA] hides it; the next popup a moment later, once it has gone
	Rfa_processing_words = nil -- [RFA]
	rfa_processing_popup(false) -- [RFA]
	for i = 1, 30 do thread_yield() end -- [RFA]
end -- [RFA]

function ui_main_menu_rfa_indexing_wait() -- [RFA]
	local ready = rfa_level_editor_ready() -- [RFA]
	while ready == nil do -- [RFA]
		rfa_processing_wait_tick() -- [RFA]
		thread_yield() -- [RFA]
		ready = rfa_level_editor_ready() -- [RFA]
	end -- [RFA]
	rfa_processing_done() -- [RFA]
	if ready then -- [RFA]
		ui_main_menu_rfa_popup_done() -- [RFA] New level is open in the editor
	else -- [RFA]
		rfa_popup("The level editor closed before it had read the game's files. logs\\editor.log says why.", "ui_main_menu_rfa_popup_done", {"MENU_OK"}) -- [RFA]
	end -- [RFA]
end -- [RFA]

-- [RFA] Back from the Create level page (ui_options_gameplay.lua): it started the editor with everything chosen
-- [RFA] (Rfa_create_wait true), or couldn't (a message): the processing screen until the editor has read the game's
-- [RFA] files and made the level.
function ui_main_menu_rfa_back_from_create() -- [RFA]
	local wait = _GetAnyGlobalSilent("Rfa_create_wait") -- [RFA]
	if wait == nil then return end -- [RFA]
	rawset(_UGGlobals, "Rfa_create_wait", nil) -- [RFA]
	Input_tracker:subscribe(false) -- [RFA]
	Mouse_input_tracker:subscribe(false) -- [RFA]
	if wait == true then -- [RFA]
		thread_new("ui_main_menu_rfa_create_wait") -- [RFA]
	else -- [RFA]
		rawset(_UGGlobals, "Rfa_create_message", wait) -- [RFA]
		thread_new("ui_main_menu_rfa_create_failed") -- [RFA]
	end -- [RFA]
end -- [RFA]

-- [RFA] A level that failed to load: the loader kept the game from closing and it came back here (rfa_failed_level
-- [RFA] gives its name once, and whether it is a custom one).
function ui_main_menu_rfa_failed_level_check() -- [RFA]
	local failed = _GetAnyGlobalSilent("rfa_failed_level") -- [RFA] (an older loader has none)
	if failed == nil then return end -- [RFA]
	local name, custom = failed() -- [RFA]
	if name == nil then return end -- [RFA]
	local message -- [RFA]
	if custom then -- [RFA]
		message = "FAILED TO LOAD CUSTOM LEVEL\n\n" .. name .. " couldn't be loaded: its files are missing or couldn't be read. logs\\rfa_mods.log says more." -- [RFA] (the popup takes 256 characters at most)
	else -- [RFA]
		message = "FAILED TO LOAD LEVEL\n\n" .. name .. " couldn't be loaded: its files are missing or couldn't be read. logs\\rfa_mods.log says more." -- [RFA]
	end -- [RFA]
	rawset(_UGGlobals, "Rfa_failed_message", message) -- [RFA]
	Input_tracker:subscribe(false) -- [RFA]
	Mouse_input_tracker:subscribe(false) -- [RFA]
	thread_new("ui_main_menu_rfa_failed_level_show") -- [RFA]
end -- [RFA]

function ui_main_menu_rfa_failed_level_show() -- [RFA] once the menu has come in
	for i = 1, 10 do thread_yield() end -- [RFA]
	rfa_popup(_GetAnyGlobalSilent("Rfa_failed_message"), "ui_main_menu_rfa_popup_done", {"MENU_OK"}) -- [RFA]
end -- [RFA]

function ui_main_menu_rfa_create_wait() -- [RFA] once the menu has come in
	for i = 1, 10 do thread_yield() end -- [RFA]
	rfa_processing("MAKING THE LEVEL") -- [RFA] (the editor reads the game's files first: a minute or so)
	ui_main_menu_rfa_indexing_wait() -- [RFA]
end -- [RFA]

function ui_main_menu_rfa_create_failed() -- [RFA]
	for i = 1, 10 do thread_yield() end -- [RFA]
	rfa_popup(_GetAnyGlobalSilent("Rfa_create_message"), "ui_main_menu_rfa_popup_done", {"MENU_OK"}) -- [RFA]
end -- [RFA]

-- [RFA] Level editor > Launch editor: starts the level editor (or brings the open one forward) with this game docked
-- [RFA] in its Game panel, on no level in particular: open one there, or make one with its New level.
function ui_main_menu_rfa_launch_editor() -- [RFA]
	if rfa_open_level_editor == nil then -- [RFA]
		rfa_popup("The level editor needs RFA-Mod's loader (rfa_loader.dll).", "ui_main_menu_rfa_popup_done", {"MENU_OK"}) -- [RFA]
	elseif rfa_open_level_editor() then -- [RFA]
		rfa_popup("The level editor is starting. It docks the game in its Game panel; open a level there, or make one with its New level.", "ui_main_menu_rfa_popup_done", {"MENU_OK"}) -- [RFA]
	else -- [RFA]
		rfa_popup("The level editor couldn't be started: editor\\RfaEditor.exe wasn't found next to the mod. logs\\rfa_mods.log says more.", "ui_main_menu_rfa_popup_done", {"MENU_OK"}) -- [RFA]
	end -- [RFA]
end -- [RFA]

function ui_main_menu_rfa_create_level() -- [RFA] asks which mode the new level is for
	if rfa_open_level_editor == nil then -- [RFA]
		rfa_popup("The level editor needs RFA-Mod's loader (rfa_loader.dll).", "ui_main_menu_rfa_popup_done", {"MENU_OK"}) -- [RFA]
		return -- [RFA]
	end -- [RFA]
	rfa_popup("Which mode is the new level for? The level editor opens on New level for it, with the mode's player starts, spawns and markers.", -- [RFA]
		"ui_main_menu_rfa_create_mode", {"MENU_RUIN_MODE", "MENU_INFESTATION", "MENU_CANCEL"}) -- [RFA]
end -- [RFA]

function ui_main_menu_rfa_create_mode(return_value) -- [RFA]
	local mode = nil -- [RFA]
	if return_value == 1 then mode = "ruin" elseif return_value == 2 then mode = "infestation" end -- [RFA]
	if mode == nil then -- [RFA]
		ui_main_menu_rfa_popup_done() -- [RFA]
	elseif rfa_open_level_editor(mode) then -- [RFA]
		rfa_processing("INDEXING THE GAME'S FILES") -- [RFA] until the level editor has read them (a minute or so)
		thread_new("ui_main_menu_rfa_indexing_wait") -- [RFA]
	else -- [RFA]
		rfa_popup("The level editor couldn't be started: editor\\RfaEditor.exe wasn't found next to the mod. logs\\rfa_mods.log says more.", "ui_main_menu_rfa_popup_done", {"MENU_OK"}) -- [RFA]
	end -- [RFA]
end -- [RFA]

function ui_main_menu_rfa_delete_level(first) -- [RFA] lists the levels from the first'th, a few at a time
	if rfa_custom_levels == nil then -- [RFA]
		rfa_popup("Deleting levels needs RFA-Mod's loader (rfa_loader.dll).", "ui_main_menu_rfa_popup_done", {"MENU_OK"}) -- [RFA]
		return -- [RFA]
	end -- [RFA]
	Rfa_levels = {rfa_custom_levels()} -- [RFA]
	if #Rfa_levels == 0 then -- [RFA]
		rfa_popup("There are no custom Ruin levels: make one with Create level.", "ui_main_menu_rfa_popup_done", {"MENU_OK"}) -- [RFA]
		return -- [RFA]
	end -- [RFA]
	if first > #Rfa_levels then first = 1 end -- [RFA]
	Rfa_level_page = first -- [RFA]
	local last = first + RFA_LEVELS_SHOWN - 1 -- [RFA] (no math.min: the menus' Lua has no math)
	if last > #Rfa_levels then last = #Rfa_levels end -- [RFA]
	local options = {} -- [RFA]
	for i = first, last do -- [RFA]
		options[#options + 1] = Rfa_levels[i] -- [RFA]
	end -- [RFA]
	if #Rfa_levels > RFA_LEVELS_SHOWN then options[#options + 1] = "MORE..." end -- [RFA]
	options[#options + 1] = "MENU_CANCEL" -- [RFA]
	rfa_popup("Which custom Ruin level should be deleted?", "ui_main_menu_rfa_delete_picked", options) -- [RFA]
end -- [RFA]

function ui_main_menu_rfa_delete_picked(return_value) -- [RFA]
	local shown = #Rfa_levels - Rfa_level_page + 1 -- [RFA]
	if shown > RFA_LEVELS_SHOWN then shown = RFA_LEVELS_SHOWN end -- [RFA]
	if return_value >= 1 and return_value <= shown then -- [RFA]
		Rfa_level_chosen = Rfa_levels[Rfa_level_page + return_value - 1] -- [RFA]
		rfa_popup("Delete " .. Rfa_level_chosen .. " for good? Its archive, its edits, its map list row, strings, pictures and spawn resources all go. This can't be undone.", -- [RFA]
			"ui_main_menu_rfa_delete_confirmed", {"MENU_ACCEPT", "MENU_CANCEL"}) -- [RFA]
	elseif return_value == shown + 1 and #Rfa_levels > RFA_LEVELS_SHOWN then -- [RFA] MORE...
		ui_main_menu_rfa_delete_level(Rfa_level_page + RFA_LEVELS_SHOWN) -- [RFA]
	else -- [RFA]
		ui_main_menu_rfa_popup_done() -- [RFA]
	end -- [RFA]
end -- [RFA]

function ui_main_menu_rfa_delete_confirmed(return_value) -- [RFA]
	if return_value ~= 1 or Rfa_level_chosen == nil then -- [RFA]
		ui_main_menu_rfa_popup_done() -- [RFA]
	elseif rfa_delete_custom_level(Rfa_level_chosen) then -- [RFA]
		rfa_processing("DELETING LEVEL") -- [RFA] until the editor is done
		thread_new("ui_main_menu_rfa_delete_wait") -- [RFA]
	else -- [RFA]
		rfa_popup(Rfa_level_chosen .. " couldn't be deleted: logs\\rfa_mods.log says why.", "ui_main_menu_rfa_popup_done", {"MENU_OK"}) -- [RFA]
	end -- [RFA]
end -- [RFA]

function ui_main_menu_rfa_delete_wait() -- [RFA] while the editor deletes the level (a few seconds)
	local code = rfa_delete_custom_level_done() -- [RFA]
	while code == nil do -- [RFA]
		rfa_processing_wait_tick() -- [RFA]
		thread_yield() -- [RFA]
		code = rfa_delete_custom_level_done() -- [RFA]
	end -- [RFA]
	rfa_processing_done() -- [RFA]
	if code == 0 then -- [RFA]
		rfa_popup(Rfa_level_chosen .. " is deleted. Restart the game to take it off Ruin's map list (until then, don't pick it).", "ui_main_menu_rfa_popup_done", {"MENU_OK"}) -- [RFA]
	else -- [RFA]
		rfa_popup(Rfa_level_chosen .. " couldn't be deleted (the editor's logs\\editor.log says why).", "ui_main_menu_rfa_popup_done", {"MENU_OK"}) -- [RFA]
	end -- [RFA]
	Rfa_level_chosen = nil -- [RFA]
end -- [RFA]

-- Note: current_option is ignored when returning from another tier to the main menu tier
--
function ui_main_menu_change_tier(new_tier, current_option, init_set)
	local sub_data = {}
	local menu_dag_group = Vdo_base_object:new("menu_dag_group")
	
	Global_show_kb_controls = (pc_is_active_input_gamepad() == false)

	Mouse_input_tracker:remove_all()

	-- Set up the version for the main menu based on game saves, put the highlight in the right place for backing up
	if new_tier == MAIN_MENU_TIER then
		-- Get the selection pointing to the previous menu
		if Global_last_main_menu_tier == MULTIPLAYER_TIER then
			current_option = ui_main_menu_get_option_from_data(Top_menu_data, Rfa_mm.group and CAMPAIGN_ID or MULTIPLAYER_ID) -- [RFA] (by game: under Armageddon)
			sub_data = MENU_DATA.MULTIPLAYER_SUB_MENU
		elseif Global_last_main_menu_tier == OPTIONS_TIER then
			current_option = ui_main_menu_get_option_from_data(Top_menu_data, OPTIONS_ID)
			sub_data = MENU_DATA.OPTIONS_SUB_MENU
		elseif Global_last_main_menu_tier == EXTRAS_TIER then
			current_option = ui_main_menu_get_option_from_data(Top_menu_data, EXTRAS_ID)
			if is_signed_into_profile() then
				sub_data = MENU_DATA.EXTRAS_SUB_MENU
			else
				sub_data = MENU_DATA.CREDITS_SUB_MENU
			end			
		elseif Global_last_main_menu_tier == LEVEL_EDITOR_TIER then -- [RFA]
			current_option = ui_main_menu_get_option_from_data(Top_menu_data, LEVEL_EDITOR_ID) -- [RFA]
			sub_data = MENU_DATA.LEVEL_EDITOR_SUB_MENU -- [RFA]
		elseif Global_last_main_menu_tier == CAMPAIGN_TIER then -- [RFA]
			current_option = ui_main_menu_get_option_from_data(Top_menu_data, CAMPAIGN_ID) -- [RFA]
			sub_data = ui_main_menu_construct_campaign_sub_menu() -- [RFA]
		elseif Global_last_main_menu_tier == GUERRILLA_TIER then -- [RFA]
			current_option = ui_main_menu_get_option_from_data(Top_menu_data, GUERRILLA_ID) -- [RFA]
			sub_data = MENU_DATA.GUERRILLA_SUB_MENU -- [RFA]
		elseif Global_last_main_menu_tier == DLC_TIER then
			if dlc_sub_menu_is_valid() then
				current_option = ui_main_menu_get_option_from_data(Top_menu_data, DLC_ID)
				sub_data = ui_main_menu_construct_dlc_sub_menu()
			else
				current_option = ui_main_menu_get_option_from_data(Top_menu_data, DLC_ID)
				sub_data = Top_menu_data
			end
		else
			sub_data = Top_menu_data
		end

		--we need to show the first tier so fade/blur out the second tier
		--reset the scale
		List:set_property("scale", 1.0, 1.0)
		List:draw_items(Top_menu_data, 1)
		List.w,List.h = List:get_property("screen_size")
		List:draw_items(Top_menu_data, current_option)
		List:add_mouse_input("ui_main_menu", Mouse_input_tracker)
		Mouse_input_tracker:subscribe(true)
		List:set_highlight(true)
		ui_main_menu_set_screen_hint()

		ui_main_menu_reset_mask_size(1)

		--start the animations
		if init_set then
			-- Coming in to the top menu straight up
			Anims.menu_in:play()
			Sub_list:set_property("alpha", 0)
		else
			-- Coming back from a sub-menu
			Anims.list_in:play()
			Anims.sub_list_out:play()
		end
		Tween_done = false

		List:set_property("blur",0)
		Sub_list:set_property("blur",0)
		menu_dag_group:set_property("depth",-3)

		--draw the second list with temp data
		Sub_list:draw_items(sub_data, Global_last_main_menu_option)
		-- Hide the back button because it doesn't do anything
		Hint_bar:set_hints(nil)
	else
		if new_tier == MULTIPLAYER_TIER then
			-- Set up data
			sub_data = MENU_DATA.MULTIPLAYER_SUB_MENU
		elseif new_tier == OPTIONS_TIER then
			sub_data = MENU_DATA.OPTIONS_SUB_MENU
		elseif new_tier == LEVEL_EDITOR_TIER then -- [RFA]
			sub_data = MENU_DATA.LEVEL_EDITOR_SUB_MENU -- [RFA]
		elseif new_tier == CAMPAIGN_TIER then -- [RFA]
			sub_data = ui_main_menu_construct_campaign_sub_menu() -- [RFA]
		elseif new_tier == GUERRILLA_TIER then -- [RFA]
			sub_data = MENU_DATA.GUERRILLA_SUB_MENU -- [RFA]
		elseif new_tier == EXTRAS_TIER then
			if is_signed_into_profile() then
				sub_data = MENU_DATA.EXTRAS_SUB_MENU
			else
				sub_data = MENU_DATA.CREDITS_SUB_MENU
			end	
		elseif new_tier == DLC_TIER then
			if dlc_sub_menu_is_valid() then
				sub_data = ui_main_menu_construct_dlc_sub_menu()
			else
				local dlc_option = ui_main_menu_get_option_from_data(Top_menu_data, DLC_ID)
				ui_main_menu_change_tier(MAIN_MENU_TIER, dlc_option, init_set)
				return
			end

		end

		Sub_list:set_property("scale", 1.0, 1.0 )
		-- Redraw the second list
		Sub_list:draw_items(sub_data, 1)
		Sub_list.w,Sub_list.h = Sub_list:get_property("screen_size")
		Sub_list:draw_items(sub_data, current_option)
		Sub_list:add_mouse_input("ui_main_menu", Mouse_input_tracker)

		ui_main_menu_reset_mask_size(2)

		--we are showing the second tier so fade/blur it in and push the first tier back
		List:set_property("alpha", 0 )
		Sub_list:set_property("alpha", 0 )
		Sub_list:set_property("blur", 0 )
		menu_dag_group:set_property("depth",-5)
		Anims.sub_list_in:play()

		if init_set then
			-- Coming in directly to a sub-menu
			Anims.list_in2:play()
			List:set_property("scale", 0.6,0.6 )
		else
			-- Coming in to a sub-menu from the top menu
			Anims.list_out:play()
		end

		--redraw the first list, incase we are coming back from a sub screen
		List:draw_items(Top_menu_data, 1)
		--unhighlight the first list
		List:set_highlight(false)
		List:set_property("blur",8.0)
		--shrink the first list
		ui_main_menu_set_screen_hint(new_tier)

		Tween_done = false

		-- Show the back button
		local back_button = get_button_name_for_action("menu_cancel")
		local hint_data = {back_button, "MENU_BACK"}
		Hint_bar:set_hints(hint_data)
		Hint_bar:add_mouse_input("ui_main_menu", Mouse_input_tracker)
		Mouse_input_tracker:subscribe(true)
	end

	-- Update the values
	Global_last_main_menu_tier = new_tier
	Global_last_main_menu_option = current_option
end

-- Matches an ID in an info table and returns the count
function ui_main_menu_get_option_from_data(data, id)
	for i, info in ipairs(data) do
		if info.id == id then
			return i
		end
	end

	return 1
end

function ui_main_menu_animate(tween_h, event_name)
	Tween_done = true
end

function ui_main_menu_load_screen(tween_h, event_name)
	Tween_done = true
	if Next_screen then
		-- Handle special codes
		if Next_screen == NEXT_SCREEN_LOAD_GAME then
			-- Don't push the screen in script, we need to wait for the game state to change
			rf4_main_menu_load_game()

		elseif Next_screen == NEXT_SCREEN_CREDITS then
			-- Need to change game state, use the special function
			rf4_main_menu_credits()

		-- Pass the screen directly through
		else
			rf4_push_ui(Next_screen)
		end
	end
end

-- Getting this means we need to redraw the top level
function ui_main_menu_update(di_h, event_type)
	local update = vint_dataitem_get(di_h)

	if update then
		-- If we're at the top tier already, save out the current selection ID so we
		-- can select it if it's still around after the update
		local saved_id = nil
		if Global_last_main_menu_tier == MAIN_MENU_TIER then
			saved_id = List:get_id()
		end

		ui_main_menu_update_top_menu_and_draw(saved_id)
	end
end

function ui_main_menu_reset_mask_size( list_num )
	local list_w,list_h
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
