# Cry of Fear: Enhanced source assessment

Date: 2026-09-27. Scope: download and inspect the public compatibility project and its pinned engine, prepare a project-local build, and identify the work needed for the **original single-player story in 2-4 player co-op**. The later authorized preparation of a separate local runtime is recorded in the appendix. No game was launched; the baseline and D: Steam installation were not modified.

Evidence labels: **VERIFIED** means checked on this machine, **STATIC** means read from downloaded source, **REPORTED** means stated by upstream or the existing handoff, and **PROPOSED** means future engineering work.

## Result

**STATIC:** Enhanced is a useful retail-ABI compatibility foundation, not an implementation of persistent story co-op. Its Host page's **Story co-op** option starts `cof_campaign_01`, the game's separate built-in co-op campaign. It does not start `c_intro`. The engine still disables smooth landmark transitions whenever there are multiple clients, and normal multiplayer saves are rejected.

**STATIC:** An engine swap alone therefore does not resolve the stock `hl.dll` assumptions. The long-term implementation needs engine transition/save changes **and** explicit game-specific behavior for player selection, story scripts, inventory, doors, teleport/cutscene state, and skip handling. Those game-specific behaviors could live in carefully validated compatibility hooks around the retail DLL; they are not supplied by Enhanced today.

## Downloaded source and reproducibility

**VERIFIED:** The parent repository is a clean clone at:

`upstream\cof-enhanced`

- Remote: <https://github.com/hajdawery/cof-enhanced>
- HEAD: `e4cdbfaa260696eb886cf6c9bd4c371d53377427`
- Commit: `Release 0.4.0-test3: remake HUD and controller improvements`, 2026-09-27T18:54:20+02:00.
- `VERSION`: `0.4.0-test3`, milestone `m10`. The checkout contained only tag `v0.4.0-test1`; version-file status should not be confused with a locally verified test3 release tag.
- The official README was checked online on the audit date and also identifies `0.4.0-test3` as a public test release.
- `git fsck --no-progress` passed. `git lfs ls-files` returned no LFS-tracked files; `git lfs fsck` passed for both the Enhanced and engine repositories, and the source/data scan found no LFS pointer payloads.
- No repo-local `AGENTS.md` was found. The existing `HANDOFF.md`, the user's applicable global `AGENTS.md`, and upstream build/testing instructions were inspected.

The Enhanced repository contains patches/scripts/data; its engine is not a submodule of that repository. The engine and dependencies below were fetched into the ignored directory `xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a` inside that checkout. Each dependency uses its own Git metadata and detached pinned commit. The engine is intentionally modified by applying the unmodified Enhanced patch stack; these are upstream compatibility changes, not new story-co-op code.

| Component | Downloaded commit |
| --- | --- |
| Xash3D FWGS | `4857b389e6ba32ddaa68582aedcbc950c138f46a` |
| MainUI | `61263995592e93a278d764807243d043d3b97c54` |
| MiniUTL | `371731e8b5ee63173e0b8c0cdc0ea2e0851a81da` |
| FreeVGUI | `73bfb4659f3d9eb28b79e5b26baea8c6b888d5eb` |
| Opus | `503d81b138d76621aae4b12786e90de48aa8db3a` |
| opusfile | `6dfd29e7adb87f2e193575fc3fa88cbf1a0b27df` |
| libogg | `06a5e0262cdc28aa4ae6797627a783b5010440f0` |
| libvorbis | `1b75110b5a2754ba1931d82dd83cb822b266a21d` |
| bzip2 | `66c46b8c9436613fd81bc5d03f63a61933a4dcc3` |
| MultiEmulator | `e369abc23c49310b0f5e58f12fef6f46563b0d21` |
| library-suffix | `14fc7cd5b1e637d8fe722daa977e72eac9244e45` |
| xash-extras (build graph requirement, not linked into engine DLL) | `9f5b017985981e5eadd1813c0cde1e4f217e12e1` |

