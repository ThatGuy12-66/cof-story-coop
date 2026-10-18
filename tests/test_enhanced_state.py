"""Shared story state and durable document payloads, without a game process."""
import unittest

from tests.native import ENGINE_SERVER, ROOT, run_native, strip_includes


class StoryStateTests(unittest.TestCase):
    def test_shared_values_and_document_journal(self):
        out = run_native(self, 'cof_state_test.c', {
            'sv_cof_state_data.h': (ENGINE_SERVER / 'sv_cof_state_data.h').read_text(),
        })
        self.assertIn('shared state scenarios passed', out)

    def test_state_integration(self):
        out = run_native(self, 'cof_state_harness.c', {
            'retail_fixture.c': (ROOT / 'tests/engine/cof_retail_harness.c').read_text(),
            'sv_cof_descriptor.h': (ENGINE_SERVER / 'sv_cof_descriptor.h').read_text(),
            'sv_cof_retail.h': (ENGINE_SERVER / 'sv_cof_retail.h').read_text(),
            'sv_cof_state_data.h': (ENGINE_SERVER / 'sv_cof_state_data.h').read_text(),
            'sv_cof_light.h': (ENGINE_SERVER / 'sv_cof_light.h').read_text(),
            'retail_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_retail.c').read_text()),
            'state_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_state.c').read_text()),
        })
        self.assertIn('state integration scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
