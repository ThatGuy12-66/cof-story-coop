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
# Keys whose value names another entity, whatever the class (and targetname itself). The engine's composer has the
# same list (cof_sections.h cof_sect_ref_keys); hl.dll's keys from docs/research/2026-10-18-cof-decompilation-survey.md
# 5.5 (evidence/decomp-survey-20261018/C1-effects-triggers.md 7.4).
REF_KEYS = ('target', 'killtarget', 'targetname', 'landmark', 'changetarget', 'master', 'netname', 'parentname',
            'LightningStart', 'LightningEnd', 'm_iszEntity', 'TriggerTarget', 'movewith', 'moveto', 'm_iszAltTarget',
            'm_iszBothTarget', 'm_iszThreadName', 'm_iszLocusThread', 'm_iszWatch', 'm_iszPath', 'm_iszNewTarget',
            'm_iszViewEntity', 'skipmanager', 'm_sAttachedModel', 'trigger_one', 'trigger_two', 'trigger_three',
            'trigger_four', 'triggerone', 'triggertwo', 'triggerthree', 'triggerfour', 'triggerfive', 'notrigger',
            'cameraent', 'simonent', 'player1trigger', 'player2trigger', 'player3trigger', 'player4trigger')
# Keys that name an entity only for some classes (for others a message, a sound or a map); cof_sect_class_keys.
CLASS_REF_KEYS = {
    'path_corner': ('message',), 'path_track': ('message',),  # the target fired on arrival
    'trigger_teleport': ('message', 'noise'), 'trigger_onsight': ('message', 'noise', 'noise1'),
    'trigger_camera': ('message',), 'trigger_playercheckarea': ('message',), 'trigger_booksimon': ('message',),
    'scripted_startrush': ('message',), 'env_sprite': ('message',), 'watcher_count': ('message', 'noise', 'noise1', 'noise2'),
    'cof_teleport': ('message',), 'cof_interdooronoff': ('message',), 'cof_updatekeypad': ('message',),
    'cof_bosshealthbar': ('message',), 'cof_doctorweapontrigger': ('message',), 'cof_mdlcutscene': ('message',),
    'cof_wheelchairmode': ('message',), 'cof_fatfuckingcunt': ('message',), 'cof_spawnpointonoff': ('message',),
    'cof_lobby_start': ('message',), 'cof_survivalmode': ('message',), 'cof_randomtimedspawner': ('message',),
    'cof_weapontrigger': ('noise',), 'cof_skipmanager': ('entity', 'teleport'),
}


def names_entity(cls: str, key: str) -> bool:
    """The key's value names an entity (to rename with it)."""
    return key in REF_KEYS or key in CLASS_REF_KEYS.get(cls, ())
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
            if v and k != 'targetname' and names_entity(cls, k):
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


def coordinate(v: float) -> str:
    """A coordinate as the engine's composer writes it: up to 6 decimals, trailing zeros dropped."""
    s = f'{v:.6f}'.rstrip('0').rstrip('.')
    return '0' if s in ('', '-0') else s


def moved_origin(value: str, dz: float) -> str:
    parts = value.split()
    if len(parts) != 3:
        return value
    try:
        z = float(parts[2])
    except ValueError:
        return value
    return f'{parts[0]} {parts[1]} {coordinate(z + dz)}'


def compose_entity(e: list, s: Section) -> list:
    """One member entity as the engine puts it into the merged map (cof_sections.h CoF_ComposeEntities)."""
    cls = ent_get(e, 'classname', '')
    out = []
    for k, v in e:
        if k == 'origin' and s.dz:
            v = moved_origin(v, s.dz)
        elif k == 'model' and v.startswith('*') and v[1:].isdigit() and int(v[1:]) >= 1:
            v = f'*{int(v[1:]) - 1 + s.first_model}'
        elif names_entity(cls, k):
            v = s.renames.get(v, v)
        elif k == 'style' and cls.startswith('light') and v.lstrip('-').isdigit():
            v = str(s.styles.get(int(v), int(v)))
        elif cls == 'multi_manager' and k not in MM_RESERVED:
            base = k.split('#')[0]
            k = s.renames.get(base, base) + k[len(base):]
        out.append([k, v])
    return out


def compose_reference(world: list, members: list, sections: list) -> bytes:
    """The merged map's entity lump the engine composes at load (the reference its tests compare with): the merged
    file's worldspawn, then every member's entities but its worldspawn, through that member's rules."""
    from tools.map_merge.bsp import serialize_ents
    ents = [world]
    for member, s in zip(members, sections):
        ents += [compose_entity(e, s) for e in member if ent_get(e, 'classname') != 'worldspawn']
    return serialize_ents(ents)


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
