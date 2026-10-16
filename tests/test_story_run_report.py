"""The run report must count only finished transitions and never echo secrets."""
import io
import tempfile
import unittest
from pathlib import Path

from tools import story_run_report as rr

LOG = """\
[2026:09:30|20:00:00] cofstory phase=exit-leader userid=1 in_exit=1 strays=1
[2026:09:30|20:00:01] [cof-story-transition] phase=capture generation=2 slot=1 userid=1 source=c_start destination=c_start2 landmark=hahaland
[2026:09:30|20:00:01] [cof-story-transition] phase=capture generation=2 slot=2 userid=2 source=c_start destination=c_start2 landmark=hahaland
[2026:09:30|20:00:01] [cof-story-transition] phase=restore generation=2 slot=1 userid=1 map=c_start2 origin=1,2,3
[2026:09:30|20:00:01] [cof-story-transition] phase=restore generation=2 slot=2 userid=2 map=c_start2 origin=4,5,6
[2026:09:30|20:00:01] cofstory phase=arrival-placed userid=2 leader=1 origin=4,5,6
[2026:09:30|20:00:02] [cof-story-transition] phase=ready generation=2 map=c_start2
[2026:09:30|20:01:00] Error: [cof-story-transition] rejected: unique info_landmark 'x' required in both c_start2 and c_apartment1
[2026:09:30|20:02:00] [cof-story-transition] phase=capture generation=3 slot=1 userid=1 source=c_start2 destination=c_start landmark=hahaland
[2026:09:30|20:02:01] cofstory phase=door-expired userid=1 reason=party-not-ready
Host_Error: something broke rcon_password hunter2
[2026:09:30|20:03:00] cofstory phase=item-found userid=1 item=inventoryitems/hallkey.txt
[2026:09:30|20:03:01] cofstory phase=item-deferred userid=2 item=inventoryitems/hallkey.txt reason=inventory-full
"""


