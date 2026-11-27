"""Red Faction: Armageddon's vehicle models (.ccar_pc + .gcar_pc, version 30), read and written, in
plain Python (no Blender). The layout is the one the RFA-Mod editor's GuerrillaVehicles.cs and
VehicleParts.cs describe, checked on all 17 of the game's vehicles:

  header (0x148)  +0x00 version 30, +0x04 the seat-to-door links, +0x08/+0x0C the tag deformation
                  records (count, offset; 28 bytes each), +0x10 the collision, +0x14/+0x18 the MOPP
                  table (one word a collision shape: an offset, or -1), +0x24 the mesh block, +0x28
                  the material map, +0x2C the materials' offsets, +0x30 how many, +0x34 the texture
                  names, +0x38 the dent morph target, +0x88 a count of (hash, hash) pairs from +0x8C,
                  +0xD4 the chassis's tags, +0xE4 its mass, +0xE8 centre of mass, +0xF4 inertia (3x3),
                  +0x118 -1, +0x11C/+0x120 the chassis's lights (80 bytes each), +0x124/+0x128 and
                  +0x12C/+0x130 two more arrays (count, offset: after the MOPP trees), +0x134 tread
                  spline count, +0x138 tread width, +0x13C the splines, +0x144 the part count
  parts (264)     +0x00 the part type's hash, +0x0C flags (0x20: its collision is pieces of the
                  chassis's, +0xC4 the first, +0xC8 how many; 0x800 never drawn), +0x10 the parent
                  part (-1 the vehicle), +0x14/+0x18 children (count, offset), +0x1C rotation (rows
                  right, up, forward), +0x40 position (from the parent), +0x4C the name (40 bytes),
                  +0x74 its collision shape, +0x78 its tags, +0x88/+0x8C its lights, +0xA0 its hinge
                  (a door's), +0xCC (and +0xD0) its submesh (-1 none), +0xD4 mass, +0xD8 centre of
                  mass, +0xE4 inertia
  then            the lights and tag lists (a count, then 52 bytes a tag: name hash, rotation,
                  position), the mesh block (on 128), the material map, the materials' offsets, the
                  materials, the texture names (each with two NULs, to an even length), "the rest"
                  (children, seat-to-door links, hinges, tag deformation records, tread splines),
                  the collision ("MCKH" and a Havok 2010 packfile: an hkpListShape whose children are
                  the shapes: 0 the chassis's, the parts' by their +0x74), the MOPP table and trees,
                  and the two arrays.
  g-file          the checksum and 12 zeros, the indices (16-bit, triangle strips), the vertices on
                  16 (format 2: position, normal and tangent as bytes, two UV sets, 1024 = 1.0, v
                  down from the texture's top), the checksum.

A model is written by keeping everything up to the mesh block where it is (the parts, tags and
lights edited in place) and rebuilding the mesh, materials, texture names, collision and MOPP table,
"the rest" and the arrays moved to where they land (every offset into them is known: above).
Collision shapes made anew have no MOPP tree (-1, as the convex shapes of every game vehicle have).
"""

import math
import struct
import zlib

try:
    from . import rfa_formats as rf
except ImportError:  # run on its own
    import rfa_formats as rf

VERSION = 30
HEADER = 0x148
PART = 0x108
TAG = 52
LIGHT = 80

# Tag names whose hashes the game's vehicles use (rf.name_hash); others are kept by hash.
TAG_NAMES = [
    "driver", "engine", "gas_tank", "exhaust", "suspension_pivot", "Piston_end", "Piston_stop",
    "turret_1", "turret_2", "turret_body", "turret_head", "PD_Camera", "Camera_check_pos",
    "Mesh_Glare_Primary", "Mesh_Glare_Secondary", "LT_Head_FL", "LT_Head_FR", "LT_Spot_FL", "LT_Spot_FR",
] + [f"passenger_{n}" for n in range(1, 13)] + [f"camera_check_pos{n:02d}" for n in range(1, 11)]
_TAG_BY_HASH = {rf.name_hash(n): n for n in TAG_NAMES}


def tag_name(h):
    return _TAG_BY_HASH.get(h, f"tag_{h:08x}")


def tag_hash(name):
    if name.lower().startswith("tag_") and len(name) == 12:
        try:
            return int(name[4:], 16)
        except ValueError:
            pass
    return rf.name_hash(name)


def _i(b, a):
    return struct.unpack_from("<i", b, a)[0]


def _f(b, a):
    return struct.unpack_from("<f", b, a)[0]


def _align(n, a):
    return (n + a - 1) // a * a


# ---------------------------------------------------------------- collision (Havok 2010)


class Convex:
    """A convex shape: its corners, its faces' planes (normal, distance: n.p + d is 0 on the face),
    the radius it is rounded by."""

    def __init__(self, vertices, planes, radius=0.05):
        self.vertices, self.planes, self.radius = vertices, planes, radius


class ShapeList:
    def __init__(self, children):
        self.children = children


_HK0, _HK1 = 0x57E0E057, 0x10C0C010
_CLASSES = [(0x75585EF6, "hkClass"), (0x5C7EA4C2, "hkClassMember"), (0x8A3609CF, "hkClassEnum"),
            (0xCE6F8A6C, "hkClassEnumItem"), (0xA1937CBD, "hkpListShape"), (0x28726AD8, "hkpConvexVerticesShape")]


