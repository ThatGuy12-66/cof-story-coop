# Story transition boundary: evidence and proposed adapter

**Superseding implementation update:** The assessment below is historical. The retail malformed-null-patch failure was later localized to an unmodified PE relocation, and repaired. Ordinary map reloads then demonstrably reset hub state. The separate Enhanced source now implements an opt-in multiplayer landmark/save adapter, per-client restoration, and party coordination, with native tests and a successful build. First live Enhanced fixture rendering was black with HUD, so full transition behavior remains unverified. See [current implementation evidence](enhanced-implementation-session.md).

2026-09-27. This document is a source assessment and design, not an implemented fix or a successful game test. No engine, game DLL, runtime, or active game session was modified for this assessment.

## Decision

**STATIC:** The prepared Xash engine already defers the game DLL's change-level callback to the next outer engine frame. Adding another frame of delay is not currently justified. The smallest useful source change is transition tracing with explicit acceptance/rejection, followed by a narrowly scoped request record that preserves the requested landmark independently of the execution mode. Reuse the existing host-state commit boundary.

**UNRESOLVED:** The retail engine's natural-exit hang has not been localized. A successful RCON `changelevel` and a failing DLL callback do not establish reentrant teardown as the cause. They differ in their callers, timing within the frame, and the game state established before/after the callback. Xash's source cannot prove the retail engine has the same scheduling behavior.

**STATIC:** Enabling smooth multiplayer transitions by deleting one guard is insufficient. Xash's reconnect path consumes a global restore flag for the first client, and its client-state file handling starts from the first client. The game DLL also retains its own story exit, door, camera, and multiplayer-rule assumptions. Engine scheduling alone cannot repair all of those.

## Exact source under review

- Enhanced outer commit: `e4cdbfaa260696eb886cf6c9bd4c371d53377427`, version `0.4.0-test3`.
- Engine base: `4857b389e6ba32ddaa68582aedcbc950c138f46a`, with the prepared Enhanced patch stack.
- Engine directory: `upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/` beneath this project.
- All source references below are relative to that directory and use the prepared source's line numbers. The source/build inventory and isolated runtime checks are in [enhanced-assessment.md](enhanced-assessment.md).

The outer checkout was clean when checked for this assessment. The successful prior build establishes compilation only; the isolated Enhanced runtime has not been launched by this assessment.

## Observations supplied by the live test owner

These are **REPORTED FROM THE PARENT'S RETAIL TEST**, not new runtime observations made here:

1. Bypassing the game DLL's multiplayer exit gate reaches a retail-engine rejection: `changelevel2 <levelname> : not for use with multiplayer games`.
2. Additionally substituting a NULL landmark at the game callback hangs during the natural `c_nightmare` to `c_start` exit.
3. An RCON ordinary `changelevel` previously reached playable gameplay.
4. The next diagnostic fixture is an immediate BSP trigger reproduction under a debugger, avoiding another long cinematic.

These observations show that bypassing the first gate exposes another problem. They do not yet show whether the failing run stops before the callback, inside the engine, after returning into game code, during map creation, or during client sign-on/rendering.

## Existing callback and command paths

```text
Game DLL touch/think/frame code
  -> pfnChangeLevel(level, landmark)             sv_game.c:1378
       validate active server; consume spawn latch; normalize landmark
  -> SV_QueueChangeLevel(level, landmark)        sv_game.c:708
       read/validate map; select smooth/classic; mark one update skipped
  -> COM_ChangeLevel(map, selected landmark)     host_state.c:130
       copy strings; set GameState.nextstate = STATE_CHANGELEVEL
  <- return to game DLL, then complete current Host_Frame
  -> Host_RunFrame tail                         host_state.c:193
       set current state = STATE_CHANGELEVEL
  -> COM_Frame exits because oldState was RUNFRAME

NEXT OUTER COM_Frame
  -> SV_ExecChangeLevel                         host_state.c:236; sv_init.c:1140
  -> SV_ChangeLevel                            sv_save.c:2230
       save if selected; inactivate; deactivate; spawn; restore/activate
```

