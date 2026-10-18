"""A merged map's view distance per section (engine/common/cof_sections.h CoF_SectMaxRange, engine/server/sv_cof_view.c):
hl.dll sets sv_zmax from the worldspawn's MaxRange (or 4096; world.cpp:1004-1008 and 923-926 in the decompilation),
and the merged worldspawn takes the largest of its members', so the short ranges that make the forests dark were lost.
zmax reaches each client in a movevars delta of its own (docs/research/2026-10-18-cof-decompilation-survey.md
sections 3 and 5)."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native
from tests.test_enhanced_sections import body_of, header, program_output


class RangeTests(unittest.TestCase):
    def test_a_members_own_view_distance(self):
        out = run_native(self, 'cof_sections_range_harness.c', {'sections_under_test.h': header()})
        self.assertEqual(program_output(out), [
            'MaxRange: 1500',
            'none: 4096',
            'zero: 4096',
            'not a number: 4096',
            'only in a later entity: 4096',
            'no entities: 4096',
        ])


class ViewWiringTests(unittest.TestCase):
    def test_on_by_default_with_the_story_layer(self):
        module = (SERVER / 'sv_cof_view.c').read_text(encoding='latin-1')
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_coop_section_view, "1",', module)
        assert_has(self, 'SV_CoF_ViewInit();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit('))
        assert_has(self, 'SV_CoF_ViewFrame();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame('))

    def test_each_client_gets_its_sections_zmax_from_what_it_has(self):
        frame = body_of(SERVER / 'sv_cof_view.c', 'void SV_CoF_ViewFrame(')
        assert_has(self, 'from.zmax = cof_view_sent[i] > 0.0f ? cof_view_sent[i] : svgame.movevars.zmax;', frame)
        assert_has(self, 'MSG_WriteDeltaMovevars( &cl->netchan.message, &from, &to )', frame)
        assert_has(self, 'SV_CoF_SectionMaxRange( section )', frame)


if __name__ == '__main__':
    unittest.main()
