"""Releases in two commands: build and check a version, then publish it.

    python -m tools.release build      tests, the package, every release check, the ModDB text
    python -m tools.release publish    the GitHub release (the app's updater and the Discord bot pick it up)

Before `build`: set VERSION / VERSION_LABEL / BUILD in tools/coop_play.py, write "WHAT'S NEW IN <label>" at the
top of tools/player_readme.txt, write the release notes players see in Discord and on GitHub in
docs/releases/<label>.md, and commit everything. docs/releasing.md has the whole routine, ModDB included.

`build` launches no game and publishes nothing. Its evidence goes to evidence/release-<label>/.
`publish` is public: it refuses unless `build` passed for exactly the commits that are checked out now.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import app_update  # noqa: E402
from tools import build_tester_package as packager  # noqa: E402
from tools import coop_play as play  # noqa: E402
from tools import friend_setup as setup  # noqa: E402

README = ROOT / "tools/player_readme.txt"
NOTES = ROOT / "docs/releases"
SMOKE_SOURCE = ROOT / "evidence/tester-package-20261015/smoke-source/Cry of Fear"   # a hardlink mirror of the game
ZIP_FILES = ["CoF Story Co-op.exe", "READ ME FIRST.txt", "source-and-licences/corresponding-source.zip",
             "source-and-licences/notices/PYINSTALLER-COPYING.txt", "source-and-licences/notices/PYTHON-LICENSE.txt",
             "source-and-licences/notices/TK-LICENSE.txt"]
DEV_WORDS = ("test build", "test package", "tester", "cheat", "console")
GH = "gh"
BELOW_NORMAL = 0x00004000   # children (tests, PyInstaller, installs) inherit it: the PC stays usable
PASSED = "RESULT: all checks passed"


def label() -> str:
    return play.VERSION_LABEL.replace(" ", "")


def folder() -> Path:
    return ROOT / "evidence" / f"release-{label()}"


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def key(text: str) -> tuple[int, ...]:
    return tuple(int(p) for p in text.split("."))


class Log:
    """Prints and keeps every line; failures are counted, not raised, so one run shows them all."""

    def __init__(self, path: Path):
        self.path, self.problems = path, []
        path.parent.mkdir(parents=True, exist_ok=True)

    def say(self, text: str) -> None:
        line = f"{time.strftime('%H:%M:%S')}  {text}"
        print(line, flush=True)
        with self.path.open("a", encoding="utf-8") as handle:
            handle.write(line + "\n")

    def check(self, ok: bool, what: str) -> bool:
        self.say(("ok   " if ok else "FAIL ") + what)
        if not ok:
            self.problems.append(what)
        return ok


def conda_dlls() -> None:
    """A conda Python keeps OpenSSL, libffi and friends in Library\\bin, found only through PATH. Without it there,
    PyInstaller leaves them out and the packaged app can't load _ssl (no update check, no UPnP lookups over HTTPS).
    The checks catch that (--self-test, --update-check); this makes sure it doesn't happen."""
    library = Path(sys.base_prefix) / "Library" / "bin"
    path = os.environ.get("PATH", "")
    if library.is_dir() and str(library).casefold() not in path.casefold().split(os.pathsep):
        os.environ["PATH"] = str(library) + os.pathsep + path


def below_normal() -> None:
    if os.name == "nt":
        import ctypes
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), BELOW_NORMAL)


def git(*args, repo: Path | None = None) -> str:
    return subprocess.run(["git", "-C", str(repo or ROOT), *args], capture_output=True, text=True,
                          check=True).stdout.strip()


def heads() -> dict:
    return {name: git("rev-parse", "HEAD", repo=repo) for name, repo in packager.REPOS.items()}


