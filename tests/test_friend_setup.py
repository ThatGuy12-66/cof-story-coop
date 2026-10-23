from datetime import datetime
import hashlib
import io
import json
import os
from pathlib import Path
import re
import tempfile
import subprocess
import types
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


# game_is_running asks Windows only on Windows: this stands in for friend_setup's os there (paths stay this OS's)
WINDOWS = types.SimpleNamespace(name="nt", environ=os.environ, sep=os.sep)


class FriendSetupTests(unittest.TestCase):
    def test_only_a_game_from_this_copy_counts_as_running(self):
        root = Path("C:/Games/CoF-Story-Coop")
        for path, expected in ((root / "CoFLaunchApp.exe", True), (Path("C:/Games/CoF-Story-Coop2/cof.exe"), False),
                               (None, False)):
            rows = [{"ProcessId": 1, "Name": "CoFLaunchApp.exe", "ExecutablePath": str(path) if path else None}]
            reply = subprocess.CompletedProcess([], 0, json.dumps(rows), "")
            with self.subTest(path=path), mock.patch.object(friend, "os", WINDOWS), \
                    mock.patch.object(friend.subprocess, "run", return_value=reply) as run:
                self.assertEqual(friend.game_is_running(root), expected)
                self.assertIn("Name='cof.exe' OR Name='CoFLaunchApp.exe'", run.call_args.args[0][-1])
        reply = subprocess.CompletedProcess([], 0, "", "")
        with mock.patch.object(friend, "os", WINDOWS), mock.patch.object(friend.subprocess, "run", return_value=reply):
            self.assertFalse(friend.game_is_running(root))

    def test_without_an_answer_from_powershell_tasklist_answers(self):
        # 1.1.8 (L3): a PowerShell that fails or takes over 20 s made Start, Join and Update do nothing at all
        root = Path("C:/Games/CoF-Story-Coop")
        listing = b'"System Idle Process","0","Services","0","8 K"\r\n"CoFLaunchApp.exe","4242","Console","1","90 K"\r\n'
        for failure in (subprocess.TimeoutExpired(["powershell"], 20), subprocess.CalledProcessError(1, ["powershell"]),
                        OSError("blocked"), subprocess.CompletedProcess([], 0, "{not json", "")):
            for tasks, expected in ((listing, True), (b'"explorer.exe","9","Console","1","9 K"\r\n', False)):
                def run(command, **kwargs):
                    if Path(command[0]).name == "tasklist.exe":
                        return subprocess.CompletedProcess(command, 0, tasks, b"")
                    if isinstance(failure, subprocess.CompletedProcess):
                        return failure
                    raise failure
                with self.subTest(failure=type(failure).__name__, expected=expected), \
                        mock.patch.object(friend, "os", WINDOWS), mock.patch.object(friend.subprocess, "run", run):
                    self.assertIs(friend.game_is_running(root), expected)

    def test_when_windows_cannot_say_the_answer_is_unknown(self):
        def run(command, **kwargs):
            raise subprocess.TimeoutExpired(command, 20)
        with mock.patch.object(friend, "os", WINDOWS), mock.patch.object(friend.subprocess, "run", run):
            self.assertIs(friend.game_is_running(Path("C:/copy")), False)   # Start, Join and Update go ahead
            self.assertIs(friend.game_is_running(Path("C:/copy"), unknown=True), True)   # hosting keeps the game

    def test_copy_excludes_personal_state_and_overrides_but_keeps_campaign_cfg(self):
        with tempfile.TemporaryDirectory() as temp:
            source, destination = Path(temp) / "retail", Path(temp) / "separate"
            source.mkdir(); destination.mkdir()
            files = {"cryoffear/maps/c_start.bsp": b"map", "cryoffear/maps/c_cof_city.cfg": b"campaign",
                     "cryoffear/skill.cfg": b"stock gameplay balance",
                     "cryoffear/Save/slot.sav": b"save", "cryoffear/config.cfg": b"private settings",
                     "cryoffear/sound/save/save_yes.wav": b"the game's save sound", "SAVE/slot.hl1": b"save",
                     "cryoffear/SAVE/cofcoop/latest.txt": b"save", "logs/today.txt": b"log",
                     "cryoffear/userconfig.cfg.cofcoop-backup-1": b"private password",
                     "cryoffear/maps/c_start.ent": b"fixture", "qconsole.log": b"private RCON",
                     "state/settings.json": b"secret", "cryoffear/models/player/cof_s2/cof_s2.mdl": b"old"}
            for rel, data in files.items():
                path = source / rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
            plan = friend.copy_plan(source)
            self.assertEqual({rel.as_posix() for rel, _ in plan}, {"cryoffear/maps/c_start.bsp", "cryoffear/maps/c_cof_city.cfg", "cryoffear/skill.cfg",
                                                                 "cryoffear/sound/save/save_yes.wav"})
            friend.copy_assets(source, destination, plan)
            for rel, _ in plan:
                self.assertEqual((destination / rel).read_bytes(), files[rel.as_posix()])
            for rel, data in files.items():
                self.assertEqual((source / rel).read_bytes(), data)

    def test_an_update_adds_the_save_sounds_an_older_setup_left_out(self):
        with tempfile.TemporaryDirectory() as temp:
            source, root = Path(temp) / "retail", Path(temp) / "copy"
            for rel, data in {"cryoffear/sound/save/save_yes.wav": b"yes", "cryoffear/sound/save/save_no.wav": b"no",
                              "cryoffear/SAVE/slot.sav": b"their save", "cryoffear/maps/c_start.bsp": b"map"}.items():
                path = source / rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
            (root / "cryoffear/sound/save").mkdir(parents=True)
            (root / "cryoffear/sound/save/save_no.wav").write_bytes(b"kept")
            notes = []
            steps = mock.Mock(note=notes.append)
            self.assertEqual(friend.restore_left_out(root, str(source), steps), 1)
            self.assertEqual((root / "cryoffear/sound/save/save_yes.wav").read_bytes(), b"yes")
            self.assertEqual((root / "cryoffear/sound/save/save_no.wav").read_bytes(), b"kept")
            self.assertFalse((root / "cryoffear/SAVE").exists())
            self.assertFalse((root / "cryoffear/maps").exists())
            self.assertEqual(friend.restore_left_out(root, str(Path(temp) / "gone"), steps), 0)
            self.assertIn("left out", notes[0])

    def test_fresh_destination_stays_outside_source_and_steam(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "original"; source.mkdir()
            for dest in (source, source / "copy", Path(temp), Path(temp) / "steamapps/common/new"):
                with self.subTest(dest=dest), self.assertRaises(ValueError):
                    friend.validate_destination(source, dest)
            self.assertEqual(friend.validate_destination(source, Path(temp) / "new"), (Path(temp) / "new").resolve())

    @unittest.skipUnless(hasattr(os, "symlink"), "links")
    def test_a_game_reached_through_a_link_is_its_real_folder(self):
        # 1.1.9 (L14.3): a Steam library moved with a junction (the owner's own PC) was refused; links inside the
        # game, and on the way to a destination, still are
        with tempfile.TemporaryDirectory() as temp:
            real, other = Path(temp) / "D" / "Cry of Fear", Path(temp) / "other.dll"
            for rel in ("CoFLaunchApp.exe", "cryoffear/liblist.gam", "cryoffear/maps/c_game_menu1.bsp", "cryoffear/cl_dlls/hl.dll"):
                path = real / rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b"fixture")
            other.write_bytes(b"fixture")
            (Path(temp) / "elsewhere").mkdir()
            try:
                os.symlink(real, Path(temp) / "via-link", target_is_directory=True)
                os.symlink(Path(temp) / "elsewhere", Path(temp) / "dest-link", target_is_directory=True)
            except OSError:
                self.skipTest("no symlinks here")
            with mock.patch.object(friend, "STOCK_DLL_SHA", hashlib.sha256(b"fixture").hexdigest()):
                self.assertEqual(friend.validate_source(Path(temp) / "via-link"), real.resolve())
                self.assertEqual(sorted(rel.as_posix() for rel, _ in friend.copy_plan(friend.validate_source(Path(temp) / "via-link"))),
                                 ["CoFLaunchApp.exe", "cryoffear/cl_dlls/hl.dll", "cryoffear/liblist.gam",
                                  "cryoffear/maps/c_game_menu1.bsp"])
                (real / "cryoffear/cl_dlls/hl.dll").unlink()
                os.symlink(other, real / "cryoffear/cl_dlls/hl.dll")
                for given in (real, Path(temp) / "via-link"):
                    with self.subTest(given=given), self.assertRaisesRegex(ValueError, "without links"):
                        friend.validate_source(given)
            with self.assertRaisesRegex(ValueError, "without links"):
                friend.validate_destination(real.resolve(), Path(temp) / "dest-link" / "CoF-Story-Coop")

    def test_program_files_and_onedrive_destinations_are_refused(self):
        # 1.1.9 (L14.8): Program Files fails the writes of a player who is not an administrator, OneDrive uploads
        # the 5 GB copy and can keep its files online only
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "original"; source.mkdir()
            programs, onedrive = Path(temp) / "Program Files (x86)", Path(temp) / "OneDrive - Home"
            env = {"ProgramFiles(x86)": str(programs), "OneDrive": str(onedrive), "ProgramW6432": "not absolute"}
            with mock.patch.dict(os.environ, env):
                for dest, words in ((programs / "CoF-Story-Coop", "outside Program Files"),
                                    (onedrive / "Games" / "CoF-Story-Coop", "outside OneDrive")):
                    with self.subTest(dest=dest), self.assertRaisesRegex(ValueError, words):
                        friend.validate_destination(source, dest)
                fine = Path(temp) / "Games" / "CoF-Story-Coop"
                self.assertEqual(friend.validate_destination(source, fine), fine.resolve())
                # a folder that only starts with the same letters is not inside it
                beside = Path(temp) / "OneDrive - Home backup" / "CoF-Story-Coop"
                self.assertEqual(friend.validate_destination(source, beside), beside.resolve())

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
        for line in ('bind "j" "cof_warp"', 'bind "h" "cof_companion_order"', 'bind "ENTER" "cof_skip"',
                     'bind "o" "cof_wardrobe"'):
            self.assertIn(line, controls)

    def test_the_wardrobe_key_is_one_the_controls_leave_free(self):
        # after 1.1.9: O opens the wardrobe; K (the candidate) is the voice key, and no other key had O
        binds = re.findall(r'bind "([^"]+)" "([^"]+)"', friend.default_controls())
        self.assertEqual([key for key, command in binds if command == "cof_wardrobe"], ["o"])
        self.assertEqual([command for key, command in binds if key == "o"], ["cof_wardrobe"])
        self.assertIn(("k", "+voicerecord"), binds)
        self.assertEqual(len({key for key, _ in binds}), len(binds))
        self.assertIn(("o", "cof_wardrobe"), friend.COOP_KEYS)
        self.assertIn(("cof_wardrobe", "Wardrobe (costumes)"), friend.COOP_ACTIONS)

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
            for rel in list(friend.weapons.SOURCES) + list(friend.weapons.OPTIONAL_SOURCES):
                path = models.parent / rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(rel.encode())
            with mock.patch.object(friend, "build_models", return_value={"models/player/cof_s1/cof_s1.mdl": b"s"}), \
                 mock.patch.object(friend.weapons, "build_models", return_value={"models/coop/p_axe.mdl": b"a"}) as made, \
                 mock.patch.object(friend, "costume_files", return_value=[]):
                files = friend.generated_models(models)
            self.assertEqual(set(files), {"models/player/cof_s1/cof_s1.mdl", "models/coop/p_axe.mdl"})
            sources = made.call_args.args[0]
            # 1.1.9: and the optional ones the copy has (build_models leaves a model out when its file is not stock)
            self.assertEqual(set(sources), set(friend.weapons.SOURCES) | set(friend.weapons.OPTIONAL_SOURCES))
            self.assertEqual(sources["models/weapons/axe/w_axe.mdl"], b"models/weapons/axe/w_axe.mdl")
            (models.parent / friend.weapons.GASMASK_SOURCE).unlink()
            with mock.patch.object(friend, "build_models", return_value={}), \
                 mock.patch.object(friend.weapons, "build_models", return_value={}) as made, \
                 mock.patch.object(friend, "costume_files", return_value=[]):
                friend.generated_models(models)
            self.assertEqual(set(made.call_args.args[0]), set(friend.weapons.SOURCES))  # missing: not refused

    def test_a_left_out_cosmetic_model_is_one_line_in_the_log(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = {"outputs": {}, "skipped": [{"path": "models/coop/h_gasmask.mdl",
                                                    "reason": "models/items/w_nightvision.mdl is not the stock file"}]}
            made = {"models/coop/p_axe.mdl": b"axe", "coop-weapons.json": json.dumps(manifest).encode()}
            steps = friend.Steps(())
            with mock.patch.object(friend, "generated_models", return_value=made), mock.patch.object(friend, "validate_model"):
                hashes = friend.make_models(root, steps=steps)
            self.assertEqual(set(hashes), {"models/coop/p_axe.mdl"})
            self.assertFalse((root / "cryoffear/models/coop/h_gasmask.mdl").exists())
            left_out = [line for line in steps.kept if "Left out" in line]
            self.assertEqual(len(left_out), 1)
            self.assertIn("models/coop/h_gasmask.mdl (cosmetic): models/items/w_nightvision.mdl is not the stock file", left_out[0])

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
            (root / "CoFLaunchApp.exe").write_bytes(b"starter")
            (root / "cryoffear/gameinfo.txt").write_text("max_edicts 4096\n")   # 1.1.8: checked too (L1)
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


STOCK_MODELS = {"models/cutscene/player.mdl": hashlib.sha256(b"simon").hexdigest()}   # stands in for model_sources


def make_source(folder: Path) -> Path:
    """A retail game folder in miniature: what validate_source looks for, two sounds and a stock model."""
    for rel, data in {"CoFLaunchApp.exe": b"launcher", "cryoffear/liblist.gam": b"game",
                      "cryoffear/maps/c_game_menu1.bsp": b"menu", "cryoffear/cl_dlls/hl.dll": b"stock dll",
                      "cryoffear/sound/a.wav": b"a", "cryoffear/sound/b.wav": b"bb",
                      "cryoffear/models/cutscene/player.mdl": b"simon"}.items():
        path = folder / rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
    return folder


def made_copy(temp: Path, installer=FakeInstaller, seen=None, replace_incomplete=False):
    """friend.setup from make_source into a new copy, with the generators stood in for; returns the copy."""
    from tools.map_merge import groups
    group = groups.Group("cm_apartment1", ["c_apartment1", "c_apartment2"], {}, "1" * 64, "2" * 64, 1, True)
    source, copy, exe = make_source(temp / "retail"), temp / "CoF-Story-Coop", temp / "app.exe"
    exe.write_bytes(b"app")
    models = {"models/player/cof_s1/cof_s1.mdl": b"s1", "models/coop/p_axe.mdl": b"axe"}
    with mock.patch.object(friend, "STOCK_DLL_SHA", hashlib.sha256(b"stock dll").hexdigest()), \
         mock.patch.object(friend, "model_sources", return_value=STOCK_MODELS), \
         mock.patch.object(friend.subprocess, "Popen", installer), \
         mock.patch.object(friend, "generated_models", return_value=models), \
         mock.patch.object(friend, "validate_model"), \
         mock.patch.object(friend.art, "generate", side_effect=lambda root: [Path(root) / "cryoffear/gfx/vgui/inventory12_left.tga"]), \
         mock.patch.object(friend.merge_groups, "load_groups", return_value=[group]), \
         mock.patch.object(friend.merge_groups, "install_group", return_value={"maps/cm_apartment1.bsp": "1" * 64}):
        friend.setup(source, copy, make_payload(temp / "payload"), exe=exe, shortcuts=False,
                     progress=lambda *report: seen.append(report) if seen is not None else None,
                     replace_incomplete=replace_incomplete)
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


def a_copy(root: Path, **record) -> Path:
    """A copy that passes validate_runtime: engine, vgui.dll, the entity limit, one model and its record."""
    for rel, data in {"CoFLaunchApp.exe": b"starter", "xash.dll": b"engine", "vgui.dll": b"vgui",
                      "cryoffear/gameinfo.txt": b'gamedir "cryoffear"\r\n'
                      b"max_edicts 4096\r\n", "cryoffear/models/player/cof_s1/cof_s1.mdl": b"model"}.items():
        path = root / rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
    state = {"schema": 2, "version": "1.1", "build": 12, "xash_sha256": hashlib.sha256(b"engine").hexdigest(),
             "vgui_sha256": hashlib.sha256(b"vgui").hexdigest(), "source_game": "src",
             "models": {"models/player/cof_s1/cof_s1.mdl": hashlib.sha256(b"model").hexdigest()}, "merged_maps": {}}
    state.update(record)
    (root / friend.INSTALLED).write_text(json.dumps(state))
    return root


class NonAsciiPathTests(unittest.TestCase):
    """1.1.9 (L14.7): the default copy is under %LOCALAPPDATA%, which holds the Windows user name. The app's side of a
    copy in a folder named with letters outside ASCII (the engine's own file loading there needs a live look)."""
    BASE = "Usuários/José Ñúñez/Иван 测试"

    def test_setup_play_and_the_install_check_in_such_a_folder(self):
        from tools import coop_play as play
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp) / self.BASE
            base.mkdir(parents=True)
            copy = made_copy(base)
            state = friend.installed_state(copy)
            self.assertEqual(state["source_game"], str((base / "retail").resolve()))
            self.assertIn("Done.", (copy / friend.SETUP_LOG).read_text(encoding="utf-8"))
            (copy / "story-session.log").write_text("last game\n", encoding="utf-8")
            kept = friend.keep_previous_log(copy, "story-session.log")
            self.assertEqual((kept.parent, kept.read_text(encoding="utf-8")), (copy / "story-logs", "last game\n"))
            args, config = play.host_launch(copy, solo=True, name="Bob", map_name="c_intro", players=2, password="",
                                            companions=1, port=play.DEFAULT_PORT, cont=False, video=[])
            self.assertEqual(args[0], str(copy / "CoFLaunchApp.exe"))
            config.encode("ascii")   # the cfgs the game reads name no path
            root = a_copy(base / "second")
            self.assertEqual(friend.validate_runtime(root), root.resolve())


