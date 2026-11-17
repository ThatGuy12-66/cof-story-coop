# Cry of Fear story co-op

Play the original single-player *Cry of Fear* campaign together, 2–4 players. The game is built
for one player: its scripts move "the player" through doors, give items to whoever touched a
trigger, and end the game when that one player dies. This project makes the campaign work with a
party, on top of [Cry of Fear: Enhanced](https://github.com/hajdawery/cof-enhanced) (the Xash3D
FWGS engine with Cry of Fear support).

> **Status:** public releases since 1.0 (October 2026); the latest is on the
> [releases page](https://github.com/charliee1w/cof-story-coop-releases/releases/latest). The whole
> campaign runs in co-op, with checkpoints, the story's set pieces, an AI companion for solo play and
> player tools. Later chapters have had less live play than the early ones, so expect some breakage.

## Players

Download the latest version from the
[releases page](https://github.com/charliee1w/cof-story-coop-releases/releases/latest) (also on
[ModDB](https://www.moddb.com/mods/cry-of-fear-story-co-op)). You need Windows 10 or 11 and Cry of Fear
from Steam (it's free). Extract the zip, run `CoF Story Co-op.exe` and click **Set up**: it makes a
separate co-op copy of your own Steam game, then plays solo with the AI companion, hosts friends or
joins them. [docs/playtesting.md](docs/playtesting.md) has the details. Report bugs on the
[Discord](https://discord.gg/4uwV6jmmY7) or as GitHub issues with the **Bug report** template.

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
| Launcher, tools and tests | this repository's root | The player app and host/test scripts (`tools/`), tests (`tests/`), research and docs (`docs/`), the retail-engine launcher (`launch.pyw`, `cofcoop/`) |
| Engine | `engine/` | Xash3D FWGS with the Cry of Fear: Enhanced patches and the story co-op modules |
| UI | `engine/3rdparty/mainui`, `engine/3rdparty/freevgui` | The engine's menu and VGUI with the Enhanced UI patches and the co-op menus |
| Releases | [`cof-story-coop-releases`](https://github.com/charliee1w/cof-story-coop-releases) | The release zips; the app checks it for updates |
| Cry of Fear: Enhanced | public [hajdawery/cof-enhanced](https://github.com/hajdawery/cof-enhanced), cloned to `upstream/cof-enhanced` at `e4cdbfa` | Prerequisites (SDL2) and its release scripts (packaging) |
| Local only (gitignored) | `baseline/`, `runtimes/`, `state/`, `evidence/`, `upstream/` | Game copies, private session state, test evidence, the Enhanced checkout |

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

This repository is licensed under the GNU General Public License v3.0 or later ([LICENSE](LICENSE)),
like Xash3D FWGS and Cry of Fear: Enhanced (see Enhanced's `LICENSING.md`). Parts of `engine/` keep
their own licences, as marked in their files: `engine/3rdparty/freevgui` is BSD-3-Clause, for example.
Every release zip also carries the engine's source in `source-and-licences`.

Cry of Fear Story Co-op is a fan project, not made or endorsed by Team Psykskallar, the makers of
Cry of Fear.
