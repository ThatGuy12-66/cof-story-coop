"""tools/release.py: what it refuses, and the texts it writes. The build itself is checked by running it."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from tools import app_update as up
from tools import release as rel

README = """Cry of Fear Co-op 9.9

Play the Cry of Fear campaign together.

WHAT'S NEW IN 9.9
- The first thing. It is good.
- The second thing, which wraps onto
  a second line.
Coming from 9.8? Run the new "CoF Story Co-op.exe" and click "Update".

KNOWN ISSUES
- One issue.
- Another issue.

REPORTING BUGS
"""


def published(**changes):
    data = {"schema": 1, "label": "9.8", "version": "9.8", "build": 40, "zip": "CoF-Story-Coop-9.8.zip",
            "size": 10, "sha256": "0" * 64}
    data.update(changes)
    return data


class TextTests(unittest.TestCase):
    def setUp(self):
        patcher = mock.patch.object(rel.play, "VERSION_LABEL", "9.9")
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_readme_sections(self):
        self.assertEqual(rel.readme_section(README, "WHAT'S NEW IN 9.9"),
                         ["The first thing. It is good.", "The second thing, which wraps onto a second line."])
        self.assertEqual(rel.readme_section(README, "KNOWN ISSUES"), ["One issue.", "Another issue."])
        self.assertEqual(rel.readme_section(README, "NOT THERE"), [])

    def test_the_moddb_text(self):
        text = rel.moddb_text(README, "ab" * 32)
        self.assertTrue(text.startswith("NAME: Cry of Fear Story Co-op 9.9\nCATEGORY: Full Version\n"))
        self.assertIn("SUMMARY:\nCry of Fear Story Co-op 9.9. The first thing.\n", text)
        self.assertIn("What's new in 9.9\n\n- The first thing. It is good.\n- The second thing, which wraps onto a "
                      "second line.\n", text)
        self.assertIn('Coming from 9.8? Extract the zip, run the new "CoF Story Co-op.exe" and click "Update". '
                      "Saves and settings are kept.", text)
        self.assertIn("Known issues\n\n- One issue.\n- Another issue.\n", text)
        self.assertTrue(text.endswith("SHA-256: " + "AB" * 32 + "\n"))


class PreflightTests(unittest.TestCase):
    def run_preflight(self, readme=README, label="9.9", build=41, latest=None, notes=True, stamp=None):
        temp = Path(tempfile.mkdtemp())
        (temp / "docs/releases").mkdir(parents=True)
        if notes:
            (temp / "docs/releases" / f"{label}.md").write_text("notes", encoding="utf-8")
        (temp / "player_readme.txt").write_text(readme, encoding="utf-8")
        (temp / "cof_version.h").write_text(f'#define COFE_VERSION   "{label if stamp is None else stamp}"\n'
                                            '#define COFE_MILESTONE "release"\n', encoding="latin-1")
        log = rel.Log(temp / "release.log")
        with mock.patch.object(rel.play, "VERSION_LABEL", label), mock.patch.object(rel.play, "BUILD", build), \
                mock.patch.object(rel, "ENGINE_STAMP", temp / "cof_version.h"), \
                mock.patch.object(rel, "README", temp / "player_readme.txt"), \
                mock.patch.object(rel, "NOTES", temp / "docs/releases"), mock.patch.object(rel, "ROOT", temp), \
                mock.patch.object(rel, "tracked", return_value=True), \
                mock.patch.object(rel.importlib.util, "find_spec", return_value=object()), \
                mock.patch.object(up, "read_info", return_value=latest or published()):
            found = rel.preflight(log)
        return found, log.problems

    def test_a_ready_release_passes(self):
        found, problems = self.run_preflight()
        self.assertEqual(problems, [])
        self.assertEqual(found.label, "9.8")

    def test_it_refuses_what_players_would_not_be_offered(self):
        self.assertTrue(self.run_preflight(build=40)[1])   # same build as the published one: no update offered
        self.assertTrue(self.run_preflight(label="9.7", readme=README.replace("9.9", "9.7"))[1])
        self.assertTrue(self.run_preflight(notes=False)[1])
        self.assertTrue(self.run_preflight(readme=README.replace("WHAT'S NEW IN 9.9", "WHATS NEW"))[1])
        self.assertTrue(self.run_preflight(readme="Cry of Fear Co-op 9.8\n" + README)[1])

    def test_it_refuses_an_engine_stamp_of_another_version(self):
        # 1.1.9 (R4): the engine's on-screen stamp said cofenhanced 0.4.0-test3 up to 1.1.7
        found, problems = self.run_preflight(stamp="9.8")
        self.assertEqual(len(problems), 1)
        self.assertIn("engine/cof_version.h", problems[0])
        self.assertIn("9.8", problems[0])
        self.assertTrue(self.run_preflight(stamp="0.4.0-test3")[1])

    def test_the_package_check_reads_the_payloads_release_stamp(self):
        import inspect
        source = inspect.getsource(rel.check_package)
        self.assertIn('archive.read("cof-enhanced/release.txt")', source)
        self.assertIn('log.check(f"cofenhanced {label()}" in stamp,', source)

    def test_the_engine_stamp_is_read_from_the_engine(self):
        self.assertEqual(rel.ENGINE_STAMP, rel.packager.enhanced.ENGINE / "engine/cof_version.h")
        self.assertIsNotNone(up.LABEL_PATTERN.fullmatch(rel.engine_stamp()))
        with mock.patch.object(rel, "ENGINE_STAMP", Path(tempfile.mkdtemp()) / "missing.h"):
            self.assertEqual(rel.engine_stamp(), "")


class UntrackedTests(unittest.TestCase):
    def test_untracked_notes_are_hidden_for_the_build_and_the_exclude_file_restored(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            subprocess.run(["git", "init", "-q", str(repo)], check=True)
            (repo / "docs/research").mkdir(parents=True)
            (repo / "docs/research/notes.md").write_text("x")
            exclude = repo / ".git/info/exclude"
            exclude.parent.mkdir(parents=True, exist_ok=True)
            exclude.write_bytes(b"# mine\n")
            with mock.patch.object(rel, "ROOT", repo):
                with rel.untracked_hidden() as hidden:
                    self.assertEqual(hidden, ["docs/research/notes.md"])
                    self.assertEqual(rel.git("status", "--porcelain"), "")
                self.assertEqual(exclude.read_bytes(), b"# mine\n")
                (repo / "tools").mkdir()
                (repo / "tools/new.py").write_text("x")
                with self.assertRaises(ValueError), rel.untracked_hidden():
                    pass
                self.assertEqual(exclude.read_bytes(), b"# mine\n")


class PublishTests(unittest.TestCase):
    def setUp(self):
        self.temp = Path(tempfile.mkdtemp())
        self.zip = self.temp / "CoF-Story-Coop-9.9.zip"
        self.zip.write_bytes(b"package")
        self.info = up.release_info("9.9", "9.9", 41, self.zip)
        (self.temp / up.INFO).write_text(json.dumps(self.info))
        self.state = {"label": "9.9", "commits": {"launcher": "a"}, "zip": str(self.zip), "sha256": self.info["sha256"],
                      "tests_skipped": False, "passed": True}
        (self.temp / "docs").mkdir()
        (self.temp / "docs/9.9.md").write_text("notes")

    def publish(self, heads=None, dry_run=True):
        (self.temp / "build-state.json").write_text(json.dumps(self.state))
        with mock.patch.object(rel.play, "VERSION_LABEL", "9.9"), mock.patch.object(rel, "folder", return_value=self.temp), \
                mock.patch.object(rel, "NOTES", self.temp / "docs"), mock.patch.object(rel, "ROOT", self.temp), \
                mock.patch.object(rel, "heads", return_value=heads or {"launcher": "a"}), \
                mock.patch.object(up, "read_info", return_value=published()), \
                mock.patch.object(rel.subprocess, "run") as run:
            code = rel.publish(dry_run)
        return code, run

    def test_a_dry_run_publishes_nothing(self):
        code, run = self.publish()
        self.assertEqual(code, 0)
        run.assert_not_called()

    def test_it_refuses_a_build_that_did_not_pass_or_is_not_whats_checked_out(self):
        self.assertEqual(self.publish(heads={"launcher": "b"})[0], 1)
        self.state["passed"] = False
        self.assertEqual(self.publish()[0], 1)
        self.state.update(passed=True, tests_skipped=True)
        self.assertEqual(self.publish()[0], 1)
        self.state["tests_skipped"] = False
        self.zip.write_bytes(b"changed")
        self.assertEqual(self.publish()[0], 1)

    def test_publish_makes_the_github_release_with_the_zip_and_release_json(self):
        result = mock.Mock(returncode=0, stdout="https://github.com/x", stderr="")
        later = dict(published(), label="9.9", build=41, zip="CoF-Story-Coop-9.9.zip")
        (self.temp / "build-state.json").write_text(json.dumps(self.state))
        with mock.patch.object(rel.play, "VERSION_LABEL", "9.9"), mock.patch.object(rel, "folder", return_value=self.temp), \
                mock.patch.object(rel, "NOTES", self.temp / "docs"), mock.patch.object(rel, "ROOT", self.temp), \
                mock.patch.object(rel, "heads", return_value={"launcher": "a"}), \
                mock.patch.object(up, "read_info", side_effect=[published(), later]), \
                mock.patch.object(rel.subprocess, "run", return_value=result) as run:
            self.assertEqual(rel.publish(), 0)
        command = run.call_args.args[0]
        self.assertEqual(command[:4], ["gh", "release", "create", "v9.9"])
        self.assertIn(str(self.zip), command)
        self.assertIn(f"{self.temp / up.INFO}#release.json", command)
        self.assertEqual(command[command.index("--repo") + 1], "charliee1w/cof-story-coop-releases")
        self.assertIn("--latest", command)


if __name__ == "__main__":
    unittest.main()
