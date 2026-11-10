"""Cry of Fear story co-op: the app. Setup on first run, updates, then Play: the game at its main menu, whose Co-op
list hosts, joins, continues or plays with AI; this window keeps the router port and the join code meanwhile."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time
import urllib.error
import webbrowser

if not getattr(sys, "frozen", False):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cofcoop import network
from tools import app_lang
from tools import app_theme as theme
from tools import app_update
from tools import coop_play as play
from tools import friend_setup as setup_lib
from tools import unlocks
from tools.app_lang import T

DEFAULT_COPY = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "CoF-Story-Coop"
COPY_POINTER = DEFAULT_COPY.with_name("CoF-Story-Coop.txt")   # where setup last made a copy


def payload_dir(explicit=None) -> Path:
    """The engine payload: packed into the exe, or a folder given for a run from source."""
    if explicit:
        return Path(explicit)
    if getattr(sys, "frozen", False):
        return Path(sys._MEIPASS) / "payload"
    raise ValueError(f"Run the packaged {setup_lib.APP_EXE}, or pass --payload <folder>.")


def this_exe() -> Path:
    return Path(sys.executable)


def dpi_aware() -> None:
    if os.name == "nt":
        import ctypes
        try:
            ctypes.windll.user32.SetProcessDPIAware()
        except (AttributeError, OSError):
            pass


def desktop_size():
    if os.name != "nt":
        return None
    import ctypes
    dpi_aware()
    return ctypes.windll.user32.GetSystemMetrics(0), ctypes.windll.user32.GetSystemMetrics(1)


def start_game(root: Path, args: list[str]) -> subprocess.Popen:
    """CoFLaunchApp.exe was a console program until 1.3.2 (a windowed one since): without CREATE_NO_WINDOW, Windows
    opened a console window beside the game (and closing that window would close the game)."""
    return subprocess.Popen(args, cwd=root, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, creationflags=0x08000000)   # CREATE_NO_WINDOW


def carry_unlocks(root: Path) -> list[str]:
    """1.3.8: the unlocks of the player's own Cry of Fear (the game the copy was made from) into the copy before the
    game starts (tools/unlocks.py); never in the way of starting it."""
    try:
        state = setup_lib.installed_state(root) or {}
        return unlocks.carry_into_copy(root, state.get("source_game"))
    except (OSError, ValueError):
        return []


def footer_text() -> str:
    return f"Cry of Fear Co-op {play.VERSION_LABEL}"


def report_bug() -> None:
    webbrowser.open(play.REPORT_URL)


def remember_copy(copy: Path, pointer: Path = COPY_POINTER) -> None:
    """A newer package finds a copy set up outside the default folder through this note."""
    try:
        pointer.write_text(str(Path(copy).resolve()), encoding="utf-8")
    except OSError:
        pass


def remembered_copy(pointer: Path) -> Path | None:
    try:
        return Path(pointer.read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        return None


def stamped(text: str) -> str:
    """A line for the progress box, timed like setup's own lines."""
    return f"{time.strftime('%H:%M:%S')}  {text}"


def update_error(error) -> str:
    if isinstance(error, (urllib.error.URLError, TimeoutError, ConnectionError)):
        return T("Couldn't download the update. Check your internet connection and try again.")
    return T("The update stopped: {error}", error=T(str(error)))


def elapsed_text(seconds: float) -> str:
    minutes, second = divmod(int(seconds), 60)
    hours, minute = divmod(minutes, 60)
    return f"{hours}:{minute:02}:{second:02}" if hours else f"{minute}:{second:02}"


class ProgressPanel:
    """Setup's and update's live status: the bar, the step it is on, the time taken and a read-only box with a
    line for each thing done (the lines of the copy's setup log)."""

    def __init__(self, tk, ttk, parent, clock=time.monotonic):
        self.clock, self.started, self.job = clock, None, None
        self.bar = ttk.Progressbar(parent, maximum=100)
        self.bar.pack(fill="x")
        row = ttk.Frame(parent)
        row.pack(fill="x", pady=(8, 6))
        self.status, self.elapsed = tk.StringVar(), tk.StringVar()
        ttk.Label(row, textvariable=self.elapsed, style="Faint.TLabel").pack(side="right")
        ttk.Label(row, textvariable=self.status, wraplength=580).pack(side="left", anchor="w")
        frame = ttk.Frame(parent)
        frame.pack(fill="both", expand=True)
        self.box = tk.Text(frame, height=9, wrap="word", font=theme.FONTS.get("mono", ("Consolas", 9)),
                           state="disabled", background=theme.LOG_BG, foreground=theme.LOG_TEXT, relief="flat",
                           borderwidth=0, padx=10, pady=8, highlightthickness=1, highlightbackground=theme.LINE,
                           highlightcolor=theme.LINE, insertbackground=theme.TEXT)
        scroll = ttk.Scrollbar(frame, command=self.box.yview)
        self.box.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.box.pack(side="left", fill="both", expand=True)

    def start(self):
        self.started = self.clock()
        self.elapsed.set(T("{time} so far", time=elapsed_text(0)))
        self.job = self.box.after(1000, self.tick)

    def tick(self):
        if self.job:
            self.box.after_cancel(self.job)
        self.elapsed.set(T("{time} so far", time=elapsed_text(self.clock() - self.started)))
        self.job = self.box.after(1000, self.tick)

    def stop(self):
        if self.job:
            self.box.after_cancel(self.job)
        self.job = None

    def show(self, status, part, line):
        self.status.set(T(status))   # setup's and update's steps come in English (friend_setup.Steps)
        self.bar.configure(value=part * 100)
        if line:
            self.box.configure(state="normal")
            if self.box.index("end-1c") != "1.0":
                self.box.insert("end", "\n")
            self.box.insert("end", line)
            self.box.configure(state="disabled")
            self.box.see("end")


