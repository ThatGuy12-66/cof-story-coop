"""The automated live runs' pieces that need no game: reading a session log, and putting a developer engine into a
copy and the packaged one back (tools/live_loop.py)."""
from pathlib import Path
import tempfile
import unittest

from tools import live_loop

LOG = """\
[2026:10:16|20:47:17] Spawn Server: c_start2 [hahaland]
[2026:10:16|20:47:18] cofstory phase=companion-mode userid=3 mode=idle
[2026:10:16|20:47:18] cofstory phase=companion-mode userid=3 mode=follow
[2026:10:16|20:47:18] cofstory phase=companion-mode userid=4 mode=follow
[2026:10:16|20:47:20] cofstory phase=companion-teleport userid=4 leader=1 reason=door at=1,2,3 leader_at=4,5,6 to=7,8,9
[2026:10:16|20:47:21] Error: Could not load sound sound/save/save_no.wav
[2026:10:16|20:47:22] Host_Error: SV_AllocEdict: no free edicts (max is 1200)
[2026:10:16|20:48:17] Spawn Server: c_start [hahaland]
"""


class ReportTests(unittest.TestCase):
    def test_counts_mode_changes_teleports_maps_and_errors(self):
        report = live_loop.read_report(LOG)
        self.assertAlmostEqual(report.minutes, 1.0)
        self.assertEqual(report.modes, {"3": 2, "4": 1})
        self.assertEqual(report.worst_second, (2, "20:47:18 u3"))
        self.assertEqual(report.teleports, {"door": 1})
        self.assertEqual(report.maps, ["c_start2", "c_start"])
        # a missing sound is Cry of Fear's own (its files lack them), not a defect
        self.assertEqual(report.errors, ["Host_Error: SV_AllocEdict: no free edicts (max is 1200)"])
        self.assertTrue(any("no free edicts" in note for note in report.notes))


class EngineInstallTests(unittest.TestCase):
    def test_a_developer_engine_goes_in_and_the_packaged_one_comes_back(self):
        with tempfile.TemporaryDirectory() as tmp:
            copy, build = Path(tmp) / "copy", Path(tmp) / "build.dll"
            copy.mkdir()
            (copy / "xash.dll").write_bytes(b"packaged")
            build.write_bytes(b"developer")
            packaged = live_loop._sha(copy / "xash.dll")
            self.assertIsNone(live_loop.dev_engine(copy))
            live_loop.install(copy, build)
            self.assertEqual((copy / "xash.dll").read_bytes(), b"developer")
            self.assertEqual(live_loop.dev_engine(copy), live_loop._sha(build)[:8])
            # a second build replaces the first; the packaged one stays kept
            build.write_bytes(b"developer 2")
            live_loop.install(copy, build)
            self.assertEqual((copy / f"xash.dll.{packaged[:8]}.bak").read_bytes(), b"packaged")
            live_loop.restore(copy)
            self.assertEqual((copy / "xash.dll").read_bytes(), b"packaged")
            self.assertIsNone(live_loop.dev_engine(copy))
            self.assertFalse((copy / live_loop.MARKER).exists())

    def test_an_engine_replaced_by_an_update_is_not_taken_for_the_developer_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            copy, build = Path(tmp) / "copy", Path(tmp) / "build.dll"
            copy.mkdir()
            (copy / "xash.dll").write_bytes(b"packaged")
            build.write_bytes(b"developer")
            live_loop.install(copy, build)
            (copy / "xash.dll").write_bytes(b"a newer package")
            self.assertIsNone(live_loop.dev_engine(copy))


