# Whole-system code review, 1.2.5 → 1.2.6 (2026-10-22 evening)

The user, after a playtest where he was the guest on 1.2.5 ("We still had some issues"; the guest log holds no server
detail) asked to "debug the entire system", then: "have a sub agent using Fable 5.1 to review the code". Two read-only
Fable 5.1 passes reviewed the code at 6b973ab (1.2.5):
- **First pass:** the engine story modules, transitions and save/restore, the command path, the globals swap, the
  1.2.5 changes, and the launcher's update, join and settings paths.
- **Second pass:** what the first pass did not reach. That covers party doors, teleports and placements, the door
  rooms, moments, companions, the item and pickup ledgers, the small modules, the client CoF modules and the rest of
  the launcher.

Each finding was checked against our code and the decompilation (`/home/claude/dbg/decomp`, byte-identical to the
retail DLLs) before anything was changed. Reports: session scratchpad `fable-review/`, `fable-review2/` (not kept).

## Fixed in 1.2.6

| Finding | Verdict | Fix |
|---|---|---|
| A player stacked on a partner (retail MP PreThink: SOLID_NOT) never touched a trigger. The 1.0 stacked-touch rule in `SV_LinkEdict`'s non-solid branch needs `touch_triggers`, but `SV_RunCmd`'s story branch linked a SOLID_NOT player with `false`, and players skip `SV_Physics_Entity`. So the rule never ran. Two players on one door-room walk-in spot (shared by design since 1.2.3), on a shared J spot or in c_intro's kunitei never took the way out until they moved apart. | confirmed | `sv_pmove.c`: `SV_LinkEdict( clent, !sv.playersonly )`. `sv_world.c` still touches only for a living player overlapping a living partner. Test: `test_coop_engine` (the pmove call and the clients' physics skip) |
| A team wipe before a new game's or chapter start's first autosave loaded the previous session's checkpoint. `latest.txt` still named it, and `CoF_SessionLatest` re-read it. | confirmed | `cof_session_owned`, set by this session's successful save or checkpoint load and cleared by `SV_CoF_SessionReset`. Until then `SV_CoF_SessionWipe` lets the game's own restart run (`session-wipe-no-checkpoint`). The first-frame log says `session-checkpoint-not-ours` instead of `-unusable` when Continue is off. Test: `cof_session_lifecycle_test` scenario, `test_enhanced_session` wiring |
| Duplicate names disable every checkpoint, silently (`CoF_SessionMatch`). Xash's duplicate check saw only *spawned* clients, so someone joining while the host loads a level kept a taken name. | confirmed (narrower than reported: stock Xash renames against spawned players) | `sv_client.c` `SV_UserinfoChanged`: in story co-op a connected (joining or level-changing) client counts too. `SV_CoF_SessionSave` logs `session-save-refused reason=same-names` once a map |
| A Host_Error inside a command jumps past `SV_CoF_GlobalsLeave`. Every later Enter only nested, so the per-player globals were shared for the rest of the process. | confirmed (minor) | `SV_CoF_GlobalsReset` from `SV_CoF_StoryShutdown`. Test: `test_enhanced_globals` |
| An exit touch counted while its player was still in a door. `b_GoingThroughADoor` is raised when the door opens, and EndSequence moves the player 2 s later. Opening the door back from inside the exit just arrived in took the player through that exit at once, then the door set them down in it again. That happened live: host log 2026-10-22 05:07:39-42, cm_apartment3 `land3`, two section moves for one door. | confirmed | `CoF_ArrivalTouchCounts( in_door, ... )`: mid-door never counts. The 3 s window after the door counts as before; the flag clears in EndSequence before FireTargets, CoF_InterDoor.cpp:521-536. `SV_CoF_ArrivalRefuses` logs `reason=mid-door`. Test: `cof_arrival_harness` |
| Engine placements without the "arrived inside" protection. A respawn or late joiner set beside a partner (`CoF_PartyPlace`), or J (`CoF_PartyWarp`), could land in an exit back the partner stands in, after the 15 s grace: the party went back a map. | plausible, cheap | `SV_CoF_ArrivalLanded` after both moves. A held exit is unaffected: `SV_CoF_ArrivalRefuses` returns early for it. Door companions follow the opener on purpose, so they are left as they were. Test: party harness |
| A door opener's next door within 5 s reset the event and dropped a companion still pending, without the cut-off line. | confirmed (minor) | `CoF_PartyWatchDoors` calls `CoF_PartyLeftBehind` for the old event's pending companions first. Test: party harness scenario, fails without the fix |
| Ladders read as busy for the party while the last climber was frozen with another camera model: the manager keeps its owner for good, and a strangle or model cutscene sets `pDeathScene`. | confirmed (minor) | `CoF_MomentClimbsThis`: the climber's camera_model is this manager's model. Test: moments harness |
| `_http` left its socket to the GC when the deadline had already passed. | confirmed (trivial) | `cofcoop/network.py`: the timer's delay is computed before the socket opens |
| `SV_SetModel: invalid entity freed` at 05:09:32 this morning, a guest's `inventorydualwield 1 6` re-dual of the same pair. | the 1.2.4 bug 1.2.5 fixed (the old loop switched to the dual being removed). This log was 1.2.4 | Diagnostic: in story co-op the freed-entity branch logs `phase=setmodel-invalid` with the entity, serial, model and the caller as an hl.dll RVA |

