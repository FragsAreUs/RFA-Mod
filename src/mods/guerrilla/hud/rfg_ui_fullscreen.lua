-- Red Faction: Guerrilla's world map in Armageddon. Opened and closed with [GuerrillaHud] MapKey= (Tab);
-- Esc closes it too. The game is paused while it's open, as Guerrilla's was.
--
-- Part of the guerrilla_hud mod. The document this drives, rfg_ui_fullscreen, is Guerrilla's own
-- full-screen map document, and its script is Guerrilla's own too, both from your copy of Guerrilla:
-- GuerrillaBuilder.exe packs our shims, Guerrilla's script (its init and cleanup renamed
-- rfg_ui_fullscreen_guerrilla_init and _cleanup) and this script, in that order, with the document;
-- run it again after changing this file. The mod loads it with the minimap in Guerrilla's world and
-- puts it on screen while the map is open.
--
-- Guerrilla's game code drew the map itself in the document's map element (its "cartographer") and
-- told its script the rest through data items. Here the mod keeps the map's view (where it is
-- centred, its zoom: rfg_map_full()) and its keys, and this script:
--   - draws the map: Guerrilla's world map picture, the zones' own pictures once zoomed in close
--     (rfg_map_full_tiles()), Guerrilla's map icons (its safehouses and its missions' and
--     activities' starts) and the player's icon, turned the way the camera looks;
--   - calls Guerrilla's own script as Guerrilla's game did, with the values in a table in place of a
--     data item (the shims' rfg_vint_dataitem_get reads either): its places' labels and the zoom
--     readout (rfg_ui_fs_update_poi_group_item, for FS_POI_Group), the box naming the icon under the
--     crosshair (rfg_ui_fs_highlight_item, FS_HighlightItem) and the hints (rfg_ui_fs_update_hints,
--     FS_Hints, with the PC keys written in);
--   - names the district under the crosshair in the header, and shows the salvage count
--     (Armageddon's salvage_hud).
-- Guerrilla's district control and morale meters and its district filter have nothing behind them in
-- Armageddon yet, so they stay hidden; so does the beacon's hint until beacons come.

local SCREEN_W, SCREEN_H = 1280, 720    -- the document's layout
local MAP_PIXELS = 1024                 -- Guerrilla's world map picture
local ZONE_PIXELS = MAP_PIXELS / 12     -- a zone on it (columns and rows 2 to 13)
local TILE_PIXELS = 512                 -- a zone's own picture
local TILES = 16                        -- zone pictures at once (the mod's kFullMapTiles)
local MARKER_SCALE = 1.2                -- the player's icon, as the document's own map icon
local ICON_SCALE = 0.5                  -- Guerrilla's map icons (40 x 40 pictures)
local HIGHLIGHT_REACH = 14              -- an icon this close to the crosshair (screen units) is highlighted
local ON_SCREEN_MARGIN = 40
local MAP_DEPTH = 30                    -- behind the document's deepest parts (20)

-- The hints' keys: Guerrilla's own PC key pictures (its sheet ui_bms_pc_buttons, from interface-pc), each
-- 31 units square (the mouse wheel's 62 wide), anchored at their right edge as the document's pictures are.
local KEY_PICTURES = { pan = { "rfg_key_06", "rfg_key_48", "rfg_key_49", "rfg_key_50" }, zoom = "rfg_key_133",
	back = "rfg_key_124", center = "rfg_key_75" }
local KEY_STEP = 33                     -- from one of the pan hint's four keys to the next
local WHEEL_EXTRA = 31                  -- the wheel's picture is this much wider than a key's

local HIDDEN = { "control_meter", "morale_meter", "district_filter_main", "fullscreen_map", "hint_group4" }

local Map = 0                           -- our group: the map, over the black ground
local World = 0
local Marker = 0
local Tile_handles = {}
local Tile_shown = {}
local Icons = {}                        -- { handle, x, y, picture, caption, description } on the world map
local Places = {}                       -- { x, y, name } Guerrilla's named places
local Player = { picture = "rfg_ui_map_icon_player", caption = "", description = "" }
local Texts = {}                        -- the hints' words and the salvage label, by what they are
local Labels = 0                        -- the document's map_label_screen_group
local Labels_inserted = false
local Hint_keys_made = false
local Highlighted = nil
local Open_frames = 0
local Shown_district = nil
local Thread = -1

local function find(name)
	return vint_object_find(name)
end

local function log(text)
	if rfa_log ~= nil then
		rfa_log("[rfg_ui_fullscreen] " .. text)
	end
end

local function picture(name, parent, image)
	local handle = vint_object_create(name, "bitmap", parent)
	vint_set_property(handle, "image", image)
	return handle
end

local function two_digits(number)
	if number < 10 then
		return "0" .. number
	end
	return "" .. number
end

-- Guerrilla's script reads two of its globals Armageddon's interface hasn't (Guerrilla's
-- rfg_ui_globals.lua); its values.
local function guerrilla_globals()
	if not pcall(function() return COLOR_MORALE end) then
		COLOR_MORALE = { R = 0.943, G = 0.78, B = 0.20 }
	end
	if not pcall(function() return COLOR_CONTROL end) then
		COLOR_CONTROL = { R = 0.58, G = 0.05, B = 0 }
	end
end

local function build_map()
	local root = find("scale_group")
	local ground = picture("rfg_fullmap_ground", root, "rfg_ui_hud_base_square")
	vint_set_property(ground, "scale", SCREEN_W, SCREEN_H)
	vint_set_property(ground, "tint", 0, 0, 0)
	vint_set_property(ground, "depth", MAP_DEPTH + 1)

	-- Under every part of the document: Guerrilla drew its map in a pass of its own before the document
	-- (fullscreen_map_render), so its labels and scan lines (depth 15), the header's and hints' torn
	-- edges and the rest are all over it.
	Map = vint_object_create("rfg_fullmap", "group", root)
	vint_set_property(Map, "depth", MAP_DEPTH)

	World = picture("rfg_fullmap_world", Map, "rfg_terr01_map")
	vint_set_property(World, "depth", 10)
	for i = 1, TILES do
		Tile_handles[i] = vint_object_create("rfg_fullmap_tile" .. i, "bitmap", Map)
		vint_set_property(Tile_handles[i], "visible", false)
		vint_set_property(Tile_handles[i], "depth", 8)
	end

	Marker = picture("rfg_fullmap_player", Map, "rfg_ui_map_icon_player")
	vint_set_property(Marker, "auto_offset", "c")
	vint_set_property(Marker, "scale", MARKER_SCALE, MARKER_SCALE)
	vint_set_property(Marker, "depth", 0)

	Labels = find("map_label_screen_group")
	if rfg_map_icon_count == nil then
		return
	end
	for i = 1, rfg_map_icon_count() do
		local kind, image, x, y, caption, description, on_world_map = rfg_map_icon(i)
		if kind == "place" then
			Places[#Places + 1] = { x = x, y = y, name = caption }
		elseif kind == "player" then
			Player = { picture = image, caption = caption, description = description }
		elseif kind == "text" then
			Texts[caption] = description
		elseif on_world_map then
			local icon = picture("rfg_fullmap_icon" .. i, Map, image)
			vint_set_property(icon, "auto_offset", "c")
			vint_set_property(icon, "scale", ICON_SCALE, ICON_SCALE)
			vint_set_property(icon, "depth", 5)	-- over the map, under the player
			Icons[#Icons + 1] = { handle = icon, x = x, y = y, picture = image, caption = caption, description = description,
				index = i, bomb = kind == "bomb" }	-- a M.O.A.B. only once its radio tags are found
		end
	end
	log(#Icons .. " icons, " .. #Places .. " places")
end

-- Guerrilla's hints, as its game filled FS_Hints: (beacon, pan, zoom, back, center) a picture and its
-- words each, then whether each is greyed out. The pan hint shows W A S D: its picture and three more
-- beside it, made before Guerrilla's script lays the hints out by their widths.
local function show_hints()
	if not Hint_keys_made then
		Hint_keys_made = true
		local stick = find("left_stick")
		local x, y = vint_get_property(stick, "anchor")
		for i = 2, 4 do
			local key = vint_object_clone_rename(stick, "rfg_hint_pan_key" .. i)
			vint_set_property(key, "image", KEY_PICTURES.pan[i])
			vint_set_property(key, "anchor", x + (i - 1) * KEY_STEP, y)
		end
		local text = find("hint_1")
		local text_x, text_y = vint_get_property(text, "anchor")
		vint_set_property(text, "anchor", text_x + 3 * KEY_STEP, text_y)
		local wheel = find("right_stick")
		local wheel_x, wheel_y = vint_get_property(wheel, "anchor")
		vint_set_property(wheel, "anchor", wheel_x + WHEEL_EXTRA, wheel_y)
		text = find("hint_2")
		text_x, text_y = vint_get_property(text, "anchor")
		vint_set_property(text, "anchor", text_x + WHEEL_EXTRA, text_y)
	end
	rfg_ui_fs_update_hints({ "", Texts.beacon or "", KEY_PICTURES.pan[1], Texts.pan or "", KEY_PICTURES.zoom, Texts.zoom or "",
		KEY_PICTURES.back, Texts.back or "", KEY_PICTURES.center, Texts.center or "", true, false, false, false, false }, "update")
	-- No beacons yet: CENTER takes the hidden beacon hint's place (Guerrilla's layout put it after it).
	vint_set_property(find("hint_group5"), "anchor", vint_get_property(find("hint_group4"), "anchor"))
end

-- The highlight box: Guerrilla's FS_HighlightItem (active, title, description, icon, its angle, its
-- colour), for the icon nearest the crosshair, or nothing.
local function highlight(item)
	if item == Highlighted then
		return
	end
	Highlighted = item
	if item == nil then
		rfg_ui_fs_highlight_item({ false, "", "", "none", 0, 1, 1, 1 }, "update")
	else
		rfg_ui_fs_highlight_item({ true, item.caption, item.description, item.picture, 0, 1, 1, 1 }, "update")
	end
end

-- One frame while the map is open.
local function frame(map_x, map_y, heading, centre_x, centre_y, view, zoom_pct, district)
	local scale = SCREEN_H / view		-- screen units a world map pixel
	local function screen(x, y)
		return SCREEN_W / 2 + (x - centre_x) * scale, SCREEN_H / 2 + (y - centre_y) * scale
	end
	local function on_screen(x, y)
		return x > -ON_SCREEN_MARGIN and x < SCREEN_W + ON_SCREEN_MARGIN and y > -ON_SCREEN_MARGIN and y < SCREEN_H + ON_SCREEN_MARGIN
	end

	local left, top = screen(0, 0)
	vint_set_property(World, "scale", scale, scale)
	vint_set_property(World, "anchor", left, top)

	-- The zones' own pictures, once close.
	local count = rfg_map_full_tiles()
	local tile_scale = scale * ZONE_PIXELS / TILE_PIXELS
	for i = 1, TILES do
		local handle = Tile_handles[i]
		if i <= count then
			local column, row = rfg_map_full_tile(i)
			local name = two_digits(column) .. "_" .. two_digits(row)
			if Tile_shown[i] ~= name then
				vint_set_property(handle, "image", "rfg_terr01_" .. name .. "_map")
				Tile_shown[i] = name
			end
			vint_set_property(handle, "scale", tile_scale, tile_scale)
			vint_set_property(handle, "anchor", screen((column - 2) * ZONE_PIXELS, (row - 2) * ZONE_PIXELS))
			vint_set_property(handle, "visible", true)
		elseif Tile_shown[i] ~= nil then
			vint_set_property(handle, "visible", false)
			Tile_shown[i] = nil
		end
	end

	-- The icons, and the one nearest the crosshair.
	local nearest, nearest_distance = nil, HIGHLIGHT_REACH * HIGHLIGHT_REACH
	local function consider(item, x, y)
		local dx, dy = x - SCREEN_W / 2, y - SCREEN_H / 2
		if dx * dx + dy * dy < nearest_distance then
			nearest, nearest_distance = item, dx * dx + dy * dy
		end
	end
	for i, icon in ipairs(Icons) do
		local x, y = screen(icon.x, icon.y)
		local shown = on_screen(x, y) and (not icon.bomb or rfg_map_icon_shown(icon.index))
		if shown ~= icon.shown then
			vint_set_property(icon.handle, "visible", shown)
			icon.shown = shown
		end
		if shown then
			vint_set_property(icon.handle, "anchor", x, y)
			consider(icon, x, y)
		end
	end
	local player_x, player_y = screen(map_x, map_y)
	vint_set_property(Marker, "anchor", player_x, player_y)
	vint_set_property(Marker, "rotation", heading)
	consider(Player, player_x, player_y)
	highlight(nearest)

	-- Guerrilla's places' labels and the zoom readout (in the labels' group's own units).
	local labels_x, labels_y = vint_get_property(Labels, "anchor")
	local labels_scale = vint_get_property(Labels, "scale")
	for i, place in ipairs(Places) do
		local x, y = screen(place.x, place.y)
		local event = "update"
		if not Labels_inserted then
			event = "insert"
		end
		rfg_ui_fs_update_poi_group_item({ i, (x - labels_x) / labels_scale, (y - labels_y) / labels_scale, place.name, zoom_pct,
			on_screen(x, y) }, event)
	end
	Labels_inserted = true

	-- The district under the crosshair, as Guerrilla's FS_DistrictItem named it ("NONE" outside).
	if district ~= Shown_district then
		if district == "" then
			vint_set_property(find("header_text"), "text_tag", "NONE")
		else
			vint_set_property(find("header_text"), "text_tag", district)
		end
		Shown_district = district
	end

	Open_frames = Open_frames + 1
	if Open_frames == 2 then
		show_hints()	-- laid out by their sizes, which come with their drawing
	end
end

local function follow()
	while true do
		local active, map_x, map_y, heading = rfg_map_view()
		local open, centre_x, centre_y, view, zoom_pct, district = rfg_map_full()
		if open and Open_frames == 0 then
			local fade = find("fadein_tween")
			if fade ~= 0 then
				vint_set_property(fade, "start_time", vint_get_time_index(vint_document_find("rfg_ui_fullscreen")))
				vint_set_property(fade, "state", TWEEN_STATE_RUNNING)
			end
		end
		if open then
			local ok, problem = pcall(frame, map_x, map_y, heading, centre_x, centre_y, view, zoom_pct, district)
			if not ok then
				log("the world map stopped: " .. tostring(problem))
				return
			end
		elseif Open_frames > 0 then
			-- Closed: its zone pictures are forgotten, as they may go while it's closed (and a picture
			-- set before its texture went and came back draws what's no longer there: the 04:16 freeze).
			Open_frames = 0
			for i = 1, TILES do
				vint_set_property(Tile_handles[i], "visible", false)
				Tile_shown[i] = nil
			end
		end
		thread_yield()
	end
end

function rfg_ui_fullscreen_init()
	guerrilla_globals()
	local ok, problem = pcall(rfg_ui_fullscreen_guerrilla_init)
	if not ok then
		log("Guerrilla's init: " .. tostring(problem))
	end
	for i, name in ipairs(HIDDEN) do
		local handle = find(name)
		if handle ~= 0 then
			vint_set_property(handle, "visible", false)
		end
	end
	vint_set_child_tween_state(find("mission_active_anim"), TWEEN_STATE_DISABLED)
	if rfg_map_full == nil then
		vint_set_property(find("scale_group"), "visible", false)	-- an older loader or mod: see rfg_ui_hud.lua
		return
	end
	local built, why = pcall(build_map)
	if not built then
		log("the world map couldn't be built: " .. tostring(why))
		return
	end
	if Texts.salvage ~= nil then
		vint_set_property(find("salvage_label"), "text_tag", Texts.salvage)
	end
	vint_dataitem_add_subscription("salvage_hud", "update", "rfg_ui_fullscreen_salvage")
	Thread = thread_new("rfg_ui_fullscreen_follow")
end

function rfg_ui_fullscreen_follow()
	follow()
end

-- Armageddon's salvage_hud: total_amount, new_amount. Guerrilla's map showed the player's salvage.
function rfg_ui_fullscreen_salvage(di_h, event_type)
	local total = vint_dataitem_get(di_h)
	if total ~= nil then
		vint_set_property(find("salvage_value"), "text_tag", "" .. floor(total))
		vint_set_property(find("salvage_value_bg"), "text_tag", "" .. floor(total))
	end
end

function rfg_ui_fullscreen_cleanup()
	if Thread ~= -1 then
		thread_kill(Thread)
		Thread = -1
	end
	pcall(rfg_ui_fullscreen_guerrilla_cleanup)
end
