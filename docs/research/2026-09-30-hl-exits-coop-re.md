# hl.dll level exits and built-in co-op machinery (static RE), 2026-09-30

Binary: `runtimes/enhanced/cryoffear/cl_dlls/hl.dll`, SHA-256 `0036B91C01E92ED205513F52563053A55A66A32D257EFB5F476B8F1F3DDE0E63` (stock), image base `0x10000000`. Cross-checks were made against `client.dll` in the same folder and entity lumps of the retail BSPs. The analysis was static only: capstone plus pefile linear sweep, reading vtables, TYPEDESCRIPTION save tables and string xrefs. Nothing was launched or modified. All addresses are preferred-base VAs.

Confidence tags: **CERTAIN** means read directly from the machine code. **LIKELY** means the code was read but a name or meaning is inferred from HLSDK parity. **UNKNOWN** means not traced.

## 0. Layout facts used below

- **CoF `entvars_t` is shifted.** The save table `gEntvarsDescription` at `0x1021AAE0` shows one extra dword after `sequence`. From `frame` onward, every field is 4 bytes later than in HLSDK. Used offsets: `spawnflags 0x1A4`, `flags 0x1A8`, `target 0x1CC`, `targetname 0x1D0`, `netname 0x1D4`, `message 0x1D8`, `dmgtime 0x1E8`, `noise 0x1EC`, `iuser1 0x248`, `iuser3 0x250`, `fuser1 0x258`, `fuser3 0x260`, `fuser4 0x264`. **CERTAIN**
- **Engine function table** at `0x10222F48`, slot n at `+4n`, standard `enginefuncs_t` order. Examples: ChangeLevel `[0x10222F60]`, AlertMessage `[0x1022303C]`, MessageBegin `[0x10223000]`, RegUserMsg `[0x10223074]`. **CERTAIN**
- **Game rules.** InstallGameRules (`0x100AF710`) picks the rules from `deathmatch`. With `deathmatch==0` it builds single-player rules: 0xE8 bytes, vtable `0x101DE3B0`, sets `sv_coop 0`. Otherwise it builds the MP/co-op rules: 0xE4 bytes, ctor `0x100C5180`, vtable `0x101D608C`, sets `sv_coop 1`.
  - Rules vtable slots: `+0x20` IsMultiplayer, `+0x24` IsDeathmatch, `+0x2C` IsCoOp (MP returns `(int)gpGlobals->coop`, `0x100C9090`), `+0x74` PlayerKilled. **CERTAIN** for the return values, **LIKELY** for the names.
  - The known ChangeLevelNow bail-out is therefore Valve's `IsDeathmatch()` test.
- **Entity vtables.**
  - CBaseEntity: `+0x08` SetNextThink(float), `+0x2C` Spawn, `+0x30` Precache, `+0x34` KeyValue, `+0x40` ObjectCaps, `+0xE8` Use.
  - Player: `+0xBC` IsAlive (`deadflag==0 && health>0`), `+0xD0` IsPlayer, `+0xD4` IsNetClient. **CERTAIN**
- **Helpers:**
  - UTIL_SetOrigin `0x10119FE0`
  - UTIL_PlayerByIndex `0x10119680`
  - FindEntityByTargetname `0x10118590`
  - FindEntityByClassname `0x10118460`
  - UTIL_EntitiesInBox `0x10118390`
  - CBaseEntity::Intersects `0x10017D80`
  - FireTargets `0x10101FF0`
  - UTIL_IsMasterTriggered `0x10119060`
  - EMIT_SOUND_DYN `0x100FC9F0`
  - CBasePlayer::EnableControl(BOOL) `0x100DEFF0`: FALSE sets FL_FROZEN 0x1000 and zeroes velocity.
  - Player PlayMP3 `0x100EA5C0`

## 1. `trigger_changelevel` → `CChangeLevel::ChangeLevelNow` (0x10109D40)

**Class setup.** The factory is at `0x10107770`, size 0x1C8, vtable `0x101E2664`.

