# hl.dll inventory system: static RE (Cry of Fear 1.6)

Target: `runtimes/enhanced/cryoffear/cl_dlls/hl.dll` (SHA-256 `0036B91C…0E63`), cross-checked against `client.dll`.
This is static analysis only: a capstone linear sweep of `.text`, string and global xrefs, and vtables recovered from the exported entity factories.
All VAs assume base 0x10000000. `string_t` is an offset from `gpGlobals->pStringBase` (`[[0x10223188]+0x98]`).
Every function listed below is MSVC `__thiscall` with `ecx = this`.

Confidence tags: **STATIC-CERTAIN** means the disassembly was read end to end. **STATIC-LIKELY** means the behaviour was inferred from the call pattern, the strings or the HL SDK layout. **UNKNOWN** means it was not established.

## 0. Reference map

**Layout facts.** These come from the DLL's own `entvars_t` TYPEDESCRIPTION table at 1021AAE0 and the player save table at 10217FE8.

- **entvars fields used below:**
  - `spawnflags` +0x1A4, `flags` +0x1A8, `owner` +0x19C
  - `target` +0x1CC, `targetname` +0x1D0, `netname` +0x1D4, `message` +0x1D8
  - `armorvalue` +0x1C0, `noise2` +0x1F4, `noise3` +0x1F8
  - `iuser1` +0x248, `iuser2` +0x24C, `fuser3` +0x260
  - `pContainingEntity` +0x20C
- **Player fields:**
  - `inventorySlot[7]` +0x21FC; `inventorySlotModels[7]` +0x2218 and `inventorySlotClassname[7]` +0x2234 are only ever zeroed.
  - An unsaved 4th array at +0x2250 is only zeroed and printed as "Client Slot %i".
  - `m_bThreeSlots` +0x1E9C.
  - Unsaved byte +0x1EF8 is the "silent pickup" flag, written as byte 5 of the Inventory message and then auto-cleared.
  - `m_iTapes` +0x1EF4, `m_bInPhoneCall` +0x1EE8.
  - `m_iQuickSlotSlot[1..3]` +0x3DC/+0x3E0/+0x3E4; `m_sQuickSel1..3` +0x204C/+0x20CC/+0x214C.
  - `m_fInitHUD` +0x24CC.
- **CBaseEntity vtable (CoF):**
  - Spawn #11 (+0x2C), KeyValue #13 (+0x34), ObjectCaps #16 (+0x40)
  - IsAlive #47 (+0xBC), IsPlayer #52 (+0xD0)
  - Think #56, Touch #57, Use #58 (+0xE8); the Use member pointer `m_pfnUse` is at +0x80
  - Player UpdateClientData is #150; player Killed is #27 (STATIC-LIKELY)
- **CGameRules vtable:** IsMultiplayer +0x20, IsDeathmatch +0x24, IsCoOp +0x2C, PlayerKilled +0x74. The singleplay rules (vt 101DE3B0) return 0 from +0x20; multiplay (vt 101D608C) returns 1.
- **Engine functions:** `g_engfuncs` base is 0x10222F48 (MessageBegin 0x10223000, WriteByte 0x10223008, WriteString 0x10223020, Cmd_Argv 0x10223094).

