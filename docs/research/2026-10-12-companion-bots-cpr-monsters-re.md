# F: AI companion as a fake client — bots, CPR, monster hostility, player damage (static RE), 2026-10-12

Binary: `runtimes/enhanced/cryoffear/cl_dlls/hl.dll` (stock, SHA-256 `0036B91C…0E63`, preferred base `0x10000000`;
RVA = VA − `0x10000000`). Engine source: `upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/server`.
Maps: the 81 story maps (the campaign map list), read with `cofcoop/bsp.py`. Static only: capstone/pefile,
nothing launched or modified (the throwaway analysis scripts were not kept).

Tags: **CERTAIN** = read from machine code / engine source / BSP data; **LIKELY** = code read, name or runtime
consequence inferred (HLSDK parity); **UNKNOWN** = not established.

Builds on `docs/research/2026-09-30-hl-exits-coop-re.md` §4 and `2026-10-04-hl-saves-death-items-re.md` §2–4 (CPR,
respawn, team wipe). Facts already stated there are only summarised; new findings are marked **NEW**.

---

## 0. Headline findings

1. **NEW — `CBasePlayer::Spawn` clears `FL_FAKECLIENT`.** `0x100F414B`: `pev->flags &= FL_PROXY(0x100000); pev->flags |= FL_CLIENT`.
   `SV_FakeConnect` sets `FL_CLIENT|FL_FAKECLIENT` on the edict, but `pfnClientPutInServer` → Spawn (and every later
   respawn) wipes `FL_FAKECLIENT`. `SV_InactivateClients` (`sv_frame.c:966`) drops fake clients **by the edict flag**, so
   after the first Spawn the bot is **not** dropped at a level change: it is demoted to `cs_connected` and, because
   `cl->flags` still has `FCL_FAKECLIENT`, it never times out (`sv_main.c:543`) and never signs on → a ghost slot per
   map change, and the companion module (which resets its table on `spawncount` change) adds a new bot in another slot.
   **CERTAIN** (DLL + engine source). Fix in the engine (test `cl->flags & FCL_FAKECLIENT`), see §1.9.
2. **NEW — friendly fire hinges on that same flag.** MP `FPlayerCanTakeDamage` (`0x100C8700`): damage is refused when
   *attacker IsPlayer* **and** *victim lacks `FL_FAKECLIENT`*. So players never hurt human players, but **any player
   can hurt a victim that has `FL_FAKECLIENT`**. With the flag cleared by Spawn the bot is protected like a human;
   re-asserting `FL_FAKECLIENT` on the bot edict would let the human's bullets/melee hurt the companion. `mp_friendlyfire`
   is dead code (only in the never-instantiated teamplay rules). **CERTAIN**.
3. **The bot is a full participant** (`fuser4 == 0`): it counts as a survivor for the DLL team wipe (good: a living bot
   blocks "Game over" and can CPR the human), for the 4-participant connect cap, and for **monster damage scaling**
   (NEW: monster damage to players × `1.2·(1+0.2·participants)` — a bot raises damage to a solo human from ×1.44 to ×1.68).
   **CERTAIN**.
4. **CPR** is reachable by a bot: player `ObjectCaps` is a constant `FCAP_IMPULSE_USE (8)`, CPR `Use` (`0x100F88C0`)
   only needs `activator->IsPlayer() && IsAlive()`. Selection is the CoF `PlayerUse` (`0x100EA630`, NEW): eye trace 80 u,
   else sphere 80 u + view-cone dot > 0.7, else trace 128 u. Hold `IN_USE` ≥ `int(sv_coopcprtime)` (5 s); release cancels.
   **NEW hazard**: if the reviver dies mid-CPR, the downed player's reviver handle (`+0x26A0`) stays valid and **blocks
   every later CPR on that player** until they respawn or the dead reviver disconnects. **CERTAIN**.
5. **Monster targeting** is HLSDK `Look`/`BestVisibleEnemy` with a CoF 19×19 relationship table (`0x10217118`). Every
   story monster except `monster_generic` (class 11, ally) and the `c_start2` scare actor (`m_iClass 3`) is hostile to
   **all** players equally (class 2), nearest visible first. CoF additions: in MP monsters ignore a player whose
   `fuser3 == 3` (being revived), `m_iPlayerReact` (+0x354) overrides player relations, `monster_booksimon` picks a
   random participant slot when idle. **CERTAIN**.
6. **Dead monsters keep `health > 0`** (BecomeDead `0x100876D0`: `takedamage = DAMAGE_YES`, `health = max_health/2`). Use
   `deadflag == DEAD_NO && takedamage == DAMAGE_AIM` to call a monster alive. **CERTAIN**.

---

## 1. Bots in the retail DLL

### 1.1 Entry points (DLL_FUNCTIONS table `0x102119F8`, rules vtable `0x101D608C`) — CERTAIN

| Engine call | DLL function | What it does for a fake client |
|---|---|---|
| `pfnClientUserInfoChanged` (already inside `SV_FakeConnect`, `sv_client.c` → `SV_UserinfoChanged`) | `0x1001D870` | Returns at once if the edict has **no private data** (`[edict+0x7C]==0`). Otherwise (stale object from a previous occupant of the slot!) replaces `%` by space in the name, prints `* X changed name to Y` (SayText MSG_ALL), then rules `+0x6C`. |
| `pfnClientConnect` | `0x1001CC70` → MP `ClientConnected` `0x100C7640` | `n = CountParticipants()` (`0x1010A470`: player slots with `fuser4 == 0`, **not** alive-filtered); `SteamUserStats()->RequestCurrentStats()` (import `0x101A11A4`, result ignored); **reject only if `n >= 4`** with `"Server is full (4 players max)"`; clears `FL_NOTARGET` on the edict (`[edict+0x228] &= ~0x80`); voice-mask init (`0x10001310`). Returns 1. No name, auth, Steam-ID or lobby check. |
| `pfnClientPutInServer` | `0x1001D830` | `GetClassPtr<CBasePlayer>` (`0x100125E0`: **reuses existing private data**, allocates 0x281C only if none), `SetCustomDecalFrames(-1)` (`0x100F2DF0`), `Spawn()` (vtable `+0x2C` = `0x100F4090`), `effects |= EF_NOINTERP`. |
| — (first `PlayerPreThink` of the bot) | PreThink `0x100EF2D0` → `UpdateClientData` member (slot 150, call at `0x100F024A`) → `m_fInitHUD` → MP `InitHUD` `0x100C8B70` | See 1.4. InitHUD therefore runs only once the engine runs the bot's first usercmd. |

