"""Red Faction: Armageddon's skeletons, characters and animations in Blender, and back.

Spaces. The game is Y up and left-handed, Blender Z up and right-handed: a game point (x, y, z) is
Blender's (x, z, y), as the RFA-Mod level tools have it (swapping two axes changes the handedness, so
nothing is mirrored). A rotation's quaternion (x, y, z, w) is Blender's (w, -x, -z, -y).

Bones. The game's rest pose has no rotations: each bone sits at its rest position, turned as the
model is. So the armature's bones all point along Blender's +Y with no roll (their rest matrices
have no rotation), and a pose bone's own rotation and location are the game's key for that bone,
converted: rotation in its parent's space, location added to its rest offset from its parent (the
game's position keys), scale the same. The game's animations turn and move each bone between keys
in straight lines (slerp, lerp); the keys come in as linear ones.

The root track (the character's own movement over the clip: walking forward, jumping down) goes on
the armature object's location and rotation. The bones' order, which the files number them by, is
kept on the armature (rfa_bones), and what the files hold that Blender has no place for (the
header's two unknown bytes, the turn and movement over the clip, the bones' weight curves) on the
action, so exporting an imported animation gives them back.
"""

import json
import math
import os
import struct

import bpy
from mathutils import Matrix, Quaternion, Vector

from . import rfa_formats as rf

BONE_LENGTH = 0.06  # how long the armature's bones are drawn (they only show where each bone is)


# ---------------------------------------------------------------- spaces


def to_blender(p):
    return Vector((p[0], p[2], p[1]))


def from_blender(v):
    return (v[0], v[2], v[1])


def quat_to_blender(q):
    x, y, z, w = q
    return Quaternion((w, -x, -z, -y))


def quat_from_blender(q):
    return (-q.x, -q.z, -q.y, q.w)


def scale_to_blender(s):
    return Vector((s[0], s[2], s[1]))


# ---------------------------------------------------------------- action keys (Blender 3.6 to 5.x)


def _fcurves(obj, action, create=True):
    """The F-curves an object's action keys it with: the action's own (before Blender 4.4), or its
    layered keys' (4.4 and later: a slot for the object, a layer, a strip, its channel bag)."""
    if hasattr(action, "layers") and hasattr(action, "slots"):
        ad = obj.animation_data
        slot = getattr(ad, "action_slot", None)
        if slot is None:
            if not create:
                return []
            slot = action.slots[0] if len(action.slots) else action.slots.new(id_type="OBJECT", name=obj.name)
            ad.action_slot = slot
        if not len(action.layers):
            if not create:
                return []
            action.layers.new("Layer")
        layer = action.layers[0]
        if not len(layer.strips):
            if not create:
                return []
            layer.strips.new(type="KEYFRAME")
        bag = layer.strips[0].channelbag(slot, ensure=create)
        return bag.fcurves if bag else []
    return action.fcurves


def _key(fcurves, path, index, keys):
    """An F-curve of (frame, value) keys, linear between them."""
    curve = fcurves.new(path, index=index)
    curve.keyframe_points.add(len(keys))
    flat = []
    for frame, value in keys:
        flat += (float(frame), float(value))
    curve.keyframe_points.foreach_set("co", flat)
    for point in curve.keyframe_points:
        point.interpolation = "LINEAR"
    curve.update()
    return curve


# ---------------------------------------------------------------- rigs


def bone_names(arm_obj):
    names = arm_obj.data.get("rfa_bones")
    if names is None:
        return None
    return list(names)


def is_rfa_armature(obj):
    return obj is not None and obj.type == "ARMATURE" and obj.data.get("rfa_bones") is not None


def make_armature(context, rig, name):
    """An armature of a skeleton's bones in their rest pose, bone i named as the skeleton names it."""
    arm = bpy.data.armatures.new(name)
    arm.display_type = "STICK"
    obj = bpy.data.objects.new(name, arm)
    context.collection.objects.link(obj)
    obj.show_in_front = True
    context.view_layer.objects.active = obj
    obj.select_set(True)

    names = []
    used = set()
    for i, b in enumerate(rig.bones):
        n = b.name or f"bone_{i}"
        while n in used:
            n = f"{n}_{i}"
        used.add(n)
        names.append(n)

    bpy.ops.object.mode_set(mode="EDIT")
    edit = []
    for i, b in enumerate(rig.bones):
        eb = arm.edit_bones.new(names[i])
        head = to_blender(b.position)
        eb.head = head
        eb.tail = head + Vector((0.0, BONE_LENGTH, 0.0))  # along +Y, no roll: no rest rotation
        eb.roll = 0.0
        edit.append(eb)
    for i, b in enumerate(rig.bones):
        if b.parent >= 0:
            edit[i].parent = edit[b.parent]
            edit[i].use_connect = False
    bpy.ops.object.mode_set(mode="OBJECT")

    arm["rfa_bones"] = names
    arm["rfa_rig"] = name
    for pb in obj.pose.bones:
        pb.rotation_mode = "QUATERNION"
    # The tags (where weapons and the camera attach) as empties on their bones.
    for tag in rig.tags:
        if not 0 <= tag.bone < len(names):
            continue
        empty = bpy.data.objects.new(f"{name}:{tag.name}", None)
        empty.empty_display_type = "ARROWS"
        empty.empty_display_size = 0.05
        context.collection.objects.link(empty)
        empty.parent = obj
        empty.parent_type = "BONE"
        empty.parent_bone = names[tag.bone]
        context.view_layer.update()
        r = tag.rotation  # rows right, up, forward, in the game's space
        rows = [to_blender(r[0:3]), to_blender(r[3:6]), to_blender(r[6:9])]
        m = Matrix((rows[0], rows[2], rows[1])).transposed().to_4x4()  # rows swap as the axes do
        bone_head = to_blender(rig.bones[tag.bone].position)
        m.translation = bone_head + to_blender(tag.position)
        # A bone child sits at the bone's tail: place it in the armature's space, then keep it there.
        empty.matrix_world = obj.matrix_world @ m
        empty["rfa_tag"] = tag.name
    return obj


