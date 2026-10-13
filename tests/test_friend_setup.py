import hashlib
import json
from pathlib import Path
import tempfile
import subprocess
import unittest
from unittest import mock
import zipfile

from tools import friend_setup as friend


class FriendSetupTests(unittest.TestCase):
    def test_existing_retail_or_enhanced_game_blocks_a_second_client(self):
        for name in ("cof.exe", "CoFLaunchApp.exe"):
            reply = subprocess.CompletedProcess([], 0, json.dumps({"ProcessId": 123, "Name": name}), "")
            with self.subTest(name=name), mock.patch.object(friend.subprocess, "run", return_value=reply) as run:
                self.assertTrue(friend.game_is_running(Path("separate")))
                self.assertIn("Name='cof.exe' OR Name='CoFLaunchApp.exe'", run.call_args.args[0][-1])
        reply = subprocess.CompletedProcess([], 0, "", "")
        with mock.patch.object(friend.subprocess, "run", return_value=reply):
            self.assertFalse(friend.game_is_running(Path("separate")))

    def test_copy_excludes_personal_state_and_overrides_but_keeps_campaign_cfg(self):
        with tempfile.TemporaryDirectory() as temp:
            source, destination = Path(temp) / "retail", Path(temp) / "separate"
            source.mkdir(); destination.mkdir()
            files = {"cryoffear/maps/c_start.bsp": b"map", "cryoffear/maps/c_cof_city.cfg": b"campaign",
                     "cryoffear/skill.cfg": b"stock gameplay balance",
                     "cryoffear/Save/slot.sav": b"save", "cryoffear/config.cfg": b"private settings",
                     "cryoffear/userconfig.cfg.cofcoop-backup-1": b"private password",
                     "cryoffear/maps/c_start.ent": b"fixture", "qconsole.log": b"private RCON",
                     "state/settings.json": b"secret", "cryoffear/models/player/cof_s2/cof_s2.mdl": b"old"}
            for rel, data in files.items():
                path = source / rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
            plan = friend.copy_plan(source)
            self.assertEqual({rel.as_posix() for rel, _ in plan}, {"cryoffear/maps/c_start.bsp", "cryoffear/maps/c_cof_city.cfg", "cryoffear/skill.cfg"})
            friend.copy_assets(source, destination, plan)
            for rel, _ in plan:
                self.assertEqual((destination / rel).read_bytes(), files[rel.as_posix()])
            for rel, data in files.items():
                self.assertEqual((source / rel).read_bytes(), data)

    def test_fresh_destination_stays_outside_source_and_steam(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "original"; source.mkdir()
            for dest in (source, source / "copy", Path(temp), Path(temp) / "steamapps/common/new"):
                with self.subTest(dest=dest), self.assertRaises(ValueError):
                    friend.validate_destination(source, dest)
            self.assertEqual(friend.validate_destination(source, Path(temp) / "new"), (Path(temp) / "new").resolve())

    def test_source_requires_exact_stock_dll_without_modifying_it(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for rel in ("CoFLaunchApp.exe", "cryoffear/liblist.gam", "cryoffear/maps/c_game_menu1.bsp", "cryoffear/cl_dlls/hl.dll"):
                path = root / rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b"fixture")
            with self.assertRaisesRegex(ValueError, "differs"):
                friend.validate_source(root)
            with mock.patch.object(friend, "STOCK_DLL_SHA", hashlib.sha256(b"fixture").hexdigest()):
                self.assertEqual(friend.validate_source(root), root.resolve())
            self.assertEqual((root / "cryoffear/cl_dlls/hl.dll").read_bytes(), b"fixture")

    def test_join_validation_blocks_console_injection_and_password_not_in_argv(self):
        args = friend.join_arguments(Path("game"), "example.com:27015")
        self.assertEqual(args[-1], "example.com:27015")
        self.assertIn("-windowed", args)
        config = friend.join_config("Tester", "private-value")
        self.assertIn('name "Tester"', config)
        self.assertIn('password "private-value"', config)
        self.assertIn("fps_max 60", config)
        self.assertNotIn("private-value", args)
        for bad in ('a";quit', "a\nquit", "a\\b", "x" * 65):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                friend.join_config(password=bad)
        for bad in ("1.2.3.999", "example.com;quit", "127.0.0.1:0", "host:70000", "host\nquit"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                friend.join_arguments(Path("game"), bad)

    def test_blank_player_name_is_refused_and_setup_default_is_generic(self):
        # The join window starts with an empty name field; joining needs a typed name.
        for blank in ("", "   "):
            with self.subTest(blank=blank), self.assertRaisesRegex(ValueError, "Enter a player name"):
                friend.join_config(blank, "private-value")
        # Setup writes its first config before anyone has typed a name.
        self.assertEqual(friend.DEFAULT_NAME, "Player")
        self.assertIn('name "Player"\n', friend.join_config())

    def test_enter_skips_cutscenes_for_the_party(self):
        controls = friend.default_controls()
        self.assertIn('bind "ENTER" "cof_skip"', controls)
        self.assertIn('bind "KP_ENTER" "cof_skip"', controls)

    def test_join_writes_a_timestamped_log_and_keeps_the_previous_one(self):
        # The engine opens -log files with "w+": without moving the last one aside,
        # rejoining after a crash would erase the log a bug report needs.
        args = friend.join_arguments(Path("game"), "example.com:27015")
        self.assertEqual(args[args.index("-log") + 1], friend.TESTER_LOG)
        self.assertIn("-logtime", args)
        self.assertLess(args.index("-log"), args.index("+exec"))
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.assertIsNone(friend.keep_previous_log(root))
            for text in ("first session", "second session"):
                (root / friend.TESTER_LOG).write_text(text, encoding="ascii")
                kept = friend.keep_previous_log(root)
                self.assertFalse((root / friend.TESTER_LOG).exists())
                self.assertEqual(kept.parent, root / "story-logs")
                self.assertEqual(kept.read_text(encoding="ascii"), text)
            self.assertEqual(len(list((root / "story-logs").iterdir())), 2)

    def test_archive_traversal_duplicate_and_link_entries_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "test.zip"
            for name in ("../escape", "/root", "C:/escape", "a\\b"):
                with zipfile.ZipFile(path, "w") as archive:
                    info = zipfile.ZipInfo()
                    info.filename = name
                    archive.writestr(info, b"bad")
                with zipfile.ZipFile(path) as archive, self.assertRaises(ValueError):
                    list(friend.safe_zip_members(archive))
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("file", b"a"); archive.writestr("FILE", b"b")
            with zipfile.ZipFile(path) as archive, self.assertRaises(ValueError):
                list(friend.safe_zip_members(archive))
            with zipfile.ZipFile(path, "w") as archive:
                info = zipfile.ZipInfo("link")
                info.create_system = 3
                info.external_attr = 0o120777 << 16
                archive.writestr(info, b"target")
            with zipfile.ZipFile(path) as archive, self.assertRaises(ValueError):
                list(friend.safe_zip_members(archive))

    def test_bundle_hashes_and_engine_identity_required(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with zipfile.ZipFile(root / "engine-payload.zip", "w") as archive:
                archive.writestr("cof-enhanced/files/xash.dll", b"engine")
            (root / "corresponding-source.zip").write_bytes(b"source")
            manifest = {"schema": 1, "protocol": 49, "xash_sha256": hashlib.sha256(b"engine").hexdigest(),
                        "sha256": {name: friend.digest_file(root / name) for name in ("engine-payload.zip", "corresponding-source.zip")}}
            (root / "bundle.json").write_text(json.dumps(manifest))
            self.assertEqual(friend.inspect_bundle(root), manifest)
            (root / "engine-payload.zip").write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "hash mismatch"):
                friend.inspect_bundle(root)

    def test_steam_discovery_reads_metadata_not_a_disk_inventory(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "Steam"; library = Path(temp) / "Library"
            (root / "steamapps").mkdir(parents=True); (library / "steamapps").mkdir(parents=True)
            escaped = str(library).replace("\\", "\\\\")
            (root / "steamapps/libraryfolders.vdf").write_text(f'"libraryfolders" {{ "1" {{ "path" "{escaped}" }} }}')
            (library / "steamapps/appmanifest_223710.acf").write_text('"AppState" { "installdir" "Cry of Fear" }')
            self.assertEqual(friend.steam_candidates([root]), [library / "steamapps/common/Cry of Fear"])


if __name__ == "__main__":
    unittest.main()
