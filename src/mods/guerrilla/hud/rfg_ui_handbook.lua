-- Red Faction: Guerrilla's handbook in Armageddon's Guerrilla world, opened from the pause menu's HANDBOOK: its pages
-- (the categories with an entry open), the open entries of the page shown (sixteen at a time, a scroll bar beyond),
-- and the entry picked: its title, text and picture (a weapon's icon on the combat page; a radio tag's number on the
-- radio tags' page). Entries not read yet are marked new, their pages too. Up / down pick an entry, left / right the
-- page, Esc goes back to the pause menu. The game is paused while it's open.
--
-- Part of the guerrilla_hud mod. The document this drives, rfg_ui_handbook, is Guerrilla's own, and so is its script,
-- both from your copy of Guerrilla: GuerrillaBuilder.exe packs our shims, Guerrilla's script (its init and cleanup
-- renamed rfg_ui_handbook_guerrilla_init and _cleanup, its generic globals rfg_ui_handbook_<name>) and this script, in
-- that order; run it again after changing this file. Guerrilla's game filled data items for it (HeaderData,
-- PageTabData, the group RowData, HintData, and one item a page kind: GameplayData, ActionsData, ...), which
-- Armageddon hasn't: this script calls Guerrilla's callbacks itself with the values in a table, from what the mod
-- answers (rfg_handbook, rfg_handbook_screen, rfg_handbook_page_new, rfg_handbook_rows, rfg_handbook_row,
-- rfg_handbook_entry). The mod has the handbook's rules (guerrilla_hud.cpp, "Guerrilla's handbook").
-- Not yet: the videos (the gameplay page's, shown here as its text; the cutscenes', which a row would play) and the
-- radio tags' recordings and portraits.

local LEFT_KEY = "rfg_key_48"			-- Guerrilla's PC key pictures: A and D (the pages), Esc (back)
local RIGHT_KEY = "rfg_key_50"
local BACK_KEY = "rfg_key_124"
local PAPER = "rfg_ui_handbook_paper"	-- the text's paper (Guerrilla's script names it unrenamed)
local SCROLL_SECONDS = 20				-- Guerrilla's hbk_duration: a long text scrolls through in about this long
local SCROLL_HOLD_SECONDS = 10			-- and stays at its end this long before it starts again (its alpha tween)
local MASK_HEIGHT = 234					-- the note's text mask (info_text_mask), in the document's units
local TITLE_BAR_HEIGHT = 24				-- the title's bar (title_bg, centred at 13 in title_main), in the same units
local TITLE_BAR_CENTRE = 13
local ARMAGEDDON_HUD = "non_subtitles"	-- Armageddon's HUD (ui_hud), all but its subtitles
local NEUTRAL_PICTURE = "rfg_ui_hud_base_square"	-- a white square every document of ours has
local Version = -1
local Thread = -1
local Rows_shown = 0			-- the rows Guerrilla's script has now
local Hud_hidden = false		-- Armageddon's HUD hidden by us while the handbook is open
local Hud_was_visible = true
local Failed = {}
local Scroll = nil				-- the note's text scrolling now (see start_scroll)
local Pictures_logged = 0

local function log(text)
	if rfa_log ~= nil then
		rfa_log("[rfg_ui_handbook] " .. text)
	end
end

-- One of Guerrilla's callbacks with a table of values; its first error of each kind is logged.
local function call(callback, name, values, event)
	local ok, problem = pcall(callback, values, event or "update")
	if not ok and not Failed[name] then
		Failed[name] = true
		log(name .. ": " .. tostring(problem))
	end
	return ok
end

