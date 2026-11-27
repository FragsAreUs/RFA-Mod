"""The game's sounds by name, for picking and hearing them in Blender (no bpy here).

Armageddon plays Wwise (banks version 48). A sound's name (what the tables give: a door's Door_open, an
animation's Foley) is a Wwise event, found by its id: FNV-1 (32 bits) of the lower-case name. The banks
(<name>.bnk_pc) hold events, actions and sounds (HIRC: type, size, then the object, its id first); a sound
names its medium, which is in a media bank beside its bank (<name>_media.bnk_pc, Volition's "VWSBPC"
container: at 0x18 the size of its index, at 0x1C the count, then 16 bytes a medium: its id, where it starts,
0, its size).

Where the banks are, the first found winning: overrides\\build\\pc\\cache\\ (loose files, and the folders
sounds.vpp_pc\\ and soundboot.vpp_pc\\), then the game's build\\pc\\cache\\sounds.vpp_pc and soundboot.vpp_pc.
The Guerrilla world's banks (rfg_*, rfge_*, ...) are loose in overrides; their media are Wwise ADPCM or PCM,
which this module turns into a .wav itself. Armageddon's own media are Wwise Vorbis: those need vgmstream
(vgmstream-cli.exe, free), which the add-on runs when its path is set.

Names: audio_events.xtbl (the game's list of its events, by category), and every name in the tables
(the game's, the Guerrilla world's, the mods' and the overrides') whose id is an event in a bank.
"""

import os
import re
import struct
import subprocess
import tempfile

from . import rfa_formats as rf

CACHE = ("build", "pc", "cache")
ARCHIVES = ("sounds.vpp_pc", "soundboot.vpp_pc")
EVENT, ACTION, SOUND = 4, 3, 2
VORBIS, ADPCM, PCM_PLUGIN = 0x00040001, 0x00020001, 0x00010001


def event_id(name):
    """Wwise's id of a name: FNV-1 of its lower-case letters."""
    h = 2166136261
    for b in name.lower().encode("ascii", "replace"):
        h = ((h * 16777619) & 0xFFFFFFFF) ^ b
    return h


def chunks(data):
    out, i = {}, 0
    while i + 8 <= len(data):
        tag = data[i:i + 4]
        n = struct.unpack_from("<I", data, i + 4)[0]
        out[tag] = (i + 8, n)
        i += 8 + n
    return out


