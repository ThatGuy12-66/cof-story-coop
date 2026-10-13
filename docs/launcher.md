# The launcher (retail engine)

`launch.pyw` is the project's first approach: a Tk launcher that hosts and joins the original
story campaign on the **retail** Cry of Fear engine. The current co-op work runs on the Enhanced
engine instead (see the [README](../README.md)); this launcher is kept because its networking,
RCON and map tools still work and are covered by tests.

Run `.\Install-shortcut.ps1` once to put **Cry of Fear Story Co-op** on the desktop, or
double-click `launch.pyw` using Python with Tk. Keep Steam open and signed in. The launcher works
on a separate local copy of the game under `baseline/`; it refuses to write to Steam-managed
installations.

## Host and join

1. On Host, set your name, choose a chapter or map, and select 2–4 total player slots.
2. Keep **LAN only** checked for the same physical LAN. Untick it for public hosting or virtual networks such as Tailscale/ZeroTier. Then click **Start co-op**.
3. Under Network & invite, find your local or public address, select it, and copy the join code. A virtual network must already be installed and joined by every player.
4. Friends enter your address/code under Join, enter the server password if needed, and choose **Check & join**. The server must answer an A2S query first.

For public hosting, UDP 27015 (or your selected port) must reach the host and be allowed by Windows Firewall. **Open UDP port with UPnP** explicitly requests a temporary, one-day router mapping; it never overwrites someone else's mapping. **Remove my UPnP mapping** removes only the mapping made by this running launcher. Remove it before closing the launcher when finished, or let its lease expire. Double NAT/CGNAT and routers requiring permanent mappings need a different network route or manual router setup. A join code encodes an address; it is not P2P transport or a relay. No router/firewall settings change merely by opening the launcher.

Every player supplies their own freely available Steam game files. This project does not redistribute game binaries or assets. Friends need Python 3 with Tk and a separate local copy containing `cof.exe` and the `cryoffear` directory. **Find installs** lists detected source locations.

## Progress and host controls

The launcher records the last confirmed map for games it starts. **Resume map checkpoint** starts that map from scratch: inventory, choices, enemies, puzzles, and hub state are not restored. A failed or refused `CHANGE LEVEL` is never recorded as successful progress. Manual attachment supports live controls, but cannot prove the running executable's folder and therefore does not create a persistent checkpoint.

Host controls show exits from the current BSP's actual `trigger_changelevel` entities, including hub branches. Those buttons and **Warp to map** send GoldSrc RCON `changelevel`; they are manual recovery controls. Map/chapter jumps can omit story items or context. The RCON password is generated locally and saved under `state/`; keep it private. Share a server password separately from the join code.

Closing the launcher leaves the game/server running. It never kills a game on a timer. A second launch from the same running game copy is refused before changing its configuration. The dedicated-server option starts shipped `hlds.exe`; dedicated story behavior requires a separate live test.

## Game configuration and experiments

Managed settings live in `cryoffear/cofcoop_launcher.cfg`, referenced from `userconfig.cfg`. Existing userconfig content is preserved and backed up when changed; the launcher does not edit regenerated `config.cfg`. It sets the multiplayer rules (`deathmatch 1`, `coop 0`), player name, flashlight, weapon persistence, monsters, all-talk, no time/frag limit, and modern rates. An active Steam process without a signed-in Steam session is detected before launching.

Current runtime limitation: the game resets flashlight, weapon persistence, and monster cvars during initialization. Their configured values are not yet guaranteed after map load. Name, hostname, rates, and authenticated RCON have been confirmed in the running game.

The original story's entities still assume single player. On the retail engine, automatic exits, landmark arrivals, hub persistence, shared inventory, cutscene skipping, and teammate visibility are experimental; the Enhanced engine work replaces most of this.

The reversible DLL experiment tool verifies the entire known stock DLL and refuses unknown changes or a running game. With the game closed:

```powershell
python tools/patch_coop.py --mode allow-transition --apply
python tools/patch_coop.py --mode stock --apply
```

The first command removes the known multiplayer exit gate; the second restores exact stock bytes. Without `--apply`, inspection is read-only. The separate `plain-coop-transition` mode removes the landmark argument and its obsolete PE relocation. The earlier version retained a relocation that corrupted the new instructions when Windows loaded the DLL; the patch tool now recognizes and repairs that version.

Live tests confirmed repeated automatic exits and the original camera section's final effects/teleport/exit chain reaching `c_start` with the repaired DLL (see [session evidence](history/session-2026-09-27.md)). It still changes single-player behavior and omits landmark semantics, so it remains a co-op experiment. These tests do not establish inventory carry-over, hub persistence, or a complete campaign with multiple clients.

## Checks

```powershell
python tools/verify_inventory.py
```

Inventory verification reads all 6,190 manifest files and all 235 maps in the local baseline copy. Runtime configuration/log/save changes and deliberate DLL experiments will be reported as differences; the pristine manifest itself is never rewritten. Protocol tests use loopback fixtures and do not open real router mappings.
