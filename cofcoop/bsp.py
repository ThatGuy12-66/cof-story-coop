"""Read the entity lump of GoldSrc (v30) BSP files without loading the whole map."""
import os
import re
import struct

_PAIR = re.compile(r'"([^"]*)"\s*"([^"]*)"')


def read_entity_text(path):
    with open(path, "rb") as f:
        head = f.read(4 + 15 * 8)
        if len(head) < 124:
            raise ValueError("truncated header")
        version = struct.unpack_from("<i", head, 0)[0]
        if version != 30:
            raise ValueError("not a v30 BSP (version %d)" % version)
        lumps = [struct.unpack_from("<ii", head, 4 + i * 8) for i in range(15)]
        # Standard layout keeps entities in lump 0; Blue Shift-style maps swap 0 and 1.
        for idx in (0, 1):
            off, size = lumps[idx]
            if size <= 0:
                continue
            f.seek(off)
            raw = f.read(size)
            text = raw.split(b"\x00", 1)[0].decode("latin-1", "replace")
            if text.lstrip().startswith("{"):
                return text
    raise ValueError("no entity lump found")


def parse_entities(text):
    ents, cur = [], None
    for line in text.splitlines():
        s = line.strip()
        if s == "{":
            cur = {}
        elif s == "}":
            if cur is not None:
                ents.append(cur)
            cur = None
        elif cur is not None:
            m = _PAIR.match(s)
            if m:
                k, v = m.group(1), m.group(2)
                if k in cur:  # keep duplicates distinguishable
                    n = 2
                    while "%s#%d" % (k, n) in cur:
                        n += 1
                    k = "%s#%d" % (k, n)
                cur[k] = v
    return ents


def load_map_entities(path):
    return parse_entities(read_entity_text(path))


def list_maps(maps_dir):
    out = {}
    for name in os.listdir(maps_dir):
        if name.lower().endswith(".bsp"):
            out[name[:-4].lower()] = os.path.join(maps_dir, name)
    return out
