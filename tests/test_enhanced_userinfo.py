"""Compile the actual info-string setter and model hook against guarded buffers."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a'
VCVARS = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat')


class EnhancedUserinfoTests(unittest.TestCase):
    def test_native_capacity_and_atomic_model_replacement(self):
        if not VCVARS.exists() and not shutil.which('cc'):
            self.skipTest('native compiler required')
        source = (ENGINE / 'engine/common/infostring.c').read_text()
        # Retain all production info-string code; replace only the engine header.
        source = '\n'.join(line for line in source.splitlines()
                           if not line.lstrip().startswith('#include'))
        story = (ENGINE / 'engine/server/sv_cof_story.c').read_text()
        start = story.index('void SV_CoF_StoryUserinfo(')
        end = story.index('\nvoid SV_CoF_StoryFrame(', start)
        with tempfile.TemporaryDirectory(prefix='cof-userinfo-test-') as tmp:
            work = Path(tmp)
            (work / 'userinfo_under_test.c').write_text(source)
            (work / 'model_hook_under_test.c').write_text(story[start:end])
            shutil.copyfile(ROOT / 'tests/engine/cof_userinfo_harness.c', work / 'harness.c')
            if os.name == 'nt':
                (work / 'build.cmd').write_text(
                    f'@call "{VCVARS}" amd64_x86 >nul\n'
                    '@cl /nologo /W4 /WX /wd4100 /wd4244 /wd4505 /D_CRT_SECURE_NO_WARNINGS /TC harness.c /Fe:userinfo-test.exe\n'
                    '@if errorlevel 1 exit /b %errorlevel%\n'
                    '@.\\userinfo-test.exe\n')
                command = ['cmd.exe', '/d', '/c', str(work / 'build.cmd')]
            else:
                command = ['sh', '-c', 'cc -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter harness.c -o userinfo-test && ./userinfo-test']
            result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('userinfo capacity and atomic replacement passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
