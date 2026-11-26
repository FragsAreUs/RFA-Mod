# Ambulance Get In / Get Out with steps, a hand on the door and turning (run in Blender, in the scene RFA
# Vehicle Animation set up for Civ_Emergency_1). The body goes between the LAV set's standing and seated
# poses; the legs and arms are then bent (two-bone IK, worked out here, as the rig's bones are short
# stubs Blender's IK can't use) so the feet land on the ground, the step and the cab floor and a hand
# takes the door, and keyed every other frame.
import json
import math
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

bva = sys.modules["rfa_animation_tools.blender_vehicle_anim"]
bv = sys.modules["rfa_animation_tools.blender_vehicle"]
C = bpy.context
VEHICLE = "Civ_Emergency_1"
root = bva.find_root(C, VEHICLE)
arm = bva.person(C)
scene = C.scene
acts = bva.clip_actions(VEHICLE)
wps = bva.waypoints(root)

# ---- the ambulance: ground, step, cab floor, door, seat (world, metres)
kit = bv._kit_objects(root, people=True)
driver = next(o for o in kit if o.get("rfa_tag") is not None and o.name.split(".")[0] == "driver")
G = min((o.matrix_world @ Vector(c)).z for o in kit
        if o.type == "MESH" and "rfa_part" in o and o.name.startswith("wheel_") and "hub" not in o.name
        and "proxy" not in o.name for c in o.bound_box)
STEP = -1.10    # the step under the door (the body's plate at x -0.9, y 2.5-3.0)
FLOOR = -0.85   # the cab's floor (the door's bottom edge)
HANDLE = Vector((-0.93, 2.40, -0.25))     # the door's rear edge, where it's pulled
PILLAR = Vector((-0.84, 3.30, -0.05))     # the door frame's front edge, held while climbing
DOOR_OPEN = Vector((-1.30, 2.55, -0.18))  # that edge swung out
seat = driver.matrix_world.to_translation()


def W(x, y, z, heading):
    """A root placement in the world: on the ground point (x, y, z), facing heading degrees from +Y."""
    return Matrix.Translation((x, y, z)) @ Matrix.Rotation(math.radians(heading), 4, "Z")


ENTER = (-1.33, 2.81)
EXIT = (-1.40, 2.55)
wps["Enter L"].matrix_world = W(ENTER[0], ENTER[1], G, 0)
wps["Exit"].matrix_world = W(EXIT[0], EXIT[1], G, 90)
wps["Drive"].matrix_world = W(seat.x, seat.y, -99, 0)  # (set below, once the seat's height is known)


# ---- the two poses
def capture(clip, frame):
    bva.show_clip(C, root, arm, clip)
    scene.frame_set(frame)
    pose = {pb.name: (pb.location.copy(), pb.rotation_quaternion.copy()) for pb in arm.pose.bones}
    heads = {n: arm.pose.bones[n].head.copy() for n in ("Pelvis", "L-Foot", "R-Foot", "Head")}
    return pose, heads


def shift_bone(action, d, bone="Pelvis"):
    """Moves the pelvis by d (its own location axes) at every key of the action, keying it where the
    clip didn't (the Drive clip holds the pelvis still, so it has no location keys)."""
    path = 'pose.bones["%s"].location' % bone
    frames = sorted({int(round(kp.co[0])) for fc in action.fcurves if fc.data_path.startswith('pose.bones["%s"]' % bone)
                     for kp in fc.keyframe_points}) or [int(action.frame_range[0])]
    for i in range(3):
        fc = action.fcurves.find(path, index=i)
        if fc is None:
            fc = action.fcurves.new(path, index=i, action_group=bone)
            base = 0.0  # unkeyed: the rest place
            for f in frames:
                fc.keyframe_points.insert(f, base)
        for kp in fc.keyframe_points:
            kp.co[1] += d[i]
            kp.handle_left[1] += d[i]
            kp.handle_right[1] += d[i]
        fc.update()


def down_in_axes(dz, bone="Pelvis"):
    """Lowering the pelvis by dz in the armature's up, written in the pelvis's own location axes."""
    pb = arm.pose.bones[bone]
    b = pb.bone
    if b.parent:
        r = (pb.parent.matrix @ b.parent.matrix_local.inverted() @ b.matrix_local).to_3x3()
    else:
        r = b.matrix_local.to_3x3()
    return r.inverted() @ Vector((0, 0, -dz))


