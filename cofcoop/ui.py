"""Desktop controls for retail Cry of Fear story co-op experiments."""
from __future__ import annotations

from datetime import datetime, timezone
from pathlib import Path
import queue
import secrets
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from . import core, network


class Launcher(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Cry of Fear • Story Co-op")
        self.geometry("900x750")
        self.minsize(800, 680)
        self.jobs = queue.Queue()
        self.catalog = core.load_catalog()
        self.settings_store = core.SettingsStore()
        self.progress_store = core.ProgressStore()
        self.settings = self.settings_store.load()
        self.progress = self.progress_store.load()
        self.proc = None
        self.tracker = None
        self.host_endpoint = None
        self.query_pending = False
        self.mapping = None
        self.mapping_pending = False
        self.active_game_dir = None
        self.session_epoch = 0
        self.process_role = "game/server"
        self.active_lan = None
        self.current_map = ""
        self.game_dir = tk.StringVar(value=self.settings.get("game_dir", str(core.DEFAULT_GAME_DIR)))
        self.player_name = tk.StringVar(value=self.settings.get("name", "Player"))
        self.port = tk.StringVar(value=str(self.settings.get("port", 27015)))
        self.players = tk.StringVar(value=str(self.settings.get("maxplayers", 4)))
        self.lan = tk.BooleanVar(value=self.settings.get("lan", True))
        self.dedicated = tk.BooleanVar(value=False)
        self.password = tk.StringVar(value=self.settings.get("password", ""))
        self.rcon_password = tk.StringVar(value=self.settings.get("rcon_password") or secrets.token_hex(16))
        self.map_name = tk.StringVar(value=self.settings.get("map_name", "c_intro"))
        self.chapter = tk.StringVar(value=next((label for label, name in core.CHAPTERS if name == self.map_name.get()), core.CHAPTERS[0][0]))
        self.join_address = tk.StringVar(value=self.settings.get("join_address", ""))
        self.join_password = tk.StringVar()
        self.share_address = tk.StringVar()
        self.status = tk.StringVar(value="Ready • Stock story co-op is experimental")
        self.live_map = tk.StringVar(value="No host connected")
        self.checkpoint = tk.StringVar(value=self._checkpoint_text())
        self._build()
        self.protocol("WM_DELETE_WINDOW", self.close)
        self.after(150, self._drain)
        self.after(2500, self._tick)

    def _checkpoint_text(self):
        checkpoint = self.progress.get("map", "")
        return f"Map checkpoint: {checkpoint or 'none'} • inventory and puzzle state are not saved"

    def _build(self):
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("TFrame", background="#202329")
        style.configure("TLabel", background="#202329", foreground="#edf0f2", font=("Segoe UI", 10))
        style.configure("Title.TLabel", font=("Segoe UI Semibold", 22))
        style.configure("Hint.TLabel", foreground="#b6bcc5", font=("Segoe UI", 9))
        style.configure("TButton", padding=(10, 7), font=("Segoe UI", 10))
        style.configure("TCheckbutton", background="#202329", foreground="#edf0f2", font=("Segoe UI", 10))
        self.configure(background="#202329")
        wrapper = ttk.Frame(self, padding=20)
        wrapper.pack(fill="both", expand=True)
        ttk.Label(wrapper, text="CRY OF FEAR", style="Title.TLabel").pack(anchor="w")
        ttk.Label(wrapper, text="Story campaign • 2–4 players • development copy", style="Hint.TLabel").pack(anchor="w", pady=(0, 16))
        setup = ttk.Frame(wrapper)
        setup.pack(fill="x", pady=(0, 12))
        setup.columnconfigure(1, weight=1)
        ttk.Label(setup, text="Game folder").grid(row=0, column=0, sticky="w", padx=(0, 10))
        ttk.Entry(setup, textvariable=self.game_dir).grid(row=0, column=1, sticky="ew")
        ttk.Button(setup, text="Browse…", command=self.browse).grid(row=0, column=2, padx=(8, 0))
        ttk.Button(setup, text="Find installs", command=self.find_installs).grid(row=1, column=2, padx=(8, 0))
        ttk.Label(setup, text="Player name").grid(row=1, column=0, sticky="w", pady=8)
        ttk.Entry(setup, textvariable=self.player_name, width=24).grid(row=1, column=1, sticky="w")
        notebook = ttk.Notebook(wrapper)
        notebook.pack(fill="both", expand=True)
        host = ttk.Frame(notebook, padding=18)
        join = ttk.Frame(notebook, padding=18)
        controls = ttk.Frame(notebook, padding=18)
        sharing = ttk.Frame(notebook, padding=18)
        for page, label in [(host, "Host"), (join, "Join"), (controls, "Host controls"), (sharing, "Network & invite")]:
            notebook.add(page, text=label)
        self._host_page(host)
        self._join_page(join)
        self._controls_page(controls)
        self._network_page(sharing)
        ttk.Label(wrapper, textvariable=self.status, wraplength=810).pack(anchor="w", pady=(12, 6))
        self.output = tk.Text(wrapper, height=6, wrap="word", bg="#15171b", fg="#c6ced6", relief="flat", font=("Consolas", 9))
        self.output.pack(fill="x")
        self.output.configure(state="disabled")

    def _host_page(self, page):
        page.columnconfigure(1, weight=1)
        ttk.Label(page, text="Chapter").grid(row=0, column=0, sticky="w", padx=(0, 14))
        chapters = ttk.Combobox(page, state="readonly", textvariable=self.chapter, values=[label for label, _ in core.CHAPTERS])
        chapters.grid(row=0, column=1, columnspan=3, sticky="ew", pady=5)
        chapters.bind("<<ComboboxSelected>>", lambda _: self.map_name.set(dict(core.CHAPTERS)[self.chapter.get()]))
        ttk.Label(page, text="Start map").grid(row=1, column=0, sticky="w")
        ttk.Combobox(page, textvariable=self.map_name, values=sorted(self.catalog)).grid(row=1, column=1, columnspan=3, sticky="ew", pady=5)
        ttk.Label(page, text="UDP port").grid(row=2, column=0, sticky="w")
        ttk.Entry(page, textvariable=self.port, width=10).grid(row=2, column=1, sticky="w", pady=5)
        ttk.Label(page, text="Players").grid(row=2, column=2, padx=(15, 8))
        ttk.Combobox(page, state="readonly", textvariable=self.players, values=[2, 3, 4], width=5).grid(row=2, column=3, sticky="w")
        ttk.Label(page, text="Server password").grid(row=3, column=0, sticky="w")
        ttk.Entry(page, textvariable=self.password, show="•").grid(row=3, column=1, columnspan=3, sticky="ew", pady=5)
        ttk.Checkbutton(page, variable=self.lan, text="LAN only (untick for public hosting, Tailscale, or ZeroTier)").grid(row=4, column=0, columnspan=4, sticky="w", pady=8)
        ttk.Checkbutton(page, variable=self.dedicated, text="Dedicated server (hlds.exe; experimental)").grid(row=5, column=0, columnspan=4, sticky="w")
        actions = ttk.Frame(page)
        actions.grid(row=6, column=0, columnspan=4, sticky="w", pady=15)
        ttk.Button(actions, text="Start co-op", command=self.host).pack(side="left", padx=(0, 10))
        ttk.Button(actions, text="Resume map checkpoint", command=self.resume).pack(side="left")
        ttk.Label(page, textvariable=self.checkpoint, style="Hint.TLabel", wraplength=740).grid(row=7, column=0, columnspan=4, sticky="w")
        ttk.Label(page, text="Chapter jumps and map warps can miss required story items. Natural story transitions, cutscenes, and shared items still need testing.", style="Hint.TLabel", wraplength=740).grid(row=8, column=0, columnspan=4, sticky="w", pady=8)

    def _join_page(self, page):
        ttk.Label(page, text="Paste an address or a COF1 join code").pack(anchor="w", pady=(0, 8))
        ttk.Entry(page, textvariable=self.join_address).pack(fill="x")
        ttk.Label(page, text="Examples: 192.168.1.20:27015 or a friend's Tailscale address", style="Hint.TLabel").pack(anchor="w", pady=8)
        ttk.Label(page, text="Server password (if required)").pack(anchor="w")
        ttk.Entry(page, textvariable=self.join_password, show="•").pack(fill="x", pady=5)
        row = ttk.Frame(page)
        row.pack(anchor="w", pady=12)
        ttk.Button(row, text="Check server", command=lambda: self.check_join(False)).pack(side="left", padx=(0, 10))
        ttk.Button(row, text="Check & join", command=lambda: self.check_join(True)).pack(side="left")
        ttk.Label(page, text="Each player needs their own Steam installation and local game copy. A join code shares an address; it does not create a relay or bypass NAT.", style="Hint.TLabel", wraplength=730).pack(anchor="w", pady=12)

    def _controls_page(self, page):
        ttk.Label(page, textvariable=self.live_map).pack(anchor="w", pady=(0, 8))
        ttk.Button(page, text="Connect to local host / refresh", command=self.attach).pack(anchor="w")
        ttk.Label(page, text="Available exits from the current map", style="Hint.TLabel").pack(anchor="w", pady=(12, 5))
        self.exits = ttk.Frame(page)
        self.exits.pack(fill="x")
        row = ttk.Frame(page)
        row.pack(fill="x", pady=16)
        ttk.Combobox(row, textvariable=self.map_name, values=sorted(self.catalog)).pack(side="left", fill="x", expand=True)
        ttk.Button(row, text="Warp to map", command=lambda: self.warp(self.map_name.get())).pack(side="left", padx=10)
        ttk.Label(page, text="RCON password", style="Hint.TLabel").pack(anchor="w")
        ttk.Entry(page, textvariable=self.rcon_password, show="•").pack(fill="x", pady=5)
        ttk.Label(page, text="Exit buttons send changelevel. They do not preserve single-player landmark placement or hub state. Only use them when your group is ready.", style="Hint.TLabel", wraplength=730).pack(anchor="w", pady=8)

    def _network_page(self, page):
        row = ttk.Frame(page)
        row.pack(fill="x")
        ttk.Button(row, text="Find local addresses", command=self.find_local).pack(side="left", padx=(0, 8))
        ttk.Button(row, text="Find public address", command=self.find_public).pack(side="left")
        self.address_list = tk.Listbox(page, height=4, bg="#15171b", fg="#edf0f2", relief="flat", exportselection=False)
        self.address_list.pack(fill="x", pady=10)
        self.address_list.bind("<<ListboxSelect>>", self._select_address)
        ttk.Entry(page, textvariable=self.share_address).pack(fill="x")
        ttk.Button(page, text="Copy join code", command=self.copy_code).pack(anchor="w", pady=8)
        row = ttk.Frame(page)
        row.pack(anchor="w", pady=6)
        ttk.Button(row, text="Open UDP port with UPnP", command=self.map_port).pack(side="left", padx=(0, 8))
        ttk.Button(row, text="Remove my UPnP mapping", command=self.unmap_port).pack(side="left")
        ttk.Label(page, text="Public hosting requires an open UDP port and Windows Firewall access. UPnP depends on your router. Tailscale / ZeroTier require everyone to be on the same virtual network.", style="Hint.TLabel", wraplength=730).pack(anchor="w", pady=8)

    def log(self, text):
        self.output.configure(state="normal")
        self.output.insert("end", str(text) + "\n")
        self.output.see("end")
        self.output.configure(state="disabled")

    def work(self, label, action, callback=None, *, quiet=False):
        if not quiet:
            self.status.set(label + "…")
        def run():
            try:
                self.jobs.put((callback, action(), None, quiet))
            except Exception as exc:
                self.jobs.put((None, None, str(exc), quiet))
        threading.Thread(target=run, daemon=True).start()

    def _drain(self):
        while True:
            try:
                callback, result, error, quiet = self.jobs.get_nowait()
            except queue.Empty:
                break
            if error:
                self.query_pending = False
                self.mapping_pending = False
                if not quiet:
                    self.status.set(error)
                    self.log(error)
                elif self.host_endpoint:
                    self.status.set("Waiting for host response • " + error)
            elif callback:
                try:
                    callback(result)
                except Exception as exc:
                    self.status.set(str(exc))
                    self.log(exc)
        self.after(150, self._drain)

    def _save_settings(self):
        self.settings.update(game_dir=self.game_dir.get(), name=self.player_name.get(),
                             port=int(self.port.get()), maxplayers=int(self.players.get()),
                             lan=self.lan.get(), password=self.password.get(),
                             rcon_password=self.rcon_password.get(), map_name=self.map_name.get(),
                             join_address=self.join_address.get())
        self.settings_store.save(self.settings)

    def browse(self):
        directory = filedialog.askdirectory(parent=self, title="Choose the development copy containing cof.exe")
        if directory:
            self.game_dir.set(directory)

    def find_installs(self):
        def found(paths):
            for path in paths:
                self.log(str(path))
            self.status.set("Install locations listed below. Launch from the C: development copy.")
        self.work("Finding installs", core.discover_installs, found)

    def host(self):
        try:
            opts = core.LaunchOptions(game_dir=Path(self.game_dir.get()), name=self.player_name.get(),
                port=int(self.port.get()), maxplayers=int(self.players.get()), map_name=self.map_name.get(),
                lan=self.lan.get(), dedicated=self.dedicated.get(),
                rcon_password=self.rcon_password.get(), password=self.password.get())
            self._save_settings()
            tracker = core.LogTracker(Path(self.game_dir.get()) / "qconsole.log", start_at_end=True)
            endpoint = network.Endpoint("127.0.0.1", opts.port)
            self.log(f"Opening {'dedicated server' if opts.dedicated else 'game'} on {opts.map_name}. The launcher will leave it running.")
            self.work("Starting co-op", lambda: core.launch_game(opts), lambda proc: self._started(proc, endpoint, opts.game_dir, tracker, opts.lan))
        except Exception as exc:
            self.status.set(str(exc))

    def _started(self, proc, endpoint, game_dir, tracker=None, lan=None):
        self.proc = proc
        self.process_role = "host"
        self.session_epoch += 1
        self.query_pending = False
        self.active_lan = lan
        self.tracker = tracker
        self.current_map = ""
        self.active_game_dir = Path(game_dir).resolve()
        self.host_endpoint = endpoint
        self.status.set("Process started; waiting for the server to answer")

    def resume(self):
        name = self.progress.get("map")
        if not name:
            self.status.set("No confirmed map checkpoint has been recorded yet")
            return
        if self.progress.get("game_dir") != str(Path(self.game_dir.get()).resolve()):
            self.status.set("The checkpoint belongs to a different game folder")
            return
        self.map_name.set(name)
        self.host()

    def check_join(self, launch):
        try:
            endpoint = network.parse_endpoint(self.join_address.get())
            game_dir, name = Path(self.game_dir.get()), self.player_name.get()
            password = self.join_password.get()
            self._save_settings()
            def checked(info):
                self.log(f"{info.get('name')} • {info.get('map')} • {info.get('players')}/{info.get('max_players')}")
                self.status.set("Server responded")
                if launch:
                    self.log("Opening the game to join " + str(endpoint))
                    self.work("Joining server", lambda: core.launch_join(game_dir, name, str(endpoint), password=password), self._joined)
            self.work("Checking server", lambda: network.query_server(endpoint), checked)
        except Exception as exc:
            self.status.set(str(exc))

    def _joined(self, proc):
        self.proc = proc
        self.process_role = "join client"
        self.host_endpoint = None
        self.tracker = None
        self.session_epoch += 1
        self.query_pending = False
        self.active_lan = None
        self.status.set(f"Join process started (PID {proc.pid})")

    def attach(self):
        try:
            endpoint = network.Endpoint("127.0.0.1", int(self.port.get()))
            game_dir = core.validate_game_dir(Path(self.game_dir.get()))
            self.work("Checking local host", lambda: network.query_server(endpoint), lambda info: self._attached(endpoint, info, game_dir))
        except Exception as exc:
            self.status.set(str(exc))

    def _attached(self, endpoint, info, game_dir):
        self.host_endpoint = endpoint
        # A2S identifies an endpoint, not its executable path. Manual attachment
        # supports controls but cannot associate a persistent checkpoint safely.
        self.active_game_dir = None
        self.tracker = None
        self.session_epoch += 1
        self.query_pending = False
        self.active_lan = None
        self.current_map = ""
        self._server_info(info)

    def _server_info(self, info, epoch=None):
        if epoch is not None and epoch != self.session_epoch:
            return
        self.query_pending = False
        name = info.get("map", "")
        self.live_map.set(f"{info.get('name', 'Host')} • {name} • {info.get('players', '?')}/{info.get('max_players', '?')} players")
        self._record_map(name, "A2S_INFO")
        self.status.set("Host is responding • " + name)

    def _record_map(self, name, source):
        if name.startswith("c_game_menu"):
            return
        if name in self.catalog and name != self.current_map:
            self.current_map = name
            if self.active_game_dir:
                self.progress = {"map": name, "game_dir": str(self.active_game_dir),
                                 "confirmed_by": source, "updated_utc": datetime.now(timezone.utc).isoformat()}
                self.progress_store.save(self.progress)
                self.checkpoint.set(self._checkpoint_text())
            for widget in self.exits.winfo_children():
                widget.destroy()
            exits = core.next_maps(self.catalog, name)
            for index, target in enumerate(exits):
                ttk.Button(self.exits, text=target, command=lambda m=target: self.warp(m)).grid(row=index // 3, column=index % 3, padx=(0, 8), pady=4)
            if not exits:
                ttk.Label(self.exits, text="No trigger_changelevel exits in the scanned map").pack(anchor="w")
            self.log("Confirmed map: " + name)

    def warp(self, name):
        try:
            if self.host_endpoint is None:
                raise ValueError("Start or connect to a local host first")
            name = core.validate_map_name(self.active_game_dir or Path(self.game_dir.get()), name)
            endpoint, password = self.host_endpoint, self.rcon_password.get()
            self.work("Requesting map change", lambda: network.rcon(endpoint, password, "changelevel " + name), self.log)
        except Exception as exc:
            self.status.set(str(exc))

    def find_local(self):
        def show(rows):
            self.address_rows = rows
            self.address_list.delete(0, "end")
            for row in rows:
                self.address_list.insert("end", f"{row['address']}   {row['label']}")
            self.status.set(f"Found {len(rows)} local addresses")
        self.work("Finding local addresses", network.local_addresses, show)

    def _select_address(self, _):
        selection = self.address_list.curselection()
        if selection:
            self.share_address.set(f"{self.address_rows[selection[0]]['address']}:{self._invite_port()}")

    def _invite_port(self):
        return self.host_endpoint.port if self.host_endpoint else int(self.port.get())

    def find_public(self):
        try:
            port = self._invite_port()
            self.work("Looking up public address", network.discover_public_address, lambda address: self._public(address, port))
        except Exception as exc:
            self.status.set(str(exc))

    def _public(self, address, port):
        self.share_address.set(f"{address}:{port}")
        self.status.set("Public address found; external reachability has not been tested")

    def copy_code(self):
        try:
            code = network.make_join_code(network.parse_endpoint(self.share_address.get()))
            self.clipboard_clear()
            self.clipboard_append(code)
            self.status.set("Join code copied. Share the server password separately if set.")
        except Exception as exc:
            self.status.set(str(exc))

    def map_port(self):
        try:
            port = self._invite_port()
            if self.active_lan is True or (self.active_lan is None and self.lan.get()):
                raise ValueError("Untick LAN only and start the host in public mode before opening a public UDP port")
            if self.mapping or self.mapping_pending:
                raise ValueError("Remove this launcher's existing mapping before opening another")
            self.mapping_pending = True
            def mapped(result):
                self.mapping_pending = False
                self.mapping = result
                self._public(result["external_ip"], result["port"])
                self.log(f"UPnP opened UDP {result['port']} for {result['lease'] // 3600} hours. Use Remove my UPnP mapping before closing this launcher when finished.")
            self.work("Opening UDP port through UPnP", lambda: network.upnp_map(port), mapped)
        except Exception as exc:
            self.status.set(str(exc))

    def unmap_port(self):
        if not self.mapping:
            self.status.set("This launcher has no UPnP mapping to remove")
            return
        mapping = self.mapping
        def removed(_):
            self.mapping = None
            self.status.set("UPnP mapping removed")
        self.work("Removing UPnP mapping", lambda: network.upnp_unmap(mapping), removed)

    def _tick(self):
        if self.proc is not None and self.proc.poll() is not None:
            self.log(f"{self.process_role.capitalize()} process exited with code {self.proc.returncode}. If authentication failed, restart Steam and try again.")
            self.proc = None
            self.host_endpoint = None
            self.active_lan = None
            self.session_epoch += 1
            self.query_pending = False
            self.status.set("Game/server process has exited")
        if self.host_endpoint and not self.query_pending:
            self.query_pending = True
            endpoint = self.host_endpoint
            epoch = self.session_epoch
            self.work("Checking host", lambda: network.query_server(endpoint, timeout=1), lambda info: self._server_info(info, epoch), quiet=True)
        if self.tracker:
            try:
                for event in self.tracker.poll():
                    self.log("Game log: " + str(event))
                    if event.get("kind") == "map":
                        self._record_map(event["map"], "game log")
            except OSError:
                pass
        self.after(2500, self._tick)

    def close(self):
        try:
            self._save_settings()
        except Exception as exc:
            # Keep the last valid settings if a field contains incomplete input.
            self.log(f"Keeping previous settings: {exc}")
        # Closing the control window never ends the user's game or server.
        self.destroy()


def main():
    Launcher().mainloop()
