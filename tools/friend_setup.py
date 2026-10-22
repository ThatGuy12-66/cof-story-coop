"""Private-copy installer for the story co-op app: setup and update.

The window is tools/coop_app.py. The package contains code and Enhanced
payloads only: every retail asset, both Simon models and the 12-slot inventory
art are copied or generated on the player's own computer from their own game.
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
from tools import prepare_coop_weapons as weapons
from tools.map_merge import groups as merge_groups
from tools.prepare_simon_models import COSTUMES, build_models, costume_files, validate_model

STOCK_DLL_SHA = "0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63"
EXCLUDED_DIRS = {"save", "saves", "logs", "screenshots", "downloads", "download", "cof-enhanced", "cof-enhanced-backup", "experiment-evidence", ".git", "__pycache__"}
ANYWHERE_DIRS = {".git", "__pycache__"}   # the others only at the game root or directly in a game folder (SAVE, cryoffear/SAVE)
EXCLUDED_FILES = {"config.cfg", "userconfig.cfg", "autoexec.cfg", "server.cfg", "listenserver.cfg", "gameinfo.txt", "mapcycle.txt", "motd.txt", "console.log", "qconsole.log", "steam_appid.txt"}
EXCLUDED_SUFFIXES = {".sav", ".hl1", ".hl2", ".hl3", ".log", ".dem", ".ent", ".orig", ".bak", ".tmp", ".dmp", ".pdb"}
APP_EXE = "CoF Story Co-op.exe"
# 1.2.1: the app is a one-folder build: its Python, Tk and engine payload sit in this folder next to the exe instead
# of being unpacked to a temporary folder at every start (antivirus programs flagged the self-unpacking exe)
APP_FILES = "coop-app"
PAYLOAD_ZIP = "engine-payload.zip"
PAYLOAD_JSON = "payload.json"
INSTALLED = "cof-story-installed.json"
MERGE_LOG = "cof-story-merge.log"
# Upstream's player-zip files a copy has no use for (the app installs the engine itself): Enhanced's
# Install.cmd/Uninstall.cmd (its uninstaller strips the copy's engine) and README (install into the Steam folder),
# the cheats guide and the tester notices. The package leaves them out; an update removes the test packages' ones.
RETIRED_FILES = ("README.txt", "Install.cmd", "Uninstall.cmd", "EXPERIMENTAL-STORY.txt", "STORY-START.txt",
                 "cof-enhanced/CHEATS.md")
ENTITY_LIMIT = 4096   # liblist.gam's commandargs "-num_edicts 4096": the limit Cry of Fear was made for
# The name in the first config.cfg, before anyone has typed one in the app.
DEFAULT_NAME = "Player"
SETUP_LOG = "cof-story-setup.log"   # every line the setup and update windows showed
INCOMPLETE = "SETUP-INCOMPLETE.txt"   # in a copy whose setup has not finished (removed at the end of setup)
KEPT_LOGS = 20   # session logs kept in story-logs
# Each step's share of the bar: about its share of the time (the 1.0 install: copy 93 s, engine 13 s, models 13 s,
# art under a second, story maps 156 s).
SETUP_STEPS = (("Checking your game", .01), ("Copying game files", .33), ("Installing the co-op engine", .05),
               ("Making the Simon and weapon models", .05), ("Making the inventory art", .01),
               ("Joining story maps (fewer loading screens)", .55))
UPDATE_STEPS = (("Installing the new engine", .07), ("Making the Simon and weapon models", .07),
                ("Making the inventory art", .01), ("Joining story maps (fewer loading screens)", .85))
# Upstream's installer was written for an install into the Steam folder; these of its lines do not fit a copy.
INSTALLER_HIDDEN = ("Start Cry of Fear from Steam", "Uninstall.cmd")


class Steps:
    """What setup is doing, for the window as it happens: report(status, fraction, line) with the step ("Step 2 of 6:
    Copying game files"), the bar for the whole job (each step has its share) and, for each thing done, a line. The
    lines also go into the copy's setup log once there is a copy, the ones from before it existed too."""

    def __init__(self, steps, report=lambda *_: None, clock=datetime.now):
        self.names = [name for name, _ in steps]
        self.shares = [share for _, share in steps]
        self.report, self.clock = report, clock
        self.at, self.share, self.detail, self.finished = -1, 0.0, "", None
        self.kept, self.log = [], None

    def status(self) -> str:
        if self.finished is not None:
            return self.finished
        if self.at < 0:
            return ""
        text = f"Step {self.at + 1} of {len(self.names)}: {self.names[self.at]}"
        return f"{text}, {self.detail}" if self.detail else text

    def fraction(self) -> float:
        if self.finished is not None:
            return 1.0
        if self.at < 0:
            return 0.0
        return min(1.0, sum(self.shares[:self.at]) + self.shares[self.at] * min(max(self.share, 0.0), 1.0))

    def start(self, name: str) -> None:
        self.at, self.share, self.detail = self.names.index(name), 0.0, ""
        self.note(self.status())

    def part(self, share: float, detail: str = "") -> None:
        """How far into the current step (0 to 1), and what it is on."""
        self.share, self.detail = share, detail
        self.report(self.status(), self.fraction(), None)

    def note(self, line: str) -> None:
        line = f"{self.clock():%H:%M:%S}  {line}"
        if self.log is None:
            self.kept.append(line)
        else:
            try:
                with self.log.open("a", encoding="utf-8") as log:
                    log.write(line + "\n")
            except OSError:
                pass   # a lost log line never stops setup
        self.report(self.status(), self.fraction(), line)

    def log_to(self, path: Path) -> None:
        self.log = Path(path)
        with self.log.open("a", encoding="utf-8") as log:
            log.writelines(line + "\n" for line in self.kept)
        self.kept = []

    def done(self, text: str) -> None:
        self.finished = text
        self.note(text)


def size_text(size: int) -> str:
    return f"{size // (1024 * 1024)} MB" if size >= 1024 * 1024 else f"{(size + 1023) // 1024} KB"


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
    # 1.1.9 (L14.3): the game folder may be reached through a link (a Steam library moved with a junction): its real
    # folder is checked and copied. Links inside the game are still refused (here and in copy_plan), and so are
    # links on the way to a destination (validate_destination).
    root = Path(path).expanduser().absolute().resolve()
    reject_links(root)
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
    # 1.1.9 (L14.8): Program Files takes writes only from an administrator (setup and the game's saves fail), and
    # OneDrive uploads the 5 GB copy and can leave its files online-only
    for names, message in ((("ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"),
                            "The destination must be outside Program Files: the game could not save there."),
                           (("OneDrive", "OneDriveConsumer", "OneDriveCommercial"),
                            "The destination must be outside OneDrive: it would upload the 5 GB copy and can keep "
                            "its files online only.")):
        for name in names:
            folder = os.environ.get(name, "")
            if folder and Path(folder).is_absolute() and target.is_relative_to(Path(folder).resolve()):
                raise ValueError(message)
    return target


def excluded_dir(name: str, depth: int) -> bool:
    """A folder of personal state or tooling at this depth (0: the game root). Saves and logs are only left out at the
    root or directly in a game folder: the game's own sounds sit in cryoffear/sound/save (the save sounds), which a
    rule for every folder named save left out of every copy before 1.1.6."""
    name = name.casefold()
    return name in ANYWHERE_DIRS or (name in EXCLUDED_DIRS and depth <= 1)


def should_copy(relative: Path) -> bool:
    parts = [part.casefold() for part in relative.parts]
    if any(excluded_dir(part, depth) for depth, part in enumerate(parts[:-1])):
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
        depth = len(current.relative_to(source).parts)
        dirs[:] = [name for name in dirs if not excluded_dir(name, depth)]
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


def copy_assets(source: Path, destination: Path, plan, steps=None) -> None:
    """Copy and verify each planned file: the bar moves with each file, and each folder done gets a line."""
    steps = steps or Steps(())
    done, total = 0, sum(size for _, size in plan)
    folder, count, size = None, 0, 0

    def folder_done():
        if folder is not None:
            name = folder.as_posix() if folder.parts else "the game folder"
            steps.note(f"Copied {name}: {count} file{'' if count == 1 else 's'}, {size_text(size)}")
    for relative, expected_size in plan:
        if relative.parent != folder:
            folder_done()
            folder, count, size = relative.parent, 0, 0
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
        count, size = count + 1, size + expected_size
        steps.part(done / max(total, 1), f"{done // (1024 * 1024)} of {total // (1024 * 1024)} MB, {relative.as_posix()}")
    folder_done()


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


def run_installer(destination: Path, steps=None) -> None:
    """The Enhanced installer from the payload; it upgrades an earlier install in place. Its output goes into
    friend-setup-install.log as it comes, and to the window line by line."""
    steps = steps or Steps(())
    shell = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    env = os.environ.copy()
    env["PSModulePath"] = str(shell.parent / "Modules")
    with (destination / "friend-setup-install.log").open("wb") as log, \
         subprocess.Popen([str(shell), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
                           str(destination / "cof-enhanced/install.ps1"), "-GameDir", str(destination)],
                          cwd=destination, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)) as process:
        for raw in process.stdout:
            log.write(raw)
            line = raw.decode("oem" if os.name == "nt" else "utf-8", "replace").strip()   # the console's code page
            if line and not any(words in line for words in INSTALLER_HIDDEN):
                steps.note(line)
    if process.returncode:
        raise RuntimeError("The engine could not be installed. See friend-setup-install.log in the copy.")


