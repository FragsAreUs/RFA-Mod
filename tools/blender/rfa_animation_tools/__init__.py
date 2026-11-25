"""RFA Animation Tools: make and change Red Faction: Armageddon's animations in Blender.

Import a skeleton (.rig_pc) as an armature, a character (.ccmesh_pc) skinned to it, and the game's
animations (.anim_pc, from the animation archives and from inside the containers) as actions;
animate; export an action as an .anim_pc, to a file or straight into RFA-Mod's mods\\animations (in
place of the game's animation of that name, wherever the game keeps it, or as a new one). The game's
archives are read directly: set the game folder in the add-on's preferences.

Part of RFA-Mod (tools/blender/rfa_animation_tools). See README.md.
"""

bl_info = {
    "name": "RFA Animation Tools",
    "author": "RFA-Mod",
    "version": (1, 2, 0),
    "blender": (3, 6, 0),
    "location": "View3D > Sidebar > RFA Anim; File > Import / Export",
    "description": "Red Faction: Armageddon skeletons, characters and animations (.rig_pc, .ccmesh_pc, .anim_pc), and new vehicles",
    "category": "Import-Export",
}

import os

import bpy
from bpy.props import BoolProperty, EnumProperty, FloatProperty, StringProperty
from bpy_extras.io_utils import ExportHelper, ImportHelper

from . import blender_io as bio
from . import blender_vehicle as bv
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
                               description="The folder RFA-Mod is installed in, holding rfa_mods.ini and mods\\ (RFA-Mod installs into the game folder; empty: the game folder)")

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
    folder = game_folder(context)
    cache_key = ("all anims", folder.lower())
    if cache_key not in _lists:
        try:
            found = bio.Animations.all(folder)
        except Exception as ex:
            found = []
            print(f"RFA Animation Tools: {ex}")
        items = [(f"{rel}|{container}|{name}", name[: -len(".anim_pc")],
                  f"{rel} > {container}" if container else rel)
                 for rel, container, name in sorted(found, key=lambda f: f[2].lower())]
        _lists[cache_key] = items or [("", "(nothing found: check the game folder in the add-on's preferences)", "")]
    return _lists[cache_key]


def _rig_items(self, context):
    return _items(context, "rigs", bio.GameFiles.RIGS, ".rig_pc")


def _human_items(self, context):
    return _items(context, "humans", bio.GameFiles.HUMANS, ".str2_pc")


def _split(choice):
    rel, _, name = choice.partition("|")
    return rel, name