| VA | Name I assigned | Signature / notes |
|---|---|---|
| 10018D60 | ClientCommand | Handles `inventoryequip/use/drop/combine/check/deletenamed/dualwield`, `quickselset1-3`, `yesnoactivate` |
| 100DAD40 | AddInventoryItem | `BOOL (string_t desc, string_t model, string_t classname)`, ret 0xC. The model arg is ignored. |
| 100E2F80 | PickupInventoryItem | `BOOL (CBaseEntity *item)`, ret 4. Shows "Inventory full" on failure. |
| 100E2240 | HasInventoryItem | `BOOL (const char *path, BOOL bRemove)`, ret 8. Exact `strcmp`, first match only. |
| 100DD990 | ClearInventorySlot | `void (int slot, BOOL bAll)`, ret 8. Zeroes the 4 arrays and sends Inventory(slot, ""). |
| 100DDA70 | DeleteInventoryItemNamed | `void (string_t path, BOOL bAll)`, ret 8 |
| 100E4360 | SendInventorySlot | `void (int slot, string_t desc, int unused)`, ret 0xC. Sends the `Inventory` user message. |
| 100E42F0 | IsInventoryFull | `BOOL ()` over 1..3 or 1..6 |
| 100E4EE0 | IsSlotEmpty | `BOOL (string_t s)` returns `s == 0` |
| 100DFF70 | GetItemDescription | `const char *(const char *key, const char *path)`. Calls LOAD_FILE_FOR_ME and parses on every call. |
| 100E4410 | UseInventoryItem | `void (int slot)`. Key consumption; see §5. |
| 100E40E0 | EquipInventoryItem | `void (int slot)`. `item_key` goes to UseInventoryItem; `weapon_*` goes to select. |
| 100E3460 / 100DE090 | DropInventoryItem / DropKeyItem | `void (int slot)` |
| 100E3020 | CombineInventoryItems | `void (int a, int b, const char *result)`, ret 0xC |
| 100E3010 | InventoryCheck | `ret 4`: an empty stub |
| 100DDC40 | CloseInventoryMenu | Sends `VGUIMenu(0xDE, 0)` |
| 1004DED0 | CKeyitemModel::KeyitemUse | `m_pfnUse` of `item_key` (vt 101B1644) |
| 100179F0 | CBaseEntity::CreateWithMessage | `(classname, string_t message, origin*, angles*, edict owner, string_t model)` |
| 100F6CA0 | CBasePlayer::UpdateClientData | Handles the `m_fInitHUD` resend |

**User messages** (from the RegUserMsg sequence at 100E63B8):

| Message | Id global | Size | Where it is sent |
|---|---|---|---|
| `Inventory` | [0x102265E4] | -1 | Only from 100E4360 |
| `QSlotClear` | [0x10226634] | 2 | |
| `AmmoInv` | [0x10226680] | 3 | Registered but never sent |
| `QSlotUpdate` | [0x10226690] | 2 | |
| `InventChck` | [0x102266B4] | 0 | |

One correction to the brief: the exports say **10017AB0 is `CBasePlayerAmmo::DefaultUse`** and 10017AC0 is `CBasePlayerItem::DefaultUse`. Story items use neither. They are picked up through `KeyitemUse`.

## 1. Pickup (`item_key` and other story items)

**The flow is `item_key` Use → KeyitemUse (1004DED0) → PickupInventoryItem (100E2F80) → AddInventoryItem (100DAD40).** STATIC-CERTAIN.

- **Spawn (1004E160).**
  - The descriptor is **`pev->message`** (e.g. `inventoryitems/apartmentkey.txt`). If it is empty the key logs "CoF: Key with no item .txt" and removes itself.
  - The model is read from the descriptor's `3dmdl` when `pev->model` is 0.
  - It sets `renderfx=99`, SOLID_TRIGGER and MOVETYPE_TOSS.
  - spawnflag 1 removes the key on one difficulty value. spawnflag 2 sets `iuser1=3`, the three-step "Should I take it?" prompt.
- **KeyitemUse.**
  - Requires `pActivator->IsPlayer()` and a use cooldown at +0x100.
  - **It fires `pev->target` (activator = the key itself, USE_TOGGLE) on every use attempt, before the inventory check**, so the target fires even when the inventory is full.
  - It then calls `PickupInventoryItem(this)`. On success it sets `armorvalue=0` and calls `UTIL_Remove(this)` (10119900). The inventory check applies only to the activator.
- **PickupInventoryItem.**
  - If the classname is not `weapon_*` it calls `AddInventoryItem(pev->message, pev->model, pev->classname)`.
  - For weapons it uses the item's +0x110 descriptor instead.
  - If Add fails it calls `ShowMessage(player, "Inventory full", 2, 0)` (10119810), returns 0, and the key stays in the world.
- **AddInventoryItem.**
  - It first scans for the **first empty slot**, 1..3 when `m_bThreeSlots` is set, otherwise 1..6. If there is none it returns 0 with no message; the message is the caller's job.
  - For non-`weapon_` classnames it then runs a **duplicate check**:

```
100DADDF  push 0                          ; bRemove = FALSE
100DADF9  push eax                        ; STRING(desc)
100DADFA  call 0x100e2240                 ; HasInventoryItem(path, FALSE)
100DAE01  jne  0x100dae14                 ; already held -> skip the store, still succeed
100DAE08  mov  [ebx+esi*4+0x21fc], edi    ; inventorySlot[free] = desc (same string_t, not re-allocated)
100DAE0F  call 0x100e4360                 ; SendInventorySlot(slot, desc, 0)
100DAE14  ...  'weapons/item_get.wav' (CHAN_ITEM) unless global [0x10226480] != 0
```

