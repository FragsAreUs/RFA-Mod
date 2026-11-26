# LAV Get In, from scratch: walk up behind the LAV, pull the rear hatch's handle, step back while the
# ramp swings down, walk up the ramp and along the troop bay (ducking under the roof), slide into the
# driver's seat and sit; the ramp closes behind. Run in Blender, in the scene RFA Vehicle Animation set
# up for RF_LAV_1. The body goes between the LAV set's standing and seated poses; legs and arms are bent
# (two-bone IK) so the feet walk on the ground, the ramp and the floor.
import json
import math
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

bva = sys.modules["rfa_animation_tools.blender_vehicle_anim"]
bv = sys.modules["rfa_animation_tools.blender_vehicle"]
C = bpy.context
VEHICLE = "RF_LAV_1"
root = bva.find_root(C, VEHICLE)
arm = bva.person(C)
scene = C.scene
acts = bva.clip_actions(VEHICLE)
wps = bva.waypoints(root)
ENTER_WP = acts["enter"]["rfa_from_waypoint"]
SEAT_WP = acts["enter"]["rfa_to_waypoint"]

# ---- the LAV (world, metres; +Y is the front)
kit = bv._kit_objects(root, people=True)
driver = next(o for o in kit if o.get("rfa_tag") is not None and o.name.split(".")[0] == "driver")
G = min((o.matrix_world @ Vector(c)).z for o in kit
        if o.type == "MESH" and "rfa_part" in o and o.name.startswith("wheel_") and "hub" not in o.name
        and "proxy" not in o.name for c in o.bound_box)
seat = driver.matrix_world.to_translation()
HINGE_Y, HINGE_Z = -2.98, -0.63          # the rear hatch's hinge (its bottom edge), across the LAV
RAMP_END_Y = -4.30                         # where the open ramp meets the ground
BAY = -0.85                                # the troop bay's floor
CAB = -0.56                                # the floor at the front, by the driver
HANDLE = Vector((0.30, -3.10, -0.25))      # the hatch's handle (its outside face, chest high)


def W(x, y, z, heading):
    """A root placement in the world: on the ground point (x, y, z), facing heading degrees from +Y."""
    return Matrix.Translation((x, y, z)) @ Matrix.Rotation(math.radians(heading), 4, "Z")


def surf(y, ramp_open=True):
    """The height the feet stand on at y along the LAV (ground, ramp, bay floor, the step up by the cab)."""
    if y < RAMP_END_Y or not ramp_open and y < HINGE_Y:
        return G
    if y < HINGE_Y + 0.05:
        t = (y - RAMP_END_Y) / (HINGE_Y + 0.05 - RAMP_END_Y)
        return G + (BAY - G) * t
    if y < 0.15:
        return BAY
    if y < 0.40:
        return BAY + (CAB - BAY) * (y - 0.15) / 0.25
    return CAB


START = (0.10, -5.60)
wps[ENTER_WP].matrix_world = W(START[0], START[1], G, 0)


def rest_pose():
    for pb in arm.pose.bones:
        pb.location = (0, 0, 0)
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.scale = (1, 1, 1)


# ---- the two poses
def capture(clip, frame):
    rest_pose()
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
LOWER = ("Pelvis", "Spine")  # the two bones hung off the root (the game ignores the root bone's own height when seated)
for bone in ("Pelvis", "Spine", "root"):  # older runs lowered Pelvis and Spine: undone first
    key = "rfa_lowered_" + bone
    if bone == "Pelvis" and acts["drive"].get("rfa_lowered_v") is not None:
        acts["drive"][key] = list(acts["drive"]["rfa_lowered_v"])
        del acts["drive"]["rfa_lowered_v"]
    if acts["drive"].get(key) is not None:
        shift_bone(acts["drive"], [-x for x in acts["drive"][key]], bone)
        del acts["drive"][key]
