"""The tester package builder's refusals and the zip it writes (no real build here)."""
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

from tools import build_tester_package as builder


class BuilderTests(unittest.TestCase):
    def test_uncommitted_work_is_refused(self):
        reply = subprocess.CompletedProcess([], 0, " M engine/server/sv_client.c\n?? new.c\n", "")
        with mock.patch.object(builder.subprocess, "run", return_value=reply):
            self.assertEqual(builder.dirty(Path(".")), [" M engine/server/sv_client.c", "?? new.c"])
            with tempfile.TemporaryDirectory() as temp, self.assertRaisesRegex(ValueError, "Commit"):
                builder.build(Path(temp) / "out")
            self.assertFalse((Path(temp) / "out").exists())

    def test_zip_layout(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp) / "bundle"
            for rel in ("CoF Story Co-op.exe", "READ ME FIRST.txt", "source-and-licences/corresponding-source.zip",
                        "source-and-licences/notices/PYTHON-LICENSE.txt"):
                (folder / rel).parent.mkdir(parents=True, exist_ok=True)
                (folder / rel).write_bytes(b"x")
            path = Path(temp) / "CoF-Story-Coop-Test1.zip"
            builder.write_zip(folder, path)
            with zipfile.ZipFile(path) as archive:
                self.assertIsNone(archive.testzip())
                self.assertEqual(sorted(archive.namelist()), [
                    "CoF Story Co-op.exe", "READ ME FIRST.txt", "source-and-licences/corresponding-source.zip",
                    "source-and-licences/notices/PYTHON-LICENSE.txt"])
        self.assertEqual(builder.ZIP_NAME, "CoF-Story-Coop-Test1.zip")

    def test_readme_names_the_version_and_no_dev_tools(self):
        text = builder.README.read_text(encoding="utf-8")
        self.assertIn("Test 1", text)
        for word in ("protocol", "sha256", "rcon", "console"):
            self.assertNotIn(word, text.lower())


if __name__ == "__main__":
    unittest.main()
