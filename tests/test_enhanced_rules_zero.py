"""Exercise the production game-rules allocation patch against real x86 call-site bytes."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'engine'
VCVARS = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat')


def function_end(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return start, end


class RulesZeroTests(unittest.TestCase):
    def test_rules_allocation_is_zeroed_and_restorable(self):
        if os.name != 'nt' or not VCVARS.exists():
            self.skipTest('x86 Windows compiler required: the patch rewrites x86 machine code')
        story = (ENGINE / 'engine/server/sv_cof_story.c').read_text()
        start, end = function_end(story, 'static qboolean CoF_StoryWriteGate( byte *p, const byte *expect, const byte *value )')
        gate = story[start:end]
        block_start = story.index('#define COF_RULES_NEW_CALL')
        _, zero_end = function_end(story, 'static qboolean CoF_StoryZeroRules( byte *base )')
        block_end = story.index('#endif', zero_end) + len('#endif')
        with tempfile.TemporaryDirectory(prefix='cof-rules-zero-') as tmp:
            work = Path(tmp)
            (work / 'writegate_under_test.c').write_text(gate)
            (work / 'rules_under_test.c').write_text(story[block_start:block_end] + '\n')
            shutil.copyfile(ROOT / 'tests/engine/cof_rules_zero_harness.c', work / 'harness.c')
            (work / 'build.cmd').write_text(
                f'@call "{VCVARS}" amd64_x86 >nul\n'
                '@cl /nologo /W4 /WX /wd4054 /wd4055 /wd4152 /wd4505 /D_CRT_SECURE_NO_WARNINGS /TC harness.c /Fe:rules-zero-test.exe\n'
                '@if errorlevel 1 exit /b %errorlevel%\n'
                '@.\\rules-zero-test.exe\n')
            result = subprocess.run(['cmd.exe', '/d', '/c', str(work / 'build.cmd')], cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('story rules allocation zeroing passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
