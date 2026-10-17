"""Automated live runs of the story co-op in a game copy (a developer tool: it starts the game; ask first).

A run starts the copy's game on a map with AI companions and drives it live over loopback RCON (the player's
moves, cheats, waits in real seconds, questions about where everyone is), then quits it and reads the session
log. Run after run, an engine change can be built, installed and tried with nobody at the keyboard.

    python -m tools.live_loop scenarios
    python -m tools.live_loop run COPY SCENARIO [--out DIR] [--companions N] [--keep-open]
    python -m tools.live_loop report LOG
    python -m tools.live_loop install COPY XASH_DLL     (an engine build into the copy; the old one kept as .bak)

Every run: -nosound (and, should the game open a Windows audio session anyway, that session muted, and unmuted
again before the game quits, so the copy keeps its sound for the next person); sv_cheats, the entity tools and an
RCON password on for this run only; the copy's story settings, configs and saves put back afterwards. The game
is closed if it outlives the scenario.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import contextlib
from dataclasses import dataclass, field
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import subprocess
import sys
import tempfile
import time

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "evidence" / "live-loop"
AUDIO = Path(__file__).with_name("game_audio.ps1")
PORT = 27015
STAMP = re.compile(r"^\[(\d{4}):(\d\d):(\d\d)\|(\d\d):(\d\d):(\d\d)\] ")
POWERSHELL = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


# --- the game's processes and audio -------------------------------------------------------------------------

def game_processes(copy: Path) -> list[int]:
    """Process ids of this copy's game (CoFLaunchApp.exe and cof.exe started from it)."""
    command = ('Get-CimInstance Win32_Process -Filter "Name=\'cof.exe\' OR Name=\'CoFLaunchApp.exe\'" | '
               'Select-Object ProcessId,ExecutablePath | ConvertTo-Json -Compress')
    out = subprocess.run([str(POWERSHELL), "-NoProfile", "-NonInteractive", "-Command", command],
                         capture_output=True, text=True, timeout=30, creationflags=NO_WINDOW).stdout
    rows = json.loads(out) if out.strip() else []
    rows = [rows] if isinstance(rows, dict) else rows
    prefix = (str(Path(copy).resolve()) + os.sep).casefold()
    return [int(r["ProcessId"]) for r in rows if str(r.get("ExecutablePath") or "").casefold().startswith(prefix)]


class AudioGuard:
    """game_audio.ps1 guard: from before the game starts until stop(), every audio session of a process from the
    copy is muted the moment it appears; stop() unmutes them (the game still running: Windows remembers a session's
    mute for the program)."""

    def __init__(self, copy: Path):
        import threading
        self.stop_file = Path(tempfile.gettempdir()) / f"live-loop-audio-stop-{secrets.token_hex(6)}"
        self.lines: list[str] = []
        self.process = subprocess.Popen([str(POWERSHELL), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                                         "-File", str(AUDIO), "-Action", "guard", "-Root", str(Path(copy).resolve()),
                                         "-Stop", str(self.stop_file)],
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                        creationflags=NO_WINDOW)
        self.ready = threading.Event()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        if not self.ready.wait(30):
            raise SystemExit("The audio guard did not start; the game was not started.")

    def _read(self) -> None:
        for line in self.process.stdout:
            line = line.rstrip()
            if line == "guard ready":
                self.ready.set()
            elif line:
                self.lines.append(line)
                print(f"  audio: {line}")

    def stop(self) -> None:
        self.stop_file.write_text("stop", encoding="ascii")
        try:
            self.process.wait(20)
        except subprocess.TimeoutExpired:
            self.process.kill()
        self.reader.join(5)
        self.stop_file.unlink(missing_ok=True)


def kill(pids: list[int]) -> None:
    for pid in pids:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(pid)], capture_output=True, creationflags=NO_WINDOW)


# --- RCON (Xash: "rcon <password> <command>", no challenge) --------------------------------------------------

