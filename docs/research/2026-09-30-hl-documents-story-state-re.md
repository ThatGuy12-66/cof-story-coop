# hl.dll documents, codes and per-player story state (static RE), 2026-09-30

> **Corrections (2026-10-18, decompilation survey, `docs/research/2026-10-18-cof-decompilation-survey.md` section 6):**
> - §5: the dialer does not need `weapon_mobile` for `cof_phonecheck`.

Target: `runtimes/enhanced/cryoffear/cl_dlls/hl.dll` (identical to `hl.dll.orig`, sha256 prefix `0036b91c01e92ed2`), image base 0x10000000. Cross-checked against `client.dll` in the same folder. This was a static-only pass: capstone linear disassembly, RTTI vtable scan and TYPEDESCRIPTION dumps. The game was not run.

Tags: **STATIC-CERTAIN**: read directly from the code. **STATIC-LIKELY**: an inference from the code that was not traced end to end. **UNKNOWN**: not determined.

Conventions used below:
- `P+x` is a `CBasePlayer` offset; the player vtable is 0x101A4DA8 and its size is 0x281C.
- `pev+x` is an `entvars_t` offset, confirmed from the DLL's entvars save table: target 0x1CC, targetname 0x1D0, message 0x1D8, iuser1..4 0x248..0x254, spawnflags 0x1A4, flags 0x1A8, owner 0x19C.
- CoF adds 11 virtuals to `CBaseEntity`, so Spawn is slot 11, KeyValue 13, IsPlayer 52 (+0xD0) and Use 58 (+0xE8).

Engine and helper calls:
- `[0x10223000]` MessageBegin, `[..04]` End, `[..08]` WriteByte, `[..10]` WriteShort, `[..20]` WriteString, `[0x10223188]` gpGlobals.
- `0x100125E0` = `GetClassPtr<CBasePlayer>(pev)`.
- `0x10119680` = `UTIL_PlayerByIndex`.
- `0x10118460` = `UTIL_FindEntityByClassname(start, name)`.
- `0x10101FF0` = `FireTargets`.

Some code finds its player with `UTIL_FindEntityByClassname(NULL,"player")`. This document calls that player the **FIRST player**: the lowest-index player edict, which is normally the listen-server host. It is *not* the activator.

User-message ids are set in the registration block at 0x100E6000..0x100E6AE1. The relevant ones are NoteDoc `[0x10226588]`, Keypad `[0x1022658C]`, UserDoc `[0x102265F0]`, Computer `[0x102265EC]`, Padlock `[0x102265D0]`, BossBar `[0x10226620]`, AddNote `[0x10226648]`, Objectives `[0x1022669C]` and UnreadSMS `[0x10226578]`. `Documents` `[0x1022663C]` is registered but the server never sends it.

## 1. Documents (`documentsstruct`, `cof_document`)

**Entity layout (STATIC-CERTAIN).** `cof_document` is `CCoFDocument`: factory 0x10018560, size 0x210, vtable 0x101A7D98.
- KeyValue 0x10095670 fills these fields:
  - `page1..page4` → `+0xDC/+0xFC/+0x11C/+0x13C` (char[32] each)
  - `note1..note4` → `+0x15C/+0x17C/+0x19C/+0x1BC` (char[32])
  - `turnsound` → `+0x1DC` (string_t, ALLOC_STRING)
  - `label` → `+0x1E0` (unbounded strcpy)
- Spawn 0x1009CD30 sets `pev->effects = 0x80` (EF_NODRAW). The document is an invisible logic entity that another entity fires, and the activator arrives through that firing chain.

**One player entry (STATIC-CERTAIN).** The field is `char[288]` (FIELD_CHARACTER). Entry `i` (1..16) is at `E = P+0x688 + 0x120*i`, so entry 1 is `P+0x7A8`.

| E+ | size | content (copied in `AddDocument`) |
|---|---|---|
| 0x00/0x10/0x20/0x30 | 16 each | page1..page4 |
| 0x40/0x50/0x60/0x70 | 16 each | note1..note4; `E+0x40` non-empty = slot used, and it is the dedupe key |
| 0x80 | 0x30 | STRING(turnsound) |
| 0xB0 | 0x40 | title = first line of `<gamedir>/notes/<note1>`, read **by the server** with ifstream |
| 0xF0 | 0x30 | label |

The copies are unbounded. Entity strings are 32 bytes but the slots are 16, so a long name overflows into the next field. This is stock behaviour.

