# Cry of Fear co-op: persistent shared agent notes

Last updated: 2026-10-13 late morning: `c_start` ↔ `c_start2` played live; the repositories were cleaned up and published as private GitHub repos for playtesters (see "Repository cleanup and sharing").
This is the current handoff. `AGENTS.md` requires project and delegated agents to read it.
Older dated handoffs remain historical evidence, not authoritative current status.

## Current status: story moments, polish and an AI companion implemented offline; campaign live test pending

- Repositories (2026-10-13 cleanup, section below): launcher `main` is one clean commit, the full local history stays on the local-only branch `split-play-stage3` (never push it: it has the remote friend's name and personal paths). Engine branch `story-coop`, everything committed. Private GitHub repos: `charliee1w/cof-story-coop`, `-engine`, `-freevgui`, `-mainui`. Do not reset, clean, merge, or discard existing changes.
- The user asked (2026-10-12) for as much work as possible **without launching game sessions**, so that the next test is as close to the finished game as possible; then for a way to test both clients alone, and for an AI companion ("use items on its own … a real cool companion for people who don't want to play it solo … less scares"). No game was launched in any of these sessions.
- Implemented and offline-tested (not live-tested): pieces 2 and 4 of the split-play spec, from static research of the retail DLL and all 81 story maps. Index: `docs/research/2026-10-12-story-moments-re.md`; plan: `docs/superpowers/plans/2026-10-12-story-moments-and-polish.md`; the user-facing checklist for the next test: `docs/campaign-test-guide.md`. Also the AI companion (`docs/companion.md`, section below).
- Engine build `3D860BC7…8626A12D` (see table below) includes everything before plus the AI companion, the test gun and the fixes from the first live session (cutscene skip, intro end, team-wipe crash). It is **installed in both disposable copies** under `evidence/story-live-20261012/` (host, guest; previous engines kept beside each as `xash.dll.<hash8>.bak`), with Enter bound to `cof_skip` in their `cof_campaign.cfg`. It is **not** installed in `runtimes/enhanced` or `runtimes/enhanced-local-client`; those still hold `B1556A82…` (rechecked 20:00) and match `state/enhanced-remote-session.json`.
- Normal runtime/save/config files were not modified. At 15:34 local on October 12, all 92 files in `evidence/story-live-20261012/protected-before.json` still matched their pre-test hashes (dated verification).
- Processes: the 20:02 solo session (started at the user's "ok run em") ended with the host crashing at 20:07:27 (below). Afterwards the user said they were going to play another game: "don't relaunch, but keep work". Builds and tests since then ran at BelowNormal priority (`lowtest.ps1` in the session scratchpad). Nothing was relaunched.

## Latest (2026-10-13): second live session (intro fixes pass), main-game test started

Second live session, 2026-10-12 22:18–22:27, engine `3D860BC7…`, solo mode, `c_intro`, m16; started outside an agent session. Logs `evidence/story-live-20261012/{host,guest}/campaign-logs/20261012-2227*-campaign-*.log` (archived by the next Start). `story_run_report.py`: 1 transition completed, no refusals, errors or crashes.

- Live-verified: Enter skip (`cutscene-skip` x6, `cutscene-skip-over` x2); the intro end (`teleport-party-moved` to `kunitei`, then `exit-leader in_exit=1 strays=0`, both players restored in `c_nightmare` without J); no AI-drive flapping (`companion-driving`/`companion-released` 2 each); the test gun for both; `c_nightmare` camera gift shared (`gift-shared class=weapon_camera` to the host, `starter-shared` to the guest); guest `kill` → `respawn-sooner` → `inventory-restored reason=respawn` → `spawn-kept reason=partner-near`.
- User report (2026-10-13, not yet reproduced): "the camera couldn't be added back to the inventory". The log does not show which items came back after the respawn. Unclear whether this is the respawn restore or something else; ask the user. The camera only exists in `c_nightmare` (`kakkaej`), and `c_start` empties the inventory.
- AI drive (solo mode), live 2026-10-13 10:29–10:37 and in the 22:18 logs: the AI takes the guest only while it is held in a camera (`companion-driving` ~8 s into a cutscene), and lets go the second the camera ends or the guest is teleported (`companion-released` together with `teleport-party-moved`, `companion-teleport` or `camera=none`). Certain: server-set views (`CoF_PartyMoveEdict` fixangle, camera end) are not in the echo ring (`sv_cof_companion.c` `CoF_CompanionEcho`); a fix would record every svc_setangle the guest is sent. Unknown: what keeps resetting `human_until` while the host plays (never during cameras). Focus-loss mouse deactivation (`host_sdl2.c` `SDLash_ActiveEvent`) is skipped when the guest loses focus before `key_game`, and `IN_ToggleClientMouse` activates without a focus check, but a live client.dll mouse in both windows would fight over the cursor, which the user did not report. No controller in the guest log. Next: ask whether the guest view turns with the host's mouse, else log the touch reason. The user said (10:38) "The AI is literally secondary"; not being worked on.
- The user asked to skip the intro and test the main game. 2026-10-13 10:29–11:08 local: `tools\Start-CampaignTest.ps1 -Map c_start` (solo, m16) launched by the agent at the user's request; the user closed both games at 11:08 (nothing running afterwards). `story_run_report.py`: `c_start` → `c_start2` → `c_start` (`hahaland`), 2 transitions, 2 players, no refusals, errors or crashes; `door-party-moved`, `warp`, `note-shared`, `exit-hold`/`exit-ready`, autosaves on both maps, the cutscene skip in `c_start`'s opening. No story progress after 10:37 (the user stepped away). Log: `evidence/story-live-20261012/host/campaign-host.log` (archived by the next Start).

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
| `upstream/…/build-local-20260927/engine/xash.dll` and staged `evidence/finish-20261012/xash.dll` | `3D860BC7354464B200EA5A84FD187E2293D5C627D1AC79CFBAD665D38626A12D` | Latest: everything below plus the first live session's fixes (cutscene skip, intro end, team-wipe crash); 153 offline tests; not live-tested |
| `evidence/story-live-20261012/host/xash.dll` and `guest/xash.dll` | `3D860BC7354464B200EA5A84FD187E2293D5C627D1AC79CFBAD665D38626A12D` | Installed 2026-10-12 late night by `Prepare-CampaignTest.ps1` (not launched) |
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
- Start with `tools\Start-CampaignTest.ps1` (default `-Mode solo`: both windows side by side, the AI drives the guest while it rests; `-Mode companion`: host only plus an AI bot; `-Mode two`: two people; default map `c_intro`, `-Map c_start` skips the intro); logs `host/campaign-host.log`, `guest/campaign-guest.log`, earlier ones archived under `campaign-logs/`. Re-run `tools\Prepare-CampaignTest.ps1 -ExpectedSha256 <hash>` after a new build.
- `prepare.ps1` created the copies and refuses existing destinations. Do not rerun it expecting an incremental setup.
- `control.py` targets only this test's loopback port and reads `private-session.json`. Never copy its credentials into notes or source control.
- Read-only status, while that isolated server is actually running: `python evidence/story-live-20261012/control.py status cof_story_status`.
- Old test checkpoints under `host/SAVE/cofcoop/` (`baseline`, `geared`, `distinct`, `truncated`) are checkpoint format 1 and are refused by the current engine ("older build").
- Older logs: `host/isolated-host.log`, `guest/isolated-guest.log`, `host/diagnostic-host.log`, `guest/diagnostic-guest.log`.

For native inspection, use the installed computer-use skill and `node_repl` / `@oai/sky`. Browser-only `cua_repl` has no native surface. A minimized game capture can show the desktop or another app; that is not proof of a black game. Never work around a user stop through another automation interface.

## Next steps, in order

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
4. Playtesters (2026-10-13 goal): with the user's go-ahead, deploy a build to `runtimes/enhanced` (+ the session state's hash), rebuild the setup exe (`tools/build_friend_setup.py`) and the package (`tools/package_enhanced_story.py`, now `CoF-Story-Tester-v<N>.zip`), then the user invites testers to the private repos; testers follow `docs/playtesting.md` and report with the issue template.
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
