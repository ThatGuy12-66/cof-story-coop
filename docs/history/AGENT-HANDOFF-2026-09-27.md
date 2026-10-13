# Cry of Fear story co-op — current agent handoff

> Current shared handoff: read `AGENT_NOTES.md` first. `AGENTS.md` requires it for every project agent.
> The October 12 isolated live test found an inventory-recovery bug; its source fix and build pass focused tests, but the live retest was interrupted. The updates below predate that result.

## UPDATE 2026-10-12 — exits, shared state and recovery implemented; live gate pending

Work remains uncommitted on launcher `split-play-stage3` and engine `story-coop`.
The October 4 research documents were pre-existing untracked work and are
preserved. No runtime deployment, game launch or live verification occurred.

- Scripted exits are classified from incoming entity targets and remain held
  after waiter death/disconnect or competing walk-in requests.
- Shared state covers keypad/digits, durable document/code-note payloads,
  objectives, ending flags and three-slot bag loss.
- Selective inventory snapshots restore respawns/rejoins through the DLL's
  serialization API and remap owned weapon references. Shared consumed items
  and deliberately dropped items are reconciled.
- Separate `.cof` session archives, host save/load commands, tape slots,
  autosaves, team-wipe recovery, restore tickets and failure-safe file staging
  are implemented. The startup map does not overwrite an existing checkpoint.
- Retail field-table inspection corrected an older research assumption:
  `m_iPadlockNumber[5]` is saved. The retail objective-file field is incorrectly
  typed as TIME; the recovery schema and session fixup use a string.

Plan: `docs/superpowers/plans/2026-10-12-story-state-recovery.md`.
Controls and required live checks: `docs/story-checkpoints.md`.
Validation logs and staged build: `evidence/story-state-recovery-20261012/`.
Consult `progress.md` there for the final counts and artifact hash; old live
results below do not validate these new paths. Do not replace either playing
copy merely because offline tests pass.

## UPDATE 2026-10-04 ~17:55 — stage 3 live results (engine B1556A82…)

The engine is `B1556A82FB1572725EED9C974FEA37BA62ABEFE28887FE5B3785C9781BBB9F2E` (engine branch `story-coop` at `bc3e91c`), deployed to both copies with the state updated. The build and the previous engines are in `evidence/stage3-live/`. The last engine proven live before this one is tagged `story-coop-live-20260930` (`19ca4fc`). Project work is on branch `split-play-stage3` (not merged). Plan: `docs/superpowers/plans/2026-10-02-coop-split-play-stage3.md`. 127 tests pass.

**Verified live with the two little rascals on this PC** (events in `evidence/stage3-live/03-session-events.log`):
- **Padlock:** the guest opened and cancelled the `c_start` padlock and could move again; the host was unaffected (user report; no log line exists for it).
- **Exit hold:** the guest stood in the exit to `c_start2`; the level waited (`exit-hold waiter=2`).
- **Warp releases the hold:** the host pressed J, landed beside the guest, and both changed level (`warp`, then `exit-ready waited=11.8`).
- **Door together:** in `c_start2` the guest followed the host's door (`door-party-moved`).
- **Escape hatch:** the host waited 16.8 s, pressed J, and both went to `c_start2`; the guest was placed beside the host (`exit-forced`, `arrival-placed`).
- The user reported the rest of the check list worked too: warp from far away and the double-J cooldown, a door taken apart (no pull), stepping out of the exit.
- All four transitions completed (generations 2–5); no refusals or engine errors.

**Ruling by the user:** the escape hatch pulling the partner along after 10 s is fine as is.

**Not yet checked live:** the stage 4 facts (engine `AddDocument` for the partner, engine code `AddNote`; probes `cof_story_probe doc|codenote` in `c_apartment1`), the 1.5 s fade, a scripted exit, the scripted-teleport cut-off prompt.

**Open from the final review (not fixed):** the two `trigger_once` exits (`c_apartment3 → c_apartmentsick` "landfitta", `c_apartmentsick → c_basement` "landkuk") have touch-enabled exit brushes sealed in unreachable pockets. If the waiter dies or disconnects during a hold there, the hold cancels and the exit is lost for that map. Fix: decide "fired by name" from the map data (`SV_CoF_MapExitModels` pass 2 already computes it) and, for such exits, never cancel, turn away or replace the hold.

## UPDATE 2026-09-30 ~16:20 — stage 1–2 finished after the final review (engine C59FC6B7…)

The engine is `C59FC6B7A262098C4543646109C68607CBD02833BE45CA239E9FE647A2999A93`, deployed to both copies with the state updated (build in `evidence/stage2-review-fixes/`). 117 tests pass.

**The final whole-branch review (opus) said "with fixes", and all of these are fixed with tests that failed first:**
- **Weapon-upgrade combines keep the partner's copy.** Taclight + Glock and battery + phone produce no new story item, so they log `item-combined` instead of `item-used`.
- **Ledger timers are cleared when a new map restarts `sv.time`.** Before, a refused give just before a level change stalled deliveries.
- **Per-item backoff:** an item that never lands backs off on its own and is logged once as `item-stalled`, and later items still arrive.
- **One safe-name rule:** `CoF_DescriptorSafe` in `sv_cof_descriptor.h` is shared by the ledger and the retail layer.
- **Review Focus 2, 3 and 5 are now pinned** by tests that fail under the reviewer's mutations.

**Deferred minors:**
- A CPR revive re-delivers an item the player dropped on purpose.
- A dropped copy can be destroyed by a partner walking over it. The dropper gets it back on respawn, or the partner can drop theirs.
- The retail descriptor cache is never cleared.
- The event-overflow comment is inaccurate.
- The `0x21FC` offset is still duplicated in `sv_cof_party.c`.

