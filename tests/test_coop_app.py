"""The tester app's entry decisions (tools/coop_app.py); the window itself is checked by --self-test."""
import json
import subprocess
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

    def test_a_copy_in_a_folder_named_outside_ascii_is_found_again(self):
        # 1.1.9 (L14.7): the default copy's folder holds the Windows user name; the pointer note keeps it as UTF-8
        with tempfile.TemporaryDirectory() as temp:
            other = Path(temp) / "Usuários" / "José Ñúñez" / "Иван 测试" / "CoF-Story-Coop"
            installed(other, 1)
            pointer = Path(temp) / "CoF-Story-Coop.txt"
            app.remember_copy(other, pointer)
            self.assertEqual(app.where_to_start(Path(temp), Path(temp) / "none", 1, pointer), ("main", other.resolve()))
            self.assertEqual(app.where_to_start(other, Path(temp) / "none", 1, pointer), ("main", other))

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
        self.assertEqual(app.footer_text(), "Cry of Fear Co-op 1.5")

    def test_report_a_bug_opens_the_discord(self):
        with mock.patch.object(app.webbrowser, "open") as browser:
            app.report_bug()
        browser.assert_called_once_with("https://discord.gg/4uwV6jmmY7")


class ProgressPanelTests(unittest.TestCase):
    """Setup's and update's live status: the step, the bar, a line for each thing done and the time taken
    (the user, 2026-10-20: "so people can see what it's actually doing in real time")."""

    def setUp(self):
        import tkinter as tk
        try:
            self.window = tk.Tk()
        except tk.TclError:
            self.skipTest("no display for the window")
        self.window.withdraw()
        self.addCleanup(self.window.destroy)
        self.app = app.App(self.window, None, Path("app.exe"))

    def test_the_time_taken_reads_like_a_clock(self):
        self.assertEqual([app.elapsed_text(s) for s in (0, 9.9, 65, 3725)], ["0:00", "0:09", "1:05", "1:02:05"])

    def test_it_shows_the_step_the_bar_each_line_and_the_time_taken(self):
        clock = iter([100.0, 165.0])
        panel = self.app.progress_panel(self.app.body, clock=lambda: next(clock))
        panel.start()
        panel.show("Step 2 of 6: Copying game files, 12 of 4000 MB", .25, "05:00:01  Step 2 of 6: Copying game files")
        panel.show("Step 2 of 6: Copying game files, 40 of 4000 MB", .3, None)
        panel.show("Step 2 of 6: Copying game files", .3, "05:00:09  Copied cryoffear/sound: 2 files, 1 KB")
        self.assertEqual(panel.status.get(), "Step 2 of 6: Copying game files")
        self.assertEqual(float(panel.bar["value"]), 30.0)
        self.assertEqual(panel.box.get("1.0", "end-1c"), "05:00:01  Step 2 of 6: Copying game files\n"
                                                         "05:00:09  Copied cryoffear/sound: 2 files, 1 KB")
        self.assertEqual(str(panel.box["state"]), "disabled")   # read-only for the player
        panel.tick()
        self.assertEqual(panel.elapsed.get(), "1:05 so far")
        panel.stop()

    def test_setup_and_update_both_show_it(self):
        self.app.show_setup()
        self.assertIsNotNone(self.app.panel)
        self.app.show_update(Path("copy"))
        self.assertIsNotNone(self.app.panel.box)


def widgets(parent):
    for child in parent.winfo_children():
        yield child
        yield from widgets(child)


def texts(parent):
    found = []
    for widget in widgets(parent):
        try:
            found.append(str(widget.cget("text")))
        except Exception:
            pass
    return found


def button(parent, text):
    return next(w for w in widgets(parent) if w.winfo_class() == "TButton" and w.cget("text") == text)