KeyValue (`0x1010BD90`) stores the keys like this (**CERTAIN**):

| key | storage | notes |
|---|---|---|
| `map` | `char m_szMapName[32]` @ this+0x180 | strcpy. Warns `Map name '%s' too long (32 chars)` if ≥32. **Lower-cased in place**, CoF-specific. |
| `landmark` | `char m_szLandmarkName[32]` @ +0x1A0 | strcpy, same length warning |
| `changetarget` | `string_t m_changeTarget` @ +0x1C0 | ALLOC_STRING |
| `changedelay` | `float m_changeTargetDelay` @ +0x1C4 | atof |

**Spawn and wiring** (`0x101105D0`, **CERTAIN**):

- Warns `a trigger_changelevel doesn't have a map` and `trigger_changelevel to %s doesn't have a landmark`.
- If `pev->targetname` is set, it installs `SetUse(UseChangeLevel)`.
- `InitTrigger` (`0x1010BA80`) sets SOLID_TRIGGER, MOVETYPE_NONE and SetModel, plus EF_NODRAW unless `showtriggers`.
- `if (!(spawnflags & 2)) SetTouch(TouchChangeLevel)`. Spawnflag 2 is SF_CHANGELEVEL_USEONLY and is honoured here, at Spawn time.

**TouchChangeLevel** (`0x101135E0`) only checks `FClassnameIs(pOther,"player")` and then calls `ChangeLevelNow(pOther)`. It has no alive check and no master check. **UseChangeLevel** (`0x10115AF0`) calls `ChangeLevelNow(pActivator)` with no checks. These are the only two callers (`0x1011362A`, `0x10115AF6`). **CERTAIN**

**ChangeLevelNow, in order.** The body is Valve's triggers.cpp with one CoF addition. **CERTAIN**

1. `0x10109D4F–D60`: `if (g_pGameRules->IsDeathmatch()) return;`. Our engine patches this out.
2. `0x10109D66–D86`: `if (gpGlobals->time == pev->dmgtime) return; pev->dmgtime = gpGlobals->time;`. This is an equality debounce on pev+0x1E8 and is the only per-call guard.
3. `0x10109D84–DAD`: `pPlayer = Instance(INDEXENT(1))`. It **always uses client slot 1**, not the activator.
4. `0x10109DB9`: `InTransitionVolume(pPlayer, m_szLandmarkName)` (`0x1010B9C0`).
   - Returns true if the entity has FCAP_FORCE_TRANSITION, follows `aiment` for MOVETYPE_FOLLOW, and defaults to true when no `trigger_transition` has that name.
   - On failure it logs `Player isn't in the transition volume %s, aborting` (at_aiconsole) and returns. dmgtime has already been stamped by then.
5. `0x10109DE7–DF1`: `if (pPlayer->IsPlayer()) pPlayer->m_iPlayerSaveLock /*+0x4C8*/ = 1337;`. This is the CoF addition.
   - hl.dll never reads this field. It is only written: here, in Restore (`0x100F1974`), and in PostThink (`0x100EB50D`). The PostThink write is gated by the "just restored" global byte `0x10226476`.
   - It persists through the player save table. **CERTAIN** that there are no direct reads; the purpose is **UNKNOWN**.
6. `0x10109DFB–E52`: `if (m_changeTarget)`:
   - `pFireAndDie = GetClassPtr((CFireAndDie*)NULL)` (`0x10107890`, 0xEC bytes, vtable `0x101E2538`).
   - It sets `pev->target = m_changeTarget`, `m_flDelay = m_changeTargetDelay`, and `origin = player1->origin`, then calls `DispatchSpawn` (`0x100167D0`).
7. `0x10109E5A`: `strcpy(st_szNextMap /*0x10231014*/, m_szMapName)`.
8. `0x10109E77–E90`: `m_hActivator = pActivator; SUB_UseTargets(pActivator, USE_TOGGLE, 0)` (`0x10102930`).
   - This fires `pev->target` and the killtarget, or spawns a `DelayedUse` if `delay` is set.
