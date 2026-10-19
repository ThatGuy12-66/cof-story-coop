"""Automated live runs of the story co-op in a game copy (a developer tool: it starts the game; ask first).

A run starts the copy's game on a map with AI companions and drives it live over loopback RCON (the player's
moves, cheats, waits in real seconds, questions about where everyone is), then quits it and reads the session
log. Run after run, an engine change can be built, installed and tried with nobody at the keyboard.

    python -m tools.live_loop scenarios
    python -m tools.live_loop run COPY SCENARIO [--out DIR] [--companions N] [--keep-open]
    python -m tools.live_loop report LOG
    python -m tools.live_loop install COPY XASH_DLL     (an engine build into the copy; the old one kept as .bak)

    python -m tools.live_loop recover COPY              (after a run that was killed: saves and cfgs back)

Every run: the game starts through the copy's test launcher, CoFLaunchApp-autotest.exe (a copy of CoFLaunchApp.exe),
with -nosound, and every audio session of that program is muted the moment it appears and stays muted (Cry of Fear
still plays through a session of its own). Windows remembers that program's mute, so later runs start silent; the
player's own launcher and every other program keep their sound, and the output devices are never touched.
sv_cheats, the entity tools and an RCON password are on for the run only; the copy's story settings, cfgs, launcher
settings and saves are put back afterwards. A recovery record under evidence/live-loop/pending holds all of that
while a run lasts, so a run killed halfway is undone by the next one (or by recover). The game is closed if it
outlives the scenario, or at once if one of its audio sessions will not mute.
"""
from __future__ import annotations

import argparse
from collections import Counter
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
import time

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "evidence" / "live-loop"
AUDIO = Path(__file__).with_name("game_audio.ps1")
TEST_EXE = "CoFLaunchApp-autotest.exe"   # the game under a name of its own: its muted sound stays its own
PORT = 27015
STAMP = re.compile(r"^\[(\d{4}):(\d\d):(\d\d)\|(\d\d):(\d\d):(\d\d)\] ")
POWERSHELL = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


# --- the game's processes and audio -------------------------------------------------------------------------

def game_processes(copy: Path) -> list[int]:
    """Process ids of this copy's game (CoFLaunchApp.exe and cof.exe started from it)."""
    command = ('Get-CimInstance Win32_Process -Filter "Name=\'cof.exe\' OR Name=\'CoFLaunchApp.exe\' OR '
               f'Name=\'{TEST_EXE}\'" | '
               'Select-Object ProcessId,ExecutablePath | ConvertTo-Json -Compress')
    out = subprocess.run([str(POWERSHELL), "-NoProfile", "-NonInteractive", "-Command", command],
                         capture_output=True, text=True, timeout=30, creationflags=NO_WINDOW).stdout
    rows = json.loads(out) if out.strip() else []
    rows = [rows] if isinstance(rows, dict) else rows
    prefix = (str(Path(copy).resolve()) + os.sep).casefold()
    return [int(r["ProcessId"]) for r in rows if str(r.get("ExecutablePath") or "").casefold().startswith(prefix)]


PENDING = EVIDENCE / "pending"   # the recovery record of the run in progress


def test_launcher(copy: Path) -> Path:
    """The copy's launcher under the test name (made again when the launcher changes)."""
    source, target = copy / "CoFLaunchApp.exe", copy / TEST_EXE
    if not target.exists() or target.read_bytes() != source.read_bytes():
        shutil.copy2(source, target)
    return target