# the co-op keys the main screen shows (friend_setup.COOP_KEYS binds them)
COOP_KEYS = (("J", "join your partner"), ("Enter", "skip a cutscene for everyone"),
             ("H", "AI companions wait or follow"), ("O", "the wardrobe (with the bonus room)"),
             ("Y", "text chat"), ("K", "voice chat (hold to talk)"),   # 1.3.8: bound since 1.0, never shown
             # player tools (2026-11-09), where the player's own config left the key free
             ("F3", "unstuck"), ("G", "ping: where you are"), ("V", "scare a teammate"),
             ("Q", "drop spare ammo"), ("F6", "hide or show the HUD"), ("T", "spray"))
COMFORT_CHECKS = (("head_bob", "Head bob"), ("faint_light", "Faint light"), ("noir", "Black and white"),
                  ("hide_hud", "Hide the HUD"))

# 1.1.8 (L14.2): closing the window ends a setup or update part way (its worker thread dies with the app)
BUSY_WARNINGS = {
    "setup": "Setup is still running. Closing now stops it part way; click Set up again later to delete the "
             "unfinished copy and start over. Close anyway?",
    "update": "The update is still running. Closing now stops it part way, and the copy may not start until it is "
              "updated or repaired. Close anyway?",
}


def where_to_start(exe_dir: Path, default_copy: Path, build: int, pointer: Path | None = COPY_POINTER):
    """'main' (a copy to play from), 'update' (an older copy), 'newer' (a copy newer than this app) or 'setup'."""
    if setup_lib.installed_state(exe_dir):
        return "main", Path(exe_dir)
    copy, state = Path(default_copy), setup_lib.installed_state(default_copy)
    if not state and pointer is not None:
        copy = remembered_copy(pointer)
        state = setup_lib.installed_state(copy) if copy else None
    if not state:
        return "setup", None
    if state["build"] < build:
        return "update", copy
    if state["build"] > build:
        return "newer", copy
    return "main", copy


