"""The planner (a developer tool, not run by setup): can these story maps be one merged map?

    python -m tools.map_merge.plan MAPS_DIR c_apartment3 c_apartment4 [...]
    python -m tools.map_merge.plan MAPS_DIR --graph
    python -m tools.map_merge.plan MAPS_DIR --record WAVE c_apartment3 c_apartment4 [...]   (into map_groups.json)

It merges the maps for real and checks every limit the merged map must stay within (file format, the renderer,
the engine, the game DLL) and every story rule the design depends on. Problems block a group; notes are for the
person adding it.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass, field
import hashlib
import json
from pathlib import Path
import sys

from tools.map_merge import bsp, groups, merge, rules
from tools.map_merge.bsp import ent_get, face_box

# hl.dll reads these worldspawn keys; a merged map has only its first member's worldspawn.
HL_WORLD_KEYS = ('player1', 'player2', 'player3', 'player4', 'materialstxt', 'MaxRange', 'WaveHeight', 'sounds')
# Differences that never matter, and why.
WORLD_KEYS_IGNORED = {
    key: 'story co-op sets every player model in the engine (sv_cof_story.c SV_CoF_StoryUserinfo); '
         'worldspawn models are not shown' for key in ('player1', 'player2', 'player3', 'player4')}
# Keys whose absence means a value: equal once the absent value is filled in. sounds is hl.dll's CD track: none sends
# nothing, 1 is "blank" in the engine's default playlist (cl_game.c CL_CreatePlaylist; Cry of Fear has no
# media/cdaudio.txt), so neither plays a track.
WORLD_KEY_DEFAULTS = {'materialstxt': 'sound/materials.txt', 'WaveHeight': '0', 'sounds': '1'}
# cof_sections.h: COF_SECT_RENAMES, COF_SECT_STYLES, COF_SECT_LANDMARKS a section; COF_SECT_NAME with its 0.
SECTION_LIMITS = {'renames': 512, 'styles': 32, 'landmarks': 64}
SECTION_NAME_MAX = 63
# hl.dll takes the map's first entity of these classes (FindEntityByClassname( NULL, ... )): the keypad, phone and
# computer checks, the puzzles, the statues, the bosses that drive a scene. In two members the second member's would be
# checked against, or driven by, the first's (docs/research/2026-10-18-cof-decompilation-survey.md 5.3).
FIRST_OF_CLASS = ('cof_keypad', 'cof_phonecheck', 'cof_computer', 'cof_telephone', 'cof_puzzlebar', 'cof_puzzlebar2',
                  'cof_puzzlebar3', 'cof_puzzlebar4', 'cof_puzzlebar5', 'cof_puzzlebarbutton', 'statue_owl', 'statue_lion',
                  'statue_horse', 'statue_eagle', 'statue_puzzle_complete', 'cof_numeralplate_right', 'monster_doctorboss',
                  'monster_booksimon', 'trigger_booksimon', 'wheelchair_hide_spot', 'boat_hide_spot', 'cof_clothesmenu')
# Not here: subtitle_main, also taken first, only prints the line its caller sets from the one subtitles.txt.
# Names found by a fixed string: hl.dll fires the player events (FireTargets in multiplay_gamerules.cpp and player.cpp
# of the decompilation) and looks up the stress effects' entities (effects.cpp); the engine finds c_roofs2's yes/no
# question (sv_cof_moments.c). A renamed copy is never found, and a player event fires on every section's players.
FIXED_NAMES = ('ryesno', 'stressbump', 'stresswater', 'stresslight', 'stressweather')
PLAYER_EVENT_NAMES = ('game_playerjoin', 'game_playerspawn', 'game_playerdie', 'game_playerkill', 'game_playerleave')
# CChangeLevel's ChangeList holds this many (map, landmark) pairs a map (and writes one past its array first).
TRANSITION_PAIRS = 16
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
                if rules.names_entity(cls, k):
                    continue
                if k.split('#')[0] in mm or number(v) or v not in s.renames:
                    continue
                out.append(f'{name}: {cls} key {k}={v} names a renamed entity but is not renamed')
    return out


def classes_in(members: list, cls: str) -> list:
    return [name for name, ents in members if any(ent_get(e, 'classname') == cls for e in ents)]


def first_of_class_problems(members: list) -> list:
    return [f'{cls} is in {", ".join(having)}: the game uses the map\'s first one for all of them'
            for cls in FIRST_OF_CLASS if len(having := classes_in(members, cls)) > 1]


def transition_pair_problems(members: list) -> list:
    pairs = {(ent_get(e, 'map'), ent_get(e, 'landmark')) for _, ents in members for e in ents
             if ent_get(e, 'classname') == 'trigger_changelevel'}
    if len(pairs) <= TRANSITION_PAIRS:
        return []
    return [f'{len(pairs)} (map, landmark) transition pairs: the game holds {TRANSITION_PAIRS} a map']


def weather_notes(members: list) -> list:
    """Rain, 3D skies and fog are each section's own in the engine (sv_cof_weather.c, sv_cof_sky.c, sv_cof_fog.c), and
    multiplayer draws the black sky whatever skyname says (client.dll CheckSky): notes, not problems."""
    notes = []
    for classes, what in ((('rain_settings',), 'rain'), (('envpos_sky', 'envpos_world'), '3D sky')):
        having = sorted({name for cls in classes for name in classes_in(members, cls)}, key=[n for n, _ in members].index)
        if having:
            notes.append(f'{what} in {", ".join(having)}: each section gets its own')
    worlds = [next((dict(e) for e in ents if ent_get(e, 'classname') == 'worldspawn'), {}) for _, ents in members]
    if len({w.get('skyname', '').lower() for w in worlds}) > 1:
        notes.append('skyname differs (' + ', '.join(f'{name}={w.get("skyname")}' for (name, _), w in zip(members, worlds))
                     + '): multiplayer draws the black sky')
    fogs = [sorted(tuple(sorted((k, v) for k, v in e if k not in ('origin', 'targetname')))
                   for e in ents if ent_get(e, 'classname') == 'env_fog') for _, ents in members]
    if any(f != fogs[0] for f in fogs[1:]):
        notes.append('env_fog differs between members: each section gets its own')
    return notes


def fixed_name_problems(members: list, sections: list) -> list:
    out = [f'{s.map}: {name} would be renamed, but it is found by that name'
           for s in sections for name in (*FIXED_NAMES, *PLAYER_EVENT_NAMES) if name in s.renames]
    if len(members) > 1:
        out += [f'{name}: {", ".join(having)} define it, and every section\'s players would fire it'
                for name in PLAYER_EVENT_NAMES
                if (having := [m for m, ents in members if name in rules.defined_names(ents)])]
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


def world_value(key: str, value: str | None):
    """A worldspawn value as hl.dll sees it, for comparing members."""
    value = WORLD_KEY_DEFAULTS.get(key) if value is None else value
    if key == 'WaveHeight' and value is not None and number(value):
        return float(value)
    if key == 'materialstxt' and value is not None:
        return value.replace('\\', '/').lower()
    return value


def world_key_problems(members: list, worlds: list, accepted: dict) -> tuple:
    """(problems, notes) for the worldspawn keys hl.dll reads; worlds are dicts. MaxRange may differ: the merged
    worldspawn takes the largest (merge.merged_worldspawn)."""
    problems, notes = [], []
    for key in HL_WORLD_KEYS:
        values = [w.get(key) for w in worlds]
        if len(set(values)) < 2:
            continue
        listed = ', '.join(f'{m}={v}' for m, v in zip(members, values))
        if key in WORLD_KEYS_IGNORED or key in accepted:
            notes.append(f'worldspawn {key} differs ({listed}): {WORLD_KEYS_IGNORED.get(key) or accepted[key]}')
        elif key == 'MaxRange':
            if all(v is None or number(v) for v in values):
                world = merge.merged_worldspawn([[['MaxRange', v]] if v is not None else [] for v in values])
                notes.append(f'worldspawn MaxRange differs ({listed}): the merged map takes '
                             f'{ent_get(world, "MaxRange", "the default")}')
            else:
                problems.append(f'worldspawn MaxRange is not a number: {listed}')
        elif len({world_value(key, v) for v in values}) > 1:
            problems.append(f'worldspawn {key} differs: {listed}')
    return problems, notes


def section_limit_problems(sections: list) -> list:
    """What the engine's .sections reader (cof_sections.h) cannot hold: it refuses the whole file."""
    out = []
    for s in sections:
        for what, limit in SECTION_LIMITS.items():
            count = len(getattr(s, what))
            if count > limit:
                out.append(f'{s.map}: {count} {what}, the engine holds {limit} a section')
        names = [s.map, *s.renames, *s.renames.values(), *s.landmarks]
        long = [n for n in names if len(n) > SECTION_NAME_MAX]
        if long:
            out.append(f'{s.map}: {long[0]} is longer than the engine\'s {SECTION_NAME_MAX} characters')
    return out


