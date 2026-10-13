# E: how an AI companion fights, reloads, heals and lights the way (static RE)

Binary: stock `runtimes/enhanced/cryoffear/cl_dlls/hl.dll` (SHA-256 `0036B91C…0E63`, base 0x10000000), cross-checked
against `client.dll` (same folder) for the input bits and the coop-flashlight renderer. Static only (capstone sweep, the DLL's
own save tables `CBasePlayerItem` 0x1021B188 / `CBasePlayerWeapon` 0x1021B328 / `PLAYER` 0x10217FE8, vtables from the
exported factories). Maps: the 81 story maps in `story_maps.json`. Nothing was launched or modified.

Tags: **CERTAIN** = read from machine code. **LIKELY** = code read, meaning/runtime consequence inferred. **UNKNOWN** = not established.

Builds on `2026-09-30-hl-inventory-re.md` (slots, `AddInventoryItem`, `EquipInventoryItem`), `2026-10-04-hl-saves-death-items-re.md`
(CPR, `StartRevivedAnim`, MP death) and `2026-10-12-player-state-re.md` (weapon gifts).

---

## 0. Layout and conventions used below

**Player (`CBasePlayer`, vtable 0x101A4DA8)** — offsets from the save table unless noted. CERTAIN.

| Offset | Field | Notes |
|---|---|---|
| +0x0308 | `m_flNextAttack` (float, abs. time) | not in the PLAYER save table; gates ItemPostFrame, ImpulseCommands, PlayerUse, SelectItem (name LIKELY) |
| +0x03AA / +0x03AC | `m_bJustJumped` / `m_flMaxJustJumpedTime` | lowers the weapon in the air |
| +0x05D0 | `m_iFlashlightFlags` | light bits (§3) |
| +0x060C | `m_iHealthCap` | 1 → clamp 80 HP, 2 → clamp 66 HP |
| +0x0610 | `m_fUseTime` | +use cooldown (0.5 s) |
| +0x1E98 | `m_iMobileFlashMode` | phone light on/off |
| +0x1EA8 | `m_iFlashlight` | weapon_flashlight on/off |
| +0x1ECC / +0x1ED0 | `m_fFallingTime` / `m_bShouldFallFreeze` | hard-landing freeze |
| +0x1EE4 / +0x1EE8 | `m_fPhoneCallTime` / `m_bInPhoneCall` | block weapon switching |
| +0x1EE9 | `m_bDodgeDisabled` | toggled by the `dodgetoggle` command |
| +0x1EF9 | `m_bWeaponWaitingToDeploy` | deferred deploy after a switch |
| +0x21D4 | pending heal pool (int) | not saved; drained by 0x100DCA10 |
| +0x21E8 / +0x21E9 / +0x21EA | `m_bInfiniteStamina` / `m_bInfiniteHealth` / `m_bInfiniteAmmo` | unlockable cheats; usable by the engine (§1.6) |
| +0x21F0 | `m_fStamina` (float 0..100) | |
| +0x21FC | `inventorySlot[7]` (string_t, 1..6 used) | |
| +0x2274 / +0x2278 | `m_fMorphineTripTime` / `m_bIsTripping` | |
| +0x22AC / +0x22B0 / +0x22B4 | `m_afButtonLast` / `m_afButtonPressed` / `m_afButtonReleased` | edge bits, computed in PreThink 0x100EFCB1–0x100EFCDC |
| +0x2514 | `m_rgpPlayerItems[6]` | buckets, chained by weapon `m_pNext` +0x164 |
| +0x252C / +0x2534 / +0x2538 | `m_pActiveItem` / `m_pLastItem` / `m_pNextItem` | |
| +0x2540 | `m_rgAmmo[32]` (int) | index table in §1.5 |

**entvars** (pev = player+4): health 0x164, frags 0x168, view_ofs 0x178, button 0x184, impulse 0x188, flags 0x1A8,
max_health 0x1B4, waterlevel 0x1C4, iuser4 0x254, fuser3 0x260 (per-player animation state: 1 fall freeze, 2/3/4 CPR,
5 phone, **10 suicide struggle**, **25 syringe heal**). CoF-specific flags: `0x40000` = sprinting, `0x80000` = out of breath. CERTAIN.

**Weapon (`CBasePlayerWeapon`, base vtable 0x101A7804).** Fields: `m_bRifleReload` +0x104, `m_bRaised` +0x106 (rifle scope),
`script` (inventory descriptor) +0x110, `m_iSprintState` +0x128 (0 ready, 1 sprinting, 2 reloading, 5 lowered after a jump),
`m_iSuicideState` +0x140 (+0x144 presses needed, +0x148 presses made), `m_pPlayer` +0x160, `m_pNext` +0x164, `m_iId` +0x168,
`m_bIronSights` +0x178, `m_bWaitingToReloadIronsights` +0x1A4, `m_flNextPrimaryAttack` +0x1CC, `m_flNextSecondaryAttack` +0x1D0,
`m_flNextTertiaryAttack` +0x1D4, `m_flTimeWeaponIdle` +0x1D8 (all absolute times), `m_iPrimaryAmmoType` +0x1DC, `m_iClip` +0x1E4,
`m_fInReload` +0x1F0, `m_iDefaultAmmo` +0x1F4; glock `m_bTaclight` +0x200; switchblade stance byte +0x204. CERTAIN.

**Weapon vtable slots** (CERTAIN for the call sites, names LIKELY): +0x12C IronSightsOn, +0x130 IronSightsOff, +0x13C GetItemInfo,
+0x144 Deploy, +0x14C Holster, +0x158 ItemPostFrame, +0x15C Drop, +0x178 SuicideStart, +0x17C SuicideResisted,
+0x1A4 PrimaryAttack (IN_ATTACK), +0x1A8 SecondaryAttack (IN_ATTACK2), +0x1AC TertiaryAttack (IN_CANCEL), +0x1B0 the
`weapontoggle` action, +0x1C0 Reload, +0x1C4 WeaponIdle.

### 0.1 Which usercmd bit is which button (client.dll `CL_ButtonBits` 0x10078670) — CERTAIN

| Bind (default key) | kbutton | usercmd bit | Server meaning in CoF |
|---|---|---|---|
| `+attack` (MOUSE1) | 0x10543498 | `IN_ATTACK` 0x1 | primary attack (fire / swing / use item) |
| `+attack3` (**MOUSE2**) | 0x10543510 | **`IN_CANCEL` 0x40** | tertiary = **iron sights / rifle scope** |
| `+attack2` (MOUSE3) | 0x105434A4 | `IN_ATTACK2` 0x800 | secondary = melee bash with a gun, switchblade stance, syringe on a partner |
| `+reload` (R) | 0x105434D4 | `IN_RELOAD` 0x2000 | reload |
| `+sprint` (SHIFT) | 0x10543474 | **`IN_RUN` 0x1000** | sprint |
| `+dodge` (ALT) | 0x10543528 | `IN_LEFT` 0x80 | client-side only; the server dodges on the `dodgeX` commands (§4.6) |
| `+use`, `+jump`, `+duck` | — | 0x20, 0x2, 0x4 | as HL |

