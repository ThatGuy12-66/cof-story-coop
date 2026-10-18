# B: endings, P345 hand-over, phone calls, doctor weapon and branch logic (static RE)

> **Corrections (2026-10-18, decompilation survey, `docs/research/2026-10-18-cof-decompilation-survey.md` section 6):**
> - The call is answered after 0.1 s.
> - The boss bar is lost only by a respawned player; joiners and restores get it.

Binary: `runtimes/enhanced/cryoffear/cl_dlls/hl.dll` (stock, base 0x10000000). Client checks in
`client.dll` from the same folder. Static only (capstone/pefile via `re/hlre.py`, entity lumps of the
81 story maps via `cofcoop/bsp.py`). Nothing was launched, built or modified.

Tags: **CERTAIN** = read from machine code (control flow and offsets). **LIKELY** = code read but a
meaning/name is inferred, or a runtime interaction is implied. **UNKNOWN** = not settled statically.

Conventions as in the brief: `P+x` = CBasePlayer offset, `pev+x` = CoF-shifted entvars. FIRST player
= `UTIL_FindEntityByClassname(NULL,"player")`. **Important engine interaction (CERTAIN, read from
`engine/server/sv_client.c:3183` and `sv_game.c:1631 CoF_SenderFind`): during EVERY game
`ClientCommand` the engine makes the command's sender the FIRST player.** Combined with the
fact below this changes who receives several FIRST-player writes.

**Spirit-style `multi_manager` fires delay-0 targets synchronously**: `ManagerUse` 0x1010E190
stores the activator in `m_hActivator` (+0x184, via EHANDLE= at 0x1010E3FE); when the start delay
(+0x16C/+0x170) is <= 0 it sets the think and calls `ManagerThink` (0x1010DA80) directly at
0x1010E555 instead of scheduling it (CERTAIN). ManagerThink fires with
`FireTargets(name, m_hActivator, this, m_useType(+0x18C), 0)` (0x1010DC7C-0x1010DC90,
0x1010DD3C-0x1010DD6A) and, in the timed mode, fires every target whose delay is
<= `time - m_startTime(+0xE8)` (0x1010DEE1-0x1010DF21), so that synchronous first call fires all
delay-0 targets (LIKELY; matches Spirit of Half-Life). So a chain started from a client command (yes/no
answer, phone number, keypad) runs its delay-0 targets **inside that command's sender-first scope**.

---

## 0. Top hazards (detail in the sections)

| # | Hazard | Class | Severity |
|---|---|---|---|
| 1 | Guest answers the P345 yes/no -> `cof_doctorweaponset` runs inside the guest's `yesnoactivate` command, so the **guest** gets `m_iDoctorWeapon`; host keeps 0. In `c_attic` the doctor boss reads the host (outside any command), sees 0 and **never starts its AI** (re-polls every 0.05 s forever); `cof_doctorweapontrigger` fires nothing. | doctor weapon chain | SOFT-LOCK (LIKELY; boss killability while idle UNKNOWN) |
| 2 | `cof_phonecall` silently drops the call (think not re-armed, target never fires) if the recipient lacks `weapon_mobile`, dies, or loses the phone. `c_park3` "sophiecall" gates the exit button ("The phone is ringing."). `weapon_mobile` is given only to the `c_start` activator (`cof_giveitems` is activator-only). | cof_phonecall | SOFT-LOCK (CERTAIN code path) |
| 3 | A recipient who dies mid-call keeps `m_bInPhoneCall` (+0x1EE8) = 1 forever (only EndCall clears it; Spawn does not). While it is set the game ignores **every** client command from that player (gate at 0x100190BE): inventory, notes, keypads, yes/no, phone. It is in the save table, so it survives transitions. | cof_phonecall | STUCK-PLAYER (CERTAIN) |
| 4 | The ending door (`c_village3`, target `endingcheck`) moves only its opener to `ending_exitN` chosen from the **opener's** flags; partners get no fade/move. Engine door-follow ignores it (target is not an `inter_door_exit`). Different flags -> different `c_endingN`. | inter_door | DESYNC / split ending |
| 5 | `cof_killplayer` kills **all** players (loop) - 5 of 6 story instances are fall-death floors. One player falling wipes the team. | cof_killplayer | team wipe (recoverable) |
| 6 | `cof_yesno` answers are not tied to a pending prompt: any client may answer any entity index, repeatedly; `c_roofs2` `ryesno` is driven by a `trigger_multiple` (wait 5) so two players can both get and answer it. | cof_yesno | DESYNC (double fire) |
| 7 | Late joiners lack `cof_begingame` stats-valid bytes (+0x678/+0x679) -> their credits show no ending number/rank; `cof_bosshealthbar` is lost on respawn (Spawn clears +0x5F4). | cof_begingame / bossbar | WRONG-PLAYER-VISUAL |

---

## 1. Doors

### inter_door (CInterDoor)
- Factory / size / vtable: factory 0x10047F10, size 0x1B0, vtable 0x101B0AB8. Spawn 0x10048BD0,
  Precache 0x100488C0, KeyValue 0x10048550, ObjectCaps 0x100488A0 (returns 8 = FCAP_IMPULSE_USE),
  Use (slot +0xE8) 0x10048F70. Helpers: IsLocked 0x10048540 (`lockedby != 0`), Unlock 0x10048E40,
  BeginSequence 0x10048CB0, NearbyMonsterReset 0x100489F0, EndSequence (think) 0x10048070,
  CloseGameThink 0x10048040, SetDisabled(byte) 0x10048030. CERTAIN.
- Keys (KeyValue 0x10048550, CERTAIN): `opensound`->pev noise (0x1EC); `closesound`->pev+0x1F0;
  `lockedsnd`->pev+0x1F4; `unlockedsnd`->pev+0x1F8; `music`->+0x18C; `lockedby`->+0x190;
  `lockedmsg`->+0x194; `unlockedmsg`->+0x198; `propertarget`->+0x19C; `useunlock`->+0x1A0;
  `musicfadeout`->+0x1A8 (float); `musicfadein`->+0x1AC (float); `silentroom`->+0x188 (byte, atoi);
  `newlevelsnd`->+0x184; others -> base 0x10102480. `+0x17C` is the master (passed to
  UTIL_IsMasterTriggered). `+0x1A4` = next-use time.
- Spawn (CERTAIN): spawnflag 4 -> disabled byte +0x180 = 1; SOLID_BSP, MOVETYPE_PUSH, SetModel;
  `+0x1A4 = time`; **overwrites +0x1A8/+0x1AC with -1.0** (so the fade keys are effectively ignored).
- Use 0x10048F70 (CERTAIN), in order:
  1. Requires activator `IsPlayer`, master triggered, `+0x180 == 0` (not disabled), `time >= +0x1A4`.
  2. targetname `unlockablesdoor` (c_start/c_bridge only): needs one of the five scriptsettings
     unlocks (`0x100DD000(1|2|4|8|0x10)`), else ProFont "I can't open it. Strange, there's no
     lock..." (colour 5).
  3. `useunlock` set -> find that `inter_door`, call its Unlock(activator) if locked, clear
     `useunlock`, done.
  4. **`pev->owner` already set -> ProFont "I can't use this while %s is using it" (owner's netname,
     colour 5) to the activator; nothing else.** Only one user per door sequence.
  5. `newlevelsnd` -> activator `P+0x5EC` (g_iDoorSound).
  6. Locked (`lockedby`) -> `lockedmsg` ProFont (5) + `lockedsnd`. Else `pev->owner = activator
     edict` (0x1004923F) and BeginSequence.
  7. Always `+0x1A4 = time + 1.0`.
