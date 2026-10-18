# Survey of the Cry of Fear decompilation (2026-10-18)

The user sent https://forgejo.caixa.cc/archive/cof-decompiled and asked: "go through the entire decompiled thing and
write and note any thing relevant to our development". This is the merged result. The full notes of the ten reading
slices, with every source line, retail address and confidence, are in `evidence/decomp-survey-20261018/` (git-ignored;
`A-player.md` ... `I-meta-offsets.md`, the brief they worked from in `BRIEF.md`, helper tables in `helpers/`).

**Open question for the user, before anything here is committed or coded.** The untracked
`docs/research/2026-10-17-uni-coop-notes.md` (written by another session) says of a decompilation: "We do not use it. Do
not take code, offsets or names from it into this repo: the engine is GPL-3 and the decomp is the developers' code."
Nothing in this survey copies code; it describes behaviour, and every offset our engine uses was already found by our own
static RE of the retail DLLs (the audit below only confirms them). Whether this document, and fixes based on it, go into
the repo is the user's decision. Until then it stays uncommitted.

## What the decompilation is

- `caixax/cof-decompiled` (mirror), commit `d505c13` of 2026-10-18; cloned read-only into the session scratchpad
  (`dl-cofdecomp/cof-decompiled`); nothing in it was run or built.
