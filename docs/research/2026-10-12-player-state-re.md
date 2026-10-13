# C: player-state, puzzle and vehicle entities (static RE of retail hl.dll)

Binary: `runtimes/enhanced/cryoffear/cl_dlls/hl.dll` (stock, base 0x10000000), with `client.dll` from the
same folder checked for one UI path. Static only: capstone/pefile through `scratchpad/re/hlre.py`,
plus helper scripts in `scratchpad/re/tmp-C/`. Story counts come from the 81 maps in `story_maps.json`,
read with `cofcoop/bsp.py`. "Fired by" was traced by walking references backwards through
`multi_manager`/`trigger_relay` (`tmp-C/roots.py`, `tmp-C/chain.py`).

Conventions are the same as the brief: `P+x` is a CBasePlayer offset; `pev+x` uses the shifted entvars.
These extra entvars offsets were confirmed while doing this pass: rendermode 0x14C, renderamt 0x150,
health 0x164, frags 0x168, takedamage 0x170, impulse 0x188, speed 0x1FC, mins.z 0xE4, fixangle 0xA0,
v_angle 0x74, angles 0x50, velocity 0x20, basevelocity 0x2C.
FL_GODMODE = 0x40, FL_NOTARGET = 0x80, FL_ONGROUND = 0x200, FL_FROZEN = 0x1000.
In this file "CERTAIN" means the claim was read from machine code.

## 0. Cross-cutting facts found on the way (they matter for several classes)

- **Multi_manager keeps the activator.** It is the Spirit-of-HL version: Use stores `m_hActivator`
  at +0x184 (0x1010E3F8), and ManagerThink fires every target with that activator (0x1010DC74..0x1010DEC0).
  `FireTargets` (0x10101FF0) never substitutes a player; it is also Spirit-style (`+`/`-` prefixes and `*locus`).
  So an activator-only entity reached through a multi_manager gets the original toucher or presser.
  A delayed multi_manager fires from its own think, outside any client usercmd. CERTAIN.
- **Monster death triggers fire with the monster as activator.** The generic AI trigger at 0x100BDBC7
  calls `FireTargets(m_iszTriggerTarget /*+0x334*/, this, this)`. The only other reader of +0x334 is
  `monster_booksimonsledgehammer` TakeDamage (0x10024F70, vtable 0x101AACB8 slot +0x64).
  It forwards damage to the "health holder" named in its `message`, and when the hit is lethal it fires that holder's
  TriggerTarget with the **FIRST player** as activator (0x10025150..0x100251B2). CERTAIN.
- **`monster_doctorboss` fires its `target` with the FIRST player** (0x1002D787..0x1002D7CB). CERTAIN.
- **`cof_strangle` fires its `target` with its stored player** (`this+0xDC`, 0x10076CB4..0x10076CD4). An earlier pass
  found that this stored player is the FIRST player. CERTAIN for the firing; LIKELY for FIRST.
- **CoF `trigger_camera` Use (0x10113B60)** behaves as follows. CERTAIN.
  - **ON:** it picks the activator if that is a player, else edict 1 (0x10113D54..0x10113D92).
    It then calls **`StopWheelchair` on that player** (0x10113DB7) and, with SF 4, `EnableControl(FALSE)` (0x10113F4D).
  - **OFF with value 666.0** (`[0x101A9734]`): it loops over **every** client (0x10113BE0..0x10113D14).
    For each alive one it calls `SetView(self)` (0x10117180), clears `P+0x2348` and FL_NOTARGET,
    sets takedamage = 2 and calls `EnableControl(TRUE)`.
- **Client commands `freeze <code>` / `unfreeze <code>`** (0x10019516 / 0x100195E5) freeze or unfreeze the **sender**.
  The client UIs send them themselves (`client.dll` strings `freeze 1024`, `unfreeze 1024`, `unfreeze 666`). CERTAIN.
- **Raw `CBasePlayer*` pointers kept by entities:** ladder manager +0x114, wheelchair +0x384, and player `P+0x5AC`
  (the camera model). The engine frees and reallocates a client's private data when a new connection takes the slot:
  `SV_InitEdict` at `sv_client.c:1404` calls `SV_FreePrivateData`. A pointer kept across a disconnect
  followed by a reconnect into the same slot therefore dangles. CERTAIN for the engine path; LIKELY for a crash.
- **Retail SetView wrapper** 0x10117180 `(ent, viewent)`:
  - sends `ModelAttach(-1,-1)` to the player;
  - stores the view-entity index at `P+0x2038`;
  - calls `pfnSetView`.

  Engine-observable. CERTAIN.

---

### cof_ladder_manager  (highest-risk class in this set)
- **Factory / size / vtable:** 0x1004E410 / 0x1B4 / 0x101B1AA4. Spawn 0x1004F380, Precache 0x1004F140, KV 0x1004E4D0, Use 0x1004FB50, think 0x1004ED40, sequence step 0x1004F470.
- **Keys:**

  | key | offset / meaning |
  |---|---|
  | `model` | pev->model, the first-person climb model, e.g. `models/ladder_climb.mdl` |
  | `animation1..30` | +0x128..+0x19C (string_t); the count goes to +0x1AC |
  | `customoff` / `customon` / `customoffd` / `customond` | +0xEC / +0xF0 / +0xF4 / +0xF8 |
  | `descendoffset` | +0x118 (int) |
  | `topent` | +0x10C (name; resolved later to EHANDLE +0xFC) |
  | `bottoment` | +0x110 (name; resolved later to EHANDLE +0x104) |

  CERTAIN.
- **Spawn:** point entity with EF_NODRAW. It forces the loop count +0x1B0 to at least 1. CERTAIN.
- **Use (0x1004FB50):**
  1. It requires an activator and pev->model. Then `pl = GetClassPtr(activator->pev)`; there is **no IsPlayer check**.
  2. If the active weapon is a lit `weapon_flare`, or a dual weapon with the flare, it shows ProFont
     "I'll need to drop this flare before I can climb" and returns.
  3. `+0xE4 descend = (value != 0.0)`.
  4. **`m_pPlayer(+0x114) = pl` is written unconditionally. There is no busy check.**
  5. `EnableControl(FALSE)` sets FL_FROZEN.
  6. It creates `camera_model` at the manager (0x10017950) and sets `pl+0x5AC` (raw pointer) to it, using the ladder model.
     When descending it raises z by `descendoffset`.
  7. `pl+0x388` (EHANDLE) = the camera, copied to +0x5C0.
  8. It sends **`AttachCam(short camIndex, byte 1)` MSG_ONE** to pl ([0x1022664C]).
  9. `pl.flags |= FL_NOTARGET | FL_GODMODE`.
  10. **`pev->owner = pl.edict`** (0x1004FE4F).
  11. It sets `SetThink(0x1004ED40)` with nextthink +0.1, then starts the animation sequence (0x1004F470, driven by `pev->owner`).

  CERTAIN.