### 1.2 `CBasePlayer::Spawn` (`0x100F4090`) — what matters for a bot (CERTAIN unless noted)

- classname `player`, health 100, `takedamage = DAMAGE_AIM (2.0)`, `SOLID_SLIDEBOX`, `MOVETYPE_WALK`, deadflag 0.
- **flags `&= FL_PROXY; |= FL_CLIENT`** (`0x100F414B/0x100F4158`) — clears `FL_FAKECLIENT`, `FL_FROZEN`, `FL_NOTARGET`, `FL_GODMODE`.
- Server model `models/player.mdl` (`0x100F47FD`); clients draw the per-slot model from userinfo (1.3).
- Clears CPR handles `+0x2698/+0x26A0`, `+0x1F2C = 0`, restart state `+0x4E8/+0x4EC/+0x4F0 = 0`, **`fuser4 = 0`** (`0x100F448B`), `m_iGoodPoints = 2`, inventory slots, all ammo, `m_fInitHUD = 1`.
- `CLIENT_COMMAND(ed, "donatorcheck\n")` and physinfo keys (`mtl`, `slj`, `hl`, step times) — the stufftext is silently dropped for a fake client (`sv_game.c` stuffcmd `FCL_FAKECLIENT` return); physinfo works.
- Calls `rules->GetPlayerSpawnSpot` (1.5) and `rules->PlayerSpawn` (1.4).

### 1.3 MP `ClientUserInfoChanged` (`0x100C79E0`) — model

Forces the client `model` key by **client slot**: slots 1–4 → `police1`..`police4` (overridable through string_t fields
`+0xE0..+0xEC` of the object at `[0x10223190]`, LIKELY `CCoFCustomisePlayerModels`), slot > 4 → `police1`; if it differs,
`SetClientKeyValue(model)` and `MPModel` (MSG_ONE, dropped for a bot). The `gordon` model `SV_FakeConnect` puts in the
userinfo is replaced. Nothing requires a reply. **CERTAIN**.

### 1.4 MP `PlayerSpawn` (`0x100C9210`) and `InitHUD` (`0x100C8B70`)

PlayerSpawn, in order (**CERTAIN**):
1. `CLIENT_COMMAND("donatorcheck")` (dropped), `pev->weapons |= 0x80000000` (suit bit).
2. Once per map (`[0x102231A1]`), random-event timer setup (cof_survivalmode-aware); harmless.
3. `SimonMode` (MSG_ONE) if `[0x10222F14]`; boss-lock `[0x10222F20]` → `fucked.mp3`/GreyFade; else `sv_coop_music` MP3 — all client-only.
4. **Equipment**: co-op store restore `0x10029090` (matches the client-slot name pointer); on no match → `Load2` reads `maps/<map>.cfg` (none for story maps) and then **Touches every `game_player_equip`** (none in the 81 story maps — CERTAIN, BSP scan). **A spawned bot gets nothing but the suit bit**; the engine's starter-kit/gift sharing (`sv_cof_party.c`) is the only source of weapons.
5. Copies a pending team-wipe restart (`+0x4E8` etc.) from any player and then freezes + "Game over" (§2.7); copies a camera-model wait (`+0x3C8/+0x3CC`) when another player's `+0x5AC` model is active (known, cutscene report).

