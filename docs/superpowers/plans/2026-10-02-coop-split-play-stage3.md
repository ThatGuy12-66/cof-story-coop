# Split play, stage 3: padlock fix, live facts, exit hold and warp — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A guest is no longer frozen by the padlock screen, a level exit waits until both players are at it, and either player can press J to join the other.

**Architecture:** Everything is in the Xash engine's opt-in story co-op layer.
- **Sender scope:** while the game DLL runs one player's client command, its "find the first player" lookup answers with that player (`sv_cof_sender.h`, hooked in `SV_FindEntityByString`).
- **Exit hold:** a pure readiness state machine (`sv_cof_exit_state.h`) plus engine glue (`sv_cof_exit.c`) hooked at `pfnChangeLevel`. A held exit is stored once and released by the frame, so it never depends on the game asking again.
- **Party edits:** doors and scripted teleports take only a partner within 128 units, players left behind get a prompt, and `cof_warp` (J) moves a player beside the partner.
- **Retail layer:** two new calls for the stage 4 fact checks, `AddDocument` and a code `AddNote`.

**Tech Stack:**
- C (MSVC x86 via the existing waf build) and Python 3.13 unittest.
- Native offline harnesses compiled with VS 2022 BuildTools through `tests/native.py`.
- The game engine is Xash3D FWGS with the Cry of Fear Enhanced patch stack.

**Spec:** `docs/superpowers/specs/2026-09-30-coop-split-play-design.md` (v2), parts 1 and 4, plus the last two "facts static analysis could not settle". Research: `docs/research/2026-09-30-hl-exits-coop-re.md` and `docs/research/2026-09-30-hl-documents-story-state-re.md`. The padlock root cause is in `AGENT-HANDOFF-2026-09-27.md`, section "two-player doors and a padlock soft-lock".

**Stage 4 (shared codes, notes, objectives, ending flags, bag loss) gets its own plan after Task 3.** Its rules for notes and code notes depend on the two live facts Task 3 records, the same way stages 3–4 waited for stage 1's results.

## Global Constraints

- **Retail game untouched:** no map, `hl.dll` or `client.dll` changes. All behavior is inside the engine's opt-in story layer: `cof_story_coop 1`, with `SV_CoF_StoryCoopEnabled()` true. That function also requires the verified retail DLL (SHA-256 `0036B91C…0E63`).
- **Retail facts in one place:** player offsets, RVAs and message layouts live only in `sv_cof_retail.c`. The one exception that already exists is `sv_cof_party.c`'s door flag and inventory offsets.
- **Never call game callbacks from the party's per-frame loops.** Moves use `CoF_PartyMove`; the regular movement pass touches triggers on the next command.
- **Log format:** every log line starts `cofstory phase=`. This plan adds `exit-hold`, `exit-ready`, `exit-forced`, `exit-cancelled`, `exit-unholdable`, `warp`, `warp-refused` and `cut-off`.
- **Prompts must not rely on colour.** Stage 1 found that the ProFont colour index did not render. Every prompt's words must be enough on their own.
- **The warp must work without a fade.** Stage 1 could not confirm that `ScreenFade` is visible. The fade is sent, and nothing depends on it.
- **Code style:** Xash style. Tabs, spaces inside parentheses (`if( x )`), `//` comments that say why, and LF line endings in new engine files.
- **Two repositories:**
  - The engine is `upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a`, branch `story-coop`; called `$ENGINE` below.
  - Tests, tools and docs are the project root. Work on a new branch `split-play-stage3` (Task 1 creates it).
  - No worktree: the build and deploy paths are tied to this checkout.
- **Running tests.** Run in PowerShell from the project root:
  ```powershell
  Remove-Item Env:NoDefaultCurrentDirectoryInExePath -ErrorAction SilentlyContinue
  $env:PATH = "$env:PATH;C:\Program Files (x86)\Microsoft Visual Studio\Installer"
  & python -m unittest <module> -v
  ```
  Use this interpreter path, not bare `python`. The suite has 117 tests before this plan.
- **Building:** `& cmd.exe /d /c tools\build_enhanced.cmd`. The output is `$ENGINE\build-local-20260927\engine\xash.dll`. New `engine/server/*.c` files are picked up by the build's glob.
- **The live game:**
  - Tell the user before launching the game.
  - Never kill or restart their session.
  - Deploy only while no `CoFLaunchApp.exe` or `cof.exe` is running.
  - Never print passwords: RCON output from `tools/enhanced_local_control.py` is already redacted.
  - Call the two players "the two little rascals".

## Review Focus

These are the failure modes a player is most likely to hit that the spec implies but does not spell out. Each has a pinned test in the task that owns the code.

1. **An exit whose trigger fires only once.** Several exits are fired by a `trigger_multiple` or a script, which may never ask again. A held exit must still go when the partner arrives. (Tasks 4 and 5: one request, then only frames.)
2. **The partner disconnects or dies while the other waits at the exit.** The waiting player must not be stuck: a disconnect releases the exit, and a death is covered by the escape hatch. (Task 4.)
3. **J mashed by the waiting player.** Repeated presses before the 10 seconds are up must not restart the timer or leave early. (Tasks 4 and 5.)
4. **J pressed at a bad moment:** while the caller or the partner is in a door sequence, or while the partner is mid level change. The caller stays where they are. (Task 7.)
5. **A command from the host, or from a player whose edict is not in play.** The sender scope must leave the plain lookup order alone, so nothing changes for the host. (Task 1.)

---

## File structure

| File | Repo | Responsibility |
|---|---|---|
| `$ENGINE/engine/server/sv_cof_sender.h` | engine | Pure: the order of "player" lookups while one player's command runs |
| `$ENGINE/engine/server/sv_game.c` | engine | + sender scope in `SV_FindEntityByString`; exit hook in `pfnChangeLevel`; quiet repeat in `pfnAlertMessage`; `SV_CoF_MapExitModels` reports firing keys |
| `$ENGINE/engine/server/sv_client.c` | engine | + sender scope around the game's `ClientCommand`; `cof_warp` dispatch |
| `$ENGINE/engine/server/server.h` | engine | + declarations of `SV_CoF_CommandSender`, `SV_CoF_LiveLandmark`; new `SV_CoF_MapExitModels` signature |
| `$ENGINE/engine/server/sv_cof_story.c` / `.h` | engine | + `SV_CoF_StoryCallMember`; exit frame and reset wiring |
| `$ENGINE/engine/server/sv_cof_retail.c` / `.h` | engine | + open document, `AddDocument`, code note, document count; probe `doc`, `codenote`, `fade` hold |
| `$ENGINE/engine/server/sv_cof_exit_state.h` | engine | Pure: who must be at an exit, waiter, escape hatch, prompt timing |
| `$ENGINE/engine/server/sv_cof_exit.c` / `.h` | engine | Exit hold glue: request, frame, prompts, warp key, quiet alert |
| `$ENGINE/engine/server/sv_save.c` | engine | `CoF_LiveLandmark` exported as `SV_CoF_LiveLandmark` |
| `$ENGINE/engine/server/sv_cof_party.c` / `.h` | engine | + exported `SV_CoF_PartyInExit`; door and teleport radius; cut-off prompt; `cof_warp` |
| `tests/engine/cof_sender_test.c`, `tests/test_enhanced_sender.py` | project | Sender scope rules and the engine hook |
| `tests/engine/cof_retail_harness.c` | project | + documents, code note, new probe commands |
| `tests/engine/cof_exit_state_test.c`, `tests/test_enhanced_exit_state.py` | project | Exit rules |
| `tests/engine/cof_exit_harness.c`, `tests/engine/cof_exit_models_test.c`, `tests/test_enhanced_exit.py` | project | Exit glue against doubles; the map guard parser |
| `tests/engine/cof_party_harness.c` | project | + radius, cut-off and warp scenarios |
| `tools/Start-StoryHost.ps1`, `tools/Start-LocalGuest.ps1`, `tools/friend_setup.py`, `tests/test_warp_bind.py` | project | `bind j cof_warp` for host, local guest and the remote friend's setup |
| `tools/story_run_report.py`, `tests/test_story_run_report.py` | project | Count and warn on the new phases |

---

### Task 1: The sender is "the first player" during their own command

The retail DLL finds "the player" with `UTIL_FindEntityByClassname( NULL, "player" )` (`0x10118460`) in 87 places. That helper pushes `"classname"` and calls engine slot 14, `pfnFindEntityByString`, so the engine decides the answer. Inside a client command the lowest slot is the wrong player whenever the guest sent it: `padlockcancel` restores the host and leaves the guest frozen.

**Files:**
- Create: `$ENGINE/engine/server/sv_cof_sender.h`
- Modify: `$ENGINE/engine/server/sv_game.c` (includes at line 28; new block above the `SV_FindEntityByString` banner at line 1557; inside that function at line 1571)
- Modify: `$ENGINE/engine/server/sv_client.c:3164-3167`
- Modify: `$ENGINE/engine/server/server.h:705`
- Modify: `$ENGINE/engine/server/sv_cof_story.c` (`SV_CoF_StoryShutdown`)
- Create: `tests/engine/cof_sender_test.c`
- Create: `tests/test_enhanced_sender.py`

**Interfaces:**
- Produces:
  - In C, `static int CoF_SenderNextPlayer( int start, int sender, int maxclients, const unsigned char *present )`.
  - In C, `void SV_CoF_CommandSender( edict_t *sender )` (NULL ends the scope).

- [ ] **Step 1: Create the project branch**

Run from the project root: `git switch -c split-play-stage3`
Expected: `Switched to a new branch 'split-play-stage3'`. The engine repo stays on `story-coop` (`git -C $ENGINE branch --show-current`).

- [ ] **Step 2: Write the failing test**

`tests/engine/cof_sender_test.c`:
```c
/* Who "the first player" is while one player's client command runs: the pure
 * order, then the engine hook cut out of sv_game.c against small doubles. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char byte;
typedef int qboolean;
#define true 1
#define false 0
#define MAX_CLIENTS 32
#define Q_strcmp strcmp
typedef struct edict_s { int free; struct { int classname; } v; } edict_t;
static edict_t entities[8];
static struct { int maxclients; } svs;
static int spawned[8];
#define NUM_FOR_EDICT( e ) ((int)(( e ) - entities ))
static edict_t *SV_EdictNum( int i ) { return &entities[i]; }
static qboolean SV_IsValidEdict( const edict_t *e ) { return e && !e->free; }
static void *SV_ClientFromEdict( const edict_t *e, qboolean spawned_only ) { return spawned[e - entities] ? (void *)e : NULL; }
static const char *SV_GetString( int s ) { return s == 1 ? "player" : "other"; }

#include "sv_cof_sender.h"
#include "sender_under_test.c"

int main( void )
{
	static const unsigned char both[3] = { 0, 1, 1 }, three[4] = { 0, 1, 1, 1 }, gap[5] = { 0, 1, 0, 1, 1 };
	int e, i;

	/* the guest (edict 2) sent the command: it is "the first player" */
	assert( CoF_SenderNextPlayer( 0, 2, 2, both ) == 2 );
	/* a loop over every player visits the sender, then the rest, then stops */
	assert( CoF_SenderNextPlayer( 2, 2, 2, both ) == 1 && CoF_SenderNextPlayer( 1, 2, 2, both ) == 0 );
	/* Review Focus 5: the host's own commands keep the plain order */
	assert( CoF_SenderNextPlayer( 0, 1, 2, both ) == 1 && CoF_SenderNextPlayer( 1, 1, 2, both ) == 2 && CoF_SenderNextPlayer( 2, 1, 2, both ) == 0 );
	/* three players, the middle one sends: 2, 1, 3, end; nobody twice */
	assert( CoF_SenderNextPlayer( 0, 2, 3, three ) == 2 && CoF_SenderNextPlayer( 2, 2, 3, three ) == 1 );
	assert( CoF_SenderNextPlayer( 1, 2, 3, three ) == 3 && CoF_SenderNextPlayer( 3, 2, 3, three ) == 0 );
	/* empty client slots are skipped */
	assert( CoF_SenderNextPlayer( 4, 4, 4, gap ) == 1 && CoF_SenderNextPlayer( 1, 4, 4, gap ) == 3 && CoF_SenderNextPlayer( 3, 4, 4, gap ) == 0 );

	/* the engine hook: two players in play, one item entity after them */
	svs.maxclients = 2;
	for( i = 1; i <= 2; i++ ) { entities[i].v.classname = 1; spawned[i] = 1; }
	/* outside a command nothing changes */
	e = 0; assert( !CoF_SenderFind( "classname", "player", &e ) && e == 0 );
	SV_CoF_CommandSender( &entities[2] );
	e = 0; assert( CoF_SenderFind( "classname", "player", &e ) && e == 2 );
	e = 2; assert( CoF_SenderFind( "classname", "player", &e ) && e == 1 );
	/* after the last player the plain search goes on past the clients */
	e = 1; assert( !CoF_SenderFind( "classname", "player", &e ) && e == 2 );
	/* only "the player" lookups are touched */
	e = 0; assert( !CoF_SenderFind( "targetname", "player", &e ) && e == 0 );
	e = 0; assert( !CoF_SenderFind( "classname", "item_key", &e ) && e == 0 );
	/* a search that already left the clients behind is left alone */
	e = 5; assert( !CoF_SenderFind( "classname", "player", &e ) && e == 5 );
	/* Review Focus 5: a sender whose edict is not in play changes nothing */
	spawned[2] = 0;
	e = 0; assert( !CoF_SenderFind( "classname", "player", &e ) && e == 0 );
	spawned[2] = 1;
	SV_CoF_CommandSender( NULL );
	e = 0; assert( !CoF_SenderFind( "classname", "player", &e ) && e == 0 );
	puts( "sender scenarios passed" );
	return 0;
}
```

`tests/test_enhanced_sender.py`:
```python
"""The first-player lookup during a client command, run natively offline."""
import unittest

from tests.native import ENGINE_SERVER, run_native


class SenderTests(unittest.TestCase):
    def test_sender_is_the_first_player_during_its_command(self):
        game = (ENGINE_SERVER / 'sv_game.c').read_text()
        start = game.index('// Cry of Fear story co-op: the player whose client command')
        end = game.index('/*\n=========\nSV_FindEntityByString', start)
        out = run_native(self, 'cof_sender_test.c', {
            'sv_cof_sender.h': (ENGINE_SERVER / 'sv_cof_sender.h').read_text(),
            'sender_under_test.c': game[start:end],
        })
        self.assertIn('sender scenarios passed', out)

    def test_scope_is_opened_and_closed_around_the_game_command(self):
        game = (ENGINE_SERVER / 'sv_game.c').read_text()
        find = game[game.index('static edict_t *GAME_EXPORT SV_FindEntityByString('):]
        find = find[:find.index('\n}\n')]
        self.assertIn('CoF_SenderFind( pszField, pszValue, &e )', find)
        client = (ENGINE_SERVER / 'sv_client.c').read_text()
        call = client.index('svgame.dllFuncs.pfnClientCommand( cl->edict );')
        self.assertIn('SV_CoF_CommandSender( cl->state == cs_spawned', client[call - 200:call])
        self.assertIn('SV_CoF_CommandSender( NULL );', client[call:call + 120])
        # an engine error inside a command must not leave the scope open
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        self.assertIn('SV_CoF_CommandSender( NULL );', story[story.index('void SV_CoF_StoryShutdown('):])


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 3: Run it to see it fail**

Run: `& python -m unittest tests.test_enhanced_sender -v` (with the env setup from Global Constraints).
Expected: both tests ERROR, the first with `ValueError: substring not found`.

- [ ] **Step 4: Write the pure order**

`$ENGINE/engine/server/sv_cof_sender.h`:
```c
/* Cry of Fear story co-op: who "the first player" is while one player's client
 * command runs. Pure, no engine types. GPL-3.0-or-later.
 * The retail DLL finds "the player" with UTIL_FindEntityByClassname( NULL,
 * "player" ) (10118460) in 87 places: the lowest client slot, normally the
 * host. Inside a client command that is the wrong player whenever a guest sent
 * it: padlockcancel (1001C968 -> 100B5EA0) restores the host and leaves the
 * guest frozen. While a command runs the order becomes: its sender, then
 * everyone else. */
#ifndef SV_COF_SENDER_H
#define SV_COF_SENDER_H

// The next "player" edict index after start (0: from the top), or 0 when the
// players are exhausted. present[i] says client edict i (1..maxclients) is a
// player the plain search would return; the sender must be present.
static int CoF_SenderNextPlayer( int start, int sender, int maxclients, const unsigned char *present )
{
	int i;
	if( start <= 0 ) return sender;
	for( i = ( start == sender ) ? 1 : start + 1; i <= maxclients; i++ )
		if( i != sender && present[i] ) return i;
	return 0;
}
#endif
```

- [ ] **Step 5: Hook the engine's entity search**

In `$ENGINE/engine/server/sv_game.c`, add after `#include "sv_cof_party.h"` (line 28):
```c
#include "sv_cof_sender.h"
```

