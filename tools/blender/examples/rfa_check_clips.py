# Checks a vehicle's clips: twist bones against their arms (degrees off their rest relationship), limbs' lengths,
# and bones keyed that the game's own clips don't animate.
import math
def _tw(t, a):
    pa, pt = arm.pose.bones[a], arm.pose.bones[t]
    rest = (pa.bone.matrix_local.inverted() @ pt.bone.matrix_local).to_quaternion()
    now = (pa.matrix.inverted() @ pt.matrix).to_quaternion()
    return math.degrees(rest.rotation_difference(now).angle)
_P = [("L-Thigh", "L-Calf"), ("L-Calf", "L-Foot"), ("R-Thigh", "R-Calf"), ("R-Calf", "R-Foot"),
      ("l-upperarm", "l-forearm"), ("l-forearm", "L-Hand"), ("r-upperarm", "r-forearm"), ("r-forearm", "R-Hand")]
def _rl(a, b):
    return (arm.data.bones[a].head_local - arm.data.bones[b].head_local).length
_acts = bva.clip_actions(VEHICLE)
for _c in ("enter", "drive", "exit"):
    bva.show_clip(C, root, arm, _c)
    _tws, _st = 0.0, 0.0
    for _f in range(0, int(_acts[_c].frame_range[1]) + 1, 5):
        scene.frame_set(_f)
        _tws = max(_tws, _tw("L-upperarm-twst1", "l-upperarm"), _tw("r-upperarm-twst1", "r-upperarm"))
        _st = max([_st] + [abs((arm.pose.bones[a].head - arm.pose.bones[b].head).length / _rl(a, b) - 1) for a, b in _P])
    _extra = sorted({fc.data_path.split('"')[1] for fc in _acts[_c].fcurves if fc.data_path.startswith("pose")} - GAME_BONES)
    print(_c, "twist off its arm, most:", round(_tws, 1), "deg; stretch", round(_st, 3), "; extra bones keyed:", len(_extra), _extra[:6])