**Rulings made during stages 1–2, each with its cost if wrong:**
- **Project work is on branch `split-play-stage1-2`, not main** (merge pending).
- **No worktree:** the build and deploy paths are tied to the checkout.
- **Solo death resets the ledger.** It's a retail team-wipe map restart and belongs to piece 3. Solo testers lose carried items on death until then.
- **ScreenFade unseen and ProFont colour ignored:** carried to stage 3.
- **A used-up key can re-enter the party** if someone picks up a dropped world copy. It is a real item.
- **Stage 2 applies the bag loss's item wipe to the partner but not three-slot mode.** Stage 4 fixes that.
- **The cassette tape is shared while each player's tape count is separate.** Tape saves are single-player only.
- **A malicious client is out of scope.** The trust model is two friends.
- **The ledger runs every frame**, not every 0.1 s.
- **Deliveries can land during cutscenes or phone calls.** Piece 2 decides the rules for scenes.

**Next:** the plan for stages 3–4. Its first task is the padlock / first-player command fix (see the ~15:50 section below). Then the stage 3–4 live facts (engine `AddDocument`, code `AddNote`), then the exit hold, warp and door radius, then shared codes, notes and objectives.

## UPDATE 2026-09-30 ~15:40 — split play stage 2 live results (engine CB93C2D9…)

The engine is `CB93C2D9DEC89CD5F6B7A2B13F074CBEEC91C17970924694CDE942C4D276ABDA`, deployed to both copies with the state updated. The build and the previous engines are in `evidence/stage2-share/`. The host (PID 43312) and the local guest (PID 58632) were left running.

**Verified live with the two little rascals on this PC (host userid 2, guest userid 3):**
- **The guest joined straight from the command-line `+connect`,** with no console step and no menu bounce. The LZSS receive fix is doing its job.
- **Late-join placement:** `spawn-placed userid=3 partner=2`, the guest beside the host. Phone and knife were shared (`starter-shared`).
- **A found item reaches the partner:** a probe give to the host logged `item-found userid=2`, then `item-delivered userid=3`. The key was in the guest's slot 3 (`04-guest-after-find.log`).
- **Used up, gone from both:** a probe removal on the host logged `item-used userid=2`, then `item-removed userid=3`. The guest's slot emptied and the ledger was empty (`05-guest-after-use.log`).
- **A drop stays personal:** the guest dropped the key from their inventory (`item-dropped userid=3`). The ledger kept it and marked it `ledger-set-down userid=3`, and the guest was not re-given it. The host kept theirs.
- **The key survives death with a living partner:** the guest `kill`ed, respawned after 60 s, and got the key back (`item-delivered userid=3` at respawn, `06-guest-after-respawn.log`). This happened twice, including after the drop, because a respawn forgets set-down items (`07-guest-after-drop-respawn.log`). The user confirmed it "worked perfectly", and the co-op "press to respawn" prompt appeared.
- **Run report:** `08-run-report.txt` counts found 3, delivered 4, used 1, removed 1, dropped 1, with no problems.

**Finding for piece 3 (death), top priority:** a solo death is a team wipe. The retail co-op rules restart the map (`CHANGE_LEVEL(c_start, NULL)`), which is a non-story changelevel, so hub history and the item ledger reset and the intro replays. The solo key was lost that way (userid 1→2). This predates stage 2.

**User feedback for piece 3:**
- "60 is a little too long" for the respawn.
- The respawn prompt did not appear the first time, but did the second.

### Same session, ~15:50: two-player doors and a padlock soft-lock

**Two-player story transitions work on engine CB93C2D9: 5 of 5**, the first time this has been proven.
- **Host-led:** generations 3–5, `c_start` ↔ `c_start2`.
- **Guest-led:** generations 7–8. In both, `exit-leader` was the guest with `in_exit=1`.
- **Every transition** logged capture, then restore, then sign-on for both players, then ready. No invisibility, and door visibility was restored.

**The earlier "guest-led door black screen" was not a bug.** Going `c_start2` → `c_start`, the door drops the user into the pitch-black stub room (lolexit2), and the exit trigger is a few steps away.
- The half-second trace `10-guest-door-trace.log` shows the guest arriving at (2424,-940) with `frozen=0`, standing still for 5 s, then walking into the trigger, which fired at 15:49:27.
- The first time, the user waited in the dark, and the host's step set off the trigger.

**Rejoin:** the guest window was closed and relaunched. It rejoined as userid 4, placed beside the host, with the Hall key delivered by the ledger and the phone and knife shared.

**Guest audio "missing":** the user had the guest's volume at zero. Not a bug.

**Bug, next plan: guest soft-locked in the padlock screen (`c_start` item_padlock).**
- **Opening works:** `item_padlock` Use (`100B6520`) acts on the activator. It sets the activator's `m_pPadlock` (`+0x2350`), freezes them, sets their view to the padlock camera, and sends them `Padlock(1)`.
- **Cancelling acts on the wrong player:** client command `padlockcancel` (ClientCommand `1001C968`) takes the sender's `m_pPadlock` and calls `100B5EA0`. That function does `UTIL_FindEntityByClassname(NULL,"player")` (the FIRST player) and restores that player: clears `m_pPadlock`, resets the view, `EnableControl(1)`, clears HUD bit 8 and FL_NOTARGET, and sends `Padlock(0)`. So a guest's cancel restores the host, and the guest stays frozen in the padlock screen. The unlock path clears the first player's handle too.
- **Scale:** hl.dll has 87 such first-player lookups (script `scratchpad/re-pad/firstplayer.py`).
- **Planned root fix:** while a player's client command is dispatched, "find the first player" returns that sender, and an all-player iteration visits the sender first, then everyone else.