class RepairCheckTests(unittest.TestCase):
    """1.1.8 (L1): what the install check finds is a CopyMismatch, which the app offers to repair."""

    def test_every_mismatch_is_a_copy_mismatch_with_its_problem(self):
        with tempfile.TemporaryDirectory() as temp:
            root = a_copy(Path(temp) / "copy")
            self.assertEqual(friend.validate_runtime(root), root.resolve())
            for rel, data, problem in (("xash.dll", b"other", "does not match its install"),
                                       ("vgui.dll", b"other", "vgui.dll"),
                                       ("cryoffear/gameinfo.txt", b'gamedir "cryoffear"\r\n', "max_edicts"),
                                       ("cryoffear/models/player/cof_s1/cof_s1.mdl", b"other", "Simon model")):
                good = (root / rel).read_bytes()
                (root / rel).write_bytes(data)
                with self.subTest(rel=rel), self.assertRaises(friend.CopyMismatch) as caught:
                    friend.validate_runtime(root)
                self.assertIn(problem, caught.exception.problem)
                self.assertIsInstance(caught.exception, ValueError)   # callers that catch ValueError still do
                self.assertIn("click Update", str(caught.exception))
                (root / rel).write_bytes(good)
            (root / "cryoffear/gameinfo.txt").unlink()
            with self.assertRaises(friend.CopyMismatch):
                friend.validate_runtime(root)

    def test_a_starter_an_antivirus_took_away_is_a_mismatch_that_names_it(self):
        # 1.2.7: a player's antivirus removed CoFLaunchApp.exe after the first game; Start then failed with Windows'
        # "[WinError 2] The system cannot find the file specified". Now the app names it and offers Repair, which
        # lays the engine package (CoFLaunchApp.exe included) over the copy again.
        with tempfile.TemporaryDirectory() as temp:
            root = a_copy(Path(temp) / "copy")
            (root / "CoFLaunchApp.exe").unlink()
            with self.assertRaises(friend.CopyMismatch) as caught:
                friend.validate_runtime(root)
            self.assertIn("CoFLaunchApp.exe", caught.exception.problem)
            self.assertIn("antivirus", caught.exception.problem)
            self.assertIn(str(root.resolve()), caught.exception.problem)

    def test_a_record_from_before_1_1_8_has_no_vgui_hash(self):
        with tempfile.TemporaryDirectory() as temp:
            root = a_copy(Path(temp) / "copy", vgui_sha256=None)
            (root / "vgui.dll").write_bytes(b"whatever it is")
            self.assertEqual(friend.validate_runtime(root), root.resolve())

    def test_setup_and_update_record_the_vgui_dll(self):
        with tempfile.TemporaryDirectory() as temp:
            root, exe = Path(temp) / "copy", Path(temp) / "app.exe"
            (root / "cryoffear").mkdir(parents=True)
            exe.write_bytes(b"app")
            (root / "vgui.dll").write_bytes(b"vgui")
            manifest = {"version": "1.1", "build": 12, "xash_sha256": "0" * 64}
            friend.finish(root, manifest, {}, "src", exe, False, friend.Steps(()))
            self.assertEqual(friend.installed_state(root)["vgui_sha256"], hashlib.sha256(b"vgui").hexdigest())
            (root / "vgui.dll").unlink()
            friend.finish(root, manifest, {}, "src", exe, False, friend.Steps(()))
            self.assertNotIn("vgui_sha256", friend.installed_state(root))

    def test_repair_is_the_update_at_the_same_build(self):
        # the record's build equals the payload's: update lays the engine over again and remakes the generated files
        with tempfile.TemporaryDirectory() as temp:
            root, exe = a_copy(Path(temp) / "copy", build=5), Path(temp) / "app.exe"
            exe.write_bytes(b"app")
            (root / "cryoffear/gameinfo.txt").write_bytes(b'gamedir "cryoffear"\r\n')   # an update stopped part way
            with self.assertRaises(friend.CopyMismatch):
                friend.validate_runtime(root)
            with mock.patch.object(friend, "game_is_running", return_value=False), \
                 mock.patch.object(friend.subprocess, "Popen", FakeInstaller), \
                 mock.patch.object(friend, "make_models", return_value={}), \
                 mock.patch.object(friend, "make_merged_maps", return_value={}), \
                 mock.patch.object(friend.art, "generate", return_value=[]):
                friend.update(root, make_payload(Path(temp) / "payload", b"engine", build=5), exe=exe, shortcuts=False)
            self.assertEqual(friend.validate_runtime(root), root.resolve())


