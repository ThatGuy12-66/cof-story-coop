"""Map merging: the per-section rules (renames, light styles, models, landmarks) and the .sections file."""
import unittest

from tests.test_map_merge_bsp import retail
from tools.map_merge import rules


def ent(**kv):
    return [[k, v] for k, v in kv.items()]


def rules_for(*members, models=None, dzs=None):
    n = len(members)
    return rules.section_rules([(f'c_m{k}', ents) for k, ents in enumerate(members)],
                               dzs or [512.0 * k for k in range(n)], models or [3] * n,
                               [(-100.0 + 512 * k, 100.0 + 512 * k) for k in range(n)])


class RenameTests(unittest.TestCase):
    def test_second_member_renames_a_name_the_first_defines(self):
        s = rules_for([ent(classname='info_target', targetname='door1')],
                      [ent(classname='info_target', targetname='door1'), ent(classname='trigger_relay', target='door1')])
        self.assertEqual(s[0].renames, {})
        self.assertEqual(s[1].renames, {'door1': 'door1_m1'})

    def test_three_members_each_get_their_own_name(self):
        e = [ent(classname='info_target', targetname='x')]
        s = rules_for(e, e, e)
        self.assertEqual([x.renames for x in s], [{}, {'x': 'x_m1'}, {'x': 'x_m2'}])

    def test_new_name_never_collides(self):
        s = rules_for([ent(classname='info_target', targetname='a')],
                      [ent(classname='info_target', targetname='a'), ent(classname='info_target', targetname='a_m1')])
        self.assertEqual(s[1].renames['a'], 'a_m1_m1')

    def test_reference_to_another_members_name_is_cut(self):
        # member 0 fires "lift", which only member 1 defines: in the merged map it must still fire nothing
        s = rules_for([ent(classname='trigger_relay', target='lift')],
                      [ent(classname='func_door', targetname='lift')])
        self.assertEqual(s[0].renames, {'lift': 'lift_x0'})
        self.assertEqual(s[1].renames, {})

    def test_multi_manager_keys_count_as_references(self):
        s = rules_for([ent(classname='info_target', targetname='t')],
                      [ent(classname='multi_manager', targetname='mm', t='0.5')])
        self.assertEqual(s[1].renames, {'t': 't_x1'})

    def test_name_with_a_space_cannot_be_renamed(self):
        with self.assertRaises(ValueError):
            rules_for([ent(classname='info_target', targetname='a b')], [ent(classname='info_target', targetname='a b')])


class StyleTests(unittest.TestCase):
    def test_clashing_switchable_style_moves_to_the_lowest_free_number(self):
        light = [ent(classname='light', targetname='l', style='32')]
        s = rules_for(light, [ent(classname='light', targetname='k', style='32')],
                      models=[3, 3])
        self.assertEqual(s[1].styles, {32: 33})

    def test_bump_styles_61_to_63_are_never_assigned(self):
        s = rules.section_rules([('c_a', [ent(classname='light', targetname=f'l{v}', style=str(v)) for v in range(32, 60)]),
                                 ('c_b', [ent(classname='light', targetname='k', style='32')])],
                                [0.0, 512.0], [3, 3], [(-1.0, 1.0), (511.0, 513.0)])
        self.assertEqual(s[1].styles, {32: 60})

    def test_no_free_style_raises(self):
        # 29 styles 32-60 taken by member 0: nothing free for member 1
        with self.assertRaises(ValueError):
            rules.section_rules([('c_a', [ent(classname='light', targetname=f'l{v}', style=str(v)) for v in range(32, 61)]),
                                 ('c_b', [ent(classname='light', targetname='k', style='32')])],
                                [0.0, 512.0], [3, 3], [(-1.0, 1.0), (511.0, 513.0)])

    def test_face_styles_take_part(self):
        s = rules.section_rules([('c_a', []), ('c_b', [])], [0.0, 512.0], [3, 3], [(-1.0, 1.0), (511.0, 513.0)],
                                face_styles=[{32, 61}, {32, 62}])
        self.assertEqual(s[1].styles, {32: 33})


class ModelAndLandmarkTests(unittest.TestCase):
    def test_model_numbers_follow_the_merge_order(self):
        s = rules_for([], [], [], models=[208, 194, 50])
        self.assertEqual([(x.first_model, x.models) for x in s], [(1, 207), (208, 193), (401, 49)])

    def test_landmarks_listed_after_renames(self):
        s = rules_for([ent(classname='info_landmark', targetname='land1')],
                      [ent(classname='info_landmark', targetname='land1'), ent(classname='info_landmark', targetname='only')])
        self.assertEqual(s[0].landmarks, ['land1'])
        self.assertEqual(s[1].landmarks, ['land1_m1', 'only'])


class FileTests(unittest.TestCase):
    def test_text_round_trip(self):
        s = rules_for([ent(classname='info_landmark', targetname='land1'), ent(classname='light', targetname='a', style='32')],
                      [ent(classname='info_landmark', targetname='land1'), ent(classname='light', targetname='b', style='32')])
        text = rules.to_text('cm_m0', s)
        self.assertTrue(text.startswith('cof-sections 1 cm_m0\n'))
        self.assertEqual(rules.parse(text), ('cm_m0', s))

    def test_parse_rejects_a_line_outside_a_section(self):
        with self.assertRaises(ValueError):
            rules.parse('cof-sections 1 cm_x\nrename a b\n')


class RetailTests(unittest.TestCase):
    def test_apartment3_and_4_rename_the_names_the_spike_found(self):
        a, b = retail('c_apartment3'), retail('c_apartment4')
        s = rules.section_rules([('c_apartment3', a.ents), ('c_apartment4', b.ents)], [0.0, 1536.0],
                                [len(a.models), len(b.models)], [(-401.0, 698.0), (1251.0, 2234.0)])
        spike = ['changelevel1', 'changelevel2', 'changelevel3', 'exitdbl1', 'exitdbl2', 'exitdbl3', 'land1', 'land2',
                 'land3', 'n', 'objectiveee']
        for name in spike:
            self.assertEqual(s[1].renames.get(name), name + '_m1', name)
        self.assertFalse(set(s[0].renames) & set(spike))
        self.assertEqual(s[1].styles, {32: 33})
        self.assertIn('land1_m1', s[1].landmarks)


if __name__ == '__main__':
    unittest.main()