- A *matching* decompilation of Cry of Fear 1.6a: source that compiles to the retail machine code, function by function.
  Its targets are **byte-identical to the files our game runs**: hl.dll SHA-256 `0036b91c...0e63` (5317 matched + 11
  equivalent of 5328 functions, 100 %) and client.dll `d2a04641...9c00` (2181 + 23 of 2252, 93 %; its README still says
  "not started"). opengl32.dll complete; hw.dll (CoF's GoldSrc build of 2013-04-21) partial; GameUI, cof.exe and
  CoFLaunchApp partly. Names come from PDBs of older builds (1.0, 1.55); `_guess` marks guessed names.
- So for hl.dll and almost all of client.dll it is a readable copy of the exact code we run. Every claim below was read
  from that source (and, where noted, from the retail bytes); none was checked live. Re-check each against the retail
  binary before coding, as for every `+0x...` in the engine.

## 1. Bugs in our own code

1. **Ladder busy guard never fires** (`engine/server/sv_cof_moments.c:421`, `:514`). It needs `FL_FROZEN|FL_GODMODE`, but
   retail PostThink clears FL_GODMODE (and noclip) on every usercmd, so the ladder's god flag lives one frame. The
   hijack guard and the drop of a leaver's ladder camera are skipped. Test instead: the manager's owner is a client with
   FL_FROZEN, or `P+0x5AC` (camera model) set / `P+0x388` (attached camera) valid. Also means the engine's `god` and
   `noclip` are undone by the next PostThink. CERTAIN. (A1)
2. **`usercmd_t.is_donator` is missing from our engine's ABI.** CoF's engine has `int is_donator` at usercmd +0x34
   (size 0x38) and `isdonator` at clientdata +0x1DC. hl.dll's `CmdStart` copies it into player `+0xC4` on every command;
   ours passes a 52-byte command, so +0xC4 is the next command's first bytes (humans) or stack garbage (companions,
   `CoF_CompanionRun`). Effects: `IsDonator()` (true unless the value is 0/3/4) is mostly true and flickers, so TMP
   pickups (donator-only), the `ammo_tmp` conversion and the "mappers only" MP5 are random per player; a value of 4 makes
   `IsDeveloper()` true (dev commands). Client side, client.dll writes the field into Xash's `runcmd_t.processedfuncs`
   (the H slice found nothing reads it; the D slice thinks a non-zero value may skip predicted pmove effects for
   donator players: LIKELY, only for Steam users in CoF's donator groups). Fix: hand `pfnCmdStart` (humans and bots) a
   0x38-byte copy with a chosen `is_donator` (0 = retail non-donor, 1 = donator); give `CL_CreateMove` a 56-byte scratch
   command. Settles UNKNOWN 7 / §4.6 of `2026-10-12-companion-weapons-items-re.md`. CERTAIN code. (H1, D4)
3. **View distance is 1.73 x retail.** Retail projects to exactly `zmax` (worldspawn MaxRange, `sv_zmax`); our
   `ref/gl/gl_rmain.c` `R_GetFarClip` uses `zmax * 1.73`. client.dll's world view has no far plane of its own, so players
   see 73 % further than retail on every map, including the forests whose short MaxRange is the darkness (c_forest4 1500
   -> visible to ~2595 even unmerged). Fix: a cvar for 1.0 (retail parity). Not seen live. CERTAIN code. (H2)
4. **Story-item duplicates.** `AddItemToInventory` dedupes only `weapon_*`; `item_key` pickups and `cof_giveitems` sf1
   always add a slot. `sv_cof_share_state.h:4-6` assumes a player can never hold two copies; a give re-issued after the
   1 s confirm window, or picking up a world copy of a held item, makes a second slot, and a consumed key can leave one
   behind. Fix in the ledger: count copies, delete extras (`inventorydeletenamed`), or re-check `held` right before each
   give. CERTAIN code, LIKELY consequence. (A2)
5. **Custom objectives reach partners as default text.** `cof_objective` writes the FIRST player only and PostThink
   zeroes `m_sCustomObjectives` right after sending, so `SV_CoF_RetailStoryRead/Write` usually mirror the key with the
   file already 0. Mirror the pair when `P+0x1E90` changes on FIRST, or replay FIRST's `Objectives` message. CERTAIN. (A8)
6. **Engine-made commands are dropped during a phone call or the computer/phone UI.** ClientCommand returns early while
   `m_bInPhoneCall`, or `m_iUsingComputerOrPhone` is 1 (computer: only freeze/unfreeze/stopcomputer/computercheck) or 2
   (phone: only freeze/unfreeze/phonenumber). Our `inventorydeletenamed` (`SV_CoF_RetailRemoveItem`), `flashlight`
   (`sv_cof_light.c`), `tapgetup`, `telescopecancel`, `boilercheck 5` do nothing then (the ledger and the light retry).
   CERTAIN. (A10)
7. **`cof_hud_remake.c`: `HealthCap` is a type, not a value** (0 none, 1 = max 80, 2 = max 66). The remake divides by 1
   or 2, so the bar shows full while capped, and it ignores the later `HealthCap 0`. CERTAIN. (G5)
8. **Our Simon models lack attachment 3.** client.dll draws other players' phone light, flashlight and co-op torch (and
   the companion's torch) from `attachment[3]`; police1.mdl has 4 (#3 `Bip01 Spine3`), our generated `cof_s1..s4` have
   3, so remote lights start from a stale or zero point. Fix in `tools/prepare_simon_models.py`: add police1's 4th
   attachment. client.dll exits the game if a studio model has more than 4. CERTAIN (code and model bytes). (D2)
9. **Spawnflag 128 is not "wait for script"** in this codebase (it only silences the stuck-in-wall error; the wait
   flag is commented out). The gate `COF_RETAIL_SF_WAIT_SCRIPT` (`sv_cof_retail.c:761`) rests on the wrong meaning; it
   does no harm. The real waits are per class (spitter/sewmo/watro `InactiveThink`, slowerstuck and roof boss never
   path, sewer boss hurt only by DMG_SHOCK, named monsters wait 5 s). Also in `docs/companion.md`. CERTAIN. (E5)
10. **Debug cvar hazard**: `cof_skip_client_normal_triangles` with a frame that draws no studio model can leave the
    client's `world->nodes[0]` box moved, and the world disappears until the map reloads. Debug only. MEDIUM-HIGH. (F7)
11. **Comment fixes**: `sv_cof_party.c:437` "the map's first spawn point" (after a level change respawns use the
    destination landmark, see 4.10); `sv_cof_cheats.c` top comment (FL_GODMODE is honoured by TakeDamage, it just never
    lasts). (C1-6, A1)

## 2. New co-op soft-locks and stuck players

None of these is handled by the engine today.

1. **c_apartmentsick, chainsaw boss**: any swing that hits someone spawns `third_person_interaction` (victim frozen,
   stripped, gibbed, killed) and the boss `UTIL_Remove`s itself without dying, so `TriggerTarget multikilled1` (removes
   `walle`, enables the teleport on) never fires. In single player that is game over; in co-op, with any survivor (a bot
   counts), the way on is closed for good. In MP the swing prefers the highest player slot in reach, usually a bot.
   Options: re-create `theboss` after the kill animation (classname `monster_bosschainsaw`, `targetname theboss`,
   `TriggerCondition 4`, `TriggerTarget multikilled1`), or force the team-wipe reload; the brain should never melee it
   and keep >150 units away. CERTAIN code and map wiring, not seen live. (C2-1, E1)
2. **c_start padlock**: a guest who solves it stays frozen, black, in the padlock camera with the HUD hidden; the
   finishing think (2 s later) releases FIRST = the host. `padlockcancel` cannot help (the link is already cleared).
   Fix: make the solver FIRST while `Finished` runs (`SV_CoF_PreferFirst`), or replay its five writes. CERTAIN. (C2-2)
3. **Wheelchair death in MP**: a lethal hit leaves the rider at 1 hp, frozen, unarmed, in a dead chair forever; both
   release paths refuse a dead chair; no team wipe happens. Fix: when chair+0x368 becomes 1 with a rider, do Disembark's
   writes, then kill the rider with `ClientKill` so the MP death/respawn runs. Settles the UNKNOWN in
   `2026-10-12-player-state-re.md`. CERTAIN. (C2-3)
4. **c_lake boat** (second member of cm_fsewer1): carries one player, parks on the north shore; partners on the south
   shore cannot use it. Fix: party move to `boat_exit` at Disembark, or return the boat. LIKELY soft-lock unless the lake
   can be swum (live check). (C2-4)
5. **Crazybitch grabs player 1, not her enemy**: the suicide struggle always goes to edict 1 (the host), anywhere on the
   map, retried every think until the host holds a suicide-capable gun. A companion (always armed) sets it off; the
   companion's own struggle branch (`sv_cof_brain.h` STRUGGLE) is dead code for bots. Our first-player redirection does
   not cover `INDEXENT(1)`. Fix: cancel the grab when her `m_hEnemy` (+0x184) is not edict 1 (write the gun's
   `m_iSuicideState` +0x140 back to 0). Corrects §4.1 of the weapons RE. CERTAIN. (D1, E2)
6. **Syringe heal interrupted**: if the healer dies in the 1.5 s window, both keep `fuser3 = 25` (no walking, jumping or
   attacking); Spawn does not reset fuser3. Fix: extend `SV_CoF_RetailCprRepair` (`sv_cof_retail.c:674`): fuser3 25 for
   >3 s or after a respawn -> 0 and `ViewLock {0, 0}`. Rare. CERTAIN. (D7)
7. **Monster melee in MP ignores walls and hits the highest slot in reach**; the taller's stamp kills outright (no
   damage path) anyone in front when its target is in CPR, getting up, ducking, on the phone, struggling or using a
   syringe. A companion reviving near an engaged taller (c_buscity, c_buscity2) dies at once. Brain: no CPR, duck or
   syringe within ~150 units of a taller that targets anyone. CERTAIN. (E3)