def read_collision(b, section):
    """The shapes of a model's collision section ("MCKH" at section): its top list's children."""
    if b[section:section + 4] != b"MCKH":
        raise ValueError("no collision where the header says")
    at = section + 16
    if struct.unpack_from("<2I", b, at) != (_HK0, _HK1):
        raise ValueError("no Havok packfile in the collision")

    def hdr(s):
        return at + 0x40 + s * 0x30 + 20

    names, data = at + _i(b, hdr(0)), at + _i(b, hdr(2))
    local, glob, virt, exports = _i(b, hdr(2) + 4), _i(b, hdr(2) + 8), _i(b, hdr(2) + 12), _i(b, hdr(2) + 16)
    cls = {}
    for q in range(virt, exports - 11, 12):
        if _i(b, data + q) == -1:
            continue
        cls[_i(b, data + q)] = rf._cstr(b, names + _i(b, data + q + 8))
    locs = [(_i(b, data + q), _i(b, data + q + 4)) for q in range(local, glob - 7, 8) if _i(b, data + q) != -1]
    globs = {_i(b, data + q): _i(b, data + q + 8) for q in range(glob, virt - 11, 12) if _i(b, data + q) != -1}
    loc_to = dict(locs)
    children = {}
    for obj, name in cls.items():
        if name == "hkpListShape":
            n = _i(b, data + obj + 0x1C)
            arr = loc_to.get(obj + 0x18, 0)
            children[obj] = [globs[arr + 16 * k] for k in range(n)]
    tops = [l for l in children if not any(l in c for c in children.values())]
    if len(tops) != 1:
        raise ValueError("the collision has no single top list")

    def v3(a):
        return struct.unpack_from("<3f", b, a)

    def shape(obj):
        kind = cls[obj]
        radius = _f(b, data + obj + 0x10)
        if kind == "hkpListShape":
            return ShapeList([shape(c) for c in children[obj]])
        if kind == "hkpConvexVerticesShape":
            arrays = sorted((f, t) for f, t in locs if obj < f < obj + 0x70)
            n, nplanes = _i(b, data + obj + 0x4C), _i(b, data + arrays[1][0] + 4)
            verts = []
            for v in range(n):
                blk, lane = data + arrays[0][1] + (v // 4) * 48, (v % 4) * 4
                verts.append((_f(b, blk + lane), _f(b, blk + 16 + lane), _f(b, blk + 32 + lane)))
            planes = [struct.unpack_from("<4f", b, data + arrays[1][1] + 16 * p) for p in range(nplanes)]
            return Convex(verts, planes, radius)
        if kind == "hkpBoxShape":
            hx, hy, hz = v3(data + obj + 0x20)
            verts = [((hx if k & 1 else -hx), (hy if k & 2 else -hy), (hz if k & 4 else -hz)) for k in range(8)]
            planes = [(1, 0, 0, -hx), (-1, 0, 0, -hx), (0, 1, 0, -hy), (0, -1, 0, -hy), (0, 0, 1, -hz), (0, 0, -1, -hz)]
            return Convex(verts, planes, radius)
        raise ValueError(f"collision shape {kind} isn't read")

    return [shape(c) for c in children[tops[0]]]


def write_collision(shapes):
    """A collision section ("MCKH", 5, its length, the shape count, the packfile) for these shapes."""
    d = bytearray()
    locs, globs, objs = [], [], []

    def put_i(a, v):
        struct.pack_into("<i", d, a, v)

    def put_f(a, v):
        struct.pack_into("<f", d, a, v)

    def lst(kids):
        at = len(d)
        objs.append((at, "hkpListShape"))
        d.extend(bytes(0x70 + 16 * len(kids)))
        put_i(at + 0x1C, len(kids))
        put_i(at + 0x20, -0x80000000 | len(kids))
        d[at + 0x50:at + 0x70] = b"\xff" * 0x20
        locs.append((at + 0x18, at + 0x70))
        lo, hi, rad = [1e30] * 3, [-1e30] * 3, 0.0
        for i, k in enumerate(kids):
            globs.append((at + 0x70 + 16 * i, len(d)))
            cmin, cmax, cr = convex(k) if isinstance(k, Convex) else lst(k.children)
            lo = [min(lo[a], cmin[a]) for a in range(3)]
            hi = [max(hi[a], cmax[a]) for a in range(3)]
            rad = max(rad, cr)
        for a in range(3):
            put_f(at + 0x30 + 4 * a, (hi[a] - lo[a]) / 2)
            put_f(at + 0x40 + 4 * a, (hi[a] + lo[a]) / 2)
        put_f(at + 0x3C, rad)
        put_f(at + 0x4C, 1.0)
        return lo, hi, rad

    def convex(s):
        at = len(d)
        objs.append((at, "hkpConvexVerticesShape"))
        d.extend(bytes(0x70))
        vs = s.vertices
        lo = [min(v[a] for v in vs) for a in range(3)]
        hi = [max(v[a] for v in vs) for a in range(3)]
        blocks = (len(vs) + 3) // 4
        put_f(at + 0x10, s.radius)
        for a in range(3):
            put_f(at + 0x20 + 4 * a, (hi[a] - lo[a]) / 2)
            put_f(at + 0x30 + 4 * a, (hi[a] + lo[a]) / 2)
        put_i(at + 0x44, blocks)
        put_i(at + 0x48, -0x80000000 | blocks)
        put_i(at + 0x4C, len(vs))
        put_i(at + 0x5C, len(s.planes))
        put_i(at + 0x60, -0x80000000 | len(s.planes))
        locs.append((at + 0x40, len(d)))
        for blk in range(blocks):
            for axis in range(3):
                for lane in range(4):
                    d.extend(struct.pack("<f", vs[min(blk * 4 + lane, len(vs) - 1)][axis]))
        locs.append((at + 0x58, len(d)))
        for p in s.planes:
            d.extend(struct.pack("<4f", *p))
        r = s.radius
        return [x - r for x in lo], [x + r for x in hi], r

    if not shapes:
        raise ValueError("a collision needs at least one shape")
    lst(shapes)

    def pad(buf, with_):
        while len(buf) % 16:
            buf.append(with_)

    names = bytearray()
    name_at = {}
    for sig, n in _CLASSES:
        names += struct.pack("<I", sig) + b"\x09"
        name_at[n] = len(names)
        names += n.encode() + b"\0"
    pad(names, 0xFF)
    pad(d, 0)
    data_len = len(d)
    for f, t in locs:
        d += struct.pack("<2i", f, t)
    pad(d, 0xFF)
    glob_at = len(d)
    for f, t in globs:
        d += struct.pack("<3i", f, 2, t)
    pad(d, 0xFF)
    virt_at = len(d)
    for a, n in objs:
        d += struct.pack("<3i", a, 0, name_at[n])
    pad(d, 0xFF)
    end = len(d)
    names_at = 0xD0
    data_at = names_at + len(names)
    p = bytearray(struct.pack("<2I2i", _HK0, _HK1, 0, 8) + bytes([4, 1, 0, 1]) + struct.pack("<3i2i", 3, 2, 0, 0, name_at["hkpListShape"]))
    p += b"hk_2010.2.0-r1".ljust(15, b"\0") + b"\xff" + struct.pack("<2i", 0, -1)

    def section(name, start, a, b_, c, e):
        nm = bytearray(name.encode().ljust(19, b"\0")) + b"\xff"
        return nm + struct.pack("<7i", start, a, b_, c, e, e, e)

    p += section("__classnames__", names_at, len(names), len(names), len(names), len(names))
    p += section("__types__", data_at, 0, 0, 0, 0)
    p += section("__data__", data_at, data_len, glob_at, virt_at, end)
    p += names + d
    return b"MCKH" + struct.pack("<3i", 5, 16 + len(p), len(shapes)) + bytes(p)


def convex_from_faces(points, faces, radius=0.05):
    """A convex shape from a closed convex mesh (its points and faces, each wound outward): the faces'
    planes, coplanar ones merged, and the corners they use."""
    planes = []
    for f in faces:
        a, b, c = (points[i] for i in f[:3])
        u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        ln = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
        if ln < 1e-12:
            continue
        n = (n[0] / ln, n[1] / ln, n[2] / ln)
        d = -(n[0] * a[0] + n[1] * a[1] + n[2] * a[2])
        if any(n[0] * q[0] + n[1] * q[1] + n[2] * q[2] > 0.99995 and abs(d - q[3]) < 1e-4 for q in planes):
            continue
        planes.append((n[0], n[1], n[2], d))
    used = sorted({i for f in faces for i in f})
    corners = [tuple(points[i]) for i in used]
    if len(planes) < 4 or len(corners) < 4:
        raise ValueError("a collision hull has no volume")
    return Convex(corners, planes, radius)


def hull(points, radius=0.05):
    """A convex shape round these points (a convex hull, by gift wrapping its faces): corners and
    planes. Points closer than a millimetre count as one."""
    pts = []
    for p in points:
        if all(sum((p[a] - q[a]) ** 2 for a in range(3)) > 1e-6 for q in pts):
            pts.append(tuple(p))
    if len(pts) < 4:
        raise ValueError("a collision hull needs at least 4 separate points")
    n = len(pts)

    def sub(a, b):
        return (a[0] - b[0], a[1] - b[1], a[2] - b[2])

    def cross(a, b):
        return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])

    def dot(a, b):
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]

    # Brute force over point triples is fine for hulls of the sizes made by hand (up to ~100 points).
    if n > 96:
        raise ValueError(f"a collision hull of {n} points is too many here (96 at most): simplify it")
    scale = max(max(abs(c) for c in p) for p in pts) or 1.0
    eps = 1e-5 * scale
    planes = []
    for i in range(n):
        for j in range(i + 1, n):
            for k in range(j + 1, n):
                nrm = cross(sub(pts[j], pts[i]), sub(pts[k], pts[i]))
                ln = math.sqrt(dot(nrm, nrm))
                if ln < 1e-9 * scale * scale:
                    continue
                nrm = (nrm[0] / ln, nrm[1] / ln, nrm[2] / ln)
                d0 = dot(nrm, pts[i])
                side = [dot(nrm, p) - d0 for p in pts]
                if all(s <= eps for s in side):
                    pass
                elif all(s >= -eps for s in side):
                    nrm, d0 = (-nrm[0], -nrm[1], -nrm[2]), -d0
                else:
                    continue
                if any(nrm[0] * q[0] + nrm[1] * q[1] + nrm[2] * q[2] > 0.99995 and abs(-d0 - q[3]) < eps for q in planes):
                    continue
                planes.append((nrm[0], nrm[1], nrm[2], -d0))
    corners = [p for p in pts if any(abs(dot(pl[:3], p) + pl[3]) <= eps for pl in planes)]
    corners = [p for p in corners if sum(1 for pl in planes if abs(dot(pl[:3], p) + pl[3]) <= eps) >= 3]
    if len(planes) < 4:
        raise ValueError("a collision hull has no volume (its points lie in a plane)")
    return Convex(corners, planes, radius)


