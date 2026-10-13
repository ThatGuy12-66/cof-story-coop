# Shared agent instructions

These instructions apply to this entire project, including its nested engine checkout.

## Required startup

1. Read `AGENT_NOTES.md` before planning, changing code, building, deploying, or testing.
2. Use its current-status section before older handoffs, chat summaries, or cached memories. Verify facts that can change, especially processes, branches, hashes, and installed builds.
3. Read the referenced task-specific documents when needed; do not assume historical live results validate newer code.
4. Give delegated agents the project root and require them to read `AGENT_NOTES.md` too.

## Required handoff

Update `AGENT_NOTES.md` after substantive project work or an interrupted test. Keep it as the single current handoff rather than creating another competing notes file. Include:

- Changes and their source locations.
- Commands actually run and evidence paths.
- Offline tests, builds, deployment, and live behavior as separate verification states.
- Remaining work, blockers, and the smallest next validation step.
- Any processes left running, explicitly labeled as a dated observation.

Never record credentials or raw private session state. Preserve older evidence and unrelated work. Do not commit, merge, deploy to playing copies, launch games, or terminate user sessions merely because these notes describe how.

## Scope and safety

- Work only on the requested task; preserve existing uncommitted work in both repositories.
- Do not touch `D:\SteamLibrary\steamapps\common\Cry of Fear`.
- Use isolated runtime/save copies for experimental live tests; do not replace normal runtime engines without explicit authorization.
- Read the installed computer-use skill before native UI automation; respect a user stop immediately.
- Keep scripts, logs, and temporary inspection artifacts under scoped project evidence, never the home-directory root.