9. `0x10109E96–EE9`: `st_szNextSpot[0]=0; pent = FindLandmark(m_szLandmarkName)` (`0x1010ADA0`: a targetname match with classname `info_landmark`, else at_error `Can't find landmark %s`).
   - If found: `strcpy(st_szNextSpot /*0x10231034*/, name); gpGlobals->vecLandmarkOffset (+0xA0) = landmark origin`.
10. `0x10109EFA`: `ALERT(at_console,"CHANGE LEVEL: %s %s\n")`, then `0x10109F10`: `CHANGE_LEVEL(st_szNextMap, st_szNextSpot)`.

ChangeLevelNow has **no** fade, sound, freeze, HUD message, inventory save or engine save hook. **CERTAIN**

**`changetarget` never fires in the source map.** CFireAndDie behaves as follows (**CERTAIN**):

- Spawn (`0x101106F0`) only sets classname `fireanddie`.
- Precache (`0x1010EEB0`) is the only place `SetNextThink(m_flDelay)` is called.
- ObjectCaps (`0x1010E960`) returns `|0x80` (FCAP_FORCE_TRANSITION).
- Think (`0x10111C00`) calls `SUB_UseTargets(USE_TOGGLE)` and then removes the entity.
- DispatchSpawn calls only Spawn (`call [eax+0x2C]` at `0x10016868`).

So the entity only thinks after a `changelevel2` save/restore in the *destination* map. With a plain `changelevel` (NULL landmark), `changetarget` is **lost**. **LIKELY** for the runtime consequence.

**If the engine refuses or defers the call.** Touch runs again on every player move that overlaps the trigger. The only thing stopping repeats is the equality test on `gpGlobals->time`.

In GoldSrc, `SV_RunCmd` sets `gpGlobals->time` per usercmd (ReHLDS parity, **LIKELY**). So several calls per server frame are possible, and one per frame at minimum. Every call that passes the debounce repeats **all** of the following:

- The transition-volume check against client 1.
- The `m_iPlayerSaveLock=1337` write (idempotent).
- **A new CFireAndDie edict** if `changetarget` is set. These accumulate, never fire in this map, and would *all* fire after a later changelevel2.
- **`SUB_UseTargets(USE_TOGGLE)`**, which re-toggles `target` every call. This flips relays, doors and multi_managers back and forth. It also re-runs `killtarget`, or spawns another `DelayedUse` each call if `delay` is set.
- The strcpy of the map and landmark, and a rewrite of `vecLandmarkOffset`.
- The `CHANGE LEVEL:` log line.
- The engine call itself.

The retail engine's spawncount latch (see `docs/transition-api-analysis.md`) would drop every engine call after the first, but it does not stop the DLL side effects above. **CERTAIN** for the DLL part.

**All CHANGE_LEVEL call sites in hl.dll** (**CERTAIN**):

| VA | Caller | Landmark |
|---|---|---|
| `0x10109F10` | ChangeLevelNow (trigger_changelevel) | `st_szNextSpot` (non-NULL buffer) |
| `0x1004ACB7` | CInterDoorCoopChangeLevel::ChangeLevelThink: `CHANGE_LEVEL(STRING(pev->message), NULL)` | NULL |
| `0x100A1FF9` | cof_changelevel Use: `CHANGE_LEVEL(STRING(pev->message), NULL)` | NULL |
| `0x100E9CF9` | MultiplayerPlayerDeathThink team-wipe restart: `CHANGE_LEVEL(STRING(gpGlobals->mapname), NULL)` | NULL |
| `0x1009A3AD/BC` | CCoFCoopStats::RestartCampaign: `message` or `"cof_campaign_01"` | NULL |
| `0x100C7228` | MP rules ChangeLevel/mapcycle (in `0x100C6FD0`, **LIKELY**) | NULL |