def make_art(root: Path, steps=None) -> None:
    """The 12-slot inventory's background art, from this copy's own retail images."""
    steps = steps or Steps(())
    try:
        written = art.generate(root)
    except SystemExit as error:
        raise RuntimeError(f"The inventory art could not be made: {error}") from None
    for path in written:
        try:
            steps.note(f"Made {Path(path).relative_to(root).as_posix()}")
        except ValueError:
            steps.note(f"Made {path}")


def generated_models(models: Path) -> dict[str, bytes]:
    """Everything the generators make from a copy's own stock models, keyed by path under cryoffear:
    the Simon player models and the third-person weapon models (models/coop)."""
    files = build_models((models / "cutscene/player.mdl").read_bytes(), (models / "player/police1/police1.mdl").read_bytes(),
                         {rel: (models / "costumes" / rel).read_bytes() for rel in costume_files()})
    # 1.1.9: and the optional sources a copy has (a cosmetic model without its stock file is left out, not refused)
    files.update(weapons.build_models({**{rel: (models.parent / rel).read_bytes() for rel in weapons.SOURCES},
                                       **weapons.read_optional(models.parent)}))
    return files


def make_models(root: Path, steps=None) -> dict[str, str]:
    """(Re)make the generated models in a copy from its own files; an update replaces older generator output."""
    steps = steps or Steps(())
    hashes = {}
    steps.part(0, "reading your game's models")
    files = generated_models(root / "cryoffear/models")
    for skipped in json.loads(files.get("coop-weapons.json", b"{}")).get("skipped", []):
        steps.note(f"Left out {skipped['path']} (cosmetic): {skipped['reason']}")
    models = [rel for rel in files if rel.endswith(".mdl")]
    for number, rel in enumerate(models, 1):
        data = files[rel]
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
        steps.part(number / len(models), rel)
        steps.note(f"Made {rel}")
    return hashes


