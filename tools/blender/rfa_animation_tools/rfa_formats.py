"""Red Faction: Armageddon's files the animation tools read and write, in plain Python (no Blender).

Ported from the RFA-Mod editor's own readers and writers (src/editor/RfaFormats: Packfile.cs,
Meshes/Rig.cs, Meshes/Animation.cs, Meshes/AnimationWriter.cs, Meshes/ModelFile.cs), which read the
files the way the game's code does. Everything here is in the game's own space: Y up, metres,
left-handed, quaternions as (x, y, z, w) turning column vectors the usual way (the files store x, y
and z negated; that is undone on reading and redone on writing). blender_io.py converts to Blender.

  Packfile   a .vpp_pc archive or a .str2_pc container (read only)
  Rig        a skeleton (.rig_pc): bones in their rest pose (no rotations), and tags
  Animation  an animation (.anim_pc, version 10): keys per bone, and the root track (the
             character's own movement)
  write_animation  an Animation back to bytes
  CharacterMesh    a character's mesh (.ccmesh_pc + .gcmesh_pc): its most detailed level, skinned,
                   and its materials (Material: the textures by slot, how it's drawn)
  PegFile    a texture file pair (.cpeg_pc + .gpeg_pc), and dds() to save one texture as a DDS
  asm_animations   the animations an asset list (.asm_pc) says each container holds
"""

import math
import struct
import zlib

# ---------------------------------------------------------------- small helpers


def _u8(b, at):
    return b[at]


def _s8(b, at):
    v = b[at]
    return v - 256 if v > 127 else v


def _u16(b, at):
    return struct.unpack_from("<H", b, at)[0]


def _s16(b, at):
    return struct.unpack_from("<h", b, at)[0]


def _u32(b, at):
    return struct.unpack_from("<I", b, at)[0]


def _s32(b, at):
    return struct.unpack_from("<i", b, at)[0]


def _f32(b, at):
    return struct.unpack_from("<f", b, at)[0]


def _cstr(b, at):
    end = b.find(b"\0", at)
    if end < 0:
        end = len(b)
    return b[at:end].decode("latin-1")


def _align(n, a):
    return (n + a - 1) // a * a


# ---------------------------------------------------------------- packfiles


class Packfile:
    """A packfile: a .vpp_pc archive or a .str2_pc container, version 6 (Armageddon) or 3 (Guerrilla).

    0x000 a 2 KB header (magic 0x51890ACE, version, flags at 0x14C, at 0x154 the file count, size,
    directory size, names size, data size, compressed data size); 0x800 the directory (24 bytes a
    file in version 6, 28 in version 3), then the names, then the data, each on a 2 KB boundary. The
    data is plain, compressed (each file its own zlib stream on a 2 KB boundary) or compressed and
    condensed (one stream; each file at its offset). The game's streams are never finished, so they
    are read by size.
    """

    MAGIC = 0x51890ACE
    COMPRESSED = 1
    CONDENSED = 2

    def __init__(self, name, data=None, path=None):
        self.name = name
        self._path = path
        self._data = data
        self._condensed = None
        header = self._read(0, 0x800)
        if len(header) < 0x800 or _u32(header, 0) != self.MAGIC:
            raise ValueError(f"{name} is not a packfile")
        self.version = _u32(header, 4)
        if self.version not in (6, 3):
            raise ValueError(f"{name} is packfile version {self.version}; only 6 and 3 are read")
        entry_size = 28 if self.version == 3 else 24
        size_at = 16 if self.version == 3 else 12
        self.flags = _u32(header, 0x14C)
        count = _u32(header, 0x154)
        directory_size = _u32(header, 0x15C)
        names_size = _u32(header, 0x160)
        self.data_size = _u32(header, 0x164)
        self.compressed_data_size = _u32(header, 0x168)
        names_start = 0x800 + _align(directory_size, 0x800)
        self.data_start = names_start + _align(names_size, 0x800)
        directory = self._read(0x800, directory_size)
        names = self._read(names_start, names_size)
        self.entries = []  # (name, offset, size, compressed size, stored at)
        stored = self.data_start
        for i in range(count):
            at = i * entry_size
            file_name = _cstr(names, _u32(directory, at))
            offset = _u32(directory, at + 8)
            size = _u32(directory, at + size_at)
            csize = _u32(directory, at + size_at + 4)
            if self.compressed and not self.condensed:
                self.entries.append((file_name, offset, size, csize, stored))
                stored += _align(csize, 0x800)
            else:
                self.entries.append((file_name, offset, size, csize, self.data_start + offset))
        self._by_name = {e[0].lower(): e for e in self.entries}

    @classmethod
    def open(cls, path):
        import os
        return cls(os.path.basename(path), path=path)

    @property
    def compressed(self):
        return bool(self.flags & self.COMPRESSED)

    @property
    def condensed(self):
        return bool(self.flags & self.CONDENSED)

    def _read(self, offset, length):
        if self._data is not None:
            return self._data[offset:offset + length]
        with open(self._path, "rb") as f:
            f.seek(offset)
            return f.read(length)

    @staticmethod
    def _inflate(data, size):
        d = zlib.decompressobj()
        out = d.decompress(data, size)
        if len(out) < size:
            raise ValueError("compressed data ended before the files it holds")
        return out[:size]

    def names(self):
        return [e[0] for e in self.entries]

    def has(self, name):
        return name.lower() in self._by_name

    def read(self, name):
        e = self._by_name.get(name.lower())
        if e is None:
            raise KeyError(f"{self.name} has no {name}")
        _, offset, size, csize, stored = e
        if not self.compressed:
            return self._read(stored, size)
        if not self.condensed:
            return self._inflate(self._read(stored, csize), size)
        if self._condensed is None:
            self._condensed = self._inflate(self._read(self.data_start, self.compressed_data_size), self.data_size)
        return self._condensed[offset:offset + size]

    def container(self, name):
        """A .str2_pc container inside this archive, opened as a packfile."""
        return Packfile(name, data=self.read(name))


