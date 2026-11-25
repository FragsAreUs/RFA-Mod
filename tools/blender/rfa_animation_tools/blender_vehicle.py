"""The vehicle builder: a game vehicle's working parts as a kit in Blender, modelled anew, and written
back as a new vehicle (see rfa_vehicle.py for the file).

A kit is a collection under one empty (the vehicle: custom properties rfa_vehicle, the kind it was
made from, and rfa_vehicle_name, the new vehicle's name), holding, by name:
  body                     the chassis's mesh (its submesh 0), in the vehicle's space
  <part name>              one object a part (wheel_fl, door_l, turret, ...), a mesh when the part is
                           drawn, else an empty, parented as the parts are; where it sits is where the
                           part goes (rfa_part: its number)
  light <n> off / on       a light's two meshes (rfa_light), where the light is
  piston <part> <n>        a piston's segments (rfa_piston)
  submesh <n>              the model's other meshes (rfa_submesh), kept as they are unless changed
  collision ...            the collision: convex hulls (each mesh one hull round its vertices;
                           rfa_shape: which shape it is part of: 0 the chassis's, else a part's)
  tags                     empties (rfa_tag: where in the file), seats (driver, passenger_<n>),
                           the engine, turrets' and cameras' points, by name
Model each part from scratch in place of the kit's meshes, keeping the objects' names (or the
rfa_ properties). Materials are Blender's: the Principled BSDF's Base Color, Normal (through a Normal
Map node), Specular and Emission colour images become the vehicle's colour, normal, specular and glow
maps.
"""

import json
import os
import re

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

from . import blender_io as bio
from . import rfa_formats as rf
from . import rfa_textures as rt
from . import rfa_vehicle as rv

VEHICLES = r"build\pc\cache\vehicles.vpp_pc"
ASSET_LIST = "vehicle_containers.asm_pc"
TABLE_ARCHIVES = (r"build\pc\cache\table.vpp_pc", r"build\pc\cache\misc.vpp_pc")

_C = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))  # game <-> Blender (swaps y and z)


def game_matrix(rot9, pos):
    """A placement (rotation rows right, up, forward; a point p goes to p * R + t) as a column matrix."""
    m = Matrix(((rot9[0], rot9[3], rot9[6], pos[0]), (rot9[1], rot9[4], rot9[7], pos[1]),
                (rot9[2], rot9[5], rot9[8], pos[2]), (0, 0, 0, 1)))
    return _C @ m @ _C


def placement(bm):
    """A Blender matrix as the file's placement: rotation rows (9 floats, no scale) and position."""
    m = _C @ bm @ _C
    loc, rot, _ = m.decompose()
    r = rot.to_matrix()
    rows = [r[0][0], r[1][0], r[2][0], r[0][1], r[1][1], r[2][1], r[0][2], r[1][2], r[2][2]]
    return rows, (loc.x, loc.y, loc.z)


def kinds(game_folder):
    """The game's vehicles to build from: (container, model name)."""
    pf = bio.GameFiles.archive(game_folder, VEHICLES)
    if pf is None:
        return []
    out = []
    for n in pf.names():
        if n.lower().endswith(".ccar_pc.str2_pc"):
            out.append((n, n[: -len(".ccar_pc.str2_pc")]))
    return sorted(out, key=lambda x: x[1].lower())


def open_kind(game_folder, container_name):
    container = bio.GameFiles.container(game_folder, VEHICLES, container_name)
    model = container_name[: -len(".str2_pc")]
    c = container.read(model)
    g = container.read(model[: -len(".ccar_pc")] + ".gcar_pc")
    return container, rv.VehicleModel(c, g)


# ---------------------------------------------------------------- the kit


def _mesh_object(context, collection, name, sm, materials, frame):
    """A mesh object for a submesh (its vertices in its own frame), placed at frame."""
    verts = [bio.to_blender(p) for p in sm.positions]
    faces, mats = [], []
    for mat, tris in sm.blocks:
        for a, b, c in tris:
            faces.append((a, c, b))  # the axes' swap turns the winding round
            mats.append(mat)
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    slot = {}
    for m in sorted(set(mats)):
        me.materials.append(materials[m] if m < len(materials) else None)
        slot[m] = len(me.materials) - 1
    if faces:
        me.polygons.foreach_set("material_index", [slot[m] for m in mats])
        uv = me.uv_layers.new(name="UVMap")
        loop_uvs = []
        for poly in me.polygons:
            for li in poly.loop_indices:
                u, v = sm.uvs[me.loops[li].vertex_index]
                loop_uvs += (u, 1.0 + v)
        uv.data.foreach_set("uv", loop_uvs)
        try:
            if hasattr(me, "use_auto_smooth"):
                me.use_auto_smooth = True
            me.normals_split_custom_set_from_vertices([bio.to_blender(n) for n in sm.normals])
        except Exception:
            pass
        for poly in me.polygons:
            poly.use_smooth = True
    obj = bpy.data.objects.new(name, me)
    collection.objects.link(obj)
    obj.matrix_world = frame
    return obj


def _empty(collection, name, frame, kind="PLAIN_AXES", size=0.15):
    obj = bpy.data.objects.new(name, None)
    obj.empty_display_type = kind
    obj.empty_display_size = size
    collection.objects.link(obj)
    obj.matrix_world = frame
    return obj


def _hide(obj):
    try:
        obj.hide_set(True)
    except RuntimeError:  # not in the view layer yet
        obj.hide_viewport = True


def _parent(obj, parent):
    world = obj.matrix_world.copy()
    obj.parent = parent
    obj.matrix_world = world


def _hull_object(collection, name, shape, frame, parent, number):
    pts = [bio.to_blender(v) for v in shape.vertices]
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    for p in pts:
        bm.verts.new(p)
    try:
        bmesh.ops.convex_hull(bm, input=bm.verts)
    except Exception:
        pass
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    collection.objects.link(obj)
    obj.matrix_world = frame
    _parent(obj, parent)
    obj.display_type = "WIRE"
    obj.hide_render = True
    obj["rfa_shape"] = number
    obj["rfa_radius"] = shape.radius
    return obj


def _hulls(shape):
    if isinstance(shape, rv.Convex):
        return [shape]
    out = []
    for c in shape.children:
        out += _hulls(c)
    return out


def _game_material(m):
    """A VehicleModel material (dict) as an rf.Material, with its constants and shader."""
    return rf.Material([(s, n) for s, n in m["textures"]], rf.block_constants(m["block"]), m["flags"],
                       rv.struct.unpack_from("<I", m["block"], 4)[0])