class AudioGuard:
    """game_audio.ps1 guard over the test launcher: from before the game starts until after it has exited, each of
    its audio sessions is muted the moment it appears (and stays muted). A session that will not mute closes the
    game at once."""

    def __init__(self, copy: Path, exe: Path):
        import threading
        self.copy = copy
        self.stop_file = EVIDENCE / f"audio-stop-{secrets.token_hex(6)}"
        self.failed = threading.Event()
        self.process = subprocess.Popen([str(POWERSHELL), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                                         "-File", str(AUDIO), "-Action", "guard", "-Exe", str(exe),
                                         "-Stop", str(self.stop_file)],
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                        creationflags=NO_WINDOW)
        self.ready = threading.Event()
        threading.Thread(target=self._read, daemon=True).start()
        if not self.ready.wait(30):
            self.process.kill()
            raise SystemExit("The audio guard did not start; the game was not started.")

    def _read(self) -> None:
        for line in self.process.stdout:
            line = line.rstrip()
            if line == "guard ready":
                self.ready.set()
            elif line:
                print(f"  audio: {line}")
                if "NOT MUTED" in line or line.startswith("error"):
                    self.failed.set()
                    kill(game_processes(self.copy))

    def stop(self) -> None:
        EVIDENCE.mkdir(parents=True, exist_ok=True)
        self.stop_file.write_text("stop", encoding="ascii")
        try:
            self.process.wait(20)
        except subprocess.TimeoutExpired:
            self.process.kill()
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
    map_name: str = ""
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


def _kept_files(copy: Path) -> list[Path]:
    """What a run changes and puts back: the game's cfgs (volumes included), the launcher's settings, the saves."""
    from tools import coop_play as play
    game = copy / "cryoffear"
    return [*game.glob("*.cfg"), *game.glob("*.json"), copy / play.SETTINGS]


def _begin(copy: Path) -> Path:
    """The recovery record: the copy's kept files and saves."""
    if PENDING.exists():
        recover(copy)
    record = PENDING
    (record / "files").mkdir(parents=True)
    (record / "copy.txt").write_text(str(copy), encoding="utf-8")
    for path in _kept_files(copy):
        if path.is_file():
            shutil.copy2(path, record / "files" / f"{path.parent.name}--{path.name}")
    if (copy / "SAVE").exists():
        shutil.copytree(copy / "SAVE", record / "SAVE")
    return record


def recover(copy: Path | None = None) -> bool:
    """Undo a run (finished or killed): the game closed, the kept files and saves put back."""
    if not PENDING.exists():
        return False
    copy = Path((PENDING / "copy.txt").read_text(encoding="utf-8")) if (PENDING / "copy.txt").exists() else copy
    left = game_processes(copy) if copy else []
    if left:
        kill(left)
        time.sleep(2)
    if copy and (PENDING / "files").exists():
        for kept in (PENDING / "files").iterdir():
            folder, name = kept.name.split("--", 1)
            target = (copy / "cryoffear" if folder == "cryoffear" else copy) / name
            if not target.exists() or target.read_bytes() != kept.read_bytes():
                shutil.copy2(kept, target)
    if copy and (PENDING / "SAVE").exists():
        if (copy / "SAVE").exists():
            shutil.rmtree(copy / "SAVE")
        shutil.copytree(PENDING / "SAVE", copy / "SAVE")
    elif copy and (PENDING / "files").exists() and (copy / "SAVE").exists():
        shutil.rmtree(copy / "SAVE")   # there were none before the run
    shutil.rmtree(PENDING)
    return True


VIDEO = ["-windowed", "-width", "960", "-height", "540"]   # a small window, not the whole desktop


def launch_config(copy: Path, map_name: str, *, companions: int, password: str, merged: bool = True,
                  bonus_room: bool = False, coop_flashlight: bool = False) -> tuple[list[str], str]:
    """A test run's command line and session cfg, with RCON for the scenario; companions 0 turns the bots off.
    The game ignores the mouse (m_ignore: no mouse look, the cursor stays free; it locks once the player has
    joined, so it is set in the session cfg) so that someone using the PC during a run does not turn the camera."""
    from tools import coop_play as play
    bots = max(companions, 1)   # a solo game keeps a place for one at least
    args, config = play.host_launch(copy, solo=True, name="Tester", map_name=map_name, players=bots + 1,
                                    password="", companions=bots, port=PORT, cont=False, video=VIDEO,
                                    bonus_room=bonus_room, merged=merged, coop_flashlight=coop_flashlight)
    if not companions:
        config = config.replace("cof_companion 1\n", "cof_companion 0\n")
    return args, config + f'rcon_password "{password}"\nrcon_enable 1\nm_ignore 1\n'