@contextmanager
def untracked_hidden():
    """The builder refuses a dirty tree. Untracked notes outside the code (docs/research and the like) are kept out of
    its sight for the build, then the exclude file is put back exactly as it was. Changed tracked files still refuse."""
    exclude = ROOT / ".git/info/exclude"
    before = exclude.read_bytes() if exclude.exists() else None
    loose = [line[3:] for line in git("status", "--porcelain", "--untracked-files=all").splitlines()
             if line.startswith("?? ")]
    code = [p for p in loose if p.split("/")[0] in ("tools", "cofcoop", "tests")]
    if code:
        raise ValueError(f"Commit or remove these first, they would change the app: {code}")
    try:
        if loose:
            text = (before or b"").decode("utf-8")
            exclude.write_text(text + ("" if text.endswith("\n") or not text else "\n") +
                               "".join(f"/{p}\n" for p in loose), encoding="utf-8")
        yield loose
    finally:
        if before is None:
            exclude.unlink(missing_ok=True)
        else:
            exclude.write_bytes(before)


def readme_section(text: str, title: str) -> list[str]:
    """The bullets under a readme heading, each joined onto one line."""
    lines, out, inside = text.splitlines(), [], False
    for line in lines:
        if line.strip() == title:
            inside = True
            continue
        if inside:
            if line.startswith("- "):
                out.append(line[2:].strip())
            elif line.startswith("  ") and out:
                out[-1] += " " + line.strip()
            else:
                break
    return out


def coming_from(text: str) -> str:
    return next((line.strip() for line in text.splitlines() if line.startswith("Coming from")), "")


def moddb_text(readme: str, zip_sha: str) -> str:
    """The ModDB add-file form's fields, in the layout used for 1.1 and 1.1.1. Edit before pasting if needed."""
    new = readme_section(readme, f"WHAT'S NEW IN {label()}")
    known = readme_section(readme, "KNOWN ISSUES")
    first = re.split(r"(?<=\.)\s", new[0], maxsplit=1)[0] if new else "Fixes and improvements."
    parts = [f"NAME: Cry of Fear Story Co-op {label()}", "CATEGORY: Full Version", "", "SUMMARY:",
             f"Cry of Fear Story Co-op {label()}. {first}", "", "DESCRIPTION:", f"What's new in {label()}", "",
             *[f"- {b}" for b in new]]
    if coming_from(readme):
        parts += ["", coming_from(readme).replace("Run the new", "Extract the zip, run the new") +
                  " Saves and settings are kept."]
    parts += ["", "Install", "",
              "1. Extract the zip to a normal folder, like Downloads. Not into the game folder.",
              "2. Double-click \"CoF Story Co-op.exe\". If Windows warns you (the app isn't signed), click \"More "
              "info\", then \"Run anyway\".",
              "3. Click \"Set up\". It finds Cry of Fear through Steam and makes a separate copy for co-op. Your Steam "
              "game, saves and settings aren't changed.",
              "4. Start it from the \"Cry of Fear Co-op\" shortcut on your desktop or Start menu.", "",
              "You need Windows 10 or 11, Cry of Fear installed from Steam (it's free) with Steam running, and about "
              "6 GB of free space.", "", "In the zip", "",
              "- CoF Story Co-op.exe: the app (setup, update, Play solo, Host, Join)",
              "- READ ME FIRST.txt: install, hosting and co-op keys",
              "- source-and-licences: the engine source (GPL) and licences"]
    if known:
        parts += ["", "Known issues" if len(known) > 1 else "Known issue", "",
                  *([f"- {k}" for k in known] if len(known) > 1 else known)]
    parts += ["", f"Bugs and help: {play.REPORT_URL.replace('https://', '')}", f"SHA-256: {zip_sha.upper()}", ""]
    return "\n".join(parts)


# ---------------------------------------------------------------------------------------------------- build

def tracked(path: Path) -> bool:
    return subprocess.run(["git", "-C", str(ROOT), "ls-files", "--error-unmatch", str(path.relative_to(ROOT))],
                          capture_output=True).returncode == 0