## UPDATE 2026-09-30 ~15:25 — split play stage 1 live results (engine 046F6631…)

The plan is `docs/superpowers/plans/2026-09-30-coop-split-play-stage1-2.md`, working on project branch `split-play-stage1-2` and engine branch `story-coop`. The engine is `046F6631920C2C27FC3EEC5A60BFF440C725D456032B50BB80494B1D7131D9AF`, deployed to both copies with the state updated. Its build, PDB and the previous engines are in `evidence/stage1-retail/`. Host PID 30496 was left running for the user.

Live results for the retail layer (`cof_story_probe` over RCON, host solo on `c_start`):

1. **Give via the game's pickup: YES.**
   - The server shows `inventoryitems/hallkey.txt` in slot 3 (`02-give.log`, `03-slots-after-give.log`).
   - The user confirmed the key is in the inventory. The "You got the Hall key" text was not separately confirmed.
2. **Removal via engine-issued `inventorydeletenamed`: YES.** The slot emptied (`04-remove.log`, `05-slots-after-remove.log`), and the user did not report the icon remaining.
3. **ProFont: YES, with a caveat.** The text appears in the game's strip at the top left (`06-msg.log`, `09-msg-again.log`), but colour index 2 did not render red. The stage 3 prompts must not rely on colour.
4. **ScreenFade: NOT CONFIRMED.** The server sent it four times (`07-fade.log`, plus three more), but the user did not report seeing a blink. The stage 3 warp must work without a fade, or the fade must be diagnosed first.

Extra checks:
- **Drop and pickup of an engine-given item:** a user drop and re-pickup kept the key in slot 3 (`10-slots-after-drop-pickup.log`).
- **Story door round trips on this engine:** 3 of 3 completed, with 0 errors and the Hall key carried (`11-slots-after-doors.log`, `12-run-report.txt`).

**Gate: passed** (give and removal work). Stage 2, the shared item ledger, proceeds.

## UPDATE 2026-09-29 ~22:30 — whole-campaign audit, arrival regroup, run report (engine C8219BB0…)

The user asked to "build out the entire game like the demo" and test solo. Answer given: the demo door fix is engine-wide, so nothing is per-level; solo proves loading/landmarks/items/hub state but not the co-op half.

- **Static audit (all 235 BSPs):** 81 story maps reachable from `c_intro`, 197 `trigger_changelevel` exits (137 walk-in, 60 fired by scripts or triggers). 195 satisfy the engine's rule (`SV_CoF_TransitionValidate`: a unique `info_landmark` in both maps). The 2 failures (`c_apartment2 changelevel4 → c_apartment1 land4`, `c_start changelevel2b → c_apartment1 land2b`) have no landmark in either map and nothing targets them. No level change happens outside `trigger_changelevel`; `cof_ending`/`cof_credits` are ending logic.
- **Bug fixed, arrival regroup:** players are restored at their landmark offset with no in-solid check. A partner far from the exit landed in walls or the void. `sv_cof_party.c` now has `SV_CoF_PartyTransitionCapture` (called at the end of `CoF_BeginStoryTransition`; the leader is whoever touches a `trigger_changelevel`, or a trigger targeting one, else whoever is nearest the source landmark; others more than 128u away are strays) and `SV_CoF_PartyTransitionArrive` (end of `LoadAdjacentEnts`, before sign-on). Arrive places strays, and near partners in solid, beside the leader. Fallbacks: keep a standable spot; else overlap the leader and set `cof_party_regroup` so `CoF_PartyPlace` separates them after sign-on. Log lines: `cofstory phase=exit-leader|arrival-placed|arrival-kept|arrival-overlapped|arrival-stranded`. 11 new harness scenarios. Full suite: 111 tests pass (`evidence/engine-arrival-regroup/tests.log`, before the report tests were added: 109).
- **Previous session's last fix (was missing here):** engine `BB74A432…` added LZSS decompression of client packets on the listen server. The guest was kicked with `clc_bad (76)`, the `L` of an `LZSS` header, at "Verifying and downloading resources". The two-client door round trip on that build was **never run**; the 20:05 host log shows only solo movement on c_start.
- **Review fixes (engine `1322319333D528686FE8670ACDC3C66DBB0DDE28879CF3E4633E817196F0102E`):** an independent review found no crash, deadlock or state corruption, but two behavior problems, now fixed.
  1. Strays left in a merely standable spot were never regrouped; they now get `cof_party_regroup` too.
  2. The leader test counted any exit's bounding box. A new `SV_CoF_MapExitModels` (sv_game.c) reads the source map's entity script for the brush models of this transition's `trigger_changelevel`, and only those count (or triggers targeting them). Offline, all 195 exits have exactly one matching `*N` model.
  A re-review came back clean. Two minor follow-ups are in the final engine, `C8219BB0FAB36B304B7389329A4FB622EF4C9BDFDDDDAD187F9CF14EAD35C438`, which is deployed to both copies with the state updated (`1322…` is in `second-build-1322/`):
  - A regroup (`place_regroup`) is no longer cancelled by the 64u wander check; that check still applies to fresh joins and respawns.
  - A partner is never overlapped onto an anchor that is itself in solid; it is logged `arrival-stranded` and left to regroup.
  The anchor moves off the leader only if the leader lands in solid, never merely because there's no floor (exits taken while falling). The first build, `4029F4F7…`, is kept in `evidence/engine-arrival-regroup/first-build-4029/`.
