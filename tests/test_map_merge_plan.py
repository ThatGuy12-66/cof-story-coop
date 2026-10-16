"""Map merging: the planner's checks before a group goes into the list."""
import unittest

from tests.test_map_merge_bsp import MAPS, retail
from tools.map_merge import plan, rules


def ent(**kv):
    return [[k, v] for k, v in kv.items()]


class RetailPlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        retail('c_apartment3')
        cls.members = ['c_apartment3', 'c_apartment4', 'c_apartment5']
        cls.report = plan.plan_group(MAPS, cls.members)

    def test_numbers_are_reported(self):
        n = self.report.numbers
        self.assertLessEqual(n['lightmap pages'], 256)
        self.assertEqual(n['info_node'], 98 + 65 + 13)
        self.assertGreater(n['entities'], 1400)

    def test_worldspawn_differences_are_problems_until_accepted(self):
        differing = [p for p in self.report.problems if p.startswith('worldspawn')]
        if not differing:
            self.skipTest('these maps agree on every worldspawn key hl.dll reads')
        keys = {p.split()[1] for p in differing}
        accepted = plan.plan_group(MAPS, self.members, accepted={k: 'test' for k in keys})
        self.assertFalse([p for p in accepted.problems if p.startswith('worldspawn')])

    def test_doors_between_members_have_landmarks_on_both_sides(self):
        self.assertFalse([p for p in self.report.problems if 'landmark' in p], self.report.problems)
        self.assertTrue(any('c_apartment3 -> c_apartment4' in note for note in self.report.notes), self.report.notes)


class LandmarkTests(unittest.TestCase):
    def test_door_without_a_landmark_on_either_side_is_a_note(self):
        # c_apartment2's changelevel4 names land4, which neither map has: the original game spawns the
        # player at the map start, and so does a section move without a landmark
        retail('c_apartment1')
        report = plan.plan_group(MAPS, ['c_apartment1', 'c_apartment2'])
        self.assertFalse([p for p in report.problems if 'land4' in p], report.problems)
        self.assertTrue(any('land4' in n and 'start' in n for n in report.notes), report.notes)

    def test_changetarget_on_a_door_between_members_is_a_problem(self):
        # hl.dll fires it in the destination map; the composer would rename it by the source's rules
        door = [['classname', 'trigger_changelevel'], ['map', 'c_b'], ['landmark', 'l1'], ['changetarget', 'arrive']]
        problems, _ = plan.door_checks(['c_a', 'c_b'], [['l1'], ['l1']], [[door], []])
        self.assertTrue(any('changetarget' in p for p in problems), problems)

    def test_landmark_on_one_side_only_is_a_problem(self):
        problems, notes = plan.door_checks(['c_a', 'c_b'], [['l1'], []],
                                           [[[['classname', 'trigger_changelevel'], ['map', 'c_b'], ['landmark', 'l1']]], []])
        self.assertTrue(problems)


class RefusalTests(unittest.TestCase):
    def test_map_named_by_the_game_dll_is_refused(self):
        retail('c_start')
        report = plan.plan_group(MAPS, ['c_start', 'c_start2'])
        self.assertFalse(report.ok)
        self.assertTrue(any('c_start' in p and 'game DLL' in p for p in report.problems), report.problems)

    def test_unlisted_reference_reported(self):
        members = [('c_a', [ent(classname='func_door', targetname='door1')]),
                   ('c_b', [ent(classname='func_door', targetname='door1'),
                            ent(classname='monster_x', enemyname='door1'), ent(classname='light', style='1')])]
        sections = rules.section_rules(members, [0.0, 512.0], [2, 2], [(0.0, 1.0), (512.0, 513.0)])
        found = plan.reference_problems(members, sections)
        self.assertEqual(len(found), 1)
        self.assertIn('enemyname', found[0])

    def test_origin_brush_entity_in_a_moved_member_is_refused(self):
        # its geometry is local to its origin: moving the section's geometry and its origin would move it twice
        members = [('c_a', [ent(classname='func_door_rotating', model='*1', origin='10 20 30')]),
                   ('c_b', [ent(classname='func_rotating', model='*2', origin='0 0 64'), ent(classname='func_wall', model='*3')])]
        found = plan.origin_brush_problems(members)
        self.assertEqual(len(found), 1)
        self.assertIn('c_b', found[0])

    def test_shared_globalname_is_refused(self):
        members = [('c_a', [ent(classname='func_door', globalname='lift')]),
                   ('c_b', [ent(classname='func_door', globalname='lift')])]
        self.assertTrue(plan.globalname_problems(members))


if __name__ == '__main__':
    unittest.main()