# ---------------------------------------------------------------- rigs


class RigBone:
    __slots__ = ("name", "parent", "position", "offset")

    def __init__(self, name, parent, position, offset):
        self.name, self.parent, self.position, self.offset = name, parent, position, offset


class RigTag:
    __slots__ = ("name", "bone", "rotation", "position")

    def __init__(self, name, bone, rotation, position):
        self.name, self.bone, self.rotation, self.position = name, bone, rotation, position


class Rig:
    """A skeleton (.rig_pc). 0x24 bone count, 0x30 tag count; 0x40 a hash per bone; the bones (36
    bytes: name offset, rest position negated, offset from the parent, parent, -1); the tags (60
    bytes: name offset, rotation rows right/up/forward, position, bone, -1); the names. A bone's
    position is its parent's plus its offset; the rest pose has no rotations.
    """

    def __init__(self, bones, tags):
        self.bones = bones
        self.tags = tags

    @classmethod
    def read(cls, data):
        if len(data) < 0x40:
            raise ValueError("too short for a rig")
        count, tag_count = _s32(data, 0x24), _s32(data, 0x30)
        if not (0 < count <= 4096) or not (0 <= tag_count <= 4096):
            raise ValueError("not a rig (bone or tag count out of range)")
        bones_at = 0x40 + 4 * count
        tags_at = bones_at + 36 * count
        names_at = tags_at + 60 * tag_count
        if names_at > len(data):
            raise ValueError("the rig's bones and tags run past its end")

        def name(off):
            return _cstr(data, names_at + off) if names_at + off < len(data) else ""

        def vec(at):
            return struct.unpack_from("<3f", data, at)

        bones = []
        for i in range(count):
            at = bones_at + 36 * i
            parent = _s32(data, at + 28)
            if parent >= i or parent < -1:
                raise ValueError(f"bone {i}'s parent ({parent}) isn't an earlier bone")
            p = vec(at + 4)
            bones.append(RigBone(name(_u32(data, at)), parent, (-p[0], -p[1], -p[2]), vec(at + 16)))
        tags = []
        for i in range(tag_count):
            at = tags_at + 60 * i
            rot = struct.unpack_from("<9f", data, at + 4)
            tags.append(RigTag(name(_u32(data, at)), _s32(data, at + 52), rot, vec(at + 40)))
        return cls(bones, tags)


# ---------------------------------------------------------------- animations

FRAMES_PER_SECOND = 60

WIDE_TIMES, EXTENSION, SCALE_KEYS, WEIGHT_CURVES, WIDE_COUNTS = 0x02, 0x10, 0x20, 0x40, 0x80


class Track:
    """One bone's keys: rotations (x, y, z, w) in its parent's space, positions added to its rest
    offset from its parent, scales, and a weight curve; each a list of (frame, value)."""

    __slots__ = ("bone", "rotations", "positions", "scales", "weights")

    def __init__(self, bone, rotations=None, positions=None, scales=None, weights=None):
        self.bone = bone
        self.rotations = rotations or []
        self.positions = positions or []
        self.scales = scales or []
        self.weights = weights or []