- **Think (0x1004ED40):** when the final animation has run out (`+0xDD` set and time > +0xE0) it does the following:
  1. `EnableControl(TRUE)` on `m_pPlayer` and removes `pl+0x5AC`, setting it to 0.
  2. Clears `pl+0x388`.
  3. Sends `AttachCam(0,0)`.
  4. Clears FL_GODMODE and FL_NOTARGET.
  5. Runs **`CLIENT_COMMAND(pl, "finishladder\n")`** (0x1004EE7A).
  6. Moves pl to the `topent`, or to the `bottoment` when descending, at +36+1 z. It copies the angles and yaw into v_angle, sets fixangle, zeroes the velocity and clears FL_ONGROUND.
  7. Sends `CameraZoom(50,0,0)` and sets `pev+0x274 = 90`.
  8. Runs `FireTargets(target, pl, pl)`.

  Nothing ever clears +0x114 or pev->owner. CERTAIN.
- **Who is affected:** the activator, which is the presser of the `cof_ladder_manager_use` brush.
- **Player state changed:** FL_FROZEN, FL_GODMODE, FL_NOTARGET, client view on `camera_model`, and `P+0x5AC`/`P+0x388`.
  All of it is undone only by the think above, and only for **the player currently stored in +0x114**.
- **Story-map usage:** **33 managers in 15 maps**: c_avlopp 4, c_basement 1, c_city 1, c_down 1, c_ending5b 2,
  c_ending5c 1, c_fsewer1 1, c_roofs 4, c_roofs2 4, c_sewer2 2, c_start 3, c_subtunnel1 5, c_subtunnel2 2,
  c_subway2st3 1, c_village1 1.
  - 24 managers have both a bottom (ascend) and a top (descend) use brush.
  - No manager has a `target`.
  - All 59 top/bottom exits are `info_teleport_destination`s that **no** `cof_teleport` targets,
    so the party layer's teleport follow does not fire on a ladder exit.
- **Co-op hazard: STUCK-PLAYER (hijack).**
  - **Scenario:** A presses the bottom brush and is climbing (frozen, godmode, view on the camera model). B presses the
    same ladder's bottom or top brush. The brush cooldown is per brush (1 s) and nothing checks the manager.
  - **Effect:** +0x114 and pev->owner become B and the sequence restarts for B. When it ends, only B is released.
  - **Result for A:** frozen, godmode (so A cannot die and respawn), view attached to an orphaned `camera_model` forever.
  - **What the existing warp does:** `cof_warp` would move A's body but leaves FL_FROZEN and the camera.
  - **Secondary (crash):** A disconnects mid-climb and another client connects into A's slot before the climb ends.
    The think then dereferences freed private data. Narrow window; LIKELY.
  - Death mid-climb is prevented by FL_GODMODE. LIKELY: the flag is set CERTAIN; the damage path that honours it was not traced.
  - **Side effect:** any `trigger_camera` OFF(666) for all players during a climb clears the climber's FL_FROZEN while the view
    stays on the camera model. This includes `closeclothes`. Cosmetic.
- **Recommended engine treatment:**
  1. **Prevent the hijack (primary).**
     - **Busy test:** a `cof_ladder_manager` is busy while its `pev->owner` is a client edict whose flags contain
       FL_FROZEN|FL_GODMODE. Both are set by the climb and both are cleared at its end, CERTAIN.
       Alternatively track it from the `AttachCam(idx,1)` MSG_ONE to the end marker `pfnClientCommand(pl,"finishladder\n")`.
     - **While busy:** strip IN_USE from other players' usercmds when they are within about 96u of any
       `cof_ladder_manager_use` whose `target` is that manager's targetname.
     - Tell them in ProFont colour 5: `<name> is climbing. Wait a moment.` (throttled).
  2. **On drop of the climber:** write manager+0x114 = 0 and clear the manager's pev->owner.
     - The think null-checks +0x114 (0x1004ED6A, 0x1004EDC4) and the step checks owner (0x1004F482). CERTAIN.
     - Remove that player's `camera_model` (classname `camera_model`, owner = that edict).
  3. **Recovery if a hijack is ever observed** (owner changes from A to B while A still has FROZEN|GODMODE):
     - replicate 0x1004EDCA..0x1004F048 for A: clear FROZEN, GODMODE and NOTARGET;
     - set `A+0x5AC = 0` (verified private write; it is a raw pointer that the client commands `attack1pressed` etc. at 0x1001A0F9 dereference);
     - remove A's camera model;
     - send `AttachCam(0,0)`;
     - `CLIENT_COMMAND(A,"finishladder\n")`;
     - place A at the manager's top/bottom exit.

     Prevention is much simpler.
  4. **Warp:** refuse `cof_warp` while the caller has FL_FROZEN, and do not choose a frozen climber as the warp target.
- **Confidence:** CERTAIN for the code path, the missing busy check, the never-cleared fields and the engine reallocation.
  LIKELY for the crash, and that the climb is long enough (several animation loops) for a partner to press in time.

### cof_ladder_manager_use
- **Factory / size / vtable:** 0x1004E470 / 0x184 / 0x101B1CA8. Spawn 0x1004F3E0 (SOLID_BSP, MOVETYPE_PUSH, +0x180 = 0); Caps 0x1004F120 → 8 (+use); KV base 0x10102480 (`master` +0x17C); Use 0x1004FE80.
- **Use:**
  1. It requires an activator that `IsPlayer`, the master, and `time >= +0x180`. The player must have FL_ONGROUND.
  2. It plays `noise`.
  3. **SF 1:** if `player.z - target.z <= 40` → `FireTargets(target, pl, pl, USE_TOGGLE, 0.0)` (ascend).
  4. **Otherwise:** `FireTargets(target, pl, pl, USE_TOGGLE, float(pev->iuser1))`. `iuser1` = 1 means descend.
  5. Cooldown: +0x180 = time + 1.

  CERTAIN.
- **Who is affected:** the activator.
- **Story-map usage:** 57 in 15 maps (same maps as the manager). spawnflags 0 ×50, 1 ×7; iuser1 0 ×31, 1 ×26.
- **Co-op hazard:** it is the entry point of the hijack above. It has no check of its own beyond the 1 s per-brush cooldown.
- **Recommended engine treatment:** the IN_USE filter described under cof_ladder_manager.
- **Confidence:** CERTAIN.

### cof_wheelchairmode
- **Factory / size / vtable:** 0x10090460 / 0xDC / 0x101C8704. Spawn 0x1009D570 (point, EF_NODRAW); Precache 0x100991F0 precaches `wheelchair`; KV base (uses `message`); Use 0x100A5A00.
- **Use:**
  1. The activator must `IsPlayer`; then **`StartWheelchair(activator)`** (0x100F5B30).
  2. If `message` is set and `pl+0x390` (m_hWheelchair) is valid, the chair moves to `FindByTargetname(message)`'s origin,
     takes `this.angles`, and gets fixangle.

  CERTAIN.