class RecoveryTests(unittest.TestCase):
    def test_a_killed_run_is_undone_settings_and_saves(self):
        from unittest import mock
        with tempfile.TemporaryDirectory() as tmp, \
                mock.patch.object(live_loop, "PENDING", Path(tmp) / "pending"), \
                mock.patch.object(live_loop, "game_processes", lambda copy: []):
            copy = Path(tmp) / "copy"
            (copy / "cryoffear").mkdir(parents=True)
            (copy / "SAVE" / "cofcoop").mkdir(parents=True)
            (copy / "cryoffear" / "config.cfg").write_text('volume "0.3"\n')
            (copy / "cryoffear" / "cof_story.cfg").write_text("sv_cheats 0\n")
            (copy / "cof-story-coop-settings.json").write_text('{"name": "gore"}')
            (copy / "SAVE" / "c_start.HL1").write_bytes(b"theirs")
            (copy / "SAVE" / "cofcoop" / "latest.txt").write_text("autosave")
            live_loop._begin(copy)
            # the run: cheats on, volume 0, a new game clears the map saves and autosaves elsewhere
            (copy / "cryoffear" / "config.cfg").write_text('volume "0"\n')
            (copy / "cryoffear" / "cof_story.cfg").write_text("sv_cheats 1\n")
            (copy / "cof-story-coop-settings.json").write_text('{"name": "Tester"}')
            (copy / "SAVE" / "c_start.HL1").unlink()
            (copy / "SAVE" / "c_nightmare.HL1").write_bytes(b"run")
            # ...killed here; the next run (or recover) undoes it
            self.assertTrue(live_loop.recover(copy))
            self.assertEqual((copy / "cryoffear" / "config.cfg").read_text(), 'volume "0.3"\n')
            self.assertEqual((copy / "cryoffear" / "cof_story.cfg").read_text(), "sv_cheats 0\n")
            self.assertEqual((copy / "cof-story-coop-settings.json").read_text(), '{"name": "gore"}')
            self.assertEqual(sorted(p.name for p in (copy / "SAVE").rglob("*") if p.is_file()), ["c_start.HL1", "latest.txt"])
            self.assertFalse(live_loop.PENDING.exists())
            self.assertFalse(live_loop.recover(copy))   # nothing left to undo


class TestLauncherTests(unittest.TestCase):
    def test_the_game_runs_under_a_name_of_its_own_kept_in_step_with_the_launcher(self):
        with tempfile.TemporaryDirectory() as tmp:
            copy = Path(tmp)
            (copy / "CoFLaunchApp.exe").write_bytes(b"launcher 1")
            exe = live_loop.test_launcher(copy)
            self.assertEqual(exe.name, "CoFLaunchApp-autotest.exe")
            self.assertEqual(exe.read_bytes(), b"launcher 1")
            (copy / "CoFLaunchApp.exe").write_bytes(b"launcher 2")   # an update
            self.assertEqual(live_loop.test_launcher(copy).read_bytes(), b"launcher 2")


class LaunchTests(unittest.TestCase):
    def test_no_companions_turns_the_bots_off(self):
        # the merged-map checks stand the player in each door alone: no bot following through the doors
        args, config = live_loop.launch_config(Path("C:/copy"), "cm_hamn", companions=0, password="pw")
        self.assertIn("cof_companion 0\n", config)
        self.assertIn("maxplayers 2\n", config)
        self.assertIn('rcon_password "pw"\nrcon_enable 1\n', config)
        self.assertEqual(args[-2:], ["+map", "cm_hamn"])

    def test_companions_as_asked(self):
        _, config = live_loop.launch_config(Path("C:/copy"), "c_start2", companions=2, password="pw", merged=False)
        self.assertIn("cof_companion 2\n", config)
        self.assertIn("maxplayers 3\n", config)
        self.assertIn("cof_merged_maps 0\n", config)

    def test_a_run_leaves_the_mouse_alone(self):
        # the user, during a run: "make it so I don't accidentally move the camera around": the game ignores the
        # mouse (no mouse look, cursor free; locked before the player joins, so it goes in the session cfg) and
        # opens in a small window instead of filling the desktop
        args, config = live_loop.launch_config(Path("C:/copy"), "c_start2", companions=1, password="pw")
        self.assertIn("m_ignore 1\n", config)
        self.assertEqual(args[1:6], ["-windowed", "-width", "960", "-height", "540"])
        self.assertNotIn("-borderless", args)


if __name__ == "__main__":
    unittest.main()
