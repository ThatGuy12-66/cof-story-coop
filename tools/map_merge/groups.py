"""The merged-map groups (tools/map_groups.json), building a group from a player's own maps, installing it.

A group is built only from the exact retail maps it lists (SHA-256 each) and must come out byte for byte as
recorded, so every player's merged files are the same and a host and a joiner always match.
"""
from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
import re
import sys

from tools.map_merge import bsp, check, merge, rules

HEX64 = re.compile(r'^[0-9a-f]{64}$')
MAP_NAME = re.compile(r'^[a-z0-9_]{1,32}$')
# Maps never merged, and why (evidence/map-merge-plan-20261017/hl-names/hl-map-names-re.md). hl.dll names c_city,
# c_forest3, c_subwaysick1 and c_intro only as level destinations, which the engine's name routing covers; c_start's
# easter egg goes into its section (rules.named_map_entities).
NEVER_MERGED = {
    'c_nightmare': 'client.dll hides the HUD and turns off sprint and the inventory by this name',
    'c_game_menu1': 'the menu map',
}
# hl.dll's cof_credits counts the ending, the rank and the unlocks only in a map whose name contains this.
CREDITS_NAME = 'c_ending'


class GroupInputMismatch(Exception):
    """A member map is not the retail file the group was made from: the group plays unmerged."""


class MergeCheckFailed(Exception):
    """A pair merge did not check clean."""


@dataclass
class Group:
    name: str
    members: list
    inputs: dict
    bsp_sha256: str
    sections_sha256: str
    wave: int
    enabled: bool
    accepted: dict = field(default_factory=dict)


def groups_path() -> Path:
    base = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parents[2]))
    return base / 'tools' / 'map_groups.json'


def hl_named(name: str) -> bool:
    return name in NEVER_MERGED


def group_name(members: list) -> str:
    """cm_ + the first member without c_, or with it when a member's name holds c_ending (the credits test)."""
    if any(CREDITS_NAME in m for m in members):
        return 'cm_' + members[0]
    return 'cm_' + members[0][2:]


def validate(g: Group) -> Group:
    if not isinstance(g.members, list) or not 2 <= len(g.members) <= 4:
        raise ValueError(f'{g.name}: a group has 2-4 members')
    for m in g.members:
        if not isinstance(m, str) or not MAP_NAME.match(m) or not m.startswith('c_') or hl_named(m):
            raise ValueError(f'{g.name}: member {m!r} cannot be merged')
    if len(set(g.members)) != len(g.members):
        raise ValueError(f'{g.name}: a member is listed twice')
    if not isinstance(g.name, str) or not MAP_NAME.match(g.name) or g.name != group_name(g.members):
        raise ValueError(f'bad group name {g.name!r}: cm_ + the first member without c_ (with it when a member '
                         f'is a c_ending map), [a-z0-9_]{{1,32}}')
    if not isinstance(g.inputs, dict) or set(g.inputs) != set(g.members) or not all(
            isinstance(v, str) and HEX64.match(v) for v in g.inputs.values()):
        raise ValueError(f'{g.name}: inputs must give a SHA-256 for every member')
    for value in (g.bsp_sha256, g.sections_sha256):
        if not isinstance(value, str) or not HEX64.match(value):
            raise ValueError(f'{g.name}: bad output hash')
    if not isinstance(g.wave, int) or g.wave < 1 or not isinstance(g.enabled, bool) or not isinstance(g.accepted, dict):
        raise ValueError(f'{g.name}: bad wave, enabled or accepted')
    return g


def load_groups(path: Path | None = None) -> list:
    data = json.loads(Path(path or groups_path()).read_text(encoding='utf-8'))
    if not isinstance(data, dict) or data.get('schema') != 1 or not isinstance(data.get('groups'), list):
        raise ValueError('map_groups.json: schema 1 with a list of groups expected')
    out = [validate(Group(**item)) for item in data['groups']]
    names = [g.name for g in out]
    members = [m for g in out for m in g.members]
    if len(set(names)) != len(names) or len(set(members)) != len(members):
        raise ValueError('map_groups.json: a group or a member appears twice')
    return out