- **First deploy:** `4029F4F7361ABDAF390F3ABDFB5EEB4C223C392CCDA0193A9D69B3A506FF5BF5` to both `runtimes/enhanced` and `runtimes/enhanced-local-client`; the session state was updated (old copy in `state/enhanced-remote-session.json.v6-lzss.bak`). The build and PDB are in `evidence/engine-arrival-regroup/`, the previous engines in `host-before/` and `local-client-before/`, the pre-edit sources in `source-before/`.
- **New tool:** `tools/story_run_report.py` reads the host logs (current plus archived `runtimes/enhanced/story-logs/*.log`; `Start-StoryHost.ps1` now archives the previous log instead of overwriting it). It lists each transition as ok or BAD, refusals, Host_Error and crashes, and party warnings, then shows coverage: story exits proven out of 195 and maps reached out of 81. `--untested` lists the exits not yet proven. Exit code 1 if there are problems.
- **Campaign-scale limitation (not fixed):** `IsValidSave` refuses when `svs.maxclients != 1` ("Can't save multiplayer games"), and the hub history `cof_story_maps` exists only in memory. A session cannot be resumed with items or world state; restarting from a chapter map means empty inventory. Candidate future work: story co-op save/load.
- **Still unverified live:** any two-player transition on the current engine; arrival regroup in a real map; death/CPR/respawn item loss (key items only go to the activator); endings.

## UPDATE 2026-09-28 ~19:50 — party/solid/visibility/join fixes (engine 7376EB1A…)

Root causes found live, all fixed in engine `7376EB1A5188175D811DF43701F3332C8CAB6E000EBC0D45419471D65A12F31E` (includes independent-review fixes: teleport_to lookup, doors supersede waiting events, password not broadcast, door-hide carried across map change, cheap SetOrigin guard) (deployed to BOTH `runtimes/enhanced` and `runtimes/enhanced-local-client`; staged with PDB in `evidence/engine-party-coop`; previous `ED2B…` in `evidence/engine-rules-zero-host-backup`; session state updated, old in `state/enhanced-remote-session.json.v5-rules-zero.bak`). 32 tests pass.

1. **Return door never changed level:** both players were SOLID_NOT, so they couldn't touch trigger_multiple `*3` → `haehecl`. Retail `PreThink` (hl VA `100EF2D0`) sets SOLID_NOT when a player's absbox intersects another `player` (Intersects at `10017D80`), else SOLID_SLIDEBOX. The engine pads absboxes by 1, so our 34-unit door offset made the boxes touch. `SV_RunCmd` only relinks solid players, so a SOLID_NOT player's box froze → permanent deadlock. Fixes: `sv_pmove.c` relinks SOLID_NOT players (touch=false) in story co-op; party offsets now try 35 → 40 → diag → 72 → 34 last.
2. **Guest invisible (EF_NODRAW):** retail MP `PostThink` (`100EBD75`) ORs NODRAW while `b_GoingThroughADoor` and never clears it. The old code cleared it only on a "confirmed" door with a visible baseline; a door used from its own exit was "cancelled" → hidden forever. Now `CoF_PartyDoorVisibility` clears the door-caused hide once the flag drops and the player is playable (no camera/freeze). Door completion no longer needs the start≠exit check.
3. **Late joiner spawned inside the host** (c_start2 `info_player_start` = the arrival spot). Fresh joins (`SV_PutClientInServer` → `fresh`) and respawns (dead→alive) are placed beside an available partner (≤30 s wait, cancelled if the joiner moves >64u, kept if the partner is ≤128u away and not overlapping). NOTE: `SV_PutClientInServer` runs before `cs_spawned`, so party identity is adopted later; the fresh mark is carried by slot+userid.
4. **cof_teleport (369 in 146 maps) moves only the activator:** `pfnSetOrigin` now notes >96u jumps of live players; post-frame, if the new spot is an `info_teleport_destination` targeted by a `cof_teleport`, partners >128u away follow (≤10 s wait). trigger_teleport/ladders/doors excluded.
5. **Join "invalid password":** `password` is now an important info key (evicts cosmetic keys), and `CL_SendConnectPacket` re-applies it. This fixes the remote friend's JOIN too, once they have a package with this engine (v5 does NOT; build v6).

Survey: 0 `trigger_transition` in all 235 maps, so the retail index-1 `InTransitionVolume` check is moot. Not handled (deliberately): story-item/weapon sharing (`cof_giveitems` → activator only; auto-giving could fill the 6 slots and block key pickups), per-player `trigger_camera`.

## UPDATE 2026-09-28 ~19:15 — local two-client test (supersedes "one client per PC")

The remote friend is out. The user asked to test **both copies on this PC**. `runtimes/enhanced-local-client/xash.dll` was updated to the host engine `ED2B7EB0…` (old one backed up in `evidence/engine-local-client-pre-rules-zero`). Guest launcher: `tools/Start-LocalGuest.ps1` (reads the host sv_password without printing it, writes `cryoffear/cof_local_join.cfg`). **The user joins through the in-game console:** `exec cof_local_join.cfg; connect 127.0.0.1:27015`.

**Join bug, also affects the remote friend's JOIN (v5 package):** the guest's userinfo is 248/256 chars (retail client.dll registers `skybrightcolourr/g/b`, `brightvalue`, `skybrightvalue`, `hud_classautokill` as userinfo). `\password\<16>` doesn't fit, and `Info_SetValueForStarKey` silently drops non-important keys → host says "invalid password". Re-setting the same value is a no-op (`common/cvar.c:604`), so the fix in the cfg is: clear the three sky keys, then `password ""`, then `password "<pw>"`. friend_setup.py `join_config` still needs this fix before the remote friend can join.

Live result 19:12: host went c_start → c_start2 alone (guest connected at 19:12:42, after the door). The guest spawned at c_start2 `info_player_start` (2428,-883), on top of the host at lolexit1 arrival (2423,-876) → stuck. The guest used door `*5` → lolexit2, and the host was party-moved behind it. Both ready, not frozen/hidden, phone+knife each. Captures show both windows rendering, and the guest sees the host's Simon. **Open issue: a late joiner spawns inside the existing player.**