Insert this block directly above the `/*` that opens the `SV_FindEntityByString` banner (line 1557):
```c
// Cry of Fear story co-op: the player whose client command the game DLL is
// running right now (NULL outside one). Inside it, a search for "the first
// player" answers with that sender, then everyone else (sv_cof_sender.h).
static edict_t *cof_command_sender;

void SV_CoF_CommandSender( edict_t *sender )
{
	cof_command_sender = sender;
}

// true: *e is the answer. false: search on from *e as usual.
static qboolean CoF_SenderFind( const char *pszField, const char *pszValue, int *e )
{
	byte present[MAX_CLIENTS + 1];
	int i, sender;

	if( !cof_command_sender || *e > svs.maxclients || svs.maxclients > MAX_CLIENTS ||
		Q_strcmp( pszField, "classname" ) || Q_strcmp( pszValue, "player" ))
		return false;
	sender = NUM_FOR_EDICT( cof_command_sender );
	if( sender < 1 || sender > svs.maxclients ) return false;
	memset( present, 0, sizeof( present ));
	for( i = 1; i <= svs.maxclients; i++ )
	{
		edict_t *ed = SV_EdictNum( i );
		present[i] = (byte)( SV_IsValidEdict( ed ) && SV_ClientFromEdict( ed, true ) &&
			!Q_strcmp( SV_GetString( ed->v.classname ), "player" ));
	}
	if( !present[sender] ) return false;
	i = CoF_SenderNextPlayer( *e, sender, svs.maxclients, present );
	if( !i ) *e = svs.maxclients; // past the clients the plain search goes on
	else *e = i;
	return i != 0;
}

```

In `SV_FindEntityByString`, directly after `if( pStartEdict ) e = NUM_FOR_EDICT( pStartEdict );`, add:
```c
	if( CoF_SenderFind( pszField, pszValue, &e ))
		return SV_EdictNum( e );
```

In `$ENGINE/engine/server/server.h`, after the `SV_CoF_SendUserMessage` declaration (line 705), add:
```c
void SV_CoF_CommandSender( edict_t *sender );
```

- [ ] **Step 6: Open the scope around the game's ClientCommand**

In `$ENGINE/engine/server/sv_client.c`, replace:
```c
		// custom client commands; story co-op attributes their inventory changes
		SV_CoF_ShareCommandBegin( cl );
		svgame.dllFuncs.pfnClientCommand( cl->edict );
		SV_CoF_ShareCommandEnd( cl );
```
with:
```c
		// custom client commands; story co-op attributes their inventory changes
		// and makes the sender "the first player" for the game's lookups
		SV_CoF_ShareCommandBegin( cl );
		SV_CoF_CommandSender( cl->state == cs_spawned && SV_CoF_StoryCoopEnabled() ? cl->edict : NULL );
		svgame.dllFuncs.pfnClientCommand( cl->edict );
		SV_CoF_CommandSender( NULL );
		SV_CoF_ShareCommandEnd( cl );
```

In `$ENGINE/engine/server/sv_cof_story.c`, make `SV_CoF_StoryShutdown` start with the scope reset:
```c
void SV_CoF_StoryShutdown( void )
{
	SV_CoF_CommandSender( NULL ); // an engine error inside a command leaves it set
	SV_CoF_PartyShutdown();
	SV_CoF_ShareReset();
	CoF_StoryCloseGate();
	SV_CoF_ClearValidation();
}
```

- [ ] **Step 7: Run the tests to see them pass**

Run: `& python -m unittest tests.test_enhanced_sender -v`
Expected: 2 tests, `OK`.

- [ ] **Step 8: Build the engine**

Run: `& cmd.exe /d /c tools\build_enhanced.cmd`
Expected: the build finishes with `'build' finished successfully` and no new warnings in `sv_game.c` or `sv_client.c`.

- [ ] **Step 9: Commit both repositories**

```bash
git -C upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a add engine/server/sv_cof_sender.h engine/server/sv_game.c engine/server/sv_client.c engine/server/server.h engine/server/sv_cof_story.c
git -C upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a commit -m "Make the command's sender the first player for the game's lookups"
git add tests/engine/cof_sender_test.c tests/test_enhanced_sender.py
git commit -m "Test the first-player lookup during a client command"
```

---

### Task 2: Retail layer: documents and code notes, with probes

Stage 4 will archive a note for the partner by calling the game's own `CBasePlayer::AddDocument` (`0x100DA6D0`) and will give the partner a code note with an `AddNote` message. Static analysis could not settle whether either works when the engine does it. This task adds both calls and the probe commands that Task 3 runs live.

Two details here are assumptions the live check tests: that `cof_addcodenote` (`0x100A6790`) writes its `nerds` placeholder into the slot's `note1` field, and that the message carries `nerds` as its first string. If Task 3's code note does not show, disassemble `0x100A6790` before writing the stage 4 plan.

**Files:**
- Modify: `$ENGINE/engine/server/sv_cof_story.c`, `$ENGINE/engine/server/sv_cof_story.h`
- Modify: `$ENGINE/engine/server/sv_cof_retail.c`, `$ENGINE/engine/server/sv_cof_retail.h`
- Modify: `tests/engine/cof_retail_harness.c`

**Interfaces:**
- Consumes: `SV_CoF_SendUserMessage`, `SV_CoF_StoryEntityPrivate`, `SV_CoF_RetailScreenFade` (all existing).
- Produces:
  - `qboolean SV_CoF_StoryCallMember( unsigned int rva, void *self, void *arg )`.
  - `edict_t *SV_CoF_RetailOpenDocument( sv_client_t *cl )`.
  - `qboolean SV_CoF_RetailAddDocument( sv_client_t *cl, edict_t *doc )`.
  - `qboolean SV_CoF_RetailCodeNote( sv_client_t *cl, int code, int style )`.
  - `int SV_CoF_RetailDocumentCount( sv_client_t *cl )`.
  - Console: `cof_story_probe doc <userid> [n]`, `cof_story_probe codenote <userid> <code> [style]`, `cof_story_probe fade <userid> [hold seconds]`.

- [ ] **Step 1: Extend the harness doubles**

In `tests/engine/cof_retail_harness.c`:

Add after `#define cs_spawned 4`:
```c
#define FL_KILLME ( 1 << 30 )
#define Q_atof( s ) ((float)atof( s ))
```

Replace the two type lines
```c
typedef struct { float health; int deadflag; } entvars_t;
typedef struct edict_s { int free; void *pvPrivateData; entvars_t v; } edict_t;
```
with:
```c
typedef struct { float health; int deadflag, flags; string_t classname; } entvars_t;
typedef struct edict_s { int free, serialnumber; void *pvPrivateData; entvars_t v; } edict_t;
```

Replace
```c
static struct { struct { void ( *pfnClientCommand )( edict_t *ent ); } dllFuncs; } svgame;
static edict_t edicts[3];
static sv_client_t clients[2];
static byte private_data[2][0x2300];
static const char *strings[] = { "", "inventoryitems/hallkey.txt", "inventoryitems/weapons/weapon_mobile.txt", "inventoryitems/fuse.txt" };
```
with:
```c
static struct { struct { void ( *pfnClientCommand )( edict_t *ent ); } dllFuncs; int numEntities; edict_t *edicts; } svgame;
static edict_t edicts[6];
static sv_client_t clients[2];
static byte private_data[2][0x2820], doc_private[0x210];
static const char *strings[] = { "", "inventoryitems/hallkey.txt", "inventoryitems/weapons/weapon_mobile.txt", "inventoryitems/fuse.txt", "cof_document" };
static unsigned int call_rva;
static void *call_self, *call_arg;
static int calls;
```

Replace
```c
static byte *SV_CoF_StoryEntityPrivate( edict_t *e, size_t need ) { return e && e->pvPrivateData && need <= 0x2300 ? (byte *)e->pvPrivateData : NULL; }
static const char *SV_GetString( string_t s ) { assert( s >= 0 && s < 4 ); return strings[s]; }
```
with:
```c
static byte *SV_CoF_StoryEntityPrivate( edict_t *e, size_t need )
{
	size_t have = e && e->pvPrivateData == doc_private ? sizeof( doc_private ) : sizeof( private_data[0] );
	return e && e->pvPrivateData && need <= have ? (byte *)e->pvPrivateData : NULL;
}
static const char *SV_GetString( string_t s ) { assert( s >= 0 && s < 5 ); return strings[s]; }
static edict_t *SV_EdictNum( int i ) { return &edicts[i]; }
static qboolean SV_CoF_StoryCallMember( unsigned int rva, void *self, void *arg )
{
	calls++; call_rva = rva; call_self = self; call_arg = arg;
	return true;
}
```

In `main`, after `svgame.dllFuncs.pfnClientCommand = ClientCommand;`, add:
```c
	svgame.edicts = edicts;
	svgame.numEntities = 6;
```

- [ ] **Step 2: Write the failing scenarios**

In the same file, insert directly before the comment `/* probe: give and remove need sv_cheats; slots and messages do not */`:
```c
	/* the document a player is reading: an EHANDLE (edict, serial) in game memory */
	{
		edict_t *doc = &edicts[3], *bogus = (edict_t *)( private_data[1] + 8 );
		int serial = 7;
		doc->v.classname = 4; doc->serialnumber = 7; doc->pvPrivateData = doc_private;
		assert( !SV_CoF_RetailOpenDocument( &clients[0] )); /* reading nothing */
		memcpy( private_data[0] + 0x630, &doc, sizeof( doc ));
		memcpy( private_data[0] + 0x630 + sizeof( doc ), &serial, sizeof( serial ));
		assert( SV_CoF_RetailOpenDocument( &clients[0] ) == doc );
		serial = 8; memcpy( private_data[0] + 0x630 + sizeof( doc ), &serial, sizeof( serial ));
		assert( !SV_CoF_RetailOpenDocument( &clients[0] )); /* a stale handle */
		serial = 7; memcpy( private_data[0] + 0x630 + sizeof( doc ), &serial, sizeof( serial ));
		memcpy( private_data[0] + 0x630, &bogus, sizeof( bogus ));
		assert( !SV_CoF_RetailOpenDocument( &clients[0] )); /* never follow a pointer outside the edict array */
		memcpy( private_data[0] + 0x630, &doc, sizeof( doc ));
		doc->v.classname = 0;
		assert( !SV_CoF_RetailOpenDocument( &clients[0] )); /* not a cof_document */
		doc->v.classname = 4;

		/* AddDocument: the game's own member, with the other player as this */
		assert( SV_CoF_RetailAddDocument( &clients[1], doc ));
		assert( calls == 1 && call_rva == 0xDA6D0 && call_self == private_data[1] && call_arg == doc_private );
		assert( !SV_CoF_RetailAddDocument( &clients[1], &edicts[2] ) && calls == 1 ); /* a player is not a document */
		assert( !SV_CoF_RetailAddDocument( &clients[1], NULL ) && calls == 1 );
		doc->v.flags = FL_KILLME;
		assert( !SV_CoF_RetailAddDocument( &clients[1], doc ) && calls == 1 ); /* being removed */
		doc->v.flags = 0;
	}

	/* a code note: the "nerds" placeholder in the first free slot, then AddNote with the code */
	{
		static const byte note[21] = { 2, 1, 0xD2, 0x04, 3, 'n', 'e', 'r', 'd', 's', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		assert( SV_CoF_RetailDocumentCount( &clients[1] ) == 0 );
		private_data[1][0x688 + 0x120 * 1 + 0x40] = 'x'; /* slot 1 is used */
		assert( SV_CoF_RetailDocumentCount( &clients[1] ) == 1 );
		assert( SV_CoF_RetailCodeNote( &clients[1], 1234, 3 ));
		assert( !strcmp( last_msg_name, "AddNote" ) && last_msg_size == 21 && !memcmp( last_msg, note, 21 ) && last_msg_reliable );
		assert( !strcmp( (char *)private_data[1] + 0x688 + 0x120 * 2 + 0x40, "nerds" ));
		assert( SV_CoF_RetailDocumentCount( &clients[1] ) == 2 );
		for( i = 1; i <= 16; i++ ) private_data[1][0x688 + 0x120 * i + 0x40] = 'x';
		assert( !SV_CoF_RetailCodeNote( &clients[1], 1234, 3 )); /* all sixteen slots are used */
		assert( !SV_CoF_RetailCodeNote( &clients[1], 70000, 3 )); /* the code travels as a short */
		memset( private_data[1] + 0x688, 0, 0x120 * 17 );
		clients[1].state = 2;
		assert( SV_CoF_RetailDocumentCount( &clients[1] ) == -1 && !SV_CoF_RetailCodeNote( &clients[1], 1, 0 ));
		clients[1].state = cs_spawned;
	}

```

Then insert directly before the line `argv[2] = "99"; probe();`:
```c
	/* probe: doc and codenote write game state, so they need sv_cheats too */
	argc = 3; argv[1] = "doc"; argv[2] = "2"; cheats = 0; calls = 0; probe();
	assert( calls == 0 && strstr( console, "what=doc result=0 reason=needs-sv_cheats" ));
	cheats = 1; probe();
	assert( calls == 1 && call_arg == doc_private && strstr( console, "what=doc userid=2 result=1 documents=0->0" ));
	argc = 4; argv[3] = "2"; probe(); /* the map has no second document */
	assert( calls == 1 && strstr( console, "what=doc userid=2 result=0 documents=0->0" ));
	argc = 5; argv[1] = "codenote"; argv[3] = "4321"; argv[4] = "1"; probe();
	assert( strstr( console, "what=codenote userid=2 result=1 documents=0->1" ));
	argc = 4; argv[1] = "fade"; argv[2] = "1"; argv[3] = "1.5"; probe();
	assert( last_msg[2] == 0x00 && last_msg[3] == 0x18 ); /* hold 1.5 s = 0x1800 */
	argc = 5; argv[1] = "msg"; argv[2] = "1"; argv[3] = "4"; argv[4] = "hello";
```

- [ ] **Step 3: Run it to see it fail**

Run: `& python -m unittest tests.test_enhanced_retail -v`
Expected: FAIL; the compiler output names `SV_CoF_RetailOpenDocument` as undefined.

- [ ] **Step 4: Add the member call to the story adapter**

In `$ENGINE/engine/server/sv_cof_story.h`, before `#endif`, add:
```c
// Call a thiscall member of the verified retail DLL that takes one pointer.
qboolean SV_CoF_StoryCallMember( unsigned int rva, void *self, void *arg );
```

In `$ENGINE/engine/server/sv_cof_story.c`, add directly above `static void CoF_StoryStatus_f( void )`:
```c
// A retail thiscall member taking one pointer (ret 4), at a known RVA of the
// hash-verified game DLL. __fastcall puts self in ecx and a dummy in edx; the
// argument goes on the stack and the callee pops it, exactly as thiscall does.
qboolean SV_CoF_StoryCallMember( unsigned int rva, void *self, void *arg )
{
#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
	typedef void ( __fastcall *cof_member_t )( void *self, void *edx, void *arg );
	dll_user_t *lib = (dll_user_t *)svgame.hInstance;
	if( !self || !SV_CoF_StoryCoopEnabled() || !lib || !lib->hInstance || lib->custom_loader || lib->encrypted ) return false;
	((cof_member_t)((byte *)lib->hInstance + rva ))( self, NULL, arg );
	return true;
#else
	return false;
#endif
}

```

- [ ] **Step 5: Add documents and code notes to the retail layer**

In `$ENGINE/engine/server/sv_cof_retail.h`, before `#endif`, add:
```c
// The cof_document this player is reading right now (m_hDocument), or NULL.
edict_t *SV_CoF_RetailOpenDocument( sv_client_t *cl );
// Archive a document for a player through the game's own AddDocument: it
// dedupes, saves, tells the player and sends AddNote.
qboolean SV_CoF_RetailAddDocument( sv_client_t *cl, edict_t *doc );
// Give a player a code note as cof_addcodenote does: placeholder slot, then AddNote.
qboolean SV_CoF_RetailCodeNote( sv_client_t *cl, int code, int style );
// Used document slots (of 16), or -1 without a verified, spawned player.
int SV_CoF_RetailDocumentCount( sv_client_t *cl );
```

