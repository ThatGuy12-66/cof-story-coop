from datetime import datetime
import hashlib
import io
import json
from pathlib import Path
import re
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
        archive.writestr("cof-enhanced/LICENSE", b"licence")
    (folder / friend.PAYLOAD_JSON).write_text(json.dumps({"schema": 2, "version": f"test{build}", "build": build,
                                                          "xash_sha256": hashlib.sha256(engine).hexdigest()}))
    return folder


class FakeInstaller:
    """Stands in for install.ps1 under subprocess.Popen: it puts files\\xash.dll in the game folder and writes
    gameinfo.txt, as the real one does, and prints what the real one printed in the 1.0 install."""
    output = (b"Cry of Fear: Enhanced 0.4.0-test3 - installer\r\nFolder: C:\\copy\r\n\r\n"
              b"Checking the 665 files in the download...\r\nInstalling 667 files...\r\n"
              b"  wrote cryoffear\\gameinfo.txt (made from your own game files)\r\n\r\n"
              b"Cry of Fear: Enhanced 0.4.0-test3 is installed.\r\n"
              b"Start Cry of Fear from Steam as usual. The version is shown in the top-right corner of the main menu.\r\n"
              b"To remove it again, run Uninstall.cmd in this folder.\r\n")
    code = 0

    def __init__(self, args, cwd, **kwargs):
        self.args, self.kwargs, self.returncode = args, kwargs, None
        (Path(cwd) / "xash.dll").write_bytes((Path(cwd) / "cof-enhanced/files/xash.dll").read_bytes())
        (Path(cwd) / "cryoffear").mkdir(exist_ok=True)
        (Path(cwd) / "cryoffear/gameinfo.txt").write_bytes(b'gamedir "cryoffear"\r\n')
        self.stdout = io.BytesIO(self.output)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.returncode = self.code


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

    def test_the_weapon_models_are_made_with_the_simon_models(self):
        with tempfile.TemporaryDirectory() as temp:
            models = Path(temp) / "cryoffear/models"
            for rel in friend.weapons.SOURCES:
                path = models.parent / rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(rel.encode())
            with mock.patch.object(friend, "build_models", return_value={"models/player/cof_s1/cof_s1.mdl": b"s"}), \
                 mock.patch.object(friend.weapons, "build_models", return_value={"models/coop/p_axe.mdl": b"a"}) as made, \
                 mock.patch.object(friend, "costume_files", return_value=[]):
                files = friend.generated_models(models)
            self.assertEqual(set(files), {"models/player/cof_s1/cof_s1.mdl", "models/coop/p_axe.mdl"})
            sources = made.call_args.args[0]
            self.assertEqual(set(sources), set(friend.weapons.SOURCES))
            self.assertEqual(sources["models/weapons/axe/w_axe.mdl"], b"models/weapons/axe/w_axe.mdl")

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
                 mock.patch.object(friend.subprocess, "Popen", FakeInstaller), \
                 mock.patch.object(friend, "make_models", return_value=remade) as make, \
                 mock.patch.object(friend, "make_merged_maps", return_value={"maps/cm_x.bsp": "3" * 64}) as merge, \
                 mock.patch.object(friend.art, "generate", return_value=[]) as art:
                friend.update(root, payload, exe=exe, shortcuts=False)
            self.assertEqual((root / "xash.dll").read_bytes(), b"new engine")
            art.assert_called_once_with(root.resolve())
            self.assertEqual(make.call_args.args, (root.resolve(),))
            self.assertEqual(merge.call_args.args, (root.resolve(),))
            self.assertEqual(friend.installed_state(root)["merged_maps"], {"maps/cm_x.bsp": "3" * 64})
            for rel, data in {"SAVE/cofcoop/autosave.cof": b"save", "cryoffear/config.cfg": b"my binds"}.items():
                self.assertEqual((root / rel).read_bytes(), data, rel)
            self.assertEqual((root / friend.APP_EXE).read_bytes(), b"new app")
            state = friend.installed_state(root)
            self.assertEqual((state["build"], state["models"]), (2, remade))
            self.assertIn("cof_test_gun 0", (root / "cryoffear/cof_story.cfg").read_text())

    def test_merged_maps_are_made_and_a_mismatched_group_is_skipped(self):
        from tools.map_merge import groups
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            maps = root / "cryoffear/maps"; maps.mkdir(parents=True)
            for stale in ("cm_old.bsp", "cm_old.sections"):
                (maps / stale).write_bytes(b"from an older build")
            (maps / "c_apartment1.bsp").write_bytes(b"retail")
            good = groups.Group("cm_apartment1", ["c_apartment1", "c_apartment2"], {}, "1" * 64, "2" * 64, 1, True)
            modded = groups.Group("cm_apartment3", ["c_apartment3", "c_apartment4"], {}, "3" * 64, "4" * 64, 1, True)
            off = groups.Group("cm_park", ["c_park", "c_park2"], {}, "5" * 64, "6" * 64, 2, False)

            def install(folder, group):
                if group is modded:
                    raise groups.GroupInputMismatch("cm_apartment3: c_apartment4.bsp is not the retail map")
                return {f"maps/{group.name}.bsp": "1" * 64, f"maps/{group.name}.sections": "2" * 64}

            with mock.patch.object(friend.merge_groups, "load_groups", return_value=[good, modded, off]), \
                 mock.patch.object(friend.merge_groups, "install_group", side_effect=install) as made:
                merged = friend.make_merged_maps(root)
            self.assertEqual([c.args[1].name for c in made.call_args_list], ["cm_apartment1", "cm_apartment3"])
            self.assertEqual(merged, {"maps/cm_apartment1.bsp": "1" * 64, "maps/cm_apartment1.sections": "2" * 64})
            self.assertFalse((maps / "cm_old.bsp").exists() or (maps / "cm_old.sections").exists())
            self.assertTrue((maps / "c_apartment1.bsp").exists())
            log = (root / friend.MERGE_LOG).read_text(encoding="utf-8")
            self.assertIn("cm_apartment1: merged", log)
            self.assertIn("cm_apartment3: not merged", log)

    def test_setup_makes_room_for_the_merged_maps(self):
        # a merged map is never larger than its members; a disabled group is not made
        from tools.map_merge import groups
        with tempfile.TemporaryDirectory() as temp:
            maps = Path(temp) / "cryoffear/maps"; maps.mkdir(parents=True)
            for name, size in (("c_apartment1", 10), ("c_apartment2", 20), ("c_park", 40)):
                (maps / f"{name}.bsp").write_bytes(b"x" * size)
            on = groups.Group("cm_apartment1", ["c_apartment1", "c_apartment2"], {}, "1" * 64, "2" * 64, 1, True)
            off = groups.Group("cm_park", ["c_park", "c_park2"], {}, "5" * 64, "6" * 64, 2, False)
            with mock.patch.object(friend.merge_groups, "load_groups", return_value=[on, off]):
                self.assertEqual(friend.merged_maps_room(Path(temp)), 30)

    def test_merged_maps_from_the_retail_maps(self):
        from tools.map_merge import groups
        retail_maps = Path(__file__).resolve().parents[1] / "baseline/Cry of Fear/cryoffear/maps"
        if not (retail_maps / "c_apartment1.bsp").is_file():
            self.skipTest("retail map fixtures are not installed")
        group = next(g for g in groups.load_groups() if g.name == "cm_apartment1")
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            maps = root / "cryoffear/maps"; maps.mkdir(parents=True)
            for member in group.members:
                (maps / f"{member}.bsp").write_bytes((retail_maps / f"{member}.bsp").read_bytes())
            with mock.patch.object(friend.merge_groups, "load_groups", return_value=[group]):
                merged = friend.make_merged_maps(root)
            self.assertEqual(merged, {"maps/cm_apartment1.bsp": group.bsp_sha256,
                                      "maps/cm_apartment1.sections": group.sections_sha256})

    def test_the_copy_gets_cry_of_fears_own_entity_limit(self):
        # liblist.gam asks GoldSrc for -num_edicts 4096; the engine reads max_edicts from gameinfo.txt only
        # (default 1200), and a merged map holds more than that
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            info = root / "cryoffear/gameinfo.txt"
            info.parent.mkdir(parents=True)
            info.write_bytes(b'gamedir "cryoffear"\r\nrender_picbutton_text 1\r\n')
            friend.ensure_entity_limit(root)
            self.assertEqual(info.read_bytes(), b'gamedir "cryoffear"\r\nrender_picbutton_text 1\r\nmax_edicts 4096\r\n')
            info.write_bytes(b'gamedir "cryoffear"\r\nmax_edicts 900\r\n')
            friend.ensure_entity_limit(root)
            self.assertEqual(info.read_bytes(), b'gamedir "cryoffear"\r\nmax_edicts 4096\r\n')
            friend.ensure_entity_limit(root)
            self.assertEqual(info.read_bytes().count(b"max_edicts"), 1)

    def test_runtime_check_covers_the_merged_maps(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "copy"
            (root / "cryoffear/maps").mkdir(parents=True)
            (root / "xash.dll").write_bytes(b"engine")
            (root / "cryoffear/maps/cm_apartment1.bsp").write_bytes(b"merged")
            (root / friend.INSTALLED).write_text(json.dumps({"schema": 2, "version": "test1", "build": 1,
                "xash_sha256": hashlib.sha256(b"engine").hexdigest(), "models": {}, "source_game": "src",
                "merged_maps": {"maps/cm_apartment1.bsp": hashlib.sha256(b"merged").hexdigest()}}))
            self.assertEqual(friend.validate_runtime(root), root.resolve())
            (root / "cryoffear/maps/cm_apartment1.bsp").write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "merged"):
                friend.validate_runtime(root)

    def test_update_removes_what_test_packages_left_in_the_copy(self):
        # the test packages put Enhanced's Install.cmd/Uninstall.cmd (its uninstaller strips the copy's engine),
        # its README (install into the Steam folder), the cheats guide and the tester notices into the copy
        retired = ("README.txt", "Install.cmd", "Uninstall.cmd", "EXPERIMENTAL-STORY.txt", "STORY-START.txt",
                   "cof-enhanced/CHEATS.md")
        with tempfile.TemporaryDirectory() as temp:
            root, exe = Path(temp) / "CoF-Story-Coop", Path(temp) / "new.exe"
            exe.write_bytes(b"new app")
            for rel in ("xash.dll", *retired, "cryoffear/README.txt", "SAVE/cofcoop/autosave.cof"):
                path = root / rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b"old")
            (root / friend.INSTALLED).write_text(json.dumps({"schema": 2, "version": "test2", "build": 3,
                "xash_sha256": hashlib.sha256(b"old").hexdigest(), "models": {}, "source_game": "src"}))
            payload = make_payload(Path(temp) / "payload", b"new engine", build=4)
            with mock.patch.object(friend, "game_is_running", return_value=False), \
                 mock.patch.object(friend.subprocess, "Popen", FakeInstaller), \
                 mock.patch.object(friend, "make_models", return_value={}), \
                 mock.patch.object(friend, "make_merged_maps", return_value={}), \
                 mock.patch.object(friend.art, "generate", return_value=[]):
                friend.update(root, payload, exe=exe, shortcuts=False)
            for rel in retired:
                self.assertFalse((root / rel).exists(), rel)
            for rel in ("cryoffear/README.txt", "SAVE/cofcoop/autosave.cof", "cof-enhanced/LICENSE"):
                self.assertTrue((root / rel).exists(), rel)

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