class App:
    def __init__(self, window, payload, exe):
        import tkinter as tk
        from tkinter import ttk
        self.tk, self.ttk = tk, ttk
        self.window, self.payload, self.exe = window, payload, exe
        self.root = None
        self.settings = {}
        self.watch = None
        self.panel = None
        self.offer = None          # a newer release (app_update.Release), once the check finds one
        self.update_slot = None    # where the main screen shows that offer
        self.checked = False
        self.busy = None           # "setup" or "update" while one runs (BUSY_WARNINGS)
        self.launching = False     # Play is checking the copy (launch)
        self.redraw = None         # draws the current screen again in another language (None: not this screen)
        self.messages = queue.Queue()
        window.title("Cry of Fear Co-op")
        window.geometry(f"840x{min(760, window.winfo_screenheight() - 80)}")
        window.minsize(720, 600)
        theme.apply(window)
        theme.Header(tk, window)
        foot = ttk.Frame(window, padding=(26, 0, 20, 10))
        foot.pack(fill="x", side="bottom")
        ttk.Frame(window, height=1, style="Line.TFrame").pack(fill="x", side="bottom")
        ttk.Label(foot, text=footer_text(), style="Faint.TLabel").pack(side="left")
        # 1.5: the window's language, the same setting as the game's Options > Game > Language (app_lang)
        self.language = tk.StringVar(value=app_lang.NAMES[app_lang.current()])
        picker = ttk.Combobox(foot, textvariable=self.language, state="readonly", width=12,
                              values=[name for _code, name, *_ in app_lang.LANGUAGES])
        picker.pack(side="left", padx=(16, 0))
        picker.bind("<<ComboboxSelected>>", lambda _: self.guard(self.pick_language)())
        self.report_button = ttk.Button(foot, text=T("Report a bug"), command=report_bug, style="Ghost.TButton")
        self.report_button.pack(side="right")
        self.logs_button = ttk.Button(foot, text=T("Open logs folder"), command=self.guard(self.open_logs),
                                      style="Ghost.TButton")
        self.logs_button.pack(side="right", padx=4)
        self.body = ttk.Frame(window, padding=(26, 18, 26, 14))
        self.body.pack(fill="both", expand=True)
        window.protocol("WM_DELETE_WINDOW", self.close)
        window.after(100, self.pump)

    # the language (1.5)
    def pick_language(self):
        """The list in the footer: the window in that language now, the game in it from its next start (the copy's
        config.cfg; the game writes its own choice back there when it quits, and the main screen takes it)."""
        code = next((c for c, name, *_ in app_lang.LANGUAGES if name == self.language.get()), "english")
        if code == app_lang.current():
            return
        app_lang.set_current(code)
        app_lang.save_choice(code)
        self.report_button.configure(text=T("Report a bug"))
        self.logs_button.configure(text=T("Open logs folder"))
        root = self.root
        if root is not None:
            def job():
                if not setup_lib.game_is_running(root, unknown=True):
                    app_lang.write_game_language(root, code)
            self.work(job, lambda _: None, lambda _: None)
        if self.redraw is not None:
            self.redraw()

    def adopt_game_language(self):
        """The game's own Language option, when the player last changed it there: its config.cfg names it."""
        code = app_lang.read_game_language(self.root) if self.root else None
        if code and code != app_lang.current():
            app_lang.set_current(code)
            app_lang.save_choice(code)
            self.language.set(app_lang.NAMES[code])
            self.report_button.configure(text=T("Report a bug"))
            self.logs_button.configure(text=T("Open logs folder"))

    # plumbing
    def clear(self, title, subtitle=""):
        for child in self.body.winfo_children():
            child.destroy()
        self.ttk.Label(self.body, text=title, style="Title.TLabel").pack(anchor="w")
        if subtitle:
            self.ttk.Label(self.body, text=subtitle, style="Sub.TLabel", wraplength=740,
                           justify="left").pack(anchor="w", pady=(4, 14))
        else:
            self.ttk.Frame(self.body, height=10).pack()
        return self.body

    def work(self, job, done, failed=None):
        """job() off the UI thread; done(result) or failed(error) back on it."""
        def run():
            try:
                self.messages.put((done, job()))
            except Exception as error:   # shown to the tester as a message, never a traceback
                self.messages.put((failed or self.error, error))
        threading.Thread(target=run, daemon=True).start()

    def pump(self):
        while True:
            try:
                handler, value = self.messages.get_nowait()
            except queue.Empty:
                break
            try:
                handler(value)
            except Exception as error:   # 1.1.8 (L14.9): one failing handler must not stop every later result
                self.error(error)
        self.window.after(100, self.pump)

    def error(self, error):
        from tkinter import messagebox
        messagebox.showerror("Cry of Fear Co-op", T(str(error)), parent=self.window)

    def guard(self, action):
        def run(*_):
            try:
                action()
            except (OSError, ValueError, RuntimeError, network.NetworkError, subprocess.SubprocessError) as error:
                self.error(error)   # 1.1.8 (L3): the windowed app has no stderr, an uncaught error was a dead button
        return run

    def close(self):
        from tkinter import messagebox
        warning = BUSY_WARNINGS.get(self.busy) or play.close_warning(self.watch)
        if warning and not messagebox.askokcancel("Cry of Fear Co-op", T(warning), parent=self.window):
            return
        if self.watch:
            self.watch.close()
        self.window.destroy()

    def open_logs(self):
        if not self.root:
            raise ValueError(T("Set up the co-op copy first."))
        log = self.root / play.LOG
        if log.is_file():
            subprocess.Popen(["explorer", "/select,", str(log)])
        else:
            os.startfile(self.root / "story-logs" if (self.root / "story-logs").is_dir() else self.root)

    def row(self, parent, label, width=16):
        holder = self.ttk.Frame(parent)
        holder.pack(fill="x", pady=3)
        self.ttk.Label(holder, text=T(label), width=width, style="Field.TLabel").pack(side="left")
        return holder

    # setup and update
    def show_setup(self):
        ttk, tk = self.ttk, self.tk
        self.redraw = None if self.busy else self.show_setup
        body = self.clear(T("Set up story co-op"), T("The app makes a separate copy of Cry of Fear for co-op. Your "
                          "Steam game, saves and settings are not changed. About 6 GB of free space is needed."))
        source = tk.StringVar()
        destination = tk.StringVar(value=str(DEFAULT_COPY))
        for label, var, pick in (("Cry of Fear folder", source, self.pick_folder), ("Co-op copy", destination, self.pick_parent)):
            holder = self.row(body, label, 18)
            ttk.Entry(holder, textvariable=var).pack(side="left", fill="x", expand=True)
            ttk.Button(holder, text=T("Browse…"), command=lambda v=var, p=pick: p(v)).pack(side="left", padx=4)
        button = ttk.Button(body, text=T("Set up"), style="Accent.TButton")
        button.pack(anchor="w", pady=16)
        panel = self.progress_panel(body)
        panel.status.set(T("Looking for Cry of Fear in Steam…"))

        def found(candidates):
            if candidates and not source.get():
                source.set(str(candidates[0]))
            if panel.started is None:   # not once setup has begun
                panel.status.set(T("Ready: click Set up.") if source.get() else
                                 T("Pick your Cry of Fear folder (the one with CoFLaunchApp.exe)."))

        def stopped(error):
            panel.stop()
            self.busy = None
            button.configure(state="normal")
            left = setup_lib.incomplete_copy(Path(destination.get()))
            panel.status.set(T("Setup stopped. Your Steam game is unchanged. Click Set up to delete the unfinished copy "
                               "and try again.") if left else T("Setup stopped. Your Steam game is unchanged."))
            self.error(error)

        def made(copy):
            panel.stop()
            self.busy = None
            remember_copy(copy)
            self.show_main(copy)

        def start():
            from tkinter import messagebox
            src, dst = Path(source.get()), Path(destination.get())
            # 1.1.8 (L4): a setup that stopped leaves its copy (up to 5 GB) in the folder, which setup never reuses
            replace = setup_lib.incomplete_copy(dst)
            if replace and not messagebox.askyesno(T("Set up"), T("{folder} holds the unfinished copy of a setup that "
                                                   "stopped. Delete it and set up again?", folder=dst), parent=self.window):
                return
            button.configure(state="disabled")
            panel.start()
            self.busy = "setup"
            self.redraw = None
            self.work(lambda: setup_lib.setup(src, dst, self.payload, exe=self.exe, progress=self.reporter(panel),
                                              replace_incomplete=replace),
                      made, stopped)
        button.configure(command=start)
        self.work(setup_lib.steam_candidates, found, lambda _: found([]))

    def pick_folder(self, var):
        from tkinter import filedialog
        chosen = filedialog.askdirectory(parent=self.window, title=T("The Cry of Fear folder (with CoFLaunchApp.exe)"))
        if chosen:
            var.set(chosen)

    def pick_parent(self, var):
        from tkinter import filedialog
        chosen = filedialog.askdirectory(parent=self.window, title=T("Where should the CoF-Story-Coop folder go?"))
        if chosen:
            var.set(str(Path(chosen) / "CoF-Story-Coop"))

    def show_update(self, copy, auto=False, wait=(), repair=False):
        """auto: started by the previous version's "Update now", which is closing: wait for it, then update.
        repair: the same update over a copy of this build that fails its install check (offer_repair)."""
        ttk = self.ttk
        self.redraw = None
        if repair:
            body = self.clear(T("Repair story co-op"), T("Version {version} is installed again over your copy in "
                              "{copy}, and its models and merged maps are made again from its own game files. Your "
                              "saves and settings are kept.", version=play.VERSION_LABEL, copy=copy))
        else:
            body = self.clear(T("Update story co-op"), T("Version {version} is ready to install over your copy in "
                              "{copy}. Your saves and settings are kept.", version=play.VERSION_LABEL, copy=copy))
        button = ttk.Button(body, text=T("Repair") if repair else T("Update"), style="Accent.TButton")
        button.pack(anchor="w", pady=16)
        panel = self.progress_panel(body)
        report = self.reporter(panel)

        def stopped(error):
            panel.stop()
            self.busy = None
            button.configure(state="normal")
            self.error(error)

        def updated(root):
            panel.stop()
            self.busy = None
            self.show_main(root)

        def job():
            if wait:
                report(T("Waiting for the old version to close…"), 0, stamped("Waiting for the old version to close"))
                app_update.wait_for_exit(list(wait))
            return setup_lib.update(copy, self.payload, exe=self.exe, progress=report)

        def start():
            button.configure(state="disabled")
            panel.start()
            self.busy = "update"
            self.work(job, updated, stopped)
        button.configure(command=start)
        if auto:
            start()

    def offer_repair(self, error):
        """1.1.8 (L1): a copy that fails its install check (CopyMismatch) at this app's build had no way back
        but a new setup: Update is offered only to older copies. Repair runs the same update with this app's own
        payload. Not offered without a payload, or to a copy newer than this app (it would go back a version)."""
        from tkinter import messagebox
        state = setup_lib.installed_state(self.root) if self.root else None
        if self.payload is None or not state or state["build"] > play.BUILD:
            self.error(error)
            return
        if messagebox.askyesno(T("Repair"), T(error.problem) + "\n\n" + T(
                               "Repair it now? The co-op engine is installed again and the models and merged maps are "
                               "made again from the copy's own game files (a few minutes). Your saves and settings are "
                               "kept."), parent=self.window):
            self.show_update(self.root, auto=True, repair=True)

    def show_download(self, release):
        """The main screen's "Update now": the new version's zip, then the new app takes over and this one closes."""
        ttk = self.ttk
        self.redraw = None
        body = self.clear(T("Update to {label}", label=release.label), T(
            "Downloading version {label} ({size}). When it's done this window closes and the new version updates your "
            "copy. Your saves and settings are kept.", label=release.label, size=setup_lib.size_text(release.size)))
        stop = threading.Event()
        cancel = ttk.Button(body, text=T("Cancel"), command=stop.set)
        cancel.pack(anchor="w", pady=14)
        panel = self.progress_panel(body)
        report = self.reporter(panel)
        folder = app_update.DOWNLOADS / release.label
        shown = [-1]

        def progress(done, total):
            percent = done * 100 // total
            if percent != shown[0]:
                shown[0] = percent
                report(T("Downloading {label}: {done} of {total}", label=release.label,
                         done=setup_lib.size_text(done), total=setup_lib.size_text(total)), done / total * .9, None)

        def job():
            report(T("Downloading {label}", label=release.label), 0,
                   stamped(f"Downloading {release.zip} from the releases page"))
            path = app_update.download(release, folder, progress, stop.is_set)
            report(T("Unpacking"), .92, stamped("Downloaded; its size and SHA-256 match the release"))
            exe = app_update.extract_app(path, folder)
            report(T("Starting the new version"), 1, stamped("Unpacked the new app; starting it"))
            return exe

        def ready(exe):
            panel.stop()
            try:
                app_update.start_new_app(exe, self.root, app_update.pids_to_wait())
            except OSError as error:
                self.show_main(self.root)
                self.error(T("The new version didn't start: {error}", error=error))
                return
            if self.watch:
                self.watch.close()
            self.window.destroy()

        def stopped(error):
            panel.stop()
            self.show_main(self.root)
            if not isinstance(error, app_update.Cancelled):
                self.error(update_error(error))
        panel.start()
        self.work(job, ready, stopped)

    # updates
    def check_for_update(self):
        """Once per run, off the UI thread; no answer (offline, GitHub down) just means no offer."""
        if self.checked:
            return
        self.checked = True
        exe = self.exe

        def job():
            app_update.clean_downloads(exe)
            return app_update.newer_release(play.BUILD)
        self.work(job, self.offer_update, lambda _: None)

    def offer_update(self, release):
        if release:
            self.offer = release
            self.show_offer()

    def show_offer(self):
        ttk, slot, release = self.ttk, self.update_slot, self.offer
        if release is None or slot is None or not slot.winfo_exists():
            return
        for child in slot.winfo_children():
            child.destroy()
        box = self.tk.Frame(slot, background=theme.PANEL, highlightthickness=1, highlightbackground=theme.ACCENT_LO)
        box.pack(fill="x", pady=(0, 12))
        self.tk.Frame(box, background=theme.ACCENT, width=3).pack(side="left", fill="y")
        inner = ttk.Frame(box, style="Card.TFrame", padding=(14, 10, 12, 10))
        inner.pack(side="left", fill="both", expand=True)
        ttk.Button(inner, text=T("What's new"), command=lambda: webbrowser.open(release.page_url)).pack(side="right")
        ttk.Button(inner, text=T("Update now"), command=self.guard(self.update_now),
                   style="Accent.TButton").pack(side="right", padx=8)
        ttk.Label(inner, text=T("Version {label} is out", label=release.label), style="Offer.TLabel").pack(anchor="w")
        ttk.Label(inner, text=T("Saves and settings are kept."), style="OfferText.TLabel").pack(anchor="w")

    def update_now(self):
        if not self.root or self.offer is None:
            raise ValueError(T("Set up the co-op copy first."))
        if setup_lib.game_is_running(self.root):
            raise ValueError(T("Close Cry of Fear first, then update."))
        self.show_download(self.offer)

    def progress_panel(self, parent, clock=time.monotonic):
        self.panel = ProgressPanel(self.tk, self.ttk, parent, clock)
        return self.panel

    def reporter(self, panel):
        """setup's progress(status, fraction, line), from its worker thread to the panel on the UI thread."""
        return lambda *report: self.messages.put((lambda value: panel.show(*value), report))

    def show_newer(self, copy):
        """1.1.9 (L14.11): a copy newer than this app. Its main screen would start the newer engine with this older
        app's settings and version check, so the copy's own app is the only way offered."""
        self.root = Path(copy)
        self.redraw = lambda: self.show_newer(copy)
        self.clear(T("Use the Cry of Fear Co-op shortcut"),
                   T("Your co-op copy in {copy} has a newer version than this app ({version}). Start it from the Cry "
                     "of Fear Co-op shortcut on your desktop or in the Start menu, or run {exe} in that folder.",
                     copy=copy, version=play.VERSION_LABEL, exe=setup_lib.APP_EXE))

    # main
    def show_main(self, copy, adopt=True):
        """1.3.7 (the owner: "now that we've integrated the fucking campaign co-op into the main menu, the launcher is
        kind of redundant"; his pick: a one-button launcher). Play opens the game at its main menu, whose Co-op list
        hosts, joins, continues or plays with AI (mainui menus/CoFStoryCoop.cpp); this app keeps setup, updates, the
        router port and the join code. Its own Play solo, Host, Join and own-co-op screens are gone (the game's
        Co-op pages send what they sent: tests/test_menu_coop.py)."""
        ttk, tk = self.ttk, self.tk
        self.root = Path(copy)
        self.settings = play.load_settings(self.root)
        self.redraw = lambda: self.show_main(copy, adopt=False)
        if adopt:   # not when the footer's list just drew it again: config.cfg may still have the old language
            self.adopt_game_language()
        body = self.clear(T("Play the story together"),
                          T("Co-op is in the game's own main menu, right under New Game: host, join, continue, or "
                            "play with AI companions."))
        self.update_slot = ttk.Frame(body)
        self.update_slot.pack(fill="x")
        self.show_offer()
        self.show_rejoin(body)
        holder = self.row(body, "Your name", 12)
        self.name = tk.StringVar(value=getattr(self, "typed_name", None) or self.settings["name"])
        self.name.trace_add("write", lambda *_: setattr(self, "typed_name", self.name.get()))
        ttk.Entry(holder, textvariable=self.name, width=28).pack(side="left")
        self.port = tk.StringVar(value=str(self.settings["port"]))
        ttk.Entry(holder, textvariable=self.port, width=7).pack(side="right")
        ttk.Label(holder, text=T("Port (UDP)"), style="Field.TLabel").pack(side="right", padx=(0, 8))
        self.open_button = ttk.Button(body, text=T("Play"), style="Play.TButton", command=self.guard(self.open_game))
        self.open_button.pack(fill="x", pady=(18, 8))
        ttk.Label(body, text=T("Hosting: Co-op, then Host co-op in the game; send your friends the join code this "
                               "window shows. Joining: Co-op, then Join co-op, and paste the code your host sent you."),
                  style="Hint.TLabel", wraplength=760, justify="left").pack(anchor="w")
        theme.section(ttk, body, T("In the game"), top=18)
        theme.keycaps(tk, body, [(key, T(text)) for key, text in COOP_KEYS])
        self.show_comfort(body)
        self.check_for_update()

    def show_comfort(self, body):
        """Player tools (2026-11-09): this player's comfort settings and spray, saved at once and written into every
        game this app starts, host or guest (play.comfort_config, play.install_spray in launch)."""
        ttk, tk = self.ttk, self.tk
        theme.section(ttk, body, T("Comfort"), top=6)
        row = ttk.Frame(body)
        row.pack(fill="x")
        ttk.Label(row, text=T("Field of view"), style="Field.TLabel").pack(side="left")
        fov = tk.StringVar(value=str(self.settings["fov"]))

        def fov_changed(*_):
            try:
                value = int(fov.get().strip())
            except ValueError:
                return
            if play.FOV[0] <= value <= play.FOV[1] and value != self.settings.get("fov"):
                self.remember(fov=value)
        fov.trace_add("write", fov_changed)
        ttk.Spinbox(row, from_=play.FOV[0], to=play.FOV[1], increment=5, width=4,
                    textvariable=fov).pack(side="left", padx=(6, 0))
        for key, label in COMFORT_CHECKS:
            var = tk.BooleanVar(value=self.settings[key])
            ttk.Checkbutton(row, text=T(label), variable=var,
                            command=lambda k=key, v=var: self.remember(**{k: v.get()})).pack(side="left", padx=(14, 0))
        spray = self.settings["spray"]
        self.spray_name = tk.StringVar(value=Path(spray).name if spray else "")
        ttk.Button(row, text=T("Spray…"), command=self.guard(self.pick_spray), style="Ghost.TButton").pack(side="right")
        ttk.Label(row, textvariable=self.spray_name, style="Hint.TLabel").pack(side="right", padx=(0, 4))

    def pick_spray(self):
        """The spray (T in the game): a .png or .bmp picture, copied into the copy when the game starts."""
        from tkinter import filedialog
        chosen = filedialog.askopenfilename(parent=self.window, title=T("Your spray (a .png or .bmp picture)"),
                                            filetypes=[(T("Spray picture"), "*.png *.bmp")])
        if not chosen:
            return
        if not play.spray_type(chosen):
            raise ValueError(T("A spray is a .png or .bmp picture."))
        self.remember(spray=str(chosen))
        self.spray_name.set(Path(chosen).name)

    def show_rejoin(self, body):
        """Player tools (2026-11-09): the last host this player's game joined over the network (play.read_last_host,
        when that game ended), offered until dismissed."""
        host = self.settings.get("last_host", "")
        if not host:
            return
        ttk = self.ttk
        box = self.tk.Frame(body, background=theme.PANEL, highlightthickness=1, highlightbackground=theme.LINE)
        box.pack(fill="x", pady=(0, 12))
        self.tk.Frame(box, background=theme.ACCENT, width=3).pack(side="left", fill="y")
        inner = ttk.Frame(box, style="Card.TFrame", padding=(14, 10, 12, 10))
        inner.pack(side="left", fill="both", expand=True)
        ttk.Button(inner, text=T("Dismiss"), command=self.guard(self.dismiss_rejoin)).pack(side="right")
        ttk.Button(inner, text=T("Rejoin"), command=self.guard(self.rejoin), style="Accent.TButton").pack(side="right",
                                                                                                         padx=8)
        ttk.Label(inner, text=T("Rejoin {host}", host=host), style="Offer.TLabel").pack(anchor="w")
        ttk.Label(inner, text=T("The last game you joined."), style="OfferText.TLabel").pack(anchor="w")

    def dismiss_rejoin(self):
        self.remember(last_host="")
        self.show_main(self.root, adopt=False)

    def rejoin(self):
        """Back into the last host's game (play.join_launch, no password: a game with one says so); when the player
        leaves it, the game is at its main menu, as after Play."""
        name = play.clean_name(self.name.get())
        args, config = play.join_launch(self.root, name=name, password="", endpoint=self.settings["last_host"],
                                        video=self.video())
        self.open_button.configure(state="disabled")
        root = self.root

        def started():
            self.show_running(play.MenuWatch(play.DEFAULT_PORT, runtime=root))

        def stopped():
            if self.open_button.winfo_exists():
                self.open_button.configure(state="normal")
        self.launch(args, config, started, stopped)

    def game_ended(self, root):
        """When a game ends: the host it last joined over the network, for the main screen's rejoin."""
        host = play.read_last_host(root)
        if host and self.root is not None and Path(root) == Path(self.root):
            self.remember(last_host=host)

    def remember(self, **values):
        self.settings.update(values)
        play.save_settings(self.root, self.settings)

    def maps_tag(self) -> str:
        """This copy's merged maps, for the cof_maps a game hosted from the menu sends (1.1.8, L8)."""
        merged = (setup_lib.installed_state(self.root) or {}).get("merged_maps")
        return play.merged_maps_tag(merged if isinstance(merged, dict) else {})

    def launch(self, args, config, started, stopped=lambda: None):
        """Starts the game, then started(); stopped() when it does not start. The copy's install check (it hashes
        about 0.6 GB) and the running-game check (PowerShell) run off the UI thread (1.1.8, L11: they froze the
        window); a copy that fails its install check is offered Repair (L1)."""
        if self.launching:
            stopped()
            self.error(T("Cry of Fear is starting already. Wait a moment."))
            return
        self.launching = True
        root = self.root

        def check():
            setup_lib.validate_runtime(root)
            return setup_lib.game_is_running(root)

        def checked(running):
            self.launching = False
            try:
                if running:
                    raise ValueError(T("Cry of Fear is already running from this copy. Close it first."))
                app_lang.write_game_language(root, app_lang.current())   # 1.5: the window's language, the game's
                (root / "cryoffear" / play.STORY_CFG).write_text(play.story_config(), encoding="ascii")
                play.write_invite(root)   # 1.3.6: no other session's join code in the game menu
                carry_unlocks(root)       # 1.3.8: what the player has unlocked in their own Cry of Fear
                # 1.2.0: a co-op key the player's own config leaves unbound (O in a copy set up before it) works;
                # player tools (2026-11-09): this player's comfort settings and spray, in every game, host or guest
                (root / "cryoffear" / play.SESSION_CFG).write_text(
                    config + setup_lib.missing_coop_binds(root) + play.comfort_config(self.settings)
                    + play.install_spray(root, self.settings.get("spray", "")), encoding="ascii")
                setup_lib.keep_previous_log(root, play.LOG)
                start_game(root, args)
                self.remember(name=self.name.get().strip(), first_launch_done=True)
                started()
            except (OSError, ValueError, RuntimeError, network.NetworkError, subprocess.SubprocessError) as error:
                stopped()
                self.error(error)

        def failed(error):
            self.launching = False
            stopped()
            if isinstance(error, setup_lib.CopyMismatch):
                self.offer_repair(error)
            else:
                self.error(error)
        self.work(check, checked, failed)

    def video(self):
        return play.video_flags(not self.settings["first_launch_done"], desktop_size())

    def open_game(self):
        """The game at its main menu (play.menu_launch); this window then shows the join code while it runs, and the
        menu's Host co-op shows it too (play.write_invite)."""
        name = play.clean_name(self.name.get())
        number = play.clean_port(self.port.get())
        args, config = play.menu_launch(self.root, name=name, port=number, maps=self.maps_tag(), video=self.video())
        self.open_button.configure(state="disabled")   # one start at a time while the copy is checked
        root = self.root

        def started():
            self.remember(port=number)
            self.show_running(play.MenuWatch(number, runtime=root))

        def stopped():
            if self.open_button.winfo_exists():
                self.open_button.configure(state="normal")
        self.launch(args, config, started, stopped)

    def show_running(self, watch):
        """While the game runs: what it is doing, the join code, the addresses and the router port (play.MenuWatch)."""
        ttk, tk = self.ttk, self.tk
        # 1.1.9 (L14.4): a game started again before the last one's watch saw it end: that watch would close later
        # and remove the router mapping the new one has just taken over. It is closed first, off the UI thread.
        previous = self.watch if self.watch is not watch else None
        self.watch = watch
        self.redraw = None

        def release():
            if previous is not None:
                try:
                    previous.close()
                except Exception:   # the new game's router port comes first
                    pass
        body = self.clear(T("Cry of Fear is open"),
                          T("Pick Co-op in the game's main menu. If you host, send your friends the join code (and the "
                            "password, if you set one); the game's Host co-op page shows it too. Keep this window open "
                            "while you play. If Windows Firewall asks about Cry of Fear (its window can be hidden "
                            "behind the game), click Allow access, or friends can't join."))
        status = tk.StringVar(value=T("Starting the game..."))
        state = tk.Frame(body, background=theme.PANEL, highlightthickness=1, highlightbackground=theme.LINE)
        state.pack(fill="x", pady=(0, 6))
        tk.Frame(state, background=theme.ACCENT, width=3).pack(side="left", fill="y")
        tk.Label(state, textvariable=status, background=theme.PANEL, foreground=theme.TEXT, font=theme.FONTS.get("body"),
                 wraplength=700, justify="left", anchor="w", padx=12, pady=10).pack(side="left", fill="x", expand=True)
        code, address, lan, router = tk.StringVar(value=T("(looking…)")), tk.StringVar(), tk.StringVar(), tk.StringVar()
        for label, var in (("Join code", code), ("Internet address", address), ("Same place (LAN)", lan)):
            holder = self.row(body, label)
            ttk.Entry(holder, textvariable=var, state="readonly").pack(side="left", fill="x", expand=True)
            ttk.Button(holder, text=T("Copy"), command=lambda v=var: (self.window.clipboard_clear(),
                                                                  self.window.clipboard_append(v.get()))).pack(side="left", padx=4)
        ttk.Label(body, textvariable=router, style="Hint.TLabel", wraplength=740).pack(anchor="w", pady=8)
        root = self.root

        def invite():
            # 1.3.6: the game menu's Host co-op shows what this window shows
            try:
                play.write_invite(root, watch.code, watch.address, lan.get())
            except OSError:
                pass

        def routed(_):
            code.set(watch.code or T("(no Internet address found: use the LAN or VPN address)"))
            address.set(watch.address)
            router.set(T(watch.router))
            invite()

        def found(rows):
            lan.set(f"{rows[0]['address']}:{watch.port}" if rows else "")
            invite()
        self.work(lambda: (release(), watch.open_router()), routed, lambda error: router.set(T(str(error))))
        self.work(network.local_addresses, found, lambda _: None)
        ttk.Button(body, text=T("Back"), command=lambda: self.show_main(self.root)).pack(anchor="w", pady=14)

        def loop():
            time.sleep(5)
            while not watch.closed:
                try:
                    running = setup_lib.game_is_running(root, unknown=True)
                except (OSError, ValueError, subprocess.SubprocessError):
                    running = True
                self.messages.put((status.set, T(watch.poll(running))))
                time.sleep(3)
            self.messages.put((self.game_ended, root))
        threading.Thread(target=loop, daemon=True).start()