class Animation:
    """An animation (.anim_pc, version 10).

    Header: 0x00 "ANIM", 0x04 version 10, 0x05 flags (0x02 key times u16, else u8; 0x10 an 8-byte
    extension at 0x30; 0x20 scale keys; 0x40 weight curves; 0x80 key counts u16, else u8), 0x06
    length in frames (u16; 60 a second), 0x08 two bytes not yet known (kept), 0x0A how many bones it
    animates, 0x0B how many bones the skeleton has, 0x0C the turn over the clip (a quaternion), 0x1C
    the character's movement over the clip, 0x28 where the list of animated bones starts (0 when it
    animates every bone in order), 0x2C where the root track starts.
    """

    def __init__(self, frames, skeleton_bones, tracks, root, header=None):
        self.frames = frames
        self.skeleton_bones = skeleton_bones
        self.tracks = tracks
        self.root = root
        self.header = bytes(header) if header else None  # the first 0x30 bytes, when read from a file
        self.guerrilla = False  # read in Guerrilla's layout (written back in Armageddon's)

    @property
    def movement(self):
        return struct.unpack_from("<3f", self.header, 0x1C) if self.header else (0.0, 0.0, 0.0)

    @property
    def turn(self):
        return struct.unpack_from("<4f", self.header, 0x0C) if self.header else (0.0, 0.0, 0.0, 1.0)

    @classmethod
    def read(cls, data, guerrilla=None):
        """An animation; with guerrilla, one of Red Faction: Guerrilla's (5-byte rotation runs, 4-byte keys).
        Left out, Armageddon's layout is tried first, then Guerrilla's (a few of the game's own files,
        such as the heavy walker's plyr_wklr_dr_enter, were left in Guerrilla's)."""
        if guerrilla is None:
            try:
                return cls._read(data, False)
            except (ValueError, struct.error, IndexError) as first:
                try:
                    return cls._read(data, True)
                except (ValueError, struct.error, IndexError):
                    raise ValueError(str(first)) from None
        return cls._read(data, guerrilla)

    @classmethod
    def _read(cls, data, guerrilla):
        if len(data) < 0x30 or data[:4] != b"ANIM":
            raise ValueError('not an animation (no "ANIM" at the start)')
        if data[4] != 10:
            raise ValueError(f"animation version {data[4]}; only version 10 is known")
        flags = data[5]
        frames = _u16(data, 6)
        animated, skeleton = data[0xA], data[0xB]
        bones_at, root_at = _s32(data, 0x28), _s32(data, 0x2C)
        keys_at = bones_at + animated if bones_at else (0x38 if flags & EXTENSION else 0x30)
        if bones_at < 0 or 0 < bones_at < 0x30 or keys_at > len(data) or root_at < keys_at or root_at > len(data):
            raise ValueError("the animation's bone list or root track is outside the file")
        r = _Reader(data, flags, guerrilla)
        r.at = keys_at
        weights = r.weights(animated)
        tracks = [r.block(data[bones_at + i] if bones_at else i, weights[i]) for i in range(animated)]
        if r.at != root_at:
            raise ValueError(f"the bones' keys end at 0x{r.at:X}, not where the root track starts (0x{root_at:X})")
        r.at = root_at
        root = r.block(-1, r.weights(1)[0])
        if r.at != len(data):
            raise ValueError(f"the root track ends at 0x{r.at:X}, not at the end of the file (0x{len(data):X})")
        anim = cls(frames, skeleton, tracks, root, data[:0x30])
        anim.guerrilla = guerrilla
        return anim


class _Reader:
    def __init__(self, data, flags, guerrilla):
        self.d, self.flags, self.guerrilla, self.at = data, flags, guerrilla, 0

    def align(self, n):
        self.at = _align(self.at, n)

    def count(self):
        if self.flags & WIDE_COUNTS:
            v = _u16(self.d, self.at)
            self.at += 2
            return v
        v = self.d[self.at]
        self.at += 1
        return v

    def times(self, n):
        wide = self.flags & WIDE_TIMES
        if wide:
            self.align(2)
            t = list(struct.unpack_from(f"<{n}H", self.d, self.at)) if n else []
            self.at += 2 * n
        else:
            t = list(self.d[self.at:self.at + n])
            self.at += n
        return t

    def weights(self, tracks):
        curves = [[] for _ in range(tracks)]
        if not self.flags & WEIGHT_CURVES:
            return curves
        counts = [self.count() for _ in range(tracks)]  # the game doesn't align these
        total = sum(counts)
        self.align(4)
        values_at = self.at
        self.at += 4 * total
        times = self.times(total)
        k = 0
        for i in range(tracks):
            for _ in range(counts[i]):
                curves[i].append((times[k], _f32(self.d, values_at + 4 * k)))
                k += 1
        return curves

    def block(self, bone, weights):
        d = self.d
        if self.flags & WIDE_COUNTS:
            self.align(2)
        nrot, npos = self.count(), self.count()
        nscale = self.count() if self.flags & SCALE_KEYS else 0

        run_size, key_size = (5, 4) if self.guerrilla else (4, 3)
        runs_at = self.at
        covered = 0
        while covered < nrot:
            covered += (d[self.at + run_size - 1] & 0x0F) + 1
            self.at += run_size
        keys_at = self.at
        self.at += nrot * key_size
        rots = (_guerrilla_rotations if self.guerrilla else _rotations)(d, runs_at, keys_at, nrot)
        rot_times = self.times(nrot)

        self.align(2)
        starts = []
        covered = 0
        while covered < npos:
            n = _u16(d, self.at)
            starts.append((n, (_s16(d, self.at + 2) / 64.0, _s16(d, self.at + 4) / 64.0, _s16(d, self.at + 6) / 64.0)))
            covered += n
            self.at += 8
        positions = []
        k = 0
        for n, s in starts:
            for _ in range(n):
                if k >= npos:
                    break
                a = self.at + 3 * k
                positions.append((s[0] + _s8(d, a) / 4000.0, s[1] + _s8(d, a + 1) / 4000.0, s[2] + _s8(d, a + 2) / 4000.0))
                k += 1
        self.at += 3 * npos
        pos_times = self.times(npos)

        scales = []
        if nscale:
            self.align(4)
            for i in range(nscale):
                scales.append(struct.unpack_from("<3f", d, self.at + 12 * i))
            self.at += 12 * nscale
        scale_times = self.times(nscale) if nscale else []
        return Track(bone, list(zip(rot_times, rots)), list(zip(pos_times, positions)), list(zip(scale_times, scales)), weights)


