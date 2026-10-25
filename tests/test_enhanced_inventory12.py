"""12-slot story co-op inventory: the hl.dll sites, the server rules, the client store and accessor."""
import hashlib
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.native import ENGINE_SERVER, ROOT, VCVARS, run_native, run_native_cpp, strip_includes

ENGINE = ENGINE_SERVER.parent
FREEVGUI = ENGINE.parent / '3rdparty/freevgui/platform/xash3d-fwgs'
ENGINE_CLIENT = ENGINE / 'client'
HL_DLL = ROOT / 'runtimes/enhanced/cryoffear/cl_dlls/hl.dll'
CLIENT_DLL = ROOT / 'runtimes/enhanced/cryoffear/cl_dlls/client.dll'
HL_SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
CLIENT_SHA = 'd2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00'


def retail(path, sha):
    return path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == sha


def run_x86(case, harness, files, args=()):
    """MSVC x86 build of a harness that maps a real retail DLL (Windows only)."""
    if os.name != 'nt' or not VCVARS.exists():
        case.skipTest('x86 Windows compiler required: the sites are x86 code in a retail DLL')
    with tempfile.TemporaryDirectory(prefix='cof-inv12-') as tmp:
        work = Path(tmp)
        for name, text in files.items():
            (work / name).write_text(text)
        shutil.copyfile(ROOT / 'tests/engine' / harness, work / 'harness.c')
        quoted = ' '.join(f'"{a}"' for a in args)
        (work / 'build.cmd').write_text(
            f'@call "{VCVARS}" amd64_x86 >nul\n'
            '@cl /nologo /W4 /WX /wd4054 /wd4055 /wd4152 /wd4505 /D_CRT_SECURE_NO_WARNINGS /TC harness.c /Fe:inv12-test.exe\n'
            '@if errorlevel 1 exit /b %errorlevel%\n'
            f'@.\\inv12-test.exe {quoted}\n')
        result = subprocess.run(['cmd.exe', '/d', '/c', str(work / 'build.cmd')], cwd=work,
                                capture_output=True, text=True, timeout=120)
        case.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout


class HlSiteTests(unittest.TestCase):
    def test_sites_against_retail_hl(self):
        if not retail(HL_DLL, HL_SHA):
            self.skipTest('the retail Cry of Fear 1.6 hl.dll is not installed locally')
        out = run_x86(self, 'cof_inv12_patch_harness.c',
                      {'patch_under_test.h': (ENGINE_SERVER / 'sv_cof_inventory12_patch.h').read_text()}, [HL_DLL])
        self.assertIn('inventory12 hl.dll sites passed', out)


class ServerRuleTests(unittest.TestCase):
    def test_rules(self):
        header = (ENGINE_SERVER / 'sv_cof_inventory12.h').read_text()
        self.assertIn('#define COF_INV12_USERINFO "_cofinv"', header)  # the harness repeats it
        out = run_native(self, 'cof_inv12_server_harness.c', {
            'patch_under_test.h': (ENGINE_SERVER / 'sv_cof_inventory12_patch.h').read_text(),
            'inventory12_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_inventory12.c').read_text()),
        })
        self.assertIn('inventory12 server rules passed', out)


class ClientStoreTests(unittest.TestCase):
    def test_store(self):
        out = run_native(self, 'cof_inv12_client_harness.c',
                         {'state_under_test.h': (ENGINE_CLIENT / 'cof_inventory12_state.h').read_text()})
        self.assertIn('inventory12 client store passed', out)

    def test_accessor_against_retail_client(self):
        if not retail(CLIENT_DLL, CLIENT_SHA):
            self.skipTest('the retail Cry of Fear 1.6 client.dll is not installed locally')
        out = run_x86(self, 'cof_inv12_accessor_harness.c', {
            'state_under_test.h': (ENGINE_CLIENT / 'cof_inventory12_state.h').read_text(),
            'patch_under_test.h': (ENGINE_CLIENT / 'cof_inventory12_patch.h').read_text(),
        }, [CLIENT_DLL])
        self.assertIn('inventory12 client.dll accessor passed', out)


class GridLogicTests(unittest.TestCase):
    def test_layout_and_clicks(self):
        out = run_native_cpp(self, 'cof_inventory_logic_test.cpp',
                             {'logic_under_test.h': (FREEVGUI / 'cofinventory_logic.h').read_text()})
        self.assertIn('inventory grid logic passed', out)

    def test_menu_acts_on_release(self):
        """The item menu lies over the client's NOTES / QUICK SELECT buttons, which act on
        release: acting on the press hid the menu and the release opened NOTES (2026-10-25)."""
        text = (FREEVGUI / 'cofinventory.cpp').read_text()
        start = text.index('class MenuInput')
        menu = text[start:text.index('};', start)]
        pressed = menu[menu.index('void mousePressed'):menu.index('void mouseReleased')]
        released = menu[menu.index('void mouseReleased'):menu.index('void mouseDoublePressed')]
        self.assertIn('cofinv::PressMenu(', pressed)
        self.assertNotIn('Apply(', pressed)
        self.assertNotIn('ClickMenu(', menu)
        self.assertIn('Apply( cofinv::ReleaseMenu(', released)


if __name__ == '__main__':
    unittest.main()