There is also `NextLevel()` at `0x1010E880`. It finds `trigger_changelevel`, otherwise `c_start`, and runs `ExecuteChangeLevel` (`0x1010AAC0`, which only sends SVC_INTERMISSION). `CInterDoorCoop::EndGame` (`0x1004ACE0`) instead uses `CLIENT_COMMAND(player,"map c_game_menu1\n")`.

## 2. Built-in co-op exits

**Retail wiring** (read from BSP entity lumps). All `c_cof_*` and `cof_campaign_01*` co-op maps pair a `trigger_coop_changelevel` (targetname e.g. `coopchangelevel`, `rendermode 2`, no EF_NODRAW set in its Spawn) with one of two users:

- an `inter_door_coop_changelevel` with `target`=that name, `message`=next map and `noise`=door sound.
- a `func_coopallplayersbutton` (bridge, cabin, gasstation) with `message`=that name and `target`=a sequence. That sequence later fires a `cof_changelevel` with `message`=next map.

The single-player story maps use none of these.

### 2a. `trigger_coop_changelevel` (factory `0x101081F0`, 0x180 bytes, vtable `0x101E4C20`)

The class uses the base KeyValue, so it has no custom keys. **CERTAIN**

Spawn (`0x10110C40`) sets SOLID_TRIGGER, MOVETYPE_NONE and SetModel, calls `SetThink(InsideThink)` and `SetNextThink(0.1)`. It never changes level and shows no UI.

**InsideThink** (`0x1010BB40`), every frame (`SetNextThink(0)`):

```
n      = UTIL_EntitiesInBox(list, 32, pev->mins, pev->maxs, FL_CLIENT);   // 0x1010BB9A
total  = CountAliveParticipants();                                        // 0x1010A400
inside = count of list[i] with this->Intersects(list[i]);                 // 0x10017D80
pev->iuser1 = (inside >= total) ? 1 : 0;                                  // 0x1010BBE9..BC0F
```

- `CountAliveParticipants` (`0x1010A400`) loops slots `1..maxClients` and counts players with `IsAlive() && pev->fuser4 == 0.0`.
- `inside` is **not** alive-filtered. Corpses and non-participants inside the box also count.
- Because the test is `>=`, a server with zero alive participants counts as ready.
- There is no timeout. The trigger is only a flag (`pev->iuser1`) for other entities to poll.

Player `fuser4` is only ever reset to 0 in hl.dll: Spawn `0x100F448B` and MP InitHUD `0x100C8B84`. Every team loop filters on `fuser4==0`, so it acts as a "participating player" flag. **LIKELY** for the meaning; **CERTAIN** for the resets.

### 2b. `inter_door_coop_changelevel` (factory `0x100496B0`, 0x184 bytes, vtable `0x101B1300`)

- It is a +USE brush: ObjectCaps `0x1004B5C0` returns 8 (FCAP_IMPULSE_USE), and Spawn `0x1004BAD0` sets SOLID_BSP and MOVETYPE_PUSH.
- KeyValue (`0x1004B440`) adds `music` at this+0x180.
- Precache precaches `noise`.

**Use** (`0x1004C100`) runs these steps. **CERTAIN**

1. It requires an activator that `IsPlayer`, `UTIL_IsMasterTriggered(m_sMaster)`, and non-null `pev->target` and `pev->message`.
2. If `music` is set, it runs `CVAR_SET_STRING("sv_coop_music", music)`. This happens before the readiness check.
3. `pTrig = FindEntityByTargetname(pev->target)`.
4. If the boss lock global `[0x10222F20]` is set (set in the CBookSimon spawn, `0x100707A3`), it shows ProFont(user, **"What the... The door is jammed!"**, 2) and returns.
5. `if (pTrig->pev->iuser1 != 1)`: ProFont(user, **"Please wait until everyone is ready!"**, colour 2, one player) at `0x1004C1F7`, then returns.
6. Otherwise:
   - `EMIT_SOUND_DYN(this, CHAN_AUTO, noise, 1.0, 0.8, 0, 100)`.
   - `memset(store+4, 0, 0x10F38)` on the co-op store `[0x10222E10]`.
   - For **every** slot `1..maxClients` (not alive-filtered):
     - `b_GoingThroughADoor(+0x1EA4)=1`. This short-circuits TraceAttack `0x100F6950` and TakeDamage `0x100F603B`, so the player is invulnerable.
     - `UTIL_ScreenFade(pl, black, 0.5, 2.0, 255, FFADE_OUT|FFADE_STAYOUT)` at `0x1004C2CD`.
     - `EnableControl(FALSE)`.
     - `m_iHideHUD(+0x2500) |= 9`.
     - `pev->flags |= FL_NOTARGET(0x80)`.
     - Optional `PlayMP3(music, pev->iuser1, pev->fuser1)`.
     - `store->SavePlayer(pl)` (`0x1002A480`).
   - `SetThink(ChangeLevelThink)` and `SetNextThink(2.0)`.
