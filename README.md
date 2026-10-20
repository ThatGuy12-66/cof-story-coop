# Cry of Fear story co-op

Play the original single-player *Cry of Fear* campaign together, 2–4 players. The game is built
for one player: its scripts move "the player" through doors, give items to whoever touched a
trigger, and end the game when that one player dies. This project makes the campaign work with a
party, on top of [Cry of Fear: Enhanced](https://github.com/hajdawery/cof-enhanced) (the Xash3D
FWGS engine with Cry of Fear support).

> **Status: early and experimental.** Two players have played live from the intro through
> `c_nightmare`, `c_start` and `c_start2`: cutscene skip, party teleports, level exits, shared
> starter items, respawns. Everything else (checkpoints, most of the story's set pieces, the AI
> companion) passes the offline test suite but has not been played live yet. Expect breakage.

## Playtesters

Start with [docs/playtesting.md](docs/playtesting.md): the tester zip (`CoF-Story-Coop-Test<N>.zip`)
holds one app, `CoF Story Co-op.exe`, that sets up a separate co-op copy of your own Steam game,
then plays solo with the AI companion, hosts friends or joins them. Report bugs as GitHub issues
with the **Bug report** template.

## Developers and helpers

- [docs/building.md](docs/building.md): getting all the sources, building the engine, running the tests.
- [AGENT_NOTES.md](AGENT_NOTES.md): the current state of the work, live results and next steps.
  It is written as a handoff for AI coding agents ([AGENTS.md](AGENTS.md)), and it is also the most
  up-to-date status for people.
- Tests: `python -m unittest discover -s tests` (Python tests plus native C harnesses that compile
  the engine's story co-op modules on their own).

## What the engine work does

All of it lives in the engine's `engine/server/sv_cof_*.c` modules, switched on by story co-op mode:

- **Level changes as a party.** Exits wait for the party or take everyone along; arrivals at
  landmarks; doors and scripted teleports bring partners along; `J` joins your partner.
- **Shared story state.** Keys, notes and story items count for everyone; scripted weapon gifts
  go to every player; weapons and items carry over between maps.
- **Checkpoints.** `cof_save` / `cof_load`, autosaves, and recovery after a team wipe, including
  each player's inventory ([docs/story-checkpoints.md](docs/story-checkpoints.md)).
- **Cutscenes.** Every player sees them; late joiners and revived players are handled; `Enter`
  skips a cutscene for the whole party.
- **Story moments.** Fall deaths, phone calls, ladders, the P345 choice, the ending door and the
  wheelchair ending, a respawn wait instead of game over
  ([research index](docs/research/2026-10-12-story-moments-re.md)).
- **AI companion** for playing alone, and a **test gun** with infinite ammo for testing the whole
  campaign ([docs/companion.md](docs/companion.md), [docs/campaign-test-guide.md](docs/campaign-test-guide.md)).

## How the pieces fit

| Piece | Where | What |
| --- | --- | --- |
| This repository | `cof-story-coop` | Host and test scripts (`tools/`), tests (`tests/`), research and docs (`docs/`), the retail-engine launcher (`launch.pyw`, `cofcoop/`) |
| Engine | `cof-story-coop-engine`, branch `story-coop` | Xash3D FWGS with the Cry of Fear: Enhanced patches and the story co-op modules; cloned to `upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a` |
| UI submodules | `cof-story-coop-freevgui`, `cof-story-coop-mainui` | The engine's `3rdparty/freevgui` and `3rdparty/mainui` with the Enhanced UI patches |
| Cry of Fear: Enhanced | public, `upstream/cof-enhanced` at `e4cdbfa` | Prerequisites (SDL2), packaging and the base the engine sits in |
| Local only (gitignored) | `baseline/`, `runtimes/`, `state/`, `evidence/`, `upstream/` | Game copies, private session state, test evidence, the nested clones |

Playtests run from the tester app: anyone with the zip can host, join or play solo. Building the
zip: `python -m tools.build_tester_package --out evidence\<new folder>` ([building](docs/building.md)).
Releasing a version (build, every check, ModDB, the public releases page the app updates from and the
Discord bot announces): `python -m tools.release build`, then `publish` ([releasing](docs/releasing.md)).
The older host script `HOST-STORY.cmd` (`tools/Start-StoryHost.ps1`, from `runtimes/enhanced`) is
still there. Testing alone with two windows on one PC: `tools/Start-CampaignTest.ps1`
([guide](docs/campaign-test-guide.md)).

The older launcher for the retail engine (host/join UI, UPnP, RCON map controls, the reversible
DLL patch) is described in [docs/launcher.md](docs/launcher.md).

## Docs

- [docs/playtesting.md](docs/playtesting.md), [docs/building.md](docs/building.md)
- [docs/campaign-test-guide.md](docs/campaign-test-guide.md): the live test checklist
- [docs/story-checkpoints.md](docs/story-checkpoints.md), [docs/companion.md](docs/companion.md)
- [docs/superpowers/specs/2026-09-30-coop-split-play-design.md](docs/superpowers/specs/2026-09-30-coop-split-play-design.md): the design the work follows; plans in `docs/superpowers/plans/`
- `docs/research/`: static reverse engineering of the retail game DLL and all 81 story maps
- `docs/history/`: older handoffs and session logs (historical; newer facts are in `AGENT_NOTES.md`)

## Game files and licences

Every player uses their own copy of Cry of Fear from Steam (it is free). This project never
redistributes game binaries or assets.

The engine is GPL-3.0-or-later, like Xash3D FWGS and Cry of Fear: Enhanced (see Enhanced's
`LICENSING.md`). Anyone who receives a build is entitled to its source; invited testers can read
the engine repository.