def door_checks(members: list, landmarks: list, member_ents: list) -> tuple:
    """Every level door between two members: its landmark once on both sides (a section move keeps the
    position relative to it), or on neither (the original game spawns at the map start; so does the move)."""
    problems, notes = [], []
    for k, ents in enumerate(member_ents):
        for e in ents:
            if ent_get(e, 'classname') != 'trigger_changelevel' or ent_get(e, 'map') not in members:
                continue
            dest, landmark = ent_get(e, 'map'), ent_get(e, 'landmark')
            here, there = landmarks[k].count(landmark), landmarks[members.index(dest)].count(landmark)
            if here == there == 1:
                notes.append(f'door {members[k]} -> {dest} via {landmark}')
            elif here == there == 0:
                notes.append(f'door {members[k]} -> {dest}: landmark {landmark} is in neither map, '
                             f'players arrive at the {dest} start as in the original game')
            else:
                problems.append(f'door {members[k]} -> {dest}: landmark {landmark} is not exactly once on both sides')
            if ent_get(e, 'changetarget'):
                # hl.dll fires it in the destination map, but the composer renames it by the source's rules
                problems.append(f'door {members[k]} -> {dest} fires changetarget {ent_get(e, "changetarget")}: '
                                f'not supported in a merged map yet')
    return problems, notes


