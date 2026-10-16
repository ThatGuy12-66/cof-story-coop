"""The planner (a developer tool, not run by setup): can these story maps be one merged map?

    python -m tools.map_merge.plan MAPS_DIR c_apartment3 c_apartment4 [...]
    python -m tools.map_merge.plan MAPS_DIR --graph

It merges the maps for real and checks every limit the merged map must stay within (file format, the renderer,
the engine, the game DLL) and every story rule the design depends on. Problems block a group; notes are for the
person adding it.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import sys

from tools.map_merge import bsp, groups, rules
from tools.map_merge.bsp import ent_get, face_box

# hl.dll reads these worldspawn keys; a merged map has only its first member's worldspawn.
HL_WORLD_KEYS = ('player1', 'player2', 'player3', 'player4', 'materialstxt', 'MaxRange', 'WaveHeight', 'sounds')
LIMITS = {
    'planes': 65535, 'nodes': 32767, 'clipnodes': 32767, 'leafs': 32767, 'faces': 65535, 'marksurfaces': 65535,
    'vertexes': 65535, 'texinfo': 32767,
    'lightmap pages': 256,      # client.dll renderer with the engine's limits patch (cof_renderer_limits.c)
    'entities': 2048,           # of 4096 edicts (liblist -num_edicts), half kept for players, items, gibs
    'models': 3500,             # MAX_MODELS 4096, room for the game DLL's own precaches
    'sounds': 1500,             # MAX_SOUNDS 2048
    'info_node': 1024,          # hl.dll MAX_NODES (cmp ecx, 0x400 before "cNodes > MAX_NODES")
}


@dataclass
class PlanReport:
    members: list
    problems: list = field(default_factory=list)
    notes: list = field(default_factory=list)
    numbers: dict = field(default_factory=dict)

    @property
    def ok(self) -> bool:
        return not self.problems


def lightmap_pages(x: bsp.Bsp, size: int = 128) -> int:
    """Pages the engine's packer (public/atlas.c, every lit face in file order) needs at 128x128."""
    count, alloc = 1, [0] * size
    for f in x.faces:
        tex = x.texinfo[f[4]]
        if tex[9] & 1 or f[9] < 0:
            continue
        (s0, s1), (t0, t1) = face_box(x.vertexes, x.edges, x.surfedges, f, tex)
        w, h = s1 - s0 + 1, t1 - t0 + 1
        for _ in range(2):
            best, at, i = size, None, 0
            while i <= size - w:
                best2, j = 0, 0
                while j < w:
                    if alloc[i + j] >= best:
                        break
                    best2 = max(best2, alloc[i + j])
                    j += 1
                if j == w:
                    at, best = i, best2
                    if best == 0:
                        break
                    i += 1
                else:
                    i += j + 1
            if at is not None and best + h <= size:
                for k in range(w):
                    alloc[at + k] = best + h
                break
            count, alloc = count + 1, [0] * size
    return count


def precaches(ents: list, suffixes: tuple) -> set:
    return {v.lower() for e in ents for _, v in e if v.lower().endswith(suffixes)}


def number(value: str) -> bool:
    try:
        float(value)
        return True
    except ValueError:
        return False


def reference_problems(members: list, sections: list) -> list:
    """Keys outside the rename list whose value is a renamed name: the engine would not rename them."""
    out = []
    for (name, ents), s in zip(members, sections):
        for e in ents:
            cls = ent_get(e, 'classname', '')
            mm = set(rules.multi_manager_targets(e)) if cls == 'multi_manager' else set()
            for k, v in e:
                if k in rules.REF_KEYS or (k == 'message' and cls in rules.PATH_MESSAGE_CLASSES):
                    continue
                if k.split('#')[0] in mm or number(v) or v not in s.renames:
                    continue
                out.append(f'{name}: {cls} key {k}={v} names a renamed entity but is not renamed')
    return out


def globalname_problems(members: list) -> list:
    seen = {}
    for name, ents in members:
        for e in ents:
            g = ent_get(e, 'globalname')
            if g:
                seen.setdefault(g, set()).add(name)
    return [f'globalname {g} is in {", ".join(sorted(m))}: its state would not carry between sections'
            for g, m in sorted(seen.items()) if len(m) > 1]


