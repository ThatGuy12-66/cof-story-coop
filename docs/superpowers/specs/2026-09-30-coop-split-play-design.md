# Story co-op, piece 1: split play without sequence breaks

- **Status:** v2, rewritten 2026-09-30 after static research of the retail game DLL. It awaits the user's review. v1 was approved in conversation.
- **Scope:** piece 1 of 4. Later pieces are designed separately:
  - 2: story moments (cutscenes, phone calls, host-only scenes)
  - 3: death, revive and save/resume
  - 4: polish (skippable intro, balance for two, endings)
- **Research this rests on**, in `docs/research/`, with every claim traced to a VA in the stock `hl.dll` (SHA-256 `0036B91C…0E63`):
  - `2026-09-30-hl-inventory-re.md`: pickup, drop, combine, key use, removal, client sync
  - `2026-09-30-hl-exits-coop-re.md`: `ChangeLevelNow`, the game's own co-op exits, ScreenFade and ProFont, death and revive
  - `2026-09-30-hl-documents-story-state-re.md`: notes, codes, objectives, endings, per-player state

## Goal

Two players can split up inside a map, each work on a different objective, and never break the
story. It should feel like the single-player campaign:

- Either player can open any lock, type any code and read any note the pair has found.
- Nobody is dragged out of what they are doing by the other's exit, door or scripted teleport.
- Nobody gets stranded: a player left behind can always get back to their partner.

## Decisions (made by the user)

| Question | Decision |
|---|---|
| Exit reached while the partner is elsewhere | **Wait + warp.** The exit holds until both are at it; the other can warp over. |
| Keys and puzzle items | **Both get it.** Whoever finds it, both carry it; if it's used up, it's gone from both. |
| Door follow | **Only if right beside you:** within 128 units (about 3 m). |
| Warp | **Any time,** to wherever the partner is. |

## What the research changed from v1

1. **A player can never hold two copies of one item.** `AddInventoryItem` (`100DAD40`) has an exact
   duplicate check; a second pickup "succeeds" and deletes the world copy. The ledger is therefore a
   **set** (party has it or not), not a count. No story map hands out one item twice.
2. **Drops, combines and key use are client commands** (`inventorydrop <slot>`,
   `inventorycombine <a> <b> <result>`, `inventoryuse|inventoryequip <slot>`). They all pass through
   the engine's `SV_ExecuteClientCommand` before the game's `ClientCommand` (`10018D60`). The engine
   can snapshot the sender's slots around the command and see exactly what happened. v1's guess
   based on items appearing on the floor is gone.
3. **The removal path exists and keeps the UI in sync.** `inventorydeletenamed <path> 0` is a
   `ClientCommand` handled by `DeleteInventoryItemNamed` (`100DDA70`), which sends the `Inventory`
   message. The client keeps its own copy of every slot, so writing a slot directly would desync it.
   Every change must go through the game's own code.
4. **Key use consumes the key only from the user.** `UseInventoryItem` (`100E4410`) clears the
   sender's slot unless the lock keeps keys: door or valve spawnflag 1, button 0x400. The partner's
   copy must be removed by us.
5. **Respawning wipes story items** (`CBasePlayer::Spawn`). The co-op carry store saves only
   weapons. Without this design a death loses keys, even solo, which can soft-lock the campaign.
6. **Codes are per player.** Keypads accept only the typing player's own `m_iKeypadNumber`
   (`KeypadNumber` `100E5890`). Computer and padlock digits are random per player on every spawn and
   are never saved. Some code writers target the activator, others the first player. In co-op a code
   the host found cannot be typed by the partner. This is a real sequence break, so piece 1 now covers
   it.
7. **Notes, objectives and code notes are per player:**
   - Reading a note archives it only for the reader (`AddDocument` `100DA6D0`).
   - Objectives go only to the first player (`cof_objective` `100A4B00`).
   - Code notes go only to the first player (`cof_addcodenote` `100A6790`).

   Sharing them is part of "either of you can do any objective", so piece 1 covers them.
8. **The bag loss (`c_bridge` `cof_clearitems` spawnflags 5)** wipes the activator only and puts
   only them into three-slot mode.