# ---------------------------------------------------------------- the model


class Tag:
    __slots__ = ("hash", "rotation", "position", "at")

    def __init__(self, h, rotation, position, at):
        self.hash, self.rotation, self.position, self.at = h, rotation, position, at

    @property
    def name(self):
        return tag_name(self.hash)


class Part:
    """A part. Its placement is from its parent part's, except a suspension piston's (a part with
    piston segments, +0x80): the LAVs' pistons name the wheel hub they push on as their parent, but
    sit where they are in the vehicle's own space (the game stretches them between the two)."""
    __slots__ = ("index", "name", "type", "flags", "parent", "rotation", "position", "shape", "submesh", "tags", "at",
                 "vehicle_space")


class Submesh:
    """The triangles of one submesh: (material, [(a, b, c) vertex numbers into its own vertices])."""

    def __init__(self):
        self.blocks = []
        self.positions = []
        self.normals = []
        self.tangents = []   # (x, y, z, w) raw bytes' meaning: -1..1, w kept as a byte
        self.uvs = []        # first set, as the file has it (v down from the texture's top)
        self.uvs2 = []


class VehicleModel:
    """A vehicle's c-file and g-file, parsed (see the module's description)."""

    def __init__(self, c, g):
        self.c = bytearray(c)
        self.g = bytes(g)
        h = self.c
        if len(h) < HEADER or _i(h, 0) != VERSION:
            raise ValueError("not a vehicle model (version 30)")
        self.mesh_at = _i(h, 0x24)
        self.material_count = _i(h, 0x30)
        self.part_count = _i(h, 0x144)
        self.parts = []
        for p in range(self.part_count):
            r = HEADER + p * PART
            pt = Part()
            pt.index, pt.at = p, r
            pt.name = rf._cstr(h, r + 0x4C)
            pt.type = struct.unpack_from("<I", h, r)[0]
            pt.flags = _i(h, r + 0xC)
            pt.parent = _i(h, r + 0x10)
            pt.rotation = struct.unpack_from("<9f", h, r + 0x1C)
            pt.position = struct.unpack_from("<3f", h, r + 0x40)
            pt.shape = _i(h, r + 0x74)
            pt.submesh = _i(h, r + 0xCC)
            pt.tags = self._tags(_i(h, r + 0x78))
            pt.vehicle_space = _i(h, r + 0x80) > 0
            self.parts.append(pt)
        self.chassis_tags = self._tags(_i(h, 0xD4))
        self.materials = self._materials()
        self.shapes = read_collision(h, _i(h, 0x10))
        self.shapes_changed = False
        self.submeshes = None  # read on asking (read_mesh)

    def segment_submeshes(self):
        """The piston segments' submeshes (as stored), in part order."""
        h, out = self.c, []
        for p in self.parts:
            n, at = _i(h, p.at + 0x80), _i(h, p.at + 0x84)
            out += [_i(h, at + 60 * k) for k in range(max(0, n))]
        return out

    def submesh_order(self, count=None):
        """For each submesh number a part (or light) gives, the submesh stored under it.

        Parts number the submeshes as if the piston segments' came right after the pistons' own
        (the LAVs: pistons 1-6, their 12 segments 7-18, the swing arms from 19), while the file keeps
        the segments' after the other parts' (the LAVs': 41-52, the swing arms 7-12; the segment
        records give those stored numbers). Vehicles whose pistons are their last parts (the C-Tran)
        number them the same either way. Checked on every vehicle with pistons: each part's mesh
        then fits it (the LAVs' and the tank's wheels, windscreen, turret, hatches)."""
        if count is None:
            count = _i(self.c, self.mesh_at + 0x14)
        segs = [s for s in self.segment_submeshes() if 0 <= s < count]
        if not segs:
            return list(range(count))
        pistons = [p.submesh for p in self.parts if _i(self.c, p.at + 0x80) > 0 and p.submesh >= 0]
        last = max(pistons) if pistons else 0
        taken = set(segs)
        others = [s for s in range(count) if s not in taken]
        return others[:last + 1] + segs + others[last + 1:]

    def _tags(self, at):
        if at in (0, -1) or at + 4 > len(self.c):
            return []
        out = []
        for k in range(_i(self.c, at)):
            a = at + 4 + TAG * k
            out.append(Tag(struct.unpack_from("<I", self.c, a)[0], struct.unpack_from("<9f", self.c, a + 4),
                           struct.unpack_from("<3f", self.c, a + 40), a))
        return out

    # ---- sections

    def _names_end(self):
        """Where the texture names end: the next section after them (as GuerrillaVehicles.Following)."""
        h = self.c
        names = _i(h, 0x34)
        starts = [_i(h, 0x04), _i(h, 0x0C), _i(h, 0x10), _i(h, 0x38)]
        if _i(h, 0x134) > 0:
            starts.append(_i(h, 0x13C))
        for pt in self.parts:
            starts.append(_i(h, pt.at + 0x18))  # set even with no children: where they would be
            starts.append(_i(h, pt.at + 0xA0))
        return min(s for s in starts if s > names)

    def _moved_offsets(self):
        """Where every offset into "the rest" and the arrays after the MOPP trees is kept: (where, what)."""
        h = self.c
        out = []

        def add(at):
            v = _i(h, at)
            if v not in (0, -1):
                out.append(at)

        for a in (0x04, 0x0C, 0x38):
            add(a)
        if _i(h, 0x124) > 0:
            add(0x128)
        if _i(h, 0x12C) > 0:
            add(0x130)
        n_spl = _i(h, 0x134)
        if n_spl > 0:
            add(0x13C)
            for s in range(n_spl):
                r = _i(h, 0x13C) + 28 * s
                add(r + 0x14)
                add(r + 0x18)
        for pt in self.parts:
            add(pt.at + 0x18)
            add(pt.at + 0xA0)
        return out

    def _materials(self):
        h = bytes(self.c)
        lst = _i(h, 0x2C)
        blocks = []
        for k in range(self.material_count):
            at = _i(h, lst + 4 * k)
            blocks.append(bytes(h[at:at + _i(h, at)]))
        names = _i(h, 0x34)
        out = []
        for b in blocks:
            n = struct.unpack_from("<H", b, 16)[0]
            tex = []
            for t in range(n):
                d = 0x24 + 12 * t
                tex.append([struct.unpack_from("<I", b, d + 4)[0], rf._cstr(h, names + _i(b, d))])
            out.append({"block": b, "textures": tex, "flags": _i(b, 12)})
        return out

    # ---- the mesh

    def read_mesh(self):
        h, g, m = self.c, self.g, self.mesh_at
        nsub, table = _i(h, m + 0x14), m + _i(h, m + 0x18)
        nv, vs, uvch = _i(h, m + 0x1C), h[m + 0x20], h[m + 0x22]
        voff, ni, ioff, isz, nblk = _i(h, m + 0x3C), _i(h, m + 0x40), _i(h, m + 0x44), h[m + 0x48], struct.unpack_from("<H", h, m + 0x4A)[0]
        idx = struct.unpack_from(f"<{ni}{'H' if isz == 2 else 'I'}", g, ioff)
        blocks_at = table + 48 * nsub
        uv_at = vs - 4 * uvch
        subs, nb = [], 0
        for s in range(nsub):
            e = table + 48 * s
            sm = Submesh()
            used = {}
            for _ in range(_i(h, e)):
                r = blocks_at + 20 * nb
                nb += 1
                mat, first, count = struct.unpack_from("<H", h, r)[0], _i(h, r + 4), _i(h, r + 8)
                tris = []
                for i in range(first, first + count - 2):
                    a, b, c = idx[i], idx[i + 1], idx[i + 2]
                    if a == b or b == c or a == c:
                        continue
                    t = (a, b, c) if (i - first) % 2 == 0 else (b, a, c)
                    tris.append(tuple(used.setdefault(v, len(used)) for v in t))
                sm.blocks.append((mat, tris))
            order = sorted(used, key=used.get)
            for v in order:
                p = voff + vs * v
                sm.positions.append(struct.unpack_from("<3f", g, p))
                sm.normals.append(tuple(x / 127.5 - 1 for x in g[p + 12:p + 15]))
                sm.tangents.append(tuple(x / 127.5 - 1 for x in g[p + 16:p + 19]) + (g[p + 19],) if vs >= 24 + 4 * (uvch - 1) and uv_at >= 20 else (1, 0, 0, 255))
                sm.uvs.append((struct.unpack_from("<h", g, p + uv_at)[0] / 1024.0, struct.unpack_from("<h", g, p + uv_at + 2)[0] / 1024.0) if uvch else (0.0, 0.0))
                sm.uvs2.append((struct.unpack_from("<h", g, p + uv_at + 4)[0] / 1024.0, struct.unpack_from("<h", g, p + uv_at + 6)[0] / 1024.0) if uvch > 1 else sm.uvs[-1])
            subs.append(sm)
        self.submeshes = subs
        return subs

    # ---- writing

    def write(self, rebuild_mesh=True):
        """(c-file, g-file) with the parts, tags, materials, submeshes and shapes as they are now."""
        h = bytes(self.c)
        I = lambda a: _i(h, a)
        mesh, names, collision, mopp_table, mopp_count = I(0x24), I(0x34), I(0x10), I(0x18), I(0x14)
        names_end = self._names_end()
        coll_len = I(collision + 8)
        mopp_trees_end = max([mopp_table + 4 * mopp_count] +
                             [I(mopp_table + 4 * k) for k in range(mopp_count) if I(mopp_table + 4 * k) != -1])
        tail_start = min([a for a in (I(0x128) if I(0x124) > 0 else 0, I(0x130) if I(0x12C) > 0 else 0) if a > 0], default=len(h))
        if tail_start < mopp_trees_end:
            tail_start = len(h)
        moved = self._moved_offsets()

        out = bytearray(h[:mesh])
        self._edit_prefix(out)
        checksum = I(mesh + 4) & 0xFFFFFFFF

        # The mesh block and the g-file.
        if rebuild_mesh and self.submeshes is not None:
            block, gfile, checksum = build_mesh(self.submeshes, checksum)
        else:
            block, gfile = h[mesh:mesh + I(mesh + 8)], self.g
        out += block
        while len(out) % 16:
            out.append(0)

        # The material map, the materials' offsets, the materials, the names.
        mats = self.materials
        new_mm = len(out)
        old_mm = I(0x28)
        if len(mats) == self.material_count:  # its run-time words as they were
            out += h[old_mm:old_mm + 4 * (len(mats) + 2)]
        else:
            out += struct.pack("<2i", 0, len(mats)) + bytes(4 * len(mats))
        while len(out) % 16:
            out.append(0)
        new_ml = len(out)
        out += bytes(4 * len(mats))
        while len(out) % 16:
            out.append(0)
        names_blob, name_at = bytearray(), {}

        def name_offset(n):
            if n.lower() not in name_at:
                name_at[n.lower()] = len(names_blob)
                names_blob.extend(n.encode("latin-1") + b"\0\0")
                if len(names_blob) % 2:
                    names_blob.append(0)
            return name_at[n.lower()]

        for k, m in enumerate(mats):
            b = bytearray(m["block"])
            for t, (slot, tex) in enumerate(m["textures"]):
                d = 0x24 + 12 * t
                struct.pack_into("<I", b, d + 4, slot)
                struct.pack_into("<i", b, d, name_offset(tex))
            struct.pack_into("<i", b, 12, m["flags"])
            struct.pack_into("<i", out, new_ml + 4 * k, len(out))
            out += b
        while len(out) % 16:
            out.append(0)
        new_names = len(out)
        out += names_blob
        while len(out) % 16:
            out.append(0)

        # The rest, moved.
        delta_rest = len(out) - names_end
        out += h[names_end:collision]
        while len(out) % 16:
            out.append(0)

        # The collision and the MOPP table.
        new_collision = len(out)
        if self.shapes_changed:
            out += write_collision(self.shapes)
        else:
            out += h[collision:collision + coll_len]
        while len(out) % 16:
            out.append(0)
        new_mopp = len(out)
        if self.shapes_changed:
            out += struct.pack(f"<{len(self.shapes)}i", *([-1] * len(self.shapes)))
            mopp_n = len(self.shapes)
            while len(out) % 16:
                out.append(0)
            delta_tail = len(out) - tail_start
        else:
            mopp_n = mopp_count
            delta_mopp = new_mopp - mopp_table
            out += h[mopp_table:tail_start]
            for k in range(mopp_count):
                v = I(mopp_table + 4 * k)
                if v != -1:
                    struct.pack_into("<i", out, new_mopp + 4 * k, v + delta_mopp)
            delta_tail = delta_mopp
        out += h[tail_start:]

        # The offsets.
        for at in moved:
            v = I(at)
            nv = v + (delta_tail if v >= tail_start else delta_rest)
            where = at if at < mesh else at + delta_rest  # spline records live in "the rest"
            struct.pack_into("<i", out, where, nv)
        struct.pack_into("<i", out, 0x10, new_collision)
        struct.pack_into("<i", out, 0x14, mopp_n)
        struct.pack_into("<i", out, 0x18, new_mopp)
        struct.pack_into("<i", out, 0x28, new_mm)
        struct.pack_into("<i", out, 0x2C, new_ml)
        struct.pack_into("<i", out, 0x30, len(mats))
        struct.pack_into("<i", out, 0x34, new_names)
        return bytes(out), gfile

    def _edit_prefix(self, out):
        for pt in self.parts:
            r = pt.at
            struct.pack_into("<9f", out, r + 0x1C, *pt.rotation)
            struct.pack_into("<3f", out, r + 0x40, *pt.position)
            struct.pack_into("<i", out, r + 0x0C, pt.flags)
            struct.pack_into("<i", out, r + 0x74, pt.shape)
            struct.pack_into("<2i", out, r + 0xCC, pt.submesh, pt.submesh)
            for t in pt.tags:
                struct.pack_into("<I9f3f", out, t.at, t.hash, *t.rotation, *t.position)
        for t in self.chassis_tags:
            struct.pack_into("<I9f3f", out, t.at, t.hash, *t.rotation, *t.position)

    def set_shapes(self, shapes):
        """New collision shapes (the same number and order as the model's: 0 the chassis's, the
        parts' by their shape numbers). Parts whose collision was pieces of the chassis's then take
        their own shape (flag 0x20 off, no pieces)."""
        if len(shapes) != len(self.shapes):
            raise ValueError(f"the model has {len(self.shapes)} collision shapes, not {len(shapes)}")
        if not _same_shape(shapes[0], self.shapes[0]):
            for pt in self.parts:
                if pt.flags & 0x20:
                    pt.flags &= ~0x20
                    struct.pack_into("<2i", self.c, pt.at + 0xC4, 0, 0)
        self.shapes = shapes
        self.shapes_changed = True