The decisive proof is `COM_Frame`'s `oldState == STATE_RUNFRAME` break at `host_state.c:248`. A request made during `Host_Frame` cannot fall through into `SV_ExecChangeLevel` within that same invocation. This is a host-loop deferral, not a promise that precisely one simulation tick or a fixed number of milliseconds elapses.

The corresponding console handlers are **`SV_ChangeLevel_f`** (`sv_cmds.c:554`) and **`SV_ChangeLevel2_f`** (`sv_cmds.c:576`). There is no function named `Host_Changelevel_f` in this prepared checkout. Both handlers funnel into `SV_QueueChangeLevel`. `changelevel` supplies NULL; `changelevel2` supplies its optional landmark. Neither handler contains the retail engine's multiplayer rejection. The queue instead forces classic mode when `svs.maxclients > 1` (`sv_game.c:745`).

RCON executes its command through `Cmd_ExecuteString` (`sv_client.c:1076`) during packet processing. That happens before game physics in `Host_ServerFrame`; a natural exit can originate during a game callback in physics. Both Xash routes use the same deferred commit after their distinct callers return.

### What remains synchronous and what continues afterward

- `SV_MapIsValid` (`sv_game.c:917`) reads/parses the destination entity script synchronously in the callback path. Deferring teardown does not defer this filesystem work. A stall there would require evidence before moving validation.
- `SV_TouchLinks` (`sv_world.c:499`, DLL touch at line 562) continues its linked trigger traversal after the callback. `SV_Physics` (`sv_phys.c:1780`) continues entity processing. This does not inherently invalidate pointers: the queued transition has not freed the old map's edicts.
- `SV_RunGameFrame` (`sv_main.c:640`) can run multiple physics ticks while consuming residual time. It does not break that loop just because a transition is pending. Thus later old-map events can still run before commit. Whether that matters to Cry of Fear must be measured.
- `SV_SkipUpdates` (`sv_frame.c:925`) marks spawned non-bot clients to skip one network send. It does not stop physics, unwind game calls, or suppress the rest of the host frame.
- The `globals->changelevel` checks in pusher movement (`sv_phys.c:878`, `994`) do not constitute a general queued-transition guard. That flag is set when executing the smooth save path, not when any ordinary request is queued.
- `Host_Frame` also performs client-frame work after `Host_ServerFrame` (`host.c:673`). A callback return alone does not establish that the full old-map frame completed.

If a DLL callback or subsequent DLL code never returns, this state machine cannot reach the deferred commit. Adding a second delay or queuing another console command would not make that call return.

## Concrete asymmetries worth tracing

| Boundary | Static behavior | Diagnostic implication |
| --- | --- | --- |
| Game callback deduplication | `pfnChangeLevel` sets static `last_spawncount` before calling the queue (`sv_game.c:1387`). | A rejected request consumes the callback's once-per-spawn allowance. Later callbacks in the same spawn are suppressed; console commands bypass this latch. This is a real asymmetry, not an established hang cause. |
| Queue versus host acceptance | `SV_QueueChangeLevel` and `COM_ChangeLevel` return void. `COM_ChangeLevel` silently ignores a busy host state or active credits (`host_state.c:132`). | Logs must distinguish request seen, map valid, and request actually accepted. The current caller cannot distinguish them. |
| Premature network suppression | `SV_SkipUpdates` runs before host acceptance (`sv_game.c:764`). | Even a request rejected by `COM_ChangeLevel` can suppress one send. Move this only as part of an explicit acceptance contract. |
| Landmark selects restore mode | Nonempty `GameState.landmarkName` sets `GameState.loadGame=true` (`host_state.c:141`). | This field cannot safely double as passive multiplayer arrival metadata. Keep requested landmark and selected restore mode separate. |
| Multiplayer fallback | The queue converts all multiplayer requests to classic mode and passes NULL onward (`sv_game.c:745`, `768`). | The original landmark is lost before execution; capture it before that conversion if future arrival logic needs it. |
| Landmark validation | `SV_MapIsValid` searches matching `targetname` (`sv_game.c:944`); it does not require `classname=info_landmark`. | `MAP_HAS_LANDMARK` alone does not prove an actual landmark entity exists. Resolve class/name/origin explicitly for arrival work. |
| Compatibility string handling | Active `HACKS_RELATED_HLMODS` strips the callback landmark at its first space using an unbounded copy (`sv_game.c:1393`; `common.h:83`). | Preserve the compatibility rule using bounded handling in an adapter. No evidence here implicates an oversized map landmark in the observed hang. NULL is checked and becomes an empty local string in this path. |
| Frame-age validation | The queue can reject requests before physics frame 15 when `sv_validate_changelevel` is enabled (`sv_game.c:754`). | Record this cvar and frame count in an immediate trigger fixture; do not confuse an intentional guard with a hang. |
| Transition messages | `SV_InactivateClients` retains pending messages only when `globals->changelevel` is set (`sv_frame.c:984`). Its comment explicitly cites CoF `HideHud` and `PlayMp3`. | Classic and smooth transitions can carry different client presentation state. Do not label a black/hidden/frozen client an engine deadlock without inspecting server progress. Do not preserve every old message indiscriminately as a speculative fix. |