7. **ChangeLevelThink** (`0x1004ACA0`) runs `CHANGE_LEVEL(STRING(pev->message), NULL)`, a plain changelevel with no landmark. Nothing guards a second Use; the engine latch dedups.

### 2c. `func_coopallplayersbutton` (factory `0x10028DA0`, 0x188 bytes, vtable `0x101AC158`)

- Spawn `0x10028EC0` makes a +USE brush (SOLID_BSP / MOVETYPE_PUSH), sets `m_flNextUse(+0x180)=0`, and sets the cooldown `+0x184 = 2.0` when it is 0. KeyValue cannot set the cooldown.
- **Use** (`0x10028F60`) requires `IsPlayer`, the master, and `time >= m_flNextUse`. Then:
  - `pTrig = FindEntityByTargetname(pev->message)`.
  - If `pTrig->pev->iuser1 == 1`: `FireTargets(pev->target, this, this, USE_TOGGLE, 0)`.
  - Otherwise: ProFont(user, **"Please wait until everyone is ready!"**, 2) at `0x10029031`.
  - Always `m_flNextUse = time + 2.0`. **CERTAIN**

### 2d. `cof_changelevel` (vtable `0x101C85E4`) and the co-op inventory store

- **cof_changelevel.** Spawn `0x1009C940` makes a point entity with EF_NODRAW. Use `0x100A1F70` runs `memset(store)`, then `store->SavePlayer(p)` for every slot, then `CHANGE_LEVEL(message, NULL)`. There is no waiting. **CERTAIN**
- **The store** is `[0x10222E10]`, 0x10F3C bytes, ctor `0x10029070`.
  - It is allocated in the worldspawn setup `0x10120EB0` whenever `IsMultiplayer()`. On lobby-flagged worlds (`worldspawn iuser1`, **LIKELY**) it is cleared instead.
  - It is global, so it survives changelevel inside the DLL.
  - `SavePlayer` `0x1002A480` fills one of 32 slots of 0x41C bytes each, keyed by netname.
  - **MP PlayerSpawn** (`0x100C9210`) calls the restore `0x10029090` at `0x100C93E0`. If no slot matches, it Uses every `game_player_equip`.
  - `trigger_changelevel`/ChangeLevelNow **never** saves to this store. **CERTAIN**

## 3. ScreenFade and ProFont

**Registration.** LinkUserMessages (`0x100E5FC0`, called from `0x100F05DB`) contains **`REG_USER_MSG("ScreenFade", 10)` → `gmsgFade [0x10226720]`** (`0x100E626E–E6287`) and **`REG_USER_MSG("ProFont", -1)` → `[0x10226568]`** (`0x100E63A6–E63BF`). **CERTAIN**

**UTIL_ScreenFade family** (HLSDK layout, **CERTAIN**):

