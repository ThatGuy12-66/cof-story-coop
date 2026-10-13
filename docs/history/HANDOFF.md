# HANDOFF: Cry of Fear story campaign in co-op

**Start here for the next agent:** [AGENT_NOTES.md](../../AGENT_NOTES.md) is the persistent current handoff required by [AGENTS.md](../../AGENTS.md). It supersedes the historical status paragraphs below and the dated [AGENT-HANDOFF-2026-09-27.md](AGENT-HANDOFF-2026-09-27.md). Read it for the October 12 inventory-recovery fix, interrupted live retest, build identities, and next steps.

**Remote-friend delivery ready:** `evidence/enhanced-story-package-v4/package/CoF-Story-Tester-v4.zip` (109,413,702 bytes; SHA-256 `7babd5dfebe7d40e2e01b6869f9fb730d7bc807848161c31ad45b17cc7e7d440`) contains the standalone setup/join EXE, matching Enhanced payload and corresponding source. A verified delivery copy and separate private invite are in `Downloads\Cry-of-Fear-package`. The real installer/model-generation smoke passed without launching a game; the full copy plan preserves all 235 maps. Independent archive CRC, host engine identity, critical source identity and private-credential-exclusion checks passed. Host PID 50180 remains the only game; the remote friend must close their old retail window, extract the whole ZIP, run `CoF-Story-Setup.exe`, click `Set up separate copy`, then enter the private invite and `Join host`. Next required live result is the remote friend joining this build and the two little rascals checking inventories/models/door transitions/backtracking. No full-campaign success has been claimed.

**Current update (2026-09-27, after the local join test):** The user explicitly wants one client here and one on the remote friend's PC. Do not launch another local guest. The model-userinfo overflow is fixed and two local clients joined successfully afterward. The first map transition exposed missing client entity allocation; that correction now builds and passes an actual-C regression, with live repeat still pending. Inventory diagnostics now read the verified `+0x21FC` descriptor slots instead of the empty legacy classname array. Latest matching engine SHA-256 is `4CB65CD93E9C8785E96BEAAE09A32EB1B6A3C52E0C154836B1C15E5DC35D5550`; 22 focused tests pass. The remote friend's one-click matching setup package is being prepared. Host entry point is `HOST-STORY.cmd`, ordinary map `c_start`, UDP 27015, 60 FPS. Fixture entity overrides have been archived. The existing router lease and firewall allow rule were verified; no network changes were needed. Private session credentials/invite are under `state` and must not enter packages. See the implementation document for current evidence; older active-process statements below are historical.

**New engine-work status (2026-09-27 afternoon):** The opt-in Enhanced story adapter, per-client transition integration, bounded party door recovery, and phone/knife catch-up are now implemented and compile. 87 project tests pass, with a separate native transition lifecycle harness also passing. Two private Simon models are prepared (standard Simon and the Sick Simon costume on the same walking rig); rendered animation remains unverified. First Enhanced fixture launch produced a live, healthy, unfrozen player with the correct position and an enabled verified runtime gate, but showed a black/noise world with HUD. This is a failed visual test, not a campaign success. Both extra Enhanced processes were closed; retail PID 46016 was preserved. The user subsequently authorized continued testing, requested a heads-up and closing the shared session before launching, and replied that the two little rascals would close it. Verify that process has actually exited before another launch. See [engine implementation and live checks](enhanced-implementation-session.md). The older statements below that Enhanced was never launched are historical.

**Latest live milestone:** the remote friend joined over the Internet using their Steam game. The server reported 2/2 active players on `c_start`, and the user confirmed they can see each other, both as police officers. Subsequent testing reproduced missing late-join items, a blocked companion at a story door, and Chapter 1 replaying on backtracking. The connection works, but correct story progression does not yet work. The active session is running; preserve it. See [Internet session notes](internet-session-2026-09-27.md) for current process, temporary router mapping, and remaining checks.

