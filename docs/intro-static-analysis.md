# Intro transition investigation, 2026-09-27

The allow-transition patch got past the DLL's multiplayer gate in the observed run. The remaining observed failure is the engine refusing a landmark-style level change in multiplayer. Earlier suspicions about an untriggered exit or failed teleport are superseded for this run by the engine refusal and the reported player location.

## Runtime evidence and its limits

**VERIFIED from saved evidence:** At `2026-09-27T17:20:15.515670+00:00`, A2S still reported `c_intro`, one player, and two maximum players. RCON status reported map position `211 x, -404 y, -2530 z`. The saved console log contains:

```text
changelevel2 <levelname> : not for use with multiplayer games
```

Sources:

- `evidence/20260927-101543-allow-transition-intro/black-screen.json`
- `evidence/20260927-101543-allow-transition-intro/qconsole.black-screen.redacted.log`, line 351

**USER-REPORTED, recorded in that JSON:** The cutscene completed, the HUD and inventory were usable, and the world remained black, as in the stock test.

**REPORTED by the controlling agent:** It then used ordinary RCON `changelevel c_nightmare` to release the user from the blocked intro. That manual action is separate from a natural transition test.

The saved log does not include the DLL's `CHANGE LEVEL: ...` message. Its absence cannot establish that `ChangeLevelNow` was never reached: this run reached the engine's refusal. The DLL's alert and the engine's command error evidently have different capture behavior in this test. Do not use absence of the former as the sole reachability test.

The proposed plain-coop-transition patch addresses the observed landmark refusal. It remains an experiment until a full intro run completes naturally; it does not provide inventory carry-over, landmark placement, or persistent hub state. It also changes single-player landmark handling in that copied DLL.

## Static map chain

**STATIC, read from all 573 entity records in the local `c_intro.bsp`.** Entity indexes below are zero based. Brush bounds come from BSP model lump 14; model origin is zero for the cited brushes.

| Stage | Entity / target | Exact data |
| --- | --- | --- |
| Player start | 98, `info_player_start` | Origin `(-1081, -2422, 66)`; angles `(0, 328, 0)` |
| Intro activation | 157, `trigger_once`, model `*59` | Targets `multiintroduct`; bounds `(-1159,-2509,34)` to `(-965,-2320,149)`, containing the start origin |
| Initial managers | 489 and 550, `multi_manager`, both `multiintroduct` | Introduction music at delay 0; `multikattej` at delay 52; text/fades/cameras on parallel schedules |
| Main cutscene managers | 382, 423, and 485, all `multikattej` | Entity 423 starts `musikplay` at delay 0, `multiintro1` at 38, and `kunitei2` at 133 |
| Main cutscene music | 422, `ambient_fmodstream`, `musikplay` | Message `introreal.mp3` |
| Exit teleport | 346, `cof_teleport`, `kunitei2` | Entity origin `(-408,-2196,162)`; target `kunitei` |
| Teleport destination | 292, `info_teleport_destination`, `kunitei` | Origin `(211,-405,-2593)`; angles `(0,90,0)` |
| Exit trigger | 291, `trigger_changelevel`, model `*141` | Map `c_nightmare`; landmark `landakukej`; no targetname, so reached through touch |
| Exit brush | Model `*141` | Bounds `(169,-451,-2590)` to `(255,-362,-2499)` |
| Landmark | 290, `info_landmark`, `landakukej` | Origin `(212,-408,-2523)` |

The intended exit is scheduled 133 **game-time** seconds after `introreal.mp3` begins, or 185 game-time seconds after `multiintroduct` fires. Wall-clock timing may differ. Several camera waits also end around main-cutscene delay 133, with the final one-second camera beginning at 132.95; static timing alone does not prove a camera/teleport race.

The raw teleport destination is three units below the brush's minimum Z, but the teleport code adjusts the player origin using a player hull offset and adds one unit. Comparing raw destination Z alone would be misleading. The RCON-reported position in the observed failed run lies inside the exit bounds. RCON status is not a direct dump of all player collision fields.