class Rcon:
    def __init__(self, password: str, port: int = PORT):
        self.password, self.port = password, port

    def __call__(self, command: str, timeout: float = 3.0) -> str:
        packet = b"\xff\xff\xff\xffrcon " + self.password.encode("ascii") + b" " + command.encode("latin-1") + b"\n"
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(timeout)
            sock.sendto(packet, ("127.0.0.1", self.port))
            parts = []
            try:
                while True:
                    data, _ = sock.recvfrom(65536)
                    text = data[4:].decode("latin-1", "replace") if data.startswith(b"\xff\xff\xff\xff") else ""
                    parts.append(text[6:] if text.startswith("print\n") else text[1:] if text.startswith("l") else text)
                    sock.settimeout(0.3)
            except (socket.timeout, ConnectionResetError):
                pass
        return "".join(parts)


# --- one live run --------------------------------------------------------------------------------------------

@dataclass
class Game:
    """A running game in a copy, driven over RCON. Use Game.session(...) as a context manager."""
    copy: Path
    rcon: Rcon
    log_path: Path
    guard: AudioGuard | None = None
    started: float = field(default_factory=time.time)

    def cmd(self, command: str, timeout: float = 3.0) -> str:
        return self.rcon(command, timeout)

    def log(self) -> str:
        try:
            return self.log_path.read_text(encoding="latin-1", errors="replace")
        except OSError:
            return ""

    def wait_for(self, pattern: str, timeout: float, after: int = 0) -> re.Match | None:
        """Wait until the session log (from character after) matches pattern."""
        regex = re.compile(pattern)
        deadline = time.time() + timeout
        while time.time() < deadline:
            if m := regex.search(self.log(), after):
                return m
            if not game_processes(self.copy):
                return None
            time.sleep(0.5)
        return None

    def mark(self, text: str) -> int:
        """A marker line in the log (echo), and the log's length before it."""
        at = len(self.log())
        self.cmd(f"echo live-loop {text}")
        return at

    def sleep(self, seconds: float) -> None:
        time.sleep(seconds)

    def at(self, x: float, y: float, z: float) -> None:
        self.cmd(f'ent_fire 1 set origin "{x:g} {y:g} {z:g}"')

    def look(self, pitch: float, yaw: float) -> None:
        self.cmd(f"ent_fire 1 angles {pitch:g} {yaw:g} 0")
        self.cmd("ent_fire 1 set fixangle 1")

    def hold(self, button: str, seconds: float) -> None:
        """+forward, +back, +moveleft, +moveright, +jump, +duck, +use, +attack... for this long."""
        self.cmd(f"+{button}")
        time.sleep(seconds)
        self.cmd(f"-{button}")


def _snapshot_cfgs(copy: Path) -> dict:
    game = copy / "cryoffear"
    return {p.name: p.read_bytes() for p in game.glob("*.cfg")} | {
        p.name: p.read_bytes() for p in game.glob("*.json")}


def _restore_cfgs(copy: Path, kept: dict) -> None:
    game = copy / "cryoffear"
    for name, data in kept.items():
        path = game / name
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)