# The standing pose is the end of the LAV's own Get Out (pc_exit_lav). Our Get Out replaces it, so a copy of
# the original is kept (no vehicle or clip of its own, so it's never shown or exported as a clip).
ORIGINAL_EXIT = VEHICLE + " pc_exit_lav (standing pose)"
if bpy.data.actions.get(ORIGINAL_EXIT) is None:
    if acts["exit"].get("rfa_source") != "pc_exit_lav":
        raise RuntimeError("Get Out no longer starts from pc_exit_lav and no copy of it is kept: start it from pc_exit_lav again (Start Clip From...)")
    keep = acts["exit"].copy()
    keep.name = ORIGINAL_EXIT
    keep.use_fake_user = True
    for k in ("rfa_vehicle", "rfa_clip", "rfa_name"):
        if k in keep:
            del keep[k]


def capture_action(action, frame):
    rest_pose()
    arm.animation_data.action = action
    scene.frame_set(frame)
    pose = {pb.name: (pb.location.copy(), pb.rotation_quaternion.copy()) for pb in arm.pose.bones}
    heads = {n: arm.pose.bones[n].head.copy() for n in ("Pelvis", "L-Foot", "R-Foot", "Head")}
    arm.animation_data.action = None
    return pose, heads


_orig = bpy.data.actions[ORIGINAL_EXIT]
standing, stand_heads = capture_action(_orig, int(round(_orig.frame_range[1])))
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


def steps(plants, f, lift=0.10):
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


def place_camera(eye, target):
    """The camera bones: camera-lookat at the point looked at, camera (its child) at the eye (world points)."""
    inv = arm.matrix_world.inverted()
    E, T = inv @ eye, inv @ target
    lb, cb = arm.pose.bones["camera-lookat"], arm.pose.bones["camera"]
    now = cb.head - lb.head
    turn = rot_between(now, E - T) if now.length > 1e-4 else Quaternion()
    m = lb.matrix.copy()
    lb.matrix = Matrix.Translation(T) @ (turn.to_matrix().to_4x4() @ m.to_3x3().to_4x4())
    C.view_layer.update()
    m = cb.matrix.copy()
    m.translation = E
    cb.matrix = m
    C.view_layer.update()
    for pb in (lb, cb):
        pb.keyframe_insert("location", frame=scene.frame_current)
        pb.keyframe_insert("rotation_quaternion", frame=scene.frame_current)


def build(clip, length, root_at, blend_keys, feet, hands, triggers, lean_keys=None, look_keys=None, camera=None, swing_keys=None):
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
    new["rfa_source"] = "made from scratch: walking in through the rear hatch"
    if camera:
        triggers = triggers + [("camera lookat start", 2), ("camera pos start", 2),
                               ("camera lookat end", length - 2), ("camera pos end", length - 2)]
    new["rfa_triggers"] = json.dumps(sorted(triggers, key=lambda t: t[1]))
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
        world = root_at(f) @ Matrix.Translation(-off)
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
        if swing_keys:
            a = track(swing_keys, f)
            # hips turn with the leg going forward, shoulders against them; the arms swing, the elbows bend more
            # as an arm comes forward
            for n, turn in (("Pelvis", 0.45 * a), ("Spine2", -0.35 * a)):
                pb = arm.pose.bones[n]
                pb.matrix = about(pb.head, Quaternion((0, 0, 1), turn)) @ pb.matrix
                C.view_layer.update()
            for n, fore, sgn in (("l-upperarm", "l-forearm", 1), ("r-upperarm", "r-forearm", -1)):
                pb = arm.pose.bones[n]
                pb.matrix = about(pb.head, Quaternion((1, 0, 0), a * sgn)) @ pb.matrix
                C.view_layer.update()
                fb = arm.pose.bones[fore]
                bend = (0.12 + 1.1 * max(0.0, a * sgn)) * min(1.0, abs(a) / 0.05 + 0.3)
                fb.matrix = about(fb.head, Quaternion((1, 0, 0), bend)) @ fb.matrix
                C.view_layer.update()
        for n in BODY_EXTRA + (["Pelvis", "l-upperarm", "r-upperarm", "l-forearm", "r-forearm"] if swing_keys else []):
            out.setdefault(n, []).append((f, arm.pose.bones[n].rotation_quaternion.copy(), None, 1.0))
        done = {}
        for side, (plants, weights) in feet.items():
            w = track(weights, f)
            target = steps(plants, f)
            if w > 1e-3 and target is not None:
                two_bone(*(arm.pose.bones[n] for n in CHAINS[side]), target)
                done[side] = w
        for side, (targets, weights) in hands.items():
            if side in done:
                continue
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
        if camera:
            eye, target = camera(f, arm.matrix_world @ arm.pose.bones["Spine2"].head)
            place_camera(eye, target)
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