def refresh_materials(root, game_folder):
    """The kit's materials' nodes made again (see blender_io.build_material), with the kind's
    constants and textures: for kits made before the materials showed the game's look, or after
    changing them too far. Returns how many."""
    container, model = open_kind(game_folder, root["rfa_vehicle"])
    kind = root["rfa_vehicle"][: -len(".ccar_pc.str2_pc")]
    tex = bio.Textures(game_folder, container)
    done = 0
    mats = {m for o in _kit_objects(root) if o.type == "MESH" for m in o.data.materials if m is not None}
    for mat in mats:
        k = mat.get("rfa_template_material")
        if isinstance(k, int) and 0 <= k < len(model.materials) and mat.get("rfa_template_kind", kind) == kind:
            game = _game_material(model.materials[k])
            bio.store_material(mat, game)
            bio.build_material(mat, game, tex)
            done += 1
        elif bio.stored_material(mat) is not None:
            bio.build_material(mat, bio.stored_material(mat), tex)
            done += 1
    return done


def make_kit(context, game_folder, container_name, new_name, textures=True):
    """The kit for a vehicle: see the module's description. Returns its root empty."""
    container, model = open_kind(game_folder, container_name)
    subs = model.read_mesh()
    kind = container_name[: -len(".ccar_pc.str2_pc")]
    coll = bpy.data.collections.new(new_name)
    context.scene.collection.children.link(coll)
    root = _empty(coll, new_name, Matrix(), "ARROWS", 1.0)
    root["rfa_vehicle"] = container_name
    root["rfa_vehicle_name"] = new_name

    # Materials: the model's, with their textures.
    tex = bio.Textures(game_folder, container) if textures else None
    materials = []
    for k, m in enumerate(model.materials):
        mat_obj = _game_material(m)
        mat = bio.make_material(mat_obj, k, tex)
        if mat.get("rfa_template_kind", kind) != kind or mat.get("rfa_template_material", k) != k:
            mat = mat.copy()  # another kit's material of the same name: this kit gets its own
            mat.name = f"{kind} material {k}"
        mat["rfa_template_material"] = k
        mat["rfa_template_kind"] = kind
        materials.append(mat)

    used = set()
    # The chassis.
    body = _mesh_object(context, coll, "body", subs[0], materials, Matrix())
    _parent(body, root)
    body["rfa_part"] = -1
    body["rfa_part_shape"] = 0
    used.add(0)

    # The parts, in their tree.
    part_obj = {}
    frames = {}

    def frame_of(i):
        if i in frames:
            return frames[i]
        p = model.parts[i]
        local = game_matrix(p.rotation, p.position)
        has_parent = 0 <= p.parent < len(model.parts) and not p.vehicle_space
        f = (frame_of(p.parent) if has_parent else Matrix()) @ local
        frames[i] = f
        return f

    for p in model.parts:
        f = frame_of(p.index)
        if 0 <= p.submesh < len(subs):
            obj = _mesh_object(context, coll, p.name, subs[p.submesh], materials, f)
            used.add(p.submesh)
        else:
            obj = _empty(coll, p.name, f)
        obj["rfa_part"] = p.index
        obj["rfa_part_shape"] = p.shape if 0 < p.shape < len(model.shapes) else 0
        if p.flags & 0x800:
            _hide(obj)
        part_obj[p.index] = obj
    for p in model.parts:
        parent = part_obj.get(p.parent, root) if p.parent >= 0 else root
        _parent(part_obj[p.index], parent)

    # Lights' meshes (off and on), where each light is.
    h = model.c
    lights = []  # (record offset, owner object, owner frame)
    n, at = rv._i(h, 0x11C), rv._i(h, 0x120)
    lights += [(at + rv.LIGHT * k, root, Matrix()) for k in range(max(0, n))]
    for p in model.parts:
        n, at = rv._i(h, p.at + 0x88), rv._i(h, p.at + 0x8C)
        lights += [(at + rv.LIGHT * k, part_obj[p.index], frames[p.index]) for k in range(max(0, n))]
    for k, (rec, owner, owner_frame) in enumerate(lights):
        rot = rv.struct.unpack_from("<9f", h, rec + 4)
        pos = rv.struct.unpack_from("<3f", h, rec + 0x28)
        f = owner_frame @ game_matrix(rot, pos)
        for side, off in (("off", 0x34), ("on", 0x40)):
            s = rv._i(h, rec + off)
            if 0 <= s < len(subs) and s not in used:
                obj = _mesh_object(context, coll, f"light {k} {side}", subs[s], materials, f)
                obj["rfa_light"] = rec
                obj["rfa_submesh"] = s
                _parent(obj, owner)
                used.add(s)

    # Pistons' segments.
    for p in model.parts:
        n, at = rv._i(h, p.at + 0x80), rv._i(h, p.at + 0x84)
        for k in range(max(0, n)):
            rec = at + 60 * k
            s = rv._i(h, rec)
            if not (0 <= s < len(subs)) or s in used:
                continue
            f = frames[p.index] @ game_matrix(rv.struct.unpack_from("<9f", h, rec + 0xC), rv.struct.unpack_from("<3f", h, rec + 0x30))
            obj = _mesh_object(context, coll, f"piston {p.name} {k}", subs[s], materials, f)
            obj["rfa_submesh"] = s
            obj["rfa_frame"] = [x for row in (frames[p.index].inverted() @ f) for x in row]
            _parent(obj, part_obj[p.index])
            used.add(s)

    # Any other mesh, as it is.
    for s in range(len(subs)):
        if s in used or not subs[s].positions:
            continue
        obj = _mesh_object(context, coll, f"submesh {s}", subs[s], materials, Matrix())
        obj["rfa_submesh"] = s
        _parent(obj, root)
        _hide(obj)

    # The collision: shape 0 the chassis's (in the vehicle's space), the parts' in theirs.
    col = bpy.data.collections.new(f"{new_name} collision")
    coll.children.link(col)
    owners = {0: (root, Matrix())}
    for p in model.parts:
        if 0 < p.shape < len(model.shapes) and p.shape not in owners:
            owners[p.shape] = (part_obj[p.index], frames[p.index])
    for s, shape in enumerate(model.shapes):
        owner, f = owners.get(s, (root, Matrix()))
        label = "body" if s == 0 else owner.name
        for k, hshape in enumerate(_hulls(shape)):
            _hull_object(col, f"collision {label} {k}", hshape, f, owner, s)

    # Tags.
    tagc = bpy.data.collections.new(f"{new_name} points")
    coll.children.link(tagc)

    def tag_objects(tags, owner, f, where):
        for k, t in enumerate(tags):
            obj = _empty(tagc, t.name, f @ game_matrix(t.rotation, t.position), "ARROWS", 0.25)
            obj["rfa_tag"] = t.at
            obj["rfa_tag_hash"] = f"{t.hash:08X}"  # (a Blender int property is 32-bit signed)
            _parent(obj, owner)

    tag_objects(model.chassis_tags, root, Matrix(), "chassis")
    for p in model.parts:
        tag_objects(p.tags, part_obj[p.index], frames[p.index], p.name)

    context.view_layer.objects.active = root
    return root, (tex.missing if tex else set())