- **`StartWheelchair` (0x100F5B30):**
  1. Returns if `+0x390` is already valid.
  2. Creates `wheelchair` at the player's position (owner = the player).
  3. **Clears the inventory slots** (0x100DD990 with (1,1): slots 1..3 or 1..6) and **removes all weapons** (0x100F0BA0).
  4. `EnableControl(FALSE)` and FL_NOTARGET.
  5. Head light off (`+0x2300`, HeadShield message).
  6. `+0x60C = 0` plus a HealthCap message; health = 100.
  7. `chair.Use(pl,pl,USE_TOGGLE)` (0x10087640).

  CERTAIN.
- **Wheelchair entity:** factory 0x10018800, 0x388 bytes, vtable 0x101A7FDC.
  - **Use 0x10087640:** if no rider (`+0x384`, raw pointer), the rider becomes the activator, `EnableControl(FALSE)`, then attach (0x10086370).
  - **Attach:**
    1. Strips items again and sets `rider+0x5DC/+0x5E0 = -2`. The chair's fuser1..4 come from the `cof_wheelchair_*` cvars.
    2. Moves the rider to the first `wheelchair_hide_spot`; there is none in the story.
    3. **MSG_ONE `Wheelchair(short chairIndex, byte 0)`** ([0x10226670]).
    4. `rider+0x390 = chair`; `SetPhysicsKeyValue(rider,"chr","1")`; fades in over 1 s.
    5. Gives `weapon_browning_wheelchair` ×4.
  - **Think 0x10087370:** copies the rider's health every frame through the raw pointer.
  - **TakeDamage 0x10086EB0:** forwards damage into the rider; a lethal hit runs the rider's death path (0x10086F5E..).
  - **Stop 0x10086200:**
    1. Only if there is a rider and `+0x368` is 0.
    2. Sends `Wheelchair(-1,0)`, then `EnableControl(TRUE)` and `HideHUD &= ~8` on the rider.
    3. Moves the rider to the chair origin + 36z with the chair's angles and fixangle.
    4. Clears `rider+0x390` and `+0x384`.

  CERTAIN.
- **Who is affected:** the **activator** (start). Stopping is the FIRST player's (see cof_stopwheelchair), or any camera ON's player.
- **Player state changed:** FL_FROZEN, FL_NOTARGET, client view in the chair, inventory and weapons wiped, gun given,
  `+0x60C` reset. Undone by the chair's Stop, which runs only from StopWheelchair on that same player
  (or when the chair reaches a `wheelchair_exit`).
- **Story-map usage:** 1, c_ending4b `wmode` (message `wheelchstart`, an `info_teleport_destination` at the
  `info_player_start`). Chain: brush `trigger_once` → `multi_manager multistart` (wmode at 0 s) → wmode.
  The activator is the first player to touch the brush.
- **Co-op hazard: STUCK-PLAYER** (and DESYNC: only one player becomes the wheelchair hero; the other plays on foot).
  - **Stop chain:** `monster_booksimonsledgehammer "simon"` forwards damage to `monster_generic "justhealth"`. On the lethal hit it
    fires `fuckoff` with the **FIRST** player (§0). `fuckoff` runs:
    - `stoooop` (cof_stopwheelchair) at 6 s → stops the FIRST player's chair;
    - `fitttth` (cof_teleport) at 6.1/7/8 s → teleports the FIRST player to `telelol`.
  - **If the guest was the rider:**
    - The host has no chair, so the stop does nothing.
    - The guest stays frozen with the chair view, its inventory wiped, while the host is moved to the ending.
    - J-warp moves the guest's body only, so it stays stuck until the map changes or the credits roll.
  - **Rider drops, then a reconnect takes the slot:** a dangling `+0x384` is read every frame → crash risk.
  - **Rider dies through the chair and respawns:** UNKNOWN (live test).
  - The inventory wipe is seen by the share ledger as "other loss → used". It removes the rider's story keys from the partner too.
    This is harmless in this final map, but worth knowing.
