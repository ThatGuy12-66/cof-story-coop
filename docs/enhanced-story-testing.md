# Isolated Enhanced story compatibility tests

Current workflow: one local host and the remote friend on their own computer. The user asked for no further two-window local tests. Two actual clients have joined successfully; the transition fixes were built and passed native regression tests, while complete remote story progression remains pending. The diagnostic fixture below is retained for development evidence and is not installed for the current Chapter 1 session. Enhanced hosts speak protocol 49, so both test clients need Enhanced over their own game assets. A stock retail protocol-48 client is not a valid second client for this test.

## Build and package

The existing x86 Waf configuration is reused by:

```powershell
cmd /c tools\build_enhanced.cmd
```

This loads VS 2022's `vcvarsall.bat amd64_x86`, selects `.lock-waf-local-20260927`, and incrementally builds `xash,vgui,ref_gl,menu`. It does not configure, install, or launch. The configured engine tree is `upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a`, output `build-local-20260927`. Python is `py -3.14`. The original full configure driver remains `upstream/cof-enhanced/build-local-20260927/build.cmd` if configuration must change.

After engine edits have stopped and the build has finished, inspect then explicitly create a package:

```powershell
python tools/package_enhanced_story.py
python tools/package_enhanced_story.py --write
```

Choose a fresh `--out` below project `evidence` for another attempt. The script stages the newly built four DLLs plus the previously verified launcher/filesystem/SDL binaries, then runs the unchanged upstream installer-package builder. It refuses missing story/party sources, an engine DLL older than engine source, source changes during packaging, and archive/hash mismatches. A timestamp guard is not proof that an arbitrary binary was built from the matching source; run the supplied build immediately before packaging with no concurrent source edits.

The output includes the installer ZIP, complete modified engine source ZIP, source hashes, local build/test tools, archive checksums, and an experimental-build notice. The engine source archive is checked for the actual story/party module bytes. Retail `hl.dll`, `client.dll`, BSPs, WADs, models, saves, and private test overrides are excluded. No package is installed or published by this script. Keep the corresponding source archive with any shared experimental binary. The upstream version remains `0.4.0-test3`; this is a local modified build, not an upstream release.

## Private Simon models

```powershell
python tools/prepare_simon_models.py --game-dir "C:\path\to\Cry of Fear"
python tools/prepare_simon_models.py --game-dir "C:\path\to\Cry of Fear" --write
```

The source generator reads each user's own hash-pinned retail assets and writes only to a new project evidence directory (default `evidence/simon-models-v2`). Choose another fresh `--out` for a repeat. It generates `models/player/cof_s1/cof_s1.mdl` and `models/player/cof_s2/cof_s2.mdl`; copy that relative layout into each isolated client's `cryoffear` directory before a controlled launch. The generated models and extracted PNG previews are excluded from the distributable archives. The generator and its Python dependencies are included in the source archive under `local-story-tools`.

The network names are deliberately six characters. The earlier `cof_simon_alt` name exposed an engine userinfo boundary bug: an exactly full 256-byte string's terminating NUL overwrote a stack cookie during the second-client test. The crash dump identified that string-boundary fault; offline model inspection found no geometry or animation overflow. Short names preserve room for existing userinfo fields, but are not a substitute for the engine's boundary and atomic-update fixes. The earlier assets remain under `evidence/simon-models` as historical evidence; the new files change only their embedded model names, retaining the validated geometry, textures, and sequence data.

Both variants use the real walking Simon geometry, 36 bones, and animations from `models/cutscene/player.mdl`. A new 85-entry sequence table follows the native police model's sequence labels and order, with explicit mappings to Simon animations. Original animation, event, mesh, and bone data retain their offsets; the obsolete transition table is disabled. Validation walks every compressed animation channel and checks texture, skin, sequence, and event bounds. Reload, CPR, and door actions have documented pose or movement fallbacks in `model-report.json`; they are not exact animation equivalents.

The alternate uses the stock Sick Simon face, hoodie, and jeans textures on the walking model. Matching texture dimensions and extracted UV templates have been checked; gloves, hair, bag, and shoes remain unchanged. No wheelchair animation model is used. Both model variants still require actual two-client rendering, movement, weapon, and door observations before claiming visual compatibility for the two little rascals.

## Deterministic two-map fixture

```powershell
python tools/story_fixture.py
python tools/story_fixture.py --write
```

Only the second command writes, and only to a new project evidence directory (default `evidence/story-fixture-v2`). It reads the hash-pinned original `c_start.bsp` and `c_start2.bsp`, retains each exact worldspawn (including duplicate keys and its WAD list), and generates private `.ent` overrides. It never changes either BSP. Install those overrides and `fixture.cfg` only in a separate Enhanced host copy when that copy is not running; preserve any existing overrides instead of replacing them. The original retail DLL belongs in that copy; do not carry over the retail null-landmark experiment.

The fixture isolates the real `c_start`/`c_start2` geometry and bidirectional `hahaland` edge from cinematic and enemy timing. Version 2 deliberately replaces the landmark coordinates and uses playable per-map spawn positions:

| Map | First spawn | Second spawn | Synthetic `hahaland` |
| --- | --- | --- | --- |
| `c_start` | `(639,-2953,-728)` | `(639,-2913,-728)` | `(635,-2978,-694)` |
| `c_start2` | `(2428,-883,-728)` | `(2428,-843,-728)` | `(2424,-908,-694)` |

The outbound landmark translation is `(+1789,+2070,0)` and the return is its inverse. The two recommended positions are 40 units apart along Y; both standing hulls are empty in both original BSPs. The generator also checks that each spawn leaf has non-black surfaces. Player-to-slot assignment is observed, not assumed: compare each stable userid's actual departure coordinates with its arrival after applying the landmark delta.

