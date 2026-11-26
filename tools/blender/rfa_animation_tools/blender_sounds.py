"""The sound browser (its own sidebar tab, "RFA Sounds"): every sound the game can play, searched by name and
sorted by type and category, heard in Blender, and set wherever a vehicle has a sound: its doors (the
vehicle animation kit's door sounds) and its Foley in vehicles.xtbl (engine, gear shift, scraping, impacts,
landing, horn, ...). See rfa_sounds for where the sounds come from."""

import os
import random
import re
import tempfile

import bpy
from bpy.props import BoolProperty, CollectionProperty, EnumProperty, IntProperty, StringProperty

from . import rfa_sounds as rs

DOOR_KEYS = (("rfa_door_open_sound", "Door opens"), ("rfa_door_close_sound", "Door shuts"), ("rfa_door_land_sound", "Door fully open"))

# A vehicle's sounds in vehicles.xtbl (<Foley>): the field, and what it is.
VEHICLE_FIELDS = (
    ("Engine_Start", "Engine"),
    ("Gear_Shift", "Gear shift (transmission)"),
    ("Engine_Peel", "Wheels spinning (peel-out)"),
    ("Scraping", "Scraping"),
    ("Low_Impact", "Impact, light"),
    ("High_Impact", "Impact, heavy"),
    ("Chassis_Land_Low", "Landing, light"),
    ("Chassis_Land_High", "Landing, heavy"),
    ("Corpse_Impact", "Hitting someone"),
    ("Pass_By", "Driving past"),
    ("Horn", "Horn"),
    ("Force_Field", "Force field"),
    ("Local_Player_in_Vehicle_State", "Inside (player's mix)"),
)

ALL = "ALL"
_library = {}
_playing = {"handle": None}
_enum_items = {"types": [], "categories": []}  # kept alive: Blender needs dynamic enum items to stay referenced
_vehicle_cache = {}


def _prefs(context):
    return context.preferences.addons[__package__].preferences


def _folders(context):
    p = _prefs(context)
    game = bpy.path.abspath(p.game_folder)
    return game, bpy.path.abspath(p.mod_folder or p.game_folder)


def library(context):
    key = tuple(f.lower() for f in _folders(context))
    if key not in _library:
        _library.clear()
        _library[key] = rs.Library(*_folders(context))
    return _library[key]


def stop():
    h = _playing["handle"]
    if h is not None:
        try:
            h.stop()
        except Exception:
            pass
    _playing["handle"] = None


def play(context, name):
    """Hear a sound by name (one of its variations at random); returns an error text, or None."""
    import aud
    stop()
    lib = library(context)
    try:
        eid = rs.event_id(name)
        bank = lib.event_banks().get(eid)
        n = len(lib.bank(bank).sounds(eid)) if bank else 1
        data = lib.wav(name, random.randrange(max(1, n)), bpy.path.abspath(_prefs(context).vgmstream), tempfile.gettempdir())
    except ValueError as e:
        return str(e)
    path = os.path.join(tempfile.gettempdir(), "rfa_preview.wav")
    with open(path, "wb") as fh:
        fh.write(data)
    _playing["handle"] = aud.Device().play(aud.Sound(path).cache())  # read now: the file is written again for the next one
    return None


def picked(context):
    wm = context.window_manager
    return wm.rfa_sounds[wm.rfa_sound_index].name if 0 <= wm.rfa_sound_index < len(wm.rfa_sounds) else ""


# ---------------------------------------------------------------- the vehicle's own sounds (vehicles.xtbl Foley)


def current_vehicle(context):
    """(name in vehicles.xtbl, its kit's root) of the vehicle being worked on: the animation kit's, else the builder's."""
    from . import people_root
    from . import blender_vehicle as bv
    root = people_root(context)
    if root is not None:
        return root["rfa_people_vehicle"], root
    kit = bv.kit_root(context.active_object)
    if kit is not None and kit.get("rfa_vehicle_name"):
        return kit["rfa_vehicle_name"], kit
    return None, None


