"""The player's animations for a vehicle: getting in, sitting at the wheel, getting out, made in
Blender and put into the game in one go.

How the game plays them (vehicle_interactions.xtbl, the anim set tables):
  - a vehicle's vehicles.xtbl entry names an interaction (Vehicle_Interaction): where people stand and
    sit (waypoints: a place in the vehicle's space, or a part's, and a heading, the way the person
    faces; an Enter Node, an Exit Node, the seat with Seat driver) and the steps between them
    (Waypoint_transitions: from one waypoint to the next, playing an action and ending in a state,
    opening the doors named), and the anim set the steps are played from (Anim_Set);
  - the anim set is a table of its own (<set>.xtbl, a Skeleton_Set: each state's and action's
    animation, "name.animx"), which the game reads while it starts when anim_set_filenames.xtbl lists
    it; anim_files.xtbl gives each animation its triggers (when a door opens and is open, when the
    camera hands over, the footsteps; frames at 30 a second) and flags;
  - getting in is the action "vehicle enter primary 1" from the Enter Node to the seat, ending in the
    state the seat is driven in ("driving forward"); getting out the action "vehicle exit driver" from
    the seat to the Exit Node, ending in "stand". The animation starts where the person stands at its
    first waypoint, facing its heading: its root track (the armature object's keys) is in that place's
    frame, and its movement over the clip should end at the next waypoint.
Most of the game's vehicles use the "Dummy" interaction (no animations: the person pops in and out);
the LAV has a set of animations made for it that nothing plays (pc_enter_lav, pc_drive_lav,
pc_exit_lav), used here as where a clip starts when the vehicle has none of its own.

In Blender: the vehicle's kit (as New Vehicle makes it), its waypoints as empties (the green Y arrow
the way the person faces), the player character, and one action per clip, each with its triggers as
timeline markers. Export writes the animations (mods\\animations), the vehicle's own anim set
(overrides\\build\\pc\\cache\\rfa_<vehicle>.xtbl, a new file the game finds by name), and its entries in
anim_set_filenames.xtbl, anim_files.xtbl, vehicle_interactions.xtbl (an interaction of its own,
RFA_<vehicle>, from the vehicle's with the waypoints moved as placed) and vehicles.xtbl (the vehicle
naming it). Those four tables are read once while the game starts, from its own copy of its tables:
RFA-Mod hands it ours from mods\\tables\\ (rfa_qol's tables part), or, when the Guerrilla world's tools
have written that table (mods\\guerrilla_world\\), ours goes into that one, which guerrilla_world hands
over. Our entries are recorded in mods\\animations\\anim_entries.txt, so the Guerrilla tools keep them
when they rebuild their lists.
"""

import json
import math
import os
import re
import xml.etree.ElementTree as ET

import bpy
from mathutils import Matrix, Vector

from . import blender_io as bio
from . import blender_vehicle as bv
from . import rfa_formats as rf

CLIPS = ("enter", "drive", "exit")
CLIP_LABELS = {"enter": "Get In", "drive": "Drive", "exit": "Get Out"}
ENTER_ACTION, EXIT_ACTION = "vehicle enter primary 1", "vehicle exit driver"
SEATED_STATE, STANDING_STATE = "driving forward", "stand"
DEFAULT_SOURCES = {"enter": "pc_enter_lav", "drive": "pc_drive_lav", "exit": "pc_exit_lav"}
MOVING_FLAGS = ("Block_Movement", "Anim_Movement", "No_Spinebending")  # getting in and out (the ATV's and the LAV's)
TRIGGERS = ("door start", "door end", "door close start", "door close end",
            "door 2 start", "door 2 end", "door 2 close start", "door 2 close end",
            "camera lookat start", "camera pos start", "camera lookat end", "camera pos end",
            "left-foot", "right-foot", "release movement", "interrupt")
PLAYER = "RF4_PC.str2_pc"
DOOR_TYPE = 0x0196EBB2  # the part type of doors and hatches  # the player character, in humans.vpp_pc

TABLES = r"build\pc\cache\table.vpp_pc"
OWN_RECORD = "anim_entries.txt"  # in mods\animations: the Guerrilla tools' record format ("set x.xtbl", "file x.animx")


# ---------------------------------------------------------------- tables


def _gw_path(mod_folder, file_name):
    return os.path.join(mod_folder, "mods", "guerrilla_world", file_name)


def _ours_path(mod_folder, file_name):
    return os.path.join(mod_folder, "mods", "tables", file_name)


def read_table(game_folder, mod_folder, file_name):
    """A start-up table's text as the game will read it with RFA-Mod: the Guerrilla world's copy, ours, or the game's."""
    for path in (_gw_path(mod_folder, file_name), _ours_path(mod_folder, file_name)):
        if os.path.isfile(path):
            with open(path, "rb") as f:
                return f.read().decode("latin-1")
    pf = bio.GameFiles.archive(game_folder, TABLES)
    if pf is None or not pf.has(file_name):
        raise ValueError(f"the game has no {file_name} (check the game folder in the add-on's preferences)")
    return pf.read(file_name).decode("latin-1")


def write_table(mod_folder, file_name, text):
    """Where the game gets it from: into the Guerrilla world's copy if there is one (and ours removed),
    else ours (mods\\tables). Returns the path written."""
    gw, ours = _gw_path(mod_folder, file_name), _ours_path(mod_folder, file_name)
    path = gw if os.path.isfile(gw) else ours
    bv._write(path, text.encode("latin-1", "replace"))
    if path == gw and os.path.isfile(ours):
        os.remove(ours)
    return path