# ---------------------------------------------------------------- writing


def _kit_objects(root):
    out = []
    stack = list(root.children)
    while stack:
        o = stack.pop()
        out.append(o)
        stack += list(o.children)
    return out


def _evaluated_mesh(context, obj):
    dg = context.evaluated_depsgraph_get()
    eo = obj.evaluated_get(dg)
    return eo, eo.to_mesh()


class _Materials:
    """The vehicle's materials, made from Blender's: each a copy of one of the kind's (the one it came
    from, else the kind's first opaque one with a colour texture), its textures the images found."""

    def __init__(self, model, template_peg, name, report):
        self.model, self.peg, self.name, self.report = model, template_peg, name.lower(), report
        self.blender = []     # Blender materials in order
        self.list = []        # the vehicle's (dicts as VehicleModel.materials)
        self.textures = rt.TextureFile()
        self.added = set()

    def index(self, mat):
        if mat in self.blender:
            return self.blender.index(mat)
        self.blender.append(mat)
        self.list.append(self._make(mat))
        return len(self.blender) - 1

    def _base(self, mat):
        k = mat.get("rfa_template_material") if mat is not None else None
        tm = self.model.materials
        if isinstance(k, int) and 0 <= k < len(tm):
            return tm[k]
        blended = mat is not None and getattr(mat, "blend_method", "OPAQUE") == "BLEND"
        for m in tm:
            slots = {s for s, _ in m["textures"]}
            if rf.DIFFUSE_MAP in slots and ((m["flags"] & 3) == 1) == blended:
                return m
        return tm[0]

    def _make(self, mat):
        base = self._base(mat)
        block = bytearray(base["block"])
        images = _material_images(mat) if mat is not None else {}
        label = re.sub(r"[^a-z0-9_]+", "_", (mat.name if mat else "default").lower()).strip("_") or "m"
        # A material of the user's own (not one of the kind's): nothing of the kind's shows through it.
        own = mat is not None and not isinstance(mat.get("rfa_template_material"), int)
        if own:
            rf.set_block_constant(block, rf.name_hash("Diffuse_Color"), (1.0, 1.0, 1.0, 1.0))
            rf.set_block_constant(block, rf.name_hash("Dust_Amount"), (0.0, 0.0, 0.0, 0.0))
            rf.set_block_constant(block, rf.name_hash("Self_Illumination"), (1.0 if GLOW_MAP in images else 0.0, 0.0, 0.0, 0.0))
            rf.set_block_constant(block, rf.name_hash("Specular_Color"), (1.0, 1.0, 1.0, 1.0))
        if mat is not None:
            for h, v in bio.material_constants(mat).items():
                rf.set_block_constant(block, h, v)
        out = {"block": bytes(block), "flags": base["flags"], "textures": [list(t) for t in base["textures"]]}
        for t in out["textures"]:
            slot, name = t
            found = images.get(slot)
            if found is not None:
                t[1] = self._image(found[0], f"{self.name}_{label}", slot, flip_green=slot == rf.NORMAL_MAP and not found[1])
            elif own and slot in (rf.DIFFUSE_MAP, rf.SPECULAR_MAP):
                t[1] = self._solid(mat, slot, f"{self.name}_{label}")
            elif own and slot == rf.NORMAL_MAP:
                t[1] = "flat-normalmap_n.tga"
            elif own and slot in (MASK_MAP, GLOW_MAP):
                t[1] = "missing.tga"
            elif not self._keep(name):
                t[1] = self._fallback(slot)
        return out

    def _solid(self, mat, slot, stem):
        """A 4 x 4 texture of the material's plain colour (Base Color) or shine (Specular), for a
        material without an image there."""
        bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None) if mat.use_nodes and mat.node_tree else None
        if slot == rf.DIFFUSE_MAP:
            c = list(bsdf.inputs["Base Color"].default_value) if bsdf else list(mat.diffuse_color)
            c = [_to_srgb(x) for x in c[:3]] + [c[3] if len(c) > 3 else 1.0]
            suffix = "di"
        else:
            sock = (bsdf.inputs.get("Specular IOR Level") or bsdf.inputs.get("Specular")) if bsdf else None
            v = float(sock.default_value) if sock is not None else 0.5
            c = [v, v, v, 1.0]
            suffix = "sp"
        name = f"{stem}_{suffix}.tga"
        if name.lower() not in self.added:
            px = np.zeros((4, 4, 4), np.uint8)
            px[...] = [int(max(0.0, min(1.0, x)) * 255 + 0.5) for x in c]
            self.textures.add(name, px)
            self.added.add(name.lower())
        return name

    def _keep(self, name):
        """A texture of the kind's own file is copied into the new one; the game's built-ins stay as named."""
        if name.lower().startswith(("missing", "flat-normalmap", "game_cube")):
            return True
        if self.peg is None:
            return False
        t = self.peg.find(name)
        if t is None:
            return True  # not the kind's own: one the game has loaded anyway
        if t.name.lower() not in self.added:
            data = self.peg.data[t.offset:t.offset + t.size]
            self.textures.add_raw(t.name, t.width, t.height, t.format, t.flags, t.mips, data)
            self.added.add(t.name.lower())
        return True

    def _fallback(self, slot):
        if slot == rf.NORMAL_MAP:
            return "flat-normalmap_n.tga"
        if slot == rf.DIFFUSE_MAP:
            return "missing-white.tga"
        return "missing.tga"

    def _image(self, img, stem, slot, flip_green=False):
        suffix = {rf.DIFFUSE_MAP: "di", rf.NORMAL_MAP: "nm", rf.SPECULAR_MAP: "sp", GLOW_MAP: "gl", MASK_MAP: "mk"}.get(slot, "tx")
        name = f"{stem}_{suffix}.tga"
        if name.lower() in self.added:
            return name
        # An image of the kind's own textures, unchanged: copied as it is.
        if self.peg is not None and not img.is_dirty and not flip_green and self.peg.find(img.name) is not None:
            self._keep(img.name)
            return self.peg.find(img.name).name
        rgba = image_pixels(img)
        if flip_green:  # a Blender (OpenGL) normal map: the game's are DirectX
            rgba[..., 1] = 255 - rgba[..., 1]
        self.textures.add(name, rgba, data_map=slot in (rf.NORMAL_MAP, MASK_MAP))
        self.added.add(name.lower())
        return name


def _to_srgb(x):
    x = max(0.0, min(1.0, float(x)))
    return x * 12.92 if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055


GLOW_MAP = 0xDBEA623E
MASK_MAP = 0x03B65AF7


