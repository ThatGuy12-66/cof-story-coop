"""Run the actual party adapter against a deterministic, offline engine harness.

Only the engine includes are replaced by the harness ABI. The normal engine
build separately checks the real ABI; this test needs no DLL, game or network.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tools.verify_story_fixture import parse_status

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'engine'
VCVARS = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat')


class EnhancedPartyTests(unittest.TestCase):
    def test_offline_party_state_machine(self):
        source = ENGINE / 'engine/server/sv_cof_party.c'
        self.assertTrue(source.exists(), 'party module must exist')
        if not VCVARS.exists() and not shutil.which('cc'):
            self.skipTest('a C compiler is required for the offline engine harness')
        with tempfile.TemporaryDirectory(prefix='cof-party-test-') as tmp:
            work = Path(tmp)
            # Keep all executable production code, replacing only external headers.
            code = '\n'.join(line for line in source.read_text().splitlines()
                             if not line.lstrip().startswith('#include'))
            (work / 'party_under_test.c').write_text(code)
            story = (ENGINE / 'engine/server/sv_cof_story.c').read_text()
            start = story.index('static void CoF_StoryStatus_f(')
            end = story.index('\nvoid SV_CoF_StoryInit(', start)
            (work / 'status_under_test.c').write_text(story[start:end])
            shutil.copyfile(ROOT / 'tests/engine/cof_party_harness.c', work / 'harness.c')
            shutil.copyfile(ENGINE / 'engine/server/sv_cof_doorroom.h', work / 'sv_cof_doorroom.h')
            if os.name == 'nt':
                (work / 'build.cmd').write_text(
                    f'@call "{VCVARS}" amd64_x86 >nul\n'
                    '@cl /nologo /W4 /WX /wd4100 /TC harness.c /Fe:party-test.exe\n'
                    '@if errorlevel 1 exit /b %errorlevel%\n'
                    '@.\\party-test.exe\n')
                command = ['cmd.exe', '/d', '/c', str(work / 'build.cmd')]
            else:
                command = ['sh', '-c', 'cc -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function harness.c -o party-test -lm && ./party-test']
            result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('party scenarios passed', result.stdout)
            parsed = parse_status(result.stdout, 'initial')
            self.assertEqual(parsed['players'][0]['inventory'], {
                'inventoryitems/weapons/weapon_mobile.txt': 1,
                'inventoryitems/weapons/weapon_switchblade.txt': 1,
                'inventoryitems/weapons/weapon_glock.txt': 1,
            })
            self.assertEqual(parsed['players'][1]['inventory'], {})


if __name__ == '__main__':
    unittest.main()