def vehicle_foley(context, vehicle):
    """{field: sound} of a vehicle's Foley, or None when vehicles.xtbl has no such vehicle (read once, until changed).
    While the RFA Tuning tab edits this vehicle, its working copy (with changes not yet written) is what's shown."""
    from . import blender_vehicle_params as bvp
    shared = bvp.working_foley(vehicle)
    if shared is not None:
        return shared
    if vehicle not in _vehicle_cache:
        from . import blender_vehicle_anim as bva
        text = bva.read_table(*_folders(context), "vehicles.xtbl")
        span = bva._entry(text, "Vehicle", vehicle)
        if span is None:
            _vehicle_cache[vehicle] = None
        else:
            entry = text[span[0]:span[1]]
            m = re.search(r"<Foley>(.*?)</Foley>", entry, re.S)
            body = m.group(1) if m else ""
            _vehicle_cache[vehicle] = {f: (re.search(r"<%s>([^<]*)</%s>" % (f, f), body) or [None, ""])[1].strip()
                                       for f, _ in VEHICLE_FIELDS}
    return _vehicle_cache[vehicle]


def set_vehicle_sound(context, vehicle, field, sound):
    """Set (or, empty, clear) one of a vehicle's Foley sounds in vehicles.xtbl, where the game reads it with RFA-Mod."""
    from . import blender_vehicle_anim as bva
    game, mod = _folders(context)
    text = bva.read_table(game, mod, "vehicles.xtbl")
    span = bva._entry(text, "Vehicle", vehicle)
    if span is None:
        raise ValueError(f"vehicles.xtbl has no vehicle named {vehicle} (export the vehicle first)")
    entry = text[span[0]:span[1]]
    element = f"<{field}>{sound}</{field}>"
    m = re.search(r"<Foley>(.*?)</Foley>", entry, re.S)
    if m:
        body = m.group(1)
        if re.search(r"<%s>[^<]*</%s>|<%s\s*/>" % (field, field, field), body):
            body = re.sub(r"<%s>[^<]*</%s>|<%s\s*/>" % (field, field, field), element, body, count=1)
        else:
            body = body.rstrip() + "\n\t\t\t" + element + "\n\t\t"
        entry = entry[:m.start(1)] + body + entry[m.end(1):]
    else:
        end = entry.rfind("</Vehicle>")
        entry = entry[:end] + f"\t<Foley>\n\t\t\t{element}\n\t\t</Foley>\n\t" + entry[end:]
    path = bva.write_table(mod, "vehicles.xtbl", text[:span[0]] + entry + text[span[1]:])
    _vehicle_cache.pop(vehicle, None)
    from . import blender_vehicle_params as bvp
    bvp.sound_written(context, vehicle, field, sound)  # the RFA Tuning tab's copy follows
    redraw(context)
    return path


def redraw(context):
    """Draw every sidebar again, so all the tabs show the same."""
    for window in context.window_manager.windows:
        for area in window.screen.areas:
            if area.type == "VIEW_3D":
                area.tag_redraw()


# ---------------------------------------------------------------- the list, its search and its drop-downs


class RFA_SoundItem(bpy.types.PropertyGroup):
    name: StringProperty()
    kind: StringProperty()
    category: StringProperty()
    bank: StringProperty()


def _type_items(self, context):
    wm = context.window_manager
    counts = {}
    for it in wm.rfa_sounds:
        counts[it.kind] = counts.get(it.kind, 0) + 1
    _enum_items["types"] = [(ALL, f"All types ({len(wm.rfa_sounds)})", "Every kind of sound")] + \
        [(k, f"{k} ({counts[k]})", f"Sounds of the {k} banks") for k in sorted(counts)]
    return _enum_items["types"]


def _category_items(self, context):
    wm = context.window_manager
    kind = wm.rfa_sound_type
    counts = {}
    for it in wm.rfa_sounds:
        if kind == ALL or it.kind == kind:
            counts[it.category] = counts.get(it.category, 0) + 1
    _enum_items["categories"] = [(ALL, "All categories", "Every category of this type")] + \
        [(c or "-", f"{c or '(none)'} ({counts[c]})", "The game's category, or the bank the sound is in") for c in sorted(counts)]
    return _enum_items["categories"]


def _type_changed(self, context):
    context.window_manager.rfa_sound_category = ALL


def _selected_changed(self, context):
    wm = context.window_manager
    if wm.rfa_sound_autoplay and 0 <= wm.rfa_sound_index < len(wm.rfa_sounds):
        play(context, wm.rfa_sounds[wm.rfa_sound_index].name)


