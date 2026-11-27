-- Red Faction: Guerrilla's corner panel for its missions and activities in Armageddon's Guerrilla world: while an
-- activity runs, its icon and name, what to do, and its own parts (demolitions master: the time left, or so far, and the
-- building's structure: UNSTABLE, CRITICAL). Nothing shows while none runs. Below it, the diversions: the free activities
-- that run (killing spree: KILLING SPREE STARTED, then the kills; ENDED once it's over).
--
-- Part of the guerrilla_hud mod. The document this drives, rfg_ui_hud_missions, is Guerrilla's own, and so is its
-- script, both from your copy of Guerrilla: GuerrillaBuilder.exe packs our shims, Guerrilla's script (its init and
-- cleanup renamed rfg_ui_hud_missions_guerrilla_init and _cleanup) and this script, in that order; run it again after
-- changing this file. Guerrilla's game filled its data item MissionInfo each frame (hud_ui_missions_get_activity_
-- demolitions_master_info RM 0x896510 for demolitions master: MISS_DEMO_MASTER, the corner subtitle, the structure's
-- indicator, the time "%02d:%02d", ACT_DM_BUILDING_STRUCTURE, ACT_DM_BS_CRITICAL at 2 else ACT_DM_BS_UNSTABLE);
-- Armageddon has none of it: this script calls Guerrilla's callback itself with the values in a table, from what the mod
-- answers (rfg_activity: the guerrilla_campaign mod's activity), whenever they change.

local SCREEN_RUNNING = 2					-- the mod's (the campaign's) screen while an activity runs
local MODE_DEMOLITIONS_MASTER = 11		-- the campaign's activity modes
local MODE_DELIVERY = 4					-- Transporter (Guerrilla Express)
local MODE_AREA_DEFENSE = 16				-- area defense ("HEAVY METAL")
local MODE_CONVOY = 13					-- a convoy (capture or destroy)
local LUA_ACTIVITY_CONVOY_TYPE = 3		-- its LUA_ACTIVITY_CONVOY
local LUA_ACTIVITY_AREA_DEFENSE_TYPE = 8	-- its LUA_ACTIVITY_AREA_DEFENSE
local LUA_ACTIVITY_DEMOLITIONS_TYPE = 6	-- Guerrilla's LUA_ACTIVITY_DEMOLITIONS (its script's)
local LUA_ACTIVITY_EXPRESS_TYPE = 4		-- its LUA_ACTIVITY_GUERILLA_EXPRESS
local LUA_INVALID_TYPE = 0				-- its LUA_MISSION_ACTIVITY_INVALID
-- The document's own texts named by Guerrilla's string tags (set in the document, not by its script), which Armageddon's
-- string tables haven't: Guerrilla's text for each, from the mod, set at the start, so the diversion items its script
-- clones from the first carry it. The convoy labels are Transporter's panel's too (its handle_express shows the convoy
-- group); the riding shotgun ones wait for that kind.
local DOCUMENT_TAGS = { diversion_millions = "HUD_MILLION", convoy_target_title_1 = "HM_TARGET_TITLE",
	convoy_target_title_2 = "HM_TARGET_DISTANCE", convoy_target_title_3 = "HM_GOTO_TARGET", convoy_strength_title_1 = "HM_CONVOY_TITLE",
	convoy_strength_title_2 = "HM_CONVOY_STRENGTH", shotgun_title = "HM_SHOTGUN_DESTRUCTION", shotgun_millions = "HM_SHOTGUN_MILLION" }
local Thread = -1
local Shown = ""							-- what was sent last ("" none)
local Timer_shown = ""						-- HudTimerInfo: the timer's text last sent ("" hidden)
local Stamp_shown = false					-- MissionStatus: a stamp is up
local Failed = {}

local function log(text)
	if rfa_log ~= nil then
		rfa_log("[rfg_ui_hud_missions] " .. text)
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

local function text(tag)
	if _GetAnyGlobalSilent("rfg_guerrilla_text") == nil then
		return ""
	end
	return rfg_guerrilla_text(tag) or ""
end

-- DiversionInfo: the free activities that run or just ended (killing spree, mayhem, car bomb), as the campaign works
-- them out (rfg_diversions, rfg_diversion: which_one, diversion_type, fade_this_in, flash_this, header_text,
-- status_text, value_text). Each item is a table handed to Guerrilla's diversion_info ("insert", then "update"
-- whenever it changes) and to its diversion_remove once it goes, as Guerrilla's data group told its subscribers: the
-- removals first, then the new and changed items, as its hud_ui_missions_determine_diversion_items and
-- process_diversion_info did each frame.
local Diversions = {}	-- by the campaign's item id: { values = the table handed over, key = its values joined }