def _same_shape(a, b):
    return a is b


# ---------------------------------------------------------------- the mesh block


def _strip(tris):
    """Triangles (a, b, c) as one strip, each starting at an even place (so it keeps its winding),
    joined by degenerate triangles: ... c', [c'], a, a, b, c."""
    s = []
    for a, b, c in tris:
        if not s:
            s = [a, b, c]
            continue
        # Carries on from the last two when the triangle shares that edge the right way round.
        i = len(s) - 2
        x, y = (s[-2], s[-1]) if i % 2 == 0 else (s[-1], s[-2])
        joined = False
        for p, q, r in ((a, b, c), (b, c, a), (c, a, b)):
            if (p, q) == (x, y):
                s.append(r)
                joined = True
                break
        if joined:
            continue
        last = s[-1]
        s.append(last)
        if len(s) % 2 == 0:  # the real triangle (a, b, c) has to start at an even place
            s.append(last)
        s += [a, a, b, c]
    return s


def _pack_byte(x):
    return max(0, min(255, int(round((x + 1.0) * 127.5))))


def build_mesh(submeshes, checksum=None):
    """A version-9 mesh block and its g-file for these submeshes (vertex format 2: 28 bytes, two UV
    sets, 16-bit strip indices when they fit)."""
    vs = 28
    verts = bytearray()
    indices = []
    blocks = []
    table = []
    base = 0
    for sm in submeshes:
        n = len(sm.positions)
        for v in range(n):
            p = sm.positions[v]
            nx, ny, nz = sm.normals[v]
            t = sm.tangents[v] if v < len(sm.tangents) else (1.0, 0.0, 0.0, 255)
            u, w = sm.uvs[v]
            u2, w2 = sm.uvs2[v] if v < len(sm.uvs2) else (u, w)
            verts += struct.pack("<3f", *p)
            verts += bytes([_pack_byte(nx), _pack_byte(ny), _pack_byte(nz), 0])
            verts += bytes([_pack_byte(t[0]), _pack_byte(t[1]), _pack_byte(t[2]), int(t[3]) & 0xFF])

            def q(x):
                return max(-32768, min(32767, int(round(x * 1024))))
            verts += struct.pack("<4h", q(u), q(w), q(u2), q(w2))
        lo = [min((p[a] for p in sm.positions), default=0.0) for a in range(3)]
        hi = [max((p[a] for p in sm.positions), default=0.0) for a in range(3)]
        count = 0
        for mat, tris in sm.blocks:
            if not tris:
                continue
            strip = [base + i for i in _strip(tris)]
            first = len(indices)
            indices += strip
            used = [base + i for t in tris for i in t]
            blocks.append((mat, first, len(strip), min(used), max(used)))
            count += 1
        table.append((count, lo, hi))
        base += n
    nv = base
    wide = nv > 0xFFFF
    isz = 4 if wide else 2
    if checksum is None:
        checksum = zlib.crc32(bytes(verts)) & 0xFFFFFFFF
    g = bytearray(struct.pack("<I", checksum) + bytes(12))
    ioff = len(g)
    g += struct.pack(f"<{len(indices)}{'I' if wide else 'H'}", *indices)
    while len(g) % 16:
        g.append(0)
    voff = len(g)
    g += verts
    g += struct.pack("<I", checksum)

    m = bytearray(0x60)
    struct.pack_into("<4I", m, 0, 9, checksum, 0, len(g))
    struct.pack_into("<4I", m, 0x10, 1, len(table), 0x60, nv)
    m[0x20], m[0x21], m[0x22], m[0x23] = vs, 2, 2, 0
    struct.pack_into("<6f", m, 0x24, 1, 1, 1, 0, 0, 0)
    struct.pack_into("<3I", m, 0x3C, voff, len(indices), ioff)
    m[0x48], m[0x49] = isz, 0
    struct.pack_into("<H", m, 0x4A, len(blocks))
    for count, lo, hi in table:
        m += struct.pack("<I3f6f", count, 0, 0, 0, *lo, *hi) + bytes(8)
    for mat, first, n, vmin, vmax in blocks:
        m += struct.pack("<HH4I", mat, 0, first, n, vmin, vmax)
    m += bytes(8) + struct.pack("<I", checksum)
    struct.pack_into("<I", m, 8, len(m))
    return bytes(m), bytes(g), checksum


