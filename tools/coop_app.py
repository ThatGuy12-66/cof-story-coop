"""Cry of Fear story co-op: the tester app. Setup on first run, then Play solo, Host or Join."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time
import webbrowser

if not getattr(sys, "frozen", False):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cofcoop import network
from tools import coop_play as play
from tools import friend_setup as setup_lib

DEFAULT_COPY = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "CoF-Story-Coop"


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


def where_to_start(exe_dir: Path, default_copy: Path, build: int):
    """'main' (a copy to play from), 'update' (an older copy), 'newer' (a copy newer than this app) or 'setup'."""
    if setup_lib.installed_state(exe_dir):
        return "main", Path(exe_dir)
    state = setup_lib.installed_state(default_copy)
    if not state:
        return "setup", None
    if state["build"] < build:
        return "update", Path(default_copy)
    if state["build"] > build:
        return "newer", Path(default_copy)
    return "main", Path(default_copy)


class App:
    def __init__(self, window, payload, exe):
        import tkinter as tk
        from tkinter import ttk
        self.tk, self.ttk = tk, ttk
        self.window, self.payload, self.exe = window, payload, exe
        self.root = None
        self.settings = {}
        self.watch = None
        self.messages = queue.Queue()
        window.title("Cry of Fear Co-op")
        window.geometry("760x560")
        window.minsize(660, 500)
        self.body = ttk.Frame(window, padding=20)
        self.body.pack(fill="both", expand=True)
        foot = ttk.Frame(window, padding=(20, 0, 20, 12))
        foot.pack(fill="x", side="bottom")
        ttk.Label(foot, text=f"Story co-op {play.VERSION_LABEL} · early test build").pack(side="left")
        ttk.Button(foot, text="Report a bug", command=lambda: webbrowser.open(play.ISSUES_URL)).pack(side="right")
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
        bar = ttk.Progressbar(body, maximum=100)
        bar.pack(fill="x")
        status = tk.StringVar(value="Looking for Cry of Fear in Steam…")
        ttk.Label(body, textvariable=status, wraplength=700).pack(anchor="w", pady=8)

        def found(candidates):
            if candidates and not source.get():
                source.set(str(candidates[0]))
            status.set("Ready: click Set up." if source.get() else
                       "Pick your Cry of Fear folder (the one with CoFLaunchApp.exe).")

        def show_progress(value):
            status.set(value[0])
            bar.configure(value=value[1] * 100)

        def stopped(error):
            button.configure(state="normal")
            status.set("Setup stopped. Your Steam game is unchanged.")
            self.error(error)

        def start():
            button.configure(state="disabled")
            src, dst = Path(source.get()), Path(destination.get())
            self.work(lambda: setup_lib.setup(src, dst, self.payload, exe=self.exe,
                                              progress=lambda text, part: self.messages.put((show_progress, (text, part)))),
                      self.show_main, stopped)
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

    def show_update(self, copy):
        ttk = self.ttk
        body = self.clear("Update story co-op", f"{play.VERSION_LABEL} is ready to install over your copy in {copy}. "
                          "Your saves and settings are kept.")
        status = self.tk.StringVar()
        button = ttk.Button(body, text="Update")
        button.pack(anchor="w", pady=14)
        ttk.Label(body, textvariable=status, wraplength=700).pack(anchor="w")

        def stopped(error):
            button.configure(state="normal")
            self.error(error)

        def start():
            button.configure(state="disabled")
            self.work(lambda: setup_lib.update(copy, self.payload, exe=self.exe,
                                               progress=lambda text, _: self.messages.put((status.set, text))),
                      self.show_main, stopped)
        button.configure(command=start)

    # main
    def show_main(self, copy):
        ttk = self.ttk
        self.root = Path(copy)
        self.settings = play.load_settings(self.root)
        body = self.clear("Cry of Fear co-op", "Play the campaign together, or alone with an AI companion.")
        holder = self.row(body, "Your name", 12)
        self.name = self.tk.StringVar(value=self.settings["name"])
        ttk.Entry(holder, textvariable=self.name, width=28).pack(side="left")
        ttk.Frame(body, height=12).pack()
        for text, action in (("Play solo (with the AI companion)", lambda: self.show_start(solo=True)),
                             ("Host a game", lambda: self.show_start(solo=False)),
                             ("Join a game", self.show_join)):
            ttk.Button(body, text=text, command=action).pack(fill="x", ipady=8, pady=4)
        ttk.Label(body, text="In the game: J joins your partner, Enter skips a cutscene for everyone, "
                  "H tells the AI companion to wait or follow.", wraplength=700).pack(anchor="w", pady=(14, 0))

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
        text = f"Continue: {checkpoint.map} (experimental)" if checkpoint else "Continue (no co-op checkpoint yet)"
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
                          "You and the AI companion, on this PC only." if solo else
                          "Friends join you with the join code shown after the game starts.")
        resolve = self.start_choice(body)
        players = tk.StringVar(value=str(self.settings["players"]))
        password = tk.StringVar(value=self.settings["password"])
        companion = tk.BooleanVar(value=self.settings["companion"])
        port = tk.StringVar(value=str(self.settings["port"]))
        bonus = tk.BooleanVar(value=self.settings["bonus_room"])
        ttk.Checkbutton(body, text="Bonus room unlocked (FAMAS, axe and more; in Chapter 1 and the bridge)",
                        variable=bonus).pack(anchor="w", pady=(8, 0))
        if not solo:
            ttk.Frame(body, height=8).pack()
            ttk.Spinbox(self.row(body, "Players", 12), from_=2, to=4, textvariable=players, width=5).pack(side="left")
            ttk.Entry(self.row(body, "Password", 12), textvariable=password, show="*", width=28).pack(side="left")
            ttk.Label(self.row(body, "", 12), text="(optional: friends need it to join)").pack(side="left")
            ttk.Entry(self.row(body, "Port (UDP)", 12), textvariable=port, width=8).pack(side="left")
            ttk.Checkbutton(body, text="AI companion until a friend takes its place", variable=companion).pack(anchor="w", pady=4)

        def start():
            from tkinter import messagebox
            name = play.clean_name(self.name.get())
            map_name, checkpoint = resolve()
            if checkpoint:
                warning = play.continue_warning(checkpoint, name)
                if warning and not messagebox.askokcancel("Continue", warning, parent=self.window):
                    return
            count = checkpoint.players if checkpoint else 2 if solo else play.clean_players(players.get())
            number = play.DEFAULT_PORT if solo else play.clean_port(port.get())
            args, config = play.host_launch(self.root, solo=solo, name=name, map_name=map_name, players=count,
                                            password=password.get(), companion=companion.get(), port=number,
                                            cont=checkpoint is not None, video=self.video(), bonus_room=bonus.get())
            self.launch(args, config)
            self.remember(bonus_room=bonus.get())
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


def open_window(payload, exe, copy=None):
    import tkinter as tk
    dpi_aware()
    window = tk.Tk()
    app = App(window, payload, exe)
    if copy:
        app.show_main(copy)
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
    args, config = play.host_launch(Path("copy"), solo=True, name="Test", map_name="c_intro", players=2, password="",
                                    companion=False, port=play.DEFAULT_PORT, cont=False, video=[])
    assert "cof_build " + play.VERSION in config and "-console" not in args
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--install", nargs=2, metavar=("SOURCE", "DEST"), help="set up a copy without the window")
    parser.add_argument("--update", metavar="COPY", help="update a copy without the window")
    parser.add_argument("--payload", help="the engine payload folder, for a run from source")
    parser.add_argument("--copy", help="open the main window for this copy")
    parser.add_argument("--no-shortcuts", action="store_true")
    args = parser.parse_args(argv)
    try:
        payload = payload_dir(args.payload) if (args.payload or getattr(sys, "frozen", False)) else None
        if args.self_test:
            return self_test(payload)
        if args.install or args.update:
            if payload is None:
                return 2
            if args.install:
                setup_lib.setup(Path(args.install[0]), Path(args.install[1]), payload, exe=this_exe(),
                                shortcuts=not args.no_shortcuts)
            else:
                setup_lib.update(Path(args.update), payload, exe=this_exe(), shortcuts=not args.no_shortcuts)
            return 0
        open_window(payload, this_exe(), args.copy)
        return 0
    except (OSError, ValueError, RuntimeError, AssertionError) as error:
        if sys.stderr:
            print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
