-- Red Faction: Guerrilla's reticles and ammo in Armageddon: Guerrilla's own document and script, fed
-- from Armageddon's weapon data.
--
-- Part of the guerrilla_hud mod. GuerrillaBuilder.exe makes the document rfg_ui_hud_reticles from your
-- copy of Guerrilla (its pictures renamed rfg_...), and its script from three parts: our
-- rfg_guerrilla_shims.lua, then Guerrilla's own script for it (its init and cleanup renamed
-- rfg_ui_hud_reticles_guerrilla_init and _cleanup, its pictures' names renamed like the
-- document's), then this file. Run it again after changing this file.
--
-- Guerrilla's script draws everything from one data item Guerrilla's game filled every frame,
-- WeaponFire. Armageddon has no such item; it fills its own (reticule_hud, primary_weapon_hud) for
-- its own HUD. So this script makes WeaponFire (through the mod's rfg_dataitem_make) and fills it
-- from those (rfg_dataitem_set), and Guerrilla's script does the rest as it did in Guerrilla:
--   - the reticle: the weapon's own picture (Guerrilla's weapons each name one; a weapon without one,
--     as every Armageddon weapon, gets Guerrilla's assault reticle, as its script says), red over an
--     enemy, green over a friend, Guerrilla's hit markers. Which of Guerrilla's weapons is held, the
--     picture the game gives for it tells (primary_weapon_hud's icon: the weapons build names each one's
--     own, rfg_ui_hud_weapon_icon_...): GuerrillaBuilder.exe puts the table Rfg_weapon_reticles before
--     this script, each picture's reticle and its reticle while aiming finely;
--   - the ammo: rounds in the magazine and the rest in the box beside the reticle (the infinity sign
--     where Armageddon's is infinite), blinking red when a reload is due;
--   - the overheat meter: for a weapon without a magazine, filling as the energy Armageddon's
--     weapon meter shows runs down.
-- Armageddon's own reticle, weapon meter and ammo ring are faded out while this is loaded, and back
-- in when it goes; its Nano Forge meter, its magnet gun's marks and its reload and wait prompts stay.

local WEAPON_FIRE = "WeaponFire"
-- WeaponFire's fields, as Guerrilla's script reads them (b: boolean, s: string, f: float, i: integer):
-- bogus, reload_warning, can_fire, is_reloading, weapon_name, weapon_icon_name, weapon_reticule_name,
-- accuracy_percent, ammo_rounds, ammo_reserve, ammo_magazine_size, targeting,
-- local_player_hit_someone_this_frame, is_overheated, overheat_percent, is_turret,
-- secondary_can_fire, lock_on, show_ammo_counter, multi_rockets, charges, status
local WEAPON_FIRE_FIELDS = "bbbbsssfiiiibbfbbibiii"

-- Guerrilla's reticle for a weapon that has none of its own.
local DEFAULT_RETICLE = "rfg_ui_hud_reti_assault"

-- The reticle for the weapon held: Guerrilla's own for one of its weapons (by its picture; while aiming
-- finely, its fine-aim reticle where it has one), else the default.
local function reticle_for(icon, fine_aimed)
	local own = Rfg_weapon_reticles ~= nil and icon ~= nil and Rfg_weapon_reticles[icon] or nil
	if own == nil then
		return DEFAULT_RETICLE
	end
	if fine_aimed and own[2] ~= nil then
		return own[2]
	end
	return own[1]
end

-- Armageddon's values (ui_hud.lua).
local RETICULE_STATE_HIDE = -1
local AMMO_AREA_STATUS_HIDDEN = 0
local AMMO_AREA_STATUS_AMMO_COUNT = 2
local AMMO_COUNT_INFINITE = -1
local RELOAD_MESSAGE_RELOAD = 1

-- Armageddon's HUD (ui_hud) parts this replaces. Faded once: its script only shows and hides them.
-- Faded every frame: its script sets their alpha back. Hidden every frame: its hit animation fades
-- them in.
local ARMAGEDDON_FADED = { "normal", "reticule_friendly", "meter_weapon", "ammo_area_vehicle", "ammo_area_bg",
	"ammo_bg_ring", "ammo_bg_ring_2", "ammo_bg_tech" }
local ARMAGEDDON_FADED_EACH_FRAME = { "ammo_area_on_foot" }
local ARMAGEDDON_HIDDEN_EACH_FRAME = { "reticule_hit", "reticule_hit_2" }

local Reticle = {}		-- reticule_hud: weapon_name, state, scale, hit, magnet_target, fine_aimed
-- Guerrilla's names for the weapons its script asks about by name for its remote charge squares (handle_charge_ammo:
-- Max_charges taken, and the squares shown with none out, while the charge placer or mine placer is held); ours are
-- named rfg_<its name>.
local GUERRILLA_NAMES = { rfg_charge_placer = "charge_placer", rfg_mine_placer = "mine_placer" }
local Charges_sent = { -1, -1 }		-- the charges out and blinking last sent
local Weapon = {}		-- primary_weapon_hud: weapon_name, icon, target_name, bar_visible, bar_pct,
					-- bar_is_red, ammo_status, total_ammo, clip_size, clip_ammo, reload_message, flash_type