In `$ENGINE/engine/server/sv_cof_retail.c`, add after the `COF_RETAIL_CACHE` define:
```c
// Documents (docs/research/2026-09-30-hl-documents-story-state-re.md, project repo)
#define COF_RETAIL_PLAYER_SIZE 0x281C // the whole CBasePlayer
#define COF_RETAIL_DOCUMENT    0x0630 // EHANDLE m_hDocument: the cof_document being read
#define COF_RETAIL_DOC_SLOTS   0x0688 // documentsstruct: entry i (1..16) at +0x688 + 0x120 * i
#define COF_RETAIL_DOC_SIZE    0x120
#define COF_RETAIL_DOC_COUNT   16
#define COF_RETAIL_DOC_NOTE1   0x40 // entry + 0x40: note1; non-empty marks a used slot
#define COF_RETAIL_DOC_ENTITY  0x210 // sizeof( CCoFDocument )
#define COF_RETAIL_ADDDOCUMENT 0xDA6D0 // CBasePlayer::AddDocument( CCoFDocument * ), thiscall, ret 4
```

Add after `CoF_RetailPlayer`:
```c
// The same player, checked for the whole object: document and story fields sit past the inventory.
static byte *CoF_RetailStoryPlayer( sv_client_t *cl )
{
	if( !cl || cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) return NULL;
	return SV_CoF_StoryEntityPrivate( cl->edict, COF_RETAIL_PLAYER_SIZE );
}

// A pointer read out of game memory, checked against the edict array before any use.
static edict_t *CoF_RetailEdict( edict_t *ent )
{
	size_t offset;
	if( !ent || !svgame.edicts || ent < svgame.edicts || ent >= svgame.edicts + svgame.numEntities ) return NULL;
	offset = (size_t)((byte *)ent - (byte *)svgame.edicts );
	return offset % sizeof( edict_t ) ? NULL : ent;
}

static qboolean CoF_RetailIsDocument( edict_t *ent )
{
	return SV_IsValidEdict( ent ) && !FBitSet( ent->v.flags, FL_KILLME ) &&
		!Q_strcmp( SV_GetString( ent->v.classname ), "cof_document" ) &&
		SV_CoF_StoryEntityPrivate( ent, COF_RETAIL_DOC_ENTITY ) != NULL;
}

// The map's n-th cof_document (from 1), for the probe.
static edict_t *CoF_RetailNthDocument( int n )
{
	int i;
	for( i = svs.maxclients + 1; n > 0 && i < svgame.numEntities; i++ )
		if( CoF_RetailIsDocument( SV_EdictNum( i )) && !--n ) return SV_EdictNum( i );
	return NULL;
}
```

Add after `SV_CoF_RetailScreenFade`:
```c
edict_t *SV_CoF_RetailOpenDocument( sv_client_t *cl )
{
	byte *pv = CoF_RetailStoryPlayer( cl );
	edict_t *ent;
	int serial;
	if( !pv ) return NULL;
	memcpy( &ent, pv + COF_RETAIL_DOCUMENT, sizeof( ent ));
	memcpy( &serial, pv + COF_RETAIL_DOCUMENT + sizeof( ent ), sizeof( serial ));
	ent = CoF_RetailEdict( ent );
	if( !ent || !CoF_RetailIsDocument( ent ) || ent->serialnumber != serial ) return NULL;
	return ent;
}

qboolean SV_CoF_RetailAddDocument( sv_client_t *cl, edict_t *doc )
{
	byte *pv = CoF_RetailStoryPlayer( cl );
	if( !pv || !doc || !CoF_RetailIsDocument( doc )) return false;
	return SV_CoF_StoryCallMember( COF_RETAIL_ADDDOCUMENT, pv, SV_CoF_StoryEntityPrivate( doc, COF_RETAIL_DOC_ENTITY ));
}

int SV_CoF_RetailDocumentCount( sv_client_t *cl )
{
	byte *pv = CoF_RetailStoryPlayer( cl );
	int i, count = 0;
	if( !pv ) return -1;
	for( i = 1; i <= COF_RETAIL_DOC_COUNT; i++ )
		if( pv[COF_RETAIL_DOC_SLOTS + COF_RETAIL_DOC_SIZE * i + COF_RETAIL_DOC_NOTE1] ) count++;
	return count;
}

// AddNote for a code note: byte slot, byte isCode = 1, short code, byte style,
// then eleven strings (note1-4, page1-4, turnsound, title, label).
// cof_addcodenote (100A6790) marks the slot with the "nerds" placeholder; the
// code itself travels only in the message.
qboolean SV_CoF_RetailCodeNote( sv_client_t *cl, int code, int style )
{
	byte payload[32], *pv = CoF_RetailStoryPlayer( cl ), *entry = NULL;
	int i, n = 0;
	if( !pv || code < 0 || code > 0xFFFF || style < 0 || style > 0xFF ) return false;
	for( i = 1; i <= COF_RETAIL_DOC_COUNT; i++ )
	{
		entry = pv + COF_RETAIL_DOC_SLOTS + COF_RETAIL_DOC_SIZE * i;
		if( !entry[COF_RETAIL_DOC_NOTE1] ) break;
	}
	if( i > COF_RETAIL_DOC_COUNT ) return false; // every document slot is used
	memcpy( entry + COF_RETAIL_DOC_NOTE1, "nerds", 6 );
	payload[n++] = (byte)i;
	payload[n++] = 1;
	payload[n++] = (byte)( code & 0xFF );
	payload[n++] = (byte)( code >> 8 );
	payload[n++] = (byte)style;
	memcpy( payload + n, "nerds", 6 ); n += 6;
	memset( payload + n, 0, 10 ); n += 10; // the other ten strings are empty
	return SV_CoF_SendUserMessage( cl->edict, "AddNote", payload, n, true );
}
```

- [ ] **Step 6: Add the probe commands**

In `CoF_RetailProbe_f`, replace the comment above the function and the usage line so they read:
```c
// Developer probe for the live checks, from the server console or RCON:
// cof_story_probe <slots|give|remove|msg|fade|doc|codenote> <userid> [args]
```
```c
		Con_Printf( S_USAGE "cof_story_probe <slots|give|remove|msg|fade|doc|codenote> <userid> [descriptor | colour text | hold | n | code style]\n" );
```

Replace the sv_cheats test
```c
	if(( !Q_strcmp( what, "give" ) || !Q_strcmp( what, "remove" )) && !Cvar_VariableInteger( "sv_cheats" ))
```
with:
```c
	if(( !Q_strcmp( what, "give" ) || !Q_strcmp( what, "remove" ) || !Q_strcmp( what, "doc" ) || !Q_strcmp( what, "codenote" )) &&
		!Cvar_VariableInteger( "sv_cheats" ))
```

Directly after that `if` block (before `if( !Q_strcmp( what, "give" ))`), add:
```c
	if( !Q_strcmp( what, "doc" ) || !Q_strcmp( what, "codenote" ))
	{
		int before = SV_CoF_RetailDocumentCount( cl );
		if( !Q_strcmp( what, "doc" )) result = SV_CoF_RetailAddDocument( cl, CoF_RetailNthDocument( arg[0] ? Q_atoi( arg ) : 1 ));
		else result = SV_CoF_RetailCodeNote( cl, Q_atoi( arg ), Q_atoi( text ));
		Con_Printf( "cofstory phase=probe what=%s userid=%d result=%d documents=%d->%d\n", what, cl->userid, result, before, SV_CoF_RetailDocumentCount( cl ));
		return;
	}
```

Replace the fade line
```c
	else if( !Q_strcmp( what, "fade" )) result = SV_CoF_RetailScreenFade( cl, 0.5f, 0.1f, 0, 0, 0, 0, 255 );
```
with:
```c
	else if( !Q_strcmp( what, "fade" )) result = SV_CoF_RetailScreenFade( cl, 0.5f, arg[0] ? Q_atof( arg ) : 0.1f, 0, 0, 0, 0, 255 );
```

And update the command's help text in `SV_CoF_RetailInit`:
```c
	Cmd_AddCommand( "cof_story_probe", CoF_RetailProbe_f, "story co-op developer probe: slots, give, remove, msg, fade, doc, codenote" );
```

- [ ] **Step 7: Run the tests to see them pass**

Run: `& python -m unittest tests.test_enhanced_retail tests.test_enhanced_share -v`
Expected: `OK` (the share harness has its own doubles and is unaffected).

- [ ] **Step 8: Build and commit**

Run: `& cmd.exe /d /c tools\build_enhanced.cmd`
Expected: `'build' finished successfully`.

```bash
git -C upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a add engine/server/sv_cof_story.c engine/server/sv_cof_story.h engine/server/sv_cof_retail.c engine/server/sv_cof_retail.h
git -C upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a commit -m "Add document and code-note calls to the retail layer, with probes"
git add tests/engine/cof_retail_harness.c
git commit -m "Test the retail layer's document and code-note calls"
```

---

### Task 3: Deploy and check live: the padlock, and the stage 4 facts

**Files:**
- Modify: `state/enhanced-remote-session.json` (engine hash only; untracked, private)
- Modify: `AGENT-HANDOFF-2026-09-27.md` (new dated UPDATE section at the top)
- Create: `evidence/stage3-sender-facts/` (untracked: build, pre-deploy engine backups, probe logs)

**Interfaces:**
- Consumes: Task 1's sender scope and Task 2's `cof_story_probe doc|codenote|fade`.
- Produces: a recorded yes or no for each of these.
  1. A guest who cancels the padlock screen is free again, and the host is unaffected.
  2. `AddDocument` called by the engine archives a note for the partner.
  3. An engine-sent code `AddNote` shows the code note.
  4. `ScreenFade` with a 1.5 s hold is visible.

  Results 2 and 3 are the inputs of the stage 4 plan. Result 4 decides whether the warp's blink can be relied on.

- [ ] **Step 1: Run the whole suite**

Run: `& python -m unittest discover -s tests 2>&1 | Select-Object -Last 3`
Expected: `OK`, 119 tests.

- [ ] **Step 2: Stage and deploy the engine to both copies**

Run in PowerShell from the project root:
```powershell
if (Get-CimInstance Win32_Process -Filter "Name='CoFLaunchApp.exe' OR Name='cof.exe'") { throw 'Game is running; not deploying.' }
$build = 'upstream\cof-enhanced\xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a\build-local-20260927\engine'
$stage = 'evidence\stage3-sender-facts'
New-Item -ItemType Directory -Force "$stage\host-before","$stage\local-client-before" | Out-Null
Copy-Item "$build\xash.dll","$build\xash.pdb" $stage
Copy-Item runtimes\enhanced\xash.dll "$stage\host-before\"
Copy-Item runtimes\enhanced-local-client\xash.dll "$stage\local-client-before\"
Copy-Item "$stage\xash.dll" runtimes\enhanced\xash.dll -Force
Copy-Item "$stage\xash.dll" runtimes\enhanced-local-client\xash.dll -Force
$json = Get-Content state\enhanced-remote-session.json -Raw | ConvertFrom-Json
$json.engine_sha256 = (Get-FileHash "$stage\xash.dll").Hash
$json | ConvertTo-Json | Set-Content state\enhanced-remote-session.json -Encoding utf8NoBOM
Get-FileHash runtimes\enhanced\xash.dll, runtimes\enhanced-local-client\xash.dll | Select-Object Hash
```
Expected: two identical hashes, equal to the new `engine_sha256`.

- [ ] **Step 3: Tell the user, then start the host and the guest**

Say to the user: "Launching the host and the local guest now for the padlock check (`c_start`)." Then run:
```powershell
Start-Process powershell.exe -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','tools\Start-StoryHost.ps1'
```
Wait until the host log shows the gate, then start the guest:
```powershell
Select-String -Path runtimes\enhanced\story-remote-host.log -Pattern 'gate-enabled' | Select-Object -Last 1
Start-Process powershell.exe -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','tools\Start-LocalGuest.ps1'
```
Confirm both are in with:
```powershell
$c = 'python'
& $c tools\enhanced_local_control.py cof_story_status --session remote --out evidence\stage3-sender-facts\01-status.log
```
Expected: two `phase=player` lines. Note the guest's userid; it is called `<guest>` below.

- [ ] **Step 4: The padlock check**

**USER CHECK:** ask the user to do this in the guest window, and to say what happened:
1. Walk to the padlock in `c_start` and press E. The padlock screen opens.
2. Cancel it, the same way the host does.
3. Expected: the guest can move and look around again. The host was not moved, frozen or shown a padlock screen.

Then save the state:
```powershell
& $c tools\enhanced_local_control.py cof_story_status --session remote --out evidence\stage3-sender-facts\02-after-padlock.log
```
Expected: the guest's `phase=player` line shows `frozen=0`.

Ask the host player to open and cancel the padlock too. Expected: unchanged behaviour for the host.

- [ ] **Step 5: Move to a map with documents**

`c_start` has no `cof_document`; `c_apartment1` has four. Say to the user: "Changing the map to `c_apartment1` for the note checks. Both windows will reload." Then run:
```powershell
& $c tools\enhanced_local_control.py "changelevel c_apartment1" --session remote
& $c tools\enhanced_local_control.py cof_story_status --session remote --out evidence\stage3-sender-facts\03-apartment-status.log
```
Expected: `map=c_apartment1` and two `phase=player` lines. This is a plain map change, so both players start it fresh. If the guest does not come along, ask the user to type `retry` in the guest console. Read the userids again.

- [ ] **Step 6: Probe the note, the code note and the fade**

```powershell
& $c tools\enhanced_local_control.py "sv_cheats 1" --session remote
& $c tools\enhanced_local_control.py "cof_story_probe doc <guest>" --session remote --out evidence\stage3-sender-facts\04-doc.log
& $c tools\enhanced_local_control.py "cof_story_probe codenote <guest> 1234 0" --session remote --out evidence\stage3-sender-facts\05-codenote.log
& $c tools\enhanced_local_control.py "cof_story_probe fade <guest> 1.5" --session remote --out evidence\stage3-sender-facts\06-fade.log
& $c tools\enhanced_local_control.py "sv_cheats 0" --session remote
```
Expected:
- `04-doc.log` contains `what=doc userid=<guest> result=1 documents=0->1`. A count that stays `0->0` means the game refused the note.
- `05-codenote.log` contains `what=codenote userid=<guest> result=1 documents=1->2`.
- `06-fade.log` contains `what=fade userid=<guest> result=1`.

If the host process exits during the `doc` probe, that is the answer for fact 2: record it, collect the crash line from the end of `runtimes\enhanced\story-remote-host.log` (redact anything credential-like), and do not retry.

**USER CHECK:** ask the user, for the guest window:
1. Did `Archived "…"` appear, and is the note in the documents list (TAB, then the notes page)?
2. Is there a code note showing 1234?
3. Did the screen go black for about a second and a half, then fade in?

Record each answer.

- [ ] **Step 7: Record the results in the handoff**

Add a dated `## UPDATE` section at the top of `AGENT-HANDOFF-2026-09-27.md` containing:
- the engine hash;
- the four yes/no results, with the evidence file names;
- for any no, what was seen instead;
- a line "Stage 4 plan inputs:" stating whether notes can use the engine's `AddDocument` call and whether code notes can use the engine's `AddNote`.

**If the padlock check failed, stop here** and report to the user with the status log; the sender scope is not working and later live checks would be confusing. The other three results do not block the rest of this plan.

- [ ] **Step 8: Commit the handoff**

```bash
git add AGENT-HANDOFF-2026-09-27.md
git commit -m "Record the padlock fix and the stage 4 live facts"
```

---

### Task 4: The exit hold's rules (pure state)

**Files:**
- Create: `$ENGINE/engine/server/sv_cof_exit_state.h`
- Create: `tests/engine/cof_exit_state_test.c`
- Create: `tests/test_enhanced_exit_state.py`

**Interfaces:**
- Produces (all `static`, in `sv_cof_exit_state.h`):
  - Types `cof_exit_player_t { int userid, spawned, alive, touching; float origin[3]; }` and `cof_exit_state_t { int active, walkin, waiter, generation; char level[64], landmark[64]; float spot[3]; double began, prompt_at; }`.
  - Constants `COF_EXIT_PLAYERS 32`, `COF_EXIT_NAME 64`, `COF_EXIT_ESCAPE 10.0`; enums `COF_EXIT_GO`, `COF_EXIT_HOLD` and `COF_EXIT_IDLE`, `COF_EXIT_WAIT`, `COF_EXIT_READY`, `COF_EXIT_CANCELLED`.
  - `void CoF_ExitClear( cof_exit_state_t *s )`.
  - `const cof_exit_player_t *CoF_ExitFind( const cof_exit_player_t *players, int n, int userid )`.
  - `int CoF_ExitAt( const cof_exit_player_t *p, const cof_exit_player_t *waiter )`.
  - `int CoF_ExitMissing( const cof_exit_player_t *players, int n, int waiter )`: the userid of the first counted player not at the exit, or 0.
  - `int CoF_ExitRequest( cof_exit_state_t *s, const cof_exit_player_t *players, int n, int generation, const char *level, const char *landmark, const float *spot, double time, int *began )`.
  - `int CoF_ExitFrame( cof_exit_state_t *s, const cof_exit_player_t *players, int n, int generation, double time )`. On `COF_EXIT_READY` the state is left intact so the caller can read `level` and `landmark`; the caller clears it.
  - `int CoF_ExitEscape( const cof_exit_state_t *s, int userid, double time, int *left )`: 1 leave now, 0 not the waiter, -1 too early with `*left` seconds to go.
  - `int CoF_ExitPromptDue( cof_exit_state_t *s, double time )`.

