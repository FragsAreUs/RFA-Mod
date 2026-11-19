-- What Red Faction: Guerrilla's own interface scripts use that Armageddon's interface Lua doesn't
-- have. Part of the guerrilla_hud mod: GuerrillaBuilder.exe puts this in front of each of Guerrilla's
-- scripts it brings over from your copy of Guerrilla (which is only read; nothing of it is shipped),
-- and our own script for that document after it.
--
-- Each document's script keeps the globals it makes in a table of its own (Armageddon's
-- system_lib.lua), but a global is read from whichever loaded document has it, so these are the
-- same in every document that has them. Reading a global no script defined is an error.

-- Guerrilla's name for the tween state Armageddon calls TWEEN_STATE_WAITING (both are 0).
TWEEN_STATE_IDLE = TWEEN_STATE_WAITING

-- Lua's string library, which the game's Lua doesn't have: find, for a plain part, from the mod.
string = { find = rfg_string_find }

-- Guerrilla's text sizes. Armageddon has fonts of the same names as Guerrilla's, drawn bigger (its font_body half as
-- big again as Guerrilla's; its font_header a little taller), so GuerrillaBuilder.exe scales the texts of some of
-- Guerrilla's documents by their font (Guerrilla's height over Armageddon's) and tells this script which, by the
-- document's text elements' names (rfg_text_scales, later in the document's script). A text_scale Guerrilla's script
-- sets or reads is then scaled the same way, so its script works in Guerrilla's sizes as it did.
-- All of it is local to this document's script (each document has its own copy of these shims): a global would live
-- in one document's table, gone when that document unloads, and reading or making a global that isn't there is an
-- error (2026-11-12: every document's script stopped on one, and the game crashed).
local Text_scale_document = nil		-- this document's name, nil if its texts weren't scaled
local Text_scale_names = {}			-- { { element name, scale }, ... }
local Text_scale_found = 0			-- the document's handle the names were last looked up in
local Text_scale_of = {}			-- element handle -> scale

local function rfg_text_scales(document, scales)
	Text_scale_document = document
	Text_scale_names = scales
	Text_scale_found = 0
end

local function rfg_text_scale(handle)
	if Text_scale_document == nil then
		return 1
	end
	-- The names looked up in the document, again whenever it was loaded again (its elements' handles are new).
	local found = vint_document_find(Text_scale_document)
	if found == nil then
		found = 0
	end
	if found ~= Text_scale_found then
		Text_scale_found = found
		Text_scale_of = {}
		if found ~= 0 then
			for i, pair in ipairs(Text_scale_names) do
				local element = vint_object_find(pair[1], 0, found)
				if element ~= nil and element ~= 0 then
					Text_scale_of[element] = pair[2]
				end
			end
		end
	end
	local scale = Text_scale_of[handle]
	if scale == nil then
		return 1
	end
	return scale
end

-- Guerrilla's scripts give and read tints as 0-1, Armageddon's interface as 0-255 (in its documents both are 0-1):
-- GuerrillaBuilder.exe makes their vint_set_property and vint_get_property calls these (local to the document too).
local function rfg_vint_set_property(handle, property, ...)
	-- Guerrilla's game named many texts by their string tag's hash (text_tag_crc), which Armageddon's
	-- string tables don't have: our bridges hand the words instead, as a string.
	if property == "text_tag_crc" then
		local value = ...
		if type(value) == "string" then
			return vint_set_property(handle, "text_tag", value)
		end
	end
	-- A text set by one of Guerrilla's string tags (MENU_MISSION_FAILED_TEXT, HM_CONVOY_TITLE, ...): Armageddon's string
	-- tables haven't Guerrilla's strings, so its text from the mod (rfg_guerrilla_text: Guerrilla's strings by the tag's
	-- hash); anything else (words, numbers) as it is.
	if property == "text_tag" then
		local value = ...
		if type(value) == "string" and _GetAnyGlobalSilent("rfg_guerrilla_text") ~= nil then
			local text = rfg_guerrilla_text(value)
			if text ~= nil then
				return vint_set_property(handle, property, text)
			end
		end
	end
	if property == "tint" then
		local r, g, b = ...
		return vint_set_property(handle, property, r * 255, g * 255, b * 255)
	end
	if property == "text_scale" then
		local x, y = ...
		local scale = rfg_text_scale(handle)
		if x ~= nil and y ~= nil then
			return vint_set_property(handle, property, x * scale, y * scale)
		end
	end
	return vint_set_property(handle, property, ...)
end

local function rfg_vint_get_property(handle, property, ...)
	if property == "tint" then
		local r, g, b = vint_get_property(handle, property, ...)
		return r / 255, g / 255, b / 255
	end
	if property == "text_scale" then
		local x, y = vint_get_property(handle, property, ...)
		local scale = rfg_text_scale(handle)
		if x ~= nil and y ~= nil then
			return x / scale, y / scale
		end
		return x, y
	end
	return vint_get_property(handle, property, ...)
end

-- What Guerrilla's game answered, as Armageddon's has it.
function rfg_is_multiplayer()		-- the Guerrilla world is single player
	return false
end

function rfg_play_audio(name)		-- Guerrilla's interface sounds aren't brought over yet
end

function rfg_get_max_charges()		-- how many remote charges the player can have out, from the mod (0 without it)
	if _GetAnyGlobalSilent("rfg_charges_most") ~= nil then
		return rfg_charges_most()
	end
	return 0
end

-- Its language (the handbook lays its texts out right to left for Arabic): Armageddon's get_language() gives a
-- name; the user's strings here are English.
LANGUAGE_MODE_AR = -1
function rfg_get_language()
	return 0
end

-- Guerrilla's controller buttons for its four weapon slots (its globals), and their pictures:
-- none, since its weapon select shows them only for a controller and Armageddon's PC keys pick
-- the weapons here.
Btn_slot_0 = 24
Btn_slot_1 = 25
Btn_slot_2 = 26
Btn_slot_3 = 27

function rfg_get_button_image_name(button)
	return "null"
end

-- Its menu buttons (its rfg_ui_globals' Btn_menu_select and Btn_menu_abort; their pictures: none, as above, our
-- scripts set Guerrilla's PC key pictures), and its screens' button input switched off while their animations play: the
-- mod reads the keys of our screens, so it's told (rfg_screen_input), if the mod has it.
Btn_menu_select = 10
Btn_menu_abort = 11

function rfg_enable_button_input(enable)
	if _GetAnyGlobalSilent("rfg_screen_input") ~= nil then
		rfg_screen_input(enable)
	end
end

-- Its activity-complete screen starting each tally's count (rfg_start_activity_complete(1 to 3), as each block fades
-- in: the numbers count up from 0, hud_ui_activity_complete_start_tally): the mod counts them, if it has it.
function rfg_start_activity_complete(tally)
	if _GetAnyGlobalSilent("rfg_activity_complete_tally") ~= nil then
		rfg_activity_complete_tally(tally)
	end
end

-- Its control and morale colours (its rfg_ui_globals), where Armageddon's own don't have them.
if not pcall(function() return COLOR_CONTROL end) then
	COLOR_CONTROL = { R = 0.58, G = 0.05, B = 0 }
end
if not pcall(function() return COLOR_MORALE end) then
	COLOR_MORALE = { R = 0.943, G = 0.78, B = 0.20 }
end

-- Guerrilla's game filled the world map's data items (FS_HighlightItem, FS_Hints, the group
-- FS_POI_Group, ...), which Armageddon has none of: there our bridge calls the script's callbacks
-- itself, handing a table of the item's values in place of the item. GuerrillaBuilder.exe makes the
-- world map's vint_dataitem_get calls this, and its subscriptions the two below.
function rfg_vint_dataitem_get(item, ...)
	if type(item) == "table" then
		return unpack(item, 1, item.n or #item)	-- n: how many, for a table with empty (nil) values
	end
	return vint_dataitem_get(item, ...)
end

-- A subscription to an item or group Armageddon doesn't have isn't an error then.
function rfg_vint_dataitem_add_subscription(...)
	local values = { ... }
	return pcall(function() return vint_dataitem_add_subscription(unpack(values)) end)
end

function rfg_vint_datagroup_add_subscription(...)
	local values = { ... }
	return pcall(function() return vint_datagroup_add_subscription(unpack(values)) end)
end

-- Guerrilla's script helpers: a number from a string (0 for none), and a whole number between two
-- (the game's Lua has no math library: a plain sequence, enough for a picture's tilt).
function rfg_tonumber(value)
	return tonumber(value) or 0
end

Rfg_rand_state = 12345
function rfg_rand(low, high)
	Rfg_rand_state = (Rfg_rand_state * 16807) % 2147483647	-- stays exact in Lua's doubles
	return low + Rfg_rand_state % (high - low + 1)
end
