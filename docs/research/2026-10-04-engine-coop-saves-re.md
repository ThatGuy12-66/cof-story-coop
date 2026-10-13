# Engine research B: carrying players, saving and loading with two players

- **Date:** 2026-10-04. Static reading only; nothing was built, launched or modified.
- **Source read:** `upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a`, branch `story-coop`, checkout HEAD `fb29635` (one commit past the deployed `bc3e91c`; the extra commit only adds `cof_story_trace`). All paths below are relative to that tree's `engine/` unless stated.
- **Companion:** research brief A (retail `hl.dll`) covers what the game does on death, revive, the team wipe and the co-op store. Where an answer here depends on the DLL, it says so and cites the existing notes in `docs/research/2026-09-30-*`.
- **Confidence:** CERTAIN = read in code. LIKELY = inferred from code plus a well-known SDK pattern or live evidence. UNKNOWN = not established.

---

## 1. How a story transition carries players today

### 1.1 Entry

- `pfnChangeLevel(level, landmark)` (`server/sv_game.c:1498`). In story mode it does not set the one-request latch (`:1508-1512`), and it asks the exit hold first (`SV_CoF_ExitRequest`, `:1530-1532`). Landmark-less requests are never held (`server/sv_cof_exit.c:89-90`). CERTAIN
- `SV_QueueChangeLevel` (`server/sv_game.c:713`):
  - A non-empty landmark makes the request "smooth".
  - Story mode validates it with `SV_CoF_TransitionValidate` (`:739-740`). That needs a unique `info_landmark` in both maps and no pending sign-on (`server/sv_save.c:114-128`).
  - Smooth is forced off only for non-story multiplayer (`server/sv_game.c:752-753`).
  - The request is queued as `COM_ChangeLevel(map, landmark)` (`:775-776`), which sets `GameState->loadGame = true` only when the landmark is non-empty (`common/host_state.c:139-149`). CERTAIN
- `SV_ExecChangeLevel` → `SV_ChangeLevel(loadGame, map, landmark)` (`server/sv_init.c:1141-1144`). `story = loadfromsavedgame && SV_CoF_StoryCoopEnabled()` (`server/sv_save.c:2508`). The story gate itself requires `svs.maxclients > 1` (`server/sv_cof_story.c:94`). CERTAIN

### 1.2 Capture: `CoF_BeginStoryTransition` (`server/sv_save.c:130-181`)

- **Tickets.** `CoF_TransitionBegin(&tickets, svs.spawncount + 1)` (`:139`). The epoch is the spawn count the *next* map will have, because `SV_SpawnServer` increments it (`server/sv_init.c:961`). CERTAIN
- **Who is captured.** Every client that is `cs_spawned`, not fake and has a valid edict gets `CoF_TransitionCapture(slot, userid)` (`:147-157`, `sv_story_transition_state.h:27-33`). A ticket is `{userid, phase}` indexed by slot. Health is not checked: a dead player is captured too. CERTAIN
- **`inventory_owner`.**
  - What it is: an array sized `svgame.numEntities` (`:145-146`).
  - What qualifies: every non-client entity whose classname starts with `weapon_` or `item_` (`:165-175`).
  - What it maps to: the 1-based slot of a captured player that is its `pev->owner` **or** its `pev->aiment` (`CoF_TransitionInventoryOwner`, `sv_story_transition_state.h:68-78`). Other owner links are deliberately not followed (inter_door uses owner as activator, `:163-164`).
  - CERTAIN
- **Party arrival bookkeeping.**
  - `SV_CoF_MapExitModels` + `SV_CoF_PartyTransitionCapture` record the leader and the strays (`:176-179`, `server/sv_cof_party.c:838-876`).
  - Leader rule: a player inside the exit wins; otherwise the player nearest the source landmark. Anyone more than 128 u from the leader is a stray.
  - CERTAIN

### 1.3 Save of the old level: `SaveGameState(true)` (`server/sv_save.c:1832-2036`)

- **Adjacency list.** `pfnParmsChangeLevel` (the DLL) builds it (`:1858`). If the DLL lists no connection with this destination and landmark, the engine reserves one (`:1860-1881`). `vecLandmarkOrigin` is the live source landmark. CERTAIN
- **Every edict is saved.**
  - `0..numEntities-1` go through the DLL's own `pfnSave(pent, pSaveData)` (`:1931-1944`). That is the game's TYPEDESCRIPTION save of each entity: the player's own fields (inventory slots, ammo, weapon pointers) and each weapon's own fields.
  - `FENTTABLE_PLAYER` is set for any `FL_CLIENT` edict (`:1943-1944`).
  - CERTAIN for the engine side. Which game fields are in the player and weapon tables is brief A's question 5. The inventory notes say `inventorySlot` is FIELD_STRING ×7 in the player save table (`2026-09-30-hl-inventory-re.md` §7).
- **Story flags.**
  - A captured player (slot + userid ticket) and every `inventory_owner` entity get `BIT(connection)`. Inventory additionally gets `FENTTABLE_COF_INVENTORY` (`0x08000000`, defined at `:28-30`) (`:1946-1959`).
  - The save is refused if one of them produced no data (`:1951-1956`), or if a captured slot's row is not a serialized player (`:1964-1976`).
  - CERTAIN
- **Files.**
  - `save/<map>.HL1`: tokens, ETABLE, header, adjacency, lightstyles and entity data (`:1997-2021`).
  - `save/<map>.HL3`: the removed-entity patch list (`:2023`).
  - `save/<map>.HL2`: client state (`:2029`).
  - CERTAIN
- **Hub history.** `cof_story_maps` gains the old map only after the save succeeds (`:2548-2549`). It is a 256-entry in-memory list (`:31`, `:41-49`). CERTAIN
- **Pre-teardown check.** Both snapshots must be readable before the world is destroyed (`:2552-2573`). CERTAIN

### 1.4 Teardown with the connections kept

