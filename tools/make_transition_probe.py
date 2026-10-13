"""Create an isolated instant-exit map from local assets; never distribute it."""
from pathlib import Path
import json
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cofcoop import core
from cofcoop.bsp import load_map_entities


def main():
    root = core.DEFAULT_GAME_DIR
    source = root / "cryoffear/maps/c_intro.bsp"
    target = source.with_name("cof_probe_api2.bsp")
    if core.running_game_processes(root):
        raise RuntimeError("Close the game before preparing the diagnostic map")
    if target.exists():
        raise RuntimeError("Diagnostic map already exists; inspect it before replacing")
    original = bytearray(source.read_bytes())
    entities = [
        dict(load_map_entities(source)[0]),
        {"classname": "info_player_start", "origin": "211 -405 -2530", "angles": "0 90 0"},
        {"classname": "trigger_once", "model": "*141", "target": "probe_manager"},
        {"classname": "multi_manager", "targetname": "probe_manager", "probe_exit": "5"},
        {"classname": "trigger_changelevel", "model": "*141", "spawnflags": "2", "targetname": "probe_exit", "map": "c_start", "landmark": "landakukej"},
        {"classname": "info_landmark", "targetname": "landakukej", "origin": "212 -408 -2523"},
    ]
    blob = ("\n".join("{\n" + "\n".join(f'"{key.split(chr(35), 1)[0]}" "{value}"' for key, value in entity.items()) + "\n}" for entity in entities) + "\n\0").encode("latin-1")
    struct.pack_into("<ii", original, 4, len(original), len(blob))
    original.extend(blob)
    target.write_bytes(original)
    parsed = load_map_entities(target)
    if parsed != entities:
        raise RuntimeError("Diagnostic entity verification failed")
    report = {"map": target.stem, "source": str(source), "path": str(target), "entities": entities,
              "purpose": "Five seconds after the player touches its starting volume, use trigger_changelevel to c_start; no camera sequence."}
    out = core.PROJECT_ROOT / "evidence/transition-probe.json"
    out.write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