8. **Bosses tied to the first player**: the doctor (c_attic) aims at and damages only its cached first player
   (`+0x368`), even when its trace hits someone else; the roof boss (c_roofs2) attacks only while the host is visible;
   the three c_ending4b book Simons shoot only the first player's wheelchair (our rider-as-first covers this only if set
   before each first locks on); the sledgehammer Simon and `cof_strangle` act on the first player. CERTAIN. (E4, C2-12)
9. **3D keypad (`cof_keypadnew`) reads the server machine's keyboard** (`GetAsyncKeyState` in hl.dll) and binds the
   first player: only the host can type. Hookable like our client Enter hook. CERTAIN. (G4, I5)
10. **Tape saves on difficulty 4**: the cassette gives its 5 tapes only to its picker; partners holding the shared
    cassette get "I can't use this without a tape"; when the picker's count reaches 0 the game removes the picker's
    cassette and our ledger then takes everyone's. Our engine accepts `savehack cofsaveN` from any client with no tape
    check. CERTAIN. (C2-10)

## 3. The known open issues: what the source says

- **"Companion has joined" spam.** `CHalfLifeMultiplay::InitHUD` (0x100C8B70) broadcasts "<name> has joined the game"
  once per fresh player object; the engine drops every bot at a level change and re-adds it, so each door prints one
  line per companion. `m_fGameHUDInitialized` is `P+0x24D0` (checked in the retail bytes at 0x100F76A4/0x100F76DB).
  Fix A: write 1 to `P+0x24D0` right after `pfnClientPutInServer` in `CoF_CompanionAdd` (what InitHUD then skips is
  harmless for a bot in story maps; listed in B1). Fix B: filter that TextMsg for fake clients. The same drop sends
  "- Companion has left the game" (SayText MSG_ALL, kept across the change). LIKELY side effect: the bot's `weapon_*`
  rows are saved into the old map's `.HL1` before `RemoveAllItems`, so a revisit creates ownerless invisible weapons
  per visit per bot (check: count ownerless `weapon_*` after c_start -> c_start2 -> c_start). (B1, B2, A9, G2)
