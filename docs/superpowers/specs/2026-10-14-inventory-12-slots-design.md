# 12-slot inventory for story co-op: design

Date: 2026-10-14. Status: design approved in conversation section by section; this written spec awaits the user's review. Nothing is implemented.

## Why

During the 2026-10-13 demo the user's six slots filled up (phone, switchblade, test-gun M16, syringe, hall key, Glock) and they asked: "make the inventory bigger … all the items aren't fitting". Offered a second page of six, they chose a real bigger grid.

Decisions the user made (2026-10-13/14):

| Question | Answer |
|---|---|
| Kind | A real bigger grid, not pages |
| Size | 12 slots |
| Scope | Story co-op only; single-player Cry of Fear and its saves unchanged |
| Approach | Our own inventory screen + `hl.dll` patched in memory (not patching the retail screen, which tops out near 9) |
| Layout | 4×3 smaller boxes on the left; the right half as today |

Success: in story co-op a player can carry 12 items, see all of them, and equip, use, drop, combine and quick-select any of them; the items survive level changes, respawns and checkpoints; nothing crashes when someone runs an older build.

Non-goals: single-player; more than 12; the `cof_` maps' UNIFORM layout (not a story map); changing the story's three-slot sections; the retail Polish text beyond regenerating its backgrounds.

## Research this builds on

- `evidence/inventory-grid-20261013/server-inventory-re.md` (hl.dll `0036B91C…0E63`): every instruction that touches the slot arrays, the patch set, our engine's six-slot dependencies with file:line.
- `evidence/inventory-grid-20261013/client-inventory-re.md` (client.dll `D2A04641…9C00`): the inventory panel `CRECBInventory`, its hard-coded layout, the slot-string accessor, click handling, the slot-6 menu overlap, the `c_nightmare` gate. Layout overlay: `client-inventory-layout-overlay.png`.
- `docs/research/2026-09-30-hl-inventory-re.md`: the inventory system (pickup, use, drop, combine, messages).

All of it is static analysis; the live checks are in "Testing".

## Shape

```
 server (listen host)                         each client (our engine)
 ───────────────────                          ────────────────────────
 hl.dll, patched in memory                    Inventory messages ──► engine slot store [1..12]
   (12-slot loops)       ── Inventory(n) ──►     n 1..6 also go on to client.dll (unchanged path)
 engine story co-op                              n 7..12 stay in the engine
   12-slot gate, capability check             client.dll slot accessor ──► engine slot store
   inventory command filter                   FreeVGUI: 4×3 grid replaces the left half of
   snapshots/ledger/party at 12 slots            CRECBInventory; right half stays retail
                     ◄── inventoryequip/drop/combine/dualwield, quickselsetN ──
```

## 1. Server: 12 slots in `hl.dll`

**In-place growth.** `inventorySlot[7]` at player `+0x21FC` can grow over the three arrays after it (`+0x2218` models, `+0x2234` classnames, `+0x2250` client copy). Only zeroing code and six unreferenced functions touch them. Slots 1..12 end at `+0x222F`. Slots 7..12 sit on the `inventorySlotModels` save-table entry (count 7), so the retail save table and our checkpoint field table (`sv_cof_retail_inventory.c:9-11`) already save and restore them: no save-format or `COF_SESSION_VERSION` change, and old checkpoints load with slots 7..12 empty.

**Patch set** (server report §5; all same-length, VAs with base 0x10000000):

