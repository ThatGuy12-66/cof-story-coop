"""Core checks use disposable game fixtures and never launch the real game."""
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest
from concurrent.futures import ThreadPoolExecutor
from types import SimpleNamespace
from unittest.mock import Mock, patch

from cofcoop import core
from cofcoop.bsp import list_maps, load_map_entities


class CoreTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "Cry of Fear"
        (self.root / "cryoffear" / "maps").mkdir(parents=True)
        (self.root / "cof.exe").write_bytes(b"fixture only")
        (self.root / "hlds.exe").write_bytes(b"fixture only")
        (self.root / "cryoffear" / "maps" / "c_intro.bsp").write_bytes(b"fixture only")
        self.options = core.LaunchOptions(self.root)
        self.steam_registry = patch("cofcoop.core._read_steam_active_process", return_value=(4321, 9876)).start()
        self.steam_process = patch("cofcoop.core._process_is_live", return_value=True).start()
        self.addCleanup(patch.stopall)
        self.addCleanup(core._LIVE_PROCESSES.clear)

    def test_catalog_filters_false_links_and_deduplicates_real_exits(self):
        catalog = {
            "start": {"links": [
                {"class": "cof_chapter", "key": "target", "map": "b"},
                {"class": "trigger_changelevel", "key": "targetname", "map": "b"},
                {"class": "trigger_changelevel", "key": "map", "map": "b"},
                {"class": "trigger_changelevel", "key": "map", "map": "b"},
                {"class": "trigger_changelevel", "key": "map", "map": "missing"},
                {"class": "trigger_changelevel", "key": "map", "map": "a"},
            ]}, "a": {}, "b": {},
        }
        self.assertEqual(core.next_maps(catalog, "START"), ["b", "a"])
        self.assertEqual(core.next_maps(catalog, "missing"), [])

    def test_catalog_rejects_incomplete_scan(self):
        path = Path(self.temporary.name) / "report.json"
        path.write_text(json.dumps({"maps": {}, "errors": {"broken": "truncated"}}))
        with self.assertRaisesRegex(ValueError, "scan errors"):
            core.load_catalog(path)

    def test_protected_paths_are_rejected_before_resolution(self):
        for path in (r"D:\SteamLibrary\steamapps\common\Cry of Fear",
                     r"D:\copied-game", r"\\?\D:\copied-game", r"C:\Steam\steamapps\common\Cry of Fear"):
            with self.subTest(path=path), patch.object(Path, "resolve", side_effect=AssertionError("filesystem touched")):
                with self.assertRaisesRegex(ValueError, "protected"):
                    core.validate_game_dir(path)

    def test_missing_and_injected_maps_never_prepare_configs(self):
        for value in ("missing", "../c_intro", "c_intro;quit", 'c_intro"', "c_intro\nquit", "c_intro.bsp"):
            self.options.map_name = value
            with self.subTest(map=value), self.assertRaises(ValueError):
                core.prepare_launch(self.options)
        self.assertFalse((self.root / "cryoffear" / core.CONFIG_NAME).exists())

    def test_config_injection_is_rejected(self):
        for key in ("name", "rcon_password", "password"):
            for value in ('a" quit', "a;quit", "a\nquit", "a\rquit", "a\x00quit", "a\\quit"):
                options = core.LaunchOptions(self.root)
                setattr(options, key, value)
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    core.build_config(options)

    def test_bad_numbers_and_reversed_rate_bounds_are_rejected(self):
        cases = {"port": 65536, "maxplayers": 1, "rate": True, "cl_cmdrate": "100",
                 "sv_minrate": 100001, "sv_minupdaterate": 101}
        for key, value in cases.items():
            options = core.LaunchOptions(self.root)
            setattr(options, key, value)
            with self.subTest(key=key), self.assertRaises(ValueError):
                core.build_launch_args(options)

    def test_launch_order_uses_direct_engine_and_multiplayer_rules(self):
        args = core.build_launch_args(self.options)
        self.assertEqual(Path(args[0]), self.root / "cof.exe")
        self.assertEqual(args[-2:], ["+map", "c_intro"])
        self.assertLess(args.index("+exec"), args.index("+map"))
        self.assertEqual(args[args.index("+coop") + 1], "0")
        self.assertEqual(args[args.index("+deathmatch") + 1], "1")
        self.assertIn("-condebug", args)
        config = core.build_config(self.options)
        self.assertIn("mp_allowmonsters 1\n", config)
        self.assertIn('name "Player"\n', config)
        self.assertIn("cl_updaterate 100\n", config)
        self.options.dedicated = True
        args = core.build_launch_args(self.options)
        self.assertEqual(Path(args[0]), self.root / "hlds.exe")
        self.assertIn("-console", args)
        self.assertNotIn("-windowed", args)

    @patch("cofcoop.core.running_game_processes", return_value=[])
    def test_preparation_preserves_userconfig_bytes_and_original_backup(self, _processes):
        game = self.root / "cryoffear"
        original = b'// My bindings\r\nbind "x" "say caf\xe9"'
        (game / "userconfig.cfg").write_bytes(original)
        (game / "config.cfg").write_bytes(b"engine owned")
        core.prepare_launch(self.options)
        updated = (game / "userconfig.cfg").read_bytes()
        self.assertTrue(updated.startswith(original + b"\r\n"))
        self.assertEqual(updated.count(b"exec cofcoop_launcher.cfg"), 1)
        backups = list(game.glob("userconfig.cfg.cofcoop-backup-*"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), original)
        self.options.name = "Second player"
        core.prepare_launch(self.options)
        self.assertEqual((game / "userconfig.cfg").read_bytes(), updated)
        self.assertEqual(len(list(game.glob("userconfig.cfg.cofcoop-backup-*"))), 1)
        self.assertEqual((game / "config.cfg").read_bytes(), b"engine owned")

    @patch("cofcoop.core.running_game_processes", return_value=[{"pid": 1234}])
    def test_live_game_blocks_all_config_changes(self, _processes):
        userconfig = self.root / "cryoffear" / "userconfig.cfg"
        userconfig.write_bytes(b"existing\n")
        with self.assertRaisesRegex(RuntimeError, "1234"):
            core.prepare_launch(self.options)
        self.assertEqual(userconfig.read_bytes(), b"existing\n")
        self.assertFalse((userconfig.parent / core.CONFIG_NAME).exists())

    @patch("cofcoop.core.running_game_processes", return_value=[])
    def test_hardlinked_config_is_rejected_before_write(self, _processes):
        target = Path(self.temporary.name) / "shared.cfg"
        target.write_bytes(b"shared")
        config = self.root / "cryoffear" / "userconfig.cfg"
        os.link(target, config)
        with self.assertRaisesRegex(ValueError, "hard-linked"):
            core.prepare_launch(self.options)
        self.assertEqual(target.read_bytes(), b"shared")
        self.assertFalse((config.parent / core.CONFIG_NAME).exists())

    def test_store_roundtrip_and_corruption_does_not_overwrite(self):
        path = Path(self.temporary.name) / "state" / "settings.json"
        store = core.SettingsStore(path)
        self.assertEqual(store.load(), {})
        settings = {"game_dir": str(self.root), "name": "Player", "rate": 100000}
        store.save(settings)
        self.assertEqual(store.load(), settings)
        self.assertEqual(list(path.parent.glob("*.tmp")), [])
        path.write_text("invalid json", encoding="utf-8")
        with self.assertRaises(ValueError):
            store.load()
        self.assertEqual(path.read_text(), "invalid json")
        path.write_text("[]", encoding="utf-8")
        with self.assertRaises(ValueError):
            store.load()

    def test_endpoint_validation_does_not_allow_console_commands(self):
        for bad in ("1.2.3.4;quit", "localhost\nquit", 'host"', "+quit", "-host", "a..b",
                    "256.1.1.1", "1.2.3.4:0", "host:65536", "host:", "http://host", "::1", "host :27015"):
            with self.subTest(address=bad), self.assertRaises(ValueError):
                core.validate_endpoint(bad)
        self.assertEqual(core.validate_endpoint("My-PC.local"), "my-pc.local:27015")
        self.assertEqual(core.validate_endpoint("100.101.102.103:27016"), "100.101.102.103:27016")

    @patch("cofcoop.core.running_game_processes", return_value=[])
    @patch("cofcoop.core.subprocess.Popen")
    def test_join_passes_one_validated_connect_argument_and_no_shell(self, popen, _processes):
        process = core.launch_join(self.root, "Player One", "100.101.102.103:27016", password="group secret")
        self.assertIs(process, popen.return_value)
        args, kwargs = popen.call_args
        self.assertEqual(args[0][-2:], ["+connect", "100.101.102.103:27016"])
        self.assertNotIn("shell", kwargs)
        self.assertEqual(kwargs["cwd"], str(self.root))
        text = (self.root / "cryoffear" / core.CONFIG_NAME).read_text()
        self.assertIn('name "Player One"', text)
        self.assertIn('password "group secret"', text)
        self.assertNotIn("rcon_password", text)

    @patch("cofcoop.core.running_game_processes", return_value=[])
    @patch("cofcoop.core.subprocess.Popen")
    def test_join_rejects_injected_password_before_writing(self, popen, _processes):
        with self.assertRaises(ValueError):
            core.launch_join(self.root, "Player", "localhost", password='hello";quit')
        popen.assert_not_called()
        self.assertFalse((self.root / "cryoffear" / core.CONFIG_NAME).exists())

    @unittest.skipUnless(os.name == "nt", "Windows Steam session preflight")
    @patch("cofcoop.core.running_game_processes", return_value=[])
    @patch("cofcoop.core.subprocess.Popen")
    def test_steam_session_zero_or_dead_blocks_client_before_config_write(self, popen, _processes):
        for pid, user, alive in ((0, 0, True), (123, 0, True), (0, 456, True), (123, 456, False)):
            self.steam_registry.return_value = (pid, user)
            self.steam_process.return_value = alive
            with self.subTest(pid=pid, user=user, alive=alive):
                with self.assertRaisesRegex(RuntimeError, "Steam has no active signed-in session"):
                    core.launch_game(self.options)
                with self.assertRaisesRegex(RuntimeError, "Steam has no active signed-in session"):
                    core.launch_join(self.root, "Player", "localhost")
                self.assertFalse((self.root / "cryoffear" / core.CONFIG_NAME).exists())
        popen.assert_not_called()

    @unittest.skipUnless(os.name == "nt", "Windows Steam session preflight")
    def test_steam_preflight_accepts_live_signed_in_registry_session(self):
        core.ensure_steam_ready()
        self.steam_registry.assert_called_once_with()
        self.steam_process.assert_called_once_with(4321)

    @patch("cofcoop.core.running_game_processes", return_value=[])
    def test_dedicated_preparation_does_not_require_client_steam_session(self, _processes):
        self.steam_registry.return_value = (0, 0)
        self.options.dedicated = True
        core.prepare_launch(self.options)
        self.steam_registry.assert_not_called()

    @patch("cofcoop.core.subprocess.run", return_value=SimpleNamespace(stdout="[]"))
    @patch("cofcoop.core.subprocess.Popen")
    def test_simultaneous_launch_clicks_start_one_process(self, popen, _process_listing):
        popen.return_value = Mock(pid=9999)
        popen.return_value.poll.return_value = None
        ready = threading.Barrier(2)
        def launch():
            ready.wait(timeout=5)
            try:
                return core.launch_game(self.options)
            except RuntimeError as exc:
                return str(exc)
        with ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(lambda _: launch(), range(2)))
        self.assertEqual(popen.call_count, 1)
        self.assertEqual(sum("already running" in result for result in results if isinstance(result, str)), 1)


class LogTrackerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.log = Path(self.temporary.name) / "qconsole.log"

    def test_refused_changelevel_does_not_create_checkpoint(self):
        self.log.write_text('CHANGE LEVEL: c_nightmare landmark\n'
                            'changelevel2 <levelname> : not for use with multiplayer games\n'
                            'Loading map "c_nightmare"\n'
                            'Player said: Started map "c_nightmare"\n')
        tracker = core.LogTracker(self.log)
        self.assertEqual(tracker.poll(), [])
        with self.log.open("a") as stream:
            stream.write('L 09/27/2026 - 12:00:00: Started map "c_intro" (CRC "-123")\n')
        self.assertEqual(tracker.poll(), [{"kind": "map", "map": "c_intro"}])
        self.assertEqual(tracker.poll(), [])

    def test_partial_lines_append_truncate_and_same_size_rewrite(self):
        tracker = core.LogTracker(self.log)
        self.assertEqual(tracker.poll(), [])
        self.log.write_bytes(b'Started map "c_in')
        self.assertEqual(tracker.poll(), [])
        with self.log.open("ab") as stream:
            stream.write(b'tro"\r\n')
        self.assertEqual(tracker.poll(), [{"kind": "map", "map": "c_intro"}])
        self.log.write_bytes(b'Started map "c_start"\r\n')
        self.assertEqual(tracker.poll(), [{"kind": "map", "map": "c_start"}])
        # Same byte count with the same file ID still indicates rewritten log.
        self.log.write_bytes(b'Started map "c_attic"\r\n')
        self.assertEqual(tracker.poll(), [{"kind": "map", "map": "c_attic"}])

    def test_start_at_end_ignores_history_and_handles_rotation(self):
        self.log.write_text('Started map "c_intro"\n')
        tracker = core.LogTracker(self.log, start_at_end=True)
        self.assertEqual(tracker.poll(), [])
        self.log.rename(self.log.with_suffix(".old"))
        self.log.write_text('Started map "c_start"\n')
        self.assertEqual(tracker.poll(), [{"kind": "map", "map": "c_start"}])


class CompleteInventoryTests(unittest.TestCase):
    @unittest.skipUnless((core.DEFAULT_GAME_DIR / "cryoffear" / "maps").is_dir(), "local map inventory is optional")
    def test_entire_local_inventory_agrees_with_real_changelevel_entities(self):
        catalog = core.load_catalog()
        installed = list_maps(core.DEFAULT_GAME_DIR / "cryoffear" / "maps")
        self.assertEqual(set(catalog), set(installed))
        self.assertEqual(len(catalog), 235)
        for name, path in installed.items():
            actual = []
            for entity in load_map_entities(path):
                target = entity.get("map", "").strip().lower()
                if (entity.get("classname") == "trigger_changelevel" and target in installed
                        and target != name and target not in actual):
                    actual.append(target)
            with self.subTest(map=name):
                self.assertEqual(core.next_maps(catalog, name), actual)
        reached, todo = set(), ["c_intro"]
        while todo:
            current = todo.pop()
            if current not in reached:
                reached.add(current)
                todo.extend(core.next_maps(catalog, current))
        self.assertEqual(len(reached), 81)
        for _, name in core.CHAPTERS:
            self.assertIn(name, installed)


if __name__ == "__main__":
    unittest.main()
