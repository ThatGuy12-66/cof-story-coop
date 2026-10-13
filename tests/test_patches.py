"""Exercise byte transformations on read-only local input and temporary copies."""
import contextlib
import io
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from cofcoop import core
from tools import patch_coop


STOCK_FILE = core.DEFAULT_GAME_DIR / "cryoffear" / "cl_dlls" / "hl.dll.orig"


def load_rebased_image(data, base):
    """Emulate the PE32 loader from headers, independent of patch constants."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[pe:pe + 4] == b"PE\0\0"
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    assert struct.unpack_from("<H", data, optional)[0] == 0x10B
    preferred = struct.unpack_from("<I", data, optional + 28)[0]
    image_size, header_size = struct.unpack_from("<II", data, optional + 56)
    image = bytearray(image_size)
    image[:header_size] = data[:header_size]
    sections = optional + optional_size
    for index in range(section_count):
        _virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, sections + 40 * index + 8)
        image[rva:rva + raw_size] = data[raw_offset:raw_offset + raw_size]
    reloc_rva, reloc_size = struct.unpack_from("<II", data, optional + 96 + 5 * 8)
    cursor, end = reloc_rva, reloc_rva + reloc_size
    relocations = []
    while cursor < end:
        page_rva, block_size = struct.unpack_from("<II", image, cursor)
        assert block_size >= 8 and block_size % 2 == 0 and cursor + block_size <= end
        for entry_offset in range(cursor + 8, cursor + block_size, 2):
            entry = struct.unpack_from("<H", image, entry_offset)[0]
            kind, operand_rva = entry >> 12, page_rva + (entry & 0xFFF)
            relocations.append((kind, operand_rva))
            if kind == 3:
                value = struct.unpack_from("<I", image, operand_rva)[0]
                struct.pack_into("<I", image, operand_rva, (value + base - preferred) & 0xFFFFFFFF)
            else:
                assert kind == 0, f"Unhandled relocation type {kind}"
        cursor += block_size
    assert cursor == end
    return bytes(image), relocations


@unittest.skipUnless(STOCK_FILE.is_file(), "The user's stock DLL is optional and is never distributed")
class PatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stock = STOCK_FILE.read_bytes()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "Cry of Fear"
        (self.root / "cryoffear" / "maps").mkdir(parents=True)
        (self.root / "cof.exe").write_bytes(b"fixture only")
        self.dll = self.root / "cryoffear" / "cl_dlls" / "hl.dll"
        self.dll.parent.mkdir()
        self.dll.write_bytes(self.stock)
        self.backup = self.dll.with_name("hl.dll.orig")
        self.backup.write_bytes(self.stock)
        self.evidence = Path(self.temporary.name) / "evidence" / "patches"
        evidence_patch = patch.object(patch_coop, "EVIDENCE_DIR", self.evidence)
        evidence_patch.start()
        self.addCleanup(evidence_patch.stop)

    def test_real_stock_hash_offsets_and_all_mode_reversals(self):
        self.assertEqual(patch_coop.sha256(self.stock), patch_coop.STOCK_SHA256)
        self.assertEqual(self.stock[0x109160:0x109166], bytes.fromhex("0f85b5010000"))
        self.assertEqual(self.stock[0x109306:0x10930b], bytes.fromhex("6834102310"))
        self.assertEqual(self.stock[0x22e996:0x22e998], bytes.fromhex("073f"))
        for source_mode in patch_coop.MODES:
            source = patch_coop.transform(self.stock, source_mode)
            self.assertEqual(patch_coop.identify(source), source_mode)
            for target_mode in patch_coop.MODES:
                with self.subTest(source=source_mode, target=target_mode):
                    result = patch_coop.transform(source, target_mode)
                    self.assertEqual(result, patch_coop.transform(self.stock, target_mode))
                    self.assertEqual(patch_coop.transform(result, "stock"), self.stock)

    def test_transforms_touch_only_documented_exact_ranges(self):
        allowed = set(range(0x109160, 0x109166))
        plain = allowed | set(range(0x109306, 0x10930b)) | {0x22e997}
        for mode, expected in (("allow-transition", allowed), ("plain-coop-transition", plain)):
            result = patch_coop.transform(self.stock, mode)
            actual = {index for index, (a, b) in enumerate(zip(self.stock, result)) if a != b}
            self.assertEqual(actual, expected)
            self.assertEqual(len(result), len(self.stock))

    def test_tampered_or_unrecognized_current_dll_is_refused(self):
        for index in (0, 0x109160, 0x109306, 0x22e996, 0x22e997, len(self.stock) - 1):
            data = bytearray(self.stock)
            data[index] ^= 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                patch_coop.transform(bytes(data), "stock")
        with self.assertRaises(ValueError):
            patch_coop.transform(self.stock[:100], "allow-transition")

    def test_invalid_stock_backup_is_refused_even_when_current_is_stock(self):
        self.backup.write_bytes(patch_coop.transform(self.stock, "allow-transition"))
        with self.assertRaisesRegex(ValueError, "not the verified stock"):
            patch_coop.inspect_patch(self.root, "plain-coop-transition")
        self.assertEqual(self.dll.read_bytes(), self.stock)

    def test_inspection_preserves_existing_patch_and_creates_no_evidence(self):
        existing = patch_coop.transform(self.stock, "allow-transition")
        self.dll.write_bytes(existing)
        report = patch_coop.inspect_patch(self.root)
        self.assertEqual(report["current_mode"], "allow-transition")
        self.assertEqual(report["target_mode"], "allow-transition")
        self.assertFalse(report["applied"])
        report = patch_coop.inspect_patch(self.root, "plain-coop-transition")
        self.assertIn("single player", report["warning"])
        self.assertEqual(len(report["changes"]), 2)
        self.assertIn({"offset": "0x22e996", "before": "073f", "after": "070f"}, report["changes"])
        self.assertEqual(self.dll.read_bytes(), existing)
        self.assertFalse(self.evidence.exists())

    def legacy_plain(self):
        legacy = bytearray(self.stock)
        legacy[0x109160:0x109166] = bytes.fromhex("909090909090")
        legacy[0x109306:0x10930b] = bytes.fromhex("6a00909090")
        return bytes(legacy)

    def test_legacy_hash_recognition_repair_and_full_reversal(self):
        legacy = self.legacy_plain()
        self.assertEqual(patch_coop.sha256(legacy), patch_coop.LEGACY_PLAIN_SHA256)
        self.assertEqual(patch_coop.identify(legacy), patch_coop.LEGACY_PLAIN_MODE)
        fixed = patch_coop.transform(legacy, "plain-coop-transition")
        self.assertEqual(patch_coop.sha256(fixed), patch_coop.PLAIN_SHA256)
        self.assertEqual(fixed, patch_coop.transform(self.stock, "plain-coop-transition"))
        self.assertEqual(patch_coop.transform(legacy, "stock"), self.stock)
        self.assertEqual(patch_coop.transform(fixed, "stock"), self.stock)
        self.assertEqual(patch_coop.sha256(patch_coop.transform(fixed, "stock")), patch_coop.STOCK_SHA256)
        self.assertEqual(patch_coop.transform(legacy, "allow-transition"), patch_coop.transform(self.stock, "allow-transition"))
        with self.assertRaises(ValueError):
            patch_coop.transform(self.stock, patch_coop.LEGACY_PLAIN_MODE)

    @patch("cofcoop.core.running_game_processes", return_value=[])
    def test_legacy_dry_run_and_repair_change_only_relocation_word(self, _processes):
        legacy = self.legacy_plain()
        self.dll.write_bytes(legacy)
        report = patch_coop.inspect_patch(self.root)
        self.assertTrue(report["legacy_repair_required"])
        self.assertEqual(report["current_mode"], patch_coop.LEGACY_PLAIN_MODE)
        self.assertEqual(report["target_mode"], "plain-coop-transition")
        self.assertEqual(report["changes"], [{"offset": "0x22e996", "before": "073f", "after": "070f"}])
        self.assertEqual(self.dll.read_bytes(), legacy)
        self.assertFalse(self.evidence.exists())
        applied = patch_coop.apply_mode(self.root, "plain-coop-transition")
        self.assertEqual(applied["verified_sha256"], patch_coop.PLAIN_SHA256)
        self.assertEqual(self.backup.read_bytes(), self.stock)
        patch_coop.apply_mode(self.root, "stock")
        self.assertEqual(self.dll.read_bytes(), self.stock)

    def test_rebasing_preserves_null_instructions_and_other_relocations(self):
        fixed = patch_coop.transform(self.stock, "plain-coop-transition")
        for base in (0x10000000, 0x68EC0000, 0x00400000, 0x7F120000, 0xE0000000):
            with self.subTest(base=hex(base)):
                loaded, relocations = load_rebased_image(fixed, base)
                self.assertIn((0, 0x109F07), relocations)
                self.assertNotIn((3, 0x109F07), relocations)
                self.assertEqual(loaded[0x109F06:0x109F0B], bytes.fromhex("6a00909090"))
                # The immediately following real address operand still relocates.
                self.assertEqual(struct.unpack_from("<I", loaded, 0x109F0C)[0], (base + 0x231014) & 0xFFFFFFFF)
                stock_loaded, stock_relocations = load_rebased_image(self.stock, base)
                self.assertIn((3, 0x109F07), stock_relocations)
                self.assertEqual(struct.unpack_from("<I", stock_loaded, 0x109F07)[0], (base + 0x231034) & 0xFFFFFFFF)

    def test_rebasing_reproduces_legacy_live_instruction_corruption(self):
        loaded, relocations = load_rebased_image(self.legacy_plain(), 0x68EC0000)
        self.assertIn((3, 0x109F07), relocations)
        self.assertEqual(loaded[0x109F06:0x109F0B], bytes.fromhex("6a00907ce9"))
        self.assertNotEqual(loaded[0x109F06:0x109F0B], bytes.fromhex("6a00909090"))

    @patch("cofcoop.core.running_game_processes", return_value=[{"pid": 1234}])
    def test_active_game_refuses_application_without_writing(self, _processes):
        with self.assertRaisesRegex(RuntimeError, "already running"):
            patch_coop.apply_mode(self.root, "allow-transition")
        self.assertEqual(self.dll.read_bytes(), self.stock)
        self.assertFalse(self.evidence.exists())

    @patch("cofcoop.core.running_game_processes", side_effect=[[], [{"pid": 1234}]])
    def test_game_starting_during_preparation_blocks_atomic_replace(self, _processes):
        with self.assertRaisesRegex(RuntimeError, "already running"):
            patch_coop.apply_mode(self.root, "allow-transition")
        self.assertEqual(self.dll.read_bytes(), self.stock)
        self.assertEqual(list(self.dll.parent.glob(".hl.dll.cofcoop-*.tmp")), [])
        self.assertFalse(self.evidence.exists())

    @patch("cofcoop.core.running_game_processes", return_value=[])
    def test_apply_and_restore_verify_hashes_and_leave_stock_backup_unchanged(self, _processes):
        for mode in ("allow-transition", "plain-coop-transition", "stock"):
            report = patch_coop.apply_mode(self.root, mode)
            self.assertEqual(patch_coop.identify(self.dll.read_bytes()), mode)
            self.assertEqual(report["verified_sha256"], patch_coop.sha256(self.dll.read_bytes()))
            evidence = Path(report["evidence_file"])
            self.assertTrue(evidence.is_relative_to(self.evidence))
            self.assertEqual(json.loads(evidence.read_text())["target_mode"], mode)
            self.assertEqual(self.backup.read_bytes(), self.stock)
        self.assertEqual(len(list(self.evidence.glob("*.json"))), 3)
        self.assertEqual(self.dll.read_bytes(), self.stock)

    def test_hardlinked_dll_and_backup_are_refused(self):
        for target in (self.dll, self.backup):
            link = target.with_suffix(".hardlink")
            os.link(target, link)
            try:
                with self.subTest(target=target.name), self.assertRaisesRegex(ValueError, "hard-linked"):
                    patch_coop.inspect_patch(self.root)
            finally:
                link.unlink()

    def test_cli_is_dry_run_by_default(self):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            code = patch_coop.main(["--game-dir", str(self.root), "--mode", "allow-transition"])
        self.assertEqual(code, 0)
        self.assertFalse(json.loads(output.getvalue())["applied"])
        self.assertEqual(self.dll.read_bytes(), self.stock)
        self.assertFalse(self.evidence.exists())

    def test_protected_paths_are_rejected_before_resolution(self):
        for path in (r"D:\copy", r"C:\Steam\steamapps\common\Cry of Fear"):
            with self.subTest(path=path), patch.object(Path, "resolve", side_effect=AssertionError("touched protected path")):
                with self.assertRaisesRegex(ValueError, "protected"):
                    patch_coop.inspect_patch(path)


if __name__ == "__main__":
    unittest.main()
