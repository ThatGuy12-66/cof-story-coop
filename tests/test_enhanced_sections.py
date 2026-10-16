"""The engine's side of merged maps (engine/common/cof_sections.h): the .sections file, and the merged map's
entities put together from each member's own entities, byte for byte as the Python reference does."""
import unittest

from tests.native import ROOT, run_native
from tests.test_map_merge_bsp import MAPS, retail
from tools.map_merge import bsp, groups, merge, rules

ENGINE_COMMON = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/common'


def fnv64(data: bytes) -> int:
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def program_output(out):
    """The harness's own lines (the compiler names the file first)."""
    return [line for line in out.splitlines() if line.strip() != 'harness.c']


def header():
    return (ENGINE_COMMON / 'cof_sections.h').read_text(encoding='latin-1')


def wave1_group(name):
    retail('c_apartment3')
    g = next(x for x in groups.load_groups() if x.name == name)
    named = [(m, bsp.Bsp.read(MAPS / f'{m}.bsp')) for m in g.members]
    return g, named, groups.plan_sections(named)


class SectionsFileTests(unittest.TestCase):
    def test_parse_and_lookups_match_the_python_side(self):
        g, named, sections = wave1_group('cm_apartment3')
        text = rules.to_text(g.name, sections)
        out = run_native(self, 'cof_sections_parse_harness.c', {'sections_under_test.h': header(), 'sections.txt': text})
        expected = [f'name {g.name} count {len(sections)}']
        for i, s in enumerate(sections):
            expected.append(f'section {i} {s.map} dz {s.dz:g} first {s.first_model} models {s.models} '
                            f'zmin {s.bounds[0]:g} zmax {s.bounds[1]:g} renames {len(s.renames)} styles {len(s.styles)} '
                            f'landmarks {len(s.landmarks)}')
        expected += [f'find c_apartment4 1', f'find C_APARTMENT5 2', f'find c_apartment1 -1',
                     f'rename 1 land1 {sections[1].renames["land1"]}', 'rename 1 nothere nothere', 'rename 0 land1 land1',
                     f'style 1 32 {sections[1].styles[32]}', 'style 1 5 5',
                     'at 0 0', f'at {sections[1].bounds[0] + 10:g} 1', f'at {sections[2].bounds[1] - 10:g} 2',
                     'landmark 1 land1_m1 1', 'landmark 1 land1 0']
        self.assertEqual(program_output(out), expected)

    def test_malformed_files_are_refused(self):
        cases =['', 'cof-sections 2 cm_x\n', 'cof-sections 1 cm_x\nrename a b\n',
                 'cof-sections 1 cm_x\nsection c_a 0 1\n', 'cof-sections 1 cm_x\nsection c_a zero 1 2\n',
                 'cof-sections 1 cm_x\nsection c_a 0 1 2\nfoo bar\n',
                 'cof-sections 1 cm_x\n' + ''.join(f'section c_{k} 0 1 2\n' for k in range(5)),
                 'cof-sections 1 cm_x\nsection ' + 'c' * 64 + ' 0 1 2\n']
        out = run_native(self, 'cof_sections_refuse_harness.c', {
            'sections_under_test.h': header(),
            'cases.h': 'static const char *cases[] = {\n' + ''.join(
                '"' + c.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '",\n' for c in cases) + '};\n'})
        self.assertEqual(program_output(out), ['refused'] * len(cases))


class ComposeTests(unittest.TestCase):
    def check_group(self, name):
        g, named, sections = wave1_group(name)
        world = merge.merged_worldspawn([x.ents[0] for _, x in named])
        expected = rules.compose_reference(world, [x.ents for _, x in named], sections)[:-1]
        files = {'sections_under_test.h': header(), 'sections.txt': rules.to_text(g.name, sections),
                 'world.ent': bsp.serialize_ents([world])[:-1].decode('latin-1')}
        for i, (m, _) in enumerate(named):
            raw = (MAPS / f'{m}.bsp').read_bytes()
            offset, length = int.from_bytes(raw[4:8], 'little'), int.from_bytes(raw[8:12], 'little')
            files[f'member{i}.ent'] = raw[offset:offset + length].split(b'\0')[0].decode('latin-1')
        files['members.h'] = f'#define MEMBERS {len(named)}\n'
        out = run_native(self, 'cof_sections_compose_harness.c', files)
        self.assertEqual(program_output(out), [f'{len(expected)} {fnv64(expected):016x}'])

    def test_apartment3_4_5_compose_like_the_reference(self):
        self.check_group('cm_apartment3')

    def test_apartment1_2_compose_like_the_reference(self):
        self.check_group('cm_apartment1')


if __name__ == '__main__':
    unittest.main()
