"""Small, verified changes to the retail hl.dll in story co-op, each switched by a cvar (engine/server/sv_cof_gates.h,
docs/research/2026-10-18-cof-decompilation-survey.md): the bytes are written only over exactly the retail ones or our
own, and put back when story co-op ends."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output


class GateTests(unittest.TestCase):
    def test_gates(self):
        header = strip_includes((SERVER / 'sv_cof_gates.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_gates_harness.c', {'gates_under_test.h': '#include <string.h>\ntypedef unsigned char byte;\n' + header})
        self.assertEqual(program_output(out), [
            'friendly-fire rva C8722 size 2',
            'on: 1 bytes 90 90 writes 1',
            'on again: 1 writes 1',
            'off: 1 bytes 75 06 writes 2',
            'foreign: 0 bytes 12 34 writes 2',
        ])


class GateWiringTests(unittest.TestCase):
    def test_runs_with_the_story_layer_and_closes_with_it(self):
        assert_has(self, 'SV_CoF_GatesFrame();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame('))
        assert_has(self, 'SV_CoF_GatesInit();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit('))
        assert_has(self, 'SV_CoF_GatesClose();', body_of(SERVER / 'sv_cof_story.c', 'static void CoF_StoryCloseGate( void )'))

    def test_friendly_fire_is_off_by_default(self):
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_coop_friendly_fire, "0",', (SERVER / 'sv_cof_gates.c').read_text(encoding='latin-1'))


if __name__ == '__main__':
    unittest.main()
