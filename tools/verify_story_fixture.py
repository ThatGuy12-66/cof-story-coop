"""Offline assertions for observed two-client, out-and-back Enhanced state.

Input is diagnostic snapshots of actual game state, not desired values. A map
name or transition log alone deliberately cannot satisfy this verifier.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
import math
from pathlib import Path
import re
import shlex
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.story_fixture import LANDMARK_ORIGINS

PHASES = ("initial", "before_outbound", "after_outbound", "before_return", "after_return")
MAPS = ("c_start", "c_start", "c_start2", "c_start2", "c_start")
REQUIRED_ITEMS = {"weapon_mobile", "weapon_switchblade"}
STARTER_DESCRIPTORS = {
    "inventoryitems/weapons/weapon_mobile.txt": "weapon_mobile",
    "inventoryitems/weapons/weapon_switchblade.txt": "weapon_switchblade",
}
DESCRIPTOR_PATH = re.compile(r"inventoryitems/(?:[A-Za-z0-9_-]+/)*[A-Za-z0-9_-]+\.txt")


def parse_status(text: str, phase: str) -> dict:
    """Normalize one complete cof_story_status response, using private slots.

    Inventory keys retain the actual descriptor paths from usable private slots
    1..6; slot 0 must be empty. Owned-edict and legacy classname observations
    cannot substitute for those descriptors. Timestamps are ignored.
    """
    records = []
    for line in text.splitlines():
        match = re.search(r"\bcofstory\b\]?\s+(.*)", line)
        if not match:
            continue
        lexer = shlex.shlex(match[1], posix=True)
        lexer.whitespace_split = True
        lexer.commenters = ""
        # Engine diagnostic paths are literal, not shell-escaped strings.
        lexer.escape = ""
        fields = {}
        for token in lexer:
            if "=" not in token:
                raise ValueError("Malformed status field")
            key, value = token.split("=", 1)
            if not key or key in fields:
                raise ValueError("Invalid or duplicate status field")
            fields[key] = value
        if fields.get("phase") == "status":
            records = [fields]
        elif records and records[-1].get("phase") != "status-end":
            records.append(fields)
    if not records or records[-1].get("phase") != "status-end":
        raise ValueError("A complete latest cof_story_status block is required")
    header = records[0]
    if header.get("enabled") != "1":
        raise ValueError("Story compatibility is not enabled in this observation")
    def vector(value):
        return _vector([float(part) for part in value.split(",")])
    players = {}
    inventories = {}
    inventory_slots = {}
    fixture = {}
    landmark = None
    for record in records[1:]:
        kind = record.get("phase")
        if kind == "player":
            userid = int(record["userid"])
            if userid in players:
                raise ValueError("Duplicate observed player")
            players[userid] = {"userid": userid, "slot": int(record["slot"]),
                               "ready": record["ready"] == "1", "origin": vector(record["origin"]),
                               "door_flag": int(record["door"]),
                               "effects": 128 if record["hidden"] == "1" else 0,
                               "health": float(record["health"]), "fakeclient": record["fakeclient"] == "1",
                               "frozen": record["frozen"] == "1"}
        elif kind == "inventory-slot":
            if set(record) != {"phase", "userid", "slot", "index", "descriptor"}:
                raise ValueError("Actual inventory-slot descriptor fields are required")
            userid = int(record["userid"])
            slot, index = int(record["slot"]), int(record["index"])
            observed = inventory_slots.setdefault(userid, {})
            if index not in range(7) or index in observed:
                raise ValueError("Invalid or duplicate private inventory index")
            observed[index] = slot
            inventory = inventories.setdefault(userid, Counter())
            descriptor = record["descriptor"]
            if descriptor:
                if index == 0:
                    raise ValueError("Unused private inventory index 0 must be empty")
                _descriptor(descriptor)
                inventory[descriptor] += 1
        elif kind == "fixture":
            name = record["name"]
            if name in fixture:
                raise ValueError("Duplicate fixture targetname in observation")
            fixture[name] = record
        elif kind == "landmark" and record.get("name") == "hahaland":
            if landmark is not None:
                raise ValueError("Duplicate hahaland landmark")
            landmark = {"name": "hahaland", "origin": vector(record["origin"])}
    if landmark is None:
        raise ValueError("Actual hahaland coordinates were not observed")
    for userid, player in players.items():
        observed = inventory_slots.get(userid, {})
        if set(observed) != set(range(7)) or any(slot != player["slot"] for slot in observed.values()):
            raise ValueError(f"Incomplete or mismatched private inventory-slot observations for userid {userid}")
        player["inventory"] = dict(inventories[userid])
    if set(inventories) != set(players):
        raise ValueError("Inventory observation refers to an unobserved player")
    marker = fixture.get("cof_probe_marker")
    return {"phase": phase, "map": header["map"], "generation": int(header["generation"]),
            "landmark": landmark, "players": list(players.values()),
            "world": {"sentinel_name": "cof_probe_once", "sentinel_present": "cof_probe_once" in fixture,
                      "marker_name": "cof_probe_marker", "marker_health": float(marker["health"]) if marker else None}}


def _vector(value):
    if not isinstance(value, list) or len(value) != 3 or any(isinstance(x, bool) or not isinstance(x, (int, float)) or not math.isfinite(x) for x in value):
        raise ValueError("Expected three finite coordinates")
    return value


def _descriptor(value):
    if not isinstance(value, str) or not DESCRIPTOR_PATH.fullmatch(value):
        raise ValueError("Inventory descriptor must be a canonical inventoryitems/.../*.txt path")
    return value


def _inventory(value):
    if not isinstance(value, dict) or any(isinstance(count, bool) or not isinstance(count, int) or count <= 0 for count in value.values()):
        raise ValueError("Inventory must contain observed descriptor:positive-count pairs")
    for descriptor in value:
        _descriptor(descriptor)
    return Counter(value)


def verify(snapshots: list[dict], *, tolerance: float = 8.0) -> dict:
    """Fail closed when evidence is absent, identities change, or state resets."""
    errors = []
    if not isinstance(snapshots, list) or len(snapshots) != len(PHASES):
        return {"passed": False, "errors": ["Exactly five ordered state snapshots are required"]}
    if not math.isfinite(tolerance) or not 0 <= tolerance <= 32:
        raise ValueError("Arrival tolerance must be finite and between 0 and 32 units")
    players_by_phase = {}
    for snapshot, phase, map_name in zip(snapshots, PHASES, MAPS):
        try:
            if snapshot["phase"] != phase or snapshot["map"] != map_name:
                errors.append(f"{phase}: wrong phase/map order")
            generation = snapshot["generation"]
            if isinstance(generation, bool) or not isinstance(generation, int) or generation < 1:
                raise ValueError("Invalid map generation")
            if snapshot["landmark"]["name"] != "hahaland":
                errors.append(f"{phase}: wrong landmark")
            _vector(snapshot["landmark"]["origin"])
            if snapshot["landmark"]["origin"] != LANDMARK_ORIGINS[map_name]:
                errors.append(f"{phase}: fixture v2 landmark coordinates are required")
            players = snapshot["players"]
            if not isinstance(players, list) or len(players) != 2:
                raise ValueError("Two actual connected clients are required")
            indexed = {}
            slots = set()
            for player in players:
                userid, slot = player["userid"], player["slot"]
                if isinstance(userid, bool) or not isinstance(userid, int) or userid < 1:
                    raise ValueError("Client userid must be a positive integer")
                if isinstance(slot, bool) or not isinstance(slot, int) or slot < 0:
                    raise ValueError("Client slot must be a nonnegative integer")
                if userid in indexed or slot in slots:
                    raise ValueError("Duplicate client userid or slot")
                if player["fakeclient"] is not False or player["ready"] is not True:
                    errors.append(f"{phase}: userid {userid} is not an actual signed-on client")
                if player["door_flag"] != 0 or player["effects"] & 0x80 or player["frozen"] is not False:
                    errors.append(f"{phase}: userid {userid} is hidden, frozen, or stuck in a door")
                health = player["health"]
                if isinstance(health, bool) or not isinstance(health, (float, int)) or not math.isfinite(health) or health <= 0:
                    raise ValueError("Expected a live client's finite health")
                _vector(player["origin"])
                inventory = _inventory(player["inventory"])
                starter_classes = {STARTER_DESCRIPTORS[descriptor] for descriptor in inventory if descriptor in STARTER_DESCRIPTORS}
                if phase != "initial" and not REQUIRED_ITEMS <= starter_classes:
                    errors.append(f"{phase}: userid {userid} lacks phone or knife")
                indexed[userid] = player
                slots.add(slot)
            players_by_phase[phase] = indexed
        except (KeyError, TypeError, ValueError) as exc:
            errors.append(f"{phase}: incomplete/invalid state evidence: {exc}")
    if errors:
        return {"passed": False, "errors": errors}
    identities = set(players_by_phase["initial"])
    for phase, players in players_by_phase.items():
        if set(players) != identities:
            errors.append(f"{phase}: client identities changed or a replacement reused a slot")
    if errors:
        return {"passed": False, "errors": errors}
    before_players = list(players_by_phase["before_outbound"].values())
    if before_players[0]["inventory"] == before_players[1]["inventory"]:
        errors.append("before_outbound: distinct inventory markers are needed to detect swapping the clients' state")
    if before_players[0]["origin"] == before_players[1]["origin"]:
        errors.append("before_outbound: distinct client positions are needed to detect stacked/restored spawn state")
    for before_index, after_index in ((1, 2), (3, 4)):
        before, after = snapshots[before_index], snapshots[after_index]
        before_phase, after_phase = before["phase"], after["phase"]
        if after["generation"] <= before["generation"]:
            errors.append(f"{after_phase}: destination generation did not advance")
        for userid in identities:
            src = players_by_phase[before_phase][userid]
            dst = players_by_phase[after_phase][userid]
            if _inventory(src["inventory"]) != _inventory(dst["inventory"]):
                errors.append(f"{after_phase}: userid {userid} inventory changed")
            if abs(src["health"] - dst["health"]) > 0.01:
                errors.append(f"{after_phase}: userid {userid} health changed")
            expected = [src["origin"][axis] + after["landmark"]["origin"][axis] - before["landmark"]["origin"][axis] for axis in range(3)]
            if any(abs(dst["origin"][axis] - expected[axis]) > tolerance for axis in range(3)):
                errors.append(f"{after_phase}: userid {userid} did not retain landmark-relative position")
    if snapshots[2]["generation"] != snapshots[3]["generation"]:
        errors.append("before_return: an unrecorded map generation intervened")
    for index, expected_presence, expected_health in ((0, True, 17), (1, False, 73), (4, False, 73)):
        snapshot = snapshots[index]
        try:
            world = snapshot["world"]
            if world["sentinel_name"] != "cof_probe_once" or world["marker_name"] != "cof_probe_marker":
                errors.append(f"{snapshot['phase']}: wrong world sentinel identity")
            if world["sentinel_present"] is not expected_presence or world["marker_health"] != expected_health:
                errors.append(f"{snapshot['phase']}: consumed trigger/modified hub marker state is incorrect")
        except (KeyError, TypeError):
            errors.append(f"{snapshot['phase']}: missing actual world sentinel observation")
    return {"passed": not errors, "errors": errors, "client_userids": sorted(identities),
            "checks": ["two real clients", "stable identities", "both inventories", "health", "landmark arrival", "consumed trigger", "hub marker", "visible/control flags"]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("observations", type=Path, nargs="?")
    parser.add_argument("--logs", type=Path, nargs=5, metavar="STATUS_LOG", help="Five status captures in the documented phase order")
    parser.add_argument("--tolerance", type=float, default=8.0)
    args = parser.parse_args(argv)
    try:
        if bool(args.observations) == bool(args.logs):
            raise ValueError("Choose either normalized JSON observations or five --logs captures")
        data = ([parse_status(path.read_text(encoding="utf-8", errors="replace"), phase) for path, phase in zip(args.logs, PHASES)]
                if args.logs else json.loads(args.observations.read_text(encoding="utf-8")))
        result = verify(data["snapshots"] if isinstance(data, dict) else data, tolerance=args.tolerance)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        result = {"passed": False, "errors": [str(exc)]}
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
