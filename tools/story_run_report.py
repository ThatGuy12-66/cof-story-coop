"""Summarize story co-op host logs: every level transition, its outcome, and
how much of the campaign's exit graph a play session has actually proven.

Reads only the engine's tagged lines; raw lines are shown only for problems,
with anything credential-like redacted.

    python tools/story_run_report.py            # current + archived host logs
    python tools/story_run_report.py some.log   # specific logs
"""
import argparse
import collections
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from cofcoop.bsp import list_maps, load_map_entities  # noqa: E402

GAME = ROOT / 'runtimes' / 'enhanced'
MAPS = GAME / 'cryoffear' / 'maps'
DEFAULT_LOGS = [GAME / 'story-remote-host.log']
ARCHIVE = GAME / 'story-logs'

STAMP = r'(?:\[(?P<time>[\d:|]+)\]\s*)?'
CAPTURE = re.compile(STAMP + r'\[cof-story-transition\] phase=capture generation=(?P<gen>\d+) slot=\d+ userid=(?P<userid>\d+) source=(?P<src>\S+) destination=(?P<dst>\S+) landmark=(?P<landmark>\S*)')
RESTORE = re.compile(STAMP + r'\[cof-story-transition\] phase=restore generation=(?P<gen>\d+) slot=\d+ userid=(?P<userid>\d+) map=(?P<map>\S+) origin=(?P<origin>\S+)')
READY = re.compile(STAMP + r'\[cof-story-transition\] phase=ready generation=(?P<gen>\d+) map=(?P<map>\S+)')
# Merged story maps: a door between two sections of one map file (instant, no capture), and a level change
# into or out of a merged file, both with the original map and landmark names.
SECTION_MOVE = re.compile(STAMP + r'\[cof-story-transition\] phase=section-move userid=(?P<userid>\d+) from=(?P<src>\S+) to=(?P<dst>\S+) landmark=(?P<landmark>\S*) origin=(?P<origin>\S+)')
SECTION_CHANGE = re.compile(STAMP + r'\[cof-sections\] changelevel from=(?P<src>\S+) to=(?P<dst>\S+) landmark=(?P<landmark>\S*) file=(?P<file>\S+) dest=(?P<dest>\S*)')
REJECTED = re.compile(STAMP + r'.*\[cof-story-transition\] rejected: (?P<reason>.*)')
PARTY = re.compile(STAMP + r'cofstory phase=(?P<phase>[a-z-]+)(?P<rest>.*)')
FATAL = re.compile(r'Host_Error|Sys_Error|Stopped with reason "crashed"|Story transition .*(failed|cannot|did not)|Level transition ERROR', re.I)
SECRET = re.compile(r'((?:rcon_)?password|sv_password|rcon)(\S*\s+)\S+', re.I)

# Party events worth a count; the ones in WARN mean a partner may have been left behind.
PARTY_EVENTS = ('exit-leader', 'arrival-placed', 'arrival-kept', 'arrival-overlapped', 'door-party-moved',
                'teleport-party-moved', 'spawn-placed', 'starter-shared', 'door-visibility-restored',
                'item-found', 'item-used', 'item-dropped', 'item-delivered', 'item-removed', 'item-combined',
                'exit-hold', 'exit-ready', 'exit-cancelled', 'warp', 'cut-off',
                'note-shared', 'inventory-captured', 'inventory-restored',
                'session-saved', 'session-load-queued', 'session-player-restored', 'session-loaded',
                'session-wipe-recovery', 'session-checkpoint-preserved', 'session-save-deferred',
                'camera-released', 'camera-held', 'telescope-released', 'cutscene-wait-over',
                'fall-death', 'call-retargeted', 'call-restarted', 'call-flags-cleared',
                'ladder-busy', 'ladder-released', 'wheelchair-removed', 'yesno-answer', 'yesno-refused',
                'gift-shared', 'respawn-sooner', 'cutscene-wait-released', 'session-checkpoint-unusable',
                'companion-added', 'companion-removed', 'companion-teleport', 'companion-equip', 'companion-cpr',
                'companion-syringe', 'companion-driving', 'companion-released', 'companion-order', 'cpr-unlinked',
                'test-gun-given', 'test-gun-full', 'test-gun-off', 'test-gun-wait',
                'cutscene-skip', 'cutscene-skip-stopped', 'cutscene-skip-over', 'logo-shared')
