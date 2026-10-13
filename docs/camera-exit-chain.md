# c_nightmare exit-chain probe design

All entity facts below are **STATIC**, read from the complete 580-entity lump and model lump of the local `c_nightmare.bsp`. Entity indexes are zero based. No BSP or live game was modified by this analysis.

The shortest entry that retains the original final music, visual effects, and teleports is **`multispotea`**, entity 430. Its exit delay is 6.5 game-time seconds. This final chain does **not** activate a `trigger_camera`; its normal entry is an ordinary `trigger_once`. A generic transition probe succeeding does not yet prove that camera state is necessary for the earlier failure.

## Complete final chain

| Relative time | Entity / target | Action |
| --- | --- | --- |
| Entry | 432, `trigger_once`, brush `*125` | Fires `multispotea` with the touching player as activator |
| 0 | 430, `multi_manager`, `multispotea` | Starts all following parallel actions |
| 0 | 431, `ambient_fmodstream`, `koldeh21` | Plays `nightmareend.mp3` |
| 0 | 443, `multi_manager`, `multiljusfl1_1` | Begins the repeating light-toggle chain described below |
| 0 | 440, `env_customize`, `headdy_vis` | Targets `headdy`, `m_iVisible=1` |
| 0 | 433, `env_model`, `headdy` | Uses `models/Props/Blandat/headdy.mdl`; on-sequence `idle`, off-sequence `action`, `m_iAction_On=1` |
| 0 | 438, `ambient_generic`, `ljudskrik` | Plays `ambience/b_screamclose1.wav`, channel 6, spawnflags 49 |
| 0 | 434, `cof_teleport`, `beforetele` | Moves the player to entity 435 `beforetelet`, origin `(-739,-1416,-320)`, angles `(0,270,0)` |
| 0 | 436, `func_wall_toggle`, `walltoggle`, brush `*126` | Toggles the wall; original spawnflags 1 |
| 1.5 | 439, `env_customize`, `headdy_inv` | Targets `headdy`, `m_iVisible=2` |
| 1.5 | 437, `multi_manager`, `multispotea2` | Begins the final fade/teleport manager |
| 1.5 | 131, `cof_blur`, `blur3` | Strength 1.5, fade 0.25, rate 90 |
| 1.5 | 256, `env_fade`, `fadelaosee` | Black, opacity 255, duration 1, **holdtime 300**, no explicit spawnflags |
| 1.5 | 428, `cof_teleport`, `teleportea45` | Moves the player to entity 132 `teleea45`, origin `(-2032,344,-1236)`, angles `(0,0,0)` |
| 6.5 | 429, `cof_teleport`, `teleportea` | Moves the player to entity 103 `teleea`, origin `(639,-2951,-747)`, angles `(0,90,0)` |
| Touch after last teleport | 101, `trigger_changelevel`, `changelevelea`, brush `*57` | Requests `c_start` with landmark `landea` |

`changelevelea` is not directly fired by either end manager. The final teleport must put the activating player's collision hull into its brush. Its targetname also allows a direct Use test, but that bypasses the final effects and touch route.

The long black fade means a failed or delayed transition can display black for five minutes even if the engine remains responsive. Confirm a genuine stall with server responses, frame progress, and thread stacks; the screen alone is insufficient.

### Relevant coordinates

| Object | Bounds or origin |
| --- | --- |
| Entry trigger 432 / model `*125` | Min `(-783,-1374,-315)`, max `(-696,-1369,-219)` |
| Existing final-scene player start 442 | Origin `(-741,-1420,-282)`, angles `(0,270,0)` |
| Head model 433 | Origin `(-738,-1474,-259)`, angles `(0,90,0)` |
| Wall 436 / model `*126` | Min `(-786,-1456,-320)`, max `(-695,-1343,-177)` |
| Intermediate teleport destination 132 | `(-2032,344,-1236)` |
| Final teleport destination 103 | `(639,-2951,-747)` |
| Exit 101 / model `*57` | Min `(608,-2983,-757)`, max `(671,-2917,-751)` |
| Landmark 102 / `landea` | `(639,-2951,-702)` |

The exit is a thin floor-like brush. Compare the actual player hull after `cof_teleport` adjustment, rather than testing whether the unadjusted destination point alone lies within its bounds.

### Repeating light managers

Entities 443 and 444 are `multiljusfl1_1` and `multiljusfl1_2`. Each toggles entity 441 `ljusfl1` at delays 0, 0.02, 0.03, and 0.05, then activates the other manager at 0.06. This deliberately repeats until map teardown. Retain it in the first faithful end-chain probe. If that reproduces a failure, disabling only the loop in a subsequent comparison would distinguish the effect; its mere presence is not proof of an infinite engine loop.

## Deterministic five-second bootstrap