def _split3(choice):
    """archive|container|name (the container empty for the archive's own files)."""
    parts = choice.split("|")
    if len(parts) == 2:
        return parts[0], "", parts[1]
    if len(parts) == 3:
        return tuple(parts)
    return "", "", ""


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
    textures: BoolProperty(name="Textures", default=True,
                           description="Bring in the materials' textures (colour, normal and specular maps), packed into the .blend")

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

        textures = bio.Textures(folder, container) if self.textures else None
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
            bio.make_mesh(context, mesh, os.path.splitext(mesh_name)[0], arm_obj, textures)
            made += 1
        context.view_layer.objects.active = arm_obj
        if textures is not None and textures.missing:
            self.report({"WARNING"}, f"{name}: textures not found in the game's files: {', '.join(sorted(textures.missing))}")
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
    """Import one of the game's animations onto the selected RFA armature: the animation archives'
    own, and those inside containers (the walkers', the vehicles', a level's monsters')"""
    bl_idname = "rfa.import_anim_game"
    bl_label = "Animation from the Game"
    bl_options = {"REGISTER", "UNDO"}
    bl_property = "choice"
    choice: EnumProperty(name="Animation", items=_anim_items)

    def invoke(self, context, event):
        context.window_manager.invoke_search_popup(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        rel, container, name = _split3(self.choice)
        if not name:
            self.report({"ERROR"}, "No animations found: set the game folder in the add-on's preferences")
            return {"CANCELLED"}
        try:
            data = bio.Animations.read(game_folder(context), rel, container, name)
        except Exception as ex:
            self.report({"ERROR"}, f"{name}: {ex}")
            return {"CANCELLED"}
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
    """Export the action into RFA-Mod's mods\\animations: the game uses it from its next start, in place
    of its own animation of that name wherever it keeps it, or as a new animation"""
    bl_idname = "rfa.export_anim_override"
    bl_label = "Export to Game"
    name: StringProperty(name="Animation", description="The game's animation it replaces, or a new name (the file name without .anim_pc)")

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
        rel, container = bio.Animations.find(game_folder(context), self.name.strip())
        if rel is None:
            layout.label(text="A new animation: an anim set's state or action has to name it", icon="INFO")
        else:
            layout.label(text=f"In place of the game's ({container or rel})", icon="CHECKMARK")
        for prop in ("use_action_range", "bones", "every_frame", "rotation_tolerance", "position_tolerance"):
            layout.prop(self, prop)

    def execute(self, context):
        name = self.name.strip()
        if name.lower().endswith(".anim_pc"):
            name = name[:-len(".anim_pc")]
        if not name or len(name) > bio.LONGEST_NAME or any(c in name for c in '\\/:*?"<>|'):
            self.report({"ERROR"}, f"Give the animation a file name of 1 to {bio.LONGEST_NAME} characters")
            return {"CANCELLED"}
        if not os.path.isdir(mod_folder(context)):
            self.report({"ERROR"}, f"No folder {mod_folder(context)}: set the mod folder (or the game folder) in the add-on's preferences")
            return {"CANCELLED"}
        path = bio.animation_path(mod_folder(context), name)
        if _export(self, context, path) is None:
            return {"CANCELLED"}
        rel, container = bio.Animations.find(game_folder(context), name)
        if rel is None:
            self.report({"INFO"}, f"Written to {path}: a new animation, which plays where an anim set names it "
                                  f"({name}.animx), from the game's next start")
        else:
            self.report({"INFO"}, f"Written to {path}: the game uses it in place of its own from its next start")
        return {"FINISHED"}


class RFA_OT_restore_anim(bpy.types.Operator):
    """Remove the action's animation from mods\\animations (and an override an earlier version of the
    add-on wrote), so the game uses its own again"""
    bl_idname = "rfa.restore_anim"
    bl_label = "Restore the Game's Own"

    def execute(self, context):
        arm_obj = active_rfa_armature(context)
        action = arm_obj.animation_data.action if arm_obj and arm_obj.animation_data else None
        if action is None:
            self.report({"ERROR"}, "Select an RFA armature with an action")
            return {"CANCELLED"}
        name = action.get("rfa_name", action.name)
        file_name = name + ".anim_pc"
        paths = [bio.animation_path(mod_folder(context), name)]
        rel, _ = bio.GameFiles.find(game_folder(context), bio.GameFiles.ANIMS, file_name)
        if rel:
            paths.append(bio.override_path(mod_folder(context), rel, file_name))
        removed = [p for p in paths if os.path.isfile(p)]
        if not removed:
            self.report({"INFO"}, f"There is no animation of yours named {file_name}")
            return {"CANCELLED"}
        for p in removed:
            os.remove(p)
        self.report({"INFO"}, f"Removed {', '.join(removed)}: from its next start the game uses its own {file_name}, if it has one")
        return {"FINISHED"}


class RFA_OT_refresh(bpy.types.Operator):
    """Read the game's archives again (after changing the game folder)"""
    bl_idname = "rfa.refresh"
    bl_label = "Read the Game's Files Again"

    def execute(self, context):
        bio.GameFiles.forget()
        _lists.clear()
        return {"FINISHED"}


# ---------------------------------------------------------------- vehicles


def _kind_items(self, context):
    folder = game_folder(context)
    key = ("vehicle kinds", folder.lower())
    if key not in _lists:
        try:
            found = bv.kinds(folder)
        except Exception as ex:
            found = []
            print(f"RFA Animation Tools: {ex}")
        _lists[key] = [(c, n, f"Built from {n}: its parts, seats, handling and entering") for c, n in found] or \
                      [("", "(no vehicles found: check the game folder in the add-on's preferences)", "")]
    return _lists[key]


def _vehicle_name_ok(name):
    return bool(name) and len(name) <= 40 and all(c.isalnum() or c in "_-" for c in name)


class RFA_OT_vehicle_new(bpy.types.Operator):
    """Start a new vehicle from one of the game's: a kit of its parts (body, wheels, doors, turrets...),
    collision, seats and points, to model anew"""
    bl_idname = "rfa.vehicle_new"
    bl_label = "New Vehicle"
    bl_options = {"REGISTER", "UNDO"}
    kind: EnumProperty(name="Kind", items=_kind_items, description="The game's vehicle whose working parts it has")
    name: StringProperty(name="Name", default="my_vehicle", description="The new vehicle's name (letters, digits, _ and -)")
    textures: BoolProperty(name="Textures", default=True, description="Bring in the kind's textures (to see it as the game has it)")

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self, width=380)

    def execute(self, context):
        if not self.kind:
            self.report({"ERROR"}, "No vehicles found: set the game folder in the add-on's preferences")
            return {"CANCELLED"}
        if not _vehicle_name_ok(self.name):
            self.report({"ERROR"}, "Name the vehicle with letters, digits, _ and - (up to 40)")
            return {"CANCELLED"}
        try:
            root, missing = bv.make_kit(context, game_folder(context), self.kind, self.name, self.textures)
        except Exception as ex:
            self.report({"ERROR"}, f"{self.kind}: {ex}")
            return {"CANCELLED"}
        for o in context.selected_objects:
            o.select_set(False)
        root.select_set(True)
        context.view_layer.objects.active = root
        if missing:
            self.report({"WARNING"}, f"Textures not found: {', '.join(sorted(missing))}")
        self.report({"INFO"}, f"{self.name}: a kit from {self.kind[: -len('.ccar_pc.str2_pc')]}: model each part in place of its mesh, keeping the names")
        return {"FINISHED"}