class Bank:
    """A bank's objects: {id: (type, body after the id)}."""

    def __init__(self, name, data):
        self.name = name
        self.objects = {}
        c = chunks(data)
        if b"HIRC" not in c:
            return
        at, size = c[b"HIRC"]
        end = at + size
        count = struct.unpack_from("<I", data, at)[0]
        p = at + 4
        for _ in range(count):
            if p + 12 > end:
                break
            kind, length = struct.unpack_from("<II", data, p)
            body = data[p + 8:p + 8 + length]
            if len(body) >= 4:
                self.objects[struct.unpack_from("<I", body, 0)[0]] = (kind, body[4:])
            p += 8 + length

    def events(self):
        return [i for i, (k, _) in self.objects.items() if k == EVENT]

    def _children(self, body):
        """A container's children: a count and that many ids of this bank's sounds or containers (found by
        looking for them, as where they sit depends on the container's other settings)."""
        best = []
        for at in range(0, len(body) - 8):
            n = struct.unpack_from("<I", body, at)[0]
            if not 0 < n <= 256 or at + 4 + 4 * n > len(body):
                continue
            ids = struct.unpack_from("<%dI" % n, body, at + 4)
            if all(i in self.objects and self.objects[i][0] in (2, 5, 6, 7, 9) for i in ids) and len(ids) > len(best):
                best = list(ids)
        return best

    def sounds(self, eid):
        """The sounds an event plays: (codec plugin, medium id), each one it may pick."""
        out, seen = [], set()

        def walk(oid, depth=0):
            if oid in seen or depth > 8 or oid not in self.objects:
                return
            seen.add(oid)
            kind, body = self.objects[oid]
            if kind == SOUND and len(body) >= 16:
                plugin, stream, source, file_id = struct.unpack_from("<4I", body, 0)
                out.append((plugin, file_id if stream else source))
            elif kind in (5, 6, 7, 9):
                for c in self._children(body):
                    walk(c, depth + 1)

        ev = self.objects.get(eid)
        if not ev or ev[0] != EVENT or len(ev[1]) < 4:
            return out
        n = struct.unpack_from("<I", ev[1], 0)[0]
        for k in range(min(n, (len(ev[1]) - 4) // 4)):
            aid = struct.unpack_from("<I", ev[1], 4 + 4 * k)[0]
            act = self.objects.get(aid)
            if not act or act[0] != ACTION or len(act[1]) < 8:
                continue
            kind, target = struct.unpack_from("<II", act[1], 0)
            if kind & 0xFFFF00 == 0x4000:  # play (0x4011 on the game object; stop 0x1011, break 0x90011)
                walk(target)
        return out


def media_index(head):
    """A media bank's index from its first bytes: {medium id: (start, size)}, and how many bytes the index needs."""
    if head[:8] != b"VWSBPC  " or len(head) < 0x1C:
        return {}, 0
    count = struct.unpack_from("<I", head, 0x18)[0]
    need = 0x1C + 16 * count
    if len(head) < need:
        return None, need
    out = {}
    for i in range(count):
        mid, start, _, size = struct.unpack_from("<4I", head, 0x1C + 16 * i)
        out[mid] = (start, size)
    return out, need


# ---------------------------------------------------------------- the media as .wav

_STEPS = (7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
          118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
          963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
          5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
          24623, 27086, 29794, 32767)
_INDEX = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)


def wwise_adpcm(data, channels):
    """Wwise ADPCM (36 bytes a channel's block of 64 samples: first sample, step index, a spare byte, 63 codes)
    to 16-bit samples, channels interleaved (as the game's decoder: see the editor's WwiseAdpcm)."""
    blocks = len(data) // (36 * channels)
    out = [0] * (blocks * 64 * channels)
    for b in range(blocks):
        for c in range(channels):
            at = (b * channels + c) * 36
            sample = struct.unpack_from("<h", data, at)[0]
            index = min(data[at + 2], 88)
            to = b * 64 * channels + c
            out[to] = sample
            for n in range(1, 64):
                both = data[at + 4 + (n - 1) // 2]
                code = both & 15 if (n - 1) % 2 == 0 else both >> 4
                change = _STEPS[index] * (2 * (code & 7) + 1) // 8
                sample = sample - change if code & 8 else sample + change
                sample = -32768 if sample < -32768 else 32767 if sample > 32767 else sample
                index = min(88, max(0, index + _INDEX[code]))
                out[to + n * channels] = sample
    return struct.pack("<%dh" % len(out), *out)


def wav(samples16, channels, rate):
    n = len(samples16)
    return (b"RIFF" + struct.pack("<I", 36 + n) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, channels, rate, rate * 2 * channels,
                                                                             2 * channels, 16)
            + b"data" + struct.pack("<I", n) + samples16)


def wem_format(wem):
    c = chunks(wem[12:]) if wem[:4] == b"RIFF" else {}
    if b"fmt " not in c or b"data" not in c:
        return None
    f, _ = c[b"fmt "]
    tag, channels, rate = struct.unpack_from("<HHI", wem, 12 + f)
    d, n = c[b"data"]
    return tag, channels, rate, wem[12 + d:12 + d + n]


def to_wav(wem, vgmstream="", work=None):
    """A medium as .wav bytes, or raise ValueError saying why it can't be."""
    f = wem_format(wem)
    if f is None:
        raise ValueError("not a Wwise medium")
    tag, channels, rate, data = f
    if tag == 2:
        return wav(wwise_adpcm(data, channels), channels, rate)
    if tag in (1, 0xFFFE):
        return wav(data, channels, rate)
    if tag == 0xFFFF:  # Wwise Vorbis
        if not vgmstream or not os.path.isfile(vgmstream):
            raise ValueError("Armageddon's own sounds are Wwise Vorbis: set vgmstream-cli.exe in the add-on's preferences to hear them")
        work = work or tempfile.gettempdir()
        src, dst = os.path.join(work, "rfa_preview.wem"), os.path.join(work, "rfa_preview_vgm.wav")
        with open(src, "wb") as fh:
            fh.write(wem)
        if os.path.isfile(dst):
            os.remove(dst)
        flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        r = subprocess.run([vgmstream, "-o", dst, src], capture_output=True, timeout=60, creationflags=flags)
        if r.returncode != 0 or not os.path.isfile(dst):
            raise ValueError("vgmstream could not convert it: " + (r.stderr or r.stdout).decode(errors="replace")[-200:])
        with open(dst, "rb") as fh:
            return fh.read()
    raise ValueError(f"medium format 0x{tag:04X} isn't known")


# ---------------------------------------------------------------- the library


# What a bank's sounds are, by the start of its name (first match wins).
_TYPES = (
    (("wep_", "rfge_wep", "rfgw_wep", "rfge_mp_wep", "rfgw_mp_wep"), "Weapons"),
    (("vehicles", "rfg_veh", "rfge_veh", "sfx_exo", "veh_"), "Vehicles"),
    (("foley", "rfg_mov", "rfge_mov", "rfgw_mov", "rfge_mp_backpacks", "rfgw_mp_backpacks", "tentacle"), "Foley & movement"),
    (("destruction", "rfg_destruction", "rfge_destruction", "rfg_obj", "rfge_obj_shared_dth", "rfge_obj_shared_exp",
      "rfge_obj_shared_impact", "rfgw_obj"), "Destruction & impacts"),
    (("amb_", "rfge_obj_shared_ambient", "rfgk_obj_shared_ambient", "sfx_streaming"), "Ambience"),
    (("efx", "nanoforge"), "Effects"),
    (("mus_", "rfgm_"), "Music"),
    (("interface", "debug", "global", "rfgw_system"), "Interface"),
    (("voices", "rfgv_", "rfgk_voc", "rfge_walla", "audiologs", "voc_"), "Voices"),
    (("sfx_",), "Mission effects"),
)


# audio_events.xtbl's categories (the start of the sounds' names) in words.
_CATEGORY_WORDS = {
    "AMB": "Ambience", "ANI": "Animations", "DES": "Destruction", "EFX": "Effects", "FOL": "Foley", "INT": "Interface",
    "ITM": "Items", "MOV": "Movement", "MP_": "Multiplayer", "MUS": "Music", "NAN": "Nano forge", "OBJ": "Objects",
    "S_C": "Scripted", "SFX": "Sound effects", "STOP": "Stop", "VEH": "Vehicles", "VFX": "Visual effects",
    "VOC": "Voices", "WEP": "Weapons", "CIN": "Cinematics", "UI_": "Interface", "PC_": "Player",
}
_GUERRILLA_BANKS = {"civ": "civilian", "edf": "EDF", "mar": "Marauder", "min": "miners", "shared": "shared",
                    "mov": "movement", "obj": "objects", "wep": "weapons", "veh": "vehicles", "dth": "deaths",
                    "exp": "explosions", "mp": "multiplayer", "voc": "voices"}


def category_words(category, bank):
    """A category to show: the game's (in words; M04 = Mission 4), else where the sound comes from."""
    c = (category or "").upper()
    if c:
        if re.fullmatch(r"M\d\d", c):
            return "Mission " + str(int(c[1:]))
        if re.fullmatch(r"Q\d\d", c):
            return "Quest " + str(int(c[1:]))
        return _CATEGORY_WORDS.get(c, c)
    b = (bank or "").lower()
    m = re.match(r"rfg[a-z]?_(.*)", b)
    if m:
        return "Guerrilla: " + " ".join(_GUERRILLA_BANKS.get(w, w) for w in m.group(1).split("_"))
    return b.replace("_", " ").capitalize()


def sound_type(bank, name=""):
    """A sound's kind, from its bank's name ("Stop" for events that end other sounds)."""
    if name.upper().endswith("_STOP"):
        return "Stop"
    b = (bank or "").lower()
    for starts, kind in _TYPES:
        if b.startswith(starts):
            return kind
    return "Other"


class _Source:
    """Somewhere banks are: a folder of loose files, or an archive."""

    def __init__(self, folder=None, archive=None):
        self.folder, self.archive = folder, archive
        if folder:
            self.files = {n.lower(): n for n in os.listdir(folder) if os.path.isfile(os.path.join(folder, n))}
        else:
            self.files = {n.lower(): n for n in archive.names()}
            self._entries = {e[0].lower(): e for e in archive.entries}

    def has(self, name):
        return name.lower() in self.files

    def read(self, name):
        if self.folder:
            with open(os.path.join(self.folder, self.files[name.lower()]), "rb") as fh:
                return fh.read()
        return self.archive.read(self.files[name.lower()])

    def read_part(self, name, start, length):
        if self.folder:
            with open(os.path.join(self.folder, self.files[name.lower()]), "rb") as fh:
                fh.seek(start)
                return fh.read(length)
        a = self.archive
        if not a.compressed:
            stored = self._entries[name.lower()][4]
            return a._read(stored + start, length)
        return a.read(self.files[name.lower()])[start:start + length]


class Library:
    """Every sound the game can play, by name."""

    def __init__(self, game_folder, mod_folder=None):
        self.game_folder = game_folder
        self.mod_folder = mod_folder or game_folder
        self.sources = []
        over = os.path.join(self.mod_folder, "overrides", *CACHE)
        if os.path.isdir(over):
            self.sources.append(_Source(folder=over))
            for a in ARCHIVES:
                if os.path.isdir(os.path.join(over, a)):
                    self.sources.append(_Source(folder=os.path.join(over, a)))
        for a in ARCHIVES:
            path = os.path.join(game_folder, *CACHE, a)
            if os.path.isfile(path):
                self.sources.append(_Source(archive=rf.Packfile.open(path)))
        self._banks = {}
        self._event_bank = None
        self._media = {}
        self._names = None
        self.categories, self.types, self.banks = {}, {}, {}

    def bank_names(self):
        seen = {}
        for s in self.sources:
            for low, n in s.files.items():
                if low.endswith(".bnk_pc") and not low.endswith("_media.bnk_pc") and low != "init.bnk_pc" and low not in seen:
                    seen[low] = (n[:-len(".bnk_pc")], s)
        return seen

    def bank(self, name):
        b = self._banks.get(name.lower())
        if b is None:
            for s in self.sources:
                if s.has(name + ".bnk_pc"):
                    b = Bank(name, s.read(name + ".bnk_pc"))
                    break
            else:
                b = Bank(name, b"")
            self._banks[name.lower()] = b
        return b

    def event_banks(self):
        """{event id: bank name} over every bank (read once)."""
        if self._event_bank is None:
            self._event_bank = {}
            for low, (name, _) in self.bank_names().items():
                for e in self.bank(name).events():
                    self._event_bank.setdefault(e, name)
        return self._event_bank

    def _table_texts(self):
        """The tables' texts: the game's table.vpp_pc, then overrides of it, mods\\tables, the Guerrilla world."""
        texts = {}
        path = os.path.join(self.game_folder, *CACHE, "table.vpp_pc")
        if os.path.isfile(path):
            pf = rf.Packfile.open(path)
            for n in pf.names():
                if n.lower().endswith(".xtbl"):
                    texts[n.lower()] = pf.read(n)
        folders = [os.path.join(self.mod_folder, "overrides", *CACHE, "table.vpp_pc"),
                   os.path.join(self.mod_folder, "mods", "tables"), os.path.join(self.mod_folder, "mods", "guerrilla_world")]
        extra = []
        for f in folders:
            if os.path.isdir(f):
                for n in os.listdir(f):
                    if n.lower().endswith(".xtbl"):
                        with open(os.path.join(f, n), "rb") as fh:
                            extra.append(fh.read())
        return list(texts.items()), extra

    def names(self):
        """Every playable sound's name, sorted (and self.categories: name → its audio_events.xtbl category, or its bank)."""
        if self._names is not None:
            return self._names
        events = self.event_banks()
        found = {}
        game_tables, extra = self._table_texts()
        for low, data in game_tables:
            if low == "audio_events.xtbl":
                text = data.decode("utf-8", "replace")
                for m in re.finditer(r"<NewEntity>\s*<Name>([^<]+)</Name>.*?(?:<Category>([^<]*)</Category>|</NewEntity>)", text, re.S):
                    name = m.group(1).strip()
                    if event_id(name) in events:
                        found[name] = (m.group(2) or "").strip()
        token = re.compile(rb"[A-Za-z][A-Za-z0-9_]{4,}")
        for data in [d for _, d in game_tables] + extra:
            for t in set(token.findall(data)):
                name = t.decode()
                if name not in found and event_id(name) in events:
                    found[name] = ""
        for name in found:
            bank = events.get(event_id(name), "")
            self.banks[name] = bank
            self.types[name] = sound_type(bank, name)
            self.categories[name] = category_words(found[name], bank)
        self._names = sorted(found, key=str.upper)
        return self._names

    def _media_of(self, bank):
        """A media bank's index and where it is: ({id: (start, size)}, source, file name)."""
        key = bank.lower()
        if key not in self._media:
            self._media[key] = ({}, None, None)
            for s in self.sources:
                fn = bank + "_media.bnk_pc"
                if s.has(fn):
                    head = s.read_part(fn, 0, 0x800)
                    index, need = media_index(head)
                    if index is None:
                        index, _ = media_index(s.read_part(fn, 0, need))
                    self._media[key] = (index or {}, s, fn)
                    break
        return self._media[key]

    def medium(self, mid, prefer=None):
        """A medium's .wem bytes, from the bank's own media first, then any bank's."""
        order = ([prefer] if prefer else []) + [n for n, _ in self.bank_names().values() if n != prefer]
        for b in order:
            index, s, fn = self._media_of(b)
            if mid in index:
                start, size = index[mid]
                return s.read_part(fn, start, size)
        return None

    def wav(self, name, pick=0, vgmstream="", work=None):
        """A sound's .wav bytes (with several to pick from, the pick-th), or raise ValueError."""
        eid = event_id(name)
        bank = self.event_banks().get(eid)
        if bank is None:
            raise ValueError(f"no bank has a sound called {name}")
        sounds = self.bank(bank).sounds(eid)
        if not sounds:
            raise ValueError(f"{name} plays nothing that can be heard here (it may only stop or change other sounds)")
        _, mid = sounds[pick % len(sounds)]
        wem = self.medium(mid, prefer=bank)
        if wem is None:
            raise ValueError(f"{name}'s sound file ({mid}) isn't in any media bank")
        return to_wav(wem, vgmstream, work)