def _upstream_image(socket):
    """The image feeding a socket, through the nodes the game look puts between (mixes by their first
    colour, separate / combine, normal map, to-grey). With it, whether a green flip was passed
    (a Math node taking 1 minus a value: the normal map's DirectX green)."""
    flipped = False
    seen = 0
    while socket is not None and socket.is_linked and seen < 32:
        seen += 1
        node = socket.links[0].from_node
        if node.type == "TEX_IMAGE":
            return node.image, flipped
        if node.type == "MIX":
            socket = node.inputs[6]
        elif node.type == "MIX_RGB":
            socket = node.inputs["Color1"]
        elif node.type in ("NORMAL_MAP",):
            socket = node.inputs["Color"]
        elif node.type in ("RGBTOBW", "SEPARATE_COLOR", "SEPRGB", "GAMMA", "CURVE_RGB", "HUE_SAT", "BRIGHTCONTRAST", "INVERT"):
            socket = node.inputs[-1] if node.type == "INVERT" else node.inputs[0]
        elif node.type in ("COMBINE_COLOR", "COMBRGB"):
            g = node.inputs[1]
            if g.is_linked and g.links[0].from_node.type == "MATH" and g.links[0].from_node.operation == "SUBTRACT":
                flipped = True
            socket = node.inputs[0]
        else:
            return None, flipped
    return None, flipped


def _material_images(mat):
    """{slot hash: (image, green already flipped)} from a material's Principled BSDF."""
    out = {}
    if not mat.use_nodes or mat.node_tree is None:
        return out
    bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if bsdf is None:
        return out
    for slot, sockets in ((rf.DIFFUSE_MAP, ("Base Color",)), (rf.NORMAL_MAP, ("Normal",)),
                          (rf.SPECULAR_MAP, ("Specular IOR Level", "Specular")), (GLOW_MAP, ("Emission Color", "Emission"))):
        for sn in sockets:
            sock = bsdf.inputs.get(sn)
            img, flipped = _upstream_image(sock)
            if img is not None:
                out[slot] = (img, flipped)
                break
    for node in mat.node_tree.nodes:  # the mask (an image node labelled "Mask")
        if node.type == "TEX_IMAGE" and node.image is not None and node.label.lower() == "mask":
            out[MASK_MAP] = (node.image, False)
    return out


def image_pixels(img, largest=2048):
    """An image's pixels as (h, w, 4) uint8, top row first, its sides powers of two (scaled if not)."""
    w, h = img.size
    if w == 0 or h == 0:
        raise ValueError(f"the image {img.name} has no pixels (is its file missing?)")
    pw, ph = rt.power_of_two(w, largest), rt.power_of_two(h, largest)
    src = img
    if (pw, ph) != (w, h):
        src = img.copy()
        src.scale(pw, ph)
    px = np.empty(pw * ph * 4, np.float32)
    src.pixels.foreach_get(px)
    if src is not img:
        bpy.data.images.remove(src)
    px = px.reshape(ph, pw, 4)[::-1]
    return (np.clip(px, 0, 1) * 255 + 0.5).astype(np.uint8)


def _owned(obj):
    """An object's own geometry: itself if a mesh, and the meshes under it that aren't the kit's own
    (a part modelled from several objects), down to the next kit object."""
    out = [obj] if obj.type == "MESH" else []
    stack = list(obj.children)
    while stack:
        o = stack.pop()
        if any(k.startswith("rfa_") for k in o.keys()):
            continue
        if o.type == "MESH" and not o.hide_render:
            out.append(o)
        stack += list(o.children)
    return out


def _submesh(context, objs, frame, materials):
    """A Submesh from objects' evaluated meshes, their vertices in the frame given (a world matrix)."""
    if not isinstance(objs, (list, tuple)):
        objs = [objs]
    sm = rv.Submesh()
    blocks = {}
    for obj in objs:
        _add_geometry(context, obj, frame, materials, sm, blocks)
    sm.blocks = sorted(blocks.items())
    return sm


def _add_geometry(context, obj, frame, materials, sm, blocks):
    eo, me = _evaluated_mesh(context, obj)
    try:
        to_frame = frame.inverted() @ obj.matrix_world
        rot = to_frame.to_3x3()
        nrot = rot.inverted().transposed()
        flip = rot.determinant() < 0
        me.calc_loop_triangles()
        has_uv = len(me.uv_layers) > 0
        if has_uv:
            try:
                me.calc_tangents()
            except Exception:
                pass
        uv = me.uv_layers.active.data if has_uv else None
        if not hasattr(me, "corner_normals") and hasattr(me, "calc_normals_split"):
            me.calc_normals_split()  # before Blender 4.1
        corner_normals = getattr(me, "corner_normals", None)
        index = {}
        for tri in me.loop_triangles:
            mat = me.materials[tri.material_index] if tri.material_index < len(me.materials) else None
            mi = materials.index(mat)
            ids = []
            for li in tri.loops:
                loop = me.loops[li]
                v = me.vertices[loop.vertex_index]
                n = corner_normals[li].vector if corner_normals is not None else loop.normal
                p = to_frame @ v.co
                nn = (nrot @ n).normalized()
                u, w = (uv[li].uv[0], uv[li].uv[1]) if uv else (0.0, 0.0)
                t = (rot @ loop.tangent).normalized() if has_uv else Vector((0, 0, 0))
                if t.length < 0.5:  # no tangents (no UVs, or faces of more than four corners)
                    t = nn.orthogonal().normalized()
                key = (loop.vertex_index, round(nn.x, 3), round(nn.y, 3), round(nn.z, 3), round(u, 5), round(w, 5))
                k = index.get(key)
                if k is None:
                    k = index[key] = len(sm.positions)
                    sm.positions.append(bio.from_blender(p))
                    sm.normals.append(bio.from_blender(nn))
                    tw = 255 if getattr(loop, "bitangent_sign", 1) >= 0 else 0
                    sm.tangents.append(bio.from_blender(t) + (tw,))
                    sm.uvs.append((u, w - 1.0))
                    sm.uvs2.append((u, w - 1.0))
                ids.append(k)
            a, b, c = ids
            tri_game = (a, b, c) if flip else (a, c, b)
            blocks.setdefault(mi, []).append(tri_game)
    finally:
        eo.to_mesh_clear()