class IncompleteSetupTests(unittest.TestCase):
    """1.1.8 (L4): a setup that stopped left up to 5 GB in a folder that setup then refused to use."""

    def test_only_an_unfinished_copy_of_this_app_counts(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp) / "CoF-Story-Coop"
            self.assertFalse(friend.incomplete_copy(folder))
            folder.mkdir()
            (folder / friend.INCOMPLETE).write_text("Setup is incomplete. Choose a new folder to retry; ...\n")
            self.assertFalse(friend.incomplete_copy(folder))   # our setup log too
            (folder / friend.SETUP_LOG).write_text("05:00:00  Step 1 of 6: Checking your game\n")
            self.assertTrue(friend.incomplete_copy(folder))   # 1.1.7's marker text counts
            (folder / friend.INSTALLED).write_text("{}")
            self.assertFalse(friend.incomplete_copy(folder))   # a finished copy never
            (folder / friend.INSTALLED).unlink()
            (folder / friend.INCOMPLETE).write_text("someone else's file\n")
            self.assertFalse(friend.incomplete_copy(folder))

    @unittest.skipUnless(hasattr(os, "symlink"), "links")
    def test_a_link_is_never_followed(self):
        with tempfile.TemporaryDirectory() as temp:
            folder, outside = Path(temp) / "CoF-Story-Coop", Path(temp) / "outside"
            (folder / "cryoffear/maps").mkdir(parents=True)
            outside.mkdir()
            (outside / "keep.txt").write_text("not ours")
            (folder / friend.INCOMPLETE).write_text("Setup is incomplete.\n")
            (folder / friend.SETUP_LOG).write_text("log\n")
            try:
                os.symlink(outside, folder / "linked", target_is_directory=True)
                os.symlink(outside, folder / "cryoffear/maps/linked", target_is_directory=True)
                os.symlink(folder, Path(temp) / "via-link", target_is_directory=True)
            except OSError:
                self.skipTest("no symlinks here")
            self.assertFalse(friend.incomplete_copy(Path(temp) / "via-link"))
            with self.assertRaisesRegex(ValueError, "new destination"):
                friend.remove_incomplete(Path(temp) / "via-link")
            friend.remove_incomplete(folder)
            self.assertFalse(folder.exists())
            self.assertEqual((outside / "keep.txt").read_text(), "not ours")

    def test_setup_deletes_the_unfinished_copy_and_starts_again_when_asked(self):
        class Failing(FakeInstaller):
            code = 1
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(RuntimeError):
                made_copy(Path(temp), installer=Failing)
            copy = Path(temp) / "CoF-Story-Coop"
            self.assertTrue(friend.incomplete_copy(copy))
            (copy / "cryoffear/stray.bin").write_bytes(b"x")
            with self.assertRaisesRegex(ValueError, "new destination"):   # never without the player's yes
                made_copy(Path(temp))
            self.assertTrue((copy / "cryoffear/stray.bin").exists())
            made_copy(Path(temp), replace_incomplete=True)
            self.assertFalse((copy / "cryoffear/stray.bin").exists() or (copy / friend.INCOMPLETE).exists())
            self.assertEqual(friend.installed_state(copy)["build"], 1)
            log = [STAMP.sub("", line) for line in (copy / friend.SETUP_LOG).read_text(encoding="utf-8").splitlines()]
        # the new log keeps the stopped setup's lines, for a bug report
        self.assertIn("Stopped: The engine could not be installed. See friend-setup-install.log in the copy.", log)
        self.assertLess(log.index("Stopped: The engine could not be installed. See friend-setup-install.log in the copy."),
                        log.index(f"Deleting the unfinished copy an earlier setup left in {copy.absolute()}"))

    def test_a_finished_or_foreign_folder_is_never_deleted(self):
        with tempfile.TemporaryDirectory() as temp:
            copy = made_copy(Path(temp))
            with self.assertRaisesRegex(ValueError, "new destination"):
                made_copy(Path(temp), replace_incomplete=True)
            self.assertIsNotNone(friend.installed_state(copy))


