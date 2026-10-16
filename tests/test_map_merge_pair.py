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


if __name__ == '__main__':
    unittest.main()