- **A player cannot hold two copies of the same descriptor through this path.** The check is exact and case-sensitive, and it runs only after the free-slot test.
  - A second pickup of an already-held path **returns TRUE**, so the world entity is removed and the item is silently lost. No Inventory message is sent, but the sound still plays.
  - With a full inventory, even a duplicate fails with "Inventory full".
  - Weapons skip the duplicate check.
  - Combine (§4) can in theory create a duplicate because it has no check.
- **In multiplayer with `weapon_syringe`** it broadcasts `Syringe` (MSG_ALL). Nothing else in pickup depends on multiplayer.

**What the client gets on pickup.** It gets the `Inventory` message (MSG_ONE) with this payload, plus the sound:
`byte slot, byte 0, string descriptor, byte m_bThreeSlots, byte silent(+0x1EF8), byte hasTacGlock(100E24A0)`.
The client copies the path into its own slot buffer and shows "You got (the) %s" from `nice_name` unless the silent byte is set. STATIC-CERTAIN.

**Other givers:**

- **`cof_giveitems`** Use (100A6BC0).
  - With spawnflags&1 it calls `AddInventoryItem(message, 0, 0)`. On failure it shows "Inventory full" and creates an `item_key` with the same `message` at the player's origin, owned by the player.
  - Without that flag it calls `GiveNamedItem`. Flag 0x10 and the default path set the silent/no-sound globals.
- **`cof_skipmanager`** (100A7680) gives items with the same full-inventory fallback. STATIC-LIKELY.
- **`item_glocktaclight` and `item_phonebattery`** Use handlers call PickupInventoryItem.
- **`weaponbox` and `CBasePlayerItem::DefaultTouch`** (1011C7D0) handle weapons only.

## 2. Removal paths

**(a) `inventorydeletenamed` is a client command handled by ClientCommand (1001B822).** STATIC-CERTAIN.

- It is not stufftext and not a user message. Neither `client.dll` nor any map contains the string; nothing in the shipped content sends it, so in practice it is a console/debug command.
- Syntax: `inventorydeletenamed <descriptor path> <all>`.

```
1001B84B  call Cmd_Argv(1) ; esi = ptr
1001B861  sub  esi, [gpGlobals+0x98]    ; string_t = argv pointer - pStringBase (NOT ALLOC_STRING'd)
1001B88E  call 0x100dda70               ; DeleteInventoryItemNamed(name, atoi(argv2) != 0)
```

- **DeleteInventoryItemNamed.**
  - It logs "Trying to delete %s!".
  - With `bAll`, it clears **every** slot 1..N and ignores the name.
  - Otherwise it clears the **first** slot whose string exactly equals the name (full path, case-sensitive).
  - It zeroes all 4 parallel arrays and sends `Inventory(slot, "")`, so **the client UI stays in sync**.

**(b) `cof_clearitems`** Use (100A60C0, vt 101C3CD0). STATIC-CERTAIN.

1. It returns immediately unless the **activator is a player**. A non-player activator makes the whole entity a no-op, even the all-players flags.
2. If `pev->message` contains `weapon_` (`strstr`), the message is treated as a **weapon classname**.
   - The player's weapon list is searched for `pev->classname == message`.
   - Quick slots referencing it are cleared with `QSlotClear`.
   - Dual-wield is fixed up.
   - `DeleteInventoryItemNamed(weapon->+0x110 descriptor)` and `DeleteInventoryItemNamed(weapon->pev->message)` are called, and the weapon is destroyed.
   - Otherwise `DeleteInventoryItemNamed(message, 0)` runs, which means the message must be the **exact descriptor path**, not `map_name`.
   - A descriptor path such as `inventoryitems/weapons/weapon_glock.txt` contains `weapon_`, so it takes the weapon branch and removes nothing.
   - In the shipped maps `message` is always a weapon classname.
3. `(spawnflags & 3) == 1` clears everything **for the activator**:
   - `ClearInventorySlot(1, bAll)`
   - `RemoveAllItems` (100F0BA0)
   - quick slots and `QSlotClear(1..3)`