## UPDATE 2026-09-27 ~15:35 — supersedes the build/package/state facts below

**Solo door transitions now work live on the host.** The user confirmed c_start → c_start2 → c_start: arrival, items kept (phone + switchblade descriptors), no intro replay on return (`c_start` restored from `save/c_start.HL1`, not respawned). Two-player transitions with the remote friend are still **unverified**.

Two root causes found and fixed (both host/server-side only):

1. **Black screen after every door (also the real cause of the old v3 two-client stall).** `SV_DeactivateServer` frees every client's `frames`; only `SV_ConnectClient` reallocates them. Story transitions keep the connection (`cof_story_changelevel`), so `SV_ReadPackets` skipped every packet (`cl->frames == NULL`) — the client's `new` was netchan-acked but never executed. Fix: `sv_save.c` `SV_ChangeLevel`, story branch reallocates zeroed frames for kept non-fake connections right after `SV_DeactivateServer`. Test: `tests/test_enhanced_server_transition.py` (runs the actual `SV_ChangeLevel`/`SV_DeactivateServer`/`SV_ReadPackets`). The old v3 guest Movevars crash at 14:26 was a *later* symptom of this stall. Log markers of success: `BUILD 4185 SERVER` after `Spooling demo header.`, then `phase=signon` and `phase=ready`.
2. **Crash ~5 s after arrival in retail hl.dll vote-kick code.** Retail `InstallGameRules` does `operator new(0xE4)` (call at RVA `0xAF79E`, operator new RVA `0x13E1E9`) with no zeroing; vote-kick (`+4` target-ID ptr, `+8` active, `+0xC` deadline) and lobby fields (`+0xA0..+0xD8`) are uninitialized. Recycled heap after a map change → phantom active vote → `100B0210` derefs junk pointer. Fix: `sv_cof_story.c` in-memory patch redirects that one call to a wrapper that calls the DLL's own operator new then zeroes; verified bytes `e8 46 ea 08 00 83`, no relocations in range, restored in `CoF_StoryCloseGate`. Startup log now reads `cofstory phase=gate-enabled landmark=preserved rules=zeroed`. Test: `tests/test_enhanced_rules_zero.py` (executes real x86 call-site bytes).

Current identities: host `runtimes/enhanced/xash.dll` = **`ED2B7EB0E23C8066425D1014080FCB2064734A978CCDF03D0B485E1B55BD2B87`** (PDB archived in `evidence/engine-rules-zero`); `state/enhanced-remote-session.json` updated to match (v4 value kept in `.v4.bak`); v4 engine backup in `evidence/engine-v4-host-backup`. 24 focused tests pass (`evidence/enhanced-rules-zero-tests.log`).