def preflight(log: Log):
    """What has to be true before anything is built. Returns the release now on the page (the players' version)."""
    readme = README.read_text(encoding="utf-8")
    log.check(readme.startswith(f"Cry of Fear Co-op {label()}\n"), f"readme starts with \"Cry of Fear Co-op {label()}\"")
    log.check(f"WHAT'S NEW IN {label()}" in readme and readme_section(readme, f"WHAT'S NEW IN {label()}"),
              f"readme has WHAT'S NEW IN {label()} with its bullets")
    notes = NOTES / f"{label()}.md"
    log.check(notes.is_file() and notes.read_text(encoding="utf-8").strip() != "" and tracked(notes),
              f"release notes for Discord and GitHub, committed: {notes.relative_to(ROOT)}")
    log.check(app_update.LABEL_PATTERN.fullmatch(label()) is not None, f"version label {label()} is plain numbers")
    log.check(importlib.util.find_spec("PyInstaller") is not None,
              f"PyInstaller is installed in this Python ({sys.executable})")
    try:
        latest = app_update.parse_release(app_update.read_info())
    except Exception as error:   # no release yet, or offline: say so, the checks below need it
        log.check(False, f"read the latest release from the releases page ({error})")
        return None
    log.say(f"Latest published: {latest.label} (build {latest.build}); this one: {label()} (build {play.BUILD})")
    log.check(play.BUILD > latest.build, f"build {play.BUILD} is higher than the published {latest.build} "
                                         "(so the app and Update offer it)")
    log.check(key(label()) > key(latest.label), f"{label()} comes after {latest.label}")
    return latest


def run_tests(log: Log, out: Path) -> None:
    log.say("Running the full test suite in parallel (tools/run_tests.py, BelowNormal priority)...")
    started = time.time()
    with (out / "full-suite.log").open("w", encoding="utf-8") as handle:
        result = subprocess.run([sys.executable, "-m", "tools.run_tests"], cwd=ROOT,
                                stdout=handle, stderr=subprocess.STDOUT, text=True)
    tail = (out / "full-suite.log").read_text(encoding="utf-8", errors="replace").strip().splitlines()[-3:]
    log.check(result.returncode == 0, f"full suite in {time.time() - started:.0f} s: {' | '.join(tail)}")