def default_controls() -> str:
    binds = {"w": "+forward", "s": "+back", "a": "+moveleft", "d": "+moveright", "SPACE": "+jump",
             "CTRL": "+duck", "SHIFT": "+sprint", "ALT": "+dodge", "e": "+use", "r": "+reload",
             "TAB": "+inventory", "b": "+objectives", "f": "flashlight", "z": "weapontoggle",
             "MOUSE1": "+attack", "MOUSE2": "+attack3", "MOUSE3": "+attack2", "ESCAPE": "cancelselect",
             "1": "quicksel 1", "2": "quicksel 2", "3": "quicksel 3", "y": "messagemode", "k": "+voicerecord",
             "j": "cof_warp", "h": "cof_companion_order", "ENTER": "cof_skip", "KP_ENTER": "cof_skip",
             "o": "cof_wardrobe"}
    return "\n".join(f'bind "{key}" "{value}"' for key, value in binds.items()) + "\n"


# 1.1.8 (L6): the co-op keys of default_controls, also in the game's default key list (Options > Keybinds > Use
# defaults binds kb_def.lst, which had none of them, so J, the way out of every strand, was lost) and in its list of
# actions (kb_act.lst: the Keybinds menu shows and rebinds only those).
# After 1.1.9: O opens the wardrobe (cof_wardrobe) anywhere while the bonus room is open; K stays the voice key.
COOP_KEYS = (("j", "cof_warp"), ("h", "cof_companion_order"), ("ENTER", "cof_skip"), ("KP_ENTER", "cof_skip"),
             ("o", "cof_wardrobe"))
COOP_ACTIONS = (("cof_warp", "Join your partner (co-op)"), ("cof_companion_order", "AI companions: wait or follow"),
                ("cof_skip", "Skip a cutscene for everyone"), ("cof_wardrobe", "Wardrobe (costumes)"))
_KEY_LIST_TOKEN = r'(?:"[^"\r\n]*"|[^\s"]+)'
_KEY_LIST_LINE = re.compile(rf'\s*(?://.*)?|\s*({_KEY_LIST_TOKEN})\s+({_KEY_LIST_TOKEN})\s*(?://.*)?')