- **Teardown calls.** `SV_InactivateClients` → `SV_FinalMessage("", true)` → `SV_DeactivateServer` (`server/sv_save.c:2575-2577`).
- **Message to clients.**
  - While `SV_CoF_TransitionActive()`, `SV_FinalMessage` sends the stufftext `cof_story_changelevel` instead of `reconnect` (`server/sv_main.c:1090-1098`).
  - Each client keeps its netchannel, slot and userid (`client/cl_main.c:4365-4378`). That handler needs `cls.state == ca_active`.
  - CERTAIN
- **Frames.** `SV_DeactivateServer` frees every client's `frames` (`server/sv_init.c:716-722`). The story branch reallocates them for kept connections (`server/sv_save.c:2579-2588`). Without that, `SV_ReadPackets` skips the client and the screen stays black (handoff root cause 1). CERTAIN
- **New map.** `SV_SpawnServer` drops every client to `cs_connected` and `SV_InitEdict`s each client edict, which frees the old player's private data (`server/sv_init.c:1055-1067`). CERTAIN

### 1.5 Load of the new level

- **Destination world** (`server/sv_save.c:2597-2608`):
  - If the destination is in `cof_story_maps`, it loads the hub snapshot with `LoadGameState(level, true)`.
  - Otherwise it runs `SV_SpawnEntities(level)` (a fresh map).
  - CERTAIN
- **Players and inventory are never taken from the destination's own snapshot.**
  - When a transition is active, `CreateEntitiesInRestoreList` asks `CoF_TransitionSkipSavedEntity(create_world, incoming, player, inventory)`. That is `(world || !incoming) && (player || inventory)` (`sv_story_transition_state.h:79-82`, `server/sv_save.c:1770-1779`).
  - Hub world loads therefore skip every player row and every `FENTTABLE_COF_INVENTORY` row.
  - CERTAIN
- **`LoadAdjacentEnts(oldlevel, landmark)`** (`server/sv_save.c:2354-2465`):
  - It walks the destination's adjacency list plus one extra entry for the old level, so one-way edges work (`:2369-2377`). Maps not in the hub history are skipped (`:2392`).
  - `cof_transition.incoming` is true only for the old level's snapshot (`:2393`).
  - For the incoming snapshot:
    - The landmark offset comes from the two recorded landmark origins (`:2410-2415`).
    - The level mask is `FENTTABLE_PLAYER | BIT(connection)`, plus any DLL connection bits to this map (`:2423-2431`).
    - `CreateEntityTransitionList` (`:2251-2345`) → `CreateEntitiesInRestoreList(…, create_world=false)`.
  - **Players** (ids `1..maxclients`) are created into `SV_EdictNum(id)`, the same slot index, and only if the ticket matches the slot, the slot's current userid and the epoch (`:1780-1788`, `:1803-1813`).
  - **Inventory rows** get new edicts (`:1814-1817`).
  - **Restore.** Each row then goes through the DLL's own `pfnRestore(pent, pSaveData, 0)` (`:2309`). Entity references (player ↔ weapons, owner, aiment) are table indices, so they resolve to the new edicts.
  - **Failures and solid checks.** A failed player or inventory restore is a `Host_Error` (`:2311-2312`). Player and inventory rows are exempt from the "in solid" suppression (`:2317`).
  - **Moved rows.** Each moved row is set to `FENTTABLE_REMOVED` (`:2332`), and the source's `.HL3` is rewritten (`:2436-2445`). The source snapshot therefore never hands the same player or items back on a revisit.
  - **Restored tickets** advance to `COF_T_RESTORED` (`:2326-2331`).
  - **Time base.** Time fields are rebased with `pSaveData->time = sv.time` (`:2404`).
  - CERTAIN
- **Missed slots.** Any ticket still `CAPTURED` after the loop is a `Host_Error` (`:2456-2459`). Then `SV_CoF_PartyTransitionArrive` places strays and players in solid beside the leader (`:2460-2461`, `server/sv_cof_party.c:905-962`). CERTAIN

### 1.6 Sign-on

- `SV_New_f` skips the game's `pfnClientConnect` for a restored client (`server/sv_client.c:1629`). CERTAIN
- `SV_PutClientInServer` treats `SV_CoF_TransitionRestoredClient(cl)` as restored (`:1349`):
  - It sends `svc_setangle` if `fixangle` is set and resets the weapon animation.
  - It skips `SV_InitEdict` and the game's `pfnClientPutInServer`.
  - It does not clear `sv.loadgame` or `sv.paused` while a transition is active (`:1385-1389`).
  - It calls `SV_CoF_PartyPlayerReady(cl, fresh=false)` (`:1417`).
  - CERTAIN
- `SV_Begin_f` → `SV_CoF_TransitionClientReady` (`:2192`, `server/sv_save.c:86-94`) marks the ticket ready. When nothing is pending, `CoF_TransitionResume` unpauses and logs `phase=ready` (`:70-79`). CERTAIN
- **A drop.** `SV_DropClient` → `SV_CoF_TransitionClientDrop` (`server/sv_client.c:635`). A deliberate reconnect from the same address does the same at `:446`. Either way the ticket is `DROPPED` and the transition can resume. CERTAIN

### 1.7 Answers

- **What carries the player and the owned items through the DLL's own save and restore:**
  - `SaveGameState`'s per-edict `pfnSave` (`:1941`).
  - The engine's connection and inventory flags (`:1946-1959`).
  - `CreateEntityTransitionList`'s `pfnRestore` (`:2309`), with the player recreated in its own slot edict and the items in new edicts.
  - Story items travel inside the player's own saved inventory slot strings. Weapons travel as separate `weapon_*` entities linked by owner and aiment. CERTAIN for the engine; LIKELY for the exact DLL fields (brief A).
- **What identifies "player X" on the way back:**
  - The ticket: **slot index** (saved table id − 1 = edict number − 1), **userid** and **epoch** (`CoF_TransitionMatches`, `sv_story_transition_state.h:34-37`, used at `server/sv_save.c:1783`, `:1948`, `:2329`).
  - The userid survives only because the connection is kept (`cof_story_changelevel`).
  - No name is used anywhere.
  - CERTAIN

