# Cry of Fear co-op: persistent shared agent notes

Last updated: 2026-10-21 midday: 1.1.9, the polish pass (gas mask on others, ladders, movement, per-player pickups, the open-issues list) (see "1.1.9: polish"). Before that, 2026-10-21 morning: 1.1.8, the debug pass over the whole story (chapter starts, shared taclight/battery, fuses, exits, sign-on re-tell, scenes, app Repair), built and offline-tested, not played (see "1.1.8: the debug pass"). Before that, 2026-10-21: 1.1.6 published (GitHub latest + ModDB); 1.1.7 published (GitHub latest + ModDB): faster level changes for guests (sign-on fragments of ten split pieces, 256 KB socket queues, the microphone kept open), not played (see "1.1.7: faster level changes"). Before that, 2026-10-20 night: 1.1.4 (AMD depth range) and 1.1.5 (join crash) published; 1.1.6 built (hands after a door, the co-op torch, the bounce back through an exit) (see "1.1.4 to 1.1.6"). Before that, 2026-10-20 evening: 1.1.2 published; 1.1.3 prepared: engine texture names for AMD cards (see "1.1.2 out; 1.1.3: textures on AMD"). Before that, 2026-10-20 afternoon: releases automated: the app updates itself from a public releases page, `tools/release.py build`/`publish`, the Discord bot posts new versions (see "Releases automated"). Before that, 2026-10-20 early morning: 1.1 polish started, investigation only: night vision flashing (cause open), the merged subway's decal overflow, an installer live-status request; the user installed 1.0 from the zip to test it (see "1.1 polish"). Before that, 2026-10-20: release 1.0 (version 1.0, build 4: player readme, Discord bug reports, upstream install docs and the cheats guide out of the copy; see "Release 1.0"). Before that, 2026-10-19 night: in cm_college2 both doors of the screwdriver classroom lead back into the room (live, after a trip to c_college2 and back); cause not found offline; a door-use probe is built and swapped into the smoke copy, not played (see "College classroom doors lead back in"). Before that, 2026-10-19 evening: the crash on dying to the second boss (a respawn on the entry landmark fired the exit back) is fixed in Test 2 build 3, built, offline-tested and in the smoke copy, not played (see "Respawn crash after the second boss"). Before that, 2026-10-19: Test 2 prepared without launching anything: wave 3 merged maps (29 groups), ambient loops per section, an offline check of every story exit; built and offline-tested, never played (see "Test 2 without launching"). Before that, 2026-10-18: the fixes from the decompilation survey are built and offline-tested on `tester-package` (46-task plan, 451 tests OK; live checks pending; see "Fixes from the decompilation survey (2026-10-18)"). Before that, 2026-10-15 midday: the first tester package, `CoF-Story-Coop-Test1.zip` (one app: setup, Play solo, Host, Join), built and smoke-installed; see "Tester package (2026-10-15)". All work is committed on branch `tester-package` (launcher and engine; FreeVGUI on `story-coop`), not merged, not pushed. Before that, 2026-10-15 early morning: a live UI pass fixed the wardrobe and objectives text, the 12-slot grid's alignment and the RCON echo (see "UI pass (2026-10-15)"); committed in the tester-package work's first commits. Before that, 2026-10-14 late evening: the AI companion fix (doors, hopping, stop-start) and its route-graph navigation, first played live 2026-10-14 23:09–23:34 (see "AI companion: doors, hops and routes"). Before that: the 12-slot inventory is implemented, verified offline, built and installed in the two disposable copies; NOT live-tested (see "Inventory grid"). Before that, 2026-10-13 late morning: `c_start` ↔ `c_start2` played live; the repositories were cleaned up and published as private GitHub repos for playtesters (see "Repository cleanup and sharing").
This is the current handoff. `AGENTS.md` requires project and delegated agents to read it.
Older dated handoffs remain historical evidence, not authoritative current status.

## Corrections to older entries (checked 2026-10-21 against the code at 1.1.8)

