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
        ttk.Label(row, textvariable=self.elapsed).pack(side="right")
        ttk.Label(row, textvariable=self.status, wraplength=580).pack(side="left", anchor="w")
        frame = ttk.Frame(parent)
        frame.pack(fill="both", expand=True)
        self.box = tk.Text(frame, height=9, wrap="word", font=("Consolas", 9), state="disabled")
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
        self.messages = queue.Queue()
        window.title("Cry of Fear Co-op")
        window.geometry("760x560")
        window.minsize(660, 500)
        self.body = ttk.Frame(window, padding=20)
        self.body.pack(fill="both", expand=True)
        foot = ttk.Frame(window, padding=(20, 0, 20, 12))
        foot.pack(fill="x", side="bottom")
        ttk.Label(foot, text=footer_text()).pack(side="left")
        ttk.Button(foot, text="Report a bug", command=report_bug).pack(side="right")
        ttk.Button(foot, text="Open logs folder", command=self.guard(self.open_logs)).pack(side="right", padx=6)
        window.protocol("WM_DELETE_WINDOW", self.close)
        window.after(100, self.pump)

    # plumbing
    def clear(self, title, subtitle=""):
        for child in self.body.winfo_children():
            child.destroy()
        self.ttk.Label(self.body, text=title, font=("Segoe UI", 18, "bold")).pack(anchor="w")
        if subtitle:
            self.ttk.Label(self.body, text=subtitle, wraplength=700).pack(anchor="w", pady=(4, 12))
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
            handler(value)
        self.window.after(100, self.pump)

    def error(self, error):
        from tkinter import messagebox
        messagebox.showerror("Cry of Fear Co-op", str(error), parent=self.window)

    def guard(self, action):
        def run(*_):
            try:
                action()
            except (OSError, ValueError, RuntimeError, network.NetworkError) as error:
                self.error(error)
        return run

    def close(self):
        from tkinter import messagebox
        warning = play.close_warning(self.watch)
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
        self.ttk.Label(holder, text=label, width=width).pack(side="left")
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
        button = ttk.Button(body, text="Set up")
        button.pack(anchor="w", pady=14)
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
            button.configure(state="normal")
            panel.status.set("Setup stopped. Your Steam game is unchanged.")
            self.error(error)

        def made(copy):
            panel.stop()
            remember_copy(copy)
            self.show_main(copy)

        def start():
            button.configure(state="disabled")
            panel.start()
            src, dst = Path(source.get()), Path(destination.get())
            self.work(lambda: setup_lib.setup(src, dst, self.payload, exe=self.exe, progress=self.reporter(panel)),
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

    def show_update(self, copy, auto=False, wait=()):
        """auto: started by the previous version's "Update now", which is closing: wait for it, then update."""
        ttk = self.ttk
        body = self.clear("Update story co-op", f"Version {play.VERSION_LABEL} is ready to install over your copy "
                          f"in {copy}. Your saves and settings are kept.")
        button = ttk.Button(body, text="Update")
        button.pack(anchor="w", pady=14)
        panel = self.progress_panel(body)
        report = self.reporter(panel)

        def stopped(error):
            panel.stop()
            button.configure(state="normal")
            self.error(error)

        def updated(root):
            panel.stop()
            self.show_main(root)

        def job():
            if wait:
                report("Waiting for the old version to close…", 0, stamped("Waiting for the old version to close"))
                app_update.wait_for_exit(list(wait))
            return setup_lib.update(copy, self.payload, exe=self.exe, progress=report)

        def start():
            button.configure(state="disabled")
            panel.start()
            self.work(job, updated, stopped)
        button.configure(command=start)
        if auto:
            start()

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
        box = ttk.Frame(slot, padding=(0, 0, 0, 10))
        box.pack(fill="x")
        ttk.Label(box, text=f"Version {release.label} is out.", font=("Segoe UI", 11, "bold")).pack(side="left")
        ttk.Button(box, text="What's new", command=lambda: webbrowser.open(release.page_url)).pack(side="right")
        ttk.Button(box, text="Update now", command=self.guard(self.update_now)).pack(side="right", padx=6)

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

    # main
    def show_main(self, copy):
        ttk = self.ttk
        self.root = Path(copy)
        self.settings = play.load_settings(self.root)
        body = self.clear("Cry of Fear co-op", "Play the campaign together, or alone with AI companions.")
        self.update_slot = ttk.Frame(body)
        self.update_slot.pack(fill="x")
        self.show_offer()
        holder = self.row(body, "Your name", 12)
        self.name = self.tk.StringVar(value=self.settings["name"])
        ttk.Entry(holder, textvariable=self.name, width=28).pack(side="left")
        ttk.Frame(body, height=12).pack()
        for text, action in (("Play solo (with AI companions)", lambda: self.show_start(solo=True)),
                             ("Host a game", lambda: self.show_start(solo=False)),
                             ("Join a game", self.show_join)):
            ttk.Button(body, text=text, command=action).pack(fill="x", ipady=8, pady=4)
        ttk.Label(body, text="In the game: J joins your partner, Enter skips a cutscene for everyone, "
                  "H tells the AI companions to wait or follow.", wraplength=700).pack(anchor="w", pady=(14, 0))
        self.check_for_update()

    def remember(self, **values):
        self.settings.update(values)
        play.save_settings(self.root, self.settings)

    def launch(self, args, config):
        setup_lib.validate_runtime(self.root)
        if setup_lib.game_is_running(self.root):
            raise ValueError("Cry of Fear is already running from this copy. Close it first.")
        (self.root / "cryoffear" / play.STORY_CFG).write_text(play.story_config(), encoding="ascii")
        (self.root / "cryoffear" / play.SESSION_CFG).write_text(config, encoding="ascii")
        setup_lib.keep_previous_log(self.root, play.LOG)
        start_game(self.root, args)
        self.remember(name=self.name.get().strip(), first_launch_done=True)

    def video(self):
        return play.video_flags(not self.settings["first_launch_done"], desktop_size())

    def start_choice(self, body):
        ttk, tk = self.ttk, self.tk
        checkpoint = play.latest_checkpoint(self.root)
        kind = tk.StringVar(value="new")
        ttk.Radiobutton(body, text="New game (the intro; Enter skips cutscenes)", variable=kind, value="new").pack(anchor="w")
        holder = ttk.Frame(body)
        holder.pack(anchor="w", fill="x")
        ttk.Radiobutton(holder, text="Chapter:", variable=kind, value="chapter").pack(side="left")
        chapter = ttk.Combobox(holder, values=[label for label, _ in play.CHAPTER_CHOICES], state="readonly", width=44)
        chapter.current(1)
        chapter.pack(side="left", padx=6)
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
        resolve = self.start_choice(body)
        players = tk.StringVar(value=str(self.settings["players"]))
        password = tk.StringVar(value=self.settings["password"])
        companion = tk.BooleanVar(value=self.settings["companion"])
        companions = tk.StringVar(value=str(self.settings["companions"]))
        port = tk.StringVar(value=str(self.settings["port"]))
        bonus = tk.BooleanVar(value=self.settings["bonus_room"])
        ttk.Checkbutton(body, text="Bonus room unlocked (FAMAS, axe and more; in Chapter 1 and the bridge)",
                        variable=bonus).pack(anchor="w", pady=(8, 0))
        merged = tk.BooleanVar(value=self.settings["merged_maps"])
        ttk.Checkbutton(body, text="Merged maps (fewer loading screens; Continue keeps the checkpoint's own)",
                        variable=merged).pack(anchor="w")
        torch = tk.BooleanVar(value=self.settings["coop_flashlight"])
        ttk.Checkbutton(body, text="Co-op flashlight on F (a free torch for everyone; the original has only Simon's phone)",
                        variable=torch).pack(anchor="w")
        if solo:
            ttk.Frame(body, height=8).pack()
            ttk.Spinbox(self.row(body, "AI companions", 14), from_=1, to=play.MAX_COMPANIONS, textvariable=companions,
                        width=5, state="readonly").pack(side="left")
        if not solo:
            ttk.Frame(body, height=8).pack()
            ttk.Spinbox(self.row(body, "Players", 12), from_=2, to=4, textvariable=players, width=5).pack(side="left")
            ttk.Entry(self.row(body, "Password", 12), textvariable=password, show="*", width=28).pack(side="left")
            ttk.Label(self.row(body, "", 12), text="(optional: friends need it to join)").pack(side="left")
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
            count, ais = play.party_size(solo=solo, players=2 if solo else play.clean_players(players.get()),
                                         companions=ais, fill=companion.get(),
                                         checkpoint_players=checkpoint.players if checkpoint else None)
            if solo and checkpoint and ais < int(companions.get() or 1):
                messagebox.showinfo("Continue", f"This checkpoint was saved with {count} places, so {ais} AI "
                                    f"{'companion comes' if ais == 1 else 'companions come'} along this time.",
                                    parent=self.window)
            number = play.DEFAULT_PORT if solo else play.clean_port(port.get())
            args, config = play.host_launch(self.root, solo=solo, name=name, map_name=map_name, players=count,
                                            password=password.get(), companions=ais, port=number,
                                            cont=checkpoint is not None, video=self.video(), bonus_room=bonus.get(),
                                            merged=play.merged_for_start(checkpoint, checkbox=merged.get()),
                                            coop_flashlight=torch.get())
            self.launch(args, config)
            self.remember(bonus_room=bonus.get(), merged_maps=merged.get(), coop_flashlight=torch.get())
            if solo and not checkpoint:
                self.remember(companions=ais)
            if not solo:
                self.remember(players=self.settings["players"] if checkpoint else count, password=password.get(),
                              companion=companion.get(), port=number)
            self.show_running(play.HostWatch(number, lan_only=solo), solo)
        buttons = ttk.Frame(body)
        buttons.pack(anchor="w", pady=16)
        ttk.Button(buttons, text="Start", command=self.guard(start)).pack(side="left")
        ttk.Button(buttons, text="Back", command=lambda: self.show_main(self.root)).pack(side="left", padx=6)

    def show_running(self, watch, solo):
        ttk, tk = self.ttk, self.tk
        self.watch = watch
        body = self.clear("Playing solo" if solo else "Hosting",
                          "Keep this window open while you play." if solo else
                          "Send your friends the join code (and the password, if you set one). "
                          "Keep this window open while you host.")
        status = tk.StringVar(value="Starting the game...")
        ttk.Label(body, textvariable=status, wraplength=700).pack(anchor="w", pady=4)
        if not solo:
            code, address, lan, router = tk.StringVar(value="(looking…)"), tk.StringVar(), tk.StringVar(), tk.StringVar()
            for label, var in (("Join code", code), ("Internet address", address), ("Same place (LAN)", lan)):
                holder = self.row(body, label)
                ttk.Entry(holder, textvariable=var, state="readonly").pack(side="left", fill="x", expand=True)
                ttk.Button(holder, text="Copy", command=lambda v=var: (self.window.clipboard_clear(),
                                                                      self.window.clipboard_append(v.get()))).pack(side="left", padx=4)
            ttk.Label(body, textvariable=router, wraplength=700).pack(anchor="w", pady=8)

            def routed(_):
                code.set(watch.code or "(no Internet address found: use the LAN or VPN address)")
                address.set(watch.address)
                router.set(watch.router)
            self.work(watch.open_router, routed, lambda error: router.set(str(error)))
            self.work(network.local_addresses,
                      lambda rows: lan.set(f"{rows[0]['address']}:{watch.port}" if rows else ""), lambda _: None)
        ttk.Button(body, text="Back", command=lambda: self.show_main(self.root)).pack(anchor="w", pady=12)
        root = self.root

        def loop():
            time.sleep(5)
            while not watch.closed:
                try:
                    running = setup_lib.game_is_running(root)
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

            def ready(_):
                args, config = play.join_launch(self.root, name=name, password=secret, endpoint=str(endpoint),
                                                video=self.video())
                self.guard(lambda: (self.launch(args, config),
                                    status.set("Joining. The game window opens in a moment.")))()

            def refused(error):
                status.set("")
                self.error(error)
            self.work(lambda: play.check_host(network.query_info(endpoint, timeout=3), password=secret), ready, refused)
        buttons = ttk.Frame(body)
        buttons.pack(anchor="w", pady=16)
        ttk.Button(buttons, text="Join", command=self.guard(join)).pack(side="left")
        ttk.Button(buttons, text="Back", command=lambda: self.show_main(self.root)).pack(side="left", padx=6)
        ttk.Label(body, textvariable=status, wraplength=700).pack(anchor="w")


def open_window(payload, exe, copy=None, update_from=None, wait=()):
    import tkinter as tk
    dpi_aware()
    window = tk.Tk()
    app = App(window, payload, exe)
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
        else:
            app.show_main(found)
            if mode == "newer":
                from tkinter import messagebox
                messagebox.showinfo("Cry of Fear Co-op", "Your copy is newer than this package. "
                                    "Use the Cry of Fear Co-op shortcut.", parent=window)
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
