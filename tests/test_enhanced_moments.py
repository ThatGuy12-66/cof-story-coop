"""Story moments: fall floors, phone calls, ladders, wheelchair, drops and yes/no prompts."""
import unittest

from tests.native import ENGINE_SERVER, run_native, strip_includes


class StoryMomentsTests(unittest.TestCase):
    def test_moments_glue(self):
        out = run_native(self, 'cof_moments_harness.c', {
            'moments_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_moments.c').read_text()),
        })
        self.assertIn('moments scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
