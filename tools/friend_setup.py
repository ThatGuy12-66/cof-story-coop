"""Private-copy installer for the story co-op tester app: setup and update.

The window is tools/coop_app.py. The package contains code and Enhanced
payloads only: every retail asset, both Simon models and the 12-slot inventory
art are copied or generated on the tester's own computer from their own game.
"""
from __future__ import annotations

from datetime import datetime
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import zipfile

if not getattr(sys, "frozen", False):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools import coop_play as play
from tools import make_inventory12_art as art
from tools.prepare_simon_models import build_models, costume_files, validate_model

STOCK_DLL_SHA = "0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63"
EXCLUDED_DIRS = {"save", "saves", "logs", "screenshots", "downloads", "download", "cof-enhanced", "cof-enhanced-backup", "experiment-evidence", ".git", "__pycache__"}
EXCLUDED_FILES = {"config.cfg", "userconfig.cfg", "autoexec.cfg", "server.cfg", "listenserver.cfg", "gameinfo.txt", "mapcycle.txt", "motd.txt", "console.log", "qconsole.log", "steam_appid.txt"}
EXCLUDED_SUFFIXES = {".sav", ".hl1", ".hl2", ".hl3", ".log", ".dem", ".ent", ".orig", ".bak", ".tmp", ".dmp", ".pdb"}
APP_EXE = "CoF Story Co-op.exe"
PAYLOAD_ZIP = "engine-payload.zip"
PAYLOAD_JSON = "payload.json"
INSTALLED = "cof-story-installed.json"
# The name in the first config.cfg, before anyone has typed one in the app.
DEFAULT_NAME = "Player"


