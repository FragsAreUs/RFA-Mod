-- Override of vdo_button_highlight.lua from <game>\build\pc\cache\interface.vpp_pc (the highlight bar of every
-- menu list in the game). rfa_loader.dll gives the game this file instead of its own copy.
--
-- It also holds RFA-Mod's Modern style for the whole interface (Options > Main Menu > Style and Accent), since
-- every menu loads it: rfa_theme_apply sets the game's shared menu colours (COLOR_HIGHLIGHT_BAR and the rest, in
-- rfg_ui_globals.lua, 0 to 255; every list, toggle, slider and title bar reads them as it draws) to the accent,
-- and rfa_theme_screen restyles a menu screen (the main menu's, the pause menu's: they share their layout).
-- Every line we added or changed is marked [RFA]; everything else is the game's original script. Only the [RFA]
-- lines are this project's (GNU GPL version 3 or later, see LICENSE); the rest belongs to the game's owners. To
-- compare with the original, extract it with the editor.

-- [RFA] ---- The Modern style --------------------------------------------------------------------------------
-- [RFA] The accents, in Options > Main Menu > Accent's order (0 to 255, as the game's colours are).
RFA_THEME_ACCENTS = { -- [RFA]
	{R = 77, G = 184, B = 255}, -- [RFA] blue
	{R = 255, G = 140, B = 38}, -- [RFA] orange
	{R = 235, G = 56, B = 56}, -- [RFA] red
	{R = 89, G = 230, B = 140}, -- [RFA] green
	{R = 204, G = 115, B = 255}, -- [RFA] violet
	{R = 245, G = 245, B = 245}, -- [RFA] white
} -- [RFA]
RFA_THEME_PACK = "rfa_ui_modern" -- [RFA] the UI pack's document (its pictures; the editor makes it)

local function rfa_theme_set(color, r, g, b) -- [RFA] a shared colour, the game's kept the first time
	if color == nil then return end -- [RFA]
	if color.rfa_game == nil then color.rfa_game = {R = color.R, G = color.G, B = color.B} end -- [RFA]
	color.R, color.G, color.B = r, g, b -- [RFA]
end -- [RFA]