InitHUD (runs from the bot's first PreThink, **CERTAIN**): `fuser4 = 0`, `+0x3A9 = 0`, three empty Custom* messages
(dropped), `UTIL_ClientPrintAll("%s has joined the game")` (visible to the humans every map), log line,
`UpdateGameMode`, per-player Health/ScoreInfo to the bot (dropped), `DevDonator` MSG_ALL (Steam-ID compare via
`GetPlayerAuthId`, harmless), lobby branch only when rules byte `+0xA8` is set (lobby maps; not story), then
`ClientUserInfoChanged`, then an `env_model` resend loop.

### 1.5 Spawn point and telefrag (EntSelectSpawnPoint `0x100DF180`, via `0x100AF610`, MP GetPlayerSpawnSpot `0x100C8A20`)

Order (MP rules, launcher `coop 0`, `deathmatch 1` — `cofcoop/core.py:290`; MP `IsDeathmatch` returns 1 unconditionally, `IsCoOp` returns `gpGlobals->coop`):

1. For the player's own slot N=1..4: first `cof_coop_spawnN` with `iuser1 == 0`.
2. If `IsCoOp` (0 here): `info_player_coop`, then `info_player_start`.
3. If `IsDeathmatch` (yes): random `info_player_deathmatch`; **only this branch telefrags** (`TakeDamage(300)` on players within 128 u, `0x100DF5EE`, attacker = world).
4. If `gpGlobals->startspot` is set (a landmark transition): `FindEntityByTargetname(startspot)` — i.e. the destination **`info_landmark`**; if nothing has that name → `"PutClientInServer: no info_player_start on level"` and the **world origin**.
5. Otherwise the first `info_player_start`.

Then origin = spot + (0,0,1), angles from the spot, `v_angle = punchangle = velocity = 0`, `fixangle = 1`. MP
GetPlayerSpawnSpot **fires the spot's `target`** with the spawning player as activator (`0x100C8A62`).

Story maps (CERTAIN, BSP scan): 142 `info_player_start`, **0** `info_player_deathmatch`, **0** `info_player_coop`,
**0** `cof_coop_spawnN`, and **no `info_player_start` has a `target` or `targetname`**. So a bot spawned mid-map lands on
the landmark (after a transition) or the first `info_player_start`, **never telefrags**, and fires nothing. Overlap with a
player is resolved by MP PreThink (SOLID_NOT while intersecting another player, known). The engine's
`SV_CoF_PartyPlayerReady` then moves it.

### 1.6 What breaks without a real client

Messages to a fake client are dropped in `SV_Multicast` (`sv_game.c:438-439`, MSG_ONE/ALL loops skip `FCL_FAKECLIENT`);
`pfnClientPrintf`, stuffcmd, `pfnCrosshairAngle`, `pfnFadeClientVolume` return early; `pfnSetView` records
`pViewEntity` but sends nothing. **No DLL path needs a client reply to spawn, InitHUD, fight, die, respawn or CPR**
(InitHUD is driven by `m_fInitHUD` in PreThink, the CPR completion by PreThink). **CERTAIN**.

Features that do wait for a client reply — the bot must never start them:

| Feature | Client reply needed | Bot outcome |
|---|---|---|
| `tape_recorder` (+use) | menu → `unfreezesave`, `savehack` | menu dropped; harmless but pointless. |
| `cof_phonecall` | answering by taking the phone out | handled by the engine moments layer (bots excluded as callees, `sv_cof_moments.c:245`). |
| `cof_yesno` | `yesnoactivate/yesnono` | never answered; engine filters prompts. |
| keypads, padlock, `cof_computer`, clothes menu | VGUI + `unfreeze`/item commands | **STUCK-PLAYER for the bot**: e.g. computer state `P+0x1EA0 != 0` blocks `PlayerUse` (§2.1) until the next Spawn. |
| `cof_strangle` (c_ending1c–3c) and other camera-model QTEs | `attack1pressed/attack2pressed` set buttons on `P+0x5AC`'s model (`0x1001A0D6..`) | only matters if the bot were FIRST player; it never is while the host is slot 1. |

All of these are reached through **+use** (`PlayerUse`) — weapons and ammo too: CoF items are picked up with +use
(`m_pfnUse = DefaultUse 0x10017AC0`; `DefaultTouch` is armed only when `pev->iuser4 == 1`, `0x1011D1E9`). A bot that never
presses `+use` (and does CPR by a direct call, §2.8) cannot trigger any of them. **CERTAIN** for the gates.

### 1.7 Who counts as a participant / survivor (CERTAIN)

| Counter | Code | Includes the bot? |
|---|---|---|
| `CountParticipants` | `0x1010A470`: slots 1..maxClients, `fuser4 == 0` | yes (connect cap of 4; booksimon target range; monster damage scaling) |
| `CountAliveParticipants` | `0x1010A400`: `IsAlive() && fuser4 == 0` | yes (`trigger_coop_changelevel` — **0 in story maps**) |
| Team-wipe "survivor" | MP Killed `0x100E9375`: `fuser4 == 0` and alive or playing `revived` | **yes — a living bot prevents the DLL "Game over"** |
| MP `ClientDisconnected` `0x100C76D0` | after the leaver's `fuser4 = 1`: if no alive participant → Game over, 5 s | dropping the bot while every human is dead **starts a game over** |
| `cof_killplayer` Use `0x100A42C0` | every `UTIL_PlayerByIndex(1..max)`, no filter: deathbits, `health = 0`, `Killed()` | yes (and dead/non-participants too) |
| `trigger_playercheckarea`, `cof_playercheck`, `cof_survivalmode`, `game_player*` | — | **not present** in the 81 story maps |
| Engine exit hold (`sv_cof_exit.c:45`), recovery, share, session | — | engine code skips `FCL_FAKECLIENT` |

### 1.8 Disconnect of a bot (`ClientDisconnect` `0x1001CC90`) — CERTAIN

Clears the leaver's `+0x2348` handle; **for every player whose reviver handle `+0x26A0` is the leaver, clears it**
(`0x1001CD0D`); `fuser4 = 1`, `SOLID_NOT`, `takedamage = 0`, `FL_NOTARGET`, relink; `"- %s has left the game"`
(SayText to all); rules `ClientDisconnected` (`RemoveAllItems(TRUE)` on the leaver, fires `game_playerleave` — absent in story;
the game-over rule above). Monsters whose `m_hEnemy` is the leaver keep that handle (edict persists; `IsAlive` stays true
because health/deadflag are untouched) — LIKELY they chase its last position until they lose it.

### 1.9 Recommended engine recipe — add / keep / remove the bot

1. `ent = SV_FakeConnect(name)`. **Then `SV_InitEdict(ent)`** and restore `ent->v.flags = FL_CLIENT|FL_FAKECLIENT`,
   `ent->v.netname = SV_MakeString(cl->name)`, `ent->v.colormap = NUM_FOR_EDICT(ent)` — the same as the real-client branch
   (`sv_client.c` "need to realloc private data for client"). Reason: `GetClassPtr` reuses a previous occupant's
   CBasePlayer (stale documents, ending flags, keypad codes, `m_fGameHUDInitialized`, CPR handles of others pointing at it).
   (The name-change print inside `SV_FakeConnect` already hit the stale object; cosmetic.)
2. `pfnClientConnect(ent, name, "127.0.0.1", reject)` — fails only with 4 participants.
3. `pfnClientPutInServer(ent)`; then place it (`SV_CoF_PartyPlayerReady`).
4. **Level change**: make `SV_InactivateClients` drop on `FBitSet(cl->flags, FCL_FAKECLIENT)` (or drop companion bots
   explicitly before `SV_InactivateClients`). Do **not** fix it by re-setting `FL_FAKECLIENT` on the edict unless you also
   want the human to be able to shoot the companion (§4.1). If the edict flag is wanted for other engine code
   (`pfnGetAimVector`, `sv_pmove.c` physent name), add `FL_FAKECLIENT` only around those uses or accept friendly fire.
5. Run one usercmd per frame **also while the bot is dead** (corpse physics, death think, being revived — §2.6).
6. Before removing a bot on purpose (`cof_companion 0`, `SV_CoF_CompanionMakeRoom`), make sure a human is alive, or
   clear the restart state (`+0x4E8/+0x4F0/+0x1F24` on all players) right after the drop.
7. Optional: suppress the per-map "has joined/has left the game" lines for companion bots (SayText/TextMsg in the
   message hook during the add/drop).

---

## 2. CPR revive in detail

Downed player = `this`, reviver = activator. Field map (CBasePlayer, CERTAIN unless noted):
`+0x2698` EHANDLE *my CPR target*, `+0x26A0` EHANDLE *my reviver*, `+0x26AC` CPR completion time, `+0x4F4`/`+0x4FC`
anim end times, `+0x501` gibbed flag, `+0x4E8` restart pending, `+0x610` m_flNextUse, `+0x22AC/+0x22B0/+0x22B4`
buttons last/pressed/released, `+0x24DC` m_pTank, `+0x1EE8` in phone call, `+0x1EA0` computer state;
`pev->fuser3` (+0x260): 2 = doing CPR, 3 = being revived, 4 = getting up, 5 = phone, 25 = syringe view-lock.

### 2.1 Target selection: `CBasePlayer::PlayerUse` `0x100EA630` (NEW) — CERTAIN

Called from ImpulseCommands (`0x100E2A60`, vtable slot 151 `+0x25C`, at `0x100E2B5E`, skipped on the menu maps
`c_server_settings/c_difficulty_settings/c_loadgame/c_video_stress_test` and when `+0x1EA0 != 0`), which is called by
ItemPostFrame `0x100E4F80` (from PostThink `0x100EED58`) only when there is no tank, `+0x230C == 0` and
`time >= +0x308` (`m_flNextAttack`, LIKELY). PostThink itself does nothing for a dead player (HLSDK, LIKELY).

Guards: not in a phone call (`+0x1EE8`), `+0x1EA0 == 0`, not on a ladder (`movetype != MOVETYPE_FLY`, `0x100E4ED0`),
`(button | pressed | released) & IN_USE`, `time >= +0x610`, **own `+0x2698` not valid** (no other use while doing CPR).
Pressed `IN_USE` first releases a tank (`+0x24DC`) or a `+0x230C` controller.

Choosing the object:
1. `UTIL_TraceLine(eye, eye + forward·80, dont_ignore_monsters, self)`; if `tr.pHit` caps `& 0x38` → that object.
2. Else `FindEntityInSphere(origin, 80)` (engine: distance from `origin` to the entity's **absbox** < 80); skip self and
   caps `& 0x100`; for caps `& 0x38`: `LOS = centre − eye`, where centre = origin + centre of the current **sequence
   bbox** for monsters/players (`ExtractBbox 0x1000E730`), else `VecBModelOrigin`; `LOS = ClampVectorToBox(LOS, size/2)`;
   pick the largest `dot(LOS, v_forward)` above **0.7** (≈ 45.6°), or an object with `LOS == 0`.
3. Else trace 128 u; `pHit` caps `& 0x200` → that object.

Firing: `(button & IN_USE && caps & FCAP_CONTINUOUS_USE)` or `(pressed & IN_USE && caps & (IMPULSE|ONOFF))` →
`Use(this, this, USE_SET=2, 1.0)`; `released & IN_USE && caps & ONOFF` → `Use(…, 0)`. For a non-continuous object
`m_flNextUse = time + 0.5`.

**Player `ObjectCaps`** (vtable `+0x40` = `0x10013F60`) returns the constant **8 (FCAP_IMPULSE_USE)** dead or alive.
A dead player's box is `(-16,-16,-36)–(16,16,36)` after the death think (`0x100E9E39`). So: stand within ~80 u of
the corpse box, look at it (dot > 0.7 or eye trace hits it) and produce a **press edge** of `IN_USE`. Anything usable
that wins the trace or the cone (door, item, keypad, `inter_door`) is used instead. **CERTAIN**.

### 2.2 Start: player `Use` `0x100F88C0` (thiscall, 4 stack args, `ret 0x10`) — CERTAIN

Guards: `this+0x501 == 0`; `this+0x26A0` not valid; `this+0x4E8 == 0`; activator non-null, `IsPlayer`, `IsAlive`;
`this` **not** alive; reviver's `+0x2698` not valid; **`this->pev->flags & FL_ONGROUND`**. Nothing checks distance,
`FL_FAKECLIENT`, `fuser4`, or whether the activator is a bot → **a bot can revive, and can be revived**.

Effects: reviver velocity 0, **reviver origin := corpse origin**, yaw := corpse yaw + 90, pitch 0, `v_angle.x = 0`,
`fixangle = 1`; reviver plays `cpr_down`/`crouch_cpr_down` and the corpse `taller_lie`/`crouch_taller_lie` (both
crouch tests read the **corpse's** `FL_DUCKING`, `0x100F89DC`/`0x100F8A84`); `dur = (int)CVAR("sv_coopcprtime")`,
`≤ 0 → 5`; `reviver+0x2698 = this`, `reviver+0x26AC = time + dur`, `this+0x26A0 = reviver`; ThirdPerson on the reviver.

### 2.3 Hold: reviver's PreThink `0x100EFCB4–0x100F01E6` — CERTAIN

Each reviver PreThink: recompute pressed/released; while linked keep `fuser3 = 2` and loop `cpr_idle`.
- **Cancel** when `released & IN_USE`: clear target `+0x26A0` (if the target IsPlayer) and own `+0x2698`, `+0x26AC = time`, `cpr_up`, `fuser3 = 4`.
- **Cancel** when the target is alive or the target's `+0x26A0` is not this reviver: clears only its own link.
- **Complete** when linked, `button & IN_USE`, `time > +0x26AC`: `target->StartRevivedAnim()` (`0x100F1860`, the only writer of `fuser3 = 3`), clear both links, `cpr_up`, `fuser3 = 4`, `+0x4FC = cpr_up end`.
- Get-up end (`fuser3 == 4`, `time > +0x4FC`, unlinked): ThirdPerson off, `fuser3 = 0`, weapon redeploy `0x100F2A50(1)`.

No distance check, no damage check, no freeze: moving away does not cancel (whether pmove blocks movement while
`fuser3 == 2` is UNKNOWN; send zero movement). A camera starting mid-CPR does not cancel.

### 2.4 Completion on the downed player: `MultiplayerPlayerDeathThink` `0x100E9AB0` — CERTAIN

`fuser3 == 3 && time > +0x4FC` → `DEAD_NO`, `MOVETYPE_WALK`, clear `FL_ONGROUND`, **health 30**, `Health(30)` to all,
redeploy, ThirdPerson off, `fuser3 = 0`, blank ProFont. Runs **only inside the downed player's own PreThink**, i.e. only
while that client sends usercmds — for a bot, only while the engine runs its usercmds.

Death-think tail (`0x100E9D51–0x100E9E39`): advances the dying anim (`+0x2654` counter), `movetype → MOVETYPE_NONE` once
`FL_ONGROUND`, `DEAD_DYING → DEAD_DEAD`, `EF_NOINTERP`, size `(-16,-16,-36)-(16,16,36)`. Client edicts get no
`SV_Physics` (`sv_phys.c:1797`); their fall and `FL_ONGROUND` come from `PM_Move` inside `SV_RunCmd` (LIKELY via
`PM_Physics_Toss` for the dead TOSS player). **A bot that gets no usercmds while dead hangs where it died and is never
`FL_ONGROUND` → cannot be CPR'd.**

While `fuser3 == 3` (playing `revived`), monsters ignore that player in MP (§3.1). The reviver doing CPR is not protected.

### 2.5 Alternative: syringe on a partner (NEW) — CERTAIN for the flow, LIKELY for the name

`weapon_syringe` vtable `0x101BBB70`, slot `+0x1A8` (`0x1007B110`, SecondaryAttack — called by the base ItemPostFrame
under `button & IN_ATTACK2`, `0x1011D9C2`), MP only: trace 48 u from the eye; a hit **living** player with health < 100
is healed; otherwise, from the trace end, `FindEntityInSphere(64)` for a **dead, non-gibbed player** → syringe
target `+0x1F8`; later `BladeRetract` (`0x1007A5CF`) calls `StartRevivedAnim(target)` and spends a syringe (no 5 s
hold, no FL_ONGROUND check). Both freeze the user briefly (`fuser3 = 25`, `ViewLock 0x100F9140`). Syringes are scarce
story healing items; keep CPR the default.

### 2.6 Respawn of a downed player (PreThink MP branch, known; confirmed) — CERTAIN

Dead and either `time > +0x3D4 && (pev->button & IN_JUMP)` (level-triggered, no edge; allowed even with a restart
pending) or `+0x4E8 == 0 && +0x1F2C != 0 && time > +0x1F2C` (`sv_auto_respawn_time`, default 384 s). It wipes the
inventory and calls `Spawn()` (1.2/1.5: lands on landmark / `info_player_start`). The engine writes `+0x3D0/+0x3D4` at
death from `cof_story_respawn_delay` (default 10 s, `sv_cof_party.c:205`).

**What a bot needs**: keep running usercmds while dead; send `IN_JUMP` only when it decides to give up waiting (e.g.
human dead/far for N seconds); never send `IN_JUMP` while the human is reviving it (a respawn cancels the CPR).

### 2.7 Hazards (CERTAIN mechanics)

| # | Scenario | Effect | Treatment |
|---|---|---|---|
| H1 | Reviver dies mid-CPR (monster hit) | Killed clears only the reviver's own links; the downed player's `+0x26A0` still names the (valid) dead reviver → **every later CPR on that player fails the guard** until they Spawn/Killed or the reviver disconnects. STUCK-PLAYER (must jump-respawn). | Engine, per frame: for each dead player P with valid `P+0x26A0 = R`, if R is not alive or `R+0x2698 != P`, clear `P+0x26A0`. |
| H2 | Bot presses `+use` near a door/item/keypad | wrong object used: item stolen, `inter_door` teleport, keypad/computer lock (`+0x1EA0`) blocks all later `+use` incl. CPR. | Do CPR by direct call (2.8 B) and never synthesise `+use` presses; or replicate the 2.1 selection and only press when the corpse wins. |
| H3 | Bot dropped while all humans are dead | DLL game over (5 s) | 1.9 step 6. |
| H4 | Bot corpse not simulated | never `FL_ONGROUND` → not revivable; death think never completes a revive | run usercmds for the dead bot. |
| H5 | Bot in `revived` but its usercmds stopped | revive never completes | same. |

### 2.8 Recipes

**A. Bot revives a human, natural input** (all CERTAIN): preconditions above (corpse dead, `FL_ONGROUND`,
`+0x501 == 0`, `+0x26A0` invalid, `+0x4E8 == 0`; bot alive, unlinked, not on a ladder/phone/computer, `time ≥ +0x610`,
`time ≥ +0x308`). Walk until the corpse absbox is < 80 u from the bot origin (aim for ≤ 50 u horizontally), aim at the
corpse centre (`origin.z` − ~20; pitch down), send one usercmd **without** `IN_USE`, then usercmds **with** `IN_USE`,
zero movement, for `int(sv_coopcprtime)` s + one frame (≥ 5.1 s at the default). Stop holding when the corpse's
`deadflag` changes or the bot's `+0x2698` clears. Leave the corpse after `cpr_up` (bot `fuser3` back to 0).

**B. Bot revives a human, direct call** (no PlayerUse involved): after the same preconditions, call
`0x100F88C0` thiscall `this = corpse CBasePlayer*`, args `(pActivator = bot CBaseEntity*, pCaller = bot, useType 2, value 1.0f)`
before the bot's next usercmd, then hold `IN_USE` in every bot usercmd until completion (no release edge from the
moment of the call). PlayerUse is blocked while the bot's `+0x2698` is valid, so holding `IN_USE` triggers nothing else.

**C. Human revives the bot**: nothing to do except keep running bot usercmds (dead: no buttons, no jump) so the corpse
lands and the bot's death think completes the `revived` animation.

**D. Engine shortcut** (already documented): `StartRevivedAnim(downed)` (`0x100F1860`, thiscall, no args) — needs the
downed player's usercmds to finish.

---

## 3. Monster hostility and targeting

### 3.1 Mechanism — CERTAIN

- **Look** (`0x100BFC50`, vtable `+0x124`, shared by every story monster class): `UTIL_EntitiesInBox(±dist, FL_CLIENT|FL_MONSTER)`;
  skip self, `spawnflags & 0x10` (SF_MONSTER_PRISONER), `health <= 0`; `monster_target` only for monsters whose
  `targetname == target.noise3`; **in MP skip any entity with `fuser3 == 3.0` (being revived)**; then require
  `IRelationship != R_NO`, `FInViewCone` (`+0x154`), **`!(flags & FL_NOTARGET)`**, `FVisible` (`+0x114`).
  `SF_MONSTER_WAIT_TILL_SEEN (1)` handling as HLSDK. Sets SEE_CLIENT/SEE_ENEMY/SEE_HATE/… conditions (`+0x180`).
- **CoF addition for `monster_booksimon` only** (`0x100BFC70–0x100BFD9A`): when it has no enemy and `pev->frags == 0`,
  it takes `UTIL_PlayerByIndex(RandomLong(1, CountParticipants()))` as enemy without sight (index-based: with
  non-contiguous slots some picks return NULL). Bots can be picked.
- **IRelationship** (`0x100BF7F0`, vtable `+0x138`, shared): `t = target->Classify()`; if `t == CLASS_PLAYER (2)` and
  `m_iPlayerReact (+0x354) != 0`: `1 → R_NO`, `4 → R_HT`, other → `R_HT` only if `+0x2EC & 1` (bits_MEMORY_PROVOKED,
  LIKELY) else `R_NO`. Otherwise `iEnemy[this->Classify()][t]`, table **`0x10217118`, 19×19 int32, row = self, column =
  target** (HLSDK 14 classes + CoF 14–18).
- **BestVisibleEnemy** (`0x100BC7E0`, `+0x14C`): HLSDK — highest relationship, then nearest (start 8192). **GetEnemy**
  (`0x100BED80`, `+0x208`) as HLSDK, plus: when the new enemy IsPlayer, `enemy->+0x2340 = this` (who chases me).
- Players: `Classify = 2` (`0x100DD970`) for humans and bots alike. **Monsters treat the bot and the human identically:
  nearest visible, non-`FL_NOTARGET`, not-being-revived player.**
- `m_iClass` (+0x350, key `m_iClass`) and `m_iPlayerReact` (+0x354, key `m_iPlayerReact`) are CBaseMonster keys
  (`0x100BFA1D/0x100BFA6C`); **`monstermaker` copies both to its child** (`0x100BBC89–0x100BBC9B`).
- Monster fields for the engine: `m_afConditions +0x180`, **`m_hEnemy +0x184`** (EHANDLE `{edict_t*, serialnumber}`,
  valid iff `edict->serialnumber == serial`; `0x10017BA0`), `m_vecEnemyLKP +0x2F4`, `+0x330/+0x334` AI trigger
  condition/target, `m_iClass +0x350`, `m_iPlayerReact +0x354`. CBaseMonster-derived sizes in story: 0x368–0x3C0.
- **Death** (BecomeDead `0x100876D0`): `takedamage = DAMAGE_YES (1.0)`, `health = max_health·0.5` (> 0!), `max_health = 5`.

Relevant relationship values (`iEnemy[row][2]` = attitude to players; R_AL −2, R_FR −1, R_NO 0, R_DL 1, R_HT 2):

| Class | 2 PLAYER | 3 HUM_PASSIVE | 4 HUM_MIL | 5 ALIEN_MIL | 7 ALIEN_MONSTER | 11 PLAYER_ALLY | 18 (CoF) |
|---|---|---|---|---|---|---|---|
| row 3 HUMAN_PASSIVE | **−2** | −2 | 2 | −1 | 2 | −2 | −1 |
| row 4 HUMAN_MILITARY | **2** | 1 | 0 | 2 | 1 | 2 | −1 |
| row 5 ALIEN_MILITARY | **2** | 1 | 2 | 0 | 0 | 1 | −1 |
| row 7 ALIEN_MONSTER | **1** | 1 | 1 | 0 | 0 | 1 | −1 |
| row 11 PLAYER_ALLY | **−2** | −2 | 1 | 1 | 1 | 0 | −1 |
| row 18 (sawrunner) | **1** | 1 | 1 | 1 | 1 | 1 | 0 |

(Every class fears class 18. Class-7 monsters "dislike" class-11 `monster_generic`, so they may attack scripted props they see.)

### 3.2 Story monster table (81 maps; direct entities + `monstermaker` children (sum of `monstercount`) + `cof_monster_random_spawn`)

Classify: `m` = returns `m_iClass` if set, else the default shown.

| Class | direct | maker | random | maps | Classify | vs player | Companion policy |
|---|---|---|---|---|---|---|---|
| `monster_slower3` | 13 | 23 | 18 | 14 (c_apartment3, c_avlopp, c_buscity, c_buscity2, c_city, c_hamn, c_park2, c_park3, c_roofs, c_sewer2, c_stad, c_subway2st3, c_trainride2, c_village2) | m/7 | DL | **shoot** |
| `monster_faceless` | 16 | 80 | 14 | 9 (c_apartment3sick, c_apartment4sick, c_asylum1, c_college1dark, c_college2, c_down, c_ending1c–3c) | m/7 | DL | shoot (4 have death triggers) |
| `monster_facelessv` | 0 | 15 | 0 | 3 (c_ending1c–3c) | m/7 | DL | shoot |
| `monster_suicider` | 8 | 9 | 21 | 10 | m/7 | DL | shoot |
| `monster_faster` | 8 | 10 | 10 | 10 | m/7 | DL | shoot (c_apartment5 `faster` has `m_iPlayerReact 4`: always hates) |
| `monster_slower` | 7 | 11 | 0 | 9 | m/7 | DL | shoot |
| `monster_sewmo` | 29 | 5 | 0 | 6 (c_avlopp, c_buscity2, c_city2, c_fsewer1, c_sewer, c_sewer2) | m/7 | DL | shoot |
| `monster_twitcher2` | 24 | 0 | 0 | 3 (c_ending5, 5b, 5c) | 7 fixed | DL | shoot (22 in c_ending5c feed counters `mycunt1–4` on death) |
| `monster_twitcher`/`3`/`4` | 5/7/3 | 0 | 0 | c_ending5b, c_ending5c | 7 fixed | DL | shoot |
| `monster_rcrazy` | 16 | 0 | 0 | 4 (c_asylum1, c_forest4, c_fsewer1, c_subtunnel1) | m/7 | DL | shoot |
| `monster_child` | 4 | 6 | 8 | 8 | m/7 | DL | shoot |
| `monster_baby` | 8 | 3 | 2 | 5 | m/7 | DL | shoot |
| `monster_crazybitch` | 7 | 1 | 3 | 5 | m/7 | DL | shoot |
| `monster_nerd` | 6 | 7 | 4 | 7 | **5 fixed** | **HT** | shoot |
| `monster_krypande` | 4 | 1 | 4 | 6 | m/7 | DL | shoot |
| `monster_crab` | 4 | 3 | 0 | 1 (c_basement) | m/7 | DL | shoot |
| `monster_spitter` | 9 | 0 | 0 | c_trainride, c_trainride2 (sf 128, `target` = breakables) | m/7 | DL | shoot |
| `monster_slowerstuck` | 4 | 0 | 0 | c_city, c_city2 (sf 128/132, stuck) | m/7 | DL | shoot if it threatens |
| `monster_stranger` | 3 | 3 | 0 | c_apartment3sick, c_asylum2, c_park2 | m/7 | DL | shoot (killable; club knock-back, `0x100764F0`) |
| `monster_taller` | 3 | 0 | 1 | c_buscity, c_buscity2 | m/7 | DL | shoot |
| `monster_sawrunner` | 4 | 2 | 0 | c_city2, c_down, c_forest3, c_fsewer1, c_hamn (chases ended by `killtarget`) | **m/18** | DL | shoot allowed (normal TakeDamage); see 3.4 |
| `monster_bosschainsaw` `theboss` | 1 | — | — | c_apartmentsick (death → `multikilled1`, boss bar) | m/7 | DL | **shoot (intended kill)** |
| `monster_bosschainsaw` via maker `horaspawn` → `chainhora` | — | 1 | — | c_start2 (maker keys **`m_iClass 3`, `m_iPlayerReact 1`**; `killtarget` 0.2 s after spawn) | **3** | **AL** | **do not shoot** (scare actor) |
| `monster_doctorboss` | 1 | 0 | 0 | c_attic (death → `multidscene`) | **4 fixed** | **HT** | shoot; TakeDamage `0x1002EB40` ignores damage unless its scripted phase index > 4 (`0x1002D690`) |
| `monster_roofboss` `carcassboss` | 1 | 0 | 0 | c_roofs2 (sf 128, death → `multikill`) | m/7 | DL | shoot when engaged |
| `monster_sewerboss` | 1 | 0 | 0 | c_sewer3 (sf 132, death → `whenIdie`) | m/7 | DL | shoot when engaged |
| `monster_booksimon` ×3 | 3 | 0 | 0 | c_ending4b; damage forwarded to `justhealth`, own health floored at 1 (`0x10070E74`) | m/7 | DL | shoot (counts toward the fight) |
| `monster_booksimonsledgehammer` `simon` | 1 | 0 | 0 | c_ending4b; forwards to `justhealth`, fires its trigger with **FIRST player** (`0x1002515B`) | m/7 | DL | shoot |
| `monster_generic` | 9 | 0 | 0 | c_apartment3 `scareyoue` (ghost, react 1), c_nightmare ×4 (react 1; `wrongm8` scripted), c_ending1c–3c `fagnerd` (strangle attacker, 100 hp, boss bar), c_ending4b `justhealth` (boss health pool, death → `fuckoff`) | **m/11** | **AL** | **do not shoot** |
| `monster_children` | 0 | 0 | 6 | c_buscity | — | — | **never spawns** (no such export); ignore |

Totals: 209 direct monster entities; 198 `monstermaker`s, of which 182 make monsters (181 with `monstercount 1`,
one unlimited `-1`, a `monster_suicider` maker; the other 16 make items/weapons); 91 random spawn points.
No story monster has `SF_MONSTER_PRISONER (16)` or a `m_iClass` key other than the `chainhora` maker; `env_customize`
(`c_start` ×5) sets `m_iClass 0` / `m_iPlayerReact -1` (no change, LIKELY). **CERTAIN** for counts and keys.

### 3.3 Can shooting a monster break the story? (CERTAIN for the map wiring; LIKELY for the outcome)

- **Scripted sequences**: only 4 in story (`c_apartment4 fastermes`, `c_apartment5 fasters2`, `c_nightmare wrongm8s`,
  `c_forest3 sawrunners` aiscripted); **none has a `target`** → killing the actor early only skips a scare.
- **Death triggers** (`TriggerCondition 4`, 34 monsters: 22 twitcher2, 4 faceless, 2 slower, 1 faster, 4 bosses,
  `justhealth`): fire on death whoever the killer is (activator = the monster, HLSDK `FCheckAITrigger`, LIKELY;
  sledgehammer Simon uses FIRST player). Companion kills progress the story exactly like the player's.
- **`monster_generic`**: killing `justhealth` ends the c_ending4b fight at once (`fuckoff`); killing `fagnerd` removes
  the strangle's damage target (the strangle itself is driven by the FIRST player's health, cutscene report) and its
  boss bar; the ghosts are cosmetic. All are class 11 (ally) → never "hostile" by relation anyway.
- **`chainhora`** (c_start2): friendly scare actor removed after 0.2 s.
- **Doctor (c_attic)**: invulnerable outside its exposed phases; walking into the `toncedeath` volumes (`cof_doctorshoot`
  → `cof_killplayer`) kills — the engine moments layer already limits that to the toucher.
- **Bosses with `spawnflags & 128`** (roofboss, sewerboss, spitters, slowerstuck): HLSDK `SF_MONSTER_WAIT_FOR_SCRIPT`;
  shooting them early can wake them before their intro (LIKELY cosmetic). Prefer engaging only monsters whose
  `m_hEnemy` is a player or that are close to a player.
- **Stray bullets**: 13 story `func_breakable`s fire targets when broken (c_buscity2 `cunter1/2`, c_city `removewalle`,
  c_hamn `ewwe`/`cunter`) — same as a player's stray shots.

### 3.4 Engine-side "alive and hostile" test (recipe)

Static, no DLL calls (all fields CERTAIN):
1. `SV_IsValidEdict(e)`, not `FL_KILLME`, `e->v.flags & FL_MONSTER`, classname starts with `monster_`.
2. Denylist: `monster_generic`, `monster_generic_dead`, `monster_furniture`, `monster_target`, `monster_cine*`,
   `monster_hevsuit_dead` (only `monster_generic` occurs in story).
3. Alive: `deadflag == DEAD_NO` **and** `takedamage == DAMAGE_AIM (2)` (corpses are `DAMAGE_YES` with health > 0) and `health > 0`.
4. Visible: `!(effects & EF_NODRAW)`, and not (`rendermode != 0 && renderamt == 0`).
5. Private data (size ≥ 0x358): `react = pv[0x354]`: `1` → not hostile; `4` → hostile; `2/3` → hostile only if
   `pv[0x2EC] & 1`. `cls = pv[0x350] ? pv[0x350] : default` (default per class from 3.2; the fixed-class ones —
   doctorboss 4, nerd 5, twitchers 7 — ignore `m_iClass`). Hostile iff `iEnemy[cls][2] > 0` (read the row from
   `base + 0x217118`, 19 ints per row).
6. Priority: `pv[0x184]` EHANDLE names a player edict with matching serial → engaged now; target these first.
7. Optional: skip `spawnflags & 0x10` (prisoner) and treat `spawnflags & 0x80` + no enemy as "not yet active".

Exact alternative: call `IRelationship` (vtable `+0x138`, thiscall, arg = a player's CBaseEntity*) and treat `> 0` as
hostile; it covers `m_iPlayerReact`/provoked automatically. Validate that `classname` starts with `monster_` and the
vtable pointer lies in hl.dll's `.rdata` before calling.

---

## 4. Damage between players

### 4.1 Rules — CERTAIN

- `CBasePlayer::TakeDamage` (`0x100F6030`, vtable `+0x64`): returns 0 if `b_GoingThroughADoor (+0x1EA4)`, if not
  alive, or if **`rules->FPlayerCanTakeDamage(this, attacker)`** (`+0x48`, call `0x100F60C7`) is false.
- MP `FPlayerCanTakeDamage` (`0x100C8700`): `if (attacker->IsPlayer() && !(victim->pev->flags & FL_FAKECLIENT)) return 0; return 1;`
  → **no player-to-player damage** from bullets, melee, or anything whose `pevAttacker` is a player, **unless the victim
  edict has `FL_FAKECLIENT`**. Attacker type does not matter (a bot cannot hurt the human either).
- `mp_friendlyfire` (cvar_t `0x10214638`) is read only by `CHalfLifeTeamplay::FPlayerCanTakeDamage` `0x10106C70`;
  the teamplay ctor `0x10106630` has no caller — InstallGameRules (`0x100AF710`) builds only SP (deathmatch 0) or MP.
  **mp_friendlyfire has no effect.**
- World/monster/trigger damage (attacker not a player) applies to bots and humans alike.
- Effects of a refused hit (blood decals/sprites from TraceAttack) still appear — LIKELY (HLSDK order).

### 4.2 Monster damage scaling (NEW) — CERTAIN

`0x100F60DC–0x100F6133`: in MP, when the attacker has `FL_MONSTER` and `[0x10222F14] == 0` (a mode flag set at
`0x100A597F`, reset per map at `0x10120EF2`; 0 in story, LIKELY), **damage × 1.2 × (1 + 0.2 · CountParticipants())**.
Solo human 1.44×; human + bot 1.68×; two humans 1.68×; two humans + bot 1.92×. The bot counts because `fuser4 == 0`.
Setting the bot's `fuser4 = 1` would remove it from the scaling **and** from the team-wipe survivor test (a dead human
with a living bot would then get "Game over"), so keep it a participant unless the engine owns the wipe.

---

## 5. Summary table

| Item | Who is affected | Hazard | Recommended engine treatment |
|---|---|---|---|
| Spawn clears `FL_FAKECLIENT` (`0x100F414B`) | the bot | STUCK slot: bot not dropped at level change; ghost `cs_connected` slot per map; extra bots | drop on `cl->flags & FCL_FAKECLIENT` in `SV_InactivateClients` (or drop bots before it) |
| Stale private data on slot reuse (`GetClassPtr 0x100125E0`) | bot in a reused slot | DESYNC (stale player state) | `SV_InitEdict` after `SV_FakeConnect`, restore flags/netname/colormap |
| Participant counting (`fuser4`) | all | none (bot blocks DLL game over); +17 % monster damage | keep; drop a bot only while a human lives |
| MP ClientDisconnected game over | all | SOFT game over when dropping the bot with all humans dead | guard the drop / clear `+0x4E8/+0x4F0/+0x1F24` |
| CPR selection by `PlayerUse` | bot | STUCK-PLAYER / item theft if `+use` picks a keypad/computer/door/item | direct `Use` call (2.8 B) + held `IN_USE` |
| Reviver dies mid-CPR | downed player | STUCK-PLAYER (no CPR until respawn) | clear stale `+0x26A0` each frame |
| Dead bot without usercmds | bot | not revivable, never completes revive | always run bot usercmds |
| Monster targeting | all players equally | none | hostility test 3.4; prefer `m_hEnemy == player` |
| `monster_generic`, `chainhora` | story | broken boss fight / scenes if shot | denylist + relation test |
| Player damage | humans/bot | none (FL_FAKECLIENT absent) | do **not** re-set `FL_FAKECLIENT` on the bot edict |
| `cof_killplayer` | every slot | kills the bot too | engine already converts fall floors to "toucher only" |
| Spawn point | bot | none (no DM spots, no spot targets in story) | party placement after spawn |

## 6. Open questions only a live test can answer

1. After the `SV_InactivateClients` fix, does the bot drop cleanly at a story transition (ClientDisconnect runs while
   the old world is still alive; "has left the game" line) and is it re-added on the new map without a stale
   old-level save entry for its slot edict (the old level's save is written before `SV_InactivateClients`)?
2. Without the fix (current code): confirm the ghost `cs_connected` slot and the second bot after one map change.
3. Does `pfnSetClientKeyValue` work on a fake client (the bot's police model shows for the human)?
4. Does pmove allow the reviver to move while `fuser3 == 2` (CPR), and does a dead bot's corpse fall and get
   `FL_ONGROUND` from its own usercmds (needed for the human to CPR it)?
5. Natural-input CPR by the bot: is the corpse reliably the `PlayerUse` winner from 40–60 u at a downward aim, or do
   nearby items/doors steal the press? Is the direct `Use` call (2.8 B) completed by the held `IN_USE` as predicted?
6. A human reviving the bot after a cutscene: is the bot's view/freeze state sane (`pfnSetView` is not sent to bots,
   but FL_FROZEN/FL_NOTARGET/takedamage 0 from `trigger_camera` are set on all players)?
7. Does the bot's `fixangle = 1` from the CPR snap need re-asserting (the companion clears `fixangle` every frame; the
   CPR yaw is then overwritten by the brain's view)?
8. With a bot touching a `trigger_changelevel` or a story `trigger_once` before the human: does the engine exit hold
   show partner-wait UI, and do cutscenes/teleports start early (FIRST-player effects land on the host, since the bot's
   usercmds are not "sender"-scoped, `sv_pmove.c:1059`)? Consider filtering story-trigger touches by fake clients.
9. c_ending4b: does `monster_booksimon`'s random-slot pick target the bot often enough to matter; does the bot's damage
   reach `justhealth` and end the fight normally?
10. Does monster damage feel noticeably higher with the bot present (×1.68 vs ×1.44)?