4. `(spawnflags & 3) == 3` does the same for **all players** (`UTIL_PlayerByIndex` loop).
5. `spawnflags & 4` sets **`m_bThreeSlots = 1`** and resends slots 1..3. With `spawnflags & 2` it applies to all players, otherwise only the activator.
   - **spawnflags 5 (1|4)** clears the activator's whole inventory and puts only the activator into three-slot mode. `c_bridge` "bagremove", `c_the_stairway3` and `c_the_stairway5` use it.
6. If the activator had the cassette tape, `m_iTapes != 0` and `targetname == "bagremove"`, it re-adds `inventoryitems/cassettetape.txt` silently.

Every removal goes through ClearInventorySlot/DeleteNamed, so **the client is updated**.

**(c) Helpers.** STATIC-CERTAIN.

- `HasInventoryItem(this, const char *path, BOOL bRemove)` at 100E2240 is the real "remove by name" helper. With bRemove it calls `ClearInventorySlot(i, 0)` on the first match.
- `DeleteInventoryItemNamed(this, string_t, BOOL)` is at 100DDA70.
- `ClearInventorySlot(this, int, BOOL)` is at 100DD990.
- Other callers with remove=1: `CYesNo::Yes` (100A9E30), and `PreSave` (100EF1D0), which removes the tape when `m_iTapes` reaches 0.

## 3. Drop (`inventorydrop`)

**The client sends `inventorydrop %i\n`**. The client's own slot copy is not touched; it only hides the menu buttons. ClientCommand then calls `DropInventoryItem(atoi(argv1))` (100E3460). STATIC-CERTAIN.

**Guards, checked in order:**

1. Not in a phone call, not on a ladder (`movetype == 5`), and `IsAlive()`. **There is no bounds check on the slot index.**
2. An empty slot plays `inventory_no.wav`.
3. A descriptor classname of `weapon_mobile` gives "You can't drop your phone" unless a world flag (`[0x10223190]` → pev+0x24C == 1) is set.
4. The syringe cannot be dropped while in use. `weapon_radio` gives "You can't drop your radio".
5. **`noitemdropping`**: worldspawn KeyValue (10120DEE) sets world `spawnflags |= 8`. The drop code tests edict 0 `spawnflags & 8` and then blocks **every** drop, weapons and keys alike. It shows world `noise3` if set, otherwise "I better keep this until I've explored this area. It doesn't look safe.", and plays `no.wav`.
   - The descriptor key `droppable` is **never read**. The strings `droppable`, `pickup_sound`, `combined_with` and `quick_picture` do not exist in either DLL.

**Story item (descriptor classname `item_key`).** It plays `inventory_click.wav` and calls `DropKeyItem(slot)` (100DE090):

```
100DE109  push 'item_key' ... call 0x100179f0 ; CreateWithMessage("item_key", slot desc, eye pos (origin+view_ofs), v_angle, owner=player edict, model 0)
100DE129  mov [pev+0x1d8], slot desc          ; pev->message = descriptor (same string_t)
          angles.x = angles.z = 0
100DE148  mov [pev+0x1c0], 666.0f             ; armorvalue = 666: "dropped by player" marker
100DE152  call 0x100dd990                     ; ClearInventorySlot(slot, 0) -> Inventory(slot, "")
100DE159  call 0x100ddc40                     ; CloseInventoryMenu -> VGUIMenu(0xDE, 0)
```

- The new `item_key` spawns normally: the model comes from `3dmdl`, and anyone can pick it up again through KeyitemUse, which does not check `owner`.
- The 666 marker is read only by `cof_entityrestore` (100A3D10). In capture mode that entity copies every `armorvalue==666` entity (message, origin, classname, clip; up to 50) into the **activator's** saved `transferredentity_*` arrays. In restore mode (`pev->impulse` set) it re-creates them. This is the single-player "dropped items follow you across a transition" mechanism.
- `item_glocktaclight` and `item_phonebattery` go through `ClearInventorySlot` and then `CBaseEntity::Create(name, origin, angles, owner)`.
- `weapon_*` has a separate path that clears quick slots with `QSlotClear` and handles the weapon drop.

## 4. Combine (`inventorycombine`)

**Client (10043AD5).** It sends `inventorycombine <slotA> <slotB> <combined_result read from ITS OWN copy of slotA's descriptor>`. STATIC-CERTAIN.