The first eleven rows match `LICENSING.md` section 3's **Used for our build** column, which deliberately differs from several engine gitlink pins. xash-extras uses the engine's own gitlink. It was needed because waf visits the extras zip task even with the four DLL targets selected: the first attempt configured successfully but stopped with `No files to archive` while that checkout was empty. No engine source was changed to avoid this requirement.

**VERIFIED:** SDL2 was downloaded from its official `release-2.30.9` release into local `prereq/`, unpacked locally, and checked against upstream's recorded SHA-256:

`8C91D91E5BCB997D062EC2B553C53832EBF95654D4AA35E8C02A954D4CE752AE`

The complete inventory of the 77 patch names, hashes, target files, and source dependency pins is recorded in `upstream/cof-enhanced/build-local-20260927/source-inventory.json`. The authoritative order is upstream `docs/dev/patch-stack.md`, steps 0-76. **VERIFIED:** all 77 official apply scripts completed, including their prerequisite and reverse-apply checks, and both generated headers were produced. The version stamp is `cofenhanced 0.4.0-test3 (m10, e4cdbfa)`.

## What the source actually implements

Paths below are relative to the Enhanced repository unless prefixed with **engine tree**. Line numbers in engine-tree references are from the locally patched tree.

| Requested capability | Source evidence and current result |
| --- | --- |
| Run current retail DLLs on Xash | **STATIC:** `cof-entvars-legacy`, `cof-edict-stride-legacy`, `cof-pmove-legacy`, `cof-client-pmove-legacy`, and `cof-pmove-callback-view` implement ABI/movement compatibility. Build requires x86 and `--enable-cof-entvars-legacy`. This is the principal foundation to retain. |
| Host and join built-in co-op | **STATIC:** engine tree `3rdparty/mainui/menus/CoFCoop.cpp:207` maps Story co-op to `cof_campaign_01`; `docs/design/coop-bridge.md` documents `deathmatch 1`, `coop 0`, 2-4 slots, player name, password, LAN selector, and address-based join. `cof-coop-bridge.patch` supplies return-to-menu behavior, lobby-caption clipping and cfg newline fixes. |
| Original story-map transition | **STATIC:** engine tree `engine/server/sv_game.c:708`, `SV_QueueChangeLevel`, still forces `smooth = false` at lines 745-746 for `svs.maxclients > 1`, then drops the landmark when queuing the classic transition. **REPORTED:** `HANDOFF.md` separately identifies a potential earlier multiplayer return inside retail `CChangeLevel::ChangeLevelNow`; this audit did not re-disassemble or runtime-verify that DLL claim. |
| Landmark arrival and per-player carry-over | **STATIC:** engine tree `engine/server/sv_save.c`, `SaveGameState` at 1652, `CreateEntityTransitionList` at 2017, `LoadAdjacentEnts` at 2112, and `SV_ChangeLevel` contain reusable single-player infrastructure. `SaveGameState(true)` / `LoadAdjacentEnts` run on the smooth path. No Enhanced patch generalizes those semantics to story co-op. Removing one gate is insufficient proof of correct player identity, private-data restoration, camera state or inventory. |
| Persistent hub revisits | **STATIC:** the classic path in engine tree `engine/server/sv_save.c:2294` resets DLL global state and spawns map entities anew; saved-level restoration is on the smooth path. The existing root-SAVE patch changes file placement and compatibility, not multiplayer persistence. |
| Save/resume a co-op story session | **STATIC:** engine tree `engine/server/sv_save.c:736` still says `Can't save multiplayer games.`; the check rejects `svs.maxclients != 1`. Host/client identity, join-in-progress and full-session restore remain work. A launcher remembering a map name is not a persistent co-op save. |
| Skip a running story cutscene in multiplayer | **STATIC:** no `cof_skipmanager` / `CCofSkipManager` adaptation was found in the complete patch/script/source sweep. The existing New Game **Skip prologue** feature simply chooses `c_nightmare` instead of `c_intro`: engine tree `3rdparty/mainui/menus/CryOfFear.cpp:140` and its new-game path near 299. It does not implement multiplayer skip consensus or restore in-progress story state. |
| Invisible teammate recovery | **STATIC:** no general teammate fix found. `cof-custom-renderfx-opaque.patch` fixes skyline/draw-list classification, not a proven player-visibility problem. A relevant lead exists in `docs/design/cheats.md:146`: upstream's retail analysis says `b_GoingThroughADoor` at player-private offset `+0x1EA4` makes multiplayer `PostThink` (`0x100EBD75`) set `EF_NODRAW`. The implemented `cof_nodamage` command refuses co-op because it borrows that flag; see engine tree `engine/server/sv_cof_cheats.c:1106`. This is a diagnostic hypothesis for door/teleport invisibility, not evidence that all invisibility shares that cause. |
| Group item handouts and story entity fan-out | **STATIC:** no implementation found for applying `cof_giveitems`, `cof_clearitems`, `cof_teleport`, `cof_phonecall`, `cof_killplayer`, or story-ending actions to a selected group. The cheat `give` implementation creates/uses a requested item for one client; it is not a campaign item policy. |
| Modern P2P/relay transport | **STATIC:** no linked GameNetworkingSockets or SteamNetworkingSockets transport found. Xash retains UDP/netchan and master-mediated NAT functionality: engine tree `engine/server/sv_main.c:24` (`sv_nat`), `sv_client.c` (`SV_ConnectNatClient`), `engine/common/masterlist.c`, `net_ws.c`, `net_chan.c`. A `GameNetworkingSockets` mention in `net_ws.c` only credits imported address-validation checks. This is not a GNS integration. Enhanced's own bridge does not add relay service. |