- [ ] **Step 1: Write the failing test**

`tests/engine/cof_exit_state_test.c`:
```c
/* The exit hold's rules, driven directly. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sv_cof_exit_state.h"

static cof_exit_player_t p[COF_EXIT_PLAYERS];
static cof_exit_state_t s;
static const float spot[3] = { 0, 0, 0 };

static void one( int i, int userid, float x, int touching )
{
	memset( &p[i], 0, sizeof( p[i] ));
	p[i].userid = userid; p[i].spawned = p[i].alive = 1; p[i].origin[0] = x; p[i].touching = touching;
}
/* host (userid 1) at x0, guest (userid 2) at x1 */
static void two( float x0, int touch0, float x1, int touch1 )
{
	memset( p, 0, sizeof( p )); memset( &s, 0, sizeof( s ));
	one( 0, 1, x0, touch0 ); one( 1, 2, x1, touch1 );
}
static int request( double t, int *began ) { return CoF_ExitRequest( &s, p, 2, 5, "c_start2", "hahaland", spot, t, began ); }
static int frame( double t ) { return CoF_ExitFrame( &s, p, 2, 5, t ); }

int main( void )
{
	int began, left;

	/* alone: an exit goes at once */
	two( 0, 1, 0, 0 ); p[1].userid = 0;
	assert( request( 100, &began ) == COF_EXIT_GO && !s.active && !began );
	/* both in the exit, or the partner within 128 units of the one in it */
	two( 0, 1, 10, 1 ); assert( request( 100, &began ) == COF_EXIT_GO );
	two( 0, 1, 128, 0 ); assert( request( 100, &began ) == COF_EXIT_GO );

	/* the partner elsewhere: held, with the player in the exit as the waiter */
	two( 0, 1, 129, 0 );
	assert( request( 100, &began ) == COF_EXIT_HOLD && began && s.active && s.walkin && s.waiter == 1 );
	assert( CoF_ExitMissing( p, 2, 1 ) == 2 );
	/* asked again every frame: the same hold, the same clock */
	assert( request( 100.1, &began ) == COF_EXIT_HOLD && !began && s.began == 100 );
	assert( frame( 100.2 ) == COF_EXIT_WAIT );
	/* Review Focus 1: no further request; the partner warps over and the frame lets it go */
	p[1].origin[0] = 35;
	assert( frame( 101 ) == COF_EXIT_READY && !strcmp( s.level, "c_start2" ) && !strcmp( s.landmark, "hahaland" ));

	/* stepping out of the exit ends the hold */
	two( 0, 1, 3000, 0 ); request( 100, &began ); p[0].touching = 0;
	assert( frame( 100.1 ) == COF_EXIT_CANCELLED && !s.active );

	/* a scripted exit (nobody in a trigger): the player nearest its landmark waits,
	   and walking about does not cancel it */
	two( 40, 0, 3000, 0 );
	assert( request( 100, &began ) == COF_EXIT_HOLD && !s.walkin && s.waiter == 1 );
	p[0].origin[0] = 300; assert( frame( 100.1 ) == COF_EXIT_WAIT && s.active );
	p[1].origin[0] = 335; assert( frame( 100.2 ) == COF_EXIT_READY );
	two( 3000, 0, 40, 0 ); request( 100, &began ); assert( s.waiter == 2 );

	/* a dead partner holds the exit, even lying in it */
	two( 0, 1, 10, 1 ); p[1].alive = 0;
	assert( request( 100, &began ) == COF_EXIT_HOLD && frame( 100.1 ) == COF_EXIT_WAIT );
	p[1].alive = 1; assert( frame( 100.2 ) == COF_EXIT_READY );
	/* a partner who is still connecting holds it until spawned or dropped */
	two( 0, 1, 0, 0 ); p[1].spawned = 0;
	assert( request( 100, &began ) == COF_EXIT_HOLD && frame( 100.1 ) == COF_EXIT_WAIT );
	/* Review Focus 2: the partner disconnects; the waiter in the exit goes */
	p[1].userid = 0; assert( frame( 100.2 ) == COF_EXIT_READY );
	/* ...but not a waiter who has stepped out meanwhile */
	two( 0, 1, 3000, 0 ); request( 100, &began ); p[1].userid = 0; p[0].touching = 0;
	assert( frame( 100.1 ) == COF_EXIT_CANCELLED );
	/* Review Focus 2: a scripted exit whose waiter leaves still happens for the one left */
	two( 40, 0, 3000, 0 ); request( 100, &began ); p[0].userid = 0;
	assert( frame( 100.1 ) == COF_EXIT_READY );
	/* with three players someone else becomes the waiter */
	two( 40, 0, 3000, 0 ); one( 2, 3, 2000, 0 );
	assert( CoF_ExitRequest( &s, p, 3, 5, "c_start2", "hahaland", spot, 100, &began ) == COF_EXIT_HOLD && s.waiter == 1 );
	p[0].userid = 0;
	assert( CoF_ExitFrame( &s, p, 3, 5, 100.1 ) == COF_EXIT_WAIT && s.waiter == 3 );
	/* everyone dead: nobody can wait, the exit is not held */
	two( 0, 1, 10, 1 ); p[0].alive = p[1].alive = 0;
	assert( request( 100, &began ) == COF_EXIT_GO );

	/* the escape hatch: only the waiter, only after 10 s */
	two( 0, 1, 3000, 0 ); request( 100, &began );
	assert( CoF_ExitEscape( &s, 2, 150, &left ) == 0 );
	assert( CoF_ExitEscape( &s, 1, 103.2, &left ) == -1 && left == 7 );
	/* Review Focus 3: pressing early changes nothing */
	assert( s.began == 100 && CoF_ExitEscape( &s, 1, 105, &left ) == -1 && left == 5 );
	assert( CoF_ExitEscape( &s, 1, 110, &left ) == 1 );
	CoF_ExitClear( &s ); assert( CoF_ExitEscape( &s, 1, 200, &left ) == 0 );

	/* a newer request for another exit replaces the held one */
	two( 0, 1, 3000, 0 ); request( 100, &began );
	assert( CoF_ExitRequest( &s, p, 2, 5, "c_apartment1", "land1", spot, 104, &began ) == COF_EXIT_HOLD && began );
	assert( !strcmp( s.level, "c_apartment1" ) && s.began == 104 );
	/* the map changed another way: the hold is dropped */
	assert( CoF_ExitFrame( &s, p, 2, 6, 105 ) == COF_EXIT_IDLE && !s.active );

	/* prompts: at once, then every 2 s */
	two( 0, 1, 3000, 0 ); request( 100, &began );
	assert( CoF_ExitPromptDue( &s, 100 ) && !CoF_ExitPromptDue( &s, 101 ) && CoF_ExitPromptDue( &s, 102 ));
	CoF_ExitClear( &s ); assert( !CoF_ExitPromptDue( &s, 200 ));

	/* a name that cannot be stored is never held */
	two( 0, 1, 3000, 0 );
	assert( CoF_ExitRequest( &s, p, 2, 5, "0123456789012345678901234567890123456789012345678901234567890123", "x", spot, 100, &began ) == COF_EXIT_GO && !s.active );
	puts( "exit state scenarios passed" );
	return 0;
}
```

`tests/test_enhanced_exit_state.py`:
```python
"""The exit hold's rules, run natively offline."""
import unittest

from tests.native import ENGINE_SERVER, run_native


class ExitStateTests(unittest.TestCase):
    def test_exit_rules(self):
        out = run_native(self, 'cof_exit_state_test.c', {
            'sv_cof_exit_state.h': (ENGINE_SERVER / 'sv_cof_exit_state.h').read_text(),
        })
        self.assertIn('exit state scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run it to see it fail**

Run: `& python -m unittest tests.test_enhanced_exit_state -v`
Expected: ERROR with `FileNotFoundError` for `sv_cof_exit_state.h`.

- [ ] **Step 3: Write the rules**

`$ENGINE/engine/server/sv_cof_exit_state.h`:
```c
/* Exit hold for Cry of Fear story co-op: who must be at a level exit before it
 * goes. Pure state, no engine types, so the offline harness drives it
 * directly. GPL-3.0-or-later.
 * Rules: docs/superpowers/specs/2026-09-30-coop-split-play-design.md, part 1
 * (project repo). A held exit is stored once and released by the frame; it
 * never depends on the game asking again, because some exits are fired by a
 * script or a once-only trigger. */
#ifndef SV_COF_EXIT_STATE_H
#define SV_COF_EXIT_STATE_H
#include <string.h>

#define COF_EXIT_PLAYERS 32
#define COF_EXIT_NAME 64
#define COF_EXIT_NEAR 128.0f // "at the exit" also means this close to the waiting player
#define COF_EXIT_ESCAPE 10.0 // the waiter may leave alone after this long
#define COF_EXIT_PROMPT 2.0

enum { COF_EXIT_GO, COF_EXIT_HOLD };
enum { COF_EXIT_IDLE, COF_EXIT_WAIT, COF_EXIT_READY, COF_EXIT_CANCELLED };

// One client slot. userid 0: nobody who counts (a free slot or a fake client).
typedef struct { int userid, spawned, alive, touching; float origin[3]; } cof_exit_player_t;

typedef struct
{
	int active; // an exit is being held
	int walkin; // the waiter stood in the exit: stepping out cancels
	int waiter; // userid; 0 while nobody is alive to wait
	int generation; // the map this hold belongs to
	char level[COF_EXIT_NAME], landmark[COF_EXIT_NAME];
	float spot[3]; // the exit's landmark: a scripted exit's waiter is the nearest player
	double began, prompt_at;
} cof_exit_state_t;

static void CoF_ExitClear( cof_exit_state_t *s )
{
	memset( s, 0, sizeof( *s ));
}

static void CoF_ExitCopy( char *dst, const char *src )
{
	size_t i;
	for( i = 0; i + 1 < COF_EXIT_NAME && src[i]; i++ ) dst[i] = src[i];
	dst[i] = 0;
}

static const cof_exit_player_t *CoF_ExitFind( const cof_exit_player_t *players, int n, int userid )
{
	int i;
	for( i = 0; userid && i < n; i++ )
		if( players[i].userid == userid ) return &players[i];
	return NULL;
}

static int CoF_ExitCounted( const cof_exit_player_t *players, int n )
{
	int i, count = 0;
	for( i = 0; i < n; i++ )
		if( players[i].userid ) count++;
	return count;
}

static float CoF_ExitDistanceSquared( const float *a, const float *b )
{
	float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
	return x * x + y * y + z * z;
}

static int CoF_ExitCanWait( const cof_exit_player_t *p )
{
	return p && p->userid && p->spawned && p->alive;
}

// Standing where the exit can take them: in it, or beside the waiting player.
// A dead player, or one still joining, holds the exit.
static int CoF_ExitAt( const cof_exit_player_t *p, const cof_exit_player_t *waiter )
{
	if( !CoF_ExitCanWait( p ) || !CoF_ExitCanWait( waiter )) return 0;
	return p == waiter || p->touching || CoF_ExitDistanceSquared( p->origin, waiter->origin ) <= COF_EXIT_NEAR * COF_EXIT_NEAR;
}

// The userid of the first counted player who is not at the exit; 0 when everyone is.
static int CoF_ExitMissing( const cof_exit_player_t *players, int n, int waiter )
{
	const cof_exit_player_t *w = CoF_ExitFind( players, n, waiter );
	int i;
	for( i = 0; i < n; i++ )
		if( players[i].userid && !CoF_ExitAt( &players[i], w )) return players[i].userid;
	return 0;
}

// Who waits: a player standing in the exit, else (a scripted exit) the living
// player nearest its landmark. 0 when nobody can.
static int CoF_ExitPickWaiter( const cof_exit_player_t *players, int n, const float *spot, int *walkin )
{
	int i, best = -1;
	float d, nearest = 0.0f;
	*walkin = 0;
	for( i = 0; i < n; i++ )
	{
		if( !CoF_ExitCanWait( &players[i] )) continue;
		if( players[i].touching )
		{
			*walkin = 1;
			return players[i].userid;
		}
		d = CoF_ExitDistanceSquared( players[i].origin, spot );
		if( best < 0 || d < nearest ) { best = i; nearest = d; }
	}
	return best < 0 ? 0 : players[best].userid;
}

// The game asked to change level. GO: change now. HOLD: the exit is held and
// CoF_ExitFrame lets it go. *began is set when this request started a new hold.
static int CoF_ExitRequest( cof_exit_state_t *s, const cof_exit_player_t *players, int n, int generation,
	const char *level, const char *landmark, const float *spot, double time, int *began )
{
	int same = s->active && s->generation == generation && !strcmp( s->level, level ) && !strcmp( s->landmark, landmark );
	int waiter, walkin;
	*began = 0;
	if( strlen( level ) >= COF_EXIT_NAME || strlen( landmark ) >= COF_EXIT_NAME )
	{
		CoF_ExitClear( s );
		return COF_EXIT_GO;
	}
	if( same && CoF_ExitFind( players, n, s->waiter ))
	{
		waiter = s->waiter;
		walkin = s->walkin;
	}
	else waiter = CoF_ExitPickWaiter( players, n, spot, &walkin );
	if( CoF_ExitCounted( players, n ) <= 1 || !waiter || !CoF_ExitMissing( players, n, waiter ))
	{
		CoF_ExitClear( s );
		return COF_EXIT_GO;
	}
	if( !same )
	{
		// a newer request replaces whatever was held
		CoF_ExitClear( s );
		s->active = 1;
		s->generation = generation;
		s->began = s->prompt_at = time;
		CoF_ExitCopy( s->level, level );
		CoF_ExitCopy( s->landmark, landmark );
		memcpy( s->spot, spot, sizeof( s->spot ));
		*began = 1;
	}
	s->waiter = waiter;
	s->walkin = walkin;
	return COF_EXIT_HOLD;
}

// Once per server frame. READY leaves the state for the caller to read the
// level and landmark; the caller clears it.
static int CoF_ExitFrame( cof_exit_state_t *s, const cof_exit_player_t *players, int n, int generation, double time )
{
	const cof_exit_player_t *w;
	(void)time;
	if( !s->active ) return COF_EXIT_IDLE;
	if( s->generation != generation )
	{
		CoF_ExitClear( s ); // the map changed another way
		return COF_EXIT_IDLE;
	}
	w = CoF_ExitFind( players, n, s->waiter );
	if( s->walkin && ( !CoF_ExitCanWait( w ) || !w->touching ))
	{
		CoF_ExitClear( s ); // stepped out, died or left: the trigger asks again if someone returns
		return COF_EXIT_CANCELLED;
	}
	if( !CoF_ExitCanWait( w ))
	{
		// A scripted exit already ran its script: it still has to happen, so
		// someone else waits for it.
		int walkin;
		s->waiter = CoF_ExitPickWaiter( players, n, s->spot, &walkin );
		if( !s->waiter ) return COF_EXIT_WAIT; // nobody alive: the game's own restart decides
	}
	if( CoF_ExitCounted( players, n ) <= 1 || !CoF_ExitMissing( players, n, s->waiter )) return COF_EXIT_READY;
	return COF_EXIT_WAIT;
}

// The waiter pressed the warp key. 1: leave now. 0: not the waiter.
// -1: too early, with *left seconds to go.
static int CoF_ExitEscape( const cof_exit_state_t *s, int userid, double time, int *left )
{
	*left = 0;
	if( !s->active || !userid || userid != s->waiter ) return 0;
	if( time - s->began >= COF_EXIT_ESCAPE ) return 1;
	*left = (int)( COF_EXIT_ESCAPE - ( time - s->began ) + 0.999 );
	return -1;
}

