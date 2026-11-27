-- Override of ui_options_gameplay.lua from <game>\build\pc\cache\interface.vpp_pc
-- (Options > Gameplay). rfa_loader.dll gives the game this file instead of its own copy.
--
-- Adds a Field of View slider (45-170), saved to rfa_mods.ini through rfa_get_setting and
-- rfa_set_setting, which the loader adds. Also Options > Experimentals: the main and pause menus
-- open this screen with the global Rfa_options_page "experimentals", and it then shows RFA-Mod's own
-- settings (Rfa_experimentals) in place of the game's; Options > Main Menu opens it with "main_menu", for
-- the main menu's layout (Rfa_main_menu). Every line we added or changed is marked [RFA];
-- everything else is the game's original script. Only the [RFA] lines are this project's (GNU GPL
-- version 3 or later, see LICENSE); the rest belongs to the game's owners.
-- To compare with the original, extract it with the editor.

local FINE_AIM_SNAP_INDEX		= 1
local HELP_MESSAGE_INDEX		= 2

-- [RFA] Field of View slider, kept in rfa_mods.ini
local FOV_INDEX					= 3

-- these are optional based on game state
-- put permanent options above
local GAMEPLAY_RESET_PROFILE	= 4 -- [RFA] was 3 (the slider takes 3)
local DIFFICULTY_INDEX			= 5 -- [RFA] was 4

local Data = {
	[FINE_AIM_SNAP_INDEX] = {
		type = TYPE_TOGGLE,
		label = "MENU_FINE_AIM_SNAP",
		options = {"MENU_ENABLED", "MENU_DISABLED"},
		current_value = 1
	},
	[HELP_MESSAGE_INDEX] = {
		type = TYPE_TOGGLE,
		label = "MENU_HELP_MESSAGES",
		options = {"MENU_ENABLED", "MENU_DISABLED"},
		current_value = 1
	},
	-- [RFA] Field of View slider
	[FOV_INDEX] = {
		type = TYPE_SLIDER,
		label = "Field of View",
		min = 45,
		max = 170,
		step = 5,
		current_value = 60,
		section = "FOV", key = "VerticalFov", default = 60,
	},
	[GAMEPLAY_RESET_PROFILE] = {
		type = TYPE_BUTTON,
		label = "MENU_RESET_PROFILE",
	},	
	[DIFFICULTY_INDEX] = {
		type = TYPE_TOGGLE,
		label = "MENU_DIFFICULTY",
		options = {"MENU_CASUAL", "MENU_NORMAL", "MENU_HARD", "MENU_INSANE"},
		current_value = 2
	},	
}

