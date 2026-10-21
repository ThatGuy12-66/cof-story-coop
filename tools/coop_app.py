"""Cry of Fear story co-op: the app. Setup on first run, then Play solo, Host or Join."""
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
from tools import app_theme as theme
from tools import app_update
from tools import coop_play as play
from tools import friend_setup as setup_lib

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
    """CoFLaunchApp.exe is a console program: without CREATE_NO_WINDOW, Windows opens a
    console window beside the game (and closing that window would close the game)."""
    return subprocess.Popen(args, cwd=root, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, creationflags=0x08000000)   # CREATE_NO_WINDOW


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
        return "Couldn't download the update. Check your internet connection and try again."
    return f"The update stopped: {error}"


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
        self.elapsed.set(f"{elapsed_text(0)} so far")
        self.job = self.box.after(1000, self.tick)

    def tick(self):
        if self.job:
            self.box.after_cancel(self.job)
        self.elapsed.set(f"{elapsed_text(self.clock() - self.started)} so far")
        self.job = self.box.after(1000, self.tick)

    def stop(self):
        if self.job:
            self.box.after_cancel(self.job)
        self.job = None

    def show(self, status, part, line):
        self.status.set(status)
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
             ("H", "AI companions wait or follow"), ("O", "the wardrobe (with the bonus room)"))

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
        self.launching = False     # a Start or Join is checking the copy (launch)
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
        ttk.Button(foot, text="Report a bug", command=report_bug, style="Ghost.TButton").pack(side="right")
        ttk.Button(foot, text="Open logs folder", command=self.guard(self.open_logs),
                   style="Ghost.TButton").pack(side="right", padx=4)
        self.body = ttk.Frame(window, padding=(26, 18, 26, 14))
        self.body.pack(fill="both", expand=True)
        window.protocol("WM_DELETE_WINDOW", self.close)
        window.after(100, self.pump)

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
        messagebox.showerror("Cry of Fear Co-op", str(error), parent=self.window)

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
        if warning and not messagebox.askokcancel("Cry of Fear Co-op", warning, parent=self.window):
            return
        if self.watch:
            self.watch.close()
        self.window.destroy()

    def open_logs(self):
        if not self.root:
            raise ValueError("Set up the co-op copy first.")
        log = self.root / play.LOG
        if log.is_file():
            subprocess.Popen(["explorer", "/select,", str(log)])
        else:
            os.startfile(self.root / "story-logs" if (self.root / "story-logs").is_dir() else self.root)

    def row(self, parent, label, width=16):
        holder = self.ttk.Frame(parent)
        holder.pack(fill="x", pady=3)
        self.ttk.Label(holder, text=label, width=width, style="Field.TLabel").pack(side="left")
        return holder

    # setup and update
    def show_setup(self):
        ttk, tk = self.ttk, self.tk
        body = self.clear("Set up story co-op", "The app makes a separate copy of Cry of Fear for co-op. Your Steam "
                          "game, saves and settings are not changed. About 6 GB of free space is needed.")
        source = tk.StringVar()
        destination = tk.StringVar(value=str(DEFAULT_COPY))
        for label, var, pick in (("Cry of Fear folder", source, self.pick_folder), ("Co-op copy", destination, self.pick_parent)):
            holder = self.row(body, label, 18)
            ttk.Entry(holder, textvariable=var).pack(side="left", fill="x", expand=True)
            ttk.Button(holder, text="Browse…", command=lambda v=var, p=pick: p(v)).pack(side="left", padx=4)
        button = ttk.Button(body, text="Set up", style="Accent.TButton")
        button.pack(anchor="w", pady=16)
        panel = self.progress_panel(body)
        panel.status.set("Looking for Cry of Fear in Steam…")

        def found(candidates):
            if candidates and not source.get():
                source.set(str(candidates[0]))
            if panel.started is None:   # not once setup has begun
                panel.status.set("Ready: click Set up." if source.get() else
                                 "Pick your Cry of Fear folder (the one with CoFLaunchApp.exe).")

        def stopped(error):
            panel.stop()
            self.busy = None
            button.configure(state="normal")
            left = setup_lib.incomplete_copy(Path(destination.get()))
            panel.status.set("Setup stopped. Your Steam game is unchanged." +
                             (" Click Set up to delete the unfinished copy and try again." if left else ""))
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
            if replace and not messagebox.askyesno("Set up", f"{dst} holds the unfinished copy of a setup that "
                                                   "stopped. Delete it and set up again?", parent=self.window):
                return
            button.configure(state="disabled")
            panel.start()
            self.busy = "setup"
            self.work(lambda: setup_lib.setup(src, dst, self.payload, exe=self.exe, progress=self.reporter(panel),
                                              replace_incomplete=replace),
                      made, stopped)
        button.configure(command=start)
        self.work(setup_lib.steam_candidates, found, lambda _: found([]))

    def pick_folder(self, var):
        from tkinter import filedialog
        chosen = filedialog.askdirectory(parent=self.window, title="The Cry of Fear folder (with CoFLaunchApp.exe)")
        if chosen:
            var.set(chosen)

    def pick_parent(self, var):
        from tkinter import filedialog
        chosen = filedialog.askdirectory(parent=self.window, title="Where should the CoF-Story-Coop folder go?")
        if chosen:
            var.set(str(Path(chosen) / "CoF-Story-Coop"))

    def show_update(self, copy, auto=False, wait=(), repair=False):
        """auto: started by the previous version's "Update now", which is closing: wait for it, then update.
        repair: the same update over a copy of this build that fails its install check (offer_repair)."""
        ttk = self.ttk
        if repair:
            body = self.clear("Repair story co-op", f"Version {play.VERSION_LABEL} is installed again over your copy "
                              f"in {copy}, and its models and merged maps are made again from its own game files. "
                              "Your saves and settings are kept.")
        else:
            body = self.clear("Update story co-op", f"Version {play.VERSION_LABEL} is ready to install over your copy "
                              f"in {copy}. Your saves and settings are kept.")
        button = ttk.Button(body, text="Repair" if repair else "Update", style="Accent.TButton")
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
                report("Waiting for the old version to close…", 0, stamped("Waiting for the old version to close"))
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
        if messagebox.askyesno("Repair", f"{error.problem}\n\nRepair it now? The co-op engine is installed again "
                               "and the models and merged maps are made again from the copy's own game files (a few "
                               "minutes). Your saves and settings are kept.", parent=self.window):
            self.show_update(self.root, auto=True, repair=True)

    def show_download(self, release):
        """The main screen's "Update now": the new version's zip, then the new app takes over and this one closes."""
        ttk = self.ttk
        body = self.clear(f"Update to {release.label}", f"Downloading version {release.label} "
                          f"({setup_lib.size_text(release.size)}). When it's done this window closes and the new "
                          "version updates your copy. Your saves and settings are kept.")
        stop = threading.Event()
        cancel = ttk.Button(body, text="Cancel", command=stop.set)
        cancel.pack(anchor="w", pady=14)
        panel = self.progress_panel(body)
        report = self.reporter(panel)
        folder = app_update.DOWNLOADS / release.label
        shown = [-1]

        def progress(done, total):
            percent = done * 100 // total
            if percent != shown[0]:
                shown[0] = percent
                report(f"Downloading {release.label}: {setup_lib.size_text(done)} of {setup_lib.size_text(total)}",
                       done / total * .9, None)

        def job():
            report(f"Downloading {release.label}", 0, stamped(f"Downloading {release.zip} from the releases page"))
            path = app_update.download(release, folder, progress, stop.is_set)
            report("Unpacking", .92, stamped("Downloaded; its size and SHA-256 match the release"))
            exe = app_update.extract_app(path, folder)
            report("Starting the new version", 1, stamped("Unpacked the new app; starting it"))
            return exe

        def ready(exe):
            panel.stop()
            try:
                app_update.start_new_app(exe, self.root, app_update.pids_to_wait())
            except OSError as error:
                self.show_main(self.root)
                self.error(f"The new version didn't start: {error}")
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
        ttk.Button(inner, text="What's new", command=lambda: webbrowser.open(release.page_url)).pack(side="right")
        ttk.Button(inner, text="Update now", command=self.guard(self.update_now),
                   style="Accent.TButton").pack(side="right", padx=8)
        ttk.Label(inner, text=f"Version {release.label} is out", style="Offer.TLabel").pack(anchor="w")
        ttk.Label(inner, text="Saves and settings are kept.", style="OfferText.TLabel").pack(anchor="w")

    def update_now(self):
        if not self.root or self.offer is None:
            raise ValueError("Set up the co-op copy first.")
        if setup_lib.game_is_running(self.root):
            raise ValueError("Close Cry of Fear first, then update.")
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
        self.clear("Use the Cry of Fear Co-op shortcut",
                   f"Your co-op copy in {copy} has a newer version than this app ({play.VERSION_LABEL}). Start it "
                   f"from the Cry of Fear Co-op shortcut on your desktop or in the Start menu, or run "
                   f"{setup_lib.APP_EXE} in that folder.")

    # main
    def show_main(self, copy):
        ttk = self.ttk
        self.root = Path(copy)
        self.settings = play.load_settings(self.root)
        body = self.clear("Play the story together", "The whole campaign with friends, or alone with AI companions.")
        self.update_slot = ttk.Frame(body)
        self.update_slot.pack(fill="x")
        self.show_offer()
        holder = self.row(body, "Your name", 12)
        self.name = self.tk.StringVar(value=self.settings["name"])
        ttk.Entry(holder, textvariable=self.name, width=28).pack(side="left")
        ttk.Frame(body, height=8).pack()
        for title, text, action in (
                ("Play solo", "You and up to three AI companions, on this PC.", lambda: self.show_start(solo=True)),
                ("Host a game", "Start the story and send your friends the join code.",
                 lambda: self.show_start(solo=False)),
                ("Join a game", "Paste the join code your host sent you.", self.show_join)):
            theme.Card(self.tk, body, title, text, action)
        theme.section(ttk, body, "In the game", top=16)
        theme.keycaps(self.tk, body, COOP_KEYS)
        self.check_for_update()

    def remember(self, **values):
        self.settings.update(values)
        play.save_settings(self.root, self.settings)

    def maps_tag(self) -> str:
        """This copy's merged maps for the host's cof_maps and the join check (1.1.8, L8)."""
        merged = (setup_lib.installed_state(self.root) or {}).get("merged_maps")
        return play.merged_maps_tag(merged if isinstance(merged, dict) else {})

    def launch(self, args, config, started, stopped=lambda: None):
        """Starts the game, then started(); stopped() when it does not start. The copy's install check (it hashes
        about 0.6 GB) and the running-game check (PowerShell) run off the UI thread (1.1.8, L11: they froze the
        window); a copy that fails its install check is offered Repair (L1)."""
        if self.launching:
            stopped()
            self.error("Cry of Fear is starting already. Wait a moment.")
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
                    raise ValueError("Cry of Fear is already running from this copy. Close it first.")
                (root / "cryoffear" / play.STORY_CFG).write_text(play.story_config(), encoding="ascii")
                # 1.2.0: a co-op key the player's own config leaves unbound (O in a copy set up before it) works
                (root / "cryoffear" / play.SESSION_CFG).write_text(config + setup_lib.missing_coop_binds(root),
                                                                   encoding="ascii")
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

    def start_choice(self, body):
        ttk, tk = self.ttk, self.tk
        checkpoint = play.latest_checkpoint(self.root)
        kind = tk.StringVar(value="new")
        theme.section(ttk, body, "Start from", top=0)
        ttk.Radiobutton(body, text="New game (the intro; Enter skips cutscenes)", variable=kind, value="new").pack(anchor="w")
        holder = ttk.Frame(body)
        holder.pack(anchor="w", fill="x")
        ttk.Radiobutton(holder, text="Chapter:", variable=kind, value="chapter").pack(side="left")
        chapter = ttk.Combobox(holder, values=[label for label, _ in play.CHAPTER_CHOICES], state="readonly", width=44)
        chapter.current(1)
        chapter.pack(side="left", padx=8, pady=2)
        chapter.bind("<<ComboboxSelected>>", lambda _: kind.set("chapter"))
        text = f"Continue: {checkpoint.label} (experimental)" if checkpoint else "Continue (no co-op checkpoint yet)"
        ttk.Radiobutton(body, text=text, variable=kind, value="continue",
                        state="normal" if checkpoint else "disabled").pack(anchor="w")

        def resolve():
            if kind.get() == "continue":
                return checkpoint.map, checkpoint
            if kind.get() == "chapter":
                return play.CHAPTER_CHOICES[chapter.current()][1], None
            return play.NEW_GAME_MAP, None
        return resolve

    def show_start(self, solo):
        ttk, tk = self.ttk, self.tk
        body = self.clear("Play solo" if solo else "Host a game",
                          "You and one to three AI companions, on this PC only." if solo else
                          "Friends join you with the join code shown after the game starts.")
        buttons = ttk.Frame(body)   # packed first, at the bottom: a short window clips options, never Start
        buttons.pack(side="bottom", anchor="w", fill="x", pady=(14, 0))
        resolve = self.start_choice(body)
        players = tk.StringVar(value=str(self.settings["players"]))
        password = tk.StringVar(value=self.settings["password"])
        companion = tk.BooleanVar(value=self.settings["companion"])
        companions = tk.StringVar(value=str(self.settings["companions"]))
        port = tk.StringVar(value=str(self.settings["port"]))
        bonus = tk.BooleanVar(value=self.settings["bonus_room"])
        theme.section(ttk, body, "Options")
        ttk.Checkbutton(body, text="Bonus room unlocked (FAMAS, axe and more; a later chapter starts with them; "
                        "O opens the wardrobe)",
                        variable=bonus).pack(anchor="w")
        merged = tk.BooleanVar(value=self.settings["merged_maps"])
        ttk.Checkbutton(body, text="Merged maps (fewer loading screens; Continue keeps the checkpoint's own)",
                        variable=merged).pack(anchor="w")
        torch = tk.BooleanVar(value=self.settings["coop_flashlight"])
        ttk.Checkbutton(body, text="Co-op flashlight on F (a free torch for everyone; the original has only Simon's phone)",
                        variable=torch).pack(anchor="w")
        if solo:
            theme.section(ttk, body, "Party")
            ttk.Spinbox(self.row(body, "AI companions", 14), from_=1, to=play.MAX_COMPANIONS, textvariable=companions,
                        width=5, state="readonly").pack(side="left")
        if not solo:
            theme.section(ttk, body, "Hosting")
            ttk.Spinbox(self.row(body, "Players", 12), from_=2, to=4, textvariable=players, width=5).pack(side="left")
            holder = self.row(body, "Password", 12)
            ttk.Entry(holder, textvariable=password, show="*", width=28).pack(side="left")
            ttk.Label(holder, text="optional: friends need it to join", style="Hint.TLabel").pack(side="left", padx=10)
            ttk.Entry(self.row(body, "Port (UDP)", 12), textvariable=port, width=8).pack(side="left")
            ttk.Checkbutton(body, text="AI companions in the empty places (each makes room when a friend joins)",
                            variable=companion).pack(anchor="w", pady=4)

        def start():
            from tkinter import messagebox
            name = play.clean_name(self.name.get())
            map_name, checkpoint = resolve()
            if checkpoint:
                warning = play.continue_warning(checkpoint, name)
                if warning and not messagebox.askokcancel("Continue", warning, parent=self.window):
                    return
            ais = int(companions.get() or 1)
            wanted = 2 if solo else play.clean_players(players.get())
            count, ais = play.party_size(solo=solo, players=wanted,
                                         companions=ais, fill=companion.get(),
                                         checkpoint_players=checkpoint.players if checkpoint else None)
            if solo and checkpoint and ais < int(companions.get() or 1):
                messagebox.showinfo("Continue", f"This checkpoint was saved with {count} places, so {ais} AI "
                                    f"{'companion comes' if ais == 1 else 'companions come'} along this time.",
                                    parent=self.window)
            if not solo and checkpoint and count != wanted:
                # 1.1.9 (L14.6): Continue keeps the checkpoint's places (the engine needs them); the Players box was
                # ignored without a word
                messagebox.showinfo("Continue", f"This checkpoint was saved with {count} places, so the game has "
                                    f"{count} places this time, not {wanted}.", parent=self.window)
            number = play.DEFAULT_PORT if solo else play.clean_port(port.get())
            args, config = play.host_launch(self.root, solo=solo, name=name, map_name=map_name, players=count,
                                            password=password.get(), companions=ais, port=number,
                                            cont=checkpoint is not None, video=self.video(), bonus_room=bonus.get(),
                                            merged=play.merged_for_start(checkpoint, checkbox=merged.get()),
                                            coop_flashlight=torch.get(), maps=self.maps_tag())
            choices = dict(bonus_room=bonus.get(), merged_maps=merged.get(), coop_flashlight=torch.get())
            hosting = dict(players=self.settings["players"] if checkpoint else count, password=password.get(),
                           companion=companion.get(), port=number)

            def started():
                checking.set("")
                self.remember(**choices)
                if solo and not checkpoint:
                    self.remember(companions=ais)
                if not solo:
                    self.remember(**hosting)
                self.show_running(play.HostWatch(number, lan_only=solo), solo)

            def stopped():
                checking.set("")
                if start_button.winfo_exists():
                    start_button.configure(state="normal")
            start_button.configure(state="disabled")   # one start at a time while the copy is checked
            checking.set("Checking your copy…")
            self.launch(args, config, started, stopped)
        checking = tk.StringVar()
        start_button = ttk.Button(buttons, text="Start", command=self.guard(start), style="Accent.TButton")
        start_button.pack(side="left")
        ttk.Button(buttons, text="Back", command=lambda: self.show_main(self.root)).pack(side="left", padx=8)
        ttk.Label(buttons, textvariable=checking, style="Hint.TLabel").pack(side="left", padx=8)

    def show_running(self, watch, solo):
        ttk, tk = self.ttk, self.tk
        # 1.1.9 (L14.4): a game started again before the last one's watch saw it end: that watch would close later
        # and remove the router mapping the new one has just taken over. It is closed first, off the UI thread.
        previous = self.watch if self.watch is not watch else None
        self.watch = watch

        def release():
            if previous is not None:
                try:
                    previous.close()
                except Exception:   # the new game's router port comes first
                    pass
        body = self.clear("Playing solo" if solo else "Hosting",
                          "Keep this window open while you play." if solo else
                          "Send your friends the join code (and the password, if you set one). "
                          "Keep this window open while you host. If Windows Firewall asks about Cry of Fear (its "
                          "window can be hidden behind the game), click Allow access, or friends can't join.")
        status = tk.StringVar(value="Starting the game...")
        state = tk.Frame(body, background=theme.PANEL, highlightthickness=1, highlightbackground=theme.LINE)
        state.pack(fill="x", pady=(0, 6))
        tk.Frame(state, background=theme.ACCENT, width=3).pack(side="left", fill="y")
        tk.Label(state, textvariable=status, background=theme.PANEL, foreground=theme.TEXT, font=theme.FONTS.get("body"),
                 wraplength=700, justify="left", anchor="w", padx=12, pady=10).pack(side="left", fill="x", expand=True)
        if not solo:
            code, address, lan, router = tk.StringVar(value="(looking…)"), tk.StringVar(), tk.StringVar(), tk.StringVar()
            for label, var in (("Join code", code), ("Internet address", address), ("Same place (LAN)", lan)):
                holder = self.row(body, label)
                ttk.Entry(holder, textvariable=var, state="readonly").pack(side="left", fill="x", expand=True)
                ttk.Button(holder, text="Copy", command=lambda v=var: (self.window.clipboard_clear(),
                                                                      self.window.clipboard_append(v.get()))).pack(side="left", padx=4)
            ttk.Label(body, textvariable=router, style="Hint.TLabel", wraplength=740).pack(anchor="w", pady=8)

            def routed(_):
                code.set(watch.code or "(no Internet address found: use the LAN or VPN address)")
                address.set(watch.address)
                router.set(watch.router)
            self.work(lambda: (release(), watch.open_router()), routed, lambda error: router.set(str(error)))
            self.work(network.local_addresses,
                      lambda rows: lan.set(f"{rows[0]['address']}:{watch.port}" if rows else ""), lambda _: None)
        elif previous is not None:
            self.work(release, lambda _: None, lambda _: None)
        ttk.Button(body, text="Back", command=lambda: self.show_main(self.root)).pack(anchor="w", pady=14)
        root = self.root

        def loop():
            time.sleep(5)
            while not watch.closed:
                try:
                    running = setup_lib.game_is_running(root, unknown=True)
                except (OSError, ValueError, subprocess.SubprocessError):
                    running = True
                self.messages.put((status.set, watch.poll(running)))
                time.sleep(3)
        threading.Thread(target=loop, daemon=True).start()

    def show_join(self):
        ttk, tk = self.ttk, self.tk
        body = self.clear("Join a game", "Paste the join code (or the address) your host sent you.")
        target, password = tk.StringVar(), tk.StringVar()
        ttk.Entry(self.row(body, "Join code", 12), textvariable=target).pack(side="left", fill="x", expand=True)
        ttk.Entry(self.row(body, "Password", 12), textvariable=password, show="*").pack(side="left", fill="x", expand=True)
        status = tk.StringVar()

        def join():
            name = play.clean_name(self.name.get())
            secret = play.clean_password(password.get())
            endpoint = network.parse_endpoint(target.get())
            status.set(f"Checking {endpoint}…")
            button.configure(state="disabled")   # 1.1.8 (L14.10): a second click started a second check and start

            def enable():
                if button.winfo_exists():
                    button.configure(state="normal")

            def ready(_):
                try:
                    args, config = play.join_launch(self.root, name=name, password=secret, endpoint=str(endpoint),
                                                    video=self.video())
                except ValueError as error:
                    refused(error)
                    return
                self.launch(args, config,
                            lambda: (enable(), status.set("Joining. The game window opens in a moment.")),
                            lambda: (enable(), status.set("")))

            def refused(error):
                enable()
                status.set("")
                self.error(error)
            self.work(lambda: play.check_host(network.query_info(endpoint, timeout=3), password=secret,
                                              maps=self.maps_tag()), ready, refused)
        buttons = ttk.Frame(body)
        buttons.pack(anchor="w", pady=18)
        button = ttk.Button(buttons, text="Join", command=self.guard(join), style="Accent.TButton")
        button.pack(side="left")
        ttk.Button(buttons, text="Back", command=lambda: self.show_main(self.root)).pack(side="left", padx=8)
        ttk.Label(body, textvariable=status, style="Hint.TLabel", wraplength=740).pack(anchor="w")


def open_window(payload, exe, copy=None, update_from=None, wait=()):
    import tkinter as tk
    dpi_aware()
    window = tk.Tk()
    app = App(window, payload, exe)
    theme.dark_title_bar(window)
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


def self_test(payload) -> int:
    import tkinter
    assert tkinter.Tcl().eval("info patchlevel")
    if getattr(sys, "frozen", False):
        assert setup_lib.inspect_payload(payload)["build"] == play.BUILD
    assert setup_lib.merge_groups.load_groups(), "the merged-map groups are missing"
    args, config = play.host_launch(Path("copy"), solo=True, name="Test", map_name="c_intro", players=4, password="",
                                    companions=3, port=play.DEFAULT_PORT, cont=False, video=[])
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
