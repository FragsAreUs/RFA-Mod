-- Red Faction: Guerrilla's health HUD in Armageddon: the health meter, and the blood that creeps in
-- from the screen's corners as health drops, driven by Armageddon's own health data (health_hud).
--
-- Part of the guerrilla_hud mod. The document this drives, rfg_ui_hud_health, is Guerrilla's own,
-- made from your copy of Guerrilla by GuerrillaHud.exe (its pictures renamed rfg_...), which also
-- packs this script with it; run that again after changing this file. The mod loads the document
-- in the worlds named by [GuerrillaHud] Worlds= and unloads it everywhere else.
--
-- While it is loaded, Armageddon's own health ring and red screen edges are faded out, and so is
-- its compass bar with its objective arrows: the top middle of the screen is where Guerrilla's
-- health meter goes (Guerrilla finds the way with its minimap, which comes later). They are faded
-- back in when it goes. Armageddon's HUD script only ever shows or hides those, never fades them
-- (its arrow animation fades the arrow inside the group), so it doesn't undo this.

-- The parts of Armageddon's HUD (the document ui_hud) that this one replaces.
local ARMAGEDDON_PARTS = { "health_grp", "low_health", "compass_group", "GPS_arrow_group" }

-- The meter's length and height at full health, in Guerrilla's layout.
local METER_LENGTH = 358
local METER_HEIGHT = 6

-- How long the meter stays up once health is full again (on foot) or after a change (for a
-- vehicle), and how long it takes to fade away, in seconds.
local FADE_AFTER_ON_FOOT = 6
local FADE_AFTER_IN_VEHICLE = 0.48
local FADE_TIME = 0.33

-- The meter flashes at or below this much health.
local FLASH_ON_FOOT = 0.6
local FLASH_IN_VEHICLE = 0.3

-- The blood's colour, and the bar's on foot (at least half red, redder as health drops) and for a
-- vehicle, as Guerrilla's script gives them: 0-1 (see tint below).
local BLOOD_R, BLOOD_G, BLOOD_B = 0.22, 0.03, 0.03
local BAR_LEAST_RED = 0.5
local VEHICLE_R, VEHICLE_G, VEHICLE_B = 1.0, 0.74, 0.26

local Armageddon_hud		-- Armageddon's HUD document, as it was when its parts were faded out
local Had_health = false	-- Armageddon's first update after a load is often a false 0
local Shown_health, Shown_target, Shown_visible	-- what is on screen, so only changes are acted on

