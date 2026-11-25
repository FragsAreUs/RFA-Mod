"""RFA Animation Tools: make and change Red Faction: Armageddon's animations in Blender.

Import a skeleton (.rig_pc) as an armature, a character (.ccmesh_pc) skinned to it, and the game's
animations (.anim_pc) as actions; animate; export an action as an .anim_pc, to a file or straight
into the mod's overrides (in place of the game's animation of that name). The game's archives are
read directly: set the game folder in the add-on's preferences.

Part of RFA-Mod (tools/blender/rfa_animation_tools). See README.md.
"""

bl_info = {
    "name": "RFA Animation Tools",
    "author": "RFA-Mod",
    "version": (1, 0, 0),
    "blender": (3, 6, 0),
    "location": "View3D > Sidebar > RFA Anim; File > Import / Export",
    "description": "Red Faction: Armageddon skeletons, characters and animations (.rig_pc, .ccmesh_pc, .anim_pc)",
    "category": "Import-Export",
}

import os

import bpy
from bpy.props import BoolProperty, EnumProperty, FloatProperty, StringProperty
from bpy_extras.io_utils import ExportHelper, ImportHelper

from . import blender_io as bio
from . import rfa_formats as rf

DEFAULT_GAME = r"D:\Steam\steamapps\common\red faction armageddon"


def prefs(context):
    return context.preferences.addons[__package__].preferences


def game_folder(context):
    return bpy.path.abspath(prefs(context).game_folder)


def mod_folder(context):
    p = prefs(context)
    return bpy.path.abspath(p.mod_folder or p.game_folder)


def active_rfa_armature(context):
    obj = context.active_object
    if bio.is_rfa_armature(obj):
        return obj
    if obj is not None and obj.parent is not None and bio.is_rfa_armature(obj.parent):
        return obj.parent
    return None


class RFA_Preferences(bpy.types.AddonPreferences):
    bl_idname = __package__

    game_folder: StringProperty(name="Game folder", subtype="DIR_PATH", default=DEFAULT_GAME,
                                description="Red Faction: Armageddon's folder (the one with build\\pc\\cache)")
    mod_folder: StringProperty(name="Mod folder", subtype="DIR_PATH", default="",
                               description="The folder whose overrides\\ the game reads (RFA-Mod installs into the game folder; empty: the game folder)")

    def draw(self, context):
        self.layout.prop(self, "game_folder")
        self.layout.prop(self, "mod_folder")


# ---------------------------------------------------------------- search lists (kept: Blender needs the items alive)

_lists = {}


def _items(context, key, archives, extension):
    folder = game_folder(context)
    cache_key = (key, folder.lower())
    if cache_key not in _lists:
        try:
            files = bio.GameFiles.files(folder, archives, extension)
        except Exception as ex:  # a broken or missing archive: say so in the list
            files = []
            print(f"RFA Animation Tools: {ex}")
        seen = set()
        items = []
        for rel, name in sorted(files, key=lambda f: f[1].lower()):
            if name.lower() in seen:
                continue
            seen.add(name.lower())
            items.append((rel + "|" + name, name[: -len(extension)], rel))
        _lists[cache_key] = items or [("", "(nothing found: check the game folder in the add-on's preferences)", "")]
    return _lists[cache_key]


def _anim_items(self, context):
    return _items(context, "anims", bio.GameFiles.ANIMS, ".anim_pc")


def _rig_items(self, context):
    return _items(context, "rigs", bio.GameFiles.RIGS, ".rig_pc")


def _human_items(self, context):
    return _items(context, "humans", bio.GameFiles.HUMANS, ".str2_pc")


def _split(choice):
    rel, _, name = choice.partition("|")
    return rel, name


# ---------------------------------------------------------------- skeletons and characters


def _import_rig(context, data, name):
    rig = rf.Rig.read(data)
    obj = bio.make_armature(context, rig, name)
    return obj, rig


class RFA_OT_import_rig_file(bpy.types.Operator, ImportHelper):
    """Import a skeleton (.rig_pc) as an armature"""
    bl_idname = "rfa.import_rig_file"
    bl_label = "RFA Skeleton (.rig_pc)"
    bl_options = {"REGISTER", "UNDO"}
    filename_ext = ".rig_pc"
    filter_glob: StringProperty(default="*.rig_pc", options={"HIDDEN"})

    def execute(self, context):
        with open(self.filepath, "rb") as f:
            data = f.read()
        name = os.path.splitext(os.path.basename(self.filepath))[0]
        try:
            obj, rig = _import_rig(context, data, name)
        except ValueError as ex:
            self.report({"ERROR"}, f"{name}: {ex}")
            return {"CANCELLED"}
        self.report({"INFO"}, f"{name}: {len(rig.bones)} bones, {len(rig.tags)} tags")
        return {"FINISHED"}