# ---------------------------------------------------------------- character meshes


def make_mesh(context, mesh, name, arm_obj, textures=None):
    """A character's mesh, skinned to an armature by vertex groups named after its bones. textures
    (a Textures) finds the materials' textures; without it they come in as plain colours."""
    verts = [to_blender(p) for p in mesh.positions]
    tris = [(a, b, c) for a, b, c, _ in mesh.triangles]
    # Swapping the axes turns the triangles' winding round: keep whichever way agrees with the normals.
    agree = 0.0
    for a, b, c in tris[: min(len(tris), 4000)]:
        n = (verts[b] - verts[a]).cross(verts[c] - verts[a])
        agree += n.dot(to_blender(mesh.normals[a]) + to_blender(mesh.normals[b]) + to_blender(mesh.normals[c]))
    if agree < 0:
        tris = [(a, c, b) for a, b, c in tris]
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], tris)
    me.update()

    materials = sorted({m for *_, m in mesh.triangles})
    slot = {}
    for m in materials:
        mat = make_material(mesh.materials[m] if m < len(mesh.materials) else None, m, textures)
        me.materials.append(mat)
        slot[m] = len(me.materials) - 1
    me.polygons.foreach_set("material_index", [slot[m] for *_, m in mesh.triangles])

    uv = me.uv_layers.new(name="UVMap")
    loop_uvs = []
    for poly in me.polygons:
        for li in poly.loop_indices:
            u, v = mesh.uvs[me.loops[li].vertex_index]
            loop_uvs += (u, -v)  # the files measure v down from the texture's top (repeating past 0 and 1); Blender up from its bottom
    uv.data.foreach_set("uv", loop_uvs)
    try:
        if hasattr(me, "use_auto_smooth"):
            me.use_auto_smooth = True
        me.normals_split_custom_set_from_vertices([to_blender(n) for n in mesh.normals])
    except Exception:
        pass
    for poly in me.polygons:
        poly.use_smooth = True

    obj = bpy.data.objects.new(name, me)
    context.collection.objects.link(obj)
    if arm_obj is not None and mesh.bones:
        names = bone_names(arm_obj)
        groups = {}
        for v, (bones, weights) in enumerate(zip(mesh.bones, mesh.weights)):
            for bone, weight in zip(bones, weights):
                if weight <= 0 or bone >= len(names):
                    continue
                group = groups.get(bone)
                if group is None:
                    group = groups[bone] = obj.vertex_groups.new(name=names[bone])
                group.add([v], weight, "ADD")
        obj.parent = arm_obj
        mod = obj.modifiers.new("Armature", "ARMATURE")
        mod.object = arm_obj
    return obj


# ---------------------------------------------------------------- materials


