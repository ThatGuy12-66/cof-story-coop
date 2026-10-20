"""The tester app's entry decisions (tools/coop_app.py); the window itself is checked by --self-test."""
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import coop_app as app
from tools import friend_setup as friend


def installed(folder: Path, build: int):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / friend.INSTALLED).write_text(json.dumps({"schema": 2, "version": f"test{build}", "build": build,
                                                       "xash_sha256": "0" * 64, "models": {}, "source_game": "src"}))


class StartTests(unittest.TestCase):
    def test_where_to_start(self):
        with tempfile.TemporaryDirectory() as temp:
            downloads, copy = Path(temp) / "Downloads", Path(temp) / "CoF-Story-Coop"
            downloads.mkdir()
            pointer = Path(temp) / "no-pointer.txt"
            self.assertEqual(app.where_to_start(downloads, copy, 1, pointer), ("setup", None))
            installed(copy, 1)
            self.assertEqual(app.where_to_start(downloads, copy, 1, pointer), ("main", copy))
            self.assertEqual(app.where_to_start(downloads, copy, 2, pointer), ("update", copy))
            self.assertEqual(app.where_to_start(downloads, copy, 0, pointer), ("newer", copy))
            # the app started from inside a copy plays from that copy
            self.assertEqual(app.where_to_start(copy, Path(temp) / "elsewhere", 1, pointer), ("main", copy))

    def test_a_copy_set_up_elsewhere_is_found_again(self):
        # a newer package must offer Update for a copy set up outside the default folder
        with tempfile.TemporaryDirectory() as temp:
            downloads, default, other = Path(temp) / "Downloads", Path(temp) / "CoF-Story-Coop", Path(temp) / "D" / "Coop"
            downloads.mkdir()
            pointer = Path(temp) / "CoF-Story-Coop.txt"
            self.assertEqual(app.where_to_start(downloads, default, 1, pointer), ("setup", None))
            installed(other, 1)
            app.remember_copy(other, pointer)
            self.assertEqual(app.where_to_start(downloads, default, 2, pointer), ("update", other.resolve()))
            self.assertEqual(app.where_to_start(downloads, default, 1, pointer), ("main", other.resolve()))
            # the default folder still comes first, and a pointer to a deleted copy is ignored
            installed(default, 1)
            self.assertEqual(app.where_to_start(downloads, default, 1, pointer), ("main", default))
            pointer.write_text(str(Path(temp) / "gone"), encoding="utf-8")
            self.assertEqual(app.where_to_start(downloads, Path(temp) / "none", 1, pointer), ("setup", None))

    def test_copy_flag_takes_a_relative_path(self):
        # game_is_running compares absolute paths: a relative --copy missed this copy's game
        with mock.patch.object(app, "open_window") as window:
            self.assertEqual(app.main(["--copy", "rel-copy"]), 0)
        self.assertEqual(window.call_args.args[2], Path("rel-copy").resolve())

    def test_the_game_starts_without_a_console_window(self):
        # CoFLaunchApp.exe is a console program: started from the windowed app, Windows
        # would open a console beside the game (live 2026-10-15: irrKlang warnings on screen)
        with mock.patch.object(app.subprocess, "Popen") as popen:
            app.start_game(Path("copy"), ["copy/CoFLaunchApp.exe", "+map", "c_intro"])
        kwargs = popen.call_args.kwargs
        self.assertEqual(kwargs["cwd"], Path("copy"))
        self.assertTrue(kwargs["creationflags"] & 0x08000000)   # CREATE_NO_WINDOW
        self.assertEqual(kwargs["stdin"], app.subprocess.DEVNULL)

    def test_cli_install_needs_a_payload_outside_the_exe(self):
        self.assertEqual(app.main(["--install", "src", "dst"]), 2)


class ReleaseTests(unittest.TestCase):
    def test_the_footer_names_the_release_not_a_test_build(self):
        self.assertEqual(app.footer_text(), "Cry of Fear Co-op 1.0")

    def test_report_a_bug_opens_the_discord(self):
        with mock.patch.object(app.webbrowser, "open") as browser:
            app.report_bug()
        browser.assert_called_once_with("https://discord.gg/4uwV6jmmY7")


if __name__ == "__main__":
    unittest.main()