# The Drive clip back as the game had it (an earlier run lowered its pelvis), to take the seated pose from.
LOWER = ("root",)  # the root bone: the game's own player clips move it (never Pelvis); everything hangs off it
for bone in ("Pelvis", "Spine", "root"):  # older runs lowered Pelvis and Spine: undone first
    key = "rfa_lowered_" + bone
    if bone == "Pelvis" and acts["drive"].get("rfa_lowered_v") is not None:
        acts["drive"][key] = list(acts["drive"]["rfa_lowered_v"])
        del acts["drive"]["rfa_lowered_v"]
    if acts["drive"].get(key) is not None:
        shift_bone(acts["drive"], [-x for x in acts["drive"][key]], bone)
        del acts["drive"][key]
standing, stand_heads = capture("exit", int(round(acts["exit"].frame_range[1])))
seated, seat_heads = capture("drive", 0)
print("root bone turned in the seated pose:", round(arm.pose.bones["root"].rotation_quaternion.angle, 3) if "root" in arm.pose.bones else None)


def flat(v):
    return Vector((v.x, v.y, 0.0))


STAND_OFF, SEAT_OFF = flat(stand_heads["Pelvis"]), flat(seat_heads["Pelvis"])

# The seat: low enough that his head clears the cab's roof (the body's underside above the driver's point),
# not below the cushion.
body = next(o for o in kit if o.name.split(".")[0] == "body" and o.type == "MESH")
verts = [body.matrix_world @ v.co for v in body.data.vertices]
near = [v for v in verts if abs(v.x - seat.x) < 0.22 and abs(v.y - (seat.y + SEAT_OFF.y * 0)) < 0.3]
ROOF = min((v.z for v in near if v.z > seat.z + 0.45), default=seat.z + 1.2)
CUSHION = max((v.z for v in near if v.z < seat.z + 0.05), default=seat.z - 0.2)
head_up = seat_heads["Head"].z + 0.24        # the top of his head above the seated pose's root
pelvis_up = seat_heads["Pelvis"].z
SEAT_Z = min(seat.z - 0.55, ROOF - 0.06 - head_up)
SEAT_Z = max(SEAT_Z, CUSHION + 0.04 - pelvis_up)
print("roof", round(ROOF, 2), "cushion", round(CUSHION, 2), "head top", round(SEAT_Z + head_up, 2), "pelvis", round(SEAT_Z + pelvis_up, 2))

# The game sits him with his root on the seat's own point (the vehicle's "driver" tag), whatever the Drive
# waypoint says (the LAV's seated pose, its pelvis 0.6 m over its root, put his head through the roof). So
# the seat's root is the tag, and the seated pose's pelvis is lowered by the difference, in the Drive clip
# too, so the body sits where SEAT_Z puts it.
DROP = seat.z - SEAT_Z
bva.show_clip(C, root, arm, "drive")
scene.frame_set(0)
for bone in LOWER:
    DV = down_in_axes(DROP, bone)
    loc, rot = seated[bone]
    seated[bone] = (loc + DV, rot)
    shift_bone(acts["drive"], DV, bone)
    acts["drive"]["rfa_lowered_" + bone] = list(DV)
print("seat: root on the driver's point, pelvis lowered", round(DROP, 2))
FOOT = {"L": stand_heads["L-Foot"] - STAND_OFF, "R": stand_heads["R-Foot"] - STAND_OFF}  # ankles off the pelvis's ground point


# ---- curves through keys
def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def track(keys, f):
    """A value at frame f from [(frame, value)] (values: floats, Vectors or Matrices), eased between."""
    if f <= keys[0][0]:
        return keys[0][1]
    for (f0, v0), (f1, v1) in zip(keys, keys[1:]):
        if f <= f1:
            t = smooth((f - f0) / (f1 - f0)) if f1 > f0 else 1.0
            if isinstance(v0, Matrix):
                l0, r0, _ = v0.decompose()
                l1, r1, _ = v1.decompose()
                return Matrix.Translation(l0.lerp(l1, t)) @ r0.slerp(r1, t).to_matrix().to_4x4()
            if isinstance(v0, Vector):
                return v0.lerp(v1, t)
            return v0 + (v1 - v0) * t
    return keys[-1][1]


