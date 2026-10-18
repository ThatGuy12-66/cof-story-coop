# Fixes from the decompilation survey: plan

The user, after the survey (`docs/research/2026-10-18-cof-decompilation-survey.md`, slice notes in
`evidence/decomp-survey-20261018/`): "I want you to implement all the ... things we learned from the decompile."
Execution: inline, task by task, no reviewer subagents (user memory), TDD where the code allows (offline C harnesses
that compile the real engine module, or source-wiring tests, as in `tests/test_enhanced_*.py`). No game is launched;
live checks wait for the user's go-ahead. Ledger: `.superpowers/sdd/2026-10-18-decomp-fixes/progress.md`.

Global rules for every task:
- Re-check each retail fact against the retail hl.dll/client.dll bytes (`tools/hlre.py`, the smoke copy's DLLs) before
  coding it; the survey read the decompiled source.
- Behaviour that departs from retail gets a cvar, default on for story co-op, registered with the other `cof_*` cvars.
- Engine changes go to the engine repo (`upstream/cof-enhanced/xash3d-fwgs-...`, branch `tester-package`), launcher
  changes to this repo; one commit per task (or small group) in each repo touched, tests with the code.
- Never touch the retail install or `runtimes/enhanced*`.

## Tasks (survey section in brackets)

### A. Certain engine fixes
1. `usercmd.is_donator`: 0x38-byte command for `pfnCmdStart` (humans, bots), cvar `cof_donator` (default 1: everyone
   counts as a donator, so TMP pickups work; 0 = retail non-donor); client `CL_CreateMove` gets a 56-byte scratch
   command so client.dll's write cannot land in `runcmd_t.processedfuncs`. [1.2]
2. Companion join/leave lines: mark a new bot's `P+0x24D0` (`m_fGameHUDInitialized`) after `ClientPutInServer`; drop
   the "has left the game" SayText of bots removed for a level change. [3]
3. Ladder busy test: owner frozen, or `P+0x5AC` set / `P+0x388` valid, instead of FL_GODMODE. [1.1]
4. `HealthCap` is a type in `cof_hud_remake.c`. [1.7]
5. Chat `%` filter (say, say_team) before the game DLL. [7]
6. Bounds for client-supplied indices (`quickselset1-3`, `inventory*`, `recalldocument`, and every other indexed command
   in I §3/§5). [7]
7. Block `vote`/`votekick` in story co-op. [4]
8. Notes never in slot 16. [7]
9. Simon models: police1's 4th attachment (`tools/prepare_simon_models.py`). [1.8]
10. Far clip: cvar `r_cof_farclip_scale` default 1.0 (retail), Xash's 1.73 on request. [1.3]

### B. Soft-locks and stuck players
11. c_apartmentsick chainsaw boss: re-create `theboss` after its kill animation when a participant lives. [2.1]
12. c_start padlock: the solver is FIRST while `Finished` runs. [2.2]
13. Wheelchair death in MP: release the rider, then `ClientKill`. [2.3]
14. c_lake boat: bring living partners to the dock exit at Disembark. [2.4]
15. Crazybitch: cancel the struggle she starts on edict 1 when her enemy is someone else. [2.5]
16. Syringe heal interrupted: clear a stale `fuser3 == 25` and the view lock. [2.6]
17. Brain: no CPR/duck/syringe near an engaged taller, never melee the chainsaw boss, keep away from it. [2.7]
18. Bosses tied to the first player: steer the roof boss's and the doctor's first player to a visible living
    participant. [2.8]
19. 3D keypad: the user's own keys, not the host's keyboard. [2.9]
20. Tape saves: `savehack` only after that player's tape-recorder `SaveLoad`; difficulty-4 tapes shared. [2.10]

### C. Open issues and multiplayer differences
21. Costumes: scriptsettings lines 51-62 in the bonus rewrite, cvar `cof_costumes` default 1. [3]
22. Fog: remember the last `SetFog` per map (per section in merged maps) and replay it on respawn, join and section
    move. [3, 5]
23. Camera views: make sure every player held by a camera has `P+0x2038` set, so the client hides partners. [3]
24. Objectives: mirror the custom objectives file with the key. [1.5]
25. Story-item duplicates in the share ledger. [1.4]
26. Engine-made commands during a phone call or the computer/phone UI. [1.6]
27. Night vision on F in co-op. [3]
28. Single-player carry limits in MP (`iMaxAmmo1`), cvar `cof_sp_carry` default 1; fixes flares. [4]
29. Simon's voice instead of the police officer's, cvar `cof_simon_voice` default 1. [4]
30. Chat and the co-op HUD on story maps (client patch of the `cof_` map-name test), cvar default 1. [4]
31. Scripted weather in MP (`rain_modify` with a name), applied to every player. [4]
32. Shared globals: `room_type` and the morphine muffle only from the host's own state; per-player weapon-spread
    statics; the cutscene global per player; `host_framerate` reset on spawn. [4]
33. Humans cannot hurt the AI companions (cvar). [4]
34. `unfreeze 1024`/`stopcomputer` from a viewport reset ignored while a scene holds the player. [4]
35. Message strip without `cl_subtitles` for our engine's own lines. [4]
36. Dual wield: per-player state of the global left-hand object. [3]
37. Lantern and flare lights visible to others. [3]
38. Co-op tape saves must not overwrite the single-player slot labels (`saveinfoN.cof`). [3]

### D. Merged maps
39. Rain per section (`RainData` per client on section moves). [5.1]
40. 3D sky per section (`skymark_*` per client, other sections' sky models not sent). [5.2]
41. Planner: first-of-class refusal, 16 transition pairs, rain/3D-sky notes, `REF_KEYS` additions, false refusals
    (non-looping `ambient_generic`, `func_fogfield`, `func_train`). [5.3-5.6]
42. Per-section view distance (zmax per client from each member's MaxRange). [3, 5]
43. Renderer patch: particle source lookup and Cg water past edict 900/1024. [3]
44. Bot weapons left as orphans in transition saves. [3]
45. `cof_mdlcutscene` camera models removed after the scene. [3]

### E. Records
46. Research doc corrections (survey section 6), AGENT_NOTES, campaign test guide entries for the live checks.
