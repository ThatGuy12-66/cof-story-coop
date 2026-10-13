"""Exercise the production story changelevel, deactivation and packet read together."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a'
VCVARS = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat')


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class ServerTransitionTests(unittest.TestCase):
    def test_kept_connection_signon_reaches_server(self):
        if not VCVARS.exists() and not shutil.which('cc'):
            self.skipTest('native compiler required')
        server = ENGINE / 'engine/server'
        extracted = {
            'changelevel_under_test.c': (server / 'sv_save.c', 'void SV_ChangeLevel( qboolean loadfromsavedgame, const char *mapname, const char *start, qboolean background )'),
            'deactivate_under_test.c': (server / 'sv_init.c', 'void SV_DeactivateServer( void )'),
            'readpackets_under_test.c': (server / 'sv_main.c', 'static void SV_ReadPackets( void )'),
        }
        with tempfile.TemporaryDirectory(prefix='cof-server-transition-') as tmp:
            work = Path(tmp)
            for name, (path, signature) in extracted.items():
                (work / name).write_text(function(path.read_text(), signature))
            shutil.copyfile(ROOT / 'tests/engine/cof_server_transition_harness.c', work / 'harness.c')
            if os.name == 'nt':
                (work / 'build.cmd').write_text(
                    f'@call "{VCVARS}" amd64_x86 >nul\n'
                    '@cl /nologo /W4 /WX /wd4100 /wd4244 /wd4505 /D_CRT_SECURE_NO_WARNINGS /TC harness.c /Fe:server-transition-test.exe\n'
                    '@if errorlevel 1 exit /b %errorlevel%\n'
                    '@.\\server-transition-test.exe\n')
                command = ['cmd.exe', '/d', '/c', str(work / 'build.cmd')]
            else:
                command = ['sh', '-c', 'cc -std=c99 -Wall -Wextra -Werror -Wno-unused-function harness.c -o server-transition-test && ./server-transition-test']
            result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('story server transition keeps client connections passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