class Textures:
    """The textures a character's materials name: from the character's own container's texture
    files, then the ones the game always has loaded for characters (misc.vpp_pc's
    always_loaded_char and always_loaded). Each is saved once as a DDS in a cache folder and loaded
    into Blender packed, so the .blend keeps it."""

    SHARED = ("always_loaded_char.cpeg_pc", "always_loaded.cpeg_pc")

    def __init__(self, game_folder, container):
        self.game_folder = game_folder
        self.pegs = []
        for n in container.names():
            g = rf.peg_data_name(n)
            if g and container.has(g):
                self.pegs.append((container, n, g))
        self._shared = None
        self._open = {}
        self.missing = set()

    def _shared_pegs(self):
        if self._shared is None:
            self._shared = []
            for rel in GameFiles.RIGS:
                pf = GameFiles.archive(self.game_folder, rel)
                if pf is None:
                    continue
                for n in self.SHARED:
                    g = rf.peg_data_name(n)
                    if pf.has(n) and pf.has(g):
                        self._shared.append((pf, n, g))
        return self._shared

    def _find(self, name):
        for i, (source, c, g) in enumerate(self.pegs + self._shared_pegs()):
            key = i
            peg = self._open.get(key)
            if peg is None:
                try:
                    peg = self._open[key] = rf.PegFile(source.read(c), source.read(g))
                except (ValueError, KeyError):
                    continue
            t = peg.find(name)
            if t is not None:
                return peg, t
        return None, None

    def image(self, name):
        """The texture of that name ("x.tga") as a Blender image, or None (and noted in missing)."""
        if not name or name.lower().startswith("missing"):
            return None
        stem = os.path.splitext(name)[0]
        img = bpy.data.images.get(stem)
        if img is not None:
            return img
        peg, tex = self._find(name)
        if tex is None:
            self.missing.add(name)
            return None
        try:
            dds = peg.dds(tex)
        except ValueError:
            self.missing.add(name)
            return None
        import tempfile
        folder = os.path.join(tempfile.gettempdir(), "rfa_animation_tools", "textures")
        os.makedirs(folder, exist_ok=True)
        path = os.path.join(folder, stem + ".dds")
        with open(path, "wb") as f:
            f.write(dds)
        img = bpy.data.images.load(path)
        img.name = stem
        try:
            img.pack()
        except RuntimeError:
            pass
        if tex.flags & 0x200 or stem.lower().endswith(("_nm", "_sp")):
            img.colorspace_settings.name = "Non-Color"
        return img


def make_material(material, number, textures):
    """A Blender material for one of the game's, named after its colour texture (or
    rfa_material_<n>); see build_material. An existing one of that name is used as it is, so
    changes made to it are kept."""
    diffuse = material.texture(rf.DIFFUSE_MAP) if material else None
    mat_name = os.path.splitext(diffuse)[0] if diffuse and not diffuse.lower().startswith("missing") else f"rfa_material_{number}"
    mat = bpy.data.materials.get(mat_name)
    if mat is not None:
        if material is not None and textures is not None and "rfa_textures" in mat and "rfa_constants" not in mat:
            store_material(mat, material)  # made by an earlier version: given the game's look now
            build_material(mat, material, textures)
        return mat
    mat = bpy.data.materials.new(mat_name)
    if material is None:
        return mat
    store_material(mat, material)
    if textures is not None:
        build_material(mat, material, textures)
    return mat


def store_material(mat, material):
    mat["rfa_textures"] = json.dumps({f"{slot:08X}": tex for slot, tex in material.textures})
    mat["rfa_constants"] = json.dumps({f"{h:08X}": list(v) for h, v in material.constants.items()})
    mat["rfa_flags"] = material.flags
    mat["rfa_shader"] = f"{material.shader:08X}"


def stored_material(mat):
    """The game's material a Blender one was made from (store_material), or None."""
    if "rfa_textures" not in mat:
        return None
    textures = [(int(k, 16), v) for k, v in json.loads(mat["rfa_textures"]).items()]
    constants = {int(k, 16): tuple(v) for k, v in json.loads(mat.get("rfa_constants", "{}")).items()}
    return rf.Material(textures, constants, int(mat.get("rfa_flags", 0x40)), int(mat.get("rfa_shader", "0"), 16))


# The constants the game's shaders take that the Blender material shows, as nodes labelled by name
# (changing them changes the exported vehicle's material).
CONSTANT_NODES = ("Diffuse_Color", "Dust_Color", "Dust_Amount", "Specular_Color", "Specular_Power",
                  "Normal_Height", "Glow_Color", "Self_Illumination", "Main_Opacity")
_COLOURS = {"Diffuse_Color", "Dust_Color", "Specular_Color", "Glow_Color"}


