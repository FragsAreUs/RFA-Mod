"""The vehicle parameters editor (sidebar tab "RFA Tuning"): any vehicle's vehicles.xtbl entry as fields by section,
templates from every vehicle in the table and the user's own, written back where the game reads it with RFA-Mod.
See rfa_vehicle_params."""

import bpy
from bpy.props import BoolProperty, CollectionProperty, EnumProperty, FloatProperty, IntProperty, StringProperty

from . import rfa_vehicle_params as vp

ALL = "ALL"
_work = {"vehicle": "", "root": None, "original": {}, "busy": False}
_kept = {}  # dynamic enum items, kept referenced


def _folders(context):
    p = context.preferences.addons[__package__].preferences
    return bpy.path.abspath(p.game_folder), bpy.path.abspath(p.mod_folder or p.game_folder)


def _table(context):
    from . import blender_vehicle_anim as bva
    return bva.read_table(*_folders(context), "vehicles.xtbl")


def _format(kind, v):
    if kind == "Float":
        return f"{v:.6g}"
    return str(v)


# ---------------------------------------------------------------- the fields


def working_foley(vehicle):
    """The working copy's Foley ({field: sound}) while this vehicle is being edited here, else None."""
    if _work["root"] is None or _work["vehicle"].lower() != (vehicle or "").lower():
        return None
    foley = _work["root"].find("Foley")
    return {el.tag: (el.text or "").strip() for el in foley} if foley is not None else {}


def sound_written(context, vehicle, field, sound):
    """The RFA Sounds tab wrote one of this vehicle's sounds to the game: the working copy and its field follow
    (as written, so not marked as changed)."""
    if working_foley(vehicle) is None:
        return
    root = _work["root"]
    foley = root.find("Foley")
    if foley is None or foley.find(field) is None:
        fill(context, _work["vehicle"], vp.parse_entry(_table(context), vehicle) or root)  # a new element: read it again
        return
    key = vp.path_key((("Foley", 0), (field, 0)))
    _work["original"][key] = sound
    for it in context.window_manager.rfa_vp_items:
        if it.key == key:
            it.sval = sound
            it.changed = False


def _changed(item):
    item.changed = item.sval != _work["original"].get(item.key, item.sval)


def _sval_update(self, context):
    if _work["busy"] or _work["root"] is None:
        return
    vp.set_value(_work["root"], vp.key_path(self.key), self.sval)
    from . import blender_sounds as bs
    bs.redraw(context)
    _work["busy"] = True
    try:
        if self.kind == "Float":
            try:
                self.fval = float(self.sval)
            except ValueError:
                pass
        elif self.kind == "Int":
            try:
                self.ival = int(float(self.sval))
            except ValueError:
                pass
    finally:
        _work["busy"] = False
    _changed(self)


def _num_update(self, context):
    if _work["busy"]:
        return
    self.sval = _format(self.kind, self.fval if self.kind == "Float" else self.ival)


class RFA_VParam(bpy.types.PropertyGroup):
    key: StringProperty()
    label: StringProperty()
    section: StringProperty()
    kind: StringProperty()
    choices: StringProperty()
    tip: StringProperty()
    own: BoolProperty()
    changed: BoolProperty()
    sval: StringProperty(name="Value", update=_sval_update)
    fval: FloatProperty(name="Value", update=_num_update, precision=3)
    ival: IntProperty(name="Value", update=_num_update)


def fill(context, vehicle, root, original=True):
    """Show an entry's parameters (original: what the table has now, to mark changes against)."""
    desc = vp.Description(_table(context))
    wm = context.window_manager
    _work["vehicle"], _work["root"] = vehicle, root
    _work["busy"] = True
    try:
        wm.rfa_vp_items.clear()
        if original:
            _work["original"] = {}
        for path, tags, value in vp.leaves(root):
            if tags[0] == "_Editor":
                continue
            info = desc.of(tags)
            it = wm.rfa_vp_items.add()
            it.key = vp.path_key(path)
            it.label = vp.label_of(path, info)
            it.section = vp.section_of(tags[0])
            it.own = tags[0] in vp.OWN
            kind = info["type"]
            it.kind = kind if kind in ("Float", "Int", "Selection") else "String"
            it.choices = "|".join(info["choices"])
            it.tip = info["description"] + (f" (the game's default: {info['default']})" if info["default"] else "")
            it.sval = value
            if it.kind == "Float":
                try:
                    it.fval = float(value)
                except ValueError:
                    it.kind = "String"
            elif it.kind == "Int":
                try:
                    it.ival = int(float(value))
                except ValueError:
                    it.kind = "String"
            if original:
                _work["original"][it.key] = value
            it.changed = it.sval != _work["original"].get(it.key, value)
    finally:
        _work["busy"] = False