def read_member(maps_dir: Path, group: Group, member: str) -> bytes:
    path = Path(maps_dir) / f'{member}.bsp'
    try:
        data = path.read_bytes()
    except OSError as error:
        raise GroupInputMismatch(f'{group.name}: {member}.bsp cannot be read ({error})') from None
    if hashlib.sha256(data).hexdigest() != group.inputs[member]:
        raise GroupInputMismatch(f'{group.name}: {member}.bsp is not the retail map this group is made from')
    return data


def build_group(maps_dir: Path, group: Group) -> tuple:
    """(merged BSP bytes, .sections text) from the members in maps_dir."""
    members = [bsp.Bsp.from_bytes(read_member(maps_dir, group, m)) for m in group.members]
    merged, sections = merge_members(group.name, list(zip(group.members, members)))
    return bsp.write_bytes(bsp.to_lumps(merged)), rules.to_text(group.name, sections)


def plan_sections(named: list) -> list:
    """The section rules of (map name, Bsp) members stacked in order (each above or below all before it)."""
    members = [x for _, x in named]
    dzs, bounds = [0.0], [(members[0].models[0][2], members[0].models[0][5])]
    lo, hi = bounds[0]
    for b in members[1:]:
        dz = merge.choose_dz((lo, hi), (b.models[0][2], b.models[0][5]))
        dzs.append(dz)
        bounds.append((b.models[0][2] + dz, b.models[0][5] + dz))
        lo, hi = min(lo, bounds[-1][0]), max(hi, bounds[-1][1])
    face_styles = [{s for f in x.faces for s in f[5:9]} for x in members]
    return rules.section_rules([(m, x.ents) for m, x in named], dzs, [len(x.models) for x in members], bounds,
                               face_styles)


def merge_members(name: str, named: list) -> tuple:
    """The merged Bsp and the section rules of (map name, Bsp) members, stacked in order, every pair checked."""
    names = [m for m, _ in named]
    members = [x for _, x in named]
    sections = plan_sections(named)
    dzs = [s.dz for s in sections]
    current = members[0]
    for k, b in enumerate(members[1:], 1):
        lumps, info = merge.merge_pair(current, b, dzs[k], sections[k].styles)
        merged = bsp.Bsp.from_bytes(bsp.write_bytes(lumps))
        problems = check.check_pair(current, b, merged, info, sections[k].styles)
        if problems:
            raise MergeCheckFailed(f'{name}: adding {names[k]}: ' + '; '.join(problems[:5]))
        current = merged
    current.ents = current.ents[:1] + rules.named_map_entities([(m, x.ents) for m, x in named], sections)
    return current, sections


def write_checked(path: Path, data: bytes, expected: str) -> str:
    partial = path.with_name(path.name + '.partial')
    partial.write_bytes(data)
    os.replace(partial, path)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != expected:
        path.unlink()
        raise RuntimeError(f'{path.name} changed while it was written')
    return digest


def installed_group(maps_dir: Path, group: Group) -> dict | None:
    """1.3.7: the group's two files when both are already the ones recorded (an earlier setup or update made them from
    the same recipe), else None: nothing to make again."""
    files = {f'maps/{group.name}.bsp': (Path(maps_dir) / f'{group.name}.bsp', group.bsp_sha256),
             f'maps/{group.name}.sections': (Path(maps_dir) / f'{group.name}.sections', group.sections_sha256)}
    for path, expected in files.values():
        try:
            if not path.is_file() or path.is_symlink() or _file_sha256(path) != expected:
                return None
        except OSError:
            return None
    return {rel: expected for rel, (_, expected) in files.items()}


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def install_group(maps_dir: Path, group: Group) -> dict:
    """Build the group into maps_dir as <name>.bsp and <name>.sections; both must match the recorded hashes."""
    data, text = build_group(maps_dir, group)
    raw = text.encode('ascii')
    if hashlib.sha256(data).hexdigest() != group.bsp_sha256 or hashlib.sha256(raw).hexdigest() != group.sections_sha256:
        raise RuntimeError(f'{group.name}: the merged map does not come out as recorded')
    maps_dir = Path(maps_dir)
    return {f'maps/{group.name}.bsp': write_checked(maps_dir / f'{group.name}.bsp', data, group.bsp_sha256),
            f'maps/{group.name}.sections': write_checked(maps_dir / f'{group.name}.sections', raw, group.sections_sha256)}
