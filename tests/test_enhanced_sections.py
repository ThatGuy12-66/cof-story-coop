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


ROUTE_FILES = {
    # c_apartment3 + 4 + 5 as the real group names them (land1-3 clash: renamed in c_apartment4)
    'cur.txt': 'cof-sections 1 cm_apartment3\n'
               'section c_apartment3 0 1 207\nbounds -401 698\nlandmark land1\nlandmark ap5la\n'
               'section c_apartment4 1536 208 195\nbounds 1251 2234\nrename land1 land1_m1\nlandmark land1_m1\n'
               'section c_apartment5 -1536 403 30\nbounds -2000 -900\nrename ap5la ap5la_m2\nlandmark ap5la_m2\nlandmark land5\n',
    'other.txt': 'cof-sections 1 cm_apartment1\n'
                 'section c_apartment1 0 1 300\nbounds -100 900\nlandmark land5\n'
                 'section c_apartment2 1536 301 50\nbounds 1400 2500\nrename land1 land1_m1\nlandmark land1_m1\n',
}


class RouteTests(unittest.TestCase):
    """Where a change-level request goes: a section move in this file, another merged file, or a plain map."""

    def test_routes(self):
        out = run_native(self, 'cof_sections_route_harness.c', dict(ROUTE_FILES, **{'sections_under_test.h': header()}))
        self.assertEqual(program_output(out), [
            # from c_apartment3 to c_apartment4 via land1: a move to section 1, landmark land1 there is land1_m1
            'c_apartment4 land1 -> move 1 cm_apartment3 dest land1_m1 source land1',
            # back: the live landmark in section 1 is land1_m1; c_apartment3 keeps land1
            'c_apartment3 land1_m1 -> move 0 cm_apartment3 dest land1 source land1_m1',
            # c_apartment3 to c_apartment5 via ap5la, and back
            'c_apartment5 ap5la -> move 2 cm_apartment3 dest ap5la_m2 source ap5la',
            'c_apartment3 ap5la_m2 -> move 0 cm_apartment3 dest ap5la source ap5la_m2',
            # c_apartment5 to c_apartment1, a member of the other merged file
            'c_apartment1 land5 -> load 0 cm_apartment1 dest land5 source land5',
            # merging off (no other file known): the original map
            'c_apartment1 land5 off -> load -1 c_apartment1 dest land5 source land5',
            # a map in no merged file
            'c_city land9 -> load -1 c_city dest land9 source land9',
            # a door whose landmark is in no section (c_apartment2's land4): still a move, no landmark
            'c_apartment4 land4 -> move 1 cm_apartment3 dest land4 source land4',
            # unmerged current map, destination merged: section by name, landmark renamed for it
            'unmerged c_apartment2 land1 -> load 1 cm_apartment1 dest land1_m1 source land1',
        ])


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