- Loop bounds `cmp r,6` → `cmp r,12` (9 sites): Add 100DADB1, Clear(bAll) 100DDA1B, DeleteNamed(bAll) 100DDB2B, DeleteNamed find 100DDBF3, Has 100E22ED, IsFull 100E4341, InitHUD resend 100F75A0, coop-file loader resend 100C64D0, map `.cfg` loader resend 100C6D70.
- Not patched: SavePlayer 1002A866. The retail coop record holds exactly six weapons; story co-op carries inventories through the engine's own snapshot instead (verify in the plan which story paths still reach SavePlayer).
- Range checks `cmp eax,5` → `cmp eax,11`: Combine 100E30C2 and 100E30CE. DualWield (100DEA0C/18) is refused in multiplayer before the check; leave it.
- NOP the 18 dead-array zero-writes (Clear single 100DDA3A/45/50, DeleteNamed single 100DDC0E/19/25, `[esi]` 100DD9C4/9FE/DDACE/DDB0E, `[esi+1C/38]` 100DD9CA/9D1/A04/A0B/AD4/ADB/B14/B1B). Left in, the bAll loops would zero slots 8..12 and, past them, `m_bUsedFirstSyringe` (+0x226C) and the morphine-trip fields.
- Unchanged: the live slot writes, the three-slot bounds (`cmp …,3`), the save tables, the allocation (0x281C).

**Two groups** (refined while planning, 2026-10-14). With the stock bounds, the retail clear code itself writes into slots 8..13 (`models[i]` is slot `i+7`), so a map at six slots would wipe items 8..12. Therefore:

- *keep* (the 18 NOPs): applied whenever story co-op is on, whatever the mode; harmless at six slots;
- *wide* (the 9 bounds + 2 combine checks): applied only on maps that run at 12.

**Mechanism.** The same as the two existing story-gate patches in `sv_cof_story.c` (`CoF_StoryWriteGate`): after the retail hash check, each site's expected bytes are compared, then written with `VirtualProtect` + `FlushInstructionCache`; all or none. They are reverted with the gate (`CoF_StoryCloseGate`), and the gate's integrity check covers them. Win32/x86 only, like the existing patches. A new module `sv_cof_inventory12.c/.h` owns the table, apply, revert and state.

**When it applies.** At map load, when the story gate opens, only if:

1. story co-op is on and `cof_inventory_slots` (new server cvar, default 12; 6 forces the old behaviour) is 12;
2. every byte check passes;
3. every connected human client has reported the capability (section 3). Fake clients (the AI companion) do not count.

Otherwise the map runs at six slots, exactly as today, and the server logs `cofstory phase=inventory12-off reason=<cvar|bytes|client userid=N|platform>` once. The mode never changes mid-map. Turning it off at a later map load (an older client is now connected) hides slots 7..12 for that map; they come back when 12 slots return, because the engine's data paths always carry all 12 (below). A client without the capability that tries to join a map running at 12 is refused at connect with a readable reason ("This game uses the 12-slot inventory; update Cry of Fear story co-op.").

**Command filter.** The retail handlers of `inventoryuse/equip/drop n` and `quickselset1-3 n` accept any `n` (server report §3) and `inventorycombine` reads before it checks. The engine filters these client commands to 1..N before the game's ClientCommand, N = 12, 6, or 3 in three-slot mode; a refused command is dropped with `cofstory phase=inventory-cmd-refused`. This also closes a retail out-of-range read/clear.

**Engine six-slot dependencies** (server report §4). `COF_RETAIL_SLOTS`/`COF_SHARE_SLOTS` become the maximum, 12 (the `#error` keeps them equal). Two kinds of use:

- Data paths always carry all 12, whatever the mode: snapshot capture/restore, respawn recovery, checkpoints, transitions. Nothing is lost on a map that runs at six.
- Rules that choose a slot or count free ones use the active count `SV_CoF_InventorySlots()` (12 or 6, 3 in three-slot mode where they test that today): share-ledger delivery and `CoF_SharePlan`, `SV_CoF_RetailFreeSlots`, the test gun and companion "2 free slots" checks.

Sites to change: `COF_PARTY_SLOTS`, `COF_RETAIL_PLAYER_NEED`/`COF_PARTY_PLAYER_SIZE` (≥ 0x2230), the `cof_story_status` dump (`index=0..12`), `CoF_MomentHasPhone`, the share ledger's `held[]` and `CoF_SharePlan`, `SV_CoF_RetailReadSlots/FreeSlots`. The test gun's and the companion's "2 free slots" rules stay.