9. **The game's own co-op exit already works like our design.** `trigger_coop_changelevel` waits until
   the players inside are at least the alive participants (`alive && fuser4==0`), with no timeout.
   Its door tells a lone player "Please wait until everyone is ready!" (ProFont, colour 2). We reuse
   that wording and colour.
10. **Holding at `pfnChangeLevel` is safe for the story maps.** `ChangeLevelNow` (`10109D40`)
    re-fires `target`, `killtarget` and `changetarget` on every call. None of the 197 story exits has
    any of them, so re-requests repeat nothing but a `CHANGE LEVEL:` console line. An exit that does
    carry targets is never held (see guard below).

## Architecture

All behavior lives in the engine's opt-in story co-op layer (`cof_story_coop 1`, hash-gated retail
DLL). No map, `hl.dll` or `client.dll` changes. There are four new units and one edited unit. Each has
a pure state header that the native offline harness can drive, in the style of
`sv_story_transition_state.h`.

```
engine/server/
  sv_cof_retail.c/.h     the ONLY place that knows retail offsets and VAs: slot read, three-slot flag,
                         code fields, objective fields, ending flags, document slots, m_hDocument;
                         run a game ClientCommand as a player; call AddDocument; send ProFont,
                         ScreenFade and AddNote by registered name
  sv_cof_exit.c/.h       exit hold: at-exit test, pending scripted exit, prompts, escape hatch
  sv_cof_exit_state.h    pure readiness decision
  sv_cof_share.c/.h      story item ledger: command-scoped diffs, frame diffs, deliveries, removals
  sv_cof_share_state.h   pure event classification and set ledger
  sv_cof_state.c/.h      shared story state: codes, digits, code notes, documents, objectives,
                         ending flags, three-slot mode, bag-loss scope
  sv_cof_party.c         + warp, door and teleport follow radius, cut-off prompt
hooks: sv_game.c   pfnChangeLevel -> SV_CoF_ExitRequest; pfnAlertMessage -> rate-limit held "CHANGE LEVEL:"
       sv_client.c SV_ExecuteClientCommand -> SV_CoF_PartyClientCommand (cof_warp) and
                   SV_CoF_ShareCommandBegin/End around every command the game DLL handles
       sv_save.c   (existing) transition capture and arrive
```

`SV_CoF_PartyFrame` stays the single per-frame driver and calls the exit, share and state frames
after the existing party steps. The existing `sv_cof_cheats.c` retail offsets are not duplicated:
shared constants move into `sv_cof_retail.h`.

### 1. Exit hold (`sv_cof_exit`)

**Request.**
- `pfnChangeLevel(level, landmark)` in story mode with a non-empty landmark calls
  `SV_CoF_ExitRequest(level, landmark)`, which answers "go" or "hold".
- Not held: landmark-less requests, which include the team-wipe restart, `RestartCampaign`,
  `cof_changelevel` and co-op doors, and console `changelevel`/`changelevel2`.

**Guard.** The exit's source-map entry is read by `SV_CoF_MapExitModels`, extended to also report
`target`, `killtarget` and `changetarget`. If any of them is present, the answer is always "go"
(logged `exit-unholdable`). No story exit has them today.

**At the exit.** A player is at the exit if either of these holds:
- their absbox touches this exit's `trigger_changelevel` (matched by brush model), or a `trigger_*`
  whose target names it; or
- they are within 128 units of the waiting player.

**Waiting player:** a player touching the exit (walk-in), or the stored waiter (scripted).

**Ready** when every counted player is at the exit. Counted players are connected, spawned and
non-fake. A dead player holds the exit, as the user decided, and the escape hatch covers a long wait.
A player who is still connecting holds until spawned or dropped. With one player, the answer is
always "go".

**Walk-in exits** (137): nothing is stored. The trigger re-requests on every touch frame
(`dmgtime` guard: once per frame), and each request is re-evaluated. Stepping out ends the hold.

**Scripted exits** (60, nobody in a volume when the request arrives):
- Store one pending `(level, landmark, waiter)`, where the waiter is the player nearest the source
  landmark.