- **Recommended engine treatment:**
  1. **Map-scoped first player := wheelchair rider** while any `wheelchair` entity exists with a living rider.
     Rider = the chair's pev->owner, or the client that received `Wheelchair(idx,0)`.
     With that, the stop, the boss trigger activator and the end teleport all hit the rider, as in single player.
  2. **Fallback:** when the FIRST player is teleported by a `cof_teleport` (or the credits start) while a different client still rides,
     call `StopWheelchair` (thiscall 0x100F5D50, ecx = the rider's CBasePlayer) through the shim.
  3. **On rider drop:** set chair+0x384 = 0 and remove the chair. The think null-checks the rider at 0x10087383 (CERTAIN).
     The other chair methods were not all checked (LIKELY safe).
  4. Optional: warn the share ledger that this wipe is scripted, so it is not propagated as "used".
- **Confidence:** CERTAIN for the code and wiring; LIKELY that the FIRST player is the activator of `fuckoff` (it holds if the lethal hit
  comes through the sledgehammer proxy; another death route would make the activator the monster, which stops nothing).

### cof_stopwheelchair
- **Factory / size / vtable:** 0x100903A0 / 0xDC / 0x101C8F38. Spawn 0x1009D4D0; Use 0x100A58C0.
- **Use:** `FIRST = UTIL_FindEntityByClassname(NULL,"player")`, then `StopWheelchair(FIRST)` (0x100F5D50). That calls
  `chair.Stop` (0x10086200) if `FIRST+0x390` is valid, then `SetPhysicsKeyValue(FIRST,"chr","1")`. The activator is ignored. CERTAIN.
- **Who is affected:** the FIRST player.
- **Story-map usage:** 1, c_ending4b `stoooop`, fired from `multi_manager fuckoff` at 6 s (chain under cof_wheelchairmode).
- **Co-op hazard:** STUCK-PLAYER when the rider is not the FIRST player (see above).
- **Recommended engine treatment:** as for cof_wheelchairmode (rider-as-first scope, or a shim `StopWheelchair(rider)`).
- **Confidence:** CERTAIN.

### cof_puzzlebar, cof_puzzlebar2..5
- **Factory / size / vtable:** all are 0x368 bytes with KV 0x100BF970 (monster base); they use `iuser1` (state) and `iuser3` (initial). Use toggles `iuser1`.

  | class | factory | vtable | Use |
  |---|---|---|---|
  | `cof_puzzlebar` | 0x10061D80 | 0x101B6184 | 0x10063260 |
  | `cof_puzzlebar2` | 0x10061C00 | 0x101B6438 | 0x100630A0 |
  | `cof_puzzlebar3` | 0x10061C60 | 0x101B6680 | 0x10063110 |
  | `cof_puzzlebar4` | 0x10061CC0 | 0x101B68C8 | 0x10063180 |
  | `cof_puzzlebar5` | 0x10061D20 | 0x101B6B10 | 0x100631F0 |

- **Use:**
  1. Toggles `pev->iuser1`, playing the `close` or `open` animation (sets `iuser2 = 1` on close).
  2. `SetThink` to the completion check (cof_puzzlebar 0x100628A0, with siblings at 0x10061F..., 0x100622..., 0x100625..., 0x100628...) at +1 s.

  CERTAIN.
- **Completion check (0x100628A0):** when all five bars report `iuser1 != 0`, it does the following.
  1. `FIRST+0x378` (m_hPuzzleButton) = NULL.
  2. `FireTargets(button.target, FIRST, this)`.
  3. `button.iuser1 = 1` (done).
  4. **`SetView(FIRST, FIRST)`** (0x10117180).
  5. **MSG_ONE `Puzzlebar(byte 0)` to the FIRST player** (closes the UI).
  6. `FIRST+0x378 = NULL`.

  CERTAIN (0x10062A76..0x10062B41; the same block is at 0x10061FC0 in each sibling think).
- **Driving commands:** the client UI sends `boilercheck 1..4`, which toggles two adjacent bars as the sender (0x10063460).
  Only senders whose own `+0x378` is valid are accepted. `boilercheck 5` is the UI's close button (`client.dll` 0x1004A20B):
  it calls `SetView(sender, sender)` and clears the sender's `+0x378` (0x1001B8A4..0x1001B911). CERTAIN.
- **Who is affected:** world state is global. The completion side effects hit the FIRST player, not the solver.
- **Story-map usage:** c_asylumb, one of each class plus one `cof_puzzlebarbutton`.
  The target `restore_power` is two multi_managers: power and lights, an exit change (`changeexio`) and `objectiveee`.
- **Co-op hazard:** STUCK-PLAYER (soft) and WRONG-PLAYER-VISUAL.
  - **Guest solves it:**
    - The guest's view stays on the puzzle camera (`cof_telescope_camera "deadfidafv"`) and the guest's UI stays open.
      The guest can still press the UI close button, because the guest's `+0x378` is still valid, so this is LIKELY recoverable by hand.
    - The host gets a spurious `SetView(self)` and `Puzzlebar(0)`. If the host was in another camera or UI scene at that moment,
      that scene's view is broken.
  - There is no SOFT-LOCK: the world effect fires regardless of who solved it.
- **Recommended engine treatment:**
  1. Watch `button.iuser1` going from 0 to 1 (or the `Puzzlebar(0)` MSG_ONE).
  2. For every other client whose `+0x378` EHANDLE points at the button, **run `boilercheck 5` as that player** (game ClientCommand)
     and send them `Puzzlebar(0)`.
  3. Optionally suppress the `pfnSetView(FIRST,FIRST)` and `Puzzlebar(0)` to the FIRST player when the FIRST player's `+0x378` was not the button.
- **Confidence:** CERTAIN for the code. LIKELY that the client keeps the UI open without the message
  (`client.dll` Puzzlebar handler not traced).

### cof_puzzlebarbutton
- **Factory / size / vtable:** 0x100187A0 / 0x188 / 0x101A8344. Spawn 0x100633E0 (+use brush); Caps 0x1001E660; Use 0x10063590.
- **Use:**
  1. Requires an activator that `IsPlayer`, and the cooldown (`+0x180`, `+0x184`).
  2. `activator+0x378 = this`.
  3. If `iuser1 != 0`, it shows ProFont "I don't need to use this anymore." (colour 5).
  4. Otherwise, with `message` set: `SetView(activator, FindByTargetname(message))` through 0x10117180, then
     `Puzzlebar(byte 1)` MSG_ONE to the activator.

  CERTAIN.
- **Who is affected:** the activator.
- **Story-map usage:** c_asylumb ×1 (model *13, message `deadfidafv`, target `restore_power`).
- **Co-op hazard:** none by itself. Both players can open their own UI at the same time and both may toggle the global bars.
  Completion is handled under cof_puzzlebar.
- **Recommended engine treatment:** see cof_puzzlebar.
- **Confidence:** CERTAIN.

### cof_clothesmenu
- **Factory / size / vtable:** 0x1008F860 / 0xE4 / 0x101C9058. Spawn 0x1009C9E0 (point, EF_NODRAW). KV 0x100953D0: `cameraent` → +0xDC, `simonent` → +0xE0.
- **Use (0x100A2050):** the activator is **ignored**.
  1. `pl = FIRST`.
  2. Builds an unlocked-outfit mask through `CheckingEndingOrUnlockables(bit,0,0)` (0x100DD000).
     That reads the **server's** `scriptsettings.dat`, i.e. the host's unlocks.
  3. If `value == 0` (open):
     - the Simon model gets `rendermode 0, renderamt 255`;
     - `camera.Use(pl, pl, USE_ON, 0)`;
     - **MSG_ONE `Clothes(long mask, byte anyUnlocked, byte pl+0x424, byte 1)`** to pl ([0x10226698]).
  4. Otherwise (close): the Simon model gets `rendermode 5, renderamt 0` and `camera.Use(pl, pl, USE_OFF, 666.0)`,
     which takes the all-players restore path in §0.

  CERTAIN.
- **Client commands:**
  - `closeclothes` (0x1001A762) calls `cof_clothesmenu.Use(sender, sender, USE_OFF, 1.0)`.
  - `setclothes <n> [hoodie]` (0x1001A7C2) sets **the sender's** `+0x424 = n` and the **global cvar** `sv_custom_hoodie`.

  CERTAIN.
- **Who is affected:** the FIRST player gets the menu and the camera (`trigger_camera "clothescamera"`, spawnflags 1108 = 0x454, which includes 4 = take control, wait 666).
  Closing restores every alive player.
- **Story-map usage:** 2: c_start and c_bridge `clothes`, both fired by a wardrobe `func_button` (pressed by either player).
- **Co-op hazard:** WRONG-PLAYER-VISUAL and a temporary STUCK-PLAYER for the host.
  - The guest presses the wardrobe. The **host** is frozen into the clothes camera with the menu, wherever the host is
    (possibly in a fight), until the host closes it. The guest gets nothing and cannot pick clothes.
  - Any player's `closeclothes` releases it (the console works too).
- **Recommended engine treatment:** **scope "first player := the commanding player" to that player's usercmd processing**
  (`SV_RunCmd` / PlayerPreThink..PostThink), extending the existing client-command scope.
  - The wardrobe button fires synchronously during the presser's +use, so the presser becomes the FIRST player and gets the menu and camera.
  - This also covers other synchronous FIRST-player lookups triggered by +use or touch.
  - Optional: send `setclothes` effects per player (the outfit `+0x424` is already per sender).
- **Confidence:** CERTAIN for the code; LIKELY that the button fires synchronously (its `delay` is 0 in both maps).

### cof_hoodie
- **Factory / size / vtable:** 0x1008FDA0 / 0x100 / 0x101C9348.
  - Spawn 0x1009CF40: model (default `models/costumes/hoodie.mdl`), `DropToFloor`, `SetUse(0x10094EC0)`.
  - Caps 0x100988C0 → 8 (+use).
- **Use (0x10094EC0):**
  1. The activator must `IsPlayer` and `IsAlive`.
  2. By `pev->iuser1` (0..15): `activator+0x42C |= bit`. Bits 0x80..0x20000 are costumes; 0x2000000..0x20000000 are book pages.
  3. ProFont "Unlocked costume: …" or "Unlocked book page: …" (colour 0) to the activator.
  4. Plays `find_note.wav`, then `UTIL_Remove(self)`.

  The `+0x42C` readers are the per-player loop in `cof_credits` (0x100A2957..0x100A2CC8) and 0x1011B97C. CERTAIN.
- **Who is affected:** the activator only.
- **Story-map usage:** 7, one each in c_apartment1, c_bridge, c_college1, c_forest3, c_roofs, c_stad and c_subtunnel1.
  4 are book pages (`generic_note.mdl`, iuser1 12..15).
- **Co-op hazard:** DESYNC (cosmetic). Only the picker gets the unlock bit, and the collectible is removed for both.
- **Recommended engine treatment:** OR `P+0x42C` across players (verified private write), or ignore it.
- **Confidence:** CERTAIN (code); LIKELY (meaning of the credits loop).

### cof_speedchange
- **Factory / size / vtable:** 0x10091180 / 0xDC / 0x101C5A30. Spawn 0x1009DEC0; Use 0x100A7910.
- **Use:** `FindByTargetname(target)->pev->speed = this->pev->speed`. No player involved. CERTAIN.
- **Who is affected:** world only.
- **Story-map usage:** 1. c_asylum1 `sob_value` (speed 25) → `sob_door` (func_door_rotating), from `multi_manager sob_mm`.
- **Co-op hazard:** NONE. **Recommended engine treatment:** nothing. **Confidence:** CERTAIN.

### cof_playerbreathetoggle
- **Factory / size / vtable:** 0x100905E0 / 0xDC / 0x101C01C4. Spawn 0x1009D660; Precache 0x10099200; Use 0x100A5EB0.
- **Use:**
  - Activator only (`IsPlayer`).
  - `pev->frags != 0`: `+0x1EEA = 1`, `+0x1EEC = time` (on).
  - Otherwise: `+0x1EEA = 0`, `+0x1EEC = 0` (off).
  - The player think (0x100DBDDB..) then plays `player/vocal/breathing/breathing_1..5.wav` from the player.

  CERTAIN.
- **Who is affected:** the activator.
- **Story-map usage:** 4 in c_forest3 and c_forest5 (a `breathe` frags=1 and a `stopbreathe` in each), fired by `trigger_multiple`.
- **Co-op hazard:** NONE or cosmetic. Each player walks through the volumes. A player warped past one keeps or misses the breathing.
- **Recommended engine treatment:** nothing.
- **Confidence:** CERTAIN.

### cof_maxhealthchange
- **Factory / size / vtable:** 0x10090E20 / 0xDC / 0x101C70C4. Spawn 0x1009DC20; Use 0x100A73E0.
- **Use:**
  1. Activator only.
  2. `+0x60C = pev->impulse` (0, 1 or 2).
  3. MSG_ONE `HealthCap(byte)` ([0x1022661C]).

  The player think (0x100EDC2B) clamps health to 80 (cap 1) or 66 (cap 2). The value is re-sent at InitHUD (0x100F6FAA).
  `StartWheelchair` and `cof_strangle` (0x10076943) reset it to 0. CERTAIN.
- **Who is affected:** the activator.
- **Story-map usage:** 2 in c_asylum1: `hitbyp228` (impulse 2) and `hitbyrevolver` (impulse 1).
  They come from the `cof_yesno "yesno"` answer (target and notrigger branches) through multi_managers `multits4`/`multits5`.
  The activator is the player who answered.
- **Co-op hazard:** DESYNC (balance). Only the answering player is capped.
- **Recommended engine treatment:** optional. Mirror `P+0x60C` and send `HealthCap` to everyone (piece 4 decision).
- **Confidence:** CERTAIN.

### cof_giveitems
- **Factory / size / vtable:** 0x10090940 / 0xDC / 0x101C49B4. Spawn 0x1009D970; KV base (`message`; the map key `custommsg` is **not** read); Use 0x100A6BC0.
- **Use:**
  1. Requires `message` and an activator that `IsPlayer`. The **receiver is the activator only**. `pl+0x1EF8 = 0`.
  2. **SF 1:** `AddInventoryItem(pl, message /*descriptor*/)`.
     If that fails: ProFont "Inventory full" (2), then an `item_key` with `message = descriptor` is spawned at the player's feet.
  3. **Otherwise**, if the inventory is full (0x100E42F0): ProFont "Inventory full", then `GiveNamedItem(message,0,0)` anyway.
  4. **Otherwise** `GiveNamedItem(message,0,0)` (0x100E0A80), with one of these setups:
     - SF 2: plain;
     - SF 0x10: global `[0x1022647C] = 1` around the call;
     - none: `[0x10226480] = 1` and `pl+0x1EF8 = 1` around the call.

  CERTAIN.
- **Who is affected:** the activator.
- **Story-map usage:** 16 in 14 maps.
  - **Story items (descriptor, SF 1 or 3)** are handled by the existing share ledger:
    - `hallkey` (c_apartment1)
    - `basementkey` (c_apartment5)
    - `gatekey2` (c_asylum1)
    - `firekey` (c_college1dark)
    - `fuse` (c_college2, c_stad, c_subway2st1)
    - `fsewerkey` (c_fsewer1)
    - `card` (c_park3)
    - `rustykey` (c_sewer3)
  - **Non-story gifts (classname, flags 0):**

    | map | entity | gift | fired by |
    |---|---|---|---|
    | c_apartment1 | `giveitem1` | `weapon_glock` | intro func_button → MM 15 s |
    | c_asylumb | `giveflsh` | `weapon_flashlight` | `func_button flashbutt`, killed after one use |
    | c_nightmare | `kakkaej` | `weapon_camera` | trigger_once → MM 40.5 s |
    | c_sewer2 | `kutteahe` | `weapon_shotgun` | corpse func_button |
    | c_start | `givemobile` / `giveswitchblade` | `weapon_mobile` / `weapon_switchblade` | already covered by the party starter kit |

- **Co-op hazard:** DESYNC.
  - The partner never gets the glock, flashlight, camera or shotgun, and the source is one-shot.
  - There is no hard soft-lock: no story check requires these. The only weapon check in the story is c_asylum1's
    `cof_weapontrigger`/`cof_yesno` for `weapon_p345`, which is not given here.
  - The c_asylumb flashlight matters most for playability in the dark.
- **Recommended engine treatment:**
  - **Mirror weapon gifts.** When the activator gains `weapon_*` from a `cof_giveitems` Use (detect it by a frame diff on
    `m_rgpPlayerItems`, or by the `message` of the giveitems fired), give the same classname to every other spawned, alive
    player who lacks it. Reuse the existing give helper, or a shim to `GiveNamedItem` 0x100E0A80 (thiscall: name, 0, 0).
  - Leave the descriptor path to the ledger.
- **Confidence:** CERTAIN (code); LIKELY (gameplay importance).

### trigger_checkbrush
- **Factory / size / vtable:** 0x10028920 / 0x188 / 0x101ABFD0.
  - KV 0x10028980: `usesound` → `pev->noise`; base keys `master` +0x17C and `checkdelay` +0x184 (default 2.0).
  - Spawn 0x10028B90: SOLID_BSP, MOVETYPE_PUSH. Caps → 8. Use 0x10028C40.
- **Use:**
  1. Requires an activator that `IsPlayer`, the master, and the cooldown.
  2. **SF 1:** ProFont `"I don't want to watch it again... %i ...What is that anyway?"` with **the activator's**
     `m_iKeypadNumber` (unless it is 9999).
  3. **Otherwise:** ProFont `message` (colour 5) to the activator.
  4. Plays `noise` and re-arms the cooldown. **It fires nothing.**

  CERTAIN.
- **Who is affected:** the activator (text only).
- **Story-map usage:** 160 in 36 maps (an "examine" brush). Only 1 has SF 1: c_apartment3 (master `multiscr1hej`, the video-tape code).
- **Co-op hazard:** NONE once keypad codes are mirrored (planned in `sv_cof_state`). Otherwise the guest sees their own (0) code.
- **Recommended engine treatment:** nothing beyond the existing code mirroring.
- **Confidence:** CERTAIN.

### trigger_statueuse
- **Factory / size / vtable:** 0x10075AF0 / 0x188 / 0x101BAA50. KV 0x10075B50 (`master` +0x17C, `statuetype` +0x184); Spawn 0x10075CE0 (+use brush); Use 0x10075D70.
- **Use:**
  1. Requires an activator that `IsPlayer`, the master, and a 1 s cooldown.
  2. By `statuetype`, sends MSG_ONE `Statue(byte n, string tga)` (eagle, horse, lion or owl) or `board(string welcome tga)` to the activator.
- **Driving command:** `movestatue <dir> <statue>` (0x1001CB88 → 0x100E8EB0). **Any** client can send it.
  It finds the global `statue_*` entity by classname and moves it. CERTAIN.
- **Who is affected:** the activator's UI. The statues are world state.
- **Story-map usage:** 5 in c_park (one per statue type, plus the welcome board).
- **Co-op hazard:** NONE. The partner just doesn't see the other player's UI.
- **Recommended engine treatment:** nothing.
- **Confidence:** CERTAIN.

### func_tracktrain
- **Factory / size / vtable:** 0x100D2E20 / 0x140 / 0x101D806C. Caps 0x100D6860 → 0x40 (FCAP_DIRECTIONAL_USE); Use 0x100D8F70 (HLSDK-style; USE_SET is the drive path).
- **Story-map usage:** **all 7 are in c_intro**.
  - `skittag` ×3 and `skittag22`: passing trains at speed 300 along path_tracks.
  - `intarget1`, `infokus2`, `kfokus1`: slow, invisible (rendermode 2) camera look-at targets fired with/by `trigger_camera`s.
  - **There is no `func_traincontrols` in any story map**, so `OnControls` can never succeed and no player can drive a train.
    Players are never attached to these trains; the intro cameras (spawnflags 116, which includes 4) freeze them, not the trains.
- **Who is affected:** world only.
- **Co-op hazard:** NONE. Train control is impossible in the story; the camera freezing belongs to the trigger_camera research.
- **Recommended engine treatment:** nothing.
- **Confidence:** CERTAIN (no controls entity, class caps); LIKELY (that no player can reach the trains).

### cof_teleport
- **Factory / size / vtable:** 0x10091240 / 0xDC / 0x101C5B50.
  - Spawn 0x1009DF40: solid 0, EF_NODRAW, movetype 0.
  - KV: base only (`target`, `message`).
  - **spawnflags are never read** (no access to pev+0x1A4 in Use). Every story instance has sf 0.
- **Use (0x100A7990):**
  1. Returns if there is no activator or no `target`. If `message` is set, `named = FindByTargetname(message)`.
  2. **Activator is a player and `message` is empty → it moves the ACTIVATOR.**
     - `dest = first entity named target` (any class).
     - Origin = `dest.origin` with `z = dest.z - pl.mins.z + 1`, through `UTIL_SetOrigin`.
     - `P+0x618 (m_vPreviousOrigin) = new origin`.
     - angles = dest angles; v_angle.y = dest yaw; fixangle = 1.
     - velocity and basevelocity zeroed; FL_ONGROUND cleared.
     - **`b_GoingThroughADoor (+0x1EA4) = 0`**.
  3. **Otherwise, if `message` is set and found → it moves the NAMED entity** to `dest.origin + (0,0,1)`, with the same
     angle, velocity and ground resets but no mins adjustment.
     This happens even when the activator is a player and `message` is set, and the activator must only be non-NULL.
  4. **Otherwise** (activator not a player and no `message`) → nothing.
  5. **It never uses the FIRST player.**

  CERTAIN.
- **Who is affected:** the activator, or the `message` entity.
- **Story-map usage:** **200 in 74 maps.** Every real target is an `info_teleport_destination` (3 targets are missing → no-op).
  Root firers:

  | root | count | who is moved |
  |---|---|---|
  | `trigger_multiple` | 118 | each toucher. These are walk-in area transitions, typically 2 per map (c_village3 6, c_hamn3 4). |
  | `trigger_once` | 57 | the first toucher only, e.g. c_start `mainteleport`/`1teleport`/`2teleport`, c_roofs, c_subway2st3 cutscene teleports |
  | `func_button` | 5 | the presser |
  | `cof_yesno` | 5 | the answerer |
  | `cof_telephone`, `cof_weapontrigger` | 1 each | the user |
  | `cof_strangle` (c_ending1c/2c/3c `cteletele`) | 3 | the **FIRST player** (§0) |
  | `monster_doctorboss.target` (c_attic `teleportblack`/`deteleport1`) | — | the **FIRST player** |
  | booksimonsledgehammer health proxy (c_ending4b `fitttth`) | — | the **FIRST player** |
  | boss TriggerTarget via the generic AI trigger (c_roofs2 `teleportia4`/`42`, c_sewer3 `teleport_till_lik`; also the doctorboss TriggerTarget) | — | **nobody**: the activator is the monster, so this is a no-op, as in single player |
  | NPC moves with `message` (c_attic doctorboss, c_city2 `sawbossheh`, c_ending4b `simon`, c_park2 `fagno`) | 4 | the named entity |
  | unfired | 5 | — |

- **Co-op hazard:** WRONG-PLAYER for the FIRST-player chains (the host is moved even when the guest was nearer the scene).
  There is also a stranded partner after a `trigger_once` teleport. This is already handled by `CoF_PartyTeleports`:
  partners within 128u of `teleport_from` are taken along, others get the cut-off prompt and J.
  The engine's destination match (an `info_teleport_destination` within 8u xy and 96u z of the landing spot, targeted by a
  `cof_teleport`) fits this code: the feet land at dest+1, so the origin is about 37u above dest. **It never leaves anyone stuck**: it clears FL_ONGROUND and the door flag and freezes nothing.