- **Costumes locked.** The wardrobe's mask is computed on the server from the **host's** `scriptsettings.dat` lines
  51-62 (bits 0x80..0x40000); our bonus-room rewrite (`sv_cof_bonus.h`, `cof_bonus_lines`) leaves those lines alone. The
  twelve locked/unlocked token pairs are in `evidence/decomp-survey-20261018/H-engine-menus-launcher.md` finding 6 (and
  A5, G 7.6); our existing 11 bonus lines match the source exactly. A costume is a client-global texture swap: every
  Simon model a client draws, partners and companions included, wears the viewer's costume (`hoodie_grey`/`jeans`/
  `head_face` texture names). `setclothes N` is unchecked on the server and also sets the server-global
  `sv_custom_hoodie`. Correction to `evidence/tester-package-20261015/research/bonus-room-re.md`: outfits are bits
  0x80..0x40000, not 0x10000..0x20000000. (A5, B6, G1, H6, F9)
- **Companions standing in view in cutscenes.** client.dll draws no player at all (local, partners, fake clients and
  their p_ weapons) while the last `ViewEntity` is not the local player or an `AttachCam` entity is set; hl.dll sends
  `ViewEntity(short)` whenever `P+0x2038` changes (written by its SET_VIEW wrapper). `trigger_camera` already freezes
  and views every player (the map-chains doc 1.6/2.6 is wrong; the engine rightly does not mirror). So a companion seen
  "in a cutscene" is in a moment without a view entity (frozen-in-place scenes, `cof_lookat`, `scripted_sequence`, door
  or teleport arrivals, the telescope's own client view), or an engine path changed the view without `P+0x2038`.
  Instead of the per-viewer `EF_NODRAW` plan (uni notes item 1), setting `P+0x2038` (or sending `ViewEntity`) for each
  watching player hides partners, but partners' F-torch spots stay visible under `ViewEntity` (only `AttachCam` hides
  them). `cof_mdlcutscene` never removes its camera models (it calls `UTIL_Remove(NULL)`, confirmed in the retail
  bytes): one skybox-flagged camera model per player per finished model cutscene stays until the map changes.
  (C1-3, C1-5, C2-7, F3)
- **The phone.** The dial screen opens only with `weapontoggle` (Z) with the phone as the active item; once open, the
  number-row digits and main Enter dial (0.2 s debounce, no numpad); the keypad and the landline are mouse-only.
  `phonenumber` has no phone, distance or in-call check on the server for `cof_phonecheck` (the dialer's distance test
  passes for ANY player within `iuser1`); only the 911/112 easter egg needs the phone in hand. Corrects §5 of
  `2026-09-30-hl-documents-story-state-re.md`. A phone call is answered 0.1 s after raising the phone, not 3 s (original
  bug). For the c_park3 gate the obstacle is `cof_phonecall` (activator with a phone), not the check. (G4, A7, C1-9,
  C2-9) Live 2026-10-18: the user dialled 543214 in c_basement at 01:46 and got 5247; the phone works.
- **Fog.** `env_fog` sends `SetFog` (9 bytes: byte r, g, b; short fadetime, >0 fade in, <0 fade out, 0 instant; short
  start; short end; all zero = off) MSG_ONE to every player present, only when it turns on, fades or off. The client
  clears fog on every ResetHUD and InitHUD, and nothing re-sends it: **a respawn or a late joiner loses the fog today,
  merged or not**. Only a save restore re-sends it. `CEnvFog` layout: start +0xDC, end +0xE0, fade in +0xE4, fade out
  +0xE8, hold +0xEC, fade start +0xF0; sf 1 = active, 0x8000 fading; an unnamed or sf-1 env_fog turns on at spawn.
  Correction to `2026-10-17-merging-the-rest.md` 4.1: c_subwaysick1 (`fog`) and c_trainscene (`foguknw`) have fog on
  from the start. Per-section fog = the engine remembers each section's last SetFog and sends it per client on section
  moves, respawns and joins. (C1-2, B5, F5, H7)
- **View distance.** client.dll never reads MaxRange; it is engine-only. zmax goes to each client in its own
  `svc_newmovevars`, so per-section view distance = send each player the MaxRange of their section. See also 1.3. (F5, H2)
- **Renderer limits.** All 26 sites of `engine/client/cof_renderer_patch.h` map to exactly one source line, and nothing
  else uses those arrays. Not covered and also cut at edict 900/1024: particle systems' source-entity lookup
  (`UTIL_GetClientEntityWithServerIndex`, edicts 1..899, client 0x100831E0) and Cg water (edicts 0..1023). Fixed client
  pools a merged map can overflow: 256 static decals, 32 dlights, 256 sky sprites, one 3D-sky marker, one rain setting.
  (F1)