def _add_key_list_lines(path: Path, pairs, taken) -> int:
    """Appends each "first" "second" pair of pairs whose first token (folded) is not in taken(rows) to a key list
    of the known format (every line two tokens, blank or a comment); another format is left alone. The lines in
    the file stay as they are. Returns the lines added, or -1 for a file of another format."""
    data = path.read_bytes()
    text = data.decode("latin-1")
    rows = []
    for line in text.splitlines():
        match = _KEY_LIST_LINE.fullmatch(line)
        if match is None:
            return -1
        if match[1]:
            rows.append((match[1].strip('"').casefold(), match[2].strip('"').casefold()))
    used = taken(rows)
    added = [(first, second) for first, second in pairs if first.casefold() not in used]
    if not added:
        return 0
    newline = "\r\n" if "\r\n" in text else "\n"
    tail = "" if not text or text.endswith(("\n", "\r")) else newline
    reject_links(path)
    with path.open("ab") as stream:   # appended: the file's own lines are never rewritten
        stream.write((tail + "".join(f'"{first}"\t\t"{second}"{newline}' for first, second in added)).encode("latin-1"))
    return len(added)


_BOUND_KEY = re.compile(r'^\s*bind\s+"?([^"\s]+)"?', re.I | re.M)


def missing_coop_binds(root: Path) -> str:
    """Bind lines for the co-op keys the player's config.cfg leaves unbound, for the session cfg (1.2.0): a copy set
    up before a key was added (O, the wardrobe) keeps its config.cfg through updates, so the key would do nothing
    until Options > Keybinds > Use defaults. A key the player bound to anything else is left alone."""
    try:
        text = (Path(root) / "cryoffear/config.cfg").read_bytes().decode("latin-1")
    except OSError:
        return ""
    bound = {key.lower() for key in _BOUND_KEY.findall(text)}
    return "".join(f'bind "{key}" "{command}"\n' for key, command in COOP_KEYS if key.lower() not in bound)


def add_coop_keys(root: Path, steps=None) -> None:
    """J, H, Enter and O in the copy's default key list and the co-op commands in its list of actions (setup and
    update; the files come from the game or the engine payload, and an update may have put the plain ones back).
    A key the list already gives another command keeps it; the player's own binds (config.cfg) are not touched."""
    steps = steps or Steps(())
    shell = Path(root) / "cryoffear/gfx/shell"
    for name, pairs, taken in (
            ("kb_def.lst", COOP_KEYS, lambda rows: {key for key, _ in rows}),
            ("kb_act.lst", COOP_ACTIONS, lambda rows: {command for command, _ in rows})):
        path = shell / name
        try:
            if not path.is_file():
                continue
            added = _add_key_list_lines(path, pairs, taken)
        except (OSError, ValueError) as error:
            steps.note(f"The co-op keys could not be added to gfx/shell/{name}: {error}")   # never stops setup
            continue
        if added < 0:
            steps.note(f"gfx/shell/{name} is not in the usual format; the co-op keys were not added to it")
        elif added:
            steps.note(f"Added the co-op keys to gfx/shell/{name} (Options > Keybinds)")


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
    # 1.1.8 (L13): the newest KEPT_LOGS only (with the story trace on, one session can write tens of MB)
    try:
        old = sorted((path for path in archive.glob(f"{stem}-*.log") if path.is_file()),
                     key=lambda path: (path.stat().st_mtime, path.name))
        for path in old[:-KEPT_LOGS]:
            path.unlink()
    except OSError:
        pass   # a log that cannot go now goes next time; it never stops a game from starting
    return kept


def ensure_entity_limit(root: Path) -> None:
    """Cry of Fear's own entity limit in gameinfo.txt (written by the Enhanced installer): liblist.gam asks
    GoldSrc for -num_edicts 4096, but this engine reads only gameinfo.txt's max_edicts (default 1200), and a
    merged story map holds more entities than that."""
    path = root / "cryoffear/gameinfo.txt"
    lines = [line for line in path.read_text(encoding="ascii").splitlines() if not line.strip().startswith("max_edicts")]
    lines.append(f"max_edicts {ENTITY_LIMIT}")
    path.write_text("\r\n".join(lines) + "\r\n", encoding="ascii", newline="")


def merged_maps_room(source: Path) -> int:
    """The room the merged story maps take: no merged map is larger than its members."""
    maps = Path(source) / "cryoffear/maps"
    return sum((maps / f"{m}.bsp").stat().st_size for g in merge_groups.load_groups() if g.enabled
               for m in g.members if (maps / f"{m}.bsp").is_file())