local function tell(callback, name, values, event)
	local ok, problem = pcall(callback, values, event)
	if not ok and not Failed[name] then
		Failed[name] = true
		log(name .. ": " .. tostring(problem))
	end
end

local function follow_diversions()
	if _GetAnyGlobalSilent("rfg_diversions") == nil then
		return
	end
	local items = {}
	for i = 1, rfg_diversions() do
		local id, which_one, kind, fade_in, flash, header, status, value = rfg_diversion(i)
		if id ~= nil then
			items[#items + 1] = { id = id, values = { which_one, kind, fade_in, flash, header, status, value, n = 7 } }
		end
	end
	local listed = {}
	for _, item in ipairs(items) do
		listed[item.id] = true
	end
	for id, shown in pairs(Diversions) do
		if not listed[id] then
			Diversions[id] = nil
			tell(diversion_remove, "diversion_remove", shown.values, "remove")
			log("a diversion gone")
		end
	end
	for _, item in ipairs(items) do
		local v = item.values
		local key = tostring(v[1]) .. "|" .. tostring(v[2]) .. "|" .. tostring(v[3]) .. "|" .. tostring(v[4]) .. "|" ..
			tostring(v[5]) .. "|" .. tostring(v[6]) .. "|" .. tostring(v[7])
		local shown = Diversions[item.id]
		if shown == nil then
			Diversions[item.id] = { values = v, key = key }
			tell(diversion_info, "diversion_info", v, "insert")
			log("a diversion: " .. tostring(v[5]) .. " (type " .. tostring(v[2]) .. ")")
		elseif shown.key ~= key then
			-- the same table (Guerrilla's script keeps mayhem's to know it at its removal), its values new
			for i = 1, 7 do
				shown.values[i] = v[i]
			end
			shown.key = key
			tell(diversion_info, "diversion_info", shown.values, "update")
		end
	end
end

-- The COMPLETE stamp (hud_ui_missions_process_activity_status RM 0x895C40: on VICTORY, area defense's status 1 with
-- MISS_COMPLETE_MALE upper-cased and the 6 s it shows): the mod's result says one is due (rfg_activity hands it as
-- Guerrilla's text, and the elapsed time as "mm:ss", so neither is used here); the text is upper-cased by the mod
-- (rfg_guerrilla_text's second argument: Armageddon's Lua has no string.upper).
local STAMP_TAG = "MISS_COMPLETE_MALE"
local STAMP_SECONDS = 6

local function follow_stamp(screen, mode, success, result)
	if screen == SCREEN_RUNNING and mode == MODE_AREA_DEFENSE and success and result ~= nil and result ~= "" then
		if not Stamp_shown then
			Stamp_shown = true		-- once, even if Guerrilla's callback fails
			local shown = "COMPLETE"
			if _GetAnyGlobalSilent("rfg_guerrilla_text") ~= nil then
				shown = rfg_guerrilla_text(STAMP_TAG, true) or shown
			end
			call(handle_activity_status, "handle_activity_status", { 1, shown, STAMP_SECONDS, n = 3 })
		end
	elseif Stamp_shown then
		call(handle_activity_status, "handle_activity_status", { -1, "", 0, n = 3 })
		Stamp_shown = false
	end
end

-- MissionInfo: type, title, objective, activity info, txt1-3 (demolitions master: the structure's indicator, the time,
-- the structure's title and label; Transporter, hud_ui_missions_get_activity_delivery_info RM 0x896910: the time left
-- and the distance to the end, its script's handle_express; a convoy: the vehicles left, the distance and the target's
-- health).
local function follow()
	while true do
		follow_diversions()
		local screen, version, mode, title, subtitle, body, objective, timer, result, failure, limit, par, best, elapsed,
			status, success, pro_time, out_of_range, salvage, distance, count, strength = rfg_activity()
		local values = nil
		local key = ""
		if screen == SCREEN_RUNNING and mode == MODE_DEMOLITIONS_MASTER then
			local label = text("ACT_DM_BS_UNSTABLE")
			if status == 2 then
				label = text("ACT_DM_BS_CRITICAL")
			end
			values = { LUA_ACTIVITY_DEMOLITIONS_TYPE, text("MISS_DEMO_MASTER"), objective, status or 0, timer,
				text("ACT_DM_BUILDING_STRUCTURE"), label, n = 7 }
			key = "dm|" .. tostring(objective) .. "|" .. tostring(status) .. "|" .. tostring(timer)
		elseif screen == SCREEN_RUNNING and mode == MODE_DELIVERY then
			values = { LUA_ACTIVITY_EXPRESS_TYPE, title, objective, 0, timer, distance or "", "", n = 7 }
			key = "td|" .. tostring(objective) .. "|" .. tostring(timer) .. "|" .. tostring(distance)
		elseif screen == SCREEN_RUNNING and mode == MODE_AREA_DEFENSE then
			-- hud_ui_missions_get_activity_area_defense_info RM 0x896B50: the title, the subtitle while running, the
			-- vehicles (0) or soldiers (1) icon, "kills / required" (its script's handle_area_defense)
			values = { LUA_ACTIVITY_AREA_DEFENSE_TYPE, title, objective or "", status or 0, distance or "", "", "", n = 7 }
			key = "ad|" .. tostring(objective) .. "|" .. tostring(status) .. "|" .. tostring(distance)
		elseif screen == SCREEN_RUNNING and mode == MODE_CONVOY then
			-- hud_ui_missions_get_activity_convoy_info RM 0x896790: the title, what to do, 0 capture or 1 destroy, the
			-- vehicles left of all, the distance and the target's health (its script's handle_convoy)
			values = { LUA_ACTIVITY_CONVOY_TYPE, title, objective or "", status or 0, count or "", distance or "", strength or "", n = 7 }
			key = "cv|" .. tostring(objective) .. "|" .. tostring(status) .. "|" .. tostring(count) .. "|" .. tostring(distance) .. "|" ..
				tostring(strength)
		end
		-- HudTimerInfo (hud_ui_missions_process_hud_timer RM 0x896DB0): area defense's 10 s out of its ring, "m:ss"
		local shown_timer = ""
		if screen == SCREEN_RUNNING and mode == MODE_AREA_DEFENSE and timer ~= nil then
			shown_timer = timer
		end
		if shown_timer ~= Timer_shown then
			call(timer_update, "timer_update", { shown_timer ~= "", shown_timer, n = 2 })
			Timer_shown = shown_timer
		end
		if key ~= Shown then
			if values == nil then
				values = { LUA_INVALID_TYPE, "", "", 0, "", "", "", n = 7 }
				log("the corner panel hidden")
			elseif Shown == "" then
				log("the corner panel: " .. tostring(values[2]) .. ", " .. tostring(objective))
			end
			call(mission_info, "mission_info", values)
			Shown = key
		end
		-- MissionStatus (hud_ui_missions_process_activity_status RM 0x895C40): a won area defense's COMPLETE stamp, orange,
		-- for 6 s (the seconds left as the mod gives them), then gone with the panel (status -1, time left 0). After the
		-- panel's update, and guarded: its error stopped this loop once (2026-11-20, string.upper isn't Armageddon's), and
		-- the panel kept its last count.
		local stamp_ok, stamp_problem = pcall(follow_stamp, screen, mode, success, result)
		if not stamp_ok and not Failed["stamp"] then
			Failed["stamp"] = true
			log("the stamp: " .. tostring(stamp_problem))
		end
		thread_yield()
	end
end

function rfg_ui_hud_missions_init()
	local ok, problem = pcall(rfg_ui_hud_missions_guerrilla_init)
	if not ok then
		log("Guerrilla's init: " .. tostring(problem))
	end
	for name, tag in pairs(DOCUMENT_TAGS) do
		local handle = vint_object_find(name)
		local shown = text(tag)
		if handle ~= 0 and shown ~= "" then
			vint_set_property(handle, "text_tag", shown)
		end
	end
	-- Nothing shows until an activity runs.
	call(mission_info, "mission_info", { LUA_INVALID_TYPE, "", "", 0, "", "", "", n = 7 })
	call(timer_update, "timer_update", { false, "", n = 2 })
	Shown = ""
	Diversions = {}
	if _GetAnyGlobalSilent("rfg_activity") == nil then
		log("no rfg_activity (an older guerrilla_hud.dll?): the corner panel stays hidden")
		return
	end
	Thread = thread_new("rfg_ui_hud_missions_follow")
end

function rfg_ui_hud_missions_follow()
	follow()
end

function rfg_ui_hud_missions_cleanup()
	if Thread ~= -1 then
		thread_kill(Thread)
		Thread = -1
	end
	pcall(rfg_ui_hud_missions_guerrilla_cleanup)
end
