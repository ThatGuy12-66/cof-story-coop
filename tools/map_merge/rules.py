"""Per-section rules of a merged map and the .sections file the engine reads.

The merger decides the rules; the engine applies them when it puts the merged map's entities together at load
(each member's own entity source, then: origins up by dz, brush models renumbered, names renamed, switchable light
styles mapped). Names: a member keeps a name unless an earlier member defines it (then `<name>_m<k>`); a reference
to a name the member does not define but another member does is cut (`<name>_x<k>`), so nothing wires across
sections that did not in the original maps.
"""
from __future__ import annotations

from dataclasses import dataclass, field
import re

from tools.map_merge.bsp import ent_get

FORMAT = 'cof-sections 1'
# Keys whose value names another entity (and targetname itself).
REF_KEYS = ('target', 'killtarget', 'targetname', 'landmark', 'changetarget', 'master', 'netname', 'parentname',
            'LightningStart', 'LightningEnd', 'm_iszEntity', 'TriggerTarget')
# Entities whose `message` is the target they fire on arrival.
PATH_MESSAGE_CLASSES = ('path_corner', 'path_track')
# multi_manager keys that are not targets.
MM_RESERVED = ('classname', 'targetname', 'origin', 'angles', 'spawnflags', 'wait', 'delay', 'target', 'killtarget',
               'master', 'globalname', 'rendermode', 'renderamt', 'rendercolor', 'renderfx', 'model')
SWITCHABLE = range(32, 61)          # 61-63 are the Paranoia renderer's bump-map styles
SAFE_NAME = re.compile(r'^[^\s"]+$')


@dataclass
class Section:
    map: str
    dz: float
    first_model: int
    models: int
    bounds: tuple
    renames: dict = field(default_factory=dict)
    styles: dict = field(default_factory=dict)
    landmarks: list = field(default_factory=list)


def defined_names(ents: list) -> set:
    return {v for e in ents for k, v in e if k == 'targetname' and v}


def multi_manager_targets(e: list) -> list:
    return [k.split('#')[0] for k, _ in e if k not in MM_RESERVED]


def referenced_names(ents: list) -> set:
    out = set()
    for e in ents:
        cls = ent_get(e, 'classname', '')
        for k, v in e:
            if v and (k in REF_KEYS and k != 'targetname' or k == 'message' and cls in PATH_MESSAGE_CLASSES):
                out.add(v)
        if cls == 'multi_manager':
            out.update(t for t in multi_manager_targets(e) if t)
    return out


def switchable_styles(ents: list) -> set:
    out = set()
    for e in ents:
        if ent_get(e, 'classname', '').startswith('light'):
            try:
                style = int(ent_get(e, 'style', '0') or 0)
            except ValueError:
                continue
            if style in SWITCHABLE:
                out.add(style)
    return out


def unique(base: str, suffix: str, taken: set) -> str:
    if not SAFE_NAME.match(base):
        raise ValueError(f'name {base!r} cannot be renamed (spaces or quotes)')
    name = base + suffix
    while name in taken:
        name += suffix
    return name


def section_rules(members: list, dzs: list, model_counts: list, z_bounds: list, face_styles: list | None = None) -> list:
    """Rules per member. members: (map name, entity list) in merge order; dzs: each member's height offset;
    model_counts: each member's model count (world included); z_bounds: each member's (zmin, zmax) after dz;
    face_styles: each member's face light styles (only 32-60 count)."""
    defined = [defined_names(ents) for _, ents in members]
    referenced = [referenced_names(ents) for _, ents in members]
    taken_names = set().union(*defined, *referenced) if members else set()
    taken_styles: set = set()
    first_model = 1
    out = []
    for k, (name, ents) in enumerate(members):
        renames = {}
        earlier = set().union(*defined[:k]) if k else set()
        others = set().union(*(defined[j] for j in range(len(members)) if j != k))
        for n in sorted(defined[k] | referenced[k]):
            if n in defined[k]:
                if n in earlier:
                    renames[n] = unique(n, f'_m{k}', taken_names)
            elif n in others:
                renames[n] = unique(n, f'_x{k}', taken_names)
            if n in renames:
                taken_names.add(renames[n])
        own = switchable_styles(ents) | ({s for s in face_styles[k] if s in SWITCHABLE} if face_styles else set())
        styles = {}
        final = set()
        for s in sorted(own):
            if s in taken_styles:
                free = [v for v in SWITCHABLE if v not in taken_styles | own | final | set(styles.values())]
                if not free:
                    raise ValueError(f'{name}: no free switchable light style for {s}')
                styles[s] = free[0]
            final.add(styles.get(s, s))
        taken_styles |= final
        landmarks = [renames.get(ent_get(e, 'targetname'), ent_get(e, 'targetname')) for e in ents
                     if ent_get(e, 'classname') == 'info_landmark' and ent_get(e, 'targetname')]
        out.append(Section(map=name, dz=float(dzs[k]), first_model=first_model, models=model_counts[k] - 1,
                           bounds=(float(z_bounds[k][0]), float(z_bounds[k][1])), renames=renames, styles=styles,
                           landmarks=landmarks))
        first_model += model_counts[k] - 1
    return out


def number(x: float) -> str:
    return f'{x:.10g}'


def to_text(name: str, sections: list) -> str:
    lines = [f'{FORMAT} {name}']
    for s in sections:
        lines.append(f'section {s.map} {number(s.dz)} {s.first_model} {s.models}')
        lines.append(f'bounds {number(s.bounds[0])} {number(s.bounds[1])}')
        lines += [f'rename {old} {new}' for old, new in sorted(s.renames.items())]
        lines += [f'style {old} {new}' for old, new in sorted(s.styles.items())]
        lines += [f'landmark {x}' for x in s.landmarks]
    return '\n'.join(lines) + '\n'


def parse(text: str) -> tuple:
    lines = [line.strip() for line in text.splitlines()]
    lines = [line for line in lines if line and not line.startswith('#')]
    if not lines or not lines[0].startswith(FORMAT + ' ') or len(lines[0].split()) != 3:
        raise ValueError('not a .sections file')
    name = lines[0].split()[2]
    sections = []
    for line in lines[1:]:
        word, *args = line.split()
        if word == 'section' and len(args) == 4:
            sections.append(Section(map=args[0], dz=float(args[1]), first_model=int(args[2]), models=int(args[3]),
                                    bounds=(0.0, 0.0)))
            continue
        if not sections:
            raise ValueError(f'{word} outside a section')
        s = sections[-1]
        if word == 'bounds' and len(args) == 2:
            s.bounds = (float(args[0]), float(args[1]))
        elif word == 'rename' and len(args) == 2:
            s.renames[args[0]] = args[1]
        elif word == 'style' and len(args) == 2:
            s.styles[int(args[0])] = int(args[1])
        elif word == 'landmark' and len(args) == 1:
            s.landmarks.append(args[0])
        else:
            raise ValueError(f'bad line: {line}')
    return name, sections