# ---------------------------------------------------------------- choosing things


def _vehicle_items(self, context):
    try:
        names = vp.vehicle_names(_table(context))
    except Exception as e:
        names = []
        print("RFA Animation Tools:", e)
    _kept["vehicles"] = [(n, n, "Edit this vehicle's parameters") for n in sorted(set(names), key=str.lower)] or \
        [("", "(no vehicles.xtbl: check the game folder in the preferences)", "")]
    return _kept["vehicles"]


def _template_items(self, context):
    from . import blender_vehicle_anim as bva  # noqa: F401 (the table read through it)
    mine = vp.user_templates(_folders(context)[1])
    try:
        names = vp.vehicle_names(_table(context))
    except Exception:
        names = []
    _kept["templates"] = [("mine:" + n, "My: " + n, "Your own template") for n in mine] + \
        [("game:" + n, n, "The parameters this vehicle has in the table") for n in sorted(set(names), key=str.lower)]
    return _kept["templates"] or [("", "(none)", "")]


class RFA_OT_vp_edit(bpy.types.Operator):
    """Edit a vehicle's parameters (vehicles.xtbl): pick any vehicle"""
    bl_idname = "rfa.vp_edit"
    bl_label = "Edit Vehicle Parameters"
    bl_property = "vehicle"
    vehicle: EnumProperty(name="Vehicle", items=_vehicle_items)

    def invoke(self, context, event):
        context.window_manager.invoke_search_popup(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        return _load(self, context, self.vehicle)


class RFA_OT_vp_edit_selected(bpy.types.Operator):
    """Edit the parameters of the vehicle selected in the scene (its animation or builder kit)"""
    bl_idname = "rfa.vp_edit_selected"
    bl_label = "Edit Selected Vehicle"

    def execute(self, context):
        from . import blender_sounds as bs
        vehicle, _ = bs.current_vehicle(context)
        if not vehicle:
            self.report({"WARNING"}, "No vehicle selected: pick one with Edit Vehicle Parameters")
            return {"CANCELLED"}
        return _load(self, context, vehicle)


def _load(op, context, vehicle):
    try:
        root = vp.parse_entry(_table(context), vehicle)
    except Exception as e:
        op.report({"ERROR"}, f"Couldn't read vehicles.xtbl: {e}")
        return {"CANCELLED"}
    if root is None:
        op.report({"ERROR"}, f"vehicles.xtbl has no vehicle named {vehicle} (export it first)")
        return {"CANCELLED"}
    fill(context, vehicle, root)
    from . import blender_sounds as bs
    bs.redraw(context)
    op.report({"INFO"}, f"{vehicle}: {len(context.window_manager.rfa_vp_items)} parameters")
    return {"FINISHED"}


class RFA_OT_vp_template_pick(bpy.types.Operator):
    """Pick a template: any vehicle in the table, or one of your own"""
    bl_idname = "rfa.vp_template_pick"
    bl_label = "Pick Template"
    bl_property = "template"
    template: EnumProperty(name="Template", items=_template_items)

    def invoke(self, context, event):
        context.window_manager.invoke_search_popup(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        context.window_manager.rfa_vp_template = self.template
        return {"FINISHED"}


def _template_sections(context, template):
    kind, _, name = template.partition(":")
    if kind == "mine":
        return vp.load_template(_folders(context)[1], name)
    root = vp.parse_entry(_table(context), name)
    if root is None:
        raise ValueError(f"vehicles.xtbl has no vehicle named {name}")
    return vp.sections_xml(root, [s for s, _, _ in vp.SECTIONS])


class RFA_OT_vp_template_apply(bpy.types.Operator):
    """Copy the template's chosen sections into the vehicle being edited (not written to the game until Write to Game)"""
    bl_idname = "rfa.vp_template_apply"
    bl_label = "Use Template"

    def execute(self, context):
        wm = context.window_manager
        if _work["root"] is None:
            self.report({"WARNING"}, "Edit a vehicle first")
            return {"CANCELLED"}
        if not wm.rfa_vp_template:
            self.report({"WARNING"}, "Pick a template first")
            return {"CANCELLED"}
        sections = list(wm.rfa_vp_apply)
        if not sections:
            self.report({"WARNING"}, "Tick the sections to copy")
            return {"CANCELLED"}
        try:
            n = vp.apply_sections(_work["root"], _template_sections(context, wm.rfa_vp_template), sections)
        except Exception as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}
        fill(context, _work["vehicle"], _work["root"], original=False)
        from . import blender_sounds as bs
        bs.redraw(context)
        self.report({"INFO"}, f"{n} parts copied from {wm.rfa_vp_template.split(':', 1)[1]}; Write to Game to keep them")
        return {"FINISHED"}


class RFA_OT_vp_template_save(bpy.types.Operator):
    """Save the vehicle being edited (its ticked sections) as a template of your own"""
    bl_idname = "rfa.vp_template_save"
    bl_label = "Save as My Template"
    name: StringProperty(name="Name")

    def invoke(self, context, event):
        self.name = _work["vehicle"] + " tuned"
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, context):
        if _work["root"] is None:
            self.report({"WARNING"}, "Edit a vehicle first")
            return {"CANCELLED"}
        sections = list(context.window_manager.rfa_vp_apply) or [s for s, _, _ in vp.SECTIONS]
        path = vp.save_template(_folders(context)[1], self.name, _work["root"], sections, _work["vehicle"])
        context.window_manager.rfa_vp_template = "mine:" + bpy.path.display_name_from_filepath(path)
        self.report({"INFO"}, f"Saved {path}")
        return {"FINISHED"}