# ---------------------------------------------------------------- containers and asset lists


def write_container(files):
    """A .str2_pc container of these (name, bytes), packed as the game's own (flags 0x4803: one zlib
    stream, sync-flushed after each file, never finished)."""
    names, directory = bytearray(), bytearray()
    size = 0
    for name, data in files:
        directory += struct.pack("<6I", len(names), 0, size, len(data), 0xFFFFFFFF, 0)
        names += name.encode("latin-1") + b"\0"
        size += len(data)
    comp = zlib.compressobj(9)
    block = bytearray()
    for i, (_, data) in enumerate(files):
        before = len(block)
        block += comp.compress(data)
        block += comp.flush(zlib.Z_SYNC_FLUSH)
        struct.pack_into("<I", directory, 24 * i + 16, len(block) - before)
    header = bytearray(0x800)
    struct.pack_into("<2I", header, 0, 0x51890ACE, 6)
    struct.pack_into("<I", header, 0x14C, 0x4803)
    names_start = 0x800 + _align(len(directory), 0x800)
    data_start = names_start + _align(len(names), 0x800)
    out = bytearray(data_start + len(block))
    struct.pack_into("<6I", header, 0x154, len(files), len(out), len(directory), len(names), size, len(block))
    out[0:0x800] = header
    out[0x800:0x800 + len(directory)] = directory
    out[names_start:names_start + len(names)] = names
    out[data_start:] = block
    return bytes(out), data_start, len(block)