def steps(plants, f, lift=0.22):
    """A foot's ankle at frame f from its plants [(from, to, where)]: held while planted, swung up and
    over between them; None before the first."""
    if f < plants[0][0]:
        return None
    for i, (a, b, where) in enumerate(plants):
        if a <= f <= b:
            return where
        if i + 1 < len(plants) and b < f < plants[i + 1][0]:
            nxt = plants[i + 1]
            t = (f - b) / (nxt[0] - b)
            p = where.lerp(nxt[2], smooth(t))
            p.z += lift * math.sin(math.pi * t) + max(0.0, nxt[2].z - where.z) * 0.3 * math.sin(math.pi * t)
            return p
    return plants[-1][2]


def foot_at(root_world, side):
    """Where the standing pose's ankle is with the root (ground point under the pelvis) there."""
    return root_world @ FOOT[side]


# ---- the IK
def rot_between(a, b):
    return a.normalized().rotation_difference(b.normalized())


def about(point, q):
    return Matrix.Translation(point) @ q.to_matrix().to_4x4() @ Matrix.Translation(-point)


def two_bone(b1, b2, b3, target_world, keep_end=True):
    """Bends b1 and b2 (pose bones) so b3's head reaches the target; b3 keeps its pose-space turn."""
    inv = arm.matrix_world.inverted()
    T = inv @ target_world
    H, K, A = b1.head.copy(), b2.head.copy(), b3.head.copy()
    M1, M2, M3 = b1.matrix.copy(), b2.matrix.copy(), b3.matrix.copy()
    l1, l2 = (K - H).length, (A - K).length
    d = T - H
    dist = max(abs(l1 - l2) + 1e-3, min((l1 + l2) * (0.995 if keep_end else 0.96), d.length))
    dn = d.normalized()
    bend = (K - H) - dn * (K - H).dot(dn)
    if bend.length < 1e-5:
        bend = Vector((0, 1, 0)) - dn * dn.y
    bn = bend.normalized()
    ca = max(-1.0, min(1.0, (l1 * l1 + dist * dist - l2 * l2) / (2 * l1 * dist)))
    a = math.acos(ca)
    K2 = H + (dn * math.cos(a) + bn * math.sin(a)) * l1
    A2 = H + dn * dist
    R1 = rot_between(K - H, K2 - H)
    A1 = K2 + R1 @ (A - K)
    R2 = rot_between(A1 - K2, A2 - K2)
    b1.matrix = about(H, R1) @ M1
    C.view_layer.update()
    b2.matrix = about(K2, R2) @ about(H, R1) @ M2
    C.view_layer.update()
    if keep_end:
        m = M3.copy()
        m.translation = A2
        b3.matrix = m
        C.view_layer.update()


CHAINS = {
    "L": ("L-Thigh", "L-Calf", "L-Foot"),
    "R": ("R-Thigh", "R-Calf", "R-Foot"),
    "LH": ("l-upperarm", "l-forearm", "L-Hand"),
    "RH": ("r-upperarm", "r-forearm", "R-Hand"),
}


# ---- making a clip
_bio = sys.modules["rfa_animation_tools.blender_io"]
_NAMES = _bio.bone_names(arm)
GAME_BONES = {_NAMES[i] for i in list(range(0, 50)) + list(range(96, 108)) + list(range(109, 117)) if i < len(_NAMES)}
# Twist bones hung off the collarbone, not the arm: they must turn with the arm or the skin on them stays behind.
TWIST = {"L-upperarm-twst1": "l-upperarm", "r-upperarm-twst1": "r-upperarm"}


def follow_twists(rel):
    """Each twist bone back where it was against its arm (rel: the pose before the arm was moved)."""
    for t, a in TWIST.items():
        m = arm.pose.bones[a].matrix @ rel[t]
        m.translation = arm.pose.bones[t].matrix.translation
        arm.pose.bones[t].matrix = m
        C.view_layer.update()


def keep_game_bones(action):
    """Only the bones the game's own clips animate keep keys (the others back to rest)."""
    for fc in list(action.fcurves):
        if fc.data_path.startswith('pose.bones["'):
            n = fc.data_path[len('pose.bones["'):].split('"]', 1)[0]
            if n not in GAME_BONES:
                action.fcurves.remove(fc)
    for pb in arm.pose.bones:
        if pb.name not in GAME_BONES:
            pb.location = (0, 0, 0)
            pb.rotation_quaternion = (1, 0, 0, 0)
            pb.scale = (1, 1, 1)


