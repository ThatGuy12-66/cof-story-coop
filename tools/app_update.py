"""The app's own updates: is there a newer version, and getting it.

Every release on the public releases page (RELEASES) carries the package zip and a small
release.json (release_info). When the main window opens, the app reads the latest release's
release.json; if its build is newer than this app's, it offers the update. "Update now" downloads
the zip into the temp folder, checks its size and SHA-256, unpacks it, starts the new app with
--update-from (it waits for this app to close, then updates the copy) and closes this app.

No GitHub API: the /releases/latest/download/ links redirect straight to the files, with no rate limit.
Nothing here touches the copy itself; friend_setup.update does that in the new app.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile

if not getattr(sys, "frozen", False):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools import friend_setup as setup_lib

RELEASES = "charliee1w/cof-story-coop-releases"
RELEASES_PAGE = f"https://github.com/{RELEASES}/releases"
INFO = "release.json"
LATEST_INFO_URL = f"{RELEASES_PAGE}/latest/download/{INFO}"
SCHEMA = 1
USER_AGENT = "CoF-Story-Coop-app"
DOWNLOADS = Path(os.environ.get("TEMP") or tempfile.gettempdir()) / "CoF-Story-Coop-update"
ZIP_PATTERN = re.compile(r"CoF-Story-Coop-[0-9][0-9A-Za-z.]*\.zip")
LABEL_PATTERN = re.compile(r"[0-9]+(\.[0-9]+)*")
CHUNK = 1 << 20
MAX_INFO_BYTES = 64 * 1024
MAX_ZIP_BYTES = 2 << 30   # GitHub's own limit for one release file

CREATE_NEW_PROCESS_GROUP = 0x00000200
CREATE_BREAKAWAY_FROM_JOB = 0x01000000   # the new app must outlive this one, whatever started it


class Cancelled(Exception):
    """The player cancelled the download."""


@dataclass(frozen=True)
class Release:
    label: str     # what players see: "1.1.2"
    version: str   # the join check (coop_play.VERSION): who can play with whom
    build: int     # coop_play.BUILD: a higher build is an update
    zip: str
    size: int
    sha256: str

    @property
    def zip_url(self) -> str:
        return f"{RELEASES_PAGE}/download/v{self.label}/{self.zip}"

    @property
    def page_url(self) -> str:
        return f"{RELEASES_PAGE}/tag/v{self.label}"


def release_info(label: str, version: str, build: int, zip_path: Path) -> dict:
    """The release.json published next to a package zip (tools/release.py writes it)."""
    zip_path = Path(zip_path)
    digest = hashlib.sha256()
    with zip_path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(CHUNK), b""):
            digest.update(chunk)
    info = {"schema": SCHEMA, "label": label, "version": version, "build": build, "zip": zip_path.name,
            "size": zip_path.stat().st_size, "sha256": digest.hexdigest()}
    parse_release(info)
    return info


def parse_release(data) -> Release:
    """A release.json, checked: anything odd in it means no update is offered."""
    bad = ValueError("The update information is not valid.")
    if not isinstance(data, dict) or data.get("schema") != SCHEMA:
        raise bad
    label, version, build, name, size, sha = (data.get(k) for k in ("label", "version", "build", "zip", "size", "sha256"))
    if not (isinstance(label, str) and LABEL_PATTERN.fullmatch(label) and isinstance(version, str) and version
            and type(build) is int and build > 0 and isinstance(name, str) and ZIP_PATTERN.fullmatch(name)
            and type(size) is int and 0 < size <= MAX_ZIP_BYTES and isinstance(sha, str)
            and re.fullmatch(r"[0-9a-f]{64}", sha)):
        raise bad
    return Release(label, version, build, name, size, sha)


def open_url(url: str, timeout: float):
    return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": USER_AGENT}), timeout=timeout)


def read_info(url: str = LATEST_INFO_URL, timeout: float = 8, opener=open_url) -> dict:
    with opener(url, timeout) as response:
        raw = response.read(MAX_INFO_BYTES + 1)
    if len(raw) > MAX_INFO_BYTES:
        raise ValueError("The update information is not valid.")
    return json.loads(raw.decode("utf-8"))


def newer_release(build: int, read=read_info) -> Release | None:
    """The latest release when it is newer than build, else None. Raises when the page can't be read."""
    release = parse_release(read())
    return release if release.build > build else None