The default bindings are in `cryoffear/config.cfg` and `gfx/shell/kb_act.lst` ("Tertiary Attack" = MOUSE2). An engine-driven
player therefore aims with **IN_CANCEL**, not IN_ATTACK2.

---

## 1. Fighting

### 1.1 The weapon set and where the story hands it out

Ammo index = `m_rgAmmo` index (player +0x2540 + 4·idx), derived in §1.5. "Box" = what one ammo box gives (multiplayer cap in brackets).
Story placement from the 81 story maps (`cofcoop/bsp.py`), givers from `cof_giveitems`/`monstermaker` keys.

| Classname (alias) | id | Kind | Clip | Ammo (idx) | Item max | Fire | IN_ATTACK2 | IN_CANCEL | Story source |
|---|---|---|---|---|---|---|---|---|---|
| `weapon_switchblade` (=`weapon_knife`) | 6 | melee | – | – | – | hold to swing | toggle stance | – | `c_start` `giveswitchblade` (starter kit); 1 lying in `c_ending5c` |
| `weapon_glock` (=`weapon_beretta`) | 3 | pistol | 15 | `glock` (2) | 150 | **semi (edge)** | pistol bash | sights | `c_apartment1` `giveitem1`; lying `c_attic`, `c_buscity`; 34 `ammo_glock` (box 15 [MP cap 45]) |
| `weapon_p345` (=`weapon_P228`) | 7 | pistol | 8 | `p345` (4) | 80 | semi (edge) | pistol bash | sights | `c_asylum2` (the only one); `c_attic` doctor `spawnp345`; 7 `ammo_p345` (8 [24]) |
| `weapon_revolver` | 21 | revolver | 5 | `revolver` (15) | 50 | semi (edge) | pistol bash | sights | only `c_attic` doctor `spawnrevolver` (depends on `m_iDoctorWeapon`); 7 `ammo_revolver` (5 [15]) |
| `weapon_shotgun` | 4 | shotgun | 5 | `buckshot` (6) | 50 | semi (edge), shell-by-shell reload | shotgun bash (stamina > 0) | sights | `c_sewer2` `kutteahe`; lying `c_asylum2`, `c_village1`; 7 `ammo_shells` (9 [27]) |
| `weapon_rifle` | 11 | bolt rifle | 5 | `rifle` (9) | 50 | semi (edge) | rifle bash (stamina) | **scope toggle** (`m_bRaised`) | `c_park`; 6 `ammo_rifle` (5 [15]) |
| `weapon_m16` (=`weapon_mp5k`) | 9 | rifle | 20 | `m16` (7) | 200 | semi (edge); `weapontoggle` flips +0x200 mode | m16 bash (stamina) | sights | `c_buscity`, `c_city2`, `c_ending5c`, `c_trainride2`, `c_village1`; 10 `ammo_m16` (20 [60]) |
| `weapon_vp70` | 19 | pistol | 18 | `vp70` (8) | 180 | no edge test (auto/burst, LIKELY) | vp70 bash | sights | `c_hamn2`; 8 `ammo_vp70` (18 [54]) |
| `weapon_tmp` (=`weapon_uzi`) | 14 | SMG | 30 | `tmp` (12) | 300 | auto (no edge) | tmp bash | sights | `c_buscity`, `c_ending5c`, `c_park2`, `c_subway2st3` — **donator-only pickup** (§4.9); 10 `ammo_tmp` (30 [90]) |
| `weapon_nightstick` (=`weapon_hammer`) | 12 | melee | – | – | – | hold | – | – | `c_basement`, `c_city2`, `c_down`, `c_fsewer1` |
| `weapon_axe` | 24 | melee | – | – | – | hold | – | – | `c_bridge`, `c_ending5b`, `c_start` (bonus room; unlock-gated in GiveNamedItem) |
| `weapon_branch` | 16 | melee | – | – | – | hold | branch bash | – | `c_bridge` ×3, `c_forest4` ×5 |
| `weapon_sledgehammer` | 18 | melee | – | – | – | hold, **needs stamina ≥ 8** | – | – | `c_subway2st3`, `c_village1` |
| `weapon_syringe` | 8 | heal | – | `syringe` (10) | 10 | self-inject | heal/revive partner (MP) | – | 50 lying in 33 maps + `monstermaker`s in `c_apartment1`, `c_park3` |
| `weapon_flashlight` | 1 | light | – | – | – | toggle light | pistol bash | – | `c_asylumb` `giveflsh` |
| `weapon_mobile` | 2 | phone/light | – | – | – | toggle phone light | punch | – | `c_start` `givemobile` (starter kit) |
| `weapon_lantern` | 10 | light | – | – | – | (none) | bash | – | 28 in forest/asylum maps |
| `weapon_flare` | 13 | light | – | `flares` (11) | 5 | light flare | bash | – | 17 in `c_subtunnel1/2`; `throwflare` command drops it lit |
| `weapon_camera` | 5 | flash | – | – | – | flash (hurts nearby `monster_*`, LIKELY) | – | – | `c_nightmare` `kakkaej` |
| `weapon_famas`, `weapon_mp5`, `weapon_booklaser`, `item_nightvision` | 23/27/25 | bonus | | | | | | | only the bonus rooms of `c_start`/`c_bridge`; mp5 "for mappers only", famas/axe unlock-gated (`0x100DD000`) — **do not plan around them** |