## Why the existing smooth path is not a multiplayer solution

`SV_ChangeLevel` has two substantially different execution paths:

- **Smooth:** set `globals->changelevel`, run `SaveGameState(true)`, reconnect/deactivate/spawn, restore destination state or spawn its entities, restore adjacent entities with landmark offsets, then activate without normal settlement physics (`sv_save.c:2253`). A save failure returns before deactivation and clears the changelevel flag.
- **Classic:** reconnect/deactivate/spawn, call the game DLL's `pfnResetGlobalState`, spawn destination entities, then normal activation (`sv_save.c:2291`). This does not provide persistent hub state or promised inventory carryover.

Several barriers remain even if multiplayer is allowed into the smooth path:

1. **Per-client restore completion is missing.** `LoadAdjacentEnts` sets global `sv.loadgame = sv.paused = true` (`sv_save.c:2122`). `SV_PutClientInServer` tests that shared flag (`sv_client.c:1337`) and clears it on the first restore sign-on (`1373`). A later client takes the fresh-spawn path: `SV_InitEdict` at `1382` followed by DLL `pfnClientPutInServer` at `1393`. That path reinitializes player data instead of giving each client its own restore completion state.
2. **Client save state is first-client oriented.** `SaveClientState` and `LoadClientState` begin with `sv_client_t *cl = svs.clients` (`sv_save.c:1370`, `1475`). View entity and client presentation/save payloads need an explicit multiplayer format/lifecycle, not just additional entity serialization.
3. **Entity serialization is not wholly single-player.** `SaveGameState` can mark multiple `FL_CLIENT` entities as `FENTTABLE_PLAYER` (`sv_save.c:1741`). This is useful existing machinery, but does not negate points 1 and 2. Restore maps saved player entity IDs to numeric client slots (`CreateEntitiesInRestoreList`, `sv_save.c:1593`); a reconnecting replacement must not inherit another player's inventory solely because it reused a slot.
4. **Transitions involve proprietary DLL callbacks.** `pfnParmsChangeLevel`, `pfnSave`, `pfnRestore`, `pfnClientConnect`, `pfnClientPutInServer`, and game global state all participate. Their Cry of Fear multiplayer behavior must be traced. Engine code cannot assume those callbacks implement story co-op semantics just because their ABI exists.
5. **World persistence and failure recovery are separate work.** The classic branch resets game globals. The smooth branch uses map save files and adjacent-state restoration, but its save/spawn sequence is not a demonstrated atomic multiplayer transaction. A failure after deactivation cannot be described as a verified rollback.

There are also three different flags whose meanings must not be conflated: host `GameState.loadGame` selects the transition execution path; server `sv.loadgame` controls restored client sign-on; game `globals->changelevel` affects save/physics/message behavior.

## Proposed minimum source adapter