**GENERATOR AND AUTOMATIC EXIT VERIFIED.** The live final-chain probe reached `c_start` automatically at 10:52:58 local on 2026-09-27, using the repaired DLL in PID 15908. The console recorded `CHANGE LEVEL: c_start landea`, then `Started map "c_start"`; A2S confirmed the destination and a responding server. See `evidence/20260927-camera-final-chain-fixed/observations.json` and its redacted console snapshot. The user subsequently confirmed normal controls at the beginning with the phone, establishing a playable destination; the exact report is recorded in the session notes. The test derives an isolated BSP from the user's own map, retains all original 580 entities, and adds only the test bootstrap plus necessary spawn-position changes:

1. Relocate **all four** existing `info_player_start` entities (104, 158, 283, 442) to the existing final-scene start position `(-741,-1420,-282)`, angles `(0,270,0)`. Otherwise multiplayer spawn selection can land inside either original intro trigger and start the long camera sequence.
2. Add a `trigger_once` using existing model `*125`, with translation `origin "0 -49 0"` and target `probe_end_delay`. Its translated bounds would be min `(-783,-1423,-315)`, max `(-696,-1418,-219)`, intersecting the test start. Keep original trigger 432 unchanged at its original position.
3. Add one `multi_manager` named `probe_end_delay` containing `"multispotea" "5"`.
4. Start the probe under the same multiplayer launch conditions and the known null-landmark patch. Expect final effects around game-time 5 seconds and the final teleport/exit request around 11.5 seconds.

Use a **player-touch bootstrap**. A simple auto-trigger can supply a null activator; the stock `cof_teleport` handler rejects null activators. The touch-to-multi_manager chain retains a player activator through the delay, matching the original sequence. Confirm that translated model bounds behave as expected in the derived map before diagnosing a missing exit.

This preserves every original entity record other than the four explicitly changed start positions. It also retains models, audio, camera entities, lights, and map geometry. The original map remains untouched. The derived file contains game geometry and should remain a private local test artifact.

### Generator commands and verification

From the launcher workspace, inspection is the default:

```powershell
python tools/make_camera_probe.py
```

Explicit creation of the new, previously absent map:

```powershell
python tools/make_camera_probe.py --write
```

The generator exposes pure `build_probe(source: bytes) -> bytes`, pins the source SHA-256, and copies original entity records as raw text. Duplicate entity keys and the entire worldspawn WAD list are retained exactly. It appends the replacement entity lump and changes only the original entity lump's eight-byte offset/length header. All other original BSP bytes and lump headers remain unchanged. The generated map parses as **582 entities** with all four starts relocated and the original final chain intact. Its SHA-256 is `0e135b8cf4b2bd8002537f9542785d829a48c0d39a88ffe142c3467e765821fc`.

The CLI permits only a C: development copy, rejects path aliases and linked input, creates `cof_probe_camera.bsp` exclusively, and refuses any existing output. It can create this new unloaded map while another map is active. It does not start the game or send commands. After generator verification, the main investigation created and ran the probe. After the successful exit, all three private diagnostic BSPs were moved into `evidence/diagnostic-maps/` with matching hashes, returning the active map directory to its original 235-map inventory.

Use the repaired null-landmark DLL variant when testing: its removed address operand must also have its PE relocation disabled. The earlier variant allowed ASLR to overwrite the new instructions. Separately, the earlier minimal basic probe omitted the original worldspawn WAD list and later displayed a fatal missing-WAD dialog. Those two established faults mean the previous apparent hangs do not establish a camera-related failure.

## Earlier camera/control state omitted by this probe

The original map's opening has substantial state that this short final-chain test intentionally does not initialize:

- Entry triggers 125 (`*62`) and 293 (`*102`) start the parallel managers named `multiintro3`.
- `multiintro3` starts cameras `kameraheh4` through `kameraheh12`. Their starts plus waits all reach approximately 83.5 seconds from the initial manager.
- It starts `nightmulti` at delay 43. That group starts the second camera sequence; those camera starts plus waits reach 40.5 seconds later, again approximately initial time 83.5. Camera spawnflags are 116.
- At `nightmulti` delay 0, entity 576 `cof_begingame` named `begin` is used. Its exact control/reset effects have not been established by this entity-only analysis.
- At `nightmulti` delay 40, `pulla` teleports the player to entity 128 `kokalek2`, `(-39,-149,2)`. At 40.5, `kakkaej` gives `weapon_camera`, starts the corridor music, and fades in. The player then traverses the photograph-trigger corridor before reaching the final ordinary touch trigger.
- `lolzoom1`, entity 575, is a `cof_camerazoom` activated during the first sequence. It has `zoomfov=40`, `zoomtime=666`, and `returntime=3`.
- Two `cof_killmp3` targets occur in the corridor: `killfittfanfan` and `killnightmusic`. They are not fired by the final `multispotea` chain itself.

The final-chain probe therefore isolates final audio, recurring lights, fade, teleports, and transition from accumulated intro/camera/inventory state. A success would narrow the problem but would not clear all prior state. A failure would reproduce the issue without those earlier cameras and would directly disprove the claim that active camera shutdown is necessary.

No final-chain entity explicitly clears camera-control flags, invokes a camera stop target, or uses `cof_begingame`. The `env_customize` objects affect the head model; their `m_iPlayerReact=-1` values should not be interpreted as demonstrated player-control resets.