class ModelSourceTests(unittest.TestCase):
    """1.1.8 (L5): a modded or damaged game is refused before the 5 GB copy, not at the models after it."""

    def test_the_pinned_sources_are_the_generators_own(self):
        pinned = friend.model_sources()
        self.assertEqual({rel: pinned[rel] for rel in friend.weapons.SOURCES}, friend.weapons.SOURCES)
        self.assertEqual({rel for rel in pinned if rel.startswith("models/costumes/")},
                         {f"models/costumes/{rel}" for rel in friend.costume_files()})

    def test_a_changed_or_missing_source_names_the_fix(self):
        with tempfile.TemporaryDirectory() as temp:
            source = make_source(Path(temp) / "retail")
            pinned = dict(STOCK_MODELS, **{"models/costumes/HLC/HLC_Hoodie.tga": hashlib.sha256(b"hoodie").hexdigest()})
            (source / "cryoffear/models/costumes/HLC").mkdir(parents=True)
            (source / "cryoffear/models/costumes/HLC/HLC_Hoodie.tga").write_bytes(b"hoodie")
            with mock.patch.object(friend, "model_sources", return_value=pinned):
                friend.check_model_sources(source)
                (source / "cryoffear/models/cutscene/player.mdl").write_bytes(b"a model mod")
                with self.assertRaisesRegex(ValueError, "Verify integrity of game files"):
                    friend.check_model_sources(source)
                friend.check_model_sources(source, {"cryoffear/models/cutscene/player.mdl"})   # the payload's own
                (source / "cryoffear/models/costumes/HLC/HLC_Hoodie.tga").unlink()
                with self.assertRaisesRegex(ValueError, "HLC_Hoodie.tga"):
                    friend.check_model_sources(source, {"cryoffear/models/cutscene/player.mdl"})

    def test_setup_refuses_before_it_copies(self):
        with tempfile.TemporaryDirectory() as temp:
            source = make_source(Path(temp) / "retail")
            (source / "cryoffear/models/cutscene/player.mdl").write_bytes(b"a model mod")
            with mock.patch.object(friend, "STOCK_DLL_SHA", hashlib.sha256(b"stock dll").hexdigest()), \
                    mock.patch.object(friend, "model_sources", return_value=STOCK_MODELS), \
                    mock.patch.object(friend, "copy_assets") as copy, \
                    self.assertRaisesRegex(ValueError, "modded or damaged"):
                friend.setup(source, Path(temp) / "copy", make_payload(Path(temp) / "payload"),
                             exe=Path(temp) / "app.exe", shortcuts=False)
            copy.assert_not_called()
            self.assertFalse((Path(temp) / "copy").exists())