No `trigger_transition` entity occurs anywhere in this map. The managers also reference `multiintro3` from one parallel sequence; the exit teleport is independently scheduled by entity 423 and does not depend on that name.

## Static DLL reachability

**STATIC, disassembled from the verified stock `hl.dll.orig`:**

```text
SHA-256 0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63
Image base 0x10000000
```

The analysis used installed Microsoft `dumpbin /disasm /range` and Python `pefile`. Capstone is unavailable in the active Python environment; no disassembler was installed and no binary was modified.

### Exit touch and ChangeLevelNow

- Exported `CChangeLevel::TouchChangeLevel` begins at VA `0x101135E0`. It compares the touching entity's classname with the literal `player` and calls `0x10109D40` at `0x1011362A` on a match. No separate multiplayer test appears in that handler.
- `ChangeLevelNow` at `0x10109D40` calls game-rules vtable slot `+0x24`. The stock conditional jump at `0x10109D60` returns when that call is nonzero. The allow-transition patch NOPs exactly this six-byte jump.
- The remaining early checks include a same-frame debounce at approximately `0x10109D66–0x10109D7D` and transition-volume validation at `0x10109DB9–0x10109DDC`.
- The function selects engine entity index 1 before transition-volume validation. That is a single-player assumption even when a different teammate touches an exit.
- The helper at `0x1010B9C0` initializes its result to true and examines named entities for classname `trigger_transition`. With no matching transition volumes, the result remains true. The failure diagnostic is `Player isn't in the transition volume %s, aborting`. This is not the observed refusal.
- At `0x10109EEF–0x10109F00`, it submits `CHANGE LEVEL: %s %s` through an engine alert callback with level 1.
- At `0x10109F06`, it pushes the landmark buffer at `0x10231034`; at `0x10109F10`, it calls the engine level-change callback. The plain-coop-transition candidate replaces only that landmark push with a null pointer, in addition to retaining the gate patch.

### Cof teleport's player assumptions

- The `cof_teleport` factory at `0x10091240` installs vtable `0x101C5B50` at `0x10091288`.
- Vtable slot 58 points to the four-argument Use handler at `0x100A7990`.
- It returns if the activator is null (`0x100A799D–0x100A799F`) or the destination target string is absent (`0x100A79A5–0x100A79AF`).
- The normal player path tests the activator's player virtual method and converts that entity to a player object around `0x100A79DB–0x100A79FC`.
- It resolves the destination by targetname, adjusts destination Z, and invokes the SetOrigin-like helper at `0x10119FE0` through the call at `0x100A7A8A`. That helper updates player position state and invokes an engine callback with the player edict and new coordinates.
- There is no multiplayer gate or all-player loop in this handler. The normal path teleports the activating player. An alternate explicitly named entity path exists, but the intro teleport has no such override key.

These activator and entity-index assumptions remain relevant for genuine multiple-player testing. They do not explain the observed engine refusal by themselves.

## Post-map cvar findings

**VERIFIED from the saved log:** It reports `mp_allowmonsters`, `mp_flashlight`, and `mp_weaponstay` as `0`, despite the launcher generating `1`. The controlling agent also reported runtime checks of the zero values.

**STATIC:** The local `cryoffear/server.cfg` and `cryoffear/listenserver.cfg` contain only the two voice settings `sv_voicecodec voice_speex` and `sv_voicequality 5`. Neither explicitly resets those three variables. `c_intro` worldspawn also has no corresponding keys. A worldspawn key handler near `0x10120DD2` can set `mp_allowmonsters`, but the intro does not supply the examined map key.

The exact reset site is not established. The startup `c_game_menu1` load before `c_intro`, game-rule initialization, and map-specific initialization need to be distinguished. Any launcher enforcement must be checked after a map has started and after a later map change; successful config generation does not establish the active cvar values. A late exec could also be too late if `mp_allowmonsters` gates entity creation during map spawn, so post-load enforcement alone should not be treated as a demonstrated monster fix.
