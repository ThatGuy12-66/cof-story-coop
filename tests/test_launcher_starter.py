"""1.3.2: the game's starter, CoFLaunchApp.exe, is an ordinary windowed program. The console build of 2026-09-27, half of
whose bytes were a PNG-compressed icon, was taken for a trojan by 16 of 71 scanners on VirusTotal (Microsoft
Trojan:Win32/Wacatac.B!ml, Bitdefender Gen:Trojan.Heur3.LPT...), and everyone took the release for malware
(user report 2026-10-26)."""
import math
import struct
import unittest
from collections import Counter
from pathlib import Path

from tools import package_enhanced_story as package

LAUNCHER = package.REPO / "launcher"
ICON = package.REPO / "assets/branding/coffix-bitmaps.ico"
SCRIPT = package.REPO / "scripts/build-cof-launcher.ps1"


def entropy(data: bytes) -> float:
    counts, n = Counter(data), len(data)
    return -sum(c / n * math.log2(c / n) for c in counts.values()) if n else 0.0


def pe_layout(data: bytes):
    """The subsystem and each section's (name, raw bytes) of a PE file."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[pe:pe + 4] == b"PE\0\0"
    sections, optional = struct.unpack_from("<HH", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
    subsystem = struct.unpack_from("<H", data, pe + 24 + 68)[0]
    table, out = pe + 24 + optional, []
    for i in range(sections):
        name, _, _, size, offset = struct.unpack_from("<8sIIII", data, table + 40 * i)
        out.append((name.rstrip(b"\0").decode(), data[offset:offset + size]))
    return subsystem, out


@unittest.skipUnless((LAUNCHER / "cof_launch.cpp").is_file(), "the CoF Enhanced checkout (upstream/cof-enhanced) is not here")
class StarterSourceTests(unittest.TestCase):
    def test_a_windowed_program_that_imports_its_engine(self):
        source = (LAUNCHER / "cof_launch.cpp").read_text(encoding="utf-8")
        self.assertIn("int WINAPI wWinMain(", source)
        self.assertIn('extern "C" __declspec( dllimport ) int Host_Main(', source)
        for gone in ("LoadLibraryW", "GetProcAddress", "SetDllDirectoryW", "int wmain("):
            self.assertNotIn(gone, source)
        script = SCRIPT.read_text(encoding="utf-8")
        for flag in ("/SUBSYSTEM:WINDOWS", "/MANIFEST:EMBED", "shell32.lib", "$libArg"):
            self.assertIn(flag, script)
        self.assertNotIn("/SUBSYSTEM:CONSOLE", script)

    def test_its_icon_is_plain_bitmaps(self):
        self.assertIn('ICON "coffix-bitmaps.ico"', (LAUNCHER / "cof_launch.rc").read_text(encoding="utf-8"))
        data = ICON.read_bytes()
        count = struct.unpack_from("<HHH", data, 0)[2]
        for i in range(count):
            size, offset = struct.unpack_from("<II", data, 6 + 16 * i + 8)
            self.assertNotEqual(data[offset:offset + 4], b"\x89PNG", f"entry {i} is PNG-compressed")
        self.assertLess(entropy(data), 5.0)


@unittest.skipUnless(package.BINARY_INPUTS["CoFLaunchApp.exe"].is_file(), "the starter is not built here")
class StarterBinaryTests(unittest.TestCase):
    def test_the_packaged_starter_is_windowed_with_no_packed_looking_section(self):
        self.assertEqual(package.BINARY_INPUTS["CoFLaunchApp.exe"].parent, package.LAUNCHER)
        data = package.BINARY_INPUTS["CoFLaunchApp.exe"].read_bytes()
        subsystem, sections = pe_layout(data)
        self.assertEqual(subsystem, 2)   # IMAGE_SUBSYSTEM_WINDOWS_GUI
        self.assertIn(b"xash.dll", data)
        rsrc = dict(sections)[".rsrc"]
        self.assertLess(entropy(rsrc), 6.0)   # the console build's PNG icon: 7.95


if __name__ == "__main__":
    unittest.main()
