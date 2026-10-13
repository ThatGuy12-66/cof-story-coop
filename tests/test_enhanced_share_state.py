"""The story item ledger's rules, run natively offline."""
import unittest

from tests.native import ENGINE_SERVER, run_native


class ShareStateTests(unittest.TestCase):
    def test_ledger_rules(self):
        out = run_native(self, 'cof_share_state_test.c', {
            'sv_cof_descriptor.h': (ENGINE_SERVER / 'sv_cof_descriptor.h').read_text(),
            'sv_cof_share_state.h': (ENGINE_SERVER / 'sv_cof_share_state.h').read_text(),
        })
        self.assertIn('share state scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
