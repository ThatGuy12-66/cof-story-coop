"""Story co-op sessions for the app: what each start option runs.

Builds command lines and config text, reads the latest co-op checkpoint, checks
a host before joining and watches a hosted game. Starts no process itself.
"""
from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import struct
import time

from cofcoop import network
from cofcoop.core import CHAPTERS

VERSION = "1.0"
VERSION_LABEL = "1.0"
BUILD = 4   # counts on from the test packages (Test 2 was build 3), so their copies are offered the update
LOG = "story-session.log"
STORY_CFG = "cof_story.cfg"
SESSION_CFG = "cof_story_session.cfg"
SETTINGS = "cof-story-coop-settings.json"
DEFAULT_PORT = 27015
NEW_GAME_MAP = "c_intro"
CHAPTER_CHOICES = [(label, name) for label, name in CHAPTERS if name != NEW_GAME_MAP]
REPORT_URL = "https://discord.gg/4uwV6jmmY7"   # "Report a bug": the public Discord
# Every game the app starts: story co-op on, the test tools off. The story trace
# goes to the log for bug reports; the engine never draws it on screen.
STORY_SETTINGS = (
    "cof_story_coop 1", "cof_story_trace 1", "sv_cheats 0", "sv_enttools_enable 0", "developer 0",
    "coop 0", "deathmatch 1", "mp_timelimit 0", "mp_fraglimit 0", "cof_test_gun 0", "fps_max 60",
    "rate 50000", "cl_cmdrate 60", "cl_updaterate 60", "cl_forwardspeed 300", "cl_backspeed 200",
    "cl_sidespeed 200", "+mlook",
)
DEFAULT_SETTINGS = {"name": "", "port": DEFAULT_PORT, "players": 2, "companion": False, "password": "",
                    "bonus_room": False, "first_launch_done": False, "companions": 1, "merged_maps": True,
                    "coop_flashlight": False}
MAX_COMPANIONS = 3   # the game takes four players at most
_MAP = re.compile(r"[a-z0-9_]{1,32}\Z")
_SLOT = re.compile(r"[a-z0-9_-]{1,24}\Z")
_ARCHIVE = struct.Struct("<5I")      # cof_archive_header_t: magic, version, count, size, checksum
_ENTRY = struct.Struct("<64sI")      # cof_archive_entry_t: name, size
_SESSION = struct.Struct("<4I64s")   # cof_session_data_t: version, clients, entities, maps, map
_NAMES_AT = 16 + 64 + 256 * 64      # cof_session_data_t.players (engine sv_cof_session_data.h)


def story_config() -> str:
    return "\n".join(STORY_SETTINGS) + "\n"


def _clean(value, limit: int, label: str, allow_empty: bool) -> str:
    if not isinstance(value, str) or len(value) > limit or any(ord(c) < 32 or ord(c) > 126 or c in '\\";' for c in value):
        raise ValueError(f"{label}: use up to {limit} plain letters, digits and punctuation (no \", \\ or ;).")
    if not value and not allow_empty:
        raise ValueError(f"Enter {label.lower()}.")
    return value


def clean_name(value) -> str:
    return _clean(value.strip() if isinstance(value, str) else value, 24, "Your name", False)


def clean_password(value) -> str:
    return _clean(value, 64, "The password", True)


def _number(value, low: int, high: int, message: str) -> int:
    try:
        number = int(str(value).strip())
    except ValueError:
        raise ValueError(message) from None
    if not low <= number <= high:
        raise ValueError(message)
    return number


def clean_port(value) -> int:
    return _number(value, 1024, 65535, "The port must be a number from 1024 to 65535.")


def clean_players(value) -> int:
    return _number(value, 2, 4, "Players: 2 to 4.")


def video_flags(first_launch: bool, desktop) -> list[str]:
    """The first game fills the desktop; later ones keep the game's own video options."""
    if not first_launch or not desktop or desktop[0] < 640 or desktop[1] < 480:
        return []
    return ["-borderless", "-width", str(desktop[0]), "-height", str(desktop[1])]


def party_size(*, solo: bool, players: int, companions: int, fill: bool, checkpoint_players: int | None) -> tuple[int, int]:
    """(places, AI companions) for a start. Solo: you and 1-3 AIs. Host: the chosen places,
    and with fill an AI in every place a friend has not taken. Continue keeps the
    checkpoint's places, which the engine requires."""
    if solo:
        ais = max(1, min(int(companions), MAX_COMPANIONS))
        places = checkpoint_players or ais + 1
        return places, max(1, min(ais, places - 1))
    places = checkpoint_players or players
    return places, places - 1 if fill else 0