- **Recommended engine treatment:** keep the existing party follow.
  - For the FIRST-player chains, the c_ending4b fix (rider-as-first) also fixes `fitttth`.
  - c_attic and c_ending1c/2c/3c are consistent within their chains (the strangle and doctorboss scenes also act on the FIRST player).
  - `trigger_multiple` teleports also pull a partner who stands within 128u. That is acceptable, because the partner would walk in anyway.
- **Confidence:** CERTAIN (code, spawnflags unused, firer classes). LIKELY that the boss TriggerTarget teleports are dead in single player too.

### trigger_cofteleport
- **Factory / size / vtable:** 0x10108130 / 0x180 / 0x101E4828. Spawn = `InitTrigger` (0x1010BA80); it does not use `SetTouch` but overrides the **Touch virtual (+0xE4) = 0x10113110**.
- **Touch:**
  1. Requires the master and a toucher that `IsPlayer`.
  2. Same placement as cof_teleport's player branch: `dest.z - mins.z + 1`, `P+0x618`, angles and v_angle, zero velocity, fixangle, clear FL_ONGROUND.
     It does **not** clear the door flag.
  3. SF 1 → removes itself after the first touch.

  CERTAIN.
- **Who is affected:** the toucher.
- **Story-map usage:** 2. c_apartment1 and c_apartmentsick (the elevator, target `hiss1_land1`, an `info_teleport_destination`, sf 0).
- **Co-op hazard:** NONE. Each player walks in. The party layer deliberately does not follow it (no `cof_teleport` targets that destination).
- **Recommended engine treatment:** nothing.
- **Confidence:** CERTAIN.