local Hud_hidden = false
local Fade_thread = -1

-- Guerrilla's reload animation, its own (its script, on WeaponFire's is_reloading): as a reload starts the box slides
-- left over the magazine's count (update_ammo_tween1, 36 units) and the count fades out (hide_cliptext_tween1), the
-- reticle pulses (reload_pulse_tween); when the reload ends they come back (update_ammo_tween2, hide_cliptext_tween2):
-- the number on the left collapses for the reload and shows again once the gun has refilled, and not again until the
-- next reload. Armageddon's HUD data has no "reloading now", so here a reload runs from its start until the magazine
-- fills up again (Armageddon moves the rounds in at the reload's end): it starts on Armageddon's reload key (R) with room
-- in the magazine and rounds to fill it, or on the magazine running empty with rounds left; it ends on the refill, on
-- Armageddon's flash_type 1 (FLASH_TYPE_WEAPON_RELOAD, which its own script never reads: the game raises it for a moment
-- as the rounds go in, the reload's end, never its start), a change of weapon, or after RELOAD_MOST_SECONDS (a reload cut
-- short). All of it follows primary_weapon_hud's own weapon (the one its ammo is of): reticule_hud's name lags it by an
-- update when a weapon changes (getting into a turret, the turret's empty magazine was taken for the rifle's).
-- Each start and end is logged (the first RELOAD_LOG_LINES).
local FLASH_TYPE_WEAPON_RELOAD = 1
local RELOAD_KEY = 0x52					-- R, Armageddon's reload key
local RELOAD_MOST_SECONDS = 5
local RELOAD_LOG_LINES = 60
local Reload = { weapon = nil, rounds = nil, reserve = nil, magazine = 0, flash = nil, active = false, started = 0,
	key_down = false, logged = 0 }

local function clock()
	if _GetAnyGlobalSilent("rfg_handbook_clock") ~= nil then
		return rfg_handbook_clock()
	end
	return 0
end

-- Seconds since the reload's start, to a hundredth (Armageddon's Lua has no string library: no string.format).
local function seconds_since_start()
	local seconds = clock() - Reload.started
	return tostring(seconds - seconds % 0.01) .. " s"
end

local function log_reload(text)
	if rfa_log ~= nil and Reload.logged < RELOAD_LOG_LINES then
		Reload.logged = Reload.logged + 1
		rfa_log("[rfg_ui_hud_reticles] reload: " .. text)
	end
end

local function armageddon_part(name)
	return vint_object_find(name, 0, vint_document_find("ui_hud"))
end

local function fade_armageddon(alpha)
	for i, name in ipairs(ARMAGEDDON_FADED) do
		local part = armageddon_part(name)
		if part ~= 0 then
			vint_set_property(part, "alpha", alpha)
		end
	end
