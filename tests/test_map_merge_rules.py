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


class ComposeTests(unittest.TestCase):
    def test_entities_are_moved_renumbered_renamed_and_restyled(self):
        world = ent(classname='worldspawn', wad='a.wad;b.wad')
        m0 = [ent(classname='worldspawn', wad='a.wad'), ent(classname='info_landmark', targetname='land1', origin='1 2 3'),
              ent(classname='trigger_relay', target='lift')]
        m1 = [ent(classname='worldspawn'), ent(classname='info_landmark', targetname='land1', origin='4  5 -6.5'),
              ent(classname='func_door', targetname='lift', model='*3'), ent(classname='light', style='32', targetname='l'),
              ent(classname='multi_manager', targetname='mm', land1='0.5', **{'land1#1': '1'}),
              ent(classname='path_corner', targetname='p', message='land1'), ent(classname='game_text', message='land1')]
        s = rules.section_rules([('c_a', m0), ('c_b', m1)], [0.0, 1536.0], [10, 5], [(-1.0, 1.0), (1535.0, 1537.0)],
                                face_styles=[{32}, set()])
        text = rules.compose_reference(world, [m0, m1], s).decode('latin-1')
        ents = bsp_parse(text)
        self.assertEqual(ents[0], world)
        self.assertEqual(ents[1], m0[1])                                     # member 0: unchanged
        self.assertEqual(ents[2], ent(classname='trigger_relay', target='lift_x0'))
        self.assertEqual(ents[3], ent(classname='info_landmark', targetname='land1_m1', origin='4 5 1529.5'))
        self.assertEqual(ents[4], ent(classname='func_door', targetname='lift', model='*12'))
        self.assertEqual(ents[5], ent(classname='light', style='33', targetname='l'))
        self.assertEqual(ents[6], [['classname', 'multi_manager'], ['targetname', 'mm'], ['land1_m1', '0.5'],
                                   ['land1_m1#1', '1']])
        self.assertEqual(ents[7], ent(classname='path_corner', targetname='p', message='land1_m1'))
        self.assertEqual(ents[8], ent(classname='game_text', message='land1'))   # text, not a target
        self.assertEqual(len(ents), 9)


    def test_every_key_the_game_reads_as_a_name_is_renamed(self):
        # docs/research/2026-10-18-cof-decompilation-survey.md 5.5 (C1 section 7.4): keys outside the old list
        m0 = [ent(classname='worldspawn'), ent(classname='info_target', targetname='spot')]
        m1 = [ent(classname='worldspawn'), ent(classname='info_target', targetname='spot'),
              ent(classname='func_wall', movewith='spot'),
              ent(classname='trigger_camera', moveto='spot', message='spot', m_iszViewEntity='spot'),
              ent(classname='trigger_changetarget', m_iszNewTarget='spot'),
              ent(classname='cof_teleport', message='spot'), ent(classname='watcher_count', noise='spot', noise2='spot'),
              ent(classname='cof_skipmanager', entity='spot'), ent(classname='cof_clothesmenu', cameraent='spot', simonent='spot'),
              ent(classname='cof_changelevel', message='spot'),        # a map name, not an entity
              ent(classname='trigger_changevalue', netname='spot'),    # netname is a name for every class
              ent(classname='game_text', noise='spot')]                # noise is a sound here
        s = rules.section_rules([('c_a', m0), ('c_b', m1)], [0.0, 1536.0], [1, 1], [(-1.0, 1.0), (1535.0, 1537.0)])
        ents = bsp_parse(rules.compose_reference(ent(classname='worldspawn'), [m0, m1], s).decode('latin-1'))
        moved = [e for e in ents[3:]]
        self.assertEqual(moved[0], ent(classname='func_wall', movewith='spot_m1'))
        self.assertEqual(moved[1], ent(classname='trigger_camera', moveto='spot_m1', message='spot_m1', m_iszViewEntity='spot_m1'))
        self.assertEqual(moved[2], ent(classname='trigger_changetarget', m_iszNewTarget='spot_m1'))
        self.assertEqual(moved[3], ent(classname='cof_teleport', message='spot_m1'))
        self.assertEqual(moved[4], ent(classname='watcher_count', noise='spot_m1', noise2='spot_m1'))
        self.assertEqual(moved[5], ent(classname='cof_skipmanager', entity='spot_m1'))
        self.assertEqual(moved[6], ent(classname='cof_clothesmenu', cameraent='spot_m1', simonent='spot_m1'))
        self.assertEqual(moved[7], ent(classname='cof_changelevel', message='spot'))
        self.assertEqual(moved[8], ent(classname='trigger_changevalue', netname='spot_m1'))
        self.assertEqual(moved[9], ent(classname='game_text', noise='spot'))

    def test_the_ladder_and_door_keys_the_game_looks_up_by_name_are_renamed(self):
        # CoF_Ladder.cpp: the climb ends at the entities topent/bottoment name; CoF_InterDoor.cpp: propertarget and
        # useunlock name entities (UTIL_FindEntityByTargetname); c_roofs2's and c_subtunnel2's ladders use them
        m0 = [ent(classname='worldspawn'), ent(classname='info_target', targetname='spot')]
        m1 = [ent(classname='worldspawn'), ent(classname='info_target', targetname='spot'),
              ent(classname='cof_ladder_manager', topent='spot', bottoment='spot'),
              ent(classname='inter_door', propertarget='spot', useunlock='spot'),
              ent(classname='inter_door_coop', propertarget='spot', useunlock='spot')]
        s = rules.section_rules([('c_a', m0), ('c_b', m1)], [0.0, 1536.0], [1, 1], [(-1.0, 1.0), (1535.0, 1537.0)])
        ents = bsp_parse(rules.compose_reference(ent(classname='worldspawn'), [m0, m1], s).decode('latin-1'))
        self.assertEqual(ents[3:], [ent(classname='cof_ladder_manager', topent='spot_m1', bottoment='spot_m1'),
                                    ent(classname='inter_door', propertarget='spot_m1', useunlock='spot_m1'),
                                    ent(classname='inter_door_coop', propertarget='spot_m1', useunlock='spot_m1')])

    def test_a_reference_through_a_class_key_is_cut_too(self):
        m0 = [ent(classname='info_target', targetname='spot')]
        m1 = [ent(classname='cof_teleport', message='spot')]
        s = rules.section_rules([('c_a', m0), ('c_b', m1)], [0.0, 512.0], [1, 1], [(-1.0, 1.0), (511.0, 513.0)])
        self.assertEqual(s[1].renames, {'spot': 'spot_x1'})

    def test_the_merged_files_own_entities_come_last_unchanged(self):
        # the entities the merger itself adds (named_map_entities): after every member's, so no member entity's
        # number changes
        m0 = [ent(classname='worldspawn'), ent(classname='info_target', targetname='spot', origin='1 2 3')]
        s = rules.section_rules([('c_a', m0)], [0.0], [1], [(-1.0, 1.0)])
        egg = ent(classname='dave_benson_phillips', origin='613 -3156 -702')
        ents = bsp_parse(rules.compose_reference(ent(classname='worldspawn'), [m0], s, extras=[egg]).decode('latin-1'))
        self.assertEqual(ents, [ent(classname='worldspawn'), m0[1], egg])


