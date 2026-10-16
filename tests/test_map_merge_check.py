"""Map merging: the check that a merged pair is the two maps, nothing lost or moved wrongly."""
import copy
import unittest

from tests.test_map_merge_bsp import retail
from tools.map_merge import bsp, check, merge


class CheckTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.a, cls.b = retail('c_apartment3'), retail('c_apartment4')
        dz = merge.choose_dz((cls.a.models[0][2], cls.a.models[0][5]), (cls.b.models[0][2], cls.b.models[0][5]))
        cls.style_map = {}
        lumps, cls.info = merge.merge_pair(cls.a, cls.b, dz, cls.style_map)
        cls.merged = bsp.Bsp.from_bytes(bsp.write_bytes(lumps))

    def problems(self, merged):
        return check.check_pair(self.a, self.b, merged, self.info, self.style_map, points=1500)

    def test_clean_merge_has_no_problems(self):
        self.assertEqual(self.problems(self.merged), [])

    def test_node_child_past_the_end_is_found(self):
        broken = copy.deepcopy(self.merged)
        broken.nodes[5][1] = len(broken.nodes) + 3
        self.assertTrue(any('node 5 child' in p for p in self.problems(broken)))

    def test_moved_light_is_found(self):
        broken = copy.deepcopy(self.merged)
        light = broken.lighting
        # a face of b whose light changes when read one texel later (uniform light would not)
        face = next(i for i, f in enumerate(broken.faces)
                    if i > self.info.Aw and f[9] >= 0 and light[f[9]:f[9] + 48] != light[f[9] + 3:f[9] + 51])
        broken.faces[face][9] += 3
        self.assertTrue(any('light' in p for p in self.problems(broken)))

    def test_unmapped_style_is_found(self):
        # c_apartment4's faces use styles 0 and 61-63 only; map 61 for the test's sake
        lumps, info = merge.merge_pair(self.a, self.b, self.info.dz, {61: 40})
        merged = bsp.Bsp.from_bytes(bsp.write_bytes(lumps))
        self.assertEqual(check.check_pair(self.a, self.b, merged, info, {61: 40}, points=200), [])
        face = next(i for i, f in enumerate(merged.faces) if 40 in f[5:9])
        merged.faces[face][5:9] = [61 if s == 40 else s for s in merged.faces[face][5:9]]
        self.assertTrue(any('styles' in p for p in check.check_pair(self.a, self.b, merged, info, {61: 40}, points=200)))


if __name__ == '__main__':
    unittest.main()
