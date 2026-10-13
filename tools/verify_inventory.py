"""Read every file in the pristine C: manifest without changing the baseline."""
from concurrent.futures import ThreadPoolExecutor
from hashlib import sha256
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from cofcoop.bsp import list_maps, load_map_entities


def verify():
    game = ROOT / "baseline" / "Cry of Fear"
    manifest = ROOT / "baseline" / "pristine-manifest.sha256"
    entries = [line.split("  ", 1) for line in manifest.read_text().splitlines() if line.strip()]

    def check(entry):
        expected, relative = entry
        path = (game / relative).resolve()
        if not path.is_relative_to(game.resolve()):
            return {"path": relative, "status": "invalid_path"}
        try:
            digest = sha256()
            with path.open("rb") as handle:
                for block in iter(lambda: handle.read(1024 * 1024), b""):
                    digest.update(block)
            actual = digest.hexdigest()
            if actual.lower() != expected.lower():
                return {"path": relative, "status": "changed", "expected": expected, "actual": actual}
        except OSError as exc:
            return {"path": relative, "status": "unreadable", "error": str(exc)}
        return None

    with ThreadPoolExecutor(max_workers=4) as pool:
        differences = [result for result in pool.map(check, entries) if result]
    maps = list_maps(str(game / "cryoffear" / "maps"))
    report = json.loads((ROOT / "tools" / "map_report.json").read_text())["maps"]
    errors = {}
    counts = {}
    for name, path in sorted(maps.items()):
        try:
            counts[name] = len(load_map_entities(path))
        except Exception as exc:
            errors[name] = str(exc)
    return {"manifest": str(manifest), "checked_files": len(entries),
            "matching_files": len(entries) - len(differences), "differences": differences,
            "maps_checked": len(maps), "map_errors": errors,
            "maps_missing_from_report": sorted(set(maps) - set(report)),
            "report_maps_missing_on_disk": sorted(set(report) - set(maps)),
            "map_entity_counts": counts}


if __name__ == "__main__":
    result = verify()
    output = ROOT / "evidence" / "inventory.json"
    output.parent.mkdir(exist_ok=True)
    output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key != "map_entity_counts"}, indent=2))