---

## 2. Reusing it for one player across a reconnect

### 2.1 What happens today on a disconnect and a rejoin

- **`SV_DropClient`** (`server/sv_client.c:628-694`):
  1. Ticket drop (`:635`).
  2. The game's `pfnClientDisconnect(edict)`, only if the client was `cs_spawned` and `crash == false` (`:645-648`).
  3. State `cs_zombie` and the name cleared (`:658-659`).
  4. `cl->edict = NULL` (`:677`).
  - The **edict is not freed**: the old `CBasePlayer` private data and any items not removed by the game stay in the world. The zombie slot becomes `cs_free` on the next timeout pass (`server/sv_main.c:548-551`).
  - CERTAIN
- **What the game's `ClientDisconnect` does in Cry of Fear is UNKNOWN.** The HL SDK's multiplayer rules call `RemoveAllItems(TRUE)` from `ClientDisconnected` (LIKELY the same here; brief A should confirm).
- **A rejoin is a new connection.** `SV_ConnectClient` takes the first `cs_free` slot (`server/sv_client.c:280-289`, `:430-440`). It wipes the slot struct and assigns a **new userid** (`:459`, `:467`), and points the slot at `edict = slot + 1` (`:465`). CERTAIN
- **Same address and port.** A reconnect from the same address and qport/port reuses that slot **without calling `SV_DropClient`**, so no capture point and no `ClientDisconnect` (`:412-426`). CERTAIN
- **Spawn.** `SV_PutClientInServer` takes the fresh branch: `SV_InitEdict(ent)` frees whatever private data was there (`:1397`, `server/sv_game.c:1113-1125`), then the game's `pfnClientPutInServer` runs (`:1408`). The game's Spawn wipes the inventory (`2026-09-30-hl-inventory-re.md` §7, `100F4294`). CERTAIN (engine) / LIKELY (DLL)
- **What comes back today.** Story items come back through the ledger, as verified live 2026-09-30 when the rejoin got userid 4 and the Hall key. Weapons, ammo, clips and health do not.

### 2.2 What a per-player capture and restore would take

All the building blocks exist in `sv_save.c`. A one-player snapshot is the transition machinery narrowed to one player and the edicts it owns.

**Capture** (in memory, or as a file; see 4.4 for why a file in `save/` is attractive):

1. `pSaveData = SaveInit(...)`, then `InitEntityTable(pSaveData, svgame.numEntities)` (`server/sv_save.c:737-749`). Every edict gets a table row with `id = edict number`, so the DLL can turn any pointer into an index while saving.
2. Run the `pfnSave` loop of `SaveGameState` (`:1931-1944`) **only** for:
   - the player edict;
   - the entities `CoF_TransitionInventoryOwner` would assign to that player (classname `weapon_`/`item_`, owner or aiment equal to the player).
3. Write tokens, ETABLE and data as `SaveGameState` does (`:1983-2021`). With a minimal `SAVE_HEADER` (`entityCount`, `connectionCount 0`, `time = sv.time`), `LoadSaveData` (`:1237-1299`) and `ParseSaveTables(…, updateGlobals=false)` (`:1308-1345`) can read the file back unchanged.
4. Key it by `cl->name`, compared case-insensitively as `SV_UserinfoChanged` does (`server/sv_client.c:1846-1869`).
5. Remove the captured item entities from the world (`FL_KILLME`) unless the game's `ClientDisconnect` provably removes them. Otherwise they stay attached to an edict that the next occupant of that slot inherits (see 2.4).

**Restore** (a third branch in `SV_PutClientInServer`, next to the restored and fresh branches at `:1349`/`:1391`):

1. `SV_InitEdict(ent)` first. `SV_AllocPrivateData` re-initialises only a *free* edict (`server/sv_game.c:1235-1242`). Otherwise the HL SDK's `GetClassPtr` would reuse the leftover object (LIKELY), carrying unsaved fields over.
2. Recreate the player into the **new** client's edict:
   - Player: `SV_CreateNamedEntity(cl->edict, classname)`, with `pTable[savedPlayerId].pent` pointed at it.
   - Each item: `SV_CreateNamedEntity(NULL, classname)`.
   - Every other row stays `NULL`, so links to world entities restore as NULL (as in a transition).
   - The saved index may differ from the new slot. The DLL resolves references through the table (LIKELY HL SDK `EntityFromIndex`, which searches by `id`), so remapping works.
3. Restore each row with `pfnRestore(pent, pSaveData, 0)`:
   - Set `pCurrentData`/`currentIndex` as in `:2266-2268`.
   - Set `pSaveData->time = sv.time` (time rebasing, as at `:2404`).
   - Use landmark offset 0. If the rejoin happens on a different map than the capture, the saved origin is meaningless and party placement must move the player.
4. Fix engine-owned per-slot fields the save carried over:
   - `ent->v.netname = SV_MakeString(cl->name)`.
   - `ent->v.colormap = NUM_FOR_EDICT(ent)`: the fresh path sets it at `server/sv_client.c:1404`, and a restored copy keeps the old slot's value (LIKELY a rendering/model mix-up otherwise).
5. Skip the game's `pfnClientPutInServer` (its Spawn would wipe what was restored and hand out starter items). `pfnClientConnect` still runs in `SV_New_f`, as in a GoldSrc single-player load. Then call `SV_CoF_PartyPlayerReady(cl, fresh=true)` so the existing join placement puts them beside the partner (`server/sv_cof_party.c:232-254`).
6. The client's inventory UI is rebuilt by the game's own `m_fInitHUD` resend (`UpdateClientData`, `2026-09-30-hl-inventory-re.md` §6). LIKELY: restored players already get a working inventory after every story transition, verified live.

### 2.3 Hook points