SPINE = (("Spine", 0.45), ("Spine1", 0.35), ("Spine2", 0.2))
LOOK = (("Neck", 0.4), ("Head", 0.6))
BODY_EXTRA = [n for n, _ in SPINE + LOOK]


def sway(f, lean_keys, look_keys):
    """The body's own motion on top of the two poses: leaning forward (radians) and looking at a point."""
    lean = track(lean_keys, f) if lean_keys else 0.0
    if abs(lean) > 1e-4:
        for n, share in SPINE:
            pb = arm.pose.bones[n]
            pb.matrix = about(pb.head, Quaternion((1, 0, 0), -lean * share)) @ pb.matrix
            C.view_layer.update()
    if look_keys:
        target, w = track(look_keys[0], f), track(look_keys[1], f)
        if w > 1e-3:
            inv = arm.matrix_world.inverted()
            head = arm.pose.bones["Head"]
            want = (inv @ target) - head.head
            want.z *= 0.6
            q = Vector((0, 1, 0)).rotation_difference(want.normalized())
            angle = min(q.angle, math.radians(55)) * w
            for n, share in LOOK:
                pb = arm.pose.bones[n]
                pb.matrix = about(pb.head, Quaternion(q.axis, angle * share)) @ pb.matrix
                C.view_layer.update()


def fix_signs(action):
    """Each bone's (and the root's) rotation keys kept on one side, so nothing turns the long way between keys."""
    groups = {}
    for fc in action.fcurves:
        if fc.data_path.endswith("rotation_quaternion"):
            groups.setdefault(fc.data_path, [None] * 4)[fc.array_index] = fc
    for fcs in groups.values():
        if None in fcs:
            continue
        n = len(fcs[0].keyframe_points)
        if any(len(fc.keyframe_points) != n for fc in fcs):
            continue
        prev = None
        for k in range(n):
            q = Quaternion([fc.keyframe_points[k].co[1] for fc in fcs])
            if prev is not None and prev.dot(q) < 0:
                for fc in fcs:
                    kp = fc.keyframe_points[k]
                    kp.co[1] = -kp.co[1]
                    kp.handle_left[1] = -kp.handle_left[1]
                    kp.handle_right[1] = -kp.handle_right[1]
                q = -q
            prev = q
        for fc in fcs:
            fc.update()