def host_launch(runtime: Path, *, solo: bool, name: str, map_name: str, players: int, password: str,
                companions: int, port: int, cont: bool, video: list[str],
                bonus_room: bool = False, merged: bool = True, coop_flashlight: bool = False) -> tuple[list[str], str]:
    """Play solo (LAN only, with AI companions) or host: the command line and the session cfg.

    companions: how many AI companion bots to keep (each gives its place to a friend who joins).
    bonus_room opens the bonus room of c_start and c_bridge (the FAMAS, the axe and the
    rest) for everyone in this game; shut otherwise, whatever the host's own game says.
    merged: door-joined story maps load as one map (fewer loading screens); a continued
    session keeps the mode it was saved with (Checkpoint.merged).
    coop_flashlight: F gives everyone a free torch (Cry of Fear's co-op mode); off, the light is Simon's phone and
    the story's own lights, as in the single-player game."""
    name = clean_name(name)
    password = "" if solo else clean_password(password)
    map_name = map_name.lower()
    if not _MAP.match(map_name):
        raise ValueError("Unknown map.")
    if not 2 <= players <= 32:
        raise ValueError("Players: 2 to 4.")
    if not (1 if solo else 0) <= companions <= players - 1:
        raise ValueError("AI companions: one for each free place at most.")
    lines = [f'name "{name}"', f"maxplayers {players}", f"sv_lan {1 if solo else 0}",
             f'sv_password "{password}"', f'password "{password}"', f'hostname "{name}\'s game"',
             f"cof_companion {companions}", "cof_companion_drive 0",
             f"cof_build {VERSION}", f"cof_continue {1 if cont else 0}", f"cof_bonus_room {1 if bonus_room else 0}",
             f"cof_merged_maps {1 if merged else 0}", f"cof_coop_flashlight {1 if coop_flashlight else 0}"]
    args = [str(runtime / "CoFLaunchApp.exe"), *video, "-port", str(port), "-clientport", str(port + 1),
            "-log", LOG, "-logtime", "+exec", STORY_CFG, "+exec", SESSION_CFG, "+map", map_name]
    return args, "\n".join(lines) + "\n"


def join_launch(runtime: Path, *, name: str, password: str, endpoint: str, video: list[str]) -> tuple[list[str], str]:
    """Join a host by join code or address: the command line and the session cfg."""
    target = network.parse_endpoint(endpoint)
    config = f'name "{clean_name(name)}"\npassword "{clean_password(password)}"\n'
    args = [str(runtime / "CoFLaunchApp.exe"), *video, "-log", LOG, "-logtime",
            "+exec", STORY_CFG, "+exec", SESSION_CFG, "+connect", str(target)]
    return args, config


def check_host(info: dict, *, password: str) -> None:
    """Refuse a join that cannot work, before the game starts (info: network.query_info)."""
    theirs = info.get("cofbuild", "")
    if theirs != VERSION:
        raise ValueError(f"The host runs a different version of story co-op (theirs: {theirs or 'unknown'}, "
                         f"yours: {VERSION_LABEL}). Everyone needs the same package.")
    try:
        players, limit = int(info.get("numcl", "0")), int(info.get("maxcl", "0"))
    except ValueError:
        players = limit = 0
    if limit and players >= limit:
        raise ValueError("The host's game is full.")
    if info.get("password") == "1" and not password:
        raise ValueError("The host's game has a password. Enter it and join again.")


@dataclass(frozen=True)
class Checkpoint:
    slot: str
    map: str
    players: int
    names: tuple[str, ...]
    merged: bool = False   # saved with merged story maps (merged.txt); saves from before them: False
    section: str = ""      # the original map the first player was in, in a merged map

    @property
    def label(self) -> str:
        return self.section or self.map


def merged_for_start(checkpoint: Checkpoint | None, *, checkbox: bool) -> bool:
    """Whether a start uses merged story maps: Continue keeps the session's own mode, a new game or chapter
    takes the host's checkbox."""
    return checkpoint.merged if checkpoint else checkbox


def parse_merged_record(data: bytes) -> tuple[bool, str]:
    """merged.txt of a checkpoint (engine cof_sections.h CoF_SectionsRecord): (merged, original map), or
    (False, "") when it is missing or malformed."""
    try:
        lines = [line.split() for line in data.decode("ascii").splitlines() if line.strip()]
    except UnicodeDecodeError:
        return False, ""
    values = {line[0]: line[1] for line in lines if len(line) == 2}
    if len(values) != len(lines) or set(values) != {"merged", "section"} or values["merged"] not in ("0", "1") \
            or not _MAP.match(values["section"]):
        return False, ""
    return values["merged"] == "1", values["section"]


def parse_checkpoint(slot: str, data: bytes) -> Checkpoint | None:
    """The map, player limit and names of a co-op checkpoint archive (format 2), with its merged-maps mode, or
    None."""
    if len(data) < _ARCHIVE.size:
        return None
    magic, version, count, size, _ = _ARCHIVE.unpack_from(data)
    if magic != 0x53434643 or version != 1 or size != len(data) or not 0 < count <= 1024:
        return None
    position = _ARCHIVE.size
    found, merged, section = None, False, ""
    for _ in range(count):
        if position + _ENTRY.size > len(data):
            return None
        raw, length = _ENTRY.unpack_from(data, position)
        position += _ENTRY.size
        if position + length > len(data):
            return None
        entry = raw.split(b"\0", 1)[0].lower()
        if entry == b"session.dat" and found is None:
            body = data[position:position + length]
            if len(body) < _NAMES_AT + 32 * 32:
                return None
            session_version, players, _, _, raw_map = _SESSION.unpack_from(body)
            map_name = raw_map.split(b"\0", 1)[0].decode("latin-1").lower()
            if session_version != 2 or not 2 <= players <= 32 or not _MAP.match(map_name):
                return None
            names = tuple(name for name in (body[_NAMES_AT + i * 32:_NAMES_AT + (i + 1) * 32].split(b"\0", 1)[0]
                                            .decode("latin-1") for i in range(32)) if name)
            found = (map_name, players, names)
        elif entry == b"merged.txt":
            merged, section = parse_merged_record(data[position:position + length])
        position += length
    return Checkpoint(slot, *found, merged=merged, section=section) if found else None


