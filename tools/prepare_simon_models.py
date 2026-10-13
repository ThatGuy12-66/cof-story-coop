"""Derive private Simon multiplayer models from the user's own stock assets.

Default is inspection. --write creates a fresh evidence directory only. Runtime
installation and model visibility/animation validation remain separate steps.
This generator is distributable source; generated MDL/texture files stay local.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cofcoop import core

SIMON_SHA = "7da905907a2204fb5e0de1b9c2b15a37abda9337412352b39ef3d574824fc37a"
POLICE_SHA = "86749c7802175793bae17a0170095fd0c0467d54515002b7b2435bc6cfd15ee1"
TEXTURES = {
    "head_face.bmp": ("Sick_Simon_Face.tga", "22c5a801f66111f78cfb4d2fcd606378fde7bea599cef230e9925e89ae32cd66"),
    "hoodie_grey.bmp": ("Sick_Simon_Hoodie.TGA", "7a92824d8aac79002372722fd23579c021dae3e4a682507c6800732e6e53fc72"),
    "jeans.bmp": ("Sick_Simon_Jeans.TGA", "107c21324e34673c05a5b7fa6a854550803ad3e2f32feb69e354a8653cda10ab"),
}
SEQ_SIZE = 176
MODEL_NAMES = ("cof_s1", "cof_s2")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def _range(data, offset, size, label):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError(f"{label} exceeds model bounds")


def sequences(data):
    count, offset = struct.unpack_from("<ii", data, 164)
    _range(data, offset, count * SEQ_SIZE, "sequence table")
    return [(data[offset + i * SEQ_SIZE:offset + (i + 1) * SEQ_SIZE]) for i in range(count)]


def label(record):
    return record[:32].split(b"\0", 1)[0].decode("ascii")


def texture_info(data):
    count, offset = struct.unpack_from("<ii", data, 180)
    _range(data, offset, count * 80, "texture table")
    result = {}
    for index in range(count):
        start = offset + 80 * index
        name = data[start:start + 64].split(b"\0", 1)[0].decode("ascii").lower()
        flags, width, height, pixels = struct.unpack_from("<4i", data, start + 64)
        if not 0 < width <= 4096 or not 0 < height <= 4096:
            raise ValueError("Invalid texture dimensions")
        _range(data, pixels, width * height + 768, "indexed texture")
        result[name] = {"width": width, "height": height, "offset": pixels, "flags": flags}
    return result


def validate_model(data):
    if len(data) < 244 or data[:4] != b"IDST" or struct.unpack_from("<i", data, 4)[0] != 10:
        raise ValueError("Expected GoldSrc IDST version 10 model")
    if struct.unpack_from("<i", data, 72)[0] != len(data):
        raise ValueError("Model length field differs from file length")
    bones, bone_offset = struct.unpack_from("<ii", data, 140)
    if not 0 < bones <= 128:
        raise ValueError("Invalid bone count")
    _range(data, bone_offset, bones * 112, "bones")
    groups, group_offset = struct.unpack_from("<ii", data, 172)
    if groups != 1:
        raise ValueError("Only an independent model with one in-file animation group is supported")
    _range(data, group_offset, 104, "sequence group")
    animations = 0
    for sequence in sequences(data):
        frames = struct.unpack_from("<i", sequence, 56)[0]
        blends, anim = struct.unpack_from("<ii", sequence, 120)
        group = struct.unpack_from("<i", sequence, 156)[0]
        events, event_offset = struct.unpack_from("<ii", sequence, 48)
        pivots, pivot_offset = struct.unpack_from("<ii", sequence, 60)
        if not 0 < frames <= 100000 or not 0 < blends <= 16 or group != 0:
            raise ValueError("Unsupported sequence animation dimensions/group")
        _range(data, anim, bones * blends * 12, "animation records")
        _range(data, event_offset, events * 76, "events")
        _range(data, pivot_offset, pivots * 20, "pivots")
        for index in range(bones * blends):
            start = anim + index * 12
            for relative in struct.unpack_from("<6H", data, start):
                if not relative:
                    continue
                cursor, covered = start + relative, 0
                while covered < frames:
                    _range(data, cursor, 2, "animation run")
                    valid, total = data[cursor], data[cursor + 1]
                    if not total or valid > total:
                        raise ValueError("Invalid compressed animation run")
                    _range(data, cursor, 2 * (valid + 1), "animation values")
                    covered += total
                    cursor += 2 * (valid + 1)
                animations += 1
    textures = texture_info(data)
    skinrefs, families, skin_offset = struct.unpack_from("<iii", data, 192)
    _range(data, skin_offset, skinrefs * families * 2, "skin families")
    for index in struct.unpack_from("<" + "H" * (skinrefs * families), data, skin_offset):
        if index >= len(textures):
            raise ValueError("Invalid skin texture index")
    return {"sequences": len(sequences(data)), "bones": bones, "validated_animation_channels": animations,
            "textures": len(textures), "skin_families": families}


def mapped_animation(name):
    fixed = {
        "pro": "look_idle", "idle": "idle", "knockout": "die_backwards1",
        "walk": "walk", "run": "run", "sprint": "run2", "stamina": "deep_idle",
        "crouch_crouch": "crawl", "crouch_idle": "crouch_idle", "jump": "jump",
        "death": "die_simple", "revived": "getup1", "cpr_down": "standtosit",
        "cpr_idle": "sitheal", "cpr_up": "sittostand", "fall_down": "die_backwards",
        "fall_get_up": "getup1", "door": "walk", "taller_fall": "long_jump",
        "taller_land": "die_forwards", "taller_lie": "lieonground", "taller_getup": "getup1",
        "pidle1": "idle", "pidle2": "look_idle", "pidle3": "deep_idle", "pidle4": "apidle1",
        "crouch_death": "die_simple", "crouch_revived": "sittostand",
        "crouch_cpr_down": "standtosit", "crouch_cpr_up": "sittostand",
        "crouch_cpr_idle": "sitheal", "crouch_taller_lie": "lieonground",
        "syringe_idle": "ref_aim_trip", "syringe_use": "sitheal",
        "syringe_heal_player_hit": "ref_shoot_trip", "radio_idle": "ref_aim_onehanded",
        "radio_melee": "ref_shoot_crowbar",
    }
    if name in fixed:
        return fixed[name]
    prefix, _, action = name.partition("_")
    if prefix in ("nightstick", "switchblade", "branch", "sledgehammer"):
        return "ref_aim_crowbar" if action == "idle" else "ref_shoot_crowbar"
    weapon = {"shotgun": "shotgun", "m16": "mp5", "tmp": "mp5", "pistol": "onehanded",
              "p345": "onehanded", "vp70": "onehanded", "revolver": "python", "rifle": "bow"}.get(prefix)
    if weapon:
        if action == "melee":
            return "ref_shoot_crowbar"
        return "ref_" + ("shoot_" if action == "shoot" else "aim_") + weapon
    raise ValueError(f"No reviewed mapping for police sequence {name}")


def adapt_sequences(simon: bytes, police: bytes, name=MODEL_NAMES[0]):
    if sha(simon) != SIMON_SHA or sha(police) != POLICE_SHA:
        raise ValueError("Stock Simon/police source hash mismatch")
    validate_model(simon)
    validate_model(police)
    available = {label(record): (index, record) for index, record in enumerate(sequences(simon))}
    if len(sequences(police)) != 85:
        raise ValueError("Expected the native 85-sequence police order")
    output = bytearray(simon)
    output.extend(b"\0" * (-len(output) % 4))
    table = len(output)
    report = []
    for slot, native in enumerate(sequences(police)):
        native_name = label(native)
        chosen = mapped_animation(native_name)
        source_index, source = available[chosen]
        record = bytearray(source)
        encoded = native_name.encode("ascii")
        record[:32] = encoded + b"\0" * (32 - len(encoded))
        output.extend(record)
        report.append({"slot": slot, "native": native_name, "simon": chosen, "source_index": source_index,
                       "exact_label_match": native_name == chosen,
                       "fallback": "reload" in native_name or "cpr" in native_name or native_name in ("door", "syringe_heal_player_hit")})
    encoded_name = (name + ".mdl").encode("ascii")
    if len(encoded_name) >= 64:
        raise ValueError("Model name too long")
    output[8:72] = encoded_name + b"\0" * (64 - len(encoded_name))
    struct.pack_into("<i", output, 72, len(output))
    struct.pack_into("<ii", output, 164, 85, table)
    struct.pack_into("<ii", output, 236, 0, 0)
    result = bytes(output)
    validate_model(result)
    return result, report


def decode_tga(data):
    if len(data) < 18 or data[1] != 0 or data[2] != 2 or data[16] != 24 or data[17] & 0xC0:
        raise ValueError("Expected uncompressed 24-bit true-color TGA")
    width, height = struct.unpack_from("<HH", data, 12)
    offset = 18 + data[0]
    _range(data, offset, width * height * 3, "TGA pixels")
    rgb = []
    for y in range(height):
        source_y = y if data[17] & 0x20 else height - 1 - y
        for x in range(width):
            source_x = width - 1 - x if data[17] & 0x10 else x
            index = offset + (source_y * width + source_x) * 3
            b, g, r = data[index:index + 3]
            rgb.append((r, g, b))
    return width, height, rgb


def quantize(rgb):
    """Deterministic weighted median cut; no image/dependency download required."""
    histogram = Counter(rgb)
    boxes = [list(sorted(histogram))]
    def spread(box):
        spans = [max(color[axis] for color in box) - min(color[axis] for color in box) for axis in range(3)]
        axis = max(range(3), key=lambda i: (spans[i], -i))
        return spans[axis], sum(histogram[color] for color in box), axis
    while len(boxes) < min(256, len(histogram)):
        candidates = [(spread(box), index) for index, box in enumerate(boxes) if len(box) > 1]
        _score, index = max(candidates, key=lambda item: (item[0][:2], -item[1]))
        box = boxes.pop(index)
        _span, population, axis = spread(box)
        box.sort(key=lambda color: (color[axis], color))
        running, split = 0, 1
        for split, color in enumerate(box[:-1], 1):
            running += histogram[color]
            if running * 2 >= population:
                break
        boxes.extend((box[:split], box[split:]))
    palette, lookup = bytearray(), {}
    for index, box in enumerate(boxes):
        count = sum(histogram[color] for color in box)
        palette.extend((sum(color[axis] * histogram[color] for color in box) + count // 2) // count for axis in range(3))
        lookup.update((color, index) for color in box)
    palette.extend(b"\0" * (768 - len(palette)))
    return bytes(lookup[color] for color in rgb), bytes(palette)


def apply_costume(model: bytes, donors: dict[str, bytes]):
    output = bytearray(model)
    report = []
    textures = texture_info(model)
    for texture_name, (filename, expected_hash) in TEXTURES.items():
        tga = donors[filename]
        if sha(tga) != expected_hash:
            raise ValueError(f"Stock costume hash mismatch: {filename}")
        width, height, rgb = decode_tga(tga)
        texture = textures[texture_name]
        if (width, height) != (texture["width"], texture["height"]) or texture["flags"] & 64:
            raise ValueError("Costume dimensions/transparency are incompatible")
        pixels, palette = quantize(rgb)
        start = texture["offset"]
        output[start:start + width * height + 768] = pixels + palette
        report.append({"texture": texture_name, "donor": filename, "width": width, "height": height,
                       "source_unique_colors": len(set(rgb)), "palette_colors": len(set(pixels))})
    validate_model(output)
    return bytes(output), report


def texture_png(model, texture):
    info = texture_info(model)[texture]
    width, height, start = info["width"], info["height"], info["offset"]
    pixels = model[start:start + width * height]
    palette = model[start + width * height:start + width * height + 768]
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for index in pixels[y * width:(y + 1) * width]:
            raw.extend(palette[index * 3:index * 3 + 3])
    def chunk(kind, value):
        return struct.pack(">I", len(value)) + kind + value + struct.pack(">I", zlib.crc32(kind + value))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">2I5B", width, height, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


def build_models(simon, police, donors):
    standard, mapping = adapt_sequences(simon, police)
    alternate, _ = adapt_sequences(simon, police, MODEL_NAMES[1])
    alternate, costume = apply_costume(alternate, donors)
    report = {"source_simon_sha256": SIMON_SHA, "source_police_sha256": POLICE_SHA,
              "model_names": list(MODEL_NAMES),
              "sequence_mapping": mapping, "costume": costume,
              "limitations": ["Reload/CPR/door actions use recorded Simon fallback animations.",
                              "Dimensional texture compatibility is checked; rendered UV fit and two-client animation remain to be verified.",
                              "Generated models are private stock-derived assets and are excluded from packages."]}
    files = {f"models/player/{MODEL_NAMES[0]}/{MODEL_NAMES[0]}.mdl": standard,
             f"models/player/{MODEL_NAMES[1]}/{MODEL_NAMES[1]}.mdl": alternate}
    report["outputs"] = {path: {"sha256": sha(data), "bytes": len(data), **validate_model(data)} for path, data in files.items()}
    for texture in TEXTURES:
        stem = texture.rsplit(".", 1)[0]
        files[f"previews/standard-{stem}.png"] = texture_png(standard, texture)
        files[f"previews/alternate-{stem}.png"] = texture_png(alternate, texture)
    files["model-report.json"] = (json.dumps(report, indent=2) + "\n").encode()
    return files


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, default=core.DEFAULT_GAME_DIR, help="Existing retail game root, read only")
    parser.add_argument("--out", type=Path, default=core.PROJECT_ROOT / "evidence/simon-models-v2")
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args(argv)
    try:
        out = core._local_path(args.out)
        evidence = (core.PROJECT_ROOT / "evidence").resolve()
        if out == evidence or not out.is_relative_to(evidence) or out.exists():
            raise ValueError("Choose a fresh subdirectory of project evidence")
        model_root = args.game_dir / "cryoffear/models"
        files = build_models((model_root / "cutscene/player.mdl").read_bytes(),
                             (model_root / "player/police1/police1.mdl").read_bytes(),
                             {name: (model_root / "costumes/Sick_Simon" / name).read_bytes() for name, _hash in TEXTURES.values()})
        if args.write:
            out.mkdir(parents=True, exist_ok=False)
            for name, data in files.items():
                path = out / name
                path.parent.mkdir(parents=True, exist_ok=True)
                with path.open("xb") as stream:
                    stream.write(data)
        print(json.dumps({"output": str(out), "written": args.write,
                          "models": json.loads(files["model-report.json"])["outputs"]}, indent=2))
    except (ValueError, OSError, KeyError, struct.error) as exc:
        print(f"Model preparation refused: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