-- A text longer than the note scrolls through it, as Guerrilla's hbk_info_text_animate made it: from 10 down to where
-- its end meets the mask's, in (its height / the mask's) x 20 s + 1.5 s, then stays 10 s and starts again. Guerrilla
-- did it with the document's tweens, which stand still here while the game is paused: so here each frame
-- (follow_scroll), by the mod's clock.
local function start_scroll(kind)
	Scroll = nil
	local anim = vint_object_find("info_scroll_anim")
	if anim ~= 0 then
		vint_set_child_tween_state(anim, TWEEN_STATE_DISABLED)
	end
	local text = vint_object_find("info_text")
	local mask = vint_object_find("info_text_mask")
	if text == 0 or mask == 0 or kind == "bftp" or kind == "cutscene" or rfg_handbook_clock == nil then
		return
	end
	local x = vint_get_property(text, "anchor")
	vint_set_property(text, "anchor", x, 0)
	local text_width, text_height = vint_get_property(text, "screen_size")
	local mask_width, mask_height = vint_get_property(mask, "screen_size")
	-- The mask is a 1 x 1 square scaled to 416.67 x 234, and Armageddon's screen_size of a bitmap leaves its own scale
	-- out (it measured a pixel or two, so every text scrolled, 2026-11-12): its height is that times its scale's.
	local mask_scale_x, mask_scale_y = vint_get_property(mask, "scale")
	if mask_height ~= nil and mask_scale_y ~= nil and mask_height < mask_scale_y / 2 then
		mask_height = mask_height * mask_scale_y
	end
	-- Only a text taller than the note scrolls (Guerrilla's text_height > mask_height).
	if text_height == nil or mask_height == nil or mask_height <= 0 or text_height <= mask_height + 2 then
		return
	end
	local units = MASK_HEIGHT / mask_height		-- the document's units a screen pixel
	Scroll = { text = text, x = x, from = 10, to = (mask_height - text_height) * units, start = rfg_handbook_clock(),
		seconds = text_height / mask_height * SCROLL_SECONDS + 1.5 }
	vint_set_property(text, "anchor", x, Scroll.from)
end

local function follow_scroll()
	if Scroll == nil then
		return
	end
	local t = rfg_handbook_clock() - Scroll.start
	if t > Scroll.seconds + SCROLL_HOLD_SECONDS then
		Scroll.start = rfg_handbook_clock()
		t = 0
	end
	if t > Scroll.seconds then
		t = Scroll.seconds
	end
	vint_set_property(Scroll.text, "anchor", Scroll.x, Scroll.from + (Scroll.to - Scroll.from) * t / Scroll.seconds)
end

-- The entry picked, by its page's kind (Guerrilla's page setters; the gameplay page's videos aren't brought over yet:
-- its text shows as the background page's does).
local function show_entry()
	local kind, title, text, picture, icon, video, name, counter = rfg_handbook_entry()
	if kind == nil then
		if rfg_handbook_picture_shown ~= nil then
			rfg_handbook_picture_shown("")
		end
		Scroll = nil
		return
	end
	-- The picture's element shows a neutral picture first, so a picture set again (its entry picked again, its
	-- document loaded again and its sheet tied again by the mod) is looked up afresh, not kept from before.
	local holder = vint_object_find("image_holder")
	if holder ~= 0 and picture ~= nil and picture ~= "" then
		vint_set_property(holder, "image", NEUTRAL_PICTURE)
	end
	if kind == "actions" then
		call(rfg_ui_handbook_set_actions, "set_actions", { true, title, text, picture, "", n = 5 })
	elseif kind == "combat" then
		call(rfg_ui_handbook_set_combat, "set_combat", { true, title, text, picture, icon, n = 5 })
	elseif kind == "vehicles" then
		call(rfg_ui_handbook_set_vehicles, "set_vehicles", { true, title, text, picture, n = 4 })
	elseif kind == "bftp" then
		-- The page's first entry (not numbered) is its own text; the counter is the tags found of all, on each.
		call(rfg_ui_handbook_set_blast_from_past, "set_blast_from_past", { true, title, text, picture, counter or "", 0, tonumber(name) == nil, n = 7 })
	elseif kind == "cutscene" then
		-- Guerrilla shows nothing beside the list of its cutscenes: a row plays its cutscene (not brought over yet).
		call(rfg_ui_handbook_set_cutscene, "set_cutscene", { true, title, text, "", "", n = 5 })
	else
		call(rfg_ui_handbook_set_background, "set_background", { true, title, text, picture, n = 4 })
	end
	-- The picture shown now: its document stays while it is (the mod unloads it a moment after it's gone).
	if rfg_handbook_picture_shown ~= nil then
		local shown = ""
		if kind ~= "bftp" and kind ~= "cutscene" and picture ~= nil then
			shown = picture
		end
		rfg_handbook_picture_shown(shown)
	end
	-- For finding out what goes wrong (the first pictures shown): the size the game gives the picture and where it is.
	if holder ~= 0 and picture ~= nil and picture ~= "" and Pictures_logged < 12 then
		Pictures_logged = Pictures_logged + 1
		local ok_size, w, h = pcall(vint_get_property, holder, "unscaled_size")
		local ok_box, x1, y1, x2, y2 = pcall(vint_get_bbox, holder)
		log("picture " .. tostring(picture) .. ": its size " .. tostring(w) .. " x " .. tostring(h) .. ", on screen " ..
			tostring(x1) .. ", " .. tostring(y1) .. " to " .. tostring(x2) .. ", " .. tostring(y2))
	end
	start_scroll(kind)
end

local function update()
	local title, page_name, page, pages, show_scroll, scroll, back, new_word = rfg_handbook_screen()
	call(rfg_ui_handbook_set_header, "set_header", { title, show_scroll, scroll, n = 3 })
	local tabs = { page_name, page, pages, n = 11 }
	for i = 1, 8 do
		tabs[3 + i] = i <= pages and rfg_handbook_page_new(i) == true
	end
	call(rfg_ui_handbook_set_page_tab, "set_page_tab", tabs)
	-- The rows: Guerrilla's script adds one with each "insert" and takes the last away with each remove.
	local count, picked = rfg_handbook_rows()
	while Rows_shown > count do
		call(rfg_ui_handbook_remove_row, "remove_row", nil, "remove")
		Rows_shown = Rows_shown - 1
	end
	for i = 1, count do
		local caption, is_new = rfg_handbook_row(i)
		local event = "update"
		if i > Rows_shown then
			event = "insert"
			Rows_shown = i
		end
		local new_text = ""
		if is_new then
			new_text = new_word
		end
		-- RowData: index, caption, highlighted, selectable (nothing to play yet), dags (every other row), new
		call(rfg_ui_handbook_update_row, "update_row", { i, caption, i == picked, false, i % 2 == 0, new_text, n = 6 }, event)
	end
	show_entry()
	-- HintData: the select button (none), the page keys, the back key and its word.
	call(rfg_ui_handbook_set_hints, "set_hints", { "null", LEFT_KEY, RIGHT_KEY, BACK_KEY, back, n = 5 })
end

-- Guerrilla hid its HUD while the handbook was up (it was a screen of the pause menu's): the mod draws none of
-- Guerrilla's then, and this hides Armageddon's own each frame, and puts it back as it was once the handbook closes.
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
		local open, version = rfg_handbook()
		if open or Hud_hidden then
			hide_armageddon_hud(open)
		end
		if open and version ~= Version then
			Version = version
			update()
		elseif not open then
			Version = -1
			Scroll = nil
		end
		if open then
			follow_scroll()
		end
		thread_yield()
	end
end

-- The title (black, on its orange bar): Armageddon's font_header stands taller than Guerrilla's, and the title reached
-- above its bar onto the black behind it (its top half not seen). Made to fit the bar, and centred on it.
local function fit_title()
	local text = vint_object_find("title_text")
	local bar = vint_object_find("title_bg")
	if text == 0 or bar == 0 then
		return
	end
	local text_width, text_height = vint_get_property(text, "screen_size")
	local bar_width, bar_height = vint_get_property(bar, "screen_size")
	if text_height == nil or bar_height == nil or text_height <= 0 or bar_height <= 0 then
		return
	end
	local most = bar_height * 0.9
	if text_height > most then
		local scale_x, scale_y = vint_get_property(text, "text_scale")
		local by = most / text_height
		vint_set_property(text, "text_scale", scale_x * by, scale_y * by)
		text_height = most
	end
	local x = vint_get_property(text, "anchor")
	vint_set_property(text, "anchor", x, TITLE_BAR_CENTRE - TITLE_BAR_HEIGHT * text_height / bar_height / 2)
end

function rfg_ui_handbook_init()
	local ok, problem = pcall(rfg_ui_handbook_guerrilla_init)
	if not ok then
		log("Guerrilla's init: " .. tostring(problem))
	end
	ok, problem = pcall(fit_title)
	if not ok then
		log("fit_title: " .. tostring(problem))
	end
	local paper = vint_object_find("info_bg_image")
	if paper ~= 0 then
		vint_set_property(paper, "image", PAPER)
	end
	if rfg_handbook == nil then
		return		-- an older mod: no handbook
	end
	Thread = thread_new("rfg_ui_handbook_follow")
end

function rfg_ui_handbook_follow()
	follow()
end

function rfg_ui_handbook_cleanup()
	if Thread ~= -1 then
		thread_kill(Thread)
		Thread = -1
	end
	pcall(hide_armageddon_hud, false)
	pcall(rfg_ui_handbook_guerrilla_cleanup)
end