def build_material(mat, material, textures):
    """The material's nodes, as the game's vehicle and character shaders (ir_carwalker, ir_cardefault,
    ir_carglass, ...) colour a surface, lighting aside:
      colour   the colour map x Diffuse_Color, laid over with Dust_Color where the mask's green x
               Dust_Amount says (the dust)
      shine    the specular map x Specular_Color, none under the dust; Specular_Power as roughness
      normal   the normal map (DirectX: green flipped for Blender) x Normal_Height
      glow     the glow map x Glow_Color x Self_Illumination
      alpha    the colour map's alpha x Main_Opacity (blended or cut-out materials); glass: how
               bright its colour is x Main_Opacity
    The constants are value and colour nodes labelled by name."""
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.location = (500, 0)
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (150, 0)
    nt.links.new(bsdf.outputs[0], out.inputs["Surface"])
    link = nt.links.new
    k = material.constants

    def const(name, default):
        return k.get(rf.name_hash(name), default)

    def value_node(name, value, x, y):
        n = nt.nodes.new("ShaderNodeValue")
        n.label = n.name = name
        n.outputs[0].default_value = float(value[0] if isinstance(value, (tuple, list)) else value)
        n.location = (x, y)
        return n.outputs[0]

    def colour_node(name, value, x, y):
        n = nt.nodes.new("ShaderNodeRGB")
        n.label = n.name = name
        n.outputs[0].default_value = (value[0], value[1], value[2], 1.0)
        n.location = (x, y)
        return n.outputs[0]

    def mix(fac, a, b, blend, x, y):
        n = nt.nodes.new("ShaderNodeMix")
        n.data_type = "RGBA"
        n.blend_type = blend
        n.location = (x, y)
        if isinstance(fac, float):
            n.inputs[0].default_value = fac
        else:
            link(fac, n.inputs[0])
        for sock, v in ((n.inputs[6], a), (n.inputs[7], b)):
            if isinstance(v, tuple):
                sock.default_value = v
            else:
                link(v, sock)
        return n.outputs[2]

    def math(op, a, b, x, y, clamp=False):
        n = nt.nodes.new("ShaderNodeMath")
        n.operation = op
        n.use_clamp = clamp
        n.location = (x, y)
        for sock, v in ((n.inputs[0], a), (n.inputs[1], b)):
            if isinstance(v, (int, float)):
                sock.default_value = float(v)
            else:
                link(v, sock)
        return n.outputs[0]

    def image(slot, x, y, data=False):
        name = material.texture(slot)
        img = textures.image(name) if name else None
        if img is None:
            return None
        n = nt.nodes.new("ShaderNodeTexImage")
        n.image = img
        n.location = (x, y)
        if data:
            n.label = "Mask" if slot == rf.PATTERN_MAP else n.label
        return n

    def socket(*names):
        for nm in names:
            if nm in bsdf.inputs:
                return bsdf.inputs[nm]
        return None

    glass = material.shader == rf.SHADER_CAR_GLASS
    # Colour.
    di = image(rf.DIFFUSE_MAP, -1100, 400)
    base = None
    if di is not None:
        base = mix(1.0, di.outputs["Color"], colour_node("Diffuse_Color", const("Diffuse_Color", (1, 1, 1)), -1100, 150), "MULTIPLY", -800, 350)
    dust = None
    mk = image(rf.PATTERN_MAP, -1400, -100, data=True)
    if mk is not None and rf.name_hash("Dust_Amount") in k:
        sep = nt.nodes.new("ShaderNodeSeparateColor")
        sep.location = (-1100, -100)
        link(mk.outputs["Color"], sep.inputs[0])
        dust = math("MULTIPLY", sep.outputs[1], value_node("Dust_Amount", const("Dust_Amount", (0,)), -1100, -250), -850, -100, clamp=True)
        if base is not None:
            base = mix(dust, base, colour_node("Dust_Color", const("Dust_Color", (0.5, 0.36, 0.15)), -850, 150), "MIX", -550, 300)
    if base is not None:
        link(base, socket("Base Color"))
    # Shine.
    sp = image(rf.SPECULAR_MAP, -1100, -450)
    spec_in = socket("Specular IOR Level", "Specular")
    if sp is not None and spec_in is not None:
        shine = mix(1.0, sp.outputs["Color"], colour_node("Specular_Color", const("Specular_Color", (1, 1, 1)), -1100, -700), "MULTIPLY", -800, -500)
        if dust is not None:
            shine = mix(dust, shine, (0.0, 0.0, 0.0, 1.0), "MIX", -550, -500)
        bw = nt.nodes.new("ShaderNodeRGBToBW")
        bw.location = (-300, -500)
        link(shine, bw.inputs[0])
        link(bw.outputs[0], spec_in)
    rough = socket("Roughness")
    if rough is not None and rf.name_hash("Specular_Power") in k:
        p = value_node("Specular_Power", const("Specular_Power", (10,)), -800, -800)
        r = math("ADD", p, 2.0, -550, -800)
        r = math("DIVIDE", 2.0, r, -400, -800)
        r = math("SQRT", r, 0.0, -250, -800, clamp=True)
        link(r, rough)
    # Normal.
    nm = image(rf.NORMAL_MAP, -1100, -1000)
    if nm is not None and not glass:
        sep = nt.nodes.new("ShaderNodeSeparateColor")
        sep.location = (-800, -1000)
        link(nm.outputs["Color"], sep.inputs[0])
        g = math("SUBTRACT", 1.0, sep.outputs[1], -600, -1050)
        comb = nt.nodes.new("ShaderNodeCombineColor")
        comb.location = (-400, -1000)
        link(sep.outputs[0], comb.inputs[0])
        link(g, comb.inputs[1])
        link(sep.outputs[2], comb.inputs[2])
        nmap = nt.nodes.new("ShaderNodeNormalMap")
        nmap.location = (-200, -1000)
        link(comb.outputs[0], nmap.inputs["Color"])
        link(value_node("Normal_Height", const("Normal_Height", (1,)), -400, -1200), nmap.inputs["Strength"])
        link(nmap.outputs["Normal"], socket("Normal"))
    # Glow.
    gl = image(rf.GLOW_MAP, -1100, -1400)
    emission = socket("Emission Color", "Emission")
    if gl is not None and emission is not None and const("Self_Illumination", (0,))[0] > 0:
        glow = mix(1.0, gl.outputs["Color"], colour_node("Glow_Color", const("Glow_Color", (1, 1, 1)), -1100, -1650), "MULTIPLY", -800, -1450)
        link(glow, emission)
        strength = socket("Emission Strength")
        if strength is not None:
            link(value_node("Self_Illumination", const("Self_Illumination", (1,)), -800, -1650), strength)
    # Alpha.
    if di is not None and (material.blended or material.cut_out or glass):
        if glass:
            bw = nt.nodes.new("ShaderNodeRGBToBW")
            bw.location = (-300, 100)
            link(base, bw.inputs[0])
            alpha = bw.outputs[0]
        else:
            alpha = di.outputs["Alpha"]
        if rf.name_hash("Main_Opacity") in k:
            alpha = math("MULTIPLY", alpha, value_node("Main_Opacity", const("Main_Opacity", (1,)), -300, -100), -100, 100, clamp=True)
        link(alpha, socket("Alpha"))
        blended = material.blended or glass
        if hasattr(mat, "surface_render_method"):
            mat.surface_render_method = "BLENDED" if blended else "DITHERED"
        if hasattr(mat, "blend_method"):
            try:
                mat.blend_method = "BLEND" if blended else "CLIP"
            except TypeError:
                pass
    return mat


