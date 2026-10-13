"""The test gun: every person keeps a gun with infinite ammo while cof_test_gun names one."""
import unittest

from tests.native import ENGINE_SERVER, run_native, strip_includes


class TestGunTests(unittest.TestCase):
    def test_glue(self):
        out = run_native(self, 'cof_testgun_harness.c', {
            'testgun_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_testgun.c').read_text()),
        })
        self.assertIn('test gun scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
