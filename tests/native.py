"""Compile and run one native C harness offline: MSVC x86 on Windows, cc elsewhere.

Production sources are passed in as text, usually with their #include lines
stripped, so a harness supplies its own engine doubles. Nothing here needs the
game, a game DLL or the network.
"""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENGINE_SERVER = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/server'
VCVARS = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat')


def assert_has(case: unittest.TestCase, needle: str, text: str) -> None:
    """A wiring check that names only the missing line (not the whole source file) when it fails."""
    case.assertTrue(needle in text, f'missing: {needle}')


def strip_includes(text: str) -> str:
    return '\n'.join(line for line in text.splitlines() if not line.lstrip().startswith('#include'))


def run_native(case: unittest.TestCase, harness: str, files: dict, timeout: int = 120) -> str:
    if not VCVARS.exists() and not shutil.which('cc'):
        case.skipTest('a C compiler is required for the offline engine harness')
    with tempfile.TemporaryDirectory(prefix='cof-native-') as tmp:
        work = Path(tmp)
        for name, text in files.items():
            (work / name).write_text(text)
        shutil.copyfile(ROOT / 'tests/engine' / harness, work / 'harness.c')
        if os.name == 'nt':
            (work / 'build.cmd').write_text(
                f'@call "{VCVARS}" amd64_x86 >nul\n'
                '@cl /nologo /W4 /WX /wd4100 /wd4505 /D_CRT_SECURE_NO_WARNINGS /TC harness.c /Fe:native-test.exe\n'
                '@if errorlevel 1 exit /b %errorlevel%\n'
                '@.\\native-test.exe\n')
            command = ['cmd.exe', '/d', '/c', str(work / 'build.cmd')]
        else:
            command = ['sh', '-c', 'cc -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function '
                       'harness.c -o native-test && ./native-test']
        result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=timeout)
        case.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout


def run_native_cpp(case: unittest.TestCase, harness: str, files: dict, timeout: int = 120) -> str:
    """The same for a C++ harness (MSVC /TP, C++14, no exceptions: as FreeVGUI builds)."""
    if not VCVARS.exists() and not shutil.which('c++'):
        case.skipTest('a C++ compiler is required for the offline UI harness')
    with tempfile.TemporaryDirectory(prefix='cof-native-cpp-') as tmp:
        work = Path(tmp)
        for name, text in files.items():
            (work / name).write_text(text)
        shutil.copyfile(ROOT / 'tests/engine' / harness, work / 'harness.cpp')
        if os.name == 'nt':
            (work / 'build.cmd').write_text(
                f'@call "{VCVARS}" amd64_x86 >nul\n'
                '@cl /nologo /W4 /WX /wd4100 /D_CRT_SECURE_NO_WARNINGS /TP harness.cpp /Fe:native-test.exe\n'
                '@if errorlevel 1 exit /b %errorlevel%\n'
                '@.\\native-test.exe\n')
            command = ['cmd.exe', '/d', '/c', str(work / 'build.cmd')]
        else:
            command = ['sh', '-c', 'c++ -std=c++14 -Wall -Wextra -Werror -Wno-unused-parameter harness.cpp -o native-test && ./native-test']
        result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=timeout)
        case.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout
