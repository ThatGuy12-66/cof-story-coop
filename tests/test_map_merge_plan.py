"""Map merging: the planner's checks before a group goes into the list."""
import hashlib
from pathlib import Path
import shutil
import tempfile
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

    def test_play_everywhere_sound_that_starts_with_the_map_is_refused(self):
        # it would go to every section's players in the sign-on data
        members = [('c_a', [ent(classname='ambient_generic', message='ambience/hum.wav', spawnflags='1')]),
                   ('c_b', [ent(classname='ambient_generic', message='ambience/x.wav', spawnflags='17'),
                            ent(classname='ambient_generic', message='ambience/y.wav', spawnflags='4')])]
        found = plan.everywhere_sound_problems(members)
        self.assertEqual(len(found), 1)
        self.assertIn('hum.wav', found[0])

    def test_shared_globalname_is_refused(self):
        members = [('c_a', [ent(classname='func_door', globalname='lift')]),
                   ('c_b', [ent(classname='func_door', globalname='lift')])]
        self.assertTrue(plan.globalname_problems(members))

    def test_names_the_engine_cannot_hold_are_refused(self):
        # cof_sections.h: 512 renames, 32 styles, 64 landmarks a section, names under 64 characters
        def section(**extra):
            return rules.Section(map='c_a', dz=0.0, first_model=1, models=1, bounds=(0.0, 1.0), **extra)
        self.assertEqual(plan.section_limit_problems([section(renames={'a': 'a_m1'}, landmarks=['l1'])]), [])
        for bad in (section(renames={f'n{i}': f'n{i}_m1' for i in range(513)}),
                    section(styles={s: s + 1 for s in range(33)}),
                    section(landmarks=[f'l{i}' for i in range(65)]),
                    section(renames={'a' * 61: 'a' * 61 + '_m1'})):
            with self.subTest(bad):
                self.assertTrue(plan.section_limit_problems([bad]))


class WorldKeyTests(unittest.TestCase):
    """The worldspawn keys hl.dll reads: a merged map has only its first member's worldspawn."""

    def check(self, *worlds, accepted=None):
        return plan.world_key_problems([f'c_{k}' for k in 'abcd'[:len(worlds)]], list(worlds), accepted or {})

    def test_player_models_never_block(self):
        # story co-op sets every player model in the engine (sv_cof_story.c); worldspawn models are not shown
        problems, notes = self.check({'player1': 'police1', 'player4': 'police4'}, {})
        self.assertEqual(problems, [])
        self.assertTrue(any('player1' in n for n in notes), notes)

    def test_materials_file_absent_is_the_default_file(self):
        self.assertEqual(self.check({'materialstxt': 'sound/materials.txt'}, {})[0], [])
        self.assertTrue(self.check({'materialstxt': 'sound/other.txt'}, {})[0])

    def test_view_distance_takes_the_largest(self):
        problems, notes = self.check({'MaxRange': '4096'}, {'MaxRange': '8000'}, {'MaxRange': '6000'})
        self.assertEqual(problems, [])
        self.assertTrue(any('MaxRange' in n and '8000' in n for n in notes), notes)
        self.assertTrue(self.check({'MaxRange': 'far'}, {'MaxRange': '8000'})[0])

    def test_wave_height_absent_is_zero(self):
        self.assertEqual(self.check({'WaveHeight': '0.0'}, {})[0], [])
        self.assertTrue(self.check({'WaveHeight': '0.0'}, {'WaveHeight': '2'})[0])

    def test_other_differences_block_until_accepted(self):
        self.assertTrue(self.check({'sounds': '1'}, {})[0])
        self.assertEqual(self.check({'sounds': '1'}, {}, accepted={'sounds': 'a reason'})[0], [])


class NameTests(unittest.TestCase):
    def test_maps_named_only_in_dead_code_or_as_destinations_can_be_planned(self):
        # hl-map-names-re.md: c_city, c_forest3, c_subwaysick1 only in an unreferenced function, c_intro a
        # single-player destination, c_ending* a substring test the group name keeps
        for m in ('c_city', 'c_forest3', 'c_subwaysick1', 'c_intro', 'c_ending1b', 'c_ending5c'):
            with self.subTest(m):
                self.assertFalse(plan.groups.hl_named(m))
        for m in ('c_nightmare', 'c_game_menu1', 'c_start'):
            with self.subTest(m):
                self.assertTrue(plan.groups.hl_named(m))


class RecordTests(unittest.TestCase):
    """Adding a group to the list: planned, built, and recorded with its hashes."""

    def setUp(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        self.path = folder / 'map_groups.json'
        self.path.write_text('{"schema": 1, "groups": []}\n', encoding='utf-8')

    def test_record_adds_the_group_with_its_hashes(self):
        retail('c_apartment3')
        group = plan.record_group(MAPS, ['c_apartment3', 'c_apartment4'], wave=2, path=self.path)
        loaded = plan.groups.load_groups(self.path)
        self.assertEqual([g.name for g in loaded], ['cm_apartment3'])
        self.assertEqual(loaded[0], group)
        data, text = plan.groups.build_group(MAPS, group)
        self.assertEqual(group.bsp_sha256, hashlib.sha256(data).hexdigest())
        self.assertEqual(group.sections_sha256, hashlib.sha256(text.encode('ascii')).hexdigest())
        self.assertEqual((group.wave, group.enabled), (2, True))

    def test_record_refuses_a_group_the_planner_refuses(self):
        retail('c_start')
        with self.assertRaises(ValueError):
            plan.record_group(MAPS, ['c_start', 'c_start2'], wave=2, path=self.path)
        self.assertEqual(plan.groups.load_groups(self.path), [])

    def test_record_replaces_a_group_of_the_same_name(self):
        retail('c_apartment3')
        plan.record_group(MAPS, ['c_apartment3', 'c_apartment4'], wave=2, path=self.path)
        plan.record_group(MAPS, ['c_apartment3', 'c_apartment5'], wave=2, path=self.path)
        self.assertEqual([g.members for g in plan.groups.load_groups(self.path)], [['c_apartment3', 'c_apartment5']])


if __name__ == '__main__':
    unittest.main()
