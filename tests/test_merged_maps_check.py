"""The merged-maps check tool's offline parts: the doors between sections, the script that walks into them, and
reading the result from the game's log."""
import unittest

from tests.test_map_merge_bsp import MAPS, retail
from tools import merged_maps_check as mc
from tools.map_merge import bsp, groups


def group(name):
    retail('c_apartment3')
    g = next(x for x in groups.load_groups() if x.name == name)
    named = [(m, bsp.Bsp.read(MAPS / f'{m}.bsp')) for m in g.members]
    return g, named, groups.plan_sections(named)


class ExitTests(unittest.TestCase):
    def test_every_door_between_sections_with_where_it_should_land(self):
        _, named, sections = group('cm_apartment3')
        exits = mc.section_exits(named, sections)
        doors = sorted((e.src, e.dst, e.landmark) for e in exits)
        self.assertIn(('c_apartment3', 'c_apartment4', 'land1'), doors)
        self.assertIn(('c_apartment4', 'c_apartment3', 'land1'), doors)
        self.assertIn(('c_apartment3', 'c_apartment5', 'ap5la'), doors)
        self.assertIn(('c_apartment5', 'c_apartment3', 'ap5la'), doors)
        self.assertFalse([d for d in doors if d[1] not in ('c_apartment3', 'c_apartment4', 'c_apartment5')])
        door = next(e for e in exits if (e.src, e.dst, e.landmark) == ('c_apartment3', 'c_apartment4', 'land1'))
        # the stand-in spot is inside the trigger that fires the exit, in the merged map
        lo, hi = door.box
        for k in range(3):
            self.assertLessEqual(lo[k], door.stand[k])
            self.assertLessEqual(door.stand[k], hi[k])
        # landing = stand + (destination landmark - source landmark), the destination 1536 up
        self.assertGreater(door.expected[2] - door.stand[2], 1000)

    def test_door_without_landmarks_expects_the_start(self):
        _, named, sections = group('cm_apartment1')
        exits = mc.section_exits(named, sections)
        orphan = [e for e in exits if e.landmark == 'land4']
        self.assertEqual(len(orphan), 1)
        self.assertIsNone(orphan[0].expected)


class ScriptTests(unittest.TestCase):
    def test_door_script_walks_into_each_exit_and_quits(self):
        exits = [mc.Exit('c_a', 'c_b', 'l1', ((0, 0, 0), (10, 10, 10)), (5.0, 5.0, 37.5), (5.0, 5.0, 1573.5)),
                 mc.Exit('c_b', 'c_a', 'l1', ((0, 0, 1536), (10, 10, 1546)), (5.0, 5.0, 1573.5), (5.0, 5.0, 37.5))]
        lines = mc.door_script(exits).splitlines()
        self.assertEqual(lines[:4], ['wait 900', 'god', 'notarget', 'wait 60'])
        first = lines.index('echo merged-check door 0 c_a c_b l1')
        self.assertEqual(lines[first + 1], 'ent_fire 1 set origin "5 5 37.5"')
        self.assertIn('echo merged-check door 1 c_b c_a l1', lines)
        self.assertEqual(lines[-1], 'quit')


class ResultTests(unittest.TestCase):
    LOG = (
        '[t] merged-check door 0 c_a c_b l1\n'
        '[t] [cof-story-transition] phase=section-move userid=1 from=c_a to=c_b landmark=l1 origin=5.000,5.000,1574.000\n'
        '[t] merged-check door 1 c_b c_a l1\n'
        '[t] [cof-story-transition] phase=section-move userid=1 from=c_b to=c_a landmark=l1 origin=300.000,5.000,37.000\n'
        '[t] merged-check door 2 c_a c_b l2\n')

    def test_each_door_moved_landed_off_or_did_nothing(self):
        exits = [mc.Exit('c_a', 'c_b', 'l1', ((0, 0, 0), (1, 1, 1)), (5, 5, 37), (5, 5, 1573.5)),
                 mc.Exit('c_b', 'c_a', 'l1', ((0, 0, 0), (1, 1, 1)), (5, 5, 1573), (5, 5, 37.5)),
                 mc.Exit('c_a', 'c_b', 'l2', ((0, 0, 0), (1, 1, 1)), (5, 5, 37), None)]
        results = mc.door_results(self.LOG, exits)
        self.assertEqual([r.verdict for r in results], ['ok', 'landed-off', 'no-move'])
        self.assertAlmostEqual(results[1].distance, 295.0, delta=0.5)

    def test_a_player_sent_straight_back_is_a_bounce(self):
        exits = [mc.Exit('c_a', 'c_b', 'l1', ((0, 0, 0), (1, 1, 1)), (5, 5, 37), (5, 5, 1573.5))]
        log = self.LOG.split('[t] merged-check door 1')[0] + \
            '[t] [cof-story-transition] phase=section-move userid=1 from=c_b to=c_a landmark=l1 origin=5.000,5.000,37.000\n'
        self.assertEqual(mc.door_results(log, exits)[0].verdict, 'bounced')


if __name__ == '__main__':
    unittest.main()
