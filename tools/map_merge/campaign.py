"""Every exit of the story campaign through the recorded groups, offline.

Each trigger_changelevel of each story map is routed as the engine routes it (cof_sections.h CoF_SectionsRoute): a
move to another section of the loaded merged map, or a map file to load and the landmark's name there. Then what the
engine does with it, compared with the same exit unmerged:
- a load with a landmark is a story transition, refused unless that landmark is once in the map left and once in the
  map loaded (sv_save.c SV_CoF_TransitionValidate);
- a load without one puts the players at the file's first info_player_start: section 0's
  (sv_cof_sections.c SV_CoF_SectionsEntry moves it only for a map command);
- a section move without the landmark puts them at the section's start, as the original game spawned at the map's.
A problem is an exit that works unmerged and not merged; a note, one the original maps already have.

    python -m tools.map_merge.campaign MAPS_DIR
"""
from __future__ import annotations

from dataclasses import dataclass, field
import json
from pathlib import Path
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.map_merge import bsp, groups
from tools.map_merge.bsp import ent_get

ROOT = Path(__file__).resolve().parents[2]
STORY_MAPS = ROOT / 'evidence/finish-20261012/research/D-map-chains.json'


@dataclass
class Exit:
    map: str
    level: str
    landmark: str


@dataclass
class Route:
    section_move: bool
    to_section: int
    file: str
    dest_landmark: str
    source_landmark: str


@dataclass
class Report:
    exits: int = 0
    moves: int = 0
    merged_loads: int = 0
    plain_loads: int = 0
    problems: list = field(default_factory=list)
    notes: list = field(default_factory=list)


def landmark_section(sections: list, name: str) -> int:
    return next((i for i, s in enumerate(sections) if name in s.landmarks), -1)


def find(sections: list, level: str) -> int:
    return next((i for i, s in enumerate(sections) if s.map.lower() == level.lower()), -1)


def route(loaded: tuple | None, level: str, landmark: str, dest: tuple | None) -> Route:
    """CoF_SectionsRoute. loaded, dest: (file name, sections) or None."""
    original = landmark or ''
    out = Route(False, -1, level, original, original)
    if loaded and original and (k := landmark_section(loaded[1], original)) >= 0:
        original = next((old for old, new in loaded[1][k].renames.items() if new == original), original)
    if loaded and (to := find(loaded[1], level)) >= 0:
        out.section_move, dest = True, loaded
    elif not dest or (to := find(dest[1], level)) < 0:
        out.dest_landmark = original
        return out
    out.to_section, out.file = to, dest[0]
    out.dest_landmark = dest[1][to].renames.get(original, original)
    return out


def story_maps() -> list:
    return sorted(json.loads(STORY_MAPS.read_text())['maps'])


def landmarks_of(ents: list) -> list:
    return [ent_get(e, 'targetname') for e in ents if ent_get(e, 'classname') == 'info_landmark' and ent_get(e, 'targetname')]


def story_exits(ents_by_map: dict) -> list:
    return [Exit(m, ent_get(e, 'map'), ent_get(e, 'landmark') or '') for m, ents in sorted(ents_by_map.items())
            for e in ents if ent_get(e, 'classname') == 'trigger_changelevel' and ent_get(e, 'map')]


def check(ents_by_map: dict, merged: dict) -> Report:
    """ents_by_map: story map -> its entities; merged: file name -> its sections (rules.Section, in order)."""
    report = Report()
    file_of = {s.map: (name, secs) for name, secs in merged.items() for s in secs}
    own = {m: landmarks_of(ents) for m, ents in ents_by_map.items()}

    def landmarks_in(file: str) -> list:
        return [x for s in merged[file] for x in s.landmarks] if file in merged else own.get(file, [])

    for x in story_exits(ents_by_map):
        report.exits += 1
        where = f'{x.map} -> {x.level} [{x.landmark or "no landmark"}]'
        if x.level not in own:
            report.notes.append(f'{where}: not a story map')
            continue
        unmerged_ok = not x.landmark or (own[x.map].count(x.landmark) == 1 and own[x.level].count(x.landmark) == 1)
        loaded = file_of.get(x.map)
        asked = loaded[1][find(loaded[1], x.map)].renames.get(x.landmark, x.landmark) if loaded else x.landmark
        r = route(loaded, x.level, asked, file_of.get(x.level))
        if r.section_move:
            report.moves += 1
            if x.landmark and r.dest_landmark not in loaded[1][r.to_section].landmarks:
                (report.problems if x.landmark in own[x.level] else report.notes).append(
                    f'{where}: a section move without the landmark, to the section\'s start')
            continue
        if r.to_section >= 0:
            report.merged_loads += 1
        else:
            report.plain_loads += 1
        if not x.landmark:
            if r.to_section > 0:
                report.problems.append(f'{where}: loads {r.file} at its first start, section 0 '
                                       f'({merged[r.file][0].map}), not {x.level}')
            continue
        merged_ok = (landmarks_in(loaded[0] if loaded else x.map).count(asked) == 1
                     and landmarks_in(r.file).count(r.dest_landmark) == 1)
        if not merged_ok:
            (report.problems if unmerged_ok else report.notes).append(
                f'{where}: refused (the landmark is not once on both sides{"" if unmerged_ok else ", unmerged too"})')
    return report


def recorded(maps_dir: Path) -> dict:
    """The enabled groups' sections, from the members in maps_dir (only the rules: no merge)."""
    out = {}
    for g in groups.load_groups():
        if g.enabled:
            out[g.name] = groups.plan_sections([(m, bsp.Bsp.read(Path(maps_dir) / f'{m}.bsp')) for m in g.members])
    return out


def main(argv: list) -> int:
    maps_dir = Path(argv[0])
    ents_by_map = {m: bsp.Bsp.read(maps_dir / f'{m}.bsp').ents for m in story_maps()}
    merged = recorded(maps_dir)
    report = check(ents_by_map, merged)
    print(f'{report.exits} exits: {report.moves} section moves, {report.merged_loads} loads of a merged file, '
          f'{report.plain_loads} of a plain map; {len(merged)} groups')
    for p in report.problems:
        print('PROBLEM', p)
    for n in report.notes:
        print('note', n)
    return 1 if report.problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