KB_DEF = b'"w"\t\t"+forward"\r\n"h"\t\t"+hint"\r\n// a comment\r\n\r\nSPACE +jump\r\n"1"\t\t"quicksel 1"\r\n'
KB_ACT = b'"blank"\t\t"Movement"\r\n"+forward"\t\t"Move forward"\r\n"cof_skip"\t\t"Skip"'


class InstallAppTests(unittest.TestCase):
    def test_the_app_and_its_folder_of_files_go_into_the_copy(self):
        # 1.2.1: the one-folder app needs its coop-app folder next to the exe; an older copy's is replaced whole
        with tempfile.TemporaryDirectory() as temp:
            download, root = Path(temp) / "download", Path(temp) / "copy"
            (download / friend.APP_FILES / "payload").mkdir(parents=True)
            (download / friend.APP_EXE).write_bytes(b"new app")
            (download / friend.APP_FILES / "payload" / "engine.zip").write_bytes(b"new payload")
            (root / friend.APP_FILES).mkdir(parents=True)
            (root / friend.APP_FILES / "stale.dll").write_bytes(b"old")
            (root / friend.APP_EXE).write_bytes(b"old app")
            friend.install_app(download / friend.APP_EXE, root)
            self.assertEqual((root / friend.APP_EXE).read_bytes(), b"new app")
            self.assertEqual((root / friend.APP_FILES / "payload" / "engine.zip").read_bytes(), b"new payload")
            self.assertFalse((root / friend.APP_FILES / "stale.dll").exists())
            self.assertEqual(sorted(p.name for p in root.iterdir()), sorted([friend.APP_EXE, friend.APP_FILES]))
            # the copy's own app (Repair from inside the copy) is left alone
            friend.install_app(root / friend.APP_EXE, root)
            self.assertEqual((root / friend.APP_EXE).read_bytes(), b"new app")
            # a one-file app from an older zip has no folder: only the exe is copied
            old = Path(temp) / "old"
            old.mkdir()
            (old / friend.APP_EXE).write_bytes(b"one-file app")
            friend.install_app(old / friend.APP_EXE, root)
            self.assertEqual((root / friend.APP_EXE).read_bytes(), b"one-file app")