class RFA_OT_import_rig_game(bpy.types.Operator):
    """Import one of the game's skeletons (misc.vpp_pc) as an armature"""
    bl_idname = "rfa.import_rig_game"
    bl_label = "Skeleton from the Game"
    bl_options = {"REGISTER", "UNDO"}
    bl_property = "choice"
    choice: EnumProperty(name="Skeleton", items=_rig_items)

    def invoke(self, context, event):
        context.window_manager.invoke_search_popup(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        rel, name = _split(self.choice)
        if not name:
            self.report({"ERROR"}, "No skeletons found: set the game folder in the add-on's preferences")
            return {"CANCELLED"}
        pf = bio.GameFiles.archive(game_folder(context), rel)
        try:
            obj, rig = _import_rig(context, pf.read(name), name[: -len(".rig_pc")])
        except (ValueError, KeyError) as ex:
            self.report({"ERROR"}, f"{name}: {ex}")
            return {"CANCELLED"}
        self.report({"INFO"}, f"{name}: {len(rig.bones)} bones, {len(rig.tags)} tags")
        return {"FINISHED"}


class RFA_OT_import_character_game(bpy.types.Operator):
    """Import one of the game's characters (humans.vpp_pc): its skeleton as an armature (or the
    selected RFA armature) and its meshes skinned to it"""
    bl_idname = "rfa.import_character_game"
    bl_label = "Character from the Game"
    bl_options = {"REGISTER", "UNDO"}
    bl_property = "choice"
    choice: EnumProperty(name="Character", items=_human_items)

    def invoke(self, context, event):
        context.window_manager.invoke_search_popup(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        rel, name = _split(self.choice)
        if not name:
            self.report({"ERROR"}, "No characters found: set the game folder in the add-on's preferences")
            return {"CANCELLED"}
        folder = game_folder(context)
        try:
            container = bio.GameFiles.archive(folder, rel).container(name)
        except Exception as ex:
            self.report({"ERROR"}, f"{name}: {ex}")
            return {"CANCELLED"}
        meshes = [n for n in container.names() if n.lower().endswith(".ccmesh_pc")]
        if not meshes:
            self.report({"ERROR"}, f"{name} holds no character mesh")
            return {"CANCELLED"}

        arm_obj = active_rfa_armature(context)
        rig = None
        if arm_obj is None:
            # The skeleton named as the mesh (or the container), in misc.vpp_pc.
            for candidate in [os.path.splitext(m)[0] + ".rig_pc" for m in meshes] + [os.path.splitext(name)[0] + ".rig_pc"]:
                rig_rel, pf = bio.GameFiles.find(folder, bio.GameFiles.RIGS, candidate)
                if pf is not None:
                    arm_obj, rig = _import_rig(context, pf.read(candidate), os.path.splitext(candidate)[0])
                    break
            if arm_obj is None:
                self.report({"ERROR"}, f"No skeleton named as {name}'s meshes: import its skeleton first, select it, and import the character again")
                return {"CANCELLED"}
        else:
            rig_name = arm_obj.data.get("rfa_rig", "")
            _, pf = bio.GameFiles.find(folder, bio.GameFiles.RIGS, rig_name + ".rig_pc")
            if pf is not None:
                rig = rf.Rig.read(pf.read(rig_name + ".rig_pc"))

        made = 0
        for mesh_name in meshes:
            data_name = rf.mesh_data_name(mesh_name)
            if not container.has(data_name):
                continue
            try:
                mesh = rf.CharacterMesh.read(container.read(mesh_name), container.read(data_name), rig)
            except Exception as ex:
                self.report({"WARNING"}, f"{mesh_name}: {ex}")
                continue
            bio.make_mesh(context, mesh, os.path.splitext(mesh_name)[0], arm_obj)
            made += 1
        context.view_layer.objects.active = arm_obj
        self.report({"INFO"}, f"{name}: {made} mesh(es) on {arm_obj.name}")
        return {"FINISHED"} if made else {"CANCELLED"}


# ---------------------------------------------------------------- animations in


def _apply(self, context, data, name):
    arm_obj = active_rfa_armature(context)
    if arm_obj is None:
        self.report({"ERROR"}, "Select an RFA armature first (import a skeleton or a character)")
        return {"CANCELLED"}
    try:
        anim = rf.Animation.read(data)
    except ValueError as ex:
        self.report({"ERROR"}, f"{name}: {ex}")
        return {"CANCELLED"}
    bio.apply_animation(context, arm_obj, anim, name, self.report)
    self.report({"INFO"}, f"{name}: {anim.frames} frames, {len(anim.tracks)} bones")
    return {"FINISHED"}


class RFA_OT_import_anim_file(bpy.types.Operator, ImportHelper):
    """Import an animation (.anim_pc) onto the selected RFA armature"""
    bl_idname = "rfa.import_anim_file"
    bl_label = "RFA Animation (.anim_pc)"
    bl_options = {"REGISTER", "UNDO"}
    filename_ext = ".anim_pc"
    filter_glob: StringProperty(default="*.anim_pc", options={"HIDDEN"})

    def execute(self, context):
        with open(self.filepath, "rb") as f:
            data = f.read()
        return _apply(self, context, data, os.path.splitext(os.path.basename(self.filepath))[0])


class RFA_OT_import_anim_game(bpy.types.Operator):
    """Import one of the game's animations (anims.vpp_pc) onto the selected RFA armature"""
    bl_idname = "rfa.import_anim_game"
    bl_label = "Animation from the Game"
    bl_options = {"REGISTER", "UNDO"}
    bl_property = "choice"
    choice: EnumProperty(name="Animation", items=_anim_items)

    def invoke(self, context, event):
        context.window_manager.invoke_search_popup(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        rel, name = _split(self.choice)
        if not name:
            self.report({"ERROR"}, "No animations found: set the game folder in the add-on's preferences")
            return {"CANCELLED"}
        data = bio.GameFiles.archive(game_folder(context), rel).read(name)
        return _apply(self, context, data, name[: -len(".anim_pc")])


# ---------------------------------------------------------------- animations out


class _ExportSettings:
    use_action_range: BoolProperty(name="Action's frame range", default=False,
                                   description="Export the action's own frame range (else the scene's start to end)")
    bones: EnumProperty(name="Bones", default="ANIMATED", items=[
        ("ANIMATED", "Keyed bones", "The bones the action has keys for (the others keep their rest pose in the game)"),
        ("ALL", "All bones", "Every bone of the skeleton"),
    ])
    every_frame: BoolProperty(name="A key every frame", default=False,
                              description="Keep a key on every frame (else only those the others can't be worked out from)")
    rotation_tolerance: FloatProperty(name="Rotation tolerance", default=0.1, min=0.0, max=5.0, unit="NONE",
                                      description="Degrees a dropped rotation key may be off the line between those kept")
    position_tolerance: FloatProperty(name="Position tolerance", default=0.0005, min=0.0, max=0.05, precision=4,
                                      description="Metres a dropped position key may be off the line between those kept")


def _export(self, context, path):
    arm_obj = active_rfa_armature(context)
    if arm_obj is None:
        self.report({"ERROR"}, "Select the RFA armature whose action to export")
        return None
    try:
        anim, header = bio.build_animation(context, arm_obj, self, self.report)
        data = rf.write_animation(anim, header)
        rf.Animation.read(data)  # reads back as the game would
    except ValueError as ex:
        self.report({"ERROR"}, str(ex))
        return None
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)
    keys = sum(len(t.rotations) + len(t.positions) for t in anim.tracks)
    self.report({"INFO"}, f"{os.path.basename(path)}: {anim.frames} frames, {len(anim.tracks)} bones, {keys} keys, {len(data):,} bytes")
    return anim


class RFA_OT_export_anim_file(bpy.types.Operator, ExportHelper, _ExportSettings):
    """Export the selected RFA armature's action as an animation (.anim_pc)"""
    bl_idname = "rfa.export_anim_file"
    bl_label = "RFA Animation (.anim_pc)"
    filename_ext = ".anim_pc"
    filter_glob: StringProperty(default="*.anim_pc", options={"HIDDEN"})

    def invoke(self, context, event):
        arm_obj = active_rfa_armature(context)
        action = arm_obj.animation_data.action if arm_obj and arm_obj.animation_data else None
        if action is not None:
            self.filepath = action.get("rfa_name", action.name) + ".anim_pc"
        return ExportHelper.invoke(self, context, event)

    def execute(self, context):
        return {"FINISHED"} if _export(self, context, self.filepath) else {"CANCELLED"}


class RFA_OT_export_anim_override(bpy.types.Operator, _ExportSettings):
    """Export the action into the mod's overrides, in place of the game's animation of that name
    (the game uses it from its next start)"""
    bl_idname = "rfa.export_anim_override"
    bl_label = "Export to Game (Override)"
    name: StringProperty(name="Animation", description="The game's animation it replaces (its file name without .anim_pc)")

    def invoke(self, context, event):
        arm_obj = active_rfa_armature(context)
        action = arm_obj.animation_data.action if arm_obj and arm_obj.animation_data else None
        if action is None:
            self.report({"ERROR"}, "Select an RFA armature with an action")
            return {"CANCELLED"}
        self.name = action.get("rfa_name", action.name)
        return context.window_manager.invoke_props_dialog(self, width=380)

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "name")
        for prop in ("use_action_range", "bones", "every_frame", "rotation_tolerance", "position_tolerance"):
            layout.prop(self, prop)

    def execute(self, context):
        file_name = self.name.strip()
        if not file_name.lower().endswith(".anim_pc"):
            file_name += ".anim_pc"
        rel, _ = bio.GameFiles.find(game_folder(context), bio.GameFiles.ANIMS, file_name)
        if rel is None:
            self.report({"ERROR"}, f"The game has no {file_name} in its animation archives to replace "
                                   "(an animation inside a container can't be replaced one file at a time). Export to a file instead.")
            return {"CANCELLED"}
        path = bio.override_path(mod_folder(context), rel, file_name)
        if _export(self, context, path) is None:
            return {"CANCELLED"}
        self.report({"INFO"}, f"Written to {path}: the game uses it from its next start")
        return {"FINISHED"}


class RFA_OT_restore_anim(bpy.types.Operator):
    """Remove the override of the game's animation of the action's name, so the game uses its own again"""
    bl_idname = "rfa.restore_anim"
    bl_label = "Restore the Game's Own"

    def execute(self, context):
        arm_obj = active_rfa_armature(context)
        action = arm_obj.animation_data.action if arm_obj and arm_obj.animation_data else None
        if action is None:
            self.report({"ERROR"}, "Select an RFA armature with an action")
            return {"CANCELLED"}
        file_name = action.get("rfa_name", action.name) + ".anim_pc"
        rel, _ = bio.GameFiles.find(game_folder(context), bio.GameFiles.ANIMS, file_name)
        path = bio.override_path(mod_folder(context), rel, file_name) if rel else None
        if not path or not os.path.isfile(path):
            self.report({"INFO"}, f"There is no override of {file_name}")
            return {"CANCELLED"}
        os.remove(path)
        self.report({"INFO"}, f"Removed {path}: the game uses its own {file_name} from its next start")
        return {"FINISHED"}


class RFA_OT_refresh(bpy.types.Operator):
    """Read the game's archives again (after changing the game folder)"""
    bl_idname = "rfa.refresh"
    bl_label = "Read the Game's Files Again"

    def execute(self, context):
        bio.GameFiles.forget()
        _lists.clear()
        return {"FINISHED"}


# ---------------------------------------------------------------- the panel


class RFA_PT_panel(bpy.types.Panel):
    bl_label = "RFA Animation"
    bl_idname = "RFA_PT_animation"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "RFA Anim"

    def draw(self, context):
        layout = self.layout
        col = layout.column(align=True)
        col.label(text="Skeletons and characters")
        col.operator(RFA_OT_import_character_game.bl_idname, icon="ARMATURE_DATA")
        col.operator(RFA_OT_import_rig_game.bl_idname, icon="BONE_DATA")

        arm_obj = active_rfa_armature(context)
        layout.separator()
        col = layout.column(align=True)
        col.label(text="Animations")
        if arm_obj is None:
            col.label(text="Select an RFA armature", icon="INFO")
            return
        col.label(text=f"{arm_obj.name}: {len(bio.bone_names(arm_obj))} bones")
        col.operator(RFA_OT_import_anim_game.bl_idname, icon="ACTION")
        action = arm_obj.animation_data.action if arm_obj.animation_data else None
        if action is not None:
            box = layout.box()
            box.label(text=f"Action: {action.name}", icon="ACTION")
            box.label(text=f"Frames {context.scene.frame_start}-{context.scene.frame_end} at {context.scene.render.fps} fps")
            row = box.row(align=True)
            row.operator(RFA_OT_export_anim_override.bl_idname, icon="EXPORT")
            box.operator(RFA_OT_export_anim_file.bl_idname, text="Export to File...", icon="FILE_TICK")
            box.operator(RFA_OT_restore_anim.bl_idname, icon="LOOP_BACK")
        layout.operator(RFA_OT_refresh.bl_idname, icon="FILE_REFRESH")


def _menu_import(self, context):
    self.layout.operator(RFA_OT_import_rig_file.bl_idname)
    self.layout.operator(RFA_OT_import_anim_file.bl_idname)


def _menu_export(self, context):
    self.layout.operator(RFA_OT_export_anim_file.bl_idname)


classes = (
    RFA_Preferences,
    RFA_OT_import_rig_file,
    RFA_OT_import_rig_game,
    RFA_OT_import_character_game,
    RFA_OT_import_anim_file,
    RFA_OT_import_anim_game,
    RFA_OT_export_anim_file,
    RFA_OT_export_anim_override,
    RFA_OT_restore_anim,
    RFA_OT_refresh,
    RFA_PT_panel,
)


def register():
    for c in classes:
        bpy.utils.register_class(c)
    bpy.types.TOPBAR_MT_file_import.append(_menu_import)
    bpy.types.TOPBAR_MT_file_export.append(_menu_export)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(_menu_export)
    bpy.types.TOPBAR_MT_file_import.remove(_menu_import)
    for c in reversed(classes):
        bpy.utils.unregister_class(c)