static int CoF_ExitPromptDue( cof_exit_state_t *s, double time )
{
	if( !s->active || !s->waiter || time < s->prompt_at ) return 0;
	s->prompt_at = time + COF_EXIT_PROMPT;
	return 1;
}
#endif
```

- [ ] **Step 4: Run the test to see it pass**

Run: `& python -m unittest tests.test_enhanced_exit_state -v`
Expected: `OK`.

- [ ] **Step 5: Commit both repositories**

```bash
git -C upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a add engine/server/sv_cof_exit_state.h
git -C upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a commit -m "Add the exit hold's rules as pure state"
git add tests/engine/cof_exit_state_test.c tests/test_enhanced_exit_state.py
git commit -m "Test the exit hold's rules"
```

---

### Task 5: The exit hold in the engine

**Files:**
- Create: `$ENGINE/engine/server/sv_cof_exit.c`, `$ENGINE/engine/server/sv_cof_exit.h`
- Modify: `$ENGINE/engine/server/sv_game.c` (includes; `SV_CoF_MapExitModels` at line 1006; `pfnChangeLevel` at line 1448; `pfnAlertMessage` at line 2988; line numbers are from before Task 1's insertions)
- Modify: `$ENGINE/engine/server/sv_save.c:101-143,177`
- Modify: `$ENGINE/engine/server/server.h:704`
- Modify: `$ENGINE/engine/server/sv_cof_party.c:743,785`, `$ENGINE/engine/server/sv_cof_party.h`
- Modify: `$ENGINE/engine/server/sv_cof_story.c`
- Create: `tests/engine/cof_exit_harness.c`, `tests/engine/cof_exit_models_test.c`, `tests/test_enhanced_exit.py`

**Interfaces:**
- Consumes: everything Task 4 produces; `SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable )`; `SV_QueueChangeLevel( const char *level, const char *landname )`.
- Produces:
  - `void SV_CoF_ExitReset( void )`.
  - `qboolean SV_CoF_ExitRequest( const char *level, const char *landmark )`: true means change level now; false means held.
  - `void SV_CoF_ExitFrame( void )`.
  - `qboolean SV_CoF_ExitWarpKey( sv_client_t *cl )`: true when the key press belonged to the exit hold (Task 7 calls it first).
  - `int SV_CoF_ExitWaiter( void )`: the waiting player's userid, or 0 (Task 7 prefers that player as the warp target).
  - `qboolean SV_CoF_ExitQuietAlert( const char *text )`.
  - `qboolean SV_CoF_PartyInExit( const edict_t *player, const int *models, int count )` (was the static `CoF_PartyInExit`).
  - `qboolean SV_CoF_LiveLandmark( const char *name, vec3_t origin )` (was the static `CoF_LiveLandmark`).
  - `int SV_CoF_MapExitModels( const char *map, const char *destination, const char *landmark, int *models, int max, qboolean *fires )`: `fires` (may be NULL) is set when a matching exit carries `target`, `killtarget` or `changetarget`.

- [ ] **Step 1: Write the failing glue harness**

`tests/engine/cof_exit_harness.c`:
```c
/* Offline doubles for sv_cof_exit.c: two players and one walk-in exit. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef int qboolean;
typedef float vec3_t[3];
#define true 1
#define false 0
#define cs_free 0
#define cs_connected 2
#define cs_spawned 4
#define FCL_FAKECLIENT ( 1 << 7 )
#define DEAD_NO 0
#define FBitSet( a, b ) (( a ) & ( b ))
#define Q_min( a, b ) (( a ) < ( b ) ? ( a ) : ( b ))
#define Q_strlen strlen
#define Q_strcmp strcmp
#define Q_strncmp strncmp
#define Q_snprintf snprintf
#define VectorCopy( a, b ) memcpy(( b ), ( a ), sizeof( vec3_t ))
#define COM_StringEmptyOrNULL( s ) ( !( s ) || !*( s ))
static size_t Q_strncpy( char *dst, const char *src, size_t size )
{
	size_t n = strlen( src );
	if( !size ) return n;
	if( n >= size ) { memcpy( dst, src, size - 1 ); dst[size - 1] = 0; }
	else memcpy( dst, src, n + 1 );
	return n;
}
static void COM_StripExtension( char *path )
{
	char *dot = strrchr( path, '.' );
	if( dot ) *dot = 0;
}
typedef struct { struct { float health; int deadflag; vec3_t origin; } v; } edict_t;
typedef struct { int state, userid; unsigned int flags; char name[32]; edict_t *edict; } sv_client_t;
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { double time; const char *name; } sv;
static sv_client_t clients[2];
static edict_t edicts[2];
static char lines[2][192], console[16384], queued_level[64], queued_landmark[64];
static int line_count[2], queued, touching[2], gate, landmark_ok, exit_fires;
static size_t console_length;

static qboolean SV_IsValidEdict( const edict_t *e ) { return e != NULL; }
static qboolean SV_CoF_StoryCoopEnabled( void ) { return gate; }
static qboolean SV_CoF_LiveLandmark( const char *name, vec3_t origin )
{
	origin[0] = origin[1] = origin[2] = 0;
	return landmark_ok;
}
static int SV_CoF_MapExitModels( const char *map, const char *destination, const char *landmark, int *models, int max, qboolean *fires )
{
	assert( !strcmp( map, "c_start" ) && max >= 1 );
	models[0] = 3;
	*fires = exit_fires;
	return 1;
}
static qboolean SV_CoF_PartyInExit( const edict_t *player, const int *models, int count )
{
	assert( count == 1 && models[0] == 3 );
	return touching[player - edicts];
}
static qboolean SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable )
{
	int p = (int)( cl - clients );
	assert( !reliable ); /* refreshed every 2 s: never queue them up */
	snprintf( lines[p], sizeof( lines[p] ), "%d:%s", colour, text );
	line_count[p]++;
	return true;
}
static void SV_QueueChangeLevel( const char *level, const char *landmark )
{
	queued++;
	Q_strncpy( queued_level, level, sizeof( queued_level ));
	Q_strncpy( queued_landmark, landmark, sizeof( queued_landmark ));
}
static void Con_Printf( const char *fmt, ... )
{
	va_list args;
	va_start( args, fmt );
	console_length += (size_t)vsnprintf( console + console_length, sizeof( console ) - console_length, fmt, args );
	va_end( args );
}

#include "sv_cof_exit_state.h"
#include "exit_under_test.c"

/* the host stands in the exit at x=0; the guest is across the map */
static void setup( void )
{
	int i;
	memset( clients, 0, sizeof( clients )); memset( edicts, 0, sizeof( edicts ));
	memset( lines, 0, sizeof( lines )); memset( line_count, 0, sizeof( line_count ));
	console[0] = 0; console_length = 0;
	queued = 0; gate = 1; landmark_ok = 1; exit_fires = 0;
	touching[0] = 1; touching[1] = 0;
	sv.time = 100; sv.name = "c_start";
	svs.maxclients = 2; svs.spawncount = 5; svs.clients = clients;
	for( i = 0; i < 2; i++ )
	{
		clients[i].state = cs_spawned;
		clients[i].userid = i + 1;
		clients[i].edict = &edicts[i];
		snprintf( clients[i].name, sizeof( clients[i].name ), "Little Rascal %d", i + 1 );
		edicts[i].v.health = 100;
	}
	edicts[1].v.origin[0] = 3000;
	SV_CoF_ExitReset();
}
static void tick( double dt ) { sv.time += dt; SV_CoF_ExitFrame(); }
static qboolean ask( void ) { return SV_CoF_ExitRequest( "c_start2", "hahaland" ); }
static int once( const char *phase )
{
	const char *first = strstr( console, phase );
	return first && !strstr( first + 1, phase );
}

int main( void )
{
	/* off, or no landmark (the game's own restarts and co-op doors): never held */
	setup(); gate = 0; assert( ask());
	setup(); assert( SV_CoF_ExitRequest( "c_start2", "" ) && SV_CoF_ExitRequest( "c_start2", NULL ));
	assert( !SV_CoF_ExitWaiter() && !strstr( console, "exit-" ));
	/* an exit the engine would refuse anyway (no live landmark) is not held */
	setup(); landmark_ok = 0; assert( ask());
	/* alone, or with only a fake client: at once */
	setup(); clients[1].state = cs_free; assert( ask());
	setup(); clients[1].flags = FCL_FAKECLIENT; assert( ask());

	/* the partner is elsewhere: held, said once, and both are told what to do */
	setup(); assert( !SV_CoF_ExitRequest( "c_start2.bsp", "hahaland" ));
	assert( strstr( console, "cofstory phase=exit-hold destination=c_start2 landmark=hahaland waiter=1 walkin=1 missing=2" ));
	assert( SV_CoF_ExitWaiter() == 1 );
	assert( SV_CoF_ExitQuietAlert( "CHANGE LEVEL: c_start2 hahaland\n" ) && !SV_CoF_ExitQuietAlert( "Can't find landmark x\n" ));
	SV_CoF_ExitFrame();
	assert( !strcmp( lines[0], "2:Please wait until everyone is ready! (Little Rascal 2 can press J to join you)" ));
	assert( !strcmp( lines[1], "1:Little Rascal 1 is at the exit. Press J to join." ));
	tick( 1 ); assert( line_count[0] == 1 );
	tick( 1.1 ); assert( line_count[0] == 2 && line_count[1] == 2 );
	/* the game asks again every frame it is touched: the same hold */
	assert( !ask() && once( "exit-hold" ) && queued == 0 );
	/* Review Focus 1: no further request; the partner arrives and the frame lets it go, once */
	edicts[1].v.origin[0] = 35; tick( 0.1 );
	assert( queued == 1 && !strcmp( queued_level, "c_start2" ) && !strcmp( queued_landmark, "hahaland" ));
	assert( strstr( console, "cofstory phase=exit-ready destination=c_start2 landmark=hahaland waiter=1" ));
	assert( !SV_CoF_ExitWaiter() && !SV_CoF_ExitQuietAlert( "CHANGE LEVEL: c_start2 hahaland\n" ));
	tick( 0.1 ); assert( queued == 1 );

	/* the game asks again once the partner is there: it goes through the game's own request */
	setup(); assert( !ask()); edicts[1].v.origin[0] = 35;
	assert( ask() && strstr( console, "phase=exit-ready" ) && queued == 0 );

	/* stepping out of the exit ends the hold */
	setup(); assert( !ask()); touching[0] = 0; tick( 0.1 );
	assert( queued == 0 && strstr( console, "cofstory phase=exit-cancelled waiter=1" ) && !SV_CoF_ExitWaiter());
	/* Review Focus 2: the partner disconnects mid-hold */
	setup(); assert( !ask()); clients[1].state = cs_free; tick( 0.1 ); assert( queued == 1 );
	/* a dead partner holds the exit and is not nagged about J */
	setup(); edicts[1].v.health = 0; edicts[1].v.origin[0] = 0; touching[1] = 1;
	assert( !ask()); SV_CoF_ExitFrame();
	assert( line_count[0] == 1 && line_count[1] == 0 );

	/* the escape hatch: the waiter's J, after 10 s */
	setup(); assert( !ask());
	assert( !SV_CoF_ExitWarpKey( &clients[1] )); /* the partner's J is a warp */
	sv.time += 3.2; assert( SV_CoF_ExitWarpKey( &clients[0] ) && queued == 0 );
	assert( !strcmp( lines[0], "2:You can leave without your partner in 7 seconds." ));
	/* Review Focus 3: mashing J neither leaves early nor restarts the wait */
	sv.time += 3; assert( SV_CoF_ExitWarpKey( &clients[0] ) && queued == 0 );
	sv.time += 4; SV_CoF_ExitFrame();
	assert( !strcmp( lines[0], "2:Please wait until everyone is ready! (press J to leave without Little Rascal 2)" ));
	assert( SV_CoF_ExitWarpKey( &clients[0] ) && queued == 1 );
	assert( strstr( console, "cofstory phase=exit-forced destination=c_start2 landmark=hahaland waiter=1" ));
	assert( !SV_CoF_ExitWarpKey( &clients[0] )); /* the hold is over: J is a warp again */

	/* an exit that fires targets is never held, and says so once */
	setup(); exit_fires = 1; assert( ask() && ask());
	assert( once( "cofstory phase=exit-unholdable destination=c_start2 landmark=hahaland" ) && !SV_CoF_ExitWaiter());
	/* the map changed another way: the hold is dropped, nothing is replayed */
	setup(); assert( !ask()); svs.spawncount++; tick( 0.1 );
	assert( queued == 0 && !SV_CoF_ExitWaiter());
	/* a scripted exit (nobody in the trigger): stored once, replayed when the partner comes */
	setup(); touching[0] = 0; edicts[0].v.origin[0] = 40; assert( !ask());
	assert( strstr( console, "waiter=1 walkin=0" ));
	edicts[0].v.origin[0] = 500; tick( 0.1 ); assert( queued == 0 );
	edicts[1].v.origin[0] = 535; tick( 0.1 ); assert( queued == 1 );
	/* a reset forgets the hold */
	setup(); assert( !ask()); SV_CoF_ExitReset(); tick( 0.1 );
	assert( queued == 0 && !SV_CoF_ExitWaiter());
	puts( "exit scenarios passed" );
	return 0;
}
```

- [ ] **Step 2: Write the failing guard-parser test**

`tests/engine/cof_exit_models_test.c`:
```c
/* SV_CoF_MapExitModels cut out of sv_game.c, against a small entity-script
 * parser: which brush models are this exit, and does it fire anything. */
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int qboolean;
#define true 1
#define false 0
#define MAX_TOKEN 2048
#define Q_strcmp strcmp
#define Q_atoi atoi
static int Q_stricmp( const char *a, const char *b )
{
	for( ; *a && tolower((unsigned char)*a ) == tolower((unsigned char)*b ); a++, b++ );
	return tolower((unsigned char)*a ) - tolower((unsigned char)*b );
}
static size_t Q_strncpy( char *dst, const char *src, size_t size )
{
	size_t n = strlen( src );
	if( n >= size ) n = size - 1;
	memcpy( dst, src, n ); dst[n] = 0;
	return n;
}
static char map_text[] =
	"{\n\"classname\" \"worldspawn\"\n}\n"
	"{\n\"classname\" \"trigger_changelevel\"\n\"map\" \"C_Start2\"\n\"landmark\" \"hahaland\"\n\"model\" \"*3\"\n}\n"
	"{\n\"classname\" \"trigger_changelevel\"\n\"map\" \"c_apartment1\"\n\"landmark\" \"land1\"\n\"model\" \"*4\"\n\"target\" \"relay\"\n}\n"
	"{\n\"classname\" \"trigger_changelevel\"\n\"map\" \"c_roof\"\n\"landmark\" \"land2\"\n\"model\" \"*5\"\n\"changetarget\" \"x\"\n}\n"
	"{\n\"classname\" \"trigger_changelevel\"\n\"map\" \"c_sewer\"\n\"landmark\" \"land3\"\n\"model\" \"*7\"\n\"killtarget\" \"y\"\n}\n"
	"{\n\"classname\" \"trigger_multiple\"\n\"target\" \"z\"\n\"model\" \"*6\"\n}\n";
static char *SV_ReadEntityScript( const char *map, int *flags ) { *flags = 0; return map_text; }
static void Mem_Free( void *p ) { assert( p == map_text ); }
static char *COM_ParseFile( char *data, char *token, int size )
{
	int n = 0;
	if( !data ) return NULL;
	while( *data && isspace((unsigned char)*data )) data++;
	if( !*data ) return NULL;
	if( *data == '"' )
	{
		for( data++; *data && *data != '"'; data++ )
			if( n < size - 1 ) token[n++] = *data;
		if( *data ) data++;
	}
	else token[n++] = *data++; /* a brace */
	token[n] = 0;
	return data;
}

#include "models_under_test.c"

int main( void )
{
	int models[4];
	qboolean fires = true;
	/* a plain story exit: its model, and nothing fired; the map key's case is ignored */
	assert( SV_CoF_MapExitModels( "c_start", "c_start2", "hahaland", models, 4, &fires ) == 1 && models[0] == 3 && !fires );
	/* each of the three keys ChangeLevelNow re-fires marks the exit */
	assert( SV_CoF_MapExitModels( "c_start", "c_apartment1", "land1", models, 4, &fires ) == 1 && models[0] == 4 && fires );
	assert( SV_CoF_MapExitModels( "c_start", "c_roof", "land2", models, 4, &fires ) == 1 && fires );
	assert( SV_CoF_MapExitModels( "c_start", "c_sewer", "land3", models, 4, &fires ) == 1 && fires );
	/* the older caller passes no flag; another exit's or trigger's target does not count */
	assert( SV_CoF_MapExitModels( "c_start", "c_start2", "hahaland", models, 4, NULL ) == 1 );
	fires = true;
	assert( SV_CoF_MapExitModels( "c_start", "c_nowhere", "hahaland", models, 4, &fires ) == 0 && !fires );
	puts( "exit model scenarios passed" );
	return 0;
}
```

`tests/test_enhanced_exit.py`:
```python
"""The exit hold's engine glue and its map guard, run natively offline."""
import unittest

from tests.native import ENGINE_SERVER, run_native, strip_includes


