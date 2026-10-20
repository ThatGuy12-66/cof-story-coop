"""The app's own updates (tools/app_update.py): the release.json, the check, the download, the hand-over."""
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

from tools import app_update as up
from tools import coop_app as app
from tools import friend_setup as friend

ZIP_BYTES = b"pretend package " * 1000
SHA = hashlib.sha256(ZIP_BYTES).hexdigest()


def info(**changes):
    data = {"schema": 1, "label": "1.1.2", "version": "1.1", "build": 7, "zip": "CoF-Story-Coop-1.1.2.zip",
            "size": len(ZIP_BYTES), "sha256": SHA}
    data.update(changes)
    return data


class Response(io.BytesIO):
    """urlopen's answer: a context manager with read(n)."""


def opener_for(payload: bytes, seen=None):
    def opener(url, timeout):
        if seen is not None:
            seen.append((url, timeout))
        return Response(payload)
    return opener


class ReleaseInfoTests(unittest.TestCase):
    def test_a_good_release_json(self):
        release = up.parse_release(info())
        self.assertEqual((release.label, release.version, release.build), ("1.1.2", "1.1", 7))
        self.assertEqual(release.zip_url, "https://github.com/charliee1w/cof-story-coop-releases/releases/download/"
                                          "v1.1.2/CoF-Story-Coop-1.1.2.zip")
        self.assertEqual(release.page_url, "https://github.com/charliee1w/cof-story-coop-releases/releases/tag/v1.1.2")

    def test_anything_odd_means_no_update(self):
        for bad in (info(schema=2), info(build="7"), info(build=True), info(build=0), info(label="1.1.2-beta"),
                    info(label="../x"), info(zip="../CoF-Story-Coop-1.1.2.zip"), info(zip="evil.exe"),
                    info(zip="CoF-Story-Coop-1.1.2.zip/../x.zip"), info(size=0), info(size=3 << 30),
                    info(sha256="A" * 64), info(sha256="0" * 63), info(version=""), [], "x", None):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                up.parse_release(bad)

    def test_the_release_json_the_pipeline_writes_is_one_the_app_accepts(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "CoF-Story-Coop-1.1.2.zip"
            path.write_bytes(ZIP_BYTES)
            data = up.release_info("1.1.2", "1.1", 7, path)
        self.assertEqual(data, info())
        self.assertEqual(up.parse_release(json.loads(json.dumps(data))).sha256, SHA)

    def test_the_latest_release_json_comes_from_the_releases_page(self):
        seen = []
        data = up.read_info(opener=opener_for(json.dumps(info()).encode(), seen))
        self.assertEqual(data, info())
        self.assertEqual(seen[0][0], "https://github.com/charliee1w/cof-story-coop-releases/releases/latest/download/"
                                     "release.json")
        with self.assertRaises(ValueError):   # not a small json file
            up.read_info(opener=opener_for(b"x" * (up.MAX_INFO_BYTES + 1)))

    def test_only_a_higher_build_is_offered(self):
        read = lambda: info()   # noqa: E731
        self.assertEqual(up.newer_release(6, read).label, "1.1.2")
        self.assertIsNone(up.newer_release(7, read))
        self.assertIsNone(up.newer_release(8, read))


class DownloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.temp, ignore_errors=True))
        self.release = up.parse_release(info())

    def test_download_checks_size_and_sha256(self):
        seen, steps = [], []
        path = up.download(self.release, self.temp, lambda done, total: steps.append((done, total)),
                           opener=opener_for(ZIP_BYTES, seen))
        self.assertEqual(path.read_bytes(), ZIP_BYTES)
        self.assertEqual(seen[0][0], self.release.zip_url)
        self.assertEqual(steps[-1], (len(ZIP_BYTES), len(ZIP_BYTES)))
        self.assertEqual(sorted(p.name for p in self.temp.iterdir()), ["CoF-Story-Coop-1.1.2.zip"])

    def test_a_damaged_download_is_refused_and_removed(self):
        for payload in (ZIP_BYTES[:-1] + b"X", ZIP_BYTES[:-10], ZIP_BYTES + b"more"):
            with self.subTest(size=len(payload)), self.assertRaises(ValueError):
                up.download(self.release, self.temp, opener=opener_for(payload))
            self.assertEqual(list(self.temp.iterdir()), [])

    def test_cancel_stops_and_removes_the_part(self):
        calls = iter([False, True])
        with mock.patch.object(up, "CHUNK", 1000), self.assertRaises(up.Cancelled):
            up.download(self.release, self.temp, cancelled=lambda: next(calls), opener=opener_for(ZIP_BYTES))
        self.assertEqual(list(self.temp.iterdir()), [])

    def test_extract_gives_the_new_app(self):
        package = self.temp / "pkg.zip"
        with zipfile.ZipFile(package, "w") as archive:
            archive.writestr(friend.APP_EXE, b"MZ new app")
            archive.writestr("READ ME FIRST.txt", "hi")
            archive.writestr("source-and-licences/notices/X.txt", "x")
        exe = up.extract_app(package, self.temp)
        self.assertEqual(exe, self.temp / "app" / friend.APP_EXE)
        self.assertEqual(exe.read_bytes(), b"MZ new app")
        self.assertTrue((self.temp / "app/source-and-licences/notices/X.txt").is_file())

    def test_extract_refuses_a_zip_without_the_app_or_with_odd_paths(self):
        empty = self.temp / "empty.zip"
        with zipfile.ZipFile(empty, "w") as archive:
            archive.writestr("READ ME FIRST.txt", "hi")
        with self.assertRaises(ValueError):
            up.extract_app(empty, self.temp)
        evil = self.temp / "evil.zip"
        with zipfile.ZipFile(evil, "w") as archive:
            archive.writestr("../outside.txt", "x")
        with self.assertRaises(ValueError):
            up.extract_app(evil, self.temp)
        self.assertFalse((self.temp.parent / "outside.txt").exists())
        broken = self.temp / "broken.zip"
        broken.write_bytes(b"not a zip")
        with self.assertRaises(ValueError):
            up.extract_app(broken, self.temp)

    def test_old_downloads_go_unless_this_app_is_one(self):
        folder = self.temp / "CoF-Story-Coop-update"
        (folder / "1.1.2/app").mkdir(parents=True)
        up.clean_downloads(folder / "1.1.2/app" / friend.APP_EXE, folder)
        self.assertTrue(folder.is_dir())
        up.clean_downloads(self.temp / "copy" / friend.APP_EXE, folder)
        self.assertFalse(folder.exists())