@contextlib.contextmanager
def session(copy: Path, map_name: str, *, companions: int = 1, merged: bool = True, bonus_room: bool = False,
            coop_flashlight: bool = False, cheats: bool = True, extra: tuple = (), ready_timeout: float = 120.0):
    """Start the copy's game on map_name, yield a Game once the host is in; afterwards close it and put the copy's
    settings and saves back."""
    from tools import coop_app as app
    from tools import coop_play as play
    from tools import friend_setup as setup_lib
    copy = Path(copy).resolve()
    try:
        setup_lib.validate_runtime(copy)
    except ValueError:
        if not dev_engine(copy):
            raise
        print(f"  developer engine {dev_engine(copy)} (python -m tools.live_loop restore puts the packaged one back)")
    if game_processes(copy) and not PENDING.exists():
        raise SystemExit("The game is already running in this copy.")
    password = secrets.token_hex(8)
    args, config = launch_config(copy, map_name, companions=companions, password=password, merged=merged,
                                 bonus_room=bonus_room, coop_flashlight=coop_flashlight)
    story =play.story_config() + "volume 0\nMP3Volume 0\n"
    if cheats:
        story = story.replace("sv_cheats 0", "sv_cheats 1").replace("sv_enttools_enable 0", "sv_enttools_enable 1")
    game = Game(copy, Rcon(password), copy / play.LOG, map_name)
    try:
        _begin(copy)
        exe = test_launcher(copy)
        game.guard = AudioGuard(copy, exe)   # before the game starts: no sound gets out
        game_dir = copy / "cryoffear"
        (game_dir / play.STORY_CFG).write_text(story, encoding="ascii")
        (game_dir / play.SESSION_CFG).write_text(config, encoding="ascii")
        setup_lib.keep_previous_log(copy, play.LOG)
        app.start_game(copy, [str(exe), "-nosound", *args[1:], *extra])
        if not game.wait_for(r"cofstory phase=|cof-story-transition\] phase=ready", ready_timeout):
            raise SystemExit("The game did not come up (see the session log).")
        time.sleep(3)
        yield game
    finally:
        _close(game)
        if game.guard:
            game.guard.stop()   # after the game has exited; its sessions stay muted (that program's own)
        recover(copy)


def _close(game: Game, timeout: float = 30.0) -> None:
    """Quit the game and wait until it has exited."""
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
    deadline = time.time() + 15
    while time.time() < deadline and game_processes(game.copy):
        time.sleep(1)


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
    finished: bool = True   # the scenario's last marker is in the log (else the game died or hung)
    sections: int = 0       # section moves of merged maps
    section_companions: int = 0

    @property
    def verdict(self) -> str:
        if not self.finished:
            return "DIED"
        return "ERRORS" if self.errors else "ok"

    def text(self) -> str:
        out = [f"{self.verdict}: {self.minutes:.1f} min, {self.lines} lines; maps: {' -> '.join(self.maps) or '-'}"]
        if self.sections:
            out.append(f"section moves: {self.sections}, companions moved with them: {self.section_companions}")
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
    return Report(minutes, len(lines), errors, modes, worst, teleports, maps, notes,
                  finished="live-loop done" in log or "live-loop" not in log,
                  sections=log.count("phase=section-move "), section_companions=log.count("phase=section-companion"))


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


