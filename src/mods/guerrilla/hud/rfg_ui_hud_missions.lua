-- Red Faction: Guerrilla's corner panel for its missions and activities in Armageddon's Guerrilla world: while an
-- activity runs, its icon and name, what to do, and its own parts (demolitions master: the time left, or so far, and the
-- building's structure: UNSTABLE, CRITICAL). Nothing shows while none runs.
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
local LUA_ACTIVITY_DEMOLITIONS_TYPE = 6	-- Guerrilla's LUA_ACTIVITY_DEMOLITIONS (its script's)
local LUA_INVALID_TYPE = 0				-- its LUA_MISSION_ACTIVITY_INVALID
local Thread = -1
local Shown = ""							-- what was sent last ("" none)
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

-- MissionInfo: type, title, objective, activity info, txt1-3 (demolitions master: the structure's indicator, the time,
-- the structure's title and label).
local function follow()
	while true do
		local screen, version, mode, title, subtitle, body, objective, timer, result, failure, limit, par, best, elapsed,
			status = rfg_activity()
		local values = nil
		local key = ""
		if screen == SCREEN_RUNNING and mode == MODE_DEMOLITIONS_MASTER then
			local label = text("ACT_DM_BS_UNSTABLE")
			if status == 2 then
				label = text("ACT_DM_BS_CRITICAL")
			end
			values = { LUA_ACTIVITY_DEMOLITIONS_TYPE, text("MISS_DEMO_MASTER"), objective, status or 0, timer,
				text("ACT_DM_BUILDING_STRUCTURE"), label, n = 7 }
			key = tostring(objective) .. "|" .. tostring(status) .. "|" .. tostring(timer)
		end
		if key ~= Shown then
			if values == nil then
				values = { LUA_INVALID_TYPE, "", "", 0, "", "", "", n = 7 }
				log("the corner panel hidden")
			elseif Shown == "" then
				log("the corner panel: demolitions master, " .. tostring(objective))
			end
			call(mission_info, "mission_info", values)
			Shown = key
		end
		thread_yield()
	end
end

function rfg_ui_hud_missions_init()
	local ok, problem = pcall(rfg_ui_hud_missions_guerrilla_init)
	if not ok then
		log("Guerrilla's init: " .. tostring(problem))
	end
	-- Nothing shows until an activity runs.
	call(mission_info, "mission_info", { LUA_INVALID_TYPE, "", "", 0, "", "", "", n = 7 })
	call(timer_update, "timer_update", { false, "", n = 2 })
	Shown = ""
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