- The exit frame replays it with `SV_QueueChangeLevel` once ready.
- A newer request replaces it, and a generation change clears it.

**Prompts** go by ProFont, `MSG_ONE_UNRELIABLE`, refreshed every 2 s while held:
- To the waiter, colour 2: `Please wait until everyone is ready! (<partner> can press J to join you)`.
- To the others, colour 1: `<waiter> is at the exit. Press J to join.`

**Escape hatch.** The waiter presses J 10 s or more after the hold began. The exit goes, and arrival
regroup (`SV_CoF_PartyTransitionArrive`) places the partner.

**Console spam.** While held, the identical `CHANGE LEVEL: %s %s` alert is printed once per hold
instead of every frame.

**Logs:** `cofstory phase=exit-hold|exit-ready|exit-forced|exit-cancelled|exit-unholdable`.

### 2. Story item ledger (`sv_cof_share`)

**Story items:** descriptors directly under `inventoryitems/` (63 files: 61 `item_key`,
`item_glocktaclight`, `item_phonebattery`). Weapons, ammo and `outofit/` stay per player in piece 1.

**Ledger:** `party = set of descriptors`, kept across story transitions. On a fresh session it
starts from whatever the players hold.

**Per-player bookkeeping:**
- `slots[1..6]` snapshot.
- `set_down` set: items this player dropped on purpose, which are not re-delivered to them.
- `in_flight`: a delivery issued less than 1 s ago.

**Command-scoped events.** `SV_CoF_ShareCommandBegin(cl)` snapshots the sender's slots before the
game's `ClientCommand`, and `…End(cl)` diffs after it:

| Command | Diff | Event | Party action |
|---|---|---|---|
| `inventorydrop N` | slot N emptied | **dropped D** | none. Add D to the sender's `set_down`. |
| `inventorycombine A B R` | A and B emptied, A = R | **combined** | `party −= {A,B}`, `party += R`. Others: remove A and B, deliver R. |
| `inventoryuse`/`inventoryequip`/`yesnoactivate` | story slot emptied | **used D** | `party −= D`. Others: remove D. |
| any other command | loss or gain | as for frame diffs below | |

Commands the engine itself issues (removals) are flagged, so they are never re-classified.

**Frame diffs** (every 0.1 s) for changes that happen outside a command:

| Observation | Event | Party action |
|---|---|---|
| gain D matching this player's in-flight delivery | delivered | clear it |
| gain D, D not in party | **found D** (pickup, `cof_giveitems`) | `party += D`. Others: deliver D, plus ProFont `<name> found the <nice_name>`. |
| gain D, D in party | re-picked | Remove D from `set_down`. |
| losses in the frame a player (re)spawned or joined | reset | Re-deliver the whole party set, and clear `set_down`. |
| other loss D (script: `cof_clearitems`, `PreSave` tape) | **used D** | `party −= D`. Others: remove D. |

**Delivery.**
- Only to a spawned, alive player with a free slot (1..3 if `m_bThreeSlots` `+0x1E9C`, else 1..6),
  checked before trying, so the game's own "Inventory full" never fires.
- `SV_CoF_StoryGiveItemEx(cl, <descriptor classname>, D)` spawns the item with `message = D` and
  the player uses it. That runs `KeyitemUse` → `PickupInventoryItem` → `AddInventoryItem`, sends
  `Inventory`, and the client shows its own "You got the …".
- It is confirmed only by D appearing in a slot. The helper already frees the entity if it was not
  taken.
- No free slot: pending, and the player is told once in ProFont colour 5: `<name> found the
  <nice_name>. Make room to carry it.` It is delivered when a slot frees.

**Removal.** The engine runs `inventorydeletenamed "<D>" 0` as that player through the game's
`ClientCommand`. It is flagged as engine-issued.

**Logs:** `cofstory phase=item-found|item-delivered|item-deferred|item-used|item-removed|item-dropped`. A combine logs as its inputs `item-used` plus the result `item-found`, and a respawn's re-delivery logs as `item-delivered`.

### 3. Shared story state (`sv_cof_state`)