class RFA_OT_vehicle_hull(bpy.types.Operator):
    """Add a collision hull round the active mesh's vertices, for the part it belongs to"""
    bl_idname = "rfa.vehicle_hull"
    bl_label = "Collision Hull from Mesh"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        obj = context.active_object
        if obj is None or obj.type != "MESH":
            self.report({"ERROR"}, "Select a mesh of a vehicle kit")
            return {"CANCELLED"}
        try:
            hull = bv.add_hull(context, obj, self.report)
        except Exception as ex:
            self.report({"ERROR"}, str(ex))
            return {"CANCELLED"}
        self.report({"INFO"}, f"{hull.name}: {len(hull.data.vertices)} corners")
        return {"FINISHED"}


class RFA_OT_refresh_materials(bpy.types.Operator):
    """Make the selected objects' game materials again with the game's look (colour x Diffuse_Color,
    dust, shine, normal map, glow): for things brought in by an earlier version, or to start over"""
    bl_idname = "rfa.refresh_materials"
    bl_label = "Game Look for Materials"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        done = 0
        roots = {bv.kit_root(o) for o in context.selected_objects + [context.active_object] if o is not None} - {None}
        try:
            for root in roots:
                done += bv.refresh_materials(root, game_folder(context))
        except Exception as ex:
            self.report({"ERROR"}, str(ex))
            return {"CANCELLED"}
        seen = set()
        for o in context.selected_objects:
            if o.type != "MESH" or bv.kit_root(o) is not None:
                continue
            for mat in o.data.materials:
                if mat is None or mat in seen:
                    continue
                seen.add(mat)
                game = bio.stored_material(mat)
                if game is not None:
                    bio.build_material(mat, game, bio.ImagesByName())
                    done += 1
        self.report({"INFO"} if done else {"WARNING"}, f"{done} material(s) made again" if done else
                    "No game materials among the selected objects")
        return {"FINISHED"} if done else {"CANCELLED"}