def make_merged_maps(root: Path, steps=None) -> dict[str, str]:
    """(Re)make the merged story maps from this copy's own retail maps (tools/map_groups.json). A group whose maps
    are not the retail files it was made from plays unmerged; merged files of older builds are removed."""
    steps = steps or Steps(())
    maps = root / "cryoffear/maps"
    reject_links(maps)
    made, lines = {}, []
    enabled = [group for group in merge_groups.load_groups() if group.enabled]
    for number, group in enumerate(enabled, 1):
        steps.part((number - 1) / len(enabled), f"{number} of {len(enabled)}: {group.name}")
        try:
            made.update(merge_groups.install_group(maps, group))
            lines.append(f"{group.name}: merged {' + '.join(group.members)}")
        except (merge_groups.GroupInputMismatch, merge_groups.MergeCheckFailed, RuntimeError, ValueError, OSError) as error:
            lines.append(f"{group.name}: not merged, its maps play as usual: {error}")
        steps.note(f"{number} of {len(enabled)}: {lines[-1]}")
    for path in maps.glob("cm_*"):
        if path.suffix in (".bsp", ".sections", ".partial") and f"maps/{path.name}" not in made:
            path.unlink()
    (root / MERGE_LOG).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return made


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


def finish(root: Path, manifest: dict, models: dict, source_game: str, exe: Path, shortcuts: bool, steps,
           merged: dict | None = None) -> None:
    """The parts setup and update share: the story cfg, the app in the copy, the installed record, shortcuts."""
    (root / "cryoffear" / play.STORY_CFG).write_text(play.story_config(), encoding="ascii")
    install_app(Path(exe), root)
    add_coop_keys(root, steps)
    state = {"schema": 2, "version": manifest["version"], "build": manifest["build"],
             "xash_sha256": manifest["xash_sha256"], "models": models, "merged_maps": merged or {},
             "source_game": source_game, "campaign_verified": False}
    if (root / "vgui.dll").is_file():   # 1.1.8 (L1): validate_runtime checks it with xash.dll
        state["vgui_sha256"] = digest_file(root / "vgui.dll")
    (root / INSTALLED).write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    if shortcuts and not write_shortcuts(root):
        steps.done(f"Done, but the desktop shortcut could not be made: start {APP_EXE} in {root}.")
        return
    steps.done("Done. Use the Cry of Fear Co-op shortcut on your desktop or in the Start menu.")


def install_app(exe: Path, root: Path) -> None:
    """The app into the copy: its folder of files (a one-folder build, 1.2.1) first, then the exe, last (1.1.8 L14.12:
    while the copy's own app runs, Windows refuses to replace it). Nothing to do when it is the copy's own app."""
    target = root / APP_EXE
    if exe.resolve() == target.resolve():
        return
    files = exe.parent / APP_FILES
    if files.is_dir():
        reject_links(files)
        new, old, current = root / f"{APP_FILES}.new", root / f"{APP_FILES}.old", root / APP_FILES
        for leftover in (new, old):
            reject_links(leftover)
            if leftover.exists():
                shutil.rmtree(leftover)
        shutil.copytree(files, new)
        if current.exists():
            reject_links(current)
            current.rename(old)
        new.rename(current)
        shutil.rmtree(old, ignore_errors=True)
    shutil.copyfile(exe, target)


def model_sources() -> dict[str, str]:
    """Every stock file the model generators read (under cryoffear) and its pinned SHA-256: the Simon, police and
    weapon models and the costume textures."""
    pinned = dict(weapons.SOURCES)
    for folder, textures in COSTUMES.values():
        for filename, expected in textures.values():
            pinned[f"models/costumes/{folder}/{filename}"] = expected
    return pinned


def check_model_sources(source: Path, provided=frozenset()) -> None:
    """1.1.8 (L5): the generators refuse anything but the stock bytes, and they run after the 5 GB copy; a modded
    or damaged game is refused here, before it. provided: the payload's own paths (the copy gets those instead)."""
    for rel, expected in model_sources().items():
        if f"cryoffear/{rel}".casefold() in provided:
            continue
        item = Path(source) / "cryoffear" / rel
        if not item.is_file() or digest_file(item) != expected:
            raise ValueError(f"Your Cry of Fear files are modded or damaged (cryoffear/{rel} is not the original). In "
                             "Steam, right-click Cry of Fear > Properties > Installed Files > Verify integrity of game "
                             "files, then set up again.")