class ExitTests(unittest.TestCase):
    def test_exit_hold_in_the_engine(self):
        out = run_native(self, 'cof_exit_harness.c', {
            'sv_cof_exit_state.h': (ENGINE_SERVER / 'sv_cof_exit_state.h').read_text(),
            'exit_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_exit.c').read_text()),
        })
        self.assertIn('exit scenarios passed', out)

    def test_exit_guard_reads_the_map(self):
        game = (ENGINE_SERVER / 'sv_game.c').read_text()
        start = game.index('int SV_CoF_MapExitModels(')
        end = game.index('\n}\n', start) + 3
        out = run_native(self, 'cof_exit_models_test.c', {'models_under_test.c': game[start:end]})
        self.assertIn('exit model scenarios passed', out)

    def test_engine_hooks_are_in_place(self):
        game = (ENGINE_SERVER / 'sv_game.c').read_text()
        change = game[game.index('static void GAME_EXPORT pfnChangeLevel('):]
        change = change[:change.index('\n}\n')]
        self.assertLess(change.index('SV_CoF_ExitRequest( level, landname )'), change.index('SV_QueueChangeLevel( level, landname )'))
        alert = game[game.index('static void GAME_EXPORT pfnAlertMessage('):]
        self.assertIn('SV_CoF_ExitQuietAlert( buffer )', alert[:alert.index('\n}\n')])
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        self.assertIn('SV_CoF_ExitFrame();', story[story.index('void SV_CoF_StoryFrame('):])
        self.assertIn('SV_CoF_ExitReset();', story[story.index('void SV_CoF_StoryShutdown('):])


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 3: Run them to see them fail**

Run: `& python -m unittest tests.test_enhanced_exit -v`
Expected: `test_exit_hold_in_the_engine` ERRORs with `FileNotFoundError` for `sv_cof_exit.c`; `test_exit_guard_reads_the_map` FAILs on the compiler's "too many arguments" for `SV_CoF_MapExitModels`; `test_engine_hooks_are_in_place` ERRORs with `ValueError`.

- [ ] **Step 4: Export the two helpers the exit needs**

In `$ENGINE/engine/server/sv_cof_party.c`, change the definition
```c
static qboolean CoF_PartyInExit( const edict_t *player, const int *models, int count )
```
to
```c
qboolean SV_CoF_PartyInExit( const edict_t *player, const int *models, int count )
```
and its one call in `SV_CoF_PartyTransitionCapture` from `CoF_PartyInExit( cl->edict, exit_models, exit_count )` to `SV_CoF_PartyInExit( cl->edict, exit_models, exit_count )`.

In `$ENGINE/engine/server/sv_cof_party.h`, before `#endif`, add:
```c
// Standing in one of these exit brush models, or in a trigger that fires one.
qboolean SV_CoF_PartyInExit( const edict_t *player, const int *models, int count );
```

In `$ENGINE/engine/server/sv_save.c`, change the definition
```c
static qboolean CoF_LiveLandmark( const char *name, vec3_t origin )
```
to
```c
qboolean SV_CoF_LiveLandmark( const char *name, vec3_t origin )
```
and rename its two calls (in `SV_CoF_TransitionValidate` and `CoF_BeginStoryTransition`) to `SV_CoF_LiveLandmark`. In `CoF_BeginStoryTransition`, pass the new last argument:
```c
		int exits[16], exit_count = SV_CoF_MapExitModels( sv.name, map, landmark, exits, ARRAYSIZE( exits ), NULL );
```

In `$ENGINE/engine/server/server.h`, replace the `SV_CoF_MapExitModels` declaration (line 704) with:
```c
int SV_CoF_MapExitModels( const char *map, const char *destination, const char *landmark, int *models, int max, qboolean *fires );
qboolean SV_CoF_LiveLandmark( const char *name, vec3_t origin );
```

- [ ] **Step 5: Make the map reader report firing keys**

In `$ENGINE/engine/server/sv_game.c`, replace the whole `SV_CoF_MapExitModels` function and the comment above it with:
```c
// Brush model numbers of the map's trigger_changelevel entities leading to one
// destination/landmark pair: the party code uses them to tell who fired a
// story exit. Their map/landmark keys live only in game private data.
// *fires: such an exit carries target, killtarget or changetarget, which the
// game fires again on every request (ChangeLevelNow 10109D40).
int SV_CoF_MapExitModels( const char *map, const char *destination, const char *landmark, int *models, int max, qboolean *fires )
{
	int flags, count = 0;
	char *entities = SV_ReadEntityScript( map, &flags ), *cursor;
	char key[MAX_TOKEN], value[MAX_TOKEN], classname[MAX_TOKEN], next[MAX_TOKEN], name[MAX_TOKEN], model[MAX_TOKEN];
	qboolean extra;
	if( fires ) *fires = false;
	if( !entities ) return 0;
	cursor = entities;
	while( count < max && ( cursor = COM_ParseFile( cursor, key, sizeof( key ))) != NULL )
	{
		if( Q_strcmp( key, "{" )) break;
		classname[0] = next[0] = name[0] = model[0] = 0;
		extra = false;
		while(( cursor = COM_ParseFile( cursor, key, sizeof( key ))) != NULL && Q_strcmp( key, "}" ))
		{
			cursor = COM_ParseFile( cursor, value, sizeof( value ));
			if( !cursor ) break;
			if( !Q_strcmp( key, "classname" )) Q_strncpy( classname, value, sizeof( classname ));
			else if( !Q_strcmp( key, "map" )) Q_strncpy( next, value, sizeof( next ));
			else if( !Q_strcmp( key, "landmark" )) Q_strncpy( name, value, sizeof( name ));
			else if( !Q_strcmp( key, "model" )) Q_strncpy( model, value, sizeof( model ));
			else if( value[0] && ( !Q_strcmp( key, "target" ) || !Q_strcmp( key, "killtarget" ) || !Q_strcmp( key, "changetarget" ))) extra = true;
		}
		if( Q_strcmp( classname, "trigger_changelevel" ) || Q_stricmp( next, destination ) || Q_strcmp( name, landmark )) continue;
		if( fires && extra ) *fires = true;
		if( model[0] == '*' ) models[count++] = Q_atoi( model + 1 );
	}
	Mem_Free( entities );
	return count;
}
```

- [ ] **Step 6: Write the exit glue**

`$ENGINE/engine/server/sv_cof_exit.h`:
```c
/* Opt-in Cry of Fear story co-op: hold a level exit until the party is at it.
 * GPL-3.0-or-later. */
#ifndef SV_COF_EXIT_H
#define SV_COF_EXIT_H
void SV_CoF_ExitReset( void );
// pfnChangeLevel hook. true: change level now. false: held; the frame lets it go.
qboolean SV_CoF_ExitRequest( const char *level, const char *landmark );
void SV_CoF_ExitFrame( void );
// The warp key during a hold. true: it was the waiter's and has been dealt with.
qboolean SV_CoF_ExitWarpKey( sv_client_t *cl );
// The userid waiting at a held exit, or 0.
int SV_CoF_ExitWaiter( void );
// The game's "CHANGE LEVEL:" console line, repeated every frame of a hold.
qboolean SV_CoF_ExitQuietAlert( const char *text );
#endif
```

`$ENGINE/engine/server/sv_cof_exit.c`:
```c
/* Cry of Fear story co-op: hold a level exit until the party is at it.
 * GPL-3.0-or-later. The engine side of sv_cof_exit_state.h: it says who
 * stands in the exit, stores a held request once, prompts both players and
 * replays the request when everyone is there. */
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_party.h"
#include "sv_cof_retail.h"
#include "sv_cof_exit_state.h"
#include "sv_cof_exit.h"

#define COF_EXIT_MODELS 16

static cof_exit_state_t cof_exit;
static int cof_exit_models[COF_EXIT_MODELS], cof_exit_model_count;
static char cof_exit_unholdable[COF_EXIT_NAME]; // already reported in this map
static int cof_exit_unholdable_generation = -1;

void SV_CoF_ExitReset( void )
{
	CoF_ExitClear( &cof_exit );
	cof_exit_model_count = 0;
	cof_exit_unholdable_generation = -1;
}

// Everyone who counts, by client slot, against the exit last asked for.
static int CoF_ExitPlayers( cof_exit_player_t *players )
{
	int i, n = Q_min( svs.maxclients, COF_EXIT_PLAYERS );
	memset( players, 0, sizeof( *players ) * COF_EXIT_PLAYERS );
	for( i = 0; i < n; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_exit_player_t *p = &players[i];
		if( cl->state < cs_connected || FBitSet( cl->flags, FCL_FAKECLIENT )) continue;
		p->userid = cl->userid;
		if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) continue; // joining: holds the exit
		p->spawned = 1;
		p->alive = cl->edict->v.health > 0 && cl->edict->v.deadflag == DEAD_NO;
		p->touching = p->alive && SV_CoF_PartyInExit( cl->edict, cof_exit_models, cof_exit_model_count );
		VectorCopy( cl->edict->v.origin, p->origin );
	}
	return n;
}

static const char *CoF_ExitName( int userid )
{
	int i;
	for( i = 0; userid && i < svs.maxclients; i++ )
		if( svs.clients[i].state >= cs_connected && svs.clients[i].userid == userid ) return svs.clients[i].name;
	return "your partner";
}

qboolean SV_CoF_ExitRequest( const char *level, const char *landmark )
{
	cof_exit_player_t players[COF_EXIT_PLAYERS];
	cof_exit_state_t was = cof_exit;
	char map[COF_EXIT_NAME];
	int models[COF_EXIT_MODELS], count, n, began;
	qboolean fires = false;
	vec3_t spot;

	// Landmark-less requests are the game's own restarts and co-op doors: never held.
	if( !SV_CoF_StoryCoopEnabled() || COM_StringEmptyOrNULL( level ) || COM_StringEmptyOrNULL( landmark )) return true;
	if( Q_strlen( level ) >= sizeof( map )) return true;
	Q_strncpy( map, level, sizeof( map ));
	COM_StripExtension( map );
	// An exit the engine is going to refuse is not worth waiting at.
	if( !SV_CoF_LiveLandmark( landmark, spot )) return true;
	count = SV_CoF_MapExitModels( sv.name, map, landmark, models, COF_EXIT_MODELS, &fires );
	if( fires )
	{
		// The game fires this exit's targets on every request; holding it
		// would repeat them. No story exit has any today.
		if( cof_exit_unholdable_generation != svs.spawncount || Q_strcmp( cof_exit_unholdable, map ))
			Con_Printf( "cofstory phase=exit-unholdable destination=%s landmark=%s\n", map, landmark );
		Q_strncpy( cof_exit_unholdable, map, sizeof( cof_exit_unholdable ));
		cof_exit_unholdable_generation = svs.spawncount;
		CoF_ExitClear( &cof_exit );
		return true;
	}
	memcpy( cof_exit_models, models, sizeof( models ));
	cof_exit_model_count = count;
	n = CoF_ExitPlayers( players );
	if( CoF_ExitRequest( &cof_exit, players, n, svs.spawncount, map, landmark, spot, sv.time, &began ) == COF_EXIT_GO )
	{
		if( was.active )
			Con_Printf( "cofstory phase=exit-ready destination=%s landmark=%s waiter=%d waited=%.1f\n", map, landmark, was.waiter, sv.time - was.began );
		return true;
	}
	if( began )
		Con_Printf( "cofstory phase=exit-hold destination=%s landmark=%s waiter=%d walkin=%d missing=%d\n", map, landmark,
			cof_exit.waiter, cof_exit.walkin, CoF_ExitMissing( players, n, cof_exit.waiter ));
	return false;
}

// Replay the held request. The transition's own capture and arrival regroup
// (sv_cof_party.c) place a partner who is not beside the leader.
static void CoF_ExitGo( const char *phase )
{
	char level[COF_EXIT_NAME], landmark[COF_EXIT_NAME];
	Q_strncpy( level, cof_exit.level, sizeof( level ));
	Q_strncpy( landmark, cof_exit.landmark, sizeof( landmark ));
	Con_Printf( "cofstory phase=%s destination=%s landmark=%s waiter=%d waited=%.1f\n", phase, level, landmark, cof_exit.waiter, sv.time - cof_exit.began );
	CoF_ExitClear( &cof_exit );
	SV_QueueChangeLevel( level, landmark );
}

// The words carry the meaning: the colour index did not render in the live check.
static void CoF_ExitPrompts( const cof_exit_player_t *players, int n )
{
	const cof_exit_player_t *waiter = CoF_ExitFind( players, n, cof_exit.waiter );
	const char *missing = CoF_ExitName( CoF_ExitMissing( players, n, cof_exit.waiter ));
	char text[160];
	int i;
	for( i = 0; i < n; i++ )
	{
		// a dead player has the game's own respawn prompt and cannot warp
		if( !players[i].userid || !players[i].spawned || !players[i].alive ) continue;
		if( CoF_ExitAt( &players[i], waiter ))
		{
			if( sv.time - cof_exit.began >= COF_EXIT_ESCAPE )
				Q_snprintf( text, sizeof( text ), "Please wait until everyone is ready! (press J to leave without %s)", missing );
			else Q_snprintf( text, sizeof( text ), "Please wait until everyone is ready! (%s can press J to join you)", missing );
			SV_CoF_RetailProFont( &svs.clients[i], 2, text, false );
		}
		else
		{
			Q_snprintf( text, sizeof( text ), "%s is at the exit. Press J to join.", CoF_ExitName( cof_exit.waiter ));
			SV_CoF_RetailProFont( &svs.clients[i], 1, text, false );
		}
	}
}

void SV_CoF_ExitFrame( void )
{
	cof_exit_player_t players[COF_EXIT_PLAYERS];
	int n, waiter = cof_exit.waiter;
	if( !cof_exit.active ) return;
	n = CoF_ExitPlayers( players );
	switch( CoF_ExitFrame( &cof_exit, players, n, svs.spawncount, sv.time ))
	{
	case COF_EXIT_READY:
		CoF_ExitGo( "exit-ready" );
		break;
	case COF_EXIT_CANCELLED:
		Con_Printf( "cofstory phase=exit-cancelled waiter=%d\n", waiter );
		break;
	case COF_EXIT_WAIT:
		if( CoF_ExitPromptDue( &cof_exit, sv.time )) CoF_ExitPrompts( players, n );
		break;
	}
}

qboolean SV_CoF_ExitWarpKey( sv_client_t *cl )
{
	char text[96];
	int left, answer = CoF_ExitEscape( &cof_exit, cl->userid, sv.time, &left );
	if( !answer ) return false;
	if( answer > 0 )
	{
		CoF_ExitGo( "exit-forced" ); // the escape hatch: leave without the partner
		return true;
	}
	Q_snprintf( text, sizeof( text ), "You can leave without your partner in %d seconds.", left );
	SV_CoF_RetailProFont( cl, 2, text, false );
	return true;
}

int SV_CoF_ExitWaiter( void )
{
	return cof_exit.active ? cof_exit.waiter : 0;
}

qboolean SV_CoF_ExitQuietAlert( const char *text )
{
	return cof_exit.active && !Q_strncmp( text, "CHANGE LEVEL: ", 14 );
}
```

- [ ] **Step 7: Hook the request, the alert and the frame**

In `$ENGINE/engine/server/sv_game.c`, add after `#include "sv_cof_sender.h"`:
```c
#include "sv_cof_exit.h"
```

In `pfnChangeLevel`, replace the final line `SV_QueueChangeLevel( level, landname );` with:
```c
	// Cry of Fear story co-op: wait until the whole party is at the exit
	if( !SV_CoF_ExitRequest( level, landname ))
		return;
	SV_QueueChangeLevel( level, landname );
```

In `pfnAlertMessage`, directly before the comment `// check message for pass`, add:
```c
	// Cry of Fear story co-op: a held exit repeats its console line every frame
	if( type == at_console && SV_CoF_ExitQuietAlert( buffer ))
		return;

```

In `$ENGINE/engine/server/sv_cof_story.c`, add after `#include "sv_cof_share.h"`:
```c
#include "sv_cof_exit.h"
```
and change the frame and the shutdown to:
```c
void SV_CoF_StoryFrame( void )
{
	if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ) return;
	SV_CoF_PartyFrame();
	SV_CoF_ExitFrame();
	SV_CoF_ShareFrame();
}

void SV_CoF_StoryShutdown( void )
{
	SV_CoF_CommandSender( NULL ); // an engine error inside a command leaves it set
	SV_CoF_PartyShutdown();
	SV_CoF_ExitReset();
	SV_CoF_ShareReset();
	CoF_StoryCloseGate();
	SV_CoF_ClearValidation();
}
```

- [ ] **Step 8: Run the tests to see them pass**

Run: `& python -m unittest tests.test_enhanced_exit tests.test_enhanced_party tests.test_enhanced_server_transition tests.test_enhanced_sender -v`
Expected: `OK`.

- [ ] **Step 9: Build and commit**

Run: `& cmd.exe /d /c tools\build_enhanced.cmd`
Expected: `'build' finished successfully`.