-- [RFA] Is the Modern style on? (Set by rfa_theme_apply, on the game's own colour table so every menu sees it.)
function rfa_theme_modern() -- [RFA]
	return COLOR_HIGHLIGHT_BAR ~= nil and COLOR_HIGHLIGHT_BAR.rfa_modern == true -- [RFA]
end -- [RFA]

-- [RFA] The accent now (a {R, G, B} of 0 to 255).
function rfa_theme_accent() -- [RFA]
	local get = _GetAnyGlobalSilent("rfa_get_setting") -- [RFA] (the loader's)
	local index = (get ~= nil) and get("MainMenu", "Accent", 0) or 0 -- [RFA]
	return RFA_THEME_ACCENTS[index + 1] or RFA_THEME_ACCENTS[1] -- [RFA]
end -- [RFA]

-- [RFA] Sets the shared colours from the settings (Modern: the accent; Classic: the game's own). Called as the
-- [RFA] main menu opens and when the style or accent is changed; what is drawn after takes them.
function rfa_theme_apply() -- [RFA]
	local get = _GetAnyGlobalSilent("rfa_get_setting") -- [RFA]
	local modern = get ~= nil and get("MainMenu", "Style", 0) == 1 -- [RFA]
	local accent = rfa_theme_accent() -- [RFA]
	if modern then -- [RFA]
		rfa_theme_set(COLOR_HIGHLIGHT_BAR, accent.R, accent.G, accent.B) -- [RFA]
		rfa_theme_set(COLOR_HIGHLIGHT_BG, accent.R * 0.45, accent.G * 0.45, accent.B * 0.45) -- [RFA]
		rfa_theme_set(COLOR_HIGHLIGHT_TEXT, 245, 245, 245) -- [RFA]
		rfa_theme_set(COLOR_UNHIGHLIGHT_TEXT, 185, 188, 194) -- [RFA]
	else -- [RFA]
		for i, color in ipairs({COLOR_HIGHLIGHT_BAR, COLOR_HIGHLIGHT_BG, COLOR_HIGHLIGHT_TEXT, COLOR_UNHIGHLIGHT_TEXT}) do -- [RFA]
			if color ~= nil and color.rfa_game ~= nil then -- [RFA]
				color.R, color.G, color.B = color.rfa_game.R, color.rfa_game.G, color.rfa_game.B -- [RFA]
			end -- [RFA]
		end -- [RFA]
	end -- [RFA]
	if COLOR_HIGHLIGHT_BAR ~= nil then COLOR_HIGHLIGHT_BAR.rfa_modern = modern end -- [RFA]
	return modern -- [RFA]
end -- [RFA]

-- [RFA] A highlight bar in the Modern style: the UI pack's pictures (a bar down its left, a light band, the right end
-- [RFA] fading), white and coloured by the accent. Without the pack loaded (not made yet) it keeps the game's.
function rfa_theme_highlight(highlight) -- [RFA]
	if highlight == nil or highlight.left == nil or not rfa_theme_modern() then return end -- [RFA]
	if vint_document_find(RFA_THEME_PACK) ~= 0 then -- [RFA]
		highlight.left:set_property("image", "rfa_modern_hl_left") -- [RFA]
		highlight.center:set_property("image", "rfa_modern_hl_center") -- [RFA]
		highlight.right:set_property("image", "rfa_modern_hl_right") -- [RFA]
	end -- [RFA]
	for i, part in ipairs({highlight.left, highlight.center, highlight.right}) do -- [RFA]
		part:set_color(COLOR_HIGHLIGHT_BAR.R, COLOR_HIGHLIGHT_BAR.G, COLOR_HIGHLIGHT_BAR.B) -- [RFA]
	end -- [RFA]
end -- [RFA]

-- [RFA] ---- The Modern style's layout and motion --------------------------------------------------------------
-- [RFA] A Modern menu screen (main menu, pause menu) is restyled once by rfa_theme_screen and then kept moving by
-- [RFA] one thread of its own (rfa_theme_screen_thread), which does everything that moves:
-- [RFA]   - the panel behind the list is a dark glass pane (the screen's own mask, which darkens what's behind it),
-- [RFA]     and it unfolds upward as the menu opens and glides to its new size when the menu changes tier;
-- [RFA]   - an accent rail runs down the panel's left edge and a thin accent line along its top, both following the
-- [RFA]     panel, the rail breathing gently;
-- [RFA]   - the entries come in one after another (a fade and a short slide from the left) each time a list is drawn;
-- [RFA]   - the highlight glides from entry to entry instead of jumping, and breathes with the rail.
-- [RFA] Only the thread changes what moves, so if it ever stops, everything is simply left where the game put it.
-- [RFA] Back in the Classic style the thread puts back what it changed and ends.
local RFA_SPACING = 40 -- [RFA] between a list's entries (the game's: 34)
local RFA_PANEL_TINT = 82 -- [RFA] how much of the picture shows through the panel (of 255; it darkens by multiplying)
local RFA_HIDE = {"techno_bits", "right_dags", "right_dags_1", "right_dags_1_1", "right_dags_1_2", "tech_corner_1",
	"tech_meter", "tech_module", "scrolling_text1", "scrolling_text2", "scrolling_text3",
	"techno_text1", "techno_text2", "tech_corner", "ruin_tech"} -- [RFA] the game's decorations (both screens)
local RFA_PANEL_TWEENS = {"menu_dag_group_alpha_twn_1", "menu_dag_group_alpha_twn_2", "menu_dag_group_alpha_twn_3",
	"menu_dag_group_alpha_twn_4", "menu_dag_group_alpha_twn_5"} -- [RFA] the panel's fades (made full: see the tint)

local function rfa_abs(v) if v < 0 then return -v end return v end -- [RFA] (no math library here)
local function rfa_clamp01(v) if v < 0 then return 0 elseif v > 1 then return 1 end return v end -- [RFA]
local function rfa_ease_out(t) -- [RFA] cubic: quick, then settling
	t = rfa_clamp01(t) -- [RFA]
	local u = 1 - t -- [RFA]
	return 1 - u * u * u -- [RFA]
end -- [RFA]
local function rfa_wave(clock, period) -- [RFA] 0 to 1 and back, smoothly, every period seconds
	local p = (clock / period) % 1 -- [RFA]
	local t = (p < 0.5) and (p * 2) or ((1 - p) * 2) -- [RFA]
	return t * t * (3 - 2 * t) -- [RFA]
end -- [RFA]

-- [RFA] The screens' states (kept in _UGGlobals: this file's own globals are made again each time a menu loads it).
local function rfa_states() -- [RFA]
	local states = rawget(_UGGlobals, "Rfa_theme_states") -- [RFA]
	if states == nil then -- [RFA]
		states = {next = 1} -- [RFA]
		rawset(_UGGlobals, "Rfa_theme_states", states) -- [RFA]
	end -- [RFA]
	return states -- [RFA]