### trigger_teleport (CoF variant)
- **Factory / size / vtable:** 0x101089D0 / 0x180 / 0x101E30A0. Spawn 0x101111C0 = `InitTrigger` + `SetTouch(0x10111760)`.
- **Touch:**
  1. The toucher's flags must contain FL_CLIENT or FL_MONSTER; then the master; then a Spirit `CanTouch` (0x10109C40: `iuser1` disable, `netname` filter, spawnflags).
  2. `dest = target`.
  3. **CoF addition:** if `message` names a landmark, it does a **landmark-relative** teleport.
     The offset, yaw delta and rotated velocity are preserved (0x101117F3..0x10111AC1).
  4. Otherwise it does the HLSDK placement.

  CERTAIN.
- **Who is affected:** the toucher.
- **Story-map usage:** 7: c_bridge 1, c_broscene 2, c_down 1 (landmark `fiskfitta`, named `teleportt`), c_trainride 2, c_trainride2 1.
- **Co-op hazard:** NONE. **Recommended engine treatment:** nothing.
- **Confidence:** CERTAIN.

### trigger_changetarget / trigger_changevalue
- **trigger_changetarget:** factory 0x10108070, 0xF0 bytes, vtable 0x101E3C68, Use 0x10114680.
  - It finds the target with the locus-aware find (0x10118500, `*locus` = activator).
  - It sets `target.pev->target = m_iszNewTarget (+0xEC)`, or `= activator.targetname` when the new target is `*locus`.
  - CERTAIN.
