# First tester package: design

Date: 2026-10-15. Approved in chat by the user ("Go ahead") after these decisions:
testers can **host, join and play solo**; hosting uses **UPnP plus a join code**; the
package goes to **private, invited testers**; Host and Solo offer **new game, a chapter
list and Continue**.

## Goal

One zip a tester extracts, one exe they double-click. It makes their own separate game
copy, then lets them play solo with the AI companion, host friends, or join a friend.
Nothing in what they run is a developer tool.

## What the tester gets

`CoF-Story-Coop-Test1.zip`:

| Item | What |
| --- | --- |
| `CoF Story Co-op.exe` | Setup and launcher in one. The Enhanced engine payload is packed inside. |
| `READ ME FIRST.txt` | One page: install, play, controls, reporting bugs. |
| `source-and-licences\` | The GPL corresponding source (`corresponding-source.zip`) and the licence notices for Python, Tk and PyInstaller. |

## Setup (first run)

- Finds Cry of Fear through Steam (as today; Browse if not found) and needs the retail
  1.6 game DLL (as today).
- Makes a separate copy, default `%LOCALAPPDATA%\CoF-Story-Coop`; the Steam game is
  never written. Same copy rules as today (no saves, logs or personal configs).
- Installs the engine payload (the Enhanced installer `install.ps1`, as today), makes
  both Simon models (as today) and **the 12-slot inventory art** from the tester's own
  images (`tools/make_inventory12_art.py`; new: the old setup never made it).
- Writes the default controls and `cryoffear\cof_story.cfg` (below), copies the exe into
  the copy, and creates a **desktop and Start-menu shortcut "Cry of Fear Co-op"**.
- **Update in place:** an exe of a newer version, run while a copy exists, offers
  **Update**: it lays the new payload over the copy and reruns the Enhanced installer
  (which upgrades in place), regenerates the art, replaces the exe and the shortcuts,
  and keeps saves, settings and the generated models. Same version: straight to the
  main window. The exe started from inside a copy always opens the main window.

## Main window

Your name (remembered), the version ("Test 1"), and three actions:

- **Play solo**: you and the AI companion. Start: **New game** (the intro; Enter skips
  cutscenes), a **chapter list** (the existing `CHAPTERS` table in `cofcoop/core.py`),
  or **Continue** (experimental). LAN-only server, no password, 2 player slots.
- **Host**: the same start choices; players 2–4; optional password; optional AI
  companion (it gives up its slot when a person joins; existing engine behaviour); port
  (default 27015). After the game starts:
  - the app asks the router for a UDP mapping with UPnP (`cofcoop/network.py`
    `upnp_map`) and removes it when the game closes (`upnp_unmap`);
  - it shows a **join code** (`make_join_code`) and the address, each with Copy, plus
    the LAN address for players at the same place;
  - if UPnP fails it says so in plain words and points to port forwarding (UDP, the
    port) or a VPN (Tailscale, ZeroTier, Radmin) where the VPN address works as the
    address; Windows Firewall may ask to allow the game: allow it;
  - it shows "Your game is up: n/N players" from the server query.
- **Join**: a join code or an address, and the password if any. Before the game starts
  the app queries the host and refuses with a clear message when nobody answers, the
  host runs **a different version**, or the game is full.
- Small: **Open logs folder**, **Report a bug** (opens
  `https://github.com/charliee1w/cof-story-coop/issues/new/choose`).

Only one game at a time: every action refuses while a Cry of Fear window from this copy
runs (as today).

### Continue

