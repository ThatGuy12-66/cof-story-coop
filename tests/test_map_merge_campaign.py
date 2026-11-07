"""Map merging: every exit of the story campaign through the recorded groups (tools/map_merge/campaign.py)."""
import unittest

from tests.test_enhanced_sections import ROUTE_FILES
from tests.test_map_merge_bsp import MAPS, retail
from tools.map_merge import bsp, campaign, rules


def ent(**kv):
    return [[k, v] for k, v in kv.items()]


def landmark(name):
    return ent(classname='info_landmark', targetname=name)


def door(level, mark=None):
    return ent(classname='trigger_changelevel', map=level, **({'landmark': mark} if mark else {}))


# c_a and c_b share the landmark ab; c_a's door to c_c has none; c_c's door back names one that is nowhere
MAPS_ABC = {
    'c_a': [landmark('ab'), door('c_b', 'ab'), door('c_c')],
    'c_b': [landmark('ab'), door('c_a', 'ab')],
    'c_c': [door('c_a', 'nowhere')],
}


def group(name, *members):
    zero = [0.0] * len(members)
    return {name: rules.section_rules([(m, MAPS_ABC[m]) for m in members], zero, [1] * len(members),
                                      [(0.0, 0.0)] * len(members))}


class RouteTests(unittest.TestCase):
    """campaign.route is the engine's CoF_SectionsRoute: the cases of test_enhanced_sections.RouteTests."""

    def test_routes_as_the_engine_does(self):
        cur, other = rules.parse(ROUTE_FILES['cur.txt']), rules.parse(ROUTE_FILES['other.txt'])

        def show(loaded, level, mark, dest):
            r = campaign.route(loaded, level, mark, dest)
            return (f'{"move" if r.section_move else "load"} {r.to_section} {r.file} dest {r.dest_landmark} '
                    f'source {r.source_landmark}')
        self.assertEqual([show(*case) for case in (
            (cur, 'c_apartment4', 'land1', None), (cur, 'c_apartment3', 'land1_m1', None),
            (cur, 'c_apartment5', 'ap5la', None), (cur, 'c_apartment3', 'ap5la_m2', None),
            (cur, 'c_apartment1', 'land5', other), (cur, 'c_apartment1', 'land5', None),
            (cur, 'c_city', 'land9', None), (cur, 'c_apartment4', 'land4', None),
            (None, 'c_apartment2', 'land1', other))], [
            'move 1 cm_apartment3 dest land1_m1 source land1',
            'move 0 cm_apartment3 dest land1 source land1_m1',
            'move 2 cm_apartment3 dest ap5la_m2 source ap5la',
            'move 0 cm_apartment3 dest ap5la source ap5la_m2',
            'load 0 cm_apartment1 dest land5 source land5',
            'load -1 c_apartment1 dest land5 source land5',
            'load -1 c_city dest land9 source land9',
            'move 1 cm_apartment3 dest land4 source land4',
            'load 1 cm_apartment1 dest land1_m1 source land1',
        ])


class CheckTests(unittest.TestCase):
    def test_loads_of_a_merged_file_and_of_plain_maps(self):
        report = campaign.check(MAPS_ABC, group('cm_c', 'c_c', 'c_b'))
        self.assertEqual((report.exits, report.moves, report.merged_loads, report.plain_loads), (4, 0, 2, 2))
        self.assertEqual(report.problems, [])
        # the original maps' own: c_c's landmark is nowhere, a story transition refuses it unmerged too
        self.assertEqual(len(report.notes), 1)
        self.assertIn('c_c -> c_a [nowhere]', report.notes[0])

    def test_a_load_without_a_landmark_into_a_later_section_lands_in_the_first(self):
        report = campaign.check(MAPS_ABC, group('cm_b', 'c_b', 'c_c'))
        self.assertEqual(len(report.problems), 1)
        self.assertIn('c_a -> c_c [no landmark]', report.problems[0])
        self.assertIn('section 0 (c_b)', report.problems[0])

    def test_section_moves_land_on_the_renamed_landmark(self):
        report = campaign.check(MAPS_ABC, group('cm_a', 'c_a', 'c_b'))
        self.assertEqual(report.moves, 2)
        self.assertEqual(report.problems, [])

    def test_a_section_move_missing_a_landmark_the_original_has_is_a_problem(self):
        merged = group('cm_a', 'c_a', 'c_b')
        merged['cm_a'][1].landmarks.clear()
        report = campaign.check(MAPS_ABC, merged)
        # both ways: c_b's live landmark (ab_m1) no longer leads back to its original name either
        self.assertEqual([p.split(':')[0] for p in report.problems], ['c_a -> c_b [ab]', 'c_b -> c_a [ab]'])

    def test_a_transition_the_merge_breaks_is_a_problem(self):
        # the landmark twice in the merged file: the story transition is refused, unmerged it was not
        merged = group('cm_b', 'c_b', 'c_c')
        merged['cm_b'][1].landmarks.append('ab')
        report = campaign.check(MAPS_ABC, merged)
        refused = [p for p in report.problems if 'refused' in p]
        self.assertEqual(len(refused), 2, report.problems)


class RetailCampaignTests(unittest.TestCase):
    def test_every_story_exit_works_through_the_recorded_groups(self):
        retail('c_start')
        if not campaign.STORY_MAPS.is_file():   # research data under evidence/ (not in git), like the retail maps
            self.skipTest(f'the story map list is not on this PC: {campaign.STORY_MAPS}')
        ents = {m: bsp.Bsp.read(MAPS / f'{m}.bsp').ents for m in campaign.story_maps()}
        report = campaign.check(ents, campaign.recorded(MAPS))
        self.assertEqual(report.exits, 197)
        self.assertEqual(report.problems, [])


if __name__ == '__main__':
    unittest.main()