@contextlib.contextmanager
def session(copy: Path, map_name: str, *, companions: int = 1, merged: bool = True, bonus_room: bool = False,
            coop_flashlight: bool = False, cheats: bool = True, extra: tuple = (), ready_timeout: float = 120.0):
    """Start the copy's game on map_name, yield a Game once the host is in, close it afterwards and put the copy's
    story settings, configs and saves back."""
    from tools import coop_app as app
    from tools import coop_play as play
    from tools import friend_setup as setup_lib
    from tools.merged_maps_check import saves_kept
    copy = Path(copy).resolve()
    try:
        setup_lib.validate_runtime(copy)
    except ValueError:
        if not dev_engine(copy):
            raise
        print(f"  developer engine {dev_engine(copy)} (python -m tools.live_loop restore puts the packaged one back)")
    if game_processes(copy):
        raise SystemExit("The game is already running in this copy.")
    settings_file = copy / play.SETTINGS
    settings_before = settings_file.read_bytes() if settings_file.exists() else None
    cfgs = _snapshot_cfgs(copy)
    password = secrets.token_hex(8)
    args, config = play.host_launch(copy, solo=True, name="Tester", map_name=map_name, players=companions + 1,
                                    password="", companions=companions, port=PORT, cont=False, video=[],
                                    bonus_room=bonus_room, merged=merged, coop_flashlight=coop_flashlight)
    config += f'rcon_password "{password}"\nrcon_enable 1\n'
    # silent from the first frame: the guard mutes what still opens a session; the volumes come back with the cfgs
    story = play.story_config() + "volume 0\nMP3Volume 0\n"
    if cheats:
        story = story.replace("sv_cheats 0", "sv_cheats 1").replace("sv_enttools_enable 0", "sv_enttools_enable 1")
    game_dir = copy / "cryoffear"
    (game_dir / play.STORY_CFG).write_text(story, encoding="ascii")
    (game_dir / play.SESSION_CFG).write_text(config, encoding="ascii")
    setup_lib.keep_previous_log(copy, play.LOG)
    game = Game(copy, Rcon(password), copy / play.LOG)
    try:
        with saves_kept(copy):
            game.guard = AudioGuard(copy)
            try:
                app.start_game(copy, [args[0], "-nosound", *args[1:], *extra])
                if not game.wait_for(r"cofstory phase=|cof-story-transition\] phase=ready", ready_timeout):
                    raise SystemExit("The game did not come up (see the session log).")
                time.sleep(3)
                yield game
            finally:
                _close(game)
    finally:
        (game_dir / play.STORY_CFG).write_text(play.story_config(), encoding="ascii")
        (game_dir / play.SESSION_CFG).write_text(config.replace(f'rcon_password "{password}"\n', 'rcon_password ""\n'),
                                                 encoding="ascii")
        _restore_cfgs(copy, cfgs)
        if settings_before is not None and settings_file.read_bytes() != settings_before:
            settings_file.write_bytes(settings_before)


def _close(game: Game, timeout: float = 30.0) -> None:
    if game.guard:
        game.guard.stop()   # unmutes while the game still runs: the session's mute is remembered for the exe
    if not game_processes(game.copy):
        return
    try:
        game.cmd("quit", timeout=1.0)
    except OSError:
        pass
    deadline = time.time() + timeout
    while time.time() < deadline and game_processes(game.copy):
        time.sleep(1)
    left = game_processes(game.copy)
    if left:
        print("  the game did not quit: closed it")
        kill(left)


# --- reading a log -------------------------------------------------------------------------------------------

@dataclass
class Report:
    minutes: float
    lines: int
    errors: list
    modes: Counter          # companion-mode lines per userid
    worst_second: tuple     # (count, "hh:mm:ss uN")
    teleports: Counter      # by reason
    maps: list
    notes: list

    def text(self) -> str:
        out = [f"{self.minutes:.1f} min, {self.lines} lines; maps: {' -> '.join(self.maps) or '-'}"]
        rate = sum(self.modes.values()) / self.minutes if self.minutes else 0.0
        out.append(f"companion mode changes: {sum(self.modes.values())} ({rate:.0f}/min) "
                   f"{dict(self.modes)}; worst second: {self.worst_second[0]} ({self.worst_second[1]})")
        out.append(f"companion teleports: {sum(self.teleports.values())} {dict(self.teleports)}")
        out += [f"error: {e}" for e in self.errors[:30]]
        out += self.notes
        return "\n".join(out)


def read_report(log: str) -> Report:
    times, errors, maps, notes = [], [], [], []
    modes, teleports, per_second = Counter(), Counter(), Counter()
    lines = log.splitlines()
    for line in lines:
        if m := STAMP.match(line):
            times.append(datetime(*map(int, m.groups())))
        body = line[STAMP.match(line).end():] if STAMP.match(line) else line
        if m := re.search(r"phase=companion-mode userid=(\d+)", body):
            modes[m[1]] += 1
            per_second[f"{line[12:20]} u{m[1]}"] += 1
        elif m := re.search(r"phase=companion-teleport userid=\d+ leader=\d+ reason=(\w+)", body):
            teleports[m[1]] += 1
        elif m := re.match(r"Spawn Server: (\S+)", body):
            maps.append(m[1])
        if re.search(r"\b(Error|ERROR|Host_Error|FATAL|Sys_Error|assert|crash)", body) and "Could not load sound" not in body:
            errors.append(body.strip())
        if re.search(r"SV_AllocEdict|no free edicts|MAX_|overflow", body, re.I):
            notes.append(f"limit: {body.strip()}")
    minutes = (times[-1] - times[0]).total_seconds() / 60.0 if len(times) > 1 else 0.0
    worst = per_second.most_common(1)[0][::-1] if per_second else (0, "-")
    return Report(minutes, len(lines), errors, modes, worst, teleports, maps, notes)