# ---- the walk: alternating footsteps along the root's path
def foot_on(P, side, ramp_open=True):
    v = P @ FOOT[side]
    v.z = surf(v.y, ramp_open) + FOOT[side].z
    return v


plants = {"L": [[0, 10 ** 6, None]], "R": [[0, 10 ** 6, None]]}
swing = [(0, 0.0)]


def walk(path, fa, fb, n, first="R", ramp_open=True, arms=0.22, settle=True):
    """n footsteps from frame fa to fb, the root on path(f); each foot lands where it stands at mid-stance."""
    T = (fb - fa) / n
    for k in range(n):
        side = first if k % 2 == 0 else ("L" if first == "R" else "R")
        t0, t1 = fa + k * T, fa + (k + 1) * T
        tm = fb if k >= n - 2 else min(fb, t1 + T / 2)
        plants[side][-1][1] = int(round(t0))
        plants[side].append([int(round(t1)), 10 ** 6, foot_on(path(tm), side, ramp_open)])
        # the arms swing against the legs: the left arm forward as the right foot goes forward
        swing.append((int(round((t0 + t1) / 2)), arms * (1 if side == "R" else -1) * (0.5 if k in (0, n - 1) else 1)))
    if settle:
        swing.append((int(round(fb + T / 2)), 0.0))


# ---- the root: where it goes (ground point under the pelvis, facing), the floor under it, and ducking
A = W(START[0], START[1], G, 0)
H = W(0.10, -3.62, G, 0)                    # at the hatch, the handle in reach
B = W(0.10, -4.80, G, 0)                    # stepped back, clear of the ramp's swing
R = W(0.00, -2.60, BAY, 0)                  # up the ramp, inside
F = W(-0.05, 0.78, CAB, 0)                  # the front of the bay, beside the driver's seat
SEAT = W(seat.x, seat.y, seat.z, 0)
wps[SEAT_WP].matrix_world = SEAT

LEN = 600
OPEN_AT, OPEN_END = 112, 176                 # the ramp swings down
SIT0, SIT1 = 426, 492                        # sitting down
CLOSE_AT, CLOSE_END = 500, 572               # the ramp swings shut
keys = [(0, A), (84, H), (118, H), (170, B), (188, B), (278, R), (420, F)]
duck = [(0, 0.0), (230, 0.0), (262, 0.14), (420, 0.12), (SIT0, 0.12), (SIT1, 0.0)]


LEG = [(B.to_translation(), 188), (R.to_translation(), None), (F.to_translation(), 420)]


def along(f):
    """Up the ramp and along the bay at an even pace (easing only at the two ends)."""
    a, b = LEG[0][0], LEG[2][0]
    mid = LEG[1][0]
    l1, l2 = (mid - a).length, (b - mid).length
    t = smooth((f - 188) / (420 - 188))
    t = 0.5 * t + 0.5 * ((f - 188) / (420 - 188))   # mostly even, a little ease
    d = t * (l1 + l2)
    p = a.lerp(mid, d / l1) if d <= l1 else mid.lerp(b, (d - l1) / l2)
    return Matrix.Translation(p)


def ground_at(f):
    """The root on the floor (ducked) while walking, then to the seat."""
    m = along(f) if 188 < f < 420 else track(keys, f)
    if f <= 420:
        loc, rot, _ = m.decompose()
        loc.z = surf(loc.y, f >= OPEN_END - 10) - track(duck, f)
        # walking: up over each planted foot (when the arms swing widest), and the weight over it
        a = track(SWING, f) if SWING else 0.0
        loc.z += 0.028 * min(1.0, abs(a) / 0.22) - 0.014 * (1.0 if abs(a) > 0.005 else 0.0)
        loc += rot @ Vector((-0.11 * a, 0.0, 0.0))
        return Matrix.Translation(loc) @ rot.to_matrix().to_4x4()
    t = smooth((f - 420) / (SIT1 - 420))
    stand = F @ Matrix.Translation((0, 0, -track(duck, f)))
    l0, r0, _ = stand.decompose()
    l1, r1, _ = SEAT.decompose()
    # slide sideways into the seat first, then down
    loc = Vector((l0.x + (l1.x - l0.x) * smooth(t * 1.4), l0.y + (l1.y - l0.y) * t, l0.z + (l1.z - l0.z) * smooth(t)))
    return Matrix.Translation(loc) @ r0.slerp(r1, t).to_matrix().to_4x4()


