"""Recovery archives, inventory schemas and lifecycle behavior."""
import unittest

from tests.native import ENGINE_SERVER, run_native, strip_includes


class RecoveryTests(unittest.TestCase):
    def test_inventory_snapshot_io(self):
        out = run_native(self, 'cof_snapshot_io_test.c', {
            'sv_cof_snapshot.h': (ENGINE_SERVER / 'sv_cof_snapshot.h').read_text(),
            'snapshot_io_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_snapshot_io.inc').read_text()),
        })
        self.assertIn('inventory IO scenarios passed', out)

    def test_archive_validation(self):
        out = run_native(self, 'cof_archive_test.c', {
            'sv_cof_snapshot.h': (ENGINE_SERVER / 'sv_cof_snapshot.h').read_text(),
            'sv_cof_archive.h': (ENGINE_SERVER / 'sv_cof_archive.h').read_text(),
        })
        self.assertIn('archive scenarios passed', out)

    def test_snapshot_validation(self):
        out = run_native(self, 'cof_snapshot_test.c', {
            'sv_cof_snapshot.h': (ENGINE_SERVER / 'sv_cof_snapshot.h').read_text(),
        })
        self.assertIn('snapshot validation scenarios passed', out)

    def test_death_rejoin_lifecycle(self):
        out = run_native(self, 'cof_recovery_harness.c', {
            'sv_cof_snapshot.h': (ENGINE_SERVER / 'sv_cof_snapshot.h').read_text(),
            'recovery_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_recovery.c').read_text()),
        })
        self.assertIn('recovery lifecycle scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