STAMP = re.compile(r"^\d\d:\d\d:\d\d  ")


def make_source(folder: Path) -> Path:
    """A retail game folder in miniature: what validate_source looks for, and two sounds."""
    for rel, data in {"CoFLaunchApp.exe": b"launcher", "cryoffear/liblist.gam": b"game",
                      "cryoffear/maps/c_game_menu1.bsp": b"menu", "cryoffear/cl_dlls/hl.dll": b"stock dll",
                      "cryoffear/sound/a.wav": b"a", "cryoffear/sound/b.wav": b"bb"}.items():
        path = folder / rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
    return folder


def made_copy(temp: Path, installer=FakeInstaller, seen=None):
    """friend.setup from make_source into a new copy, with the generators stood in for; returns the copy."""
    from tools.map_merge import groups
    group = groups.Group("cm_apartment1", ["c_apartment1", "c_apartment2"], {}, "1" * 64, "2" * 64, 1, True)
    source, copy, exe = make_source(temp / "retail"), temp / "CoF-Story-Coop", temp / "app.exe"
    exe.write_bytes(b"app")
    models = {"models/player/cof_s1/cof_s1.mdl": b"s1", "models/coop/p_axe.mdl": b"axe"}
    with mock.patch.object(friend, "STOCK_DLL_SHA", hashlib.sha256(b"stock dll").hexdigest()), \
         mock.patch.object(friend.subprocess, "Popen", installer), \
         mock.patch.object(friend, "generated_models", return_value=models), \
         mock.patch.object(friend, "validate_model"), \
         mock.patch.object(friend.art, "generate", side_effect=lambda root: [Path(root) / "cryoffear/gfx/vgui/inventory12_left.tga"]), \
         mock.patch.object(friend.merge_groups, "load_groups", return_value=[group]), \
         mock.patch.object(friend.merge_groups, "install_group", return_value={"maps/cm_apartment1.bsp": "1" * 64}):
        friend.setup(source, copy, make_payload(temp / "payload"), exe=exe, shortcuts=False,
                     progress=lambda *report: seen.append(report) if seen is not None else None)
    return copy


