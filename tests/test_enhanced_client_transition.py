"""Exercise the production map-change handler and first movevars parse together."""
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


class ClientTransitionTests(unittest.TestCase):
    def test_movevars_after_level_change_and_duplicate_notification(self):
        if not VCVARS.exists() and not shutil.which('cc'):
            self.skipTest('native compiler required')
        main = (ENGINE / 'engine/client/cl_main.c').read_text()
        parse = (ENGINE / 'engine/client/parse/cl_parse.c').read_text()
        with tempfile.TemporaryDirectory(prefix='cof-client-transition-') as tmp:
            work = Path(tmp)
            (work / 'transition_under_test.c').write_text(function(main, 'static void CL_CoF_StoryChangeLevel_f( void )'))
            (work / 'movevars_under_test.c').write_text(function(parse, 'void CL_ParseMovevars( sizebuf_t *msg )'))
            shutil.copyfile(ROOT / 'tests/engine/cof_client_transition_harness.c', work / 'harness.c')
            if os.name == 'nt':
                (work / 'build.cmd').write_text(
                    f'@call "{VCVARS}" amd64_x86 >nul\n'
                    '@cl /nologo /W4 /WX /wd4505 /D_CRT_SECURE_NO_WARNINGS /TC harness.c /Fe:transition-test.exe\n'
                    '@if errorlevel 1 exit /b %errorlevel%\n'
                    '@.\\transition-test.exe\n')
                command = ['cmd.exe', '/d', '/c', str(work / 'build.cmd')]
            else:
                command = ['sh', '-c', 'cc -std=c99 -Wall -Wextra -Werror -Wno-unused-function harness.c -o transition-test && ./transition-test']
            result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('story client transition entity lifetime passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