def plan_group(maps_dir: Path, members: list, accepted: dict | None = None) -> PlanReport:
    accepted = accepted or {}
    report = PlanReport(members=list(members))
    for m in members:
        if groups.hl_named(m):
            report.problems.append(f'{m} is named by the game DLL (or is the menu map) and is never merged')
    if report.problems:
        return report
    maps = [bsp.Bsp.read(Path(maps_dir) / f'{m}.bsp') for m in members]
    named = list(zip(members, [x.ents for x in maps]))
    try:
        merged, sections = groups.merge_members('cm_' + members[0][2:], list(zip(members, maps)))
    except (ValueError, groups.MergeCheckFailed) as error:
        report.problems.append(f'merge: {error}')
        return report

    n = report.numbers
    for key in ('planes', 'nodes', 'clipnodes', 'leafs', 'faces', 'marksurfaces', 'vertexes', 'texinfo'):
        n[key] = len(getattr(merged, key))
    n['lightmap pages'] = lightmap_pages(merged)
    n['entities'] = sum(len(x.ents) for x in maps) - (len(maps) - 1)
    n['models'] = len(merged.models) + len(precaches([e for x in maps for e in x.ents], ('.mdl', '.spr')))
    n['sounds'] = len(precaches([e for x in maps for e in x.ents], ('.wav',)))
    n['info_node'] = sum(1 for x in maps for e in x.ents if ent_get(e, 'classname', '').startswith('info_node'))
    for key, limit in LIMITS.items():
        if key in n and n[key] > limit:
            report.problems.append(f'{key}: {n[key]} over the limit {limit}')

    worlds = [dict((k, v) for k, v in x.ents[0]) for x in maps]
    if len({w.get('skyname', '').lower() for w in worlds}) > 1:
        report.problems.append('skyname differs: ' + ', '.join(f'{m}={w.get("skyname")}' for m, w in zip(members, worlds)))
    for key in HL_WORLD_KEYS:
        values = [w.get(key) for w in worlds]
        if len(set(values)) > 1 and key not in accepted:
            report.problems.append(f'worldspawn {key} differs: ' + ', '.join(f'{m}={v}' for m, v in zip(members, values)))
    fogs = [sorted(tuple(sorted((k, v) for k, v in e if k not in ('origin', 'targetname')))
                   for e in x.ents if ent_get(e, 'classname') == 'env_fog') for x in maps]
    if any(f != fogs[0] for f in fogs[1:]):
        report.problems.append('env_fog differs between members')
    report.problems += globalname_problems(named)
    report.problems += reference_problems(named, sections)

    landmarks = [[ent_get(e, 'targetname') for e in x.ents if ent_get(e, 'classname') == 'info_landmark'] for x in maps]
    for k, x in enumerate(maps):
        for e in x.ents:
            if ent_get(e, 'classname') != 'trigger_changelevel' or ent_get(e, 'map') not in members:
                continue
            dest, landmark = ent_get(e, 'map'), ent_get(e, 'landmark')
            d = members.index(dest)
            if landmarks[k].count(landmark) != 1 or landmarks[d].count(landmark) != 1:
                report.problems.append(f'door {members[k]} -> {dest}: landmark {landmark} is not exactly once on both sides')
            else:
                report.notes.append(f'door {members[k]} -> {dest} via {landmark}')
            if ent_get(e, 'changetarget'):
                report.notes.append(f'door {members[k]} -> {dest} fires changetarget {ent_get(e, "changetarget")}')
    report.notes.append('dz ' + ', '.join(f'{s.map}={s.dz:g}' for s in sections))
    report.notes.append('renames ' + ', '.join(f'{s.map}={len(s.renames)}' for s in sections))
    report.notes.append('styles ' + ', '.join(f'{s.map}={s.styles}' for s in sections if s.styles))
    return report


def door_graph(maps_dir: Path, maps: list) -> dict:
    out = {}
    for m in maps:
        x = bsp.Bsp.read(Path(maps_dir) / f'{m}.bsp')
        out[m] = {ent_get(e, 'map') for e in x.ents if ent_get(e, 'classname') == 'trigger_changelevel' and ent_get(e, 'map')}
    return out


def main(argv: list) -> int:
    maps_dir = Path(argv[0])
    if argv[1:] == ['--graph']:
        names = sorted(p.stem for p in maps_dir.glob('c_*.bsp'))
        for m, dests in door_graph(maps_dir, names).items():
            if dests:
                print(f'{m} -> {", ".join(sorted(dests))}')
        return 0
    report = plan_group(maps_dir, argv[1:])
    print('OK' if report.ok else 'REFUSED', ' + '.join(report.members))
    for key, value in report.numbers.items():
        limit = LIMITS.get(key)
        print(f'  {key:15} {value:7}' + (f'  / {limit}' if limit else ''))
    for p in report.problems:
        print('  PROBLEM', p)
    for note in report.notes:
        print('  note', note)
    return 0 if report.ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