def open_window(payload, exe, copy=None, update_from=None, wait=()):
    import tkinter as tk
    dpi_aware()
    app_lang.init()
    window = tk.Tk()
    app = App(window, payload, exe)
    theme.dark_title_bar(window)
    window_icon(window)
    state = setup_lib.installed_state(update_from) if update_from else None
    if state and state["build"] < play.BUILD:
        app.show_update(update_from, auto=True, wait=wait)
    elif state or copy:
        app.show_main(copy or update_from)
    else:
        mode, found = where_to_start(exe.parent, DEFAULT_COPY, play.BUILD)
        if mode == "setup":
            app.show_setup()
        elif mode == "update":
            app.show_update(found)
        elif mode == "newer":
            app.show_newer(found)
        else:
            app.show_main(found)
    window.mainloop()


def window_icon(window) -> None:
    """The app's own icon (1.2.1) on the window and taskbar; packed in the app's files (build_tester_package)."""
    base = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[1]))
    icon = base / "tools/app.ico"
    try:
        if os.name == "nt" and icon.is_file():
            window.iconbitmap(default=str(icon))
    except Exception:   # a missing or unreadable icon is only cosmetic
        pass


def self_test(payload) -> int:
    import tkinter
    assert tkinter.Tcl().eval("info patchlevel")
    if getattr(sys, "frozen", False):
        assert setup_lib.inspect_payload(payload)["build"] == play.BUILD
    assert setup_lib.merge_groups.load_groups(), "the merged-map groups are missing"
    args, config = play.menu_launch(Path("copy"), name="Test", port=play.DEFAULT_PORT, maps="", video=[])
    assert "cof_build " + play.VERSION in config and "-console" not in args
    return 0


