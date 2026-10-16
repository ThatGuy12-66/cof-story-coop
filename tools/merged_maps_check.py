"""Scripted checks of merged story maps in a game copy (a developer tool; it starts the game: ask first).

    python -m tools.merged_maps_check doors COPY GROUP [--out DIR]
        put the player into every door between the group's sections, one after another; each must move them to
        the other section by the landmark offset (the game's log tells where they landed)
    python -m tools.merged_maps_check pictures COPY GROUP [--out DIR] [--spots N]
        the same spots in the merged map and in each original map, snapshots compared pixel by pixel (needs pillow
        and numpy: uv run --no-project --with pillow --with numpy python -m tools.merged_maps_check ...)

The run turns sv_cheats and the entity tools on for itself (god, notarget, ent_fire set origin) and puts the
copy's normal story settings back afterwards. Method: AGENT_NOTES "Renderer patch live" (2026-10-16).
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import math
from pathlib import Path
import random
import re
import shutil
import sys
import time

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.map_merge import bsp, check, rules
from tools.map_merge.bsp import ent_get

SCRIPT = "cof_check.cfg"
MARK = re.compile(r"merged-check (?P<kind>door|spot) (?P<index>\d+)")
MOVE = re.compile(r"phase=section-move userid=\d+ from=(?P<src>\S+) to=(?P<dst>\S+) landmark=\S* origin=(?P<origin>[-\d.,]+)")
NEAR = 64.0    # a landing this close to the landmark offset's spot is right (stuck spots move a little)
EYE = 28.0


@dataclass
class Exit:
    src: str
    dst: str
    landmark: str
    box: tuple             # (mins, maxs) of the trigger to stand in, merged map coordinates
    stand: tuple           # where the player is put (inside that trigger)
    expected: tuple | None # where the move should land them (None: the destination's start, no landmarks)


@dataclass
class DoorResult:
    exit: Exit
    verdict: str           # ok, landed-off, wrong-section, no-move
    origin: tuple | None
    distance: float | None


def origin_of(e: list) -> tuple | None:
    try:
        x, y, z = (float(v) for v in ent_get(e, "origin", "").split())
    except ValueError:
        return None
    return x, y, z


def landmark_origins(ents: list) -> dict:
    found: dict = {}
    for e in ents:
        if ent_get(e, "classname") == "info_landmark" and ent_get(e, "targetname"):
            found.setdefault(ent_get(e, "targetname"), []).append(origin_of(e))
    return {name: spots[0] for name, spots in found.items() if len(spots) == 1 and spots[0]}


def section_exits(named: list, sections: list) -> list:
    """Every level door from one member to another: the trigger a player stands in to use it (the one that fires
    it by name, else the trigger_changelevel itself), in merged coordinates, and where the move should land them."""
    members = [m for m, _ in named]
    marks = [landmark_origins(x.ents) for _, x in named]
    out = []
    for k, (name, x) in enumerate(named):
        dz = sections[k].dz
        for e in x.ents:
            dest = ent_get(e, "map", "")
            if ent_get(e, "classname") != "trigger_changelevel" or dest not in members:
                continue
            d, landmark, label = members.index(dest), ent_get(e, "landmark", ""), ent_get(e, "targetname")
            fires = [t for t in x.ents if label and ent_get(t, "target") == label
                     and ent_get(t, "classname", "").startswith("trigger_") and ent_get(t, "model", "").startswith("*")]
            source = fires[0] if fires else e
            if not ent_get(source, "model", "").startswith("*"):
                continue
            model = x.models[int(ent_get(source, "model")[1:])]
            lo = (model[0], model[1], model[2] + dz)
            hi = (model[3], model[4], model[5] + dz)
            stand = ((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, min(lo[2] + 37.0, hi[2]))
            src, dst = marks[k].get(landmark), marks[d].get(landmark)
            expected = None
            if src and dst:
                expected = tuple(stand[i] + (dst[i] - src[i]) for i in range(3))
                expected = (expected[0], expected[1], expected[2] + sections[d].dz - dz)
            out.append(Exit(name, dest, landmark, (lo, hi), stand, expected))
    return out


def number(v: float) -> str:
    return f"{v:g}"


def door_script(exits: list) -> str:
    lines = ["wait 900", "god", "notarget", "wait 60"]
    for i, e in enumerate(exits):
        lines += [f"echo merged-check door {i} {e.src} {e.dst} {e.landmark}",
                  f'ent_fire 1 set origin "{number(e.stand[0])} {number(e.stand[1])} {number(e.stand[2])}"',
                  "wait 180"]
    lines += ["echo merged-check done", "wait 30", "quit"]
    return "\n".join(lines) + "\n"


def door_results(log: str, exits: list) -> list:
    segments: dict = {}
    current = None
    for line in log.splitlines():
        if m := MARK.search(line):
            current = int(m["index"]) if m["kind"] == "door" else None
            if current is not None:
                segments[current] = []
        elif current is not None:
            segments[current].append(line)
    out = []
    for i, e in enumerate(exits):
        moves = [m for line in segments.get(i, []) if (m := MOVE.search(line))]
        move = moves[0] if moves else None
        if not move:
            out.append(DoorResult(e, "no-move", None, None))
            continue
        if len(moves) > 1:
            # landed in the door back, which sent them straight back: players would bounce between them
            out.append(DoorResult(e, "bounced", tuple(float(v) for v in move["origin"].split(",")), None))
            continue
        origin = tuple(float(v) for v in move["origin"].split(","))
        if move["dst"] != e.dst:
            out.append(DoorResult(e, "wrong-section", origin, None))
            continue
        distance = math.dist(origin, e.expected) if e.expected else None
        out.append(DoorResult(e, "ok" if distance is None or distance <= NEAR else "landed-off", origin, distance))
    return out


# ---- pictures ----

def open_spots(x: bsp.Bsp, count: int, seed: int = 20261016) -> list:
    """Standing spots inside a map (its own coordinates), each looking down its longest free line."""
    rng = random.Random(seed)
    lo, hi = x.models[0][0:3], x.models[0][3:6]
    empty = lambda p: x.leafs[check.leaf_at(x, x.models[0][9], p)][0] == -1
    spots, tries = [], 0
    while len(spots) < count and tries < 200000:
        tries += 1
        p = [rng.uniform(lo[i] + 32, hi[i] - 32) for i in range(3)]
        if not empty(p):
            continue
        z = p[2]
        while z > p[2] - 400 and empty((p[0], p[1], z)):
            z -= 4
        if z <= p[2] - 400:
            continue
        eye = (p[0], p[1], z + 64)
        if not all(empty((eye[0] + dx, eye[1] + dy, eye[2] + dz)) for dx in (-20, 20) for dy in (-20, 20) for dz in (-20, 20)):
            continue
        if any(math.dist(eye, s[0]) < 256 for s in spots):
            continue
        best, yaw = 0, 0.0
        for k in range(16):
            a = 2 * math.pi * k / 16
            d = 0
            while d < 1200 and empty((eye[0] + d * math.cos(a), eye[1] + d * math.sin(a), eye[2])):
                d += 16
            if d > best:
                best, yaw = d, math.degrees(a)
        if best >= 256:
            spots.append((eye, yaw))
    return spots


def picture_script(spots: list, dz: float) -> str:
    lines = ["wait 900", "god", "notarget", "noclip", "hud_draw 0", "r_drawviewmodel 0", "wait 60"]
    for i, (eye, yaw) in enumerate(spots):
        lines += [f"echo merged-check spot {i}",
                  f'ent_fire 1 set origin "{number(eye[0])} {number(eye[1])} {number(eye[2] + dz - EYE)}"',
                  f"ent_fire 1 angles 8 {number(yaw)} 0", "ent_fire 1 set fixangle 1", "wait 150", "snapshot", "wait 30"]
    lines += ["hud_draw 1", "r_drawviewmodel 1", "echo merged-check done", "wait 60", "quit"]
    return "\n".join(lines) + "\n"


def compare_pictures(merged: list, original: list) -> list:
    """Per spot: the share of pixels more than 16/255 apart, and the mean brightness (console text masked)."""
    import numpy as np
    from PIL import Image
    out = []
    for a_path, b_path in zip(merged, original):
        a, b = (np.asarray(Image.open(p).convert("RGB")).astype(np.float32) for p in (a_path, b_path))
        a[:80], b[:80], a[-60:], b[-60:] = 0, 0, 0, 0
        out.append((float((np.abs(a - b).max(axis=2) > 16).mean() * 100), float(b.mean())))
    return out


# ---- running the game ----

def run_game(copy: Path, map_name: str, merged: bool, script: str, timeout: float = 600.0) -> str:
    """Start the copy on map_name with the script, wait for it to quit, return the session log."""
    from tools import coop_app as app
    from tools import coop_play as play
    from tools import friend_setup as setup_lib
    setup_lib.validate_runtime(copy)
    if setup_lib.game_is_running(copy):
        raise SystemExit("The game is already running in this copy.")
    (copy / "cryoffear" / SCRIPT).write_text(script, encoding="ascii")
    settings = play.load_settings(copy)
    args, config = play.host_launch(copy, solo=True, name=play.clean_name(settings["name"] or "Player"), map_name=map_name,
                                    players=2, password="", companions=1, port=play.DEFAULT_PORT, cont=False,
                                    video=play.video_flags(False, app.desktop_size()), merged=merged)
    config = config.replace("cof_companion 1\n", "cof_companion 0\n")
    story = play.story_config().replace("sv_cheats 0", "sv_cheats 1").replace("sv_enttools_enable 0", "sv_enttools_enable 1")
    (copy / "cryoffear" / play.STORY_CFG).write_text(story, encoding="ascii")
    (copy / "cryoffear" / play.SESSION_CFG).write_text(config, encoding="ascii")
    setup_lib.keep_previous_log(copy, play.LOG)
    try:
        app.start_game(copy, list(args) + ["+exec", SCRIPT])
        deadline = time.time() + timeout
        time.sleep(5)
        while time.time() < deadline and setup_lib.game_is_running(copy):
            time.sleep(2)
        return (copy / play.LOG).read_text(encoding="latin-1", errors="replace")
    finally:
        (copy / "cryoffear" / play.STORY_CFG).write_text(play.story_config(), encoding="ascii")
        (copy / "cryoffear" / SCRIPT).unlink(missing_ok=True)


def load_group(copy: Path, name: str) -> tuple:
    maps = copy / "cryoffear" / "maps"
    _, sections = rules.parse((maps / f"{name}.sections").read_text(encoding="ascii"))
    named = [(s.map, bsp.Bsp.read(maps / f"{s.map}.bsp")) for s in sections]
    return named, sections


def doors(copy: Path, name: str, out: Path) -> int:
    named, sections = load_group(copy, name)
    exits = section_exits(named, sections)
    log = run_game(copy, sections[0].map, True, door_script(exits))
    (out / f"doors-{name}.log").write_text(log, encoding="latin-1")
    results = door_results(log, exits)
    for r in results:
        where = f"{r.distance:.0f} units off" if r.distance is not None else ("start spot" if r.verdict == "ok" else "")
        print(f"{r.verdict:13} {r.exit.src} -> {r.exit.dst} [{r.exit.landmark}] {where}")
    bad = [r for r in results if r.verdict != "ok"]
    print(f"{len(results) - len(bad)} of {len(results)} doors ok")
    return 1 if bad else 0


def pictures(copy: Path, name: str, out: Path, spots_per_map: int) -> int:
    named, sections = load_group(copy, name)
    rows = []
    for (member, x), s in zip(named, sections):
        spots = open_spots(x, spots_per_map)
        for merged in (True, False):
            for old in copy.glob("*.png"):
                old.unlink()
            run_game(copy, member, merged, picture_script(spots, s.dz if merged else 0.0))
            folder = out / f"{member}-{'merged' if merged else 'original'}"
            folder.mkdir(parents=True, exist_ok=True)
            for shot in sorted(copy.glob("*.png")):
                shutil.move(str(shot), folder / shot.name)
        merged_shots = sorted((out / f"{member}-merged").glob("*.png"))
        original_shots = sorted((out / f"{member}-original").glob("*.png"))
        for i, (diff, light) in enumerate(compare_pictures(merged_shots, original_shots)):
            rows.append((diff, light, member, i))
            print(f"{member} spot {i}: {diff:.3f}% of pixels differ, brightness {light:.1f}")
    worst = max((r[0] for r in rows), default=0.0)
    print(f"{len(rows)} spots, worst {worst:.3f}% of pixels differ")
    return 0 if rows and worst < 1.0 else 1


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("check", choices=("doors", "pictures"))
    parser.add_argument("copy", type=Path)
    parser.add_argument("group")
    parser.add_argument("--out", type=Path, default=Path("evidence") / "merged-maps-check")
    parser.add_argument("--spots", type=int, default=8)
    args = parser.parse_args(argv)
    args.out.mkdir(parents=True, exist_ok=True)
    copy = args.copy.resolve()
    if args.check == "doors":
        return doors(copy, args.group, args.out)
    return pictures(copy, args.group, args.out, args.spots)


if __name__ == "__main__":
    sys.exit(main())