class MissingCoopBindTests(unittest.TestCase):
    def test_only_unbound_coop_keys_are_bound_for_the_session(self):
        # 1.2.0: a copy set up before O existed keeps its config.cfg; a key bound to anything else stays the player's
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.assertEqual(friend.missing_coop_binds(root), "")   # no config yet: the game's defaults apply
            (root / "cryoffear").mkdir()
            (root / "cryoffear/config.cfg").write_text('bind "j" "cof_warp"\nbind "H" "cof_companion_order"\n'
                                                       'bind ENTER cof_skip\nbind "KP_ENTER" "cof_skip"\n'
                                                       'name "Bob"\n', encoding="ascii")
            self.assertEqual(friend.missing_coop_binds(root), 'bind "o" "cof_wardrobe"\n')
            (root / "cryoffear/config.cfg").write_text('bind "o" "+voicerecord"\nbind "j" "cof_warp"\n',
                                                       encoding="ascii")
            missing = friend.missing_coop_binds(root)
            self.assertNotIn('"o"', missing)
            self.assertIn('bind "h" "cof_companion_order"', missing)


class CoopKeyListTests(unittest.TestCase):
    """1.1.8 (L6): Options > Keybinds > Use defaults binds kb_def.lst, which had no J, H or Enter."""

    def keys(self, root, kb_def=KB_DEF, kb_act=KB_ACT):
        shell = root / "cryoffear/gfx/shell"
        shell.mkdir(parents=True, exist_ok=True)
        (shell / "kb_def.lst").write_bytes(kb_def)
        (shell / "kb_act.lst").write_bytes(kb_act)
        return shell

    def test_the_coop_keys_are_added_once_and_nothing_else_changes(self):
        with tempfile.TemporaryDirectory() as temp:
            shell = self.keys(Path(temp))
            notes = []
            friend.add_coop_keys(Path(temp), mock.Mock(note=notes.append))
            kb_def, kb_act = (shell / "kb_def.lst").read_bytes(), (shell / "kb_act.lst").read_bytes()
            # j, ENTER, KP_ENTER and o added; h keeps the list's own command
            self.assertEqual(kb_def, KB_DEF + b'"j"\t\t"cof_warp"\r\n"ENTER"\t\t"cof_skip"\r\n"KP_ENTER"\t\t"cof_skip"\r\n'
                                              b'"o"\t\t"cof_wardrobe"\r\n')
            # cof_skip is in the menu already; the file had no last line ending
            self.assertEqual(kb_act, KB_ACT + b'\r\n"cof_warp"\t\t"Join your partner (co-op)"\r\n'
                                              b'"cof_companion_order"\t\t"AI companions: wait or follow"\r\n'
                                              b'"cof_wardrobe"\t\t"Wardrobe (costumes)"\r\n')
            self.assertEqual(len(notes), 2)
            friend.add_coop_keys(Path(temp), mock.Mock(note=notes.append))
            self.assertEqual(((shell / "kb_def.lst").read_bytes(), (shell / "kb_act.lst").read_bytes()), (kb_def, kb_act))
            self.assertEqual(len(notes), 2)

    def test_a_list_of_another_format_or_no_list_is_left_alone(self):
        with tempfile.TemporaryDirectory() as temp:
            odd = b'"w" "+forward" "extra"\n'
            shell = self.keys(Path(temp), kb_def=odd)
            (shell / "kb_act.lst").unlink()
            notes = []
            friend.add_coop_keys(Path(temp), mock.Mock(note=notes.append))
            self.assertEqual((shell / "kb_def.lst").read_bytes(), odd)
            self.assertFalse((shell / "kb_act.lst").exists())
            self.assertIn("not in the usual format", notes[0])
            friend.add_coop_keys(Path(temp) / "nothing-here")

    def test_update_adds_them_to_the_copy_and_keeps_the_players_binds(self):
        with tempfile.TemporaryDirectory() as temp:
            root, exe = a_copy(Path(temp) / "copy", build=5), Path(temp) / "app.exe"
            exe.write_bytes(b"app")
            shell = self.keys(root)
            (root / "cryoffear/config.cfg").write_bytes(b'bind "j" "+jump"\n')
            with mock.patch.object(friend, "game_is_running", return_value=False), \
                 mock.patch.object(friend.subprocess, "Popen", FakeInstaller), \
                 mock.patch.object(friend, "make_models", return_value={}), \
                 mock.patch.object(friend, "make_merged_maps", return_value={}), \
                 mock.patch.object(friend.art, "generate", return_value=[]):
                friend.update(root, make_payload(Path(temp) / "payload", b"engine", build=6), exe=exe, shortcuts=False)
            self.assertIn(b'"j"\t\t"cof_warp"', (shell / "kb_def.lst").read_bytes())
            self.assertEqual((root / "cryoffear/config.cfg").read_bytes(), b'bind "j" "+jump"\n')