- **trigger_changevalue:** factory 0x101080D0, 0xF0 bytes, vtable 0x101E3DD0, Use 0x10114760.
  - It calls `target->KeyValue(netname = m_iszNewValue (+0xEC))` on the locus-aware target, so it could hit the activator with `*locus`.
  - CERTAIN.
- **Who is affected:** world only. No story instance uses `*locus`.
- **Story-map usage:**
  - changetarget: 19, all in c_ending5b. A button-order sequence puzzle: `changeNN` re-points buttons 26/16/4/27/49/2/32/45/12/23 at the next `multiNN` or at `wrong`.
  - changevalue: 6. c_start ×5 (subtitle `linenumber`, `simon3.body`, from cutscene multi_managers) and c_apartment5 ×1 (`levelma3`, unfired).
- **Co-op hazard:** NONE. The sequence state is global and both players' presses feed the same sequence, as with a single player pressing quickly.
- **Recommended engine treatment:** nothing.
- **Confidence:** CERTAIN.

### cof_monster_random_spawn
- **Factory / size / vtable:** 0x10090E80 / 0xDC / 0x101C839C. Spawn 0x1009DC60; Precache 0x100992D0 (`message` classname).
- **Spawn:**
  - `pev->takedamage = 66.0` is used as a marker.
  - `SetThink(0x10094370)` at +2 s when `health > 0` (the "master" of a group).
- **Think:**
  1. Collects up to 30 marked entities with the same targetname by walking edicts from index 1. Players are skipped by the marker.
  2. Picks `frags` of them at random and creates `message` at each one's origin and angles.
     Names ending `_slower` get a random body. A `model` override is applied, and SF 1 → the monster gets spawnflag 4.
  3. Removes itself.

  **It never selects a player.** CERTAIN.
- **Who is affected:** world only.
- **Story-map usage:** 91 in 8 maps: c_buscity 15, c_city 6, c_college1dark 14, c_sewer2 9, c_stad 12, c_subway1st1 10, c_village1 8, c_village2 17.
- **Co-op hazard:** NONE. The spawn count is not scaled for two players (balance, piece 4).
- **Recommended engine treatment:** nothing.
- **Confidence:** CERTAIN.

### monster_generic (Use)
- **Factory / size / vtable:** 0x100B05E0 / 0x378 / 0x101CE36C. Use 0x100B0EB0.
- **Use:** writes `FIRST.m_iKeypadNumber (+0x2040) = atoi(sprintf("%i7%i9", this+0x36C+1, this+0x370+1))`. That is all. CERTAIN.
- **Who is affected:** the FIRST player.
- **Story-map usage:** 9 in 6 maps.
  - c_apartment3 `scareyoue`, c_ending1c/2c/3c `fagnerd`, c_ending4b `justhealth`, c_nightmare ×4.
  - The references are `killtarget`, `env_customize.target`, `scripted_sequence.m_iszEntity`, and boss `message` health links.
    None of them is a Use, so this path LIKELY never runs in the story.
- **Co-op hazard:** NONE once codes are mirrored (planned).
- **Recommended engine treatment:** nothing new.
- **Confidence:** CERTAIN (code); LIKELY (never used).

### trigger_subwaywall
- **Factory / size / vtable:** 0x10077920 / 0x184 / 0x101BB3C8.
  - Spawn 0x100779F0: +use brush with takedamage = 1.
  - Use 0x10077B80: ProFont `message` (colour 5) to the activator, 1 s cooldown.
  - **TakeDamage 0x10077A80:** counts only hits with damage bit 0x8000000.
    After `frags` hits it calls `FireTargets(target, this, this)`, plays `noise2` and removes itself.
  - CERTAIN.
- **Who is affected:** world. Either player's qualifying hits count.
- **Story-map usage:** 1. c_subway2st2 (frags 3, target `multibrick`).
- **Co-op hazard:** NONE.
  - It needs a weapon whose damage carries bit 0x8000000 (UNKNOWN which; LIKELY a specific melee weapon).
  - Only a player carrying it can break the wall, then both walk through.