def game_table(game_folder, file_name):
    """A table of the game's own (an anim set's), or None."""
    pf = bio.GameFiles.archive(game_folder, TABLES)
    return pf.read(file_name).decode("latin-1") if pf is not None and pf.has(file_name) else None


def _entry(text, tag, name):
    """(start, end) of the <tag> entry whose <Name> is name."""
    m = re.search(r"<" + tag + r">\s*<Name>\s*" + re.escape(name) + r"\s*</Name>", text, re.IGNORECASE)
    if not m:
        return None
    end = text.find("</" + tag + ">", m.end())
    return (m.start(), end + len(tag) + 3) if end >= 0 else None


def _insert_before(text, closing, entry):
    at = text.rfind(closing)
    if at < 0:
        raise ValueError(f"the table has no {closing}")
    line = text.rfind("\n", 0, at) + 1
    newline = "\r\n" if "\r\n" in text else "\n"
    if text[line:at].strip():  # </closing> isn't on a line of its own
        return text[:at] + entry + newline + text[at:]
    return text[:line] + "\t" + entry + newline + text[line:]


def _put_entry(text, tag, name, entry, closing="</Table>"):
    span = _entry(text, tag, name)
    if span:
        return text[:span[0]] + entry + text[span[1]:]
    return _insert_before(text, closing, entry)


def _drop_entry(text, tag, name):
    span = _entry(text, tag, name)
    if span is None:
        return text
    start, end = span
    while end < len(text) and text[end] in "\r\n":
        end += 1
    while start > 0 and text[start - 1] in " \t":
        start -= 1
    return text[:start] + text[end:]


def vehicle_entry(text, vehicle):
    span = _entry(text, "Vehicle", vehicle)
    if span is None:
        raise ValueError(f"vehicles.xtbl has no vehicle named {vehicle}")
    return text[span[0]:span[1]]


def vehicle_names(game_folder, mod_folder):
    text = read_table(game_folder, mod_folder, "vehicles.xtbl")
    return [m.group(1).strip() for m in re.finditer(r"<Vehicle>\s*<Name>([^<]+)</Name>", text)]


def vehicle_container(entry):
    m = re.search(r"<Mesh_Name>\s*<Filename>\s*([^<]+?)\s*</Filename>", entry)
    if not m:
        raise ValueError("the vehicle's entry names no model (Mesh_Name)")
    return os.path.splitext(m.group(1))[0].lower() + ".ccar_pc.str2_pc"


def vehicle_interaction(entry):
    m = re.search(r"<Vehicle_Interaction>\s*([^<]*?)\s*</Vehicle_Interaction>", entry)
    return m.group(1) if m else ""


def _anim_file_entry(files_text, animx):
    m = re.search(r"<Filename>\s*" + re.escape(animx) + r"\s*</Filename>", files_text, re.IGNORECASE)
    if not m:
        return None
    s = files_text.rfind("<Anim_file>", 0, m.start())
    e = files_text.find("</Anim_file>", m.end())
    return (s, e + len("</Anim_file>")) if s >= 0 and e >= 0 else None


def source_triggers(files_text, stem):
    """The triggers anim_files.xtbl gives the game's animation (name, frame at 60 a second)."""
    span = _anim_file_entry(files_text, stem + ".animx")
    if span is None:
        return []
    entry = files_text[span[0]:span[1]]
    return [(n.strip(), int(f) * 2) for n, f in
            re.findall(r"<Trigger>\s*<Name>([^<]*)</Name>\s*<Frame>\s*(-?\d+)\s*</Frame>", entry)]


# ---------------------------------------------------------------- the interaction


class Waypoint:
    __slots__ = ("name", "pos", "heading", "flags", "seat", "component")

    def __init__(self, el):
        self.name = (el.findtext("Name") or "").strip()
        p = el.find("Pos")
        self.pos = tuple(float((p.findtext(k) or "0").strip() or 0) for k in "XYZ") if p is not None else (0.0, 0.0, 0.0)
        self.heading = float((el.findtext("Heading") or "0").strip() or 0)
        self.flags = [(f.text or "").strip() for f in el.findall("Node_Type/Flag")]
        self.seat = (el.findtext("Seat") or "").strip()
        self.component = (el.findtext("Parent_Component") or "").strip()


def parse_interaction(text, name):
    span = _entry(text, "Vehicle", name)
    if span is None:
        raise ValueError(f"vehicle_interactions.xtbl has no interaction named {name}")
    return ET.fromstring(text[span[0]:span[1]])


def roles(el):
    """{clip: (from waypoint, to waypoint)} of the interaction: getting in (an Enter Node to the
    driver's seat), driving (the seat), getting out (the seat to an Exit Node)."""
    wps = {w.name: w for w in (Waypoint(x) for x in el.findall("Waypoints/Waypoint"))}
    trans = [((t.findtext("Name") or "").strip(), (t.findtext("Destination") or "").strip())
             for t in el.findall("Waypoint_transitions/Waypoint_transition")]
    seat = next((w.name for w in wps.values() if w.seat.lower() == "driver"), None) or \
        next((w.name for w in wps.values() if w.seat), None)
    if seat is None:
        return wps, {}
    out = {"drive": (seat, seat)}
    enter = next((a for a, b in trans if b == seat and a in wps and "Enter Node" in wps[a].flags), None) or \
        next((w.name for w in wps.values() if "Enter Node" in w.flags), None)
    if enter:
        out["enter"] = (enter, seat)
    leave = next((b for a, b in trans if a == seat and b in wps and "Exit Node" in wps[b].flags), None) or \
        next((w.name for w in wps.values() if "Exit Node" in w.flags), None)
    if leave:
        out["exit"] = (seat, leave)
    return wps, out