def _rotations(d, runs_at, keys_at, count):
    """Rotation keys as the game decodes them (0xD87AC0): runs of 4 bytes (each axis's start, × 256,
    and whether it takes big steps; the run's key count − 1, w's sign, the doubled steps), keys of 3
    signed bytes, the steps each axis moves from the run's previous key; an axis is (start × 256 +
    steps × step) over 16383, w makes the length 1. Stored x, y, z are negated to turn the usual way."""
    out = []
    key = 0
    run = runs_at
    while key < count:
        hdr = d[run + 3]
        in_run = (hdr & 0x0F) + 1
        neg_w = bool(hdr & 0x10)
        start, step = [], []
        for axis in range(3):
            b = d[run + axis]
            s = b & 0x7F
            start.append(s - 128 if s & 0x40 else s)
            big, doubled = bool(b & 0x80), bool(hdr & (0x80 >> axis))
            step.append((32 if doubled else 4) if big else (2 if doubled else 1))
        steps = [0, 0, 0]
        k = 0
        while k < in_run and key < count:
            v = []
            for axis in range(3):
                steps[axis] += _s8(d, keys_at + key * 3 + axis)
                raw = (start[axis] * 256 + steps[axis] * step[axis]) & 0xFFFF
                if raw >= 0x8000:
                    raw -= 0x10000
                v.append(raw / 16383.0)
            ls = min(1.0, v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
            w = math.sqrt(1.0 - ls)
            out.append((-v[0], -v[1], -v[2], -w if neg_w else w))
            k += 1
            key += 1
        run += 4
    return out


def _guerrilla_rotations(d, runs_at, keys_at, count):
    out = []
    key = 0
    run = runs_at
    while key < count:
        hdr = d[run + 4]
        in_run = (hdr & 0x0F) + 1
        start, step = [], []
        for axis in range(4):
            b = d[run + axis]
            s = b & 0x7F
            start.append(s - 128 if s & 0x40 else s)
            big, doubled = bool(b & 0x80), bool(hdr & (0x80 >> axis))
            step.append((32 if doubled else 4) if big else (2 if doubled else 1))
        steps = [0, 0, 0, 0]
        k = 0
        while k < in_run and key < count:
            v = []
            for axis in range(4):
                steps[axis] += _s8(d, keys_at + key * 4 + axis)
                raw = (start[axis] * 256 + steps[axis] * step[axis]) & 0xFFFF
                if raw >= 0x8000:
                    raw -= 0x10000
                v.append(raw / 16383.0)
            q = (-v[0], -v[1], -v[2], v[3])
            n = math.sqrt(sum(c * c for c in q)) or 1.0
            out.append(tuple(c / n for c in q))
            k += 1
            key += 1
        run += 5
    return out


# ---------------------------------------------------------------- writing animations

_ROTATION_SCALE = 16383
_STEPS = (1, 2, 4, 32)


def default_header(movement=(0.0, 0.0, 0.0), turn=(0.0, 0.0, 0.0, 1.0), unknown08=(3, 3)):
    """A header for an animation made from nothing: 0x08 as the game's own animations mostly have it,
    the turn and the movement over the clip."""
    h = bytearray(0x30)
    h[0:4] = b"ANIM"
    h[4] = 10
    h[8], h[9] = unknown08
    struct.pack_into("<4f", h, 0x0C, *turn)
    struct.pack_into("<3f", h, 0x1C, *movement)
    return bytes(h)


def write_animation(anim, header=None):
    """An animation's bytes, as the editor's AnimationWriter writes them: key counts and times as
    u16 (flags 0x80 and 0x02), a list of the bones animated, weight curves if any track has one,
    a block of keys per bone, then the root track. header: the first 0x30 bytes to keep (0x08, the
    turn and the movement); by default the animation's own, else default_header()."""
    tracks, root = anim.tracks, anim.root
    if len(tracks) > 255 or anim.skeleton_bones > 255:
        raise ValueError("an animation holds at most 255 bones")
    header = header or anim.header or default_header()
    scales = any(t.scales for t in tracks + [root])
    weights = any(t.weights for t in tracks + [root])
    o = bytearray(header[:0x30])
    o[0:4] = b"ANIM"
    o[4] = 10
    o[5] = WIDE_TIMES | WIDE_COUNTS | (SCALE_KEYS if scales else 0) | (WEIGHT_CURVES if weights else 0)
    struct.pack_into("<H", o, 6, anim.frames)
    o[0xA] = len(tracks)
    o[0xB] = anim.skeleton_bones
    struct.pack_into("<I", o, 0x28, 0x30)
    for t in tracks:
        if not 0 <= t.bone <= 255:
            raise ValueError(f"bone {t.bone} can't be listed")
        o.append(t.bone)
    if weights:
        _w_weights(o, tracks)
    for t in tracks:
        _w_block(o, t, scales)
    struct.pack_into("<I", o, 0x2C, len(o))
    if weights:
        _w_weights(o, [root])
    _w_block(o, root, scales)
    return bytes(o)


def _w_align(o, n):
    while len(o) % n:
        o.append(0)


def _w16(o, v):
    o += struct.pack("<H", v & 0xFFFF)


def _w_times(o, frames):
    _w_align(o, 2)
    for f in frames:
        _w16(o, f)


def _w_weights(o, tracks):
    for t in tracks:
        _w16(o, len(t.weights))
    _w_align(o, 4)
    for t in tracks:
        for _, v in t.weights:
            o += struct.pack("<f", v)
    _w_times(o, [f for t in tracks for f, _ in t.weights])


def _w_block(o, t, scales):
    _w_align(o, 2)
    _w16(o, len(t.rotations))
    _w16(o, len(t.positions))
    if scales:
        _w16(o, len(t.scales))
    _w_rotations(o, [q for _, q in t.rotations])
    _w_times(o, [f for f, _ in t.rotations])
    _w_align(o, 2)
    _w_positions(o, [p for _, p in t.positions])
    _w_times(o, [f for f, _ in t.positions])
    if t.scales:
        _w_align(o, 4)
        for _, s in t.scales:
            o += struct.pack("<3f", *s)
        _w_times(o, [f for f, _ in t.scales])


def _quantize(v):
    return max(-_ROTATION_SCALE, min(_ROTATION_SCALE, int(round(v * _ROTATION_SCALE))))


def _most_step_for(w):
    return 33 * w


def _step_for(keys, first, count, axis):
    """The smallest step (its index) the axis's moves fit in for this run, or -1; and the run's start."""
    start = max(-64, min(63, int(round(keys[first][0][axis] / 256.0))))
    smallest_w = min(1.0, min(keys[first + k][2] for k in range(count)))
    for s, step in enumerate(_STEPS):
        if count > 1 and s > 0 and step > _most_step_for(smallest_w):
            break
        fits = True
        previous = 0
        for k in range(count):
            target = int(round((keys[first + k][0][axis] - start * 256) / step))
            move = target - previous
            if move < -128 or move > 127 or abs(start * 256 + target * step) > 32767:
                fits = False
                break
            previous = target
        if fits:
            return s, start
    return -1, start


def _run_fits(keys, first, count):
    return all(_step_for(keys, first, count, a)[0] >= 0 for a in range(3))


def _w_rotations(o, rotations):
    keys = []  # (stored axes, w negative, |w|)
    prev = (0.0, 0.0, 0.0, 1.0)
    for i, q in enumerate(rotations):
        n = math.sqrt(sum(c * c for c in q)) or 1.0
        q = tuple(c / n for c in q)
        if i > 0 and sum(a * b for a, b in zip(q, prev)) < 0:
            q = tuple(-c for c in q)
        prev = q
        keys.append(([_quantize(-q[0]), _quantize(-q[1]), _quantize(-q[2])], q[3] < 0, abs(q[3])))
    runs, moves = bytearray(), bytearray()
    first = 0
    while first < len(keys):
        count = 1
        while count < 16 and first + count < len(keys) and keys[first + count][1] == keys[first][1] and _run_fits(keys, first, count + 1):
            count += 1
        hdr = bytearray(4)
        mv = bytearray(count * 3)
        hdr[3] = (count - 1) | (0x10 if keys[first][1] else 0)
        for axis in range(3):
            s, start = _step_for(keys, first, count, axis)
            if s < 0:
                raise ValueError("a rotation key moves too far to write")
            step = _STEPS[s]
            big, doubled = step >= 4, step in (2, 32)
            hdr[axis] = (start & 0x7F) | (0x80 if big else 0)
            if doubled:
                hdr[3] |= 0x80 >> axis
            previous = 0
            for k in range(count):
                target = int(round((keys[first + k][0][axis] - start * 256) / step))
                mv[k * 3 + axis] = (target - previous) & 0xFF
                previous = target
        runs += hdr
        moves += mv
        first += count
    o += runs
    o += moves


def _w_positions(o, positions):
    runs = []
    data = bytearray()
    start = None
    in_run = 0
    for p in positions:
        fits = start is not None and in_run < 0xFFFF and all(abs(round((p[a] - start[a] / 64.0) * 4000)) <= 127 for a in range(3))
        if not fits:
            if start is not None:
                runs.append((in_run, start))
            start = [max(-32768, min(32767, int(round(v * 64)))) for v in p]
            in_run = 0
        for a in range(3):
            data.append(int(round((p[a] - start[a] / 64.0) * 4000)) & 0xFF)
        in_run += 1
    if start is not None:
        runs.append((in_run, start))
    for n, s in runs:
        o += struct.pack("<H3h", n, *s)
    o += data


# ---------------------------------------------------------------- character meshes


class CharacterMesh:
    """A character's mesh (.ccmesh_pc with its .gcmesh_pc): the most detailed level (the first
    submesh), its vertices (position, normal, the first UV channel), triangles by material, and for
    each vertex up to 4 skeleton bones and weights.

    The c-file's 0x08 points at the mesh block (version 9): +0x10 detail levels, +0x14 submesh
    count, +0x18 submesh table, +0x1C vertex count, +0x20 vertex size, format, UV channels, +0x3C
    vertex buffer offset (in the g-file), +0x40 index count, +0x44 index offset, +0x48 index size,
    topology (0 strips), +0x4A render block count, +0x4C the bone palettes' size, +0x54 their
    count, from +0x5C the palettes (a skeleton bone per mesh bone number), then 2 bytes each (size,
    start). Vertices (format 3): position, normal (4 bytes), tangent, at +20 four weights, at +24
    four palette numbers, the UVs last (16-bit, 1024 = 1.0; v negated: 0 at the texture's top, -1 at its
    bottom, as checked against the textures).
    """

    def __init__(self):
        self.positions = []
        self.normals = []
        self.uvs = []
        self.triangles = []   # (a, b, c, material)
        self.bones = []       # per vertex, 4 skeleton bone numbers
        self.weights = []     # per vertex, 4 weights (adding up to 1)
        self.materials = []   # Material, by the number the triangles give

    @classmethod
    def read(cls, header, data, rig=None):
        m = cls()
        at = _u32(header, 0x08)
        if _u32(header, at) != 9:
            raise ValueError("no version-9 mesh block where the header says")
        submesh_count = _u32(header, at + 0x14)
        submesh_table = at + _u32(header, at + 0x18)
        vcount = _u32(header, at + 0x1C)
        vsize, vformat, uvch = header[at + 0x20], header[at + 0x21], header[at + 0x22]
        voff, icount, ioff = _u32(header, at + 0x3C), _u32(header, at + 0x40), _u32(header, at + 0x44)
        isize, topology = header[at + 0x48], header[at + 0x49]
        block_count = _u16(header, at + 0x4A)
        if isize not in (2, 4):
            raise ValueError(f"{isize}-byte indices")
        uv_at = vsize - 4 * uvch
        skinned = vformat == 3 and vsize >= 28
        numbers = []
        for v in range(vcount):
            p = voff + v * vsize
            m.positions.append(struct.unpack_from("<3f", data, p))
            nx, ny, nz = (data[p + 12] / 127.5 - 1, data[p + 13] / 127.5 - 1, data[p + 14] / 127.5 - 1)
            ln = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
            m.normals.append((nx / ln, ny / ln, nz / ln))
            m.uvs.append((_s16(data, p + uv_at) / 1024.0, _s16(data, p + uv_at + 2) / 1024.0) if uvch else (0.0, 0.0))
            if skinned:
                w = data[p + 20:p + 24]
                total = sum(w)
                m.weights.append(tuple((x / total) if total else (1.0 if i == 0 else 0.0) for i, x in enumerate(w)))
                numbers.append(tuple(data[p + 24:p + 28]))
        fmt = "<%d%s" % (icount, "H" if isize == 2 else "I")
        indices = struct.unpack_from(fmt, data, ioff)

        block_table = submesh_table + submesh_count * 48
        parts = []  # (material, triangles) of the first submesh
        if submesh_count:
            blocks = _u32(header, submesh_table)
            for b in range(min(blocks, block_count)):
                r = block_table + b * 20
                material, first, count = _u16(header, r), _u32(header, r + 4), _u32(header, r + 8)
                tris = []
                if topology == 0:
                    for i in range(first, first + count - 2):
                        a, bb, c = indices[i], indices[i + 1], indices[i + 2]
                        if a == bb or bb == c or a == c:
                            continue
                        tris.append((a, bb, c) if (i - first) % 2 == 0 else (bb, a, c))
                else:
                    for i in range(first, first + count // 3 * 3, 3):
                        tris.append((indices[i], indices[i + 1], indices[i + 2]))
                parts.append((material, tris))
        for material, tris in parts:
            for t in tris:
                m.triangles.append((t[0], t[1], t[2], material))

        if skinned:
            palette_size, palette_count = _u32(header, at + 0x4C), _u32(header, at + 0x54)
            palettes_at = at + 0x5C
            list_at = palettes_at + palette_size
            palettes = []
            for i in range(palette_count):
                if list_at + 2 * i + 1 >= len(header):
                    break
                size, first = header[list_at + 2 * i], header[list_at + 2 * i + 1]
                if first + size <= palette_size:
                    palettes.append(list(header[palettes_at + first:palettes_at + first + size]))
            if sum(len(p) for p in palettes) != palette_size:
                palettes = [list(header[palettes_at:list_at])]
            m.bones = _skin_bones(m, parts, numbers, palettes, rig)
        try:
            m.materials = Material.read_listed(header)
        except (struct.error, IndexError, ValueError):
            m.materials = []
        return m


def _skin_bones(m, parts, numbers, palettes, rig):
    """Skeleton bones per vertex through the palettes: each part takes the palette that puts its
    vertices nearest the bones moving them most (as the editor's ModelFile.SkinBones)."""
    nbones = len(rig.bones) if rig else 256

    def bone_of(pal, n):
        return pal[n] if n < len(pal) and pal[n] < nbones else 0

    def heaviest(v):
        w = m.weights[v]
        return numbers[v][max(range(4), key=lambda i: w[i])]

    out = [None] * len(m.positions)
    for _, tris in parts:
        verts = sorted({i for t in tris for i in t})
        chosen, nearest = palettes[0], float("inf")
        for pal in palettes:
            if any(m.weights[v][i] > 0 and numbers[v][i] >= len(pal) for v in verts for i in range(4)):
                continue
            if rig is None:
                chosen = pal
                break
            dist = 0.0
            for v in verts:
                bp = rig.bones[bone_of(pal, heaviest(v))].position
                pp = m.positions[v]
                dist += math.sqrt(sum((pp[a] - bp[a]) ** 2 for a in range(3)))
            if dist < nearest:
                chosen, nearest = pal, dist
        for v in verts:
            out[v] = tuple(bone_of(chosen, numbers[v][i]) for i in range(4))
    for v in range(len(out)):
        if out[v] is None:
            out[v] = tuple(bone_of(palettes[0], numbers[v][i]) for i in range(4))
    return out


def mesh_data_name(name):
    """x.ccmesh_pc -> x.gcmesh_pc"""
    return name[:-len("ccmesh_pc")] + "gcmesh_pc" if name.lower().endswith("ccmesh_pc") else name


# ---------------------------------------------------------------- materials


def name_hash(name):
    """The game's hash of a shader's slot and constant names: CRC-32 of the lower-cased name, with
    no inversion before or after (Diffuse_Map 0x69B48F91, Normal_Map 0x2808EB90)."""
    crc = 0
    for c in name.lower().encode("latin-1"):
        crc ^= c
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc


DIFFUSE_MAP = name_hash("Diffuse_Map")
NORMAL_MAP = name_hash("Normal_Map")
SPECULAR_MAP = name_hash("Specular_Map")


class Material:
    """A model's material (as the editor's ModelMaterial): its block starts with its size, two
    checksums, flags (+12: the low two bits 0 solid, 1 blended over what's behind it, 2 cut out by
    the colour texture's alpha), the texture count (+16, u16) and the constant count (+18); from
    +0x24, 12 bytes a texture: where its name is in the texture name list, the slot's hash, its
    number (u16); then the constants' name hashes and, from the next multiple of 16, their values
    (a float4 each). Characters list their materials' offsets in the header (0x10 the list, 0x14
    how many); the name list is where every name offset lands on a ".tga" name."""

    def __init__(self, textures, constants, flags):
        self.textures = textures    # [(slot hash, file name "x.tga")]
        self.constants = constants  # {name hash: (x, y, z, w)}
        self.flags = flags

    @property
    def blended(self):
        return self.flags & 3 == 1

    @property
    def cut_out(self):
        return self.flags & 3 == 2

    def texture(self, slot):
        for h, name in self.textures:
            if h == slot and name:
                return name
        return None

    def constant(self, name):
        return self.constants.get(name_hash(name))

    @staticmethod
    def _slots(h, at):
        if at < 0 or at + 0x30 > len(h):
            return None
        size, count = _u32(h, at), _u16(h, at + 16)
        if size < 0x40 or size > 0x4000 or at + size > len(h) or not 1 <= count <= 16 or h[at + 18] > h[at + 19] \
                or at + 0x24 + count * 12 > len(h):
            return None
        slots = []
        for t in range(count):
            d = at + 0x24 + t * 12
            if _u16(h, d + 8) != t:
                return None
            slots.append((_u32(h, d + 4), _u32(h, d)))
        return slots

    @staticmethod
    def _names_at(h, offsets):
        if not offsets:
            return -1
        at = h.find(b".tga")
        while at >= 0:
            start = at
            while start > 0 and 0x20 <= h[start - 1] < 0x7F:
                start -= 1
            if all(start + o < len(h) and _cstr(h, start + o).lower().endswith(".tga") for o in offsets):
                return start
            at = h.find(b".tga", at + 4)
        return -1

    @classmethod
    def read_listed(cls, header):
        """A character's or static mesh's materials, in the order its triangles number them."""
        h = bytes(header)
        list_at, count = _u32(h, 0x10), _u32(h, 0x14)
        if count >= 1024 or list_at + 4 * count > len(h):
            return []
        found = []
        for i in range(count):
            at = _u32(h, list_at + 4 * i)
            found.append((at, cls._slots(h, at) or []))
        names = cls._names_at(h, sorted({o for _, slots in found for _, o in slots}))
        out = []
        for at, slots in found:
            textures = [(slot, _cstr(h, names + o) if names >= 0 else "") for slot, o in slots]
            constants = {}
            if 0 <= at and at + 0x24 <= len(h):
                n = h[at + 18]
                names_at = at + 0x24 + len(slots) * 12
                values_at = _align(names_at + 4 * n, 16)
                for c in range(n):
                    v = values_at + 16 * c
                    if v + 16 > len(h):
                        break
                    constants[_u32(h, names_at + 4 * c)] = struct.unpack_from("<4f", h, v)
            flags = _u32(h, at + 12) if 0 <= at and at + 16 <= len(h) else 0x40
            out.append(cls(textures, constants, flags))
        return out


# ---------------------------------------------------------------- textures


DXT1, DXT3, DXT5, RGB565, ARGB1555, ARGB4444, RGB888, ARGB8888, A8 = 400, 401, 402, 403, 404, 405, 406, 407, 410


def level_size(fmt, w, h):
    w, h = max(1, w), max(1, h)
    blocks = (w + 3) // 4 * ((h + 3) // 4)
    if fmt == DXT1:
        return blocks * 8
    if fmt in (DXT3, DXT5):
        return blocks * 16
    per = {RGB565: 2, ARGB1555: 2, ARGB4444: 2, RGB888: 3, ARGB8888: 4, A8: 1}.get(fmt)
    if per is None:
        raise ValueError(f"texture format {fmt} is unknown")
    return w * h * per


class PegTexture:
    __slots__ = ("name", "width", "height", "format", "flags", "mips", "offset", "size")

    def __init__(self, name, width, height, fmt, flags, mips, offset, size):
        self.name, self.width, self.height, self.format = name, width, height, fmt
        self.flags, self.mips, self.offset, self.size = flags, mips, offset, size


class PegFile:
    """A texture file pair (as the editor's PegFile): the header file (.cpeg_pc / .cvbm_pc, "GEKV"
    version 10: at 0x10 the texture count; from 0x18, 48 bytes a texture: data offset, width,
    height, format (u16 each from +4), flags (+18), mip levels (+27), data size (+28); then the
    names) and the data file (.gpeg_pc / .gvbm_pc: each texture's mip levels, largest first)."""

    MAGIC = b"GEKV"

    def __init__(self, header, data):
        if len(header) < 0x18 or header[:4] != self.MAGIC:
            raise ValueError("not a texture header file (GEKV)")
        if _u16(header, 4) != 10:
            raise ValueError(f"texture format version {_u16(header, 4)}; only version 10 is known")
        self.data = data
        count = _u16(header, 0x10)
        name_at = 0x18 + 48 * count
        self.textures = []
        for i in range(count):
            at = 0x18 + 48 * i
            name = _cstr(header, name_at)
            name_at += len(name) + 1
            self.textures.append(PegTexture(name, _u16(header, at + 4), _u16(header, at + 6), _u16(header, at + 8),
                                            _u16(header, at + 18), header[at + 27], _u32(header, at), _u32(header, at + 28)))
        self._by_name = {t.name.lower(): t for t in self.textures}

    def find(self, name):
        """A texture by name, with or without its ".tga"."""
        key = name.lower()
        return self._by_name.get(key) or self._by_name.get(key[:-4] if key.endswith(".tga") else key + ".tga")

    def dds(self, texture):
        """One texture as a DDS file, its mip levels as they are (Blender and image editors read them)."""
        fmt, w, h = texture.format, texture.width, texture.height
        mips, total = 0, 0
        for level in range(max(1, texture.mips)):
            n = level_size(fmt, w >> level, h >> level)
            if texture.offset + total + n > len(self.data):
                break
            total += n
            mips += 1
        if not mips:
            raise ValueError(f"{texture.name}: the data file is shorter than its header says")
        pixels = self.data[texture.offset:texture.offset + total]
        compressed = fmt in (DXT1, DXT3, DXT5)
        flags = 0x1 | 0x2 | 0x4 | 0x1000 | (0x20000 if mips > 1 else 0) | (0x80000 if compressed else 0x8)
        pitch = level_size(fmt, w, h) if compressed else level_size(fmt, w, 1)
        out = bytearray(b"DDS ")
        out += struct.pack("<7I", 124, flags, h, w, pitch, 0, mips) + bytes(44)
        pf = {
            DXT1: (0x4, b"DXT1", 0, 0, 0, 0, 0), DXT3: (0x4, b"DXT3", 0, 0, 0, 0, 0), DXT5: (0x4, b"DXT5", 0, 0, 0, 0, 0),
            ARGB8888: (0x41, b"\0\0\0\0", 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000),
            RGB888: (0x40, b"\0\0\0\0", 24, 0xFF0000, 0xFF00, 0xFF, 0),
            RGB565: (0x40, b"\0\0\0\0", 16, 0xF800, 0x07E0, 0x001F, 0),
            ARGB1555: (0x41, b"\0\0\0\0", 16, 0x7C00, 0x03E0, 0x001F, 0x8000),
            ARGB4444: (0x41, b"\0\0\0\0", 16, 0x0F00, 0x00F0, 0x000F, 0xF000),
            A8: (0x2, b"\0\0\0\0", 8, 0, 0, 0, 0xFF),
        }[fmt]
        out += struct.pack("<2I", 32, pf[0]) + pf[1] + struct.pack("<5I", *pf[2:])
        out += struct.pack("<5I", 0x1000 | (0x400008 if mips > 1 else 0), 0, 0, 0, 0)
        out += pixels
        return bytes(out)


def peg_data_name(name):
    """x.cpeg_pc -> x.gpeg_pc, x.cvbm_pc -> x.gvbm_pc"""
    low = name.lower()
    for c, g in ((".cpeg_pc", ".gpeg_pc"), (".cvbm_pc", ".gvbm_pc")):
        if low.endswith(c):
            return name[:-len(c)] + g
    return None


# ---------------------------------------------------------------- asset lists


def asm_animations(data):
    """{container file name (x.str2_pc): [animation file names]} from an asset list (.asm_pc,
    version 5: 0xBEEFFEED, version (u16), container count (u16); a container: name (u16 length +
    name, without .str2_pc), type (u8), flags (u16), primitive count (u16), data offset, size count,
    compressed size, the sizes (i32 each); a primitive: name, type, allocator, flags, split index
    (u8 each), its two sizes (i32 each))."""
    if len(data) < 8 or _u32(data, 0) != 0xBEEFFEED or _u16(data, 4) != 5:
        return {}
    out = {}
    at = 8
    for _ in range(_u16(data, 6)):
        n = _u16(data, at)
        name = data[at + 2:at + 2 + n].decode("latin-1")
        at += 2 + n
        primitives, sizes = _u16(data, at + 3), _u32(data, at + 9)
        at += 17 + 4 * sizes
        anims = []
        for _ in range(primitives):
            n = _u16(data, at)
            p = data[at + 2:at + 2 + n].decode("latin-1")
            at += 2 + n + 12
            if p.lower().endswith(".anim_pc"):
                anims.append(p)
        if anims:
            out[name + ".str2_pc"] = anims
    return out