def _hull(context, obj, frame):
    """The convex shape round an object's vertices (Blender's convex hull of them), in the frame given."""
    eo, me = _evaluated_mesh(context, obj)
    try:
        m = frame.inverted() @ obj.matrix_world
        pts = [m @ v.co for v in me.vertices]
    finally:
        eo.to_mesh_clear()
    if len(pts) < 4:
        raise ValueError(f"{obj.name}: a collision hull needs at least 4 vertices")
    bm = bmesh.new()
    try:
        for p in pts:
            bm.verts.new(p)
        bmesh.ops.convex_hull(bm, input=bm.verts)
        for v in [v for v in bm.verts if not v.link_faces]:
            bm.verts.remove(v)
        bm.verts.index_update()
        bm.normal_update()
        points = [bio.from_blender(v.co) for v in bm.verts]
        # Blender's faces wind outward in Blender's space; in the game's (axes swapped) the other way.
        faces = [[v.index for v in reversed(f.verts)] for f in bm.faces]
    finally:
        bm.free()
    if len(points) > 255:
        raise ValueError(f"{obj.name}: a collision hull of {len(points)} corners is too many (255 at most): simplify it")
    try:
        return rv.convex_from_faces(points, faces, float(obj.get("rfa_radius", 0.05)))
    except ValueError as ex:
        raise ValueError(f"{obj.name}: {ex}") from None


def build(context, root, game_folder, report):
    """The new vehicle's files from a kit: (container files [(name, bytes)], the model's name, the kind's container)."""
    kind_container = root["rfa_vehicle"]
    name = re.sub(r"[^A-Za-z0-9_\-]+", "_", root.get("rfa_vehicle_name", root.name)).strip("_")
    if not name:
        raise ValueError("give the vehicle a name (its root object's rfa_vehicle_name)")
    container, model = open_kind(game_folder, kind_container)
    subs = model.read_mesh()
    kind = kind_container[: -len(".ccar_pc.str2_pc")]
    peg = None
    cpeg = next((n for n in container.names() if n.lower() == kind.lower() + ".cpeg_pc"), None)
    if cpeg:
        peg = rf.PegFile(container.read(cpeg), container.read(rf.peg_data_name(cpeg)))

    objs = _kit_objects(root)
    by_part = {o["rfa_part"]: o for o in objs if "rfa_part" in o and "rfa_shape" not in o}
    materials = _Materials(model, peg, name, report)
    # Every material the kind had keeps its number when the kit's materials are used (so meshes kept
    # as they are still point at the right ones).
    kit_materials = {m for o in objs if o.type == "MESH" for m in o.data.materials if m is not None}
    for k in range(len(model.materials)):
        found = next((m for m in kit_materials if m.get("rfa_template_material") == k and m.get("rfa_template_kind", kind) == kind), None)
        materials.blender.append(found)
        materials.list.append(materials._make(found) if found is not None else
                              {"block": model.materials[k]["block"], "flags": model.materials[k]["flags"],
                               "textures": [[s, n if materials._keep(n) else materials._fallback(s)] for s, n in model.materials[k]["textures"]]})

    world = {}  # part number -> the frame its object gives (world matrix without scale)
    root_inv = root.matrix_world.inverted()

    def unscaled(m):
        loc, rot, _ = m.decompose()
        return Matrix.Translation(loc) @ rot.to_matrix().to_4x4()

    # The parts: where each now is, relative to its parent part.
    for p in model.parts:
        o = by_part.get(p.index)
        if o is None:
            continue
        world[p.index] = unscaled(root_inv @ o.matrix_world)
    for p in model.parts:
        if p.index not in world:
            continue
        parent = world.get(p.parent, Matrix()) if p.parent >= 0 and not p.vehicle_space else Matrix()
        p.rotation, p.position = placement(parent.inverted() @ world[p.index])

    # The meshes.
    new_subs = list(subs)
    body = by_part.get(-1)
    if body is not None and _owned(body):
        new_subs[0] = _submesh(context, _owned(body), root.matrix_world, materials)
    for p in model.parts:
        o = by_part.get(p.index)
        if o is None:
            continue
        geometry = _owned(o)
        if not geometry:
            continue
        sm = _submesh(context, geometry, root.matrix_world @ world[p.index], materials)
        if 0 <= p.submesh < len(new_subs):
            new_subs[p.submesh] = sm
        else:  # a part the kind didn't draw: its own new submesh
            p.submesh = len(new_subs)
            new_subs.append(sm)
    for o in objs:
        s = o.get("rfa_submesh")
        if s is None or "rfa_part" in o or o.type != "MESH" or not (0 <= s < len(new_subs)):
            continue
        parent_frame = o.parent.matrix_world if o.parent is not None else root.matrix_world
        if "rfa_light" in o:
            frame = unscaled(o.matrix_world)
        elif "rfa_frame" in o:
            values = list(o["rfa_frame"])
            frame = unscaled(parent_frame) @ Matrix([values[i * 4:i * 4 + 4] for i in range(4)])
        else:
            frame = root.matrix_world
        new_subs[s] = _submesh(context, o, frame, materials)
        if "rfa_light" in o:  # the light goes where its meshes are
            rec = o["rfa_light"]
            local = unscaled(parent_frame).inverted() @ frame
            rows, pos = placement(local)
            rv.struct.pack_into("<9f", model.c, rec + 4, *rows)
            rv.struct.pack_into("<3f", model.c, rec + 0x28, *pos)
    model.submeshes = new_subs
    model.materials = materials.list
    vertices = sum(len(sm.positions) for sm in new_subs)
    if vertices > 0xFFFF:
        report({"WARNING"}, f"{vertices:,} vertices: more than 65,535 needs 32-bit indices, which the game's own "
                            "vehicles never use; fewer is safer")

    # Tags.
    tag_objs = {o["rfa_tag"]: o for o in objs if "rfa_tag" in o}
    for t in model.chassis_tags + [t for p in model.parts for t in p.tags]:
        o = tag_objs.get(t.at)
        if o is None:
            continue
        owner = o.parent if o.parent is not None else root
        rows, pos = placement(unscaled(owner.matrix_world).inverted() @ unscaled(o.matrix_world))
        t.rotation, t.position = rows, pos
        if o.name.split(".")[0] != t.name:  # renamed: a point of another name
            t.hash = rv.tag_hash(o.name.split(".")[0])

    # The collision.
    hull_objs = [o for o in objs if "rfa_shape" in o and o.type == "MESH"]
    if hull_objs:
        frames_by_shape = {0: root.matrix_world}
        for p in model.parts:
            if 0 < p.shape < len(model.shapes) and p.index in world and p.shape not in frames_by_shape:
                frames_by_shape[p.shape] = root.matrix_world @ world[p.index]
        shapes = list(model.shapes)
        groups = {}
        for o in hull_objs:
            groups.setdefault(int(o["rfa_shape"]), []).append(o)
        for s, group in groups.items():
            if not (0 <= s < len(shapes)):
                continue
            frame = frames_by_shape.get(s, root.matrix_world)
            hulls = [_hull(context, o, frame) for o in sorted(group, key=lambda o: o.name)]
            shapes[s] = hulls[0] if len(hulls) == 1 and s != 0 else rv.ShapeList(hulls)
        model.set_shapes(shapes)

    c, g = model.write(rebuild_mesh=True)
    cpeg_data, gpeg_data = materials.textures.files()
    files = [(name + ".ccar_pc", c), (name + ".gcar_pc", g), (name + ".cpeg_pc", cpeg_data), (name + ".gpeg_pc", gpeg_data)]
    skip = {kind.lower() + e for e in (".ccar_pc", ".gcar_pc", ".cpeg_pc", ".gpeg_pc")}
    for n in container.names():
        if n.lower() not in skip:
            files.append((n, container.read(n)))
    return files, name, kind