| Point | Where | Why |
|---|---|---|
| Capture on a normal drop | `SV_DropClient`, after `SV_CoF_TransitionClientDrop` (`server/sv_client.c:635`) and **before** `pfnClientDisconnect` (`:647`) | The game's disconnect may strip the items. Note the `crash` path skips `ClientDisconnect` entirely. |
| Capture on a same-address reconnect | `SV_ConnectClient`, reuse branch (`:412-426`), before the `memset` (`:459`), when the slot was `cs_spawned` | No `SV_DropClient` happens on this path. |
| Restore | `SV_PutClientInServer` (`:1340`), new branch before the fresh path | It runs at `spawn` time, before baselines and sign-on data are sent, the same place the single-player load restore happens. |
| Placement | `SV_CoF_PartyPlayerReady(cl, true)` (`:1417`) | Existing join placement beside the partner. |
| Invalidate | `SV_CoF_TransitionReset(true)` (`server/sv_save.c:56-68`) | The same "new session" funnel that clears hub history and the ledger. |

Restoring after `cs_spawned` (at `SV_Begin_f`) would mean replacing a live player. It is not recommended.

### 2.4 Risks

- **Edict reuse keeps EHANDLEs valid** (CERTAIN). `SV_InitEdict` does not bump `serialnumber` (`server/sv_game.c:1113-1125`); only `SV_FreeEdict` does (`:1146`). Anything still linked to the old player's edict therefore looks linked to whoever takes that slot next. That includes leftover weapons with owner/aiment = that edict, and monsters' or doors' handles. If items are left behind, the next story capture classifies them as the new occupant's inventory (`server/sv_save.c:165-175`), which duplicates them.
- **Dangling pointers** (LIKELY). The HL SDK's weapons keep a raw `m_pPlayer`. If the player's private data is freed (`SV_InitEdict` on a new join) while its weapons still exist, they point at freed memory. That is harmless only if the game removed them on disconnect (UNKNOWN for CoF).
- **The slot can change.** A rejoin can land in another slot (first `cs_free`). Saved index ≠ new edict is fine through the table remap. `SV_CoF_StoryUserinfo` picks the Simon variant by slot, so the rejoiner could swap models (cosmetic).
- **Links to world entities restore as NULL:** `m_hDocument` +0x630, `m_pPadlock` +0x2350, ladder, tank, ground entity. The engine-side camera `cl->pViewEntity` is reset. Cross-player state in the snapshot (revive handles +0x2698/+0x26A0, restart pending +0x4E8; field list in `2026-09-30-hl-exits-coop-re.md` §4) may be stale. UNKNOWN; brief A.
- **Dropped keys count as owned** (LIKELY):
  - `DropKeyItem` creates the world `item_key` with `owner = player` (`2026-09-30-hl-inventory-re.md` §3).
  - The `weapon_`/`item_` + owner rule therefore counts a key lying anywhere in the map as the dropper's inventory.
  - This already happens in every story transition today. The key travels with its dropper, is landmark-translated, skips the in-solid suppression and is marked removed in the source map.
  - A rejoin capture would take it off the floor too.
- **Name keys:**
  - Duplicate names get ` (1)` appended among spawned clients (`server/sv_client.c:1846-1869`).
  - A name can change mid-game.
  - `cl->name` is cleared at drop (`:659`), so capture must read it first.
- **Mid-transition drops.** A player who drops while their ticket is pending has an entity restored in the new map but no client. Capture should handle "restored, not signed on" too (the edict has valid private data). Today that entity is wiped by the next joiner's `SV_InitEdict`.
- **Late precache** is not fatal: Xash precaches late with a warning and sends the resource (`server/sv_init.c:131-136`). CERTAIN

---

## 3. Console `changelevel` in story mode

### 3.1 Today

- `changelevel <map>` → `SV_QueueChangeLevel(map, NULL)` (`server/sv_cmds.c:554-570`) → `COM_ChangeLevel(map, NULL)` sets `loadGame = false` (`common/host_state.c:147-150`).
- `SV_ChangeLevel(false, …)`: `story` is false, so `SV_CoF_TransitionReset(true)` clears the hub history and the ledger (`server/sv_save.c:2529`, `:56-68`).
- `SV_FinalMessage("", true)`: the transition is not active and `maxclients > 1`, so clients get **`reconnect`** (`server/sv_main.c:1099-1104`). Every client reconnects as a new connection with a new userid.
- The classic branch calls `pfnResetGlobalState` and `SV_SpawnEntities`, and everyone fresh-spawns (`server/sv_save.c:2614-2620`).
- `changelevel2 <map>` with one argument behaves the same (`server/sv_cmds.c:576-590`).
- The game's own landmark-less `CHANGE_LEVEL(map, NULL)` takes the same route: the team-wipe restart, `cof_changelevel`, the co-op doors (`2026-09-30-hl-exits-coop-re.md` §2d, §4).
- CERTAIN

### 3.2 Option A: a landmark-less story transition (recommended)

Reuse the whole verified pipeline in section 1 with an identity "landmark". What changes:

1. **Routing.** In story mode, route the console `changelevel`/`changelevel2 <map>` (no landmark) into a story change:
   - `COM_ChangeLevel` decides `loadGame` from a non-empty landmark (`common/host_state.c:139-149`), so it needs either a reserved landmark token or a separate pending flag that `SV_ChangeLevel` reads.
   - Keep the game's own landmark-less requests on their separate entry point (`pfnChangeLevel`, `server/sv_game.c:1498`), so the team wipe can be handled by "load the last save" (implication 6).