def digest_file(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def reject_links(path: Path) -> None:
    for part in (path, *path.parents):
        if part.is_symlink() or (hasattr(part, "is_junction") and part.is_junction()):
            raise ValueError(f"Choose an ordinary folder without links: {part}")


def validate_source(path: Path) -> Path:
    raw = Path(path).expanduser().absolute()
    reject_links(raw)
    root = raw.resolve()
    for rel in ("CoFLaunchApp.exe", "cryoffear/liblist.gam", "cryoffear/maps/c_game_menu1.bsp", "cryoffear/cl_dlls/hl.dll"):
        item = root / rel
        reject_links(item)
        if not item.is_file():
            raise ValueError("Select the Cry of Fear game folder containing CoFLaunchApp.exe and cryoffear.")
    if digest_file(root / "cryoffear/cl_dlls/hl.dll") != STOCK_DLL_SHA:
        raise ValueError("The source game DLL differs from retail Cry of Fear 1.6. Select an unmodified installation.")
    return root


def validate_destination(source: Path, path: Path) -> Path:
    raw = Path(path).expanduser().absolute()
    reject_links(raw)
    target = raw.resolve()
    if target.exists():
        raise ValueError("Choose a new destination folder. Existing folders are never replaced.")
    if target.is_relative_to(source) or source.is_relative_to(target):
        raise ValueError("The separate game folder must be outside the original game.")
    if any(part.casefold() == "steamapps" for part in target.parts):
        raise ValueError("The destination must be outside Steam's managed folders.")
    return target


def should_copy(relative: Path) -> bool:
    parts = [part.casefold() for part in relative.parts]
    if any(part in EXCLUDED_DIRS for part in parts[:-1]):
        return False
    name = parts[-1]
    if name in EXCLUDED_FILES or relative.suffix.casefold() in EXCLUDED_SUFFIXES:
        return False
    if any(marker in name for marker in (".cfg.", ".log.", ".sav.", ".bak.")) or relative.suffix.casefold() in (".json", ".vdf"):
        return False
    if name.startswith(("cofcoop_", "cof_story_", "story-local", "story-host", "story-join", "fixture", "cof-enhanced-", "cof_probe_")):
        return False
    if len(parts) >= 3 and parts[:2] == ["cryoffear", "models"] and "player" in parts and any(part.startswith("cof_s") for part in parts):
        return False
    # Map cfgs contain original campaign behavior; arbitrary root/user cfgs do not.
    if (relative.suffix.casefold() == ".cfg" and parts[:2] != ["cryoffear", "maps"]
            and relative.as_posix().casefold() != "cryoffear/skill.cfg"):
        return False
    return True


def copy_plan(source: Path) -> list[tuple[Path, int]]:
    result = []
    for directory, dirs, files in os.walk(source, followlinks=False):
        current = Path(directory)
        dirs[:] = [name for name in dirs if name.casefold() not in EXCLUDED_DIRS]
        for name in dirs:
            reject_links(current / name)
        for name in files:
            path = current / name
            relative = path.relative_to(source)
            if not should_copy(relative):
                continue
            reject_links(path)
            if not path.is_file():
                raise ValueError(f"Not a regular game file: {relative}")
            result.append((relative, path.stat().st_size))
    return result


def copy_assets(source: Path, destination: Path, plan, progress=lambda *_: None) -> None:
    done, total = 0, sum(size for _, size in plan)
    for relative, expected_size in plan:
        src, dst = source / relative, destination / relative
        reject_links(src)
        reject_links(dst)
        dst.parent.mkdir(parents=True, exist_ok=True)
        source_hash = hashlib.sha256()
        with src.open("rb") as reader, dst.open("xb") as writer:
            for block in iter(lambda: reader.read(1024 * 1024), b""):
                writer.write(block)
                source_hash.update(block)
        if dst.stat().st_size != expected_size or digest_file(dst) != source_hash.hexdigest():
            raise RuntimeError(f"Copy verification failed: {relative}")
        done += expected_size
        progress(f"Copying game files: {done // (1024 * 1024)} / {total // (1024 * 1024)} MB", .85 * done / max(total, 1))


def steam_candidates(roots=None) -> list[Path]:
    if roots is None:
        roots = []
        if os.name == "nt":
            import winreg
            try:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
                    roots.append(Path(winreg.QueryValueEx(key, "SteamPath")[0]))
            except OSError:
                pass
            roots.append(Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Steam")
    candidates = []
    for root in dict.fromkeys(roots):
        libraries = [root]
        try:
            metadata = (root / "steamapps/libraryfolders.vdf").read_text(encoding="utf-8", errors="replace")
            libraries += [Path(value.replace("\\\\", "\\")) for value in re.findall(r'"path"\s+"((?:\\.|[^"\\])*)"', metadata)]
        except OSError:
            pass
        for library in dict.fromkeys(libraries):
            try:
                manifest = (library / "steamapps/appmanifest_223710.acf").read_text(encoding="utf-8", errors="replace")
                found = re.search(r'"installdir"\s+"([^"\r\n]+)"', manifest)
                if found and found[1] not in (".", "..") and not any(c in found[1] for c in "/\\:"):
                    candidates.append(library / "steamapps/common" / found[1])
            except OSError:
                continue
    return list(dict.fromkeys(candidates))


def safe_zip_members(archive: zipfile.ZipFile):
    seen = set()
    for info in archive.infolist():
        name = info.filename
        path = PurePosixPath(name)
        if "\\" in info.orig_filename or ":" in name or path.is_absolute() or ".." in path.parts or not path.parts:
            raise ValueError("Invalid archive path")
        folded = name.casefold().rstrip("/")
        if folded in seen or stat.S_ISLNK(info.external_attr >> 16):
            raise ValueError("Duplicate or linked archive entry")
        seen.add(folded)
        yield info, path


def digest_file_from_archive(archive, name):
    return hashlib.sha256(archive.read(name)).hexdigest()


def inspect_payload(folder: Path) -> dict:
    """The engine payload packed into the app: its manifest, checked against the engine inside."""
    try:
        manifest = json.loads((Path(folder) / PAYLOAD_JSON).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        raise ValueError("This package is incomplete. Download it again.") from None
    if not isinstance(manifest, dict) or manifest.get("schema") != 2 or not isinstance(manifest.get("build"), int):
        raise ValueError("This package is incomplete. Download it again.")
    try:
        with zipfile.ZipFile(Path(folder) / PAYLOAD_ZIP) as archive:
            list(safe_zip_members(archive))
            engine = digest_file_from_archive(archive, "cof-enhanced/files/xash.dll")
    except (OSError, KeyError, zipfile.BadZipFile):
        raise ValueError("This package is damaged. Download it again.") from None
    if engine != manifest.get("xash_sha256"):
        raise ValueError("This package is damaged. Download it again.")
    return manifest


def extract_payload(payload: Path, destination: Path, *, overwrite: bool = False) -> None:
    with zipfile.ZipFile(payload) as archive:
        members = list(safe_zip_members(archive))
        for info, rel in members:
            dst = destination.joinpath(*rel.parts)
            reject_links(dst)
            if info.is_dir():
                dst.mkdir(parents=True, exist_ok=True)
            else:
                dst.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(info) as reader, dst.open("wb" if overwrite else "xb") as writer:
                    shutil.copyfileobj(reader, writer)


def run_installer(destination: Path) -> None:
    """The Enhanced installer from the payload; it upgrades an earlier install in place."""
    shell = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    env = os.environ.copy()
    env["PSModulePath"] = str(shell.parent / "Modules")
    with (destination / "friend-setup-install.log").open("wb") as log:
        result = subprocess.run([str(shell), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
                                 str(destination / "cof-enhanced/install.ps1"), "-GameDir", str(destination)],
                                cwd=destination, env=env, stdout=log, stderr=subprocess.STDOUT,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), check=False)
    if result.returncode:
        raise RuntimeError("The engine could not be installed. See friend-setup-install.log in the copy.")


def make_art(root: Path) -> None:
    """The 12-slot inventory's background art, from this copy's own retail images."""
    try:
        art.generate(root)
    except SystemExit as error:
        raise RuntimeError(f"The inventory art could not be made: {error}") from None


def generated_models(models: Path) -> dict[str, bytes]:
    """Everything the generators make from a copy's own stock models, keyed by path under cryoffear."""
    return build_models((models / "cutscene/player.mdl").read_bytes(), (models / "player/police1/police1.mdl").read_bytes(),
                        {rel: (models / "costumes" / rel).read_bytes() for rel in costume_files()})


def make_models(root: Path) -> dict[str, str]:
    """(Re)make the generated models in a copy from its own files; an update replaces older generator output."""
    hashes = {}
    for rel, data in generated_models(root / "cryoffear/models").items():
        if not rel.endswith(".mdl"):
            continue
        validate_model(data)
        target = root / "cryoffear" / rel
        reject_links(target)
        target.parent.mkdir(parents=True, exist_ok=True)
        partial = target.with_name(target.name + ".partial")
        partial.write_bytes(data)
        os.replace(partial, target)
        expected = hashlib.sha256(data).hexdigest()
        if digest_file(target) != expected:
            raise RuntimeError("Generated model verification failed.")
        hashes[rel] = expected
    return hashes


def default_controls() -> str:
    binds = {"w": "+forward", "s": "+back", "a": "+moveleft", "d": "+moveright", "SPACE": "+jump",
             "CTRL": "+duck", "SHIFT": "+sprint", "ALT": "+dodge", "e": "+use", "r": "+reload",
             "TAB": "+inventory", "b": "+objectives", "f": "flashlight", "z": "weapontoggle",
             "MOUSE1": "+attack", "MOUSE2": "+attack3", "MOUSE3": "+attack2", "ESCAPE": "cancelselect",
             "1": "quicksel 1", "2": "quicksel 2", "3": "quicksel 3", "y": "messagemode", "k": "+voicerecord",
             "j": "cof_warp", "h": "cof_companion_order", "ENTER": "cof_skip", "KP_ENTER": "cof_skip"}
    return "\n".join(f'bind "{key}" "{value}"' for key, value in binds.items()) + "\n"


def keep_previous_log(root: Path, name: str) -> Path | None:
    """The engine opens -log files with "w+": move the last one into story-logs first."""
    log = root / name
    if not log.is_file():
        return None
    archive = root / "story-logs"
    reject_links(archive / name)
    archive.mkdir(exist_ok=True)
    stamp = datetime.fromtimestamp(log.stat().st_mtime).strftime("%Y%m%d-%H%M%S")
    stem = Path(name).stem
    kept = archive / f"{stem}-{stamp}.log"
    number = 1
    while kept.exists():
        number += 1
        kept = archive / f"{stem}-{stamp}-{number}.log"
    log.replace(kept)
    return kept


def installed_state(root: Path) -> dict | None:
    """The record setup writes into a copy, or None when the folder holds no copy made by this app."""
    try:
        state = json.loads((Path(root) / INSTALLED).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if not isinstance(state, dict) or state.get("schema") != 2 or not isinstance(state.get("build"), int):
        return None
    return state


SHORTCUT_SCRIPT = (
    "$shell = New-Object -ComObject WScript.Shell; "
    "foreach ($folder in @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs'))) { "
    "$link = $shell.CreateShortcut((Join-Path $folder 'Cry of Fear Co-op.lnk')); "
    "$link.TargetPath = $env:COF_APP_EXE; $link.WorkingDirectory = $env:COF_APP_DIR; "
    "$link.IconLocation = $env:COF_APP_ICON + ',0'; $link.Description = 'Cry of Fear story co-op'; $link.Save() }")


def write_shortcuts(root: Path) -> bool:
    """Desktop and Start-menu shortcuts; paths go through the environment, never into the script."""
    env = dict(os.environ, COF_APP_EXE=str(root / APP_EXE), COF_APP_DIR=str(root), COF_APP_ICON=str(root / "CoFLaunchApp.exe"))
    shell = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    result = subprocess.run([str(shell), "-NoProfile", "-NonInteractive", "-Command", SHORTCUT_SCRIPT], env=env,
                            capture_output=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), check=False)
    return result.returncode == 0


def finish(root: Path, manifest: dict, models: dict, source_game: str, exe: Path, shortcuts: bool, progress) -> None:
    """The parts setup and update share: the story cfg, the app in the copy, the installed record, shortcuts."""
    (root / "cryoffear" / play.STORY_CFG).write_text(play.story_config(), encoding="ascii")
    target = root / APP_EXE
    if Path(exe).resolve() != target.resolve():
        shutil.copyfile(exe, target)
    state = {"schema": 2, "version": manifest["version"], "build": manifest["build"],
             "xash_sha256": manifest["xash_sha256"], "models": models, "source_game": source_game,
             "campaign_verified": False}
    (root / INSTALLED).write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    if shortcuts and not write_shortcuts(root):
        progress(f"Done, but the desktop shortcut could not be made: start {APP_EXE} in {root}.", 1)
        return
    progress("Done. Use the Cry of Fear Co-op shortcut on your desktop or in the Start menu.", 1)


def setup(source: Path, destination: Path, payload: Path, *, exe: Path, progress=lambda *_: None, shortcuts=True) -> Path:
    source = validate_source(source)
    destination = validate_destination(source, destination)
    payload = Path(payload)
    manifest = inspect_payload(payload)
    progress("Checking game files and disk space", 0)
    plan = copy_plan(source)
    with zipfile.ZipFile(payload / PAYLOAD_ZIP) as archive:
        package_paths = {info.filename.casefold() for info in archive.infolist() if not info.is_dir()}
    plan = [(relative, size) for relative, size in plan if relative.as_posix().casefold() not in package_paths]
    ancestor = destination.parent
    while not ancestor.exists():
        ancestor = ancestor.parent
    needed = sum(size for _, size in plan) + 600 * 1024 * 1024
    if shutil.disk_usage(ancestor).free < needed:
        raise ValueError(f"The destination needs at least {needed / (1024 ** 3):.1f} GB free.")
    destination.mkdir(parents=True, exist_ok=False)
    (destination / "SETUP-INCOMPLETE.txt").write_text("Setup is incomplete. Choose a new folder to retry; no original game files were changed.\n")
    copy_assets(source, destination, plan, progress)
    progress("Installing the co-op engine", .86)
    extract_payload(payload / PAYLOAD_ZIP, destination)
    run_installer(destination)
    if digest_file(destination / "xash.dll") != manifest["xash_sha256"]:
        raise RuntimeError("Installed engine hash differs from the package.")
    if digest_file(destination / "cryoffear/cl_dlls/hl.dll") != STOCK_DLL_SHA:
        raise RuntimeError("The copied retail game DLL changed unexpectedly.")
    progress("Making the Simon and weapon models from your game files", .92)
    model_hashes = make_models(destination)
    progress("Making the inventory art", .96)
    make_art(destination)
    (destination / "cryoffear/config.cfg").write_text(default_controls() + f'name "{DEFAULT_NAME}"\nsensitivity 4\n', encoding="ascii")
    (destination / "steam_appid.txt").write_text("223710\n", encoding="ascii")
    (destination / "SETUP-INCOMPLETE.txt").unlink()
    finish(destination, manifest, model_hashes, str(source), exe, shortcuts, progress)
    return destination


def update(root: Path, payload: Path, *, exe: Path, progress=lambda *_: None, shortcuts=True) -> Path:
    """Lay a newer package over a copy made by this app: saves, settings and models stay."""
    root = Path(root).resolve()
    state = installed_state(root)
    if not state:
        raise ValueError("This folder is not a story co-op copy made by this app.")
    if game_is_running(root):
        raise ValueError("Close Cry of Fear first, then update.")
    payload = Path(payload)
    manifest = inspect_payload(payload)
    progress("Installing the new engine", .2)
    extract_payload(payload / PAYLOAD_ZIP, root, overwrite=True)
    run_installer(root)
    if digest_file(root / "xash.dll") != manifest["xash_sha256"]:
        raise RuntimeError("Installed engine hash differs from the package.")
    progress("Making the Simon and weapon models", .6)
    models = make_models(root)
    progress("Making the inventory art", .7)
    make_art(root)
    finish(root, manifest, models, state["source_game"], exe, shortcuts, progress)
    return root


def validate_runtime(runtime: Path) -> Path:
    """A copy made by this app, with the engine and models it was set up with."""
    raw = Path(runtime).absolute()
    reject_links(raw)
    root = raw.resolve()
    if any(part.casefold() == "steamapps" for part in root.parts):
        raise ValueError("Use the separate game copy created by setup.")
    state = installed_state(root)
    message = f"This game copy does not match its install. Run the newest {APP_EXE} and click Update."
    if not state or not (root / "xash.dll").is_file() or digest_file(root / "xash.dll") != state.get("xash_sha256"):
        raise ValueError(message)
    for rel, expected in state.get("models", {}).items():
        path = PurePosixPath(rel)
        if path.is_absolute() or ".." in path.parts or "\\" in rel or ":" in rel:
            raise ValueError("Invalid installed model path")
        model = root / "cryoffear" / rel
        reject_links(model)
        if not model.is_file() or digest_file(model) != expected:
            raise ValueError("A generated Simon model is missing or changed. " + message)
    return root


def game_is_running(runtime: Path) -> bool:
    """A Cry of Fear process started from this copy (another copy's game does not count)."""
    if os.name != "nt":
        return False
    shell = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    command = ('Get-CimInstance Win32_Process -Filter "Name=\'cof.exe\' OR Name=\'CoFLaunchApp.exe\'" | '
               'Select-Object ProcessId,Name,ExecutablePath | ConvertTo-Json -Compress')
    result = subprocess.run([str(shell), "-NoProfile", "-NonInteractive", "-Command", command],
                            capture_output=True, text=True, check=True, timeout=20,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    rows = json.loads(result.stdout) if result.stdout.strip() else []
    if isinstance(rows, dict):
        rows = [rows]
    prefix = (str(Path(runtime)) + os.sep).casefold()
    return any(str(row.get("ExecutablePath") or "").casefold().startswith(prefix) for row in rows)
