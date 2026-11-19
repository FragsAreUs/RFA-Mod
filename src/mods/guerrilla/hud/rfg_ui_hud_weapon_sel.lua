-- Red Faction: Guerrilla's weapon select in Armageddon: Guerrilla's own document and script, fed
-- from Armageddon's weapon slots.
--
-- Part of the guerrilla_hud mod. GuerrillaBuilder.exe makes the document rfg_ui_hud_weapon_sel from
-- your copy of Guerrilla (its pictures renamed rfg_...), and its script from three parts: our
-- rfg_guerrilla_shims.lua, then Guerrilla's own script for it (its init and cleanup renamed
-- rfg_ui_hud_weapon_sel_guerrilla_init and _cleanup), then this file. Run it again after changing
-- this file.
--
-- Guerrilla's weapon select is four plates in a cross at the bottom right (the first on the left,
-- then clockwise), each with its weapon's picture and ammo, the chosen one lit with its name
-- beside it. Its script reads two data items Guerrilla's game filled, WeaponSlotInfo (the four
-- weapons) and WeaponShowInfo (open or not, the chosen slot, the one before). Armageddon has the
-- same four slots behind its own selector (which sat at the bottom left, over the minimap): its
-- item weapon_radial_hud says when it is open and which slot is chosen, and its data responder
-- wpn_radial_populate gives each slot's picture and ammo. So this script makes Guerrilla's two
-- items and fills them from those; Armageddon's slot 1 (key 1) is Guerrilla's first plate, and
-- so on. The pictures are Armageddon's own weapons' (the weapons are Armageddon's). Armageddon's
-- selector is faded out while this is loaded, and back in when it goes.

local SLOT_INFO = "WeaponSlotInfo"
local SLOT_INFO_FIELDS = "ississississ"	-- per slot: ammo count, picture, name
local SHOW_INFO = "WeaponShowInfo"
local SHOW_INFO_FIELDS = "biib"			-- shown, chosen slot, slot before (from 0; -1 none), controller

local SLOTS = 4
local NO_PICTURE = "null"				-- Guerrilla's document's own name for none

-- Armageddon closes its selector as soon as a key has picked a weapon (about 0.7 s after it
-- opens), before Guerrilla's has finished coming up: its plates take about 0.3 s, then the
-- weapon's name fades in half a second later. So Guerrilla's stays up this long after Armageddon's
-- closes, for the name to be read. (Our choice: how long Guerrilla's own game kept it up isn't
-- known; its exe can't be read for it.)
local HOLD_AFTER_CLOSE = 1.5

-- Armageddon's values (ui_hud.lua, vdo_hud_wpn_selector.lua, rfg_ui_globals.lua). Its slots count
-- from 1 (0 or -1: none); its ammo counts are Guerrilla's: -1 unlimited, -2 none to show.
local WEAPON_RADIAL_STATE_HIDDEN = 0
local AMMO_COUNT_NONE = -2

local Slots = {}						-- by Armageddon's slot: icon_name, ammo_count
local Names = {}						-- by slot: the name Armageddon gave when it was chosen
local Empty = { true, true, true, true }	-- by slot: no weapon in it
local Icons = {}						-- by slot: Guerrilla's picture element for it
local Open = false						-- Armageddon's selector
local Shown = false						-- Guerrilla's
local Chosen = 0						-- chosen in the open selector
local Equipped = 0						-- chosen when the selector last closed
local Before = 0						-- equipped when it opened
local Fade_thread = -1
local Close_thread = -1

-- [GuerrillaHud] Log=1: what Armageddon gives, on the first updates (through the loader's rfa_log).
local LOG_LINES = 40
local Log_on = false
local Logged = 0

local function log(text)
	if Log_on and rfa_log ~= nil and Logged < LOG_LINES then
		Logged = Logged + 1
		rfa_log("[rfg_ui_hud_weapon_sel] " .. text)
	end
end

local function armageddon_selector()
	return vint_object_find("weapon_selector_grp", 0, vint_document_find("ui_hud"))
end

local function is_slot(slot)
	return slot ~= nil and slot >= 1 and slot <= SLOTS
end

-- Called by Armageddon's data responder once per slot, in order.
function rfg_ui_hud_weapon_sel_populate(icon_name, ammo_count, ammo_max)
	Slots[#Slots + 1] = { icon_name = icon_name, ammo_count = ammo_count }
end

local function send_slots()
	local values = {}
	for slot = 1, SLOTS do
		local info = Slots[slot]
		local picture = NO_PICTURE
		local ammo = AMMO_COUNT_NONE
		Empty[slot] = true
		if info ~= nil and info.icon_name ~= nil and info.icon_name ~= "" and info.icon_name ~= "empty" then
			picture = info.icon_name
			ammo = info.ammo_count or AMMO_COUNT_NONE
			Empty[slot] = false
		end
		values[#values + 1] = ammo
		values[#values + 1] = picture
		values[#values + 1] = Names[slot] or ""
		log("slot " .. slot .. ": " .. picture .. ", ammo " .. tostring(ammo) .. ", name '" .. (Names[slot] or "") .. "'")
	end
	rfg_dataitem_set(SLOT_INFO, unpack(values))
end

local function send_show()
	local chosen = Chosen
	if not is_slot(chosen) then
		chosen = Equipped
	end
	if not is_slot(chosen) then
		chosen = 0
	end
	local before = Before
	if not is_slot(before) then
		before = chosen
	end
	rfg_dataitem_set(SHOW_INFO, Shown, chosen - 1, before - 1, false)
end

local function stop_closing()
	if Close_thread ~= -1 then
		thread_kill(Close_thread)
		Close_thread = -1
	end
end

-- Closes Guerrilla's select HOLD_AFTER_CLOSE seconds after Armageddon closed its own.
function rfg_ui_hud_weapon_sel_close_later()
	delay(HOLD_AFTER_CLOSE)
	Close_thread = -1
	Shown = false
	send_show()
end

function rfg_ui_hud_weapon_sel_radial_update(di_h, event_type)
	local state, refresh, weapon_name, slot_selected = vint_dataitem_get(di_h)
	local open = state ~= WEAPON_RADIAL_STATE_HIDDEN
	log("weapon_radial_hud: state " .. tostring(state) .. ", refresh " .. tostring(refresh) .. ", weapon '" ..
		tostring(weapon_name) .. "', slot " .. tostring(slot_selected))

	if open and not Open then
		Before = Equipped
		refresh = true
	end
	if refresh then
		Slots = {}
		vint_dataresponder_request("wpn_radial_populate", "rfg_ui_hud_weapon_sel_populate")
	end
	if is_slot(slot_selected) then
		Chosen = slot_selected
		if weapon_name ~= nil and weapon_name ~= "" then
			Names[slot_selected] = weapon_name
		end
	end
	if open then
		stop_closing()
		Shown = true
	elseif Open then
		-- Armageddon closed its selector: the weapon is picked.
		if is_slot(Chosen) then
			Equipped = Chosen
		end
		stop_closing()
		Close_thread = thread_new("rfg_ui_hud_weapon_sel_close_later")
	end
	Open = open

	send_slots()
	send_show()
end

-- Each frame: Armageddon's selector faded (its script shows it whenever it opens), and the
-- pictures of empty slots hidden (Guerrilla's script shows every slot's picture it is given, and
-- its document's "null" draws a grey square here).
function rfg_ui_hud_weapon_sel_keep_faded()
	while true do
		local selector = armageddon_selector()
		if selector ~= 0 then
			vint_set_property(selector, "alpha", 0)
		end
		for slot = 1, SLOTS do
			if Empty[slot] and Icons[slot] ~= nil and Icons[slot] ~= 0 then
				vint_set_property(Icons[slot], "visible", false)
			end
		end
		thread_yield()
	end
end

function rfg_ui_hud_weapon_sel_init()
	Log_on = rfa_get_setting ~= nil and rfa_get_setting("GuerrillaHud", "Log", 0) ~= 0
	rfg_dataitem_make(SLOT_INFO, SLOT_INFO_FIELDS)	-- before Guerrilla's script subscribes to them
	rfg_dataitem_make(SHOW_INFO, SHOW_INFO_FIELDS)
	rfg_ui_hud_weapon_sel_guerrilla_init()
	for slot = 1, SLOTS do
		Icons[slot] = vint_object_find("weapon_icon_0" .. slot)
	end
	vint_dataitem_add_subscription("weapon_radial_hud", "update", "rfg_ui_hud_weapon_sel_radial_update")
	Fade_thread = thread_new("rfg_ui_hud_weapon_sel_keep_faded")
end

function rfg_ui_hud_weapon_sel_cleanup()
	stop_closing()
	if Fade_thread ~= -1 then
		thread_kill(Fade_thread)
		Fade_thread = -1
	end
	local selector = armageddon_selector()
	if selector ~= 0 then
		vint_set_property(selector, "alpha", 1)
	end
	rfg_ui_hud_weapon_sel_guerrilla_cleanup()
end
