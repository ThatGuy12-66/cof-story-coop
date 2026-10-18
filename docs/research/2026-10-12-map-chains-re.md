# D - Story-map entity-chain audit (81 story maps)

> **Corrections (2026-10-18, decompilation survey, `docs/research/2026-10-18-cof-decompilation-survey.md` section 6):**
> - 1.6 and 2.6: `trigger_camera` already applies to every player (SET_VIEW, freeze, HUD bits); do not mirror it.
> - 2.x: liblist's `-num_edicts` was most likely ignored by retail too (2048 + 15 per extra player).

Static, read-only audit of the entity lumps of the 81 story maps (`scratchpad/story_maps.json`), read with `cofcoop/bsp.py` from `runtimes/enhanced/cryoffear/maps`, plus spot checks in the stock `hl.dll` (SHA-256 `0036B91C...0E63`, base 0x10000000). Scripts: `scratchpad/re/tmp-D/` (`chains.py` graph + backward walk, `doors.py`, `timeline.py`, `fwd.py`, `scan_classes.py`, `report.py`). Machine-readable data: `D-map-chains.json` next to this file (per map: every edge, every assigned instance with all root paths, hazard flags; plus the door list).

Confidence tags: **CERTAIN** = read from machine code in this audit (VA given); **LIKELY** = HLSDK/SoHL parity or a heuristic code scan; **DATA** = read from the map entity lumps (exact, but intent is inferred).

## 0. What the graph contains

Per map, every entity's outgoing edges (stored in the JSON as `[srcIndex, targetName, kind, key, delay]`):

| edge kind | from | notes |
|---|---|---|
| `fire` | `target` of every class whose target is Used (`trigger_*`, `func_button`, `game_counter`, `func_breakable`, `item_key`, `cof_*` actors such as keypads, `cof_phonecall`, `cof_strangle`, `cof_mdlcutscene`, `cof_puzzlebar*`, `func_valve`, `statue_puzzle_complete`, `trigger_subwaywall`, `camera_spot_trigger`, `cof_computer`, `cof_telephone`, `item_padlock`...); every non-standard key of a `multi_manager` (value = delay; `name#2` duplicates kept) | `delay` on triggers is kept as the edge delay |
| `cond-fire(...)` | `cof_weapontrigger.noise` (no weapon), `cof_yesno.notrigger` (answer no), `cof_doctorweapontrigger.message` (revolver branch) | the `target` of these three is the other branch |
| `monster-trigger:<condition>` | monster `TriggerTarget` + `TriggerCondition` (4 = death) | activator = the monster |
| `path-pass` | `func_train`/`func_tracktrain`/`trigger_camera` -> `message` of a `path_corner` on its route | activator = the mover |
| `fire-after-changetarget` | entity rewritten by `trigger_changetarget` -> its `m_iszNewTarget` (only `c_ending5b`, 19 rewrites of buttons) | |
| `kill` | `killtarget` (every story `trigger_relay` is killtarget-only; none has a `target`) | |
| `enable(globalstate)` | `env_global` -> `multisource` with the same `globalstate` | masters of `trigger_once/multiple/inter_door/trigger_checkbrush` recorded as `ref:master` |
| `ref:*` | references that are not Use(): `trigger_camera.target/moveto`, `inter_door.target` (exit), `cof_teleport.target/message`, `trigger_teleport.target`, `env_customize.target`, `cof_interdooronoff.message`, `inter_door_disable.target`, `cof_inter_door_exitchange`, `cof_triggeron.target` (enables a disabled trigger), `cof_clothesmenu.cameraent/simonent`, `cof_updatekeypad.message`, `cof_strangle.message` (the monster), `cof_wheelchairmode.message` (destination), `cof_telescope.message`, `item_padlock.message`, `func_valve.message`, `monstermaker.netname`, `trigger_changevalue`... | |

Keys checked and found to be names, but NOT fire edges: `cof_updatekeypad.message` points at an `env_model` (the keypad model), `cof_strangle.message` at a `monster_generic`, `cof_wheelchairmode.message` at an `info_teleport_destination`. `func_coopallplayersbutton`, `cof_changelevel`, `cof_keypad*` other than 1-4, `trigger_transition` and `trigger_auto` (except 3 in `c_start`) do not occur in the 81 story maps (DATA). `netname` is only a name on `monstermaker` (spawned children) and a key name on `trigger_changevalue`; SoHL's `netname` toucher filter is honoured by `CanTouch` but no story trigger uses it.

Backward walk: from each instance, follow incoming `fire`-type edges until a root. Roots: `trigger_once/multiple` (their Use is null, see 1.3, so incoming edges are ignored), monsters, `trigger_auto`, player-usable classes (`func_button`, `inter_door`, `item_key`, `func_valve`, `item_padlock`, puzzles...) and entities nothing fires (`unknown`). Root notes: `covers info_player_start` (volume contains the spawn point, 0 units padding), `covers an info_landmark`, `starts disabled (iuser1)`, `master=...`. Activator propagation per hop is in 1.x; the result is the `act=` / `activator_at_instance` field.

## 1. Engine-relevant facts verified in hl.dll during this audit

1. **`FireTargets` 0x10101FF0** (SoHL version: `+name`/`-name` force ON/OFF, `name(value)`, USE_KILL) passes the activator through unchanged to `Use` (`call [eax+0xE8]` at 0x10102191 with `push ebx` = activator). CERTAIN.
2. **`multi_manager`** (SoHL; factory 0x10107C50, vtable 0x101E08E4): ManagerUse 0x1010E190 stores `m_hActivator` at +0x184 (0x1010E3F8); ManagerThink 0x1010DA80 fires `FireTargets(name, m_hActivator, this, useType)` (0x1010DC74..0x1010DC90). **The activator survives multi_manager delays**, including the 46 s and 358 s delays found below. CERTAIN. Spawn warns above 16 targets; no story multi_manager has more than 16. DATA.
3. **`trigger_relay` Use 0x10115680** checks its master and calls `FireTargets(target, pActivator, this, ...)`: unlike HLSDK it keeps the activator. CERTAIN. Irrelevant for the story maps because all 177 relays only `killtarget`. DATA.
4. **`trigger_once`/`trigger_multiple`**: Spawn (0x10111000 -> 0x10110F40) installs only Touch 0x1010E840; there is no Use, so firing a trigger by name does nothing. `CanTouch` 0x10109C40 returns 0 while `pev->iuser1 != 0` (35 story `trigger_once` start disabled this way and are switched on by `cof_triggeron`), then applies the SoHL `netname` filter, then the client/monster/pushable spawnflags. ActivateMultiTrigger 0x10108BC0 calls `SUB_UseTargets(toucher)`. The activator of every touch chain is the touching player. CERTAIN.
5. **Monster `TriggerTarget`**: FCheckAITrigger fires at 0x100BDBC7 with `FireTargets(m_iszTriggerTarget /*+0x334*/, this, this, USE_TOGGLE)`, so the activator is **the monster**. CERTAIN. Exception: `monster_booksimonsledgehammer` forwards damage to its `message` monster (vtable 0x101AACB8 slot +0x64 = 0x10024F70) and, when that runs out, fires the target monster's TriggerTarget with activator = **FIRST player** (`UTIL_FindEntityByClassname(NULL,"player")` at 0x1002515B, FireTargets at 0x100251B2). CERTAIN. This matters for `c_ending4b` (`justhealth`).
6. **`trigger_camera`** Use 0x10113B60: ON picks `pActivator` if `IsPlayer()`, otherwise `INDEXENT(1)` (0x10113D54..0x10113D92) and only that player gets the freeze, HUD bits and view. OFF (value 0 at the end of `wait`) loops over **all** clients, restoring view, control and HUD (0x10113BE0..0x10113D14). The camera's `moveto` path fires `path_corner.message` with the camera as activator (Move 0x1010E570, FireTargets 0x1010E5E7). CERTAIN.
7. **`cof_teleport`** Use 0x100A7990: without `message`, teleports the activator **only if it is a player** (otherwise it does nothing). With `message`, it teleports the named entity (used for 4 monsters). It clears FL_ONGROUND, sets `fixangle` and clears `b_GoingThroughADoor`. CERTAIN. So the 6 teleports fired from monster deaths (`c_roofs2` teleportia4/42, `c_sewer3` teleport_till_lik, `c_attic` teleportblack/deteleport1, `c_ending4b` fitttth when the boss dies normally) are inert in single player as well.
8. **`cof_weapontrigger`** Use 0x100A8090: needs a player activator; fires `target` if `activator->HasNamedPlayerItem(message)`, otherwise `noise`, passing the same activator. CERTAIN. **`cof_yesno`** Use 0x100A8140 also returns at once without a player activator. CERTAIN.
9. **`cof_killplayer`** Use 0x100A42C0 loops over clients 1..maxClients, ORs the spawnflags into the death-type field +0x5B4, sets health 0 and calls Killed (vtable +0x6C). **It kills every player.** CERTAIN.
10. **`cof_ending`** Use 0x100A3C80 needs any non-null activator and sets flag +0x4CD/+0x4CE/+0x4CF (iuser1 0/1/2) on **every connected player**. CERTAIN. A player who is not connected at that moment never gets the flag.
11. **`cof_mdlcutscene`**: the end think 0x1009F4A0 fires `target` (`FireTargets` 0x1009F6B2) with the player it held as activator (LIKELY the FIRST player from the FindEntityByClassname at the start of Use 0x100A4670).
12. **Phone dial**: a player method at 0x100E9FF0 finds the single `cof_phonecheck` by classname, sets its owner to the dialling player and passes it the dialled number. So `c_basement`'s call chain root is "the player who dials the number". CERTAIN for the lookup, LIKELY for the activator it then fires with.
13. `inter_door` code references the strings `endingcheck` (0x100481AF) and `ending_exit1..5` (0x1004821C..0x1004829D). The selection rule belongs to the inter_door/ending report.

Heuristic scan (tmp-D/scan_classes.py, LIKELY only): FIRST-player lookups are found in cof_lookat, cof_greyfade, cof_strangle, cof_clothesmenu, cof_phonedisable, cof_doctorweaponset/trigger, cof_objective, cof_keypad4, cof_stopwheelchair, cof_puzzlebar1-5 (plus SetView) and cof_mdlcutscene. Loops over all clients are found in trigger_cofmobile, cof_credits, cof_begingame and cof_chapter. The activator is read in cof_hint, cof_simonspeak, cof_camerazoom, cof_maxhealthchange, cof_giveitems, cof_wheelchairmode, keypads 1-3, cof_telephone and cof_computer. The `affects` column in the JSON records this per class.

## 2. The most important co-op hazards (prioritised)

