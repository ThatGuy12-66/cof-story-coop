"""Checkpoint validation, identity matching and failure-safe file staging."""
import unittest

from tests.native import ENGINE_SERVER, ROOT, run_native, strip_includes


def session_headers():
    return {name: (ENGINE_SERVER / name).read_text() for name in (
        'sv_cof_session_data.h', 'sv_cof_archive.h', 'sv_cof_snapshot.h',
        'sv_cof_state_data.h', 'sv_cof_share_state.h', 'sv_cof_descriptor.h',
    )}


class SessionTests(unittest.TestCase):
    def test_checkpoint_lifecycle_and_commands(self):
        source = strip_includes((ENGINE_SERVER / 'sv_cof_session.inc').read_text())
        start = source.index('void SV_CoF_SessionFrame')
        end = source.index('void SV_CoF_SessionInit', start)
        output = run_native(self, 'cof_session_lifecycle_test.c', {
            'session_lifecycle_under_test.c': source[start:end],
        })
        self.assertIn('session lifecycle scenarios passed', output)

    def test_format_and_identity(self):
        files = session_headers()
        paths = sorted((ROOT / 'runtimes/enhanced/SAVE').glob('*.HL[123]'))
        files['session_fixture_paths.h'] = (
            'static const char *fixture_paths[] = {' +
            ','.join('"' + path.as_posix() + '"' for path in paths) +
            ',NULL};' if paths else 'static const char *fixture_paths[] = {NULL};')
        output = run_native(self, 'cof_session_test.c', files)
        self.assertIn('session validation scenarios passed', output)

    def test_staging_rolls_back_io_failures(self):
        files = session_headers()
        source = strip_includes((ENGINE_SERVER / 'sv_cof_session.inc').read_text())
        start = source.index('static byte *CoF_SessionReadFile')
        end = source.index('static byte *CoF_SessionGlobalCapture')
        stage = source.index('static qboolean CoF_SessionStage')
        stage_end = source.index('static qboolean CoF_SessionCurrentNames', stage)
        files['session_io_under_test.c'] = source[start:end] + source[stage:stage_end]
        output = run_native(self, 'cof_session_io_test.c', files)
        self.assertIn('session staging scenarios passed', output)


if __name__ == '__main__':
    unittest.main()