-- For finding out what goes wrong, from rfa_mods.ini (through the loader's rfa_get_setting):
-- [GuerrillaHud] Log=1 writes what this script finds and every health update it gets to
-- logs\rfa_mods.log (at most LOG_LINES lines a load); Test=1 shows the blood and the meter all the
-- time, whatever the health, to see whether they are drawn at all.
local LOG_LINES = 100
local Log_on = false
local Test_on = false
local Logged = 0
local Logged_health, Logged_target, Logged_visible	-- the last health update written to the log

local function setting(key)
	return rfa_get_setting ~= nil and rfa_get_setting("GuerrillaHud", key, 0) ~= 0
end

local function log(text)
	if Log_on and rfa_log ~= nil and Logged < LOG_LINES then
		Logged = Logged + 1
		rfa_log("[rfg_ui_hud_health] " .. text)
	end
end

local function find(name)
	return vint_object_find(name)
end

-- Sets a colour given 0-1, as Guerrilla's scripts give them. Armageddon's takes a script's tint as
-- 0-255 (its own scripts call set_color(192, 36, 18) for red), so 1, 0, 0 would come out black.
-- The colours stored in the document itself are 0-1 either way.
local function tint(handle, r, g, b)
	vint_set_property(handle, "tint", r * 255, g * 255, b * 255)
end

-- An element's or document's handle for the log, and a property's value as the game gives it.
local function described(name, handle)
	return name .. "=" .. tostring(handle)
end

local function property(handle, name)
	local ok, a, b, c = pcall(vint_get_property, handle, name)
	if not ok then
		return "error: " .. tostring(a)
	end
	return tostring(a) .. (b ~= nil and (" " .. tostring(b)) or "") .. (c ~= nil and (" " .. tostring(c)) or "")
end

-- Fades Armageddon's parts to this alpha; returns Armageddon's HUD document.
local function fade_armageddon_parts(alpha)
	local doc = vint_document_find("ui_hud")
	for i, name in ipairs(ARMAGEDDON_PARTS) do
		local part = vint_object_find(name, 0, doc)
		if part ~= 0 then
			vint_set_property(part, "alpha", alpha)
		end
	end
	return doc
end

-- Each picture as the game holds it: its picture's size (unscaled_size: 0 when the name found no
-- picture, as Armageddon's vint_lib reads sizes) and where it lands on screen (vint_get_bbox);
-- Armageddon's own (which draw) beside ours, to compare.
local function log_pictures(when)
	local ui_hud = vint_document_find("ui_hud")
	local pictures = { { "top_left_gradient" }, { "top_right_gradient" }, { "the_health_meter" }, { "the_health_meter_bg" },
		{ "health_dags1" }, { "health_bg", ui_hud }, { "compass_bg", ui_hud }, { "new_bitmap1", ui_hud } }
	for i, picture in ipairs(pictures) do
		local handle = vint_object_find(picture[1], 0, picture[2])
		local ok, x1, y1, x2, y2 = pcall(vint_get_bbox, handle)
		log("  " .. when .. ", picture of " .. (picture[2] and "Armageddon's " or "") .. picture[1] .. ": unscaled_size " ..
			property(handle, "unscaled_size") .. ", bbox " .. (ok and (tostring(x1) .. " " .. tostring(y1) .. " " ..
			tostring(x2) .. " " .. tostring(y2)) or ("error: " .. tostring(x1))))
	end
end

-- The test display, all the time: the blood bright red at full strength, and the meter red at
-- half length.
local function show_for_test()
	local blood = find("health_bar_main")
	vint_set_property(blood, "visible", true)
	vint_set_property(blood, "alpha", 1)
	tint(blood, 1, 0, 0)
	local meter = find("health_meter_main")
	vint_set_property(meter, "visible", true)
	vint_set_property(meter, "alpha", 1)
	tint(find("the_health_meter"), 1, 0, 0)
	vint_set_property(find("the_health_meter"), "visible", true)
	tint(find("the_health_meter2"), 1, 0, 0)
	vint_set_property(find("the_health_meter2"), "visible", true)
	vint_set_property(find("the_health_meter_clip"), "clip_size", 0.5 * METER_LENGTH, METER_HEIGHT)
	vint_set_property(find("the_health_meter_bg"), "scale", METER_LENGTH, METER_HEIGHT)
end

function rfg_ui_hud_health_init()
	Log_on = setting("Log")
	Test_on = setting("Test")
	Logged = 0
	log("init" .. (Test_on and " (Test=1: shown all the time)" or "") .. ": " ..
		described("this document", vint_document_find("rfg_ui_hud_health")) .. ", " ..
		described("ui_hud", vint_document_find("ui_hud")))
	local names = { "scale_group", "health_bar_main", "top_left_gradient", "health_meter_main", "the_health_meter_clip",
		"the_health_meter", "health_meter_anim", "health_meter_flash_anim", "health_meter_main_alpha_twn_1" }
	for i, name in ipairs(names) do
		local handle = find(name)
		log("  " .. described(name, handle) .. ", visible " .. property(handle, "visible") .. ", alpha " ..
			property(handle, "alpha") .. ", anchor " .. property(handle, "anchor"))
	end
	log_pictures("at load")

	vint_set_property(find("health_bar_main"), "visible", false)
	vint_set_property(find("health_meter_main"), "visible", false)
	vint_set_property(find("armor_meter_main"), "visible", false)	-- Armageddon has no armour to show
	vint_set_property(find("radiation_icon"), "visible", false)	-- nor radiation
	vint_set_child_tween_state(find("radiation_anim"), TWEEN_STATE_DISABLED)
	vint_set_child_tween_state(find("health_meter_flash_anim"), TWEEN_STATE_DISABLED)

	Armageddon_hud = fade_armageddon_parts(0)
	for i, name in ipairs(ARMAGEDDON_PARTS) do
		log("  Armageddon's " .. described(name, vint_object_find(name, 0, Armageddon_hud)) .. " faded out")
	end
	vint_dataitem_add_subscription("health_hud", "update", "rfg_ui_hud_health_update")
	vint_dataitem_add_subscription("hud_hide", "update", "rfg_ui_hud_health_hide")
	if Test_on then
		show_for_test()
		log_pictures("test display")
	end
	log("init done")
end

function rfg_ui_hud_health_cleanup()
	fade_armageddon_parts(1)
end

-- Cutscenes and the like hide the HUD (all but the subtitles): hide this with it.
function rfg_ui_hud_health_hide(di_h, event_type)
	local hide_all = vint_dataitem_get(di_h)
	log("hud_hide: hide all " .. tostring(hide_all))
	vint_set_property(find("scale_group"), "visible", not hide_all)
end

-- health_hud: visible, target ("player", or the vehicle or exo-suit being driven), health 0-1, ...
function rfg_ui_hud_health_update(di_h, event_type)
	local visible, target_name, health = vint_dataitem_get(di_h)
	if health ~= Logged_health or target_name ~= Logged_target or visible ~= Logged_visible then
		log("health_hud: visible " .. tostring(visible) .. ", target " .. tostring(target_name) .. ", health " ..
			tostring(health))
		Logged_health, Logged_target, Logged_visible = health, target_name, visible
	end
	if Test_on then
		return
	end
	if health == 0 and not Had_health then
		return
	end
	Had_health = true
	if health < 0 or health > 1 then
		health = 1	-- below 0: can't be hurt
	end

	-- Armageddon's HUD can be loaded again while this one stays: fade the new one's parts too.
	if vint_document_find("ui_hud") ~= Armageddon_hud then
		Armageddon_hud = fade_armageddon_parts(0)
	end

	if health == Shown_health and target_name == Shown_target and visible == Shown_visible then
		return
	end
	Shown_health, Shown_target, Shown_visible = health, target_name, visible
	local on_foot = target_name == "player"

	-- The blood: the player's own health only, stronger as it drops, gone when full.
	local blood = find("health_bar_main")
	vint_set_property(blood, "visible", visible and on_foot and health < 1)
	vint_set_property(blood, "alpha", 1 - health)
	tint(blood, BLOOD_R, BLOOD_G, BLOOD_B)

	local meter = find("health_meter_main")
	local flash = find("health_meter_flash_anim")
	local fade = find("health_meter_anim")
	if not visible or (on_foot and health <= 0) then
		vint_set_property(meter, "visible", false)
		vint_set_child_tween_state(flash, TWEEN_STATE_DISABLED)
		vint_set_child_tween_state(fade, TWEEN_STATE_DISABLED)
		return
	end
	vint_set_property(meter, "visible", true)
	vint_set_property(meter, "alpha", 1)

	-- The bar: red on foot, orange for a vehicle.
	local bar = find("the_health_meter")
	local bar_edge = find("the_health_meter2")
	local vehicle_bar = find("the_health_meter_fill")
	if on_foot then
		local red = 1 - health
		if red < BAR_LEAST_RED then
			red = BAR_LEAST_RED
		end
		tint(bar, red, 0, 0)
		tint(bar_edge, red, 0, 0)
		vint_set_property(bar, "visible", true)
		vint_set_property(bar_edge, "visible", true)
		vint_set_property(vehicle_bar, "visible", false)
	else
		vint_set_property(bar, "visible", false)
		vint_set_property(bar_edge, "visible", false)
		tint(vehicle_bar, VEHICLE_R, VEHICLE_G, VEHICLE_B)
		vint_set_property(vehicle_bar, "alpha", 1)
		vint_set_property(vehicle_bar, "visible", true)
	end
	vint_set_property(find("health_dag_group"), "visible", false)
	vint_set_property(find("the_health_meter_clip"), "clip_size", health * METER_LENGTH, METER_HEIGHT)
	vint_set_property(find("the_health_meter_bg"), "scale", METER_LENGTH, METER_HEIGHT)

	-- Flashing when low.
	local flash_at = FLASH_ON_FOOT
	if not on_foot then
		flash_at = FLASH_IN_VEHICLE
	end
	if health < 1 and health <= flash_at then
		vint_set_child_tween_state(flash, TWEEN_STATE_WAITING)
	else
		vint_set_child_tween_state(flash, TWEEN_STATE_DISABLED)
	end

	-- Fading away: on foot once health is full again; for a vehicle a moment after each change.
	if health >= 1 or not on_foot then
		local fade_tween = find("health_meter_main_alpha_twn_1")
		if on_foot then
			vint_set_property(fade_tween, "start_time", FADE_AFTER_ON_FOOT)
		else
			vint_set_property(fade_tween, "start_time", FADE_AFTER_IN_VEHICLE)
		end
		vint_set_property(fade_tween, "duration", FADE_TIME)
		vint_set_property(fade, "start_time", vint_get_time_index())
		vint_set_child_tween_state(fade, TWEEN_STATE_RUNNING)
	else
		vint_set_child_tween_state(fade, TWEEN_STATE_DISABLED)
	end
end