- **Black `{` boxes.** The cause in AGENT_NOTES "Merged maps wave 2" is confirmed: the client's TMU1 is Xash's
  lightmap unit. Refinement: while any client dynamic light lives (flashlight, a partner's torch, muzzle flash, light
  renderfx) rendermode-4 faces take a `GL_REPLACE` depth-only path that ignores the combine state, so the bug hides;
  A/B checks must be made with the flashlight off ("apartment 4 very weird, especially without the flashlight",
  2026-10-16, fits). (F2)
- **Companion navigation.** `sv_cof_nav.h`'s file layout is right byte for byte (CGraph 8396, CNode 88, CLink 24,
  DIST_INFO 16, version 16). MAX_NODES 1024 is soft (extra nodes dropped); MAX_NODE_INITIAL_LINKS 128 is hard (a node
  that sees 128 others stops the whole graph). Link door names keep 4 characters (`*1000`+ misread). Water nodes are
  left out. In-memory `WorldGraph` at VA 0x102238B0. `impulse 197`/`199` draw the graph and need no cheats.
  `VEC_HUMAN_HULL` is 24x24x72 in CoF. Movement: any `fuser3` in (0,50) stops ground movement (CPR, fall freeze, phone,
  struggle, syringe), any fuser3 refuses jumps; forward+side input halves forward (the brain's mixed moves skew up to
  ~18-27 degrees and slow: double the forward part when both are non-zero; LIKELY part of "caught on corners");
  ladders move by button bits only. (E7, E8, D6)
