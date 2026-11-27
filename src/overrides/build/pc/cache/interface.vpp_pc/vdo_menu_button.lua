-- Override of vdo_menu_button.lua from <game>\build\pc\cache\interface.vpp_pc (a button of the menus' lists).
-- rfa_loader.dll gives the game this file instead of its own copy.
--
-- In RFA-Mod's Modern style (see vdo_button_highlight.lua's rfa_theme_*) a button keeps its box and the little
-- text beside it hidden when it isn't highlighted, too. Every line we added or changed is marked [RFA]; everything
-- else is the game's original script. Only the [RFA] lines are this project's (GNU GPL version 3 or later, see
-- LICENSE); the rest belongs to the game's owners. To compare with the original, extract it with the editor.

function vdo_menu_button_init()
end

function vdo_menu_button_cleanup()
end

-- Inherited from Vdo_base_object
Vdo_menu_button = Vdo_base_object:new_base()

function Vdo_menu_button:set_label(label)
	local text = Vdo_base_object:new("text", self.handle)

	if text.handle ~= 0 then
		--handle Sony and MS branded term case requirments
		if label == "NETWORK_PLATFORM_NAME" then
			text:set_property("force_case", "none")
			text:set_property("text_tag", label)
		else
			text:set_property("force_case", "upper")
			text:set_property("text_tag", to_upper(label))
		end

		local text_width, text_height = text:get_property("screen_size")
		self:set_width(text_width)
	end
end

function Vdo_menu_button:set_width(width)
	local bg = Vdo_base_object:new("background", self.handle)
	local techno_text = Vdo_base_object:new("techno_text", self.handle)

	--get the old width and height of the background
	local bogus_width, bg_height = bg:get_property("screen_size")
	local bg_x, bg_y = bg:get_property("anchor")
	local text_x, text_y = techno_text:get_property("anchor")

	--set the background image width and height
	bg:set_property("screen_size", width + 8, bg_height)
	local adjusted_x = bg_x + width + 10
	techno_text:set_property("anchor", adjusted_x, text_y)

	local random_text = rand_int(0, 10)
	if random_text < 5 then
		techno_text:set_property("image", "ui_hud_small_text1")
	else
		techno_text:set_property("image", "ui_hud_small_text2")
	end
end

function Vdo_menu_button:set_highlight(is_highlighted)
	local text = Vdo_base_object:new("text", self.handle)
	local bg = Vdo_base_object:new("background", self.handle)
	local techno_text = Vdo_base_object:new("techno_text", self.handle)

	if is_highlighted == true then
		bg:set_visible(false)
		techno_text:set_visible(false)
		text:set_visible(false)
		text:set_property("text_scale", 1.1, 1.1)
	else
		local modern = _GetAnyGlobalSilent("rfa_theme_modern") -- [RFA]
		local boxed = not (modern ~= nil and modern()) -- [RFA] Modern: no box, no little text
		bg:set_visible(boxed) -- [RFA] (was true)
		techno_text:set_visible(boxed) -- [RFA] (was true)
		text:set_visible(true)
		text:set_property("text_scale", 0.7,0.7)
		text:set_color(COLOR_UNHIGHLIGHT_TEXT.R,COLOR_UNHIGHLIGHT_TEXT.G,COLOR_UNHIGHLIGHT_TEXT.B)
	end
end

function Vdo_menu_button:get_width()
	local text = Vdo_base_object:new("text", self.handle)
	local width, height = text:get_property("screen_size")
	return width
end