@scenario("start", "c_start", companions=2)
def start(game: Game) -> None:
    """A story start (--map): the companions join, the opening is skipped, the player walks a little."""
    joined = game.wait_for(r"companion-added userid=\d+ name=Companion 2", 60)
    if not joined:
        game.mark("NOTE the companions were not added within 60 s")
    game.cmd("god"); game.cmd("notarget"); game.cmd("cof_companion_debug 1")
    skip_cutscenes(game, 16)
    game.sleep(8)
    game.hold("forward", 2.0)
    game.sleep(6)
    game.hold("back", 2.0)
    game.sleep(8)


@scenario("checkpoint", "c_start2", companions=2)
def checkpoint(game: Game) -> None:
    """Save, walk elsewhere, load (one session, two bots): everyone back where they saved, the bots added again."""
    game.wait_for(r"companion-added userid=\d+ name=Companion 2", 60)
    game.cmd("god"); game.cmd("notarget"); game.cmd("cof_companion_debug 1")
    game.at(*C_START2_SPOTS[2])
    game.sleep(8)
    at = game.mark("save")
    game.cmd("cof_save manual")
    if not game.wait_for(r"session-saved slot=manual", 30, at):
        game.mark("NOTE no manual save within 30 s")
    game.at(*C_START2_SPOTS[6])
    game.sleep(6)
    at = game.mark("load")
    game.cmd("cof_load manual")
    if not game.wait_for(r"companion-added userid=\d+ name=Companion 2", 90, at):
        game.mark("NOTE the companions were not back within 90 s of the load")
    game.cmd("god"); game.cmd("notarget")
    game.sleep(8)
    game.hold("forward", 1.5)
    game.sleep(6)


@scenario("death", "c_start2", companions=2)
def death(game: Game) -> None:
    """The player dies with two bots up: a bot comes to revive them (CPR) or they respawn beside it; twice."""
    game.wait_for(r"companion-added userid=\d+ name=Companion 2", 60)
    game.cmd("notarget"); game.cmd("cof_companion_debug 1")
    game.at(*C_START2_SPOTS[2])
    game.sleep(8)
    for k in range(2):
        at = game.mark(f"kill {k}")
        game.cmd("kill")
        if not game.wait_for(r"companion-cpr|respawn|inventory-restored", 30, at):
            game.mark(f"NOTE nothing brought the player back within 30 s ({k})")
        game.sleep(12)
        game.hold("forward", 1.0)
        game.sleep(5)


@scenario("flashlight", "c_start2", companions=1)
def flashlight(game: Game) -> None:
    """The co-op flashlight off (the default): F is refused for the player and the bot lights no torch."""
    game.wait_for(r"companion-added userid=\d+ name=Companion", 60)
    game.cmd("god"); game.cmd("notarget")
    game.sleep(4)
    at = game.mark("press F")
    game.cmd("cmd flashlight")
    if not game.wait_for(r"phase=flashlight-refused", 10, at):
        game.mark("NOTE the flashlight was not refused")
    game.sleep(15)
    if re.search(r'phase=cmd userid=\d+ cmd=flashlight', game.log()[at:]):   # one the game got (not refused)
        game.mark("NOTE a flashlight command went through")


@scenario("flashlight-on", "c_start2", companions=1, coop_flashlight=True)
def flashlight_on(game: Game) -> None:
    """The co-op flashlight allowed: F switches the torch on (nothing refused) and the bot lights its own."""
    game.wait_for(r"companion-added userid=\d+ name=Companion", 60)
    game.cmd("god"); game.cmd("notarget")
    game.sleep(4)
    at = game.mark("press F")
    game.cmd("cmd flashlight")
    game.sleep(15)
    if "flashlight-refused" in game.log()[at:]:
        game.mark("NOTE the flashlight was refused although allowed")


