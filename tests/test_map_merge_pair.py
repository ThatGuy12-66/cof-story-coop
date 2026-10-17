"""Map merging: two maps' geometry stacked into one file."""
import unittest

from tests.test_map_merge_bsp import retail
from tools.map_merge import bsp, merge


class ChooseDzTests(unittest.TestCase):
    def test_smallest_multiple_of_512_above_with_a_gap(self):
        self.assertEqual(merge.choose_dz((-500.0, 600.0), (-400.0, 583.0)), 1536.0)

    def test_below_when_above_does_not_fit(self):
        # 700 - dz must reach -300 - 128: at least 1128 down
        self.assertEqual(merge.choose_dz((-300.0, 3000.0), (-200.0, 700.0)), -1536.0)

    def test_nothing_fits(self):
        with self.assertRaises(ValueError):
            merge.choose_dz((-4000.0, 4000.0), (-100.0, 100.0))


class StyleTests(unittest.TestCase):
    def test_face_styles_mapped_and_others_kept(self):
        face = [0, 0, 0, 0, 0, 0, 32, 61, 255, 0]
        self.assertEqual(merge.remap_styles(face, {32: 34}), [0, 0, 0, 0, 0, 0, 34, 61, 255, 0])


class PairTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.a, cls.b = retail('c_apartment3'), retail('c_apartment4')
        za = (cls.a.models[0][2], cls.a.models[0][5])
        zb = (cls.b.models[0][2], cls.b.models[0][5])
        cls.dz = merge.choose_dz(za, zb)
        cls.lumps, cls.info = merge.merge_pair(cls.a, cls.b, cls.dz, {})
        cls.data = bsp.write_bytes(cls.lumps)

    def test_offset_is_a_multiple_of_512_clear_of_a(self):
        self.assertEqual(self.dz % 512, 0)
        self.assertGreaterEqual(self.b.models[0][2] + self.dz, self.a.models[0][5] + 128)
        self.assertLessEqual(self.b.models[0][5] + self.dz, bsp.WORLD_HALF - 16)

    def test_counts_add_up(self):
        m = bsp.Bsp.from_bytes(self.data)
        self.assertEqual(len(m.faces), len(self.a.faces) + len(self.b.faces))
        self.assertEqual(len(m.models), len(self.a.models) + len(self.b.models) - 1)
        self.assertEqual(len(m.leafs), len(self.a.leafs) + len(self.b.leafs) - 1)
        self.assertEqual(self.info.a_models, len(self.a.models))
        self.assertEqual(m.models[0][15], self.a.models[0][15] + self.b.models[0][15])

    def test_entity_lump_is_one_worldspawn_with_both_wad_lists(self):
        m = bsp.Bsp.from_bytes(self.data)
        self.assertEqual(len(m.ents), 1)
        self.assertEqual(bsp.ent_get(m.ents[0], 'classname'), 'worldspawn')
        wads = merge.wad_names(bsp.ent_get(m.ents[0], 'wad', ''))
        for x in (self.a, self.b):
            self.assertLessEqual(merge.wad_names(bsp.ent_get(x.ents[0], 'wad', '')), wads)

    def test_deterministic(self):
        again, _ = merge.merge_pair(self.a, self.b, self.dz, {})
        self.assertEqual(bsp.write_bytes(again), self.data)

    def test_worldspawn_is_the_merged_worldspawn(self):
        m = bsp.Bsp.from_bytes(self.data)
        self.assertEqual(m.ents[0], merge.merged_worldspawn([self.a.ents[0], self.b.ents[0]]))


class WorldspawnTests(unittest.TestCase):
    """The merged worldspawn: hl.dll reads MaxRange (the view distance) from it, so it takes the largest."""

    def world(self, **kv):
        return [['classname', 'worldspawn']] + [[k, v] for k, v in kv.items()]

    def test_view_distance_is_the_largest(self):
        merged = merge.merged_worldspawn([self.world(MaxRange='4096'), self.world(MaxRange='8000'),
                                          self.world(MaxRange='6000')])
        self.assertEqual(bsp.ent_get(merged, 'MaxRange'), '8000')

    def test_first_members_value_kept_when_it_is_the_largest(self):
        merged = merge.merged_worldspawn([self.world(MaxRange='10000'), self.world(MaxRange='4096')])
        self.assertEqual(bsp.ent_get(merged, 'MaxRange'), '10000')

    def test_absent_is_the_game_default(self):
        # hl.dll's default view distance is 4096: a smaller value elsewhere adds nothing, a larger one is set
        self.assertIsNone(bsp.ent_get(merge.merged_worldspawn([self.world(), self.world(MaxRange='2000')]), 'MaxRange'))
        self.assertEqual(bsp.ent_get(merge.merged_worldspawn([self.world(), self.world(MaxRange='8000')]), 'MaxRange'),
                         '8000')


if __name__ == '__main__':
    unittest.main()