def path(f):
    return ground_at(f)


SWING = None
for side in "LR":
    plants[side][0][2] = foot_on(A, side)
walk(path, 0, 84, 4, "R", ramp_open=False)                   # up to the hatch
walk(path, 124, 170, 2, "L", ramp_open=False, arms=0.08)     # back off as it swings down
walk(path, 188, 420, 10, "R")                                # up the ramp and along the bay
for side in "LR":
    plants[side][-1][1] = LEN
SWING = sorted(swing + [(SIT0, 0.0), (LEN, 0.0)], key=lambda k: k[0])
feet = {s: ([tuple(p) for p in plants[s]], [(0, 1.0), (SIT0 + 20, 1.0), (SIT1 - 6, 0.0)]) for s in "LR"}

# the hands: the right pulls the handle; the left steadies on the roof edge ducking in, then on the seat back
ROOF_EDGE = Vector((-0.55, -2.75, 0.62))
SEAT_BACK = Vector((seat.x + 0.05, seat.y - 0.30, seat.z + 0.45))
hands = {"RH": ([(84, HANDLE + Vector((0, -0.10, 0))), (104, HANDLE), (118, HANDLE + Vector((0, -0.06, -0.02)))],
                [(84, 0.0), (102, 1.0), (118, 1.0), (134, 0.0)]),
         "LH": ([(236, ROOF_EDGE), (268, ROOF_EDGE + Vector((0, 0.25, 0))), (410, SEAT_BACK), (450, SEAT_BACK)],
                [(232, 0.0), (248, 0.9), (266, 0.9), (284, 0.0), (404, 0.0), (418, 1.0), (450, 1.0), (470, 0.0)])}

# the clip's camera: behind him and to his right outside; once he's up the ramp, up behind the LAV's right
# rear corner, then round to its right side as he sits (outside the hull, which hides him inside)
def lav_cam(f, chest):
    loc = ground_at(min(f, 420)).to_translation()
    chase_eye = Vector((loc.x + 1.4, loc.y - 2.9, G + 2.0))
    chase_at = chest + Vector((0, 0.6, 0.1))
    rear_eye, rear_at = Vector((2.9, -7.0, G + 3.2)), Vector((0.0, max(chest.y, -3.0), chest.z))
    side_eye, side_at = Vector((3.6, -1.4, G + 3.1)), Vector((seat.x, seat.y, seat.z + 0.6))
    a, b = smooth((f - 190) / 80), smooth((f - 400) / 130)
    eye = chase_eye.lerp(rear_eye, a).lerp(side_eye, b)
    at = chase_at.lerp(rear_at, a).lerp(side_at, b)
    return eye, at


ONLY_EXIT = globals().get("ONLY_EXIT", False)
if not ONLY_EXIT:
    enter = build(
        "enter", LEN, ground_at,
        [(0, 0.0), (SIT0, 0.0), (SIT1, 1.0), (LEN, 1.0)],
        feet, hands,
        [("door start", OPEN_AT), ("door end", OPEN_END), ("door close start", CLOSE_AT), ("door close end", CLOSE_END)],
        lean_keys=[(0, 0.04), (84, 0.08), (104, 0.18), (124, 0.05), (188, 0.04), (230, 0.12), (262, 0.32), (300, 0.22),
                   (420, 0.22), (450, 0.35), (SIT1, 0.0), (LEN, 0.0)],
        look_keys=([(0, HANDLE), (110, HANDLE), (150, Vector((0, -3.6, -0.6))), (200, Vector((0, -2.6, -0.3))),
                    (300, Vector((0, 0.5, 0.2))), (400, seat), (520, Vector((0, -3.0, 0.0))), (LEN, Vector((0, 3.0, 0.3)))],
                   [(0, 0.3), (60, 0.7), (130, 0.6), (200, 0.4), (300, 0.2), (400, 0.6), (440, 0.4), (SIT1, 0.0),
                    (520, 0.3), (560, 0.3), (LEN, 0.0)]),
        swing_keys=SWING, camera=lav_cam)