def incomplete_copy(path: Path) -> bool:
    """A folder an earlier setup of this app made and did not finish: our SETUP-INCOMPLETE.txt and our setup log in
    it, no install record, and no link anywhere on the way to it."""
    try:
        root = Path(path).expanduser().absolute()
        reject_links(root)
        marker = root / INCOMPLETE
        return (root.is_dir() and marker.is_file() and not marker.is_symlink() and (root / SETUP_LOG).is_file()
                and not (root / SETUP_LOG).is_symlink() and not (root / INSTALLED).exists()
                and marker.read_text(encoding="latin-1").startswith("Setup is incomplete."))
    except (OSError, ValueError):
        return False


def remove_incomplete(path: Path, steps=None) -> list[str]:
    """1.1.8 (L4): deletes the unfinished copy of an earlier setup (incomplete_copy only) so setup can use its folder
    again. Links inside are removed, never followed; its two markers go last, so a delete that stops part way can
    be retried. Returns the old setup log's lines."""
    steps = steps or Steps(())
    root = Path(path).expanduser().absolute()
    if not incomplete_copy(root):
        raise ValueError("Choose a new destination folder. Existing folders are never replaced.")
    earlier = (root / SETUP_LOG).read_text(encoding="utf-8", errors="replace").splitlines()
    steps.note(f"Deleting the unfinished copy an earlier setup left in {root}")
    markers = {INCOMPLETE.casefold(), SETUP_LOG.casefold()}
    for child in list(root.iterdir()):
        if child.name.casefold() in markers:
            continue
        if child.is_dir() and not child.is_symlink() and not (hasattr(child, "is_junction") and child.is_junction()):
            shutil.rmtree(child)
        else:
            child.unlink()   # a file, or a link itself (never what it points to)
    (root / SETUP_LOG).unlink()
    (root / INCOMPLETE).unlink()
    root.rmdir()
    return earlier


def setup(source: Path, destination: Path, payload: Path, *, exe: Path, progress=lambda *_: None, shortcuts=True,
          replace_incomplete: bool = False) -> Path:
    """A new copy. progress(status, fraction, line) as it goes (Steps); a setup that stops says why in its log.
    replace_incomplete: delete an unfinished copy of an earlier setup in destination first (incomplete_copy)."""
    steps = Steps(SETUP_STEPS, progress)
    try:
        return make_copy(Path(source), Path(destination), Path(payload), exe, shortcuts, steps, replace_incomplete)
    except Exception as error:
        steps.note(f"Stopped: {error}")
        raise


def make_copy(source: Path, destination: Path, payload: Path, exe: Path, shortcuts: bool, steps: Steps,
              replace_incomplete: bool = False) -> Path:
    steps.start("Checking your game")
    source = validate_source(source)
    if replace_incomplete and incomplete_copy(destination):
        earlier = remove_incomplete(destination, steps)
        steps.kept[:0] = earlier   # the new log starts with the stopped setup's lines, for a bug report
    destination = validate_destination(source, destination)
    manifest = inspect_payload(payload)
    steps.note(f"Cry of Fear Co-op {manifest['version']} (build {manifest['build']}): a copy of {source} in {destination}")
    plan = copy_plan(source)
    with zipfile.ZipFile(payload / PAYLOAD_ZIP) as archive:
        package_paths = {info.filename.casefold() for info in archive.infolist() if not info.is_dir()}
    check_model_sources(source, package_paths)
    plan = [(relative, size) for relative, size in plan if relative.as_posix().casefold() not in package_paths]
    steps.note(f"{len(plan)} game files to copy, {size_text(sum(size for _, size in plan))}")
    ancestor = destination.parent
    while not ancestor.exists():
        ancestor = ancestor.parent
    needed = sum(size for _, size in plan) + 600 * 1024 * 1024 + merged_maps_room(source)
    if shutil.disk_usage(ancestor).free < needed:
        raise ValueError(f"The destination needs at least {needed / (1024 ** 3):.1f} GB free.")
    destination.mkdir(parents=True, exist_ok=False)
    steps.log_to(destination / SETUP_LOG)
    (destination / INCOMPLETE).write_text("Setup is incomplete. Click Set up in the app again to delete this folder and "
                                          "retry; no original game files were changed.\n")
    steps.start("Copying game files")
    copy_assets(source, destination, plan, steps)
    steps.start("Installing the co-op engine")
    extract_payload(payload / PAYLOAD_ZIP, destination)
    run_installer(destination, steps)
    if digest_file(destination / "xash.dll") != manifest["xash_sha256"]:
        raise RuntimeError("Installed engine hash differs from the package.")
    ensure_entity_limit(destination)
    if digest_file(destination / "cryoffear/cl_dlls/hl.dll") != STOCK_DLL_SHA:
        raise RuntimeError("The copied retail game DLL changed unexpectedly.")
    steps.start("Making the Simon and weapon models")
    model_hashes = make_models(destination, steps=steps)
    steps.start("Making the inventory art")
    make_art(destination, steps)
    steps.start("Joining story maps (fewer loading screens)")
    merged = make_merged_maps(destination, steps=steps)
    (destination / "cryoffear/config.cfg").write_text(default_controls() + f'name "{DEFAULT_NAME}"\nsensitivity 4\n', encoding="ascii")
    (destination / "steam_appid.txt").write_text("223710\n", encoding="ascii")
    (destination / INCOMPLETE).unlink()
    finish(destination, manifest, model_hashes, str(source), exe, shortcuts, steps, merged)
    return destination