class RFA_UL_sounds(bpy.types.UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        row = layout.row(align=True)
        row.label(text=item.name, icon="SOUND")
        sub = row.row()
        sub.alignment = "RIGHT"
        sub.label(text=item.category)
        row.operator(RFA_OT_sound_play.bl_idname, text="", icon="PLAY", emboss=False).name = item.name

    def filter_items(self, context, data, propname):
        wm = context.window_manager
        items = getattr(data, propname)
        words = (wm.rfa_sound_search + " " + self.filter_name).lower().split()
        kind, cat = wm.rfa_sound_type, wm.rfa_sound_category
        flags = []
        for it in items:
            ok = (kind == ALL or it.kind == kind) and (cat == ALL or (it.category or "-") == cat) and \
                all(w in (it.name + " " + it.category + " " + it.bank).lower() for w in words)
            flags.append(self.bitflag_filter_item if ok else 0)
        return flags, []


class RFA_OT_sound_list(bpy.types.Operator):
    """Read every sound the game can play (the game's banks and the Guerrilla world's) into the browser"""
    bl_idname = "rfa.sound_list"
    bl_label = "Load Game Sounds"

    def execute(self, context):
        _library.clear()
        _vehicle_cache.clear()
        lib = library(context)
        try:
            names = lib.names()
        except Exception as e:
            self.report({"ERROR"}, f"Couldn't read the game's sounds: {e}")
            return {"CANCELLED"}
        wm = context.window_manager
        wm.rfa_sounds.clear()
        for n in sorted(names, key=lambda n: (lib.types.get(n, ""), lib.categories.get(n, ""), n.upper())):
            it = wm.rfa_sounds.add()
            it.name = n
            it.kind = lib.types.get(n, "Other")
            it.category = lib.categories.get(n, "")
            it.bank = lib.banks.get(n, "")
        wm.rfa_sound_type = ALL
        self.report({"INFO"}, f"{len(names)} sounds")
        return {"FINISHED"}


class RFA_OT_sound_play(bpy.types.Operator):
    """Hear this sound (one of its variations at random, where it has several)"""
    bl_idname = "rfa.sound_play"
    bl_label = "Play Sound"
    name: StringProperty()

    def execute(self, context):
        if not self.name:
            self.report({"WARNING"}, "No sound set here")
            return {"CANCELLED"}
        err = play(context, self.name)
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


class RFA_OT_sound_set(bpy.types.Operator):
    """Use the sound picked in the browser here (or, with Clear, no sound)"""
    bl_idname = "rfa.sound_set"
    bl_label = "Use Picked Sound"
    key: StringProperty(description="A door sound of the animation kit (rfa_door_...), or a vehicles.xtbl Foley field")
    clear: BoolProperty(default=False)

    def execute(self, context):
        sound = "" if self.clear else picked(context)
        if not sound and not self.clear:
            self.report({"WARNING"}, "Pick a sound in the list first")
            return {"CANCELLED"}
        vehicle, root = current_vehicle(context)
        if vehicle is None:
            self.report({"WARNING"}, "Select the vehicle (its animation kit or its builder kit)")
            return {"CANCELLED"}
        if self.key.startswith("rfa_door_"):
            root[self.key] = sound
            redraw(context)
            self.report({"INFO"}, f"{vehicle}: {self.key} = {sound or '(none)'}; export the vehicle's animations to use it")
            return {"FINISHED"}
        try:
            path = set_vehicle_sound(context, vehicle, self.key, sound)
        except ValueError as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}
        self.report({"INFO"}, f"{vehicle}: {self.key} = {sound or '(none)'} in {path}; the game reads it at its next start")
        return {"FINISHED"}


def _sound_row(layout, label, value, key):
    row = layout.row(align=True)
    split = row.split(factor=0.42, align=True)
    split.label(text=label)
    split.label(text=value or "-")
    play = row.row(align=True)
    play.enabled = bool(value)
    play.operator(RFA_OT_sound_play.bl_idname, text="", icon="PLAY").name = value
    row.operator(RFA_OT_sound_set.bl_idname, text="", icon="EYEDROPPER").key = key
    op = row.operator(RFA_OT_sound_set.bl_idname, text="", icon="X")
    op.key, op.clear = key, True


