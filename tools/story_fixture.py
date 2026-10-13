"""Generate private Enhanced .ent test overrides; never install or launch them."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cofcoop import core
from cofcoop.bsp import parse_entities

SOURCES = {
    "c_start": "72d1520baf7950ba056c6bc53310612c8582e83488a4df690199045e67815415",
    "c_start2": "3933456e6c0e8a449ec17e7829c9d69a8880a1e414b54dd6c0cf2b361483157e",
}
LANDMARK = "hahaland"
SPAWNS = {"c_start": [639, -2953, -728], "c_start2": [2428, -883, -728]}
LANDMARK_ORIGINS = {"c_start": [635, -2978, -694], "c_start2": [2424, -908, -694]}
GUEST_POSITIONS = {name: [origin[0], origin[1] + 40, origin[2]] for name, origin in SPAWNS.items()}
SENTINEL = "cof_probe_once"
MARKER = "cof_probe_marker"


def inspect_spawn(source: bytes, position: list[int]) -> dict:
    """Check the original BSP leaf, standing hull, and visible leaf textures."""
    lumps = [struct.unpack_from("<ii", source, 4 + 8 * i) for i in range(15)]
    def at(lump, relative, size):
        offset, length = lumps[lump]
        if relative < 0 or size < 0 or relative + size > length or offset + relative + size > len(source):
            raise ValueError("Invalid BSP geometry reference")
        return offset + relative
    def side(plane):
        x, y, z, distance = struct.unpack_from("<4f", source, at(1, plane * 20, 20))
        return 0 if sum(a * b for a, b in zip((x, y, z), position)) >= distance else 1
    world = at(14, 0, 64)
    node = struct.unpack_from("<i", source, world + 36)[0]
    visits = 0
    while node >= 0:
        offset = at(5, node * 24, 24)
        plane, front, back = struct.unpack_from("<i2h", source, offset)
        node = (front, back)[side(plane)]
        visits += 1
        if visits > 100000:
            raise ValueError("Cyclic BSP tree")
    leaf_index = -1 - node
    leaf = at(10, leaf_index * 28, 28)
    contents = struct.unpack_from("<i", source, leaf)[0]
    first, count = struct.unpack_from("<2H", source, leaf + 20)
    textures = set()
    for index in range(first, first + count):
        face = struct.unpack_from("<H", source, at(11, index * 2, 2))[0]
        texinfo = struct.unpack_from("<h", source, at(7, face * 20 + 10, 2))[0]
        miptex = struct.unpack_from("<i", source, at(6, texinfo * 40 + 32, 4))[0]
        texture = struct.unpack_from("<i", source, at(2, 4 + miptex * 4, 4))[0]
        if texture >= 0:
            offset = at(2, texture, 16)
            textures.add(source[offset:offset + 16].split(b"\0", 1)[0].decode("latin-1"))
    node = struct.unpack_from("<i", source, world + 40)[0]
    visits = 0
    while node >= 0:
        plane, front, back = struct.unpack_from("<i2h", source, at(9, node * 8, 8))
        node = (front, back)[side(plane)]
        visits += 1
        if visits > 100000:
            raise ValueError("Cyclic BSP hull")
    return {"leaf": leaf_index, "point_contents": contents, "standing_hull_contents": node,
            "leaf_textures": sorted(textures)}


def _entity(entity: dict[str, str]) -> str:
    if any('"' in value or '\n' in value or '\r' in value for value in entity.values()):
        raise ValueError("Invalid entity field")
    return "{\n" + "\n".join(f'"{key}" "{value}"' for key, value in entity.items()) + "\n}\n"


def make_override(source: bytes, name: str) -> bytes:
    """Keep the exact retail worldspawn/WAD, replacing only entity records.

    No geometry is generated or copied. Use alongside the user's original BSP.
    Per-map playable spawns and synthetic landmarks produce a nonzero delta.
    """
    if name not in SOURCES or hashlib.sha256(source).hexdigest() != SOURCES[name]:
        raise ValueError("Unsupported source map or modified BSP")
    if struct.unpack_from("<i", source)[0] != 30:
        raise ValueError("Expected version 30 BSP")
    for position in (SPAWNS[name], GUEST_POSITIONS[name]):
        spawn_check = inspect_spawn(source, position)
        if (spawn_check["point_contents"] != -1 or spawn_check["standing_hull_contents"] != -1
                or not any(texture.upper() != "BLACK" for texture in spawn_check["leaf_textures"])):
            raise ValueError("Fixture spawn is solid, empty of surfaces, or in an all-black staging leaf")
    offset, size = struct.unpack_from("<ii", source, 4)
    raw = source[offset:offset + size].split(b"\0", 1)[0].decode("latin-1")
    match = re.match(r"\s*(\{\s*\n.*?^\}\s*\n)", raw, re.MULTILINE | re.DOTALL)
    if not match or parse_entities(match[1])[0].get("classname") != "worldspawn":
        raise ValueError("Original worldspawn is missing")
    model = "*97" if name == "c_start" else "*4"
    destination = "c_start2" if name == "c_start" else "c_start"
    entities = [
        {"classname": "info_player_start", "origin": " ".join(map(str, SPAWNS[name])), "angles": "0 90 0"},
        {"classname": "info_player_start", "origin": " ".join(map(str, GUEST_POSITIONS[name])), "angles": "0 90 0"},
        {"classname": "info_landmark", "targetname": LANDMARK, "origin": " ".join(map(str, LANDMARK_ORIGINS[name]))},
        {"classname": "trigger_changelevel", "model": model, "targetname": "cof_probe_exit",
         "map": destination, "landmark": LANDMARK, "spawnflags": "2"},
    ]
    if name == "c_start":
        entities.extend([
            {"classname": "trigger_once", "model": model, "targetname": SENTINEL, "target": "cof_probe_grants"},
            {"classname": "multi_manager", "targetname": "cof_probe_grants",
             "cof_probe_mobile": "0", "cof_probe_knife": "0.1"},
            {"classname": "cof_giveitems", "targetname": "cof_probe_mobile", "message": "weapon_mobile"},
            {"classname": "cof_giveitems", "targetname": "cof_probe_knife", "message": "weapon_switchblade"},
            {"classname": "info_target", "targetname": MARKER, "origin": " ".join(map(str, LANDMARK_ORIGINS[name])), "health": "17"},
        ])
    output = (match[1] + "".join(_entity(entity) for entity in entities)).encode("latin-1")
    if parse_entities(output.decode("latin-1"))[0] != parse_entities(raw)[0]:
        raise ValueError("Worldspawn was not preserved")
    return output


def build_fixture(sources: dict[str, bytes]) -> dict[str, bytes]:
    overrides = {name + ".ent": make_override(sources[name], name) for name in SOURCES}
    manifest = {
        "schema": 2, "private_local_fixture": True, "maps": list(SOURCES),
        "source_bsp_sha256": SOURCES,
        "overrides_sha256": {name: hashlib.sha256(data).hexdigest() for name, data in overrides.items()},
        "spawns": SPAWNS,
        "recommended_guest_positions": GUEST_POSITIONS,
        "spawn_geometry": {name: inspect_spawn(sources[name], SPAWNS[name]) for name in SOURCES},
        "landmark": {"name": LANDMARK, "origins": LANDMARK_ORIGINS, "synthetic_origins": True},
        "sentinel": SENTINEL, "marker": MARKER, "marker_initial_health": 17,
        "marker_changed_health": 73,
        "required_inventory": ["weapon_mobile", "weapon_switchblade"],
        "map_sequence": ["c_start", "c_start2", "c_start"],
        "client_count": 2, "arrival_tolerance": 8.0,
    }
    overrides["fixture.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    overrides["fixture.cfg"] = (
        "// Isolated Enhanced test runtime only; execute before map c_start.\n"
        "cof_story_coop 1\nsv_lan 1\ncoop 0\ndeathmatch 1\nmaxplayers 2\n"
        "sv_cheats 1\nsv_enttools_enable 1\nsv_enttools_maxfire 5\n"
        "mp_timelimit 0\nmp_fraglimit 0\n"
        "fps_max 60\n"
    ).encode()
    return overrides


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=core.PROJECT_ROOT / "evidence" / "story-fixture-v2")
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args(argv)
    try:
        out = core._local_path(args.out)
        evidence = (core.PROJECT_ROOT / "evidence").resolve()
        if not out.is_relative_to(evidence) or out == evidence:
            raise ValueError("Fixture output must be a new subdirectory of project evidence")
        if out.exists():
            raise ValueError("Output already exists; choose a fresh evidence directory")
        sources = {name: (core.DEFAULT_GAME_DIR / "cryoffear" / "maps" / (name + ".bsp")).read_bytes() for name in SOURCES}
        files = build_fixture(sources)
        if args.write:
            out.mkdir(parents=True, exist_ok=False)
            for name, data in files.items():
                with (out / name).open("xb") as stream:
                    stream.write(data)
        print(json.dumps({"output": str(out), "written": args.write,
                          "files": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                                    for name, data in files.items()}}, indent=2))
    except (ValueError, OSError) as exc:
        print(f"Fixture refused: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