def restore_left_out(root: Path, source_game: str, steps) -> int:
    """Game files an older setup left out (cryoffear/sound/save, under the old rule for folders named save), copied
    from the game the copy was made from when they are missing. Nothing in the copy is replaced."""
    source = Path(source_game) if source_game else None
    if not source or not (source / "cryoffear").is_dir():
        return 0
    restored = 0
    try:
        for relative, _ in copy_plan(source):
            parts = [part.casefold() for part in relative.parts[:-1]]
            if not any(part in EXCLUDED_DIRS for part in parts) or (root / relative).exists():
                continue
            target = root / relative
            reject_links(target.parent)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / relative, target)
            restored += 1
    except (OSError, ValueError) as error:
        steps.note(f"Some game files could not be added from {source}: {error}")
    if restored:
        steps.note(f"Added {restored} game file{'' if restored == 1 else 's'} an older setup left out (the save sounds)")
    return restored


def update(root: Path, payload: Path, *, exe: Path, progress=lambda *_: None, shortcuts=True) -> Path:
    """Lay a newer package over a copy made by this app: saves, settings and models stay. progress as for setup;
    the lines go on in the copy's setup log."""
    steps = Steps(UPDATE_STEPS, progress)
    try:
        return update_copy(Path(root).resolve(), Path(payload), exe, shortcuts, steps)
    except Exception as error:
        steps.note(f"Stopped: {error}")
        raise


def update_copy(root: Path, payload: Path, exe: Path, shortcuts: bool, steps: Steps) -> Path:
    state = installed_state(root)
    if not state:
        raise ValueError("This folder is not a story co-op copy made by this app.")
    if game_is_running(root):
        raise ValueError("Close Cry of Fear first, then update.")
    app = root / APP_EXE
    if app.is_file() and Path(exe).resolve() != app.resolve():
        # 1.1.8 (L14.12): finish replaces the copy's app last; while it runs, Windows refuses that, and the whole
        # update would have to be done again
        try:
            with app.open("ab"):
                pass
        except OSError:
            raise ValueError(f"Close the other Cry of Fear Co-op window (the app in {root}), then update.") from None
    manifest = inspect_payload(payload)
    steps.log_to(root / SETUP_LOG)
    steps.start("Installing the new engine")
    steps.note(f"Cry of Fear Co-op {manifest['version']} (build {manifest['build']}) over {state['version']} "
               f"(build {state['build']}) in {root}")
    for rel in RETIRED_FILES:
        old = root / rel
        reject_links(old)
        if old.is_file():
            old.unlink()
    extract_payload(payload / PAYLOAD_ZIP, root, overwrite=True)
    run_installer(root, steps)
    if digest_file(root / "xash.dll") != manifest["xash_sha256"]:
        raise RuntimeError("Installed engine hash differs from the package.")
    ensure_entity_limit(root)
    restore_left_out(root, state.get("source_game", ""), steps)
    steps.start("Making the Simon and weapon models")
    models = make_models(root, steps=steps)
    steps.start("Making the inventory art")
    make_art(root, steps)
    steps.start("Joining story maps (fewer loading screens)")
    merged = make_merged_maps(root, steps=steps)
    finish(root, manifest, models, state["source_game"], exe, shortcuts, steps, merged)
    return root


class CopyMismatch(ValueError):
    """validate_runtime: a file of the copy is not what its install record says. 1.1.8 (L1): the app then offers
    Repair (update with its own payload, also at the same build); problem is the first sentence of the message."""

    def __init__(self, problem: str):
        self.problem = problem
        super().__init__(f"{problem} Run the newest {APP_EXE} and click Update.")