Version 1 incorrectly reused the `c_start2` spawn in `c_start`, inside a sealed room whose six faces use a black texture. This explains the black fixture view without establishing a renderer failure. The original private artifacts are retained as superseded evidence; the verifier requires version 2 landmark coordinates and rejects the old shared-coordinate fixture.

Named entities:

| Name | Role |
| --- | --- |
| `cof_probe_once` | `trigger_once` on `c_start`; consume it before leaving and prove it stays absent on return |
| `cof_probe_grants` | Short manager called by that trigger; no cameras or teleports |
| `cof_probe_mobile`, `cof_probe_knife` | Actual `cof_giveitems` for phone and knife |
| `cof_probe_marker` | Persistent `info_target`, initial health 17; change to 73 before leaving |
| `cof_probe_exit` | Use-only actual `trigger_changelevel`, opposite map, landmark `hahaland` |

The exit has no automatic timer. Wait until both actual clients are connected and evidence is captured. In the **Enhanced host client's console**, the existing entity tool supplies that player as activator:

```text
ent_fire cof_probe_once touch
ent_fire cof_probe_marker health 73
ent_fire cof_probe_exit use
```

These are isolated-fixture client commands, not retail commands or RCON commands. `SV_EntFire_f` invokes the DLL's Touch/Use with `cl->edict`; a null activator is unsafe for retail `cof_giveitems`. Run the first two commands on the initial `c_start`, allow the grant manager and trigger removal to finish, then capture the pre-exit state before using the exit. On `c_start2`, capture arrival and the return departure before using its `cof_probe_exit`. No original story cutscene is replayed.

Before departure, both players must actually hold phone and knife. Add a distinct harmless test inventory marker to each client using Enhanced's client-side `give` command with `sv_cheats 1` (for example a Glock for client A and a flare for client B), then confirm the private inventory observations. Distinct markers distinguish correct per-user restoration from copying the host's inventory to both clients. Keep the clients stationary and separated during departure/arrival capture. Do not use the fixture's grant a second time to conceal a failed restore.

## Remote-friend setup and one host window

> Historical (2026-09-27). Since 2026-10-15 testers use the tester app (`CoF-Story-Coop-Test<N>.zip`,
> [playtesting.md](playtesting.md)): setup, Play solo, Host and Join in one exe.

The distributable is `CoF-Story-Tester-v4.zip`. The remote friend extracts the complete ZIP and opens `CoF-Story-Setup.exe`; no Python installation is required. Steam metadata supplies the source folder when available, with Browse as fallback. Setup creates a fresh separate game copy, verifies the stock game DLL, retains campaign maps and `skill.cfg`, installs the matching Enhanced payload, generates both Simon models, and verifies their hashes. It excludes personal saves, logs, settings, and existing entity overrides.

After setup, the window asks for the address and password from the separate private invitation, with the friend's player name. **Join host** opens one 960×540 window capped at 60 FPS. Subsequent joins use `JOIN.cmd` in the separate copy. The current host runs ordinary `c_start` campaign data, without fixture overrides. Both players need the same protocol-49 build. Host credentials and invitation files are not included in the bundle or corresponding source.

There will be one game on this machine and one on the remote friend's. A dedicated host or fake client can exercise server code, but cannot replace observations of actual networking, controls, visibility, inventory presentation, and progression. The offline native party, userinfo, and client-transition harnesses remain available without launching any game.

`tools/enhanced_local_control.py` accepts `--session local` for the old private loopback fixture and `--session remote` for the current local host. Both use unshared local state files; never copy those files into a public bundle. Redact raw engine logs before sharing because they may contain console credentials.

## State capture and verification

Capture a complete `cof_story_status` response at each phase, preserving actual output:

1. `initial`: both connected in `c_start`, before consuming the sentinel. It exists and marker health is 17.
2. `before_outbound`: still `c_start`, sentinel absent, marker health 73, both inventories prepared.
3. `after_outbound`: `c_start2`, both fully signed on, before either moves.
4. `before_return`: same `c_start2` generation, immediately before using the return exit.
5. `after_return`: `c_start`, both signed on; sentinel still absent and marker health still 73.

```powershell
python tools/verify_story_fixture.py --logs initial.log before-outbound.log after-outbound.log before-return.log after-return.log
```

The parser consumes `cofstory` key-value records. A status block must end with `phase=status-end`. Header fields are `enabled`, `map`, `generation`; player fields are `slot`, `userid`, `ready`, `origin=x,y,z`, `door`, `hidden`, `health`, `fakeclient`, `frozen`. Slots are zero-based; userids are positive stable identities. Actual private inventory records use `phase=inventory-slot slot=... userid=... index=... descriptor="..."`; all seven indices (0 through 6) must be observed for each player. Index 0 is unused; usable slots are 1 through 6, and empty slots have `descriptor=""`. Nonempty descriptors are canonical `inventoryitems/.../*.txt` paths, not entity classnames. The separate owned-edict inventory diagnostic is deliberately not used as a substitute. Landmark records use `phase=landmark name=hahaland origin=x,y,z`. Fixture records report actual `name`, `class`, and `health`; absence is meaningful only in a complete dump.

The verifier requires two distinct stable userids, two real ready clients, actual phone/knife inventory, preservation of each complete distinct inventory and health across both transitions, landmark-relative arrival within eight units, no hidden/frozen/door flags, consumption of the original trigger, and restoration of marker health 73. It fails incomplete evidence. `cofstory` transition phase logs and A2S map names establish ordering and responsiveness, but cannot make this test pass alone.

This fixture is one repeatable compatibility slice. It does not certify all story doors, save/resume, late joins, camera control, inventory UI, combat, endings, or the full campaign.