class RFA_OT_vehicle_export(bpy.types.Operator):
    """Write the vehicle into the game: its model, textures, collision and container, the asset list
    entry and its vehicles.xtbl entry (the kind's handling, seats and entering), from the game's next start"""
    bl_idname = "rfa.vehicle_export"
    bl_label = "Export Vehicle to Game"
    name: StringProperty(name="Name", description="The vehicle's name (its table entry and model)")
    display_name: StringProperty(name="Display name", description="The name the game shows")

    def invoke(self, context, event):
        root = bv.kit_root(context.active_object)
        if root is None:
            self.report({"ERROR"}, "Select a vehicle kit (or any object in one)")
            return {"CANCELLED"}
        self.name = root.get("rfa_vehicle_name", root.name)
        self.display_name = root.get("rfa_display_name", self.name.replace("_", " ").title())
        return context.window_manager.invoke_props_dialog(self, width=380)

    def execute(self, context):
        root = bv.kit_root(context.active_object)
        if root is None:
            self.report({"ERROR"}, "Select a vehicle kit (or any object in one)")
            return {"CANCELLED"}
        if not _vehicle_name_ok(self.name):
            self.report({"ERROR"}, "Name the vehicle with letters, digits, _ and - (up to 40)")
            return {"CANCELLED"}
        if self.name.lower() + ".ccar_pc.str2_pc" in {c.lower() for c, _ in bv.kinds(game_folder(context))}:
            self.report({"ERROR"}, f"The game has a vehicle model named {self.name}: give yours another name")
            return {"CANCELLED"}
        root["rfa_vehicle_name"] = self.name
        root["rfa_display_name"] = self.display_name
        try:
            files, name, kind = bv.build(context, root, game_folder(context), self.report)
            written = bv.install(game_folder(context), mod_folder(context), files, name, kind, self.display_name or name)
        except Exception as ex:
            import traceback
            traceback.print_exc()
            self.report({"ERROR"}, f"{self.name}: {ex}")
            return {"CANCELLED"}
        size = sum(len(d) for _, d in files)
        self.report({"INFO"}, f"{name}: {len(files)} files ({size:,} bytes) into the game, {len(written)} files written; "
                              f"spawn it as {name} from the game's next start")
        return {"FINISHED"}


class RFA_OT_vehicle_remove(bpy.types.Operator):
    """Take the vehicle out of the game again (its container, asset list entry and table entries)"""
    bl_idname = "rfa.vehicle_remove"
    bl_label = "Remove Vehicle from Game"

    def execute(self, context):
        root = bv.kit_root(context.active_object)
        if root is None:
            self.report({"ERROR"}, "Select a vehicle kit (or any object in one)")
            return {"CANCELLED"}
        name = root.get("rfa_vehicle_name", root.name)
        removed = bv.uninstall(game_folder(context), mod_folder(context), name)
        self.report({"INFO"}, f"{name}: " + (f"taken out ({len(removed)} files changed)" if removed else "isn't in the game"))
        return {"FINISHED"}


class RFA_PT_vehicle(bpy.types.Panel):
    bl_label = "RFA Vehicle"
    bl_idname = "RFA_PT_vehicle"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "RFA Anim"

    def draw(self, context):
        layout = self.layout
        layout.operator(RFA_OT_vehicle_new.bl_idname, icon="AUTO")
        root = bv.kit_root(context.active_object)
        if root is None:
            layout.label(text="Select a vehicle kit to export it", icon="INFO")
            return
        box = layout.box()
        box.label(text=f"{root.get('rfa_vehicle_name', root.name)} (from {root['rfa_vehicle'][: -len('.ccar_pc.str2_pc')]})", icon="AUTO")
        box.operator(RFA_OT_vehicle_hull.bl_idname, icon="MESH_ICOSPHERE")
        box.operator(RFA_OT_refresh_materials.bl_idname, icon="MATERIAL")
        box.operator(RFA_OT_vehicle_export.bl_idname, icon="EXPORT")
        box.operator(RFA_OT_vehicle_remove.bl_idname, icon="TRASH")


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
    RFA_OT_vehicle_new,
    RFA_OT_vehicle_hull,
    RFA_OT_refresh_materials,
    RFA_OT_vehicle_export,
    RFA_OT_vehicle_remove,
    RFA_OT_restore_anim,
    RFA_OT_refresh,
    RFA_PT_panel,
    RFA_PT_vehicle,
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
