"""The sound browser: every sound the game can play, searchable, heard in Blender, and set as a vehicle's door
sounds (the people panel's Door sounds). See rfa_sounds for where the sounds come from."""

import os
import random
import tempfile

import bpy
from bpy.props import BoolProperty, CollectionProperty, IntProperty, StringProperty

from . import rfa_sounds as rs

DOOR_KEYS = (("rfa_door_open_sound", "Opens"), ("rfa_door_close_sound", "Shuts"), ("rfa_door_land_sound", "Fully open"))

_library = {}
_playing = {"handle": None}


def _prefs(context):
    return context.preferences.addons[__package__].preferences


def library(context):
    p = _prefs(context)
    game = bpy.path.abspath(p.game_folder)
    mod = bpy.path.abspath(p.mod_folder or p.game_folder)
    key = (game.lower(), mod.lower())
    if key not in _library:
        _library.clear()
        _library[key] = rs.Library(game, mod)
    return _library[key]


def stop():
    h = _playing["handle"]
    if h is not None:
        try:
            h.stop()
        except Exception:
            pass
    _playing["handle"] = None


def play(context, name, pick=None):
    """Hear a sound by name; returns an error text, or None."""
    import aud
    stop()
    lib = library(context)
    try:
        if pick is None:
            eid = rs.event_id(name)
            bank = lib.event_banks().get(eid)
            n = len(lib.bank(bank).sounds(eid)) if bank else 1
            pick = random.randrange(max(1, n))
        data = lib.wav(name, pick, bpy.path.abspath(_prefs(context).vgmstream), tempfile.gettempdir())
    except ValueError as e:
        return str(e)
    path = os.path.join(tempfile.gettempdir(), "rfa_preview.wav")
    with open(path, "wb") as fh:
        fh.write(data)
    sound = aud.Sound(path).cache()  # read now: the file is written again for the next one
    _playing["handle"] = aud.Device().play(sound)
    return None


class RFA_SoundItem(bpy.types.PropertyGroup):
    name: StringProperty()
    category: StringProperty()


def _selected_changed(self, context):
    wm = context.window_manager
    if wm.rfa_sound_autoplay and 0 <= wm.rfa_sound_index < len(wm.rfa_sounds):
        play(context, wm.rfa_sounds[wm.rfa_sound_index].name)