The field meanings are STATIC-LIKELY, based on the `cof_passwordnote` usage below. `noteN` is the text-file name under `notes/`. `pageN` is a page key, for example `user3` or `pass3`. `label` is the background tga, for example `gfx/vgui/notes/hiddennote1.tga`.

**Flow (STATIC-CERTAIN):**
1. **`CCoFDocument::Use` (0x100A3B90).** It requires `pActivator->IsPlayer()`. It sets the **activator's** `m_hDocument` EHANDLE (`P+0x630`, via 0x10017740). It then sends `MSG_ONE NoteDoc` **to the activator only**, containing page1-4, note1-4 and STRING(turnsound). This opens the reading UI and stores nothing.
2. **Client command `closedocument` (0x1001A061).** It takes the *sender's* `m_hDocument` and does `GetClassPtr<CCoFDocument>` (0x10018560). It then calls **`CBasePlayer::AddDocument(CCoFDocument*)` at 0x100DA6D0** (thiscall, `ret 4`, call site 0x1001A0B3) and clears `m_hDocument`.
3. **`AddDocument` (0x100DA6D0):**
   - Returns if any used slot's `note1` equals `doc->note1` (dedupe).
   - Finds the first empty slot and opens `GetGameDir()+"/notes/"+note1`. If the file is missing or empty, **nothing is added**.
   - On line 1 of the file it sends ProFont `Archived "%s"` to that player (0x10119810), fills the slot, and sends **`AddNote`** MSG_ONE to that player.
   - The AddNote payload is: byte slot, byte isCode=0, short 0, byte style=0, then 11 strings: note1-4, page1-4, turnsound, title, label.
4. **Resync (STATIC-CERTAIN).** In `CBasePlayer::UpdateClientData` (vtable slot 150, 0x100F6CA0), under `m_fInitHUD` (`P+0x24CC`), the loop at 0x100F71D5-0x100F729C re-sends `AddNote` for **all 16 slots**, including empty ones, from server state. This happens after spawn, level change and load.
5. **`recalldocument <n>` (0x1001A01C → 0x100F0AA0).** It re-sends `NoteDoc` from the sender's own slot n.
6. **Client side (STATIC-LIKELY).**
   - The AddNote hook is `client.dll` 0x1006E440 → 0x1006F190, which stores into a per-slot 0x1A0 array.
   - Text is read by the client itself: the client builds `"/notes/"` paths at 0x100471AE and 0x1004848E and opens them via 0x10026090.
   - Code notes are drawn from `gfx/vgui/notes/code_%i.tga` using the short, with a template picked by style: codefrompc, codefromvideotape, codefromwindow or phonecode.

**Replicating to another player.**
- **Recommended.** Call `AddDocument` with `ecx = other CBasePlayer*` (from `GetClassPtr<CBasePlayer>(UTIL_PlayerByIndex(i)->pev)`) and the same `CCoFDocument*`. This writes that player's saved state, sends that player AddNote plus the "Archived" text, and is idempotent through the note1 dedupe (STATIC-CERTAIN). The natural hook is right after 0x1001A0B3, looping over the other connected players. Limits:
  - It needs the doc entity to still exist. No path removes it (STATIC-CERTAIN for Use, closedocument and AddDocument).
  - It needs the notes file to be readable relative to the server's CWD.
  - MSG_ONE must target a spawned client.
- **Alternative, intrusive.** Calling `CCoFDocument::Use(P2, …)` (0x100A3B90, vtable+0xE8) pops the reading UI on P2. P2 is archived only after P2's client sends `closedocument`.
- **Message-only is not durable.** Sending only `AddNote` is client-side and is **wiped** by the next InitHUD resync (step 4), because the server slot is empty (STATIC-CERTAIN).

## 2. Codes, notes and keypads

**Where a keypad code lives (STATIC-CERTAIN).** It is per-player: `m_iKeypadNumber` (`P+0x2040`, saved) and `m_iKeypadType` (`P+0x2044`, saved). Nothing initialises `P+0x2040`, so a fresh player has **0**. 9999 (0x270F) means "used".

| Writer | Player | Value |
|---|---|---|
| `cof_keypad2` Use 0x100A6FE0 | activator | `"279%i"`, RANDOM_LONG(1,9); type=1; `Keypad(1,0,0)` |
| `cof_keypad3` Use 0x100A70A0 | activator | type=2 only; `Keypad(2,0,0)` |
| `cof_keypad4` Use 0x100A7120 | FIRST | fixed 5247 (0x147F), type=2 |
| `env_model` Use 0x100A8E80 / `monster_generic` Use 0x100B0EB0 | FIRST | `"%i7%i9"` from the entity's digits |
| `cof_updatekeypad` Use 0x100A7FB0 | passes activator through | `FindEntityByTargetname(message)->Use(activator, caller, USE_TOGGLE)`, so behaviour depends on which keypad2/3/4 it targets |