def run(log: Log, command: list, what: str, timeout: int = 1800) -> bool:
    started = time.time()
    result = subprocess.run([str(c) for c in command], timeout=timeout, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    return log.check(result.returncode == 0, f"{what}: exit {result.returncode} in {time.time() - started:.0f} s "
                                             f"{result.stderr.strip()[-400:]}")


def check_copy(log: Log, copy: Path, report: dict, what: str) -> str:
    state = setup.installed_state(copy) or {}
    log.check((state.get("version"), state.get("build")) == (play.VERSION, play.BUILD),
              f"{what}: record {state.get('version')} build {state.get('build')}")
    log.check(sha(copy / "xash.dll") == report["payload"]["xash_sha256"], f"{what}: the engine is this build's")
    log.check(sha(copy / "cryoffear/cl_dlls/hl.dll") == setup.STOCK_DLL_SHA, f"{what}: stock hl.dll")
    log.check(not any((copy / rel).exists() for rel in setup.RETIRED_FILES), f"{what}: no retired files")
    cfg = (copy / "cryoffear" / play.STORY_CFG).read_text(encoding="ascii").splitlines()
    log.check(all(line in cfg for line in ("sv_cheats 0", "cof_test_gun 0", "developer 0")),
              f"{what}: the story cfg turns the test tools off")
    log.check(sha(copy / setup.APP_EXE) == report["exe_sha256"], f"{what}: this version's app is in the copy")
    merged = (copy / setup.MERGE_LOG).read_text(encoding="utf-8").splitlines()
    log.check(merged and not any("not merged" in line for line in merged), f"{what}: {len(merged)} merged groups")
    return (copy / setup.SETUP_LOG).read_text(encoding="utf-8")


def check_package(log: Log, out: Path, report: dict, latest: app_update.Release, source: Path) -> None:
    """The checks each release had by hand (evidence/release-1.1.1/check_release.py), for any version."""
    zip_path = Path(report["zip"])
    with zipfile.ZipFile(zip_path) as archive:
        names = sorted(archive.namelist())
        log.check(zip_path.name == f"CoF-Story-Coop-{label()}.zip", f"zip name {zip_path.name}")
        log.check(archive.testzip() is None, "zip CRC")
        log.check(names == ZIP_FILES, f"zip files: {names}")
        readme = archive.read("READ ME FIRST.txt").decode("utf-8")
        log.check(archive.read("READ ME FIRST.txt") == README.read_bytes(), "the zip's readme is tools/player_readme.txt")
        log.check(not any(w in readme.lower() for w in DEV_WORDS), "readme has no test or dev wording")
        unzipped = out / "unzipped"
        shutil.rmtree(unzipped, ignore_errors=True)
        archive.extractall(unzipped)
    with zipfile.ZipFile(out / "package/payload" / setup.PAYLOAD_ZIP) as archive:
        members = {n.casefold() for n in archive.namelist()}
        log.check(not [n for n in setup.RETIRED_FILES if n.casefold() in members], "payload leaves out the retired files")
        engine = archive.read("cof-enhanced/files/xash.dll")
        log.check(hashlib.sha256(engine).hexdigest() == report["payload"]["xash_sha256"], "payload engine matches its record")
    log.check((report["payload"]["version"], report["payload"]["build"]) == (play.VERSION, play.BUILD),
              f"payload manifest {report['payload']['version']} build {report['payload']['build']}")

    exe = unzipped / setup.APP_EXE
    run(log, [exe, "--self-test"], "the extracted exe's --self-test", 180)
    found = out / "update-check.json"
    if run(log, [exe, "--update-check", found], "the extracted exe reads the releases page", 120):
        seen = json.loads(found.read_text(encoding="utf-8"))
        log.check(seen.get("latest", {}).get("label") == latest.label and seen.get("newer") is False,
                  f"it sees {seen.get('latest', {}).get('label')} and offers nothing older")

    if not source.is_dir():
        log.check(False, f"the source game for the install checks: {source} (pass --source)")
        return
    work = out / "install-check"
    shutil.rmtree(work, ignore_errors=True)
    fresh = work / "fresh" / "CoF-Story-Coop"
    if run(log, [exe, "--install", source, fresh, "--no-shortcuts"], f"fresh {label()} install"):
        text = check_copy(log, fresh, report, "fresh")
        log.check(all(f"Step {n} of 6" in text for n in range(1, 7)), "setup log has all six steps")
    shutil.rmtree(fresh.parent, ignore_errors=True)

    # what players have now: the published version, updated by this one
    try:
        old_release = latest
        old_zip = app_update.download(old_release, work / "published")
        old_exe = app_update.extract_app(old_zip, work / "published")
        log.check(True, f"downloaded the published {old_release.label} ({old_release.size} bytes, SHA-256 matches)")
    except Exception as error:
        log.check(False, f"download the published version: {error}")
        return
    old = work / "update" / "CoF-Story-Coop"
    if run(log, [old_exe, "--install", source, old, "--no-shortcuts"], f"{old_release.label} install"):
        marker = old / "cryoffear/save/update-check-marker.sav"
        marker.parent.mkdir(parents=True, exist_ok=True)
        marker.write_bytes(b"kept")
        if run(log, [exe, "--update", old, "--no-shortcuts"], f"{label()} update over {old_release.label}"):
            text = check_copy(log, old, report, "updated")
            log.check(all(f"Step {n} of 4" in text for n in range(1, 5)), "update log has all four steps")
            log.check(marker.read_bytes() == b"kept", "a save is kept by the update")
    shutil.rmtree(work, ignore_errors=True)


def build(source: Path, skip_tests: bool = False) -> int:
    out = folder()
    if (out / "package").exists():
        raise ValueError(f"{out / 'package'} is there already: this version was built. Bump the version, or move "
                         "that folder aside to build it again.")
    out.mkdir(parents=True, exist_ok=True)
    below_normal()
    conda_dlls()
    log = Log(out / "release.log")
    log.say(f"Release {label()} (join version {play.VERSION}, build {play.BUILD}): build and check")
    latest = preflight(log)
    if log.problems:
        log.say(f"Stopped before building: {log.problems}")
        return 1
    if skip_tests:
        log.say("Tests skipped (--skip-tests): run them before publishing.")
    else:
        run_tests(log, out)
        if log.problems:
            return 1
    log.say("Building the package (engine payload, PyInstaller, zip)...")
    with untracked_hidden() as hidden:
        if hidden:
            log.say(f"Kept out of the build's dirty check (untracked, not code): {hidden}")
        report = packager.build(out / "package")
    log.say(f"Package {Path(report['zip']).name}: {report['zip_bytes']} bytes, SHA-256 {report['zip_sha256']}")
    check_package(log, out, report, latest, source)
    zip_path = Path(report["zip"])
    info = app_update.release_info(label(), play.VERSION, play.BUILD, zip_path)
    (out / app_update.INFO).write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    (out / "moddb-text.txt").write_text(moddb_text(README.read_text(encoding="utf-8"), info["sha256"]), encoding="utf-8")
    state = {"label": label(), "commits": report["commits"], "zip": str(zip_path), "sha256": info["sha256"],
             "tests_skipped": skip_tests, "passed": not log.problems}
    (out / "build-state.json").write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    if log.problems:
        log.say(f"RESULT: {len(log.problems)} failed: {log.problems}")
        return 1
    log.say(PASSED)
    log.say(f"Next: upload {zip_path.name} to ModDB with {out / 'moddb-text.txt'}, then `python -m tools.release "
            "publish`.")
    return 0


# ---------------------------------------------------------------------------------------------------- publish

def publish(dry_run: bool = False) -> int:
    out = folder()
    log = Log(out / "release.log")
    log.say(f"Release {label()}: publish{' (dry run)' if dry_run else ''}")
    try:
        state = json.loads((out / "build-state.json").read_text(encoding="utf-8"))
        info = json.loads((out / app_update.INFO).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        log.say("No passed build for this version. Run `python -m tools.release build` first.")
        return 1
    zip_path = Path(state["zip"])
    notes = NOTES / f"{label()}.md"
    log.check(state.get("passed") is True, "the build and every check passed")
    log.check(not state.get("tests_skipped"), "the build ran the full test suite")
    log.check(heads() == state["commits"], "the checked-out commits are the ones that were built")
    log.check(zip_path.is_file() and sha(zip_path) == info["sha256"] == state["sha256"], f"{zip_path.name} unchanged")
    log.check(notes.is_file(), f"release notes {notes.relative_to(ROOT)}")
    try:
        app_update.parse_release(info)
        latest = app_update.parse_release(app_update.read_info())
        log.check(info["build"] > latest.build, f"still newer than the published {latest.label}")
    except Exception as error:
        log.check(False, f"read the releases page: {error}")
    if log.problems:
        log.say(f"Not published: {log.problems}")
        return 1
    command = [GH, "release", "create", f"v{label()}", str(zip_path), f"{out / app_update.INFO}#{app_update.INFO}",
               "--repo", app_update.RELEASES, "--title", f"Cry of Fear Story Co-op {label()}",
               "--notes-file", str(notes), "--latest"]
    if dry_run:
        log.say("Would run: " + " ".join(command))
        return 0
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if not log.check(result.returncode == 0, f"gh release create: {(result.stdout + result.stderr).strip()[-400:]}"):
        return 1
    for _ in range(12):   # the latest/download link can lag a few seconds behind
        try:
            seen = app_update.parse_release(app_update.read_info())
            if seen.label == label():
                break
        except Exception:
            pass
        time.sleep(5)
    else:
        log.check(False, "the releases page shows the new version")
        return 1
    log.check(True, f"the releases page serves {label()}: the app offers it, the Discord bot posts it within "
                    "5 minutes")
    log.say(f"Published: {app_update.RELEASES_PAGE}/tag/v{label()}")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="step", required=True)
    b = sub.add_parser("build", help="tests, package, checks, ModDB text (publishes nothing)")
    b.add_argument("--source", type=Path, default=SMOKE_SOURCE, help="a Cry of Fear folder for the install checks")
    b.add_argument("--skip-tests", action="store_true", help="for a dry run only: publish refuses such a build")
    p = sub.add_parser("publish", help="the GitHub release, after a passed build")
    p.add_argument("--dry-run", action="store_true", help="check everything, print the command, publish nothing")
    args = parser.parse_args(argv)
    try:
        return build(args.source, args.skip_tests) if args.step == "build" else publish(args.dry_run)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Release stopped: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