def build(clip, length, root_keys, blend_keys, feet, hands, triggers, lean_keys=None, look_keys=None):
    """root_keys [(frame, world placement)], blend_keys [(frame, 0 standing .. 1 seated)], feet
    {"L"/"R": (plants, weight keys)}, hands {"LH"/"RH": (target keys, weight keys)}."""
    old = acts[clip]
    start_wp = wps[old["rfa_from_waypoint"]]
    arm.animation_data.action = None
    arm.parent = start_wp
    arm.matrix_parent_inverse = Matrix()
    new = bpy.data.actions.new(old.name + " new")
    for k in ("rfa_name", "rfa_vehicle", "rfa_clip", "rfa_from_waypoint", "rfa_to_waypoint", "rfa_header"):
        if k in old:
            new[k] = old[k]
    new["rfa_source"] = "made from the LAV's standing and seated poses, with steps and hands"
    new["rfa_triggers"] = json.dumps(triggers)
    arm.animation_data.action = new
    arm.rotation_mode = "QUATERNION"
    for pb in arm.pose.bones:
        pb.rotation_mode = "QUATERNION"
    inv_start = start_wp.matrix_world.inverted()

    # The body: standing to seated as blend_keys say, every 4 frames (bones), and the root every 2.
    prev = {}
    for f in range(0, length + 1, 4):
        s = track(blend_keys, f)
        for pb in arm.pose.bones:
            l0, r0 = standing[pb.name]
            l1, r1 = seated[pb.name]
            if r0.dot(r1) < 0:
                r1 = -r1
            q = r0.slerp(r1, s)
            if pb.name in prev and prev[pb.name].dot(q) < 0:
                q.negate()
            prev[pb.name] = q
            pb.location = l0.lerp(l1, s)
            pb.rotation_quaternion = q
            pb.keyframe_insert("location", frame=f)
            pb.keyframe_insert("rotation_quaternion", frame=f)
    for f in range(0, length + 1, 2):
        s = track(blend_keys, f)
        off = STAND_OFF.lerp(SEAT_OFF, s)
        world = track(root_keys, f) @ Matrix.Translation(-off)
        loc, rot, _ = (inv_start @ world).decompose()
        arm.location = loc
        arm.rotation_quaternion = rot
        arm.keyframe_insert("location", frame=f)
        arm.keyframe_insert("rotation_quaternion", frame=f)

    # The limbs: bent each other frame, worked out on the body as keyed, then keyed over it.
    out = {}
    for f in range(0, length + 1):
        scene.frame_set(f)
        base = {n: arm.pose.bones[n].rotation_quaternion.copy() for c in CHAINS.values() for n in c}
        rel = {t: arm.pose.bones[a].matrix.inverted() @ arm.pose.bones[t].matrix for t, a in TWIST.items()}
        sway(f, lean_keys, look_keys)
        for n in BODY_EXTRA:
            out.setdefault(n, []).append((f, arm.pose.bones[n].rotation_quaternion.copy(), None, 1.0))
        done = {}
        for side, (plants, weights) in feet.items():
            w = track(weights, f)
            target = steps(plants, f)
            if w > 1e-3 and target is not None:
                two_bone(*(arm.pose.bones[n] for n in CHAINS[side]), target)
                done[side] = w
        for side, (targets, weights) in hands.items():
            w = track(weights, f)
            if w > 1e-3:
                two_bone(*(arm.pose.bones[n] for n in CHAINS[side]), track(targets, f), keep_end=False)
                done[side] = w
        for side, w in done.items():
            for n in CHAINS[side]:
                ik = arm.pose.bones[n].rotation_quaternion.copy()
                if base[n].dot(ik) < 0:
                    ik.negate()
                q = base[n].slerp(ik, w)
                out.setdefault(n, []).append((f, q, arm.pose.bones[n].location.copy() if n.endswith("Foot") else None, w))
        for n, keys in out.items():  # the arms as they'll be keyed this frame, then the twist bones after them
            if keys and keys[-1][0] == f and n in ("l-upperarm", "r-upperarm", "l-forearm", "r-forearm"):
                arm.pose.bones[n].rotation_quaternion = keys[-1][1]
        C.view_layer.update()
        follow_twists(rel)
        for t in TWIST:
            out.setdefault(t, []).append((f, arm.pose.bones[t].rotation_quaternion.copy(), None, 1.0))
    for n, keys in out.items():
        pb = arm.pose.bones[n]
        for f, q, loc, w in keys:
            pb.rotation_quaternion = q
            pb.keyframe_insert("rotation_quaternion", frame=f)
    fix_signs(new)
    keep_game_bones(new)
    arm.animation_data.action = None
    name = old.name
    bpy.data.actions.remove(old)
    new.name = name
    return new