2. **Validation.** Skip `SV_CoF_TransitionValidate`'s landmark check (`server/sv_save.c:117`) but keep its "sign-on pending" refusal (`:122-126`). Set `source_origin = destination_origin`, so the offset is zero.
3. **Synthetic connection.** `SaveGameState` already reserves a connection when none matches (`:1865-1878`). An empty landmark name will never match a DLL entry. CERTAIN
4. **Other adjacent maps.** Their entities are moved with `LandmarkOrigin(…, pLandmarkName)` (`:2418-2419`). With no landmark, both lookups return zero (`Q_strcmp` is NULL-safe, `public/crtlib.h:202-207`). DLL-flagged entities from *other* visited maps would then move with a wrong offset. Restrict a landmark-less change to the incoming map only.
5. **Arrival.**
   - Players arrive at their old coordinates. Move the leader to the destination's spawn spot (`info_player_start`, or the `startspot`; the game's own choice is `EntSelectSpawnPoint`, `2026-09-30-hl-exits-coop-re.md` §5).
   - Mark everyone else a stray, so `SV_CoF_PartyTransitionArrive` puts them beside the leader (`server/sv_cof_party.c:905-962`). Its anchor and regroup fallbacks already handle a spot in solid.
6. **Same map** (`changelevel <current map>`):
   - The story branch would save the current world and then load it straight back as a hub (`server/sv_save.c:2548-2549`, `:2602-2604`). That is a reload in place, not a restart.
   - Either refuse it, or spawn the world fresh (`SV_SpawnEntities`) while still carrying the players from the incoming snapshot.
7. **Spawn-spot side effects are skipped** (UNKNOWN impact). Restored players never run the game's spawn, so the spawn spot's `target` (fired by the MP `GetPlayerSpawnSpot`, §5 of the exits notes) and `game_player_equip` do not fire. Landmark transitions into the same maps already work this way. A chapter start reached by console might still rely on its spawn spot's target for an intro.

### 3.3 Option B: per-player snapshots across a plain changelevel

Snapshot each spawned player as in 2.2 before `SV_InactivateClients`, let the classic change and `reconnect` happen, then restore by name at each sign-on and place at the spawn spot. It works with new userids, but adds placement and duplicate-item handling. It only helps if the section 2 mechanism is built anyway.

---

## 4. Saving with two players

### 4.1 Single-client assumptions in the save and load path

| Where | What | Tag |
|---|---|---|
| `IsValidSave` `server/sv_save.c:893-897` | `svs.maxclients != 1` → "Can't save multiplayer games." | CERTAIN |
| `IsValidSave` `:899-921` | Only `svs.clients[0]` must be spawned, have an edict and be alive. Other players are not checked. | CERTAIN |
| `IsValidSave` `:881-885` | `CL_Active()`: the host's own local client must be active (listen server only). | CERTAIN |
| `SaveClientState` `:1568-1569`, `LoadClientState` `:1736-1737` | The camera `viewentity` is saved and restored for client 0, and only when `maxclients == 1`. Decals, sounds and music come from the host's local client (`:1546-1565`); that is world-level, not per player. | CERTAIN |
| `SV_LoadGame` `:2714-2716` | Forces `maxplayers 1`, `deathmatch 0`, `coop 0`. | CERTAIN |
| `SV_LoadGame` `:2640` | Refused on a dedicated server. | CERTAIN |
| `LoadGameState` → `CreateEntitiesInRestoreList` `:1803-1817` | Player rows with `id <= maxclients` are created into the client edict. With `maxclients 1`, a saved player id 2 falls into the generic branch and becomes a **"player" object in an ordinary edict**, a ghost with `FL_CLIENT`. | CERTAIN (code) / LIKELY (consequence) |
| `SV_PutClientInServer` `server/sv_client.c:1349`, `:1385-1389` | `sv.loadgame` is one global flag. The first client to spawn takes the restored path and clears it. A second client gets the fresh path, and `SV_InitEdict` frees its restored player (`:1397`), orphaning that player's restored weapons. | CERTAIN |
| `SV_FinalMessage` `server/sv_main.c:1099-1104` | `svc_changing` (stay connected) only when `maxclients <= 1`. Multiplayer gets `reconnect` unless a story transition is active. | CERTAIN |
| `client/cof_quicksave.c:108-119,126-130,159-163` | Quick save and quick load refuse whenever host or joiner is in a multiplayer session. | CERTAIN |
| `client/dll_int/cl_game.c:2036-2039` | The tape recorder's `savehack cofsaveN` becomes a local `save cofsaveN` only when `cl.maxclients == 1`. In multiplayer it goes to the game DLL as a client string command (what `hl.dll` does with it is UNKNOWN; brief A). | CERTAIN |
| `.sav` header `GAME_HEADER` `:387-392`; per-level `SAVE_HEADER` `:394-409` | Map name, comment, file count; skill, counts, time, sky. **Nothing about players, names or slots.** | CERTAIN |
| `SV_GetLatestSave` `:2932-2964` | Newest `*.sav` of any kind (single-player, quick, autosave, `cofsave*`). | CERTAIN |

### 4.2 Are all player edicts written with the level?

- **Yes.** `SaveGameState` saves every valid edict `0..numEntities-1` with the game's `pfnSave`, the client edicts `1..maxclients` included. Each `FL_CLIENT` row is flagged `FENTTABLE_PLAYER`, with table id = edict number = slot + 1 (`server/sv_save.c:1931-1944`). CERTAIN
- **Weapons** are ordinary rows, linked to their player by table index. CERTAIN
- **Story inventory flags.** Outside a transition, owned items get **no** `FENTTABLE_COF_INVENTORY` flag and players get no connection bit (`:1946`). A plain save therefore cannot tell later whose items are whose without re-reading owner/aiment. CERTAIN
- **Unoccupied slots.** An empty slot's edict has no private data and is presumably skipped by the DLL. A disconnected player's leftover object in its old edict is saved as a player row. LIKELY

### 4.3 Per-map story files vs a savegame