def read_asm(data):
    """An asset list (.asm_pc) as a list of containers: dicts with name, type, flags, data_offset,
    compressed_size, sizes, primitives (dicts: name, type, allocator, flags, split, header_size, data_size)."""
    if _i(data, 0) != -0x41101113 and struct.unpack_from("<I", data, 0)[0] != 0xBEEFFEED:
        raise ValueError("not an asset list")
    out = []
    at = 8
    for _ in range(struct.unpack_from("<H", data, 6)[0]):
        n = struct.unpack_from("<H", data, at)[0]
        c = {"name": data[at + 2:at + 2 + n].decode("latin-1")}
        at += 2 + n
        c["type"], c["flags"], np_, c["data_offset"], ns, c["compressed_size"] = struct.unpack_from("<BHHIII", data, at)
        at += 17
        c["sizes"] = list(struct.unpack_from(f"<{ns}i", data, at))
        at += 4 * ns
        prims = []
        for _ in range(np_):
            n = struct.unpack_from("<H", data, at)[0]
            p = {"name": data[at + 2:at + 2 + n].decode("latin-1")}
            at += 2 + n
            p["type"], p["allocator"], p["flags"], p["split"], p["header_size"], p["data_size"] = struct.unpack_from("<4B2i", data, at)
            at += 12
            prims.append(p)
        c["primitives"] = prims
        out.append(c)
    if at != len(data):
        raise ValueError("the asset list has data after its last container")
    return out


