# Saves side-view pictures of a clip's frames (viewport render) to the Desktop, to look at.
import bpy, math
from mathutils import Euler
def shots(clip, frames, tag, dist=3.4):
    bva.show_clip(C, root, arm, clip)
    V = [a for a in C.screen.areas if a.type == "VIEW_3D"][0]
    r3 = V.spaces[0].region_3d
    r3.view_perspective = "ORTHO"
    r3.view_rotation = Euler((math.radians(90), 0, math.radians(90))).to_quaternion()
    r3.view_distance = dist
    keep = C.scene.render.filepath
    for f in frames:
        C.scene.frame_set(f)
        r3.view_location = arm.matrix_world @ arm.pose.bones["Pelvis"].head
        with C.temp_override(area=V, region=[r for r in V.regions if r.type == "WINDOW"][0]):
            C.scene.render.filepath = r"C:\Users\Venom's Desktop\OneDrive\Desktop\rfa_shots\%s_%03d.png" % (tag, f)
            bpy.ops.render.opengl(write_still=True)
    C.scene.render.filepath = keep
    r3.view_perspective = "PERSP"