def draw_browser(layout, context):
    wm = context.window_manager
    head = layout.row(align=True)
    head.operator(RFA_OT_sound_list.bl_idname, text="Reload" if len(wm.rfa_sounds) else "Load Game Sounds", icon="FILE_REFRESH")
    head.operator(RFA_OT_sound_stop.bl_idname, text="", icon="SNAP_FACE")
    if not len(wm.rfa_sounds):
        layout.label(text="Load the game's sounds to search and hear them")
        return
    layout.prop(wm, "rfa_sound_search", text="", icon="VIEWZOOM")
    row = layout.row(align=True)
    row.prop(wm, "rfa_sound_type", text="")
    row.prop(wm, "rfa_sound_category", text="")
    layout.template_list("RFA_UL_sounds", "", wm, "rfa_sounds", wm, "rfa_sound_index", rows=12)
    row = layout.row()
    row.prop(wm, "rfa_sound_autoplay")
    p = picked(context)
    if p:
        layout.label(text=f"Picked: {p}", icon="CHECKMARK")
    if not bpy.path.abspath(_prefs(context).vgmstream):
        layout.label(text="Armageddon's own sounds need vgmstream-cli.exe (preferences)", icon="ERROR")


def draw_vehicle_sounds(layout, context):
    vehicle, root = current_vehicle(context)
    if vehicle is None:
        layout.label(text="Select a vehicle (its animation or builder kit)", icon="INFO")
        return
    layout.label(text=vehicle, icon="AUTO")
    doors = [(k, t) for k, t in DOOR_KEYS if k in root]
    if doors:
        col = layout.column(align=True)
        for key, text in doors:
            _sound_row(col, text, str(root.get(key, "")), key)
    try:
        foley = vehicle_foley(context, vehicle)
    except Exception as e:
        layout.label(text=f"Couldn't read vehicles.xtbl: {e}", icon="ERROR")
        return
    if foley is None:
        layout.label(text="Not in vehicles.xtbl yet: export the vehicle first", icon="INFO")
        return
    col = layout.column(align=True)
    for field, text in VEHICLE_FIELDS:
        _sound_row(col, text, foley.get(field, ""), field)
    layout.label(text="Pick a sound above, then its eyedropper here", icon="EYEDROPPER")


def draw_door_sounds(layout, context, root):
    """The vehicle animation panel's door sounds (the browser itself is in the RFA Sounds tab)."""
    col = layout.column(align=True)
    col.label(text="Door sounds (browse them in the RFA Sounds tab)", icon="SPEAKER")
    for key, text in DOOR_KEYS:
        if key in root:
            _sound_row(col, text, str(root.get(key, "")), key)


class RFA_PT_sounds(bpy.types.Panel):
    bl_label = "Sound Browser"
    bl_idname = "RFA_PT_sounds"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "RFA Sounds"

    def draw(self, context):
        draw_browser(self.layout, context)


class RFA_PT_vehicle_sounds(bpy.types.Panel):
    bl_label = "Vehicle Sounds"
    bl_idname = "RFA_PT_vehicle_sounds"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "RFA Sounds"

    def draw(self, context):
        draw_vehicle_sounds(self.layout, context)


classes = (RFA_SoundItem, RFA_UL_sounds, RFA_OT_sound_list, RFA_OT_sound_play, RFA_OT_sound_stop, RFA_OT_sound_set,
           RFA_PT_sounds, RFA_PT_vehicle_sounds)


def register():
    for c in classes:
        bpy.utils.register_class(c)
    wm = bpy.types.WindowManager
    wm.rfa_sounds = CollectionProperty(type=RFA_SoundItem)
    wm.rfa_sound_index = IntProperty(name="Sound", default=-1, update=_selected_changed)
    wm.rfa_sound_autoplay = BoolProperty(name="Play when picked", default=True,
                                         description="Hear a sound as soon as it's picked in the list")
    wm.rfa_sound_search = StringProperty(name="Search", default="", options={"TEXTEDIT_UPDATE"},
                                         description="Words the sound's name, category or bank must have (any order)")
    wm.rfa_sound_type = EnumProperty(name="Type", items=_type_items, update=_type_changed,
                                     description="What kind of sound (from the bank it's in)")
    wm.rfa_sound_category = EnumProperty(name="Category", items=_category_items,
                                         description="The game's category for it (audio_events.xtbl), or its bank")


def unregister():
    stop()
    wm = bpy.types.WindowManager
    for p in ("rfa_sound_category", "rfa_sound_type", "rfa_sound_search", "rfa_sound_autoplay", "rfa_sound_index", "rfa_sounds"):
        if hasattr(wm, p):
            delattr(wm, p)
    for c in reversed(classes):
        bpy.utils.unregister_class(c)
