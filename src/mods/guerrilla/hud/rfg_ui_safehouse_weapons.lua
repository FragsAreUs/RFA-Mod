-- Red Faction: Guerrilla's weapon cabinet in Armageddon's Guerrilla world: opened at a safehouse's weapon locker
-- and at every ammo crate. It lists the weapons found or bought (seven at a time around the one picked: its name,
-- the ammo it will have, its upgrades, its team's logo) and the player's four slots; the keys 1, 2 and 3 put the
-- weapon picked into a slot (the fourth is Mason's hammer); W / S, the arrows or the mouse wheel move down the
-- list (top to bottom). The game is paused while it's open.
--
-- Part of the guerrilla_hud mod. The document this drives, rfg_ui_safehouse_weapons, is Guerrilla's own, and so is
-- its script, both from your copy of Guerrilla: GuerrillaBuilder.exe packs our shims, Guerrilla's script (its init
-- and cleanup renamed rfg_ui_safehouse_weapons_guerrilla_init and _cleanup) and this script, in that order; run it
-- again after changing this file. Guerrilla's game filled data items for it (SafehouseWeaponSetInfo, ...SetIcons,
-- ...SlotInfo, ...Hints, ...Anim), which Armageddon hasn't: this script calls Guerrilla's callbacks itself with the
-- values in a table, from what the mod answers (rfg_cabinet, rfg_cabinet_screen, rfg_cabinet_info,
-- rfg_cabinet_icon, rfg_cabinet_slot). The mod has the cabinet's rules (guerrilla_hud.cpp, "Guerrilla's weapon
-- cabinet").

local KEYS = { "rfg_key_20", "rfg_key_43", "rfg_key_66", "rfg_key_89" }	-- Guerrilla's PC key pictures 1 2 3 4, for the slots
local BACK_KEY = "rfg_key_124"			-- and Esc
-- The key pictures by the slots' plates 1-4 (plate 1 left, 2 top, 3 right, 4 bottom: Mason's hammer): the
-- document's buttons X, Y, B and A sit by them (A, by the hammer, hidden).
local BUTTONS = { "weapon_x_button", "weapon_y_button", "weapon_b_button", "weapon_a_button" }
local SLIDE_FRAMES = 30					-- the list's slide: its icons wait at most this long for its end

local ARMAGEDDON_HUD = "non_subtitles"	-- Armageddon's HUD (ui_hud), all but its subtitles
local Version = -1
local Thread = -1
local Hud_hidden = false		-- Armageddon's HUD hidden by us while the cabinet is open
local Hud_was_visible = true
local Sliding = false
local Slide_frames = 0
local Failed = {}

local function log(text)
	if rfa_log ~= nil then
		rfa_log("[rfg_ui_safehouse_weapons] " .. text)
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

-- The seven icons around the one picked (SafehouseWeaponSetIcons): picture and bought for each.
local function set_icons()
	local values = { n = 14 }
	for i = 0, 6 do
		local picture, bought = rfg_cabinet_icon(i - 3)
		values[i * 2 + 1] = picture or "null"
		values[i * 2 + 2] = bought ~= false
	end
	call(sh_ws_set_list_icons, "sh_ws_set_list_icons", values)
end

