"""Build the tester package: CoF-Story-Coop-<version>.zip with the app exe, the readme and the source.

Nothing here launches the game. Build the engine first (tools\\build_enhanced.cmd) and
commit every repository: the source archive must match the binaries.
Run: python -m tools.build_tester_package --out evidence\\<new folder>
"""
from __future__ import annotations

import argparse
from importlib.metadata import distribution
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import coop_play as play  # noqa: E402
from tools import friend_setup as setup_lib  # noqa: E402
from tools import package_enhanced_story as enhanced  # noqa: E402

README = ROOT / "tools/tester_readme.txt"
REPOS = {"launcher": ROOT, "engine": enhanced.ENGINE, "freevgui": enhanced.ENGINE / "3rdparty/freevgui"}
ZIP_NAME = f"CoF-Story-Coop-{play.VERSION_LABEL.replace(' ', '')}.zip"


def digest(path: Path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def dirty(repo: Path) -> list[str]:
    result = subprocess.run(["git", "-C", str(repo), "status", "--porcelain"], capture_output=True, text=True, check=True)
    return [line for line in result.stdout.splitlines() if line.strip()]


def write_zip(folder: Path, path: Path) -> None:
    with zipfile.ZipFile(path, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for item in sorted(folder.rglob("*")):
            if item.is_file():
                archive.write(item, item.relative_to(folder).as_posix())
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None:
            raise RuntimeError("The package zip failed its CRC check.")


def notices(target: Path) -> None:
    """The licences of what PyInstaller packs into the exe: Python, Tk and PyInstaller's bootloader."""
    target.mkdir(parents=True)
    licences = {"PYTHON-LICENSE.txt": Path(sys.base_prefix) / "LICENSE_PYTHON.txt",
                "TK-LICENSE.txt": Path(sys.base_prefix) / "Library/lib/tk8.6/license.terms"}
    package = distribution("pyinstaller")
    licences["PYINSTALLER-COPYING.txt"] = next(Path(package.locate_file(f)) for f in package.files
                                               if str(f).endswith("licenses/COPYING.txt"))
    for name, path in licences.items():
        shutil.copyfile(path, target / name)


def build(out: Path) -> dict:
    problems = {name: lines for name, path in REPOS.items() if (lines := dirty(path))}
    if problems:
        raise ValueError(f"Commit everything first, so the source matches the build: {problems}")
    out = Path(out).resolve()
    out.mkdir(parents=True, exist_ok=False)
    stage = enhanced.package(out / "enhanced")
    payload = out / "payload"
    payload.mkdir()
    shutil.copyfile(stage["player_zip"], payload / setup_lib.PAYLOAD_ZIP)
    manifest = {"schema": 2, "version": play.VERSION, "build": play.BUILD,
                "xash_sha256": stage["binaries"]["xash.dll"]["sha256"]}
    (payload / setup_lib.PAYLOAD_JSON).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    setup_lib.inspect_payload(payload)
    command = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--onefile", "--windowed",
               "--name", Path(setup_lib.APP_EXE).stem, "--paths", str(ROOT),
               "--add-data", f"{payload}{os.pathsep}payload",
               "--distpath", str(out / "dist"), "--workpath", str(out / "work"), "--specpath", str(out),
               str(ROOT / "tools/coop_app.py")]
    with (out / "pyinstaller.log").open("wb") as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    exe = out / "dist" / setup_lib.APP_EXE
    subprocess.run([str(exe), "--self-test"], check=True, timeout=120)
    bundle = out / "bundle"
    (bundle / "source-and-licences").mkdir(parents=True)
    shutil.copyfile(exe, bundle / setup_lib.APP_EXE)
    shutil.copyfile(README, bundle / "READ ME FIRST.txt")
    shutil.copyfile(stage["source_zip"], bundle / "source-and-licences/corresponding-source.zip")
    notices(bundle / "source-and-licences/notices")
    path = out / ZIP_NAME
    write_zip(bundle, path)
    commits = {name: subprocess.run(["git", "-C", str(repo), "rev-parse", "HEAD"], capture_output=True, text=True,
                                    check=True).stdout.strip() for name, repo in REPOS.items()}
    report = {"zip": str(path), "zip_sha256": digest(path), "zip_bytes": path.stat().st_size,
              "exe_sha256": digest(exe), "payload": manifest,
              "vgui_sha256": stage["binaries"]["vgui.dll"]["sha256"], "commits": commits}
    (out / "package-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, required=True, help="a new folder under evidence\\")
    args = parser.parse_args(argv)
    try:
        print(json.dumps(build(args.out), indent=2))
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Package refused: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
