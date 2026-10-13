"""The exit hold's rules, run natively offline."""
import unittest

from tests.native import ENGINE_SERVER, run_native


class ExitStateTests(unittest.TestCase):
    def test_exit_rules(self):
        out = run_native(self, 'cof_exit_state_test.c', {
            'sv_cof_exit_state.h': (ENGINE_SERVER / 'sv_cof_exit_state.h').read_text(),
        })
        self.assertIn('exit state scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