def download(release: Release, folder: Path, progress=lambda done, total: None, cancelled=lambda: False,
             opener=open_url) -> Path:
    """The release's zip in folder, its size and SHA-256 checked. A partial or damaged download is removed."""
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    target, part = folder / release.zip, folder / (release.zip + ".part")
    digest, done = hashlib.sha256(), 0
    try:
        with opener(release.zip_url, 30) as response, part.open("wb") as out:
            while True:
                if cancelled():
                    raise Cancelled()
                chunk = response.read(CHUNK)
                if not chunk:
                    break
                done += len(chunk)
                if done > release.size:
                    raise ValueError("The download is bigger than it should be. Try again later.")
                digest.update(chunk)
                out.write(chunk)
                progress(done, release.size)
        if done != release.size or digest.hexdigest() != release.sha256:
            raise ValueError("The download is damaged. Try again.")
        os.replace(part, target)
    except BaseException:
        part.unlink(missing_ok=True)
        raise
    return target


def extract_app(zip_path: Path, folder: Path) -> Path:
    """Unpacks the package next to its zip; returns the new app exe."""
    target = Path(folder) / "app"
    shutil.rmtree(target, ignore_errors=True)
    try:
        with zipfile.ZipFile(zip_path) as archive:
            for info, path in setup_lib.safe_zip_members(archive):
                destination = target.joinpath(*path.parts)
                if info.is_dir():
                    destination.mkdir(parents=True, exist_ok=True)
                    continue
                destination.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(info) as source, destination.open("wb") as out:
                    shutil.copyfileobj(source, out, CHUNK)
    except zipfile.BadZipFile:
        raise ValueError("The download is damaged. Try again.") from None
    exe = target / setup_lib.APP_EXE
    if not exe.is_file():
        raise ValueError("The download has no app in it.")
    return exe


def pids_to_wait() -> list[int]:
    """This app's processes. A one-file exe runs as two: the unpacking parent and Python."""
    pids = [os.getpid()]
    if getattr(sys, "frozen", False):
        pids.append(os.getppid())
    return pids


def start_new_app(exe: Path, copy: Path, wait: list[int], popen=subprocess.Popen):
    """The new app updates the copy once this app has closed (it waits for these processes)."""
    command = [str(exe), "--update-from", str(copy), "--wait-pid", ",".join(str(p) for p in wait)]
    cwd = str(Path(exe).parent)
    if os.name != "nt":
        return popen(command, cwd=cwd, close_fds=True, start_new_session=True)
    try:
        return popen(command, cwd=cwd, close_fds=True,
                     creationflags=CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB)
    except OSError:   # this app runs in a job that doesn't allow breaking away: the new app joins it, like the game does
        return popen(command, cwd=cwd, close_fds=True, creationflags=CREATE_NEW_PROCESS_GROUP)


def parse_pids(text: str | None) -> list[int]:
    return [int(p) for p in (text or "").split(",") if p.strip().isdigit()]


def running(pid: int) -> bool:
    if os.name == "nt":
        import ctypes
        kernel = ctypes.windll.kernel32
        handle = kernel.OpenProcess(0x00100000 | 0x1000, False, pid)   # SYNCHRONIZE | QUERY_LIMITED_INFORMATION
        if not handle:
            return False
        try:
            return kernel.WaitForSingleObject(handle, 0) == 0x102   # WAIT_TIMEOUT: still running
        finally:
            kernel.CloseHandle(handle)
    try:
        os.kill(pid, 0)
    except (ProcessLookupError, PermissionError):
        return False
    except OSError:
        return False
    return True


def wait_for_exit(pids: list[int], timeout: float = 60, is_running=running, sleep=time.sleep, clock=time.monotonic) -> bool:
    """True once every process has closed; False when the time runs out (the update then tries anyway)."""
    end = clock() + timeout
    while any(is_running(p) for p in pids if p and p != os.getpid()):
        if clock() >= end:
            return False
        sleep(0.25)
    return True


def clean_downloads(exe: Path, folder: Path = DOWNLOADS) -> None:
    """Old downloads go once the app runs from somewhere else (normally the copy, after an update)."""
    try:
        Path(exe).resolve().relative_to(Path(folder).resolve())
        return   # this app is the downloaded one
    except ValueError:
        pass
    shutil.rmtree(folder, ignore_errors=True)