def entity_limit_set(root: Path) -> bool:
    """gameinfo.txt has ensure_entity_limit's line: an update stopped after its installer step leaves it out."""
    try:
        lines = (Path(root) / "cryoffear/gameinfo.txt").read_text(encoding="latin-1").splitlines()
    except OSError:
        return False
    return any(line.split()[:1] == ["max_edicts"] and line.split()[1:] == [str(ENTITY_LIMIT)] for line in lines)


def validate_runtime(runtime: Path) -> Path:
    """A copy made by this app, with the engine and models it was set up with."""
    raw = Path(runtime).absolute()
    reject_links(raw)
    root = raw.resolve()
    if any(part.casefold() == "steamapps" for part in root.parts):
        raise ValueError("Use the separate game copy created by setup.")
    state = installed_state(root)
    message = "This game copy does not match its install."
    if not state or not (root / "xash.dll").is_file() or digest_file(root / "xash.dll") != state.get("xash_sha256"):
        raise CopyMismatch(message)
    # 1.1.8 (L1): vgui.dll is always installed with xash.dll (they share an interface every build changes); records
    # from before 1.1.8 have no hash for it
    if state.get("vgui_sha256") and (not (root / "vgui.dll").is_file()
                                     or digest_file(root / "vgui.dll") != state["vgui_sha256"]):
        raise CopyMismatch("The engine's vgui.dll is missing or changed. " + message)
    if not entity_limit_set(root):
        raise CopyMismatch("The game's entity limit (max_edicts in gameinfo.txt) is missing. " + message)
    for rel, expected in state.get("models", {}).items():
        path = PurePosixPath(rel)
        if path.is_absolute() or ".." in path.parts or "\\" in rel or ":" in rel:
            raise ValueError("Invalid installed model path")
        model = root / "cryoffear" / rel
        reject_links(model)
        if not model.is_file() or digest_file(model) != expected:
            raise CopyMismatch("A generated Simon model is missing or changed. " + message)
    for rel, expected in state.get("merged_maps", {}).items():
        path = PurePosixPath(rel)
        if path.is_absolute() or ".." in path.parts or "\\" in rel or ":" in rel or path.parts[:1] != ("maps",):
            raise ValueError("Invalid installed merged map path")
        merged = root / "cryoffear" / rel
        reject_links(merged)
        if not merged.is_file() or digest_file(merged) != expected:
            raise CopyMismatch("A merged story map is missing or changed. " + message)
    return root


GAME_PROCESSES = ("cof.exe", "coflaunchapp.exe")


def game_process_named() -> bool:
    """Any Cry of Fear process, from tasklist: it names processes but not their folders."""
    tasklist = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/tasklist.exe"
    result = subprocess.run([str(tasklist), "/FO", "CSV", "/NH"], capture_output=True, check=True, timeout=10,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    names = {line.split(",", 1)[0].strip().strip('"').casefold()
             for line in result.stdout.decode("latin-1").splitlines() if line.strip()}
    return any(name in names for name in GAME_PROCESSES)


def game_is_running(runtime: Path, unknown: bool = False) -> bool:
    """A Cry of Fear process started from this copy (another copy's game does not count).

    1.1.8 (L3): when PowerShell fails or takes over 20 s (a broken WMI, a blocked PowerShell, a slow cold start),
    tasklist answers instead, and any Cry of Fear process then counts; when that fails too, the answer is unknown
    (the hosting window keeps a game it cannot see as running; Start, Join and Update go ahead)."""
    if os.name != "nt":
        return False
    shell = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    command = ('Get-CimInstance Win32_Process -Filter "Name=\'cof.exe\' OR Name=\'CoFLaunchApp.exe\'" | '
               'Select-Object ProcessId,Name,ExecutablePath | ConvertTo-Json -Compress')
    try:
        result = subprocess.run([str(shell), "-NoProfile", "-NonInteractive", "-Command", command],
                                capture_output=True, text=True, check=True, timeout=20,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        rows = json.loads(result.stdout) if result.stdout.strip() else []
    except (OSError, ValueError, subprocess.SubprocessError):
        try:
            return game_process_named()
        except (OSError, ValueError, subprocess.SubprocessError):
            return unknown
    if isinstance(rows, dict):
        rows = [rows]
    prefix = (str(Path(runtime)) + os.sep).casefold()
    return any(str(row.get("ExecutablePath") or "").casefold().startswith(prefix) for row in rows)