- **Recommended engine treatment:** nothing.
- **Confidence:** CERTAIN (code); UNKNOWN (which weapon).

### trigger_hurt (CoF changes)
- **Factory / size / vtable:** 0x10108430 / 0x180 / 0x101E1774. Spawn 0x10110DE0 (HLSDK); HurtTouch 0x1010B700.
- **CoF additions:**
  - A player toucher with `iuser1` set, taking lethal damage, gets `P+0x1F2A = 1`. That flag is read at 0x100E5DB3 and reset in Spawn at 0x100F4511.
  - With `iuser2` set and lethal damage: `SetView(toucher, toucher)`.
  - The rest is HLSDK: the multiplayer per-client bitmask in `pev->impulse`, SF 0x10 "fire only for players", and SF 1 "fire once".

  CERTAIN.
- **Who is affected:** the toucher.
- **Story-map usage:** 57 in 19 maps. **No story instance sets `iuser1` or `iuser2`**. Damage 10–1000, several of them kill volumes.
- **Co-op hazard:** NONE beyond normal death handling (piece 3).
- **Recommended engine treatment:** nothing.
- **Confidence:** CERTAIN.

### trigger_auto
- **Factory / size / vtable:** 0x101076B0 / 0xF4 / 0x101E0364. Activation through vtable +0x44 → 0x10108BA0 (Spirit-style). CERTAIN.
- **Who is affected:** world. The activator is not a player.
  As a general rule, any activator-only entity (cof_teleport, cof_giveitems, cof_wheelchairmode) fed from a trigger_auto does nothing.
- **Story-map usage:** 3, all in c_start. They hide cutscene models (`env_customize` targets `simon_scene_invisible`, `…2`, `dooropen2_change1`).
- **Co-op hazard:** NONE. **Recommended engine treatment:** nothing.
- **Confidence:** CERTAIN (activation slot); LIKELY (activator detail).

### Related notes (not assigned, found while tracing)
- **`cof_ladderclimb`, `cof_ladderend_*`, `func_ladder*`** are absent from the story. Ladders in the story are only
  `cof_ladder_manager`.
- **`cof_weapontrigger`** (Use 0x100A8090) checks whether the **activator** has the weapon in `message` (0x100E2380) and fires
  `target` or `noise`. It appears once, in c_asylum1 (`weapon_p345`). The hand-over is already noted in the design.

---

## Summary table

| class | affected | hazard (2 players) | treatment |
|---|---|---|---|
| cof_ladder_manager / _use | activator (raw ptr +0x114) | **STUCK-PLAYER**: a second user hijacks; the first stays frozen + godmode + camera forever. Crash if the slot is reconnected mid-climb. | Busy = owner has FROZEN\|GODMODE: strip IN_USE near that ladder's use brushes for others. Null +0x114/owner on drop. Refuse warp while frozen. |
| cof_wheelchairmode | activator (chair raw ptr +0x384) | **STUCK-PLAYER** when the rider ≠ FIRST (c_ending4b) | First player := rider while a chair is ridden; fallback shim `StopWheelchair(rider)` 0x100F5D50; remove the chair on rider drop |
| cof_stopwheelchair | FIRST | (cause of the above) | same |
| cof_puzzlebar 1-5 | world; completion → FIRST | STUCK-soft (solver's view/UI) + WRONG-PLAYER-VISUAL (host's view reset) | On button iuser1 0→1 run `boilercheck 5` as each other player holding the button; optionally drop the stray SetView/Puzzlebar to FIRST |
| cof_puzzlebarbutton | activator | NONE alone | — |
| cof_clothesmenu | FIRST (menu/camera); close = all | WRONG-PLAYER-VISUAL + host temporarily frozen | First player := commanding player for that player's usercmd (+use/touch) |
| cof_hoodie | activator | DESYNC (cosmetic) | optional OR of P+0x42C |
| cof_speedchange | world | NONE | — |
| cof_playerbreathetoggle | activator | NONE/cosmetic | — |
| cof_maxhealthchange | activator | DESYNC (balance) | optional mirror of P+0x60C + HealthCap |
| cof_giveitems | activator | DESYNC (glock, flashlight, camera, shotgun only for one player) | mirror weapon gifts to partners; descriptors stay with the ledger |
| trigger_checkbrush | activator (text) | NONE (with code mirroring) | — |
| trigger_statueuse | activator UI; world statues | NONE | — |
| func_tracktrain | world (c_intro cinematic, 7) | NONE (no traincontrols in story) | — |
| cof_teleport | **activator**, or the `message` entity; never FIRST; spawnflags unused | stranded partner (handled); WRONG-PLAYER only through FIRST-activator chains | keep party follow; c_ending4b fixed by the rider rule |
| trigger_cofteleport | toucher | NONE | — |
| trigger_teleport | toucher (landmark variant) | NONE | — |
| trigger_changetarget / changevalue | world (`*locus` unused) | NONE | — |
| cof_monster_random_spawn | world | NONE (balance) | — |
| monster_generic Use | FIRST keypad code | NONE (unused; codes mirrored) | — |
| trigger_subwaywall | world | NONE | — |
| trigger_hurt | toucher | NONE (CoF keys unused) | — |
| trigger_auto | world | NONE | — |

## Open questions only a live test can answer

1. **Ladder hijack:**
   - Can player B really start the same ladder while A climbs? The prediction is yes; check whether A's frozen body or the +use cone blocks it.
   - Does A stay frozen with the orphaned camera after B finishes?
   - How long is a typical climb?
2. **Ladder dangling pointer:** disconnect mid-climb, then reconnect into the same slot within the climb time. Crash or not?
3. **Wheelchair, guest rider (c_ending4b):**
   - Confirm the guest stays frozen and in chair view after the host is teleported to `telelol`.
   - Does the client view recover at the credits?
   - Rider death through the chair and respawn: does `P+0x390` or the chair state survive Spawn?
4. **Puzzle solved by the guest:**
   - Does the guest's Puzzlebar UI stay open, and does its close button restore the view?
   - Does the host's client react to the stray `Puzzlebar(0)` while not in the UI?
5. **Clothes wardrobe pressed by the guest:**
   - Is the host frozen into the camera mid-play?
   - Does the guest's `closeclothes` (console) release it?
   - With the usercmd-scoped first-player reorder, does the presser get the menu?
6. **The `sv_custom_hoodie` cvar is global:** check what the guest's model shows after the host picks a hoodie.
7. **Boss-death teleports with a monster activator** (c_roofs2 `teleportia4`/`42`, c_sewer3 `teleport_till_lik`):
   confirm they do nothing in single player too, i.e. are not a co-op regression.
8. **trigger_subwaywall:** which weapon deals damage type 0x8000000?
9. **Weapon gift mirroring:** does a `GiveNamedItem` through the shim, or the existing give helper, behave for `weapon_flashlight` and `weapon_camera` without duplicates?