Also in 1.2.6, for the playtest itself: 1.1.1 to 1.2.5 all join each other (`cofbuild` 1.1), and almost every fix runs
in the host's game. A host who did not update silently plays without them. The host now sends its release (`cof_label`
in the session cfg, `cofver` in the info reply), and a guest whose app is newer is told before joining
(`host_version_note`; a host sending none is "older than 1.2.6"). Tests: `test_coop_play`, `test_coop_app`,
`test_coop_engine`.

## Review of the fixes

A third Fable 5.1 agent reviewed the 1.2.6 diff before release. It found nothing wrong and no regression. It checked:
- the stacked touch for companions;
- mid-door against every inter_door flow (EndSequence clears the flag before FireTargets, and
  `inter_door_coop_changelevel` is landmark-less);
- `cof_session_owned` across loads, transitions and Continue;
- the rename across level changes;
- MSVC constructs.

Three low points, all taken:
- **Rename of an earlier try:** a slot still joining from the same address (the player's own earlier try, left until
  `sv_connect_timeout`) would have renamed their retry. It is no longer counted (`NET_CompareBaseAdr`).
- **No checkpoint after a refused Continue:** a refused Continue left the map with no checkpoint of its own for the
  whole map (the first frame kept the old one and planned no autosave). It now schedules an autosave
  (`SV_CoF_SessionAutosaveSoon`).
- **Cut-off line before the ending door:** the reused-door cut-off line is skipped when the new door is the ending
  door, which takes everyone anyway.

## Checked and false

- `recalldocument <n>` unbounded: `RecallDocument` returns outside 1..16 and `documentsstruct` has 17 entries.
  `quicksel` is bounded 1..3, and `inventorycheck` is empty.
- `skillset` from a guest: a no-op in multiplayer (`!IsMultiplayer()`).
- `CoF_NavLoad` link overflow: each link record must name its node as source, so a link is appended at most once and
  the total stays within the file's own count (≤ `COF_NAV_LINKS`).

## Left

- Door-room walk-ins whose trigger waits between fires (c_buscity2, c_park3, c_hamn, c_stad, c_subway1st1: `wait 2`):
  a second player on the walk-in goes up to 2 s after the first. The `wait 10` ones are outside the main story maps
  (c_base, c_ec*, c_lobby, c_garden, ...).
- Not reviewed by either pass: `common/cof_language.c` and `cof_entpatch.c` bodies, the client panels, `cof_quicksave.c`,
  `cof_osk.c`, `tools/release.py`, `tools/app_update.py` beyond the first pass's download checks.