| Action | Files in `save/` | Tag |
|---|---|---|
| Story transition | Writes `save/<old map>.HL1/.HL2/.HL3` (`:1851-2033`). The incoming restore rewrites the old map's `.HL3` (`:2436-2445`). Nothing is deleted. | CERTAIN |
| `SV_SaveGame` → `SaveGameSlot` | Writes the current map's `.HL1/.HL2/.HL3` (`SaveGameState(false)`, `:2129`), then **copies every `save/*.HL?` file** into the `.sav` (`:2136`, `:2184`, `DirectoryCopy` `:986-1013`). A save mid-session therefore contains every visited map's snapshot plus the current map. | CERTAIN |
| `SV_LoadGame` | `SV_ClearGameState` (`:2677`) = `SV_CoF_TransitionReset(true)` + **`ClearSaveDir` (deletes all `save/*.HL?`)** + `pfnResetGlobalState` (`:2487-2494`). It runs **before** the file is validated (`:2679-2700`), so a bad save wipes the running session's snapshots, hub history, ledger and DLL global state. Then `DirectoryExtract` restores the save's files (`:1022-1049`) and `SaveReadHeader` restores the DLL global state (`:2237`). | CERTAIN |
| New game, `map`, `restart` | `COM_NewGame`/`COM_LoadLevel` set `loadGame = false` (`common/host_state.c:67`, `:96`) → `SV_ShutdownGame` → `SV_ClearGameState` (`server/sv_init.c:772-773`): **all `save/*.HL?` wiped**. `restart` is `COM_LoadLevel(sv.name)` (`server/sv_cmds.c:522-529`). | CERTAIN |
| Engine start | `SV_Init` → `SV_ClearGameState` (`server/sv_main.c:1061`). The per-map story files never outlive the host process unless a `.sav` holds them. | CERTAIN |
| Non-story changelevel (console, team-wipe restart, `cof_changelevel`) | Files stay on disk, but `TransitionReset(true)` forgets the hub history (`server/sv_save.c:2529`). Later story transitions treat every map as unvisited and spawn it fresh, so the intro replays. | CERTAIN |
| `sv_newunit` | Clears `save/*.HL?` on a smooth non-story change only (`:2610-2611`). | CERTAIN |

Where `save/` is: all save I/O goes through the `SV_SaveFS*` adapter. With `cof_save_root_compat 1` it maps `save/` to the runtime-root `SAVE/` (`:198-384`). The live host's per-map files are in `runtimes/enhanced/SAVE/`: `c_start`, `c_start2`, `c_apartment1`, `c_apartment2` `.HL1/2/3`, plus old `quick*.sav`. So compat is on in the deployed host (LIKELY). Single-player and co-op saves share that folder and the menu's load list.

### 4.4 Where the hub history would need to live

- `cof_story_maps` is memory-only (`server/sv_save.c:41-42`) and cleared by every `TransitionReset(true)`, which includes the load path. CERTAIN
- **Option 1, no format change:** after extracting a save, rebuild it from the `save/*.HL1` files present. Every `.HL1` belongs to a map that was saved, which means visited. The order does not matter (it is a set capped at 256).
- **Option 2, a sidecar file:**
  - Name it e.g. `save/_cofsession.HL4`. Every helper globs `save/*.HL?` (`ClearSaveDir` `:844`, `SaveGameSlot` `:2136`, `DirectoryExtract` writes whatever names the `.sav` lists), so the file is bundled into every `.sav`, extracted on load and wiped on a new game automatically.
  - It could hold the hub history, the ledger (party set, set-down per name), the name ↔ saved player id map, and parked absent-player snapshots (section 2).
  - It only counts toward `mapCount` (`:2139`), which is just the file count.

---

## 5. Loading with two players

### 5.1 What `SV_LoadGame` does to a running listen server with a guest

1. `SV_LoadGame` (`server/sv_save.c:2628-2720`), still inside the running session, does the following in order:
   - Clears all state (see 4.3) and extracts the save.
   - Sets `maxplayers 1`, `deathmatch 0`, `coop 0`.
   - Calls `COM_LoadGame(map)`. That sets `loadGame = true` and `STATE_LOAD_GAME`. It does not disconnect the host's own client, because the server is active (`common/host_state.c:105-127`).
   - CERTAIN
2. On the next host frame, `STATE_GAME_SHUTDOWN` → `SV_ShutdownGame`. `loadGame` is true, so there is no clear. `SV_FinalMessage("", true)` sends **`reconnect`** to the guest and the host's loopback client (`server/sv_main.c:1104`), then `SV_DeactivateServer` (`server/sv_init.c:770-788`). CERTAIN
3. `SV_ExecLoadGame` → `SV_SpawnServer` → `SV_SetupClients`. `maxclients` 2 ≠ cvar 1, so **`SV_Shutdown("Server was killed due to maxclients change")`** (`server/sv_init.c:802-809`). That function:
   - sends `svc_disconnect` (`SV_FinalMessage(…, false)`);
   - closes the network (`NET_Config(false…)`);
   - calls `SV_CoF_StoryShutdown` (story gate closed);
   - calls `CL_Drop()`;
   - frees the client array (`server/sv_main.c:1158-1206`).
   - CERTAIN
4. The server comes back single-player. The host's local client auto-connects to it (`client/cl_main.c:3714-3726`). The **guest is disconnected** and cannot rejoin, because the network is closed and there is one slot. With `maxclients 1` the story gate stays shut (`server/sv_cof_story.c:94`). CERTAIN per step; LIKELY as an end-to-end result (not run).

So today a load always ends co-op: the guest is kicked, not kept or re-signed-on.

### 5.2 How restored players are matched to clients

- **By slot index only:** saved table id → `SV_EdictNum(id)` (`server/sv_save.c:1803-1813`). The client in slot `id-1` owns that edict (`server/sv_init.c:1063-1065`). The story transition adds the ticket check (slot + userid + epoch). CERTAIN
- **The host is slot 0** in practice: the local client connects first when the listen server starts. LIKELY

**What matching by name needs:**
- **Each saved player's name.** Either read the saved entity's `netname` with `pfnSaveReadFields(pSaveData, "ENTVARS", …)` and a temporary TYPEDESCRIPTION, as the engine already does for `classname`/`globalname` (`server/sv_save.c:570-574`, `:2280`), or write a name ↔ id table into the sidecar at save time.
  - The ENTVARS route works only if the DLL's entvars save table includes `netname`. The engine's own copy of the SDK table does (`server/sv_game.c:86`), so it is LIKELY for the retail DLL.
  - The sidecar route is CERTAIN to work because the engine controls it.