**How the code is revealed (STATIC-CERTAIN):**
- **`cof_addcodenote` Use (0x100A6790).** It targets the **FIRST player**, not the activator. It writes the "nerds" placeholder into the first empty slot and sends `AddNote(slot, 1, first->m_iKeypadNumber, pev->iuser1 = style, "nerds"…)`. The code is **not** stored in `documentsstruct`; only the message carries it. After an InitHUD resync the slot comes back with isCode=0, so code-note display after reload is UNKNOWN (possibly blank).
- **`cof_telescope` Use (0x1007D430).** It shows ProFont "The board said %i" with the **activator's** code, and `telescopefinish` (0x1001C4D1) adds a code note to the command sender.
- **`trigger_checkbrush` Use (0x10028C40).** It shows the **activator's** code: "I don't want to watch it again... %i".

**Entering the code (STATIC-CERTAIN).**
- `cof_keypad` Use (0x100A71A0) returns immediately if the keypad's `+0xDC` "unlocked" byte is set. Otherwise it sends `MSG_ONE Keypad(byte m_iKeypadType, byte 0, byte 1=open)` to the **activator only**, so the UI is per-player.
- The client replies `keypadnumber <n>` (0x10019409), which calls `CBasePlayer::KeypadNumber(n)` at 0x100E5890 on the **sender**:
  - It finds the **first** `cof_keypad` in the map.
  - Success requires `n == sender->m_iKeypadNumber && != 9999`. On success it:
    - sets keypad `+0xDC=1`
    - shows ProFont `message` or "You unlocked the door."
    - calls `FireTargets(keypad->pev->target, sender, sender, USE_TOGGLE)`, which is a global effect
    - sends `Keypad(type,1,0)`
    - sets the sender's code to 9999
  - Otherwise it sets `+0xDC=0` and plays `keypad_deny.wav`.

**Holding the note is not required.** No document is checked. The typed number must equal **the typing player's own** `m_iKeypadNumber`, however. Consequences:
- P2 typing P1's code fails whenever the code was assigned only to P1, either as activator or as FIRST.
- A player whose code was never assigned, a fresh 0, passes with `keypadnumber 0`. The server logic is STATIC-CERTAIN; whether the client UI can emit 0 is UNKNOWN, but the console command reaches the server.

**`cof_passwordnote` (`CPasswordNote::NoteUse` 0x10083120, KV `usertype`→pev->iuser1) (STATIC-CERTAIN):**
- The user/password pair is **global**. It comes from the FIRST `cof_computer`'s `pev->iuser2/iuser3`.
- On the first use it creates a `cof_document` (page1=`user%i`/`pass%i`, note1=`<name>.txt`, label=hiddennote2/1.tga) and sets **only that first reader's** `m_hDocument`.
- Every user gets `MSG_ONE UserDoc(byte 1|2, byte index)`. Later readers see the note but it is **never archived** for them.

**Computer and padlock (STATIC-CERTAIN):**
- `computercheck <user> <pass>` (0x10019EFD → `CCofComputer::Check` 0x100934A0) can be typed by anyone. The credentials are compared against the computer's stored strings.
  - On the first success it sets computer `iuser1=1`, runs `FireTargets(target)` and sends `Computer`. It then adds a code note built from the **logging-in player's** 4 digits `P+0x235C..0x2368`. Those digits are random 1..9 per player in `CBasePlayer::Spawn` 0x100F4090 and are **not saved**.
  - A correct login while `iuser1!=0` does nothing, so a second player gets no code. A wrong login resets `iuser1=0`.
- `item_padlock` Use (0x100B6520) copies **the activator's** digits into the padlock's `fuser1..4`. `CItemPadlock::Check` (0x100B5F60) compares the wheels against those digits. On unlock it clears the FIRST player's `m_pPadlock` (`P+0x2350`).
- The result is that a padlock opened by P2 expects P2's digits, which P2 can only learn from their own computer login.

`cof_keypadnew` (`CKeypadNew::KeypadUse` 0x1001FB40) drives the FIRST player's camera and UI (AttachCam), not the activator's (STATIC-CERTAIN).

## 3. `cof_objective` (Use 0x100A4B00)