# --- scenarios -----------------------------------------------------------------------------------------------

SCENARIOS = {}


def scenario(name: str, map_name: str, companions: int = 2, **options):
    def register(fn):
        SCENARIOS[name] = (fn, map_name, companions, options)
        return fn
    return register


@scenario("smoke", "c_start2", companions=2)
def smoke(game: Game) -> None:
    """The game comes up with two companions, they are added, the player stands for 20 s."""
    game.wait_for(r"companion-added userid=\d+ name=Companion 2", 60)
    game.cmd("god"); game.cmd("notarget")
    game.sleep(20)


# Where the player stood in c_start2 during the 2026-10-16 evening playtest (companion-teleport leader_at),
# the street, the alleys and the computer room where the bots flipped between follow and idle every frame.
C_START2_SPOTS = [(1177, -1871, -860), (1415, -1883, -860), (1682, -869, -832), (1682, -698, -821),
                  (677, 1375, -568), (517, 1412, -568), (1109, 1839, -555), (1126, 1624, -555),
                  (799, 1110, -568), (800, 1046, -568), (1674, -1144, -860), (1686, -1374, -860)]


@scenario("follow", "c_start2", companions=2)
def follow(game: Game) -> None:
    """Two bots follow the player through c_start2's spots from the 10-16 playtest: a short walk and a stand at each."""
    game.wait_for(r"companion-added userid=\d+ name=Companion 2", 60)
    game.cmd("god"); game.cmd("notarget"); game.cmd("cof_companion_debug 1")
    game.sleep(3)
    for k, (x, y, z) in enumerate(C_START2_SPOTS):
        game.mark(f"spot {k} {x} {y} {z}")
        game.at(x, y, z)
        game.sleep(6)
        game.hold("forward", 1.0)
        game.sleep(4)
        game.hold("back", 1.0)
        game.sleep(4)


def level_change(game: Game, trigger: str | None, destination: str, timeout: float = 90.0, spot: tuple | None = None) -> bool:
    """Fire a level change trigger as the player would (its Use with the player), or put the player on a spot that
    takes them out; wait for the new map and for the companions to be added again."""
    at = len(game.log())
    if trigger:
        game.cmd(f"ent_fire {trigger} use")
    if spot:
        game.at(*spot)
    if not game.wait_for(rf"\[cof-story-transition\] phase=ready generation=\d+ map={destination}\b", timeout, at):
        return False
    game.wait_for(r"companion-added userid=\d+ name=Companion 2", 30, at)
    game.cmd("god"); game.cmd("notarget")
    return True


def skip_cutscenes(game: Game, seconds: float = 12.0) -> None:
    end = time.time() + seconds
    while time.time() < end:
        game.cmd("cof_skip")
        game.sleep(2)


def walk_spots(game: Game, spots: list, stand: float = 4.0) -> None:
    for k, (x, y, z) in enumerate(spots):
        game.mark(f"spot {k} {x} {y} {z}")
        game.at(x, y, z)
        game.sleep(6)
        game.hold("forward", 1.0)
        game.sleep(stand)
        game.hold("back", 1.0)
        game.sleep(stand)


@scenario("levels", "c_start", companions=2)
def levels(game: Game) -> None:
    """c_start -> c_start2 -> c_start through the story's own level change with two bots, walking the playtest's spots."""
    game.wait_for(r"companion-added userid=\d+ name=Companion 2", 90)
    game.cmd("god"); game.cmd("notarget"); game.cmd("cof_companion_debug 1")
    skip_cutscenes(game)
    walk_spots(game, [(2376, -1254, -731), (2167, -1300, -731)])
    if not level_change(game, "haehecl", "c_start2"):
        game.mark("FAILED level change to c_start2")
        return
    game.cmd("cof_companion_debug 1")
    walk_spots(game, C_START2_SPOTS[:8])
    # c_start2's arrival spot is inside its way back to c_start
    if not level_change(game, None, "c_start", spot=(2424, -940, -731)):
        game.mark("FAILED level change to c_start")
        return
    game.cmd("cof_companion_debug 1")
    walk_spots(game, [(2376, -1254, -731), (2167, -1300, -731), (2101, -1534, -731)])