class RFA_OT_vp_template_delete(bpy.types.Operator):
    """Delete the picked template of your own"""
    bl_idname = "rfa.vp_template_delete"
    bl_label = "Delete My Template"

    def invoke(self, context, event):
        return context.window_manager.invoke_confirm(self, event)

    def execute(self, context):
        t = context.window_manager.rfa_vp_template
        if not t.startswith("mine:"):
            self.report({"WARNING"}, "Only your own templates can be deleted")
            return {"CANCELLED"}
        vp.delete_template(_folders(context)[1], t[5:])
        context.window_manager.rfa_vp_template = ""
        return {"FINISHED"}


class RFA_OT_vp_write(bpy.types.Operator):
    """Write the vehicle's parameters into vehicles.xtbl, where the game reads it with RFA-Mod (at its next start)"""
    bl_idname = "rfa.vp_write"
    bl_label = "Write to Game"

    def execute(self, context):
        from . import blender_vehicle_anim as bva
        from . import blender_sounds as bs
        if _work["root"] is None:
            self.report({"WARNING"}, "Edit a vehicle first")
            return {"CANCELLED"}
        text = _table(context)
        span = vp.entry_span(text, _work["vehicle"])
        if span is None:
            self.report({"ERROR"}, f"vehicles.xtbl has no vehicle named {_work['vehicle']}")
            return {"CANCELLED"}
        path = bva.write_table(_folders(context)[1], "vehicles.xtbl", text[:span[0]] + vp.entry_text(_work["root"]) + text[span[1]:])
        bs._vehicle_cache.pop(_work["vehicle"], None)
        fill(context, _work["vehicle"], _work["root"])
        from . import blender_live
        blender_live.vehicles_changed(context, _work["vehicle"], self.report)
        bs.redraw(context)
        self.report({"INFO"}, f"{_work['vehicle']} written to {path}; the game uses it from its next start")
        return {"FINISHED"}


class RFA_OT_vp_revert(bpy.types.Operator):
    """Forget the changes not written yet: read the vehicle from the table again"""
    bl_idname = "rfa.vp_revert"
    bl_label = "Revert"

    def execute(self, context):
        if not _work["vehicle"]:
            return {"CANCELLED"}
        return _load(self, context, _work["vehicle"])