**PROPOSED ONLY.** First add gated tracing with unchanged behavior and run the instant trigger fixture. If the test confirms that requests reach and return from the existing queue, adapt that boundary rather than replacing the host state machine.

### Acceptance and metadata

Introduce an internal result for request handling: accepted, duplicate/pending, inactive, invalid map, incompatible host state/credits, or validation rejection. Preserve the external game ABI: `pfnChangeLevel` still returns void, but the internal queue/host request functions can report a result.

Use one engine-owned pending record with copied values:

```text
request ID, session generation, source spawncount
source map, destination map, requested normalized landmark
origin (game callback / console / RCON where known)
selected execution mode (classic / existing single-player smooth)
request host frame and physics frame
phase (accepted / committing / destination active / completed / failed)
```

The initial multiplayer execution mode remains classic; retaining a landmark is metadata only. Do not populate the existing smooth-mode selector merely to remember the landmark. No player inventory or hub persistence is promised by this first slice.

The queue should reserve/accept one request atomically on the engine thread, record the reason for every rejection, then apply the one-send suppression and callback deduplication only after acceptance. A precommit failure must explicitly release the reservation and its accepted-request latch if retry on the same source generation is allowed. Log that release; do not leave an invisible static once-per-map lock.

Keep the compatibility treatment of space-containing landmark strings and use bounded copies. Reject or explicitly diagnose overlength identifiers instead of silently converting them into a different valid destination/landmark. Avoid converting the request to a textual command: that introduces tokenization and loses structured metadata.

### Existing safe commit point

Keep actual teardown in `SV_ExecChangeLevel` from `COM_Frame`'s `STATE_CHANGELEVEL` branch. Assert/log that the request's source generation still matches and that execution occurs outside a game DLL callback. A diagnostic callback-depth counter can prove this during the fixture; it is not evidence that the present engine is violating it.

Do not immediately invoke `SV_ChangeLevel` inside `pfnChangeLevel`. Do not use `Host_AbortCurrentFrame` to jump out of nested game code as a shortcut. Do not break arbitrary touch-list or physics internals before measuring whether continued old-map work is the failure. If the trace specifically proves that extra residual physics ticks cause a problem, stopping *additional completed ticks* at `SV_RunGameFrame` is a separate, narrowly testable change.

Moving map-file validation to the commit phase would shorten the callback, but changes error timing and retry behavior. It is optional work only if evidence shows synchronous validation is material; it is not required to obtain the existing deferred teardown.

### Ownership and reset paths

- Do not place cross-map metadata in per-level `sv`: `SV_SpawnServer` increments `svs.spawncount` (`sv_init.c:961`) and wipes `sv` (`986`). Do not retain raw edict, entity-string, or DLL-private-data pointers: `SV_DeactivateServer` (`sv_init.c:690`) frees the old world and player state.
- Give the request a session lifetime and explicit phases, with owned strings and numeric identities. If later arrival work needs it, keep the accepted record through intended deactivation and destination sign-on; do not clear it merely because the old map is being torn down successfully.
- Cancel pending state on an unrelated new game/map/load request, server shutdown, or `Host_Error` recovery. `Host_Error` already invokes `COM_InitHostState` (`host.c:736`) before aborting the frame; the new record must not survive that reset accidentally.
- After precommit rejection/failure, release the request and log whether the original map is still active. After failure during destructive commit, report the failed phase and leave no stale arrival record attached to a later map. Do not promise recovery without a tested snapshot/rollback implementation.
- `Host_SetState(...RUNFRAME...)` resets some host flags, not a future per-client restore transaction. Define completion separately: destination activation alone is insufficient when clients have yet to sign on.

### Future arrival and inventory extension

Once classic natural transitions are reproducibly reliable, extend the same record at the commit boundary with source/destination `info_landmark` origins and per-player snapshots. Capture a consistent final old-map state after callbacks return, before any deactivation; retaining only the landmark name at request time is not yet a player snapshot.