class HandOverTests(unittest.TestCase):
    def test_the_new_app_gets_the_copy_and_who_to_wait_for(self):
        popen = mock.Mock()
        up.start_new_app(Path("dl/app/new.exe"), Path("C:/copy"), [11, 12], popen=popen)
        command = popen.call_args.args[0]
        self.assertEqual(command, [str(Path("dl/app/new.exe")), "--update-from", str(Path("C:/copy")),
                                   "--wait-pid", "11,12"])
        self.assertEqual(popen.call_args.kwargs["cwd"], str(Path("dl/app")))

    @unittest.skipUnless(os.name == "nt", "Windows process flags")
    def test_it_breaks_away_from_a_job_when_it_can(self):
        popen = mock.Mock(side_effect=[OSError("access denied"), "started"])
        self.assertEqual(up.start_new_app(Path("new.exe"), Path("copy"), [1], popen=popen), "started")
        first, second = (c.kwargs["creationflags"] for c in popen.call_args_list)
        self.assertTrue(first & up.CREATE_BREAKAWAY_FROM_JOB)
        self.assertFalse(second & up.CREATE_BREAKAWAY_FROM_JOB)

    def test_pids(self):
        self.assertEqual(up.parse_pids("11,12, x,"), [11, 12])
        self.assertEqual(up.parse_pids(None), [])
        self.assertEqual(up.pids_to_wait()[0], os.getpid())

    def test_waiting_for_the_old_app(self):
        import threading
        sleeper = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(0.6)"])
        threading.Thread(target=sleeper.wait, daemon=True).start()   # reaped as it ends, like any process not ours
        self.assertTrue(up.running(sleeper.pid))
        self.assertTrue(up.wait_for_exit([sleeper.pid], timeout=20))
        self.assertFalse(up.running(sleeper.pid))
        clock = iter(range(100))
        self.assertFalse(up.wait_for_exit([1234], timeout=3, is_running=lambda pid: True, sleep=lambda s: None,
                                          clock=lambda: next(clock)))


