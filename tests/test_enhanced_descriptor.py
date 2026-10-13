"""Inventory descriptor reader and story-item test, run natively offline."""
import unittest

from tests.native import ENGINE_SERVER, run_native


class DescriptorTests(unittest.TestCase):
    def test_descriptor_reader(self):
        out = run_native(self, 'cof_descriptor_test.c', {
            'sv_cof_descriptor.h': (ENGINE_SERVER / 'sv_cof_descriptor.h').read_text(),
        })
        self.assertIn('descriptor scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