# ---- Get In (60 frames a second)
E = W(ENTER[0], ENTER[1], G, 0)
E_CAB = W(ENTER[0], ENTER[1], G, -90)                     # turned to face the cab
ON_STEP = W(-1.12, 2.80, STEP, -90)
IN_DOOR = W(-0.84, 2.80, FLOOR, -90)
wps["Drive"].matrix_world = W(seat.x, seat.y, seat.z, 0)
SEAT = W(seat.x, seat.y, seat.z, 0)
step_L = Vector((-0.98, 2.92, STEP)) + Vector((0, 0, FOOT["L"].z))
floor_R = Vector((-0.70, 2.66, FLOOR)) + Vector((0, 0, FOOT["R"].z))
floor_L = Vector((-0.70, 2.94, FLOOR)) + Vector((0, 0, FOOT["L"].z))
enter = build(
    "enter", 205,
    [(0, E), (16, E_CAB), (78, E_CAB), (84, E_CAB @ Matrix.Translation((0, 0.08, 0))), (102, ON_STEP), (120, IN_DOOR),
     (152, SEAT), (205, SEAT)],
    [(0, 0.0), (100, 0.0), (118, 0.45), (150, 1.0), (205, 1.0)],
    {"L": ([(16, 70, foot_at(E_CAB, "L")), (82, 114, step_L), (126, 160, floor_L)],
           [(0, 0.0), (15, 0.0), (16, 1.0), (130, 1.0), (152, 0.0)]),
     "R": ([(16, 86, foot_at(E_CAB, "R")), (106, 160, floor_R)],
           [(0, 0.0), (15, 0.0), (16, 1.0), (130, 1.0), (152, 0.0)])},
    {"RH": ([(18, HANDLE), (40, HANDLE), (62, DOOR_OPEN), (75, DOOR_OPEN)],
            [(18, 0.0), (36, 1.0), (62, 1.0), (78, 0.0)]),
     "LH": ([(78, PILLAR), (118, PILLAR), (150, DOOR_OPEN), (170, DOOR_OPEN), (190, HANDLE + Vector((-0.05, 0, 0)))],
            [(74, 0.0), (88, 1.0), (114, 1.0), (128, 0.0), (152, 0.0), (168, 1.0), (190, 1.0), (204, 0.0)])},
    [("door start", 40), ("door end", 62), ("door close start", 172), ("door close end", 190)],
    lean_keys=[(0, 0.0), (16, 0.06), (40, 0.12), (70, 0.06), (84, 0.35), (100, 0.45), (118, 0.55), (136, 0.3),
               (152, 0.0), (205, 0.0)],
    look_keys=([(0, HANDLE), (60, HANDLE), (76, step_L), (100, PILLAR), (130, PILLAR), (160, DOOR_OPEN), (190, HANDLE)],
               [(0, 0.0), (12, 0.8), (55, 0.6), (76, 0.7), (100, 0.4), (130, 0.0), (160, 0.5), (190, 0.3), (205, 0.0)]))

# ---- Get Out
D = W(seat.x, seat.y, seat.z, 0)
OUT_DOOR = W(-0.84, 2.78, FLOOR, 90)                      # turned to face out
DOWN_STEP = W(-1.10, 2.74, STEP, 90)
X = W(EXIT[0], EXIT[1], G, 90)
step_R = Vector((-0.98, 2.62, STEP)) + Vector((0, 0, FOOT["R"].z))
door_L = Vector((-0.74, 2.92, FLOOR)) + Vector((0, 0, FOOT["L"].z))
door_R = Vector((-0.74, 2.62, FLOOR)) + Vector((0, 0, FOOT["R"].z))
exit_ = build(
    "exit", 170,
    [(0, D), (38, D), (72, OUT_DOOR), (96, DOWN_STEP), (120, X), (170, X)],
    [(0, 1.0), (36, 1.0), (52, 0.55), (76, 0.0), (170, 0.0)],
    {"R": ([(62, 78, door_R), (92, 108, step_R), (124, 170, foot_at(X, "R"))],
           [(0, 0.0), (55, 0.0), (66, 1.0), (170, 1.0)]),
     "L": ([(62, 100, door_L), (114, 170, foot_at(X, "L"))],
           [(0, 0.0), (55, 0.0), (66, 1.0), (170, 1.0)])},
    {"LH": ([(4, HANDLE), (18, HANDLE), (38, DOOR_OPEN), (48, DOOR_OPEN), (60, PILLAR), (98, PILLAR)],
            [(4, 0.0), (16, 1.0), (38, 1.0), (50, 0.3), (62, 1.0), (96, 1.0), (110, 0.0)]),
     "RH": ([(126, DOOR_OPEN), (140, DOOR_OPEN), (156, HANDLE + Vector((-0.05, 0, 0)))],
            [(126, 0.0), (138, 1.0), (156, 1.0), (168, 0.0)])},
    [("door start", 18), ("door end", 38), ("door close start", 140), ("door close end", 156)],
    lean_keys=[(0, 0.0), (36, 0.05), (55, 0.45), (72, 0.5), (90, 0.4), (110, 0.15), (125, 0.0), (170, 0.0)],
    look_keys=([(0, HANDLE), (40, DOOR_OPEN), (70, step_R), (100, foot_at(X, "R")), (130, DOOR_OPEN), (160, HANDLE)],
               [(0, 0.5), (20, 0.8), (45, 0.5), (70, 0.7), (100, 0.5), (118, 0.0), (130, 0.5), (156, 0.3), (170, 0.0)]))

scene.timeline_markers.clear()
bva.show_clip(C, root, arm, "enter")
print("made: Get In 0-205, Get Out 0-170; ground", round(G, 2), "seat", tuple(round(v, 2) for v in seat))
