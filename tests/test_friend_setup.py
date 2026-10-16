import hashlib
import json
from pathlib import Path
import tempfile
import subprocess
import unittest
from unittest import mock
import zipfile

from tools import friend_setup as friend


def make_payload(folder: Path, engine=b"engine", build=1):
    """The payload the app carries: the Enhanced player zip and its manifest."""
    folder.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(folder / friend.PAYLOAD_ZIP, "w") as archive:
        archive.writestr("cof-enhanced/files/xash.dll", engine)
        archive.writestr("cof-enhanced/install.ps1", b"# installer")
        archive.writestr("README.txt", b"readme")
    (folder / friend.PAYLOAD_JSON).write_text(json.dumps({"schema": 2, "version": f"test{build}", "build": build,
                                                          "xash_sha256": hashlib.sha256(engine).hexdigest()}))
    return folder


def fake_installer(args, cwd, **kwargs):
    """Stands in for install.ps1: it puts files\\xash.dll in the game folder, as the real one does."""
    (Path(cwd) / "xash.dll").write_bytes((Path(cwd) / "cof-enhanced/files/xash.dll").read_bytes())
    return subprocess.CompletedProcess(args, 0)


class FriendSetupTests(unittest.TestCase):
    def test_only_a_game_from_this_copy_counts_as_running(self):
        root = Path("C:/Games/CoF-Story-Coop")
        for path, expected in ((root / "CoFLaunchApp.exe", True), (Path("C:/Games/CoF-Story-Coop2/cof.exe"), False),
                               (None, False)):
            rows = [{"ProcessId": 1, "Name": "CoFLaunchApp.exe", "ExecutablePath": str(path) if path else None}]
            reply = subprocess.CompletedProcess([], 0, json.dumps(rows), "")
            with self.subTest(path=path), mock.patch.object(friend.subprocess, "run", return_value=reply) as run:
                self.assertEqual(friend.game_is_running(root), expected)
                self.assertIn("Name='cof.exe' OR Name='CoFLaunchApp.exe'", run.call_args.args[0][-1])
        reply = subprocess.CompletedProcess([], 0, "", "")
        with mock.patch.object(friend.subprocess, "run", return_value=reply):
            self.assertFalse(friend.game_is_running(root))

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

    def test_enter_skips_cutscenes_for_the_party(self):
        controls = friend.default_controls()
        self.assertIn('bind "ENTER" "cof_skip"', controls)
        self.assertIn('bind "KP_ENTER" "cof_skip"', controls)

    def test_controls_bind_the_coop_keys(self):
        controls = friend.default_controls()
        for line in ('bind "j" "cof_warp"', 'bind "h" "cof_companion_order"', 'bind "ENTER" "cof_skip"'):
            self.assertIn(line, controls)

    def test_each_game_keeps_the_previous_log(self):
        # The engine opens -log files with "w+": without moving the last one aside,
        # starting again after a crash would erase the log a bug report needs.
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.assertIsNone(friend.keep_previous_log(root, "story-session.log"))
            for text in ("first", "second"):
                (root / "story-session.log").write_text(text)
                kept = friend.keep_previous_log(root, "story-session.log")
                self.assertFalse((root / "story-session.log").exists())
                self.assertEqual((kept.parent, kept.read_text()), (root / "story-logs", text))
                self.assertTrue(kept.name.startswith("story-session-"))
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

    def test_payload_identity_is_checked(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = make_payload(Path(temp) / "payload")
            self.assertEqual(friend.inspect_payload(folder)["build"], 1)
            manifest = json.loads((folder / friend.PAYLOAD_JSON).read_text())
            manifest["xash_sha256"] = "0" * 64
            (folder / friend.PAYLOAD_JSON).write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "damaged"):
                friend.inspect_payload(folder)
            (folder / friend.PAYLOAD_JSON).unlink()
            with self.assertRaisesRegex(ValueError, "incomplete"):
                friend.inspect_payload(folder)

    def test_models_are_remade_from_the_copys_own_files(self):
        # An update must replace models made by an older generator (the aim-pitch fix), not keep them.
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            old = root / "cryoffear/models/player/cof_s1/cof_s1.mdl"
            old.parent.mkdir(parents=True); old.write_bytes(b"old model")
            made = {"models/player/cof_s1/cof_s1.mdl": b"new model", "models/coop/p_axe.mdl": b"axe",
                    "model-report.json": b"{}", "previews/standard-jeans.png": b"png"}
            with mock.patch.object(friend, "generated_models", return_value=made) as generate, \
                 mock.patch.object(friend, "validate_model") as validate:
                hashes = friend.make_models(root)
            generate.assert_called_once_with(root / "cryoffear/models")
            self.assertEqual(validate.call_count, 2)
            self.assertEqual(old.read_bytes(), b"new model")
            self.assertEqual((root / "cryoffear/models/coop/p_axe.mdl").read_bytes(), b"axe")
            self.assertFalse((root / "cryoffear/model-report.json").exists())
            self.assertEqual(hashes, {rel: hashlib.sha256(made[rel]).hexdigest()
                                      for rel in ("models/player/cof_s1/cof_s1.mdl", "models/coop/p_axe.mdl")})

    def test_update_lays_the_new_engine_over_and_keeps_saves_settings_models(self):
        with tempfile.TemporaryDirectory() as temp:
            root, exe = Path(temp) / "CoF-Story-Coop", Path(temp) / "new.exe"
            exe.write_bytes(b"new app")
            for rel, data in {"xash.dll": b"old engine", "SAVE/cofcoop/autosave.cof": b"save",
                              "cryoffear/config.cfg": b"my binds", "cryoffear/models/player/cof_s1/cof_s1.mdl": b"model",
                              "cof-story-coop-settings.json": b"{}"}.items():
                path = root / rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
            models = {"models/player/cof_s1/cof_s1.mdl": hashlib.sha256(b"model").hexdigest()}
            (root / friend.INSTALLED).write_text(json.dumps({"schema": 2, "version": "test1", "build": 1,
                "xash_sha256": hashlib.sha256(b"old engine").hexdigest(), "models": models, "source_game": "src"}))
            payload = make_payload(Path(temp) / "payload", b"new engine", build=2)
            remade = {"models/player/cof_s1/cof_s1.mdl": "1" * 64, "models/coop/p_axe.mdl": "2" * 64}
            with mock.patch.object(friend, "game_is_running", return_value=False), \
                 mock.patch.object(friend.subprocess, "run", side_effect=fake_installer), \
                 mock.patch.object(friend, "make_models", return_value=remade) as make, \
                 mock.patch.object(friend.art, "generate", return_value=[]) as art:
                friend.update(root, payload, exe=exe, shortcuts=False)
            self.assertEqual((root / "xash.dll").read_bytes(), b"new engine")
            art.assert_called_once_with(root.resolve())
            make.assert_called_once_with(root.resolve())
            for rel, data in {"SAVE/cofcoop/autosave.cof": b"save", "cryoffear/config.cfg": b"my binds"}.items():
                self.assertEqual((root / rel).read_bytes(), data, rel)
            self.assertEqual((root / friend.APP_EXE).read_bytes(), b"new app")
            state = friend.installed_state(root)
            self.assertEqual((state["build"], state["models"]), (2, remade))
            self.assertIn("cof_test_gun 0", (root / "cryoffear/cof_story.cfg").read_text())

    def test_update_refuses_while_the_game_runs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "copy"; root.mkdir()
            (root / friend.INSTALLED).write_text(json.dumps({"schema": 2, "version": "test1", "build": 1,
                "xash_sha256": "0" * 64, "models": {}, "source_game": "src"}))
            (root / "xash.dll").write_bytes(b"old")
            with mock.patch.object(friend, "game_is_running", return_value=True), self.assertRaisesRegex(ValueError, "Close"):
                friend.update(root, make_payload(Path(temp) / "payload"), exe=root / "x.exe", shortcuts=False)
            self.assertEqual((root / "xash.dll").read_bytes(), b"old")

    def test_installed_state_needs_this_apps_schema(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.assertIsNone(friend.installed_state(root))
            (root / friend.INSTALLED).write_text(json.dumps({"schema": 1, "protocol": 49}))
            self.assertIsNone(friend.installed_state(root))

    def test_shortcuts_pass_paths_through_the_environment(self):
        with mock.patch.object(friend.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
            self.assertTrue(friend.write_shortcuts(Path("C:/Games/CoF-Story-Coop")))
        args, kwargs = run.call_args
        self.assertNotIn("CoF-Story-Coop", " ".join(args[0]))
        self.assertEqual(kwargs["env"]["COF_APP_EXE"], str(Path("C:/Games/CoF-Story-Coop") / friend.APP_EXE))
        self.assertIn("Cry of Fear Co-op.lnk", args[0][-1])

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
