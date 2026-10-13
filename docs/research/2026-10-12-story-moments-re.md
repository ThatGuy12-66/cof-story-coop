# Story moments and endings with two players: research index, 2026-10-12

Static research only, against the stock `hl.dll` (SHA-256 `0036B91C…0E63`) and the entity lumps of
the 81 story maps reachable from `c_intro`. Nothing was launched. It covers pieces 2 (story moments)
and 4 (polish) of `docs/superpowers/specs/2026-09-30-coop-split-play-design.md`. The engine work it
led to is listed at the end. Paths under `scratchpad/` in the reports are this session's temporary
working copies; the reusable disassembly helper is `tools/hlre.py`.

| Report | Scope |
|---|---|
| [Cutscenes, cameras, screen effects](2026-10-12-cutscenes-re.md) | `trigger_camera`, `cof_mdlcutscene`, look-at, zoom, fades, blur, telescope, strangle, music |
| [Endings, P345, phones, branches](2026-10-12-endings-phone-re.md) | `inter_door` ending, `cof_ending`/`cof_credits`, doctor weapon, `cof_yesno`, `cof_phonecall`, `cof_killplayer` |
| [Player state, puzzles, vehicles](2026-10-12-player-state-re.md) | ladders, wheelchair, puzzle bars, wardrobe, `cof_giveitems`, `cof_teleport`, triggers |
| [Entity-chain audit of the 81 maps](2026-10-12-map-chains-re.md) | who fires what, hazard lists, hand-traced ending and P345 chains |

## Facts that changed the plan

- **Cutscenes are already shared.** The retail `trigger_camera` start loop (Use `10113B60`,
  `1011420B`) calls `SET_VIEW`, sets `FL_NOTARGET`, takedamage 0 and (spawnflag 4) `FL_FROZEN` on
  every player. Only its end loop (`1010B267`) skips dead players. Mirroring cameras in the engine
  would fight the game; a watchdog for the cases it leaves wrong is what is needed.
- **`multi_manager` keeps the activator** (`m_hActivator` +0x184) and runs delay-0 targets
  synchronously inside the triggering call (`1010E555`). A chain started by a player's command or
  touch reaches "first player" entities while that player is the engine's command sender.
- **`cof_teleport` never uses the first player**: it moves its activator or its `message` entity.
- **The game's `ClientKill` (`1001CEF0`) kills one player** through the normal death path, so a
  fall-death floor can be limited to whoever fell.
- **`cof_killplayer` kills every connected player** (`100A42C0`). Five of six story instances are
  fall-death floors behind one `trigger_once`.
- **`cof_phonecall` drops a call for good** when its recipient has no phone, dies or puts it away,
  with no timeout; `m_bInPhoneCall` then stays set and the game ignores every command from that
  player (`100190BE`).
- **The doctor weapon (`P+0x5B0`) is written to "the first player"**, which is the guest when the
  guest answers the P345 question, and read from the first player by the `c_attic` doctor, who
  never fights while it is 0.

## Implemented in the engine (2026-10-12, offline-tested, not live-tested)

| Hazard | Where | Treatment |
|---|---|---|
| Revived after a scene: frozen, invulnerable, dead camera | `sv_cof_scene.c` | release a living viewer of an ended camera |
| Respawn/rejoin mid-scene: free, damageable | `sv_cof_scene.c` | hold like everyone (freeze, no target, no damage) |
| Telescope switched off under its user | `sv_cof_scene.c` | the game's own `telescopecancel` for that user |
| Late joiner black after a model cutscene | `sv_cof_scene.c` | fade in when the wait byte clears |
| Respawn held black and frozen by a ladder's or the strangle's camera model (MP PlayerSpawn `100C948D` starts the model-cutscene wait for any camera model) | `sv_cof_scene.c` | lift the wait while no `cof_mdlcutscene` runs |
| Camera freeze carried through a map change or saved | `sv_save.c`, `sv_cof_session.inc` | release before the transition save; co-op saves wait for running cameras |
| Scripted teleport leaves a watching partner | `sv_cof_party.c` | a partner viewing the same running camera travels along |
| Wardrobe and other first-player effects of a guest's +use/touch | `sv_pmove.c` | the acting player is "the first player" while their command runs |
| Fall-death floor wipes the team | `sv_cof_moments.c`, `sv_world.c` | only the toucher dies (`ClientKill`); the floor stays armed |
| Phone call to a player without a phone, or dropped | `sv_cof_moments.c` | ring someone who can answer; restart; fire its target once after 30 s |
| Dead player keeps `m_bInPhoneCall` | `sv_cof_moments.c` | clear the call fields |
| Ladder hijack (climber stuck forever) | `sv_cof_moments.c`, `sv_pmove.c` | no +use near a ladder someone is climbing |
| Wheelchair stop/teleport hit the host, not the rider | `sv_cof_moments.c`, `sv_game.c` | the rider is the preferred first player |
| Disconnect leaves raw pointers (ladder, chair) | `sv_cof_moments.c` | clear them on drop; a climber's camera model goes too |
| Yes/no answered twice or by someone not asked | `sv_cof_moments.c` | only a prompted player answers; the first answer wins |
| Doctor weapon on the wrong player (`c_attic` soft-lock) | `sv_cof_state_data.h`, `sv_cof_retail.c` | shared story value (checkpoint format 2; older checkpoints are refused as "older build") |
| Late joiner credits without an ending | same | `cof_begingame` stats block shared |
| Ending door moves only its opener | `sv_cof_party.c` | everyone, blacked out and held, lands in the opener's ending room |
| Scripted weapon gifts to one player | `sv_cof_party.c` | glock, flashlight, camera, shotgun shared like the starter kit |
| Dead for a minute although respawn keeps items | `sv_cof_party.c` | `cof_story_respawn_delay` (default 10 s, retail 60) |

## Known and not addressed

- `cof_lookat`, `cof_greyfade`, `cof_logo`, `cof_camerazoom`, `cof_blur` reached through delayed
  chains still act on the host or the activator only (cosmetic).
- `c_asylumb` puzzle bars: a guest who solves it keeps the puzzle view until they press the UI's
  close button; the host receives a stray view reset.
- `cof_strangle` (`c_ending1c/2c/3c`) grabs the host; only the host's input finishes it.
- `cof_entityrestore` (`c_college1` → `c_college1dark`): dropped weapons belong to whoever touched
  the save trigger last.
- `cof_phonedisable`, `cof_hoodie`, `cof_maxhealthchange`, the boss health bar after a respawn and
  `cof_telephone` with two users are per-player desyncs.
- The intro cannot be skipped in multiplayer (the retail skip reads the server's keyboard).

The open questions for a live test are at the end of each report; the most important are collected
in `AGENT_NOTES.md`.