The latest co-op checkpoint (`SAVE\cofcoop\latest.txt`, or `cryoffear\save\cofcoop\`)
is read by the app: the archive's `session.dat` gives the map, the player limit and
the saved names. Continue is greyed out without a readable format-2 checkpoint, and
shows "Continue: <map>". It starts the server with the **checkpoint's player limit**
(the engine refuses any other) on the checkpoint's map with `cof_continue 1`. The
engine then loads the latest checkpoint once the host has spawned (below). If the load
is refused the map simply plays from its start and the host sees a one-line message.
The host should keep the same name; the app warns when the name is not among the
saved names.

## What the game runs (dev parts removed)

Removed for testers: `-console` (no console), RCON (no `rcon_password`), the test gun
(`cof_test_gun 0`, not offered), cheats (`sv_cheats 0`, `sv_enttools_enable 0`), the
forced 960x540 window, "Little Rascal" names and test hostnames, hash/protocol wording
in every text, and the join's reset of mouse sensitivity (defaults are written once at
setup). The story trace (`cof_story_trace 1`) stays in the log file for bug reports;
it is already never drawn on screen.

Video: the **first** launch passes `-borderless -width <desktop> -height <desktop>`
(the engine saves the mode); later launches pass nothing, so the game's own
Options > Video apply. `fps_max 60` stays (GoldSrc physics).

`cryoffear\cof_story.cfg` (all modes): `cof_story_coop 1`, `cof_story_trace 1`,
`sv_cheats 0`, `sv_enttools_enable 0`, `coop 0`, `deathmatch 1`, `mp_timelimit 0`,
`mp_fraglimit 0`, `cof_test_gun 0`, `fps_max 60`, the rates, binds `j cof_warp`,
`h cof_companion_order`, `enter cof_skip`, `kp_enter cof_skip`. A per-launch
`cryoffear\cof_story_session.cfg` carries name, password, hostname and player limit, so
no password is on a command line. Each game writes `story-session.log` in the copy;
the previous one moves to `story-logs\` (as today).

## Engine changes (small)

1. **Build id.** Server cvar `cof_build` (default empty). When set, the Xash `info`
   reply (`SV_Info`, `engine/server/sv_client.c`) carries `\cofbuild\<value>`. The app
   starts every server with `+cof_build test1`; Join compares it with its own.
2. **Continue.** Server cvar `cof_continue` (default 0). In `SV_CoF_SessionFrame`, on
   the first map of a fresh server, once the listen host is spawned and the existing
   2-second settle has passed, the engine calls `SV_CoF_SessionLoad( NULL )` once, logs
   `session-continue slot=… result=queued|refused`, tells the host when refused, and
   sets the cvar back to 0.

## Package build

`tools/build_tester_package.py` (replaces `tools/build_friend_setup.py` and the friend
bundle part of `tools/package_enhanced_story.py`):

1. Refuses unless the launcher, engine and FreeVGUI repos have no uncommitted changes
   (the source archive must match the build) and the engine build is newer than its
   sources.
2. Builds the Enhanced player payload and the source archive with the existing
   `package_enhanced_story.py` stage (upstream `scripts/build-release.ps1`).
3. Builds the exe with PyInstaller (one file, windowed), the payload zip as data,
   runs `--self-test`.
4. Writes the zip above, CRC-checks it, and records hashes in a report under a new
   `evidence/` folder.

Before the first package, all current uncommitted work (companion routes, UI pass) is
committed in the three repos, on their current branches.

## Testing

- Unit tests: start-option command lines and cfg text per mode (no password in argv,
  no `-console`, no RCON, test gun off), the first-launch video flags, chapters,
  checkpoint reading (synthetic archives: good, other version, truncated, none),
  the info reply parser and version comparison, update-in-place (payload laid over,
  saves/configs/models kept), the art step in setup, shortcut command text, the
  package builder's refusals (dirty repo, stale engine) with fakes.
- `cofcoop/network.py` `query_info` against a loopback UDP fake server.
- Engine: the session lifecycle harness covers `cof_continue` (waits for the host,
  once, cleared, refused path); a source check covers the `cofbuild` key.
- Full suite; the built exe's `--self-test`; an unattended install (`--install` CLI)
  from `baseline\Cry of Fear` into a scratch evidence folder, then the update path over
  it.
- Live, only with the user's go-ahead: Play solo from the installed copy; Join from a
  second installed copy over loopback.

## Not in Test 1

Auto-update, code signing (Windows SmartScreen "More info, Run anyway"), dedicated
servers, an in-app server browser, and the companion fixes (teleport rate, ledge
jumps, spawning in view). The user's own hosting setup (`HOST-STORY.cmd`,
`runtimes\`, the test copies under `evidence\`) is unchanged.