end -- [RFA]

-- [RFA] A menu list (Vdo_menu_list) in the Modern style, its buttons too (each draw makes them again): sharp (no
-- [RFA] blur), the highlighted entry in the accent without its glow, the buttons without their boxes and the
-- [RFA] little text beside them, a little more room between them.
function rfa_theme_list(list, fresh) -- [RFA] fresh: just drawn (its entries are spaced out again)
	if list == nil or list.handle == nil or not rfa_theme_modern() then return end -- [RFA]
	local group = Vdo_base_object:new("list_group", list.handle) -- [RFA]
	group:set_property("blur", 0) -- [RFA]
	group:set_property("alpha", 0.9) -- [RFA] (the game's 0.6: the panel behind is dark now)
	Vdo_base_object:new("menu_list_button_text_2", list.handle):set_property("alpha", 0) -- [RFA] the glow
	local text = Vdo_base_object:new("menu_list_button_text", list.handle) -- [RFA] the highlighted entry's words
	text:set_color(COLOR_HIGHLIGHT_TEXT.R, COLOR_HIGHLIGHT_TEXT.G, COLOR_HIGHLIGHT_TEXT.B) -- [RFA]
	text:set_property("text_scale", 0.9, 0.9) -- [RFA] (the game's 1.1: it ran past the panel)
	Vdo_base_object:new("button_highlight", list.handle):set_color(255, 255, 255) -- [RFA] (its parts carry the accent)
	local data = list.list_data -- [RFA]
	if data ~= nil and data.buttons ~= nil then -- [RFA]
		local n = data.num_buttons or #data.buttons -- [RFA]
		local bottom_x, bottom_y = nil, nil -- [RFA]
		if fresh and data.buttons[n] ~= nil then bottom_x, bottom_y = data.buttons[n]:get_property("anchor") end -- [RFA]
		for i, button in pairs(data.buttons) do -- [RFA]
			Vdo_base_object:new("background", button.handle):set_property("alpha", 0) -- [RFA]
			Vdo_base_object:new("techno_text", button.handle):set_property("alpha", 0) -- [RFA]
			if bottom_y ~= nil then button:set_property("anchor", bottom_x, bottom_y - RFA_SPACING * (n - i)) end -- [RFA]
		end -- [RFA]
		if fresh then list.rfa_drawn = true end -- [RFA] (the thread brings the entries in)
		if fresh and data.current_idx ~= nil and list.move_cursor ~= nil then list:move_cursor(0) end -- [RFA] the highlight, to the new places
	end -- [RFA]
	rfa_theme_highlight(list.highlight) -- [RFA]
end -- [RFA]

-- [RFA] Wraps a list's draw (restyle after it) and cursor (the highlight glides), once.
local function rfa_theme_wrap(list) -- [RFA]
	if list.rfa_themed ~= nil then return end -- [RFA]
	list.rfa_themed = true -- [RFA]
	local draw_items, move_cursor = list.draw_items, list.move_cursor -- [RFA]
	list.draw_items = function(self, ...) -- [RFA]
		self.rfa_glide = nil -- [RFA]
		local r = draw_items(self, ...) -- [RFA]
		rfa_theme_list(self, true) -- [RFA]
		return r -- [RFA]
	end -- [RFA]
	list.move_cursor = function(self, direction, ...) -- [RFA]
		local group = Vdo_base_object:new("highlight_group", self.handle) -- [RFA]
		local from_x, from_y = group:get_property("anchor") -- [RFA]
		local r = move_cursor(self, direction, ...) -- [RFA]
		if direction ~= nil and direction ~= 0 and from_y ~= nil and rfa_theme_modern() then -- [RFA]
			local to_x, to_y = group:get_property("anchor") -- [RFA]
			self.rfa_glide = {handle = group.handle, x = to_x, from = from_y, to = to_y} -- [RFA]
		else -- [RFA]
			self.rfa_glide = nil -- [RFA]
		end -- [RFA]
		return r -- [RFA]
	end -- [RFA]
end -- [RFA]

-- [RFA] A menu screen in the Modern style (the main menu's and the pause menu's layout: their names are the same),
-- [RFA] in the document that calls it. Nothing in Classic. Called again (the UI pack loaded), it only refreshes.
function rfa_theme_screen(lists) -- [RFA]
	if not rfa_theme_modern() then return end -- [RFA]
	local first = lists[1] -- [RFA]
	local pack = vint_document_find(RFA_THEME_PACK) ~= 0 -- [RFA]
	local state = (first ~= nil) and first.rfa_screen or nil -- [RFA]
	if state == nil then -- [RFA]
		state = {lists = lists, hidden = {}, tweens = {}, tints = {}, clock = 0, track = {}} -- [RFA]
		for i, name in ipairs(RFA_HIDE) do -- [RFA]
			local object = vint_object_find(name) -- [RFA]
			if object ~= 0 and vint_get_property(object, "visible") ~= false then -- [RFA]
				vint_set_property(object, "visible", false) -- [RFA]
				state.hidden[#state.hidden + 1] = object -- [RFA]
			end -- [RFA]
		end -- [RFA]
		-- [RFA] The panel: the screen's mask (it multiplies), tinted dark and faded in fully.
		state.group = vint_object_find("menu_dag_group") -- [RFA]
		state.dag = vint_object_find("dag_mask") -- [RFA]
		state.bg1 = vint_object_find("mask_bg1") -- [RFA]
		state.bg2 = vint_object_find("mask_bg2") -- [RFA]
		state.corner = vint_object_find("mask_corner") -- [RFA]
		for i, object in ipairs({state.bg1, state.bg2, state.corner}) do -- [RFA]
			if object ~= 0 then -- [RFA]
				local r, g, b = vint_get_property(object, "tint") -- [RFA]
				state.tints[#state.tints + 1] = {object, r, g, b} -- [RFA]
				vint_set_property(object, "tint", RFA_PANEL_TINT, RFA_PANEL_TINT, RFA_PANEL_TINT + 6) -- [RFA]
			end -- [RFA]
		end -- [RFA]
		for i, name in ipairs(RFA_PANEL_TWEENS) do -- [RFA]
			local tween = vint_object_find(name) -- [RFA]
			if tween ~= 0 then -- [RFA]
				local from, to = vint_get_property(tween, "start_value"), vint_get_property(tween, "end_value") -- [RFA]
				if type(from) == "number" and type(to) == "number" then -- [RFA]
					state.tweens[#state.tweens + 1] = {tween, from, to} -- [RFA]
					if from > 0.001 then vint_set_property(tween, "start_value", 1) end -- [RFA]
					if to > 0.001 then vint_set_property(tween, "end_value", 1) end -- [RFA]
				end -- [RFA]
			end -- [RFA]
		end -- [RFA]
		if state.group ~= 0 then -- [RFA]
			state.group_alpha = vint_get_property(state.group, "alpha") -- [RFA]
			if type(state.group_alpha) == "number" and state.group_alpha > 0.001 then vint_set_property(state.group, "alpha", 1) end -- [RFA]
		end -- [RFA]
		-- [RFA] New elements: the rail and the top line (copies of the mask's square, outside the mask: they glow).
		if state.bg1 ~= 0 and state.dag ~= 0 then -- [RFA]
			local parent = vint_object_find("menu_dag_main") -- [RFA] (the mask's parent)
			if parent ~= 0 then -- [RFA]
				state.rail = vint_object_clone(state.bg1, parent) -- [RFA]
				state.line = vint_object_clone(state.bg1, parent) -- [RFA]
				state.glow = vint_object_clone(state.bg1, parent) -- [RFA] a soft band behind the rail
			end -- [RFA]
		end -- [RFA]
		if first ~= nil then first.rfa_screen = state end -- [RFA]
		for i, list in ipairs(lists) do -- [RFA]
			if list ~= nil then -- [RFA]
				rfa_theme_wrap(list) -- [RFA]
				rfa_theme_list(list, true) -- [RFA]
			end -- [RFA]
		end -- [RFA]
		local states = rfa_states() -- [RFA]
		state.id = states.next -- [RFA]
		states.next = states.next + 1 -- [RFA]
		states[state.id] = state -- [RFA]
		thread_new("rfa_theme_screen_thread", state.id) -- [RFA]
	else -- [RFA]
		for i, list in ipairs(lists) do rfa_theme_list(list) end -- [RFA]
	end -- [RFA]
	state.pack = pack -- [RFA]
	if pack and state.corner ~= nil and state.corner ~= 0 then -- [RFA]
		vint_set_property(state.corner, "image", "rfa_modern_corner") -- [RFA]
	end -- [RFA]
end -- [RFA]

-- [RFA] Puts back what the Modern style changed on a screen (back to Classic, or the thread failed).
local function rfa_theme_screen_undo(state) -- [RFA]
	for i, object in ipairs(state.hidden) do pcall(vint_set_property, object, "visible", true) end -- [RFA]
	for i, t in ipairs(state.tints) do pcall(vint_set_property, t[1], "tint", t[2], t[3], t[4]) end -- [RFA]
	for i, t in ipairs(state.tweens) do -- [RFA]
		pcall(vint_set_property, t[1], "start_value", t[2]) -- [RFA]
		pcall(vint_set_property, t[1], "end_value", t[3]) -- [RFA]
	end -- [RFA]
	if type(state.group_alpha) == "number" and state.group ~= 0 then pcall(vint_set_property, state.group, "alpha", state.group_alpha) end -- [RFA]
	for i, object in ipairs({state.rail, state.line, state.glow}) do -- [RFA]
		if object ~= nil and object ~= 0 then pcall(vint_object_destroy, object) end -- [RFA]
	end -- [RFA]
	state.hidden, state.tints, state.tweens = {}, {}, {} -- [RFA]
	state.rail, state.line, state.glow = nil, nil, nil -- [RFA]
end -- [RFA]

-- [RFA] Leaves every moving thing where the game wants it (the thread is ending).
local function rfa_theme_screen_settle(state) -- [RFA]
	for key, t in pairs(state.track) do -- [RFA]
		if t.tx ~= nil then pcall(vint_set_property, t.handle, t.prop, t.tx, t.ty) end -- [RFA]
	end -- [RFA]
	for i, list in ipairs(state.lists) do -- [RFA]
		if list ~= nil then -- [RFA]
			local g = list.rfa_glide -- [RFA]
			if g ~= nil then pcall(vint_set_property, g.handle, "anchor", g.x, g.to) end -- [RFA]
			list.rfa_glide = nil -- [RFA]
			local h = list.highlight -- [RFA]
			if h ~= nil and h.left ~= nil then -- [RFA]
				for j, part in ipairs({h.left, h.center, h.right}) do pcall(vint_set_property, part.handle, "alpha", 1) end -- [RFA]
			end -- [RFA]
			list.rfa_t0 = nil -- [RFA]
			if list.list_data ~= nil and list.list_data.buttons ~= nil then -- [RFA]
				for j, button in pairs(list.list_data.buttons) do -- [RFA]
					pcall(vint_set_property, button.handle, "alpha", 1) -- [RFA]
					local base = list.rfa_base and list.rfa_base[button.handle] -- [RFA]
					if base ~= nil then pcall(vint_set_property, button.handle, "anchor", base[1], base[2]) end -- [RFA]
				end -- [RFA]
			end -- [RFA]
		end -- [RFA]
	end -- [RFA]
end -- [RFA]

-- [RFA] A property the game sets now and then (the panel's size), eased toward each new value it is given.
local function rfa_track(state, handle, prop, start_y_zero, k) -- [RFA]
	if handle == nil or handle == 0 then return nil end -- [RFA]
	local key = handle .. prop -- [RFA]
	local t = state.track[key] -- [RFA]
	local x, y = vint_get_property(handle, prop) -- [RFA]
	if x == nil or y == nil then return nil end -- [RFA]
	if t == nil then -- [RFA] first seen: unfold from nothing
		t = {handle = handle, prop = prop, cx = x, cy = start_y_zero and 0 or y, tx = x, ty = y, wx = nil} -- [RFA]
		state.track[key] = t -- [RFA]
	elseif t.wx == nil or rfa_abs(x - t.wx) > 0.5 or rfa_abs(y - t.wy) > 0.5 then -- [RFA] the game set it anew
		t.tx, t.ty = x, y -- [RFA]
	end -- [RFA]
	t.cx = t.cx + (t.tx - t.cx) * k -- [RFA]
	t.cy = t.cy + (t.ty - t.cy) * k -- [RFA]
	if rfa_abs(t.tx - t.cx) < 0.25 then t.cx = t.tx end -- [RFA]
	if rfa_abs(t.ty - t.cy) < 0.25 then t.cy = t.ty end -- [RFA]
	vint_set_property(handle, prop, t.cx, t.cy) -- [RFA]
	t.wx, t.wy = vint_get_property(handle, prop) -- [RFA] (as it reads back)
	return t -- [RFA]
end -- [RFA]

-- [RFA] One frame of a screen's motion.
local function rfa_theme_screen_frame(state, dt) -- [RFA]
	state.clock = state.clock + dt -- [RFA]
	local clock = state.clock -- [RFA]
	local k = rfa_clamp01(dt * 11) -- [RFA] (the panel's easing)
	local wave = rfa_wave(clock, 2.2) -- [RFA]
	local accent = COLOR_HIGHLIGHT_BAR -- [RFA]

	-- [RFA] The panel, and the rail and line on it.
	local bg1 = rfa_track(state, state.bg1, "scale", true, k) -- [RFA]
	rfa_track(state, state.bg2, "scale", true, k) -- [RFA]
	local corner = rfa_track(state, state.corner, "anchor", true, k) -- [RFA]
	if bg1 ~= nil and state.rail ~= nil and state.rail ~= 0 and state.line ~= 0 and state.glow ~= 0 then -- [RFA]
		local dx, dy = vint_get_property(state.dag, "anchor") -- [RFA]
		local height = -bg1.cy -- [RFA]
		local width = bg1.cx -- [RFA]
		if corner ~= nil then width = corner.cx end -- [RFA] (the line stops where the corner begins)
		if height < 0 then height = 0 end -- [RFA]
		vint_set_property(state.rail, "anchor", dx - 6, dy - height) -- [RFA] (drawn downward: a flipped one doesn't show)
		vint_set_property(state.rail, "scale", 3, height) -- [RFA]
		vint_set_property(state.rail, "tint", accent.R, accent.G, accent.B) -- [RFA]
		vint_set_property(state.rail, "alpha", 0.75 + 0.25 * wave) -- [RFA]
		vint_set_property(state.glow, "anchor", dx - 12, dy - height) -- [RFA]
		vint_set_property(state.glow, "scale", 15, height) -- [RFA]
		vint_set_property(state.glow, "tint", accent.R, accent.G, accent.B) -- [RFA]
		vint_set_property(state.glow, "alpha", 0.06 + 0.08 * wave) -- [RFA]
		local grown = rfa_ease_out(clock / 0.6) -- [RFA] the line draws itself across as the menu opens
		vint_set_property(state.line, "anchor", dx, dy - height) -- [RFA]
		vint_set_property(state.line, "scale", width * grown, 2) -- [RFA]
		vint_set_property(state.line, "tint", accent.R, accent.G, accent.B) -- [RFA]
		vint_set_property(state.line, "alpha", 0.55) -- [RFA]
	end -- [RFA]

	-- [RFA] The lists: entries coming in, the highlight gliding and breathing.
	for i, list in ipairs(state.lists) do -- [RFA]
		local data = list.list_data -- [RFA]
		if data ~= nil and data.buttons ~= nil then -- [RFA]
			if list.rfa_drawn then -- [RFA] a new draw: its entries come in from here
				list.rfa_drawn = nil -- [RFA]
				list.rfa_t0 = clock -- [RFA]
				list.rfa_base = {} -- [RFA]
				for j, button in pairs(data.buttons) do -- [RFA]
					local x, y = button:get_property("anchor") -- [RFA]
					list.rfa_base[button.handle] = {x, y} -- [RFA]
				end -- [RFA]
			end -- [RFA]
			if list.rfa_t0 ~= nil then -- [RFA]
				local n = data.num_buttons or #data.buttons -- [RFA]
				local done = true -- [RFA]
				for j = 1, n do -- [RFA]
					local button = data.buttons[j] -- [RFA]
					local base = button and list.rfa_base[button.handle] -- [RFA]
					if base ~= nil then -- [RFA]
						local e = rfa_ease_out((clock - list.rfa_t0 - (j - 1) * 0.035) / 0.24) -- [RFA]
						if e < 1 then done = false end -- [RFA]
						button:set_property("alpha", e) -- [RFA]
						button:set_property("anchor", base[1] - 22 * (1 - e), base[2]) -- [RFA]
					end -- [RFA]
				end -- [RFA]
				if done then list.rfa_t0 = nil end -- [RFA]
			end -- [RFA]
			local glide = list.rfa_glide -- [RFA]
			if glide ~= nil then -- [RFA]
				glide.t0 = glide.t0 or clock -- [RFA]
				local e = rfa_ease_out((clock - glide.t0) / 0.16) -- [RFA]
				vint_set_property(glide.handle, "anchor", glide.x, glide.from + (glide.to - glide.from) * e) -- [RFA]
				if e >= 1 then list.rfa_glide = nil end -- [RFA]
			end -- [RFA]
			local h = list.highlight -- [RFA]
			if h ~= nil and h.left ~= nil then -- [RFA]
				if state.pack then -- [RFA] the pack's bar
					local a = 0.8 + 0.2 * wave -- [RFA]
					h.left:set_property("alpha", a) -- [RFA]
					h.center:set_property("alpha", a) -- [RFA]
					h.right:set_property("alpha", a) -- [RFA]
				else -- [RFA] without it, a soft band of the accent only (the game's ends are heavy)
					h.left:set_property("alpha", 0) -- [RFA]
					h.right:set_property("alpha", 0) -- [RFA]
					h.center:set_property("alpha", 0.3 + 0.14 * wave) -- [RFA]
				end -- [RFA]
			end -- [RFA]
		end -- [RFA]
	end -- [RFA]
end -- [RFA]

-- [RFA] The screen's thread: a frame of motion each frame, while the screen is there and the style is Modern.
function rfa_theme_screen_thread(id) -- [RFA]
	local states = rfa_states() -- [RFA]
	local state = states[id] -- [RFA]
	if state == nil then return end -- [RFA]
	local last = nil -- [RFA]
	while true do -- [RFA]
		if not rfa_theme_modern() then -- [RFA]
			pcall(rfa_theme_screen_settle, state) -- [RFA]
			pcall(rfa_theme_screen_undo, state) -- [RFA]
			break -- [RFA]
		end -- [RFA]
		local now = vint_get_time_index() -- [RFA]
		local dt = 1 / 60 -- [RFA] (when the menu's clock stands still, as in a pause)
		if type(now) == "number" and last ~= nil and now > last then dt = now - last end -- [RFA]
		if dt > 0.1 then dt = 0.1 end -- [RFA]
		last = now -- [RFA]
		if not pcall(rfa_theme_screen_frame, state, dt) then -- [RFA] (the screen has gone, or a list with it)
			pcall(rfa_theme_screen_settle, state) -- [RFA]
			break -- [RFA]
		end -- [RFA]
		thread_yield() -- [RFA]
	end -- [RFA]
	if state.lists[1] ~= nil and state.lists[1].rfa_screen == state then state.lists[1].rfa_screen = nil end -- [RFA]
	states[id] = nil -- [RFA]
end -- [RFA]
-- [RFA] ---------------------------------------------------------------------------------------------------------

function vdo_button_highlight_init()
	vint_set_property(vint_object_find("highlight_blur_anim"),"state",TWEEN_STATE_DISABLED)
end

function vdo_button_highlight_cleanup()
end

-- Inherited from Vdo_base_object
Vdo_button_highlight = Vdo_base_object:new_base()

function Vdo_button_highlight:init()
	self.button = Vdo_hint_button:new("icon", self.handle)
	self.center = Vdo_base_object:new("center", self.handle)
	self.left = Vdo_base_object:new("left", self.handle)
	self.right = Vdo_base_object:new("right", self.handle)
	self.center:set_color(COLOR_HIGHLIGHT_BAR.R, COLOR_HIGHLIGHT_BAR.G, COLOR_HIGHLIGHT_BAR.B)
	self.left:set_color(COLOR_HIGHLIGHT_BAR.R, COLOR_HIGHLIGHT_BAR.G, COLOR_HIGHLIGHT_BAR.B)
	self.right:set_color(COLOR_HIGHLIGHT_BAR.R, COLOR_HIGHLIGHT_BAR.G, COLOR_HIGHLIGHT_BAR.B)
	rfa_theme_highlight(self) -- [RFA] the Modern style's pictures
end

function Vdo_button_highlight:show_button(button_icon)
	self.button:set_button( button_icon )
	self.button:set_property("visible", true)
end

function Vdo_button_highlight:hide_button()
	self.button:set_property("visible", false)
end

function Vdo_button_highlight:set_width(width)
	
	local blur_group = Vdo_base_object:new("blur_group", self.handle)
	blur_group:set_scale(1.0,1.0)

	--set the size of the highlight
	local center = Vdo_base_object:new("center", self.handle)
	local right = Vdo_base_object:new("right", self.handle)

	--get the size of the center
	local center_width, center_height = center:get_property("screen_size")

	--set the width of the center
	local highlight_fudge = 15
	center:set_property("screen_size", width + highlight_fudge, center_height)

	--get the x and y of the right
	local right_x, right_y = right:get_property("anchor")
	local center_x, center_y = center:get_property("anchor")
	local adjusted_x = center_x + width + highlight_fudge

	--set the x of the right
	right:set_property("anchor", adjusted_x, right_y)

	center = Vdo_base_object:new("blur_center", self.handle)
	right = Vdo_base_object:new("blur_right", self.handle)

	center_width, center_height = center:get_property("screen_size")

	--set the width of the center
	--center:set_property("screen_size", width, center_height)

	right_x, right_y = right:get_property("anchor")
	center_x, center_y = center:get_property("anchor")
	adjusted_x = center_x + (width)

	--set the x of the right
	--right:set_property( "anchor", adjusted_x, right_y )
end

function Vdo_button_highlight:set_highlight()
	local blur_anim = Vdo_anim_object:new( "highlight_blur_anim", self.handle )
	local blur_group = Vdo_base_object:new( "blur_group", self.handle )
	blur_anim:set_child_tween_target_handle( blur_group.handle )
	--blur_anim:play()
end
