"""A vehicle's parameters in vehicles.xtbl, for editing, and templates (no bpy here).

An entry is kept as an XML tree (the working copy). Its parameters are its leaves, each found by its path from
the entry: (tag, index among siblings of that tag) pairs, so the second axle's friction is
(("Axles", 0), ("Axle", 1), ("Wheel_Friction", 0)). The table's own description (<TableDescription> at its end)
gives each parameter its type, name to show, description, choices and default.

Templates: any vehicle in the table (the game's and the Guerrilla world's), or one of the user's own (a .json of
sections, each the section's elements as XML). Applying one copies whole sections into the working copy; the
axles are copied axle by axle (a vehicle's axle count is its model's and never changes), and the parts that
belong to the vehicle itself (its name, model, interaction, lights, turrets, spawn box) are never copied.
"""

import copy
import json
import os
import re
import xml.etree.ElementTree as ET

# Sections: what each holds (top-level tags of an entry), in the order shown.
SECTIONS = (
    ("ENGINE", "Engine", ("Engine",)),
    ("TRANSMISSION", "Transmission", ("Transmission",)),
    ("WHEELS", "Wheels & suspension", ("Axles",)),
    ("STEERING", "Steering", ("Steering", "Extra_Steering_Torque")),
    ("HANDLING", "Handling & speed", ("Mass", "Center_Of_Mass_Y", "Center_Of_Mass_Z", "Viscosity_Friction", "Roll_Torque_Factor",
                                      "Pitch_Torque_Factor", "Yaw_Torque_Factor", "Yaw_Torque_Scalar", "Yaw_Unit_Inertia", "Angular_Damping",
                                      "Aerodynamics", "EnforcedMaxSpeed", "EnforcedMaxRSpeed", "AIEnforcedMaxSpeed", "Powerslide_Friction",
                                      "Powerslide_Power")),
    ("DAMAGE", "Health & damage", ("Max_Hitpoints", "Electric_Hitpoints", "Terrain_Damage_Scale", "Collision_Damage_Scale",
                                   "Explode_When_On_Fire", "humans_invulnerable")),
    ("CAMERA", "Camera", ("Cameras", "speed_effects", "Camera_Shake_Multiplier")),
    ("SOUNDS", "Sounds", ("Foley",)),
    ("EFFECTS", "Effects", ("Effects",)),
    ("GENERAL", "General", ("Group", "Value", "Default_Team", "Special_Vehicle_Type", "Convertible", "CanBlockRoads",
                            "Always_Allow_enter", "Player_Passenger_Only", "Neutral_Reticule", "Has_Vision_Mode")),
)
# Templates copy only the tags the sections name. Everything else in an entry (its rig and rider animations, seats,
# items, lights, turrets, model, ...) is the vehicle's own: copying an ATV's rig into the ambulance crashed the game
# at start-up (2026-11-26).
COPIED = {t for _, _, tags in SECTIONS for t in tags}
# The vehicle's own: never copied from a template, shown read-only under General.
OWN = ("Name", "_Editor", "Mesh_Name", "Vehicle_Interaction", "Lights", "Turrets", "spawn_offset_info", "Component_Info",
       "Memory_Slot_Type", "VehicleClass", "Preload", "Display_Name")
TEMPLATE_FOLDER = ("mods", "vehicle_templates")


def section_of(top_tag):
    for key, _, tags in SECTIONS:
        if top_tag in tags:
            return key
    return "OTHER"


def entry_span(text, name):
    """(start, end) of a vehicle's <Vehicle> entry in the table text."""
    m = re.search(r"<Vehicle>\s*<Name>\s*" + re.escape(name) + r"\s*</Name>", text, re.IGNORECASE)
    if not m:
        return None
    end = text.find("</Vehicle>", m.end())
    return (m.start(), end + len("</Vehicle>")) if end >= 0 else None


def vehicle_names(text):
    body = text[:text.find("<TableDescription>")] if "<TableDescription>" in text else text
    return re.findall(r"<Vehicle>\s*<Name>\s*([^<]+?)\s*</Name>", body)


def parse_entry(text, name):
    span = entry_span(text, name)
    return None if span is None else ET.fromstring(text[span[0]:span[1]])


class Description:
    """The table's description of its fields: {tag path without indices: info}."""

    def __init__(self, text):
        self.info = {}
        i = text.find("<TableDescription>")
        if i < 0:
            return
        try:
            root = ET.fromstring(text[i:text.find("</TableDescription>", i) + len("</TableDescription>")])
        except ET.ParseError:
            return
        self._walk(root, ())

    def _walk(self, node, path):
        for el in node.findall("Element"):
            name = (el.findtext("Name") or "").strip()
            if not name:
                continue
            p = path + (name,)
            self.info[p] = {
                "type": (el.findtext("Type") or "").strip(),
                "label": (el.findtext("Display_Name") or "").strip(),
                "description": " ".join((el.findtext("Description") or "").split()),
                "default": (el.findtext("Default") or "").strip(),
                "choices": [c.text.strip() for c in el.findall("Choice") if c.text and c.text.strip()],
                "min": (el.findtext("MinValue") or el.findtext("Min") or "").strip(),
                "max": (el.findtext("MaxValue") or el.findtext("Max") or "").strip(),
            }
            self._walk(el, p)

    def of(self, tags):
        """Info for a field by its tags (without the entry's own tag); lists' items may sit under their list's name."""
        if tags in self.info:
            return self.info[tags]
        for k in range(1, len(tags)):  # an item of a list described without the item level
            alt = tags[:k] + tags[k + 1:]
            if alt in self.info:
                return self.info[alt]
        return {"type": "", "label": "", "description": "", "default": "", "choices": [], "min": "", "max": ""}


