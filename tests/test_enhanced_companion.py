"""The AI companion: its decisions, and the engine glue that drives a player with them."""
import unittest

from tests.native import ENGINE_SERVER, run_native, strip_includes


class CompanionTests(unittest.TestCase):
    def test_brain(self):
        out = run_native(self, 'cof_brain_test.c', {
            'sv_cof_brain.h': (ENGINE_SERVER / 'sv_cof_brain.h').read_text(),
        })
        self.assertIn('brain scenarios passed', out)

    def test_glue(self):
        out = run_native(self, 'cof_companion_harness.c', {
            'sv_cof_brain.h': (ENGINE_SERVER / 'sv_cof_brain.h').read_text(),
            'companion_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_companion.c').read_text()),
        })
        self.assertIn('companion glue scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
