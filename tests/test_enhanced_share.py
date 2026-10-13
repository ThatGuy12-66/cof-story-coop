"""The story item ledger's engine glue against offline doubles."""
import unittest

from tests.native import ENGINE_SERVER, run_native, strip_includes


class ShareTests(unittest.TestCase):
    def test_ledger_in_the_engine(self):
        out = run_native(self, 'cof_share_harness.c', {
            'sv_cof_descriptor.h': (ENGINE_SERVER / 'sv_cof_descriptor.h').read_text(),
            'sv_cof_share_state.h': (ENGINE_SERVER / 'sv_cof_share_state.h').read_text(),
            'share_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_share.c').read_text()),
        })
        self.assertIn('share scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