```bash
git -C upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a add engine/server/sv_cof_exit.c engine/server/sv_cof_exit.h engine/server/sv_game.c engine/server/sv_save.c engine/server/server.h engine/server/sv_cof_party.c engine/server/sv_cof_party.h engine/server/sv_cof_story.c
git -C upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a commit -m "Hold a story exit until the party is at it"
git add tests/engine/cof_exit_harness.c tests/engine/cof_exit_models_test.c tests/test_enhanced_exit.py
git commit -m "Test the exit hold's engine glue and map guard"
```

---

### Task 6: Doors and scripted teleports take only a partner beside you

Today a door pulls every available partner, however far away, and a scripted teleport pulls exactly the partners who were far away. Both now take only a partner within 128 units of the player who went. Anyone left behind who can no longer walk after them is told how to follow.

**Files:**
- Modify: `$ENGINE/engine/server/sv_cof_party.c` (includes; `cof_party_door_t`; `CoF_PartyWatchDoors`; `CoF_PartyFinishDoors`; `CoF_PartyTeleports`)
- Modify: `tests/engine/cof_party_harness.c`

**Interfaces:**
- Consumes: `SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable )`.
- Produces: log line `cofstory phase=cut-off userid=%d owner=%d kind=door|teleport`; prompt `<name> went somewhere you can't follow. Press J to join.`

- [ ] **Step 1: Extend the harness doubles**

In `tests/engine/cof_party_harness.c`:

Add after `#define Q_atoi atoi`:
```c
#define Q_snprintf snprintf
```

Replace the client type
```c
typedef struct { int state,userid; unsigned int flags; edict_t *edict,*pViewEntity; } sv_client_t;
```
with:
```c
typedef struct { int state,userid; unsigned int flags; char name[32]; edict_t *edict,*pViewEntity; } sv_client_t;
```

Add after the line `static int no_floor; static float no_floor_x; /* a shaft: nothing below this x */`:
```c
static char prompts[MAX_CLIENTS][192]; /* the last ProFont line each player was sent */
```

Add after the `SV_LinkEdict` double:
```c
static qboolean SV_CoF_RetailProFont(sv_client_t *cl,int colour,const char *text,qboolean reliable) {
    snprintf(prompts[cl-clients],sizeof(prompts[0]),"%d:%s",colour,text); return true;
}
```

In `setup`, add `memset(prompts,0,sizeof(prompts));` after `memset(private_data,0,sizeof(private_data));`, and inside its `for` loop add:
```c
        snprintf(clients[i].name,sizeof(clients[i].name),"Little Rascal %d",i+1);
```

- [ ] **Step 2: Move the existing teleport scenarios' partner beside the activator**

Seven existing teleport scenarios put the partner at `-500` and expect them to be pulled. Under the new rule a partner there is left behind, so those scenarios must start with the partner beside the activator. Replace every occurrence (there are exactly 7) of
```c
entities[2].v.origin[0]=-500;
```
with
```c
entities[2].v.origin[0]=64;
```
The expected landing spots in those scenarios do not change.

- [ ] **Step 3: Write the failing scenarios**

Insert directly before `puts("party scenarios passed"); return 0;`:
```c
    /* Split play: a door taken with the partner across the room pulls nobody. */
    setup(); entities[2].v.origin[0]=300; start_door(); finish_door(); tick(0.1);
    assert(links==0 && !strstr(status_log,"door-party-moved") && !prompts[1][0]);
    /* ...and when the door removed itself, the one left behind is told how to follow. */
    setup(); entities[2].v.origin[0]=300; start_door(); entities[5].free=1; finish_door(); tick(0.1);
    assert(links==0 && !strcmp(prompts[1],"1:Little Rascal 1 went somewhere you can't follow. Press J to join."));
    assert(strstr(status_log,"cofstory phase=cut-off userid=101 owner=100 kind=door") && !prompts[0][0]);
    /* A partner who went through the removed door with the opener is not told. */
    setup(); start_door(); entities[5].free=1; finish_door(); tick(0.1);
    assert(links==1 && !prompts[1][0] && !strstr(status_log,"cut-off"));
    /* A dead partner is not told either: they cannot press J. */
    setup(); entities[2].v.origin[0]=300; entities[2].v.health=0; start_door(); entities[5].free=1; finish_door(); tick(0.1);
    assert(!prompts[1][0]);
    /* A scripted teleport leaves a far partner where they are, and tells them. */
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19; entities[10].v.origin[0]=2000;
    entities[11].v.classname=18; entities[11].v.target=19; entities[2].v.origin[0]=-500; tick(0.1);
    VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=2000;
    SV_CoF_PartyNoteOrigin(&entities[1],before); tick(0.1);
    assert(links==0 && entities[2].v.origin[0]==-500 && strstr(status_log,"companions=0"));
    assert(strstr(status_log,"cofstory phase=cut-off userid=101 owner=100 kind=teleport"));
    assert(!strcmp(prompts[1],"1:Little Rascal 1 went somewhere you can't follow. Press J to join."));
    /* A partner already at the destination is neither moved nor told. */
    setup(); entities[10].v.classname=17; entities[10].v.targetname=19; entities[10].v.origin[0]=2000;
    entities[11].v.classname=18; entities[11].v.target=19; entities[2].v.origin[0]=2060; tick(0.1);
    VectorCopy(entities[1].v.origin,before); entities[1].v.origin[0]=2000;
    SV_CoF_PartyNoteOrigin(&entities[1],before); tick(0.1);
    assert(links==0 && !prompts[1][0]);
```

- [ ] **Step 4: Run it to see it fail**

Run: `& python -m unittest tests.test_enhanced_party -v`
Expected: FAIL. The first new assertion fails with `links==0` (the far partner is still pulled through the door).

- [ ] **Step 5: Implement the radius and the prompt**

In `$ENGINE/engine/server/sv_cof_party.c`:

Add after `#include "sv_cof_party.h"`:
```c
#include "sv_cof_retail.h"
```

In `cof_party_door_t`, add two members after `double deadline;`:
```c
	edict_t *door; // the inter_door that began it, to see whether it removed itself
	int door_serial;
```

In `CoF_PartyWatchDoors`, after `event->deadline = sv.time + 8.0;`, add:
```c
				event->door = door;
				event->door_serial = door->serialnumber;
```
and replace the companion loop
```c
				for( j = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
				{
					if( j == slot || !CoF_PartyAvailable( &svs.clients[j] )) continue;
					event->companions[j] = CoF_PartyIdentity( &svs.clients[j] );
					event->pending[j] = true;
				}
```
with:
```c
				for( j = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
				{
					if( j == slot || !CoF_PartyAvailable( &svs.clients[j] )) continue;
					// Only a partner right beside the opener goes through with them.
					if( CoF_PartyDistanceSquared( svs.clients[j].edict->v.origin, cl->edict->v.origin ) > COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR ) continue;
					event->companions[j] = CoF_PartyIdentity( &svs.clients[j] );
					event->pending[j] = true;
				}
```

Add directly above `static void CoF_PartyFinishDoors( void )`:
```c
// The door that began this event is still there to be used again.
static qboolean CoF_PartyDoorRemains( const cof_party_door_t *event )
{
	return SV_IsValidEdict( event->door ) && event->door->serialnumber == event->door_serial &&
		!( event->door->v.flags & FL_KILLME ) && !Q_strcmp( SV_GetString( event->door->v.classname ), "inter_door" );
}

// The owner went where the others cannot walk after them. Tell every living
// player who was not taken along and is not already there how to follow.
static void CoF_PartyCutOff( int slot, const cof_party_door_t *event, const char *kind )
{
	sv_client_t *owner = &svs.clients[slot];
	char text[96];
	int i;
	Q_snprintf( text, sizeof( text ), "%s went somewhere you can't follow. Press J to join.", owner->name );
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( i == slot || event->pending[i] || ( cl->flags & FCL_FAKECLIENT ) || !CoF_PartyPlayer( cl )) continue;
		if( CoF_PartyDistanceSquared( cl->edict->v.origin, owner->edict->v.origin ) <= COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR ) continue;
		SV_CoF_RetailProFont( cl, 1, text, true );
		Con_Printf( "cofstory phase=cut-off userid=%d owner=%d kind=%s\n", cl->userid, owner->userid, kind );
	}
}

```

In `CoF_PartyFinishDoors`, replace
```c
			event->completed = true;
			event->deadline = sv.time + 5.0;
```
with:
```c
			event->completed = true;
			event->deadline = sv.time + 5.0;
			// A one-shot door is gone now: nobody else can take it.
			if( !CoF_PartyDoorRemains( event )) CoF_PartyCutOff( slot, event, "door" );
```

In `CoF_PartyTeleports`, replace
```c
			// A partner already beside the destination was part of the scene.
			if( CoF_PartyDistanceSquared( other->edict->v.origin, cl->edict->v.origin ) <= COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR ) continue;
			event->companions[j] = cof_party_players[j].identity;
```
with:
```c
			// A partner already beside the destination was part of the scene.
			if( CoF_PartyDistanceSquared( other->edict->v.origin, cl->edict->v.origin ) <= COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR ) continue;
			// Only a partner who stood beside the activator is taken along.
			if( CoF_PartyDistanceSquared( other->edict->v.origin, p->teleport_from ) > COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR ) continue;
			event->companions[j] = cof_party_players[j].identity;
```
and replace
```c
		Con_Printf( "cofstory phase=teleport-begin userid=%d target=%s companions=%d\n", cl->userid, SV_GetString( dest->v.targetname ), count );
		if( !count ) event->active = false;
```
with:
```c
		Con_Printf( "cofstory phase=teleport-begin userid=%d target=%s companions=%d\n", cl->userid, SV_GetString( dest->v.targetname ), count );
		CoF_PartyCutOff( i, event, "teleport" );
		if( !count ) event->active = false;
```

- [ ] **Step 6: Run the tests to see them pass**

Run: `& python -m unittest tests.test_enhanced_party -v`
Expected: `OK`.

- [ ] **Step 7: Build and commit**

Run: `& cmd.exe /d /c tools\build_enhanced.cmd`
Expected: `'build' finished successfully`.

```bash
git -C upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a add engine/server/sv_cof_party.c
git -C upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a commit -m "Take only a partner beside you through doors and teleports"
git add tests/engine/cof_party_harness.c
git commit -m "Test the door and teleport radius and the cut-off prompt"
```

---

### Task 7: Warp to the partner with J

**Files:**
- Modify: `$ENGINE/engine/server/sv_cof_party.c`, `$ENGINE/engine/server/sv_cof_party.h`
- Modify: `$ENGINE/engine/server/sv_client.c` (after the `SV_CoF_CheatsClientCommand` call, line 3121)
- Modify: `tests/engine/cof_party_harness.c`
- Modify: `tools/Start-StoryHost.ps1:29`, `tools/Start-LocalGuest.ps1:27-34`, `tools/friend_setup.py:211-215`
- Create: `tests/test_warp_bind.py`

**Interfaces:**
- Consumes: `SV_CoF_ExitWarpKey( sv_client_t *cl )` and `SV_CoF_ExitWaiter( void )` from Task 5; `SV_CoF_RetailProFont`; `SV_CoF_RetailScreenFade( sv_client_t *cl, float duration, float hold, int flags, int r, int g, int b, int a )`.
- Produces:
  - `qboolean SV_CoF_PartyClientCommand( sv_client_t *cl )`: true when the engine handled the command.
  - Client command `cof_warp`, bound to J.
  - Log lines `cofstory phase=warp userid=%d partner=%d origin=%.3f,%.3f,%.3f` and `cofstory phase=warp-refused userid=%d reason=dead|door|no-partner|no-room`. A press during the 2 s cooldown is ignored without a log line, so key mashing does not flood the report.

- [ ] **Step 1: Extend the harness doubles**

In `tests/engine/cof_party_harness.c`, add after the `prompts` line from Task 6:
```c
static const char *command_name=""; static int exit_key,exit_waiter,fades;
```
and after the `SV_CoF_RetailProFont` double:
```c
static const char *Cmd_Argv(int i) { return i==0 ? command_name : ""; }
static qboolean SV_CoF_ExitWarpKey(sv_client_t *cl) { return exit_key; }
static int SV_CoF_ExitWaiter(void) { return exit_waiter; }
static qboolean SV_CoF_RetailScreenFade(sv_client_t *cl,float duration,float hold,int flags,int r,int g,int b,int a) {
    assert(duration==0.5f && flags==0 && r==0 && g==0 && b==0 && a==255); fades++; return true;
}
```
In `setup`, add `exit_key=exit_waiter=fades=0;` after the `memset(prompts,...)` line. Add after the `tick` helper:
```c
static void warp(int p) { command_name="cof_warp"; assert(SV_CoF_PartyClientCommand(&clients[p])); }
```

- [ ] **Step 2: Write the failing scenarios**

Insert directly before `puts("party scenarios passed"); return 0;`:
```c
    /* Warp: J puts the caller beside the partner, facing the same way, with a blink. */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; entities[1].v.angles[1]=90; warp(1);
    assert(links==1 && entities[2].v.origin[0]==35 && entities[2].v.angles[1]==90 && fades==1);
    assert(strstr(status_log,"cofstory phase=warp userid=101 partner=100"));
    /* ...not again inside the 2 s cooldown, and silently */
    entities[2].v.origin[0]=3000; warp(1);
    assert(links==1 && entities[2].v.origin[0]==3000 && !strstr(status_log,"warp-refused"));
    tick(2.1); warp(1); assert(links==2 && entities[2].v.origin[0]==35);
    /* Review Focus 4: never while the caller is dead or in a door sequence... */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; entities[2].v.health=0; warp(1);
    assert(links==0 && strstr(status_log,"warp-refused userid=101 reason=dead"));
    setup(); tick(0.1); entities[2].v.origin[0]=3000; private_data[1][0x1EA4]=1; warp(1);
    assert(links==0 && strstr(status_log,"warp-refused userid=101 reason=door"));
    /* ...nor onto a partner who is in a door sequence or mid level change */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; private_data[0][0x1EA4]=1; warp(1);
    assert(links==0 && strstr(status_log,"warp-refused userid=101 reason=no-partner"));
    assert(!strcmp(prompts[1],"1:Nobody to join right now."));
    setup(); tick(0.1); entities[2].v.origin[0]=3000; clients[0].state=3; warp(1);
    assert(links==0 && entities[2].v.origin[0]==3000 && strstr(status_log,"reason=no-partner"));
    /* No room beside the partner: stay put and say so. */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; blocked=1; warp(1);
    assert(links==0 && entities[2].v.origin[0]==3000 && fades==0);
    assert(!strcmp(prompts[1],"1:No room next to Little Rascal 1.") && strstr(status_log,"reason=no-room"));
    /* During an exit hold the waiter's J belongs to the exit, not the warp. */
    setup(); tick(0.1); entities[2].v.origin[0]=3000; exit_key=1; warp(1);
    assert(links==0 && !strstr(status_log,"phase=warp"));
    /* With three players the nearest partner is the target, unless someone waits at a held exit. */
    setup(); svs.maxclients=3; clients[2]=clients[1]; clients[2].userid=102; clients[2].edict=&entities[3];
    entities[3]=entities[2]; entities[3].serialnumber=12; entities[3].pvPrivateData=private_data[2];
    entities[2].v.origin[0]=3000; entities[3].v.origin[0]=2900; tick(0.1);
    warp(1); assert(entities[2].v.origin[0]==2935);
    tick(2.1); exit_waiter=100; warp(1); assert(entities[2].v.origin[0]==35);
    /* Every other command goes on to the game; so does this one when the story layer is off. */
    setup(); tick(0.1); command_name="say"; assert(!SV_CoF_PartyClientCommand(&clients[1]));
    gate=false; command_name="cof_warp"; assert(!SV_CoF_PartyClientCommand(&clients[1])); gate=true;
    /* A player still joining presses J: handled, nothing moves. */
    setup(); tick(0.1); clients[1].state=3; warp(1); assert(links==0 && !strstr(status_log,"warp"));
```

`tests/test_warp_bind.py`:
```python
"""J is bound to cof_warp for the host, the local guest and the friend's setup."""
import unittest
from pathlib import Path

from tools import friend_setup

ROOT = Path(__file__).resolve().parents[1]


class WarpBindTests(unittest.TestCase):
    def test_host_launch_binds_j(self):
        self.assertIn('+bind j cof_warp +map c_start', (ROOT / 'tools/Start-StoryHost.ps1').read_text())

    def test_local_guest_config_binds_j(self):
        self.assertIn("'bind j cof_warp'", (ROOT / 'tools/Start-LocalGuest.ps1').read_text())

    def test_friend_setup_binds_j(self):
        self.assertIn('bind "j" "cof_warp"', friend_setup.default_controls())


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 3: Run them to see them fail**

Run: `& python -m unittest tests.test_enhanced_party tests.test_warp_bind -v`
Expected: the party test FAILs to compile (`SV_CoF_PartyClientCommand` undefined); the three bind tests FAIL.

- [ ] **Step 4: Implement the warp**

In `$ENGINE/engine/server/sv_cof_party.c`:

Add after `#include "sv_cof_retail.h"`:
```c
#include "sv_cof_exit.h"
```

