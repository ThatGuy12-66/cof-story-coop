"""Map merging: BSP30 files read and written back unchanged."""
import unittest

from tests.native import ROOT
from tools.map_merge import bsp

MAPS = ROOT / 'baseline/Cry of Fear/cryoffear/maps'


def retail(name):
    path = MAPS / f'{name}.bsp'
    if not path.is_file():
        raise unittest.SkipTest('retail map fixtures are not installed')
    return bsp.Bsp.read(path)


class BspTests(unittest.TestCase):
    def test_round_trip_keeps_every_lump(self):
        original = retail('c_apartment5')
        again = bsp.Bsp.from_bytes(bsp.write_bytes(bsp.to_lumps(original)))
        for name in ('planes', 'vertexes', 'nodes', 'texinfo', 'faces', 'clipnodes', 'leafs', 'marksurfaces',
                     'edges', 'surfedges', 'models'):
            self.assertEqual(getattr(again, name), getattr(original, name), name)
        self.assertEqual(again.vis, original.vis)
        self.assertEqual(again.lighting, original.lighting)
        self.assertEqual(again.textures, original.textures)
        self.assertEqual(again.ents, original.ents)

    def test_entity_text_round_trip(self):
        ents = retail('c_apartment5').ents
        text = bsp.serialize_ents(ents)
        self.assertTrue(text.endswith(b'\0'))
        self.assertEqual(bsp.parse_ents(text[:-1].decode('latin-1')), ents)

    def test_a_brace_inside_a_quoted_value_belongs_to_the_value(self):
        # decals are named like "{ding10" (GoldSrc's transparent textures); the engine's tokenizer reads such a value
        # whole, so the entity keeps its keys (it lost them all: merged maps logged "Decal has invalid texture!")
        text = ('{\n"classname" "worldspawn"\n}\n{\n"origin" "1 2 3"\n"texture" "{ding10"\n"classname" "infodecal"\n}\n'
                '{\n"message" "a } b {"\n"classname" "env_message"\n}\n')
        self.assertEqual(bsp.parse_ents(text), [
            [['classname', 'worldspawn']],
            [['origin', '1 2 3'], ['texture', '{ding10'], ['classname', 'infodecal']],
            [['message', 'a } b {'], ['classname', 'env_message']]])

    def test_the_apartment_decals_keep_their_names_and_places(self):
        decals = [e for e in retail('c_apartment1').ents if bsp.ent_get(e, 'classname') == 'infodecal']
        self.assertEqual(len(decals), 2)
        for e in decals:
            self.assertTrue(bsp.ent_get(e, 'texture', '').startswith('{'), e)
            self.assertEqual(len(bsp.ent_get(e, 'origin', '').split()), 3, e)

    def test_written_file_is_bsp30_with_aligned_lumps(self):
        data = bsp.write_bytes(bsp.to_lumps(retail('c_apartment5')))
        self.assertEqual(data[:4], b'\x1e\x00\x00\x00')
        for i in range(15):
            offset = int.from_bytes(data[4 + i * 8:8 + i * 8], 'little')
            self.assertEqual(offset % 4, 0, bsp.NAMES[i])


if __name__ == '__main__':
    unittest.main()