class NamedMapEntityTests(unittest.TestCase):
    """hl.dll's worldspawn spawns entities of its own only in a map of an exact name; a merged map has another name, so
    the merger puts them into that member's section (evidence/map-merge-plan-20261017/hl-names/hl-map-names-re.md 2.4)."""

    def sections(self, members, dzs):
        return rules.section_rules(members, dzs, [1] * len(members), [(-1.0 + dz, 1.0 + dz) for dz in dzs])

    def test_a_merged_c_start_gets_the_easter_egg_in_its_section(self):
        members = [('c_start2', [ent(classname='worldspawn')]), ('c_start', [ent(classname='worldspawn')])]
        s = self.sections(members, [0.0, -2048.0])
        self.assertEqual(rules.named_map_entities(members, s),
                         [ent(classname='dave_benson_phillips', origin='613 -3156 -2750')])

    def test_a_c_start_that_already_has_the_egg_gets_no_second(self):
        members = [('c_start', [ent(classname='worldspawn'), ent(classname='dave_benson_phillips', origin='1 2 3')]),
                   ('c_start2', [ent(classname='worldspawn')])]
        self.assertEqual(rules.named_map_entities(members, self.sections(members, [0.0, 2048.0])), [])

    def test_other_maps_get_nothing(self):
        members = [('c_apartment3', [ent(classname='worldspawn')]), ('c_apartment4', [ent(classname='worldspawn')])]
        self.assertEqual(rules.named_map_entities(members, self.sections(members, [0.0, 1536.0])), [])


def bsp_parse(text):
    from tools.map_merge import bsp
    return bsp.parse_ents(text)


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
