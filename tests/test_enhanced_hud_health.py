"""The game's HealthCap message is a type, not a value: 0 none, 1 = health held at 80, 2 = at 66 (hl.dll
CCofMaxHealthChange, CBasePlayer's clamp; client.dll only swaps the bar's art and keeps it out of 100). The remade
health bar (engine/client/cof_hud_remake.c) draws health out of 100 and dims what the cap takes away
(docs/research/2026-10-18-cof-decompilation-survey.md 1.7)."""
import unittest

from tests.native import ROOT, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output

CLIENT = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/client'


class HealthCapTests(unittest.TestCase):
    def test_the_type_byte_is_the_maximum(self):
        header = strip_includes((CLIENT / 'cof_hud_health.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_hud_health_harness.c', {'hud_health_under_test.h': header})
        self.assertEqual(program_output(out), [
            'maximum 0: 100', 'maximum 1: 80', 'maximum 2: 66', 'maximum 3: 100', 'maximum -1: 100',
            'fill 50 cap 0: 0.50', 'fill 120 cap 0: 1.00', 'fill 80 cap 1: 0.80', 'fill 90 cap 2: 0.66', 'fill -5 cap 0: 0.00',
        ])

    def test_the_message_keeps_the_type_including_zero(self):
        body = body_of(CLIENT / 'cof_hud_remake.c', 'void CL_CoF_HudRemakeMessage( const char *name, int size, const byte *data )')
        assert_has(self, 'else if( !Q_stricmp(name,"HealthCap") && size==1 ) hud.health_cap=data[0];', body)

    def test_the_bar_uses_the_maximum(self):
        source = (CLIENT / 'cof_hud_remake.c').read_text(encoding='latin-1')
        assert_has(self, 'float fraction=CoF_HudHealthFill(cl.local.health,hud.health_cap);', source)
        assert_has(self, 'float maximum=CoF_HudHealthMaximum(hud.health_cap)/100.0f;', source)
        self.assertFalse('cl.local.health/Q_max(1,hud.health_cap)' in source)


if __name__ == '__main__':
    unittest.main()