def run(copy: Path, name: str, out: Path, companions: int | None = None, keep_open: bool = False) -> Path:
    fn, map_name, default_companions, options = SCENARIOS[name]
    out.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    print(f"{stamp} {name}: {map_name}, {companions or default_companions} companions")
    with session(copy, map_name, companions=companions or default_companions, **options) as game:
        fn(game)
        game.mark("done")
        if keep_open:
            input("The game stays open; Enter closes it. ")
        log = game.log()
    saved = out / f"{stamp}-{name}.log"
    saved.write_text(log, encoding="latin-1")
    report = read_report(log)
    (out / f"{stamp}-{name}.txt").write_text(report.text() + "\n", encoding="utf-8")
    print(report.text())
    print(f"log: {saved}")
    return saved


MARKER = "live-loop-engine.json"   # in the copy: the packaged engine's hash and the developer build put in


def _sha(path: Path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest().upper()


def dev_engine(copy: Path) -> str | None:
    """The developer build this tool installed, while it is still the copy's xash.dll (its hash's first 8)."""
    try:
        note = json.loads((Path(copy) / MARKER).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    current = _sha(Path(copy) / "xash.dll")
    return current[:8] if current == note.get("installed") else None


def install(copy: Path, dll: Path) -> None:
    """Put an engine build into the copy; the packaged one stays beside it as xash.dll.<hash8>.bak."""
    copy = Path(copy)
    target, marker = copy / "xash.dll", copy / MARKER
    try:
        packaged = json.loads(marker.read_text(encoding="utf-8"))["packaged"]
    except (OSError, ValueError, KeyError):
        packaged = _sha(target)
    backup = target.with_name(f"xash.dll.{packaged[:8]}.bak")
    if not backup.exists():
        if _sha(target) != packaged:
            raise SystemExit(f"The packaged engine {packaged[:8]} is neither installed nor kept as {backup.name}.")
        shutil.copy2(target, backup)
    new = _sha(dll)
    shutil.copy2(dll, target)
    marker.write_text(json.dumps({"packaged": packaged, "installed": new}), encoding="utf-8")
    print(f"xash.dll {new[:8]} installed (packaged {packaged[:8]} kept as {backup.name})")


def restore(copy: Path) -> None:
    """The packaged engine back (the copy then matches its install again)."""
    copy = Path(copy)
    marker = copy / MARKER
    packaged = json.loads(marker.read_text(encoding="utf-8"))["packaged"]
    shutil.copy2(copy / f"xash.dll.{packaged[:8]}.bak", copy / "xash.dll")
    marker.unlink()
    print(f"xash.dll {packaged[:8]} (packaged) restored")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("scenarios")
    r = sub.add_parser("run")
    r.add_argument("copy", type=Path)
    r.add_argument("scenario", choices=sorted(SCENARIOS))
    r.add_argument("--out", type=Path, default=EVIDENCE)
    r.add_argument("--companions", type=int)
    r.add_argument("--keep-open", action="store_true")
    p = sub.add_parser("report")
    p.add_argument("log", type=Path)
    i = sub.add_parser("install")
    i.add_argument("copy", type=Path)
    i.add_argument("dll", type=Path)
    sub.add_parser("restore").add_argument("copy", type=Path)
    a = parser.parse_args(argv)
    if a.command == "scenarios":
        for name, (fn, map_name, companions, _) in sorted(SCENARIOS.items()):
            print(f"{name:20} {map_name:14} {companions} AI  {(fn.__doc__ or '').strip().splitlines()[0]}")
    elif a.command == "run":
        run(a.copy, a.scenario, a.out, a.companions, a.keep_open)
    elif a.command == "report":
        print(read_report(a.log.read_text(encoding="latin-1", errors="replace")).text())
    elif a.command == "install":
        install(a.copy, a.dll)
    elif a.command == "restore":
        restore(a.copy)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