def write_asm(containers):
    out = bytearray(struct.pack("<IHH", 0xBEEFFEED, 5, len(containers)))
    for c in containers:
        n = c["name"].encode("latin-1")
        out += struct.pack("<H", len(n)) + n
        out += struct.pack("<BHHIII", c["type"], c["flags"], len(c["primitives"]), c["data_offset"], len(c["sizes"]), c["compressed_size"])
        out += struct.pack(f"<{len(c['sizes'])}i", *c["sizes"])
        for p in c["primitives"]:
            n = p["name"].encode("latin-1")
            out += struct.pack("<H", len(n)) + n
            out += struct.pack("<4B2i", p["type"], p["allocator"], p["flags"], p["split"], p["header_size"], p["data_size"])
    return bytes(out)


def asm_entry(template, name, container_info, files):
    """The asset list entry for a container made from a template's: the template's entry (its type
    and flags) with this container's name, data offset, compressed size, file sizes and primitives
    (the template's own, by kind, renamed to the files that replaced them)."""
    _, data_offset, compressed = container_info
    sizes = {f.lower(): len(d) for f, d in files}
    prims = []
    for f, d in files:
        low = f.lower()
        ext = low[low.rfind("."):]
        typ = {".ccar_pc": 11, ".cpeg_pc": 1, ".cvbm_pc": 1, ".anim_pc": 16}.get(ext)
        if typ is None:
            keep = next((p for p in template["primitives"] if p["name"].lower() == low), None)
            if keep is not None:
                prims.append(dict(keep, header_size=len(d)))
            continue
        if typ == 16:
            prims.append({"name": f, "type": 16, "allocator": 0, "flags": 0, "split": 0, "header_size": len(d), "data_size": 0})
            continue
        g = "." + "g" + ext[2:]
        data_size = sizes.get(low[: -len(ext)] + g, 0)
        like = next((p for p in template["primitives"] if p["type"] == typ), None)
        base = dict(like) if like else {"allocator": 0, "flags": 4, "split": 0}
        base.update({"name": f, "type": typ, "header_size": len(d), "data_size": data_size})
        prims.append(base)
    return {"name": name, "type": template["type"], "flags": template["flags"], "data_offset": data_offset,
            "compressed_size": compressed, "sizes": [len(d) for _, d in files], "primitives": prims}


# ---------------------------------------------------------------- vehicles made from nothing

SCRATCH_DONOR = "mar_combug_1"  # a buggy: wheels and plain pieces, no doors, no pistons, no treads
ROLE_FRONT_WHEEL = 0x0A48F662   # the part types (hashes) the game knows wheels by: the first axle's
ROLE_REAR_WHEEL = 0x6A42CA04    # (front, steering) and the second's
ROLE_PIECE = 0x6CA328EE         # a plain piece of the vehicle


class ScratchPart:
    """A part of a vehicle made from nothing: role ("front wheel", "rear wheel" or "piece"), its name,
    its parent (an index into the list, or -1), its placement (rotation rows, position: from the
    parent's), its submesh (Submesh or None), its collision (a Convex, a ShapeList or None) and its
    points ([(name hash, rotation rows, position)])."""

    def __init__(self, role, name, parent=-1, rotation=(1, 0, 0, 0, 1, 0, 0, 0, 1), position=(0, 0, 0),
                 submesh=None, shape=None, tags=()):
        self.role, self.name, self.parent = role, name, parent
        self.rotation, self.position = tuple(rotation), tuple(position)
        self.submesh, self.shape, self.tags = submesh, shape, list(tags)


def _role_record(donor, role):
    """The donor's part record a part of that role is made from."""
    want = {"front wheel": ROLE_FRONT_WHEEL, "rear wheel": ROLE_REAR_WHEEL, "piece": ROLE_PIECE}[role]
    best = None
    for p in donor.parts:
        if p.type != want:
            continue
        # a plain one: no parent, not pieces of the chassis, no children
        if p.parent < 0 and not p.flags & 0x20 and _i(donor.c, p.at + 0x14) == 0:
            return bytes(donor.c[p.at:p.at + PART])
        best = best or bytes(donor.c[p.at:p.at + PART])
    if best is None:
        raise ValueError(f"the donor has no {role}")
    return best


