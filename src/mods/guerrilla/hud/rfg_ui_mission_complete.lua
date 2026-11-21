-- Red Faction: Guerrilla's mission-complete screen in Armageddon's Guerrilla world, its activity path: an area defense's
-- ("HEAVY METAL") loss: the title, FAILED, why (YOU LEFT THE AREA, YOU DIED), and its hints: Enter to retry, Esc to exit.
-- The game is paused while it's open. (Guerrilla's raids and house arrest showed their losses here too; its missions
-- used the same document another way, not ported.)
--
-- Part of the guerrilla_hud mod. The document this drives, rfg_ui_mission_complete, is Guerrilla's own, and so is its
-- script, both from your copy of Guerrilla: GuerrillaBuilder.exe packs our shims, Guerrilla's script (its init and
-- cleanup renamed rfg_ui_mission_complete_guerrilla_init and _cleanup, its set_hints renamed too) and this script, in
-- that order; run it again after changing this file. Guerrilla's game filled its data item ActivityComplete
-- (process_activity_information RM 0x845B80, the same values as its activity-complete screen's); Armageddon has none of
-- it: this script calls Guerrilla's callback itself with the values in a table, from what the mod answers
-- (rfg_activity_complete). The mod has the screen's rules (guerrilla_hud.cpp, "Guerrilla's activity-complete screen").

local ARMAGEDDON_HUD = "non_subtitles"	-- Armageddon's HUD (ui_hud), all but its subtitles
local DOCUMENT = "rfg_ui_mission_complete"
local Version = -1
local Thread = -1
local Hud_hidden = false
local Hud_was_visible = true
local Failed = {}

local function log(text)
	if rfa_log ~= nil then
		rfa_log("[rfg_ui_mission_complete] " .. text)
	end
end

-- One of Guerrilla's callbacks with a table of values; its first error of each kind is logged.
local function call(callback, name, values)
	local ok, problem = pcall(callback, values, "update")
	if not ok and not Failed[name] then
		Failed[name] = true
		log(name .. ": " .. tostring(problem))
	end
	return ok
end

-- Guerrilla hid its HUD for this screen (Hide_hud_ref_count, hud_ui_activity_complete_enter 0x846400): the mod draws
-- none of Guerrilla's then, and this hides Armageddon's own, each frame, and puts it back as it was once the screen
-- closes.
local function hide_armageddon_hud(hide)
	local hud = vint_object_find(ARMAGEDDON_HUD, 0, vint_document_find("ui_hud"))
	if hud == 0 then
		return
	end
	if hide then
		if not Hud_hidden then
			Hud_was_visible = vint_get_property(hud, "visible") ~= false
			Hud_hidden = true
		end
		vint_set_property(hud, "visible", false)
	elseif Hud_hidden then
		vint_set_property(hud, "visible", Hud_was_visible)
		Hud_hidden = false
	end
end

-- mc_failed_anim: the stamp, the title's line and the hints fading in (mc_hints_main_alpha_twn_12, whose end event,
-- rfg_ui_mc_enable_button_input, lets the keys in). Guerrilla's activity_complete sets it waiting (TWEEN_STATE_IDLE)
-- and Guerrilla's interface then played it from the screen's start; Armageddon's doesn't (as with the activity-complete
-- screen's animations), so it's started here after each activity_complete call, always from the screen's start, so its
-- timing stays as Guerrilla's.
local FAILED_ANIMATION = "mc_failed_anim"
local HINTS_TWEEN = "mc_hints_main_alpha_twn_12"
local Screen_start = 0			-- the document's time the screen's animations count from
local Keys_called = false

local function play_failed()
	local anim = vint_object_find(FAILED_ANIMATION)
	if anim == nil or anim == 0 then
		log("no " .. FAILED_ANIMATION .. " in the document: its hints stay hidden")
		return
	end
	vint_set_property(anim, "start_time", Screen_start)
	vint_set_child_tween_state(anim, TWEEN_STATE_RUNNING)
end

-- The hints' tween's end event lets the keys in. Kept as a guard: once that tween's end is reached (its start_time and
-- duration) with the keys still out, its callback is called here, once.
local function follow_keys(keys_in)
	if keys_in or Keys_called then
		return
	end
	local tween = vint_object_find(HINTS_TWEEN)
	if tween == nil or tween == 0 then
		return
	end
	local name = rfg_end_event_of(tween)
	if name == nil then
		return		-- not set yet: activity_complete hasn't run
	end
	local at = vint_get_property(tween, "start_time")
	local length = vint_get_property(tween, "duration")
	if at ~= nil and length ~= nil and vint_get_time_index() < Screen_start + at + length then
		return
	end
	Keys_called = true
	log("the hints' end event (" .. tostring(name) .. ") called here")
	local callback = _GetAnyGlobalSilent(name)
	if callback ~= nil then
		local ok, problem = pcall(callback)
		if not ok then
			log(tostring(name) .. ": " .. tostring(problem))
		end
	end
end

-- ActivityComplete as Guerrilla's process_activity_information filled it: shown, the screen's number (8 area defense),
-- complete, the title's tag, the failure's tag (nil for none), each hint's key picture and text (the pictures nil
-- until the keys are let in). Handed again whenever it changes (the keys let in: the hints' pictures come up).
local function send_complete()
	local open, version, mode, complete, title, failure, hint_1_image, hint_1_text, hint_2_image, hint_2_text = rfg_activity_complete()
	if not open then
		return
	end
	call(activity_complete, "activity_complete", { true, mode, complete, title, failure, hint_1_image, hint_1_text,
		hint_2_image, hint_2_text, n = 9 })
	local ok, problem = pcall(play_failed)	-- as Guerrilla's activity_complete set mc_failed_anim waiting
	if not ok then
		log(FAILED_ANIMATION .. ": " .. tostring(problem))
	end
	log("mode " .. tostring(mode) .. ", " .. tostring(title) .. (complete and " complete" or " failed") ..
		(failure ~= nil and (" (" .. tostring(failure) .. ")") or "") .. (hint_1_image ~= nil and ", keys in" or ""))
end

local function follow()
	while true do
		-- Guerrilla's tweens' start events, which Armageddon's interface doesn't call (the shims keep them).
		rfg_follow_start_events()
		local open, version, mode, complete, title, failure, hint_1_image = rfg_activity_complete()
		if open or Hud_hidden then
			hide_armageddon_hud(open)
		end
		if open then
			if version ~= Version then
				Version = version
				send_complete()
			end
			follow_keys(hint_1_image ~= nil)
		else
			Version = -1
			Keys_called = false
		end
		thread_yield()
	end
end

function rfg_ui_mission_complete_init()
	local ok, problem = pcall(rfg_ui_mission_complete_guerrilla_init)
	if not ok then
		log("Guerrilla's init: " .. tostring(problem))
	end
	Screen_start = vint_get_time_index(vint_document_find(DOCUMENT))
	if _GetAnyGlobalSilent("rfg_activity_complete") == nil then
		log("no rfg_activity_complete (an older guerrilla_hud.dll?): the screen stays empty")
		return
	end
	Thread = thread_new("rfg_ui_mission_complete_follow")
end

function rfg_ui_mission_complete_follow()
	follow()
end

function rfg_ui_mission_complete_cleanup()
	if Thread ~= -1 then
		thread_kill(Thread)
		Thread = -1
	end
	pcall(hide_armageddon_hud, false)
	pcall(rfg_ui_mission_complete_guerrilla_cleanup)
end