| State | Retail location | Rule |
|---|---|---|
| Keypad code | `m_iKeypadNumber +0x2040`, `m_iKeypadType +0x2044` | When any player's value changes, write it to all players (including 9999, "used"). Whoever types, the code matches. |
| Computer and padlock digits | 4 ints `+0x235C..+0x2368` | The party digits are the first valid set seen. Write them to any player whose digits differ, for example after a spawn re-randomises them. The computer's code note then matches everyone's padlock. |
| Code notes | `cof_addcodenote` sends to the first player only | When a player's document slots gain a code note, give the others the same entry: the placeholder in their first free slot plus the same `AddNote` payload, isCode=1. |
| Notes (documents) | `documentsstruct` via `AddDocument` `100DA6D0` | Around the `closedocument` command: read the sender's `m_hDocument` (`+0x630`) before, then call `AddDocument(other, doc)` for each other player. It is a thiscall invoked through a `__fastcall` shim (`ecx = player`). It dedupes, saves and sends `AddNote`. This also fixes password notes, which archive only for the first reader. |
| Objectives | `m_sObjectiveText +0x1E90`, `m_sCustomObjectives +0x420` (first player only) | Copy them from the first player to the others when they change. Their own think then sends `Objectives`. |
| Ending flags | `+0x4CD..+0x4CF` (set on all connected by `cof_ending`) | OR them across players on any change, join or respawn, so a late joiner is not missing a flag. |
| Three-slot mode | `m_bThreeSlots +0x1E9C` | A player joining or respawning after a partner lost the bag gets it through the game: a temporary `cof_clearitems` with spawnflags 4, used by that player. That sets the flag and resends the slots. |
| Bag-loss scope | `cof_clearitems` with `(spawnflags & 3) == 1` | At map activation, set spawnflag bit 2 so the wipe and three-slot mode apply to every player, as retail supports. This is `c_bridge` "bagremove" in the story. |

All reads and writes go through `sv_cof_retail` with the existing private-data size and back-pointer
checks. The retail DLL hash gate stays mandatory.

### 4. Doors, teleports and warp (`sv_cof_party` edits)

- **Doors** (`CoF_PartyWatchDoors`): capture a companion only if it is within 128 units of the owner
  when the door begins. Today there is no distance limit.
- **Scripted teleports** (`CoF_PartyTeleports`): companions are the partners within 128 units of
  `teleport_from`. Today it is the reverse.
- **Cut-off prompt**, ProFont colour 1, `<name> went somewhere you can't follow. Press J to join.`,
  sent to players left behind when:
  - a door event completes and its door entity was removed; or
  - a scripted teleport moved the owner.

  The ending door (target `endingcheck`, which moves only its opener to `ending_exitN`) falls into
  this case until piece 4.
- **Warp:** new client command `cof_warp`, handled in the engine before the game. Launcher configs add
  `bind j cof_warp`; J is unbound in the shipped config.
  - **Rules:** the caller must be alive, spawned and not mid-door. The target is the nearest alive
    partner not mid-door.
  - **Placement:** `CoF_PartyLanding` beside the target, then `CoF_PartyMove`, matching the target's
    yaw.
  - **Exit hold interaction:** during a hold, the waiter's J is the escape hatch (after 10 s), and
    the other player's J warps them beside the waiter, which releases the exit.
  - **Fade:** one `ScreenFade` (size 10: duration, hold and flags as ×4096 shorts, then rgba):
    black, fade-in 0.5 s, sent as the move happens. The screen blinks, the way the retail co-op door
    fades.
  - **Cooldown:** 2 s.
  - **Logs:** `warp` or `warp-refused reason=…`. The refusal is also shown in ProFont, for example
    `No room next to <partner>.`

## Build order

The piece ships in four stages, each playable and testable on its own:

1. **`sv_cof_retail` plus the live fact checks** (top of the check list below). If a check fails,
   the affected rule changes before anything depends on it.
2. **Item ledger.** This alone fixes keys for split play and death soft-locks, solo included.
3. **Exit hold, warp, and door and teleport radius.**
4. **Shared story state:** codes, digits, notes, objectives, ending flags, bag loss.

