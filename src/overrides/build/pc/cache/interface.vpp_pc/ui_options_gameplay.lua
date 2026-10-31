-- Override of ui_options_gameplay.lua from <game>\build\pc\cache\interface.vpp_pc
-- (Options > Gameplay). rfa_loader.dll gives the game this file instead of its own copy.
--
-- Adds a Field of View slider (45-170), saved to rfa_mods.ini through rfa_get_setting and
-- rfa_set_setting, which the loader adds. Every line we added or changed is marked [RFA];
-- everything else is the game's original script. To compare with the original:
--   python tools\research\vpp.py extract "<game>\build\pc\cache\interface.vpp_pc" ui_options_gameplay.lua extracted

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
		current_value = 60
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
-- The FOV is read from and saved to rfa_mods.ini through functions the mod loader adds.
local function rfa_options_populate()
	if rfa_get_setting == nil then
		return
	end
	local fov = Data[FOV_INDEX]
	fov.current_value = rfa_get_setting("FOV", "VerticalFov", 60)
	if fov.current_value < fov.min then
		fov.current_value = fov.min
	elseif fov.current_value > fov.max then
		fov.current_value = fov.max
	end
end

-- Also called while the slider moves, so the new FOV shows behind the menu straight away.
local function rfa_options_save()
	if rfa_set_setting == nil then
		return
	end
	rfa_set_setting("FOV", "VerticalFov", Data[FOV_INDEX].current_value)
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
	rfa_options_populate() -- [RFA]
	
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
	-- Move highlight up
	List:move_cursor(-1)
end

function ui_options_gameplay_nav_down(event, acceleration)
	-- Move highlight down
	List:move_cursor(1)
end

function ui_options_gameplay_nav_left(event, acceleration)
	-- Move highlight left
	List:move_slider(-1)
	rfa_options_save() -- [RFA] shows the new FOV while the slider moves
end

function ui_options_gameplay_nav_right(event, acceleration)
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
		end
	end
end

function ui_options_gameplay_button_b(event, acceleration)
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