**Server (100E3020).**

- **Guards:** not in a phone call and not on a ladder. `strlen(result) > 1`, otherwise "COMBINE ERROR: No combine result". Both slots must be non-empty. Both indices must be in 1..6 (ignoring `m_bThreeSlots`) and different.
- **Validation:** `combined_result` is read from **both** server-side descriptors and `strcmp`'d. If they differ, the server plays `no.wav`.
- **The server never checks the client-supplied result against those values.**
- **Special results:**
  - `glockwithtaclight`: the taclight slot is cleared, the glock weapon gets +0x1F8 = 1, and that slot is resent. The resend's byte 6 tells the client the glock now has the light.
  - `phonewithbattery` works the same way with `mobile_battery.txt` and the mobile.
- **Generic result:** `ClearInventorySlot(A)`, `ClearInventorySlot(B)`, then `slot[A] = ALLOC_STRING(result)` (the client's string) and `SendInventorySlot(A, new)`.
- **What the client is told:** three `Inventory` messages (A cleared, B cleared, A set). The last one triggers the "You got the …" text, and the server plays `item_get.wav`.

## 5. Key consumption

`inter_door` Use (10048F70) **never looks at inventories**. For a locked door it shows `m_sLockedMsg` (+0x194) and plays `noise2`. Its only other unlock route is `useunlock` (+0x1A0), which unlocks another door named by that key. Keys work the other way round: **the player uses the item from the inventory** (`inventoryuse %i` or `inventoryequip %i` → UseInventoryItem 100E4410). STATIC-CERTAIN.

**How UseInventoryItem finds the lock:**

1. It reads `map_name` from the slot's descriptor (disk read). An empty value gives "Error! Item script seems to have no nice name!".
2. It traces along the view from the eye. The hit entity must have use caps (`ObjectCaps & 0x38`).
3. If the trace finds nothing it scans `UTIL_FindEntityInSphere(origin, 64)`.
4. The classname tests are **exact**.

```
100E4642  ecx = STRING(door->m_sLockedBy[+0x190]); strcmp(map_name, ecx)
100E4674  test byte [door.pev+0x1a4], 1      ; spawnflag 1 = keep key
100E4687  call 0x100dd990                    ; else ClearInventorySlot(slot, 0) -> REMOVED + Inventory msg
100E468F  call 0x10048e40                    ; CInterDoor::Unlock(player): noise3 sound, unlockedmsg, m_sLockedBy = m_sLockedMsg = 0
          ... click.wav, CloseInventoryMenu
```

**The key is consumed per lock type:**

- **`inter_door`:** yes, unless the door has spawnflag 1. In the maps, 1 of 914 locked doors has it.
- **`func_valve`:** matches the item against `+0x180`. The only value used in the maps is lockedby "valve". Consumed unless spawnflag 1. Unlock is 10042CF0: sound, message, renderamt 255, lock cleared.
- **`func_button`:** needs the locked byte at +0x180 set; lock name at +0x18C. Consumed unless **spawnflag 0x400**; for example the spade with spawnflags 3233 is kept. Unlock is 10015A20.
  - That function sends its message to `UTIL_FindEntityByClassname("player")`, the first player, not the user. This is a cosmetic co-op bug.

**Which inventory is used:** only the **sender of the command**. Unlocking changes door state globally; the other players keep their copies.

- **`inter_door_coop` cannot be unlocked by an item**, because the classname test is exactly `inter_door`. No shipped map puts `lockedby` on `inter_door_coop`.
- **`trigger_checkbrush`** has no item logic at all (KeyValues `usesound`/`master`/`checkdelay`; it only shows `message`).
- **`cof_yesno`** (Use 100A8140; KeyValues `weaponneed` +0xDC, `itemneed` +0xE0, `noitemmsg` +0xE4):
  - The Use checks the **activator**: `HasNamedPlayerItem(weaponneed)` (100E2380) and `HasInventoryItem(itemneed, FALSE)`. Either failure shows `noitemmsg`; success sends YesNo.
  - The answer `yesnoactivate <entindex>` calls `CYesNo::Yes(answering player)` (100A9E30). With spawnflags&1 that runs **`HasInventoryItem(itemneed, TRUE)`**, so the item is consumed from the answering player. `weaponneed` is only checked, never removed.
  - The targets fire either way. No shipped map sets `itemneed`.

## 6. Client sync

- **`Inventory`** (payload in §1) is the only path that tells the client about slot contents.
  - Every server-side slot write sends it: Add, ClearSlot, DeleteNamed, Combine, three-slot resend, and the coop persistence loaders (100C5300/100C6570, all 6 slots).
  - `UpdateClientData` (100F6CA0) resends everything when `m_fInitHUD` is set: `InventChck`, then `Inventory` for slots 1..N, `QSlotClear(i,1)` and `QSlotUpdate`.
  - STATIC-CERTAIN.
- **The client keeps its own copy:** `char[128]` per slot at viewport `0x1018A0C0 + 0x2C64 + (slot-1)*0x80`, via 1006CDD0. The Inventory hook is 1006E760 → 1006FF40, which copies the string.
  - That copy is used for pictures, `nice_name` and `combined_result`, and to pick the `inventoryuse/equip/drop` indices.
  - **So a server-side write to `inventorySlot[]` without the message leaves the UI stale.** The UI would show the old item, and commands would act on whatever the server holds at that index. STATIC-CERTAIN.
- **`InventChck` (size 0)** is sent only on `m_fInitHUD`. The client (10044160 → 10044A30) re-lays out the inventory panel depending on whether the level name starts with `cof_`. It carries no item data. STATIC-LIKELY.
- **`QSlotUpdate`** carries `byte quickslot(1..3), byte inventorySlot`. **`QSlotClear`** carries `byte quickslot, byte flag` (0 = clear).
  - Quick slots accept only `weapon_` descriptors on both server (`quickselset1-3`) and client, so story items never appear there.
- **`AmmoInv` is never sent.** STATIC-CERTAIN.

## 7. Multiplayer differences

- **Pickup, drop, use, combine and delete** have no IsMultiplayer or IsCoOp branch that affects story items. The only MP branch is the syringe broadcast. **Items are never given to other players by the game.** STATIC-CERTAIN for the listed functions.
- **Death (`Killed`, 100E59F0) does not touch the inventory.** Nothing is dropped or cleared. STATIC-LIKELY.
  - **Revive** (the player Use override 100F88C0 with `sv_coopcprtime`, handled in `MultiplayerPlayerDeathThink` 100E9AB0) keeps the inventory.
  - If the death leads to a restart (+0x4E8 set and a timer running out), the MP branch **saves every player through the coop state store and calls `CHANGE_LEVEL(current map)`**. The store is the global `[0x10222E10]`, `SavePlayer` 1002A480.
- **`CBasePlayer::Spawn` (100F4090) always wipes the inventory:**

```
100F4216  push 1 / push 1
100F4294  call 0x100dd990        ; ClearInventorySlot(1, bAll=TRUE) on every Spawn (respawn, MP changelevel, restart)
```

- **The coop state store keeps weapons only.** `SavePlayer` loops slots 1..6 and stores a slot only when its descriptor classname starts with `weapon_`; story items are skipped. It also stores health, stamina, quick slots and stats.
  - The loaders (10029090, which calls 100C6570; `sv_coop_nosave`) restore weapons with `GiveNamedItem` and resend all slots.
  - **Net effect in MP: story items are lost on changelevel (`cof_changelevel`, `inter_door_coop_changelevel`) and on death-restarts.**
  - In SP they persist through the save table (`inventorySlot` is FIELD_STRING ×7).
  - STATIC-LIKELY. The store functions were read and the string lists in the loaders contain no story-item handling.
- **The `m_bThreeSlots` reset path also differs.** It is only ever set to 1, by `cof_clearitems` sf 4; no code resets it to 0. In MP a fresh player object starts at 0; in SP the value is saved. STATIC-CERTAIN.

## 8. Slots

- **Six usable slots, 1..6; index 0 is unused.** Every loop runs `esi=1 .. cmp esi,6 / jle`, and the client's slot switch covers 1..6. STATIC-CERTAIN.
- **`m_bThreeSlots` limits every loop to 1..3.** That includes Add, Has, Clear(bAll), Delete(bAll), IsFull, the resend, and the `cof_clearitems` all-clear.
- It is also sent as byte 4 of `Inventory`. The client then hides slots 4-6 and switches to `640_inventory_three.tga`. Items already in slots 4-6 stay in the server array, but they are invisible and skipped by all loops.
- **Drop, use and equip do no bounds checking.** Combine only checks 1..6.

## Implications for engine-side item sharing

1. **Give through the game's own code.**
   - Call `AddInventoryItem(player, ALLOC_STRING(path), 0, 0)` at 100DAD40 (`__thiscall`, ret 0xC, BOOL).
   - It picks a slot, applies the duplicate check, sends `Inventory`, shows "You got the X" and plays `item_get.wav`.
   - Use the exact descriptor path, case-sensitive (e.g. `inventoryitems/Videotape.txt`), taken from the source `item_key`'s `pev->message`. Always `ALLOC_STRING` it; never reuse a `Cmd_Argv` or other volatile pointer.
2. **Silent give.** Set player byte +0x1EF8 = 1 before the call; SendInventorySlot clears it. Set the dword `[0x10226480] = 1` around the call to suppress the sound.
3. **Handle a full inventory yourself.**
   - Add returns FALSE when there is no free slot (3 in three-slot mode) and shows no text.
   - Do what `cof_giveitems` sf1 does: `ShowMessage("Inventory full")`, then `CreateWithMessage("item_key", path, origin, angles, owner=player)`.
   - Alternatively, spawn a `cof_giveitems` (spawnflags 1, `message=path`) and Use it with the player as activator. That uses only entity-level calls.
4. **Re-giving is a safe no-op.** A path the player already holds returns TRUE without touching slots or the UI; only the sound plays. A full inventory still fails.
5. **Remove through the game's own code.** Call `HasInventoryItem(player, path, TRUE)` (100E2240) or `DeleteInventoryItemNamed` (100DDA70). Both clear the first match and send `Inventory(slot, "")`.
   - Never zero `inventorySlot[]` directly.
   - `cof_clearitems` is unsuitable for story items: the message must be the exact path, the path must not contain `weapon_`, and the activator must be a player.
6. **Detect changes.** Every slot mutation, drop and combine included, ends in SendInventorySlot, so there are two options:
   - Hook engine-side `MessageBegin(MSG_ONE, value at [0x102265E4], …, ed)`, then `WriteByte slot`, the `0` byte and `WriteString path`. An empty path means cleared.
   - Diff `inventorySlot[1..6]` per player each frame. That is 6 dwords and is the recommended ground truth.
7. **Attribute the cause.** Classify an event by intercepting the client command before the game's ClientCommand (`inventorydrop/use/equip/combine <n>`, `yesnoactivate`), or by observing a side effect in the same frame:
   - A drop leaves a new `item_key` with `armorvalue == 666`, `owner == player` and `message == path`.
   - A use clears `m_sLockedBy` on `inter_door` +0x190, `func_valve` +0x180 or `func_button` +0x18C.
   - A combine clears two slots and sets one.
8. **Mirror key consumption.**
   - The game consumes a key only from the player who used it, and only if the lock lacks its keep flag (door or valve sf 1, button sf 0x400).
   - The door unlock itself is global.
   - For shared keys, remove the path from every other holder after you detect a consumption, and skip that when the keep flag is set.
9. **Drops duplicate shared items.**
   - A drop creates a real world `item_key` while the other players still hold copies.
   - One option is to block drops map-wide at runtime: set world `edict(0)->v.spawnflags |= 8`, with optional `noise3` text.
   - The other is to treat a drop as "remove from all players", with the world entity as the single source. A later pickup of a held path destroys the entity, so a duplicate heals itself for that holder.
10. **Replay the pickup, not the use.** `KeyitemUse` fires `pev->target` on every use attempt, so giving copies through Add correctly avoids re-firing pickup triggers.
11. **Re-deliver from a ledger in MP.**
    - Spawn wipes the inventory and the coop persistence keeps weapons only.
    - After respawn, `cof_changelevel` / `inter_door_coop_changelevel` or a death-restart, re-give the story items once the client is spawned and active.
    - Delivering after the first `UpdateClientData` InitHUD resend is safe; Add sends its own messages anyway.
    - Revive keeps the inventory.
12. **Validate slot indices.** Drop, use and equip have no bounds check, so filter injected or relayed commands to 1..N, where N is 3 when `m_bThreeSlots` is set.
    - Beware that `inventorydeletenamed <path> 1` wipes everything regardless of the path.