WARN = ('arrival-stranded', 'door-expired', 'teleport-expired', 'door-party-skipped', 'teleport-party-skipped',
        'starter-deferred', 'item-deferred', 'item-stalled',
        'exit-forced', 'exit-unholdable', 'warp-refused',
        'session-save-refused', 'session-load-refused', 'session-rollback-failed', 'session-index-failed',
        'call-dropped', 'call-forced', 'companion-refused', 'test-gun-refused')


def story_exits(maps_dir=MAPS, start='c_intro'):
    """Every usable trigger_changelevel reachable from the campaign start:
    {(src, dst, landmark)}. Exits whose landmark is missing from the source map
    (two orphans nothing fires) are left out; the engine would refuse them."""
    maps = list_maps(maps_dir)
    edges = collections.defaultdict(set)
    for name, path in maps.items():
        ents = load_map_entities(path)
        landmarks = collections.Counter(e.get('targetname', '') for e in ents if e.get('classname') == 'info_landmark')
        for ent in ents:
            if (ent.get('classname') == 'trigger_changelevel' and ent.get('map', '').strip().lower() in maps
                    and landmarks[ent.get('landmark', '')] == 1):
                edges[name].add((ent['map'].strip().lower(), ent.get('landmark', '')))
    seen, stack = set(), [start]
    while stack:
        name = stack.pop()
        if name in seen:
            continue
        seen.add(name)
        stack.extend(dst for dst, _ in edges[name])
    return seen, {(src, dst, lm) for src in seen for dst, lm in edges[src]}


def parse_log(path):
    """One host session: transitions in order, plus problems and party events."""
    transitions = collections.OrderedDict()
    problems, warnings, party = [], [], collections.Counter()
    first_map = None
    originals = {}   # (merged file, landmark there) -> the original (src, dst, landmark) of a level change
    text = Path(path).read_text(encoding='latin-1', errors='replace')
    for raw in text.splitlines():
        line = raw.strip()
        if m := SECTION_MOVE.search(line):
            src, dst = m['src'].lower(), m['dst'].lower()
            t = transitions.setdefault(('move', m['time'], src, dst, m['landmark']), {
                'src': src, 'dst': dst, 'landmark': m['landmark'], 'time': m['time'], 'players': set(),
                'restored': {}, 'ready': True, 'rejected': None})
            t['players'].add(int(m['userid']))
            t['restored'][int(m['userid'])] = m['origin']
            party['section-move'] += 1
            first_map = first_map or src
        elif m := SECTION_CHANGE.search(line):
            originals[(m['file'].lower(), m['dest'])] = (m['src'].lower(), m['dst'].lower(), m['landmark'])
        elif m := CAPTURE.search(line):
            src, dst, landmark = originals.get((m['dst'].lower(), m['landmark']),
                                               (m['src'].lower(), m['dst'].lower(), m['landmark']))
            t = transitions.setdefault(int(m['gen']), {
                'src': src, 'dst': dst, 'landmark': landmark,
                'time': m['time'], 'players': set(), 'restored': {}, 'ready': False, 'rejected': None})
            t['players'].add(int(m['userid']))
            first_map = first_map or src
        elif m := RESTORE.search(line):
            if int(m['gen']) in transitions:
                transitions[int(m['gen'])]['restored'][int(m['userid'])] = m['origin']
        elif m := READY.search(line):
            if int(m['gen']) in transitions:
                transitions[int(m['gen'])]['ready'] = True
        elif m := REJECTED.search(line):
            problems.append((m['time'], 'transition refused: ' + m['reason']))
        elif m := PARTY.search(line):
            if m['phase'] in PARTY_EVENTS:
                party[m['phase']] += 1
            if m['phase'] in WARN:
                warnings.append((m['time'], m['phase'] + m['rest']))
        elif FATAL.search(line):
            problems.append((None, SECRET.sub(r'\1\2<redacted>', line)))
    return {'path': str(path), 'first_map': first_map, 'transitions': list(transitions.values()),
            'problems': problems, 'warnings': warnings, 'party': party}


