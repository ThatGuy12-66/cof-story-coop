"""Install validation, safe launch configuration, and map checkpoints.

The launcher never edits config.cfg or a Steam-owned installation. A checkpoint
records the loaded map only; it does not preserve inventory, puzzles, or saves.
"""
from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
import ipaddress
import json
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import subprocess
import tempfile
import threading
from typing import Any


PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_GAME_DIR = PROJECT_ROOT / "baseline" / "Cry of Fear"
CONFIG_NAME = "cofcoop_launcher.cfg"
CHAPTERS = [
    ("Introduction", "c_intro"),
    ("Camera prologue (skip accident cutscene)", "c_nightmare"),
    ("Chapter 1 — Lost in a city", "c_start"),
    ("Chapter 2 — Who is that doctor?", "c_basement"),
    ("Chapter 3 — The city is not safe", "c_sewer3"),
    ("Chapter 4 — Drowned in sorrow", "c_park3"),
    # 1.1.8: c_subway2st1's first spawn is the chapter 4 arrival; chapter 5 begins at its landing from c_subwaysick2
    ("Chapter 5 — Leaving this for good", "c_subwaysick2"),
    ("Chapter 6 — It's not over yet", "c_bridge"),
    ("Chapter 7 — Only safe at home", "c_attic"),
    ("Chapter 8 — My life ends here (ending 1)", "c_ending1b"),
    ("Chapter 8 — My life ends here (ending 2)", "c_ending2b"),
    ("Chapter 8 — My life ends here (ending 3)", "c_ending3b"),
    ("Ending 1", "c_ending1"),
    ("Ending 2", "c_ending2"),
    ("Ending 3", "c_ending3"),
    ("Ending 4", "c_ending4"),
    ("Ending 5", "c_ending5"),
]
_MAP_NAME = re.compile(r"[a-zA-Z0-9_][a-zA-Z0-9_-]{0,63}\Z")
_LIVE_PROCESSES: dict[str, subprocess.Popen] = {}
_LAUNCH_LOCK = threading.RLock()


def load_catalog(report_path: Path = PROJECT_ROOT / "tools" / "map_report.json") -> dict:
    """Load the complete scanned map inventory, refusing an incomplete report."""
    with Path(report_path).open("r", encoding="utf-8") as stream:
        report = json.load(stream)
    if not isinstance(report, dict) or not isinstance(report.get("maps"), dict):
        raise ValueError("The map report must contain a maps object.")
    if report.get("errors"):
        raise ValueError(f"The map report has scan errors: {report['errors']}")
    catalog = report["maps"]
    for name, record in catalog.items():
        if not isinstance(name, str) or not _MAP_NAME.fullmatch(name):
            raise ValueError(f"Invalid map name in catalog: {name!r}")
        if not isinstance(record, dict) or not isinstance(record.get("links", []), list):
            raise ValueError(f"Invalid catalog record for {name}.")
    return catalog


def next_maps(catalog: dict, map_name: str) -> list[str]:
    """Return only real trigger_changelevel map destinations, in entity order."""
    found: list[str] = []
    for link in catalog.get(map_name.lower(), {}).get("links", []):
        if not isinstance(link, dict):
            continue
        name = str(link.get("map", "")).lower()
        if (link.get("class") == "trigger_changelevel" and link.get("key") == "map"
                and name in catalog and name not in found):
            found.append(name)
    return found


def _forbidden_location(path: Path | str) -> bool:
    """Check lexical paths before filesystem access, including Windows on POSIX."""
    windows = PureWindowsPath(str(path))
    parts = tuple(part.casefold() for part in windows.parts)
    if windows.drive.casefold().endswith("d:"):
        return True
    # Steam library ownership is structural, even outside the default library.
    return any(parts[index:index + 2] == ("steamapps", "common")
               for index in range(len(parts) - 1))


def _local_path(path: Path | str) -> Path:
    raw = Path(path).expanduser()
    if _forbidden_location(raw):
        raise ValueError("Use a local development copy; Steam installations and drive D: are protected.")
    resolved = raw.resolve()
    if _forbidden_location(resolved):
        raise ValueError("This path resolves to a protected Steam installation or drive D:.")
    return resolved


