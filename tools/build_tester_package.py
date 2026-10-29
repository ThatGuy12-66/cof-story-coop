"""Build the package: CoF-Story-Coop-<version>.zip with the app exe, the readme and the source.

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

README = ROOT / "tools/player_readme.txt"
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


def player_payload(player_zip: Path, target: Path) -> None:
    """Enhanced's player zip as the app's engine payload, without the files a copy has no use for
    (friend_setup.RETIRED_FILES)."""
    retired = {name.casefold() for name in setup_lib.RETIRED_FILES}
    with zipfile.ZipFile(player_zip) as source, zipfile.ZipFile(target, "x", compression=zipfile.ZIP_DEFLATED) as out:
        for info in source.infolist():
            if info.filename.casefold() not in retired:
                out.writestr(info, source.read(info), compress_type=zipfile.ZIP_DEFLATED)
    with zipfile.ZipFile(target) as archive:
        if archive.testzip() is not None:
            raise RuntimeError("The engine payload failed its CRC check.")


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


ICON = ROOT / "tools/app.ico"   # 1.2.1: the app's own icon (tools/make_app_icon.py), not PyInstaller's stock one


def version_info() -> str:
    """The exe's Windows version details (Properties > Details), for PyInstaller's --version-file (1.2.1: an exe
    with none, PyInstaller's stock icon and a self-unpacking body was flagged as a trojan by antivirus programs)."""
    parts = [int(p) for p in play.VERSION_LABEL.split(".")][:3]
    numbers = tuple(parts + [0] * (3 - len(parts)) + [play.BUILD])
    strings = {"CompanyName": "Cry of Fear Story Co-op", "FileDescription": "Cry of Fear Story Co-op",
               "FileVersion": f"{play.VERSION_LABEL} (build {play.BUILD})", "InternalName": "CoF Story Co-op",
               "LegalCopyright": "GPL-3.0-or-later",
               "OriginalFilename": setup_lib.APP_EXE, "ProductName": "Cry of Fear Story Co-op",
               "ProductVersion": play.VERSION_LABEL}
    rows = ",\n".join(f"          StringStruct({k!r}, {v!r})" for k, v in strings.items())
    return (f"VSVersionInfo(\n  ffi=FixedFileInfo(filevers={numbers}, prodvers={numbers}, mask=0x3f, flags=0x0, "
            f"OS=0x40004, fileType=0x1, subtype=0x0, date=(0, 0)),\n  kids=[\n    StringFileInfo([\n"
            f"      StringTable('040904B0', [\n{rows}])]),\n"
            f"    VarFileInfo([VarStruct('Translation', [1033, 1200])])\n  ]\n)\n")


def pyinstaller_command(out: Path, payload: Path) -> list:
    """The one-folder app (1.2.1; it was one file, unpacked to a temporary folder at every start): the window, the
    payload, and the merged-map group list (tools.map_merge.groups_path), in setup_lib.APP_FILES next to the exe;
    its own icon and version details; no UPX."""
    return [sys.executable, "-m", "PyInstaller", "--noconfirm", "--onedir", "--windowed", "--noupx",
            "--contents-directory", setup_lib.APP_FILES, "--icon", str(ICON),
            "--version-file", str(out / "version-info.txt"),
            "--name", Path(setup_lib.APP_EXE).stem, "--paths", str(ROOT),
            "--add-data", f"{payload}{os.pathsep}payload",
            "--add-data", f"{ROOT / 'tools/map_groups.json'}{os.pathsep}tools",
            "--add-data", f"{ICON}{os.pathsep}tools",
            "--distpath", str(out / "dist"), "--workpath", str(out / "work"), "--specpath", str(out),
            str(ROOT / "tools/coop_app.py")]


def build(out: Path) -> dict:
    problems = {name: lines for name, path in REPOS.items() if (lines := dirty(path))}
    if problems:
        raise ValueError(f"Commit everything first, so the source matches the build: {problems}")
    out = Path(out).resolve()
    out.mkdir(parents=True, exist_ok=False)
    stage = enhanced.package(out / "enhanced")
    payload = out / "payload"
    payload.mkdir()
    player_payload(Path(stage["player_zip"]), payload / setup_lib.PAYLOAD_ZIP)
    manifest = {"schema": 2, "version": play.VERSION, "build": play.BUILD,
                "xash_sha256": stage["binaries"]["xash.dll"]["sha256"],
                # 1.3.7: how this package makes the models and the art; an update made the same way keeps them
                "recipes": setup_lib.recipes(ROOT)}
    (payload / setup_lib.PAYLOAD_JSON).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    setup_lib.inspect_payload(payload)
    (out / "version-info.txt").write_text(version_info(), encoding="utf-8")
    command = pyinstaller_command(out, payload)
    with (out / "pyinstaller.log").open("wb") as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    exe = out / "dist" / Path(setup_lib.APP_EXE).stem / setup_lib.APP_EXE
    subprocess.run([str(exe), "--self-test"], check=True, timeout=120)
    bundle = out / "bundle"
    (bundle / "source-and-licences").mkdir(parents=True)
    shutil.copyfile(exe, bundle / setup_lib.APP_EXE)
    shutil.copytree(exe.parent / setup_lib.APP_FILES, bundle / setup_lib.APP_FILES)
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
