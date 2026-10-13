# Story co-op pieces 2 and 4: story moments and polish (offline)

## Goal

Make the next live test as close to the finished campaign as possible without
launching the game: pieces 2 (story moments) and 4 (polish) of
`docs/superpowers/specs/2026-09-30-coop-split-play-design.md`, on top of the
uncommitted piece 1 and 3 work on launcher `split-play-stage3` / engine
`story-coop`.

## Constraints

- No game launches, commits, deployments to playing copies, or Steam-install changes.
- Behind the existing opt-in story mode and verified retail DLL; no hl.dll code patches.
- Static research first (capstone over the stock `hl.dll`, entity lumps of the 81 story maps),
  then pure-state rules with native harnesses, then engine glue with its own harness.
- Offline tests and a successful build do not establish live behaviour.

## Research (2026-10-12)

Four read-only static passes, indexed in `docs/research/2026-10-12-story-moments-re.md`:
cutscenes/cameras/screen effects, endings/P345/phone/branch logic, player-state and puzzle
entities, and a chain audit of all 81 story maps.

## Tasks and outcome

1. Fix the five older harness tests that failed only because the shell sets
   `NoDefaultCurrentDirectoryInExePath` (run `.\name.exe`). Done.
2. Cutscenes. A first version mirrored `trigger_camera` views to partners; the research then
   showed the retail camera already applies itself to every player, so it was replaced by a
   watchdog (`sv_cof_scene_state.h`, `sv_cof_scene.c`) for what the retail code leaves wrong:
   revived-after-a-scene players, respawn/rejoin mid-scene, the telescope taken over, the late-join
   model-cutscene wait (including the wait wrongly started by ladder/strangle camera models).
   No freeze is saved or carried through a transition. Done.
3. Watching together = travelling together: partners viewing the same running camera follow
   the owner's scripted teleport. Done.
4. `cof_story_respawn_delay` (default 10 s, retail 60). Done.
5. Story moments (`sv_cof_moments.c`): fall-death floors, phone calls, ladders, the wheelchair
   rider, disconnect cleanup, yes/no prompts; the acting player is "the first player" during their
   usercmd (`sv_pmove.c`). Done.
6. Endings: shared doctor weapon and `cof_begingame` stats (story values, checkpoint format 2);
   the ending door brings everyone to the opener's room. Done.
7. Scripted weapon gifts shared like the starter kit, remembered per player across maps. Done.
8. Independent code review (ready with fixes); every Important finding and most Minor ones fixed
   with regressions. Run report, docs, full suite, x86 build, staged disposable copies, handoff
   notes. See `AGENT_NOTES.md` for the verification state.