-- [RFA] Options > Experimentals: RFA-Mod's own settings, each kept in rfa_mods.ini (its section,
-- key and default there; a toggle saves 1 or 0). The Guerrilla world's change while you play.
local Rfa_experimentals = {
	{type = TYPE_TOGGLE, label = "Sun's shadows", options = {"MENU_ENABLED", "MENU_DISABLED"}, current_value = 1,
		section = "GuerrillaWorld", key = "SunShadows", default = 1},
	{type = TYPE_TOGGLE, label = "Sun shafts", options = {"MENU_ENABLED", "MENU_DISABLED"}, current_value = 1,
		section = "GuerrillaWorld", key = "SunShafts", default = 1},
	{type = TYPE_TOGGLE, label = "Guerrilla's fog", options = {"MENU_ENABLED", "MENU_DISABLED"}, current_value = 1,
		section = "GuerrillaWorld", key = "Fog", default = 1},
	{type = TYPE_TOGGLE, label = "Blowing dust", options = {"MENU_ENABLED", "MENU_DISABLED"}, current_value = 1,
		section = "GuerrillaWorld", key = "Dust", default = 1},
	{type = TYPE_TOGGLE, label = "Guerrilla's particles", options = {"MENU_ENABLED", "MENU_DISABLED"}, current_value = 1,
		section = "GuerrillaWorld", key = "Particles", default = 1},
	{type = TYPE_TOGGLE, label = "Guerrilla's tone mapping", options = {"MENU_ENABLED", "MENU_DISABLED"}, current_value = 1,
		section = "GuerrillaWorld", key = "ToneMapping", default = 1},
	{type = TYPE_TOGGLE, label = "Colour correction", options = {"MENU_ENABLED", "MENU_DISABLED"}, current_value = 1,
		section = "GuerrillaWorld", key = "ColourCorrection", default = 1},
	{type = TYPE_SLIDER, label = "Sky brightness", min = 0, max = 10, step = 1, current_value = 5,
		section = "GuerrillaWorld", key = "Brightness", default = 5},
	{type = TYPE_SLIDER, label = "Clock speed", min = 0, max = 120, step = 4, current_value = 48,
		section = "GuerrillaWorld", key = "ClockSpeed", default = 48},
	-- Ruin levels made with the editor can have squads; the game turns enemies' hostility off in Ruin
	-- (rfa_qol's ruin_ai), so they only fight with this on. It changes at once.
	{type = TYPE_TOGGLE, label = "Enemies fight in Ruin", options = {"MENU_ENABLED", "MENU_DISABLED"}, current_value = 2,
		section = "Ruin", key = "HostileAI", default = 0},
	-- RFA-Mod's MCP servers (live_analysis_mcp.exe and the editor's RfaEditorMcp.exe, for an AI assistant):
	-- [Mcp] Enabled, off by default. rfa_qol opens or closes the game's live analysis pipe at once; the servers
	-- themselves read it as they start, so the program that runs them needs starting again after a change.
	{type = TYPE_TOGGLE, label = "MCP servers", options = {"MENU_ENABLED", "MENU_DISABLED"}, current_value = 2,
		section = "Mcp", key = "Enabled", default = 0},
}
-- [RFA] Options > Main Menu: how the main menu is laid out ([MainMenu] in rfa_mods.ini; ui_main_menu_pc.lua
-- reads them each time it opens).
local Rfa_main_menu = {
	{type = TYPE_TOGGLE, label = "Layout", options = {"BY GAME", "CLASSIC"}, current_value = 1,
		section = "MainMenu", key = "GroupByGame", default = 1},
	{type = TYPE_TOGGLE, label = "Continue at the top", options = {"MENU_ENABLED", "MENU_DISABLED"}, current_value = 1,
		section = "MainMenu", key = "ContinueAtTop", default = 1},
	{type = TYPE_TOGGLE, label = "Red Faction: Guerrilla", options = {"MENU_ENABLED", "MENU_DISABLED"}, current_value = 1,
		section = "MainMenu", key = "ShowGuerrilla", default = 1},
	{type = TYPE_TOGGLE, label = "Level editor", options = {"MENU_ENABLED", "MENU_DISABLED"}, current_value = 1,
		section = "MainMenu", key = "ShowLevelEditor", default = 1},
	{type = TYPE_TOGGLE, label = "Descriptions", options = {"MENU_ENABLED", "MENU_DISABLED"}, current_value = 1,
		section = "MainMenu", key = "Descriptions", default = 1},
	{type = TYPE_TOGGLE, label = "Decorations", options = {"MENU_ENABLED", "MENU_DISABLED"}, current_value = 1,
		section = "MainMenu", key = "Decorations", default = 1},
	{type = TYPE_TOGGLE, label = "Open where you left off", options = {"MENU_ENABLED", "MENU_DISABLED"}, current_value = 1,
		section = "MainMenu", key = "Remember", default = 1},
	-- Classic (the game's) or Modern (flat and sharp, in an accent colour: ui_main_menu_pc.lua's rfa_modern_style)
	{type = TYPE_TOGGLE, label = "Style", choice = true, current_value = 1, options = {"CLASSIC", "MODERN"},
		section = "MainMenu", key = "Style", default = 0},
	{type = TYPE_TOGGLE, label = "Accent (Modern)", choice = true, current_value = 1,
		options = {"BLUE", "ORANGE", "RED", "GREEN", "VIOLET", "WHITE"},
		section = "MainMenu", key = "Accent", default = 0},
	-- Any of the game's music for the menu: its music modes, in the loader's order (rfa_loader's kMusicModes),
	-- saved as the place in the list from 0 (choice: not a toggle's 1 or 0). It plays as soon as it is saved.
	{type = TYPE_TOGGLE, label = "Music", choice = true, current_value = 1,
		options = {"THE MENU'S OWN",
			"MISSION 1", "MISSION 2", "MISSION 3", "MISSION 4", "MISSION 5", "MISSION 6", "MISSION 7", "MISSION 8",
			"MISSION 9", "MISSION 12", "MISSION 13", "MISSION 14", "MISSION 16", "MISSION 16B", "MISSION 17A",
			"MISSION 17B", "MISSION 17C",
			"SIDE QUEST 1", "SIDE QUEST 2", "SIDE QUEST 3", "SIDE QUEST 4", "SIDE QUEST 6", "SIDE QUEST 7",
			"SIDE QUEST 8", "SIDE QUEST 10",
			"INFESTATION A", "INFESTATION B", "INFESTATION C", "INFESTATION D", "INFESTATION E", "INFESTATION F",
			"INFESTATION G", "INFESTATION H",
			"RUIN", "CREDITS"},
		section = "MainMenu", key = "Music", default = 0},
}
local Rfa_experimental = false -- [RFA] showing Rfa_experimentals, Rfa_main_menu or Rfa_create_level (ours, none of the game's)
-- [RFA] Level editor > Create level: the menu's own page for a new level (what the editor's New level asks, all of it
-- chosen here; the editor then makes it with no dialog and opens it). Mode, ground and ground size are kept in
-- rfa_mods.ini ([NewLevel]); the names are typed (the loader's rfa_text_*: the game has no text box); the ground
-- textures and skies are what the editor listed when it last read the game's files (rfa_new_level_choices).
local Rfa_page = "gameplay" -- [RFA] which page this is (Rfa_options_page as it opened)
local Rfa_create_level = nil -- [RFA] its rows, made as it opens
local RFA_ROW_MODE, RFA_ROW_FILE, RFA_ROW_DISPLAY, RFA_ROW_GROUND, RFA_ROW_SIZE, RFA_ROW_TEXTURES, RFA_ROW_SKY = 1, 2, 3, 4, 5, 6, 7 -- [RFA]
local RFA_SIZES = {32, 64, 96, 128, 160, 192, 224, 255} -- [RFA] the ground's size, metres (New level's)
local RFA_CREATE_HINT = "A new level, made by the level editor and opened in it once it has read the game's files" -- [RFA]
local Rfa_typing = nil -- [RFA] the row being typed in
local Rfa_settle = false -- [RFA] just after typing: the key that ended it isn't taken for a press too
local Rfa_name_typed = false -- [RFA] the file name was typed (else it follows the mode: custom_ruin_01...)

local Anims = {}

local Input_tracker
local Mouse_input_tracker
local List
local Hint_bar
local Hint_data -- [RFA] kept so the hints can come back after a slider closes

local Tween_done = true

local Texticles = {}

local Warned_difficulty = false

-- [RFA] Everything from here to ui_options_gameplay_init is ours.
-- The entries with a key (the FOV, and every one in Experimentals) are read from and saved to
-- rfa_mods.ini through functions the mod loader adds. A toggle is enabled (1) for anything but 0.
local function rfa_options_populate()
	if rfa_get_setting == nil then
		return
	end
	for index, data in pairs(Data) do
		if data.key ~= nil then
			local value = rfa_get_setting(data.section, data.key, data.default)
			if data.choice then -- [RFA] a choice of many: its place from 0
				if value < 0 or value >= #data.options then value = 0 end
				data.current_value = value + 1
			elseif data.type == TYPE_TOGGLE then
				data.current_value = (value ~= 0) and 1 or 2
			else
				if value < data.min then
					value = data.min
				elseif value > data.max then
					value = data.max
				end
				data.current_value = value
			end
		end
	end
end

-- Also called while a slider moves, so its change shows behind the menu straight away. Unchanged
-- settings aren't written (the loader checks).
local function rfa_options_save()
	if rfa_set_setting == nil then
		return
	end
	for index, data in pairs(Data) do
		if data.key ~= nil then
			if data.choice then -- [RFA]
				rfa_set_setting(data.section, data.key, data.current_value - 1)
			elseif data.type == TYPE_TOGGLE then
				rfa_set_setting(data.section, data.key, (data.current_value == 1) and 1 or 0)
			else
				rfa_set_setting(data.section, data.key, data.current_value)
			end
		end
	end
	-- [RFA] Main Menu's Style and Accent take effect at once: the shared colours (vdo_button_highlight.lua's
	-- [RFA] rfa_theme_apply; what is drawn after takes them) and this page's own highlight.
	local apply = _GetAnyGlobalSilent("rfa_theme_apply") -- [RFA]
	if Rfa_page == "main_menu" and apply ~= nil then -- [RFA]
		apply() -- [RFA]
		local highlight = List.highlight -- [RFA]
		if highlight ~= nil and highlight.left ~= nil then -- [RFA]
			for i, part in ipairs({highlight.left, highlight.center, highlight.right}) do -- [RFA]
				part:set_color(COLOR_HIGHLIGHT_BAR.R, COLOR_HIGHLIGHT_BAR.G, COLOR_HIGHLIGHT_BAR.B) -- [RFA]
			end -- [RFA]
			rfa_theme_highlight(highlight) -- [RFA]
		end -- [RFA]
	end -- [RFA]
end

-- As in the audio menu: an open slider shows its own confirm/cancel, so the hint bar hides.
local function rfa_update_hints()
	local data = List:return_selected_data()
	if List:return_state() and data ~= nil and data.type == TYPE_SLIDER then
		Hint_bar:set_hints()
	else
		Hint_bar:set_hints(Hint_data)
	end
	if Mouse_input_tracker ~= nil then
		Hint_bar:update_mouse_inputs("ui_options_gameplay", Mouse_input_tracker)
	end
end

-- The list's own mouse handling assumes sliders start at 0; this one starts at 45.
local function rfa_slider_to_mouse(mouse_x)
	local data = List:return_selected_data()
	local min_x, min_y, max_x = element_get_bbox(List.slider:get_meter_handle())
	local value = round(data.min + (mouse_x - min_x) / (max_x - min_x) * (data.max - data.min))
	if value < data.min then
		value = data.min
	elseif value > data.max then
		value = data.max
	end
	if value ~= data.current_value then
		play_ui_sound( "INT_TOGGLE_UD" )
	end
	data.current_value = value
	List:move_slider(0)
	rfa_options_save()
end

-- Level editor > Create level (see Rfa_create_level above).
local function rfa_create_rows()
	local textures, skies, sizes = {}, {}, {}
	local count = rfa_new_level_choices("texture")
	for i = 1, count do textures[i] = rfa_new_level_choice("texture", i) or "?" end
	if count == 0 then textures[1] = "AUTOMATIC" end -- the editor hasn't listed them yet: it takes the first
	local sky_count = rfa_new_level_choices("sky")
	local sky_default = 1
	for i = 1, sky_count do
		skies[i] = rfa_new_level_choice("sky", i) or "?"
		if skies[i] == "env_sur_day_sky" then sky_default = i end
	end
	if sky_count == 0 then skies[1] = "env_sur_day_sky" end
	for i, size in ipairs(RFA_SIZES) do sizes[i] = size .. " M" end
	sizes[#sizes] = "255 M (THE WHOLE ZONE)"
	Rfa_create_level = {
		{type = TYPE_TOGGLE, label = "Mode", choice = true, current_value = 1, options = {"RUIN", "INFESTATION"},
			section = "NewLevel", key = "Mode", default = 0},
		{type = TYPE_BUTTON, label = "File name", current_value = 1, options = {""}, rfa_text = "file", rfa_value = ""},
		{type = TYPE_BUTTON, label = "Name in the menu", current_value = 1, options = {""}, rfa_text = "display", rfa_value = ""},
		{type = TYPE_TOGGLE, label = "Ground", choice = true, current_value = 1, options = {"NEW, FLAT", "NONE (CAVES, INTERIORS)"},
			section = "NewLevel", key = "Ground", default = 0},
		{type = TYPE_TOGGLE, label = "Ground size", choice = true, current_value = #RFA_SIZES, options = sizes,
			section = "NewLevel", key = "GroundSize", default = #RFA_SIZES - 1},
		{type = TYPE_TOGGLE, label = "Ground textures", current_value = 1, options = textures, rfa_auto = (count == 0)},
		{type = TYPE_TOGGLE, label = "Sky", current_value = sky_default, options = skies},
		{type = TYPE_BUTTON, label = "CREATE LEVEL", rfa_create = true},
	}
end

local function rfa_mode_name()
	return (Data[RFA_ROW_MODE].current_value == 2) and "infestation" or "ruin"
end

-- What a text row shows when not being typed in.
local function rfa_text_shown(data)
	if data.rfa_text == "display" and data.rfa_value == "" then return "(THE FILE NAME)" end
	return data.rfa_value
end

-- A text row's value, on screen too (the list keeps its buttons: the highlight takes the new width).
local function rfa_show_text(row, text)
	Data[row].options[1] = text
	local button = (List.buttons ~= nil) and List.buttons[row] or nil
	if button ~= nil then
		button:set_value(text)
		if List:get_selection() == row and not List:return_state() then List.highlight:set_width(button:get_text_width()) end
	end
end

-- A free file name for the mode, unless one was typed.
local function rfa_suggest_name()
	if Rfa_name_typed then return end
	Data[RFA_ROW_FILE].rfa_value = rfa_new_level_suggest(rfa_mode_name())
	Data[RFA_ROW_FILE].options[1] = Data[RFA_ROW_FILE].rfa_value
	Data[RFA_ROW_DISPLAY].options[1] = rfa_text_shown(Data[RFA_ROW_DISPLAY])
end

local function rfa_popup(message, callback, options)
	Input_tracker:subscribe(false)
	if Mouse_input_tracker ~= nil then Mouse_input_tracker:subscribe(false) end
	rf4_popup_create(message, callback, options, #options)
end

function ui_options_gameplay_rfa_popup_done()
	Input_tracker:subscribe(true)
	if Mouse_input_tracker ~= nil then Mouse_input_tracker:subscribe(true) end
end

function ui_options_gameplay_rfa_settle()
	for i = 1, 15 do thread_yield() end
	Rfa_settle = false
end

local function rfa_end_typing(keep)
	local row = Rfa_typing
	if row == nil then return end
	local text = rfa_text_state()
	rfa_text_end()
	Rfa_typing = nil
	local data = Data[row]
	if keep and (text ~= "" or data.rfa_text == "display") then
		data.rfa_value = text
		if data.rfa_text == "file" then Rfa_name_typed = true end
	end
	rfa_show_text(row, rfa_text_shown(data))
	if Mouse_input_tracker ~= nil then Mouse_input_tracker:subscribe(true) end
	Hint_bar:set_value(RFA_CREATE_HINT)
	Rfa_settle = true
	thread_new("ui_options_gameplay_rfa_settle")
end

-- Each frame while typing: what is typed, on the row (the loader's window hook has the keyboard: Enter keeps it,
-- Esc puts back what it was).
function ui_options_gameplay_rfa_typing()
	while Rfa_typing ~= nil do
		local text, state = rfa_text_state()
		if state == 0 then
			rfa_show_text(Rfa_typing, text .. "_")
		else
			rfa_end_typing(state == 1)
		end
		thread_yield()
	end
end

local function rfa_begin_typing(row)
	local data = Data[row]
	if not rfa_text_begin(data.rfa_value, data.rfa_text) then
		rfa_popup("Typing needs RFA-Mod's in-game menus (the loader hooks the game's window for them). The suggested file name can still be used.",
			"ui_options_gameplay_rfa_popup_done", {"MENU_OK"})
		return
	end
	play_ui_sound("INT_SELECT")
	Rfa_typing = row
	if Mouse_input_tracker ~= nil then Mouse_input_tracker:subscribe(false) end
	Hint_bar:set_value(data.rfa_text == "file" and "Type the file name (letters, digits and _): Enter keeps it, Esc puts back the old one"
		or "Type the name the map list shows: Enter keeps it, Esc puts back the old one")
	thread_new("ui_options_gameplay_rfa_typing")
end

-- Leaves the page, as Back does.
local function rfa_leave()
	Input_tracker:subscribe(false)
	if Mouse_input_tracker ~= nil then Mouse_input_tracker:subscribe(false) end
	Anims.list_in:stop()
	Anims.list_out:play()
	Anims.list_out_tween = Vdo_tween_object:new("list_anchor_twn_2")
	Anims.list_out_tween:set_property("end_event", "ui_options_gameplay_anim_done")
	Tween_done = false
end

-- Starts the editor with everything chosen, and goes back to the main menu, which shows the processing screen
-- until the editor has read the game's files and made the level (ui_main_menu_rfa_back_from_create).
local function rfa_create_go(replace)
	local ground = (Data[RFA_ROW_GROUND].current_value == 2) and "none" or "new"
	local textures_row = Data[RFA_ROW_TEXTURES]
	local textures = textures_row.rfa_auto and "" or textures_row.options[textures_row.current_value]
	local sky = Data[RFA_ROW_SKY].options[Data[RFA_ROW_SKY].current_value]
	local size = RFA_SIZES[Data[RFA_ROW_SIZE].current_value] or 255
	local started = rfa_open_level_editor(rfa_mode_name(), Data[RFA_ROW_FILE].rfa_value, Data[RFA_ROW_DISPLAY].rfa_value,
		ground, size, textures, sky, replace)
	if started then
		rawset(_UGGlobals, "Rfa_create_wait", true)
	else
		rawset(_UGGlobals, "Rfa_create_wait", "The level editor couldn't be started: editor\\RfaEditor.exe wasn't found next to the mod. logs\\rfa_mods.log says more.")
	end
	rfa_options_save()
	rfa_leave()
end

function ui_options_gameplay_rfa_replace(return_value)
	ui_options_gameplay_rfa_popup_done()
	if return_value == 1 then rfa_create_go(true) end
end

local function rfa_create()
	local name = Data[RFA_ROW_FILE].rfa_value
	local check = rfa_new_level_check(name)
	if check == "bad" then
		rfa_popup("The file name has to be 2 to 31 lower case letters, digits and _, a letter first.", "ui_options_gameplay_rfa_popup_done", {"MENU_OK"})
	elseif check == "game" then
		rfa_popup(name .. " is one of the game's own levels: choose another file name.", "ui_options_gameplay_rfa_popup_done", {"MENU_OK"})
	elseif check == "taken" then
		rfa_popup("There is already a level " .. name .. " made with the editor. Make it again, new? Its edits are lost.",
			"ui_options_gameplay_rfa_replace", {"MENU_ACCEPT", "MENU_CANCEL"})
	else
		rfa_create_go(false)
	end
end

function ui_options_gameplay_init()
	-- Subscribe to the button presses we need
	Input_tracker = Vdo_input_tracker:new()
	Input_tracker:add_input("menu_select", "ui_options_gameplay_button_a", 50)
	Input_tracker:add_input("menu_cancel", "ui_options_gameplay_button_b", 50)
	Input_tracker:add_input("nav_up", "ui_options_gameplay_nav_up", 50)
	Input_tracker:add_input("nav_down", "ui_options_gameplay_nav_down", 50)
	Input_tracker:add_input("nav_left", "ui_options_gameplay_nav_left", 50)
	Input_tracker:add_input("nav_right", "ui_options_gameplay_nav_right", 50)
	Input_tracker:subscribe(true)
	
	if is_pc() then
		Mouse_input_tracker = Vdo_input_tracker:new()
	end

	-- Initialize the list object
	List = Vdo_mega_list:new("list")

	-- Set the title and hints
	local hint_doc_handle
	local ingame = vint_document_find("ui_background_ingame")
	local frontend = vint_document_find("ui_background_video")
	if ingame == 0 then
		hint_doc_handle = frontend
	else
		hint_doc_handle = ingame
	end
	Hint_bar = Vdo_hint_bar:new("hint_bar",0,hint_doc_handle)
	
	-- Set up the correct back button for the platform
	local back_button = get_button_name_for_action("menu_cancel")
	Hint_data = {back_button, "MENU_BACK"} -- [RFA] was a local
	Hint_bar:set_hints(Hint_data)

	-- [RFA] Options > Experimentals: our list in place of the game's, and none of its settings. (The
	-- page is kept with rawset and read with _GetAnyGlobalSilent, system_lib's: the menus' Lua
	-- refuses to write or read a global that wasn't declared as its script loaded.)
	local rfa_page = _GetAnyGlobalSilent("Rfa_options_page") -- [RFA]
	Rfa_page = rfa_page or "gameplay" -- [RFA]
	Rfa_experimental = (rfa_page == "experimentals" or rfa_page == "main_menu" or rfa_page == "create_level") -- [RFA]
	rawset(_UGGlobals, "Rfa_options_page", "gameplay")
	if Rfa_experimental then
		Data = (rfa_page == "main_menu") and Rfa_main_menu or Rfa_experimentals -- [RFA]
		if rfa_page == "create_level" then -- [RFA]
			rfa_create_rows() -- [RFA]
			Data = Rfa_create_level -- [RFA]
		end -- [RFA]
		FINE_AIM_SNAP_INDEX = nil
		HELP_MESSAGE_INDEX = nil
		FOV_INDEX = nil
		GAMEPLAY_RESET_PROFILE = nil
		DIFFICULTY_INDEX = nil
		if rfa_page == "create_level" then -- [RFA]
			Hint_bar:set_header("CREATE LEVEL") -- [RFA]
			Hint_bar:set_value(RFA_CREATE_HINT) -- [RFA]
		elseif rfa_page == "main_menu" then -- [RFA]
			Hint_bar:set_header("MAIN MENU") -- [RFA]
			Hint_bar:set_value("How the main menu is laid out and what it shows, saved in rfa_mods.ini") -- [RFA]
		else -- [RFA]
		Hint_bar:set_header("EXPERIMENTALS")
		Hint_bar:set_value("RFA-Mod's settings for the Guerrilla world, saved in rfa_mods.ini")
		end -- [RFA]
	else -- [RFA] the game's own, as before
	Hint_bar:set_header("MENU_GAMEPLAY_SETTINGS")
	Hint_bar:set_value("MENU_CHANGE_GAMEPLAY_SETTINGS")
	
	local show_difficulty = true
	local show_reset_profile = true
	
	if rf4_wrecking_crew_is_initialized() == true or mp_is_multiplayer() == true then
		show_difficulty = false
		show_reset_profile = false
	end
	
	if is_in_gameplay() == true then
		show_reset_profile = false
	else
		show_difficulty = false
	end
	
	if is_signed_into_profile() == false then
		show_reset_profile = false
	end
	
	if show_reset_profile == false then
		-- in game swap difficulty and reset profile, remove reset profile
		Data[GAMEPLAY_RESET_PROFILE] = Data[DIFFICULTY_INDEX]
		Data[DIFFICULTY_INDEX] = nil
		DIFFICULTY_INDEX = GAMEPLAY_RESET_PROFILE
		GAMEPLAY_RESET_PROFILE = nil
	end

	if show_difficulty == false then
			Data[DIFFICULTY_INDEX] = nil
			DIFFICULTY_INDEX = nil
	end

	-- Get the current settings
	vint_dataresponder_request("ui_gameplay_populate", "ui_options_gameplay_populate")
	end -- [RFA]
	rfa_options_populate() -- [RFA]
	if Rfa_page == "create_level" then -- [RFA] its mode as kept: a free file name for it
		Rfa_name_typed = false -- [RFA]
		rfa_suggest_name() -- [RFA]
	end -- [RFA]
	
	-- Draw the list for the first time
	List:draw_items(Data, 1, 650)
	
	if Mouse_input_tracker ~= nil then
		Hint_bar:add_mouse_input("ui_options_gameplay", Mouse_input_tracker)
		List:add_mouse_input("ui_options_gameplay", Mouse_input_tracker)
		Mouse_input_tracker:subscribe(true)
	end

	Anims.list_in = Vdo_anim_object:new("gameplay_anim")
	Anims.list_in:play()

	Anims.list_out = Vdo_anim_object:new("gameplay_out_anim")
	Anims.list_out:stop()
	
	--====================================================================================
	-- Techno background stuff
	--====================================================================================
	local tech_module = Vdo_tech_module:new("tech_module")
	tech_module:module_animate_in()
	
	local tech_meter = Vdo_tech_meter:new("tech_meter")
	tech_meter:meter_animate_in()

	-- Little scrolling texticles
	Texticles.scrolling_text1 = Vdo_scrolling_text:new("scrolling_text1")
	Texticles.scrolling_text2 = Vdo_scrolling_text:new("scrolling_text2")
	Texticles.scrolling_text3 = Vdo_scrolling_text:new("scrolling_text3")
	local text1_string = {
						[1] = "23908475290348   324578345234785   2374589239485729";
						[2] = "259sdrose384           2349857398547tam87694";
						[3] = "457368954746           845gnuechtel4576498";
						[4] = "43297873                                                95874698567584938";
						[5] = "54898062                                                084533498023tam2311111";
						[6] = "3423425645345987345345                          5468974593";
						[7] = "\n ";
						[8] = "879549738495765                                      0721334432";
						[9] = "4508montgomery570234                                                         534schnieder3049345";
						[10] = "453986tam48567498567                             5698749683475sdrose7233245";
						[11] = "5schnieder8405   54                                081234874";
						[12] = "4325435345                                            50897596470394tam603485";
						}
		local text2_string = {
						[1] = "214523215234563        78   7899925349582734535                                                    984759schnieder423453745";
						[2] = "987653498679345        54  3290875montgomery544235324                      234098501montgomery8945689";
						[3] = "234589893284         096802356234789273485938533479485";
						[4] = "20394850236753460983495gnuechtel72394568723945890685";
						[5] = "\n ";
						[6] = "048952345273495271340985111152309487520635         42039845620893457690348gnuechtel39246534890756983456";
						[7] = "45827394587234985678023094867923856               0895234609834579683745638495654689734";
						[8] = "gnuechtel montgomery schnieder tam";
						}
	
	Texticles.scrolling_text1:run_text(text1_string, 0.001, true, 5)
	Texticles.scrolling_text2:run_text(text2_string, 0.001, true, 3)
	Texticles.scrolling_text3:run_text(text1_string, 0.01, true, 2)
end

function ui_options_gameplay_cleanup()
	if Rfa_typing ~= nil then rfa_text_end() end -- [RFA] the keyboard is the game's again
	Rfa_typing = nil -- [RFA]
	-- Nuke all button subscriptions	
	Input_tracker:subscribe(false)
	
	if Mouse_input_tracker ~= nil then
		Mouse_input_tracker:subscribe(false)
	end
end

function ui_options_gameplay_populate(fine_aim_snap, help_messages, difficulty)
	Data[FINE_AIM_SNAP_INDEX].current_value = fine_aim_snap and 1 or 2
	
	if Data[HELP_MESSAGE_INDEX] ~= nil then
		Data[HELP_MESSAGE_INDEX].current_value = help_messages and 1 or 2
	end
	
	if Data[DIFFICULTY_INDEX] ~= nil then
		Data[DIFFICULTY_INDEX].current_value = difficulty	
	end
end

function ui_options_gameplay_return_values()
	-- [RFA] Experimentals has none of the game's settings, only ours
	if Rfa_experimental then
		rfa_options_save()
		return
	end

	-- May not have help message value
	local help_message_enabled = nil
	if Data[HELP_MESSAGE_INDEX] ~= nil then
		help_message_enabled = Data[HELP_MESSAGE_INDEX].current_value == 1
	end

	-- We may not have a difficulty value
	local difficulty = nil
	if Data[DIFFICULTY_INDEX] ~= nil then
		difficulty = Data[DIFFICULTY_INDEX].current_value
	end

	rf4_gameplay_options_return_values(
		Data[FINE_AIM_SNAP_INDEX].current_value == 1,
		help_message_enabled,
		difficulty)
	rfa_options_save() -- [RFA]
end


function ui_options_gameplay_nav_up(event, acceleration)
	if Rfa_typing ~= nil or Rfa_settle then return end -- [RFA] the keyboard is typing
	-- Move highlight up
	List:move_cursor(-1)
end

function ui_options_gameplay_nav_down(event, acceleration)
	if Rfa_typing ~= nil or Rfa_settle then return end -- [RFA] the keyboard is typing
	-- Move highlight down
	List:move_cursor(1)
end

function ui_options_gameplay_nav_left(event, acceleration)
	if Rfa_typing ~= nil or Rfa_settle then return end -- [RFA] the keyboard is typing
	-- Move highlight left
	List:move_slider(-1)
	rfa_options_save() -- [RFA] shows the new FOV while the slider moves
end

function ui_options_gameplay_nav_right(event, acceleration)
	if Rfa_typing ~= nil or Rfa_settle then return end -- [RFA] the keyboard is typing
	-- Move highlight right
	List:move_slider(1)
	rfa_options_save() -- [RFA] shows the new FOV while the slider moves
end

function ui_options_gameplay_difficulty_return(return_value)
	if return_value == 1 then
		Warned_difficulty = true
		
		-- Immediately pass off the input if valid
		if List:get_selection() == DIFFICULTY_INDEX then
			ui_options_gameplay_button_a()
		end
	end
end

function ui_options_gameplay_button_a(event, acceleration)
	if Rfa_typing ~= nil then -- [RFA] typing: a pad's A keeps it (the keyboard's Enter does that itself)
		local gamepad = _GetAnyGlobalSilent("pc_is_active_input_gamepad") -- [RFA]
		if gamepad ~= nil and gamepad() then rfa_end_typing(true) end -- [RFA]
		return -- [RFA]
	end -- [RFA]
	if Rfa_settle then return end -- [RFA]
	if Tween_done == true and Rfa_page == "create_level" and not List:return_state() then -- [RFA] Create level's names and its button
		local data = List:return_selected_data() -- [RFA]
		if data ~= nil and data.rfa_text ~= nil then -- [RFA]
			rfa_begin_typing(List:get_selection()) -- [RFA]
			return -- [RFA]
		elseif data ~= nil and data.rfa_create then -- [RFA]
			play_ui_sound( "INT_SELECT" ) -- [RFA]
			rfa_create() -- [RFA]
			return -- [RFA]
		end -- [RFA]
	end -- [RFA]
	if Tween_done == true then
		local selection = List:get_selection()
	
		if selection == GAMEPLAY_RESET_PROFILE then
			play_ui_sound( "INT_SELECT" )
		
			rf4_reset_upgrades_and_salvage()
			
		elseif selection == DIFFICULTY_INDEX and not Warned_difficulty and rf4_is_signed_into_profile() == true and not rf4_dlc_are_dlc04_missions_active() then
			play_ui_sound( "INT_SELECT" )
		
			-- Warn the user about changing difficulty
			rf4_popup_create("PLT_DIFFICULTY_CHANGE_WARNING","ui_options_gameplay_difficulty_return", {"MENU_ACCEPT", "MENU_CANCEL"}, 2)
			
		else
			if not List:return_state() then
				-- Set the current highlighted index as the selected index
				local highlight_idx = List:get_highlight_index()
				List:set_selection(highlight_idx)
			elseif selection == DIFFICULTY_INDEX then
				-- Reset the warning flag on close
				Warned_difficulty = false
			end
			
			--set the screen data to the list data
			Data = List:return_data()
			--pass off the input to the list
			List:button_a("ui_options_gameplay")
			ui_options_gameplay_return_values()
			rfa_update_hints() -- [RFA]
			if Rfa_page == "create_level" and not List:return_state() then -- [RFA] a mode chosen: its file name follows
				rfa_suggest_name() -- [RFA]
				rfa_show_text(RFA_ROW_FILE, Data[RFA_ROW_FILE].options[1]) -- [RFA]
			end -- [RFA]
		end
	end
end

function ui_options_gameplay_button_b(event, acceleration)
	if Rfa_typing ~= nil or Rfa_settle then return end -- [RFA] typing: Esc is the loader's (puts the text back)
	if Tween_done == true then
		play_ui_sound( "INT_BACK" )
	
		--set the screen data to the list data
		Data = List:return_data()
			--check if the list is open or closed
		if List:return_state() == true then
			--pass off the input to the list
			List:button_b()
			-- [RFA] A cancelled slider is back at its old value; put that FOV back too
			rfa_options_save() -- [RFA]
			rfa_update_hints() -- [RFA]
		else
			--back up a screen
			Anims.list_in:stop()
			Anims.list_out:play()
			Anims.list_out_tween = Vdo_tween_object:new("list_anchor_twn_2")
			Anims.list_out_tween:set_property("end_event", "ui_options_gameplay_anim_done")
			Tween_done = false
		end
	end
end

-- [RFA] also takes mouse_x and mouse_y, for the slider
function ui_options_gameplay_mouse_click(event, target_handle, mouse_x, mouse_y)
	-- First check if the target_handle refers to the Hint_bar
	if Hint_bar.hint_button_hit_boxes[1].handle == target_handle then
		ui_options_gameplay_button_b(event, 0)
		return
	end
	
	local new_index
	local is_list_open = List:return_state()
	
	if not is_list_open then
		-- Enter an option if the target_handle is in the Mega_list
		new_index = List:get_button_index(target_handle)
		if new_index ~= 0 then
			List:set_selection(new_index)
			List:move_cursor(0, true, false)
			ui_options_gameplay_button_a(event, 0)
		end
		
	else
		--Confirm the selection in the toggle or change in the slider
		local data = List:return_selected_data()
		if data.type == TYPE_TOGGLE then 
			new_index = List.list:get_button_index(target_handle)
			if new_index ~= 0 then
				List.list:set_selection(new_index)
				ui_options_gameplay_button_a(event, 0)
				return
			end
		elseif data.type == TYPE_SLIDER then -- [RFA] the game's screen had no slider
			if target_handle == List.slider:get_meter_handle() then
				rfa_slider_to_mouse(mouse_x)
			elseif target_handle == List.slider:get_confirm_handle() then
				ui_options_gameplay_button_a(event, 0)
			elseif target_handle == List.slider:get_cancel_handle() then
				ui_options_gameplay_button_b(event, 0)
			end
		end
	end
end

function ui_options_gameplay_mouse_move(event, target_handle)
	-- Reset all highlights
	Hint_bar:set_highlight(0)
	List.slider:set_button_highlights(0) -- [RFA]
	
	-- Do nothing if the target_handle is invalid
	if target_handle == INVALID_VDO_HANDLE then
		return
	end

	-- First check if the target_handle refers to the Hint_bar
	if Hint_bar.hint_button_hit_boxes[1].handle == target_handle then
		Hint_bar:set_highlight(1)
		return
	end
	
	local new_index
	local is_list_open = List:return_state()
	
	if not is_list_open then
		-- Get the button's index in the Mega_list given its handle
		new_index = List:get_button_index(target_handle)
		if new_index ~= 0 then
			List:set_highlight_index(new_index)
			List:set_selection(new_index)
			List:move_cursor(0, true)
			return
		end
		
	else
		-- If a button wasn't found in the Mega_list, check the toggle list
		local data = List:return_selected_data()
		if data.type == TYPE_TOGGLE then
			new_index = List.list:get_button_index(target_handle)
			if new_index ~= 0 then
				List.list:set_selection(new_index)
				List.list:move_cursor(0, true)
			end
		elseif data.type == TYPE_SLIDER then -- [RFA]
			if target_handle == List.slider:get_confirm_handle() then
				List.slider:set_button_highlights(1)
			elseif target_handle == List.slider:get_cancel_handle() then
				List.slider:set_button_highlights(2)
			end
		end
	end
end

-- [RFA] new: dragging the slider's bar
function ui_options_gameplay_mouse_drag(event, target_handle, mouse_x, mouse_y)
	local data = List:return_selected_data()
	if List:return_state() and data ~= nil and data.type == TYPE_SLIDER then
		rfa_slider_to_mouse(mouse_x)
	end
end

function ui_options_gameplay_anim_done(tween_h, event_name)
	--Tween_done = true
	rf4_pop_ui()
end
