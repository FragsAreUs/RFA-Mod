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
--     activities' starts), a running activity's own icons (a Transporter's end point) and the player's
--     icon, turned the way the camera looks;
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
local Fog_element = false               -- the fog of war's element (guerrilla_hud draws the fog there), false for none
local Has_fog = false                   -- whether guerrilla_hud has rfg_map_fog
-- The convoy's route (cartographer::render_convoy_routes RM 0x870940: its arrow picture along the path, EDF red with a
-- lighter one moving along) and the nav beacon (render_player_marker 0x871B30: ui_map_icon_end_point, yellow, bobbing),
-- from the mod (rfg_map_route, rfg_map_route_point, rfg_map_beacon). The arrows a fixed set, made with the map.
local ROUTE_ARROWS = 96
local ROUTE_PICTURE = "rfg_ui_fsmap_icon_arrow"
local ROUTE_COLOUR = { 148, 13, 0 }
local ROUTE_LIT_COLOUR = { 252, 22, 0 }
local ROUTE_STEP = 12                   -- screen units from one arrow to the next
local ROUTE_SCALE = 0.3                 -- an arrow's size (its picture's)
local BEACON_PICTURE = "rfg_ui_map_icon_end_point"
local BEACON_COLOUR = { 250, 230, 0 }

-- The hints' keys: Guerrilla's own PC key pictures (its sheet ui_bms_pc_buttons, from interface-pc), each
-- 31 units square (the mouse wheel's 62 wide), anchored at their right edge as the document's pictures are.
local KEY_PICTURES = { pan = { "rfg_key_06", "rfg_key_48", "rfg_key_49", "rfg_key_50" }, zoom = "rfg_key_133",
	back = "rfg_key_124", center = "rfg_key_75" }
local KEY_STEP = 33                     -- from one of the pan hint's four keys to the next
local WHEEL_EXTRA = 31                  -- the wheel's picture is this much wider than a key's

local HIDDEN = { "control_meter", "morale_meter", "district_filter_main", "fullscreen_map", "hint_group4" }

local Map = 0                           -- our group: the map, over the black ground
local Route_group = 0                   -- in Map: the convoy route's arrows
local Icon_group = 0                    -- in Map: the map's icons and the campaign's
local World = 0
local Marker = 0
local Route_handles, Route_shown = {}, {}
-- Guerrilla's GPS route to the nav beacon (render_full RM 0x881B30: its arrow picture at each point, turned toward the
-- next, coloured by the mod: rfg_map_gps, rfg_map_gps_point), in a group of its own over the convoy's route. An arrow
-- Guerrilla's 18 x 10 (its map icons' 26), here by our icons' size.
local GPS_ARROWS = 128
local GPS_LENGTH, GPS_WIDTH = 13.8, 7.7	-- screen units
local Gps_group = 0
local Gps_handles, Gps_shown = {}, {}
local Beacon = 0
local Beacon_shown = false
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
	-- Guerrilla's fog of war over the parts of the world not yet seen (fog_of_war_render_fog_layer): drawn by guerrilla_hud
	-- itself where this element comes, over the world and zone pictures, under the route, the icons and the player.
	Has_fog = _GetAnyGlobalSilent("rfg_map_fog") ~= nil
	local fog = Has_fog and vint_object_create("rfg_fullmap_fog", "map", Map) or 0
	if fog ~= nil and fog ~= 0 then
		Fog_element = fog
		vint_set_property(Fog_element, "depth", 7)
	end

	-- Armageddon's interface draws a document through one list of 256 elements (Process_stack 0x28C3B18, given at
	-- 0xC51697; vint_element_base::render takes a group's children into it, its children's children after them, and
	-- leaves out those past its end), as Guerrilla's did (RM 0x47C9B2). The route's arrows and the icons are kept in
	-- groups of their own so that Map holds about 20: all in Map (260 with step 8's arrows) left out the world picture
	-- and the nearest zone pictures (2026-11-26: black when zoomed out, black squares in the middle when zoomed in).
	Icon_group = vint_object_create("rfg_fullmap_icons", "group", Map)
	vint_set_property(Icon_group, "depth", 5)		-- over the map, under the player
	if rfg_map_route ~= nil then
		Route_group = vint_object_create("rfg_fullmap_route", "group", Map)
		vint_set_property(Route_group, "depth", 6)	-- over the map, under the icons
		for i = 1, ROUTE_ARROWS do
			Route_handles[i] = picture("rfg_fullmap_route" .. i, Route_group, ROUTE_PICTURE)
			vint_set_property(Route_handles[i], "visible", false)
			vint_set_property(Route_handles[i], "auto_offset", "c")
			vint_set_property(Route_handles[i], "scale", ROUTE_SCALE, ROUTE_SCALE)
		end
		Gps_group = vint_object_create("rfg_fullmap_gps", "group", Map)
		vint_set_property(Gps_group, "depth", 6)	-- with the convoy's route: over the map and the fog, under the icons
		for i = 1, GPS_ARROWS do
			Gps_handles[i] = picture("rfg_fullmap_gps" .. i, Gps_group, ROUTE_PICTURE)
			vint_set_property(Gps_handles[i], "visible", false)
			vint_set_property(Gps_handles[i], "auto_offset", "c")
		end
		Beacon = picture("rfg_fullmap_beacon", Map, BEACON_PICTURE)
		vint_set_property(Beacon, "visible", false)
		vint_set_property(Beacon, "auto_offset", "c")
		vint_set_property(Beacon, "tint", BEACON_COLOUR[1], BEACON_COLOUR[2], BEACON_COLOUR[3])
		vint_set_property(Beacon, "depth", 4)	-- over the icons, under the player
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
			local icon = picture("rfg_fullmap_icon" .. i, Icon_group, image)
			vint_set_property(icon, "auto_offset", "c")
			vint_set_property(icon, "scale", ICON_SCALE, ICON_SCALE)
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
-- colour), for the icon nearest the crosshair, or nothing. The box's own picture (map_icon) shows the
-- item at the crosshair, so Guerrilla's cartographer leaves the item itself out while it's highlighted
-- (render_player RM 0x8723A0 and the icons' renders: is_old_highlight_item, then no draw), and sends the
-- player's heading as the angle (check_highlight_for_item 0x862EC0).
local Highlight_angle = 0

local function item_picture(item)
	if item == Player then
		return Marker
	end
	return item.handle
end

local function highlight(item, angle)
	if item == Highlighted and (item ~= Player or angle == Highlight_angle) then
		return
	end
	if item ~= Highlighted then
		if Highlighted ~= nil and item_picture(Highlighted) ~= nil then
			vint_set_property(item_picture(Highlighted), "visible", Highlighted == Player or Highlighted.shown == true)
		end
		if item ~= nil and item_picture(item) ~= nil then
			vint_set_property(item_picture(item), "visible", false)
		end
	end
	Highlighted = item
	Highlight_angle = angle
	if item == nil then
		rfg_ui_fs_highlight_item({ false, "", "", "none", 0, 1, 1, 1 }, "update")
	else
		rfg_ui_fs_highlight_item({ true, item.caption, item.description, item.picture, angle, 1, 1, 1 }, "update")
	end
end

-- The campaign's icons Guerrilla's world map drew too (its activities' temporary icons whose row is VisibleOnFSMap: a
-- running Transporter's end point): the map markers whose last value (rfg_map_marker's ninth) says so, tinted as on
-- the minimap, at Guerrilla's map icon size.
local CAMPAIGN_ICONS = 16				-- a Transporter's end point, a convoy's members
local Campaign_icons = {}               -- { handle, image }, made as they're needed

local function place_campaign_icons(screen, on_screen)
	local used = 0
	if rfg_map_markers ~= nil then
		for i = 1, rfg_map_markers() do
			local mx, my, image, placement, r, g, b, age, on_world_map = rfg_map_marker(i)
			if on_world_map == true and used < CAMPAIGN_ICONS then
				local x, y = screen(mx, my)
				if on_screen(x, y) then
					used = used + 1
					local icon = Campaign_icons[used]
					if icon == nil then
						icon = { handle = picture("rfg_fullmap_campaign" .. used, Icon_group, image), image = image }
						vint_set_property(icon.handle, "auto_offset", "c")
						vint_set_property(icon.handle, "scale", ICON_SCALE, ICON_SCALE)
						Campaign_icons[used] = icon
					elseif icon.image ~= image then
						vint_set_property(icon.handle, "image", image)
						icon.image = image
					end
					if r ~= nil and r >= 0 then
						vint_set_property(icon.handle, "tint", r, g, b)
					else
						vint_set_property(icon.handle, "tint", 255, 255, 255)
					end
					vint_set_property(icon.handle, "anchor", x, y)
					vint_set_property(icon.handle, "visible", true)
				end
			end
		end
	end
	for i = used + 1, #Campaign_icons do
		vint_set_property(Campaign_icons[i].handle, "visible", false)
	end
end

-- The convoy's route and the nav beacon, on the map shown.
local function place_route(screen, on_screen, scale, centre_x, centre_y)
	local used = 0
	local reach = (SCREEN_W + SCREEN_H) / scale		-- map pixels: past the screen's corners
	local count = rfg_map_route(ROUTE_STEP / scale, centre_x, centre_y, reach)
	for i = 1, count do
		if used >= ROUTE_ARROWS then
			break
		end
		local mx, my, angle, lit = rfg_map_route_point(i)
		if mx ~= nil then
			local x, y = screen(mx, my)
			if on_screen(x, y) then
				used = used + 1
				local handle = Route_handles[used]
				vint_set_property(handle, "anchor", x, y)
				vint_set_property(handle, "rotation", angle)
				local colour = ROUTE_COLOUR
				if lit then
					colour = ROUTE_LIT_COLOUR
				end
				vint_set_property(handle, "tint", colour[1], colour[2], colour[3])
				if not Route_shown[used] then
					vint_set_property(handle, "visible", true)
					Route_shown[used] = true
				end
			end
		end
	end
	for i = used + 1, ROUTE_ARROWS do
		if Route_shown[i] then
			vint_set_property(Route_handles[i], "visible", false)
			Route_shown[i] = nil
		end
	end
	-- The GPS route's arrows.
	local gps_used = 0
	if Gps_group ~= 0 then
		local w, h = 0, 0
		for i = 1, rfg_map_gps(true) do
			if gps_used >= GPS_ARROWS then
				break
			end
			local mx, my, angle, r, g, b = rfg_map_gps_point(i)
			if mx ~= nil then
				local x, y = screen(mx, my)
				if on_screen(x, y) then
					gps_used = gps_used + 1
					local handle = Gps_handles[gps_used]
					if w == 0 then
						local ok, pw, ph = pcall(vint_get_property, handle, "unscaled_size")
						w, h = 40, 40
						if ok and pw ~= nil and pw > 0 and ph ~= nil and ph > 0 then
							w, h = pw, ph
						end
					end
					vint_set_property(handle, "anchor", x, y)
					vint_set_property(handle, "rotation", angle)
					vint_set_property(handle, "scale", GPS_LENGTH / w, GPS_WIDTH / h)
					vint_set_property(handle, "tint", r, g, b)
					if not Gps_shown[gps_used] then
						vint_set_property(handle, "visible", true)
						Gps_shown[gps_used] = true
					end
				end
			end
		end
	end
	for i = gps_used + 1, GPS_ARROWS do
		if Gps_shown[i] then
			vint_set_property(Gps_handles[i], "visible", false)
			Gps_shown[i] = nil
		end
	end
	local shown, bx, by, size = rfg_map_beacon()
	local x, y = 0, 0
	if shown then
		x, y = screen(bx, by)
		shown = on_screen(x, y)
	end
	if shown then
		vint_set_property(Beacon, "anchor", x, y)
		vint_set_property(Beacon, "scale", ICON_SCALE * (size or 1), ICON_SCALE * (size or 1))
	end
	if shown ~= Beacon_shown then
		vint_set_property(Beacon, "visible", shown)
		Beacon_shown = shown
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
	if Fog_element and Has_fog then rfg_map_fog(Fog_element, centre_x, centre_y, view, zoom_pct) end

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
	place_campaign_icons(screen, on_screen)
	if Beacon ~= 0 then
		local ok, problem = pcall(place_route, screen, on_screen, scale, centre_x, centre_y)
		if not ok then
			Beacon = 0
			log("the convoy's route and the nav beacon stopped: " .. tostring(problem))
		end
	end
	local player_x, player_y = screen(map_x, map_y)
	vint_set_property(Marker, "anchor", player_x, player_y)
	vint_set_property(Marker, "rotation", heading)
	consider(Player, player_x, player_y)
	highlight(nearest, nearest == Player and heading or 0)

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