def compose(donor, body, chassis_shapes, chassis_tags, parts, materials, seats=1):
    """A vehicle model (c-file, g-file) made from nothing but the donor's settings: the header's
    physics, the records of its parts by role and its tag deformation records are the donor's; the
    rest is these: body (Submesh: submesh 0), chassis_shapes (the chassis's convex shapes),
    chassis_tags ([(name hash, rotation rows, position)]), parts (ScratchPart), materials (as
    VehicleModel.materials), seats (each without a door). No lights, doors, pistons or treads."""
    d = bytes(donor.c)
    if not parts:
        raise ValueError("a vehicle needs at least one part (its wheels)")
    n = len(parts)
    out = bytearray(d[:HEADER])

    def put(a, fmt, *v):
        struct.pack_into(fmt, out, a, *v)

    # The parts' records, then their (and the chassis's) points.
    for k, p in enumerate(parts):
        rec = bytearray(_role_record(donor, p.role))
        struct.pack_into("<i", rec, 0x0C, _i(rec, 0x0C) & ~0x20)
        struct.pack_into("<i", rec, 0x10, p.parent)
        struct.pack_into("<i", rec, 0x14, 0)
        struct.pack_into("<9f", rec, 0x1C, *p.rotation)
        struct.pack_into("<3f", rec, 0x40, *p.position)
        rec[0x4C:0x74] = p.name.encode("latin-1")[:39].ljust(40, b"\0")
        struct.pack_into("<i", rec, 0x74, k + 1)  # its collision shape: 0 is the chassis's
        struct.pack_into("<2i", rec, 0x80, 0, 0)
        struct.pack_into("<2i", rec, 0x88, 0, -1)
        struct.pack_into("<4i", rec, 0x90, 0, -1, 0, -1)
        struct.pack_into("<i", rec, 0xA0, -1)
        struct.pack_into("<i", rec, 0xC0, -1)
        struct.pack_into("<2i", rec, 0xC4, 0, 0)
        out += rec
    tags_at = {}

    def tag_list(key, tags):
        tags_at[key] = len(out)
        out.extend(struct.pack("<i", len(tags)))
        for h, rot, pos in tags:
            out.extend(struct.pack("<I9f3f", h, *rot, *pos))

    tag_list("chassis", chassis_tags)
    for k, p in enumerate(parts):
        tag_list(k, p.tags)
    lights_at = len(out)
    while len(out) % 128:
        out.append(0)
    for k in range(n):
        put(HEADER + k * PART + 0x78, "<i", tags_at[k])
    put(0xD4, "<i", tags_at["chassis"])
    put(0x11C, "<2i", 0, lights_at)

    # The meshes: 0 the body, then each part's that has one.
    submeshes = [body]
    for k, p in enumerate(parts):
        s = -1
        if p.submesh is not None and p.submesh.positions:
            s = len(submeshes)
            submeshes.append(p.submesh)
        put(HEADER + k * PART + 0xCC, "<2i", s, s)
    mesh_at = len(out)
    block, gfile, _ = build_mesh(submeshes)
    out += block
    while len(out) % 16:
        out.append(0)

    # Materials (their run-time map, offsets, blocks) and texture names.
    mm_at = len(out)
    out += struct.pack("<2i", 0, len(materials)) + bytes(4 * len(materials))
    while len(out) % 16:
        out.append(0)
    ml_at = len(out)
    out += bytes(4 * len(materials))
    while len(out) % 16:
        out.append(0)
    names_blob, name_at = bytearray(), {}

    def name_offset(nm):
        if nm.lower() not in name_at:
            name_at[nm.lower()] = len(names_blob)
            names_blob.extend(nm.encode("latin-1") + b"\0\0")
            if len(names_blob) % 2:
                names_blob.append(0)
        return name_at[nm.lower()]

    for k, m in enumerate(materials):
        b = bytearray(m["block"])
        for t, (slot, tex) in enumerate(m["textures"]):
            struct.pack_into("<Ii", b, 0x24 + 12 * t + 4, slot, 0)
            struct.pack_into("<i", b, 0x24 + 12 * t, name_offset(tex))
        struct.pack_into("<i", b, 12, m["flags"])
        struct.pack_into("<i", out, ml_at + 4 * k, len(out))
        out += b
    while len(out) % 16:
        out.append(0)
    names_at = len(out)
    out += names_blob
    while len(out) % 16:
        out.append(0)

    # The rest: children lists, the seats' doors (none), the tag deformation records (the donor's).
    rest_at = len(out)
    children = {}
    for k, p in enumerate(parts):
        if p.parent >= 0:
            children.setdefault(p.parent, []).append(k)
    for k in range(n):
        kids = children.get(k, [])
        put(HEADER + k * PART + 0x14, "<2i", len(kids), len(out))
        out += struct.pack(f"<{len(kids)}i", *kids)
    while len(out) % 16:
        out.append(0)
    seats = max(1, min(int(seats), 16))
    seats_at = len(out)
    out += struct.pack(f"<i{seats}i", seats, *([-1] * seats))
    while len(out) % 16:
        out.append(0)
    tagdef_count, tagdef_at = _i(d, 0x08), _i(d, 0x0C)
    tagdef_new = len(out)
    out += d[tagdef_at:tagdef_at + 28 * tagdef_count]
    while len(out) % 16:
        out.append(0)

    # The collision and its (empty) MOPP table, then the bounding box record.
    shapes = [ShapeList(list(chassis_shapes)) if chassis_shapes else None]
    for p in parts:
        shapes.append(p.shape)
    fallback = Convex([(x, y, z) for x in (-0.1, 0.1) for y in (-0.1, 0.1) for z in (-0.1, 0.1)],
                      [(1, 0, 0, -0.1), (-1, 0, 0, -0.1), (0, 1, 0, -0.1), (0, -1, 0, -0.1), (0, 0, 1, -0.1), (0, 0, -1, -0.1)])
    shapes = [s if s is not None else fallback for s in shapes]
    collision_at = len(out)
    out += write_collision(shapes)
    while len(out) % 16:
        out.append(0)
    mopp_at = len(out)
    out += struct.pack(f"<{len(shapes)}i", *([-1] * len(shapes)))
    while len(out) % 16:
        out.append(0)
    box_at = len(out)
    lo = [min((p[a] for p in body.positions), default=-1.0) for a in range(3)]
    hi = [max((p[a] for p in body.positions), default=1.0) for a in range(3)]
    half = [(hi[a] - lo[a]) / 2 for a in range(3)]
    centre = [(hi[a] + lo[a]) / 2 for a in range(3)]
    donor_box = d[_i(d, 0x128):_i(d, 0x128) + 56] if _i(d, 0x124) > 0 else bytes(56)
    box = bytearray(donor_box)
    struct.pack_into("<6f", box, 0, -half[0], -half[1], -half[2], half[0], half[1], half[2])
    struct.pack_into("<3f", box, 36, *centre)
    out += box

    # The header.
    put(0x04, "<i", seats_at)
    put(0x08, "<2i", tagdef_count, tagdef_new)
    put(0x10, "<3i", collision_at, len(shapes), mopp_at)
    put(0x1C, "<2i", 0, 0)
    put(0x24, "<4i", mesh_at, mm_at, ml_at, len(materials))
    put(0x34, "<2i", names_at, 0)
    put(0x3C, "<2i", 0, 0)
    put(0x88, "<i", 0)
    for a in range(0x8C, 0xD4, 4):
        put(a, "<i", 0)
    # Mass properties: the donor's per volume, for the body's box; centre of mass the box's.
    dv = max(1e-6, 8 * abs(struct.unpack_from("<f", donor_box, 0)[0] * struct.unpack_from("<f", donor_box, 4)[0]
                           * struct.unpack_from("<f", donor_box, 8)[0])) if _i(d, 0x124) > 0 else 1.0
    vol = max(1e-6, 8 * half[0] * half[1] * half[2])
    put(0xE4, "<f", struct.unpack_from("<f", d, 0xE4)[0] * vol / dv)
    put(0xE8, "<3f", *centre)
    w, h, l = 2 * half[0], 2 * half[1], 2 * half[2]
    put(0xF4, "<9f", (h * h + l * l) / 12, 0, 0, 0, (w * w + l * l) / 12, 0, 0, 0, (w * w + h * h) / 12)
    put(0x118, "<i", -1)
    put(0x124, "<2i", 1, box_at)
    put(0x12C, "<2i", 0, -1)
    put(0x134, "<3i", 0, 0, -1)
    put(0x144, "<i", n)
    return bytes(out), gfile
