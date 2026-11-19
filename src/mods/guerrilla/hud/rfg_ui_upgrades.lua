-- Red Faction: Guerrilla's upgrade screen, in Armageddon's Guerrilla world: a safehouse's upgrade shop, opened
-- with the use key at its station (the mod pauses the game while it's open).
--
-- Part of the guerrilla_hud mod. The document this drives, rfg_ui_upgrades, is Guerrilla's own, and so is its
-- script, both from your copy of Guerrilla: GuerrillaBuilder.exe packs our shims, Guerrilla's script (its init
-- and cleanup renamed rfg_ui_upgrades_guerrilla_init and _cleanup; its set_header, set_button, set_hints and
-- set_highlight renamed rfg_ui_upgrades_..., as Armageddon's own scripts have globals of those names), the table
-- Rfg_upgrade_pictures (each upgrade's icon and blueprint, from your copy's upgrades.xtbl) and this script.
--
-- Guerrilla's game filled the screen's data items (HeaderData, the group ButtonData, HintData) from its
-- upgrades; this script calls Guerrilla's callbacks itself with the values in a table (the shims'
-- rfg_vint_dataitem_get reads either), from the mod's shop (rfg_shop, rfg_shop_item: guerrilla_gear's
-- upgrades, which it buys) and Guerrilla's own words for it (rfg_shop_text). Texts Guerrilla named by their
-- string tag's hash are handed over as words (the shims turn text_tag_crc into text_tag).

local ROWS = 4                          -- Guerrilla's MAX_BUTTONS
local SELECT_KEY = "rfg_key_24"         -- Guerrilla's PC key pictures: E (buy), Esc (back)
local BACK_KEY = "rfg_key_124"

local Shown = nil                       -- what the screen shows now, to redraw only when it changes
local Anchors = {}                      -- the document's own anchors of the parts moved (see below_label)
local Texts = {}
local Thread = -1

local function log(text)
	if rfa_log ~= nil then
		rfa_log("[rfg_ui_upgrades] " .. text)
	end
end

local function text(what)
	if Texts[what] == nil then
		Texts[what] = rfg_shop_text(what)
	end
	return Texts[what]
end

-- Guerrilla laid its header out for its own fonts; Armageddon's of the same names are taller, so a value
-- 20 units under its label (COST, SALVAGE) came out over it. Each value is put under its label, by the
-- label's height as drawn (screen_size is after the resolution's scale, the anchors before it, as
-- Guerrilla's own scroll bar code has it), from the document's own anchor each time.
local function below_label(label_name, value_name, gap)
	local label, value = vint_object_find(label_name), vint_object_find(value_name)
	if label == 0 or value == 0 then
		return
	end
	if Anchors[value_name] == nil then
		local x, y = vint_get_property(value, "anchor")
		Anchors[value_name] = { x, y }
	end
	local label_x, label_y = vint_get_property(label, "anchor")
	local width, height = vint_get_property(label, "screen_size")
	local scale_x, scale_y = vint_get_property(vint_object_find("stage_items"), "scale")
	if label_y == nil or height == nil or scale_y == nil or scale_y == 0 then
		return		-- not drawn yet: the document's own place
	end
	local x, y = Anchors[value_name][1], Anchors[value_name][2]
	local bottom = label_y + height / scale_y + gap
	if y < bottom then
		y = bottom
	end
	vint_set_property(value, "anchor", x, y)
end

-- One upgrade's row and the header for the one picked, as Guerrilla's game filled them.
local function show(count, highlighted, top, salvage, status)
	local name, shown, kind, level_name, level_text, bought, levels, cost, available = rfg_shop_item(highlighted)
	if name == nil then
		return
	end
	local pictures = Rfg_upgrade_pictures[name] or {}
	local title = level_name
	local price = "" .. cost
	if level_name == "" then	-- all bought
		title = shown
		price = ""
	end
	local scroll = 0
	if count > 1 then
		scroll = (highlighted - 1) / (count - 1)
	end
	-- A level that can't be bought yet says so where its price goes, in words, as Guerrilla's script
	-- has it (a price that isn't a number is set in font_body); the sub header stays hidden.
	if not available and level_name ~= "" then
		price = text("locked")
	end
	local sub_header = nil
	-- HeaderData: title, type, icon, image, price label, price, info, credits label, credits, scroll, show scroll, sub header.
	rfg_ui_upgrades_set_header({ title, kind, pictures[1], pictures[2], text("price"), price, level_text, text("credits"),
		"" .. salvage, scroll, count > ROWS, sub_header, n = 12 }, "update")
	below_label("header_cost_label", "header_cost_value", 0)
	below_label("header_credits_label", "header_credits_value", 0)

	-- ButtonData: index, visible, locked, available, highlighted, has dags, text, is new, show new.
	for row = 1, ROWS do
		local item = top + row - 1
		local item_name, item_shown, item_kind, item_level, item_text, item_bought, item_levels, item_cost, item_available = rfg_shop_item(item)
		if item_name ~= nil then
			local locked = not item_available and item_level ~= ""
			rfg_ui_upgrades_set_button({ row, true, locked, item_available, item == highlighted, false, item_shown, false, false }, "update")
		else
			rfg_ui_upgrades_set_button({ row, false, false, false, false, false, "", false, false }, "update")
		end
	end
	-- HintData: the picked row's button, the hint's button, its words.
	-- The last purchase's outcome, for a moment, where the hint's words go.
	local hint = text("back")
	if status ~= "" then
		hint = status
	end
	rfg_ui_upgrades_set_hints({ SELECT_KEY, BACK_KEY, hint }, "update")
end

local function follow()
	while true do
		local open, count, highlighted, top, salvage, status = rfg_shop()
		if open then
			local now = count .. "|" .. highlighted .. "|" .. top .. "|" .. salvage .. "|" .. status
			if now ~= Shown then
				local ok, problem = pcall(show, count, highlighted, top, salvage, status)
				if not ok then
					log("the upgrade screen stopped: " .. tostring(problem))
					return
				end
				Shown = now
			end
		else
			Shown = nil
		end
		thread_yield()
	end
end

function rfg_ui_upgrades_init()
	local ok, problem = pcall(rfg_ui_upgrades_guerrilla_init)
	if not ok then
		log("Guerrilla's init: " .. tostring(problem))
	end
	if rfg_shop == nil then
		return		-- an older mod: no shops
	end
	Thread = thread_new("rfg_ui_upgrades_follow")
end

function rfg_ui_upgrades_follow()
	follow()
end

function rfg_ui_upgrades_cleanup()
	if Thread ~= -1 then
		thread_kill(Thread)
		Thread = -1
	end
	pcall(rfg_ui_upgrades_guerrilla_cleanup)
end