def latest_checkpoint(runtime: Path) -> Checkpoint | None:
    """The checkpoint the engine's cof_load would take: latest.txt's slot, in either save folder."""
    folders = [folder for folder in (Path(runtime) / "SAVE/cofcoop", Path(runtime) / "cryoffear/save/cofcoop")
               if (folder / "latest.txt").is_file()]
    if not folders:
        return None
    folder = max(folders, key=lambda f: (f / "latest.txt").stat().st_mtime)
    try:
        slot = (folder / "latest.txt").read_text("latin-1").strip()
        if not _SLOT.match(slot):
            return None
        return parse_checkpoint(slot, (folder / f"{slot}.cof").read_bytes())
    except OSError:
        return None


def continue_warning(checkpoint: Checkpoint, name: str) -> str | None:
    if not checkpoint.names or name.strip().casefold() in {n.casefold() for n in checkpoint.names}:
        return None
    return (f"This checkpoint was saved by {', '.join(checkpoint.names)}. Items come back only under the "
            f"same name: change Your name to one of those, or continue without your items.")


def load_settings(root: Path) -> dict:
    try:
        stored = json.loads((Path(root) / SETTINGS).read_text("utf-8"))
    except (OSError, ValueError):
        stored = {}
    if not isinstance(stored, dict):
        stored = {}
    return {key: stored[key] if type(stored.get(key)) is type(value) else value
            for key, value in DEFAULT_SETTINGS.items()}


def save_settings(root: Path, settings: dict) -> None:
    path = Path(root) / SETTINGS
    temp = path.with_suffix(".tmp")
    temp.write_text(json.dumps(settings, indent=2) + "\n", "utf-8")
    os.replace(temp, path)


def close_warning(watch) -> str | None:
    """The question to ask before the app closes while it still holds a hosted game's router port."""
    if watch is None or watch.lan_only or watch.closed:
        return None
    return ("Your game may still be running. Closing this window closes the router port, and friends "
            "who joined over the Internet can drop. Close anyway?")


class HostWatch:
    """While a hosted (or solo) game runs: the router mapping, the join code and the player count.

    The window calls open_router() once and poll() every few seconds, both off the UI thread."""

    def __init__(self, port: int, *, lan_only: bool, net=network, clock=time.monotonic, wait: float = 90):
        self.port, self.lan_only, self.net, self.clock, self.wait = port, lan_only, net, clock, wait
        self.started = clock()
        self.up = self.closed = False
        self.mapping = None
        self.code = self.address = self.router = self.last = ""

    def open_router(self) -> None:
        if self.lan_only:
            return
        public = ""
        try:
            self.mapping = self.net.upnp_map(self.port)
            public = self.mapping.get("external_ip", "")
            self.router = "Your router opened the port for this game (UPnP)."
        except self.net.NetworkError as error:
            self.router = (f"Your router did not open UDP {self.port} by itself ({error}) Forward UDP {self.port} "
                           "to this PC in your router, or all use a VPN such as Tailscale, ZeroTier or Radmin "
                           "and share this PC's VPN address instead.")
            try:
                public = self.net.discover_public_address()
            except self.net.NetworkError:
                public = ""
        if public:
            endpoint = self.net.Endpoint(public, self.port)
            self.address, self.code = str(endpoint), self.net.make_join_code(endpoint)

    def poll(self, running: bool) -> str:
        if self.closed or not running:
            self.close()
            return "The game has ended."
        try:
            info = self.net.query_info(self.net.Endpoint("127.0.0.1", self.port), timeout=1)
        except self.net.NetworkError:
            if self.up:   # a level change can make one query time out
                return self.last
            if self.clock() - self.started > self.wait:
                return (f"The game has not answered on UDP {self.port}. If it showed an error, another program "
                        "may be using that port: close it, or pick another port.")
            return "Starting the game..."
        self.up = True
        people, bots = info.get('numcl', '?'), info.get('bots', '0')
        if bots not in ('', '0'):
            self.last = (f"Your game is up: {people} {'player' if people == '1' else 'players'} and {bots} AI "
                         f"{'companion' if bots == '1' else 'companions'} ({info.get('maxcl', '?')} places).")
        else:
            self.last = f"Your game is up: {people}/{info.get('maxcl', '?')} players."
        return self.last

    def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        if self.mapping:
            try:
                self.net.upnp_unmap(self.mapping)
            except self.net.NetworkError:
                pass
            self.mapping = None
