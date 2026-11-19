-- Red Faction: Guerrilla's district-liberated screen in Armageddon's Guerrilla world: the district's name and LIBERATED,
-- then (on Enter, Space or a click) the EDF's control of it counting down to nothing as its meter empties, then the
-- upgrades its liberation unlocked (up to three, one a press), then it closes. The game is paused while it's open.
--
-- Part of the guerrilla_hud mod. The document this drives, rfg_ui_district_liberated, is Guerrilla's own, and so is its
-- script, both from your copy of Guerrilla: GuerrillaBuilder.exe packs our shims, Guerrilla's script (its init and
-- cleanup renamed rfg_ui_district_liberated_guerrilla_init and _cleanup) and this script, in that order; run it again
-- after changing this file. Guerrilla's game filled its data items (DistrictLiberated, DistrictControl, DistrictUpgrade,
-- DistrictHints) and went on to the next step as each animation ended (its script calls rfg_dist_lib_tween_end);
-- Armageddon has none of it: this script calls Guerrilla's callbacks itself with the values in a table, from what the mod
-- answers (rfg_liberation), and tells the mod each animation's end (rfg_liberation_step_done). The mod has the screen's
-- rules (guerrilla_hud.cpp, "Guerrilla's screens the campaign raises").

local ARMAGEDDON_HUD = "non_subtitles"	-- Armageddon's HUD (ui_hud), all but its subtitles
local Version = -1
local Step = -1
local Thread = -1
local Hud_hidden = false
local Hud_was_visible = true
local Failed = {}

local function log(text)
	if rfa_log ~= nil then
		rfa_log("[rfg_ui_district_liberated] " .. text)
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

-- Guerrilla's game function its script calls as each step's animation ends (hud_ui_district_liberation_tween_done):
-- the mod takes the continue keys again.
function rfg_dist_lib_tween_end()
	if _GetAnyGlobalSilent("rfg_liberation_step_done") ~= nil then
		rfg_liberation_step_done()
	end
end

-- Guerrilla's _process_control_data: the control's number follows the meter's bar as it empties (meter_fill_scale_twn_1
-- on meter_fill): (the bar's height / (the tween's start - its end) + 1) x the most, from the most down to 0.
local function tally(most)
	local tween, fill = vint_object_find("meter_fill_scale_twn_1"), vint_object_find("meter_fill")
	if tween == 0 or fill == 0 then
		return 0
	end
	local start_x, start_y = vint_get_property(tween, "start_value")
	local end_x, end_y = vint_get_property(tween, "end_value")
	local fill_x, fill_y = vint_get_property(fill, "scale")
	if start_y == nil or end_y == nil or fill_y == nil or start_y == end_y then
		return 0
	end
	local number = (fill_y / (start_y - end_y) + 1) * most
	if number < 0 then
		number = 0
	elseif number > most then
		number = most
	end
	return number - number % 1
end

-- Guerrilla hid its HUD for this screen (hud_ui_unload_items): the mod draws none of Guerrilla's then, and this hides
-- Armageddon's own (as the weapon cabinet does), each frame, and puts it back as it was once the screen closes.
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

local function follow()
	while true do
		local open, version, step, busy, district, label, most, missions, hint_key, hint_text,
			upgrade, upgrade_title, upgrade_label, upgrade_info = rfg_liberation()
		if open or Hud_hidden then
			hide_armageddon_hud(open)
		end
		if open then
			if version ~= Version then
				Version = version
				if Step == -1 then
					-- Just opened: the header (district_liberated) and the continue hint (dl_set_hints).
					call(district_liberated, "district_liberated", { district, label, n = 2 })
					call(dl_set_hints, "dl_set_hints", { hint_key, hint_text, n = 2 })
					log("opened: " .. tostring(district))
				end
				Step = step
				if step == 2 then
					-- An unlock (DistrictUpgrade, as hud_ui_district_liberation_process_unlock_data filled it): the
					-- upgrade's icon, the level's name, MENU_UNLOCKED, Guerrilla's unlock text and the upgrade's
					-- blueprint. Guerrilla's script shows it only with a blueprint (Guerrilla left it out while the
					-- picture wasn't streamed in), so without one the upgrade's parts stay hidden.
					local pictures = {}
					if upgrade ~= nil and Rfg_upgrade_pictures ~= nil then
						pictures = Rfg_upgrade_pictures[upgrade] or {}
					end
					call(dl_upgrades, "dl_upgrades", { pictures[1], upgrade_title, upgrade_label, upgrade_info, pictures[2], n = 5 })
					log("unlock: " .. tostring(upgrade) .. " (" .. tostring(upgrade_title) .. "), blueprint " .. tostring(pictures[2]))
				end
			end
			if step == 1 then
				-- The control's tally (DistrictControl), each frame: shown, the number, the district's missions.
				call(dl_set_control, "dl_set_control", { true, tally(most), missions, n = 3 })
			end
		else
			Version = -1
			Step = -1
		end
		thread_yield()
	end
end

function rfg_ui_district_liberated_init()
	local ok, problem = pcall(rfg_ui_district_liberated_guerrilla_init)
	if not ok then
		log("Guerrilla's init: " .. tostring(problem))
	end
	if _GetAnyGlobalSilent("rfg_liberation") == nil then
		log("no rfg_liberation (an older guerrilla_hud.dll?): the screen stays empty")
		return
	end
	Thread = thread_new("rfg_ui_district_liberated_follow")
end

function rfg_ui_district_liberated_follow()
	follow()
end

function rfg_ui_district_liberated_cleanup()
	if Thread ~= -1 then
		thread_kill(Thread)
		Thread = -1
	end
	pcall(hide_armageddon_hud, false)
	pcall(rfg_ui_district_liberated_guerrilla_cleanup)
end