**STATIC-CERTAIN:**
- It ignores the activator. On the **FIRST player** it sets `m_sObjectiveText` (`P+0x1E90`, saved) = `pev->message`, for example `objective_b`. If `pev->target` is non-null, it sets `m_sCustomObjectives` (`P+0x420`) to it as a file override.
- It sets a **single global** byte `0x10222E54` ("new objective").
- The per-frame player virtual (vtable slot 148, 0x100EAD70, block at 0x100EE9AB-0x100EEBEA) compares `P+0x1E90` against a last-sent cache at `P+0x1E8C`. When they differ it sends `MSG_ONE Objectives(string key, string txtfile, byte isNew)`.
  - The txtfile is `txtfiles/hints.txt` or `txtfiles/languages/<lang>/hints.txt` (from cvar `cof_subtitlelanguage`), or the custom file.
  - isNew is the global byte, cleared by the first player who sends.
- The client resolves the text from the file (hook 0x1006E8B0, STATIC-LIKELY).
- P2 never gets objectives. Copying `P+0x1E90` (and `P+0x420`) to other players would make their own think send it automatically (STATIC-LIKELY).

## 4. Ending flags and `m_iDoctorWeapon`

**Writers (STATIC-CERTAIN).**
- The only writer of `P+0x4CD/0x4CE/0x4CF` is **`cof_ending` Use (`CCoFEnding` 0x100A3C80)**. It loops `i = 1..gpGlobals->maxClients` with `UTIL_PlayerByIndex`, and sets the flag on **every connected player** according to `pev->iuser1`: 0 → RoofBossKilled, 1 → GivenP345, 2 → DeliveredPackage.
- Players who are not connected at that moment, and state that is not carried over for P2, never get the flag.

**Reader (STATIC-CERTAIN).** `CInterDoor::EndSequence` (0x10048070) reads the flags.
- `CInterDoor::Use` (0x10048F70, write at 0x1004923F) sets `pev->owner = activator`, and EndSequence uses **that activator's flags only**.
- When the door's `target == "endingcheck"`, it selects the exit:
  - !Roof & !P345 → `ending_exit1`
  - !Roof & P345 → `ending_exit2`
  - Roof & !P345 → `ending_exit3`
  - Roof & P345 → `ending_exit4`
  - `DeliveredPackage` overrides all of these → `ending_exit5`
- It then teleports **only the activator** to that target.
- `closegame1..4` are demo-end branches.
- `cof_credits` (0x100A2710) evaluates each player's own flags in an all-player loop. `CInterDoorCoop` does not read the flags.

**`m_iDoctorWeapon` (`P+0x5B0`) (STATIC-CERTAIN).**
- `cof_doctorweaponset` (0x100A3AD0) sets the FIRST player's value to `pev->iuser1`.
- `cof_doctorweapontrigger` (0x100A3B10) reads the FIRST player's value: ==1 fires `pev->target` and ==2 fires `pev->message`, with the FIRST player as activator.
- `CDoctorBoss` (vtable slot 77, 0x1002D800 region, read at 0x1002D845) also reads the FIRST player's value.
- It is consistent host-only state.

## 5. Phone and boss bar (brief)

- **`cof_phonecall`** (`CPhoneCall::Use` 0x10060FA0 → WaitForCall 0x10061000): **activator only** (STATIC-CERTAIN).
  - If the activator lacks `weapon_mobile`, the think is not re-armed and the call is silently dropped.
  - It rings the activator and sets that player's `m_bInPhoneCall` (`P+0x1EE8`). EndCall (0x1005FF90) fires `target`.
- **`cof_phonecheck`**: no Use. `phonenumber <n>` from **any** player (0x100197DB → 0x100E9FF0) finds the first `cof_phonecheck`, sets its owner to the dialer and compares against `pev->impulse`, then fires targets. The effect is global; the dialer needs `weapon_mobile` and must not be in a call (STATIC-CERTAIN).
- **`cof_phonedisable`** (0x100A7480): **FIRST player only** gets `m_iPhoneDisabled` (`P+0x1F14`) =1 and their mobile flagged (STATIC-CERTAIN).
- **`trigger_cofmobile`** (SMS) (0x100A7C50): all players who own a mobile (STATIC-CERTAIN).
- **`cof_bosshealthbar`** (0x100A5BF0) (STATIC-CERTAIN):
  - If `g_pGameRules->IsMultiplayer()` (vfunc +0x20) is true and the use value != 666.0, it sets `m_bBossHealthBar` and the bar fields on **all players** and sends `BossBar` to each.
  - Otherwise it applies to the activator only.