class StoryRunReportTests(unittest.TestCase):
    def test_recovery_events_and_refusals(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'host.log'
            log.write_text(
                'cofstory phase=note-shared userid=2 slot=1 code=1\n'
                'cofstory phase=inventory-restored userid=2 reason=respawn\n'
                'cofstory phase=session-saved slot=manual map=c_start bytes=123\n'
                'cofstory phase=session-loaded slot=manual map=c_start generation=2\n'
                'cofstory phase=session-load-refused reason=preflight-or-stage\n'
                'cofstory phase=session-wipe-recovery map=c_start\n')
            session = rr.parse_log(log)
        self.assertEqual(session['party']['inventory-restored'], 1)
        self.assertEqual(session['party']['session-loaded'], 1)
        self.assertEqual(session['party']['session-wipe-recovery'], 1)
        self.assertEqual(session['transitions'], [])
        self.assertEqual(len(session['warnings']), 1)

    def test_merged_maps_count_as_their_original_maps(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'host.log'
            log.write_text(
                # a door between two sections: instant, both movers in one transition, original names
                '[2026:10:16|20:00:00] [cof-story-transition] phase=section-move userid=1 from=c_apartment3 to=c_apartment4 landmark=land1 origin=1,2,3\n'
                '[2026:10:16|20:00:00] [cof-story-transition] phase=section-move userid=2 from=c_apartment3 to=c_apartment4 landmark=land1 origin=4,5,6\n'
                # a level change out of one merged file into another: reported by the original maps
                '[2026:10:16|20:05:00] [cof-sections] changelevel from=c_apartment5 to=c_apartment1 landmark=land5 file=cm_apartment1 dest=land5\n'
                '[2026:10:16|20:05:01] [cof-story-transition] phase=capture generation=4 slot=1 userid=1 source=cm_apartment3 destination=cm_apartment1 landmark=land5\n'
                '[2026:10:16|20:05:01] [cof-story-transition] phase=restore generation=4 slot=1 userid=1 map=cm_apartment1 origin=1,2,3\n'
                '[2026:10:16|20:05:02] [cof-story-transition] phase=ready generation=4 map=cm_apartment1\n')
            session = rr.parse_log(log)
        moves = [(t['src'], t['dst'], t['landmark'], len(t['players']), t['ready']) for t in session['transitions']]
        self.assertEqual(moves, [('c_apartment3', 'c_apartment4', 'land1', 2, True),
                                 ('c_apartment5', 'c_apartment1', 'land5', 1, True)])
        self.assertEqual(session['first_map'], 'c_apartment3')
        self.assertEqual(session['party']['section-move'], 2)

    def test_parse_counts_finished_transitions_and_redacts(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'host.log'
            log.write_text(LOG, encoding='latin-1')
            session = rr.parse_log(log)
        first, second = session['transitions']
        self.assertTrue(first['ready'])
        self.assertEqual(len(first['restored']), 2)
        self.assertFalse(second['ready'])
        self.assertEqual(session['party']['arrival-placed'], 1)
        self.assertEqual(len(session['warnings']), 2)
        problems = ' '.join(what for _, what in session['problems'])
        self.assertIn('transition refused', problems)
        self.assertIn('Host_Error', problems)
        self.assertNotIn('hunter2', problems)

    def test_item_phases_are_counted_and_deferrals_warned(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'host.log'
            log.write_text(LOG, encoding='latin-1')
            session = rr.parse_log(log)
        self.assertEqual(session['party']['item-found'], 1)
        self.assertTrue(any('item-deferred' in what for _, what in session['warnings']))

    def test_combined_counted_and_stalled_warned(self):
        text = (LOG + '[2026:09:30|20:04:00] cofstory phase=item-combined userid=1 item=inventoryitems/taclight.txt\n'
                '[2026:09:30|20:04:05] cofstory phase=item-stalled userid=2 item=inventoryitems/hallkey.txt\n')
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'host.log'
            log.write_text(text, encoding='latin-1')
            session = rr.parse_log(log)
        self.assertEqual(session['party']['item-combined'], 1)
        self.assertTrue(any('item-stalled' in what for _, what in session['warnings']))

    def test_exit_and_warp_phases_are_counted_and_forced_exits_warned(self):
        text = (LOG + '[2026:10:02|20:05:00] cofstory phase=exit-hold destination=c_start2 landmark=hahaland waiter=1 walkin=1 missing=2\n'
                '[2026:10:02|20:05:04] cofstory phase=warp userid=2 partner=1 origin=1,2,3\n'
                '[2026:10:02|20:05:04] cofstory phase=exit-ready destination=c_start2 landmark=hahaland waiter=1 waited=4.0\n'
                '[2026:10:02|20:06:00] cofstory phase=exit-cancelled waiter=1\n'
                '[2026:10:02|20:07:00] cofstory phase=cut-off userid=2 owner=1 kind=door\n'
                '[2026:10:02|20:08:00] cofstory phase=exit-forced destination=c_start landmark=hahaland waiter=1 waited=11.5\n'
                '[2026:10:02|20:09:00] cofstory phase=warp-refused userid=2 reason=no-room\n'
                '[2026:10:02|20:10:00] cofstory phase=exit-unholdable destination=c_roof landmark=land2\n')
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'host.log'
            log.write_text(text, encoding='latin-1')
            session = rr.parse_log(log)
        for phase in ('exit-hold', 'exit-ready', 'exit-cancelled', 'warp', 'cut-off'):
            self.assertEqual(session['party'][phase], 1, phase)
        warned = ' '.join(what for _, what in session['warnings'])
        for phase in ('exit-forced', 'warp-refused', 'exit-unholdable'):
            self.assertIn(phase, warned)
        self.assertNotIn('exit-ready', warned)

    def test_story_moment_phases_are_counted_and_lost_calls_warned(self):
        text = (LOG + '[2026:10:12|20:05:00] cofstory phase=camera-released userid=2 camera=40\n'
                '[2026:10:12|20:05:01] cofstory phase=fall-death userid=2 trigger=12\n'
                '[2026:10:12|20:05:02] cofstory phase=call-retargeted call=sophiecall entity=1\n'
                '[2026:10:12|20:05:03] cofstory phase=call-dropped call=sophiecall\n'
                '[2026:10:12|20:05:04] cofstory phase=call-restarted call=sophiecall entity=1\n'
                '[2026:10:12|20:05:40] cofstory phase=call-forced call=sophiecall target=gateinter\n'
                '[2026:10:12|20:06:00] cofstory phase=yesno-refused userid=2 entity=20\n'
                '[2026:10:12|20:06:01] cofstory phase=gift-shared class=weapon_glock\n')
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'host.log'
            log.write_text(text, encoding='latin-1')
            session = rr.parse_log(log)
        for phase in ('camera-released', 'fall-death', 'call-retargeted', 'call-restarted', 'yesno-refused', 'gift-shared'):
            self.assertEqual(session['party'][phase], 1, phase)
        warned = ' '.join(what for _, what in session['warnings'])
        for phase in ('call-dropped', 'call-forced'):
            self.assertIn(phase, warned)
        self.assertNotIn('fall-death', warned)

    def test_companion_phases_are_counted_and_refusals_warned(self):
        text = (LOG + '[2026:10:12|21:00:00] cofstory phase=companion-added userid=200 name=Companion\n'
                '[2026:10:12|21:00:01] cofstory phase=companion-equip userid=200 weapon=weapon_glock\n'
                '[2026:10:12|21:00:02] cofstory phase=companion-cpr userid=200 leader=1\n'
                '[2026:10:12|21:00:03] cofstory phase=companion-mode userid=200 mode=fight\n'
                '[2026:10:12|21:00:04] cofstory phase=cpr-unlinked userid=1\n'
                '[2026:10:12|21:00:05] cofstory phase=companion-refused reason=nope\n')
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'host.log'
            log.write_text(text, encoding='latin-1')
            session = rr.parse_log(log)
        for phase in ('companion-added', 'companion-equip', 'companion-cpr', 'cpr-unlinked'):
            self.assertEqual(session['party'][phase], 1, phase)
        self.assertEqual(session['party']['companion-mode'], 0) # every mode change: too many to count
        warned = ' '.join(what for _, what in session['warnings'])
        self.assertIn('companion-refused', warned)
        self.assertNotIn('companion-added', warned)

    def test_test_gun_phases_are_counted_and_a_wrong_name_warned(self):
        text = (LOG + '[2026:10:12|22:00:00] cofstory phase=test-gun-given userid=2 class=weapon_m16\n'
                '[2026:10:12|22:00:01] cofstory phase=test-gun-full userid=3 class=weapon_m16\n'
                '[2026:10:12|22:00:02] cofstory phase=test-gun-refused class=weapon_tmp\n'
                '[2026:10:12|22:00:03] cofstory phase=cutscene-skip userid=1\n'
                '[2026:10:12|22:00:04] cofstory phase=logo-shared from=1 logo=3\n')
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'host.log'
            log.write_text(text, encoding='latin-1')
            session = rr.parse_log(log)
        for phase in ('test-gun-given', 'test-gun-full', 'cutscene-skip', 'logo-shared'):
            self.assertEqual(session['party'][phase], 1, phase)
        warned = ' '.join(what for _, what in session['warnings'])
        self.assertIn('test-gun-refused', warned)
        self.assertNotIn('test-gun-given', warned)

    def test_report_flags_unfinished_transition(self):
        if not rr.MAPS.is_dir():
            self.skipTest('runtime maps are not present')
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'host.log'
            log.write_text(LOG, encoding='latin-1')
            out = io.StringIO()
            status = rr.report([log], out)
        text = out.getvalue()
        self.assertEqual(status, 1)
        self.assertIn('1 completed, 1 unfinished', text)
        self.assertIn('Story exits proven: 1 of 195', text)
        self.assertNotIn('hunter2', text)


if __name__ == '__main__':
    unittest.main()