- BeginSequence 0x10048CB0 (owner only, CERTAIN): `b_GoingThroughADoor` (P+0x1EA4)=1 (invulnerable,
  MP PostThink adds EF_NODRAW); `opensound` from the player; `UTIL_ScreenFade(owner, black, 0.5,
  2.0, 255, OUT|STAYOUT)`; `P+0x1EC8 (m_OnStairs)=0`; `EnableControl(FALSE)` (FL_FROZEN);
  `m_iHideHUD |= 9`; FL_NOTARGET; music fade-out (0x100EA600) depending on `silentroom`/`music`;
  0x100489F0 snaps every living `monster_*` (not generic/spitter) within 1024 u of the exit to its
  stored position (+0x1E4); `SetNextThink(2.0)`, think = EndSequence.
- EndSequence 0x10048070 (owner only, CERTAIN):
  - owner missing -> "No USER!" and owner cleared; owner not a player -> "No PLAYER!" (owner NOT
    cleared).
  - Target `closegame1..4` (0x100480E6-0x100481A1): ProFont "End of <x> demo chapter!" (colour 2),
    music fade, think 0x10048040 in 2 s which runs `CLIENT_COMMAND(FIRST, "map c_game_menu1\n")`.
    **No story door uses these** (map scan: 0 instances).
  - Target `endingcheck` (0x100481AE-0x100482B1): copy "endingcheck" into a local, then from the
    **owner's** bytes: R=`P+0x4CD` RoofBossKilled, G=`P+0x4CE` GivenP345, D=`P+0x4CF`
    DeliveredPackage:

    | R | G | name |
    |---|---|---|
    | 0 | 0 | ending_exit1 |
    | 0 | 1 | ending_exit2 |
    | 1 | 0 | ending_exit3 |
    | 1 | 1 | ending_exit4 |
    | any | any, D==1 | ending_exit5 (override) |

    A byte that is neither 0 nor 1 leaves the name "endingcheck" -> no entity -> "No TARGET!" and
    the opener stays frozen, black, door-flagged, and the door stays "in use" forever. Engine writes
    to these bytes must be exactly 0/1.
  - Otherwise target = `pev->target`. `FindEntityByTargetname(name)`; none -> "No TARGET!" (same
    stuck state as above).
  - Move: `org = target.origin; org.z = org.z - owner.mins.z + 1` (players), `UTIL_SetOrigin`,
    `P+0x618 = org`; `angles = v_angle = target.angles`, `fixangle = 1`; velocity and basevelocity
    zeroed; FL_ONGROUND cleared; `b_GoingThroughADoor = 0`; `closesound`; `ScreenFade(owner, black,
    0.5, 0, 255, FFADE_IN)`; `EnableControl(TRUE)`; `m_iHideHUD &= ~9`; FL_NOTARGET cleared;
    `propertarget` fired with **activator = the door** (`FireTargets(propertarget, door, door,
    USE_TOGGLE, 0)`, 0x100484AB); `PlayMP3(music, spawnflags&2, +0x1AC)` (sets only P+0x2310/0x231C/
    0x2320/0x2314, i.e. owner's music); `P+0x2294 = 1000`; door `pev->owner = 0`.
- Who is affected: activator (owner) only. Other players: nothing (no fade, freeze, move or music).
  CERTAIN.
- Player state changed: see above; all undone in EndSequence for the owner except the stuck paths.
- Story-map usage: 758 in 76 maps. Targets: 437 `inter_door_exit`, 314 none (all locked/fake), 6
  missing names, 1 `info_teleport_destination` (`c_subwaysick2` #34 `exitforever`). The only
  **unlocked** door whose target does not resolve is `c_village3` #287 (target `endingcheck`,
  model `*22`, `music basement1end.mp3`, no lock). `propertarget` 1, `useunlock` 1,
  `unlockablesdoor` 2 (c_start #828, c_bridge #377).
- `c_village3` ending geometry: `ending_exit1..5` = `inter_door_exit` at x -1808, y -2264/-2389/
  -2516/-2636/-2759, z -1246 (125 u apart). Each sits inside its own walk-in `trigger_changelevel`
  (`*23/*25/*27/*29/*31`, 80 x 92 x 131 u) to `c_ending1..5` with landmarks `e1..e5`; no
  `trigger_transition`. Arrival in the room immediately touches the level change. CERTAIN (map data).
- Co-op hazard:
  - Normal doors: NONE beyond what the engine's door follow already handles. During the 2 s
    sequence the partner gets "I can't use this while X is using it".
  - Ending door: **DESYNC.** Only the opener goes to `ending_exitN`. The engine's
    `CoF_PartyWatchDoors` skips it (`CoF_PartyDoorExit` needs an `inter_door_exit` target), so no
    follow and no cut-off prompt. The exit hold then holds the opener in the 80 x 92 room and shows
    the J prompt. If flags differ between players, a partner who uses the door themselves lands in a
    different room/trigger -> two different held exits (only the escape hatch resolves it).
- Recommended engine treatment (to make all players end at the same `ending_exitN` and see the same
  ending):
  1. **Flags first.** At door-begin (owner of `c_village3`'s `endingcheck` door gets
     `b_GoingThroughADoor`, 2 s before EndSequence) force the party OR of `P+0x4CD..0x4CF` (values
     exactly 0/1) onto every connected player, including dead ones. This is the stage-4 "ending OR"
     rule, applied explicitly here.
  2. Compute N with the table above (same flags for everyone, so it equals what the DLL picks) and
     resolve the `inter_door_exit` `ending_exitN`.
  3. Special-case this door in `CoF_PartyWatchDoors`: create a door event with destination
     `ending_exitN` and **every alive spawned partner as companion, ignoring the 128 u radius**.
     At begin, mirror the retail presentation to them: ScreenFade (0.5 s, hold 2.0, OUT|STAYOUT,
     black) and FL_FROZEN; at completion place them with `CoF_PartyLanding` limited to offsets
     <= 40 u (stays in the same 80 x 92 trigger; the neighbour rooms are 125 u away), fade in 0.5 s,
     unfreeze, and copy the door music fields (`P+0x2310/0x231C/0x2320`, set `P+0x2314=1`).
  4. Both players then touch the same `trigger_changelevel` -> the exit hold sees everyone at the
     exit -> one changelevel to `c_endingN` for all. Dead partners: keep the existing hold/escape
     hatch.
  - Minimal fallback (no special case): with step 1 alone, a partner who uses the same door, or who
    presses J, lands in the same room and releases the hold.
- Confidence: Use/Begin/EndSequence behaviour, ending table, owner-only scope CERTAIN; room layout
  CERTAIN (map data); whether walls separate the five rooms UNKNOWN (bounds only).

### inter_door_exit
- Factory 0x10047FD0, size 0xDC, vtable 0x101A249C. Spawn 0x10102C40 (point entity), Use = generic
  `m_pfnUse` dispatcher 0x1000E2E0 (none set). Pure destination marker. CERTAIN.
- Story usage: 403 in 76 maps. Hazard NONE. Treatment: nothing.

### inter_door_disable
- Factory 0x10047F70, size 0xDC, vtable 0x101B0E04; Use 0x10049280.
- Use (CERTAIN): **requires activator `IsPlayer`**; finds `target`; if it is an `inter_door`:
  `pev->frags == 0` -> `SetDisabled(1)` (+0x180=1), else `SetDisabled(0)` (enable).
- Who: world only (activator only gates). 13 instances in 8 maps, all `frags 1` (enable), e.g.
  `c_asylum1` `enabledoor1` (gate `othersidem2`, fired by the P345 branches).
- Hazard NONE (non-player activators fail exactly as in SP). Treatment: nothing.

### cof_inter_door_exitchange
- Factory 0x10047EB0, size 0xDC, vtable 0x101B0F24; Use 0x10048EF0.
- Use (CERTAIN): requires activator `IsPlayer`; door named `message` gets `pev->target = this
  target`. World only.
- Story: 5 (c_asylumb, c_attic, c_college2, c_forest7, c_hamn3). Hazard NONE.

### cof_interdooronoff
- Factory 0x10090A60, size 0x36C, vtable 0x101C4AE4; KeyValue 0x100964F0 (`newlockedby`->+0x368);
  Use 0x100A6EA0.
- Use (CERTAIN): entity named `message` via 0x10118500 (supports `*locus`); `inter_door` -> toggle
  `lockedby` between NULL and "somethinglocked"; `func_button` -> toggle its lock (+0x180/+0x18C).
  World only.
- Story: 52 in 26 maps. Notable: `c_park3` #401 `gateinter` unlocks `gatebutton` ("The phone is
  ringing.") at the end of the `sophiecall` phone call (see phone section). Hazard NONE by itself.

---

## 2. Ending state and credits

### cof_ending (CCoFEnding)
- Factory 0x1008FBC0, size 0xDC, vtable 0x101C8BC0; Spawn 0x1009CDB0 (point, EF_NODRAW); Use
  0x100A3C80.
- Keys: base only; `iuser1` selects the flag.
- Use (CERTAIN): requires non-null activator (any entity). Loops `i = 1..maxClients`,
  `UTIL_PlayerByIndex` (edict not free), sets on **every connected player** (alive or dead):
  iuser1 0 -> `P+0x4CD`=1, 1 -> `P+0x4CE`=1, 2 -> `P+0x4CF`=1.
- Flags are in the player save table (`m_b_Ending_*`, FIELD_BOOLEAN, entries 0x10218BB8..BD8) and in
  the engine's COFINVENTORY snapshot list. CERTAIN.
- Story: 3. `c_roofs2` #329 (iuser1 0, from `multikill` on `monster_roofboss` death),
  `c_asylum1` #771 (iuser1 1, from `multits4` = P345 given), `c_hamn3` #307 (iuser1 2, from
  `multiposten` = mailbox `func_button` locked by item `package`).
- Hazard: players not connected at that moment miss the flag (DESYNC at the ending door).
- Treatment: the stage-4 OR rule (on change, join, respawn, rejoin) - and re-apply at the ending
  door (section 1).

### cof_credits
- Factory 0x1008F9E0, size 0xDC, vtable 0x101C8A70; Spawn 0x1009CB20 (point); Use 0x100A2710.
- Keys: `message` (credits text path; default `<gamedir>/txtfiles/credits.txt`), `iuser1` (type byte
  sent to the client), spawnflags 1 (skip evaluation), 2 (grant unlocks), 4 (message flag).
- Use (CERTAIN): requires non-null activator; loops all connected players; per player:
  - Evaluates only if `P+0x678` (stats valid, set by `cof_begingame`) and the map name contains
    `c_ending` (sets `P+0x679`) and not spawnflag 1.
  - With spawnflag 2 grants unlock bits via 0x100F2ED0, which writes **the server's**
    `scriptsettings.dat` / `alt.dat` (host machine), and checks them with 0x100DD000
    ("CheckingEndingOrUnlockables").
  - Ending number from the **player's own** flags: !R!G 1, !R G 2, R!G 3, R G 4, D 5, doctor mode
    (`P+0x680`) 6; rank 1..7 from play time/stats (0x10092F60 / 0x10092CE0).
  - Sends `Credits` **MSG_ONE** to that player: byte (custom file && !sf4), string path, the
    `playerstats` shorts (P+0x63C..0x674), a coord, byte valid, byte iuser1, byte sf1, byte
    difficulty, long new-unlock bits, byte ending number, byte rank, byte doctor mode, byte 0, byte
    IsMultiplayer, ... (0x100A3640-0x100A38A3).
  - No think, no FireTargets, **no map change**.
- After credits (client, LIKELY): the client credits panel on close runs local console
  `map c_game_menu1` (client.dll 0x10029DDC and 0x10031680) when its flag `0x10542A68` is non-zero
  (set to 1 at client init 0x1006DA09, 2 elsewhere) else sends `closegame`, which the server ignores
  in multiplayer (hl.dll 0x1001972E-0x10019755; SP: `EndSection("_oem_end_training")`). So each
  client ends its own session: the host's `map` restarts the listen server on the menu map; the
  guest's starts a local menu map.
- Story: 13 in 5 maps (`c_ending1c/2c/3c` 3 each, `c_ending4b` 3, `c_ending5c` 1): `creditstext`
  (iuser1 15, sf 2), `scrolltext` (iuser1 45, `message /txtfiles/conclusionN.txt`), `scrolltextoff`
  (iuser1 22, sf 1).
- Who: all connected players, each with their own evaluation. Hazards: WRONG-PLAYER-VISUAL when flags
  or `P+0x678/0x679` differ (late joiner: ending 0, rank 7); unlocks of both players go to the host
  file only.
- Treatment: shared ending flags (above) and copy `P+0x678..0x687` stats-validity bytes to late
  joiners (see `cof_begingame`); nothing for the message itself (already sent to all). Optionally:
  when the host's client issues `map c_game_menu1`, give the guest a moment (cosmetic).
- Confidence: server side CERTAIN; post-credits client action LIKELY.

### cof_begingame
- Factory 0x1008FCE0, size 0xDC, vtable 0x101C8CE0; Use 0x100A4080.
- Use (CERTAIN): requires non-null activator; all connected players: `P+0x678=1`;
  `P+0x67C = int(difficulty cvar)`; `P+0x679=1` iff map is `c_nightmare` or `c_doc_city`;
  `P+0x684 = cof_subtitlelanguage`; spawnflag 1 -> doctor mode `P+0x680=1`, `pev+0x268=1.0`,
  else `pev+0x268=0`.
- Story: 1 (`c_nightmare` #576 `begin`, from `nightmulti` at 0 s).
- Hazard: a guest who joins after `c_nightmare` never gets it -> credits without ending/rank
  (WRONG-PLAYER-VISUAL). The bytes are inside `playerstats` (P+0x638, 80 bytes), which the engine's
  COFINVENTORY already carries for rejoin.
- Treatment: on join copy `P+0x678, 0x679, 0x67C, 0x680, 0x684` from the host (or any player with
  `P+0x678=1`).

---

## 3. The P345 hand-over and the doctor weapon

### Story chain (c_asylum1), CERTAIN from entity data unless noted
1. `trigger_once *207` (thin plane at y 384-393, x -293..-182, corridor to the doctor) -> `2multits1`
   -> `multits1` (46 s scene: `trigger_camera` `1kam1..11` on the activator, `cof_teleport`
   `teledestts1t` at 0 s parks the activator at (-712,536,144), subtitles) -> at 46.1 s
   **`dohaveornot` (`cof_weapontrigger`, message `weapon_p345`)** with the toucher as activator.
2. Has P345 -> `multits3` -> at 12 s **`yesno`** (`cof_yesno`, `weaponneed weapon_p345`,
   `noitemmsg "I'd better go and get that gun for him... He said it's in the bowling alley."`,
   target `multits4`, notrigger `multits5`).
   Lacks it -> `multits2` (13.5 s scene, `teledestts2t` puts the activator in front of the doctor).
3. Later hand-over: `func_button *208` `givethep228b` (spawnflags 161, in front of the doctor's bars,
   y 724) -> `yesno`, activator = whoever presses +use. This is the normal SP path: the P345 is in
   `c_asylum2` (#93, the only `weapon_p345` in the campaign; `c_attic` can spawn another via
   `monstermaker spawnp345`).
4. Yes -> `multits4` (6 managers). Delay 0: `endingtrigger` (cof_ending 1, all players),
   **`thisisp345` (cof_doctorweaponset 1)**, `mmmonsters`, `killgaybar`, `yessub`, `kgivethep228b`
   (kills the button), `adoctor1_inv`, cameras/teleports `4teledestts1t` (0 s), `4teledestts2t`
   (1 s); later `removep228` (29.5 s, `cof_clearitems message weapon_p345` = strip from the
   activator), `givethekey` (gatekey2, sf 3), `enabledoor1`, `hitbyp228` (`cof_maxhealthchange`
   impulse 2 on the activator).
   No -> `multits5`: `thisrevolver` (cof_doctorweaponset 2) at 0, `hitbyrevolver`, `givethekey`,
   `enabledoor1`, `kgivethep228b` (also kills the button: "No" is final).
5. `c_attic` (doctor fight): `multidadoc` -> `docweaponsec` (5.1 s) and `doctorbar`;
   `monster_doctorboss` death (TriggerCondition 4) -> `multidscene` -> `whatweapon`
   (`spawnp345` / `spawnrevolver` monstermakers), `killtoncedeath`; `trigger_once *27 toncedeath`
   -> `multiyoudie` -> `triggerdocbody` and `deathuknow` (`cof_killplayer`).

### cof_weapontrigger
- Factory 0x100914E0, size 0xDC, vtable 0x101C6FA4; Spawn 0x1009E190; Use 0x100A8090.
- Keys: `message` (weapon classname), `target` (has), `noise` (has not).
- Use (CERTAIN): requires activator `IsPlayer` and all three keys;
  `HasNamedPlayerItem(activator, message)` (0x100E2380: walks `m_rgpPlayerItems[6]` at P+0x2514,
  chain +0x164, classname compare) -> `FireTargets(target or noise, activator, activator,
  USE_TOGGLE, 0)`.
- Who: **the activator** is the one checked. Story: 1 (`c_asylum1` `dohaveornot`).
- Hazard: host walks in first without the gun while the guest carries it -> "go get it" scene for
  the host. Not a soft-lock: the button path (step 3) still works for the carrier. NONE/confusing.
- Treatment: none needed beyond the hint below. Confidence CERTAIN.

### cof_yesno (+ `yesnoactivate` / `yesnono`)
- Factory 0x10018680, size 0xFC, vtable 0x101A743C; Spawn 0x1009E1D0; KeyValue 0x10096830;
  Use 0x100A8140; Yes 0x100A9E30; No 0x10098750; client commands at 0x1001C9D3 / 0x1001CAAF.
- Keys (CERTAIN): `weaponneed` +0xDC, `itemneed` +0xE0, `noitemmsg` +0xE4, `notrigger` +0xE8,
  `cvarset` +0xEC / `cvarvalue` +0xF4, `cvarsetno` +0xF0 / `cvarvalueno` +0xF8, `message` = tga.
- Use (CERTAIN): requires activator `IsPlayer` and `message` (else "YesNo error!"). Targetname
  `ryesno` -> activator `velocity.xy = 0`, `pev->fuser3 = 1.0`. `weaponneed` not held, or `itemneed`
  not in inventory (0x100E2240(item,0)) -> `noitemmsg` ProFont (colour 5) to the activator, stop.
  Otherwise `MSG_ONE YesNo(string tga, short entindex)` to the activator. **No pending state is
  stored.**
- `yesnoactivate <idx>` (sender, CERTAIN): entity must be `cof_yesno`; Yes(sender): `ryesno` ->
  sender `fuser3 = 0`; spawnflag 1 -> consume `itemneed` (0x100E2240(item,1)); `weaponneed` is only
  re-tested and the result discarded; `cvarset`; `FireTargets(target, sender, sender)`.
  `yesnono <idx>`: `ryesno` fuser3 reset, `cvarsetno`, `FireTargets(notrigger, sender, sender)`.
  Neither checks that the sender was prompted, holds the weapon, or that the prompt was already
  answered.
- Story: 2. `c_asylum1` #342 `yesno` (P345); `c_roofs2` #246 `ryesno` (escape the roof boss, from
  `trigger_multiple *44` wait 5; yes -> `multiescape` kills the boss without the death trigger, so
  RoofBossKilled is not set).
- Co-op hazards:
  - P345: a non-carrier who presses the doctor button gets the "go get that gun" text although the
    partner carries it (confusing).
  - **P345 answer by the guest -> doctor weapon on the guest (SOFT-LOCK, see below).**
  - `ryesno`: both players inside `*44` get prompts 5 s apart; two "yes" answers run `multiescape`
    twice (teleports, fades). The `fuser3=1` of a prompted player is cleared only by that player's
    own answer. DESYNC.
- Treatment:
  - Engine tracks prompts: when it sees `YesNo` MSG_ONE to player P for entity E, record (P,E);
    pass `yesnoactivate/yesnono E` from P only if (P,E) is pending, then clear every pending prompt
    for E (first answer wins); tell the other prompted players with a ProFont line.
  - When the `noitemmsg` ProFont goes to P and a partner owns the needed weapon
    (`SV_CoF_RetailOwnsWeapon`), send both a hint: "<carrier> has the P345 - <carrier> must give
    it to the doctor."
- Confidence: CERTAIN (code); double-fire consequence LIKELY.

### cof_doctorweaponset / cof_doctorweapontrigger / CDoctorBoss
- `cof_doctorweaponset`: factory 0x1008FB00, size 0xDC, vtable 0x101C77C0, Use 0x100A3AD0.
  Requires non-null activator; **FIRST player `m_iDoctorWeapon` (P+0x5B0) = pev->iuser1**. CERTAIN.
- `cof_doctorweapontrigger`: factory 0x1008FB60, vtable 0x101C78E0, Use 0x100A3B10. Reads FIRST's
  P+0x5B0: 1 -> `FireTargets(target, FIRST, this)`; 2 -> `FireTargets(message, FIRST, this)`;
  **anything else -> nothing**. CERTAIN.
- `monster_doctorboss` think 0x1002D800: while its own +0x39C == 0 it reads FIRST's P+0x5B0; if
  `<= 0` it only re-arms its think (0.05 s, 0x1002E757) and **runs no AI**; 1 -> body 0,
  +0x398 = 8; 2 -> body 1, +0x398 = 6. CERTAIN. These are the only three readers/writers of
  P+0x5B0 (code sweep). Spawn never resets it.
- Field persistence: `m_iDoctorWeapon` is in the player save table (entry 0x10218A98) so retail
  transitions carry it; it is **not** in the engine's COFINVENTORY list
  (`sv_cof_retail_inventory.c`) nor in the shared state (`sv_cof_state_data.h`), so a rejoin or late
  join gets 0. CERTAIN.
- Story: set 4 (`c_asylum1` `thisisp345`/`thisrevolver`; `c_attic` `dochasp345`/`dochasrevolver`),
  trigger 3 (`c_attic` `whatweapon`, `docweaponsec`, `triggerdocbody`).
- **Co-op hazard (SOFT-LOCK, LIKELY):** when the guest answers the yes/no, `multits4`/`multits5` run
  their delay-0 targets synchronously inside the guest's `yesnoactivate`/`yesnono` command, the
  engine makes the guest FIRST, and `thisisp345`/`thisrevolver` write the **guest's** P+0x5B0. The
  host stays 0. In `c_attic` the boss and the weapon triggers read the host (thinks run outside any
  command) -> boss never fights, `whatweapon` spawns nothing. Whether the idle boss can still be
  killed (which would let `multidscene` continue) is UNKNOWN. Same if the host rejoins (0 after
  rejoin) or the host slot changes.
- Treatment: add P+0x5B0 to the shared story state: whenever any player's value becomes 1 or 2,
  write it to every player (and to late joiners and rejoin restores; add `m_iDoctorWeapon` to
  COFINVENTORY). This is independent of who is FIRST. Log `state phase=doctor-weapon`.

### cof_doctorshoot
- Factory 0x1002ED60, size 0xEC, vtable 0x101AD38C; Spawn 0x1002EDF0 (takedamage YES, SOLID_BSP,
  MOVETYPE_PUSH, EF_NODRAW); TraceAttack (slot +0x60) 0x1002EE70; Use none (generic).
- Behaviour (CERTAIN): only reacts when the attacker is `monster_doctorboss` in a boss state equal to
  `iuser1` or `iuser1+4` (0x1002D690), at most once per second: sets the boss `pev->iuser2=1`,
  forwards the damage call to the boss (vfunc +0x64) and spawns a ricochet. Doctor-shot cover.
- Story: 4 (`c_attic` `toncedeath` *35..*38, removed by `killtoncedeath`). Hazard NONE.

### What breaks when the guest holds the P345 and the host does not (summary)
- First pass: if the host crosses `*207` first, the host gets the "go get it" scene; harmless, the
  hand-over goes through the doctor button anyway.
- Host presses the button: "I'd better go and get that gun for him..." (wrong hint).
- Guest presses the button and answers: P345 removal, health change, key and door all target the
  guest correctly (activator-based), ending flag goes to everyone, **but the doctor weapon goes to
  the guest -> c_attic boss broken** (above).
- If the only P345 is lost with its carrier (disconnect), "yes" becomes impossible; "No" still
  progresses (different ending).
- Engine should: share P+0x5B0 (critical); hint the carrier; optionally (piece 4) give
  `weapon_p345` to every player on pickup (duplicates are harmless: `removep228` strips only the
  answerer's copy).

---

## 4. Phones

### cof_phonecall (CPhoneCall)
- Factory 0x1005FB60, size 0x204, vtable 0x101B5A20; Spawn 0x10060F30; Precache 0x10060E50;
  KeyValue 0x10060140; Save/Restore 0x10060ED0/0x10060E90 (TYPEDESCRIPTION 0x10212A80, 7 fields);
  Use 0x10060FA0; thinks WaitForCall 0x10061000, CallThink 0x1005FC20, EndCall 0x1005FF90.
- Fields/keys (CERTAIN): `subtitlenumber1..32` -> +0xEC.. (`m_iSubtitleNumber`, base +0xE8),
  `subtitletime1..32` -> +0x170.. (`m_fSubtitleNumberNext`, base +0x16C), `audiofile` -> +0x1F0;
  `m_pPlayer` EHANDLE +0xE0 {edict*, serial}; `m_iCurrentSubtitle` +0x1F4; `m_hPhoneUser` +0x1F8;
  `m_bAnswered` +0x200; ring-started byte +0xDC (not saved). `iuser1` != 0 selects
  `phone_ringing.wav` (outgoing ring-back) instead of `phone_ringtone.wav`.
- Spawn: point, EF_NODRAW, +0xDC = 0, +0x1F4 = 1 if 0.
- Use 0x10060FA0 (CERTAIN): activator must be a player; `m_bAnswered = 0`; `m_pPlayer = activator`;
  think = WaitForCall in 0.1 s. **No "already used/in progress" guard** (a second Use re-targets and
  restarts).
- WaitForCall 0x10061000 (CERTAIN), every 0.1 s:
  1. `m_pPlayer` invalid or not a player -> **return without re-arming (call dropped)**.
  2. Not FL_ONGROUND -> wait.
  3. `!HasNamedPlayerItem("weapon_mobile")` -> **dropped**.
  4. First time (+0xDC == 0): set it; iuser1 != 0: `phone_ringing.wav` from the player,
     `m_iHideHUD |= 8`, `P+0x1EB4 = 1`, `pev->fuser3 = 5`, think again in 3 s, return.
     iuser1 == 0: `phone_ringtone.wav` (once, never repeated) and `P+0x1EA0 = 3` (incoming-call UI
     mode), fall through.
  5. Waits (re-arm 0.1 s) until: active item is `weapon_mobile` **and** `IsAlive` **and**
     `time > m_flNextAttack (P+0x308)`. **No timeout.** Then `m_iHideHUD |= 8`, `P+0x1EB4 = 1`,
     `fuser3 = 5`, `+0x1F4 = 1`, `m_hPhoneUser = player`, `P+0x308 = time+4`, think CallThink in 3 s.
- CallThink 0x1005FC20 (CERTAIN): `m_hPhoneUser` not a player, no active item, or active item not
  `weapon_mobile` -> **return without re-arming (dropped)**. Dead -> ProFont " " (colour 2), stop
  sound (`common/null.wav`), **dropped**. First run: stop the ring, `phone/tone_send.wav`, emit
  `audiofile` on the player's entity (CHAN_AUTO, vol 1, attn 0.8), phone animation, **`P+0x1EE8
  m_bInPhoneCall = 1`**. Later runs: `subtitle_main` -> `Use(FIRST, FIRST)` with the next number
  (subtitle_main loops **all** players and sends ProFont, 0x10077868-0x10077888), advance by
  `subtitletime`; when the next time is < 0.1 -> think EndCall in 1 s.
- EndCall 0x1005FF90 (CERTAIN): `P+0x1EA0 = 0`; no active item or not `weapon_mobile` -> **return,
  target never fired, call state never cleared**. Else `FireTargets(target, this, this,
  USE_TOGGLE, 0)` (**activator = the phonecall entity**), animation, `P+0x308`, `m_bInPhoneCall = 0`,
  tone, `m_iHideHUD &= ~8`, `P+0x1EB4 = 0`, `fuser3 = 0`, think = NULL (finished marker).
- CallThink and EndCall dereference `m_hPhoneUser` without a null check (0x1005FC30-0x1005FC37,
  0x1005FF9A-0x1005FFA1). Xash keeps a dropped client's edict and private data (`SV_DropClient`
  does not free it), so a disconnect mid-call LIKELY just continues the call on the zombie entity;
  an engine that ever frees player private data mid-call would crash here.
- `m_bInPhoneCall` consequences (CERTAIN): ClientCommand returns early for every command except a
  per-menu-map whitelist when `P+0x1EE8 != 0` (0x100190BE); many item/weapon paths also bail on it.
  Writers: only 0x1005FE6F/0x1005FE85 (set), 0x100600E5 (EndCall), 0x100EA1B4/0x100EAEB2 (player's
  own emergency call). Spawn does not reset it; it is saved (`m_bInPhoneCall`, 0x102185E8).
- Who: activator only. Partner: hears ring/voice only if near the recipient (spatial
  EMIT_SOUND); sees the subtitles (broadcast); no HUD change.
- Story: 2.
  - `c_basement` #675 `basementcall` (iuser1 1, `subtitles/magphone.wav`, 12 subtitles): fired by
    `cof_phonecheck` #671 (number 543214) -> activator = the dialer; end -> `addnotemm` ->
    `phonecode` (`cof_addcodenote` iuser1 4, FIRST) and `keypad4` (`cof_keypad4`, FIRST, code 5247).
  - `c_park3` #347 `sophiecall` (iuser1 0, `cutscene/park_call.wav`, 9 subtitles): `trigger_once
    *55` -> `multiparkcall` -> sophiecall (toucher); end -> `gateinter` (`cof_interdooronoff`)
    unlocks `gatebutton` (`func_button`, "The phone is ringing.") -> `multigateopen`.
- Co-op hazards:
  - **SOFT-LOCK:** `c_park3` toucher without `weapon_mobile` (any player who was not the `c_start`
    `givemobile` activator, any late joiner) -> dropped at once, gate locked forever.
  - **SOFT-LOCK:** recipient dies while waiting: after MP respawn the inventory is cleared, the next
    WaitForCall sees no phone -> dropped (unless the engine's recovery restores the phone first:
    UNKNOWN race). Recipient dies during the call -> dropped.
  - **STUCK-PLAYER:** recipient dies during the call -> `m_bInPhoneCall`, `P+0x1EB4`,
    `fuser3 = 5` (and HUD bit 8 until MP spawn resets it, LIKELY 0x100C94EA) stay set; all client
    commands ignored for the rest of the session, carried by transitions.
  - Partner cannot answer; nobody is told the call is waiting for someone else.
- Recommended engine treatment ("every call reaches a player and fires its target once"):
  1. **Phone for everyone:** treat `weapon_mobile` as a party item: when any player owns it (from
     `c_start` `givemobile` on), give it to every other player and to joiners/respawns (engine give
     path), and mirror the broken/repaired state (`P+0x1F14 m_iPhoneDisabled`, mobile +0x1F9,
     body `P+0x5CC`).
  2. **Recipient fix-up (per frame):** for each `cof_phonecall` whose `m_pfnThink` (+0x78) is
     WaitForCall 0x10061000 and whose think is scheduled: if `m_pPlayer` (+0xE0) is invalid, dead,
     not spawned, lacks `weapon_mobile` or has `m_bInPhoneCall`, rewrite the EHANDLE
     ({edict, edict->serialnumber}) to the best eligible player (prefer the original activator,
     then the nearest alive partner owning the phone). Send a ProFont "Your phone is ringing - take
     it out to answer." to the chosen player and "<name> is getting a call." to others.
  3. **Drop detection:** `m_pfnThink` in {0x10061000, 0x1005FC20, 0x1005FF90} with no pending
     nextthink = dropped (a finished call has `m_pfnThink == 0`). Recovery: clear the old
     recipient (`P+0x1EE8=0`, `P+0x1EB4=0`, `fuser3=0`, `m_iHideHUD &= ~8`, `P+0x1EA0=0` if 3);
     pick an eligible player, set `m_pPlayer`, `m_bAnswered=0`, think = WaitForCall,
     nextthink = time+0.1 (+0xDC stays 1, so no re-ring; the call restarts from subtitle 1).
     If nobody is eligible for 30 s, fire the target once yourself:
     `FireTargets(STRING(target), self, self, USE_TOGGLE, 0)` (cdecl 0x10101FF0, same activator as
     EndCall) and set `m_pfnThink = 0`. Track per entity so the target fires exactly once.
  4. **Stuck flag sweep:** any player with `P+0x1EE8 = 1` who is not the `m_hPhoneUser` of a live
     call and is not in an own call (`P+0x3A0 == 0`) -> clear the four call fields. Also clear them
     on death.
  5. Optional: copy the call audio (`pfnEmitSound` of `audiofile` on the recipient) to partners out
     of earshot.
- Confidence: code CERTAIN; MP inventory timing at death/respawn UNKNOWN; HUD reset on MP spawn
  LIKELY.

### cof_phonecheck (+ `phonenumber`)
- Factory 0x10090EE0, size 0x368, vtable 0x101C3698; Spawn 0x1009DD00 (point); KeyValue 0x100BF970;
  Use none. Client command `phonenumber <n>` (0x100197DB) -> `CBasePlayer::PhoneNumber` 0x100E9FF0.
- PhoneNumber (CERTAIN): FIRST `cof_phonecheck` by classname; `owner = dialer`; `frags == 1` ->
  0x100A1CD0: `n == pev->impulse` -> `FireTargets(target, dialer, dialer)`, noise, and
  `UTIL_Remove(self)` unless spawnflag 1 (one-shot); else 0x10094270 (radius variant, unused in
  story). Then 911/112 emergency-call handling for the dialer. `phonenumber` is ignored while the
  dialer is in a call (command gate) or in UI mode 1.
- Who: the dialer (any player). Story: 1 (`c_basement` #671, impulse 543214, frags 1).
- Hazard NONE (the dialer has the phone out, so `basementcall` reaches them). The code at the end
  goes to FIRST (host) because EndCall runs in a think; the stage-4 code sharing covers it.

### cof_phonedisable
- Factory 0x10090F40, size 0xDC, vtable 0x101C6AA4; Use 0x100A7480.
- Use (CERTAIN): FIRST player only: for its `weapon_mobile`: mobile +0x1F9 = 1 (broken), bodygroups
  (1,7) and (2,1), `P+0x1F14 m_iPhoneDisabled = 1`, `P+0x5CC = body`. Repair (weapon_mobile code
  0x10052FA6) clears the owner's flags only.
- Story: 1 (`c_subway1st1` #408 `disablefuck`, `trigger_once *84` iuser1 1, enabled by
  `cof_triggeron`). Next to it `batterilagt` SMS (#406).
- Hazard: host's phone breaks, guest's does not (DESYNC); if the guest uses the battery the ledger
  removes the host's copy and the host's phone stays broken. Whether a broken phone can still be
  raised to answer a later `cof_phonecall` is UNKNOWN.
- Treatment: mirror break/repair to all players (fields above).

### trigger_cofmobile (SMS)
- Factory 0x10091300, size 0xE0, vtable 0x101C3B90; KeyValue 0x100967C0 (`bodynumber` -> +0xDC);
  Spawn 0x1009E090; Use 0x100A7C50.
- Use (CERTAIN): all connected players who own `weapon_mobile` with +0x1F9 == 0: `P+0x5CC =
  bodynumber`, `P+0x1EC0 = 1` (unread), spawnflag 2 -> `P+0x1F1C = 1`, `P+0x1F20 = time`;
  `UnreadSMS` MSG_ONE; animation if the phone is out; unless spawnflag 1, `mobile_sms.wav` +
  `phone_vibrate.wav` on that player.
- Story: 7 (`c_start` 3: `givesms1` (sf 1, from the intro scene at 45 s), `sms2hehe`, `sms3hehe`
  (sf 1); `c_apartment1` 2; `c_city` 1; `c_subway1st1` `batterilagt` (sf 3)). All from
  `trigger_once` or scene managers.
- Hazard: players without a phone (guest) miss every SMS (WRONG-PLAYER-VISUAL / lost hint).
- Treatment: phone for everyone (above); on giving a joiner the phone copy `P+0x5CC`, `P+0x1EC0`.

### Partner visibility of phone events (summary)
Ring, ringtone, call audio, SMS sounds: spatial sounds on the recipient's entity only. Subtitles of
calls: all players (subtitle_main loop). UI/HUD state (`P+0x1EA0`, `P+0x1EB4`, `m_iHideHUD` 8,
`fuser3`): recipient only.

---

## 5. Other assigned classes

### cof_telephone (+ `telephoneright` / `telephonewrong`)
- Factory 0x10018620, size 0xE4, vtable 0x101A8224; Spawn 0x1007CDB0 (point); Use 0x1007CF70;
  right 0x1007CDF0 -> think 0x1007CC90; wrong 0x1007CEB0 -> think 0x1007D020 -> 0x1007CBA0.
- Use (CERTAIN): activator player and `pev->health == 0` (not solved): activator
  `m_hTelephone` (P+0x368) = this; `m_hUser` (+0xDC) = activator; `Telephone(1, message)` MSG_ONE.
- `telephoneright` / `telephonewrong` (client commands 0x1001C731/0x1001C794) act on the **sender's**
  `P+0x368`. Right: `health = 1` (one-shot), `Telephone(3)` + dialing sound to `m_hUser`, 7 s later
  `Telephone(0)`, clear the user's handle, `FireTargets(target, this, this)`. Wrong: 7 s wrong-number
  sound, 6.1 s later `Telephone(2)` (retry). The right/wrong decision is made by the client.
- Story: 1 (`c_asylum1` #680 `telephoneent`, from `func_button *217 buttonphone` (wait 1), target
  `multiphone` -> cutscene `multiphone2`).
- Hazards: a second player using the phone while the first is dialing overwrites `m_hUser`; the
  first player's answer then drives UI messages to the second, whose dial UI was never meant to
  close, while the first player's UI stays open (WRONG-PLAYER-VISUAL). If `m_hUser` is gone in the
  7 s window the right-think skips only the UI message and still fires the target
  (0x1007CCA1 -> 0x1007CD17), so no soft-lock. The cutscene runs with activator = telephone entity
  (non-player), so downstream cameras pick their own player (other agent's scope).
- Treatment: while a telephone has `m_hUser` set and `health == 0`, refuse other players' +use on
  `buttonphone` (or tell them "<name> is using the phone").

### cof_killplayer
- Factory 0x1008FE60, size 0xDC, vtable 0x101C76A0; Spawn 0x1009D020 (point); Use 0x100A42C0.
- Use (CERTAIN): requires non-null activator; **every connected player**: `m_iDeathBits` (P+0x5B4)
  |= spawnflags & 0x3F (presentation bits: 8 = no death fade, 1/2/0x20 skip death extras, 0x10 death
  menu byte, LIKELY), `pev->health = 0`, `Killed(pev, gib 1)` (vfunc +0x6C). In MP the second kill
  sees no survivors -> team wipe ("Game over", restart / engine checkpoint).
- Story: 6. Fall-death floors via `trigger_once`: `c_ending1b/2b/3b` `fuckingdie` (`*26`, map-wide
  volume z -1154..-885, sf 39), `c_ending5` `killmefuck` (`*48`, z -2463, sf 6), `c_subwaysick1`
  `fuckingdie` (`*112`, sf 39). Scripted: `c_attic` `deathuknow` (sf 39) at the end of
  `multiyoudie` (doctor executes you after crossing `*27`).
- Hazard: one player falling kills the partner too (team wipe; recoverable via checkpoint).
- Treatment: for the five fall floors, intercept the touch of those `trigger_once` brushes (static
  list by map + model) and kill only the toucher (game `TakeDamage` via vtable, DMG_FALL, lethal),
  skipping the touch so the trigger stays for the partner. Keep `c_attic` retail. A vtable-slot
  replacement of `cof_killplayer::Use` would also work but is a last resort.

### cof_entityrestore
- Factory 0x1008FC20, size 0xDC, vtable 0x101C7F0C; Spawn 0x1009CE00; Use 0x100A3D10.
- Use (CERTAIN): activator must be a player. `impulse == 0` (save): clear the **activator's**
  `transferredentity_*` arrays (P+0x19A8 message, +0x1A74 origin, +0x1CD8 classname, +0x1DA4 clip;
  saved fields) and record up to 50 world entities whose `pev->armorvalue == 666.0` (the
  player-dropped-item marker written by the drop code 0x100DE148..0x100DE971; any player's drops),
  except the activator's own edict. `impulse != 0` (restore): re-create the activator's list
  (`item_key` with message, `weapon_*` with clip) at the stored origins, then clear it.
- Story: 2. `c_college1` #481 `saveitems` (from `trigger_multiple *53`, wait 2) and
  `c_college1dark` #344 `restoreitems` (impulse 1, from `trigger_once *55`).
- Hazard: the list lives in whoever touched the save trigger last; the restore uses whoever touches
  `*55` first. If that player never passed `*53` (warped, joined late), dropped items vanish
  (DESYNC; story keys are covered by the item ledger, weapons are lost).
- Treatment: after every `saveitems` fire, copy the activator's four arrays to all other players
  (COFINVENTORY already carries them across rejoin).

### cof_triggeron
- Factory 0x10091360, size 0xDC, vtable 0x101C5EA8; Use 0x100A7F00.
- Use (CERTAIN): first entity named `target`; if `trigger_once` or `trigger_multiple`,
  `pev->iuser1 = 0`. `CBaseTrigger` touch filter 0x10109C40 refuses all touches while
  `iuser1 != 0`, so this enables a trigger. No activator use. World only.
- Story: 21 in 16 maps. Hazard NONE.

### cof_deadcat
- Factory 0x1008FAA0, size 0x100, vtable 0x101C91A0; Spawn 0x1009CBE0 (model, drop to floor, use
  fn 0x100931F0); ObjectCaps 0x100988A0 (+use).
- Use 0x100931F0 (CERTAIN): alive player activator, cooldown `fuser1`; after 5 uses, "Hello
  kitty..." + `hellokitty.wav` and unlock bit 0x200 into the user's `P+0x42C`; otherwise "A dead
  cat..." (colour 5), `iuser1++`. Activator only, cosmetic.
- Story: 1 (`c_subtunnel1` #327). Hazard NONE.

### cof_barshoot
- Factory 0x10021900, size 0xEC, vtable 0x101AA498; Spawn 0x100219B0 (takedamage, SOLID_BSP);
  TraceAttack 0x10021A30.
- Shot (CERTAIN): ricochet at the hit point; at most once per voice line, a random line different
  from the last one is played from the `message` entity. No player state.
- Story: 1 (`c_asylum1` #743 `gaybar`, message `adoctor1`, removed by `killgaybar` in both P345
  branches). Hazard NONE.

### cof_cracker
- Factory 0x100907C0, size 0x368, vtable 0x101C463C; Spawn 0x1009D8A0; Use 0x100A6A60.
- Use (CERTAIN): every entity within 1024 u whose classname is `monster_sewerboss` or `player`, with
  takedamage and health > 0, takes `pev->dmg` (+0x1E4) as DMG_SHOCK (vfunc +0x64). **All players in
  range**, not the FIRST player (correcting the earlier LIKELY note).
- Story: 1 (`c_sewer3` #210 `fittfanfittfan`, dmg 50, fired at 3 s by the four electrocution button
  managers `but1..4`). Hazard NONE (same per-player rule as SP; a partner standing in the arena takes
  50 per shock).

### cof_bosshealthbar
- Factory 0x10090580, size 0xE4, vtable 0x101C6E5C; Use 0x100A5BF0.
- Use (CERTAIN): activator must be a player; `iuser1 = 1`; needs `fulltga` (+0xDC), `emptytga`
  (+0xE0) and a live boss named `message`. **If `IsMultiplayer()` and the use value != 666.0: every
  connected player** gets `P+0x5F4 = 1`, tgas, max health (+0x600), health (+0x604), boss name
  (+0x608) and `BossBar` MSG_ONE; otherwise the activator only. Story triggers use value 0, so in
  story co-op all players get it. `CBasePlayer::Spawn` clears `P+0x5F4` (0x100F44F4);
  UpdateClientData re-sends `BossBar` from the player's own fields on InitHUD (0x100F7711).
- Story: 8 (`c_apartmentsick`, `c_attic`, `c_roofs2`, `c_sewer3`, `c_ending1c/2c/3c`, `c_ending4b`).
- Hazard: a player who respawns or joins mid-fight has no bar (WRONG-PLAYER-VISUAL).
- Treatment: on respawn/join, if a partner has `P+0x5F4 = 1`, copy `P+0x5F4..0x60B` and force an
  InitHUD resend (or send `BossBar` ourselves).

### cof_goodpoints / cof_goodpointstrigger
- Factories 0x100909A0 / 0x10090A00; Use 0x100A6D90 / 0x100A6DF0 (activator's `m_iGoodPoints`).
- **Not present in any of the 81 story maps** (0 instances). Nothing to do.

---

## 6. Summary table

| Class | Affected | Hazard | Treatment |
|---|---|---|---|
| inter_door (normal) | opener only | NONE | existing door follow |
| inter_door `endingcheck` (c_village3) | opener only, opener's flags | DESYNC | OR flags at door-begin; move all players to the same `ending_exitN` with fade/freeze; keep exit hold |
| inter_door_exit | none | NONE | nothing |
| inter_door_disable | world (player activator gate) | NONE | nothing |
| cof_inter_door_exitchange | world | NONE | nothing |
| cof_interdooronoff | world | NONE | nothing |
| cof_ending | all connected | DESYNC for absent players | OR rule incl. joins/rejoins |
| cof_credits | all, own flags/stats; host file unlocks | WRONG-PLAYER-VISUAL | shared flags + stats bytes; accept post-credits menu |
| cof_begingame | all connected | WRONG-PLAYER-VISUAL (late join) | copy P+0x678..0x684 on join |
| cof_weapontrigger | activator | NONE (confusing) | hint |
| cof_yesno + answers | activator prompt; any sender answers | DESYNC (double fire); P345 hint | pending-prompt filter, first answer wins, carrier hint |
| cof_doctorweaponset | FIRST (= sender inside commands) | SOFT-LOCK in c_attic | share P+0x5B0 to all, add to COFINVENTORY |
| cof_doctorweapontrigger | FIRST | (part of above) | (same) |
| cof_doctorshoot | world | NONE | nothing |
| cof_phonecall | activator | SOFT-LOCK + STUCK-PLAYER | phone for all; retarget recipient; detect drops and restart/fire once; clear stuck call flags |
| cof_phonecheck / phonenumber | dialer | NONE | nothing |
| cof_phonedisable | FIRST | DESYNC | mirror break/repair |
| trigger_cofmobile | all owning a phone | WRONG-PLAYER-VISUAL | phone for all |
| cof_telephone | activator / sender | WRONG-PLAYER-VISUAL | one user at a time |
| cof_killplayer | all connected | team wipe | kill only the toucher on the 5 fall floors |
| cof_entityrestore | activator list | DESYNC (items lost) | copy list to all on save |
| cof_triggeron | world | NONE | nothing |
| cof_deadcat | activator | NONE | nothing |
| cof_barshoot | world | NONE | nothing |
| cof_cracker | all players within 1024 u | NONE | nothing |
| cof_bosshealthbar | all (MP) | WRONG-PLAYER-VISUAL after respawn/join | copy bar fields, resend |
| cof_goodpoints(+trigger) | - | absent from story | nothing |

## 7. Open questions for a live test

1. P345 answered by the guest: does `thisisp345` really land on the guest (sender-first + sync
   multi_manager), and in `c_attic` does the doctor stay idle; can he still be killed?
2. MP death with an active or waiting `cof_phonecall`: are weapons removed at death or at respawn,
   and does the engine's inventory recovery restore `weapon_mobile` before the next 0.1 s
   WaitForCall think?
3. Does MP PlayerSpawn (0x100C94EA) reset `m_iHideHUD` bit 8, and does a stuck `fuser3 = 5` block
   weapons/movement?
4. Can a broken phone (`m_iPhoneDisabled`, +0x1F9) be raised to answer `sophiecall`?
5. After the credits, does each client run `map c_game_menu1` (host ends the server while the
   guest's panel may still be open)?
6. Are the five `c_village3` ending rooms walled off from each other (partner placement and stray
   touches of the neighbouring `trigger_changelevel`)?
7. `ryesno` in `c_roofs2` with both players inside `*44`: does a double "yes" visibly break the
   escape sequence?
8. `c_college1dark` restore with the non-saving player touching `*55` first: confirm the item loss.
