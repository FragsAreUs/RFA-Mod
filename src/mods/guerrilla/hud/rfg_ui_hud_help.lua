-- Red Faction: Guerrilla's help box in Armageddon's Guerrilla world: its tool tips, one at a time (a title, a text, an
-- icon, at one of five places on the screen, an arrow, and for a tip that paused the game "PRESS <key> TO CONTINUE").
--
-- Part of the guerrilla_hud mod. The document this drives, rfg_ui_hud_help, is Guerrilla's own, and so is its script,
-- both from your copy of Guerrilla: GuerrillaBuilder.exe packs our shims, Guerrilla's script (its init and cleanup
-- renamed rfg_ui_hud_help_guerrilla_init and _cleanup, its generic globals rfg_ui_hud_help_<name>) and this script, in
-- that order; run it again after changing this file. Guerrilla's game filled its data item HelpUpdate (ten values) and
-- HelpLanguageInfo, which Armageddon hasn't: this script calls Guerrilla's callbacks itself, with what the mod answers
-- (rfg_tool_tip: the tips' rules are the mod's, tool_tips.cpp).
--
-- Guerrilla's box comes in and goes with its tweens, which stand still while the game is paused (a tip that pauses
-- it, or one shown in a safehouse's screen): then it's shown at once, whole, and goes when the mod says.

local MAIN = "help_message_main"
local MAIN_ANIM = "help_main_anim"
local Version = -1
local Thread = -1
local Failed = {}

local function log(text)
	if rfa_log ~= nil then
		rfa_log("[rfg_ui_hud_help] " .. text)
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

local function update()
	local version, visible, title, text, icon, animation, anchor, arrow, duration, hint, is_paused, game_paused = rfg_tool_tip()
	Version = version
	if not visible then
		call(rfg_ui_hud_help_set_help_data, "set_help_data", { false, n = 1 })
		return
	end
	call(rfg_ui_hud_help_set_help_data, "set_help_data",
		{ true, title, text, icon, animation, anchor, arrow, duration, hint, is_paused, n = 10 })
	if game_paused then
		-- Its tweens stand still: the box whole, at once (the mod takes it away when its time is up or its key is let go).
		local main = vint_object_find(MAIN)
		local anim = vint_object_find(MAIN_ANIM)
		if anim ~= 0 then
			vint_set_child_tween_state(anim, TWEEN_STATE_DISABLED)
		end
		if main ~= 0 then
			vint_set_property(main, "alpha", 1)
			vint_set_property(main, "scale", 1, 1)
		end
	end
end

local function follow()
	while true do
		local version = rfg_tool_tip()
		if version ~= Version then
			local ok, problem = pcall(update)
			if not ok and not Failed.update then
				Failed.update = true
				log("update: " .. tostring(problem))
			end
		end
		thread_yield()
	end
end

function rfg_ui_hud_help_init()
	local ok, problem = pcall(rfg_ui_hud_help_guerrilla_init)
	if not ok then
		log("Guerrilla's init: " .. tostring(problem))
	end
	-- Guerrilla's HelpLanguageInfo: not Arabic (its texts left to right).
	call(rfg_ui_hud_help_set_help_language, "set_help_language", { 0, n = 1 })
	-- An older mod has no tool tips (reading a global that isn't there is an error: Armageddon's own silent look).
	if _GetAnyGlobalSilent("rfg_tool_tip") == nil then
		return
	end
	Thread = thread_new("rfg_ui_hud_help_follow")
end

function rfg_ui_hud_help_follow()
	follow()
end

function rfg_ui_hud_help_cleanup()
	if Thread ~= -1 then
		thread_kill(Thread)
		Thread = -1
	end
	pcall(rfg_ui_hud_help_guerrilla_cleanup)
end