@scenario("merged", "cm_apartment1", companions=2)
def merged(game: Game) -> None:
    """A merged map (--map cm_apartment1 or cm_apartment3): the player goes through every door between its sections
    with two bots; each door must move them and the bots come along."""
    from tools.merged_maps_check import load_group, section_exits
    named, sections = load_group(game.copy, game.map_name)
    exits = section_exits(named, sections)
    game.wait_for(r"companion-added userid=\d+ name=Companion 2", 90)
    game.cmd("god"); game.cmd("notarget"); game.cmd("cof_companion_debug 1")
    skip_cutscenes(game, 12)
    for i, e in enumerate(exits):
        # first next to the door, so the bots gather; then into it
        at = game.mark(f"door {i} {e.src} {e.dst} {e.landmark}")
        game.at(e.stand[0] - 48, e.stand[1], e.stand[2])
        game.sleep(5)
        game.at(*e.stand)
        if not game.wait_for(r"phase=section-move ", 10, at):
            game.mark(f"NOTE door {i} {e.src} -> {e.dst}: no section move")
        game.sleep(6)


def run(copy: Path, name: str, out: Path, companions: int | None = None, keep_open: bool = False,
        map_name: str | None = None) -> Path:
    fn, default_map, default_companions, options = SCENARIOS[name]
    map_name = map_name or default_map
    out.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    print(f"{stamp} {name}: {map_name}, {companions or default_companions} companions")
    with session(copy, map_name, companions=companions or default_companions, **options) as game:
        fn(game)
        game.mark("done")
        if keep_open:
            input("The game stays open; Enter closes it. ")
        log = game.log()
    if map_name != default_map:
        name = f"{name}-{map_name}"
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


def sweep(copy: Path, out: Path, maps: list[str], companions: int = 2) -> int:
    """The start scenario on each map, one game each; a table of verdicts."""
    rows = []
    for map_name in maps:
        try:
            log = run(copy, "start", out, companions, map_name=map_name).read_text(encoding="latin-1")
            report = read_report(log)
            rows.append(f"{map_name:16} {report.verdict:7} {report.minutes:4.1f} min  modes {sum(report.modes.values()):4}  "
                        f"teleports {sum(report.teleports.values()):3}  errors {len(report.errors)}")
        except (SystemExit, Exception) as error:   # one map's trouble must not end the sweep
            rows.append(f"{map_name:16} FAILED  {error}")
    print("\n".join(rows))
    (out / f"{datetime.now():%Y%m%d-%H%M%S}-sweep.txt").write_text("\n".join(rows) + "\n", encoding="utf-8")
    return 0


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
    r.add_argument("--map", help="another map than the scenario's own")
    p = sub.add_parser("report")
    p.add_argument("log", type=Path)
    i = sub.add_parser("install")
    i.add_argument("copy", type=Path)
    i.add_argument("dll", type=Path)
    sub.add_parser("restore").add_argument("copy", type=Path)
    sub.add_parser("recover").add_argument("copy", type=Path)
    w = sub.add_parser("sweep", help="the start scenario on every chapter's first map (or --maps)")
    w.add_argument("copy", type=Path)
    w.add_argument("--maps", nargs="*")
    w.add_argument("--out", type=Path, default=EVIDENCE)
    a = parser.parse_args(argv)
    if a.command == "scenarios":
        for name, (fn, map_name, companions, _) in sorted(SCENARIOS.items()):
            print(f"{name:20} {map_name:14} {companions} AI  {(fn.__doc__ or '').strip().splitlines()[0]}")
    elif a.command == "run":
        run(a.copy, a.scenario, a.out, a.companions, a.keep_open, a.map)
    elif a.command == "report":
        print(read_report(a.log.read_text(encoding="latin-1", errors="replace")).text())
    elif a.command == "install":
        install(a.copy, a.dll)
    elif a.command == "restore":
        restore(a.copy)
    elif a.command == "recover":
        print("recovered" if recover(Path(a.copy).resolve()) else "nothing to recover")
    elif a.command == "sweep":
        from cofcoop.core import CHAPTERS
        sweep(a.copy, a.out, a.maps or [name for _, name in CHAPTERS])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
