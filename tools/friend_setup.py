"""Private-copy installer and join window for the experimental CoF story build.

The distributable contains code/Enhanced payloads only. Every retail asset and
both Simon models are copied/generated on the recipient's own computer.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import queue
import re
import shutil
import stat
import subprocess
import sys
import threading
import zipfile

if not getattr(sys, "frozen", False):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cofcoop.core import validate_endpoint
from tools.prepare_simon_models import build_models, TEXTURES, validate_model

STOCK_DLL_SHA = "0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63"
EXCLUDED_DIRS = {"save", "saves", "logs", "screenshots", "downloads", "download", "cof-enhanced", "cof-enhanced-backup", "experiment-evidence", ".git", "__pycache__"}
EXCLUDED_FILES = {"config.cfg", "userconfig.cfg", "autoexec.cfg", "server.cfg", "listenserver.cfg", "gameinfo.txt", "mapcycle.txt", "motd.txt", "console.log", "qconsole.log", "steam_appid.txt"}
EXCLUDED_SUFFIXES = {".sav", ".hl1", ".hl2", ".hl3", ".log", ".dem", ".ent", ".orig", ".bak", ".tmp", ".dmp", ".pdb"}
JOIN_CFG = "cof_story_join.cfg"
# The game's log for bug reports, in the game folder. The engine opens -log files
# with "w+", so each join first moves the previous one into story-logs\.
TESTER_LOG = "story-tester.log"
# Written by setup before anyone has typed a name; the join window starts blank
# and refuses to join until a name is entered.
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
        progress(f"Copying game assets: {done // (1024 * 1024)} / {total // (1024 * 1024)} MB", done / max(total, 1))


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


def inspect_bundle(folder: Path) -> dict:
    manifest = json.loads((folder / "bundle.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or manifest.get("protocol") != 49:
        raise ValueError("Unsupported or incomplete friend bundle")
    for name in ("engine-payload.zip", "corresponding-source.zip"):
        path = folder / name
        reject_links(path)
        if digest_file(path) != manifest["sha256"][name]:
            raise ValueError(f"Bundle hash mismatch: {name}. Extract the complete download again.")
    with zipfile.ZipFile(folder / "engine-payload.zip") as archive:
        list(safe_zip_members(archive))
        if digest_file_from_archive(archive, "cof-enhanced/files/xash.dll") != manifest["xash_sha256"]:
            raise ValueError("Engine payload does not match this bundle")
    return manifest


def digest_file_from_archive(archive, name):
    return hashlib.sha256(archive.read(name)).hexdigest()


def extract_payload(payload: Path, destination: Path) -> None:
    with zipfile.ZipFile(payload) as archive:
        members = list(safe_zip_members(archive))
        for info, rel in members:
            dst = destination.joinpath(*rel.parts)
            reject_links(dst)
            if info.is_dir():
                dst.mkdir(parents=True, exist_ok=True)
            else:
                dst.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(info) as reader, dst.open("xb") as writer:
                    shutil.copyfileobj(reader, writer)


def default_controls() -> str:
    binds = {"w": "+forward", "s": "+back", "a": "+moveleft", "d": "+moveright", "SPACE": "+jump",
             "CTRL": "+duck", "SHIFT": "+sprint", "ALT": "+dodge", "e": "+use", "r": "+reload",
             "TAB": "+inventory", "b": "+objectives", "f": "flashlight", "z": "weapontoggle",
             "MOUSE1": "+attack", "MOUSE2": "+attack3", "MOUSE3": "+attack2", "ESCAPE": "cancelselect",
             "1": "quicksel 1", "2": "quicksel 2", "3": "quicksel 3", "y": "messagemode", "k": "+voicerecord",
             "j": "cof_warp", "ENTER": "cof_skip", "KP_ENTER": "cof_skip"}
    return "\n".join(f'bind "{key}" "{value}"' for key, value in binds.items()) + "\n"


def safe_console(value: str, limit: int, label: str) -> str:
    if not isinstance(value, str) or len(value) > limit or any(ord(c) < 32 or ord(c) > 126 or c in '\\";' for c in value):
        raise ValueError(f"{label} contains unsupported characters or is too long.")
    return value


def join_config(name=DEFAULT_NAME, password="") -> str:
    name = safe_console(name.strip(), 24, "Player name")
    password = safe_console(password, 64, "Password")
    if not name:
        raise ValueError("Enter a player name.")
    return (f'name "{name}"\npassword "{password}"\nfps_max 60\nrate 50000\ncl_cmdrate 60\ncl_updaterate 60\n'
            'cl_forwardspeed 300\ncl_backspeed 200\ncl_sidespeed 200\nsensitivity 4\n+mlook\n')


def join_arguments(runtime: Path, address: str) -> list[str]:
    endpoint = validate_endpoint(address.strip())
    return [str(runtime / "CoFLaunchApp.exe"), "-windowed", "-width", "960", "-height", "540",
            "-log", TESTER_LOG, "-logtime", "+exec", JOIN_CFG, "+connect", endpoint]


def keep_previous_log(root: Path) -> Path | None:
    log = root / TESTER_LOG
    if not log.is_file():
        return None
    archive = root / "story-logs"
    reject_links(archive / TESTER_LOG)
    archive.mkdir(exist_ok=True)
    stamp = datetime.fromtimestamp(log.stat().st_mtime).strftime("%Y%m%d-%H%M%S")
    kept = archive / f"story-tester-{stamp}.log"
    number = 1
    while kept.exists():
        number += 1
        kept = archive / f"story-tester-{stamp}-{number}.log"
    log.replace(kept)
    return kept


def setup(source: Path, destination: Path, bundle: Path, *, progress=lambda *_: None) -> Path:
    source = validate_source(source)
    destination = validate_destination(source, destination)
    manifest = inspect_bundle(bundle)
    progress("Checking game files and disk space", 0)
    plan = copy_plan(source)
    with zipfile.ZipFile(bundle / "engine-payload.zip") as archive:
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
    progress("Installing the matching Enhanced engine", .86)
    extract_payload(bundle / "engine-payload.zip", destination)
    shell = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    env = os.environ.copy()
    env["PSModulePath"] = str(shell.parent / "Modules")
    with (destination / "friend-setup-install.log").open("wb") as log:
        result = subprocess.run([str(shell), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
                                 str(destination / "cof-enhanced/install.ps1"), "-GameDir", str(destination)],
                                cwd=destination, env=env, stdout=log, stderr=subprocess.STDOUT,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), check=False)
    if result.returncode:
        raise RuntimeError("Enhanced installation failed. See friend-setup-install.log in the separate copy.")
    if digest_file(destination / "xash.dll") != manifest["xash_sha256"]:
        raise RuntimeError("Installed engine hash differs from the bundle.")
    if digest_file(destination / "cryoffear/cl_dlls/hl.dll") != STOCK_DLL_SHA:
        raise RuntimeError("The copied retail game DLL changed unexpectedly.")
    progress("Generating both Simon models from your game assets", .93)
    models = destination / "cryoffear/models"
    generated = build_models((models / "cutscene/player.mdl").read_bytes(), (models / "player/police1/police1.mdl").read_bytes(),
                             {name: (models / "costumes/Sick_Simon" / name).read_bytes() for name, _ in TEXTURES.values()})
    model_hashes = {}
    for rel, data in generated.items():
        if not rel.endswith(".mdl"):
            continue
        validate_model(data)
        target = destination / "cryoffear" / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as stream:
            stream.write(data)
        expected = hashlib.sha256(data).hexdigest()
        if digest_file(target) != expected:
            raise RuntimeError("Generated model verification failed.")
        model_hashes[rel] = expected
    cfg = destination / "cryoffear"
    (cfg / "config.cfg").write_text(default_controls() + join_config(), encoding="ascii")
    (cfg / JOIN_CFG).write_text(join_config(), encoding="ascii")
    (destination / "steam_appid.txt").write_text("223710\n", encoding="ascii")
    exe = Path(sys.executable) if getattr(sys, "frozen", False) else bundle / "CoF-Story-Setup.exe"
    shutil.copyfile(exe, destination / "CoF-Story-Join.exe")
    (destination / "JOIN.cmd").write_text('@echo off\r\nstart "" "%~dp0CoF-Story-Join.exe" --join "%~dp0."\r\n', encoding="ascii")
    installed = {"schema": 1, "protocol": 49, "xash_sha256": manifest["xash_sha256"], "models": model_hashes,
                 "player_name": DEFAULT_NAME, "source_game": str(source), "campaign_verified": False}
    (destination / "cof-story-installed.json").write_text(json.dumps(installed, indent=2) + "\n", encoding="utf-8")
    (destination / "SETUP-INCOMPLETE.txt").unlink()
    progress("Setup complete. Enter the host address and password to join.", 1)
    return destination


def validate_join_runtime(runtime: Path) -> Path:
    raw = Path(runtime).absolute()
    reject_links(raw)
    root = raw.resolve()
    if any(part.casefold() == "steamapps" for part in root.parts):
        raise ValueError("Use the separate game copy created by setup.")
    state = json.loads((root / "cof-story-installed.json").read_text(encoding="utf-8"))
    if state.get("schema") != 1 or state.get("protocol") != 49 or digest_file(root / "xash.dll") != state["xash_sha256"]:
        raise ValueError("This game copy does not match the installed story build.")
    for rel, expected in state["models"].items():
        path = PurePosixPath(rel)
        if path.is_absolute() or ".." in path.parts or "\\" in rel or ":" in rel:
            raise ValueError("Invalid installed model path")
        model = root / "cryoffear" / rel
        reject_links(model)
        if digest_file(model) != expected:
            raise ValueError("A generated Simon model is missing or changed. Run setup into a new folder.")
    return root


def game_is_running(runtime: Path) -> bool:
    if os.name != "nt":
        return False
    shell = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    command = 'Get-CimInstance Win32_Process -Filter "Name=\'cof.exe\' OR Name=\'CoFLaunchApp.exe\'" | Select-Object ProcessId,Name | ConvertTo-Json -Compress'
    result = subprocess.run([str(shell), "-NoProfile", "-NonInteractive", "-Command", command],
                            capture_output=True, text=True, check=True, timeout=20,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    rows = json.loads(result.stdout) if result.stdout.strip() else []
    if isinstance(rows, dict):
        rows = [rows]
    return bool(rows)


def open_window(runtime=None):
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    window = tk.Tk()
    window.title("Cry of Fear — story co-op")
    window.geometry("690x450")
    window.minsize(620, 420)
    panel = ttk.Frame(window, padding=22)
    panel.pack(fill="both", expand=True)
    ttk.Label(panel, text="Play story co-op", font=("Segoe UI", 18, "bold")).pack(anchor="w")
    ttk.Label(panel, text="A separate game copy · Simon models generated from your files · 60 FPS", wraplength=620).pack(anchor="w", pady=(4, 18))
    status = tk.StringVar(value="Select your installed Cry of Fear folder.")
    progress = ttk.Progressbar(panel, maximum=100)
    messages = queue.Queue()
    process = None
    working = False
    bundle = Path(sys.executable).parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parent
    fields = ttk.Frame(panel)
    fields.pack(fill="x")

    def row(label, initial="", hidden=False):
        holder = ttk.Frame(fields)
        holder.pack(fill="x", pady=5)
        ttk.Label(holder, text=label, width=15).pack(side="left")
        value = tk.StringVar(value=initial)
        ttk.Entry(holder, textvariable=value, show="*" if hidden else "").pack(side="left", fill="x", expand=True)
        return holder, value

    def show_join(folder):
        nonlocal runtime
        runtime = folder
        for child in fields.winfo_children():
            child.destroy()
        _, address = row("Host address", "")
        _, password = row("Server password", "", True)
        _, name = row("Your name", "")
        def launch():
            nonlocal process
            try:
                # Checked first: a blank name is refused before any process check.
                config = join_config(name.get(), password.get())
                if process is not None and process.poll() is None:
                    raise ValueError("Close your current Cry of Fear window, then Join.")
                root = validate_join_runtime(runtime)
                if game_is_running(root):
                    raise ValueError("Close your current Cry of Fear window, then Join.")
                args = join_arguments(root, address.get())
                path = root / "cryoffear" / JOIN_CFG
                reject_links(path)
                path.write_text(config, encoding="ascii")
                keep_previous_log(root)
                process = subprocess.Popen(args, cwd=root)
                status.set("Game opened in one window. Use this join window again after closing the game.")
            except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
                messagebox.showerror("Cannot join", str(error), parent=window)
        action.configure(text="Join host", command=launch, state="normal")
        status.set(f"Ready: {folder}")

    source_holder, source = row("Original game")
    ttk.Button(source_holder, text="Browse…", command=lambda: source.set(filedialog.askdirectory(parent=window) or source.get())).pack(side="right")
    dest_default = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "CoF-Story"
    dest_holder, destination = row("Separate copy", str(dest_default))
    def choose_destination():
        parent = filedialog.askdirectory(parent=window, title="Choose a parent folder for the new CoF-Story copy")
        if parent:
            destination.set(str(Path(parent) / "CoF-Story"))
    ttk.Button(dest_holder, text="Browse…", command=choose_destination).pack(side="right")
    def install():
        nonlocal working
        working = True
        action.configure(state="disabled")
        src, dst = Path(source.get()), Path(destination.get())
        def worker():
            try:
                result = setup(src, dst, bundle, progress=lambda text, fraction: messages.put(("progress", text, fraction)))
                messages.put(("done", result))
            except Exception as error:
                messages.put(("error", str(error)))
        threading.Thread(target=worker, daemon=True).start()
    action = ttk.Button(panel, text="Set up separate copy", command=install)
    action.pack(anchor="w", pady=18)
    progress.pack(fill="x")
    ttk.Label(panel, textvariable=status, wraplength=620).pack(anchor="w", pady=12)
    ttk.Label(panel, text="Experimental story build. Full campaign compatibility is still being tested.", wraplength=620).pack(anchor="w")
    def poll():
        nonlocal working
        while True:
            try:
                item = messages.get_nowait()
            except queue.Empty:
                break
            if item[0] == "progress":
                status.set(item[1]); progress["value"] = item[2] * 100
            elif item[0] == "done":
                working = False; show_join(item[1])
            elif item[0] == "candidate":
                if not source.get():
                    source.set(str(item[1]))
            else:
                working = False; action.configure(state="normal")
                status.set("Setup stopped; your original game is unchanged.")
                messagebox.showerror("Setup could not finish", item[1], parent=window)
        window.after(100, poll)
    def close():
        if working:
            messagebox.showinfo("Setup is working", "Let the verified copy finish before closing this window.", parent=window)
        else:
            window.destroy()
    window.protocol("WM_DELETE_WINDOW", close)
    if runtime:
        show_join(Path(runtime))
    else:
        def discover():
            candidates = steam_candidates()
            if candidates:
                messages.put(("candidate", candidates[0]))
        threading.Thread(target=discover, daemon=True).start()
    poll()
    window.mainloop()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--join", type=Path)
    parser.add_argument("--self-test", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args.self_test:
        import tkinter
        assert tkinter.Tcl().eval("info patchlevel")
        assert validate_endpoint("127.0.0.1:27017") == "127.0.0.1:27017"
        assert f'name "{DEFAULT_NAME}"' in join_config()
        return 0
    open_window(args.join)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