def _choice_items(self, context):
    it = _current_choice_item(context)
    _kept["choices"] = [(c, c, "") for c in (it.choices.split("|") if it and it.choices else [])] or [("", "(no choices)", "")]
    return _kept["choices"]


def _current_choice_item(context):
    wm = context.window_manager
    return next((i for i in wm.rfa_vp_items if i.key == wm.rfa_vp_choosing), None)


class RFA_OT_vp_choose(bpy.types.Operator):
    """Pick one of this setting's choices"""
    bl_idname = "rfa.vp_choose"
    bl_label = "Choose"
    bl_property = "choice"
    key: StringProperty()
    choice: EnumProperty(items=_choice_items)

    def invoke(self, context, event):
        context.window_manager.rfa_vp_choosing = self.key
        context.window_manager.invoke_search_popup(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        it = _current_choice_item(context)
        if it is not None and self.choice:
            it.sval = self.choice
        return {"FINISHED"}


class RFA_OT_vp_reset(bpy.types.Operator):
    """Put this setting back to what the table has now"""
    bl_idname = "rfa.vp_reset"
    bl_label = "Reset"
    key: StringProperty()

    def execute(self, context):
        it = next((i for i in context.window_manager.rfa_vp_items if i.key == self.key), None)
        if it is not None and self.key in _work["original"]:
            it.sval = _work["original"][self.key]
        return {"FINISHED"}


# ---------------------------------------------------------------- the panel


SHOWN = vp.SECTIONS + (("OTHER", "The vehicle's own (never copied by templates)", ()),)


def _section_items(self, context):
    return [(ALL, "All sections", "")] + [(k, label, "") for k, label, _ in SHOWN]


class RFA_PT_vehicle_params(bpy.types.Panel):
    bl_label = "Vehicle Parameters"
    bl_idname = "RFA_PT_vehicle_params"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "RFA Tuning"

    def draw(self, context):
        layout = self.layout
        wm = context.window_manager
        row = layout.row(align=True)
        row.operator(RFA_OT_vp_edit_selected.bl_idname, icon="RESTRICT_SELECT_OFF")
        row.operator(RFA_OT_vp_edit.bl_idname, text="Any Vehicle...", icon="VIEWZOOM")
        layout.prop(wm, "rfa_vp_follow")
        if _work["root"] is None or not len(wm.rfa_vp_items):
            layout.label(text="Pick a vehicle to edit its parameters", icon="INFO")
            return
        changed = sum(1 for i in wm.rfa_vp_items if i.changed)
        layout.label(text=f"{_work['vehicle']}" + (f"  ({changed} changed, not written)" if changed else ""), icon="AUTO")

        box = layout.box()
        box.label(text="Templates", icon="PRESET")
        row = box.row(align=True)
        t = wm.rfa_vp_template
        row.operator(RFA_OT_vp_template_pick.bl_idname, text=(t.split(":", 1)[1] if t else "Pick a template..."), icon="VIEWZOOM")
        row.operator(RFA_OT_vp_template_delete.bl_idname, text="", icon="TRASH")
        box.label(text="Sections to copy:")
        box.grid_flow(columns=2, align=True).prop(wm, "rfa_vp_apply", expand=True)
        row = box.row(align=True)
        row.operator(RFA_OT_vp_template_apply.bl_idname, icon="IMPORT")
        row.operator(RFA_OT_vp_template_save.bl_idname, icon="FILE_TICK")

        row = layout.row(align=True)
        row.operator(RFA_OT_vp_write.bl_idname, icon="EXPORT")
        row.operator(RFA_OT_vp_revert.bl_idname, icon="LOOP_BACK")
        from . import blender_live
        blender_live.draw(layout, context)

        layout.prop(wm, "rfa_vp_filter", text="", icon="VIEWZOOM")
        layout.prop(wm, "rfa_vp_section", text="")
        words = wm.rfa_vp_filter.lower().split()
        sec = wm.rfa_vp_section
        shown = 0
        for key, label, _ in SHOWN:
            if sec != ALL and sec != key:
                continue
            items = [i for i in wm.rfa_vp_items if i.section == key and all(w in (i.label + " " + i.key).lower() for w in words)]
            if not items:
                continue
            col = layout.box().column(align=True)
            col.label(text=label)
            for it in items:
                shown += 1
                row = col.row(align=True)
                split = row.split(factor=0.5, align=True)
                split.label(text=it.label, icon="DOT" if it.changed else "NONE")
                sub = split.row(align=True)
                sub.enabled = not it.own
                if it.kind == "Float":
                    sub.prop(it, "fval", text="")
                elif it.kind == "Int":
                    sub.prop(it, "ival", text="")
                elif it.kind == "Selection" and it.choices:
                    sub.operator(RFA_OT_vp_choose.bl_idname, text=it.sval or "-", icon="DOWNARROW_HLT").key = it.key
                else:
                    sub.prop(it, "sval", text="")
                r = row.row(align=True)
                r.enabled = it.changed
                r.operator(RFA_OT_vp_reset.bl_idname, text="", icon="LOOP_BACK", emboss=False).key = it.key
        if not shown:
            layout.label(text="Nothing matches")


def _follow():
    """Every half second: when the vehicle selected in the scene isn't the one edited here and nothing here is
    waiting to be written, edit the selected one (so the tabs all show the same vehicle)."""
    try:
        context = bpy.context
        wm = context.window_manager
        if not wm.rfa_vp_follow or context.mode not in ("OBJECT", "POSE", "EDIT_MESH", "EDIT_ARMATURE"):
            return 0.5
        from . import blender_sounds as bs
        vehicle, _ = bs.current_vehicle(context)
        if vehicle and vehicle.lower() != _work["vehicle"].lower() and not any(i.changed for i in wm.rfa_vp_items):
            root = vp.parse_entry(_table(context), vehicle)
            if root is not None:
                fill(context, vehicle, root)
                bs.redraw(context)
    except Exception as e:  # never let the timer die
        print("RFA Animation Tools: following the selected vehicle:", e)
    return 0.5


classes = (RFA_VParam, RFA_OT_vp_edit, RFA_OT_vp_edit_selected, RFA_OT_vp_template_pick, RFA_OT_vp_template_apply,
           RFA_OT_vp_template_save, RFA_OT_vp_template_delete, RFA_OT_vp_write, RFA_OT_vp_revert, RFA_OT_vp_choose,
           RFA_OT_vp_reset, RFA_PT_vehicle_params)


def register():
    for c in classes:
        bpy.utils.register_class(c)
    wm = bpy.types.WindowManager
    wm.rfa_vp_items = CollectionProperty(type=RFA_VParam)
    wm.rfa_vp_filter = StringProperty(name="Search", options={"TEXTEDIT_UPDATE"}, description="Words the setting's name must have")
    wm.rfa_vp_section = EnumProperty(name="Section", items=_section_items)
    wm.rfa_vp_template = StringProperty(name="Template")
    wm.rfa_vp_choosing = StringProperty()
    wm.rfa_vp_follow = BoolProperty(name="Follow selected vehicle", default=True,
                                    description="Edit whichever vehicle is selected in the scene (unless changes here are waiting to be written)")
    if not bpy.app.timers.is_registered(_follow):
        bpy.app.timers.register(_follow, first_interval=1.0, persistent=True)
    wm.rfa_vp_apply = EnumProperty(name="Copy", options={"ENUM_FLAG"},
                                   items=[(k, label, f"Copy the template's {label.lower()}") for k, label, _ in vp.SECTIONS],
                                   default={"ENGINE", "TRANSMISSION", "WHEELS", "STEERING", "HANDLING"})


def unregister():
    if bpy.app.timers.is_registered(_follow):
        bpy.app.timers.unregister(_follow)
    wm = bpy.types.WindowManager
    for p in ("rfa_vp_follow", "rfa_vp_apply", "rfa_vp_choosing", "rfa_vp_template", "rfa_vp_section", "rfa_vp_filter", "rfa_vp_items"):
        if hasattr(wm, p):
            delattr(wm, p)
    for c in reversed(classes):
        bpy.utils.unregister_class(c)
