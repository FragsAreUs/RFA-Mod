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


def make_mesh(context, mesh, name, arm_obj):
    """A character's mesh, skinned to an armature by vertex groups named after its bones."""
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
        mat_name = f"rfa_material_{m}"
        mat = bpy.data.materials.get(mat_name) or bpy.data.materials.new(mat_name)
        me.materials.append(mat)
        slot[m] = len(me.materials) - 1
    me.polygons.foreach_set("material_index", [slot[m] for *_, m in mesh.triangles])

    uv = me.uv_layers.new(name="UVMap")
    loop_uvs = []
    for poly in me.polygons:
        for li in poly.loop_indices:
            u, v = mesh.uvs[me.loops[li].vertex_index]
            loop_uvs += (u, 1.0 - v)
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

    @classmethod
    def forget(cls):
        cls._open.clear()


def override_path(mod_folder, archive_relative, file_name):
    """Where an override of a file in an archive goes: overrides\\<the archive's path>\\<the file>."""
    return os.path.join(mod_folder, "overrides", *archive_relative.split("\\"), file_name)