## 6. Other per-player or asymmetric story entities

| Entity / command | Per-player aspect | VA | Tag |
|---|---|---|---|
| `cof_goodpoints` / `cof_goodpointstrigger` | activator's `m_iGoodPoints` (`P+0x5F0`, saved, init 2 at 0x100F44EA) ±1; trigger branches to 1 of 5 targets on the **activator's** points | 0x100A6D90 / 0x100A6DF0 | CERTAIN |
| `cof_telephone` + `telephoneright/wrong` | UI (`Telephone`) to activator, `P+0x368` handle; result fires globally | 0x1007CF70 / 0x1001C731 | LIKELY |
| `cof_yesno` + `yesnoactivate <ent>` | prompt to activator only; answer fires globally | 0x100A8140 / 0x1001C9D3 | LIKELY |
| `cof_computer` Use | `Computer` UI to activator; sets activator `P+0x1EA0` | 0x100A6980 | CERTAIN |
| `cof_clothesmenu`, `cof_startdoctormode`, `cof_greyfade`, `cof_simpletext`, `cof_lookat`, `cof_strangle`, `cof_stopwheelchair`, `cof_cracker`, `cof_lobby_start`, `cof_stats` | act on FIRST player | 0x100A2050, 0x100A5070, 0x100A41D0, 0x100A4CB0, 0x100A4580, 0x10076D10, 0x100A58C0, 0x100A6A60, 0x100A4500, 0x100A51C0 | LIKELY (classname "player" lookup seen; bodies not fully read) |
| `cof_hint`, `cof_camerazoom`, `cof_simonspeak`, `cof_ladderclimb`, `cof_maxhealthchange` | activator only | 0x100A7600, 0x100A5F30, 0x100A4C30, 0x100A43A0, 0x100A73E0 | LIKELY |
| all-player (already shared) | `cof_ending`, `cof_chapter`, `cof_credits`, `cof_blackandwhite`, `cof_customiseplayer`, `cof_killplayer`, `cof_changelevel`, `cof_begingame`, `trigger_cofmobile` | loops at 0x100A3C80, 0x100A5FF0, 0x100A2710, … | CERTAIN (loop present) |

The inventory and level-exit entities are covered by the other two research passes.

## Implications for sharing story state between two players

1. **Documents.** Hook `closedocument` after 0x1001A0B3, or wrap `AddDocument` 0x100DA6D0, and call `AddDocument(otherPlayer, doc)` for each other connected player. This is idempotent (dedupe on note1), persistent (saved `documentsstruct`) and gets the resync for free. Do not rely on client-only `AddNote`, because InitHUD overwrites it.
2. **Password notes.** Archiving goes only to the first reader. Either set `m_hDocument` on the later readers as well, or broadcast `AddDocument` as in item 1. The computer's user and password are already global.
3. **Keypad codes.** Keep `m_iKeypadNumber`/`m_iKeypadType` (`P+0x2040/0x2044`) identical on all players. Mirror every write from the keypad2/3/4, env_model, monster_generic and updatekeypad paths, or copy them from the host each frame or on change. Mirror the code notes too: replicate the 0x100A6790 logic per player. Otherwise P2 cannot open the door with the code they read, while a player with the default 0 can pass with `keypadnumber 0`.
4. **Computer and padlock.** Copy the host's digits `P+0x235C..0x2368` to every player after each Spawn. Without that, the note only matches the logging-in player's padlock, and a second correct login gives no code at all.
5. **Objectives.** Copy `P+0x1E90`, and `P+0x420` when it is set, from the FIRST player to the others. Their own think sends `Objectives`. The "new" flag is one global, so later players see isNew=0.
6. **Endings.** The flags are already set on everyone connected when `cof_ending` fires. The risks are players absent at that moment, flags lost for P2 across transitions if the coop path doesn't carry saved player fields, and the fact that only the door activator is teleported to `ending_exitN`. To be safe, OR the three flags across all players before EndSequence, and move all players.
7. **Host-only state.** `m_iDoctorWeapon`, `cof_phonedisable`, objectives, `cof_addcodenote`, `cof_keypad4`, `cof_keypadnew` and several cutscene entities target the lowest-index player, not the activator. They work if the host is player 1 and stays connected. If the host slot empties, they hit the wrong player (or none).
8. **Activator-only state.** Phone calls need the activator to own `weapon_mobile`, and good points belong to the activator, so a branch depends on who triggers it. Decide per case whether to mirror these to all players or to leave them per player.