- Build `0x10119CC0` computes `duration = FixedUnsigned16(fadeTime, 4096)` and `hold = FixedUnsigned16(holdTime, 4096)` via `0x101166D0` (`clamp(int(v*scale),0,0xFFFF)`). CoF keeps those two in the struct as floats.
- Write `0x10119D40` only sends when `IsNetClient`: `MESSAGE_BEGIN(MSG_ONE=1, gmsgFade, NULL, ed)`, then `WRITE_SHORT(duration)`, `WRITE_SHORT(hold)`, `WRITE_SHORT(flags)`, `WRITE_BYTE r,g,b,a`.
- `UTIL_ScreenFade(ent,color,t,hold,a,flags)` is `0x10119C00`. `UTIL_ScreenFadeAll` is `0x10119C50` (loops `1..maxClients`).
- Flags are FFADE_IN 0, OUT 1, MODULATE 2, STAYOUT 4.

Call site `0x1004C293–C2CD` (inter_door_coop_changelevel):

```
push 5 ; push 0xff ; sub esp,8 ; color=(0,0,0) ; [esp+4]=2.0f ; [esp]=0.5f ; push &color ; push pl ; call 0x10119C00
```

This puts the bytes `00 08 | 00 20 | 05 00 | 00 00 00 FF` on the wire: 0.5 s out, 2.0 s hold, OUT|STAYOUT, black. The matching fade-in at `0x1004AFF6` is `(0.5, 0, 255, FFADE_IN)`.

**ProFont.** `UTIL_ProFont(CBaseEntity *pl, const char *text, int colour, bool toAll)` is at `0x10119810`.

- If `toAll` is set it sends MSG_ALL. Otherwise, when `IsNetClient`, it sends `MSG_ONE_UNRELIABLE(8)`.
- The payload is `WRITE_BYTE(colour)` followed by `WRITE_STRING(text)`. **CERTAIN**

On the client (`client.dll` `0x100712A0`), READ_BYTE and READ_STRING fill a panel. If the text contains `DEADNOTIFY`, it is replaced by `"Press %s to respawn (lose weapons) or wait to be revived"`, where `%s` is the key bound to `+jump`. The panel is reset to alpha 255 and the colour index is stored at panel+0xBC.

The paint switch at `client!0x1003B8E6` (table `0x1003BB80`) maps the colour index to RGB. **CERTAIN** for the RGB; the `setFgColor(r,g,b,a)` argument order is **LIKELY**.

| index | RGB |
|---|---|
| 0 | yellow 255,255,0 |
| 1 | blue 0,128,255 |
| 2 | red 255,0,0 |
| 3 | yellow |
| 4 | green 0,255,0 |
| 5 | grey 200,200,200 |
| 6 | red |
| >6 | colour unchanged |

The server uses 2 for warnings and co-op messages, 5 for item and hint messages, and 4 for "Saved".

## 4. Co-op death, revive and game over (MP rules only)

- **Killed tail**: `0x100E9230`, player vtable slot 149. **LIKELY** that this is Killed, since it calls `rules->PlayerKilled` at `0x100E9784`.
  - It sets `SetThink(MultiplayerPlayerDeathThink /*0x100E9AB0*/)`.
  - Respawn-by-jump becomes allowed at `+0x3D4 = time+60`, and the first DEADNOTIFY is due at `+0x3D0 = time+60`.
  - Auto-respawn is scheduled at `+0x1F2C = time + sv_auto_respawn_time`. The cvar default is `"384"` (cvar_t `0x102147DC`).
  - It clears the revive handles at `+0x2698/+0x26A0`.
- **Team-wipe check** (same function, `0x100E9375–E9674`):
  - Survivors are participants (`fuser4==0`) that are alive or playing the `revived`/`crouch_revived` sequence.
  - With no survivors and a `cof_survivalmode` entity present, it fires that entity's target once.
  - Otherwise, for every player it sets `+0x4E8=1` (restart pending), `+0x1F24=time+10`, and `+0x4F0=10` (countdown). It also sends a title message `"Game over"` and PlayMP3 `game_over.mp3`.