def material_constants(mat):
    """{name hash: value} of the constant nodes a material shows (see build_material)."""
    out = {}
    if not mat.use_nodes or mat.node_tree is None:
        return out
    for n in mat.node_tree.nodes:
        name = n.label or n.name
        if name not in CONSTANT_NODES:
            continue
        if n.type == "RGB":
            c = n.outputs[0].default_value
            out[rf.name_hash(name)] = (c[0], c[1], c[2], 1.0)
        elif n.type == "VALUE":
            out[rf.name_hash(name)] = (n.outputs[0].default_value, 0.0, 0.0, 0.0)
    return out


class ImagesByName:
    """Textures already in Blender, by name (for rebuilding materials without the game's files)."""

    missing = set()

    def image(self, name):
        if not name:
            return None
        return bpy.data.images.get(os.path.splitext(name)[0])


# ---------------------------------------------------------------- animations in


def apply_animation(context, arm_obj, anim, name, report):
    """Puts an animation on an RFA armature as a new action (60 frames a second, from frame 0)."""
    names = bone_names(arm_obj)
    if anim.skeleton_bones != len(names):
        report({"WARNING"}, f"{name} was made for a skeleton of {anim.skeleton_bones} bones; this one has {len(names)}")
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    arm_obj.animation_data_create()
    arm_obj.animation_data.action = action
    fcurves = _fcurves(arm_obj, action)

    weights = {}
    skipped = 0
    for track in anim.tracks:
        if track.bone >= len(names):
            skipped += 1
            continue
        bone = names[track.bone]
        base = f'pose.bones["{bone}"]'
        pb = arm_obj.pose.bones.get(bone)
        if pb is not None:
            pb.rotation_mode = "QUATERNION"
        if track.rotations:
            qs, previous = [], None
            for frame, q in track.rotations:
                bq = quat_to_blender(q)
                if previous is not None and bq.dot(previous) < 0:
                    bq.negate()  # the same turn, on the side of the key before (Blender blends the parts)
                previous = bq
                qs.append((frame, bq))
            for i in range(4):
                _key(fcurves, base + ".rotation_quaternion", i, [(f, q[i]) for f, q in qs])
        if track.positions:
            ps = [(f, to_blender(p)) for f, p in track.positions]
            for i in range(3):
                _key(fcurves, base + ".location", i, [(f, p[i]) for f, p in ps])
        if track.scales:
            ss = [(f, scale_to_blender(s)) for f, s in track.scales]
            for i in range(3):
                _key(fcurves, base + ".scale", i, [(f, s[i]) for f, s in ss])
        if track.weights:
            weights[str(track.bone)] = [[f, v] for f, v in track.weights]
    root = anim.root
    if root.positions:
        ps = [(f, to_blender(p)) for f, p in root.positions]
        for i in range(3):
            _key(fcurves, "location", i, [(f, p[i]) for f, p in ps])
    if root.rotations:
        arm_obj.rotation_mode = "QUATERNION"
        qs = [(f, quat_to_blender(q)) for f, q in root.rotations]
        for i in range(4):
            _key(fcurves, "rotation_quaternion", i, [(f, q[i]) for f, q in qs])
    if root.weights:
        weights["-1"] = [[f, v] for f, v in root.weights]

    action["rfa_name"] = name
    if anim.header:
        action["rfa_header"] = anim.header.hex()
    if weights:
        action["rfa_weights"] = json.dumps(weights)
    action["rfa_frames"] = anim.frames

    scene = context.scene
    scene.render.fps = rf.FRAMES_PER_SECOND
    scene.render.fps_base = 1.0
    scene.frame_start = 0
    scene.frame_end = max(1, anim.frames)
    scene.frame_set(0)
    if skipped:
        report({"WARNING"}, f"{skipped} of the animation's bones aren't in this skeleton; they were left out")
    return action


