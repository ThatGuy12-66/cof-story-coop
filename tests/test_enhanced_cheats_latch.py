"""The restored cheats' latches (engine/server/sv_cof_cheats.c) belong to the slot's person: with sv_cheats 1 the next
occupant of a slot inherited noclip/notarget/fly/infammo/infstamina, re-applied every frame (1.1.8, E2-9)."""
import re
import unittest

from tests.native import ENGINE_SERVER as SERVER, run_native
from tests.test_enhanced_sections import body_of


class CheatLatchTests(unittest.TestCase):
    def test_a_new_occupant_starts_with_no_latch(self):
        source = (SERVER / 'sv_cof_cheats.c').read_text(encoding='latin-1')
        latch_type = re.search(r'typedef struct cof_latch_s\n\{.*?\} cof_latch_t;', source, re.S).group(0)
        latch = body_of(SERVER / 'sv_cof_cheats.c', 'static cof_latch_t *CoF_Latch( const sv_client_t *cl )') + '\n}\n'
        under_test = latch_type + '\nstatic cof_latch_t cof_latches[MAX_CLIENTS];\n' + latch
        out = run_native(self, 'cof_cheats_latch_harness.c', {'latch_under_test.c': under_test})
        self.assertIn('cheat latch scenarios passed', out)

    def test_every_latch_goes_through_the_owner_check(self):
        source = (SERVER / 'sv_cof_cheats.c').read_text(encoding='latin-1')
        self.assertEqual(source.count('cof_latches['), source.count('cof_latches[slot]') + 1)  # + the definition


if __name__ == '__main__':
    unittest.main()