class RFA_UL_sounds(bpy.types.UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        row = layout.row(align=True)
        row.label(text=item.name, icon="SOUND")
        if item.category:
            sub = row.row()
            sub.alignment = "RIGHT"
            sub.label(text=item.category)
        row.operator(RFA_OT_sound_play.bl_idname, text="", icon="PLAY", emboss=False).name = item.name

    def filter_items(self, context, data, propname):
        items = getattr(data, propname)
        flt = self.filter_name.lower().split()
        flags = [self.bitflag_filter_item if all(w in (it.name + " " + it.category).lower() for w in flt) else 0 for it in items]
        if self.use_filter_invert:
            flags = [self.bitflag_filter_item & ~f for f in flags]
        return flags, []


class RFA_OT_sound_list(bpy.types.Operator):
    """Read every sound the game can play (the game's banks and the Guerrilla world's) into the browser"""
    bl_idname = "rfa.sound_list"
    bl_label = "Load Game Sounds"

    def execute(self, context):
        _library.clear()
        lib = library(context)
        try:
            names = lib.names()
        except Exception as e:
            self.report({"ERROR"}, f"Couldn't read the game's sounds: {e}")
            return {"CANCELLED"}
        wm = context.window_manager
        wm.rfa_sounds.clear()
        for n in names:
            it = wm.rfa_sounds.add()
            it.name = n
            it.category = lib.categories.get(n, "")
        self.report({"INFO"}, f"{len(names)} sounds")
        return {"FINISHED"}


class RFA_OT_sound_play(bpy.types.Operator):
    """Hear this sound (one of its variations at random, where it has several)"""
    bl_idname = "rfa.sound_play"
    bl_label = "Play Sound"
    name: StringProperty()
    key: StringProperty(description="Play the sound the vehicle's door sound field names")

    def execute(self, context):
        name = self.name
        if self.key:
            from . import people_root
            root = people_root(context)
            name = str(root.get(self.key, "")).strip() if root is not None else ""
        if not name:
            self.report({"WARNING"}, "No sound named")
            return {"CANCELLED"}
        err = play(context, name)
        if err:
            self.report({"WARNING"}, err)
            return {"CANCELLED"}
        return {"FINISHED"}


class RFA_OT_sound_stop(bpy.types.Operator):
    """Stop the sound playing"""
    bl_idname = "rfa.sound_stop"
    bl_label = "Stop Sound"

    def execute(self, context):
        stop()
        return {"FINISHED"}


class RFA_OT_sound_use(bpy.types.Operator):
    """Use the sound picked in the browser for this door sound"""
    bl_idname = "rfa.sound_use"
    bl_label = "Use Picked Sound"
    key: StringProperty()

    def execute(self, context):
        from . import people_root
        root = people_root(context)
        wm = context.window_manager
        if root is None or not 0 <= wm.rfa_sound_index < len(wm.rfa_sounds):
            self.report({"WARNING"}, "Pick a sound in the browser first")
            return {"CANCELLED"}
        root[self.key] = wm.rfa_sounds[wm.rfa_sound_index].name
        return {"FINISHED"}


class RFA_OT_sound_clear(bpy.types.Operator):
    """No sound here"""
    bl_idname = "rfa.sound_clear"
    bl_label = "Clear Sound"
    key: StringProperty()

    def execute(self, context):
        from . import people_root
        root = people_root(context)
        if root is not None:
            root[self.key] = ""
        return {"FINISHED"}


def draw_door_sounds(layout, context, root):
    """The people panel's Door sounds: each field, with play, use-the-picked-one and clear; then the browser."""
    col = layout.column(align=True)
    col.label(text="Door sounds", icon="SPEAKER")
    for key, text in DOOR_KEYS:
        if key not in root:  # (made by Animate Getting In; older kits: set them as the root's custom properties)
            continue
        row = col.row(align=True)
        row.prop(root, '["%s"]' % key, text=text)
        row.operator(RFA_OT_sound_play.bl_idname, text="", icon="PLAY").key = key
        row.operator(RFA_OT_sound_use.bl_idname, text="", icon="EYEDROPPER").key = key
        row.operator(RFA_OT_sound_clear.bl_idname, text="", icon="X").key = key
    wm = context.window_manager
    box = layout.box()
    head = box.row(align=True)
    head.label(text="Sound browser", icon="VIEWZOOM")
    head.operator(RFA_OT_sound_list.bl_idname, text="" if len(wm.rfa_sounds) else "Load", icon="FILE_REFRESH")
    head.operator(RFA_OT_sound_stop.bl_idname, text="", icon="PAUSE")
    if not len(wm.rfa_sounds):
        box.label(text="Load the game's sounds to search and hear them")
        return
    box.template_list("RFA_UL_sounds", "", wm, "rfa_sounds", wm, "rfa_sound_index", rows=8)
    row = box.row()
    row.prop(wm, "rfa_sound_autoplay")
    box.label(text="Search above (the list's filter); pick one, then the eyedropper by a field uses it", icon="INFO")
    if not bpy.path.abspath(_prefs(context).vgmstream):
        box.label(text="Armageddon's own sounds need vgmstream-cli.exe (add-on preferences)", icon="ERROR")


classes = (RFA_SoundItem, RFA_UL_sounds, RFA_OT_sound_list, RFA_OT_sound_play, RFA_OT_sound_stop, RFA_OT_sound_use,
           RFA_OT_sound_clear)


def register():
    for c in classes:
        bpy.utils.register_class(c)
    wm = bpy.types.WindowManager
    wm.rfa_sounds = CollectionProperty(type=RFA_SoundItem)
    wm.rfa_sound_index = IntProperty(name="Sound", default=-1, update=_selected_changed)
    wm.rfa_sound_autoplay = BoolProperty(name="Play when picked", default=True,
                                         description="Hear a sound as soon as it's picked in the list")


def unregister():
    stop()
    wm = bpy.types.WindowManager
    for p in ("rfa_sound_autoplay", "rfa_sound_index", "rfa_sounds"):
        if hasattr(wm, p):
            delattr(wm, p)
    for c in reversed(classes):
        bpy.utils.unregister_class(c)
