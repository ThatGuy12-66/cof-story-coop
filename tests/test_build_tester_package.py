"""The package builder's refusals and the zip it writes (no real build here)."""
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
            path = Path(temp) / "CoF-Story-Coop-1.1.4.zip"
            builder.write_zip(folder, path)
            with zipfile.ZipFile(path) as archive:
                self.assertIsNone(archive.testzip())
                self.assertEqual(sorted(archive.namelist()), [
                    "CoF Story Co-op.exe", "READ ME FIRST.txt", "source-and-licences/corresponding-source.zip",
                    "source-and-licences/notices/PYTHON-LICENSE.txt"])
        self.assertEqual(builder.ZIP_NAME, "CoF-Story-Coop-1.1.4.zip")

    def test_the_app_carries_the_merged_map_groups(self):
        out, payload = Path("C:/out"), Path("C:/out/payload")
        command = builder.pyinstaller_command(out, payload)
        groups = builder.ROOT / "tools/map_groups.json"
        self.assertIn(f"{groups}{builder.os.pathsep}tools", command)
        self.assertIn(f"{payload}{builder.os.pathsep}payload", command)

    def test_readme_names_the_release_and_no_test_or_dev_tools(self):
        text = builder.README.read_text(encoding="utf-8")
        self.assertTrue(text.startswith("Cry of Fear Co-op 1.1.4\n"))
        self.assertIn("WHAT'S NEW IN 1.1.4", text)
        self.assertIn("1.1, 1.1.1, 1.1.2, 1.1.3 and 1.1.4 can play together", text)
        self.assertIn("https://discord.gg/4uwV6jmmY7", text)
        for word in ("protocol", "sha256", "rcon", "console", "test build", "test package", "tester", "cheat"):
            self.assertNotIn(word, text.lower())

    def test_the_source_archive_carries_the_readme_it_ships(self):
        self.assertIn(builder.README.relative_to(builder.ROOT).as_posix(), builder.enhanced.LOCAL_SOURCES)

    def test_the_payload_leaves_out_upstreams_install_docs_and_cheats(self):
        # the app installs the engine itself: Enhanced's Install.cmd/Uninstall.cmd and its README (which say to
        # install into the Steam folder), the cheats guide and the tester notices have no place in a player's copy
        kept = ["cof-enhanced/DISCLAIMER.md", "cof-enhanced/LICENSE", "cof-enhanced/MANIFEST.sha256",
                "cof-enhanced/files/xash.dll", "cof-enhanced/install.ps1"]
        with tempfile.TemporaryDirectory() as temp:
            source, target = Path(temp) / "player.zip", Path(temp) / "payload.zip"
            with zipfile.ZipFile(source, "w") as archive:
                for name in kept + ["README.txt", "Install.cmd", "Uninstall.cmd", "EXPERIMENTAL-STORY.txt",
                                    "STORY-START.txt", "cof-enhanced/CHEATS.md"]:
                    archive.writestr(name, name)
            builder.player_payload(source, target)
            with zipfile.ZipFile(target) as archive:
                self.assertIsNone(archive.testzip())
                self.assertEqual(sorted(archive.namelist()), kept)
                self.assertEqual(archive.read("cof-enhanced/files/xash.dll"), b"cof-enhanced/files/xash.dll")


if __name__ == "__main__":
    unittest.main()
