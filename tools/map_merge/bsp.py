"""Half-Life BSP30 files as lists of rows, and back to bytes; the helpers the merger and its check share."""
from __future__ import annotations

import math
from pathlib import Path
import re
import struct

NAMES = ['entities', 'planes', 'textures', 'vertexes', 'visibility', 'nodes', 'texinfo', 'faces',
         'lighting', 'clipnodes', 'leafs', 'marksurfaces', 'edges', 'surfedges', 'models']
FMT = {'planes': '<4fi', 'vertexes': '<3f', 'nodes': '<i2h3h3h2H', 'texinfo': '<8f2i',
       'faces': '<HhiHh4Bi', 'clipnodes': '<i2h', 'leafs': '<2i3h3h2H4B', 'marksurfaces': '<H',
       'edges': '<2H', 'surfedges': '<i', 'models': '<9f4i3i'}
ROWS = tuple(FMT)
WORLD_HALF = 4096.0


class Bsp:
    """A BSP30 file: every fixed-size lump as a list of rows (lists), plus textures, vis, lighting, entities."""

    def __init__(self, data: bytes):
        version = struct.unpack_from('<i', data, 0)[0]
        if version != 30:
            raise ValueError(f'not a BSP30 file (version {version})')
        raw = {}
        for i, name in enumerate(NAMES):
            offset, length = struct.unpack_from('<ii', data, 4 + i * 8)
            raw[name] = data[offset:offset + length]
        for name, fmt in FMT.items():
            size = struct.calcsize(fmt)
            if len(raw[name]) % size:
                raise ValueError(f'lump {name} is not a whole number of rows')
            setattr(self, name, [list(row) for row in struct.iter_unpack(fmt, raw[name])])
        self.vis = raw['visibility']
        self.lighting = raw['lighting']
        self.ents = parse_ents(raw['entities'].split(b'\0')[0].decode('latin-1'))
        self.textures = parse_textures(raw['textures'])

    @classmethod
    def from_bytes(cls, data: bytes) -> 'Bsp':
        return cls(data)

    @classmethod
    def read(cls, path) -> 'Bsp':
        return cls(Path(path).read_bytes())


def to_lumps(x: Bsp) -> dict:
    """The lumps of a Bsp, ready for write_bytes."""
    lumps = {name: getattr(x, name) for name in ROWS}
    lumps.update(entities=serialize_ents(x.ents), textures=build_textures(x.textures), visibility=x.vis,
                 lighting=x.lighting)
    return lumps


def write_bytes(lumps: dict) -> bytes:
    """A BSP30 file from lumps (rows or bytes), each lump 4-byte aligned, in the standard order."""
    blobs = []
    for name in NAMES:
        value = lumps[name]
        if isinstance(value, (bytes, bytearray)):
            blobs.append(bytes(value))
        else:
            blobs.append(b''.join(struct.pack(FMT[name], *row) for row in value))
    pos, table, body = 4 + 15 * 8, [], bytearray()
    for blob in blobs:
        while pos % 4:
            body.append(0)
            pos += 1
        table.append((pos, len(blob)))
        body.extend(blob)
        pos += len(blob)
    return struct.pack('<i', 30) + b''.join(struct.pack('<ii', *entry) for entry in table) + bytes(body)


def entity_blocks(text: str):
    """Each entity's text: from a '{' to the next '}' with no '{' between them, braces inside a quoted value being
    the value's (a decal named "{ding10"), as in the engine's tokenizer and cof_sections.h CoF_SectNextBlock."""
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            end = text.find('"', i + 1)
            i = n if end < 0 else end + 1
            continue
        if c != '{':
            i += 1
            continue
        q = i + 1
        while q < n and text[q] not in '{}':
            if text[q] == '"':
                end = text.find('"', q + 1)
                if end < 0:
                    return
                q = end
            q += 1
        if q >= n:
            return
        if text[q] == '{':
            i = q
            continue
        yield text[i + 1:q]
        i = q + 1


def parse_ents(text: str) -> list:
    return [[list(kv) for kv in re.findall(r'"([^"]*)"\s*"([^"]*)"', block)] for block in entity_blocks(text)]


