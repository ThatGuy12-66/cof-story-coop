# Story state and checkpoints

This is experimental **source/staged-build functionality**, not a claim that the
installed runtime has been updated or that these paths have passed a live game.
The exact retail DLL hash and existing opt-in `cof_story_coop` gate remain required.

## Controls

- On the host console, `cof_save manual` saves a full checkpoint.
- `cof_load manual` restores it while keeping client connections.
- `cof_load` restores the latest accepted checkpoint in this session, or the
  last saved slot recorded on disk after restarting.
- Names for slots are 1–24 lowercase letters, digits, underscores or hyphens;
  Windows device names are refused. Default save slot: `manual`.
- Tape-recorder save requests from either player use separate `tape1` through
  `tape5` checkpoints. Only the host can request a load. The original retail
  tape animation/cost still runs; a failed checkpoint is reported separately.
- The launcher's **Resume map checkpoint** is still a map-only restart. To
  restore full state after closing the game, start story co-op, use the same
  maximum player count and player names, then run `cof_load <slot>`.

A manual save requires all connected human players to be spawned and alive,
with unique names, outside a map transition, and waits while a cutscene runs or
the ending door holds a player (their freeze must never be saved). Player identity across sessions is
the case-insensitive name, not the connection slot or transient userid. Renamed
players cannot claim their old inventory. Names are not authentication.

## Persistence

The engine snapshots the world, visited hub maps, global game state, full
connected players, owned weapons, shared notes/codes/objectives/ending flags,
bag-loss state, shared-item ledger and deliberate-drop marks. Missing players'
inventories are parked for a same-name rejoin. Late joiners get the current
party's story state rather than stale puzzle values.

Individual death/respawn and disconnect/rejoin use selective inventory
snapshots: ordered slots, weapon references and clips, ammo, quick slots,
documents and carried state. This does not restore a corpse's health, death
animation or position over a living respawn. Existing party placement handles
the spawn location. Consumed shared keys are reconciled before redelivery.

After a completed map transition, an autosave waits for the living party to
finish joining. A fresh server preserves an existing checkpoint instead of
overwriting it before the host has time to load. Save explicitly to start a new
checkpoint, or continue to the next map to establish a new autosave. A team wipe
requests the latest checkpoint before the retail restart timer; a missing or
invalid checkpoint leaves the retail restart fallback available.

## Files and failure handling

With the existing root-save compatibility enabled, files are under the runtime's
`SAVE/cofcoop/`; otherwise they are under the game's `save/cofcoop/`.

- `<slot>.cof`: versioned co-op archive, separate from ordinary `.sav` files.
  Format 2 (2026-10-12) adds the shared doctor-weapon choice and `cof_begingame`
  stats; checkpoints written by earlier builds are refused as "older build".
- `<slot>.cof.bak`: previous version retained on replacement.
- `latest.txt`: last saved slot; its replacement also keeps a backup.
- `load-before-*.HL?`: native map snapshots retained when staging a load.
- `*.next` / `load-next-*.HL?`: staging files, possibly retained after failure.

Archive size, checksum, member names, framing, native headers/token streams,
inventory records and player metadata are checked before scheduling a load.
Native map files are staged with read-back verification and rollback before
the current world is torn down. The ordinary single-player loader rejects a
co-op archive before clearing the running game. A malformed save or staging
failure is not silently converted into a new game.

An unexpected DLL restore failure after world teardown is still an engine
error, not an in-memory rollback. Treat checkpoint files as local trusted game
data, not a secure interchange format. The current format requires the same
retail DLL and maximum player count.

## Verification still requiring a game

Use a disposable copy of the runtime and saves, not the working play session:

1. Fire `landfitta` and `landkuk`; kill/disconnect the original waiter while
   the other player is away, then verify the exit remains usable.
2. Read a document and acquire multiple code notes; check both inventories,
   HUD resync, late join, keypad use, bag loss and ending flags.
3. Respawn and reconnect with deliberately rearranged slots, a partly used
   magazine, quick slots and a deliberately dropped story item.
4. Save, change the world and both inventories, then load. Verify positions,
   enemies, puzzle state, weapon presentation and exactly one sign-on per
   player. Repeat with reordered connection slots and one absent player.
5. Change map, wipe the team, and verify autosave recovery, normal movement,
   restored sound/fade and no stale retail game-over state.
6. Try a truncated copy of a checkpoint; the current game must remain running.

Offline native harnesses cover format/identity checks, actual retail map exit
classification, snapshot remapping, lifecycle policy, deliberate-drop
persistence and file-write/rename failure rollback. They do not execute the
retail game DLL or prove the live presentation and timing above.