# ---- check: the head under the roof inside, the feet on what they stand on
body = next(o for o in kit if o.name.split(".")[0] == "body" and o.type == "MESH")
bv_verts = [body.matrix_world @ v.co for v in body.data.vertices]
worst = (9.0, 0)
bva.show_clip(C, root, arm, "enter")
for f in ([] if ONLY_EXIT else range(240, LEN + 1, 6)):
    scene.frame_set(f)
    hd = arm.matrix_world @ arm.pose.bones["Head"].head
    above = [v.z for v in bv_verts if abs(v.x - hd.x) < 0.15 and abs(v.y - hd.y) < 0.15 and v.z > hd.z - 0.05]
    if above:
        worst = min(worst, (round(min(above) - (hd.z + 0.14), 3), f))
arm.animation_data.action = None   # (so clearing the markers below doesn't clear the clip's triggers)
scene.timeline_markers.clear()
bva.show_clip(C, root, arm, "enter")
print("LAV Get In made, 0-%d; ground %.2f, seat %s; head's least room under the roof %s m (frame %s)"
      % (LEN, G, tuple(round(v, 2) for v in seat), worst[0], worst[1]))


# ================================================================ Get Out
# Stand up out of the seat and turn round (the ramp swinging down meanwhile), duck back along the bay, walk
# down the ramp and off it, the ramp swinging shut behind. Ends standing behind the LAV, facing away from it.
EXIT_WP = acts["exit"]["rfa_to_waypoint"]
X_LEN = 440
X_OPEN, X_OPEN_END = 60, 124
X_CLOSE, X_CLOSE_END = 350, 420
UP0, UP1 = 40, 110                                   # out of the seat, turned round, in the aisle
F2 = W(-0.05, 0.78, CAB, 180)
R2 = W(0.00, -2.60, BAY, 180)
B2 = W(0.20, -4.90, G, 180)                          # off the ramp, clear of its swing back up
wps[EXIT_WP].matrix_world = B2
x_duck = [(0, 0.0), (UP0, 0.0), (95, 0.2), (175, 0.2), (205, 0.13), (245, 0.13), (285, 0.0)]  # lower under the turret's ring by the cab
X_LEG = [F2.to_translation(), R2.to_translation(), B2.to_translation()]


def x_along(f):
    a, mid, b = X_LEG
    l1, l2 = (mid - a).length, (b - mid).length
    lin = (f - UP1) / (330 - UP1)
    t = 0.5 * smooth(lin) + 0.5 * max(0.0, min(1.0, lin))
    d = t * (l1 + l2)
    return a.lerp(mid, d / l1) if d <= l1 else mid.lerp(b, (d - l1) / l2)


X_SWING = None


def x_ground_at(f):
    """Out of the seat (up first, then across), turning round; then walking out on the floor and the ramp."""
    if f <= UP1:
        t = smooth((f - UP0) / (UP1 - UP0))
        l0, r0, _ = SEAT.decompose()
        l1, r1, _ = F2.decompose()
        l1 = l1 - Vector((0, 0, track(x_duck, f)))
        loc = Vector((l0.x + (l1.x - l0.x) * smooth((t - 0.25) / 0.75), l0.y + (l1.y - l0.y) * t,
                      l0.z + (l1.z - l0.z) * smooth(t * 1.5)))
        return Matrix.Translation(loc) @ r0.slerp(r1, smooth((t - 0.1) / 0.9)).to_matrix().to_4x4()
    loc = x_along(min(f, 330))
    rot = F2.to_quaternion()
    loc.z = surf(loc.y) - track(x_duck, f)
    if f < 330:
        a = track(X_SWING, f) if X_SWING else 0.0
        loc.z += 0.028 * min(1.0, abs(a) / 0.22) - 0.014 * (1.0 if abs(a) > 0.005 else 0.0)
        loc += rot @ Vector((-0.11 * a, 0.0, 0.0))
    return Matrix.Translation(loc) @ rot.to_matrix().to_4x4()