def leaves(root):
    """[(path, tags, text)] of an entry's leaves: path ((tag, index), ...) from the entry, tags without indices."""
    out = []

    def walk(node, path):
        counts = {}
        for child in node:
            n = counts.get(child.tag, 0)
            counts[child.tag] = n + 1
            p = path + ((child.tag, n),)
            if len(child):
                walk(child, p)
            else:
                out.append((p, tuple(t for t, _ in p), (child.text or "").strip()))
    walk(root, ())
    return out


def find(root, path):
    node = root
    for tag, index in path:
        kids = [c for c in node if c.tag == tag]
        if index >= len(kids):
            return None
        node = kids[index]
    return node


def set_value(root, path, value):
    el = find(root, path)
    if el is None:
        return False
    old_tail = el.tail
    el.text = value
    el.tail = old_tail
    return True


def path_key(path):
    return "/".join(f"{t}[{i}]" for t, i in path)


def key_path(key):
    out = []
    for part in key.split("/"):
        m = re.fullmatch(r"(.+)\[(\d+)\]", part)
        out.append((m.group(1), int(m.group(2))))
    return tuple(out)


def label_of(path, info):
    """A readable name: the description's, else the tag; list items numbered (Axle 2, Gear Ratios 1)."""
    tag, index = path[-1]
    name = info.get("label") or tag.replace("_", " ")
    parents = []
    for t, i in path[:-1]:
        if t in ("Axle", "Camera", "Element", "LightMap", "Mount"):
            parents.append(f"{t.replace('_', ' ')} {i + 1}")
    if tag == "Element":
        name = (path[-2][0] if len(path) > 1 else tag).replace("_", " ") + f" {index + 1}"
        parents = [p for p in parents if not p.startswith("Element")]
    return " / ".join(parents + [name])


# ---------------------------------------------------------------- templates


def sections_xml(root, sections):
    """{section: [element XML, ...]} of an entry's chosen sections (the vehicle's own parts left out)."""
    out = {}
    for child in root:
        if child.tag in OWN or child.tag not in COPIED:
            continue
        s = section_of(child.tag)
        if s in sections:
            el = copy.deepcopy(child)
            el.tail = None
            out.setdefault(s, []).append(ET.tostring(el, encoding="unicode", short_empty_elements=False))
    return out


def apply_sections(root, template, sections):
    """Copy a template's sections ({section: [element XML]}) into an entry, in place. Returns how many elements."""
    count = 0
    for s in sections:
        for xml in template.get(s, []):
            new = ET.fromstring(xml)
            if new.tag in OWN or new.tag not in COPIED:  # (older templates may hold more)
                continue
            old = root.find(new.tag)
            if new.tag == "Axles" and old is not None:
                # Axle by axle, keeping the vehicle's own count (its model's).
                mine, theirs = old.findall("Axle"), new.findall("Axle")
                for k, axle in enumerate(mine):
                    if not theirs:
                        break
                    src = theirs[min(k, len(theirs) - 1)]
                    tail = axle.tail
                    axle.clear()
                    axle.text, axle.tail = src.text, tail
                    axle.attrib.update(src.attrib)
                    for c in src:
                        axle.append(copy.deepcopy(c))
                count += 1
                continue
            if old is not None:
                new.tail = old.tail
                idx = list(root).index(old)
                root.remove(old)
                root.insert(idx, new)
            else:
                last = list(root)[-1] if len(root) else None
                if last is not None:
                    new.tail, last.tail = last.tail, (list(root)[0].tail if len(root) > 1 else "\n\t\t")
                root.append(new)
            count += 1
    return count


def template_folder(mod_folder):
    return os.path.join(mod_folder, *TEMPLATE_FOLDER)


def user_templates(mod_folder):
    folder = template_folder(mod_folder)
    if not os.path.isdir(folder):
        return []
    return sorted(n[:-5] for n in os.listdir(folder) if n.lower().endswith(".json"))


def save_template(mod_folder, name, root, sections, source=""):
    folder = template_folder(mod_folder)
    os.makedirs(folder, exist_ok=True)
    safe = re.sub(r"[^A-Za-z0-9_\- ]", "_", name).strip() or "template"
    path = os.path.join(folder, safe + ".json")
    with open(path, "w", encoding="utf-8") as fh:
        json.dump({"name": name, "from": source, "sections": sections_xml(root, sections)}, fh, indent=1)
    return path


def load_template(mod_folder, name):
    with open(os.path.join(template_folder(mod_folder), name + ".json"), encoding="utf-8") as fh:
        return json.load(fh)["sections"]


def delete_template(mod_folder, name):
    path = os.path.join(template_folder(mod_folder), name + ".json")
    if os.path.isfile(path):
        os.remove(path)


def entry_text(root):
    # Never <tag />: the game's table reader takes only <tag></tag> (a short one crashed it at start-up, 2026-11-26).
    return ET.tostring(root, encoding="unicode", short_empty_elements=False)
