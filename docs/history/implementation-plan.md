# Story co-op launcher and transition investigation

2026-09-27. Implements the user's supplied HANDOFF.md; all game changes stay in the C: development copy. The Steam install on D: is excluded. The user approved downloading Enhanced source in this session.

## Intended result

A local Python/Tk launcher with one-click host/join, chapter selection, real map exits, map checkpoint resume, RCON controls, server queries, address sharing and opt-in UDP UPnP mapping. Retail story scripting remains experimental until tested with multiple players. Checkpoints do not restore inventory, decisions, or hub state.

## Work and ownership

1. Main agent: archive existing patch; run stock and then patched intro without timed termination; record DLL hashes, elapsed time, process state, console and observed outcome. Investigate startup failures before drawing transition conclusions.
2. Core worker: game validation, non-destructive userconfig integration, launch arguments, settings/progress, chapter/map catalog, log tracking, regression tests. Never alter an active game's files.
3. Network worker: GoldSrc A2S/challenge RCON, IPv4 join codes, LAN/virtual LAN/public addresses, explicit UPnP mapping/unmapping, bounded protocol tests.
4. Enhanced worker: clone source separately, verify pins/build requirements and identify actual compatibility patches versus unimplemented story-co-op work.
5. Main agent: integrate Tk UI, command line entry points, desktop shortcut, inventory validation, tests, launcher smoke check, instructions and evidence update.

## Verification focus

- Preserve every existing user setting and the pristine manifest; hash the entire manifest inventory.
- Reject unsupported game directories and console-command injection; detect active instances.
- Never mistake a requested/refused CHANGE LEVEL for a completed map load.
- Parse actual exit links, including hub branches, without inventing a linear next chapter.
- Handle challenged, split, malformed and timed-out UDP replies with useful errors.
- A successful local server query does not establish external reachability or multi-player story correctness.
- No short automatic game kill; no external router changes during protocol unit tests.

## Current verification labels

VERIFIED means current file/process/protocol evidence. STATIC means source/binary inspection. REPORTED means upstream documentation or a user observation. Live campaign claims require live game evidence.