end

-- Each change in what shows (the reticle, the ammo) with what Armageddon gave for it, written to the log (at most
-- SHOWN_LOG_LINES a load): to tell why the ammo may show with no reticle.
local SHOWN_LOG_LINES = 60
local Shown_logged = 0
local Shown_last = ""

local function log_shown(reticle_shown, ammo_shown)
	local shown = tostring(reticle_shown) .. " " .. tostring(ammo_shown) .. " " .. tostring(Reticle[1]) .. " " .. tostring(Weapon[1])
	if shown == Shown_last or Shown_logged >= SHOWN_LOG_LINES or rfa_log == nil then
		return
	end
	Shown_last = shown
	Shown_logged = Shown_logged + 1
	rfa_log("[rfg_ui_hud_reticles] reticle " .. tostring(reticle_shown) .. ", ammo " .. tostring(ammo_shown) ..
		": reticule_hud weapon '" .. tostring(Reticle[1]) .. "' state " .. tostring(Reticle[2]) .. "; primary_weapon_hud weapon '" ..
		tostring(Weapon[1]) .. "' ammo status " .. tostring(Weapon[7]) .. ", total " .. tostring(Weapon[8]) .. ", clip " ..
		tostring(Weapon[10]) .. " of " .. tostring(Weapon[9]))
end

-- Shows or hides Guerrilla's reticle and ammo as Armageddon would show its own.
local function show_parts()
	local reticle_shown = not Hud_hidden and Reticle[1] ~= nil and Reticle[1] ~= "" and Reticle[2] ~= RETICULE_STATE_HIDE
	local ammo_shown = not Hud_hidden and Weapon[7] ~= nil and Weapon[7] ~= AMMO_AREA_STATUS_HIDDEN
	log_shown(reticle_shown, ammo_shown)
	vint_set_property(vint_object_find("reticle_main"), "visible", reticle_shown)
	vint_set_property(vint_object_find("ammo_main"), "visible", ammo_shown)
	vint_set_property(vint_object_find("charge_main"), "visible", ammo_shown)
end