- **PreThink**: `0x100EF2D0`, slot 147, MP branch at `0x100EF569`. The name is **LIKELY** (it calls both death thinks, at `0x100F0288`/`0x100F0295`); the behaviour below is **CERTAIN**.
  - A dead player without restart pending gets ProFont(`"DEADNOTIFYMESSAGE"`, 2) at `0x100EF5BA` when `time > +0x3D0`, repeating every 12 s.
  - Respawn happens when the player is dead and either `(time > +0x3D4 && IN_JUMP)` or `(+0x1F2C != 0 && time > +0x1F2C)`. It clears the inventory (`0x100DD990`, `0x100F0BA0`, …) and calls `respawn()` (`0x1001F0A0`), which is `pPlayer->Spawn()` in MP.
  - When restart is pending it shows `"Restarting the map in %i seconds\n"` (colour 5) for counts 5..0.
- **CPR revive** is the player Use at `0x100F88C0` (slot 58), with `this` as the downed player and the activator as the reviver. **CERTAIN**
  - Guards: no restart pending, the reviver is alive, and the downed player is dead and on the ground.
  - The **reviver is snapped to the corpse origin** (`0x100F8988–F899B`), yaw is set to corpse yaw + 90, and the CPR animations play.
  - Duration is `int(sv_coopcprtime)` (default `"5"`; 0 or less means 5). The reviver's `+0x26AC = time+dur`, and the two are linked by handles.
  - Completion happens in `MultiplayerPlayerDeathThink` (`0x100E9AB0`) when `pev->fuser3==3.0 && time>+0x4FC`. It sets DEAD_NO, MOVETYPE_WALK and **health 30**. Who writes `fuser3=3` is **UNKNOWN** (not traced).
- **Restart**: `MultiplayerPlayerDeathThink` at `0x100E9C55`. When restart is pending, the count is ≤0, `time>+0x1F24`, and `!+0x1F28`, it:
  - saves every player to the store;
  - calls `CHANGE_LEVEL(STRING(gpGlobals->mapname), NULL)`.

  This only runs from a *dead* player's think. The MP PlayerSpawn copies restart state to new joiners (`0x100C9446`).
- **`cof_coopgameover`** Use (`0x100A2320`) stops `cof_survivalmode` thinks. For each participant it applies the same restart state, `EnableControl(FALSE)`, FL_NOTARGET, "Game over" and game_over.mp3. If everyone is alive when it fires, the path that issues the restart is **UNKNOWN**.
- **`cof_coop_stats`** Use (`0x100A24B0`) sends the stats screen to all players. It then schedules `RestartCampaign` after 30 s (spawnflag 1) or 260 s.

## 5. Teleport helpers and "all players present" checks

