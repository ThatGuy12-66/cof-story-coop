"""The engine's player ID on Windows (1.2.8). Upstream Xash ran `wmic` twice on every launch, as hidden child
processes reading the disk and BIOS serial numbers; Windows Defender's behaviour monitoring took that for an
infostealer (Behavior:Win32/SuspLummalnj.A on WMIC.exe launched by CoFLaunchApp.exe), killed the game and
quarantined it. A tester's friend, 2026-10-22. The two sources are now read in the game's own process."""
import os
import re
import unittest

from tests.native import ROOT, assert_has, run_native
from tests.test_enhanced_sections import body_of

ENGINE = ROOT / 'engine/engine'
ID = ENGINE / 'client/identification.c'


def code_only(text):
    """The source without its comments: they may name what the code no longer does."""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


class IdentificationTests(unittest.TestCase):
    def test_no_child_process_is_started(self):
        code = code_only(ID.read_text(encoding='latin-1'))
        for needle in ('CreateProcess', 'wmic', 'WMIC', 'ShellExecute', 'popen', 'system(', 'CreatePipe'):
            self.assertNotIn(needle, code)

    def test_the_id_is_made_and_checked_from_the_same_sources(self):
        generate = body_of(ID, 'static bloomfilter_t ID_GenerateRawId( void )')
        check = body_of(ID, 'static uint ID_CheckRawId( bloomfilter_t filter )')
        assert_has(self, 'count += ID_ProcessWindows( &value );', generate)
        assert_has(self, 'count += ID_CheckWindows( filter );', check)
        for name in ('static int ID_ProcessWindows( bloomfilter_t *value )', 'static int ID_CheckWindows( bloomfilter_t value )'):
            body = body_of(ID, name)
            assert_has(self, 'for( int i = 0; i < ID_WINDOWS_SOURCES; i++ )', body)
            assert_has(self, 'if( !ID_WindowsSource( i, buf, sizeof( buf )))', body)

    def test_the_sources(self):
        body = body_of(ID, 'static qboolean ID_WindowsSource( int index, char *buf, DWORD size )')
        # the 64-bit view: a 32-bit game is redirected to WOW6432Node, which has no MachineGuid
        assert_has(self, 'KEY_QUERY_VALUE | KEY_WOW64_64KEY', body)
        assert_has(self, '"MachineGuid"', body)
        assert_has(self, 'GetVolumeInformationA( root, NULL, 0, &serial, NULL, NULL, NULL, 0 )', body)
        assert_has(self, 'return ID_VerifyHEX( buf );', body)

    def test_the_sources_read_in_process_on_windows(self):
        if os.name != 'nt':
            self.skipTest('reads the Windows registry and the system volume')
        text = ID.read_text(encoding='latin-1')
        code = '\n'.join([
            re.search(r'#define ID_WINDOWS_SOURCES \d+', text)[0],
            body_of(ID, 'static qboolean ID_VerifyHEX( const char *hex )') + '\n}\n',
            body_of(ID, 'static qboolean ID_WindowsSource( int index, char *buf, DWORD size )') + '\n}\n',
        ])
        out = run_native(self, 'cof_id_windows_harness.c', {'id_under_test.c': code})
        self.assertIn('windows id sources passed', out)


if __name__ == '__main__':
    unittest.main()
