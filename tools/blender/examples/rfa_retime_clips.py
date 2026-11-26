# Makes vehicle clips quicker: each clip cut where its motion ends (the idle tail dropped), then played faster.
# Keys (bones and the root), triggers and the clip camera all scale together. Run once per clip (it notes itself).
import json, sys
import bpy
bva = sys.modules["rfa_animation_tools.blender_vehicle_anim"]
C = bpy.context

# vehicle: {clip: (cut at frame, speed factor)}
PLAN = {"RF_LAV_1": {"enter": (510, 0.6), "exit": (345, 0.65)},
        "Civ_Emergency_1": {"enter": (192, 0.7), "exit": (150, 0.7)}}
SCENES = {"RF_LAV_1": "LAV", "Civ_Emergency_1": "Ambulance"}


def retime(action, cut, s):
    if action.get("rfa_retimed"):
        return "already quicker (x%.2f)" % action["rfa_retimed"]
    # a key at the cut on every curve, then everything after it gone
    for fc in action.fcurves:
        if any(k.co[0] > cut for k in fc.keyframe_points):
            fc.keyframe_points.insert(cut, fc.evaluate(cut))
            for i in reversed([i for i, k in enumerate(fc.keyframe_points) if k.co[0] > cut + 1e-3]):
                fc.keyframe_points.remove(fc.keyframe_points[i])
    for fc in action.fcurves:
        for k in fc.keyframe_points:
            k.co[0] *= s
            k.handle_left[0] *= s
            k.handle_right[0] *= s
        fc.update()
    length = int(round(cut * s))
    trig = []
    for name, f in json.loads(action.get("rfa_triggers", "[]")):
        if name.startswith("camera") and name.endswith("end"):
            trig.append([name, length - 2])
        elif f <= cut:
            trig.append([name, int(round(f * s))])
    action["rfa_triggers"] = json.dumps(sorted(trig, key=lambda t: t[1]))
    action["rfa_retimed"] = s
    return "%d -> %d frames (%.1f s)" % (cut, length, length / 60.0)


for vehicle, clips in PLAN.items():
    scene = bpy.data.scenes.get(SCENES[vehicle])
    if scene is None:
        print(vehicle, ": no scene", SCENES[vehicle]); continue
    C.window.scene = scene
    root = bva.find_root(C, vehicle)
    arm = bva.person(C)
    arm.animation_data.action = None
    acts = bva.clip_actions(vehicle)
    for clip, (cut, s) in clips.items():
        print(vehicle, clip, retime(acts[clip], cut, s), json.loads(acts[clip]["rfa_triggers"]))
    scene.timeline_markers.clear()
    bva.show_clip(C, root, arm, "enter")
    print(vehicle, "export:", bpy.ops.rfa.people_export())
