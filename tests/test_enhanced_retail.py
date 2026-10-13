"""The retail layer against offline engine doubles: no DLL, game or network."""
import unittest

from tests.native import ENGINE_SERVER, run_native, strip_includes


class RetailTests(unittest.TestCase):
    def test_retail_layer(self):
        out = run_native(self, 'cof_retail_harness.c', {
            'sv_cof_descriptor.h': (ENGINE_SERVER / 'sv_cof_descriptor.h').read_text(),
            'sv_cof_retail.h': (ENGINE_SERVER / 'sv_cof_retail.h').read_text(),
            'sv_cof_state_data.h': (ENGINE_SERVER / 'sv_cof_state_data.h').read_text(),
            'retail_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_retail.c').read_text()),
        })
        self.assertIn('retail scenarios passed', out)

    def test_shared_state_storage(self):
        out = run_native(self, 'cof_retail_state_test.c', {
            'retail_fixture.c': (ENGINE_SERVER.parents[4] / 'tests/engine/cof_retail_harness.c').read_text(),
            'sv_cof_descriptor.h': (ENGINE_SERVER / 'sv_cof_descriptor.h').read_text(),
            'sv_cof_retail.h': (ENGINE_SERVER / 'sv_cof_retail.h').read_text(),
            'sv_cof_state_data.h': (ENGINE_SERVER / 'sv_cof_state_data.h').read_text(),
            'retail_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_retail.c').read_text()),
        })
        self.assertIn('retail state scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
