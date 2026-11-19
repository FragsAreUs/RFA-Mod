-- Red Faction: Guerrilla's activity-complete screen in Armageddon's Guerrilla world: the results of a Transporter
-- (Guerrilla's Guerrilla Express: the pro, target and best times, then your time) or a Collateral Damage (its Riding
-- Shotgun: the goal, the damage to soldiers, structures and vehicles, the score, the colonists' deaths, the total), each
-- block counting up as it comes in, COMPLETE or FAILED (and why) stamped over it, then its hints: Enter to continue
-- (or retry after a failure), and Esc to exit after a failure or to retry a Transporter won. The game is paused while
-- it's open. (Guerrilla's raids, house arrest and area defense show its mission-complete screen instead.)
--
-- Part of the guerrilla_hud mod. The document this drives, rfg_ui_activity_complete, is Guerrilla's own, and so is its
-- script, both from your copy of Guerrilla: GuerrillaBuilder.exe packs our shims, Guerrilla's script (its init and
-- cleanup renamed rfg_ui_activity_complete_guerrilla_init and _cleanup, its STAMP_PAD and HINT_PAD renamed too) and this
-- script, in that order; run it again after changing this file. Guerrilla's game filled its data items ActivityComplete
-- (process_activity_information RM 0x845B80) and ActivityTallys (process_activity_tallys 0x856000, counting each tally
-- up once its script starts it: rfg_start_activity_complete, in the shims); Armageddon has none of it: this script calls
-- Guerrilla's callbacks itself with the values in a table, from what the mod answers (rfg_activity_complete,
-- rfg_activity_complete_tallys). The mod has the screen's rules (guerrilla_hud.cpp, "Guerrilla's activity-complete
-- screen").

local ARMAGEDDON_HUD = "non_subtitles"	-- Armageddon's HUD (ui_hud), all but its subtitles
-- The document's own texts named by Guerrilla's string tags that its script leaves as they are (it measures the stamp's
-- text before placing the stamp's outline): set to Guerrilla's text before its callback runs.
local DOCUMENT_TAGS = { complete_text = "MENU_MISSION_COMPLETE_TEXT", goal_label = "ACT_RESULTS_GOAL_TEXT",
	goal_million = "ACT_RESULTS_MILLION", mc_total_million = "ACT_RESULTS_MILLION" }
local Version = -1
local Tallys = ""		-- the numbers last handed to Guerrilla's set_numbers, joined
local Thread = -1
local Hud_hidden = false
local Hud_was_visible = true
local Failed = {}

local function log(text)
	if rfa_log ~= nil then
		rfa_log("[rfg_ui_activity_complete] " .. text)
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

-- A text of Guerrilla's by its tag ("" if the mod hasn't it).
local function text(tag)
	if _GetAnyGlobalSilent("rfg_guerrilla_text") == nil then
		return ""
	end
	return rfg_guerrilla_text(tag) or ""
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

-- ActivityComplete as Guerrilla's process_activity_information filled it: shown, the screen's mode (4 Transporter,
-- 5 Collateral Damage), complete, the title's tag, the failure's tag (nil for none), each hint's key picture and text
-- (the pictures nil until the keys are let in; the second nil when it isn't offered), and hide_overall. Handed again
-- whenever it changes (the keys let in: the hints come up), as Guerrilla's item told its subscriber.
local function send_complete()
	local open, version, mode, complete, title, failure, hint_1_image, hint_1_text, hint_2_image, hint_2_text,
		hide_overall = rfg_activity_complete()
	if not open then
		return
	end
	for name, tag in pairs(DOCUMENT_TAGS) do
		local handle = vint_object_find(name)
		local shown = text(tag)
		if handle ~= 0 and shown ~= "" then
			vint_set_property(handle, "text_tag", shown)
		end
	end
	call(activity_complete, "activity_complete", { true, mode, complete, title, failure, hint_1_image, hint_1_text,
		hint_2_image, hint_2_text, hide_overall, n = 10 })
	log("mode " .. tostring(mode) .. ", " .. tostring(title) .. (complete and " complete" or " failed") ..
		(failure ~= nil and (" (" .. tostring(failure) .. ")") or "") .. (hint_1_image ~= nil and ", keys in" or ""))
end

-- ActivityTallys (the goal, soldiers, structures, vehicles, score, deaths, total: as Guerrilla's
-- process_activity_tallys wrote them), handed whenever they change (each frame while a tally counts up).
local function send_tallys()
	local goal, soldiers, structures, vehicles, score, deaths, total = rfg_activity_complete_tallys()
	local joined = tostring(goal) .. "|" .. tostring(soldiers) .. "|" .. tostring(structures) .. "|" .. tostring(vehicles) ..
		"|" .. tostring(score) .. "|" .. tostring(deaths) .. "|" .. tostring(total)
	if joined == Tallys then
		return
	end
	Tallys = joined
	call(set_numbers, "set_numbers", { goal, soldiers, structures, vehicles, score, deaths, total, n = 7 })
end

local function follow()
	while true do
		local open, version = rfg_activity_complete()
		if open or Hud_hidden then
			hide_armageddon_hud(open)
		end
		if open then
			if version ~= Version then
				Version = version
				send_complete()
			end
			send_tallys()
		else
			Version = -1
			Tallys = ""
		end
		thread_yield()
	end
end

function rfg_ui_activity_complete_init()
	local ok, problem = pcall(rfg_ui_activity_complete_guerrilla_init)
	if not ok then
		log("Guerrilla's init: " .. tostring(problem))
	end
	if _GetAnyGlobalSilent("rfg_activity_complete") == nil or _GetAnyGlobalSilent("rfg_activity_complete_tallys") == nil then
		log("no rfg_activity_complete (an older guerrilla_hud.dll?): the screen stays empty")
		return
	end
	Thread = thread_new("rfg_ui_activity_complete_follow")
end

function rfg_ui_activity_complete_follow()
	follow()
end

function rfg_ui_activity_complete_cleanup()
	if Thread ~= -1 then
		thread_kill(Thread)
		Thread = -1
	end
	pcall(hide_armageddon_hud, false)
	pcall(rfg_ui_activity_complete_guerrilla_cleanup)
end