**REPORTED, not reproduced here:** upstream tested two copies over loopback and expressly lists second-machine LAN discovery, Internet/NAT, packet loss/latency and extended campaign progression as untested. Its README also says the entire game has not been completed on this engine. Upstream ignored `stage1/` evidence paths are not in this clone; their described outcomes should remain reported evidence.

## Build and licensing

**VERIFIED:** existing tools discovered without installation: VS 2022 Build Tools `17.14.37411.7`, x86 C++ toolchain, Windows SDK `10.0.26100.0`, and Python `3.14.6` via `py -3.14` (plain `python` separately reports `3.13.11`). No global tool/runtime settings were changed.

Reproducible local driver: `upstream/cof-enhanced/build-local-20260927/build.cmd`. It loads the installed `vcvarsall.bat amd64_x86`, uses `WAFLOCK=.lock-waf-local-20260927`, and executes:

```text
py -3.14 waf configure -4 --out=build-local-20260927 --sdl2=..\prereq\sdl2-2.30.9-vc\SDL2-2.30.9 -T release --notests --disable-mbedtls --enable-cof-entvars-legacy --enable-stbtt --msvc_version="msvc 17.14" --msvc_targets=x86
py -3.14 waf build -j8 --targets=xash,vgui,ref_gl,menu
```

**VERIFIED:** configuration and the full requested build succeeded with exit code 0 (530 tasks; final build elapsed 32.848 seconds). The final configure took 15.628 seconds. Each DLL's PE machine field was independently checked as `0x014C` (x86). Outputs are below, relative to the engine tree's `build-local-20260927/` directory:

| Output | Bytes | SHA-256 |
| --- | ---: | --- |
| `engine/xash.dll` | 3,707,392 | `3FF6076595050E0146C66D9EBFCC5A7A4EADE394F43C36F70704287F82574A76` |
| `ref/gl/ref_gl.dll` | 1,104,384 | `6CFD8DFEEFF335FD86607561B9559243790948E4E2A970CBDACB65EEEF3354C5` |
| `3rdparty/freevgui/vgui.dll` | 1,062,400 | `F77963A01A6D126488315F7DC2FE5C3C37D161D1C94147BA42747BC25B431BAF` |
| `3rdparty/mainui/menu.dll` | 1,577,984 | `D6F43D18CFD3B77010AF9ABAF22B396BA21E3B7C9BC6855C80FF217DCB9C67A1` |