**Three-slot mode** (`cof_clearitems` sf 4, the story's "bag lost" sections) stays as the retail game has it: slots 1..3 usable, 4..12 kept but hidden and skipped. The engine already spreads it to the whole party (`sv_cof_state.c:122`).

## 2. Client: the 12-slot screen

### 2.1 Engine slot store and message routing (`engine/client`, new `cof_inventory12.c`)

- The client engine keeps `char slots[13][128]` (index 0 unused), the client's copy of the server's slots.
- It already parses `svc_usermessage` before client.dll sees it. For the `Inventory` message (`byte slot, byte 0, string descriptor, byte three, byte silent, byte hasTacGlock`):
  - slot 1..6: passed on to client.dll unchanged (its "You got the X" strip, its quick-slot logic);
  - slot 7..12: kept from client.dll (its handler writes slot 7+ into read-only memory, a crash). The engine stores the string and, unless `silent`, hands client.dll a synthetic `ProFont` message (the game's own on-screen text message, as in "Picked up a glock magazine…") with the wording the client's handler would have used, built from the descriptor's `nice_name`;
  - slot 0 or > 12: dropped with a log line, in every mode. This safety net is on even when 12-slot mode is off.
- **client.dll accessor patch.** All of client.dll's slot strings are reached through one function, 1006CDD0 (`dec eax; cmp eax,5; ja` + a six-case table). The engine redirects it, after a client.dll hash check (`D2A04641…`) and an expected-bytes check, to an engine function that returns `slots[n]` for 1..12, and the existing read-only fallback otherwise. Then client.dll's own writers (the `Inventory` handler for 1..6, ResetHUD's clear of 1..6) and readers (`QSlotUpdate` for a quick slot holding slot 7..12, the hover picture) all use the engine store. Precedent: `cof_enter_hook.c` (an IAT patch with checks); this one is a code patch, so it uses the server mechanism (expect bytes, `VirtualProtect`, revert on shutdown).
- ResetHUD clears only 1..6 in client.dll; the engine clears 7..12 on the same message.
- **Capability.** When the accessor patch is in place and the UI library reports the grid panel (2.2), the client engine sets the userinfo key `_cofinv 12`; otherwise it is absent. The server reads it at connect and on userinfo change. The retail userinfo is already 248 of 256 characters, so `_cofinv` joins the "important" keys in `common/infostring.c` (it evicts a cosmetic key, as `password` does). A listen server asks its own client engine directly (`CL_CoF_Inventory12Ready`, a stub returning false in dedicated builds).
- **Mode.** The server tells each connected human client the map's mode with the client command `cof_inv_slots 12|6` (stufftext, the way `cof_story_changelevel` is sent), again after every map load; the client forgets it on `CL_ClearState`.
- **client.dll identity.** The SHA-256 code in `sv_cof_cheats.c` is shared through one new function (`COM_CoF_FileSha256Is`). client.dll is ASLR-relocatable: only the first 15 bytes of the accessor are relocation-free, so the check compares those at `base + 0x6CDD0` and the patch writes a `jmp rel32` computed from the real address.

### 2.2 The grid (FreeVGUI, `3rdparty/freevgui/platform/xash3d-fwgs/`, new `cofinventory.cpp`)

FreeVGUI already identifies `CRECBInventory` by RTTI each painted frame (`cofpanels.cpp`) and anchors client surfaces (`cofscale.cpp`). With 12-slot mode on (the engine tells it through a new `vguiapi_t` entry, like `CofPanelReport`/`CofOskCall`):

- **Hidden:** the panel's six slot buttons (+0x15C..+0x170), six slot pictures (+0x120..+0x134), the hover picture (+0x140), the item popup (+0xDC art, +0x100..+0x118 buttons) and the selection highlight (+0xF8). The offsets are client.dll-specific, so this runs only when the engine confirmed the client.dll hash.
- **Kept:** QUICK SELECT (pictures and buttons), NOTES, CURRENT OBJECTIVE, the background's right half, opening/closing, the `c_nightmare` and other open gates.
- **Added**, as children of the 748×480 background panel (+0x150) so scaling and hit tests follow it, added last so they are drawn and hit-tested above every retail child:
  - a grid panel covering the left 373×480, which paints the generated left-half art (2.3) over the retail background (the client re-sets +0x150's image on every `InventChck`, so swapping it would not stick), then a 4×3 grid of 72×72 boxes at x 36/114/192/270 and y 68/146/262: BAG over slots 1..8, POCKETS over 9..12; each draws the descriptor's `inventory_picture` (99×99 TGA) scaled to 72;
  - ITEM DESCRIPTION at the bottom left: the hovered item's `check_picture` (303×99), as today;
  - an item menu with the retail art (`inventory_items.tga` / `inventory_weapons.tga`) and buttons: EQUIP or USE (by `weapon_` in the descriptor path, as retail), COMBINE, DROP, and for weapons DUAL WIELD and quick 1/2/3.
- **Behaviour**, matching retail `SlotClicked` (10044670) and the popup actions (10043130):
  - EQUIP/USE → `inventoryequip n`; DROP → `inventorydrop n`; COMBINE → highlight, the next box completes `inventorycombine a b <a's combined_result>` if the target's `combined_result` is not empty, else `inventory_no.wav`; DUAL WIELD → `inventorydualwield a b` (refused by the server in multiplayer, as today); quick k → `quickselsetk n` and the quick picture (through the patched accessor, retail `QSlotUpdate` updates it).
  - Sounds: `inventory_toggle.wav` on open, `inventory_no.wav` on refusal.
  - **Fix for the slot-6 bug:** the menu opens beside the grid's right edge or below the box, never over another box; while a menu is open a click on any box selects that box (its menu replaces the open one); a click elsewhere closes it.
- Three-slot mode: only boxes 1..3 show, under POCKETS (the retail `_three` relabelling).
- Gamepads use the existing pad cursor; the buttons are ordinary VGUI buttons.

### 2.3 Art (`tools/make_inventory12_art.py`, new)

Generated on the player's machine from their own retail files, never shipped as copied art: reads `gfx/vgui/640_inventory.tga` and `640_inventory_three.tga` (and the Polish overlays under `languages/polish/overlay/cryoffear/gfx/vgui/`), and writes only the left part, 373×480, as `gfx/vgui/inventory12_left.tga` / `inventory12_left_three.tga` (+ Polish): the old frames and the moved labels are erased with the panel texture from the clean strip x 342..364, the POCKETS and ITEM DESCRIPTION labels (found by brightness, so translations work) and the description box are moved down, and 12 (or 3) frames of 72 px are drawn. The grid panel draws it over the retail background in 12-slot mode; the retail art is never modified. FreeVGUI has no file access, so images come through the engine (2.4). `Prepare-CampaignTest.ps1` (test copies) and the tester setup run the tool.

### 2.4 Engine ↔ UI library

One new engine-filled `vguiapi_t` entry, appended after `CofPanelAction`: `int (*CofInventory)( int op, int arg, void *buf, int size )`. Ops: the grid's slot count to show now (12, or 0 for the retail screen), three-slot mode, one slot's item (descriptor, picture, examine picture, combine result, classname, weapon flag; parsed and cached by the engine), send an inventory command (only `inventoryequip/drop/combine/dualwield`, `quickselset1-3`), play a local sound, load a TGA from the game filesystem as RGBA, and "the grid is available" from the library. `xash.dll` and `vgui.dll` must come from the same build, so they are installed together.

## 3. Failure handling (summary)

| Situation | Result |
|---|---|
| hl.dll bytes or hash differ | Six slots for the session; `inventory12-off reason=bytes`; no partial patch |
| client.dll bytes or hash differ, or the UI library lacks the grid | No `_cofinv`; the client is an "older client" |
| An older client connected at map load | That map runs at six; slots 7..12 kept but hidden |
| An older client joins a map at 12 | Refused at connect with a readable reason |
| A slot ≥ 7 message reaches a client without the patch | Cannot happen through the server rules; the client engine drops it anyway |
| Out-of-range inventory command | Dropped by the server filter, logged |
| Story co-op turned off / server shutdown | Server and client patches reverted |

## 4. Testing

Offline (the project's pattern: C harnesses in `tests/engine/` + `python -m unittest discover -s tests`), written test-first:

- Server patch table against the real `hl.dll`: every expected byte present; apply then revert restores the file image; one wrong byte → nothing written (skipped without local game files).
- Client accessor patch against the real `client.dll`, same three checks.
- Gate rules: cvar, platform, bytes, an incapable human client → off with the right reason; fake clients ignored; mode fixed for the map; join refusal while at 12; userinfo parsing.
- Client routing: 1..6 forwarded, 7..12 stored and never forwarded, 0 and > 12 dropped, the "You got the X" text for 7..12 unless silent, ResetHUD clears 7..12.
- Command filter at N = 12, 6 and 3.
- Engine six-slot sites at 12: snapshot capture/restore and respawn recovery keep slots 7..12; the share ledger delivers into 7..12 and reconciles them; party/status dumps; `CoF_MomentHasPhone` finds a phone in slot 9; three-slot mode hides 4..12; an old (format 2) checkpoint loads with 7..12 empty; a 12-slot checkpoint loads back.
- Existing harness and tool updates listed in the server report §4 (`cof_retail_harness.c`, `cof_share_harness.c`, `cof_party_harness.c`, `test_story_fixture.py`, `tools/verify_story_fixture.py`, …).
- Full suite green; a mutation check over the new rules; an independent read-only code review.

Live (the user's go-ahead first; disposable copies, `-Mode solo`, both clients ours), a checklist added to `docs/campaign-test-guide.md`:

1. `cof_story_status` shows `inventory12` on and 12 slot lines; `m_bUsedFirstSyringe`/morphine fields unchanged after a full clear (the residual RE risk).
2. Fill all 12 (test gun + story items); the 4×3 grid shows them; "You got the X" for slots 7..12.
3. Equip a weapon in slot 7..12, use a key from slot 7..12 on its door, drop, combine (Glock + tac light) across the 6/7 line, assign a quick slot from slot 9.
4. Open a menu, click the next box (the slot-6 case): it selects that box.
5. Level change, death + respawn, `cof_save`/`cof_load`, an autosave from before the change: slots kept, old checkpoint loads.
6. A three-slot section (if reachable) or `cof_story_probe` equivalent: boxes 1..3 only.
7. A client running the previous engine build (a copy kept as the "older client"): refused at connect with the message while a map runs at 12; when it is connected before a map loads, that map runs at six and slots 7..12 return on the next map at 12.
8. `c_nightmare`: the inventory still does not open (retail rule).

## 5. Installation

Build with the existing commands (`tools/build_enhanced.cmd`; FreeVGUI builds with the engine). Install only into the disposable copies (`evidence/story-live-20261012/{host,guest}`) through `Prepare-CampaignTest.ps1` with the expected hash, which also generates the art. The normal runtimes (`runtimes/enhanced`, `runtimes/enhanced-local-client`), the tester package and anything on GitHub stay untouched until the user says so. No game is launched without the user's go-ahead.

## 6. Risks and open points

- Residual RE risk on the server: unusual pointer arithmetic into the dead arrays cannot be excluded statically (live check 1).
- The retail coop record (`SavePlayer`, six weapons): any story path that still relies on it would drop weapons in 7..12. The plan must list which paths reach it in story co-op and cover them with the engine snapshot.
- `CHUDControl`'s message strip may paint over the inventory (client report §8); the grid's menu placement should avoid the strip's rows if it does.
- The FreeVGUI child offsets inside `CRECBInventory` are client.dll-specific; any other client.dll means no grid and no capability, by design.
- Scaled 99→75 px item pictures may look soft; acceptable for the first version, revisit with the user after the live test.