def validate_game_dir(path: Path | str) -> Path:
    """Return a local copied game root; reject protected paths before reading it."""
    root = _local_path(path)
    if not root.is_dir():
        raise ValueError(f"Game folder does not exist: {root}")
    required = [root / "cof.exe", root / "cryoffear" / "maps"]
    for item in required:
        target = _local_path(item)
        if not target.is_relative_to(root):
            raise ValueError(f"Game files must remain inside the copied game folder: {item}")
    if not (root / "cof.exe").is_file() or not (root / "cryoffear" / "maps").is_dir():
        raise ValueError("Choose the Cry of Fear root containing cof.exe and cryoffear/maps.")
    return root


def validate_map_name(game_dir: Path | str, map_name: str) -> str:
    if not isinstance(map_name, str) or not _MAP_NAME.fullmatch(map_name):
        raise ValueError("Map names may contain only letters, digits, underscores, and hyphens.")
    root = validate_game_dir(game_dir)
    name = map_name.lower()
    bsp_path = _local_path(root / "cryoffear" / "maps" / f"{name}.bsp")
    if not bsp_path.is_relative_to(root) or not bsp_path.is_file():
        raise ValueError(f"Map {name!r} is not installed in this copied game folder.")
    return name


def discover_installs() -> list[Path]:
    """Prefer the dev copy, then discover Steam paths from C: metadata only.

    Steam results are source locations, not writable targets. D: candidates are
    inferred from libraryfolders.vdf without stat/list/read operations on D:.
    """
    found: list[Path] = []
    if DEFAULT_GAME_DIR.is_dir():
        found.append(DEFAULT_GAME_DIR)
    steam_roots: list[Path] = []
    if os.name == "nt":
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
                steam_roots.append(Path(winreg.QueryValueEx(key, "SteamPath")[0]))
        except OSError:
            pass
        steam_roots.append(Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Steam")
    for steam_root in dict.fromkeys(steam_roots):
        if PureWindowsPath(str(steam_root)).drive.casefold() == "d:":
            continue
        library_roots = [steam_root]
        metadata = steam_root / "steamapps" / "libraryfolders.vdf"
        try:
            text = metadata.read_text(encoding="utf-8", errors="replace")
            for value in re.findall(r'"path"\s+"((?:\\.|[^"\\])*)"', text):
                library_roots.append(Path(value.replace("\\\\", "\\")))
        except OSError:
            pass
        for library_root in dict.fromkeys(library_roots):
            candidate = library_root / "steamapps" / "common" / "Cry of Fear"
            if PureWindowsPath(str(library_root)).drive.casefold() == "d:":
                # A discovery hint only; never touch this drive.
                if candidate not in found:
                    found.append(candidate)
                continue
            manifest = library_root / "steamapps" / "appmanifest_223710.acf"
            try:
                manifest_text = manifest.read_text(encoding="utf-8", errors="replace")
                match = re.search(r'"installdir"\s+"([^"\r\n]+)"', manifest_text)
                if match and match[1] not in (".", "..") and not any(c in match[1] for c in "/\\:"):
                    candidate = library_root / "steamapps" / "common" / match[1]
                if candidate not in found:
                    found.append(candidate)
            except OSError:
                continue
    return found


def _atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


class SettingsStore:
    def __init__(self, path: Path = PROJECT_ROOT / "state" / "settings.json"):
        self.path = _local_path(path)

    def load(self) -> dict:
        try:
            with self.path.open("r", encoding="utf-8") as stream:
                data = json.load(stream)
        except FileNotFoundError:
            return {}
        except (UnicodeError, json.JSONDecodeError) as exc:
            raise ValueError(f"Cannot read {self.path}: {exc}") from exc
        if not isinstance(data, dict):
            raise ValueError(f"Expected a JSON object in {self.path}.")
        return data

    def save(self, data: dict) -> None:
        if not isinstance(data, dict):
            raise ValueError("Settings/progress must be a dictionary.")
        encoded = (json.dumps(data, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")
        _atomic_write(self.path, encoded)


class ProgressStore(SettingsStore):
    def __init__(self, path: Path = PROJECT_ROOT / "state" / "progress.json"):
        super().__init__(path)


@dataclass
class LaunchOptions:
    game_dir: Path
    name: str = "Player"
    port: int = 27015
    maxplayers: int = 4
    map_name: str = "c_intro"
    lan: bool = True
    dedicated: bool = False
    rcon_password: str = ""
    password: str = ""
    rate: int = 100000
    cl_cmdrate: int = 100
    cl_updaterate: int = 100
    sv_maxrate: int = 100000
    sv_minrate: int = 20000
    sv_maxupdaterate: int = 100
    sv_minupdaterate: int = 20


def _safe_text(value: str, label: str, *, allow_empty: bool = False, limit: int = 64) -> str:
    if not isinstance(value, str) or len(value) > limit:
        raise ValueError(f"{label} must be text of at most {limit} characters.")
    if not allow_empty and not value.strip():
        raise ValueError(f"{label} cannot be empty.")
    if any(ord(char) < 32 or ord(char) == 127 or char in '";\\' for char in value):
        raise ValueError(f"{label} cannot contain quotes, semicolons, backslashes, or control characters.")
    return value


def _integer(value: int, label: str, lower: int, upper: int) -> int:
    if type(value) is not int or not lower <= value <= upper:
        raise ValueError(f"{label} must be an integer from {lower} through {upper}.")
    return value


def _validate_options(options: LaunchOptions) -> Path:
    root = validate_game_dir(options.game_dir)
    validate_map_name(root, options.map_name)
    _safe_text(options.name, "Player name", limit=31)
    _safe_text(options.rcon_password, "RCON password", allow_empty=True)
    _safe_text(options.password, "Server password", allow_empty=True)
    _integer(options.port, "Port", 1, 65535)
    _integer(options.maxplayers, "Player count", 2, 4)
    if type(options.lan) is not bool or type(options.dedicated) is not bool:
        raise ValueError("LAN and dedicated options must be booleans.")
    for key in ("rate", "sv_maxrate", "sv_minrate"):
        _integer(getattr(options, key), key, 2000, 1000000)
    for key in ("cl_cmdrate", "cl_updaterate", "sv_maxupdaterate", "sv_minupdaterate"):
        _integer(getattr(options, key), key, 10, 200)
    if options.sv_minrate > options.sv_maxrate or options.sv_minupdaterate > options.sv_maxupdaterate:
        raise ValueError("Server minimum rates cannot exceed maximum rates.")
    executable = _local_path(root / ("hlds.exe" if options.dedicated else "cof.exe"))
    if not executable.is_relative_to(root) or not executable.is_file():
        raise ValueError(f"Required executable is missing from the copied game: {executable.name}")
    return root


def _rate_lines(options: LaunchOptions) -> list[str]:
    return [f"{key} {getattr(options, key)}" for key in (
        "rate", "cl_cmdrate", "cl_updaterate", "sv_maxrate", "sv_minrate",
        "sv_maxupdaterate", "sv_minupdaterate")]


def build_config(options: LaunchOptions) -> str:
    """Generate only validated configuration; this function does not write it."""
    _validate_options(options)
    lines = [
        "// Managed by the Cry of Fear co-op launcher.",
        "// Resume restores the map only, not save data or inventory.",
        f'name "{options.name}"',
        f'hostname "{options.name}\'s Cry of Fear story co-op"',
        f"sv_lan {int(options.lan)}",
        "coop 0", "deathmatch 1", "mp_timelimit 0", "mp_fraglimit 0",
        "mp_flashlight 1", "mp_weaponstay 1", "mp_allowmonsters 1", "sv_alltalk 1",
        f'rcon_password "{options.rcon_password}"', f'sv_password "{options.password}"',
        *_rate_lines(options), "log on",
    ]
    return "\n".join(lines) + "\n"


def build_launch_args(options: LaunchOptions) -> list[str]:
    """Return direct-engine arguments with configuration and rules before +map."""
    root = _validate_options(options)
    executable = root / ("hlds.exe" if options.dedicated else "cof.exe")
    args = [str(executable), "-basedir", "cryoffear", "-game", "cryoffear",
            "-heapsize", "1536000", "-num_edicts", "4096", "-condebug",
            "-port", str(options.port)]
    args.extend(["-console"] if options.dedicated else ["-hw", "-no3dmenu", "-windowed"])
    args.extend(["+exec", CONFIG_NAME, "+sv_lan", str(int(options.lan)),
                 "+coop", "0", "+deathmatch", "1", "+maxplayers", str(options.maxplayers),
                 "+log", "on", "+map", options.map_name.lower()])
    return args


def running_game_processes(game_dir: Path | str) -> list[dict[str, Any]]:
    """Read process identity without stopping or changing existing game sessions."""
    root = validate_game_dir(game_dir)
    key = os.path.normcase(str(root))
    known = _LIVE_PROCESSES.get(key)
    found: list[dict[str, Any]] = []
    if known is not None:
        if known.poll() is None:
            found.append({"pid": known.pid, "path": str(root), "name": "launcher game"})
        else:
            _LIVE_PROCESSES.pop(key, None)
    if os.name != "nt":
        return found
    command = (
        "$ErrorActionPreference = 'Stop'; "
        "@(Get-CimInstance Win32_Process -Filter \"Name = 'cof.exe' OR Name = 'hlds.exe'\" | "
        "Select-Object ProcessId, ExecutablePath, Name) | ConvertTo-Json -Compress"
    )
    try:
        result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", command],
                                capture_output=True, text=True, check=True, timeout=15,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        processes = json.loads(result.stdout.strip() or "[]")
    except (OSError, subprocess.SubprocessError, json.JSONDecodeError) as exc:
        raise RuntimeError("Could not check existing game processes; no configuration was changed.") from exc
    if isinstance(processes, dict):
        processes = [processes]
    for process in processes:
        executable = process.get("ExecutablePath")
        if not executable:
            raise RuntimeError(f"Cannot identify running {process.get('Name', 'game')} "
                               f"PID {process.get('ProcessId')}; close it before preparing another launch.")
        # Lexical comparison avoids reading any Steam/D: process executable.
        parent = os.path.normcase(str(Path(executable).parent))
        if parent == key and not any(item["pid"] == process["ProcessId"] for item in found):
            found.append({"pid": process["ProcessId"], "path": executable, "name": process.get("Name")})
    return found


def _assert_not_running(root: Path) -> None:
    active = running_game_processes(root)
    if active:
        pids = ", ".join(str(item["pid"]) for item in active)
        raise RuntimeError(f"This game copy is already running (PID {pids}). Close it before launching again; "
                           "its configuration has not been changed.")


def _read_steam_active_process() -> tuple[int, int]:
    """Read Steam's signed-in session marker, not just the presence of steam.exe."""
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam\ActiveProcess") as key:
            pid = winreg.QueryValueEx(key, "pid")[0]
            user = winreg.QueryValueEx(key, "ActiveUser")[0]
    except OSError:
        return 0, 0
    return (pid if isinstance(pid, int) else 0, user if isinstance(user, int) else 0)


def _process_is_live(pid: int) -> bool:
    """Test a Windows PID without terminating it or invoking another application."""
    import ctypes
    from ctypes import wintypes
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
    kernel32.WaitForSingleObject.restype = wintypes.DWORD
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel32.CloseHandle.restype = wintypes.BOOL
    synchronize = 0x00100000
    handle = kernel32.OpenProcess(synchronize, False, pid)
    if not handle:
        return False
    try:
        return kernel32.WaitForSingleObject(handle, 0) == 0x00000102  # WAIT_TIMEOUT
    finally:
        kernel32.CloseHandle(handle)


def ensure_steam_ready() -> None:
    """Reject a missing/stale Windows Steam session before any client changes.

    The presence of steam.exe alone is insufficient: Steam can remain running
    with ActiveProcess pid/ActiveUser set to zero and fail game authentication.
    This is a startup check, not proof that the later Steam ticket will succeed.
    Dedicated servers do not need this client-session preflight.
    """
    if os.name != "nt":
        return
    pid, user = _read_steam_active_process()
    if pid <= 0 or user <= 0 or not _process_is_live(pid):
        raise RuntimeError("Steam has no active signed-in session. Open Steam and sign in, or fully exit "
                           "and reopen Steam, then try again. Merely having steam.exe running is not enough; "
                           "no game configuration was changed.")


def _write_managed_config(root: Path, config: str) -> None:
    game = root / "cryoffear"
    targets = [game / CONFIG_NAME, game / "userconfig.cfg"]
    for target in targets:
        resolved = _local_path(target)
        if not resolved.is_relative_to(root) or target.is_symlink():
            raise ValueError(f"Refusing configuration path outside the game copy: {target}")
        if target.exists() and target.stat().st_nlink > 1:
            raise ValueError(f"Refusing a hard-linked configuration file: {target}")
    userconfig = targets[1]
    original = userconfig.read_bytes() if userconfig.exists() else b""
    # Ignore comments; retain all existing bytes, including non-UTF8 key bindings.
    reference = re.compile(rb'^\s*exec\s+"?cofcoop_launcher\.cfg"?\s*(?://.*)?$', re.I | re.M)
    updated = original
    if not reference.search(original):
        newline = b"\r\n" if b"\r\n" in original else b"\n"
        prefix = b"" if not original or original.endswith((b"\n", b"\r")) else newline
        updated = original + prefix + b"// Cry of Fear co-op launcher" + newline + b"exec " + CONFIG_NAME.encode("ascii") + newline
    if original and updated != original:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        backup = userconfig.with_name(f"userconfig.cfg.cofcoop-backup-{stamp}")
        shutil.copy2(userconfig, backup)
    _atomic_write(targets[0], config.encode("utf-8"))
    if updated != original:
        _atomic_write(userconfig, updated)


def prepare_launch(options: LaunchOptions) -> list[str]:
    """Prepare a local copy without starting it; preserve user config and backup."""
    with _LAUNCH_LOCK:
        args = build_launch_args(options)
        config = build_config(options)
        root = validate_game_dir(options.game_dir)
        _assert_not_running(root)
        if not options.dedicated:
            ensure_steam_ready()
        _write_managed_config(root, config)
        return args


def launch_game(options: LaunchOptions) -> subprocess.Popen:
    """Start one host after all validation; the caller announces the game window."""
    with _LAUNCH_LOCK:
        args = prepare_launch(options)
        root = validate_game_dir(options.game_dir)
        process = subprocess.Popen(args, cwd=str(root))
        _LIVE_PROCESSES[os.path.normcase(str(root))] = process
        return process


def validate_endpoint(address: str) -> str:
    """Validate a retail GoldSrc IPv4/DNS endpoint without resolving it."""
    _safe_text(address, "Server address", limit=260)
    if address != address.strip() or any(char.isspace() for char in address):
        raise ValueError("Server addresses cannot contain spaces.")
    if address.count(":") > 1:
        raise ValueError("The retail game needs an IPv4 address or DNS hostname, optionally :port.")
    host, separator, port_text = address.partition(":")
    if not host or (separator and (not port_text or not port_text.isascii() or not port_text.isdecimal())):
        raise ValueError("Use a hostname or IPv4 address followed by an optional :port.")
    port = int(port_text) if separator else 27015
    _integer(port, "Server port", 1, 65535)
    if re.fullmatch(r"[0-9.]+", host):
        try:
            ipaddress.IPv4Address(host)
        except ipaddress.AddressValueError as exc:
            raise ValueError("Invalid IPv4 address.") from exc
    else:
        if len(host) > 253 or not all(re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?", label)
                                     for label in host.rstrip(".").split(".")):
            raise ValueError("Invalid DNS hostname.")
    return f"{host.lower()}:{port}"


def launch_join(game_dir: Path | str, name: str, address: str, *, password: str = "") -> subprocess.Popen:
    """Start the local client with a validated endpoint and managed client rates."""
    with _LAUNCH_LOCK:
        root = validate_game_dir(game_dir)
        _safe_text(name, "Player name", limit=31)
        _safe_text(password, "Server password", allow_empty=True)
        endpoint = validate_endpoint(address)
        _assert_not_running(root)
        ensure_steam_ready()
        config = "\n".join([
            "// Managed client settings for the Cry of Fear co-op launcher.",
            f'name "{name}"', f'password "{password}"',
            "rate 100000", "cl_cmdrate 100", "cl_updaterate 100", "",
        ])
        _write_managed_config(root, config)
        args = [str(root / "cof.exe"), "-basedir", "cryoffear", "-game", "cryoffear",
                "-hw", "-heapsize", "1536000", "-num_edicts", "4096", "-no3dmenu",
                "-windowed", "-condebug", "+exec", CONFIG_NAME, "+connect", endpoint]
        process = subprocess.Popen(args, cwd=str(root))
        _LIVE_PROCESSES[os.path.normcase(str(root))] = process
        return process


class LogTracker:
    """Incremental confirmed map-load tracking with rotation/truncation handling.

    `CHANGE LEVEL` and `Loading map` are requests, so they never create a
    checkpoint. GoldSrc's `Started map` log line confirms a running map. Create
    this tracker with start_at_end=True before launching to ignore older runs.
    """
    _STARTED = re.compile(r'^(?:L \d{2}/\d{2}/\d{4} - \d{2}:\d{2}:\d{2}:\s*)?'
                          r'(?:Started map "([A-Za-z0-9_-]+)"(?:\s+\(CRC "[^"\r\n]*"\))?'
                          r'|Server started on map "?([A-Za-z0-9_-]+)"?)\s*$', re.I)

    def __init__(self, log_path: Path | str, *, start_at_end: bool = False):
        self.path = Path(log_path)
        self.offset = 0
        self._identity: tuple[int, int] | None = None
        self._anchor = b""
        self._pending = b""
        self._last_map: str | None = None
        if start_at_end:
            try:
                with self.path.open("rb") as stream:
                    stat = os.fstat(stream.fileno())
                    self.offset = stat.st_size
                    self._identity = (stat.st_dev, stat.st_ino)
                    stream.seek(max(0, self.offset - 128))
                    self._anchor = stream.read()
            except FileNotFoundError:
                pass

    def poll(self) -> list[dict[str, str]]:
        try:
            stream = self.path.open("rb")
        except FileNotFoundError:
            return []
        with stream:
            stat = os.fstat(stream.fileno())
            identity = (stat.st_dev, stat.st_ino)
            replaced = self._identity is not None and identity != self._identity
            truncated = stat.st_size < self.offset
            if not replaced and not truncated and self._anchor:
                stream.seek(self.offset - len(self._anchor))
                truncated = stream.read(len(self._anchor)) != self._anchor
            if replaced or truncated:
                self.offset = 0
                self._pending = b""
                self._anchor = b""
                self._last_map = None
            self._identity = identity
            stream.seek(self.offset)
            data = stream.read(2 * 1024 * 1024)
            self.offset = stream.tell()
            self._anchor = (self._anchor + data)[-128:]
        lines = (self._pending + data).split(b"\n")
        self._pending = lines.pop()
        # A broken log line cannot grow an unbounded buffer.
        if len(self._pending) > 65536:
            self._pending = b""
        events: list[dict[str, str]] = []
        for raw in lines:
            match = self._STARTED.fullmatch(raw.decode("latin-1").strip())
            if match:
                name = (match[1] or match[2]).lower()
                if _MAP_NAME.fullmatch(name) and name != self._last_map:
                    events.append({"kind": "map", "map": name})
                    self._last_map = name
        return events