def set_animations(game_folder, set_name):
    """{(kind, id): stem} of an anim set's Default group (kind "State" or "Action")."""
    text = game_table(game_folder, set_name.lower() + ".xtbl") if set_name else None
    if not text:
        return {}
    out = {}
    for kind in ("State", "Action"):
        for m in re.finditer(r"<" + kind + r">\s*<ID>([^<]*)</ID>\s*<Animation>\s*<Filename>([^<]*)</Filename>", text):
            out.setdefault((kind, m.group(1).strip().lower()), os.path.splitext(m.group(2).strip())[0])
    return out


def _transition_steps(el, a, b):
    for t in el.findall("Waypoint_transitions/Waypoint_transition"):
        if (t.findtext("Name") or "").strip() == a and (t.findtext("Destination") or "").strip() == b:
            i = t.find("Interactions/Interaction")
            if i is not None:
                return (i.findtext("Anim_Action") or "").strip(), (i.findtext("Anim_State") or "").strip()
            return "", ""
    return "", ""


# ---------------------------------------------------------------- in Blender


def _heading_matrix(pos, heading):
    return Matrix.Translation(bio.to_blender(pos)) @ Matrix.Rotation(-math.radians(heading), 4, "Z")


def _local(obj):
    parent = obj.parent
    return (parent.matrix_world.inverted() @ obj.matrix_world) if parent is not None else obj.matrix_world.copy()


def waypoint_of(obj):
    """(position in the game's axes, heading in degrees) of a waypoint empty, in its parent's frame."""
    m = _local(obj)
    loc = m.to_translation()
    forward = m.to_3x3() @ Vector((0, 1, 0))
    heading = math.degrees(math.atan2(forward.x, forward.y))
    return bio.from_blender(loc), heading


def _part_object(root, component):
    if component:
        low = component.lower()
        for o in bv._kit_objects(root):
            if "rfa_part" in o and o.name.split(".")[0].lower() == low:
                return o
    return root


def find_root(context, vehicle):
    for o in context.scene.objects:
        if o.get("rfa_people_vehicle", "").lower() == vehicle.lower():
            return o
    return None


def waypoints(root):
    return {o["rfa_waypoint"]: o for o in bv._kit_objects(root, people=True) if "rfa_waypoint" in o}


def person(context):
    return next((o for o in context.scene.objects if o.get("rfa_vehicle_person") and o.type == "ARMATURE"), None)


def clip_actions(vehicle):
    return {a["rfa_clip"]: a for a in bpy.data.actions
            if a.get("rfa_vehicle", "").lower() == vehicle.lower() and a.get("rfa_clip") in CLIPS}


def anim_name(vehicle, clip):
    return f"rfa_{vehicle[:40]}_{clip}".lower()


def set_name(vehicle):
    return f"rfa_{vehicle}".lower()


def interaction_name(vehicle):
    return f"RFA_{vehicle}"


def _save_markers(scene, action):
    if action is None or "rfa_clip" not in action:
        return
    action["rfa_triggers"] = json.dumps(sorted([m.name, m.frame] for m in scene.timeline_markers))


def _load_markers(scene, action):
    scene.timeline_markers.clear()
    for name, frame in json.loads(action.get("rfa_triggers", "[]")):
        scene.timeline_markers.new(name, frame=int(frame))


def show_clip(context, root, arm, clip):
    """Puts the clip's action on the person, at the waypoint it starts from, with its triggers as markers."""
    vehicle = root["rfa_people_vehicle"]
    actions = clip_actions(vehicle)
    action = actions.get(clip)
    if action is None:
        raise ValueError(f"{vehicle} has no {CLIP_LABELS[clip]} clip")
    scene = context.scene
    current = arm.animation_data.action if arm.animation_data else None
    _save_markers(scene, current)
    start = waypoints(root).get(action.get("rfa_from_waypoint", ""))
    arm.animation_data_create()
    arm.animation_data.action = None
    arm.parent = start or root
    arm.matrix_parent_inverse = Matrix()
    arm.rotation_mode = "QUATERNION"
    arm.location = (0, 0, 0)
    arm.rotation_quaternion = (1, 0, 0, 0)
    # Every bone back to rest first: a channel the clip doesn't key would otherwise keep whatever an
    # earlier clip or script left on it (a stray scale there stretched the arms in game).
    for pb in arm.pose.bones:
        pb.location = (0, 0, 0)
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.rotation_euler = (0, 0, 0)
        pb.scale = (1, 1, 1)
    arm.animation_data.action = action
    _load_markers(scene, action)
    first, last = (int(round(v)) for v in action.frame_range)
    scene.frame_start = 0
    scene.frame_end = max(1, int(action.get("rfa_frames", last)), last)
    scene.frame_set(0)
    root["rfa_people_clip"] = clip