- **Nothing places a player next to another player**, except the CPR snap in §4.
  - `respawn()` leads to Spawn, then the MP `GetPlayerSpawnSpot` (`0x100C8A20`, which also fires the spot's `target`), then `0x100AF610`, then **EntSelectSpawnPoint** `0x100DF180`. **CERTAIN**
  - Client slot N (1–4) takes the first `cof_coop_spawnN` with `iuser1==0`.
  - Otherwise, if IsCoOp (`gpGlobals->coop`), it takes `info_player_coop`, then `info_player_start`.
  - Otherwise, if IsDeathmatch, it takes a random `info_player_deathmatch` and **telefrags players within 128u (300 dmg)** (`0x100DF5EE`).
  - The fallback is the `startspot` targetname, then `info_player_start`.
  - The chosen origin gets `+1z`; angles come from the spot, with `fixangle=1` and zero velocity.
  - On SP maps (no coop/dm spots) every player therefore spawns on the same `info_player_start`.
- **Overlap tolerance.** MP PreThink (`0x100EF738–EF7B2`) checks whether the player's box intersects any other `player`. If it does, the player becomes `SOLID_NOT`; otherwise, if alive, it is set back to `SOLID_SLIDEBOX`. Stacked or warped players therefore stay unstuck. **CERTAIN**
- **Point-warp recipes:**
  - `CInterDoorCoop::EndSequence` (`0x1004AD10`) is the cleanest. It performs the following steps:
    - `org = target.origin`; for players, `org.z = org.z - pl.mins.z + 1`, then `UTIL_SetOrigin`;
    - copies `angles` and `v_angle` from the target and sets `fixangle=1`;
    - zeroes velocity and basevelocity and clears FL_ONGROUND;
    - fades in over 0.5 s and calls `EnableControl(TRUE)`;
    - clears HIDEHUD 9 and FL_NOTARGET, and sets `b_GoingThroughADoor=0`.
  - `cof_entteleport` Use `0x100A6B80` is a bare `UTIL_SetOrigin(FindByTargetname(target), self.origin)`.
  - `cof_teleport` Use is `0x100A7990` (see the earlier doc).
- **Reusable counters and checks:**
  - `0x1010A400`: alive participants.
  - `0x1010A470`: all participants with `fuser4==0`.
  - `InsideThink` (§2a).
  - `trigger_playercheckarea` Use `0x10115480`. It box-counts players (alive-only if `iuser1`), then either fires `target` when count==total, or picks a per-count target.
  - `cof_playercheck` Use `0x100A4B60` fires one of four targets based on the player count.

## Implications for an engine-side exit hold and warp

1. **Hold at the touch, not at `pfnChangeLevel`.** By the time the engine sees `CHANGE_LEVEL`, the DLL has already toggled `target`, run `killtarget`, and possibly spawned a CFireAndDie. Refusing or deferring the call makes all of that repeat on every usercmd touch.
   - To hold, record the trigger edict whose `DispatchTouch` is active when `pfnChangeLevel` fires, and stop dispatching touches to it (or move players out).
   - Keep exactly one owned copy of `(map, landmark)` and do not rely on a re-call.
2. **Mirror CoF's own semantics.** Ready means players inside the volume ≥ `IsAlive && fuser4==0` participants, with no timeout. Evaluate all clients, not client 1, which ChangeLevelNow and InTransitionVolume hard-code.
3. **Reuse the retail UI:**
   - Send ProFont colour 2 `"Please wait until everyone is ready!"` via MSG_ONE_UNRELIABLE, throttled to about 2 s as the button cooldown does.
   - The `DEADNOTIFY` substitution is client-side, so avoid that substring in custom text.
4. **Reuse the retail departure sequence (inter_door_coop_changelevel).** Fade black `(0.5, 2.0, 255, 5)`, set FL_FROZEN (EnableControl FALSE), FL_NOTARGET and HIDEHUD 9, then `CHANGE_LEVEL(map, NULL)` 2 s later.
   - These can all be done from the engine: the ScreenFade id is from RegUserMsg and the flags live in the edict.
   - Invulnerability needs private data `b_GoingThroughADoor` at +0x1EA4, which is riskier.
5. **Inventory carry is available for free in MP.** Before issuing the change, call `store->SavePlayer(pl)` for each player (thiscall `0x1002A480`, `ecx=[0x10222E10]`), after the `memset(store+4,0,0x10F38)` as the retail code does. PlayerSpawn will then restore by netname. The retail `trigger_changelevel` path does not do this.
6. **Landmark and `changetarget` are lost with plain changelevel.** Spawns go to `cof_coop_spawnN` or `info_player_start`. If a landmark-relative arrival is wanted, the engine must warp after spawn using the EndSequence recipe (`z -= mins.z; z += 1`, `fixangle=1`, zero velocity).
   - With `deathmatch=1, coop=0`, spawn selection takes the deathmatch branch, which telefrags. Make sure SP maps have no `info_player_deathmatch`, or set `coop 1`.
7. **Do not block other retail exits.** These also call `CHANGE_LEVEL(...,NULL)` and must pass through an engine hold:
   - the team-wipe restart (`map == gpGlobals->mapname`, `0x100E9CF9`);
   - `RestartCampaign`;
   - `cof_changelevel`;
   - `inter_door_coop_changelevel`.

   Distinguish them by caller, or apply the hold only to requests that come from a `trigger_changelevel` touch.
8. **Warping onto a teammate is survivable** because of the MP PreThink overlap rule (SOLID_NOT while intersecting). Still, prefer an offset with a hull trace so players do not stay stacked.