Use a session client identity plus connection generation, and associate it with the transition request. `sv_client_t.userid` (`server.h:222`) is a starting identity field, not permission to trust a reused slot or a newly connected client. Define treatment of disconnected/dead players, late joins, and players outside the transition volume explicitly.

Implement per-client restore-pending/completed state so every expected client takes its own restore path exactly once. Do not unblock the whole restore because one client consumes `sv.loadgame`. Test DLL save/restore callbacks with distinct inventories and verify the actual resulting item state. Recreate camera/control/visibility state deliberately, including legitimate cinematic hiding; clearing all `EF_NODRAW` is not an adequate lifecycle fix.

Hub persistence then needs versioned session world snapshots, global state, and repeat-visit tests. These are follow-on capabilities, not benefits obtained automatically from storing the landmark.

## Short reproduction and discriminating evidence

Use the parent's immediate BSP trigger fixture, an explicit destination map/landmark, pinned DLLs, and fixture-local configs/saves. Announce each launch and preserve the user's current game. No timer should terminate the run automatically. A stalled run should yield debugger evidence before another behavioral patch.

Record these events with one request ID, host frame, physics frame, source spawncount, and thread/call-depth information:

1. Game exit handler entered; multiplayer gate taken/bypassed; callback arguments and return address.
2. Engine callback entered; map-validation entry/exit; actual host acceptance or rejection reason; callback exit.
3. Return into the game exit handler and exit from the enclosing DLL touch/think/frame callback. This separates engine callback return from successful return through game code.
4. End of the old server/host frame; next outer host-state dispatch; destructive transition entry.
5. Save (if selected), deactivate, spawn, game server activation, and transition result.
6. Each client's reconnect/spawn/restore branch, camera/view entity, relevant door flag, visibility/control state, and first advancing destination frame.

Interpret the first missing event, not the appearance of the window:

| Trace outcome | Next focused investigation |
| --- | --- |
| No callback reached | Game gate, trigger dispatch, or earlier game code. Engine queue changes cannot affect an uncalled callback. |
| Callback entered but validation/return missing | Sample the blocked thread/stack; inspect the exact engine operation. Do not infer map teardown merely from entering the callback. |
| Engine callback returns but DLL handler/frame does not | Inspect the game continuation and its state assumptions; another engine-frame delay will not make it return. |
| Callback and host frame return, request rejected | Use recorded reason; repair acceptance/latch semantics only if the intended request should be accepted. |
| Accepted but next-frame commit absent | Inspect host-state replacement/reset and abort paths with request generation. |
| Commit starts but spawn/activation stops | Inspect map/resource/game-activation stack and error logs; this is later than callback scheduling. |
| Destination server frames advance but a client appears stuck | Inspect sign-on, camera, UI messages, flags, and client-frame stack. Distinguish this from a server deadlock. |

Run a small controlled matrix rather than stacking patches:

- Retail engine: ordinary RCON transition control; natural callback with only the exit gate bypass; natural callback with NULL landmark, each from a clean identical fixture state. Capture the callback/return stacks for the failing case.
- Prepared Xash: original stock DLL first to confirm the game gate; then the separately identified gate-only DLL if authorized, retaining the original landmark. Xash already chooses classic mode for multiplayer, so a second game-side NULL substitution is unnecessary for that particular engine-path comparison.
- For Xash request-contract work: invalid destination followed by a valid request in the same map; duplicate requests in one frame; a busy host state; map-validation guard on/off recorded; shutdown/error before commit. Verify acceptance, one commit, and absence of stale metadata without weakening gameplay guards silently.
- Only after a natural classic transition is reliable: two real clients, then four; distinct inventories; one client outside the trigger volume; reconnect/slot reuse; death/disconnect during transition; destination placement; and a hub out-and-back with a killed monster, unlocked door, consumed item, and global trigger.

An engine-only smoke result must not be called complete story co-op. The near-term success criterion is narrower and observable: the natural request returns, commits at the known boundary exactly once, and all participating clients reach advancing destination gameplay. Inventory, landmarks, hub persistence, skipmanager behavior, and visibility each need their own demonstrated result.