# ---------------------------------------------------------------- animations out


def _reduce(samples, close):
    """Keeps the keys the others can be worked out from (in straight lines between the kept ones)."""
    if len(samples) <= 2:
        return samples
    kept = [samples[0]]
    i = 0
    while i < len(samples) - 1:
        j = i + 1
        while j + 1 < len(samples):
            a, b = samples[i], samples[j + 1]
            ok = True
            for k in range(i + 1, j + 1):
                t = (samples[k][0] - a[0]) / (b[0] - a[0])
                if not close(a[1], b[1], t, samples[k][1]):
                    ok = False
                    break
            if not ok:
                break
            j += 1
        kept.append(samples[j])
        i = j
    return kept


def _slerp(a, b, t):
    qa, qb = Quaternion((a[3], a[0], a[1], a[2])), Quaternion((b[3], b[0], b[1], b[2]))
    if qa.dot(qb) < 0:
        qb.negate()
    q = qa.slerp(qb, t)
    return (q.x, q.y, q.z, q.w)


def _angle(a, b):
    d = abs(sum(x * y for x, y in zip(a, b)))
    return 2 * math.degrees(math.acos(min(1.0, d)))


def _animated_bones(arm_obj, action):
    """The bones the action has keys for (by the names in its F-curves' paths)."""
    names = set()
    for curve in _fcurves(arm_obj, action, create=False):
        path = curve.data_path
        if path.startswith('pose.bones["'):
            names.add(path[len('pose.bones["'):].split('"]', 1)[0])
    return names


def _keys_object(arm_obj, action):
    """Whether the action moves or turns the armature object itself (the root track)."""
    return any(c.data_path in ("location", "rotation_quaternion", "rotation_euler", "rotation_axis_angle")
               for c in _fcurves(arm_obj, action, create=False))


def build_animation(context, arm_obj, settings, report):
    """The animation the armature's action plays, as the game's file wants it: each frame from the
    scene's start to its end sampled as posed (with constraints, IK and drivers: the bones' local
    transforms as seen), converted, and thinned to the keys needed within the tolerances."""
    names = bone_names(arm_obj)
    action = arm_obj.animation_data.action if arm_obj.animation_data else None
    if action is None:
        raise ValueError("the armature has no action to export")
    scene = context.scene
    start, end = scene.frame_start, scene.frame_end
    if settings.use_action_range:
        start, end = (int(round(v)) for v in action.frame_range)
    frames = end - start
    if frames < 1:
        raise ValueError("the animation needs at least two frames (the scene's or the action's range)")

    if settings.bones == "ALL":
        chosen = list(range(len(names)))
    else:
        animated = _animated_bones(arm_obj, action)
        chosen = [i for i, n in enumerate(names) if n in animated]
        if not chosen:
            chosen = list(range(len(names)))
    only = getattr(settings, "only_bones", None)
    if only is not None:
        chosen = [i for i in chosen if i in only]
    pbs = [arm_obj.pose.bones.get(names[i]) for i in chosen]

    rot = {i: [] for i in chosen}
    pos = {i: [] for i in chosen}
    scl = {i: [] for i in chosen}
    root_pos, root_rot = [], []
    current = scene.frame_current
    try:
        for f in range(start, end + 1):
            scene.frame_set(f)
            k = f - start
            for i, pb in zip(chosen, pbs):
                if pb is None:
                    continue
                local = arm_obj.convert_space(pose_bone=pb, matrix=pb.matrix, from_space="POSE", to_space="LOCAL")
                loc, q, s = local.decompose()
                rot[i].append((k, quat_from_blender(q)))
                pos[i].append((k, from_blender(loc)))
                scl[i].append((k, from_blender(s)))
            loc, q, _ = arm_obj.matrix_basis.decompose()
            root_pos.append((k, from_blender(loc)))
            root_rot.append((k, quat_from_blender(q)))
    finally:
        scene.frame_set(current)

    tol_deg, tol_m = settings.rotation_tolerance, settings.position_tolerance

    def close_rot(a, b, t, v):
        return _angle(_slerp(a, b, t), v) <= tol_deg

    def close_vec(a, b, t, v, tol=tol_m):
        return all(abs(a[n] + (b[n] - a[n]) * t - v[n]) <= tol for n in range(3))

    def thin(samples, close):
        return samples if settings.every_frame else _reduce(samples, close)

    def all_near(samples, value, tol):
        return all(all(abs(x - y) <= tol for x, y in zip(v, value)) for _, v in samples)

    weights = json.loads(action.get("rfa_weights", "{}"))
    tracks = []
    for i in chosen:
        if not rot[i]:
            continue
        r = [] if all(_angle(q, (0, 0, 0, 1)) <= tol_deg for _, q in rot[i]) else thin(rot[i], close_rot)
        p = [] if all_near(pos[i], (0, 0, 0), 1e-5) else thin(pos[i], close_vec)
        s = [] if all_near(scl[i], (1, 1, 1), 1e-4) else thin(scl[i], lambda a, b, t, v: close_vec(a, b, t, v, 1e-3))
        w = [(int(f), float(v)) for f, v in weights.get(str(i), []) if 0 <= f <= frames]
        tracks.append(rf.Track(i, r, p, s, w))

    root = rf.Track(-1)
    if _keys_object(arm_obj, action):  # the object's own keys are the root track; else it has none
        if not all_near(root_pos, (0, 0, 0), 1e-5):
            root.positions = thin(root_pos, close_vec)
        if not all(_angle(q, (0, 0, 0, 1)) <= tol_deg for _, q in root_rot):
            root.rotations = thin(root_rot, close_rot)
    root.weights = [(int(f), float(v)) for f, v in weights.get("-1", []) if 0 <= f <= frames]

    header = bytes.fromhex(action["rfa_header"]) if action.get("rfa_header") else rf.default_header()
    header = bytearray(header)
    if root.positions:  # the movement over the clip: where the root track ends, from where it starts
        a, b = root.positions[0][1], root.positions[-1][1]
        struct.pack_into("<3f", header, 0x1C, b[0] - a[0], b[1] - a[1], b[2] - a[2])
    if root.rotations:  # the turn over the clip: the root track's last turn, from its first
        qa = Quaternion((root.rotations[0][1][3], *root.rotations[0][1][:3]))
        qb = Quaternion((root.rotations[-1][1][3], *root.rotations[-1][1][:3]))
        t = qa.inverted() @ qb
        struct.pack_into("<4f", header, 0x0C, t.x, t.y, t.z, t.w)
    anim = rf.Animation(frames, len(names), tracks, root)
    return anim, bytes(header)