class LogKeepingTests(unittest.TestCase):
    def test_story_logs_keeps_the_newest_twenty(self):
        # 1.1.8 (L13): every start moved the last log into story-logs, which was never pruned
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "story-logs"
            archive.mkdir()
            for number in range(25):
                old = archive / f"story-session-202610{number:02}-000000.log"
                old.write_text(str(number))
                os.utime(old, (1_000_000 + number, 1_000_000 + number))
            (archive / "notes.txt").write_text("not a session log")
            (root / "story-session.log").write_text("latest")
            kept = friend.keep_previous_log(root, "story-session.log")
            logs = sorted(path.name for path in archive.glob("story-session-*.log"))
            self.assertEqual(len(logs), friend.KEPT_LOGS)
            self.assertIn(kept.name, logs)
            self.assertNotIn("story-session-20261000-000000.log", logs)   # the oldest went
            self.assertIn("story-session-20261024-000000.log", logs)
            self.assertTrue((archive / "notes.txt").exists())


class ManualUpdateTests(unittest.TestCase):
    def test_an_update_refuses_while_the_copys_own_app_runs(self):
        # 1.1.8 (L14.12): it failed at the very end, replacing the running app, and had to be done again
        with tempfile.TemporaryDirectory() as temp:
            root, exe = a_copy(Path(temp) / "copy", build=5), Path(temp) / "new.exe"
            exe.write_bytes(b"new app")
            (root / friend.APP_EXE).write_bytes(b"old app, running")
            real_open = Path.open

            def locked(path, mode="r", *args, **kwargs):
                if Path(path).name == friend.APP_EXE and Path(path).parent == root.resolve() and "r" not in mode:
                    raise PermissionError(13, "The process cannot access the file")
                return real_open(path, mode, *args, **kwargs)
            payload = make_payload(Path(temp) / "payload", b"engine", build=6)
            with mock.patch.object(friend, "game_is_running", return_value=False), \
                    mock.patch.object(Path, "open", locked), \
                    mock.patch.object(friend, "extract_payload") as extract, \
                    self.assertRaisesRegex(ValueError, "Close the other Cry of Fear Co-op window"):
                friend.update(root, payload, exe=exe, shortcuts=False)
            extract.assert_not_called()
            self.assertEqual((root / friend.APP_EXE).read_bytes(), b"old app, running")
            # the copy's own app repairing its copy is no other window
            with mock.patch.object(friend, "game_is_running", return_value=False), \
                    mock.patch.object(Path, "open", locked), \
                    mock.patch.object(friend.subprocess, "Popen", FakeInstaller), \
                    mock.patch.object(friend, "make_models", return_value={}), \
                    mock.patch.object(friend, "make_merged_maps", return_value={}), \
                    mock.patch.object(friend.art, "generate", return_value=[]):
                friend.update(root, payload, exe=root / friend.APP_EXE, shortcuts=False)
            self.assertEqual(friend.installed_state(root)["build"], 6)


if __name__ == "__main__":
    unittest.main()