def plan_group(maps_dir: Path, members: list, accepted: dict | None = None) -> PlanReport:
    accepted = accepted or {}
    report = PlanReport(members=list(members))
    for m in members:
        if groups.hl_named(m):
            report.problems.append(f'{m} is never merged: {groups.NEVER_MERGED[m]}')
    if report.problems:
        return report
    maps = [bsp.Bsp.read(Path(maps_dir) / f'{m}.bsp') for m in members]
    named = list(zip(members, [x.ents for x in maps]))
    try:
        merged, sections = groups.merge_members(groups.group_name(members), list(zip(members, maps)))
    except (ValueError, groups.MergeCheckFailed) as error:
        report.problems.append(f'merge: {error}')
        return report

    n = report.numbers
    for key in ('planes', 'nodes', 'clipnodes', 'leafs', 'faces', 'marksurfaces', 'vertexes', 'texinfo'):
        n[key] = len(getattr(merged, key))
    n['lightmap pages'] = lightmap_pages(merged)
    n['entities'] = sum(len(x.ents) for x in maps) - (len(maps) - 1) + len(merged.ents) - 1
    n['models'] = len(merged.models) + len(precaches([e for x in maps for e in x.ents], ('.mdl', '.spr')))
    n['sounds'] = len(precaches([e for x in maps for e in x.ents], ('.wav',)))
    n['info_node'] = sum(1 for x in maps for e in x.ents if ent_get(e, 'classname', '').startswith('info_node'))
    for key, limit in LIMITS.items():
        if key in n and n[key] > limit:
            report.problems.append(f'{key}: {n[key]} over the limit {limit}')

    worlds = [dict((k, v) for k, v in x.ents[0]) for x in maps]
    problems, notes = world_key_problems(members, worlds, accepted)
    report.problems += problems
    report.notes += notes
    report.problems += section_limit_problems(sections)
    report.notes += weather_notes(named)
    report.problems += first_of_class_problems(named)
    report.problems += transition_pair_problems(named)
    report.problems += globalname_problems(named)
    report.problems += fixed_name_problems(named, sections)
    report.problems += reference_problems(named, sections)

    landmarks = [[ent_get(e, 'targetname') for e in x.ents if ent_get(e, 'classname') == 'info_landmark'] for x in maps]
    problems, notes = door_checks(members, landmarks, [x.ents for x in maps])
    report.problems += problems
    report.notes += notes
    report.notes.append('dz ' + ', '.join(f'{s.map}={s.dz:g}' for s in sections))
    report.notes.append('renames ' + ', '.join(f'{s.map}={len(s.renames)}' for s in sections))
    report.notes.append('styles ' + ', '.join(f'{s.map}={s.styles}' for s in sections if s.styles))
    return report


def record_group(maps_dir: Path, members: list, wave: int, path: Path | None = None) -> groups.Group:
    """Plan the group, build it from the maps in maps_dir and write it into the group list with its input and
    output hashes, in place of a group of the same name."""
    report = plan_group(maps_dir, members)
    if not report.ok:
        raise ValueError(f'{" + ".join(members)} is refused: ' + '; '.join(report.problems[:5]))
    inputs = {m: hashlib.sha256((Path(maps_dir) / f'{m}.bsp').read_bytes()).hexdigest() for m in members}
    group = groups.Group(name=groups.group_name(members), members=list(members), inputs=inputs,
                         bsp_sha256='0' * 64, sections_sha256='0' * 64, wave=wave, enabled=True)
    data, text = groups.build_group(maps_dir, group)
    group.bsp_sha256 = hashlib.sha256(data).hexdigest()
    group.sections_sha256 = hashlib.sha256(text.encode('ascii')).hexdigest()
    groups.validate(group)
    path = Path(path or groups.groups_path())
    listed = json.loads(path.read_text(encoding='utf-8'))
    others = [g for g in listed['groups'] if g['name'] != group.name]
    shared = sorted({m for g in others for m in g['members']} & set(members))
    if shared:
        raise ValueError(f'{group.name}: {", ".join(shared)} already in another group')
    names = [g['name'] for g in listed['groups']]
    if group.name in names:
        listed['groups'][names.index(group.name)] = asdict(group)
    else:
        listed['groups'].append(asdict(group))
    path.write_text(json.dumps(listed, indent=2) + '\n', encoding='utf-8')
    return group


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
    if argv[1:2] == ['--record']:
        group = record_group(maps_dir, argv[3:], int(argv[2]))
        print(f'recorded {group.name} = {" + ".join(group.members)} (wave {group.wave}) in {groups.groups_path()}')
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