def setup(context, game_folder, mod_folder, vehicle, report):
    """The kit, the waypoints, the player and the three clips for a vehicle; returns the kit's root."""
    vtext = read_table(game_folder, mod_folder, "vehicles.xtbl")
    ventry = vehicle_entry(vtext, vehicle)
    container = vehicle_container(ventry)
    itext = read_table(game_folder, mod_folder, "vehicle_interactions.xtbl")
    current = vehicle_interaction(ventry)
    ours = interaction_name(vehicle)
    # An earlier export's interaction (ours) is where the waypoints were left; else the vehicle's own.
    base_name = ours if _entry(itext, "Vehicle", ours) else (current if _entry(itext, "Vehicle", current) else "Dummy")
    el = parse_interaction(itext, base_name)
    wps, clip_roles = roles(el)
    if "enter" not in clip_roles:
        raise ValueError(f"{vehicle}'s interaction ({base_name}) has no way in to a seat to animate")
    original = recorded_original(mod_folder, vehicle) or (current if current.lower() != ours.lower() else "Dummy")
    the_set = (el.findtext("Anim_Set") or "").strip()
    if base_name == ours:
        the_set = ""  # our own set's clips are in the .blend (or mods\animations)

    root = find_root(context, vehicle)
    if root is None:
        root = next((o for o in context.scene.objects if o.get("rfa_vehicle", "").lower() == container.lower()
                     and o.get("rfa_vehicle_name", "").lower() == vehicle.lower() and o.parent is None), None)
        if root is None:
            if not bio.GameFiles.archive(game_folder, bv.VEHICLES) or \
                    container.lower() not in {n.lower() for n in bio.GameFiles.archive(game_folder, bv.VEHICLES).names()}:
                raise ValueError(f"the game has no {container} (the model {vehicle} names)")
            container = next(n for n in bio.GameFiles.archive(game_folder, bv.VEHICLES).names() if n.lower() == container.lower())
            root, _ = bv.make_kit(context, game_folder, container, vehicle)
        root["rfa_people_vehicle"] = vehicle
        root["rfa_people_original"] = original

    # The waypoints, as empties.
    coll = root.users_collection[0] if root.users_collection else context.scene.collection
    have = waypoints(root)
    for w in wps.values():
        obj = have.get(w.name)
        if obj is not None:
            continue  # placed already (maybe moved since): left where it is
        obj = bpy.data.objects.new(w.name, None)
        obj.empty_display_type = "ARROWS"
        obj.empty_display_size = 0.4
        obj.show_name = True
        obj.show_in_front = True
        coll.objects.link(obj)
        obj["rfa_waypoint"] = w.name
        obj.parent = _part_object(root, w.component)
        obj.matrix_parent_inverse = Matrix()
        obj.matrix_basis = _heading_matrix(w.pos, w.heading)

    # The person.
    arm = person(context)
    if arm is None:
        arm = import_player(context, game_folder, report)
    arm["rfa_vehicle_person"] = True

    # The clips: the vehicle's own animations where it has them, else the LAV's.
    own = set_animations(game_folder, the_set)
    files_text = read_table(game_folder, mod_folder, "anim_files.xtbl")
    have_clips = clip_actions(vehicle)
    scene = context.scene
    if arm.animation_data and arm.animation_data.action is not None:
        _save_markers(scene, arm.animation_data.action)  # the clip shown keeps its triggers
        arm.animation_data.action = None
    for clip in CLIPS:
        if clip not in clip_roles or clip in have_clips:
            continue
        a, b = clip_roles[clip]
        if clip == "drive":
            _, state = _transition_steps(el, *clip_roles["enter"])
            stem = own.get(("State", (state or SEATED_STATE).lower()))
        else:
            act, _ = _transition_steps(el, a, b)
            stem = own.get(("Action", (act or (ENTER_ACTION if clip == "enter" else EXIT_ACTION)).lower()))
        found = _game_animation(game_folder, stem) if stem else None
        if found is None:
            stem = DEFAULT_SOURCES[clip]
            found = _game_animation(game_folder, stem)
        if found is None:
            report({"WARNING"}, f"the game has no {stem}: {CLIP_LABELS[clip]} left out")
            continue
        anim = rf.Animation.read(bio.Animations.read(game_folder, *found))
        arm.animation_data_create()
        arm.animation_data.action = None
        arm.location = (0, 0, 0)
        arm.rotation_quaternion = (1, 0, 0, 0)
        action = bio.apply_animation(context, arm, anim, f"{vehicle} {CLIP_LABELS[clip]}", report)
        action.name = f"{vehicle} {CLIP_LABELS[clip]}"
        action["rfa_name"] = anim_name(vehicle, clip)
        action["rfa_vehicle"] = vehicle
        action["rfa_clip"] = clip
        action["rfa_source"] = stem
        action["rfa_from_waypoint"] = a
        action["rfa_to_waypoint"] = b
        action["rfa_triggers"] = json.dumps(source_triggers(files_text, stem))
        arm.animation_data.action = None  # (show_clip below would take the scene's markers for it)
    show_clip(context, root, arm, "enter")
    return root


def _game_animation(game_folder, stem):
    """(archive, container, file name) of the game's animation of that name, or None."""
    low = stem.lower() + ".anim_pc"
    for rel, container, name in bio.Animations.all(game_folder):
        if name.lower() == low:
            return rel, container, name
    return None