Add after `#define COF_PARTY_TELEPORT_WAIT 10.0`:
```c
#define COF_PARTY_WARP_COOLDOWN 2.0
```

In `cof_party_player_t`, add after `vec3_t teleport_from, teleport_to;`:
```c
	double warp_after; // the next moment this player's warp key works
```

Add directly above `void SV_CoF_PartyFrame( void )`:
```c
static void CoF_PartyWarpRefused( sv_client_t *cl, const char *reason, const char *text )
{
	Con_Printf( "cofstory phase=warp-refused userid=%d reason=%s\n", cl->userid, reason );
	if( text ) SV_CoF_RetailProFont( cl, 1, text, false );
}

// cof_warp (bound to J): join the partner, wherever they are.
static void CoF_PartyWarp( sv_client_t *cl )
{
	int i = CoF_PartySlot( cl ), j, waiter = SV_CoF_ExitWaiter();
	sv_client_t *partner = NULL;
	float best = 0.0f, d;
	vec3_t feet, landing, angles = { 0, 0, 0 };
	char text[96];
	byte *pv;
	if( i < 0 ) return;
	CoF_PartyCheckGeneration();
	if( !( pv = CoF_PartyPlayer( cl )))
	{
		CoF_PartyWarpRefused( cl, "dead", NULL ); // the game's respawn prompt already speaks to them
		return;
	}
	if( pv[COF_PARTY_DOOR_FLAG] )
	{
		CoF_PartyWarpRefused( cl, "door", NULL ); // the door sequence moves them itself
		return;
	}
	if( sv.time < cof_party_players[i].warp_after ) return;
	for( j = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
	{
		sv_client_t *other = &svs.clients[j];
		byte *opv = CoF_PartyPlayer( other );
		if( j == i || !opv || opv[COF_PARTY_DOOR_FLAG] || ( other->flags & FCL_FAKECLIENT )) continue;
		// During an exit hold the one waiting at the exit is the place to be.
		if( waiter && other->userid == waiter )
		{
			partner = other;
			break;
		}
		d = CoF_PartyDistanceSquared( cl->edict->v.origin, other->edict->v.origin );
		if( !partner || d < best ) { partner = other; best = d; }
	}
	if( !partner )
	{
		CoF_PartyWarpRefused( cl, "no-partner", "Nobody to join right now." );
		return;
	}
	CoF_PartyFeet( cl, partner->edict, feet );
	angles[1] = partner->edict->v.angles[1];
	if( !CoF_PartyLanding( cl, partner->edict, feet, landing ) || !CoF_PartyMove( cl, landing, angles ))
	{
		Q_snprintf( text, sizeof( text ), "No room next to %s.", partner->name );
		CoF_PartyWarpRefused( cl, "no-room", text );
		return;
	}
	cof_party_players[i].warp_after = sv.time + COF_PARTY_WARP_COOLDOWN;
	// Black, fading in over half a second: the retail co-op door's blink. The
	// warp does not depend on it being seen.
	SV_CoF_RetailScreenFade( cl, 0.5f, 0.0f, 0, 0, 0, 0, 255 );
	Con_Printf( "cofstory phase=warp userid=%d partner=%d origin=%.3f,%.3f,%.3f\n", cl->userid, partner->userid, landing[0], landing[1], landing[2] );
}

// Client commands the story layer answers itself, before the game DLL sees them.
qboolean SV_CoF_PartyClientCommand( sv_client_t *cl )
{
	if( Q_strcmp( Cmd_Argv( 0 ), "cof_warp" ) || sv.state != ss_active || !SV_CoF_StoryCoopEnabled() ) return false;
	// A held exit's waiter uses the same key as the escape hatch.
	if( cl->state == cs_spawned && !SV_CoF_ExitWarpKey( cl )) CoF_PartyWarp( cl );
	return true;
}

```

In `$ENGINE/engine/server/sv_cof_party.h`, before `#endif`, add:
```c
// cof_warp and other commands the story layer handles itself; true when handled.
qboolean SV_CoF_PartyClientCommand( sv_client_t *cl );
```

In `$ENGINE/engine/server/sv_client.c`, directly after
```c
	if( SV_CoF_CheatsClientCommand( cl ))
		return;
```
add:
```c

	// Cry of Fear story co-op: cof_warp
	if( SV_CoF_PartyClientCommand( cl ))
		return;
```

- [ ] **Step 5: Bind J for the host, the local guest and the friend's setup**

In `tools/Start-StoryHost.ps1`, in the last line's `-ArgumentList`, change `+fps_max 60 +map c_start` to `+fps_max 60 +bind j cof_warp +map c_start`.

In `tools/Start-LocalGuest.ps1`, add one line to the `$joinCfg` list after `'cl_forwardspeed 300', ...`:
```powershell
    'bind j cof_warp'
```

In `tools/friend_setup.py`, in `default_controls`, extend the last line of the `binds` dict:
```python
             "1": "quicksel 1", "2": "quicksel 2", "3": "quicksel 3", "y": "messagemode", "k": "+voicerecord",
             "j": "cof_warp"}
```
The remote friend's packaged setup is not rebuilt in this plan; the next package build picks this up.

- [ ] **Step 6: Run the tests to see them pass**

Run: `& python -m unittest tests.test_enhanced_party tests.test_warp_bind tests.test_friend_setup -v`
Expected: `OK`.

- [ ] **Step 7: Build and commit**

Run: `& cmd.exe /d /c tools\build_enhanced.cmd`
Expected: `'build' finished successfully`.

```bash
git -C upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a add engine/server/sv_cof_party.c engine/server/sv_cof_party.h engine/server/sv_client.c
git -C upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a commit -m "Add cof_warp: join the partner with J"
git add tests/engine/cof_party_harness.c tests/test_warp_bind.py tools/Start-StoryHost.ps1 tools/Start-LocalGuest.ps1 tools/friend_setup.py
git commit -m "Bind J to cof_warp and test the warp"
```

---

### Task 8: Report the exit, warp and cut-off phases

**Files:**
- Modify: `tools/story_run_report.py:36-40`
- Modify: `tests/test_story_run_report.py`

**Interfaces:**
- Consumes: the log phases from Tasks 5–7.
- Produces: counts for `exit-hold`, `exit-ready`, `exit-cancelled`, `warp`, `cut-off`; warnings for `exit-forced`, `exit-unholdable`, `warp-refused`.

- [ ] **Step 1: Write the failing test**

In `tests/test_story_run_report.py`, add this method to `StoryRunReportTests`, after `test_combined_counted_and_stalled_warned`:
```python
    def test_exit_and_warp_phases_are_counted_and_forced_exits_warned(self):
        text = (LOG + '[2026:10:02|20:05:00] cofstory phase=exit-hold destination=c_start2 landmark=hahaland waiter=1 walkin=1 missing=2\n'
                '[2026:10:02|20:05:04] cofstory phase=warp userid=2 partner=1 origin=1,2,3\n'
                '[2026:10:02|20:05:04] cofstory phase=exit-ready destination=c_start2 landmark=hahaland waiter=1 waited=4.0\n'
                '[2026:10:02|20:06:00] cofstory phase=exit-cancelled waiter=1\n'
                '[2026:10:02|20:07:00] cofstory phase=cut-off userid=2 owner=1 kind=door\n'
                '[2026:10:02|20:08:00] cofstory phase=exit-forced destination=c_start landmark=hahaland waiter=1 waited=11.5\n'
                '[2026:10:02|20:09:00] cofstory phase=warp-refused userid=2 reason=no-room\n'
                '[2026:10:02|20:10:00] cofstory phase=exit-unholdable destination=c_roof landmark=land2\n')
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'host.log'
            log.write_text(text, encoding='latin-1')
            session = rr.parse_log(log)
        for phase in ('exit-hold', 'exit-ready', 'exit-cancelled', 'warp', 'cut-off'):
            self.assertEqual(session['party'][phase], 1, phase)
        warned = ' '.join(what for _, what in session['warnings'])
        for phase in ('exit-forced', 'warp-refused', 'exit-unholdable'):
            self.assertIn(phase, warned)
        self.assertNotIn('exit-ready', warned)
```

- [ ] **Step 2: Run it to see it fail**

Run: `& python -m unittest tests.test_story_run_report -v`
Expected: the new test FAILs with `AssertionError: 0 != 1 : exit-hold`.

- [ ] **Step 3: Add the phases**

In `tools/story_run_report.py`, replace the `PARTY_EVENTS` and `WARN` definitions with:
```python
# Party events worth a count; the ones in WARN mean a partner may have been left behind.
PARTY_EVENTS = ('exit-leader', 'arrival-placed', 'arrival-kept', 'arrival-overlapped', 'door-party-moved',
                'teleport-party-moved', 'spawn-placed', 'starter-shared', 'door-visibility-restored',
                'item-found', 'item-used', 'item-dropped', 'item-delivered', 'item-removed', 'item-combined',
                'exit-hold', 'exit-ready', 'exit-cancelled', 'warp', 'cut-off')
WARN = ('arrival-stranded', 'door-expired', 'teleport-expired', 'door-party-skipped', 'teleport-party-skipped',
        'starter-deferred', 'item-deferred', 'item-stalled',
        'exit-forced', 'exit-unholdable', 'warp-refused')
```

- [ ] **Step 4: Run the tests to see them pass**

Run: `& python -m unittest tests.test_story_run_report -v`
Expected: `OK`.

- [ ] **Step 5: Commit**

```bash
git add tools/story_run_report.py tests/test_story_run_report.py
git commit -m "Count exit, warp and cut-off phases in the run report"
```

---

### Task 9: Deploy stage 3 and check it live

**Files:**
- Modify: `state/enhanced-remote-session.json` (engine hash only; untracked, private)
- Modify: `AGENT-HANDOFF-2026-09-27.md` (new dated UPDATE section at the top)
- Create: `evidence/stage3-exit-warp/` (untracked: build, pre-deploy engine backups, logs)

**Interfaces:**
- Consumes: everything from Tasks 1 and 4–8.
- Produces: recorded live results for the exit hold, the warp, the escape hatch and the door radius, and the list of rulings below in the handoff.

- [ ] **Step 1: Run the whole suite**

Run: `& python -m unittest discover -s tests 2>&1 | Select-Object -Last 3`
Expected: `OK`, 127 tests (117 before this plan, plus 2 sender, 1 exit state, 3 exit, 3 bind and 1 report test).

- [ ] **Step 2: Stage and deploy the engine to both copies**

Run in PowerShell from the project root:
```powershell
if (Get-CimInstance Win32_Process -Filter "Name='CoFLaunchApp.exe' OR Name='cof.exe'") { throw 'Game is running; not deploying.' }
$build = 'upstream\cof-enhanced\xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a\build-local-20260927\engine'
$stage = 'evidence\stage3-exit-warp'
New-Item -ItemType Directory -Force "$stage\host-before","$stage\local-client-before" | Out-Null
Copy-Item "$build\xash.dll","$build\xash.pdb" $stage
Copy-Item runtimes\enhanced\xash.dll "$stage\host-before\"
Copy-Item runtimes\enhanced-local-client\xash.dll "$stage\local-client-before\"
Copy-Item "$stage\xash.dll" runtimes\enhanced\xash.dll -Force
Copy-Item "$stage\xash.dll" runtimes\enhanced-local-client\xash.dll -Force
$json = Get-Content state\enhanced-remote-session.json -Raw | ConvertFrom-Json
$json.engine_sha256 = (Get-FileHash "$stage\xash.dll").Hash
$json | ConvertTo-Json | Set-Content state\enhanced-remote-session.json -Encoding utf8NoBOM
Get-FileHash runtimes\enhanced\xash.dll, runtimes\enhanced-local-client\xash.dll | Select-Object Hash
```
Expected: two identical hashes, equal to the new `engine_sha256`.

- [ ] **Step 3: Tell the user, then start the host and the guest**

Say to the user: "Launching the host and the local guest now for the exit and warp checks (`c_start`)." Then:
```powershell
Start-Process powershell.exe -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','tools\Start-StoryHost.ps1'
Select-String -Path runtimes\enhanced\story-remote-host.log -Pattern 'gate-enabled' | Select-Object -Last 1
Start-Process powershell.exe -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','tools\Start-LocalGuest.ps1'
```
Run the second line again until it prints the gate line, then the third.

- [ ] **Step 4: Warp and door radius**

**USER CHECK**, one item at a time. After each, read the host log with
`Select-String -Path runtimes\enhanced\story-remote-host.log -Pattern 'cofstory phase=(warp|cut-off|door-)' | Select-Object -Last 6`.

1. The two little rascals stand far apart. The guest presses J.
   Expected: the guest appears beside the host; log `phase=warp userid=<guest> partner=<host>`. Ask whether the screen blinked.
2. The guest presses J twice quickly.
   Expected: one warp only.
3. Far apart again. The host takes a door (`inter_door`).
   Expected: the guest is not pulled; no `door-party-moved` line.
4. Standing together. The host takes a door.
   Expected: the guest follows; log `phase=door-party-moved`.

Save the log excerpt to `evidence\stage3-exit-warp\01-warp-doors.log`.

- [ ] **Step 5: Exit hold, warp to release, cancel and escape hatch**

**USER CHECK**, with `Select-String -Path runtimes\enhanced\story-remote-host.log -Pattern 'cofstory phase=exit-' | Select-Object -Last 6` after each.

1. The guest stays in the first room. The host walks into the exit to `c_start2`.
   Expected: the level does not change. The host sees "Please wait until everyone is ready! (Little Rascal 2 can press J to join you)". The guest sees "… is at the exit. Press J to join." Log: one `phase=exit-hold` line.
2. The host steps back out.
   Expected: log `phase=exit-cancelled`; the prompts stop.
3. The host walks in again; the guest presses J.
   Expected: the guest appears beside the host and the level changes for both. Log: `phase=warp`, then `phase=exit-ready`, then the transition's capture, restore and ready lines for two players.
4. In `c_start2`, the host walks into the exit back to `c_start` and waits. After 10 seconds the host's prompt changes to "(press J to leave without …)". The host presses J.
   Expected: the level changes; the guest is placed beside the host on arrival. Log: `phase=exit-forced`, then `phase=arrival-placed` or `spawn-placed` for the guest.
5. Check the console is quiet during a hold:
   `(Select-String -Path runtimes\enhanced\story-remote-host.log -Pattern 'CHANGE LEVEL:').Count`
   Expected: about one line per exit attempt, not hundreds.

Save the log excerpt to `evidence\stage3-exit-warp\02-exit.log`.

A scripted exit and a scripted teleport are not reachable in the first two maps. They are checked when play first meets one; note that in the handoff as still unverified live.

- [ ] **Step 6: Run the report**

Run: `& python tools\story_run_report.py | Tee-Object evidence\stage3-exit-warp\03-run-report.txt | Select-Object -Last 12`
Expected: the transitions from step 5 are `ok`; the party events include `exit-hold`, `exit-ready`, `warp`; one `warn … exit-forced` line from the escape hatch check; no PROBLEMS.

- [ ] **Step 7: Record the results and the rulings in the handoff**

Add a dated `## UPDATE` section at the top of `AGENT-HANDOFF-2026-09-27.md` containing:
- the engine hash and the test count;
- each live result from steps 4 and 5, with the evidence file names, and anything that did not match;
- "Still unverified live: a scripted exit replay; the scripted-teleport cut-off prompt";
- these rulings, each with its cost if wrong:
  - **The sender scope covers client commands only.** First-player lookups made from entity `Use` functions (objectives, `cof_keypad4`, code notes) still hit the host. Stage 4 mirrors that state instead.
  - **The waiter's prompt changes after 10 s** to say that J leaves without the partner. The spec did not word this; without it the escape hatch cannot be discovered.
  - **A held exit is stored once and released by the frame** for walk-in exits too, not only scripted ones. Some walk-in exits are fired by a trigger that may not ask again.
  - **The ending door is not covered by the cut-off prompt.** Its target is `endingcheck`, not an `inter_door_exit`, so the party code does not track it. J still works. Piece 4 owns the endings.
  - **A warp is allowed while the caller is frozen or in a camera scene**, as the spec says "any time". Piece 2 decides the rules for scenes.
  - **The remote friend's package is not rebuilt.** `friend_setup.py` binds J, but they need a new package with this engine before any of this reaches their PC.
- "Next: the stage 4 plan (shared codes, notes, objectives, ending flags, bag loss), using the two live facts from the earlier UPDATE."

- [ ] **Step 8: Commit the handoff**

```bash
git add AGENT-HANDOFF-2026-09-27.md
git commit -m "Record stage 3 live results for the exit hold and warp"
```