- **A mapping** from saved id to the current slot whose `cl->name` matches (case-insensitive, aware of the ` (n)` dedup suffix).
- **Create the player into that client's edict** instead of `SV_EdictNum(pTable->id)`: a change to `CreateEntitiesInRestoreList`'s player branch (`:1803-1813`).
- **A ticket for each matched client**, so sign-on takes the restored path (no `ClientConnect`/`ClientPutInServer`).
- **For unmatched saved players:** skip them and their items, or park them as section 2 snapshots. Skipping their items needs the items flagged at save time (4.2).
- **Fix `netname` and `colormap`** after the restore (2.2 step 4).

### 5.3 Enhanced's quick save and load

- `client/cof_quicksave.c` is client-side command glue in the host process.
  - `cof_quicksave` (F5) → `SV_CoFQuickSave` → `SV_CoFSaveTransaction` (`server/sv_save.c:2889-2905`, `:2774-2862`). The transaction writes the label first, keeps a backup, calls `IsValidSave` + `SV_SaveGame("cofquick")`, and rolls back on any I/O error.
  - `cof_quickload` (F9) → `SV_LoadGame("save/cofquick.sav")` (`client/cof_quicksave.c:154-178`).
  - Both refuse in multiplayer before reaching the server (`:110-119`). The pause-menu save `cof_menu_save N` uses the same transaction (`server/sv_cmds.c:434-444`, `server/sv_save.c:2864-2883`).
  - The only client-only logic is the gating and the "Quick saved/loaded" messages. Saving and loading are entirely server code.
  - CERTAIN
- `svc_restore`, which the single-player load sends at spawn, does nothing on the client (`client/parse/cl_parse.c:1295-1311`). CERTAIN

### 5.4 An existing mechanism that keeps the guest connected

- **The story changelevel path, and only that path:**
  - `cof_story_changelevel` while `SV_CoF_TransitionActive()` (`server/sv_main.c:1090-1098`).
  - The frames reallocation (`server/sv_save.c:2583-2588`).
  - The kept slot and userid with `SV_SpawnServer` resetting clients to `cs_connected` (`server/sv_init.c:1055-1067`).
  - The restored-client sign-on branches (`server/sv_client.c:1349`, `:1629`).
- `SV_LoadGame` never uses it, because `SV_ClearGameState` resets the tickets before `SV_FinalMessage` runs.
- A co-op load built as a variant of `SV_ChangeLevel`'s story branch keeps both players connected, without `maxplayers` changing.
- **Limitation:** a client that is not `ca_active` ignores `cof_story_changelevel` (`client/cl_main.c:4368`). That is the open edge already noted in the handoff.
- CERTAIN

---

## 6. Autosave