The build emitted six C5287 enum-comparison warnings in sprite code and two C4333 right-shift warnings in `img_png.c`; these upstream warnings were preserved, not silently treated as tested behavior. No compile/link errors remained. Recorded evidence in the Enhanced repository's `build-local-20260927/` folder: `build.log`, `build-attempt1-missing-extras.log`, `apply-stack.log`, `source-inventory.json`, `build-artifacts.json`, and the repeatable `build.cmd` driver. The outer Enhanced Git checkout remains clean; downloaded and built material uses its existing ignored paths.

This verifies that the inspected engine/menu source can be compiled on this machine. It does not verify original story progression, rendering, multiplayer connections, saves or any proposed story fix. At this source-build checkpoint the installer had not been run and no runtime game files had been replaced. The subsequent isolated preparation is below; no game process has been launched.

**STATIC, upstream license declarations:** project patches/scripts/launcher and engine work are GPL-3.0-or-later; FreeVGUI changes retain BSD-3-Clause, Valve SDK headers retain their own notices, Inter fonts use SIL OFL, and artwork/translations have separate terms. Preserve `LICENSE`, `LICENSING.md`, `THIRD-PARTY-NOTICES.md`, and component notices. Upstream documents exact source/pin/build information accompanying binary releases. Downloaded engine source and local build products are separate from the user's proprietary game content; no retail DLL/assets were downloaded, copied into this checkout, or redistributed during this work.

## Concrete next engineering scope

1. **PROPOSED: establish a reproducible two-client fixture.** Keep stock and Enhanced test runtimes separate, generated from the user's C: copy. Record retail DLL hashes, all four Enhanced DLL hashes, launch args, save/config snapshots, and map logs. Announce every game launch and preserve active user sessions. Complete one unpatched two-client built-in co-op control and one original-story control before adding behavior. The parent task's stock intro test is separate from this source audit.
2. **PROPOSED: add observation before changing story rules.** A narrow story-co-op module should log per-client transition requests, player slot/edict identity, health/inventory, `b_GoingThroughADoor`, `EF_NODRAW`, camera/view entity and teleports. Use the known-DLL SHA-256 and private-data checks in `sv_cof_cheats.c` as a pattern for compatibility validation, rather than applying offsets to arbitrary binaries. A previously patched `hl.dll` will not match Enhanced's existing retail hash; account for patch identity explicitly.
3. **PROPOSED: implement one complete transition slice.** Resolve the retail game-rule exit gate, then introduce story-only smooth multiplayer transitions at `SV_QueueChangeLevel` and `SV_ChangeLevel`. Preserve each player's state and identity, apply landmark offsets, separate persistent world state from live client connections, reconnect/re-sign-on all clients, and release every player from cutscene/door hiding. Do not present simply deleting the multiplayer guard as a finished solution.
4. **PROPOSED: prove hub persistence.** Test a bidirectional story edge from the complete map inventory, such as a real `c_city` connection. Record a killed monster, unlocked door, consumed/persistent item and global trigger before leaving; verify expected state after return with two then four clients. Make storage session-scoped and atomic so a failed transition can retain the previous snapshot. Only then extend checkpoint save/load and reconnect/join-in-progress.
5. **PROPOSED: resolve story-entity semantics systematically.** Enumerate every occurrence in the already scanned 235 BSPs and reachable story graph. Add group behavior for grant/clear/teleport/kill/phone/camera/ending entities, non-overlapping arrival slots, missing/dead/disconnected players, and recovery from interrupted transitions. Decide who owns ending choices and key items before baking those semantics into the format.
6. **PROPOSED: fix skip and visibility from observations.** Trace `CCofSkipManager` dispatch and its multiplayer checks, define host-authorized or voted skipping, and restore camera, controls and visibility for every affected client. A stuck door flag is a concrete invisibility suspect; clearing all `EF_NODRAW` globally would destroy intentional cinematic hiding and should not substitute for fixing its lifecycle.
7. **PROPOSED: add Internet transport after deterministic LAN behavior.** Keep UDP/netchan as a working baseline. Evaluate a GNS transport adapter separately, including discovery/identity, reliable/unreliable channel mapping, NAT/relay availability, disconnection and compatibility; a library alone does not provide a relay service. Test actual separate machines and adverse network conditions.