**Continuation update (2026-09-27):** See [current session evidence](session-2026-09-27.md), [launcher instructions](../../README.md), and [Enhanced source/build assessment](../enhanced-assessment.md). The original notes below are preserved as history. The exit-gate patch reached the engine, which rejected landmark transitions in multiplayer. The first null-landmark patch, SHA-256 `61406330BE1B838926C4660938F148C8E3429C03D0F331E2BF27A4D804FF03C4`, was malformed: an unchanged PE relocation corrupted its replacement instructions when loaded away from the preferred base. The repaired patch also disables that relocation and has SHA-256 `942586ABA56AB6C3FAB2DC9F1B4BF42B2DD87E3AF86B67BD29687C507171AFBA`; it was applied at 17:47:29 UTC and its live instructions were verified. The corrected isolated probe naturally reached `c_start` twice, and the full final-sequence probe also naturally reached it at 17:52:58 UTC; PID 15908 remained responsive. A separate missing-WAD fatal dialog came from the earlier minimal probe's omitted worldspawn data; the new probes preserve it. The user subsequently confirmed normal controls and playable Chapter 1 at the beginning with the phone. All three private diagnostic BSPs are archived under `evidence/diagnostic-maps` with matching hashes. The active catalog is exactly 235 maps, and all 62 tests pass. A fresh 6,190-file manifest check found only the expected repaired `hl.dll` difference; the other 6,189 files match. Enhanced is built but has never been launched. Full campaign co-op remains unverified.

Written 2026-09-27. Previous agent: Claude (claude.ai chat, working on this PC through Desktop Commander).
Read all of this before touching anything. Each claim is marked **VERIFIED** (seen on this machine or
in-game), **STATIC** (read from disassembly or files, not yet seen in-game), or **REPORTED** (from docs or
forums).

## 1. Goal

The user wants to play Cry of Fear's **single-player story campaign** in co-op
with 2-4 friends, with modern networking and quality-of-life features.

Planned route:
1. **Phase 1:** a launcher as a playable baseline.
2. **Longer term:** make it actually solid, by moving to the open-source Xash3D FWGS engine (the engine
   the "Cry of Fear: Enhanced" project already runs the game on) plus binary patches to the game's `hl.dll`.

The user was told a launcher alone cannot be flawless, because the problems live in `hl.dll` and the engine.

### How the user wants work done

- Execute rather than propose.
- Fix root causes.
- Check the whole inventory, not a sample.
- Give one-click launch workflows.
- Be honest about what is verified and what isn't.

### Game windows

The user watches test windows and interacts with them. **Say before you launch a game window. Never
auto-kill a run on a short timer.** The previous agent did that twice and drew a wrong conclusion
(section 5).

## 2. Paths and files

| What | Where |
| --- | --- |
| Steam install (**do not modify**) | `D:\SteamLibrary\steamapps\common\Cry of Fear` |
| Dev copy (work here) | `baseline\Cry of Fear` |
| Pristine hash manifest of the dev copy | `...\baseline\pristine-manifest.sha256` (taken before any patch) |
| Robocopy log | `...\baseline\robocopy.log` (6190 files, 4.373 GB, 0 failed) |
| BSP entity-lump reader | `...\CoF-Coop-Launcher\cofcoop\bsp.py` |
| Map scan script | `...\CoF-Coop-Launcher\tools\analyze_maps.py` |
| Scan output | `tools\map_report.json` (links, spawns, entity counts for all 235 maps), `tools\special_ents.json` |

Notes on these locations:
- **Why the D: drive is off-limits:** D: has had read failures (a known hardware-health concern), so work
  from the C: copy.