def default_logs():
    logs = sorted(ARCHIVE.glob('*.log')) if ARCHIVE.is_dir() else []
    return logs + [p for p in DEFAULT_LOGS if p.exists()]


def report(logs, out=sys.stdout):
    reachable, exits = story_exits()
    used, maps_reached = set(), set()
    completed = failed = 0
    all_problems = []
    party = collections.Counter()
    for log in logs:
        session = parse_log(log)
        party.update(session['party'])
        if session['first_map']:
            maps_reached.add(session['first_map'])
        print(f'\n== {os.path.basename(session["path"])}', file=out)
        path = [session['first_map']] if session['first_map'] else []
        for t in session['transitions']:
            ok = t['ready'] and len(t['restored']) == len(t['players'])
            mark = 'ok ' if ok else 'BAD'
            completed += ok
            failed += not ok
            if ok:
                used.add((t['src'], t['dst'], t['landmark']))
                maps_reached.update((t['src'], t['dst']))
            else:
                all_problems.append((t['time'], f'{t["src"]} -> {t["dst"]} ({t["landmark"]}) never finished: '
                                     f'{len(t["restored"])}/{len(t["players"])} players restored, '
                                     f'ready={"yes" if t["ready"] else "no"}'))
            who = f'{len(t["players"])} player' + ('s' if len(t['players']) != 1 else '')
            print(f'  {mark} {t["time"] or "":>19}  {t["src"]} -> {t["dst"]}  [{t["landmark"]}]  {who}', file=out)
            path.append(t['dst'])
        if not session['transitions']:
            print('  (no level transitions in this log)', file=out)
        for when, what in session['problems']:
            all_problems.append((when, what))
        for when, what in session['warnings']:
            print(f'  warn {when or ""} {what}', file=out)
    print('\n== Summary', file=out)
    print(f'Transitions: {completed} completed, {failed} unfinished', file=out)
    print(f'Story maps reached: {len(maps_reached & reachable)} of {len(reachable)}', file=out)
    print(f'Story exits proven: {len(used & exits)} of {len(exits)}', file=out)
    if party:
        print('Party events: ' + ', '.join(f'{k}={v}' for k, v in sorted(party.items())), file=out)
    if all_problems:
        print(f'\nPROBLEMS ({len(all_problems)}):', file=out)
        for when, what in all_problems:
            print(f'  {when or "":>19}  {what}', file=out)
    else:
        print('No refused transitions, engine errors or crashes found.', file=out)
    return 1 if all_problems else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('logs', nargs='*', type=Path, help='host logs (default: current and archived story host logs)')
    parser.add_argument('--untested', action='store_true', help='also list story exits no log has proven yet')
    args = parser.parse_args(argv)
    logs = args.logs or default_logs()
    if not logs:
        print('No story host logs found yet.')
        return 0
    status = report(logs)
    if args.untested:
        _, exits = story_exits()
        proven = set()
        for log in logs:
            for t in parse_log(log)['transitions']:
                if t['ready']:
                    proven.add((t['src'], t['dst'], t['landmark']))
        print('\nUntested story exits:')
        for src, dst, lm in sorted(exits - proven):
            print(f'  {src} -> {dst}  [{lm}]')
    return status


if __name__ == '__main__':
    sys.exit(main())
