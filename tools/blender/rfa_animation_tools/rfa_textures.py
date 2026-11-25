"""Textures for the game: pixels (numpy, RGBA bytes) to DXT1 / DXT5 blocks with their mip levels, and
texture file pairs (.cpeg_pc + .gpeg_pc, "GEKV" version 10) holding several, laid out as the game's
own vehicle texture files are (each texture's levels from its full size down to 4 pixels on its
shorter side, on 16-byte boundaries; the header's entries: data offset, width, height, format,
source width 0, one animation tile across and down, one frame, flags (0x200 on maps holding data
rather than colours: normal maps, masks), name offset 0, source height 0, one frame a second, the
level count, the data's size, 16 bytes the game fills in; then the names).

The encoder is a plain range fit (each 4 x 4 block's colours along the line between its extremes),
done for all blocks at once with numpy (Blender ships it).
"""

import struct

import numpy as np

DXT1, DXT5, ARGB8888 = 400, 402, 407
DATA_FLAG = 0x200


def _to565(c):
    c = c.astype(np.int32)
    return ((c[..., 0] >> 3) << 11) | ((c[..., 1] >> 2) << 5) | (c[..., 2] >> 3)


def _from565(v):
    r = (v >> 11) & 31
    g = (v >> 5) & 63
    b = v & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], axis=-1).astype(np.float32)


def _blocks(rgba):
    h, w, _ = rgba.shape
    return rgba.reshape(h // 4, 4, w // 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 4)


def _colour_blocks(px):
    """DXT1 colour halves (8 bytes a block, four-colour mode) of blocks of 16 RGB pixels."""
    rgb = px[..., :3].astype(np.float32)
    mean = rgb.mean(axis=1, keepdims=True)
    lo, hi = rgb.min(axis=1), rgb.max(axis=1)
    axis = hi - lo
    norm = np.linalg.norm(axis, axis=1, keepdims=True)
    axis = np.where(norm > 0, axis / np.maximum(norm, 1e-6), 0)
    t = ((rgb - mean) * axis[:, None, :]).sum(axis=2)
    tmin, tmax = t.min(axis=1), t.max(axis=1)
    e0 = np.clip(mean[:, 0, :] + axis * tmax[:, None], 0, 255)
    e1 = np.clip(mean[:, 0, :] + axis * tmin[:, None], 0, 255)
    c0, c1 = _to565(e0.round()), _to565(e1.round())
    swap = c0 < c1
    c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
    p0, p1 = _from565(c0), _from565(c1)
    palette = np.stack([p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3], axis=1)  # (n, 4, 3)
    dist = ((rgb[:, :, None, :] - palette[:, None, :, :]) ** 2).sum(axis=3)
    idx = dist.argmin(axis=2).astype(np.uint32)
    idx = np.where((c0 == c1)[:, None], 0, idx)
    bits = (idx << (2 * np.arange(16, dtype=np.uint32))).sum(axis=1).astype(np.uint32)
    out = np.zeros((len(px), 8), np.uint8)
    out[:, 0:2] = c0.astype("<u2").view(np.uint8).reshape(-1, 2)
    out[:, 2:4] = c1.astype("<u2").view(np.uint8).reshape(-1, 2)
    out[:, 4:8] = bits.astype("<u4").view(np.uint8).reshape(-1, 4)
    return out


def _alpha_blocks(px):
    """DXT5 alpha halves (8 bytes a block, eight-value mode)."""
    a = px[..., 3].astype(np.int32)
    a0, a1 = a.max(axis=1), a.min(axis=1)
    pal = np.stack([a0, a1] + [((7 - k) * a0 + k * a1) // 7 for k in range(1, 7)], axis=1)
    idx = np.abs(a[:, :, None] - pal[:, None, :]).argmin(axis=2).astype(np.uint64)
    idx = np.where((a0 == a1)[:, None], 0, idx)
    bits = (idx << (3 * np.arange(16, dtype=np.uint64))).sum(axis=1)
    out = np.zeros((len(px), 8), np.uint8)
    out[:, 0] = a0
    out[:, 1] = a1
    for k in range(6):
        out[:, 2 + k] = (bits >> np.uint64(8 * k)) & np.uint64(0xFF)
    return out


def encode(rgba, fmt):
    """One level's blocks: rgba is (h, w, 4) uint8, h and w multiples of 4."""
    px = _blocks(rgba)
    if fmt == DXT1:
        return _colour_blocks(px).tobytes()
    if fmt == DXT5:
        return np.concatenate([_alpha_blocks(px), _colour_blocks(px)], axis=1).tobytes()
    raise ValueError(f"format {fmt} isn't encoded")


def _half(rgba):
    h, w, _ = rgba.shape
    f = rgba.astype(np.float32)
    if h > 1:
        f = (f[0::2] + f[1::2]) / 2
    if w > 1:
        f = (f[:, 0::2] + f[:, 1::2]) / 2
    return f.round().astype(np.uint8)


def levels(rgba, fmt):
    """All mip levels' bytes (down to 4 pixels on the shorter side) and how many."""
    h, w, _ = rgba.shape
    data, n = bytearray(), 0
    level = rgba
    while True:
        data += encode(level, fmt) if fmt in (DXT1, DXT5) else level[..., [2, 1, 0, 3]].tobytes()
        n += 1
        lh, lw = level.shape[:2]
        if min(lh, lw) <= 4:
            break
        level = _half(level)
    return bytes(data), n


def power_of_two(n, largest=2048):
    p = 4
    while p < n and p < largest:
        p *= 2
    return p


class TextureFile:
    """A texture file pair being made: add textures, then files() gives (c-file, g-file)."""

    def __init__(self):
        self.entries = []  # (name, width, height, format, flags, mips, data)

    def add(self, name, rgba, fmt=None, data_map=False):
        """A texture from (h, w, 4) uint8 pixels (sizes powers of two, at least 4)."""
        if fmt is None:
            fmt = DXT5 if (rgba[..., 3] < 250).any() else DXT1
        data, mips = levels(rgba, fmt)
        self.entries.append((name, rgba.shape[1], rgba.shape[0], fmt, DATA_FLAG if data_map else 0, mips, data))

    def add_raw(self, name, width, height, fmt, flags, mips, data):
        """A texture as stored (from another texture file)."""
        self.entries.append((name, width, height, fmt, flags, mips, bytes(data)))

    def names(self):
        return [e[0] for e in self.entries]

    def files(self):
        g = bytearray()
        entries = bytearray()
        names = bytearray()
        for name, w, h, fmt, flags, mips, data in self.entries:
            while len(g) % 16:
                g.append(0)
            entries += struct.pack("<I8H", len(g), w, h, fmt, 0, 1, 1, 1, flags)
            entries += struct.pack("<IHBBI", 0, 0, 1, mips, len(data)) + bytes(16)
            g += data
            names += name.encode("latin-1") + b"\0"
        while len(g) % 16:
            g.append(0)
        size = 0x18 + len(entries) + len(names)
        header = struct.pack("<4sHHIIHHHH", b"GEKV", 10, 0, size, len(g), len(self.entries), 0, len(self.entries), 16)
        return bytes(header + entries + names), bytes(g)