**Remote-friend package v5:** `Downloads\Cry-of-Fear-package\CoF-Story-Tester-v5.zip`, 109,422,447 bytes, SHA-256 `a5c5181890881e42818828a49ebe1a11845f42cbb0526a33aa36f626f1389e12` (canonical copy in `evidence/enhanced-story-package-v5/package`). Independent verification passed (`evidence/enhanced-story-package-v5/independent-bundle-verification.json`). Invite updated for v5 (state + Downloads copies identical). v4 moved to `Downloads\...\superseded\`. If the remote friend installed v4, they must delete `%LOCALAPPDATA%\CoF-Story` or choose another folder (setup never overwrites).

**Environment gotcha:** Claude Code's shell sets `NoDefaultCurrentDirectoryInExePath=1`, which makes every native test harness fail with 9009. Run tests with `Remove-Item Env:NoDefaultCurrentDirectoryInExePath` and the VS Installer dir on PATH.

Known unhandled edge: a client mid-connect (not `ca_active`) during a story door ignores `cof_story_changelevel`; the server side now keeps its frames, but the client-side handshake restart is untested.

---

Written 2026-09-27 at approximately 14:48 America/Los_Angeles. The sections below were the starting point before the update above. Older `HANDOFF.md` paragraphs and session notes retain historical process states and superseded approaches. Read this file first; verify live state before acting.

## User's objective and working agreement

Make the original Cry of Fear story campaign playable with the user and their remote friend. The user wants practical fixes and a usable game, not more proposals. They also requested that both players appear as Simon, with an appropriate variant for player two.

- Refer to the pair as **the two little rascals** in testing/development discussion.
- **One client on this PC and one on the remote friend's PC. Do not launch another local guest.** The user explicitly changed the test approach to two physical PCs.
- Announce before any game launch. Preserve their active session; do not silently restart, kill, warp, or replace its files. Never auto-kill a run on a timer.
- Keep performance reasonable: RTX 5070; current host is windowed 1280×720, capped at 60 FPS. The remote friend's joiner starts windowed 960×540, capped at 60 FPS.
- The user already authorized source download, implementation, ordinary builds, fixes and coordinated testing. Do not repeatedly ask for that authorization. Ask only for genuinely missing information or a necessary interruption of active play.
- Do not modify the original D: Steam installation. Work on C: copies. D: has a prior read-error concern.
- Do not send messages to the remote friend externally. The user relays files/instructions. This handoff contains no passwords; private paths below are for local use.
- Be candid about verified results versus pending gameplay. The full campaign is **not** confirmed working.

## Immediate state, freshly verified at 14:48

**One Cry of Fear process is running:** PID **50180**, started 14:36:31 local, executable:

`runtimes\enhanced\CoFLaunchApp.exe`

`cof_story_status` freshly reported:

- Original `c_start`, generation 1, story adapter enabled, unpaused.
- One real ready player, userid 1, health 100, no hidden/frozen/door-in-progress state.
- Actual inventory slots 1 and 2 contain phone and switchblade; remaining slots empty.
- Position `(636.744, -3016.308, -730.969)`; the user has moved during this session.
- Latest saved response: `evidence/enhanced-live/handoff-host-status.log`.

Earlier A2S also confirmed one of two slots, protocol 49, required game password, and server name `The two little rascals - story co-op`. UDP 27015 listens on this host. **The remote friend has not yet been observed joining the Enhanced build.** The prior successful Internet session was the retail engine and is a separate result.

Keep this host running. Do not launch `runtimes/enhanced-local-client`; that old test copy exists only as preserved development data. The minimal installer smoke copy is intentionally incomplete and must not be launched.

## What was delivered to the user

Delivery folder:

`Downloads\Cry-of-Fear-package`

1. `CoF-Story-Tester-v4.zip` — 109,413,702 bytes (~109 MB).
2. `private-invite.txt` — address/password and exact setup steps. Keep separate from the public package.

ZIP SHA-256:

`7babd5dfebe7d40e2e01b6869f9fb730d7bc807848161c31ad45b17cc7e7d440`

Canonical build artifact is the same file at:

`evidence\enhanced-story-package-v4\package\CoF-Story-Tester-v4.zip`

The remote friend's steps, already given to the user:

1. Close any existing Cry of Fear window; keep Steam open.
2. Extract the **entire** ZIP into a normal folder.
3. Run `CoF-Story-Setup.exe` and select the original Steam game if discovery misses it.
4. Click **Set up separate copy**. Default destination is `%LOCALAPPDATA%\CoF-Story`; allow about 6 GB free.
5. When setup finishes, enter the private invite's address/password and click **Join host**. Name defaults to the friend's name.
6. Future joins use `JOIN.cmd` in that separate copy.

The standalone setup needs no Python installation. It validates the retail game DLL, makes an independent copy, preserves campaign map CFGs and `skill.cfg`, runs the actual upstream Enhanced installer, generates both Simon models locally, and checks hashes. It excludes saves, user configs, old test overrides and logs. Joining refuses another retail or Enhanced game process on the remote friend's PC. It does not alter Steam's original installation.

If the default separate destination already exists, choose a new folder such as `CoF-Story-v4`; setup intentionally does not overwrite an existing game copy. Delivery and installer checks do not establish that the remote friend has downloaded or installed it yet.

The ZIP includes matching engine payload, corresponding modified source, setup EXE and notices. It contains no retail maps/models/game DLLs or private session credentials. Packages v1–v3 are superseded; **v3 contains the previously found join overflow**, so do not distribute it.

## Paths and commands

Project root (use explicitly; the chat's shell cwd may still be SteamVR):

`<project root>`

| Purpose | Path relative to project root |
|---|---|
| Current Enhanced host | `runtimes/enhanced` |
| Old second local client — do not launch | `runtimes/enhanced-local-client` |
| Earlier retail development copy | `baseline/Cry of Fear` |
| Enhanced source checkout | `upstream/cof-enhanced` |
| Prepared engine source | `upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a` |
| Current host startup | `HOST-STORY.cmd` → `tools/Start-StoryHost.ps1` |
| Host config, includes private credentials | `runtimes/enhanced/cryoffear/cof_remote_host.cfg` |
| Private host control state | `state/enhanced-remote-session.json` |
| Private invite original | `state/Remote-Enhanced-invite.txt` |
| Current host log — redact before displaying | `runtimes/enhanced/story-remote-host.log` |
| Package implementation | `tools/package_enhanced_story.py` |
| Friend setup / EXE builder | `tools/friend_setup.py`, `tools/build_friend_setup.py` |
| Simon model generator | `tools/prepare_simon_models.py` |
| Expanded implementation notes | `docs/enhanced-implementation-session.md` |
| Original observed two-player failures | `docs/internet-session-2026-09-27.md` |

Do not touch `D:\SteamLibrary\steamapps\common\Cry of Fear`.

Read-only current status:

```powershell
Set-Location -LiteralPath '<project root>'
Get-CimInstance Win32_Process -Filter "Name='CoFLaunchApp.exe' OR Name='cof.exe'" |
    Select-Object ProcessId, ExecutablePath, CreationDate