Minimum completion evidence for a story milestone: two and four clients cross a real story exit with intended inventory and landmark placement, traverse a hub out-and-back without world reset, skip one running cutscene without leaving any client hidden/frozen, and recover predictably from one client disconnect. Full campaign support additionally requires the complete map/entity inventory and every ending route to be exercised.

## Sources

- [Enhanced repository at the inspected commit](https://github.com/hajdawery/cof-enhanced/tree/e4cdbfaa260696eb886cf6c9bd4c371d53377427)
- [Co-op bridge design](https://github.com/hajdawery/cof-enhanced/blob/e4cdbfaa260696eb886cf6c9bd4c371d53377427/docs/design/coop-bridge.md)
- [Build requirements](https://github.com/hajdawery/cof-enhanced/blob/e4cdbfaa260696eb886cf6c9bd4c371d53377427/docs/dev/building.md)
- [Pins and component terms](https://github.com/hajdawery/cof-enhanced/blob/e4cdbfaa260696eb886cf6c9bd4c371d53377427/LICENSING.md)
- [Upstream engine transition queue](https://github.com/FWGS/xash3d-fwgs/blob/4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/server/sv_game.c#L707)
- [Upstream engine save and transition implementation](https://github.com/FWGS/xash3d-fwgs/blob/4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/server/sv_save.c)

## Appendix: separate Enhanced runtime prepared

**VERIFIED, 2026-09-27:** A separate experiment is installed at:

`runtimes\enhanced`

This preparation does not depend on D:. The complete source was the C: baseline. Robocopy copied 6,197 files (the old `qconsole.log` was excluded), with zero failed files. The source had no reparse points. The experiment's `cryoffear/cl_dlls/hl.dll` was explicitly replaced from the verified C: `hl.dll.orig` so concurrent baseline experiments cannot contaminate this fixture. All **6,190 entries** of `baseline/pristine-manifest.sha256` matched the new runtime before the Enhanced overlay. The seven additional files already present in the development copy were also included in the preinstall manifest.

**VERIFIED:** The local x86 `CoFLaunchApp.exe` and `filesystem_stdio.dll` were built with the existing toolchain, and the complete seven-file executable payload was copied into `experiment-evidence/staged-binaries/`. Every copied binary was hash-checked and pinned in `SHA256SUMS.txt` with `BUILD-INFO.txt`. Additional hashes:

| File | SHA-256 |
| --- | --- |
| `CoFLaunchApp.exe` | `1A7F290EB1EB748CDCA2E031D706EC29E2BE760EB7210CCA8F98D927159F30C7` |
| `FileSystem_Stdio.dll` | `D62ADC36597F950E84DE6137D041F8D5D801F123C01008C74A31260907F355C4` |
| Official SDL2 x86 runtime | `921E4C9910983C740A96EB243BDBD2A5B17C3178DB5473804909AC11EF06E8D7` |

The unchanged upstream `scripts/build-release.ps1` generated a local installation package and a corresponding-source package from the pinned clean outer checkout. It checked all seven language-pack manifests, gamepad art, required HUD/font assets, and confirmed no payload file was identical to a retail file in the C: baseline. These packages were not published:

| Local package | Bytes | SHA-256 |
| --- | ---: | --- |
| `experiment-evidence/package/cof-enhanced-0.4.0-test3.zip` | 39,649,435 | `F9503BF7D11D326EC470A365281892015757A6DA862EAA0A0EDCBCCB38B280FB` |
| `experiment-evidence/package/cof-enhanced-0.4.0-test3-source.zip` | 60,106,080 | `3D9863E35B92D8D0A8054262D26727D20975385AA01047DD914F98A9548459AA` |

**VERIFIED:** After extraction into the experiment, the official `cof-enhanced/install.ps1 -GameDir <experiment>` completed under Windows PowerShell 5.1 with exit code 0. Only that child process received the standard Windows PowerShell module search path; no persistent environment or execution-policy changes were made. The installer:

- checked and installed **665 payload files**, then generated `cryoffear/gameinfo.txt` and `cryoffear/maps/c_game_menu1.ent` from this fixture's own files;
- verified all **667 installed/generated files**;
- backed up precisely `CoFLaunchApp.exe`, `FileSystem_Stdio.dll` and `vgui.dll` in this fixture's `cof-enhanced-backup/`;
- retained the stock retail `hl.dll` and `client.dll` hashes documented above.

Independent verification, recorded in `experiment-evidence/runtime-verification.json`, rehashed all 6,197 preinstall files: **6,194 unchanged**, exactly those three expected replacements, no missing files, and matching original backups. It also checked all 667 installer state hashes, x86 machine fields for the seven payload modules plus both retail game DLLs, and the on-disk presence of all **24 distinct direct DLL imports** in the runtime or Windows' x86 system directory. This import check does not load the modules or validate dynamic loading.

The generated menu entity override matched the independent upstream Python generator byte for byte, and both generated files were newer than their source metadata/BSP. The recursive fixture check found **zero symlinks, junctions or multiply linked files**, including saves/configs. The verification snapshot contained about 5.06 GB including logs, full game content and retained local packages. `SAVE/`, settings, fonts, resource files, and engine outputs are ordinary independent copies.

### Prepared launch entry points (not executed)

`Open-Enhanced-Menu.cmd` and `Start-Enhanced-Story-Experiment.cmd` are beside the fixture's launcher. Both use a 1280x720 window, `+volume 0` for muted diagnosis, player name `Player`, and **UDP 27017** to separate this experiment from the stock baseline's usual 27015. Neither contains a timer or termination command. Steam's Play button still starts the separate Steam installation; use the local entry points for this experiment despite the upstream installer's generic Steam message.

Exact PowerShell story-test command, from the runtime directory:

```powershell
& '.\CoFLaunchApp.exe' -windowed -width 1280 -height 720 -port 27017 -log enhanced-story.log -logtime +volume 0 +name Player +hostname CoF_Enhanced_Story_Experiment +sv_lan 1 +coop 0 +deathmatch 1 +maxplayers 2 +mp_timelimit 0 +map c_intro
```

The launcher itself supplies `-game cryoffear -cof-pmove-legacy +set cof_save_root_compat 1`, sets `XASH3D_BASEDIR` and current directory to this fixture, and loads its colocated `xash.dll`. `enhanced-story.log` and `enhanced-menu.log` will be written into this runtime when their respective launch scripts are used. No RCON password, firewall rule, UPnP mapping or global networking setting was added.

**Not yet verified:** first launch, DLL load success, menu rendering, sound, gameplay, second-client connection, story transition, saves and all long-term co-op changes. No game was started during preparation. The next operation is an announced menu smoke test, followed by a separately observed two-client control; this fixture is ready for those tests, not evidence that they already pass.

### Evidence and restore path

Runtime-local files:

- `EXPERIMENT-README.md`: how to start this specific copy and its current limits.
- `experiment-evidence/baseline-copy.log`, `preinstall-files.sha256`, `pristine-comparison.json`: source-copy and complete baseline checks.
- `experiment-evidence/staged-binaries/`: frozen binary payload and hashes.
- `experiment-evidence/package-build.log`, `package/`: assembled installer and corresponding source packages.
- `experiment-evidence/install-output.log`, plus root `cof-enhanced-install.log`: actual installer result.
- `experiment-evidence/pe-dependencies.txt`, `entity-override-check.log`, `verify-runtime.py`, `runtime-verification.json`: independent static verification.
- `cof-enhanced-backup/` and `Uninstall.cmd`: upstream rollback for this experiment only. Uninstall was not run because the prepared Enhanced runtime is the requested result.

The complete fixture contains the user's proprietary retail assets and must stay local. Only the Enhanced-only package is separated from those assets. The baseline and D: were never written by this preparation, and no live baseline game process was controlled.
