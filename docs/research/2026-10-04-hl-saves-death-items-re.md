# hl.dll saving, death, revive and item state (static RE), 2026-10-04

> **Corrections (2026-10-18, decompilation survey, `docs/research/2026-10-18-cof-decompilation-survey.md` section 6):**
> - `0x100F2A50(1)` (line 217) is `SetAnimation(PLAYER_WALK)`, not a weapon redeploy.
> - `P+0x501` (the engine's `COF_RETAIL_GIBBED`) is `m_bCantBeRevived`, the right byte to test; the real `m_bGibbed` is `P+0x1F29`.
> - Addresses: `0x10223190` is `g_pWorld` (CWorld; `+0xE0..+0xEC` its `player1model..player4model`); `0x1005FC20` is `CPhoneCall::ChangeSubtitle`; `0x10226480` is `gSpecialItemGive`; LIKELY `0x10222F14` `g_iSurvivalMode`, `0x10222F20` `m_iSimonMode`, `0x10222E10` `g_pLevelChange`.
> - Every co-op tape save used to overwrite that machine's single-player slot label `saveinfoN.cof`; the engine now stops it in multiplayer (`cof_coop_sp_save_labels`, `engine/client/cof_coop_hud.c`).

Binary: `runtimes/enhanced/cryoffear/cl_dlls/hl.dll` (stock, SHA-256 `0036B91C…0E63`), image base `0x10000000`. Cross-checked against `client.dll` in the same folder, the project's engine source (`upstream/cof-enhanced/xash3d-fwgs-…`) and the entity lumps of all 235 retail BSPs. The pass was static only: a capstone linear sweep, the DLL's exported symbol names (846 exports, including `?PreSave@CBasePlayer@@…` style think/use names), RTTI vtables, and every `CSave::WriteFields`/`CRestore::ReadFields` call site to recover the save tables. Nothing was launched or modified.

Tags: **CERTAIN** = read from the machine code (or the engine source). **LIKELY** = the code was read, but a name, meaning or runtime consequence is inferred. **UNKNOWN** = not established.

Builds on `2026-09-30-hl-exits-coop-re.md` (§4), `2026-09-30-hl-inventory-re.md` and `2026-09-30-hl-documents-story-state-re.md`. Corrections to those notes are collected in §0.

## 0. Layout facts and corrections

- **Entity vtable.** Spawn #11 (+0x2C), Precache #12, KeyValue #13, **Save #14 (+0x38), Restore #15 (+0x3C)**, ObjectCaps #16, Killed #27 (+0x6C), IsAlive #47 (+0xBC), IsPlayer #52, Use #58 (+0xE8). Save is identified by its call to `CSave::WriteFields` (`0x1011ADC0`, 5 args: cname, pname, base, table, count), Restore by `CRestore::ReadFields` (`0x10117010`, 4 args). **CERTAIN**
- **Player vtable `0x101A4DA8`** (152 slots): Spawn `0x100F4090`, Save `0x100F1940`, Restore `0x100F14C0`, Killed `0x100E59F0`, Use (CPR) `0x100F88C0`, DeathSound #139 `0x100DDC60`, PreThink #147 `0x100EF2D0`, per-frame #148 `0x100EAD70`, **MP Killed #149 `0x100E9230`**, UpdateClientData #150 `0x100F6CA0`. **CERTAIN** for the addresses; names LIKELY.
- **Weapon vtable** (`CBasePlayerWeapon` `0x101A7804`): #81 (+0x144) = Deploy (base returns 1), #83 (+0x14C) = Holster (`0x1011D410`: clears view/weapon model, sleeve sound), #87 (+0x15C) = Drop. **LIKELY** (HLSDK parity plus bodies).
- **entvars offsets used** (from `gEntvarsDescription` `0x1021AAE0`): movetype +0x108, solid +0x10C, effects +0x118, sequence +0x12C, frame +0x134, framerate +0x13C, health +0x164, weapons +0x16C, takedamage +0x170, deadflag +0x174, button +0x184, flags +0x1A8, waterlevel +0x1C4, netname +0x1D4, fuser3 +0x260, fuser4 +0x264, vuser2 +0x274. **CERTAIN**
- **Rules vtable** (`CHalfLifeMultiplay` `0x101D608C`): +0x20 IsMultiplayer, +0x3C ClientDisconnected (`0x100C76D0`), +0x50 PlayerSpawn (`0x100C9210`), +0x60 GetPlayerSpawnSpot, +0x68 ClientCommand, +0x74 PlayerKilled. **CERTAIN** for the bodies; names LIKELY.
- **DLL_FUNCTIONS** (`GetEntityAPI2` copies the table at `0x102119F8`): ClientDisconnect `0x1001CC90`, ClientCommand `0x10018D60`, PlayerPreThink `0x1001E7B0`, Save dispatch `0x10016AB0`, Restore dispatch `0x10016C80`. **CERTAIN**
- **Helpers named here:** StartRevivedAnim `0x100F1860`, GiveNamedItem `0x100E0A80`, HasNamedPlayerItem `0x100E2380`, SelectItem `0x100F1A50`, SendVGUIMenu `0x100F4050`, ThirdPerson on `0x100F68A0` / off `0x100F67F0`, ViewLock `0x100F9140`, DualWield clear `0x100F2E10`, StopMP3 `0x100EA600`, LookupSequence `0x1000E9B0`, ResetSequenceInfo `0x1000E9E0`, StudioFrameAdvance `0x1000EDA0`, SequenceDuration `0x1000F390`.
- **The launcher runs MP rules with `coop 0`, `deathmatch 1`** (`cofcoop/core.py:290`). Everything below that depends on these globals assumes that.

**Corrections to the earlier notes**

1. Exits note: the `m_iPlayerSaveLock = 0` write at `0x100F1974` is in **CBasePlayer::Save**, not Restore. Save writes the 207 fields first, so a transition save carries the 1337 value that ChangeLevelNow set; it is then zeroed in memory. **CERTAIN**
2. Exits note §4: the writer of `fuser3 = 3` is **StartRevivedAnim `0x100F1860`** (§3). **CERTAIN**
3. Exits note §4: when `cof_coopgameover` fires with everyone alive, the DLL issues **no** restart. The only `CHANGE_LEVEL(mapname)` is in MultiplayerPlayerDeathThink, and that runs only for dead players (§4). **CERTAIN** (xref-based)
4. Documents note §2: the padlock digits `P+0x235C..0x2368` **are saved**, because `m_iPadlockNumber [5]` (INTEGER, `+0x2358`) covers them. Spawn re-rolls them only when one of them is 0 (`0x100F45C7`). **CERTAIN**
5. Inventory note §7: `SavePlayer` does **not** key by netname. It appends to the first free entry; only the loader matches, by netname **pointer** (§6). **CERTAIN**

## 1. How single-player saves happen

### 1a. The tape recorder: `tape_recorder` = `CTapeRecorder`

- Factory `0x1007CB90`, Precache `0x1007C730` (model `models/tape_recorder.mdl`, `save/save_no|yes|sequence.wav`), glint `BlinkThink` `0x1007C440`.
- Save table `0x10212F10` (4 fields): `m_useTime` +0x104 (next use), `m_flGlintTime` +0x100, `m_bTapeReminder` +0x10C, `custommessage` +0x108 (the map key `custommsg`, LIKELY). **CERTAIN**
- **`TapeUse` `0x1007C930`** (`m_pfnUse`) runs these steps. **CERTAIN**
  1. Guards: activator non-null, `time >= m_useTime`, `activator->IsPlayer()`.
  2. **First use per player.** If the player's `m_bHasSaved` (`P+0x21D8`, saved) is 0, it shows ProFont(colour 5) with the intro text, sets `m_bHasSaved = 1`, and returns. The text is the "Huh? What's this doing here?…" line when spawnflag 1 is set, otherwise `custommessage` or "A dusty tape recorder…". The next use is allowed 1 s later.
  3. **`difficulty == 4` only (tape mode).**
     - It needs `m_iTapes` (`P+0x1EF4`) != 0 **and** `HasInventoryItem("inventoryitems/cassettetape.txt", FALSE)`. Otherwise it shows "I can't use this without a tape." (colour 2).
     - The first such use only prints "Looks like there's enough room for %i more saves." (or "…only enough room for 1 more save...") and sets the recorder's `m_bTapeReminder`. The second use proceeds.
     - Any other difficulty skips straight to step 4.
  4. It sends **`SaveLoad`** (`[0x102265D4]`, MSG_ONE to the activator) with `byte 0` (save mode) and `string` = the recorder's `message` (area name) or "Unknown area".
  5. `UTIL_ScreenFade(player, black, 0.1, 10.0, 255, OUT|STAYOUT)`, `m_bTapeReminder = 0`, `m_bHasSaved = 1`, next use at +2 s.
  - Nothing in TapeUse saves. **There is no IsMultiplayer/deathmatch test anywhere in TapeUse.** **CERTAIN**
- **The client finishes the job** (client.dll). **CERTAIN** for the calls; LIKELY for the file write.
  - The SaveLoad hook is `0x10032320`; the menu handler is `0x100314A0`.
  - When slot *n* is chosen in save mode, it calls `pfnServerCmd("unfreezesave 1024 n")` (`0x1003176A`). It then writes `saveinfo<n>.cof` locally (`0x10032340`, LIKELY) and arms a timer at `now + 3.25 s` (`0x1018A3DC/E0/E4`).
  - Each frame `0x10072740` checks the timer. When it expires it sends **`pfnServerCmd("savehack cofsave%i\n")`** (`0x1007279B`).
  - In load mode the client sends `unfreeze 1024` then `cofload n`. Cancel sends `unfreeze 666`. The client's own `Save` dialog (`0x1004A5E0`) sends `cofsave 19573`. Neither hl.dll nor the project engine handles that command; its purpose is UNKNOWN.
- **Server side of the client commands** (hl.dll ClientCommand `0x10018D60`). **CERTAIN**
  - `unfreezesave 1024 <n>` (`0x1001977A`) calls **`CBasePlayer::PreSave(n)` `0x100EF1D0`**, with no MP gate:
    - IsAlive gate.
    - `n` is stored to `[0x10217E38]`, which nothing reads.
    - `VGUIMenu(200,0)`.
    - `m_iHideHUD |= 9`, `FL_NOTARGET`, `save_sequence.wav`, velocity zero, `EnableControl(FALSE)`.
    - ProFont "Saving progress..." (colour 2).
    - **On `difficulty == 4`: `--m_iTapes`, and at 0 `HasInventoryItem(cassette, TRUE)` removes the tape item** (`0x100EF28F–0x100EF2A1`).
    - `SetThink(EndSave)` at +3 s.
  - **`EndSave` `0x100DF100`** undoes the HUD, NOTARGET and freeze, increments the stat dword `P+0x650` (playerstats; the client labels it "Times saved"), shows ProFont **"Saved"** (colour 4) and fades in over 1 s.
  - `cofload <n>` (`0x1001C7F7`) runs `SERVER_COMMAND("load cofsave<n>\n")` **only if `!IsMultiplayer()`**.
  - `loadgamemenu` (`0x1001BBE3`) sends `SaveLoad(1,"nothing")` **only if `!IsMultiplayer()`**.
  - `unfreeze 1024|512` calls `EnableControl(TRUE)`.
  - **`savehack` is not a hl.dll command.** An unknown command goes to `rules->ClientCommand` (+0x68) and then prints "Unknown command: %s" to that client (`0x1001CC27`).
- **Who performs the actual save.** The retail engine `hw.dll` contains the string `savehack`, so LIKELY it handled the command itself. The project engine handles it in `engine/client/dll_int/cl_game.c` `pfnServerCmd`: it rewrites `savehack cofsave<N>` to the local console command `save cofsave<N>` **only when `cof_save_root_compat && SV_Active() && CL_Active() && cl.maxclients == 1`**. Otherwise it forwards it as a stringcmd. **CERTAIN** (engine source)
- **Net effect in co-op.** For host or guest, the tape recorder opens the menu, freezes the user for 3 s, prints "Saving progress..." then "Saved", and on difficulty 4 spends a tape. **No save is written.** `savehack` reaches hl.dll and is rejected as "Unknown command". **LIKELY** (each step is CERTAIN; the chain was not run)
- **Tapes.**
  - `m_iTapes` is set to **5** when an `item_key` with spawnflag 1 is picked up on `difficulty == 4` (KeyitemUse `0x1004DFE4`).
  - It is decremented only by PreSave.
  - `cof_clearitems` re-adds the tape item for `bagremove` when `m_iTapes != 0` (`0x100A66E9`).
  - It is in the player save table. `difficulty` defaults to `"2"` (cvar `0x10214750`); 4 is the tape mode, LIKELY "Nightmare". **CERTAIN** apart from the name.

### 1b. Automatic and scripted saves

- **No DLL autosave.** `CTriggerSave::SaveTouch` (`trigger_autosave`, `0x10110270`) is a stub: it calls `UTIL_IsMasterTriggered(m_sMaster, pOther)` and returns. It issues no `SERVER_COMMAND`. No `"autosave"` command string is referenced anywhere. **CERTAIN**
- **Every `SERVER_COMMAND` (`[0x10222FE4]`) call site** is listed below. None saves. **CERTAIN**
  - `load cofsave1..5` (ClientCommand, SP only)
  - `reload\n` in `respawn()` `0x1001F0A0`, only when `coop==0 && deathmatch==0`
  - `reload\n` in `CRevertSaved::LoadThink` `0x100E6AF0`, only when `deathmatch==0`
  - `map c_difficulty_settings` / `map c_video_stress_test`
  - `exec skill.cfg` / `exec game.cfg` / `exec <listenserver cfg>`
  - `kick "%s"`
  - the mapcycle command (`0x100C7252`)
  - `trigger_changecvar` (`%s "%s"`, `0x1011465D` and restore `0x10112906`)
  - **`trigger_command`** (`CTriggerCommand` Use `0x101147E0`: `SERVER_COMMAND(netname + "\n")`)
- **Map scan (all 235 BSPs).** **CERTAIN**
  - There are **0** `trigger_autosave`, **0** `player_loadsaved` and **0** `point_servercommand` entities.
  - `trigger_command` appears only on `c_outofit0/00/8/9/11` (custom "Out of it" campaign). It runs `map c_outofitN`, a full new-game `map`, which would also restart a co-op server.
  - Save-related entities are therefore only `tape_recorder`: **145** in all maps.
  - **The main campaign (81 maps reachable from `c_intro` through `trigger_changelevel`) has 69 recorders on 53 maps:**
    - 2 each: `c_apartment2`, `c_apartment4`, `c_asylum1`, `c_asylum1day`, `c_avlopp`, `c_basement`, `c_city`, `c_ending1c`, `c_ending2c`, `c_ending3c`, `c_ending5`, `c_ending5c`, `c_fsewer1`, `c_sewer3`, `c_start`, `c_subtunnel1`
    - 1 each: `c_apartment1`, `c_apartment4sick`, `c_apartment5`, `c_apartmentsick`, `c_asylum1dark`, `c_bridge`, `c_buscity`, `c_city2`, `c_down`, `c_ending1`, `c_ending1b`, `c_ending2`, `c_ending2b`, `c_ending3`, `c_ending3b`, `c_ending4`, `c_forest3`, `c_forest4`, `c_forest5`, `c_hamn`, `c_hamn3`, `c_park`, `c_park3`, `c_roofs`, `c_roofs2`, `c_sewer`, `c_sewer2`, `c_stad`, `c_subtunnel2`, `c_subway1st1`, `c_subway2st1`, `c_subway2st2`, `c_subway2st3`, `c_subwaysick1`, `c_subwaysick2`, `c_trainride`, `c_village1`
    - 0: the other 28 campaign maps (e.g. `c_intro`, `c_forest1/2/6/7`, `c_college*`, `c_asylum2*`, `c_park2`, `c_village2/3`, `c_lake`)
  - The remaining 76 recorders are on custom/Halloween/Doctor `c_*` maps. The `c_cof_*` co-op maps have none.
- **The SP game-over path loads saves through the client, not the DLL.**
  - SP `Killed` sets `+0x1F24 = time+2`.
  - `PlayerDeathThink` `0x100EA310` then, when `!IsMultiplayer`, sends `CustomDoc` and **`VGUIMenu(35)`** (the death menu, `0x100EA3E7`). The menu leads to `cofload`. **LIKELY** for the menu's role.

### 1c. Does the DLL refuse or alter saving in MP?

- **It never refuses.** The Save/Restore dispatchers (`0x10016AB0`/`0x10016C80`), `CBasePlayer::Save/Restore` and the weapon Save/Restore have no `g_pGameRules`, coop or deathmatch reference. **CERTAIN**
- The only MP differences are:
  - `cofload`/`loadgamemenu` are SP-only.
  - `CRevertSaved` and `respawn()` issue `reload` only in SP.
  - Restore sets the `low` physinfo key only in SP (`0x100F16C5`).
- **`CBasePlayer::Restore` `0x100F14C0` behaviour on load:**
  1. It runs `ReadFields("PLAYER", 207 fields)`.
  2. If the save has no landmark (`[gpGlobals+0x9C]+0x52C == 0`) it logs "No Landmark:%s". It then **re-places the player at `EntSelectSpawnPoint` + 1 z and copies the spot's angles** (`0x100F1505–0x100F15B1`). This is HLSDK behaviour, so a plain `load` puts every restored player on a spawn spot unless the engine fixes it.
  3. It resets the view angle, sets `fixangle = 1`, the hull (crouch-aware) and the physinfo keys, restarts the saved MP3, and sets the "just restored" byte `[0x10226476]`. PostThink uses that byte to clear `m_iPlayerSaveLock` and redo the stair/ladder state.

  **CERTAIN**

## 2. Death → respawn in multiplayer

**Killed dispatch.** `CBasePlayer::Killed` `0x100E59F0` (slot 27) does `if (g_pGameRules->IsMultiplayer()) { this->MPKilled(attacker, gib) /* slot 149, 0x100E9230 */; return; }` (`0x100E5A0C`). **CERTAIN**

**MP Killed `0x100E9230`, in order.** **CERTAIN**

1. `ViewLock(0)`. `P+0x3A4 = (int)angles.y` (corpse yaw, 1 if 0).
2. **`SetThink(MultiplayerPlayerDeathThink)`, next think +0.1** (`0x100E9269`).
3. **`+0x3D4 = time + 60`** (jump-respawn allowed), **`+0x3D0 = time + 60`** (first DEADNOTIFY).
4. **`+0x1F2C = time + sv_auto_respawn_time`** (default `"384"`).
5. **`+0x2698 = +0x26A0 = NULL`** (CPR links), `+0x26AC = time`. If `FL_FROZEN`, `EnableControl(TRUE)`.
6. Team-wipe check (§4).
7. Common death:
   - ThirdPerson death cam `0x100F68A0` (writes the reviver index or 0xFF).
   - Clears the active item's think. View punch. Deathbits (water ≥3 → `|=0x27`; MP → `|=6`). `b_GoingThroughADoor = 0`. Flashlight flags 0.
   - **`m_pActiveItem->Holster()`** (`0x100E976C`), then `rules->PlayerKilled` (`0x100E9784`). Releases tank and spec tank.
   - Death sequence (`death`/`crouch_death`/`taller_lie`), `+0x4F4 = time + duration`. Velocity 0. **`deadflag = DEAD_DYING`** (`0x100E98D3`). **movetype TOSS**.
   - DualWield cleared, dual ammo −2. `Health(0, entindex, 1)` to **all** players. `CurWeapon(0,0xFF,0xFF)`. FOV 0. DeathSound. `angles.x/z = 0`.
8. **Nothing touches inventory slots, the weapon list, ammo, quick slots, tapes, three-slot mode or documents.** Every callee was inspected.

**PreThink (slot 147 `0x100EF2D0`), MP branch `0x100EF569–0x100EF7B2`.** **CERTAIN**

```
if (rules && rules->IsMultiplayer()) {
  if (!IsAlive() && !restartPending(+0x4E8) && time > +0x3D0)
      { ProFont(this,"DEADNOTIFYMESSAGE",2); +0x3D0 = time + 12; }            // 0x100EF5BA
  if ( (!IsAlive() && time > +0x3D4 && (pev->button & IN_JUMP)) ||          // no +0x4E8 test on this path
       (!IsAlive() && !+0x4E8 && +0x1F2C != 0 && time > +0x1F2C) ) {
      pev->button = 0;  +0x2654 = 0;
      ClearInventorySlot(1, TRUE);              // 0x100EF671  all slots (1..3 in three-slot mode)
      RemoveAllItems(FALSE);                    // 0x100EF67A
      DualWield clear; m_iDualWeaponLeft/RightAmmo (+0x5DC/+0x5E0) = -2;
      m_sQuickSel1..3[0] = 0;  for q=1..3: QSlotClear(q,0), m_iQuickSlotSlot[q] (+0x3DC..) = 0;
      ThirdPerson off;
      respawn(pev, !(m_afPhysicsFlags & 0x20)); // 0x100EF71F -> Spawn() (vtable +0x2C) because deathmatch=1
      0x10017AD0(this); +0x1F2C = 0;
  }
  player-overlap rule (SOLID_NOT while intersecting a player, else SOLID_SLIDEBOX if alive)
}
...
if (+0x4E8 && time > +0x4EC) { if (0 <= +0x4F0 <= 5) ProFont("Restarting the map in %i seconds",5); --+0x4F0; +0x4EC = time+1; }  // 0x100EF800
...
if (deadflag >= DEAD_DYING) rules->IsMultiplayer() ? MultiplayerPlayerDeathThink() : PlayerDeathThink();  // 0x100F0268..0x100F0295
```

- **Respawn gates.** It needs `deadflag != 0 || health <= 0` (IsAlive false), plus one of two conditions:
  - **jump after +0x3D4** (death + 60 s), even with a restart pending;
  - **auto at +0x1F2C** (death + 384 s), only when `+0x4E8 == 0`.
- `+0x1F24` (restart time) and `+0x4F0` (countdown) do **not** gate respawn; they belong to the restart (§4).
- **RemoveAllItems `0x100F0BA0`** (arg = remove suit):
  - Holsters the active item. Clears `m_pActiveItem`/`m_pLastItem`.
  - For every item in the 6 buckets, clears any quick slot whose `m_sQuickSel` names that classname (QSlotClear). It then calls `Drop` (+0x15C), which destroys the item, and nulls the buckets.
  - Clears viewmodel/weaponmodel and `pev->weapons &= WEAPON_SUIT`.
  - **Zeroes all 32 `m_rgAmmo` and sends `AmmoX(i,0)`.** Calls UpdateClientData and sends `CurWeapon(0,0,0)`.

  **CERTAIN**

**`respawn()` `0x1001F0A0`.** It does `if (coop==0 && deathmatch==0) SERVER_COMMAND("reload\n"); else pPlayer->Spawn();`. The corpse-copy argument is ignored. **CERTAIN**

**`Spawn` `0x100F4090` resets** the following. **CERTAIN**

- classname, **health 100**, armor 0, takedamage AIM, SOLID_SLIDEBOX, MOVETYPE_WALK, max_health, flags (keeps FL_PROXY, adds FL_CLIENT), effects 0, **deadflag 0**, gravity/friction 1.
- MP3 state and rain fields.
- **`ClearInventorySlot(1, TRUE)`** (`0x100F4294`).
- `+0x2698/+0x26A0 = NULL`, `+0x26AC = time`, **`+0x1F2C = 0`**, `+0x3D0 = time`.
- In MP, `+0x3C4 = 50·slot`, re-randomised to 0..350 when ≥ 360 (purpose UNKNOWN).
- **`+0x4E8/+0x4EC/+0x4F0 = 0`** (restart state), **`fuser4 = 0`** (participant), `+0x501 = 0`, `+0x4F4/+0x504 = time`.
- **`m_iGoodPoints (+0x5F0) = 2`**, `m_bBossHealthBar = 0`, **`m_sCustomObjectives (+0x420) = 0`**, objective cache `+0x1E8C = 0` (forces an Objectives resend), `m_pPadlock = NULL`, computer state `+0x1EA0 = 0`.
- `m_bUsedFirstSyringe`/trip timers 0, `m_iViewEntIndex = 0`, **`m_fStamina = 100`** (Stamina message), FOV 0, flashlight battery 99.
- Model and hull, view_ofs (0,0,28), **`m_fInitHUD = 1`** (full HUD and inventory resend), `m_pLastItem = 0`, `+0x2530 = 0`.
- **all 32 `m_rgAmmo` and `m_rgAmmoLast` = 0** (`0x100F49B0`).
- Finally **`rules->PlayerSpawn(this)`** (`0x100F49E8`), which runs the co-op store restore (§6).
- It sends ProFont(" ", 2, **toAll**), which blanks every client's ProFont panel.

**Spawn does not touch** `m_rgpPlayerItems` (the respawn path already removed them), `m_iTapes`, `m_bThreeSlots`, `documentsstruct`, keypad codes, `m_sObjectiveText`, ending flags, `m_bHasSaved`, `m_iOwnedWeaponsList` or `m_iDoctorWeapon`. **CERTAIN**

## 3. The CPR revive

**fuser3 is a per-player animation state.** 1 = fall freeze (SP Killed), **2 = reviver doing CPR**, **3 = being revived**, **4 = reviver getting up**, 5 = phone call. **Only `StartRevivedAnim` `0x100F1860` writes 3** (`0x100F18FD`). **CERTAIN**

**Start: player Use `0x100F88C0`** (`this` = downed player, activator = reviver). **CERTAIN**

- **Guards:**
  - `this+0x501 == 0` (a "gibbed" flag, set with `+0x1F29` at `0x10089D9B`; LIKELY).
  - `this->+0x26A0` (its reviver) not valid.
  - **`this->+0x4E8 == 0`** (no restart pending).
  - Activator `IsPlayer` and `IsAlive`.
  - `this` **not** alive.
  - The reviver's `+0x2698` (its target) not valid.
  - **`this` has `FL_ONGROUND`.**
- **Effects:**
  - Reviver velocity 0. **Reviver origin = corpse origin.** Reviver yaw = corpse yaw + 90, pitch 0, `fixangle = 1`. Reviver plays `cpr_down`/`crouch_cpr_down` (crouch chosen by the corpse's `FL_DUCKING`).
  - Corpse plays `taller_lie`/`crouch_taller_lie`.
  - `dur = (int)sv_coopcprtime` (default `"5"`; ≤0 means 5).
  - **`reviver+0x2698 = this`**, **`reviver+0x26AC = time + dur`**, **`this+0x26A0 = reviver`**. Reviver ThirdPerson on.

**Reviver side, in the reviver's PreThink** (`0x100EFCB4–0x100F01E6`, `edi = &reviver->+0x2698`). **CERTAIN**

- While linked, it keeps `fuser3 = 2` and loops `cpr_idle`.
- **Releasing +USE** cancels: it clears the target's `+0x26A0` and its own `+0x2698`, `+0x26AC = time`, plays `cpr_up`, sets `fuser3 = 4`.
- If the target is **alive**, or the target's `+0x26A0` is not this reviver, it cancels the same way **but leaves the target's `+0x26A0` set**. Killed or Spawn clears it later.
- **Completion trigger:** `+0x2698` valid, `button & IN_USE`, and `time > +0x26AC`. It then runs:
  1. `target->StartRevivedAnim()` (`0x100F010B`)
  2. `target+0x26A0 = NULL`, `+0x2698 = NULL`, `+0x26AC = time`
  3. `cpr_up`, `fuser3 = 4`

  When the get-up finishes (`fuser3 == 4 && time > +0x4FC && unlinked`) it runs ThirdPerson off, `fuser3 = 0` and weapon redeploy (`0x100F2A50(1)`).
- **Second caller:** `CSyringe::BladeRetract` `0x1007A5CF`. In MP, a syringe used on a dead target calls `StartRevivedAnim(target)` and spends one syringe unless ammo is infinite. **CERTAIN**

**`StartRevivedAnim` `0x100F1860`** (thiscall, no args, on the downed player). **CERTAIN**

- `+0x50C = 0`, `+0x2654 = 0` (dying-anim frame counter), ResetSequenceInfo, sequence `revived`/`crouch_revived`, frame 0.
- **`+0x4F4 = time + SequenceDuration`**, **`fuser3 = 3.0`**, **`+0x4FC = +0x4F4`**.

**Completion: `MultiplayerPlayerDeathThink` `0x100E9AB0`, first block (`0x100E9AC1–0x100E9C54`).** **CERTAIN**

```
if (pev->fuser3 == 3.0f && time > +0x4FC) {
    deadflag = DEAD_NO;  movetype = MOVETYPE_WALK;  flags &= ~FL_ONGROUND;  health = 30;
    for every player: Health(byte 30, entindex, 1);
    if (m_pActiveItem) { m_pActiveItem->Deploy(); CurWeapon(1, m_iId, m_iClip) to self; }
    ThirdPerson off;  fuser3 = 0;  ProFont(this, " ", 4)   // blanks the DEADNOTIFY panel
    return;
}
```

- While `fuser3 == 3 && +0x4FC > time && deadflag == DEAD_DEAD`, it only advances the `revived` animation (`0x100E9D10`).
- **Nothing in the revive touches the inventory, weapon list, ammo, quick slots or tapes.** Death only holstered the active weapon, and Deploy brings it back. **CERTAIN**
- **Not restored by the completion:**
  - `solid`, which never changed and is re-set to SLIDEBOX by the MP overlap rule.
  - `effects` keeps `EF_NOINTERP` from DEAD_DEAD (cosmetic).
  - `framerate` (0); a later SetAnimation is LIKELY to fix it.
  - `+0x3D0/+0x3D4/+0x1F2C`, which are harmless while alive.
  - `+0x4E8`: a pending restart stays pending.
  - Health is a fixed 30.

  **CERTAIN** for the list.

**Can the engine trigger it? Yes; the completion reads only `fuser3` and `+0x4FC`.**

- What must be true:
  - The downed player must still be dead (`deadflag ≥ 1`).
  - `IsMultiplayer()`.
  - **Its own client must be running PreThink.** MultiplayerPlayerDeathThink is called from PreThink each usercmd (`0x100F0288`). The think pointer set in Killed fires only once, because the function never re-arms `nextthink`. A disconnected or still-loading player is never revived. **CERTAIN** for the call sites.
- It does **not** need: on-ground, no restart pending, or any CPR links.
- **Preferred method:** call `StartRevivedAnim` (`ecx` = the downed `CBasePlayer*`). This is exactly what CPR and the syringe do: the revived animation plays and the counters reset. Completion follows after the animation length.
- Writing `fuser3 = 3` and `+0x4FC = time` directly also completes the revive on the next PreThink. It skips the animation and leaves `+0x2654` high, so the *next* death skips the dying-animation wait (cosmetic).
- If a real CPR is in progress, also clear `downed+0x26A0` and the reviver's `+0x2698`, and set the reviver to `fuser3 = 4`, as `0x100F0113–0x100F01B0` does. Otherwise the reviver cancels on its own next frame (target alive). That path leaves `downed+0x26A0` stale until the next death, which blocks a second CPR on that player.
- If a team wipe is pending, clear `+0x4E8` on everyone (§4). Otherwise the countdown continues and the restart still fires from another dead player.

## 4. The team wipe

**Decision, inside MP Killed (`0x100E9375–0x100E9674`), at the moment of a death.** **CERTAIN**

- **survivor** = any `UTIL_PlayerByIndex(1..maxClients)` with **`fuser4 == 0`** that is `IsAlive()` **or** whose `sequence` equals `revived`/`crouch_revived` (i.e. mid-revive).
- The dying player is already `health ≤ 0`, so it never counts.
- A player lying in CPR (`taller_lie`) does not count.

**If there is no survivor:**

- **With a `cof_survivalmode` entity** (survival maps only): it sends `Timer` zeros and StopMP3 to all players, and fires the entity's `target` once (`iuser3` latch).
- **Otherwise, for every player slot** (not alive-filtered, no `fuser4` filter):
  - `+0x1F28 = 0`, **`+0x4E8 = 1`**, **`+0x1F24 = time + 10`**, `+0x4EC = time`, **`+0x4F0 = 10`**
  - `GreyFade(0,2)` (`[0x1022667C]`)
  - `Chapter("Game over", NULL)` (`[0x10226658]`)
  - `PlayMP3(1,1,0,"game_over.mp3")` (`[0x10226544]`)

**Other writers of the same state:**

- **MP `ClientDisconnected` `0x100C76D0`.** ClientDisconnect has already set the leaver's `fuser4 = 1`. If no alive participant remains, it does the same as above for every player, but with a **5 s** countdown and `ProFont("Game over",4)`. It then **`RemoveAllItems(TRUE)` on the leaver** (`0x100C79B9`, unconditional).
- `cof_coopgameover` `0x100A23AE` (survival maps).
- **Story deaths.** `cof_killplayer` Use `0x100A42C0` sets deathbits from spawnflags, `health = 0` and `Killed()` for **every** connected player. It is placed on `c_attic`, `c_ending1b/2b/3b`, `c_ending5` and `c_subwaysick1`. In co-op every scripted death is therefore a team wipe. **CERTAIN**

**Countdown (PreThink `0x100EF800`)** applies to any player with `+0x4E8`: once per second it decrements `+0x4F0` and shows "Restarting the map in %i seconds" when the count is 0..5. **CERTAIN**

**Restart (`0x100E9C55–0x100E9CFF`)** runs only inside MultiplayerPlayerDeathThink, i.e. for a **dead** player's PreThink. **CERTAIN**

```
if (+0x4E8 == 1 && +0x4F0 <= 0 && time > +0x1F24 && !+0x1F28 && rules && rules->IsMultiplayer()) {
    +0x1F28 = 1;
    if (g_pCoopStore) for i in 1..maxClients: store->SavePlayer(UTIL_PlayerByIndex(i));   // no memset first
    CHANGE_LEVEL(STRING(gpGlobals->mapname), NULL);                                       // 0x100E9CF9
}
```

- **Saved to the store:** an entry per connected player, appended after any existing ones.
  - Players are normally all dead here, so the entries are **dead entries**: netname pointer, dead flag, stats, donator byte, **no weapons**.
  - A player who jump-respawned during the 10 s, which is allowed because the jump path ignores `+0x4E8`, gets a full alive entry. **CERTAIN**
- **Joiners inherit it.** MP PlayerSpawn copies `+0x1F24/+0x1F28/+0x4E8/+0x4EC/+0x4F0` from any player with `+0x4E8` (`0x100C9446`). It then freezes the joiner and shows "Game over" with the music.

**How the engine can prevent it while keeping the per-player death UI:**

- **The single gating field is `+0x4E8` (byte).** When it is 0:
  - the restart branch never runs;
  - DEADNOTIFY repeats (`time > +0x3D0`, every 12 s);
  - auto-respawn works (`+0x1F2C`);
  - CPR Use is allowed;
  - the countdown text stops.
- Clear `+0x4E8`, and for safety `+0x4F0 = 0` and `+0x1F24 = 0`, on **all** player slots in the same frame the wipe was decided. There are 10 s (5 s for the disconnect path) before the restart could fire.
- `+0x3D0/+0x3D4/+0x1F2C` were set by Killed before the wipe check, so the normal per-player timers are already in place.
- The three one-shot messages (GreyFade, Chapter "Game over", game_over.mp3) are already sent by then. Suppress them in the engine's MessageBegin path for that frame (MSG_ONE with ids `[0x1022667C]`, `[0x10226658]`, `[0x10226544]`), or undo them (stop the MP3, fade back).
- Intercepting only the `CHANGE_LEVEL(mapname)` from `0x100E9CF9` is worse: `SavePlayer` has already appended stale entries, `+0x1F28` is set, and `+0x4E8` keeps CPR and auto-respawn disabled.

## 5. What makes up "the items"

All entries below are in the **player save table `0x10217FE8` (207 fields, "PLAYER")** or in the owned weapons' tables. The engine's normal save/restore of the player edict and its weapon entities therefore carries them. **CERTAIN**

| Item state | Where | Save-table field (type) |
|---|---|---|
| Inventory slots 1..6 (story items **and** weapon descriptors) | `P+0x21FC` string_t[7] | `inventorySlot [7]` (STRING); `inventorySlotModels`/`inventorySlotClassname [7]` (only zeroed) |
| Three-slot mode | `P+0x1E9C` | `m_bThreeSlots` (BOOLEAN) |
| Weapon list (6 buckets, chained by `m_pNext`) | `P+0x2514` | `m_rgpPlayerItems [6]` (CLASSPTR) |
| Active / last / next item | `P+0x252C` / `+0x2534` / `+0x2538` | `m_pActiveItem`, `m_pLastItem`, `m_pNextItem` (CLASSPTR); `m_pStoredActionItem`, `m_pStoredNightVisionWeapon` |
| Reserve ammo, by ammo type | `P+0x2540` int[32] | `m_rgAmmo [32]` (INTEGER) |
| Weapon bitmask | `pev->weapons` | entvars `weapons` |
| Per-weapon clip / ammo types / default ammo | weapon `+0x1E4` / `+0x1DC` / `+0x1E0` / `+0x1F4` | `CBasePlayerWeapon` `0x1021B328` (27): `m_iClip`, `m_iPrimaryAmmoType`, `m_iSecondaryAmmoType`, `m_iDefaultAmmo`, timers, iron sights … |
| Per-weapon owner / link / id / descriptor | weapon `+0x160` / `+0x164` / `+0x168` / `+0x110` | `CBasePlayerItem` `0x1021B188` (26): `m_pPlayer`, `m_pNext`, `m_iId`, `script` (inventory descriptor), `slotnumber`, `m_bCanDualWield`, … |
| Weapon-specific state | glock / mobile / dual / spread / toggle | `CGlock` `0x1021B570` (`m_bTaclight` +0x200, `m_bWaitingToFix` +0x1F8); `CMobile` `0x1021B540` (`m_bPhoneCall`, `m_bDisabled`, `m_bWaitingToFix`); `CDualWeapon` `0x1021B610` (7); `CBaseSpreadWeapon` (3); `CBaseToggleWeapon` (`m_iWeaponMode`) |
| Dual wield | `P+0x5DC/+0x5E0`, `+0x1E70/+0x1E74`, `+0x624`, `+0x62C` | `m_iDualWeaponLeftAmmo/RightAmmo`, `m_iDualWeaponLeftID/RightID`, `m_sDualWieldViewModel`, `m_bMirroredDualWield` |
| Quick slots | `P+0x3D8` int[4] (1..3 used), `P+0x204C/+0x20CC/+0x214C` char[128] | `m_iQuickSlotSlot [4]`, `m_sQuickSel1..3 [128]` (CHARACTER), `m_flQuickSlotTimeDecay` |
| Tapes / has-saved | `P+0x1EF4` / `P+0x21D8` | `m_iTapes`, `m_bHasSaved` |
| Documents | `P+0x7A8 + 0x120·(i-1)` | `documentsstruct[1..16] [288]` |
| Other carried state | | `m_iOwnedWeaponsList [32]`, `m_bFirstDeploy [33]`, `m_bHasNightVision`, `m_iFlashBattery`, `m_fStamina`, `transferredentity_*` [51] (`cof_entityrestore`), `playerstats [80]`, `m_iGoodPoints`, keypad, padlock, objectives, ending flags |

- Health and armor are entvars fields (`health`, `armorvalue`), saved by the entvars table.
- Weapon pickup (`CBasePlayerItem::DefaultTouch` `0x1011C7D0`) skips a weapon the player already has (`HasNamedPlayerItem`). Otherwise it calls **`AddInventoryItem(script, model, classname)`** (`0x1011CB45`) and refuses with "Inventory full" when no slot is free. Weapons and story items therefore compete for the same 6 (or 3) slots, and giving a weapon back through `GiveNamedItem` also re-creates its inventory slot in the first free position. **CERTAIN** for DefaultTouch; LIKELY that GiveNamedItem goes through it (HLSDK pattern).
- Quick slots store **inventory slot indices** (QSlotUpdate carries `quickslot, inventorySlot`). Restoring items into different slot positions makes them point at the wrong item. **CERTAIN** for the format.
- **Story-driven losses** stay retail behaviour, and the engine must not undo them:
  - `cof_clearitems` (`c_start` camera, `c_asylum1` p345, `c_bridge` bagremove sf 5);
  - `cof_wheelchairmode` (`c_ending4b`; `0x100F5B30` runs `ClearInventorySlot(1,TRUE)` and `RemoveAllItems`);
  - `third_person_interaction` Spawn (`0x1007DF92`, `RemoveAllItems`);
  - key consumption.

## 6. The co-op store

**Storage.** `[0x10222E10]`, 0x10F3C bytes. **CERTAIN**

- Byte `[0]` is a mode flag. `SavePlayer` sets it to 0, and **nothing ever sets it to 1**.
- **Table A** has entries `i = 1..32` at `store + 0x41C·i`, fields from +4. **Table B** sits at `store + 0x41C·i + 0x87A0`.
- B is written only by the dead branch of SavePlayer. It is read by the loader and by ClientDisconnect only when byte `[0]` != 0, so it is effectively dead code.

**`SavePlayer(player)` `0x1002A480`** (thiscall, `ecx` = store). **CERTAIN**

- It takes the **first entry whose used byte (+4) is 0**. There is **no lookup by name and no dedupe**; if none is free it returns.
- **Alive player:**

  | Entry offset | Content |
  |---|---|
  | +0x04 | used = 1 |
  | +0x08 | `P+0x3B4` (a dword compared in GiveNamedItem's unlock/donator weapon check; meaning UNKNOWN) |
  | +0x0C | **`STRING(pev->netname)` as a `char*`** |
  | +0x10 / +0x14 | active weapon classname `char*` / its `m_iId` |
  | +0x18 | `0x100E4D60(player) != 0`, a Steam/donator check (LIKELY) |
  | +0x4C / +0x50 | **health (int) / stamina (int)** |
  | +0x1D4 | 0 (alive) |
  | +0x1DC..+0x220 | `playerstats` (`P+0x63C..0x680`) |
  | +0x228..+0x230 | `m_iQuickSlotSlot[1..3]` |
  | +0x27C / +0x2FC / +0x37C | `m_sQuickSel1..3` |
  | +0x3FD.. | `m_bFirstDeploy[1..32]` |
  | **+0x234 + 0xC·(s-1)** | for each inventory slot `s` 1..6 whose descriptor's `classname` key (read from the file by `GetItemDescription`) starts with `weapon_`: **weapon classname `char*`, `m_iClip`, `m_rgAmmo[m_iPrimaryAmmoType]`** (`0x1002A000`; skips buckets headed by `weapon_action`) |

- **Dead player** (`!IsAlive()`): used, netname, **dead flag +0x1D4 = 1**, stats and donator byte only. **No weapons, health or quick slots.** A copy also goes to table B.

**Loader `0x10029090`** (called only from MP PlayerSpawn `0x100C93E0`, i.e. at the end of **every** Spawn: changelevel arrival, respawn, join). **CERTAIN**

- **Match:** the first used entry `i` (1..32) with **`entry+0xC == STRING(player->pev->netname)` as a pointer compare** (`cmp dword` at `0x10029100`).
  - In the project engine `pev->netname = cl->name - pStringBase` (`sv_client.c:1403/1909`, 32-bit), so the pointer is `&svs.clients[slot].name`.
  - The match is therefore **client-slot identity, not the name text**. A rejoin into the same slot matches even for a different person; the same person in another slot does not. **LIKELY** (engine semantics), CERTAIN (DLL compare).
- **Alive entry** (`0x10029443–0x10029FF3`):
  - Restores health, stamina, `P+0x3B4`, quick slot indices, stats, `m_bFirstDeploy`, `m_sQuickSel1..3`.
  - For each of the 6 weapon fields it calls **`GiveNamedItem(classname, 0, donator)`** (the syringe also broadcasts `Syringe`). It then sets that weapon's `m_iClip` and `m_rgAmmo[primary]`.
  - `SelectItem(active classname)` if held. QSlotUpdate for the set quick slots. **`memset(entry, 0)`** (consumed). Returns 1.
- **Dead entry** (`0x100292D2`):
  - Restores stats only, then runs `Load2`, which reads `<gamedir>/maps/<map>.cfg`, and sends `AmmoX` ×32. Returns 1.
  - **Retail bug:** the clearing `memset` uses the ammo-loop counter (`esi = 32`) instead of `i` (`0x1002941D`). **It wipes entry 32 and leaves the dead entry in place.** The stale entry then matches on every later Spawn of that slot until a door, `cof_changelevel`, lobby or worldspawn memset clears the store.
- **No match:** if `sv_coop_nosave == 0` (default `"0"`) it calls `Load2` and sends AmmoX. **Returns 0**, so PlayerSpawn Uses (Touches) every `game_player_equip`.
- **`Load2` `0x100C6570`** parses `maps/<map>.cfg`: keys `health `, `stamina `, `quicksel1..3 `, and `weapon_*` lines followed by clip and ammo lines (GiveNamedItem plus set). It ends with a resend of all inventory slots.
  - **Only the 49 `c_cof_*`/`cof_*` co-op maps ship a `.cfg`.** No story `c_*` map has one, and no main-campaign map has a `game_player_equip`.

  **CERTAIN**

**Who fills it.**

- `inter_door_coop_changelevel` and `cof_changelevel`: memset, then SavePlayer for all.
- The team-wipe restart: SavePlayer for all, no memset.
- Never `trigger_changelevel`, death, respawn or disconnect. ClientDisconnect saves nothing, and the MP rules destroy the leaver's items.

**CERTAIN**

**Is it a usable building block for "a respawned player gets their weapons back"?** Partly.

- **Usable.**
  - The DLL already restores from it automatically inside Spawn, after Spawn's own inventory and ammo wipe, using the game's own `GiveNamedItem`.
  - Clips, primary reserve ammo, active weapon, quick-slot names and stamina come back.
  - Because the restore runs inside `rules->PlayerSpawn`, an entry the engine places before `respawn()` is consumed by that same Spawn.
- **Requirements and hazards:**
  1. **`SavePlayer` only records weapons for a living player.** At respawn time the player is dead, which gives a weapons-less entry. The engine must snapshot while alive, or briefly present the corpse as alive (`deadflag = 0`, `health > 0`) around the call and patch the stored health (+0x4C). The weapon list is intact until the PreThink respawn path's `RemoveAllItems`.
  2. Clear old entries for the slot first, because the loader takes the lowest matching index. Also beware the retail dead-entry leak.
  3. Matching is by client slot (above).
- **What it never carries:**
  - **story items** (non-`weapon_` descriptors) and their slot positions (weapons are re-added first-free, so quick-slot indices can point at the wrong slot);
  - ammo for types not used by a held weapon's primary, secondary ammo;
  - dual-wield state and ammo;
  - weapon-specific state (glock taclight, phone disabled/battery fix);
  - `m_iDefaultAmmo`, armor, flashlight battery;
  - `m_iTapes`, `m_bThreeSlots`, documents, keypad/padlock codes, objectives, good points (Spawn resets these to 2), ending flags.
- A full engine-side ledger (§5) is the better ground truth. The store is at best a convenience for the weapon half.

## Implications for the engine

1. **There is no co-op save path in the DLL, so the engine must own it.** The DLL never saves; saves come from the client's `savehack cofsave<N>`, which the project engine honours only when `maxclients == 1`.
   - **Hook point:** intercept the stringcmd `savehack cofsave<1..5>` on the server, before it reaches hl.dll, from any client, and perform the session save there.
   - The DLL has already frozen the player, charged the tape (difficulty 4) and shown "Saved" 3.25 s earlier.
   - `unfreezesave 1024 <N>` arrives first and could be used to start the save sooner, but PreSave runs in the same command.
   - Write the `cofsave<N>` label on the host, because the client writes `saveinfo<N>.cof` on its own machine (LIKELY).
   - The DLL has no autosaves and no map triggers saves (0 `trigger_autosave`/`point_servercommand` anywhere). Any autosave is engine policy.
2. **DLL save/restore works in MP as code.** Player and weapon Save/Restore have no MP gates and carry every item field in §5.
   - `CBasePlayer::Restore` re-places the player on a spawn spot when the save has no landmark (`0x100F1505`). After loading a co-op save, restore each player's saved origin and angles, or save with a landmark.
   - `ClientCommand cofload`/`loadgamemenu` are SP-only, so co-op loads must be engine-initiated.
3. **The capture set for "items never lost"** is the §5 table. Minimum: `inventorySlot[1..6]`, `m_bThreeSlots`, each weapon (classname, `m_iClip`, `m_iDefaultAmmo`, weapon-specific flags), `m_rgAmmo[32]`, active weapon, `m_iQuickSlotSlot[1..3]` + `m_sQuickSel1..3`, dual-wield fields, `m_iTapes`, documents. Add the Spawn-reset story fields: `m_iGoodPoints`, `m_sCustomObjectives`/`m_sObjectiveText`.
   - Restore story items through `AddInventoryItem` and weapons through `GiveNamedItem`, **in original slot order**, then fix quick-slot indices.
4. **Where items are destroyed:**
   - **MP respawn**: PreThink `0x100EF654–0x100EF71F` wipes slots, weapons, ammo and quick slots **before** `respawn()`. Spawn wipes slots and zeroes ammo again. The store restore then runs inside Spawn.
   - **MP disconnect**: `RemoveAllItems(TRUE)` at `0x100C79B9`.
   - **Death** destroys nothing.
   - To give a respawned player their items back, snapshot while alive (or at death, from the intact weapon list) and re-give after `Spawn` returns. An alternative is to pre-load a correct store entry for that client slot.
   - For a rejoin, snapshot at disconnect, before ClientDisconnect.
5. **Revive from the engine:** call `StartRevivedAnim` (`0x100F1860`, thiscall on the dead `CBasePlayer*`). The DLL completes the revive in the player's own PreThink after the animation: `DEAD_NO`, WALK, **health 30**, weapon redeploy, HUD messages, inventory untouched.
   - It requires a connected, simulating client.
   - Clear the CPR links if a real CPR is underway, and clear any pending restart.
6. **Prevent the DLL team wipe by keeping `+0x4E8 == 0` on every player slot**; also zero `+0x4F0` and `+0x1F24`.
   - The wipe is decided synchronously inside MP Killed (and in MP ClientDisconnected). Fix it up in the same frame, and swallow or undo GreyFade, Chapter "Game over" and game_over.mp3.
   - Per-player DEADNOTIFY, jump/auto respawn and CPR keep working because they depend only on `+0x3D0/+0x3D4/+0x1F2C` and `+0x4E8 == 0`.
   - "Everyone dead" in the DLL's own terms is: no `fuser4 == 0` player alive or mid-`revived`. Use the same test to trigger "load last save".
7. **Story deaths kill everyone.** `cof_killplayer` on `c_attic`, `c_ending1b/2b/3b`, `c_ending5` and `c_subwaysick1` calls `Killed` on all players. With the wipe suppressed, the engine's "all dead → load last save" path must handle these, which matches SP, where they lead to the death/load menu.
8. **If the co-op store is used at all,** memset it (or the slot's entries) before writing, call `SavePlayer` only for living players, and expect the dead-entry leak. Matching is by client-slot name pointer, not by player name, so a rejoin needs the same slot or an engine-side ledger.