ItemInfo values are CERTAIN (each weapon's GetItemInfo, +0x13C; e.g. glock 0x10045AC0). Aliases share a factory (CERTAIN, `hlre factory`).
Fire-mode classification: an edge test `test byte [player+0x22B0], 1` inside PrimaryAttack (CERTAIN per weapon: glock 0x10046E00,
p345 1005EC90, revolver 10064C60, shotgun 1006DF20, rifle 100672A4, m16 10051D9E; absent in tmp 1007F1A0, vp70 10084880, melee).

### 1.2 Equipping a weapon

**What the DLL accepts — CERTAIN.**

- **`inventoryequip <slot>`** (ClientCommand 0x10018D60, branch 0x10019FD7) → `EquipInventoryItem(slot)` 0x100E40E0:
  1. returns if `m_bInPhoneCall` (0x100E40FA), on a ladder (`movetype == 5`, 0x100E4ED0), or `fuser3 == 10` (suicide struggle, 0x100E411F);
  2. empty slot → `inventory/inventory_no.wav`;
  3. reads the descriptor's `classname` key from disk; `item_key` → `UseInventoryItem` (keys, 0x100E4232);
  4. classname starting with `weapon_` → **`SelectItem(classname)` 0x100F1A50**, `CloseInventoryMenu`, `inventory_click.wav`.
  There is **no bounds check** on the slot (inventory note §8); valid slots are 1..6, or 1..3 when `m_bThreeSlots` (+0x1E9C).
- **`quicksel <1..3>`** → QuickSelect 0x100F07F0 → `SelectItem(m_sQuickSelN)` (char[128] at +0x204C/+0x20CC/+0x214C, filled by
  `quickselset<N> <slot>`, which accepts only `weapon_` descriptors). Guards: `time > +0x1EE4`, not on a ladder, not in a call.
- **Not available:** there is no `lastinv`, no `weapon_xxx` select command, no `slotN`/`invnext` handling and no select impulse in
  hl.dll. The string `lastinv` does not exist; ClientCommand's only `weapon_` comparisons are inside `quickselset1-3`; unknown
  commands fall through `rules->ClientCommand` (multiplay 0x100C7290: voice/`nerdrage`/`vote` only) to "Unknown command" (0x1001CC27).
- **`inventoryuse <slot>`** is key-only (`UseInventoryItem` 0x100E4410 looks up `map_name`); on a weapon slot it prints
  "Error! Item script seems to have no nice name!". Never use it for weapons.

**`SelectItem` 0x100F1A50 guards** (it silently does nothing when one fails) — CERTAIN:

| VA | Condition that blocks the switch |
|---|---|
| 0x100F1A59 / 0x100F1A66 | in a phone call / on a ladder |
| 0x100F1A7E | `m_fPhoneCallTime (+0x1EE4) > time` |
| 0x100F1A9A–0x100F1AD1 | the **current** weapon's next primary/secondary/tertiary time is in the future (mid-shot, mid-reload, mid-swing) |
| 0x100F1AD7 | `m_flNextAttack (+0x308) > time` (holster/deploy in progress) |
| 0x100F1AEC | `waterlevel ≥ 3` and the name is not `weapon_action` |
| 0x100F1B2D | `fuser3 == 10` |
| 0x100F1BCA | the weapon is already active, or not owned |

Then it exits iron sights (+0x130), calls `Holster` (+0x14C, ~0.5 s: e.g. glock 0x10045B40 sets `m_flNextAttack`), sets
`m_bWeaponWaitingToDeploy = 1` and `m_pNextItem`. **The swap happens later** in PostThink 0x100EDFB4–0x100EE061 once
`time > m_flNextAttack`: `m_pLastItem = active; m_pActiveItem = next; Deploy (+0x144); UpdateItemInfo (+0x150)`. Deploy then
sets the weapon's own next-attack times (switchblade ~0.8–1.0 s). A switch therefore takes roughly 1–1.5 s. CERTAIN (timings LIKELY).

**Finding the slot of a weapon.** Walk `m_rgpPlayerItems[0..5]` (+0x2514) and each `m_pNext` (+0x164); for each weapon read
`script` (+0x110, string_t descriptor) and `pev->classname`; the slot is the `i ∈ 1..6` with `inventorySlot[i]` (+0x21FC+4·i)
equal to that string (same string_t in practice; compare text to be safe). Default descriptors are
`inventoryitems/weapons/<classname>.txt` (strings at 0x101B0730 glock … 0x101BBD70 syringe); the glock with taclight uses
`weapon_glocktac.txt`. CERTAIN for the fields; the "same string_t" shortcut is LIKELY.

**Recipe (equip).** `inventoryequip <slot>` once; then poll every frame until `m_pActiveItem == weapon` **and**
`m_bWeaponWaitingToDeploy == 0` **and** `weapon.m_flNextPrimaryAttack <= time`. If after ~2 s nothing changed, one of the
guards held: retry, do not spam every frame (each call replays the click sound). Do not send `inventoryequip` while the current
weapon is reloading or swinging; it is refused.

### 1.3 Firing

**Server path — CERTAIN.** PostThink 0x100EAD70 → `CBasePlayer::ItemPostFrame` 0x100E4F80: returns while controlling a
`func_tank` (+0x24DC/+0x230C) or while `m_flNextAttack (+0x308) > time`; otherwise runs `ImpulseCommands` (vtable +0x25C =
0x100E2A60, which also runs `PlayerUse`) and then the active weapon's ItemPostFrame (+0x158). Base
`CBasePlayerWeapon::ItemPostFrame` 0x1011D560:

1. finishes a pending reload when `m_fInReload && m_flNextAttack <= time` (0x1011D91F–0x1011D9AC: `j = min(maxclip − clip, reserve)`);
2. `IN_ATTACK2` and `fuser3 <= 0` and next-secondary ready and not on a ladder → SecondaryAttack (0x1011D9B6);
3. else `IN_CANCEL` and `fuser3 <= 0` and `m_flNextAttack` ready → TertiaryAttack (0x1011DA9F);
4. else `IN_ATTACK` and `fuser3 <= 0` and next-primary ready and not on a ladder → PrimaryAttack (0x1011DB19);
5. else `IN_RELOAD` with a clip weapon, not already reloading → Reload (+0x1C0, 0x1011DBEB);
6. else idle.

Every firearm's own ItemPostFrame (glock 0x10046220) runs first and then calls the base one (0x10046C1D).

**Rules an AI must respect — CERTAIN unless marked.**

- **Semi-automatic guns fire on the press edge.** Glock PrimaryAttack 0x10046DC0 requires `m_afButtonPressed & IN_ATTACK`
  (0x10046E00). The engine must send `IN_ATTACK` in one usercmd and **clear it in the next** (PreThink computes the edge from
  `m_afButtonLast`). Holding the button fires once. Same for p345, revolver, shotgun, rifle, m16 (semi). TMP and VP70 have no edge
  test and fire while held.
- Fire rate: glock next primary = time + 0.1 s after a shot, +1.0 s when that shot emptied the clip (0x10047561–0x10047599).
- **Hip fire is allowed.** No PrimaryAttack requires `m_bIronSights`. Sights only change animation/spread (the shot uses the
  weapon's `m_AccuracyVector` +0x18C via 0x10088CA0). LIKELY: sights improve accuracy.
- **Iron sights = tap `IN_CANCEL`.** TertiaryAttack (glock 0x10047C00) needs all three next-attack times ready, not sprinting, not
  in the suicide state and the **press edge** of `IN_CANCEL` (`+0x22B0 & 0x40`). With the server cvar `ironsights_toggle`
  (cvar_t 0x10214778, **default "1"**) each tap toggles `m_bIronSights`. With `ironsights_toggle 0` sights are held: the glock
  ItemPostFrame drops them as soon as `IN_CANCEL` is no longer held/pressed/released (0x1004626B–0x100462C3). The rifle's
  IN_CANCEL (0x100682A0) toggles a scope (`m_bRaised` +0x106) with no edge test: tap it for **one** frame only.
- **No firing while sprinting.** Every PrimaryAttack requires `m_iSprintState (+0x128) == 0`. Sprint (state 1) starts when
  `IN_RUN` is held, `IN_BACK` is not, stamina ≥ 1, the player moves, does not duck and is **not in iron sights**
  (0x100F5239–0x100F52C2). After releasing `IN_RUN` the weapon needs ≈0.33 s to come back up (glock 0x10046B86–0x10046C13).
- **No firing just after a jump.** `Jump` 0x100E5030 sets `m_bJustJumped` (0x100E520F) until on ground or the timer expires
  (0x100EB22D). The glock/flashlight ItemPostFrame (via 0x100F5DA0, which returns that byte) puts the weapon into state 5
  ("lowered") and raises it on landing. This is the only "lowered weapon" logic; **there is no wall-proximity lowering**.
- **`fuser3` must be 0.** Any CPR, phone, syringe-heal (25) or suicide (10) state blocks all attacks (base gates 0x1011D9D2 etc.).
- **Ladders and deep water.** No attack on a ladder; at `waterlevel 3` guns dry-fire (glock 0x10046E10) and only `weapon_action`
  can be selected.
- **An empty clip reloads itself.** PrimaryAttack with `m_iClip == 0` calls Reload (glock 0x10046ED8) unless +0x1B8 is set.

### 1.4 Reloading

**Mechanism — CERTAIN.** `IN_RELOAD` → Reload (+0x1C0). Glock Reload 0x100475B0:

- needs reserve ammo > 0, next primary ready, not sprinting, `m_iSprintState ≤ 2`, not in the suicide state;
- in iron sights it only sets `m_bWaitingToReloadIronsights` (+0x1A4); ItemPostFrame then leaves sights and reloads (0x100462C9–0x10046357);
- full clip → nothing;
- otherwise `m_iSprintState = 2`, **`m_iClip = 0`** (0x1004765A), then `DefaultReload(15, anim, 2.6 s or 3.5 s if empty)` 0x1011C560
  (sets `m_fInReload`). Completion in base ItemPostFrame fills `min(15, reserve)` from the reserve.

**The rounds still in the magazine are thrown away** on every magazine reload: the same `m_iClip = 0` store is in p345
(0x1005F3DE), m16 (0x10051F8A), tmp (0x1007F975), vp70 (0x10084BA5/0x10084BEB), famas (0x1003C33A), g43 (0x10044DFE),
mp5 (0x1005BC01). CERTAIN for the code; that this costs ammo is LIKELY (live check below).

Revolver (`revolver_reload_start`, 0x10065270), shotgun (`shotgun_reload_start/insert`, 0x1006E3E0) and rifle reload one round at a
time and do **not** zero the clip. The shotgun ends its reload early on an `IN_ATTACK` press edge (0x1006D3FC). LIKELY: the
revolver and rifle behave the same.

**Recipe (reload).** Reload only when `m_iClip == 0`, or when no enemy is near and the clip is ≤ ⅓ full. Press `IN_RELOAD`
for one frame (base reacts to the level, so holding is harmless but useless). Do not switch weapons, sprint or aim during the
2.6–3.5 s; watch `weapon.m_fInReload (+0x1F0)` and `m_iSprintState == 2`. For the shotgun, tap `IN_ATTACK` once to cancel the
reload when a target is close and at least one shell is loaded.

### 1.5 Where ammo lives and how to refill it

**Ammo indices — CERTAIN.** `W_Precache` 0x1011FD90 calls `UTIL_PrecacheOtherWeapon` 0x1011FB60 in a fixed order with no branches
between calls (0x1011FEEF–0x1011FFF4; the CS block before it is gated on a global that nothing writes). Each call registers the
weapon's `pszAmmo1` through `AddAmmoNameToAmmoRegistry` 0x1011B4D0, which skips names it already has and starts at index 1.
The resulting table:

| idx | name | `m_rgAmmo` offset | used by |
|---|---|---|---|
| 1 | `flashlight` | +0x2544 | weapon_flashlight (unused by its code) |
| 2 | `glock` | +0x2548 | glock |
| 3 | `browningwheelchair` | +0x254C | wheelchair ending |
| 4 | `p345` | +0x2550 | p345 |
| 5 | `g43` | +0x2554 | g43 |
| 6 | `buckshot` | +0x2558 | shotgun |
| 7 | `m16` | +0x255C | m16/mp5k |
| 8 | `vp70` | +0x2560 | vp70 |
| 9 | `rifle` | +0x2564 | rifle |
| 10 | `syringe` | +0x2568 | syringe (count of syringes) |
| 11 | `flares` | +0x256C | flare |
| 12 | `tmp` | +0x2570 | tmp/uzi |
| 13 | `mp5` | +0x2574 | mp5 |
| 14 | `famas` | +0x2578 | famas |
| 15 | `revolver` | +0x257C | revolver |

Read the index from the weapon (`m_iPrimaryAmmoType` +0x1DC) at runtime anyway.

**How the game adds ammo — CERTAIN.**

- `GiveAmmo(amount, name, max, silent)` is player vtable +0x94 = 0x100E04D0. It asks `rules->CanHaveAmmo` (+0xA8), adds
  `min(amount, max − current)`, prints "Picked up …" (ProFont) and sends `AmmoPickup`, `AmmoX` and `WpnInfo`. When full it
  prints "I can't carry any more … ammo" only if IN_USE is involved.
- Ammo boxes (`ammo_*`, AddAmmo = vtable +0x11C) give the amounts in the table above. **In multiplayer the cap is three magazines**
  (glock 45, p345 24, buckshot 27, revolver 15, rifle 15, m16 60, tmp 90, vp70 54, g43 30), in single-player the item maximum.
  Example: ammo_glock 0x10045640 (`push 0x2d` in the IsMultiplayer branch, `0x96` otherwise).
- **An ammo box needs IN_USE or `iuser4 == 1`.** `CBasePlayerAmmo::DefaultUse` 0x10017AB0 → 0x1011C5E0 rejects the player unless
  `pev->iuser4 == 1` or `(button|pressed|released) & IN_USE` (0x1011C611–0x1011C634). `FallInit` 0x1011F7D0 installs the touch
  handler only when `iuser4 == 1`.
- **`GiveNamedItem` 0x100E0A80** (thiscall on the player, args `(const char *classname, BOOL, BOOL)`, `ret 0xC`) creates the entity
  at the player, sets `iuser4 = 1` (edict+0x2D4 at 0x100E0C37), `DispatchSpawn`s it and `DispatchTouch`es it with the player
  (0x10016980). It therefore works for `ammo_*` without IN_USE. It refuses `weapon_famas`/`weapon_axe` without the unlock,
  `weapon_tmp` without donator status and `weapon_mp5` for non-mappers.

**Refill options for the companion, best first.**

1. **`m_bInfiniteAmmo` (+0x21EA) = 1 on the companion.** Every firing path tests it before `--m_iClip`
   (glock 0x10046FE5, p345, revolver, shotgun, rifle, m16, tmp, vp70; syringe 0x1007AB3B/0x1007A5E6; flare 0x1001B147).
   The clip never drops, so no reloads and no ammo bookkeeping. CERTAIN. It does not touch the partner's game.
2. **Write `m_rgAmmo[idx]`** (int at +0x2540+4·idx). Base ItemPostFrame resends `WpnInfo` when the cached value (+0x120) differs
   (0x1011D891–0x1011D919), so the HUD of a real client follows. CERTAIN. Clip: weapon +0x1E4.
3. **Give an ammo box through the game.** `SV_CoF_StoryGiveItemEx` (`sv_cof_cheats.c:782`) already sets IN_USE around Use, which
   satisfies the ammo check, but it **only accepts `weapon_`/`item_` classnames** (line 794). Allow `ammo_` there, or call
   `GiveNamedItem` through the shim. Respect the MP three-magazine cap.
4. Let the companion pick up boxes: aim at the box within 80 units and tap IN_USE (§1.8).

### 1.6 Melee

**Mechanism — CERTAIN** (timings/ranges read from constants, LIKELY to the nearest frame).

- Melee PrimaryAttack has **no edge test**: hold `IN_ATTACK` and it repeats whenever the next-primary time passes. Each swing
  sets a player think that traces from the eye along the view.
- Melee data (swing → hit delay, range from eye, refire):

  | Weapon | Hit delay | Range | Refire | Damage (skill.cfg) |
  |---|---|---|---|---|
  | switchblade | 0.16 s | 32 u | 0.5 s stance A / 0.9 s stance B | 6 / 9 (`sk_cof_switchblade`, `sk_cof_2switchblade`) |
  | nightstick | 0.31 s | 32 u | 1.0 s | 11 |
  | axe | 0.26 s | 40 u | ~0.8–1.0 s | 55 |
  | branch | 0.37 s | 40 u | ~0.8–1.0 s | 10 |
  | sledgehammer | 1.0 s | 32 u | 2.66 s | 45 |
  | pistol/flashlight bash (IN_ATTACK2) | 0.3 s | 32 u | 1.0 s | |
  | shotgun bash (IN_ATTACK2) | 0.23 s | | 1.0 s | |

- Switchblade (PrimaryAttack 0x10079F50): stance byte +0x204 picks `switchblade_attack1` (think 0x100E87D0) or `attack2`
  (0x100E80B0). **IN_ATTACK2 toggles the stance** (0x1007A2D0) and locks attacks for 1.5 s; an AI should not press it.
- **Stamina:** the sledgehammer refuses to swing below **8 stamina** (0x100716FF); gun bashes (shotgun/rifle/m16/famas/g43) need
  stamina > 0 (shotgun 0x1006E677). The hit thinks subtract stamina unless `m_bInfiniteStamina` (e.g. 0x100E8BA1).
- The 32–40 unit range is measured from the eye, so the companion must be practically touching the monster.

**Recipe (melee).** Close to ≤ 30 units (centre distance ≈ 30 + monster hull radius), aim at the chest, hold `IN_ATTACK`, keep
pressing forward lightly. Use the switchblade as the default fallback (it is in every player's starter kit). Prefer
nightstick/axe if owned. Skip the sledgehammer unless stamina ≥ 8.

### 1.7 Friendly fire between players — CERTAIN

`CHalfLifeMultiplay::FPlayerCanTakeDamage` (rules vtable +0x48 = 0x100C8700) is called from the player's TakeDamage 0x100F6030
at 0x100F60C7:

```
if (attacker->IsPlayer() && !(victim->pev->flags & FL_FAKECLIENT /*0x2000*/)) return FALSE;   // no damage
return TRUE;
```

- Players never hurt **real** clients: bullets, melee and syringes from the companion cannot damage the human.
- **A fake client (`FL_FAKECLIENT`) can be hurt by players.** If the companion is created with `pfnCreateFakeClient` (which sets
  `FL_FAKECLIENT`), the human's stray shots and swings damage it. Note this is not a rule an engine "idle second client" has.
- `mp_friendlyfire` (cvar_t 0x10214638, default "0") is read only by the teamplay rules (0x10106C70) and does not apply here.
- TakeDamage also returns at once while `b_GoingThroughADoor` (+0x1EA4) is set (0x100F603B).

**Recommendation.** The damage decision reads only the victim's edict flag. Do **not** clear `FL_FAKECLIENT` permanently: this
engine tests the edict flag itself in `SV_InactivateClients` (`sv_frame.c:966`, drops fake clients on a level change),
lag compensation (`sv_pmove.c:336`), `pfnGetAimVector` (`sv_game.c:2472/2501`), client counting (`sv_main.c:538`) and pmove
physents (`sv_pmove.c:149`). Instead clear it **only while a human's usercmd runs**: `SV_RunCmd` (`sv_pmove.c:1008`) already
brackets PreThink, the player's own think (`SV_PlayerRunThink`, where melee hit thinks such as 0x100E87D0 run) and PostThink
(where guns fire) with the `SV_CoF_UsercmdSender` scope (lines 1065–1160). Clearing the companion's bit inside that scope and
restoring it after blocks every player-caused hit, while monsters (which act in `SV_Physics`) still hurt the companion. LIKELY;
see open question 3. An idle real client slot instead of `pfnCreateFakeClient` avoids the issue entirely.

### 1.8 Picking things up (used by §1.5 and §2)

`PlayerUse` 0x100EA630 runs inside ImpulseCommands. It needs `IN_USE` in button/pressed/released, `time ≥ m_fUseTime (+0x610)`,
no phone call, no computer (+0x1EA0), not on a ladder and no active CPR (+0x2698). It traces 80 units from the eye and otherwise
searches an 80-unit sphere in the view cone; afterwards `m_fUseTime = time + 0.5`. Weapons are picked up by Use
(`CBasePlayerItem::DefaultUse` 0x10017AC0 → DefaultTouch 0x1011C7D0; Touch is armed only if `iuser4 == 1`). The weapon shares
the 6 (or 3) inventory slots with story items ("Inventory full"). CERTAIN.

---

## 2. Healing

### 2.1 What heals

- **`weapon_syringe` is the only healing item in the story** (morphine). 50 lie in the story maps (`frags -1` → each gives 1
  syringe, Spawn 0x1007B600), plus `monstermaker`s in `c_apartment1` (`spawnsyringe`) and `c_park3` (`spawnd5`). No health kits,
  no `item_healthkit`, no regeneration. CERTAIN for the map scan.
- It is a weapon (id 8, inventory slot `inventoryitems/weapons/weapon_syringe.txt`), the count is `m_rgAmmo[10]`, max 10
  ("I can't carry any more syringes", DefaultTouch 0x1011C9CD). Picking one up while holding one adds 1 to the count.
- In multiplayer a **CPR revive** (hold +use on a corpse) and the **syringe on a partner** exist (§2.4).

### 2.2 Self-injection — CERTAIN

1. Equip the syringe (`inventoryequip <slot>`).
2. `PrimaryAttack` 0x1007B040 (IN_ATTACK; needs next-primary ready and not sprinting): plays `syringe_use`, schedules
   `0x1007AAB0` in **2.0 s**, next attack +5 s.
3. `0x1007AAB0` heals the target (itself here):
   - single-player only: injecting again before `m_fMorphineTripTime` starts the morphine trip (0x100E8D70: blur, `room_type`,
     `Concuss`). **Not in multiplayer** (the IsMultiplayer test at 0x1007AAC4 skips it);
   - `pending heal (+0x21D4) = sk_syringe_heal` (global 0x1022672C, loaded at 0x100AF858: skill 1/2/3/4 → **100/80/60/50**);
   - plays a heal sound (0x100E8D00); `m_fMorphineTripTime = time + sv_morphinetriptime` (default "35");
   - `m_rgAmmo[syringe]−−` unless `m_bInfiniteAmmo`; ≥ 1 left → redeploy the syringe after 1.5 s (0x1007B0D0), else retire the
     weapon and switch away (0x1007A710 → 0x100F1C20).
4. The pool is drained by 0x100DCA10 (called from PreThink at 0x100F0260): 5 HP per call (its 0.1 s throttle compares against
   the *integer* time, so it lets almost every usercmd through) via `TakeHealth` (+0x68), adding the same amount to stamina
   (capped at 100). In practice the heal lands within a fraction of a second. It stops at `pev->max_health` (+0x1B4); the rest
   of the pool is discarded.

There is no health check before injecting: a full-health injection wastes a syringe.

### 2.3 Health cap rules — CERTAIN

- Health never exceeds `pev->max_health` through healing (0x100DCA65).
- `m_iHealthCap` (+0x60C) is enforced every frame in PostThink 0x100EDC2B: **1 → health clamped to 80, 2 → clamped to 66**.
  It is set per player by `cof_maxhealthchange` (Use 0x100A73E0; `c_asylum1` `hitbyrevolver` = 1, `hitbyp228` = 2) and cleared
  only by the strangle scene (0x10076943, which also sets the heal pool to 100) and wheelchair mode (0x100F5C19). `Spawn` does
  not clear it.
- A revive (CPR or syringe) restores exactly 30 HP (inventory/saves note §3).

### 2.4 Healing or reviving the partner (multiplayer only) — CERTAIN

`SecondaryAttack` 0x1007B110 (IN_ATTACK2 with the syringe active; returns at once in single-player):

- traces **48 units** from the eye along the view, testing every connected player;
- **living partner with health < 100** (0x1007B44F): both players get `fuser3 = 25` and `ViewLock` (0x100F9140) → think
  0x1007A540 after 1.5 s clears both states and heals the partner through 0x1007AAB0 (same amount and syringe cost as above);
- **dead partner** found by the trace or within a **64-unit sphere** of its end, not gibbed (+0x501): plays the same animation and
  after ≈2.16 s calls `StartRevivedAnim(partner)` 0x100F1860 (at 0x1007A5CF) and spends one syringe. The partner stands up with 30 HP.
- **CPR without a syringe:** aim at the corpse (within 80 u, corpse on the ground), hold `IN_USE` for `sv_coopcprtime`
  (default "5") seconds; releasing cancels (player Use 0x100F88C0 and PreThink 0x100EFCB4–0x100F01E6, saves note §3).

### 2.5 Recipe (healing)

- **Self:** when `health ≤ max(1, effectiveMax − heal)` (`effectiveMax` = 80/66 with a cap, else `max_health`; `heal` = 100/80/60/50
  by skill) and `m_rgAmmo[10] ≥ 1` and no enemy within ~300 units: `inventoryequip <syringe slot>` → wait for deploy →
  IN_ATTACK for one frame → keep still for 2.0 s (do not switch, sprint or jump) → wait for the redeploy (1.5 s) → re-equip the gun.
  About 4–5 s in total.
- **Partner:** with the syringe active, stand within ~40 units facing the partner's chest and tap IN_ATTACK2 once. Partner health
  must be < 100. Both of you are locked for 1.5 s.
- **Downed partner:** prefer CPR (free, 5 s hold of IN_USE aimed at the body). Use the syringe revive (IN_ATTACK2) when an enemy is
  near, since it takes ≈2 s.
- Engine shortcuts: write the heal pool (+0x21D4) on the companion, or call `StartRevivedAnim` on a dead player (saves note §3).

---

## 3. Light

### 3.1 The co-op flashlight: the `flashlight` client command — CERTAIN

ClientCommand branch 0x1001C1E4:

- Refused while viewing a camera (0x100E4E00: `m_hCurrentCamera`/`m_pAttachedCamera` with a foreign view entity), with an attached
  camera (+0x388) or in the wheelchair (+0x390).
- On `doc_` maps (global 0x10222F10, set at 0x10120F06) and in single-player it toggles night vision instead (needs
  `item_nightvision`, `m_bHasNightVision` +0x434).
- **In multiplayer, for a living player, it toggles `pev->frags` between 0 and 32.0**, plays `items/flashlight1.wav` and broadcasts
  `ScoreInfo` (MSG_ALL) with the new frags (0x1001C3A3–0x1001C463).
- client.dll stores frags per player (ScoreInfo parser 0x100A9DA0 → 0x105571C8) and its renderer 0x10065260 draws a flashlight for
  every player whose **frags & 0x29** (bits 1, 8, 32) is set. So the light is client-drawn, needs no item and no battery, and every
  client sees it. kb_act.lst calls it "Flashlight (coop only)".
- PostThink 0x100EDB39–0x100EDC25: whenever `m_iFlashlightFlags` (+0x5D0) changes, MP sets **`frags = flags`** and broadcasts
  `ScoreInfo`, which **overwrites the 32 from the command** (e.g. switching to or from the phone light or the flashlight weapon).

`impulse 100` is **not** a flashlight in CoF: it prints the aimed entity's classname (ImpulseCommands 0x100E2A60, case 100 →
0x100E2BEC). CERTAIN.

### 3.2 Item lights — CERTAIN

| Light | How it turns on | Flag in +0x5D0 | Seen by others |
|---|---|---|---|
| `weapon_flashlight` | **on automatically when deployed** (think 0x10040190); IN_ATTACK toggles (0x10040040, `m_iFlashlight` +0x1EA8); off on holster (0x10040110) | 8 | yes (frags & 8) |
| `weapon_mobile` (phone) | tap IN_ATTACK (0x10053990 → 0x100530B0 toggles `m_iMobileFlashMode` +0x1E98); refused when the phone is broken (mobile +0x1F9). **Holding IN_ATTACK re-toggles every second** | 1 (2 while holstered, if `m_iPhoneDisabled == 0`) | yes (frags & 1) |
| glock taclight | `weapontoggle` (glock +0x1B0, 0x10047DB0) once the taclight is combined (`m_bTaclight` +0x200) | 4 | no (not in 0x29) |
| `weapon_lantern` | always lit while held (no attack code; `p_` model is `models/null.mdl`, 0x100500BB) | – | probably not (LIKELY) |
| `weapon_flare` | IN_ATTACK lights it (0x1003E9F0); `throwflare` (0x1001AF3F) drops it lit; burns `sk_flare_burn_time` (120/90/60/30 s) | 0x10 | by the flare entity (LIKELY) |

No battery drain exists for the flashlight weapon or the coop flashlight (`m_iFlashBattery` +0x22A8 is only set to 99 in Spawn
and sent to the HUD). CERTAIN.

### 3.3 Do lights affect monsters?

No monster code reads `m_iFlashlightFlags` (+0x5D0) or `m_iFlashlight` (+0x1EA8): every reader is a weapon or player function
(CERTAIN for those two fields). The coop flashlight only changes `frags`; I found no monster that tests a player's frags
(LIKELY, not exhaustive). Lights are cosmetic for the server. The camera flash is the only light that hurts monsters
(`weapon_camera` PrimaryAttack 0x10027280 damages `monster_*` in a sphere, LIKELY).

### 3.4 Recipe (light)

- In dark areas send `flashlight` once (check `pev->frags == 32` afterwards; re-send if a weapon-light change reset it). It needs
  no slot and works with any weapon in hand, so it is the right default for the companion.
- Keep the phone and the flashlight weapon for the human; if the companion carries `weapon_flashlight`, holding it is enough (it
  lights on deploy), but then the companion has no gun up.
- Never send `weapontoggle` with the phone in hand (§4.5).

---

## 4. What an AI player must avoid

### 4.1 The suicide struggle (`monster_crazybitch`) — CERTAIN; this one kills the AI

- `monster_crazybitch` (story: `c_apartment4` ×4, `c_asylum1`, `c_asylum2`, `c_buscity`, `c_subway1st1` ×4) grabs a player who holds
  a firearm and starts the weapon's suicide state (0x10055860: `m_iSuicideState = 1`).
- ItemPostFrame (glock 0x100466A8) → state 2 → SuicideStart (+0x178, glock 0x100478E0): `fuser3 = 10`, red fade, `suicide` player
  animation, **needed presses = RandomLong(10, 15)**, think 0x10047B70 after **4.0 s**.
- Each **press edge of IN_ATTACK or IN_ATTACK2** (`m_afButtonPressed & 0x801`, 0x100465CF) adds one; reaching the count calls
  SuicideResisted (+0x17C, glock 0x10047A70: state 0, `fuser3` 0).
- If the 4 s think fires first, the player **shoots himself**: health 0, `Killed` (0x10047B87/0x10047B9C).
- **Recipe:** when `weapon.m_iSuicideState (+0x140) == 2` or `pev->fuser3 == 10`, alternate IN_ATTACK on/off every usercmd until
  the state clears (15 presses ≈ 0.5 s at 60 Hz). Holding the button counts once.

### 4.2 Hard landings — CERTAIN

A hard fall sets `m_bShouldFallFreeze` (+0x1ED0) and holds the player down until `m_fFallingTime` (+0x1ECC; 3 s in MP, 5 s in SP,
0x100EC0DE). The client normally sends `tapgetup` after a number of jump presses; the server only accepts it when `fuser3 != 0`
(0x1001A33C) and then ends the freeze at once. **Recipe:** after landing, if `fuser3 != 0` and the companion cannot move, send
`tapgetup`.

### 4.3 States that silently swallow input — CERTAIN

| State | What it blocks |
|---|---|
| `m_bInPhoneCall` (+0x1EE8), `m_fPhoneCallTime > time` | equip, quick-select, +use |
| ladder (`movetype 5`) | equip, quick-select, drop, every attack |
| `fuser3 != 0` (CPR 2/3/4, phone 5, suicide 10, syringe heal 25, fall freeze) | every attack (`0 >= fuser3`) |
| `m_flNextAttack (+0x308) > time` | the whole ItemPostFrame, impulses and +use |
| `func_tank` control (+0x24DC/+0x230C) | weapons |
| computer (+0x1EA0) | +use, impulses |
| `waterlevel 3` | guns dry-fire; only `weapon_action` selectable |
| camera view (0x100E4E00) | `flashlight`, `adlib` |
| `FL_FROZEN` (cutscenes, `EnableControl(FALSE)`) | movement only. **Weapon code does not test it** — the AI must stop firing on its own while frozen (LIKELY consequence) |

### 4.4 Stamina, sprint, jump and dodge — CERTAIN (rates LIKELY)

- Stamina (+0x21F0, 0..100) is updated in 0x100F4D70: sprint start costs 5; sprinting drains about 1 per 0.1 s; otherwise it
  regenerates about 1 per 0.25 s. At 0 the player is out of breath: stamina 0.1, `FL_OUT_OF_BREATH`, sound, and no regeneration for
  5 s (0x100F54B5–0x100F55EC). **A jump costs 15** (0x100E514A, same constant 0x101CEB90 as the dodge); a dodge costs **15**.
- Dodge: commands **`dodgeforward` / `dodgebackward` / `dodgeleft` / `dodgeright`** (0x10019820…) → 0x100DDE70. Needs dodge enabled,
  not frozen, not ducking or holding IN_DUCK, `waterlevel < 3`, stamina ≥ 15 and the 1 s cooldown (+0x2048); 600 u/s burst.
  The `IN_LEFT` bit alone does nothing on the server.
- **Never send `dodgetoggle`**: it flips `m_bDodgeDisabled` (+0x1EE9) and prints "Dodge disabled".
- Sprinting disables firing, sights and reload; aiming disables sprinting. Keep stamina above ~20 in a fight (dodge + sledgehammer).
- Engine option: `m_bInfiniteStamina` (+0x21E8) removes all costs (checked at 0x100F52DB, 0x100F53E9, 0x100DDEFE, 0x100E8BA1).

### 4.5 Commands and inputs to keep out of the companion's vocabulary — CERTAIN

| Command / input | Why |
|---|---|
| `weapontoggle` with the phone | opens the phone UI (`VGUIMenu 200` + `Phone`, 0x10053D80) |
| `inventoryuse <weapon slot>` | key-only; prints an error |
| `inventorydrop`, `inventorycombine`, `inventorydualwield`, `inventorydeletenamed` | lose or merge items (inventory note) |
| `dodgetoggle` | disables dodging |
| `knockout` | mapper-only punch (`0x100E4C30`) |
| `aitest`, `fire`, `muschi*`, `laugh`, `nerdrage`, `vote*` | debug, griefing or no-ops |
| impulses other than 0 | 99 logo, 100 debug print, 201 spray, 204 HUD resend; 76/90–92/103–107/195–202 debug (0x100DC0D0) |
| IN_ATTACK2 with the switchblade | toggles stance (1.5 s lockout) |
| IN_CANCEL held with the rifle | scope flickers on/off |
| IN_ATTACK held with the phone | light toggles every second |
| IN_JUMP while dead | respawns once `+0x3D4` has passed (saves note §2); leave respawn to the party layer |
| `yesnoactivate`, `padlockrotate`, `telephone*`, `telescope*`, `computercheck`, `phonenumber`, `keypadnumber`, `+use` on puzzle entities | story UI the human should drive |

`adlib 0|1|2` (0x1001A832, MP only, 1 s cooldown) plays "Follow me!/Help me!/Found something!" voice lines with an `AdLib`
marker: harmless and useful for the companion.

### 4.6 Other pitfalls

- **Inventory space.** Weapons, syringes, the phone and story items share 6 slots (3 after `c_bridge`'s `bagremove`). A full
  inventory refuses pickups. Give the companion only what it uses: switchblade, one gun, syringe (phone is the starter kit).
- **Donator/unlock gates.** `weapon_tmp` pickup checks donator status (DefaultTouch 0x1011C816–0x1011C86B; in MP `0x100E4D60`
  tests player +0xC4); `weapon_famas`/`weapon_axe` via `GiveNamedItem` check unlocks; `weapon_mp5` is mapper-only. What +0xC4 holds for
  an engine-made client is UNKNOWN.
- **Magazine reloads waste rounds** (§1.4).
- **Gunfire is noise.** Each shot sets `m_iWeaponVolume`/`m_iWeaponFlash` (+0x2294/+0x229C) like HL, so monsters hear the companion
  (LIKELY HL behaviour).
- **Engine note for a fake-client companion:** `SV_InactivateClients` (`sv_frame.c:966`) drops every `FL_FAKECLIENT` client on a
  level change (CERTAIN, engine source), so the companion must be re-created after each changelevel and re-equipped from the
  party ledger.

---

## 5. The companion's control loop (summary of the recipe)

```
each server frame, for the companion player P (engine-built usercmd):
  if dead: no buttons (party layer respawns); return
  if fuser3 == 10 or activeWeapon.m_iSuicideState == 2: buttons = IN_ATTACK on odd frames, 0 on even; return
  if fuser3 != 0 and cannot move after a fall: ClientCommand("tapgetup")
  if FL_FROZEN or viewing a camera: no attack buttons
  dark area and pev->frags != 32 (MP): ClientCommand("flashlight") (rate-limit 1 s)
  heal check (§2.5) -> syringe sequence; partner hurt or down -> syringe IN_ATTACK2 / CPR (hold IN_USE 5 s)
  choose weapon: gun with ammo for ranged targets, switchblade/nightstick/axe within ~30 u
     if not active: ClientCommand("inventoryequip <slot>") once; wait for m_pActiveItem/m_bWeaponWaitingToDeploy/nextPrimary
  aim: viewangles at the target's chest (HL pitch: positive = down)
  gun: optional tap IN_CANCEL once to aim (no sprinting); fire = IN_ATTACK on one frame, off the next, while
       weapon.m_flNextPrimaryAttack <= time; TMP/VP70 may hold
       clip == 0 -> IN_RELOAD one frame (or just keep attacking: empty-fire auto-reloads); wait out m_fInReload
  melee: hold IN_ATTACK at <= 30 u; sledgehammer only with stamina >= 8
  movement: no IN_RUN in combat; no jumping while shooting (a jump costs 15 stamina and lowers the gun);
            dodge with "dodgeleft"/"dodgeright" when stamina >= 15
  ammo low: engine writes m_rgAmmo[idx] or gives ammo_* (with IN_USE / iuser4 = 1), or sets m_bInfiniteAmmo
```

---

## 6. Summary table

| Topic | Mechanism (key VA) | Who is affected | Hazard with 2 players | Recommended engine treatment |
|---|---|---|---|---|
| Equip | `inventoryequip` → EquipInventoryItem 0x100E40E0 → SelectItem 0x100F1A50, deploy in PostThink 0x100EDFB4 | sender | NONE | drive via `pfnClientCommand`; resolve slot from `script` (+0x110) vs `inventorySlot` |
| Fire | ItemPostFrame 0x1011D560; semi edge (glock 0x10046E00) | sender | NONE (no FF to real clients) | toggle IN_ATTACK per usercmd; respect sprint/jump/fuser3 gates |
| Aim | IN_CANCEL edge, `ironsights_toggle` "1" (0x10047C00) | sender | NONE | tap IN_CANCEL once |
| Reload | Reload 0x100475B0, DefaultReload 0x1011C560, clip zeroed | sender | NONE | reload only when empty / safe; or `m_bInfiniteAmmo` |
| Ammo | `m_rgAmmo` +0x2540, GiveAmmo 0x100E04D0, MP box cap = 3 mags | sender | NONE | write `m_rgAmmo` or allow `ammo_` in the give helper |
| Melee | per-weapon Primary; 32–40 u; sledge stamina ≥ 8 | sender | NONE | close to 30 u, hold IN_ATTACK |
| Friendly fire | FPlayerCanTakeDamage 0x100C8700 | FL_FAKECLIENT victims | WRONG-PLAYER damage: human can hurt a fake-client companion | clear the companion's FL_FAKECLIENT only inside a human's `SV_RunCmd` scope |
| Self-heal | syringe 0x1007B040 → 0x1007AAB0 → 0x100DCA10 | sender | NONE | syringe sequence; or write +0x21D4 |
| Partner heal/revive | syringe IN_ATTACK2 0x1007B110 (MP), CPR hold IN_USE | partner | NONE | use it; prefer CPR when safe |
| Health cap | +0x60C, PostThink 0x100EDC2B | per player | DESYNC (cap only on `c_asylum1` toucher) | (already noted elsewhere) |
| Light | `flashlight` cmd → frags 32 (0x1001C3A3); client draws frags&0x29 | all viewers | NONE (frags overwritten by flag changes) | send `flashlight`; re-check frags |
| Suicide struggle | crazybitch 0x10055860 → glock 0x100478E0/0x10047B70 | grabbed player | STUCK→DEATH for a bot that holds a button | mash IN_ATTACK edges while `fuser3 == 10` |
| Fall freeze | +0x1ED0/+0x1ECC, `tapgetup` 0x1001A33C | sender | STUCK-PLAYER for 3 s | send `tapgetup` |

---

## 7. Open questions only a live test can answer

1. **Edge timing:** at the server's usercmd rate, does alternating IN_ATTACK every usercmd give one shot per press for the glock, and
   does a single usercmd with IN_ATTACK set always register (no dropped edges when two usercmds run in one server frame)?
2. **Reload loss:** confirm that a glock reload with 10 rounds left ends with 15 in the clip and 10 fewer rounds in total.
3. **Fake-client friendly fire:** if the companion is a `pfnCreateFakeClient`, does the human's glock actually damage it, and does
   clearing its `FL_FAKECLIENT` only during the human's `SV_RunCmd` stop that without side effects (lag compensation runs in the
   same scope)?
4. **Coop flashlight on a bot:** does the human's client draw the companion's flashlight beam from `frags = 32` (renderer 0x10065260),
   and in which direction (the bot's `v_angle`/angles)?
5. **Iron sights effect:** how much do sights reduce spread (`m_AccuracyVector`), i.e. is aiming worth the time for an AI at 5–15 m?
6. **Syringe on a partner:** does the 48-unit trace hit a crouched or moving partner reliably, and does the `ViewLock`/`fuser3 = 25`
   freeze the human for 1.5 s as the code implies?
7. **Donator status of an engine-made client** (player +0xC4): can the companion pick up `weapon_tmp`?
8. **Melee reach in practice** (32 u from the eye): which story monsters can the switchblade reach without the companion clipping
   into them, and does the hit think fall back to a hull trace like HL's crowbar?
9. **Crazybitch:** does it pick a bot as readily as a human, and is 15 alternating presses within 4 s reliably met by the usercmd rate?
10. **`tapgetup`:** which landings set `m_bShouldFallFreeze` in MP, and does a bot recover cleanly after the command?
