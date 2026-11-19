-- Red Faction: Guerrilla's messages in Armageddon's Guerrilla world: its two message lines (the major one in the
-- middle, the minor one at the top left), its use prompt under the reticle (a hand, a ladder, a vehicle, a mission's
-- start, the upgrades, each with the use key's picture), its weapon swap (the held weapon, an arrow, the one on the
-- ground, their rounds), what is picked up beside the reticle (weapons, rounds, salvage: at most three rows) and its
-- saving icon. Armageddon's own message lines, salvage counter, objective banner and use prompt are hidden while
-- these show.
--
-- Part of the guerrilla_hud mod. The document this drives, rfg_ui_messages, is Guerrilla's own, and so is its
-- script, both from your copy of Guerrilla: GuerrillaBuilder.exe packs our shims, Guerrilla's script (its init
-- and cleanup renamed rfg_ui_messages_guerrilla_init and _cleanup) and this script, in that order; run it again
-- after changing this file. Guerrilla's game told the script what to show through data items (UseData, SwapData,
-- the group AmmoData) and drove its message lines itself, which Armageddon hasn't: this script calls Guerrilla's
-- callbacks with the values in a table (the shims' rfg_vint_dataitem_get reads either) and runs the message lines
-- as Guerrilla's code did (RM ui_process_secondary_messages: 3 s each, one at a time, the same words never twice at
-- once). What to show comes from the mod (rfg_use_prompt, rfg_pickups, rfg_pickup, rfg_message_next, rfg_saving;
-- see guerrilla_hud.cpp, "Guerrilla's messages") and from Armageddon's own message lines (messages_hud).

local USE_KEY = "rfg_key_24"            -- Guerrilla's PC key picture for E, the use key's default
local MESSAGE_SECONDS = 3               -- Guerrilla's message lines' time when none is given
local MAJOR_FADE_SECONDS = 0.3
local ROW_SECONDS = 2.5                 -- a pickup row's time if its animation's can't be read
-- Guerrilla's saving icon, as its utility document (rfg_ui_utility, not brought over) has it under small_saving:
-- "SAVING..." (the text its game falls back to) and the save icon, orange, beside it.
local SAVING_TEXT = "SAVING..."
local SAVING_ICON = "rfg_ui_hud_save_icon"

-- The document's parts our script shows itself (hidden until then, else their placeholder texts show:
-- "NEW MISSION AVAILABLE", "Pressing text").
local HIDDEN = { "small_saving", "usage_text_major_group1", "usage_text_minor_group1", "usage_text_perm_group1",
	"saving_effects", "salvage_add", "message_swap_main", "message_ammo" }
-- Armageddon's HUD (ui_hud) parts these replace: its message lines, salvage counter and objective banner (the
-- magoo, its vdo_magoo: a grey bar at the top right, which showed empty in the Guerrilla world; Guerrilla's free roam
-- has none), hidden while this is loaded; and its use prompt, hidden unless the action is one Guerrilla's prompts
-- have no picture for.
local ARMAGEDDON_LINES = { "update_lines", "salvage_group", "magoo" }
local ARMAGEDDON_PROMPT = "prompt_grp"
local MAGOO_LOGGED_MOST = 10

local Thread = -1
local Failed = {}
local Use_shown = 0			-- the use_type shown now; 0 = no prompt
local Swap_shown = ""		-- what the swap shows now; "" = nothing
local Prompt_hidden = false	-- Armageddon's use prompt hidden by us
local Pickup_version = -1
local Rows = 0				-- pickup rows in Guerrilla's script now
local Rows_until = 0
local Row_seconds = ROW_SECONDS
local Minor = { queue = {}, text = nil, ends = 0 }
local Major = { queue = {}, text = nil, ends = 0, seconds = 0 }
local Minor_out_seconds = 0.5
local Minor_two_lines = 0	-- the text's height from which it takes two lines (the second line's background shows)
local Subscribing = false
local Saving_text = 0
local Saving_icon = 0
local Magoo_logged = 0

local function log(text)
	if rfa_log ~= nil then
		rfa_log("[rfg_ui_messages] " .. text)
	end
end

local function now()
	return vint_get_time_index(vint_document_find("rfg_ui_messages"))
end

-- One of Guerrilla's callbacks with a table of values; its first error of each kind is logged.
local function call(callback, name, values, event)
	local ok, problem = pcall(callback, values, event or "update")
	if not ok and not Failed[name] then
		Failed[name] = true
		log(name .. ": " .. tostring(problem))
	end
	return ok
end

local function armageddon(name)
	return vint_object_find(name, 0, vint_document_find("ui_hud"))
end