- **Saves and Continue.** Retail `save` works only right after `savehack` (the tape recorder) and never in MP
  (upgrades `2026-10-04-hl-saves-death-items-re.md:63` to CERTAIN; our `cl_game.c:2036` and `sv_cof_session.inc:705`
  match the intent). Slot labels `cryoffear/SAVE/saveinfoN.cof` are written by the client that clicked, before the save:
  **every co-op tape save overwrites that machine's single-player slot label** while its `cofsaveN.sav` stays, so the
  single-player Load menu shows the co-op area and loads an unrelated save. `.sav` go to the root `SAVE\`. (H4, H5, C2-10)
- **Flashlight in co-op.** F sets `frags = frags ? 0 : 32`, and frags also carries the light bits (1 phone, 2 phone
  remembered while holstered, 4 Glock taclight, 8 flashlight, 16 flare/booklaser, 32 co-op torch, 64 night vision), so F
  while the phone light is on hides it from partners until the flags change. Our exact `frags == 32` test in
  `sv_cof_light.h` is right. Only bits 1/8/32 are drawn for others; their dlight keys clash with the local light keys.
  **Bonus-room night vision cannot be switched on in co-op** (its F branch is single-player only, and we drop F when
  the co-op flashlight is off). Lantern and in-hand flare light are visible only to the holder; the dual-wield
  follower could carry renderfx 245 (lantern) / 165 (flare) so others see them. (A4, F4, D10)
- **Dual wield.** One global `gDualWeapon` handles every player's left hand: with two dual-wielders the pending
  left-hand action (punch, blade, nightstick, branch, **phone light on/off**, VP70 burst) lands on whichever
  dual-wielder thinks first; timers, idle anims, phone screen and sprint state are shared. (D9)
- **12-slot inventory.** The patch site list is complete (all loops with bound 6, except
  `CCoopLevelChangeManager::SetPlayerVariables`, left out on purpose, and a debug print). The client's slot store is
  in `gHUD` (0x1018A0C0+0x2C64), not the viewport; the client reloads `640_inventory*.tga` into the background panel on
  each update/toggle (our art swap must survive that). Slots 7..12 travel through retail save/restore inside the
  models field. (G8, I3)

## 4. What the original does differently because our story runs as a multiplayer game

All CERTAIN from the source unless marked. These are retail behaviour, not our bugs, but players will meet them.

- **Carry limits shrink** (`iMaxAmmo1`, MP branch): one syringe per player (SP 10; the story's only healing), about a
  third of the SP ammo (glock 45, revolver 15, shotgun 15, m16 60...), and **flares are broken**: the first comes with 0
  in reserve, every later one is refused with the wrong message "I can't carry any more syringes", throwing or
  switching away from a lit flare deletes the item (17 flares in c_subtunnel1/2). `2026-10-12-companion-weapons-items-re.md`
  §2 lists SP limits only. Option: an engine gate on the MP branch of 0x101202F0, like the dual-wield gate. (B3, D3)
- **Simon sounds like the police officer** (pain, death, swing, breathing, morphine): `player/police/...` in MP. The
  engine could remap the samples and precache Simon's. (B7)
- **Black sky always** (`CheckSky` loads `gfx/env/black*` when maxclients > 1). The merger's "sky differs" blocker is
  moot. (F6, G6, C1-11)
- **Chat and the co-op HUD (player list, health, "X died.", "looking at") show only on maps named `cof_*`.** On story
  maps chat only reaches the console. (G3)
- **Scripted weather never happens**: `rain_modify` with a targetname returns at once in deathmatch ("only static rain in
  multiplayer"). Targeted rain/snow never starts or stops in c_city, c_city2, c_park, c_park2, c_park3, c_stad, c_hamn,
  c_hamn2, c_hamn3 (no rain at all), c_intro, c_start2, c_trainride. This is the "Rain error" log spam. (B4)
- **Shared globals leak between players**: one player's morphine trip plays every server sound at 25 % for everyone;
  every player's think rewrites the host's `room_type` DSP each frame (also the "room_type not found" spam with
  `-nosound`); the cutscene flag, `host_framerate` (reset on every Spawn, which ends a slow-motion scene for everyone),
  `v_dark`, difficulty, subtitle language, the restore resend and the objective popup flag are single globals. Weapon
  spread uses file globals, so a moving player next to a standing one gets ~10 degrees spread. (A3, C2-8, D8)
- **Humans can hurt the AI companions** (only damage to non-fake-client players is refused); companions cannot hurt
  humans. Player bullets pass through players (up to three bodies) to the monster behind; melee stops on the first
  entity, so the brain's hold-fire is only needed for melee. (A6, D5)
- **Vote-kick is open to everyone** (`votekick`/`vote`; immunity only for authids our engine never returns; one vote
  fills every free slot; a lone yes kicks after 30 s). Block both in story co-op. (B9)
- **Cutscene skip is single-player only** (a `SkipScene` message after the server polls Enter); ours replaces it. (G12)
- **Message strip lines (pickups, our "X found the Y", quick-save lines) draw only while `cl_subtitles` is on.** (G7)
- **Each client viewport reset (map load, `HUD_VidInit`, a video-mode change) sends `unfreeze 1024` and
  `stopcomputer`**: mid-game this unfreezes the player and drops the computer/phone UI state. LIKELY effect. (G11)
- Smaller: stair-speed entities removed (21 story maps), no view punch, low-health limp off; `cof_leveltrigger` never
  fires (no story map has one); `sv_coop_music` is never cleared on a map change, so a respawn in a later map can replay
  an earlier map's track (LIKELY cosmetic); hl.dll sends `map c_game_menu1`/`map c_intro`... to clients by stufftext,
  which our engine rightly refuses (keep it so). (B7, C1-7, C1-8, H8)
- **Keep `coop 0`**: with `coop 1` spawn selection never reaches `startspot`, which merged maps' start-spot swap and
  landmark arrivals rely on. (B8)

## 5. Merged maps: planner and engine work

1. **Rain is one per map**: every player's rain comes from the FIRST `rain_settings` (its absolute height) and the first
   constant `rain_modify`, sent only at spawn/restore. cm_city2, cm_hamn, cm_buscity have one per member (only the
   first used, at the first member's height); cm_sewer3, cm_forest6, cm_asylum1dark, cm_fsewer1, cm_c_ending4b have
   one, which now covers sections that had none. Fix: per-section `RainData` per client on section moves (format in C1),
   or a planner warning. (C1-1, F10, B4)
2. **3D sky markers**: the client keeps one sky origin, the last `skymark_sky`/`skymark_w` received (at join), and
   draws every sky-flagged model (renderfx 70/137/184, sent without the PVS test) against it. cm_village1 (3),
   cm_fsewer1, cm_c_ending4b, cm_asylum1day merge several; cm_asylum1dark and cm_forest6 have one that now covers the
   other sections. Fix: send each section's own marker on entry and do not send other sections' sky models to that
   client; the planner checks only `skyname` today. LIKELY visible. (C1-1, C2-5)
3. **First-of-class lookups**: keypad (`cof_keypad`), `cof_phonecheck`, `cof_computer`, puzzle bars, `cof_telephone`,
   numeral plate all use the FIRST entity of their class in the map. No current group has two members with one; the
   planner must refuse it for future groups. (C1-1)
4. **At most 16 (map, landmark) transition pairs per map** (`ChangeList`; it overflows its stack array by one before
   warning). Current groups have at most 10; add a planner check. (C1-12)
5. **Rename keys missing from `rules.REF_KEYS`**: `movewith` (hundreds), `trigger_camera.moveto`/`.message`,
   `cof_interdooronoff.message`, `trigger_changetarget.m_iszNewTarget`, `cof_bosshealthbar.message`,
   `cof_spawnpointonoff.message`, `cof_teleport.message`, `cof_clothesmenu.cameraent/simonent`, `watcher.m_iszWatch`,
   and more; full per-class list in `C1-effects-triggers.md` section 7. (C1-10)
6. **Planner false refusals**: an `ambient_generic` starts with the map only when looping (no sf 32) and not silent
   (no sf 16), so c_asylum1 and c_ending5b pairs are not blocked (real cases: c_broscene, c_trainride, c_trainride2);
   `func_fogfield` uses only origin and size (harmless); every story `func_train` takes its position from its first
   path_corner, which confirms "leave origin-brush geometry unmoved". (C2-6)
7. **Map-name dependent client behaviour**: renderfx 92 prop culling at 800 units only on names containing
   college1dark/forest1..forest6 (cm_college2 loses it for its college1dark section, cm_forest6 gains it for forest7);
   lightmaps rebuild only when the level name changes. (F8)
8. **Node graph**: merged BSPs get their graph built by hl.dll on first load; MAX_NODES is soft, 128 initial links per
   node is hard. Monsters in other sections hear gunfire within 1000 units, but every story monster triggers only on
   death (`TriggerCondition 4`), so no story trigger can fire across sections. (E8)
9. **Entity limit**: retail is 2048 + 15 per extra player; liblist's `-num_edicts 4096` was most likely ignored by retail
   too (correct the AGENT_NOTES wording). Our `max_edicts 4096` is above retail; the planner's 2048 budget equals
   retail's floor. Other retail limits (all below ours): precache 768 each, decals 512, lightstyles 64, dlights 32,
   visedicts 512, user message 192 bytes. (H3)
10. The c_start easter egg position is (613, -3156, -702), for the merger's wave 3 work. (B notes)
11. A respawn after a level change uses the destination `info_landmark` + 1 z (the startspot), never
    `info_player_start` (correction to `2026-09-30-hl-exits-coop-re.md` 5). (C1-6)

## 6. Corrections to our research docs (to apply when the docs are next touched)

- `2026-09-30-hl-documents-story-state-re.md` §5: the dialer does not need `weapon_mobile` for `cof_phonecheck`. (A7)
- `2026-09-30-hl-inventory-re.md` §1: story items, not weapons, skip the duplicate check; the client slot store is in
  `gHUD`; the "world flag pev+0x24C" is worldspawn `iuser2`. (A2, G8, I2)
- `2026-09-30-hl-exits-coop-re.md` 5: spawn is the startspot (landmark) or `info_player_start`, either-or. (C1-6)
- `2026-10-04-hl-saves-death-items-re.md:217` and `2026-10-12-companion-bots-cpr-monsters-re.md:225`: `0x100F2A50(1)` is
  `SetAnimation(PLAYER_WALK)`, not a weapon redeploy. (I2)
- `P+0x501` (our `COF_RETAIL_GIBBED`) is `m_bCantBeRevived` (the right byte to test); the real `m_bGibbed` is `P+0x1F29`.
  `0x10223190` is `g_pWorld` (CWorld), `+0xE0..+0xEC` its `player1model..player4model`. `0x1005FC20` is
  `CPhoneCall::ChangeSubtitle`. `0x10222F14` LIKELY `g_iSurvivalMode`, `0x10222F20` LIKELY `m_iSimonMode`,
  `0x10226480` `gSpecialItemGive`, `0x10222E10` LIKELY `g_pLevelChange`. (I2)
- `2026-10-12-cutscenes-re.md`: `cof_mdlcutscene` never removes its camera models. `func_valve` fires from a think with
  itself as activator, so the c_ending1c-3c strangle chain reaches `cof_strangle` with FIRST = host (settles a
  LIKELY). (C1-3, C2-11)
- `2026-10-12-map-chains-re.md` 1.6 and 2.6: `trigger_camera` already applies to every player. (C1-4)
- `2026-10-12-endings-phone-re.md`: call answered after 0.1 s; boss bar lost only by a respawned player (joiners and
  restores get it). (C2-9)
- `2026-10-12-player-state-re.md`: FL_GODMODE does not protect a climber (cleared each usercmd); wheelchair death in MP
  is a permanent hold. (A1, C2-3)
- `2026-10-12-companion-weapons-items-re.md`: §2 limits are SP only; §4.1 the crazybitch grabs edict 1; §3.2 flare and
  lantern light; §3.3 the camera freezes monsters (no damage) until they die; +0xC4 = `is_donator`; reload timing,
  stamina rates, sights spread (D notes). (B3, D1, D10, H1)
- `2026-10-12-companion-bots-cpr-monsters-re.md` §3.3/§3.4 and `docs/companion.md`: spawnflag 128; `FVisible` ignores
  FL_NOTARGET (only `Look` filters it), and in MP a player being revived (`fuser3 == 3`) is invisible to monsters. (E5, E6)
- `2026-10-17-merging-the-rest.md` 4.1 (fog at start), 4.2/4.3 (false refusals), 4.6 (sky moot). (B5, C2-6, F6)
- `docs/research/2026-10-12-map-chains-re.md` 2.x / AGENT_NOTES: liblist `-num_edicts` was ignored by retail too. (H3)

## 7. Robustness of a public build

- **Chat text is used as a printf format string** in client.dll (`saytext.cpp:147`): a `%n` or `%s` in chat can crash
  every client that shows it (in the console on story maps). Filter `%` in say text in the engine. (I5)
- **Unchecked client indices**: `quickselset1-3 <n>`, `inventoryequip/use/drop/check <n>`, `recalldocument <n>` index
  arrays with the client's number unchecked; any client can make the server read out of bounds and crash. Our engine
  already parses some of these for the 12-slot inventory (`sv_cof_inventory12.c`); bound all of them. (I6)
- `AddNote` slot 16 writes past the client's notes array into the iron-sight/fade/clothes members; our engine sends
  notes up to slot 16. (G9, I5)
- Nine entity classes lose their base-class fields in saves (`IMPLEMENT_SAVERESTORE` on CBaseEntity); `CRoofBoss`
  passes a 32-entry list with max 96 (a stack overflow more likely with more players); `ChangeLevelNow` tests only
  edict 1 for the transition volume. (I5, C1-12)

## 8. Building our own game DLLs from this source

Facts (I1/§1): both DLLs link completely from source today; hl.dll has been played through the c_start intro into
c_start2; client.dll links, loads and shows the HUD in c_start (its least-verified function is the particle update,
30 %). Toolchain: MSVC 19.27.29112 (VS 2019 16.7) with Windows 10 SDK 10.0.19041 (plus the third-party libraries the
game ships). Blockers: our engine is tied to the retail bytes (SHA-256 gate in `sv_cof_cheats.c:59`, 40+ RVA patch and
call sites, the in-memory client patches), so a rebuilt DLL switches story co-op off until that is reworked; the
decompilation has no licence file (its README defers to the Half-Life SDK licence; the CoF code belongs to Team
Psykskallar); and the uni-notes rule above. This is a separate decision for the user, not part of any current wave.

## 9. Reference data in the slice notes

- Every user message hl.dll sends and client.dll hooks, with formats (G, A, C1, C2).
- Every client command with its arguments and gates (B, G).
- Cvars of both DLLs (G, B), worldspawn keys (B), the per-weapon table of what the AI companion can do (D).
- The offset audit: ~150 hard-coded offsets, sizes and addresses in the engine and `tools/hlre.py`, all agreeing (I §2);
  the recomputed CBasePlayer layout (0x281C, `helpers/_i_player_layout.txt`).
- A VA -> function-name list for the 253 addresses our research docs quote most (I §4).
- The full save flow through the tape recorder (C2-10), the developers' own co-op mode end to end (B), entities that
  pick "the first player" inside thinks (C2), the CoFLaunchApp launch line and registry keys (H10).

## Suggested order (agent's recommendation; the user decides)

1. Small, certain engine fixes: the `is_donator` command copy (1.2), the companion join line (`P+0x24D0`, section 3), the
   ladder busy test (1.1), `HealthCap` (1.7), the chat `%` filter and index bounds (7), the fourth model attachment
   (1.8).
2. Soft-locks before the next long playtest: chainsaw boss (2.1), padlock (2.2), wheelchair (2.3), crazybitch (2.5);
   then the boat (2.4) after a live look.
3. Costumes unlock (section 3, `sv_cof_bonus.h`), fog replay on respawn/join (section 3).
4. Merged maps: rain and 3D sky per section, planner checks (first-of-class, 16 pairs, REF_KEYS, false refusals), the
   far-clip cvar, then per-section fog and view distance (wave 3).
5. Decide the carry-limit and police-voice questions (section 4) with the user.