def import_player(context, game_folder, report):
    """The player character (its skeleton and skinned meshes), as Character from the Game brings it in."""
    pf, exact = None, None
    for rel in bio.GameFiles.HUMANS:
        a = bio.GameFiles.archive(game_folder, rel)
        exact = next((n for n in a.names() if n.lower() == PLAYER.lower()), None) if a is not None else None
        if exact:
            pf = a
            break
    if pf is None:
        raise ValueError(f"the game has no {PLAYER} (the player character) in humans.vpp_pc")
    container = pf.container(exact)
    meshes = [n for n in container.names() if n.lower().endswith(".ccmesh_pc")]
    arm, rig = None, None
    for candidate in [os.path.splitext(m)[0] + ".rig_pc" for m in meshes] + [os.path.splitext(PLAYER)[0] + ".rig_pc"]:
        _, rpf = bio.GameFiles.find(game_folder, bio.GameFiles.RIGS, candidate)
        if rpf is not None:
            rig = rf.Rig.read(rpf.read(candidate))
            arm = bio.make_armature(context, rig, os.path.splitext(candidate)[0])
            break
    if arm is None:
        raise ValueError(f"no skeleton named as {PLAYER}'s meshes")
    textures = bio.Textures(game_folder, container)
    for mesh_name in meshes:
        data_name = rf.mesh_data_name(mesh_name)
        if container.has(data_name):
            try:
                mesh = rf.CharacterMesh.read(container.read(mesh_name), container.read(data_name), rig)
                bio.make_mesh(context, mesh, os.path.splitext(mesh_name)[0], arm, textures)
            except Exception as ex:
                report({"WARNING"}, f"{mesh_name}: {ex}")
    return arm


def snap_end(context, root, arm):
    """Keys the root so the clip's last frame is at the waypoint it goes to."""
    action = arm.animation_data.action if arm.animation_data else None
    if action is None or "rfa_clip" not in action:
        raise ValueError("show one of the vehicle's clips first")
    wps = waypoints(root)
    a, b = wps.get(action.get("rfa_from_waypoint")), wps.get(action.get("rfa_to_waypoint"))
    if a is None or b is None or a == b:
        raise ValueError("this clip stays where it starts (only getting in and out move)")
    end = context.scene.frame_end
    local = a.matrix_world.inverted() @ b.matrix_world
    loc, rot, _ = local.decompose()
    context.scene.frame_set(end)
    arm.location = loc
    arm.rotation_mode = "QUATERNION"
    arm.rotation_quaternion = rot
    arm.keyframe_insert("location", frame=end)
    arm.keyframe_insert("rotation_quaternion", frame=end)
    # The first frame stays where the clip starts.
    first = context.scene.frame_start
    has_first = any(c.data_path == "location" and any(abs(k.co[0] - first) < 0.5 for k in c.keyframe_points)
                    for c in bio._fcurves(arm, action, create=False))
    if not has_first:
        context.scene.frame_set(first)
        arm.location = (0, 0, 0)
        arm.rotation_quaternion = (1, 0, 0, 0)
        arm.keyframe_insert("location", frame=first)
        arm.keyframe_insert("rotation_quaternion", frame=first)
    context.scene.frame_set(end)


def seat_at_end(context, root, arm):
    """Moves the waypoint the clip goes to where the clip's last frame leaves the person."""
    action = arm.animation_data.action if arm.animation_data else None
    if action is None or "rfa_clip" not in action:
        raise ValueError("show one of the vehicle's clips first")
    wps = waypoints(root)
    b = wps.get(action.get("rfa_to_waypoint"))
    if b is None or action.get("rfa_from_waypoint") == action.get("rfa_to_waypoint"):
        raise ValueError("this clip stays where it starts (only getting in and out move)")
    scene = context.scene
    keep = scene.frame_current
    scene.frame_set(scene.frame_end)
    world = arm.matrix_world.copy()
    scene.frame_set(keep)
    loc, rot, _ = world.decompose()
    # Upright: only the turn about the vertical is a heading.
    forward = rot.to_matrix() @ Vector((0, 1, 0))
    heading = math.atan2(forward.x, forward.y)
    m = Matrix.Translation(loc) @ Matrix.Rotation(-heading, 4, "Z")
    b.matrix_world = m
    return b.name


# ---------------------------------------------------------------- into the game


class _Settings:
    use_action_range = True
    bones = "ANIMATED"
    every_frame = False
    rotation_tolerance = 0.1
    position_tolerance = 0.0005