# ---------------------------------------------------------------- into the game


def _read_override_or_game(mod_folder, game_folder, archive, file_name):
    path = os.path.join(mod_folder, "overrides", *archive.split("\\"), file_name)
    if os.path.isfile(path):
        with open(path, "rb") as f:
            return f.read(), path
    pf = bio.GameFiles.archive(game_folder, archive)
    if pf is None or not pf.has(file_name):
        raise ValueError(f"the game has no {archive}\\{file_name}")
    return pf.read(file_name), path


def _write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def _entry_span(text, name):
    m = re.search(r"<Vehicle>\s*<Name>\s*" + re.escape(name) + r"\s*</Name>", text, re.IGNORECASE)
    if not m:
        return None
    end = text.find("</Vehicle>", m.end())
    return (m.start(), end + len("</Vehicle>")) if end >= 0 else None


def _kind_entry(text, kind):
    """The table entry of the vehicle whose model is the kind's (its Mesh_Name)."""
    for m in re.finditer(r"<Vehicle>.*?</Vehicle>", text, re.DOTALL):
        f = re.search(r"<Filename>\s*([^<]+?)\s*</Filename>", m.group(0))
        if f and os.path.splitext(f.group(1).strip())[0].lower() == kind.lower():
            return m.group(0)
    return None


def add_table_entry(text, kind, name, display_name):
    """vehicles.xtbl's text with an entry for the new vehicle (the kind's, renamed), replacing one of that name."""
    entry = _kind_entry(text, kind)
    if entry is None:
        raise ValueError(f"vehicles.xtbl has no vehicle whose model is {kind}")
    entry = re.sub(r"<Name>[^<]*</Name>", f"<Name>{name}</Name>", entry, count=1)
    entry = re.sub(r"<Display_Name>[^<]*</Display_Name>", f"<Display_Name>{display_name}</Display_Name>", entry, count=1)
    entry = re.sub(r"(<Mesh_Name>\s*<Filename>)[^<]*(</Filename>)", r"\g<1>" + name + r".carx\g<2>", entry, count=1)
    span = _entry_span(text, name)
    if span is not None:
        text = text[:span[0]] + entry + text[span[1]:]
    else:
        at = text.rfind("</Table>")
        if at < 0:
            raise ValueError("vehicles.xtbl has no </Table>")
        line = text.rfind("\n", 0, at) + 1  # the start of </Table>'s line
        newline = "\r\n" if "\r\n" in text else "\n"
        text = text[:line] + "\t" + entry + newline + text[line:]
    return text


def remove_table_entry(text, name):
    span = _entry_span(text, name)
    if span is None:
        return text
    end = span[1]
    while end < len(text) and text[end] in "\r\n":
        end += 1
    start = span[0]
    while start > 0 and text[start - 1] in " \t":
        start -= 1
    return text[:start] + text[end:]


def install(game_folder, mod_folder, files, name, kind, display_name):
    """Puts the vehicle into the game: its container, its asset list entry, its table entries. Returns the paths written."""
    written = []
    container, data_start, compressed = rv.write_container(files)
    path = os.path.join(mod_folder, "overrides", "build", "pc", "cache", name + ".ccar_pc.str2_pc")
    _write(path, container)
    written.append(path)

    data, asm_path = _read_override_or_game(mod_folder, game_folder, VEHICLES, ASSET_LIST)
    entries = rv.read_asm(data)
    template = next((e for e in entries if e["name"].lower() == kind.lower() + ".ccar_pc"), None)
    if template is None:
        raise ValueError(f"the asset list has no {kind}")
    entries = [e for e in entries if e["name"].lower() != name.lower() + ".ccar_pc"]
    entries.append(rv.asm_entry(template, name + ".ccar_pc", (container, data_start, compressed), files))
    _write(asm_path, rv.write_asm(entries))
    written.append(asm_path)

    for archive in TABLE_ARCHIVES:
        try:
            data, tpath = _read_override_or_game(mod_folder, game_folder, archive, "vehicles.xtbl")
        except ValueError:
            continue
        text = add_table_entry(data.decode("utf-8", "replace"), kind, name, display_name)
        _write(tpath, text.encode("utf-8"))
        written.append(tpath)
    gw = os.path.join(mod_folder, "mods", "guerrilla_world", "vehicles.xtbl")
    if os.path.isfile(gw):
        with open(gw, "rb") as f:
            text = add_table_entry(f.read().decode("utf-8", "replace"), kind, name, display_name)
        _write(gw, text.encode("utf-8"))
        written.append(gw)
    return written


def uninstall(game_folder, mod_folder, name):
    removed = []
    path = os.path.join(mod_folder, "overrides", "build", "pc", "cache", name + ".ccar_pc.str2_pc")
    if os.path.isfile(path):
        os.remove(path)
        removed.append(path)
    asm_path = os.path.join(mod_folder, "overrides", *VEHICLES.split("\\"), ASSET_LIST)
    if os.path.isfile(asm_path):
        with open(asm_path, "rb") as f:
            entries = rv.read_asm(f.read())
        kept = [e for e in entries if e["name"].lower() != name.lower() + ".ccar_pc"]
        if len(kept) != len(entries):
            _write(asm_path, rv.write_asm(kept))
            removed.append(asm_path)
    tables = [os.path.join(mod_folder, "overrides", *a.split("\\"), "vehicles.xtbl") for a in TABLE_ARCHIVES]
    tables.append(os.path.join(mod_folder, "mods", "guerrilla_world", "vehicles.xtbl"))
    for t in tables:
        if os.path.isfile(t):
            with open(t, "rb") as f:
                text = f.read().decode("utf-8", "replace")
            new = remove_table_entry(text, name)
            if new != text:
                _write(t, new.encode("utf-8"))
                removed.append(t)
    return removed


def kit_root(obj):
    while obj is not None:
        if "rfa_vehicle" in obj:
            return obj
        obj = obj.parent
    return None