# ---------------------------------------------------------------- the game's archives


class GameFiles:
    """The game's archives the tools read: skeletons (misc.vpp_pc), animations (anims.vpp_pc and the
    DLC's), characters (humans.vpp_pc). Opened once a session."""

    ANIMS = (r"build\pc\cache\anims.vpp_pc", r"build\dlc04\pc\cache\dlcp04_anims.vpp_pc")
    RIGS = (r"build\pc\cache\misc.vpp_pc", r"build\dlc04\pc\cache\dlcp04_misc.vpp_pc")
    HUMANS = (r"build\pc\cache\humans.vpp_pc", r"build\dlc04\pc\cache\dlcp04_humans.vpp_pc")

    _open = {}

    @classmethod
    def archive(cls, game_folder, relative):
        path = os.path.join(game_folder, *relative.split("\\"))
        key = path.lower()
        if key not in cls._open:
            cls._open[key] = rf.Packfile.open(path) if os.path.isfile(path) else None
        return cls._open[key]

    @classmethod
    def files(cls, game_folder, archives, extension):
        """(archive, file name) for each file of that kind, in the archives that exist."""
        out = []
        for rel in archives:
            pf = cls.archive(game_folder, rel)
            if pf is None:
                continue
            for n in pf.names():
                if n.lower().endswith(extension):
                    out.append((rel, n))
        return out

    @classmethod
    def find(cls, game_folder, archives, name):
        for rel in archives:
            pf = cls.archive(game_folder, rel)
            if pf is not None and pf.has(name):
                return rel, pf
        return None, None

    _containers = {}

    @classmethod
    def container(cls, game_folder, relative, name):
        """A container in an archive, opened (the last few kept, as they can be big)."""
        key = (os.path.join(game_folder, relative).lower(), name.lower())
        c = cls._containers.get(key)
        if c is None:
            c = cls.archive(game_folder, relative).container(name)
            if len(cls._containers) >= 4:
                cls._containers.pop(next(iter(cls._containers)))
            cls._containers[key] = c
        return c

    @classmethod
    def forget(cls):
        cls._open.clear()
        cls._containers.clear()
        Animations.forget()


def override_path(mod_folder, archive_relative, file_name):
    """Where an override of a file in an archive goes: overrides\\<the archive's path>\\<the file>
    (version 1.0 of the add-on wrote animations there; Restore still removes those)."""
    return os.path.join(mod_folder, "overrides", *archive_relative.split("\\"), file_name)


ANIMATIONS_FOLDER = ("mods", "animations")
LONGEST_NAME = 53  # the game's library takes names of up to 58 characters, "name.anim"


