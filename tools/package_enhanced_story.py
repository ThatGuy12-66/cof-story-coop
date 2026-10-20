"""Stage an experimental Enhanced installer plus corresponding modified source.

Read-only plan by default. --write packages into a fresh project evidence
directory. Never installs, launches, publishes, or includes retail game assets.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cofcoop import core

REPO = core.PROJECT_ROOT / "upstream/cof-enhanced"
ENGINE = REPO / "xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a"
BUILD = ENGINE / "build-local-20260927"
FROZEN = core.PROJECT_ROOT / "runtimes/enhanced/experiment-evidence/staged-binaries"
VERSION = "0.4.0-test3"
REVISION = 5  # playtester bundle revision; v3 has the join overflow, v4 lacks the transition fixes
BINARY_INPUTS = {
    "CoFLaunchApp.exe": FROZEN / "CoFLaunchApp.exe",
    "xash.dll": BUILD / "engine/xash.dll",
    "ref_gl.dll": BUILD / "ref/gl/ref_gl.dll",
    "vgui.dll": BUILD / "3rdparty/freevgui/vgui.dll",
    "FileSystem_Stdio.dll": FROZEN / "FileSystem_Stdio.dll",
    "SDL2.dll": FROZEN / "SDL2.dll",
    "cryoffear/cl_dlls/menu.dll": BUILD / "3rdparty/mainui/menu.dll",
}
LOCAL_SOURCES = (
    "cofcoop/__init__.py", "cofcoop/core.py", "cofcoop/bsp.py",
    "tools/build_enhanced.cmd", "tools/package_enhanced_story.py",
    "tools/story_fixture.py", "tools/verify_story_fixture.py",
    "tools/prepare_simon_models.py", "tests/test_simon_models.py",
    "tools/enhanced_local_control.py", "tests/test_enhanced_party.py",
    "tests/engine/cof_party_harness.c",
    "tests/test_story_fixture.py", "docs/enhanced-story-testing.md",
    "tools/friend_setup.py", "tests/test_friend_setup.py",
    "tools/build_tester_package.py", "tools/coop_app.py", "tools/coop_play.py", "tools/player_readme.txt",
    "tools/make_inventory12_art.py", "cofcoop/network.py", "tests/test_coop_play.py", "tests/test_coop_app.py",
    "tests/test_enhanced_userinfo.py", "tests/engine/cof_userinfo_harness.c",
    "tests/test_enhanced_client_transition.py", "tests/engine/cof_client_transition_harness.c",
    "tests/test_enhanced_server_transition.py", "tests/engine/cof_server_transition_harness.c",
    "tests/test_enhanced_rules_zero.py", "tests/engine/cof_rules_zero_harness.c",
)
EXPERIMENTAL_NOTICE = (
    "Cry of Fear story co-op. Not an official Cry of Fear or Cry of Fear: Enhanced release.\n"
    "No retail game files are included; the app copies them from the player's own Steam installation.\n"
)
START_REQUIREMENTS = EXPERIMENTAL_NOTICE


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _plain_file(path: Path) -> None:
    for candidate in (path, *path.parents):
        if candidate.is_symlink() or (hasattr(candidate, "is_junction") and candidate.is_junction()):
            raise ValueError(f"Linked build/package path is not allowed: {candidate}")
    if not path.is_file() or path.stat().st_nlink != 1:
        raise ValueError(f"Expected an independent regular file: {path}")


def source_snapshot() -> dict[str, str]:
    result = {}
    for directory, dirs, files in os.walk(ENGINE):
        dirs[:] = [name for name in dirs if name != ".git" and name != "__pycache__" and not name.startswith(("build-", ".waf"))]
        for name in files:
            path = Path(directory) / name
            if path.suffix.lower() not in (".c", ".h", ".cpp", ".hpp", ".inl") and name != "wscript":
                continue
            _plain_file(path)
            result[path.relative_to(ENGINE).as_posix()] = digest(path.read_bytes())
    return result


def inspect_plan(out: Path) -> dict:
    out = core._local_path(out)
    evidence = (core.PROJECT_ROOT / "evidence").resolve()
    if not out.is_relative_to(evidence) or out == evidence or out.exists():
        raise ValueError("Choose a new subdirectory of project evidence")
    for path in BINARY_INPUTS.values():
        _plain_file(path)
    sources = source_snapshot()
    required = ("engine/server/sv_cof_story.c", "engine/server/sv_cof_party.c")
    missing = [path for path in required if path not in sources]
    binaries = {name: {"source": str(path), "sha256": digest(path.read_bytes()), "bytes": path.stat().st_size}
                for name, path in BINARY_INPUTS.items()}
    newest_source = max((ENGINE / rel).stat().st_mtime for rel in sources if rel.startswith("engine/"))
    stale = BINARY_INPUTS["xash.dll"].stat().st_mtime < newest_source
    return {"output": str(out), "version": VERSION, "experimental_story_build": True,
            "written": False, "missing_required_sources": missing, "engine_binary_older_than_sources": stale,
            "binaries": binaries, "engine_source_sha256": sources}


def package(out: Path) -> dict:
    report = inspect_plan(out)
    if report["missing_required_sources"] or report["engine_binary_older_than_sources"]:
        raise ValueError("Story/party source is missing or xash.dll predates current source; finish the incremental build first")
    out = Path(report["output"])
    staged = out / "staged-binaries"
    staged.mkdir(parents=True, exist_ok=False)
    for name, source in BINARY_INPUTS.items():
        target = staged / name
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as stream:
            stream.write(source.read_bytes())
        if digest(target.read_bytes()) != report["binaries"][name]["sha256"]:
            raise RuntimeError("A build output changed during staging")
    (staged / "SHA256SUMS.txt").write_text("".join(f"{entry['sha256']}  {name}\n" for name, entry in report["binaries"].items()), encoding="utf-8")
    commit = subprocess.check_output(["git", "-C", str(REPO), "rev-parse", "HEAD"], text=True).strip()
    (staged / "BUILD-INFO.txt").write_text(
        f"binaries_commit={commit}\nstamp=cofenhanced {VERSION} with story co-op\n"
        "local_story_changes=true\nprotocol=49\nretail_assets_included=false\n", encoding="utf-8")
    shell = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    env = os.environ.copy()
    env["PSModulePath"] = str(shell.parent / "Modules") + os.pathsep + str(Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "WindowsPowerShell/Modules")
    args = [str(shell), "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(REPO / "scripts/build-release.ps1"),
            "-Version", VERSION, "-Binaries", str(staged), "-OutDir", str(out / "package"),
            "-Repo", str(REPO), "-EngineTree", str(ENGINE), "-CanonicalGame", str(core.DEFAULT_GAME_DIR)]
    with (out / "package-build.log").open("wb") as log:
        result = subprocess.run(args, cwd=REPO, env=env, stdout=log, stderr=subprocess.STDOUT, check=False)
    if result.returncode:
        raise RuntimeError(f"Upstream packaging failed ({result.returncode}); inspect {out / 'package-build.log'}")
    if source_snapshot() != report["engine_source_sha256"]:
        raise RuntimeError("Engine sources changed during packaging; do not distribute this attempt")
    source_zip = out / "package" / f"cof-enhanced-{VERSION}-source.zip"
    prefix = f"cof-enhanced-{VERSION}/engine-source/{ENGINE.name}/"
    with zipfile.ZipFile(source_zip, "a", compression=zipfile.ZIP_DEFLATED) as archive:
        for rel in ("engine/server/sv_cof_story.c", "engine/server/sv_cof_party.c", "engine/server/sv_save.c"):
            if digest(archive.read(prefix + rel)) != report["engine_source_sha256"][rel]:
                raise RuntimeError(f"Corresponding source archive mismatch: {rel}")
        for rel in LOCAL_SOURCES:
            path = core.PROJECT_ROOT / rel
            _plain_file(path)
            archive.write(path, f"cof-enhanced-{VERSION}/local-story-tools/{rel}")
        archive.writestr(f"cof-enhanced-{VERSION}/LOCAL-STORY-SOURCE-HASHES.json", json.dumps(report["engine_source_sha256"], indent=2) + "\n")
        archive.writestr(f"cof-enhanced-{VERSION}/EXPERIMENTAL-STORY.txt", EXPERIMENTAL_NOTICE)
        archive.writestr(f"cof-enhanced-{VERSION}/STORY-START.txt", START_REQUIREMENTS)
    player_zip = out / "package" / f"cof-enhanced-{VERSION}.zip"
    with zipfile.ZipFile(player_zip, "a", compression=zipfile.ZIP_DEFLATED) as archive:
        forbidden = [name for name in archive.namelist()
                     if Path(name).name.lower() in ("hl.dll", "client.dll") or Path(name).suffix.lower() in (".bsp", ".wad", ".mdl", ".sav", ".hl1", ".hl2")]
        if forbidden:
            raise RuntimeError("Unexpected retail asset in player archive")
        if digest(archive.read("cof-enhanced/files/xash.dll")) != report["binaries"]["xash.dll"]["sha256"]:
            raise RuntimeError("Packaged engine binary differs from the staged build")
        archive.writestr("EXPERIMENTAL-STORY.txt", EXPERIMENTAL_NOTICE)
        archive.writestr("STORY-START.txt", START_REQUIREMENTS)
    archives = {path.name: digest(path.read_bytes()) for path in (player_zip, source_zip)}
    (out / "package/SHA256SUMS.txt").write_text("".join(f"{value}  {name}\n" for name, value in archives.items()), encoding="utf-8")
    report.update(written=True, archives=archives, player_zip=str(player_zip), source_zip=str(source_zip))
    (out / "package-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (out / "EXPERIMENTAL-STORY.txt").write_text(EXPERIMENTAL_NOTICE, encoding="utf-8")
    (out / "STORY-START.txt").write_text(START_REQUIREMENTS, encoding="utf-8")
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=core.PROJECT_ROOT / f"evidence/enhanced-story-package-v{REVISION}")
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args(argv)
    try:
        report = package(args.out) if args.write else inspect_plan(args.out)
        # The full per-file source inventory belongs in the report file, not the console.
        summary = {key: value for key, value in report.items() if key != "engine_source_sha256"}
        summary["source_files"] = len(report["engine_source_sha256"])
        print(json.dumps(summary, indent=2))
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, KeyError, zipfile.BadZipFile) as exc:
        print(f"Package refused: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
