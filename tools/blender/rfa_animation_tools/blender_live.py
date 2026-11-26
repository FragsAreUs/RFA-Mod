"""Push live: changes sent to the running game as they are written, through RFA-Mod's editor link.

The game (rfa_qol's console part) checks <RFA-Mod folder>\\link\\to_game.txt ten times a second while the
folder link\\ exists, runs each line as a console command (or one of the editor's own editor_... lines),
and deletes the file; a file more than 30 seconds old is dropped unrun (sent while the game wasn't running).

What can go live now:
- vehicles.xtbl (tuning, the vehicle's own sounds): the game's own console command reload_vehicles (Reload
  vehicle table., 0x619550 -> 0x6195E0): it stops the player's vehicle's engine sound, reads the table again
  into each vehicle's entry by name (0x626820: 0x6271C0 -> 0x6309C0, which parses each <Vehicle> and copies
  it over the entry of that name) and points every vehicle in the world at its entry again. The sounds and
  the settings read while driving change at once; the physics the game builds when a vehicle is made
  (engine, gears, wheels, suspension, mass) are a new vehicle's: spawn one again (drop_car <name>).
"""

import os

import bpy

LINK = "link"
COMMANDS = "to_game.txt"


def _mod_folder(context):
    p = context.preferences.addons[__package__].preferences
    return bpy.path.abspath(p.mod_folder or p.game_folder)


def enabled(context):
    return bool(getattr(context.window_manager, "rfa_push_live", False))


def send(context, *lines):
    """Hand console lines to the running game (appended to the link file; the game runs and deletes it)."""
    folder = os.path.join(_mod_folder(context), LINK)
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, COMMANDS), "a", encoding="ascii", errors="replace") as fh:
        for line in lines:
            fh.write(line.strip() + "\n")
    return os.path.join(folder, COMMANDS)


def drop_name(context, vehicle):
    """The name drop_car takes: the entry's Display_Name (the spawner's +4), else its own name."""
    from . import blender_vehicle_anim as bva
    from . import rfa_vehicle_params as vp
    p = context.preferences.addons[__package__].preferences
    try:
        root = vp.parse_entry(bva.read_table(bpy.path.abspath(p.game_folder), _mod_folder(context), "vehicles.xtbl"), vehicle)
    except Exception:
        root = None
    shown = (root.findtext("Display_Name") or "").strip() if root is not None else ""
    return shown or vehicle


def vehicles_changed(context, vehicle=None, report=None):
    """vehicles.xtbl was written: with Push live on, the game reads it again (and, if asked, spawns the vehicle again)."""
    if not enabled(context):
        return False
    lines = ["reload_vehicles"]
    wm = context.window_manager
    if vehicle and getattr(wm, "rfa_push_respawn", False):
        lines.append(f"drop_car {drop_name(context, vehicle)}")
    send(context, *lines)
    if report:
        report({"INFO"}, "Sent to the game: " + "; ".join(lines))
    return True


class RFA_OT_push_now(bpy.types.Operator):
    """Have the running game read the vehicle table again now (and spawn the vehicle again, if ticked)"""
    bl_idname = "rfa.push_now"
    bl_label = "Push to Game Now"

    def execute(self, context):
        from . import blender_sounds as bs
        vehicle, _ = bs.current_vehicle(context)
        lines = ["reload_vehicles"]
        if vehicle and context.window_manager.rfa_push_respawn:
            lines.append(f"drop_car {drop_name(context, vehicle)}")
        path = send(context, *lines)
        self.report({"INFO"}, f"Sent to the game ({path}): " + "; ".join(lines))
        return {"FINISHED"}


def draw(layout, context):
    """The Push live settings, as the Tuning and Sounds tabs show them."""
    wm = context.window_manager
    box = layout.box()
    row = box.row(align=True)
    row.prop(wm, "rfa_push_live", icon="LINKED" if wm.rfa_push_live else "UNLINKED")
    row.operator(RFA_OT_push_now.bl_idname, text="", icon="EXPORT")
    if wm.rfa_push_live:
        box.prop(wm, "rfa_push_respawn")
        if wm.rfa_push_respawn:
            box.label(text="(on foot: the game makes no vehicle while you drive one)")
        box.label(text="Sounds change at once; driving physics on a new vehicle", icon="INFO")


classes = (RFA_OT_push_now,)


def register():
    for c in classes:
        bpy.utils.register_class(c)
    wm = bpy.types.WindowManager
    wm.rfa_push_live = bpy.props.BoolProperty(
        name="Push live", default=False,
        description="Send every change written to the game's tables to the running game too (it reads them again)")
    wm.rfa_push_respawn = bpy.props.BoolProperty(
        name="Spawn the vehicle again", default=False,
        description="After the game reads the table again, drop a new one of the vehicle by the player "
                    "(drop_car: its engine, gears, wheels and suspension are made from the new settings)")


def unregister():
    wm = bpy.types.WindowManager
    for p in ("rfa_push_respawn", "rfa_push_live"):
        if hasattr(wm, p):
            delattr(wm, p)
    for c in reversed(classes):
        bpy.utils.unregister_class(c)