class SetupProgressTests(unittest.TestCase):
    """What the window shows while setup works: the step, a bar for the whole job and a line for each thing done,
    which also goes into the copy's setup log (the user, 2026-10-20: "so people can see what it's actually doing")."""

    def test_steps_report_the_step_the_whole_bar_and_each_line(self):
        seen, ticks = [], iter(range(60))
        steps = friend.Steps([("Checking your game", .2), ("Copying game files", .8)],
                             lambda status, part, line: seen.append((status, round(part, 3), line)),
                             clock=lambda: datetime(2026, 10, 20, 5, 0, next(ticks)))
        steps.start("Checking your game")
        steps.note("6 files to copy")
        steps.start("Copying game files")
        steps.part(.5, "2 of 4 MB")
        steps.part(2.0)   # never past its own share
        steps.done("Done.")
        self.assertEqual(seen, [
            ("Step 1 of 2: Checking your game", 0.0, "05:00:00  Step 1 of 2: Checking your game"),
            ("Step 1 of 2: Checking your game", 0.0, "05:00:01  6 files to copy"),
            ("Step 2 of 2: Copying game files", 0.2, "05:00:02  Step 2 of 2: Copying game files"),
            ("Step 2 of 2: Copying game files, 2 of 4 MB", 0.6, None),
            ("Step 2 of 2: Copying game files", 1.0, None),
            ("Done.", 1.0, "05:00:03  Done."),
        ])

    def test_the_setup_log_keeps_the_lines_from_before_the_copy_existed(self):
        with tempfile.TemporaryDirectory() as temp:
            steps = friend.Steps([("Checking your game", 1)])
            steps.start("Checking your game")
            steps.note("6 files to copy")
            log = Path(temp) / friend.SETUP_LOG
            steps.log_to(log)
            steps.note("after")
            lines = log.read_text(encoding="utf-8").splitlines()
        self.assertTrue(all(STAMP.match(line) for line in lines))
        self.assertEqual([STAMP.sub("", line) for line in lines], ["Step 1 of 1: Checking your game", "6 files to copy", "after"])

    def test_setup_shows_each_step_and_what_it_does(self):
        seen = []
        with tempfile.TemporaryDirectory() as temp:
            copy = made_copy(Path(temp), seen=seen)
            log = (copy / friend.SETUP_LOG).read_text(encoding="utf-8").splitlines()
        statuses = [status for status, _, _ in seen]
        parts = [part for _, part, _ in seen]
        lines = [line for _, _, line in seen if line]
        heads = [status.split(":")[0] for status in statuses if status.startswith("Step ")]
        self.assertEqual(list(dict.fromkeys(heads)), [f"Step {n} of 6" for n in range(1, 7)])
        self.assertEqual(parts, sorted(parts))   # the bar only moves forward
        self.assertEqual((parts[-1], statuses[-1].split(".")[0]), (1, "Done"))
        self.assertTrue(any(s.startswith("Step 2 of 6: Copying game files, ") and "cryoffear/sound/" in s for s in statuses))
        self.assertIn("Step 6 of 6: Joining story maps (fewer loading screens), 1 of 1: cm_apartment1", statuses)
        text = [STAMP.sub("", line) for line in lines]
        for expected in ("Copied cryoffear/sound: 2 files, 1 KB", "Installing 667 files...",
                         "wrote cryoffear\\gameinfo.txt (made from your own game files)",
                         "Made models/player/cof_s1/cof_s1.mdl", "Made cryoffear/gfx/vgui/inventory12_left.tga",
                         "1 of 1: cm_apartment1: merged c_apartment1 + c_apartment2"):
            self.assertIn(expected, text)
        # upstream's words for an install into the Steam folder do not fit a copy
        self.assertFalse([line for line in text if "Uninstall.cmd" in line or "from Steam" in line])
        self.assertTrue(all(STAMP.match(line) for line in lines))
        self.assertEqual(log, lines)   # the copy's setup log: every line the window showed, the first step's too

    def test_a_stopped_setup_says_why_in_its_log(self):
        class Failing(FakeInstaller):
            code = 1
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(RuntimeError, "engine could not be installed"):
                made_copy(Path(temp), installer=Failing)
            log = (Path(temp) / "CoF-Story-Coop" / friend.SETUP_LOG).read_text(encoding="utf-8")
        self.assertIn("Stopped: The engine could not be installed.", log)

    def test_update_shows_its_steps_and_adds_to_the_setup_log(self):
        seen = []
        with tempfile.TemporaryDirectory() as temp:
            root, exe = Path(temp) / "CoF-Story-Coop", Path(temp) / "new.exe"
            exe.write_bytes(b"new app")
            root.mkdir()
            (root / "xash.dll").write_bytes(b"old")
            (root / friend.SETUP_LOG).write_text("05:00:00  the first setup\n", encoding="utf-8")
            (root / friend.INSTALLED).write_text(json.dumps({"schema": 2, "version": "1.0", "build": 4,
                "xash_sha256": hashlib.sha256(b"old").hexdigest(), "models": {}, "source_game": "src"}))
            with mock.patch.object(friend, "game_is_running", return_value=False), \
                 mock.patch.object(friend.subprocess, "Popen", FakeInstaller), \
                 mock.patch.object(friend, "make_models", return_value={}), \
                 mock.patch.object(friend, "make_merged_maps", return_value={}), \
                 mock.patch.object(friend.art, "generate", return_value=[]):
                friend.update(root, make_payload(Path(temp) / "payload", b"new engine", build=5), exe=exe,
                              progress=lambda *report: seen.append(report), shortcuts=False)
            log = (root / friend.SETUP_LOG).read_text(encoding="utf-8").splitlines()
        heads = [status.split(":")[0] for status, _, _ in seen if status.startswith("Step ")]
        self.assertEqual(list(dict.fromkeys(heads)), [f"Step {n} of 4" for n in range(1, 5)])
        self.assertEqual(seen[-1][1], 1)
        self.assertEqual(log[0], "05:00:00  the first setup")
        self.assertIn("Installing 667 files...", [STAMP.sub("", line) for line in log])


if __name__ == "__main__":
    unittest.main()