def _fmt(v):
    s = f"{v:.6f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def _interaction_entry(base_text, vehicle, root, the_set, steps):
    """Our interaction: the one it was made from, renamed, the waypoints where they are now, the steps
    playing our clips, and our set."""
    el = ET.fromstring(base_text)
    el.find("Name").text = interaction_name(vehicle)
    have = waypoints(root)
    for w in el.findall("Waypoints/Waypoint"):
        obj = have.get((w.findtext("Name") or "").strip())
        if obj is None:
            continue
        pos, heading = waypoint_of(obj)
        p = w.find("Pos")
        if p is None:
            p = ET.SubElement(w, "Pos")
        for k, v in zip("XYZ", pos):
            e = p.find(k)
            if e is None:
                e = ET.SubElement(p, k)
            e.text = _fmt(v)
        h = w.find("Heading")
        if h is None:
            h = ET.SubElement(w, "Heading")
        h.text = _fmt(round(heading, 3))
    for (a, b), (action, state, door) in steps.items():
        for t in el.findall("Waypoint_transitions/Waypoint_transition"):
            if (t.findtext("Name") or "").strip() != a or (t.findtext("Destination") or "").strip() != b:
                continue
            inter = t.find("Interactions")
            if inter is None:
                inter = ET.Element("Interactions")
                t.insert(2, inter)
            for old in list(inter):
                inter.remove(old)
            i = ET.SubElement(inter, "Interaction")
            ET.SubElement(i, "Action").text = "Transition"
            ET.SubElement(i, "Anim_Action").text = action
            ET.SubElement(i, "Anim_State").text = state
            if door:  # the door the clip's door triggers open (the copied entry may name another vehicle's)
                d1 = t.find("Door_1")
                if d1 is None:
                    d1 = ET.SubElement(t, "Door_1")
                if (d1.text or "").strip().lower() != door.lower():
                    for snd in ("Door_open", "Door_close"):  # that other door's sounds
                        for e in t.findall(snd):
                            t.remove(e)
                d1.text = door
    s = el.find("Anim_Set")
    if s is None:
        s = ET.SubElement(el, "Anim_Set")
    s.text = the_set
    return ET.tostring(el, encoding="unicode")


def _set_table(game_folder, base_set, name, states, actions):
    """Our anim set's table: the one the vehicle played (its states and actions kept), ours put in."""
    text = (game_table(game_folder, base_set.lower() + ".xtbl") if base_set else None) or \
        game_table(game_folder, "default_vehicle.xtbl")
    if text is None:
        raise ValueError("the game has no default_vehicle.xtbl")
    text = re.sub(r"(<Skeleton_Set>\s*<Name>)[^<]*(</Name>)", r"\g<1>" + name + r"\g<2>", text, count=1)
    for kind, items in (("State", states), ("Action", actions)):
        group = kind + "s"
        m = re.search(r"<" + group + r">(.*?)</" + group + r">", text, re.DOTALL)
        body = m.group(1) if m else ""
        for ident, stem in items.items():
            body = re.sub(r"<" + kind + r">\s*<ID>\s*" + re.escape(ident) + r"\s*</ID>.*?</" + kind + r">\s*", "",
                          body, flags=re.DOTALL | re.IGNORECASE)
            body += f"<{kind}><ID>{ident}</ID><Animation><Filename>{stem}.animx</Filename></Animation></{kind}>\n"
        if m:
            text = text[:m.start(1)] + body + text[m.end(1):]
        else:
            g = text.find("</Group>")
            text = text[:g] + f"<{group}>{body}</{group}>" + text[g:]
    return text


def _anim_file(stem, triggers, moving):
    t = "".join(f"<Trigger><Name>{n}</Name><Frame>{f}</Frame></Trigger>" for n, f in triggers)
    out = f"<Anim_file><Animation><Filename>{stem}.animx</Filename><Preload>False</Preload></Animation>"
    if t:
        out += f"<Triggers>{t}</Triggers>"
    if moving:
        out += "<Flags>" + "".join(f"<Flag>{f}</Flag>" for f in MOVING_FLAGS) + "</Flags>"
    return out + "</Anim_file>"


def _record(mod_folder, sets=(), files=(), vehicles=(), drop_vehicle=None):
    """Our entries in the shared lists (mods\\animations\\anim_entries.txt): "set x.xtbl", "file x.animx",
    and "vehicle <name> <its interaction before ours>" (the Guerrilla tools read only the first two)."""
    path = os.path.join(mod_folder, *bio.ANIMATIONS_FOLDER, OWN_RECORD)
    lines = []
    if os.path.isfile(path):
        with open(path, "r", encoding="latin-1") as f:
            lines = [l.strip() for l in f if l.strip()]
    if drop_vehicle:
        low = drop_vehicle.lower()
        lines = [l for l in lines if not (l.lower() == f"set {set_name(drop_vehicle)}.xtbl" or
                                          l.lower().startswith(f"file rfa_{low}_") or
                                          l.lower().startswith(f"vehicle {low} "))]
    for kind, items in (("set", sets), ("file", files), ("vehicle", vehicles)):
        for item in items:
            line = f"{kind} {item}"
            key = line.lower() if kind != "vehicle" else " ".join(line.lower().split()[:2])
            lines = [l for l in lines if (l.lower() if kind != "vehicle" else " ".join(l.lower().split()[:2])) != key]
            lines.append(line)
    bv._write(path, ("\r\n".join(lines) + "\r\n").encode("latin-1"))
    return lines


def recorded_original(mod_folder, vehicle):
    path = os.path.join(mod_folder, *bio.ANIMATIONS_FOLDER, OWN_RECORD)
    if os.path.isfile(path):
        with open(path, "r", encoding="latin-1") as f:
            for l in f:
                p = l.split()
                if len(p) >= 3 and p[0] == "vehicle" and p[1].lower() == vehicle.lower():
                    return p[2]
    return None