def serialize_ents(ents: list) -> bytes:
    parts = ['{\n' + ''.join(f'"{k}" "{v}"\n' for k, v in e) + '}\n' for e in ents]
    return ''.join(parts).encode('latin-1') + b'\0'


def ent_get(e: list, key: str, default=None):
    for k, v in e:
        if k == key:
            return v
    return default


def parse_textures(blob: bytes) -> list:
    """Each miptex as the bytes from its offset to the next one (None for a missing texture)."""
    count = struct.unpack_from('<i', blob, 0)[0]
    offsets = list(struct.unpack_from(f'<{count}i', blob, 4))
    valid = sorted(o for o in offsets if o >= 0)
    ends = {o: (valid[i + 1] if i + 1 < len(valid) else len(blob)) for i, o in enumerate(valid)}
    return [blob[o:ends[o]] if o >= 0 else None for o in offsets]


def tex_name(mip) -> str | None:
    return mip[:16].split(b'\0')[0].decode('latin-1').lower() if mip else None


def build_textures(mips: list) -> bytes:
    count = len(mips)
    head = 4 + 4 * count
    offsets, body = [], bytearray()
    for mip in mips:
        if mip is None:
            offsets.append(-1)
            continue
        while (head + len(body)) % 4:
            body.append(0)
        offsets.append(head + len(body))
        body.extend(mip)
    return struct.pack(f'<i{count}i', count, *offsets) + bytes(body)


def decompress_vis(vis: bytes, offset: int, rowbytes: int) -> bytes:
    out = bytearray()
    i = offset
    while len(out) < rowbytes:
        b = vis[i]
        i += 1
        if b:
            out.append(b)
        else:
            out.extend(b'\0' * vis[i])
            i += 1
    return bytes(out[:rowbytes])


def compress_vis(row: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(row):
        if row[i]:
            out.append(row[i])
            i += 1
            continue
        run = 0
        while i < len(row) and row[i] == 0 and run < 255:
            run += 1
            i += 1
        out += bytes((0, run))
    return bytes(out)


def bits_of(row: bytes, count: int) -> list:
    return [k for k in range(count) if row[k >> 3] & (1 << (k & 7))]


def f32(x: float) -> float:
    return struct.unpack('<f', struct.pack('<f', x))[0]


def axis_box(vertexes, edges, surfedges, face, tex, j):
    """Lightmap cells of a face along texture axis j, computed as the engine does
    (Mod_CalcSurfaceExtents: single precision, floor/ceil of value / 16)."""
    r = 4 * j
    lo, hi = 1e9, -1e9
    for i in range(face[3]):
        se = surfedges[face[2] + i][0]
        v = vertexes[edges[se][0] if se >= 0 else edges[-se][1]]
        val = f32(v[0] * tex[r] + v[1] * tex[r + 1] + v[2] * tex[r + 2] + tex[r + 3])
        lo, hi = min(lo, val), max(hi, val)
    return math.floor(f32(lo / 16)), math.ceil(f32(hi / 16))


def face_box(vertexes, edges, surfedges, face, tex):
    return tuple(axis_box(vertexes, edges, surfedges, face, tex, j) for j in (0, 1))


def styles_of(face) -> int:
    return sum(1 for s in face[5:9] if s != 255)


def relay(src: bytes, old, new, nstyles: int) -> bytes:
    """Lightmap data laid out for box `new` from data laid out for box `old` (each ((smin, smax), (tmin, tmax))
    in 16-unit cells). Cells are at the same texture-space places in both; cells only in `new` copy the
    nearest edge cell, so a box that grew by a cell looks exactly like before."""
    ow, oh = old[0][1] - old[0][0] + 1, old[1][1] - old[1][0] + 1
    nw, nh = new[0][1] - new[0][0] + 1, new[1][1] - new[1][0] + 1
    out = bytearray()
    for st in range(nstyles):
        for tt in range(nh):
            ot = min(max(new[1][0] + tt, old[1][0]), old[1][1]) - old[1][0]
            for ss in range(nw):
                os_ = min(max(new[0][0] + ss, old[0][0]), old[0][1]) - old[0][0]
                o = (st * ow * oh + ot * ow + os_) * 3
                out += src[o:o + 3]
    return bytes(out)