local function update(scroll, shine)
	local title, back, counter, counter_title = rfg_cabinet_screen()
	-- The word over "n / count": the document names it by Guerrilla's tag, which Armageddon's strings haven't.
	local counter_title_handle = vint_object_find("list_counter_title")
	if counter_title_handle ~= 0 and counter_title ~= nil and counter_title ~= "" then
		vint_set_property(counter_title_handle, "text_tag", counter_title)
	end
	local name, ammo, upgrades, team, bought = rfg_cabinet_info()
	if name ~= nil then
		call(sh_ws_set_weapon_info, "sh_ws_set_weapon_info", { name, ammo, upgrades, team, counter, bought, title, n = 7 })
	end
	-- The four slots (SafehouseWeaponSlotInfo): rounds and picture each, and the plate to shine.
	local slots = { n = 9 }
	for slot = 1, 4 do
		local picture, rounds = rfg_cabinet_slot(slot)
		slots[slot * 2 - 1] = rounds or 0
		slots[slot * 2] = picture
	end
	slots[9] = shine
	if not call(sh_ws_weapon_info, "sh_ws_weapon_info", slots) and shine > 0 then
		slots[9] = 0
		call(sh_ws_weapon_info, "sh_ws_weapon_info (no shine)", slots)
	end
	call(sh_ws_set_hint, "sh_ws_set_hint", { BACK_KEY, back, n = 2 })
	-- A move slides the list (SafehouseWeaponAnim); its icons are set when the slide ends (rfg_wepon_cab_tween_end).
	-- The list runs top to bottom: Guerrilla slides it down (its "down") for the weapon above, the previous
	-- (hud_ui_weapon_cabinet_process_animation_data RM 0x84B870: down is its "previous" flag), up for the next.
	if scroll ~= 0 and call(sh_ws_set_animation, "sh_ws_set_animation", { true, scroll < 0, n = 2 }) then
		Sliding = true
		Slide_frames = 0
	else
		set_icons()
	end
end

-- Guerrilla's script calls this when the list's slide ends (its game set the new icons then).
function rfg_wepon_cab_tween_end()
	if Sliding then
		Sliding = false
		set_icons()
	end
end

-- Guerrilla hid its HUD while the cabinet was open (hud_ui_unload_items): the mod draws none of Guerrilla's then,
-- and this hides Armageddon's own (its parts our documents don't replace, such as its update lines), each frame
-- (its script may show it again), and puts it back as it was once the cabinet closes.
local function armageddon_hud()
	return vint_object_find(ARMAGEDDON_HUD, 0, vint_document_find("ui_hud"))
end

local function hide_armageddon_hud(hide)
	local hud = armageddon_hud()
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
		local open, version, scroll, shine = rfg_cabinet()
		if open or Hud_hidden then
			hide_armageddon_hud(open)
		end
		if open and version ~= Version then
			if Version == -1 then
				-- Just opened: the weapon's panel shows (a slide left unfinished last time hid it).
				Sliding = false
				local panel = vint_object_find("weapon_info_main")
				if panel ~= 0 then
					vint_set_property(panel, "visible", true)
				end
				scroll = 0
			end
			Version = version
			update(scroll, shine)
		elseif not open then
			Version = -1
		end
		if Sliding then
			Slide_frames = Slide_frames + 1
			if Slide_frames > SLIDE_FRAMES then
				-- The slide's end event didn't come: what Guerrilla's script does at its end (the weapon's
				-- panel shown again, its icons set).
				if not call(sh_ws_anim_end, "sh_ws_anim_end", nil) then
					rfg_wepon_cab_tween_end()
				end
			end
		end
		thread_yield()
	end
end

function rfg_ui_safehouse_weapons_init()
	local ok, problem = pcall(rfg_ui_safehouse_weapons_guerrilla_init)
	if not ok then
		log("Guerrilla's init: " .. tostring(problem))
	end
	-- The slots' keys: Guerrilla's PC pictures (its script asked for the controller's buttons).
	for i, name in ipairs(BUTTONS) do
		local handle = vint_object_find(name)
		if handle ~= 0 then
			vint_set_property(handle, "image", KEYS[i])
		end
	end
	if rfg_cabinet == nil then
		return		-- an older mod: no cabinet
	end
	Thread = thread_new("rfg_ui_safehouse_weapons_follow")
end

function rfg_ui_safehouse_weapons_follow()
	follow()
end

function rfg_ui_safehouse_weapons_cleanup()
	if Thread ~= -1 then
		thread_kill(Thread)
		Thread = -1
	end
	pcall(hide_armageddon_hud, false)
	pcall(rfg_ui_safehouse_weapons_guerrilla_cleanup)
end
