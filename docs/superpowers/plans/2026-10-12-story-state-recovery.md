# Story co-op: persistent exits, shared state and recovery

## Scope

Implement the three follow-ups requested on 2026-10-12 against launcher
`bc7b516` and engine `fb29635`. The split-play specification and the October 4
retail/engine research are the starting evidence; verify assumptions against
the current source and native harnesses before depending on them.

## Constraints and decisions

- Work in the existing `split-play-stage3` and `story-coop` checkouts; build
  paths are tied to them. Preserve the two existing untracked research files.
- No commits, branch changes, Steam-install changes, or publication.
- Keep changes behind the existing opt-in story mode and verified retail DLL.
- Preserve existing runtime saves and active sessions. Stage builds separately.
- Offline tests and a successful engine build do not establish live gameplay.
- A scripted exit is durable within its map generation. Repeated requests,
  waiter death/disconnect, and competing walk-in exits must not discard it.
- Shared state follows the split-play specification. Code-note payloads must
  survive HUD resync and saves; raw client-only messages are insufficient.
- Save files must be validated before changing a running session. Ordinary
  single-player loading must reject co-op saves. Keep connections and restore
  each player once through the game's save/restore interface.
- Recover inventory from engine snapshots, not the incomplete retail co-op
  store. Preserve ordered slots, weapon state, ammo, documents and story state.

## Tasks

1. Persistent scripted exits: map classification, durable arbitration, death
   and disconnect handling, native regressions and shipped-map scan.
2. Guarded retail shared-state access: codes, digits, objectives, ending flags,
   document entries, bag loss and real message payload coverage.
3. Shared-state integration: command/message/frame hooks, late joins and HUD
   resync; independent native state and glue scenarios.
4. Player recovery: capture before respawn/disconnect, restore inventory and
   placement, deterministic identity matching and lifecycle tests.
5. Session save/load: validated durable archive, party-state sidecar, per-client
   restore tickets, transition autosaves and team-wipe recovery.
6. User controls, run reporting and documentation; full suite, x86 engine build,
   review, staged artifact and live validation where authorized and available.

## Interface review

- Tasks 1/5: pending exits reset only when a map generation actually changes;
  recovery cannot discard a scripted exit merely because its waiter dies.
- Tasks 2/3/4: retail helpers own layout knowledge; respawn is not a new story
  event and must not overwrite the shared ledger with reset player fields.
- Tasks 3/5: shared state and deliberate dropped-item marks require persistence.
- Tasks 4/5: reuse entity save tables and restore tickets for identity and
  inventory ownership; do not retain raw player pointers across map loads.
- Tasks 5/6: preflight and failures are observable; a failed save never replaces
  the last usable checkpoint and a refused load leaves the session running.

## Validation

Use failing native behavioral regressions before implementation, then focused
unittest modules. At integration boundaries run `python -m unittest discover
-s tests`, and build through `tools/build_enhanced.cmd`. Store logs and staged
artifacts under `evidence/story-state-recovery-20261012/`. Track completion and
deviations in that directory's progress file. Live checks must include shared
notes/codes, real scripted exits, respawn/rejoin, manual save/load and team wipe.

## Status

Tasks 1–5 implemented; task 6's documentation, reporting, native validation and
staged x86 build are complete. Full suite: 144 passing tests, followed by eight
passing focused tests for the final metadata guard. Both worktrees remain
uncommitted. No installed runtime or saves were changed and no games launched.
Live verification is the remaining gate, not an inferred result of these tests.