def add_hull(context, source, report):
    """A collision hull round a mesh's vertices, for the part it belongs to (its nearest kit part
    above it, or the body): a new wire object in the kit's collision collection."""
    root = kit_root(source)
    if root is None:
        raise ValueError("the mesh isn't in a vehicle kit")
    owner = source
    while owner is not None and "rfa_part" not in owner:
        owner = owner.parent
    if owner is None:
        owner = next((o for o in _kit_objects(root) if o.get("rfa_part") == -1), root)
    shape = int(owner.get("rfa_part_shape", 0))
    scratch = bool(root.get("rfa_scratch"))
    if scratch:
        owner = source if source.get("rfa_role") in PART_ROLES else _owner(source, root)
    eo, me = _evaluated_mesh(context, source)
    try:
        pts = [source.matrix_world @ v.co for v in me.vertices]
    finally:
        eo.to_mesh_clear()
    if len(pts) < 4:
        raise ValueError("a hull needs at least 4 vertices")
    coll = next((c for c in bpy.data.collections if c.name.startswith(root.name + " collision")), None) or context.collection
    me = bpy.data.meshes.new(f"collision {owner.name}")
    bm = bmesh.new()
    for p in pts:
        bm.verts.new(p)
    bmesh.ops.convex_hull(bm, input=bm.verts)
    for v in [v for v in bm.verts if not v.link_faces]:
        bm.verts.remove(v)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(f"collision {owner.name}", me)
    coll.objects.link(obj)
    obj.parent = owner
    obj.matrix_parent_inverse = owner.matrix_world.inverted()
    obj.display_type = "WIRE"
    obj.hide_render = True
    if scratch:
        obj["rfa_hull"] = 1
    else:
        obj["rfa_shape"] = shape
    obj["rfa_radius"] = 0.05
    return obj


# ---------------------------------------------------------------- vehicles made from nothing
#
# A kit with no game vehicle's parts: the root empty (rfa_vehicle: the donor, whose physics settings,
# table entry and part records are used; rfa_scratch), `body` (rfa_role "body": the chassis's mesh,
# and every plain mesh under it), parts (rfa_role "front wheel", "rear wheel" or "piece"; a part's
# mesh is it and the plain meshes under it), collision hulls (rfa_hull: one convex shape each, for the
# part they're under, or the chassis's under the root or the body) and points (rfa_point: empties
# named as the game's points: driver, passenger_<n>, engine, gas_tank, exhaust...). A part without a
# hull gets one round its mesh; the chassis too.

SCRATCH_DONOR = rv.SCRATCH_DONOR + ".ccar_pc.str2_pc"
PART_ROLES = ("front wheel", "rear wheel", "piece")


def _cube_mesh(name, half, centre=(0, 0, 0)):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=2.0)
    for v in bm.verts:
        v.co = Vector((centre[0] + v.co.x * half[0], centre[1] + v.co.y * half[1], centre[2] + v.co.z * half[2]))
    bm.to_mesh(me)
    bm.free()
    return me


def _wheel_mesh(name, radius, width, segments=16):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=segments, radius1=radius, radius2=radius, depth=width,
                          matrix=Matrix.Rotation(1.5707963, 4, "Y"))
    bm.to_mesh(me)
    bm.free()
    return me


def _new_object(coll, name, data, parent=None, location=(0, 0, 0)):
    obj = bpy.data.objects.new(name, data)
    coll.objects.link(obj)
    obj.location = location
    if parent is not None:
        obj.parent = parent
    return obj