## Failure modes

- **An item delivery the game refuses:** it stays pending and is retried every 5 s (logged
  `item-deferred`). It is never assumed delivered.
- **A removal command that fails** (the item isn't found): logged once. The player keeps a stale copy,
  which can't open anything already opened.
- **A player disconnects mid-hold:** they stop counting, and the hold is re-evaluated.
- **A pending scripted exit when the map changes another way:** cleared on the generation change.
- **The first player (host) leaves:** first-player-only retail state (objectives, code notes,
  `m_iDoctorWeapon`) then targets the next-lowest player. Piece 1 accepts this, since the host
  leaving ends a listen session anyway.
- **An unexpected retail layout** (any private-data size or back-pointer check fails): that feature
  switches off for the session, logged `state-disabled feature=…`. It is never written blindly.

## Verification

- **Native harness scenarios**, one set per unit:
  - Exit: solo goes at once; a partner elsewhere holds; walking out cancels; a scripted exit is
    stored and replayed; a warped partner within 128 units releases the exit; a dead partner holds;
    a disconnect releases; the escape hatch works only after 10 s; a newer request replaces the
    pending one; a generation change clears it; a guarded exit (with targets) is never held;
    landmark-less requests pass.
  - Share: each row of both tables above; engine-issued removals are not re-classified; a full
    slot defers, then delivers; three-slot capacity; respawn re-delivery; the ledger survives a
    transition; `set_down` is respected.
  - State: code mirroring (activator writes and first-player writes); digit repair after a respawn;
    objective copy; ending OR; the bag-loss spawnflag rewrite; the three-slot catch-up.
  - Party: door and teleport radius; cut-off prompt; warp placement, refusal and cooldown; warp
    during a hold.
- **Existing suites stay green:** 111 tests as of 2026-09-29.
- **Report tool:** counts the new phases; `exit-forced`, `item-deferred`, `warp-refused` and
  `state-disabled` are warnings.
- **Live check list,** with both copies on this PC unless marked solo:
  - **First, the facts static analysis could not settle:**
    - `inventorydeletenamed` issued by the engine removes an item and updates the partner's UI.
    - An engine delivery shows "You got the …".
    - The engine-issued `ScreenFade` blinks.
    - An `AddDocument` call from the engine archives a note for the partner.
    - An engine-sent code `AddNote` shows the code.
  - Solo: die with a key and respawn with it; the bag loss in `c_bridge` (via chapter start).
  - `c_start` / `c_start2`: the host logs into the computer and the partner opens the padlock (codes
    and digits); the password note appears for both.
  - `c_apartment1`: split up; one finds the key while the other waits at its door and uses it;
    keypad, note and objective for both.
  - `c_basement`: fixed-code keypad and code note for both.
  - A fuse map (`c_college2`, `c_stad` or `c_subway2st1`): use-up removes it from both.
  - Exits: a walk-in held until a warp; the escape hatch; a scripted exit replayed.
  - Doors: taken far apart (no pull) and together (follow). A scripted teleport with the partner
    far away gives the cut-off prompt, then a warp.

## Out of scope, recorded for later pieces

- **Story moments (2):**
  - `cof_phonecall` rings only the activator, and is dropped if they have no phone.
  - About ten cutscene and menu entities (`cof_clothesmenu`, `cof_greyfade`, `cof_lookat`,
    `cof_strangle`, …) act on the first player; `cof_hint`, `cof_camerazoom` and `cof_simonspeak`
    act on the activator.
  - `trigger_camera` scenes (311 in 33 maps).
- **Death (3):**
  - Respawn: jump respawns after 60 s; auto-respawn after `sv_auto_respawn_time`.
  - CPR revive: `sv_coopcprtime`, revived with 30 HP.
  - A team wipe restarts the map with a landmark-less changelevel, which resets hub history.
  - No multiplayer saves.
- **Polish (4):**
  - The ending door moves only its opener.
  - Weapons are per player, so the `c_asylum1` P345 hand-over needs the carrier.
  - Weapon, ammo and health sharing; skippable intro; balance.