local function set_visible(name, visible)
	local handle = vint_object_find(name)
	if handle ~= 0 then
		vint_set_property(handle, "visible", visible)
	end
end

-- A tween's end in its animation (start_time + duration), or nil.
local function tween_end(name)
	local tween = vint_object_find(name)
	if tween == 0 then
		return nil
	end
	local ok, start, duration = pcall(function()
		return vint_get_property(tween, "start_time"), vint_get_property(tween, "duration")
	end)
	if not ok or start == nil or duration == nil then
		return nil
	end
	return start + duration
end

---------------------------------------------------------------- the message lines

local function queued(line, text)
	if line.text == text then
		return true
	end
	for i, waiting in ipairs(line.queue) do
		if waiting.text == text then
			return true
		end
	end
	return false
end

local function add_message(major, text, seconds)
	if text == nil or text == "" then
		return
	end
	local line = Minor
	if major then
		line = Major
	end
	if queued(line, text) then
		return		-- Guerrilla's ui_secondary_string_exists: the same words aren't shown twice at once
	end
	if seconds == nil or seconds <= 0 then
		seconds = MESSAGE_SECONDS
	end
	line.queue[#line.queue + 1] = { text = text, seconds = seconds }
end

local function next_of(line)
	local first = line.queue[1]
	if first == nil then
		return nil
	end
	local rest = {}
	for i = 2, #line.queue do
		rest[#rest + 1] = line.queue[i]
	end
	line.queue = rest
	return first
end

-- The minor line as Guerrilla's code started it: the words, the second line's background for two lines, the
-- in, the dags and the out animation (the out one ending as the time does).
local function minor_start(message, at)
	local text = vint_object_find("usage_text_minor1")
	vint_set_property(text, "text_tag", message.text)
	vint_set_property(text, "alpha", 1)
	local width, height = vint_get_property(text, "screen_size")
	set_visible("minor_line2", Minor_two_lines > 0 and height ~= nil and height >= Minor_two_lines)
	local anim_in, anim, anim_out = vint_object_find("usage_text_minor_anim_in"), vint_object_find("usage_text_minor_anim"),
		vint_object_find("usage_text_minor_anim_out")
	vint_set_property(anim_in, "start_time", at)
	vint_set_property(anim, "start_time", at)
	local out_at = at + message.seconds - Minor_out_seconds
	if out_at < at then
		out_at = at
	end
	vint_set_property(anim_out, "start_time", out_at)
	vint_set_child_tween_state(anim_in, TWEEN_STATE_WAITING)
	vint_set_child_tween_state(anim, TWEEN_STATE_WAITING)
	vint_set_child_tween_state(anim_out, TWEEN_STATE_WAITING)
	for i, name in ipairs({ "usage_text_minor_anim_in_2", "usage_text_minor_anim_2", "usage_text_minor_anim_out_2" }) do
		vint_set_child_tween_state(vint_object_find(name), TWEEN_STATE_DISABLED)
	end
	set_visible("usage_text_minor_group1", true)
	Minor.text = message.text
	Minor.ends = at + message.seconds
end

local function minor_stop()
	for i, name in ipairs({ "usage_text_minor_anim_in", "usage_text_minor_anim", "usage_text_minor_anim_out" }) do
		vint_set_child_tween_state(vint_object_find(name), TWEEN_STATE_DISABLED)
	end
	set_visible("usage_text_minor_group1", false)
	Minor.text = nil
end

local function major_start(message, at)
	local group = vint_object_find("usage_text_major_group1")
	vint_set_property(vint_object_find("usage_text_major1"), "text_tag", message.text)
	vint_set_property(group, "alpha", 1)
	vint_set_property(group, "visible", true)
	Major.text = message.text
	Major.ends = at + message.seconds
end

local function major_stop()
	set_visible("usage_text_major_group1", false)
	Major.text = nil
end

local function follow_lines(at)
	while true do
		local more, text, seconds = nil, nil, nil
		if rfg_message_next ~= nil then
			more, text, seconds = rfg_message_next()
		end
		if more == nil then
			break
		end
		add_message(more, text, seconds)
	end
	if Minor.text ~= nil and at >= Minor.ends then
		minor_stop()
	end
	if Minor.text == nil then
		local message = next_of(Minor)
		if message ~= nil then
			minor_start(message, at)
		end
	end
	if Major.text ~= nil then
		local left = Major.ends - at
		if left <= 0 then
			major_stop()
		elseif left < MAJOR_FADE_SECONDS then
			vint_set_property(vint_object_find("usage_text_major_group1"), "alpha", left / MAJOR_FADE_SECONDS)
		end
	end
	if Major.text == nil then
		local message = next_of(Major)
		if message ~= nil then
			major_start(message, at)
		end
	end
end

-- Armageddon's own message lines (messages_hud: a line, or HUD_MESSAGE_CLEAR_ALL) go to Guerrilla's minor line.
function rfg_ui_messages_armageddon_line(di_h, event_type)
	local line = vint_dataitem_get(di_h)
	if Subscribing then
		return		-- the line it had before this document came (subscribing hands it over at once)
	end
	if line == nil or line == "" then
		return
	end
	if line == "HUD_MESSAGE_CLEAR_ALL" then
		Minor.queue = {}
		minor_stop()
		return
	end
	add_message(false, line, nil)
end

-- Armageddon's objective banner (magoo_main_item: state, sticky, banner text, ...): hidden here (see
-- ARMAGEDDON_LINES); what asks for it is written to the log, to find out what shows it in the Guerrilla world.
function rfg_ui_messages_armageddon_magoo(di_h, event_type)
	if Magoo_logged >= MAGOO_LOGGED_MOST then
		return
	end
	Magoo_logged = Magoo_logged + 1
	local state, sticky, banner_text, banner_fade_out, manual, visible = vint_dataitem_get(di_h)
	log("Armageddon's objective banner (hidden): state " .. tostring(state) .. ", visible " .. tostring(visible) ..
		", text '" .. tostring(banner_text) .. "'" .. (Subscribing and " (as this document came)" or ""))
end

---------------------------------------------------------------- pickups

local function remove_rows()
	for i = 1, Rows do
		call(ammo_remove, "ammo_remove", { n = 0 }, "remove")
	end
	Rows = 0
end

-- Guerrilla's AmmoData row: which, total_num, is_full, ammo_icon, ammo_text, salvage_increase. A row of salvage
-- shows the total (and "+N" over it from 3 up), a weapon's "+ N" rounds (nothing for none).
local function send_row(which, count, event)
	local picture, amount, total, salvage = rfg_pickup(which)
	if picture == nil then
		return
	end
	local text = ""
	local increase = 0
	if salvage then
		text = tostring(total)
		increase = amount
	elseif amount > 0 then
		text = "+ " .. tostring(amount)
	end
	call(ammo_info, "ammo_info", { which, count, false, picture, text, increase, n = 6 }, event)
end

local function follow_pickups(at)
	if rfg_pickups == nil then
		return
	end
	local version, count, merged = rfg_pickups()
	if Pickup_version == -1 then
		Pickup_version = version	-- what was picked up before this document came isn't shown
	elseif version ~= Pickup_version then
		Pickup_version = version
		if merged and Rows == 1 and at < Rows_until then
			send_row(1, 1, "update")
		else
			remove_rows()
			for which = 1, count do
				send_row(which, count, "insert")
				Rows = Rows + 1
			end
		end
		Rows_until = at + Row_seconds
	end
	if Rows > 0 and at >= Rows_until then
		remove_rows()
	end
end

---------------------------------------------------------------- the use prompt and the weapon swap

-- Hidden each frame (its script may show it again) unless the action is one only it can show.
local function hide_armageddon_prompt(hide)
	if not hide and not Prompt_hidden then
		return
	end
	local prompt = armageddon(ARMAGEDDON_PROMPT)
	if prompt ~= 0 then
		vint_set_property(prompt, "visible", not hide)
	end
	Prompt_hidden = hide
end

local function follow_prompts(at)
	local use_type, swap, current_icon, next_icon, current_ammo, next_ammo, armageddon_own = 0, false, "", "", -1, -1, false
	if rfg_use_prompt ~= nil then
		use_type, swap, current_icon, next_icon, current_ammo, next_ammo, armageddon_own = rfg_use_prompt()
	end
	-- Guerrilla hid its use prompt while a pickup row showed (hud_ui_process_use_action_info). Its weapon swap stands in
	-- the same place as the first row (message_swap_main and message_ammo), and Guerrilla never gave both at once (a
	-- weapon it couldn't take gave no rounds); Armageddon takes the ground weapon's rounds while it offers the swap, so
	-- the swap waits for the rows too.
	if Rows > 0 then
		use_type = 0
		swap = false
	end
	if use_type ~= Use_shown then
		-- Hiding keeps the picture it had, as it fades out.
		local picture = use_type
		if use_type == 0 then
			picture = Use_shown
		end
		call(use_info, "use_info", { use_type ~= 0, picture, USE_KEY, n = 3 })
		Use_shown = use_type
	end
	local swap_shown = ""
	if swap then
		swap_shown = current_icon .. "|" .. next_icon .. "|" .. tostring(current_ammo) .. "|" .. tostring(next_ammo)
	end
	if swap_shown ~= Swap_shown then
		if swap then
			local current, current_rounds, next_rounds = nil, nil, nil
			if current_icon ~= "" then
				current = current_icon
			end
			if current_ammo >= 0 then
				current_rounds = tostring(current_ammo)
			end
			if next_ammo >= 0 then
				next_rounds = tostring(next_ammo)
			end
			-- SwapData: is_visible, swap_type, next_icon, current_icon, button_name, current_ammo, next_ammo
			call(swap_info, "swap_info", { true, 0, next_icon, current, USE_KEY, current_rounds, next_rounds, n = 7 })
		else
			call(swap_info, "swap_info", { false, 0, nil, nil, nil, nil, nil, n = 7 })
		end
		Swap_shown = swap_shown
	end
	-- Guerrilla's prompts replace Armageddon's, also while a pickup row hides ours (2026-11-12: its "[E] Enter"
	-- showed at a car then).
	hide_armageddon_prompt(armageddon_own ~= true)
end

---------------------------------------------------------------- the saving icon

local function build_saving()
	local group = vint_object_find("small_saving")
	if group == 0 then
		return
	end
	Saving_text = vint_object_create("rfg_saving_text", "text", group)
	vint_set_property(Saving_text, "font", "font_body")
	vint_set_property(Saving_text, "text_tag", SAVING_TEXT)
	vint_set_property(Saving_text, "anchor", 145.28, 17.9)
	vint_set_property(Saving_text, "scale", 0.7, 0.7)
	vint_set_property(Saving_text, "tint", 255, 128, 0)
	Saving_icon = vint_object_create("rfg_saving_icon", "bitmap", group)
	vint_set_property(Saving_icon, "image", SAVING_ICON)
	-- Guerrilla's: 33 x 34, centred on (118.04, 29.09), scale 0.85, turned -6.74 radians.
	vint_set_property(Saving_icon, "anchor", 118.04 - 16.5 * 0.85, 29.09 - 17 * 0.85)
	vint_set_property(Saving_icon, "scale", 0.85, 0.85)
	vint_set_property(Saving_icon, "rotation", -6.736959934234619)
	vint_set_property(Saving_icon, "tint", 255, 128, 0)
end

local function follow_saving()
	local shown = rfg_saving ~= nil and rfg_saving()
	set_visible("small_saving", shown)
end

---------------------------------------------------------------- every frame

local function frame()
	local at = now()
	-- Armageddon's message lines and salvage counter: its script shows them again with each new one.
	for i, name in ipairs(ARMAGEDDON_LINES) do
		local part = armageddon(name)
		if part ~= 0 then
			vint_set_property(part, "visible", false)
		end
	end
	follow_lines(at)
	follow_pickups(at)
	follow_prompts(at)
	follow_saving()
end

local function follow()
	while true do
		local ok, problem = pcall(frame)
		if not ok then
			log("stopped: " .. tostring(problem))
			return
		end
		thread_yield()
	end
end

function rfg_ui_messages_init()
	local ok, problem = pcall(rfg_ui_messages_guerrilla_init)
	if not ok then
		log("Guerrilla's init: " .. tostring(problem))
	end
	for i, name in ipairs(HIDDEN) do
		set_visible(name, false)
	end
	-- The minor line's height for one line (from which two show its second background) and its out animation's
	-- length; a pickup row's length (its animation's fade).
	local text = vint_object_find("usage_text_minor1")
	if text ~= 0 then
		vint_set_property(text, "text_tag", "A")
		local width, height = vint_get_property(text, "screen_size")
		if height ~= nil and height > 0 then
			Minor_two_lines = height * 1.5
		end
	end
	Minor_out_seconds = tween_end("usage_text_minor_group1_anchor_twn_out_1") or Minor_out_seconds
	Row_seconds = tween_end("ammo_alpha_twn_3") or ROW_SECONDS
	build_saving()
	pcall(use_info, { false, 1, USE_KEY, n = 3 }, "update")
	Subscribing = true
	pcall(vint_dataitem_add_subscription, "messages_hud", "update", "rfg_ui_messages_armageddon_line")
	pcall(vint_dataitem_add_subscription, "magoo_main_item", "update", "rfg_ui_messages_armageddon_magoo")
	Subscribing = false
	if rfg_use_prompt == nil then
		return		-- an older mod: nothing to show
	end
	Thread = thread_new("rfg_ui_messages_follow")
end

function rfg_ui_messages_follow()
	follow()
end

function rfg_ui_messages_cleanup()
	if Thread ~= -1 then
		thread_kill(Thread)
		Thread = -1
	end
	hide_armageddon_prompt(false)
	local lines = armageddon("update_lines")
	if lines ~= 0 then
		vint_set_property(lines, "visible", true)
	end
	pcall(rfg_ui_messages_guerrilla_cleanup)
end