def update_check(out: Path) -> int:
    """Reads the releases page the way the window does and writes what it found (the release check runs this
    on the packaged exe, which has no console)."""
    import dataclasses
    import json
    try:
        latest = app_update.parse_release(app_update.read_info())
        result = {"ok": True, "build": play.BUILD, "latest": dataclasses.asdict(latest),
                  "newer": latest.build > play.BUILD}
    except Exception as error:   # reported, not raised: the caller reads the file
        result = {"ok": False, "build": play.BUILD, "error": f"{type(error).__name__}: {error}"}
    out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return 0 if result["ok"] else 1


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--install", nargs=2, metavar=("SOURCE", "DEST"), help="set up a copy without the window")
    parser.add_argument("--update", metavar="COPY", help="update a copy without the window")
    parser.add_argument("--payload", help="the engine payload folder, for a run from source")
    parser.add_argument("--copy", help="open the main window for this copy")
    parser.add_argument("--no-shortcuts", action="store_true")
    parser.add_argument("--update-from", help=argparse.SUPPRESS)   # the previous version's "Update now"
    parser.add_argument("--wait-pid", help=argparse.SUPPRESS)
    parser.add_argument("--update-check", metavar="OUT", help=argparse.SUPPRESS)   # releases: can this exe see the page
    args = parser.parse_args(argv)
    try:
        payload = payload_dir(args.payload) if (args.payload or getattr(sys, "frozen", False)) else None
        if args.self_test:
            return self_test(payload)
        if args.update_check:
            return update_check(Path(args.update_check))
        if args.install or args.update:
            if payload is None:
                return 2
            if args.install:
                setup_lib.setup(Path(args.install[0]), Path(args.install[1]), payload, exe=this_exe(),
                                shortcuts=not args.no_shortcuts)
            else:
                setup_lib.update(Path(args.update), payload, exe=this_exe(), shortcuts=not args.no_shortcuts)
            return 0
        open_window(payload, this_exe(), Path(args.copy).resolve() if args.copy else None,
                    update_from=Path(args.update_from).resolve() if args.update_from else None,
                    wait=app_update.parse_pids(args.wait_pid))
        return 0
    except (OSError, ValueError, RuntimeError, AssertionError) as error:
        if sys.stderr:
            print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