class AppCommandLineTests(unittest.TestCase):
    def test_update_from_and_wait_pid_reach_the_window(self):
        with mock.patch.object(app, "open_window") as window:
            self.assertEqual(app.main(["--update-from", "rel-copy", "--wait-pid", "5,6"]), 0)
        self.assertEqual(window.call_args.kwargs["update_from"], Path("rel-copy").resolve())
        self.assertEqual(window.call_args.kwargs["wait"], [5, 6])

    def test_update_check_writes_what_it_found(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / "check.json"
            with mock.patch.object(up, "read_info", return_value=info()):
                self.assertEqual(app.main(["--update-check", str(out)]), 0)
            found = json.loads(out.read_text(encoding="utf-8"))
            self.assertEqual((found["ok"], found["latest"]["label"], found["newer"]),
                             (True, "1.1.2", 7 > app.play.BUILD))
            with mock.patch.object(up, "read_info", side_effect=OSError("offline")):
                self.assertEqual(app.main(["--update-check", str(out)]), 1)
            self.assertIn("offline", json.loads(out.read_text(encoding="utf-8"))["error"])

    def test_open_window_from_the_old_version(self):
        with tempfile.TemporaryDirectory() as temp:
            copy = Path(temp) / "copy"
            copy.mkdir()
            state = {"schema": 2, "version": "1.1", "build": app.play.BUILD - 1, "xash_sha256": "0" * 64}
            (copy / friend.INSTALLED).write_text(json.dumps(state))
            with mock.patch.object(app, "App") as window_app, mock.patch("tkinter.Tk"):
                app.open_window(None, Path(temp) / "new.exe", update_from=copy, wait=[9])
            window_app.return_value.show_update.assert_called_once_with(copy, auto=True, wait=[9])
            state["build"] = app.play.BUILD   # already updated: just the main screen
            (copy / friend.INSTALLED).write_text(json.dumps(state))
            with mock.patch.object(app, "App") as window_app, mock.patch("tkinter.Tk"):
                app.open_window(None, Path(temp) / "new.exe", update_from=copy, wait=[9])
            window_app.return_value.show_main.assert_called_once_with(copy)

    def test_the_download_error_a_player_sees(self):
        import urllib.error
        self.assertIn("internet connection", app.update_error(urllib.error.URLError("no route")))
        self.assertIn("damaged", app.update_error(ValueError("The download is damaged. Try again.")))


class UpdateWindowTests(unittest.TestCase):
    """The offer on the main screen, Update now, and the new app finishing the update by itself."""

    def setUp(self):
        import tkinter as tk
        try:
            self.window = tk.Tk()
        except tk.TclError:
            self.skipTest("no display for the window")
        self.window.withdraw()
        self.addCleanup(self.window.destroy)
        self.temp = Path(tempfile.mkdtemp())
        self.app = app.App(self.window, None, self.temp / "app.exe")
        self.app.work = lambda job, done, failed=None: None   # no background threads unless a test asks
        self.release = up.parse_release(info())

    def labels(self, widget):
        found = []
        for child in widget.winfo_children():
            try:
                found.append(child.cget("text"))
            except Exception:
                pass
            found += self.labels(child)
        return found

    def test_the_main_screen_offers_a_newer_version(self):
        with mock.patch.object(app.play, "load_settings", return_value=dict(app.play.DEFAULT_SETTINGS)):
            self.app.show_main(self.temp)
            self.assertNotIn("Update now", self.labels(self.app.body))
            self.app.offer_update(self.release)
            self.assertIn("Version 1.1.2 is out.", self.labels(self.app.body))
            self.assertIn("Update now", self.labels(self.app.body))
            self.app.show_main(self.temp)   # coming back to the main screen keeps the offer
            self.assertIn("Update now", self.labels(self.app.body))
            self.app.offer_update(None)
            self.assertIn("Update now", self.labels(self.app.body))

    def test_the_check_runs_once_and_cleans_old_downloads(self):
        jobs = []
        self.app.work = lambda job, done, failed=None: jobs.append((job, done))
        with mock.patch.object(app.play, "load_settings", return_value=dict(app.play.DEFAULT_SETTINGS)):
            self.app.show_main(self.temp)
            self.app.show_main(self.temp)
        self.assertEqual(len(jobs), 1)
        with mock.patch.object(up, "clean_downloads") as clean, \
                mock.patch.object(up, "newer_release", return_value=self.release) as newer:
            self.assertEqual(jobs[0][0](), self.release)
        clean.assert_called_once_with(self.temp / "app.exe")
        newer.assert_called_once_with(app.play.BUILD)

    def test_update_now_needs_the_game_closed(self):
        self.app.root, self.app.offer = self.temp, self.release
        with mock.patch.object(friend, "game_is_running", return_value=True):
            with self.assertRaisesRegex(ValueError, "Close Cry of Fear"):
                self.app.update_now()
        with mock.patch.object(friend, "game_is_running", return_value=False):
            self.app.update_now()
        self.assertIn("Update to 1.1.2", self.labels(self.app.body))
        self.assertIn("Cancel", self.labels(self.app.body))

    def test_a_finished_download_starts_the_new_app_and_closes_this_one(self):
        calls = {}
        self.app.work = lambda job, done, failed=None: calls.update(job=job, done=done, failed=failed)
        self.app.root = self.temp
        self.app.show_download(self.release)
        with mock.patch.object(up, "download", return_value=self.temp / "z.zip") as download, \
                mock.patch.object(up, "extract_app", return_value=self.temp / "new.exe"):
            self.assertEqual(calls["job"](), self.temp / "new.exe")
        self.assertEqual(download.call_args.args[:2], (self.release, up.DOWNLOADS / "1.1.2"))
        destroyed = []
        self.app.window.destroy = lambda: destroyed.append(True)
        with mock.patch.object(up, "start_new_app") as start:
            calls["done"](self.temp / "new.exe")
        self.assertEqual(start.call_args.args[:2], (self.temp / "new.exe", self.temp))
        self.assertEqual(destroyed, [True])

    def test_a_failed_or_cancelled_download_goes_back_to_the_main_screen(self):
        calls = {}
        self.app.work = lambda job, done, failed=None: calls.update(failed=failed)
        self.app.root = self.temp
        errors = []
        self.app.error = errors.append
        with mock.patch.object(app.play, "load_settings", return_value=dict(app.play.DEFAULT_SETTINGS)):
            self.app.show_download(self.release)
            calls["failed"](up.Cancelled())
            self.assertIn("Play solo (with AI companions)", self.labels(self.app.body))
            self.assertEqual(errors, [])
            self.app.show_download(self.release)
            calls["failed"](ValueError("The download is damaged. Try again."))
        self.assertEqual(errors, ["The update stopped: The download is damaged. Try again."])

    def test_the_new_app_waits_for_the_old_one_then_updates_by_itself(self):
        calls = {}
        self.app.work = lambda job, done, failed=None: calls.update(job=job, done=done)
        self.app.show_update(self.temp, auto=True, wait=[4242])
        self.assertIn("job", calls)   # started without a click
        order = []
        with mock.patch.object(up, "wait_for_exit", side_effect=lambda pids: order.append(("wait", pids))), \
                mock.patch.object(friend, "update", side_effect=lambda *a, **k: order.append("update") or self.temp):
            calls["job"]()
        self.assertEqual(order, [("wait", [4242]), "update"])



if __name__ == "__main__":
    unittest.main()
