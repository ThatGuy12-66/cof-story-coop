# A: cutscenes, cameras and screen/view effects (static RE of retail hl.dll)

Binary: `runtimes/enhanced/cryoffear/cl_dlls/hl.dll` (stock, base 0x10000000). Static only (capstone linear
sweep + vtable/xref reads); nothing launched or modified. Engine-behaviour statements cite the local Xash
source under `upstream/cof-enhanced/xash3d-fwgs-…/engine/server/`. Story-map counts are over the 81
`story_maps.json` maps via `cofcoop/bsp.py`. Tags: **CERTAIN** = read from machine code (or engine source),
**LIKELY** = inferred, **UNKNOWN** = not traced.

Naming used below: P = CBasePlayer*, `pev+x` = CoF-shifted entvars offset. FIRST = `UTIL_FindEntityByClassname(NULL,"player")`
(0x10118460). "Slot 1" = `CBaseEntity::Instance(PEntityOfEntIndex(1))` (host on a listen server; NOT affected by the
engine's FIRST-player reordering). Game rules in story co-op are the MP rules, so `g_pGameRules->IsMultiplayer()`
(vtable+0x20 of `[0x10222EE4]`, MP impl 0x100C90B0 returns 1) is **true**; several branches below depend on that.

## 0. Shared machinery discovered on the way (needed to read the class entries)

| Item | VA / offset | Meaning | Conf. |
|---|---|---|---|
| SET_VIEW wrapper | 0x10117180 `(client, viewent)` | If client is a player: MSG_ONE `ModelAttach`(short -1, byte -1) to client, `P+0x2038 = ENTINDEX(viewent)` (m_iViewEntIndex), then `g_engfuncs.pfnSetView` `[0x102230B8]`. **Every** SetView in hl.dll goes through it (23 call sites). | CERTAIN |
| Player camera handle | `P+0x2348` EHANDLE (m_hCamera) | Set to the trigger_camera on start, cleared on end; ClientDisconnect (0x1001CC90) clears the leaver's own handle only. Not touched by CBasePlayer::Spawn. | CERTAIN |
| Player camera-model ptr / handle | `P+0x5AC` (raw CBaseEntity*), `P+0x388` EHANDLE | Per-player `camera_model` entity used by cof_mdlcutscene / cof_strangle / ladder classes; `AttachCam` message attaches the client's view to it. Client cmds `attack1pressed/…released` (0x1001A0D6…) set `pev->button` bits on the **sender's own** `P+0x5AC` model. | CERTAIN |
| IsInCameraView | 0x100E4E00 | `(m_hCamera||P+0x388) && m_iViewEntIndex!=0 && !=self`. Used by client commands to refuse actions during cameras. | CERTAIN |
| EnableControl(BOOL) | 0x100DEFF0 (duplicate body at 0x100DF6A0) | FALSE: `flags|=FL_FROZEN(0x1000)`, velocity=0. TRUE: clears FL_FROZEN **unconditionally**. Both clear `P+0x1ED0/+0x1ECC`. These are the only FL_FROZEN clearers in the DLL. | CERTAIN |
| Spawn resets | CBasePlayer::Spawn 0x100F4090 | `takedamage=DAMAGE_AIM` (0x100F4113), `flags&=FL_PROXY; flags|=FL_CLIENT` (clears FL_FROZEN/FL_NOTARGET), `SET_VIEW(self,self)` (0x100F45A3), `m_fInitHUD=1` (0x100F497B), MP3 dirty=1 (0x100F42D5), `P+0x1EB8=-1` (forces Blur resend). | CERTAIN |
| CPR revive completion | MultiplayerPlayerDeathThink 0x100E9AF1–0x100E9C47 | Sets deadflag=0, MOVETYPE_WALK, health 30, clears FL_ONGROUND, sends messages. Does **not** touch FL_FROZEN, FL_NOTARGET, takedamage, view (SetView) or `m_hCamera`. Not a Spawn, so no InitHUD. | CERTAIN |
| InitHUD camera resync (MP only) | UpdateClientData 0x100F72A2–0x100F7584, inside `if(m_fInitHUD)` | If own `m_hCamera` valid and camera `pev->iuser1!=0`: SET_VIEW(self,camera), FL_NOTARGET, takedamage=0, HUD bits, HeadShield. Else loops other players: first one with an active camera → copies its handle and SET_VIEW(self,camera) — **but writes FL_NOTARGET and takedamage=0 onto the *other* player** (`edi`, 0x100F74B7–0x100F74CC), not the joiner. If no active camera: SET_VIEW(self,self). Never re-applies FL_FROZEN. | CERTAIN |
| InitHUD AttachCam/Blur/BlackWhite resync | 0x100F70F3 (AttachCam from `P+0x388`, -1 if none), 0x100F6F79 (Blur from `P+0x1EBC`), 0x100F77B7 (BlackWhite from `P+0x1EF0` if ≠0) | per-player state re-sent after spawn/load | CERTAIN |
| Per-player HUD state sent on change | PostThink 0x100EAD70 (player vtable +0x250) / UpdateClientData | `P+0x2500` m_iHideHUD → `HideWeapon`(byte) when ≠ `P+0x2504`; `P+0x1EB4` black bars → `BlackBars`(byte) when changed (0x100EE829); `P+0x1EBC` blur → `Blur`(byte) when ≠ `P+0x1EB8` (0x100EEBF6). So these effects are **state**, not one-shot messages: writing the field makes the DLL send the message. | CERTAIN |
| MP late-join into an mdl cutscene | MP PlayerSpawn 0x100C948D–0x100C94BB; PostThink 0x100EB324–0x100EB3DD | If any player has `P+0x5AC` (model iuser4≠1): joiner gets `P+0x3C8=1`; every PostThink while set: ScreenFade(black, hold 1.0, alpha 255, flags 5 = OUT|STAYOUT), EnableControl(FALSE), HUD|=9, FL_NOTARGET, ProFont "Please wait until cutscene has finished." every 5 s. Cleared by the cutscene end (0x1009F584). No fade-in is sent by the clear. | CERTAIN (fade left black: LIKELY) |
| Disconnected clients | engine `SV_DropClient` (sv_client.c:633) | Edict is **not** freed and its serialnumber is not bumped; private data survives until the slot is reused (`SV_InitEdict` on fresh entry, sv_client.c:1404, also does not bump the serial). So EHANDLEs to a leaver stay "valid" and `UTIL_PlayerByIndex` (0x10119680: `!free` + pvPrivateData) still returns the stale CBasePlayer; `pfnSetView` on it just writes into the dead slot (`SV_ClientFromEdict(…, false)`). | CERTAIN (engine source) |

User-message registry used below (`RegUserMsg` size; global): HeadShield -1 `[0x102265F4]`, IronSights -1 `[0x10226668]`,
CameraZoom -1 `[0x102265CC]`, SkipScene 0 `[0x1022665C]`, AttachCam -1 `[0x1022664C]`, ModelAttach -1 `[0x10226618]`,
VGUIMenu 2 `[0x10226580]`, ViewLock -1 `[0x102265AC]`, GreyFade 2 `[0x1022667C]`, Blur 1 `[0x10226574]`, BlackWhite 1
`[0x102265DC]`, ScreenFade 10 `[0x10226720]`, ScreenShake 6 `[0x1022671C]`, ControlHint -1 `[0x10226674]`, Introdn 3
`[0x102266D0]`, CoFLogo 1 `[0x10226598]`, Chapter -1 `[0x10226658]`, board -1 `[0x10226584]`, PlayMP3 -1 `[0x10226544]`,
Telescope -1 `[0x10226630]`, Scope 1 `[0x102265FC]`, Clothes 7 `[0x10226698]`, BlackBars -1 `[0x10226570]`, HideWeapon 1
`[0x10226510]`, Concuss 2 `[0x102265E8]`. All sends below are `MSG_ONE` (dest 1) to the named player unless stated.

---

### trigger_camera (CTriggerCamera, CoF version)
- **Factory / size / vtable:** 0x10107FB0 / 0x154 / 0x101E4170. Spawn 0x10110B80, KeyValue 0x1010CBC0, Use 0x10113B60,
  Think `FollowTarget` 0x1010AF30 (exported `?FollowTarget@CTriggerCamera@@QAEXXZ`, set at 0x101144F6), Move 0x1010E570.
  0x10110340 is an alternate end routine with **no references** (dead code). CERTAIN
- **Fields:** m_hPlayer +0xEC, m_hTarget +0xF4, m_pentPath +0xFC, m_sPath +0x100, m_flWait +0x104, m_flReturnTime +0x108,
  m_flStopTime +0x10C, m_moveDistance +0x110, m_targetSpeed +0x114, m_initialSpeed +0x118, m_acceleration +0x11C,
  m_deceleration +0x120, m_state +0x124 (HLSDK order). CERTAIN (layout matches HLSDK use).
- **Keys:** `wait`→+0x104, `moveto`→+0x100, `acceleration`→+0x11C, `deceleration`→+0x120, CoF: `m_iszViewEntity`→+0x128,
  `keepblackbars`→+0x12C, `keepnohud`→+0x130, `keepnoise`→+0x134, `zangle`→+0x144 (int), `skipmanager`→+0x148,
  `m_sAttachedModel`→+0x14C, `m_iAttachment`→+0x150; else CBaseDelay (`delay`,`killtarget`). `iuser2` (entvars) also used. CERTAIN
- **Spawn:** MOVETYPE_NOCLIP, SOLID_NOT, renderamt 0, rendermode 2, m_initialSpeed=pev->speed, accel/decel default 500,
  `pev->iuser1=0` (iuser1 is the "camera active" flag read by InitHUD), angles.z=zangle. CERTAIN
- **Spawnflags actually implemented:** 1 PLAYER_POSITION, 2 PLAYER_TARGET, 4 TAKECONTROL (freeze), 0x10 hide HUD bit 8,
  0x20 black bars, 0x40 snap-look (no angular smoothing, 0x1010AFF7), 0x100 HeadShield msg, 0x200 VGUIMenu(0xA6) to the
  activator, 0x400 not skippable, 0x800 detach the player's attached `camera_model` (+0x5AC) and reset zoom. Value-magic:
  `Use(…, value=666.0)` = forced off; `wait=666` = never time out. CERTAIN
- **Use (0x10113B60):**
  1. `if(!ShouldToggle(useType, m_state)) return;` (0x100183A0, HLSDK semantics: USE_ON while on / USE_OFF while off → no-op). CERTAIN
  2. **value == 666.0** (`[0x101A9734]`): forced off — `iuser1=0, m_state=0`, then for every `UTIL_PlayerByIndex(1..maxClients)`:
     if alive → SET_VIEW(p,p), `m_hCamera=NULL`, clear FL_NOTARGET, takedamage=2.0 (DAMAGE_AIM), EnableControl(TRUE);
     for all (alive or not) → HUD bit 8 off (sf 0x10 && !keepnohud), black bars off (sf 0x20 && !keepblackbars), HUD bit 0x10
     off (iuser2), HeadShield(0,0,"",0) (sf 0x100), `m_hCamera=NULL`. Only sender in the DLL: `cof_clothesmenu` close
     (0x100A22A4, USE_OFF,666). CERTAIN
  3. Normal toggle: `m_state=!m_state`. **If it was on → `m_flReturnTime=now; return`** (second Use turns it off at the next
     think, unless wait==666). CERTAIN
  4. Turning on: `pActivator` if `IsPlayer()`, **else slot 1** (`PEntityOfEntIndex(1)` → `+0x7C`, 0x10113D6E). `m_hPlayer=` that.
     If player: `0x100F5D50` (dismount wheelchair `P+0x390`, SetPhysicsKeyValue "chr"). `m_flReturnTime=now+wait`,
     speed setup. If m_hPlayer's active weapon (`P+0x252C`) is in iron sights (`+0x178`): IronSights(0,0,0,0,1) and
     CameraZoom(350,0,0) to m_hPlayer, `pev+0x274=90`. CERTAIN
  5. `m_hTarget = sf&2 ? m_hPlayer : GetNextTarget()`; if NULL → `UTIL_ProFont(m_hPlayer,"Nothing to look at!",5,0)` and
     return **with m_state already 1** (latent toggle desync; no story instance lacks a target). CERTAIN
  6. sf 0x200 → VGUIMenu(byte 0xA6, byte 0) to m_hPlayer (0x100F4050). sf 4 → EnableControl(FALSE) on m_hPlayer. Path /
     stop time / sf 1 position copy (eye origin, -pitch, yaw, zangle, velocity) as HLSDK. CERTAIN
  7. **All-player loop** (two variants: with `m_iszViewEntity` → view = that entity, 0x101140E5; without → view = camera,
     0x10114211). For every `UTIL_PlayerByIndex(i)` **with no IsAlive/IsNetClient filter**: SET_VIEW(p, view),
     `m_hCamera=this`, `pev->iuser1=1`, `p.flags|=FL_NOTARGET (0x80)`, `p.takedamage=0` (godmode), sf 4 →
     EnableControl(FALSE), [no-view-entity variant only: sf 0x800 && `P+0x5AC` && alive → UTIL_Remove(model), clear
     `P+0x388`, AttachCam(short 0, byte 0), CameraZoom(50,0,0), `pev+0x274=90`], sf 0x10 → `P+0x2500|=8`, sf 0x20 →
     `P+0x1EB4=1`, iuser2 → `P+0x2500|=0x10`, sf 0x100 → HeadShield(1,1,"",0), and if morphine active (`P+0x2278`) →
     end morphine for that player (0x100F5C90: sound, **global** cvar `room_type`, Concuss(0,0), blur 0). CERTAIN
  8. If `m_sAttachedModel` resolves and m_hPlayer is a player: ModelAttach(long entindex, byte m_iAttachment) to m_hPlayer
     only. `SET_MODEL(camera, m_hPlayer->pev->model)`, `iuser1=1`, SetThink(FollowTarget), nextthink now, store
     m_hPlayer's v_angle at +0x138, Move(). CERTAIN
- **Think FollowTarget (0x1010AF30):**
  - `if(!m_hPlayer) return;` without rescheduling (HLSDK). m_hPlayer can only become NULL if the edict serial changes,
    which never happens for client edicts (§0), so this is not reachable in practice. CERTAIN (engine) / LIKELY (practice)
  - Skip: `if(g_pGameRules && !IsMultiplayer() && GetAsyncKeyState(VK_RETURN)&0x8000 && !(sf&0x400))` → MSG_ONE SkipScene
    to m_hPlayer. **Reads the server machine's keyboard and is disabled under MP rules** → no skipping in co-op. CERTAIN
  - End when `!m_hTarget || (now > m_flReturnTime && m_flWait != 666)`. CERTAIN
  - End sequence (0x1010B267): `iuser1=0`; fire `pev->message` with **activator = the camera** (CoF addition);
    all-player loop: **if IsAlive** → SET_VIEW(p,p), clear m_hCamera, clear FL_NOTARGET, takedamage=DAMAGE_AIM,
    EnableControl(TRUE); for all → HUD/black-bar/iuser2 bits off (respecting keepnohud/keepblackbars), HeadShield(0) if
    sf 0x100, `m_hCamera=NULL`. Then `SUB_UseTargets(this /*activator = camera*/, USE_TOGGLE, 0)`, avelocity=0,
    `m_state=0`; if `skipmanager` set → `Use(FIRST, FIRST, USE_TOGGLE, 0)` on it. CERTAIN
  - Otherwise track target (sf 0x40 snap, else HLSDK avelocity), zangle, velocity damping when sf 4 not set, re-think, Move(). CERTAIN
- **Move (0x1010E570):** HLSDK path following; path_corner `message` fired with activator = camera. CERTAIN
- **Who is affected:** **all players** (view, godmode, notarget, freeze, HUD). m_hPlayer (activator or slot 1) only gets the
  position/target reference, iron-sight reset, VGUIMenu, wheelchair dismount, ModelAttach, "Nothing to look at!".
- **Player state changed / undone:**
  | change | set at | undone at | dead players |
  |---|---|---|---|
  | view → camera (pfnSetView) + `P+0x2038` | start loop | end loop, alive only | **not undone**; fixed only by Spawn (SET_VIEW self) |
  | FL_FROZEN | start (sf 4), all players | end loop, alive only (EnableControl TRUE) | **not undone**; Spawn resets flags |
  | FL_NOTARGET, takedamage 0 | start, all players | end loop, alive only | **not undone**; Spawn resets |
  | HUD bit 8 / 0x10, black bars, HeadShield | start | end loop, all players | undone |
  | m_hCamera | start | end loop, all players | cleared |
- **Story-map usage:** 311 in 33 maps: c_roofs 52, c_asylum1 32, c_nightmare 28, c_start 25, c_basement 16, c_sewer3 13,
  c_subwaysick2 13, c_apartmentsick 11, c_intro 10, c_ending5c 9, c_trainscene 8, c_attic 7, c_city2 7, c_apartment1 6,
  c_apartment5 6, c_broscene 6, c_apartment3 5, c_ending1c/2c/3c 5 each, c_ending4b 5, c_roofs2 5, c_subway1st1 5,
  c_subway2st1 5, c_city 4, c_ending5 4, c_park3 4, c_subway2st3 3, c_bridge 2, c_sewer2 2, c_ending5b 1, c_forest7 1, c_park 1.
  - Spawnflags: 116 (215×), 1140 (76×), 1108 (7×), 52 (4×), 112/628/3188 (2× each), 28/1044/1136 (1× each). Bits: 4 in
    308/311 (freeze), 0x10 in 311, 0x20 in 302, 0x40 in 305, 0x400 in 87, 0x200 in 2, 0x800 in 2, 8 in 1. **No story camera
    uses sf 1 or 2** (never positioned at / looking at the player), none uses 0x100, `m_iszViewEntity`, `m_sAttachedModel`
    or `skipmanager`. Keys: moveto 42, speed 63, zangle 83, keepnohud 17, keepblackbars 13, delay 16, message 1, iuser2 1.
  - `wait`: 666 on **24** cameras (never time out): c_attic diekam1/2; c_bridge + c_start `clothescamera` (wait 666, fired
    by `cof_clothesmenu` `cameraent`); c_ending1c/2c/3c `statskamera1`, `conkam` (target of `emdlkam` mdlcutscene);
    c_ending4b e5kam1-3, conkam, statskamera1; c_ending5c ejkam1-7, credka1, statskamera1. All others 0.1–357.9 s.
  - Not frozen (sf 4 clear): c_apartmentsick `saw2kam4`, c_roofs2 `rkam4550` (both 112), c_ending5b `kamerahora` (28).
    sf 0x200: c_roofs2 `rkam2`,`rkam3`. sf 0x800: c_subway2st3 `tkam1`,`tkam2`.
  - Root firing sources (through multi_manager/relay chains): trigger_once 234, func_button 26, cof_yesno 16, monster
    TriggerTarget 17 (slower 7, bosschainsaw 4, doctorboss 2, roofboss 2, sewerboss 2), cof_mdlcutscene.target 4,
    cof_strangle.target 3, cof_telephone 3, func_valve 3, cof_weapontrigger 3, cof_clothesmenu 2, another camera's `message` 2.
    Monster/camera-rooted fires reach the camera with a non-player activator → m_hPlayer = slot 1 (host). LIKELY (relies on
    HLSDK multi_manager passing the activator)
- **Co-op hazards (host = slot 1, guest = slot 2):**
  1. **STUCK-PLAYER (CPR revive):** a player who is dead when a camera starts gets SET_VIEW→camera, FL_FROZEN (sf 4,
     308/311 cameras), FL_NOTARGET and takedamage 0; the end loop skips non-alive players. If that player is then CPR-revived
     (0x100E9AF1, no Spawn, no InitHUD) they come back **frozen, invulnerable, untargetable and looking through a dead
     camera**, with nothing in the DLL to release them until a real Spawn (map change / jump-respawn impossible while alive).
     A team wipe also cannot trigger because they are alive and invulnerable. Very plausible in co-op (downed partner,
     survivor walks into a trigger_once). CERTAIN for the code paths, LIKELY for the gameplay result.
  2. **WRONG-PLAYER-VISUAL (split play):** the partner anywhere on the map is frozen, made invulnerable and has their view
     yanked into a cutscene they did not trigger. This is CoF's own MP design (shared cutscenes). Nothing restores a partner's
     own UI view (telescope, keypad, phone): `EnableControl(TRUE)` at the end also clears FL_FROZEN that another system
     (telescope, strangle, padlock, door departure, an engine exit hold) set on that partner. CERTAIN
  3. **DESYNC (respawn / late join during a camera):** InitHUD re-attaches the view but never re-freezes; for a joiner whose
     own m_hCamera is empty the FL_NOTARGET/takedamage writes go to the *other* player (bug, 0x100F74B7). So a respawned or
     reconnected player watches the cutscene while their body is free to move and, for joiners, damageable. CERTAIN
  4. **Early cut-off:** any second USE_TOGGLE while on ends the camera next think (except wait 666). With 308/311 cameras
     freezing everyone and the view on the camera, a partner re-pressing the source func_button (26 cameras) is unlikely;
     the 3 non-freeze cameras are exposed. LIKELY low risk.
  5. **No skip in co-op** (MP gate). The c_intro chain (10 cameras, 9 env_fade, 7 cof_logo) is unskippable. CERTAIN
  6. **Disconnect mid-camera:** harmless — the edict/private data survive (§0), the think finishes, the end loop touches
     the stale player (SetView into a dead slot, no crash). LIKELY
  7. Camera end fires `target`/`message` with activator = the camera; downstream player-requiring entities fall back to
     their own rules (stock behaviour, same in SP). CERTAIN
- **Recommended engine treatment:**
  - No mirroring needed: the DLL already applies the camera to every player.
  - **Camera watchdog (fixes 1 and 3):** each frame, for every spawned client: if `cl->pViewEntity` is a `trigger_camera`
    whose `v.iuser1 == 0` (ended) and the player is alive → `pfnSetView(self)` semantics (also write `P+0x2038 = own index`
    if private data is verified), clear FL_FROZEN|FL_NOTARGET and set `takedamage = 2.0`, unless another engine hold owns
    FL_FROZEN. Conversely, if `pViewEntity` is an active camera (`iuser1==1`) whose spawnflags include 4, keep FL_FROZEN,
    FL_NOTARGET and takedamage 0 asserted on that player (repairs the InitHUD bug). Simplest trigger: on any
    dead→alive transition, run the check once.
  - Re-assert engine-owned FL_FROZEN holds every frame (a camera end clears them).
  - Optionally drop a Use on an *active* camera unless value==666 — not possible without hooking Use; skip.
  - Checkpoints: do not checkpoint while any player's `pViewEntity != self` or FL_FROZEN is set by a camera, or sanitize
    those on restore.
- **Confidence:** behaviour CERTAIN (VAs above); activator-chain counts LIKELY.

### cof_mdlcutscene (CCoFMDLCutscene)
- **Factory / size / vtable:** 0x1008FFE0 / 0x3F4 / 0x101C7C7C. Spawn 0x1009D160, KeyValue 0x10095920, Use 0x100A4670,
  Think `StartNextAnimation` 0x1009F4A0, `GetTargetEnt` 0x10094D30 (spawn think when `message` set: stores the named
  entity's edict at pev+0x298, copied to the camera models). CERTAIN
- **Keys:** `animation1..30` → +0x36C..+0x3E0 (string_t); current index +0x3F0 (min 1), count +0x3EC; `model`, `target`,
  `message`, `iuser4` (1 = single-player-only behaviour), spawnflags. CERTAIN
- **Use (0x100A4670):** requires `pActivator != NULL` and `pev->model`. Owner = **FIRST** player. If `IsMultiplayer() &&
  pev->iuser4 != 1` → loop all players, else FIRST only. Per player: wheelchair dismount; sf 0x20 → SET_VIEW(p,p); sf 1 →
  EnableControl(FALSE); sf 2 → HUD|=8; sf 4 → black bars; sf 0x40 → HUD|=0x10; FL_NOTARGET; `P+0x5AC =
  Create("camera_model", origin, angles, owner=p)` with this model, renderfx 0x46/0xB8; `P+0x388=model`; **MSG_ONE
  AttachCam(short ENTINDEX(model), byte 0)**. Then `pev->owner = FIRST`, SetThink(StartNextAnimation) now. Note: takedamage is
  **not** changed. CERTAIN
- **Think (0x1009F4A0):** returns **without rescheduling** if `pev->owner` has no private data or owner's `P+0x5AC==0`.
  While anims remain: set the next `animationN` sequence on every player's model (MP) and re-think after the owner model's
  sequence length. When done and sf 0x10: for all players (MP; no alive check) clear `P+0x5AC`, `P+0x388`, `P+0x3C8`,
  HUD bit 1, sf1 → EnableControl(TRUE), HUD 8/0x10 off, black bars off, FL_NOTARGET off, UTIL_Remove(model),
  AttachCam(0,0), CameraZoom(50,0,0), `pev+0x274=90`; then `FireTargets(target, owner, owner, USE_TOGGLE)` (activator = owner). CERTAIN
- **Who is affected:** all players (MP); owner = FIRST.
- **Story-map usage:** 22 in 10 maps (c_attic 1, c_ending1/2/3 2 each, c_ending1c/2c/3c 3 each, c_ending4 4, c_ending4b 1,
  c_subway2st3 1). Spawnflags 23 (16×), 55 (2×), 115 (4×); **all have 1 (freeze) and 0x10 (restore at end)**. Model always
  `models/mdl_camera.mdl`. Chains: kambokam1→sukams, emdlkam→conkam (wait-666 camera), c_subway2st3 tkam3cmdl→tkam4.
- **Co-op hazard:** mostly NONE (MP-aware). Edge cases:
  - SOFT-LOCK if the owner's `P+0x5AC` is cleared mid-scene (e.g. a trigger_camera with sf 0x800 runs its per-player
    detach) → think stops; non-owner players stay attached and frozen. Same in SP. CERTAIN (code), LIKELY (reachability low)
  - Late joiner / respawner: black STAYOUT fade + frozen until the end; the end does not fade in (LIKELY black until the
    next env_fade; ending maps usually fade). DESYNC/visual.
  - Players can still take damage while frozen (FL_NOTARGET only). Ending maps; low risk.
- **Recommended engine treatment:** keep FIRST = host for this class (owner must persist; the host cannot leave
  without ending the session). After the cutscene end, send ScreenFade(in) to any player that had `P+0x3C8` set. Nothing else.
- **Confidence:** CERTAIN for the code; LIKELY for the late-join black screen.

### cof_lookat (CCoFLookAt?) and func_asylumlookat
- **cof_lookat:** factory 0x1008FF80 / 0xDC / 0x101C7A00; Spawn 0x1009D110 (invisible point); keys: `fuser1` = duration.
  **Use 0x100A4580:** needs `pActivator != NULL`; **FIRST** player: `P+0x59C..0x5A4 = pev->origin` (look point),
  `P+0x598 = now+fuser1`, `0x100F9140(P, 1, fuser1)` = MSG_ONE **ViewLock(byte 1, coord fuser1)**, `P+0x594 = now+5`.
  PostThink (0x100EC21D) then forces `pev->angles` toward the point with `fixangle=1` every frame until `P+0x598`. Undo:
  automatic timeout. CERTAIN
- **func_asylumlookat:** factory 0x1000FA40 / 0xDC / 0x101A45D4; brush (MOVETYPE_PUSH, SOLID_BSP); **Use 0x10012310 only sets
  `pev->renderfx = 0xA2`** (networked render state, all clients). CERTAIN
- **Story usage:** cof_lookat 1 (c_village3 `lookatit`, fuser1 5, fired by trigger_once→multi_manager `multilook`);
  func_asylumlookat 1 (c_asylum1 `lookyou`, model *232).
- **Hazard:** cof_lookat **WRONG-PLAYER-VISUAL** — only the FIRST player (host) is turned, even if the guest touched the
  trigger; guest unaffected. func_asylumlookat NONE.
- **Treatment:** cof_lookat: FIRST := activator (trigger_once toucher) during the touch, or better run the Use once per
  player with FIRST := each player (Use only writes per-player fields + a MSG_ONE; idempotent). func_asylumlookat: nothing.
- **Confidence:** CERTAIN.

### cof_camerazoom (CCofCameraZoom)
- Factory 0x10090640 / 0xDC / 0x101C6574; Spawn 0x1009D6A0 (default `fuser1=111` when equal to 110.0 const); keys
  `zoomfov`→fuser1, `zoomtime`→fuser2, `returntime`→fuser3. CERTAIN
- **Use 0x100A5F30:** activator must be a player; MSG_ONE **CameraZoom(coord fuser2, byte 0, byte 0)** to the activator,
  activator `pev+0x274 = fuser1` (CoF zoom-FOV slot; 90 = normal), SetThink(ReturnFOV 0x1009AAF0) after fuser3.
  **ReturnFOV:** for **all** players CameraZoom(sf&1 ? fuser2 : 666.0, 0, 0) and `pev+0x274 = 90`. CERTAIN
- **Story usage:** 13 in 11 maps (c_apartment1, c_apartment5 (sf 1), c_asylum1, c_basement, c_forest7, c_nightmare, c_park3,
  c_roofs, c_sewer2, c_start, c_trainscene ×3). Roots: trigger_once (10), cof_telephone (c_asylum1), func_button (c_sewer2).
- **Hazard:** WRONG-PLAYER-VISUAL — zoom only on the activator; the return resets everyone (cancels a partner's own scope /
  iron-sight zoom). Cosmetic.
- **Treatment:** mirror CameraZoom + `pev+0x274` to partners when sent by this Use (message is not unique to this class:
  38 sender functions — filter needs context), or nothing. Low priority.

### cof_greyfade
- Factory 0x1008FD40 / 0xDC / 0x101C8E18; keys `iuser1`, `iuser2` (entvars). **Use 0x100A41D0:** **FIRST** player only →
  MSG_ONE **GreyFade(byte iuser1, byte iuser2)**. No server state; no undo. CERTAIN
- **Story usage:** 1 (c_ending4 `fadeoutbw`, iuser1 1, iuser2 4; trigger_once root).
- **Hazard:** WRONG-PLAYER-VISUAL (guest never gets the grey fade).
- **Treatment:** mirror the GreyFade MSG_ONE to all spawned players (or replay Use with FIRST := each player). GreyFade has
  6 sender functions (team wipe, booksimon, game-over, MP spawn) — key the mirror on this Use or on payload (1,4).

### cof_blur
- Factory 0x10090520 / 0xDC / 0x101C3A70. Keys `strength/fade/rate` are present in all 29 instances but **ignored by
  hl.dll** (base KeyValue 0x1000DBA0 only handles movewith/skill/style). **Use 0x100A5BB0:** activator must be a player →
  toggles `P+0x1EBC` (m_iBlur) 0↔1. PostThink sends **Blur(byte)** when it differs from `P+0x1EB8`; InitHUD resends;
  morphine sets/clears the same field. CERTAIN
- **Story usage:** 29 in 23 maps (mostly endings; c_attic, c_basement, c_down, c_forest6, c_nightmare ×2, c_park3,
  c_sewer3, c_start, c_subway2st1, c_subwaysick1/2). Roots: trigger_once 23, func_button 1, doctorboss 1, no referrer 4.
- **Hazard:** DESYNC — per-activator toggle: only the toucher is blurred; if a later blur entity in the same map is
  triggered by the other player that player is toggled on instead of the first one off (both stay blurred / only one
  clears). Cosmetic, persists until morphine-end or another toggle.
- **Treatment:** after a cof_blur toggle, copy the activator's `P+0x1EBC` to all players (DLL sends Blur itself). Needs a
  context hook (Use is DLL-internal); otherwise leave it.

### cof_blackandwhite
- Factory 0x100904C0 / 0xDC / 0x101C451C; key `iuser1` (clamped 0..255). **Use 0x100A5AF0:** needs `pActivator != NULL`;
  for **all** players MSG_ONE **BlackWhite(byte iuser1)** and `P+0x1EF0 = iuser1` (resent by InitHUD when ≠0). CERTAIN
- **Story usage:** 30 in 14 maps. **Hazard:** NONE. **Treatment:** nothing.

### env_fade (CoF variant)
- Factory 0x10091CC0 / 0xE0 / 0x101C19BC. Keys `duration`→pev+0x1DC, `holdtime`→pev+0x1E0, renderamt, rendercolor.
  CoF adds a fade state +0xDC (1 fading, 2 holding, 0 done) advanced by its Think (vtable +0xE0, 0x100A13E0), read by
  vtable +0x5C (0x10094CB0). **Use 0x100A96E0:** flags = (sf&1 ? 0 : FFADE_OUT) | (sf&2 ? MODULATE) | (sf&8 ? STAYOUT,
  CoF extension); **sf 4 (SF_FADE_ONLYONE)** → `UTIL_ScreenFade(activator…)` only if `activator->IsNetClient()`; else
  `UTIL_ScreenFadeAll` (0x10119C50 → per player MSG_ONE **ScreenFade(short dur, short hold, short flags, byte r,g,b,a)**);
  then fires targets with activator = the fade. CERTAIN
- **Story usage:** 249 in 45 maps; spawnflags 0 (118), 1 (129), 8 (2: c_roofs/c_roofs2 `fadefall`). **No story instance
  uses ONLYONE.** **Hazard:** NONE (late joiners miss an in-progress STAYOUT, cosmetic). **Treatment:** nothing.

### env_shake
- Factory 0x10092140 / 0xE0 / 0x101C187C; keys amplitude→pev+0x148, frequency→+0x1E0, duration→+0x1DC, radius→+0x1E4
  (Spawn zeroes radius for sf 1). **Use 0x100A9AD0** → `UTIL_ScreenShake` 0x10119DE0: loops all players, on-ground and
  within radius (0 = everyone) → MSG_ONE **ScreenShake(short amp, short dur, short freq)**. CERTAIN
- **Story usage:** 5 (c_asylumb, c_bridge, c_sewer3, c_trainride, c_trainride2; 4 with sf 1). **Hazard:** NONE.

### cof_simonspeak
- Factory 0x10090160 / 0xDC / 0x101C8950; uses `noise`, `iuser1` (channel), `fuser1` (volume). **Use 0x100A4C30:**
  activator must be a player → `EMIT_SOUND_DYN(activator, iuser1, noise, fuser1, ATTN 0.8, 0, 100)` (positional, all
  clients in range hear it). CERTAIN
- **Story usage:** 8 (c_ending1-4 `momvoice`, `cancelvoice`; trigger_once roots). **Hazard:** WRONG-PLAYER-VISUAL (audio
  follows the activator; a distant partner may not hear). **Treatment:** nothing (or duplicate the emit on partners).

### cof_hint
- Factory 0x10091060 / 0xDC / 0x101C5FEC; key `message`. **Use 0x100A7600:** activator player, `message` set,
  `sv_disablehints==0` → `0x100F3D90(P, message)`: **returns immediately when `IsMultiplayer()`** (0x100F3DBC–0x100F3DC7).
  In SP: per-player dedupe list `P+0x514[32]`, MSG_ONE **ControlHint(string key, string hints-file path by
  cof_subtitlelanguage)**. CERTAIN
- **Story usage:** 11 (c_start 8, c_start2 1, c_apartment1 1, c_basement 1; trigger_once roots).
- **Hazard:** none for soft-lock; hints simply never appear in co-op. **Treatment:** optional: engine sends ControlHint
  itself to each player (payload above). Cosmetic.

### cof_strangle (CCoFStrangle)
- Factory 0x100766F0 / 0xE8 / 0x101BAEC8; Spawn 0x10076830; keys `message` (attacker entity → m_hTarget +0xE0),
  `target`. **Use 0x10076D10:** needs `pActivator != NULL`; **FIRST** player: unduck, `+0xDC = FIRST`,
  **EnableControl(FALSE)**, `P+0x5AC = Create("camera_model")` with `models/cutscene/strangle_simon.mdl`, `P+0x388`,
  sound, sequence `strangle_start1`, **AttachCam(short model, byte 0)** to FIRST, owner=FIRST, think 0.1 s. CERTAIN
- **Think StrangleThink 0x10076880:** start1 → start2 → state 3; then each time FIRST's **camera model** has
  `pev->button & (IN_ATTACK|IN_ATTACK2)` (set only by FIRST's own `attack1pressed`/`attack2pressed` client commands):
  random strangle anim + sound, `m_hTarget->TakeDamage(25)`, FIRST health -= 25; when FIRST health ≤ 1 and `target` set:
  `strangle_death`, health=1, `FireTargets(target, FIRST, FIRST)`, SetThink(NULL), UTIL_Remove(self). **No unfreeze, no
  AttachCam(0)** (ending continues to credits). CERTAIN
- **Story usage:** 3 (c_ending1c/2c/3c `strangle`, message `fagnerd`, target `multisdeath`; root: game_counter
  `countgate` fed by four `multivalve*` multi_managers ← func_valve).
- **Hazard:** WRONG-PLAYER-VISUAL + soft-lock risk: whoever turned the last valve, the **host** is grabbed and only the
  host's input can finish it; the guest is free and outside the scene. If the host is AFK the ending never advances
  (SOFT-LOCK until host acts). The guest-turns-valve case mis-targets. LIKELY
- **Treatment:** FIRST := activator during this Use is not available from the valve chain without context, so keep host;
  optionally mirror to partners: EnableControl(FALSE)/FL_FROZEN + MSG_ONE AttachCam(short host-model index, byte 0) so
  they watch (the ending changes level afterwards, so no restore is needed).

### cof_telescope and cof_telescope_camera
- **cof_telescope:** factory 0x1007D160 / 0xF8 / 0x101BC660; Spawn 0x1007D380; ObjectCaps 0x1007D1D0 (0 unless movewith);
  key `message` = telescope camera. **Use 0x1007D430:** activator must be a player.
  - If `pev->frags != 0 && pev->health == 0` (already used): ProFont "Hmm... The board said %i..." with the
    **activator's** `P+0x2040` keypad number; return.
  - `m_hUser (+0xDC) = activator` (**overwritten every Use**); resolve camera (+0xE4, angles +0xEC); first time ProFont
    "Press USE or MOUSE1 to return, MOUSE to rotate".
  - Toggle `pev->health` 0↔1. On: to m_hUser CameraZoom(350,1,1), Scope(2), `pev+0x274=35`, **EnableControl(FALSE) on the
    activator**, `P+0x370 (m_hTelescope) = this`, **Telescope(short cam index, short ang x,y,z)** to activator. Off:
    CameraZoom(350,0,1), Scope(0), FOV 90, `P+0x370=NULL`, **EnableControl(TRUE) on the activator**, Telescope(-1, angles). CERTAIN
  - Exit path: client command `telescopecancel` (0x1001C150) → if sender's `m_hTelescope` valid **and telescope
    health != 0** → `telescope->Use(sender)`. Reading the board: a client command (0x1001C6D4) sends a code AddNote and sets
    `frags=1` when the sender's telescope is active. CERTAIN
- **cof_telescope_camera:** factory 0x1007D100 / 0x368 / 0x101BC7D8; Spawn 0x1007D2E0 (null sprite, MOVETYPE_NOCLIP);
  no Use (base). Passive anchor for the client-side Telescope view. CERTAIN
- **Story usage:** cof_telescope 1 (c_asylum1 `telescope`, via func_button sf 33 → multi_manager `telepenis`);
  cof_telescope_camera 2 (c_asylum1 `telescope_camera`; c_asylumb `deadfidafv`, used by a cof_puzzlebarbutton).
- **Hazard: STUCK-PLAYER.** P1 uses the telescope (P1 frozen, scoped). P2 presses the same button → `m_hUser=P2`,
  health 1→0 → the "off" branch unfreezes and resets **P2** (who was never in it); P1 stays FL_FROZEN with Scope/zoom and
  `P1+0x370` still set; P1's `telescopecancel` is ignored because health==0. P1 is released only by a later Use with P1 as
  activator (P1 is frozen and looking through the scope). CERTAIN (code), LIKELY (gameplay).
- **Treatment:** engine watchdog: for each player with a verified `P+0x370` handle to a cof_telescope whose `pev->health
  == 0`, release them: clear FL_FROZEN, MSG_ONE Scope(0), CameraZoom(350,0,1), Telescope(short -1, short 0,0,0),
  `pev+0x274=90`, clear `P+0x370`. Alternatively block the second user (needs a Use hook; vtable-slot wrap = last resort).

### cof_introduction
- Factory 0x1008FE00 / 0xDC / 0x101CA098. **Use 0x100A4240:** **MSG_ALL** (dest 2) **Introdn(byte iuser1, byte iuser2,
  byte IsMultiplayer())**. CERTAIN
- **Story usage:** 1 (c_intro `introductiontxt`, iuser2 1, iuser3 1). **Hazard:** NONE. **Treatment:** nothing.

### cof_logo
- Factory 0x10090DC0 / 0xDC / 0x101C5910; key `frags`. **Use 0x100A7370:** activator must be a player → MSG_ONE
  **CoFLogo(byte (int)frags)** to the activator only. **This is the only CoFLogo sender in the DLL.** CERTAIN
- **Story usage:** 7 (c_intro `credits1..7`, via trigger_once → `multiintroduct` → `credits`).
- **Hazard:** WRONG-PLAYER-VISUAL — the partner misses the intro logos/credits.
- **Treatment:** mirror every MSG_ONE CoFLogo to all other spawned players (safe: unique sender).

### cof_chapter
- Factory 0x100906A0 / 0xDC / 0x101C7580; keys `message`, `noise`. **Use 0x100A5FF0:** activator must be a player (else
  nothing); then for **all** players MSG_ONE **Chapter(string message, string noise|NULL)**. CERTAIN
- **Story usage:** 10 (c_attic, c_basement, c_bridge, c_ending1b/2b/3b, c_park3, c_sewer3, c_start, c_subway2st1).
  c_attic's chain is rooted in `monster_doctorboss` TriggerTarget, so its activator is a monster and the chapter card
  never shows (also in SP). LIKELY
- **Hazard:** NONE. **Treatment:** nothing.

### cof_billboard
- Factory 0x101077D0 / 0xE0 / 0x101E494C; keys `message` (tga), `text` (→ +0xDC), `noise`. **Use 0x10113640:** activator
  player → MSG_ONE **board(string tga)**, ProFont(text, 5) and EMIT_SOUND(noise, chan 2) on the activator. CERTAIN
- **Story usage:** 20 in 10 maps, all fired by func_button. **Hazard:** NONE (deliberate per-reader UI). **Treatment:** nothing.

### cof_developer_commentary (CCoFDevCommentary)
- Factory 0x1002C240 / 0x100 / 0x101ACA34; Spawn 0x1002C850 (model, think DevThink 0x1002C2A0, use DevUse 0x1002C4B0).
- DevThink removes the node unless `dev_commentary` cvar ≠0 (or sf 1) and FIRST player owns inventory flag 0x400000.
  **DevUse:** activator player → owner, MSG_ONE PlayMP3(0,1,0,noise), server-side `CVarSetFloat("volume", volume*k)`
  (**process-global cvar = the listen host's audio**), HUD 8, black bars, FL_NOTARGET, Chapter(commentator line).
  DevThink ends on **`GetAsyncKeyState(VK_RETURN)` on the server machine** or timeout and restores the listener. CERTAIN
- **Story usage:** 85 in 50 maps (dormant unless commentary is enabled). **Hazard:** WRONG-PLAYER (host volume ducked
  and only the host's Enter key skips a guest's commentary). **Treatment:** keep `dev_commentary 0` in co-op.

### scripted_sequence
- Factory 0x100FA1A0 / 0x3C0 / 0x101DDB68. Story instances (3) all drive monsters: c_apartment4 `fasterme`,
  c_apartment5 `faster` (monster_faster), c_nightmare `wrongm8` (monster_generic). No player access. **Hazard:** NONE.

### cof_killmp3, CBasePlayer::PlayMP3 and the music entities
- **PlayMP3 0x100EA5C0** `(string_t name, BOOL loop, float fade)`: per-player state only — `P+0x2310` name, `P+0x231C` loop,
  `P+0x2320` fade, `P+0x2314` dirty. **StopMP3 0x100EA600**(fade): name 0, `P+0x2318` fade, dirty. PostThink (0x100EB83A
  and 0x100EF0B0) sends MSG_ONE **PlayMP3(byte loop, byte 1, coord fade, string name)** or stop **PlayMP3(0, 0, coord
  fade, "nothing")** to that player and clears dirty. Spawn sets dirty, so music is re-sent per player. CERTAIN
- **Music triggers** (`ambient_fmodstream`, `trigger_mp3`, `trigger_mp3audio` share Use 0x101148B0): if `IsMultiplayer()` →
  `sv_coop_music = message`, globals fade `[0x10222F1C]`/loop `[0x10222F18]`, and **PlayMP3 on every player** (unless the
  booksimon override `[0x10222F20]`); SP → activator or slot 1. MP PlayerSpawn (0x100C9381) replays `sv_coop_music` to
  joiners. CERTAIN
- **cof_killmp3** (factory 0x10090C40 / 0x368 / 0x101C56C8, key `frags` = fade): **Use 0x100A7220** needs a FIRST player
  to exist; MP → `sv_coop_music=""`, all players StopMP3(frags); SP → FIRST only. CERTAIN
- **Story usage:** ambient_fmodstream 120 in 53 maps; cof_killmp3 28 in 19 maps; trigger_mp3/trigger_mp3audio 0.
- **Hazard:** NONE (music is per player but every player gets it in MP; who hears it = everyone). **Treatment:** nothing.

### trigger_sound
- Factory 0x100FC890 / 0xF4 / 0x101DE84C; keys `roomtype`→+0xEC, `master`→+0xF0. Touch is the vtable override
  (+0xE4, 0x100FFEF0): master check; toucher must be a player not already on this trigger (`P+0x22B8`); stores room
  type (`P+0x22BC`, `P+0x2284`) and sends MSG_ONE **svc_roomtype (0x25), short roomtype** to the toucher (skipped while
  the morphine state holds it); fires targets with the toucher. CERTAIN
- **Story usage:** 121 in 26 maps. **Hazard:** NONE (per-toucher DSP is correct). Note: morphine end (0x100F5C90) writes
  the global `room_type` cvar from `P+0x2284`, affecting only the host's local audio. **Treatment:** nothing.

---

## Summary table

| Class | Affected | Hazard | Treatment |
|---|---|---|---|
| trigger_camera | all players (view, freeze, godmode, HUD); m_hPlayer = activator or slot 1 | **STUCK-PLAYER** (dead at start + CPR revive: frozen, invulnerable, stale camera view); DESYNC on respawn/join (no freeze, notarget bug); split-play partner yanked (by design); end unfreezes other systems' freezes | Engine camera watchdog (release when viewent camera `iuser1==0`; assert freeze/notarget/takedamage while active); re-assert engine holds each frame; no mirroring needed |
| cof_mdlcutscene | all players (MP), owner = FIRST | Edge SOFT-LOCK if owner's model cleared; late joiner left black | Keep owner = host; fade-in late joiners at end |
| cof_lookat | FIRST | WRONG-PLAYER-VISUAL | FIRST := activator, or replay Use per player |
| func_asylumlookat | world (renderfx) | NONE | nothing |
| cof_camerazoom | activator; return = all | WRONG-PLAYER-VISUAL (cosmetic) | optional mirror CameraZoom + FOV |
| cof_greyfade | FIRST | WRONG-PLAYER-VISUAL | mirror GreyFade(iuser1,iuser2) to all |
| cof_blur | activator (toggle) | DESYNC (cosmetic) | sync `P+0x1EBC` from activator to all (needs context) |
| cof_blackandwhite | all | NONE | nothing |
| env_fade | all (no ONLYONE in story) | NONE | nothing |
| env_shake | all in radius, on ground | NONE | nothing |
| cof_simonspeak | activator (positional sound) | cosmetic | nothing |
| cof_hint | activator; **disabled in MP** | none (hints lost) | optional engine ControlHint |
| cof_strangle | FIRST | WRONG-PLAYER-VISUAL; host-only input (SOFT-LOCK if host idle) | keep host; optionally mirror freeze + AttachCam to partners |
| cof_telescope | activator, user handle overwritten | **STUCK-PLAYER** (second user releases the wrong player) | Watchdog on `P+0x370` → release when telescope health==0 |
| cof_telescope_camera | none | NONE | nothing |
| cof_introduction | MSG_ALL | NONE | nothing |
| cof_logo | activator | WRONG-PLAYER-VISUAL | mirror CoFLogo (unique sender) |
| cof_chapter | all (needs player activator) | NONE | nothing |
| cof_billboard | activator | NONE | nothing |
| cof_developer_commentary | activator; host volume + host Enter key | WRONG-PLAYER (dormant) | keep dev_commentary 0 |
| scripted_sequence | monsters | NONE | nothing |
| cof_killmp3 / PlayMP3 / ambient_fmodstream | all (MP), per-player state | NONE | nothing |
| trigger_sound | toucher | NONE | nothing |

## Open questions only a live test can answer

1. CPR-revive a partner who was dead during a whole trigger_camera scene: are they frozen, invulnerable and stuck on
   the camera view as predicted? Does anything client-side (death cam, HUD) mask it?
2. Respawn (jump) and reconnect during an active camera: does the client show the camera with the body free to move,
   and does the other player really get double-applied notarget/godmode (harmless) while the joiner is damageable?
3. Can a FL_FROZEN player in a camera view still trigger `+use` on a func_button (affects early camera cut-off and the
   telescope recovery)?
4. Telescope: host scoped, guest presses the telescope button: is the host stuck in scope? Does any client key
   (USE/MOUSE1) send something other than `telescopecancel`?
5. Late joiner during an mdl cutscene: does the screen stay black after the cutscene ends?
6. What does VGUIMenu 0xA6 (c_roofs2 `rkam2`/`rkam3`, sent to the activator only) open or close on the client?
7. With the MP rules active, does the c_intro sequence run end to end for both clients (no skip)? Does the guest get the
   intro logos (CoFLogo goes to the toucher only)?
8. cof_strangle with the guest having turned the last valve: is the host grabbed, and does the ending stall until the
   host presses attack?