def animation_path(mod_folder, name):
    """Where an animation for the game goes: mods\\animations\\<name>.anim_pc. RFA-Mod (rfa_qol's
    animations part) loads every file there at the game's start, in place of the game's animation of
    that name wherever the game keeps it, or as a new one."""
    if name.lower().endswith(".anim_pc"):
        name = name[:-len(".anim_pc")]
    return os.path.join(mod_folder, *ANIMATIONS_FOLDER, name + ".anim_pc")


class Animations:
    """Every animation the game has: the animation archives' own files, and those inside the
    containers (.str2_pc) of every archive under build\\, as each archive's asset lists (.asm_pc)
    name them (a level's monsters, the walkers' and vehicles' own). Listed once a session; the list of
    each archive is kept in a cache file and read again only when the archive changes."""

    _list = {}

    @classmethod
    def _cache_path(cls):
        import tempfile
        return os.path.join(tempfile.gettempdir(), "rfa_animation_tools", "animations.json")

    @classmethod
    def all(cls, game_folder):
        """[(archive, container or "", file name)], each name once (the animation archives first)."""
        key = os.path.normcase(os.path.abspath(game_folder))
        if key in cls._list:
            return cls._list[key]
        try:
            with open(cls._cache_path(), "r", encoding="utf-8") as f:
                cache = json.load(f)
        except (OSError, ValueError):
            cache = {}
        archives = []
        build = os.path.join(game_folder, "build")
        for root, _, files in os.walk(build):
            for n in sorted(files):
                if n.lower().endswith(".vpp_pc"):
                    archives.append(os.path.relpath(os.path.join(root, n), game_folder).replace("/", "\\"))
        first = {a.lower() for a in GameFiles.ANIMS}
        archives.sort(key=lambda a: (a.lower() not in first, a.lower()))
        entries, changed = [], False
        for rel in archives:
            path = os.path.join(game_folder, *rel.split("\\"))
            try:
                st = os.stat(path)
            except OSError:
                continue
            stamp = f"{st.st_size}:{int(st.st_mtime)}"
            entry = cache.get(path.lower())
            if not entry or entry.get("stamp") != stamp or "containers" not in entry:
                entry = dict(cls._list_archive(path), stamp=stamp)
                cache[path.lower()] = entry
                changed = True
            entries.append((rel, entry))
        # A container is read from the archive whose list names it, or else from any that holds it
        # (a level's lists name containers its other archives hold).
        holder = {}
        for rel, entry in entries:
            for c in entry["containers"]:
                holder.setdefault(c.lower(), (rel, c))
        out, seen = [], set()
        for rel, entry in entries:
            for name in entry["anims"]:
                if name.lower() not in seen:
                    seen.add(name.lower())
                    out.append((rel, "", name))
        for rel, entry in entries:
            here = {c.lower() for c in entry["containers"]}
            for container, anims in entry["lists"].items():
                where = (rel, container) if container.lower() in here else holder.get(container.lower())
                if where is None:
                    continue
                for name in anims:
                    if name.lower() not in seen:
                        seen.add(name.lower())
                        out.append((where[0], where[1], name))
        if changed:
            try:
                os.makedirs(os.path.dirname(cls._cache_path()), exist_ok=True)
                with open(cls._cache_path(), "w", encoding="utf-8") as f:
                    json.dump(cache, f)
            except OSError:
                pass
        cls._list[key] = out
        return out

    @staticmethod
    def _list_archive(path):
        """{"anims": its own animations, "containers": its containers, "lists": {container: animations}}."""
        out = {"anims": [], "containers": [], "lists": {}}
        try:
            pf = rf.Packfile.open(path)
        except (OSError, ValueError, struct.error):
            return out
        out["anims"] = [n for n in pf.names() if n.lower().endswith(".anim_pc")]
        out["containers"] = [n for n in pf.names() if n.lower().endswith(".str2_pc")]
        for n in pf.names():
            if not n.lower().endswith(".asm_pc"):
                continue
            # An archive compressed in one stream is read whole for its lists: skip the huge ones.
            if pf.compressed and pf.condensed and pf.compressed_data_size > (256 << 20):
                continue
            try:
                for container, anims in rf.asm_animations(pf.read(n)).items():
                    out["lists"].setdefault(container, []).extend(anims)
            except (ValueError, KeyError, struct.error, zlib_error()):
                continue
        return out

    @classmethod
    def read(cls, game_folder, archive, container, name):
        pf = GameFiles.archive(game_folder, archive)
        if pf is None:
            raise KeyError(f"no {archive}")
        if container:
            pf = GameFiles.container(game_folder, archive, container)
        return pf.read(name)

    @classmethod
    def find(cls, game_folder, name):
        """(archive, container) where the game keeps an animation of that name, or (None, None)."""
        low = name.lower()
        if not low.endswith(".anim_pc"):
            low += ".anim_pc"
        for rel, container, n in cls.all(game_folder):
            if n.lower() == low:
                return rel, container
        return None, None

    @classmethod
    def forget(cls):
        cls._list.clear()


def zlib_error():
    import zlib
    return zlib.error