-- WeaponFire from Armageddon's two items.
local function send()
	local weapon_name = Reticle[1] or Weapon[1] or ""
	local state = Reticle[2] or RETICULE_STATE_HIDE
	local scale = Reticle[3] or 1
	local hit = Reticle[4] == true
	local fine_aimed = Reticle[6] == true
	local bar_visible = Weapon[4] == true
	local bar_pct = Weapon[5] or 0
	local bar_is_red = Weapon[6] == true
	local ammo_status = Weapon[7] or AMMO_AREA_STATUS_HIDDEN
	local total = Weapon[8] or 0
	local clip_size = Weapon[9] or 0
	local clip = Weapon[10] or 0
	local reload_message = Weapon[11] or 0
	local target_name = Weapon[3] or "player"

	-- Guerrilla counts the magazine and the rest apart; Armageddon's total includes the magazine.
	-- Guerrilla's -1 is "no limit": in the magazine's place (no magazine) or the rest's.
	local rounds, reserve, magazine
	if total == AMMO_COUNT_INFINITE and clip_size > 0 then
		rounds, reserve, magazine = clip, -1, clip_size
	elseif total == AMMO_COUNT_INFINITE then
		rounds, reserve, magazine = -1, 0, 0
	elseif clip_size > 0 then
		rounds, reserve, magazine = clip, total - clip, clip_size
	else
		rounds, reserve, magazine = total, 0, 0
	end
	local can_fire = rounds ~= 0
	-- Reloading now (see Reload above), by the ammo's own weapon.
	local ammo_weapon = Weapon[1] or ""
	local flash = Weapon[12] or 0
	local flash_rose = flash == FLASH_TYPE_WEAPON_RELOAD and Reload.flash ~= FLASH_TYPE_WEAPON_RELOAD
	Reload.flash = flash
	if Reload.active and ammo_weapon ~= Reload.weapon then
		Reload.active = false
		log_reload("ended: another weapon (" .. tostring(Reload.weapon) .. " to " .. tostring(ammo_weapon) .. ")")
	end
	if ammo_weapon == Reload.weapon and Reload.rounds ~= nil and magazine > 0 and rounds > Reload.rounds and
		(reserve == -1 or (Reload.reserve ~= nil and reserve < Reload.reserve)) then
		if Reload.active then
			log_reload("ended: the magazine filled up (" .. tostring(Reload.rounds) .. " to " .. tostring(rounds) .. ", " ..
				tostring(ammo_weapon) .. ", " .. seconds_since_start() .. " after its start)")
		else
			log_reload("a refill with no reload seen to start (" .. tostring(Reload.rounds) .. " to " .. tostring(rounds) .. ", " .. tostring(ammo_weapon) .. ")")
		end
		Reload.active = false
	elseif not Reload.active and magazine > 0 and rounds == 0 and reserve ~= 0 and Reload.rounds ~= nil and Reload.rounds > 0 and
		ammo_weapon == Reload.weapon then
		Reload.active = true
		Reload.started = clock()
		log_reload("started: the magazine ran empty (" .. tostring(ammo_weapon) .. ", " .. tostring(reserve) .. " left)")
	end
	if flash_rose and Reload.active then
		Reload.active = false
		log_reload("ended: flash_type 1, the rounds going in (" .. tostring(ammo_weapon) .. ", " .. tostring(rounds) ..
			" in the magazine, " .. seconds_since_start() .. " after its start)")
	end
	Reload.weapon, Reload.rounds, Reload.reserve, Reload.magazine = ammo_weapon, rounds, reserve, magazine
	local is_reloading = Reload.active
	local targeting = state
	if targeting < 0 then
		targeting = 0
	end
	-- Guerrilla's accuracy is a percentage, below zero while zoomed.
	local accuracy = scale * 100
	if fine_aimed then
		accuracy = -accuracy
	end
	-- Armageddon's weapon meter is what is left: of the magazine, or for a weapon without one, of
	-- its energy. Guerrilla's overheat meter is how near the weapon is to having to stop, shown
	-- only once it starts to fill; so a weapon without a magazine gets it, filling as its energy
	-- runs down, red where Armageddon's turns red. (Magazines are counted in numbers, as in
	-- Guerrilla; its magnet gun has no meter.)
	local overheat = 0
	local overheated = false
	if bar_visible and clip_size <= 0 and weapon_name ~= "magnet_gun" then
		overheat = (1 - bar_pct) * 100
		overheated = bar_is_red
	end

	-- Guerrilla's remote charge squares beside the reticle (its WeaponFire's charges and status): the player's charges
	-- out, and how many of them blink (those not stuck to anything), from the mod (guerrilla_gear's placed explosives).
	local charges, loose = 0, 0
	if _GetAnyGlobalSilent("rfg_charges") ~= nil then
		charges, loose = rfg_charges()
	end
	Charges_sent[1], Charges_sent[2] = charges, loose
	rfg_dataitem_set(WEAPON_FIRE, true, reload_message == RELOAD_MESSAGE_RELOAD, can_fire, is_reloading,
		GUERRILLA_NAMES[weapon_name] or weapon_name, Weapon[2] or "", reticle_for(Weapon[2], fine_aimed), accuracy, rounds,
		reserve, magazine, targeting, hit, overheated, overheat, target_name ~= "player", true, 0,
		ammo_status == AMMO_AREA_STATUS_AMMO_COUNT, -1, charges, loose)
	show_parts()
end

function rfg_ui_hud_reticles_reticle_update(di_h, event_type)
	Reticle = { vint_dataitem_get(di_h) }
	send()
end

function rfg_ui_hud_reticles_weapon_update(di_h, event_type)
	Weapon = { vint_dataitem_get(di_h) }
	send()
end

-- Cutscenes and the like hide the HUD (all but the subtitles): hide this with it.
function rfg_ui_hud_reticles_hide(di_h, event_type)
	Hud_hidden = vint_dataitem_get(di_h)
	show_parts()
end

-- Every frame, the parts of Armageddon's HUD its own script would bring back.
function rfg_ui_hud_reticles_keep_faded()
	while true do
		for i, name in ipairs(ARMAGEDDON_FADED_EACH_FRAME) do
			local part = armageddon_part(name)
			if part ~= 0 then
				vint_set_property(part, "alpha", 0)
			end
		end
		for i, name in ipairs(ARMAGEDDON_HIDDEN_EACH_FRAME) do
			local part = armageddon_part(name)
			if part ~= 0 then
				vint_set_property(part, "visible", false)
			end
		end
		-- The charges out change without Armageddon's weapon data changing (one detonated, one stuck): sent again.
		if Weapon[1] ~= nil and _GetAnyGlobalSilent("rfg_charges") ~= nil then
			local placed, loose = rfg_charges()
			if placed ~= Charges_sent[1] or loose ~= Charges_sent[2] then
				send()
			end
		end
		-- A reload's start by its key (with rounds in the magazine but room for more, and rounds to fill it: an empty
		-- magazine starts one by itself, see send; a turret's stays at 0), and one cut short.
		if Weapon[1] ~= nil and _GetAnyGlobalSilent("rfg_key_down") ~= nil then
			local down = rfg_key_down(RELOAD_KEY)
			if down and not Reload.key_down and not Reload.active and Reload.magazine > 0 and Reload.rounds ~= nil and
				Reload.rounds > 0 and Reload.rounds < Reload.magazine and Reload.reserve ~= 0 then
				Reload.active = true
				Reload.started = clock()
				log_reload("started: the reload key (" .. tostring(Reload.weapon) .. ", " .. tostring(Reload.rounds) .. " of " .. tostring(Reload.magazine) .. ")")
				send()
			end
			Reload.key_down = down
		end
		if Reload.active and clock() - Reload.started > RELOAD_MOST_SECONDS then
			Reload.active = false
			log_reload("ended: no refill after " .. tostring(RELOAD_MOST_SECONDS) .. " s (cut short)")
			if Weapon[1] ~= nil then
				send()
			end
		end
		thread_yield()
	end
end

function rfg_ui_hud_reticles_init()
	rfg_dataitem_make(WEAPON_FIRE, WEAPON_FIRE_FIELDS)	-- before Guerrilla's script subscribes to it
	rfg_ui_hud_reticles_guerrilla_init()
	-- Guerrilla's safehouse weapon panel (a placeholder "SHOTGUN 999/999" until its game said
	-- otherwise, through SafehouseInfo, which Armageddon hasn't got).
	vint_set_property(vint_object_find("weapon_data_main"), "visible", false)
	vint_set_property(vint_object_find("weapon_swap_main"), "visible", false)
	fade_armageddon(0)
	vint_dataitem_add_subscription("reticule_hud", "update", "rfg_ui_hud_reticles_reticle_update")
	vint_dataitem_add_subscription("primary_weapon_hud", "update", "rfg_ui_hud_reticles_weapon_update")
	vint_dataitem_add_subscription("hud_hide", "update", "rfg_ui_hud_reticles_hide")
	Fade_thread = thread_new("rfg_ui_hud_reticles_keep_faded")
end

function rfg_ui_hud_reticles_cleanup()
	if Fade_thread ~= -1 then
		thread_kill(Fade_thread)
		Fade_thread = -1
	end
	fade_armageddon(1)
	for i, name in ipairs(ARMAGEDDON_FADED_EACH_FRAME) do
		local part = armageddon_part(name)
		if part ~= 0 then
			vint_set_property(part, "alpha", 1)
		end
	end
	for i, name in ipairs(ARMAGEDDON_HIDDEN_EACH_FRAME) do
		local part = armageddon_part(name)
		if part ~= 0 then
			vint_set_property(part, "visible", true)
		end
	end
	rfg_ui_hud_reticles_guerrilla_cleanup()
end
