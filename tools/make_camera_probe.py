"""Prepare a private final-sequence BSP from the user's own c_nightmare map.

The default is read-only. --write creates a new cof_probe_camera.bsp and refuses
to replace an existing file. This retains the final effects/teleports, not the
opening camera sequence; no game commands or process control are performed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cofcoop import core
from cofcoop.bsp import parse_entities


SOURCE_SHA256 = "c6f78abb7ebc8299c62db69a018f652bdd9f35915cb90dd2d46257699c00ba65"
MAP_NAME = "cof_probe_camera"
START_INDEXES = (104, 158, 283, 442)
START_ORIGIN = "-741 -1420 -282"
START_ANGLES = "0 270 0"
BOOTSTRAP_TARGET = "probe_end_delay"
_ENTITY = re.compile(rb"(?m)^[ \t]*\{[ \t]*\r?\n.*?^[ \t]*\}[ \t]*(?:\r?\n|$)", re.DOTALL)
BOOTSTRAP = (
    b'{\n"classname" "trigger_once"\n"model" "*125"\n'
    b'"origin" "0 -49 0"\n"target" "probe_end_delay"\n}\n'
    b'{\n"classname" "multi_manager"\n"targetname" "probe_end_delay"\n'
    b'"multispotea" "5"\n}\n'
)


def _lumps(data: bytes) -> list[tuple[int, int]]:
    if len(data) < 124 or struct.unpack_from("<i", data)[0] != 30:
        raise ValueError("Expected a complete GoldSrc version 30 BSP.")
    result = [struct.unpack_from("<ii", data, 4 + index * 8) for index in range(15)]
    for offset, size in result:
        if offset < 0 or size < 0 or offset + size > len(data):
            raise ValueError("BSP contains an invalid lump range.")
    return result


def _entities(data: bytes) -> list[dict[str, str]]:
    offset, size = _lumps(data)[0]
    return parse_entities(data[offset:offset + size].split(b"\0", 1)[0].decode("latin-1"))


def _set_existing_value(entity: bytes, key: str, value: str) -> bytes:
    pattern = re.compile(rb'(?m)^(\s*"' + key.encode("ascii") + rb'"\s*")[^"]*(")')
    changed, count = pattern.subn(lambda match: match[1] + value.encode("ascii") + match[2], entity)
    if count != 1:
        raise ValueError(f"Expected exactly one {key!r} field in each player start.")
    return changed


def build_probe(source: bytes) -> bytes:
    """Pure transformation: preserve original lump bytes and all 580 entities.

    Original entity records are copied as raw text, including duplicate keys;
    only four starts' origin/angles values change. The two new entities retain
    the touching player as activator through the five-second manager delay.
    """
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise ValueError("Source is not the verified original c_nightmare BSP.")
    lumps = _lumps(source)
    offset, size = lumps[0]
    raw = source[offset:offset + size].split(b"\0", 1)[0]
    matches = list(_ENTITY.finditer(raw))
    entities = _entities(source)
    if len(matches) != 580 or len(entities) != 580:
        raise ValueError("Expected the complete 580-entity source map.")
    actual_starts = tuple(index for index, entity in enumerate(entities) if entity.get("classname") == "info_player_start")
    if actual_starts != START_INDEXES:
        raise ValueError("Source player start indexes differ from the verified map.")
    chunks, cursor = [], 0
    for index, match in enumerate(matches):
        chunks.append(raw[cursor:match.start()])
        record = match[0]
        if index in START_INDEXES:
            record = _set_existing_value(record, "origin", START_ORIGIN)
            record = _set_existing_value(record, "angles", START_ANGLES)
        chunks.append(record)
        cursor = match.end()
    chunks.append(raw[cursor:])
    entity_blob = b"".join(chunks).rstrip(b"\r\n") + b"\n" + BOOTSTRAP + b"\0"
    result = bytearray(source)
    result.extend(b"\0" * (-len(result) % 4))
    struct.pack_into("<ii", result, 4, len(result), len(entity_blob))
    result.extend(entity_blob)
    output = bytes(result)

    expected = [dict(entity) for entity in entities]
    for index in START_INDEXES:
        expected[index].update(origin=START_ORIGIN, angles=START_ANGLES)
    expected.extend(parse_entities(BOOTSTRAP.decode("ascii")))
    if _entities(output) != expected or len(expected) != 582:
        raise ValueError("Generated entity records did not match the planned 582-entity map.")
    # Appending the replacement entity lump changes only its eight-byte header;
    # worldspawn/WAD fields, duplicate keys, geometry, and every other lump stay.
    if output[:4] != source[:4] or output[12:len(source)] != source[12:]:
        raise ValueError("An original BSP byte outside the entity header changed.")
    if _lumps(output)[1:] != lumps[1:]:
        raise ValueError("A non-entity lump header changed.")
    return output


def _reject_aliases(path: Path) -> None:
    for candidate in (path, *path.parents):
        if candidate.is_symlink() or (hasattr(candidate, "is_junction") and candidate.is_junction()):
            raise ValueError(f"Probe paths cannot contain links or junctions: {candidate}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game-dir", type=Path, default=core.DEFAULT_GAME_DIR)
    parser.add_argument("--write", action="store_true", help="Create the new probe map; default only verifies and reports")
    args = parser.parse_args(argv)
    try:
        # Check lexical ownership before probing any protected path.
        root = core.validate_game_dir(args.game_dir)
        if root.drive.casefold() != "c:":
            raise ValueError("This diagnostic can create maps only in a C: development copy.")
        _reject_aliases(args.game_dir.expanduser().absolute())
        source = root / "cryoffear" / "maps" / "c_nightmare.bsp"
        target = source.with_name(MAP_NAME + ".bsp")
        _reject_aliases(source)
        _reject_aliases(target)
        if not source.resolve().is_relative_to(root) or not target.resolve().is_relative_to(root):
            raise ValueError("Probe paths must stay inside the copied game directory.")
        if target.exists():
            raise ValueError(f"Probe already exists; refusing to replace it: {target}")
        if source.stat().st_nlink != 1:
            raise ValueError("Source BSP must not be hard-linked.")
        original = source.read_bytes()
        result = build_probe(original)
        report = {
            "map": MAP_NAME, "source": str(source), "target": str(target),
            "source_sha256": hashlib.sha256(original).hexdigest(),
            "probe_sha256": hashlib.sha256(result).hexdigest(),
            "original_entities": 580, "probe_entities": 582,
            "relocated_player_starts": list(START_INDEXES),
            "start_origin": START_ORIGIN, "trigger_translation": "0 -49 0",
            "final_chain_target": "multispotea", "delay_seconds": 5,
            "written": False,
        }
        if args.write:
            # Exclusive creation permits a new unloaded map during another map's
            # session and prevents accidental replacement if a race creates it.
            with target.open("xb") as stream:
                stream.write(result)
                stream.flush()
                os.fsync(stream.fileno())
            if target.read_bytes() != result:
                raise RuntimeError("New probe file failed byte-exact write verification.")
            report["written"] = True
        print(json.dumps(report, indent=2))
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"Probe refused: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