1. **Team wipes from one player's touch: 6 `cof_killplayer`** (all reached from a single `trigger_once`, and the class kills everyone, 1.9). The 6 are `c_attic` 'deathuknow' (the doctor-shoots-you volume `trigger_once#82 'toncedeath'` -> `multiyoudie` -> +1.32 s), `c_ending1b/2b/3b` 'fuckingdie' (`trigger_once#173`), `c_ending5` 'killmefuck' (`trigger_once#156`) and `c_subwaysick1` 'fuckingdie' (`trigger_once#291`). With 2 players, one misstep (or one player loitering in the doctor's line of fire) kills both and starts the team-wipe restart. **SOFT-LOCK-ish (forced wipe).** Treatment: the engine cannot narrow a loop inside the DLL. Options: (a) a vtable-slot replacement of `cof_killplayer::Use` that kills only `pActivator` (last resort), (b) engine-side: when a `cof_killplayer` Use is dispatched (observe through the touch->Use chain or the `Killed` burst in one frame), immediately re-spawn/revive the non-activator players through the existing respawn path. (b) avoids code patches but needs live testing.
2. **One-shot activator teleports strand the partner: 58 player `cof_teleport` reachable from a `trigger_once`** (4 more with `message` move monsters, not players). Only the toucher moves (1.7). The trigger is consumed, so the partner cannot follow through it. Section 4.8 classifies every such root as *round-trip* (the chain later teleports the toucher back within 400 units of the trigger: scene rooms, e.g. `c_asylum1#310`, `c_start#120/#147`, `c_city#707`, `c_subway2st1#725`) or **ONE-WAY** (34 roots). The worst are **arrival-room teleports**: the trigger covers `info_player_start` and moves the toucher out of the arrival/intro area, at t=0 to t=185 s depending on the map. Whether that area has another exit cannot be seen statically; check it live. These are `c_ending1b/2b/3b#4` ('teledes_tele' at t=0, 1800 u), `c_roofs2#244` ('teleportia2' t=0), `c_ending5#65` ('tellemenow' +21 s, 4.9 k u), `c_nightmare#125/#159/#293` ('pulla' +40/83 s), `c_broscene#150`, `c_trainscene#106` ('teleportoar_df' +101 s) and `c_intro#157` ('kunitei2' +185 s). The second player to arrive lands in the same volume after the trigger is gone. **STUCK-PLAYER** if the area is closed. Mid-map ONE-WAY moves: `c_city#171/#180` ('telelole22', ~7 k u), `c_bridge#165` ('telemia' +95 s), `c_subway2st1#631` ('2teleportion', chapter 5 start; the same chain also kills door 'shitdoor'), `c_subway2st3` 'triggercutscene'/'traintrigger', `c_roofs#265/#681` ('teleport1' then 'teleport2' after 358 s), `c_ending1..4` 'teledes_tele' (+27/34.7 s), `c_ending5b#3`, `c_subwaysick2#251`, `c_apartmentsick#302/#452`, `c_basement#240`, `c_start#148` and `c_subway2st1#556`. Treatment: during `cof_teleport::Use` with a player activator and no `message`, mirror the resulting `SetOrigin` to every other live player (destination + small lateral offsets, same angles). Apply it to one-shot roots only: the 118 teleports behind reusable `trigger_multiple` volumes are per-player out-of-bounds/loop returns and must stay per-player. The engine can key this on the JSON (map + cof_teleport targetname list) or on "the touched trigger was a trigger_once".
3. **`c_park3` phone call gates progress: possible SOFT-LOCK.** `trigger_once#508` covers `info_player_start` -> `multiparkcall` -> `cof_phonecall 'sophiecall'` (activator = first player into the spawn volume). After the call, its `target` 'gateinter' (`cof_interdooronoff`) changes the lock of `func_button 'gatebutton'` (`lockedby lockedthing`, locked message "The phone is ringing."). Nothing else unlocks that button. If the activator has no phone the call is dropped (design spec), 'gateinter' never fires and the gate is locked for everyone. Even with a phone, only that player hears the call. The risk is real because **the phone itself is handed out by a one-shot volume**: `c_start` spawn `trigger_once#120` -> `multiscene1` -> `cof_giveitems 'givemobile'` (+2 s, `weapon_mobile`) and `'giveswitchblade'` (+4 s). Both go only to the first player into the spawn volume (activator-scoped giveitems, scan). The same pattern gives the Glock in `c_apartment1` (button -> `giveitem1`). Unless the engine hands personal weapons to everyone, the partner has no phone for the whole game. Treatment: give `weapon_mobile` (and the scripted weapons) to every player when `cof_giveitems` runs. Also ring every player, or choose a phone holder as activator, and make the hang-up fire the target once.
4. **`c_asylum1` P345 hand-over is keyed on one player throughout** (section 3.2). The toucher of `trigger_once#310` is teleported into the scene room, frozen by 11 cameras for 46 s, then **only the toucher's inventory** is checked for `weapon_p345`. The yes/no prompt goes to the toucher, and the key, the max-health penalty and the P345 removal also go to that player. The doctor-weapon field is set on the FIRST player, and `c_attic` later reads it from the FIRST player. The ending flag (`cof_ending iuser1=1`) is the only effect applied to all players.
5. **`c_ending4b` wheelchair mismatch.** The spawn-volume `trigger_once#112` -> `cof_wheelchairmode 'wmode'` puts **the toucher** in a wheelchair and teleports them to 'wheelchstart'. At the end, `cof_stopwheelchair 'stoooop'` (fired 6 s after 'justhealth' dies) acts on the **FIRST** player (scan). If the guest touched first, the guest stays in wheelchair mode and the host gets a no-op stop. **STUCK-PLAYER.** The partner never enters the wheelchair. Treatment: during `cof_stopwheelchair::Use`, make FIRST := the player in wheelchair mode, or mirror wheelchair mode to all players.
6. **Cameras: 311 `trigger_camera`, 308 with TAKECONTROL.** They freeze and blind only the activator (or the host for the 17 fired by monster deaths: `c_apartmentsick` saw2kam1-4, `c_attic` dykam1/dockam1, `c_roofs2` rkam4/5, `c_sewer3` ssz1kam1/menhorunge, `c_start` laikamera1-7). The partner keeps moving, unfrozen, with HUD, while monsters spawned by the same multi_managers attack. The OFF loop releases everybody (harmless). 72 cameras belong to start-of-map scenes (SPAWN-RACE), so whoever arrives first watches the intro. **WRONG-PLAYER-VISUAL / DESYNC.** Treatment: mirror `SetView` + FL_FROZEN + HUD bits to every spawned player while a camera is on, and undo them on the OFF loop.
7. **FIRST-player effects reachable by the guest (65 instances).** `cof_strangle` in `c_ending1c/2c/3c` (valve user -> +3.5 s -> the host is moved into the strangle pose, `SetOrigin`+`EnableControl`) is followed by credits for all. `cof_clothesmenu` (`c_bridge`, `c_start` buttons) gives the host the clothes menu even when the guest pressed the button. The `cof_puzzlebar1-5` views in `c_asylumb` switch the host's view. Other cases: `cof_lookat` `c_village3` (host looks at the ending door), `cof_greyfade` `c_ending4`, `cof_phonedisable` `c_subway1st1` (disables the host's phone although the guest walked into the trigger), `cof_doctorweaponset/trigger` (`c_asylum1`/`c_attic`), 23 `cof_objective`. Treatment: "FIRST player := activator" during the Use, which the engine already does for client commands, extended to these Use dispatches.
8. **Ending choice is per-player data.** The flags are set on all players present (1.10), but `c_village3`'s `endingcheck` door reads the opener's flags and moves only the opener into the changelevel N volume (section 3.1). A late joiner or a reconnected player has different flags and could pick a different ending than the team earned. Treatment: OR the flags across players (or take the host's) when the door is used. Also carry the partner across that changelevel, because the partner is ~1.7-2 k units from landmark eN.

## 3. Hand-traced chains

### 3.1 The ending (c_village3 -> c_endingN -> ...Nb -> ...Nc / 4b / 5b,5c)

**Flags.** Three `cof_ending` set the flags on every connected player (1.10).

- `c_roofs2` `cof_ending 'endingtrigger'` (iuser1 absent = 0 -> +0x4CD). Path: `monster_roofboss 'carcassboss'` death -> `multikill` -> `endingtrigger`. The activator is the monster, which is fine because the Use only needs a non-null activator. The alternative "escape" answer (`trigger_multiple 'tmultiei'` -> `cof_yesno 'ryesno'` -> `multiescape`) kills the boss without setting the flag. It also turns a camera on the answerer and teleports **only the answerer** ('teleportia3', +5 s); the partner stays on the roof.
- `c_asylum1` 'endingtrigger' iuser1=1 (+0x4CE): the P345 was given (3.2).
- `c_hamn3` 'endingtrigger' iuser1=2 (+0x4CF): `func_button#309` (needs item `package`) -> `multiposten` -> `endingtrigger`. The user must carry the package.

**Choice.** `c_village3`:

1. `trigger_once#286` (box 162..729 x -922..-338) -> `multilook` fires:
   - `cof_lookat 'lookatit'` (fuser1 5, FIRST player's view);
   - `cof_objective 'objectiveee'` (FIRST);
   - voice/subtitles.
2. `inter_door#287` (at -268,-1226) has `target "endingcheck"`. No entity has that name: the DLL picks `ending_exitN` (1.13) and moves **only the user** there.
3. Each `ending_exitN` (-1808, y_N, -1246) lies **inside** `trigger_changelevel` N (box x -1856..-1776, z -1253..-1122 -> `c_endingN`, landmark eN). The user's arrival is itself the level change, and everyone goes to the opener's ending.
4. A reusable `trigger_multiple 'teleportse1_1x'` beside each exit fires `cof_teleport 'teleportse1_1N'`, which returns a player who walks the wrong way to `teleportse1_1ld` beside the door. It is per-player, which is fine.

Co-op consequences:

- The ending is the opener's.
- The partner crosses the level with a landmark offset of roughly 1.7-2.5 k units, so the engine must place them next to the leader.

**c_ending1..4 (first map).**

1. `trigger_once` on `info_player_start` -> `multistartis`, which plays `cof_simonspeak 'momvoice'` on the toucher.
2. `trigger_once#259/#254/#258/#254` -> `multibokam0` -> two `cof_mdlcutscene` (kambokam1 -> sukams, FIRST + loop).
3. 'teledes_tele' **ONE-WAY** teleports the toucher (+27 s; +34.7 s in c_ending4).

`c_ending4` additionally fires `cof_greyfade 'fadeoutbw'` (FIRST) at +25.5 s via `multisu`.

**c_ending1b/2b/3b.**

- `trigger_once#4` covers `info_player_start` -> `multisu` -> 'teledes_tele' at **t=0** (ONE-WAY, 1.8 k u), a door break, and chapter text "Chapter 8 - My life ends here" (`cof_chapter`, loop).
- `trigger_once#173` -> `cof_killplayer 'fuckingdie'` (team wipe).
- The exit to `c_endingNc` is touch changelevel #156.

**c_ending1c/2c/3c.**

- Arrival `trigger_multiple#656/#604` covers `info_player_start` -> `cof_teleport 'teleportf5_2'` (reusable, per player).
- `trigger_once#9` -> `multi_hiss_1` -> cameras hisskam1/2 (toucher) -> +20 s `cof_mdlcutscene 'bkam'`.
- Four `func_valve` (each `lockedby valve`, an inventory item) each fire `multivalveN`. That counts `game_counter 'countgate'` (health 4) and, in parallel, `valvecount` -> `multiendsimon`.
- When `countgate` reaches 4 -> `multisd`:
  - +0.5 s: camera `sdkam` on the 4th valve user;
  - +3.5 s: `cof_strangle 'strangle'` (FIRST player moved and frozen; `message 'fagnerd'` = the strangling monster_generic);
  - when the strangle ends it fires `multisdeath`.
- `multisdeath` -> +8 s `multiend1`, which starts:
  - `cof_credits 'scrolltext'` (conclusionN.txt, loop);
  - `cof_mdlcutscene 'emdlkam'` -> camera 'conkam';
  - +202 s `multicredits` -> `ckamera1`, `creditstext` (+85 s), `statskamera1` (+90 s), `scrolltextoff`.

Co-op: the valve items may be split between players (the shared story-item ledger covers this). The strangle always takes the host. The credits reach everyone.

**c_ending4b.**

- Wheelchair (hazard 5).
- Three `monster_booksimon` and one `monster_booksimonsledgehammer 'simon'` all forward damage to `monster_generic 'justhealth'` (health 250, TriggerCondition 4 -> 'fuckoff'). Activator: the monster on normal death, or the FIRST player through the sledgehammer path (1.5). 'fuckoff' fires:
  - `whensimondies` -> `scrolltext` (+4 s);
  - `stoooop` (+6 s, FIRST);
  - `cof_teleport 'fitttth'` three times (+6.1/7/8 s): inert when the activator is the monster, FIRST player via the sledgehammer path;
  - `env_global 'global14'` -> `multisource 'multiscr14'`, which unlocks `trigger_once#462`.
- `trigger_once#462` (box -2581..-577 x -2008..1542, it covers `info_player_start`, `master multiscr14`) -> `multiending5` -> cameras e5kam1-3 and conkam on whoever is standing in the arena when the master opens, then credits.

**c_ending5 -> 5b -> 5c.**

- `c_ending5`: arrival `trigger_once#65` -> four cameras -> `tellemenow` (+21 s, ONE-WAY 4.9 k u); `trigger_once#156` -> `cof_killplayer 'killmefuck'` (sf 6, team wipe).
- `c_ending5b`: `trigger_once#3` -> `lollipopp_t` at t=0 (ONE-WAY 5.6 k u). The button puzzle uses 19 `trigger_changetarget` that rewrite `func_button` targets between `multiN` and `wrong`: a wrong press goes to 'wrong' (shared world state, so it works in co-op).
- `c_ending5c`: `trigger_once#404` -> `multicrash` -> `multiend5` -> cameras ejkam1-7 -> `multiendcr` -> `credka1`, `credits` (+45 s), `statskamera1`.

### 3.2 The P345 hand-over (c_asylum1) and the doctor's weapon (c_attic)

Timeline from `trigger_once#310` (at -238,388,204; `timeline.py c_asylum1 #310`; the activator is the toucher throughout, carried by the multi_managers, 1.2):

- t=0: `2multits1` -> `fadein`.
- t=0.5: the six `multits1` start:
  - `cof_teleport 'teledestts1t'` -> the **toucher** enters the scene;
  - `trigger_camera 1kam1..1kam11` (sf 1140 = TAKECONTROL|HIDEHUD|BARS|0x40|SKIPPABLE) over 46 s, toucher only;
  - `monstermaker 'mmmonsters2'`.
- t=46.6: `cof_weapontrigger 'dohaveornot'` (`message weapon_p345`) checks **the toucher's** inventory (1.8).
  - **has it** -> `multits3` -> camera 3kam14 -> t=58.6 `cof_yesno 'yesno'` (`weaponneed weapon_p345`, prompt to the toucher):
    - **yes** -> `multits4`:
      - `cof_ending 'endingtrigger'` iuser1=1 (all players);
      - `cof_doctorweaponset 'thisisp345'` iuser1=1 (**FIRST player**);
      - kill `gaybar` (cof_barshoot) and kill the button `givethep228b`;
      - cameras 4kam16-22, teleports `4teledestts1t`/`4teledestts2t` (answerer);
      - `mmmonsters` x5;
      - at +29.5 s: `cof_giveitems 'givethekey'` (gatekey2 -> answerer), `cof_maxhealthchange 'hitbyp228'` (impulse 2, answerer), `inter_door_disable 'enabledoor1'` (enables `othersidem2`, `lockedby gatekey2`), `cof_clearitems 'removep228'` (removes `weapon_p345`, LIKELY from the answerer).
    - **no** -> `multits5`:
      - `cof_doctorweaponset 'thisrevolver'` iuser1=2 (FIRST);
      - cameras 5kam23-29, teleports `5teledestts1t`/`5teledestts2t`;
      - +28 s: `hitbyrevolver`, `enabledoor1`, `givethekey`.
  - **lacks it** -> `multits2` -> cameras 2kam12/13 -> `teledestts2t` (toucher teleported out). The player must fetch the P345 ("He said it's in the bowling alley") and come back to `func_button 'givethep228b'` (sf 161), which fires `yesno` again with **the presser** as activator.

Co-op hazards in this scene:

- (a) If the guest walks in but the host carries the P345 (or the reverse), the "lacks it" branch runs. The carrier can then press `givethep228b`, which works because the button path re-checks the presser. The 46 s scene and the teleports still hit only the first toucher. The partner is never teleported into the scene room. `othersidem` / `othersidem2` are enabled by the chain, so whether the partner can walk in should be checked live.
- (b) `cof_doctorweaponset` writes the **host's** `m_iDoctorWeapon` (+0x5B0, scan: FIRST) regardless of who gave the gun.
- (c) `cof_maxhealthchange` and `givethekey` affect only the answerer. The gate key is a shared story item (ledger), but the max-health penalty is per player.

`c_attic` (the doctor fight):

- `trigger_once#58` -> `multidadoc2` -> +5.6 s `cof_doctorweapontrigger 'docweaponsec'`. Reading the **FIRST** player's doctor weapon, it fires `dochasp345` / `dochasrevolver` (`cof_doctorweaponset`, FIRST again). At +5.5 s it also teleports the toucher alone (`dateleport1`, landing ~130 u from the trigger) and moves the doctor (`dteleport`, `message doctorboss`).
- Boss death -> `multidscene` (activator = monster) fires:
  - `whatweapon` (`cof_doctorweapontrigger`, FIRST) -> `spawnp345` / `spawnrevolver` (monstermaker drops the weapon);
  - `teleportblack` (inert, 1.7);
  - `door2changee`, `killtoncedeath`;
  - after 4 s+: `dykam1`, `dockam1` (cameras -> host), `demdlcam`, `chapterss`, `objectiveee`.
- `trigger_once#82 'toncedeath'` (and the four `cof_doctorshoot` brushes, also named 'toncedeath', that nothing fires and the boss death kills) -> `multiyoudie` -> `triggerdocbody` + `deathuknow` -> **team wipe** (hazard 1).

Consistency rule: as long as the same machine is the host through `c_asylum1` -> `c_attic`, the FIRST-based field is consistent with itself. It only goes wrong if the host changes, or if the guest gave the gun and the team expects the guest's inventory to matter. Treatment: keep `m_iDoctorWeapon` identical on all players (copy it when `cof_doctorweaponset` runs), and FIRST := activator for `cof_doctorweaponset` in `c_asylum1`.

### 3.3 Phone moments

| map | entity | root | who gets it | follow-up |
|---|---|---|---|---|
| c_park3 | `cof_phonecall 'sophiecall'` | `trigger_once#508` (covers info_player_start) -> `multiparkcall` | the toucher (spec: dropped without a phone) | after the call: `gateinter` changes the lock of `gatebutton`, the only way on: **SOFT-LOCK if dropped** |
| c_basement | `cof_phonecall 'basementcall'` (iuser1 1) | the player dialling on `weapon_mobile` -> `cof_phonecheck` (impulse 543214 = number; 1.12) | the dialler | `addnotemm` -> `cof_addcodenote 'phonecode'` (FIRST/activator) + `cof_keypad4` (FIRST) |
| c_asylum1 | `cof_telephone 'telephoneent'` (wall phone) | `func_button 'buttonphone'` | the user | `multiphone` -> `multiphone2` -> +12 s `func_asylumlookat 'lookyou'` |
| c_subway1st1 | `cof_phonedisable 'disablefuck'` | `trigger_once#409 'loltrigger'` (starts disabled; enabled by `cof_triggeron`) | **FIRST** player | the toucher's phone keeps working |
| several | `trigger_cofmobile` (7: SMS/phone body changes) | all `trigger_once` | loop over all players (scan) | none |


## 4. Hazard lists (generated)

Flags: `KILLALL` (one toucher kills everyone), `ACT-ONCE` (activator-scoped effect behind a one-shot `trigger_once`), `FIRST` (FIRST-player-scoped effect reachable from a player touch/use, so the guest can trigger an effect on the host), `NONPLAYER` (the activator reaching the entity is not a player), `PHONE-VOLUME`, `SPAWN-RACE` (root volume covers `info_player_start`).

| flag | instances | classes |
|---|---|---|
| KILLALL | 6 | cof_killplayer 6 |
| ACT-ONCE | 353 | trigger_camera 251, cof_teleport 58, cof_camerazoom 11, cof_hint 11, cof_simonspeak 8, cof_giveitems 5, cof_maxhealthchange 2, cof_weapontrigger 1, cof_yesno 1, cof_keypad3 1, cof_wheelchairmode 1, cof_phonecall 1, cof_keypad 1, cof_keypad2 1 |
| FIRST | 65 | cof_objective 23, cof_mdlcutscene 21, cof_doctorweaponset 4, cof_strangle 3, cof_doctorweapontrigger 2, cof_clothesmenu 2, cof_addcodenote 1, cof_puzzlebar5 1, cof_puzzlebar4 1, cof_puzzlebar3 1, cof_puzzlebar2 1, cof_puzzlebar 1, cof_keypad4 1, cof_greyfade 1, cof_phonedisable 1, cof_lookat 1 |
| NONPLAYER | 23 | trigger_camera 17, cof_teleport 6 |
| PHONE-VOLUME | 1 | cof_phonecall 1 |
| SPAWN-RACE | 104 | trigger_camera 72, cof_teleport 14, cof_camerazoom 5, cof_simonspeak 4, cof_giveitems 3, cof_objective 2, cof_keypad3 1, cof_wheelchairmode 1, cof_mdlcutscene 1, cof_phonecall 1 |

### 4.1 `KILLALL`

- **c_attic** `cof_killplayer` 'deathuknow' #108 - SOFT-LOCK/TEAM-WIPE: cof_killplayer kills every player (loop, CERTAIN) when one player touches the trigger. Root: trigger_once#82'toncedeath' {player-touch (once)} -[fire]-> multi_manager#83'multiyoudie' -[fire:multiyoudie2 +0.3s]-> multi_manager#111'multiyoudie2' -[fire:deathuknow +1.02s]-> cof_killplayer#108'deathuknow'
- **c_ending1b** `cof_killplayer` 'fuckingdie' #174 - SOFT-LOCK/TEAM-WIPE: cof_killplayer kills every player (loop, CERTAIN) when one player touches the trigger. Root: trigger_once#173 {player-touch (once)} -[fire]-> cof_killplayer#174'fuckingdie'
- **c_ending2b** `cof_killplayer` 'fuckingdie' #174 - SOFT-LOCK/TEAM-WIPE: cof_killplayer kills every player (loop, CERTAIN) when one player touches the trigger. Root: trigger_once#173 {player-touch (once)} -[fire]-> cof_killplayer#174'fuckingdie'
- **c_ending3b** `cof_killplayer` 'fuckingdie' #174 - SOFT-LOCK/TEAM-WIPE: cof_killplayer kills every player (loop, CERTAIN) when one player touches the trigger. Root: trigger_once#173 {player-touch (once)} -[fire]-> cof_killplayer#174'fuckingdie'
- **c_ending5** `cof_killplayer` 'killmefuck' #157 - SOFT-LOCK/TEAM-WIPE: cof_killplayer kills every player (loop, CERTAIN) when one player touches the trigger. Root: trigger_once#156 {player-touch (once)} -[fire]-> cof_killplayer#157'killmefuck'
- **c_subwaysick1** `cof_killplayer` 'fuckingdie' #292 - SOFT-LOCK/TEAM-WIPE: cof_killplayer kills every player (loop, CERTAIN) when one player touches the trigger. Root: trigger_once#291 {player-touch (once)} -[fire]-> cof_killplayer#292'fuckingdie'

### 4.2 `ACT-ONCE`

- **c_apartment5** `cof_teleport` 'bajsiballe' #153 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#270 {player-touch (once)} -[fire]-> multi_manager#290'multibadkars2' -[fire:multibadkars +0.5s]-> multi_manager#318'multibadkars' -[fire:bajsiballe +10s]-> cof_teleport#153'bajsiballe'
- **c_apartmentsick** `cof_teleport` 'telemiehaeh' #352 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#302 {player-touch (once)} -[fire]-> multi_manager#346'multisawersc' -[fire:telemiehaeh +25s]-> cof_teleport#352'telemiehaeh'
- **c_apartmentsick** `cof_teleport` 'teleportusus' #450 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#452'lazzerro' {player-touch (once) [starts disabled (iuser1), enabled by cof_triggeron]} -[fire]-> cof_teleport#450'teleportusus'
- **c_asylum1** `cof_teleport` 'teledestts1t' #386 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#310 {player-touch (once)} -[fire]-> multi_manager#400'2multits1' -[fire:multits1 +0.5s]-> multi_manager#379'multits1' -[fire:teledestts1t]-> cof_teleport#386'teledestts1t'
- **c_asylum1** `cof_teleport` 'teledestts2t' #387 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#310 {player-touch (once)} -[fire]-> multi_manager#400'2multits1' -[fire:multits1 +0.5s]-> multi_manager#311'multits1' -[fire:dohaveornot +46.1s]-> cof_weapontrigger#341'dohaveornot' -[cond-fire(no-weapon):noise]-> multi_manager#380'multits2' -[fire:teledestts2t +13.5s]-> cof_teleport#387'teledestts2t'
- **c_asylum1** `cof_teleport` '4teledestts2t' #396 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#310 {player-touch (once)} -[fire]-> multi_manager#400'2multits1' -[fire:multits1 +0.5s]-> multi_manager#311'multits1' -[fire:dohaveornot +46.1s]-> cof_weapontrigger#341'dohaveornot' -[fire]-> multi_manager#334'multits3' -[fire:yesno +12s]-> cof_yesno#342'yesno' -[fire]-> multi_manager#382'multits4' -[fire:4teledestts2t +1s]-> cof_teleport#396...
- **c_asylum1** `cof_teleport` '5teledestts2t' #397 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#310 {player-touch (once)} -[fire]-> multi_manager#400'2multits1' -[fire:multits1 +0.5s]-> multi_manager#311'multits1' -[fire:dohaveornot +46.1s]-> cof_weapontrigger#341'dohaveornot' -[fire]-> multi_manager#334'multits3' -[fire:yesno +12s]-> cof_yesno#342'yesno' -[cond-fire(answer-no):notrigger]-> multi_manager#383'multits5' -[fire:5teledestts...
- **c_asylum1** `cof_teleport` '4teledestts1t' #398 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#310 {player-touch (once)} -[fire]-> multi_manager#400'2multits1' -[fire:multits1 +0.5s]-> multi_manager#311'multits1' -[fire:dohaveornot +46.1s]-> cof_weapontrigger#341'dohaveornot' -[fire]-> multi_manager#334'multits3' -[fire:yesno +12s]-> cof_yesno#342'yesno' -[fire]-> multi_manager#382'multits4' -[fire:4teledestts1t]-> cof_teleport#398'4te...
- **c_asylum1** `cof_teleport` '5teledestts1t' #399 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#310 {player-touch (once)} -[fire]-> multi_manager#400'2multits1' -[fire:multits1 +0.5s]-> multi_manager#311'multits1' -[fire:dohaveornot +46.1s]-> cof_weapontrigger#341'dohaveornot' -[fire]-> multi_manager#334'multits3' -[fire:yesno +12s]-> cof_yesno#342'yesno' -[cond-fire(answer-no):notrigger]-> multi_manager#383'multits5' -[fire:5teledestts...
- **c_attic** `cof_teleport` 'dateleport1' #59 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#58 {player-touch (once)} -[fire]-> multi_manager#49'multidadoc2' -[fire:multidadoc +0.5s]-> multi_manager#48'multidadoc' -[fire:dateleport1 +5s]-> cof_teleport#59'dateleport1'
- **c_basement** `cof_teleport` 'fittjaevel' #241 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#240 {player-touch (once)} -[fire]-> multi_manager#614'multiirlap' -[fire:multibass1 +15s]-> multi_manager#415'multibass1' -[fire:fittjaevel +49s]-> cof_teleport#241'fittjaevel'
- **c_bridge** `cof_teleport` 'telemia' #204 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#165 {player-touch (once)} -[fire]-> multi_manager#195'multikuken' -[fire:telemia +95s]-> cof_teleport#204'telemia'
- **c_broscene** `cof_teleport` 'kanalkuk2' #294 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#150 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#180'multibrorsan' -[fire:kanalkuk2 +21.5s]-> cof_teleport#294'kanalkuk2'
- **c_city** `cof_teleport` 'telelole22' #174 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#171 {player-touch (once)} -[fire]-> multi_manager#175'multiall1' -[fire:telelole22]-> cof_teleport#174'telelole22'
- **c_city** `cof_teleport` 'teletvo1c' #254 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#707 {player-touch (once)} -[fire]-> multi_manager#233'multikatteh' -[fire:multikatteh2 +0.5s]-> multi_manager#250'multikatteh2' -[fire:teletvo1c]-> cof_teleport#254'teletvo1c'
- **c_city** `cof_teleport` 'teletvo1c2' #255 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#707 {player-touch (once)} -[fire]-> multi_manager#233'multikatteh' -[fire:multikatteh2 +0.5s]-> multi_manager#250'multikatteh2' -[fire:teletvo1c2 +15s]-> cof_teleport#255'teletvo1c2'
- **c_city2** `cof_teleport` 'teleziee' #132 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#130 {player-touch (once)} -[fire]-> multi_manager#690'multimarend' -[fire:teleziee +1s]-> cof_teleport#132'teleziee'
- **c_ending1** `cof_teleport` 'teledes_tele' #332 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#259 {player-touch (once)} -[fire]-> multi_manager#253'multibokam0' -[fire:multibokam +0.5s]-> multi_manager#254'multibokam' -[fire:multisu +14s]-> multi_manager#308'multisu' -[fire:teledes_tele +12.5s]-> cof_teleport#332'teledes_tele'
- **c_ending1b** `cof_teleport` 'teledes_tele' #107 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#4 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#85'multisu' -[fire:teledes_tele]-> cof_teleport#107'teledes_tele'
- **c_ending1c** `cof_teleport` 'tele_niceye' #743 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#744 {player-touch (once) [master=multiscr1nej]} -[fire]-> cof_teleport#743'tele_niceye'
- **c_ending2** `cof_teleport` 'teledes_tele' #327 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#254 {player-touch (once)} -[fire]-> multi_manager#248'multibokam0' -[fire:multibokam +0.5s]-> multi_manager#249'multibokam' -[fire:multisu +14s]-> multi_manager#303'multisu' -[fire:teledes_tele +12.5s]-> cof_teleport#327'teledes_tele'
- **c_ending2b** `cof_teleport` 'teledes_tele' #107 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#4 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#85'multisu' -[fire:teledes_tele]-> cof_teleport#107'teledes_tele'
- **c_ending2c** `cof_teleport` 'tele_niceye' #743 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#739 {player-touch (once) [master=multiscr1nej]} -[fire]-> cof_teleport#743'tele_niceye'
- **c_ending3** `cof_teleport` 'teledes_tele' #331 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#258 {player-touch (once)} -[fire]-> multi_manager#252'multibokam0' -[fire:multibokam +0.5s]-> multi_manager#253'multibokam' -[fire:multisu +14s]-> multi_manager#307'multisu' -[fire:teledes_tele +12.5s]-> cof_teleport#331'teledes_tele'
- **c_ending3b** `cof_teleport` 'teledes_tele' #107 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#4 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#85'multisu' -[fire:teledes_tele]-> cof_teleport#107'teledes_tele'
- **c_ending3c** `cof_teleport` 'tele_niceye' #739 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#735 {player-touch (once) [master=multiscr1nej]} -[fire]-> cof_teleport#739'tele_niceye'
- **c_ending4** `cof_teleport` 'teledes_tele' #323 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#254 {player-touch (once)} -[fire]-> multi_manager#248'multibokam0' -[fire:multibokam +0.5s]-> multi_manager#249'multibokam' -[fire:multisu +14s]-> multi_manager#303'multisu' -[fire:teledes_tele +20.2s]-> cof_teleport#323'teledes_tele'
- **c_ending5** `cof_teleport` 'tellemenow' #101 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#65 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#102'multistartis' -[fire:tellemenow +21s]-> cof_teleport#101'tellemenow'
- **c_ending5b** `cof_teleport` 'lollipopp_t' #252 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#3 {player-touch (once)} -[fire]-> cof_teleport#252'lollipopp_t'
- **c_intro** `cof_teleport` 'kunitei2' #346 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#381 {player-touch (once)} -[fire]-> multi_manager#423'multikattej' -[fire:kunitei2 +133s]-> cof_teleport#346'kunitei2'
- **c_nightmare** `cof_teleport` 'pulla' #254 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#159 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#210'nightmulti' -[fire:pulla +40s]-> cof_teleport#254'pulla'
- **c_nightmare** `cof_teleport` 'teleportea45' #428 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#432 {player-touch (once)} -[fire]-> multi_manager#430'multispotea' -[fire:multispotea2 +1.5s]-> multi_manager#437'multispotea2' -[fire:teleportea45]-> cof_teleport#428'teleportea45'
- **c_nightmare** `cof_teleport` 'teleportea' #429 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#432 {player-touch (once)} -[fire]-> multi_manager#430'multispotea' -[fire:multispotea2 +1.5s]-> multi_manager#437'multispotea2' -[fire:teleportea +5s]-> cof_teleport#429'teleportea'
- **c_nightmare** `cof_teleport` 'beforetele' #434 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#432 {player-touch (once)} -[fire]-> multi_manager#430'multispotea' -[fire:beforetele]-> cof_teleport#434'beforetele'
- **c_park3** `cof_teleport` 'isteleport1' #459 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#499'tro_inte' {player-touch (once) [starts disabled (iuser1), enabled by cof_triggeron]} -[fire]-> multi_manager#495'multiirlsophie' -[fire:isteleport1 +1s]-> cof_teleport#459'isteleport1'
- **c_park3** `cof_teleport` 'isteleport2' #460 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#499'tro_inte' {player-touch (once) [starts disabled (iuser1), enabled by cof_triggeron]} -[fire]-> multi_manager#456'multiirlsophie' -[fire:isteleport2 +11s]-> cof_teleport#460'isteleport2'
- **c_roofs** `cof_teleport` 'teleportfall' #189 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#186 {player-touch (once)} -[fire]-> multi_manager#188'multifallheh' -[fire:teleportfall +0.6s]-> cof_teleport#189'teleportfall'
- **c_roofs** `cof_teleport` 'teleport2' #678 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#265 {player-touch (once)} -[fire]-> multi_manager#560'multiroofscene' -[fire:teleport2 +357.9s]-> cof_teleport#678'teleport2'
- **c_roofs** `cof_teleport` 'teleport1' #679 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#265 {player-touch (once)} -[fire]-> multi_manager#560'multiroofscene' -[fire:teleport1]-> cof_teleport#679'teleport1'
- **c_roofs2** `cof_teleport` 'teleportfall' #180 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#177 {player-touch (once)} -[fire]-> multi_manager#179'multifallheh' -[fire:teleportfall +0.6s]-> cof_teleport#180'teleportfall'
- **c_roofs2** `cof_teleport` 'teleportia3' #292 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_multiple#294'tmultiei' {player-touch (multiple)} -[fire]-> cof_yesno#246'ryesno' -[fire]-> multi_manager#247'multiescape' -[fire:teleportia3 +5s]-> cof_teleport#292'teleportia3'
- **c_roofs2** `cof_teleport` 'teleportia2' #317 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#244 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#245'multistartitall' -[fire:teleportia2]-> cof_teleport#317'teleportia2'
- **c_start** `cof_teleport` 'kahshe' #415 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#148 {player-touch (once)} -[fire]-> multi_manager#601'slower_eyes_mm' -[fire:kahshe +41s]-> cof_teleport#415'kahshe'
- **c_start** `cof_teleport` 'mainteleport' #536 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#147 {player-touch (once)} -[fire]-> multi_manager#200'multiscene3' -[fire:mainteleport +0.5s]-> cof_teleport#536'mainteleport'
- **c_start** `cof_teleport` '1teleport' #537 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#120 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#499'multiscene1' -[fire:1teleport +44s]-> cof_teleport#537'1teleport'
- **c_start** `cof_teleport` '2teleport' #540 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#147 {player-touch (once)} -[fire]-> multi_manager#200'multiscene3' -[fire:2teleport +15.4s]-> cof_teleport#540'2teleport'
- **c_subway2st1** `cof_teleport` 'tbteleport1h' #557 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#556 {player-touch (once)} -[fire]-> multi_manager#528'multitban1lol' -[fire:multitban1 +0.5s]-> multi_manager#554'multitban1' -[fire:tbteleport1h]-> cof_teleport#557'tbteleport1h'
- **c_subway2st1** `cof_teleport` 'tbteleport2h' #558 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#556 {player-touch (once)} -[fire]-> multi_manager#528'multitban1lol' -[fire:multitban1 +0.5s]-> multi_manager#554'multitban1' -[fire:tbteleport2h +24s]-> cof_teleport#558'tbteleport2h'
- **c_subway2st1** `cof_teleport` 'tb2teleport1' #578 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#725 {player-touch (once)} -[fire]-> multi_manager#573'multitb2lol' -[fire:multitb2 +0.5s]-> multi_manager#574'multitb2' -[fire:tb2teleport1]-> cof_teleport#578'tb2teleport1'
- **c_subway2st1** `cof_teleport` 'tb2teleport2' #579 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#725 {player-touch (once)} -[fire]-> multi_manager#573'multitb2lol' -[fire:multitb2 +0.5s]-> multi_manager#574'multitb2' -[fire:tb2teleport2 +11s]-> cof_teleport#579'tb2teleport2'
- **c_subway2st1** `cof_teleport` '2teleportion' #621 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#631 {player-touch (once)} -[fire]-> multi_manager#629'multigrejer' -[fire:2teleportion]-> cof_teleport#621'2teleportion'
- **c_subway2st3** `cof_teleport` 'telechange' #283 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#252'triggercutscene' {player-touch (once) [starts disabled (iuser1), enabled by cof_triggeron]} -[fire]-> multi_manager#253'multitscene' -[fire:multitscene2 +0.5s]-> multi_manager#281'multitscene2' -[fire:telechange +23s]-> cof_teleport#283'telechange'
- **c_subway2st3** `cof_teleport` 'telechange2' #287 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#252'triggercutscene' {player-touch (once) [starts disabled (iuser1), enabled by cof_triggeron]} -[fire]-> multi_manager#253'multitscene' -[fire:multitscene2 +0.5s]-> multi_manager#281'multitscene2' -[fire:telechange2]-> cof_teleport#287'telechange2'
- **c_subway2st3** `cof_teleport` 'traintele' #357 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#356'traintrigger' {player-touch (once) [starts disabled (iuser1), enabled by cof_triggeron]} -[fire]-> cof_teleport#357'traintele'
- **c_subway2st3** `cof_teleport` 'teleblack' #361 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#252'triggercutscene' {player-touch (once) [starts disabled (iuser1), enabled by cof_triggeron]} -[fire]-> multi_manager#253'multitscene' -[fire:multitscene2 +0.5s]-> multi_manager#281'multitscene2' -[fire:teleblack +2s]-> cof_teleport#361'teleblack'
- **c_subwaysick2** `cof_teleport` 'isteleport1' #277 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#251 {player-touch (once)} -[fire]-> multi_manager#279'multiirldoctor' -[fire:isteleport1 +1s]-> cof_teleport#277'isteleport1'
- **c_subwaysick2** `cof_teleport` 'isteleportx' #377 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#251 {player-touch (once)} -[fire]-> multi_manager#278'multiirldoctor' -[fire:differentmult +25s]-> multi_manager#374'differentmult' -[fire:isteleportx +47.1s]-> cof_teleport#377'isteleportx'
- **c_trainscene** `cof_teleport` 'teleportoar_df' #181 - STUCK-PLAYER (stranding candidate: only the toucher is moved). Root: trigger_once#106 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#145'multispwnn' -[fire:teleportoar_df +101.5s]-> cof_teleport#181'teleportoar_df'
- **c_apartment1** `cof_camerazoom` 'zoomuknw' #737 - cosmetic (zoom only on the toucher). Root: trigger_once#343 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#621'multispawnhe' -[fire:zoomuknw +5s]-> cof_camerazoom#737'zoomuknw'
- **c_apartment1** `cof_hint` 'hintuse' #760 - cosmetic (hint only to the toucher). Root: trigger_once#759'hinttrigger' {player-touch (once) [starts disabled (iuser1), enabled by cof_triggeron]} -[fire]-> cof_hint#760'hintuse'
- **c_apartment5** `cof_giveitems` 'waterkey' #146 - DESYNC (item/weapon only to the toucher). Root: trigger_once#270 {player-touch (once)} -[fire]-> multi_manager#290'multibadkars2' -[fire:multibadkars +0.5s]-> multi_manager#318'multibadkars' -[fire:waterkey +21s]-> cof_giveitems#146'waterkey'
- **c_apartment5** `cof_camerazoom` 'zoomoutlol' #306 - cosmetic (zoom only on the toucher). Root: trigger_once#123 {player-touch (once)} -[fire]-> multi_manager#125'balkongscene' -[fire:zoomoutlol +0.5s]-> cof_camerazoom#306'zoomoutlol'
- **c_asylum1** `cof_weapontrigger` 'dohaveornot' #341 - DESYNC (checks only the toucher's inventory). Root: trigger_once#310 {player-touch (once)} -[fire]-> multi_manager#400'2multits1' -[fire:multits1 +0.5s]-> multi_manager#311'multits1' -[fire:dohaveornot +46.1s]-> cof_weapontrigger#341'dohaveornot'
- **c_asylum1** `cof_yesno` 'yesno' #342 - DESYNC (prompt only to the toucher). Root: trigger_once#310 {player-touch (once)} -[fire]-> multi_manager#400'2multits1' -[fire:multits1 +0.5s]-> multi_manager#311'multits1' -[fire:dohaveornot +46.1s]-> cof_weapontrigger#341'dohaveornot' -[fire]-> multi_manager#334'multits3' -[fire:yesno +12s]-> cof_yesno#342'yesno'
- **c_asylum1** `cof_giveitems` 'givethekey' #563 - DESYNC (item/weapon only to the toucher). Root: trigger_once#310 {player-touch (once)} -[fire]-> multi_manager#400'2multits1' -[fire:multits1 +0.5s]-> multi_manager#311'multits1' -[fire:dohaveornot +46.1s]-> cof_weapontrigger#341'dohaveornot' -[fire]-> multi_manager#334'multits3' -[fire:yesno +12s]-> cof_yesno#342'yesno' -[cond-fire(answer-no):notrigger]-> multi_manager#383'multits5' -[fire:givethekey ...
- **c_asylum1** `cof_maxhealthchange` 'hitbyp228' #567 - DESYNC (max-health change only on the toucher). Root: trigger_once#310 {player-touch (once)} -[fire]-> multi_manager#400'2multits1' -[fire:multits1 +0.5s]-> multi_manager#311'multits1' -[fire:dohaveornot +46.1s]-> cof_weapontrigger#341'dohaveornot' -[fire]-> multi_manager#334'multits3' -[fire:yesno +12s]-> cof_yesno#342'yesno' -[fire]-> multi_manager#395'multits4' -[fire:hitbyp228 +29.5s]-> cof_maxhealthchan...
- **c_asylum1** `cof_maxhealthchange` 'hitbyrevolver' #568 - DESYNC (max-health change only on the toucher). Root: trigger_once#310 {player-touch (once)} -[fire]-> multi_manager#400'2multits1' -[fire:multits1 +0.5s]-> multi_manager#311'multits1' -[fire:dohaveornot +46.1s]-> cof_weapontrigger#341'dohaveornot' -[fire]-> multi_manager#334'multits3' -[fire:yesno +12s]-> cof_yesno#342'yesno' -[cond-fire(answer-no):notrigger]-> multi_manager#394'multits5' -[fire:hitbyrevolv...
- **c_asylum1** `cof_keypad3` 'fittstruyts' #734 - UI to the activator. Root: trigger_once#2 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#470'multistartas' -[fire:fittstruyts]-> cof_keypad3#734'fittstruyts'
- **c_basement** `cof_camerazoom` 'zoomin' #494 - cosmetic (zoom only on the toucher). Root: trigger_once#240 {player-touch (once)} -[fire]-> multi_manager#614'multiirlap' -[fire:multibass1 +15s]-> multi_manager#489'multibass1' -[fire:zoomin +10s]-> cof_camerazoom#494'zoomin'
- **c_basement** `cof_hint` 'hintphone' #687 - cosmetic (hint only to the toucher). Root: trigger_once#688 {player-touch (once)} -[fire]-> cof_hint#687'hintphone'
- **c_ending1** `cof_simonspeak` 'momvoice' #343 - cosmetic (voice line only to the toucher). Root: trigger_once#258 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#257'multistartis' -[fire:momvoice +0.1s]-> cof_simonspeak#343'momvoice'
- **c_ending1** `cof_simonspeak` 'cancelvoice' #344 - cosmetic (voice line only to the toucher). Root: trigger_once#259 {player-touch (once)} -[fire]-> multi_manager#253'multibokam0' -[fire:cancelvoice]-> cof_simonspeak#344'cancelvoice'
- **c_ending2** `cof_simonspeak` 'momvoice' #338 - cosmetic (voice line only to the toucher). Root: trigger_once#253 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#252'multistartis' -[fire:momvoice +0.1s]-> cof_simonspeak#338'momvoice'
- **c_ending2** `cof_simonspeak` 'cancelvoice' #339 - cosmetic (voice line only to the toucher). Root: trigger_once#254 {player-touch (once)} -[fire]-> multi_manager#248'multibokam0' -[fire:cancelvoice]-> cof_simonspeak#339'cancelvoice'
- **c_ending3** `cof_simonspeak` 'momvoice' #342 - cosmetic (voice line only to the toucher). Root: trigger_once#257 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#256'multistartis' -[fire:momvoice +0.1s]-> cof_simonspeak#342'momvoice'
- **c_ending3** `cof_simonspeak` 'cancelvoice' #343 - cosmetic (voice line only to the toucher). Root: trigger_once#258 {player-touch (once)} -[fire]-> multi_manager#252'multibokam0' -[fire:cancelvoice]-> cof_simonspeak#343'cancelvoice'
- **c_ending4** `cof_simonspeak` 'cancelvoice' #346 - cosmetic (voice line only to the toucher). Root: trigger_once#254 {player-touch (once)} -[fire]-> multi_manager#248'multibokam0' -[fire:cancelvoice]-> cof_simonspeak#346'cancelvoice'
- **c_ending4** `cof_simonspeak` 'momvoice' #347 - cosmetic (voice line only to the toucher). Root: trigger_once#253 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#252'multistartis' -[fire:momvoice +0.1s]-> cof_simonspeak#347'momvoice'
- **c_ending4b** `cof_wheelchairmode` 'wmode' #51 - STUCK-PLAYER/DESYNC (only the toucher enters wheelchair mode). Root: trigger_once#112 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#50'multistart' -[fire:wmode]-> cof_wheelchairmode#51'wmode'
- **c_forest7** `cof_camerazoom` 'fzoom' #342 - cosmetic (zoom only on the toucher). Root: trigger_once#338 {player-touch (once)} -[fire]-> multi_manager#337'multifscene' -[fire:multifscene2 +0.5s]-> multi_manager#339'multifscene2' -[fire:fzoom +2s]-> cof_camerazoom#342'fzoom'
- **c_nightmare** `cof_giveitems` 'kakkaej' #253 - DESYNC (item/weapon only to the toucher). Root: trigger_once#159 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#210'nightmulti' -[fire:kakkaej +40.5s]-> cof_giveitems#253'kakkaej'
- **c_nightmare** `cof_camerazoom` 'lolzoom1' #575 - cosmetic (zoom only on the toucher). Root: trigger_once#125 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#417'multiintro3' -[fire:lolzoom1 +15s]-> cof_camerazoom#575'lolzoom1'
- **c_park3** `cof_phonecall` 'sophiecall' #347 - DESYNC (only the toucher gets the call). Root: trigger_once#508 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#344'multiparkcall' -[fire:sophiecall]-> cof_phonecall#347'sophiecall'
- **c_park3** `cof_camerazoom` 'iszoom1' #482 - cosmetic (zoom only on the toucher). Root: trigger_once#499'tro_inte' {player-touch (once) [starts disabled (iuser1), enabled by cof_triggeron]} -[fire]-> multi_manager#456'multiirlsophie' -[fire:iszoom1 +2.5s]-> cof_camerazoom#482'iszoom1'
- **c_roofs** `cof_camerazoom` 'zoomstad' #707 - cosmetic (zoom only on the toucher). Root: trigger_once#265 {player-touch (once)} -[fire]-> multi_manager#560'multiroofscene' -[fire:zoomstad +259.8s]-> cof_camerazoom#707'zoomstad'
- **c_sewer2** `cof_keypad` 'keypanel2' #258 - UI to the activator. Root: func_button#259 {player-use (button)} -[fire]-> cof_keypad#258'keypanel2'
- **c_sewer2** `cof_keypad2` 'fittstruyts' #329 - UI to the activator. Root: trigger_once#151 {player-touch (once)} -[fire]-> multi_manager#310'kant' -[fire:fittstruyts]-> cof_keypad2#329'fittstruyts'
- **c_start** `cof_giveitems` 'givemobile' #497 - DESYNC (item/weapon only to the toucher). Root: trigger_once#120 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#499'multiscene1' -[fire:givemobile +2s]-> cof_giveitems#497'givemobile'
- **c_start** `cof_giveitems` 'giveswitchblade' #498 - DESYNC (item/weapon only to the toucher). Root: trigger_once#120 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#499'multiscene1' -[fire:giveswitchblade +4s]-> cof_giveitems#498'giveswitchblade'
- **c_start** `cof_camerazoom` 'zoomin1' #709 - cosmetic (zoom only on the toucher). Root: trigger_once#148 {player-touch (once)} -[fire]-> multi_manager#354'slower_eyes_mm' -[fire:attack_mm +34s]-> multi_manager#392'attack_mm' -[fire:zoomin1 +2s]-> cof_camerazoom#709'zoomin1'
- **c_start** `cof_hint` 'hintinventory' #729 - cosmetic (hint only to the toucher). Root: trigger_once#730 {player-touch (once)} -[fire]-> cof_hint#729'hintinventory'
- **c_start** `cof_hint` 'hintsprint' #731 - cosmetic (hint only to the toucher). Root: trigger_once#734 {player-touch (once)} -[fire]-> cof_hint#731'hintsprint'
- **c_start** `cof_hint` 'hintquick' #732 - cosmetic (hint only to the toucher). Root: trigger_once#855 {player-touch (once)} -[fire]-> cof_hint#732'hintquick'
- **c_start** `cof_hint` 'hintsave' #733 - cosmetic (hint only to the toucher). Root: trigger_once#735 {player-touch (once)} -[fire]-> cof_hint#733'hintsave'
- **c_start** `cof_hint` 'hintmelee' #856 - cosmetic (hint only to the toucher). Root: trigger_once#859 {player-touch (once)} -[fire]-> cof_hint#856'hintmelee'
- **c_start** `cof_hint` 'hintladders' #857 - cosmetic (hint only to the toucher). Root: trigger_once#736 {player-touch (once)} -[fire]-> cof_hint#857'hintladders'
- **c_start** `cof_hint` 'hintdodge' #858 - cosmetic (hint only to the toucher). Root: trigger_once#860 {player-touch (once)} -[fire]-> cof_hint#858'hintdodge'
- **c_start** `cof_hint` 'hintobjective' #862 - cosmetic (hint only to the toucher). Root: trigger_once#865 {player-touch (once)} -[fire]-> cof_hint#862'hintobjective'
- **c_start2** `cof_hint` 'hintflashlight' #333 - cosmetic (hint only to the toucher). Root: trigger_once#334 {player-touch (once)} -[fire]-> cof_hint#333'hintflashlight'
- **c_trainscene** `cof_camerazoom` 'zoom1' #144 - cosmetic (zoom only on the toucher). Root: trigger_once#106 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#145'multispwnn' -[fire:zoom1 +5s]-> cof_camerazoom#144'zoom1'
- **c_trainscene** `cof_camerazoom` 'zoom2' #154 - cosmetic (zoom only on the toucher). Root: trigger_once#106 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#145'multispwnn' -[fire:zoom2 +75s]-> cof_camerazoom#154'zoom2'
- **c_trainscene** `cof_camerazoom` 'zander3' #160 - cosmetic (zoom only on the toucher). Root: trigger_once#106 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#145'multispwnn' -[fire:zander3 +90s]-> cof_camerazoom#160'zander3'
- plus **251 trigger_camera** instances (see the camera table in section 5): c_apartment1 4, c_apartment5 6, c_apartmentsick 7, c_asylum1 28, c_attic 5, c_basement 16, c_bridge 1, c_broscene 6, c_city 4, c_city2 7, c_ending1c 2, c_ending2c 2, c_ending3c 2, c_ending4b 5, c_ending5 4, c_ending5c 9, c_forest7 1, c_intro 10, c_nightmare 28, c_park3 4, c_roofs 52, c_roofs2 3, c_start 17, c_subway2st1 5, c_subway2st3 2, c_subwaysick2 13, c_trainscene 8

### 4.3 `FIRST`

- **c_apartment1** `cof_objective` 'objectivee' #764 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#763 {player-touch (once)} -[fire]-> cof_objective#764'objectivee'
- **c_apartment3** `cof_addcodenote` 'addcode' #565 - DESYNC (code note to host). Root: func_button#345'menhora' {player-use (button) [needs item Videotape; also fired by name]} -[fire]-> multi_manager#317'multitortyr' -[fire:addcode +30s]-> cof_addcodenote#565'addcode'
- **c_apartment3** `cof_objective` 'objectiveee' #579 - DESYNC (host's objective only, unless the engine shares it). Root: func_button#345'menhora' {player-use (button) [needs item Videotape; also fired by name]} -[fire]-> multi_manager#341'multitortyr' -[fire:objectiveee +45s]-> cof_objective#579'objectiveee'
- **c_apartment4** `cof_objective` 'objectiveee' #469 - DESYNC (host's objective only, unless the engine shares it). Root: item_key#250 {player-use (pickup)} -[fire]-> cof_objective#469'objectiveee'
- **c_apartment5** `cof_objective` 'objectiveee' #334 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#270 {player-touch (once)} -[fire]-> multi_manager#290'multibadkars2' -[fire:multibadkars +0.5s]-> multi_manager#314'multibadkars' -[fire:objectiveee +22s]-> cof_objective#334'objectiveee'
- **c_apartmentsick** `cof_objective` 'objectiveee' #477 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#53 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#362'multitare' -[fire:objectiveee]-> cof_objective#477'objectiveee'
- **c_asylum1** `cof_doctorweaponset` 'thisisp345' #745 - DESYNC (host's doctor-weapon field set). Root: trigger_once#310 {player-touch (once)} -[fire]-> multi_manager#400'2multits1' -[fire:multits1 +0.5s]-> multi_manager#311'multits1' -[fire:dohaveornot +46.1s]-> cof_weapontrigger#341'dohaveornot' -[fire]-> multi_manager#334'multits3' -[fire:yesno +12s]-> cof_yesno#342'yesno' -[fire]-> multi_manager#395'multits4' -[fire:thisisp345]-> cof_doctorweaponset#745...
- **c_asylum1** `cof_doctorweaponset` 'thisrevolver' #746 - DESYNC (host's doctor-weapon field set). Root: trigger_once#310 {player-touch (once)} -[fire]-> multi_manager#400'2multits1' -[fire:multits1 +0.5s]-> multi_manager#311'multits1' -[fire:dohaveornot +46.1s]-> cof_weapontrigger#341'dohaveornot' -[fire]-> multi_manager#334'multits3' -[fire:yesno +12s]-> cof_yesno#342'yesno' -[cond-fire(answer-no):notrigger]-> multi_manager#394'multits5' -[fire:thisrevolve...
- **c_asylum1** `cof_objective` 'objectiveee' #776 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#775 {player-touch (once)} -[fire]-> cof_objective#776'objectiveee'
- **c_asylum1** `cof_objective` 'objectiveee2' #778 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#777 {player-touch (once)} -[fire]-> cof_objective#778'objectiveee2'
- **c_asylum1** `cof_objective` 'objectiveee3' #779 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#780'triggerobj' {player-touch (once) [starts disabled (iuser1), enabled by cof_triggeron]} -[fire]-> cof_objective#779'objectiveee3'
- **c_asylum1dark** `cof_objective` 'objectiveee' #322 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#323 {player-touch (once)} -[fire]-> cof_objective#322'objectiveee'
- **c_asylum1day** `cof_objective` 'objectiveee' #412 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#411 {player-touch (once)} -[fire]-> cof_objective#412'objectiveee'
- **c_asylumb** `cof_puzzlebar5` '' #58 - WRONG-PLAYER-VISUAL (host's view). Root: cof_puzzlebar5#58 {player-use (puzzle)}
- **c_asylumb** `cof_puzzlebar4` '' #59 - WRONG-PLAYER-VISUAL (host's view). Root: cof_puzzlebar4#59 {player-use (puzzle)}
- **c_asylumb** `cof_puzzlebar3` '' #60 - WRONG-PLAYER-VISUAL (host's view). Root: cof_puzzlebar3#60 {player-use (puzzle)}
- **c_asylumb** `cof_puzzlebar2` '' #61 - WRONG-PLAYER-VISUAL (host's view). Root: cof_puzzlebar2#61 {player-use (puzzle)}
- **c_asylumb** `cof_puzzlebar` '' #62 - WRONG-PLAYER-VISUAL (host's view). Root: cof_puzzlebar#62 {player-use (puzzle)}
- **c_asylumb** `cof_objective` 'objectiveee' #64 - DESYNC (host's objective only, unless the engine shares it). Root: cof_puzzlebarbutton#35 {player-use (puzzle button)} -[fire]-> multi_manager#65'restore_power' -[fire:objectiveee +2s]-> cof_objective#64'objectiveee'
- **c_attic** `cof_doctorweapontrigger` 'triggerdocbody' #114 - DESYNC (branch chosen from the host's doctor-weapon field). Root: trigger_once#82'toncedeath' {player-touch (once)} -[fire]-> multi_manager#83'multiyoudie' -[fire:triggerdocbody +0.3s]-> cof_doctorweapontrigger#114'triggerdocbody'
- **c_attic** `cof_doctorweaponset` 'dochasrevolver' #238 - DESYNC (host's doctor-weapon field set). Root: trigger_once#58 {player-touch (once)} -[fire]-> multi_manager#49'multidadoc2' -[fire:multidadoc +0.5s]-> multi_manager#68'multidadoc' -[fire:docweaponsec +5.1s]-> cof_doctorweapontrigger#240'docweaponsec' -[cond-fire(message):message]-> cof_doctorweaponset#238'dochasrevolver'
- **c_attic** `cof_doctorweaponset` 'dochasp345' #239 - DESYNC (host's doctor-weapon field set). Root: trigger_once#58 {player-touch (once)} -[fire]-> multi_manager#49'multidadoc2' -[fire:multidadoc +0.5s]-> multi_manager#68'multidadoc' -[fire:docweaponsec +5.1s]-> cof_doctorweapontrigger#240'docweaponsec' -[fire]-> cof_doctorweaponset#239'dochasp345'
- **c_attic** `cof_doctorweapontrigger` 'docweaponsec' #240 - DESYNC (branch chosen from the host's doctor-weapon field). Root: trigger_once#58 {player-touch (once)} -[fire]-> multi_manager#49'multidadoc2' -[fire:multidadoc +0.5s]-> multi_manager#68'multidadoc' -[fire:docweaponsec +5.1s]-> cof_doctorweapontrigger#240'docweaponsec'
- **c_basement** `cof_keypad4` 'keypad4' #676 - WRONG-PLAYER (keypad UI to host). Root: trigger_once#240 {player-touch (once)} -[fire]-> multi_manager#614'multiirlap' -[fire:multibass1 +15s]-> multi_manager#368'multibass1' -[fire:keypad4]-> cof_keypad4#676'keypad4'
- **c_bridge** `cof_objective` 'objectiveee' #345 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#344 {player-touch (once)} -[fire]-> cof_objective#345'objectiveee'
- **c_bridge** `cof_clothesmenu` 'clothes' #364 - WRONG-PLAYER (host gets the clothes menu/camera even if the guest pressed the button). Root: func_button#359 {player-use (button)} -[fire]-> cof_clothesmenu#364'clothes'
- **c_city** `cof_objective` 'objectivee' #734 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#707 {player-touch (once)} -[fire]-> multi_manager#233'multikatteh' -[fire:multikatteh2 +0.5s]-> multi_manager#236'multikatteh2' -[fire:objectivee +16s]-> cof_objective#734'objectivee'
- **c_college2** `cof_objective` 'objectiveee' #52 - DESYNC (host's objective only, unless the engine shares it). Root: func_button#9'drastisk' {player-use (button) [also fired by name]} -[fire]-> multi_manager#39'multipoff' -[fire:objectiveee +3s]-> cof_objective#52'objectiveee'
- **c_ending1** `cof_mdlcutscene` 'kambokam1' #252 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: trigger_once#259 {player-touch (once)} -[fire]-> multi_manager#253'multibokam0' -[fire:multibokam +0.5s]-> multi_manager#254'multibokam' -[fire:kambokam1]-> cof_mdlcutscene#252'kambokam1'
- **c_ending1** `cof_mdlcutscene` 'sukams' #307 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: trigger_once#259 {player-touch (once)} -[fire]-> multi_manager#253'multibokam0' -[fire:multibokam +0.5s]-> multi_manager#254'multibokam' -[fire:kambokam1]-> cof_mdlcutscene#252'kambokam1' -[fire]-> cof_mdlcutscene#307'sukams'
- **c_ending1c** `cof_mdlcutscene` 'bkam' #158 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: trigger_once#9 {player-touch (once)} -[fire]-> multi_manager#444'multi_hiss_1' -[fire:multibossscene +20s]-> multi_manager#159'multibossscene' -[fire:bkam]-> cof_mdlcutscene#158'bkam'
- **c_ending1c** `cof_strangle` 'strangle' #187 - WRONG-PLAYER (host is moved into the strangle pose and frozen). Root: func_valve#23'fvalve1' {player-use (valve)} -[fire]-> multi_manager#30'multivalve1' -[fire:countgate]-> game_counter#190'countgate' -[fire]-> multi_manager#186'multisd' -[fire:strangle +3.5s]-> cof_strangle#187'strangle'
- **c_ending1c** `cof_mdlcutscene` 'ckamera1' #195 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: func_valve#23'fvalve1' {player-use (valve)} -[fire]-> multi_manager#30'multivalve1' -[fire:countgate]-> game_counter#190'countgate' -[fire]-> multi_manager#186'multisd' -[fire:strangle +3.5s]-> cof_strangle#187'strangle' -[fire]-> multi_manager#191'multisdeath' -[fire:multiend1 +8s]-> multi_manager#681'multiend1' -[fire:multicredits +202s]-> multi_manager...
- **c_ending1c** `cof_mdlcutscene` 'emdlkam' #691 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: func_valve#23'fvalve1' {player-use (valve)} -[fire]-> multi_manager#30'multivalve1' -[fire:countgate]-> game_counter#190'countgate' -[fire]-> multi_manager#186'multisd' -[fire:strangle +3.5s]-> cof_strangle#187'strangle' -[fire]-> multi_manager#191'multisdeath' -[fire:multiend1 +8s]-> multi_manager#690'multiend1' -[fire:emdlkam]-> cof_mdlcutscene#691'emdl...
- **c_ending2** `cof_mdlcutscene` 'kambokam1' #247 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: trigger_once#254 {player-touch (once)} -[fire]-> multi_manager#248'multibokam0' -[fire:multibokam +0.5s]-> multi_manager#249'multibokam' -[fire:kambokam1]-> cof_mdlcutscene#247'kambokam1'
- **c_ending2** `cof_mdlcutscene` 'sukams' #302 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: trigger_once#254 {player-touch (once)} -[fire]-> multi_manager#248'multibokam0' -[fire:multibokam +0.5s]-> multi_manager#249'multibokam' -[fire:kambokam1]-> cof_mdlcutscene#247'kambokam1' -[fire]-> cof_mdlcutscene#302'sukams'
- **c_ending2c** `cof_mdlcutscene` 'bkam' #158 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: trigger_once#9 {player-touch (once)} -[fire]-> multi_manager#393'multi_hiss_1' -[fire:multibossscene +20s]-> multi_manager#159'multibossscene' -[fire:bkam]-> cof_mdlcutscene#158'bkam'
- **c_ending2c** `cof_strangle` 'strangle' #186 - WRONG-PLAYER (host is moved into the strangle pose and frozen). Root: func_valve#23'fvalve1' {player-use (valve)} -[fire]-> multi_manager#30'multivalve1' -[fire:countgate]-> game_counter#189'countgate' -[fire]-> multi_manager#744'multisd' -[fire:strangle +3.5s]-> cof_strangle#186'strangle'
- **c_ending2c** `cof_mdlcutscene` 'ckamera1' #194 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: func_valve#23'fvalve1' {player-use (valve)} -[fire]-> multi_manager#30'multivalve1' -[fire:countgate]-> game_counter#189'countgate' -[fire]-> multi_manager#744'multisd' -[fire:strangle +3.5s]-> cof_strangle#186'strangle' -[fire]-> multi_manager#190'multisdeath' -[fire:multiend1 +8s]-> multi_manager#631'multiend1' -[fire:multicredits +150s]-> multi_manager...
- **c_ending2c** `cof_mdlcutscene` 'emdlkam' #642 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: func_valve#23'fvalve1' {player-use (valve)} -[fire]-> multi_manager#30'multivalve1' -[fire:countgate]-> game_counter#189'countgate' -[fire]-> multi_manager#744'multisd' -[fire:strangle +3.5s]-> cof_strangle#186'strangle' -[fire]-> multi_manager#190'multisdeath' -[fire:multiend1 +8s]-> multi_manager#641'multiend1' -[fire:emdlkam]-> cof_mdlcutscene#642'emdl...
- **c_ending3** `cof_mdlcutscene` 'kambokam1' #251 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: trigger_once#258 {player-touch (once)} -[fire]-> multi_manager#252'multibokam0' -[fire:multibokam +0.5s]-> multi_manager#253'multibokam' -[fire:kambokam1]-> cof_mdlcutscene#251'kambokam1'
- **c_ending3** `cof_mdlcutscene` 'sukams' #306 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: trigger_once#258 {player-touch (once)} -[fire]-> multi_manager#252'multibokam0' -[fire:multibokam +0.5s]-> multi_manager#253'multibokam' -[fire:kambokam1]-> cof_mdlcutscene#251'kambokam1' -[fire]-> cof_mdlcutscene#306'sukams'
- **c_ending3c** `cof_mdlcutscene` 'bkam' #158 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: trigger_once#9 {player-touch (once)} -[fire]-> multi_manager#393'multi_hiss_1' -[fire:multibossscene +20s]-> multi_manager#159'multibossscene' -[fire:bkam]-> cof_mdlcutscene#158'bkam'
- **c_ending3c** `cof_strangle` 'strangle' #186 - WRONG-PLAYER (host is moved into the strangle pose and frozen). Root: func_valve#23'fvalve1' {player-use (valve)} -[fire]-> multi_manager#30'multivalve1' -[fire:countgate]-> game_counter#189'countgate' -[fire]-> multi_manager#740'multisd' -[fire:strangle +3.5s]-> cof_strangle#186'strangle'
- **c_ending3c** `cof_mdlcutscene` 'ckamera1' #194 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: func_valve#23'fvalve1' {player-use (valve)} -[fire]-> multi_manager#30'multivalve1' -[fire:countgate]-> game_counter#189'countgate' -[fire]-> multi_manager#740'multisd' -[fire:strangle +3.5s]-> cof_strangle#186'strangle' -[fire]-> multi_manager#190'multisdeath' -[fire:multiend1 +8s]-> multi_manager#631'multiend1' -[fire:multicredits +117s]-> multi_manager...
- **c_ending3c** `cof_mdlcutscene` 'emdlkam' #642 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: func_valve#23'fvalve1' {player-use (valve)} -[fire]-> multi_manager#30'multivalve1' -[fire:countgate]-> game_counter#189'countgate' -[fire]-> multi_manager#740'multisd' -[fire:strangle +3.5s]-> cof_strangle#186'strangle' -[fire]-> multi_manager#190'multisdeath' -[fire:multiend1 +8s]-> multi_manager#641'multiend1' -[fire:emdlkam]-> cof_mdlcutscene#642'emdl...
- **c_ending4** `cof_mdlcutscene` 'kambokam1' #247 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: trigger_once#254 {player-touch (once)} -[fire]-> multi_manager#248'multibokam0' -[fire:multibokam +0.5s]-> multi_manager#249'multibokam' -[fire:kambokam1]-> cof_mdlcutscene#247'kambokam1'
- **c_ending4** `cof_mdlcutscene` 'sukams' #302 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: trigger_once#254 {player-touch (once)} -[fire]-> multi_manager#248'multibokam0' -[fire:multibokam +0.5s]-> multi_manager#249'multibokam' -[fire:kambokam1]-> cof_mdlcutscene#247'kambokam1' -[fire]-> cof_mdlcutscene#302'sukams'
- **c_ending4** `cof_mdlcutscene` 'bsimkam' #336 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: trigger_once#254 {player-touch (once)} -[fire]-> multi_manager#248'multibokam0' -[fire:multibokam +0.5s]-> multi_manager#249'multibokam' -[fire:kambokam1]-> cof_mdlcutscene#247'kambokam1' -[fire]-> cof_mdlcutscene#302'sukams' -[fire]-> cof_mdlcutscene#336'bsimkam'
- **c_ending4** `cof_mdlcutscene` 'sukams2' #338 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: trigger_once#254 {player-touch (once)} -[fire]-> multi_manager#248'multibokam0' -[fire:multibokam +0.5s]-> multi_manager#249'multibokam' -[fire:kambokam1]-> cof_mdlcutscene#247'kambokam1' -[fire]-> cof_mdlcutscene#302'sukams' -[fire]-> cof_mdlcutscene#336'bsimkam' -[fire]-> cof_mdlcutscene#338'sukams2'
- **c_ending4** `cof_greyfade` 'fadeoutbw' #354 - WRONG-PLAYER-VISUAL (host fades). Root: trigger_once#254 {player-touch (once)} -[fire]-> multi_manager#248'multibokam0' -[fire:multibokam +0.5s]-> multi_manager#249'multibokam' -[fire:multisu +14s]-> multi_manager#303'multisu' -[fire:fadeoutbw +11s]-> cof_greyfade#354'fadeoutbw'
- **c_ending4b** `cof_mdlcutscene` 'ckamera1' #231 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: trigger_once#462 {player-touch (once) [covers info_player_start: fires at map start/spawn; master=multiscr14]} -[fire]-> multi_manager#230'multiending5' -[fire:multicredits +165s]-> multi_manager#232'multicredits' -[fire:ckamera1]-> cof_mdlcutscene#231'ckamera1'
- **c_start** `cof_objective` 'objective1' #863 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#861 {player-touch (once)} -[fire]-> cof_objective#863'objective1'
- **c_start** `cof_objective` 'objective2' #864 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#866 {player-touch (once)} -[fire]-> cof_objective#864'objective2'
- **c_start** `cof_clothesmenu` 'clothes' #873 - WRONG-PLAYER (host gets the clothes menu/camera even if the guest pressed the button). Root: func_button#868 {player-use (button)} -[fire]-> cof_clothesmenu#873'clothes'
- **c_subway1st1** `cof_phonedisable` 'disablefuck' #408 - DESYNC (host's phone disabled, not the toucher's). Root: trigger_once#409'loltrigger' {player-touch (once) [starts disabled (iuser1), enabled by cof_triggeron]} -[fire]-> cof_phonedisable#408'disablefuck'
- **c_subway1st1** `cof_objective` 'objectiveee' #574 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#575 {player-touch (once)} -[fire]-> cof_objective#574'objectiveee'
- **c_subway2st1** `cof_objective` 'objectiveee1' #732 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#556 {player-touch (once)} -[fire]-> multi_manager#528'multitban1lol' -[fire:multitban1 +0.5s]-> multi_manager#554'multitban1' -[fire:objectiveee1 +26s]-> cof_objective#732'objectiveee1'
- **c_subway2st1** `cof_objective` 'objectiveee2' #733 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#725 {player-touch (once)} -[fire]-> multi_manager#573'multitb2lol' -[fire:multitb2 +0.5s]-> multi_manager#734'multitb2' -[fire:objectiveee2 +13s]-> cof_objective#733'objectiveee2'
- **c_subway2st2** `cof_objective` 'objectiveeee' #637 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#636 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> cof_objective#637'objectiveeee'
- **c_subway2st3** `cof_mdlcutscene` 'tkam3cmdl' #343 - WRONG-PLAYER-VISUAL (cutscene on host, loops for others). Root: trigger_once#252'triggercutscene' {player-touch (once) [starts disabled (iuser1), enabled by cof_triggeron]} -[fire]-> multi_manager#253'multitscene' -[fire:multitscene2 +0.5s]-> multi_manager#280'multitscene2' -[fire:tkam3cmdl +7s]-> cof_mdlcutscene#343'tkam3cmdl'
- **c_subway2st3** `cof_objective` 'objectiveee1' #457 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#456 {player-touch (once)} -[fire]-> cof_objective#457'objectiveee1'
- **c_subway2st3** `cof_objective` 'objectiveee2' #458 - DESYNC (host's objective only, unless the engine shares it). Root: func_button#301'buttkuk1' {player-use (button) [needs item fuse; also fired by name]} -[fire]-> multi_manager#300'multiunlo' -[fire:cunters]-> game_counter#309'cunters' -[fire]-> multi_manager#310'multisudoor' -[fire:objectiveee2]-> cof_objective#458'objectiveee2'
- **c_village3** `cof_lookat` 'lookatit' #285 - WRONG-PLAYER-VISUAL (host's view is turned). Root: trigger_once#286 {player-touch (once)} -[fire]-> multi_manager#303'multilook' -[fire:lookatit]-> cof_lookat#285'lookatit'
- **c_village3** `cof_objective` 'objectiveee' #326 - DESYNC (host's objective only, unless the engine shares it). Root: trigger_once#286 {player-touch (once)} -[fire]-> multi_manager#303'multilook' -[fire:objectiveee]-> cof_objective#326'objectiveee'

### 4.4 `NONPLAYER`

- **c_attic** `cof_teleport` 'teleportblack' #117 - INERT: cof_teleport ignores a non-player activator (monster (non-player)) - no teleport in SP either (CERTAIN). Root: monster_doctorboss#232'doctorboss' {death/monster} -[monster-trigger:death:TriggerTarget]-> multi_manager#73'multidscene' -[fire:teleportblack]-> cof_teleport#117'teleportblack'
- **c_attic** `cof_teleport` 'deteleport1' #146 - INERT: cof_teleport ignores a non-player activator (monster (non-player)) - no teleport in SP either (CERTAIN). Root: monster_doctorboss#232'doctorboss' {death/monster} -[monster-trigger:death:TriggerTarget]-> multi_manager#73'multidscene' -[fire:multidydoc]-> multi_manager#155'multidydoc' -[fire:multiirldoctor +4s]-> multi_manager#217'multiirldoctor' -[fire:multistamp +54s]-> multi_manager#144'multistamp' -[fire:deteleport1 +12s]-> cof_teleport#146'deteleport1'
- **c_ending4b** `cof_teleport` 'fitttth' #463 - INERT: cof_teleport ignores a non-player activator (monster (non-player)) - no teleport in SP either (CERTAIN). Root: monster_generic#123'justhealth' {death/monster} -[monster-trigger:death:TriggerTarget]-> multi_manager#459'fuckoff' -[fire:fitttth#2 +8s]-> cof_teleport#463'fitttth'
- **c_roofs2** `cof_teleport` 'teleportia4' #258 - INERT: cof_teleport ignores a non-player activator (monster (non-player)) - no teleport in SP either (CERTAIN). Root: monster_roofboss#273'carcassboss' {death/monster} -[monster-trigger:death:TriggerTarget]-> multi_manager#275'multikill' -[fire:multikill2 +0.5s]-> multi_manager#259'multikill2' -[fire:teleportia4 +5s]-> cof_teleport#258'teleportia4'
- **c_roofs2** `cof_teleport` 'teleportia42' #298 - INERT: cof_teleport ignores a non-player activator (monster (non-player)) - no teleport in SP either (CERTAIN). Root: monster_roofboss#273'carcassboss' {death/monster} -[monster-trigger:death:TriggerTarget]-> multi_manager#275'multikill' -[fire:multikill2 +0.5s]-> multi_manager#296'multikill2' -[fire:teleportia42 +4.5s]-> cof_teleport#298'teleportia42'
- **c_sewer3** `cof_teleport` 'teleport_till_lik' #339 - INERT: cof_teleport ignores a non-player activator (monster (non-player)) - no teleport in SP either (CERTAIN). Root: monster_sewerboss#146'dsasdaeas' {death/monster} -[monster-trigger:death:TriggerTarget]-> multi_manager#118'whenIdie' -[fire:teleport_till_lik +10.1s]-> cof_teleport#339'teleport_till_lik'
- plus **17 trigger_camera** instances (see the camera table in section 5): c_apartmentsick 4, c_attic 2, c_roofs2 2, c_sewer3 2, c_start 7

### 4.5 `PHONE-VOLUME`

- **c_park3** `cof_phonecall` 'sophiecall' #347 - phone call started by a volume: rings whoever walked in. Root: trigger_once#508 {player-touch (once) [covers info_player_start: fires at map start/spawn]} -[fire]-> multi_manager#344'multiparkcall' -[fire:sophiecall]-> cof_phonecall#347'sophiecall'

### 4.6 `SPAWN-RACE` (start-of-map scene volumes)

- c_apartment1: cof_camerazoom 'zoomuknw', trigger_camera 'aplolkamera1', trigger_camera 'aplolkamera2'
- c_apartmentsick: cof_objective 'objectiveee'
- c_asylum1: cof_keypad3 'fittstruyts'
- c_broscene: cof_teleport 'kanalkuk2', trigger_camera 'dckamera1', trigger_camera 'dckamera2', trigger_camera 'dckamera3', trigger_camera 'dckamera4', trigger_camera 'dckamera5', trigger_camera 'dckamera6'
- c_ending1: cof_simonspeak 'momvoice'
- c_ending1b: cof_teleport 'teledes_tele'
- c_ending1c: cof_teleport 'teleportf5_2'
- c_ending2: cof_simonspeak 'momvoice'
- c_ending2b: cof_teleport 'teledes_tele'
- c_ending2c: cof_teleport 'teleportf5_2'
- c_ending3: cof_simonspeak 'momvoice'
- c_ending3b: cof_teleport 'teledes_tele'
- c_ending3c: cof_teleport 'teleportf5_2'
- c_ending4: cof_simonspeak 'momvoice'
- c_ending4b: cof_mdlcutscene 'ckamera1', cof_wheelchairmode 'wmode', trigger_camera 'conkam', trigger_camera 'e5kam1', trigger_camera 'e5kam2', trigger_camera 'e5kam3', trigger_camera 'statskamera1'
- c_ending5: cof_teleport 'tellemenow', trigger_camera 'ppkam1', trigger_camera 'ppkam2', trigger_camera 'ppkam3', trigger_camera 'ppkam4'
- c_intro: cof_teleport 'kunitei2', trigger_camera '2inkamera2', trigger_camera 'inkamera1', trigger_camera 'inkamera2', trigger_camera 'inkamera2222', trigger_camera 'inkamera22223', trigger_camera 'inkamera4550', trigger_camera 'kkamera1', trigger_camera 'kkamera2', trigger_camera 'kkamera3', trigger_camera 'kkamera4'
- c_nightmare: cof_camerazoom 'lolzoom1', cof_giveitems 'kakkaej', cof_teleport 'pulla', trigger_camera 'kamera10lal', trigger_camera 'kamera11lal', trigger_camera 'kamera12lal', trigger_camera 'kamera13lal', trigger_camera 'kamera14lal', trigger_camera 'kamera15lal', trigger_camera 'kamera16lal', trigger_camera 'kamera17lal', trigger_camera 'kamera18lal', trigger_camera 'kamera19lal', trigger_camera 'kamera1lal', trigger_camera 'kamera2lal', trigger_camera 'kamera3lal', trigger_camera 'kamera4lal', trigger_camera 'kamera5lal', trigger_camera 'kamera6lal', trigger_camera 'kamera7lal', trigger_camera 'kamera8lal', trigger_camera 'kamera9lal', trigger_camera 'kameraheh10', trigger_camera 'kameraheh11', trigger_camera 'kameraheh12', trigger_camera 'kameraheh4', trigger_camera 'kameraheh5', trigger_camera 'kameraheh6', trigger_camera 'kameraheh7', trigger_camera 'kameraheh8', trigger_camera 'kameraheh9'
- c_park3: cof_phonecall 'sophiecall'
- c_roofs2: cof_teleport 'teleportia2', trigger_camera 'rkam4550'
- c_start: cof_giveitems 'givemobile', cof_giveitems 'giveswitchblade', cof_teleport '1teleport', cof_teleport 'mainteleport', trigger_camera 'kamera2', trigger_camera 'kamera3', trigger_camera 'kamera4', trigger_camera 'kamera5', trigger_camera 'kamera6', trigger_camera 'kamera7', trigger_camera 'kamera8', trigger_camera 'kamera9'
- c_subway2st2: cof_objective 'objectiveeee'
- c_trainscene: cof_camerazoom 'zander3', cof_camerazoom 'zoom1', cof_camerazoom 'zoom2', cof_teleport 'teleportoar_df', trigger_camera 'tkam10', trigger_camera 'tkam11', trigger_camera 'tkam12', trigger_camera 'tkam5', trigger_camera 'tkam6', trigger_camera 'tkam7', trigger_camera 'tkam8', trigger_camera 'tkam9'

### 4.7 Doors that a chain kills, disables, re-locks or re-targets (stranding candidates)

`inter_door` moves only its user (per the split-play design notes), but stays usable, so a partner can normally follow. The partner is stranded only when the door is removed or changed after the first player went through. The list shows every `inter_door` that is the object of a `killtarget`, `cof_interdooronoff` (lock toggle), `inter_door_disable` or `cof_inter_door_exitchange`, and the root of that chain. `FAR-SIDE` = the root trigger is nearer the door's exit than the door itself (the first player to go through fires it). Whether a toggle locks or unlocks depends on the door's start state; `inter_door_disable` with `frags 1` is named `enable...` in the maps, so it is LIKELY an unlock.

| map | door | action | root | dist root-door / root-exit | note |
|---|---|---|---|---|---|
| c_apartment1 | inter_door#228'door_office' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#690'gay1' player-use (button) | 0 / 690 |  |
| c_apartment1 | inter_door#228'door_office' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#343 player-touch (once) | 890 / 437 | FAR-SIDE |
| c_apartment1 | inter_door#229'door_office_a' | lock-toggle(cof_interdooronoff newlockedby=assange) | func_button#690'gay1' player-use (button) | 662 / 57 | FAR-SIDE |
| c_apartment1 | inter_door#713'kakaelai' | lock-toggle(cof_interdooronoff newlockedby=lasersvett) | trigger_once#573 player-touch (once) | 32 / 233 |  |
| c_apartment3 | inter_door#47'dede222' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#250 player-use (button) | 48 / 3094 |  |
| c_apartment4 | inter_door#188'fittnanna' | lock-toggle(cof_interdooronoff newlockedby=hornanna) | func_button#300'nanna' player-use (button) | 877 / 59 | FAR-SIDE |
| c_apartment5 | inter_door#58'fittansman' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#270 player-touch (once) | 102 / 154 |  |
| c_apartment5 | inter_door#142'ropelocked' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#268'ropebutton' player-use (button) | 48 / 196 |  |
| c_apartment5 | inter_door#142'ropelocked' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#270 player-touch (once) | 404 / 422 |  |
| c_apartment5 | inter_door#269'fittaea' | KILLED | trigger_once#123 player-touch (once) | 149 / None | door removed |
| c_asylum1 | inter_door#74'staffdoor2' | inter_door_disable(frags=1) | trigger_once#761 player-touch (once) | 30 / 76 |  |
| c_asylum1 | inter_door#232'karatekungen' | KILLED | trigger_once#763'tpsycho' player-touch (once) | 356 / None | door removed |
| c_asylum1 | inter_door#304'elecdoor' | lock-toggle(cof_interdooronoff newlockedby=lockedelec) | func_button#735 player-use (button) | 23 / 1270 |  |
| c_asylum1 | inter_door#480'othersidem' | inter_door_disable(frags=1) | trigger_once#484 player-touch (once) | 27 / 71 |  |
| c_asylum1 | inter_door#482'othersidem2' | inter_door_disable(frags=1) | trigger_once#310 player-touch (once) | 340 / 368 |  |
| c_asylum1 | inter_door#482'othersidem2' | inter_door_disable(frags=1) | func_button#343'givethep228b' player-use (button) | 7 / 63 |  |
| c_asylum1 | inter_door#482'othersidem2' | inter_door_disable(frags=1) | trigger_once#310 player-touch (once) | 340 / 368 |  |
| c_asylumb | inter_door#20'doorchanges' | exit-change | cof_puzzlebarbutton#35 player-use (puzzle button) | 214 / 510 |  |
| c_asylumb | inter_door#20'doorchanges' | exit-change | cof_puzzlebar4#59 player-use (puzzle) | 373 / 361 | FAR-SIDE |
| c_asylumb | inter_door#20'doorchanges' | exit-change | cof_puzzlebar3#60 player-use (puzzle) | 375 / 359 | FAR-SIDE |
| c_attic | inter_door#4'door2change' | exit-change | monster_doctorboss#232'doctorboss' death/monster | 1154 / 2269 |  |
| c_attic | inter_door#4'door2change' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#58 player-touch (once) | 383 / 1644 |  |
| c_attic | inter_door#4'door2change' | lock-toggle(cof_interdooronoff newlockedby=) | monster_doctorboss#232'doctorboss' death/monster | 1154 / 2269 |  |
| c_avlopp | inter_door#275'ladder1door' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#195'ladder1b' player-use (button) | 96 / 391 |  |
| c_avlopp | inter_door#275'ladder1door' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#194 player-touch (once) | 4879 / 5019 |  |
| c_basement | inter_door#236'elecdoor' | lock-toggle(cof_interdooronoff newlockedby=lockedelec) | func_button#678 player-use (button) | 36 / 95 |  |
| c_buscity | inter_door#250'doorbclock' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#372 player-touch (once) | 1256 / 2027 |  |
| c_buscity | inter_door#250'doorbclock' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#373'bbb' player-use (button) | 2 / 2962 |  |
| c_buscity2 | inter_door#253'chdoor1' | lock-toggle(cof_interdooronoff newlockedby=blockach1) | func_breakable#217 break | 15 / 1628 |  |
| c_buscity2 | inter_door#253'chdoor1' | lock-toggle(cof_interdooronoff newlockedby=blockach1) | func_breakable#218 break | 34 / 1621 |  |
| c_buscity2 | inter_door#253'chdoor1' | lock-toggle(cof_interdooronoff newlockedby=blockach1) | func_breakable#219 break | 14 / 1638 |  |
| c_buscity2 | inter_door#255'chdoor2' | lock-toggle(cof_interdooronoff newlockedby=blockach2) | func_breakable#224 break | 14 / 2672 |  |
| c_buscity2 | inter_door#255'chdoor2' | lock-toggle(cof_interdooronoff newlockedby=blockach2) | func_breakable#225 break | 16 / 2665 |  |
| c_buscity2 | inter_door#255'chdoor2' | lock-toggle(cof_interdooronoff newlockedby=blockach2) | func_breakable#226 break | 34 / 2661 |  |
| c_city | inter_door#347'bclockeddoor' | lock-toggle(cof_interdooronoff newlockedby=bclocky) | trigger_once#361 player-touch (once) | 418 / 94 | FAR-SIDE |
| c_city2 | inter_door#93'lockeddoor1' | KILLED | trigger_once#45 player-touch (once) | 317 / None | door removed |
| c_city2 | inter_door#313'apdoor1' | KILLED | trigger_once#317 player-touch (once) | 142 / 176 | door removed |
| c_city2 | inter_door#314'apdoor1' | KILLED | trigger_once#317 player-touch (once) | 146 / 134 | FAR-SIDE door removed |
| c_college1dark | inter_door#290'neegerdo' | inter_door_disable(frags=1) | trigger_once#338 player-touch (once) | 27 / 73 |  |
| c_college2 | inter_door#2'doorch1' | exit-change | func_button#9'drastisk' player-use (button) | 312 / 305 | FAR-SIDE |
| c_college2 | inter_door#25'blockingdoor' | KILLED | func_button#9'drastisk' player-use (button) | 298 / 305 | door removed |
| c_down | inter_door#8'tjenana' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#15 player-touch (once) | 41 / 4543 |  |
| c_down | inter_door#190'sakerfan' | lock-toggle(cof_interdooronoff newlockedby=sakerfand) | trigger_once#116 player-touch (once) | 6953 / 7199 |  |
| c_forest3 | inter_door#166'inter_doors' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#173 player-touch (once) | 3882 / 3915 |  |
| c_forest3 | inter_door#166'inter_doors' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#174'stbutton' player-use (button) | 0 / 60 |  |
| c_forest3 | inter_door#169'interdorus' | inter_door_disable(frags=1) | trigger_once#193 player-touch (once) | 36 / 83 |  |
| c_forest4 | inter_door#17'doorlocko' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#164 player-touch (once) | 2217 / 3296 |  |
| c_forest4 | inter_door#17'doorlocko' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#167 player-use (button) | 184 / 1641 |  |
| c_forest7 | inter_door#191'exitfucka' | exit-change | trigger_once#265 player-touch (once) | 34 / 1445 |  |
| c_fsewer1 | inter_door#225'keymdl' | KILLED | func_button#221 player-use (button) | 205 / None | door removed |
| c_hamn | inter_door#7'sawrunnerha' | KILLED | trigger_once#61 player-touch (once) | 65 / 96 | door removed |
| c_hamn | inter_door#109'tjenafena' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#61 player-touch (once) | 3112 / 3073 | FAR-SIDE |
| c_hamn | inter_door#109'tjenafena' | lock-toggle(cof_interdooronoff newlockedby=) | func_breakable#116'matene' break | 28 / 5833 |  |
| c_hamn | inter_door#109'tjenafena' | lock-toggle(cof_interdooronoff newlockedby=) | func_breakable#117'matene' break | 19 / 5832 |  |
| c_hamn | inter_door#147'fudoor2' | lock-toggle(cof_interdooronoff newlockedby=blockadorr) | func_breakable#157'sakenebiff' break | 54 / 40 | FAR-SIDE |
| c_hamn | inter_door#147'fudoor2' | lock-toggle(cof_interdooronoff newlockedby=blockadorr) | func_breakable#158'sakenebiff' break | 52 / 57 |  |
| c_hamn | inter_door#147'fudoor2' | lock-toggle(cof_interdooronoff newlockedby=blockadorr) | func_breakable#159'sakenebiff' break | 61 / 83 |  |
| c_hamn | inter_door#149'fudoor1' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#66 player-touch (once) | 51 / 68 |  |
| c_hamn | inter_door#154'fudoor3' | lock-toggle(cof_interdooronoff newlockedby=blockadorr2) | func_breakable#157'sakenebiff' break | 15 / 84 |  |
| c_hamn | inter_door#154'fudoor3' | lock-toggle(cof_interdooronoff newlockedby=blockadorr2) | func_breakable#158'sakenebiff' break | 5 / 93 |  |
| c_hamn | inter_door#154'fudoor3' | lock-toggle(cof_interdooronoff newlockedby=blockadorr2) | func_breakable#159'sakenebiff' break | 32 / 111 |  |
| c_hamn3 | inter_door#16'doortc1' | exit-change | trigger_once#144 player-touch (once) | 542 / 2257 |  |
| c_hamn3 | inter_door#154'2doortc1' | lock-toggle(cof_interdooronoff newlockedby=tjenakungen) | trigger_once#144 player-touch (once) | 456 / 2010 |  |
| c_park | inter_door#125'fittsaftse' | lock-toggle(cof_interdooronoff newlockedby=) | statue_puzzle_complete#115 player-use (statue puzzle) | 1141 / 2329 |  |
| c_park | inter_door#125'fittsaftse' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#278 player-touch (once) | 2179 / 2570 |  |
| c_park2 | inter_door#88'4dernn' | KILLED | trigger_once#365 player-touch (once) | 3620 / 3580 | FAR-SIDE door removed |
| c_park2 | inter_door#116'3dernn' | KILLED | trigger_once#365 player-touch (once) | 5142 / 5169 | door removed |
| c_park2 | inter_door#119'2dernn' | KILLED | trigger_once#365 player-touch (once) | 5162 / 5131 | FAR-SIDE door removed |
| c_park2 | inter_door#190'dernn' | KILLED | trigger_once#365 player-touch (once) | 50 / 517 | door removed |
| c_park2 | inter_door#361'dernn2' | inter_door_disable(frags=1) | trigger_once#365 player-touch (once) | 52 / 517 |  |
| c_park2 | inter_door#367'2dernn2' | inter_door_disable(frags=1) | trigger_once#365 player-touch (once) | 5161 / 5131 | FAR-SIDE |
| c_park2 | inter_door#368'3dernn2' | inter_door_disable(frags=1) | trigger_once#365 player-touch (once) | 5142 / 5169 |  |
| c_park2 | inter_door#369'4dernn2' | inter_door_disable(frags=1) | trigger_once#365 player-touch (once) | 3618 / 3580 | FAR-SIDE |
| c_park3 | inter_door#372'chdoor12' | lock-toggle(cof_interdooronoff newlockedby=blockadock) | trigger_once#385 player-touch (once) | 600 / 1883 |  |
| c_park3 | inter_door#374'chdoor22' | lock-toggle(cof_interdooronoff newlockedby=blockadock2) | trigger_once#388 player-touch (once) | 595 / 1789 |  |
| c_roofs2 | inter_door#58'doorlockeo' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_multiple#294'tmultiei' player-touch (multiple) | 248 / 271 |  |
| c_roofs2 | inter_door#58'doorlockeo' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#293'toncen' player-touch (once) | 255 / 255 |  |
| c_roofs2 | inter_door#250'doorlockeo2' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_multiple#294'tmultiei' player-touch (multiple) | 246 / 275 |  |
| c_roofs2 | inter_door#250'doorlockeo2' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#293'toncen' player-touch (once) | 247 / 275 |  |
| c_sewer2 | inter_door#36'hemoglobin' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#212 player-use (button) | 194 / 180 | FAR-SIDE |
| c_sewer2 | inter_door#36'hemoglobin' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#151 player-touch (once) | 1244 / 1143 | FAR-SIDE |
| c_sewer2 | inter_door#43'fisteh' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#259 player-use (button) | 40 / 109 |  |
| c_sewer2 | inter_door#43'fisteh' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#151 player-touch (once) | 1356 / 1363 |  |
| c_sewer2 | inter_door#43'fisteh' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#151 player-touch (once) | 1356 / 1363 |  |
| c_sewer3 | inter_door#104'doorsewlcks2' | KILLED | func_button#401'button' player-use (button) | 1951 / 1399 | FAR-SIDE door removed |
| c_sewer3 | inter_door#107'satmara' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#221 player-touch (once) | 572 / 1298 |  |
| c_sewer3 | inter_door#107'satmara' | lock-toggle(cof_interdooronoff newlockedby=) | monster_sewerboss#146'dsasdaeas' death/monster | 1424 / 374 | FAR-SIDE |
| c_sewer3 | inter_door#140'lockadehehea' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#110'kukstave' player-use (button) | 1034 / 56 | FAR-SIDE |
| c_sewer3 | inter_door#140'lockadehehea' | lock-toggle(cof_interdooronoff newlockedby=) | monster_sewerboss#146'dsasdaeas' death/monster | 396 / 1445 |  |
| c_sewer3 | inter_door#208'lockadeheheab' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#110'kukstave' player-use (button) | 1448 / 1762 |  |
| c_sewer3 | inter_door#208'lockadeheheab' | lock-toggle(cof_interdooronoff newlockedby=) | monster_sewerboss#146'dsasdaeas' death/monster | 59 / 493 |  |
| c_sewer3 | inter_door#445'door45' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#448 player-touch (once) | 139 / 2548 |  |
| c_sewer3 | inter_door#471'doorsewlcks1' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#401'button' player-use (button) | 1952 / 3713 |  |
| c_sewer3 | inter_door#471'doorsewlcks1' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#538 player-touch (once) | 395 / 2466 |  |
| c_stad | inter_door#1'doornen2' | KILLED | func_button#312 player-use (button) | 1326 / 1237 | FAR-SIDE door removed |
| c_stad | inter_door#138'shitdoor' | lock-toggle(cof_interdooronoff newlockedby=gabriella) | func_button#299'elbutton1' player-use (button) | 55 / 477 |  |
| c_stad | inter_door#138'shitdoor' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#312 player-use (button) | 287 / 638 |  |
| c_stad | inter_door#495'doornen' | inter_door_disable(frags=1) | func_button#312 player-use (button) | 1327 / 1237 | FAR-SIDE |
| c_start | inter_door#204'doorlockedss' | lock-toggle(cof_interdooronoff newlockedby=nasiss) | trigger_once#147 player-touch (once) | 186 / 1269 |  |
| c_start | inter_door#713'sd_door1' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#120 player-touch (once) | 2159 / 2076 | FAR-SIDE |
| c_start | inter_door#713'sd_door1' | lock-toggle(cof_interdooronoff newlockedby=) | item_padlock#719'padlock' player-use (padlock) | 685 / 611 | FAR-SIDE |
| c_start | inter_door#713'sd_door1' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#721'padbutton' player-use (button) | 0 / 125 |  |
| c_start2 | inter_door#204'cafedoors' | KILLED | func_button#245 player-use (button) | 404 / 369 | FAR-SIDE door removed |
| c_start2 | inter_door#206'cafedoors2' | KILLED | func_button#245 player-use (button) | 387 / 435 | door removed |
| c_start2 | inter_door#206'cafedoors2' | inter_door_disable(frags=1) | trigger_once#413 player-touch (once) | 33 / 107 |  |
| c_subway1st1 | inter_door#135'eldoor1' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#208 player-use (button) | 1390 / 1317 | FAR-SIDE |
| c_subway1st1 | inter_door#135'eldoor1' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#138 player-touch (once) | 2323 / 2269 | FAR-SIDE |
| c_subway1st1 | inter_door#157'eldoor2' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#206 player-use (button) | 2535 / 2595 |  |
| c_subway1st1 | inter_door#157'eldoor2' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#273 player-use (button) | 1005 / 927 | FAR-SIDE |
| c_subway1st1 | inter_door#157'eldoor2' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#138 player-touch (once) | 2196 / 2281 |  |
| c_subway1st1 | inter_door#159'2eldoor2' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#206 player-use (button) | 2581 / 2523 | FAR-SIDE |
| c_subway1st1 | inter_door#159'2eldoor2' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#273 player-use (button) | 942 / 1025 |  |
| c_subway1st1 | inter_door#159'2eldoor2' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#138 player-touch (once) | 2262 / 2179 | FAR-SIDE |
| c_subway1st1 | inter_door#173'lockedd1' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#275 player-use (button) | 1455 / 1485 |  |
| c_subway1st1 | inter_door#173'lockedd1' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#138 player-touch (once) | 1443 / 1431 | FAR-SIDE |
| c_subway1st1 | inter_door#240'2eldoor1' | lock-toggle(cof_interdooronoff newlockedby=) | func_button#208 player-use (button) | 1331 / 1408 |  |
| c_subway1st1 | inter_door#240'2eldoor1' | lock-toggle(cof_interdooronoff newlockedby=) | trigger_once#138 player-touch (once) | 2280 / 2337 |  |
| c_subway2st1 | inter_door#27'lockshit2' | lock-toggle(cof_interdooronoff newlockedby=greger) | trigger_once#631 player-touch (once) | 2853 / 187 | FAR-SIDE |
| c_subway2st1 | inter_door#721'shitdoor' | lock-toggle(cof_interdooronoff newlockedby=gabriella2) | func_button#595'buttkuk1' player-use (button) | 124 / 166 |  |
| c_subway2st1 | inter_door#721'shitdoor' | lock-toggle(cof_interdooronoff newlockedby=gabriella2) | func_button#608'buttkuk2' player-use (button) | 56 / 96 |  |
| c_subway2st1 | inter_door#721'shitdoor' | KILLED | func_button#637 player-use (button) | 218 / 227 | door removed |
| c_subway2st1 | inter_door#721'shitdoor' | KILLED | func_button#633 player-use (button) | 227 / 244 | door removed |
| c_subway2st1 | inter_door#721'shitdoor' | KILLED | trigger_once#631 player-touch (once) | 1063 / 1070 | door removed |
| c_subway2st1 | inter_door#727'fuseddoor' | inter_door_disable(frags=1) | trigger_once#631 player-touch (once) | 1066 / 1070 |  |
| c_subway2st2 | inter_door#592'removed' | KILLED | trigger_once#602 player-touch (once) | 35 / 594 | door removed |
| c_subway2st2 | inter_door#598'doorl' | inter_door_disable(frags=1) | trigger_once#602 player-touch (once) | 37 / 586 |  |

### 4.8 One-shot (`trigger_once`) roots that teleport their toucher

Every `trigger_once` whose chain reaches a player `cof_teleport` (no `message`), with the teleports in time order (cumulative multi_manager delays). `round-trip` = the last destination is within 400 units of the trigger (scene rooms that put the toucher back); `ONE-WAY` = the toucher ends up elsewhere and the consumed trigger cannot take the partner along. `player act` = the activator reaching the teleport is still the toucher (false after a train/camera hop).

| map | root | root notes | class | final dist | teleports (name@t) |
|---|---|---|---|---|---|
| c_apartment5 | trigger_once#270 |  | round-trip | 199 | bajsiballe@10.5s |
| c_apartmentsick | trigger_once#302 |  | **ONE-WAY** | 759 | telemiehaeh@25s |
| c_apartmentsick | trigger_once#452'lazzerro' | starts disabled (iuser1), enabled by cof_triggeron | **ONE-WAY** | 2061 | teleportusus@0s |
| c_asylum1 | trigger_once#310 |  | round-trip | 301 | teledestts1t@0.5s, 4teledestts1t@58.6s, 5teledestts1t@58.6s, 4teledestts2t@59.6s, 5teledestts2t@59.6s, teledestts2t@60.1s |
| c_attic | trigger_once#58 |  | round-trip | 129 | dateleport1@5.5s |
| c_basement | trigger_once#240 |  | **ONE-WAY** | 421 | fittjaevel@64s |
| c_bridge | trigger_once#165 |  | **ONE-WAY** | 3794 | telemia@95s |
| c_broscene | trigger_once#150 | covers info_player_start: fires at map start/spawn | **ONE-WAY** | 1048 | kanalkuk2@21.5s |
| c_city | trigger_once#171 |  | **ONE-WAY** | 7171 | telelole22@0s |
| c_city | trigger_once#180 |  | **ONE-WAY** | 7281 | telelole22@0s |
| c_city | trigger_once#707 |  | round-trip | 268 | teletvo1c@0.5s, teletvo1c2@15.5s |
| c_city2 | trigger_once#130 |  | round-trip | 243 | teleziee@1s |
| c_ending1 | trigger_once#259 |  | **ONE-WAY** | 2791 | teledes_tele@27s |
| c_ending1b | trigger_once#4 | covers info_player_start: fires at map start/spawn | **ONE-WAY** | 1800 | teledes_tele@0s |
| c_ending1c | trigger_once#744 | master=multiscr1nej | round-trip | 107 | tele_niceye@0s |
| c_ending2 | trigger_once#254 |  | **ONE-WAY** | 2791 | teledes_tele@27s |
| c_ending2b | trigger_once#4 | covers info_player_start: fires at map start/spawn | **ONE-WAY** | 1800 | teledes_tele@0s |
| c_ending2c | trigger_once#739 | master=multiscr1nej | round-trip | 107 | tele_niceye@0s |
| c_ending3 | trigger_once#258 |  | **ONE-WAY** | 2791 | teledes_tele@27s |
| c_ending3b | trigger_once#4 | covers info_player_start: fires at map start/spawn | **ONE-WAY** | 1800 | teledes_tele@0s |
| c_ending3c | trigger_once#735 | master=multiscr1nej | round-trip | 107 | tele_niceye@0s |
| c_ending4 | trigger_once#254 |  | **ONE-WAY** | 2791 | teledes_tele@34.7s |
| c_ending5 | trigger_once#65 | covers info_player_start: fires at map start/spawn | **ONE-WAY** | 4947 | tellemenow@21s |
| c_ending5b | trigger_once#3 |  | **ONE-WAY** | 5649 | lollipopp_t@0s |
| c_intro | trigger_once#157 | covers info_player_start: fires at map start/spawn | **ONE-WAY** | 3587 | kunitei2@185s |
| c_intro | trigger_once#381 |  | **ONE-WAY** | 4917 | kunitei2@133s |
| c_nightmare | trigger_once#125 | covers info_player_start: fires at map start/spawn | **ONE-WAY** | 2575 | pulla@83s |
| c_nightmare | trigger_once#159 | covers info_player_start: fires at map start/spawn | **ONE-WAY** | 2308 | pulla@40s |
| c_nightmare | trigger_once#293 | covers info_player_start: fires at map start/spawn | **ONE-WAY** | 3538 | pulla@83s |
| c_nightmare | trigger_once#432 |  | **ONE-WAY** | 2151 | beforetele@0s, teleportea45@1.5s, teleportea@6.5s |
| c_park3 | trigger_once#499'tro_inte' | starts disabled (iuser1), enabled by cof_triggeron | round-trip | 60 | isteleport1@1s, isteleport2@11s |
| c_roofs | trigger_once#186 |  | **ONE-WAY** | 891 | teleportfall@0.6s |
| c_roofs | trigger_once#265 |  | **ONE-WAY** | 656 | teleport1@0s, teleport2@357.9s |
| c_roofs | trigger_once#681 |  | **ONE-WAY** | 1570 | teleport1@0.5s, teleport2@358.4s |
| c_roofs2 | trigger_once#177 |  | **ONE-WAY** | 891 | teleportfall@0.6s |
| c_roofs2 | trigger_once#244 | covers info_player_start: fires at map start/spawn | **ONE-WAY** | 1622 | teleportia2@0s |
| c_roofs2 | trigger_once#293'toncen' |  | round-trip | 276 | teleportia3@5s |
| c_start | trigger_once#120 | covers info_player_start: fires at map start/spawn | round-trip | 57 | mainteleport@0s, 1teleport@44s |
| c_start | trigger_once#147 |  | round-trip | 85 | mainteleport@0.5s, 2teleport@15.4s |
| c_start | trigger_once#148 |  | **ONE-WAY** | 404 | kahshe@41s |
| c_subway2st1 | trigger_once#556 |  | **ONE-WAY** | 423 | tbteleport1h@0.5s, tbteleport2h@24.5s |
| c_subway2st1 | trigger_once#631 |  | **ONE-WAY** | 2864 | 2teleportion@0s |
| c_subway2st1 | trigger_once#725 |  | round-trip | 134 | tb2teleport1@0.5s, tb2teleport2@11.5s |
| c_subway2st3 | trigger_once#252'triggercutscene' | starts disabled (iuser1), enabled by cof_triggeron | **ONE-WAY** | 1237 | telechange2@0.5s, teleblack@2.5s, telechange@23.5s |
| c_subway2st3 | trigger_once#356'traintrigger' | starts disabled (iuser1), enabled by cof_triggeron | **ONE-WAY** | 1459 | traintele@0s |
| c_subwaysick2 | trigger_once#251 |  | **ONE-WAY** | 913 | isteleport1@1s, isteleportx@72.1s |
| c_trainscene | trigger_once#106 | covers info_player_start: fires at map start/spawn | **ONE-WAY** | 1879 | teleportoar_df@101.5s |


## 5-6. Camera table and per-map listing

Omitted from the repository copy (about 400 KB). The full report, its 311-row trigger_camera table, the per-map listing and the machine-readable graph are kept locally in `evidence/finish-20261012/research/D-map-chains.md` and `D-map-chains.json` (git-ignored).

## 7. Summary table

"Roots" counts distinct instances per dominant root kind in the 81 story maps (an instance can have several roots; see JSON). "Affected" uses CERTAIN where section 1 read it, otherwise the scan/spec (LIKELY).

| class (story count) | roots | affected | hazard (2 players) | treatment |
|---|---|---|---|---|
| trigger_camera (311 / 33 maps) | 252 reachable from a trigger_once (14 of them also from a button, 2 also from a trigger_multiple), 29 button only, 9 valve, 17 monster death, 1 statue puzzle, 3 never fired | activator, else host (CERTAIN); OFF releases all | WRONG-PLAYER-VISUAL: partner free and unfrozen during 46 s+ scenes; death-rooted scenes always on host | mirror SetView + FL_FROZEN + HUD bits to all spawned players while on; undo on the OFF loop |
| cof_teleport (200 / 74) | 118 only behind a reusable trigger_multiple; 58 player teleports reachable from a trigger_once (+4 with `message`, which move monsters); 6 button only; 3 valve; 6 monster death; 5 never fired | activator only if player (CERTAIN) | STUCK-PLAYER for one-shot roots (34 ONE-WAY roots, section 4.8); death-rooted ones inert even in SP | mirror the activator's teleport to partners for one-shot roots; keep trigger_multiple ones per-player |
| cof_killplayer (6 / 6) | all trigger_once | ALL players (CERTAIN) | TEAM-WIPE from one player's touch | vtable-slot Use replacement (kill activator only) or engine revive of non-activators |
| cof_phonecall (2) | c_park3 spawn trigger_once; c_basement dial | activator (spec) | c_park3 SOFT-LOCK if dropped (gate only unlocks after the call); DESYNC | ring all players or pick a phone holder; fire target once on hang-up |
| cof_phonedisable (1) | trigger_once (starts disabled) | FIRST (scan) | DESYNC (host's phone off, not the toucher's) | FIRST := activator, or apply to all |
| trigger_cofmobile (7) | trigger_once | loop (scan) | NONE | nothing |
| cof_mdlcutscene (22 / 10) | 15 trigger_once, 6 valve, 1 death | FIRST + loop (scan) | WRONG-PLAYER-VISUAL | FIRST := activator; mirror view |
| cof_strangle (3: c_ending1c/2c/3c) | valve -> game_counter | FIRST (scan; SetOrigin + EnableControl) | WRONG-PLAYER (host yanked) | FIRST := activator (valve user) |
| cof_clothesmenu (2) | func_button | FIRST (scan) | WRONG-PLAYER (menu on host) | FIRST := activator |
| cof_lookat, cof_greyfade (1 each) | trigger_once | FIRST (scan) | WRONG-PLAYER-VISUAL | FIRST := activator or all |
| func_asylumlookat (1) | telephone button (+12 s) | unknown (scan) | unknown | check live |
| cof_camerazoom (13), cof_hint (11), cof_simonspeak (8) | trigger_once (+2 buttons) | activator (spec) | cosmetic | optional: mirror to all |
| cof_yesno (2) | c_asylum1 P345 (trigger_once / button), c_roofs2 escape (trigger_multiple) | activator; needs player (CERTAIN) | DESYNC: prompt and effects for one player; c_roofs2 answerer teleported alone | mirror teleport; keep prompt per answerer |
| cof_weapontrigger (1, c_asylum1) | trigger_once#310 (+46.6 s) | activator's inventory (CERTAIN) | wrong branch if the partner carries the P345 (recoverable through the button) | check every player's inventory, or FIRST := carrier |
| cof_doctorweaponset (4) / cof_doctorweapontrigger (3) | c_asylum1 yes/no; c_attic trigger_once, boss death | FIRST (scan) | DESYNC only if the host changes between maps | copy `m_iDoctorWeapon` to all players |
| cof_ending (3) | c_roofs2 boss death; c_asylum1 yes; c_hamn3 package button | ALL connected (CERTAIN) | late joiner misses the flag | OR the flags at `endingcheck` |
| endingcheck inter_door (c_village3) | player use | user only (inter_door) | ending chosen by the opener's flags; partner crosses the changelevel from ~2 k u away | regroup the partner at the leader on arrival; OR flags |
| cof_credits (13 / 5) | valve chain (1c-3c), boss death (4b), trigger_once (4b, 5c) | loop (scan) | NONE | nothing |
| cof_wheelchairmode (1) / cof_stopwheelchair (1) | c_ending4b spawn trigger_once / boss death | activator / FIRST (scan) | STUCK-PLAYER (guest stays in the wheelchair) | stop: FIRST := wheelchair rider, or mirror the mode |
| cof_maxhealthchange (2) | c_asylum1 yes/no | activator (scan) | DESYNC (max HP only on the answerer) | apply to all |
| cof_giveitems (16 / 14) | 12 button, 5 trigger_once | activator (scan) | DESYNC (story items go through the ledger; weapons do not) | share story items (exists); weapons per player |
| cof_clearitems (3) | trigger_once (c_bridge bag, c_start camera), c_asylum1 yes | activator (LIKELY) | DESYNC (e.g. c_bridge bag removed only from the toucher) | apply to all for story removals |
| cof_objective (24 / 19) | 18 trigger_once, 3 button, pickup, puzzle, death | FIRST (scan) | DESYNC (host's objective) | broadcast |
| cof_addcodenote (2), cof_updatekeypad (2), keypads (9) | button / dial / spawn trigger | FIRST or activator | DESYNC (note/keypad on one player) | broadcast note; UI per user |
| cof_puzzlebar1-5 (5, c_asylumb) | player use | FIRST + SetView (scan) | WRONG-PLAYER-VISUAL | FIRST := user |
| trigger_statueuse (5, c_park) | player use | activator (scan) | NONE | nothing |
| cof_speedchange (1) | trigger_once | world (door speed) | NONE | nothing |
| cof_chapter (10), cof_begingame (1), cof_introduction (1) | start-of-map trigger_once / death | loop (scan) / unknown | NONE (cof_introduction unknown) | nothing |
| trigger_changelevel with targetname (60) | 51 fired by a trigger_multiple in the same spot, 2 trigger_once, the rest touch | everyone (level change) | NONE beyond the known exit issues; no `trigger_transition` exists in story maps | nothing |
| trigger_teleport (7), trigger_cofteleport (2) | touch volumes (reusable) | toucher | NONE (per player) | nothing |
| func_tracktrain (7, c_intro) | spawn trigger_once | world | NONE | nothing |

## 8. Open questions only a live test can answer

1. Arrival-room teleports (`c_ending1b/2b/3b#4`, `c_roofs2#244`, `c_ending5#65`, `c_nightmare`, `c_broscene#150`, `c_trainscene#106`, `c_intro#157`): is the area the second player lands in really closed, and where does the engine place the second player on arrival (inside the consumed trigger volume or at the leader)?
2. `c_park3`: does `cof_phonecall` really drop the call (and never fire `gateinter`) when the activator has no `weapon_mobile`? Does the gate button then stay locked? Who touches `trigger_once#508` first when both players arrive together?
3. `c_attic` 'toncedeath' / `c_ending*b` 'fuckingdie' / `c_subwaysick1` / `c_ending5` 'killmefuck': confirm the team wipe and what the volumes are (doctor's line of fire, falls).
4. `c_asylum1#310`: can the partner enter the doctor's room after the toucher is teleported in (doors `othersidem`/`othersidem2`, `staffdoor2`)? With the P345 on the partner, does the `givethep228b` button path complete cleanly?
5. `c_ending4b`: who ends up in wheelchair mode, and does `cof_stopwheelchair` free them?
6. `c_village3` endingcheck: which player's flags decide, and where does the partner appear in `c_endingN`?
7. `c_roofs2`: after "escape", is the partner left on the roof with no way down? `teleportia3` moves only the answerer.
8. Cameras: with the partner unfrozen during long scenes (c_asylum1 46 s, c_nightmare 40 s, c_roofs `multiroofscene` 358 s), do spawned monsters attack the partner? Does the OFF loop clear the partner's HUD bits harmlessly?
9. The 6 death-rooted `cof_teleport` (inert by code): is anything visibly wrong in SP (player not moved after the `c_roofs2` boss, the `c_sewer3` boss, the `c_attic` doctor)? If SP is fine, co-op needs nothing there.
10. Doors in section 4.7 marked `FAR-SIDE` or `door removed` (e.g. the `c_park2` 'dernn' set killed by `trigger_once#365`, `c_city2` 'apdoor1', `c_start2` 'cafedoors', `c_subway2st1` 'shitdoor', `c_stad` 'doornen2'): is there another route for a partner who has not gone through yet?