- **Test runs wrote only normal runtime files to the Steam install:** `qconsole.log` in the root, and
  level-transition `.HL1` files in `SAVE\` from one single-player run. No game files, configs or tape-recorder
  saves were changed.
- **The dev copy has one patch applied.** See section 6.

## 3. How the game runs (VERIFIED on this machine)

### Launch chain

- Steam starts `CoFLaunchApp.exe`, a settings launcher. It spawns the real engine, **`cof.exe`**, with:
  `-basedir cryoffear -game cryoffear -hw -heapsize 1536000 -no3dmenu -windowed`
- Steam must be running. `steam_appid.txt` = `223710`.
- Running `cof.exe` directly from the **C: copy** works: maps load and a listen server starts.
- `+map <name>` on the command line works with `-no3dmenu`.
- `-condebug` writes `qconsole.log` to the game root. That is how every test so far was read.

### Game files

- The server game code is `cryoffear\cl_dlls\hl.dll`, SHA256
  `0036B91C01E92ED205513F52563053A55A66A32D257EFB5F476B8F1F3DDE0E63` (stock 1.6).
- The client is `cryoffear\cl_dlls\client.dll`.
- `hlds.exe` ships with the game, so a dedicated server is possible (untested).
- `liblist.gam` contains `gamedll "cl_dlls/hl.dll"`, `type "singleplayer_only"`, `mpfilter "c_"` and
  `commandargs "-num_edicts 4096 -heapsize 1024000"`.
- `valve.rc` runs `exec autoexec.cfg`, then `stuffcmds`. `config.cfg` ends with `exec userconfig.cfg`
  (that file does not exist yet, so it is a safe place for launcher settings).

### Defaults and cvars

- Player name defaults to `name "Unknown"`. Players showed up as "Unknown" in tests, so the launcher
  must set `name`.
- Network defaults are dated: `cl_updaterate 20`, `cl_cmdrate 30`.
- The engine has `sv_maxrate`, `sv_minrate`, `sv_maxupdaterate`, `sv_minupdaterate`, `sv_downloadurl`,
  `logaddress`, `rcon_password` and `-port`.
- Relevant cvars found in `hl.dll`: `mp_flashlight`, `mp_weaponstay`, `mp_allowmonsters`,
  `mp_friendlyfire`, `mp_falldamage`, `mp_timelimit`, `mp_fraglimit`, `mp_forcerespawn`,
  `mp_copsalwaysvisible`, `sv_coop`, `sv_coop_nosave`, `sv_coop_music`, `sv_auto_respawn_time`,
  `sv_coopcprtime`, `sv_lobby_time`, `sv_votekick_time`, `sv_alltalk`, `sv_disablehints`,
  `mp_footsteps` (the lobby autostart time).
- Multiplayer game rules can set `mp_allowmonsters` from a map keyvalue (see the code near `0x10120dd3`).

### Co-op switch (REPORTED by the cof-enhanced docs, consistent with tests)

- `deathmatch 1` switches `hl.dll` to its multiplayer rules, and they set `sv_coop 1` themselves.
- `coop` must stay `0`.
- Test launch line used for the co-op host:
  `+sv_lan 1 +coop 0 +deathmatch 1 +maxplayers 2 +mp_timelimit 0 +map c_intro`

### Lobby (VERIFIED by BSP scan)

- The ready-up lobby only exists on maps whose worldspawn has `iuser1`. Only `cof_campaign_01` and
  `cof_manhunt_campaign` have it.
- Story maps have no lobby.
- Story maps have only `info_player_start`. The co-op maps use `cof_coop_spawn1` to `cof_coop_spawn4`.

## 4. Story campaign structure (VERIFIED from BSP entity lumps)

### Map graph

- 81 maps are reachable from `c_intro` through `trigger_changelevel` (`map` key).
- The graph is a **hub**: many links go both ways. For example, `c_city` connects to `c_sewer3`,
  `c_avlopp`, `c_buscity` and `c_city2`.
- Full adjacency is in `map_report.json`.

### Chapter start maps (from the game's own `cof_chapter` entities)

| Chapter | Title | Start map |
| --- | --- | --- |
| Ch1 | "Lost in a city" | `c_start` (after `c_intro` and `c_nightmare`) |
| Ch2 | "Who is that doctor?" | `c_basement` |
| Ch3 | "The city is not safe" | `c_sewer3` |
| Ch4 | "Drowned in sorrow" | `c_park3` |
| Ch5 | "Leaving this for good" | `c_subway2st1` |
| Ch6 | "It's not over yet" | `c_bridge` |
| Ch7 | "Only safe at home" | `c_attic` |
| Ch8 | "My life ends here" | `c_ending1b` / `c_ending2b` / `c_ending3b` |

### Endings

`c_village3` branches to `c_ending1` through `c_ending5`, each followed by `b`/`c` maps.

### Single-player scripting in story maps (each assumes one player)

| Entity | Count | What it does |
| --- | --- | --- |
| `cof_giveitems` | 16 | Hands out the camera, phone (`weapon_mobile`), switchblade, glock, key items (e.g. `inventoryitems/hallkey.txt`) |
| `cof_teleport` | 200 | Section moves within a map. Community reports teammates turn invisible around these. |
| `cof_killplayer` | 6 | |
| `cof_clearitems` | 3 | |
| `cof_phonecall` | 2 | |
| `cof_ending` | 3 | |

### The intro's exit

`c_intro`'s exit is a touch `trigger_changelevel` with no targetname, pointing at `c_nightmare` with
landmark `landakukej`. The cutscene carries the player into it.

## 5. Transition findings: what is and is NOT proven

### Single-player control run (VERIFIED)

- Command: `+map c_intro`, no deathmatch.
- Result: the log printed `CHANGE LEVEL: c_nightmare landakukej` within 100 s, and the level changed.

### Co-op runs (INCONCLUSIVE)

- Command: `deathmatch 1`, `maxplayers 2`, `+map c_intro`.
- Two runs were killed at 150 s and at 120 s (the second with the patched dll), with no level change
  logged.
- The user pointed out these runs were cut off too early:
  - In co-op the game first shows a co-op introduction screen (log: `sv_coop_music` goes to
    `introduction.mp3`, then later to `introreal.mp3`).
  - Only after that does the real cutscene start.
  - **The intro cannot be skipped in multiplayer**, although it can in single player (**VERIFIED by the
    user**). Skipping lives in `cof_skipmanager` / `CCofSkipManager`, so it goes on the fix list.
- **So it is NOT proven that exits are dead in co-op.** The user's position: "it's not that it won't
  transition, the intro's just not skippable."

### Static analysis of `hl.dll` (STATIC, image base `0x10000000`)

Tools used: pefile and capstone, on a copy of the stock dll.

**Where the level change happens.** `CChangeLevel::ChangeLevelNow` is at `0x10109d40`. Its xref is the
string `"CHANGE LEVEL: %s %s\n"` at `0x10109efa`.

**The check that may block it in multiplayer:**
1. The function first calls `g_pGameRules` (the pointer is at `[0x10222ee4]`) virtual slot `+0x24`, and
   returns immediately if that is nonzero (`jne 0x10109f1b` at `0x10109d60`).
2. The `CHalfLifeMultiplay` vtable `0x101d608c`, slot 9 (`0x100c90a0`), returns 1.
3. The `CHalfLifeRules` (single-player) vtable `0x101de3b0`, slot 9, returns 0.
4. So under multiplayer rules the exit is **probably** a no-op.

The contradiction: a 2023 Steam forum post claims the whole story is playable in co-op via console. The
in-game test above has to settle this.

**How the level change is issued.** Later, the function calls engine `CHANGE_LEVEL` through `[0x10222f60]`
at `0x10109f10`. The landmark (static buffer `0x10231034`) is pushed at `0x10109f06`.

**The engine side.** `hw.dll` contains `"changelevel2 <levelname> : not for use with multiplayer games"`.
If `hl.dll` passes a landmark in multiplayer, the engine probably refuses it. The consequences:
- no landmark arrival (players spawn at `info_player_start`)
- no hub state in multiplayer (maps reset when revisited)

### Other static findings

**`mp_copsalwaysvisible`** (function `0x100a9ce0`):
- It toggles `EF_NODRAW` (`effects` at pev `+0x118`) on a per-player **marker** entity. The entity's
  `pev->iuser1` = player index 1-4.
- It does not affect the player model, so it is a helper, not an invisibility fix. Moderate confidence.

**`cof_teleport`:**
- Export at `0x100aa5b0`, jumping to factory `0x10091240`, with vtable `0x101c5b50`.
- Its Use handler has not been found yet.

**Struct layout.** Cry of Fear's retail structs differ from the standard ones (REPORTED by cof-enhanced
docs):
- `entvars_t` is shifted +4 bytes after `light_level`. Offsets observed: `pContainingEntity` at pev
  `+0x20C`, `iuser1` at `+0x248`.
- `edict_t` is `0x32C` bytes.
- Consequence: stock Metamod/AMX plugins would read the wrong offsets.

## 6. Current patch state of the dev copy (C: only)

**Patched file:** `baseline\Cry of Fear\cryoffear\cl_dlls\hl.dll`
- Backup: `hl.dll.orig` (the stock hash above).
- Change: file offset `0x109160` (VA `0x10109d60`), 6 bytes, `0f85b5010000` changed to `909090909090`.
- This NOPs the multiplayer bail-out in `ChangeLevelNow`.
- Effect: **unverified** (that run was also cut off early).

**Candidate next patch, not applied:**
- Where: file offset `0x109306` (VA `0x10109f06`), bytes `68 34 10 23 10` (`push 0x10231034`).
- Change to: `6A 00 90 90 90` (`push 0`).
- Effect: plain `changelevel` without a landmark. Consider making it conditional on multiplayer.

**File offset rule:** `.text` VA `0x1000`, raw pointer `0x400`, so file offset = VA - `0x10000000` - `0xC00`.

**Restore:** copy `hl.dll.orig` back, or check against `pristine-manifest.sha256`.

## 7. Next steps (in order)

1. **Settle the transition question properly.**
   - Tell the user first.
   - Run the **stock** co-op intro (use the unpatched Steam install, or restore `hl.dll.orig` in the dev
     copy).
   - Do not kill it. Poll `qconsole.log` for `CHANGE LEVEL` or `c_nightmare` for up to about 10 minutes,
     or until the user closes it.
   - Then do the same with the patched dll.
   - Record whether the engine prints the `changelevel2 ... not for use with multiplayer games` refusal.
   - If the stock game transitions fine, the static reading was wrong. Update this doc.
2. **Build Phase 1, the launcher.** Python and tkinter with no external dependencies is fine; the user has
   Python 3.13 on PATH. Features:
   - **Setup:**
     - find the install
     - launch `cof.exe` directly with the args above
     - set `name`
     - co-op cvars: `deathmatch 1`, `coop 0`, `mp_timelimit 0`, `mp_flashlight 1`, `mp_weaponstay 1`,
       `mp_allowmonsters 1`, `sv_alltalk 1`
     - modern rates
   - **Chapter select:** from the table in section 4.
   - **Host panel over RCON:** warp to map, and "next map" buttons built from each map's real exits in
     `map_report.json`.
   - **Networking:**
     - UPnP port-forward on UDP 27015
     - LAN, public and virtual-LAN (Tailscale/ZeroTier) addresses
     - join codes
     - server query (A2S) before joining
   - **Progress:** `log on` and map tracking for resume.
   - **Extras:** a dedicated-server option through `hlds.exe`, and a desktop shortcut.
   - **Don't bother:** write launcher settings to `userconfig.cfg`, not `config.cfg`, which the game
     regenerates.
3. **Path C, the long-term fix.** Base it on github.com/hajdawery/cof-enhanced (GPL, v0.4, pre-release):
   - What it is: stock `hl.dll` and `client.dll` running on Xash3D FWGS commit
     `4857b389e6ba32ddaa68582aedcbc950c138f46a`. It has Host/Join co-op pages. Co-op was only tested on one
     PC.
   - Build instructions: `docs/dev/building.md` and `LICENSING.md` section 3 (submodule pins), SDL2
     `SDL2-devel-2.30.9-VC.zip`, and waf with
     `--enable-cof-entvars-legacy --msvc_version="msvc 17.14" --msvc_targets=x86`.
   - This machine has VS 2022 Community, VS 2022 BuildTools, VS 18 Community, VS 2019 and git 2.55.
   - **Nothing from that project is downloaded on this machine yet.** Confirm with the user before
     downloading.
   - Goals:
     - multiplayer transitions with per-player carry-over and landmark arrival
     - hub state for all clients
     - skippable intros in co-op
     - the invisible-teammate fix
     - peer-to-peer networking (e.g. GameNetworkingSockets)
     - `hl.dll` patches for item handouts and teleports affecting the whole group
4. **Open design questions for the user:**
   - Whose choices decide the ending?
   - Does every player get key items?
   - What do the others see during Simon's cutscenes?

## 8. Guardrails

- Never modify the Steam install on D:. Work in the C: copy and keep `pristine-manifest.sha256` valid.
- Never redistribute Cry of Fear's files. Friends install the free game from Steam, and patches go on top.
- Announce before launching game windows. Don't auto-kill runs on short timers. The user may be playing
  in them.
- Keep every claim labeled verified, static or reported, and correct this doc when a test disproves
  something.