plants = {"L": [[0, 10 ** 6, None]], "R": [[0, 10 ** 6, None]]}
swing = [(0, 0.0)]
for side in "LR":
    plants[side][0][2] = foot_on(F2, side)
walk(x_ground_at, UP1, 330, 10, "R")                     # back along the bay and down the ramp
for side in "LR":
    plants[side][-1][1] = X_LEN
X_SWING = sorted(swing + [(X_LEN, 0.0)], key=lambda k: k[0])
x_feet = {s: ([tuple(p) for p in plants[s]], [(0, 0.0), (80, 0.0), (105, 1.0), (X_LEN, 1.0)]) for s in "LR"}
X_ROOF_EDGE = Vector((-0.55, -2.85, 0.62))
x_hands = {"RH": ([(40, SEAT_BACK), (90, SEAT_BACK)], [(36, 0.0), (52, 1.0), (84, 1.0), (100, 0.0)]),
           "LH": ([(222, X_ROOF_EDGE + Vector((0, 0.25, 0))), (262, X_ROOF_EDGE)],
                  [(218, 0.0), (234, 0.9), (256, 0.9), (276, 0.0)])}


def lav_exit_cam(f, chest):
    side_eye, side_at = Vector((3.6, -1.4, G + 3.1)), Vector((seat.x, seat.y, seat.z + 0.6))
    rear_eye = Vector((2.9, -7.4, G + 2.9))
    rear_at = Vector((0.0, min(chest.y, 0.5), chest.z))
    out_eye = Vector((2.4, -8.2, G + 2.0))
    a, b = smooth((f - 80) / 100), smooth((f - 300) / 100)
    eye = side_eye.lerp(rear_eye, a).lerp(out_eye, b)
    at = side_at.lerp(rear_at, a).lerp(chest + Vector((0, -0.4, 0.1)), b)
    return eye, at


exit_ = build(
    "exit", X_LEN, x_ground_at,
    [(0, 1.0), (UP0, 1.0), (100, 0.0), (X_LEN, 0.0)],
    x_feet, x_hands,
    [("door start", X_OPEN), ("door end", X_OPEN_END), ("door close start", X_CLOSE), ("door close end", X_CLOSE_END)],
    lean_keys=[(0, 0.0), (UP0, 0.12), (70, 0.38), (110, 0.32), (175, 0.32), (240, 0.3), (285, 0.06), (330, 0.04), (X_LEN, 0.0)],
    look_keys=([(0, Vector((0, -3.0, -0.2))), (100, Vector((0, -3.0, -0.4))), (240, Vector((0, -4.6, -1.4))),
                (330, Vector((0.3, -8.0, -0.6))), (360, Vector((0, -3.2, -0.2))), (X_LEN, Vector((0.3, -9.0, 0.0)))],
               [(0, 0.5), (40, 0.6), (120, 0.3), (240, 0.4), (330, 0.2), (350, 0.6), (420, 0.5), (X_LEN, 0.0)]),
    swing_keys=X_SWING, camera=lav_exit_cam)

worst = (9.0, 0)
bva.show_clip(C, root, arm, "exit")
for f in range(0, 300, 6):
    scene.frame_set(f)
    hd = arm.matrix_world @ arm.pose.bones["Head"].head
    above = [v.z for v in bv_verts if abs(v.x - hd.x) < 0.15 and abs(v.y - hd.y) < 0.15 and v.z > hd.z - 0.05]
    if above:
        worst = min(worst, (round(min(above) - (hd.z + 0.14), 3), f))
arm.animation_data.action = None
scene.timeline_markers.clear()
bva.show_clip(C, root, arm, "exit")
print("LAV Get Out made, 0-%d; head's least room under the roof %s m (frame %s)" % (X_LEN, worst[0], worst[1]))