- **The engine never saves by itself.** The only automatic hook is the console command `autosave` (`server/sv_cmds.c:503-513`): `SV_SaveGame("autosave")` when `sv_autosave` is set (default `1`, `server/sv_main.c:68`; the host's config has `sv_autosave "1"`). Nothing in the engine calls it. In GoldSrc the game DLL's `trigger_autosave` issues it. CERTAIN
- **`hl.dll` does not appear to issue it.** The string `autosave` occurs once in the retail `hl.dll`, inside the exported class name `trigger_autosave` (offset `0x20EEE1`). There is no `autosave\n` command string. LIKELY the game never asks the engine to autosave; brief A's map scan will confirm. Even if it did, `IsValidSave` refuses in multiplayer.
- **Changelevel writes no `.sav`.** Story transitions write only the per-map `.HL?` files (1.3). CERTAIN
- **`AgeSaveList` rotation.** The names `autosave`/`quick` rotate `…01..NN` by `GI->autosave_aged_count`/`quicksave_aged_count` (`server/sv_save.c:2154-2157`). CERTAIN
- **`reload`** loads `SV_GetLatestSave()` (newest of all `.sav`), else starts `sv_hostmap` (`server/sv_cmds.c:537-546`). It goes through `SV_LoadGame`, so in co-op it would end the session and could pick a single-player save. CERTAIN
- **Relevance:** none today. The natural co-op checkpoint is the moment a story transition completes (`CoF_TransitionResume`, `server/sv_save.c:70-79`). Every per-map file is fresh then and both players are signed on.

---

## 7. Risks and unknowns

1. **A failed load already destroys the session.** `SV_LoadGame` clears the game state before validating the file (`server/sv_save.c:2677` vs `:2679-2709`). A co-op load must validate first, then tear down. CERTAIN
2. **Ghost player on a single-player load of a co-op save.** A co-op `.sav` loaded through the normal path (menu, `load`, `reload`, F9 when allowed) recreates saved player 2 as a `player` object in an ordinary edict (`:1814-1817`). Name co-op saves distinctively, and refuse them on the single-player path. LIKELY harmful.
3. **One global `sv.loadgame`.** With two clients, the second fresh-spawns over its restored edict and orphans its restored weapons. Their raw `m_pPlayer` points at freed memory, which risks duplicates or a crash (`server/sv_client.c:1385-1397`). A co-op load must use per-slot tickets. CERTAIN (code) / LIKELY (consequence)
4. **Edict serials survive `SV_InitEdict`** (2.4). Leftover items or handles of a departed player attach to the next player in that slot, and the story capture's owner/aiment rule then carries them, which duplicates them. Whether the game removes items on disconnect is UNKNOWN (brief A).
5. **`SV_AllocPrivateData` does not reset a non-free edict** (`server/sv_game.c:1235-1242`). Every restore into an existing client edict must `SV_InitEdict` first. CERTAIN
6. **Dropped `item_key` keeps `owner = dropper`.** The capture counts it as inventory: it is carried across transitions, misplaced in the new map, removed from the old one, and would be swept into a rejoin snapshot. LIKELY, from `DropKeyItem`'s owner argument and the capture rule.
7. **Client inventory copy.** Every restore must go through the game's `pfnRestore` and its own HUD resend. The engine must never write slots or rely on the client keeping its copy. The client's documents and code notes after a load or rejoin are UNKNOWN; the notes research says a code note comes back with `isCode=0` after an InitHUD resync.
8. **Name matching.** Names are deduped with ` (n)`, can change mid-game, and two friends could pick the same base name. The match must be deterministic, and the result logged.
9. **Per-slot cosmetic state.** The Simon variant is chosen by slot (`SV_CoF_StoryUserinfo`). A name-matched player in another slot swaps models. `colormap` must be reset.
10. **Clients not `ca_active` during a kept-connection load** ignore `cof_story_changelevel` (`client/cl_main.c:4368`). Handle them like a drop: park their snapshot, and let them rejoin.
11. **The ledger and party state are slot+userid keyed** (`server/sv_cof_share.c:26-30`, `:100`; `server/sv_cof_party.c:110-129`). A load or rejoin is a new identity to them. Set-down marks are lost unless stored in the sidecar. The ledger otherwise rebuilds the party set from what players hold.
12. **The game's global state.** The `.sav` carries `pfnSaveGlobalState` (`server/sv_save.c:2145`, restored at `:2237`). The retail co-op store `[0x10222E10]` keyed by netname is DLL memory and is **not** in any save. Its contents after a load are whatever the running DLL had (UNKNOWN; brief A).
13. **Saving a dead player.** `IsValidSave` checks only client 0. A co-op save must decide whether a dead partner blocks the save or is saved dead (restored into the death think with rebased timers).
14. **The live host has `sv_autosave "1"` and `cof_quick_saves "0"`.** Single-player and co-op `.sav` files share the root `SAVE/` folder and the menu's load list. `SV_GetLatestSave` mixes them.

---

## Implications for the design

The simplest viable approach for each piece, in build order:

1. **Respawn keeps items.**
   - The engine sees no death or respawn event. The game's MP respawn runs `Spawn` on the same edict and wipes slots and weapons (`2026-09-30-hl-inventory-re.md` §7).
   - **Story items:** the ledger already re-delivers them (verified live 2026-09-30).
   - **Weapons, ammo and clips:** at the frame the party tracker first sees the player dead (`CoF_PartyTrackLife`, `server/sv_cof_party.c:297-343`; `Killed` does not touch the inventory), call the game's co-op store `SavePlayer(pl)` (`1002A480`, keyed by netname). The game's MP `PlayerSpawn` restore (`10029090`) then rebuilds them on respawn through the game's own code and messages.
   - Brief A must confirm what the store restores (health and stamina from a corpse?) and that `PlayerSpawn` runs it on a respawn, not only after a changelevel.
   - Fallback if the store is unfit: an engine save-table snapshot taken at death, with only the weapons restored after respawn.
2. **Rejoin keeps items.**
   - **Capture:** a one-player DLL save snapshot of the player plus its owned `weapon_`/`item_` entities, taken in `SV_DropClient` before `pfnClientDisconnect` and in `SV_ConnectClient`'s same-address reuse branch, keyed by name. Then remove the captured items from the world.
   - **Restore:** in a new `SV_PutClientInServer` branch: `SV_InitEdict`, recreate the player and items, run `pfnRestore`, fix `netname`/`colormap`, skip `ClientPutInServer`, and place the player with `PartyPlayerReady(cl, true)`.
   - **Format:** write the snapshot in the `.HL1` format with a minimal header, so `LoadSaveData`/`ParseSaveTables` read it unchanged.
3. **Console changelevel keeps items.** Make story-mode console `changelevel`/`changelevel2 <map>` a story transition without a landmark:
   - zero offset;
   - incoming map only;
   - leader moved to the destination's spawn spot, everyone else a stray placed by `SV_CoF_PartyTransitionArrive`;
   - same-map refused, or spawned fresh with the players carried.

   Kept connections and the whole verified capture → restore → sign-on path come for free.
4. **Co-op save.**
   - A story-mode save command (distinct names, e.g. `coop<N>.sav`, so the single-player list, `reload` and F9 never pick it up).
   - It replaces `IsValidSave`'s `maxclients`/client-0 tests with: no pending transition, every spawned player present, plus a dead-player policy.
   - It flags owned inventory like a transition capture in `SaveGameState`.
   - It writes a sidecar `save/_cofsession.HL?`: hub history, ledger, saved player id ↔ name, parked snapshots of absent players.
   - It calls the existing `SaveGameSlot`, which bundles every `save/*.HL?` file, the per-map story snapshots included.
5. **Co-op load.** Do not call `SV_LoadGame`. Queue a kept-connection load at the changelevel state boundary, modelled on `SV_ChangeLevel`'s story branch:
   1. Validate the `.sav` first.
   2. Issue tickets for connected spawned clients, matched by name to saved players.
   3. Send `cof_story_changelevel`; deactivate; reallocate frames.
   4. Clear `save/*.HL?`, extract, restore the DLL global state, and restore the hub history (sidecar, or rebuilt from `save/*.HL1`).
   5. `SV_SpawnServer`. Load the world skipping players and inventory, then a player pass restoring matched players into their current edicts.
   6. Park unmatched players for a later rejoin, then activate.
   7. The normal ticket sign-on follows.
6. **All dead → load the last save.**
   - In `pfnChangeLevel`, catch the game's landmark-less request for the current map while every spawned player is dead (the retail team-wipe restart, `2026-09-30-hl-exits-coop-re.md` §4). Run the co-op load of the newest **co-op** save instead (its own lookup, not `SV_GetLatestSave`).
   - Fall through to today's behavior only if no co-op save exists.
   - Take a co-op autosave when each story transition reaches `phase=ready`, so the last save is never far behind.