& 'python' tools\enhanced_local_control.py cof_story_status --session remote --out evidence\enhanced-live\next-status.log
```

The controller addresses only loopback. `--session remote` reads the private host state and uses 27015; default `local` addresses the retired private fixture on 27017. It implements Xash's RCON format, which differs from the earlier GoldSrc helper. Never print its password. The payload ends with newline **and NUL**.

Do not use `cofcoop.network.rcon` for this Enhanced session; that helper targets the earlier retail protocol. Also, `state/internet-control-status.json` may still contain `remote_join_verified=true` and old helper PID 49744: those refer to the historical retail session and do not prove the remote friend has joined the current build.

`HOST-STORY.cmd` starts a new host only if no Cry of Fear game is already running, the engine hash matches private session state, and the c_start/c_start2 fixture overrides are absent. Do not run it merely to check status.

Build from project root:

```powershell
& .\tools\build_enhanced.cmd
```

This uses VS 2022 BuildTools `amd64_x86`, Python launcher `py -3.14`, existing Waf configuration `.lock-waf-local-20260927`, and targets `xash,vgui,ref_gl,menu`. Normal local tooling uses `python` (3.13). PyInstaller 6.19 is already installed there.

Enhanced checkout HEAD: `e4cdbfaa260696eb886cf6c9bd4c371d53377427` (0.4.0-test3). The prepared engine includes upstream Enhanced patches plus our local changes. **Do not reset/clean this source tree or discard untracked modules.** Avoid plain project-root `git status`: the outer directory inherits a Git repository rooted at the user profile. Target the actual source checkout explicitly for Git work.

## Matching build identities

Host and v4 packaged `xash.dll`:

`4CB65CD93E9C8785E96BEAAE09A32EB1B6A3C52E0C154836B1C15E5DC35D5550`

Exact stock retail `cryoffear/cl_dlls/hl.dll` required by Enhanced:

`0036B91C01E92ED205513F52563053A55A66A32D257EFB5F476B8F1F3DDE0E63`

Do not deploy the earlier patched retail DLL to Enhanced. Enhanced preserves its on-disk stock DLL and applies the needed narrow opt-in transition change in memory.

Generated private models (both clients need both):

- `cryoffear/models/player/cof_s1/cof_s1.mdl`: `e791b16acc4ea61c9e433a854d1d7c8f2fb22e166f230a4c6a3d4b3e104565e9`.
- `cryoffear/models/player/cof_s2/cof_s2.mdl`: `402c3344794a7f31f68e52c68dd8118ba4232a9a6160a78d69682cffc0ca4620`.

Current generator outputs are under `evidence/simon-models-v2`; older long-name models remain historical. Player one uses standard Simon. Player two uses the same walking Simon rig with stock Sick Simon face/hoodie/jeans textures. The wheelchair model was rejected because it lacks normal locomotion. Several reload/CPR/door animations use documented fallbacks; exact visual parity is not established. Actual decoded vertex poses showed normal model scale/origin, so do not apply a guessed −36-unit correction. Overlapping spawn positions caused the close-up legs in an early screenshot.

## Engine implementation and concrete bug fixes

All paths in this section are under the prepared engine's `engine` directory unless stated otherwise.

### Story opt-in and retail compatibility

`server/sv_cof_story.c/.h` and helpers in `server/sv_cof_cheats.c/.h` implement `cof_story_coop 1`, exact DLL validation, diagnostics and ordinary game-callback item grants. Gate checks include Cry of Fear game directory, multiplayer, non-background map, DLL hash and expected instruction bytes. The six-byte multiplayer transition veto at retail module RVA `0x109D60` is temporarily replaced; original landmark arguments and relocation entries are retained. Restore it before DLL unload. Private-data access validates allocation size and the entity back-pointer.

### Multiplayer world/player transitions

`server/sv_save.c`, `sv_game.c`, `sv_client.c`, `sv_init.c`, `sv_main.c`, `server.h`, the transition helper and `client/cl_main.c`:

- Capture per-client slot/userid/epoch tickets and owned inventory before map teardown.
- Reuse native world snapshots and landmark translation. Carry players/inventory even when ordinary visibility selection omits them; check owner and aiment independently.
- Exclude stale destination player/inventory snapshots on return, while retaining world changes.
- Reject missing snapshot/landmark prerequisites before teardown.
- Send `cof_story_changelevel`, preserving the established netchannel and userid, instead of generic reconnect/new identity.
- Finish each client's transition at sign-on; resume the world after all pending clients become ready or disconnect.

**Latest fixed client error:** the first two-client transition captured/restored both players to the correct translated destination positions, but the guest later failed inside `CL_ParseMovevars` (`client/parse/cl_parse.c`, call at line 2692 in that build). The story client handler cleared entity storage but did not recreate it; movevars writes world entity zero. The handler now saves maxclients, calls `CL_ClearState()`, then `CL_InitEdicts(maxclients)`, sets changelevel and reconnects on the same channel. This matches native `svc_changing`. Server-data handling intentionally skips allocation while changelevel is true. An actual-C regression failed before the fix and passes afterward, including duplicate notification and demo guards. Independent review found no lifecycle blocker. **Live two-PC transition repeat remains pending.**

### Party items and doors

`server/sv_cof_party.c/.h`:

- Phone/knife catch-up reads actual `inventorySlot[7]` at player-private offset `+0x21FC`; usable indices are **1–6**, index 0 unused. Entries are descriptor paths such as `inventoryitems/weapons/weapon_mobile.txt`.
- `inventorySlotClassname` at `+0x2234` exists in save metadata but stays empty; the earlier diagnostic mistakenly used it. Static assignment code and read-only live inspection confirmed the correct field.
- Normal Spawn/Use/Touch grants are followed by checking actual inventory possession. Owned-edict presence alone is not proof. Preserve intentional drops and bounded retry behavior.
- Cache actual inter_door owner/destination/companions before one-shot deletion, wait for owner completion, then choose floor/LOS/collision-safe companion positions. Include 34-unit offsets because the real black transition rooms cannot fit 40-unit offsets.
- Wait boundedly through temporary camera/freeze states; cancel death, disconnect or identity reuse. Clear door-caused invisibility only with the prior visible identity-matched baseline and a completed playable exit, not by globally clearing cutscene flags.

Diagnostics now emit `descriptor="..."` for all seven inventory slots, with empty quotes for empty slots. The verifier compares whole descriptors. Old `class=empty` status output cannot prove missing inventory and is rejected by the updated verifier. Separate owned-edict lines are supplemental only.

### Join overflow and Simon transmission

`SV_CoF_StoryUserinfo` modifies only the transmitted model name in `SV_FullClientUpdate`; it does not replace the game entity's physics/animation model.

The old longer alternate name caused a 256-character string to be copied into `info[256]`, followed by a terminating NUL overwriting the stack cookie. Dump evidence pinpointed this at `SV_FullClientUpdate`, not the remote friend's connection attempt. The central `common/infostring.c` setter now rejects length **equal to** capacity (`>=`), and the model hook preflights an atomic replacement preserving all original fields if it cannot fit. Names are now six characters, `cof_s1`/`cof_s2`. Canary/native tests reproduced the old exact-length failure and passed the correction. **Two real local clients successfully joined and remained responsive after this fix.**

## Evidence and verification limits

Verified:

- All-target x86 engine build succeeded with the final hash above.
- 22 focused tests passed in `evidence/enhanced-release-fix-tests.log`: actual-C userinfo capacity, actual-C client transition/Movevars, party/status and fixture/verifier tests.
- Separate native transition lifecycle harness passed epoch/identity/sign-on/disconnect/slot reuse/owner-aiment/world filtering cases. Earlier broad suite had 87 passing tests; do not present that old count as the final suite count.
- Nine model tests passed; file ranges, bones, sequence mapping and compressed animation channels were validated. Current models differ from prior outputs only in embedded names.
- Eight standalone setup tests passed, plus frozen EXE self-test.
- Actual upstream Windows installer and model generation passed on a minimal private 15 MB asset fixture, with original source unchanged. **That fixture is not a playable game and was not launched.** Full copy planning separately preserves all 235 maps and 6,156 files (~4.70 GB).
- Independent v4 checking passed all nested ZIP CRCs, host engine identity, critical corresponding source byte identity and private-credential exclusion across 4,522 inner entries.

Useful evidence:

- `evidence/friend-setup-smoke-v4/smoke-report.json`.
- `evidence/enhanced-story-package-v4/independent-bundle-verification.json` and `package-report.json`.
- `evidence/enhanced-live/v3-initial.log`, `v3-before-outbound.log`: old diagnostic schema; use for identities/positions/world order, not private inventory conclusions.
- `runtimes/enhanced/story-host-v3.log`: capture/restore of both players into c_start2.
- `runtimes/enhanced-local-client/story-guest-v3.log`: concrete client Movevars failure. This log belongs to the pre-final transition fix.
- Previous overflow dump: `%USERPROFILE%\AppData\Local\CrashDumps\CoFLaunchApp.exe.29504.dmp`. Its old engine was `B361F55A...`; the current PDB has been rebuilt, so do not symbolize that old dump with the current PDB and assume a match.

**Not yet verified:** the remote friend's Enhanced remote join; both rendered Simon variants and all weapon animations on separate PCs; the repaired two-client outbound/return transition; no intro replay on backtracking; all campaign doors/cutscenes/combat/endings/save-resume. There is no passing five-phase live round-trip proof yet.

**Known unimplemented compatibility areas:** general key-item replication/ledger; whole-party coordination for `cof_teleport` cutscene movement (the retail callback moves only its activator). Do not call the full campaign fixed based on phone/knife catch-up or a successful join.

## Network and private information

Most recent infrastructure check, during preparation of v4:

- Public endpoint `104.33.25.1:27015`; routed LAN address `192.168.1.241`, gateway `192.168.1.1`.
- Existing enabled UPnP lease UDP 27015 → `192.168.1.241:27015`, ownership description matched, roughly 21h52m remained when checked. **The lease is temporary; recheck if continuing later.**
- Router WAN query and public HTTPS lookup agreed. A previously malformed XML mapping response later parsed cleanly.
- Existing enabled Public-profile UDP firewall rule already binds the exact Enhanced executable; no applicable blocking rule found. No firewall/router changes were required for v4.
- This PC's public-address hairpin test is not proof of Internet failure. The remote friend's actual join is the remaining external test.
- The old public 27015 invite did not route to the former local 27017 test server. Do not blame the remote friend's simultaneous attempt for the independently proven code errors.

Credentials remain only in private state/config/invite paths. Raw engine logs can contain RCON credentials and identifiers: redact before displaying, sharing or packaging. Do not upload crash dumps or raw logs publicly. Do not include this private-session handoff in the redistributable ZIP.

## Next agent's first actions

1. Read this file, then inspect current game process and use the read-only status command. **Preserve an existing session.** Confirm whether the remote friend has received/extracted v4 and joined; do not assume the user has completed those steps.
2. If setup fails, ask for its exact error and inspect the separate copy's `friend-setup-install.log`. Diagnose the actual failure without modifying their Steam install. Both sides must use protocol 49 and the matching v4 engine/model set.
3. After the remote friend joins, record two real ready userids and both actual descriptor inventories. Have the two little rascals confirm mutual Simon appearance, usable phone/knife and controls.
4. Coordinate an ordinary campaign door/return trip; capture before/after map, generation, userids, positions, health, inventories, door/visibility/freeze flags and transition logs. In particular, verify the Chapter 1 opening does not replay when returning from c_start2.
5. If a problem remains, distinguish client map sign-on, wrong arrival location, camera/fade state, shared items and persistent world state. Fix the evidenced cause. A plain changelevel loop cannot preserve world snapshots and is not a substitute.
6. If another build is needed, keep its source and binary matched, package a new revision for **both** PCs, and coordinate closing the active game before staging. Announce any launch. Do not reopen two clients here.

The controlled fixture remains available via `tools/story_fixture.py` and `tools/verify_story_fixture.py`, but is **not installed in the current campaign host**. Its c_start/c_start2 overrides were hash-checked and moved to `evidence/enhanced-live/retired-fixture-v2`. Do not reinsert them into active gameplay. The old fixture v1 deliberately failed visual validation because it reused a spawn inside a sealed room with all-black textures; fixture v2 corrected that. Any future controlled reproduction must respect the user's one-client-per-PC preference and preserve progress.

For native app inspection, use the installed computer-use skill and its `node_repl` + `@oai/sky` APIs after reading the skill. Select current returned windows, never stale PIDs/handles. Do not build PowerShell UI automation. The browser-only `cua_repl` native app surface is disabled here. Prefer logs and game status where they answer the question; some background/minimized game captures returned the desktop instead of game imagery, which is not proof of a black game screen.