Several older entries below still read as open but were settled later. What is true now:
- Night-vision strobe and the merged subway's decal overflow: fixed in 1.1 (`engine/client/cof_renderer_patch.h`), NV seen live ("1.1 polish").
- College classroom doors loop: reported once (2026-10-19 21:53, cm_college2), did not repeat later that night, the scripted repro did not reproduce it; cause unknown. The use probe now needs `cof_use_probe 1` (off by default since 1.1.8).
- "I can't switch weapons or access the inventory" after the c_nightmare camera gift, and "the camera couldn't be added back": client.dll blocks the inventory, objectives and sprint in c_nightmare by design (`cl_dll/input.cpp` 637/857/903). Not our bug.
- A story-given weapon that could not be equipped (slot-6 menu overlap): gone with the 12-slot grid, shipped since 1.0.
- Dual wield "others see empty hands" and third-person weapons "proposed": implemented, both hands drawn.
- Blur "not seen live / not in a package": shipped in 1.1.2. Grey fade: shared since 1.1.8. Enter/`cof_skip` skips scenes in co-op (seen live).
- Intro logos `logo-shared`: the CoFLogo mirror never ran (fixed-size message) through 1.1.8.
- ~71k ScreenFade messages to the companion in the trace: no longer traced (1.1.8). Arrivals/respawns in an exit back: refused since 1.1.6 / 1.1.8.
- "Report a bug" destination: the public Discord since 1.0.
- Loops started later not re-sent to a player entering a section: per-section loops handle it.
- The solo-mode AI drive bug: unreachable from the app (`cof_companion_drive 0`).
- The "Next steps" lists from 2026-10-14..10-19 (packages build 3/4, disposable-pair tests, `3D860BC7`, playtesters via GitHub): superseded by the app, Discord and the public releases 1.0-1.1.8.
- Exits still held as scripted after 1.1.8: c_apartment3 `changefitta` (#291 via trigger_once #289) and c_apartmentsick `changekuk` (#412 via #446), not c_hall5/c_hall6.
- docs/research/2026-10-12-map-chains-re.md: c_attic `teleportblack`/`deteleport1` are not inert: `CDoctorBoss::Killed` fires its target with the first player.

## Current status: story moments, polish and an AI companion implemented offline; campaign live test pending

- **12-slot inventory (2026-10-14): implemented, offline-verified (172 tests OK on the final tree, mutation 14/14 caught), built (`xash.dll` `02F90A33…`, `vgui.dll` `D92A5D17…`) and installed in both disposable copies; live: not yet.** At the user's choice ("do one": merge locally) the `inventory-12` branches were fast-forward merged on 2026-10-14 and deleted: launcher `main` = `0eb673f` plus this notes commit, engine `story-coop` = `b9e0e54b` (includes the FreeVGUI submodule pointer), FreeVGUI `story-coop` = `e4a6d8c`. **Not pushed**: the private GitHub repos still hold the 2026-10-13 state. Smallest next step: the live checklist "12-slot inventory" in `docs/campaign-test-guide.md` (needs the user's go-ahead to launch). The copies no longer run the `3D860BC7…` build.
- Repositories (2026-10-13 cleanup, section below; since 2026-10-14 `main`/`story-coop` also carry the 12-slot inventory, see above): launcher `main` started as one clean commit, the full local history stays on the local-only branch `split-play-stage3` (never push it: it has the remote friend's name and personal paths). Engine branch `story-coop`, everything committed. Private GitHub repos: `charliee1w/cof-story-coop`, `-engine`, `-freevgui`, `-mainui`. Do not reset, clean, merge, or discard existing changes.
- The user asked (2026-10-12) for as much work as possible **without launching game sessions**, so that the next test is as close to the finished game as possible; then for a way to test both clients alone, and for an AI companion ("use items on its own … a real cool companion for people who don't want to play it solo … less scares"). No game was launched in any of these sessions.
- Implemented and offline-tested (not live-tested): pieces 2 and 4 of the split-play spec, from static research of the retail DLL and all 81 story maps. Index: `docs/research/2026-10-12-story-moments-re.md`; plan: `docs/superpowers/plans/2026-10-12-story-moments-and-polish.md`; the user-facing checklist for the next test: `docs/campaign-test-guide.md`. Also the AI companion (`docs/companion.md`, section below).
- Engine build `3D860BC7…8626A12D` (see table below) includes everything before plus the AI companion, the test gun and the fixes from the first live session (cutscene skip, intro end, team-wipe crash). It is **installed in both disposable copies** under `evidence/story-live-20261012/` (host, guest; previous engines kept beside each as `xash.dll.<hash8>.bak`), with Enter bound to `cof_skip` in their `cof_campaign.cfg`. It is **not** installed in `runtimes/enhanced` or `runtimes/enhanced-local-client`; those still hold `B1556A82…` (rechecked 20:00) and match `state/enhanced-remote-session.json`.
- Normal runtime/save/config files were not modified. At 15:34 local on October 12, all 92 files in `evidence/story-live-20261012/protected-before.json` still matched their pre-test hashes (dated verification).
- Processes (dated observation, 2026-10-14): no game was running when the 12-slot build was installed (`Get-Process CoFLaunchApp,cof` empty before each Prepare run), and nothing was launched in that work. Earlier observation, 2026-10-13 23:22: no game running; the evening demo session was closed by the agent at the user's request (section "Demo session" below).
- Older: the 20:02 solo session (started at the user's "ok run em") ended with the host crashing at 20:07:27 (below). Afterwards the user said they were going to play another game: "don't relaunch, but keep work". Builds and tests since then ran at BelowNormal priority (`lowtest.ps1` in the session scratchpad). Nothing was relaunched.

## Tester package (2026-10-15)

User: "get the system ready for our first tester package. Get all the dev shit out of there … public facing, easy to install zip slash exe". Decisions (chat): testers can host, join and play solo; hosting = UPnP + join code; private, invited testers; Host/Solo start = new game, chapter list, Continue. Spec `docs/superpowers/specs/2026-10-15-tester-package-design.md`, plan `docs/superpowers/plans/2026-10-15-tester-package.md` (executed inline; the user: "Do it all yourself"; no reviewer subagents per the standing preference).

What testers get: `CoF-Story-Coop-Test1.zip` = `CoF Story Co-op.exe` (setup + launcher, PyInstaller one-file, the Enhanced player payload inside) + `READ ME FIRST.txt` (`tools/tester_readme.txt`) + `source-and-licences\` (corresponding source, Python/Tk/PyInstaller licences).
- `tools/coop_app.py` (new): the Tk window and exe entry. Setup screen (Steam discovery, default copy `%LOCALAPPDATA%\CoF-Story-Coop`), Update screen (a newer package over an older copy), main window (name; Play solo / Host / Join; Open logs folder; Report a bug), hosting panel (status, join code, Internet and LAN address, router text). CLI: `--install SOURCE DEST`, `--update COPY`, `--no-shortcuts`, `--payload DIR`, `--copy COPY`, `--self-test`.
- `tools/coop_play.py` (new, pure): command lines and cfgs per mode (`cryoffear\cof_story.cfg` every launch: story co-op on, `cof_story_trace 1`, cheats/enttools/test gun off, `fps_max 60`; `cof_story_session.cfg` per launch: name, password, hostname, maxplayers, `sv_lan`, `cof_companion`, `cof_build test1`, `cof_continue`), no `-console`, no RCON, no password on any command line, first launch `-borderless` at desktop size; `check_host` (refuses another/missing `cofbuild`, full, password needed); checkpoint reader (`SAVE\cofcoop\latest.txt` → `<slot>.cof` → `session.dat`: map, player limit, names; format 2 only); `HostWatch` (UPnP map → join code, fallback to the public address with port-forward/VPN advice, server status, unmap once on game end or app close).
- `tools/friend_setup.py`: payload from the exe (`payload.json` schema 2 + `engine-payload.zip`), the 12-slot inventory art in setup (`make_inventory12_art`), update in place (payload over the copy, the Enhanced installer upgrades, art regenerated, saves/configs/models kept), desktop + Start-menu shortcuts "Cry of Fear Co-op" (paths via environment), `game_is_running(root)` counts only this copy's game, logs `story-session.log` → `story-logs\`. The old join window, `JOIN.cmd`, `CoF-Story-Join.exe` and `bundle.json` are gone.
- `cofcoop/network.py`: `query_info` / `parse_info_reply` (the Xash `info 49` query).
- Engine (`tester-package`, `d16d01f3`): server cvar `cof_build` → `\cofbuild\` in the `info` reply (`SV_Info`); `cof_continue` loads the latest checkpoint once the listen host is spawned and the 2 s settle has passed (`SV_CoF_SessionFrame`, logs `session-continue result=queued|refused`, tells the host when refused).
- Builder `tools/build_tester_package.py` (replaces `tools/build_friend_setup.py`): refuses uncommitted repos, runs `package_enhanced_story.package` (friend bundle removed, neutral notice texts, new sources listed), PyInstaller, `--self-test`, the zip, `package-report.json`.
- Docs: `docs/playtesting.md` rewritten for the app, README, `docs/building.md`, the bug template, a historical note in `docs/enhanced-story-testing.md`.

Commits: launcher `tester-package` `3ab46d5..9c55b7a` (+ this notes commit), engine `tester-package` `62674f8b..d16d01f3`, FreeVGUI `story-coop` `66eca8d` (the UI pass). Before the package work: launcher `b09bd4a` (companion routes + UI pass, on `companion-ai`), engine `62674f8b` (on `companion-ai`). Nothing merged or pushed.

Verification: full suite **205 OK** (`evidence/tester-package-20261015/full-suite-task8.log`); engine build exit 0 (`build.log`); package `evidence/tester-package-20261015/package/` (`package.log`, `package-report.json`, `pyinstaller.log`): zip `57E585F0…` (110,770,350 bytes), exe `BD14EC8A…`, payload `xash.dll` `6C5DAB9B…`, `vgui.dll` `40642512…` (FreeVGUI unchanged since the UI pass). The app windows were checked by rendering each panel from a script (`capture_panels.py`, session scratchpad; PNGs there), not by clicking.
Smoke install (2026-10-15 11:50, first package build): `"CoF Story Co-op.exe" --install <source> evidence\tester-package-20261015\smoke\CoF-Story-Coop --no-shortcuts` → exit 0 in 106 s. Source: a hardlink mirror of `baseline\Cry of Fear` (`smoke-source\`) with the stock `hl.dll` (baseline's own `hl.dll` is the patched retail-launcher experiment `942586AB…`; its `hl.dll.orig` is stock `0036B91C…`); baseline and the Steam folder were not changed. Checked: `cof-story-installed.json` schema 2 / build 1, `xash.dll` `6C5DAB9B…`, `vgui.dll` `40642512…`, stock `hl.dll`, both Simon models, all 8 `inventory12_*.tga` (main + Polish overlay), `cof_story.cfg` (test gun 0, no console/RCON), the app exe in the copy. The copy also holds Enhanced's `Install.cmd`/`Uninstall.cmd`/`README.txt` from the payload (as before).
Self-review finding fixed after that build: closing the app while hosting removed the router port (friends could drop) → it now asks first (`close_warning`, launcher `a1f0ea0`); the package was rebuilt.
**Final package (the one to send):** `evidence/tester-package-20261015/package-final/CoF-Story-Coop-Test1.zip`, SHA-256 `EF2DFBA3481FDD030FE4125E3D7C3D861796D80B67B3EBF8E25AA637F6C18564`, 110,771,943 bytes; exe `CF84B0D7…`; payload `xash.dll` `6C5DAB9B…`; built from launcher `0ce6020`, engine `d16d01f3`, FreeVGUI `66eca8d` (`package-final.log`, `package-final/package-report.json`). Update path checked with it: `--update` over the smoke copy → exit 0 in 17 s, the Enhanced installer "upgrading it in place", 667 files verified; a marker save, an edited `config.cfg` and the models unchanged; the app exe replaced. The zip extracts to exactly `CoF Story Co-op.exe`, `READ ME FIRST.txt`, `source-and-licences\` (`unzipped\`), and the extracted exe's `--self-test` passes. The first build (`package/`, zip `57E585F0…`) is superseded. Disk: C: 7.8 GB free after this (smoke copy ~4.4 GB; `smoke-source\` is hardlinks only).
Deferred minors (self-review): the hosting status can show "Starting the game..." again for a moment when one status query times out (a level change); a copy set up outside `%LOCALAPPDATA%\CoF-Story-Coop` is not found by a newer package (it offers Setup, not Update); `--copy` with a relative path misses this copy's game in `game_is_running` (dev flag only).
Live (2026-10-15 11:52–12:02, user's go-ahead "Do it"): the smoke copy's app → Play solo → New game. Worked: `-borderless -width 2560 -height 1440`, no `-console`, `companion-added`, Enter skip, `query_info` on 127.0.0.1:27015 → `cofbuild test1`, `numcl 1`, `maxcl 2` (`check_host` passes), version stamps only the first seconds (upstream `force_draw_version_time`). The user started recording with OBS. Two bugs found and fixed (both offline-tested, built; **not yet re-checked live**):
- **A console window beside the game** (irrKlang/NumTemps warnings; closing it would kill the game): `CoFLaunchApp.exe` is a console-subsystem exe (the Steam one is GUI); started from the windowed app it got its own console (the PowerShell test scripts shared theirs). Fix: `coop_app.start_game` uses `CREATE_NO_WINDOW` (launcher `f0a82b6`, test `test_the_game_starts_without_a_console_window`).
- **Stuck in c_intro's black room (`kunitei`)**: the party shares the owner's spot there (the 10-12 intro fix; the room fits one), retail MP PreThink makes a player that overlaps another `SOLID_NOT` (`docs/research/2026-09-30-hl-exits-coop-re.md`), and `SV_LinkEdict` skips trigger touches for `SOLID_NOT` bodies, so the player never touched the exit while the idle companion stood on it (the room is too small to separate: ±27 units vs 32 needed). With two humans it only fired once they happened to separate (10-12 log: 69 s). Fix: `SV_CoF_MomentsStackedTouch` (`sv_cof_moments.c`) + the `sv_world.c` non-solid branch: a living player non-solid only because it overlaps a living partner still touches triggers (engine `866bd81b`, `cof_moments_harness` scenario, source check `test_a_stacked_player_still_touches_triggers`). On the way a missing `gate-enabled` line looked like the cause; reading the live process (`read_gate.py`, scratchpad) showed the gate bytes `90×6` (open) and the client.dll hook in place: the tester build's log simply lacks non-trace console lines (no `-console`).
The user asked to close the game (12:02, closed cleanly), then the package was rebuilt: **current package** `evidence/tester-package-20261015/package-fix2/CoF-Story-Coop-Test1.zip`, SHA-256 `005E3E20FCA08451E2087C68576016CFCEBB3275FC0E4E9E8585EDABAD7F21C5`, 110,774,587 bytes, exe `1DD38E81…`, `xash.dll` `AC329D5C…`, from launcher `14d4756`, engine `866bd81b`; full suite 208 OK (`full-suite-fix2.log`); the smoke copy updated to it (`--update`, exit 0). `package-final/` is superseded. Next: the user replays the intro to the black room (the exit should fire at once), then Host.

### Dual wield and the bonus room (2026-10-15 afternoon)

User (after playing `c_start` from the tester app): "Dual wielding isn't working correctly, and I want there to be a check option for the … room with the FAMAS … to be unlocked or locked." Chose: a Host/Solo checkbox, unticked by default. Research by two read-only subagents: `evidence/tester-package-20261015/research/dualwield-mp-re.md`, `bonus-room-re.md` (scripts beside them).
- **Dual wield** (engine `ceed4781`): the retail ClientCommand refuses `inventorydualwield` in multiplayer (jnz at RVA 0x1B6E0, `0F 85 5A 15 00 00`): NOPed with the exit gate in `sv_cof_story.c` (`COF_DUAL_GATE`, logged `gate-enabled … dualwield=open`, closed with the gate). The game then builds `weapon_dualweapon`, but the multiplayer `FShouldSwitchWeapon` switches only to heavier weapons (dual 1, pistols 5), so `SV_CoF_RetailDualDeploy` (`sv_cof_retail.c`, called in `sv_client.c` after the game's ClientCommand) calls retail `SwitchWeapon` (RVA 0xF5E50) for the pair the player just asked for (logs `dualwield-switch`). The DualWield range checks (RVA 0xDEA0C/0xDEA18, `cmp eax,5` → 11) joined the 12-slot wide group. Known limits (static RE): others see empty hands; the left hand's client state is one global object, so two players dual-wielding at once may mix animations; the quick-slot double-tap dual wield stays closed. Tests: `cof_retail_harness` scenario, `cof_inv12_patch_harness` (13 wide sites against the retail hl.dll), `test_coop_engine` (gate bytes against hl.dll).
- **Bonus room** (engine `9b89f0ec`): the door (`inter_door` `unlockablesdoor`, c_start #828 / c_bridge #377) and the bonus items read `scriptsettings.dat` through the server's LoadFileForMe on every check (hl.dll 0x100DD000). New `sv_cof_bonus.h` (pure) + `sv_cof_bonus.c`: cvar `cof_bonus_room` (-1 default = the file, 0 shut, 1 open) and `SV_CoF_BonusLoadFile` in the server engfuncs table rewrite the 11 gated lines (five endings for the door; FAMAS, axe, camera, booklaser, package key, night vision) in the returned buffer; the file is never written; a missing file is synthesized when open. The user's own game (baseline) has every unlock locked. Tests: `cof_bonus_test.c` runs the game's own reader rule against a synthetic file, the baseline (locked) file and `runtimes/enhanced`'s (unlocked) file.
- **App** (launcher `321341c`): "Bonus room unlocked (FAMAS, axe and more …)" checkbox on Play solo and Host, unticked by default, remembered (`bonus_room`); writes `cof_bonus_room 0|1` into the session cfg. Readme and `docs/playtesting.md` updated.
- Package with both (2026-10-15 ~13:00): `evidence/tester-package-20261015/package-fix3/CoF-Story-Coop-Test1.zip`, SHA-256 `A18B3B1991BCF35B38278E88671D8E30ACEFEA6730CF9AD851B8E7189B259CA9`, 110,778,825 bytes, `xash.dll` `DA5E162A…` (engine `3a51b42b`: the dual deploy moved after the command-sender scope, which `test_enhanced_sender` pins); full suite 212 OK (`full-suite-fix3.log`); the smoke copy updated to it. **This is the current package**; `package-fix2/` is superseded.
- Live (2026-10-15 12:48-13:11, the user's go-ahead "launch it", smoke copy on `package-fix3`; Play solo, Chapter 1, bonus room ticked): `bonus-room state=open file=rewritten` on each map that reads the file; dual wield `inventorydualwield 1 2` -> `dualwield-switch userid=1 left=6 right=2` (switchblade + phone; the user did not comment on how it looked); no console window. `story_run_report`: 22 transitions OK (c_start <-> c_start2, the apartments 1-5), 7 maps, 16 exits, no refusals, errors or crashes; `companion-teleport` 72 (the known teleport rate). The user closed the game ~13:11. Not checked: the bonus room door and items in view, the Glock + flashlight pair.
- Third-person weapons (user: "Can we get weapons to appear properly in each other's hands?"): research `evidence/tester-package-20261015/research/thirdperson-weapons-re.md` (static): (1) story-only items have no p_ model in retail (phone, lantern, flare, camera, axe, FAMAS, booklaser, every dual pair) -> empty hands; (2) the generated Simon models keep HL's pitch controller (upper-torso twist) where police1 bends the torso -> sideways swing when aiming up/down; (3) long-gun holds off. Proposed (not approved yet): engine stand-in weaponmodel for empty ones (dual -> right-hand pistol, FAMAS -> M16) in `sv_frame.c`; copy police1's pitch controller in `tools/prepare_simon_models.py` and regenerate models on update. Asked the user which symptom they saw.

### Weapons in hand, up to three AI companions, companion movement (2026-10-15 evening)

User: "Can we get weapons to appear properly in each other's hands?" → "Do it. Fix everything."; then "Simon gets caught on corners a lot too, the AI. And let's also test three AIs because it's supposed to be two to four player co-op." Three read-only research agents: `evidence/weapons-companions-20261015/research/` (`pmodels-report.md` + the generator, `companion-movement-re.md`, `multi-companion-re.md`; scripts beside them). Ledger: `.superpowers/sdd/2026-10-15-weapons-companions/progress.md` (rulings). No reviewer subagents (standing preference).
- **Third-person weapons.** `tools/prepare_coop_weapons.py` (stdlib, 21 hash-pinned stock sources) makes 24 models under `cryoffear/models/coop/` at setup and update (`friend_setup.generated_models` → `make_models`, which now also *replaces* older generator output on update): p_ story items (axe, FAMAS, lantern, flare, mobile, camera, booklaser, browning) from the w_ meshes, the rifle/shotgun/TMP re-gripped for Simon (stock rifle points 40° up), `l_*` left-hand variants and `r_flashlight`/`r_lantern` for dual wield. Engine `sv_cof_weaponmodel.h/.c`: by weapon id (`m_iId`; table tested against `engine_mapping()`), precached at activation when the file exists (`weapon-models precached=N`), substituted in the player's entity state (`sv_frame.c`), body sequence overrides (FAMAS/Browning 36, camera/book 55, axe 50, dual 36), dual wield = light (or second pistol, P+0x1E74) in the right hand, the other (P+0x1E70) in the left as a follow entity (MOVETYPE_FOLLOW, number at the top of the edict range, not sent while the viewer watches a camera). Client guard (`cl_frame.c`): a weaponmodel the client cannot load is cleared before the retail client merges it (no NULL check there). Offline renders: `research/pmodels/renders/`. **Not seen in game yet.**
- **Simon models.** Aim pitch: controller 3 copied from police1 (Bip01 Spine ZR ±45; Simon twisted Spine3 sideways). Players 3/4: `cof_s3` (ModDB red hoodie), `cof_s4` (HLC light hoodie); engine slot map 0→s1, 1→s2, 2→s3, 3→s4 (cof_s2 when missing).
- **Companions (several).** Names Companion / Companion 2 / 3; a bot makes room for a joining friend while any other participant stays up and returns when a place frees; one syringe per cooldown for the party; hold fire for any player in line; one "coming to help" message; bots look after every person (nearest downed for CPR, rank k follows person k). **Wipe recovery fix (affected one bot too):** no checkpoint load while a bot is alive or someone revives the person. App: Play solo 1–3 AIs (`maxplayers` = AIs+1), Host "AI companions in the empty places" (`cof_companion` = places−1), Continue keeps the checkpoint's places; info key `bots` → hosting status names them. `Start-CampaignTest.ps1 -Mode companion -Companions 1-3`.
- **Companion movement** (live 2026-10-15: 72 teleports in 23 min, 40 of them doors): door follow-through (game move = faster than walking: keep the trail, walk to where the leader left, come out near the exit), placement out of sight (`SV_CoF_PartyPlaceUnseen`: ≥75° off view or no line of sight, ≥48 away, rank rows back, waits and after 3 s takes the least seen ≥30° off; no world-axis spot ahead), side probes at jambs/corners, stairs-aware ledge test, progress-based stuck, no teleports while the leader/bot is held (+0.5 s), personal space, rank distance (+48) and partner-in-the-way = arrived, party frame before companion frame, nav points +36 (nodes lie on the floor), teleport log `at=… leader_at=… to=…`. Not done: sprint (R1 fix 11), the unstick manoeuvre, trail-crumb placement candidates.
- App minors fixed: hosting status keeps the last state on a missed query; setup notes the copy (`%LOCALAPPDATA%\CoF-Story-Coop.txt`) so a newer package finds a copy elsewhere; `--copy` resolves.
- Decision left to the user: monster damage scales with every participant, bots included (1 AI ×1.68, 2 ×1.92, 3 ×2.16 vs alone ×1.44); left as retail.
- Verification: full suite **241 OK** (`evidence/weapons-companions-20261015/full-suite-w2.log`), engine builds exit 0 (`build-*.log`). Commits: launcher `9d2790d..720e972`, engine `d887312d..715170e8` (branch `tester-package`). Live: not yet (next: package-fix4, smoke copy update, Play solo with three AIs).

## 1.1 (2026-10-20)

- The user: "let's do some polish for 1.1. The night vision goggles caused constant flashing. Find any other bugs we
  need to fix." (the flashing = "Lighting strobes": the green light on walls and floor pulses, the screen tint
  steady). Then: "I want to test 1.0" → the zip's extracted exe (`evidence/release-20261020/package/unzipped/`,
  hash `476BB033…` checked) started at 04:10 for their own play; they ran Setup themselves (default copy
  `%LOCALAPPDATA%\CoF-Story-Coop`, record 1.0 build 4 at 04:15:45, about 5 min). Then: "a status thing in the
  installer when it's doing all the setup and stuff so people can see what it's actually doing in real time"
  (design approved in chat). Last: "Once it's all done and ready, go on the mod DB page and add the 1.1 files with
  proper change logs and everything."
- **Night vision strobe: cause found and fixed, seen live.** client.dll (`view.cpp`, RVA 0xB14AB) re-makes the green
  light (radius 900) every frame with `MY_AllocDlight(0)` and `die = time + 0.01`. Key 0 takes a new slot each
  call, so in any frame shorter than 10 ms last frame's light is still alive and two are drawn: measured 51% of
  frames with 2 lights (`cl_fixtimerate 0`: 46%), the doubled brightness flickering on and off. Fix: the renderer
  patch passes key -100 (`6a 00` → `6a 9c`; keys in use elsewhere: -1, -2, -8, -50, -66, -300, players -1..-32,
  entity indices), so the light reuses its own slot. Live (approved launch, `evidence/nightvision-20261020/`,
  `repro_nv.py`, `run.out` before, `run-fixed.out` after): after the fix 0 of 2497 frames drew two, one slot used.
- **Static decals: 1024, not 256.** The merged subway `cm_subway1st1` has 273 level decals; client.dll's
  `staticdecals[256]` (RVA 0x1BF368, 32-byte entries) dropped 17 ("too many static decals on level!"). The renderer
  patch moves the array into engine-allocated storage of 1024 (`AllocStaticDecal` bound at 0x57996, its base at
  0x579A3, the draw loop's base at 0x5879A; the relocation table has only those two references). Offline: the real
  client.dll's `AllocStaticDecal` in the run harness keeps 1024 patched, 256 stock. **Not seen live.**
- Engine commits (branch `tester-package`): `0b508f73` (NV), `c875e6ce` (decals); `xash.dll` `856725C9…`, its load
  note "[cof-renderer] client.dll's renderer: … %d static decals, one night vision light". The smoke copy runs the
  NV-only build `9829A338…` (swapped by `evidence/nightvision-20261020/swap_engine.py`, the earlier one kept as
  `xash-probe.dll`), its record still Test 2 build 3.
- **Setup and Update show what they do.** `friend_setup.Steps` reports (status, fraction, line) for each step,
  sized from the 1.0 install timings (copy .33, engine .05, models .05, art .01, maps .55; update .07/.07/.01/.85): a
  line per copied folder with its files and size, the Enhanced installer's own output streamed (Popen, "oem"
  decoding), a line per model, art file and merged map. Every line also goes to `cof-story-setup.log` in the copy
  (update appends; a failure writes "Stopped: …" first). The app (`coop_app.ProgressPanel`): the step, the bar, "M:SS
  so far" and a read-only box of timestamped lines. Tests: `SetupProgressTests` (5, with a Popen double carrying the
  real 1.0 installer output), `ProgressPanelTests` (3, a hidden real Tk window); a screenshot of the window checked
  the layout (`setup_screen_shot.py`, scratchpad).
- Version 1.1, `BUILD` 5; readme "WHAT'S NEW IN 1.1", "Coming from 1.0? … click Update", and "If Setup or Update stops,
  attach cof-story-setup.log".
- **Verification states.** Offline: full suite **505 OK, 1 skipped** (`evidence/release-1.1/full-suite.log`). Build:
  engine `c875e6ce` → `xash.dll` `856725C9…` (waf, `build-decals.log` in `evidence/nightvision-20261020/`). Package:
  `evidence/release-1.1/package/CoF-Story-Coop-1.1.zip`, SHA-256
  `E64AF336C9455D6E55873F857A8CF94680043675302B5F05BA26C9B95A61D711`, 110,796,618 bytes; exe `AF66E117…`; payload 1.1
  build 5, `vgui.dll` `40642512…`; commits launcher `0a2f1b5`, engine `c875e6ce`, FreeVGUI `66eca8dc`
  (`package.log`, `package-report.json`); the uni notes hidden by `.git/info/exclude` for the build, the line removed
  afterwards. Release check (`evidence/release-1.1/check_release.py` → `check-release.log`, all passed): zip files
  and CRC, readme, payload (no retired files, the 1.1 engine with its renderer note), the extracted exe's
  `--self-test`; a fresh `--install` from smoke-source (`--no-shortcuts`, 252 s: record 1.1 build 5, the 1.1 engine,
  29 groups merged, `cof-story-setup.log` 478 lines with all six steps); and the 1.0 players' path: a copy made by the
  1.0 exe (267 s), `--update` by the 1.1 exe (195 s: record 1.1 build 5, the 1.1 engine, a save kept, the log's four
  steps). Both test copies deleted. Live: the NV fix only (above, NV-only engine in the smoke copy); the decals and
  the 1.1 package itself not played. The user's own 1.0 copy (`%LOCALAPPDATA%\CoF-Story-Coop`) is untouched.
- ModDB text for the 1.1 file: `evidence/release-1.1/moddb-text.txt`.
- Other findings in the smoke copy's logs (10-19 night): not ours: `save/save_*.wav`, `krypande/k_crawl*`, `pl_wade*`, `models/grenade.mdl` are
  referenced by retail but absent from the game files; "X found the Y" carries two marker bytes on purpose
  (`CoF_StripPayload`). College: no repeat of the classroom loop; at 23:42:43 the player came through `oexd3_12`,
  was teleported to `c2_lm1` facing `oexd3_22` and used it within a second (back into c_college1dark), then went on
  through cm_hamn normally (likely a second press, not the reported bug).

### After 1.1 went up: a player's log (2026-10-20)

- 1.1 is on ModDB (its downloads page said "5 hours ago" when checked at about 10:40). The user then sent a log with no comment:
  `story-session-20261020-125132.log` (copy in `evidence/warp-20261020/`). It is from another player's 1.1 copy
  (renderer note with 1024 decals and one NV light; the clock about 3 h ahead), host "Xaier", guest "Wolf",
  c_intro → c_nightmare → cm_start, 4 minutes.
- **J in a room only one fits: fixed offline, not seen live.** At the end of c_nightmare the cof_teleports
  `teleea45` and then `teleea` took Wolf into the exit room: `teleea` (639 -2951 -747), its trigger_changelevel `*57`
  64×66 units, the exit to c_start. Xaier got "Wolf went somewhere you can't follow. Press J to join." and then the
  exit hold's "Press J to join.", but every J gave `warp-refused reason=no-room` ("No room next to Wolf."): none
  of `CoF_PartyLanding`'s 20 spots fits beside a hull in that room. The hold let Wolf go after 11.4 s
  (`exit-forced`), and the level change carried Xaier anyway (`arrival-overlapped`). The teleport-follow and the
  arrival already share the spot in such rooms (the `kunitei` fix); J did not. Now `CoF_PartyWarp` falls back to
  `CoF_PartySharedSpot`: the partner's spot, feet on their floor, unless the caller's hull meets the world there or
  no floor is within 64 units (a ladder); the log line gets ` shared=1`. Engine `3578c354`; harness test (launcher
  `7e450f9`, `crowded` knob: shared spot, crouched partner, ladder refusal); `test_enhanced_party`,
  `test_enhanced_respawn_spot`, `test_story_run_report`, `test_warp_bind` OK. Build: `tools/build_enhanced.cmd` →
  `xash.dll` `643BFC42…`, no warnings (`evidence/warp-20261020/build.log`).
- **1.1.1** (the user chose "1.1.1, works with 1.1"): `VERSION` stays "1.1" (the join check, so 1.1 and 1.1.1 join
  each other; the fix is host-side), `VERSION_LABEL` "1.1.1", `BUILD` 6 (1.1 copies are offered Update); readme
  "WHAT'S NEW IN 1.1.1", "1.1 and 1.1.1 can play together", "Coming from 1.0 or 1.1?" (launcher `b58baae`). Offline:
  full suite **505 OK, 1 skipped** (`evidence/release-1.1.1/full-suite.log`). Package:
  `evidence/release-1.1.1/package/CoF-Story-Coop-1.1.1.zip`, SHA-256
  `9498F12B442868F84E4DD190E29B66286C2C7EC6CB046A63D5582AE15C42261F`, 110,799,327 bytes; exe `2CC984AF…`; payload 1.1
  build 6, `xash.dll` `643BFC42…`; commits launcher `b58baae`, engine `3578c354`, FreeVGUI `66eca8dc` (uni notes
  hidden by `.git/info/exclude` for the build, the line removed). Release check
  (`evidence/release-1.1.1/check_release.py` → `check-release.log`, all passed): zip, readme, payload (the J fix's
  ` shared=1` in the engine), `--self-test`, a fresh install (249 s, record 1.1 build 6, 29 groups), and a copy made
  by the 1.1 exe (220 s) updated by 1.1.1 (159 s, a save kept). 1.0 → 1.1.1 not run (same update code as 1.0 → 1.1).
  Live: nothing; the J fix not played. ModDB text: `evidence/release-1.1.1/moddb-text.txt`.
- **"Couldn't see anything": the story blur stuck on both players. Fixed offline, not seen live, not in a package.**
  The user, about the same log: the player "couldn't see anything. It made them blind." Cause (map data, the
  decompilation and the retail hl.dll agree): `cof_blur`'s Use flips `m_iBlurActive` (+0x1EBC, hl.dll 100A5BB0) of
  the activator only; the field is saved with the player, UpdateClientData sends "Blur", and client.dll draws a
  radial smear (`gl_screenblur` 1 by default, about 60 copies at `gl_opacity` 0.5). c_nightmare: `nightmulti`
  (trigger_once) flips blur2 at 0 and 39 and blur3 at 40.6, so the host (alone then) is blurred from 12:48:17 (its
  `fadehehi` fade-in at 40.5 is in the log then); Wolf joins at 12:49:00 unblurred; `multispotea2` (blur3, `fadelaosee`,
  `teleportea45` at 0, `teleportea` at 5) ran with Wolf as activator (`teleport-begin userid=2`), so Wolf's flip
  turned Wolf's blur on and the host's stayed on. Both carried it into cm_start, where `slower_eyes_mm`'s four flips
  (0, 20, 30, 34) change nothing. The host closed an options page at 12:49:28 (`FS_SaveVFSConfig`, the menu's
  `host_writeconfig`), switched `gl_vbo` on at 12:51:17 (`R_GenerateVBO` mid-map), typed `kick all` and quit. Not
  logged then: "Blur" was not in the trace. Scan of every retail map (`blur_scan.py`, session scratchpad): 30 maps have a
  cof_blur; every story map's flips come in pairs; odd ones only in the endings, the suicide maps and `c_cof_*`; split
  across triggers different players can fire in c_nightmare, c_down, c_forest6, c_rumpel2, c_subwaysick1/2,
  c_theevinas, c_tricky, c_volcomator1/3.
  Fix (engine `24ca59d8`, `90cbb658`; `sv_cof_blur.h/.c`, cvar `cof_coop_shared_blur` 1): one blur for the
  party. After the game frame a flip by anyone sets everyone, a joiner/arrival takes the party's, a morphine trip
  (`m_bIsTripping` +0x2278, MorphineTrip 100E8D70) stays the tripper's own until it ends; a story level change from a
  map that does not hand the blur on (`CoF_BlurCarried`: "ending", "suicide", `c_cof_`) clears everyone's before
  `SaveGameState` (`SV_CoF_BlurLeaving` in `SV_ChangeLevel`); a checkpoint loaded on such a map starts the party off
  (`SV_CoF_BlurCheckpointLoaded`), so the player's stuck cm_start autosave is healed. "Blur" is in the trace now;
  logs `phase=blur-shared userid blur reason from`, `phase=blur-cleared userid map`. Tests (launcher):
  `test_enhanced_blur` (rule scenarios incl. the log's sequence, retail bytes, wiring), the retail harness block,
  the server-transition harness stub. Not covered offline: the client draw itself, the skip's fast-forward.
  Verification: offline, full suite **545 OK, 1 skipped** (`evidence/blur-20261020/full-suite.log`; the count
  includes another session's new, uncommitted `test_app_update`/`test_release`, which were in the tree then). Build:
  `tools/build_enhanced.cmd` exit 0, no warnings (`evidence/blur-20261020/build.log`), `xash.dll` `F161F1E2…`.
  Package: none (1.1.1's zip `9498F12B…` does not have it). Live: nothing.
  Also from the log, not changed: the 17 s of black at c_nightmare's end is the map's `fadelaosee` (hold 300 s, the
  engine caps the message at 16 s) to every player, held by the exit wait; with the 1.1.1 J fix the partner can join
  at once.
- Deferred minor: the payload's `cof-enhanced/release.txt` still says `cofenhanced 0.4.0-test3`
  (`package_enhanced_story.VERSION`), as in 1.0 and 1.1.
- Seen, not changed: Xaier pressed Enter 9 times in the intro, and every second press cancels the skip (Enter does
  not autorepeat in game, so they were real presses). Xaier typed `kick all` in the console: stock Xash allows the
  console in any game with more than one slot (`cl_parse.c`, "allow console in multiplayer games"), so the
  release's "no console" holds only for single-slot games, and co-op always has two or more. The log's banner
  still reads `1ab3793e-dirty, tester-package` (waf's configure-time stamp). Wolf's 31 `dodgeforward` in 3 s
  during the chase is the dodge key. Missing retail sounds as before.

## Release 1.0 (2026-10-20)

- The user: "make me the public facing version that I'm going to send out to people. Get rid of all the admin stuff,
  all the dev work stuff. Get us a like release version of it." Chose (chat): version **1.0**, "Report a bug" → their
  Discord `https://discord.gg/4uwV6jmmY7`, build now with the college door bug listed as a known issue.
- Changes (launcher, branch `tester-package`): `tools/coop_play.py` `VERSION`/`VERSION_LABEL` `1.0`, `BUILD` 4 (counts
  on from Test 2's 3, so test copies are offered Update), `REPORT_URL` (was the private GitHub `ISSUES_URL`);
  `tools/coop_app.py` footer `footer_text()` "Cry of Fear Co-op 1.0" (no "early test build"), `report_bug()`;
  readme `tools/tester_readme.txt` → `tools/player_readme.txt` (player wording, known issues, Discord, fan-project line);
  `tools/friend_setup.py` `RETIRED_FILES` (Enhanced's README.txt, Install.cmd, Uninstall.cmd, the cheats guide
  `cof-enhanced/CHEATS.md`, `EXPERIMENTAL-STORY.txt`, `STORY-START.txt`): `build_tester_package.player_payload` leaves
  them out of the engine payload and `update` deletes the test packages' ones from a copy; `package_enhanced_story`
  notice text and the `release.txt` stamp lose "tester"/"experimental". Engine unchanged (`1ab3793e`, the door-use
  probe build, `xash.dll` `61C160EE…`): the story log and the probe only write to `story-session.log`; the test gun
  and cheats stay unreachable (the app writes `cof_test_gun 0`, `sv_cheats 0` every start; no console).
- Not changed: the engine's on-screen Enhanced stamp ("cofenhanced 0.4.0-test3 (…)", top right for the first seconds:
  upstream's version, compiled in); the hidden CLI flags of the exe; `corresponding-source.zip` still carries the
  local tools and tests (the GPL source offer).
- **Verification states.** Offline: the new tests watched failing first (12 failures/errors), then green; full suite
  **497 OK, 1 skipped** (`evidence/release-20261020/full-suite.log`). Build: no engine rebuild (the built `xash.dll` is
  newer than its sources; `package_enhanced_story` plan check). Package: `evidence/release-20261020/package/
  CoF-Story-Coop-1.0.zip`, SHA-256 `68E92F8B7CD86D0354F67377DBE8519EE5B4A25D3A99C568ADDA02921C63FFB1`, 110,783,342
  bytes; exe `476BB033…`; payload 1.0 build 4, `xash.dll` `61C160EE…`, `vgui.dll` `40642512…`; commits launcher
  `81a95f8`, engine `1ab3793e`, FreeVGUI `66eca8dc` (`package.log`, `package-report.json`); built with the uni notes
  hidden by `.git/info/exclude`, the line removed afterwards. Release check (`check_release.py` →
  `check-release.log`, all passed): the zip's six files and CRC, the readme (1.0, Discord, no test/dev words), the
  payload without the retired files, `release.txt` stamp `cofenhanced 0.4.0-test3 with story co-op`, the extracted
  exe's `--self-test`, and a fresh `--install` from the extracted exe (smoke-source, `--no-shortcuts`, exit 0 in
  330 s): record 1.0 build 4, engine and stock hl.dll, no retired files, no listenserver.cfg, story cfg with the test
  tools off, 29 groups merged; that 5.2 GB test copy was deleted afterwards. Live: **none** (no game launched; the
  user's own game on the smoke copy kept running, its `xash.dll` unchanged). Update over a Test 2 copy: unit-tested
  only (the smoke copy was in use).
- Deferred minors: `cof-enhanced-version.txt` (written by upstream's installer) still says "uninstall=run
  Uninstall.cmd"; the host-check refusal names the other side's raw build ("theirs: test2").
- Not done (user decisions): nothing merged, pushed or published; the zip is for the user to send.

## College classroom doors lead back in (2026-10-19 night; cause open; a diagnostic is built and in the smoke copy, not played)

- The user, on the smoke copy (Test 2 build 3, merged maps): "Got all the way to college, but then the doors in
  college for the classroom where you get the screwdriver, when I try and leave, it just brings me back into the same
  room, both doors." Log `evidence/tester-package-20261015/smoke/CoF-Story-Coop/story-session.log` (session from
  21:05), cm_college2, c_college1 section (dz +1024). First visit fine: 21:52:06 in by door 2 (`oexd2_1`), 21:52:11
  screwdriver, 21:52:18 out by door 1 (`oexd1_2`). Then c_college2 and back (21:52:33-21:52:40; the Continue autosave
  is 21:52:37, in c_college2), in by door 1 (21:53:22 `oexd1_1`), and every use from inside went back in: door 2 →
  `oexd2_1` at 21:53:29, :32, :36; door 1 → `oexd1_1` at :42, :46, :49. No refusal or locked message.
- Each doorway has two inter_door brushes 7 units apart: retail *40/*41 on the room's face (x -286..-285, to the
  hallway), *23/*42 on the hallway's face (x -293..-292, back into the room); door 2 also has func_wall *7 (x -290)
  between them. From inside, the game used the hallway-face brush, passing over the room-face one in front of it.
- Ruled out offline (scripts and outputs in `evidence/college-doors-20261019/`): the merge (the door models and the
  world's hull 0 match retail +1024; `door_hulls.txt`; `merged_world_doorway.py`: 0 of 459,940 cells differ); the
  doors' key values (unnamed, no master; nothing can retarget or disable them); the engine (nothing writes a door's
  owner or solid; traces have no per-section filter); the companion (over 128 units away at 21:53:29).
- What the game does (decompilation player.cpp:3468-3610, CoF_InterDoor.cpp): PlayerUse traces 80 units from the eye
  and uses what it hits if usable. Otherwise it ranks every usable entity within 80 of the origin by the dot of the
  view with the box-clamped direction (best above 0.7; equals go to the lower edict). Square in front of a two-brush
  door both brushes rank equal; off to one side the FAR brush wins. So the symptom fits the trace not returning the
  room-face brush and the fallback picking the far one. Why the trace would miss only after the round trip: unknown.
- Diagnostic (engine `1ab3793e`, launcher tests `a8c9d1a`): `engine/server/sv_cof_useprobe.c`, in the story frame
  before the party code. The frame a player starts an inter_door it logs `cofstory phase=use-probe` (door, eye, view,
  the engine's trace with the player skipped), `use-probe-hit` for each brush or body on the 80-unit ray, nearest
  first (the door just used is listed too: its owner is now the player, so the engine's own trace skips it), and
  `use-probe-door` for each inter_door within 80 with the fallback's dot. Observes only (trace globals restored);
  gated by `cof_story_trace`; human players only.
- Workaround meanwhile: `noclip` in the console (sv_cheats is on in the smoke copy's `listenserver.cfg`).
- More offline analysis (`evidence/college-doors-20261019/`): `tie_rounding.py`: with the eye in line with a doorway
  both panels clamp to (-L,0,0) and tie, and float rounding decides: at door 2 the hall-side brush wins in 11.1% of eye
  x positions, at door 1 in 89.7%, so three door-2 failures from the fallback at the landing spot are unlikely: the
  live trace probably returned the hall-side brush, or the user stood off-centre. `ent_classes.py`: retail c_college1
  has no entity that can change another's fields (no trigger_changevalue and the like).
- Automated repro (the user approved one run; 2026-10-19 23:38-23:40; `repro_doors.py`, output `repro-run1.txt`, log
  `repro-20261019-234022.log`): fresh `map c_college1` merged, one companion, a section round trip c_college1 →
  c_college2 → c_college1 through the exit triggers; afterwards both doors used from inside took the room-face brush
  (door 2 → `oexd2_2`, door 1 → `oexd1_2`): **not reproduced**. Probe after the trip: `eye=-265,-912,1096 view=0,180
  trace=0.287 hit=92`, ray hits #377 *55 0.250, #92 *21 0.287, #378 *56 0.337. Gaps: the script's uses from the hallway
  registered nothing (aiming: `tools/live_loop.py` `look()` sends the angles and fixangle as two RCON packets and one
  view came out at yaw 143; send both in one command joined by `;`), so the user's way in through the doors was not
  exercised; the user also arrived by changelevel from cm_hamn (a save restore), took the screwdriver and used door 3
  and doorch1 before the trip. Still open: the next live failure's `use-probe*` lines decide it.
- 2026-10-20 ~00:00 (dated observation): the user's own play resumed on the smoke copy at their request, probe active.
- **Verification states.** Offline: `test_enhanced_use_probe` watched failing (no module), then 2 OK; trace and party
  tests 8 OK. Build: `tools\build_enhanced.cmd` exit 0 (`evidence/college-doors-20261019/build-probe.log`), `xash.dll`
  `61C160EE…`. Smoke copy: `xash.dll` swapped by `swap_engine.py` (the install record's engine hash moved to match;
  build 3's `14876831…` kept beside it as `xash-build3.dll`); not a package, the copy still says Test 2 build 3; nothing
  was running, nothing launched. Live: **none**.

## Respawn crash after the second boss (2026-10-19 evening; fixed, built, offline-tested; not played)

- The user, playing the smoke copy on Test 2 (merged maps, F11 stamina): "Game crashed when I got killed by second
  boss." Log `evidence/tester-package-20261015/smoke/CoF-Story-Coop/story-session.log` lines ~71100-72080: 19:44:41
  death (`respawn-sooner delay=10`); 19:44:52 the respawn's messages, then `[cof-sections] changelevel from=c_sewer2
  to=c_sewer landmark=kalalale file=cm_basement`; client.dll faults in `CL_ParseUserMessage` (RVA 0x580A8, in
  `CreateDecal`, called from `MsgCustomDecal` at 0x594E9; base 0x65300000).
- Cause, two links. (1) hl.dll respawns a player on `gpGlobals->startspot`, the landmark the map was entered by
  (decompilation player.cpp:7003-7134); in cm_sewer3 that is `kalalale` in the c_sewer2 section, inside
  trigger_multiple *20 (fires `chsw55`, the changelevel to c_sewer); the move after PreThink touched it and the exit hold
  said go (a solo human in the exit). Offline count (`evidence/map-merge-wave3-20261019/spawn_in_exit.py`/`.txt`,
  `spawn_triggers.py`/`.txt`): **180 of 195 entry landmarks stand in an exit trigger**; 0 of 81 first
  info_player_starts do, but 59 of 81 stand in a map-start script trigger, so "respawn on the first start" (tried,
  reverted) is not safe either. (2) The respawn's Precache resent the init messages (every `env_static_decal` as
  `customdecal`); `SV_InactivateClients` keeps queued messages across a CoF level change; the listen server loaded
  cm_basement into the shared world model, and the client ran the old map's decals on a world `R_NewMap` had not set
  up (NULL `polys`).
- Fixes (engine `188b143b`, `19224602`; launcher tests `1e91c73`, build bump `34f6334`): `SV_RunCmd` notes
  `SV_CoF_PartyDead` before PreThink and, after a PreThink that respawned, `SV_CoF_PartyRespawned` moves the player
  (before their move touches anything) back where they came into the map (recorded at `SV_CoF_PartyTransitionArrive`)
  or section (`SV_CoF_PartySectionMove`); a player carried in dead uses a partner's spot; no record (new spawncount):
  left where the game put them, logged `cofstory phase=respawn-kept reason=no-entry`; logs `respawn-moved` otherwise.
  Client: `CL_ParseUserMessage` drops `customdecal` while `cl.world_generation != Mod_WorldGeneration()`.
- Not fixed (seen in the same log): ~71k ScreenFade messages to the companion bot; the arrival regroup could still
  place a guest in a back trigger (edge).
- **Verification states.** Offline: new tests watched failing first (`test_enhanced_respawn_spot`,
  `test_enhanced_stale_world`, harness block in `tests/engine/cof_party_harness.c`); full suite 488 OK, 1 skipped
  (`evidence/respawn-crash-20261019/full-suite.log`). Build: `tools\build_enhanced.cmd` exit 0, no warnings
  (`build.log`), `xash.dll` `14876831…`. Package: `evidence/respawn-crash-20261019/package/CoF-Story-Coop-Test2.zip`
  (`8910B255…`; exe `85DBDD4B…`; payload test2 build 3; commits launcher `34f6334`, engine `19224602`, FreeVGUI
  `66eca8dc`), built with the uni notes hidden by `.git/info/exclude` and the line removed afterwards. Smoke copy:
  updated with `update_smoke.py` -> `update-smoke.log` (test2 build 3, `xash.dll` `14876831…`, 29 groups merged,
  `cryoffear/listenserver.cfg` kept; nothing was running, nothing launched). Live: **none**; the user's Continue is the 19:42:01
  autosave at the c_sewer3 entry, just before the boss.

## Test 2 without launching: wave 3 merged maps, ambient loops, campaign check (2026-10-19; built and offline-tested; nothing played)

- The user: "Without ... launching a bunch of fucking instances, all without launching the game, get the game as done
  as you can ... merge the maps so someone could feasibly run through the whole game." No game, live check or
  instance was launched for any of this. Ledger `.superpowers/sdd/2026-10-19-finish-offline/progress.md`
  (git-ignored) carries the rulings.
- Changes, all on `tester-package` (launcher `e2f083a..`, engine `e5fed43e..b0edceee`), not merged, not pushed:
  - **Ambient loops per section** (engine `19497c42`, `sv_cof_loops.*`; launcher `b3862c2`): every looping
    `ambient_generic` (no spawnflag 32, any attenuation) of a merged map belongs to its section: one that starts
    with the map is held out of the sign-on, a player moving between sections gets the stops of the section left
    and the starts of the section entered (`cofstory phase=loops-moved` in the log), a restore does not resend them
    (hl.dll's own `CAmbientGeneric::Precache` re-emits the active ones). The planner's play-everywhere refusal is gone.
  - Planner: the worldspawn `sounds` key (CD track) absent = 1 (`b3862c2`).
  - **Campaign exit check** `python -m tools.map_merge.campaign MAPS` (`8751fa2`): routes all 197 story exits as
    `CoF_SectionsRoute` does and flags any exit that works unmerged but not merged.
  - **Wave 3: 13 more groups** (`c04fba9`; table and choices in `docs/research/2026-10-17-merging-the-rest.md`'s
    status block): c_start + c_start2; c_apartment3sick + 4sick + c_down; c_basement + c_sewer; c_park2 + c_stad;
    c_park3 + c_roofs + c_roofs2; c_subway1st1 + c_subtunnel1 + c_subtunnel2; c_subway2st1 + c_subwaysick2;
    c_trainscene + c_trainride + c_trainride2; c_bridge + c_forest1; c_ending1/2/3 + their b maps; c_ending5b + 5c.
    29 groups: 99 section moves, 75 merged loads, 23 plain loads of the 197 exits (16 groups: 59/44/94), no
    problem. Still plain: c_intro, c_nightmare, c_city, c_avlopp, c_apartmentsick, c_subwaysick1, c_asylum1,
    c_broscene, c_ending1c-3c, c_ending5.
  - Setup reserves the merged maps' room: the members' size of every enabled group (`11784b9`); the 29 groups take
    570 MB (members 664 MB) and about 2.5 min of setup (`sizes.txt`). The readme's "About 6 GB" still holds.
  - **Test 2, build 2** (`fa82fda`): `cof_build test2`; a Test 1 host and a Test 2 joiner are refused. The readme says
    what "Merged maps" does, that the host can untick it and restart the chapter, and asks reports whether it was on.
  - **A companion's weapons no longer come back on a revisit** (the live runs' open c_start revisit, 13 weapon_*
    against 7): the first companion's syringe, phone and knife were restored at its old spot, and the second
    companion's three arrived with the level change (the game's change list carries what is near the exit) and stayed
    at its spot. The mark from `48d8a60` asked the owner's `FL_FAKECLIENT` edict flag, which the game's player Spawn
    clears on a bot (every logged `phase=player` line reads `fakeclient=0`); `sv_save.c` `CoF_TransitionFakeClient`
    now asks the client's `FCL_FAKECLIENT`, and `CoF_TransitionSkipSavedEntity` leaves a companion's items out of the
    arriving transition too (the engine adds the companions again with their own kit; a companion never picks
    anything up). Tests `test_a_companion_is_known_by_its_client_flag` and the arriving case (each RED, then GREEN).
    Expected live (merged maps off, since c_start + c_start2 is now one file): a `c_start -> c_start2 -> c_start`
    revisit with one companion counts 7 `weapon_*` again (`run_live.py fixes revisit`).
  - Lint (`c2ddaf0`, `21600c0`); docs (`787c186`: research status block, the spec's sound and fog bullets as built).
- Commands and evidence (`evidence/map-merge-wave3-20261019/`, git-ignored): candidates `try3.txt`,
  `try4-trains.txt`; `record3.sh` -> `runs/record3.log`; `campaign-wave3.txt` (before: `campaign-wave2.txt`);
  `exits.txt` (every story map's exits); `renames3.txt`; `autos.txt` (no later section of any group has a
  `trigger_auto`, so nothing fires early at the file's load); `sizes.txt`; tests `runs/merge-tests-wave3.log`
  (62 OK, 1 skipped), `runs/sections-wave3.log` (29 OK); engine `runs/build-wave3.log`.
- Also checked offline: no wave 3 rename touches a fixed name (`ryesno` etc.) or the bonus door (`unlockablesdoor`:
  c_start and c_bridge are first sections); every credits map's file keeps `c_ending`; the engine finds merged
  files by `FS_Search("maps/cm_*.sections")` (no count limit); a save is ~230-290 bytes an entity (smoke copy
  `.HL1` files), so cm_park3's 1808 entities are far inside `SAVE_HEAPSIZE` (4 MB).
- **Verification states.** Offline: full suite `python -m unittest discover -s tests` 484 OK, 1 skipped before the
  companion fix (`runs/full-suite-wave3.log`), 485 OK, 1 skipped on the final tree (`runs/full-suite-final.log`). Build:
  engine `b0edceee`, `tools\build_enhanced.cmd` exit 0, no warnings (`runs/build-companion-items.log`), `xash.dll`
  `533ABC47…`. Package: `evidence/test2-20261019/package/CoF-Story-Coop-Test2.zip` (`A119A0CD…`, 110.8 MB; exe
  `7165F943…`; payload `test2` build 2, `xash.dll` `533ABC47…`; commits launcher `97a80ab`, engine `b0edceee`,
  FreeVGUI `66eca8dc`; `package-report.json`), built with the uni notes hidden by `.git/info/exclude` and the line
  removed afterwards; the exe's bundled `tools\map_groups.json` holds the 29 groups (read with PyInstaller's
  `CArchiveReader`, nothing run). Not smoke-installed: setup runs the app, and nothing was run. Live: **none**. Wave 3's 13 groups, the per-section
  loops and the Test 2 package have never been played; before these changes wave 1 was played and wave 2's doors
  were checked live by script (section "Merged maps wave 2").
- Known gaps: `SV_RestartAmbientSounds` (demo recording's full update only) resends the host's loops to everyone;
  client.dll lights models by map name (`forest1`-`forest6`), and c_forest1 now plays inside `cm_bridge`
  (cosmetic, unchecked); monsters chasing through a door do not follow in a section move (all merged groups).
- 2026-10-19 evening, the user: "launch it i want inf stamina too". The smoke copy
  (`evidence/tester-package-20261015/smoke/CoF-Story-Coop`) was updated to Test 2 with `friend_setup.update`
  (`evidence/test2-20261019/update_smoke.py` -> `update-smoke.log`: test2 build 2, `xash.dll` `533ABC47…`, all 29
  groups merged, no shortcuts), and only that copy got `cryoffear/listenserver.cfg` (`sv_cheats 1` at every map
  start, F11 = `cof_infstamina 1`; delete the file to play without). The app was opened for the user to play.
- Open for the user: where "Report a bug" goes (`coop_play.ISSUES_URL` is the private repo: a tester without an
  invite gets a 404); merge/push of `tester-package`; sending the zip.

## Decompilation survey and the user's playthrough (2026-10-18)

- **Playthrough 2026-10-18 00:05-01:54** (the user: "I'll play it"; the agent opened `CoF Story Co-op.exe` in the
  smoke copy, after first opening the bare `CoFLaunchApp.exe` by mistake): Chapter 1 with merged maps on, 2 AIs, through
  cm_apartment1/3, c_apartmentsick, c_basement, c_sewer, into cm_sewer3 (wave 2), tape save `tape1` there. The user
  reported "Simon's phone can't be used to dial numbers": not a bug. The dial screen opens only with `weapontoggle`
  (Z) while the phone is the active item; the log showed number keys (quick slots) and F but no Z until after the
  agent said so; then `phonenumber 543214` worked (call, code 5247, door). Typed number-row digits and Enter also dial.
- **Survey of `caixax/cof-decompiled`** (the user: "go through the entire ... decompiled thing and write and note any
  ... thing relevant to our development"): a matching decompilation whose hl.dll and client.dll are byte-identical to
  ours. Merged result `docs/research/2026-10-18-cof-decompilation-survey.md`, slice notes in
  `evidence/decomp-survey-20261018/` (git-ignored). The untracked uni notes (another session) say not to take code,
  offsets or names from a decompilation into the repo; the user decided to implement the findings. Highlights: no engine
  offset disagrees; our bugs (ladder busy test, missing `usercmd.is_donator`, far clip 1.73 x retail, `HealthCap`,
  story-item duplicates, model attachment 3); new soft-locks (c_apartmentsick chainsaw boss, c_start padlock for a
  guest, wheelchair death, c_lake boat, crazybitch grabs the host); open issues answered (join spam = InitHUD per new
  bot object, `P+0x24D0`; costumes = host's scriptsettings lines 51-62; client hides players while `P+0x2038`
  ViewEntity is set; fog lost on respawn); merged-map rain and 3D-sky singletons. Since committed (`e00a3ec`) at the
  user's request to implement it all (next section).

## Fixes from the decompilation survey (2026-10-18, built and offline-tested; live checks pending)

- The user: "I want you to implement all the ... things we learned from the decompile" (taken as their decision on the
  uni notes' caution about decompilation-derived code). Plan `docs/superpowers/plans/2026-10-18-decomp-fixes.md`
  (46 tasks), executed inline with TDD, one commit per task in each repo touched; every ruling is in the ledger
  `.superpowers/sdd/2026-10-18-decomp-fixes/progress.md` (git-ignored). Launcher `6279028..` (40 commits), engine
  `0e6e768d..7109ae0e` (39), both on `tester-package`: not merged, not pushed, package built (below), nothing installed in
  the smoke copy, no game launched. Full suite 451 OK (1 skipped); engine build `xash.dll` `7922C7EB...`
  (`build-local-20260927`). Every retail fact was re-checked against the retail hl.dll/client.dll bytes
  (`tools/hlre.py`); gates and client patches are verified against the stock bytes by tests.
- Package (the user: "build it", 2026-10-18 15:28): `evidence/decomp-fixes-20261018/package/CoF-Story-Coop-Test1.zip`,
  SHA-256 `2019996E...`, 110,765,705 bytes; exe `678BFAF7...` (`--self-test` passed); payload `xash.dll` `7922C7EB...`,
  `vgui.dll` `40642512...`; from launcher `350e346`, engine `7109ae0e`, FreeVGUI `66eca8d`; suite 451 OK (1 skipped)
  on that tree (`full-suite.log`, `build.log`, `package.log` beside it). The builder refuses an untracked file, so the
  other session's `docs/research/2026-10-17-uni-coop-notes.md` was hidden by a line in `.git/info/exclude` for the
  build only (removed after; the file itself untouched). Not installed in the smoke copy; not sent to anyone.
- No-launch release checks on that zip (the user: "do all the offline stuff", 16:41-16:48): extracted zip = exe,
  readme, `source-and-licences` only; exe `--self-test` 0; Windows Defender (`MpCmdRun -ScanType 3`) no threats in
  the exe or the zip. Fresh `--install` from `smoke-source` → `evidence/decomp-fixes-20261018/fresh/` (195 s); the
  previous package (`overnight-20261017`) installed → `update/`, marker saves and a config edit added, then the new
  exe's `--update` (115 s): markers unchanged. Both copies (scratchpad `verify_copies.py`): `validate_runtime`, record
  test1/1, payload binaries, retail hl/client.dll untouched, `max_edicts 4096`, story cfg, 24+4 models, 8 art files,
  16/16 merged groups with identical hashes, no stray files. No shortcuts or copy pointer written. Finding:
  `VERSION`/`BUILD` are still test1/1, so the app's GUI opens an existing Test 1 copy (the smoke copy) without
  offering the update, and the host/guest check cannot tell this build from older Test 1 builds; bump before sending.
- **Crash fixed (the user's playtest 2026-10-19 07:42, smoke copy on this package: "companion died during chainsaw
  fight and crashed")**: c_apartmentsick, the chainsaw boss's grab killed the AI companion; `Crash: 6F9C7E8D` =
  hl.dll 100C7E8D in CHalfLifeMultiplay::DeathNotice, reading the NULL inflictor's classname (killer not a client).
  PlayerKilled (100C9160) passes g_pevLastInflictor (102266FC), which the game clears after each TakeDamage
  (1008C0E2/C10A/C128); the boss's grab think (1007D93A) calls the victim's Killed itself, so in multiplayer any
  monster kill outside TakeDamage crashed (retail behaviour; single player has no DeathNotice). Fix: gate
  `death-notice` (0xC916B `FF 75 10` -> `53 90 90`, push the killer), always on (no cvar). Reproduced live first in
  the fresh copy (`run_live.py fixes boss-kill`: open the arena door, player notarget; same stack), then with the fix:
  two grabs, "Companion died.", `boss-respawned ok=1` twice, no crash. Tests: `test_enhanced_gates` (bytes against
  the retail hl.dll, always wanted).
- Live release checks (the user: "I'm ready to do the runs"; `evidence/decomp-fixes-20261018/run_live.py`, fresh copy,
  silent, windowed 960x540, `m_ignore 1` after the user moved the camera by accident, launcher `08a4c32`): smoke, the
  13 gates toggled live, cutscene hold, rain mirror, attic camera models, levels, checkpoint, death/CPR, both
  flashlight modes, follow (22 changes/min, 12 door teleports) and 15/17 chapter starts pass (the only errors: SDL's
  gamepad gyro "Calibration failed", a controller on the PC). Open: on a c_start revisit 13 weapon_* vs 7 (three at
  the first visit's start spot; cause found and fixed offline 2026-10-19: the dropped companion's items, section
  "Test 2 without launching"), the ladder fired by name left the player frozen (to rerun
  with use), night vision check needs the item used. Paused by the user before the merged groups, two-client run and
  the 197-exit sweep (`exits` in the driver); the session log drops the server lines of the first map's load.
- Cvars (all default on; 0 = the game's own behaviour): `cof_donator`, `cof_costumes`, `cof_coop_nightvision`,
  `cof_sp_carry` (11 hl.dll gates), `cof_simon_voice`, `cof_coop_weather` (a gate plus a per-player mirror),
  `cof_coop_player_state` (muffle, cutscene flag, spread statics, `gDualWeapon`, `room_type`), `cof_coop_heldlights`,
  `cof_coop_section_sky`, `cof_coop_section_view`, `cof_coop_camera_models`; client: `cof_coop_hud`,
  `cof_coop_sp_save_labels`, `cof_farclip_scale` (1 = retail); and `cof_coop_friendly_fire` (default 0: nobody's
  shots hurt another player, the AI companions included).
- New engine modules: `sv_cof_gates.*` (byte gates on the verified hl.dll), `sv_cof_rescue.*` (soft-locks),
  `sv_cof_guard.*`, `sv_cof_fog.*`, `sv_cof_voice.*`, `sv_cof_weather.*`, `sv_cof_globals.*`, `sv_cof_heldlight.h`,
  `sv_cof_sky.*`, `sv_cof_view.c`, `sv_cof_camera_models.*`, `common/cof_strip.h`, `common/cof_abi.h`;
  client: `cof_coop_hud.*` (two client.dll gates), `cof_hud_health.h`, `ref/gl/gl_cof_farclip.h`; the renderer patch
  has 30 sites (particle sources, Cg water). Map merging: the engine's composer and `tools/map_merge/rules.py`
  rename every key the game reads as a name (a test compares the lists); the planner refuses first-of-class
  entities in two members and more than 16 transition pairs, and notes rain, 3D sky, fog and skyname.
- Not done, by ruling (ledger): the 3D keypad (no story map has one), camera views (`P+0x2038` already set on every
  path), `%` and index bounds (already safe), `func_train` origin brushes in a moved member (the merger must leave
  that geometry unmoved first), `host_framerate` (no story `cof_slowmotion`), v_dark/difficulty/subtitle-language
  globals. The research docs carry the survey's corrections in a block under their titles.
- Live checks: `docs/campaign-test-guide.md`, "Fixes from the decompilation". Most uncertain: the held-light entity
  reaching client.dll's hook, sky models on a section's band edge, per-client zmax deltas, the lake swim and the
  companions in view during cutscenes.

## Merged maps wave 2 (2026-10-17)

The user asked which overlapping groups made most sense, then "Do it. What is it?". Choices and reasons:
`docs/research/2026-10-17-merging-the-rest.md` section 6 (most loads removed on the route actually walked; on a tie,
leave a map free for a later pair). 14 groups, `wave: 2` in `tools/map_groups.json`: cm_sewer3 (sewer3+sewer2),
cm_city2 (city2+park), cm_hamn (hamn+hamn2+hamn3), cm_college2 (college2+college1+college1dark), cm_buscity
(buscity+buscity2), cm_subway2st3 (subway2st3+subway2st2), cm_forest2/4/6 (2+3, 4+5, 6+7), cm_asylum1dark
(asylum1dark+asylum2dark+asylumb+asylum2), cm_asylum1day (asylum1day+attic+forestday), cm_fsewer1 (fsewer1+lake),
cm_village1 (village1-3), cm_c_ending4b (ending4b+ending4).

- **Merger rules (no engine change)**: worldspawn player1-4 never block, materialstxt absent = `sound/materials.txt`,
  WaveHeight absent = 0 (`plan.WORLD_KEYS_IGNORED`/`WORLD_KEY_DEFAULTS`); MaxRange takes the largest
  (`merge.merged_worldspawn`, also used by `merge_pair`); never merged now only c_nightmare (client.dll), c_start
  (the easter egg, until the merger adds it) and c_game_menu1 (`groups.NEVER_MERGED`); a group with a c_ending
  member keeps `c_ending` in its file name (`groups.group_name`: `cm_c_ending4b`). The planner checks the engine's
  `.sections` limits (`plan.section_limit_problems`). `python -m tools.map_merge.plan MAPS --record WAVE m1 m2 ...`
  plans, builds and writes a group with its hashes. Wave 1's files come out unchanged.
- Planner notes for every group: `evidence/map-merge-plan-20261017/wave2-notes.txt` (no door without landmarks).
  **Watch: view distance.** c_forest3 2000 → 10000 in cm_forest2, c_forest4 1500 → 4000 in cm_forest4, c_city2
  4096 → 6000, c_hamn 6000 → 8000. The forests have no env_fog and a black sky: their short MaxRange is the
  darkness. If the pictures show distant trees, the fix is per-section view distance in the engine's client
  (each member's MaxRange kept at entity composition, the local player's section sets the far clip).
- **Live checks now silent**: `merged_maps_check.run_game` goes through `live_loop.session` (test launcher,
  `-nosound`, guard, recovery); `live_loop.launch_config(companions=0)` turns the bots off for the door and picture
  runs. Before this, `merged_maps_check` started the player's own `CoFLaunchApp.exe` with sound.
- Smoke copy: all 16 groups built into it with `friend_setup.make_merged_maps` from the repo's list (74 s; the copy's
  maps are the recorded inputs; `validate_runtime` passes, it checks only the install record's wave 1 files). No new
  package yet: `build_tester_package` refuses the untracked `docs/research/2026-10-17-uni-coop-notes.md`, which another
  session wrote; not this agent's to commit or delete.
- Live-check driver: `evidence/merged-maps-wave2/run_checks.py doors|walk|pictures|starts [GROUP ...]` (results and
  `summary.txt` beside it).
- **Doors, live (the user: "Run the live checks")**: all 14 groups, every door between sections lands on its landmark
  offset. cm_college2 first failed one door 88 units off: the check's fault, not the merge's. c_college1's door
  trigger is 1 unit thick and the check stood the player with feet 36 units in the floor (the engine put them on
  the landmark). Fixed in the check (launcher `2e658d7`); rerun 4 of 4.
- **Black boxes round objects (the user, watching the door runs: "huge black squares around some objects", "the
  car ... in the village")**: not the merge. The original c_village1 shows them too. They were c_village1's
  `trigger_checkbrush` use-boxes round each car (rendermode 4, texture `{invisible`, embedded, every texel index 255)
  drawn opaque black by client.dll's Paranoia renderer; house doors (`inter_door`, rendermode 4) black for the same
  reason. Cause: the engine's VBO lightmap pass (`ref/gl/gl_rsurf.c` R_SetLightmap, `gl_overbright 1`) left the
  lightmap unit's alpha combine at REPLACE from PREVIOUS; the client's texture modes (Paranoia `ENVSTATE_MUL_X2`)
  set only the colour combine and take alpha from GL's initial state, so the alpha test passed every hole. GoldSrc
  never sets those states. Found by elimination (scripts in `evidence/merged-maps-wave2/village_*.py`, shots in
  `village-render/`): moving every env_sprite or env_model away left the boxes; `gl_brushents 0` (the engine
  draws brush entities) or `gl_overbright 0`, even switched at runtime, took them away; skipping the client's
  normal or transparent triangle callback did not (the client draws world and brush entities from elsewhere).
  The 2026-10-16 note that the apartment hallway door is "solid black ... stock Cry of Fear" was this bug.
  **Fix: engine `d97e180d`** (`ref/gl/gl_cof_texenv.h`; R_ClearVBOState ends with every unit at GL's initial
  texture environment; `cof_client_texenv 0` brings the old behaviour back), tests launcher `96147d5`. Live A/B in
  one game: on, the van and white doors; off, the black box; on again, the van. Only `ref_gl.dll` changes (no ref/gl
  commits since 10-14 besides this). The smoke copy runs this `ref_gl.dll` (`dcc7cbb7`), the packaged one is kept
  as `ref_gl.dll.7E32F815.bak`; `validate_runtime` checks only xash.dll. Testers get it with the next package.

## Overnight debug with automated live runs (2026-10-16 night → 10-17)

The user played Chapter 1 (2 AIs, merged maps on, co-op flashlight off) 20:46–20:52 and said "Multiple things were broken", then asked for "a long, very thorough debug on the whole system" while they sleep, and for "a powerful iterative development cycle that's completely automated … launching and closing the game" on the real project ("I want you to do this on the main system, on our current project"). They heard a run ("I was able to hear it last time") and then: "I don't want you muting my entire computer because I have audio playing while I sleep from chrome".
- Evidence of their run: `evidence/playtest-20261016-evening/` (session log); their OBS recording `D:\Media\2026-10-16 20-46-37.mp4` (no voice; frames in the session scratchpad). They never reached the apartments (c_start → c_start2 → c_start, quit at the first fight).
- **Automated loop** `tools/live_loop.py` (+ `tools/game_audio.ps1`, tests `tests/test_live_loop.py`): `run COPY SCENARIO [--map M]`, `sweep COPY` (the start scenario on every chapter map), `report LOG`, `install COPY XASH_DLL` / `restore COPY` (developer engine; packaged one kept as `.bak`, marker `live-loop-engine.json` lets the install check through), `recover COPY`. Drives the game over loopback RCON (random password per run; `ent_fire 1 set origin`, `+forward`, `ent_fire <trigger> use`, `cof_skip`). Scenarios: smoke, follow, levels (c_start → c_start2 → c_start via `haehecl`; c_start2's arrival spot 2424 -940 -731 walks you straight back), start, merged (every section door with 2 AIs). Logs and reports in `evidence/live-loop/`.
- **Silence, game only**: runs start the game through `CoFLaunchApp-autotest.exe` (a copy of the copy's launcher) with `-nosound`; the guard mutes each audio session of that program the moment it appears and never unmutes (Windows remembers a program's mute: from the second run on its sessions start muted, seen "muted from the start"). Never mute output devices (the user plays Chrome audio at night) and never mute the player's own `CoFLaunchApp.exe` (a remembered mute makes their next game silent). Cry of Fear opens an audio session even with `-nosound` (its own music player). Lessons: the first guard unmuted before quitting (audible); a killed sweep left the smoke copy's `CoFLaunchApp.exe` remembered as muted on the fifine headset output (cleared in `HKCU\Software\Microsoft\Internet Explorer\LowRegistry\Audio\PolicyConfig\PropertyStore\c6709a07_0`, value `5` = VT_BOOL mute); COM `SetMute` needs `[MarshalAs(UnmanagedType.Bool)]` (the fifine driver refuses VARIANT_BOOL).
- **Crash safety**: a run keeps a recovery record (`evidence/live-loop/pending`: the copy's cfgs, launcher settings, SAVE); the next run or `recover` undoes a killed run. (A killed sweep had left cheats, an RCON password and a cleared SAVE; restored from its temp backup, 22 files, hashes equal.)
- **Fixed: companions flipping follow/idle every frame** (the playtest: 3972 mode lines in 6 min, 180 a second; 10-15: 20/min). The partner-in-the-way check looked toward the trail point while walking and toward the leader while standing. Now the brain reports where it would walk (`out->aim`) and the engine checks that way in both states; the line behind a person goes by distance (sticky by 48 units a place); a partner walking on is followed at its pace. `cof_companion_debug 1` logs `companion-why` (what it perceived) at each mode change. Engine `b982c003`, harness scenarios (40 flips in 40 frames before). Live after: worst second 3 (was 14 in the same scenario, 180 in the playtest).
- **Fixed: merged maps lost every decal** ("Decal has invalid texture!" on every merged map, never on an original): decals are named "{ding10" and both entity parsers (engine `cof_sections.h` CoF_SectNextBlock, `tools/map_merge/bsp.py` parse_ents) split entities at any brace, quotes ignored. Engine `46b858d1`, launcher `6d02340`. The wave 1 `.sections` files are unchanged. A client warning now names a map decal without texture.
- **Fixed: a section door entered off its usual spot** refused the move (and logged it) ~140 times a second: the offset landing lay in a wall and the beside-search traced from inside it. Now the destination landmark (or beside it) is the last resort; refusals logged once a second. Engine `c11713e2`. (In normal play the ap2la door puts you on a fixed spot; the stairwell exits match across the members.)
- Sweep of chapter starts (2 AIs, skip, short walk): c_intro → c_nightmare, c_nightmare, c_basement, c_sewer3, c_park3, c_subway2st1, c_bridge, c_attic, c_ending1b/2b … all came up, companions joined, no engine errors except c_intro's "can't set background map while game is active" on entering c_nightmare (refused, harmless). `-nosound` runs log thousands of "Cvar_Set: variable 'room_type' not found" (the sound cvars do not exist then; not in normal play).
- **Fixed: bots never revived a downed player** (live death scenario: the player stayed down, no CPR): a dead body's FL_ONGROUND flickers every frame and the game's CPR (and our revivable check) wants it on the ground, so the bots switched revive/idle each frame. A downed leader now stays down 0.5 s and CPR is tried on a frame the body lies still. Live after: CPR 1 s after death, up 8 s later. Engine `ef0e610e`.
- **Fixed: a teleport a bot asked for fired after the engine had already carried it** (section moves, party doors): `SV_CoF_CompanionMoved` drops it and restarts the trail. Engine `ef0e610e`. cm_apartment1 companion teleports 16 → 9 (the rest are the scenario teleporting the player around).
- Checked live and fine: both merged groups' doors with 2 AIs (cm_apartment1 12 moves, cm_apartment3 12, companions along 10/12 and 12/12), checkpoint save/load with bots (`cof_save manual`/`cof_load manual`: everyone back, bots re-added), co-op flashlight off (F refused) and on (goes through). Full suite **345 OK** (`evidence/live-loop/full-suite-overnight.log`).
- Seen in the recording, not yet addressed: companions standing close in the player's view in corridors and at doors; "Companion has joined the game" twice after every level change (printed by hl.dll); the wardrobe's costumes all "Locked" (retail unlocks, not the bonus room setting).
- **Current package**: `evidence/overnight-20261017/package/CoF-Story-Coop-Test1.zip` SHA-256 `685E19FF…`, xash `60AA79F4…`, from launcher `e61fda1`, engine `ef0e610e`, FreeVGUI `66eca8d`; the smoke copy was updated from it (`--update`, exit 0, `validate_runtime` passes, both merged groups) and the launcher app reopened. The test launcher `CoFLaunchApp-autotest.exe` stays in the copy (muted for Windows; the player's `CoFLaunchApp.exe` is not).
- **Merging the rest** (the user: "read the map merge research … start figuring out how we are gonna merge the rest"): survey of every door-joined story pair through the real planner, `evidence/map-merge-plan-20261017/` (`pairs.json`, `story_pairs.txt`, `after_rules.txt`, `reverse_origin.txt`, `groups.txt`, scripts beside them). 13/88 story pairs clean today; 24 once the merger takes the worldspawn rules (player1-4/materialstxt accepted as in wave 1, MaxRange = the largest); 7 more by putting the member with origin brushes first. Blockers: maps hl.dll names (27 pairs, incl. c_start+c_start2 and every c_city/c_ending pair; RE in `hl-names/`), origin brushes in a moved member (21), fog (11), play-everywhere sounds from the start (7), unrenamed references (6), clipnode overflow (5), sky (3). 4-member chains mostly fail the ±4096 height (delta.lst origin range). Clean groups now (with the rules): c_asylum1dark+c_asylum2dark+c_asylumb+c_forest7, c_village1-3, c_asylum1day+c_attic+c_forestday, c_fsewer1+c_lake+c_village1, c_stad+c_hamn+c_hamn2, c_hamn2+c_hamn3+c_college1; pairs c_sewer+c_sewer2, c_sewer3+c_sewer2, c_city2+c_park, c_subway2st3+c_subway2st2, c_college2+c_college1, c_asylum2+c_asylumb.

## Co-op flashlight is the host's choice (2026-10-16)

A friend of the user's wants only Simon's phone as the light, not "the police flashlight": the F key's `flashlight` command, which in multiplayer gives a free, battery-less torch every client draws (frags 32; `docs/research/2026-10-12-companion-weapons-items-re.md` 3.1; Cry of Fear's own co-op mode). The user chose a host setting, off by default, and the AI companion follows the same rule. Engine `6585553d`: `sv_cof_light.h` (rules), `sv_cof_light.c` (cvar `cof_coop_flashlight` "0"; a player's `flashlight` is dropped in `SV_ExecuteClientCommand` before the game DLL, log `phase=flashlight-refused`; a torch on although not allowed is switched off with the game's own command once a second, log `phase=flashlight-off`), the companion lights its torch only when allowed. Launcher `708f357`/`8cc90ac`: checkbox "Co-op flashlight on F", `coop_flashlight` setting (default False), `host_launch(coop_flashlight=)` writes `cof_coop_flashlight`; docs (controls, companion). The phone, the story's flashlight weapon, the Glock taclight, lantern and flares are untouched. Full suite 335 OK (`evidence/merged-maps-20261016/full-suite-light.log`). Package `evidence/merged-maps-20261016/package3/CoF-Story-Coop-Test1.zip` SHA-256 `86c0c06e…`, xash `5ec8c4c8…`; smoke copy updated from it. Not yet seen live.

## Merged story maps: the feature (2026-10-16, built; scripted live checks pass; the user's playtest pending)

The user: "Let's make it a full feature." Decided with them: the whole campaign in waves (apartments first), each player goes through a door between sections on their own (partners within 128 units come along), a host checkbox "Merged maps" on by default, approach A (engine sections; the original level-door entities stay). The user then: "If you're confident in it, I am too. Build it." (review gates waived; inline, no reviewer subagents). Spec `docs/superpowers/specs/2026-10-16-merged-maps-design.md`, plan `docs/superpowers/plans/2026-10-16-merged-maps.md` (rolling-wave detail), ledger with every ruling `.superpowers/sdd/2026-10-16-merged-maps/progress.md` (git-ignored).
- **Launcher** (`tools/map_merge/`): `bsp.py` (BSP30), `merge.py` (pair merge, geometry only; dz = smallest multiple of 512 with a 128 gap; entity lump = first member's worldspawn, wads joined), `rules.py` (renames `_m<k>`, cut references `_x<k>`, styles 32-60 remapped, never 61-63, `.sections` text, `compose_reference`), `check.py` (pair check, run after every pair), `groups.py` (`tools/map_groups.json`, build/install, input and output SHA-256), `plan.py` (planner: format/pages/entities/models/sounds/AI nodes (hl.dll MAX_NODES 1024), sky, fog, hl.dll worldspawn keys, globalname, origin brushes, play-everywhere sounds that start with the map, changetarget, landmarks; `python -m tools.map_merge.plan MAPS m1 m2 ...`, `--graph`). Setup and update merge every enabled group (`friend_setup.make_merged_maps`, `cof-story-merge.log`, `merged_maps` in the install record, checked by `validate_runtime`); the app bundles `map_groups.json`. Launcher: checkbox → `cof_merged_maps`, Continue keeps the save's mode and shows the original map (`Checkpoint.merged/section/label`, `merged_for_start`); run report counts section moves and merged level changes by original names.
- **Wave 1** (`tools/map_groups.json`): `cm_apartment1` = c_apartment1 + c_apartment2 (24.5 MB), `cm_apartment3` = c_apartment3 + c_apartment4 (+1536) + c_apartment5 (-1536) (30.9 MB, 87 lightmap pages); accepted worldspawn differences (player1-4: the engine sets player models itself; materialstxt: default is the same file). c_apartment2's `changelevel4` names `land4`, in neither map (arrival at the start spot, as the original game).
- **Engine**: `engine/common/cof_sections.h` (header-only: `.sections` parser, routing `CoF_SectionsRoute`, movers `CoF_SectionMovers`, composition `CoF_ComposeEntities` byte-identical to the Python reference for both groups, session record), `cof_sections.c`/`cof_merged.h` (members' entity strings through the language-pack rules, composition cache; `Mod_LoadEntities` and `SV_ReadEntityScript` compose merged maps; Host_Error if a merged map cannot be composed), `engine/server/sv_cof_sections.c` (cvar `cof_merged_maps`; `SV_CoF_SectionChangeLevel` first in `pfnChangeLevel` (before the exit hold and the one-change guard); `SV_CoF_SectionsResolve` first in `SV_QueueChangeLevel`; transitions find the source landmark/exit by their own names (`sv_save.c`); `map` command + start-spot swap for a member's section; respawn back to the section died in; companion bots follow their leader's section; once-only exits remembered; autosave on first entry; play-everywhere sounds only to their section (`SV_Multicast`); `merged.txt` in story saves). Party helper `SV_CoF_PartySectionMove`. Logs: `[cof-story-transition] phase=section-move userid from to landmark(original) origin`, `[cof-sections] changelevel from to landmark file dest`, `[cof-sections] ... entities of N maps`.
- **Verified**: full suite 319 OK (launcher `a1ff881`, engine `cb7c532d`); package `evidence/merged-maps-20261016/package/CoF-Story-Coop-Test1.zip` (xash `a3ef57e3…`); smoke copy updated from it: both groups merged by the frozen app in 51 s with the recorded hashes, `validate_runtime` passes.
- **Live 2026-10-16 (the user: "test it,")**. First the user's own run crashed: "SV_AllocEdict: no free edicts (max is 1200)" (the user: "Hey, it crashed."). Xash takes `max_edicts` only from `gameinfo.txt` (default 1200) and ignores liblist's `-num_edicts 4096`; a merged map holds the entities of 2-3 maps. Fix: setup and update write `max_edicts 4096` into the copy's `gameinfo.txt` (`friend_setup.ensure_entity_limit`, after `run_installer`). Then the scripted checks (`tools/merged_maps_check.py`, cheats on for the run only, restored after; results `evidence/merged-maps-20261016/check/`): **doors** 17/17 (8 in cm_apartment1, 9 in cm_apartment3) land exactly on the landmark offset (0 units off), none bounce; c_apartment2's `land4` door goes to the start spot as designed. Pitfall: the script must wait 1500 frames between doors (frames run ~400 a second; a 0.5 s wait hit the 1.5 s re-move guard and showed 3/8). **Pictures**: 40 camera spots over the five members, merged vs original: no difference caused by the merge (c_apartment1's ≤0.955% edges are its opening cutscene's camera, not the map; the check skips cutscenes first now). cm_apartment1 loaded "72 x 3 additional lightmaps", no MAX_LIGHTMAPS warnings. The `[cof-sections]` start-up lines went missing for a `+map` start (Con_Printf before allow_console): now `COM_CoF_SectionsNote` → Sys_Print, with a test.
- **Current build**: full suite 326 OK (`full-suite-live.log`); launcher `c138b42`, engine `efbda362`, FreeVGUI `66eca8d`; package `evidence/merged-maps-20261016/package2/CoF-Story-Coop-Test1.zip` SHA-256 `05a0a700…`, xash `a858e7d5…`; smoke copy updated from it with `--update` (both groups merged, `max_edicts 4096`, `validate_runtime` passes).
- **First user playtest, 2026-10-16 17:20** (the user: "All right, let's test it"; the agent opened the app in the smoke copy): the app's Continue was the door check's own 17:01 autosave (cheats run, empty inventory), so the user started in cm_apartment1 with no weapons and died ("I have no weapons"). Their "no sound" was on their side ("Wait, I have sound"). Seen working: a merged door c_apartment2 → c_apartment1 with no load (`section-move`, `section-entered`), the companion followed (`section-companion`), autosave on entry, the game-over reload of cm_apartment1. Fix (launcher `5cade59`): `merged_maps_check.run_game` puts the copy's `SAVE` folder back exactly after a run (`saves_kept`). The copy's Continue still descends from that save: start Chapter 1 instead.
- **Next**: the user's playtest of the apartments from Chapter 1 with Merged maps on (needs their go-ahead to launch); then the plan in `docs/research/2026-10-17-merging-the-rest.md` (wave 2 = merger rules only: worldspawn rules, member order, hl.dll names lifted except c_nightmare; c_start + c_start2 needs per-section fog events first, wave 3). The user decides the waves and the group choices listed there.
- Known limits (rulings): velocity is cleared on a section move; sections entered are not kept in saves (an extra autosave after Continue at most); loops started later are not re-sent to a player entering a section (wave 1: machineroom.wav); origin-brush entities in moved members, changetarget on doors between members, and play-everywhere sounds that start with the map are refused by the planner until handled.

## Map merge spike (2026-10-16, throwaway, nothing in the product)

The user: "remove some transitions and make maps bigger because loading on every single door is gonna be pain in the ass for coop", then "find out how feasible it is to merge maps". Everything is in `evidence/map-merge-spike-20261016/` (git-ignored): `bspmerge.py` (merger), `verify.py` (offline checker), `c_apt34.bsp` (c_apartment3 + c_apartment4), `merge_survey.txt`, `map_graph_summary.txt`, `bspguy-research.md`.
- Cost of a level door now (logs): load 1-2 s solo, 3-4 s with a second player, plus the exit gather (9 s in the two-player test). The 2026-10-15 13:11 run: 22 level doors in 23 min, 9 of them c_apartment3 <-> c_apartment4.
- Campaign: 81 story maps, 88 door-joined map pairs (87 two-way). Entity origins travel as +-4096 (`cryoffear/delta.lst`: 16 bits / 8), and maps already span most of that in x/y, so sections must be stacked in z. Xash lifts the old engine limits (leafs 131072, models 4096, edicts 4096 via liblist), but plain BSP30 keeps 16-bit clipnodes/nodes/faces/marksurfaces/vertexes. 82/88 pairs fit both. Groups that fit: c_start + c_start2 (72%), c_apartment1 + 2 (68%), c_apartment3 + 4 + 5 (77%); with those three the 13:11 run would have had 2 loads instead of 22. c_subway2st1-3 does not fit (clipnodes, vertexes; two skies).
- The game DLL names only c_city, c_forest3, c_intro, c_nightmare, c_start, c_subwaysick1 and the c_ending prefix.
- Merge method (same as bspguy's): B moved by a power of two in z (exact texture shifts; 2048 for this pair), new root node 0 on a z plane (renderers start at node 0), three new root clipnodes, world leafs first then submodel leafs (vis rows index world leafs), world faces contiguous, block-diagonal vis, textures deduplicated by name+bytes, B's `*n` models renumbered, B's origins moved, the 11 clashing targetnames renamed `_b` in B (doors' exits, landmarks, the changelevel triggers, fog, the objective). Float rounding tipped 119 faces' lightmap boxes by one cell: their light is laid out again for the new box (edge cells copied), exact.
- `verify.py` on c_apt34: clean. Structure in range; 80,398 points same leaf/contents in hull 0 and the three clip hulls; PVS identical for all 3,577 leafs; all 30,138 faces same texture/styles/light; no new shared names. Not loaded in the engine yet (needs the user's go-ahead).
- bspguy (public domain) joins trees the same way but its runtime (section spawning, transitions) is Sven Co-op AngelScript only; Sven's merged HL chapters were rebuilt from source.
- Live (the user's go-ahead "Yeah, do it", 2026-10-16 08:19-08:38, smoke copy, `scratchpad launch_apt34.py`: `+map c_apt34`, sv_cheats 1 and sv_enttools_enable 1 for this run only, no AI, aliases `up4`/`down3` = `ent_fire 1 moveup +-2048`). Two engine load rules the first files broke (`Host_ErrorInit: PM_RecursiveHullCheck: bad node number`): a hull is walked only over [head, last] (`Mod_SetupHull`), so the three new clip-hull heads go first; hull 0's last node is head + nodes reached (`CountClipNodes16_r`), so the world's nodes must be one block (root, A world, B world, then submodels). `verify.py` now checks both (and that ZHLT's "head past the end" empty hulls stay empty). Third file loaded; the user walked the apartment 3 half (~20 s), took a level door into the original c_apartment4 (expected: no engine sections), closed the game; "Everything looks good". The apartment 4 half was never visited.
- **The binding limit is Cry of Fear's own renderer** (`client-renderer-re.md` in the spike folder): client.dll draws the world and brush entities itself with its own copy of the lightmaps, 64 pages of 128x128, and assumes 128 blocks (so Xash's large lightmaps would break every map). c_apt34 needs 75 pages (texels alone 64.09): 5071 brush-entity faces of apartment 4 on pages 64-74 would be drawn with another page's lighting. It also draws only brush entities with edict index 1-899. Page counts (`lm_all.txt`): only 40/88 door pairs fit 64 pages; c_start + c_start2 87, c_apartment1 + 2 72, c_apartment3 + 4 75 do not. Ways out, none done: drop lightmaps of `{invisible`/`black` faces and pack by height (just enough for 3 + 4, not the others); `gl_brushents 0` on merged maps (engine draws brush entities, without the bump/specular look); patching the client renderer's 64-page limit in memory (RE project); brush entities first in the merged entity lump (edicts < 900).
- **Renderer limits patch, built 2026-10-16** (the user: "find out the feasibility of patching the renderer", then "If both agents come back with good feedback, I want you to start building it"; both did: `client-renderer-patch-plan.md`, `paranoia-source-research.md` in the spike folder; Paranoia 1.2 source https://github.com/HLSources/Paranoia confirms the same 64-page sites). Engine `engine/client/cof_renderer_patch.h` (26 code edits + 3 set-base words, stock bytes checked with relocated operands, all or nothing, removable) and `cof_renderer_limits.c` (`CL_CoF_RendererLimitsLoad/Unload`, wired after `CL_CoF_Inventory12Load` / before `COM_FreeLibrary` in `cl_game.c`; retail client.dll SHA only; VirtualAlloc 12 MB pages + chain array + entity list; log line `[cof-renderer] client.dll's renderer: 256 lightmap pages, 2048 brush entities a frame, edicts up to 8191`). Tests `tests/test_enhanced_renderer_limits.py`: sites (every byte, refusal on any changed byte, byte-exact undo) and real runs of the retail code (UpdateLightmaps 1005A110 plain and bump on 80 pages: stock 64 warnings/64 pages, patched 0/80, pages 0-63 identical to stock, 64-79 identical to a renumbered run, guard pages after the new storage; UpdateEntityList 1496 of 1500 vs 512; chain write for 256 pages). RED shown with the patch compiled out. Full suite 244 OK. Engine `8fb4ac24` (tester-package), launcher `778e237`; package `evidence/renderer-limits-20261016/package/CoF-Story-Coop-Test1.zip` SHA-256 `d3610f71…`, xash `2591ce47…`; smoke copy updated.
- **Renderer patch live, 2026-10-16 14:13-14:46** (the user: "Okay, let's test it", then "do that" for scripted runs). paranoia_log on c_apt34: "Loaded 75 x 3 additional lightmaps", 0 MAX_LIGHTMAPS warnings (unpatched: 64 and 5071 warnings). The user walked in, said apartment 4 looked "very weird, especially without the flashlight", then "I'm not sure"; their two deaths were gameplay (fresh `+map` start, empty inventory, then the autosave reload drops you in the original c_apartment4; `god`/`notarget` work with the spike's cheats). Measured instead of eyeballed (scratchpad `shots_launch.py`/`shots_sweep.py`; spike folder `pick_views.py`, `pick_sweep.py`, `compare_sweep.py`, `shots/`): 41 camera spots in apartment 4, merged (+2048 z) vs the original c_apartment4, client renderer: pixel-identical (median 0%, worst 0.002% of pixels off by >16/255). **The patch works; the merged half looks exactly like the original.** Pitfall that cost an hour: comparing the client renderer with `gl_brushents 0`/`gl_renderer 0` (engine-drawn) shows differences that are stock Cry of Fear: e.g. the hallway door `*333` (c_apartment4 `*126`) is solid black (0,0,0) in the client renderer in the original map too. Always compare against the original map. A temporary probe (`clientre/probe.diff`, not committed) confirmed on the GPU: pages 64-74 exist (128x128) with real light, the door is in the client's entity list; `clientre/page_uses.py`, `disva.py`, `imm_range.py` are the RE helpers used.
- Scripted camera in the engine: `ent_fire 1 set origin "x y z"` (hl.dll's DispatchKeyValue takes entvars keys), `ent_fire 1 angles p y 0` then `ent_fire 1 set fixangle 1` in the same frame (fixangle stops pmove overwriting the angles), `wait N` (frames), `snapshot` writes `<map>_NNNN.png` to the game root (not `scrshots/`). `noclip`/`god`/`notarget` toggle. `ent_fire <n> moveup` does not move FL_WORLDBRUSH func_walls visibly.
- Engine `c75f5097`: the `[cof-renderer]` lines go straight to Sys_Print (Con_Printf drops everything while host.allow_console is false, which is when client.dll loads; seen live: the patch worked and its line was missing). Launcher `21c6ae3` (test). Package `evidence/renderer-limits-20261016/package2/CoF-Story-Coop-Test1.zip` SHA-256 `9c5bdc45…`, xash `9558e5ca…`, from launcher `21c6ae3`, engine `c75f5097`, FreeVGUI `66eca8d`; smoke copy updated from it and validates (it ran the probe build and a skipped install check in between). c_apt34.bsp is still in the smoke copy's maps folder.
- "Decal has invalid texture!" once at c_apt34 load (client renderer, `R_DecalShoot` index <= 0); not seen in normal runs; cause not found.
- A product version would need: the merger as a setup step from the player's files (N-map stacks, origin-brush models, CoF custom keys for renames, worldspawn differences such as `doorsound` 1/0 in c_apartment3/4); engine sections (a level door inside a merged map becomes an instant landmark-offset move of whoever walks through, `changetarget`, redirects of other maps' level doors into a section, saves/Continue/chapters/run report names, per-map engine code such as the use-only c_apartment exits); muting sounds and dynamic lights across sections (GoldSrc sounds and dlights ignore walls, and stacked sections are only ~128-1000 units apart); then a playthrough of every merged group at every story visit.

## UI pass (2026-10-15)

User: "you gotta fix the menus because they fuck up whenever I go into them", then "Let's make sure all UI elements are perfect." The agent launched `Start-CampaignTest.ps1 -Mode companion -Map c_start` at the user's go-ahead ("launch it"), drove the host window (windows-mcp clicks/keys) and captured with the game's own `screenshot` over RCON (`control.py "screenshot"` → `host/cryoffear/scrshots/c_start_shot*.png`; it includes VGUI, unlike desktop captures, which the terminal window overlaps). RCON can also run client commands in the host (`+inventory`/`-inventory`, `+objectives`, `cof_skip`, `inventoryequip 1`, `cof_hud_text_trace 1`).

Fixed (all uncommitted; FreeVGUI `story-coop` working tree, engine `companion-ai`, launcher `companion-ai`):
- **Text pulled onto the message row** (FreeVGUI `surface.cpp`, CoF Enhanced baseline bug, not ours). Enhanced moves short runs of the message font role ("credits") to `cof_hud_msg_y_pct` (78 % → y 562 at 720p). (1) With the UI transform off (1280x720) `CofUI_ScreenSize` is 0 and the 35–90 % band check was skipped, so EVERY short "credits" run moved: the wardrobe's costume list and Close/Reset all at y 562, the objectives text drawn at y 18 on its paper moved 544 px down (`[cof-vguitext] … moved=544`). Fix: `CofTextScreenTall()` falls back to the root panel's height. (2) `runInMenu()`: a run drawn inside a recognised non-HUD client panel (`CofPanels_Class`, e.g. `CClothesMenu`) is never moved and gets no strip. The pickup line ("I can't carry any more flares ammo") is laid out at y 632 (87.8 %) at 720p, inside the band, and is still moved to 562 (traced live). The trace line now prints `menu=0/1`. The user confirmed the wardrobe on the first fix build (`vgui.dll` `81F77D78…`, fix 2 only); the objectives text after fix 1 is not yet seen live (no objective was given in the rerun).
- **12-slot grid alignment**, measured on captures: the four columns ended 3 px right of the ITEM DESCRIPTION box (x 607 vs 604). Now `kStride` 77 (5 px gaps), rows 68/145/259, POCKETS label top 227 (10 px clear above and below), and the examine picture at 36,356 (`kCheckX/Y`; was the retail 31,352, which put its drawn-in text 5 px left of centre and, with the box moved by `DESC_SHIFT`, 3 px high: margins now L24/R24/T9/B7). Changed: `cofinventory_logic.h`, `tools/make_inventory12_art.py`, `tests/test_inventory12_art.py` (asserts the grid ends on the box and the picture placement), `tests/engine/cof_inventory_logic_test.cpp`.
- **RCON echo** (`engine/server/sv_client.c` `SV_RemoteCommand`): printed the raw packet `rcon <password> <cmd>` into the on-screen notify lines and every log; now `Rcon from <adr>: "<cmd>"`, no password.
- Earlier the same night: the inventory's see-through background showed the retail POCKETS label under the grid; fixed by `inventory12_full*.tga` swapped into the background (`FullArt`, `ImagePanel::getImage()`); live-checked 2026-10-15, no ghost.

Checked live and fine: inventory hover/description, item menu, notes list (empty: no note picked up in that run) and its back arrow, pause menu, Options (Enhanced's own), phone message screen, pickup message, ammo HUD (the FAMAS's `INF` is retail: unlimited ammo). The engine version stamp (bottom right) and the `cofenhanced …` stamp (top right) appear only in screenshots and the pause menu (`Con_DrawVersion`), not during play. Not reached: padlock, keypad, computer, tape recorder page, note reader, yes/no, billboard (other maps).
- The item menu opens right of the grid by design (never over a box) and covers QUICK SELECT slot 1 and most of the NOTES heading; left as it is.
- `give weapon_glocktac` has no entity ("No spawn function"): the tactical Glock is made by combining. `give` needs `sv_cheats 1`.

Builds (staged under `evidence/menu-text-20261015/`): `vgui.dll` `81F77D78…` (menu fix only; `full-suite-menu-text.log` 174 OK), `ui-pass/` (`xash.dll` `BD39CF72…`, `vgui.dll` `357321CF…`, 174 OK), `ui-pass2/` (same `xash.dll`, `vgui.dll` `40642512…`, the spacing tweak; installed in both copies with `Prepare-CampaignTest.ps1`, host running at `c_start` since 04:45).

## AI companion: doors, hops and routes (2026-10-14)

User report after the 2026-10-13 demo: the bot "gets stuck in a lot of doors and jumps a lot for no reason". Implemented on local branches `companion-ai` (launcher and engine), uncommitted; the user said "I don't need a review of it" (no reviewer subagents).
- Phase A (`sv_cof_brain.h`, `sv_cof_companion.c`): doors are opened through `pfnUse` (`companion-door`), no blind hops (a jump only over a low ledge, `companion-jump reason=ledge`), follow hysteresis start 128 / stop 80, teleport reasons `far/lost/stuck/trail`.
- Route graph: new `engine/server/sv_cof_nav.h` (pure) reads hl.dll's saved node graphs `maps/graphs/<map>.nod` (version 16; layout checked against all 117 CoF graphs), A* with a per-link cost callback (a shut story door blocks, a shut openable door costs 128). The graphs are fragmented into rooms (teleporting inter_doors split them; `c_apartment1` 202 nodes, ~6 % of pairs connected), so the graph is a LOCAL planner: the engine plans only when the straight way to the brain's goal is blocked; stuck on a route it replans once (`companion-replan`) before teleporting. Logs `companion-nav map=… nodes=… links=…` (or `graph=none`) once per map, `companion-route userid=… nodes=N|none`.
- Tests: `tests/engine/cof_nav_test.c`, `cof_nav_real_test.c` (real graphs of c_start, c_apartment1/2, c_asylum1, cross-checked by a Python reader in `tests/test_enhanced_companion.py`); 174 OK (`evidence/companion-nav-20261014/`). Docs: `docs/companion.md`, `docs/campaign-test-guide.md` (AI table).
- `tools/Start-CampaignTest.ps1`: the test gun is off by default now (`-Gun none`; user: "you can take off the infinite ammo M16").
- Live 2026-10-14 23:23–23:34 (companion mode, `c_apartment1`/`c_apartment2`): the graph loads (`nodes=202 links=698`; `c_apartment2` 18/39); in ~7 min on `c_apartment1` 15 teleports (9 `trail`, 5 `far`, 1 `stuck`); on entering `c_apartment2` 6 `companion-jump reason=ledge` in 13 s. User feedback on how it felt: not given yet. 2026-10-15: right after the `c_start` opening cutscene the bot stood inside the player's view (captures `c_start_shot0018/19`). These three are the next companion work.

## Latest (2026-10-13): second live session (intro fixes pass), main-game test started

Second live session, 2026-10-12 22:18–22:27, engine `3D860BC7…`, solo mode, `c_intro`, m16; started outside an agent session. Logs `evidence/story-live-20261012/{host,guest}/campaign-logs/20261012-2227*-campaign-*.log` (archived by the next Start). `story_run_report.py`: 1 transition completed, no refusals, errors or crashes.

- Live-verified: Enter skip (`cutscene-skip` x6, `cutscene-skip-over` x2); the intro end (`teleport-party-moved` to `kunitei`, then `exit-leader in_exit=1 strays=0`, both players restored in `c_nightmare` without J); no AI-drive flapping (`companion-driving`/`companion-released` 2 each); the test gun for both; `c_nightmare` camera gift shared (`gift-shared class=weapon_camera` to the host, `starter-shared` to the guest); guest `kill` → `respawn-sooner` → `inventory-restored reason=respawn` → `spawn-kept reason=partner-near`.
- User report (2026-10-13, not yet reproduced): "the camera couldn't be added back to the inventory". The log does not show which items came back after the respawn. Unclear whether this is the respawn restore or something else; ask the user. The camera only exists in `c_nightmare` (`kakkaej`), and `c_start` empties the inventory.
- AI drive (solo mode), live 2026-10-13 10:29–10:37 and in the 22:18 logs: the AI takes the guest only while it is held in a camera (`companion-driving` ~8 s into a cutscene), and lets go the second the camera ends or the guest is teleported (`companion-released` together with `teleport-party-moved`, `companion-teleport` or `camera=none`). Certain: server-set views (`CoF_PartyMoveEdict` fixangle, camera end) are not in the echo ring (`sv_cof_companion.c` `CoF_CompanionEcho`); a fix would record every svc_setangle the guest is sent. Unknown: what keeps resetting `human_until` while the host plays (never during cameras). Focus-loss mouse deactivation (`host_sdl2.c` `SDLash_ActiveEvent`) is skipped when the guest loses focus before `key_game`, and `IN_ToggleClientMouse` activates without a focus check, but a live client.dll mouse in both windows would fight over the cursor, which the user did not report. No controller in the guest log. Next: ask whether the guest view turns with the host's mouse, else log the touch reason. The user said (10:38) "The AI is literally secondary"; not being worked on.
- The user asked to skip the intro and test the main game. 2026-10-13 10:29–11:08 local: `tools\Start-CampaignTest.ps1 -Map c_start` (solo, m16) launched by the agent at the user's request; the user closed both games at 11:08 (nothing running afterwards). `story_run_report.py`: `c_start` → `c_start2` → `c_start` (`hahaland`), 2 transitions, 2 players, no refusals, errors or crashes; `door-party-moved`, `warp`, `note-shared`, `exit-hold`/`exit-ready`, autosaves on both maps, the cutscene skip in `c_start`'s opening. No story progress after 10:37 (the user stepped away). Log: `evidence/story-live-20261012/host/campaign-host.log` (archived by the next Start).

## Demo session (2026-10-13 evening)

At the user's request ("launch both instances … with the bot active", a demo for a friend), the agent ran `tools\Start-CampaignTest.ps1 -Mode solo` (defaults: `c_intro`, m16) at 22:46 local, engine `3D860BC7…` in both copies (hashes rechecked). Observed by 22:47:17: both clients connected, `companion-driving`/`companion-released` for userid 2, Enter skip (`cutscene-skip`, `cutscene-skip-over`), `teleport-party-moved` to `kunitei`, `test-gun-given` x2. Then c_nightmare: `gift-shared`/`starter-shared class=weapon_camera`, autosave, host death 22:48:05 → `inventory-restored reason=respawn` 22:48:16. User report (22:48): "I get the digital camera in the inventory, but I can't switch weapons or access the inventory". Same map and also after a respawn as the earlier camera report; not investigated, and not yet known whether it began before or after the death. Log archived as `host/campaign-logs/20261013-224855-campaign-host.log`.

At the user's request to go to the main first level, the agent closed both copies at 22:48 (the guest closed via its window; the host did not respond to the close request and was force-stopped) and ran `Start-CampaignTest.ps1 -Mode solo -Map c_start`. By 22:49:20 both clients were connected, the guest was `camera-held`, and the AI briefly drove it.

User (22:51): "The bot's not working." Log: `companion-driving` 22:49:19, `companion-released` 22:49:20, then no further drive (the known solo-mode drive bug, still unfixed). The agent set `cof_companion 1` over RCON (`control.py`), but no bot joined: `CoF_CompanionAdd` (`sv_cof_companion.c`) silently returns false with no free slot, and the campaign server runs `maxplayers 2` (host + guest). **Gap:** solo mode can never hold a bot, and the refusal is not logged. The agent then closed the guest (no response to the close request, so it was force-stopped) and ran `kick #2`; `companion-added userid=3 name=Companion` at 22:52:28 (`companion-syringe`, `companion-equip weapon_switchblade`, `mode=follow`). This is the first live companion-bot run. Through 23:12 the user played `c_start` → `c_apartment1`/`c_apartment2` (several round trips) with the bot: it was re-added after each level change (new userid each map) and followed through inter_doors (`door-party-moved`, `companion-teleport`). The apartment key went normally: `item-found` 23:04:46 (room behind `exitheh1`), `item-used` 23:11:18 on the 2nd-floor hall door, `door-begin target=3_exit1`, then a camera. While stuck, the user said they "never got that key"; the 23:04 pickup is automatic. `cof_story_status` over RCON lists each player's inventory slots (useful mid-session).

End of the demo: the user said "You can close the game … I'm not playing"; the agent sent `quit` over RCON at about 23:22 (clean shutdown); no game processes afterwards. `story_run_report.py host/campaign-host.log`: 15 transitions OK (`c_start` → `c_start2` → `c_start` → `c_apartment1`/`2` round trips → `c_apartment5` → `c_apartment3`), 6 story maps, 13 exits proven, no refusals, errors or crashes; `companion-added` 16 (one per map), `companion-teleport` 50, `door-party-moved` 20.

**Bug: a story-given weapon cannot be equipped** (user, 23:20: the pistol "went into the bag section and could not be equipped as a weapon"). Evidence: the `c_apartment1` Glock (retail `cof_giveitems` to the activator) landed in slot 6 (inventory full: phone, switchblade, test-gun M16, syringe, hall key, Glock); the server holds `inventorySlot[6]="…/weapon_glock.txt"` and a `weapon_glock` in the weapon list (`cof_story_status`, `cof_story_probe slots 1` at 23:21). The client sent `inventoryequip` 1, 2, 4, 5 but never 6, so the client UI is the suspect. Same symptom as the `c_nightmare` camera (script-given, could not be equipped or switched). Under investigation as part of the inventory work below; not fixed.

## Inventory grid (started 2026-10-13 23:15)

The user asked to "make the inventory bigger" (all items don't fit; the test gun takes one of the 6 slots). Offered a second-page backpack (engine-only) or a real bigger grid; the user chose **the real bigger grid, 12 slots**. Path: architectural (brainstorming → written spec → plan); nothing implemented. Read-only RE by two subagents: client inventory screen (`client.dll`) → `evidence/inventory-grid-20261013/client-inventory-re.md` (also asked about the slot-6 equip bug), server slot sites in `hl.dll` and our engine's 6-slot dependencies → `server-inventory-re.md`. Known constraint: 6 slots are hard-coded in retail loops on both sides, and the three per-slot arrays at `+0x21FC/+0x2218/+0x2234` are contiguous (`docs/research/2026-09-30-hl-inventory-re.md` §8).

Research results (static only): server side feasible in place (the arrays after `inventorySlot` are dead; 9 loop bounds, 2 combine checks, 18 zero-writes to NOP; slots 7..12 fall on the saved `inventorySlotModels` entry, so no checkpoint-format change). Client side: an unpatched client.dll crashes on any `Inventory` slot ≥ 7; every slot position is hard-coded; one accessor (1006CDD0) serves all slot strings; 12 slots need our own panel. Also found: `c_nightmare` blocks the inventory in client.dll by design (explains the 22:48 camera report; not our bug), and an open item menu of slot 5 covers the slot-6 button (the likely cause of the pistol report).

User decisions 2026-10-14: 12 slots, story co-op only, our own grid (FreeVGUI) + hl.dll patched in memory, 4×3 layout with the retail right half kept. Spec: `docs/superpowers/specs/2026-10-14-inventory-12-slots-design.md` (the user said "build it" after reading it; refined while planning: keep/wide patch groups, `cof_inv_slots` stufftext, `_cofinv` as an important userinfo key, 72 px boxes, the art drawn over the background, one `vguiapi_t::CofInventory` entry). Plan: `docs/superpowers/plans/2026-10-14-inventory-12-slots.md` (11 tasks, full code; written 2026-10-14). Both committed (first commit of the work, `811cdd4`). Planning facts gathered by three read-only explorers and direct reads: client.dll accessor bytes at RVA 0x6CDD0 (`55 8b ec 8b 45 08 48 83 f8 05 77 43 ff 24 85`, thiscall, ret 4, ASLR-relocatable, first 15 bytes relocation-free); the retail userinfo is 248/256 characters; FreeVGUI has no file/command/sound access (hence the new entry); the per-map hook is `SV_CoF_StateActivate` (sv_init.c:599); retail art geometry measured (see plan Task 9).

### Done (2026-10-14, subagent-driven, tasks 1-11 plus a final review and fix wave)

What changed and where (made on local branches `inventory-12`, fast-forward merged into launcher `main` / engine and FreeVGUI `story-coop` on 2026-10-14, not pushed):
- **Engine** (`upstream/cof-enhanced/xash3d-fwgs-4857…/engine`, base `39d72549`, head `b9e0e54b`): `sv_cof_inventory12_patch.h` + `sv_cof_inventory12.c/.h` (in-memory `hl.dll` patch in two groups, "keep" and "wide", all-or-nothing, hash-checked against the retail `hl.dll`, reverted at six; gate opened per map in `SV_CoF_StateActivate`; `cof_inventory_slots` cvar, `cof_inv_slots` stufftext, `_cofinv` important userinfo key; `cof_inventory12_status`); `sv_cof_retail.c`/`sv_cof_share.c`/`sv_cof_story.c` use the active slot count (13 `inventory-slot` status lines per player, party/share logic); `COM_CoF_ModuleSha256Is` (client.dll hash check); client side `cof_inventory12*` state/patch headers and glue (client store, accessor hook on client.dll RVA 0x6CDD0, language-pack redirect through `CoF_Lang_GameFile`, `CL_CoF_Inventory12Ready` in `common.h`); `infostring.c`. Final wave: tell only `cs_spawned` clients and key on userid (host/reconnect keeps `cof_inv_slots 12`), quick select dispatches a synthetic QSlotUpdate to client.dll (picture no longer stale), `cof_inventory12_status` prints the post-array fields per person plus a count of nonzero dwords after slot 12, hardening M1/M5/M6/M10/M12.
- **FreeVGUI** (`3rdparty/freevgui`, base `981e45a`, head `e4a6d8c`): `CofInventory` grid and item menu (`cofinventory_logic.h` with the testable logic, layout 72 px boxes), one new `vguiapi_t::CofInventory` entry; outside-click close by edge detection, `mouseDoublePressed` forwarded; rebuild when the grid no longer hangs under the inventory background; the vgui.dll must come from the same build as xash.dll.
- **Launcher**: `tools/make_inventory12_art.py` (4x3 background art from the retail images, three-slot variant, Polish overlay; reads RLE TGAs since `159178d`), `tools/Prepare-CampaignTest.ps1` (installs xash.dll + vgui.dll together, `-ExpectedVguiSha256` and `-Vgui` mandatory, generates the art in both copies), tests (`tests/test_enhanced_inventory12.py` and engine harnesses), checklist `docs/campaign-test-guide.md` (12-slot section, 9 items), spec and plan under `docs/superpowers/`. Launcher base `811cdd4`.

Rulings that matter: the disk game DLLs stay stock (patch only in memory, reverted); story co-op only (`cof_story_coop 0` = retail untouched); `CL_CoF_Inventory12Ready` has a static-inline stub for dedicated builds; descriptor reads go through the language-pack redirect; the final review's C1/I1/I2 and M1/M5/M6/M10/M12 were fixed, all 17 ruled findings re-reviewed as addressed.

Commands run and evidence (`evidence/inventory-grid-20261013/`): full suite `python -m unittest discover -s tests` -> `full-suite.log` (168 OK, 0 skipped) and `full-suite-final-fixes.log`; builds `tools\build_enhanced.cmd` -> `build-final.log`, `build-final-fixes.log` (exit 0; also per-task `build-task6*.log`, `build-task8*.log`); mutation check 14 deliberate bugs -> `mutation.log` (12/14 caught first: slot-7 routing and accessor 14/15-byte compare survived) and `mutation-final-fixes.log` (14/14 caught after the fixes); staged `xash.dll` and `vgui.dll`; RE notes `client-inventory-re.md`, `server-inventory-re.md`, art previews `inventory12_*_preview.png`. Install: `pwsh -NoProfile -File tools\Prepare-CampaignTest.ps1 -ExpectedSha256 02F90A33… -Engine evidence\inventory-grid-20261013\xash.dll -ExpectedVguiSha256 D92A5D17… -Vgui evidence\inventory-grid-20261013\vgui.dll` (full hashes in the table below).

Install incident (dated observation, 2026-10-14): the first Prepare run installed the new pair in the host copy only, then stopped with "Inventory art generation failed: host": the Polish overlay original `640_inventory.tga` is RLE-compressed (TGA type 10) and `read_tga` accepted only uncompressed files. Fixed in launcher commit `159178d` (RLE decoding; an unreadable Polish overlay is skipped with one stderr line), found at install time, not offline. The second Prepare run (idempotent) finished both copies. Verified afterwards: host and guest `xash.dll`/`vgui.dll` hash to `02F90A33…`/`D92A5D17…`; `xash.dll.3D860BC7.bak` and `vgui.dll.F77963A0.bak` kept in both; `inventory12_left.tga` and `inventory12_left_three.tga` exist in `cryoffear/gfx/vgui/` and in the Polish overlay folder of both.

Verification states, kept separate: offline tests: done (168 OK, mutation 14/14). Build: done (exit 0). Installed in the disposable copies: done (not in `runtimes/*`, not in the Steam folder). Live: **not yet**; nothing was launched.

Known limits (deferred minors a tester may meet; the ledger is deleted after this):
- M2: a story that clears the inventory at six slots leaves items 7..12 hidden but kept (matches the spec).
- M3: no notice when a map drops to six slots (`reason=client:<userid>` or `cof_inventory_slots 6`).
- Quick select sends its command twice (the synthetic QSlotUpdate plus the real `quickselset`); idempotent and harmless.
- Parked: re-binding on the same inventory drops the remembered retail-button state (could leave retail buttons hidden on a later six-slot map; speculative).
- The 17 live checks from the Task 8 review (Bind succeeds with no "not the expected one" line, no retail flashes, right half usable, menu/grid overlap clicks, 6-after-12 restore, three-slot art, image alpha/scale, gamepad, UI scaling alignment, sounds, button origin vs art offset, TGA orientation, quickselset wording) are covered by the checklist; item 7 (older client) is run by hand, see the checklist. Item 6 has a baseline: repeat with `cof_inventory_slots 6` if a field differs.
- Minor, not user-visible: `Prepare` is not atomic across the two copies; the art tool swallows nothing but its stdout is discarded.

## Repository cleanup and sharing (2026-10-13)

The user asked to make the repository ready for invited playtesters and helpers (private GitHub, not public), with a fresh launcher history, the engine and its two patched UI submodules pushed too, and everything on main.

- Safety commits first: launcher `228dec9` on `split-play-stage3` (all uncommitted work), engine `f1c5eec` (client world guard, console filter) and `0b8b06b` (story co-op modules), engine `39d7254` (`.gitmodules`: `3rdparty/mainui` and `3rdparty/freevgui` point at `charliee1w/cof-story-coop-mainui`/`-freevgui`). The engine and both submodules were shallow clones; `git fetch --unshallow origin` from FWGS made them pushable (same commit hashes).
- Launcher cleanup: README rewritten for newcomers; `docs/building.md`, `docs/playtesting.md`, `.github/ISSUE_TEMPLATE/bug_report.md`, `docs/launcher.md` (the retail-engine launcher's old README sections) are new; old journals moved to `docs/history/` (links repointed; `docs/history/README.md` explains the anonymised names); the remote friend's name, personal paths and the user's handle scrubbed from all tracked files (scripts `scrub_docs.py`, `fix_links.py` in the session scratchpad only); `.superpowers/` ignored.
- Tools: the friend package is a generic playtester package (`CoF-Story-Tester-v<N>.zip`, blank name field, setup default `Player`); session `remote`, `state/enhanced-remote-session.json`, `cof_remote_host.cfg`, `story-remote-host.log`. The agent **copied** (not renamed) the local session state and host config, which carried the friend's name, to the new names; the originals are untouched. `tools/hlre.py` finds `hl.dll` relative to the repo. Launcher default player name `Player` (the user's saved setting keeps theirs).
- Tester package fixes (TDD): Enter and keypad Enter bound to `cof_skip`; the join writes `story-tester.log` (`-log -logtime`) and first moves the previous one to `story-logs/` (the engine opens logs with `w+`). The setup exe must be rebuilt (`tools/build_friend_setup.py`) before the next package; building or sending a package needs the user's go-ahead.
- Offline: full suite **156 OK** (`evidence/repo-cleanup-20261013/full-suite-final.log`).
- Published 2026-10-13 ~11:13 (private, created with `gh` as `charliee1w`): `cof-story-coop` (`main` = `5005823`, one commit; only `main` pushed, `split-play-stage3` stays local), `cof-story-coop-engine` (`story-coop` = `39d7254`, full FWGS history), `cof-story-coop-freevgui` (`981e45a`), `cof-story-coop-mainui` (`15edd2b`); default branches `main` / `story-coop`. Local remotes: launcher `origin` = the private repo; in the engine and both submodules the private repo is `coop`, and `origin` stays the FWGS upstream.
- Fresh-clone check (`evidence/repo-cleanup-20261013/fresh-clone.log`, script `fresh-clone.ps1` in the session scratchpad): the `docs/building.md` sequence from the private repos into `%TEMP%\cofcheck` (deleted afterwards): private submodules resolve, SDL2 download + hash, configure 13 s, build 33 s (all four DLLs), tests 156 OK (28 skipped without local game files). `--recurse-submodules` fails on Windows (`mbedtls` paths too long, and it stops the rest), so the guide initialises only the submodules the build uses. The session scratchpad itself is too deep for the engine's submodule git dirs.
- Not done: no one invited (the user chooses); no tester package built; the user's commit email stays visible (they did not ask for the no-reply address).

## Earlier (2026-10-12 late night): first live campaign session, and fixes

Live session 20:02–20:07 (solo mode, `c_intro`, test gun m16; logs `evidence/story-live-20261012/{host,guest}/campaign-*.log`, will be archived by the next Start):

- Worked: both clients connected; the camera watchdog held the late-joining guest (`camera-held`); both players got the M16 (`test-gun-given` x2) right after the intro.
- The intro (about 3 min, 10 cameras) cannot be skipped in multiplayer; the user asked for skippable cutscenes.
- **Black room:** the intro ends with cof_teleport `kunitei` moving the host into an 86-unit black loading room that holds the walk-in exit to `c_nightmare`. The party's teleport-follow found no spot beside the host (`teleport-expired reason=party-not-ready`), and the exit hold then waited for the AI-driven guest, which never presses J.
- **AI drive flapping:** `companion-driving`/`companion-released` 13 times each, within the same second. The client echoes the view angle the AI set (fixangle) a few usercmds late, and that was read as a person moving the mouse.
- **Crash (the user killed both players to get out):** the exit hold treated "nobody alive" as GO, sending two dead players to `c_nightmare`. The team-wipe recovery then loaded the `c_intro` autosave on the first frame after arrival while the host client was mid-signon. About 4 s later the retail client.dll crashed in its one-shot "Killing sky faces" routine (HUD_CreateEntities → 0x1005FED0 → 0x1005F130, fault 0x1005F168 reading a NULL sky `polys`; client.dll `D2A04641…`). Cause: the reloaded world was never set up by the client's R_NewMap. Static RE by a subagent; scripts only in the session scratchpad (`crash-re/`).
- **Noise:** 2,582 `cofstory phase=cmd` lines in 25 min (the voice handshake `VModEnable`/`vban`), drawn as notify lines on screen because of `-console`; plus `session-save-deferred` every 5 s through a cutscene.

Fixes (engine, offline-tested, not live-tested):

- **Cutscene skip** (`sv_cof_scene_state.h` `CoF_SkipHeld`/`CoF_SkipSteps`, `sv_cof_scene.c/.h`, `sv_main.c` `SV_RunGameFrame`, `sv_client.c`): client command `cof_skip` (Enter and keypad Enter, bound by `Prepare-CampaignTest.ps1`). While every living person is frozen in a running camera or model cutscene, the server runs `cof_skip_speed` (default 10, max 20; below 2 = off) `SV_Physics` steps per frame (sv_fps 0 only). The retail SP skip is the same idea (host_framerate; `common/host.c` comment, FWGS issue 2706); MP rules disable it. A skip ends 2 s after nobody is held, at a map change, or on a second Enter; anyone can start or stop it for the party. Logs `cutscene-skip`, `cutscene-skip-stopped`, `cutscene-skip-over`.
- **Intro end:** a scripted teleport whose destination has no free spot beside the owner shares the owner's spot (`sv_cof_party.c`, as the ending door already did; retail PreThink lets overlapping players pass). A second client the AI drives (`SV_CoF_CompanionAway`: drive on, not the leader, idle for `cof_companion_idle`) does not hold an exit (`sv_cof_exit.c`); it comes along as a stray and is regrouped on arrival.
- **AI drive:** a ring of the last 32 angles the AI handed the client; an echo of any of them is not a person (`CoF_CompanionEcho`).
- **Dead players never take an exit:** `CoF_ExitRequest` holds with waiter 0 when nobody alive can wait (it was GO); the old test that expected GO was changed deliberately.
- **Wipe recovery timing** (`sv_cof_session.inc`): not in a map's first 2 s, and not while any non-fake client is connected but not spawned.
- **Client crash guard:** `Mod_WorldGeneration()` (bumped when `Mod_LoadWorld` loads a different world); `cl.world_generation` is recorded after `R_NewMap` in `CL_RegisterResources`; `CL_EmitEntities` and `V_RenderView` return while they differ. Accepting `cof_story_changelevel` in `ca_connected` was tried and reverted: the server sends it twice by design, and `test_enhanced_client_transition` rightly expects the second copy to be ignored.
- **Intro logos/credits** (`sv_cof_moments.c`): each `CoFLogo` the game sends to one player is sent to the others next frame (`logo-shared`).
- **Noise:** `VModEnable`/`vban` are not traced; lines starting `cofstory ` or `[cof-story-transition]` never become on-screen notify lines (`client/console.c` `Con_StoryLogLine`), but stay in the console and the log; `session-save-deferred` once per deferral.
- Independent review (subagent, read-only): no Critical. 1 Important: the drive fix was partial, because `svc_setangle` carries `v.angles` (`sv_frame.c`) and the drive wrote only its yaw, so a stale model pitch never matched the ring. Fixed by writing all of `v.angles` (the HL fixangle convention). Minor findings fixed:
  - a dead waiter is never kept on a repeated exit request;
  - "held by a model cutscene" needs the player's own camera-model handle (`P+0x388`) and `fuser3 == 0`;
  - `cof_skip` is refused when `sv_fps` is not 0;
  - the teleport fallback lands on the teleport's own spot, not the owner's later one;
  - a wipe recovery that waits for a joining client logs `session-wipe-waiting` once;
  - wrapped story lines and `Con_ClearNotify` no longer bring story lines on screen.
  Left: logos queued in a map's very first frame are dropped; no unit test for the client world guard or the console filter (client code).
- Verification: full suite **153 OK** (`evidence/finish-20261012/full-suite-skip-2.log`). Mutation check **29 of 29** caught (`mutation-skip-3.log`; script `mut_skip.py` in the session scratchpad only). Build `build-skip-2.log` → `3D860BC7…`, staged as `evidence/finish-20261012/xash.dll` (previous staged builds kept as `xash.<hash8>.dll`). Installed with `tools\Prepare-CampaignTest.ps1 -ExpectedSha256 3D860BC7354464B200EA5A84FD187E2293D5C627D1AC79CFBAD665D38626A12D -Engine .\evidence\finish-20261012\xash.dll`; nothing launched.
- Live: **nothing verified yet**. Next test: Enter during the c_intro cameras (`cutscene-skip`, about 20 s for the intro, `cutscene-skip-over`); the guest lands in the `kunitei` room with the host (`teleport-party-moved`) and both go to c_nightmare without J; no `companion-released` flapping in solo mode; the guest sees the intro logos (`logo-shared`); no `cofstory` lines in the top-left corner.

## Earlier (2026-10-12 night): the test gun

The user asked: "Give me an infinite ammo gun that I can test the whole game with and that carries over."

- Engine `sv_cof_testgun.c/.h` (new): server cvar `cof_test_gun` (default empty = off; `weapon_m16` or `m16`; allowed: m16, shotgun, vp70, revolver, rifle, glock; not the donator-only TMP or the story's P345). Every **person** (not a companion bot) keeps the retail `m_bInfiniteAmmo` switch on (+0x21EA, through `SV_CoF_RetailSupply`: no shot, syringe or flare spent, a magazine in reserve). Each person also gets the gun through `SV_CoF_StoryGiveItem` when they carry none. Conditions for the gift: 2 s standing free on this map (alive, `MOVETYPE_WALK`, not `FL_FROZEN`, no foreign view entity, not in the c_ending4b wheelchair); no transition; no pending recovery restore (`SV_CoF_RecoveryPending`); ≥ 2 free slots, so a key still fits. Retries every 5 s; the gun is taken in hand for 3 s after a gift. Off or refused: the switch goes back off only for players this module turned it on for. Logs `test-gun-given/full/off/refused` (the run report counts them; `refused` warns).
- Carry-over: story transitions and checkpoints keep weapons, and recovery gives them back after a respawn. The module only re-gives a missing gun, which covers `cof_clearitems` (c_start, c_bridge bag, stairway).
- `sv_cof_retail.c/.h`: weapons table with classnames; `SV_CoF_RetailInfiniteAmmo`, `SV_CoF_RetailCarrying` (by weapon id, so aliases count; 2 = in hand), `SV_CoF_RetailInWheelchair` (`m_hWheelchair` +0x390 EHANDLE). `sv_cof_recovery.c/.h`: `SV_CoF_RecoveryPending`. `sv_cof_story.c`: wiring (frame after the party layer).
- `tools/Start-CampaignTest.ps1 -Gun m16|shotgun|vp70|revolver|rifle|glock|none` (default **m16**, host command line `+cof_test_gun weapon_<gun>`). Docs: `docs/campaign-test-guide.md`, README.
- Independent review (subagent, read-only): 1 Critical, the module went idle on every new map for as long as the previous map had run, because `sv.time` restarts at 1.0 per map (`sv_init.c`) and the tick gate came before the map check; 1 Important, a second loss skipped the 2 s settle. Both fixed with regressions. Minor fixes: no gift in a phone call or any `fuser3` state; the switch is turned off only if it was off before; the record is kept by userid through a level change; a pending recovery logs `test-gun-wait` once; docs say which checks need `-Gun none`; `cof_test_gun <other>` *adds* a gun. Left: a dropped gun stays on the floor (a new one is given).
- Tests: `tests/engine/cof_testgun_harness.c` + `tests/test_enhanced_testgun.py`, retail harness additions, run-report test. Full suite: **153 OK** (`evidence/finish-20261012/full-suite-testgun-2.log`). Mutation check: 37 of 37 caught (`mutation-testgun-3.log`; script kept in the session scratchpad only). Build `build-testgun-3.log`.
- Live: not yet verified. Watch for `test-gun-given` per person after `c_intro`, the gun kept across the first level change, and back after `c_start`'s inventory clear.

## Earlier (2026-10-12 evening): AI companion and one-person testing

What the user gets: `tools\Start-CampaignTest.ps1 -Mode solo` (default; two windows side by side, the AI plays the guest client while its controls rest, `cof_companion_drive`) and `-Mode companion` (one window, an AI bot player, `cof_companion 1`). `H` = `cof_companion_order` (wait/follow), bound by `Prepare-CampaignTest.ps1`. Player guide and cvars: `docs/companion.md`.

Engine (`engine/server/`):

- `sv_cof_brain.h` (new, pure): priorities dead (jump to respawn unless being revived) > suicide struggle (attack on alternate frames) > CPR in progress (hold +use) > held > leader down (walk up, ask for CPR) > fight (gun with rounds: sights tap `IN_CANCEL`, one press per shot, reload press; melee held within 30 u; never `IN_ATTACK2` except syringe on the partner) > self-heal (syringe) > medic (syringe on a leader at ≤ 50 HP, `IN_ATTACK2`, cooldown `cof_companion_medic`) > wait/follow (breadcrumb trail, teleport behind when lost/stuck/far) > idle (weapon in hand, sights down, reload).
- `sv_cof_companion.c/.h` (new): fake-client lifecycle (slot edict `SV_InitEdict`ed before `SV_FakeConnect`; at most 4 players; never removed while no human is alive, the DLL's game-over rule; gives up its slot to a joining person), perception (hostile visible monsters near the leader, engaged ones first; corpses keep health > 0 so alive = `DAMAGE_AIM`), actions, upkeep (bots: infinite ammo + reserve, own syringe if ≥ 2 free slots, co-op `flashlight` command; all: `tapgetup`), driving an idle human client (one-usercmd taps), `SV_CoF_CompanionTriggerTouch` (a bot touches no trigger except push, gravity and target-less hurt: no exits, scripts or items).
- `sv_cof_retail.c/.h`: `SV_CoF_RetailArms/Equip` (weapons found by `m_pPlayer == player`, `SelectItem` RVA `0xF1A50`), `Supply`, CPR (`Revivable` incl. the get-up `fuser3 3` guard, `Revive` = `pfnUse(body, reviver)`, `CprRepair` unlinks bodies of dead revivers and gets a reviver stuck in `fuser3 2` up), `Fallen`, `Command`, `Hostile` (denylist, `m_iPlayerReact`, `m_iClass`, relationship table `base+0x217118`, script-waiting bosses only once engaged). A private-data validation cache keyed by (pv, serial, spawncount, need), because `Mem_AllocatedSize` walks the whole pool.
- `sv_frame.c`: **level change drops fake clients by `FCL_FAKECLIENT`** (the retail Spawn clears the edict flag `FL_FAKECLIENT`, which also makes the bot immune to the human's shots; without this a ghost `cs_connected` slot stays per map). `sv_world.c`: the trigger filter hook. `sv_pmove.c`/`sv_client.c`/`sv_cof_story.c`: companion wiring. `sv_cof_party.c`: `SV_CoF_PartyPlaceBehind`.
- Research: `docs/research/2026-10-12-companion-weapons-items-re.md` and `2026-10-12-companion-bots-cpr-monsters-re.md` (static; open live questions at their ends). DispatchUse (`100169E0`) → player Use `100F88C0` and `SelectItem` `ret 4` were rechecked with `tools/hlre.py`.

Launcher repository: `tools/Start-CampaignTest.ps1` (modes), `tools/Prepare-CampaignTest.ps1` (`bind h`), `tools/story_run_report.py` (companion phases counted, `companion-refused` warns), tests `tests/engine/cof_brain_test.c`, `cof_companion_harness.c`, additions to `cof_retail_harness.c`, `tests/test_enhanced_companion.py`, run-report test; `docs/companion.md`, `docs/campaign-test-guide.md` (modes, AI checklist), README paragraph.

Independent code review (subagent, read-only): 1 Critical (a second CPR on a body playing its get-up) and 1 Important (target-firing `trigger_hurt`) plus 5 Minor; all fixed with tests (also: unconditional fake-client drop, no disconnect call on a never-put-in bot, stale taps cleared, a person's syringes never used by the AI driving them, equip log once per request).

Verification:

- Offline: `python -m unittest discover -s tests` → **151 tests OK** (`evidence/finish-20261012/full-suite-arms-3.log`, 72 s).
- Mutation check (`evidence/finish-20261012/mutation-companion-3.log`): 36 of 37 deliberate bugs caught; the survivor (dropping the person-medic gate) is equivalent, because `has_heal` is already gated.
- Build: `cmd /d /c tools\build_enhanced.cmd` → success (`build-arms-2.log`); staged `evidence/finish-20261012/xash.dll` (`95F8D556…`), the previous staged build kept as `xash.E0C77F7A.dll`.
- Deployment: disposable copies only, `tools\Prepare-CampaignTest.ps1 -ExpectedSha256 95F8D556BE93BE9CB6B2561D2BED5832DD18395CDCFA54C298DAA9375E996FB1 -Engine .\evidence\finish-20261012\xash.dll`.
- Live: **nothing verified**.

## Earlier session (2026-10-12 afternoon): story moments and polish, what changed and where

Engine (`upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/server/`):

- `sv_cof_scene_state.h`, `sv_cof_scene.c/.h` (new): cutscene watchdog. The retail `trigger_camera` already shows every player the camera (start loop `1011420B`); the watchdog releases a living player still viewing an ended camera (CPR revive), holds a respawn/rejoin mid-scene, takes a player out of a telescope another player switched off (the game's own `telescopecancel`), lifts the model-cutscene black wait when no `cof_mdlcutscene` runs (MP PlayerSpawn starts it for ladder/strangle camera models too), fades in late joiners, releases all camera viewers (dead too) before a transition save, defers co-op saves during cameras, and tells the party layer who watches the same running camera.
- `sv_cof_moments.c/.h` (new): fall-death floors (a `trigger_once` targeting `cof_killplayer`) kill only the toucher through the game's `ClientKill`; phone calls retarget/restart (rate-limited) and force their target after 30 s with nobody able to answer; dead players' `m_bInPhoneCall` cleared; no +use near a ladder someone climbs; wheelchair rider preferred as first player; ladder/chair pointers (and a climber's camera model) cleared on disconnect; yes/no answered only by a prompted player, first answer wins, a "yes" is final.
- `sv_pmove.c`: the acting player is "the first player" for game lookups while their usercmd runs (`SV_CoF_UsercmdSender`); ladder +use filter. `sv_game.c`: three-level first-player order (client-command sender, story preference, usercmd sender), gated on story mode; YesNo message observation. `sv_world.c`: fall-floor touch hook. `sv_client.c`: drop cleanup, yes/no command filter. `sv_save.c`: camera settle and (party) ending-hold release before transition saves.
- `sv_cof_party.c`: `cof_story_respawn_delay` (default 10 s; writes retail `+0x3D0/+0x3D4` at death); J refused while frozen; teleport/door follow for partners watching the owner's camera; the `c_village3` ending door takes every partner to the opener's `ending_exitN` room (blacked out, frozen, untargetable, invulnerable while held; overlap fallback for 3+ players; released on every exit path and before transitions); scripted weapon gifts (`cof_giveitems` with a classname: glock, flashlight, camera, shotgun) shared like the starter kit, remembered per player across maps.
- `sv_cof_state_data.h`, `sv_cof_retail.c`: shared `m_iDoctorWeapon` (`+0x5B0`, the `c_attic` doctor soft-lock) and `cof_begingame` stats (`+0x678..0x687`). `sv_cof_session_data.h`/`.inc`: checkpoint format **2**; older checkpoints are refused as "older build"; the first autosave of a fresh server is skipped only for a checkpoint that passes the load checks; saves wait for cutscenes and ending holds.
- `sv_cof_story.c`: `SV_CoF_StoryBase`, `SV_CoF_StoryFireTargets`, frame/shutdown wiring.

Launcher repository: tests (`tests/engine/cof_scene_*`, `cof_moments_harness.c`, additions to party/state/session/sender harnesses, `tests/test_enhanced_scene.py`, `test_enhanced_moments.py`, run-report test); five older harness tests now run `.\name.exe` (they failed only because the shell sets `NoDefaultCurrentDirectoryInExePath`); `tools/story_run_report.py` counts the new phases (`call-dropped`, `call-forced` warn); `tools/hlre.py` (read-only disassembly helper; needs capstone + pefile in a throwaway venv); `tools/Prepare-CampaignTest.ps1`, `tools/Start-CampaignTest.ps1`; research in `docs/research/2026-10-12-*.md`; `docs/campaign-test-guide.md`; README and `docs/story-checkpoints.md` notes.

Independent code review (subagent, read-only): "ready with fixes", no Critical. All four Important findings fixed with regressions (checkpoint format, stuck late-join wait, phone restart flood, gift re-give after map change); Minor findings 1–6, 8–12 fixed or covered; 7 (two ladder brushes pressed in the same frame) left as is.

Verification (this session):

- Offline: `python -m unittest discover -s tests` → **148 tests OK** (`evidence/finish-20261012/full-suite-final.log`, 65.9 s). Mutation check of the first scene rules: all six mutations caught.
- Build: `cmd /d /c tools\build_enhanced.cmd` → success (`evidence/finish-20261012/build-final.log`); staged copy `evidence/finish-20261012/xash.dll`.
- Deployment: disposable copies only, via `tools\Prepare-CampaignTest.ps1 -ExpectedSha256 E0C77F7A95BB43CF8D7B5B7E07FE8365F540E870C8CCF5AB278BF87711985A0F -Engine .\evidence\finish-20261012\xash.dll`; previous engines kept as `xash.dll.DF39B1A9.bak`; fixtures moved to `evidence/story-live-20261012/retired-20261012-153441/`.
- Live: **nothing verified**. The old test checkpoints (`baseline`, `geared`, `distinct`, `truncated`) are format 1 and will be refused; make new ones.

## Previous session (2026-10-12 morning): live defect and fix

Full checkpoint loading restored owned weapon entities, but their usable inventory slots were then cleared for both players.

Root cause: `SV_CoF_ShareRecovered` used `CoF_DescriptorSafe` to decide which restored items to reconcile against the shared-story-item ledger. Weapon, ammo, and outfit descriptor paths are also safe paths, but are not story-ledger items; recovery incorrectly removed them as if they were consumed keys.

Fix: use `CoF_IsStoryItem` instead. Preserve personal items and their slot order while still removing consumed shared story items.

- Source: `upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/server/sv_cof_share.c`.
- Regression: `tests/engine/cof_share_harness.c` checks that Glock, phone, ammo, and outfit slots survive import/recovery while a consumed key is removed.
- The regression failed before the fix; eight focused tests passed afterward.
- Temporary inventory logging in `sv_cof_session.inc` was removed from the fixed source. The older diagnostic DLL is now only the `xash.dll.DF39B1A9.bak` backup in the disposable copies.

The inventory fix is part of `E0C77F7A…` (installed in the disposable copies) but is not live-verified: the failing load scenario has not been repeated, and its `geared` fixture checkpoint is now a refused format-1 file.

## Previous session: verification and evidence

Evidence root: `evidence/story-live-20261012/` (git-ignored, local only).

Observed live, before the fix:

- Two real clients connected to the isolated loopback server on `c_start`.
- Checkpoint load restored the fixture's world-marker health and the presence/absence of its once-only trigger.
- Player restoration/sign-on completed, but usable inventory restoration failed as described above. A successful `session-loaded` line alone is not a passing inventory test.
- Two code-note probes produced successful calls and partner `note-shared` events. This does not prove the visible note contents, journal reopening, or HUD resynchronization.
- A truncated checkpoint was refused; the same map generation and current world stayed running.

Fresh offline verification after the fix:

```powershell
python -m unittest tests.test_enhanced_share tests.test_enhanced_recovery tests.test_enhanced_session
cmd /d /c tools\build_enhanced.cmd
```

- `share-recovery-red.log`: regression fails on the retained Glock slot before the fix.
- `share-recovery-green.log`: 8 tests pass, 15.388 seconds.
- `build-fixed.log`: x86 engine build succeeds, 2.992 seconds reported by Waf.
- The earlier 144-test full-suite result is under `evidence/story-state-recovery-20261012/`; it predates this live-test fix. Do not report it as a fresh full-suite run after the fix.
- `commands.log` records controller requests and redacted replies. Raw game logs may contain private RCON credentials; redact before displaying or sharing.

## Build identities at handoff

| Artifact | SHA-256 | Status |
| --- | --- | --- |
| `build-local-20260927/engine/xash.dll` (2026-10-19 night), swapped into the smoke copy (no package) | `61C160EEBB35385A7CCDA9FE1CCD92559A1A06C9DAF04EC1CD16D9C5EB261C89` | Build 3 plus the door-use probe (`sv_cof_useprobe.c`); engine `1ab3793e`; built and offline-tested, never played |
| `build-local-20260927/engine/xash.dll` (2026-10-19 evening), in the Test 2 build 3 package's payload (`evidence/respawn-crash-20261019/package/`) and the smoke copy | `14876831804D67D8E721C8D3CBE5E88790A0C2BA95C7027AC095120AFA8B1CF9` | Test 2 build 3: the respawn-crash fix (respawn back where the player came in; old-world decals dropped); engine `19224602`; built and offline-tested, never played |
| `build-local-20260927/engine/xash.dll` (2026-10-19), in the Test 2 package's payload (`evidence/test2-20261019/package/`) | `533ABC475CED0ED0D061B79786C7E050EC8C7D08B4BA2C3F5AE1F76256E085B4` | Test 2: the decompilation fixes, the death-notice fix, per-section ambient loops, companion items left behind; engine `b0edceee`; built and offline-tested, never played |
| `build-local-20260927/engine/xash.dll` (2026-10-15 11:42), in the tester package's payload (`evidence/tester-package-20261015/package/`) | `6C5DAB9BA7A13567EA96C2E307B8614B47496D58300A811B62E1C43F70A43D7A` | Tester package Test 1: everything below plus `cof_build` and `cof_continue`; committed (engine `d16d01f3`); not installed in the disposable copies |
| `build-local-20260927/engine/xash.dll`, staged `evidence/menu-text-20261015/ui-pass2/xash.dll`; installed in `{host,guest}` (2026-10-15 04:45) | `BD39CF728B69D221BC65447AC11FE716FA3CEB17EAAEB5906932039E67540E52` | 12-slot inventory + companion routes + the RCON echo fix; live in the host copy (2026-10-15 early morning) |
| `build-local-20260927/3rdparty/freevgui/vgui.dll`, staged `evidence/menu-text-20261015/ui-pass2/vgui.dll`; installed in `{host,guest}` (2026-10-15 04:45) | `40642512C3C2418B15B677701578A56BD30EA7A812108E2007022F464054E0AA` | Same build: the UI pass (text placement, grid alignment, full background art) |
| Earlier 2026-10-14/15 pairs (`evidence/companion-nav-20261014/`, `menu-text-20261015/`, `ui-pass/`) | `776ADF1C…` + `B82229CE…`; `776ADF1C…` + `81F77D78…`; `BD39CF72…` + `357321CF…` | Superseded in this order; the previous installed files are kept beside each copy as `*.dll.<hash8>.bak` by Prepare |
| `upstream/…/build-local-20260927/engine/xash.dll` and staged `evidence/inventory-grid-20261013/xash.dll`; installed in `evidence/story-live-20261012/{host,guest}/xash.dll` (2026-10-14) | `02F90A333AF5BBBF9C7150953BA7E0F1DA153912B1E22AE0AC3457FC00FF1A37` | The committed 12-slot inventory build (after the final review fixes); 168 offline tests; replaced in the copies on 2026-10-14 evening |
| `build-local-20260927/3rdparty/freevgui/vgui.dll` and staged `evidence/inventory-grid-20261013/vgui.dll`; installed in `{host,guest}/vgui.dll` (2026-10-14) | `D92A5D17D9880BFF623C397BC4413D9D011E07EEC8F63B54E90879D00DA5D87F` | Same build as the `xash.dll` above (they share `vguiapi_t`, install together); not live-tested. Previous `vgui.dll` `F77963A0…` kept as `vgui.dll.F77963A0.bak` |
| `upstream/…/build-local-20260927/engine/xash.dll` (before 2026-10-14) and staged `evidence/finish-20261012/xash.dll` | `3D860BC7354464B200EA5A84FD187E2293D5C627D1AC79CFBAD665D38626A12D` | Previous installed build (until 2026-10-14; kept as `xash.dll.3D860BC7.bak` in both copies): everything below plus the first live session's fixes (cutscene skip, intro end, team-wipe crash); 153 offline tests; not live-tested |
| `evidence/story-live-20261012/host/xash.dll` and `guest/xash.dll` until 2026-10-14 | `3D860BC7354464B200EA5A84FD187E2293D5C627D1AC79CFBAD665D38626A12D` | Installed 2026-10-12 late night by `Prepare-CampaignTest.ps1`; replaced on 2026-10-14 by `02F90A33…` (row above) |
| `evidence/finish-20261012/xash.684E0D54.dll`, `evidence/story-live-20261012/{host,guest}/xash.dll.684E0D54.bak` | `684E0D542EFF85A54CC9113BC930DCB86848EED12AEEE983C4CDDB6539ECD1B2` | Ran the first live session (20:02–20:07); the test gun, without these fixes |
| `evidence/finish-20261012/xash.95F8D556.dll`, `evidence/story-live-20261012/{host,guest}/xash.dll.95F8D556.bak` | `95F8D556BE93BE9CB6B2561D2BED5832DD18395CDCFA54C298DAA9375E996FB1` | Previous: the AI companion without the test gun; kept |
| `evidence/finish-20261012/xash.E0C77F7A.dll`, `evidence/story-live-20261012/{host,guest}/xash.dll.E0C77F7A.bak` | `E0C77F7A95BB43CF8D7B5B7E07FE8365F540E870C8CCF5AB278BF87711985A0F` | Previous: inventory fix + story moments/polish, without the companion; kept |
| `evidence/story-live-20261012/{host,guest}/xash.dll.DF39B1A9.bak` | `DF39B1A9601304546EFDD14528E88D92916914BDF0511092EC30D9FE402381E5` | Previous diagnostic build (inventory-loss bug), kept |
| `runtimes/enhanced/xash.dll`, `runtimes/enhanced-local-client/xash.dll` | `B1556A82FB1572725EED9C974FEA37BA62ABEFE28887FE5B3785C9781BBB9F2E` | Normal playing runtime; unchanged; matches `state/enhanced-remote-session.json` |
| (superseded) previous fixed build | `CEC49AA5EDE1F86769D985628FC0ADE88800F74C0D19F147782428707B412366` | Inventory fix only; no longer on disk as the build output |
| `evidence/story-state-recovery-20261012/xash.dll` | `AF011075E3325B677C7BB13718D9F530F1F3545AFC06C5F7B6CB79E4856C68D5` | Earlier staged build; does not include the live-test fix |

Recompute hashes before copying anything. Matching historical filenames do not prove matching builds.

## Disposable test setup

- Host: `evidence/story-live-20261012/host/CoFLaunchApp.exe`; guest: corresponding `guest/CoFLaunchApp.exe`.
- Both are full runtime copies with copied saves, not shared writable directories or hardlinked configs.
- Names: `Little Rascal 1` and `Little Rascal 2` (the user's "two little rascals").
- Host: loopback server port 27115, client port 27116, window 1280x720. Guest: ports 27119/27120, window 960x540. Both capped at 60 FPS.
- Since 2026-10-12 15:34 these are **campaign copies**: the `c_start`/`c_start2` fixture overrides were moved to `retired-20261012-153441/` (restore them to reproduce fixture scenarios), and `cryoffear/cof_campaign.cfg` (story co-op on, cheats off, J = `cof_warp`) was added. `cof_disposable.cfg` from the fixture test is still there.
- Start with `tools\Start-CampaignTest.ps1` (default `-Mode solo`: both windows side by side, the AI drives the guest while it rests; `-Mode companion`: host only plus an AI bot; `-Mode two`: two people; default map `c_intro`, `-Map c_start` skips the intro); logs `host/campaign-host.log`, `guest/campaign-guest.log`, earlier ones archived under `campaign-logs/`. Re-run `tools\Prepare-CampaignTest.ps1 -ExpectedSha256 <xash hash> -Engine <xash.dll> -ExpectedVguiSha256 <vgui hash> -Vgui <vgui.dll>` after a new build (both DLLs from the same build; the vgui arguments are mandatory).
- `prepare.ps1` created the copies and refuses existing destinations. Do not rerun it expecting an incremental setup.
- `control.py` targets only this test's loopback port and reads `private-session.json`. Never copy its credentials into notes or source control.
- Read-only status, while that isolated server is actually running: `python evidence/story-live-20261012/control.py status cof_story_status`.
- Old test checkpoints under `host/SAVE/cofcoop/` (`baseline`, `geared`, `distinct`, `truncated`) are checkpoint format 1 and are refused by the current engine ("older build").
- Older logs: `host/isolated-host.log`, `guest/isolated-guest.log`, `host/diagnostic-host.log`, `guest/diagnostic-guest.log`.

For native inspection, use the installed computer-use skill and `node_repl` / `@oai/sky`. Browser-only `cua_repl` has no native surface. A minimized game capture can show the desktop or another app; that is not proof of a black game. Never work around a user stop through another automation interface.

## Next steps, in order

0. (2026-10-19 night) College doors (section "College classroom doors lead back in"): the smoke copy has the probe
   engine. When the user plays it (Continue from the 21:52:37 autosave in c_college2, back through the door to
   c_college1, into the screwdriver classroom, try both doors from inside), read the `use-probe*` lines (since 1.1.8 the probe needs `cof_use_probe 1` on the host) at each
   `door-begin`: which brush the ray met first, and whether the fallback ranked the doors. Then fix with a test, and
   build a package (build 4) before sending anything.
   (2026-10-19 evening) The respawn-crash fix (section "Respawn crash after the second boss") is in the smoke copy,
   never played: the user's Continue from the c_sewer3 autosave, dying to the boss, should log `cofstory
   phase=respawn-moved` and stay in cm_sewer3. The zip to send is now build 3 (`evidence/respawn-crash-20261019/package/`).
   (2026-10-19) Test 2 (section "Test 2 without launching"): the user decides where "Report a bug" goes, merge/push of `tester-package`, and sending the zip. When the user allows a launch: one playthrough of a wave 3 chapter with merged maps on (the loops are heard only in their own section; doors land on their landmarks), the two-player join (`cofbuild test2`), and the paused runs (merged groups, two clients, the 197-exit sweep).
   (2026-10-15 midday) Tester package Test 1: with the user's go-ahead, the live check from the smoke copy (`evidence/tester-package-20261015/smoke/CoF-Story-Coop/CoF Story Co-op.exe`: Play solo → New game; then Host and `network.query_info` on 127.0.0.1:27015 shows `cofbuild test1`); then the user decides merge/push of `tester-package` and sends the zip. Building the next package: `python -m tools.build_tester_package --out evidence\<new folder>` after bumping `VERSION`/`VERSION_LABEL`/`BUILD` in `tools/coop_play.py`. Then the companion: the teleport rate on `c_apartment1`, the ledge jumps on entering `c_apartment2`, the bot standing in the player's view after the `c_start` opening. Live check still open: the objectives text on its paper (B) at 1280x720.
1. The user runs (or authorizes) the campaign test with the disposable pair: `tools\Start-CampaignTest.ps1`, checklist in `docs/campaign-test-guide.md`. Do not launch it without the user's go-ahead; say before launching any window.
2. After the session: `python tools\story_run_report.py <host log>` and record exact passing/failing cases here (redact RCON passwords).
3. Most valuable live questions (details at the end of each `docs/research/2026-10-12-*.md`):
   - c_intro end to end for both clients (cameras, the `kunitei2` teleport, the exit hold).
   - A CPR revive after a cutscene; a respawn while the partner climbs a ladder; a ladder pressed by both.
   - `c_park3` sophiecall (and the gate) with the call's ringer killed or without a phone.
   - The P345 answered by the guest, then the `c_attic` doctor fight.
   - The `c_village3` ending door with the partner far away; `c_ending4b` wheelchair with the non-rider landing the kill.
   - Fall-death floors (`c_subwaysick1`, `c_ending1b–3b`, `c_ending5`): only the faller dies.
   - Checkpoints: save, load, autosave and team-wipe recovery on the new format; the inventory-load scenario of the previous session.
   - The AI companion (`-Mode companion`): it is dropped and re-added cleanly at a level change (no second bot, no stuck slot); it takes a story gun in hand and fires/reloads (is one usercmd per press enough?); CPR on the host and the get-up; the syringe partner heal; its flashlight beam is drawn; it fires no exit, script or item; the `monster_crazybitch` struggle; monster damage scaling with it present. In `-Mode solo`, taking the guest over and handing it back.
4. Playtesters: superseded on 2026-10-15 by the tester app (item 0, section "Tester package"). Testers follow `docs/playtesting.md` / `READ ME FIRST.txt` and report with the issue template (GitHub needs an invite to the private repo).
5. Deploying `3D860BC7…` to the normal pair (`runtimes/enhanced`, `runtimes/enhanced-local-client`, and `state/enhanced-remote-session.json`'s hash) or building a new package for the remote friend needs the user's explicit go-ahead; nothing was deployed there.
6. Known, not addressed: delayed-chain look-at/grey fade/logos/zoom/blur on one player; `c_asylumb` puzzle view for a guest solver; `c_ending1c–3c` strangle host-only input; `c_college1` dropped-weapon restore; per-player phone-disable, hoodie, health cap, boss bar after respawn, two telephone users; no intro skip in multiplayer.

## Further context

- `docs/research/2026-10-12-story-moments-re.md` (index) and its four reports; `docs/campaign-test-guide.md`; `docs/superpowers/plans/2026-10-12-story-moments-and-polish.md`.
- Full map-chain audit (camera table, per-map listing, JSON graph): `evidence/finish-20261012/research/` (local only).
- `docs/story-checkpoints.md`: commands, persistence format, limitations, and live checklist.
- `docs/superpowers/plans/2026-10-12-story-state-recovery.md`: implementation plan.
- `docs/history/AGENT-HANDOFF-2026-09-27.md`: detailed older launcher/runtime history; newer facts in this file take precedence.
- `docs/research/2026-10-04-engine-coop-saves-re.md` and `docs/research/2026-10-04-hl-saves-death-items-re.md`: pre-existing research; preserve unrelated work.
- There are separate launcher and engine Git repositories. Inspect both explicitly; do not assume a launcher diff includes engine changes.

## Releases automated: the app updates itself, two-command releases (2026-10-20 afternoon)

User: "Can you automate the releases for updates somehow?" Chose (chat): the app updates itself from a public GitHub page that holds only the release zips; ModDB is filled in the user's Chrome by an agent, the user confirms before saving; 1.1.1 posted in Discord without a ping. Done by a separate session (bot + releases page + launcher code); this section is its handoff.
- **Public releases page** `charliee1w/cof-story-coop-releases` (public; created 2026-10-20 with `gh` as charliee1w): README and release `v1.1.1` = `CoF-Story-Coop-1.1.1.zip` (`9498F12B…`, 110,799,327 bytes, from `evidence/release-1.1.1/package/`) + `release.json`. Only zips; the code repos stay private (each zip carries the GPL source).
- **App updater** (launcher; in no released package yet, the first version built from here on has it): `tools/app_update.py`: `release.json` schema 1 (`release_info` writes it, `parse_release` checks it), read from `…/releases/latest/download/release.json` (no API, no rate limit), download with size + SHA-256 check (`.part` removed on failure or Cancel), unpack with `friend_setup.safe_zip_members`, start the new exe with `--update-from <copy> --wait-pid <this app's pids>` (`CREATE_BREAKAWAY_FROM_JOB`, plain if refused), `wait_for_exit`, `clean_downloads` (`%TEMP%\CoF-Story-Coop-update`, not while running from it). `tools/coop_app.py`: the main screen checks once per run in the background (offline = no offer) and shows "Version X is out." with **Update now** / **What's new**; Update now refuses while this copy's game runs, shows the download in the progress panel (Cancel), starts the new app and closes; the new app (`--update-from`) waits for the old one, then runs the normal update by itself. Hidden `--update-check OUT` writes what a packaged exe sees (the release check uses it). Readme UPDATES section and `docs/playtesting.md` updated.
- **Release pipeline**: `tools/release.py build` (preflight against the releases page, full suite, the package, every check of `evidence/release-1.1.1/check_release.py` generalized + the packaged exe reading the page + the published version updated by the new one, `release.json`, `moddb-text.txt`, `build-state.json`) and `publish` (refuses unless that build passed for the checked-out commits; `gh release create`). Routine: `docs/releasing.md`. Notes players read: `docs/releases/<label>.md` (1.1.1's added). Run with `%USERPROFILE%\miniconda3\python.exe`.
- **Discord**: `cof-coop-bot/cogs/builds.py` checks the releases page every 5 min and posts a new version in #downloads and #announcements (no ping unless `RELEASE_PING=1` in the bot's `.env`) and records it for `/build latest`. 1.1.1 went out that way at 13:10.
- **Pitfall found**: PyInstaller run from a shell without conda's `Library\bin` on PATH leaves out the OpenSSL DLLs, and the packaged app then can't import `_ssl` (`evidence/app-update-20261020/frozen/pyi2.log`, `console.err`). The 1.0–1.1.1 builds had it on PATH (no warnings in their `pyinstaller.log`). `release.py` now puts it on PATH itself; `--self-test` and `--update-check` would catch it.
- Verification: offline `tests/test_app_update.py`, `tests/test_release.py`, `tests/test_coop_app.py` 43 OK (miniconda 3.13, `evidence/app-update-20261020/new-tests.log`); full suite 545 OK, 1 skipped (674 s, `evidence/app-update-20261020/full-suite.log`). Live against the real page (`evidence/app-update-20261020/live_check.py`/`.out`): build 5 is offered 1.1.1, build 6 nothing; download 4 s, SHA-256 OK; the downloaded exe's `--self-test` 0. A packaged (one-file, windowed) check reads the page over HTTPS (`frozen/result.json`). The window flow (Update now → new app → update) is tested offline only; its first live run is the first update after the next release. No game launched.
- Pipeline exercise (2026-10-20 13:40-13:51, `evidence/app-update-20261020/pipeline_exercise.py` → `pipeline-exercise/release.log`): `release.check_package` on the published 1.1.1 package: 30 ok, 2 expected failures (the readme changed since 1.1.1; the 1.1.1 exe predates `--update-check`): zip, payload, `--self-test`, a fresh install (261 s), the published 1.1.1 downloaded, installed (230 s) and updated over (160 s, a save kept), test copies deleted.
- Next release: follow `docs/releasing.md`. Its ModDB step (zip through the Windows file dialog) has not been exercised yet.

## 1.1.2 out; 1.1.3: textures on AMD (2026-10-20 evening)

- **1.1.2 published** (the blur fix + the app updater): GitHub release `v1.1.2` (zip `F18BC953…`, 111,082,818 bytes, `evidence/release-1.1.2/`), Discord posted by the bot, ModDB filled through "upload a file from a URL" (the add-file form's `filedataWget` field takes the GitHub download link; no Windows file dialog) and saved at the user's OK.
- **Tests in parallel**: `python -m tools.run_tests` (launcher `5a60039`): one process per module, slowest first, `test_map_merge_groups`' long test split 8 ways (`COF_TEST_SHARD`); `release.py` uses it.
- **AMD Radeon RX 5500 (player report, video + `story-session.log`)**: the sky showed walls, a grey wall in front of the player, `GL_INVALID_OPERATION while uploading #HUD/sprites/hi_rain(frame:00).spr`. Not shaders (`gl_vbo`, `gl_specular`, `gl_bump`, `gl_renderer` changed nothing for him). Cause: client.dll binds the engine's texture numbers straight to GL (`gl_studiomodel.cpp` `glBindTexture( ptex->index & 0xFFFF )`, `gl_model.cpp` sprite frames) and names its own textures from `1 << 25` without `glGenTextures` (`gl_texloader.cpp` `current_ext_texture_id`, the sky's six faces in `gl_sky.cpp`). A driver that offers the name after the highest in use then hands the engine names in the client's range, so slot != name and the two overwrite each other. Fix (engine, `ref/gl/gl_cof_texnames.h` + `gl_image.c` `GL_AllocTexture`/`GL_CoF_NameTaken`): a name the driver offers at or past `MAX_TEXTURES` is replaced by the lowest free slot's number (skipping the skybox hack's 5800..5806), logged once as `[cof-textures] the driver offered texture name …`. It does nothing on drivers that offer the lowest free name. Tests: `tests/test_enhanced_texnames.py` + `tests/engine/cof_texnames_harness.c`. **Not checked on AMD**: the player's next log should carry the `[cof-textures]` line and a normal sky.
- Commits: engine `tester-package` `b421a491`, launcher `5532a9c` (1.1.3 prep). Full suite 553 OK, 1 skipped in 134 s (`evidence/texnames-20261020/suite.log`); engine build `build.log` there.
- 1.1.3 = version `1.1` (plays with 1.1 to 1.1.2), build 8; the fix is in each player's own renderer (`ref_gl.dll`), so the player who saw it updates.

## 1.1.4 to 1.1.6: AMD depth range, a join crash, level-change leftovers (2026-10-20 night)

- **1.1.4 (published): the AMD sky was the depth range, not texture names** (1.1.3's theory was wrong: no `[cof-textures]` line in the player's next log). Cry of Fear's `opengl32.dll` (Paranoia's, `PARANOIA_HACKS_V1`) turns `glDepthRange(0,1)`/`(0,0.5)` into `(0,0.8)`; client.dll draws 3D-sky entities in 0.8..0.9 and the sky box in 0.9..1 after the world. SDL's `WIN_GL_GetProcAddress` asks `wglGetProcAddress` first and AMD answers it for GL 1.1 functions, so the engine got the driver's own `glDepthRange` and the world spanned 0..1. Fix: `ref/gl/gl_cof_depthrange.h` (`CoF_DepthRangeFar`), used in `gl_rmain.c` `R_Clear` and `gl_studio.c` `R_DrawViewModel` (logged once `[cof-depth] …`); reproduced on NVIDIA with a copy of the dll patched to skip its rule (`evidence/amd-repro-20261020/`, byte 0x11d4 0x74->0xEB). The player confirmed it fixed. Engine `2911d77e`; tests `tests/test_enhanced_depthrange.py`.
- **1.1.5 (published): join crash** `cl_parse.c:2711` (Sys_Crash dialog): a client reconnecting mid level change (`cof_story_changelevel`) skipped `CL_InitEdicts` and parsed movevars into a NULL `clgame.entities`. `CL_ParseServerData` now inits edicts when `clgame.entities` is NULL; `CL_ParseMovevars` checks it. Engine `377f0ab0`; test `tests/test_enhanced_reconnect_edicts.py`.
- **Discord bot**: the status now leads with "📦 <version> is out, free on ModDB" and is refreshed right after a release is posted (`bot.py` `show_status(newest=True)`, `cogs/builds.py`).
- **1.1.6 (published 2026-10-21: GitHub latest + ModDB)**, from Xavier's host log (`story-session (2).log`, 23 level changes) after the user: "Hands disappearing when going through doors. Generally unreliable when transitioning between maps." / "flashlights turning off when entering new areas. Or any door." Engine `b06ea6c3` (`engine/server/sv_cof_arrival.h/.c`), launcher `826e9fa`:
  - Hands: a door to another map ends (EndSequence clears HIDEHUD 1|8) in the frame the level change starts; the game's HideWeapon goes out a frame later from UpdateClientData, so it never does. The client keeps 9 (no viewmodel: `StudioModelRenderer.cpp:1144`), and the restored player's `m_iClientHideHUD` is a fresh 0 like `m_iHideHUD`. Story transitions get no ResetHUD/InitHUD (`gInitHUD` is set only by `ResetGlobalState`, `world.cpp:455`; no `Blur` lines after any transition in the log). Fix: `SV_Begin_f` → `SV_CoF_ArrivalSignon` for carried players; the frame sends `HideWeapon` (m_iHideHUD at P+0x2500) and lets a player carried over mid-door out of it (P+0x1EA4, FL_FROZEN/NOTARGET, EF_NODRAW). Log `phase=arrival-hud`, `phase=arrival-door-finished`.
  - Torch (F = co-op torch, frags 32, `cof_coop_flashlight` was on): client.dll draws torches from `g_PlayerExtraInfo[i].frags` (`gl_studiomodel.cpp:656`, from StudioCalcAttachments of any drawn model); VidInit wipes it (`ScorePanel::Initialize`); PostThink writes `pev->frags = m_iFlashlightFlags` whenever the flags differ from the (fresh 0 after restore) client copy (`player.cpp:6293`). Log: F pressed twice after nearly every level change. Fix: per-player torch kept as switched (around the game's F: frags set to 32/0 first, so one press switches), bit 32 put back each frame, ScoreInfo for all at sign-on (deaths/class/team cached from the game's own ScoreInfo). Log `phase=torch-kept`, `phase=torch-switched`.
  - Bounce: cm_park2 <-> cm_park3 ping-pong (23:00:02 door, 23:00:18 a partner dodged into park3's exit back, 23:00:24 the host restored inside park2's exit at sign-on). `SV_CoF_ArrivalRefuses` in `pfnChangeLevel` (after the section move, before the hold): a touch does not count for a player standing in an exit since the party arrived, nor in the first 15 s at the exit back (same landmark position as arrival), unless they were in an inter_door in the last 3 s; never while an exit hold is live, nor for exits that fire targets or are scripted. Log `phase=exit-arrival-refused`.
  - Verified: engine builds clean; `tests/test_enhanced_arrival.py` + the tests that read the touched code (117 OK after moving the F hook before `SV_CoF_CommandSender` for `test_enhanced_sender`'s window). **Not played.** Package `evidence/release-1.1.6/` (zip `14267DEE…`, 111,095,585 bytes); `release.py build --skip-tests`, its install checks fail only because `evidence/tester-package-20261015` is a junction to `D:\Offload\CoF-evidence\` ("Choose an ordinary folder without links"). The user's copy (`%LOCALAPPDATA%\CoF-Story-Coop`) updated to 1.1.6 (build 11).
  - Log review, second pass (the user: "debug the rest of the logs"; Xavier's host log + the three client logs in `evidence/coop-session-20261020/`), fixed in the same 1.1.6 (engine `4512749d`, launcher `da2434d`):
    - **Save sounds missing in every copy**: `friend_setup.EXCLUDED_DIRS` matched a folder named `save` at any depth, so `cryoffear/sound/save/save_{yes,no,sequence}.wav` (present in Steam) never reached a copy (`Could not load sound sound/save/...` at every map load, all logs). Now saves/logs folders are left out only at depth 0-1 (`excluded_dir`); `update_copy` calls `restore_left_out` (copies missing files from `source_game` that the old rule skipped). Only `sound/save` matched in the Steam install.
    - **A guest paused everyone**: `GOR3 paused the game` x4 (14 s frozen); config binds BACK and PAUSE to `pause`, Xash `pausable` is 1. `CoF_GuardRefusal`: `pause` from a non-host in story co-op -> `host-pause`, the guest gets "Only the host can pause a co-op game."
    - **Items lost when a joiner drops while loading a level change** (Charlie's 1.1.4 crash at 22:29 ET: rejoined with only `starter-shared` weapons): `SV_CoF_RecoveryDrop` captures only `cs_spawned`. `SV_CoF_ArrivalRestored` (sv_save.c restore) marks carried players; `SV_DropClient` (and the reconnect path) call `SV_CoF_RecoveryDropCarried` when `SV_CoF_ArrivalLoading`.
    - Checked and left alone: `pl_wade*.wav`, `krypande/k_crawl*.wav`, `models/grenade.mdl`, `plats/ttrain_*.wav` are missing from the Steam install too (retail); `OpenGL Error: ... while uploading #HUD/sprites/hi_rain|waterring|snowflake` (AMD INVALID_OPERATION, NVIDIA INVALID_VALUE) are stale errors: `GL_CheckTexImageError` reads `glGetError` without clearing it first, so whatever GL call failed earlier in the frame (client.dll's renderer) is blamed on the next mid-game upload; `RestoreDecal: couldn't restore entity index` (decals on entities the new map lacks); `Failed to load bitmap font for scheme "Default Text"/"impact"`; teleport `cut-off` prompts are design (a partner far from a scripted teleport is offered J; the message says "can't follow" even when the trigger stays); `door-expired reason=party-not-ready` once (the partner took the door himself); `cmd-suppressed` = the log's own spam limit for dodge commands. Transition times 5-14 s (60 s once: the crash).
  - Also seen in the log, not addressed: frequent `RestoreDecal: couldn't restore entity index` errors.
- Repro rig (kept): `evidence/coop-repro-20261020/host|joiner` (1.1.5 copies; session cfg has `rcon_password`, binds F9 `changelevel2 c_apartment1 stla`, F10 `impulse 100`), `capture.ps1` (PrintWindow by copy), `key.ps1` (focus + keybd_event), `place.ps1`. `rcon.py` never got a challenge reply.

## 1.1.7: faster level changes (2026-10-21)

- The user: "any way to get level changes to be faster?" then "go online and figure out how to do this best. don't need live tests for everything. Most of this stuff is already documented online."
- Cause (code, not measured live): a guest's sign-on after every story level change goes out as reliable fragments, stop and wait (the next fragment only after the client acks the last; the server sends a connecting client one packet per packet it gets, `sv_frame.c`, no rate choke before `cs_spawned`). The engine's sign-on fragment is cl_dlmax less `HEADER_BYTES`: 1366 bytes at the co-op's `cl_dlmax 1400`, so the sign-on costs about one round trip per 1.4 KB (LZSS already on). Map loading from disk is unchanged by any of this.
- **Sign-on fragments** (`engine/server/sv_cof_signon.h`, `SV_GetFragmentSize`): a client with `NET_EXT_SPLITSIZE` that is not `cs_spawned` gets fragments of `COF_SIGNON_WINDOW` (10) split pieces: `10 * (cl_dlmax - 10) - 64` = 13,836 bytes. `NET_SendLong` already cuts any server datagram above cl_dlmax into cl_dlmax pieces (10-byte `SPLITPACKET` header) and `NET_GetLong` puts them back (up to 64 KB, 131 pieces), so no datagram on the wire grows and nothing is IP-fragmented (RFC 8900); ten segments is TCP's initial window on the same links (RFC 6928). Netchan limits checked: fragment fields are 32-bit bit counts (`FRAGMENT_MAX_SIZE << 3`), `NET_MAX_FRAGMENT` 65535 per packet. In game nothing changes (half of cl_dlmax, or cl_frmax). Each burst adds `NET_SendLong`'s 100 us sleep per piece (about 1 ms per fragment on the host). Older 1.1.x guests receive it with the same code.
- **Socket queues** (`net_ws.c` `NET_RaiseSocketBuffer`): SO_RCVBUF/SO_SNDBUF raised to 256 KB when smaller, so a burst waits for the next frame. Windows 7 gave UDP 8 KB (Factorio and FFmpeg hit the same thing); the user's Windows 11 gives 64 KB (checked).
- **Microphone** (`voice.c` `Voice_Init`): each story sign-on's `svc_voiceinit` re-ran the whole init without a disconnect: new Opus mode/decoders/encoder over the old ones (leaked) and `VoiceCapture_Init` closing and reopening the SDL capture device. Same codec, quality and `cl.maxclients` now keeps what is open (opens the device only if it isn't); another codec calls `Voice_Disconnect` first.
- Verified: engine builds clean (`tools\build_enhanced.cmd`), `tests/test_enhanced_signon.py` (harness `tests/engine/cof_signon_harness.c`) + version/arrival/sender/voice tests: 62 OK. **Not played.** Engine `e57595d6`. Version 1.1.7, build 12 (`VERSION` stays 1.1). Release notes `docs/releases/1.1.7.md` (Idiolect).
- **Published 2026-10-21 ~03:10 PT** at the user's go-ahead. Build `evidence/release-1.1.7/` with `--source "D:\Offload\CoF-evidence\tester-package-20261015\smoke-source\Cry of Fear"` (the junction's real path: the install checks refuse a path through a link) and the full suite (570 OK); the fresh install check passed. The GitHub release was made with `gh release create` directly (same arguments as `release.py publish`, `release.json` written by hand in `%TEMP%\cof-signon\gh\`) before the 1.1.6 -> 1.1.7 update check finished, so `build-state.json` may not say passed. Zip `CB91435B…`, 111,102,149 bytes, MD5 `6CD37159…`. ModDB file `cry-of-fear-story-co-op-117` from the GitHub URL (add form: "upload a file from a URL", then **Upload from URL**; a preview image is required: 1.1.6's reused, `evidence/release-1.1.7-first/moddb-preview.jpg`); ModDB MD5 matches. `evidence/release-1.1.7-first/` is the first build (`--skip-tests`, junction source; not published).
- Release tooling: `release.py` must run with `%USERPROFILE%\miniconda3\python.exe` (PyInstaller); `py -3.14`/`-3.12` don't have it.
- If a guest's sign-on ever stalls or times out after this (a burst lost every time, e.g. a VPN with a small MTU), set `COF_SIGNON_WINDOW` to 1 (the engine's own size) or lower the guest's `cl_dlmax`.

## 1.1.8: the debug pass (2026-10-21)

The user, after the boat question: "go through the rest of the game scripts and find any other shit like that. I want to make this next update the last one we do for a while, so do a debug pass over everything." Later: "Also, items aren't shared as they're supposed to be." (no log; the newest log on this PC is 1.1.4-era).

- **Review notes** (the findings, with evidence): `/home/claude/dbg/notes/{E1,E2,E3,K,L,M1,M2,M3,CH}.md` in the cloud session that did the pass (not in this repo). E1 story flow (exits, sections, arrivals, party), E2 companions/retail/recovery, E3 client and sign-on, K entity classes, L launcher, M1/M3 map audits, M2 the remaining maps, CH chapter starts.
- **Engine** (`tester-package`): `010c0324` (the pass), `a52071b6` (items), `cd573a67` (final review); on top of `effae680` (every partner follows the boat rider to the dock, done after 1.1.7). Highlights:
  - Level changes: `SV_FinalMessage` sends `cof_story_changelevel <spawncount>`; `SV_CoF_StoryNotices` tells a guest still signing on again every 2 s (`changelevel-told-again`); a `begin` for a map left restarts the sign-on (`SV_New_f`); guest sign-ons LZSS; the client acts once per epoch. F9 between maps stays co-op; the prediction copy has room for client.dll's donator field; `map c_game_menu1` (the credits' end) returns to the menu in co-op too.
  - Exits: `SV_CoF_MapExitModels` counts a reusable `trigger_multiple` firer as a walk-in exit (36 story exits; c_apartment3 `changefitta` and c_apartmentsick `changekuk`, fired by trigger_once, stay scripted); the arrival refusal covers them; section moves no longer pull everyone during the cooldown (`CoF_SectionMovers`: occupants without activators move nobody); arrivals protected after a section move.
  - Party (`sv_cof_party.c`): walk-in teleports not followed, deadly-hurt teleports never followed, placement waits out scenes, cut-off prompt repeats (3 x 10 s), respawn/arrival never in solid, shared-weapon gives back off (5 to 60 s), door companions who went their own way stay.
  - Chapter starts: the app writes `cof_chapter_start <map>` (not New game, not Continue); the first story frame grants the kit (`sv_cof_chapter_kit.h`; `SV_CoF_PartyGrant`: shared as if carried, on that map only) and presets `doctor_weapon` 2 / three slots (`SV_CoF_StatePreset`). Chapter 5 starts at `c_subwaysick2` (launcher). Logs `chapter-start`, `chapter-kit`, `chapter-state`.
  - Items: taclight and phone battery (`item_glocktaclight`/`item_phonebattery`, MyTouch needs `m_afButtonPressed & IN_USE`, P+0x22B0) now reach partners (the give presses Use for that call); fuses and ladders counted per copy; gives in flight settled across level changes and recoveries.
  - Story moments/scenes (FXB): wardrobe camera is its user's; permanent `env_fade`s per activator; set-only globals for the 15 toggling `env_global`s; c_down ladder kill dropped; boat riders released before a level capture (`SV_CoF_MomentsReleaseRiders` in sv_save.c); GreyFade mirrored (only GreyFade: the CoFLogo mirror still never runs); stopped wheelchair; crazybitch slot-one remap only in the gun think (plus `MAX_CLIENTS` free edicts past the array, sv_game.c, for her Look's walk); death-trap teleports become fall floors; scene entrance siblings (c_intro, c_nightmare, c_roofs).
  - Retail/recovery/companions (FXC): CPR/fall get-ups, leaver weapons let go before freeing, chainsaw victim's inventory kept (capture per boss think within 128 u; costs a 4 MB alloc per think while someone is that close), P345/sledgehammer left in the world by a leaver (a rejoiner can end up with a second one), fog and weapon pose per player, bot fixes, latches per person; `cof_use_probe` (default 0) and the test gun need `sv_cheats 1` (Start-CampaignTest passes it).
  - Checkpoints: an old checkpoint is kept on the first map only for Continue; a new game or chapter start saves its own first.
- **Launcher**: `af47d10` (the pass: Repair, unfinished-setup delete/retry, modded files refused before the copy, J/H/Enter in `kb_def`/`kb_act`, firewall in the No answer text, cofmaps join check, 20 logs kept, off-UI-thread checks, `sv_connect_timeout_ban 0`), `3988d9a`, `d55293f`, `ee38332`; 1.1.8 build 13 (`VERSION` stays 1.1). Release notes `docs/releases/1.1.8.md` (Idiolect).
- **Not fixed (known)**: the strangle valve's turner (no user recorded); joiners after c_bridge can get a party gift into hidden slot 4+ before three-slot mode applies (partners still hold it); a pickup in the frame right after a take of the same fuse; `section-move-nobody` logs once a second while someone idles in a section exit back; the chainsaw capture cost above.
- **Not played.** Offline: full suite on this PC (MSVC), engine builds without warnings.
- **Published 2026-10-21 ~06:38 PT** (the user: "Finish now."). GitHub `v1.1.8` (latest; `release.json` build 13) made with `gh release create` directly while the build's 1.1.7 -> 1.1.8 update check was still running (the full suite, 629 OK, and the fresh install of this zip had passed; the same update check passed on the build just before the review fixes, `evidence/release-1.1.8/stale-before-review/`). Zip `F88748CB…`, 111,178,753 bytes, MD5 `98833e91…`; ModDB file `cry-of-fear-story-co-op-118` from the GitHub URL, preview image as 1.1.7, MD5 matches. `release.json` for the upload was written by hand in `%TEMP%\cof-118\gh\`.

## 1.1.9: polish (2026-10-21)

The user, after 1.1.8 went up: "Now I want to do all polish items, get it working really well", plus the gas mask on others ("is there a way for us to show the gas mask on a player when it's on?"), movement sync ("They don't sprint properly"), ladders ("people turn into a floating pair of hands when using ladders, and ladders are kind of laggy"), and world weapons/ammo/meds per player (his pick: "Everyone gets their own"). "You can refer to the decompilation of the game for any of this info involving what the original game's behavior is."

- **Input**: the inventory of open issues after 1.1.8 (`open-items.md` in the cloud session's scratchpad, IDs X/P/S/I/C/N/AI/R below), with research briefs for the gas mask, movement and ladders. Eight fix branches (fxl, fxt, fxm, fxv, fxn, fxi, fxp, fxw), each with its own tests, merged by the lead; then an independent review.
- **Gas mask on others**: `tools/prepare_coop_weapons.py` makes `models/coop/h_gasmask.mdl` from the stock `models/items/w_nightvision.mdl` (a GASMASK bone under `Bip01 Head`, fitted from the real meshes: `tests/test_coop_gasmask.py` RealMaskTests need `COF_GASMASK_FILES` or the game). Optional source: a modded `w_nightvision.mdl` leaves only the mask out (Setup/Update not blocked). The engine (`weaponmodel`) puts a follower entity (aiment, bone merge) on a player whose night vision is on (flashlight flags P+0x5D0 bit 64), host cvar `cof_coop_gasmask`. The model is precached only when the file exists on the host.
- **Ladders**: `cof_ladder_manager`'s `camera_model` (`models/ladder_climb.mdl`) is shown to the others instead of the floating hands; the climber is drawn with the climb; a guest's view on the camera model moves on between packets (client); a climber's W/S read from their usercmds.
- **Movement**: 100 updates and commands a second, `sv_maxupdaterate 100`, `ex_interp 0.1` (settings); players stamped by their own commands, animtime at 1 ms (`sv_cof_cmdclock.h`); a partner whose history was reset holds the oldest sample (no snap-back); sprint keeps the game's sprint sequence (pev->flags bit 18); AI companions sprint to catch up while their stamina (P+0x21F0) lasts.
- **Per-player map pickups** (`sv_cof_pickups*.{c,h}`, host cvar `cof_coop_pickups`): weapons, ammo and meds a map places get a copy per player (SF_NORESPAWN, shown only to its owner, +use blocked for others in `pfnFindEntityInSphere`), a takers ledger by player name kept with checkpoints; story items stay shared. Never takes the game's last edicts.
- **Story/party**: S1 strangle to the last valve turner; S2 delayed chains go on after their starter leaves; S3 CoFLogo mirrored (intro logos for all); S5 merged-map fades/titles stay in their section; S6 music for partners taken through doors; S8 removecamera for everyone; S13 camera ends no longer free climbers/riders; S15/S17/P1 teleports and doors take partners; S18 credits for late joiners; S19 phone call locks cleared; P2 c_bridge carriage; P3 bounce triggers; P4 wheelchair; P5 the rower's frozen body hidden; X1-X5 exits and arrivals; AI2 unanswered calls move on.
- **Items**: I1/I2 share ledger; I3 three slots before any gift; I4/I5 grab captures and leaver P345/sledgehammer; I6 party gift list rebuilt after a checkpoint load; I7 checkpoints wait for the chapter kit; I10 slots 7-12 told when kept. Chapter kit: C1 c_park3 switches `stater` on (no Chapter 3 replay), C2 Ending 4 kit.
- **App/release**: L14.3/7/8 setup paths (links resolved, Program Files/OneDrive refused, non-ASCII), L14.4/6/11, R2 client.dll section names, R4 stamps, N1 `cof_signon_window`; the use probe is on by itself in c_college1 (R1).
- **Left open on purpose**: S16 (cm_start easter egg), S10 (entity restore by section), the rest of S11, S13's `ryesno`, S14 (not a bug per the decompilation), L14.1, the uninstall line, the "-dirty" banner, the C1 start spot, R1's cause.
- **Live checks pending (not played)**: P1 one-way teleports (c_nightmare, c_ending*b, c_bridge telemia, c_ending5b); S17 prompts (c_start2 cafedoors, c_subway2st2, c_hamn); S8; pickups (c_apartment1, the bonus room, flares, companion CPR); ladder/sprint/mask follower; the C1 c_park3 scene.
- **Device commits**: launcher `300848f` (part 1), `1ab8dc9` (part 2: pickups tests, version 1.1.9 build 14, readme, notes `docs/releases/1.1.9.md` via Idiolect); engine `814e58d9` (part 1), `4def8b9a` (part 2: per-player pickups). `VERSION` stays 1.1.
- **Review**: an independent review of 1fb4fb7..HEAD found no confirmed crash, softlock or regression (offsets rechecked against `player.h`; pickups, movement, cross-branch interactions, checkpoints, launcher). A guest without `models/coop/h_gasmask.mdl` (a 1.1.8 guest, or a modded `w_nightvision.mdl`) downloads it from the host (downloads are on by default; not fatal if refused: the follower is just not drawn).
- **Published 2026-10-21 ~09:26 PT** with `release.py build` (`--source "D:\Offload\CoF-evidence\tester-package-20261015\smoke-source\Cry of Fear"`, `COF_GASMASK_FILES` = `evidence/polish-20261021/models-for-fit`; full suite 691 OK, fresh install and the 1.1.8 -> 1.1.9 update check passed) then `release.py publish`: GitHub `v1.1.9` latest, `release.json` build 14. Zip `CC2EECBC…`, 111,285,446 bytes, MD5 `3F189223…`. Evidence `evidence/release-1.1.9/`.
- **ModDB not added yet**: saving the file form (from the GitHub URL, preview as 1.1.7) was blocked twice by moddb.com's Cloudflare firewall ("submitting a certain word or phrase…", Ray IDs `a4e1a1eda9b8d254`, `a4e1a45a3ac2d254`); nothing was saved. The text was `evidence/release-1.1.9/moddb-text.txt` (with its two semicolons made periods on the second try). Left to the owner.

## 1.2.0: the bonus room at every chapter start, the wardrobe key, the app's look (2026-10-21)

The user: "How do we put the bonus room in each chapter so even if you start at a later chapter, you can still access it?" On the wardrobe: "the wardrobe is just a door." Then: "Yeah, do it. And then make the launcher also look better. Like, I want it to not look like so default."

- **Bonus kit** (engine, branch fxbonus merged): a chapter start (`cof_chapter_start`) whose chapter can't reach a room (every one but c_start, c_nightmare and c_bridge) lays out the room's items (`sv_cof_bonus_kit.h`; keys left out) beside the party's spawn, once, on its first story frame, only while the room is open (`cof_bonus_room` 1, or -1 and the file opens the door); a line the file reads as locked is skipped. Placement: floor, straight-line reachable, clear of walls/props/water and exit/hurt/teleport/push/bounce triggers; merged maps: the chapter's own section (`SV_CoF_SectionOfMap`). Per player through the 1.1.9 pickups ("bonus" spawnflag, own ledger entries; touch-taking blocked for who took theirs, `sv_world.c`); with `cof_coop_pickups` 0, one set per place (max 4). The MP5 still vanishes unless the donator level is 2-4 (as in the room). Logs `phase=bonus-kit`, `bonus-kit-item`.
- **Wardrobe key** `cof_wardrobe` (O; K is push-to-talk): sends the presser the wardrobe's 7-byte `Clothes` message (outfits per the file and `cof_costumes`, their current outfit) while the room is open; refused when dead, in a camera/cutscene/scene freeze, on a ladder or in the boat; once a second. The menu's Close is not passed to the game (its `closeclothes` ends any running wardrobe camera) unless the presser is in that camera. Logs `wardrobe*`. O is in kb_def/kb_act; the app adds `bind "o" "cof_wardrobe"` to the session cfg when the copy's config.cfg leaves O unbound (`friend_setup.missing_coop_binds`).
- **App look** (`tools/app_theme.py`): dark clam theme, a header band drawn on a canvas, cards for Play solo/Host/Join, key caps, red main buttons, smooth check/radio images, dark title bar (DWM); Start/Back pinned to the bottom of the start screen. Screenshots: `evidence/ui-20261021/shots2/` (preview script there; dev only).
- **Not played.** Live checks: start Chapter 2 with a friend (items ahead of the spawn, one each, night vision on F); Chapter 5, 7, Ending 4/5 with merged maps (right section); team wipe then Continue (no second set); bonus room unticked (nothing, O says locked); O + outfit + Close (partner sees it; in c_start, one player in the real wardrobe while the other uses O then Close: the first one's camera stays); O refused in a cutscene, on a ladder, in the boat.
- **Released as 1.2.0** (the user: "make it 1.2.0", build 15; `VERSION` stays 1.1). Build `evidence/release-1.2.0/` (full suite 703 OK; fresh install and the 1.1.9 -> 1.2.0 update passed; `engine/client/console.c` includes `cof_version.h` through `__has_include`, which waf does not track: its object was moved aside to rebuild the stamp). **Published 2026-10-21 ~12:15 PT** with `release.py publish` (the user: "publish"): GitHub `v1.2.0` latest, `release.json` build 14 -> 15. Zip `C4A453FD…`, 111,323,444 bytes, MD5 `351E8C9A…`. The user's copy (`%LOCALAPPDATA%\CoF-Story-Coop`) is on 1.2.0. A 1.1.10 build was started and stopped for the rename (`evidence/release-1.1.10/`, not published).
- **ModDB still blocked**: the file form's Save is refused by Cloudflare for this connection whatever the text (six tries on 1.1.9 at ~09:30, including the 1.1.8 text with only the version changed, a re-encoded preview image; one try on 1.2.0 at ~12:17, Ray ID `a4e299bb3b87f7cd`). The zip's URL upload itself works.
- **ModDB added 2026-10-21 ~17:00 PT** at the user's "Try and post on ModDB again": the same form saved first time (the block had lifted). File `cry-of-fear-story-co-op-120` from the GitHub URL, preview as 1.1.7, ModDB MD5 `351e8c9a…` and 111,323,444 bytes match. ModDB never got 1.1.9 (1.2.0 replaces it there).

## 1.2.1: the antivirus false positive (2026-10-21 evening)

The user, after 1.2.0 went up: "People are saying they're getting a Trojan warning." (No vendor or threat name given yet.) Defender on this PC (definitions 1.461.139.0) scans the 1.2.0 exe and zip clean; the usual cause is ML heuristics (`...!ml`) on a PyInstaller one-file exe.
- **Build** (`tools/build_tester_package.py`): the app is now a PyInstaller one-folder build (`--onedir`, `--contents-directory coop-app`, `--noupx`), with its own icon (`tools/app.ico`, original art drawn by `tools/make_app_icon.py`; also the window icon) and Windows version details (`version_info()`: company/product "Cry of Fear Story Co-op", no other names). The zip holds `CoF Story Co-op.exe` + `coop-app/` (payload, Tk, Python, `tools/map_groups.json`, `tools/app.ico`). The bootloader is rebuilt from PyInstaller 6.19.0's source on this PC (`evidence/pyi-bootloader/`), so the exe is not the stock bootloader every PyInstaller build shares.
- **App/setup**: `friend_setup.install_app` copies `coop-app` (via `coop-app.new`, old one moved to `.old` and removed) then the exe, into the copy; `app_update.one_file()`: only a one-file app waits for its parent (a one-folder app's parent is Explorer). Old apps' Update now extracts the whole zip (`extract_app`), so they start the new one-folder app fine. `release.py` checks the zip's top level plus `coop-app/` and that the copy has `coop-app/payload`.
- Still unsigned: code signing (or Azure Trusted Signing) is the long-term fix; a false-positive submission to Microsoft needs the user's account.