def export(context, game_folder, mod_folder, root, arm, report):
    """The clips as animations, and the tables that play them. Returns the paths written."""
    vehicle = root["rfa_people_vehicle"]
    actions = clip_actions(vehicle)
    if "enter" not in actions:
        raise ValueError(f"{vehicle} has no Get In clip")
    scene = context.scene
    shown = arm.animation_data.action if arm.animation_data else None
    _save_markers(scene, shown)
    keep = (scene.frame_start, scene.frame_end, scene.frame_current, root.get("rfa_people_clip", "enter"))
    written = []
    triggers = {}
    try:
        for clip, action in actions.items():
            show_clip(context, root, arm, clip)
            anim, header = bio.build_animation(context, arm, _Settings(), report)
            data = rf.write_animation(anim, header)
            rf.Animation.read(data)
            path = bio.animation_path(mod_folder, action["rfa_name"])
            bv._write(path, data)
            written.append(path)
            # Triggers at 30 frames a second, as anim_files.xtbl counts them, from the clip's start.
            first = int(round(action.frame_range[0]))
            triggers[clip] = sorted(((n, max(0, (int(f) - first) // 2)) for n, f in json.loads(action.get("rfa_triggers", "[]"))),
                                    key=lambda t: t[1])
    finally:
        show_clip(context, root, arm, keep[3] if keep[3] in actions else "enter")
        scene.frame_start, scene.frame_end = keep[0], keep[1]
        scene.frame_set(keep[2])

    # The interaction ours is made from, and the set it played.
    vtext = read_table(game_folder, mod_folder, "vehicles.xtbl")
    ventry = vehicle_entry(vtext, vehicle)
    itext = read_table(game_folder, mod_folder, "vehicle_interactions.xtbl")
    ours = interaction_name(vehicle)
    current = vehicle_interaction(ventry)
    original = recorded_original(mod_folder, vehicle) or (current if current.lower() != ours.lower() else "") or \
        root.get("rfa_people_original", "Dummy")
    if not _entry(itext, "Vehicle", original):
        original = "Dummy"
    base_name = ours if _entry(itext, "Vehicle", ours) else original
    span = _entry(itext, "Vehicle", base_name)
    base_text = itext[span[0]:span[1]]
    base_el = ET.fromstring(base_text)
    _, clip_roles = roles(base_el)
    first_set = (ET.fromstring(itext[slice(*_entry(itext, "Vehicle", original))]).findtext("Anim_Set") or "").strip()

    steps, states, set_actions = {}, {}, {}
    enter_action, seated = _transition_steps(base_el, *clip_roles["enter"])
    seated = seated or SEATED_STATE
    doors = door_parts(game_folder, root)
    wps = waypoints(root)
    used_doors = []

    def door_for(clip, at):
        """The door nearest the place a clip with door triggers gets in or out at (else none)."""
        if not any(n.startswith("door") for n, _ in triggers.get(clip, [])) or not doors or wps.get(at) is None:
            return None
        here = wps[at].matrix_world.to_translation()
        name = min(doors, key=lambda d: (doors[d] - here).length)
        used_doors.append(name)
        return name

    steps[clip_roles["enter"]] = (ENTER_ACTION, seated, door_for("enter", clip_roles["enter"][0]))
    set_actions[ENTER_ACTION] = actions["enter"]["rfa_name"]
    if "drive" in actions:
        states[seated] = actions["drive"]["rfa_name"]
    if "exit" in actions and "exit" in clip_roles:
        _, standing = _transition_steps(base_el, *clip_roles["exit"])
        steps[clip_roles["exit"]] = (EXIT_ACTION, standing or STANDING_STATE, door_for("exit", clip_roles["exit"][1]))
        set_actions[EXIT_ACTION] = actions["exit"]["rfa_name"]
    if used_doors:
        report({"INFO"}, f"{vehicle}: the door triggers open {', '.join(dict.fromkeys(used_doors))}")
    the_set = set_name(vehicle)

    # Our anim set: a new table the game finds by name.
    set_text = _set_table(game_folder, first_set if first_set.lower() != the_set else "", the_set, states, set_actions)
    set_path = os.path.join(mod_folder, "overrides", "build", "pc", "cache", the_set + ".xtbl")
    bv._write(set_path, set_text.encode("latin-1"))
    written.append(set_path)

    # The lists.
    lst = read_table(game_folder, mod_folder, "anim_set_filenames.xtbl")
    lst = _put_entry(lst, "Anim_Set_Filename", the_set + ".xtbl",
                     f"<Anim_Set_Filename><Name>{the_set}.xtbl</Name><Path>\\</Path></Anim_Set_Filename>")
    written.append(write_table(mod_folder, "anim_set_filenames.xtbl", lst))

    files = read_table(game_folder, mod_folder, "anim_files.xtbl")
    for clip, action in actions.items():
        stem = action["rfa_name"]
        entry = _anim_file(stem, triggers.get(clip, []), clip != "drive")
        span = _anim_file_entry(files, stem + ".animx")
        files = files[:span[0]] + entry + files[span[1]:] if span else _insert_before(files, "</Anim_files>", entry)
        # Any other entry for the same file (left by an older export) goes: the game would read the first.
        keep_end = _anim_file_entry(files, stem + ".animx")[1]
        while True:
            rest = _anim_file_entry(files[keep_end:], stem + ".animx")
            if rest is None:
                break
            s, e = keep_end + rest[0], keep_end + rest[1]
            while e < len(files) and files[e] in "\r\n":
                e += 1
            while s > 0 and files[s - 1] in " \t":
                s -= 1
            files = files[:s] + files[e:]
    written.append(write_table(mod_folder, "anim_files.xtbl", files))

    entry = _interaction_entry(base_text, vehicle, root, the_set, steps)
    itext = _put_entry(itext, "Vehicle", ours, entry)
    written.append(write_table(mod_folder, "vehicle_interactions.xtbl", itext))

    new_entry = re.sub(r"(<Vehicle_Interaction>)[^<]*(</Vehicle_Interaction>)", r"\g<1>" + ours + r"\g<2>", ventry, count=1)
    if new_entry == ventry and "<Vehicle_Interaction>" not in ventry:
        new_entry = ventry.replace("</Vehicle>", f"<Vehicle_Interaction>{ours}</Vehicle_Interaction></Vehicle>", 1)
    span = _entry(vtext, "Vehicle", vehicle)
    vtext = vtext[:span[0]] + new_entry + vtext[span[1]:]
    written.append(write_table(mod_folder, "vehicles.xtbl", vtext))

    _record(mod_folder, sets=[the_set + ".xtbl"], files=[a["rfa_name"] + ".animx" for a in actions.values()],
            vehicles=[f"{vehicle} {original}"])
    return written


def door_parts(game_folder, root):
    """{part name: where it is (world)} of the kit's doors (part type "door")."""
    try:
        _, model = bv.open_kind(game_folder, root["rfa_vehicle"])
    except Exception:
        return {}
    kinds = {p.index: p for p in model.parts}
    out = {}
    for o in bv._kit_objects(root):
        p = kinds.get(o.get("rfa_part", -2))
        if p is not None and p.type == DOOR_TYPE:
            out[p.name] = o.matrix_world.to_translation()
    return out


def restart_clip(context, game_folder, root, arm, clip, rel, container, name, report):
    """The clip started anew from another of the game's animations (its triggers too), keeping its place."""
    vehicle = root["rfa_people_vehicle"]
    old = clip_actions(vehicle).get(clip)
    if old is None:
        raise ValueError(f"{vehicle} has no {CLIP_LABELS[clip]} clip")
    anim = rf.Animation.read(bio.Animations.read(game_folder, rel, container, name))
    scene = context.scene
    _save_markers(scene, arm.animation_data.action if arm.animation_data else None)
    arm.animation_data_create()
    arm.animation_data.action = None
    arm.location = (0, 0, 0)
    arm.rotation_mode = "QUATERNION"
    arm.rotation_quaternion = (1, 0, 0, 0)
    action = bio.apply_animation(context, arm, anim, f"{vehicle} {CLIP_LABELS[clip]}", report)
    stem = name[: -len(".anim_pc")] if name.lower().endswith(".anim_pc") else name
    for key in ("rfa_name", "rfa_vehicle", "rfa_clip", "rfa_from_waypoint", "rfa_to_waypoint"):
        action[key] = old[key]
    action["rfa_source"] = stem
    files_text = game_table(game_folder, "anim_files.xtbl") or ""
    action["rfa_triggers"] = json.dumps(source_triggers(files_text, stem))
    arm.animation_data.action = None
    label = old.name
    bpy.data.actions.remove(old)
    action.name = label
    show_clip(context, root, arm, clip)
    return action


def remove(game_folder, mod_folder, vehicle):
    """Takes the vehicle's animations and table entries out again; it gets in as it did before."""
    removed = []
    for clip in CLIPS:
        p = bio.animation_path(mod_folder, anim_name(vehicle, clip))
        if os.path.isfile(p):
            os.remove(p)
            removed.append(p)
    the_set = set_name(vehicle)
    p = os.path.join(mod_folder, "overrides", "build", "pc", "cache", the_set + ".xtbl")
    if os.path.isfile(p):
        os.remove(p)
        removed.append(p)
    original = recorded_original(mod_folder, vehicle) or "Dummy"

    def edit(file_name, change):
        for path in (_gw_path(mod_folder, file_name), _ours_path(mod_folder, file_name)):
            if not os.path.isfile(path):
                continue
            with open(path, "rb") as f:
                text = f.read().decode("latin-1")
            new = change(text)
            if new != text:
                bv._write(path, new.encode("latin-1"))
                removed.append(path)

    edit("anim_set_filenames.xtbl", lambda t: _drop_entry(t, "Anim_Set_Filename", the_set + ".xtbl"))

    def drop_files(t):
        for clip in CLIPS:
            span = _anim_file_entry(t, anim_name(vehicle, clip) + ".animx")
            if span:
                t = t[:span[0]] + t[span[1]:]
        return t
    edit("anim_files.xtbl", drop_files)
    edit("vehicle_interactions.xtbl", lambda t: _drop_entry(t, "Vehicle", interaction_name(vehicle)))

    def restore(t):
        span = _entry(t, "Vehicle", vehicle)
        if span is None:
            return t
        e = t[span[0]:span[1]]
        e2 = re.sub(r"(<Vehicle_Interaction>)\s*" + re.escape(interaction_name(vehicle)) + r"\s*(</Vehicle_Interaction>)",
                    r"\g<1>" + original + r"\g<2>", e, flags=re.IGNORECASE)
        return t[:span[0]] + e2 + t[span[1]:]
    edit("vehicles.xtbl", restore)
    _record(mod_folder, drop_vehicle=vehicle)
    return removed