class WindowTests(unittest.TestCase):
    """The 1.1.8 fixes in the window (findings L1, L3, L4, L7, L11, L14)."""

    def setUp(self):
        import tkinter as tk
        try:
            self.window = tk.Tk()
        except tk.TclError:
            self.skipTest("no display for the window")
        self.window.withdraw()
        self.addCleanup(self.window.destroy)
        self.temp = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(self.temp, ignore_errors=True))
        self.app = app.App(self.window, Path("payload"), self.temp / "app.exe")
        self.jobs = []
        self.app.work = lambda job, done, failed=None: self.jobs.append((job, done, failed))
        self.errors = []
        self.app.error = self.errors.append
        self.app.name = tk.StringVar(value="Bob")

    def test_a_failing_handler_does_not_stop_the_others(self):
        # L14.9: the re-arm was after the loop, so one exception lost every later result for good
        seen = []
        self.app.messages.put((lambda value: 1 / 0, None))
        self.app.messages.put((seen.append, "next"))
        self.app.pump()
        self.assertEqual(seen, ["next"])
        self.assertIsInstance(self.errors[0], ZeroDivisionError)

    def test_a_failed_process_check_is_shown_not_swallowed(self):
        # L3: guard did not catch SubprocessError, and the windowed app has no stderr: the button did nothing
        def check():
            raise subprocess.TimeoutExpired(["powershell"], 20)
        self.app.guard(check)()
        self.assertEqual(len(self.errors), 1)

    def test_closing_during_setup_or_update_asks_first(self):
        # L14.2: closing ends the job part way
        from tkinter import messagebox
        destroyed = []
        self.app.window.destroy = lambda: destroyed.append(True)
        for busy in ("setup", "update"):
            self.app.busy = busy
            with self.subTest(busy=busy), mock.patch.object(messagebox, "askokcancel", return_value=False) as ask:
                self.app.close()
                self.assertIn("part way", ask.call_args.args[1])
            self.assertEqual(destroyed, [])
        self.app.busy = None
        with mock.patch.object(messagebox, "askokcancel") as ask:
            self.app.close()
        ask.assert_not_called()
        self.assertEqual(destroyed, [True])

    def copy(self, build=app.play.BUILD):
        installed(self.temp / "copy", build)
        (self.temp / "copy/cryoffear").mkdir(exist_ok=True)
        self.app.root = self.temp / "copy"
        return self.app.root

    def test_start_checks_the_copy_off_the_ui_thread(self):
        # L11: the check hashes about 0.6 GB and asks PowerShell; on the Tk thread the window froze
        root = self.copy()
        calls, started, stopped = [], [], []
        with mock.patch.object(friend, "validate_runtime", side_effect=calls.append), \
                mock.patch.object(friend, "game_is_running", return_value=False):
            self.app.launch(["game"], "cfg\n", lambda: started.append(1), lambda: stopped.append(1))
            self.assertEqual(calls, [])   # nothing yet on the UI thread
            job, done, failed = self.jobs[0]
            running = job()
        self.assertEqual((calls, running), ([root], False))
        self.app.launch(["game"], "cfg\n", lambda: started.append(2), lambda: stopped.append(2))
        self.assertEqual((len(self.jobs), stopped), (1, [2]))   # one start at a time
        with mock.patch.object(app, "start_game") as start:
            done(False)
        start.assert_called_once_with(root, ["game"])
        self.assertEqual((root / "cryoffear" / app.play.SESSION_CFG).read_text(), "cfg\n")
        self.assertEqual(started, [1])
        self.app.launch(["game"], "cfg\n", lambda: started.append(3), lambda: stopped.append(3))
        with mock.patch.object(app, "start_game") as start:
            self.jobs[-1][1](True)   # already running
        start.assert_not_called()
        self.assertEqual((started, stopped[-1]), ([1], 3))
        self.assertIn("already running", str(self.errors[-1]))

    def test_a_start_brings_the_players_own_unlocks(self):
        # 1.3.8 (a player: "the unlocks you already unlocked, would transfer to the simon's story with friends")
        from tests.test_unlocks import settings
        root = self.copy()
        game = self.temp / "Cry of Fear"
        (game / "cryoffear").mkdir(parents=True)
        (game / app.unlocks.SETTINGS).write_bytes(settings({"ending 1", "the axe"}))
        (root / app.unlocks.SETTINGS).write_bytes(settings())
        state = json.loads((root / friend.INSTALLED).read_text())
        (root / friend.INSTALLED).write_text(json.dumps(dict(state, source_game=str(game))))
        with mock.patch.object(friend, "validate_runtime"), mock.patch.object(friend, "game_is_running",
                                                                               return_value=False):
            self.app.launch(["game"], "cfg\n", lambda: None)
            job, done, _ = self.jobs[-1]
            running = job()
        with mock.patch.object(app, "start_game") as start:
            done(running)
        start.assert_called_once()
        self.assertEqual(app.unlocks.unlocked_names((root / app.unlocks.SETTINGS).read_bytes()), ["ending 1", "the axe"])
        # never in the way of starting the game
        with mock.patch.object(app.unlocks, "carry_into_copy", side_effect=PermissionError("in use")), \
                mock.patch.object(friend, "validate_runtime"), mock.patch.object(friend, "game_is_running",
                                                                                 return_value=False):
            self.app.launch(["game"], "cfg\n", lambda: None)
            job, done, _ = self.jobs[-1]
            running = job()
            with mock.patch.object(app, "start_game") as start:
                done(running)
        start.assert_called_once()

    def test_play_starts_the_game_at_its_menu_and_shares_the_join_code_with_it(self):
        # 1.3.6 (the owner: "integrate the mod into the main menu somehow"): the game opens at its main menu, whose
        # Co-op list starts the games; this window keeps the router port and the join code, and the menu's Host co-op
        # reads the code from cof_invite.txt. 1.3.7: that is the app's one button, Play
        root = self.copy()
        (root / "cryoffear" / app.play.INVITE).write_text("code COF1-OLD-00000000\n", encoding="ascii")
        with mock.patch.object(app.play, "load_settings", return_value=dict(app.play.DEFAULT_SETTINGS, name="Bob")):
            self.app.show_main(root)
        opened = button(self.app.body, "Play")
        opened.invoke()
        self.assertEqual(str(opened.cget("state")), "disabled")   # one start at a time while the copy is checked
        self.jobs[-1][2](ValueError("Close it first."))
        self.assertEqual(str(opened.cget("state")), "normal")
        opened.invoke()
        with mock.patch.object(app, "start_game") as game, mock.patch.object(app.threading, "Thread"):
            self.jobs[-1][1](False)
        args = game.call_args.args[1]
        self.assertNotIn("+map", args)
        self.assertNotIn("+connect", args)
        self.assertEqual(args[args.index("-port") + 1], str(app.play.DEFAULT_PORT))
        self.assertTrue(app.play.load_settings(root)["first_launch_done"])
        self.assertIn(f'set cof_menu_maps "{self.app.maps_tag()}"', (root / "cryoffear" / app.play.SESSION_CFG).read_text())
        # no other session's join code in the menu, then this one's once the router answers
        self.assertEqual((root / "cryoffear" / app.play.INVITE).read_text(), "")
        self.assertIn("Cry of Fear is open", texts(self.app.body))
        watch = self.app.watch
        self.addCleanup(setattr, watch, "closed", True)
        self.assertIsInstance(watch, app.play.MenuWatch)
        job, routed, _ = self.jobs[-2]
        watch.code, watch.address = "COF1-AB-12345678", "203.0.113.7:27015"
        routed(None)
        self.assertEqual((root / "cryoffear" / app.play.INVITE).read_text(),
                         "code COF1-AB-12345678\naddress 203.0.113.7:27015\n")
        self.jobs[-1][1]([{"address": "192.168.1.5"}])
        self.assertIn("lan 192.168.1.5:27015", (root / "cryoffear" / app.play.INVITE).read_text())

    def test_a_copy_that_fails_its_check_is_offered_repair(self):
        # L1: at the same build no Update was offered, and the only way out was a new setup
        from tkinter import messagebox
        self.copy()
        mismatch = friend.CopyMismatch("A merged story map is missing or changed. This game copy does not match "
                                       "its install.")
        with mock.patch.object(messagebox, "askyesno", return_value=True) as ask, \
                mock.patch.object(self.app, "show_update") as update:
            self.app.launch(["game"], "cfg\n", lambda: None)
            self.jobs[-1][2](mismatch)
        self.assertIn("A merged story map is missing", ask.call_args.args[1])
        self.assertNotIn("click Update", ask.call_args.args[1])
        update.assert_called_once_with(self.app.root, auto=True, repair=True)
        self.assertFalse(self.app.launching)
        with mock.patch.object(messagebox, "askyesno", return_value=False), \
                mock.patch.object(self.app, "show_update") as update:
            self.app.offer_repair(mismatch)
        update.assert_not_called()

    def test_no_repair_without_a_payload_or_over_a_newer_copy(self):
        from tkinter import messagebox
        mismatch = friend.CopyMismatch("This game copy does not match its install.")
        for payload, build in ((None, app.play.BUILD), (Path("payload"), app.play.BUILD + 1)):
            self.app.payload = payload
            self.copy(build)
            with self.subTest(payload=payload, build=build), mock.patch.object(messagebox, "askyesno") as ask:
                self.app.offer_repair(mismatch)
            ask.assert_not_called()
            self.assertIs(self.errors[-1], mismatch)

    def test_the_repair_screen(self):
        root = self.copy()
        self.app.show_update(root, repair=True)
        self.assertIn("Repair story co-op", texts(self.app.body))
        button(self.app.body, "Repair").invoke()
        self.assertEqual(self.app.busy, "update")
        job, done, failed = self.jobs[-1]
        with mock.patch.object(friend, "update", return_value=root) as update:
            job()
        self.assertEqual(update.call_args.args, (root, Path("payload")))
        failed(ValueError("stopped"))
        self.assertIsNone(self.app.busy)

    def test_setup_offers_to_delete_an_unfinished_copy(self):
        # L4: a stopped setup left its copy, and setup refused the folder from then on
        from tkinter import messagebox
        folder = self.temp / "CoF-Story-Coop"
        folder.mkdir()
        (folder / friend.INCOMPLETE).write_text("Setup is incomplete.\n")
        (folder / friend.SETUP_LOG).write_text("log\n")
        with mock.patch.object(app, "DEFAULT_COPY", folder):
            self.app.show_setup()
        self.jobs.clear()
        with mock.patch.object(messagebox, "askyesno", return_value=False):
            button(self.app.body, "Set up").invoke()
        self.assertEqual(self.jobs, [])
        with mock.patch.object(messagebox, "askyesno", return_value=True) as ask:
            button(self.app.body, "Set up").invoke()
        self.assertIn("unfinished copy", ask.call_args.args[1])
        self.assertEqual(self.app.busy, "setup")
        job, done, failed = self.jobs[-1]
        with mock.patch.object(friend, "setup") as setup:
            job()
        self.assertIs(setup.call_args.kwargs["replace_incomplete"], True)
        failed(RuntimeError("The engine could not be installed."))
        self.assertIsNone(self.app.busy)
        self.assertIn("delete the unfinished copy", self.app.panel.status.get())

    def test_the_running_screen_names_the_firewall(self):
        # L7: the first host's Windows Firewall question can sit behind the borderless game
        self.copy()
        watch = app.play.MenuWatch(27015, runtime=self.app.root)
        self.addCleanup(lambda: setattr(watch, "closed", True))
        with mock.patch.object(app.threading, "Thread"):
            self.app.show_running(watch)
        self.assertTrue(any("Windows Firewall" in text for text in texts(self.app.body)))

    def test_the_main_screen_is_one_button(self):
        # 1.3.7 (the owner: "now that we've integrated the fucking campaign co-op into the main menu, the launcher is
        # kind of redundant"; his pick: a one-button launcher): name, port, Play; no Play solo, Host, Join or own co-op
        self.copy()
        self.app.check_for_update = lambda: None
        settings = dict(app.play.DEFAULT_SETTINGS, name="Bob", port=27020)
        with mock.patch.object(app.play, "load_settings", return_value=settings):
            self.app.show_main(self.app.root)
        shown = [w.cget("text") for w in widgets(self.app.body) if w.winfo_class() == "TButton"]
        self.assertEqual(shown, ["Play"])
        for gone in ("Play solo", "Host a game", "Join a game", "Cry of Fear's own co-op"):
            self.assertNotIn(gone, texts(self.app.body))
        self.assertTrue(any("Co-op" in text and "New Game" in text for text in texts(self.app.body)))
        for name in ("show_start", "show_join", "show_official", "start_choice"):
            self.assertFalse(hasattr(app.App, name), name)
        # the port is the app's to set (hosting from the menu uses the one the game was started with)
        self.assertEqual(self.app.port.get(), "27020")
        self.app.port.set("80")
        button(self.app.body, "Play").invoke()
        self.assertIn("1024 to 65534", str(self.errors[-1]))
        self.app.port.set("27030")
        with mock.patch.object(app.play, "save_settings") as saved:
            button(self.app.body, "Play").invoke()
            with mock.patch.object(app, "start_game") as game, mock.patch.object(app.threading, "Thread"):
                self.jobs[-1][1](False)
        self.addCleanup(setattr, self.app.watch, "closed", True)
        args = game.call_args.args[1]
        self.assertEqual(args[args.index("-port") + 1], "27030")
        self.assertTrue(any(c.args[1].get("port") == 27030 for c in saved.call_args_list))   # remembered

    def test_the_main_screen_fits_its_window(self):
        # the window is 760 px tall (a 768 px screen leaves 688), and an update offer adds a box
        import types
        self.copy()
        self.app.check_for_update = lambda: None
        heights = []
        for offer in (None, types.SimpleNamespace(label="9.9", page_url="https://example.invalid")):
            self.app.offer = offer
            with mock.patch.object(app.play, "load_settings", return_value=dict(app.play.DEFAULT_SETTINGS, name="Bob")):
                self.app.show_main(self.app.root)
            self.window.update_idletasks()
            heights.append(self.window.winfo_reqheight())
        self.assertLessEqual(heights[0], 688)
        self.assertLessEqual(heights[1], 760)

    def test_a_newer_copy_offers_only_its_own_shortcut(self):
        # L14.11: the older app opened the main screen and started the newer engine with its own settings
        root = self.copy(app.play.BUILD + 1)
        self.app.show_newer(root)
        self.assertIn("Use the Cry of Fear Co-op shortcut", texts(self.app.body))
        shown = [w.cget("text") for w in widgets(self.app.body) if w.winfo_class() == "TButton"]
        self.assertEqual(shown, [])
        self.assertEqual(self.app.root, root)   # Open logs folder still works
        with mock.patch.object(app, "where_to_start", return_value=("newer", root)), \
                mock.patch.object(app, "App") as window_app, mock.patch("tkinter.Tk"):
            app.open_window(Path("payload"), self.temp / "app.exe")
        window_app.return_value.show_newer.assert_called_once_with(root)
        window_app.return_value.show_main.assert_not_called()

    def test_hosting_again_closes_the_last_watch_before_the_router(self):
        # L14.4: a game hosted again before the last one's watch saw it end: that watch, closing later, removed
        # the router mapping the new one had just taken over
        self.copy()
        calls = []

        class Net:
            NetworkError = app.network.NetworkError

            @staticmethod
            def upnp_map(port):
                calls.append("map")
                return {"port": port}

            @staticmethod
            def upnp_unmap(mapping):
                calls.append("unmap")

            @staticmethod
            def discover_public_address():
                return ""
        old, new = (app.play.MenuWatch(27015, net=Net) for _ in range(2))
        old.mapping = {"port": 27015}
        for watch in (old, new):
            self.addCleanup(setattr, watch, "closed", True)
        self.app.watch = old
        with mock.patch.object(app.threading, "Thread"):
            self.app.show_running(new)
        self.assertEqual(calls, [])   # nothing on the UI thread
        self.jobs[0][0]()
        self.assertEqual(calls, ["unmap", "map"])
        self.assertTrue(old.closed)
        self.assertIs(self.app.watch, new)



if __name__ == "__main__":
    unittest.main()
