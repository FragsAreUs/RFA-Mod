-- Red Faction: Guerrilla's demolitions master screens in Armageddon's Guerrilla world: the briefing (the challenge's
-- name, what to do, the weapons it gives with their rounds, the goal time, the text; Enter accepts, Esc cancels) and the
-- results (COMPLETE or FAILED, your time, the pro (par) and best times, the salvage; Enter, Space or Esc closes). The game
-- is paused while one is open.
--
-- Part of the guerrilla_hud mod. The document this drives, rfg_ui_demolitions, is Guerrilla's own, and so is its script,
-- both from your copy of Guerrilla: GuerrillaBuilder.exe packs our shims, Guerrilla's script (its init and cleanup
-- renamed rfg_ui_demolitions_guerrilla_init and _cleanup, its STAMP_PAD renamed too) and this script, in that order; run
-- it again after changing this file. Guerrilla's game filled its data item DemolitionsInfo (process_demolitions_data RM
-- 0x844D50); Armageddon has none of it: this script calls Guerrilla's callback itself with the values in a table, from
-- what the mod answers (rfg_activity, rfg_activity_weapon: the guerrilla_campaign mod's activity, its texts Guerrilla's).
-- Its keys are the mod's, let in as Guerrilla's script let its buttons in (rfg_enable_button_input, in the shims).

local STATE_BRIEFING, STATE_COMPLETE, STATE_FAILED = 0, 3, 4			-- its get_alt_interface_state
local SCREEN_BRIEFING, SCREEN_RESULTS = 1, 3							-- the mod's (the campaign's) screens
local ACCEPT_KEY, CANCEL_KEY = "rfg_key_178", "rfg_key_124"			-- Guerrilla's PC key pictures: Enter, Esc
local ARMAGEDDON_HUD = "non_subtitles"	-- Armageddon's HUD (ui_hud), all but its subtitles
-- The document's own texts named by Guerrilla's string tags (its script measures the stamp's text before drawing its
-- outline): set to Guerrilla's text before its callback runs.
local DOCUMENT_TAGS = { demo_stamp_text = "MENU_MISSION_COMPLETE_TEXT", demo_result_label = "ACT_DM_RESULTS_TAG" }
local Version = -1
local Thread = -1
local Hud_hidden = false
local Hud_was_visible = true
local Failed = {}

local function log(text)
	if rfa_log ~= nil then
		rfa_log("[rfg_ui_demolitions] " .. text)
	end
end

local function call(callback, name, values)
	local ok, problem = pcall(callback, values, "update")
	if not ok and not Failed[name] then
		Failed[name] = true
		log(name .. ": " .. tostring(problem))
	end
	return ok
end

-- A text of Guerrilla's by its tag ("" if the mod hasn't it).
local function text(tag)
	if _GetAnyGlobalSilent("rfg_guerrilla_text") == nil then
		return ""
	end
	return rfg_guerrilla_text(tag) or ""
end

-- A time shown, or nil for none (Guerrilla's screen hides its row then).
local function shown_time(time)
	if time == nil or time == "" then
		return nil
	end
	return time
end

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

-- DemolitionsInfo as Guerrilla's process_demolitions_data filled it: state, title, subtitle, four weapons with their
-- rounds (nil past the last; rounds -1 not shown), the goal time's label and value, the text, your time's, the pro
-- (par) time's and the best time's labels and values, the two hints (cancel and accept at the briefing; none and close
-- at the results), the salvage's label and value ("+N").
local function send()
	local screen, version, mode, title, subtitle, body, objective, timer, result, failure, limit, par, best, elapsed,
		status, success, pro_time, out_of_range, salvage = rfg_activity()
	if screen ~= SCREEN_BRIEFING and screen ~= SCREEN_RESULTS then
		return
	end
	local state = STATE_BRIEFING
	local hint1, hint2 = text("MENU_CANCEL"), text("MENU_ACCEPT")
	if screen == SCREEN_RESULTS then
		state = STATE_FAILED
		if success then
			state = STATE_COMPLETE
		end
		hint1, hint2 = "", text("MENU_CLOSE")
		-- The results' line under the title: the challenge's own success or failure text, else why it failed.
		if result ~= nil and result ~= "" then
			subtitle = result
		elseif failure ~= nil and failure ~= "" then
			subtitle = failure
		end
	end
	local values = { state, title, subtitle, n = 24 }
	for i = 1, 4 do
		local picture, rounds = rfg_activity_weapon(i)
		values[2 + i * 2] = picture
		values[3 + i * 2] = rounds or -1
	end
	values[12] = text("ACT_DM_RESULTS_GOAL_TIME")
	values[13] = shown_time(limit)
	values[14] = body
	values[15] = text("ACT_DM_RESULTS_YOU_TIME")
	values[16] = shown_time(elapsed) or "00:00"
	values[17] = text("ACT_DM_RESULTS_PAR_TIME")
	values[18] = shown_time(par) or "--:--"
	values[19] = text("ACT_DM_RESULTS_BEST_TIME")
	values[20] = shown_time(best) or "--:--"
	values[21] = hint1
	values[22] = hint2
	values[23] = text("ACT_DM_RESULTS_SALVAGE")
	values[24] = "+" .. tostring(salvage or 0)
	for name, tag in pairs(DOCUMENT_TAGS) do
		local handle = vint_object_find(name)
		local shown = text(tag)
		if handle ~= 0 and shown ~= "" then
			vint_set_property(handle, "text_tag", shown)
		end
	end
	call(demolitions_info, "demolitions_info", values)
	-- Its keys as the mod reads them: Esc for cancel, Enter for accept or close (its script asked for the
	-- controller's buttons).
	local cancel, accept = vint_object_find("demo_hint_b"), vint_object_find("demo_hint_a")
	if cancel ~= 0 then
		vint_set_property(cancel, "image", CANCEL_KEY)
	end
	if accept ~= 0 then
		vint_set_property(accept, "image", ACCEPT_KEY)
	end
	log("screen " .. tostring(screen) .. " (state " .. tostring(state) .. "): " .. tostring(title) .. ", " .. tostring(subtitle))
end

local function follow()
	while true do
		local screen, version = rfg_activity()
		local open = screen == SCREEN_BRIEFING or screen == SCREEN_RESULTS
		if open or Hud_hidden then
			hide_armageddon_hud(open)
		end
		if open and version ~= Version then
			Version = version
			send()
		elseif not open then
			Version = -1
		end
		thread_yield()
	end
end

function rfg_ui_demolitions_init()
	local ok, problem = pcall(rfg_ui_demolitions_guerrilla_init)
	if not ok then
		log("Guerrilla's init: " .. tostring(problem))
	end
	if _GetAnyGlobalSilent("rfg_activity") == nil then
		log("no rfg_activity (an older guerrilla_hud.dll?): the screen stays empty")
		return
	end
	Thread = thread_new("rfg_ui_demolitions_follow")
end

function rfg_ui_demolitions_follow()
	follow()
end

function rfg_ui_demolitions_cleanup()
	if Thread ~= -1 then
		thread_kill(Thread)
		Thread = -1
	end
	pcall(hide_armageddon_hud, false)
	pcall(rfg_ui_demolitions_guerrilla_cleanup)
end