def make_scratch_kit(context, game_folder, name, wheels=4, seats=1):
    """A kit to build a vehicle from nothing: a box for a body and wheels to replace (see above)."""
    open_kind(game_folder, SCRATCH_DONOR)  # the donor has to be there
    coll = bpy.data.collections.new(name)
    context.scene.collection.children.link(coll)
    col = bpy.data.collections.new(f"{name} collision")
    coll.children.link(col)
    pts = bpy.data.collections.new(f"{name} points")
    coll.children.link(pts)
    root = _new_object(coll, name, None)
    root.empty_display_type = "ARROWS"
    root["rfa_vehicle"] = SCRATCH_DONOR
    root["rfa_vehicle_name"] = name
    root["rfa_scratch"] = 1

    mat = bpy.data.materials.new(f"{name} paint")
    mat.use_nodes = True
    bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if bsdf is not None:
        bsdf.inputs["Base Color"].default_value = (0.35, 0.33, 0.3, 1.0)

    # Blender's axes: x across, y forward, z up.
    body = _new_object(coll, "body", _cube_mesh("body", (1.0, 2.2, 0.55)), root, (0, 0, 0.35))
    body["rfa_role"] = "body"
    body.data.materials.append(mat)
    hull = _new_object(col, "collision body", _cube_mesh("collision body", (1.0, 2.2, 0.55)), root, (0, 0, 0.35))
    hull["rfa_hull"] = 1
    hull.display_type = "WIRE"
    hull.hide_render = True

    rows = {4: [("f", 1.5), ("b", -1.5)], 6: [("f", 1.6), ("c", 0.0), ("b", -1.6)], 8: [("f", 1.8), ("g", 0.6), ("c", -0.6), ("b", -1.8)]}
    for k, (axle, y) in enumerate(rows.get(int(wheels), rows[4])):
        for side, x in (("l", -1.05), ("r", 1.05)):
            wname = f"wheel_{axle}{side}"
            w = _new_object(coll, wname, _wheel_mesh(wname, 0.45, 0.3), root, (x, y, -0.35))
            w["rfa_role"] = "front wheel" if k == 0 else "rear wheel"
            w.data.materials.append(mat)
            wh = _new_object(col, f"collision {wname}", _wheel_mesh(f"collision {wname}", 0.45, 0.3, 8), w)
            wh["rfa_hull"] = 1
            wh.display_type = "WIRE"
            wh.hide_render = True

    points = [("driver", (-0.4, 0.3, 0.45))] + [(f"passenger_{n}", (0.4 if n % 2 else -0.4, 0.3 - 0.9 * ((n + 1) // 2), 0.45))
                                               for n in range(1, max(1, int(seats)))]
    points += [("engine", (0, 1.6, 0.3)), ("gas_tank", (0.3, -1.6, 0.0)), ("exhaust", (0.6, -2.2, -0.1))]
    for pname, loc in points:
        e = _new_object(pts, pname, None, root, loc)
        e.empty_display_type = "ARROWS"
        e.empty_display_size = 0.3
        e["rfa_point"] = 1
    context.view_layer.objects.active = root
    return root


def _owner(obj, root):
    """The kit object an object belongs to: the nearest part above it, else the root (the chassis)."""
    o = obj.parent
    while o is not None and o is not root:
        if o.get("rfa_role") in PART_ROLES:
            return o
        o = o.parent
    return root


def _auto_hull(context, objs, frame, name):
    """A convex shape round the meshes' vertices (for a part or chassis given no hull)."""
    pts = []
    for o in objs:
        eo, me = _evaluated_mesh(context, o)
        try:
            m = frame.inverted() @ o.matrix_world
            pts += [m @ v.co for v in me.vertices]
        finally:
            eo.to_mesh_clear()
    if len(pts) < 4:
        return None
    bm = bmesh.new()
    try:
        for p in pts:
            bm.verts.new(p)
        bmesh.ops.convex_hull(bm, input=bm.verts)
        for v in [v for v in bm.verts if not v.link_faces]:
            bm.verts.remove(v)
        if len(bm.verts) > 128:  # simpler: a box round it
            lo = Vector([min(v.co[a] for v in bm.verts) for a in range(3)])
            hi = Vector([max(v.co[a] for v in bm.verts) for a in range(3)])
            bm.clear()
            for x in (lo.x, hi.x):
                for y in (lo.y, hi.y):
                    for z in (lo.z, hi.z):
                        bm.verts.new((x, y, z))
            bmesh.ops.convex_hull(bm, input=bm.verts)
        bm.verts.index_update()
        points = [bio.from_blender(v.co) for v in bm.verts]
        faces = [[v.index for v in reversed(f.verts)] for f in bm.faces]
    finally:
        bm.free()
    try:
        return rv.convex_from_faces(points, faces)
    except ValueError:
        return None


def build_scratch(context, root, game_folder, report):
    """The files of a vehicle made from nothing (see make_scratch_kit)."""
    name = re.sub(r"[^A-Za-z0-9_\-]+", "_", root.get("rfa_vehicle_name", root.name)).strip("_")
    if not name:
        raise ValueError("give the vehicle a name")
    container, donor = open_kind(game_folder, root["rfa_vehicle"])
    kind = root["rfa_vehicle"][: -len(".ccar_pc.str2_pc")]
    peg = None
    cpeg = next((n for n in container.names() if n.lower() == kind.lower() + ".cpeg_pc"), None)
    if cpeg:
        peg = rf.PegFile(container.read(cpeg), container.read(rf.peg_data_name(cpeg)))
    objs = _kit_objects(root)
    body = next((o for o in objs if o.get("rfa_role") == "body"), None)
    if body is None:
        raise ValueError("the kit has no body (an object with rfa_role \"body\")")
    order = {"front wheel": 0, "rear wheel": 1, "piece": 2}
    part_objs = sorted((o for o in objs if o.get("rfa_role") in PART_ROLES), key=lambda o: (order[o["rfa_role"]], o.name))
    if not any(o["rfa_role"] in ("front wheel", "rear wheel") for o in part_objs):
        raise ValueError("the vehicle has no wheels (objects with rfa_role \"front wheel\" or \"rear wheel\")")
    index = {o: k for k, o in enumerate(part_objs)}
    root_inv = root.matrix_world.inverted()

    def unscaled(m):
        loc, rot, _ = m.decompose()
        return Matrix.Translation(loc) @ rot.to_matrix().to_4x4()

    W = {o: unscaled(root_inv @ o.matrix_world) for o in part_objs}
    materials = _Materials(donor, peg, name, report)
    body_sm = _submesh(context, _owned(body), root.matrix_world, materials)

    hulls = {}
    for o in objs:
        if o.get("rfa_hull") and o.type == "MESH":
            hulls.setdefault(_owner(o, root), []).append(o)
    points = {}
    for o in objs:
        if o.get("rfa_point"):
            points.setdefault(_owner(o, root), []).append(o)

    def point_list(owner, frame):
        out = []
        for e in sorted(points.get(owner, []), key=lambda e: e.name):
            rows, pos = placement(frame.inverted() @ unscaled(root_inv @ e.matrix_world))
            out.append((rv.tag_hash(e.name.split(".")[0]), rows, pos))
        return out

    chassis_frame = Matrix()
    chassis_shapes = [_hull(context, o, root.matrix_world) for o in hulls.get(root, [])]
    if not chassis_shapes:
        auto = _auto_hull(context, _owned(body), root.matrix_world, "body")
        chassis_shapes = [auto] if auto else []
    parts = []
    for o in part_objs:
        parent = _owner(o, root)
        pk = index.get(parent, -1)
        local = (W[parent].inverted() if pk >= 0 else Matrix()) @ W[o]
        rows, pos = placement(local)
        frame = root.matrix_world @ W[o]
        geometry = _owned(o)
        sm = _submesh(context, geometry, frame, materials) if geometry else None
        hs = [_hull(context, h, frame) for h in hulls.get(o, [])]
        if not hs and geometry:
            auto = _auto_hull(context, geometry, frame, o.name)
            hs = [auto] if auto else []
        shape = None if not hs else (hs[0] if len(hs) == 1 else rv.ShapeList(hs))
        parts.append(rv.ScratchPart(o["rfa_role"], o.name.split(".")[0], pk, rows, pos, sm, shape, point_list(o, W[o])))
    seats = sum(1 for e in points.get(root, []) if e.name.split(".")[0].lower() == "driver" or e.name.lower().startswith("passenger_"))
    c, g = rv.compose(donor, body_sm, chassis_shapes, point_list(root, chassis_frame), parts, materials.list or donor.materials[:1], seats)
    vertices = len(body_sm.positions) + sum(len(p.submesh.positions) for p in parts if p.submesh)
    if vertices > 0xFFFF:
        report({"WARNING"}, f"{vertices:,} vertices: more than 65,535 needs 32-bit indices; fewer is safer")
    cpeg_data, gpeg_data = materials.textures.files()
    files = [(name + ".ccar_pc", c), (name + ".gcar_pc", g), (name + ".cpeg_pc", cpeg_data), (name + ".gpeg_pc", gpeg_data)]
    skip = {kind.lower() + e for e in (".ccar_pc", ".gcar_pc", ".cpeg_pc", ".gpeg_pc")}
    for n in container.names():
        if n.lower() not in skip and not n.lower().endswith(".anim_pc"):
            files.append((n, container.read(n)))
    return files, name, kind


def set_role(obj, role):
    """Makes a mesh of a from-nothing kit a part (a wheel or a piece) or back into the body's."""
    root = kit_root(obj)
    if root is None or not root.get("rfa_scratch"):
        raise ValueError("select a mesh of a vehicle made from nothing (New Vehicle from Nothing)")
    for k in ("rfa_hull", "rfa_point"):
        if k in obj:
            del obj[k]
    if role == "body":
        if "rfa_role" in obj:
            del obj["rfa_role"]
        body = next((o for o in _kit_objects(root) if o.get("rfa_role") == "body"), None)
        if body is not None and obj is not body:
            world = obj.matrix_world.copy()
            obj.parent = body
            obj.matrix_world = world
    else:
        obj["rfa_role"] = role
